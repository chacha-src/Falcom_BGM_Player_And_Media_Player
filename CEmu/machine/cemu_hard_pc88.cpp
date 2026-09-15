#include "StdAfx.h"
#include "cemu_hard_pc88.h"
#include "../cemu_zipfs.h"
#include "../cemu_rhythm.h"
#include "../chip/cemu_chip_opna.h"
#include "../z80/cemu_z80_bus.h"
#define BLARGG_LITTLE_ENDIAN 1
#include "../z80/Ay_Cpu.h"
#include <stdlib.h>
#include <string.h>

/* PC-88 音源専用モデル（hoot 互換）:
   - メイン RAM は平坦 64KB イメージ（mem_[0x10000]）。サブ RAM は未使用。
   - カタログ <rom type="code" offset="…"> はそのマップへそのままコピー。
   - 曲データ: mdata_addr／mfile_size／タイトルビット。音楽ファイルが code@mdata にもマップされているときはフルイメージを残しゲストに曲を選ばせる（ホストトランポリンやサンプル毎ベクタガードを発明しない）。
   - IM2 ベクタは固定: VRTC=02、RTC=04、SOUND=08、ページ (I<<8)。
   - カタログから use_rtc／use_vrtc を配送。OPN Timer → SOUND。 */

static CHardPc88* g_pc88Active = NULL;

/* CEmuParseOptHex の実装 */
static int CEmuParseOptHex(const CEmuGameEntry* ge, const char* name, int defVal)
{
	if (!ge || !name) return defVal;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, name) != 0) continue;
		const char* v = ge->opt[i].value;
		if (!v || !v[0]) return defVal;
		return (int)strtoul(v, NULL, 0);
	}
	return defVal;
}

/* 手元 bpoint.zip は DRIVER.BIN／MDAT*／MDATN*／BURNPCM を載せるがカタログ bpoint88 はまだ MUCO3／B0xx／ADPCM。Enix プレーヤがロードできるようリマップ。 */
static const unsigned char* CEmuPc88ZipFind(const CEmuZipFs* fs, const char* name,
	unsigned* outSize, int bgmOff, int preferMdatN)
{
	if (outSize) *outSize = 0;
	if (!fs || !name || !name[0]) return NULL;
	const unsigned char* data = CEmuZipFsFind(fs, name, outSize);
	if (data && (!outSize || *outSize > 0))
		return data;
	if (_stricmp(name, "MUCO3") == 0) {
		data = CEmuZipFsFind(fs, "DRIVER.BIN", outSize);
		if (data) return data;
	}
	if (_stricmp(name, "ADPCM") == 0) {
		data = CEmuZipFsFind(fs, "BURNPCM", outSize);
		if (data) return data;
	}
	if (bgmOff >= 0 && bgmOff < 256) {
		char alt[32];
		const int n = bgmOff + 1; /* カタログ 0x00 → MDAT01 */
		const char* first = preferMdatN ? "MDATN%02X" : "MDAT%02X";
		const char* second = preferMdatN ? "MDAT%02X" : "MDATN%02X";
		_snprintf_s(alt, _TRUNCATE, first, n);
		data = CEmuZipFsFind(fs, alt, outSize);
		if (data) return data;
		_snprintf_s(alt, _TRUNCATE, second, n);
		data = CEmuZipFsFind(fs, alt, outSize);
		if (data) return data;
	}
	return NULL;
}

CHardPc88::CHardPc88()
	: cmd(0)
	, param(0)
	, song(0)
	, soundIrqMasked(0)
	, useRtc(0)
	, useVrtc(0)
	, opnaMode(0)
	, cpuHz_(4000000)
	, cpu_(NULL)
	, chip_(NULL)
	, sampleRate_(44100)
	, cpuCycles_(0)
	, mdataAddr_(-1)
	, mdataSize_(0)
	, mfileSize_(0)
	, vdataAddr_(-1)
	, vfileSize_(0)
	, wolfteamMode_(0)
	, mucomBankCopy_(0)
	, mdataAddrDefaulted_(0)
	, packedKoei_(0)
	, initPc_(0)
	, forcePlayEi_(0)
	, mirrorSoundToRtc_(0)
	, armGineidenTimer_(0)
	, armLizardTimer_(0)
	, longPlayDrain_(0)
	, yaksaPatch2_(0)
	, armNavituneTimer_(0)
	, naviSongAddr_(0)
	, deferRtcAfterPlay_(0)
	, n88RtcIsr_(0)
	, n88RtcThrottleAddr_(0)
	, hardrankSb2_(0)
	, schemeMode_(0)
	, falcomType_(0)
	, playKickBase_(0)
	, playKickInitOff_(0)
	, playKickEi_(1)
	, titleCode_(0)
{
	hardKind = KIND_PC88;
	memset(mem_, 0, sizeof(mem_));
	memset(ioPorts_, 0, sizeof(ioPorts_));
	textWinHi_ = 0x80; /* 窓閉じ = 8000-83FF の素のメイン RAM */
	memset(textWinShadow_, 0, sizeof(textWinShadow_));
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(progBank_, 0, sizeof(progBank_));
	memset(progBankSize_, 0, sizeof(progBankSize_));
	memset(voiceBank_, 0, sizeof(voiceBank_));
	memset(voiceBankSize_, 0, sizeof(voiceBankSize_));
}

CHardPc88::~CHardPc88()
{
	Shutdown();
}

/* PCM／コードバンク */
void CHardPc88::FreeBanks()
{
	for (int i = 0; i < 256; i++) {
		if (bgmBank_[i]) { free(bgmBank_[i]); bgmBank_[i] = NULL; }
		if (progBank_[i]) { free(progBank_[i]); progBank_[i] = NULL; }
		if (voiceBank_[i]) { free(voiceBank_[i]); voiceBank_[i] = NULL; }
		bgmBankSize_[i] = 0;
		progBankSize_[i] = 0;
		voiceBankSize_[i] = 0;
	}
}

/* PCM／コードバンク */
static int CEmuPc88HasSongBank(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->romCount; i++) {
		const char* t = ge->rom[i].type;
		if (!t || !t[0]) continue;
		if (_stricmp(t, "bgm") == 0 || _stricmp(t, "voice") == 0 || _stricmp(t, "song") == 0)
			return 1;
	}
	return 0;
}

/* カタログが mdata_addr を省略してもタイトルビット 16..31 が絶対ロードページを符号化（ashe 0x1000/0x1100、meltdown/refight MMLEX 0x9400/0xa900、galfstrm 0x6800/0xe000）。既定 0x4000 だと HL が見ない所へバンクを載せる。wolfteam/mfile（vdata またはパックオフセット）と KOEI 風 mid バイトは飛ばす。ashe DRIVER@7800 は 0x1000 だけ参照 — タイトル 0x11xxxxxx はそのページを共有（上位バイトは再生／モードであり第 2 ロード窓ではない）。 */
static int CEmuPc88TitleEncodedMdata(unsigned titleCode, int mdataDefaulted, int wolfteam)
{
	if (!mdataDefaulted || wolfteam)
		return -1;
	if ((titleCode & 0xff00u) != 0)
		return -1; /* bits 8..15 使用（KOEI ファイルオフセット／他パック） */
	unsigned hi = (titleCode >> 16) & 0xffffu;
	if (hi < 0x1000 || hi > 0xE000 || (hi & 0xffu) != 0)
		return -1;
	if (hi >= 0x1000 && hi < 0x1200)
		hi = 0x1000;
	return (int)hi;
}

/* ashe 系 MUS: 先頭チャネル数のあと絶対 ptr ワード。上位バイトがリンクページに集まる（MUS09→0x93xx）。タイトル hi16 0x10/0x11 は再生／モードでありリンク番地ではない — そこに載せると ptr がぶら下がる。 */
static int CEmuPc88InferMusLinkAddr(const unsigned char* data, unsigned len)
{
	if (!data || len < 8)
		return -1;
	const unsigned nch = data[0];
	if (nch < 1 || nch > 16 || 1 + nch * 4 > len)
		return -1;
	unsigned hist[256];
	memset(hist, 0, sizeof(hist));
	unsigned hits = 0;
	unsigned minP = 0xFFFF, maxP = 0;
	for (unsigned i = 0; i < nch; i++) {
		const unsigned ptr = (unsigned)data[1 + i * 4]
			| ((unsigned)data[2 + i * 4] << 8);
		if (ptr < 0x2000 || ptr >= 0xE000)
			return -1;
		if (ptr < minP) minP = ptr;
		if (ptr > maxP) maxP = ptr;
		hist[ptr >> 8]++;
		hits++;
	}
	if (hits < 3)
		return -1;
	/* MUS00 は 6 ch ptr を A0/A1/A2 に散らす（各 2）のでページ多数決が立たない。フレーズはまだ 1 つの 8K 窓。256B 整列（MUS10 は 4K 丸め 9000 ではなく 9300 にリンク）。 */
	if (maxP >= minP && (maxP - minP) < 0x2000)
		return (int)(minP & 0xFF00);
	unsigned bestPage = 0, bestCount = 0;
	for (unsigned p = 0; p < 256; p++) {
		if (hist[p] > bestCount) {
			bestCount = hist[p];
			bestPage = p;
		}
	}
	if (bestCount * 2 < hits)
		return -1;
	return (int)(bestPage << 8);
}

/* ashe 93xx バンクは titleMdata に載せる（DRIVER がネイティブ 9000 を占める）。ヘッダリロケが 4 バイト ch ptr を直す。8188 はその ptr のワードを読む: 上位 bit7 = その場コマンドストリーム。さもなくばフレーズリスト（レコード上の絶対 93xx、またはリストへの小さなオフセット）。リスト（と FFFF ループワード）もリロケ。MUS00 はネイティブ A000 — loadAddr==linkBase のとき飛ばす。 */
static void CEmuPc88AsheRelocPhrases(uint8_t* mem, int loadAddr, unsigned n,
	unsigned linkBase)
{
	if (!mem || loadAddr < 0 || n < 8 || linkBase < 0x2000
		|| linkBase >= 0xE000)
		return;
	const unsigned nch = mem[loadAddr];
	if (nch < 1 || nch > 16 || 1 + nch * 4 > n)
		return;
	unsigned hdrEnd = 1 + nch * 4;
	if (n >= 0x19 + 5
		&& mem[loadAddr + 0x19] >= 3 && mem[loadAddr + 0x19] <= 16) {
		const unsigned n2 = mem[loadAddr + 0x19];
		const unsigned e2 = 0x19 + 1 + n2 * 4;
		if (e2 > hdrEnd && e2 <= n)
			hdrEnd = e2;
	}
	unsigned chPtr[16];
	unsigned chOff[16];
	unsigned nList = 0;
	for (unsigned c = 0; c < nch && nList < 16; c++) {
		const unsigned po = 1 + c * 4;
		unsigned w = (unsigned)mem[loadAddr + (int)po]
			| ((unsigned)mem[loadAddr + (int)po + 1] << 8);
		if (w >= (unsigned)loadAddr && w < (unsigned)loadAddr + n) {
			chOff[nList] = po;
			chPtr[nList] = w;
			nList++;
		}
	}
	for (unsigned i = 0; i < nList; i++) {
		const unsigned p = chPtr[i];
		if (p + 1 >= (unsigned)loadAddr + n)
			continue;
		unsigned w = (unsigned)mem[p]
			| ((unsigned)mem[p + 1] << 8);
		/* 93xx/94xx フレーズ ptr は bit15 セット — それはその場 C1..FE コマンドストリームではない（それらはリンク窓の外）。 */
		unsigned list = 0;
		if (w >= linkBase && w < linkBase + n) {
			/* チャネルレコードは既にフレーズリスト（MUS09/10 @ 9300） */
			list = p;
		} else if (w >= hdrEnd && w < n) {
			const unsigned dest = (unsigned)loadAddr + w;
			if (dest + 1 >= (unsigned)loadAddr + n)
				continue;
			const unsigned peek = (unsigned)mem[dest]
				| ((unsigned)mem[dest + 1] << 8);
			if (peek >= linkBase && peek < linkBase + n) {
				list = dest;
				mem[loadAddr + (int)chOff[i]] = (uint8_t)(dest & 0xff);
				mem[loadAddr + (int)chOff[i] + 1] = (uint8_t)((dest >> 8) & 0xff);
			} else if (p + 5 < (unsigned)loadAddr + n) {
				mem[p] = (uint8_t)(dest & 0xff);
				mem[p + 1] = (uint8_t)((dest >> 8) & 0xff);
				mem[p + 2] = 0xFF;
				mem[p + 3] = 0xFF;
				mem[p + 4] = (uint8_t)(p & 0xff);
				mem[p + 5] = (uint8_t)((p >> 8) & 0xff);
				continue;
			}
		}
		if (!list)
			continue;
		for (unsigned q = list; q + 1 < (unsigned)loadAddr + n; q += 2) {
			unsigned v = (unsigned)mem[q]
				| ((unsigned)mem[q + 1] << 8);
			if (v == 0)
				break;
			if (v == 0xFFFF) {
				if (q + 3 < (unsigned)loadAddr + n) {
					unsigned lp = (unsigned)mem[q + 2]
						| ((unsigned)mem[q + 3] << 8);
					if (lp >= linkBase && lp < linkBase + n)
						lp = lp - linkBase + (unsigned)loadAddr;
					else if (lp >= hdrEnd && lp < n)
						lp = (unsigned)loadAddr + lp;
					if (lp + 1 < (unsigned)loadAddr + n) {
						const unsigned peek = (unsigned)mem[lp]
							| ((unsigned)mem[lp + 1] << 8);
						const int ok = (peek >= (unsigned)loadAddr
								&& peek < (unsigned)loadAddr + n)
							|| (peek >= linkBase && peek < linkBase + n);
						if (!ok)
							lp = list;
					}
					mem[q + 2] = (uint8_t)(lp & 0xff);
					mem[q + 3] = (uint8_t)((lp >> 8) & 0xff);
				}
				break;
			}
			if (v >= linkBase && v < linkBase + n) {
				v = v - linkBase + (unsigned)loadAddr;
				mem[q] = (uint8_t)(v & 0xff);
				mem[q + 1] = (uint8_t)((v >> 8) & 0xff);
			}
		}
	}
}

static int CEmuPc88PatchSongShift8(const uint8_t* mem);
static int CEmuPc88PatchLdirFrom4000(const uint8_t* mem);
static int CEmuPc88PatchFalcomAndF0Cp10(const uint8_t* mem);
static int CEmuPc88PatchFalcomLdirFromC000(const uint8_t* mem);
static unsigned CEmuPc88FalcomYs2MusDest(const uint8_t* mem, unsigned bank);
static void CEmuPc88FalcomYs2PlantTable(uint8_t* mem, unsigned bank);
static int CEmuPc88AshePlayHi(const uint8_t* mem);
static void CEmuPc88PlantAsheSongTable(uint8_t* mem, unsigned songNum, int mdataAddr);
static int CEmuPc88PatchManreq(const uint8_t* mem);
static int CEmuPc88PatchGandhara(const uint8_t* mem);
static int CEmuPc88PatchGinei2(const uint8_t* mem);
static int CEmuPc88PatchXzrA4(const uint8_t* mem);
static int CEmuPc88PatchXzr2VoiceF000(const uint8_t* mem);
static int CEmuPc88PatchAfHl4400(const uint8_t* mem);
static int CEmuPc88PatchRomanciaSr(const uint8_t* mem, int initPc);
static int CEmuPc88PatchRobowr(const uint8_t* mem);
static void CEmuPc88PlantRobowrSong1(uint8_t* mem);
static int CEmuPc88PatchHarakiriMplay(const uint8_t* mem);
static int CEmuPc88PatchXanadu80sr(const uint8_t* mem, int initPc);
static int CEmuPc88PatchHardrankSb2(const uint8_t* mem);
static void CEmuPc88SkipHardrankLeadRest(uint8_t* mem);
static void CEmuPc88PlantIceclimbTitleLoop(uint8_t* mem);

/* JR/JR cc 変位 0xFB は EI オペコードではない — p1demo/castle は `JR Z,$` で FB をオフセットに符号化し NeedsBootEiPulse が中断していた。 */
static int CEmuPc88IsJrDisp(const uint8_t* mem, int at)
{
	if (!mem || at <= 0) return 0;
	const uint8_t prev = mem[at - 1];
	return prev == 0x18 || prev == 0x20 || prev == 0x28
		|| prev == 0x30 || prev == 0x38;
}

