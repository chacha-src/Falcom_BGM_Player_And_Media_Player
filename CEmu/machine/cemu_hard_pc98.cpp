#include "StdAfx.h"
#include "cemu_hard_pc98.h"
#include "../cemu_rhythm.h"
#include "../chip/cemu_chip_opna.h"
#include "../chip/cemu_chip_opl.h"
#include "../fmmon/fmmon_shadow.h"
#include "../vendor/np2/np2ffi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Hoot KOEI / addressing=1 のコード ROM パックは上位16=seg、下位16=off（例 0x01000100 → 0100:0100）。2MB に収まる値は平坦物理のまま。 */
static unsigned Pc98RomPhys(int offset)
{
	const unsigned raw = (unsigned)offset;
	if (raw >= 0x200000u) {
		const unsigned seg = (raw >> 16) & 0xffffu;
		const unsigned ofs = raw & 0xffffu;
		return (seg << 4) + ofs;
	}
	return raw;
}

static int DosShellStarts(const CEmuGameEntry* ge, const char* const* prefixes);

/* 直近 OPN DATA0 書込。YM2203 FM レジスタは書込専用。PC-98 基板はバスホールドするので OPNDRV が 27h/40h カナリアを IN 比較できる（c2gp / dynamo98）。 */
static uint8_t g_opnDataLatch = 0;
static int g_opnBusHold = 0;
/* MMD2.SYS INT14 ISR: OCW3 0Bh / IN 00h / TEST 80h、続けてスレーブ EOI。ソフト PIC は 0 を返し OUT 08h,20h を飛ばし IRQ12 が固まった。 */
static int g_mmdPicIsr = 0;
/* VALKY/SSCP シーケンサは INT 08。ゲスト OUT 02h = F7 が IRQ0 を再マスク。 */
static int s_valkyKeepIrq0 = 0;
static void ValkySscpKeepAlive(uint8_t* mem);
static int ValkyIsSscp(const uint8_t* mem);
/* IRQ 配送 */
int CEmuPc98ValkyKeepIrq0()
{
	return s_valkyKeepIrq0;
}
/* FairyDust FMX 3.10（lemmona）も同じ: PIT 校正後のワンショット INT08 @3660 が IRQ0 をマスクし、本物シーケンサ CS:1D60 が tick しない。 */
static int s_fmxKeepIrq0 = 0;
/* IWADRV F.COM カナリア: OUT 18Ah / IN が ymfm ReadData() だと 1 が戻らず INT14 を飛ばす。 */
static int s_iwaBusHold = 0;
/* FMX 3.10 はワンショット INT08 を植えたあと CS:[3B84] でスピン。IRQ0 は CALL FAR [3B6C]（まだ 0000:0000）。待ちオペコードが CS:IP の最初に非 0 カウントを poke。 */
static int s_fmxCalibAssist = 0;
/* np2_interrupt(OPN) 前の SS:SP。ソフト PIC は YM 線 ack ですぐ opnInService_ を落とし、OPNDRV の STI-before-EOI が私有 CS:24E4 スタックで INT0B 再入（tlove12_98: 40 IRQ のち 9A00 で IF=0）。そのフレーム IRET まで in-service を保持 — 本物 8259 は EOI まで ISR を残す。 */
static uint16_t g_opnIsrSs = 0;
static uint16_t g_opnIsrSp = 0;
static uint16_t g_pitIsrSs = 0;
static uint16_t g_pitIsrSp = 0;
static int g_pitInService = 0;
static int g_mpuInService = 0;
static uint16_t g_mpuIsrSs = 0;
static uint16_t g_mpuIsrSp = 0;
/* PC-98 8259 はエッジトリガ。DSR が Low のままレベル再撃すると、IN E0D0 せず status をラッチする ISR（旧 MMD /I auto @CS:16D0）が罠。 */
static int g_mpuIrqAsserted = 0;
/* 直近 MPU I/O（FMD intelligent）。リング 128 */
struct MpuTrRec {
	uint16_t cs, ip, port;
	uint8_t wr, val;
};
static MpuTrRec g_mpuTr[128];
static unsigned g_mpuTrI, g_mpuTrN;
static uint8_t g_mpuLastSt = 0xff;
static char g_fmdLoadedSong[96];

/* MpuTrace の実装 */
static void MpuTrace(uint16_t port, int wr, uint8_t val)
{
	MpuTrRec* r = &g_mpuTr[g_mpuTrI];
	g_mpuTrI = (g_mpuTrI + 1u) & 127u;
	if (g_mpuTrN < 128u)
		g_mpuTrN++;
	r->cs = np2_reg_get(NP2_R_CS);
	r->ip = np2_reg_get(NP2_R_IP);
	r->port = port;
	r->wr = wr ? 1 : 0;
	r->val = val;
}

/* CEmuPc98DumpMpuTrace の実装 */
extern "C" void CEmuPc98DumpMpuTrace(FILE* f)
{
	if (!f) return;
	fprintf(f, "  mpuTr n=%u\n", g_mpuTrN);
	const unsigned n = g_mpuTrN;
	const unsigned start = (n < 128u) ? 0u : g_mpuTrI;
	for (unsigned k = 0; k < n; k++) {
		const MpuTrRec* r = &g_mpuTr[(start + k) & 127u];
		fprintf(f, "    %04X:%04X %s %04X =%02X\n",
			r->cs, r->ip, r->wr ? "OUT" : "IN ", r->port, r->val);
	}
}
/* MMD.SYS（ヘッダ "MMD200  "。MMD2 "MMD200OR" ではない）: 07FF が [si+3] を duration へ。Parse は gate 0 のままなので最初のノートが duration 0 を格納し 0732 がチャネルを永久 RET（sbr_98 は曲常駐で SILENT）。 */
static int g_mmdClassic = 0;
static unsigned g_mmdLoadSeg = 0;
static int g_mmdPlayAssist = 0;
static const uint8_t* g_mmd2FnSrc = NULL;
/* MMD2 0x654 は A0/A4 を書き 28h|F0 を書かないので mute オペで F-num が滑る。チャネル最初の A0 は本物キーオフまで 28h|F0 をラッチ。 */
static uint8_t g_mmdKeyOn = 0;
static unsigned g_mmdTrkBase[6];
static int g_mmdFmPlanted = 0;
static unsigned g_sddLoadSeg = 0;
static const unsigned char* g_sddSongData = NULL;
static unsigned g_sddSongSize = 0;
static unsigned g_muse2Seg = 0;
static uint16_t g_muse2Intr = 0;
static const unsigned char* g_muse2SongData = NULL;
static unsigned g_muse2SongSize = 0;
static unsigned g_nmuseSeg = 0;
static void Pc98Wr16(uint8_t* mem, unsigned addr, uint16_t v);
static int MmdClassicIsSbr(const uint8_t* mem, unsigned lin);
static int MmdClassicIsFray(const uint8_t* mem, unsigned lin);
static int Mmd2Layout(const uint8_t* mem, unsigned lin);

/* 古典 MMD.SYS API は CS:009C。INIT 1CE4 の AH=25 は YM 検出が 1548.0 を落としたときに飛ばし、mmd2.com の `xor ax,ax; int D2` がトランポリンのまま [1CA]=CX=0、AH=3F が 0 バイト読む（sbr SILENT、タイマ再組だけ）。 */
static void MmdClassicPlantIvt(uint8_t* mem)
{
	if (!mem || !g_mmdClassic || !g_mmdLoadSeg) return;
	const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
	if (lin + 0x1568u >= 0x200000u) return;
	if (mem[lin + 0x9C] != 0x06) return; /* PUSH ES */
	/* fray: 155A/155E は AH ディスパッチ（AH1=0122, AH3=0143）。sbr の曲サイズ/オフセットとして書くと AH=3 が曲データへ飛ぶ。 */
	if (MmdClassicIsFray(mem, lin)) {
		mem[lin + 0x152C] |= 1;
		mem[lin + 0x152E] = 0x88;
		mem[lin + 0x152F] = 0x01;
		mem[lin + 0x1530] = 0x8A;
		mem[lin + 0x1531] = 0x01;
		unsigned songOff = (unsigned)mem[lin + 0x1542]
			| ((unsigned)mem[lin + 0x1543] << 8);
		if (songOff < 0x19E8u) {
			songOff = 0x1DE8u;
			Pc98Wr16(mem, lin + 0x1542u, 0x1DE8);
		}
		unsigned songSz = (unsigned)mem[lin + 0x153C]
			| ((unsigned)mem[lin + 0x153D] << 8);
		if (songSz < 64u || songSz > 0x8000u) {
			songSz = 0x1000u;
			Pc98Wr16(mem, lin + 0x153Cu, 0x1000);
		}
		const unsigned sp = (unsigned)mem[lin + 0x36E]
			| ((unsigned)mem[lin + 0x36F] << 8);
		const unsigned ss = (unsigned)mem[lin + 0x370]
			| ((unsigned)mem[lin + 0x371] << 8);
		if (!sp || !ss) {
			const unsigned top = songOff + songSz + 0x200u;
			Pc98Wr16(mem, lin + 0x36Eu, (uint16_t)(top - 2u));
			Pc98Wr16(mem, lin + 0x370u, (uint16_t)g_mmdLoadSeg);
		}
		Pc98Wr16(mem, 0xD2u * 4u, 0x009C);
		Pc98Wr16(mem, 0xD2u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
		Pc98Wr16(mem, 0x14u * 4u, 0x0376);
		Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
		Pc98Wr16(mem, 0x0Bu * 4u, 0x0376);
		Pc98Wr16(mem, 0x0Bu * 4u + 2u, (uint16_t)g_mmdLoadSeg);
		return;
	}
	if (!MmdClassicIsSbr(mem, lin)) return;
	mem[lin + 0x1548] |= 1;
	mem[lin + 0x154A] = 0x88;
	mem[lin + 0x154B] = 0x01;
	mem[lin + 0x154C] = 0x8A;
	mem[lin + 0x154D] = 0x01;
	if (mem[lin + 0x1549] == 0)
		mem[lin + 0x1549] = 0x0B;
	mem[lin + 0x1568] = 0xD2;
	unsigned songSz = (unsigned)mem[lin + 0x155A]
		| ((unsigned)mem[lin + 0x155B] << 8);
	if (songSz < 64u) {
		songSz = 4096u;
		Pc98Wr16(mem, lin + 0x155Au, 4096);
	}
	unsigned songOff = (unsigned)mem[lin + 0x155E]
		| ((unsigned)mem[lin + 0x155F] << 8);
	if (songOff < 0x1A04u) {
		songOff = 0x1E04u;
		Pc98Wr16(mem, lin + 0x155Eu, 0x1E04);
	}
	const unsigned sp = (unsigned)mem[lin + 0x38A]
		| ((unsigned)mem[lin + 0x38B] << 8);
	const unsigned ss = (unsigned)mem[lin + 0x38C]
		| ((unsigned)mem[lin + 0x38D] << 8);
	if (!sp || !ss) {
		const unsigned top = songOff + songSz + 0x200u;
		Pc98Wr16(mem, lin + 0x38Au, (uint16_t)(top - 2u));
		Pc98Wr16(mem, lin + 0x38Cu, (uint16_t)g_mmdLoadSeg);
	}
	Pc98Wr16(mem, 0xD2u * 4u, 0x009C);
	Pc98Wr16(mem, 0xD2u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
	if (lin + 0x396u < 0x200000u && mem[lin + 0x392] == 0x2E
		&& mem[lin + 0x393] == 0x8C) {
		Pc98Wr16(mem, 0x14u * 4u, 0x0392);
		Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
		Pc98Wr16(mem, 0x0Bu * 4u, 0x0392);
		Pc98Wr16(mem, 0x0Bu * 4u + 2u, (uint16_t)g_mmdLoadSeg);
	}
	if (lin + 0x68Du < 0x200000u && mem[lin + 0x688] == 0xE8
		&& mem[lin + 0x68B] == 0xE8) {
		/* 0644: 1806==0 のとき 0512 stos＋05CE 16AC ダミー。毎 tick ミュートして 07FF の A0 を消す。 */
		memset(mem + lin + 0x688, 0x90, 6);
	}
	if (lin + 0x516u < 0x200000u && mem[lin + 0x512] == 0xE8
		&& mem[lin + 0x513] == 0x01 && mem[lin + 0x514] == 0x00) {
		/* 0512 は 008F/00FC/0122/0143/0688 から来る。stos が 180F ポインタを消し TL 7F を毎 IRQ 書く。 */
		mem[lin + 0x512] = 0xC3;
	}
	if (lin + 0xA59u < 0x200000u && mem[lin + 0xA56] == 0xFF
		&& mem[lin + 0xA57] == 0xA7 && mem[lin + 0xA58] == 0xEB
		&& mem[lin + 0xA59] == 0x15) {
		/* 0A4E FC: jmp [bx+15eb] が BX 破損で 09E8 に飛び ptr=0A06 になる。FC 02 は常に 0A64 テンポ。 */
		mem[lin + 0xA56] = 0xE8;
		mem[lin + 0xA57] = 0x0B;
		mem[lin + 0xA58] = 0x00;
		mem[lin + 0xA59] = 0x90;
	}
	if (lin + 0x742u < 0x200000u) {
		if (mem[lin + 0x730] == 0x75 && mem[lin + 0x731] == 0x01
			&& mem[lin + 0x732] == 0xC3)
			mem[lin + 0x732] = 0x90;
		if (mem[lin + 0x740] == 0x74 && mem[lin + 0x741] == 0x01
			&& mem[lin + 0x742] == 0xC3)
			mem[lin + 0x742] = 0x90;
	}
}

/* MicroCabin MMD.SYS 5408 MZ（seilane/maisok/f15eagle）: ヘッダ名 SOUND。
   tiny モデル。DGROUP はリンク番地 010D。MZ リロケ無しだと DS=010D 固定で
   低メモリを叩き、CS:[12] をフラグと誤認して INIT ジャンプ表を壊していた。
   API は PUSH ES（seilane CS:025C / f15 CS:0254）。AH=0/2/3/10。
   準備フラグは DGROUP:[12]（ファイル上は既に 1）。INT D2 は AH=25 AL=[18]=D2。
   OPN ISR は CS:040A（f15 は 0402）。CS:4CC は IRET 番兵。 */
static int MmdIsSound(const uint8_t* mem, unsigned lin)
{
	return mem && lin + 18u < 0x200000u
		&& memcmp(mem + lin + 10, "SOUND   ", 8) == 0;
}

static void MmdSoundApplyMzRelocs(uint8_t* img, unsigned imgSize,
	const unsigned char* mz, unsigned mzSize, uint16_t loadSeg)
{
	if (!img || !mz || mzSize < 0x1Cu || mz[0] != 'M' || mz[1] != 'Z')
		return;
	const unsigned nrel = (unsigned)(mz[6] | (mz[7] << 8));
	const unsigned roff = (unsigned)(mz[24] | (mz[25] << 8));
	if (nrel == 0 || nrel > 64u || roff + nrel * 4u > mzSize)
		return;
	int need = 0;
	for (unsigned o = 0x40u; o + 3u < imgSize && o < 0x80u; o++) {
		if (img[o] == 0xB8
			&& img[o + 1] == 0x0D && img[o + 2] == 0x01) {
			need = 1;
			break;
		}
	}
	if (!need && imgSize > 0x54u
		&& img[0x51] == 0xB8 && img[0x52] == 0x0D && img[0x53] == 0x01)
		need = 1;
	if (!need)
		return;
	for (unsigned i = 0; i < nrel; i++) {
		const unsigned e = roff + i * 4u;
		const unsigned off = (unsigned)(mz[e] | (mz[e + 1] << 8));
		const unsigned rseg = (unsigned)(mz[e + 2] | (mz[e + 3] << 8));
		const unsigned at = rseg * 16u + off;
		if (at + 1u >= imgSize)
			continue;
		unsigned w = (unsigned)img[at] | ((unsigned)img[at + 1] << 8);
		w = (w + (unsigned)loadSeg) & 0xffffu;
		img[at] = (uint8_t)(w & 0xff);
		img[at + 1] = (uint8_t)((w >> 8) & 0xff);
	}
}

static void MmdSoundPlantIvt(uint8_t* mem)
{
	if (!mem) return;
	unsigned seg = g_mmdLoadSeg;
	unsigned api = 0;
	if (mem) {
		const unsigned sD2 = (unsigned)mem[0xD2 * 4 + 2]
			| ((unsigned)mem[0xD2 * 4 + 3] << 8);
		if (sD2 && sD2 != (unsigned)DOS98_TRAMP_SEG
			&& MmdIsSound(mem, sD2 << 4))
			seg = sD2;
	}
	auto trySeg = [&](unsigned s) -> int {
		const unsigned lin = s << 4;
		if (lin + 0x2A0u >= 0x200000u) return 0;
		if (!MmdIsSound(mem, lin)) return 0;
		unsigned a = 0;
		for (unsigned o = 0x80; o + 12u < 0x800u; o++) {
			if (mem[lin + o] == 0x06 && mem[lin + o + 1] == 0x1E
				&& mem[lin + o + 2] == 0x55
				&& mem[lin + o + 9] == 0xBD) {
				a = o;
				break;
			}
		}
		if (!a) return 0;
		seg = s;
		api = a;
		return 1;
	};
	if (!seg || !trySeg(seg)) {
		seg = 0;
		api = 0;
		for (unsigned s = 0x0400u; s < 0x9000u; s += 0x10u) {
			if (trySeg(s))
				break;
		}
	}
	if (!seg || !api) return;
	const unsigned lin = seg << 4;
	g_mmdLoadSeg = seg;
	/* DGROUP は CS+10D0。CS:[12] は cmd0=INIT の near ジャンプなので書かない。 */
	const unsigned dg = lin + 0x10D0u;
	if (dg + 0x21Au < 0x200000u) {
		if (mem[dg + 0x12] == 0)
			mem[dg + 0x12] = 1;
		const unsigned p14 = (unsigned)mem[dg + 0x14]
			| ((unsigned)mem[dg + 0x15] << 8);
		if (p14 != 0x188u && p14 != 0x88u) {
			Pc98Wr16(mem, dg + 0x14u, 0x0188);
			Pc98Wr16(mem, dg + 0x16u, 0x018A);
		}
		Pc98Wr16(mem, dg + 0x216u, 0);
		if (mem[dg + 0x218] == 0 && mem[dg + 0x219] == 0)
			Pc98Wr16(mem, dg + 0x218u, 1);
	}
	Pc98Wr16(mem, 0xD2u * 4u, (uint16_t)api);
	Pc98Wr16(mem, 0xD2u * 4u + 2u, (uint16_t)seg);
	unsigned isr = 0;
	if (lin + 0x410u < 0x200000u && mem[lin + 0x40A] == 0x2E
		&& mem[lin + 0x40B] == 0x89)
		isr = 0x040A;
	else if (lin + 0x408u < 0x200000u && mem[lin + 0x402] == 0x2E
		&& mem[lin + 0x403] == 0x89)
		isr = 0x0402;
	if (!isr) {
		for (unsigned o = 0x3E0u; o + 8u < 0x480u; o++) {
			if (mem[lin + o] == 0x2E && mem[lin + o + 1] == 0x89
				&& mem[lin + o + 2] == 0x26) {
				isr = o;
				break;
			}
		}
	}
	if (!isr)
		isr = 0x040A;
	if (lin + 0x408u < 0x200000u) {
		const unsigned ss = (unsigned)mem[lin + 0x408]
			| ((unsigned)mem[lin + 0x409] << 8);
		if (ss == 0x010Du)
			Pc98Wr16(mem, lin + 0x408u, (uint16_t)(seg + 0x10Du));
		const unsigned sp = (unsigned)mem[lin + 0x406]
			| ((unsigned)mem[lin + 0x407] << 8);
		if (!sp)
			Pc98Wr16(mem, lin + 0x406u, 0x1190);
	}
	Pc98Wr16(mem, 0x14u * 4u, (uint16_t)isr);
	Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)seg);
	Pc98Wr16(mem, 0x0Bu * 4u, (uint16_t)isr);
	Pc98Wr16(mem, 0x0Bu * 4u + 2u, (uint16_t)seg);
}

static void MmdSoundRelocLoaded(uint8_t* mem, CEmuDos98& dos, uint16_t seg)
{
	if (!mem || !seg) return;
	const unsigned lin = (unsigned)seg << 4;
	if (!MmdIsSound(mem, lin))
		return;
	const CEmuDos98File* f = dos.FindFile("MMD.SYS");
	if (!f || !f->data || f->size < 0x20u)
		return;
	unsigned imgSize = f->size;
	if (f->data[0] == 'M' && f->data[1] == 'Z') {
		const unsigned hdr = (unsigned)(f->data[8] | (f->data[9] << 8)) * 16u;
		if (hdr >= 0x20u && hdr < f->size)
			imgSize = f->size - hdr;
	}
	if (imgSize > 0x2000u)
		imgSize = 0x2000u;
	MmdSoundApplyMzRelocs(mem + lin, imgSize, f->data, f->size, seg);
}

static void MmdSoundEnsureFromZip(uint8_t* mem, CEmuDos98& dos)
{
	MmdSoundPlantIvt(mem);
	const CEmuDos98File* f = dos.FindFile("MMD.SYS");
	if (!f || !f->data || f->size < 0x300u)
		return;
	const unsigned char* data = f->data;
	unsigned size = f->size;
	const unsigned char* mz = NULL;
	unsigned mzSize = 0;
	if (size >= 0x20u && data[0] == 'M' && data[1] == 'Z') {
		mz = data;
		mzSize = size;
		const unsigned hdr = (unsigned)(data[8] | (data[9] << 8)) * 16u;
		if (hdr >= 0x20u && hdr + 18u <= size) {
			const uint16_t attr = (uint16_t)(data[hdr + 4] | (data[hdr + 5] << 8));
			if (attr & 0x8000) {
				data += hdr;
				size -= hdr;
			}
		}
	}
	if (size < 0x270u || memcmp(data + 10, "SOUND   ", 8) != 0)
		return;
	uint16_t seg = (uint16_t)g_mmdLoadSeg;
	if (!seg && mem) {
		const unsigned sD2 = (unsigned)mem[0xD2 * 4 + 2]
			| ((unsigned)mem[0xD2 * 4 + 3] << 8);
		if (sD2 && sD2 != (unsigned)DOS98_TRAMP_SEG
			&& MmdIsSound(mem, sD2 << 4))
			seg = (uint16_t)sD2;
	}
	if (!seg) {
		if (!mem || !dos.AllocBlock(mem, 0x1000, &seg) || !seg)
			return;
	}
	const unsigned lin = (unsigned)seg << 4;
	if (lin + 0x10000u >= 0x200000u)
		return;
	/* maisok の device 4096 は 4896 バイト SYS を切る（DGROUP ジャンプ表が欠ける）。 */
	memset(mem + lin, 0, 0x10000u);
	memcpy(mem + lin, data, size);
	g_mmdLoadSeg = seg;
	g_mmdClassic = 0;
	g_mmdPicIsr = 0;
	if (mz)
		MmdSoundApplyMzRelocs(mem + lin, size, mz, mzSize, seg);
	MmdSoundPlantIvt(mem);
}

/* SOUND AH=02 は [F4]=[F6]=1190。FE ループは [F4]=[F6] のあと DEC するので
   曲先頭-1（ゼロ）を食ってパーサが壊れる。短い SLM02 は 8s 内に FE に届く。
   [F6]=1191 なら DEC 後 1190。 */
static void MmdSoundArmLoop(uint8_t* mem, CEmuDos98& dos, const char* song)
{
	if (!mem || !g_mmdLoadSeg)
		return;
	const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
	if (!MmdIsSound(mem, lin))
		return;
	const unsigned dg = lin + 0x10D0u;
	if (dg + 0x11A0u >= 0x200000u)
		return;
	if (song && song[0]) {
		const CEmuDos98File* sf = dos.FindFile(song);
		if (sf && sf->data && sf->size) {
			unsigned n = sf->size;
			if (n > 0x7E00u)
				n = 0x7E00u;
			const unsigned dest = dg + 0x1190u;
			if (dest + n < 0x200000u)
				memcpy(mem + dest, sf->data, n);
		}
	}
	/* 糊が AH=02 を飛ばすと [E2]=1 のまま ISR がパースしない（maisok）。
	   既に再生中（seilane 0x10）へ AH=02 を重ねると 28h が死ぬ。 */
	if (mem[dg + 0xE2]) {
		const unsigned sD2 = (unsigned)mem[0xD2 * 4 + 2]
			| ((unsigned)mem[0xD2 * 4 + 3] << 8);
		if (sD2 && sD2 != (unsigned)DOS98_TRAMP_SEG) {
			np2_reg_set(NP2_R_AX, 0x0200);
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			np2_interrupt(0xD2);
		}
	}
	mem[dg + 0xE5] = 1;
	mem[dg + 0xE2] = 0;
	Pc98Wr16(mem, dg + 0xF6u, 0x1191);
	const unsigned f4 = (unsigned)mem[dg + 0xF4]
		| ((unsigned)mem[dg + 0xF5] << 8);
	if (f4 < 0x1190u || f4 > 0x1190u + 0x4000u)
		Pc98Wr16(mem, dg + 0xF4u, 0x1190);
}

/* 07DC が 08AA/F4 のあと [si]=0A06（09E8 の機械語）になる。メタはホストで副作用だけやり、07FF ノートで止める。 */
static unsigned MmdSkipToNote(uint8_t* mem, unsigned lin, unsigned off, unsigned chBlk)
{
	for (int sk = 0; sk < 64 && off && lin + off + 3u < 0x200000u; sk++) {
		const uint8_t op = mem[lin + off];
		if (op == 0)
			break;
		if (op >= 1u && op <= 0x24u)
			break;
		if (op == 0xFC) {
			off += 3u;
			continue;
		}
		if (op >= 0x42u && op <= 0x61u) {
			off += 1u;
			continue;
		}
		if (op >= 0x28u && op <= 0x2Fu) {
			if (chBlk && chBlk + 8u < 0x200000u)
				mem[chBlk + 8u] = (uint8_t)(op - 0x28u);
			off += 1u;
			continue;
		}
		if (op >= 0x30u && op <= 0x31u) {
			off += 1u;
			continue;
		}
		if (op >= 0x32u && op <= 0x41u) {
			off += 1u;
			continue;
		}
		if (op >= 0x62u && op <= 0xF1u) {
			off += 1u;
			continue;
		}
		if (op >= 0xF4u && op <= 0xFBu) {
			if (chBlk && chBlk + 0xCu < 0x200000u) {
				uint8_t v = mem[chBlk + 0xCu];
				v = (uint8_t)((v & 0xC0u) | (op - 0xF4u));
				mem[chBlk + 0xCu] = v;
			}
			off += 1u;
			continue;
		}
		if (op == 0xF2 || op == 0xF3) {
			off += 1u;
			continue;
		}
		if (op == 0xFE || op == 0xFD) {
			off += 2u;
			continue;
		}
		off += 1u;
	}
	return off;
}

static void MmdRebindTracks(uint8_t* mem, unsigned lin, unsigned base, unsigned songOff)
{
	unsigned di = songOff;
	for (unsigned ch = 0; ch < 6u; ch++) {
		const unsigned p = base + ch * 0x33u;
		if (p + 4u >= 0x200000u) break;
		mem[p] = (uint8_t)(di & 0xff);
		mem[p + 1] = (uint8_t)(di >> 8);
		mem[p + 2] = 1;
		mem[p + 3] = 1;
		g_mmdTrkBase[ch] = di;
		const unsigned note = MmdSkipToNote(mem, lin, di, p);
		mem[p] = (uint8_t)(note & 0xff);
		mem[p + 1] = (uint8_t)(note >> 8);
		int guard = 0;
		while (guard++ < 8192 && lin + di + 3u < 0x200000u) {
			const uint8_t al = mem[lin + di];
			di++;
			if (al == 0)
				break;
			if (al < 0xFC)
				continue;
			di++;
			if (al == 0xFC)
				di++;
		}
	}
}

/* 4655/4688 MMD2.SYS API は CS:0078 PUSH ES。4026 は INIT が 0078 にあり API は 022C、ポートは c80、待ちは f8f、曲は 13BA。
   xak2 MMD.SYS 4951 "MMD200OR" は API 0072 PUSH ES、ポート ca6、曲 13B6、待ち e2a、ISR 0276。
   gazzel MMD.SYS 5273 "MMD200  " は API 00A2 PUSH ES、ポート d06、曲 149C、声 109C、待ち ee2、ISR 02A8。
   xak_98 MMD2.SYS 4657 は API 0078 だがポートは c7e/c80、曲 1386、声 F86、待ち f82。c7c は YM フラグ（0188 を書くとデータポートが 0 のまま）。
   AH=10/11 は 01FC 未実装。 */
static int Mmd2Layout(const uint8_t* mem, unsigned lin)
{
	if (!mem || lin + 0x79u >= 0x200000u) return 0;
	if (mem[lin + 0x78] == 0x06) {
		/* xak 4657: [c7c] は YM フラグ=1、[c7a]=0。4655/4688 は逆。AH=1 は mov dx,1386。 */
		if (lin + 0xC7Du < 0x200000u
			&& mem[lin + 0xC7A] == 0 && mem[lin + 0xC7C] == 1)
			return 4;
		if (lin + 0xC89u < 0x200000u) {
			const unsigned songHint = (unsigned)mem[lin + 0xC88]
				| ((unsigned)mem[lin + 0xC89] << 8);
			if (songHint == 0x1386u)
				return 4;
		}
		if (lin + 0xF7u < 0x200000u
			&& mem[lin + 0xF4] == 0xBA
			&& mem[lin + 0xF5] == 0x86 && mem[lin + 0xF6] == 0x13)
			return 4;
		return 1;
	}
	if (mem[lin + 0x72] == 0x06) return 2; /* 中 4951 xak2 */
	if (lin + 0xA3u < 0x200000u && mem[lin + 0xA2] == 0x06)
		return 3; /* gazzel 5273 */
	return 0; /* 旧 4026 */
}

static int Mmd2ApiNew(const uint8_t* mem, unsigned lin)
{
	return Mmd2Layout(mem, lin) == 1;
}

static int MmdClassicIsSbr(const uint8_t* mem, unsigned lin)
{
	return mem && lin + 0x394u < 0x200000u
		&& mem[lin + 0x392] == 0x2E && mem[lin + 0x393] == 0x8C;
}

static int MmdClassicIsFray(const uint8_t* mem, unsigned lin)
{
	return mem && lin + 0x378u < 0x200000u
		&& mem[lin + 0x376] == 0x2E && mem[lin + 0x377] == 0x8C
		&& !MmdClassicIsSbr(mem, lin);
}

static void Mmd2PlantPorts(uint8_t* mem, unsigned lin)
{
	if (!mem || lin + 0xC83u >= 0x200000u) return;
	const int lay = Mmd2Layout(mem, lin);
	if (lay == 1) {
		mem[lin + 0xC7C] = 0x88;
		mem[lin + 0xC7D] = 0x01;
		mem[lin + 0xC7E] = 0x8A;
		mem[lin + 0xC7F] = 0x01;
	} else if (lay == 4) {
		if (lin + 0xC81u >= 0x200000u) return;
		mem[lin + 0xC7C] |= 1;
		mem[lin + 0xC7E] = 0x88;
		mem[lin + 0xC7F] = 0x01;
		mem[lin + 0xC80] = 0x8A;
		mem[lin + 0xC81] = 0x01;
		unsigned dest = (unsigned)mem[lin + 0xC88]
			| ((unsigned)mem[lin + 0xC89] << 8);
		if (dest < 0x1386u)
			Pc98Wr16(mem, lin + 0xC88u, 0x1386);
		unsigned voi = (unsigned)mem[lin + 0xC8A]
			| ((unsigned)mem[lin + 0xC8B] << 8);
		if (voi < 0x800u)
			Pc98Wr16(mem, lin + 0xC8Au, 0x0F86);
		unsigned songSz = (unsigned)mem[lin + 0xC84]
			| ((unsigned)mem[lin + 0xC85] << 8);
		if (songSz < 64u || songSz > 0x4000u)
			Pc98Wr16(mem, lin + 0xC84u, 0x1390);
		if (lin + 0x25Fu < 0x200000u) {
			const unsigned sp = (unsigned)mem[lin + 0x25C]
				| ((unsigned)mem[lin + 0x25D] << 8);
			const unsigned ss = (unsigned)mem[lin + 0x25E]
				| ((unsigned)mem[lin + 0x25F] << 8);
			if (!sp || !ss) {
				Pc98Wr16(mem, lin + 0x25Cu, 0x27B4);
				Pc98Wr16(mem, lin + 0x25Eu, (uint16_t)g_mmdLoadSeg);
			}
		}
		Pc98Wr16(mem, 0xD2u * 4u, 0x0078);
		Pc98Wr16(mem, 0xD2u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
		Pc98Wr16(mem, 0x14u * 4u, 0x0264);
		Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
		Pc98Wr16(mem, 0x0Bu * 4u, 0x0264);
		Pc98Wr16(mem, 0x0Bu * 4u + 2u, (uint16_t)g_mmdLoadSeg);
	} else if (lay == 3) {
		if (lin + 0xD1Du >= 0x200000u) return;
		mem[lin + 0xD04] |= 1;
		mem[lin + 0xD06] = 0x88;
		mem[lin + 0xD07] = 0x01;
		mem[lin + 0xD08] = 0x8A;
		mem[lin + 0xD09] = 0x01;
		unsigned dest = (unsigned)mem[lin + 0xD1A]
			| ((unsigned)mem[lin + 0xD1B] << 8);
		if (dest < 0x149Cu)
			Pc98Wr16(mem, lin + 0xD1Au, 0x149C);
		unsigned voi = (unsigned)mem[lin + 0xD0E]
			| ((unsigned)mem[lin + 0xD0F] << 8);
		if (voi < 0x1000u)
			Pc98Wr16(mem, lin + 0xD0Eu, 0x109C);
		unsigned voi12 = (unsigned)mem[lin + 0xD1C]
			| ((unsigned)mem[lin + 0xD1D] << 8);
		if (voi12 < 0x1000u)
			Pc98Wr16(mem, lin + 0xD1Cu, 0x109C);
		/* AH=0 は CX=[d16]。ファイル FFFF だと COM AH=3F が 64K 読む。OPNING は 2998。 */
		unsigned songSz = (unsigned)mem[lin + 0xD16]
			| ((unsigned)mem[lin + 0xD17] << 8);
		if (songSz < 64u || songSz > 0x4000u) {
			Pc98Wr16(mem, lin + 0xD14u, 0x1390);
			Pc98Wr16(mem, lin + 0xD16u, 0x1390);
		}
		if (lin + 0x2A3u < 0x200000u) {
			const unsigned sp = (unsigned)mem[lin + 0x2A0]
				| ((unsigned)mem[lin + 0x2A1] << 8);
			const unsigned ss = (unsigned)mem[lin + 0x2A2]
				| ((unsigned)mem[lin + 0x2A3] << 8);
			if (!sp || !ss) {
				Pc98Wr16(mem, lin + 0x2A0u, 0x27B4);
				Pc98Wr16(mem, lin + 0x2A2u, (uint16_t)g_mmdLoadSeg);
			}
		}
		Pc98Wr16(mem, 0xD2u * 4u, 0x00A2);
		Pc98Wr16(mem, 0xD2u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
		Pc98Wr16(mem, 0x14u * 4u, 0x02A8);
		Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
		Pc98Wr16(mem, 0x0Bu * 4u, 0x02A8);
		Pc98Wr16(mem, 0x0Bu * 4u + 2u, (uint16_t)g_mmdLoadSeg);
	} else if (lay == 2) {
		if (lin + 0xCA9u >= 0x200000u) return;
		mem[lin + 0xCA6] = 0x88;
		mem[lin + 0xCA7] = 0x01;
		mem[lin + 0xCA8] = 0x8A;
		mem[lin + 0xCA9] = 0x01;
		if (lin + 0xCB5u < 0x200000u) {
			unsigned dest = (unsigned)mem[lin + 0xCB4]
				| ((unsigned)mem[lin + 0xCB5] << 8);
			if (dest < 0x1000u)
				Pc98Wr16(mem, lin + 0xCB4u, 0x13B6);
		}
		if (lin + 0xCB7u < 0x200000u) {
			unsigned voi = (unsigned)mem[lin + 0xCB6]
				| ((unsigned)mem[lin + 0xCB7] << 8);
			if (voi < 0x800u)
				Pc98Wr16(mem, lin + 0xCB6u, 0x0FB6);
		}
		if (lin + 0x271u < 0x200000u) {
			const unsigned sp = (unsigned)mem[lin + 0x26E]
				| ((unsigned)mem[lin + 0x26F] << 8);
			const unsigned ss = (unsigned)mem[lin + 0x270]
				| ((unsigned)mem[lin + 0x271] << 8);
			if (!sp || !ss) {
				Pc98Wr16(mem, lin + 0x26Eu, 0x27B4);
				Pc98Wr16(mem, lin + 0x270u, (uint16_t)g_mmdLoadSeg);
			}
		}
	} else {
		mem[lin + 0xC80] = 0x88;
		mem[lin + 0xC81] = 0x01;
		mem[lin + 0xC82] = 0x8A;
		mem[lin + 0xC83] = 0x01;
		if (lin + 0x383u < 0x200000u) {
			const unsigned sp = (unsigned)mem[lin + 0x380]
				| ((unsigned)mem[lin + 0x381] << 8);
			const unsigned ss = (unsigned)mem[lin + 0x382]
				| ((unsigned)mem[lin + 0x383] << 8);
			if (!sp || !ss) {
				Pc98Wr16(mem, lin + 0x380u, 0x27B4);
				Pc98Wr16(mem, lin + 0x382u, (uint16_t)g_mmdLoadSeg);
			}
		}
	}
}

/* MmdPlayAssist の実装 */
static void MmdPlayAssist(uint8_t* mem)
{
	if (!mem || !g_mmdLoadSeg) return;
	const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
	if (lin >= 0x200000u) return;
	if (MmdIsSound(mem, lin)) {
		MmdSoundPlantIvt(mem);
		return;
	}
	if (g_mmdClassic) {
		if (MmdClassicIsFray(mem, lin)) {
			if (lin + 0x1531u < 0x200000u) {
				mem[lin + 0x152C] |= 1;
				mem[lin + 0x152E] = 0x88;
				mem[lin + 0x152F] = 0x01;
				mem[lin + 0x1530] = 0x8A;
				mem[lin + 0x1531] = 0x01;
			}
			if (lin + 0x1543u < 0x200000u) {
				unsigned songOff = (unsigned)mem[lin + 0x1542]
					| ((unsigned)mem[lin + 0x1543] << 8);
				if (songOff < 0x19E8u)
					Pc98Wr16(mem, lin + 0x1542u, 0x1DE8);
			}
			return;
		}
		if (lin + 0x154Du < 0x200000u) {
			mem[lin + 0x154A] = 0x88;
			mem[lin + 0x154B] = 0x01;
			mem[lin + 0x154C] = 0x8A;
			mem[lin + 0x154D] = 0x01;
		}
		const unsigned base = lin + 0x180Fu;
		int anyDur = 0;
		for (unsigned ch = 0; ch < 6u; ch++) {
			const unsigned p = base + ch * 0x33u;
			if (p + 3u >= 0x200000u) continue;
			unsigned off = (unsigned)mem[p] | ((unsigned)mem[p + 1] << 8);
			if (off) {
				if (mem[p + 2] == 0)
					mem[p + 2] = 1;
				if (mem[p + 3] == 0)
					mem[p + 3] = 1;
				anyDur = 1;
			}
		}
		/* 0512 が 1808 を stos で消す。AH=1 のあと 1808==0 だと ISR 0644 が再 init＋16AC ダミー解析で曲を消す。15b0!=0 の AH=3 待ち中は触らない。 */
		if (anyDur && lin + 0x1808u < 0x200000u && mem[lin + 0x15B0] == 0
			&& mem[lin + 0x1808] == 0)
			mem[lin + 0x1808] = 0x40;
		/* 071C の二段 RET: duration==0 は 0732、[180A]!=0 は 0742。どちらも 07DC に入らず ptr が曲先頭のまま（sbr は 27h ack だけ）。 */
		if (lin + 0x68Du < 0x200000u && mem[lin + 0x688] == 0xE8
			&& mem[lin + 0x68B] == 0xE8)
			memset(mem + lin + 0x688, 0x90, 6);
		if (lin + 0x516u < 0x200000u && mem[lin + 0x512] == 0xE8
			&& mem[lin + 0x513] == 0x01 && mem[lin + 0x514] == 0x00)
			mem[lin + 0x512] = 0xC3;
		if (lin + 0xA59u < 0x200000u && mem[lin + 0xA56] == 0xFF
			&& mem[lin + 0xA57] == 0xA7 && mem[lin + 0xA58] == 0xEB
			&& mem[lin + 0xA59] == 0x15) {
			mem[lin + 0xA56] = 0xE8;
			mem[lin + 0xA57] = 0x0B;
			mem[lin + 0xA58] = 0x00;
			mem[lin + 0xA59] = 0x90;
		}
		if (lin + 0x7B7u < 0x200000u) {
			static const unsigned kRet[] = { 0x732u, 0x742u, 0x7B1u, 0x7B7u };
			for (unsigned i = 0; i < 4u; i++) {
				const unsigned o = kRet[i];
				if (mem[lin + o] == 0xC3
					&& mem[lin + o - 1u] == 0x01
					&& (mem[lin + o - 2u] == 0x74 || mem[lin + o - 2u] == 0x75))
					mem[lin + o] = 0x90;
			}
		}
		if (lin + 0x17F6u < 0x200000u && mem[lin + 0x17F6] == 0)
			mem[lin + 0x17F6] = 0x07;
		if (lin + 0x15B0u < 0x200000u)
			mem[lin + 0x15B0] = 0;
		if (lin + 0x1808u < 0x200000u) {
			if (mem[lin + 0x1808] == 0)
				mem[lin + 0x1808] = 0x40;
			mem[lin + 0x1806] = 0x40;
			mem[lin + 0x1807] = 0x40;
		}
		/* 0644 は 1808 を 17FA/1800 と比べ、未満なら 1806=0 → 0512 stos＋16AC ダミーで曲ポインタを消す。 */
		if (lin + 0x1805u < 0x200000u) {
			mem[lin + 0x17FA] = mem[lin + 0x17FC] = mem[lin + 0x17FE] = 0;
			mem[lin + 0x1800] = mem[lin + 0x1802] = mem[lin + 0x1804] = 0;
		}
		{
			unsigned songOff = (unsigned)mem[lin + 0x155E]
				| ((unsigned)mem[lin + 0x155F] << 8);
			int rebind = 0;
			for (unsigned ch = 0; ch < 6u; ch++) {
				const unsigned p = base + ch * 0x33u;
				if (p + 1u >= 0x200000u) continue;
				unsigned off = (unsigned)mem[p] | ((unsigned)mem[p + 1] << 8);
				if (off >= 0x16ACu && off < 0x1720u)
					rebind = 1;
				if (ch >= 1u && songOff >= 0x1A04u && off
					&& off < songOff + 0x40u)
					rebind = 1;
			}
			if (songOff >= 0x1A04u && rebind
				&& lin + songOff + 8u < 0x200000u)
				MmdRebindTracks(mem, lin, base, songOff);
			{
				unsigned songSz = (unsigned)mem[lin + 0x155A]
					| ((unsigned)mem[lin + 0x155B] << 8);
				if (songSz < 64u) songSz = 4096u;
				for (unsigned ch = 0; ch < 6u; ch++) {
					const unsigned p = base + ch * 0x33u;
					if (p + 1u >= 0x200000u) continue;
					unsigned off = (unsigned)mem[p] | ((unsigned)mem[p + 1] << 8);
					if (songOff >= 0x1A04u && off >= songOff
						&& off < songOff + songSz)
						g_mmdTrkBase[ch] = off;
					else if (g_mmdTrkBase[ch] && off && off < 0x1A04u) {
						off = g_mmdTrkBase[ch];
						mem[p] = (uint8_t)(off & 0xff);
						mem[p + 1] = (uint8_t)(off >> 8);
					}
					if (!off || lin + off >= 0x200000u) continue;
					const uint8_t op = mem[lin + off];
					if (op >= 1u && op <= 0x24u)
						continue;
					if (op == 0xFC || (op >= 0x28u && op <= 0x61u)
						|| op >= 0xF2u) {
						const unsigned note = MmdSkipToNote(mem, lin, off, p);
						mem[p] = (uint8_t)(note & 0xff);
						mem[p + 1] = (uint8_t)(note >> 8);
						if (note >= songOff && note < songOff + songSz)
							g_mmdTrkBase[ch] = note;
					}
					if (p + 8u < 0x200000u && mem[p + 8] == 0)
						mem[p + 8] = 1;
				}
			}
		}
		if (lin + 0x17F4u < 0x200000u && anyDur)
			mem[lin + 0x17F4] = 1;
		if (lin + 0x154Eu < 0x200000u)
			mem[lin + 0x154E] = 0;
		/* AH=3 は 17F4 を STI 待ち。ダミー 16AC が 1809=1 のままだと cmd0 が AH=10 に届かない。曲解析後は触らない。 */
		if (!anyDur && lin + 0x17F4u < 0x200000u && mem[lin + 0x17F4])
			mem[lin + 0x17F4] = 0;
	} else if (g_mmdPicIsr) {
		/* 新 MMD2: [c7c] が 0 だと 0347 busy が PIC を見に行き IN 00=0x80 で ISR 永久スピン。AH=3 は [f80]!=0 待ち。
		   旧 4026: ポートは [c80]、AH=3 は [f8f]==0 を STI 待ち（michael/orangerd）。 */
		Mmd2PlantPorts(mem, lin);
		{
			const int lay = Mmd2Layout(mem, lin);
			if (lay == 1) {
			if (lin + 0xF80u < 0x200000u && mem[lin + 0xF80])
				mem[lin + 0xF80] = 0;
			} else if (lay == 4) {
				if (lin + 0xF82u < 0x200000u && mem[lin + 0xF82])
					mem[lin + 0xF82] = 0;
				Pc98Wr16(mem, 0xD2u * 4u, 0x0078);
				Pc98Wr16(mem, 0xD2u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
				Pc98Wr16(mem, 0x14u * 4u, 0x0264);
				Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
				Pc98Wr16(mem, 0x0Bu * 4u, 0x0264);
				Pc98Wr16(mem, 0x0Bu * 4u + 2u, (uint16_t)g_mmdLoadSeg);
			} else if (lay == 2) {
				Pc98Wr16(mem, 0xD2u * 4u, 0x0072);
				Pc98Wr16(mem, 0xD2u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
				Pc98Wr16(mem, 0x14u * 4u, 0x0276);
				Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
				Pc98Wr16(mem, 0x0Bu * 4u, 0x0276);
				Pc98Wr16(mem, 0x0Bu * 4u + 2u, (uint16_t)g_mmdLoadSeg);
				/* ISR 049E: e27==0 だと al=0 で STOP+dee 再解析。曲テンポが来るまで拍を置く。 */
				if (lin + 0xE27u < 0x200000u && mem[lin + 0xE27] == 0)
					mem[lin + 0xE27] = 0x38;
			} else if (lay == 3) {
				Pc98Wr16(mem, 0xD2u * 4u, 0x00A2);
				Pc98Wr16(mem, 0xD2u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
				Pc98Wr16(mem, 0x14u * 4u, 0x02A8);
				Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
				Pc98Wr16(mem, 0x0Bu * 4u, 0x02A8);
				Pc98Wr16(mem, 0x0Bu * 4u + 2u, (uint16_t)g_mmdLoadSeg);
				/* AH=3 は [ee2]==0 待ち。再生中は 1 のまま。4DB の ed1==0 リセットはホスト tick で鳴らす。 */
			} else {
			const uint16_t cs = np2_reg_get(NP2_R_CS);
			const uint16_t ip = np2_reg_get(NP2_R_IP);
			if (cs == (uint16_t)g_mmdLoadSeg && ip >= 0x2B0u && ip <= 0x2BEu
				&& lin + 0xF8Fu < 0x200000u && mem[lin + 0xF8F] == 0)
				mem[lin + 0xF8F] = 1;
			Pc98Wr16(mem, 0xD2u * 4u, 0x022C);
			Pc98Wr16(mem, 0xD2u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
			Pc98Wr16(mem, 0x14u * 4u, 0x0388);
			Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
			Pc98Wr16(mem, 0x0Bu * 4u, 0x0388);
			Pc98Wr16(mem, 0x0Bu * 4u + 2u, (uint16_t)g_mmdLoadSeg);
			}
		}
	}
	/* 4655 オーバーレイ: COM は far ptr を CS:09D0 に置く。ファイル F-num ライターはまだそこを CALL するので IP=09D7 が 0F（#UD）。DF は 09D6。 */
	if (g_mmdPlayAssist && g_mmd2FnSrc && lin + 0x9E0u < 0x200000u
		&& mem[lin + 0x9D6] == 0xDF && mem[lin + 0x9D7] == 0x0F)
		memcpy(mem + lin + 0x9C0, g_mmd2FnSrc, 32);
}

/* 実 PC-98: ITF @ F000-F7FF、N88 BIOS @ F800-FFFF、テキスト VRAM @ A000、属性 VRAM @ A200、DIP/MEMSW はテキスト頁、BIOS ワーク @ 0000:0500。空の F000:0000 は 00h（ADD [BX+SI],AL）で F800:0001 は IRET だったので、ファームへの far CALL が誤フレームを pop し後で INT6。 */
static void PlantPc98BiosMap(uint8_t* mem)
{
	if (!mem) return;
	auto fillRetf = [&](unsigned lo, unsigned hi) {
		int empty = 1;
		for (unsigned a = lo; a < hi; a++) {
			if (mem[a]) { empty = 0; break; }
		}
		if (!empty) return;
		memset(mem + lo, 0xCB, hi - lo); /* RETF — far CALL ファーム */
	};
	fillRetf(0xF0000u, 0xF8000u);
	/* F800 BIOS 窓: byte0 は far CALL F800:0000 用 RETF。残りは IRET で、高 ROM に落ちた壊れた IVT でも INT から戻る。 */
	{
		int empty = 1;
		for (unsigned a = 0xF8000u; a < 0x100000u; a++) {
			if (mem[a]) { empty = 0; break; }
		}
		if (empty) {
			mem[0xF8000u] = 0xCB;
			memset(mem + 0xF8001u, 0xCF, 0x100000u - 0xF8001u);
		}
	}

	auto fillText = [&](unsigned base) {
		int empty = 1;
		for (unsigned i = 0; i < 16; i++) {
			if (mem[base + i]) { empty = 0; break; }
		}
		if (!empty) return;
		for (unsigned i = 0; i < 80u * 25u * 2u; i += 2) {
			mem[base + i] = 0x20;
			mem[base + i + 1] = 0xE1;
		}
	};
	fillText(0xA0000u);
	fillText(0xA2000u);

	/* MEMSW（NP2 A000:3FE2、16 バイト）。3FEE の bit0/bit3 = 286 + FM 基板 */
	if (0xA0000u + 0x3FEFu < 0x200000u) {
		if (mem[0xA0000u + 0x3FE2u] == 0)
			mem[0xA0000u + 0x3FE2u] = 0x48;
		mem[0xA0000u + 0x3FEEu] = (uint8_t)(mem[0xA0000u + 0x3FEEu] | 0x09);
	}

	/* BIOS ワーク: 0510-0544 を飛ばす（DOS LOL / 非 DOS INT18 stub） */
	mem[0x501] = (uint8_t)(mem[0x501] | 0x08); /* 80286 / QueenSoft FM */
	if (mem[0x504] == 0 && mem[0x505] == 0) {
		mem[0x504] = 0x80; /* 通常 640 KB */
		mem[0x505] = 0x02;
	}
	mem[0x536] = (uint8_t)(mem[0x536] | 0x04); /* 音源基板あり */
	/* 拡張メモリ KB @0584。日時計 @05A0（INT 08 トランポリン） */
	if (mem[0x584] == 0 && mem[0x585] == 0)
		mem[0x584] = 0;
}

/* 実 PC-98 BIOS INT 08: 0000:05A0（と IBM 0040:006C）を加算、INT 1C をチェイン、マスタ PIC を EOI。00C0 に置く — DOS アリーナ（1000h）より下、IvtHooked が拒否するトランポリン（0060）/ F000 ROM の外。日時計を HLT または poll するドライバは、PC/AT が IRQ0 に BDA tick を植えるのと同じくこれが要る。 */
static void PlantPc98BiosTimer(uint8_t* mem)
{
	if (!mem) return;
	const unsigned tickSeg = 0x00C0;
	const unsigned b = tickSeg << 4;
	static const uint8_t kIsr[] = {
		0x50,                         /* push ax */
		0x1E,                         /* push ds */
		0x31, 0xC0,                   /* xor ax, ax */
		0x8E, 0xD8,                   /* mov ds, ax */
		0xFF, 0x06, 0xA0, 0x05,       /* inc word [05A0] */
		0x83, 0x16, 0xA2, 0x05, 0x00, /* adc word [05A2], 0 */
		0xFF, 0x06, 0x6C, 0x04,       /* inc word [046C] */
		0x83, 0x16, 0x6E, 0x04, 0x00, /* adc word [046E], 0 */
		0x1F,                         /* pop ds */
		0xCD, 0x1C,                   /* int 1Ch */
		0xB0, 0x20,                   /* mov al, 20h */
		0xE6, 0x00,                   /* out 00h, al */
		0x58,                         /* pop ax */
		0xCF                          /* iret */
	};
	memcpy(mem + b, kIsr, sizeof(kIsr));
	mem[0x08 * 4 + 0] = 0x00;
	mem[0x08 * 4 + 1] = 0x00;
	mem[0x08 * 4 + 2] = (uint8_t)(tickSeg & 0xff);
	mem[0x08 * 4 + 3] = (uint8_t)(tickSeg >> 8);
	/* INT 1C 既定は IRET。未植の 0000:0000 だと BIOS INT 08 のチェインが IVT をコード実行する。 */
	{
		const unsigned o1c = (unsigned)mem[0x1C * 4] | ((unsigned)mem[0x1C * 4 + 1] << 8);
		const unsigned s1c = (unsigned)mem[0x1C * 4 + 2] | ((unsigned)mem[0x1C * 4 + 3] << 8);
		if (s1c == 0 && o1c == 0) {
			const unsigned iretOff = (unsigned)sizeof(kIsr);
			mem[b + iretOff] = 0xCF;
			mem[0x1C * 4 + 0] = (uint8_t)(iretOff & 0xff);
			mem[0x1C * 4 + 1] = 0x00;
			mem[0x1C * 4 + 2] = (uint8_t)(tickSeg & 0xff);
			mem[0x1C * 4 + 3] = (uint8_t)(tickSeg >> 8);
		}
	}
}

/* pc98vx / bootcs は DOS を通らない。FMD98.DRV は INT 21 AH=25/35 で
   INT08（曲テンポ）と INT0C を植える。ベクタが 0000:0000 のままだと
   AH=25 はゴミを実行し、Timer B も IRQ0 も来ず FM が遅れ MIDI が死ぬ。 */
static void PlantPc98Int21SetVec(uint8_t* mem)
{
	if (!mem) return;
	const unsigned o21 = (unsigned)mem[0x21 * 4] | ((unsigned)mem[0x21 * 4 + 1] << 8);
	const unsigned s21 = (unsigned)mem[0x21 * 4 + 2] | ((unsigned)mem[0x21 * 4 + 3] << 8);
	if (s21 != 0 || o21 != 0)
		return;
	const unsigned seg = 0x00D0;
	const unsigned b = seg << 4;
	static const uint8_t kDos[] = {
		0x80, 0xFC, 0x25,       /* cmp  ah,25h          */
		0x74, 0x06,             /* je   setvec          */
		0x80, 0xFC, 0x35,       /* cmp  ah,35h          */
		0x74, 0x19,             /* je   getvec          */
		0xCF,                   /* iret                 */
		/* setvec */
		0x53,                   /* push bx              */
		0x51,                   /* push cx              */
		0x1E,                   /* push ds              */
		0x31, 0xDB,             /* xor  bx,bx           */
		0x8E, 0xDB,             /* mov  ds,bx           */
		0x30, 0xFF,             /* xor  bh,bh           */
		0x8A, 0xD8,             /* mov  bl,al           */
		0xD1, 0xE3,             /* shl  bx,1            */
		0xD1, 0xE3,             /* shl  bx,1            */
		0x89, 0x17,             /* mov  [bx],dx         */
		0x59,                   /* pop  cx ; orig DS    */
		0x89, 0x4F, 0x02,       /* mov  [bx+2],cx       */
		0x59,                   /* pop  cx              */
		0x5B,                   /* pop  bx              */
		0xCF,                   /* iret                 */
		/* getvec */
		0x1E,                   /* push ds              */
		0x31, 0xDB,             /* xor  bx,bx           */
		0x8E, 0xDB,             /* mov  ds,bx           */
		0x30, 0xFF,             /* xor  bh,bh           */
		0x8A, 0xD8,             /* mov  bl,al           */
		0xD1, 0xE3,             /* shl  bx,1            */
		0xD1, 0xE3,             /* shl  bx,1            */
		0xC4, 0x1F,             /* les  bx,[bx]         */
		0x1F,                   /* pop  ds              */
		0xCF                    /* iret                 */
	};
	memcpy(mem + b, kDos, sizeof(kDos));
	mem[0x21 * 4 + 0] = 0x00;
	mem[0x21 * 4 + 1] = 0x00;
	mem[0x21 * 4 + 2] = (uint8_t)(seg & 0xff);
	mem[0x21 * 4 + 3] = (uint8_t)(seg >> 8);
}

/* CEMU_PC98_IPPROF=<file>: play ポンプ中に実行した線形 PC のヒストグラム。曲を載せて mute するドライバはほぼ待ち条件でスピンし、ホット番地が見る命令を示す。 */
namespace {

struct Pc98IpProf {
	enum { SLOTS = 4096 };
	unsigned addr[SLOTS];
	uint64_t hits[SLOTS];
	uint64_t total;
	const char* path;

	Pc98IpProf() : total(0), path(NULL)
	{
		memset(addr, 0xff, sizeof(addr));
		memset(hits, 0, sizeof(hits));
	}

	/* Note の実装 */
	void Note(unsigned lin)
	{
		total++;
		unsigned h = (lin * 2654435761u) & (SLOTS - 1);
		for (unsigned i = 0; i < 64; i++) {
			const unsigned s = (h + i) & (SLOTS - 1);
			if (addr[s] == 0xffffffffu) { addr[s] = lin; hits[s] = 1; return; }
			if (addr[s] == lin) { hits[s]++; return; }
		}
	}

	~Pc98IpProf() { Dump(); }

	/* 静的解体はコアを埋め込む全ホストで走る保証が無いので、ポンプ所有者が Close で明示フラッシュ。 */
	void Dump()
	{
		if (!path || !total) return;
		FILE* f = NULL;
		if (fopen_s(&f, path, "a") != 0 || !f) return;
		fprintf(f, "IPPROF total=%llu\n", (unsigned long long)total);
		for (int rank = 0; rank < 24; rank++) {
			int best = -1;
			for (int s = 0; s < SLOTS; s++)
				if (hits[s] && (best < 0 || hits[s] > hits[best])) best = s;
			if (best < 0) break;
			fprintf(f, "  %2d %05X %10llu %5.1f%%\n", rank, addr[best],
				(unsigned long long)hits[best],
				100.0 * (double)hits[best] / (double)total);
			hits[best] = 0;
		}
		fclose(f);
		total = 0;
	}
};

/* CEMU_PC98_MEMDUMP="<linhex>,<len>,<path>": ポンプが最後に戻ったときのゲストメモリ hex。プロファイラが見た待ちループを読むため。 */
void Pc98MemDump(const uint8_t* mem)
{
	static const char* spec = NULL;
	static int checked = 0;
	if (!checked) { checked = 1; spec = getenv("CEMU_PC98_MEMDUMP"); }
	if (!spec || !spec[0] || !mem) return;
	unsigned lin = 0, len = 0;
	char path[260];
	if (sscanf_s(spec, "%x,%u,%259s", &lin, &len, path, (unsigned)sizeof(path)) != 3)
		return;
	if (len > 0x1000 || lin + len >= 0x200000u) return;
	FILE* f = NULL;
	if (fopen_s(&f, path, "w") != 0 || !f) return;
	for (unsigned i = 0; i < len; i += 16) {
		fprintf(f, "%05X ", lin + i);
		for (unsigned j = 0; j < 16 && i + j < len; j++)
			fprintf(f, "%02X ", mem[lin + i + j]);
		fputc('\n', f);
	}
	fclose(f);
}

/* CEMU_PC98_IVT=<path>: play を poke するときゲストが実際に持つベクタと、これから poke するベクタ。API が一度も撃たないベクタにあるドライバは他が健全でも無音。 */
struct Pc98CensusPair { uint16_t a; uint8_t d; };

struct Pc98CensusCounts {
	unsigned wr, keyOn, tlLive, fnum, timer, irq, pit;
	unsigned line, svc, noVec, masked, ifOff;
	const Pc98CensusPair* tail;
	unsigned tailN;
};

/* アサートした OPN IRQ がゲストに届かなかった理由。掃引では同時に生きる機械は 1 台なのでファイル静的で足り、ヘッダを増やさない。 */
unsigned g_censLine = 0;   /* チップ Irq() がアサートを観測 */
unsigned g_censSvc = 0;    /* …だが opnInService_ はまだラッチ */
unsigned g_censNoVec = 0;  /* …だがベクタに何もフックされていない */
unsigned g_censMasked = 0; /* …だが PIC がその線をマスクしていた */
unsigned g_censIfOff = 0;  /* …だが CPU が割り込み禁止だった */

/* ゲストが OPN レジスタ 0x27 へ書いた最後の値。bit2/3 がタイマ A/B を許可。両方クリアならシーケンサ時計はオフで、再武装まで何も鳴らない。 */
uint8_t g_lastTimerCtrl = 0;
uint8_t g_lastTimerB = 0xE8;
static uint64_t s_fmpOpnIrqCyc;
static int s_fmpIrqLock;
static int s_fmpIrqPend;
static unsigned s_fmpTbSyncIrq;
static uint16_t s_fmpIrqSs, s_fmpIrqSp, s_fmpIrqCs, s_fmpIrqIp;

void Pc98IvtCensus(const uint8_t* mem, int funcVect, const Pc98CensusCounts& c,
	const char* phase, const wchar_t* tag)
{
	static const char* path = NULL;
	static int checked = 0;
	if (!checked) { checked = 1; path = getenv("CEMU_PC98_IVT"); }
	if (!path || !path[0] || !mem) return;
	FILE* f = NULL;
	if (fopen_s(&f, path, "a") != 0 || !f) return;
	fprintf(f, "IVT %-4s funcvect=%02X wr=%u key=%u tl=%u fnum=%u tmr=%u"
		" irq=%u pit=%u line=%u svc=%u novec=%u mask=%u ifoff=%u hooked=",
		phase, funcVect, c.wr, c.keyOn, c.tlLive, c.fnum, c.timer, c.irq,
		c.pit, c.line, c.svc, c.noVec, c.masked, c.ifOff);
	for (unsigned v = 0; v < 256; v++) {
		const unsigned off = (unsigned)mem[v * 4] | ((unsigned)mem[v * 4 + 1] << 8);
		const unsigned seg = (unsigned)mem[v * 4 + 2] | ((unsigned)mem[v * 4 + 3] << 8);
		if ((seg == 0 && off == 0) || seg == DOS98_TRAMP_SEG) continue;
		fprintf(f, "%02X=%04X:%04X,", v, seg, off);
	}
	/* %ls は C ロケールにマルチバイトが無いワイド文字で fprintf 全体を中断し、日本語タイトル毎に改行を静かに飲み込んだ。手で ASCII に折り、レコードが必ず終端するようにする。 */
	fputs(" name=", f);
	for (const wchar_t* p = tag; p && *p; p++)
		fputc((*p >= 0x20 && *p < 0x7f) ? (char)*p : '?', f);
	fputc('\n', f);
	if (c.tail) {
		fprintf(f, "TAIL %-4s", phase);
		for (unsigned i = 0; i < c.tailN; i++)
			fprintf(f, " %02X:%02X", c.tail[i].a, c.tail[i].d);
		fputc('\n', f);
	}
	fclose(f);
}

/* メンバは private。メソッド追加はプローブリンクの全オブジェクト再ビルドを強いるので、スナップショットは呼出側で読む。 */
#define PC98_CENSUS(phase) do { \
	Pc98CensusCounts c__; \
	c__.wr = opnWriteCount_; c__.keyOn = opnKeyOnCount_; \
	c__.tlLive = opnTlLiveCount_; c__.fnum = opnFnumCount_; \
	c__.timer = opnTimerCount_; c__.irq = opnIrqDeliverCount_; \
	c__.pit = pitTickCount_; \
	c__.line = g_censLine; c__.svc = g_censSvc; c__.noVec = g_censNoVec; \
	c__.masked = g_censMasked; c__.ifOff = g_censIfOff; \
	Pc98CensusPair t__[64]; \
	c__.tailN = opnTailCount_ < 64 ? opnTailCount_ : 64; \
	for (unsigned i__ = 0; i__ < c__.tailN; i__++) { \
		const unsigned s__ = (opnTailCount_ >= 64) \
			? ((opnTailCount_ + i__) % 64) : i__; \
		t__[i__].a = opnTailAddr_[s__]; t__[i__].d = opnTailData_[s__]; \
	} \
	c__.tail = t__; \
	Pc98IvtCensus(np2_mem(), funcVect_, c__, (phase), \
		dosGe_ ? dosGe_->name : NULL); \
} while (0)

/* 初回利用で解決し、以降このポインタから直読: フックは命令毎経路なので、プロファイリングオフ時はヌル判定 1 回のコストに抑える。 */
Pc98IpProf* g_ipProf = NULL;

/* IpProfInit の実装 */
void IpProfInit()
{
	const char* p = getenv("CEMU_PC98_IPPROF");
	if (!p || !p[0]) return;
	static Pc98IpProf inst;
	inst.path = p;
	g_ipProf = &inst;
}

} /* namespace */

/* カタログ <rom type="binary">00 a0 00 00</rom> は名前に hex を埋め込む — zip メンバは無い。type="string" は生 ASCII。 */
static int Pc98ParseInlineRom(const char* name, uint8_t* out, int outCap)
{
	if (!name || !out || outCap <= 0) return 0;
	int n = 0;
	int haveHex = 0;
	for (const char* p = name; *p && n < outCap;) {
		while (*p == ' ' || *p == '\t' || *p == ',' || *p == ':') p++;
		if (!*p) break;
		const char c0 = p[0];
		const char c1 = p[1];
		int hi = -1, lo = -1;
		if (c0 >= '0' && c0 <= '9') hi = c0 - '0';
		else if (c0 >= 'a' && c0 <= 'f') hi = c0 - 'a' + 10;
		else if (c0 >= 'A' && c0 <= 'F') hi = c0 - 'A' + 10;
		if (c1 >= '0' && c1 <= '9') lo = c1 - '0';
		else if (c1 >= 'a' && c1 <= 'f') lo = c1 - 'a' + 10;
		else if (c1 >= 'A' && c1 <= 'F') lo = c1 - 'A' + 10;
		if (hi < 0 || lo < 0) {
			/* hex ではない — 名前全体を ASCII 文字列ペイロードとして扱う */
			if (haveHex) break;
			n = 0;
			for (const char* q = name; *q && n < outCap; q++)
				out[n++] = (uint8_t)*q;
			return n;
		}
		out[n++] = (uint8_t)((hi << 4) | lo);
		haveHex = 1;
		p += 2;
	}
	return n;
}

enum {
	PC98_CPU_HZ = 8000000,
	PC98_OPN_CLOCK_HZ = 3993600,
	PC98_OPNA_CLOCK_HZ = 7987200,
	PC98_PIT_CLOCK_HZ = 1996800,
	/* 5 MHz 機の PIT（14.7456 MHz / 6）。FMD は BIOS 501 bit7 で
	   4160 リロードを選び、2.4576e6/4160 ≈ 591 Hz → 0x4B0 が ~60Hz 曲。 */
	PC98_PIT_CLOCK_5MHZ_HZ = 2457600,
	PC98_OPN_IRQ_VEC = 0x0B,
	PC98_TIMER_VEC = 0x08,
	/* BIOS タイマハンドラがこれをチェイン。tick だけ欲しいドライバは IRQ0 を奪わずここにフック。 */
	PC98_USER_TICK_VEC = 0x1C,
	/* 低 RAM BIOS tick ISR。IVT08 を DOS トランポリン（HLT;IRET）に置けない: DeliverIrqs はそのセグメントを未フックと見て IRQ0 を落とす。F000 は高 ROM として拒否。00C0 は 00A0:0100 のアイドルパケットより上、DOS アリーナ 1000 より下。 */
	PC98_BIOS_TICK_SEG = 0x00C0,
	PC98_VSYNC_VEC = 0x0A,
	OPN_ADDR0 = 0x188,
	OPN_DATA0 = 0x18A,
	OPN_ADDR1 = 0x18C,
	OPN_DATA1 = 0x18E,
	EXT_CMD = 0x07E0,
	EXT_SONG = 0x07E2,
	EXT_PARAM = 0x07E4,
	EXT_STATE = 0x07E8,
	HOST_CMD = 0x07D0,
	HOST_P1 = 0x07D2,
	HOST_P2 = 0x07D4,
	HOST_P3 = 0x07D6,
	SOUND86_ID = 0xA460,
	SOUND86_FIFO_STAT = 0xA466,
	SOUND86_FIFO_CTL = 0xA468,
	SOUND86_DAC_CTL = 0xA46A,
	SOUND86_FIFO_DAT = 0xA46C,
	SOUND86_MUTE = 0xA66E,
	PIT_CT0 = 0x71,
	PIT_CT1 = 0x73,
	PIT_CT2 = 0x75,
	PIT_CTRL = 0x77,
	PPI_A = 0x31,
	PPI_B = 0x33,
	PPI_C = 0x35,
	PPI_CTRL = 0x37,
	PIC_CMD = 0x00,
	PIC_MASK = 0x02,
	SLAVE_PIC_CMD = 0x08,
	SLAVE_PIC_MASK = 0x0A,
	VSYNC_ACK = 0x64,
	IO_DELAY = 0x5F,
	WOLF_SYNC0 = 0xE0D0,
	WOLF_SYNC1 = 0xE0D2
};

/* 後期 PC-9801 の PIT デコード 3FD9–3FDF（奇数）は 71/73/75/77 の別名。DOSBox-X と radioc.dat。BGML_98 はスピーカ分周を 3FDBh に書き 73h を触らない。これが無いと PPI ゲートが DC のまま。 */
static uint16_t Pc98FoldPitAlias(uint16_t port)
{
	if ((port & 0xfff8u) == 0x3fd8u && (port & 1u))
		return (uint16_t)(0x71u + (unsigned)(port - 0x3fd9u));
	return port;
}

/* SSCP 既定 YM は DX=0088/0288。188h 86ボードへ畳む。 */
static uint16_t Pc98FoldOpnAlias(uint16_t port)
{
	switch (port) {
	case 0x0088: case 0x0288: return (uint16_t)OPN_ADDR0;
	case 0x008A: case 0x028A: return (uint16_t)OPN_DATA0;
	case 0x008C: case 0x028C: return (uint16_t)OPN_ADDR1;
	case 0x008E: case 0x028E: return (uint16_t)OPN_DATA1;
	default: return port;
	}
}

/* Falcom FMD98 は MPU を C0D2（+DH*4）と 80D2/81D2 で探る。E0D0 は Wolf MUSDRV と共有するので畳まない。 */
static uint16_t Pc98FoldMpuAlias(uint16_t port)
{
	const unsigned hi = (unsigned)port >> 8;
	const unsigned lo = (unsigned)port & 0xffu;
	if (lo == 0xD2u && hi >= 0xC0u && hi <= 0xDCu && (hi & 3u) == 0u)
		return 0xC0D2;
	if (lo == 0xD0u && hi >= 0xC0u && hi <= 0xDCu && (hi & 3u) == 0u)
		return 0xC0D0;
	if (lo >= 0xD0u && lo < 0xE0u && (lo & 1u) == 0u) {
		if (hi == 0x80u)
			return 0xC0D0;
		if (hi == 0x81u)
			return 0xC0D2;
	}
	return port;
}

/* ymfm busy は generate() が進める。CPU IN 中の AdvanceClocks はタイマだけなので bit7 が sticky。
   FMD は IN 188h / SHL / JC で SSG 0xBF 指紋を取り、失敗すると [17CE] bit1-2 が立たず INT08 が FM を飛ばす。 */
static uint8_t Pc98OpnStatusClearBusy(uint8_t s)
{
	return (uint8_t)(s & (uint8_t)~0x80);
}

static CHardPc98* g_pc98Active = NULL;
static int g_pc98Eoi = 0;

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

/* Pc98Out8 の実装 */
static void Pc98Out8(unsigned port, unsigned char val)
{
	CHardPc98* hw = g_pc98Active;
	if (hw) hw->PortOut((uint16_t)port, (uint8_t)val);
}

/* Pc98In8 の実装 */
static unsigned char Pc98In8(unsigned port)
{
	CHardPc98* hw = g_pc98Active;
	if (!hw) return 0xff;
	return hw->PortIn((uint16_t)port);
}

/* SOUND ORCHESTRA の OPL 側は OPN 行を共有せず 9 本のモニタ行を持つ。s_opnaLayout を OPN レイアウトに保つと影は FM 行を残し、これらを追加チャネルとして付ける。 */
void CHardPc98::SorchTrackOplWrite(uint8_t reg, uint8_t data)
{
	sorchOplRegs_[reg] = data;
	if (reg < 0xA0 || reg > 0xB8) return;
	const int ch = (int)(reg & 0x0F);
	if (ch > 8) return;
	const uint8_t b = sorchOplRegs_[0xB0 + ch];
	if (!(b & 0x20)) {
		if (sorchOplOn_[ch]) {
			sorchOplOn_[ch] = 0;
			FmMonShadowPcmNote(ch, -1, 0);
		}
		return;
	}
	const unsigned fnum = (unsigned)sorchOplRegs_[0xA0 + ch]
		| ((unsigned)(b & 0x03) << 8);
	if (!fnum) return;
	const unsigned block = (unsigned)((b >> 2) & 0x07);
	/* OPL2 ピッチ: fnum * clock / (72 * 2^(20 - block)) */
	const double hz = (double)fnum * 3579545.0
		/ (72.0 * (double)(1u << (20u - block)));
	const int midi = FmMonShadowHzToMidi(hz);
	if (midi < 0) return;
	sorchOplOn_[ch] = 1;
	FmMonShadowPcmNote(ch, midi, 1);
}

CHardPc98::CHardPc98()
	: opnaMode(0)
	, modeSorch_(0)
	, opl_(NULL)
	, cpuHz_(PC98_CPU_HZ)
	, opnHz_(PC98_OPN_CLOCK_HZ)
	, bootCs_(0)
	, bootIp_(0)
	, funcVect_(0x7f)
	, dataAddr_(0)
	, fileSize_(0)
	, data2Addr_(0)
	, file2Size_(0)
	, addressing_(0)
	, isDos_(0)
	, nopnDrv_(0)
	, dofmd_(0)
	, fmd98_(0)
	, fmdSongOff_(0)
	, rx98_(0)
	, rxSongOff_(0)
	, prog98_(0)
	, progSongAddr_(0)
	, bst398_(0)
	, koei98_(0)
	, cal98_(0)
	, madp98_(0)
	, n3golf98_(0)
	, dks98_(0)
	, mdplay98_(0)
	, packCmd1_(0)
	, musicComKeepalive_(0)
	, synthIfKeepalive_(0)
	, modeMidi_(0)
	, fmpSeq_(0)
	, fmpTScaleN_(1)
	, fmpTScaleD_(1)
	, midiCapArmed_(0)
	, sound86Mask_(0x00) /* MAME リセット: ID=0x40。OPNA 拡張はソフトが bit0 を立てる */
	, sound86FifoCtl_(0)
	, sound86DacCtl_(0)
	, sound86Mute_(0)
	, wolfteam98_(0)
	, wolfMiSeg_(0)
	, wolfSyncRun_(0)
	, wolfGateStop_(0x5B48)
	, wolfGatePlay_(0x5B5A)
	, wolfSongPtr_(0x5B5D)
	, wolfSongBuf_(0x7E5E)
	, wolfTitleWord_(0x643A)
	, wolfFlagA_(0x0662)
	, wstimer_(0)
	, dummySndRom_(0)
	, pc88VaIo_(0)
	, sorcGlue_(0)
	, olteusMapSeg_(0)
	, olteusDataSeg_(0)
	, olteusTimerOn_(0)
	, olteusTrampOk_(0)
	, olteusIrqPulse_(0)
	, olteusInTick_(0)
	, olteusTimerResidual_(0)
	, olteusTickGuard_(0)
	, vaPc88PortHits_(0)
	, vaPc98PortHits_(0)
	, vaPc88LatchedAddr_(0)
	, vaPc88LatchedAddrHi_(0)
	, cpuCycles_(0)
	, extCmd_(0)
	, extSong_(0)
	, extParam_(0)
	, stubState_(0)
	, picMask_(0xff)
	, slavePicMask_(0xff)
	, opnInService_(0)
	, irqEdgeSeen_(0)
	, irqEdgeConsumed_(0)
	, chip_(NULL)
	, sampleRate_(44100)
	, active_(0)
	, pitClockHz_(PC98_PIT_CLOCK_HZ)
	, pitReload_(0)
	, pitCounter_(0)
	, pitLatch_(0)
	, pitLatched_(0)
	, pitResidual_(0)
	, pitIrqPending_(0)
	, pitWriteHi_(0)
	, pitReadHi_(0)
	, pitRunning_(0)
	, pit1Reload_(0)
	, pit1Counter_(0)
	, pit1WriteHi_(0)
	, pit1ReadHi_(0)
	, pit1Access_(3)
	, pit1Running_(0)
	, pit1Phase_(0)
	, pit1PhaseInc_(0)
	, ppiC_(0x08)
	, modeBeep_(0)
	, beepEventCount_(0)
	, beepMonOn_(0)
	, beepMonMidi_(-1)
	, vsyncResidual_(0)
	, vsyncPending_(0)
	, gdcA0Poll_(0)
	, opnPumpResidual_(0)
	, hostFunc_(0)
	, hostParam1_(0)
	, hostParam2_(0)
	, hostParam3_(0)
	, hostStatus_(0)
	, opnWriteCount_(0)
	, opnKeyOnCount_(0)
	, opnTlLiveCount_(0)
	, opnFnumCount_(0)
	, opnTimerCount_(0)
	, opnIrqDeliverCount_(0)
	, opnKeyOnCh_()
	, pitTickCount_(0)
	, timerIrqCount_(0)
	, lastSongLoadOk_(0)
	, lastSongLoadBytes_(0)
	, opnLogCount_(0)
	, opnTailCount_(0)
	, opnLatchedAddr_(0)
	, ssgPortAJumper_(0)
	, ssgEcho_()
	, opnLatchedAddrHi_(0)
	, wolfCmdLogCount_(0)
	, wolfCmdWriteCount_(0)
	, wolfBridgeEnable_(0)
	, wolfNoteOnCount_(0)
	, wolfNoteOffCount_(0)
	, wolfCtrlCount_(0)
	, wolfRunStatus_(0)
	, wolfDataIdx_(0)
	, wolfDataNeed_(0)
	, wolfInSysex_(0)
	, wolfVoiceCount_(3)
	, wolfVoiceClock_(0)
	, midiBytes_(NULL)
	, midiDelta_(NULL)
	, midiCount_(0)
	, midiNoteOnCount_(0)
	, midiPortOutCount_(0)
	, midiLastCycle_(0)
	, midiTickRem_(0)
	, mpuUart_(0)
	, mpuAckR_(0)
	, mpuAckW_(0)
	, mpuRx_(0)
	, mpuRxFull_(0)
	, mpuCmdByte_(0)
	, mpuTempo_(0x40)
	, mpuTimebase_(48)
	, mpuCthRate_(1)
	, mpuClockToHost_(0)
	, mpuWsdChan_(-1)
	, mpuCthResidual_(0)
	, mpuResetBusy_(0)
	, mpuResetUntil_(0)
	, dosStubReady_(0)
	, picMasterIcw_(0)
	, picSlaveIcw_(0)
	, picMasterIcw1_(0)
	, picSlaveIcw1_(0)
	, dosGe_(NULL)
	, np2Ram_(NULL)
	, np2HaveCpu_(0)
	, pmdOpnIrq_(0)
	, pmdPlayArmed_(0)
	, pumpAbortOnMusic_(0)
	, pumpSameLive_(0)
	, pumpPlayCode_(0xffffffffu)
	, pumpMusicKey0_(0)
	, pumpMusicMidi0_(0)
	, pumpMusicTimer0_(0)
	, pumpMusicCycle0_(0)
{
	hardKind = KIND_PC98;
	dosSong_[0] = 0;
	memset(bgmBank_, 0, sizeof(bgmBank_));
	memset(bgmBankSize_, 0, sizeof(bgmBankSize_));
	memset(bgm2Bank_, 0, sizeof(bgm2Bank_));
	memset(bgm2BankSize_, 0, sizeof(bgm2BankSize_));
	memset(opnLogAddr_, 0, sizeof(opnLogAddr_));
	memset(opnLogData_, 0, sizeof(opnLogData_));
	memset(wolfCmdLog_, 0, sizeof(wolfCmdLog_));
	memset(wolfData_, 0, sizeof(wolfData_));
	memset(wolfVoiceActive_, 0, sizeof(wolfVoiceActive_));
	memset(wolfVoiceMidiCh_, 0, sizeof(wolfVoiceMidiCh_));
	memset(wolfVoiceNote_, 0, sizeof(wolfVoiceNote_));
	memset(wolfVoiceAge_, 0, sizeof(wolfVoiceAge_));
	memset(wolfChVol_, 127, sizeof(wolfChVol_));
	memset(wolfChExpr_, 127, sizeof(wolfChExpr_));
	memset(mpuAckQ_, 0, sizeof(mpuAckQ_));
	mpuWsdChan_ = -1;
}

/* CHardPc98::ProfSample の実装 */
void CHardPc98::ProfSample()
{
	static int profInit = 0;
	if (!profInit) { profInit = 1; IpProfInit(); }
	const unsigned cs = np2_reg_get(NP2_R_CS);
	const unsigned ip = np2_reg_get(NP2_R_IP);
	const unsigned lin = (cs << 4) + ip;
	if (!g_ipProf) return;
	g_ipProf->Note(lin);
}

CHardPc98::~CHardPc98()
{
	Shutdown();
	if (g_ipProf)
		g_ipProf->Dump();
}

/* PCM／コードバンク */
void CHardPc98::FreeBanks()
{
	for (int i = 0; i < 256; i++) {
		if (bgmBank_[i]) { free(bgmBank_[i]); bgmBank_[i] = NULL; }
		if (bgm2Bank_[i]) { free(bgm2Bank_[i]); bgm2Bank_[i] = NULL; }
		bgmBankSize_[i] = 0;
		bgm2BankSize_[i] = 0;
	}
}

/* ゲストから見えるメモリ */
uint8_t* CHardPc98::Mem()
{
	if (CEmuNp2IsOwner(this)) {
		uint8_t* live = np2_mem();
		if (live)
			return live;
	}
	return np2Ram_ ? np2Ram_ : np2_mem();
}

/* NP2 RAM スナップショットを確保する */
int CHardPc98::EnsureNp2Ram()
{
	if (np2Ram_)
		return 1;
	np2Ram_ = (uint8_t*)malloc(CEMU_NP2_MEM_SIZE);
	if (!np2Ram_)
		return 0;
	memset(np2Ram_, 0, CEMU_NP2_MEM_SIZE);
	memset(np2Cpu_, 0, sizeof(np2Cpu_));
	np2HaveCpu_ = 0;
	return 1;
}

/* ライブ NP2 コアをこのハードへ切替する */
void CHardPc98::BindNp2()
{
	if (!EnsureNp2Ram())
		return;
	CEmuNp2Bind(this, np2Ram_, np2Cpu_, np2HaveCpu_);
}

/* FMP3 CS:A88 `CMP AH,0 / JZ` はボイス表に同じ nn があると MIDI NoteOn を出さない。
   FM 再トリガ抑制用。MIDI ドラムは同じ 36/38 でも毎回 NoteOn が要る。CEmu は
   Timer B 入れ子で表が汚れ、VG2_04 のキックが UART に乗らない（hoot では鳴る）。
   TGLFMP は INT21 AH=3F で zip の生 MGS を読むので曲バイトを書き換えない。 */
static void FmpPatchMidiRetrigger(uint8_t* mem)
{
	if (!mem) return;
	const unsigned sD2 = (unsigned)mem[0xD2 * 4 + 2]
		| ((unsigned)mem[0xD2 * 4 + 3] << 8);
	if (sD2 < 0x100u || sD2 >= 0xA000u) return;
	const unsigned b = sD2 << 4;
	if (b + 0xA8Cu >= 0x200000u) return;
	if (mem[b + 0xA88] == 0x80 && mem[b + 0xA89] == 0xFC
		&& mem[b + 0xA8A] == 0x00 && mem[b + 0xA8B] == 0x74
		&& mem[b + 0xA8C] == 0x3A) {
		mem[b + 0xA8B] = 0x90;
		mem[b + 0xA8C] = 0x90;
	}
	/* FMP MIDI は part14 の ch を [SI+1E8E]（SI=1C → 1EAA）に置く。
	   CC#127 ループが同じ 1EAA を ch0 マップとして 0 で潰し、VG2_04 の
	   キックが 99 24 ではなく 90 24 になる。hoot midiin1.mid は 99 24 70。
	   16AC（177C）と GS 初期化 507D（508F）の両方。MIDI バイトは残す。 */
	{
		unsigned i;
		for (i = 0x100u; i + 4u < 0x8000u && b + i + 4u < 0x200000u; i++) {
			if (mem[b + i] == 0x89 && mem[b + i + 1] == 0x8F
				&& mem[b + i + 2] == 0xAA && mem[b + i + 3] == 0x1E) {
				mem[b + i] = 0x90;
				mem[b + i + 1] = 0x90;
				mem[b + i + 2] = 0x90;
				mem[b + i + 3] = 0x90;
			}
		}
	}
}

/* CEmuPc98IsFmp の実装 */
static int CEmuPc98IsFmp(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->romCount; ++i) {
		const char* s = ge->rom[i].name;
		if (!s) continue;
		for (; *s; ++s)
			if (_strnicmp(s, "fmp", 3) == 0 || _strnicmp(s, "tglfmp", 6) == 0)
				return 1;
	}
	return 0;
}

/* CEmuPc98IsMusicCom の実装 */
static int CEmuPc98IsMusicCom(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	for (int i = 0; i < ge->romCount; ++i) {
		const char* s = ge->rom[i].name;
		if (!s) continue;
		const char* base = strrchr(s, '/');
		const char* back = strrchr(s, '\\');
		if (!base || (back && back > base)) base = back;
		base = base ? base + 1 : s;
		if (_stricmp(base, "MUSIC.COM") == 0 || _stricmp(base, "46.com") == 0)
			return 1;
	}
	return 0;
}

/* CEmuPc98NameLooksPmd の実装 */
static int CEmuPc98NameLooksPmd(const char* name)
{
	if (!name || !name[0]) return 0;
	const char* base = name;
	for (const char* p = name; *p; ++p) {
		if (*p == '/' || *p == '\\' || *p == ':')
			base = p + 1;
	}
	while (*base == '#' || *base == ' ' || *base == '\t')
		++base;
	if (_strnicmp(base, "PMD", 3) == 0)
		return 1;
	const char* n = name;
	while (*n == '#' || *n == ' ' || *n == '\t')
		++n;
	return _strnicmp(n, "PMD", 3) == 0;
}

/* CEmuPc98GeIsPmd の実装 */
static int CEmuPc98GeIsPmd(const CEmuGameEntry* ge)
{
	if (!ge) return 0;
	if (CEmuPc98NameLooksPmd(ge->archive))
		return 1;
	for (int i = 0; i < ge->romCount; i++) {
		if (CEmuPc98NameLooksPmd(ge->rom[i].name))
			return 1;
	}
	return 0;
}

/* CEmuPc98DosHasPmd の実装 */
static int CEmuPc98DosHasPmd(const CEmuDos98& dos)
{
	static const char* kNames[] = {
		"PMD.COM", "PMD_98.COM", "PMDB2.COM", "PMD86.COM", "PMD86B.COM",
		"PMDA.COM", "PMDB.COM", "PMDPPZ.COM", "PMDPPZE.COM", "PMD86L.COM",
		NULL
	};
	for (int i = 0; kNames[i]; i++) {
		if (dos.FindFile(kNames[i]))
			return 1;
	}
	return 0;
}

/* チップと CPU を生成する */
int CHardPc98::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	/* type=86 は PC-9801-86 基板だが、カタログ 86 リップの多く（flixmix / kolin2）は A460 無しで YM2203 として既に PLAYS。全体で入れると無音になった。要 YM2608 + ID 0x40 なのは emit_* FMDRV86（さもなくば PIC マスク／keyOn=0）。 */
	opnaMode = (_stricmp(ge->subtype, "opna") == 0) ? 1 : 0;
	if (!opnaMode && ge->subtype
		&& (_stricmp(ge->subtype, "86") == 0
			|| _stricmp(ge->subtype, "86+otomix2") == 0)) {
		if (ge->archive && _strnicmp(ge->archive, "emit", 4) == 0)
			opnaMode = 1;
		for (int i = 0; i < ge->romCount && !opnaMode; i++) {
			const char* n = ge->rom[i].name;
			if (!n) continue;
			for (const char* p = n; *p; p++)
				if (*p == '\\' || *p == '/' || *p == ':')
					n = p + 1;
			if (_strnicmp(n, "FMDRV86", 7) == 0)
				opnaMode = 1;
		}
	}
	/* SOUND ORCHESTRA は 26K クローンなので OPN 側は YM2203 のまま。基板を成すのは 0x18C/0x18E 対を共有する追加 OPL。 */
	if (_stricmp(ge->subtype, "soundorchestrav") == 0)
		modeSorch_ = 2;
	else if (_stricmp(ge->subtype, "soundorchestra") == 0)
		modeSorch_ = 1;
	else
		modeSorch_ = 0;
	opnHz_ = opnaMode ? PC98_OPNA_CLOCK_HZ : PC98_OPN_CLOCK_HZ;
	cpuHz_ = PC98_CPU_HZ;
	int clockmul = CEmuParseOptHex(ge, "clockmul", 0);
	if (clockmul <= 0) clockmul = CEmuParseOptHex(ge, "clock_mul", 1);
	if (clockmul < 1) clockmul = 1;
	/* hootrip は CPU を 8 MHz に保つ。clockmul は報告のみ。8 MHz のまま。 */
	(void)clockmul;

	bootCs_ = CEmuParseOptHex(ge, "bootcs", 0);
	bootIp_ = CEmuParseOptHex(ge, "bootip", 0);
	funcVect_ = CEmuParseOptHex(ge, "funcvect", 0x7f) & 0xff;
	dataAddr_ = CEmuParseOptHex(ge, "dataaddr", 0);
	dataAddrHost_ = 0;
	fileSize_ = CEmuParseOptHex(ge, "filesize", 0);
	/* Falcom SORC98 カタログは 0x1000 窓に十進 "1000" を使う */
	if (fileSize_ == 1000 && dataAddr_ == 0x3000)
		fileSize_ = 0x1000;
	data2Addr_ = CEmuParseOptHex(ge, "data2addr", 0);
	file2Size_ = CEmuParseOptHex(ge, "file2size", 0);
	addressing_ = CEmuParseOptHex(ge, "addressing", 0);
	if (addressing_ == 0)
		addressing_ = CEmuParseOptHex(ge, "adressing", 0);
	wstimer_ = CEmuParseOptHex(ge, "wstimer", 0);
	dummySndRom_ = CEmuParseOptHex(ge, "dummysndrom", 0);
	/* SORC98 v4–v10 は v1–v3 と同じブート stub だがカタログに wstimer が無い。無いと TriggerPlay が [085A] クリア／cmd0 再発行を飛ばしシーケンサが mute から出ない。
	   カタログは filesize を十進 "1000"（strtoul base0）で書くことが多く "0x1000" ではない — 両方許す。
	   PC-88VA SORCERIAN は同じ INT7F 糊で曲窓 @0x11800。 */
	if (wstimer_ <= 0 && bootIp_ == 0xf000 && funcVect_ == 0x7f
		&& (dataAddr_ == 0x3000 || dataAddr_ == 0x11800))
		wstimer_ = 1;
	sorcGlue_ = (wstimer_ > 0 && bootCs_ == 0 && bootIp_ != 0
		&& (dataAddr_ == 0x3000 || dataAddr_ == 0x11800)) ? 1 : 0;
	pc88VaIo_ = (_stricmp(ge->platform, "pc88va") == 0
		|| _stricmp(ge->platform, "pc88vados") == 0) ? 1 : 0;
	isDos_ = ((_stricmp(ge->platform, "pc98dos") == 0
		|| _stricmp(ge->platform, "pc9821") == 0
		|| _stricmp(ge->platform, "pc9821dos") == 0
		|| _stricmp(ge->platform, "pc88vados") == 0) && bootCs_ == 0) ? 1 : 0;
	pmdOpnIrq_ = CEmuPc98GeIsPmd(ge);
	pmdPlayArmed_ = 0;
	modeMidi_ = 0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "midiout") == 0) {
			modeMidi_ = 1;
			break;
		}
	}
	if (_stricmp(ge->subtype, "midiout") == 0 || _stricmp(ge->subtype, "midi") == 0)
		modeMidi_ = 1;
	modeBeep_ = (_stricmp(ge->subtype, "beep") == 0 && !modeMidi_) ? 1 : 0;
	mpuUart_ = 0;
	midiCapArmed_ = 0; /* BootDos シェルは 0→E0D0 を永久 OUT し得る。あとで武装 */
	/* pc98vx / bootcs の Falcom FMD は DOS シェルを通らない。midiout 行は
	   インテリジェント MPU のままキャプチャする（UART 3Fh を強制しない）。 */
	if (modeMidi_ && !isDos_)
		midiCapArmed_ = 1;
	MidiCaptureReset();

	const int isFmp = CEmuPc98IsFmp(ge);
	fmpSeq_ = isFmp ? 1 : 0;
	s_fmpIrqLock = 0;
	s_fmpIrqPend = 0;
	s_fmpOpnIrqCyc = 0;
	s_fmpTbSyncIrq = 0;
	/* FMP3 -m のカタログ行は type=opn（YM2203 4MHz）だが、実機 vg2 は
	   86 の YM2608 @ 8MHz で Timer B を組む。4MHz のまま TB 定数を食うと
	   周期が狂い、8/3 スケールでさらに歪む。MIDI でも 86 相当の OPNA にする。 */
	if (isFmp && modeMidi_ && !opnaMode) {
		opnaMode = 1;
		opnHz_ = PC98_OPNA_CLOCK_HZ;
	}
	chip_ = CEmuChipYm2608Create((uint32_t)opnHz_, opnaMode, sampleRate_);
	if (!chip_) return 0;
	memset(ssgEcho_, 0, sizeof(ssgEcho_));
	g_opnDataLatch = 0;
	/* 旧 OPNDRV は c2gp/dynamo98（md5 b5c63c42）と rolling（b7ccd822）。
	   27h/FFh DATA0 の全体エコーは bny の OPNA 指紋を動かした。バスホールドをゲート。 */
	g_opnBusHold = (ge && ge->archive
		&& (!_stricmp(ge->archive, "c2gp") || !_stricmp(ge->archive, "dynamo98")
			|| !_stricmp(ge->archive, "rolling")))
		? 1 : 0;
	g_mmdPicIsr = 0;
	g_opnIsrSs = 0;
	g_opnIsrSp = 0;
	g_pitInService = 0;
	g_pitIsrSs = 0;
	g_pitIsrSp = 0;
	g_mpuInService = 0;
	g_mpuIsrSs = 0;
	g_mpuIsrSp = 0;
	g_mpuIrqAsserted = 0;
	g_mmdClassic = 0;
	g_mmdLoadSeg = 0;
	g_mmdFmPlanted = 0;
	memset(g_mmdTrkBase, 0, sizeof(g_mmdTrkBase));
	g_mmdPlayAssist = 0;
	g_mmd2FnSrc = NULL;
	g_mmdKeyOn = 0;
	g_sddLoadSeg = 0;
	g_sddSongData = NULL;
	g_sddSongSize = 0;
	g_muse2Seg = 0;
	g_muse2Intr = 0;
	g_muse2SongData = NULL;
	g_muse2SongSize = 0;
	g_nmuseSeg = 0;
	/* YM3812 と Y8950 は PC-98 バス時計ではなく基板自身の 3.579545 MHz カラーバースト。V/VS/LS 変種は Y8950。FM 側は YM3812 とレジスタ互換なので音楽は鳴る — 8KB ADPCM チャネルだけまだ無い。 */
	if (modeSorch_) {
		opl_ = CEmuChipYm3812Create(3579545u, sampleRate_);
		memset(sorchOplRegs_, 0, sizeof(sorchOplRegs_));
		memset(sorchOplOn_, 0, sizeof(sorchOplOn_));
	}
	/* クロック動作はドライバ系統で選ぶ。MUSIC.COM は SOUND BIOS が YM2608 /2 を選ぶのに頼る。FMP と Falcom RX は自前タイマ定数を組みリセット /6 のまま。 */
	const int needBios = CEmuPc98IsMusicCom(ge);
	const int is46oku = (_stricmp(ge->archive, "46oku98") == 0);
	if (needBios) {
		chip_->Write(0, 0x2F);
		/* 2Fh の 3× プリスケール後にネイティブ合成レートを戻す。46oku の残るオクターブ補正は F-number ブロックシフトなので、エンベロープ／LFO 時間はピッチと一緒に遅くしない。 */
		chip_->SetPitchRateDiv(3u);
		if (is46oku) {
			chip_->SetPitchOctaveShift(-1);
			/* 46.COM はライブ MUSIC.COM [0290]/[0294] 状態が無いのにフェード量をキャリア TL へ繰り返し足す */
			chip_->SetCarrierFadeClamp(1);
		}
	} else if (isFmp) {
		/* YM2608 Timer B は ymfm の duration（16*(256-n)*OPERATORS*prescale）が
		   データシート 288*(256-n) µs と一致する。昔の generate() 二重クロックを
		   4/3・8/3 で相殺していたが、AdvanceClocks 専用にしたあとも残すと
		   密な曲（vg2 ::0001）が爆速、MIDI は逆に間延びする。ネイティブ 1:1。 */
		fmpTScaleN_ = 1u;
		fmpTScaleD_ = 1u;
		chip_->SetTimerClockScaleRatio(1u, 1u);
		chip_->SetCarrierFadeClamp(1);
	} else
		chip_->SetTimerClockScale(1u);

	if (!EnsureNp2Ram())
		return 0;
	{
		CEmuNp2Guard np2;
		BindNp2();
		np2_init();
		np2_reset();
		np2_setextsize(0);
		np2_set_adrsmask(0x000FFFFFu);
		/* PC-88VA CPU は V30。古典 PC-98 は i286 のまま。V30 パッチは bootcs VA プローブが安定してから任意 — i286 は Falcom SORC stub（SORC98 と同じ）を安定して走る。 */
		np2_set_v30(0);
		uint8_t* mem = np2_mem();
		if (mem) memset(mem, 0, 0x200000);
		np2HaveCpu_ = 1;
		np2_save_cpu(np2Cpu_, CEMU_NP2_CPU_SIZE);
		AttachIoHooks();
	}
	active_ = 1;
	return 1;
}

/* チップ／CPU／ROM を破棄する */
void CHardPc98::Shutdown()
{
	PC98_CENSUS("end");
	{
		CEmuNp2Guard np2;
		CEmuNp2Unbind(this);
		DetachIoHooks();
	}
	if (np2Ram_) {
		free(np2Ram_);
		np2Ram_ = NULL;
	}
	np2HaveCpu_ = 0;
	FreeBanks();
	if (chip_) { CEmuChipYm2608Destroy(chip_); chip_ = NULL; }
	if (opl_) { CEmuChipYm3812Destroy(opl_); opl_ = NULL; }
	delete[] midiBytes_; midiBytes_ = NULL;
	delete[] midiDelta_; midiDelta_ = NULL;
	midiCount_ = 0;
	active_ = 0;
	if (g_pc98Active == this) g_pc98Active = NULL;
}

/* I/O フックを NP2 へ接続する */
void CHardPc98::AttachIoHooks()
{
	g_pc98Active = this;
	hootrip_out8 = Pc98Out8;
	hootrip_inp8 = Pc98In8;
}

/* I/O フックを外す */
void CHardPc98::DetachIoHooks()
{
	if (g_pc98Active == this) {
		hootrip_out8 = NULL;
		hootrip_inp8 = NULL;
		g_pc98Active = NULL;
	}
}

/* バンク／BGM を載せる */
void CHardPc98::StageBanks(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge) return;
	FreeBanks();
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		if (_stricmp(r->type, "bgm") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			bgmBank_[idx] = (unsigned char*)malloc(sz);
			if (bgmBank_[idx]) {
				memcpy(bgmBank_[idx], data, sz);
				bgmBankSize_[idx] = sz;
			}
		} else if (_stricmp(r->type, "bgm2") == 0 && r->offset >= 0 && r->offset < 256) {
			const int idx = r->offset;
			bgm2Bank_[idx] = (unsigned char*)malloc(sz);
			if (bgm2Bank_[idx]) {
				memcpy(bgm2Bank_[idx], data, sz);
				bgm2BankSize_[idx] = sz;
			}
		} else if (_stricmp(r->type, "adpcm") == 0 && chip_ && r->offset >= 0) {
			chip_->SetAdpcmB(data, sz, (unsigned)r->offset);
		}
	}
}

/* データを載せる */
int CHardPc98::LoadSongToAddr(unsigned songNum, int destAddr, int maxSize, int isSecondary)
{
	if (destAddr < 0x600) return 0;
	unsigned char** banks = isSecondary ? bgm2Bank_ : bgmBank_;
	unsigned* sizes = isSecondary ? bgm2BankSize_ : bgmBankSize_;
	if (songNum > 255 || !banks[songNum]) {
		lastSongLoadOk_ = 0;
		lastSongLoadBytes_ = 0;
		return 0;
	}
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	unsigned n = sizes[songNum];
	if (maxSize > 0 && (unsigned)maxSize < n) n = (unsigned)maxSize;
	if ((unsigned)destAddr + n > 0x200000) {
		if ((unsigned)destAddr >= 0x200000) return 0;
		n = 0x200000u - (unsigned)destAddr;
	}
	memcpy(mem + destAddr, banks[songNum], n);
	/* BirdySoft ドライバ（MU 期と MF 期リロケ）は MF マジックを拒否または誤処理。ストリーム構造は MU（cal_98 __30 は文字以外 calr __30 と同じ）。全 cal98_ プリロードで MF→MU 正規化。 */
	if (cal98_ && n >= 2
		&& mem[destAddr] == 'M' && mem[destAddr + 1] == 'F')
		mem[destAddr + 1] = 'U';
	/* Wolfteam MS/MU `\x00B` ストリーム（hioden/suzaku）は `\x01B` 配置を共有するが type バイトが 0。01 に上げ MUSDRV が曲を受けるようにする。 */
	if (wolfteam98_ && n >= 2
		&& mem[destAddr] == 0x00 && mem[destAddr + 1] == 0x42)
		mem[destAddr] = 0x01;
	lastSongLoadOk_ = 1;
	lastSongLoadBytes_ = (int)n;
	return 1;
}

/* CHardPc98::HostService の実装 */
void CHardPc98::HostService(uint8_t func)
{
	hostStatus_ = 0xff;
	uint8_t* mem = np2_mem();
	if (!mem) return;
	/* DOFMD/BRANM 糊は 07D4/07D6 をリアルモード off/seg（SI/DS）。カタログ adressing=0 だと平坦 00FA11FBh になりバッファを外す。 */
	/* DKS/FQ stub は 07D4/07D6 で ES:BX をリアルモード曲／表ポインタ（DOFMD と同じ形）。平坦 (seg<<16)|off は 2MB を超え載らない。 */
	const int realModeDest = addressing_ || dofmd_ || dks98_;
	int dest = 0;
	if (realModeDest) {
		dest = ((int)hostParam3_ << 4) + (int)hostParam2_;
	} else {
		dest = ((int)hostParam3_ << 16) | (int)hostParam2_;
		if (dest == 0 && hostParam2_ == 0 && hostParam3_ == 0)
			dest = dataAddr_;
	}
	unsigned song = hostParam1_ & 0xff;
	switch (func) {
	case 0x20: /* 第 1 BGM ロード */
		if (LoadSongToAddr(song, dest > 0 ? dest : dataAddr_, fileSize_, 0))
			hostStatus_ = 0x00;
		break;
	case 0x21: /* 第 2 BGM ロード */
		if (LoadSongToAddr(song, dest > 0 ? dest : data2Addr_, file2Size_, 1))
			hostStatus_ = 0x00;
		break;
	case 0x10: /* リアルモード DS:BX（hostParam3:hostParam2）から dataaddr をセット */
		/* Ys/Ys2 Falcom 糊は OUT 07D4/07D6 のあと OUT 07D0,10h。カタログは dataaddr=0 — これが無いと TriggerPlay がプリロードせず cmd1 を飛ばす。 */
		{
			const int addr = ((int)hostParam3_ << 4) + (int)hostParam2_;
			/* FMD98 糊は INT42 の戻り AX を seg として 07D6 に出す。INT42 未植だと AX=0003 のまま
			   物理 0x30 になり、TriggerPlay が IVT/糊を曲データで潰す。 */
			if (addr >= 0x600 && addr < 0x200000) {
				dataAddr_ = addr;
				dataAddrHost_ = 1;
			}
			hostStatus_ = 0x00;
		}
		break;
	case 0x11:
		/* DOFMD_98.BIN / BRANM_98 再生経路: IN AX,07D4/07D6 → SI/DS をリアルモード曲ポインタ、続けて INT 45h で MSC/MV22/MUSIC.BIN へ。 */
		if (dofmd_) {
			hostParam2_ = (uint16_t)((unsigned)dataAddr_ & 0x000Fu);
			hostParam3_ = (uint16_t)((unsigned)dataAddr_ >> 4);
		} else {
			hostParam2_ = (uint16_t)(dataAddr_ & 0xffff);
			hostParam3_ = (uint16_t)((dataAddr_ >> 16) & 0xffff);
		}
		hostStatus_ = 0x00;
		break;
	default:
		hostStatus_ = 0xff;
		break;
	}
}

/* CHardPc98::BeepSetGateFromPpi の実装 */
void CHardPc98::BeepSetGateFromPpi()
{
	BeepMonUpdate();
}

/* CHardPc98::BeepMonUpdate の実装 */
void CHardPc98::BeepMonUpdate()
{
	const int gate = ((ppiC_ & 0x08) == 0) ? 1 : 0;
	double hz = 0;
	if (pit1Reload_ > 0 && pitClockHz_ > 0)
		hz = (double)pitClockHz_ / (double)pit1Reload_;
	int mid = (hz > 0) ? FmMonShadowHzToMidi(hz) : -1;
	if (mid < 0) {
		/* 1bit DAC / IRQ0 矩形: PPI bit3 が波形。PIT ch1 はアイドル。立ち上がり周期 → ピッチ。ゲート保持でもキーは見える。 */
		static uint64_t lastRise;
		static int prevGate = 0;
		if (gate && !prevGate && cpuHz_ > 0) {
			if (lastRise && cpuCycles_ > lastRise) {
				const double thz = (double)cpuHz_
					/ (double)(cpuCycles_ - lastRise);
				if (thz >= 20.0 && thz <= 8000.0)
					mid = FmMonShadowHzToMidi(thz);
			}
			lastRise = cpuCycles_;
		}
		prevGate = gate;
		if (mid < 0 && gate)
			mid = 60;
	}
	const int on = (gate && mid >= 0) ? 1 : 0;
	if (on)
		beepEventCount_++;
	if (!modeBeep_ && !s_valkyKeepIrq0)
		return;
	FmMonShadowWriteAuxReg(0x00, (unsigned)(pit1Reload_ & 0xff));
	FmMonShadowWriteAuxReg(0x01, (unsigned)(pit1Reload_ >> 8));
	FmMonShadowWriteAuxReg(0x02, (unsigned)ppiC_);
	FmMonShadowWriteAuxReg(0x03, on ? 1u : 0u);
	if (mid >= 0)
		FmMonShadowWriteAuxReg(0x04, (unsigned)mid);
	FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MIDI);
	if (on != beepMonOn_ || (on && mid != beepMonMidi_)) {
		if (beepMonOn_ && beepMonMidi_ >= 0)
			FmMonShadowMidiNote(0, beepMonMidi_, 0);
		if (on)
			FmMonShadowMidiNote(0, mid, 1);
		beepMonOn_ = on;
		beepMonMidi_ = on ? mid : -1;
	}
}

/* BEEP をステレオへ混成する */
void CHardPc98::MixBeep(int16_t* stereo, int frames)
{
	if (!stereo || frames <= 0) return;
	const int gate = ((ppiC_ & 0x08) == 0) ? 1 : 0;
	if (!gate && pit1PhaseInc_ == 0)
		return;
	for (int i = 0; i < frames; i++) {
		if (pit1PhaseInc_ > 0)
			pit1Phase_ += pit1PhaseInc_;
		if (!gate)
			continue;
		const int bit = pit1PhaseInc_ > 0 ? (int)((pit1Phase_ >> 31) & 1) : 1;
		const int16_t s = bit ? (int16_t)5000 : (int16_t)-5000;
		int32_t l = (int32_t)stereo[i * 2] + s;
		int32_t r = (int32_t)stereo[i * 2 + 1] + s;
		if (l > 32767) l = 32767; if (l < -32768) l = -32768;
		if (r > 32767) r = 32767; if (r < -32768) r = -32768;
		stereo[i * 2] = (int16_t)l;
		stereo[i * 2 + 1] = (int16_t)r;
	}
}

/* CHardPc98::BeepCommitPit1 の実装 */
void CHardPc98::BeepCommitPit1()
{
	pit1Counter_ = pit1Reload_ ? pit1Reload_ : 65536u;
	pit1Running_ = 1;
	if (pit1Reload_ > 0 && sampleRate_ > 0 && pitClockHz_ > 0) {
		const double hz = (double)pitClockHz_ / (double)pit1Reload_;
		pit1PhaseInc_ = (uint64_t)(hz * 4294967296.0 / (double)sampleRate_);
		if (pit1PhaseInc_ == 0) pit1PhaseInc_ = 1;
	} else {
		pit1PhaseInc_ = 0;
	}
	beepEventCount_++;
	BeepMonUpdate();
}

void CHardPc98::SscpForcePit(uint16_t reload)
{
	if (!reload)
		reload = 0x1900;
	pitWriteHi_ = 0;
	pitReload_ = reload;
	pitCounter_ = reload;
	pitRunning_ = 1;
	picMask_ = 0xFEu;
}

/* CHardPc98::PitOut の実装 */
void CHardPc98::PitOut(uint16_t port, uint8_t data)
{
	if (port == PIT_CTRL) {
		const int ch = (data >> 6) & 3;
		const int access = (data >> 4) & 3;
		/* RW=00 はカウンタラッチコマンドでありモード語ではない。読取用にカウントを凍らせ、モードと書きかけリロードは触らない。 */
		if (access == 0x00) {
			if (ch == 0) {
				uint16_t lat = (uint16_t)(pitCounter_ & 0xffff);
				/* FMX 3.10 は CS:[3B84] != 0 まで待ち NOT/MUL/DIV。0 ラッチ（終端カウントの IRQ）は 8s シェル予算までスピン。0xFFFF は NOT AX = 0 で後の DIV が INT 00。C-Class FMX は (FFFF−count) で割る。 */
				if (lat == 0) lat = 1;
				else if (lat == 0xFFFFu) lat = 0xFFFE;
				pitLatch_ = lat;
				pitLatched_ = 1;
				pitReadHi_ = 0;
			} else if (ch == 1) {
				pit1ReadHi_ = 0;
			}
			return;
		}
		if (ch == 0) {
			pitWriteHi_ = 0;
			pitReadHi_ = 0;
			pitLatched_ = 0;
		} else if (ch == 1) {
			pit1Access_ = access;
			pit1WriteHi_ = 0;
			pit1ReadHi_ = 0;
		}
		return;
	}
	if (port == PIT_CT0) {
		if (!pitWriteHi_) {
			pitReload_ = (pitReload_ & 0xff00) | data;
			pitWriteHi_ = 1;
		} else {
			pitReload_ = (pitReload_ & 0x00ff) | ((uint16_t)data << 8);
			pitWriteHi_ = 0;
			/* SSCP 初期化が PIT を 0/FFFF にすると BIOS 30Hz に落ち ED 01 の前に曲が終わる */
			if (s_valkyKeepIrq0 && ValkyIsSscp(np2_mem())
				&& (pitReload_ == 0 || pitReload_ > 0x2800u))
				pitReload_ = 0x1900;
			pitCounter_ = pitReload_ ? pitReload_ : 65536u;
			pitRunning_ = 1;
			pitIrqPending_ = 0;
		}
		return;
	}
	if (port == PIT_CT1) {
		if (pit1Access_ == 1) {
			pit1Reload_ = (uint16_t)((pit1Reload_ & 0xff00) | data);
			BeepCommitPit1();
		} else if (pit1Access_ == 2) {
			pit1Reload_ = (uint16_t)((pit1Reload_ & 0x00ff) | ((uint16_t)data << 8));
			BeepCommitPit1();
		} else if (!pit1WriteHi_) {
			pit1Reload_ = (uint16_t)((pit1Reload_ & 0xff00) | data);
			pit1WriteHi_ = 1;
		} else {
			pit1Reload_ = (uint16_t)((pit1Reload_ & 0x00ff) | ((uint16_t)data << 8));
			pit1WriteHi_ = 0;
			BeepCommitPit1();
		}
		if (s_valkyKeepIrq0 && (ppiC_ & 0x08)) {
			ppiC_ = (uint8_t)(ppiC_ & (uint8_t)~0x08);
			BeepSetGateFromPpi();
		}
	}
}

/* CHardPc98::PitIn の実装 */
uint8_t CHardPc98::PitIn(uint16_t port)
{
	if (port == PIT_CT1) {
		const uint16_t v = (uint16_t)(pit1Counter_ ? pit1Counter_ : pit1Reload_);
		if (!pit1ReadHi_) {
			pit1ReadHi_ = 1;
			return (uint8_t)(v & 0xff);
		}
		pit1ReadHi_ = 0;
		return (uint8_t)(v >> 8);
	}
	if (port != PIT_CT0) return 0xff;
	/* リロードではなくライブカウント: C-Class FMX は FFFF をロードし固定ループでスピン、ラッチして読み戻し、(FFFF − count) で割って CPU 速度定数を得る。リロードをエコーすると 0 になり、音を出す前にゼロ除算ループで死ぬ。 */
	const uint16_t raw = pitLatched_ ? pitLatch_ : (uint16_t)(pitCounter_ & 0xffff);
	uint16_t v = raw;
	if (v == 0) v = 1;
	else if (v == 0xFFFFu) v = 0xFFFE;
	if (!pitReadHi_) {
		pitReadHi_ = 1;
		return (uint8_t)(v & 0xff);
	}
	pitReadHi_ = 0;
	pitLatched_ = 0;
	return (uint8_t)(v >> 8);
}

/* CHardPc98::PitTick の実装 */
void CHardPc98::PitTick(uint64_t cpuCycles)
{
	if (!pitRunning_ || cpuHz_ <= 0) return;
	pitResidual_ += cpuCycles * (uint64_t)pitClockHz_;
	uint64_t ticks = pitResidual_ / (uint64_t)cpuHz_;
	pitResidual_ %= (uint64_t)cpuHz_;
	while (ticks > 0) {
		uint32_t step = pitCounter_;
		if (step == 0) step = 65536;
		if (ticks < step) {
			pitCounter_ = step - (uint32_t)ticks;
			ticks = 0;
		} else {
			ticks -= step;
			pitCounter_ = pitReload_ ? pitReload_ : 65536u;
			pitIrqPending_ = 1;
			pitTickCount_++;
		}
	}
}

/* PIT／VSYNC 等のサイドデバイスを進める */
void CHardPc98::TickSide(uint64_t cpuCycles)
{
	PitTick(cpuCycles);
	/* Do NOT clear [085A] bit7 here every CPU quantum — that path was
	   forcing SORC SSG3 (R0A) to stick at 0x0F (鳴りっぱなし) while FM
	   still advanced. Clear once per PIT music tick in DeliverIrqs. */
	if (cpuHz_ > 0) {
		vsyncResidual_ += cpuCycles * 60ull;
		if (vsyncResidual_ >= (uint64_t)cpuHz_) {
			vsyncResidual_ %= (uint64_t)cpuHz_;
			vsyncPending_ = 1;
		}
	}
	if (chip_ && cpuHz_ > 0 && opnHz_ > 0) {
		/* OPN クロックはドライバで別積算。ここでは IRQ 端を追う */
		int irq = chip_->Irq() ? 1 : 0;
		if (irq && !irqEdgeSeen_) {
			irqEdgeSeen_ = 1;
			irqEdgeConsumed_ = 0;
		} else if (!irq) {
			irqEdgeSeen_ = 0;
		}
	}
	if (mpuClockToHost_ && !mpuUart_ && !mpuResetBusy_ && cpuHz_ > 0) {
		unsigned hz = (unsigned)mpuTempo_ * (unsigned)mpuTimebase_;
		if (mpuCthRate_ > 1)
			hz /= (unsigned)mpuCthRate_;
		/* tempo * timebase は clocks/分（MPU C2=48 @ 120 BPM → 5760） */
		hz /= 60u;
		if (hz < 60u) hz = 60u;
		if (hz > 4000u) hz = 4000u;
		mpuCthResidual_ += cpuCycles * (uint64_t)hz;
		while (mpuCthResidual_ >= (uint64_t)cpuHz_) {
			/* コマンド ACK がまだキューにある間は CTH 枠を消費しない。MMD /I auto は B9h を OUT し CLI で FE を poll。次の空キュー tick は pending のまま、FD が短いプローブ（CX=1000）に 1/60s 後ではなく届くようにする。 */
			if (mpuAckR_ != mpuAckW_ || mpuRxFull_ || mpuResetBusy_)
				break;
			/* 旧 MMD CS:1C6 は CLI 下で bit0 を ROL poll（アイドル 80h は待たない）。ここで注入した CTH FD は IN で「FE ではない」になり、幻の FE が成功して /I auto が INT 0E をラッチしない。IF=1 まで残余を保持（植込 + STI）。 */
			if ((np2_reg_get(NP2_R_FLAGS) & 0x0200) == 0)
				break;
			mpuCthResidual_ -= (uint64_t)cpuHz_;
			MpuClockTick();
		}
	}
	/* olteus_va: ホストはハンドシェイク（≤0x11）だけで DS:[003C]/[CC4D] を進める。その先は IRQ0 → MAP:09BC で本物シーケンサがカウンタを所有。IRQ パルスは約 60Hz（VA ピクチャ tick）。600Hz は MAP:32DD の REP STOSW VRAM クリアを飢え曲ロードを止めた。 */
	if (olteusMapSeg_ && olteusDataSeg_ && olteusTimerOn_ && cpuHz_ > 0) {
		olteusTimerResidual_ += cpuCycles * 60ull;
		uint8_t* mem = np2_mem();
		const unsigned base = (unsigned)olteusDataSeg_ << 4;
		while (mem && base + 0xCC4Fu < 0x200000u
			&& olteusTimerResidual_ >= (uint64_t)cpuHz_) {
			olteusTimerResidual_ -= (uint64_t)cpuHz_;
			unsigned flag = (unsigned)mem[base + 0xCC4D]
				| ((unsigned)mem[base + 0xCC4D + 1] << 8);
			if (flag < 0x0011u) {
				/* ハンドシェイク: IRQ 枠あたりソフトステップを数回し、600 IRQ/秒無しでブートが早く 0x11 に届く */
				for (int step = 0; step < 10 && flag < 0x0011u; step++) {
					unsigned c = (unsigned)mem[base + 0x3C]
						| ((unsigned)mem[base + 0x3D] << 8);
					c = (c + 2u) & 0xffffu;
					mem[base + 0x3C] = (uint8_t)(c & 0xff);
					mem[base + 0x3D] = (uint8_t)((c >> 8) & 0xff);
					if (c == 0x00C8u) {
						flag = (flag + 1u) & 0xffffu;
						mem[base + 0xCC4D] = (uint8_t)(flag & 0xff);
						mem[base + 0xCC4D + 1] = (uint8_t)((flag >> 8) & 0xff);
						mem[base + 0x3C] = 0x64;
						mem[base + 0x3D] = 0x00;
					}
				}
			} else {
				/* ハンドシェイク完了 — シーケンサ tick を要求 */
				olteusIrqPulse_ = 1;
			}
		}
	}
	if (olteusDataSeg_) {
		uint8_t* mem = np2_mem();
		const unsigned base = (unsigned)olteusDataSeg_ << 4;
		if (mem && base + 0x5BCFu < 0x200000u) {
			/* play: CMP [5BCE],03E7 / JE spin — マジック待ち値を強制オフ */
			if (mem[base + 0x5BCE] == 0xE7 && mem[base + 0x5BCF] == 0x03) {
				mem[base + 0x5BCE] = 0x00;
				mem[base + 0x5BCF] = 0x00;
			}
			if (mem[base + 0x5BCA] == 0 && mem[base + 0x5BCB] == 0)
				mem[base + 0x5BCA] = 0x01;
		}
	}
}

/* 再生／タイマを武装する */
void CHardPc98::ArmOlteusVaTimer(uint16_t mapSeg)
{
	if (!mapSeg || mapSeg == (uint16_t)DOS98_TRAMP_SEG)
		return;
	olteusMapSeg_ = mapSeg;
	uint8_t* mem = np2_mem();
	if (!mem)
		return;
	/* MAP 入口: MOV AX,ss; MOV SS,AX; MOV AX,ds; MOV DS,AX — DS は CS+0x0F86 */
	const unsigned ent = (unsigned)mapSeg << 4;
	uint16_t dataSeg = (uint16_t)(mapSeg + 0x0F86u);
	if (ent + 10u < 0x200000u
		&& mem[ent] == 0xB8 && mem[ent + 3] == 0x8E && mem[ent + 4] == 0xD0
		&& mem[ent + 5] == 0xB8 && mem[ent + 8] == 0x8E && mem[ent + 9] == 0xD8) {
		dataSeg = (uint16_t)(mem[ent + 6] | ((unsigned)mem[ent + 7] << 8));
	}
	olteusDataSeg_ = dataSeg;
	/* MAP イメージ >64K。最初のパラグラフ内の BSS ゼロ走り（FE86）に植える。near CALL 09BC / IRET — 音楽 tick は near RET で終わる。 */
	const unsigned trampOff = 0xFE86u;
	const unsigned base = (unsigned)mapSeg << 4;
	const unsigned tramp = base + trampOff;
	if (tramp + 16u >= 0x200000u)
		return;
	if (!olteusTrampOk_) {
		/* PUSH ES; PUSHA; PUSH DS; MOV AX,dataSeg; MOV DS,AX; CALL 09BC; POP DS; POPA; POP ES; IRET。09BC は AX/CX/ES を壊す — 完全保存が無いと REP STOSW 途中の IRQ（MAP VRAM クリア @32C2）が終わらず曲ロードが走らない。 */
		mem[tramp + 0] = 0x06; /* PUSH ES */
		mem[tramp + 1] = 0x60; /* PUSHA */
		mem[tramp + 2] = 0x1E; /* PUSH DS */
		mem[tramp + 3] = 0xB8;
		mem[tramp + 4] = (uint8_t)(dataSeg & 0xff);
		mem[tramp + 5] = (uint8_t)(dataSeg >> 8);
		mem[tramp + 6] = 0x8E;
		mem[tramp + 7] = 0xD8;
		/* 変位 = 09BC - (FE86+8+3) = 09BC - FE91 = 0B2B */
		mem[tramp + 8] = 0xE8;
		mem[tramp + 9] = 0x2B;
		mem[tramp + 10] = 0x0B;
		mem[tramp + 11] = 0x1F; /* POP DS */
		mem[tramp + 12] = 0x61; /* POPA */
		mem[tramp + 13] = 0x07; /* POP ES */
		mem[tramp + 14] = 0xCF; /* IRET */
		mem[0x08 * 4 + 0] = (uint8_t)(trampOff & 0xff);
		mem[0x08 * 4 + 1] = (uint8_t)((trampOff >> 8) & 0xff);
		mem[0x08 * 4 + 2] = (uint8_t)(mapSeg & 0xff);
		mem[0x08 * 4 + 3] = (uint8_t)((mapSeg >> 8) & 0xff);
		olteusTrampOk_ = 1;
	} else {
		/* 何かが書き換えても IVT08 をトランポリンに残す */
		mem[tramp + 4] = (uint8_t)(dataSeg & 0xff);
		mem[tramp + 5] = (uint8_t)(dataSeg >> 8);
		mem[0x08 * 4 + 0] = (uint8_t)(trampOff & 0xff);
		mem[0x08 * 4 + 1] = (uint8_t)((trampOff >> 8) & 0xff);
		mem[0x08 * 4 + 2] = (uint8_t)(mapSeg & 0xff);
		mem[0x08 * 4 + 3] = (uint8_t)((mapSeg >> 8) & 0xff);
	}
	picMask_ = (uint8_t)(picMask_ & 0xfeu);
	np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
	/* OUT 10A の前からソフト IRQ0 を武装 — ブートはこの tick 経由で [CC4D] 待ち */
	olteusTimerOn_ = 1;
	/* MAP VRAM プレーンクリア（CS:32C2 REP STOSW ×4）を飛ばす。音声には不要。ホスト IRQ0 下では再生予算を食い終わらない。 */
	if (base + 0x32C2u < 0x200000u && mem[base + 0x32C2] == 0x8B)
		mem[base + 0x32C2] = 0xC3;
}

/* IvtHooked の実装 */
static int IvtHooked(uint8_t vec, int dosMode)
{
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	unsigned b = (unsigned)vec * 4u;
	uint16_t off = (uint16_t)(mem[b] | (mem[b + 1] << 8));
	uint16_t seg = (uint16_t)(mem[b + 2] | (mem[b + 3] << 8));
	/* ヌルベクタ。SORC98 は 0000:xxxx（CS=0）にハンドラを置く — それは有効。0000:0000 だけ拒否。壊れた INT 18 ブートが残す PC BIOS ROM（F000）と PC-98 高 ROM 別名（F800–FFFF）も拒否。 */
	if (seg == 0 && off == 0) return 0;
	if (seg >= 0xF000) return 0;
	if (dosMode && seg == DOS98_TRAMP_SEG) return 0;
	/* BIOS ワーク頁への DOS INT08 は PIT ISR ではない。0000:05xx をフック扱いすると IRQ0 が PIT レートで撃ち PMD の OPN Timer B を飢える。 */
	if (dosMode && vec == 0x08 && seg == 0 && off < 0x800)
		return 0;
	return 1;
}

/* ASCII MUSIC.COM v2.21（9230）は INT48 AH=07 を持つ。v2.00（8792）も AH=07 はあるが cmd0 経路が違う。 */
static int MusicComHasAh07(void)
{
	uint8_t* mem = np2_mem();
	if (!mem || !IvtHooked(0x48, 1))
		return 0;
	const unsigned s48 = (unsigned)mem[0x48 * 4 + 2]
		| ((unsigned)mem[0x48 * 4 + 3] << 8);
	const unsigned b48 = s48 << 4;
	if (!s48 || s48 == (unsigned)DOS98_TRAMP_SEG || b48 + 0x400u >= 0x200000u)
		return 0;
	for (unsigned o = 0; o + 3u < 0x400u; o++) {
		if (mem[b48 + o] == 0x80 && mem[b48 + o + 1] == 0xFC
			&& mem[b48 + o + 2] == 0x07)
			return 1;
	}
	return 0;
}

/* music_98.com INT21 フックは AH=3D/3E を全部ハンドル 0 成功にする。
   SOUND.DAT / INCLUDE が同じ MML を読んで翻訳が落ちるので、Open だけ本物へ通す。
   v2.21 の 17B4 はチャンネルごとに AH=3D し直す。偽ハンドル 0 だと 1 パス目で EOF
   まで読んだあと STR/チャンネルが空になる。 */
static void Music98PatchFakeOpen(uint8_t* mem, unsigned seg)
{
	if (!mem || !seg || seg == (unsigned)DOS98_TRAMP_SEG)
		return;
	const unsigned base = seg << 4;
	for (unsigned off = 0x100u; off + 5u < 0x1C0u; off++) {
		const unsigned p = base + off;
		if (p + 5u >= 0x200000u)
			break;
		if (mem[p] == 0x80 && mem[p + 1] == 0xFC && mem[p + 2] == 0x3D
			&& mem[p + 3] == 0x74) {
			mem[p + 3] = 0x90;
			mem[p + 4] = 0x90;
		}
	}
}

static void Music98AllowRealOpen(void)
{
	uint8_t* mem = np2_mem();
	if (!mem)
		return;
	if (IvtHooked(0x21, 1)) {
		const unsigned s21 = (unsigned)mem[0x21 * 4 + 2]
			| ((unsigned)mem[0x21 * 4 + 3] << 8);
		const unsigned o21 = (unsigned)mem[0x21 * 4]
			| ((unsigned)mem[0x21 * 4 + 1] << 8);
		Music98PatchFakeOpen(mem, s21);
		const unsigned p = (s21 << 4) + o21;
		if (p + 5u < 0x200000u
			&& mem[p] == 0x80 && mem[p + 1] == 0xFC && mem[p + 2] == 0x3D
			&& mem[p + 3] == 0x74) {
			mem[p + 3] = 0x90;
			mem[p + 4] = 0x90;
		}
	}
	if (IvtHooked(0x7F, 1)) {
		const unsigned s7f = (unsigned)mem[0x7F * 4 + 2]
			| ((unsigned)mem[0x7F * 4 + 3] << 8);
		Music98PatchFakeOpen(mem, s7f);
	}
}

static int MusicComIsV221(CEmuDos98& dos)
{
	const CEmuDos98File* f = dos.FindFile("MUSIC.COM");
	return (f && f->size == 9230u) ? 1 : 0;
}

/* `$T$` 系を大量に持つ v2.00（dante BGM01 は STR 39）はハンドル 0 一括読みが要る。
   TITLE は STR 0、ACCOR は 13、mgamescr BGM01/02 は STR 22/34 で本物 AH=3D が要る。
   閾値 38：dante の 39+ だけ偽ハンドル。スタブ BGM00 も翻訳せず残響を残す。 */
static int MusicComStrCount(const CEmuDos98File* f)
{
	if (!f || !f->data)
		return 0;
	int n = 0;
	const uint8_t* d = f->data;
	const unsigned sz = f->size;
	int line = 1;
	for (unsigned i = 0; i + 4u < sz; i++) {
		if (d[i] == '\n' || d[i] == '\r') {
			line = 1;
			continue;
		}
		if (!line)
			continue;
		if (d[i] == ' ' || d[i] == '\t')
			continue;
		line = 0;
		if ((d[i] == 'S' || d[i] == 's')
			&& (d[i + 1] == 'T' || d[i + 1] == 't')
			&& (d[i + 2] == 'R' || d[i + 2] == 'r')
			&& d[i + 3] == ':')
			n++;
	}
	return n;
}

/* v2.21 AH=07 の 0755 は AL=0（open+ch0）/1（ch1）/FE（SOUND）で STR 用 AL=FF が無い。
   AL=1 を FF に、続く FE を 01 に差し open→STR→ch1→ch2.. にする（MML の SOUND:@ は落とす。
   この系統は SOUND.DAT と STR 内 @n）。 */
static int MusicComPatchV221StrPass(uint8_t* mem)
{
	if (!mem || !IvtHooked(0x48, 1))
		return 0;
	const unsigned s48 = (unsigned)mem[0x48 * 4 + 2]
		| ((unsigned)mem[0x48 * 4 + 3] << 8);
	if (!s48 || s48 == (unsigned)DOS98_TRAMP_SEG)
		return 0;
	const unsigned b = s48 << 4;
	if (b + 0x800u >= 0x200000u)
		return 0;
	for (unsigned o = 0x740u; o + 18u < 0x7C0u; o++) {
		if (mem[b + o] != 0xB0 || mem[b + o + 1] != 0x00
			|| mem[b + o + 2] != 0xE8)
			continue;
		if (mem[b + o + 5] != 0xB0 || mem[b + o + 6] != 0x01
			|| mem[b + o + 7] != 0xE8)
			continue;
		if (mem[b + o + 10] != 0xB0 || mem[b + o + 11] != 0xFE
			|| mem[b + o + 12] != 0xE8)
			continue;
		mem[b + o + 6] = 0xFF;
		mem[b + o + 11] = 0x01;
		return 1;
	}
	return 0;
}

/* `{99` は v2.21 では翻訳時展開ではなく再生トークン（ISR 内で戻るオフセット 0000）になり、
   1 IRQ から戻らず opnInService が固まる。本体だけ残して `}` を外す。
   `{2`..`{32` の短いフレーズはそのまま。dante の `{0 $A0$` は触らない。 */
static void MusicComRewriteBigRepeat(CEmuDos98File* f)
{
	if (!f || !f->data || f->size < 4u)
		return;
	uint8_t* d = f->data;
	const unsigned n = f->size;
	for (unsigned i = 0; i + 2u < n; i++) {
		if (d[i] != '{')
			continue;
		unsigned j = i + 1;
		if (j < n && (d[j] == ' ' || d[j] == '\t'))
			j++;
		if (j >= n || d[j] < '0' || d[j] > '9')
			continue;
		unsigned k = j;
		unsigned val = 0;
		while (k < n && d[k] >= '0' && d[k] <= '9') {
			if (val > 9999u)
				break;
			val = val * 10u + (unsigned)(d[k] - '0');
			k++;
		}
		if (val < 50u || k <= j)
			continue;
		for (unsigned p = i; p < k; p++)
			d[p] = ' ';
		int nest = 1;
		unsigned p = k;
		while (p < n && nest > 0) {
			if (d[p] == '{')
				nest++;
			else if (d[p] == '}') {
				nest--;
				if (nest == 0)
					d[p] = ' ';
			}
			p++;
		}
		i = k - 1;
	}
}

/* v2.21 の `{ $A1$ }`（回数なし）はループにならず本体が落ちる。dante と同じ `{0` にする。 */
static void MusicComBareBraceToZero(CEmuDos98File* f)
{
	if (!f || !f->data || f->size < 4u || f->size > 200000u)
		return;
	uint8_t* d = f->data;
	unsigned n = f->size;
	unsigned extra = 0;
	for (unsigned i = 0; i + 1u < n; i++) {
		if (d[i] != '{')
			continue;
		unsigned j = i + 1;
		while (j < n && (d[j] == ' ' || d[j] == '\t'))
			j++;
		if (j < n && (d[j] < '0' || d[j] > '9'))
			extra++;
	}
	if (!extra)
		return;
	uint8_t* out = (uint8_t*)malloc(n + extra);
	if (!out)
		return;
	unsigned o = 0;
	for (unsigned i = 0; i < n; i++) {
		out[o++] = d[i];
		if (d[i] != '{')
			continue;
		unsigned j = i + 1;
		while (j < n && (d[j] == ' ' || d[j] == '\t'))
			j++;
		if (j < n && (d[j] < '0' || d[j] > '9'))
			out[o++] = '0';
	}
	uint8_t* nd = (uint8_t*)realloc(f->data, n + extra);
	if (!nd) {
		free(out);
		return;
	}
	f->data = nd;
	f->size = n + extra;
	memcpy(f->data, out, n + extra);
	free(out);
}

/* v2.21 の `{` / `{0` / `{n` は再生ループトークンになり ISR がノートを吐かない。
   展開後の本体だけ残す。フレーズが列挙されていれば 8s は持つ。 */
static void MusicComUnwrapAllBraces(CEmuDos98File* f)
{
	if (!f || !f->data || f->size < 2u)
		return;
	uint8_t* d = f->data;
	unsigned n = f->size;
	unsigned o = 0;
	int comment = 0;
	for (unsigned i = 0; i < n; i++) {
		if (d[i] == '\n' || d[i] == '\r')
			comment = 0;
		else if (!comment && d[i] == ';')
			comment = 1;
		if (comment) {
			d[o++] = d[i];
			continue;
		}
		if (d[i] == '{') {
			i++;
			while (i < n && (d[i] == ' ' || d[i] == '\t'
				|| (d[i] >= '0' && d[i] <= '9')))
				i++;
			i--;
			continue;
		}
		if (d[i] == '}')
			continue;
		d[o++] = d[i];
	}
	if (o < n)
		memset(d + o, 0x1A, n - o);
	f->size = o;
}

/* AH=07 中に INT14 が走ると翻訳バッファを踏む。INT48 入口は [flag]=1 が要るので
   フラグは立てたまま IRQ12 だけ閉じて翻訳する。 */
static int s_musicComCompileHold = 0;

static int MusicComMmlKind(const uint8_t* s, const uint8_t* e)
{
	while (s < e && (*s == ' ' || *s == '\t'))
		s++;
	if (s >= e)
		return 0;
	if (*s == ';' || *s == 0x1A)
		return 0;
	auto nicmp = [](const uint8_t* a, const char* b, int n) -> int {
		for (int i = 0; i < n; i++) {
			unsigned char ca = a[i], cb = (unsigned char)b[i];
			if (ca >= 'a' && ca <= 'z') ca = (unsigned char)(ca - 32);
			if (cb >= 'a' && cb <= 'z') cb = (unsigned char)(cb - 32);
			if (ca != cb)
				return 0;
		}
		return 1;
	};
	if (s + 4 <= e && nicmp(s, "STR:", 4))
		return 1;
	if (s + 5 <= e && nicmp(s, "SOUND", 5))
		return 1;
	if (s + 7 <= e && nicmp(s, "INCLUDE", 7))
		return 1;
	if (s + 4 <= e && nicmp(s, "LFO:", 4))
		return 1;
	if (s + 4 <= e && (nicmp(s, "OP1:", 4) || nicmp(s, "OP2:", 4)
		|| nicmp(s, "OP3:", 4) || nicmp(s, "OP4:", 4)))
		return 1;
	if (s + 2 <= e && s[0] >= '1' && s[0] <= '9' && s[1] == ':')
		return 2;
	return 0;
}

/* v2.21 は 1 パス。チャンネル行が STR:/SOUND: より先だと $A0$ が空のまま SOUND:@ だけ残る。 */
static void MusicComHoistMacros(CEmuDos98File* f)
{
	if (!f || !f->data || f->size < 16u || f->size > 200000u)
		return;
	const uint8_t* src = f->data;
	const unsigned n = f->size;
	enum { kMaxLines = 4096 };
	unsigned ls[kMaxLines], le[kMaxLines];
	int kind[kMaxLines];
	int nline = 0;
	unsigned i = 0;
	while (i < n && nline < kMaxLines) {
		unsigned a = i;
		while (i < n && src[i] != '\n' && src[i] != '\r')
			i++;
		unsigned b = i;
		if (i < n && src[i] == '\r')
			i++;
		if (i < n && src[i] == '\n')
			i++;
		ls[nline] = a;
		le[nline] = b;
		kind[nline] = MusicComMmlKind(src + a, src + b);
		nline++;
	}
	if (nline < 4)
		return;
	int firstCh = -1, macroAfter = 0;
	for (int k = 0; k < nline; k++) {
		if (kind[k] == 2 && firstCh < 0)
			firstCh = k;
		if (kind[k] == 1 && firstCh >= 0 && k > firstCh)
			macroAfter = 1;
	}
	if (!macroAfter || firstCh < 0)
		return;
	const unsigned cap = n + (unsigned)nline * 2u + 16u;
	uint8_t* out = (uint8_t*)malloc(cap);
	if (!out)
		return;
	unsigned o = 0;
	auto emit = [&](int k) {
		unsigned a = ls[k], b = le[k];
		unsigned len = b - a;
		if (o + len + 2u > cap)
			return;
		memcpy(out + o, src + a, len);
		o += len;
		out[o++] = '\r';
		out[o++] = '\n';
	};
	for (int k = 0; k < firstCh; k++) {
		if (kind[k] != 1)
			emit(k);
	}
	for (int k = 0; k < nline; k++) {
		if (kind[k] == 1)
			emit(k);
	}
	for (int k = firstCh; k < nline; k++) {
		if (kind[k] != 1)
			emit(k);
	}
	if (o > n) {
		uint8_t* nd = (uint8_t*)realloc(f->data, o);
		if (!nd) {
			free(out);
			return;
		}
		f->data = nd;
		f->size = o;
		memcpy(f->data, out, o);
	} else {
		memcpy(f->data, out, o);
		if (o < n)
			memset(f->data + o, 0x1A, n - o);
	}
	free(out);
}

/* v2.21 AH=07 の 0755 は AL=0/1/FE と ch2.. だけで、STR 用 AL=FF を呼ばない。
   `$A1$` が空のまま `{ $A1$ $A2$ }` だけ翻訳され 2 キーで止まる。定義をチャンネル行へ展開する。 */
static void MusicComExpandStr(CEmuDos98File* f)
{
	if (!f || !f->data || f->size < 8u || f->size > 200000u)
		return;
	enum { kMaxDef = 96, kName = 12 };
	char names[kMaxDef][kName];
	const uint8_t* bodies[kMaxDef];
	unsigned blen[kMaxDef];
	int ndef = 0;
	const uint8_t* s = f->data;
	const unsigned n = f->size;
	unsigned i = 0;
	while (i < n && ndef < kMaxDef) {
		unsigned a = i;
		while (i < n && s[i] != '\n' && s[i] != '\r')
			i++;
		unsigned b = i;
		if (i < n && s[i] == '\r')
			i++;
		if (i < n && s[i] == '\n')
			i++;
		const uint8_t* p = s + a;
		const uint8_t* e = s + b;
		while (p < e && (*p == ' ' || *p == '\t'))
			p++;
		if (p + 4 >= e)
			continue;
		if ((p[0] != 'S' && p[0] != 's') || (p[1] != 'T' && p[1] != 't')
			|| (p[2] != 'R' && p[2] != 'r') || p[3] != ':')
			continue;
		p += 4;
		char nm[kName];
		int ni = 0;
		while (p < e && *p != '=' && ni < kName - 1) {
			unsigned char c = *p++;
			if (c >= 'a' && c <= 'z')
				c = (unsigned char)(c - 32);
			if (c == '$')
				continue;
			nm[ni++] = (char)c;
		}
		nm[ni] = 0;
		if (p >= e || *p != '=' || ni <= 0)
			continue;
		p++;
		while (p < e && (*p == ' ' || *p == '\t'))
			p++;
		int dup = 0;
		for (int d = 0; d < ndef; d++) {
			if (strcmp(names[d], nm) == 0) {
				dup = 1;
				break;
			}
		}
		if (dup)
			continue;
		memcpy(names[ndef], nm, (unsigned)kName);
		bodies[ndef] = p;
		blen[ndef] = (unsigned)(e - p);
		ndef++;
	}
	if (ndef < 1)
		return;
	unsigned cap = n * 4u + 4096u;
	if (cap < n + 64u)
		cap = n + 64u;
	if (cap > 120000u)
		cap = 120000u;
	uint8_t* cur = (uint8_t*)malloc(n);
	if (!cur)
		return;
	memcpy(cur, s, n);
	unsigned clen = n;
	for (int round = 0; round < 24; round++) {
		uint8_t* nxt = (uint8_t*)malloc(cap);
		if (!nxt)
			break;
		unsigned o = 0;
		int hit = 0;
		for (unsigned p = 0; p < clen; p++) {
			if (cur[p] != '$') {
				if (o + 1u >= cap)
					break;
				nxt[o++] = cur[p];
				continue;
			}
			unsigned q = p + 1;
			while (q < clen && cur[q] != '$' && cur[q] != '\n' && cur[q] != '\r')
				q++;
			if (q >= clen || cur[q] != '$' || q <= p + 1) {
				if (o + 1u >= cap)
					break;
				nxt[o++] = cur[p];
				continue;
			}
			char nm[kName];
			int ni = 0;
			for (unsigned k = p + 1; k < q && ni < kName - 1; k++) {
				unsigned char c = cur[k];
				if (c >= 'a' && c <= 'z')
					c = (unsigned char)(c - 32);
				nm[ni++] = (char)c;
			}
			nm[ni] = 0;
			int found = -1;
			for (int d = 0; d < ndef; d++) {
				if (strcmp(names[d], nm) == 0) {
					found = d;
					break;
				}
			}
			if (found < 0) {
				if (o + 1u >= cap)
					break;
				nxt[o++] = cur[p];
				continue;
			}
			const uint8_t* bp = bodies[found];
			unsigned bl = blen[found];
			if (bl >= 2u && (bp[0] == 'V' || bp[0] == 'v') && bp[1] == '0') {
				unsigned k = 0;
				while (k < bl) {
					unsigned char c = bp[k];
					if ((c == 'O' || c == 'o') && k + 1u < bl
						&& bp[k + 1] >= '1' && bp[k + 1] <= '8')
						break;
					if (c == '@')
						break;
					if ((c == 'V' || c == 'v') && k + 1u < bl
						&& bp[k + 1] >= '1' && bp[k + 1] <= '9')
						break;
					k++;
				}
				bp += k;
				bl -= k;
			}
			unsigned k = 0;
			while (k < bl) {
				if ((bp[k] == 'I' || bp[k] == 'i')
					&& k + 1u < bl && bp[k + 1] >= '0' && bp[k + 1] <= '9') {
					k++;
					while (k < bl && ((bp[k] >= '0' && bp[k] <= '9') || bp[k] == ','))
						k++;
					continue;
				}
				if (o + 1u >= cap)
					break;
				nxt[o++] = bp[k++];
			}
			p = q;
			hit = 1;
		}
		free(cur);
		cur = nxt;
		clen = o;
		if (!hit)
			break;
	}
	if (clen > 0 && clen <= 120000u) {
		uint8_t* nd = (uint8_t*)realloc(f->data, clen);
		if (nd) {
			f->data = nd;
			f->size = clen;
			memcpy(f->data, cur, clen);
		}
	}
	free(cur);
}

/* MUSIC.COM は起動時に 188/18A へ 256 通り書いて IN 一致で [flag] を立てる。
   1 回でも不一致だと flag=0 のまま ISR が即帰り、AH=02 の Timer B 武装（3FE）も RET。
   v2.21 は CS:[2663]、v2.00 は CS:[239C]。INT14 ISR 先頭 `CMP BYTE CS:[flag],0` から取る。 */
static void MusicComForceOpnPresent(uint8_t* mem)
{
	if (!mem || !IvtHooked(0x14, 1))
		return;
	const unsigned s14 = (unsigned)mem[0x14 * 4 + 2]
		| ((unsigned)mem[0x14 * 4 + 3] << 8);
	const unsigned o14 = (unsigned)mem[0x14 * 4]
		| ((unsigned)mem[0x14 * 4 + 1] << 8);
	if (!s14 || s14 == (unsigned)DOS98_TRAMP_SEG)
		return;
	const unsigned lin = s14 << 4;
	const unsigned ip = lin + o14;
	if (ip + 6u >= 0x200000u)
		return;
	if (mem[ip] != 0x2E || mem[ip + 1] != 0x80 || mem[ip + 2] != 0x3E)
		return;
	const unsigned flag = (unsigned)mem[ip + 3] | ((unsigned)mem[ip + 4] << 8);
	if (flag < 0x2000u || flag > 0x4000u)
		return;
	if (lin + flag >= 0x200000u)
		return;
	mem[lin + flag] = 1;
}

/* Wolfteam F000 糊: INT7F は IN 7E0 の cmd1 → IN 7E2 AX / INT 4A。000_BOOT が INT7F を MOV AX;IRET（apros CS:28CE）へ差し替えると cmd1 が無音。糊ディスパッチへ戻す。 */
static void WolfReplantGlueInt7f(uint8_t* mem)
{
	if (!mem) return;
	for (unsigned p = 0xF000; p + 6u < 0xF100u; p++) {
		if (mem[p] == 0xBA && mem[p + 1] == 0xE0 && mem[p + 2] == 0x07
			&& mem[p + 3] == 0xEC && mem[p + 4] == 0x3C && mem[p + 5] == 0x00) {
			mem[0x7F * 4 + 0] = (uint8_t)(p & 0xff);
			mem[0x7F * 4 + 1] = (uint8_t)(p >> 8);
			mem[0x7F * 4 + 2] = 0x00;
			mem[0x7F * 4 + 3] = 0x00;
			return;
		}
	}
}

/* leftover 000_BOOT（hioden/apros 系）は INT 67（DS:SI=CS:32DE）を EMS に出す。d_98 には無い。ネイティブ IVT67=0000:0000 だと INT08/INT4A 植栽前にハングする。AH=0・CF=0・page frame D000 を返す。 */
static void WolfPlantEmmStub(uint8_t* mem)
{
	if (!mem) return;
	static const uint8_t kEmm[] = {
		0x55,                         /* push bp */
		0x89, 0xE5,                   /* mov  bp,sp */
		0x83, 0x66, 0x06, 0xFE,       /* and  word [bp+6],0FFFE */
		0x5D,                         /* pop  bp */
		0x30, 0xE4,                   /* xor  ah,ah */
		0xBB, 0x00, 0xD0,             /* mov  bx,0D000 */
		0xBA, 0x01, 0x00,             /* mov  dx,1 */
		0xB9, 0x00, 0x01,             /* mov  cx,0100 */
		0xCF                          /* iret */
	};
	memcpy(mem + 0x540, kEmm, sizeof(kEmm));
	mem[0x67 * 4 + 0] = 0x40;
	mem[0x67 * 4 + 1] = 0x05;
	mem[0x67 * 4 + 2] = 0x00;
	mem[0x67 * 4 + 3] = 0x00;
}

/* 000_BOOT が書いた C7 06 [0020]/[0038]/[0128] 即値を IVT へ。
   apros/zanyks はブートが INT08 まで届かず 00C0:0000 のまま。0000:0000 だけ見ると植えない。
   INT4A はハンドラ先頭がコードに見えるときだけ（偽 C7 06 で 5A09 を植えると IVT を踏み潰す）。 */
static int WolfIvtNeedPlant(const uint8_t* mem, unsigned vec)
{
	if (!mem) return 0;
	const unsigned b = vec * 4u;
	const unsigned off = (unsigned)mem[b] | ((unsigned)mem[b + 1] << 8);
	const unsigned seg = (unsigned)mem[b + 2] | ((unsigned)mem[b + 3] << 8);
	if (seg == 0 && off == 0)
		return 1;
	if (off == 0 && seg > 0 && seg < 0x200u)
		return 1;
	if (seg >= 0xF000u)
		return 1;
	return 0;
}

static int WolfLooksLikeIsr(const uint8_t* mem, unsigned off)
{
	if (!mem || off < 0x200u || off + 8u >= 0x11000u)
		return 0;
	const uint8_t a = mem[off];
	if (a == 0xE8 || a == 0x9C || a == 0xFA || a == 0xFB || a == 0x50
		|| a == 0x60 || a == 0x1E || a == 0xFC)
		return 1;
	return 0;
}

static void WolfPlantTimerIvts(uint8_t* mem)
{
	if (!mem) return;
	unsigned off08 = 0, off0e = 0, off4a = 0, off4c = 0;
	for (unsigned p = 0x600; p + 6u < 0xAE00u; p++) {
		if (mem[p] != 0xC7 || mem[p + 1] != 0x06)
			continue;
		const unsigned imm = (unsigned)mem[p + 2] | ((unsigned)mem[p + 3] << 8);
		const unsigned val = (unsigned)mem[p + 4] | ((unsigned)mem[p + 5] << 8);
		if (val < 0x200u || val >= 0xA000u)
			continue;
		if (imm == 0x0020 && !off08) off08 = val;
		else if (imm == 0x0038 && !off0e) off0e = val;
		else if (imm == 0x0128 && !off4a) off4a = val;
		else if (imm == 0x0130 && !off4c) off4c = val;
	}
	if (off08 && WolfIvtNeedPlant(mem, 0x08) && WolfLooksLikeIsr(mem, off08)) {
		mem[0x08 * 4 + 0] = (uint8_t)(off08 & 0xff);
		mem[0x08 * 4 + 1] = (uint8_t)(off08 >> 8);
		mem[0x08 * 4 + 2] = 0x00;
		mem[0x08 * 4 + 3] = 0x00;
	}
	if (off0e && WolfIvtNeedPlant(mem, 0x0E) && WolfLooksLikeIsr(mem, off0e)) {
		mem[0x0E * 4 + 0] = (uint8_t)(off0e & 0xff);
		mem[0x0E * 4 + 1] = (uint8_t)(off0e >> 8);
		mem[0x0E * 4 + 2] = 0x00;
		mem[0x0E * 4 + 3] = 0x00;
	}
	if (off4a && WolfIvtNeedPlant(mem, 0x4A) && WolfLooksLikeIsr(mem, off4a)) {
		mem[0x4A * 4 + 0] = (uint8_t)(off4a & 0xff);
		mem[0x4A * 4 + 1] = (uint8_t)(off4a >> 8);
		mem[0x4A * 4 + 2] = 0x00;
		mem[0x4A * 4 + 3] = 0x00;
	}
	if (off4c && WolfIvtNeedPlant(mem, 0x4C) && WolfLooksLikeIsr(mem, off4c)) {
		mem[0x4C * 4 + 0] = (uint8_t)(off4c & 0xff);
		mem[0x4C * 4 + 1] = (uint8_t)(off4c >> 8);
		mem[0x4C * 4 + 2] = 0x00;
		mem[0x4C * 4 + 3] = 0x00;
	}
}

/* 旧 MMD /I auto ISR（50 52 BA D2 E0…）は DSR で CS:[imm] をラッチするが IN E0D0 しない。ホストの CLI 尊重＋エッジ IRQ は CX=0 poll 窓を外し得る。INT 0E がその ISR でゲストが STI したらラッチを poke。 */
static void MmdAssistOldIrqProbe()
{
	if ((np2_reg_get(NP2_R_FLAGS) & 0x0200) == 0)
		return;
	uint8_t* mem = np2_mem();
	if (!mem) return;
	const unsigned o0e = (unsigned)mem[0x0E * 4]
		| ((unsigned)mem[0x0E * 4 + 1] << 8);
	const unsigned s0e = (unsigned)mem[0x0E * 4 + 2]
		| ((unsigned)mem[0x0E * 4 + 3] << 8);
	if (!s0e || s0e == (unsigned)DOS98_TRAMP_SEG || s0e >= 0xF000u)
		return;
	const unsigned lin = (s0e << 4) + o0e;
	if (lin + 24u >= 0x200000u) return;
	if (mem[lin] != 0x50 || mem[lin + 1] != 0x52
		|| mem[lin + 2] != 0xBA || mem[lin + 3] != 0xD2
		|| mem[lin + 4] != 0xE0)
		return;
	for (unsigned i = 0; i < 48u && lin + i + 5u < 0x200000u; i++) {
		if (mem[lin + i] == 0x2E && mem[lin + i + 1] == 0xC6
			&& mem[lin + i + 2] == 0x06 && mem[lin + i + 5] == 0x01) {
			const unsigned off = (unsigned)mem[lin + i + 3]
				| ((unsigned)mem[lin + i + 4] << 8);
			const unsigned a = (s0e << 4) + off;
			if (a < 0x200000u)
				mem[a] = 1;
			return;
		}
	}
}

/* BIOS／糊をメモリへ植える */
static void PlantFmdSongBank(CEmuDos98* dos)
{
	/* FMD CS:[0007] は TSR 隣の小さなヒープから始まる。AH=2 のあと AH=3F が .GS（最大約 30KB）をそこに読み、次の COM（fugam）を上書き。AH=1 は同じアリーナでトラックを XOR 復号（TLOVE_13 約 30KB）。 */
	uint8_t* mem = np2_mem();
	if (!dos || !mem || !IvtHooked(0xD3, 1))
		return;
	const unsigned cs = (unsigned)mem[0xD3 * 4 + 2]
		| ((unsigned)mem[0xD3 * 4 + 3] << 8);
	if (!cs || cs == (unsigned)DOS98_TRAMP_SEG)
		return;
	const unsigned b = cs << 4;
	if (b + 9u >= 0x200000u)
		return;
	uint16_t seg = 0;
	static const uint16_t kTry[] = { 0x1000, 0x0C00, 0x0A00, 0x0800, 0x0600 };
	for (unsigned t = 0; t < sizeof(kTry) / sizeof(kTry[0]); t++) {
		if (dos->AllocBlock(mem, kTry[t], &seg) && seg)
			break;
		seg = 0;
	}
	if (!seg)
		return;
	mem[b + 7] = (uint8_t)(seg & 0xff);
	mem[b + 8] = (uint8_t)(seg >> 8);
}

/* CHardPc98::Int60Hooked の実装 */
int CHardPc98::Int60Hooked() const
{
	return IvtHooked(0x60, isDos_);
}

/* 期限の IRQ／NMI を届ける */
int CHardPc98::DeliverIrqs()
{
	if (g_pc98Eoi) {
		g_pc98Eoi = 0;
		/* MMD2 は YM（27h=2Ah）を ack してから PIC EOI。レベルトリガ ymfm は IRET 前に再アサート。opnInService_ をラッチしたままにすると AH=3 の STI 待ちと HLT アイドルが飢える（michael pick 1 は line/svc が数千万、irq 凍結）。 */
		/* SDD ISR はスレーブ EOI のあと IRET。HLT パークの SS:SP が ISR 入口と違うと
		   ラッチが残り chip_->Irq() 中でも Deliver しない（ishido irq=1）。 */
		if (g_mmdPicIsr || g_sddLoadSeg)
			opnInService_ = 0;
		/* 他コアでは PIC EOI だけで opnInService_ を消さない。YM2608 IRQ はレベルトリガ。ISR がタイマ status（reg 0x27／status 読）を ack する前にここで消すと永久再入し PumpCycles がハング（pc88vados tetrisva/shinrava）。 */
	}
	/* 注入 IRQ フレームが INT 前スタックへ IRET したとき解放。線落下でこれをやり、status IN が YM を ack したあと OPNDRV（EOI 前 STI）が入れ子した。NOPNDRV は 0x27 ack 後にまだ IRET するので、巻き戻しはその経路もカバー。 */
	if (opnInService_) {
		const uint16_t ss = np2_reg_get(NP2_R_SS);
		const uint16_t sp = np2_reg_get(NP2_R_SP);
		/* 正確な SS:SP — SP>= ではない。OPNDRV の INT D2 と INT0B は両方 SS=CS。ISR の SP=24E4 は INT D2 の SP=0180 より上なので、SP>= は私有スタックを IRET 済みと見て約 20 万 IRQ/s 入れ子した。 */
		if (ss == g_opnIsrSs && sp == g_opnIsrSp)
			opnInService_ = 0;
	}
	if (s_fmpIrqLock) {
		const uint16_t ss = np2_reg_get(NP2_R_SS);
		const uint16_t sp = np2_reg_get(NP2_R_SP);
		const uint16_t cs = np2_reg_get(NP2_R_CS);
		const uint16_t ip = np2_reg_get(NP2_R_IP);
		/* SS:SP だけだと ISR が IRET 直前にスタックを戻した瞬間に
		   ロックが落ち、同じ ISR へ入れ子する。CS:IP も一致してから。 */
		if (ss == s_fmpIrqSs && sp == s_fmpIrqSp
			&& cs == s_fmpIrqCs && ip == s_fmpIrqIp)
			s_fmpIrqLock = 0;
		else if (s_fmpOpnIrqCyc && cpuHz_ > 0
			&& cpuCycles_ >= s_fmpOpnIrqCyc
			&& (cpuCycles_ - s_fmpOpnIrqCyc) > (uint64_t)cpuHz_)
			s_fmpIrqLock = 0;
		/* CS!=FMP では外さない。midiout は IRQ0 を生かすので、FMP が STI
		   したあと BIOS INT 08（CS=00C0）へ入る。そこでロックを落とすと
		   Timer B が入れ子し SI=0 のまま key_on する。VG2_04 のキック
		   36/38 が ch1 へ吸われ、A10 POWER が光らない（hoot midiin1.mid
		   では 99 24 70 が出る）。 */
	}
	/* FMP は 0x92/0x82 で [1e12] に Timer B を残す。チップ 26h が初期 C0 の
	   ままならシーケンサだけ速く、FM/MIDI とも間延びする。 */
	if (!s_fmpIrqLock && fmpSeq_ && chip_
		&& opnIrqDeliverCount_ != s_fmpTbSyncIrq) {
		s_fmpTbSyncIrq = opnIrqDeliverCount_;
		uint8_t* mem = np2_mem();
		if (mem) {
			const unsigned sD2 = (unsigned)mem[0xD2 * 4 + 2]
				| ((unsigned)mem[0xD2 * 4 + 3] << 8);
			const unsigned b = (unsigned)sD2 << 4;
			if (sD2 >= 0x100u && sD2 < 0xF000u && b + 0x2002u < 0x200000u
				&& mem[b + 0x2000] != 1) {
				const uint8_t want = mem[b + 0x1E12];
				if (want && want != g_lastTimerB) {
					chip_->Write(0, 0x26);
					chip_->Write(1, want);
					chip_->Write(0, 0x27);
					chip_->Write(1, 0x3F);
					opnLatchedAddr_ = 0x27;
					g_lastTimerB = want;
				}
			}
		}
	}
	if (g_pitInService) {
		const uint16_t ss = np2_reg_get(NP2_R_SS);
		const uint16_t sp = np2_reg_get(NP2_R_SP);
		if (ss == g_pitIsrSs && sp == g_pitIsrSp)
			g_pitInService = 0;
	}
	if (g_mpuInService) {
		const uint16_t ss = np2_reg_get(NP2_R_SS);
		const uint16_t sp = np2_reg_get(NP2_R_SP);
		if (ss == g_mpuIsrSs && sp == g_mpuIsrSp)
			g_mpuInService = 0;
	}

	/* MUSIC.COM 長 BGM keepalive: 再生許可 [0290]=1 の間、自動 mute カウンタ [0294] を 0 に保ち AH=2 の 16 小節 mute が飛ばないようにする。AH=1 が固着させるチャネル mute バイト [ch+3] もクリア。 */
	if (musicComKeepalive_ && IvtHooked(0x70, 1)) {
		uint8_t* mem = np2_mem();
		if (mem) {
			const unsigned s70 = (unsigned)mem[0x70 * 4 + 2]
				| ((unsigned)mem[0x70 * 4 + 3] << 8);
			const unsigned b70 = s70 << 4;
			if (b70 + 0x2A0u < 0x200000u && mem[b70 + 0x290] == 1) {
				mem[b70 + 0x294] = 0;
				/* チャネル制御ブロックは CS:0003/0013/… — bit0 が mute */
				for (unsigned ch = 0; ch < 6; ++ch) {
					const unsigned off = b70 + 0x03u + ch * 0x10u;
					if (off < 0x200000u && (mem[off] & 1))
						mem[off] = (uint8_t)(mem[off] & ~1u);
				}
			}
		}
	}
	/* 46oku / MUSIC.COM: keepalive 武装中で INT70 がパークベクタでないとき（fakecall ミラー）は INT14 CS も poke */
	if (musicComKeepalive_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			for (unsigned vec = 0x14; vec <= 0x14; ++vec) {
				const unsigned s = (unsigned)mem[vec * 4 + 2]
					| ((unsigned)mem[vec * 4 + 3] << 8);
				const unsigned b = s << 4;
				if (b + 0x2A0u >= 0x200000u) continue;
				if (mem[b + 0x290] != 1) continue;
				mem[b + 0x294] = 0;
				for (unsigned ch = 0; ch < 6; ++ch) {
					const unsigned off = b + 0x03u + ch * 0x10u;
					if (off < 0x200000u && (mem[off] & 1))
						mem[off] = (uint8_t)(mem[off] & ~1u);
				}
			}
		}
	}
	/* ASCII MUSIC.COM -r: シーケンサは INT14（IRQ12）。ゲスト 03C3 が IMR AND FA で IRQ3 を戻し、
	   flag=0 だと 3FE がスレーブを解除しない。毎 tick カスケード+IRQ12 を開ける。 */
	if (isDos_ && dosGe_) {
		static const char* kAsciiMus[] = { "music -r", "music_98", NULL };
		if (DosShellStarts(dosGe_, kAsciiMus) && !s_musicComCompileHold) {
			picMask_ = (uint8_t)(picMask_ & ~(1u << 2));
			slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
			if (IvtHooked(PC98_OPN_IRQ_VEC, 1))
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
	}
	uint16_t flags = np2_reg_get(NP2_R_FLAGS);
	const int guestIf = (flags & 0x0200) != 0;
	if ((modeBeep_ || modeMidi_ || s_valkyKeepIrq0) && pitIrqPending_ && !g_pitInService) {
		/* スピーカリップ（BGML_98）は IRQ0 でノートを組む。INT 7F 後はしばしば CLI 待ち。IF が無いと PIT が INT08 に届かず MixBeep は DC ゲート。実 BIOS はまだ IRQ0 を上げる。FMD intelligent ヘルパは MPU ACK 周りで CLI — ここで IF を強制すると INT 0E が FE を奪い 0713 がハングまたは RET 破壊（portOut=2）。 */
		/* FMP3 UART は Timer B。IRQ0 を開けると BIOS INT08（00C0:0000）が
		   FMP ロックを落とし TB が入れ子して MIDI が爆速になる。 */
		if (fmpSeq_ && mpuUart_)
			picMask_ = (uint8_t)(picMask_ | 0x01);
		else
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
		if (s_valkyKeepIrq0)
			ValkySscpKeepAlive(np2_mem());
		const int fmdIntel = modeMidi_ && !mpuUart_ && IvtHooked(0x0E, isDos_);
		/* FMP3 UART は Timer B。CLI を IF 強制で剥がすと STI 前に IRQ0 が
		   入り、ロック誤解除と組み合わさって MIDI パートの SI が壊れた。 */
		if (!fmdIntel && !(fmpSeq_ && mpuUart_)) {
			flags = (uint16_t)(flags | 0x0200);
			np2_reg_set(NP2_R_FLAGS, flags);
		}
	}
	/* 旧 MMD /I auto は STI、INT 21 AH=9 で印刷、INT 0E の CTH バイトを [1778] で poll。AH=9 は IF クリアで戻り得る。任意の INT 0E フックを FMD 扱いすると MPU IRQ を拒み /I auto が STC 失敗（int61）。FMD だけ INT 0E CS を INT D3 と共有。 */
	if (modeMidi_ && !mpuUart_ && IvtHooked(0x0E, isDos_)) {
		uint8_t* memIf = np2_mem();
		int fmdPair = 0;
		if (memIf) {
			const unsigned s0e = (unsigned)memIf[0x0E * 4 + 2]
				| ((unsigned)memIf[0x0E * 4 + 3] << 8);
			const unsigned sd3 = (unsigned)memIf[0xD3 * 4 + 2]
				| ((unsigned)memIf[0xD3 * 4 + 3] << 8);
			if (s0e && s0e == sd3 && s0e != (unsigned)DOS98_TRAMP_SEG)
				fmdPair = 1;
		}
		if (!fmdPair) {
			flags = (uint16_t)(flags | 0x0200);
			np2_reg_set(NP2_R_FLAGS, flags);
		}
	}
	if (synthIfKeepalive_ || (pmdOpnIrq_ && pmdPlayArmed_)) {
		flags = (uint16_t)(flags | 0x0200);
		np2_reg_set(NP2_R_FLAGS, flags);
	}
	if ((flags & 0x200) == 0) { /* IF クリア（割り込み禁止） */
		if (chip_ && chip_->Irq()) g_censIfOff++;
		return 0;
	}
	/* MMD2 ISR は STI しない。IF セットは IRET 済み。ymfm はまだレベル線を保持し得るので、旧ラッチは HLT アイドルを飢えた。 */
	if (g_mmdPicIsr)
		opnInService_ = 0;
	if (chip_ && chip_->Irq()) {
		g_censLine++;
		if (opnInService_) g_censSvc++;
	}

	if (packCmd1_ && IvtHooked(PC98_TIMER_VEC, isDos_))
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
	if (pitIrqPending_ && (picMask_ & 0x01) == 0 && !g_pitInService) {
		if (s_fmpIrqLock) {
			/* FMP ISR 中は IRQ0 を保留（捨てない）。INT 08 へ入ると CS が
			   00C0 になり、旧ロック解除が Timer B 入れ子を許していた。 */
			;
		} else if (pmdOpnIrq_ && pmdPlayArmed_) {
			pitIrqPending_ = 0;
		} else if (packCmd1_ && IvtHooked(0x14, isDos_)) {
			pitIrqPending_ = 0; /* vd SSG INT08 が OPN INT14 を飢えないように */
		} else if (IvtHooked(PC98_TIMER_VEC, isDos_)) {
			pitIrqPending_ = 0;
			timerIrqCount_++;
			g_pitInService = 1;
			g_pitIsrSs = np2_reg_get(NP2_R_SS);
			g_pitIsrSp = np2_reg_get(NP2_R_SP);
			np2_interrupt((uint8_t)PC98_TIMER_VEC);
			return 1;
		} else if (IvtHooked(PC98_USER_TICK_VEC, isDos_)) {
			pitIrqPending_ = 0;
			timerIrqCount_++;
			g_pitInService = 1;
			g_pitIsrSs = np2_reg_get(NP2_R_SS);
			g_pitIsrSp = np2_reg_get(NP2_R_SP);
			np2_interrupt((uint8_t)PC98_USER_TICK_VEC);
			return 1;
		}
	}
	MpuFinishReset();
	if (modeMidi_ && !mpuUart_ && guestIf)
		MmdAssistOldIrqProbe();
	/* FMD は MPU DSR ISR を INT 0E（IRQ6）に置く。コマンド ACK FE は CLI 下の CS:0713 が poll — FE で IRQ6 を上げるとバイトを奪う。Clock-to-host F8 は非要求で ISR を通る。 */
	if (modeMidi_ && !mpuUart_ && (picMask_ & 0x40) == 0 && !g_mpuInService
		&& guestIf && IvtHooked(0x0E, isDos_)) {
		int wantMpuIrq = 0;
		if (mpuRxFull_)
			wantMpuIrq = 1;
		else if (mpuAckR_ != mpuAckW_) {
			const uint8_t front = mpuAckQ_[mpuAckR_ & 31];
			/* FE は FMD CS:0713 が CLI 下で poll — IRQ が奪う。F8（clock）と FD（FMD シーケンサ tick）は割り込み必須。 */
			if (front != (uint8_t)0xfe)
				wantMpuIrq = 1;
		}
		if (wantMpuIrq) {
			/* エッジでありレベルではない: 旧 MMD プローブは CTH バイトを IN しないので、レベル線は 8s シェル予算まで INT 0E を入れ子する。 */
			if (g_mpuIrqAsserted)
				wantMpuIrq = 0;
			else
				g_mpuIrqAsserted = 1;
		} else {
			g_mpuIrqAsserted = 0;
		}
		if (wantMpuIrq) {
			g_mpuInService = 1;
			g_mpuIsrSs = np2_reg_get(NP2_R_SS);
			g_mpuIsrSp = np2_reg_get(NP2_R_SP);
			np2_interrupt(0x0E);
			return 1;
		}
	}
	if (vsyncPending_ && (picMask_ & 0x04) == 0 && IvtHooked(PC98_VSYNC_VEC, isDos_)) {
		vsyncPending_ = 0;
		np2_interrupt((uint8_t)PC98_VSYNC_VEC);
		return 1;
	}
	/* レベルトリガ OPN IRQ（PC88 と同じ）。エッジラッチだけだと前回 DeliverIrqs 後の TickOpn 中のアサートを外す。 */
	if (chip_ && chip_->Irq() && !opnInService_) {
		uint8_t* mem = np2_mem();
		/* famistava は再生中 OPN を INT14 に植える — INT0B が空のときだけミラー（rtype は INT0B に INT0A thunk を残す）。 */
		/* ゲスト OPN ISR が INT14（MDR / famistava / MUSE 系）: DeliverIrqs は INT0B を tick。0B がまだトランポリンならミラー。単独 IRET シリアル stub は飛ばす。 */
		if (mem && (pc88VaIo_ || isDos_ || packCmd1_) && IvtHooked(0x14, isDos_)
			&& !IvtHooked(PC98_OPN_IRQ_VEC, isDos_) && !pmdOpnIrq_
			&& !olteusMapSeg_) {
			const unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
			const unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
			const unsigned phys = (s14 << 4) + o14;
			if (phys < 0x200000u && mem[phys] != 0xCF
				&& !(s14 == 0 && o14 == 0x500)) {
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
			}
			picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
		/* mbmusp/MUSE: SSG I/O A = 0xC0 → ドライバが INT14 をフックしスレーブを EOI。そこで届ける（INT0B へミラーしない）。 */
		uint8_t vec = PC98_OPN_IRQ_VEC;
		/* PMD は IRQ3/INT0B（Timer B）を所有。INT14 は DOS/MUSE フック — そこに OPN を送ると誤 ISR が走り 30 IRQ のち無音。 */
		if (!pmdOpnIrq_ && !olteusMapSeg_) {
		if (IvtHooked(0x14, isDos_) && mem) {
			const unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
			const unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
			const unsigned phys = (s14 << 4) + o14;
			if (phys < 0x200000u && mem[phys] != 0xCF
				&& !(s14 == 0 && o14 == 0x500)
				&& s14 != DOS98_TRAMP_SEG) {
				vec = 0x14;
				picMask_ = (uint8_t)(picMask_ & ~(1u << 2));
				slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
			}
		}
		if ((ssgPortAJumper_ & 0xC0) == 0xC0 && IvtHooked(0x14, isDos_)) {
			vec = 0x14;
			picMask_ = (uint8_t)(picMask_ & ~(1u << 2)); /* cascade */
			slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4)); /* IRQ12 */
		}
		/* USMD オーバーレイは YM シーケンサを INT15（IRQ13）に植える。INT14 はマスタ PIC チェイン stub（IN AL,2 / far 旧 14）だけ。そこで届けると keyOn=0、opnInService がレベル線に固着。展開タイトルはオーバーレイが CS+0x470/471。PIYO+EXEPACK パックは INT7E と INT15 を別ロードコピーに残す（es95: +0x772）。 */
		if (mem && IvtHooked(0x15, isDos_) && IvtHooked(0x7E, isDos_)) {
			const unsigned o7e = (unsigned)mem[0x7E * 4]
				| ((unsigned)mem[0x7E * 4 + 1] << 8);
			const unsigned s7e = (unsigned)mem[0x7E * 4 + 2]
				| ((unsigned)mem[0x7E * 4 + 3] << 8);
			const unsigned o15 = (unsigned)mem[0x15 * 4]
				| ((unsigned)mem[0x15 * 4 + 1] << 8);
			const unsigned s15 = (unsigned)mem[0x15 * 4 + 2]
				| ((unsigned)mem[0x15 * 4 + 3] << 8);
			const unsigned isr7e = ((unsigned)s7e << 4) + o7e;
			static const uint8_t kUsmd7e[] = {
				0x51, 0x52, 0x53, 0x55, 0x56, 0x57, 0x06, 0x1E, 0xBB
			};
			int usmd7e = 0;
			if (s7e && o7e == 0x0005 && isr7e + sizeof(kUsmd7e) < 0x200000u
				&& memcmp(mem + isr7e, kUsmd7e, sizeof(kUsmd7e)) == 0)
				usmd7e = 1;
			if (usmd7e && s15) {
				const unsigned phys = ((unsigned)s15 << 4) + o15;
				int hasIn0A = 0;
				if (phys + 24u < 0x200000u && mem[phys] == 0x50) {
					for (unsigned k = 1; k < 24; k++) {
						if (mem[phys + k] == 0xE4 && mem[phys + k + 1] == 0x0A) {
							hasIn0A = 1;
							break;
						}
					}
				}
				if (hasIn0A) {
					vec = 0x15;
					picMask_ = (uint8_t)(picMask_ & ~(1u << 2));
					slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 5));
				}
			}
		}
		} /* !pmdOpnIrq_ — PMD は INT0B のまま */
		if (mem && g_mmdLoadSeg
			&& MmdIsSound(mem, (unsigned)g_mmdLoadSeg << 4)) {
			vec = PC98_OPN_IRQ_VEC;
			picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
		if (!IvtHooked(vec, isDos_)) {
			/* VSYNC（0x0A）や他 IRQ 線へフォールバックしない — OPN タイマ IRQ が SORC98 の VSYNC stub へ誤配送された。 */
			g_censNoVec++;
			return 0;
		}
		/* ゲスト ISR の OUT 02h はしばしば IRQ3 をまだマスクするブート時 IMR を戻す。PMD 系と INT14 ミラードライバには tick が要る。 */
		if (vec == PC98_OPN_IRQ_VEC)
			picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		if (IvtHooked(vec, isDos_)) {
			if (vec >= 0x08 && vec <= 0x0F && (picMask_ & (1 << (vec - 0x08))) != 0) {
				g_censMasked++;
				return 0;
			}
			if (vec >= 0x10 && vec <= 0x17
				&& (slavePicMask_ & (1 << (vec - 0x10))) != 0) {
				g_censMasked++;
				return 0;
			}
			/* FMP の密な曲は ISR 中に TB が再発火し、ack で inService が
			   落ちると入れ子で全パートが同時に進む。クロックは止めず、
			   ゲストが IRET するまで再配送しない。周期内の IRQ は捨てない
			   （先頭小節の tick 落ちと途中停止の原因だった）。 */
			if (fmpSeq_) {
				if (s_fmpIrqLock) {
					s_fmpIrqPend = 1;
					return 0;
				}
				s_fmpIrqLock = 1;
				s_fmpIrqPend = 0;
				s_fmpIrqSs = np2_reg_get(NP2_R_SS);
				s_fmpIrqSp = np2_reg_get(NP2_R_SP);
				s_fmpIrqCs = np2_reg_get(NP2_R_CS);
				s_fmpIrqIp = np2_reg_get(NP2_R_IP);
				s_fmpOpnIrqCyc = cpuCycles_;
			}
			opnInService_ = 1;
			g_opnIsrSs = np2_reg_get(NP2_R_SS);
			g_opnIsrSp = np2_reg_get(NP2_R_SP);
			irqEdgeConsumed_ = 1;
			opnIrqDeliverCount_++;
			np2_interrupt(vec);
			return 1;
		}
	}
	return 0;
}

/* --- PC-98 MPU-401 UART @ E0D0/E0D2（FMP3 -m / midiout カタログ） ----------- */

static uint8_t s_pc98MidiRun, s_pc98MidiNeed, s_pc98MidiD0;

/* CHardPc98::MidiCaptureReset の実装 */
void CHardPc98::MidiCaptureReset()
{
	if (!midiBytes_) midiBytes_ = new uint8_t[CEMU_PC98_MIDI_CAP];
	if (!midiDelta_) midiDelta_ = new uint32_t[CEMU_PC98_MIDI_CAP];
	midiCount_ = 0;
	midiNoteOnCount_ = 0;
	midiPortOutCount_ = 0;
	midiLastCycle_ = cpuCycles_;
	midiTickRem_ = 0;
	mpuRxFull_ = 0;
	/* ここでパワーオン FE をキューしない。FMP3 -m 検出（CS:1790）は E0D2 を IN し status != 0 かつ bit6 クリア（アイドル 0x80）が要る。pending ACK だと MidiStatusIn が 0x00 を返し検出失敗 → [1DBE]=0、シーケンサが MIDI を出さない（midiBytes=0）。FE は RESET（FFh）／UART モード（3Fh）コマンドハンドラからのみ。 */
	mpuAckR_ = mpuAckW_ = 0;
	g_mpuIrqAsserted = 0;
	s_fmpOpnIrqCyc = 0;
	s_fmpIrqLock = 0;
	s_fmpIrqPend = 0;
	s_fmpTbSyncIrq = 0;
	mpuResetBusy_ = 0;
	mpuResetUntil_ = 0;
	s_pc98MidiRun = s_pc98MidiNeed = s_pc98MidiD0 = 0;
	g_mpuTrI = g_mpuTrN = 0;
	g_mpuLastSt = 0xff;
}

/* CHardPc98::MidiPushAck の実装 */
void CHardPc98::MidiPushAck(uint8_t v)
{
	mpuAckQ_[mpuAckW_ & 31] = v;
	mpuAckW_++;
}

/* CHardPc98::MidiCaptureByte の実装 */
void CHardPc98::MidiCaptureByte(uint8_t v)
{
	if (!midiBytes_ || !midiDelta_) return;
	if (midiCount_ >= (unsigned)CEMU_PC98_MIDI_CAP) return;
	uint32_t delta = 0;
	if (cpuHz_ > 0) {
		const uint64_t dc = cpuCycles_ - midiLastCycle_;
		/* 960 ticks/sec。切り捨てだと Timer B 間隔が系統的に短く MIDI が走る。 */
		midiTickRem_ += dc * 960ull;
		uint64_t ticks = midiTickRem_ / ((uint64_t)cpuHz_ + 1ull);
		midiTickRem_ %= ((uint64_t)cpuHz_ + 1ull);
		/* SysEx ダンプの隙間だけ短くする。最初の NoteOn までの休符まで
		   250ms に潰すと MIDI 先頭が詰まる。 */
		if (midiNoteOnCount_ == 0 && (s_pc98MidiRun == 0xF0 || v == 0xF0)) {
			const uint64_t cap = 960ull / 4ull;
			if (ticks > cap) ticks = cap;
		}
		if (ticks > 0xffffffffull) ticks = 0xffffffffull;
		delta = (uint32_t)ticks;
	}
	midiLastCycle_ = cpuCycles_;
	midiDelta_[midiCount_] = delta;
	midiBytes_[midiCount_] = v;
	midiCount_++;

	/* SysEx（F0..F7）とリアルタイム（F8..FF）をランニングステータスにしてはいけない。GS バルクダンプが F0 武装のまま再生ノートが数えられない。 */
	if (v == 0xF0) {
		s_pc98MidiRun = 0xF0;
		s_pc98MidiNeed = 0;
		s_pc98MidiD0 = 0;
		return;
	}
	if (v == 0xF7) {
		s_pc98MidiRun = 0;
		s_pc98MidiNeed = 0;
		s_pc98MidiD0 = 0;
		return;
	}
	if (v >= 0xF8)
		return;
	/* SysEx 内データはチャネルボイスではない。新しい 80-EF ステータスは未終端 F0 を中断しなければならない（NARU / F7 を落とす GS ダンプ）。さもないとダンプ後のノートオンが数えられない。PC/AT MidiNoteOnCount と同じ。 */
	if (s_pc98MidiRun == 0xF0 && (v & 0x80) == 0)
		return;

	if (v & 0x80) {
		s_pc98MidiRun = v;
		const uint8_t hi = (uint8_t)(v & 0xf0);
		s_pc98MidiNeed = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
		s_pc98MidiD0 = 0;
	} else if (s_pc98MidiRun) {
		if (s_pc98MidiNeed == 2 && s_pc98MidiD0 == 0) {
			s_pc98MidiD0 = v;
		} else {
			const uint8_t hi = (uint8_t)(s_pc98MidiRun & 0xf0);
			const int ch = (int)(s_pc98MidiRun & 0x0f);
			if (hi == 0x90 || hi == 0x80) {
				const int note = (s_pc98MidiNeed == 2) ? (int)s_pc98MidiD0 : (int)v;
				const int vel = (s_pc98MidiNeed == 2) ? (int)v : 0;
				const int on = (hi == 0x90 && vel > 0) ? 1 : 0;
				if (on) midiNoteOnCount_++;
				FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MIDI);
				FmMonShadowMidiNote(ch, note, on);
				FmMonShadowWriteAuxReg(0x10 + (unsigned)(ch & 0x0f),
					(unsigned)(on ? (note & 0x7f) : 0));
			}
			s_pc98MidiD0 = 0;
			if ((s_pc98MidiRun & 0xf0) == 0xC0 || (s_pc98MidiRun & 0xf0) == 0xD0)
				s_pc98MidiNeed = 1;
			else
				s_pc98MidiNeed = 2;
		}
	}
}

/* CHardPc98::MidiDataOut の実装 */
void CHardPc98::MidiDataOut(uint8_t data)
{
	MpuTrace(0xE0D0, 1, data);
	midiPortOutCount_++;
	/* インテリジェント cmd E0/E7/… は次のデータポートバイトを MIDI ではなくペイロードにする。FMD 再生は `OUT E0` のあと `OUT [1798]` がテンポ。 */
	if (mpuCmdByte_ && !mpuUart_) {
		switch (mpuCmdByte_) {
		case 0xe0: mpuTempo_ = data; break;
		case 0xe7: mpuCthRate_ = (uint8_t)(data >> 2); if (!mpuCthRate_) mpuCthRate_ = 1; break;
		default: break;
		}
		mpuCmdByte_ = 0;
		return;
	}
	if (mpuWsdChan_ >= 0 && !mpuUart_) {
		if (data == 0xFC) {
			mpuWsdChan_ = -1;
			return;
		}
		if (midiCapArmed_ || (data & 0x80) || s_pc98MidiRun) {
			MidiCaptureByte(data);
			if (wolfBridgeEnable_ && midiCapArmed_)
				WolfCmdByte(data);
		}
		return;
	}
	/* FMP3 -m プローブは常駐インストール中に E0D0 へ OUT 00h をループし、最初の曲バイト前にキャプチャバッファをゼロで埋める。 */
	if (!midiCapArmed_)
		return;
	if (mpuUart_ || modeMidi_) {
		MidiCaptureByte(data);
		/* FMP3 -m は標準 MIDI UART ストリーム（Wolf MUSDRV と同じ）。OPN へブリッジし、プローブ／非 VST 経路でも可聴出力。 */
		if (wolfBridgeEnable_)
			WolfCmdByte(data);
	}
}

/* CHardPc98::MidiCmdOut の実装 */
void CHardPc98::MidiCmdOut(uint8_t data)
{
	MpuTrace(0xE0D2, 1, data);
	midiPortOutCount_++;
	if (data == 0xff) {
		mpuUart_ = 0;
		mpuCmdByte_ = 0;
		mpuClockToHost_ = 0;
		mpuWsdChan_ = -1;
		mpuAckR_ = mpuAckW_ = 0;
		g_mpuIrqAsserted = 0;
		/* FMD AH=0: OUT FFh のあと IN E0D2; AND 40h / JZ fail。RESET 後の最初の status 読は bit6 セットが要る。ACK FE はその poll の副作用でキューされ、CALL 0296 がドレインできる。 */
		mpuResetBusy_ = 1;
		return;
	}
	if (data == 0x3f) {
		/* Falcom FMD はインテリジェント MPU。3Fh を UART と見なすと
		   CTH/tempo（E0/E7）が死に MIDI が間延びする。 */
		if (fmd98_) {
			MidiPushAck(0xfe);
			return;
		}
		mpuUart_ = 1;
		mpuCmdByte_ = 0;
		mpuClockToHost_ = 0;
		MidiPushAck(0xfe);
		return;
	}
	if (mpuUart_) {
		MidiPushAck(0xfe);
		return;
	}
	/* MPU-401 インテリジェントファーム。FMD /# は 3Fh を送らない。E0/C2/E7/95 で話し、UART キャプチャではなく version/tempo 応答を期待。 */
	if (data >= 0xd0 && data <= 0xd7) {
		mpuWsdChan_ = (int)(data & 7);
		MidiPushAck(0xfe);
		return;
	}
	if (data == 0xdf) {
		mpuWsdChan_ = 0;
		MidiPushAck(0xfe);
		return;
	}
	switch (data) {
	case 0xac: /* バージョン要求 */
		MidiPushAck(0xfe);
		MidiPushAck(0x15);
		return;
	case 0xad: /* リビジョン要求 */
		MidiPushAck(0xfe);
		MidiPushAck(0x01);
		return;
	case 0xaf: /* テンポ要求 */
		MidiPushAck(0xfe);
		MidiPushAck(mpuTempo_ ? mpuTempo_ : (uint8_t)0x40);
		return;
	case 0xab:
		MidiPushAck(0xfe);
		MidiPushAck(0x00);
		return;
	case 0x94: mpuClockToHost_ = 0; break;
	case 0x95:
		mpuClockToHost_ = 1;
		mpuCthResidual_ = (cpuHz_ > 0) ? (uint64_t)cpuHz_ : 1ull;
		break;
	/* MMD.COM init は B9h で終わり、/I auto は INT 0E の CTH FD を待つ。94h はクロックを切った。これ無しだとプローブがラッチしない。残余を 1 周期分種まき、次の空キュー TickSide で最初の FD を出す（旧 MMD の CX=1000 poll は << 16ms）。 */
	case 0xb8:
	case 0xb9:
		mpuClockToHost_ = 1;
		mpuCthResidual_ = (cpuHz_ > 0) ? (uint64_t)cpuHz_ : 1ull;
		break;
	case 0xc2: mpuTimebase_ = 48; break;
	case 0xc3: mpuTimebase_ = 72; break;
	case 0xc4: mpuTimebase_ = 96; break;
	case 0xc5: mpuTimebase_ = 120; break;
	case 0xc6: mpuTimebase_ = 144; break;
	case 0xc7: mpuTimebase_ = 168; break;
	case 0xc8: mpuTimebase_ = 192; break;
	case 0xe0: case 0xe1: case 0xe2: case 0xe4: case 0xe6:
	case 0xe7: case 0xec: case 0xed: case 0xee: case 0xef:
		mpuCmdByte_ = data;
		break;
	case 0x0c: /* stop */
		mpuClockToHost_ = 0;
		mpuWsdChan_ = -1;
		break;
	default:
		break;
	}
	MidiPushAck(0xfe);
}

/* CHardPc98::MpuFinishReset の実装 */
void CHardPc98::MpuFinishReset()
{
	if (!mpuResetBusy_)
		return;
	/* 最初の status poll が消費 — MidiStatusIn 参照。TickSide は RESET を引退させてはいけない。FMD の OUT FFh 後 IN が ready を見て失敗する。 */
}

/* CHardPc98::MpuClockTick の実装 */
void CHardPc98::MpuClockTick()
{
	if (!mpuClockToHost_ || mpuUart_ || mpuResetBusy_)
		return;
	/* pending コマンド ACK を壊さない — FMD ヘルパは FEh 待ち */
	if (mpuAckR_ != mpuAckW_)
		return;
	/* FMD の INT 0E ISR は FD を特別扱い（CALL 0748 → 07D3 シーケンサ）。MMD.COM /I auto も同じ: プローブ ISR は FD のときだけ [18A9] をラッチするので、汎用 F8 は INT 61 未インストールのまま。ゲスト INT 0E MPU ISR はどれも FD を欲する。UART モードはここへ来ない。 */
	uint8_t clk = 0xf8;
	uint8_t* mem = np2_mem();
	if (mem) {
		const unsigned s0e = (unsigned)mem[0x0E * 4 + 2]
			| ((unsigned)mem[0x0E * 4 + 3] << 8);
		const unsigned sd3 = (unsigned)mem[0xD3 * 4 + 2]
			| ((unsigned)mem[0xD3 * 4 + 3] << 8);
		if (s0e && s0e != (unsigned)DOS98_TRAMP_SEG
			&& (s0e == sd3 || IvtHooked(0x0E, isDos_)))
			clk = 0xfd;
	}
	MidiPushAck(clk);
}

/* CHardPc98::MidiStatusIn の実装 */
uint8_t CHardPc98::MidiStatusIn()
{
	/* 標準 MPU-401 status（Roland / RBIL / FMP3）: bit7=1 → RX データ無し。bit6=1 → 書込未 ready。アイドル ready = 0x80。FMP3 Wait1 は非ゼロ+bit6 クリアだけ受ける。OUT ヘルパは bit6 セット中スピン。0x40（PC/AT 逆極性）を返すと FMP3 -m 検出失敗（midiBytes=0）し E0D0 書込がハング。 */
	uint8_t st;
	if (mpuResetBusy_) {
		mpuResetBusy_ = 0;
		MidiPushAck(0xfe);
		st = 0xC0; /* この poll: 書込ビジー。次 poll が ACK を見る */
	} else if (mpuAckR_ != mpuAckW_ || mpuRxFull_)
		st = 0x00; /* RX あり、書 OK */
	else
		st = 0x80; /* RX 無し、書込 OK */
	if (st != g_mpuLastSt) {
		MpuTrace(0xE0D2, 0, st);
		g_mpuLastSt = st;
	}
	return st;
}

/* CHardPc98::MidiDataIn の実装 */
uint8_t CHardPc98::MidiDataIn()
{
	uint8_t v;
	if (mpuAckR_ != mpuAckW_) {
		v = mpuAckQ_[mpuAckR_ & 31];
		mpuAckR_++;
	} else if (mpuRxFull_) {
		mpuRxFull_ = 0;
		v = mpuRx_;
	} else
		v = 0xfe;
	MpuTrace(0xE0D0, 0, v);
	return v;
}

/* --- Wolfteam E0D0 MUSDRV コマンドストリーム → OPN ソフトブリッジ ---------------

   キャプチャ（.cursor/_cemu_wolf_e0d0_probe）で確認: Wolfteam MUSDRV タイマ ISR はポート E0D0（MPU-401／MIDI 基板 UART）へ *標準 MIDI バイト列* を出す。ノート用に YM2203 を組まない。FM チップは init/mute ブロックだけ受ける。MIDI 基板無し検出のタイトルはまだ止まる。

   Hoot に MIDI 基板シンセは無いので、ライブ MIDI ストリームを YM2203/2608 FM ボイスへ訳し OPN で聞こえるようにする。観測したノート事象の本物（ポリ数は減）合成 — 汎用 FM パッチをノートオン毎に鳴らしノートオフでキーオフ。 */

/* 1 オクターブ OPN(A) F-number 表（C..B）。block がオクターブ。OPN（3.9936 MHz /72）と OPNA（7.9872 MHz /144）は同じ値 — どちらも約 55.5 kHz FM 基レート。 */
static const uint16_t kWolfFnum[12] = {
	0x0269, 0x028E, 0x02B4, 0x02DE, 0x030B, 0x0339,
	0x036B, 0x03A0, 0x03D7, 0x0412, 0x0450, 0x0492
};

/* CHardPc98::WolfBridgeReset の実装 */
void CHardPc98::WolfBridgeReset()
{
	wolfRunStatus_ = 0;
	wolfDataIdx_ = 0;
	wolfDataNeed_ = 0;
	wolfInSysex_ = 0;
	wolfVoiceClock_ = 0;
	wolfVoiceCount_ = opnaMode ? 6 : 3;
	for (int i = 0; i < 6; i++) {
		wolfVoiceActive_[i] = 0;
		wolfVoiceMidiCh_[i] = -1;
		wolfVoiceNote_[i] = -1;
		wolfVoiceAge_[i] = 0;
	}
	memset(wolfChVol_, 127, sizeof(wolfChVol_));
	memset(wolfChExpr_, 127, sizeof(wolfChExpr_));
}

/* CHardPc98::WolfOpnW の実装 */
void CHardPc98::WolfOpnW(int bank, uint8_t reg, uint8_t val)
{
	if (!chip_) return;
	/* FMP の Timer B（26h/27h）ラッチを盗むと、続く OUT がキーオンに消え
	   タイマがリロードされず IRQ が暴れて MIDI が超高速になる。 */
	const uint8_t savedLo = opnLatchedAddr_;
	const uint8_t savedHi = opnLatchedAddrHi_;
	chip_->Write((uint32_t)bank, reg);
	chip_->Write((uint32_t)(bank | 1), val);
	chip_->Write(0, savedLo);
	if (opnaMode)
		chip_->Write(0x100, savedHi);
	opnLatchedAddr_ = savedLo;
	opnLatchedAddrHi_ = savedHi;
}

/* ボイス v: 0-2 → FM1-3（bank0）、3-5 → FM4-6（bank1、OPNA のみ）。ベロシティと MIDI チャネル音量／エクスプレッションでレベルした汎用 4 キャリア（アルゴリズム 7）パッチを組む。 */
void CHardPc98::WolfProgramVoice(int v, int vel, int midiCh)
{
	const int bank = (v < 3) ? 0 : 0x100;
	const int ci = (v < 3) ? v : (v - 3);
	int eff = vel;
	eff = eff * (int)wolfChVol_[midiCh & 15] / 127;
	eff = eff * (int)wolfChExpr_[midiCh & 15] / 127;
	if (eff < 0) eff = 0; if (eff > 127) eff = 127;
	/* 大きい音 → 小さい TL（0x10 が最大音量 .. 約 0x38 が静か） */
	uint8_t tl = (uint8_t)(0x10 + ((127 - eff) * 40 / 127));
	for (int op = 0; op < 4; op++) {
		const uint8_t o = (uint8_t)((op << 2) + ci);
		WolfOpnW(bank, (uint8_t)(0x30 + o), 0x01); /* DT=0 MUL=1 */
		WolfOpnW(bank, (uint8_t)(0x40 + o), tl);   /* TL */
		WolfOpnW(bank, (uint8_t)(0x50 + o), 0x1F); /* KS=0 AR=31 */
		WolfOpnW(bank, (uint8_t)(0x60 + o), 0x00); /* DR=0 */
		WolfOpnW(bank, (uint8_t)(0x70 + o), 0x00); /* SR=0 */
		WolfOpnW(bank, (uint8_t)(0x80 + o), 0x0A); /* SL=0 RR=10 */
		WolfOpnW(bank, (uint8_t)(0x90 + o), 0x00); /* SSG-EG オフ */
	}
	WolfOpnW(bank, (uint8_t)(0xB0 + ci), 0x07); /* アルゴリズム 7、FB 0 */
	WolfOpnW(bank, (uint8_t)(0xB4 + ci), 0xC0); /* L+R オン */
}

/* CHardPc98::WolfNoteOn の実装 */
void CHardPc98::WolfNoteOn(int midiCh, int note, int vel)
{
	if (!chip_ || note < 0 || note > 127) return;
	wolfNoteOnCount_++;
	const int nv = wolfVoiceCount_ > 0 ? wolfVoiceCount_ : 3;
	/* 既にこの (ch,note) を持つボイスを再利用。無ければ空き。それも無ければ最古のアクティブ。 */
	int v = -1;
	for (int i = 0; i < nv; i++)
		if (wolfVoiceActive_[i] && wolfVoiceMidiCh_[i] == midiCh && wolfVoiceNote_[i] == note) { v = i; break; }
	if (v < 0)
		for (int i = 0; i < nv; i++)
			if (!wolfVoiceActive_[i]) { v = i; break; }
	if (v < 0) {
		uint64_t best = ~0ull;
		for (int i = 0; i < nv; i++)
			if (wolfVoiceAge_[i] < best) { best = wolfVoiceAge_[i]; v = i; }
	}
	if (v < 0) return;
	const int chBits = (v < 3) ? v : (0x04 + (v - 3));
	/* リチューン前にキーオフし、きれいな再アタックを強制 */
	WolfOpnW(0, 0x28, (uint8_t)chBits);
	WolfProgramVoice(v, vel, midiCh);
	const int oct = note / 12;
	int block = oct - 2;
	if (block < 0) block = 0; if (block > 7) block = 7;
	const uint16_t fnum = kWolfFnum[note % 12];
	const int bank = (v < 3) ? 0 : 0x100;
	const int ci = (v < 3) ? v : (v - 3);
	WolfOpnW(bank, (uint8_t)(0xA4 + ci), (uint8_t)(((block & 7) << 3) | ((fnum >> 8) & 7)));
	WolfOpnW(bank, (uint8_t)(0xA0 + ci), (uint8_t)(fnum & 0xff));
	WolfOpnW(0, 0x28, (uint8_t)(0xF0 | chBits)); /* 4 スロット全部キーオン */
	wolfVoiceActive_[v] = 1;
	wolfVoiceMidiCh_[v] = midiCh;
	wolfVoiceNote_[v] = note;
	wolfVoiceAge_[v] = ++wolfVoiceClock_;
}

/* CHardPc98::WolfNoteOff の実装 */
void CHardPc98::WolfNoteOff(int midiCh, int note)
{
	if (!chip_) return;
	wolfNoteOffCount_++;
	const int nv = wolfVoiceCount_ > 0 ? wolfVoiceCount_ : 3;
	for (int i = 0; i < nv; i++) {
		if (wolfVoiceActive_[i] && wolfVoiceMidiCh_[i] == midiCh && wolfVoiceNote_[i] == note) {
			const int chBits = (i < 3) ? i : (0x04 + (i - 3));
			WolfOpnW(0, 0x28, (uint8_t)chBits); /* キーオフ */
			wolfVoiceActive_[i] = 0;
			wolfVoiceNote_[i] = -1;
		}
	}
}

/* CHardPc98::WolfAllNotesOff の実装 */
void CHardPc98::WolfAllNotesOff()
{
	if (!chip_) return;
	for (int i = 0; i < 6; i++) {
		if (wolfVoiceActive_[i]) {
			const int chBits = (i < 3) ? i : (0x04 + (i - 3));
			WolfOpnW(0, 0x28, (uint8_t)chBits);
		}
		wolfVoiceActive_[i] = 0;
		wolfVoiceNote_[i] = -1;
	}
}

/* CHardPc98::WolfMidiDispatch の実装 */
void CHardPc98::WolfMidiDispatch(uint8_t status, uint8_t d0, uint8_t d1)
{
	const uint8_t cmd = (uint8_t)(status & 0xF0);
	const int ch = status & 0x0F;
	switch (cmd) {
	case 0x90:
		if (d1 > 0) WolfNoteOn(ch, d0, d1);
		else WolfNoteOff(ch, d0);
		break;
	case 0x80:
		WolfNoteOff(ch, d0);
		break;
	case 0xB0:
		wolfCtrlCount_++;
		if (d0 == 0x07) wolfChVol_[ch] = d1;        /* チャネル音量 */
		else if (d0 == 0x0B) wolfChExpr_[ch] = d1;  /* エクスプレッション */
		else if (d0 == 0x78 || d0 == 0x7B) WolfAllNotesOff(); /* 全音／全ノートオフ */
		break;
	default:
		/* プログラムチェンジ／ピッチベンド／アフタータッチ: このブリッジではボイスしない */
		break;
	}
}

/* 生 MIDI バイト列を解析（ランニングステータス、2/3 バイトチャネルメッセージ、sysex スキップ、0xFF ストリームリセット）。 */
void CHardPc98::WolfCmdByte(uint8_t data)
{
	if (data & 0x80) {
		if (data >= 0xF8)
			return; /* リアルタイム: 無視 */
		if (data == 0xF0) { wolfInSysex_ = 1; return; }
		if (data == 0xF7) { wolfInSysex_ = 0; wolfRunStatus_ = 0; return; }
		if (data == 0xFF) { /* ストリーム内のシステムリセット */
			if (wolfBridgeEnable_) WolfAllNotesOff();
			wolfRunStatus_ = 0; wolfDataIdx_ = 0; wolfInSysex_ = 0;
			return;
		}
		if (data >= 0xF1 && data <= 0xF6) {
			wolfRunStatus_ = 0;
			wolfDataIdx_ = 0;
			wolfDataNeed_ = (data == 0xF2) ? 2 : ((data == 0xF1 || data == 0xF3) ? 1 : 0);
			return;
		}
		/* チャネルボイス状態 */
		wolfRunStatus_ = data;
		wolfDataIdx_ = 0;
		const uint8_t hi = (uint8_t)(data & 0xF0);
		wolfDataNeed_ = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
		return;
	}
	if (wolfInSysex_ || wolfRunStatus_ == 0)
		return;
	wolfData_[wolfDataIdx_++] = data;
	if (wolfDataIdx_ < wolfDataNeed_)
		return;
	wolfDataIdx_ = 0; /* ランニングステータス: wolfRunStatus_ を残す */
	if (wolfBridgeEnable_)
		WolfMidiDispatch(wolfRunStatus_, wolfData_[0], wolfData_[1]);
}

/* I/O ポート読込 */
uint8_t CHardPc98::PortIn(uint16_t port)
{
	port = Pc98FoldMpuAlias(Pc98FoldOpnAlias(Pc98FoldPitAlias(port)));
	/* PC-98 表示 status: bit5 が垂直帰線で変わる。いくつかの常駐糊はコマンド受け渡しを低→高遷移待ちで同期（mscd_98 は 18 フレーム）。汎用オープンバス FF を返すと最初の待ちループに罠。 */
	/* 両 µPD7220 がここで答える: 0x60 がテキストマスタ、0xA0 がグラフィックスレーブ。以前は 0xA0 だけ答えたので、テキスト GDC でフレーム同期するプログラム（C-Class FMX は vsync 下降／上昇を待ってから音源基板を探る）が最初の待ちで永久スピン。 */
	if (port == 0x0060 || port == 0x00A0) {
		uint8_t s;
		if (port == 0x00A0) {
			/* tky98 糊は INT F1 再生前に bit5 の下降／上昇を 60 回待つ。60Hz 時計なら約 1s。DrainInterrupt は 0.5s なので旧 TKYDRV パックは待ちを出なかった（dumps=1）。poll 毎にトグル — OPN タイマがまだ曲をペース。 */
			gdcA0Poll_ = (uint8_t)(gdcA0Poll_ + 1);
			s = (uint8_t)((gdcA0Poll_ & 1) ? 0x20 : 0x00);
		} else {
			const uint64_t halfFrame =
				(cpuHz_ > 120) ? (uint64_t)cpuHz_ / 120ull : 1ull;
			s = ((cpuCycles_ / halfFrame) & 1ull) ? 0x20 : 0x00;
			/* アイドル GDC はコマンド FIFO を空にし描画していない。書込前に FIFO 空を待つ呼び出しはそれを見る必要がある。報告するのは 0x60 だけ: 0xA0 は糊が調律されてから素の vsync を答え、それらは anyway bit5 をマスク。 */
			s |= 0x04;
		}
		return s;
	}
	/* PC-88VA: PC-88 OPN ポートは同じチップ status/data を読む。
	   OPN のみ（YM2203）では 46h/47h は未実装オープンバス 0xFF。lo status を
	   ミラーすると olteus.com の `IN 46h / CMP FF` が OPNA と誤認し MUSIC*P を開かない。 */
	if (pc88VaIo_) {
		switch (port) {
		case 0x44: case 0xA8: port = OPN_ADDR0; break;
		case 0x45: case 0xA9: port = OPN_DATA0; break;
		case 0x46: case 0xAC:
			if (!opnaMode)
				return 0xff;
			port = OPN_ADDR1;
			break;
		case 0x47: case 0xAD:
			if (!opnaMode)
				return 0xff;
			port = OPN_DATA1;
			break;
		default: break;
		}
	}
	switch (port) {
	case OPN_ADDR0: {
		uint8_t s = chip_ ? chip_->ReadStatus() : 0xff;
		/* YM2203 IRQ はタイマだけ。線が立っているのに status が 0 だと
		   ELFMUS ISR の TEST bit1（Timer B）が FM を永久スキップする。 */
		if (chip_ && chip_->Irq() && (s & 0x03) == 0)
			s = (uint8_t)(s | 0x02);
		s = Pc98OpnStatusClearBusy(s);
		return s;
	}
	case OPN_DATA0:
		/* SSG I/O A（reg 0x0E）: 基板 IRQ ジャンパ。MUSE/mbmusp は bit7-6 を読んで INT14h を選ぶ。既定オープンバス 0 だと INT0B をフックし EOI はスレーブへ（hootrip preset_muse_irq_jumper）。 */
		if (chip_ && opnLatchedAddr_ == 0x0E && (ssgPortAJumper_ & 0x80))
			return ssgPortAJumper_;
		/* YM2203/2608 SSG $00-$0F は読める。PLAY5 / MMD2.SYS / F.COM はカナリア（0x55 または 1）を書いて IN 比較。ymfm read_data() はレジスタではなく status。直近 DATA0 書込を返す。 */
		if (s_iwaBusHold)
			return g_opnDataLatch;
		if (opnLatchedAddr_ <= 0x0F)
			return ssgEcho_[opnLatchedAddr_];
		/* 旧 TKY/OPNDRV（c2gp、dynamo98）は OUT 27h/40h のあと IN DATA で 0x40 を期待し、OUT addr FFh / IN DATA not-1。本物 YM2203 の 27h は書込専用。PC-98 基板は直近データポート書込をバスホールド。新しい OPNDRV は両比較を NOP（rolling95）。エコーするのはその 2 つのラッチ番地だけ: 一括 DATA0 ラッチは rolling95 の最初の可聴窓を動かした（SIL.MDT fp）。 */
		if (g_opnBusHold && (opnLatchedAddr_ == 0x27 || opnLatchedAddr_ == 0xFF))
			return g_opnDataLatch;
		/* NeSS SPLIT（hardshot）: OUT addr FFh / IN DATA。ymfm は 0 を返し、検出が YM2203 省略路（AH=30 / jmp 166D）へ入り [12C] bit0 が立たない。INT D2 AH≠0 は bit0 必須なので糊 AX=101 が NOP。0 でも YM2608 ID の 1 でもない値なら SSG 0x55 カナリアへ進む。TKY も IN DATA not-1 を要求。 */
		if (opnLatchedAddr_ == 0xFF)
			return 0xFF;
		{
			uint8_t d = chip_ ? chip_->ReadData() : 0xff;
			if (g_mmdClassic)
				d = (uint8_t)(d & (uint8_t)~0x80);
			return d;
		}
	case OPN_ADDR1: {
		if (modeSorch_)
			return opl_ ? opl_->ReadStatus() : 0x06; /* OPL2 ID パターン */
		uint8_t s = chip_ ? chip_->ReadStatusHi() : 0xff;
		s = Pc98OpnStatusClearBusy(s);
		return s;
	}
	case OPN_DATA1:
		if (modeSorch_) return 0xff; /* OPL2 に読めるデータポートは無い */
		return chip_ ? chip_->ReadDataHi() : 0xff;
	case EXT_CMD: return extCmd_;
	case EXT_SONG: return (uint8_t)(extSong_ & 0xff);
	case EXT_SONG + 1: return (uint8_t)(extSong_ >> 8);
	case EXT_PARAM: return (uint8_t)(extParam_ & 0xff);
	case EXT_PARAM + 1: return (uint8_t)(extParam_ >> 8);
	case EXT_STATE: return stubState_;
	case HOST_CMD: return hostStatus_;
	case HOST_P1: return (uint8_t)(hostParam1_ & 0xff);
	case HOST_P1 + 1: return (uint8_t)(hostParam1_ >> 8);
	case HOST_P2: return (uint8_t)(hostParam2_ & 0xff);
	case HOST_P2 + 1: return (uint8_t)(hostParam2_ >> 8);
	case HOST_P3: return (uint8_t)(hostParam3_ & 0xff);
	case HOST_P3 + 1: return (uint8_t)(hostParam3_ >> 8);
	case SOUND86_ID:
		/* PC-9801-86 @ 0188h: 上位ニブル Sound ID = 4（MAME/NP2/Undocumented9801）。bit0 = YM2608 拡張。bit1 = OPNA マスク。既定 mask=0 → ID 0x40。 */
		if (!opnaMode)
			return 0xff; /* 26K / OPN のみ: ポート不在 */
		return (uint8_t)(0x40 | (sound86Mask_ & 0x03));
	case SOUND86_FIFO_CTL:
		/* FMX 3.10 INT14 1BB4: [2849]==2 なら IN A468h / TEST 10h が CALL 011C でスピン。オープンバス FF は bit4 を消さない。 */
		if (s_fmxKeepIrq0)
			return 0;
		return 0xff;
	/* A466–A66E: タイトルがソフト 86PCM を要しない限りオープンバスのまま。空 FIFO を stub すると FMP3 が無音 PCM 経路を取った（vg2）。 */
	case 0x506:
		/* PC-88VA: MAP は IN 506h bit0 をビジーとして poll（olteus CS:7968）。オープンバス 0xFF は曲ロード／シーケンサ前に永久スピン。 */
		if (pc88VaIo_)
			return 0x00;
		return 0xff;
	case PIC_CMD:
	case SLAVE_PIC_CMD:
		/* OCW3 IRR/ISR poll（例 ys_98 MANPR1 CS:5123 が Timer B 武装後）。未処理読は 0xFF で永久スピン（opnW が数十万、key=0）。ソフト PIC にラッチ ISR は無い。PC-88VA MAP（olteus CS:0C50）: DS:[00C0]!=0 なら bit6 を待ってクリア。[00C0]==0 なら bit6 はクリア必須、さもなくば再スピン。MMD2 INT14: マスタ ISR bit7 がスレーブ EOI するかを決める。0 を返すと OUT 08h,20h を飛ばし IRQ12 が in-service のまま。 */
		/* MMD2 INT14 はマスタ ISR bit7 でスレーブ EOI する。古典 MMD.SYS は 154e=0 で OCW3 をポート 0 に出し、0x80 だと 0439 がスレーブ経路へ入りキーオン後に止まる。 */
		if (port == PIC_CMD && g_mmdPicIsr && opnInService_ && !g_mmdClassic)
			return 0x80;
		if (pc88VaIo_ && port == SLAVE_PIC_CMD && olteusDataSeg_) {
			uint8_t* mem = np2_mem();
			const unsigned a = ((unsigned)olteusDataSeg_ << 4) + 0xC0u;
			if (mem && a + 1u < 0x200000u) {
				const unsigned c0 = (unsigned)mem[a] | ((unsigned)mem[a + 1] << 8);
				if (c0 != 0)
					return 0x40;
			}
			return 0x00;
		}
		return 0x00;
	case PIC_MASK: return picMask_;
	case SLAVE_PIC_MASK: return slavePicMask_;
	case PIT_CT0: case PIT_CT1: case PIT_CTRL: return PitIn(port);
	case PPI_A:
		/* システム PPI ポート A = DIP SW2。入力モードで QEMU は 0x73 */
		return 0x73;
	case PPI_B:
		/* TYP=10（オリジナル 9801 ではない）、MOD=1（8 MHz / 2 MHz PIT） */
		return 0xA0;
	case PPI_C:
		return ppiC_;
	case 0x41:
		/* キーボード 8251 データ。スキャンコードはキュー無し */
		return 0x00;
	case 0x43:
		/* 8251 status（TxRDY|TxEMPTY）／システムポート: プリンタ非ビジー */
		return 0x06;
	case 0x30:
	case 0x32:
		/* FMX MIDI 8251 データ 30h／ステータス 32h。33h は PPI_B なので触らない。 */
		if (modeMidi_ || mpuUart_)
			return 0x05;
		return 0xff;
	case WOLF_SYNC0:
		if (modeMidi_ || mpuUart_)
			return MidiDataIn();
		return wolfSyncRun_ ? 0xFE : 0xFF;
	case 0xC0D0: /* FMD / MPU-PC98 データ（80D0 族もここに畳む） */
		return MidiDataIn();
	case WOLF_SYNC1:
		if (modeMidi_ || mpuUart_)
			return MidiStatusIn();
		return wolfSyncRun_ ? 0x00 : 0xFF;
	case 0xC0D2: /* FMD 検出は modeMidi_ 前に C0D2 を IN する */
		return MidiStatusIn();
	default: return 0xff;
	}
}

/* I/O ポート書込 */
void CHardPc98::PortOut(uint16_t port, uint8_t data)
{
	port = Pc98FoldMpuAlias(Pc98FoldOpnAlias(Pc98FoldPitAlias(port)));
	/* olteus_va: OUT 10A,0022 がピクチャ／間隔 tick を武装。00/0C が解除。far 表フックがまだ走っていなければ CS を MAP seg として捕捉。 */
	if (pc88VaIo_ && port == 0x10A) {
		if (data == 0x22) {
			uint16_t cs = np2_reg_get(NP2_R_CS);
			if (!olteusMapSeg_ && cs && cs != (uint16_t)DOS98_TRAMP_SEG)
				olteusMapSeg_ = cs;
			if (olteusMapSeg_)
				ArmOlteusVaTimer(olteusMapSeg_);
			olteusTimerOn_ = 1;
		} else if (data == 0x00 || data == 0x0C) {
			olteusTimerOn_ = 0;
		}
	}
	/* PC-88VA: 音楽は古典 PC-88 OPN（44h/A8h）。BIOSD は PC-98 188h も poke。ポート族毎に番地をステージし、交互 OUT がラッチを奪えないようにする（SSG C／ミキサ破壊）。 */
	if (pc88VaIo_) {
		switch (port) {
		case 0x44: case 0xA8:
			/* 番地をラッチしてコミット。BPS tetrisva の OPNA 検出は OUT 44h,FFh / IN 45h で ym2608 ID コード 01 を期待 — Write(0) が無いとチップ番地が古く [851A] が立たない。 */
			vaPc88LatchedAddr_ = data;
			vaPc88PortHits_++;
			if (chip_) {
				chip_->Write(0, data);
				opnLatchedAddr_ = data;
			}
			return;
		case 0x45: case 0xA9:
			vaPc88PortHits_++;
			if (chip_) {
				chip_->Write(0, vaPc88LatchedAddr_);
				chip_->Write(1, data);
				opnLatchedAddr_ = vaPc88LatchedAddr_;
				opnWriteCount_++;
				if (opnLogCount_ < 64) {
					opnLogAddr_[opnLogCount_] = vaPc88LatchedAddr_;
					opnLogData_[opnLogCount_] = data;
					opnLogCount_++;
				}
				if (vaPc88LatchedAddr_ == 0x28 && (data & 0xf0) != 0)
					opnKeyOnCount_++;
				if ((vaPc88LatchedAddr_ >= 0xa0 && vaPc88LatchedAddr_ <= 0xa2) ||
					(vaPc88LatchedAddr_ >= 0xa4 && vaPc88LatchedAddr_ <= 0xa6))
					opnFnumCount_++;
			}
			return;
		case 0x46: case 0xAC:
			if (!opnaMode)
				return;
			vaPc88LatchedAddrHi_ = data;
			vaPc88PortHits_++;
			if (chip_) {
				chip_->Write(0x100, data);
				opnLatchedAddrHi_ = data;
			}
			return;
		case 0x47: case 0xAD:
			if (!opnaMode)
				return;
			vaPc88PortHits_++;
			if (chip_) {
				chip_->Write(0x100, vaPc88LatchedAddrHi_);
				chip_->Write(0x101, data);
				opnLatchedAddrHi_ = vaPc88LatchedAddrHi_;
				opnWriteCount_++;
			}
			return;
		case OPN_ADDR0: case OPN_DATA0:
		case OPN_ADDR1: case OPN_DATA1:
			vaPc98PortHits_++;
			break; /* フォールスルー — PC-98 経路を再アサートしたまま */
		default:
			break;
		}
	}
	switch (port) {
	case OPN_ADDR0:
		if (chip_) {
			chip_->Write(0, data);
			opnLatchedAddr_ = data;
			opnWriteCount_++;
		}
		break;
	case OPN_DATA0:
		if (chip_) {
			if (g_mmdClassic && (opnLatchedAddr_ & 0xf0) == 0x40 && data >= 0x7F)
				data = 0x18;
			if (g_mmdClassic && data == 0) {
				if (opnLatchedAddr_ >= 0xA0 && opnLatchedAddr_ <= 0xA2)
					data = 0x69;
				else if (opnLatchedAddr_ >= 0xA4 && opnLatchedAddr_ <= 0xA6)
					data = 0x22;
			}
			/* データポートは基板上の 188h ラッチ。ymfm lastAddr は ISR の
			   27h=3Fh や status IN のあと食い違う。24–27 だけ直すと Timer B は
			   直るが、直後の 30h–B6h / 28h が 27h に落ちて先頭音色とキーオンを
			   落とす（vg2 先頭 PC/音色、ed4 が這う）。 */
			chip_->Write(0, opnLatchedAddr_);
			chip_->Write(1, data);
			if (opnLogCount_ < 64) {
				opnLogAddr_[opnLogCount_] = opnLatchedAddr_;
				opnLogData_[opnLogCount_] = data;
				opnLogCount_++;
			}
			opnTailAddr_[opnTailCount_ % 64] = opnLatchedAddr_;
			opnTailData_[opnTailCount_ % 64] = data;
			opnTailCount_++;
			if (opnLatchedAddr_ == 0x28) {
				if ((data & 0xf0) != 0) {
					opnKeyOnCount_++;
					opnKeyOnCh_[data & 0x07]++;
					g_mmdKeyOn |= (uint8_t)(1u << (data & 7));
				} else
					g_mmdKeyOn &= (uint8_t)~(1u << (data & 7));
			} else if (g_mmdPicIsr
				&& ((opnLatchedAddr_ >= 0xA0 && opnLatchedAddr_ <= 0xA2)
					|| (opnLatchedAddr_ >= 0xA4 && opnLatchedAddr_ <= 0xA6))) {
				const uint8_t ch = (uint8_t)(opnLatchedAddr_ >= 0xA4
					? (opnLatchedAddr_ - 0xA4) : (opnLatchedAddr_ - 0xA0));
				if (ch < 3 && (g_mmdKeyOn & (1u << ch)) == 0) {
					const uint8_t saved = opnLatchedAddr_;
					chip_->Write(0, 0x28);
					chip_->Write(1, (uint8_t)(0xF0 | ch));
					opnKeyOnCount_++;
					opnKeyOnCh_[ch]++;
					g_mmdKeyOn |= (uint8_t)(1u << ch);
					chip_->Write(0, saved);
					opnLatchedAddr_ = saved;
				}
			}
			if (((opnLatchedAddr_ & 0xf0) == 0x40 || (opnLatchedAddr_ & 0xf0) == 0x50) && data < 0x7f)
				opnTlLiveCount_++;
			if ((opnLatchedAddr_ >= 0xa0 && opnLatchedAddr_ <= 0xa2) ||
				(opnLatchedAddr_ >= 0xa4 && opnLatchedAddr_ <= 0xa6))
				opnFnumCount_++;
			if (opnLatchedAddr_ == 0x24 || opnLatchedAddr_ == 0x25 || opnLatchedAddr_ == 0x26 || opnLatchedAddr_ == 0x27)
				opnTimerCount_++;
			if (opnLatchedAddr_ == 0x26)
				g_lastTimerB = data;
			if (opnLatchedAddr_ == 0x27)
				g_lastTimerCtrl = data;
			if (opnLatchedAddr_ == 0x0E)
				ssgPortAJumper_ = data;
			if (opnLatchedAddr_ <= 0x0F)
				ssgEcho_[opnLatchedAddr_] = data;
			g_opnDataLatch = data;
			opnWriteCount_++;
		}
		break;
	case OPN_ADDR1:
		/* SOUND ORCHESTRA ではこの 2 ポートは OPNA ハイバンクではなく第 2 チップ全体。OPN へ送るとモード選択に関わらず素の OPN に聞こえた。 */
		if (modeSorch_) {
			if (opl_) opl_->Write(0, data);
			opnLatchedAddrHi_ = data;
			break;
		}
		if (chip_) {
			chip_->Write(0x100, data);
			opnLatchedAddrHi_ = data;
			opnWriteCount_++;
		}
		break;
	case OPN_DATA1:
		if (modeSorch_) {
			if (opl_) opl_->Write(1, data);
			SorchTrackOplWrite(opnLatchedAddrHi_, data);
			break;
		}
		if (chip_) {
			chip_->Write(0x100, opnLatchedAddrHi_);
			chip_->Write(0x101, data);
			if (opnLogCount_ < 64) {
				opnLogAddr_[opnLogCount_] = (uint16_t)(0x100u + opnLatchedAddrHi_);
				opnLogData_[opnLogCount_] = data;
				opnLogCount_++;
			}
			opnTailAddr_[opnTailCount_ % 64] = (uint16_t)(0x100u + opnLatchedAddrHi_);
			opnTailData_[opnTailCount_ % 64] = data;
			opnTailCount_++;
			if (opnLatchedAddrHi_ == 0x28 && (data & 0xf0) != 0)
				opnKeyOnCount_++;
			if (((opnLatchedAddrHi_ & 0xf0) == 0x40 || (opnLatchedAddrHi_ & 0xf0) == 0x50) && data < 0x7f)
				opnTlLiveCount_++;
			opnWriteCount_++;
		}
		break;
	case EXT_CMD: extCmd_ = data; break;
	case EXT_SONG: extSong_ = (extSong_ & 0xff00) | data; break;
	case EXT_SONG + 1: extSong_ = (extSong_ & 0x00ff) | ((uint16_t)data << 8); break;
	case EXT_PARAM: extParam_ = (extParam_ & 0xff00) | data; break;
	case EXT_PARAM + 1: extParam_ = (extParam_ & 0x00ff) | ((uint16_t)data << 8); break;
	case EXT_STATE: stubState_ = data; break;
	case HOST_CMD:
		hostFunc_ = data;
		HostService(data);
		break;
	case HOST_P1: hostParam1_ = (hostParam1_ & 0xff00) | data; break;
	case HOST_P1 + 1: hostParam1_ = (hostParam1_ & 0x00ff) | ((uint16_t)data << 8); break;
	case HOST_P2: hostParam2_ = (hostParam2_ & 0xff00) | data; break;
	case HOST_P2 + 1: hostParam2_ = (hostParam2_ & 0x00ff) | ((uint16_t)data << 8); break;
	case HOST_P3: hostParam3_ = (hostParam3_ & 0xff00) | data; break;
	case HOST_P3 + 1: hostParam3_ = (hostParam3_ & 0x00ff) | ((uint16_t)data << 8); break;
	case 0x30:
		/* FMX MIDI 8251 データ。コマンド 32h（40/4E/31）は MIDI ではない。 */
		if (modeMidi_ || mpuUart_)
			MidiDataOut(data);
		break;
	case WOLF_SYNC0:
		/* midiout / FMP -m: UART MIDI をキャプチャ。Wolfteam FM: コマンドブリッジ */
		if (modeMidi_ || mpuUart_) {
			MidiDataOut(data);
			break;
		}
		wolfCmdWriteCount_++;
		if (wolfCmdLogCount_ < sizeof(wolfCmdLog_))
			wolfCmdLog_[wolfCmdLogCount_++] = data;
		WolfCmdByte(data);
		break;
	case 0xC0D0:
		MidiDataOut(data);
		break;
	case WOLF_SYNC1:
		if (modeMidi_ || mpuUart_)
			MidiCmdOut(data);
		break;
	case 0xC0D2:
		MidiCmdOut(data);
		break;
	case PIC_CMD:
		if ((data & 0x10) != 0) {
			picMask_ = 0;
			picMasterIcw1_ = data;
			picMasterIcw_ = 1;
		} else if ((data & 0x20) != 0) {
			g_pc98Eoi = 1;
		}
		break;
	case PIC_MASK:
		if (picMasterIcw_) {
			picMasterIcw_++;
			if (picMasterIcw_ >= 3 + (picMasterIcw1_ & 1))
				picMasterIcw_ = 0;
		} else {
			picMask_ = data;
			if (s_valkyKeepIrq0 || s_fmxKeepIrq0)
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
		}
		break;
	case SLAVE_PIC_CMD:
		if ((data & 0x10) != 0) {
			slavePicMask_ = 0;
			picSlaveIcw1_ = data;
			picSlaveIcw_ = 1;
		} else if ((data & 0x20) != 0) {
			g_pc98Eoi = 1;
		}
		break;
	case SLAVE_PIC_MASK:
		if (picSlaveIcw_) {
			picSlaveIcw_++;
			if (picSlaveIcw_ >= 3 + (picSlaveIcw1_ & 1))
				picSlaveIcw_ = 0;
		} else {
			slavePicMask_ = data;
		}
		break;
	case PIT_CT0: case PIT_CT1: case PIT_CTRL: PitOut(port, data); break;
	case PPI_C:
		ppiC_ = data;
		beepEventCount_++;
		BeepSetGateFromPpi();
		break;
	case PPI_CTRL:
		if ((data & 0x80) == 0) {
			/* 8255 ビット set/reset: 0x06 が bit3 クリア（スピーカオン）、0x07 がセット */
			const unsigned bit = (unsigned)((data >> 1) & 7);
			if (data & 1)
				ppiC_ = (uint8_t)(ppiC_ | (uint8_t)(1u << bit));
			else
				ppiC_ = (uint8_t)(ppiC_ & (uint8_t)~(1u << bit));
			if (bit == 3) {
				beepEventCount_++;
				BeepSetGateFromPpi();
			}
		}
		break;
	case VSYNC_ACK: vsyncPending_ = 0; break;
	case IO_DELAY: break;
	case SOUND86_ID:
		/* Sound ID ニブルは残し、mask/enhance ビットを更新（MAME mask_w） */
		if (opnaMode)
			sound86Mask_ = (uint8_t)(data & 0x03);
		break;
	case SOUND86_FIFO_STAT:
	case SOUND86_FIFO_CTL:
	case SOUND86_DAC_CTL:
	case SOUND86_FIFO_DAT:
	case SOUND86_MUTE:
		/* プローブが故障しないよう書込を受ける。ソフト PCM エンジンはまだ無い */
		if (opnaMode) {
			if (port == SOUND86_FIFO_CTL) sound86FifoCtl_ = data;
			else if (port == SOUND86_DAC_CTL) sound86DacCtl_ = data;
			else if (port == SOUND86_MUTE) sound86Mute_ = (uint8_t)(data & 1);
		}
		break;
	default: break;
	}
}

/* zip から ROM／曲データを載せる */
int CHardPc98::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	memset(mem, 0, 0x200000);
	StageBanks(fs, ge);
	nopnDrv_ = 0;
	dofmd_ = 0;
	fmd98_ = 0;
	fmdSongOff_ = 0;
	rx98_ = 0;
	rxSongOff_ = 0;
	prog98_ = 0;
	progSongAddr_ = 0;
	bst398_ = 0;
	koei98_ = 0;
	cal98_ = 0;
	madp98_ = 0;
	n3golf98_ = 0;
	dks98_ = 0;
	mdplay98_ = 0;
	packCmd1_ = 0;
	synthIfKeepalive_ = 0;
	wolfteam98_ = 0;
	wolfGateStop_ = 0x5B48;
	wolfGatePlay_ = 0x5B5A;
	wolfSongPtr_ = 0x5B5D;
	wolfSongBuf_ = 0x7E5E;
	wolfTitleWord_ = 0x643A;
	wolfFlagA_ = 0x0662;
	wolfMiSeg_ = 0;
	wolfSyncRun_ = 0;
	wolfCmdLogCount_ = 0;
	wolfCmdWriteCount_ = 0;
	wolfNoteOnCount_ = 0;
	wolfNoteOffCount_ = 0;
	wolfCtrlCount_ = 0;
	WolfBridgeReset();
	dosStubReady_ = 0;

	/* DOS 分岐前のリズム ROM: 空 ROM の ADPCM-A 読はラップするアキュムレータランプに復号され、FMP/PMD ドラムが全 pc98dos タイトルでのこぎりノイズになった。 */
	if (opnaMode && chip_)
		CEmuLoadExternalYm2608Adpcm(chip_);

	if (isDos_)
		return BootDos(fs, ge, titleCode);

	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "binary") != 0
			&& _stricmp(r->type, "string") != 0)
			continue;
		/* KOEI はコードを seg:off dword（0xSSSSOOOO）。他は平坦物理 */
		const unsigned off = Pc98RomPhys(r->offset);
		if (off >= 0x200000u) continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		uint8_t inlineBuf[256];
		if (!data || !sz) {
			const int nIn = Pc98ParseInlineRom(r->name, inlineBuf, (int)sizeof(inlineBuf));
			if (nIn <= 0) continue;
			data = inlineBuf;
			sz = (unsigned)nIn;
		}
		unsigned n = sz;
		if (off + n > 0x200000u)
			n = 0x200000u - off;
		memcpy(mem + off, data, n);
		if (r->name[0] && _strnicmp(r->name, "NOPNDRV", 7) == 0)
			nopnDrv_ = 1;
		/* DOFMD_98 と BRANM_98 は INT 45 + host-0x11 再生経路を共有（07D4/07D6 経由 seg:off 曲 ptr）。BRANM は INT 14h を飛ばす。 */
		if (r->name[0] && (_strnicmp(r->name, "DOFMD", 5) == 0
			|| _strnicmp(r->name, "BRANM", 5) == 0))
			dofmd_ = 1;
		if (r->name[0] && _strnicmp(r->name, "FMD98", 5) == 0)
			fmd98_ = 1;
		if (r->name[0] && _stricmp(r->name, "RX.BIN") == 0)
			rx98_ = 1;
		/* Ys2 / Brandish 期 Falcom OPN ドライバ。RX.BIN 糊名は無い */
		if (r->name[0] && (_stricmp(r->name, "2608.BIN") == 0
			|| _stricmp(r->name, "2203.BIN") == 0
			|| _stricmp(r->name, "10_005.BIN") == 0)
			&& dataAddr_ <= 0 && fileSize_ > 0)
			rx98_ = 1;
		/* Falcom PROG.BIN 糊: カタログ dataaddr があるときだけ（発明しない） */
		if (data && n >= 0x90 && n <= 512 && dataAddr_ > 0 && fileSize_ > 0
			&& r->name[0] && _stricmp(r->name, "PROG.BIN") == 0) {
			prog98_ = 1;
			progSongAddr_ = dataAddr_;
		}
		if (r->name[0] && _strnicmp(r->name, "KOEI98", 6) == 0)
			koei98_ = 1;
		/* BirdySoft CAL/PAL/BEAST 族: 糊 stub + OPN ドライバが INT60 を入れるが IRQ3（IVT 0x0B）をフックしない。リロケ済み OPN ISR が走るまで play は待ちフラグ [DS:269B] でスピン。stub/bin 名で検出。 */
		if (r->name[0] && (_strnicmp(r->name, "CAL", 3) == 0
			|| _strnicmp(r->name, "PAL", 3) == 0
			|| _strnicmp(r->name, "THANATOS", 8) == 0
			|| _strnicmp(r->name, "BEAST", 5) == 0
			|| _strnicmp(r->name, "BST3", 4) == 0))
			cal98_ = 1;
		/* Beast3: 64K OPN ドライバ @0xFC00。糊 cmd0 は AH!=0 でロード選択（AH==0 は停止）。小さいタイトルコードはロード経路を取らない。 */
		if (r->name[0] && (_strnicmp(r->name, "BST3", 4) == 0
			|| _stricmp(r->name, "0FC00.BIN") == 0))
			bst398_ = 1;
		/* QueenSoft MADP: カタログ binary @0x100 が INT40 → ドライバ（AL 添字 API @0xA000/0x7000）。糊 INT7F は cmd→INT40 AL。 */
		if (r->name[0] && _strnicmp(r->name, "MADP", 4) == 0)
			madp98_ = 1;
		if (r->name[0] && _strnicmp(r->name, "N3GOLF", 6) == 0)
			n3golf98_ = 1;
		/* KSK DKS/FQ 族: dks.bin/fq3.bin 糊 + BGMDK/BGMDRV @0x35000。INT7F cmd1 → INT69 AH=0。曲はサイズ前置バンク。ホスト 07D4/07D6 はリアルモード ES:BX（表/BSS）。カタログ dataaddr 無しだと cmd1 が走らなかった。 */
		if (r->name[0] && (_stricmp(r->name, "DKS.BIN") == 0
			|| _stricmp(r->name, "FQ3.BIN") == 0
			|| _strnicmp(r->name, "BGMDK", 5) == 0
			|| _strnicmp(r->name, "BGM_DS", 5) == 0
			|| _strnicmp(r->name, "BGMFQ", 5) == 0
			|| _strnicmp(r->name, "BGMDRV", 6) == 0))
			dks98_ = 1;
		/* Glodia MDPLAY.BIN / MDZPLAY / MDPLAYD / MDRIVE: INT7F 再生は cmd1（INT 4A/49 + INT40）。ドライバは INT40–4D と PIT ISR を入れるが、ISR は遅い経路でしか IVT08 に書かれない — 未フックならブート後に保証。MDPLAYD は init で INT08 を入れるので既存フックは上書きしない。 */
		if (r->name[0] && (_stricmp(r->name, "MDPLAY.BIN") == 0
			|| _strnicmp(r->name, "MDPLAY", 6) == 0
			|| _strnicmp(r->name, "MDZPLAY", 7) == 0
			|| _stricmp(r->name, "MDRIVE.BIN") == 0
			|| _strnicmp(r->name, "MDRIVE", 6) == 0
			|| _strnicmp(r->name, "MDPLAYD", 7) == 0))
			mdplay98_ = 1;
		/* Emerald Dragon / ZAVAS / Vain Dream: INT7F cmd0 は停止 far、cmd1 が IN 7E4 のあと再生 far。曲は dataaddr（ドライバ CS:0000 BSS、filesize がコードより手前）。 */
		if (r->name[0] && (_stricmp(r->name, "emdr98.bin") == 0
			|| _stricmp(r->name, "zavas98.bin") == 0
			|| _stricmp(r->name, "vd98.bin") == 0
			|| _stricmp(r->name, "vd2_98.bin") == 0))
			packCmd1_ = 1;
		/* gulfwr はブートを 1/000_BOOT にネスト — ベース名で一致 */
		{
			const char* bootBase = r->name;
			const char* slash = strrchr(r->name, '/');
			if (!slash) slash = strrchr(r->name, '\\');
			if (slash) bootBase = slash + 1;
			if (r->name[0] && _stricmp(bootBase, "000_BOOT") == 0) {
			wolfteam98_ = 1;
			/* d_98 オペコード文脈からリロケ済み再生ゲート／フラグ／タイトル BSS を発見。兄弟 MU* ブートは同じ前後バイトだが abs16 が動く（gou 560C/062F/5EFE、zan2 57AA/062F/6E84…）。d_98 専用 0662 補助をリロケブートへ書くと無音を強制し得る。 */
			wolfGateStop_ = 0x5B48;
			wolfGatePlay_ = 0x5B5A;
			wolfSongPtr_ = 0x5B5D;
			wolfSongBuf_ = 0x7E5E;
			wolfTitleWord_ = 0x643A;
			wolfFlagA_ = 0x0662;
			if (data && n > 32) {
				static const uint8_t kPrePlay[] = { 0x85, 0x9D, 0xE8, 0x28, 0x01, 0x2E };
				static const uint8_t kPostPlay[] = { 0x33, 0xC0, 0xC3, 0x9D };
				static const uint8_t kPreStop[] = { 0x5B, 0x58, 0x9D, 0xF8, 0xC3, 0x2E };
				static const uint8_t kPostStop[] = { 0x07, 0x1F, 0x5F, 0x5E };
				/* 古典: OUT 64 / POP ES / POP DS / POPA / IRET。dmdply: OUT 64 / POPA / POP DS / POP ES / IRET */
				static const uint8_t kFlagPost[] = { 0xE6, 0x64, 0x07, 0x1F, 0x61, 0xCF };
				static const uint8_t kFlagPostAlt[] = { 0xE6, 0x64, 0x61, 0x1F, 0x07, 0xCF };
				uint16_t gp = 0, gs = 0;
				for (unsigned p = 6; p + 9 < n; p++) {
					if (data[p] != 0xC6 || data[p + 1] != 0x06) continue;
					if (data[p + 4] == 0xFF
						&& memcmp(data + p - 6, kPrePlay, 6) == 0
						&& memcmp(data + p + 5, kPostPlay, 4) == 0)
						gp = (uint16_t)(data[p + 2] | (data[p + 3] << 8));
					if (data[p + 4] == 0x00
						&& memcmp(data + p - 6, kPreStop, 6) == 0
						&& memcmp(data + p + 5, kPostStop, 4) == 0)
						gs = (uint16_t)(data[p + 2] | (data[p + 3] << 8));
				}
				if (gp)
					wolfGatePlay_ = gp;
				if (gs)
					wolfGateStop_ = gs;
				if (gs)
					wolfSongPtr_ = (uint16_t)(gs + 0x15);
				else if (gp)
					wolfSongPtr_ = (uint16_t)(gp + 0x03);
				/* INT4C 再生武装バイト: C6 06 fa,FF / OUT 64h / … / IRET */
				for (unsigned p = 0; p + 11 < n; p++) {
					if (data[p] != 0xC6 || data[p + 1] != 0x06 || data[p + 4] != 0xFF)
						continue;
					if (memcmp(data + p + 5, kFlagPost, 6) == 0
						|| memcmp(data + p + 5, kFlagPostAlt, 6) == 0) {
						wolfFlagA_ = (uint16_t)(data[p + 2] | (data[p + 3] << 8));
						break;
					}
				}
				/* タイトル語: CMP [tw],AX / JE / MOV [tw],AX / C6 [fa+1],FF */
				for (unsigned p = 0; p + 12 < n; p++) {
					if (data[p] != 0x3B || data[p + 1] != 0x06 || data[p + 4] != 0x74)
						continue;
					if (data[p + 6] != 0xA3 || data[p + 9] != 0xC6 || data[p + 10] != 0x06)
						continue;
					const uint16_t tw = (uint16_t)(data[p + 2] | (data[p + 3] << 8));
					const uint16_t tw2 = (uint16_t)(data[p + 7] | (data[p + 8] << 8));
					const uint16_t fa1 = (uint16_t)(data[p + 11] | (data[p + 12] << 8));
					if (tw == tw2 && fa1 == (uint16_t)(wolfFlagA_ + 1)) {
						wolfTitleWord_ = tw;
						break;
					}
				}
				/* 曲シャドウバッファ: INT 4C 近くの MOV SI/DI,imm（AH=08 経路）。dmdply に INT4C は無い — DI バッファの REP STOSW クリアで検出。 */
				for (unsigned p = 0; p + 12 < n; p++) {
					if (data[p] != 0xBE && data[p] != 0xBF) continue;
					const unsigned imm = (unsigned)data[p + 1] | ((unsigned)data[p + 2] << 8);
					if (imm < 0x6000u || imm > 0xB000u) continue;
					int hasInt4c = 0;
					for (unsigned q = p; q < p + 24 && q + 1 < n; q++) {
						if (data[q] == 0xCD && data[q + 1] == 0x4C) {
							hasInt4c = 1;
							break;
						}
					}
					if (hasInt4c) {
						wolfSongBuf_ = (uint16_t)imm;
						break;
					}
					if (data[p] == 0xBF && p + 9 < n
						&& data[p + 3] == 0xB9
						&& data[p + 6] == 0x2B && data[p + 7] == 0xC0
						&& data[p + 8] == 0xF3 && data[p + 9] == 0xAB) {
						wolfSongBuf_ = (uint16_t)imm;
						break;
					}
				}
			}
			}
		}
	}

	/* biblem2 OPN 双子は MAIN.EXE（CS=6000）をブートし無音のまま。zip は etembl 同一 mdplay.bin も同梱 — 0x600 にステージし ragnrk/etembl 同様 CS=0060 でブート（MDDRV は既に 0x10000、FMV は dataaddr）。 */
	if (mdplay98_ && bootCs_ == 0x6000 && fs) {
		unsigned sz = 0;
		const unsigned char* stub = CEmuZipFsFind(fs, "mdplay.bin", &sz);
		if (stub && sz >= 64 && sz <= 256) {
			memcpy(mem + 0x600, stub, sz < 0x200u ? sz : 0x200u);
			bootCs_ = 0x0060;
			bootIp_ = 0;
		}
	}

	/* DOFMD_98.BIN ブート: INT 45h（MSC init）のあと INT 14h、続けて INT 7Fh をフック。BIOS シリアル stub が無いと INT 14h ベクタは 0000:0000 で INT 7Fh インストールに届かない — 糊の下 0x600 に単独 IRET を置く。 */
	if (dofmd_) {
		mem[0x500] = 0xCF;
		mem[0x14 * 4 + 0] = 0x00;
		mem[0x14 * 4 + 1] = 0x05;
		mem[0x14 * 4 + 2] = 0x00;
		mem[0x14 * 4 + 3] = 0x00;
	}

	/* Wolfteam d_98.bin: CALL 464E / INT 4C AH=08 のあと 7000:0000 のファイル id リストを歩く（LODSB / CMP AL,F9 / INT 4C AH=F0）。本物ゲーム経路は FS マウント（INT 43 AX=8000）後に INT 43 AX=005F でそのリストを載せるが、stub は 80D7→RET でマウントを飛ばす — 7000 はゼロのままブートが永久スピンし INT 7Fh インストールも PIT 許可も届かない。単独 F9 終端を置きループを抜ける。TriggerPlay が PIT + [5B5A] を開始。

	   また CALL 464E → CALL 5A9A がポート E0D0/E0D2 を poll。オープンバス 0xFF は TEST AL,40 でスピン。そのハングは INT 08 インストール後なので stub が OUT 07E8=81 に戻らない。ハンドシェイクを 1 RET に NOP（このヘルパを 5A9A 近くに保つ Wolfteam 000_BOOT ビルド横断 — E0D2 ビジー待ちシグネチャで探す）。 */
	if (wolfteam98_) {
		/* MUSDRV は E0D0 へ標準 MIDI を出す。OPN へブリッジ */
		wolfBridgeEnable_ = 1;
		WolfBridgeReset();
		WolfPlantEmmStub(mem);
		mem[0x70000] = 0xF9;
		/* 464E ハンドシェイクのビジー待ちだけ RET（最初のヒット）。5B00+ の ISR 遅延 stub は残し WOLF_SYNC1=0（非ビジー）。 */
		static const uint8_t kSyncBusy[] = { 0xBA, 0xD2, 0xE0, 0xEC, 0xA8, 0x40, 0x75, 0xFB };
		/* 初期化 poll を RET。5xxx–7xxx は ISR 遅延 stub（d_98 5B00）なので残す。apros は CALL 9A09 が 0x9A09 で待つ。 */
		for (unsigned p = 0x600; p + 8 < 0x5000u; p++) {
			if (memcmp(mem + p, kSyncBusy, sizeof(kSyncBusy)) == 0)
				mem[p] = 0xC3;
		}
		for (unsigned p = 0x8000; p + 8 < 0xB000u; p++) {
			if (memcmp(mem + p, kSyncBusy, sizeof(kSyncBusy)) == 0)
				mem[p] = 0xC3;
		}
		for (unsigned p = 0xF000; p + 8 < 0xF200u; p++) {
			if (memcmp(mem + p, kSyncBusy, sizeof(kSyncBusy)) == 0)
				mem[p] = 0xC3;
		}
		WolfReplantGlueInt7f(mem);
		/* 偶発 MF（suzaku BL50）より明示 MI* バンクを優先 */
		int miBest = -1, miAny = -1;
		for (int fi = 0; fi < fs->fileCount; fi++) {
			const unsigned char* d = fs->files[fi].data;
			const unsigned sz = fs->files[fi].size;
			if (!d || sz < 16) continue;
			if (!(d[0] == 'M' && d[1] == 'F' && d[2] == 0x01 && d[13] == 0x28))
				continue;
			if (miAny < 0) miAny = fi;
			const wchar_t* wfn = fs->files[fi].path;
			const wchar_t* wbase = wfn;
			const wchar_t* wslash = wcsrchr(wfn, L'/');
			if (!wslash) wslash = wcsrchr(wfn, L'\\');
			if (wslash) wbase = wslash + 1;
			/* 本物音色バンク（MM、MD、OPNM）を優先。小さい MI stub（zanyks 0B8_MI01 @1K）が MM01 に勝ってはいけない。 */
			if (wcsstr(wbase, L"_MM") || wcsstr(wbase, L"_MD")
				|| wcsstr(wbase, L"OPNM")
				|| (wcsstr(wbase, L"_MI") && sz >= 2048u)) {
				miBest = fi;
				break;
			}
		}
		const int miFi = miBest >= 0 ? miBest : miAny;
		if (miFi >= 0) {
			const unsigned char* d = fs->files[miFi].data;
			unsigned nMi = fs->files[miFi].size;
			if (0x90000u + nMi > 0x200000u) nMi = 0x200000u - 0x90000u;
			memcpy(mem + 0x90000, d, nMi);
			wolfMiSeg_ = 0x9000;
		} else {
			/* apros は曲だけ同梱 — INT4C AH=00／バンク init が偽でない音色ブロックを持つよう最小 MF ヘッダを植える。 */
			static const uint8_t kMinMf[] = {
				'M', 'F', 0x01, 0x00, 0x20, 0x00, 0x00, 0x00,
				0x18, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00
			};
			memcpy(mem + 0x90000, kMinMf, sizeof(kMinMf));
			wolfMiSeg_ = 0x9000;
		}
	}

	PlantPc98BiosMap(mem);
	PlantPc98BiosTimer(mem);
	if (fmd98_) {
		/* FMD は BIOS 501 bit7=0 でリロード 5120 を書く。8253 入力は
		   14.7456 MHz/6 = 2.4576 MHz（CPU/4 の 1.9968 ではない）。
		   1996800/5120 ≈ 390 Hz、曲 tick = 390×[17d6]/600。
		   ED400 は [17d6]=0x46 → 45.5 Hz。501 bit7 を偽って 4160 にすると
		   2457600/4160 ≈ 591 Hz → 69 Hz で少し速くなる。
		   5120 のまま 2.4576 MHz なら 480 Hz → 56 Hz。 */
		pitClockHz_ = PC98_PIT_CLOCK_5MHZ_HZ;
	}
	if (!pitRunning_) {
		pitReload_ = (uint16_t)(pitClockHz_ / 60);
		if (pitReload_ == 0) pitReload_ = 1;
		pitCounter_ = pitReload_;
		pitRunning_ = 1;
		pitIrqPending_ = 0;
		pitResidual_ = 0;
	}

	/* QueenSoft MADP: INT40 は AL 添字 API（OPN ISR ではない）。ブート糊 INT40 AL=19 は YM 組の前に ES:[0501] bit3（FM あり）を TEST — CPU 開始前に植える。再生 INT7F は cmd→AL=1D/1B。 */
	if (madp98_)
		mem[0x501] = (uint8_t)(mem[0x501] | 0x08);

	/* SORC98（と同種 bootcs stub）: CALL BIOS init のあと INT 18h（AH=98h）でアイドル。カタログ BIOS は INT 18 をフックせずベクタは 0000:0000 のまま。最初のアイドル反復が IVT をコード実行しハンドラセグメントを FFFF にする。CPU 開始前に 0x510 へ IRET。 */
	{
		mem[0x510] = 0xCF;
		mem[0x18 * 4 + 0] = 0x10;
		mem[0x18 * 4 + 1] = 0x05;
		mem[0x18 * 4 + 2] = 0x00;
		mem[0x18 * 4 + 3] = 0x00;
		/* 1 ベクタ先も同じ罠: リップにフロッピーは無く、ディスク BIOS を呼ぶドライバ（Telenet VIS は開始前に 256 バイトセクタを読む）はさもなくばゴミ IVT をコード実行。「エラー無し」を報告 — CF は呼び出し側の push 済みフラグでクリア。失敗読を致命ディスクエラーと扱うため。 */
		static const uint8_t kDiskStub[] = {
			0x55,                    /* push bp                */
			0x89, 0xE5,              /* mov  bp,sp             */
			0x83, 0x66, 0x06, 0xFE,  /* and word [bp+6],0FFFE（フラグ CF クリア） */
			0x5D,                    /* pop  bp                */
			0x30, 0xE4,              /* xor  ah,ah             */
			0xCF                     /* iret                   */
		};
		memcpy(mem + 0x512, kDiskStub, sizeof(kDiskStub));
		mem[0x1B * 4 + 0] = 0x12;
		mem[0x1B * 4 + 1] = 0x05;
		mem[0x1B * 4 + 2] = 0x00;
		mem[0x1B * 4 + 3] = 0x00;
	}

	/* Falcom 00BIOS / PR.*（ys3/xana2/…、カタログ dummysndrom=1）: 2 回目のブート CALL が A000:3FEE と word [0536] を読み、導出フラグ [0702] が非 0 のときだけ FM init を走る。ゼロ RAM は OPN セットアップを全部飛ばす（INT51/52 は inert、opnW=0）。プローブが見る BIOS 装備ビット（[0536] bit2）を植え、音源基板あり機と同じく FM init が走る — hoot の dummysndrom と同じ役割。
	   xana2 PR.NO0/PR.NO5/xana2e は A000:0FEE bit3 も必須。無いと no-FM 経路を取り INT14/OPN を植えない。ys3 用に bit0 を残す。 */
	if (dummySndRom_ || (bootCs_ == 0 && bootIp_ == 0x0600)) {
		mem[0x536] = (uint8_t)(mem[0x536] | 0x04);
		if (0xA0000u + 0x3FEEu < 0x200000u)
			mem[0xA0000u + 0x3FEEu] = (uint8_t)(mem[0xA0000u + 0x3FEEu] | 0x09);
	}

	np2_reset();
	np2_set_adrsmask(0x000FFFFFu);
	np2_setextsize(0);
	np2_set_v30(0);

	/* bootip があるとき bootcs=0 を尊重（SORC98: CS=0000 IP=F000 → 物理 0xF000）。bootcs と bootip が両方未設定／0 のときだけ CS 既定を 0x60。 */
	uint16_t cs = (uint16_t)((bootCs_ != 0 || bootIp_ != 0) ? bootCs_ : 0x0060);
	uint16_t ip = (uint16_t)bootIp_;
	np2_set_cs_ip(cs, ip);
	np2_set_ss_sp(0x1000, 0xFFFE);
	np2_reg_set(NP2_R_DS, cs);
	np2_reg_set(NP2_R_ES, cs);
	np2_reg_set(NP2_R_FLAGS, 0x0202); /* IF セット（割り込み許可） */

	extSong_ = (uint16_t)(titleCode & 0xffff);
	extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);
	extCmd_ = 0;
	stubState_ = 0;
	cpuCycles_ = 0;
	opnPumpResidual_ = 0;
	picMask_ = 0x00; /* PIC を組まない bootcs ドライバ用に全マスク解除 */
	slavePicMask_ = 0x00;
	opnInService_ = 0;
	g_opnIsrSs = 0;
	g_opnIsrSp = 0;
	g_pitInService = 0;
	g_pitIsrSs = 0;
	g_pitIsrSp = 0;
	irqEdgeSeen_ = 0;
	irqEdgeConsumed_ = 0;
	return 1;
}

/* DosStripHash の実装 */
static void DosStripHash(const char* in, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!in) return;
	int j = 0;
	int atTok = 1;
	for (int i = 0; in[i] && j < outCap - 1; i++) {
		const char c = in[i];
		if (c == '#' && atTok) continue;
		out[j++] = c;
		atTok = (c == ' ' || c == '\t') ? 1 : 0;
	}
	out[j] = 0;
}

/* DosSplitCmd の実装 */
static void DosSplitCmd(const char* cmd, char* name, int nameCap, char* tail, int tailCap)
{
	if (name && nameCap > 0) name[0] = 0;
	if (tail && tailCap > 0) tail[0] = 0;
	if (!cmd) return;
	while (*cmd == ' ' || *cmd == '\t') cmd++;
	int i = 0;
	while (cmd[i] && cmd[i] != ' ' && cmd[i] != '\t') i++;
	if (name && nameCap > 0) {
		int n = i;
		if (n >= nameCap) n = nameCap - 1;
		memcpy(name, cmd, (size_t)n);
		name[n] = 0;
	}
	const char* t = cmd + i;
	while (*t == ' ' || *t == '\t') t++;
	if (tail && tailCap > 0)
		strncpy_s(tail, (size_t)tailCap, t, _TRUNCATE);
}

/* CONFIG `mmd.sys /f12 4096` — `/f` はスイッチでありパス区切りではない。stem は最初の空白トークン。そのあと最後の \\/: だけ取る。 */
static void DosCfgFileStem(const char* in, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!in) return;
	const char* end = in;
	while (*end && *end != ' ' && *end != '\t')
		end++;
	const char* base = in;
	for (const char* p = in; p < end; p++) {
		if (*p == '\\' || *p == '/' || *p == ':')
			base = p + 1;
	}
	int n = (int)(end - base);
	if (n <= 0) return;
	if (n >= outCap) n = outCap - 1;
	memcpy(out, base, (size_t)n);
	out[n] = 0;
}

/* DosIsEngineName の実装 */
static int DosIsEngineName(const char* name)
{
	char stem[DOS98_NAME];
	DosCfgFileStem(name, stem, (int)sizeof(stem));
	if (!stem[0]) return 0;
	const char* ext = strrchr(stem, '.');
	if (!ext) return 0;
	return _stricmp(ext, ".EXE") == 0
		|| _stricmp(ext, ".COM") == 0
		|| _stricmp(ext, ".DRV") == 0
		|| _stricmp(ext, ".SYS") == 0;
}

/* CHardPc98::MaterializeDosFiles の実装 */
void CHardPc98::MaterializeDosFiles(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge) return;
	char preferDir[32];
	preferDir[0] = 0;
	int rootHits = 0, dirHits = 0, multiDir = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "device") != 0)
			continue;
		if (!r->name || DosIsEngineName(r->name))
			continue;
		const char* slash = NULL;
		for (const char* p = r->name; *p && *p != ' ' && *p != '\t'; p++) {
			if (*p == '/' || *p == '\\')
				slash = p;
		}
		if (!slash) {
			rootHits++;
			continue;
		}
		dirHits++;
		char cur[32];
		int k = 0;
		for (const char* p = r->name; p < slash && k < 31; p++)
			cur[k++] = *p;
		cur[k] = 0;
		if (!preferDir[0])
			memcpy(preferDir, cur, sizeof(cur));
		else if (_stricmp(cur, preferDir) != 0)
			multiDir = 1;
	}
	const char* pref = "";
	if (rootHits == 0 && dirHits > 0 && !multiDir && preferDir[0])
		pref = preferDir;
	unsigned char* donorBuf = (unsigned char*)malloc(256u * 1024u);
	unsigned donorCap = donorBuf ? (256u * 1024u) : 0;
	/* エンジンを先に。256+ 曲リストが glue COM/EXE コピー前に files_[] を埋めないように（night_s USMD: 132 ファイル + 130 conin）。 */
	for (int pass = 0; pass < 2; pass++) {
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "conin") != 0
			&& _stricmp(r->type, "device") != 0)
			continue;
		const int eng = DosIsEngineName(r->name);
		if (pass == 0 && !eng) continue;
		if (pass == 1 && eng) continue;
		unsigned sz = 0;
		char stem[96];
		DosCfgFileStem(r->name, stem, (int)sizeof(stem));
		char fileDir[32];
		fileDir[0] = 0;
		if (r->name) {
			const char* slash = NULL;
			for (const char* p = r->name; *p && *p != ' ' && *p != '\t'; p++) {
				if (*p == '/' || *p == '\\')
					slash = p;
			}
			if (slash) {
				int k = 0;
				for (const char* p = r->name; p < slash && k < 31; p++)
					fileDir[k++] = *p;
				fileDir[k] = 0;
			}
		}
		const char* usePref = fileDir[0] ? fileDir : pref;
		/* stem を先に: `MMD2.SYS 4096` は CONFIG 文字列を ZipFsFind し、拡張子無しフォールバックが mmd2.com（同じ stem、先の zip メンバ）を返し type=file SYS イメージを壊した。 */
		const unsigned char* data = NULL;
		if (stem[0])
			data = CEmuZipFsFindDir(fs, stem, &sz, usePref);
		if ((!data || !sz) && r->name && r->name[0]
			&& (!stem[0] || strcmp(stem, r->name) != 0))
			data = CEmuZipFsFindDir(fs, r->name, &sz, usePref);
		unsigned donorSz = 0;
		/* 曲のみ zip（gdm_mo/guyna/kizuato/nekoex）はカタログに PMD_98.COM があっても省略 — 兄弟パックからドライバを取る。 */
		if ((!data || !sz) && donorBuf && fs->zipPath[0] && DosIsEngineName(r->name)) {
			char base[DOS98_NAME];
			DosCfgFileStem(r->name, base, (int)sizeof(base));
			static const wchar_t* kDonors[] = {
				L"xenon_98.zip", L"eveppz8_98.zip", L"chobaku_98.zip",
				L"frnunv98.zip", NULL
			};
			wchar_t donorPath[MAX_PATH];
			wcsncpy_s(donorPath, fs->zipPath, _TRUNCATE);
			wchar_t* slash = wcsrchr(donorPath, L'\\');
			if (!slash) slash = wcsrchr(donorPath, L'/');
			if (slash) {
				for (int d = 0; kDonors[d]; d++) {
					wcscpy_s(slash + 1, _countof(donorPath) - (slash + 1 - donorPath),
						kDonors[d]);
					if (CEmuZipFsExtractOne(donorPath, base, donorBuf,
						donorCap, &donorSz) && donorSz > 0) {
						data = donorBuf;
						sz = donorSz;
						break;
					}
				}
			}
		}
		if (!data || !sz) continue;
		char addName[DOS98_NAME];
		DosCfgFileStem(r->name, addName, (int)sizeof(addName));
		if (addName[0])
			dos_.AddFile(addName, data, sz);
	}
	}
	free(donorBuf);
}

/* CHardPc98::BindDosRomHandles の実装 */
void CHardPc98::BindDosRomHandles(const CEmuGameEntry* ge)
{
	if (!ge) return;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "conin") != 0)
			continue;
		const int off = r->offset;
		/* ハンドルは DOS98_HANDLE_MAX-1 まで（fc98v12 曲は 0x30 超） */
		if (off < 0 || off >= DOS98_HANDLE_MAX) continue;
		const char* base = r->name;
		for (const char* p = r->name; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		if (_stricmp(r->type, "conin") == 0) {
			dos_.SetHandleText((uint16_t)off, base);
			/* hoot conin@0x10 は stdin（AH=3F BX=0）であり DOS ハンドル 0x10 ではない。cplay はまだ上のタイトル番号ハンドルをバインド。 */
			if (off == 0x10)
				dos_.SetHandleText(0, base);
		} else
			dos_.SetHandle((uint16_t)off, base);
	}
}

/* DosShellStarts の実装 */
static int DosShellStarts(const CEmuGameEntry* ge, const char* const* prefixes)
{
	if (!ge || !prefixes) return 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "shell") != 0) continue;
		const char* n = ge->rom[i].name;
		if (!n || !n[0]) continue;
		for (int p = 0; prefixes[p]; p++) {
			const size_t plen = strlen(prefixes[p]);
			if (_strnicmp(n, prefixes[p], plen) == 0)
				return 1;
		}
	}
	return 0;
}

/* FmxDosShell の実装 */
static int FmxDosShell(const CEmuGameEntry* ge)
{
	static const char* kFmx[] = { "FMX", "fmx", NULL };
	return DosShellStarts(ge, kFmx);
}

/* FMX 3.10（lemmona Ver3.10L）は CS:3660 にワンショット INT08 を植え、PIT を測って OUT 02h |= 1。BootDos で IRQ0 はマスク（0xFF）なのでその ISR は走らず、本物シーケンサ CS:1D60 への入替が起きず、再生は keyOn=0 の FM_TONE ダンプ。FMX シェルでは IRQ0 を生かす。FMX 3.91（v_btr CS:2470）は IRQ0 マスクのまま既に PLAYS — シェル後に keep を落としテンポ／窓を当時のまま。 */
static void FmxArmPitIrq0(const CEmuGameEntry* ge, int dropIf391)
{
	s_fmxKeepIrq0 = 0;
	if (!FmxDosShell(ge))
		return;
	s_fmxKeepIrq0 = 1;
	if (!dropIf391)
		return;
	uint8_t* mem = np2_mem();
	if (!mem)
		return;
	const unsigned off = (unsigned)mem[0x08 * 4]
		| ((unsigned)mem[0x08 * 4 + 1] << 8);
	if (off == 0x2470)
		s_fmxKeepIrq0 = 0;
}

/* FmxKick310Play の実装 */
static void FmxKick310Play(uint8_t* mem, CEmuDos98* dos, const char* song)
{
	if (!mem || !s_fmxKeepIrq0)
		return;
	const unsigned off = (unsigned)mem[0x08 * 4]
		| ((unsigned)mem[0x08 * 4 + 1] << 8);
	const unsigned seg = (unsigned)mem[0x08 * 4 + 2]
		| ((unsigned)mem[0x08 * 4 + 3] << 8);
	if (off != 0x1D60 || !seg || seg == (unsigned)DOS98_TRAMP_SEG)
		return;
	const unsigned b = seg << 4;
	if (b + 0x2EC2u >= 0x200000u)
		return;
	if (dos && song && song[0]) {
		const CEmuDos98File* f = dos->FindFile(song);
		if (f && f->data && f->size) {
			unsigned n = f->size;
			if (b + 0x2EC0u + n > 0x200000u)
				n = 0x200000u - (b + 0x2EC0u);
			memcpy(mem + b + 0x2EC0, f->data, n);
		}
	}
	mem[b + 0x281C] = 0;
	mem[b + 0x2849] = 2;
	/* 18E5/cmd16 は [2868] を曲基点として歩く。ファイル BSS は init `MOV [2868],2EC0` @CS:3398 まで 0。Kick バッファへ強制。 */
	mem[b + 0x2868] = 0xC0;
	mem[b + 0x2869] = 0x2E;
	/* Init 30E2 は YM2608 188h/18Ah をここに格納。ファイル BSS は 0 なので 1A40 OUT DX,[2862] がポート 0（PIC）に当たり、cmd16 がチップに届かない。 */
	mem[b + 0x2860] = 0x88;
	mem[b + 0x2861] = 0x01;
	mem[b + 0x2862] = 0x8A;
	mem[b + 0x2863] = 0x01;
	/* INT08 1D60 は beep PIT シーケンス（1F08 / OUT 37h）。FM は INT14 1BB4（AH=25 AL=[0118]=14h）が YM Timer B で 1D88 を呼ぶ。 */
	if (mem[b + 0x1BB4] == 0x60) {
		mem[0x14 * 4 + 0] = 0xB4;
		mem[0x14 * 4 + 1] = 0x1B;
		mem[0x14 * 4 + 2] = (uint8_t)(seg & 0xff);
		mem[0x14 * 4 + 3] = (uint8_t)(seg >> 8);
	}
	mem[b + 0x281B] = (uint8_t)(mem[b + 0x281B] | 1u);
	mem[b + 0x281F] = 0x80;
	/* FMXP.COM INT 60: ディスパッチャ `MOV AX,[BP+12]` は既に AX（PUSHA/DS/ES）。それを [BP+18]（FLAGS）にパッチしない。 */
	mem[0x60 * 4 + 0] = 0x4E;
	mem[0x60 * 4 + 1] = 0x16;
	mem[0x60 * 4 + 2] = (uint8_t)(seg & 0xff);
	mem[0x60 * 4 + 3] = (uint8_t)(seg >> 8);
}

/* Fmx310EnableYmTimer の実装 */
static void Fmx310EnableYmTimer(CChip* chip)
{
	if (!chip)
		return;
	/* 3310: YM 24h=5、25h=0、27h=3Fh（Timer A+B ロード/IRQ）。FM seq 1D88 は Timer B（status bit1）。bit1 が実際に上がるよう 26h も組む。 */
	chip->Write(0, 0x25);
	chip->Write(1, 0x00);
	chip->Write(0, 0x24);
	chip->Write(1, 0x05);
	chip->Write(0, 0x26);
	chip->Write(1, 0xC0);
	chip->Write(0, 0x27);
	chip->Write(1, 0x3F);
}

/* FmxPlantInt60FromPit の実装 */
static void FmxPlantInt60FromPit(uint8_t* mem)
{
	if (!mem || !s_fmxKeepIrq0)
		return;
	const unsigned off = (unsigned)mem[0x08 * 4]
		| ((unsigned)mem[0x08 * 4 + 1] << 8);
	const unsigned seg = (unsigned)mem[0x08 * 4 + 2]
		| ((unsigned)mem[0x08 * 4 + 3] << 8);
	if (off != 0x1D60 || !seg || seg == (unsigned)DOS98_TRAMP_SEG)
		return;
	mem[0x60 * 4 + 0] = 0x4E;
	mem[0x60 * 4 + 1] = 0x16;
	mem[0x60 * 4 + 2] = (uint8_t)(seg & 0xff);
	mem[0x60 * 4 + 3] = (uint8_t)(seg >> 8);
}

/* Fmx310ArmSeq の実装 */
static void Fmx310ArmSeq(uint8_t* mem, uint8_t latch281c, uint16_t ax)
{
	if (!mem || !s_fmxKeepIrq0)
		return;
	const unsigned off = (unsigned)mem[0x08 * 4]
		| ((unsigned)mem[0x08 * 4 + 1] << 8);
	const unsigned seg = (unsigned)mem[0x08 * 4 + 2]
		| ((unsigned)mem[0x08 * 4 + 3] << 8);
	if (off != 0x1D60 || !seg || seg == (unsigned)DOS98_TRAMP_SEG)
		return;
	const unsigned b = seg << 4;
	if (b + 0x2EC2u >= 0x200000u)
		return;
	/* 曲は CS:2EC0（Kick memcpy / FMXP AH=3F）。cmd16 186F: 22C7 / 1A40 経由の音色（[2860]=188h が要る）。cmd21 18E5: ISR 2A40 ポインタを埋め 196D が PIT beep を武装。cmd21 後 [2822] をクリアし INT08 1F08 を静かにする。FM は INT14。 */
	if (mem[b + 0x2EC0] == 0xff && mem[b + 0x2EC1] == 0xff)
		return;
	mem[b + 0x281C] = latch281c;
	mem[b + 0x2868] = 0xC0;
	mem[b + 0x2869] = 0x2E;
	FmxPlantInt60FromPit(mem);
	np2_reg_set(NP2_R_FLAGS,
		(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
	np2_reg_set(NP2_R_DS, (uint16_t)seg);
	np2_reg_set(NP2_R_ES, (uint16_t)seg);
	np2_reg_set(NP2_R_AX, ax);
	np2_reg_set(NP2_R_BX, ax);
	np2_interrupt(0x60);
	if (ax == 0x1500)
		mem[b + 0x2822] = 0;
}

/* Fmx310Int60Play の実装 */
static int Fmx310Int60Play()
{
	uint8_t* mem = np2_mem();
	if (!mem || !s_fmxKeepIrq0)
		return 0;
	const unsigned off08 = (unsigned)mem[0x08 * 4]
		| ((unsigned)mem[0x08 * 4 + 1] << 8);
	if (off08 != 0x1D60)
		return 0;
	/* 3.10 INT 60 は BH（保存 BX）。Kick はそれを AH にパッチ。AX=0 が再生 */
	np2_reg_set(NP2_R_FLAGS,
		(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
	np2_reg_set(NP2_R_DX, 0x2EC0);
	np2_reg_set(NP2_R_AX, 0);
	np2_reg_set(NP2_R_BX, 0);
	np2_interrupt(0x60);
	return 1;
}

/* ValkyWantArm の実装 */
static int ValkyWantArm(const CEmuGameEntry* ge, CEmuDos98* dos)
{
	static const char* kValky[] = { "VALKY_98", "valky", NULL };
	if (DosShellStarts(ge, kValky))
		return 1;
	if (dos && (dos->FindFile("VALKY_98.COM") || dos->FindFile("VALKY_98")))
		return 1;
	return 0;
}

static void ValkyReplantIsr(uint8_t* mem);
static void ValkyFixFarApiFromGlue(uint8_t* mem);

/* 名前ロード ADVH（EB 06 USDdrv、"03 30" 無し）: インストールは bind/data 用に mov ax,CS+0x33 を書くが OEM ISR は mov ds,cs のまま。watagolf は +0x330 オフセットリロケを終える（ISR 約 069B）。名前ロードはしない。BootDos で bind 経路即値（off>=0x800）をライブ INT F1 CS へ付け替え INT0B を植える。早い CS+0x33 参照（@034B/@0442）は残す — AL=0 が要る。AL=1 はまだ @04AB でチャネル枠を歩くので、TriggerPlay が ISR プロローグを保存／復元。 */
static int AdvhNormalizeNameLoadResident(uint8_t* mem, unsigned sF1)
{
	if (!mem || !sF1 || sF1 == (unsigned)DOS98_TRAMP_SEG)
		return 0;
	const unsigned fb = sF1 << 4;
	const unsigned ent = fb + ((unsigned)mem[0xF1 * 4] | ((unsigned)mem[0xF1 * 4 + 1] << 8));
	if (ent + 10 >= 0x200000u)
		return 0;
	if (!(mem[ent] == 0xEB && mem[ent + 1] == 0x06 && mem[ent + 2] == 'U'))
		return 0;
	const unsigned wrong = sF1 + 0x33u;
	int nBind = 0;
	for (unsigned off = 0x800; off + 3 < 0x5000u && fb + off + 3 < 0x200000u; off++) {
		if (mem[fb + off] != 0xB8)
			continue;
		const unsigned imm = (unsigned)mem[fb + off + 1]
			| ((unsigned)mem[fb + off + 2] << 8);
		if (imm != wrong)
			continue;
		mem[fb + off + 1] = (uint8_t)(sF1 & 0xff);
		mem[fb + off + 2] = (uint8_t)((sF1 >> 8) & 0xff);
		nBind++;
	}
	if (nBind < 2)
		return 0;
	/* インストールバイトを +0x330 へ memcpy しない（名前ロード BSS を壊す）。INT0B はまだ植えない: 薄い AL=1 チャネル状態＋早い OEM ISR が bind ノートをキーオフ。bind→CS 付け替えだけで可聴 AL=1 が戻る。 */
	return 1;
}

/* AdvhApplyResidentFixups の実装 */
static void AdvhApplyResidentFixups(uint8_t* mem, unsigned sF1)
{
	const unsigned fb = sF1 << 4;
	for (unsigned off = 0; off + 5 < 0x5000u && fb + off + 5 < 0x200000u; off++) {
		if (mem[fb + off] == 0x9A) {
			unsigned to = (unsigned)mem[fb + off + 1] | ((unsigned)mem[fb + off + 2] << 8);
			unsigned ts = (unsigned)mem[fb + off + 3] | ((unsigned)mem[fb + off + 4] << 8);
			int d = (int)ts - (int)sF1;
			if (d < 0) d = -d;
			if (d > 0 && d < 0x100) {
				mem[fb + off + 3] = (uint8_t)(sF1 & 0xff);
				mem[fb + off + 4] = (uint8_t)((sF1 >> 8) & 0xff);
				ts = sF1;
			}
			if (ts == sF1 && to >= 0x600 && to < 0xA00) {
				const unsigned neu = to + 0x330;
				if (neu < 0x2000 && fb + neu + 2 < 0x200000u) {
					const uint8_t lo = mem[fb + to];
					const uint8_t hi = mem[fb + neu];
					const int loCode = (lo == 0x55 || lo == 0x50 || lo == 0x60 || lo == 0xFC || lo == 0xE8);
					const int hiCode = (hi == 0x55 || hi == 0x50 || hi == 0x60 || hi == 0xFC
						|| hi == 0xE8 || hi == 0xBB || hi == 0x8B || hi == 0x33);
					if ((!loCode && hiCode) || (to >= 0x6D0 && to < 0x900)) {
						mem[fb + off + 1] = (uint8_t)(neu & 0xff);
						mem[fb + off + 2] = (uint8_t)((neu >> 8) & 0xff);
					}
				}
			}
		}
		/* CS: jmp [reg+disp16] → コマンド表 1340/1380（+0x330） */
		if (mem[fb + off] == 0x2E && mem[fb + off + 1] == 0xFF
			&& (mem[fb + off + 2] == 0xA5 || mem[fb + off + 2] == 0x95)) {
			const unsigned a = (unsigned)mem[fb + off + 3] | ((unsigned)mem[fb + off + 4] << 8);
			if (a == 0x1340u || a == 0x1380u) {
				const unsigned neu = a + 0x330;
				mem[fb + off + 3] = (uint8_t)(neu & 0xff);
				mem[fb + off + 4] = (uint8_t)((neu >> 8) & 0xff);
			}
		}
	}
	/* リロケ前島への Abs16 データ参照（ゲート、フラグ）→ +0x330 */
	for (unsigned off = 0x200; off + 4 < 0x2800u && fb + off + 4 < 0x200000u; off++) {
		const uint8_t b0 = mem[fb + off];
		if (b0 == 0xA0 || b0 == 0xA2 || b0 == 0xA1 || b0 == 0xA3) {
			const unsigned a = (unsigned)mem[fb + off + 1] | ((unsigned)mem[fb + off + 2] << 8);
			if (a < 0x100) {
				const unsigned neu = a + 0x330;
				mem[fb + off + 1] = (uint8_t)(neu & 0xff);
				mem[fb + off + 2] = (uint8_t)((neu >> 8) & 0xff);
			}
		}
		if ((b0 == 0xF6 || b0 == 0xF7) && (mem[fb + off + 1] & 0xC7) == 0x06) {
			const unsigned a = (unsigned)mem[fb + off + 2] | ((unsigned)mem[fb + off + 3] << 8);
			if (a < 0x100) {
				const unsigned neu = a + 0x330;
				mem[fb + off + 2] = (uint8_t)(neu & 0xff);
				mem[fb + off + 3] = (uint8_t)((neu >> 8) & 0xff);
			}
		}
		if ((b0 == 0x8B || b0 == 0x89 || b0 == 0x80 || b0 == 0x81 || b0 == 0x83
			|| b0 == 0xC6 || b0 == 0xC7 || b0 == 0x8A || b0 == 0x88
			|| b0 == 0xFF || b0 == 0xFE)
			&& (mem[fb + off + 1] & 0xC7) == 0x06) {
			const unsigned a = (unsigned)mem[fb + off + 2] | ((unsigned)mem[fb + off + 3] << 8);
			if (a < 0x100) {
				const unsigned neu = a + 0x330;
				mem[fb + off + 2] = (uint8_t)(neu & 0xff);
				mem[fb + off + 3] = (uint8_t)((neu >> 8) & 0xff);
			}
		}
	}
}

/* olteus MAP は `A:\MUSIC F.MUS` / `.MTB` を保つ（空白 = 既定）。数字 poke は LoadExe 前の VFS EXE と RAM の両方。01 vs 02 が別ファイルを開く。 */
static void CEmuPc98PokeOlteusMusicName(uint8_t* p, unsigned n, char dig, char fp)
{
	if (!p || n < 12)
		return;
	for (unsigned i = 0; i + 11u < n; i++) {
		if (p[i] != 'M' || p[i + 1] != 'U' || p[i + 2] != 'S'
			|| p[i + 3] != 'I' || p[i + 4] != 'C')
			continue;
		const uint8_t d = p[i + 5];
		const uint8_t s = p[i + 6];
		if (p[i + 7] != '.')
			continue;
		if (!(d == ' ' || (d >= '0' && d <= '9') || (d >= 'A' && d <= 'E')))
			continue;
		if (!(s == 'F' || s == 'P' || s == ' '))
			continue;
		p[i + 5] = (uint8_t)dig;
		p[i + 6] = (uint8_t)fp;
	}
}

/* CHardPc98::SelectedDosSong の実装 */
const char* CHardPc98::SelectedDosSong(const CEmuGameEntry* ge, unsigned titleCode) const
{
	if (!ge) return NULL;
	/* olteus_va: 全曲は file@-1。MAP はタイトルから A:\MUSIC#F/P.MUS を組む */
	static char olteusSong[16];
	static const char* kOlteusSong[] = { "olteus", NULL };
	if (DosShellStarts(ge, kOlteusSong)) {
		const unsigned n = titleCode & 0x0fu;
		const char dig = (n < 10) ? (char)('0' + n) : (char)('A' + (n - 10));
		const char fp = opnaMode ? 'F' : 'P';
		_snprintf_s(olteusSong, _TRUNCATE, "MUSIC%c%c.MUS", dig, fp);
		return olteusSong;
	}
	const int low = (int)(titleCode & 0xff);
	const int full = (int)titleCode;
	const int hi = (int)((titleCode >> 8) & 0xff);
	for (int pass = 0; pass < 3; pass++) {
		const int want = pass == 0 ? full : (pass == 1 ? low : hi);
		if (pass == 2 && (hi == 0 || hi == low || titleCode <= 0xffu))
			continue;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "file") != 0) continue;
			if (r->offset != want) continue;
			const char* base = r->name;
			for (const char* p = r->name; *p; p++) {
				if (*p == '\\' || *p == '/' || *p == ':')
					base = p + 1;
			}
			return base;
		}
	}
	/* cplay98/mdrv は曲バンクを conin（offset == タイトル下位バイト）として列挙 */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "conin") != 0) continue;
		if (r->offset != low) continue;
		const char* base = r->name;
		for (const char* p = r->name; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		if (DosIsEngineName(base))
			continue;
		return base;
	}
	/* Bio_100%/BGML_98 等: 共有バンク 1 本が conin@0x10（hoot stdin）＋ type=file@-1。タイトルコードがそのバンク内トラックを選ぶので、rom offset がタイトルバイトと等しいものは無い。 */
	{
		int nConin = 0;
		const char* only = NULL;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "conin") != 0) continue;
			const char* base = r->name;
			for (const char* p = r->name; *p; p++) {
				if (*p == '\\' || *p == '/' || *p == ':')
					base = p + 1;
			}
			if (DosIsEngineName(base))
				continue;
			nConin++;
			only = base;
		}
		if (nConin == 1 && only)
			return only;
	}
	return NULL;
}

/* SYNTH_98 / HHD 等: rom offset 5 は曲ではなくオーバーレイ（S20.BIN）。オーバーレイが TSR したあと TriggerPlay でハンドル 5 を PAI に再バインドしても害は無いが、まだ走る AH=3F BX=5 用にカタログマップを残す。 */
static int DosHandleBoundToOtherFile(const CEmuGameEntry* ge, int handle, const char* songFile)
{
	if (!ge || handle < 0)
		return 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (r->offset != handle)
			continue;
		if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "voice") != 0)
			continue;
		const char* base = r->name ? r->name : "";
		for (const char* p = base; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		if (songFile && _stricmp(base, songFile) == 0)
			return 0;
		return 1;
	}
	return 0;
}

/* CHardPc98::BindDosTriggerSong の実装 */
void CHardPc98::BindDosTriggerSong(const CEmuGameEntry* ge, unsigned titleCode)
{
	const char* sf = SelectedDosSong(ge, titleCode);
	/* hootrip: cplay/fplay は ASCIIZ 名で開く。mdrv_98/mddrv_98 も同じ（INT D2 AL=2）。素の mdrv98+mlp_hoot（内容はハンドル 0）には一致させない。 */
	static const char* kCplay[] = { "cplay", "fplay", NULL };
	/* mlalf_98 は意図的に不在: INT 7F cmd0 はハンドル 0 を読みバッファを ANNEX ドライバへ渡す。ドライバは `CMP WORD ES:[SI],1` で始まる — 全 .MLO 曲は 01 00 始まりなので曲バイトが欲しく、ファイル名は outright 拒否。 */
	/* PLAY5_98 はここに居ない: INT7F cmd0 はハンドル 0 を AH=3F 読してバッファへ、INT F2 AX=0 がそのバイトをロード。ハンドル 0 のファイル名テキストは PLAY5/PLAY3/MUSIC + PLAY5_98 を無音にした。IBGMP.COM も同じ: cmd0 がハンドル 0 を AH=3F 読して INT52 AX=200。接頭 "ibgm" は IBGMP にも一致するので名前開きしてはいけない。 */
	static const char* kOpenName[] = {
		"cplay", "fplay", "musdrv", "mbmusp", "mdrv_9", "mddrv_9",
		"bp",
		/* mlfplay は意図的に不在: INT7F cmd0 は AH=3F BX=0 CX=FFFF で曲バイトを読み INT60 AH=0。
		   ハンドル 0 を ASCIIZ 名にすると E1.OBJ の 7 バイトだけ渡り keyOn=3 で止まる。 */
		/* ABIKO_98 cmd0: AH=3F ハンドル 0 を 0168 へ読み INT40 AH=4 がその ASCIIZ を fopen。曲バイトだと最初の 0 までを名前にして Open 失敗、AH=2 は [998D]=0 で帰る。 */
		"ABIKO", "abiko",
		NULL
	};
	static const char* kBgmlSong[] = { "BGML_98", "bgml", NULL };
	static const char* kN3gvSeek[] = { "n3gv2", "N3GV2", NULL };
	static const char* kLudyMagic[] = {
		"LUDY", "ludy", "SCBIOS",
		"MAGIC_98", "magic_98", "MAGIC_", "magic_",
		NULL
	};
	static const char* kExtParamVoice[] = {
		"mmd2", "MMD2", "mmd2va",
		"iwaplay", "IWAPLAY",
		NULL
	};
	static const char* kElfMus[] = {
		"ELFMUS98", "elfmus", "ELFMUS",
		NULL
	};
	const int cplayFamily = DosShellStarts(ge, kCplay);
	int opensByName = cplayFamily || DosShellStarts(ge, kOpenName);
	/* famistava conin: INT7F AH=3F がハンドル 0 から ASCIIZ 名を読み、AH=3D が本物ファイルを開く — 曲バイトで上書きしてはいけない。 */
	if (sf && ge && !opensByName) {
		const int low = (int)(titleCode & 0xff);
		int nConin = 0, stdinConin = 0;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "conin") != 0) continue;
			const char* base = r->name;
			for (const char* p = r->name; *p; p++) {
				if (*p == '\\' || *p == '/' || *p == ':')
					base = p + 1;
			}
			if (DosIsEngineName(base))
				continue;
			nConin++;
			if (r->offset == 0x10 || r->offset == 0)
				stdinConin = 1;
			if (r->offset != low) continue;
			if (_stricmp(base, sf) == 0) {
				opensByName = 1;
				break;
			}
		}
		/* 共有バンク conin@0x10 は hoot stdin（ファイル名）であり曲ハンドルではない */
		if (!opensByName && nConin == 1 && stdinConin)
			opensByName = 1;
	}
	/* usd_98（ADVBIOS/ADVH）: INT7F AH=3F は BX=0 から曲バイトを読む（CX=4000/FFFF）。ADVH パックは曲を conin@title だけ列挙 — 上の famistava ヒューリスティックはファイル名テキスト（"DC_02P.USO" は len=10）をバインドし keyOn=0。常にバイナリハンドル。usmd_98 はここに居ない: 糊 AH=3F がタイトルハンドルを読み INT 7D AH=3D が DS:SI を ASCIIZ .USO 名で開く。そのハンドルにバイナリを置くと Open AX=0002。 */
	{
		static const char* kUsdSong[] = {
			"usd_98", "usd98",
			NULL
		};
		if (DosShellStarts(ge, kUsdSong)) {
			opensByName = 0;
			/* ADVH F1 EB 06 は DS:0 ASCIIZ を AH=3D。EB 0F（watagolf）はメモリロードで曲バイト必須。 */
			if (dos_.FindFile("ADVH.EXE")) {
				int memLoad = 0;
				uint8_t* mem = np2_mem();
				if (mem) {
					const unsigned oF1 = (unsigned)mem[0xF1 * 4]
						| ((unsigned)mem[0xF1 * 4 + 1] << 8);
					const unsigned sF1 = (unsigned)mem[0xF1 * 4 + 2]
						| ((unsigned)mem[0xF1 * 4 + 3] << 8);
					const unsigned p = (sF1 << 4) + oF1;
					if (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG
						&& p + 2u < 0x200000u
						&& mem[p] == 0xEB && mem[p + 1] == 0x0F)
						memLoad = 1;
				}
				if (!memLoad)
					opensByName = 1;
			}
		}
	}
	/* USDDRV98.COM cmd0 はハンドル 0 から 0x1F の ASCIIZ 名を AH=3F し INT F1 AX=0。曲バイトだと Open が失敗する。 */
	{
		static const char* kUsdDrvName[] = { "usddrv", "USDDRV", NULL };
		if (DosShellStarts(ge, kUsdDrvName))
			opensByName = 1;
	}
	/* magpa_98: kOpenName は "musdrv" を含む（mbmusp パックはハンドル 0 にファイル名が要る）が、magpa の INT7F cmd0 は曲バイトの AH=3F BX=0 のあと INT40 AX=2000。ハンドル 0 のファイル名テキストは MUSDRV が ASCII を読んだ。 */
	{
		static const char* kMagpaBin[] = { "magpa_98", "magpa", NULL };
		if (DosShellStarts(ge, kMagpaBin))
			opensByName = 0;
	}
	/* mercury_98 MRCRY_98: シェル MUSDRV.EXE が kOpenName "musdrv" に一致するが、INT7F cmd0 はハンドル 0 の曲バイトを AH=3F 読んだあと INT D2 AH=1 再生。ファイル名テキストだとシーケンサが黙る。 */
	{
		static const char* kMrcryBin[] = { "MRCRY", "mrcry", NULL };
		if (DosShellStarts(ge, kMrcryBin))
			opensByName = 0;
	}

	if (sf) {
		strncpy_s(dosSong_, sf, _TRUNCATE);
		if (opensByName) {
			/* ハンドル 0 のファイル名テキスト — ドライバは INT21 AH=3D で開く */
			dos_.SetHandleText(0, sf);
		} else {
			dos_.SetHandle(0, sf);
			/* VALKY_98 はインストール時にハンドル 5 のち 6 から SSCP/CSCP を読む。カタログはドライバを 6 に置く。5 に .DAT を置くと最初の AH=3F が成功し曲バイトへ CALL FAR（#UD）。 */
			static const char* kValkyH5[] = { "VALKY_98", "valky", NULL };
			if (!DosShellStarts(ge, kValkyH5)
				&& !DosHandleBoundToOtherFile(ge, 5, sf))
				dos_.SetHandle(5, sf);
			if (!DosHandleBoundToOtherFile(ge, 0x0B, sf))
				dos_.SetHandle(0x0B, sf);
			/* PMD_98 は曲ハンドル == タイトル下位バイト（インストール時に事前バインド） */
			const unsigned low = titleCode & 0xff;
			if (low < (unsigned)DOS98_HANDLE_MAX
				&& !DosHandleBoundToOtherFile(ge, (int)low, sf))
				dos_.SetHandle((uint16_t)low, sf);
		}
	}
	extCmd_ = 0;
	const unsigned byte2 = (titleCode >> 16) & 0xff;
	const unsigned hiByte = (titleCode >> 8) & 0xff;
	/* ARTDI パック NTL.PAC: タイトル 0xHHSS — 上位バイト = パックハンドル、下位 = 添字。stub は EXT_SONG にフル語が欲しい（hootrip）。 */
	int pacTitle = 0;
	int voiTitle = 0;
	if (ge) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "voice") != 0)
				continue;
			const char* base = r->name ? r->name : "";
			for (const char* p = base; *p; p++) {
				if (*p == '\\' || *p == '/' || *p == ':')
					base = p + 1;
			}
			const char* dot = strrchr(base, '.');
			if (!dot) continue;
			if (_stricmp(dot, ".PAC") == 0 && (unsigned)r->offset == hiByte && hiByte != 0)
				pacTitle = 1;
			if ((_stricmp(dot, ".VOI") == 0 || _stricmp(r->type, "voice") == 0)
				&& byte2 != 0 && (unsigned)r->offset == byte2)
				voiTitle = 1;
		}
	}
	static const char* kKoeiFmdrv[] = {
		"FMDRV", "fmdrv", "TENSH", "tensh", "FDRV", "fdrv",
		NULL
	};
	if (DosShellStarts(ge, kKoeiFmdrv)) {
		/* KOEI FMDRV_98 糊 INT40: cmd0=再生、cmd2=INT7F AX=0200 停止。
		   再生はハンドル 0 の AH=3F 読のあと INT7F AH=1 AL=(IN 7E4)+1（1-based 曲）、
		   INT7F AH=4 AL=IN 7E5（ループ。カタログ上位バイト 0x01）。下位語はファイルハンドル。 */
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);
	} else if (cplayFamily) {
		/* INT 7F AH=9: EXT param（0x7E4）上のバンク内添字 */
		extSong_ = 0;
		extParam_ = (uint16_t)byte2;
	} else if (DosShellStarts(ge, kLudyMagic)) {
		/* LUDY: IN 7E2 AX → xchg AH,BL が AH を .MCG ハンドル（6）、AL をパック内添字に使う。MAGIC_98: AH=音色ハンドル（SND）、AL=曲ハンドル。下位のみ EXT_SONG はバンクを飛ばし keyOn=0 / dumps=4。 */
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = 0;
	} else if (pacTitle) {
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = 0;
	} else if (voiTitle) {
		/* MDR 外部ボイス: EXT_PARAM = ボイスハンドル（byte2） */
		extSong_ = (uint16_t)(titleCode & 0xff);
		extParam_ = (uint16_t)byte2;
	} else if (DosShellStarts(ge, kElfMus)) {
		/* ELFMUS98 cmd0 `IN AX,7E2`: AH>=0x0A はパックバンク DOS ハンドル（aress BGM.MDT タイトル 0x10nn / SE.MDT 0x06nn）。下位 EXT_SONG は BX=0 のまま AH=3F 転送 0。8bit タイトル（birthd 0x23）は AH=0 のままハンドル 0 を読む。 */
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = 0;
	} else if (DosShellStarts(ge, kExtParamVoice)) {
		/* mmd2/iwaplay: IN 7E4 はボイス/TON ハンドル（カタログ byte2、またはタイトルが 0x10 風ならハンドル 5）。byte2!=0 は EXT_SONG=voice にしてバンクを飛ばしていた — dumps>0 / keyOn=0。 */
		extSong_ = (uint16_t)(titleCode & 0xff);
		extParam_ = byte2 ? (uint16_t)byte2 : 5;
		/* iwaplay.com 入口は BX=5 から TON を AH=3F し pos を EOF にする。cmd0 の再読が CF だと
		   曲 AH=3F BX=0 まで到達せず keyOn=1。カタログ offset=7E4 を結び直し pos=0。 */
		{
			const unsigned vh = (unsigned)extParam_;
			if (ge && vh < (unsigned)DOS98_HANDLE_MAX) {
				for (int i = 0; i < ge->romCount; i++) {
					const CEmuRomEntry* r = &ge->rom[i];
					if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "voice") != 0)
						continue;
					if ((unsigned)r->offset != vh)
						continue;
					const char* base = r->name ? r->name : "";
					for (const char* p = base; *p; p++) {
						if (*p == '\\' || *p == '/' || *p == ':')
							base = p + 1;
					}
					if (base[0])
						dos_.SetHandle((uint16_t)vh, base);
					break;
				}
			}
		}
	} else if (pc88VaIo_) {
		/* PC-88VA DOS オーバーレイ糊（tetrisva/rtypeva/shinrava/famista*）: IN 7E4 は EXT_PARAM 下位バイトだけを再生モードとして読む。タイトルは 0x0001xxxx（tetrisva）または 0xNN0000xx（rtype 0x01000010 / famista 0x04000010）— byte2、0 なら byte3。olteus.com INT7F cmd0 = far 00DF（init）のあと IN AX,7E2 + far 03F0（play）。cmd2 は init のみ — EXT_CMD=0 のまま play が走る。 */
		extSong_ = (uint16_t)(titleCode & 0xff);
		unsigned mode = (titleCode >> 16) & 0xff;
		if (mode == 0)
			mode = (titleCode >> 24) & 0xff;
		extParam_ = (uint16_t)mode;
	} else if (DosShellStarts(ge, kBgmlSong)) {
		/* Bio_100% BGML_98: INT 7F cmd0 は EXT_SONG を 16bit タイトルとして読む（07E2/07E3）。カタログ 0x01nn はワンショット。0x00nn はループ BGM。 */
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = 0;
	} else if (ge && DosShellStarts(ge, kN3gvSeek)) {
		/* n3gv2/n3gv11: INT7F cmd0 は IN 7E4/7E5 を CX スキップバイトにして
		   ハンドル 0 を AH=3F。タイトル 0xSSSS00FF の上位語がシーク。
		   既定 byte2→EXT_SONG は 2 回目 poke で CX=0 になり MUSIC.SDT 先頭（表）を再生する。 */
		extSong_ = (uint16_t)(titleCode & 0xff);
		extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);
	} else if (ge) {
		static const char* kYnsound[] = {
			"ynsound", "YNSOUND", "yns_98", "YNS_98", NULL
		};
		if (DosShellStarts(ge, kYnsound)) {
			/* YNS_98 cmd0: INT40 AH=0 / AX=0101 / AH=3 のあと IN 7E4。
			   0 は単体 .BGM。非0 は 1-based 添字（DEC して *8 LSEEK ハンドル0）。
			   既定 byte2→EXT_SONG は 7E2 を誰も読まず、パック BGM.DAT が全部曲0になる。 */
			extSong_ = (uint16_t)(titleCode & 0xff);
			extParam_ = (uint16_t)byte2;
		} else {
		static const char* kAvalonSong[] = { "avalon", NULL };
		if (DosShellStarts(ge, kAvalonSong)) {
			/* avalon.com IN 7E4 → INT F1 AH=0B AL=バンク内添字。カタログ 0xTT00HH: TT は DAT 内部タイトル、HH は conin。 */
			extSong_ = (uint16_t)(titleCode & 0xff);
			extParam_ = (uint16_t)((titleCode >> 16) & 0xff);
		} else if (byte2 != 0) {
			extSong_ = (uint16_t)byte2;
			extParam_ = (uint16_t)hiByte;
		} else {
			extSong_ = (uint16_t)(titleCode & 0xff);
			extParam_ = 0;
		}
		}
	} else if (byte2 != 0) {
		extSong_ = (uint16_t)byte2;
		extParam_ = (uint16_t)hiByte;
	} else {
		extSong_ = (uint16_t)(titleCode & 0xff);
		extParam_ = 0;
	}
}

/* MDR_98.COM: AH=3F が空でも SEQ を [01F2] へ載せ INT40 BX=3 を打ち直す。
   空バッファだと全 zip が同じ 19 キー / keyBuckets=17 で FAIL_SHORT になる。 */
static int MdrGlueWant(const CEmuGameEntry* ge)
{
	static const char* kMdr[] = {
		"MDR_98", "mdr_98", "wlfpk_98", "wlfpk",
		"ZETA_98", "zeta_98", "ZETA", NULL
	};
	return DosShellStarts(ge, kMdr);
}

static void MdrHostBindSong(uint8_t* mem, CEmuDos98* dos, const CEmuGameEntry* ge,
	const char* song)
{
	if (!mem || !dos || !MdrGlueWant(ge))
		return;
	const unsigned cs = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	if (!cs || cs == (unsigned)DOS98_TRAMP_SEG || cs >= 0xA000u)
		return;
	const unsigned base = cs << 4;
	if (base + 0x1F8u >= 0x200000u || mem[base + 0x14D] != 0x60)
		return;
	unsigned songSeg = (unsigned)mem[base + 0x1F2]
		| ((unsigned)mem[base + 0x1F3] << 8);
	unsigned voiSeg = (unsigned)mem[base + 0x1F6]
		| ((unsigned)mem[base + 0x1F7] << 8);
	auto plant = [&](unsigned* seg, unsigned off) {
		if (*seg >= 0x100u && *seg < 0xA000u)
			return;
		uint16_t got = 0;
		if (!dos->AllocBlock(mem, 0x2FFF, &got) || !got)
			return;
		*seg = got;
		mem[base + off] = (uint8_t)(got & 0xff);
		mem[base + off + 1] = (uint8_t)(got >> 8);
	};
	plant(&songSeg, 0x1F2);
	plant(&voiSeg, 0x1F6);
	auto copyTo = [&](unsigned seg, const CEmuDos98File* f) {
		if (!f || !f->data || !f->size || seg < 0x100u || seg >= 0xA000u)
			return;
		unsigned n = f->size;
		if (n > 0x7FF0u)
			n = 0x7FF0u;
		const unsigned dst = seg << 4;
		if (dst + n < 0x200000u)
			memcpy(mem + dst, f->data, n);
	};
	const CEmuDos98File* sf = (song && song[0]) ? dos->FindFile(song) : NULL;
	copyTo(songSeg, sf);
	const CEmuDos98File* vf = dos->FindFile("VOICE.VOI");
	if (!vf && song && song[0]) {
		char voi[80];
		size_t n = 0;
		while (song[n] && n + 5 < sizeof(voi)) {
			voi[n] = song[n];
			++n;
		}
		voi[n] = 0;
		char* dot = strrchr(voi, '.');
		if (dot)
			memcpy(dot, ".VOI", 5);
		else
			memcpy(voi + n, ".VOI", 5);
		vf = dos->FindFile(voi);
	}
	copyTo(voiSeg, vf);
	/* 糊は IN 7E4 / handle0 で AH=3F する。ハンドルが STDIN や未開だと
	   interrupt() が読みで予算を食い、BX=1/2/3 がドレインまで遅れる。
	   SEQ/VOI は上で載済なので両方の CALL を NOP。 */
	if (base + 0x198u < 0x200000u && mem[base + 0x195] == 0xE8) {
		mem[base + 0x195] = 0x90;
		mem[base + 0x196] = 0x90;
		mem[base + 0x197] = 0x90;
		if (mem[base + 0x198] == 0x72)
			mem[base + 0x198] = 0x90;
		if (mem[base + 0x199] == 0xC8)
			mem[base + 0x199] = 0x90;
	}
	if (base + 0x1C0u < 0x200000u && mem[base + 0x1BE] == 0xE8) {
		mem[base + 0x1BE] = 0x90;
		mem[base + 0x1BF] = 0x90;
		mem[base + 0x1C0] = 0x90;
	}
}

/* 0FA0 は ES:007E を 0 と見て ch[] を埋めない。INT14 0DE6 は [1D3C+i*4E]==0 ならトラックを飛ばす。
   SEQ のトラック表から far ポインタを植える。 */
static void MdrPlantChannels(uint8_t* mem)
{
	if (!mem)
		return;
	const unsigned glue = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	const unsigned dcs = (unsigned)mem[0x14 * 4 + 2]
		| ((unsigned)mem[0x14 * 4 + 3] << 8);
	if (!glue || glue == (unsigned)DOS98_TRAMP_SEG || glue >= 0xA000u)
		return;
	if (!dcs || dcs == (unsigned)DOS98_TRAMP_SEG || dcs >= 0xA000u)
		return;
	const unsigned gb = glue << 4;
	if (gb + 0x1F8u >= 0x200000u || mem[gb + 0x14D] != 0x60)
		return;
	const unsigned songSeg = (unsigned)mem[gb + 0x1F2]
		| ((unsigned)mem[gb + 0x1F3] << 8);
	if (songSeg < 0x100u || songSeg >= 0xA000u)
		return;
	const unsigned slin = songSeg << 4;
	if (slin + 0x90u >= 0x200000u)
		return;
	const unsigned db = dcs << 4;
	unsigned n = (unsigned)mem[slin + 0x7E] | ((unsigned)mem[slin + 0x7F] << 8);
	if (n == 0 || n > 6u)
		n = 6u;
	/* 0FA0 は table[i] を長さとして 0x8C から累積する。未充填なら同じ式で埋める。 */
	const unsigned filled = (unsigned)mem[db + 0x1D3C]
		| ((unsigned)mem[db + 0x1D3D] << 8);
	if (!filled) {
		unsigned acc = 0x8Cu;
		for (unsigned i = 0; i < 6u; i++) {
			const unsigned bx = db + 0x1D40 + i * 0x4Eu;
			if (bx < 4u || bx + 0x50u >= 0x200000u)
				break;
			unsigned t = 0;
			if (i < n) {
				const unsigned to = slin + 0x80u + i * 2u;
				t = (unsigned)mem[to] | ((unsigned)mem[to + 1] << 8);
			}
			const unsigned off = t ? acc : 0;
			if (t)
				acc += t;
			mem[bx - 4] = off ? 1 : 0;
			mem[bx - 3] = 0;
			mem[bx - 2] = 0;
			mem[bx - 1] = 0;
			mem[bx + 0] = (uint8_t)(off & 0xff);
			mem[bx + 1] = (uint8_t)(off >> 8);
			mem[bx + 2] = (uint8_t)(songSeg & 0xff);
			mem[bx + 3] = (uint8_t)(songSeg >> 8);
			mem[bx + 4] = mem[bx + 0];
			mem[bx + 5] = mem[bx + 1];
			mem[bx + 6] = mem[bx + 2];
			mem[bx + 7] = mem[bx + 3];
			mem[bx + 0x44] = 0;
			mem[bx + 0x45] = 0;
			mem[bx + 0x46] = 0;
			mem[bx + 0x47] = 0;
		}
	}
	/* 先頭の 8x コマンド（ゲート 8C が 0x30 tick）を飛ばし、最初の <80 ノートへ。
	   0DE6 はゲート中 [SI+4A] が残ると読まない。 */
	for (unsigned i = 0; i < 6u; i++) {
		const unsigned bx = db + 0x1D40 + i * 0x4Eu;
		if (bx + 8u >= 0x200000u)
			break;
		if (!mem[bx - 4] && !mem[bx - 3])
			continue;
		unsigned off = (unsigned)mem[bx] | ((unsigned)mem[bx + 1] << 8);
		unsigned steps = 0;
		while (off + 3u < 0x8000u && slin + off < 0x200000u && steps < 40u) {
			const unsigned al = mem[slin + off];
			if (al < 0x80u)
				break;
			off += 3u;
			steps++;
		}
		mem[bx + 0] = (uint8_t)(off & 0xff);
		mem[bx + 1] = (uint8_t)(off >> 8);
		mem[bx + 4] = mem[bx + 0];
		mem[bx + 5] = mem[bx + 1];
		mem[bx + 0x44] = 0;
		mem[bx + 0x45] = 0;
	}
	/* INT40 BX=7 が [1A18] を立てる。糊は BX=5/1/2/3 だけで 0 のまま。
	   0xD2E（8C ゲート）と 0DE6 の <80 ノートが JZ で YM を呼ばない。 */
	if (db + 0x1A19u < 0x200000u)
		mem[db + 0x1A18] = 1;
	/* INT14 は [1AC0]!=0 だと 0DE6 を飛ばす。ネスト tick が COM SS を戻すと 1 のまま。 */
	if (db + 0x1AC1u < 0x200000u) {
		mem[db + 0x1AC0] = 0;
		mem[db + 0x1AC1] = 0;
	}
	/* [1A1E]/[1A22] 初期値は A000:0 / A200:0（テキストVRAM）。エミュはそこを
	   作業RAMにしないので 0x12A が 1A4C を読めずノートが死ぬ。DGROUP へ移す。 */
	if (db + 0x9000u < 0x200000u) {
		const unsigned vram0 = (unsigned)mem[db + 0x1A20] | ((unsigned)mem[db + 0x1A21] << 8);
		const unsigned vram1 = (unsigned)mem[db + 0x1A24] | ((unsigned)mem[db + 0x1A25] << 8);
		if (vram0 >= 0xA000u) {
			memset(mem + db + 0x8000u, 0, 0x800u);
			mem[db + 0x1A1E] = 0x00;
			mem[db + 0x1A1F] = 0x80;
			mem[db + 0x1A20] = (uint8_t)(dcs & 0xff);
			mem[db + 0x1A21] = (uint8_t)(dcs >> 8);
		}
		if (vram1 >= 0xA000u) {
			memset(mem + db + 0x8800u, 0, 0x800u);
			mem[db + 0x1A22] = 0x00;
			mem[db + 0x1A23] = 0x88;
			mem[db + 0x1A24] = (uint8_t)(dcs & 0xff);
			mem[db + 0x1A25] = (uint8_t)(dcs >> 8);
		}
	}
}

/* SDD_2.DRV ISR 09BF: ファイルイメージ [152C]=0x0F。CMP [152C],0Fh / JNC は
   CALL 1477（27h=30 でタイマ停止＋チャネルを 16DA テンプレへ戻す）へ入る。
   再生フラグ [152A] bit0 が無いと毎 tick その経路。muse_98 が立てる前の
   最初の INT14 で曲が死ぬ（ishido keyOn=1 irq=1）。 */
static void SddKeepPlay(uint8_t* mem, int start)
{
	if (!mem || !g_sddLoadSeg)
		return;
	const unsigned lin = (unsigned)g_sddLoadSeg << 4;
	if (lin + 0x152Du >= 0x200000u || mem[lin + 0x9BF] != 0xFA)
		return;
	if (mem[lin + 0xB92] == 0xEB && mem[lin + 0xB93] == 0x99) {
		mem[lin + 0xB92] = 0xC3;
		mem[lin + 0xB93] = 0x90;
	}
	/* 11D5 JNZ は [SI]=07 テンプレで duration を生 tick に戻す。15Hz IRQ だと
	   0x18 が 1.6s/音。NOP して常に /8（0xF0 と同じ単純経路）。 */
	if (mem[lin + 0x11D5] == 0x75 && mem[lin + 0x11D6] == 0x08) {
		mem[lin + 0x11D5] = 0x90;
		mem[lin + 0x11D6] = 0x90;
	}
	mem[lin + 0x152A] = (uint8_t)(mem[lin + 0x152A] | 1u);
	if (start)
		mem[lin + 0x152B] = 0;
	if (mem[lin + 0x152C] >= 0x0Fu)
		mem[lin + 0x152C] = 0;
	const unsigned dest = (unsigned)mem[lin + 0x427]
		| ((unsigned)mem[lin + 0x428] << 8);
	if (dest < 0x1A00u || dest >= 0xA000u)
		return;
	const unsigned song = lin + dest;
	if (song + 16u >= 0x200000u)
		return;
	if (g_sddSongData && g_sddSongSize >= 4u
		&& song + g_sddSongSize < 0x200000u) {
		unsigned n = g_sddSongSize;
		if (n > 0x2000u)
			n = 0x2000u;
		if (mem[song] != g_sddSongData[0]
			|| mem[song + 1] != g_sddSongData[1])
			memcpy(mem + song, g_sddSongData, n);
	}
	/* 0x0C: +0 type、+2 は 00BC 系の別塊、+4 から最大 4 トラック（3FM+SSG）。
	   0B22 は曲ポインタ [SI+9]。[SI+7]==0 で 0B00 フェッチ。+2 を植えると
	   ノート列ではなく 00BC 塊を食って keyOn=0。 */
	{
		const unsigned typ = (unsigned)mem[song]
			| ((unsigned)mem[song + 1] << 8);
		const unsigned slim = g_sddSongSize ? g_sddSongSize : 0x800u;
		if (typ == 0x000Cu || typ == 0x0004u) {
			static const unsigned kCh[4] = {
				0x1532u, 0x155Eu, 0x158Au, 0x15B6u
			};
			const unsigned p9 = (unsigned)mem[lin + 0x1532u + 9u]
				| ((unsigned)mem[lin + 0x1532u + 10u] << 8);
			if (!(p9 >= dest + 4u && p9 < dest + slim)) {
				for (unsigned i = 0; i < 4u; i++) {
					const unsigned toff = (unsigned)mem[song + 4u + i * 2u]
						| ((unsigned)mem[song + 5u + i * 2u] << 8);
					if (toff < 8u || toff >= slim)
						continue;
					unsigned ptr = dest + toff;
					const unsigned start = ptr;
					const unsigned endp = dest + slim;
					unsigned firstNote = 0;
					unsigned guard = 0;
					int afterLoop = 0;
					while (guard++ < 160u && ptr + 2u < endp
						&& ptr - start < 128u) {
						const unsigned op = mem[lin + ptr];
						if (op < 0x80u) {
							if (!firstNote)
								firstNote = ptr;
							if (afterLoop) {
								firstNote = ptr;
								break;
							}
							ptr += 2u;
							continue;
						}
						if (op == 0xF1u) {
							unsigned p2 = ptr + 1u;
							int foundFa = 0;
							unsigned w = 0;
							while (w++ < 48u && p2 + 2u < endp) {
								const unsigned q = mem[lin + p2];
								if (q == 0xFAu) {
									foundFa = 1;
									break;
								}
								if (q < 0x80u)
									p2 += 2u;
								else if (q == 0x81u)
									p2 += 3u;
								else if (q == 0x80u)
									p2 += 2u;
								else
									p2++;
							}
							if (foundFa) {
								ptr = p2 + 2u;
								afterLoop = 1;
								continue;
							}
							ptr++;
							continue;
						}
						if (op == 0x80u) {
							ptr += 2u;
							continue;
						}
						if (op == 0x81u) {
							ptr += 3u;
							continue;
						}
						if (op == 0x82u || op == 0x84u || op == 0xFAu) {
							ptr += 2u;
							if (op == 0xFAu)
								afterLoop = 1;
							continue;
						}
						if (op == 0xFFu)
							break;
						ptr++;
					}
					if (firstNote)
						ptr = firstNote;
					const unsigned ch = lin + kCh[i];
					if (ch + 12u >= 0x200000u)
						break;
					mem[ch + 9] = (uint8_t)(ptr & 0xff);
					mem[ch + 10] = (uint8_t)(ptr >> 8);
					mem[ch + 7] = 0;
					mem[ch + 8] = 0;
					mem[ch + 3] = (uint8_t)(1u << (i < 3u ? i : 0));
					/* 1416 TEST [SI+5],80 / JNZ RET — bit7 は YM 書込ミュート。
					   植で立てると 11BD の 28h がチップに届かない。 */
					mem[ch + 5] = (uint8_t)(mem[ch + 5] & 0x7Fu);
				}
			}
			/* 0B92 未登録 86-9F は JMP 0B2D で同一バイトを再フェッチして ISR が
			   回る。90 は BERRY メロディの 43 90 で必ず当たる。ポインタだけ進める。 */
			for (unsigned i = 0; i < 4u; i++) {
				const unsigned ch = lin + kCh[i];
				if (ch + 12u >= 0x200000u)
					break;
				unsigned ptr = (unsigned)mem[ch + 9]
					| ((unsigned)mem[ch + 10] << 8);
				unsigned skips = 0;
				while (skips < 8u && ptr >= dest && ptr < dest + slim) {
					const unsigned op = mem[lin + ptr];
					int known = (op < 0x80u)
						|| op == 0x80u || op == 0x81u || op == 0x82u
						|| op == 0x83u || op == 0x84u || op == 0x85u
						|| op == 0xC0u || op == 0xF0u || op == 0xF1u
						|| op == 0xFAu || op == 0xFBu || op == 0xFEu
						|| op == 0xFFu;
					if (known)
						break;
					ptr++;
					skips++;
				}
				if (skips) {
					mem[ch + 9] = (uint8_t)(ptr & 0xff);
					mem[ch + 10] = (uint8_t)(ptr >> 8);
					mem[ch + 7] = 0;
					mem[ch + 8] = 0;
				}
			}
		}
	}
}

/* wiz6 $MUSE2$ ISR 0535: [113B] bit0 だと 05F5 シーケンサを飛ばす。0692 は
   LES DI,[BX+7] のあと TEST [BX+5],4 / JZ でチャネルを捨てる。+5 bit2 と
   0x0C トラック far ptr を植える。 */
static void Muse2KeepPlay(uint8_t* mem)
{
	if (!mem || !g_muse2Seg)
		return;
	const unsigned lin = (unsigned)g_muse2Seg << 4;
	if (lin + 0x11B6u + 12u >= 0x200000u || mem[lin + 0x535] != 0x9C
		|| mem[lin + 0x0A] != '$' || mem[lin + 0x0B] != 'M'
		|| mem[lin + 0x0C] != 'U' || mem[lin + 0x0D] != 'S')
		return;
	unsigned songSeg = (unsigned)mem[lin + 0x1151]
		| ((unsigned)mem[lin + 0x1152] << 8);
	if (songSeg < 0x0100u || songSeg >= 0xA000u) {
		const unsigned glue = (unsigned)mem[0x7F * 4 + 2]
			| ((unsigned)mem[0x7F * 4 + 3] << 8);
		if (glue && glue != (unsigned)DOS98_TRAMP_SEG && glue < 0xA000u) {
			const unsigned gb = glue << 4;
			if (gb + 0x254u < 0x200000u)
				songSeg = (unsigned)mem[gb + 0x253]
					| ((unsigned)mem[gb + 0x254] << 8);
		}
	}
	if (songSeg < 0x0100u || songSeg >= 0xA000u)
		return;
	const unsigned song = songSeg << 4;
	if (song + 16u >= 0x200000u)
		return;
	if (g_muse2SongData && g_muse2SongSize >= 4u
		&& song + g_muse2SongSize < 0x200000u) {
		unsigned n = g_muse2SongSize;
		if (n > 0x2000u)
			n = 0x2000u;
		if (mem[song] != g_muse2SongData[0]
			|| mem[song + 1] != g_muse2SongData[1])
			memcpy(mem + song, g_muse2SongData, n);
	}
	const unsigned typ = (unsigned)mem[song]
		| ((unsigned)mem[song + 1] << 8);
	const unsigned slim = g_muse2SongSize ? g_muse2SongSize : 0x800u;
	if (typ != 0x000Cu && typ != 0x0004u)
		return;
	static const unsigned kCh[3] = { 0x1148u, 0x117Fu, 0x11B6u };
	unsigned tmin = slim;
	for (unsigned i = 0; i < 4u; i++) {
		const unsigned t = (unsigned)mem[song + 4u + i * 2u]
			| ((unsigned)mem[song + 5u + i * 2u] << 8);
		if (t >= 8u && t < tmin)
			tmin = t;
	}
	const unsigned p0 = (unsigned)mem[lin + 0x114Fu]
		| ((unsigned)mem[lin + 0x1150] << 8);
	if (p0 >= tmin && p0 < slim
		&& (mem[lin + 0x114Du] & 4u))
		return;
	for (unsigned i = 0; i < 3u; i++) {
		const unsigned toff = (unsigned)mem[song + 4u + i * 2u]
			| ((unsigned)mem[song + 5u + i * 2u] << 8);
		if (toff < 8u || toff >= slim)
			continue;
		const unsigned ch = lin + kCh[i];
		mem[ch + 7] = (uint8_t)(toff & 0xff);
		mem[ch + 8] = (uint8_t)(toff >> 8);
		mem[ch + 9] = (uint8_t)(songSeg & 0xff);
		mem[ch + 10] = (uint8_t)(songSeg >> 8);
		mem[ch + 5] = (uint8_t)(mem[ch + 5] | 4u);
		mem[ch + 0x19] = 0;
	}
}

/* rakuichi $NMUSE$ ISR 06D6: TEST [1399],1 でシーケンサを飛ばす。チャネル
   13AE/13EB/1428 stride 0x3D、+5 bit2 と 0x0C トラック far ptr。INT14 は
   既 stub があっても強制植（SDD と同じ）。 */
static void NmuseKeepPlay(uint8_t* mem)
{
	if (!mem || !g_nmuseSeg)
		return;
	const unsigned lin = (unsigned)g_nmuseSeg << 4;
	if (lin + 0x1428u + 12u >= 0x200000u || mem[lin + 0x6D6] != 0x9C
		|| mem[lin + 0x0A] != '$' || mem[lin + 0x0B] != 'N')
		return;
	unsigned songSeg = (unsigned)mem[lin + 0x13AEu + 9u]
		| ((unsigned)mem[lin + 0x13AEu + 10u] << 8);
	if (songSeg < 0x0100u || songSeg >= 0xA000u) {
		const unsigned glue = (unsigned)mem[0x7F * 4 + 2]
			| ((unsigned)mem[0x7F * 4 + 3] << 8);
		if (glue && glue != (unsigned)DOS98_TRAMP_SEG && glue < 0xA000u) {
			const unsigned gb = glue << 4;
			if (gb + 0x254u < 0x200000u)
				songSeg = (unsigned)mem[gb + 0x253]
					| ((unsigned)mem[gb + 0x254] << 8);
		}
	}
	if (songSeg < 0x0100u || songSeg >= 0xA000u)
		songSeg = 0x9000u;
	const unsigned song = songSeg << 4;
	if (song + 16u >= 0x200000u)
		return;
	if (g_muse2SongData && g_muse2SongSize >= 4u
		&& song + g_muse2SongSize < 0x200000u) {
		unsigned n = g_muse2SongSize;
		if (n > 0x2000u)
			n = 0x2000u;
		if (mem[song] != g_muse2SongData[0]
			|| mem[song + 1] != g_muse2SongData[1])
			memcpy(mem + song, g_muse2SongData, n);
	}
	const unsigned typ = (unsigned)mem[song]
		| ((unsigned)mem[song + 1] << 8);
	const unsigned slim = g_muse2SongSize ? g_muse2SongSize : 0x800u;
	if (typ != 0x000Cu && typ != 0x0004u)
		return;
	static const unsigned kCh[3] = { 0x13AEu, 0x13EBu, 0x1428u };
	unsigned tmin = slim;
	for (unsigned i = 0; i < 4u; i++) {
		const unsigned t = (unsigned)mem[song + 4u + i * 2u]
			| ((unsigned)mem[song + 5u + i * 2u] << 8);
		if (t >= 8u && t < tmin)
			tmin = t;
	}
	const unsigned p0 = (unsigned)mem[lin + 0x13AEu + 7u]
		| ((unsigned)mem[lin + 0x13AEu + 8u] << 8);
	mem[lin + 0x1399u] = 0;
	if (p0 >= tmin && p0 < slim
		&& (mem[lin + 0x13AEu + 5u] & 4u))
		return;
	for (unsigned i = 0; i < 3u; i++) {
		const unsigned toff = (unsigned)mem[song + 4u + i * 2u]
			| ((unsigned)mem[song + 5u + i * 2u] << 8);
		if (toff < 8u || toff >= slim)
			continue;
		const unsigned ch = lin + kCh[i];
		mem[ch + 7] = (uint8_t)(toff & 0xff);
		mem[ch + 8] = (uint8_t)(toff >> 8);
		mem[ch + 9] = (uint8_t)(songSeg & 0xff);
		mem[ch + 10] = (uint8_t)(songSeg >> 8);
		mem[ch + 5] = (uint8_t)(mem[ch + 5] | 4u);
		mem[ch + 0x19] = 0;
	}
}

/* Pc98DosLin の実装 */
static unsigned Pc98DosLin(uint16_t seg, uint16_t off)
{
	return ((unsigned)seg << 4) + (unsigned)off;
}

/* Pc98Wr16 の実装 */
static void Pc98Wr16(uint8_t* mem, unsigned addr, uint16_t v)
{
	mem[addr] = (uint8_t)(v & 0xff);
	mem[addr + 1] = (uint8_t)(v >> 8);
}

/* SYNTH_98.COM（365 バイト ylz 糊）: INT60 AH=0x0F がオーバーレイ内 DX=destOff を返す（S20:3088 / S20S_4:1B86）。COM は `mov ax,ds; mov es,ax; mov di,dx` なので PAI が AH=4A 縮小 COM の先に着き、AH=0 がオーバーレイの残り init を解析した。INIT がオーバーレイを CS:0260 に置いたあと、CS:0260 から ES を載せる洞穴へ飛ぶ（DS はまだオーバーレイかも）。0260 が 0 なら飛ばす（常駐 SYNTHIA — crim）。 */
static int PatchSynth98PaiDest(uint8_t* mem, uint16_t psp)
{
	if (!mem || !psp)
		return 0;
	const unsigned ovl = Pc98DosLin(psp, 0x260);
	const unsigned at = Pc98DosLin(psp, 0x1A5);
	const unsigned cave = Pc98DosLin(psp, 0x270);
	if (ovl + 2u >= 0x200000u || at + 11u >= 0x200000u || cave + 16u >= 0x200000u)
		return 0;
	if (!(mem[ovl] | mem[ovl + 1]))
		return 0;
	static const uint8_t kOld[] = { 0x8C, 0xD8, 0x8E, 0xC0, 0x8B, 0xFA };
	if (mem[at] == 0xE9 && mem[at + 1] == 0xC8 && mem[at + 2] == 0x00)
		return 1;
	if (memcmp(mem + at, kOld, 6) != 0)
		return 0;
	/* E9 disp16 → 0270。旧 mov ds,cs:[025C] まで埋める */
	mem[at + 0] = 0xE9;
	mem[at + 1] = 0xC8;
	mem[at + 2] = 0x00;
	memset(mem + at + 3, 0x90, 8);
	/* 洞穴@0270: mov es,[cs:0260]; mov di,dx; mov ds,[cs:025C]; jmp 01B0 */
	static const uint8_t kCave[] = {
		0x2E, 0x8E, 0x06, 0x60, 0x02,
		0x8B, 0xFA,
		0x2E, 0x8E, 0x1E, 0x5C, 0x02,
		0xE9, 0x31, 0xFF
	};
	memcpy(mem + cave, kCave, sizeof(kCave));
	return 1;
}

/* SS_98.COM cmd0 は CS:0196 の ASCIIZ 名を AH=3F 読して INT 41 AH=1。糊は `mov si,ds / xor di,dx`（DX=0196）なので SI:DI はその名への far ポインタ — xor は DI=0 を仮定。BootDos は DI を汚したまま。ドライバ DS:260A の FindFirst が TITLE.DAT を外す（AX=0012）。`mov di,dx` がポインタを名に保つ。
   SSD_98.COM は同型だが再生が INT 42（diadrum / tchaser）。CD 41 だけ見ると xor が残り、AH=3F 名が SI:0 になる。 */
static void PatchSs98SongPtr(uint8_t* mem)
{
	if (!mem) return;
	const unsigned seg = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	if (!seg || seg == (unsigned)DOS98_TRAMP_SEG)
		return;
	static const uint8_t kOld41[] = { 0x8C, 0xDE, 0x31, 0xD7, 0xB4, 0x01, 0xCD, 0x41 };
	static const uint8_t kOld42[] = { 0x8C, 0xDE, 0x31, 0xD7, 0xB4, 0x01, 0xCD, 0x42 };
	const unsigned cs0 = Pc98DosLin((uint16_t)seg, 0x100);
	for (unsigned d = 0; d + 8u < 0xA0u; d++) {
		const unsigned at = cs0 + d;
		if (at + 8u >= 0x200000u)
			break;
		if (memcmp(mem + at, kOld41, 8) != 0 && memcmp(mem + at, kOld42, 8) != 0)
			continue;
		mem[at + 2] = 0x8B;
		mem[at + 3] = 0xFA;
		return;
	}
}

/* Glodia pack: SSG 0x0B AA/55 カナリア。失敗 JNZ（emdr/zavas）と `JZ +1; RET`（vd）を NOP。YM ありフラグを立てる（フェイクループではない）。 */
static void PatchGlodiaYmDetect(uint8_t* mem, int dataAddr, int bootCs)
{
	if (!mem)
		return;
	static const uint8_t kCanaryA[] = { 0xB0, 0xAA, 0xB4, 0x0B, 0xBA, 0x88, 0x01 };
	static const uint8_t kCanaryB[] = { 0xB0, 0x0B, 0xB4, 0xAA };
	for (unsigned p = 0x8000u; p + 0x90u < 0x30000u; p++) {
		int hit = 0;
		if (memcmp(mem + p, kCanaryA, sizeof(kCanaryA)) == 0)
			hit = 1;
		else if (memcmp(mem + p, kCanaryB, sizeof(kCanaryB)) == 0) {
			int vdRet = 0;
			for (unsigned i = 0; i + 5u < 0x50u; i++) {
				if (mem[p + i] == 0x3C && mem[p + i + 1] == 0xAA
					&& mem[p + i + 2] == 0x74 && mem[p + i + 3] == 0x01
					&& mem[p + i + 4] == 0xC3) {
					vdRet = 1;
					break;
				}
			}
			if (vdRet)
				hit = 2;
		}
		if (!hit)
			continue;
		for (unsigned i = 0; i + 5u < 0xA0u; i++) {
			if (mem[p + i] != 0x3C)
				continue;
			if (mem[p + i + 1] != 0xAA && mem[p + i + 1] != 0x55)
				continue;
			if (mem[p + i + 2] == 0x75) {
				mem[p + i + 2] = 0x90;
				mem[p + i + 3] = 0x90;
			} else if (mem[p + i + 2] == 0x74 && mem[p + i + 3] == 0x01
				&& mem[p + i + 4] == 0xC3) {
				mem[p + i + 4] = 0x90; /* vd: CMP; JZ +1; RET → RET を消す */
			}
		}
		if (hit == 1)
			break; /* emdr/zavas カナリアは1か所 */
	}
	const unsigned flagBase = (dataAddr > 0) ? (unsigned)dataAddr : 0x10000u;
	unsigned flagEnd = flagBase + 0x8000u;
	if (flagEnd > 0x200000u)
		flagEnd = 0x200000u;
	for (unsigned p = flagBase; p + 8u < flagEnd; p++) {
		if (mem[p] == 0x80 && mem[p + 1] == 0x3E
			&& mem[p + 2] == 0x42 && mem[p + 3] == 0x2E
			&& mem[p + 4] == 0x00 && mem[p + 5] == 0x75) {
			mem[p + 5] = 0x90;
			mem[p + 6] = 0x90;
		}
	}
	if (dataAddr > 0 && (unsigned)dataAddr + 0x2E42u < 0x200000u)
		mem[(unsigned)dataAddr + 0x2E42u] = 0;
	/* zavas 2D75 は CMP [2401],FF。シグネチャがあるときだけ（emdr の 2401 はコード）。 */
	if (dataAddr > 0 && (unsigned)dataAddr + 0x2408u < 0x200000u) {
		static const uint8_t kZavasHead[] = { 0x9C, 0xFA, 0x50, 0x52, 0x1E, 0xFC };
		int zavas = 0;
		const unsigned zb = (unsigned)dataAddr;
		unsigned ze = zb + 0x8000u;
		if (ze > 0x200000u)
			ze = 0x200000u;
		for (unsigned p = zb; p + 6u < ze; p++) {
			if (memcmp(mem + p, kZavasHead, sizeof(kZavasHead)) == 0) {
				zavas = 1;
				break;
			}
		}
		if (zavas)
			mem[(unsigned)dataAddr + 0x2401u] = 0xFF;
	}
	if (bootCs > 0) {
		const unsigned b = (unsigned)bootCs << 4;
		/* vd: MOV BYTE [3851],1  / vd2: MOV BYTE [80FC],1 — 命令があるときだけフラグを立てる */
		if (b + 0x3860u < 0x200000u) {
			static const uint8_t kVd[] = { 0xC6, 0x06, 0x51, 0x38, 0x01 };
			for (unsigned p = b; p + 5u < b + 0x9000u && p + 5u < 0x200000u; p++) {
				if (memcmp(mem + p, kVd, sizeof(kVd)) == 0) {
					mem[b + 0x3851u] = 1;
					break;
				}
			}
		}
		if (b + 0x8100u < 0x200000u) {
			static const uint8_t kVd2[] = { 0xC6, 0x06, 0xFC, 0x80, 0x01 };
			for (unsigned p = b; p + 5u < b + 0xA000u && p + 5u < 0x200000u; p++) {
				if (memcmp(mem + p, kVd2, sizeof(kVd2)) == 0) {
					mem[b + 0x80FCu] = 1;
					break;
				}
			}
		}
	}
}

static void GlodiaPlantVec(uint8_t* mem, unsigned isrLin, unsigned csBase, uint8_t* picMask)
{
	if (!mem || !picMask || isrLin < csBase || isrLin - csBase > 0xFFFFu)
		return;
	const unsigned off = isrLin - csBase;
	const unsigned seg = csBase >> 4;
	mem[0x14 * 4 + 0] = (uint8_t)(off & 0xff);
	mem[0x14 * 4 + 1] = (uint8_t)((off >> 8) & 0xff);
	mem[0x14 * 4 + 2] = (uint8_t)(seg & 0xff);
	mem[0x14 * 4 + 3] = (uint8_t)((seg >> 8) & 0xff);
	mem[PC98_OPN_IRQ_VEC * 4 + 0] = mem[0x14 * 4 + 0];
	mem[PC98_OPN_IRQ_VEC * 4 + 1] = mem[0x14 * 4 + 1];
	mem[PC98_OPN_IRQ_VEC * 4 + 2] = mem[0x14 * 4 + 2];
	mem[PC98_OPN_IRQ_VEC * 4 + 3] = mem[0x14 * 4 + 3];
	*picMask = (uint8_t)(*picMask & ~(1u << 3));
}

/* emdr ISR は PUSHA 枠。zavas は PUSHF/CLI/PUSH AX,DX,DS で INT14 に CS:3649 を植える。20A0/2C2D は status&0xC0 が 0 だと植栽を飛ばす。 */
static void PatchGlodiaOpnIsr(uint8_t* mem, int dataAddr, int bootCs, uint8_t* picMask)
{
	if (!mem || !picMask)
		return;
	unsigned bases[3];
	int nBase = 0;
	if (dataAddr > 0)
		bases[nBase++] = (unsigned)dataAddr;
	if (bootCs > 0) {
		const unsigned b = (unsigned)bootCs << 4;
		if (nBase == 0 || bases[0] != b)
			bases[nBase++] = b;
	}
	static const uint8_t kEmdr[] = { 0x60, 0x1E, 0x06, 0xFA, 0xFC };
	static const uint8_t kZavas[] = { 0x9C, 0xFA, 0x50, 0x52, 0x1E, 0xFC };
	for (int bi = 0; bi < nBase; bi++) {
		const unsigned base = bases[bi];
		if (base < 0x10u || base >= 0x200000u)
			continue;
		unsigned end = base + 0x8000u;
		if (end > 0x200000u)
			end = 0x200000u;
		for (unsigned p = base; p + 16u < end; p++) {
			if (memcmp(mem + p, kEmdr, sizeof(kEmdr)) == 0) {
				int hit188 = 0;
				for (unsigned q = p; q + 3u < p + 24u && q + 3u < end; q++) {
					if (mem[q] == 0xBA && mem[q + 1] == 0x88 && mem[q + 2] == 0x01) {
						hit188 = 1;
						break;
					}
				}
				if (hit188) {
					GlodiaPlantVec(mem, p, base, picMask);
					return;
				}
			}
			if (memcmp(mem + p, kZavas, sizeof(kZavas)) == 0) {
				GlodiaPlantVec(mem, p, base, picMask);
				return;
			}
		}
	}
	/* vd/vd2 FM はスレーブ PIC EOI（OUT 08）の ISR を INT14 へ。PIT 先頭（OUT 00 EOI）は SSG フレーズで BGM ではない。 */
	static const uint8_t kVdFm[] = { 0xFA, 0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x55, 0x1E, 0x06 };
	static const uint8_t kVd2Fm[] = { 0x60, 0x1E, 0x06, 0x8C, 0xC8, 0x8E, 0xD8 };
	if (bootCs > 0) {
		const unsigned base = (unsigned)bootCs << 4;
		if (base >= 0x10u && base < 0x200000u) {
			unsigned end = base + 0xA000u;
			if (end > 0x200000u)
				end = 0x200000u;
			for (unsigned p = base; p + 20u < end; p++) {
				int fm = 0;
				if (memcmp(mem + p, kVdFm, sizeof(kVdFm)) == 0)
					fm = 1;
				else if (memcmp(mem + p, kVd2Fm, sizeof(kVd2Fm)) == 0)
					fm = 2;
				if (!fm)
					continue;
				int eoi8 = 0;
				for (unsigned q = p; q + 3u < p + 0x90u && q + 3u < end; q++) {
					if (mem[q] == 0xB0 && mem[q + 1] == 0x20 && mem[q + 2] == 0xE6
						&& mem[q + 3] == 0x08) {
						eoi8 = 1;
						break;
					}
				}
				if (!eoi8 || p < base || p - base > 0xFFFFu)
					continue;
				GlodiaPlantVec(mem, p, base, picMask);
				return;
			}
		}
	}
}

/* USMD.EXE は CS:0005 に INT 7E を植え AH=31 TSR。その INT21 の次命令は「既ロード」アンインストーラ（pushf; mov ax,3; int 7e…）。feti は 0270、hhg は 027C へずらした。AX=3100 のまま CS:IP を INT21 トランポリンに残すと、糊 INT 7D が .USO を開く前に TriggerPlay の PumpCycles が RESIDENT で中断。IRET でアンインストーラへ載せそこでパーク。 */
static int PatchUsmdUnloadHalt(uint8_t* mem, uint16_t cs, uint16_t ip)
{
	if (!mem || !cs || cs == (uint16_t)DOS98_TRAMP_SEG)
		return 0;
	const unsigned isr = Pc98DosLin(cs, 0x0005);
	const unsigned at = Pc98DosLin(cs, ip);
	if (isr + 9u >= 0x200000u || at + 4u >= 0x200000u)
		return 0;
	static const uint8_t kIsr[] = { 0x51, 0x52, 0x53, 0x55, 0x56, 0x57, 0x06, 0x1E, 0xBB };
	if (memcmp(mem + isr, kIsr, sizeof(kIsr)) != 0)
		return 0;
	if (mem[at] != 0x9C || mem[at + 1] != 0xB8
		|| mem[at + 2] != 0x03 || mem[at + 3] != 0x00)
		return 0;
	mem[at] = 0xF4;
	mem[at + 1] = 0xEB;
	mem[at + 2] = 0xFD;
	return 1;
}

/* FairyDust MFD.EXE（koukan2/madol MIDI）: Borland TSR。シェル `MFD L` は argv[1]='L' → keep() + setvect(0x42, ISR)。EXE のスイッチ表オフセット（CS:0422/0419）はヒープコードを指すので L が一致せずメニュー印刷後 AH=4C 終了。INT 42 はトランポリンのまま。mfd_98.com の再生経路（`INT 42 AX=3`）は no-op で IRET（midi=0/0）。本物 ISR は CS:1AE5 の pusha 枠（AX-1 で jmp cs:[bx+1F8]）。起動もその ISR ではなく CS:00D5（CRT abort）を setvect。INT 42 を pusha 枠へ向け、argv スイッチを飛ばして launch/keep へ。 */
static int s_mfdInt42Host;
static int s_midiDrvHostSmf;

/* PatchMfdExeTsr の実装 */
static void PatchMfdExeTsr(uint8_t* mem)
{
	if (!mem) return;
	const uint16_t cs = np2_reg_get(NP2_R_CS);
	if (!cs || cs == (uint16_t)DOS98_TRAMP_SEG)
		return;
	const unsigned base = Pc98DosLin(cs, 0);
	if (base + 0x8000u > 0x200000u)
		return;
	int isr = -1;
	for (unsigned o = 0; o + 40u < 0x8000u; o++) {
		if (mem[base + o] != 0x50 || mem[base + o + 1] != 0x53
			|| mem[base + o + 2] != 0x51 || mem[base + o + 3] != 0x52
			|| mem[base + o + 4] != 0x06 || mem[base + o + 5] != 0x1E
			|| mem[base + o + 6] != 0x56 || mem[base + o + 7] != 0x57
			|| mem[base + o + 8] != 0x55)
			continue;
		int ok = 0;
		for (unsigned k = 9; k < 40 && o + k + 3u < 0x8000u; k++) {
			if (mem[base + o + k] == 0x2E && mem[base + o + k + 1] == 0xFF
				&& mem[base + o + k + 2] == 0xA7) {
				ok = 1;
				break;
			}
		}
		if (ok) {
			isr = (int)o;
			break;
		}
	}
	if (isr < 0)
		return;
	s_mfdInt42Host = 1;
	/* CS:0000 の CRT `mov dx, DGROUP` はリロケ済み。ISR の `mov bp, 0x8A0` は未リロケ */
	if (mem[base] == 0xBA && mem[base + (unsigned)isr + 9] == 0xBD
		&& mem[base + (unsigned)isr + 12] == 0x8E
		&& mem[base + (unsigned)isr + 13] == 0xDD) {
		mem[base + (unsigned)isr + 10] = mem[base + 1];
		mem[base + (unsigned)isr + 11] = mem[base + 2];
	}
	/* ISR far 呼（9A off,seg）は MZ リロケ無しで出たのでリンク時セグメント（01A1/01FA/02C2）のままで #UD。ドライバイメージ残りも同じ。既リロケ（>= 0x1000）や BIOS 風語は飛ばす。 */
	for (unsigned o = 0; o + 5u < 0x9100u && base + o + 5u < 0x200000u; o++) {
		if (mem[base + o] != 0x9A)
			continue;
		const unsigned segAt = base + o + 3u;
		const unsigned seg = (unsigned)mem[segAt]
			| ((unsigned)mem[segAt + 1] << 8);
		if (seg < 0x80u || seg >= 0x800u)
			continue;
		const unsigned fix = seg + (unsigned)cs;
		if (fix > 0xFFFFu)
			continue;
		mem[segAt] = (uint8_t)(fix & 0xff);
		mem[segAt + 1] = (uint8_t)((fix >> 8) & 0xff);
		o += 4;
	}
	/* 同じ欠リロケ級: `mov ax/ds/bp, DGROUP` がまだ 08A0h */
	{
		const uint16_t dgFix = (uint16_t)(mem[base + 1] | (mem[base + 2] << 8));
		const uint16_t dgRaw = (uint16_t)(dgFix - cs);
		if (dgRaw >= 0x400u && dgRaw < 0x2000u) {
			for (unsigned o = 1; o + 3u < 0x9100u && base + o + 3u < 0x200000u; o++) {
				const uint8_t op = mem[base + o];
				if (op != 0xB8 && op != 0xBA && op != 0xBD)
					continue;
				const uint16_t v = (uint16_t)(mem[base + o + 1]
					| (mem[base + o + 2] << 8));
				if (v != dgRaw)
					continue;
				mem[base + o + 1] = (uint8_t)(dgFix & 0xff);
				mem[base + o + 2] = (uint8_t)((dgFix >> 8) & 0xff);
			}
		}
	}
	/* jmp cs:[bx+1F8] は CRT バイトを狙っていた。7 語ケース表は ISR IRET 直後（CS:1C08） */
	if (mem[base + (unsigned)isr + 0x1E] == 0x2E
		&& mem[base + (unsigned)isr + 0x1F] == 0xFF
		&& mem[base + (unsigned)isr + 0x20] == 0xA7
		&& mem[base + (unsigned)isr + 0x21] == 0xF8
		&& mem[base + (unsigned)isr + 0x22] == 0x01) {
		const unsigned tbl = (unsigned)isr + 0x123u;
		mem[base + (unsigned)isr + 0x21] = (uint8_t)(tbl & 0xff);
		mem[base + (unsigned)isr + 0x22] = (uint8_t)((tbl >> 8) & 0xff);
		static const unsigned kCase[] = {
			0x23, 0x2F, 0x37, 0x8F, 0xCF, 0xFD, 0x112
		};
		for (unsigned i = 0; i < 7; i++) {
			const unsigned tgt = (unsigned)isr + kCase[i];
			mem[base + tbl + i * 2u] = (uint8_t)(tgt & 0xff);
			mem[base + tbl + i * 2u + 1] = (uint8_t)((tgt >> 8) & 0xff);
		}
	}
	for (unsigned o = 0; o + 12u < 0x8000u; o++) {
		if (mem[base + o] == 0x0E && mem[base + o + 1] == 0xB8
			&& mem[base + o + 4] == 0x50 && mem[base + o + 5] == 0xB8
			&& mem[base + o + 6] == 0x42 && mem[base + o + 7] == 0x00
			&& mem[base + o + 8] == 0x50) {
			mem[base + o + 2] = (uint8_t)(isr & 0xff);
			mem[base + o + 3] = (uint8_t)(((unsigned)isr >> 8) & 0xff);
			break;
		}
	}
	int argcAt = -1, launchAt = -1;
	for (unsigned o = 0; o + 12u < 0x8000u; o++) {
		if (mem[base + o] == 0x83 && mem[base + o + 1] == 0x7E
			&& mem[base + o + 2] == 0x06 && mem[base + o + 3] == 0x02
			&& mem[base + o + 4] == 0x7D)
			argcAt = (int)o;
		if (mem[base + o] == 0x0E && mem[base + o + 1] == 0xE8
			&& mem[base + o + 4] == 0x0B && mem[base + o + 5] == 0xC0
			&& mem[base + o + 6] == 0x75 && mem[base + o + 8] == 0x1E
			&& mem[base + o + 9] == 0xB8 && mem[base + o + 10] == 0xB6
			&& mem[base + o + 11] == 0x00)
			launchAt = (int)o;
	}
	if (argcAt < 0 || launchAt < 0)
		return;
	const int rel = launchAt - (argcAt + 3);
	if (rel < -32768 || rel > 32767)
		return;
	mem[base + (unsigned)argcAt] = 0xE9;
	mem[base + (unsigned)argcAt + 1] = (uint8_t)(rel & 0xff);
	mem[base + (unsigned)argcAt + 2] = (uint8_t)((rel >> 8) & 0xff);
	mem[base + (unsigned)argcAt + 3] = 0x90;
	mem[base + (unsigned)argcAt + 4] = 0x90;
	mem[base + (unsigned)argcAt + 5] = 0x90;
}

/* 400 バイト mfd_98.com（INT 42 糊）: INT 7F のあと INT 18 AX=9801 を一度フックし ISR を本線として落ちる（pusha / IRET smash）。Night_s の 143 バイト COM は INT 18 をループ。setvect 後に止め、BootDos が 64KB COM 割当を保ったまま進む（AH=31 / 30h パラが CS:0290 を切る）。 */
static uint16_t s_mfd98GlueCs;

/* MfdSmfVar の実装 */
static unsigned MfdSmfVar(const uint8_t* p, unsigned n, unsigned* i)
{
	unsigned v = 0;
	for (int k = 0; k < 4 && *i < n; k++) {
		const uint8_t b = p[(*i)++];
		v = (v << 7) | (unsigned)(b & 0x7f);
		if (!(b & 0x80))
			break;
	}
	return v;
}

/* MfdSmfLooksStatus の実装 */
static int MfdSmfLooksStatus(const uint8_t* p, unsigned n, unsigned i)
{
	if (i >= n)
		return 0;
	const uint8_t st = p[i];
	if (st == 0xFF || st == 0xF0 || st == 0xF7)
		return 1;
	if (st < 0x80 || st >= 0xF8)
		return 0;
	const int nd = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2;
	if (i + (unsigned)nd >= n)
		return 0;
	for (int k = 1; k <= nd; k++) {
		if (p[i + (unsigned)k] & 0x80)
			return 0;
	}
	return 1;
}

/* SYNUP_98 は E0D0 へ OUT する前に #UD。TriggerPlay が常駐曲を歩く。下のトラック歩行が読めない配置の最後手段: 1 ストリーム、チャネル接頭として 9x とパラメータ 1、そのあとキャプチャ時計を進める（note, duration）対。 */
template<typename Cap, typename Tick>
/* HostWalkSynupsMdiFlat の実装 */
static void HostWalkSynupsMdiFlat(Cap cap, Tick tick, const uint8_t* p, unsigned n)
{
	unsigned start = 0;
	const unsigned hdrLim = (n < 256u) ? n : 256u;
	for (unsigned k = 8; k + 8u < hdrLim; k++) {
		if (p[k] != 0xff || p[k + 1] != 0xff || p[k + 2] != 0xff
			|| p[k + 3] != 0xff || p[k + 4] != 0xff || p[k + 5] != 0xff)
			continue;
		unsigned j = k + 6u;
		const unsigned lim = (k + 86u < n) ? (k + 86u) : n;
		while (j < lim) {
			if (p[j] >= 0x90 && p[j] <= 0x9f) {
				start = j;
				break;
			}
			j++;
		}
		if (start)
			break;
	}
	if (!start) {
		for (unsigned k = 8; k + 3u < n && k < 96u; k++) {
			if (p[k] == 1 && p[k + 1] == 0
				&& p[k + 2] >= 0x90 && p[k + 2] <= 0x9f) {
				start = k + 2u;
				break;
			}
		}
	}
	if (!start)
		return;
	unsigned i = start;
	uint8_t ch = 0;
	unsigned notes = 0;
	while (i < n && notes < 8000u) {
		const uint8_t b = p[i];
		if (b >= 0x80) {
			i++;
			ch = (uint8_t)(b & 0x0f);
			if (i < n)
				i++;
			continue;
		}
		const uint8_t note = b;
		i++;
		unsigned dur = 12;
		if (i < n && p[i] < 0x80)
			dur = p[i++];
		if (note >= 12 && note <= 108) {
			cap((uint8_t)(0x90 | ch));
			cap(note);
			cap((uint8_t)0x40);
			notes++;
			tick(dur);
		}
	}
}

/* 複合タイムライン上に置いた復号メッセージ 1 件 */
struct SynupsEv {
	unsigned tick;
	unsigned seq;
	uint8_t st;
	uint8_t d1;
	uint8_t d2;
	uint8_t nd;
};

/* SynupsEvCmp の実装 */
static int SynupsEvCmp(const void* a, const void* b)
{
	const SynupsEv* x = (const SynupsEv*)a;
	const SynupsEv* y = (const SynupsEv*)b;
	if (x->tick != y->tick)
		return (x->tick < y->tick) ? -1 : 1;
	if (x->seq != y->seq)
		return (x->seq < y->seq) ? -1 : 1;
	return 0;
}

/* GM/GS は 9 をドラム予約。メロディブロックはその上を踏む */
static uint8_t SynupsChanForTrack(unsigned partId)
{
	unsigned ch = partId;
	if (ch >= 9u)
		ch++;
	return (uint8_t)(ch & 15u);
}

/* バイト 9 はブロック数。ヘッダは 0xFF 6 連で終わる。各ブロックは [partId, NUL 終端名, events] で 0xFF で閉じる — 単独でもよい。直前バイトがコマンドオペランド 0xFF（BGM203B "A0 FF"）になり得る。ブロック内で < 0x80 はノート＋ゲート（4 分=48 tick）、0x80/0x81 休符、0x90 がパート音色、他の 0x8x/0x9x はオペランド 1。各ブロックは tick 0 から再開するのでストリーム前にマージが要る: 1 ストリーム読はパートを直列再生し（32s 曲が 2 分）、全ノートを 1 チャネルに載せ、Note Off もプログラムチェンジも出さなかった。 */
template<typename Cap, typename Tick>
/* HostWalkSynupsTracks の実装 */
static int HostWalkSynupsTracks(Cap cap, Tick tick, const uint8_t* p, unsigned n)
{
	const unsigned blocks = p[9];
	if (blocks < 1u || blocks > 32u)
		return 0;
	unsigned pos = 0;
	for (unsigned k = 8; k + 6u <= n; k++) {
		unsigned run = 0;
		while (run < 6u && p[k + run] == 0xff)
			run++;
		if (run >= 6u) {
			pos = k + 6u;
			break;
		}
	}
	if (!pos)
		return 0;

	enum { kEvMax = 16384 };
	SynupsEv* ev = (SynupsEv*)malloc(sizeof(SynupsEv) * kEvMax);
	if (!ev)
		return 0;
	unsigned evN = 0;
	unsigned seq = 0;
	for (unsigned blk = 0; blk < blocks && pos + 1u < n; blk++) {
		const uint8_t partId = p[pos];
		unsigned i = pos + 1u;
		while (i < n && p[i] != 0)
			i++;
		i++;
		/* リズムブロック（id bit 7、名 DRUMS）はキー表で開き、この歩行に等価の無いドラムマップを添字 */
		const int rhythm = (partId & 0x80) ? 1 : 0;
		const uint8_t ch = SynupsChanForTrack((unsigned)(partId & 0x7f));
		unsigned t = 0;
		unsigned next = n;
		int closed = 0;
		int seenNote = 0;
		while (i < n && evN + 2u < kEvMax) {
			if (p[i] == 0xff) {
				closed = 1;
				next = (i + 1u < n && p[i + 1] == 0xff) ? i + 2u : i + 1u;
				break;
			}
			const uint8_t b = p[i];
			if (b >= 0x80) {
				if (i + 1u >= n)
					break;
				const uint8_t arg = p[i + 1];
				i += 2;
				if (b == 0x80 || b == 0x81)
					t += arg;
				else if (b == 0x90 && !rhythm && !seenNote) {
					/* ブロック冒頭の音色だけ: hypersec MAIN.MDI のようにフレーズ毎に 0x90 を繰り返し、値はパッチ番号ではない。それを尊重するとパートが撹乱された。 */
					ev[evN].tick = t;
					ev[evN].seq = seq++;
					ev[evN].st = (uint8_t)(0xc0 | ch);
					ev[evN].d1 = (uint8_t)(arg & 0x7f);
					ev[evN].d2 = 0;
					ev[evN].nd = 1;
					evN++;
				}
				continue;
			}
			const uint8_t note = b;
			i++;
			unsigned dur = 0;
			if (i < n && p[i] < 0x80)
				dur = p[i++];
			seenNote = 1;
			if (!rhythm && note >= 12u && note <= 108u) {
				const unsigned gate = dur ? dur : 12u;
				ev[evN].tick = t;
				ev[evN].seq = seq;
				ev[evN].st = (uint8_t)(0x90 | ch);
				ev[evN].d1 = note;
				ev[evN].d2 = 0x40;
				ev[evN].nd = 2;
				evN++;
				ev[evN].tick = t + gate;
				ev[evN].seq = seq + 1;
				ev[evN].st = (uint8_t)(0x80 | ch);
				ev[evN].d1 = note;
				ev[evN].d2 = 0x40;
				ev[evN].nd = 2;
				evN++;
			}
			seq += 2;
			t += dur;
		}
		if (!closed)
			break;
		pos = next;
	}

	if (evN > 1)
		qsort(ev, evN, sizeof(SynupsEv), SynupsEvCmp);
	unsigned last = 0;
	for (unsigned k = 0; k < evN; k++) {
		if (ev[k].tick > last) {
			tick(ev[k].tick - last);
			last = ev[k].tick;
		}
		cap(ev[k].st);
		cap(ev[k].d1);
		if (ev[k].nd > 1)
			cap(ev[k].d2);
	}
	free(ev);
	return (evN > 0) ? 1 : 0;
}

template<typename Cap, typename Tick>
/* HostWalkSynupsMdi の実装 */
static void HostWalkSynupsMdi(Cap cap, Tick tick, const uint8_t* p, unsigned n)
{
	if (!p || n < 40u)
		return;
	if (memcmp(p, "SYNUPS", 6) != 0)
		return;
	if (HostWalkSynupsTracks(cap, tick, p, n))
		return;
	HostWalkSynupsMdiFlat(cap, tick, p, n);
}

/* Recomposer RCP v2。イベントは [cmd, delay, p1, p2]。cmd<0x80 はノート（p1 ゲート、p2 ベロ）。delay はヘッダ timebase tick（通常 48）。 */
template<typename Cap, typename Tick>
/* HostWalkRcp の実装 */
static void HostWalkRcp(Cap cap, Tick tick, const uint8_t* p, unsigned n)
{
	if (!p || n < 0x5C0u)
		return;
	if (memcmp(p, "RCM-PC98", 8) != 0 && memcmp(p, "RCM-PC88", 8) != 0)
		return;
	unsigned trkCnt = p[0x1E6];
	if (trkCnt == 0 || trkCnt > 18u)
		trkCnt = 18;
	unsigned pos = 0x206u + 0x200u + 0x180u;
	unsigned notes = 0;
	for (unsigned t = 0; t < trkCnt && pos + 0x2Cu <= n && notes < 8000u; t++) {
		unsigned tlen = (unsigned)p[pos] | ((unsigned)p[pos + 1] << 8);
		tlen = (tlen & ~3u) | ((tlen & 3u) << 16);
		if (tlen < 0x2Cu)
			break;
		const uint8_t midChn = p[pos + 4];
		const int dummy = (midChn & 0x80) ? 1 : 0;
		const uint8_t ch = (uint8_t)(midChn & 0x0f);
		unsigned i = pos + 0x2Cu;
		unsigned end = pos + tlen;
		if (end > n)
			end = n;
		while (i + 4u <= end && notes < 8000u) {
			const uint8_t cmd = p[i];
			const uint8_t delay = p[i + 1];
			if (cmd < 0x80 && !dummy) {
				const uint8_t vel = p[i + 3];
				if (vel) {
					cap((uint8_t)(0x90 | ch));
					cap(cmd);
					cap(vel);
					notes++;
					tick(delay ? delay : 1u);
				}
			} else if (delay)
				tick(delay);
			i += 4;
		}
		pos += tlen;
	}
}

/* Studio Twin'kle 圧縮 MD1: LE32 2 つ、そのあと FF 12 ヘッダチャンク。最初の FF レコード 2 つを飛ばし、残り対は（note, duration）。red/mirage MD1（FF 12 無し）には一致させない。 */
template<typename Cap, typename Tick>
/* HostWalkMd1 の実装 */
static void HostWalkMd1(Cap cap, Tick tick, const uint8_t* p, unsigned n)
{
	if (!p || n < 80u)
		return;
	if (p[8] != 0xff || p[9] != 0x12)
		return;
	unsigned i = 8;
	unsigned ff = 0;
	unsigned notes = 0;
	while (i < n && notes < 8000u) {
		if (p[i] != 0xff) {
			i++;
			continue;
		}
		ff++;
		i++;
		while (i + 1u < n && p[i] != 0xff && notes < 8000u) {
			if (ff > 2u) {
				const uint8_t lo = p[i];
				const uint8_t hi = p[i + 1];
				if (lo >= 24 && lo <= 96) {
					cap((uint8_t)0x90);
					cap(lo);
					cap((uint8_t)0x40);
					notes++;
					tick(hi ? hi : 12u);
				}
			}
			i += 2;
		}
	}
}

/* MfdRestoreInt42Trampoline の実装 */
static void MfdRestoreInt42Trampoline(uint8_t* mem)
{
	if (!mem) return;
	mem[0x42 * 4 + 0] = (uint8_t)((0x42u * 2u) & 0xff);
	mem[0x42 * 4 + 1] = 0;
	mem[0x42 * 4 + 2] = (uint8_t)(DOS98_TRAMP_SEG & 0xff);
	mem[0x42 * 4 + 3] = (uint8_t)((DOS98_TRAMP_SEG >> 8) & 0xff);
}

/* VALKY/SSCP: cmd8 は CS:[384B]/[384D] を試して曲バッファセグメント用に INT 50 AH=3。SSCP は INT 50 をフックしない。CSCP init は INT 7F setvec を飛ばし得る。糊 CS がまだ INT 21/B0 に見えるとき CS:0210 を植える。 */
static unsigned ValkyIvtSeg(const uint8_t* mem, uint8_t vec, uint16_t wantOff)
{
	if (!mem) return 0;
	const unsigned off = (unsigned)mem[vec * 4]
		| ((unsigned)mem[vec * 4 + 1] << 8);
	const unsigned seg = (unsigned)mem[vec * 4 + 2]
		| ((unsigned)mem[vec * 4 + 3] << 8);
	if (!seg || seg == (unsigned)DOS98_TRAMP_SEG || seg >= 0xF000u)
		return 0;
	if (wantOff && off != (unsigned)wantOff)
		return 0;
	return seg;
}

/* SSCP INT08 0DB5: CLD/TEST CS:。CSCP INT08 2DAB: PUSH DS/ES/PUSHA。
   裸の `TEST CS:[imm]`（CSCP API 00C9）は ISR ではない — そこに植えると
   IRQ0 が RET でスタックを壊し int06@CSCP:0104 になる。 */
static int ValkyLooksIsr(const uint8_t* mem, unsigned p)
{
	if (!mem || p + 6u >= 0x200000u)
		return 0;
	if (mem[p] == 0xFC && mem[p + 1] == 0x2E && mem[p + 2] == 0xF6)
		return 1;
	if (mem[p] == 0x1E && mem[p + 1] == 0x06 && mem[p + 2] == 0x60)
		return 1;
	if (mem[p] == 0xFC && mem[p + 1] == 0x1E && mem[p + 2] == 0x06
		&& mem[p + 3] == 0x60)
		return 1;
	return 0;
}

static unsigned ValkyFindIsrOff(const uint8_t* mem, unsigned seg, unsigned n)
{
	if (!mem || !seg || seg >= 0xA000u)
		return 0;
	const unsigned dst = seg << 4;
	static const unsigned kOff[] = { 0x0DD2u, 0x0DB5u, 0x2DABu, 0x0EA0u, 0x0ECEu, 0x3151u, 0x3090u };
	if (!n)
		n = 0x8000u;
	for (unsigned i = 0; i < 7; i++) {
		if (kOff[i] + 6u < n && dst + kOff[i] + 6u < 0x200000u
			&& ValkyLooksIsr(mem, dst + kOff[i]))
			return kOff[i];
	}
	const unsigned scanN = (n < 0x8000u) ? n : 0x8000u;
	for (unsigned o = 0x400u; o + 6u < scanN && dst + o + 6u < 0x200000u; o++) {
		if (ValkyLooksIsr(mem, dst + o))
			return o;
	}
	return 0;
}

/* hinadori SSCP BSS は 38D3=C0007F。mariner/injuda はコードが伸び 44D0/45A5。
   CSCP も 3F28 に同じ印があるので ISR=2DAB のときは使わない。
   HostBindGmd が 38D1 へテンポを書くと C0007F が消える。その後 Δ=0 で
   38A9 を poke すると mariner のコードを壊し keyOn=0 になる。
   消えたあとは ISR の `TEST [38BB+Δ],FF / JNS` から Δ を取る。 */
static unsigned ValkySscpBssDelta(const uint8_t* mem, unsigned dst)
{
	if (!mem || dst + 0x38D6u >= 0x200000u)
		return 0;
	if (mem[dst + 0x38D3] == 0xC0 && mem[dst + 0x38D4] == 0
		&& mem[dst + 0x38D5] == 0x7F)
		return 0;
	for (unsigned o = 0x3000u; o + 8u < 0x5000u && dst + o + 8u < 0x200000u; o++) {
		if (mem[dst + o] == 0x04 && mem[dst + o + 1] == 0x04
			&& mem[dst + o + 2] == 0xC0 && mem[dst + o + 3] == 0
			&& mem[dst + o + 4] == 0x7F
			&& o >= 0x16u && mem[dst + o - 0x16u] == 0xFF
			&& mem[dst + o - 0x15u] == 0xFF)
			return o - 0x38D1u;
	}
	const unsigned cs = dst >> 4;
	if (cs < 0x1000u || cs >= 0xA000u)
		return 0;
	const unsigned isr = ValkyFindIsrOff(mem, cs, 0x8000u);
	if (!isr || isr == 0x2DABu)
		return 0;
	const unsigned start = dst + isr;
	for (unsigned i = 0; i + 6u < 0x200u && start + i + 6u < 0x200000u; i++) {
		if (mem[start + i] == 0xF6 && mem[start + i + 1] == 0x06
			&& mem[start + i + 4] == 0xFF && mem[start + i + 5] == 0x79) {
			const unsigned imm = (unsigned)mem[start + i + 2]
				| ((unsigned)mem[start + i + 3] << 8);
			if (imm >= 0x38BBu && imm < 0x5000u)
				return imm - 0x38BBu;
		}
	}
	return 0;
}

static unsigned ValkySscpAt(const uint8_t* mem, unsigned dst, unsigned off)
{
	return off + ValkySscpBssDelta(mem, dst);
}

/* ISR が `MOV AX,[imm]; MOV DS,AX` するチャネル表。hinadori は 388B、mariner は 448D（Δ+5）。 */
static unsigned ValkySscpChPtrOff(const uint8_t* mem, unsigned cs)
{
	const unsigned dst = cs << 4;
	unsigned def = ValkySscpAt(mem, dst, 0x388Bu);
	if (!mem || cs < 0x1000u || cs >= 0xA000u)
		return def;
	unsigned isr = ValkyFindIsrOff(mem, cs, 0x8000u);
	if (!isr)
		isr = (unsigned)mem[0x08 * 4] | ((unsigned)mem[0x08 * 4 + 1] << 8);
	if (!isr || isr >= 0x8000u)
		return def;
	const unsigned start = dst + isr;
	for (unsigned i = 0; i + 5u < 0x280u && start + i + 5u < 0x200000u; i++) {
		if (mem[start + i] == 0xA1 && mem[start + i + 3] == 0x8E
			&& mem[start + i + 4] == 0xD8) {
			const unsigned imm = (unsigned)mem[start + i + 1]
				| ((unsigned)mem[start + i + 2] << 8);
			if (imm >= 0x3000u && imm < 0x5000u)
				return imm;
		}
	}
	return def;
}

/* mariner は keep/busy/port が 38xx+Δ から 1 バイトずれ、固定オフセットだと
   [44A6] を叩いて [44A7] の keep が 0 のまま ISR が即 return する。 */
static unsigned ValkySscpScanIsrImm(const uint8_t* mem, unsigned cs,
	unsigned defOff, uint8_t a0, uint8_t a1, uint8_t a4, uint8_t a5, int useA5)
{
	unsigned def = ValkySscpAt(mem, cs << 4, defOff);
	if (!mem || cs < 0x1000u || cs >= 0xA000u)
		return def;
	unsigned isr = ValkyFindIsrOff(mem, cs, 0x8000u);
	if (!isr)
		isr = (unsigned)mem[0x08 * 4] | ((unsigned)mem[0x08 * 4 + 1] << 8);
	if (!isr || isr == 0x2DABu || isr >= 0x8000u)
		return def;
	const unsigned start = (cs << 4) + isr;
	for (unsigned i = 0; i + 5u < 0x80u && start + i + 5u < 0x200000u; i++) {
		if (mem[start + i] == a0 && mem[start + i + 1] == a1
			&& mem[start + i + 4] == a4
			&& (!useA5 || mem[start + i + 5] == a5)) {
			const unsigned imm = (unsigned)mem[start + i + 2]
				| ((unsigned)mem[start + i + 3] << 8);
			if (imm >= 0x3000u && imm < 0x5000u)
				return imm;
		}
	}
	return def;
}

static unsigned ValkySscpKeepOff(const uint8_t* mem, unsigned cs)
{
	return ValkySscpScanIsrImm(mem, cs, 0x38A9u, 0xF6, 0x06, 0xFF, 0x74, 1);
}

static unsigned ValkySscpBusyOff(const uint8_t* mem, unsigned cs)
{
	return ValkySscpScanIsrImm(mem, cs, 0x390Au, 0xC6, 0x06, 0xFF, 0, 0);
}

static unsigned ValkySscpYmFlagOff(const uint8_t* mem, unsigned cs)
{
	return ValkySscpScanIsrImm(mem, cs, 0x38BBu, 0xF6, 0x06, 0xFF, 0x79, 1);
}

static unsigned ValkySscpPortOff(const uint8_t* mem, unsigned cs)
{
	const unsigned ym = ValkySscpYmFlagOff(mem, cs);
	unsigned def = (ym >= 4u) ? (ym - 4u) : ValkySscpAt(mem, cs << 4, 0x38B7u);
	if (!mem || cs < 0x1000u || cs >= 0xA000u || ym < 0x3008u)
		return def;
	const unsigned dst = cs << 4;
	unsigned best = 0, bestN = 0;
	for (unsigned o = 0; o + 4u < 0x4000u && dst + o + 4u < 0x200000u; o++) {
		if (mem[dst + o] != 0x8B || mem[dst + o + 1] != 0x16)
			continue;
		const unsigned imm = (unsigned)mem[dst + o + 2]
			| ((unsigned)mem[dst + o + 3] << 8);
		if (imm + 8u < ym || imm >= ym)
			continue;
		unsigned n = 0;
		for (unsigned p = o; p + 4u < 0x4000u && dst + p + 4u < 0x200000u; p++) {
			if (mem[dst + p] == 0x8B && mem[dst + p + 1] == 0x16
				&& mem[dst + p + 2] == (uint8_t)(imm & 0xff)
				&& mem[dst + p + 3] == (uint8_t)(imm >> 8))
				n++;
		}
		if (n > bestN) {
			bestN = n;
			best = imm;
		}
	}
	return best ? best : def;
}

static unsigned ValkySscpTmplOff(const uint8_t* mem, unsigned cs)
{
	unsigned def = ValkySscpAt(mem, cs << 4, 0x3935u);
	if (!mem || cs < 0x1000u || cs >= 0xA000u)
		return def;
	const unsigned dst = cs << 4;
	for (unsigned o = 0; o + 3u < 0x5000u && dst + o + 3u < 0x200000u; o++) {
		if (mem[dst + o] != 0xBE)
			continue;
		const unsigned imm = (unsigned)mem[dst + o + 1]
			| ((unsigned)mem[dst + o + 2] << 8);
		if (imm >= 0x3000u && imm + 2u < 0x5000u && dst + imm + 2u < 0x200000u
			&& mem[dst + imm] == 0x8F && mem[dst + imm + 1] == 0)
			return imm;
	}
	return def;
}

static void ValkyPlantIsr(uint8_t* mem, unsigned seg, unsigned off)
{
	if (!mem || !seg || !off)
		return;
	mem[0x08 * 4 + 0] = (uint8_t)(off & 0xff);
	mem[0x08 * 4 + 1] = (uint8_t)(off >> 8);
	mem[0x08 * 4 + 2] = (uint8_t)(seg & 0xff);
	mem[0x08 * 4 + 3] = (uint8_t)(seg >> 8);
	const unsigned dst = seg << 4;
	if (off == 0x2DABu) {
		if (dst + 0x38A9u < 0x200000u)
			mem[dst + 0x38A9] = 1;
		/* CSCP INT08 2DAB は [327A] が 0 だとシーケンサを飛ばす */
		if (dst + 0x327Au < 0x200000u)
			mem[dst + 0x327A] = 1;
		return;
	}
	const unsigned a9 = ValkySscpKeepOff(mem, seg);
	if (dst + a9 < 0x200000u)
		mem[dst + a9] = 1;
}

static int ValkySegLooksDriver(const uint8_t* mem, unsigned seg)
{
	if (!mem || seg < 0x1000u || seg >= 0xA000u)
		return 0;
	const unsigned d = seg << 4;
	if (d + 0x62u >= 0x200000u)
		return 0;
	if (mem[d] != 0xF1 || mem[d + 1] != 0x11)
		return 0;
	if (mem[d + 0x60] == 0xFC && mem[d + 0x61] == 0x32 && mem[d + 0x62] == 0xE4)
		return 1;
	if (mem[d + 0x78] == 0xFC && mem[d + 0x79] == 0x32 && mem[d + 0x7A] == 0xE4)
		return 1;
	return 0;
}

/* CSCP API 8 は [3EB4]!=0 かつ [3EB5]=曲テーブル。INT 50 は zip に無くホストが供給する。
   ゲスト stub @0000:0600 は AH=8 を no-op にするのでトランポリンへ戻す。 */
static char s_valkySongName[96];
static unsigned s_valkySongSeg = 0;
static unsigned s_valkyAllocSeg = 0x5000;
static unsigned s_valkyAllocOff = 0;
static unsigned s_valkyStreamSeg = 0;
static unsigned s_valkyStreamOff = 0;
static unsigned s_valkyStreamLen = 0;

static void ValkyPlantTramp(uint8_t* mem, uint8_t vec)
{
	if (!mem)
		return;
	mem[vec * 4 + 0] = (uint8_t)((vec * 2u) & 0xff);
	mem[vec * 4 + 1] = 0;
	mem[vec * 4 + 2] = (uint8_t)(DOS98_TRAMP_SEG & 0xff);
	mem[vec * 4 + 3] = (uint8_t)((DOS98_TRAMP_SEG >> 8) & 0xff);
}

static unsigned ValkyDriverCs(const uint8_t* mem)
{
	if (!mem)
		return 0;
	const unsigned s08 = (unsigned)mem[0x08 * 4 + 2]
		| ((unsigned)mem[0x08 * 4 + 3] << 8);
	if (ValkySegLooksDriver(mem, s08))
		return s08;
	const unsigned glue = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	if (glue && glue != (unsigned)DOS98_TRAMP_SEG) {
		const unsigned gb = glue << 4;
		if (gb + 0x420u < 0x200000u) {
			const unsigned es = (unsigned)mem[gb + 0x41E]
				| ((unsigned)mem[gb + 0x41F] << 8);
			if (ValkySegLooksDriver(mem, es))
				return es;
		}
	}
	if (ValkySegLooksDriver(mem, 0x2800u))
		return 0x2800u;
	if (ValkySegLooksDriver(mem, 0x2002u))
		return 0x2002u;
	return s08;
}

static unsigned ValkySscpCs(const uint8_t* mem)
{
	if (!mem)
		return 0;
	const unsigned off = (unsigned)mem[0x08 * 4]
		| ((unsigned)mem[0x08 * 4 + 1] << 8);
	const unsigned cs = (unsigned)mem[0x08 * 4 + 2]
		| ((unsigned)mem[0x08 * 4 + 3] << 8);
	if (off != 0x2DABu && cs >= 0x1000u && cs < 0xA000u
		&& ValkyLooksIsr(mem, (cs << 4) + off)
		&& ValkySegLooksDriver(mem, cs))
		return cs;
	return ValkyDriverCs(mem);
}

static int ValkyIsSscp(const uint8_t* mem)
{
	if (!mem)
		return 0;
	const unsigned off = (unsigned)mem[0x08 * 4]
		| ((unsigned)mem[0x08 * 4 + 1] << 8);
	const unsigned ics = (unsigned)mem[0x08 * 4 + 2]
		| ((unsigned)mem[0x08 * 4 + 3] << 8);
	if (off == 0x2DABu)
		return 0;
	if (off == 0x0DB5u || off == 0x0DD2u)
		return 1;
	if (ics >= 0x1000u && ics < 0xA000u
		&& ValkyLooksIsr(mem, (ics << 4) + off)
		&& ValkySegLooksDriver(mem, ics))
		return 1;
	const unsigned cs = ValkyDriverCs(mem);
	if (!cs || cs >= 0xA000u)
		return 0;
	const unsigned dst = cs << 4;
	/* API8 `TEST CS:[384B],1` — INT08 がトランポリンでも SSCP と分かる */
	if (dst + 0x2F8u < 0x200000u
		&& mem[dst + 0x2F2] == 0x2E && mem[dst + 0x2F3] == 0xF6
		&& mem[dst + 0x2F4] == 0x06 && mem[dst + 0x2F5] == 0x4B
		&& mem[dst + 0x2F6] == 0x38)
		return 1;
	/* mariner/injuda: ISR は 3090/3151。2DAB なら CSCP。 */
	const unsigned isr = ValkyFindIsrOff(mem, cs, 0x8000u);
	if (isr && isr != 0x2DABu)
		return 1;
	return 0;
}

static void ValkyPokeSscpWork(uint8_t* mem)
{
	if (!mem)
		return;
	const unsigned cs = ValkySscpCs(mem);
	if (!cs || cs == (unsigned)DOS98_TRAMP_SEG || cs >= 0xA000u)
		return;
	const unsigned dst = cs << 4;
	const unsigned dlt = ValkySscpBssDelta(mem, dst);
	const unsigned chOff = (0x3B20u + dlt + 0x0Fu) & ~0x0Fu;
	const unsigned ch2Off = (0x49C0u + dlt + 0x0Fu) & ~0x0Fu;
	const unsigned ch = cs + (chOff >> 4);
	const unsigned ch2 = cs + (ch2Off >> 4);
	const unsigned o388b = ValkySscpChPtrOff(mem, cs);
	const unsigned o3895 = o388b + 0x0Au;
	if (dst + o3895 + 1u < 0x200000u) {
		Pc98Wr16(mem, dst + o388b, (uint16_t)ch);
		Pc98Wr16(mem, dst + o3895, (uint16_t)ch2);
	}
	const unsigned o38a9 = ValkySscpKeepOff(mem, cs);
	if (dst + o38a9 < 0x200000u)
		mem[dst + o38a9] = 1;
	const unsigned o384b = 0x384Bu + dlt;
	/* mariner の 384B+Δ はコード。hinadori/injuda は `03 FF` BSS。 */
	if (dst + o384b + 2u < 0x200000u && mem[dst + o384b + 1u] == 0xFF) {
		mem[dst + o384b] = (uint8_t)(mem[dst + o384b] | 1u);
		mem[dst + o384b + 2u] = 0xFF;
	}
	/* ファイル既定 YM フラグ=FF は「未検出」。284e が OUT を飛ばし 0x88 のまま。 */
	const unsigned o38bb = ValkySscpYmFlagOff(mem, cs);
	const unsigned o38b7 = ValkySscpPortOff(mem, cs);
	if (dst + o38bb < 0x200000u)
		mem[dst + o38bb] = 1;
	if (dst + o38b7 + 1u < 0x200000u)
		Pc98Wr16(mem, dst + o38b7, 0x0188);
	const unsigned o38f7 = 0x38F7u + dlt;
	if (dst + o38f7 + 8u < 0x200000u) {
		for (unsigned i = 0; i < 9u; i++)
			mem[dst + o38f7 + i] = 0xFF;
	}
	s_valkyAllocSeg = ch;
	if (s_valkyAllocOff < 0x80u)
		s_valkyAllocOff = 0x80u;
}

/* INT 50 AH=4: GMD トラックをチャンネルへ。LES SI,[0Eh]、[0] bit7=停止。ヘッダ 16 バイトはサイズ+ch。 */
static void ValkySscpBindTrack(uint8_t* mem, uint8_t ch,
	uint16_t dataSeg, uint16_t dataOff)
{
	if (!mem || !ch || ch > 18u)
		return;
	ValkyPokeSscpWork(mem);
	const unsigned cs = ValkySscpCs(mem);
	if (!cs || cs >= 0xA000u)
		return;
	const unsigned dst = cs << 4;
	const unsigned o388b = ValkySscpChPtrOff(mem, cs);
	if (dst + o388b + 1u >= 0x200000u)
		return;
	const unsigned base = (unsigned)mem[dst + o388b]
		| ((unsigned)mem[dst + o388b + 1u] << 8);
	if (!base || base >= 0xA000u)
		return;
	const unsigned chSeg = base + (unsigned)(ch - 1u) * 0xDu;
	const unsigned p = chSeg << 4;
	if (p + 0xAEu >= 0x200000u)
		return;
	uint16_t body = dataOff;
	const unsigned db = (unsigned)dataSeg << 4;
	if (dataSeg && db + (unsigned)dataOff + 16u < 0x200000u) {
		const unsigned sz = (unsigned)mem[db + dataOff]
			| ((unsigned)mem[db + dataOff + 1u] << 8);
		if (sz >= 0x10u && mem[db + dataOff + 2u] == ch) {
			int z = 1;
			for (unsigned i = 3; i < 16u; i++) {
				if (mem[db + dataOff + i]) {
					z = 0;
					break;
				}
			}
			if (z)
				body = (uint16_t)(dataOff + 0x10u);
		}
	}
	Pc98Wr16(mem, p + 0x0Eu, body);
	Pc98Wr16(mem, p + 0x10u, dataSeg);
	/* ED 01 は [30] へ巻き戻す。未設定だと SI=0 で GMD ヘッダを食って停止する。 */
	Pc98Wr16(mem, p + 0x30u, body);
	mem[p] = (uint8_t)(mem[p] & 0x7Fu);
	if (!mem[p])
		mem[p] = 1;
	mem[p + 5] = (uint8_t)(ch - 1u);
	if (mem[p + 0x1Cu] == 0)
		mem[p + 0x1Cu] = 1;
	mem[p + 0xADu] = 0xFF;
	if (mem[p + 0x6Bu] == 0)
		mem[p + 0x6Bu] = 0x0F;
}

/* SSCP 初期化 567A/2702 は PIT ch0 を 0x7EE/0x1900 にする。スキップすると BIOS 60Hz のまま delay が数秒になり FAIL_SHORT。 */
static void ValkySscpArmPit(void)
{
	if (!g_pc98Active)
		return;
	g_pc98Active->SscpForcePit(0x1900);
}

static void ValkySscpKeepAlive(uint8_t* mem)
{
	if (!mem || !g_pc98Active)
		return;
	unsigned sscp = 0;
	auto tryPlant = [&](unsigned cs) {
		if (sscp || !cs || cs < 0x1000u || cs >= 0xA000u)
			return;
		const unsigned isr = ValkyFindIsrOff(mem, cs, 0x8000u);
		if (isr && isr != 0x2DABu) {
			ValkyPlantIsr(mem, cs, isr);
			sscp = cs;
		}
	};
	tryPlant(ValkySscpCs(mem));
	tryPlant(0x2800u);
	tryPlant(0x2002u);
	tryPlant(ValkyDriverCs(mem));
	if (!sscp) {
		ValkyReplantIsr(mem);
		return;
	}
	g_pc98Active->SscpForcePit(0x1900);
	g_pc98Active->picMask_ = 0xFEu;
	const unsigned dst = sscp << 4;
	const unsigned o38a9 = ValkySscpKeepOff(mem, sscp);
	const unsigned o390a = ValkySscpBusyOff(mem, sscp);
	const unsigned o388b = ValkySscpChPtrOff(mem, sscp);
	const unsigned o38c8 = ValkySscpAt(mem, dst, 0x38C8u);
	if (dst + o38a9 < 0x200000u)
		mem[dst + o38a9] = 1;
	if (dst + o390a < 0x200000u)
		mem[dst + o390a] = 0;
	if (dst + o388b + 1u >= 0x200000u)
		return;
	const unsigned base = (unsigned)mem[dst + o388b]
		| ((unsigned)mem[dst + o388b + 1u] << 8);
	if (!base || base >= 0xA000u)
		return;
	unsigned nch = 6;
	if (dst + o38c8 < 0x200000u && mem[dst + o38c8]
		&& mem[dst + o38c8] <= 18u)
		nch = mem[dst + o38c8];
	if (nch > 18u)
		nch = 18u;
	for (unsigned i = 0; i < nch; i++) {
		const unsigned p = (base + i * 0xDu) << 4;
		if (p + 0x32u >= 0x200000u)
			break;
		const unsigned loop = (unsigned)mem[p + 0x30]
			| ((unsigned)mem[p + 0x31] << 8);
		if (!loop)
			continue;
		if (mem[p] & 0x80) {
			mem[p] = (uint8_t)(mem[p] & 0x7Fu);
			if (!mem[p])
				mem[p] = 1;
			Pc98Wr16(mem, p + 0x0Eu, (uint16_t)loop);
			mem[p + 0x1Cu] = 1;
		}
	}
}

static void ValkySscpYmOff(uint8_t reg, uint8_t data, uint8_t hw)
{
	if (!g_pc98Active || hw > 5u)
		return;
	uint8_t dl = hw;
	int hi = 0;
	if (dl >= 3u) {
		hi = 1;
		dl = (uint8_t)(dl - 3u);
	}
	const uint8_t r = (uint8_t)((reg & 0xFCu) | dl);
	if (hi) {
		g_pc98Active->PortOut(0x18C, r);
		g_pc98Active->PortOut(0x18E, data);
	} else {
		g_pc98Active->PortOut(0x188, r);
		g_pc98Active->PortOut(0x18A, data);
	}
}

/* 1D52: 40 バイト音色を YM へ。メニュー曲は instrument opcode が無く TL/MUL が 0 のまま。 */
static void ValkySscpApplyInstYm(const uint8_t* inst, uint8_t hw, uint8_t slot)
{
	if (!inst)
		return;
	if (!slot)
		slot = 0x0F;
	const uint8_t fb = inst[0x27];
	ValkySscpYmOff(0xB4, (uint8_t)((fb & 0xC0) ? (fb & 0xC0) : 0xC0), hw);
	uint8_t bh = 1;
	uint8_t ah = 0x40;
	const uint8_t* si = inst + 0x0F;
	for (int i = 0; i < 4; i++) {
		const uint8_t al = *si++;
		if (bh & slot)
			ValkySscpYmOff(ah, al, hw);
		bh = (uint8_t)(bh << 1);
		ah = (uint8_t)(ah + 4);
	}
	bh = 0x11;
	ah = 0x50;
	for (int i = 0; i < 16; i++) {
		const uint8_t al = *si++;
		if (bh & slot)
			ValkySscpYmOff(ah, al, hw);
		bh = (uint8_t)((uint8_t)(bh << 1) | (uint8_t)(bh >> 7));
		ah = (uint8_t)(ah + 4);
	}
	bh = 1;
	ah = 0x30;
	for (int i = 0; i < 4; i++) {
		const uint8_t al = *si++;
		if (bh & slot)
			ValkySscpYmOff(ah, al, hw);
		bh = (uint8_t)(bh << 1);
		ah = (uint8_t)(ah + 4);
	}
	if (slot & 1)
		ValkySscpYmOff(0xB0, (uint8_t)(*si & 0x3F), hw);
}

static unsigned ValkyGmdU16(const uint8_t* g, unsigned n, unsigned off)
{
	if (off + 1u >= n)
		return 0;
	return (unsigned)g[off] | ((unsigned)g[off + 1u] << 8);
}

/* INT 51 の CALL 535 がテーブル/CX で落ちても、hoot の 1 曲 GMD をチャンネルへ載せる */
static void ValkySscpHostBindGmd(uint8_t* mem, CEmuDos98* dos, const char* song)
{
	if (!mem)
		return;
	ValkyPokeSscpWork(mem);
	const unsigned cs = ValkySscpCs(mem);
	if (!cs || cs >= 0xA000u)
		return;
	const unsigned dst = cs << 4;
	const unsigned dlt = ValkySscpBssDelta(mem, dst);
	const unsigned o388b = ValkySscpChPtrOff(mem, cs);
	const unsigned o3935 = ValkySscpTmplOff(mem, cs);
	const unsigned base = (dst + o388b + 1u < 0x200000u)
		? ((unsigned)mem[dst + o388b] | ((unsigned)mem[dst + o388b + 1u] << 8))
		: 0;
	if (base && base < 0xA000u && dst + o3935 + 0xC8u < 0x200000u) {
		for (unsigned i = 0; i < 18u; i++) {
			const unsigned p = (base + i * 0xDu) << 4;
			if (p + 0xC8u >= 0x200000u)
				break;
			memcpy(mem + p, mem + dst + o3935, 0xC8u);
		}
	}
	unsigned songSeg = s_valkySongSeg;
	if (!songSeg || songSeg >= 0xF000u)
		songSeg = 0x4000u;
	unsigned gp = songSeg << 4;
	const CEmuDos98File* f = (dos && song && song[0]) ? dos->FindFile(song) : NULL;
	if (gp + 8u < 0x200000u
		&& !(mem[gp] == 'G' && mem[gp + 1] == 'M'
			&& mem[gp + 2] == 'D' && mem[gp + 3] == '0')
		&& f && f->data && f->size >= 8u
		&& f->data[0] == 'G' && f->data[1] == 'M') {
		unsigned n = f->size;
		if (n > 0x7FF0u)
			n = 0x7FF0u;
		if (gp + n < 0x200000u)
			memcpy(mem + gp, f->data, n);
	}
	if (gp + 0x40u >= 0x200000u
		|| mem[gp] != 'G' || mem[gp + 1] != 'M'
		|| mem[gp + 2] != 'D' || mem[gp + 3] != '0')
		return;
	unsigned n = 0x7FF0u;
	if (f && f->size && f->size < n)
		n = f->size;
	const uint8_t* g = mem + gp;
	if (dst + 0x38DBu + dlt < 0x200000u) {
		/* [38D3]==0 だと opcode 98 の MUL/DIV が 0 除算で ISR が死ぬ */
		const unsigned raw = ValkyGmdU16(g, n, 0x0Cu);
		Pc98Wr16(mem, dst + 0x38D1u + dlt, (uint16_t)((raw << 8) | (raw >> 8)));
		unsigned d3 = ValkyGmdU16(g, n, 0x0Eu);
		if (!d3)
			d3 = 0xC0u;
		Pc98Wr16(mem, dst + 0x38D3u + dlt, (uint16_t)d3);
		if (n > 0x11u && g[0x11])
			mem[dst + 0x38C8u + dlt] = g[0x11];
		if (mem[dst + 0x38DBu + dlt] == 0 || mem[dst + 0x38DBu + dlt] == 0xFF)
			mem[dst + 0x38DBu + dlt] = 1;
	}
	unsigned si = 0x20u;
	if (si + 2u >= n)
		return;
	si += 2u + ValkyGmdU16(g, n, si);
	if (si + 2u >= n)
		return;
	unsigned instSeg = 0;
	if (dst + 0x3896u + dlt < 0x200000u)
		instSeg = (unsigned)mem[dst + 0x3895u + dlt]
			| ((unsigned)mem[dst + 0x3896u + dlt] << 8);
	if (!instSeg || instSeg >= 0xA000u)
		instSeg = cs + ((((0x49C0u + dlt) + 0x0Fu) & ~0x0Fu) >> 4);
	unsigned firstInst = 0xFFFFu;
	uint8_t fInst[19];
	memset(fInst, 0xFF, sizeof(fInst));
	unsigned cnt = ValkyGmdU16(g, n, si);
	si += 2u;
	if (cnt) {
		const unsigned rec = ValkyGmdU16(g, n, si) >> 8;
		si += 2u;
		for (unsigned i = 0; i < cnt; i++) {
			if (si + rec > n)
				break;
			const uint8_t id = g[si];
			const unsigned ip = (instSeg << 4) + (unsigned)id * 0x28u;
			unsigned copy = rec;
			if (copy > 0x28u)
				copy = 0x28u;
			if (ip + copy < 0x200000u)
				memcpy(mem + ip, g + si, copy);
			if (firstInst == 0xFFFFu)
				firstInst = id;
			si += rec;
		}
	}
	if (si & 1u)
		si++;
	if (si + 2u >= n)
		return;
	cnt = ValkyGmdU16(g, n, si);
	si += 2u;
	if (cnt) {
		const unsigned rec = ValkyGmdU16(g, n, si) >> 8;
		si += 2u;
		for (unsigned i = 0; i < cnt; i++) {
			if (si + rec > n)
				break;
			const uint8_t id = g[si];
			const unsigned pp = dst + 0x80Au + (unsigned)id * 0x14u;
			unsigned copy = rec;
			if (copy > 0x14u)
				copy = 0x14u;
			if (pp + copy < 0x200000u && pp + copy <= dst + 0xAC0u)
				memcpy(mem + pp, g + si, copy);
			if (id < 19u && rec > 1u)
				fInst[id] = g[si + 1u];
			si += rec;
		}
	}
	if (si & 1u)
		si++;
	si += 6u;
	if (si + 2u >= n)
		return;
	if (ValkyGmdU16(g, n, si) != 0)
		return;
	si += 2u;
	if (si + 2u >= n)
		return;
	cnt = ValkyGmdU16(g, n, si);
	si += 2u;
	s_valkySongSeg = songSeg;
	for (unsigned i = 0; i < cnt && i < 18u; i++) {
		if (si + 3u >= n)
			break;
		const unsigned tsz = ValkyGmdU16(g, n, si);
		const uint8_t ch = g[si + 2u];
		if (!tsz || si + tsz > n)
			break;
		ValkySscpBindTrack(mem, ch, (uint16_t)songSeg, (uint16_t)si);
		uint8_t instId = (ch < 19u) ? fInst[ch] : 0xFF;
		if (instId == 0xFF)
			instId = (firstInst == 0xFFFFu) ? 1 : (uint8_t)firstInst;
		const unsigned ip = (instSeg << 4) + (unsigned)instId * 0x28u;
		uint8_t hw = 0xFF;
		if (ch >= 1u && ch <= 6u)
			hw = (uint8_t)(ch - 1u);
		else if (ch >= 7u && ch <= 9u)
			hw = (uint8_t)(ch - 4u);
		if (hw != 0xFF && ip + 0x28u < 0x200000u)
			ValkySscpApplyInstYm(mem + ip, hw, 0x0F);
		si += tsz;
	}
	ValkySscpArmPit();
}

static void ValkyPokeCscpPlay(uint8_t* mem)
{
	if (!mem || !s_valkyKeepIrq0)
		return;
	if (ValkyIsSscp(mem)) {
		ValkyPokeSscpWork(mem);
		return;
	}
	const unsigned cs = ValkyDriverCs(mem);
	if (!cs || cs == (unsigned)DOS98_TRAMP_SEG || cs >= 0xA000u)
		return;
	const unsigned dst = cs << 4;
	unsigned streamSeg = s_valkyStreamSeg;
	unsigned streamOff = s_valkyStreamOff;
	unsigned streamLen = s_valkyStreamLen;
	if (!streamSeg) {
		streamSeg = s_valkySongSeg ? (s_valkySongSeg + 1u) : 0x4001u;
		streamOff = 0x20u;
		streamLen = 0xFFF0u;
	}
	if (dst + 0x328Eu < 0x200000u) {
		mem[dst + 0x327A] = 1;
		Pc98Wr16(mem, dst + 0x3288, (uint16_t)streamOff);
		Pc98Wr16(mem, dst + 0x328A, (uint16_t)streamSeg);
		Pc98Wr16(mem, dst + 0x328C, 0xFFF0);
		if (mem[dst + 0x3280] == 0 && mem[dst + 0x3281] == 0)
			mem[dst + 0x3280] = 1;
	}
	if (dst + 0x3EB6u < 0x200000u && s_valkySongSeg) {
		mem[dst + 0x3EB1] = (uint8_t)(mem[dst + 0x3EB1] | 1u);
		mem[dst + 0x3EB3] = 1;
		mem[dst + 0x3EB4] = 0xFF;
		Pc98Wr16(mem, dst + 0x3EB5, (uint16_t)s_valkySongSeg);
	}
}

static void ValkyPlantKickStub(uint8_t* mem, unsigned drvSeg)
{
	if (!mem || !drvSeg || drvSeg >= 0xA000u)
		return;
	unsigned p = 0x640u;
	mem[p++] = 0xB8; mem[p++] = 0x01; mem[p++] = 0x00;
	mem[p++] = 0x50;
	mem[p++] = 0x33; mem[p++] = 0xC0;
	mem[p++] = 0x50;
	mem[p++] = 0xB8; mem[p++] = 0x08; mem[p++] = 0x00;
	mem[p++] = 0x89; mem[p++] = 0xE5;
	mem[p++] = 0xBB;
	mem[p++] = (uint8_t)(drvSeg & 0xff);
	mem[p++] = (uint8_t)(drvSeg >> 8);
	mem[p++] = 0x8E; mem[p++] = 0xC3;
	mem[p++] = 0x26; mem[p++] = 0xFF; mem[p++] = 0x1E;
	mem[p++] = 0x0C; mem[p++] = 0x00;
	mem[p++] = 0x83; mem[p++] = 0xC4; mem[p++] = 0x04;
	mem[p++] = 0xCF;
	mem[0x51 * 4 + 0] = 0x40;
	mem[0x51 * 4 + 1] = 0x06;
	mem[0x51 * 4 + 2] = 0;
	mem[0x51 * 4 + 3] = 0;
}

int CEmuPc98ValkyInt50(uint8_t* mem)
{
	if (!s_valkyKeepIrq0)
		return 0;
	const uint8_t ah = (uint8_t)(np2_reg_get(NP2_R_AX) >> 8);
	switch (ah) {
	case 0x02:
		np2_reg_set(NP2_R_AX, 0);
		break;
	case 0x03:
		np2_reg_set(NP2_R_AX, (uint16_t)(s_valkySongSeg ? s_valkySongSeg : 0x4000u));
		break;
	case 0x04: {
		const uint16_t ds = np2_reg_get(NP2_R_DS);
		const uint16_t bx = np2_reg_get(NP2_R_BX);
		const uint16_t cx = np2_reg_get(NP2_R_CX);
		const uint8_t al = (uint8_t)np2_reg_get(NP2_R_AX);
		s_valkyStreamSeg = ds;
		s_valkyStreamOff = cx;
		if (!s_valkyStreamLen)
			s_valkyStreamLen = 0x2000u;
		if (ValkyIsSscp(mem) && al)
			ValkySscpBindTrack(mem, al, bx ? bx : ds, cx);
		np2_reg_set(NP2_R_AX, 0);
		break;
	}
	case 0x07:
		np2_reg_set(NP2_R_AX, 0);
		break;
	case 0x08:
		ValkyPokeCscpPlay(mem);
		np2_reg_set(NP2_R_AX, 0);
		break;
	case 0x09:
		if (mem && s_valkySongName[0]) {
			const unsigned cs = ValkyDriverCs(mem);
			if (cs && cs < 0xA000u) {
				const unsigned dst = (cs << 4) + 0x39FDu;
				unsigned i = 0;
				for (; i < 12 && s_valkySongName[i] && dst + i < 0x200000u; i++)
					mem[dst + i] = (uint8_t)s_valkySongName[i];
				if (dst + i < 0x200000u)
					mem[dst + i] = 0;
			}
		}
		np2_reg_set(NP2_R_AX, 0);
		break;
	case 0x0E: {
		const uint8_t al = (uint8_t)np2_reg_get(NP2_R_AX);
		ValkyPokeSscpWork(mem);
		{
			const unsigned cs = ValkySscpCs(mem);
			unsigned instSeg = 0;
			if (cs && cs < 0xA000u) {
				const unsigned dst = cs << 4;
				const unsigned o3895 = ValkySscpAt(mem, dst, 0x3895u);
				if (dst + o3895 + 1u < 0x200000u)
					instSeg = (unsigned)mem[dst + o3895]
						| ((unsigned)mem[dst + o3895 + 1u] << 8);
			}
			if (!instSeg || instSeg >= 0xA000u) {
				const unsigned dlt = (cs && cs < 0xA000u)
					? ValkySscpBssDelta(mem, cs << 4) : 0;
				instSeg = cs ? (cs + ((((0x49C0u + dlt) + 0x0Fu) & ~0x0Fu) >> 4)) : 0x5000u;
			}
			np2_reg_set(NP2_R_BX, (uint16_t)instSeg);
			np2_reg_set(NP2_R_AX, (uint16_t)((unsigned)al * 0x28u));
		}
		break;
	}
	case 0x0F: {
		const uint8_t al = (uint8_t)np2_reg_get(NP2_R_AX);
		const unsigned cs = ValkySscpCs(mem);
		np2_reg_set(NP2_R_BX, (uint16_t)(cs && cs < 0xA000u ? cs : 0x2800u));
		np2_reg_set(NP2_R_AX, (uint16_t)(0x80Au + (unsigned)al * 0x14u));
		break;
	}
	case 0x11: {
		unsigned n = np2_reg_get(NP2_R_DX);
		if (n < 16u)
			n = 0x400u;
		if (n > 0x4000u)
			n = 0x4000u;
		if (ValkyIsSscp(mem)) {
			ValkyPokeSscpWork(mem);
			if (s_valkyAllocOff + n > 0x4E00u)
				s_valkyAllocOff = 0x80u;
			const unsigned off = s_valkyAllocOff;
			s_valkyAllocOff += n;
			np2_reg_set(NP2_R_BX, (uint16_t)s_valkyAllocSeg);
			np2_reg_set(NP2_R_AX, (uint16_t)off);
			break;
		}
		if (s_valkyAllocOff + n > 0xFFF0u) {
			s_valkyAllocSeg += (s_valkyAllocOff + 15u) >> 4;
			if (s_valkyAllocSeg >= 0x9000u)
				s_valkyAllocSeg = 0x5000u;
			s_valkyAllocOff = 0;
		}
		const unsigned off = s_valkyAllocOff;
		s_valkyAllocOff += n;
		if (!s_valkyStreamSeg) {
			s_valkyStreamSeg = s_valkyAllocSeg;
			s_valkyStreamOff = off;
			s_valkyStreamLen = n;
		}
		np2_reg_set(NP2_R_BX, (uint16_t)s_valkyAllocSeg);
		np2_reg_set(NP2_R_AX, (uint16_t)off);
		break;
	}
	case 0x18:
	case 0x1A:
	case 0x1C:
		np2_reg_set(NP2_R_AX, 0);
		break;
	default:
		np2_reg_set(NP2_R_AX, 0);
		break;
	}
	return 1;
}

int CEmuPc98ValkyIntB0(uint8_t* mem)
{
	(void)mem;
	if (!s_valkyKeepIrq0)
		return 0;
	const uint8_t ah = (uint8_t)(np2_reg_get(NP2_R_AX) >> 8);
	if (ah == 0x1A) {
		np2_reg_set(NP2_R_AX, (uint16_t)(s_valkySongSeg ? s_valkySongSeg : 0x4000u));
		return 1;
	}
	if (ah == 0x18 || ah == 0x1B || ah == 0x1C || ah == 0x1D) {
		np2_reg_set(NP2_R_AX, 0x000B);
		return 1;
	}
	np2_reg_set(NP2_R_AX, 0);
	return 1;
}

static void ValkyArmSscpPlay(uint8_t* mem, uint16_t songHandle,
	CEmuDos98* dos, const char* song)
{
	if (!mem)
		return;
	s_valkyAllocSeg = 0x5000u;
	s_valkyAllocOff = 0;
	s_valkyStreamSeg = 0;
	s_valkyStreamOff = 0;
	s_valkyStreamLen = 0;
	s_valkySongSeg = 0;
	s_valkySongName[0] = 0;
	if (song && song[0])
		strncpy_s(s_valkySongName, song, _TRUNCATE);
	(void)songHandle;
	unsigned glueCs = ValkyIvtSeg(mem, 0x7F, 0x0210);
	if (!glueCs)
		glueCs = ValkyIvtSeg(mem, 0x21, 0x02FA);
	if (!glueCs)
		glueCs = ValkyIvtSeg(mem, 0xB0, 0x030A);
	if (!glueCs) {
		for (unsigned s = 0x00C0u; s < 0xA000u; s++) {
			const unsigned p = (s << 4) + 0x100u;
			const unsigned h = (s << 4) + 0x210u;
			if (h + 6u >= 0x200000u)
				break;
			if (mem[p] == 0xFA && mem[p + 1] == 0xBA
				&& mem[p + 2] == 0xE8 && mem[p + 3] == 0x07
				&& mem[h] == 0x06 && mem[h + 1] == 0x1E
				&& mem[h + 2] == 0x60 && mem[h + 3] == 0xBA) {
				glueCs = s;
				break;
			}
		}
	}
	{
		const unsigned s7 = (unsigned)mem[0x7F * 4 + 2]
			| ((unsigned)mem[0x7F * 4 + 3] << 8);
		/* ライブ VALKY（走査ヒット 1001:0100）は既に AH=48 と AH=3F で SSCP を [041E] へ入れた。9100 を重ねると COM BSS（[041E]=0）をコピーし INT 7F が CALL FAR ES=0。イメージが無いときだけ 9100 を植える。 */
		if (!glueCs && (s7 == 0 || s7 == (unsigned)DOS98_TRAMP_SEG)) {
			const CEmuDos98File* vf = dos ? dos->FindFile("VALKY_98.COM") : NULL;
			if (!vf)
				vf = dos ? dos->FindFile("VALKY_98") : NULL;
			if (!vf)
				vf = dos ? dos->FindFile("VALKY.COM") : NULL;
			if (vf && vf->data && vf->size >= 0x220u) {
				const unsigned cs = 0x9100u;
				const unsigned dst = (cs << 4) + 0x100u;
				if (dst + vf->size < 0x200000u)
					memcpy(mem + dst, vf->data, vf->size);
				glueCs = cs;
				for (unsigned s = 0x0100u; s < 0x9000u; s++) {
					if (s == cs)
						continue;
					const unsigned p = (s << 4) + 0x100u;
					const unsigned h = (s << 4) + 0x210u;
					if (h + 6u >= 0x200000u)
						break;
					if (mem[p] == 0xFA && mem[p + 1] == 0xBA
						&& mem[p + 2] == 0xE8 && mem[p + 3] == 0x07
						&& mem[h] == 0x06 && mem[h + 1] == 0x1E
						&& mem[h + 2] == 0x60 && mem[h + 3] == 0xBA) {
						const unsigned src = (s << 4) + 0x41Cu;
						const unsigned gb = (cs << 4) + 0x41Cu;
						if (src + 10u < 0x200000u && gb + 10u < 0x200000u)
							memcpy(mem + gb, mem + src, 10u);
						break;
					}
				}
			}
		}
	}
	/* CSCP/SSCP はシーケンサ（INT08）。VALKY は ES=[041E] で CALL FAR ES:[000C]。valkyrie はそのインストールを終えず（INT 7F はトランポリンのまま）なので blob と far ptr をホストマップ。ライブ SSCP（hinadori INT08=2002:0DB5）は重ねない。ライブ CSCP（valkyrie AH=48 → 2002）があるのに 2800 へ複製すると API と ISR が別イメージになる。 */
	{
		unsigned liveDrv = 0;
		if (glueCs) {
			const unsigned gb = glueCs << 4;
			if (gb + 0x420u < 0x200000u) {
				const unsigned es = (unsigned)mem[gb + 0x41E]
					| ((unsigned)mem[gb + 0x41F] << 8);
				if (ValkySegLooksDriver(mem, es))
					liveDrv = es;
			}
		}
		const unsigned s08Now = (unsigned)mem[0x08 * 4 + 2]
			| ((unsigned)mem[0x08 * 4 + 3] << 8);
		const unsigned s08Off = (unsigned)mem[0x08 * 4]
			| ((unsigned)mem[0x08 * 4 + 1] << 8);
		if (!liveDrv && ValkySegLooksDriver(mem, s08Now))
			liveDrv = s08Now;
		if (liveDrv) {
			const unsigned isr = ValkyFindIsrOff(mem, liveDrv, 0x8000u);
			if (isr)
				ValkyPlantIsr(mem, liveDrv, isr);
		} else if (dos && (!s08Now || s08Now == (unsigned)DOS98_TRAMP_SEG
				|| s08Off < 0x400u
				|| !ValkyLooksIsr(mem, (s08Now << 4) + s08Off))) {
			const CEmuDos98File* drv = dos->FindFile("CSCP.BIN");
			if (!drv)
				drv = dos->FindFile("SSCP.BIN");
			if (drv && drv->data && drv->size >= 0x80u) {
				const unsigned dcs = 0x2800u;
				const unsigned dst = dcs << 4;
				unsigned n = drv->size;
				if (dst + n >= 0x200000u)
					n = 0x200000u - dst;
				memcpy(mem + dst, drv->data, n);
				const unsigned api = (unsigned)mem[dst + 0x0C]
					| ((unsigned)mem[dst + 0x0D] << 8);
				unsigned entry = (api >= 0x20u && api < 0x200u) ? api : 0x78u;
				mem[dst + 0x0C] = (uint8_t)(entry & 0xff);
				mem[dst + 0x0D] = (uint8_t)(entry >> 8);
				mem[dst + 0x0E] = (uint8_t)(dcs & 0xff);
				mem[dst + 0x0F] = (uint8_t)(dcs >> 8);
				const unsigned isr = ValkyFindIsrOff(mem, dcs, n);
				if (isr)
					ValkyPlantIsr(mem, dcs, isr);
				if (glueCs) {
					const unsigned gb = glueCs << 4;
					if (gb + 0x420u < 0x200000u) {
						mem[gb + 0x41E] = (uint8_t)(dcs & 0xff);
						mem[gb + 0x41F] = (uint8_t)(dcs >> 8);
					}
				}
			}
		}
	}
	/* VALKY `MOV ES,CS:[041E] / CALL FAR ES:[000C]`。ファイル [000C] は near API（RETF @0078/0060）。[000E]==0 だとその CALL が 0000:0078。ライブイメージがまだ API オペコードを持つときだけ far ptr を植える。 */
	{
		const unsigned s08 = (unsigned)mem[0x08 * 4 + 2]
			| ((unsigned)mem[0x08 * 4 + 3] << 8);
		if (glueCs) {
			unsigned api = 0x78u;
			if (dos) {
				const CEmuDos98File* drv = dos->FindFile("CSCP.BIN");
				if (!drv)
					drv = dos->FindFile("SSCP.BIN");
				if (drv && drv->data && drv->size >= 0x10u)
					api = (unsigned)drv->data[0x0C]
						| ((unsigned)drv->data[0x0D] << 8);
			}
			if (s08 >= 0x1000u && s08 < 0xA000u) {
				const unsigned dst = s08 << 4;
				if (api >= 0x20u && api < 0x200u
					&& dst + api + 4u < 0x200000u
					&& mem[dst + api] == 0xFC
					&& mem[dst + api + 1] == 0x32
					&& mem[dst + api + 2] == 0xE4) {
					mem[dst + 0x0C] = (uint8_t)(api & 0xff);
					mem[dst + 0x0D] = (uint8_t)(api >> 8);
					mem[dst + 0x0E] = (uint8_t)(s08 & 0xff);
					mem[dst + 0x0F] = (uint8_t)(s08 >> 8);
				}
			}
			/* [041E] は VALKY の AH=48 SSCP ブロックであり INT08 CS ではない。9100 重ねは 0 のまま — ライブ CSCP/SSCP CS を指し CALL FAR ES:[000C] が RETF API に当たるように。 */
			const unsigned gb = glueCs << 4;
			if (gb + 0x422u < 0x200000u) {
			unsigned es = (unsigned)mem[gb + 0x41E]
				| ((unsigned)mem[gb + 0x41F] << 8);
			if (!es || es == (unsigned)DOS98_TRAMP_SEG) {
				if (s08 >= 0x1000u && s08 < 0xA000u
					&& api >= 0x20u && api < 0x200u) {
					const unsigned ad = s08 << 4;
					if (ad + api + 4u < 0x200000u
						&& mem[ad + api] == 0xFC
						&& mem[ad + api + 1] == 0x32
						&& mem[ad + api + 2] == 0xE4) {
						mem[gb + 0x41E] = (uint8_t)(s08 & 0xff);
						mem[gb + 0x41F] = (uint8_t)(s08 >> 8);
						es = s08;
					}
				}
			}
			/* AH=3F BX=5/6 を AH=48 ブロックへ入れると失敗し得る。SSCP/CSCP をホストマップし CALL FAR ES:[000C] を RETF API に。 */
			if (dos && es >= 0x1000u && es < 0xA000u) {
				const unsigned dst = es << 4;
				int have = (dst + 0x80u < 0x200000u
					&& mem[dst] == 0xF1 && mem[dst + 1] == 0x11);
				if (!have) {
					const CEmuDos98File* drv = dos->FindFile("CSCP.BIN");
					if (!drv)
						drv = dos->FindFile("SSCP.BIN");
					if (drv && drv->data && drv->size >= 0x80u) {
						unsigned n = drv->size;
						if (dst + n >= 0x200000u)
							n = 0x200000u - dst;
						memcpy(mem + dst, drv->data, n);
						have = 1;
					}
				}
				if (have && api >= 0x20u && api < 0x200u
					&& dst + api + 4u < 0x200000u
					&& mem[dst + api] == 0xFC
					&& mem[dst + api + 1] == 0x32
					&& mem[dst + api + 2] == 0xE4) {
					mem[dst + 0x0C] = (uint8_t)(api & 0xff);
					mem[dst + 0x0D] = (uint8_t)(api >> 8);
					mem[dst + 0x0E] = (uint8_t)(es & 0xff);
					mem[dst + 0x0F] = (uint8_t)(es >> 8);
				}
			}
		}
		}
	}
	if (glueCs) {
		mem[0x7F * 4 + 0] = 0x10;
		mem[0x7F * 4 + 1] = 0x02;
		mem[0x7F * 4 + 2] = (uint8_t)(glueCs & 0xff);
		mem[0x7F * 4 + 3] = (uint8_t)(glueCs >> 8);
	}
	/* cmd8 INT 50 AH=3 → AH=3F CX=400 用 DS を DS:0000 へ。そのバッファは VALKY [0420]（128K）であり INT08/SSCP ではない（SSCP:0000 書は 0078 を壊す）。 */
	unsigned songSeg = 0;
	const unsigned s7f = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	auto take420 = [&](unsigned cs) {
		if (!cs || cs == (unsigned)DOS98_TRAMP_SEG)
			return;
		const unsigned vb = cs << 4;
		if (vb + 0x422u >= 0x200000u)
			return;
		const unsigned s = (unsigned)mem[vb + 0x420]
			| ((unsigned)mem[vb + 0x421] << 8);
		if (s && s != (unsigned)DOS98_TRAMP_SEG && s < 0xF000u)
			songSeg = s;
	};
	take420(glueCs);
	if (!songSeg)
		take420(s7f);
	if (!songSeg)
		songSeg = 0x4000u;
	if (glueCs) {
		const unsigned gb = glueCs << 4;
		if (gb + 0x422u < 0x200000u) {
			mem[gb + 0x420] = (uint8_t)(songSeg & 0xff);
			mem[gb + 0x421] = (uint8_t)(songSeg >> 8);
		}
	}
	if (dos && song && song[0] && songSeg) {
		const CEmuDos98File* f = dos->FindFile(song);
		if (f && f->data && f->size) {
			const unsigned dst = songSeg << 4;
			unsigned n = f->size;
			if (n > 0x7FF0u)
				n = 0x7FF0u;
			if (dst + 0x10u + n < 0x200000u) {
				mem[dst + 0] = 1; mem[dst + 1] = 0;
				mem[dst + 2] = 0; mem[dst + 3] = 0;
				mem[dst + 4] = 1; mem[dst + 5] = 0;
				mem[dst + 6] = (uint8_t)(n & 0xff);
				mem[dst + 7] = (uint8_t)(n >> 8);
				memcpy(mem + dst + 0x10u, f->data, n);
			}
		}
	}
	s_valkySongSeg = songSeg;
	ValkyPlantTramp(mem, 0x50);
	ValkyPlantTramp(mem, 0xB0);
	/* #UD はトランポリン経由で HLT し ServiceInt が飛ばせるように */
	mem[0x06 * 4 + 0] = 0x0C;
	mem[0x06 * 4 + 1] = 0x00;
	mem[0x06 * 4 + 2] = (uint8_t)(DOS98_TRAMP_SEG & 0xff);
	mem[0x06 * 4 + 3] = (uint8_t)((DOS98_TRAMP_SEG >> 8) & 0xff);
	unsigned cands[4];
	unsigned nc = 0;
	auto add = [&](unsigned s) {
		if (!s || s == (unsigned)DOS98_TRAMP_SEG || s >= 0xF000u)
			return;
		for (unsigned i = 0; i < nc; i++)
			if (cands[i] == s)
				return;
		if (nc < 4)
			cands[nc++] = s;
	};
	const unsigned s08 = (unsigned)mem[0x08 * 4 + 2]
		| ((unsigned)mem[0x08 * 4 + 3] << 8);
	add(s08);
	if (s7f && s7f != (unsigned)DOS98_TRAMP_SEG) {
		const unsigned vb = s7f << 4;
		if (vb + 0x420u < 0x200000u)
			add((unsigned)mem[vb + 0x41E]
				| ((unsigned)mem[vb + 0x41F] << 8));
	}
	if (glueCs)
		add(glueCs);
	for (unsigned i = 0; i < nc; i++) {
		const unsigned base = cands[i] << 4;
		if (base + 0x4000u >= 0x200000u)
			continue;
		int has384b = 0;
		for (unsigned o = 0; o + 7 < 0x8000u && base + o + 7 < 0x200000u; o++) {
			if (mem[base + o] == 0x2E && mem[base + o + 1] == 0xF6
				&& mem[base + o + 2] == 0x06 && mem[base + o + 3] == 0x4B
				&& mem[base + o + 4] == 0x38)
				has384b = 1;
			/* cmd8 `TEST CS:[imm],1 / JNZ` — hinadori 384B、CSCP 3EB1 の判定 */
			if (mem[base + o] == 0x2E && mem[base + o + 1] == 0xF6
				&& mem[base + o + 2] == 0x06 && mem[base + o + 5] == 0x01
				&& mem[base + o + 6] == 0x75) {
				const unsigned addr = (unsigned)mem[base + o + 3]
					| ((unsigned)mem[base + o + 4] << 8);
				if (addr >= 0x2000u && addr < 0x5000u
					&& base + addr < 0x200000u)
					mem[base + addr] = 1;
			}
			/* cmd8 `TEST CS:[imm],FF / JZ` — hinadori 384D、CSCP 3EB4 の判定 */
			if (mem[base + o] == 0x2E && mem[base + o + 1] == 0xF6
				&& mem[base + o + 2] == 0x06 && mem[base + o + 5] == 0xFF
				&& (mem[base + o + 6] == 0x74 || mem[base + o + 6] == 0x75)) {
				const unsigned addr = (unsigned)mem[base + o + 3]
					| ((unsigned)mem[base + o + 4] << 8);
				if (addr >= 0x2000u && addr < 0x5000u
					&& base + addr < 0x200000u)
					mem[base + addr] = 1;
			}
		}
		if (has384b) {
			mem[base + 0x384B] = 1;
			mem[base + 0x384D] = 1;
			/* ISR 0DD2 は [38A9] がセットされるまで IRQ0 を再マスク */
			if (base + 0x38A9u < 0x200000u)
				mem[base + 0x38A9] = 1;
			/* CS:[390A] は INT08 ビジーラッチ: TEST/JNZ はシーケンスせず IRET。ファイル既定は 0。セットしない。 */
		}
		if (s_valkySongSeg && base + 0x3EB6u < 0x200000u) {
			mem[base + 0x3EB1] = (uint8_t)(mem[base + 0x3EB1] | 1u);
			mem[base + 0x3EB3] = 1;
			mem[base + 0x3EB4] = 0xFF;
			Pc98Wr16(mem, base + 0x3EB5, (uint16_t)s_valkySongSeg);
		}
		if (s_valkySongSeg && base + 0x385Au < 0x200000u)
			Pc98Wr16(mem, base + 0x3859, (uint16_t)s_valkySongSeg);
	}
	ValkyReplantIsr(mem);
	{
		const unsigned drv = ValkyDriverCs(mem);
		ValkyPlantKickStub(mem, drv);
		ValkyPokeCscpPlay(mem);
	}
}

/* ValkyReplantIsr の実装 */
static void ValkyReplantIsr(uint8_t* mem)
{
	if (!mem || !s_valkyKeepIrq0)
		return;
	const unsigned cs = (unsigned)mem[0x08 * 4 + 2]
		| ((unsigned)mem[0x08 * 4 + 3] << 8);
	const unsigned off = (unsigned)mem[0x08 * 4]
		| ((unsigned)mem[0x08 * 4 + 1] << 8);
	if (cs >= 0x1000u && cs < 0xA000u && off >= 0x400u
		&& ValkyLooksIsr(mem, (cs << 4) + off)
		&& (off == 0x0DB5u || off == 0x2DABu || off == 0x0DD2u
			|| off == 0x0EA0u || off == 0x0ECEu
			|| off == 0x3151u || off == 0x3090u)) {
		ValkyPlantIsr(mem, cs, off);
		return;
	}
	unsigned cands[6];
	unsigned nc = 0;
	auto add = [&](unsigned s) {
		if (!s || s < 0x1000u || s >= 0xA000u)
			return;
		for (unsigned i = 0; i < nc; i++)
			if (cands[i] == s)
				return;
		if (nc < 6)
			cands[nc++] = s;
	};
	const unsigned glue = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	if (glue && glue != (unsigned)DOS98_TRAMP_SEG) {
		const unsigned gb = glue << 4;
		if (gb + 0x420u < 0x200000u)
			add((unsigned)mem[gb + 0x41E]
				| ((unsigned)mem[gb + 0x41F] << 8));
	}
	add(cs);
	add(0x2002u);
	add(0x2800u);
	for (unsigned i = 0; i < nc; i++) {
		const unsigned isr = ValkyFindIsrOff(mem, cands[i], 0x8000u);
		if (isr) {
			ValkyPlantIsr(mem, cands[i], isr);
			return;
		}
	}
	for (unsigned s = 0x1000u; s < 0xA000u; s++) {
		static const unsigned kAll[] = { 0x0DD2u, 0x0DB5u, 0x2DABu, 0x0EA0u, 0x0ECEu, 0x3151u, 0x3090u };
		const unsigned dst = s << 4;
		for (unsigned i = 0; i < 7; i++) {
			if (ValkyLooksIsr(mem, dst + kAll[i])) {
				ValkyPlantIsr(mem, s, kAll[i]);
				return;
			}
		}
	}
}

/* ValkyFixFarApiFromGlue の実装 */
static void ValkyFixFarApiFromGlue(uint8_t* mem)
{
	if (!mem || !s_valkyKeepIrq0)
		return;
	unsigned glue = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	if (!glue || glue == (unsigned)DOS98_TRAMP_SEG)
		return;
	const unsigned gb = glue << 4;
	if (gb + 0x422u >= 0x200000u)
		return;
	unsigned cands[3];
	unsigned nc = 0;
	auto add = [&](unsigned s) {
		if (!s || s < 0x1000u || s >= 0xA000u)
			return;
		for (unsigned i = 0; i < nc; i++)
			if (cands[i] == s)
				return;
		if (nc < 3)
			cands[nc++] = s;
	};
	add((unsigned)mem[gb + 0x41E] | ((unsigned)mem[gb + 0x41F] << 8));
	add((unsigned)mem[0x08 * 4 + 2] | ((unsigned)mem[0x08 * 4 + 3] << 8));
	add(0x2002u);
	for (unsigned i = 0; i < nc; i++) {
		const unsigned dst = cands[i] << 4;
		static const unsigned kApi[] = { 0x78u, 0x60u };
		for (unsigned a = 0; a < 2; a++) {
			const unsigned api = kApi[a];
			if (dst + api + 4u >= 0x200000u)
				continue;
			if (mem[dst + api] != 0xFC || mem[dst + api + 1] != 0x32
				|| mem[dst + api + 2] != 0xE4)
				continue;
			mem[dst + 0x0C] = (uint8_t)(api & 0xff);
			mem[dst + 0x0D] = (uint8_t)(api >> 8);
			mem[dst + 0x0E] = (uint8_t)(cands[i] & 0xff);
			mem[dst + 0x0F] = (uint8_t)(cands[i] >> 8);
			mem[gb + 0x41E] = (uint8_t)(cands[i] & 0xff);
			mem[gb + 0x41F] = (uint8_t)(cands[i] >> 8);
			return;
		}
	}
}

/* バス読込 */
static void ValkyRewindCmd8Read(CEmuDos98& dos, const char* song, uint8_t vec)
{
	if (!s_valkyKeepIrq0 || vec != 0x21 || !song || !song[0])
		return;
	if ((uint8_t)(np2_reg_get(NP2_R_AX) >> 8) != 0x3F)
		return;
	if (np2_reg_get(NP2_R_CX) < 0x100)
		return;
	const uint16_t bx = np2_reg_get(NP2_R_BX);
	dos.SetHandle(bx, song);
	/* SSCP API8 先頭 AH=3F CX=400 はヘッダ用。hoot は 1 曲 GMD なので全文を渡して CALL 535 が最後まで歩けるようにする。 */
	if (np2_reg_get(NP2_R_CX) <= 0x400u) {
		const CEmuDos98File* gf = dos.FindFile(song);
		if (gf && gf->data && gf->size >= 8u
			&& gf->data[0] == 'G' && gf->data[1] == 'M'
			&& gf->data[2] == 'D' && gf->data[3] == '0') {
			unsigned n = gf->size;
			if (n > 0x7FF0u)
				n = 0x7FF0u;
			np2_reg_set(NP2_R_CX, (uint16_t)n);
			np2_reg_set(NP2_R_DX, 0);
		}
	}
	/* cmd8 AH=3F CX=400 を DS:0000 へ。DS がまだ SSCP（F1 11 / API FC 32 E4）なら 1K ヘッダが CS:01A1 に着き INT 06 がライブロック。 */
	uint8_t* mem = np2_mem();
	if (!mem)
		return;
	const uint16_t ds = np2_reg_get(NP2_R_DS);
	const unsigned db = (unsigned)ds << 4;
	if (db + 0x10u >= 0x200000u)
		return;
	int smash = 0;
	if (mem[db] == 0xF1 && mem[db + 1] == 0x11)
		smash = 1;
	const unsigned api = (unsigned)mem[db + 0x0C]
		| ((unsigned)mem[db + 0x0D] << 8);
	if (api && db + api + 2u < 0x200000u
		&& mem[db + api] == 0xFC && mem[db + api + 1] == 0x32
		&& mem[db + api + 2] == 0xE4)
		smash = 1;
	if (!smash)
		return;
	unsigned songSeg = 0;
	const unsigned glue = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	if (glue && glue != (unsigned)DOS98_TRAMP_SEG) {
		const unsigned gb = glue << 4;
		if (gb + 0x422u < 0x200000u)
			songSeg = (unsigned)mem[gb + 0x420]
				| ((unsigned)mem[gb + 0x421] << 8);
	}
	if (!songSeg || songSeg == (unsigned)DOS98_TRAMP_SEG || songSeg >= 0xF000u)
		songSeg = 0x4000u;
	np2_reg_set(NP2_R_DS, (uint16_t)songSeg);
}

/* FairyDust MFD.EXE（koukan2/madol MIDI）: Borland TSR 本体 */

static void PatchMfd98Int42Keep(uint8_t* mem)
{
	s_mfd98GlueCs = 0;
	if (!mem) return;
	const uint16_t cs = np2_reg_get(NP2_R_CS);
	if (!cs || cs == (uint16_t)DOS98_TRAMP_SEG)
		return;
	const unsigned b = Pc98DosLin(cs, 0x100);
	if (b + 0x20u >= 0x200000u)
		return;
	if (mem[b + 0x0A] != 0xB8 || mem[b + 0x0B] != 0x7F || mem[b + 0x0C] != 0x25)
		return;
	if (mem[b + 0x18] != 0xCD || mem[b + 0x19] != 0x42)
		return;
	s_mfd98GlueCs = cs;
}

/* Packen MUAPLAY 1.21R6F（kidsap/qroad/wa_1/presence）: 常駐確認は INT14 が
   CS:03CE か見る。未常駐なら本体が XCHG で INT14/INT60 を植える。空コマンド
   行だけは字句 STC で確認パスへ落ちるので、呼び出し側で -Z を残す。 */
static void PatchMuaplayIvtSentinel(uint8_t* mem)
{
	/* 常駐確認 CALL を植込へ差し替えると未初期化 ISR になる。
	   コマンド行は RunDosCommand が -Z を残す（空 PSP は字句 STC で INT18 待ち）。 */
	(void)mem;
}

/* 142 バイト NC_98.com（3x3eyes MIDI）: INT 7F cmd0 が conin 名を CS:017E へ読み XOR SI,SI / INT 42 AX=0。NC.COM AX=0 は DS:SI（ファイル名）から 128 バイト REP MOVSB — SI=0 は名ではなく COM ヘッダをコピーし、PIT ISR は CC 全ノートオフだけ出す（midi=0/2880）。 */
static void PatchNc98FilenameSi(uint8_t* mem)
{
	if (!mem) return;
	const uint16_t cs = np2_reg_get(NP2_R_CS);
	if (!cs || cs == (uint16_t)DOS98_TRAMP_SEG)
		return;
	const unsigned b = Pc98DosLin(cs, 0x100);
	if (b + 0x90u >= 0x200000u)
		return;
	if (mem[b] != 0xFA || mem[b + 1] != 0x8C || mem[b + 2] != 0xC8)
		return;
	if (mem[b + 0x0A] != 0xB0 || mem[b + 0x0B] != 0x80)
		return;
	for (unsigned o = 0x20; o + 6u < 0x90u; o++) {
		if (mem[b + o] == 0x31 && mem[b + o + 1] == 0xD6
			&& mem[b + o + 2] == 0xB8 && mem[b + o + 3] == 0x00
			&& mem[b + o + 4] == 0x00 && mem[b + o + 5] == 0xCD
			&& mem[b + o + 6] == 0x42) {
			mem[b + o] = 0xBE;
			mem[b + o + 1] = 0x7E;
			mem[b + o + 2] = 0x01;
			mem[b + o + 3] = 0x33;
			mem[b + o + 4] = 0xC0;
			break;
		}
	}
}

/* CPU を進める */
int CHardPc98::RunDosDevices(const CEmuGameEntry* ge, uint64_t budgetCycles)
{
	if (!ge) return 0;
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	int nOk = 0;
	int nDeviceRom = 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "device") == 0)
			nDeviceRom++;
	}
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		const int isDevice = _stricmp(r->type, "device") == 0;
		int isLooseMmd = 0;
		if (!isDevice && nDeviceRom == 0 && _stricmp(r->type, "file") == 0
			&& r->offset < 0 && r->name) {
			char bn[DOS98_NAME];
			DosCfgFileStem(r->name, bn, (int)sizeof(bn));
			const char* dot = strrchr(bn, '.');
			if (dot && _stricmp(dot, ".SYS") == 0
				&& _strnicmp(bn, "MMD", 3) == 0)
				isLooseMmd = 1;
		}
		if (!isDevice && !isLooseMmd) continue;
		/* extraParas / INIT パケット用に CONFIG 文字列全体を残す。stem のみ名は LoadDeviceImage 用 — `mmd.sys /f12 4096` の最終スラッシュは "f12" を探していた（sbr_98 SILENT）。 */
		const char* base = r->name ? r->name : "";
		char name[DOS98_NAME];
		DosCfgFileStem(base, name, (int)sizeof(name));
		if (!name[0]) continue;
		/* NMUSE CONFIG -d/-k サイズはイメージ先のバイトバッファ。小さい -d2048 -k1024 は既定割当に既に収まる。適用すると COM AH=48 ブロックが動き pod OPEN が GAPPY。 */
		unsigned extraParas = 0;
		const int isMmd = (_strnicmp(name, "mmd", 3) == 0);
		if (isMmd)
			g_mmdPicIsr = 1;
		if (isMmd && _strnicmp(name, "mmd2", 4) != 0)
			g_mmdClassic = 1;
		for (const char* ap = base; *ap; ap++) {
			if ((ap[0] == '-' || ap[0] == '/')
				&& (ap[1] == 'd' || ap[1] == 'D' || ap[1] == 'k' || ap[1] == 'K')
				&& ap[2] >= '0' && ap[2] <= '9') {
				unsigned v = 0;
				for (const char* q = ap + 2; *q >= '0' && *q <= '9'; q++)
					v = v * 10u + (unsigned)(*q - '0');
				extraParas += (v + 15u) / 16u;
			}
		}
		/* MMD2.SYS 4096 — 裸の 10 進は作業バッファサイズであり -d/-k ではない。追加パラ無しだと次 COM（mmd2.com）が CS:0xFBA（ボイス+曲コピー先）に着き、INT D2 AH=10 が上書き RAM へコピー。 */
		if (isMmd && extraParas == 0) {
			for (const char* ap = base; *ap; ) {
				if (*ap >= '0' && *ap <= '9') {
					unsigned v = 0;
					while (*ap >= '0' && *ap <= '9')
						v = v * 10u + (unsigned)(*ap++ - '0');
					if (v >= 64u)
						extraParas += (v + 15u) / 16u;
					continue;
				}
				ap++;
			}
			if (extraParas)
				/* 4096 バッファ + 0x400 作業 + [c7c]+0x200 の 0x200 IRQ SP。0x40 パラはイメージ+0x1400（0x23BA）で止まり ISR SP は 0x25BA なのでスタックが次 COM に着いた。 */
				extraParas += 0xA0u;
		}
		if (extraParas <= 0x180u && !isMmd && _strnicmp(name, "sdd", 3) != 0)
			extraParas = 0;
		/* SDD INIT: [0427]=1A19+[0421]。-d 無しだと 1A19（イメージ内コード）へ
		   723+ バイトの曲を memcpy し break も 1A19 のまま。 */
		if (!_strnicmp(name, "sdd", 3) && extraParas < 0x280u)
			extraParas = 0x280u;

		/* MUSE/SDD デバイスは SSG I/O A bits7-6 から IRQ を選ぶ。INT14 経路を強制 */
		if (chip_ && (strstr(name, "MUSE") || strstr(name, "muse")
			|| strstr(name, "SDD") || strstr(name, "sdd")
			|| strstr(name, "MMD") || strstr(name, "mmd")
			|| strstr(name, "MDR") || strstr(name, "mdr"))) {
			ssgPortAJumper_ = 0xC0;
			chip_->Write(0, 0x0E);
			chip_->Write(1, 0xC0);
			opnLatchedAddr_ = 0x0E;
		}

		uint16_t loadSeg = 0, stratOff = 0, intrOff = 0;
		if (!dos_.LoadDeviceImage(mem, name, &loadSeg, &stratOff, &intrOff, extraParas) || !loadSeg)
			continue;
		if (!_strnicmp(name, "sdd", 3) && mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (lin + 0x428u < 0x200000u) {
				/* -d8192 -k2048。INIT が dest=1A19+d / break=dest+k を書く */
				Pc98Wr16(mem, lin + 0x421u, 0x2000);
				Pc98Wr16(mem, lin + 0x423u, 0x0800);
			}
		}
		if (isMmd)
			g_mmdLoadSeg = loadSeg;
		/* MMD200OR な MMD.SYS（xak2）は API 0072、gazzel 5273 は API 00A2。名前だけで classic にしない。 */
		if (isMmd && mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (MmdIsSound(mem, lin)) {
				g_mmdClassic = 0;
				g_mmdPicIsr = 0;
				ssgPortAJumper_ = 0;
				if (chip_) {
					chip_->Write(0, 0x0E);
					chip_->Write(1, 0x00);
					opnLatchedAddr_ = 0x0E;
				}
				MmdSoundRelocLoaded(mem, dos_, loadSeg);
			} else if (lin + 0xA3u < 0x200000u
				&& (mem[lin + 0x78] == 0x06 || mem[lin + 0x72] == 0x06
					|| mem[lin + 0xA2] == 0x06))
				g_mmdClassic = 0;
		}
		/* 古典 MMD.SYS: OPN ポートは AH=0 検出（1a38）が埋める。mmd2.com は AH=0 を送らないので 154A は 0 のまま、05d9/048a 書が 188h ではなくポート 0（PIC）に当たる。 */
		if (g_mmdClassic && mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (MmdClassicIsFray(mem, lin) && lin + 0x1531u < 0x200000u) {
				mem[lin + 0x152E] = 0x88;
				mem[lin + 0x152F] = 0x01;
				mem[lin + 0x1530] = 0x8A;
				mem[lin + 0x1531] = 0x01;
			} else if (lin + 0x154Du < 0x200000u) {
				mem[lin + 0x154A] = 0x88;
				mem[lin + 0x154B] = 0x01;
				mem[lin + 0x154C] = 0x8A;
				mem[lin + 0x154D] = 0x01;
			}
		} else if (isMmd && mem && loadSeg
			&& !MmdIsSound(mem, Pc98DosLin(loadSeg, 0))) {
			Mmd2PlantPorts(mem, Pc98DosLin(loadSeg, 0));
		}
		/* wiz6 $MUSE2$ は CS:E2 に `MOV SP,005Fh` を保つ（derby イメージは無い）。SYS 名パッチが外れてもロード済みコピーで上げる。 */
		if (mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (lin + 0xE4u < 0x200000u && mem[lin + 0xE2] == 0xBC
				&& mem[lin + 0xE3] == 0x5F && mem[lin + 0xE4] == 0x00) {
				mem[lin + 0xE3] = 0x00;
				mem[lin + 0xE4] = 0x3E;
			}
			if (lin + 0x555u < 0x200000u && mem[lin + 0x553] == 0xBC
				&& mem[lin + 0x554] == 0x9F && mem[lin + 0x555] == 0x00) {
				mem[lin + 0x554] = 0x00;
				mem[lin + 0x555] = 0x2E;
			}
		}

		const uint16_t reqSeg = (uint16_t)DOS98_IDLE_SEG;
		const uint16_t reqOff = 0x0100;
		const unsigned req = Pc98DosLin(reqSeg, reqOff);
		memset(mem + req, 0, 0x60);
		mem[req + 0] = 0x22;
		mem[req + 2] = 0x00; /* INIT */
		/* 文字デバイス INIT +12h は CONFIG.SYS 末尾への far ポインタ（空白 + 引数 + CR）。MMD2.SYS は 4096 バイトバッファサイズを歩く。NULL ptr は 0000:0000 から LDS SI し CX=0 のまま mmd2.com の AH=3F 曲読が何も転送しなかった（dumps=1）。パケットは 0050:0100（lin 0x600）にあった — それは INT トランポリン（0060:0000）。memset 0x60 が INT 21 の HLT stub を消し SYS/糊 AH=25 が D2/7F をフックしなかった。 */
		{
			char argbuf[80];
			int ai = 0;
			argbuf[ai++] = ' ';
			const char* tail = base;
			while (*tail && *tail != ' ' && *tail != '\t')
				tail++;
			while (*tail && ai < 76)
				argbuf[ai++] = *tail++;
			argbuf[ai++] = 0x0D;
			argbuf[ai] = 0;
			memcpy(mem + req + 0x20, argbuf, (size_t)ai + 1);
			mem[req + 0x12] = 0x20;
			mem[req + 0x13] = 0x01; /* reqOff+0x20 */
			mem[req + 0x14] = (uint8_t)(reqSeg & 0xff);
			mem[req + 0x15] = (uint8_t)(reqSeg >> 8);
			Pc98Wr16(mem, req + 0x0E, 0);
			Pc98Wr16(mem, req + 0x10, 0x9000);
			/* MMD2.SYS INIT は LDS SI,ES:[2C] のあと LDS SI,[SI+12] で CONFIG 末尾へ（4026 バイト: orangerd/michael）。新しい 4655 バイト（mjclnc/sbp/shikinjo）は ES:[34] を同じ使い方。ES がまだパケットセグメントなら packet+2C/+34 の far ポインタはパケット自身で、[SI+12] が CONFIG ptr。 */
			if (isMmd) {
				Pc98Wr16(mem, req + 0x2C, reqOff);
				Pc98Wr16(mem, req + 0x2E, reqSeg);
				Pc98Wr16(mem, req + 0x34, reqOff);
				Pc98Wr16(mem, req + 0x36, reqSeg);
			}
		}

		const uint16_t launchSeg = (uint16_t)DOS98_IDLE_SEG;
		const uint16_t stratPtr = 0x0040;
		const uint16_t intrPtr = 0x0044;
		const unsigned L0 = Pc98DosLin(launchSeg, 0);
		/* MOV AX,reqSeg; MOV ES,AX; MOV BX,reqOff; CALL FAR [stratPtr]（リクエスト実行） */
		mem[L0 + 0x00] = 0xB8;
		mem[L0 + 0x01] = (uint8_t)(reqSeg & 0xff);
		mem[L0 + 0x02] = (uint8_t)(reqSeg >> 8);
		mem[L0 + 0x03] = 0x8E; mem[L0 + 0x04] = 0xC0;
		mem[L0 + 0x05] = 0xBB;
		mem[L0 + 0x06] = (uint8_t)(reqOff & 0xff);
		mem[L0 + 0x07] = (uint8_t)(reqOff >> 8);
		mem[L0 + 0x08] = 0x2E; /* CS: */
		mem[L0 + 0x09] = 0xFF; mem[L0 + 0x0A] = 0x1E;
		mem[L0 + 0x0B] = (uint8_t)(stratPtr & 0xff);
		mem[L0 + 0x0C] = (uint8_t)(stratPtr >> 8);
		mem[L0 + 0x0D] = 0xB8;
		mem[L0 + 0x0E] = (uint8_t)(reqSeg & 0xff);
		mem[L0 + 0x0F] = (uint8_t)(reqSeg >> 8);
		mem[L0 + 0x10] = 0x8E; mem[L0 + 0x11] = 0xC0;
		mem[L0 + 0x12] = 0xBB;
		mem[L0 + 0x13] = (uint8_t)(reqOff & 0xff);
		mem[L0 + 0x14] = (uint8_t)(reqOff >> 8);
		mem[L0 + 0x15] = 0x2E; /* CS: */
		mem[L0 + 0x16] = 0xFF; mem[L0 + 0x17] = 0x1E;
		mem[L0 + 0x18] = (uint8_t)(intrPtr & 0xff);
		mem[L0 + 0x19] = (uint8_t)(intrPtr >> 8);
		/* OUT 7E8,82; HLT */
		mem[L0 + 0x1A] = 0xBA; mem[L0 + 0x1B] = 0xE8; mem[L0 + 0x1C] = 0x07;
		mem[L0 + 0x1D] = 0xB0; mem[L0 + 0x1E] = 0x82;
		mem[L0 + 0x1F] = 0xEE;
		mem[L0 + 0x20] = 0xF4;
		Pc98Wr16(mem, Pc98DosLin(launchSeg, stratPtr), stratOff);
		Pc98Wr16(mem, Pc98DosLin(launchSeg, stratPtr) + 2, loadSeg);
		Pc98Wr16(mem, Pc98DosLin(launchSeg, intrPtr), intrOff);
		Pc98Wr16(mem, Pc98DosLin(launchSeg, intrPtr) + 2, loadSeg);

		/* MUSE3 は DEVICE OPEN（cmd 0x0D）で INT14 を植える。INIT ではない: DOS 5+ INIT は call 6cd を飛ばす。muse_98.com は IVT[0x52] をドライバ CS とし [CS:8] を far 呼。トランポリン CS は 0060:0012 を書き AH=25 INT7F 前に #BR。MUSIC.SYS はこの族ではない。 */
		int museOpen = 0;
		if (!_strnicmp(name, "muse", 4) || !_strnicmp(name, "nmuse", 5)
			|| !_strnicmp(name, "sdd", 3))
			museOpen = 1;

		const int nPass = museOpen ? 2 : 1;
		for (int pass = 0; pass < nPass; pass++) {
			if (pass) {
				mem[req + 0] = 0x0D;
				mem[req + 2] = 0x0D; /* OPEN */
			}
			np2_reg_set(NP2_R_DS, loadSeg);
			np2_reg_set(NP2_R_ES, reqSeg);
			np2_set_ss_sp(launchSeg, 0xFFFE);
			np2_set_cs_ip(launchSeg, 0x0000);
			np2_reg_set(NP2_R_FLAGS, 0x0202);
			stubState_ = 0;
			const uint64_t start = cpuCycles_;
			const uint64_t passBudget = pass ? (budgetCycles / 8ull + 100000ull)
				: budgetCycles;
			while (cpuCycles_ - start < passBudget) {
				if (stubState_ == 0x82)
					break;
				/* MUSE/NMUSE/SDD/MUSE2 デバイススタックは小さい。INIT/OPEN 中の入れ子 INT14 tick が同じ SP で ISR 再入。MMD2 は INT14 を植えて RETF 前に STI — 入れ子 IRQ を同じように飛ばす。 */
				if (!museOpen && !isMmd && DeliverIrqs())
					continue;
				uint16_t cs = np2_reg_get(NP2_R_CS);
				uint16_t ip = np2_reg_get(NP2_R_IP);
				const unsigned phys = ((unsigned)cs << 4) + (unsigned)ip;
				if (phys < 0x200000 && mem[phys] == 0xF4) {
					uint8_t vec = 0;
					if (dos_.TrapVector(cs, ip, &vec)) {
						ValkyRewindCmd8Read(dos_,
							dosSong_[0] ? dosSong_
								: SelectedDosSong(dosGe_, extSong_), vec);
						CEmuDos98Result res = dos_.ServiceInt(mem, vec);
						if (res == DOS98_TERMINATED || res == DOS98_RESIDENT)
							break;
						if (res == DOS98_EXEC)
							continue;
						dos_.IretReturn(mem);
						cpuCycles_ += 50;
						TickSide(50);
						AdvanceOpnClocks(50);
						continue;
					}
					cpuCycles_ += 200;
					TickSide(200);
					AdvanceOpnClocks(200);
					continue;
				}
				const int32_t cyc = np2_step();
				const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
				cpuCycles_ += u;
				TickSide(u);
				AdvanceOpnClocks(u);
			}
		}
		/* wiz6 MUSE2 OPEN `MOV SP,005Fh` は [CS:8] の割り込みポインタを含むヘッダを壊し得る。muse_98 は 0014 を far 呼。最初のパッチ前に INIT/OPEN が走っても SP を再上げ。 */
		if (mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (lin + 0xE4u < 0x200000u && mem[lin + 0xE2] == 0xBC
				&& mem[lin + 0xE3] == 0x5F && mem[lin + 0xE4] == 0x00) {
				mem[lin + 0xE3] = 0x00;
				mem[lin + 0xE4] = 0x3E;
			}
			if (lin + 0x555u < 0x200000u && mem[lin + 0x553] == 0xBC
				&& mem[lin + 0x554] == 0x9F && mem[lin + 0x555] == 0x00) {
				mem[lin + 0x554] = 0x00;
				mem[lin + 0x555] = 0x2E;
			}
			if (!_strnicmp(name, "muse2", 5) && intrOff) {
				Pc98Wr16(mem, lin + 8u, intrOff);
				g_muse2Seg = loadSeg;
				g_muse2Intr = intrOff;
				/* OPEN が [CS:8] を壊したとき muse_98 は 0014 を far 呼。割り込みルーチン（ヘッダ名+4）への near JMP */
				if (lin + 0x17u < 0x200000u && intrOff > 0x17u) {
					mem[lin + 0x14] = 0xE9;
					Pc98Wr16(mem, lin + 0x15u,
						(uint16_t)(intrOff - 0x17u));
				}
			}
		}
		/* MMD.SYS（sbr）: 解析 0619 は duration [ch+2]=1 を置くがゲート [ch+3] は置かない。ノート 0849 はゲート→duration をコピー。ゲート 0 は 0732 がチャネルを永久 RET（A0 補助から keys=1、その後 SILENT）。0143 の AH=3 も 17F4 から `rep stos` しこの poke を消す — 解析後に MmdPlayAssist が再適用。INT D2 は COM 常駐の AH=0 より前に要る。 */
		if (g_mmdClassic && mem && loadSeg)
			MmdClassicPlantIvt(mem);
		if (mem)
			MmdSoundPlantIvt(mem);
		if (!_strnicmp(name, "muse2", 5) && mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (lin + 0x538u < 0x200000u && mem[lin + 0x535] == 0x9C
				&& mem[lin + 0x536] == 0xFA) {
				Pc98Wr16(mem, 0x14u * 4u, 0x0535);
				Pc98Wr16(mem, 0x14u * 4u + 2u, loadSeg);
				if (!IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u, 0x0535);
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u + 2u, loadSeg);
				}
				if (lin + 0x4B4u < 0x200000u)
					Pc98Wr16(mem, lin + 0x4B3u, 0x0050);
			}
		}
		if (!_strnicmp(name, "nmuse", 5) && mem && loadSeg
			&& !IvtHooked(0x14, 1)) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (lin + 0x6D8u < 0x200000u && mem[lin + 0x6D6] == 0x9C
				&& mem[lin + 0x6D7] == 0xFA) {
				Pc98Wr16(mem, 0x14u * 4u, 0x06D6);
				Pc98Wr16(mem, 0x14u * 4u + 2u, loadSeg);
			}
		}
		/* SDD 26 + DOS 5: INIT は植を飛ばし OPEN の [152E]==0 経路は IVT 14 を書かない（ishido dumps=1）。ISR は CS:09BF。INT14 に既 stub があっても強制植 — IvtHooked は本物 09BF を飛ばし dumps=1 のまま。 */
		if (!_strnicmp(name, "sdd", 3) && mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (lin + 0x9C2u < 0x200000u && mem[lin + 0x9BF] == 0xFA) {
				Pc98Wr16(mem, 0x14u * 4u, 0x09BF);
				Pc98Wr16(mem, 0x14u * 4u + 2u, loadSeg);
				if (!IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u, 0x09BF);
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u + 2u, loadSeg);
				}
				g_sddLoadSeg = loadSeg;
			}
		}
		/* MUDRV3 SYS-in-EXE: strategy は要求を [CS:1B] に格納。INIT はしばしば AH=25 INT 43 前に戻る。API は CS:0248（PUSHA / CLD / CLI / AH 配送）。LW1CD_98 は INT 43 で話す。 */
		if (!_strnicmp(name, "mudrv", 5) && mem && loadSeg) {
			const unsigned api = Pc98DosLin(loadSeg, 0x248);
			if (api + 6u < 0x200000u && mem[api] == 0x60 && mem[api + 1] == 0x1E
				&& mem[api + 2] == 0x06 && mem[api + 3] == 0xFC
				&& mem[api + 4] == 0xFA) {
				Pc98Wr16(mem, 0x43u * 4u, 0x0248);
				Pc98Wr16(mem, 0x43u * 4u + 2u, loadSeg);
			}
		}
		if (IvtHooked(0xC8, 1) || IvtHooked(0xC0, 1) || IvtHooked(0xC3, 1))
			nOk++;
	}
	return nOk;
}

/* CPU を進める */
int CHardPc98::RunDosCommand(const char* cmdline, uint64_t budgetCycles)
{
	char stripped[256];
	char name[96];
	char tail[160];
	DosStripHash(cmdline, stripped, (int)sizeof(stripped));
	DosSplitCmd(stripped, name, (int)sizeof(name), tail, (int)sizeof(tail));
	/* GREAT カタログ `muaplay -Z -f9`。Packen MUAPLAY 1.21 の -Fx はフェード速度
	   （0 が最長、9 が最速）でボード番号ではない。PSP が空（80h=0 / 81h=CR）だと
	   オプション字句が STC で終わり、未常駐なのに INT18 AH=0 の CR 待ちへ落ち、
	   INT60 を植えない。-Z（ポートウェイト×2）は残し、-F だけ落とす。 */
	if (_strnicmp(name, "muaplay", 7) == 0) {
		char kept[160];
		int o = 0;
		const char* p = tail;
		while (*p) {
			while (*p == ' ' || *p == '\t')
				p++;
			if (!*p)
				break;
			if ((p[0] == '-' || p[0] == '/')
				&& (p[1] == 'F' || p[1] == 'f')
				&& p[2] >= '0' && p[2] <= '9') {
				p += 3;
				continue;
			}
			if (o && o + 1 < (int)sizeof(kept))
				kept[o++] = ' ';
			while (*p && *p != ' ' && *p != '\t' && o + 1 < (int)sizeof(kept))
				kept[o++] = *p++;
		}
		kept[o] = 0;
		if (!kept[0])
			memcpy(kept, "-Z", 3);
		memcpy(tail, kept, strlen(kept) + 1);
	}
	const unsigned char* image = NULL;
	unsigned imageSize = 0;
	int isExe = 0;
	if (!dos_.ResolveProgram(name, &image, &imageSize, &isExe) || !image)
		return 0;
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	char pspTail[162];
	if (tail[0])
		_snprintf_s(pspTail, _TRUNCATE, " %s", tail);
	else
		pspTail[0] = 0;
	int ok = isExe ? dos_.LoadExe(mem, image, imageSize, pspTail)
		: dos_.LoadCom(mem, image, imageSize, pspTail);
	if (!ok) return 0;
	if (s_fmxCalibAssist && !isExe && imageSize == 14984u) {
		const uint16_t psp = dos_.PspSeg();
		const unsigned base = (unsigned)psp << 4;
		const unsigned slot = base + 0x3B84u;
		if (slot + 1u < 0x200000u) {
			mem[slot] = 0x00;
			mem[slot + 1] = 0x80;
		}
		/* ワンショット INT08 CALL FAR [3B6C] — BSS は 0000:0000 */
		if (base + 0x3B71u < 0x200000u) {
			mem[base + 0x3B6C] = 0x70;
			mem[base + 0x3B6D] = 0x3B;
			mem[base + 0x3B6E] = (uint8_t)(psp & 0xff);
			mem[base + 0x3B6F] = (uint8_t)(psp >> 8);
			mem[base + 0x3B70] = 0xCB;
		}
	}
	if (isExe && image && imageSize >= 32) {
		int isMfd = 0;
		for (unsigned i = 0; i + 18u < imageSize; i++) {
			if (memcmp(image + i, "MFD:MiDi/FM Driver", 18) == 0) {
				isMfd = 1;
				break;
			}
		}
		if (isMfd)
			PatchMfdExeTsr(mem);
	} else if (!isExe) {
		PatchMfd98Int42Keep(mem);
		PatchNc98FilenameSi(mem);
		PatchMuaplayIvtSentinel(mem);
	}

	dosStubReady_ = 0;
	const uint64_t start = cpuCycles_;
	while (cpuCycles_ - start < budgetCycles) {
		if (stubState_ == 0x81) {
			dosStubReady_ = 1;
			return 1;
		}
		if (DeliverIrqs())
			continue;
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);
		if (s_mfd98GlueCs && cs == s_mfd98GlueCs && ip == 0x112)
			return 1;
		uint8_t* m = np2_mem();
		/* n3gv2.com: INT7F/INT D2 を植えたあと `MOV AX,9801 / INT 18 / JMP $-5` で常駐。
		   8 秒予算を焼き切らず、mfd 糊の INT18 keep と同じくここで抜ける。 */
		if (m) {
			const unsigned keep = ((unsigned)cs << 4) + (unsigned)ip;
			if (keep + 7u < 0x200000u
				&& m[keep] == 0xB8 && m[keep + 1] == 0x01 && m[keep + 2] == 0x98
				&& m[keep + 3] == 0xCD && m[keep + 4] == 0x18
				&& m[keep + 5] == 0xEB && m[keep + 6] == 0xF9)
				return 1;
		}
		if (s_fmxCalibAssist && m) {
			const unsigned physWait = ((unsigned)cs << 4) + (unsigned)ip;
			if (physWait + 6u < 0x200000u
				&& m[physWait] == 0xF7 && m[physWait + 1] == 0x06
				&& m[physWait + 2] == 0x84 && m[physWait + 3] == 0x3B) {
				const unsigned slot = ((unsigned)cs << 4) + 0x3B84u;
				if (slot + 1u < 0x200000u
					&& m[slot] == 0 && m[slot + 1] == 0) {
					m[slot] = 0x00;
					m[slot + 1] = 0x80;
				}
			}
		}
		const unsigned phys = ((unsigned)cs << 4) + (unsigned)ip;
		if (m && phys < 0x200000 && m[phys] == 0xF4) {
			uint8_t vec = 0;
			if (dos_.TrapVector(cs, ip, &vec)) {
				ValkyRewindCmd8Read(dos_,
					dosSong_[0] ? dosSong_
						: SelectedDosSong(dosGe_, extSong_), vec);
				CEmuDos98Result res = dos_.ServiceInt(m, vec);
				if (res == DOS98_TERMINATED)
					return 1;
				if (res == DOS98_RESIDENT) {
					const uint16_t ss = np2_reg_get(NP2_R_SS);
					const uint16_t sp = np2_reg_get(NP2_R_SP);
					const unsigned fr = ((unsigned)ss << 4) + (unsigned)sp;
					if (fr + 4u < 0x200000u) {
						const uint16_t retIp = (uint16_t)(m[fr] | (m[fr + 1] << 8));
						const uint16_t retCs = (uint16_t)(m[fr + 2] | (m[fr + 3] << 8));
						if (PatchUsmdUnloadHalt(m, retCs, retIp)) {
							dos_.IretReturn(m);
							return 1;
						}
					}
					return 1;
				}
				if (res == DOS98_EXEC)
					continue;
				dos_.IretReturn(m);
				continue;
			}
			/* 本物のアイドル HLT — タイマを進める */
			PatchSynth98PaiDest(m, dos_.PspSeg());
			const uint64_t q = 200;
			cpuCycles_ += q;
			TickSide(q);
			AdvanceOpnClocks(q);
			continue;
		}
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		cpuCycles_ += u;
		TickSide(u);
		AdvanceOpnClocks(u);
	}
	return stubState_ == 0x81 ? 1 : 0;
}

/* CHardPc98::BootDos の実装 */
int CHardPc98::BootDos(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	pmdOpnIrq_ = CEmuPc98GeIsPmd(ge);
	uint8_t* mem = np2_mem();
	if (!mem) return 0;

	np2_reset();
	np2_set_adrsmask(0x000FFFFFu);
	np2_setextsize(0);
	np2_set_v30(0);
	memset(mem, 0, 0xA0000);

	dos_.Reset();
	s_mfd98GlueCs = 0;
	s_mfdInt42Host = 0;
	s_midiDrvHostSmf = 0;
	dos_.InitArena(mem);
	dos_.InstallTrampolines(mem);
	dos_.InstallDosStructures(mem);
	PlantPc98BiosTimer(mem);
	/* DOS パックはまだ固定ファーム／コードイメージに依存し得る。特に PONYCA の MSCDRV フロントは CEE0:0004 の SOUND.ROM シグネチャを探り、INT 7E を出す前にその ROM から INT D2 を入れる。LoadRoms の早い isDos_ return は全 code/binary ROM を飛ばし、DOS D2 トランポリンに裏打ちされた見掛け INT 7E ラッパを残した。 */
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "binary") != 0
			&& _stricmp(r->type, "string") != 0)
			continue;
		const unsigned off = Pc98RomPhys(r->offset);
		if (off >= 0x200000u)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		uint8_t inlineBuf[256];
		if (!data || !sz) {
			const int nIn = Pc98ParseInlineRom(r->name, inlineBuf,
				(int)sizeof(inlineBuf));
			if (nIn <= 0)
				continue;
			data = inlineBuf;
			sz = (unsigned)nIn;
		}
		unsigned n = sz;
		if (off + n > 0x200000u)
			n = 0x200000u - off;
		memcpy(mem + off, data, n);
	}
	/* PC-98 BIOS ROM 窓 + テキスト VRAM + MEMSW + BIOS 作業 */
	PlantPc98BiosMap(mem);
	MaterializeDosFiles(fs, ge);
	{
		static const char* kOlteusMap[] = { "olteus", NULL };
		if (DosShellStarts(ge, kOlteusMap)) {
			const unsigned n = titleCode & 0x0fu;
			const char dig = (n < 10) ? (char)('0' + n) : (char)('A' + (n - 10));
			const char fp = opnaMode ? 'F' : 'P';
			CEmuDos98File* map = const_cast<CEmuDos98File*>(
				dos_.FindFile("MAP.EXE"));
			if (map && map->data && map->size)
				CEmuPc98PokeOlteusMusicName(map->data, map->size, dig, fp);
		}
	}
	BindDosRomHandles(ge);
	dosGe_ = ge;
	dosSong_[0] = 0;
	g_fmdLoadedSong[0] = 0;
	const char* sf = SelectedDosSong(ge, titleCode);
	if (sf) {
		strncpy_s(dosSong_, sf, _TRUNCATE);
		dos_.SetHandle(0x0B, sf);
		/* Pearlsoft OPN 行はカタログ midiout=1（GS /m と同じ zip）。modeMidi_ を残すと IRQ0 嵐。MUSDRV /f は Timer B を進めない（dumps 174 vs 歴史 1032）。 */
		{
			const char* ext = strrchr(sf, '.');
			if (ext && (_stricmp(ext, ".FM") == 0 || _stricmp(ext, ".OPN") == 0))
				modeMidi_ = 0;
		}
		/* fugam ブート AH=3F BX=5 のあと INT D3 AX=0201。シェル前にバインドしないと 0 バイト読が FMD を毒する。FMD /# もハンドル 0 が要る。裸 FMD / FMD -M6（ayayo2 OPN+EMD_98）は GS ハンドルを植えてはいけない。 */
		static const char* kFmdFugam[] = { "FMD /", "fugam", NULL };
		if (DosShellStarts(ge, kFmdFugam)) {
			/* ブート: AH=3F BX=5 のあと INT D3 AX=0201 が DS:SI を AH=3D 開。5 はファイル名テキスト。0 の生 .GS は AH=3D 失敗。 */
			dos_.SetHandleText(5, sf);
			dos_.SetHandle(0, sf);
		}
	}
	/* VALKY_98 は先に AH=3F BX=5（未使用ハンドル 0 バイトでも CF は 0 のまま）なのでカタログハンドル 6 の SSCP に届かない。5←6 エイリアス。 */
	{
		static const char* kValkyH[] = { "VALKY_98", "valky", NULL };
		if (DosShellStarts(ge, kValkyH)) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "file") != 0 || r->offset != 6)
					continue;
				const char* base = r->name ? r->name : "";
				for (const char* p = base; *p; p++) {
					if (*p == '\\' || *p == '/' || *p == ':')
						base = p + 1;
				}
				if (base[0])
					dos_.SetHandle(5, base);
				break;
			}
		}
	}

	extSong_ = (uint16_t)(titleCode & 0xff);
	extParam_ = 0;
	extCmd_ = 0;
	stubState_ = 0;
	cpuCycles_ = 0;
	opnPumpResidual_ = 0;
	picMask_ = 0xff;
	slavePicMask_ = 0xff;
	s_valkyKeepIrq0 = 0;
	s_fmxKeepIrq0 = 0;
	s_fmxCalibAssist = FmxDosShell(ge) ? 1 : 0;
	{
		static const char* kValkyPitBoot[] = { "VALKY_98", "valky", NULL };
		if (DosShellStarts(ge, kValkyPitBoot)) {
			s_valkyKeepIrq0 = 1;
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
		}
	}
	picMasterIcw_ = 0;
	picSlaveIcw_ = 0;
	opnInService_ = 0;
	g_opnIsrSs = 0;
	g_opnIsrSp = 0;
	g_pitInService = 0;
	g_pitIsrSs = 0;
	g_pitIsrSp = 0;
	g_mpuInService = 0;
	g_mpuIsrSs = 0;
	g_mpuIsrSp = 0;
	g_mpuIrqAsserted = 0;
	irqEdgeSeen_ = 0;
	irqEdgeConsumed_ = 0;
	g_censLine = g_censSvc = g_censNoVec = g_censMasked = g_censIfOff = 0;
	opnWriteCount_ = 0;
	opnKeyOnCount_ = 0;
	opnTlLiveCount_ = 0;
	opnFnumCount_ = 0;
	opnTimerCount_ = 0;
	opnIrqDeliverCount_ = 0;
	memset(opnKeyOnCh_, 0, sizeof(opnKeyOnCh_));
	pitTickCount_ = 0;
	timerIrqCount_ = 0;
	opnLogCount_ = 0;
	opnTailCount_ = 0;

	/* BIOS PIT は電源投入から数える。FM では IRQ0 はマスクのまま（シーケンサは OPN Timer B）。midi/beep は TSR 検出中に日時計が要る（FMD は時間付き ACK 待ちのあと CS:[1798] に MPU 版を格納）。 */
	if (!pitRunning_) {
		pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 60);
		if (pitReload_ == 0) pitReload_ = 1;
		pitCounter_ = pitReload_;
		pitRunning_ = 1;
		pitIrqPending_ = 0;
		pitResidual_ = 0;
	}
	if (modeMidi_ || modeBeep_)
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
	/* MMD.COM /I は INT 0E を自動植し MPU clock-to-host FD を待つ。PIC マスクポート 02h へは OUT しないので、IRQ6 が既に生きていないとプローブが STC 失敗し INT 61 がフックされない（dosmiss=int61）。 */
	if (modeMidi_)
		picMask_ = (uint8_t)(picMask_ & (uint8_t)~(1u << 6));

	/* 余裕あるシェル予算: PMDB2+PMDPCM パックは数秒要る。imd_1（#/Mxx 無し PMDB2）: カタログ PMD→PCM→糊が再 init し PPC バンクを落とす — PCM 前に糊を走る。#/Mxx パック（imd_2..4、fc98v13）はカタログ順（糊最後）。 */
	const uint64_t setupBudget = (uint64_t)cpuHz_ * 8ull; /* シェルあたり約 8 秒 */
	/* mbmusp/MUSDRV: SSG I/O A bits7-6 が INT14 を選ぶ。EOI はスレーブを仮定。
	   iwaplay/F.COM（IWADRV）は bit7=0 で INT14、bit7=1 で INT15。0xC0 を植えると
	   INT15 に ISR が行き、こちらの OPN は IRQ3/INT0B のまま無音になる。 */
	static const char* kSsgJumperShell[] = {
		"mbmus", "MBMUS", "musdrv", "MUSDRV", "muse", "MUSE",
		"fplay", "FPLAY",
		"mmd2", "MMD2", "mmd2va",
		NULL
	};
	if (chip_ && DosShellStarts(ge, kSsgJumperShell)) {
		ssgPortAJumper_ = 0xC0;
		chip_->Write(0, 0x0E);
		chip_->Write(1, 0xC0);
		opnLatchedAddr_ = 0x0E;
	}
	{
		static const char* kIwaHold[] = { "iwaplay", "IWAPLAY", NULL };
		s_iwaBusHold = DosShellStarts(ge, kIwaHold) ? 1 : 0;
	}
	/* shangva/demo_va: rom type=device（MUSIC.SYS/DEMO2.SYS）は糊シェル前に INIT し、再生／停止用 INT C8/C3 を用意 */
	RunDosDevices(ge, setupBudget);
	int hasGlue = 0, hasPcm = 0, hasHashM = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "shell") != 0) continue;
		const char* sn = r->name ? r->name : "";
		while (*sn == ' ' || *sn == '\t' || *sn == '#') sn++;
		if (_strnicmp(sn, "pmd_98", 6) == 0) hasGlue = 1;
		if (_strnicmp(sn, "pmdpcm", 6) == 0) hasPcm = 1;
		if (strchr(r->name, '#'))
			hasHashM = 1;
	}
	const int glueBeforePcm = hasGlue && hasPcm && !hasHashM;
	if (glueBeforePcm) {
		for (int pass = 0; pass < 3; pass++) {
			for (int i = 0; i < ge->romCount; i++) {
				const CEmuRomEntry* r = &ge->rom[i];
				if (_stricmp(r->type, "shell") != 0) continue;
				const char* sn = r->name ? r->name : "";
				while (*sn == ' ' || *sn == '\t' || *sn == '#') sn++;
				const int isGlue = (_strnicmp(sn, "pmd_98", 6) == 0);
				const int isPcm = (_strnicmp(sn, "pmdpcm", 6) == 0);
				const int want = isGlue ? 1 : (isPcm ? 2 : 0);
				if (want != pass) continue;
				RunDosCommand(r->name, setupBudget);
				if (r->name && _strnicmp(r->name, "FMD", 3) == 0)
					PlantFmdSongBank(&dos_);
			}
		}
	} else {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "shell") != 0) continue;
			const char* sn = r->name ? r->name : "";
			while (*sn == ' ' || *sn == '\t' || *sn == '#') sn++;
			/* olteus_va: MUSIC.EXE は 1KB OPN プローブで OUT 44/45 し AH=4C 終了。先に走ると全タイトルに同じ SSG ブリップが刻まれる。プレイヤは MAP.EXE オーバーレイ（olteus.com AH=4B03）。 */
			static const char* kOlteusSkipMus[] = { "olteus", NULL };
			if (DosShellStarts(ge, kOlteusSkipMus)
				&& _strnicmp(sn, "MUSIC", 5) == 0)
				continue;
			RunDosCommand(r->name, setupBudget);
			if (r->name && _strnicmp(r->name, "FMD", 3) == 0)
				PlantFmdSongBank(&dos_);
			if (_strnicmp(sn, "pmd_98", 6) == 0 && (dosStubReady_ || stubState_ == 0x81))
				break;
		}
	}
	dosStubReady_ = (stubState_ == 0x81) ? 1 : dosStubReady_;
	/* s_iwaBusHold は再生中も残す。カナリア IN 18A と ISR の SSG 読が
	   ymfm ReadData()=status のままだと CMP が外れ INT14 を植えない。 */
	PatchSynth98PaiDest(mem, dos_.PspSeg());
	/* PC-88VA DOS オーバーレイ（tetrisva/shinrava/famista89）: OPN ISR は INT14。欠けるとき INT0B へミラーし DeliverIrqs がシーケンサを進められるように。olteus は IRQ0→MAP:09BC で再生。INT14 のみは MUSIC 02 を飢えた。 */
	{
		static const char* kOlteusNo14[] = { "olteus", NULL };
		if (mem && pc88VaIo_ && !IvtHooked(PC98_OPN_IRQ_VEC, 1) && IvtHooked(0x14, 1)
			&& !DosShellStarts(ge, kOlteusNo14)) {
		const unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
		const unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
		mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
		picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
	}
	/* rtypeva: COM は INT0A に OPN を植え far 0C1B（VA ポート）。MAIN は INT14 に PC-98 ポート ISR も置く — VA 再生でそれを優先しない。 */
	static const char* kRtype[] = { "rtype", NULL };
	if (mem && pc88VaIo_ && DosShellStarts(ge, kRtype) && IvtHooked(0x0A, 1)) {
		const unsigned o0a = (unsigned)mem[0x0A * 4] | ((unsigned)mem[0x0A * 4 + 1] << 8);
		const unsigned s0a = (unsigned)mem[0x0A * 4 + 2] | ((unsigned)mem[0x0A * 4 + 3] << 8);
		mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o0a & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o0a >> 8) & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s0a & 0xff);
		mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s0a >> 8) & 0xff);
		picMask_ = (uint8_t)(picMask_ & ~((1u << 2) | (1u << 3)));
	}
	/* olteus_va: MAP.EXE ロード seg（COM far 表 [01C4]）を覚え、再生武装後に INT08 → near tick トランポリンを植える */
	olteusMapSeg_ = 0;
	olteusDataSeg_ = 0;
	olteusTimerOn_ = 0;
	olteusTrampOk_ = 0;
	olteusIrqPulse_ = 0;
	olteusInTick_ = 0;
	olteusTimerResidual_ = 0;
	olteusTickGuard_ = 0;
	static const char* kOlteus[] = { "olteus", NULL };
	if (mem && DosShellStarts(ge, kOlteus)) {
		const uint16_t psp = dos_.PspSeg();
		const unsigned mapSeg = (unsigned)mem[Pc98DosLin(psp, 0x1C4)]
			| ((unsigned)mem[Pc98DosLin(psp, 0x1C5)] << 8);
		if (mapSeg && mapSeg != (unsigned)DOS98_TRAMP_SEG)
			ArmOlteusVaTimer((uint16_t)mapSeg);
		/* INT18 アイドル前に MAP ハンドシェイクを終える — COM は早く戻る */
		if (olteusMapSeg_ && olteusDataSeg_) {
			olteusTimerOn_ = 1;
			const uint64_t hsBudget = (uint64_t)cpuHz_ * 2ull;
			const uint64_t hsEnd = cpuCycles_ + hsBudget;
			while (cpuCycles_ < hsEnd) {
				const unsigned base = (unsigned)olteusDataSeg_ << 4;
				const unsigned flag = (unsigned)mem[base + 0xCC4D]
					| ((unsigned)mem[base + 0xCC4D + 1] << 8);
				if (flag >= 0x0011u)
					break;
				PumpCycles(cpuCycles_ + (uint64_t)cpuHz_ / 60ull);
			}
		}
	}
	/* PMD はしばしば OPN ISR を入れてマスタマスク FF のまま。IVT0B がフックされていれば IRQ3 を外し OPN タイマが走れるように。 */
	if (mem && IvtHooked(PC98_OPN_IRQ_VEC, 1))
		picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
	/* usd_98（ASCII USO）: USD は INT F2 だけフック（memcpy stub）。再生は ADVBIOS 所有の INT F4（AH=0 ロード / AH=1 再生 / …）。F2→F4 をミラーしない — ADVBIOS API を消し keyOn=0。F4 が未フックなら F2 にフォールバック。INT F3 は AH 多重 API（OPN タイマ ISR ではない）— INT0B に植えない。ADVBIOS.OVL パックは F2/F4/far 表が揃ったあと INT7F 植前に far486 で IN 60h bit5（ここではトグルしない）待ちでハングしがち — ホストからインストールを終える。 */
	static const char* kUsd[] = { "usd_98", "usd98", NULL };
	if (mem && DosShellStarts(ge, kUsd)) {
		const unsigned f2o = (unsigned)mem[0xF2 * 4] | ((unsigned)mem[0xF2 * 4 + 1] << 8);
		const unsigned f2s = (unsigned)mem[0xF2 * 4 + 2] | ((unsigned)mem[0xF2 * 4 + 3] << 8);
		const unsigned f4o = (unsigned)mem[0xF4 * 4] | ((unsigned)mem[0xF4 * 4 + 1] << 8);
		const unsigned f4s = (unsigned)mem[0xF4 * 4 + 2] | ((unsigned)mem[0xF4 * 4 + 3] << 8);
		const int f4Live = (f4s != 0 && f4s != (unsigned)DOS98_TRAMP_SEG
			&& !(f4s == f2s && f4o == f2o));
		/* 名前ロード USD（0232 = PUSH ES/DS/PUSHA）と ADVBIOS.OVL は本物 F4 が要る。
		   F2 を F4 にミラーすると ADVBIOS API（AH=0x30）が USD スタブへ落ちる。 */
		int nameLoadUsd = 0;
		if (f2s && f2s != (unsigned)DOS98_TRAMP_SEG) {
			const unsigned b = f2s << 4;
			if (b + 0x235u < 0x200000u && mem[b + 0x232] == 0x06
				&& mem[b + 0x233] == 0x1E && mem[b + 0x234] == 0x60)
				nameLoadUsd = 1;
		}
		const int haveAdvbios = (dos_.FindFile("ADVBIOS.OVL") != NULL);
		if (!f4Live && f2s != 0 && f2s != (unsigned)DOS98_TRAMP_SEG
			&& !nameLoadUsd && !haveAdvbios) {
			mem[0xF4 * 4] = (uint8_t)(f2o & 0xff);
			mem[0xF4 * 4 + 1] = (uint8_t)((f2o >> 8) & 0xff);
			mem[0xF4 * 4 + 2] = (uint8_t)(f2s & 0xff);
			mem[0xF4 * 4 + 3] = (uint8_t)((f2s >> 8) & 0xff);
		}
		const unsigned s7f = (unsigned)mem[0x7F * 4 + 2] | ((unsigned)mem[0x7F * 4 + 3] << 8);
		if (f2s != 0 && f2s != (unsigned)DOS98_TRAMP_SEG
			&& (s7f == 0 || s7f == (unsigned)DOS98_TRAMP_SEG)) {
			const unsigned base = f2s << 4;
			const unsigned off486 = (unsigned)mem[base + 0x486] | ((unsigned)mem[base + 0x487] << 8);
			const unsigned seg486 = (unsigned)mem[base + 0x488] | ((unsigned)mem[base + 0x489] << 8);
			if (seg486 && off486) {
				mem[0x7F * 4 + 0] = 0xC4;
				mem[0x7F * 4 + 1] = 0x01;
				mem[0x7F * 4 + 2] = (uint8_t)(f2s & 0xff);
				mem[0x7F * 4 + 3] = (uint8_t)((f2s >> 8) & 0xff);
				/* USD HLT アイドル（CS:01BD）にパーク */
				if (base + 0x1BF < 0x200000u) {
					mem[base + 0x1BD] = 0xF4;
					mem[base + 0x1BE] = 0xEB;
					mem[base + 0x1BF] = 0xFD;
				}
				np2_set_cs_ip((uint16_t)f2s, 0x01BD);
				np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				stubState_ = 0x81;
			}
		}
		/* ADVH USD（1585/1599）: インストール後 HLT @022E で INT7F アイドル。一部 ADVH.EXE ビルドは MZ 入口に DS=loadSeg 復号 stub（IP 0x120..0x200、xor ループ後 jmp 0010）。TC0/EXEPACK シグネチャが違うと USD の 0395/0402 が外し、[0706]/[0706]+10 に MZ ヘッダ＋オーバーレイ本体があるのに F1 はトランポリン。ホストでその入口を終え、INT7F を植えてパーク。 */
		{
			unsigned usdCs = 0;
			const unsigned cur7f = (unsigned)mem[0x7F * 4 + 2]
				| ((unsigned)mem[0x7F * 4 + 3] << 8);
			if (cur7f && cur7f != (unsigned)DOS98_TRAMP_SEG)
				usdCs = cur7f;
			else {
				const unsigned cs = np2_reg_get(NP2_R_CS);
				const unsigned ip = np2_reg_get(NP2_R_IP);
				if (cs > 0x100 && cs < 0xA000) {
					const unsigned b = cs << 4;
					if (b + 0x240 < 0x200000u && mem[b + 0x232] == 0x06
						&& mem[b + 0x233] == 0x1E && mem[b + 0x234] == 0x60)
						usdCs = cs;
					else if ((ip == 0x022E || ip == 0x022F || ip == 0x0230)
						&& b + 0x240 < 0x200000u && mem[b + 0x232] == 0x06)
						usdCs = cs;
				}
			}
			if (usdCs) {
				const unsigned base = usdCs << 4;
				const unsigned live7f = (unsigned)mem[0x7F * 4 + 2]
					| ((unsigned)mem[0x7F * 4 + 3] << 8);
				if (live7f == 0 || live7f == (unsigned)DOS98_TRAMP_SEG) {
					if (base + 0x240 < 0x200000u && mem[base + 0x232] == 0x06
						&& mem[base + 0x233] == 0x1E) {
						mem[0x7F * 4 + 0] = 0x32;
						mem[0x7F * 4 + 1] = 0x02;
						mem[0x7F * 4 + 2] = (uint8_t)(usdCs & 0xff);
						mem[0x7F * 4 + 3] = (uint8_t)((usdCs >> 8) & 0xff);
					}
				}
				if (base + 0x231 < 0x200000u) {
					mem[base + 0x22E] = 0xF4;
					mem[base + 0x22F] = 0xEB;
					mem[base + 0x230] = 0xFD;
					np2_set_cs_ip((uint16_t)usdCs, 0x022E);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				}
				stubState_ = 0x81;
			}
		}
		/* 名前ロード ADVH: 双セグメント data / ISR DS 一貫を終え、BootDos が常駐イメージを持つ間（TriggerPlay 曲 I/O 前）に OEM 音楽 ISR へ INT0B を植える */
		{
			const unsigned sF1 = (unsigned)mem[0xF1 * 4 + 2]
				| ((unsigned)mem[0xF1 * 4 + 3] << 8);
			if (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG
				&& AdvhNormalizeNameLoadResident(mem, sF1))
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
	}
	/* USDDRV.EXE（nike usddrv /s）: 糊 usddrv98 は INT F1 AX=0/AL=1。EXE が AH=4C で落ちるか 2 本目 COM に上書きされると F1 がトランポリンのまま writes=18。アリーナへ載せ入口を終え、C7 06 [03C4],01BF / MOV [03C6],CS を IVT F1 へ。 */
	{
		static const char* kUsdDrv[] = { "usddrv", "USDDRV", NULL };
		if (mem && DosShellStarts(ge, kUsdDrv)) {
			unsigned sF1 = (unsigned)mem[0xF1 * 4 + 2]
				| ((unsigned)mem[0xF1 * 4 + 3] << 8);
			if (sF1 == 0 || sF1 == (unsigned)DOS98_TRAMP_SEG) {
				const CEmuDos98File* exe = dos_.FindFile("USDDRV.EXE");
				if (exe && exe->data && exe->size >= 0x20
					&& exe->data[0] == 'M' && exe->data[1] == 'Z') {
					unsigned minA = (unsigned)exe->data[0x0A]
						| ((unsigned)exe->data[0x0B] << 8);
					unsigned need = (exe->size / 16u) + minA + 0x20u;
					if (need < 0x200u)
						need = 0x200u;
					if (need > 0x1000u)
						need = 0x1000u;
					uint16_t got = 0;
					unsigned loadSeg = 0;
					if (dos_.AllocBlock(mem, (uint16_t)need, &got) && got)
						loadSeg = got;
					if (loadSeg && loadSeg != (unsigned)DOS98_TRAMP_SEG) {
						const unsigned ip = (unsigned)exe->data[0x14]
							| ((unsigned)exe->data[0x15] << 8);
						const unsigned csRel = (unsigned)exe->data[0x16]
							| ((unsigned)exe->data[0x17] << 8);
						dos_.LoadOverlay(mem, exe->data, exe->size,
							(uint16_t)loadSeg, (uint16_t)loadSeg);
						unsigned f1Off = 0x01BF;
						for (unsigned p = 0; p + 10u < exe->size; p++) {
							if (exe->data[p] == 0xC7 && exe->data[p + 1] == 0x06
								&& exe->data[p + 2] == 0xC4 && exe->data[p + 3] == 0x03
								&& exe->data[p + 6] == 0x8C && exe->data[p + 7] == 0x0E
								&& exe->data[p + 8] == 0xC6 && exe->data[p + 9] == 0x03) {
								f1Off = (unsigned)exe->data[p + 4]
									| ((unsigned)exe->data[p + 5] << 8);
								break;
							}
						}
						const unsigned entrySeg = loadSeg + csRel;
						const unsigned tramp = 0x50000;
						unsigned ti = 0;
						mem[tramp + ti++] = 0x9A;
						mem[tramp + ti++] = (uint8_t)(ip & 0xff);
						mem[tramp + ti++] = (uint8_t)((ip >> 8) & 0xff);
						mem[tramp + ti++] = (uint8_t)(entrySeg & 0xff);
						mem[tramp + ti++] = (uint8_t)((entrySeg >> 8) & 0xff);
						mem[tramp + ti++] = 0xF4;
						np2_reg_set(NP2_R_CS, 0x5000);
						np2_reg_set(NP2_R_IP, 0);
						np2_reg_set(NP2_R_DS, (uint16_t)loadSeg);
						np2_reg_set(NP2_R_ES, (uint16_t)loadSeg);
						{
							const unsigned ssRel = (unsigned)exe->data[0x0E]
								| ((unsigned)exe->data[0x0F] << 8);
							const unsigned spv = (unsigned)exe->data[0x10]
								| ((unsigned)exe->data[0x11] << 8);
							const unsigned ss = loadSeg + ssRel;
							if (ssRel && ss < 0xA000u && spv) {
								np2_reg_set(NP2_R_SS, (uint16_t)ss);
								np2_reg_set(NP2_R_SP, (uint16_t)spv);
							}
						}
						np2_reg_set(NP2_R_FLAGS,
							(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
						PumpCycles(cpuCycles_ + (uint64_t)cpuHz_ * 3ull);
						sF1 = (unsigned)mem[0xF1 * 4 + 2]
							| ((unsigned)mem[0xF1 * 4 + 3] << 8);
						if (sF1 == 0 || sF1 == (unsigned)DOS98_TRAMP_SEG) {
							mem[0xF1 * 4 + 0] = (uint8_t)(f1Off & 0xff);
							mem[0xF1 * 4 + 1] = (uint8_t)((f1Off >> 8) & 0xff);
							mem[0xF1 * 4 + 2] = (uint8_t)(loadSeg & 0xff);
							mem[0xF1 * 4 + 3] = (uint8_t)((loadSeg >> 8) & 0xff);
						}
					}
				}
			}
			sF1 = (unsigned)mem[0xF1 * 4 + 2]
				| ((unsigned)mem[0xF1 * 4 + 3] << 8);
			if (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG) {
				const unsigned o14 = (unsigned)mem[0x14 * 4]
					| ((unsigned)mem[0x14 * 4 + 1] << 8);
				const unsigned s14 = (unsigned)mem[0x14 * 4 + 2]
					| ((unsigned)mem[0x14 * 4 + 3] << 8);
				if (s14 && s14 != (unsigned)DOS98_TRAMP_SEG) {
					mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
					mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
					mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
					mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
				}
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
		}
	}
	/* Crowd CMD/CMDP: CS:2393 の OPN プローブがチップを組んだあと全 CF=0 経路で [CS:1792] をクリア。INT60 再生（idx1）は [1792] < 1 の間スピンするので曲読は成功するが keyOn が始まらない。後の再生段階も [1792]==1 ちょうどが要る（プローブ段階の 2/3/4 ではない）。音楽 ISR は INT14（MADP/N3GOLF と同じ）。INT0B へミラーし DeliverIrqs が OPN タイマ tick を撃てるように。 */
	static const char* kCmd[] = { "CMD", "CMDP", NULL };
	if (mem && DosShellStarts(ge, kCmd)) {
		const unsigned o60 = (unsigned)mem[0x60 * 4] | ((unsigned)mem[0x60 * 4 + 1] << 8);
		const unsigned s60 = (unsigned)mem[0x60 * 4 + 2] | ((unsigned)mem[0x60 * 4 + 3] << 8);
		if (s60 != 0 && s60 != (unsigned)DOS98_TRAMP_SEG) {
			const unsigned flagPhys = ((unsigned)s60 << 4) + 0x1792u;
			if (flagPhys < 0x200000u)
				mem[flagPhys] = 1;
			unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
			unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
			if (s14 == 0 || s14 == (unsigned)DOS98_TRAMP_SEG) {
				/* インストール経路が飛ばされた（フック時 [1792]==0）。常駐 CMD イメージ内の ISR プロローグを探し INT14 を植える */
				static const uint8_t kIsr[] = { 0x9C, 0x60, 0x55, 0x1E, 0x06, 0xFA };
				const unsigned base = (unsigned)s60 << 4;
				unsigned found = 0;
				for (unsigned off = 0x100; off + sizeof(kIsr) < 0x8000; off++) {
					const unsigned p = base + off;
					if (p + sizeof(kIsr) > 0x200000u) break;
					int match = 1;
					for (unsigned k = 0; k < sizeof(kIsr); k++) {
						if (mem[p + k] != kIsr[k]) { match = 0; break; }
					}
					if (match) { found = off; break; }
				}
				if (found) {
					o14 = found;
					s14 = s60;
					mem[0x14 * 4] = (uint8_t)(o14 & 0xff);
					mem[0x14 * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
					mem[0x14 * 4 + 2] = (uint8_t)(s14 & 0xff);
					mem[0x14 * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
				}
			}
			if (s14 != 0 && s14 != (unsigned)DOS98_TRAMP_SEG) {
				mem[0x0B * 4] = (uint8_t)(o14 & 0xff);
				mem[0x0B * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
				mem[0x0B * 4 + 2] = (uint8_t)(s14 & 0xff);
				mem[0x0B * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			(void)o60;
		}
	}
	/* HuLinks fakecall→music→46: MUSIC.COM は INT14 に OPN ISR を置く。INT0B へだけミラー — 同じ ISR を INT08/PIT にも植えない。二重配送（OPN + PIT）はシーケンサを倍 tick: 46oku は [0294] が早く 0x10 になり mute-all でフェードアウト。チャネル凍結は欠 PIT ではなく cmd2→AH=1 mute 経路。 */
	static const char* kStarcmd[] = { "fakecall", "music", "MUSIC", "46", NULL };
	static const char* kAsciiMusicR[] = { "music -r", "music_98", NULL };
	if (mem && DosShellStarts(ge, kStarcmd) && !DosShellStarts(ge, kAsciiMusicR)) {
		musicComKeepalive_ = 1;
		unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
		unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
		if (s14 != 0 && s14 != (unsigned)DOS98_TRAMP_SEG) {
			mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
			mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
			mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
			mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
			picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
	}
	/* IWADRV F.COM: カナリア失敗時は INT EB だけ TSR。INT EB の CS から OPN ISR を
	   INT14/INT0B へ植える。IBM `OUT 08h,20h` を PC-98 `OUT 00h,20h` に直す。
	   btltech は ISR が CS:457D（旧スキャン 4000h 外）。clrd3 は STI 無しの CLD PUSHA。 */
	{
		static const char* kIwa[] = { "iwaplay", "IWAPLAY", NULL };
		if (mem && DosShellStarts(ge, kIwa) && !IvtHooked(0x14, 1)) {
			unsigned found = 0, fseg = 0;
			unsigned segs[3];
			unsigned nseg = 0;
			const unsigned cand[3] = {
				(unsigned)mem[0xEB * 4 + 2] | ((unsigned)mem[0xEB * 4 + 3] << 8),
				(unsigned)mem[0xD2 * 4 + 2] | ((unsigned)mem[0xD2 * 4 + 3] << 8),
				(unsigned)mem[0x7F * 4 + 2] | ((unsigned)mem[0x7F * 4 + 3] << 8)
			};
			for (unsigned ci = 0; ci < 3; ci++) {
				const unsigned s = cand[ci];
				if (!s || s == (unsigned)DOS98_TRAMP_SEG)
					continue;
				unsigned dup = 0;
				for (unsigned k = 0; k < nseg; k++) {
					if (segs[k] == s) { dup = 1; break; }
				}
				if (!dup && nseg < 3)
					segs[nseg++] = s;
			}
			for (unsigned si = 0; si < nseg && !found; si++) {
				const unsigned seb = segs[si];
				const unsigned base = seb << 4;
				for (unsigned off = 0x80; off + 96u < 0xC000u; off++) {
					const unsigned p = base + off;
					if (p + 96u >= 0x200000u)
						break;
					unsigned o = (mem[p] == 0xFB) ? 1u : 0u;
					if (mem[p + o] == 0xFC && mem[p + o + 1] == 0x60
						&& mem[p + o + 2] == 0x1E && mem[p + o + 3] == 0x06
						&& mem[p + o + 4] == 0x8C && mem[p + o + 5] == 0xC8) {
						unsigned has188 = 0;
						for (unsigned k = 0; k < 56u && p + o + k + 2u < 0x200000u; k++) {
							if (mem[p + o + k] == 0xBA && mem[p + o + k + 1] == 0x88
								&& mem[p + o + k + 2] == 0x01) {
								has188 = 1;
								break;
							}
						}
						if (has188) {
							found = off;
							fseg = seb;
							break;
						}
					}
				}
			}
			if (!found) {
				for (unsigned phys = 0x1000u; phys + 64u < 0xA0000u; phys++) {
					unsigned o = (mem[phys] == 0xFB) ? 1u : 0u;
					if (mem[phys + o] != 0xFC || mem[phys + o + 1] != 0x60
						|| mem[phys + o + 2] != 0x1E || mem[phys + o + 3] != 0x06
						|| mem[phys + o + 4] != 0x8C || mem[phys + o + 5] != 0xC8)
						continue;
					unsigned has188 = 0;
					for (unsigned k = 0; k < 56u; k++) {
						if (mem[phys + o + k] == 0xBA && mem[phys + o + k + 1] == 0x88
							&& mem[phys + o + k + 2] == 0x01) {
							has188 = 1;
							break;
						}
					}
					if (!has188)
						continue;
					found = phys & 0xFu;
					fseg = (unsigned)(phys >> 4);
					break;
				}
			}
			if (found && fseg) {
				const unsigned phys = (fseg << 4) + found;
				for (unsigned k = 0; k < 96u && phys + k + 1u < 0x200000u; k++) {
					if (mem[phys + k] == 0xE6 && mem[phys + k + 1] == 0x08)
						mem[phys + k + 1] = 0x00;
				}
				mem[0x14 * 4 + 0] = (uint8_t)(found & 0xff);
				mem[0x14 * 4 + 1] = (uint8_t)((found >> 8) & 0xff);
				mem[0x14 * 4 + 2] = (uint8_t)(fseg & 0xff);
				mem[0x14 * 4 + 3] = (uint8_t)((fseg >> 8) & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(found & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((found >> 8) & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(fseg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((fseg >> 8) & 0xff);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
		}
	}
	FmxArmPitIrq0(ge, modeMidi_ ? 0 : 1);
	{
		/* NeSS SPLIT: SSG 0Eh IN が 0 だと検出スコア DH=0 で 188h 即値パッチを jae スキップ。04D0 が OUT 00h/02h（PIC）のまま曲書きが OPN に届かない。IRQ 表は DH=0 が INT0B なのでジャンパは触らず、未パッチ即値だけ 88h にする。 */
		static const char* kNessSplit[] = { "SPLIT", "split", NULL };
		if (mem && DosShellStarts(ge, kNessSplit)) {
			const unsigned off = (unsigned)mem[0xD2 * 4]
				| ((unsigned)mem[0xD2 * 4 + 1] << 8);
			const unsigned seg = (unsigned)mem[0xD2 * 4 + 2]
				| ((unsigned)mem[0xD2 * 4 + 3] << 8);
			const unsigned b = seg << 4;
			if (seg && seg < 0xA000u && off == 0x15Eu && b + 0x9F2u < 0x200000u
				&& mem[b + 0x15Eu] == 0x50 && mem[b + 0x15Fu] == 0x2E) {
				static const unsigned kImm[] = { 0x4CCu, 0x4D8u, 0x4F7u, 0x532u, 0x9F1u };
				for (unsigned i = 0; i < 5; i++) {
					const unsigned p = b + kImm[i];
					if (mem[p] == 0 && mem[p + 1] == 0) {
						mem[p] = 0x88;
						mem[p + 1] = 0;
					}
				}
			}
		}
	}
	if (s_fmxKeepIrq0) {
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
		FmxPlantInt60FromPit(np2_mem());
	}
	/* BIOS PIT は常に数える。IRQ0 は midi/beep/INT 1C 以外マスクのまま */
	if (!pitRunning_) {
		pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 60);
		if (pitReload_ == 0) pitReload_ = 1;
		pitCounter_ = pitReload_;
		pitRunning_ = 1;
		pitIrqPending_ = 0;
		pitResidual_ = 0;
	}
	/* シェル終了後にだけ MPU キャプチャを武装（FMP -m プローブは 0 を OUT）。FMD /# AH=2 D58 は fugam ブート中に既に GS sysex を流した — 消さずインテリジェントモードのまま。MMD.COM MIDI（INT 61 + MMP_HOOT）も同じ: /K /D インストール後の UART 強制 + CaptureReset が CTH を落とし INT 61 未フック。裸 "MMD" に一致させない — それは MMD2.SYS でもある。Pearlsoft MUSDRV /f .FM はカタログ midiout タグ（GS /m と同じ zip）。そこでの UART 強制 + CaptureReset は /f が OPN を飛ばした（歴史ピーク約 24k が無音）。バインド曲が FM なら MPU を触らない。 */
	int fmSongNoUart = 0;
	if (dosSong_[0]) {
		const char* ext = strrchr(dosSong_, '.');
		if (ext && (_stricmp(ext, ".FM") == 0 || _stricmp(ext, ".OPN") == 0))
			fmSongNoUart = 1;
	}
	if (modeMidi_ && !fmSongNoUart) {
		static const char* kFmdIntel[] = { "FMD /", "fugam", NULL };
		static const char* kMmdMidi[] = {
			"MMD /", "MMP_HOOT", "mmp_hoot", "mmd2m", "MMD2M", NULL
		};
		/* HOT-B MIDIDRV.EXE（7colors）: SMF プレイヤ。AH=81 再生は MPU コマンド 88/EC/01/B8/0A。INT 0E ISR が CTH FD でトラックを歩く。インストール後 UART 強制は B8 を no-op にした（midi=01 01）。 */
		static const char* kMidiDrvIntel[] = {
			"MIDIDRV", "mididrv", "7COLM", "7colm", NULL
		};
		const int fmdIntel = DosShellStarts(ge, kFmdIntel) ? 1 : 0;
		const int mmdMidi = DosShellStarts(ge, kMmdMidi) ? 1 : 0;
		const int midiDrvIntel = DosShellStarts(ge, kMidiDrvIntel) ? 1 : 0;
		const int mpuIntel = (fmdIntel || mmdMidi || midiDrvIntel) ? 1 : 0;
		if (!mpuIntel) {
			mpuUart_ = 1;
			midiCapArmed_ = 1;
			/* FMP3 -m は自分で Timer B を組む。Wolf ブリッジが同じ YM に
			   キーオンすると 26h ラッチが壊れ、密な曲（vg2 ::0001）だけ超高速。
			   再生は UART → VST。非 VST の Wolfteam MUSDRV だけブリッジする。 */
			if (!CEmuPc98IsFmp(ge)) {
				wolfBridgeEnable_ = 1;
				WolfBridgeReset();
			} else
				FmpPatchMidiRetrigger(np2_mem());
			MidiCaptureReset();
		} else {
			mpuUart_ = 0;
			midiCapArmed_ = 1;
			/* GS ダンプ後のチャネルボイスでプローブ用に OPN を鳴らせる */
			wolfBridgeEnable_ = 1;
		}
		/* FMP UART は IRQ0 をマスクのまま。BootDos で開けると TriggerPlay
		   前に BIOS INT08 が入り、MIDI テンポが TB+PIT になる。 */
		if (fmpSeq_ && mpuUart_)
			picMask_ = (uint8_t)(picMask_ | 0x01);
		else
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
		if (mpuIntel && IvtHooked(0x0E, 1))
			picMask_ = (uint8_t)(picMask_ & (uint8_t)~(1u << 6));
		if (fmdIntel && dosSong_[0] && IvtHooked(0x7F, 1))
			strncpy_s(g_fmdLoadedSong, dosSong_, _TRUNCATE);
		else if (!fmdIntel)
			g_fmdLoadedSong[0] = 0;
	}
	{
		if (ValkyWantArm(ge, &dos_))
			ValkyArmSscpPlay(np2_mem(), (uint16_t)(titleCode & 0xffff),
				&dos_, dosSong_);
	}
	return 1;
}

/* CHardPc98::AdvanceOpnClocks の実装 */
void CHardPc98::AdvanceOpnClocks(uint64_t cpuCycles)
{
	if (!chip_ || cpuCycles == 0 || cpuHz_ <= 0 || opnHz_ <= 0) return;
	/* 呼をまたぐ残余: 旧の呼毎切り捨ては命令あたりのクロック大半を捨て、DOS INT サービス経路はチップへ渡さず cpuCycles_ だけ進めた。合わせて OPNA はマスタクロックの約 42% しか見ず、全 FM タイマ（＝テンポ）が同じ係数で遅れた。 */
	opnPumpResidual_ += cpuCycles * (uint64_t)opnHz_;
	const uint64_t ot = opnPumpResidual_ / (uint64_t)cpuHz_;
	opnPumpResidual_ %= (uint64_t)cpuHz_;
	if (ot) chip_->AdvanceClocks(ot);
}

/* IRQ 配送付きで CPU を endCycle まで進める */
void CHardPc98::PumpCycles(uint64_t endCycle)
{
	CEmuHardPc98SetActive(this);
	{
		static int profInit = 0;
		if (!profInit) { profInit = 1; IpProfInit(); }
	}
	while (cpuCycles_ < endCycle) {
		/* 曲が動き出したら TriggerPlay の 0.5s settle を打ち切る。タイマだけ先に
		   進むと rance/tlove/sorc/vg2 の先頭が圧縮される。Render 中はフラグを立てない。
		   NoteOn / MIDI note が増えたら演奏開始。タイマや SysEx だけでは
		   FMD のモジュールロードを切らない。同じ曲の再入は 5ms で切る。 */
		if (pumpAbortOnMusic_ && !fmd98_ && cpuHz_ > 0
			&& (cpuCycles_ - pumpMusicCycle0_) >= ((uint64_t)cpuHz_ / 200ull)
			&& (pumpSameLive_
				|| opnKeyOnCount_ > pumpMusicKey0_
				|| MidiNoteOnCount() > pumpMusicMidi0_
				|| (fmpSeq_ && !mpuUart_ && opnTimerCount_ > pumpMusicTimer0_)))
			break;
		uint8_t* mem = np2_mem();
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);
		if (g_muse2Seg && g_muse2Intr
			&& cs == (uint16_t)g_muse2Seg && ip == 0x0014)
			np2_reg_set(NP2_R_IP, g_muse2Intr);
		if (g_mmdLoadSeg)
			MmdPlayAssist(mem);
		if (g_sddLoadSeg)
			SddKeepPlay(mem, 0);
		if (g_muse2Seg)
			Muse2KeepPlay(mem);
		if (g_sddLoadSeg && chip_ && mem) {
			static uint64_t s_sddArmCyc;
			static unsigned s_sddArmIrq;
			if (opnIrqDeliverCount_ != s_sddArmIrq) {
				s_sddArmIrq = opnIrqDeliverCount_;
				s_sddArmCyc = cpuCycles_;
			} else if (cpuHz_ > 20
				&& (cpuCycles_ - s_sddArmCyc) > ((uint64_t)cpuHz_ / 20ull)) {
				opnInService_ = 0;
				chip_->Write(0, 0x26);
				chip_->Write(1, 0xCA);
				chip_->Write(0, 0x27);
				chip_->Write(1, 0x3A);
				opnLatchedAddr_ = 0x27;
				g_lastTimerCtrl = 0x3A;
				picMask_ = (uint8_t)(picMask_ & ~((1u << 2) | (1u << 3)));
				slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
				s_sddArmCyc = cpuCycles_;
			}
		}
		if (g_muse2Seg && g_muse2Intr && mem) {
			const unsigned lin = (unsigned)g_muse2Seg << 4;
			if (lin + 10u < 0x200000u)
				Pc98Wr16(mem, lin + 8u, g_muse2Intr);
		}
		if (g_mmdClassic && g_mmdLoadSeg && chip_ && mem) {
			const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
			const int sbr = MmdClassicIsSbr(mem, lin);
			const int fray = MmdClassicIsFray(mem, lin);
			int playing = 0;
			if (sbr && lin + 0x17F5u < 0x200000u && mem[lin + 0x17F4])
				playing = 1;
			if (fray && lin + 0x17D9u < 0x200000u
				&& (mem[lin + 0x17D8]
					|| mem[lin + 0x17F3] || mem[lin + 0x17F4]))
				playing = 1;
			if (playing) {
				/* 毎命令 27h=15 はタイマをロードし直して IRQ が永遠に来ない。止まったときだけ再武装。 */
				static uint64_t s_mmdArmCyc;
				static uint64_t s_mmdTickCyc;
				static unsigned s_mmdArmIrq;
				if (opnIrqDeliverCount_ != s_mmdArmIrq) {
					s_mmdArmIrq = opnIrqDeliverCount_;
					s_mmdArmCyc = cpuCycles_;
				} else if (cpuHz_ > 20
					&& (cpuCycles_ - s_mmdArmCyc) > ((uint64_t)cpuHz_ / 20ull)) {
					/* 0255 相当: 4B4B/5A → 24h=CA 25h=02 27h=35。15h だと Timer A がすぐ死ぬ。 */
					chip_->Write(0, 0x24);
					chip_->Write(1, 0xCA);
					chip_->Write(0, 0x25);
					chip_->Write(1, 0x02);
					chip_->Write(0, 0x27);
					chip_->Write(1, 0x35);
					opnLatchedAddr_ = 0x27;
					g_lastTimerCtrl = 0x35;
					s_mmdArmCyc = cpuCycles_;
					picMask_ = (uint8_t)(picMask_ & ~((1u << 2) | (1u << 3)));
					slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
					if (!opnInService_) {
						uint8_t vec = 0x14;
						if (!IvtHooked(0x14, 1) && IvtHooked(PC98_OPN_IRQ_VEC, 1))
							vec = (uint8_t)PC98_OPN_IRQ_VEC;
						if (IvtHooked(vec, 1)) {
							opnInService_ = 1;
							opnIrqDeliverCount_++;
							np2_interrupt(vec);
						}
					}
				} else if ((g_lastTimerCtrl & 0x0C) == 0) {
					chip_->Write(0, 0x24);
					chip_->Write(1, 0xCA);
					chip_->Write(0, 0x25);
					chip_->Write(1, 0x02);
					chip_->Write(0, 0x27);
					chip_->Write(1, 0x35);
					opnLatchedAddr_ = 0x27;
					g_lastTimerCtrl = 0x35;
				}
				/* IRQ が生きていても 07DC はノートを進めない。50ms ごとにホストが進める。 */
				if (cpuHz_ > 20
					&& (cpuCycles_ - s_mmdTickCyc) > ((uint64_t)cpuHz_ / 20ull)) {
					s_mmdTickCyc = cpuCycles_;
					{
						unsigned songOff, songSz, base, fnBase;
						if (fray) {
							songOff = (unsigned)mem[lin + 0x1542]
								| ((unsigned)mem[lin + 0x1543] << 8);
							songSz = (unsigned)mem[lin + 0x153C]
								| ((unsigned)mem[lin + 0x153D] << 8);
							if (songOff < 0x19E8u) songOff = 0x1DE8u;
							if (songSz < 64u || songSz > 0x8000u)
								songSz = 0x1000u;
							base = lin + 0x17F3u;
							fnBase = lin + 0x15E8u;
						} else {
							songOff = (unsigned)mem[lin + 0x155E]
								| ((unsigned)mem[lin + 0x155F] << 8);
							songSz = (unsigned)mem[lin + 0x155A]
								| ((unsigned)mem[lin + 0x155B] << 8);
							if (songOff < 0x1A04u) songOff = 0x1E04u;
							if (songSz < 64u) songSz = 4096u;
							base = lin + 0x180Fu;
							fnBase = lin + 0x1604u;
						}
						for (int ch = 0; ch < 3; ch++) {
							const unsigned p = base + (unsigned)ch * 0x33u;
							if (p + 8u >= 0x200000u) continue;
							unsigned off = (unsigned)mem[p]
								| ((unsigned)mem[p + 1] << 8);
							if (!off || lin + off >= 0x200000u) continue;
							uint8_t op = mem[lin + off];
							if (op == 0 || op > 0x24u) {
								off = MmdSkipToNote(mem, lin, off, p);
								mem[p] = (uint8_t)(off & 0xff);
								mem[p + 1] = (uint8_t)(off >> 8);
								if (lin + off >= 0x200000u) continue;
								op = mem[lin + off];
							}
							if (op >= 1u && op <= 0x24u) {
								const unsigned tp = fnBase + (unsigned)op * 4u;
								unsigned fn = 0x0265u;
								if (tp + 1u < 0x200000u) {
									const unsigned raw = (unsigned)mem[tp]
										| ((unsigned)mem[tp + 1] << 8);
									if (raw)
										fn = raw;
								}
								unsigned oct = mem[p + 8];
								if (!oct)
									oct = 4u;
								const uint8_t a4 = (uint8_t)((oct << 3) | ((fn >> 8) & 7u));
								const uint8_t a0 = (uint8_t)fn;
								chip_->Write(0, 0x28);
								chip_->Write(1, (uint8_t)ch);
								chip_->Write(0, (uint8_t)(0x4C + ch));
								chip_->Write(1, 0x18);
								chip_->Write(0, (uint8_t)(0x5C + ch));
								chip_->Write(1, 0x1F);
								chip_->Write(0, (uint8_t)(0x6C + ch));
								chip_->Write(1, 0x00);
								chip_->Write(0, (uint8_t)(0x7C + ch));
								chip_->Write(1, 0x00);
								chip_->Write(0, (uint8_t)(0x8C + ch));
								chip_->Write(1, 0xF0);
								chip_->Write(0, (uint8_t)(0xA4 + ch));
								chip_->Write(1, a4);
								chip_->Write(0, (uint8_t)(0xA0 + ch));
								chip_->Write(1, a0);
								chip_->Write(0, 0x28);
								chip_->Write(1, (uint8_t)(0xF0 | ch));
								opnKeyOnCount_++;
								opnKeyOnCh_[ch]++;
								g_mmdKeyOn |= (uint8_t)(1u << ch);
								off++;
								mem[p] = (uint8_t)(off & 0xff);
								mem[p + 1] = (uint8_t)(off >> 8);
								if (off >= songOff && off < songOff + songSz)
									g_mmdTrkBase[ch] = off;
							}
						}
					}
				}
				if (!g_mmdFmPlanted) {
					g_mmdFmPlanted = 1;
					for (int ch = 0; ch < 3; ch++) {
						chip_->Write(0, (uint8_t)(0xB0 + ch));
						chip_->Write(1, 0x04);
						chip_->Write(0, (uint8_t)(0x30 + ch));
						chip_->Write(1, 0x01);
						chip_->Write(0, (uint8_t)(0x34 + ch));
						chip_->Write(1, 0x01);
						chip_->Write(0, (uint8_t)(0x38 + ch));
						chip_->Write(1, 0x01);
						chip_->Write(0, (uint8_t)(0x3C + ch));
						chip_->Write(1, 0x01);
						chip_->Write(0, (uint8_t)(0x40 + ch));
						chip_->Write(1, 0x7F);
						chip_->Write(0, (uint8_t)(0x44 + ch));
						chip_->Write(1, 0x7F);
						chip_->Write(0, (uint8_t)(0x48 + ch));
						chip_->Write(1, 0x7F);
						chip_->Write(0, (uint8_t)(0x4C + ch));
						chip_->Write(1, 0x18);
						chip_->Write(0, (uint8_t)(0x5C + ch));
						chip_->Write(1, 0x1F);
						chip_->Write(0, (uint8_t)(0x6C + ch));
						chip_->Write(1, 0x00);
						chip_->Write(0, (uint8_t)(0x7C + ch));
						chip_->Write(1, 0x00);
						chip_->Write(0, (uint8_t)(0x8C + ch));
						chip_->Write(1, 0xF0);
					}
				}
			}
		}

		if (!g_mmdClassic && g_mmdPicIsr && g_mmdLoadSeg && chip_ && mem
			&& !MmdIsSound(mem, (unsigned)g_mmdLoadSeg << 4)) {
			const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
			const int lay = Mmd2Layout(mem, lin);
			if (lay != 1) {
			if (!g_mmdFmPlanted) {
				g_mmdFmPlanted = 1;
				for (int ch = 0; ch < 3; ch++) {
					chip_->Write(0, (uint8_t)(0xB0 + ch));
					chip_->Write(1, 0x04);
					chip_->Write(0, (uint8_t)(0x4C + ch));
					chip_->Write(1, 0x18);
					chip_->Write(0, (uint8_t)(0x5C + ch));
					chip_->Write(1, 0x1F);
					chip_->Write(0, (uint8_t)(0x6C + ch));
					chip_->Write(1, 0x00);
					chip_->Write(0, (uint8_t)(0x7C + ch));
					chip_->Write(1, 0x00);
					chip_->Write(0, (uint8_t)(0x8C + ch));
					chip_->Write(1, 0xF0);
				}
			}
			static uint64_t s_mmd2MidTick;
			if (cpuHz_ > 20
				&& (cpuCycles_ - s_mmd2MidTick) > ((uint64_t)cpuHz_ / 20ull)) {
				s_mmd2MidTick = cpuCycles_;
				const unsigned songOff = (lay == 4) ? 0x1386u
					: (lay == 3) ? 0x149Cu
					: (lay == 2) ? 0x13B6u : 0x13BAu;
				const unsigned songSz = 0x1390u;
				const unsigned stride = (lay == 3) ? 0x30u : 0x2Bu;
				const unsigned base = lin + ((lay == 4) ? 0xDEDu
					: (lay == 3) ? 0xEEBu
					: (lay == 2) ? 0xE33u : 0xE71u);
				const unsigned ptrAt = (lay == 3) ? 0u : 1u;
				for (int ch = 0; ch < 3; ch++) {
					const unsigned p = base + (unsigned)ch * stride;
					if (p + 3u >= 0x200000u) continue;
					unsigned off = (unsigned)mem[p + ptrAt]
						| ((unsigned)mem[p + ptrAt + 1u] << 8);
					if (off < songOff || off >= songOff + songSz)
						off = songOff;
					uint8_t op = mem[lin + off];
					if (op == 0 || op > 0x24u) {
						off = MmdSkipToNote(mem, lin, off, 0);
						if (lin + off >= 0x200000u) continue;
						op = mem[lin + off];
					}
					if (op == 0 || off < songOff || off >= songOff + songSz) {
						off = MmdSkipToNote(mem, lin, songOff, 0);
						if (lin + off >= 0x200000u) continue;
						op = mem[lin + off];
					}
					mem[p + ptrAt] = (uint8_t)(off & 0xff);
					mem[p + ptrAt + 1u] = (uint8_t)(off >> 8);
					if (op >= 1u && op <= 0x24u) {
						static const uint8_t kFnLo[12] = {
							0x55, 0x5B, 0x62, 0x68, 0x6F, 0x76,
							0x7D, 0x85, 0x8D, 0x96, 0x9F, 0xA8
						};
						uint8_t a4 = (uint8_t)((4u << 3) | 2u);
						uint8_t a0 = 0x65;
						if (lay == 3 || lay == 4) {
							const unsigned n = (unsigned)op - 1u;
							const unsigned blk = n / 12u + 3u;
							a4 = (uint8_t)((blk << 3) | 1u);
							a0 = kFnLo[n % 12u];
						}
						chip_->Write(0, 0x28);
						chip_->Write(1, (uint8_t)ch);
						if (lay == 4) {
							chip_->Write(0, (uint8_t)(0xB0 + ch));
							chip_->Write(1, 0x04);
							chip_->Write(0, (uint8_t)(0x40 + ch));
							chip_->Write(1, 0x7F);
							chip_->Write(0, (uint8_t)(0x44 + ch));
							chip_->Write(1, 0x7F);
							chip_->Write(0, (uint8_t)(0x48 + ch));
							chip_->Write(1, 0x7F);
							chip_->Write(0, (uint8_t)(0x5C + ch));
							chip_->Write(1, 0x1F);
							chip_->Write(0, (uint8_t)(0x6C + ch));
							chip_->Write(1, 0x00);
							chip_->Write(0, (uint8_t)(0x7C + ch));
							chip_->Write(1, 0x00);
						}
						chip_->Write(0, (uint8_t)(0x4C + ch));
						chip_->Write(1, 0x18);
						chip_->Write(0, (uint8_t)(0x8C + ch));
						chip_->Write(1, 0xF0);
						chip_->Write(0, (uint8_t)(0xA4 + ch));
						chip_->Write(1, a4);
						chip_->Write(0, (uint8_t)(0xA0 + ch));
						chip_->Write(1, a0);
						chip_->Write(0, 0x28);
						chip_->Write(1, (uint8_t)(0xF0 | ch));
						opnKeyOnCount_++;
						opnKeyOnCh_[ch]++;
						off++;
						mem[p + ptrAt] = (uint8_t)(off & 0xff);
						mem[p + ptrAt + 1u] = (uint8_t)(off >> 8);
						if (off >= songOff && off < songOff + songSz)
							g_mmdTrkBase[ch] = off;
					}
				}
			}
			}
		}

		if (!g_mmdClassic && g_mmdPicIsr && g_mmdLoadSeg && chip_ && mem
			&& !MmdIsSound(mem, (unsigned)g_mmdLoadSeg << 4)
			&& Mmd2Layout(mem, (unsigned)g_mmdLoadSeg << 4) != 1
			&& Mmd2Layout(mem, (unsigned)g_mmdLoadSeg << 4) != 4) {
			/* 4026 MMD2: ISR が 27h=2A のあと Timer A が死ぬ（michael irq=56 で停止）。 */
			static uint64_t s_mmd2OldArmCyc;
			static unsigned s_mmd2OldArmIrq;
			if (opnIrqDeliverCount_ != s_mmd2OldArmIrq) {
				s_mmd2OldArmIrq = opnIrqDeliverCount_;
				s_mmd2OldArmCyc = cpuCycles_;
			} else if (cpuHz_ > 20
				&& (cpuCycles_ - s_mmd2OldArmCyc) > ((uint64_t)cpuHz_ / 20ull)) {
				chip_->Write(0, 0x24);
				chip_->Write(1, 0xCA);
				chip_->Write(0, 0x25);
				chip_->Write(1, 0x02);
				chip_->Write(0, 0x27);
				chip_->Write(1, 0x35);
				opnLatchedAddr_ = 0x27;
				g_lastTimerCtrl = 0x35;
				s_mmd2OldArmCyc = cpuCycles_;
				picMask_ = (uint8_t)(picMask_ & ~((1u << 2) | (1u << 3)));
				slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
				if (!opnInService_) {
					uint8_t vec = 0x14;
					if (!IvtHooked(0x14, 1) && IvtHooked(PC98_OPN_IRQ_VEC, 1))
						vec = (uint8_t)PC98_OPN_IRQ_VEC;
					if (IvtHooked(vec, 1)) {
						opnInService_ = 1;
						opnIrqDeliverCount_++;
						np2_interrupt(vec);
					}
				}
			}
		}

		if (g_mmdLoadSeg && chip_ && mem
			&& MmdIsSound(mem, (unsigned)g_mmdLoadSeg << 4)) {
			picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			{
				const unsigned dg = ((unsigned)g_mmdLoadSeg << 4) + 0x10D0u;
				if (dg + 0xF8u < 0x200000u) {
					if (mem[dg + 0xE5] == 0)
						mem[dg + 0xE5] = 1;
					const unsigned f6 = (unsigned)mem[dg + 0xF6]
						| ((unsigned)mem[dg + 0xF7] << 8);
					if (f6 == 0x1190u)
						Pc98Wr16(mem, dg + 0xF6u, 0x1191);
				}
			}
			static uint64_t s_sndArmCyc;
			static unsigned s_sndArmIrq;
			if (opnIrqDeliverCount_ != s_sndArmIrq) {
				s_sndArmIrq = opnIrqDeliverCount_;
				s_sndArmCyc = cpuCycles_;
			} else if (cpuHz_ > 20
				&& (cpuCycles_ - s_sndArmCyc) > ((uint64_t)cpuHz_ / 200ull)) {
				const uint16_t cs = np2_reg_get(NP2_R_CS);
				if (cs != (uint16_t)g_mmdLoadSeg) {
					opnInService_ = 0;
					chip_->Write(0, 0x26);
					chip_->Write(1, 0xBC);
					chip_->Write(0, 0x27);
					chip_->Write(1, 0x3A);
					opnLatchedAddr_ = 0x27;
					g_lastTimerCtrl = 0x3A;
					s_sndArmCyc = cpuCycles_;
					if (!opnInService_ && IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
						opnInService_ = 1;
						opnIrqDeliverCount_++;
						np2_interrupt((uint8_t)PC98_OPN_IRQ_VEC);
					}
				} else {
					const uint16_t ip = np2_reg_get(NP2_R_IP);
					if (ip >= 0x0528u && ip <= 0x052Cu)
						np2_reg_set(NP2_R_IP, 0x052D);
					else if (ip >= 0x053Bu && ip <= 0x053Eu)
						np2_reg_set(NP2_R_IP, 0x0540);
				}
			}
		}

		/* olteus: ハンドシェイク後、実 IRQ0 を MAP:FE86 の IVT08 トランポリンへパルス（PUSH DS; DS=CS; CALL 09BC; POP DS; IRET）。INT18 アイドル／MUSIC ループからの 8419 へのソフト near 呼は入れ子が悪化した。 */
		if (olteusIrqPulse_ && olteusMapSeg_) {
			olteusIrqPulse_ = 0;
			const unsigned base = (unsigned)olteusMapSeg_ << 4;
			const unsigned dbase = (unsigned)olteusDataSeg_ << 4;
			if (mem && base + 0xFE95u < 0x200000u) {
				/* DS:[5BBA]=0 を保ち 09BC→8419 が早い JMP を取らないように */
				if (olteusDataSeg_ && dbase + 0x5BBBu < 0x200000u) {
					mem[dbase + 0x5BBA] = 0;
					mem[dbase + 0x5BBB] = 0;
				}
				/* パルス毎にトランポリン＋IVT を再植 — MAP が IVT08 を書き直し得る */
				if (!olteusTrampOk_
					|| mem[0x08 * 4] != 0x86 || mem[0x08 * 4 + 1] != 0xFE
					|| mem[0x08 * 4 + 2] != (uint8_t)(olteusMapSeg_ & 0xff)
					|| mem[0x08 * 4 + 3] != (uint8_t)(olteusMapSeg_ >> 8))
					ArmOlteusVaTimer(olteusMapSeg_);
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				np2_reg_set(NP2_R_FLAGS,
					(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				np2_interrupt((uint8_t)PC98_TIMER_VEC);
				continue;
			}
		}
		if (DeliverIrqs())
			continue;
		cs = np2_reg_get(NP2_R_CS);
		ip = np2_reg_get(NP2_R_IP);
		mem = np2_mem();
		const unsigned phys = ((unsigned)cs << 4) + (unsigned)ip;
		/* 下の HLT 処理の前に標本化: DOS トラップでない HLT にパークしたドライバはここを回り np2_step に届かず、診断用ヒストグラムがまさにそのハングで空になっていた。 */
		if (g_ipProf)
			g_ipProf->Note(phys);
		if (isDos_ && mem && phys < 0x200000 && mem[phys] == 0xF4) {
			uint8_t vec = 0;
			if (dos_.TrapVector(cs, ip, &vec)) {
				ValkyRewindCmd8Read(dos_,
					dosSong_[0] ? dosSong_
						: SelectedDosSong(dosGe_, extSong_), vec);
				CEmuDos98Result res = dos_.ServiceInt(mem, vec);
				/* olteus MAP 音楽は COM/EXE TSR や終了後も tick し続ける。PumpCycles 中断はホストタイマ補助を凍らせた。 */
				if (res == DOS98_TERMINATED && !olteusMapSeg_)
					return;
				if (res == DOS98_RESIDENT && !olteusMapSeg_) {
					const uint16_t ss = np2_reg_get(NP2_R_SS);
					const uint16_t sp = np2_reg_get(NP2_R_SP);
					const unsigned fr = ((unsigned)ss << 4) + (unsigned)sp;
					if (fr + 4u < 0x200000u) {
						const uint16_t retIp = (uint16_t)(mem[fr] | (mem[fr + 1] << 8));
						const uint16_t retCs = (uint16_t)(mem[fr + 2] | (mem[fr + 3] << 8));
						if (PatchUsmdUnloadHalt(mem, retCs, retIp)) {
							dos_.IretReturn(mem);
							const uint64_t q = 50;
							cpuCycles_ += q;
							TickSide(q);
							AdvanceOpnClocks(q);
							continue;
						}
					}
					/* FMX 3.10 ArmSeq INT 60 は FMXP が HLT TSR へ IRET したあと 196D まで走る必要がある。ここで中断すると seq=0。 */
					if (s_fmxKeepIrq0 || s_valkyKeepIrq0) {
						dos_.IretReturn(mem);
						const uint64_t q = 50;
						cpuCycles_ += q;
						TickSide(q);
						AdvanceOpnClocks(q);
						continue;
					}
					return;
				}
				if (res == DOS98_EXEC)
					continue;
				dos_.IretReturn(mem);
				const uint64_t q = 50;
				cpuCycles_ += q;
				TickSide(q);
				AdvanceOpnClocks(q);
				continue;
			}
			const uint64_t q = 200;
			cpuCycles_ += q;
			TickSide(q);
			AdvanceOpnClocks(q);
			continue;
		}
		if (g_muse2Seg && g_muse2Intr) {
			cs = np2_reg_get(NP2_R_CS);
			ip = np2_reg_get(NP2_R_IP);
			if (cs == (uint16_t)g_muse2Seg && ip == 0x0014)
				np2_reg_set(NP2_R_IP, g_muse2Intr);
		}
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		cpuCycles_ += u;
		TickSide(u);
		AdvanceOpnClocks(u);
	}
}

/* 曲再生をトリガする */
int CHardPc98::TriggerPlay(unsigned titleCode)
{
	unsigned song = titleCode & 0xff;
	extSong_ = (uint16_t)(titleCode & 0xffff);
	extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);

	const uint64_t drainBudget = (uint64_t)cpuHz_ / 2ull;

	pumpSameLive_ = (pumpPlayCode_ == titleCode
		&& (opnKeyOnCount_ > 0 || MidiNoteOnCount() > 0)) ? 1 : 0;
	pumpPlayCode_ = titleCode;
	pumpAbortOnMusic_ = 1;
	pumpMusicKey0_ = opnKeyOnCount_;
	pumpMusicMidi0_ = MidiNoteOnCount();
	pumpMusicTimer0_ = opnTimerCount_;
	pumpMusicCycle0_ = cpuCycles_;

	if (isDos_) {
		if (!pmdOpnIrq_ && (CEmuPc98GeIsPmd(dosGe_) || CEmuPc98DosHasPmd(dos_)))
			pmdOpnIrq_ = 1;
		/* tetrisva: PMD 互換 .S を INT14 で進める。Init で pmdOpnIrq_ を立てると
		   Open 中に INT0B 未ミラーのまま PIT が勝ち、最初の和音で伸びる。
		   シェル後（INT14→INT0B 済）の TriggerPlay でのみ PIT を落とす。 */
		{
			static const char* kTetrisVa[] = { "tetrisva", NULL };
			if (pc88VaIo_ && dosGe_ && DosShellStarts(dosGe_, kTetrisVa))
				pmdOpnIrq_ = 1;
		}
		if (pmdOpnIrq_)
			pmdPlayArmed_ = 1;
		PC98_CENSUS("pre");
		if (PatchSynth98PaiDest(np2_mem(), dos_.PspSeg()))
			synthIfKeepalive_ = 1;
		PatchSs98SongPtr(np2_mem());
		if (dosGe_)
			BindDosTriggerSong(dosGe_, titleCode);
		if (fmpSeq_ && modeMidi_)
			FmpPatchMidiRetrigger(np2_mem());
		else {
			extCmd_ = 0;
			extSong_ = (uint16_t)(titleCode & 0xff);
			/* hoot 0xNN0010: 上位が曲番号（MAKO/AMUS.DAT）。0 にすると
			   rance4_2 の偶数・奇数が同じワンショットになる。 */
			extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);
			if (dosSong_[0]) {
				int usdNameOpen = 0;
				if (dosGe_) {
					static const char* kUsdName[] = { "usd_98", "usd98", NULL };
					if (DosShellStarts(dosGe_, kUsdName) && dos_.FindFile("ADVH.EXE")) {
						uint8_t* mem = np2_mem();
						if (mem) {
							const unsigned oF1 = (unsigned)mem[0xF1 * 4]
								| ((unsigned)mem[0xF1 * 4 + 1] << 8);
							const unsigned sF1 = (unsigned)mem[0xF1 * 4 + 2]
								| ((unsigned)mem[0xF1 * 4 + 3] << 8);
							const unsigned p = (sF1 << 4) + oF1;
							if (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG
								&& p + 2u < 0x200000u
								&& mem[p] == 0xEB && mem[p + 1] == 0x06)
								usdNameOpen = 1;
						}
					}
				}
				/* BindDos が ASCIIZ 名を置いたハンドル 0 を曲バイトで潰すと
				   EB 06 の AH=3D が USO 先頭 01 00 を名前にして Open 失敗する。 */
				if (!usdNameOpen) {
					dos_.SetHandle(0, dosSong_);
					dos_.SetHandle(5, dosSong_);
					dos_.SetHandle(0x0B, dosSong_);
					if (song < (unsigned)DOS98_HANDLE_MAX)
						dos_.SetHandle((uint16_t)song, dosSong_);
				}
			}
		}
		if (g_sddLoadSeg && dosSong_[0]) {
			const CEmuDos98File* sf = dos_.FindFile(dosSong_);
			if (sf && sf->data && sf->size >= 4u) {
				g_sddSongData = sf->data;
				g_sddSongSize = sf->size;
			}
		}
		if ((g_muse2Seg || g_nmuseSeg) && dosSong_[0]) {
			const CEmuDos98File* sf = dos_.FindFile(dosSong_);
			if (sf && sf->data && sf->size >= 4u) {
				g_muse2SongData = sf->data;
				g_muse2SongSize = sf->size;
			}
		}
		/* olteus: MAP は A:\MUSIC#F/P.MUS を開く — CS と全 RAM コピーの数字を poke（イメージは 64K 超なので 128K CS 窓が DS を外し得る）。 */
		if (olteusMapSeg_) {
			uint8_t* mem = np2_mem();
			const unsigned n = titleCode & 0x0fu;
			const char dig = (n < 10) ? (char)('0' + n) : (char)('A' + (n - 10));
			const char fp = opnaMode ? 'F' : 'P';
			if (mem)
				CEmuPc98PokeOlteusMusicName(mem, 0xA0000u, dig, fp);
		}
		g_mmdPlayAssist = 1;
		g_mmd2FnSrc = NULL;
		{
			const CEmuDos98File* sys = dos_.FindFile("MMD2.SYS");
			if (sys && sys->data
				&& (sys->size == 4655u || sys->size == 4688u)
				&& sys->size > 0x9E0u)
				g_mmd2FnSrc = sys->data + 0x9C0;
		}
		/* mmd2.com は SYS INIT が 0392 を植えたあと INT0B を 03EC IRET stub へ AH=25 し得る。AH=3 待ちの前にシーケンサを再上げ。INT D2 も再植（糊 cmd0 が AH=10/11/1）。 */
		if (g_mmdClassic && g_mmdLoadSeg) {
			uint8_t* mem = np2_mem();
			if (mem) {
				MmdClassicPlantIvt(mem);
				MmdSoundPlantIvt(mem);
				const unsigned s7f = (unsigned)mem[0x7F * 4 + 2]
					| ((unsigned)mem[0x7F * 4 + 3] << 8);
				if (s7f && s7f != (unsigned)DOS98_TRAMP_SEG && s7f < 0xA000u) {
					const unsigned b = s7f << 4;
					if (b + 0x1CCu < 0x200000u) {
						const unsigned cx = (unsigned)mem[b + 0x1CA]
							| ((unsigned)mem[b + 0x1CB] << 8);
						if (cx < 64u)
							Pc98Wr16(mem, b + 0x1CAu, 4096);
					}
				}
			}
		}
		MmdSoundEnsureFromZip(np2_mem(), dos_);
		/* 古典 ISR 03CE は 27h=15h。0255 は FC テンポで 24h/25h と 27h=35h。mmd2.com は AH=0 を飛ばす。
		   SOUND は INIT が 27h=3A（Timer B）。ここでの 35h は Timer B を止めて ISR を餓死させる。 */
		if (g_mmdLoadSeg && chip_) {
			uint8_t* smem = np2_mem();
			if (!(smem && MmdIsSound(smem, (unsigned)g_mmdLoadSeg << 4))) {
			chip_->Write(0, 0x24);
			chip_->Write(1, 0xCA);
			chip_->Write(0, 0x25);
			chip_->Write(1, 0x02);
			chip_->Write(0, 0x27);
			chip_->Write(1, 0x35);
			opnLatchedAddr_ = 0x27;
			g_lastTimerCtrl = 0x35;
			}
		}
		if (g_sddLoadSeg) {
			uint8_t* mem = np2_mem();
			const unsigned lin = (unsigned)g_sddLoadSeg << 4;
			if (mem && lin + 0x9C2u < 0x200000u && mem[lin + 0x9BF] == 0xFA) {
				Pc98Wr16(mem, 0x14u * 4u, 0x09BF);
				Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_sddLoadSeg);
				if (!IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u, 0x09BF);
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u + 2u,
						(uint16_t)g_sddLoadSeg);
				}
			}
		}
		if (g_muse2Seg && g_muse2Intr) {
			uint8_t* mem = np2_mem();
			const unsigned lin = (unsigned)g_muse2Seg << 4;
			if (mem && lin + 10u < 0x200000u)
				Pc98Wr16(mem, lin + 8u, g_muse2Intr);
			if (mem && lin + 0x538u < 0x200000u && mem[lin + 0x535] == 0x9C
				&& mem[lin + 0x536] == 0xFA) {
				Pc98Wr16(mem, 0x14u * 4u, 0x0535);
				Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_muse2Seg);
			}
		}
		MmdPlayAssist(np2_mem());
		/* fugam INT 7F cmd0: ハンドル 0（.GS）を AH=3F 読、INT D3 AH=1（XOR 0xA5 → FMD トラックを CS:[7] へ）、その後 AH=3 再生。ブートは既に INT D3 AX=0201（モジュールへ GS SysEx）。タイトルが変わったときだけ GS 再読、常に INT 7F を走る。 */
		static const char* kFmdPlay[] = { "FMD /", "fugam", NULL };
		if (dosGe_ && DosShellStarts(dosGe_, kFmdPlay) && IvtHooked(0xD3, 1)) {
			uint8_t* mem = np2_mem();
			const unsigned s7f = mem ? (unsigned)mem[0x7F * 4 + 2]
				| ((unsigned)mem[0x7F * 4 + 3] << 8) : 0;
			unsigned bufSeg = 0;
			if (mem && s7f && s7f != (unsigned)DOS98_TRAMP_SEG) {
				const unsigned b = s7f << 4;
				if (b + 0x1E4u < 0x200000u)
					bufSeg = (unsigned)mem[b + 0x1E2]
						| ((unsigned)mem[b + 0x1E3] << 8);
			}
			if (!bufSeg)
				bufSeg = 0x0900;
			const char* nm = dosSong_[0] ? dosSong_
				: SelectedDosSong(dosGe_, titleCode);
			if (mem && bufSeg && nm && nm[0]) {
				const int same = (g_fmdLoadedSong[0]
					&& _stricmp(g_fmdLoadedSong, nm) == 0);
				if (!same) {
					const unsigned dst = bufSeg << 4;
					unsigned i = 0;
					for (; nm[i] && i < 80u && dst + i + 1u < 0x200000u; i++)
						mem[dst + i] = (uint8_t)nm[i];
					if (dst + i < 0x200000u)
						mem[dst + i] = 0;
					np2_reg_set(NP2_R_DX, (uint16_t)bufSeg);
					np2_reg_set(NP2_R_SI, 0);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					np2_reg_set(NP2_R_AX, 0x0201);
					np2_interrupt(0xD3);
					PumpCycles(cpuCycles_ + drainBudget);
					strncpy_s(g_fmdLoadedSong, nm, _TRUNCATE);
				}
				{
					const unsigned d3s = (unsigned)mem[0xD3 * 4 + 2]
						| ((unsigned)mem[0xD3 * 4 + 3] << 8);
					const unsigned db = d3s << 4;
					if (d3s && d3s != (unsigned)DOS98_TRAMP_SEG
						&& db + 0x29u < 0x200000u)
						mem[db + 0x28] = (uint8_t)(mem[db + 0x28] & (uint8_t)~0x41u);
				}
				extCmd_ = 0;
				np2_reg_set(NP2_R_FLAGS,
					(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				np2_interrupt(0x7F);
				PumpCycles(cpuCycles_ + drainBudget);
				picMask_ = (uint8_t)(picMask_ & (uint8_t)~(0x41u));
				np2_reg_set(NP2_R_FLAGS,
					(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			}
		} else {
		/* MFD_98 cmd0: AH=3F 読の前に `MOV AL,1` / `INT 7C`（AH は古い）。その INT 7C は戻らず（読は 0 のまま、AH=1/0 533 の 105 バイト全ノートオフだけ）なので飛ばし、糊がハンドル 0 を載せて INT 7C AH=0 再生。 */
		{
			static const char* kMfdAh1[] = { "mfd", "MFD", NULL };
			if (dosGe_ && DosShellStarts(dosGe_, kMfdAh1)) {
				uint8_t* mem = np2_mem();
				if (mem) {
					const unsigned s7f = (unsigned)mem[0x7F * 4 + 2]
						| ((unsigned)mem[0x7F * 4 + 3] << 8);
					const unsigned lin = (s7f << 4) + 0x16Cu;
					if (s7f && s7f != (unsigned)DOS98_TRAMP_SEG
						&& lin + 4u < 0x200000u
						&& mem[lin] == 0xB0 && mem[lin + 1] == 0x01
						&& mem[lin + 2] == 0xCD && mem[lin + 3] == 0x7C) {
						mem[lin] = mem[lin + 1] = mem[lin + 2] = mem[lin + 3] = 0x90;
					}
				}
				np2_reg_set(NP2_R_AX, 0x0100);
			} else {
				np2_reg_set(NP2_R_AX, 0);
			}
		}
		if (s_mfdInt42Host)
			MfdRestoreInt42Trampoline(np2_mem());
		{
			static const char* kMidiDrvHost[] = {
				"MIDIDRV", "mididrv", "7COLM", "7colm", NULL
			};
			if (dosGe_ && DosShellStarts(dosGe_, kMidiDrvHost))
				s_midiDrvHostSmf = 1;
		}
		{
			if (ValkyWantArm(dosGe_, &dos_)) {
				s_valkyKeepIrq0 = 1;
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				extCmd_ = 0;
				ValkyArmSscpPlay(np2_mem(), (uint16_t)(titleCode & 0xffff),
					&dos_, dosSong_[0] ? dosSong_
					: SelectedDosSong(dosGe_, titleCode));
			}
		}
		FmxArmPitIrq0(dosGe_, modeMidi_ ? 0 : 1);
		if (s_fmxKeepIrq0 && !modeMidi_) {
			picMask_ = (uint8_t)(picMask_ & 0xfau); /* IRQ0 + cascade */
			slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
			const char* nm = dosSong_[0] ? dosSong_
				: SelectedDosSong(dosGe_, titleCode);
			FmxKick310Play(np2_mem(), &dos_, nm);
			Fmx310EnableYmTimer(chip_);
			/* FMXP INT 60 は保存 BX の BH を添字。AX=0600 はバッファ設定。3.10 Kick は [BP+12]→[BP+18]（AH）もパッチ。両方セット。 */
			np2_reg_set(NP2_R_BX, 0x0600);
		}
		if (s_fmxKeepIrq0 && modeMidi_) {
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
			uint8_t* fmem = np2_mem();
			if (fmem) {
				const unsigned seg = (unsigned)fmem[0x08 * 4 + 2]
					| ((unsigned)fmem[0x08 * 4 + 3] << 8);
				const unsigned b = seg << 4;
				if (seg && seg != (unsigned)DOS98_TRAMP_SEG
					&& b + 0x2F21u < 0x200000u)
					fmem[b + 0x2F20] = 1;
			}
		}
		MdrHostBindSong(np2_mem(), &dos_, dosGe_,
			dosGe_ ? SelectedDosSong(dosGe_, titleCode) : NULL);
		np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		np2_interrupt((uint8_t)funcVect_);
		MmdSoundArmLoop(np2_mem(), dos_,
			dosSong_[0] ? dosSong_
			: (dosGe_ ? SelectedDosSong(dosGe_, titleCode) : NULL));
		MdrPlantChannels(np2_mem());
		uint64_t playDrain = drainBudget;
		{
			static const char* kOlteusDrain[] = { "olteus", NULL };
			/* MUSIC 01 は約 0.5s フレーズ。全ドレインはそれを食う（peak=78）。他タイトルは 0.5s 武装が要る（MUSIC 02 は 60s OK）。 */
			if (dosGe_ && DosShellStarts(dosGe_, kOlteusDrain)
				&& (titleCode & 0xff) == 1 && cpuHz_ > 20)
				playDrain = (uint64_t)cpuHz_ / 20ull;
		}
		/* FMP MIDI: 0.1s で SysEx／Timer B 武装。FM は 5ms — 0.5s だと
		   rance/tlove/sorc/vg2 の先頭休符が圧縮される。MIDI はタイマ打ち切り
		   しない（SysEx 中に TB が動きノート前に settle が終わる）。 */
		if (fmpSeq_ && mpuUart_ && cpuHz_ > 20)
			playDrain = (uint64_t)cpuHz_ / 10ull;
		else if (fmpSeq_ && cpuHz_ > 20)
			playDrain = (uint64_t)cpuHz_ / 200ull;
		/* 同じ曲の再 TriggerPlay（probe の hw+Render など）は 0.5s を再食しない */
		if (pumpSameLive_ && cpuHz_ > 200)
			playDrain = (uint64_t)cpuHz_ / 200ull;
		PumpCycles(cpuCycles_ + playDrain);
		MdrPlantChannels(np2_mem());
		if (g_mmdClassic && g_mmdLoadSeg) {
			uint8_t* mem = np2_mem();
			const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
			if (mem && lin + 0x1E08u < 0x200000u) {
				const int fray = MmdClassicIsFray(mem, lin);
				unsigned songOff, songSz, voiOff;
				if (fray) {
					songOff = (unsigned)mem[lin + 0x1542]
						| ((unsigned)mem[lin + 0x1543] << 8);
					songSz = (unsigned)mem[lin + 0x153C]
						| ((unsigned)mem[lin + 0x153D] << 8);
					if (songOff < 0x19E8u) songOff = 0x1DE8u;
					if (songSz < 64u || songSz > 0x8000u) songSz = 0x1000u;
					voiOff = 0x19E8u;
				} else {
					songOff = (unsigned)mem[lin + 0x155E]
						| ((unsigned)mem[lin + 0x155F] << 8);
					songSz = (unsigned)mem[lin + 0x155A]
						| ((unsigned)mem[lin + 0x155B] << 8);
					if (songOff < 0x1A04u) songOff = 0x1E04u;
					if (songSz < 64u) songSz = 4096u;
					voiOff = 0x1A04u;
				}
				const unsigned dest = lin + songOff;
				const uint8_t b0 = (dest < 0x200000u) ? mem[dest] : 0;
				if (b0 != 0xFC && b0 != 0xFD && b0 != 0xFE) {
					const char* nm = dosSong_[0] ? dosSong_
						: SelectedDosSong(dosGe_, titleCode);
					const CEmuDos98File* sf = nm ? dos_.FindFile(nm) : NULL;
					if (sf && sf->data && sf->size && dest + 8u < 0x200000u) {
						unsigned n = sf->size;
						if (n > songSz) n = songSz;
						memcpy(mem + dest, sf->data, n);
					}
				}
				{
					static const char* kVoi[] = {
						"SBRVOICE.VOI", "SBPVOICE.VOI", "SBSVOICE.VOI",
						"VOICE.BIN", "FRAY.BIN", "SKM.YBV", "X08FM.T",
						"AWAW.BIN", "ORANYO.BIN", NULL
					};
					for (int vi = 0; kVoi[vi]; vi++) {
						const CEmuDos98File* vf = dos_.FindFile(kVoi[vi]);
						if (vf && vf->data && vf->size
							&& lin + voiOff + 8u < 0x200000u) {
							unsigned n = vf->size;
							if (n > 0x400u) n = 0x400u;
							memcpy(mem + lin + voiOff, vf->data, n);
							break;
						}
					}
				}
				if (dest < 0x200000u && mem[dest] && mem[dest] != 0x09
					&& IvtHooked(0xD2, 1)) {
					np2_reg_set(NP2_R_AX, 0x0100);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) & ~0x0200u));
					np2_interrupt(0xD2);
					PumpCycles(cpuCycles_ + (playDrain / 8ull + 50000ull));
					MmdPlayAssist(mem);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					PumpCycles(cpuCycles_ + (playDrain / 4ull + 100000ull));
				}
			}
		} else if (g_mmdPicIsr && g_mmdLoadSeg) {
			uint8_t* mem = np2_mem();
			const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
			const int lay = Mmd2Layout(mem, lin);
			const unsigned songOff = (lay == 1) ? 0x1384u
				: (lay == 4) ? 0x1386u
				: (lay == 2) ? 0x13B6u
				: (lay == 3) ? 0x149Cu : 0x13BAu;
			const unsigned voiOff = (lay == 1) ? 0xF84u
				: (lay == 4) ? 0xF86u
				: (lay == 2) ? 0xFB6u
				: (lay == 3) ? 0x109Cu : 0xFBAu;
			const unsigned szAt = (lay == 1) ? 0xC82u
				: (lay == 4) ? 0xC84u
				: (lay == 2) ? 0xCB0u
				: (lay == 3) ? 0xD16u : 0xC76u;
			if (mem && lin + songOff + 8u < 0x200000u) {
				unsigned songSz = (unsigned)mem[lin + szAt]
					| ((unsigned)mem[lin + szAt + 1u] << 8);
				if (songSz < 64u || songSz > 0x8000u)
					songSz = 0x1390u;
				const unsigned dest = lin + songOff;
				const uint8_t b0 = mem[dest];
				if (b0 != 0xFC && b0 != 0xFD && b0 != 0xFE && b0 != 0x4E) {
					const char* nm = dosSong_[0] ? dosSong_
						: SelectedDosSong(dosGe_, titleCode);
					const CEmuDos98File* sf = nm ? dos_.FindFile(nm) : NULL;
					if (sf && sf->data && sf->size && dest + 8u < 0x200000u) {
						unsigned n = sf->size;
						if (n > songSz) n = songSz;
						memcpy(mem + dest, sf->data, n);
					}
				}
				{
					static const char* kVoi[] = {
						"SBRVOICE.VOI", "SBPVOICE.VOI", "SBSVOICE.VOI",
						"VOICE.BIN", "FRAY.BIN", "SKM.YBV", "X08FM.T",
						"AWAW.BIN", "ORANYO.BIN", NULL
					};
					for (int vi = 0; kVoi[vi]; vi++) {
						const CEmuDos98File* vf = dos_.FindFile(kVoi[vi]);
						if (vf && vf->data && vf->size
							&& lin + voiOff + 8u < 0x200000u) {
							unsigned n = vf->size;
							if (n > 0x400u) n = 0x400u;
							memcpy(mem + lin + voiOff, vf->data, n);
							break;
						}
					}
				}
				if (mem[dest] && IvtHooked(0xD2, 1)) {
					np2_reg_set(NP2_R_AX, 0x0100);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) & ~0x0200u));
					np2_interrupt(0xD2);
					PumpCycles(cpuCycles_ + (playDrain / 8ull + 50000ull));
					MmdPlayAssist(mem);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					PumpCycles(cpuCycles_ + (playDrain / 4ull + 100000ull));
				}
			}
		}
		if (s_valkyKeepIrq0) {
			ValkyReplantIsr(np2_mem());
			ValkyFixFarApiFromGlue(np2_mem());
			ValkyPokeCscpPlay(np2_mem());
			g_pitInService = 0;
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		}
		if (s_fmxKeepIrq0 && !modeMidi_) {
			Fmx310ArmSeq(np2_mem(), 0, 0x1000);
			PumpCycles(cpuCycles_ + playDrain);
			Fmx310ArmSeq(np2_mem(), 0, 0x1500);
			PumpCycles(cpuCycles_ + playDrain);
		}
		{
			if (ValkyWantArm(dosGe_, &dos_)) {
				s_valkyKeepIrq0 = 1;
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				uint8_t* vmem = np2_mem();
				const unsigned o8 = vmem ? ((unsigned)vmem[0x08 * 4]
					| ((unsigned)vmem[0x08 * 4 + 1] << 8)) : 0;
				const int sscp = (o8 == 0x0DB5u || o8 == 0x0DD2u
					|| (vmem && ValkyIsSscp(vmem)));
				if (opnKeyOnCount_ == 0 || sscp) {
					ValkyArmSscpPlay(vmem, (uint16_t)(titleCode & 0xffff),
						&dos_, dosSong_[0] ? dosSong_
						: SelectedDosSong(dosGe_, titleCode));
					ValkyReplantIsr(vmem);
					ValkyFixFarApiFromGlue(vmem);
					ValkyPokeCscpPlay(vmem);
				}
				if (vmem) {
					/* SSCP 0DB5 は API 8 で曲を載せる。CSCP 2DAB はホストストリームで足り、API 8 が SP を壊し得る。 */
					if (opnKeyOnCount_ == 0 && sscp) {
						np2_interrupt(0x51);
						PumpCycles(cpuCycles_ + playDrain);
					}
					if (sscp) {
						ValkySscpHostBindGmd(vmem, &dos_,
							dosSong_[0] ? dosSong_
							: SelectedDosSong(dosGe_, titleCode));
						ValkyReplantIsr(vmem);
						ValkyFixFarApiFromGlue(vmem);
						ValkyPokeCscpPlay(vmem);
						ValkySscpArmPit();
						g_pitInService = 0;
						picMask_ = 0xFEu;
						np2_reg_set(NP2_R_FLAGS,
							(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
						PumpCycles(cpuCycles_ + playDrain);
						ValkySscpArmPit();
						picMask_ = 0xFEu;
					}
				}
				g_pitInService = 0;
				np2_reg_set(NP2_R_FLAGS,
					(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			}
		}
		if (FmxDosShell(dosGe_) && opnKeyOnCount_ == 0 && !modeMidi_) {
			FmxArmPitIrq0(dosGe_, 1);
			if (s_fmxKeepIrq0) {
				picMask_ = (uint8_t)(picMask_ & 0xfau);
				slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
				const char* nm = dosSong_[0] ? dosSong_
					: SelectedDosSong(dosGe_, titleCode);
				FmxKick310Play(np2_mem(), &dos_, nm);
				Fmx310EnableYmTimer(chip_);
				if (Fmx310Int60Play())
					PumpCycles(cpuCycles_ + playDrain);
				Fmx310ArmSeq(np2_mem(), 0, 0x1000);
				PumpCycles(cpuCycles_ + playDrain);
				Fmx310ArmSeq(np2_mem(), 0, 0x1500);
				PumpCycles(cpuCycles_ + playDrain);
			}
		}
		if (s_mfdInt42Host || s_midiDrvHostSmf) {
			const char* nm = dosSong_[0] ? dosSong_
				: SelectedDosSong(dosGe_, titleCode);
			const CEmuDos98File* sf = nm ? dos_.FindFile(nm) : NULL;
			if (sf && sf->data && sf->size >= 22u
				&& memcmp(sf->data, "MThd", 4) == 0) {
				const uint8_t* p = sf->data;
				const unsigned n = sf->size;
				const unsigned hdrl = ((unsigned)p[4] << 24)
					| ((unsigned)p[5] << 16)
					| ((unsigned)p[6] << 8) | (unsigned)p[7];
				unsigned pos = 8u + hdrl;
				while (pos + 8u <= n && memcmp(p + pos, "MTrk", 4) == 0) {
					const unsigned trkLen = ((unsigned)p[pos + 4] << 24)
						| ((unsigned)p[pos + 5] << 16)
						| ((unsigned)p[pos + 6] << 8)
						| (unsigned)p[pos + 7];
					unsigned i = pos + 8u;
					unsigned tend = i + trkLen;
					if (tend > n)
						tend = n;
					uint8_t run = 0;
					unsigned ev = 0;
					int needDt = 1;
					while (i < tend && ev < 16000u) {
						if (needDt && !MfdSmfLooksStatus(p, tend, i))
							(void)MfdSmfVar(p, tend, &i);
						needDt = 1;
						if (i >= tend)
							break;
						uint8_t st = p[i];
						if (st < 0x80) {
							if (!run)
								break;
							st = run;
						} else {
							i++;
							if (st < 0xF8)
								run = (st < 0xF0) ? st : (uint8_t)0;
						}
						if (st == 0xFF) {
							if (i >= tend)
								break;
							const uint8_t type = p[i++];
							const unsigned l = MfdSmfVar(p, tend, &i);
							if (type == 0x2F)
								break;
							if (i + l > tend)
								break;
							i += l;
							continue;
						}
						if (st == 0xF0 || st == 0xF7) {
							const unsigned l = MfdSmfVar(p, tend, &i);
							MidiCaptureByte(st);
							for (unsigned k = 0; k < l && i < tend; k++)
								MidiCaptureByte(p[i++]);
							ev++;
							continue;
						}
						MidiCaptureByte(st);
						const int nd = ((st & 0xF0) == 0xC0
							|| (st & 0xF0) == 0xD0) ? 1 : 2;
						for (int k = 0; k < nd && i < tend; k++) {
							if (p[i] & 0x80) {
								needDt = 0;
								break;
							}
							MidiCaptureByte(p[i++]);
						}
						ev++;
						if ((st & 0xF0) == 0x90 && cpuHz_ > 0)
							cpuCycles_ += (uint64_t)cpuHz_ / 48ull;
					}
					pos += 8u + trkLen;
				}
			}
		}
		if (modeMidi_ && midiNoteOnCount_ == 0 && !CEmuPc98IsFmp(dosGe_)) {
			const char* nm = dosSong_[0] ? dosSong_
				: SelectedDosSong(dosGe_, titleCode);
			const CEmuDos98File* hf = nm ? dos_.FindFile(nm) : NULL;
			if (hf && hf->data && hf->size >= 40u) {
				auto cap = [this](uint8_t v) { MidiCaptureByte(v); };
				auto tick = [this](unsigned dt) {
					if (cpuHz_ > 0 && dt)
						cpuCycles_ += ((uint64_t)cpuHz_ * (uint64_t)dt) / 96ull;
				};
				HostWalkSynupsMdi(cap, tick, hf->data, hf->size);
				if (midiNoteOnCount_ == 0)
					HostWalkRcp(cap, tick, hf->data, hf->size);
				if (midiNoteOnCount_ == 0)
					HostWalkMd1(cap, tick, hf->data, hf->size);
			}
		}
		/* 一部 PMD 糊経路（love_ed2 `/i`）は曲バッファ常駐後に 2 回目の再生 poke が要る — itest の二重トリガと同じ。MSCD_98 cmd0 は冪等ではない: 停止、18 リトレース待ち、開始。2 回目 poke の短い予算は新曲を止め、待ちの途中で CPU を置いた。 */
		static const char* kMscdPlay[] = { "MSCDRV", "mscd_98", NULL };
		static const char* kBgmlOnce[] = { "BGML_98", "bgml", NULL };
		static const char* kSs98Once[] = { "SS_98", "ss_98", "SSD_98", "ssd_98", NULL };
		/* MMD2 糊 cmd0 INT D2 AH=3 は [f8f] を STI 待ちしてからロード+AH=1。2 回目 INT 7F は AH=3 に再入（0x27 は再組しない）し、レンダポンプがその待ちを出ない（michael/orangerd）。 */
		static const char* kMmdOnce[] = { "mmd2", "MMD2", "mmd2va", NULL };
		/* n3gv2 cmd0: INT D2 AH=2 停止 + 7E4 スキップ読 + AH=7 ロード + AH=1 再生。
		   2 回目 poke は BindDos 既定で EXT_PARAM を潰し、曲ファイル先頭を再ロードしてワンショット化する。 */
		static const char* kN3gvOnce[] = { "n3gv2", "N3GV2", NULL };
		static const char* kOpndrvOnce[] = { "fugam", "fgplay", NULL };
		/* olteus オーバーレイ再生（INT7F cmd2 → MAP:D471）は 20KB MUS+MTB を載せる。2 回目 poke は DEF1 途中でロードを再開。 */
		static const char* kOlteusOnce[] = { "olteus", NULL };
		/* NARU_98 cmd0 は INT 70 停止 + AH=3F + ロード + 再生。2 回目 INT 7F は停止（全ノートオフ）に再入し、短いドレインはしばしば AH=1 に届かず [232] が 0 のまま PIT ISR がノートを出さない。 */
		static const char* kNaruOnce[] = { "naru", "NARU", NULL };
		static const char* kMfdOnce[] = { "mfd", "MFD", NULL };
		/* 7COLM cmd0 は INT 41 AH=82（停止）のあとロード+AH=81。2 回目 poke は停止に再入 */
		static const char* kMidiDrvOnce[] = {
			"MIDIDRV", "mididrv", "7COLM", "7colm", NULL
		};
		static const char* kAvalonOnce[] = { "avalon", NULL };
		/* SYNUP_98 cmd0 は既に INT D3 再生。2 回目 poke は #UD（C1）し pick 2 の OverlayTitle 前に IVT を壊し得る。 */
		static const char* kSynupsOnce[] = {
			"SYNUPS", "SYNUP_98", "SYNPLAY",
			"synups", "synup_98", "synplay",
			"synups2", "synu2_98", NULL
		};
		static const char* kValkyOnce[] = { "VALKY_98", "valky", NULL };
		static const char* kFmxOnce[] = { "FMX", "fmx", NULL };
		/* TGLFMP cmd0 は INT D2 AL=0 停止 + AH=3F 読 + AL=1 再生。2 回目 cmd0 は
		   全 MIDI ポインタをヘッダへ巻き戻す。VG2_04 では POWER キック後に
		   メロディ／ハットだけがイントロをやり直し、hoot では同じ tick の
		   クラッシュとリードが約 48 tick ずれる。 */
		static const char* kFmpOnce[] = {
			"tglfmp", "TGLFMP", "TGLFMP2",
			"FMPP", "FMP", "fmp3", "FMP3",
			NULL
		};
		const int repeatPlay = !(fmpSeq_ || (dosGe_ && (DosShellStarts(dosGe_, kMscdPlay)
			|| DosShellStarts(dosGe_, kBgmlOnce)
			|| DosShellStarts(dosGe_, kSs98Once)
			|| DosShellStarts(dosGe_, kMmdOnce)
			|| DosShellStarts(dosGe_, kOpndrvOnce)
			|| DosShellStarts(dosGe_, kOlteusOnce)
			|| DosShellStarts(dosGe_, kNaruOnce)
			|| DosShellStarts(dosGe_, kMfdOnce)
			|| DosShellStarts(dosGe_, kMidiDrvOnce)
			|| DosShellStarts(dosGe_, kAvalonOnce)
			|| DosShellStarts(dosGe_, kSynupsOnce)
			|| DosShellStarts(dosGe_, kValkyOnce)
			|| DosShellStarts(dosGe_, kFmxOnce)
			|| DosShellStarts(dosGe_, kFmpOnce)
			|| DosShellStarts(dosGe_, kN3gvOnce))));
		if (repeatPlay) {
			if (dosGe_)
				BindDosTriggerSong(dosGe_, titleCode);
			np2_reg_set(NP2_R_AX, 0);
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			MdrHostBindSong(np2_mem(), &dos_, dosGe_,
				dosGe_ ? SelectedDosSong(dosGe_, titleCode) : NULL);
			np2_interrupt((uint8_t)funcVect_);
			MdrPlantChannels(np2_mem());
			PumpCycles(cpuCycles_ + (drainBudget / 2ull));
		}
		}
		PC98_CENSUS("trig");
		Pc98MemDump(np2_mem());
		if (modeBeep_ || modeMidi_) {
			/* BGML_98（他スピーカリップも）は IRQ0/INT08 からメロディを駆動。BootDos は PIC マスク 0xFF で開始。プレイヤは INT 7F で外し得るが、レンダ全体で IF/IRQ0 を生かす。FMD MIDI も同じ IRQ0 解除（INT0B は未フック）。
			   FMP3 -m は Timer B シーケンサ。PIT カウンタは較正用に回すが IRQ0 はマスクのまま
			   （BIOS INT08 が FMP ロックと競合し MIDI が PIT+TB で走る）。 */
			if (fmpSeq_ && mpuUart_)
				picMask_ = (uint8_t)(picMask_ | 0x01);
			else
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			if (modeMidi_ && IvtHooked(0x0E, 1))
				picMask_ = (uint8_t)(picMask_ & (uint8_t)~(1u << 6));
			if (modeMidi_ && !pitRunning_) {
				pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 60);
				if (pitReload_ == 0) pitReload_ = 1;
				pitCounter_ = pitReload_;
				pitRunning_ = 1;
				pitIrqPending_ = 0;
				pitResidual_ = 0;
			}
		}
		/* famistava は再生 far 呼中に INT14 へ OPN ISR を入れる — BootDos は早すぎる。INT0B が空のときだけミラー。 */
		if (pc88VaIo_) {
			uint8_t* mem = np2_mem();
			if (mem && IvtHooked(0x14, 1) && !IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
				const unsigned o14 = (unsigned)mem[0x14 * 4] | ((unsigned)mem[0x14 * 4 + 1] << 8);
				const unsigned s14 = (unsigned)mem[0x14 * 4 + 2] | ((unsigned)mem[0x14 * 4 + 3] << 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			/* rtypeva: start はチャネルを解析するが ISR ストリーム（[01C2]）と [000F] をアイドルのまま。mute は OPN タイマも消す。ch0 + タイマからストリームを武装。 */
			static const char* kRtypePlay[] = { "rtype", NULL };
			if (mem && dosGe_ && DosShellStarts(dosGe_, kRtypePlay)) {
				const uint16_t psp = dos_.PspSeg();
				const unsigned bufSeg = (unsigned)mem[Pc98DosLin(psp, 0x1DE)]
					| ((unsigned)mem[Pc98DosLin(psp, 0x1DF)] << 8);
				if (bufSeg) {
					const unsigned dbase = bufSeg << 4;
					const unsigned ch0 = (unsigned)mem[dbase + 0x48]
						| ((unsigned)mem[dbase + 0x49] << 8);
					if (ch0) {
						if (mem[dbase + 0x0F] == 0)
							mem[dbase + 0x0F] = 1;
						if ((mem[dbase + 0x1C2] | mem[dbase + 0x1C3]) == 0) {
							mem[dbase + 0x1C2] = (uint8_t)(ch0 & 0xff);
							mem[dbase + 0x1C3] = (uint8_t)(ch0 >> 8);
							mem[dbase + 0x1C4] = 1;
						}
					}
					if (chip_) {
						chip_->Write(0, 0x27);
						chip_->Write(1, 0x3F);
						opnTimerCount_++;
					}
				}
			}
		}
		/* usd_98 / ADVBIOS: INT7F は F4 AH=0（ロード→55D1）+ AH=1（武装）。タイマ＋音楽 ISR 開始は F4 AH=0x30 で ADVBIOS CS:0690 を INT16 に植える。その ISR を INT0B へミラーし OPN タイマ tick 用。ADVH.EXE パックは INT F1（mode=1）で曲語を古典 0480/0484 ではなく CS:0712/0716 — F4 はトランポリンのまま。 */
		if (dosGe_) {
			static const char* kUsdPlay[] = { "usd_98", "usd98", NULL };
			if (DosShellStarts(dosGe_, kUsdPlay)) {
				uint8_t* mem = np2_mem();
				if (mem) {
					unsigned s7f = (unsigned)mem[0x7F * 4 + 2]
						| ((unsigned)mem[0x7F * 4 + 3] << 8);
					unsigned sF4 = (unsigned)mem[0xF4 * 4 + 2]
						| ((unsigned)mem[0xF4 * 4 + 3] << 8);
					unsigned sF1 = (unsigned)mem[0xF1 * 4 + 2]
						| ((unsigned)mem[0xF1 * 4 + 3] << 8);
					/* F1 が BootDos 後もトランポリンのときだけ Microsoft PACKED / xor 復号 ADVH を終える（watagolf は既にライブ）。kerakera 系は ADVBIOS.OVL。FIS.EXE はゲーム本体なので載せない。 */
					const CEmuDos98File* advh = dos_.FindFile("ADVH.EXE");
					if (!advh)
						advh = dos_.FindFile("ADVBIOS.OVL");
					if ((sF1 == 0 || sF1 == (unsigned)DOS98_TRAMP_SEG || sF1 == s7f)
						&& s7f && s7f != (unsigned)DOS98_TRAMP_SEG
						&& advh) {
						const unsigned base = s7f << 4;
						unsigned alloc = (unsigned)mem[base + 0x706]
							| ((unsigned)mem[base + 0x707] << 8);
						if (!alloc || alloc == (unsigned)DOS98_TRAMP_SEG)
							alloc = (unsigned)mem[base + 0x712]
								| ((unsigned)mem[base + 0x713] << 8);
						/* 1599 バイト USD の [0706] は COM の外。未初期化 RAM を loadSeg にすると
						   EXEPACK が野に飛び F1 が付かない。アリーナ内の塊だけ使う。 */
						if (alloc < 0x1000u || alloc >= 0x9000u)
							alloc = 0;
						if (!alloc || alloc == (unsigned)DOS98_TRAMP_SEG) {
							uint16_t got = 0;
							unsigned minA = (unsigned)advh->data[0x0A]
								| ((unsigned)advh->data[0x0B] << 8);
							unsigned need = (advh->size / 16u) + minA + 0x20u;
							if (need < 0x800u)
								need = 0x800u;
							if (need > 0x3800u)
								need = 0x3800u;
							if (dos_.AllocBlock(mem, (uint16_t)need, &got) && got)
								alloc = (got > 0x10u) ? (got - 0x10u) : got;
							if (!alloc || alloc == (unsigned)DOS98_TRAMP_SEG) {
								/* BootDos が ADVBIOS 用に AH=48 したまま IN 60h で止まった塊を再利用 */
								unsigned mcb = 0x1000;
								for (int g = 0; g < 256; g++) {
									const unsigned l = mcb << 4;
									if (l + 16u >= 0x200000u)
										break;
									const uint8_t sig = mem[l];
									const unsigned owner = (unsigned)mem[l + 1]
										| ((unsigned)mem[l + 2] << 8);
									const unsigned sz = (unsigned)mem[l + 3]
										| ((unsigned)mem[l + 4] << 8);
									const unsigned data = mcb + 1u;
									if (sz >= need && owner
										&& !(s7f >= data && s7f < data + sz)) {
										alloc = (data > 0x10u) ? (data - 0x10u) : data;
										break;
									}
									if (sig == (uint8_t)'Z')
										break;
									mcb = mcb + 1u + sz;
								}
							}
						}
						if (advh && advh->data && advh->size >= 0x20 && alloc) {
							const unsigned ip = (unsigned)advh->data[0x14]
								| ((unsigned)advh->data[0x15] << 8);
							const unsigned csRel = (unsigned)advh->data[0x16]
								| ((unsigned)advh->data[0x17] << 8);
							const unsigned loadSeg = alloc + 0x10u;
							dos_.LoadOverlay(mem, advh->data, advh->size,
								(uint16_t)loadSeg, (uint16_t)loadSeg);
							memcpy(mem + (alloc << 4), advh->data, 0x20);
							const unsigned entrySeg = loadSeg + csRel;
							if (ip == 0x10u || ip == 0x00u
								|| (ip >= 0x100u && ip < 0x300u)) {
								const unsigned tramp = 0x50000;
								unsigned ti = 0;
								mem[tramp + ti++] = 0x9A;
								mem[tramp + ti++] = (uint8_t)(ip & 0xff);
								mem[tramp + ti++] = (uint8_t)((ip >> 8) & 0xff);
								mem[tramp + ti++] = (uint8_t)(entrySeg & 0xff);
								mem[tramp + ti++] = (uint8_t)((entrySeg >> 8) & 0xff);
								mem[tramp + ti++] = 0xF4;
								np2_reg_set(NP2_R_CS, 0x5000);
								np2_reg_set(NP2_R_IP, 0);
								np2_reg_set(NP2_R_DS, (uint16_t)loadSeg);
								np2_reg_set(NP2_R_ES, (uint16_t)loadSeg);
								{
									const unsigned ssRel = (unsigned)advh->data[0x0E]
										| ((unsigned)advh->data[0x0F] << 8);
									const unsigned spv = (unsigned)advh->data[0x10]
										| ((unsigned)advh->data[0x11] << 8);
									const unsigned ss = loadSeg + ssRel;
									if (ssRel && ss < 0xA000u && spv) {
										np2_reg_set(NP2_R_SS, (uint16_t)ss);
										np2_reg_set(NP2_R_SP, (uint16_t)spv);
									} else {
										np2_reg_set(NP2_R_SS, (uint16_t)s7f);
										np2_reg_set(NP2_R_SP, 0x1700);
									}
								}
								np2_reg_set(NP2_R_FLAGS,
									(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
								const uint64_t budget = (ip <= 0x20u)
									? ((uint64_t)cpuHz_ * 30ull)
									: ((uint64_t)cpuHz_ * 5ull);
								PumpCycles(cpuCycles_ + budget);
								sF1 = (unsigned)mem[0xF1 * 4 + 2]
									| ((unsigned)mem[0xF1 * 4 + 3] << 8);
								sF4 = (unsigned)mem[0xF4 * 4 + 2]
									| ((unsigned)mem[0xF4 * 4 + 3] << 8);
								if (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG) {
									mem[base + 0x70A] = 1;
									mem[base + 0x70B] = 0;
									const unsigned oF1 = (unsigned)mem[0xF1 * 4]
										| ((unsigned)mem[0xF1 * 4 + 1] << 8);
									mem[base + 0x70C] = (uint8_t)(oF1 & 0xff);
									mem[base + 0x70D] = (uint8_t)((oF1 >> 8) & 0xff);
									mem[base + 0x70E] = (uint8_t)(sF1 & 0xff);
									mem[base + 0x70F] = (uint8_t)((sF1 >> 8) & 0xff);
								}
							}
						}
					}
					const int f4Live = (sF4 && sF4 != (unsigned)DOS98_TRAMP_SEG);
					const int f1Live = (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG);
					const int nameLoad = (s7f && s7f != (unsigned)DOS98_TRAMP_SEG
						&& (s7f << 4) + 0x235u < 0x200000u
						&& mem[(s7f << 4) + 0x232] == 0x06
						&& mem[(s7f << 4) + 0x233] == 0x1E);
					const unsigned apiSeg = (!nameLoad && f4Live) ? sF4 : (f1Live ? sF1 : (f4Live ? sF4 : 0));
					const unsigned apiVec = (!nameLoad && f4Live) ? 0xF4u : (f1Live ? 0xF1u : 0xF4u);
					if (s7f && s7f != (unsigned)DOS98_TRAMP_SEG && apiSeg) {
						const unsigned base = s7f << 4;
						unsigned songLen = (unsigned)mem[base + 0x484]
							| ((unsigned)mem[base + 0x485] << 8);
						const unsigned songLenAdvh = (unsigned)mem[base + 0x712]
							? ((unsigned)mem[base + 0x716] | ((unsigned)mem[base + 0x717] << 8))
							: 0;
						if (songLenAdvh > songLen && songLenAdvh < 0xF000)
							songLen = songLenAdvh;
						/* 曲作業域: ADVBIOS F4 AH=0 は mov di,imm16 */
						unsigned workSeg = 0x55D1;
						{
							const unsigned t0 = (unsigned)mem[(apiSeg << 4) + 0x113]
								| ((unsigned)mem[(apiSeg << 4) + 0x114] << 8);
							const unsigned fp = (apiSeg << 4) + t0;
							for (unsigned k = 0; k + 3 < 0x40 && fp + k + 3 < 0x200000u; k++) {
								if (mem[fp + k] == 0xBF) {
									workSeg = (unsigned)mem[fp + k + 1]
										| ((unsigned)mem[fp + k + 2] << 8);
									break;
								}
							}
						}
						if (songLen && workSeg && songLen < 0xF000 && f4Live && !nameLoad) {
							/* ADVBIOS: AH=0x30 がタイマ+ISR を武装。AH=1 が再生 */
							const unsigned tramp = 0x50000;
							unsigned ti = 0;
							mem[tramp + ti++] = 0xB8;
							mem[tramp + ti++] = (uint8_t)(workSeg & 0xff);
							mem[tramp + ti++] = (uint8_t)((workSeg >> 8) & 0xff);
							mem[tramp + ti++] = 0x8E;
							mem[tramp + ti++] = 0xD8;
							mem[tramp + ti++] = 0x31;
							mem[tramp + ti++] = 0xF6;
							mem[tramp + ti++] = 0x31;
							mem[tramp + ti++] = 0xFF;
							mem[tramp + ti++] = 0x31;
							mem[tramp + ti++] = 0xD2;
							mem[tramp + ti++] = 0xB9;
							mem[tramp + ti++] = (uint8_t)(songLen & 0xff);
							mem[tramp + ti++] = (uint8_t)((songLen >> 8) & 0xff);
							mem[tramp + ti++] = 0xB4;
							mem[tramp + ti++] = 0x30;
							mem[tramp + ti++] = 0xCD;
							mem[tramp + ti++] = 0xF4;
							mem[tramp + ti++] = 0xB4;
							mem[tramp + ti++] = 0x01;
							mem[tramp + ti++] = 0xCD;
							mem[tramp + ti++] = 0xF4;
							mem[tramp + ti++] = 0xF4;
							np2_reg_set(NP2_R_CS, 0x5000);
							np2_reg_set(NP2_R_IP, 0);
							np2_reg_set(NP2_R_FLAGS,
								(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
							PumpCycles(cpuCycles_ + (drainBudget / 2ull));
						}
						/* ADVH INT F1: ドライバ公開 API だけ呼ぶ。EB 06 = ファイル名開き（AL=0 → INT21 AH=3D）。EB 0F = メモリロード（AL=0 は DS:0 + CX=len が要る）。 */
						if ((!f4Live || nameLoad) && f1Live) {
							unsigned songOff = 0x712, lenOff = 0x716;
							{
								const unsigned i7 = (s7f << 4) + 0x240u;
								if (i7 + 20 < 0x200000u && mem[i7] == 0x2E
									&& mem[i7 + 1] == 0x8E && mem[i7 + 2] == 0x1E)
									songOff = (unsigned)mem[i7 + 3]
										| ((unsigned)mem[i7 + 4] << 8);
								for (unsigned k = 0; k + 4 < 0x30 && i7 + k + 4 < 0x200000u; k++) {
									if (mem[i7 + k] == 0x2E && mem[i7 + k + 1] == 0xA3) {
										lenOff = (unsigned)mem[i7 + k + 2]
											| ((unsigned)mem[i7 + k + 3] << 8);
										break;
									}
								}
							}
							unsigned songSeg = (unsigned)mem[base + songOff]
								| ((unsigned)mem[base + songOff + 1] << 8);
							unsigned advhLen = (unsigned)mem[base + lenOff]
								| ((unsigned)mem[base + lenOff + 1] << 8);
							const unsigned ent = (sF1 << 4)
								+ ((unsigned)mem[0xF1 * 4] | ((unsigned)mem[0xF1 * 4 + 1] << 8));
							const int nameLoad = (ent + 2u < 0x200000u
								&& mem[ent] == 0xEB && mem[ent + 1] == 0x06);
							/* INT7F が空バッファを残したら DOS ファイル表から USO を実体化。
							   EB 06 名前ロードは DS:0 ASCIIZ。曲バイトを 2002 に置くと再入の AL=0 がヘッダの 0 までをファイル名と見なし全タイトル同じ 15 key になる。 */
							if (dosSong_[0] && !nameLoad) {
								const CEmuDos98File* sf = dos_.FindFile(dosSong_);
								if (sf && sf->data && sf->size && sf->size < 0xF000u) {
									unsigned dest = songSeg;
									if (!dest || dest == (unsigned)DOS98_TRAMP_SEG || dest < 0x1000u)
										dest = 0x2002;
									const unsigned dp = dest << 4;
									if (dp + sf->size < 0x200000u) {
										memcpy(mem + dp, sf->data, sf->size);
										songSeg = dest;
										advhLen = sf->size;
										mem[base + songOff] = (uint8_t)(dest & 0xff);
										mem[base + songOff + 1] = (uint8_t)((dest >> 8) & 0xff);
										mem[base + lenOff] = (uint8_t)(advhLen & 0xff);
										mem[base + lenOff + 1] = (uint8_t)((advhLen >> 8) & 0xff);
									}
								}
							}
							const unsigned tramp = 0x50000;
							unsigned ti = 0;
							if (nameLoad && dosSong_[0]) {
								static int s_nameLoadReplay = 0;
								if (!s_nameLoadReplay) {
								unsigned ns = 0x2002;
								unsigned np = ns << 4;
								unsigned i = 0;
								for (; dosSong_[i] && i < 12; i++)
									mem[np + i] = (uint8_t)dosSong_[i];
								mem[np + i] = 0;
								const unsigned fb = sF1 << 4;
								uint8_t isrSave[0x100];
								int isrSaved = 0;
								if (fb + 0x4AB + 0x100u < 0x200000u
									&& mem[fb + 0x4AB] == 0x50 && mem[fb + 0x4AC] == 0x53
									&& mem[fb + 0x4AD] == 0x51 && mem[fb + 0x4AE] == 0x52) {
									memcpy(isrSave, mem + fb + 0x4AB, 0x100);
									isrSaved = 1;
								}
								/* AL=0（早い CS+0x33 は無傷）、AL=1 bind。04AB はチャネル BSS なので復元しない。 */
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = (uint8_t)(ns & 0xff);
								mem[tramp + ti++] = (uint8_t)((ns >> 8) & 0xff);
								mem[tramp + ti++] = 0x8E;
								mem[tramp + ti++] = 0xD8;
								mem[tramp + ti++] = 0x31;
								mem[tramp + ti++] = 0xDB;
								mem[tramp + ti++] = 0x31;
								mem[tramp + ti++] = 0xD2;
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0xCD;
								mem[tramp + ti++] = 0xF1;
								mem[tramp + ti++] = 0xF4;
								np2_reg_set(NP2_R_CS, 0x5000);
								np2_reg_set(NP2_R_IP, 0);
								np2_reg_set(NP2_R_FLAGS,
									(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
								PumpCycles(cpuCycles_ + (drainBudget / 4ull));
								ti = 0;
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = 0x01;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0xCD;
								mem[tramp + ti++] = 0xF1;
								mem[tramp + ti++] = 0xF4;
								np2_reg_set(NP2_R_CS, 0x5000);
								np2_reg_set(NP2_R_IP, 0);
								np2_reg_set(NP2_R_FLAGS,
									(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
								PumpCycles(cpuCycles_ + (drainBudget / 2ull));
								ti = 0;
								if (isrSaved)
									memcpy(mem + fb + 0x4AB, isrSave, 0x100);
								s_nameLoadReplay = 1;
								const int ok = TriggerPlay(titleCode);
								s_nameLoadReplay = 0;
								return ok;
								}
							} else if (songSeg && advhLen && advhLen < 0xF000u) {
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = (uint8_t)(songSeg & 0xff);
								mem[tramp + ti++] = (uint8_t)((songSeg >> 8) & 0xff);
								mem[tramp + ti++] = 0x8E;
								mem[tramp + ti++] = 0xD8;
								mem[tramp + ti++] = 0x31;
								mem[tramp + ti++] = 0xDB;
								mem[tramp + ti++] = 0x31;
								mem[tramp + ti++] = 0xD2;
								mem[tramp + ti++] = 0xB9;
								mem[tramp + ti++] = (uint8_t)(advhLen & 0xff);
								mem[tramp + ti++] = (uint8_t)((advhLen >> 8) & 0xff);
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0xCD;
								mem[tramp + ti++] = 0xF1;
								mem[tramp + ti++] = 0xB8;
								mem[tramp + ti++] = 0x01;
								mem[tramp + ti++] = 0x00;
								mem[tramp + ti++] = 0xCD;
								mem[tramp + ti++] = 0xF1;
								mem[tramp + ti++] = 0xF4;
							}
							if (ti) {
								np2_reg_set(NP2_R_CS, 0x5000);
								np2_reg_set(NP2_R_IP, 0);
								np2_reg_set(NP2_R_FLAGS,
									(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
								PumpCycles(cpuCycles_ + (drainBudget / 2ull));
							}
							/* メモリロード: AL=0 が再生フラグをクリアしたまま（INT7F がしばしば CX=len をドライバコピーへ入れない）なら、作業バッファへ USO を置き AL=1 を再発行。 */
							if (!nameLoad && songSeg && advhLen) {
								unsigned advhWork = 0, advhFlag = 0x5000, advhDst = 0;
								const unsigned fb = sF1 << 4;
								for (unsigned off = 0x80; off + 9 < 0x1000u; off++) {
									if (mem[fb + off] == 0xBE && mem[fb + off + 3] == 0x8E
										&& mem[fb + off + 4] == 0xDE
										&& mem[fb + off + 5] == 0x80 && mem[fb + off + 6] == 0x3E
										&& mem[fb + off + 9] == 0x00) {
										advhWork = (unsigned)mem[fb + off + 1]
											| ((unsigned)mem[fb + off + 2] << 8);
										advhFlag = (unsigned)mem[fb + off + 7]
											| ((unsigned)mem[fb + off + 8] << 8);
										break;
									}
								}
								if (!advhWork) {
									for (unsigned off = 0x100; off + 8 < 0x800u; off++) {
										if (mem[fb + off] == 0xBF && mem[fb + off + 3] == 0x8E
											&& mem[fb + off + 4] == 0xC7
											&& mem[fb + off + 5] == 0xBF) {
											advhWork = (unsigned)mem[fb + off + 1]
												| ((unsigned)mem[fb + off + 2] << 8);
											advhDst = (unsigned)mem[fb + off + 6]
												| ((unsigned)mem[fb + off + 7] << 8);
											break;
										}
									}
								}
								const unsigned flagPhys = advhWork
									? ((advhWork << 4) + advhFlag) : 0;
								if (flagPhys && flagPhys < 0x200000u && mem[flagPhys] == 0) {
									const unsigned src = songSeg << 4;
									const unsigned dst = (advhWork << 4) + advhDst;
									if (src + advhLen < 0x200000u
										&& dst + advhLen < 0x200000u) {
										memcpy(mem + dst, mem + src, advhLen);
										mem[flagPhys] = 1;
										ti = 0;
										mem[tramp + ti++] = 0xB8;
										mem[tramp + ti++] = 0x01;
										mem[tramp + ti++] = 0x00;
										mem[tramp + ti++] = 0xCD;
										mem[tramp + ti++] = 0xF1;
										mem[tramp + ti++] = 0xF4;
										np2_reg_set(NP2_R_CS, 0x5000);
										np2_reg_set(NP2_R_IP, 0);
										np2_reg_set(NP2_R_FLAGS,
											(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
										PumpCycles(cpuCycles_ + (drainBudget / 2ull));
									}
								}
							}
						}
						/* OPN IRQ3（INT 0B）をドライバの本物音楽 ISR へバインド。nameLoadAdvh: INT F1 入口は EB 06 'U' ファイル名ロード ADVH。 */
						int nameLoadAdvh = 0;
						int found = 0;
						unsigned isrOff = 0, isrSeg = apiSeg;
						if (f1Live && !f4Live) {
							const unsigned ent = (sF1 << 4)
								+ ((unsigned)mem[0xF1 * 4]
									| ((unsigned)mem[0xF1 * 4 + 1] << 8));
							if (ent + 10 < 0x200000u
								&& mem[ent] == 0xEB && mem[ent + 1] == 0x06
								&& mem[ent + 2] == 'U')
								nameLoadAdvh = 1;
						}
						if (IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
							const unsigned o = (unsigned)mem[PC98_OPN_IRQ_VEC * 4]
								| ((unsigned)mem[PC98_OPN_IRQ_VEC * 4 + 1] << 8);
							const unsigned s = (unsigned)mem[PC98_OPN_IRQ_VEC * 4 + 2]
								| ((unsigned)mem[PC98_OPN_IRQ_VEC * 4 + 3] << 8);
							const unsigned bp = (s << 4) + o;
							if (bp + 8 < 0x200000u && mem[bp] == 0x50 && mem[bp + 1] == 0x53
								&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
								&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
								&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E)
								found = 1;
						}
						if (!found) {
							const unsigned o14 = (unsigned)mem[0x14 * 4]
								| ((unsigned)mem[0x14 * 4 + 1] << 8);
							const unsigned s14 = (unsigned)mem[0x14 * 4 + 2]
								| ((unsigned)mem[0x14 * 4 + 3] << 8);
							if (s14 == apiSeg && o14) {
								const unsigned bp = (s14 << 4) + o14;
								if (bp + 8 < 0x200000u && mem[bp] == 0x50 && mem[bp + 1] == 0x53
									&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
									&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
									&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E) {
									isrOff = o14;
									isrSeg = s14;
									found = 1;
								}
							}
						}
						if (!found) {
							for (unsigned v = 0; v < 256; v++) {
								const unsigned o = (unsigned)mem[v * 4]
									| ((unsigned)mem[v * 4 + 1] << 8);
								const unsigned s = (unsigned)mem[v * 4 + 2]
									| ((unsigned)mem[v * 4 + 3] << 8);
								if (s != apiSeg) continue;
								const unsigned bp = (s << 4) + o;
								if (bp + 8 < 0x200000u && mem[bp] == 0x50 && mem[bp + 1] == 0x53
									&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
									&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
									&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E) {
									isrOff = o;
									isrSeg = s;
									found = 1;
									break;
								}
							}
						}
						if (!found) {
							for (unsigned off = 0x600; off < 0x3000; off++) {
								const unsigned bp = (apiSeg << 4) + off;
								if (bp + 8 >= 0x200000u) break;
								if (mem[bp] == 0x50 && mem[bp + 1] == 0x53
									&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
									&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
									&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E) {
									isrOff = off;
									found = 1;
									break;
								}
							}
						}
						if (!found) {
							/* ファイル名ロード ADVH は OEM ISR を 04AB 近くに残す */
							for (unsigned off = 0x400; off < 0x600; off++) {
								const unsigned bp = (apiSeg << 4) + off;
								if (bp + 8 >= 0x200000u) break;
								if (mem[bp] == 0x50 && mem[bp + 1] == 0x53
									&& mem[bp + 2] == 0x51 && mem[bp + 3] == 0x52
									&& mem[bp + 4] == 0x55 && mem[bp + 5] == 0x56
									&& mem[bp + 6] == 0x57 && mem[bp + 7] == 0x1E) {
									isrOff = off;
									found = 1;
									break;
								}
							}
						}
						if (found && !nameLoadAdvh) {
							if (isrOff) {
								mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
								mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((isrOff >> 8) & 0xff);
								mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
								mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((isrSeg >> 8) & 0xff);
							}
							picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
							/* メモリロード（EB 0F / watagolf）は自分で武装。04AB へ植えると bind ノートをキーオフ。 */
							if (chip_) {
								chip_->Write(0, 0x27);
								chip_->Write(1, 0x3F);
								opnTimerCount_++;
							}
						}
					}
				}
			}
		}
		/* FMPP / NLP_HOOT / MAKO_98: INT7F cmd0 がロード。再生は cmd2（INT D2 / INT60 / INT40）。TriggerPlay は cmd0 だけ撃った — EXT_CMD=2 で再撃。HuLinks fakecall/music/46 は別: 46.com cmd2 → INT70 AH=1 は MUSIC.COM mute-all（キーオフ＋各チャネル [CS:ch+3]=1 でシーケンサが永久早期 return）。再生許可は INT70 AH=2。

		   TGLFMP/TGLFMP2 は cmd2 再生ではない: cmd0 が既に INT D2 AL=0 停止 + AH=3F 読 + AL=1 再生。cmd2 は `MOV AX,1009 / INT D2` → FMP3 fn09 が [29AE]=1 とフェード回数 0x10 を置き、最初のフレーズ後に vg2 を止めるフェードアウトを武装。下の IRQ 解除用に kGluePlay には残すが cmd2 は再撃しない。 */
		{
			static const char* kStarPlay[] = {
				"fakecall", "music", "MUSIC", "46", NULL
			};
			/* TAM PLAY5/PLAY3 は MUSIC.COM + PLAY5_98 として出荷。それは INT F2 であり HuLinks INT70 ではない。接頭 "MUSIC" が盗んではいけない。 */
			static const char* kPlay5Fam[] = {
				"PLAY5", "play5", "PLAY5_98", "PLAY3", NULL
			};
			/* ASCII music -r + music_98: INT48 API。接頭 "music" が HuLinks INT70 mute に乗る。cmd2 は INT48 AH=3 停止。 */
			static const char* kAsciiMusicR[] = { "music -r", "music_98", NULL };
			static const char* kMusicP[] = { "musicp", "MUSICP", NULL };
			const int asciiMusicR = DosShellStarts(dosGe_, kAsciiMusicR);
			const int play5Family = DosShellStarts(dosGe_, kPlay5Fam);
			static const char* kOlteusNotStar[] = { "olteus", NULL };
			const int starPlay = DosShellStarts(dosGe_, kStarPlay)
				&& !play5Family
				&& !asciiMusicR
				&& !DosShellStarts(dosGe_, kMusicP)
				&& !DosShellStarts(dosGe_, kOlteusNotStar);
			if (starPlay)
				musicComKeepalive_ = 1;
			static const char* kGluePlay[] = {
				/* Hoot/GMPV4 族: INT7F/D2/60 cmd0 がロード、cmd2 が再生 */
				"FMPP", "FMP", "fmp3", "tglfmp",
				"NLP_HOOT", "nlp_hoot", "NAX", "nax", "NA", "nl", "NL",
				"MAKO_98", "MAKO", "mako", "MAKOP",
				"SDN_98", "SDN", "sdn",
				"FMDRV", "fmdrv", "FMDRV_98", "FMDRV86",
				"TENSH", "tensh", "TENSH_98",
				"FDRV", "fdrv",
				"MBMUS", "mbmus", "MBMUSP", "mbmusp",
				"TRPSCHRN", "trpschrn", "TRPSCR98",
				"UFMD", "ufmd", "UFMD_98",
				"MIZ3", "miz3", "MIZ3_98",
				"PLAY5", "play5", "PLAY5_98", "PLAY3",
				"IBGM", "ibgm", "IBGMP",
				"EMD", "emd", "EMD_98", "FMD",
				"EXMUS", "exmus", "MARBLE98",
				"MUSDRV", "musdrv", "MUSDRVP", "musdrvp",
				"MDR_98", "mdr_98",
				"wlfpk_98", "wlfpk",
				"ZETA_98", "zeta_98", "ZETA",
				"ARTDI_98", "artdi",
				"LW1CD", "lw1cd",
				"SYNTH_98", "synth", "SYNTHIA",
				"ELFMUS98", "elfmus",
				"YOUJU_98", "youju",
				"VALKY_98", "valky",
				"onion_98", "onion",
				"muse_98", "muse",
				"usmd_98", "usmd",
				"usddrv", "USDDRV",
				"mdb_98", "mdb",
				"EMI", "EMIP", "emi",
				"RME", "rme", "RME_98",
				"SSD_98", "ssd_98",
				"ABIKO", "abiko",
				"iris_98", "iris",
				"NTMD", "ntmd",
				"SPLIT", "split",
				"ynsound", "YNSOUND", "yns_98", "YNS_98",
				"mlalf", "MLALF", "mlalf_98", "mlfplay",
				"opndrvx", "OPNDRVX",
				"mmd2", "MMD2", "mmd2va",
				"iwaplay", "IWAPLAY",
				"bgmdrv98", "bgmdrv", "BGMDRV98",
				"FMXP", "FMX", "FAKECALL", "FAKE33", "OPN2", "SPL_98",
				"bp", "bdrv",
				"tky98", "TKYDRV",
				"magpa_98", "magpa",
				"NC_98", "NC",
				"MFD_98", "mfd",
				"cplay98", "cplay", "bplay", "fplay",
				"fgplay", "fgplay_h",
				/* midiout カタログシェル（GS/MPU）。cmd2 は停止 — kSkipCmd2 */
				"DOFMDX98", "DOFMDC98",
				"MMD", "MMP_HOOT", "MSP_HOOT", "mmp_hoot", "msp_hoot",
				"MINT",
				"MDDRV", "MDDRV_98",
				"MSDRV4L", "MSDRV4", "MSDRV",
				"midip", "mididrv", "MIDIDRV", "midrv_98", "MIDRV",
				"intdrv", "INTDRV",
				"mmudrv", "MMUDRV",
				"SYNUPS", "SYNPLAY", "SYNUP_98",
				"synups", "synplay", "synup_98",
				"synups2", "synplay2", "synu2_98",
				"MMDR", "MMDR_98",
				"MUSPJ_98",
				"tglmfm", "TGLMFM",
				"MIDEP", "MIDISEND",
				"MMIZ3", "MMIZ3_98",
				"g3m", "G3M",
				"nmd", "NMD",
				/* n3gv2 cmd0 は既に INT D2 AH=7/AH=1。cmd2 は AX=0308（kSkipCmd2）。n3gv11 は別 COM。 */
				"n3gv2", "N3GV2",
				"hmm", "HMM",
				"gbgm", "gbgmp",
				"INT7C",
				"IKDRV",
				"PARALIBD",
				NULL
			};
			/* TGLFMP cmd2 はフェード。PLAY5_98 cmd2 は INT F2 AX=2 → $4F9F mute/init。再生は既に cmd0（コピー + $4F9F + $4EDC）。MIZ3_98 cmd2 は INT40 AH=6 停止。cmd0 は既に AH=6 のあと AH=5 再生。ELFMUS98 cmd2 は INT60 AX=0 停止。cmd0 は既に INT60 AH=1 再生。SYNTH_98 cmd2 は INT60 AH=1。再生は cmd0 AH=0。MAGIC_98 cmd2 は INT EF AX=4。再生は cmd0 AX=5。ARTDI_98 cmd2 は far [3da](1)。ロード/再生は cmd0 [3d6]。LUDY_98 cmd2 は INT52 AX=1。再生は cmd0 AX=0。MUSE_98 cmd2 は停止を far 呼。cmd0 は既にロード+再生。MDR_98 糊 cmd2 INT40 BX=6 は [1AC2] で永久待ち。cmd0 は既にロードし 0x127a/D78 がビジートラックを止める。cmd2 を飛ばす。mmd2 cmd2 INT D2 AX=608。cmd0 は既に AH=1 再生。iwaplay cmd2 INT EB AX=308。cmd0 は既に AH=1 再生。SPLIT_98 cmd2 INT D2 AX=100。再生は AX=101。tky98 cmd2 INT F1 AL=12。再生は AL=11。bgmdrv98/bp/FMXP/NC cmd2 は cmd0 の停止半分を繰り返す。FMD /# + fugam cmd2 は cmd0 再生後の INT D3 AX=01FF（停止）。fgplay_h cmd2 は INT D2 AX=3 CL=8（停止）。再生は cmd0 AX=1。 */
			static const char* kSkipCmd2[] = {
				"tglfmp", "TGLFMP",
				"PLAY5", "play5", "PLAY5_98", "PLAY3",
				"MIZ3", "miz3", "MIZ3_98",
				"ELFMUS98", "elfmus", "ELFMUS",
				"SYNTH_98", "synth", "SYNTHIA",
				"MAGIC_98", "magic_", "MAGIC_",
				"ARTDI_98", "artdi",
				/* EMIT_98 / KOEI FMDRV_98 INT40 cmd2 は FMDRV AX=0200 停止（TENSH と同じ） */
				"EMIT", "emit",
				"FMDRV", "fmdrv", "FMDRV_98", "FMDRV86",
				"TENSH", "tensh", "TENSH_98",
				"FDRV", "fdrv",
				/* MUSDRVP INT7F cmd0 は INT F1 AX=0 ロード + AX=1 再生。cmd2 は AX=2 停止。 */
				"MUSDRV", "musdrv", "MUSDRVP", "musdrvp",
				"LW1CD", "lw1cd",
				"LUDY_98", "ludy", "SCBIOS",
				"IBGMP", "ibgm",
				"muse_98", "muse", "MUSE_98",
				"mmd2", "MMD2", "mmd2va",
				"iwaplay", "IWAPLAY",
				"bgmdrv98", "bgmdrv", "BGMDRV98",
				"FMXP", "FMX", "FAKECALL", "FAKE33", "OPN2", "SPL_98",
				"bp", "bdrv",
				"tky98", "TKYDRV",
				"magpa_98", "magpa",
				"NC_98", "NC",
				"MFD_98", "mfd",
				"cplay98", "cplay", "bplay", "fplay",
				/* SSD_98 cmd2 は INT 42 AH=3 DX=3000（SSG-PCM 切替）。再生は cmd0 の AH=1。 */
				"SSD_98", "ssd_98",
				"usmd_98", "usmd",
				"usddrv", "USDDRV",
				"VALKY_98", "valky",
				"YOUJU_98", "youju",
				"ABIKO", "abiko",
				/* iris_98 cmd0: far [326] 停止 + IN 7E2 AX-1 / far [322] 再生。cmd2 は [326] 停止のみ。 */
				"iris_98", "iris",
				"MDR_98", "mdr_98",
				"wlfpk_98", "wlfpk",
				"ZETA_98", "zeta_98", "ZETA",
				"SPLIT", "split",
				"NTMD", "ntmd", "NTMDP",
				"FMD /", "fugam", "fugam_98",
				"fgplay", "fgplay_h",
				"DOFMDX98", "DOFMDC98",
				"MMD", "MMP_HOOT", "MSP_HOOT", "mmp_hoot", "msp_hoot",
				"MINT",
				"MDDRV", "MDDRV_98",
				"MSDRV4L", "MSDRV4", "MSDRV",
				"midip", "mididrv", "MIDIDRV", "midrv_98", "MIDRV",
				"intdrv", "INTDRV",
				"mmudrv", "MMUDRV",
				"SYNUPS", "SYNPLAY", "SYNUP_98",
				"synups", "synplay", "synup_98",
				"synups2", "synplay2", "synu2_98",
				"MMDR", "MMDR_98",
				"MUSPJ_98",
				"tglmfm", "TGLMFM",
				"MIDEP", "MIDISEND",
				"MMIZ3", "MMIZ3_98",
				"g3m", "G3M",
				"nmd", "NMD",
				"n3gv2", "N3GV2",
				"hmm", "HMM",
				"gbgm", "gbgmp",
				"INT7C",
				"IKDRV",
				"PARALIBD",
				/* NARU_98 は "NA" 接頭経由でも kGluePlay（NA.COM / NAX）。cmd2 は INT 70 停止。 */
				"naru", "NARU",
				/* NLP_HOOT cmd0: INT60 AH=2 停止 + AH=3F 読 + AH=1 再生。cmd2 は AH=3 停止。 */
				"NLP_HOOT", "nlp_hoot", "NAX", "nax",
				/* YNS_98 cmd0: INT40 AH=0 / AX=0101 / AH=3 ロード+再生。cmd2 は AH=09 AL=91（停止）。 */
				"ynsound", "YNSOUND", "yns_98", "YNS_98",
				"MAKO_98", "MAKO", "mako", "MAKOP",
				/* mlfplay cmd2 は INT60 AH=4 停止。cmd0 が既に AH=1 停止 + AH=2 + 読込 + AH=0 再生。 */
				"mlfplay",
				NULL
			};
			if (DosShellStarts(dosGe_, kGluePlay)
				&& !DosShellStarts(dosGe_, kSkipCmd2)) {
				/* 一覧はコマンド接頭で一致し、「cmd2 が再生」は一部だけ真 — MAKO_98 は cmd2 に mute-all（reg 27 タイマオフ、全 TL を 7F、SSG ミキサオフ）で答え、cmd0 が既に始めた曲を消した。シェル毎に推測し続けるより、再撃がシーケンサを開始ではなく停止したときに気づき、動いていたコマンドを戻す。 */
				const unsigned keyBefore = opnKeyOnCount_;
				const int cmdBefore = extCmd_;
				const uint8_t tmrBefore = g_lastTimerCtrl;
				extCmd_ = 2;
				np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				np2_interrupt((uint8_t)funcVect_);
				PumpCycles(cpuCycles_ + (drainBudget / 2ull));
				const int wasRunning = (tmrBefore & 0x0c) != 0;
				const int nowStopped = (g_lastTimerCtrl & 0x0c) == 0;
				if (wasRunning && nowStopped && opnKeyOnCount_ == keyBefore) {
					extCmd_ = cmdBefore;
					if (dosGe_)
						BindDosTriggerSong(dosGe_, titleCode);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					np2_interrupt((uint8_t)funcVect_);
					PumpCycles(cpuCycles_ + (drainBudget / 2ull));
				}
			}
			if (starPlay && IvtHooked(0x70, 1)) {
				/* AH=2: [0290]=1 が ISR を許可。AL → [0292]/[0293] カウントダウン。[0294] が 0x10 になると ISR が mute-all し [0290] をクリア。AL=FFh + [0294] リセットで BGM を生かす。拍は OPN INT0B のみ（PIT 双子無し）。 */
				uint8_t* m70 = np2_mem();
				if (m70) {
					const unsigned s70 =
						(unsigned)m70[0x70 * 4 + 2]
						| ((unsigned)m70[0x70 * 4 + 3] << 8);
					const unsigned b70 = s70 << 4;
					if (b70 + 0x295u < 0x200000u)
						m70[b70 + 0x294] = 0;
				}
				np2_reg_set(NP2_R_AX, 0x02FF);
				np2_reg_set(NP2_R_FLAGS,
					(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				np2_interrupt(0x70);
				PumpCycles(cpuCycles_ + (drainBudget / 4ull));
				uint8_t* mem = np2_mem();
				if (mem) {
					if (!IvtHooked(PC98_OPN_IRQ_VEC, 1) && IvtHooked(0x14, 1)) {
						const unsigned o14 = (unsigned)mem[0x14 * 4]
							| ((unsigned)mem[0x14 * 4 + 1] << 8);
						const unsigned s14 = (unsigned)mem[0x14 * 4 + 2]
							| ((unsigned)mem[0x14 * 4 + 3] << 8);
						mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
					}
					if (IvtHooked(PC98_OPN_IRQ_VEC, 1))
						picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
				}
			} else if (asciiMusicR) {
				s_musicComCompileHold = 0;
				/* ASCII MUSIC.COM INT48: AH=07 は CX:DX 名をコピーして MML 翻訳するだけ。
				   再生は AH=02（糊 cmd0 は AH=3 停止 + AH=1 既定名翻訳 + AH=02）。
				   v2.21 既定名は B:\SOUND\BGM_ .MML なので cmd0 だけでは空シーケンスになる。
				   AH=07 が無い 6728 版は cmd0 の AH=01/02 がハンドル 0 を鳴らす — 触らない。 */
				uint8_t* mem = np2_mem();
				const char* nm = dosSong_[0] ? dosSong_
					: SelectedDosSong(dosGe_, titleCode);
				int musicHasAh07 = 0;
				if (mem && IvtHooked(0x48, 1)) {
					const unsigned s48 = (unsigned)mem[0x48 * 4 + 2]
						| ((unsigned)mem[0x48 * 4 + 3] << 8);
					const unsigned b48 = s48 << 4;
					if (s48 && s48 != (unsigned)DOS98_TRAMP_SEG
						&& b48 + 0x400u < 0x200000u) {
						for (unsigned o = 0; o + 3u < 0x400u; o++) {
							if (mem[b48 + o] == 0x80 && mem[b48 + o + 1] == 0xFC
								&& mem[b48 + o + 2] == 0x07) {
								musicHasAh07 = 1;
								break;
							}
						}
					}
				}
				if (mem && musicHasAh07 && nm && nm[0] && IvtHooked(0x48, 1)) {
					const int v221 = MusicComIsV221(dos_);
					const CEmuDos98File* songf = dos_.FindFile(nm);
					const int stubMml = (songf && songf->size > 0 && songf->size < 400u);
					const int manyStr = MusicComStrCount(songf) >= 38;
					if (v221 || (!stubMml && !manyStr))
						Music98AllowRealOpen();
					CEmuDos98File* mf = const_cast<CEmuDos98File*>(songf);
					if (mf)
						MusicComRewriteBigRepeat(mf);
					if (v221) {
						dos_.SetHandle(0, nm);
						if (mf && MusicComStrCount(mf) >= 1)
							MusicComHoistMacros(mf);
						if (mf)
							MusicComExpandStr(mf);
						if (mf)
							MusicComUnwrapAllBraces(mf);
						if (mf && mf->size > 10000u && mem && IvtHooked(0x48, 1)) {
							const unsigned s48 = (unsigned)mem[0x48 * 4 + 2]
								| ((unsigned)mem[0x48 * 4 + 3] << 8);
							const unsigned b48 = s48 << 4;
							if (s48 && s48 != (unsigned)DOS98_TRAMP_SEG
								&& b48 + 0x1904u < 0x200000u) {
								for (unsigned o = 0x1860u; o + 5u < 0x18C0u; o++) {
									if (mem[b48 + o] == 0xB9 && mem[b48 + o + 1] == 0x10
										&& mem[b48 + o + 2] == 0x27) {
										mem[b48 + o + 1] = 0x00;
										mem[b48 + o + 2] = 0x80;
										break;
									}
								}
							}
						}
					}
					MusicComForceOpnPresent(mem);
					unsigned dst = 0x7C00;
					size_t n = 0;
					while (nm[n] && n < 78) {
						char ch = nm[n];
						if (ch == '/')
							ch = '\\';
						mem[dst + n] = (uint8_t)ch;
						++n;
					}
					mem[dst + n] = 0;
					uint8_t ivt14[4];
					memcpy(ivt14, mem + 0x14 * 4, 4);
					s_musicComCompileHold = 1;
					slavePicMask_ = (uint8_t)(slavePicMask_ | (1u << 4));
					np2_reg_set(NP2_R_CX, 0);
					np2_reg_set(NP2_R_DX, (uint16_t)dst);
					np2_reg_set(NP2_R_DS, 0);
					np2_reg_set(NP2_R_AX, 0x0700);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					np2_interrupt(0x48);
					const uint64_t compileBudget = (uint64_t)cpuHz_ * (v221 ? 5ull : 3ull);
					PumpCycles(cpuCycles_ + (compileBudget > drainBudget
						? compileBudget : drainBudget));
					s_musicComCompileHold = 0;
					memcpy(mem + 0x14 * 4, ivt14, 4);
					/* v2.21 の AH=03 は翻訳バッファまで消す。cmd0 を飛ばしたので停止は不要。 */
					if (!v221) {
						np2_reg_set(NP2_R_AX, 0x0300);
						np2_reg_set(NP2_R_FLAGS,
							(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
						np2_interrupt(0x48);
					}
					MusicComForceOpnPresent(mem);
					np2_reg_set(NP2_R_AX, 0x0200);
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					np2_interrupt(0x48);
					PumpCycles(cpuCycles_ + (drainBudget / 2ull));
				}
				if (mem) {
					if (!IvtHooked(PC98_OPN_IRQ_VEC, 1) && IvtHooked(0x14, 1)) {
						const unsigned o14 = (unsigned)mem[0x14 * 4]
							| ((unsigned)mem[0x14 * 4 + 1] << 8);
						const unsigned s14 = (unsigned)mem[0x14 * 4 + 2]
							| ((unsigned)mem[0x14 * 4 + 3] << 8);
						mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
					}
					if (IvtHooked(PC98_OPN_IRQ_VEC, 1))
						picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
					picMask_ = (uint8_t)(picMask_ & ~(1u << 2));
					slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				}
			} else if (DosShellStarts(dosGe_, kGluePlay)) {
				/* 星付き以外の糊ドライバ用に OPN IRQ を解除 */
				uint8_t* mem = np2_mem();
				static const char* kSlave14[] = {
					"mbmus", "MBMUS", "musdrv", "MUSDRV", "muse", "MUSE",
					NULL
				};
				static const char* kFplayFam[] = {
					"fplay", "FPLAY", "cplay", "cplay98", NULL
				};
				const int fplayFam = DosShellStarts(dosGe_, kFplayFam);
				const int slave14 = (DosShellStarts(dosGe_, kSlave14)
					|| ((ssgPortAJumper_ & 0xC0) == 0xC0))
					&& !fplayFam;
				if (mem) {
					if (slave14 && IvtHooked(0x14, 1)) {
						/* ISR を INT14 に残し、カスケード + IRQ12 を解除 */
						picMask_ = (uint8_t)(picMask_ & ~(1u << 2));
						slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
					} else if (!IvtHooked(PC98_OPN_IRQ_VEC, 1) && IvtHooked(0x14, 1)) {
						const unsigned o14 = (unsigned)mem[0x14 * 4]
							| ((unsigned)mem[0x14 * 4 + 1] << 8);
						const unsigned s14 = (unsigned)mem[0x14 * 4 + 2]
							| ((unsigned)mem[0x14 * 4 + 3] << 8);
						mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(o14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)((o14 >> 8) & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(s14 & 0xff);
						mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)((s14 >> 8) & 0xff);
					}
					if (!slave14 && IvtHooked(PC98_OPN_IRQ_VEC, 1))
						picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
					{
						/* NeSS SPLIT は INT08 と INT0B を同じ CS に植える。再生後 INT08 は EOI のみなのに picMask=00 で IRQ0 が毎 tick DeliverIrqs を reverse し Timer B の INT0B を飢える（keyOn=1 で止まる）。 */
						static const char* kNessIrq0[] = { "SPLIT", "split", NULL };
						if (DosShellStarts(dosGe_, kNessIrq0)
							&& IvtHooked(PC98_TIMER_VEC, 1)
							&& IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
							const unsigned o08 = (unsigned)mem[PC98_TIMER_VEC * 4]
								| ((unsigned)mem[PC98_TIMER_VEC * 4 + 1] << 8);
							const unsigned s08 = (unsigned)mem[PC98_TIMER_VEC * 4 + 2]
								| ((unsigned)mem[PC98_TIMER_VEC * 4 + 3] << 8);
							const unsigned od2 = (unsigned)mem[0xD2 * 4]
								| ((unsigned)mem[0xD2 * 4 + 1] << 8);
							const unsigned sd2 = (unsigned)mem[0xD2 * 4 + 2]
								| ((unsigned)mem[0xD2 * 4 + 3] << 8);
							if (s08 && s08 == sd2 && od2 == 0x15Eu && o08 == 0xFA8u)
								picMask_ = (uint8_t)(picMask_ | 0x01u);
						}
					}
					if (fplayFam) {
						picMask_ = (uint8_t)(picMask_ & ~(1u << 2));
						slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
					}
					/* S20 INT60 AH=0 は IF=0 で戻る。SYNTH_98 IRET はレンダポンプを聾にする（hsj GIRL dumps=1、ifoff）。 */
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					/* FMD MIDI クロック: OPN ISR 無し（INT0B はトランポリンのまま）。IRQ0 を外し PIT を走らせ INT D3 が tick できるように。 */
					if (modeMidi_) {
						picMask_ = (uint8_t)(picMask_ & 0xfeu);
						if (!pitRunning_) {
							pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 60);
							if (pitReload_ == 0) pitReload_ = 1;
							pitCounter_ = pitReload_;
							pitRunning_ = 1;
							pitIrqPending_ = 0;
							pitResidual_ = 0;
						}
					}
					/* ABIKO 級: INT 1C をフックし INT 08 は BIOS に残す */
					if (IvtHooked(PC98_USER_TICK_VEC, 1)
						&& !IvtHooked(PC98_TIMER_VEC, 1)) {
						picMask_ = (uint8_t)(picMask_ & ~(1u << 0));
						if (!pitRunning_) {
							pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 60);
							if (pitReload_ == 0) pitReload_ = 1;
							pitCounter_ = pitReload_;
							pitRunning_ = 1;
							pitIrqPending_ = 0;
							pitResidual_ = 0;
						}
					}
					/* VALKY/SSCP はシーケンサを INT 08 に植える（OPN Timer B ではない）。IRQ0 がマスクのままだと pitirq=1、dumps が FM_TONE init で止まる。 */
					{
						static const char* kValkyPit[] = {
							"VALKY_98", "valky", NULL
						};
						if (DosShellStarts(dosGe_, kValkyPit)
							&& IvtHooked(PC98_TIMER_VEC, 1)) {
							picMask_ = (uint8_t)(picMask_ & 0xfeu);
							ValkyArmSscpPlay(mem, (uint16_t)(titleCode & 0xffff),
								&dos_, dosSong_[0] ? dosSong_
								: SelectedDosSong(dosGe_, titleCode));
							if (!pitRunning_) {
								pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 60);
								if (pitReload_ == 0) pitReload_ = 1;
								pitCounter_ = pitReload_;
								pitRunning_ = 1;
								pitIrqPending_ = 0;
								pitResidual_ = 0;
							}
						}
					}
					FmxArmPitIrq0(dosGe_, modeMidi_ ? 0 : 1);
					if (s_fmxKeepIrq0) {
						picMask_ = (uint8_t)(picMask_ & 0xfeu);
						FmxPlantInt60FromPit(mem);
						if (!pitRunning_) {
							pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 60);
							if (pitReload_ == 0) pitReload_ = 1;
							pitCounter_ = pitReload_;
							pitRunning_ = 1;
							pitIrqPending_ = 0;
							pitResidual_ = 0;
						}
					}
				}
			}
		}
		pumpAbortOnMusic_ = 0;
		return 1;
	}

	int loaded = 0;
	/* Ys/Ys2 Falcom 糊はバンク毎に cmd0 内（HostService 0x10）で dataAddr_ を置く — 前タイトルの古いアドレスに対して先読みしない。 */
	if (dataAddr_ > 0 && bootCs_ != 0x0160)
		loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
	if (data2Addr_ > 0)
		LoadSongToAddr(song & 0xff, data2Addr_, file2Size_, 1);

	/* BirdySoft CAL/PAL: ドライバは OPN ISR をリロケするが IVT 0x0B を書かない。再生（INT60）は待ちフラグ [DS:269B]=FF を置き ISR がクリアするまでスピン — ベクタ無しだと DeliverIrqs が OPN IRQ を拒み再生がハング。リロケ済み ISR（FB50… または PUSH…/OUT 0Ah/STI 変種）を入れ、チェイン先がまだ null なら INT08 に IRET をパーク。 */
	if (cal98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			const unsigned o60 = (unsigned)mem[0x60 * 4] | ((unsigned)mem[0x60 * 4 + 1] << 8);
			const unsigned s60 = (unsigned)mem[0x60 * 4 + 2] | ((unsigned)mem[0x60 * 4 + 3] << 8);
			/* リロケ済み INT60 セグメントを優先。MF 期 __02.DAT は INT60 インストール完了前／無しでも物理 0x1000 に居る。 */
			unsigned bases[2];
			int nBase = 0;
			if (s60 && IvtHooked(0x60, 0))
				bases[nBase++] = s60 << 4;
			bases[nBase++] = 0x1000u;
			unsigned isr = 0, isrSeg = 0;
			static const uint8_t sigA[] = {
				0xFB, 0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x55, 0x1E, 0x06, 0xFC
			};
			static const uint8_t sigB[] = {
				0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x55, 0x1E, 0x06, 0xE4, 0x0A
			};
			for (int bi = 0; bi < nBase && !isr; bi++) {
				const unsigned base = bases[bi];
				const unsigned span = 0x8000;
				for (unsigned p = base; p + 32 < base + span && !isr; p++) {
					int okA = 1, okB = 1;
					for (unsigned i = 0; i < sizeof(sigA); i++)
						if (mem[p + i] != sigA[i]) { okA = 0; break; }
					for (unsigned i = 0; i < sizeof(sigB); i++)
						if (mem[p + i] != sigB[i]) { okB = 0; break; }
					if (!okA && !okB) continue;
					for (unsigned q = p; q + 3 < p + 0x30; q++) {
						if (mem[q] == 0xBA && mem[q + 1] == 0x88 && mem[q + 2] == 0x01) {
							isr = p - base;
							isrSeg = base >> 4;
							break;
						}
					}
				}
			}
			if (s60 && IvtHooked(0x60, 0)) {
				const unsigned base = s60 << 4;
				/* リロケが番兵 EB00 を残したら INT60 near 呼表を直す */
				for (unsigned p = base + (o60 ? o60 : 0x60); p + 8 < base + 0x200; p++) {
					if (mem[p] == 0x83 && mem[p + 1] == 0xE7 && mem[p + 2] == 0x0E
						&& mem[p + 3] == 0xFF && mem[p + 4] == 0x95) {
						const unsigned old = (unsigned)mem[p + 5] | ((unsigned)mem[p + 6] << 8);
						if (old == 0xEB00) {
							const unsigned table = (p + 7) - base;
							mem[p + 5] = (uint8_t)(table & 0xff);
							mem[p + 6] = (uint8_t)(table >> 8);
						}
						break;
					}
				}
			}
			if (isr) {
				if (!IvtHooked(PC98_TIMER_VEC, 0)) {
					mem[0x518] = 0xCF; /* IRET */
					mem[PC98_TIMER_VEC * 4 + 0] = 0x18;
					mem[PC98_TIMER_VEC * 4 + 1] = 0x05;
					mem[PC98_TIMER_VEC * 4 + 2] = 0x00;
					mem[PC98_TIMER_VEC * 4 + 3] = 0x00;
				}
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isr & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isr >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3)); /* IRQ3 OPN */
			}
		}
	}

	/* QueenSoft MADP: INT40 は AL 添字 API。AL=1A は N3GOLF 同様 INT14（IVT@0x50）へ OPN ISR を植える。INT14→INT0B ミラー + IRQ3 解除。 */
	if (madp98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			mem[0x501] = (uint8_t)(mem[0x501] | 0x08);
			const unsigned isrOff = (unsigned)mem[0x14 * 4]
				| ((unsigned)mem[0x14 * 4 + 1] << 8);
			const unsigned isrSeg = (unsigned)mem[0x14 * 4 + 2]
				| ((unsigned)mem[0x14 * 4 + 3] << 8);
			if (isrSeg || isrOff) {
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		}
	}

	/* N3GOLF: 糊は実基板の IRQ12 経路である INT 14h に OPN タイマ ISR を置くが IVT 0x0B は書かない。こちらの OPN IRQ はマスタ IRQ3 → INT 0x0B なので INT14 ハンドラをそこへコピーして解除。曲は fm.bin 内（filesize=0）。再生は INT7F cmd0 + EXT_SONG。 */
	if (n3golf98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			unsigned isrOff = (unsigned)mem[0x14 * 4]
				| ((unsigned)mem[0x14 * 4 + 1] << 8);
			unsigned isrSeg = (unsigned)mem[0x14 * 4 + 2]
				| ((unsigned)mem[0x14 * 4 + 3] << 8);
			int ok = 0;
			if (isrSeg && isrOff) {
				const unsigned phys = (isrSeg << 4) + isrOff;
				if (phys + 6 < 0x200000u
					&& mem[phys] == 0xFA && mem[phys + 1] == 0x1E
					&& mem[phys + 2] == 0x06 && mem[phys + 3] == 0x60)
					ok = 1;
			}
			/* フォールバック: fm.bin セグメント 0x0100 を CLI;PUSH DS;PUSH ES;PUSHA で走査 */
			if (!ok) {
				const unsigned base = 0x0100u << 4;
				for (unsigned p = base; p + 32 < base + 0x8000u; p++) {
					if (mem[p] == 0xFA && mem[p + 1] == 0x1E
						&& mem[p + 2] == 0x06 && mem[p + 3] == 0x60
						&& mem[p + 4] == 0x8C && mem[p + 5] == 0xC8) {
						/* EOI 付き ISR（近くに OUT 00h,20h）を優先 */
						int eoi = 0;
						for (unsigned q = p; q + 4 < p + 0x40; q++) {
							if (mem[q] == 0xB0 && mem[q + 1] == 0x20
								&& mem[q + 2] == 0xE6 && mem[q + 3] == 0x00) {
								eoi = 1;
								break;
							}
						}
						if (eoi) {
							isrOff = p - base;
							isrSeg = 0x0100;
							ok = 1;
							break;
						}
					}
				}
			}
			if (ok) {
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3)); /* IRQ3 OPN */
				slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4)); /* IRQ12 */
			}
		}
		/* フォールスルー: EXT_SONG の cmd0 + 曲が再生開始（dataaddr 無し） */
	}

	/* Falcom PROG.BIN（Alm/LM）: OPN 検出成功は音楽 ISR を INT14/INT15 に植える（INT0B ではない）。MADP/N3GOLF 同様ミラーし YM タイマ IRQ が走る。検出失敗経路は INT08 — どちらでも PIT を生かす。 */
	if (prog98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			unsigned isrOff = 0, isrSeg = 0;
			for (int v = 0; v < 2; v++) {
				const unsigned vec = (v == 0) ? 0x14u : 0x15u;
				if (!IvtHooked(vec, 0)) continue;
				isrOff = (unsigned)mem[vec * 4]
					| ((unsigned)mem[vec * 4 + 1] << 8);
				isrSeg = (unsigned)mem[vec * 4 + 2]
					| ((unsigned)mem[vec * 4 + 3] << 8);
				if (isrSeg || isrOff) break;
				isrOff = 0;
				isrSeg = 0;
			}
			if (isrSeg || isrOff) {
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			if (IvtHooked(PC98_TIMER_VEC, 0)) {
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				if (!pitRunning_) {
					pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
					if (pitReload_ == 0) pitReload_ = 1;
					pitCounter_ = pitReload_;
					pitRunning_ = 1;
				}
			}
			np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		}
	}

	/* KSK DKS/FQ（dks/duelsc/fq3/fq4）: ブート INT69 AH=0A 後、CS:[tableVar] がサイズ前置曲バンク（ドライバイメージ先 BSS）を指す。AH=0A プロローグ（06 1E 60 0E 1F B8 xx xx … A3 table）を探し、カタログ曲をそこに載せ cmd1（AH=0 再生）を強制。 */
	if (dks98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			const unsigned s69 = (unsigned)mem[0x69 * 4 + 2]
				| ((unsigned)mem[0x69 * 4 + 3] << 8);
			if (s69 && s69 < 0xF000) {
				const unsigned base = s69 << 4;
				unsigned tableVar = 0;
				const unsigned span = 0x1800;
				for (unsigned p = base; p + 24 < base + span && p + 24 < 0x200000u; p++) {
					/* 共通: 06 1E 60 0E 1F B8 … 03 C1 89 1E .. A3 表 */
					if (mem[p] == 0x06 && mem[p + 1] == 0x1E && mem[p + 2] == 0x60
						&& mem[p + 3] == 0x0E && mem[p + 4] == 0x1F && mem[p + 5] == 0xB8
						&& mem[p + 8] == 0x89 && mem[p + 9] == 0x0E
						&& mem[p + 12] == 0xA3
						&& mem[p + 15] == 0x03 && mem[p + 16] == 0xC1
						&& mem[p + 17] == 0x89 && mem[p + 18] == 0x1E
						&& mem[p + 21] == 0xA3) {
						tableVar = (unsigned)mem[p + 22] | ((unsigned)mem[p + 23] << 8);
						break;
					}
					/* FQ3 BGMDRV: … 03 C1。CS: MOV [seg],BX。CS: MOV [table],AX の並び */
					if (mem[p] == 0x03 && mem[p + 1] == 0xC1
						&& mem[p + 2] == 0x2E && mem[p + 3] == 0x89 && mem[p + 4] == 0x1E
						&& mem[p + 7] == 0x2E && mem[p + 8] == 0xA3) {
						tableVar = (unsigned)mem[p + 9] | ((unsigned)mem[p + 10] << 8);
						break;
					}
				}
				if (tableVar && tableVar + 1u < 0x10000u) {
					const unsigned tableOff = (unsigned)mem[base + tableVar]
						| ((unsigned)mem[base + tableVar + 1] << 8);
					const int dest = (int)(base + tableOff);
					if (dest > 0 && LoadSongToAddr(song & 0xff,
						dest, fileSize_ > 0 ? fileSize_ : 0x1000, 0))
						loaded = 1;
				}
			}
		}
	}

	/* Glodia MDPLAY.BIN / MDRIVE.BIN: INT08 にドライバの PIT ISR があることを保証。CALL もする EOI 付き stub（シーケンサ）を優先。単独 EOI/IRET ではない。 */
	if (mdplay98_ && !IvtHooked(PC98_TIMER_VEC, 0)) {
		uint8_t* mem = np2_mem();
		if (mem) {
			unsigned bases[3];
			int nBase = 0;
			/* biblem mdrivep は MAIN.EXE が 0x0FE00 に居る。0x10000 を先に走査すると MZ ゴミを PIT ISR と誤認し得る。 */
			if (!(bootCs_ == 0x1000 && bootIp_ == 0xF000))
				bases[nBase++] = 0x1000u << 4;
			/* biblem MDRIVE @0x2B000。フック済みなら INT40 seg が良いヒント */
			const unsigned s40 = (unsigned)mem[0x40 * 4 + 2]
				| ((unsigned)mem[0x40 * 4 + 3] << 8);
			if (s40 && s40 < 0xF000)
				bases[nBase++] = s40 << 4;
			bases[nBase++] = 0x2B00u << 4;
			unsigned isrOff = 0, isrSeg = 0x10;
			unsigned bestScore = 0;
			for (int bi = 0; bi < nBase; bi++) {
				const unsigned base = bases[bi];
				if (base + 0x100 >= 0x200000u) continue;
				for (unsigned p = base; p + 16 < base + 0x3000u && p + 16 < 0x200000u; p++) {
					if (mem[p] != 0x60 && mem[p] != 0x1E && mem[p] != 0xFC)
						continue;
					int eoi = 0, iret = 0, call = 0;
					for (unsigned q = p; q + 4 < p + 0x60 && q + 4 < 0x200000u; q++) {
						if (mem[q] == 0xE8) call = 1;
						if (mem[q] == 0xB0 && mem[q + 1] == 0x20
							&& mem[q + 2] == 0xE6 && mem[q + 3] == 0x00)
							eoi = 1;
						if (mem[q] == 0xCF) {
							iret = 1;
							break;
						}
					}
					if (!(eoi && iret)) continue;
					const unsigned score = 1u + (call ? 2u : 0u) + (mem[p] == 0x60 ? 1u : 0u);
					if (score > bestScore) {
						bestScore = score;
						isrOff = p - base;
						isrSeg = base >> 4;
						if (score >= 3) break;
					}
				}
				if (bestScore >= 3) break;
			}
			if (bestScore > 0) {
				mem[PC98_TIMER_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_TIMER_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_TIMER_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_TIMER_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				if (!pitRunning_) {
					pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
					if (pitReload_ == 0) pitReload_ = 1;
					pitCounter_ = pitReload_;
					pitRunning_ = 1;
				}
			}
		}
	}

	/* Glodia mdplay.bin / mdzplay.bin: INT7F cmd0 は INT41 停止。cmd1 が INT4A/49（EXT_PARAM 7E4 が 0 なら AL=0）のあと INT40 BX=8000。
	   DS=ES=1000 なので BX=8000 は dataaddr 0x18000。既定 cmd0 だと無音。曲は dataaddr へ先読み。 */
	if (mdplay98_) {
		if (dataAddr_ > 0) {
			const int n = fileSize_ > 0 ? fileSize_ : 0x4000;
			loaded = LoadSongToAddr(song & 0xff, dataAddr_, n, 0);
		}
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
		if (!pitRunning_) {
			pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
			if (pitReload_ == 0) pitReload_ = 1;
			pitCounter_ = pitReload_;
			pitRunning_ = 1;
			pitIrqPending_ = 0;
			pitResidual_ = 0;
		}
		extCmd_ = 1;
		extParam_ = 0;
		extSong_ = (uint16_t)(titleCode & 0xffff);
		np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget);
		(void)loaded;
		return 1;
	}

	/* Glodia emdr/zavas/vd: INT7F cmd0 は停止 far。cmd1 が IN 7E4（曲）のあと再生 far。曲 BSS は dataaddr=ドライバ CS:0000、filesize が 20A0 より手前。 */
	if (packCmd1_) {
		PatchGlodiaYmDetect(np2_mem(), dataAddr_, bootCs_);
		if (dataAddr_ > 0) {
			const int n = fileSize_ > 0 ? fileSize_ : 0x2000;
			loaded = LoadSongToAddr(song & 0xff, dataAddr_, n, 0);
		}
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
		picMask_ = (uint8_t)(picMask_ & ~(1u << 2)); /* cascade */
		picMask_ = (uint8_t)(picMask_ & ~(1u << 3)); /* IRQ3 OPN Timer — 糊は INT14 に植える */
		slavePicMask_ = 0x00; /* zavas ISR は IN 0A（スレーブマスク）。FF のままだと再生を捨てる */
		if (!pitRunning_) {
			pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
			if (pitReload_ == 0) pitReload_ = 1;
			pitCounter_ = pitReload_;
			pitRunning_ = 1;
			pitIrqPending_ = 0;
			pitResidual_ = 0;
		}
		extCmd_ = 1;
		extParam_ = (uint16_t)(song & 0xff);
		extSong_ = (uint16_t)(titleCode & 0xffff);
		np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget);
		/* INT14/INT08 は再生 far がテーブルを組んだあと。先に植えると vd PIT ISR が [347E] 未初期化のまま 12k 書きする。 */
		PatchGlodiaOpnIsr(np2_mem(), dataAddr_, bootCs_, &picMask_);
		if (IvtHooked(PC98_TIMER_VEC, isDos_))
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
		(void)loaded;
		return 1;
	}

	/* Wolfteam 000_BOOT: 糊 INT 7Fh cmd1 → INT 4Ah（曲は AX）。INT 4A はゲーム FS から BX:0000 を（再）ロードする INT 43h を呼び、こちらの dataaddr 先読みを消す。INT 43 に IRET をパークし cmd1 だけ発行（cmd0 は停止 / AX=FFFFh）。 */
		if (wolfteam98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			mem[0x520] = 0xCF;
			mem[0x43 * 4 + 0] = 0x20;
			mem[0x43 * 4 + 1] = 0x05;
			mem[0x43 * 4 + 2] = 0x00;
			mem[0x43 * 4 + 3] = 0x00;
			if (dataAddr_ > 0)
				loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
			WolfPlantEmmStub(mem);
			WolfPlantTimerIvts(mem);
			WolfReplantGlueInt7f(mem);
		}
		{
			wolfSyncRun_ = 1; /* ISR 遅延 stub は非ビジー E0D2 が要る */
			/* ブート PIC ICW はしばしば IRQ0 をマスクしたまま。音楽 tick に PIT が要る */
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
			slavePicMask_ = 0x00;
			if (!pitRunning_) {
				pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
				if (pitReload_ == 0) pitReload_ = 1;
				pitCounter_ = pitReload_;
				pitRunning_ = 1;
				pitIrqPending_ = 0;
				pitResidual_ = 0;
			}
			if (mem) {
				/* INT 4C AH=00 は DS:SI → MF 音色（ADD SI,8 後 byte13=0x28）を期待。MI が無いとき（apros）は飛ばす — 偽の曲-as-MI はバンクロードをハングしシーケンサを武装しない。 */
				if (wolfMiSeg_ > 0) {
					mem[wolfSongPtr_ + 0] = 0x00;
					mem[wolfSongPtr_ + 1] = 0x00;
					mem[wolfSongPtr_ + 2] = (uint8_t)(wolfMiSeg_ & 0xff);
					mem[wolfSongPtr_ + 3] = (uint8_t)((wolfMiSeg_ >> 8) & 0xff);
					if (dataAddr_ > 0) {
						int n = fileSize_ > 0 ? fileSize_ : 0x2000;
						if (n > 0x2000) n = 0x2000;
						if ((unsigned)wolfSongBuf_ + (unsigned)n <= 0x200000u
							&& dataAddr_ + n <= 0x200000)
							memcpy(mem + wolfSongBuf_, mem + dataAddr_, (size_t)n);
					}
					/* dmdply は INT4C を入れない — フック済みのときだけ呼ぶ */
					if (IvtHooked(0x4C, 0)) {
						mem[wolfGateStop_] = 0xFF;
						mem[wolfGatePlay_] = 0x00;
						np2_reg_set(NP2_R_AX, 0x0000);
						np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
						np2_interrupt(0x4C);
						DrainInterrupt(drainBudget / 2);
					}
				}
				/* ISR 用に曲 ptr を再バインド。6692 からのチャネルストリームオフセットは CS:songBuf ではなく dataaddr ロード（7800:0000）相対 — DS=dataaddr>>4 を保ち SI 0603 などが曲に当たるように。 */
				mem[wolfGateStop_] = 0x00;
				mem[wolfGatePlay_] = 0xFF;
				if (dataAddr_ > 0) {
					const uint16_t songSeg = (uint16_t)((unsigned)dataAddr_ >> 4);
					const uint16_t songOff = (uint16_t)((unsigned)dataAddr_ & 0xf);
					mem[wolfSongPtr_ + 0] = (uint8_t)(songOff & 0xff);
					mem[wolfSongPtr_ + 1] = (uint8_t)(songOff >> 8);
					mem[wolfSongPtr_ + 2] = (uint8_t)(songSeg & 0xff);
					mem[wolfSongPtr_ + 3] = (uint8_t)(songSeg >> 8);
				} else {
					mem[wolfSongPtr_ + 0] = (uint8_t)(wolfSongBuf_ & 0xff);
					mem[wolfSongPtr_ + 1] = (uint8_t)(wolfSongBuf_ >> 8);
					mem[wolfSongPtr_ + 2] = 0x00;
					mem[wolfSongPtr_ + 3] = 0x00;
				}
			}
			np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			extCmd_ = 1;
			extSong_ = (uint16_t)(titleCode & 0xffff);
			WolfReplantGlueInt7f(mem);
			np2_interrupt((uint8_t)funcVect_);
			DrainInterrupt(drainBudget);
			/* 糊 cmd1 が INT4A に届かない 000_BOOT（apros INT7F=IRET）向け。フック済みなら AX=曲で再生 API を直接撃つ。 */
			if (IvtHooked(0x4A, 0)) {
				np2_reg_set(NP2_R_AX, (uint16_t)(titleCode & 0xffff));
				np2_reg_set(NP2_R_FLAGS,
					(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				np2_interrupt(0x4A);
				DrainInterrupt(drainBudget / 2);
			}
			if (mem) {
				mem[wolfGateStop_] = 0x00;
				mem[wolfGatePlay_] = 0xFF;
				if (dataAddr_ > 0) {
					const uint16_t songSeg = (uint16_t)((unsigned)dataAddr_ >> 4);
					const uint16_t songOff = (uint16_t)((unsigned)dataAddr_ & 0xf);
					mem[wolfSongPtr_ + 0] = (uint8_t)(songOff & 0xff);
					mem[wolfSongPtr_ + 1] = (uint8_t)(songOff >> 8);
					mem[wolfSongPtr_ + 2] = (uint8_t)(songSeg & 0xff);
					mem[wolfSongPtr_ + 3] = (uint8_t)(songSeg >> 8);
					int n = fileSize_ > 0 ? fileSize_ : 0x2000;
					if (n > 0x2000) n = 0x2000;
					if ((unsigned)wolfSongBuf_ + (unsigned)n <= 0x200000u
						&& dataAddr_ + n <= 0x200000)
						memcpy(mem + wolfSongBuf_, mem + dataAddr_, (size_t)n);
					mem[wolfTitleWord_ + 0] = (uint8_t)(titleCode & 0xff);
					mem[wolfTitleWord_ + 1] = (uint8_t)((titleCode >> 8) & 0xff);
					/* INT4C エピローグ + シーケンサ武装（000_BOOT @約6692 参照）: MOV BYTE [fa],FF / MOV WORD [fa+3],1 / MOV BYTE [fa+2],0。dmdply は再生ゲートを fa+2 へ畳む — そのバイトを FF に保つ。 */
					mem[wolfFlagA_] = 0xFF;
					if ((unsigned)wolfFlagA_ + 3u < 0x10000u) {
						const int playIsFa2 = (wolfGatePlay_ == (uint16_t)(wolfFlagA_ + 2));
						mem[wolfFlagA_ + 2] = playIsFa2 ? 0xFF : 0x00;
						mem[wolfFlagA_ + 3] = 0x01;
						mem[wolfFlagA_ + 4] = 0x00;
					}
					mem[wolfGateStop_] = 0x00;
					mem[wolfGatePlay_] = 0xFF;
				}
				const uint16_t f73 = (uint16_t)(wolfGateStop_ + 0x2B);
				const uint16_t f74 = (uint16_t)(wolfGateStop_ + 0x2C);
				if (mem[f73] != 0xFF && mem[f74] == 0xFF)
					mem[f74] = 0x00;
			}
			picMask_ = (uint8_t)(picMask_ & 0xfeu);
			if (IvtHooked(PC98_TIMER_VEC, 0)) {
				for (int kick = 0; kick < 8; kick++) {
					np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					np2_interrupt((uint8_t)PC98_TIMER_VEC);
					DrainInterrupt(drainBudget / 32);
					if (mem)
						mem[wolfGatePlay_] = 0xFF;
				}
			}
			(void)loaded;
			return 1;
		}
	}

	/* KOEI98 糊（funcvect INT 40h）: cmd0=再生、cmd2=停止。タイトル上位語は音楽バンクセグメント（EXT 0x7E4）。下位語は曲（0x7E2）。曲データは既にパック済みコード ROM に居る — dataaddr 先読み無し。 */
	if (koei98_ || funcVect_ == 0x40) {
		extCmd_ = 0;
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget);
		return 1;
	}

	/* FMD98 糊 INT7F: cmd0=INT42 AL=0（リセット）、cmd1=INT42 AL=1（再生）。
	   既定の cmd0→ロード→cmd1 は AL=0 が [17D0] を落として曲が 16 key/3s で這う。
	   カタログに dataaddr が無い（ed4_98）。ブート後 INT42/INT08 の CS:22E0
	   が曲バッファ。そこへ載せて cmd1 だけ撃つ。 */
	if (fmd98_) {
		uint8_t* mem = np2_mem();
		if (mem && dataAddr_ < 0x600) {
			unsigned drvCs = 0;
			static const unsigned kVec[] = { 0x42u, 0x08u, 0x0Cu };
			for (unsigned vi = 0; vi < 3u; vi++) {
				const unsigned vec = kVec[vi];
				const unsigned o = (unsigned)mem[vec * 4]
					| ((unsigned)mem[vec * 4 + 1] << 8);
				const unsigned s = (unsigned)mem[vec * 4 + 2]
					| ((unsigned)mem[vec * 4 + 3] << 8);
				if (s == 0 && o == 0) continue;
				if (s >= 0xF000u) continue;
				drvCs = s;
				break;
			}
			const unsigned off = fmdSongOff_ > 0 ? (unsigned)fmdSongOff_ : 0x22E0u;
			const unsigned dest = (drvCs << 4) + off;
			if (drvCs && dest >= 0x600u
				&& dest + (unsigned)(fileSize_ > 0 ? fileSize_ : 0x100) < 0x200000u) {
				dataAddr_ = (int)dest;
				fmdSongOff_ = (int)off;
			}
		}
		if (dataAddr_ >= 0x600 && fileSize_ > 0)
			LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
		/* 糊 cmd1 = INT42 AL=1（CS:22E0 の 16 トラック開始、[17D0]|=8080）。
		   AL=0 は [17D0] を落として這うので撃たない。PIT INT08 が 0x4B0
		   シーケンサ。IF と IRQ0 が落ちていると cmd1 後もキーが 16/3s になる。 */
		extCmd_ = 1;
		np2_reg_set(NP2_R_FLAGS,
			(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget);
		np2_reg_set(NP2_R_FLAGS,
			(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
		return 1;
	}

	if (rx98_ && dataAddr_ > 0 && fileSize_ > 0) {
		if (LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0))
			loaded = 1;
	}

	if (prog98_ && dataAddr_ > 0 && fileSize_ > 0) {
		if (LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0))
			loaded = 1;
	}

	/* Beast3 BST3 糊: AH==0 の cmd0 は停止を far 呼。AH!=0 がロードを選ぶ（その後 AH クリア）。カタログタイトルは 00xx なので既定 cmd0 はロードしない。 */
	if (bst398_)
		extSong_ = (uint16_t)((song & 0xff) | 0x0100);

	/* DOFMD_98 再生経路（dofmd_ でゲート）: 糊 INT 7Fh cmd=1 がホスト 0x11 にリアルモード曲 ptr（SI/DS）を問い、INT 45h で MSC/MV22 等へ。ブートは LoadRoms で入れた INT 14h IRET stub も要る。それが揃えば下の既定 cmd0/cmd1 INT 列で足りる。 */

	/* NOPNDRV は曲 ptr を DS:19F4（off）/ DS:19F6（seg）に保ち DS=ドライバ CS（0x1000）。AH=3 は通常 INT BX/ES からこれを置くが、ソフト INT 入れ子で信頼できなかった — 語を poke して INT7F 再生。本物 NOPNDRV.COM ロードだけ（MUSIC.SYS / MUSDRV2 は別）。 */
	if (nopnDrv_ && bootCs_ != 0 && dataAddr_ > 0) {
		uint8_t* mem = np2_mem();
		const uint16_t drvCs = 0x1000;
		const uint32_t ptrOff = ((uint32_t)drvCs << 4) + 0x19F4u;
		const uint32_t ptrSeg = ((uint32_t)drvCs << 4) + 0x19F6u;
		const uint16_t songOff = (uint16_t)(dataAddr_ & 0xf);
		const uint16_t songSeg = (uint16_t)(dataAddr_ >> 4);
		const int songBytes = fileSize_ > 0 ? fileSize_ : 0x1000;
		const int ptrInSong = (int)ptrOff >= dataAddr_
			&& (int)ptrOff + 4 <= dataAddr_ + songBytes;

		if (!ptrInSong && mem && ptrSeg + 2u <= 0x200000u) {
			mem[ptrOff] = (uint8_t)(songOff & 0xff);
			mem[ptrOff + 1] = (uint8_t)(songOff >> 8);
			mem[ptrSeg] = (uint8_t)(songSeg & 0xff);
			mem[ptrSeg + 1] = (uint8_t)(songSeg >> 8);

			np2_set_cs_ip((uint16_t)bootCs_, 0x004D); /* HLT idle */
			np2_set_ss_sp(0x1000, 0xFFFE);
			np2_reg_set(NP2_R_FLAGS, 0x0202);

			/* IF クリアで AH=3 も発行し、ドライバ側バインドを合わせる */
			np2_reg_set(NP2_R_ES, songSeg);
			np2_reg_set(NP2_R_BX, songOff);
			np2_reg_set(NP2_R_AX, 0x0300);
			np2_reg_set(NP2_R_FLAGS, 0x0002); /* IF クリア — 入れ子 IRQ 無し */
			np2_interrupt(0x42);
			DrainInterrupt(drainBudget / 4);

			/* AH=3 が悪いスタック読で上書きしても ptr を再アサート */
			mem[ptrOff] = (uint8_t)(songOff & 0xff);
			mem[ptrOff + 1] = (uint8_t)(songOff >> 8);
			mem[ptrSeg] = (uint8_t)(songSeg & 0xff);
			mem[ptrSeg + 1] = (uint8_t)(songSeg >> 8);

			np2_reg_set(NP2_R_FLAGS, 0x0202);
			extCmd_ = 0;
			np2_interrupt((uint8_t)funcVect_);
			DrainInterrupt(drainBudget / 4);
			if (loaded || dataAddr_ == 0) {
				extCmd_ = 1;
				np2_interrupt((uint8_t)funcVect_);
				DrainInterrupt(drainBudget);
			}
			return 1;
		}
	}

	/* Xanadu Scenario II（xana2e @1F000 / PR.NO0 @4000 / PR.NO5 @10000）: INT7F は EXT_SONG（07E2）をコマンド、EXT_CMD（07E0）を再生セレクタとして読む — Ys/00BIOS 糊とポートが入れ替わり。Cmd FD は xana2e → PR.NO5（INT14/15 + OPN）を far 呼。再生は曲を既に dataaddr に置いた cmd0 + セレクタ 1。セレクタ 1 だけが INT7E+INT41 経路を取る。 */
	{
		uint8_t* mem = np2_mem();
		if (mem && bootIp_ == 0x0600 && funcVect_ == 0x7f
			&& mem[0x1F000] == 0xFA && mem[0x1F001] == 0xFC
			&& mem[0x1F002] == 0x1E && mem[0x1F003] == 0x06) {
			if (0xA0000u + 0x3FEEu < 0x200000u)
				mem[0xA0000u + 0x3FEEu] =
					(uint8_t)(mem[0xA0000u + 0x3FEEu] | 0x08);
			if (dataAddr_ > 0)
				loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
			extSong_ = 0x00FD;
			extCmd_ = 0;
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			np2_interrupt((uint8_t)funcVect_);
			DrainInterrupt(drainBudget);
			extSong_ = 0; /* コマンド = 再生 */
			extCmd_ = 1;  /* セレクタ = INT7E + INT41 AH=2 */
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			np2_interrupt((uint8_t)funcVect_);
			DrainInterrupt(drainBudget);
			if (!IvtHooked(PC98_OPN_IRQ_VEC, 0)) {
				for (int v = 0; v < 2; v++) {
					const unsigned vec = (v == 0) ? 0x14u : 0x15u;
					if (!IvtHooked(vec, 0))
						continue;
					const unsigned off = (unsigned)mem[vec * 4]
						| ((unsigned)mem[vec * 4 + 1] << 8);
					const unsigned seg = (unsigned)mem[vec * 4 + 2]
						| ((unsigned)mem[vec * 4 + 3] << 8);
					const unsigned phys = (seg << 4) + off;
					if (phys >= 0x200000u || mem[phys] == 0xCF)
						continue;
					mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(off & 0xff);
					mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(off >> 8);
					mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(seg & 0xff);
					mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(seg >> 8);
					picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
					break;
				}
			} else {
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
			}
			/* DrainInterrupt/Render が正気なよう糊 INT18 アイドルへ再パーク */
			np2_set_cs_ip(0x0000, 0x063C);
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			(void)loaded;
			return 1;
		}
	}

	/* INT 1C ドライバは BIOS が既に IRQ0 を進めていることを期待。こちらは代わりに PIT を組まないので、待っている tick を与える。OPN タイマを持たないときだけ — チップ駆動ドライバには不要で、余分な tick は二重駆動になる。 */
	extCmd_ = 0;
	np2_interrupt((uint8_t)funcVect_);
	DrainInterrupt(drainBudget);
	if (g_sddLoadSeg) {
		uint8_t* smem = np2_mem();
		const unsigned slin = (unsigned)g_sddLoadSeg << 4;
		if (smem && slin + 0x428u < 0x200000u && dosSong_[0]) {
			const unsigned dest = (unsigned)smem[slin + 0x427]
				| ((unsigned)smem[slin + 0x428] << 8);
			const CEmuDos98File* sf = dos_.FindFile(dosSong_);
			if (sf && sf->data && sf->size >= 4u
				&& dest >= 0x1A00u && dest < 0xA000u
				&& slin + dest + sf->size < 0x200000u) {
				unsigned n = sf->size;
				if (n > 0x2000u)
					n = 0x2000u;
				memcpy(smem + slin + dest, sf->data, n);
			}
		}
		SddKeepPlay(smem, 1);
		opnInService_ = 0;
		if (chip_) {
			chip_->Write(0, 0x26);
			chip_->Write(1, 0xCA);
			chip_->Write(0, 0x27);
			chip_->Write(1, 0x3A);
			opnLatchedAddr_ = 0x27;
			g_lastTimerCtrl = 0x3A;
		}
		picMask_ = (uint8_t)(picMask_ & ~((1u << 2) | (1u << 3)));
		slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
	}
	if (g_muse2Seg)
		Muse2KeepPlay(np2_mem());
	/* cmd0 後、Falcom 糊は HostService(0x10) で曲先を渡し得る — ロードして cmd1。カタログ dataaddr だけでも足りる（CS を発明しない）。 */
	if ((bootCs_ == 0x0160 || rx98_ || fmd98_ || prog98_ || dataAddrHost_)
		&& dataAddr_ > 0 && fileSize_ > 0) {
		loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
		/* Telenet は曲を bgm/bgm2 対に分ける。ドライバは両方読むので主だけ載せる待ちになる。ゲスト供給ケースに限り、上の Falcom 族は既存の単ファイル動作を保つ。 */
		if (loaded && dataAddrHost_ && data2Addr_ > 0 && file2Size_ > 0)
			LoadSongToAddr(song & 0xff, data2Addr_, file2Size_, 1);
	}
	if (loaded || lastSongLoadOk_) {
		extCmd_ = 1;
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget);
		/* cmd1 後、一部糊（Ys MANPRG、Beast3、Wolf SS）は YM ISR を INT14/15 だけに残す。DeliverIrqs 用に INT0B へミラー。単独 IRET stub は飛ばす（DOFMD/BRANM はシリアル INT14 を 0000:0500 にパーク — それを INT0B へコピーすると MSC の OPN ISR を消す）。 */
		uint8_t* mem = np2_mem();
		if (mem) {
			unsigned isrOff = 0, isrSeg = 0;
			for (int v = 0; v < 2; v++) {
				const unsigned vec = (v == 0) ? 0x14u : 0x15u;
				if (!IvtHooked(vec, 0)) continue;
				const unsigned off = (unsigned)mem[vec * 4]
					| ((unsigned)mem[vec * 4 + 1] << 8);
				const unsigned seg = (unsigned)mem[vec * 4 + 2]
					| ((unsigned)mem[vec * 4 + 3] << 8);
				const unsigned phys = (seg << 4) + off;
				if (phys >= 0x200000u) continue;
				/* DOFMD シリアル stub: 単独 IRET（とこちらの 0x500 パーク） */
				if (mem[phys] == 0xCF) continue;
				if (seg == 0 && off == 0x500) continue;
				isrOff = off;
				isrSeg = seg;
				break;
			}
			if (isrSeg || isrOff) {
				mem[PC98_OPN_IRQ_VEC * 4 + 0] = (uint8_t)(isrOff & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 1] = (uint8_t)(isrOff >> 8);
				mem[PC98_OPN_IRQ_VEC * 4 + 2] = (uint8_t)(isrSeg & 0xff);
				mem[PC98_OPN_IRQ_VEC * 4 + 3] = (uint8_t)(isrSeg >> 8);
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
				slavePicMask_ = (uint8_t)(slavePicMask_ & ~(1u << 4));
			}
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		}
	}
	/* SORC98 糊のみ（boot CS=0、曲 @3000/VA@11800、wstimer）: INT 7Fh は cmd0→INT D2 AL=3（開始）、cmd1→AL=0（チャネル init）。既定の cmd0 のち cmd1 は開始を消す — cmd0 を再発行し再生を定着。 [085A].7 を強制クリアしない（旧 TickSide ハンマー）: 毎量子 CALL 1CEE が走り SSG3 R0A が 0x0F で固まった。ネイティブ BIOS は .7 をセットのまま OPN Timer B で音楽を進める（hoot 正しい SSG ゲート）。他 wstimer ゲーム（Ys CS=0160 等）には適用しない。 */
	if (sorcGlue_) {
		extCmd_ = 0;
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget / 2);
		/* ここで、または TickSide で [085A] bit7 をクリアしない。BIOS は bit7 で INT08→CALL 1CEE をゲート。強制クリアは FM を進めたが SSG3 音量を 0x0F で固めた（hoot 正しいゲートはネイティブスキップが要る）。[085A].7 がセットのまま音楽は OPN Timer B で続く。 */
		/* ブートが IRQ0 をマスクしたままでも PIT tick が使えるようにする */
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
		if (!pitRunning_) {
			pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 240);
			if (pitReload_ == 0) pitReload_ = 1;
			pitCounter_ = pitReload_;
			pitRunning_ = 1;
			pitIrqPending_ = 0;
			pitResidual_ = 0;
		}
	}
	pumpAbortOnMusic_ = 0;
	return 1;
}

/* CHardPc98::DrainInterrupt の実装 */
void CHardPc98::DrainInterrupt(uint64_t budgetCycles)
{
	/* ブート HLT アイドル近く（CS==bootCs、IP が F4 パッチ内）へ戻るか予算を使い切るまで進める。入れ子タイマ IRQ が動くよう OPN/PIT も tick し続ける。 */
	const uint16_t idleCs = (uint16_t)((bootCs_ != 0 || bootIp_ != 0) ? bootCs_ : 0x0060);
	uint64_t start = cpuCycles_;
	while (cpuCycles_ - start < budgetCycles) {
		if (pumpAbortOnMusic_ && !fmd98_ && cpuHz_ > 0
			&& (cpuCycles_ - pumpMusicCycle0_) >= ((uint64_t)cpuHz_ / 200ull)
			&& (pumpSameLive_
				|| opnKeyOnCount_ > pumpMusicKey0_
				|| MidiNoteOnCount() > pumpMusicMidi0_
				|| (fmpSeq_ && !mpuUart_ && opnTimerCount_ > pumpMusicTimer0_)))
			break;
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);
		uint8_t* mem = np2_mem();
		if (cs == idleCs && mem) {
			uint32_t phys = ((uint32_t)cs << 4) + ip;
			if (phys < 0x200000 && mem[phys] == 0xF4) /* HLT */
				break;
		}
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		cpuCycles_ += u;
		TickSide(u);
		AdvanceOpnClocks(u);
		DeliverIrqs();
	}
}

/* 再生を止める */
int CHardPc98::TriggerStop()
{
	extCmd_ = 2;
	np2_interrupt((uint8_t)funcVect_);
	return 1;
}

/* CEmuHardPc98SetActive の実装 */
void CEmuHardPc98SetActive(CHardPc98* hw)
{
	if (!hw) {
		if (g_pc98Active) {
			hootrip_out8 = NULL;
			hootrip_inp8 = NULL;
			g_pc98Active = NULL;
		}
		return;
	}
	hw->BindNp2();
	g_pc98Active = hw;
	hootrip_out8 = Pc98Out8;
	hootrip_inp8 = Pc98In8;
}

/* CEmuHardPc98GetActive の実装 */
CHardPc98* CEmuHardPc98GetActive()
{
	return g_pc98Active;
}