/* チップと CPU を生成する */
int CHardPc88::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	/* 8801-10 = Sound Board II（OPNA）。特殊 Falcom タイプはカタログで OPN にエイリアス */
	opnaMode = (_stricmp(ge->subtype, "opna") == 0
		|| _stricmp(ge->subtype, "8801-10") == 0) ? 1 : 0;
	mdataAddr_ = CEmuParseOptHex(ge, "mdata_addr", -1);
	mdataSize_ = CEmuParseOptHex(ge, "mdata_size", 0);
	mfileSize_ = CEmuParseOptHex(ge, "mfile_size", 0);
	vdataAddr_ = CEmuParseOptHex(ge, "vdata_addr", -1);
	vfileSize_ = CEmuParseOptHex(ge, "vfile_size", 0);
	if (vfileSize_ <= 0 && vdataAddr_ >= 0)
		vfileSize_ = CEmuParseOptHex(ge, "vdata_size", 0x1000);
	wolfteamMode_ = (vdataAddr_ >= 0 || mfileSize_ > 0) ? 1 : 0;
	mucomBankCopy_ = 0;
	if (_stricmp(ge->subtype, "muco") == 0 || _stricmp(ge->subtype, "mucom88") == 0)
		mucomBankCopy_ = 1;
	else if (ge->driverAlias[0]) {
		char aliasA[CEMU_GAME_NAME];
		WideCharToMultiByte(CP_ACP, 0, ge->driverAlias, -1, aliasA, (int)sizeof(aliasA), NULL, NULL);
		if (_strnicmp(aliasA, "mucom", 5) == 0)
			mucomBankCopy_ = 1;
	}
	/* カタログはしばしば mdata_addr を省略。PATCH はまだ HL=0x4000 をロード（hoot/wolfteam、大半の silent-FAIL セット）。既定が無いと LoadSongData が no-op。Falcom type=prog＋bgm（xana2）: 曲バンクは約 0x5C00 にリンク（m.* ヘッダは 5Cxx を指す）。既定 0x4000 は prog イメージの下に居て OUT(02) マップで消える。 */
	mdataAddrDefaulted_ = 0;
	if (mdataAddr_ < 0) {
		int hasProg = 0;
		for (int i = 0; i < ge->romCount; i++) {
			if (_stricmp(ge->rom[i].type, "prog") == 0) {
				hasProg = 1;
				break;
			}
		}
		if (hasProg && CEmuPc88HasSongBank(ge)) {
			mdataAddr_ = 0x5C00;
			mdataAddrDefaulted_ = 1;
		} else if (wolfteamMode_ || CEmuPc88HasSongBank(ge)) {
			mdataAddr_ = 0x4000;
			mdataAddrDefaulted_ = 1;
		}
	}
	/* KOEI FMDRV.SYS 系統: パック CIM、既定 mdata 0x4000。再生は E=0（生ポインタ）。下位タイトルバイトはバンクであり FMDRV 曲ではない。carmine88 は mfile/mdata サイズを共有するが MUSIC@A000 がポート 01 にタイトル下位バイトを要し KOEI 扱いしない。 */
	{
		int musicA000 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0xa000 && _stricmp(r->name, "MUSIC") == 0)
				musicA000 = 1;
		}
		packedKoei_ = (!mucomBankCopy_ && mdataAddrDefaulted_ && mfileSize_ > 0
			&& mdataSize_ > 0 && !musicA000) ? 1 : 0;
	}
	initPc_ = CEmuParseOptHex(ge, "init_pc", 0);
	schemeMode_ = 0;
	falcomType_ = 0;
	yaksaPatch2_ = 0;
	armLizardTimer_ = 0;
	longPlayDrain_ = 0;
	/* BOTHTEC Scheme OPNA を検出: MUS2＋ADR_（＋INT2）。カタログはしばしば PATCH を 0000 に置き SP=0100 下でスタックを壊す。hoot は 0x9000。 */
	{
		int hasMus2 = 0, hasAdr = 0, hasInt2 = 0, patchAt0 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (_stricmp(r->name, "MUS2") == 0) hasMus2 = 1;
			if (_stricmp(r->name, "ADR_") == 0 || _stricmp(r->name, "ADR") == 0)
				hasAdr = 1;
			if (_stricmp(r->name, "INT2") == 0) hasInt2 = 1;
			if (_stricmp(r->name, "PATCH") == 0 && r->offset == 0)
				patchAt0 = 1;
		}
		if (opnaMode && hasMus2 && hasAdr) {
			schemeMode_ = 1;
			if (patchAt0 && initPc_ == 0)
				initPc_ = 0x9000;
			/* hoot scheme.cpp は TIMER_INT をコメントアウトのまま */
			useRtc = 0;
			/* カタログは scheme を mucom88 の下にエイリアスするが、hoot scheme.cpp は Timer A と B で音源 IRQ を上げる（mucom の Timer-B のみと違う）。ポート 0 は BGM バンクロードであり mucom [5C/5D] バンクコピーではない。 */
			mucomBankCopy_ = 0;
		}
		(void)hasInt2;
	}
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "use_rtc") == 0 && strtoul(ge->opt[i].value, NULL, 0))
			useRtc = 1;
		if (_stricmp(ge->opt[i].name, "use_vrtc") == 0 && strtoul(ge->opt[i].value, NULL, 0))
			useVrtc = 1;
	}
	/* Game Arts THEXDER 系統 — 割り込みページは音楽イメージが植えるものと一致必須（完全ゲーム／OS は無くレイアウトがすべて）:
	     thexder88: DEMOM LD (F302),HL＋カタログ use_vrtc
	     thexder: DEMOM LD (F304),HL＋N88_3@6000＋カタログ use_rtc
	     bokosuka: MUSIC LD (F304),HL＋N88_3@6000＋カタログ use_rtc
	   N88 パックを VRTC に切らない — PATCH stub に F302 が残り本物プレーヤが走らなかった。DEMOM@9000 のみ（N88 無し）は thexder88 と同様 VRTC を優先。N88 パックはホスト EI が要る: bokosuka poll JR は disp=FBh（オペコード EI ではない）で play が DI を残し得る。 */
	{
		int hasN88 = 0, hasDemom9000 = 0, hasMusicC000 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (_strnicmp(r->name, "N88", 3) == 0 || _strnicmp(r->name, "n88", 3) == 0)
				hasN88 = 1;
			if (r->offset == 0x9000 && (_stricmp(r->name, "DEMOM") == 0
				|| _stricmp(r->name, "demom") == 0))
				hasDemom9000 = 1;
			if (r->offset == 0xC000 && (_stricmp(r->name, "MUSIC") == 0
				|| _stricmp(r->name, "music") == 0))
				hasMusicC000 = 1;
		}
		if (hasN88 && (hasDemom9000 || hasMusicC000)) {
			/* ブート／play 中は RTC を切り、最初の tick 前に DEMOM/MUSIC が LD (F304),HL で本物プレーヤ（C1D1／C183）へ載せられるようにする。早い RTC は E80E stub に着地し PC がそこに楔（wr≈0）。 */
			useRtc = 0;
			useVrtc = 0;
			deferRtcAfterPlay_ = 1;
			forcePlayEi_ = 1;
			/* これらのパックのポートコマンド再生は F304 植込前に N88/MUSIC 末尾へ迷う。ホストが DEMOM/MUSIC 再生入口を直接キック（thexder88 の PATCH も fmdtex 後 CALL C000 で同じ）。 */
			if (hasDemom9000) {
				playKickBase_ = 0xC000; /* DEMOM イメージは C000 へまたがる */
				playKickInitOff_ = 0;
				playKickEi_ = 0; /* DI 下で植える。延期後に RTC＋EI */
				n88RtcIsr_ = 0xC1D1;
				n88RtcThrottleAddr_ = 0xD1D2;
			} else if (hasMusicC000) {
				playKickBase_ = 0xC00F; /* (8FCE) 上の C000 RET Z を飛ばす */
				playKickInitOff_ = 0;
				playKickEi_ = 0;
				n88RtcIsr_ = 0xC183;
				n88RtcThrottleAddr_ = 0xC5B0;
			}
		} else if (useRtc && !useVrtc && hasDemom9000 && !hasN88) {
			useRtc = 0;
			useVrtc = 1;
			forcePlayEi_ = 1;
		}
	}
	/* castle/castleex: PROG2 は音楽 ISR を I:04（RTC）に植え、OPN ベクタ 08 ではない。カタログ use_rtc は残す — クリアすると tick 源無しで I=$1A が武装（MUSIC@F800 は無傷、key=0）。 */
	/* Scheme: カタログが use_rtc を立てることがある。切ったまま（上記） */
	if (schemeMode_)
		useRtc = 0;
	/* Hoot mucom88 TIMER_INT（RTC）は Sorcerian 用にコメントアウト。カタログ pc88 変換が use_rtc を足し、配送すると BIOS ディスク待ち 05EE へベクタし無音のまま。OK 兄弟は use_rtc を省略。 */
	if (useRtc) {
		int hasBios = 0, hasSedat = 0, patchC000 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (_stricmp(r->name, "BIOS") == 0 && r->offset == 0) hasBios = 1;
			if (strstr(r->name, "SEDAT") || strstr(r->name, "sedat")) hasSedat = 1;
			if (_stricmp(r->name, "PATCH") == 0 && r->offset == 0xc000) patchC000 = 1;
		}
		if (hasBios && hasSedat && patchC000)
			useRtc = 0;
	}
	/* Game Arts（solitair／jikochu*）: PATCH はプレーヤ ISR を IM2 ベクタ 04（RTC）に植え、08（OPN）ではない。カタログ use_rtc は残す — クリアすると EI 下で tick 源無しに play が武装。例外: PLAY88/C000 への CALL 中の RTC は ISR に再入し mute。RTC は切ったまま。DirectPlayKick が init(+6) のあと OPN 無しホスト pacing で基点を走る。 */
	/* jikochu*: 唯一の音楽イメージが code@mdata かつ bgm バンク */
	if (useRtc && mdataAddr_ >= 0) {
		const char* codeAtMdata = NULL;
		int bgmSame = 0, codeRoms = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") == 0) {
				codeRoms++;
				if (r->offset == mdataAddr_ && r->name)
					codeAtMdata = r->name;
			}
		}
		if (codeAtMdata && codeRoms <= 2) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "bgm") == 0 && r->name
					&& _stricmp(r->name, codeAtMdata) == 0)
					bgmSame = 1;
			}
			if (bgmSame)
				useRtc = 0;
		}
	}
	/* jikochu3: 音楽は code@C000 のみ（bgm 無し）。同じ I=0 PATCH */
	if (useRtc) {
		int codeC000 = 0, codeRoms = 0, bgmRoms = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") == 0) {
				codeRoms++;
				if (r->offset == 0xc000) codeC000 = 1;
			} else if (_stricmp(r->type, "bgm") == 0)
				bgmRoms++;
		}
		if (codeC000 && codeRoms <= 2 && bgmRoms == 0
			&& (mdataAddr_ < 0 || mdataAddrDefaulted_))
			useRtc = 0;
	}
	/* solitair: PLAY88@6000 — RTC 再入が mute。キック経路が置換 */
	if (useRtc && !useVrtc) {
		int hasPlay88 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (_strnicmp(r->name, "PLAY88", 6) == 0)
				hasPlay88 = 1;
		}
		if (hasPlay88)
			useRtc = 0;
	}
	/* rogueal: カタログ use_rtc。ブート CALL 0868 のページクリア LDIR（BC=5BB5 @08B0）中の RTC が終わらない（PC が 08B3 で固まる）。play が MUS* @8000 を載せるまで RTC を延期 — vec04=0091 がプレーヤ ISR。 */
	if (useRtc && !useVrtc && initPc_ == 0xf000) {
		int prog0 = 0, patchF000 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0 && _strnicmp(r->name, "PROG", 4) == 0)
				prog0 = 1;
			if (r->offset == 0xf000 && _strnicmp(r->name, "PATCH", 5) == 0)
				patchF000 = 1;
		}
		if (prog0 && patchF000 && mdataAddr_ == 0x8000
			&& CEmuPc88HasSongBank(ge)) {
			useRtc = 0;
			deferRtcAfterPlay_ = 1;
			forcePlayEi_ = 1;
		}
	}
	if (useRtc && !useVrtc) {
		int demo100 = 0, driver81 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x0100 && _strnicmp(r->name, "DEMO", 4) == 0)
				demo100 = 1;
			if (r->offset == 0x8100 && _strnicmp(r->name, "DRIVER", 6) == 0)
				driver81 = 1;
		}
		if (demo100 && driver81) {
			/* p1demo1: DEMO1A@0100 はビットマップ。PATCH CALL 01AE は使えない。VRTC＋EI を優先し I=0 が RTC をビットマップへベクタしないようにする。LoadRoms はバイナリが合うとき CALL 96EE→OPN@84EE を植える。曲／チャネル RAM @9A5B–A07F はまだリップに無い（DRIVER EOF @9A00）— .cursor/_cemu_p1demo1_blocker.txt 参照。 */
			useRtc = 0;
			useVrtc = 1;
			forcePlayEi_ = 1;
		}
	}
	/* byouin_88: MUSIC.SYS@9C00 プレーヤ。PATCH poll に EI があるがドライバは I=F3 を植え RTC tick が要る。カタログは use_rtc を省略。 */
	if (!useRtc && !useVrtc) {
		int music9c = 0, patch0 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x9c00 && (_strnicmp(r->name, "MUSIC", 5) == 0
				|| _stricmp(r->name, "MUSIC.SYS") == 0))
				music9c = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patch0 = 1;
		}
		if (music9c && patch0)
			useRtc = 1;
	}
	/* gineiden: DEMO@0100＋AMAIN@0480。Play は IM2 音源 ISR を AMAIN（vec08）へ植えるが vec02/04 は空 — VRTC/RTC は no-op。RTC を使い play 後に音源→RTC をミラー（FixupIm2AfterPlay 参照）。 */
	if (!useRtc && !useVrtc) {
		int demo100 = 0, amain480 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x0100 && _strnicmp(r->name, "DEMO", 4) == 0)
				demo100 = 1;
			if (r->offset == 0x0480 && _strnicmp(r->name, "AMAIN", 5) == 0)
				amain480 = 1;
		}
		if (demo100 && amain480) {
			useRtc = 0;
			useVrtc = 0;
			mirrorSoundToRtc_ = 0;
			/* PATCH CALL 0080 → 4E2F がタイマをクリア。4E00 が許可。ホストが play 後に Timer B を再武装（ArmGineidenOpnTimer 参照）。 */
			armGineidenTimer_ = 1;
		}
	}
	/* navitune 系: PATCH@init_pc＋音楽 code@mdata＋一致 bgm バンク。タイトルビット 8..23 はそのイメージ内の曲オフセット（ApplyNavituneTitleSong）。 */
	if (!armGineidenTimer_ && initPc_ == 0x0100 && mdataAddr_ == 0x7700) {
		int hasNavi = 0, hasPrg = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (!r->name) continue;
			if (_strnicmp(r->name, "navimus", 7) == 0) hasNavi = 1;
			if (_strnicmp(r->name, "naviprg", 7) == 0) hasPrg = 1;
		}
		if (hasNavi && hasPrg)
			armNavituneTimer_ = 1;
	}
	/* lizard88: カタログ RTC/VRTC 無し — プレーヤは OPN Timer B @vec08 で進む。PATCH ブートがタイマを武装したあと play CALL 9F0F がクリアし得る。gineiden と同様 cmd=1 後に再武装。 */
	if (!armGineidenTimer_ && !useRtc && !useVrtc) {
		int main9f = 0, patch0 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x9f00 && _strnicmp(r->name, "MAIN", 4) == 0)
				main9f = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patch0 = 1;
		}
		if (main9f && patch0)
			armLizardTimer_ = 1;
	}
	/* yaksa PATCH2: DI 下で play。vdata+voiceBank は port01 をゼロにする — EI を強制し曲 ID を param に残す（PlaySongIndex 参照）。 */
	if (useVrtc) {
		int patch2 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0 && _stricmp(r->name, "PATCH2") == 0)
				patch2 = 1;
		}
		if (patch2) {
			forcePlayEi_ = 1;
			yaksaPatch2_ = 1;
		}
	}
	/* 1942_88: ADEE LDIR は A343 が I＋Timer を武装する前に長い cmd=1 ドレインが要る */
	if (!useRtc && !useVrtc) {
		int progA3 = 0, musicF0 = 0, patch0 = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0xa300 && _strnicmp(r->name, "PROG", 4) == 0)
				progA3 = 1;
			if (r->offset == 0xf000 && _strnicmp(r->name, "MUSIC", 5) == 0)
				musicF0 = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patch0 = 1;
		}
		if (progA3 && musicF0 && patch0) {
			longPlayDrain_ = 1;
			forcePlayEi_ = 1; /* A343 後の Timer IM2 はホスト EI が要る */
		}
	}
	/* RTC と VRTC の両方を求めるタイトルは本気。実際にプレーヤを進めるのは IM2 表であり ROM 名ではない — stub が表を植えたあと PruneDeadTickSources。
	   （yokosuka: vec02→$0052 CALL SOUND+$0B0A、vec04→$B7EE。旧「SOUND @≥B000 なら VRTC を落とす」は前者を殺し、曲が約 1 秒で死んだ。） */
	/* spitfl88 / tf88sr: PROG のみ + PATCH@0 + init_pc 無し。IM2 は本物プレーヤを RTC（vec04）へ。VRTC/SOUND 枠は RET stub
	   （spitfl A820=C9、tf88 409E=ack+RET）。カタログ RTC を残し EI 強制 — VRTC へ切ると確実に mute。hangon88/plazmasr は init_pc がありこのフィルタに入らない。 */
	if (useRtc && !useVrtc) {
		int codeRoms = 0, bgmRoms = 0, hasProg = 0, patchAt0 = 0;
		int hasInitPc = 0, patchAtInit = 0;
		for (int i = 0; i < ge->optCount; i++) {
			if (_stricmp(ge->opt[i].name, "init_pc") == 0)
				hasInitPc = 1;
		}
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "bgm") == 0) bgmRoms++;
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			codeRoms++;
			if (_strnicmp(r->name, "PROG", 4) == 0) hasProg = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patchAt0 = 1;
			if (hasInitPc && initPc_ >= 0 && r->offset == initPc_
				&& _strnicmp(r->name, "PATCH", 5) == 0)
				patchAtInit = 1;
		}
		if (!hasInitPc && hasProg && patchAt0 && bgmRoms == 0 && codeRoms <= 2) {
			/* useRtc は残す。PATCH は poll 前に EI が無い */
			forcePlayEi_ = 1;
		}
		/* hangon88: PATCH@init_pc (8F00) は IM2+poll、EI オペコード無し — DI 下では RTC ISR が走らない。spitfl の forcePlayEi と同じ契約。 */
		if (patchAtInit && hasProg && bgmRoms == 0)
			forcePlayEi_ = 1;
	}
	/* 直接再生キック: Game Arts JR PATCH は player+6 を CALL し、RTC ISR（vec04）が player+0 を CALL。RTC が無いときはホストが同じ init(+6) のあと基点 CALL。
	   castle/castleex: 実入口は PROG2@1000（PATCH の CALL 1033 だけでは無音）。 */
	{
		int play88 = 0, codeC000 = 0, codeRoms = 0, voiceF000 = 0, prog1000 = 0;
		int patchJr = 0, soundKick = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			codeRoms++;
			if (_strnicmp(r->name, "PLAY88", 6) == 0 && r->offset == 0x6000)
				play88 = 1;
			if (r->offset == 0xc000) codeC000 = 1;
			if (r->offset == 0 && _strnicmp(r->name, "PATCH", 5) == 0)
				patchJr = 1; /* 必要なら LoadRoms で JR を確認 */
			if (r->offset == 0xf000 && _strnicmp(r->name, "VOICE", 5) == 0)
				voiceF000 = 1;
			if (r->offset == 0x1000 && _strnicmp(r->name, "PROG", 4) == 0)
				prog1000 = 1;
			/* 正確な "SOUND" のみ — SOUND1@D800（makai88）で B780（空 RAM）をキックしてはいけない。曲メールボックス 0274=0 のままになった。 */
			if (r->offset >= 0xb000 && r->offset < 0xe000
				&& _stricmp(r->name, "SOUND") == 0)
				soundKick = (int)r->offset + 0x1BD; /* B5C3+0x1BD → B780（SOUND 再生入口） */
		}
		if (play88) {
			playKickBase_ = 0x6000;
			playKickInitOff_ = 6;
			playKickEi_ = 1;
		} else if (codeC000 && patchJr && codeRoms <= 2) {
			/* jikochu*: PATCH@0 + 音楽イメージ C000（C3 ジャンプ表） */
			playKickBase_ = 0xC000;
			playKickInitOff_ = 6;
			playKickEi_ = 1;
		} else if (voiceF000 && prog1000) {
			/* castle/castleex: PROG2@1000。OPN Timer IM2 のため EI 必須 — playKickEi=0 だと iff1=0 のまま（SSG ノイズのみ、key=0）。 */
			playKickBase_ = 0x1000;
			playKickInitOff_ = 0;
			playKickEi_ = 1;
			forcePlayEi_ = 1;
		} else if (soundKick && (useRtc || useVrtc)) {
			/* yokosuka: PATCH play は DI。CALL SOUND+0x1BD のあと RTC tick */
			playKickBase_ = soundKick;
			playKickInitOff_ = 0;
			playKickEi_ = 1;
		}
	}
	/* カタログは clockmul と clock_mul の両方を使う */
	int clockmul = CEmuParseOptHex(ge, "clockmul", 0);
	if (clockmul <= 0)
		clockmul = CEmuParseOptHex(ge, "clock_mul", 1);
	if (clockmul < 1 || clockmul > 64) clockmul = 1;
	cpuHz_ = 4000000 * clockmul;
	const uint32_t clk = opnaMode ? 7987200u : 3993600u;
	chip_ = CEmuChipYm2608Create(clk, opnaMode, sampleRate_);
	/* hoot mucom88: Timer A 溢れで Z80 音源 IRQ を上げてはいけない（Timer B のみ）。他 PC88 ドライバ（KOEI/wolfteam 等）は A+B。
	   SEDAT+BIOS ブートも subtype が素の opn でも mucom 扱い。 */
	if (chip_) {
		/* scheme OPNA: 常に A+B（上記 schemeMode_） */
		int timerBOnly = (!schemeMode_ && mucomBankCopy_) ? 1 : 0;
		if (!timerBOnly && !schemeMode_) {
			int hasBios = 0, hasSedat = 0;
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "code") != 0 || !r->name) continue;
				if (_stricmp(r->name, "BIOS") == 0 && r->offset == 0) hasBios = 1;
				if (strstr(r->name, "SEDAT") || strstr(r->name, "sedat")) hasSedat = 1;
			}
			if (hasBios && hasSedat)
				timerBOnly = 1;
		}
		if (timerBOnly)
			chip_->SetTimerIrqPolicy(0);
	}
	cpu_ = new Ay_Cpu();
	return chip_ && cpu_ ? 1 : 0;
}

/* チップ／CPU／ROM を破棄する */
void CHardPc88::Shutdown()
{
	if (g_pc88Active == this)
		g_pc88Active = NULL;
	if (CEmuZ80BusGetActive() == this)
		CEmuZ80BusSetActive(NULL);
	FreeBanks();
	if (cpu_) { delete cpu_; cpu_ = NULL; }
	if (chip_) { CEmuChipYm2608Destroy(chip_); chip_ = NULL; }
}

/* バンク／BGM を載せる */
void CHardPc88::StageBanks(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge) return;
	FreeBanks();
	const int preferMdatN = opnaMode || CEmuParseOptHex(ge, "use_pcmx8", 0);
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const int bgmOff = (_stricmp(r->type, "bgm") == 0) ? r->offset : -1;
		const unsigned char* data = CEmuPc88ZipFind(fs, r->name, &sz, bgmOff, preferMdatN);
		if (!data || !sz) continue;
		if (_stricmp(r->type, "bgm") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			bgmBank_[idx] = (unsigned char*)malloc(sz);
			if (bgmBank_[idx]) {
				memcpy(bgmBank_[idx], data, sz);
				bgmBankSize_[idx] = sz;
			}
		} else if (_stricmp(r->type, "voice") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			voiceBank_[idx] = (unsigned char*)malloc(sz);
			if (voiceBank_[idx]) {
				memcpy(voiceBank_[idx], data, sz);
				voiceBankSize_[idx] = sz;
			}
		}
		/* Falcom 特殊: type=prog バンク（APRG/SOUND/PR.NO*）。タイトル bit 8..15 がバンク。OUT (02),song でホストがマップ。 */
		else if (_stricmp(r->type, "prog") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			progBank_[idx] = (unsigned char*)malloc(sz);
			if (progBank_[idx]) {
				memcpy(progBank_[idx], data, sz);
				progBankSize_[idx] = sz;
			}
		} else if (_stricmp(r->type, "adpcm") == 0 && chip_ && r->offset >= 0) {
			/* hoot type=adpcm → YM2608 ADPCM-B 外部 RAM（scheme V_* は最大約 0x3e000、valis2 は約 235KB）。ADPCM-A に入れない:
			   A は固定 8KiB リズム ROM（ym2608_adpcm_rom.bin）。 */
			if (!getenv("CEMU_SKIP_ADPCM_B"))
				chip_->SetAdpcmB(data, sz, (unsigned)r->offset);
		}
	}
	/* カタログ bgm offset 0 は最初の ZipFind を外し得る。空きスロットを埋める。 */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "bgm") != 0 || r->offset < 0 || r->offset >= 256)
			continue;
		if (bgmBank_[r->offset] && bgmBankSize_[r->offset] >= 16)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuPc88ZipFind(fs, r->name, &sz, r->offset, preferMdatN);
		if (!data || sz < 16) continue;
		if (bgmBank_[r->offset]) {
			free(bgmBank_[r->offset]);
			bgmBank_[r->offset] = NULL;
		}
		bgmBank_[r->offset] = (unsigned char*)malloc(sz);
		if (bgmBank_[r->offset]) {
			memcpy(bgmBank_[r->offset], data, sz);
			bgmBankSize_[r->offset] = sz;
		}
	}
	DeriveMicrocabinVdata(ge);
}

/* xak_88 は曲ごとに type=voice があるが vdata_addr が無く、FM 音色バンクが RAM に届かずオペレータ残留でキーオン — 「FM が欠け SSG だけ」に聞こえ、兄弟 xak2_88 より約 4 倍小さい。

   Microcabin の FM88/MMD は状態を自イメージ内に持ちシーケンス ptr だけ取るので、音色はドライバがハードコードする番地（mdata_addr 直下）へ植える。
   xak2_88 がその配置（mdata $F800、vdata $F400）。xak_88 も相対は同じ（$F400 / $F000、掃引で確認）。

   一般算術ではなくドライババイナリで鍵を掛ける: カタログで vdata_addr を出すものはドライバ固有の場所へ置き、シーケンス直下に来るのはこの系統だけ。 */
void CHardPc88::DeriveMicrocabinVdata(const CEmuGameEntry* ge)
{
	if (vdataAddr_ >= 0 || mdataAddr_ <= 0)
		return;

	int isMicrocabin = 0;
	for (int i = 0; i < ge->romCount && !isMicrocabin; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0) continue;
		isMicrocabin = (_stricmp(r->name, "FM88.COM") == 0
			|| _stricmp(r->name, "MMD.COM") == 0);
	}
	if (!isMicrocabin)
		return;

	unsigned voiceSize = 0;
	for (int i = 0; i < 256; i++) {
		if (voiceBank_[i] && voiceBankSize_[i] > voiceSize)
			voiceSize = voiceBankSize_[i];
	}
	if (!voiceSize || (int)voiceSize > mdataAddr_)
		return;

	vdataAddr_ = mdataAddr_ - (int)voiceSize;
	if (vfileSize_ <= 0)
		vfileSize_ = (int)voiceSize;
}

/* CHardPc88::ShouldRestageSong の実装 */
int CHardPc88::ShouldRestageSong() const
{
	if (mdataAddr_ < 0)
		return 0;
	/* Scheme OPNA: C000 はライブドライバ（MS0A）+ BGM 窓。Render から曲を載せ直すと MUS2 を壊す。BGM は OUT (0),bank。 */
	if (schemeMode_)
		return 0;
	/* Falcom E000: prog は 0000..5FFF。ApplyFalcomPlay 前の mdata@5C00 再載せは消され、OUT(02) / Apply が BGM をコピー。 */
	if (falcomType_)
		return 0;
	/* PATCH@0 + SP=0x0100: page 0 へ mdata を載せ直すとスタックと poll を壊す（albatrss mdata=0x100）。init_pc が高ければ PATCH が別ページで mdata=0 でも安全。 */
	if (mdataAddr_ < 0x200 && initPc_ < 0x200)
		return 0;
	return 1;
}

/* mdata がソフトオーバーレイ頁（SEDAT / TRPSCR）のとき真。ブート中は無傷のまま。play 時の再載せで曲を戻す。汎用 PROG 重なり（smariosp）は従来どおりプリロード。 */
static int CEmuPc88SongOverlapsCode(CEmuZipFs* fs, const CEmuGameEntry* ge, int mdataAddr, int mdataSize)
{
	if (!fs || !ge || mdataAddr < 0)
		return 0;
	int mend = mdataAddr + (mdataSize > 0 ? mdataSize : 0x1000);
	if (mend > 0x10000) mend = 0x10000;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0) continue;
		const char* nm = r->name;
		if (!nm) continue;
		int soft = 0;
		if (strstr(nm, "SEDAT") || strstr(nm, "sedat")
			|| strstr(nm, "TRPSCR") || strstr(nm, "trpscr")
			|| strstr(nm, "Trpscr"))
			soft = 1;
		const int off = r->offset;
		if (off < 0 || off >= 0x10000) continue;
		/* jikochu*: 同一イメージが code@mdata かつ bgm — ブート中はコードを残す */
		if (off == mdataAddr)
			soft = 1;
		if (!soft) continue;
		unsigned sz = 0;
		if (!CEmuPc88ZipFind(fs, r->name, &sz, -1, 0) || !sz) continue;
		const int end = off + (int)sz;
		if (off < mend && mdataAddr < end)
			return 1;
	}
	return 0;
}

/* can1_88 系: DRIVER.BIN はカタログ 0x0100 だが PATCH は IM2 ワード 0x2008 書込後に 0x20xx（I ページ 0x20）を CALL。4K イメージをミラー。 */
static void CEmuPc88MirrorDriverPage20(uint8_t* mem)
{
	int calls20 = 0;
	for (int i = 0; i < 0x80; i++) {
		if (mem[i] == 0xCD && mem[i + 2] == 0x20) {
			calls20 = 1;
			break;
		}
	}
	if (!calls20)
		return;
	int occupied = 0;
	for (int i = 0; i < 16; i++) {
		if (mem[0x2000 + i]) {
			occupied = 1;
			break;
		}
	}
	if (occupied)
		return;
	if (mem[0x0100] == 0 && mem[0x0101] == 0)
		return;
	memcpy(mem + 0x2000, mem + 0x0100, 0x1000);
}

/* 選んだ BGM／ボイスを mdata へ載せる */
void CHardPc88::LoadSongData(unsigned titleCode)
{
	const unsigned songNum = titleCode & 0xff;
	/* KOEI / パックバンク: ファイルオフセットは bit 8..23 */
	unsigned fileOff = (titleCode >> 8) & 0xffff;
	const int titleMdata = CEmuPc88TitleEncodedMdata(titleCode, mdataAddrDefaulted_, wolfteamMode_);
	if (titleMdata >= 0) {
		mdataAddr_ = titleMdata;
		/* hi16 は絶対ロードページ（ashe/galfstrm/MMLEX）。fileOff に使うとバンク全体を飛ばす（0xC00 ファイル上の MUS09@0x1100）。 */
		fileOff = 0;
	}
	/* ashe MUS00 はカタログ offset 0。スロット空、または A000 リンクのオープニング（ch0 ptr A0A1）でなければシグネチャでバンクを復元。 */
	if (CEmuPc88AshePlayHi(mem_) && songNum == 0
		&& (!bgmBank_[0] || bgmBankSize_[0] < 25
			|| ((unsigned)bgmBank_[0][1] | ((unsigned)bgmBank_[0][2] << 8)) != 0xA0A1u)) {
		for (int i = 1; i < 256; i++) {
			if (!bgmBank_[i] || bgmBankSize_[i] < 25 || bgmBank_[i][0] != 6)
				continue;
			const unsigned p = (unsigned)bgmBank_[i][1]
				| ((unsigned)bgmBank_[i][2] << 8);
			if (p != 0xA0A1u)
				continue;
			unsigned char* copy = (unsigned char*)malloc(bgmBankSize_[i]);
			if (!copy)
				break;
			memcpy(copy, bgmBank_[i], bgmBankSize_[i]);
			if (bgmBank_[0])
				free(bgmBank_[0]);
			bgmBank_[0] = copy;
			bgmBankSize_[0] = bgmBankSize_[i];
			break;
		}
	}
	/* Voice を BGM より先: ys2 END/TTL は音楽ステージ頁で終わる（ENDPRG@0100..20FF が mus@2000 と重なり、TTLPRG は @3000）。音色を後に植えるとミラーした *MUS を壊していた。 */
	/* ginei2: OPENING0 を vdata 0400 に置くと ITEST（CALL 52FA）を消す。GEDS は BGM_* と同じ 06ch プレーヤ — ITEST を残しオーバーレイを飛ばす。 */
	if (CEmuPc88PatchGinei2(mem_) && vdataAddr_ == 0x400)
		;
	else if (vdataAddr_ >= 0) {
		unsigned vnum = songNum;
		if (voiceBank_[vnum]) {
			unsigned n = voiceBankSize_[vnum];
			if (vfileSize_ > 0 && (unsigned)vfileSize_ < n)
				n = (unsigned)vfileSize_;
			if (vdataAddr_ + (int)n > 0x10000)
				n = (unsigned)(0x10000 - vdataAddr_);
			if (n > 0) {
				memcpy(mem_ + vdataAddr_, voiceBank_[vnum], n);
				if (CEmuPc88PatchXzr2VoiceF000(mem_)) {
					const unsigned dest = (unsigned)mem_[0xA449]
						| ((unsigned)mem_[0xA44A] << 8);
					if (dest >= 0x100u && dest + n <= 0x10000u
						&& dest != (unsigned)vdataAddr_)
						memcpy(mem_ + dest, voiceBank_[vnum], n);
					mem_[0xA445] |= 1;
					/* dest+0x200 は書かない: DRIVER は既に音色 16 を BA8E+0x200 に持つ。上書きすると MA001 が無音。 */
				}
			}
			/* manreq88 A9C: LD A,$D0 / LD (A2C0),A。D0=RET NC はキャリークリア時にライブスランプを戻す（M's BOOGIE 無音）。音色バンク 3–6 は PATCH が D1 を植える。イメージでも同じ。 */
			if (CEmuPc88PatchManreq(mem_) && vdataAddr_ >= 0
				&& vdataAddr_ + 7 < 0x10000
				&& mem_[vdataAddr_ + 6] == 0x3E && mem_[vdataAddr_ + 7] == 0xD0)
				mem_[vdataAddr_ + 7] = 0xD1;
		}
	}
	if (bgmBank_[songNum] && mdataAddr_ >= 0) {
		unsigned avail = bgmBankSize_[songNum];
		/* DUMMY/極小 bgm（gra88 MDAT=1、castle DUMMY=0）: 音楽は code ROM。stub を mdata に書くと壊すだけ。 */
		if (avail < 16)
			avail = 0;
		/* 同一イメージが既に code@mdata（navitune navimus@7700）ならフルファイルを残す — タイトル bit 8..23 は PATCH LD BC（ApplyNavituneTitleSong）で曲ヘッダを選ぶ。スライスしない。 */
		unsigned stageOff = fileOff;
		if (armNavituneTimer_ && mdataAddr_ >= 0 && fileOff < avail) {
			naviSongAddr_ = (uint16_t)((unsigned)mdataAddr_ + fileOff);
			stageOff = 0;
		}
		if (stageOff < avail) {
			unsigned n = avail - stageOff;
			/* mdata_size = mdata_addr に置いた窓。mfile_size はオンデマンド BankCopy 長で、このコピーを膨らませない。 */
			if (mdataSize_ > 0 && (unsigned)mdataSize_ < n)
				n = (unsigned)mdataSize_;
			else if (mdataSize_ <= 0 && mfileSize_ > 0 && (unsigned)mfileSize_ < n)
				n = (unsigned)mfileSize_;
			/* 表インデックス PATCH（ashe のみ）: MUS は DRIVER（0x7800）と重なりがちな高ページへリンク。title mdata にステージし絶対 ch ptr をその基点へリロケ。
			   他の titleMdata 仲間（MMLEX / song<<8）では走らせない — InferMusLinkAddr + 偶数ワードリロケで約 30 の OK タイトルが無音になった。 */
			int loadAddr = mdataAddr_;
			int linkBase = -1;
			const int asheHi = CEmuPc88AshePlayHi(mem_);
			if (asheHi && titleMdata >= 0) {
				linkBase = CEmuPc88InferMusLinkAddr(
					bgmBank_[songNum] + stageOff, avail - stageOff);
				/* MUS00/01 は A000 リンクで DRIVER@7800 に張り付く。titleMdata 1000 にステージすると 1xxx ptr（bit7 クリア）が PATCH スタック/IM2 に壊される。93xx バンクも DRIVER と重なる — 同じ A000 窓へ。 */
				if (linkBase >= 0xA000) {
					unsigned native = (unsigned)linkBase;
					if (native + n > 0x10000u)
						n = 0x10000u - native;
					if (n > 0)
						loadAddr = (int)native;
				} else if (linkBase >= 0x7800 && linkBase < 0xA000) {
					loadAddr = 0xA000;
					if ((unsigned)loadAddr + n > 0x10000u)
						n = 0x10000u - (unsigned)loadAddr;
				}
			}
			if (loadAddr + (int)n > 0x10000)
				n = (unsigned)(0x10000 - loadAddr);
			if (n > 0 && loadAddr >= 0)
				memcpy(mem_ + loadAddr, bgmBank_[songNum] + stageOff, n);
			/* xanadu_80sr: PR.NO3/4/5 は 0x1400..0x3000。プレーヤは 0x6000 Main イメージ内の 5Cxx ワークを読む（PR.NO2）。ゼロ埋めは Boss を mute。Main の末尾をコピー。 */
			if (n > 0 && loadAddr == 0 && n < 0x6000u
				&& CEmuPc88PatchXanadu80sr(mem_, initPc_)
				&& bgmBank_[1] && bgmBankSize_[1] >= 0x6000u)
				memcpy(mem_ + n, bgmBank_[1] + n, 0x6000u - n);
			/* ys2_88: PATCH LDIR 先（4D00/3000/2000）にも植える。ゲストの C000→dest コピーだけに頼らず MANPR/TTL の絶対フレーズ ptr を解決。 */
			if (n > 0 && loadAddr == 0xc000
				&& CEmuPc88PatchFalcomLdirFromC000(mem_)) {
				const unsigned dest = CEmuPc88FalcomYs2MusDest(mem_, songNum);
				if (dest != (unsigned)loadAddr && dest < 0x10000u) {
					unsigned n2 = n;
					if (dest + n2 > 0x10000u)
						n2 = 0x10000u - dest;
					if (n2 > 0)
						memcpy(mem_ + dest, bgmBank_[songNum] + stageOff, n2);
				}
				CEmuPc88FalcomYs2PlantTable(mem_, songNum);
			}
			/* ApplyNavituneTitleSong は最初のポート再生のあと（ドライバ） */
			if (asheHi && n > 0 && loadAddr >= 0 && linkBase >= 0x2000
				&& linkBase != loadAddr) {
				/* ステージ基点の ashe ヘッダだけリロケ。全オフセット＋偶数ワード走査は attr_hi||next_ptr_lo（偽 0x9A01）と A0xx ノート（MUS00 6.5K 無音）に当たる。MUS09 は運で生き残った。 */
				const unsigned lb = (unsigned)linkBase;
				unsigned heads[2] = { 0, 0 };
				unsigned nHeads = 1;
				if (n >= 0x19 + 5
					&& mem_[loadAddr + 0x19] >= 3
					&& mem_[loadAddr + 0x19] <= 16)
					heads[nHeads++] = 0x19;
				for (unsigned h = 0; h < nHeads; h++) {
					const unsigned off = heads[h];
					const unsigned nch = mem_[loadAddr + (int)off];
					if (nch < 1 || nch > 16 || off + 1 + nch * 4 > n)
						continue;
					int ok = 1;
					for (unsigned c = 0; c < nch; c++) {
						const unsigned po = off + 1 + c * 4;
						const unsigned w = (unsigned)mem_[loadAddr + (int)po]
							| ((unsigned)mem_[loadAddr + (int)po + 1] << 8);
						if (w < lb || w >= lb + n) { ok = 0; break; }
					}
					if (!ok) continue;
					for (unsigned c = 0; c < nch; c++) {
						const unsigned po = off + 1 + c * 4;
						unsigned w = (unsigned)mem_[loadAddr + (int)po]
							| ((unsigned)mem_[loadAddr + (int)po + 1] << 8);
						if (w < lb || w >= lb + n) continue;
						w = w - lb + (unsigned)loadAddr;
						mem_[loadAddr + (int)po] = (uint8_t)(w & 0xff);
						mem_[loadAddr + (int)po + 1] = (uint8_t)((w >> 8) & 0xff);
					}
				}
				CEmuPc88AsheRelocPhrases(mem_, loadAddr, n, lb);
			}
			/* song<<8 + HL=4000: LDIR が 4000 → titlepage。バンクをミラー。 */
			if (n > 0 && titleMdata >= 0 && CEmuPc88PatchLdirFrom4000(mem_)) {
				unsigned n4 = n;
				if (0x4000 + n4 > 0x10000)
					n4 = 0x10000 - 0x4000;
				if (n4 > 0)
					memcpy(mem_ + 0x4000, bgmBank_[songNum] + fileOff, n4);
			}
			/* ashe 表インデックス: ptr を植え、リロケ済みイメージを 0x4000 へミラー。DRIVER@80EE は song < 0x10 を拒否。タイトル上位バイトが添字。 */
			if (asheHi && titleMdata >= 0) {
				unsigned plantIdx = (titleCode >> 24) & 0xffu;
				int plantAddr = loadAddr;
				/* タイトル上位 0x11 はバンク内 2 番目の ashe 風ヘッダ（MUS09@+0x19）。0x10 は先頭。 */
				if (plantIdx == 0x11 && n >= 0x19 + 5
					&& mem_[loadAddr + 0x19] >= 3
					&& mem_[loadAddr + 0x19] <= 16)
					plantAddr = loadAddr + 0x19;
				CEmuPc88PlantAsheSongTable(mem_, plantIdx, plantAddr);
				unsigned n4 = n;
				if (n4 > 0x2000) n4 = 0x2000;
				if (0x4000 + n4 > 0x10000) n4 = 0x10000 - 0x4000;
				if (n4 > 0)
					memcpy(mem_ + 0x4000, mem_ + loadAddr, n4);
			}
		}
	}
	/* manreq88/kissof88: vdata は mdata 窓の中（A9C @AD4E は A9A の 8D77..CA77 コピー内）。BGM memcpy が M's BOOGIE / SILVER KNIFE を選ぶオーバーレイを消す。ys2 は音色が mdata より下なので Voice 先行のまま（音楽側が重なりに勝つ）。 */
	if (voiceBank_[songNum] && vdataAddr_ >= 0 && mdataAddr_ >= 0
		&& vdataAddr_ >= mdataAddr_
		&& !CEmuPc88PatchXzr2VoiceF000(mem_)) {
		const int mend = mdataAddr_
			+ (mdataSize_ > 0 ? mdataSize_
				: (mfileSize_ > 0 ? mfileSize_ : 0x1000));
		if (vdataAddr_ < mend) {
			unsigned n = voiceBankSize_[songNum];
			if (vfileSize_ > 0 && (unsigned)vfileSize_ < n)
				n = (unsigned)vfileSize_;
			if (vdataAddr_ + (int)n > 0x10000)
				n = (unsigned)(0x10000 - vdataAddr_);
			if (n > 0)
				memcpy(mem_ + vdataAddr_, voiceBank_[songNum], n);
			if (CEmuPc88PatchManreq(mem_) && vdataAddr_ + 7 < 0x10000
				&& mem_[vdataAddr_ + 6] == 0x3E && mem_[vdataAddr_ + 7] == 0xD0)
				mem_[vdataAddr_ + 7] = 0xD1;
		}
	}
	if (CEmuPc88PatchHarakiriMplay(mem_)) {
		const int md = (mdataAddr_ >= 0) ? mdataAddr_ : 0x4000;
		mem_[0xC003] = 0x21;
		mem_[0xC004] = (uint8_t)(md & 0xff);
		mem_[0xC005] = (uint8_t)((md >> 8) & 0xff);
		mem_[0xC006] = 0xC3;
		mem_[0xC007] = 0xD7;
		mem_[0xC008] = 0xC0;
		if (mem_[0x11] == 0xCD && mem_[0x12] == 0x09 && mem_[0x13] == 0xC0)
			mem_[0x12] = 0x03;
		if (mem_[0x25] == 0xCD && mem_[0x26] == 0x06 && mem_[0x27] == 0xC0)
			mem_[0x26] = 0x03;
		/* C0D7 は PATCH CALL 09F5 が I=7 を立てる前に EI。I=0 IM2 は 0004 のゴミへ。ホスト NeedsPlayEi も C003 が ld hl になると一度外す。 */
		if (mem_[0xC12D] == 0xFB)
			mem_[0xC12D] = 0x00;
	}
	/* robowr88 曲 1 は PROG2。重なり延期だと PROG1（CB5A 無し）。ここでバンクをコピー（offset 1、または CB5A=IM2 のイメージ）してから植える。
	   $C0 をキックし cmd=1 が BA41 のゼロに当たらないようにする。 */
	if (CEmuPc88PatchRobowr(mem_) && (titleCode & 0xffu) == 1) {
		const int dst = (mdataAddr_ >= 0) ? mdataAddr_ : 0x818B;
		const unsigned char* src = NULL;
		unsigned n = 0;
		if (bgmBank_[1] && bgmBankSize_[1] >= 16) {
			src = bgmBank_[1];
			n = bgmBankSize_[1];
		} else {
			const unsigned mark = 0xCB5A - 0x818B;
			for (int i = 0; i < 256; i++) {
				if (!bgmBank_[i] || bgmBankSize_[i] <= mark + 1)
					continue;
				if (bgmBank_[i][mark] == 0xF3 && bgmBank_[i][mark + 1] == 0xED) {
					src = bgmBank_[i];
					n = bgmBankSize_[i];
					break;
				}
			}
		}
		if (src && n && dst >= 0) {
			if (dst + (int)n > 0x10000)
				n = (unsigned)(0x10000 - dst);
			if (n > 0)
				memcpy(mem_ + dst, src, n);
		}
		CEmuPc88PlantRobowrSong1(mem_);
		if (mem_[0xCB5A] == 0xF3 && mem_[0xCB5B] == 0xED) {
			playKickBase_ = 0xC0;
			playKickInitOff_ = 0;
			playKickEi_ = 0;
			if (cpu_)
				cpu_->r.sp = 0xFF00;
		}
	}
	if (CEmuPc88PatchHardrankSb2(mem_)) {
		hardrankSb2_ = 1;
		if (mem_[0x79D7] < 0x38)
			mem_[0x79D7] = 0x40;
		CEmuPc88SkipHardrankLeadRest(mem_);
		/* PATCH cmd=1 はポート 0 をクリアしないのでドレインが 8A00 を再 CALL し、9130 をヘッダ ptr に戻す（Stage 1 が和音で固まる）。 */
		if (mem_[0x18] == 0xCD && mem_[0x19] == 0x00 && mem_[0x1A] == 0x8A
			&& mem_[0x1B] == 0x18 && mem_[0x1C] == 0xE9) {
			mem_[0x1B] = 0xAF;
			mem_[0x1C] = 0xD3;
			mem_[0x1D] = 0x00;
			mem_[0x1E] = 0x18;
			mem_[0x1F] = 0xE6;
		}
	}
	if (CEmuPc88PatchXanadu80sr(mem_, initPc_)) {
		mem_[0x606A] = 0;
		/* Boss play@174D の OUT E6,0 は IM2 をマスク。Main も同じ OUT だが 0x6000 イメージ。マスク解除し、(617A) の 1675 tick を飛ばさない。 */
		if ((titleCode & 0xffu) == 2 && mem_[0x174D] == 0xF3) {
			if (mem_[0x175C] == 0x3E && mem_[0x175D] == 0x00
				&& mem_[0x175E] == 0xD3 && mem_[0x175F] == 0xE6)
				mem_[0x175D] = 0x03;
			if (mem_[0x167F] == 0xA7 && mem_[0x1680] == 0x20 && mem_[0x1681] == 0x1B)
				mem_[0x167F] = mem_[0x1680] = mem_[0x1681] = 0x00;
			if (mem_[0x1685] == 0xA7 && mem_[0x1686] == 0x28 && mem_[0x1687] == 0x15)
				mem_[0x1685] = mem_[0x1686] = mem_[0x1687] = 0x00;
		}
	}
}

/* FE19 DRIVER @ E000（arugies/schwarz）: arugies PATCH は IM2 ワード 0004/0008 → E265/E2A6。schwarz/schwarz2 は I=01 だが I ページベクタを書かず、RTC/音源 IRQ が 0000 に着地して無音。 */
static void CEmuPc88InstallFe19Im2(uint8_t* mem, Ay_Cpu* cpu)
{
	if (!mem || !cpu)
		return;
	if (mem[0xE000] != 0xFE || mem[0xE001] != 0x19)
		return;
	const unsigned iPage = ((unsigned)cpu->r.i) << 8;
	if (iPage + 0x0A > 0x10000)
		return;
	static const uint8_t kVecs[] = { 0x02, 0x04, 0x08 };
	static const uint16_t kHandlers[] = { 0xE265, 0xE265, 0xE2A6 };
	for (int n = 0; n < 3; n++) {
		const unsigned p = iPage + kVecs[n];
		if (mem[p] == 0 && mem[p + 1] == 0) {
			mem[p] = (uint8_t)(kHandlers[n] & 0xff);
			mem[p + 1] = (uint8_t)(kHandlers[n] >> 8);
		}
	}
}

/* yokosuka: cmd=1 + param==FF → IN (80); LD (E23C),A（効果要求）。param!=FF → DI; CALL SOUND+0x1BD（BGM）。効果タイトルは low=FF、hi24 をポート 80 の効果 id。 */
static int CEmuPc88PatchYokosukaEff(const uint8_t* mem)
{
	if (!mem) return 0;
	for (int i = 0; i + 4 < 0x80; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0x32 && mem[i + 3] == 0x3C && mem[i + 4] == 0xE2)
			return 1;
	}
	return 0;
}

/* smariosp: IN (80); OR A; JR NZ → CALL play。さもなくば IN (01); LD (mailbox),A。BGM は port80=0 にして param を曲メールボックスへ（makai と同様）。 */
static int CEmuPc88PatchPort80GateParam(const uint8_t* mem)
{
	if (!mem) return 0;
	for (int i = 0; i + 12 < 0x80; i++) {
		if (mem[i] != 0xDB || mem[i + 1] != 0x80 || mem[i + 2] != 0xB7)
			continue;
		if (mem[i + 3] != 0x20)
			continue;
		/* XOR A; LD (nn),A; IN A,(01); LD (nn),A（曲メールボックス植込） */
		int k = i + 5;
		if (k + 7 >= 0x80) continue;
		if (mem[k] == 0xAF && mem[k + 1] == 0x32
			&& mem[k + 4] == 0xDB && mem[k + 5] == 0x01
			&& mem[k + 6] == 0x32
			&& mem[k + 2] == mem[k + 7] && mem[k + 3] == mem[k + 8])
			return 1;
	}
	return 0;
}

/* 100yen4（onion split@9000）: IN A,(80); LD (9006),A; CALL 9000。タイトル hi24 がバンク内曲（'1'..）、下位バイトが MUS バンク。
   HootCmd01 は不一致（CALL 9007 が JR NZ と IN (01) の間）なので PlaySongIndex がバンク id を返し、god2 全タイトルが曲 3 になった。IN (80) は $25 であり $26 ではない。 */
static int CEmuPc88PatchOnion9006(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3)
		return 0;
	for (int i = 0; i + 7 < 0x40; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0x32 && mem[i + 3] == 0x06 && mem[i + 4] == 0x90
			&& mem[i + 5] == 0xCD && mem[i + 6] == 0x00 && mem[i + 7] == 0x90)
			return 1;
	}
	return 0;
}

/* pwmajan2: PATCH@F000 が PROG4 の EI@9038 に C9 を植え、CALL 9019 がディスクローダ前に戻る。ポート 01 が MUS バンク、ポート 80 がバンク内曲（0 は本物オープニング）。
   poll は DI のまま — ホストが EI。LoadRoms は CALL 9019 を NOP。その形を残し PlaySongIndex / NeedsPlayEi / ArmPwmajan2 が植込後もタイトルを見る。 */
static int CEmuPc88PatchPwmajan2(const uint8_t* mem)
{
	if (!mem || mem[0xF000] != 0xF3)
		return 0;
	if (!(mem[0xF00D] == 0x3E && mem[0xF00E] == 0xC9
		&& mem[0xF00F] == 0x32 && mem[0xF010] == 0x38 && mem[0xF011] == 0x90))
		return 0;
	if (mem[0xF012] == 0xCD && mem[0xF013] == 0x19 && mem[0xF014] == 0x90)
		return 1;
	if (mem[0xF012] == 0x00 && mem[0xF013] == 0x00 && mem[0xF014] == 0x00)
		return 1;
	return 0;
}

/* gandhara: IN (01) は 00BD の 8 バイト行。IN (80)==1 は特別 LDIR、==2 は CALL 8C3D を飛ばす（mute）。ポート 80 はタイトル上位（01/02/03 BGM は 0）のまま、全行が LDDR + CALL 8C3D。 */
static int CEmuPc88PatchGandhara(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3)
		return 0;
	return (mem[6] == 0x3E && mem[7] == 0x86
		&& mem[0x21] == 0xCD && mem[0x22] == 0x1E && mem[0x23] == 0x8C
		&& mem[0x2F] == 0x11 && mem[0x30] == 0xBD && mem[0x31] == 0x00) ? 1 : 0;
}

/* CEmuPc88PatchHootCmd01At の実装 */
static int CEmuPc88PatchHootCmd01At(const uint8_t* mem, unsigned base)
{
	/* IN A,(0); OR A; JR Z,poll; CP 1; JR NZ,stop; IN A,(1)
	   rouge88 / hchaser / blmnstry / pias88 はポート 01 読の前に LD C,0 を上げ、OUT (C),C でコマンドラッチをクリア。この 1 挿入だけ許し、緩くしない:
	   前置は PC-88 リップでほぼ共通（220）で、緩い一致だと下のゲーム固有規則を全部隠す。 */
	for (unsigned i = base; i < base + 0x60 && i + 12 < 0x10000; i++) {
		if (mem[i] != 0xDB || mem[i+1] != 0x00 || mem[i+2] != 0xB7
			|| mem[i+3] != 0x28 || mem[i+5] != 0xFE || mem[i+6] != 0x01
			|| mem[i+7] != 0x20)
			continue;
		if (mem[i+9] == 0xDB && mem[i+10] == 0x01)
			return 1;
		if (mem[i+9] == 0x0E && mem[i+10] == 0x00
			&& mem[i+11] == 0xDB && mem[i+12] == 0x01)
			return 1;
	}
	return 0;
}

/* iceclimb88: JR $10 PATCH、CALL B323、メールボックス B816 */
static int CEmuPc88PatchIceclimb(const uint8_t* mem)
{
	if (!mem) return 0;
	return (mem[0] == 0x18 && mem[0x10] == 0xF3 && mem[0x11] == 0xED
		&& mem[0x16] == 0xCD && mem[0x17] == 0x23 && mem[0x18] == 0xB3
		&& mem[0x35] == 0x32 && mem[0x36] == 0x16 && mem[0x37] == 0xB8) ? 1 : 0;
}

/* Title (song 5) shares phrase bytes with looping boss (song 4) but the
   5-byte table flag is 00 so it plays once. NES title loops — same flag as
   通常BGM/boss. Round intro (song 1) stays finite. */
static void CEmuPc88PlantIceclimbTitleLoop(uint8_t* mem)
{
	if (!mem || !CEmuPc88PatchIceclimb(mem))
		return;
	if (mem[0xBB41] == 0x5F && mem[0xBB42] == 0xBB
		&& mem[0xBB55] == 0xE7 && mem[0xBB56] == 0xBD
		&& mem[0xBB59] == 0x00)
		mem[0xBB59] = 0x01;
}

/* manreq88: ディスクフック 8892/92EA に RET、I=BF */
static int CEmuPc88PatchManreq(const uint8_t* mem)
{
	if (!mem) return 0;
	return (mem[0] == 0xF3 && mem[0x0B] == 0x3E && mem[0x0C] == 0xBF
		&& mem[0x0F] == 0x3E && mem[0x10] == 0xC9
		&& mem[0x11] == 0x32 && mem[0x12] == 0x92 && mem[0x13] == 0x88
		&& mem[0x14] == 0x32 && mem[0x15] == 0xEA && mem[0x16] == 0x92) ? 1 : 0;
}

/* d': IN (80) は 0 始まりフレーズ添字。CALL 17B3。PATCH は mdata を (808e) に植える。 */
static int CEmuPc88PatchDprime(const uint8_t* mem)
{
	if (!mem) return 0;
	return (mem[0] == 0xF3 && mem[0x11] == 0x22
		&& mem[0x12] == 0x8E && mem[0x13] == 0x80
		&& mem[0x3F] == 0xCD && mem[0x40] == 0xB3 && mem[0x41] == 0x17) ? 1 : 0;
}

/* adrnalin: IN (80); CALL 9106。ドライバは DEC A して word[9e00] を引く。タイトル hi24 が 1 始まり再生 id。下位バイトは MUS0n 選択のみ。 */
static int CEmuPc88PatchAdrnalin(const uint8_t* mem)
{
	if (!mem) return 0;
	return (mem[0] == 0x18 && mem[0x10] == 0xF3
		&& mem[0x2C] == 0xDB && mem[0x2D] == 0x80
		&& mem[0x2E] == 0xCD && mem[0x2F] == 0x06 && mem[0x30] == 0x91) ? 1 : 0;
}

/* CEmuPc88PatchHootCmd01 の実装 */
static int CEmuPc88PatchHootCmd01(const uint8_t* mem, int initPc)
{
	/* 標準 hoot PC-88 PATCH プロローグ。約 134 リップが共有（triton2、goonies88、valis、sorc88…）。ポートは固定:
	     IN A,(00)  コマンド。1=再生（他は停止）
	     IN A,(01)  曲／バンク番号。OUT (01),A でエコー
	     IN A,(80)  ドライバ再生入口へ渡す A
	   ポート 01 はタイトル下位、ポート 80 は上位。シグネチャは要求曲を語らない。
	   半数は PATCH を page 0 ではなく init_pc（sorc88 は C000、F000 族、rouge88 は 1000）。そこも探さないと汎用規則へ落ち、全曲が同じバイトを要求する。 */
	if (!mem) return 0;
	if (CEmuPc88PatchHootCmd01At(mem, 0))
		return 1;
	if (initPc > 0)
		return CEmuPc88PatchHootCmd01At(mem, (unsigned)initPc);
	return 0;
}

/* HootCmd01 は JR NZ 直後の IN (01) が必須。多くは先に CALL stop（xzr/xzr2/jikochu2/vaxol/wibarm/romanciasr）。ポート 01 はバンク、ポート 80 はファイル内添字（タイトル上位）。無いと両ポートが下位バイトになり、ディスク双子（00000001 vs 01000001）が SAMESONG／範囲外シークで無音。 */
static int CEmuPc88PatchPort01Then80(const uint8_t* mem, int initPc)
{
	if (!mem) return 0;
	auto scan = [](const uint8_t* mem, unsigned base) -> int {
		for (unsigned i = base; i + 12 < base + 0x70 && i + 12 < 0x10000u; i++) {
			if (mem[i] != 0xDB || mem[i + 1] != 0x00 || mem[i + 2] != 0xB7
				|| mem[i + 3] != 0x28 || mem[i + 5] != 0xFE || mem[i + 6] != 0x01
				|| mem[i + 7] != 0x20)
				continue;
			int saw01 = 0;
			const unsigned end = i + 8 + 56;
			for (unsigned k = i + 8; k + 1 < end && k + 1 < 0x10000u; k++) {
				if (mem[k] == 0xDB && mem[k + 1] == 0x01)
					saw01 = 1;
				if (saw01 && mem[k] == 0xDB && mem[k + 1] == 0x80)
					return 1;
			}
		}
		return 0;
	};
	if (scan(mem, 0))
		return 1;
	if (initPc > 0)
		return scan(mem, (unsigned)initPc);
	return 0;
}

/* mule: IN (01) はページ表 004E（B0/B9/BB/BC）。IN (80) は RRA で ISR 8FE5 vs 8B51。上位のみタイトルは port80=1（MAIN 01000000）になり、全曲が 8B51／同一ページ。 */
static int CEmuPc88PatchMulePages(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3 || mem[1] != 0xED || mem[2] != 0x5E)
		return 0;
	if (!(mem[0x1C] == 0xDB && mem[0x1D] == 0x01
		&& mem[0x1E] == 0xFE && mem[0x1F] == 0x05))
		return 0;
	return (mem[0x25] == 0xDB && mem[0x26] == 0x80 && mem[0x27] == 0x1F) ? 1 : 0;
}

/* blmnstry/hchaser/rouge88/pias88: PATCH は PMD メールボックス（xx0F）に HL=4000。各 bgm は 4000 の 1 MML。ポート 01 は 0 のまま、PMD がそのファイルを再生（ファイル内添字をシークしない）。 */
static int CEmuPc88PatchPmdHl4000(const uint8_t* mem, int initPc)
{
	if (!mem) return 0;
	int pc = initPc;
	if (pc < 0) pc = 0;
	if (pc + 1 < 0x10000 && mem[pc] == 0x18)
		pc = pc + 2 + (int)(int8_t)mem[pc + 1];
	for (int i = pc; i + 5 < pc + 0x40 && i + 5 < 0x10000; i++) {
		if (mem[i] == 0x21 && mem[i + 1] == 0x00 && mem[i + 2] == 0x40
			&& mem[i + 3] == 0x22 && mem[i + 4] == 0x0F)
			return 1;
	}
	return 0;
}

/* lvaccus: XOR A; CALL 9800（停止）; IN (01)=バンク; IN (80)=バンク内 id; CALL 9800 再生。PATCH は EI しない — VRTC はホストから。 */
static int CEmuPc88PatchLvaccus9800(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3 || mem[1] != 0xED || mem[2] != 0x5E)
		return 0;
	return (mem[0x18] == 0xCD && mem[0x19] == 0x00 && mem[0x1A] == 0x98
		&& mem[0x1B] == 0xDB && mem[0x1C] == 0x01
		&& mem[0x1F] == 0xDB && mem[0x20] == 0x80
		&& mem[0x21] == 0xCD && mem[0x22] == 0x00 && mem[0x23] == 0x98) ? 1 : 0;
}

/* xzr/xzr2: I=A4、IN (80); CALL A410。各 MA/MB は 4000 の 1 曲。Port01Then80 はタイトル上位をファイル内添字に渡し（MA000 01000015 → A=1 範囲外。MA* ヘッダは 01 42 = 1 曲）。 */
static int CEmuPc88PatchXzrA4(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3 || mem[1] != 0xED || mem[2] != 0x5E)
		return 0;
	if (!(mem[6] == 0x3E && mem[7] == 0xA4 && mem[8] == 0xED && mem[9] == 0x47))
		return 0;
	for (int i = 0x20; i + 4 < 0x80; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0xCD && mem[i + 3] == 0x10 && mem[i + 4] == 0xA4)
			return 1;
	}
	return 0;
}

/* xzr2: LDIR F000 → (A449) が CALL A410 前に 0x200 音色バンクをコピー。カタログに vdata_addr が無く VD* が F000 に届かず、e0 がゼロパッチ（AR=0 → キーオン、peak 0）。PATCH は
   `ld hl,F000 / ld de,(A449) / ld bc,0200 / ldir` — ld bc が ld de と ldir の間にあり、11 バイト「+9 の ED B0」一致は外す。 */
static int CEmuPc88PatchXzr2VoiceF000(const uint8_t* mem)
{
	if (!mem || !CEmuPc88PatchXzrA4(mem))
		return 0;
	for (int i = 0x20; i + 12 < 0x80; i++) {
		if (!(mem[i] == 0x21 && mem[i + 1] == 0x00 && mem[i + 2] == 0xF0
			&& mem[i + 3] == 0xED && mem[i + 4] == 0x5B))
			continue;
		/* ld de,(nn) は 4 バイト。任意で ld bc,nn のあと ldir */
		if (mem[i + 7] == 0x01 && mem[i + 10] == 0xED && mem[i + 11] == 0xB0)
			return 1;
		if (mem[i + 9] == 0xED && mem[i + 10] == 0xB0)
			return 1;
	}
	return 0;
}

/* ginei2: CP 40 / CP 20 で ITEST オープニング vs BGM。各 GEDS/BGM は 6390 の 1 曲で同じ 06ch ヘッダ。オープニングは OPENING0 が ITEST@0400 を消したあと 0x21 を CALL 159F へ。param 0 なら CALL 52FA。 */
static int CEmuPc88PatchGinei2(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18)
		return 0;
	return (mem[0x3D] == 0xFE && mem[0x3E] == 0x40
		&& mem[0x41] == 0xFE && mem[0x42] == 0x20
		&& mem[0xB5] == 0x21 && mem[0xB6] == 0x90 && mem[0xB7] == 0x63) ? 1 : 0;
}

/* af ENDING/OPENING: param>=0x20 は opdrv をコピーして IN (80); OR A; LD HL,4400 / LD H,A。タイトル 00000021 は両ポートに 0x21 → 再生が 2100。 */
static int CEmuPc88PatchAfHl4400(const uint8_t* mem)
{
	if (!mem || mem[0] != 0xF3 || mem[1] != 0xED || mem[2] != 0x5E)
		return 0;
	if (!(mem[0x5D] == 0xFE && mem[0x5E] == 0x20))
		return 0;
	return (mem[0xB1] == 0xDB && mem[0xB2] == 0x80
		&& mem[0xB3] == 0xB7
		&& mem[0xB4] == 0x21 && mem[0xB5] == 0x00 && mem[0xB6] == 0x44) ? 1 : 0;
}

/* romanciasr: JP NZ（JR ではない）なので Port01Then80 が外す。IN (80); LD (HL),A がバンク内曲 id（00000001 vs 01000001）。 */
static int CEmuPc88PatchRomanciaSr(const uint8_t* mem, int initPc)
{
	if (!mem || initPc != 0xf000)
		return 0;
	if (!(mem[0xF024] == 0xDB && mem[0xF025] == 0x01))
		return 0;
	return (mem[0xF04D] == 0xDB && mem[0xF04E] == 0x80
		&& mem[0xF04F] == 0x77) ? 1 : 0;
}

/* robowr88: JR $10、IN (C=01)、005F の 6 バイト行。PROG2 の play（CB5A）は (000A)==1 がゲート。残留 000A は 02 で skip。PATCH CALL BA4A は PROG1。曲 1 は CALL CB5A（PROG2 では BA4A がゼロ）。 */
static int CEmuPc88PatchRobowr(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18)
		return 0;
	if (!(mem[0x29] == 0x0E && mem[0x2A] == 0x01
		&& mem[0x2B] == 0xED && mem[0x2C] == 0x78
		&& mem[0x51] == 0xCD))
		return 0;
	return ((mem[0x52] == 0x4A && mem[0x53] == 0xBA)
		|| (mem[0x52] == 0x5A && mem[0x53] == 0xCB)) ? 1 : 0;
}

/* PROG2 ライブ: skip は vec8=8DAB を植え 8FD4 を歩く（CC09 が ptr を埋める）。9013 へ付け替えない — そのエンジンの IX+1 ptr は F000 のまま。
   cmd=1 CALL $005B はまだ JP BA41（PROG2 ではゼロ）。NOP し、$C0 トランポリンで CALL CB5A のあと JP CB48。(000A)==02 のまま skip が OPN 44/45 を保つ。 */
static void CEmuPc88PlantRobowrSong1(uint8_t* mem)
{
	if (!mem || mem[0xCB5A] != 0xF3 || mem[0xCB5B] != 0xED)
		return;
	if (mem[0x51] == 0xCD && mem[0x52] == 0x4A && mem[0x53] == 0xBA
		&& mem[0x54] == 0xC3 && mem[0x55] == 0x38 && mem[0x56] == 0xBA) {
		mem[0x52] = 0x5A;
		mem[0x53] = 0xCB;
		mem[0x55] = 0x48;
		mem[0x56] = 0xCB;
	}
	if (mem[0x25] == 0xCD && mem[0x26] == 0x5B && mem[0x27] == 0x00)
		mem[0x25] = mem[0x26] = mem[0x27] = 0x00;
	if (mem[0x5B] == 0xC3)
		mem[0x5B] = 0xC9;
	if (mem[0x11] == 0x31 && mem[0x12] == 0x00 && mem[0x13] == 0x01)
		mem[0x13] = 0xFF;
	if (mem[0xCC86] == 0xF5 && mem[0xCC87] == 0xDB && mem[0xCC8B] == 0x20)
		mem[0xCC8B] = mem[0xCC8C] = 0x00;
	if (mem[0x8F73] == 0xF5 && mem[0x8F74] == 0xDB && mem[0x8F78] == 0x20)
		mem[0x8F78] = mem[0x8F79] = 0x00;
	mem[0xC0] = 0xCD;
	mem[0xC1] = 0x5A;
	mem[0xC2] = 0xCB;
	mem[0xC3] = 0xC3;
	mem[0xC4] = 0x48;
	mem[0xC5] = 0xCB;
}

/* harakiri MPLAY オーバーレイ: C000=tick、C003=load HL、C006=stop。PATCH はまだ CALL C009（FMDRV init）/ C006（play）。C0D7 は HL=mdata が要り (C00A) をクリア。C006 は 3F に戻す。トランポリン後 C003 は ld hl（JP C0D7 ではない）— まだこのオーバーレイ。 */
static int CEmuPc88PatchHarakiriMplay(const uint8_t* mem)
{
	if (!mem)
		return 0;
	if (!(mem[0xC000] == 0xC3 && mem[0xC001] == 0x9A && mem[0xC002] == 0xC1
		&& mem[0xC0D7] == 0xF3 && mem[0xC162] == 0xF3
		&& mem[0xC009] != 0xC3))
		return 0;
	if (mem[0xC003] == 0xC3 && mem[0xC004] == 0xD7 && mem[0xC005] == 0xC0
		&& mem[0xC006] == 0xC3)
		return 1;
	if (mem[0xC003] == 0x21 && mem[0xC006] == 0xC3 && mem[0xC007] == 0xD7
		&& mem[0xC008] == 0xC0)
		return 1;
	return 0;
}

/* xanadu_80sr PATCH@F000: IN (01)、CP 6、F0BC の 6 バイト行 */
static int CEmuPc88PatchXanadu80sr(const uint8_t* mem, int initPc)
{
	if (!mem || initPc != 0xf000)
		return 0;
	if (!(mem[0xF000] == 0xF3 && mem[0xF004] == 0xED && mem[0xF005] == 0x5E))
		return 0;
	return (mem[0xF014] == 0xDB && mem[0xF015] == 0x01
		&& mem[0xF032] == 0xFE && mem[0xF033] == 0x06) ? 1 : 0;
}

/* hardrank SMD-88.sb2@8A00: (79D7)<$38 で OPNA ワーク $4446 を $A8AC と入れ替え */
static int CEmuPc88PatchHardrankSb2(const uint8_t* mem)
{
	if (!mem || mem[0x8A00] != 0xF3)
		return 0;
	return (mem[0x8A07] == 0x3A && mem[0x8A08] == 0xD7 && mem[0x8A09] == 0x79
		&& mem[0x8A35] == 0x21 && mem[0x8A36] == 0x00 && mem[0x8A37] == 0x93) ? 1 : 0;
}

/* SMD-88 長さ 0 コマンドのオペランドサイズ（cmd id の後のバイト数） */
static const uint8_t kHardrankSmdOps[32] = {
	1, 1, 0, 0, 1, 1, 1, 1,
	1, 1, 0, 0, 1, 1, 2, 1,
	2, 2, 1, 1, 0, 1, 0, 1,
	1, 1, 2, 1, 1, 1, 0, 0
};

/* MA103/MA108/MA200 は 00 0D nn で開始。残りは 8B26 から RETURN（8FD4 経由キーオフ）し jp 8BA7 ではない。待ち中の入れ子 RTC が 9130 をリセットしチャネルが rest から出ない — SILENT/NOSEQ。00 00 05 に書き換え（同じ 3 バイト枠、MA102 と同様に連鎖）。
   MA105 ch2-5 は cmd01 のあと 2 つ目の $40 rest。それも書き換え、最初の ISR が 0x026A ノートに届くように。MA101 の $40 rest はセットアップ cmd のあと — 同じパス。ストリーム後方の短い音楽 rest（<$20）は触らない。 */
static void CEmuPc88SkipHardrankLeadRest(uint8_t* mem)
{
	int i, n;
	if (!mem)
		return;
	if (mem[0x9301] > 0x0B)
		mem[0x9301] = 0x0B;
	for (i = 0; i < 6; i++) {
		unsigned p = (unsigned)mem[0x9302 + i * 2]
			| ((unsigned)mem[0x9303 + i * 2] << 8);
		if (p < 0x9200u || p + 3u >= 0xA200u)
			continue;
		for (n = 0; n < 8 && p + 2u < 0xA200u; n++) {
			if (mem[p] != 0)
				break;
			{
				const unsigned cmd = mem[p + 1];
				const unsigned opsz = (cmd < 32u) ? kHardrankSmdOps[cmd] : 1u;
				if (cmd == 0x0Du) {
					const unsigned dur = mem[p + 2];
					if (n == 0 || dur >= 0x20u) {
						mem[p + 1] = 0x00;
						mem[p + 2] = 0x05;
					}
					p += 3u;
					continue;
				}
				p += 2u + opsz;
			}
		}
	}
}

/* arcus88demo（wolfteam 88/87 @B000）: PATCH は (B000) に HL=4000、(B003) に IN A,(80)。ISR は (B003)==0 または (B007)==0 のときだけ tick。FFFF フレーズ終端が (B007)=FF。port 80 非 0（タイトル下位）だと最初のループ後にプレーヤが死ぬ — C5 は短時間後無音、C6 は約 40s で STOP。各 bgm は 1 曲。port 80 は 0。 */
static int CEmuPc88PatchArcusB000(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18)
		return 0;
	if (!(mem[0x19] == 0x21 && mem[0x1A] == 0x00 && mem[0x1B] == 0x40
		&& mem[0x1C] == 0x22 && mem[0x1D] == 0x00 && mem[0x1E] == 0xB0))
		return 0;
	for (int i = 0x20; i + 5 < 0x50; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0x32 && mem[i + 3] == 0x03 && mem[i + 4] == 0xB0)
			return 1;
	}
	return 0;
}

/* yaksa PATCH2: (0575)=0x21 を植える。play は IN (01)/OR A/JR Z で skip。Falcom 風 vdata+voiceBank 経路で port01=0 にしてはいけない（mute）。 */
int CEmuPc88PatchYaksa2(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18)
		return 0;
	return (mem[0x10] == 0xF3 && mem[0x18] == 0x3E && mem[0x19] == 0x21
		&& mem[0x1A] == 0x32 && mem[0x1B] == 0x75 && mem[0x1C] == 0x05) ? 1 : 0;
}

/* lizard88: MAIN@9F00 復号 + page0 Timer ISR @009C（PUSH AF） */
static int CEmuPc88PatchLizardTimer(const uint8_t* mem)
{
	if (!mem || mem[0] != 0x18 || mem[8] != 0x9C || mem[9] != 0x00)
		return 0;
	return (mem[0x9C] == 0xF5 && mem[0x9F00] == 0xC3) ? 1 : 0;
}

/* 4 バイト period/dur/wait の 3 チャネル、FF 終端、表は A576。チャネル 1 イベントはワンショット。2 チャネル以上で異なる period が 2 つ以上なら BGM。同音スティング（0x41）は JR 0051 すると持続ドローンになる。 */
static int CEmuPc88LizardSongIsBgm(const uint8_t* mem, unsigned song)
{
	if (!mem || song > 0x4Cu)
		return 0;
	const unsigned tab = 0xA576u + song * 6u;
	if (tab + 6u >= 0x10000u)
		return 0;
	int variedCh = 0;
	for (int ch = 0; ch < 3; ch++) {
		unsigned p = (unsigned)mem[tab + (unsigned)ch * 2u]
			| ((unsigned)mem[tab + (unsigned)ch * 2u + 1u] << 8);
		if (p < 0x9F00u || p >= 0xC000u)
			continue;
		int n = 0, nSeen = 0;
		unsigned seen[8];
		while (n + 3 < 256 && p + (unsigned)n + 3u < 0x10000u
			&& mem[p + (unsigned)n] != 0xFF) {
			const unsigned per = (unsigned)mem[p + (unsigned)n]
				| ((unsigned)mem[p + (unsigned)n + 1u] << 8);
			int k = 0;
			for (; k < nSeen; k++) {
				if (seen[k] == per)
					break;
			}
			if (k == nSeen && nSeen < 8)
				seen[nSeen++] = per;
			n += 4;
		}
		if (n > 4 && nSeen >= 2)
			variedCh++;
	}
	return variedCh >= 2;
}

/* 1942_88: cmd=1 → CALL ADEE（LDIR MUSIC）のあと CALL A343（I=80+Timer） */
static int CEmuPc88Patch1942LongPlay(const uint8_t* mem)
{
	if (!mem)
		return 0;
	/* PATCH は A375 に C9 を植え、CALL 0034（ADEE）/ CALL A343 */
	int sawRetPlant = 0, sawAdeee = 0, sawA343 = 0;
	for (int i = 0; i + 3 < 0x40; i++) {
		if (mem[i] == 0x3E && mem[i + 1] == 0xC9
			&& mem[i + 2] == 0x32 && mem[i + 3] == 0x75 && mem[i + 4] == 0xA3)
			sawRetPlant = 1;
		if (mem[i] == 0xCD && mem[i + 1] == 0x34 && mem[i + 2] == 0x00)
			sawAdeee = 1;
		if (mem[i] == 0xCD && mem[i + 1] == 0x43 && mem[i + 2] == 0xA3)
			sawA343 = 1;
	}
	return sawRetPlant && sawAdeee && sawA343;
}

/* makai88: LD IX,0274 / LD IY,0276。play は IN A,(80); OR A; IN A,(01); JR NZ → (IY)=param（SE）、さもなくば (IX)=param（BGM）。IN A,(n) はフラグを触らないので port80 がメールボックス、port01 が曲 id。タイトル hi24 = SE（非0）/BGM（0）。下位バイトが曲番号。 */
static int CEmuPc88PatchMakaiIxIy(const uint8_t* mem)
{
	if (!mem) return 0;
	int sawIx = 0, sawIy = 0;
	for (int i = 0; i + 3 < 0x80; i++) {
		if (mem[i] == 0xDD && mem[i + 1] == 0x21
			&& mem[i + 2] == 0x74 && mem[i + 3] == 0x02)
			sawIx = 1;
		if (mem[i] == 0xFD && mem[i + 1] == 0x21
			&& mem[i + 2] == 0x76 && mem[i + 3] == 0x02)
			sawIy = 1;
	}
	if (!sawIx || !sawIy)
		return 0;
	for (int i = 0; i + 12 < 0x80; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80
			&& mem[i + 2] == 0xB7
			&& mem[i + 3] == 0xDB && mem[i + 4] == 0x01
			&& mem[i + 5] == 0x20
			&& mem[i + 7] == 0xDD && mem[i + 8] == 0x77
			&& mem[i + 10] == 0xC9
			&& mem[i + 11] == 0xFD && mem[i + 12] == 0x77)
			return 1;
	}
	return 0;
}

/* PATCH play: IN A,(80); LD D,A; LD E,0; … LDIR は DE = song<<8 を far 番地に使う（galfstrm/meltdown は HL=4000→DE、duel は HL=1000→DE）。ポート 80 はタイトル hi16 のページ上位バイト。 */
static int CEmuPc88PatchSongShift8(const uint8_t* mem)
{
	if (!mem) return 0;
	for (int i = 0; i < 0x80; i++) {
		if (mem[i] != 0xDB || mem[i + 1] != 0x80)
			continue;
		int sawDe = 0, sawLdir = 0;
		for (int k = i; k < i + 32 && k + 2 < 0x100; k++) {
			if (mem[k] == 0x57 && mem[k + 1] == 0x1E && mem[k + 2] == 0x00)
				sawDe = 1; /* LD D,A; LD E,0（ページ上位） */
			if (mem[k] == 0xED && mem[k + 1] == 0xB0)
				sawLdir = 1;
		}
		if (sawDe && sawLdir)
			return 1;
	}
	return 0;
}

/* galfstrm/meltdown: LDIR 元は HL=4000（先 DE=titlepage） */
static int CEmuPc88PatchLdirFrom4000(const uint8_t* mem)
{
	if (!mem || !CEmuPc88PatchSongShift8(mem))
		return 0;
	for (int i = 0; i < 0x80; i++) {
		if (mem[i] != 0xDB || mem[i + 1] != 0x80)
			continue;
		for (int k = i; k < i + 32 && k + 2 < 0x100; k++) {
			if (mem[k] == 0x21 && mem[k + 1] == 0x00 && mem[k + 2] == 0x40)
				return 1;
		}
	}
	return 0;
}

/* Falcom YS/Ys2 PATCH: IN A,(01); AND F0; CP 10 → TTL / MANPR / END 分岐 */
static int CEmuPc88PatchFalcomAndF0Cp10(const uint8_t* mem)
{
	if (!mem)
		return 0;
	for (int i = 0; i + 3 < 0x80; i++) {
		if (mem[i] == 0xE6 && mem[i + 1] == 0xF0
			&& mem[i + 2] == 0xFE && mem[i + 3] == 0x10)
			return 1;
	}
	return 0;
}

/* ys2_88: LD HL,C000 / LD BC,1000 / LDIR、DE = バンク表+6（TTL→3000、MANPR→4D00、END→2000）。曲ファイルはその dest にリンク。カタログ mdata=C000 だけだと MANPR は PATCH まで空の 4D00 を読む。誤 param はその LDIR に届かない。 */
static int CEmuPc88PatchFalcomLdirFromC000(const uint8_t* mem)
{
	if (!mem)
		return 0;
	for (int i = 0; i + 7 < 0x80; i++) {
		if (mem[i] == 0x21 && mem[i + 1] == 0x00 && mem[i + 2] == 0xC0
			&& mem[i + 3] == 0x01 && mem[i + 4] == 0x00 && mem[i + 5] == 0x10
			&& mem[i + 6] == 0xED && mem[i + 7] == 0xB0)
			return 1;
	}
	return 0;
}

/* CEmuPc88FalcomYs2MusDest の実装 */
static unsigned CEmuPc88FalcomYs2MusDest(const uint8_t* mem, unsigned bank)
{
	const unsigned nibble = bank & 0xf0u;
	int table = 0x61;
	if (nibble == 0x10u)
		table = 0x69;
	else if (nibble > 0x10u)
		table = 0x71;
	if (mem && table + 7 < 0x100) {
		const unsigned d = (unsigned)mem[table + 6]
			| ((unsigned)mem[table + 7] << 8);
		if (d >= 0x200u && d < 0xF000u)
			return d;
	}
	if (nibble == 0x10u)
		return 0x4d00u;
	if (nibble > 0x10u)
		return 0x2000u;
	return 0x3000u;
}

/* PATCH は param 再読の前に必ず CALL (0079) stop。ROM イメージは TTL ベクタを残す。ENDPRG は約 0100..20FF だけなので TTL stop@2D9E が残留 TTLPRG を走り END ドライバを壊す。 */
static void CEmuPc88FalcomYs2PlantTable(uint8_t* mem, unsigned bank)
{
	if (!mem || !CEmuPc88PatchFalcomLdirFromC000(mem))
		return;
	const unsigned nibble = bank & 0xf0u;
	int table = 0x61;
	if (nibble == 0x10u)
		table = 0x69;
	else if (nibble > 0x10u)
		table = 0x71;
	memcpy(mem + 0x79, mem + table, 6);
}

/* ashe DRIVER@80EE: CP 10 / JP NC — 7803..05 の要求バイトは >= 0x10。タイトル上位（0x10/0x11）が 9152 表添字。下位はステージした MUS* バンク選択のみ。 */
static int CEmuPc88AshePlayHi(const uint8_t* mem)
{
	if (!mem || mem[0x7800] != 0xC3)
		return 0;
	if (mem[0x80EE] != 0x36 || mem[0x80EF] != 0x00
		|| mem[0x80F0] != 0xFE || mem[0x80F1] != 0x10)
		return 0;
	/* 80F6: LD L,A / LD H,0 / ADD HL,HL / LD DE,9152（曲表） */
	return (mem[0x80FA] == 0x11 && mem[0x80FB] == 0x52
		&& mem[0x80FC] == 0x91) ? 1 : 0;
}

/* ashe PATCH/DRIVER: song*2 でワード表（LD DE,9152）を引き、その ptr を 0x4000 へ LDIR。ROM 表はゴミ。ホストが選んだ曲のステージ済み mdata 番地を植える。 */
static void CEmuPc88PlantAsheSongTable(uint8_t* mem, unsigned songNum, int mdataAddr)
{
	if (!mem || mdataAddr < 0 || mdataAddr >= 0x10000 || songNum > 0x1e)
		return;
	int table = -1;
	for (int i = 0; i < 0x80; i++) {
		if (mem[i] == 0xDB && mem[i + 1] == 0x80) {
			for (int k = i; k < i + 40 && k + 2 < 0x100; k++) {
				if (mem[k] == 0x11) {
					const unsigned de = (unsigned)mem[k + 1]
						| ((unsigned)mem[k + 2] << 8);
					/* ashe は 9152。DRIVER ROM 頁内の任意の表を許す */
					if (de >= 0x7800 && de < 0xA000) {
						table = (int)de;
						break;
					}
				}
			}
		}
		if (table >= 0) break;
	}
	/* 同じ表の DRIVER ミラー（ashe @80FB） */
	if (table < 0) {
		for (int i = 0x8000; i < 0x8200; i++) {
			if (mem[i] == 0x11) {
				const unsigned de = (unsigned)mem[i + 1]
					| ((unsigned)mem[i + 2] << 8);
				if (de >= 0x7800 && de < 0xA000
					&& mem[i - 2] == 0x29 && mem[i - 3] == 0x00)
					table = (int)de;
			}
			if (table >= 0) break;
		}
	}
	if (table < 0 || table + (int)songNum * 2 + 1 >= 0x10000)
		return;
	const int p = table + (int)songNum * 2;
	mem[p] = (uint8_t)(mdataAddr & 0xff);
	mem[p + 1] = (uint8_t)((mdataAddr >> 8) & 0xff);
}

/* CHardPc88::PlaySongIndex の実装 */
uint8_t CHardPc88::PlaySongIndex() const
{
	/* yaksa PATCH2: IN (01)/OR A/JR Z は param==0 で play を飛ばす。タイトル 0xPP00BB の中間バイトが再生 id（0x030002→03）。0 を返さない。 */
	if (yaksaPatch2_ || CEmuPc88PatchYaksa2(mem_)) {
		const unsigned mid = (titleCode_ >> 16) & 0xff;
		const unsigned songNum = titleCode_ & 0xff;
		if (mid != 0)
			return (uint8_t)mid;
		if (songNum != 0)
			return (uint8_t)songNum;
		return 1;
	}
	/* mule: IN (80)/RRA が CALL 8D05 をパッチ。奇数（MAIN 01000000）は CALL 8B51。偶数は CALL 8FE5 のまま（ポート 32 を OR 80 = IRQ マスク）。ページ選択はポート 01 = タイトル下位であり、ここではない。 */
	if (CEmuPc88PatchMulePages(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* PMD@4000: ファイルあたり 1 MML */
	if (CEmuPc88PatchPmdHl4000(mem_, initPc_))
		return 0;
	if (CEmuPc88PatchRobowr(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchXzrA4(mem_))
		return 0;
	if (CEmuPc88PatchGinei2(mem_))
		return 0;
	if (CEmuPc88PatchAfHl4400(mem_) || CEmuPc88PatchRomanciaSr(mem_, initPc_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchLvaccus9800(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* 標準 hoot PATCH: ポート 80 はドライバ再生入口へ渡すバイト。これらのリップはタイトル上位（triton2 効果 id、goonies88 曲、valis オープニング分岐）。 */
	if (CEmuPc88PatchHootCmd01(mem_, initPc_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchDprime(mem_) || CEmuPc88PatchAdrnalin(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchOnion9006(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchPwmajan2(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (CEmuPc88PatchGandhara(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* makai88: port80 は BGM/SE 選択（タイトル hi24）であり曲番号ではない */
	if (CEmuPc88PatchMakaiIxIy(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* smariosp: port80!=0 はメールボックス植込を飛ばす（曲 id 無しで CALL play） */
	if (CEmuPc88PatchPort80GateParam(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* yokosuka 効果: low=FF、hi24 はポート 80 経由で (E23C) へ書く id */
	if (CEmuPc88PatchYokosukaEff(mem_) && (titleCode_ & 0xffu) == 0xffu)
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* gineiden: 各 BGM_n.COM は mdata の 1 曲。AMAIN は IN (80) の B*6 で引く。タイトル下位がバンク — port 80 は 0。さもなくばチャネル表へ飛び mute。 */
	if (armGineidenTimer_)
		return 0;
	if (CEmuPc88PatchArcusB000(mem_))
		return 0;
	/* yakyufan: 各 MUS* は 0x4000 の 1 バンク。(010B)/port80 はファイル内曲添字。バンク id（0..0x0A）を渡すと唯一のフレーズ表を過ぎ、mute TL でキーオン。 */
	if (NeedsYakyufanArm())
		return 0;
	if (CEmuPc88PatchPort01Then80(mem_, initPc_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	if (packedKoei_) {
		/* valis2 OPNA: PATCH は param>=0xE0 を ADPCM ボイス（PCM00..）へ。BGM は 0x4000 のパック CIM を添字 0 で再生。 */
		const unsigned songNum = titleCode_ & 0xff;
		if (songNum >= 0xE0)
			return (uint8_t)songNum;
		return 0;
	}
	const unsigned songNum = titleCode_ & 0xff;
	const int titleMdata = CEmuPc88TitleEncodedMdata(titleCode_, mdataAddrDefaulted_, wolfteamMode_);
	/* song<<8 LDIR PATCH: ポート 80 はページ上位（タイトル hi16） */
	if (CEmuPc88PatchSongShift8(mem_)) {
		unsigned hi = 0;
		if (titleMdata >= 0)
			hi = (unsigned)titleMdata;
		else {
			const unsigned tHi = (titleCode_ >> 16) & 0xffffu;
			if (tHi >= 0x1000 && tHi <= 0xE000 && (tHi & 0xffu) == 0
				&& (titleCode_ & 0xff00u) == 0)
				hi = tHi;
		}
		if (hi != 0)
			return (uint8_t)((hi >> 8) & 0xff);
	}
	/* ashe: 上位は 9152/7803 再生添字（>=0x10）。下位 = MUS バンク */
	if (CEmuPc88AshePlayHi(mem_))
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* タイトル hi16 はロード番地だった（MMLEX/galfstrm）。他の表インデックス PATCH は下位バイトのバンク添字を残す。 */
	if (titleMdata >= 0)
		return (uint8_t)songNum;
	/* FE19/kogado（arugies/schwarz）: タイトル上位がバンク内曲添字。下位は mdata にステージした MUS* バンク選択のみ。 */
	if (mem_[0xE000] == 0xFE && mem_[0xE001] == 0x19)
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* Falcom YS/Ys2 風（vdata 音色 + MUS バンク、mfile_size 無し）: 各 MUS は 1 曲。hi24 = 再生/param、lo = 既ステージバンク。バンク id を再生添字にすると無意味フレーズ（ch1 のみ／SSG 不一致）。manreq88 は mfile_size>0 で下位曲を残す。
	   例外: ssymphony PATCH は CP 1 / IN (01) で param==0 なら CALL を飛ばす — 素の 0x01/0x02 はポート 01 にバンク id が要る。 */
	if (vdataAddr_ >= 0 && mfileSize_ <= 0 && songNum < 256 && voiceBank_[songNum]) {
		const unsigned hi = (titleCode_ >> 24) & 0xff;
		if (hi != 0)
			return (uint8_t)hi;
		if (mem_) {
			for (int i = 0; i + 8 < 0x50; i++) {
				if (mem_[i] != 0xFE || mem_[i + 1] != 0x01)
					continue;
				/* CP 1; JR NZ; …; IN A,(01) — ssymphony は DI のまま IN */
				for (int k = i + 2; k + 1 < i + 12 && k + 1 < 0x50; k++) {
					if (mem_[k] == 0xDB && mem_[k + 1] == 0x01)
						return (uint8_t)songNum;
				}
			}
		}
		return 0;
	}
	/* ポインタ表バンク（Herzog/Tecnosoft、tenchi、triton2）: word[mdata+2] はステージ済みバンクへの絶対 ptr。再生添字はタイトル上位。ys3_88 もバンク内 ptr があるが high==0 かつ mfile_size 無しなら下位曲番号で再生。 */
	if (mdataAddr_ >= 0 && mdataAddr_ + 3 < 0x10000) {
		const unsigned p = (unsigned)mem_[mdataAddr_ + 2]
			| ((unsigned)mem_[mdataAddr_ + 3] << 8);
		const unsigned mend = (unsigned)mdataAddr_
			+ (unsigned)(mdataSize_ > 0 ? mdataSize_ : 0x1000);
		/* 厳密に > mdata: lyrane/OP は word[2]==mdata（ld sp,mdata） */
		if (p > (unsigned)mdataAddr_ && p < mend) {
			const unsigned hi = (titleCode_ >> 24) & 0xff;
			/* タイトル上位の再生添字があれば優先（Herzog/Tecnosoft）。manreq88: mfile_size>0 でバンク内 ptr があるが曲は下位 — hi=0 を返すと全タイトル mute。 */
			if (hi != 0)
				return (uint8_t)hi;
			return (uint8_t)songNum;
		}
	}
	/* 非ゼロが上位だけのタイトル（adrnalin MUS00-02…、dione、prontis EFFECT #nn）: 下位はステージ済みバンク名（バンク 0）、上位は PATCH がポート 80 で渡す再生添字。下位を返すと全タイトルが曲 0 を要求し、これらドライバは曲 0 を停止と読む。 */
	if (songNum == 0 && ((titleCode_ >> 8) & 0xffffu) == 0
		&& ((titleCode_ >> 24) & 0xffu) != 0)
		return (uint8_t)((titleCode_ >> 24) & 0xffu);
	return (uint8_t)songNum;
}

/* CHardPc88::PlayParamIndex の実装 */
uint8_t CHardPc88::PlayParamIndex() const
{
	if (CEmuPc88PatchMulePages(mem_) || CEmuPc88PatchLvaccus9800(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	/* PMD メールボックス @4000: カタログ下位が既にステージ済みファイルを選んでいる */
	if (CEmuPc88PatchPmdHl4000(mem_, initPc_))
		return 0;
	if (CEmuPc88PatchRobowr(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchGinei2(mem_) || CEmuPc88PatchXzrA4(mem_))
		return 0;
	if (CEmuPc88PatchAfHl4400(mem_) || CEmuPc88PatchRomanciaSr(mem_, initPc_))
		return (uint8_t)(titleCode_ & 0xff);
	/* 標準 hoot PATCH: ポート 01 はドライバメールボックスへエコーする曲／バンク。ここで PlaySongIndex() へ落とすと全リップがポート 80 の再生バイトを曲として渡す。 */
	if (CEmuPc88PatchHootCmd01(mem_, initPc_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchPort01Then80(mem_, initPc_))
		return (uint8_t)(titleCode_ & 0xff);
	/* 1942_88: IN A,(01) → (829C) 曲 id。HootCmd01 は不一致（JR NZ と IN 01 の間に CALL ADEE）。 */
	if (CEmuPc88Patch1942LongPlay(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchDprime(mem_) || CEmuPc88PatchAdrnalin(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchOnion9006(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchPwmajan2(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	if (CEmuPc88PatchGandhara(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	/* makai88: port01 は (0274) BGM または (0276) SE へ書く曲 id */
	if (CEmuPc88PatchMakaiIxIy(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	/* smariosp: port01 は port80==0 のとき LD (mailbox),A する曲 id */
	if (CEmuPc88PatchPort80GateParam(mem_))
		return (uint8_t)(titleCode_ & 0xff);
	/* yokosuka: low=FF が効果メールボックス経路（param は FF のまま） */
	if (CEmuPc88PatchYokosukaEff(mem_) && (titleCode_ & 0xffu) == 0xffu)
		return 0xff;
	/* gineiden: param!=0 が AMAIN vs DEMO ジャンプパッチを選ぶ。タイトルを残す。 */
	if (armGineidenTimer_)
		return (uint8_t)(titleCode_ & 0xff);
	/* Falcom YS/Ys2 PATCH（AND F0 / CP 10）: ポート 01 は TTL（<10）/ MANPR（==10）/ END（>10）を選ぶバンク id。タイトル hi24 はポート 80 のバンク内ページのみ。ページを param に返すと全 *MUS が TTL ジャンプへ — 無音または誤ドライバ。 */
	if (CEmuPc88PatchFalcomAndF0Cp10(mem_) && vdataAddr_ >= 0 && mfileSize_ <= 0) {
		const unsigned bank = titleCode_ & 0xffu;
		if (bank < 256u && voiceBank_[bank])
			return (uint8_t)bank;
	}
	/* navitune 系: 下位はバンク 0、bit 8..23 はファイルオフセット、bit 24..31 はポート 01 の再生モード（PATCH は LDIR 後に IN A,(01)）。 */
	if ((titleCode_ & 0xffu) == 0 && ((titleCode_ >> 8) & 0xffffu) != 0
		&& mdataAddr_ >= 0 && mfileSize_ > 0)
		return (uint8_t)((titleCode_ >> 24) & 0xff);
	/* song<<8: port80 = ページ、port01 = タイトル下位（duel は param でゲート） */
	if (CEmuPc88PatchSongShift8(mem_)) {
		const int titleMdata = CEmuPc88TitleEncodedMdata(
			titleCode_, mdataAddrDefaulted_, wolfteamMode_);
		const unsigned tHi = (titleCode_ >> 16) & 0xffffu;
		if (titleMdata >= 0
			|| (tHi >= 0x1000 && tHi <= 0xE000 && (tHi & 0xffu) == 0
				&& (titleCode_ & 0xff00u) == 0))
			return (uint8_t)(titleCode_ & 0xff);
	}
	/* 上位のみタイトル: PlaySongIndex() は上位をポート 80 へ。ポート 01 はバンク id。goonies88 の PATCH は port01!=0 なら停止分岐。adrnalin はポート 01 をエコーするだけ。 */
	if ((titleCode_ & 0xffu) == 0 && ((titleCode_ >> 8) & 0xffffu) == 0)
		return 0;
	return PlaySongIndex();
}

/* ゲスト状態を修復する */
void CHardPc88::FixupIm2AfterBoot()
{
	CEmuPc88InstallFe19Im2(mem_, cpu_);
}

/* カタログは機械が出せるクロックを言い、stub の IM2 表はプレーヤが実際に聞くものを言う。両方が分かったら空ベクタのカタログクロックを落とす — 届けるとゲストに余計な ISR が入るだけ。最後の 1 つは落とさない。表が未植のときは推測しない。 */
void CHardPc88::PruneDeadTickSources()
{
	if (!mem_ || !cpu_ || cpu_->r.im != 2)
		return;
	if (!useVrtc || !useRtc)
		return;
	const unsigned iBase = ((unsigned)cpu_->r.i) << 8;
	if (iBase + 9 >= 0x10000)
		return;
	const unsigned vrtc = (unsigned)mem_[iBase + 2] | ((unsigned)mem_[iBase + 3] << 8);
	const unsigned rtc = (unsigned)mem_[iBase + 4] | ((unsigned)mem_[iBase + 5] << 8);
	if (vrtc == 0 && rtc != 0)
		useVrtc = 0;
	else if (rtc == 0 && vrtc != 0)
		useRtc = 0;
}

/* ゲスト状態を修復する */
void CHardPc88::FixupIm2AfterPlay()
{
	/* lvaccus play は I=1 / vec02=$9803。settle 中は VRTC を止め、I=0 が 0002（ED 5E）を取って PROG へ迷わないようにする。 */
	if (CEmuPc88PatchLvaccus9800(mem_) && cpu_ && cpu_->r.i == 1)
		useVrtc = 1;
	/* mule 8B01 は I=5F / vec02=8EB5（SSG）/ vec08=8B9E（FM）。Timer B が FM クロック。VRTC はまだ 8EB5。settle 中は止める。 */
	if (CEmuPc88PatchMulePages(mem_) && cpu_ && cpu_->r.i == 0x5F)
		useVrtc = 1;
	if (CEmuPc88PatchXzrA4(mem_) && cpu_ && cpu_->r.i == 0xA4)
		useVrtc = 1;
	if (!mirrorSoundToRtc_ || !cpu_ || !mem_)
		return;
	const unsigned iBase = ((unsigned)cpu_->r.i) << 8;
	if (iBase + 9 >= 0x10000)
		return;
	const unsigned snd = (unsigned)mem_[iBase + 8]
		| ((unsigned)mem_[iBase + 9] << 8);
	const unsigned rtc = (unsigned)mem_[iBase + 4]
		| ((unsigned)mem_[iBase + 5] << 8);
	if (snd < 0x40 || snd >= 0x10000)
		return;
	if (rtc != 0)
		return;
	mem_[iBase + 4] = (uint8_t)(snd & 0xff);
	mem_[iBase + 5] = (uint8_t)(snd >> 8);
}

/* 再生／タイマを武装する */
void CHardPc88::ArmN88RtcPlayer()
{
	/* 完全ゲーム本体は無い: DEMOM/MUSIC は I:04 に植える RTC ISR 番地を知っている（I=F3 で F304）。ポート再生は植込を外すか E80E stop stub を残す — 最初の tick 前にレイアウトを戻す。 */
	if (!mem_ || !cpu_ || n88RtcIsr_ == 0)
		return;
	if (cpu_->r.i != 0xF3)
		return;
	if (n88RtcIsr_ >= 0x10000 || mem_[n88RtcIsr_] != 0xF5)
		return; /* ISR イメージが無い — ベクタを発明しない */
	mem_[0xF304] = (uint8_t)(n88RtcIsr_ & 0xff);
	mem_[0xF305] = (uint8_t)(n88RtcIsr_ >> 8);
	if (n88RtcThrottleAddr_ && n88RtcThrottleAddr_ < 0x10000)
		mem_[n88RtcThrottleAddr_] = 0x01;
	/* SP を IM2 ページから離す。play が SP を F3xx 近くに残すと次 IRQ の push が F304 をゴミ ISR 番地に潰す。 */
	if (cpu_->r.sp >= 0xF000 || cpu_->r.sp < 0x0100)
		cpu_->r.sp = 0x0200;
	cpu_->r.iff1 = 1;
}

/* 再生／タイマを武装する */
void CHardPc88::ArmGineidenOpnTimer()
{
	if (!armGineidenTimer_ || !chip_ || !cpu_)
		return;
	/* AMAIN@4E1B に合わせる: OUT 44,27 / OUT 45,2A のあとポート 32 マスク解除 */
	PortOut(0x44, 0x27);
	PortOut(0x45, 0x2A);
	ioPorts_[0x32] = (uint8_t)(ioPorts_[0x32] & 0x7F);
	soundIrqMasked = 0;
	cpu_->r.iff1 = 1;
}

/* 再生／タイマを武装する */
void CHardPc88::ArmFallbackOpnTimer()
{
	if (!chip_ || !cpu_)
		return;
	/* ウォッチドッグ最後の手段: ブートが tick 源を組まなくてもベクタ 08 ハンドラがあるなら、標準 PC-88 プレーヤ相当の約 71Hz Timer B を与えポート 32 を開く。 */
	PortOut(0x44, 0x26);
	PortOut(0x45, 0xCF);
	PortOut(0x44, 0x27);
	PortOut(0x45, 0x3A);
	ioPorts_[0x32] = (uint8_t)(ioPorts_[0x32] & 0x7F);
	soundIrqMasked = 0;
}

/* 再生／タイマを武装する */
void CHardPc88::ArmLizardOpnTimer()
{
	if (!armLizardTimer_ || !chip_ || !cpu_)
		return;
	/* PATCH@002A（MAIN XOR 復号後）が A3DF に JP 00B2 を植え、長さループが (00B1) 経由で Timer B を待つ。その JP が無いと A3B0 が 1 CALL で全曲を消費し PATCH が STOP。 */
	if (mem_ && mem_[0x9F00] == 0xC3 && mem_[0x00B2] == 0xF3
		&& mem_[0x00B3] == 0x3E) {
		mem_[0xA3DF] = 0xC3;
		mem_[0xA3E0] = 0xB2;
		mem_[0xA3E1] = 0x00;
	}
	/* PATCH play: CALL 9F0F のあと忙しい待ちと CALL 9F12 STOP。BGM ストリームは FF 終端なので A3B0 は 1 パスで戻る — 0061 の JR 0051（LD B,2 待ちの上）が再演。1 tick SFX（チャネル 4 バイト）は STOP のまま。さもなくば持続ドローン。 */
	if (mem_ && mem_[0x0051] == 0xD5 && mem_[0x0052] == 0xCD
		&& mem_[0x0061] == 0x06 && mem_[0x0062] == 0x02
		&& CEmuPc88LizardSongIsBgm(mem_, titleCode_ & 0xffu)) {
		mem_[0x0061] = 0x18;
		mem_[0x0062] = 0xEE; /* JR 0051（再演） */
	}
	/* PATCH@0077 に合わせる: Timer B ロード 0x69、モード 0x3A、port32 マスク解除、EI */
	PortOut(0x44, 0x26);
	PortOut(0x45, 0x69);
	PortOut(0x44, 0x27);
	PortOut(0x45, 0x3A);
	ioPorts_[0x32] = (uint8_t)(ioPorts_[0x32] & 0x7F);
	soundIrqMasked = 0;
	/* A572 は 0 のまま。A4E7 が植える「旧 BASIC ROM」フラグで、停止ルーチンが読む: 0 なら A4D3（FF 00 00… 表から OPN 07-0E、ミキサオフ・全音量下げ）。他は A4CD（ポート 40h だけ触り音が残る）。ここで 1 にすると lizard88 全タイトルが最後の音のあとドローン — 掃引 211 件中 63 の固着失敗。 */
	cpu_->r.iff1 = 1;
}

/* 再生／タイマを武装する */
void CHardPc88::ArmYaksaPlay()
{
	if (!mem_ || !(yaksaPatch2_ || CEmuPc88PatchYaksa2(mem_)))
		return;
	/* 0C6A: LD A,1 / LD (37D1),A — CALL 3556 用シーケンサ許可 */
	mem_[0x37D1] = 1;
}

/* 再生／タイマを武装する */
void CHardPc88::ArmPwmajan2()
{
	if (!mem_ || !cpu_ || !CEmuPc88PatchPwmajan2(mem_))
		return;
	/* 9019: I=0、IM2 ワード 0004 = 2060（PROG2 ISR）、IRQ レベル 1 */
	mem_[0x0004] = 0x60;
	mem_[0x0005] = 0x20;
	cpu_->r.i = 0;
	cpu_->r.im = 2;
	cpu_->r.iff1 = 1;
	/* 9019 の OR 80 はディスクローダ用に音源 IRQ をマスク。こちらは生かす。同じ ISR を OPN 枠へ — 1072 は Timer B を武装しない。 */
	mem_[0x0008] = 0x60;
	mem_[0x0009] = 0x20;
	PortOut(0x32, (uint8_t)(PortIn(0x32) & 0x5F));
	PortOut(0xE6, 1);
	PortOut(0xE4, 3);
	PortOut(0x44, 0x26);
	PortOut(0x45, 0xCF);
	PortOut(0x44, 0x27);
	PortOut(0x45, 0x2A);
	soundIrqMasked = 0;
}

/* CHardPc88::PrepareNavitunePatch の実装 */
void CHardPc88::PrepareNavitunePatch()
{
	/* Hoot PATCH の隙間（音源のみ、ゲーム本体無し）:
	   1) play 前 DI / 一度も EI — OPN IM2 が走らない
	   2) 標準再生は cmd07+cmd0E。タイトルは cmd07 のあと cmd10
	   3) poll カウンタは LD HL,017D / INC (HL) — play バイト拡張前に 01FF へ移し、stop stub + JR NZ をリロケ */
	if (!armNavituneTimer_ || !mem_)
		return;
	const unsigned base = (initPc_ > 0) ? (unsigned)initPc_ : 0;
	const unsigned end = base + 0x100u;
	for (unsigned a = base; a + 2u < end && a + 2u < 0x10000u; a++) {
		if (mem_[a] == 0xF3 && mem_[a + 1] == 0xDB && mem_[a + 2] == 0x80) {
			mem_[a] = 0xFB;
			break;
		}
	}
	/* Play LDIR DE=4D00 BC=3000 が C000/1000 を 4D00-7CFF へ。naviprg は 2C00 バイトなので dest 7700-78FF はドライバ／音楽重なり、7900-7CFF は未マップ元 — BGM 00/01 ヘッダと 79xx フレーズが死に、BGM 03（813A）は生き残る。7700（navimus）で止める。 */
	for (unsigned a = base; a + 8u < end && a + 8u < 0x10000u; a++) {
		if (mem_[a] == 0x11 && mem_[a + 1] == 0x00 && mem_[a + 2] == 0x4D
			&& mem_[a + 3] == 0x01 && mem_[a + 4] == 0x00 && mem_[a + 5] == 0x30
			&& mem_[a + 6] == 0xED && mem_[a + 7] == 0xB0) {
			mem_[a + 4] = 0x00;
			mem_[a + 5] = 0x2A;
			break;
		}
	}
	/* 空のリターンスタック（5752）の F2 は (52CF) をゼロにし、4EC0 の tick がシーケンサを飛ばす。BGM 01 のリストはチャネルを裸 F2（7777、次フレーズの 1 バイト前）へ向けるので初期 IX+6 待ちのあと曲全体が死ぬ。そのチャネルだけ終端。両コピーをパッチ。play LDIR が 4D00 を C000 から更新。 */
	if (mem_[0x576E] == 0x21 && mem_[0x576F] == 0x00 && mem_[0x5770] == 0x00
		&& mem_[0x5771] == 0x22 && mem_[0x5772] == 0xCF && mem_[0x5773] == 0x52) {
		memset(mem_ + 0x576E, 0x00, 6);
		if (mem_[0xCA6E] == 0x21)
			memset(mem_ + 0xCA6E, 0x00, 6);
		if (mem_[0x1A6E] == 0x21)
			memset(mem_ + 0x1A6E, 0x00, 6);
	}
	for (unsigned a = base; a + 2u < end && a + 2u < 0x10000u; a++) {
		if (mem_[a] == 0x21 && mem_[a + 1] == 0x7D && mem_[a + 2] == 0x01) {
			mem_[a + 1] = 0xFF;
			break;
		}
	}
	ApplyNavituneTitleSong();
	/* 専用 cmd10 再生のあとポート 00 をクリアせず JR poll するとタイトループで再トリガし、曲が ISR に居ない。旧 017D カウンタバイト（INC は 01FF へ）に XOR A; OUT (00),A のあと JR poll。 */
	for (unsigned a = base; a + 6u < end && a + 6u < 0x10000u; a++) {
		if (!(mem_[a] == 0x3E && mem_[a + 1] == 0x10
			&& mem_[a + 2] == 0xCD && mem_[a + 3] == 0x00 && mem_[a + 4] == 0x4D
			&& mem_[a + 5] == 0x18))
			continue;
		const unsigned cave = base + 0x7Du;
		if (cave + 5u >= 0x10000u)
			break;
		const int toCave = (int)cave - (int)(a + 5u + 2u);
		const int toPoll = (int)(base + 0x29u) - (int)(cave + 5u);
		if (toCave < -128 || toCave > 127 || toPoll < -128 || toPoll > 127)
			break;
		mem_[a + 6] = (uint8_t)toCave;
		mem_[cave] = 0xAF;
		mem_[cave + 1] = 0xD3;
		mem_[cave + 2] = 0x00;
		mem_[cave + 3] = 0x18;
		mem_[cave + 4] = (uint8_t)toPoll;
		break;
	}
	/* 再生経路は既に LD A,10 / CALL 4D00 のあと JR poll。cmd10 を init（cmd07 + LD BC,song + cmd0E）へ足すと曲が始まり cmd0E がすぐ止める — 聞こえるのは数 ms のクリック。同じ init の DirectPlayKick も飛ばす（NavituneRetargetPc）。 */
	for (unsigned a = base; a + 4u < end && a + 4u < 0x10000u; a++) {
		if (mem_[a] == 0x3E && mem_[a + 1] == 0x10
			&& mem_[a + 2] == 0xCD && mem_[a + 3] == 0x00 && mem_[a + 4] == 0x4D)
			return;
	}
	for (unsigned a = base; a + 20u < end && a + 20u < 0x10000u; a++) {
		if (!(mem_[a] == 0x3E && mem_[a + 1] == 0x07 && mem_[a + 2] == 0x01
			&& mem_[a + 5] == 0xCD && mem_[a + 6] == 0x00 && mem_[a + 7] == 0x4D))
			continue;
		const unsigned p = a + 8u;
		if (mem_[p] == 0x3E && mem_[p + 1] == 0x10
			&& mem_[p + 5] == 0x3E && mem_[p + 6] == 0x0E)
			return;
		if (!(mem_[p] == 0x3E && mem_[p + 1] == 0x0E
			&& mem_[p + 2] == 0xCD && mem_[p + 3] == 0x00 && mem_[p + 4] == 0x4D
			&& mem_[p + 5] == 0xC9))
			continue;
		uint8_t stop[12];
		memcpy(stop, mem_ + p + 6, sizeof(stop));
		static const uint8_t kCmd10Then0E[] = {
			0x3E, 0x10, 0xCD, 0x00, 0x4D,
			0x3E, 0x0E, 0xCD, 0x00, 0x4D,
			0xC9
		};
		memcpy(mem_ + p, kCmd10Then0E, sizeof(kCmd10Then0E));
		const unsigned stopAt = p + (unsigned)sizeof(kCmd10Then0E);
		memcpy(mem_ + stopAt, stop, sizeof(stop));
		if (stop[10] == 0x18) {
			const unsigned jrAt = stopAt + 10u;
			const unsigned poll = base + 0x29u;
			const int rel = (int)poll - (int)(jrAt + 2u);
			if (rel >= -128 && rel <= 127)
				mem_[jrAt + 1] = (uint8_t)rel;
		}
		for (unsigned j = base; j + 3u < end && j + 3u < 0x10000u; j++) {
			if (mem_[j] != 0xFE || mem_[j + 1] != 0x01 || mem_[j + 2] != 0x20)
				continue;
			const int oldRel = (int)(int8_t)mem_[j + 3];
			const unsigned oldTgt = j + 4u + (unsigned)oldRel;
			if (oldTgt != p + 6u)
				continue;
			const int newRel = (int)stopAt - (int)(j + 4u);
			if (newRel >= -128 && newRel <= 127)
				mem_[j + 3] = (uint8_t)newRel;
			break;
		}
		return;
	}
}

/* CHardPc88::ApplyNavituneTitleSong の実装 */
void CHardPc88::ApplyNavituneTitleSong()
{
	if (!armNavituneTimer_ || !mem_)
		return;
	if (!naviSongAddr_ && mdataAddr_ >= 0) {
		const unsigned fileOff = (titleCode_ >> 8) & 0xffffu;
		naviSongAddr_ = (uint16_t)((unsigned)mdataAddr_ + fileOff);
	}
	if (!naviSongAddr_)
		return;
	/* ヘッダは 06 77 F5 F5 F2 00、続けて id/ptr 三つ組、FF 終端。cmd07 は BC を (52D9) へ。cmd10/532E は word[(52D9)+A*2] をリスト ptr。BC をヘッダへ向けると全曲が 7706（全ヘッダ先頭ワード）を読み BGM 00。リスト番地を mdata（7700）に植え、標準 LD BC,$7700 が動くように。$01E0 は使わない — PATCH SP=$0200 が下へ伸びて潰す。 */
	const unsigned body = (unsigned)naviSongAddr_ + 6u;
	if (body < 6u || body >= 0x10000u)
		return;
	const unsigned slot = (mdataAddr_ >= 0) ? (unsigned)mdataAddr_ : 0x7700u;
	mem_[slot] = (uint8_t)(body & 0xff);
	mem_[slot + 1] = (uint8_t)((body >> 8) & 0xff);
}

/* CHardPc88::NavituneRetargetPc の実装 */
unsigned CHardPc88::NavituneRetargetPc() const
{
	if (!armNavituneTimer_ || !mem_)
		return 0;
	const unsigned base = (initPc_ > 0) ? (unsigned)initPc_ : 0;
	const unsigned end = base + 0x100u;
	/* 専用 cmd10 再生は既にポート 1 経由。そのあと cmd07+cmd0E をキックすると cmd0E が曲を止める — 両タイトルが曲 0 の 2s クリックに見える。 */
	for (unsigned a = base; a + 4u < end && a + 4u < 0x10000u; a++) {
		if (mem_[a] == 0x3E && mem_[a + 1] == 0x10
			&& mem_[a + 2] == 0xCD && mem_[a + 3] == 0x00 && mem_[a + 4] == 0x4D)
			return 0;
	}
	for (unsigned a = base; a + 8u < end && a + 8u < 0x10000u; a++) {
		if (mem_[a] == 0x3E && mem_[a + 1] == 0x07
			&& mem_[a + 2] == 0x01
			&& mem_[a + 5] == 0xCD && mem_[a + 6] == 0x00 && mem_[a + 7] == 0x4D)
			return a;
	}
	return 0;
}

/* CHardPc88::FinishNavitunePlay の実装 */
void CHardPc88::FinishNavitunePlay()
{
	/* PATCH は DI 下で play を終える。IM2 音源 IRQ は届ける。Timer B は書き換えない（ホスト 0x27/0x2A がドライバリロードを凍らせる）。
	   Tick @4EC0 は (4D59) セット中に EI — 入れ子 SOUND IRQ は SP 下に余裕が要る。標準 SP=0200 は PATCH ページへ溢れる。7000 へ（navimus@7700 / naviprg@4D00 の下）。 */
	if (!armNavituneTimer_ || !cpu_)
		return;
	ioPorts_[0x32] = (uint8_t)(ioPorts_[0x32] & 0x7F);
	soundIrqMasked = 0;
	cpu_->r.iff1 = 1;
	if (cpu_->r.sp < 0x4000 || cpu_->r.sp >= 0x7700)
		cpu_->r.sp = 0x7000;
	if (mem_ && naviSongAddr_) {
		const unsigned body = (unsigned)naviSongAddr_ + 6u;
		if (body < 0x10000u) {
			const unsigned slot = (mdataAddr_ >= 0) ? (unsigned)mdataAddr_ : 0x7700u;
			mem_[slot] = (uint8_t)(body & 0xff);
			mem_[slot + 1] = (uint8_t)((body >> 8) & 0xff);
			mem_[0x52D9] = (uint8_t)(slot & 0xff);
			mem_[0x52DA] = (uint8_t)((slot >> 8) & 0xff);
		}
	}
	if (mem_[0x576E] == 0x21 && mem_[0x5771] == 0x22
		&& mem_[0x5772] == 0xCF && mem_[0x5773] == 0x52)
		memset(mem_ + 0x576E, 0x00, 6);
}

/* CHardPc88::NeedsYakyufanArm の実装 */
int CHardPc88::NeedsYakyufanArm() const
{
	if (!mem_ || initPc_ != 0xc000)
		return 0;
	return (mem_[0xc000] == 0xf3 && mem_[0xc001] == 0x31
		&& mem_[0xc004] == 0xcd && mem_[0xc005] == 0x00
		&& mem_[0xc006] == 0x01 && mem_[0x100] == 0xc3) ? 1 : 0;
}

/* 再生／タイマを武装する */
void CHardPc88::ArmYakyufanPlay()
{
	if (!NeedsYakyufanArm() || !mem_)
		return;
	/* Mute@0C5D が (0118) をクリア。無いと VRTC @0BE4 が RET Z しフレーズ／音色取得が走らない（TL=7F キーオン → peak 0）。 */
	mem_[0x115] = 1;
	mem_[0x118] = 1;
	/* ISR@0453 はチャネル毎音色 id を 086A から読む。そこはコード（7F/FE/FF…）と重なり (0123) が ≥0x10 になり CALL 0717 音色ロードを飛ばす — AR/MUL 無し。ch0-2 に FM 音色 id 0/1/2 を植える。 */
	mem_[0x86a] = 0x00;
	mem_[0x86b] = 0x01;
	mem_[0x86c] = 0x02;
	for (int i = 3; i < 9; i++)
		mem_[0x86a + i] = 0xff;
	/* DRIVER は 0453 経路から 0717 に届かない（音色表重なりで ISR の CALL が壊れる）。ch0-2 に基本 FM パッチを種まきし、シーケンサのキーオン/fnum を可聴に。ライブ TL/fnum はゲストから。 */
	if (chip_) {
		auto opn = [this](uint8_t a, uint8_t d) {
			PortOut(0x44, a);
			PortOut(0x45, d);
		};
		for (int ch = 0; ch < 3; ch++) {
			const uint8_t c = (uint8_t)ch;
			opn((uint8_t)(0x30 + c), 0x71);
			opn((uint8_t)(0x34 + c), 0x0d);
			opn((uint8_t)(0x38 + c), 0x33);
			opn((uint8_t)(0x3c + c), 0x01);
			opn((uint8_t)(0x40 + c), 0x23);
			opn((uint8_t)(0x44 + c), 0x2d);
			opn((uint8_t)(0x48 + c), 0x26);
			opn((uint8_t)(0x4c + c), 0x00);
			opn((uint8_t)(0x50 + c), 0x5f);
			opn((uint8_t)(0x54 + c), 0x99);
			opn((uint8_t)(0x58 + c), 0x5f);
			opn((uint8_t)(0x5c + c), 0x94);
			opn((uint8_t)(0x60 + c), 0x05);
			opn((uint8_t)(0x64 + c), 0x05);
			opn((uint8_t)(0x68 + c), 0x05);
			opn((uint8_t)(0x6c + c), 0x07);
			opn((uint8_t)(0x70 + c), 0x02);
			opn((uint8_t)(0x74 + c), 0x02);
			opn((uint8_t)(0x78 + c), 0x02);
			opn((uint8_t)(0x7c + c), 0x02);
			opn((uint8_t)(0x80 + c), 0x11);
			opn((uint8_t)(0x84 + c), 0x11);
			opn((uint8_t)(0x88 + c), 0x11);
			opn((uint8_t)(0x8c + c), 0xa6);
			opn((uint8_t)(0xb0 + c), 0x32);
			opn((uint8_t)(0xb4 + c), 0xc0);
		}
	}
}

/* CHardPc88::NeedsFe19PlayEi の実装 */
int CHardPc88::NeedsFe19PlayEi() const
{
	/* arugies は I=0、ベクタは page 0、play で DI しない。schwarz/schwarz2 は I=01 + play CALL 周り DI。 */
	if (!cpu_ || mem_[0xE000] != 0xFE || mem_[0xE001] != 0x19)
		return 0;
	return cpu_->r.i != 0;
}

/* 広め: PATCH が IM2 に音源ベクタを置き play トリガ後 DI のまま（Falcom E000 PATCH、一部 Wing）。EI が無いと OPN/RTC IRQ が走らずキーオン無音。 */
int CHardPc88::NeedsPlayEi() const
{
	if (!cpu_)
		return 0;
	if (NeedsFe19PlayEi())
		return 1;
	if (forcePlayEi_)
		return 1;
	/* navitune 系 PATCH: play 前 DI、poll 前 EI 無し — ホストが IFF1 を保たないと OPN IM2 が走らない（FE19 と同じ契約）。 */
	if (armNavituneTimer_)
		return 1;
	/* ashe DRIVER@7800: OPN 書込経路は DI。ホスト EI が無いとキーオン無音 */
	if (CEmuPc88AshePlayHi(mem_))
		return 1;
	if (CEmuPc88PatchPwmajan2(mem_))
		return 1;
	if (mem_[0xC000] == 0xC3 && mem_[0xC001] == 0x9A && mem_[0xC002] == 0xC1
		&& mem_[0xC0D7] == 0xF3)
		return 1;
	/* Falcom 特殊 PATCH @E000: JR + IM2 表、play 時 DI */
	if (mem_[0xE000] == 0x18 && mem_[0xE017] == 0xF3 && mem_[0xE018] == 0xED)
		return 1;
	/* yaksa PATCH2: play は DI のまま。VRTC はホスト EI（forcePlayEi_ も） */
	if (yaksaPatch2_ || CEmuPc88PatchYaksa2(mem_))
		return 1;
	/* lizard88/gineiden: JR PATCH は DI 下で poll へ戻る。I ページ音源ベクタは PUSH AF ISR。ホスト EI で Timer B が続く。lizard は I=0 で ISR を 009C（PATCH の page0 表）。 */
	if (mem_[0] == 0x18 && cpu_->r.im == 2 && !cpu_->r.iff1) {
		const unsigned iBase = ((unsigned)cpu_->r.i) << 8;
		const unsigned slot = iBase + 8;
		if (slot + 1 < 0x10000) {
			const unsigned isr = (unsigned)mem_[slot]
				| ((unsigned)mem_[slot + 1] << 8);
			if (isr >= 0x40 && isr + 1 < 0x10000
				&& (mem_[isr] == 0xF5
					|| (mem_[isr] == 0xF3 && mem_[isr + 1] == 0xF5))
				&& (isr < 0x200 || cpu_->r.i == 0))
				return 1;
		}
	}
	if (armLizardTimer_)
		return 1;
	if (CEmuPc88PatchXanadu80sr(mem_, initPc_))
		return 1;
	if (CEmuPc88PatchRobowr(mem_))
		return 1;
	return 0;
}

/* Wing/Konami PATCH は IM2 のあと DI 下で高い DRIVER init を CALL。init は OPN IRQ 待ち（hadou DRIVER1@B166、gra88 DRIVER@8980）。herzog の CALL 81DE は PATCH 内 EI が無いがブート終了時 iff1=1 — 中ページ CALL に一致させない。 */
int CHardPc88::NeedsBootEiPulse() const
{
	if (!cpu_)
		return 0;
	int pc = initPc_;
	if (pc < 0) pc = 0;
	if (pc + 8 >= 0x10000)
		return 0;
	if (mem_[pc] == 0x18) {
		const int rel = (int)(int8_t)mem_[pc + 1];
		pc = pc + 2 + rel;
	}
	if (pc + 8 >= 0x10000)
		return 0;
	/* F3 … ED 5E（IM2）。scheme OPNA は F3 / LD SP / ED 5E（連続ではない） */
	if (mem_[pc] != 0xF3)
		return 0;
	int im2At = -1;
	for (int k = 1; k < 16 && pc + k + 1 < 0x10000; k++) {
		if (mem_[pc + k] == 0xED && mem_[pc + k + 1] == 0x5E) {
			im2At = k;
			break;
		}
		if (mem_[pc + k] == 0xFB && !CEmuPc88IsJrDisp(mem_, pc + k))
			return 0;
	}
	if (im2At < 0)
		return 0;
	int sawHiCall = 0;
	for (int k = im2At + 2; k < 48 && pc + k + 2 < 0x10000; k++) {
		if (mem_[pc + k] == 0xFB && !CEmuPc88IsJrDisp(mem_, pc + k))
			break; /* 最初の本 EI — この後の CALL は再生経路 */
		if (mem_[pc + k] == 0xCD) {
			const unsigned t = (unsigned)mem_[pc + k + 1]
				| ((unsigned)mem_[pc + k + 2] << 8);
			/* gra88 DRIVER@8980。≥0x8900 を残す（herzog CALL 81DE は iff1=1 で settle）。
			   p1demo CALL 811D は意図的にこの下 — 広げると不要なブート EI で recover3 緑が大量回帰。 */
			if (t >= 0x8900)
				sawHiCall = 1;
		}
	}
	return sawHiCall;
}

/* CHardPc88::SchemePlayTrigger の実装 */
void CHardPc88::SchemePlayTrigger(unsigned titleCode)
{
	if (!schemeMode_)
		return;
	const uint8_t bank = (uint8_t)(titleCode & 0xff);
	/* bothtec PATCH@9000: IN (00) poll、続けて IN (80) / LD (C000),A / CALL MUS2。
	   C000 は BGM イメージ（MS0A ヘッダは 00 A8…）。バンク番号（0x0A）を byte0 に書くと FM mute。ポート 0x80 は mem[C000] をエコーするのでそのストアは no-op。hoot OUT(0) と同様に要求バンクを memcpy。 */
	const int livePoll = (mem_[0x9011] == 0xDB && mem_[0x9012] == 0x00);
	if (bank < 128 && bgmBank_[bank] && bgmBankSize_[bank]) {
		unsigned n = bgmBankSize_[bank];
		if (n > 8 * 1024u) n = 8 * 1024u;
		if (0xc000 + n > 0x10000)
			n = 0x10000 - 0xc000;
		memcpy(mem_ + 0xc000, bgmBank_[bank], n);
		if (!livePoll) {
			/* 標準 hoot PATCH は 9010.. を PLAY_* メールボックス（オペコードではない） */
			mem_[0x9012] = 0xff;
			mem_[0x9010] = 0x01;
			mem_[0x9011] = bank;
			mem_[0x9013] = (uint8_t)((titleCode >> 8) & 0xff);
		}
	}
}

/* CHardPc88::DirectPlayKick の実装 */
void CHardPc88::DirectPlayKick(unsigned addr, int ei)
{
	if (!cpu_ || !mem_ || addr == 0 || addr >= 0x10000)
		return;
	if (cpu_->r.sp < 2)
		cpu_->r.sp = 0x0100;
	cpu_->r.sp = (uint16_t)(cpu_->r.sp - 2);
	mem_[cpu_->r.sp] = (uint8_t)(cpu_->r.pc & 0xff);
	mem_[cpu_->r.sp + 1] = (uint8_t)(cpu_->r.pc >> 8);
	cpu_->r.pc = (uint16_t)addr;
	cpu_->r.iff1 = ei ? 1 : 0;
}

/* CHardPc88::SkipUnwedge の実装 */
int CHardPc88::SkipUnwedge() const
{
	if (!cpu_ || !mem_)
		return 0;
	/* mule の CALL 8B01 は PROG@6000。PC を page0 poll へ引き戻すと Timer/I 植込が中断し MAIN が無音。 */
	if (CEmuPc88PatchMulePages(mem_)
		&& cpu_->r.pc >= 0x6000 && cpu_->r.pc < 0xA000)
		return 1;
	if (CEmuPc88PatchRobowr(mem_)
		&& cpu_->r.pc >= 0x818B && cpu_->r.pc < 0xE000)
		return 1;
	return 0;
}

/* CHardPc88::GuardHardrankPc の実装 */
void CHardPc88::GuardHardrankPc()
{
	if (!cpu_ || !mem_)
		return;
	if (hardrankSb2_) {
		/* SMD-88.sb2 は I=$91 / vec04@9104=8AC9。I やベクタがずれると RTC が $28..$89FF の NOP 穴へ落ち、フォールスルーで 8A00 再入、90B2 再ロード、MA102 が 8B26 を二度と tick しない。 */
		cpu_->r.i = 0x91;
		mem_[0x9104] = 0xC9;
		mem_[0x9105] = 0x8A;
		/* MA204 メインスレッド PC が NOP 穴（2295..883A）を歩き 8A00 へ落ち 9130 を消す。EI 中だけ引き戻す（8A00 は 8AC7 まで DI）。PATCH 側 SP。$28..$8A00 窓は 8A00 を中断した。 */
		{
			const unsigned pc = cpu_->r.pc;
			if (cpu_->r.iff1
				&& pc >= 0x2000u && pc < 0x8A00u
				&& cpu_->r.sp >= 0x00F8u) {
				cpu_->r.pc = 0x0006;
				cpu_->r.sp = 0x0100;
			}
		}
	}
	/* harakiri MPLAY: I=7 / IM2 表 @0700（RTC 0704=0A15）。MMAIN が SP を $0100 から $0712 へ上げ、ISR push が表を潰し、次の SOUND/RTC ベクタが 3xxx RAM へ — C19A が固まる（MON_THIN）。
	   SP+表のリセットは PATCH poll に居て SP がベクタ頁のときだけ。PC が曲 RAM へ逃げていたら引き戻す。 */
	if (CEmuPc88PatchHarakiriMplay(mem_)) {
		static const uint8_t kIm2[16] = {
			0x0B, 0x0A, 0x56, 0x0A, 0x15, 0x0A, 0x0B, 0x0A,
			0x0B, 0x0A, 0x0B, 0x0A, 0x0B, 0x0A, 0x0B, 0x0A
		};
		const unsigned pc = cpu_->r.pc;
		const unsigned sp = cpu_->r.sp;
		const int inPatch = (pc < 0x40u);
		const int inProg = (pc >= 0x0710u && pc < 0x1B00u);
		const int inPlay = (pc >= 0xC000u && pc < 0xC700u);
		if (inPatch && sp >= 0x0700u && sp < 0x0800u) {
			cpu_->r.i = 7;
			cpu_->r.sp = 0x0100;
			cpu_->r.iff1 = 1;
			memcpy(mem_ + 0x0700, kIm2, 16);
		} else if (!inPatch && !inProg && !inPlay && pc >= 0x3000u) {
			cpu_->r.i = 7;
			cpu_->r.pc = 0x001B;
			cpu_->r.sp = 0x0100;
			cpu_->r.iff1 = 1;
			memcpy(mem_ + 0x0700, kIm2, 16);
		}
	}
}

/* IRQ 配送 */
int CHardPc88::IgnoreSoundIrqMask() const
{
	/* Falcom OUT (32),OR 80 のあと JP (HL) で type=prog の 0000 へ */
	if (!cpu_ || mem_[0xE000] != 0x18 || mem_[0xE017] != 0xF3)
		return 0;
	return cpu_->r.pc < 0xE000 ? 1 : 0;
}

/* CHardPc88::CmdPollPc の実装 */
int CHardPc88::CmdPollPc() const
{
	if (!mem_)
		return -1;
	/* Falcom 特殊 PATCH: E027 で IN A,(00) / OR A / JR Z */
	if (mem_[0xE000] == 0x18 && mem_[0xE027] == 0xDB && mem_[0xE028] == 0x00
		&& mem_[0xE029] == 0xB7 && mem_[0xE02A] == 0x28)
		return 0xE027;
	auto findAt = [this](unsigned base) -> int {
		const unsigned end = base + 0x70;
		for (unsigned i = base; i + 4 < end && i + 4 < 0x10000u; i++) {
			if (mem_[i] == 0xDB && mem_[i + 1] == 0x00
				&& mem_[i + 2] == 0xB7 && mem_[i + 3] == 0x28)
				return (int)i;
		}
		return -1;
	};
	int p = findAt(0);
	if (p >= 0)
		return p;
	/* blmnstry/hchaser/rouge88/pias88: PATCH は init_pc（JR 後しばしば $1000）であり page 0 ではない。mappy88 は $F000。 */
	if (initPc_ >= 0x80 && initPc_ < 0xFF80) {
		unsigned pc = (unsigned)initPc_;
		p = findAt(pc);
		if (p >= 0)
			return p;
		if (mem_[pc] == 0x18) {
			const int rel = (int)(int8_t)mem_[pc + 1];
			const unsigned jr = (unsigned)((int)pc + 2 + rel);
			if (jr >= 0x80 && jr < 0xFF80) {
				p = findAt(jr);
				if (p >= 0)
					return p;
			}
		}
	}
	return -1;
}

/* hoot oldfalcom.cpp sndadr[] — 再生前に E00E/E010/E012/E014 へ植える。添字: Romancia 0-1、Xanadu 2-7、Xanadu2 8-12、Asteka2 13。 */
enum {
	FALCOM_NONE = 0,
	FALCOM_XANADU = 1,
	FALCOM_XANADU2 = 2,
	FALCOM_ASTEKA2 = 3
};

static const uint16_t kFalcomSndadr[][5] = {
	{0x1056, 0x103a, 0xa000, 0x0000, 0x1043}, /*  0 ROMANCIA オープニング */
	{0x3a39, 0x3bee, 0x3b98, 0x3db5, 0x0000}, /*  1 |        メイン */
	{0x1b67, 0x1c41, 0x1c0a, 0x0000, 0x1c49}, /*  2 XANADU 訓練 */
	{0x4351, 0x443b, 0x43f5, 0x0000, 0x4443}, /*  3 |      メイン */
	{0x168a, 0x1762, 0x172b, 0x0000, 0x176a}, /*  4 |      ボス */
	{0x16b5, 0x178d, 0x1756, 0x0000, 0x1795}, /*  5 |      キングドラゴン */
	{0x03d6, 0x0495, 0x0489, 0x0000, 0x049d}, /*  6 |      エンディング 1 */
	{0x03d6, 0x053d, 0x0489, 0x0000, 0x0545}, /*  7 |      エンディング 2 */
	{0x0160, 0x03dc, 0xa000, 0x0000, 0x0000}, /*  8 XANADU2 オープニング */
	{0x431c, 0x4466, 0x4417, 0x6067, 0x0000}, /*  9 |       メイン */
	{0x1563, 0x16a9, 0x165a, 0x6067, 0x0000}, /* 10 |       ボス */
	{0x159c, 0x16e2, 0x1693, 0x6067, 0x0000}, /* 11 |       キングドラゴン */
	{0x03d5, 0x05d6, 0xa000, 0x0000, 0x0000}, /* 12 |       エンディング */
	{0xaeb7, 0xb103, 0xb05b, 0x7eec, 0x0000}, /* 13 ASTEKA2 APRG（プログラム） */
	{0x029a, 0x0119, 0xa000, 0x0000, 0x0000}, /* 14 XANADU2 IPL（drv 6）。I=01 を残す */
	{0xb02a, 0xb00c, 0xb02a, 0x0000, 0x0000}, /* 15 ASTEKA2 SOUND（drv 2、音源） */
};

/* CEmuPc88Poke16 の実装 */
static void CEmuPc88Poke16(uint8_t* mem, unsigned addr, uint16_t v)
{
	mem[addr] = (uint8_t)(v & 0xff);
	mem[addr + 1] = (uint8_t)((v >> 8) & 0xff);
}

/* CEmuPc88DetectFalcom の実装 */
static int CEmuPc88DetectFalcom(const CEmuGameEntry* ge, const uint8_t* mem)
{
	if (!ge || !mem)
		return FALCOM_NONE;
	if (mem[0xE000] != 0x18 || mem[0xE017] != 0xF3 || mem[0xE027] != 0xDB)
		return FALCOM_NONE;
	int hasAprg = 0, hasSound = 0, hasPrno = 0, hasBgm = 0, hasIpl = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const char* t = ge->rom[i].type;
		const char* n = ge->rom[i].name;
		if (!t)
			continue;
		if (_stricmp(t, "bgm") == 0)
			hasBgm = 1;
		if (_stricmp(t, "prog") != 0 || !n)
			continue;
		if (_stricmp(n, "APRG") == 0)
			hasAprg = 1;
		else if (_stricmp(n, "SOUND") == 0)
			hasSound = 1;
		else if (_stricmp(n, "IPL") == 0)
			hasIpl = 1;
		else if (_strnicmp(n, "PR.NO", 5) == 0)
			hasPrno = 1;
	}
	if (hasAprg || hasSound)
		return FALCOM_ASTEKA2;
	if (hasPrno && hasBgm)
		return FALCOM_XANADU2;
	if (hasPrno || hasIpl)
		return FALCOM_XANADU;
	return FALCOM_NONE;
}

/* CHardPc88::ApplyFalcomPlay の実装 */
void CHardPc88::ApplyFalcomPlay()
{
	if (!falcomType_)
		return;
	/* タイトル 0x12xx / 0x13xx / 0x14xx: 0x02/03/04 と同じドライバ。food は空 */
	const unsigned drvHi = (titleCode_ >> 8) & 0xffu;
	const int foodEmpty = (drvHi & 0xF0) != 0;
	const unsigned drv = drvHi & 0x0Fu;
	int ind = -1;
	unsigned load = 0;

	switch (falcomType_) {
	case FALCOM_XANADU:
		ind = (int)drv + 1; /* drv 1..6 → 行 2..7 */
		if (ind < 2 || ind > 7)
			ind = -1;
		load = 0;
		break;
	case FALCOM_XANADU2:
		if (drv == 6)
			ind = 14; /* IPL 編曲オープニング */
		else
			ind = (int)drv + 7; /* drv 1..5 → 行 8..12 */
		if (drv != 6 && (ind < 8 || ind > 12))
			ind = -1;
		load = 0;
		break;
	case FALCOM_ASTEKA2:
		if (drv == 2) {
			ind = 15; /* SOUND @ B000（音源） */
			load = 0xB000;
		} else {
			ind = 13;
			load = 0x8000;
		}
		break;
	default:
		break;
	}
	/* Prog を先に — volume/food は 0000..5FFF 窓の中 */
	if (drv < 256 && progBank_[drv] && progBankSize_[drv] > 0) {
		unsigned n = progBankSize_[drv];
		if (load + n > 0xE000)
			n = 0xE000 - load;
		if (n > 0)
			memcpy(mem_ + load, progBank_[drv], n);
	}
	if (falcomType_ == FALCOM_XANADU) {
		mem_[0x617a] = 0x09; /* 音量 */
		if (!foodEmpty && (drv == 2 || drv == 3 || drv == 4)) {
			mem_[0x60a7] = 0xff; /* food != 0（餌あり） */
			mem_[0x607d] = 0x00;
			mem_[0x6170] = 0x00;
		}
		if (drv == 6) {
			CEmuPc88Poke16(mem_, 0x06af, 0x0001);
			CEmuPc88Poke16(mem_, 0x06b8, 0x0001);
			CEmuPc88Poke16(mem_, 0x06c1, 0x0001);
			CEmuPc88Poke16(mem_, 0x06b1, 0x09e2);
			CEmuPc88Poke16(mem_, 0x06b3, 0x09e2);
			CEmuPc88Poke16(mem_, 0x06ba, 0x0b2f);
			CEmuPc88Poke16(mem_, 0x06bc, 0x0b2f);
			CEmuPc88Poke16(mem_, 0x06c3, 0x0ca3);
			CEmuPc88Poke16(mem_, 0x06c5, 0x0ca3);
			mem_[0x0226] = 0x0c;
			mem_[0x0227] = 0xaa;
		}
	}
	if (ind >= 0 && ind < (int)(sizeof(kFalcomSndadr) / sizeof(kFalcomSndadr[0]))) {
		CEmuPc88Poke16(mem_, 0xE00E, kFalcomSndadr[ind][0]);
		CEmuPc88Poke16(mem_, 0xE010, kFalcomSndadr[ind][1]);
		CEmuPc88Poke16(mem_, 0xE012, kFalcomSndadr[ind][2]);
		CEmuPc88Poke16(mem_, 0xE014, kFalcomSndadr[ind][3]);
		const uint16_t iffadr = kFalcomSndadr[ind][4];
		if (iffadr)
			mem_[iffadr] = 0xE0;
	}
	if (falcomType_ == FALCOM_XANADU2) {
		const unsigned song = titleCode_ & 0xffu;
		if (song < 256 && bgmBank_[song] && bgmBankSize_[song] >= 16) {
			unsigned n = bgmBankSize_[song];
			if (n > 13u * 1024u)
				n = 13u * 1024u;
			if (0x5C00 + n > 0xE000)
				n = 0xE000 - 0x5C00;
			memcpy(mem_ + 0x5C00, bgmBank_[song], n);
		}
		if (drv != 5 && !foodEmpty)
			mem_[0x60a5] = 0xff;
	}
}

/* PCM／コードバンク */
void CHardPc88::BankCopyBgm(uint8_t songIndex)
{
	if (!bgmBank_[songIndex] || mfileSize_ <= 0) return;
	const int dst = ((int)mem_[0x5d] << 8) | (int)mem_[0x5c];
	/* 宛先はドライバ（mucom88）が武装。dst==0 はポート 0 を cmd/stop ラッチ（KOEI OUT 0,A=0）に使うと PATCH を壊す。 */
	if (dst < 0x100 || dst >= 0x10000) return;
	unsigned n = bgmBankSize_[songIndex];
	if ((unsigned)mfileSize_ < n)
		n = (unsigned)mfileSize_;
	if (dst + (int)n > 0x10000)
		n = (unsigned)(0x10000 - dst);
	memcpy(mem_ + dst, bgmBank_[songIndex], n);
	mem_[0x5e] = 0xff;
}

/* CHardPc88::SetSoundIrqPort の実装 */
void CHardPc88::SetSoundIrqPort(uint8_t data)
{
	ioPorts_[0x32] = data;
	soundIrqMasked = (data & 0x80) != 0;
}

/* PC-8801 テキスト窓: 8000-83FF の 1KB はメイン RAM の可動ビュー。基点は (ポート 70h << 8)。閉じる公式は 80h 書込で素の 8000-83FF を戻す。ポート 78h が基点を進める。
   Z80 コアは mem_ を直接引くので、基点変更時にコピーしてビューを実体化 — ページ切替はそこ経由の読より稀。
   yokosuka のバイト取得は `OUT (70),H / LD H,80 / LD A,(HL)`。最悪フェッチ毎 1 切替。これが無いと読む音は全部ページ F0。 */
void CHardPc88::SetTextWindow(uint8_t hi)
{
	if (hi == textWinHi_)
		return;
	const unsigned oldBase = (unsigned)textWinHi_ << 8;
	const unsigned newBase = (unsigned)hi << 8;
	/* メモリ上端近くの基点は 64K 未満に収まる分だけ見える */
	unsigned oldLen = (oldBase + 0x400 <= 0x10000) ? 0x400 : (0x10000 - oldBase);
	unsigned newLen = (newBase + 0x400 <= 0x10000) ? 0x400 : (0x10000 - newBase);
	if (textWinHi_ != 0x80)
		memmove(mem_ + oldBase, mem_ + 0x8000, oldLen);
	else
		memcpy(textWinShadow_, mem_ + 0x8000, 0x400);
	textWinHi_ = hi;
	if (hi != 0x80)
		memmove(mem_ + 0x8000, mem_ + newBase, newLen);
	else
		memcpy(mem_ + 0x8000, textWinShadow_, 0x400);
}


/* I/O ポート読込 */
uint8_t CHardPc88::PortIn(uint16_t port)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	switch (p) {
	case 0x00: { uint8_t v = cmd; cmd = 0; return v; }
	case 0x01: return param;
	case 0x80:
		/* Scheme bothtec PATCH: IN (80) のあと LD (C000),A。ライブ BGM ヘッダバイトをエコーし MS0A を壊さない（バンク番号を曲にすると mute）。 */
		if (schemeMode_)
			return mem_[0xc000];
		return song;
	case 0x40: {
		const uint64_t hz = cpuHz_ > 0 ? (uint64_t)cpuHz_ : 4000000ull;
		const uint64_t frame = hz / 60;
		const uint64_t vblank = frame / 12;
		return ((cpuCycles_ % frame) < vblank) ? 0x20 : 0x00;
	}
	/* hoot: 0x32 と別名 0xAA は ioport[0x32] を共有（既定 0） */
	case 0x32: case 0xAA: return ioPorts_[0x32];
	/* ドライバは ISR 周りで窓基点を保存／復元 */
	case 0x70: return textWinHi_;
	/* A007 は 0x44-47 と代替 0xA8-AD の両方を探る（firehawk/hoot） */
	case 0x44: case 0xA8: return chip_ ? chip_->ReadStatus() : 0xff;
	case 0x45: case 0xA9: return chip_ ? chip_->ReadData() : 0xff;
	case 0x46: case 0xAC: return chip_ ? chip_->ReadStatusHi() : 0xff;
	case 0x47: case 0xAD: return chip_ ? chip_->ReadDataHi() : 0xff;
	default: return ioPorts_[p];
	}
}

/* I/O ポート書込 */
void CHardPc88::PortOut(uint16_t port, uint8_t data)
{
	const uint8_t p = (uint8_t)(port & 0xff);
	switch (p) {
	case 0x00:
		cmd = data;
		/* mucom88: OUT (0),song がバンクを [0x5d:0x5c] へコピー。KOEI はポート 0 を cmd/stop のみ — そこでバンクコピーしない。非 mucom の mfile（shnghai2）も OUT 0 をラッチにし 5C/5D はライブ。
		   標準 hoot PATCH はここへ 0 を書いてコマンドラッチをクリアするだけ（triton2 0037、sorc88 0038/0042）。バンクスローブではない: そう扱うと全曲の上に MUS00 を再ステージ。 */
		if (mucomBankCopy_ && mfileSize_ > 0)
			BankCopyBgm(data);
		/* hoot scheme.cpp: OUT (0),bank → C000 へ memcpy + LOAD_FLAG@9012 */
		/* hoot scheme.cpp: OUT (0),bank → memcpy C000 + LOAD_FLAG@9012。
		   bothtec PATCH は 9011 に IN (00) poll — そこのゲスト OUT (0) はバンクロードストローブではない（C000 のライブ MML を壊す）。 */
		else if (schemeMode_ && data < 128 && bgmBank_[data] && bgmBankSize_[data]
			&& !(mem_[0x9011] == 0xDB && mem_[0x9012] == 0x00)) {
			unsigned n = bgmBankSize_[data];
			if (n > 8 * 1024u) n = 8 * 1024u;
			if (0xc000 + n > 0x10000)
				n = 0x10000 - 0xc000;
			memcpy(mem_ + 0xc000, bgmBank_[data], n);
			mem_[0x9012] = 0xff;
		}
		break;
	case 0x01: param = data; break;
	case 0x02:
		/* hoot oldfalcom: OUT (02) は Xanadu2 BGM 窓のみ。Prog と E00E..E014 は ApplyFalcomPlay が植えた — ここで書き直すと、PATCH が E00E（まだ 0）を IM2 音源ベクタへコピーしたあと開始番地を壊す。 */
		if (falcomType_ == FALCOM_XANADU2) {
			if (data < 256 && bgmBank_[data] && bgmBankSize_[data] >= 16) {
				unsigned n = bgmBankSize_[data];
				if (n > 13u * 1024u)
					n = 13u * 1024u;
				if (0x5C00 + n > 0xE000)
					n = 0xE000 - 0x5C00;
				memcpy(mem_ + 0x5C00, bgmBank_[data], n);
			}
			const unsigned drv = (titleCode_ >> 8) & 0x0Fu;
			const int foodEmpty = ((titleCode_ >> 8) & 0xF0) != 0;
			if (drv != 5 && !foodEmpty)
				mem_[0x60a5] = 0xff;
			break;
		}
		if (falcomType_)
			break;
		/* 他の type=prog PATCH: OUT (02),song がバンクをマップ（タイトル bit 8..15）。ロード番地はバンク自身の `LD A,n; LD I,A`（asteka2 APRG→8000、SOUND→B000）。素の xana PR.NO* は 0000 へ戻す。
		   +2/+4 のヘッダワードは IM2/play-ISR 番地（SOUND B02A）でありホスト JP 先ではない — PATCH は E06C を push して JP (E010)。E010 は JR/JP init stub（LD I / CALL … / EI RET）。 */
		{
			const unsigned prog = (titleCode_ >> 8) & 0xff;
			if (prog < 256 && progBank_[prog] && progBankSize_[prog] > 0) {
				const unsigned char* img = progBank_[prog];
				unsigned n = progBankSize_[prog];
				unsigned load = 0;
				unsigned entry = 0;
				int entryHasLdI = 0;
				const int jrAt0 = (img[0] == 0x18);
				const int jpAt0 = (img[0] == 0xC3 && n >= 3);
				if (jrAt0) {
					entry = (unsigned)(2 + (int)(int8_t)img[1]);
				} else if (jpAt0) {
					entry = (unsigned)img[1] | ((unsigned)img[2] << 8);
					if (entry >= n) entry = 0;
				} else if (n > 0x120 && img[0x100] == 0x18) {
					/* xana PR.NO*: ファイルヘッダのあと Falcom JR stub @0100。イメージは load=0000 リンク。後の LD I,A は IM2 ページだけ（リロケ基点ではない）。 */
					entry = 0x100;
				}
				if (jrAt0 || jpAt0) {
					for (unsigned k = entry; k + 4 <= n && k < entry + 64; k++) {
						if (img[k] == 0x3E && img[k + 2] == 0xED
							&& img[k + 3] == 0x47) {
							load = (unsigned)img[k + 1] << 8;
							entryHasLdI = 1;
							break;
						}
					}
				}
				/* xana stub@100: JR は C9 へ着地。本物 init はベクタ RET バイトのあとの LD SP（E000 PATCH / C9 / F3… と同じ配置）。 */
				if (!entryHasLdI && entry == 0x100) {
					for (unsigned k = entry; k + 1 < n && k < entry + 32; k++) {
						if (img[k] == 0x31) { /* LD SP,nn（スタック初期化） */
							entry = k;
							break;
						}
					}
				}
				if (load >= 0xE000) load = 0;
				if (load + n > 0xE000)
					n = 0xE000 - load; /* PATCH は E000 に残す */
				if (load + n > 0x10000)
					n = 0x10000 - load;
				if (n > 0)
					memcpy(mem_ + load, img, n);
				unsigned play = load + entry;
				unsigned stop = play;
				/* JR init stub があるとき +2/+4 は IM2/ISR 番地（SOUND B02A）。stub が見つからないときだけ使う。 */
				if (n >= 6 && entry == 0 && !entryHasLdI) {
					const unsigned w2 = (unsigned)img[2] | ((unsigned)img[3] << 8);
					const unsigned w4 = (unsigned)img[4] | ((unsigned)img[5] << 8);
					if (w2 != 0)
						play = w2;
					if (w4 != 0)
						stop = w4;
					else
						stop = play;
				}
				mem_[0xE010] = (uint8_t)(play & 0xff);
				mem_[0xE011] = (uint8_t)((play >> 8) & 0xff);
				mem_[0xE012] = (uint8_t)(stop & 0xff);
				mem_[0xE013] = (uint8_t)((stop >> 8) & 0xff);
				/* xana2 PR.NO* stub@0100: LD I,A が IM2 ページ。ゲスト JP (E010) がその命令前に戻る（または RETI が I=E0 を戻す）と、音源ベクタ E008 は OUT 前 LDI 消去の 0 のまま曲死。stub ヘッダから I + E0 音源ベクタを植える。 */
				if (entry == 0x100 || (play >= 0x100 && play < 0x140)) {
					for (unsigned k = 0x100; k + 4 <= n && k < 0x140; k++) {
						if (img[k] == 0x3E && img[k + 2] == 0xED
							&& img[k + 3] == 0x47) {
							if (cpu_)
								cpu_->r.i = img[k + 1];
							break;
						}
					}
					if (n >= 0x10A) {
						mem_[0xE008] = img[0x108];
						mem_[0xE009] = img[0x109];
						mem_[0xE00E] = img[0x108];
						mem_[0xE00F] = img[0x109];
					}
				}
				/* xana2: prog イメージは 0000..5FFF。マップ後に mdata（0x5C00）へ bgm バンクを重ねる。 */
				if (mdataAddr_ >= 0 && data < 256 && bgmBank_[data]
					&& bgmBankSize_[data] >= 16) {
					unsigned bn = bgmBankSize_[data];
					if (mdataSize_ > 0 && (unsigned)mdataSize_ < bn)
						bn = (unsigned)mdataSize_;
					if (mdataAddr_ + (int)bn > 0xE000)
						bn = (unsigned)(0xE000 - mdataAddr_);
					if (bn > 0)
						memcpy(mem_ + mdataAddr_, bgmBank_[data], bn);
				}
			}
		}
		break;
	case 0x32: case 0xAA: SetSoundIrqPort(data); break;
	case 0x44: case 0xA8: if (chip_) chip_->Write(0, data); break;
	case 0x46: case 0xAC: if (chip_) chip_->Write(0x100, data); break;
	case 0x45: case 0xA9: if (chip_) chip_->Write(1, data); break;
	case 0x47: case 0xAD: if (chip_) chip_->Write(0x101, data); break;
	case 0x70: SetTextWindow(data); break;
	case 0x78: SetTextWindow((uint8_t)(textWinHi_ + 1)); break;
	/* 5Ch-5Fh（C000-FFFF 上の GVRAM プレーン選択）は意図的に未実装: 237 リップがストローブし 89 がそこにコードを置く。従うとドライバが自分の下から入れ替わる。hoot はポートを無視。これらリップは hoot 向けパッチ。実マップをエミュしても測定差なし（arka88、84 ストローブ、バイト一致）。 */
	case 0xE4: if (chip_) chip_->AckIrq(); break;
	case 0xE6: break; /* PC-88 IRQ レベル — hoot は無視 */
	default: ioPorts_[p] = data; break;
	}
}

/* zip から ROM／曲データを載せる */
int CHardPc88::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	const unsigned songNum = titleCode & 0xff;
	const int isMucom = (_stricmp(ge->subtype, "muco") == 0 || _stricmp(ge->subtype, "mucom88") == 0);
	titleCode_ = titleCode;
	hardrankSb2_ = 0;
	memset(mem_, 0, sizeof(mem_));
	textWinHi_ = 0x80;
	memset(textWinShadow_, 0, sizeof(textWinShadow_));
	StageBanks(fs, ge);
	const int preferMdatN = opnaMode || CEmuParseOptHex(ge, "use_pcmx8", 0);
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0) continue;
		const int off = r->offset;
		if (off < 0 || off >= 0x10000) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuPc88ZipFind(fs, r->name, &sz, -1, preferMdatN);
		if (!data || !sz || off + (int)sz > 0x10000) continue;
		memcpy(mem_ + off, data, sz);
		/* Scheme OPNA: PATCH@0 を 0x9000 へミラーするだけ（page 0 は消さない — 低メモリを覗く経路あり。スタック破壊は init@9000 で避ける）。 */
		if (schemeMode_ && _stricmp(r->name, "PATCH") == 0 && off == 0) {
			unsigned n = sz;
			if (0x9000 + n > 0x10000)
				n = 0x10000 - 0x9000;
			if (n > 0)
				memcpy(mem_ + 0x9000, data, n);
			if (initPc_ == 0)
				initPc_ = 0x9000;
		}
		/* Falcom 特殊: PATCH のみ E000 — そこから開始（0000 からの NOP 滑走に頼らない。4000/5C00 の曲プリロードが割り込む）。 */
		if (initPc_ == 0 && _stricmp(r->name, "PATCH") == 0 && off == 0xE000)
			initPc_ = 0xE000;
	}
	falcomType_ = CEmuPc88DetectFalcom(ge, mem_);
	CEmuPc88MirrorDriverPage20(mem_);
	/* robowr88: PROG2 はカタログ bgm@1。StageBanks が外したら再ステージ用コピーを残す — ブート中に PROG1 を重ねない（B545 ISR）。 */
	if (CEmuPc88PatchRobowr(mem_) && (titleCode & 0xffu) == 1
		&& (!bgmBank_[1] || bgmBankSize_[1] < 16)) {
		unsigned sz = 0;
		const unsigned char* data = CEmuPc88ZipFind(fs, "PROG2", &sz, 1, 0);
		if (data && sz >= 16) {
			if (bgmBank_[1])
				free(bgmBank_[1]);
			bgmBank_[1] = (unsigned char*)malloc(sz);
			if (bgmBank_[1]) {
				memcpy(bgmBank_[1], data, sz);
				bgmBankSize_[1] = sz;
			}
		}
	}
	/* 不完全リップ（例 p1demo1 は曲 RAM 前に DRIVER EOF）はここで直さない — トランポリン発明は欠けたペイロードを隠す。 */
	/* yakyufan: play@02A0 は XOR A; LD (0115),A のあとフラグを立てない。キーオン @110A とテンポ @0874 は (0115)==0 で RET — タイマ ISR の fnum 書込は残るが peak/key=0。11 バイトのクリア+INC を LD A,1; LD (0115/16/19),A に置換（続く LD (0129),A のため A は 1）。
	   また PATCH は SP=0100、DRIVER init は page0 に I=0 IM2 — ブート中 VRTC がベクタ表へ push し RET がゴミ（PC≈8Bxx）。IRQ 前に SP を FF00 へ。
	   Mute@0C5D が (0118) をクリア。VRTC tick @0BE4 は RET Z — ArmYakyufanPlay が play CALL 後に両フラグを再アサート。 */
	if (initPc_ == 0xc000 && mem_[0xc000] == 0xf3 && mem_[0xc001] == 0x31
		&& mem_[0xc004] == 0xcd && mem_[0xc005] == 0x00 && mem_[0xc006] == 0x01
		&& mem_[0x100] == 0xc3) {
		if (mem_[0xc002] == 0x00 && mem_[0xc003] == 0x01)
			mem_[0xc003] = 0xff; /* LD SP,0xFF00（IRQ 前にスタックを上げる） */
		if (mem_[0x2b3] == 0xaf && mem_[0x2b4] == 0x32
			&& mem_[0x2b5] == 0x15 && mem_[0x2b6] == 0x01 && mem_[0x2b7] == 0x32
			&& mem_[0x2b8] == 0x16 && mem_[0x2b9] == 0x01 && mem_[0x2ba] == 0x32
			&& mem_[0x2bb] == 0x19 && mem_[0x2bc] == 0x01 && mem_[0x2bd] == 0x3c) {
			static const uint8_t kArm115[11] = {
				0x3e, 0x01, 0x32, 0x15, 0x01, 0x32, 0x16, 0x01, 0x32, 0x19, 0x01
			};
			memcpy(mem_ + 0x2b3, kArm115, sizeof(kArm115));
		}
	}
	/* 重なり／プリロード判定の前にタイトル符号化 mdata を適用 */
	{
		const int titleMdata = CEmuPc88TitleEncodedMdata(titleCode, mdataAddrDefaulted_, wolfteamMode_);
		if (titleMdata >= 0)
			mdataAddr_ = titleMdata;
	}
	if (CEmuPc88PatchXzr2VoiceF000(mem_)) {
		vdataAddr_ = 0xF000;
		if (vfileSize_ <= 0)
			vfileSize_ = 0x200;
	}
	const int overlapsCode = CEmuPc88SongOverlapsCode(fs, ge, mdataAddr_, mdataSize_);
	/* 重なる mdata（sorc88 SEDAT、kbreed TRPSCR 等）: ブート中はコードを無傷。play トリガで LoadSongData を再実行。 */
	const int preloadSong = !(overlapsCode && ShouldRestageSong());
	/* navitune 系: タイトル bit 8..23 は code@mdata でもある bgm 内バイトオフセット。重なり延期は restage が no-op だと offset-0 コードが永久に残る — ここで必ず fileOff を適用。 */
	const unsigned titleFileOff = (titleCode >> 8) & 0xffffu;
	if (titleFileOff != 0 && mdataAddr_ >= 0 && bgmBank_[songNum]
		&& titleFileOff < bgmBankSize_[songNum])
		LoadSongData(titleCode);
	else if (preloadSong && (wolfteamMode_ || mdataAddr_ >= 0 || vdataAddr_ >= 0))
		LoadSongData(titleCode);
	else if (preloadSong && !isMucom) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "bgm") != 0 || r->offset != (int)songNum) continue;
			if (mdataAddr_ < 0) continue;
			unsigned sz = 0;
			const unsigned char* data = CEmuPc88ZipFind(fs, r->name, &sz,
				r->offset, preferMdatN);
			if (!data || !sz || mdataAddr_ < 0) continue;
			unsigned n = sz;
			if (mdataAddr_ + (int)n > 0x10000)
				n = (unsigned)(0x10000 - mdataAddr_);
			if (n > 0)
				memcpy(mem_ + mdataAddr_, data, n);
		}
	}
	/* harakiri MPLAY: ブート CALL C009 / play CALL C006 は FMDRV ベクタ。MPLAY のロードは C0D7（HL=曲）。C006 は停止（C00A=3F）。残った JP 表バイトに ld hl,mdata / jp C0D7。 */
	if (CEmuPc88PatchHarakiriMplay(mem_)) {
		const int md = (mdataAddr_ >= 0) ? mdataAddr_ : 0x4000;
		mem_[0xC003] = 0x21;
		mem_[0xC004] = (uint8_t)(md & 0xff);
		mem_[0xC005] = (uint8_t)((md >> 8) & 0xff);
		mem_[0xC006] = 0xC3;
		mem_[0xC007] = 0xD7;
		mem_[0xC008] = 0xC0;
		if (mem_[0x11] == 0xCD && mem_[0x12] == 0x09 && mem_[0x13] == 0xC0)
			mem_[0x12] = 0x03;
		if (mem_[0x25] == 0xCD && mem_[0x26] == 0x06 && mem_[0x27] == 0xC0)
			mem_[0x26] = 0x03;
		if (mem_[0xC12D] == 0xFB)
			mem_[0xC12D] = 0x00;
	} else if (mem_[0x0B] == 0xCD && mem_[0x0C] == 0x09 && mem_[0x0D] == 0xC0
		&& mem_[0xC000] == 0xC3 && mem_[0xC009] != 0xC3)
		mem_[0x0C] = 0x00;
	/* ブートで PROG2 をステージしない — PROG1 の B545 ISR を消す。play 時の再ステージが LoadSongData でコピー。 */
	if (CEmuPc88PatchHardrankSb2(mem_)) {
		hardrankSb2_ = 1;
		if (mem_[0x79D7] < 0x38)
			mem_[0x79D7] = 0x40;
		CEmuPc88SkipHardrankLeadRest(mem_);
		/* PATCH cmd=1 はポート 0 をクリアしないのでドレインが 8A00 を再 CALL し、9130 をヘッダ ptr に戻す（Stage 1 が和音で固まる）。 */
		if (mem_[0x18] == 0xCD && mem_[0x19] == 0x00 && mem_[0x1A] == 0x8A
			&& mem_[0x1B] == 0x18 && mem_[0x1C] == 0xE9) {
			mem_[0x1B] = 0xAF;
			mem_[0x1C] = 0xD3;
			mem_[0x1D] = 0x00;
			mem_[0x1E] = 0x18;
			mem_[0x1F] = 0xE6;
		}
	}
	if (CEmuPc88PatchXanadu80sr(mem_, initPc_)) {
		mem_[0x606A] = 0;
		if ((titleCode & 0xffu) == 2 && mem_[0x174D] == 0xF3) {
			if (mem_[0x175C] == 0x3E && mem_[0x175D] == 0x00
				&& mem_[0x175E] == 0xD3 && mem_[0x175F] == 0xE6)
				mem_[0x175D] = 0x03;
			if (mem_[0x167F] == 0xA7 && mem_[0x1680] == 0x20 && mem_[0x1681] == 0x1B)
				mem_[0x167F] = mem_[0x1680] = mem_[0x1681] = 0x00;
			if (mem_[0x1685] == 0xA7 && mem_[0x1686] == 0x28 && mem_[0x1687] == 0x15)
				mem_[0x1685] = mem_[0x1686] = mem_[0x1687] = 0x00;
		}
	}
	/* archon: OUT (C=0),A が曲 id をコマンドラッチへ書くので、タイトル 02 は 1 再生のあと停止経路（CALL A000 A=4）。 */
	if (mem_[0] == 0xF3 && mem_[0x13] == 0x3E && mem_[0x14] == 0x04
		&& mem_[0x15] == 0xCD && mem_[0x16] == 0x00 && mem_[0x17] == 0xA0
		&& mem_[0x18] == 0xDB && mem_[0x19] == 0x01
		&& mem_[0x1A] == 0x0E && mem_[0x1B] == 0x00
		&& mem_[0x1C] == 0xED && mem_[0x1D] == 0x49)
		mem_[0x1C] = mem_[0x1D] = 0x00;
	if (armNavituneTimer_)
		PrepareNavitunePatch();
	if (isMucom) {
		mem_[0xEEA7] = 0xAF;
		mem_[0xEEA8] = 0xC3;
		mem_[0xEEA9] = 0x00;
		mem_[0xEEAA] = 0xB0;
	}
	/* spitfl88: ブート CALL A826 は (79D7) を読み <0x34 なら A824（プレーヤ許可）をクリア。そのセルは PROG 外（中 RAM）。植えないと play が A6A9=0x20 を武装したあとも ISR が AD0C を永久に飛ばす。 */
	if (forcePlayEi_ && mem_[0xA826] == 0xF3 && mem_[0xA830] == 0xFE
		&& mem_[0xA831] == 0x34)
		mem_[0x79D7] = 0x40;
	/* byouin_88 MUSIC.SYS@9C00: 入口（JP 9EFA）は LD A,(79D7) / CP 34 / RET C — 植えないと全 CALL 9C00 が no-op、IM2 ページ I=F3 は空、RTC/OPN が走らない。 */
	if (mem_[0x9C00] == 0xC3) {
		const unsigned ent = (unsigned)mem_[0x9C01]
			| ((unsigned)mem_[0x9C02] << 8);
		if (ent + 5 < 0x10000
			&& mem_[ent] == 0xF5 && mem_[ent + 1] == 0x3A
			&& mem_[ent + 2] == 0xD7 && mem_[ent + 3] == 0x79
			&& mem_[ent + 4] == 0xFE && mem_[ent + 5] == 0x34
			&& mem_[0x79D7] < 0x34)
			mem_[0x79D7] = 0x40;
	}
	/* lizard88: MAIN@9F00 はブートで XOR 復号。曲ロード A3B0 は (79D7)>=0x34（spitfl/byouin と同じ中 RAM セル）。植えないと A3B0 が A4E7 へ逃げ、A572 が固着、9F12 が OPN 書込を飛ばす。 */
	if (mem_[0x79D7] < 0x34 && mem_[0] == 0x18 && mem_[0x10] == 0xF3) {
		int main9f = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "code") != 0 || !r->name) continue;
			if (r->offset == 0x9f00 && _strnicmp(r->name, "MAIN", 4) == 0)
				main9f = 1;
		}
		if (main9f)
			mem_[0x79D7] = 0x40;
		/* A572 曲ゲートはドライバの XOR 復号後に植える — LoadRoms は PATCH が MAIN@9F00 を復号する前。 */
	}
	/* pwmajan2: PATCH CALL 9019 が I/IRQ を初期化し CALL 0184/0178、EI+ディスク上の C9 で RET する予定だった。ブートは poll に届かなかった。9019 を飛ばし、ArmPwmajan2 が settle 後に IM2 ベクタ＋マスク解除。 */
	if (CEmuPc88PatchPwmajan2(mem_)
		&& mem_[0xF012] == 0xCD && mem_[0xF013] == 0x19 && mem_[0xF014] == 0x90) {
		mem_[0xF012] = 0x00;
		mem_[0xF013] = 0x00;
		mem_[0xF014] = 0x00;
	}
	CEmuPc88PlantIceclimbTitleLoop(mem_);
	if (CEmuPc88PatchYaksa2(mem_))
		mem_[0x37D1] = 1;
	/* mule 8B01 は I がまだ 0 のまま EI し、そのあと LD I,5F。その窓のカタログ VRTC は 0004 経由でスタック頁へ。OPN Timer B は 8B01 自身が武装。 */
	if (CEmuPc88PatchMulePages(mem_))
		useVrtc = 0;
	/* lvaccus: PATCH は I=0 の DI/IM2。settle 中のカタログ VRTC はベクタ 0002（ED 5E）を読み空 RAM から PROG へ。play が I=1 / (0102)=$9803 を植え EI — そのあと VRTC を再許可。 */
	if (CEmuPc88PatchLvaccus9800(mem_))
		useVrtc = 0;
	/* xzr の I=A4 ベクタはブートで植えるが、settle 中のカタログ VRTC は A416 が EI する前に A86F（SP を BD4B へ）へ当たる。 */
	if (CEmuPc88PatchXzrA4(mem_))
		useVrtc = 0;
	if (falcomType_)
		ApplyFalcomPlay();
	/* f_crisis MMLEX@9A00 + 素の DI PATCH: EI だけでは音が開かない（play が MMLEX へ迷う）。MUSIC.OBJ プロトコルが分かるまで forcePlayEi_ はクリア。castle は Init から forcePlayEi_（OPN IM2）。spitfl88/tf88sr の PROG のみは Init 時 forcePlayEi_ を残す。 */
	{
		const int keepForceEi = forcePlayEi_;
		forcePlayEi_ = 0;
		if (keepForceEi)
			forcePlayEi_ = 1;
	}
	int initPc = initPc_;
	cpu_->reset(mem_);
	cpu_->r.pc = (uint16_t)initPc;
	/* LD SP 無しの IM2（herzog/gra88）: SP=0 だと最初の IRQ が page 0 を潰す。Hoot PATCH はほぼ SP=0x0100。LD SP 無しの OK 兄弟もその既定で動く。 */
	{
		int pc = initPc_;
		if (pc < 0) pc = 0;
		if (mem_[pc] == 0x18) {
			const int rel = (int)(int8_t)mem_[pc + 1];
			pc = pc + 2 + rel;
		}
		int hasIm2 = 0, hasLdSp = 0;
		for (int k = 0; k < 32 && pc + k + 1 < 0x10000; k++) {
			if (mem_[pc + k] == 0x31) hasLdSp = 1;
			if (mem_[pc + k] == 0xED && mem_[pc + k + 1] == 0x5E) hasIm2 = 1;
		}
		if (hasIm2 && !hasLdSp)
			cpu_->r.sp = 0x0100;
	}
	/* パック KOEI: ポート 80/01 は FMDRV 再生添字（0 = table[0]）でありバンクではない */
	song = PlaySongIndex();
	param = PlayParamIndex();
	cpuCycles_ = 0;
	memset(ioPorts_, 0, sizeof(ioPorts_));
	/* Hoot は ioport[0..0x0E]=0xFF で初期化。0 は未使用低ポートを IN する mugen3/yakyudou 系を壊す。wing2 は既知の反例（0xFF で無音 — 複数タイトルの利得として受容）。 */
	for (int p = 0x02; p <= 0x0E; p++)
		ioPorts_[p] = 0xff;
	soundIrqMasked = 0;
	/* OPNA: 標準 ADPCM-A リズム ROM。ゲーム type=adpcm は ADPCM-B（上記）。Scheme は ADPCM-A 打楽器にリズム ROM が要る。 */
	if (opnaMode && chip_)
		CEmuLoadExternalYm2608Adpcm(chip_);
	/* Scheme MUS2 は OPN ポートメールボックスを F0BB..（32/44/46）に保つ */
	if (schemeMode_) {
		mem_[0xf0bb] = 0x32;
		mem_[0xf0bc] = 0x44;
		mem_[0xf0bd] = 0x46;
	}
	return 1;
}

/* CEmuHardPc88SetActive の実装 */
void CEmuHardPc88SetActive(CHardPc88* hw)
{
	g_pc88Active = hw;
	CEmuZ80BusSetActive(hw);
}
