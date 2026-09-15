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
/* IRQ 配送 */
int CEmuPc98ValkyKeepIrq0()
{
	return s_valkyKeepIrq0;
}
/* FairyDust FMX 3.10（lemmona）も同じ: PIT 校正後のワンショット INT08 @3660 が IRQ0 をマスクし、本物シーケンサ CS:1D60 が tick しない。 */
static int s_fmxKeepIrq0 = 0;
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
static unsigned g_sddLoadSeg = 0;
static unsigned g_muse2Seg = 0;
static uint16_t g_muse2Intr = 0;

/* MmdPlayAssist の実装 */
static void MmdPlayAssist(uint8_t* mem)
{
	if (!mem || !g_mmdLoadSeg) return;
	const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
	if (lin >= 0x200000u) return;
	if (g_mmdClassic) {
		if (lin + 0x154Du < 0x200000u) {
			mem[lin + 0x154A] = 0x88;
			mem[lin + 0x154B] = 0x01;
			mem[lin + 0x154C] = 0x8A;
			mem[lin + 0x154D] = 0x01;
		}
		const unsigned base = lin + 0x180Fu;
		for (unsigned ch = 0; ch < 6u; ch++) {
			const unsigned p = base + ch * 0x33u;
			if (p + 3u < 0x200000u && mem[p + 2] != 0 && mem[p + 3] == 0)
				mem[p + 3] = 1;
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
	, musicComKeepalive_(0)
	, synthIfKeepalive_(0)
	, modeMidi_(0)
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
	MidiCaptureReset();

	chip_ = CEmuChipYm2608Create((uint32_t)opnHz_, opnaMode, sampleRate_);
	if (!chip_) return 0;
	memset(ssgEcho_, 0, sizeof(ssgEcho_));
	g_opnDataLatch = 0;
	/* 旧 OPNDRV.EXE（md5 b5c63c42）は c2gp/dynamo98 のみ。27h/FFh DATA0 の全体エコーは bny の OPNA 指紋を動かした。バスホールドをゲート。 */
	g_opnBusHold = (ge && ge->archive
		&& (!_stricmp(ge->archive, "c2gp") || !_stricmp(ge->archive, "dynamo98")))
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
	g_mmdPlayAssist = 0;
	g_mmd2FnSrc = NULL;
	g_mmdKeyOn = 0;
	g_sddLoadSeg = 0;
	g_muse2Seg = 0;
	g_muse2Intr = 0;
	/* YM3812 と Y8950 は PC-98 バス時計ではなく基板自身の 3.579545 MHz カラーバースト。V/VS/LS 変種は Y8950。FM 側は YM3812 とレジスタ互換なので音楽は鳴る — 8KB ADPCM チャネルだけまだ無い。 */
	if (modeSorch_) {
		opl_ = CEmuChipYm3812Create(3579545u, sampleRate_);
		memset(sorchOplRegs_, 0, sizeof(sorchOplRegs_));
		memset(sorchOplOn_, 0, sizeof(sorchOplOn_));
	}
	/* クロック動作はドライバ系統で選ぶ。MUSIC.COM は SOUND BIOS が YM2608 /2 を選ぶのに頼る。FMP と Falcom RX は自前タイマ定数を組みリセット /6 のまま。ymfm はタイマ長をマスタクロック半分で報告するので FMP は ×2 補償。旧 BIOS 相当 ×3 はドライバ自身の Timer B 拍を追い越した。 */
	const int isFmp = CEmuPc98IsFmp(ge);
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
	} else if (isFmp && modeMidi_) {
		chip_->Write(0, 0x2E); /* MIDI に保つ可聴 FM ピッチは無い */
	}
	/* vg2 / 他 FMP: リセット ÷6 ピッチを残す。旧 ×2/×3 は OPNA がマスタの約 42% しか受けていなかった穴埋め。今は全 cpuCycles_ 経路が AdvanceOpnClocks を食わせ、ymfm の Timer-A/B 長は既にマスタクロック。倍率はテンポをずらすだけ。 */
	else if (isFmp && !modeMidi_) {
		chip_->SetTimerClockScale(1u);
		chip_->SetCarrierFadeClamp(1); /* 停止時のノイズを防ぐ */
	}
	else
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
	if (destAddr <= 0) return 0;
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
			if (addr > 0 && addr < 0x200000) {
				dataAddr_ = addr;
				/* ゲスト自身がこの番地を名付けたので、そこにプリロードするのは下の系統ゲートが防ぐ「ロード番地の発明」ではない。 */
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
	if (!modeBeep_)
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
		if (g_mmdPicIsr)
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
	uint16_t flags = np2_reg_get(NP2_R_FLAGS);
	const int guestIf = (flags & 0x0200) != 0;
	if ((modeBeep_ || modeMidi_) && pitIrqPending_ && !g_pitInService) {
		/* スピーカリップ（BGML_98）は IRQ0 でノートを組む。INT 7F 後はしばしば CLI 待ち。IF が無いと PIT が INT08 に届かず MixBeep は DC ゲート。実 BIOS はまだ IRQ0 を上げる。FMD intelligent ヘルパは MPU ACK 周りで CLI — ここで IF を強制すると INT 0E が FE を奪い 0713 がハングまたは RET 破壊（portOut=2）。 */
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
		const int fmdIntel = modeMidi_ && !mpuUart_ && IvtHooked(0x0E, isDos_);
		if (!fmdIntel) {
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

	if (pitIrqPending_ && (picMask_ & 0x01) == 0 && !g_pitInService) {
		/* PMD の時計は OPN Timer B（IRQ3）。ゲスト組 PIT と本物 INT08 CS がその ISR を飢え（250ms で約 3kHz IRQ0）、IF=0 のままキーオンが凍る。OPN ベクタが生きたら IRQ0 を落とす。 */
		if (pmdOpnIrq_ && pmdPlayArmed_) {
			pitIrqPending_ = 0;
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
		if (mem && (pc88VaIo_ || isDos_) && IvtHooked(0x14, isDos_)
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
	mpuRxFull_ = 0;
	/* ここでパワーオン FE をキューしない。FMP3 -m 検出（CS:1790）は E0D2 を IN し status != 0 かつ bit6 クリア（アイドル 0x80）が要る。pending ACK だと MidiStatusIn が 0x00 を返し検出失敗 → [1DBE]=0、シーケンサが MIDI を出さない（midiBytes=0）。FE は RESET（FFh）／UART モード（3Fh）コマンドハンドラからのみ。 */
	mpuAckR_ = mpuAckW_ = 0;
	g_mpuIrqAsserted = 0;
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
		uint64_t ticks = (dc * 960ull) / ((uint64_t)cpuHz_ + 1ull);
		if (midiNoteOnCount_ == 0) {
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
	chip_->Write((uint32_t)bank, reg);
	chip_->Write((uint32_t)(bank | 1), val);
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
	port = Pc98FoldPitAlias(port);
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
	/* PC-88VA: PC-88 OPN ポートは同じチップ status/data を読む */
	if (pc88VaIo_) {
		switch (port) {
		case 0x44: case 0xA8: port = OPN_ADDR0; break;
		case 0x45: case 0xA9: port = OPN_DATA0; break;
		case 0x46: case 0xAC: port = OPN_ADDR1; break;
		case 0x47: case 0xAD: port = OPN_DATA1; break;
		default: break;
		}
	}
	switch (port) {
	case OPN_ADDR0: {
		uint8_t s = chip_ ? chip_->ReadStatus() : 0xff;
		/* olteus MAP DA40: IN 44h / TEST 80h ビジー待ち。ymfm は OUT と IN の間にクロックが進まないとビジーのまま — VA 再生ではマスク。 */
		if (pc88VaIo_)
			s = (uint8_t)(s & (uint8_t)~0x80);
		/* MMD2.SYS ISR 0x3ff / 0x4a3: OUT addr / IN 188h / TEST 80h。入れ子 INT14 は IF クリアなので、sticky ymfm busy が ISR を永久駐車（opnInService 固着、キーオン 0x28 が書かれない）。 */
		if (g_mmdPicIsr)
			s = (uint8_t)(s & (uint8_t)~0x80);
		/* FMX 3.10 cmd16（186F）は CS:22C7 を near CALL し IN 188h / TEST 80h。ymfm がその fill をまたいで busy のままだと 196D が [2822] を武装しない。 */
		if (s_fmxKeepIrq0)
			s = (uint8_t)(s & (uint8_t)~0x80);
		return s;
	}
	case OPN_DATA0:
		/* SSG I/O A（reg 0x0E）: 基板 IRQ ジャンパ。MUSE/mbmusp は bit7-6 を読んで INT14h を選ぶ。既定オープンバス 0 だと INT0B をフックし EOI はスレーブへ（hootrip preset_muse_irq_jumper）。 */
		if (chip_ && opnLatchedAddr_ == 0x0E && (ssgPortAJumper_ & 0x80))
			return ssgPortAJumper_;
		/* YM2203/2608 SSG $00-$0F は読める。PLAY5 / MMD2.SYS / F.COM はカナリア（0x55 または 1）を書いて IN 比較。ymfm read_data() はレジスタではなく status。直近 DATA0 書込を返す。 */
		if (opnLatchedAddr_ <= 0x0F)
			return ssgEcho_[opnLatchedAddr_];
		/* 旧 TKY/OPNDRV（c2gp、dynamo98）は OUT 27h/40h のあと IN DATA で 0x40 を期待し、OUT addr FFh / IN DATA not-1。本物 YM2203 の 27h は書込専用。PC-98 基板は直近データポート書込をバスホールド。新しい OPNDRV は両比較を NOP（rolling95）。エコーするのはその 2 つのラッチ番地だけ: 一括 DATA0 ラッチは rolling95 の最初の可聴窓を動かした（SIL.MDT fp）。 */
		if (g_opnBusHold && (opnLatchedAddr_ == 0x27 || opnLatchedAddr_ == 0xFF))
			return g_opnDataLatch;
		return chip_ ? chip_->ReadData() : 0xff;
	case OPN_ADDR1: {
		if (modeSorch_)
			return opl_ ? opl_->ReadStatus() : 0x06; /* OPL2 ID パターン */
		uint8_t s = chip_ ? chip_->ReadStatusHi() : 0xff;
		if (pc88VaIo_)
			s = (uint8_t)(s & (uint8_t)~0x80);
		if (s_fmxKeepIrq0)
			s = (uint8_t)(s & (uint8_t)~0x80);
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
		if (port == PIC_CMD && g_mmdPicIsr && opnInService_)
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
	case WOLF_SYNC0:
	case 0xC0D0: /* 代替 PC-98 MIDI データポート */
		if (modeMidi_ || mpuUart_)
			return MidiDataIn();
		if (port == WOLF_SYNC0)
			return wolfSyncRun_ ? 0xFE : 0xFF;
		return 0xff;
	case WOLF_SYNC1:
	case 0xC0D2:
		if (modeMidi_ || mpuUart_)
			return MidiStatusIn();
		if (port == WOLF_SYNC1)
			return wolfSyncRun_ ? 0x00 : 0xFF;
		return 0xff;
	default: return 0xff;
	}
}

/* I/O ポート書込 */
void CHardPc98::PortOut(uint16_t port, uint8_t data)
{
	port = Pc98FoldPitAlias(port);
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
			vaPc88LatchedAddrHi_ = data;
			vaPc88PortHits_++;
			if (chip_) {
				chip_->Write(0x100, data);
				opnLatchedAddrHi_ = data;
			}
			return;
		case 0x47: case 0xAD:
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
			if (pc88VaIo_)
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
			if (opnLatchedAddr_ == 0x24 || opnLatchedAddr_ == 0x25 || opnLatchedAddr_ == 0x27)
				opnTimerCount_++;
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
			if (pc88VaIo_)
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
	case WOLF_SYNC0:
	case 0xC0D0:
		/* midiout / FMP -m: UART MIDI をキャプチャ。Wolfteam FM: コマンドブリッジ */
		if (modeMidi_ || mpuUart_ || port == 0xC0D0) {
			MidiDataOut(data);
			break;
		}
		wolfCmdWriteCount_++;
		if (wolfCmdLogCount_ < sizeof(wolfCmdLog_))
			wolfCmdLog_[wolfCmdLogCount_++] = data;
		WolfCmdByte(data);
		break;
	case WOLF_SYNC1:
	case 0xC0D2:
		if (modeMidi_ || mpuUart_ || port == 0xC0D2)
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
		/* Glodia MDPLAY.BIN（etembl/ragnrk/biblem2）: INT7F 再生は INT 4A/40。ドライバは INT40–4D と PIT ISR を入れるが、ISR は遅い経路でしか IVT08 に書かれない — ブート後 INT08 フックを保証。MDPLAYD（difrlm）は init で既に INT08 を入れ、触ってはいけない。 */
		if (r->name[0] && (_stricmp(r->name, "MDPLAY.BIN") == 0
			|| _strnicmp(r->name, "MDPLAY", 6) == 0
			|| _stricmp(r->name, "MDRIVE.BIN") == 0
			|| _strnicmp(r->name, "MDRIVE", 6) == 0)
			&& _strnicmp(r->name, "MDPLAYD", 7) != 0)
			mdplay98_ = 1;
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
				if (gs && gp) {
					/* 正規配置: play = stop+0x12、曲 far-ptr @stop+0x15。dmdply は play を flagA+2（060B）へ畳む — まだ使える。 */
					wolfGateStop_ = gs;
					wolfGatePlay_ = gp;
					if ((uint16_t)(gp - gs) == 0x0012)
						wolfSongPtr_ = (uint16_t)(gs + 0x15);
					else
						wolfSongPtr_ = (uint16_t)(gs + 0x15);
				}
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
		mem[0x70000] = 0xF9;
		/* 464E ハンドシェイクのビジー待ちだけ RET（最初のヒット）。5B00+ の ISR 遅延 stub は残し WOLF_SYNC1=0（非ビジー）。 */
		static const uint8_t kSyncBusy[] = { 0xBA, 0xD2, 0xE0, 0xEC, 0xA8, 0x40, 0x75, 0xFB };
		for (unsigned p = 0x600; p + 8 < 0x10000u; p++) {
			if (memcmp(mem + p, kSyncBusy, sizeof(kSyncBusy)) == 0) {
				mem[p] = 0xC3;
				break;
			}
		}
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
		/* stem を先に: `MMD2.SYS 4096` は CONFIG 文字列を ZipFsFind し、拡張子無しフォールバックが mmd2.com（同じ stem、先の zip メンバ）を返し type=file SYS イメージを壊した。 */
		const unsigned char* data = NULL;
		if (stem[0])
			data = CEmuZipFsFind(fs, stem, &sz);
		if ((!data || !sz) && r->name && r->name[0]
			&& (!stem[0] || strcmp(stem, r->name) != 0))
			data = CEmuZipFsFind(fs, r->name, &sz);
		unsigned char donorBuf[256 * 1024];
		unsigned donorSz = 0;
		/* 曲のみ zip（gdm_mo/guyna/kizuato/nekoex）はカタログに PMD_98.COM があっても省略 — 兄弟パックからドライバを取る。 */
		if ((!data || !sz) && fs->zipPath[0] && DosIsEngineName(r->name)) {
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
						(unsigned)sizeof(donorBuf), &donorSz) && donorSz > 0) {
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
		"mlfplay", "bp", NULL
	};
	static const char* kBgmlSong[] = { "BGML_98", "bgml", NULL };
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
		if (DosShellStarts(ge, kUsdSong))
			opensByName = 0;
	}
	/* magpa_98: kOpenName は "musdrv" を含む（mbmusp パックはハンドル 0 にファイル名が要る）が、magpa の INT7F cmd0 は曲バイトの AH=3F BX=0 のあと INT40 AX=2000。ハンドル 0 のファイル名テキストは MUSDRV が ASCII を読んだ。 */
	{
		static const char* kMagpaBin[] = { "magpa_98", "magpa", NULL };
		if (DosShellStarts(ge, kMagpaBin))
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
	if (cplayFamily) {
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
	} else if (ge) {
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
	} else if (byte2 != 0) {
		extSong_ = (uint16_t)byte2;
		extParam_ = (uint16_t)hiByte;
	} else {
		extSong_ = (uint16_t)(titleCode & 0xff);
		extParam_ = 0;
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

/* SS_98.COM cmd0 は CS:0196 の ASCIIZ 名を AH=3F 読して INT 41 AH=1。糊は `mov si,ds / xor di,dx`（DX=0196）なので SI:DI はその名への far ポインタ — xor は DI=0 を仮定。BootDos は DI を汚したまま。ドライバ DS:260A の FindFirst が TITLE.DAT を外す（AX=0012）。`mov di,dx` がポインタを名に保つ。 */
static void PatchSs98SongPtr(uint8_t* mem)
{
	if (!mem) return;
	const unsigned seg = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	if (!seg || seg == (unsigned)DOS98_TRAMP_SEG)
		return;
	static const uint8_t kOld[] = { 0x8C, 0xDE, 0x31, 0xD7, 0xB4, 0x01, 0xCD, 0x41 };
	const unsigned cs0 = Pc98DosLin((uint16_t)seg, 0x100);
	for (unsigned d = 0; d + 8u < 0xA0u; d++) {
		const unsigned at = cs0 + d;
		if (at + 8u >= 0x200000u)
			break;
		if (memcmp(mem + at, kOld, 8) != 0)
			continue;
		mem[at + 2] = 0x8B;
		mem[at + 3] = 0xFA;
		return;
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

static void ValkyArmSscpPlay(uint8_t* mem, uint16_t songHandle,
	CEmuDos98* dos, const char* song)
{
	if (!mem)
		return;
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
	/* CSCP/SSCP はシーケンサ（INT08）。VALKY は ES=[041E] で CALL FAR ES:[000C]。valkyrie はそのインストールを終えず（INT 7F はトランポリンのまま）なので blob と far ptr をホストマップ。ライブ SSCP（hinadori INT08=2002:0DB5）は重ねない。 */
	const unsigned s08Now = (unsigned)mem[0x08 * 4 + 2]
		| ((unsigned)mem[0x08 * 4 + 3] << 8);
	if (dos && (!s08Now || s08Now == (unsigned)DOS98_TRAMP_SEG)) {
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
			unsigned isr = 0;
			const unsigned scanN = (n < 0x8000u) ? n : 0x8000u;
			for (unsigned o = 0; o + 8u < scanN; o++) {
				if (mem[dst + o] == 0xFC && mem[dst + o + 1] == 0x2E
					&& mem[dst + o + 2] == 0xF6 && mem[dst + o + 3] == 0x06) {
					isr = o;
					break;
				}
			}
			if (!isr) {
				for (unsigned o = 0; o + 8u < scanN; o++) {
					if (mem[dst + o] == 0x2E && mem[dst + o + 1] == 0xF6
						&& mem[dst + o + 2] == 0x06) {
						isr = o;
						break;
					}
				}
			}
			if (isr) {
				mem[0x08 * 4 + 0] = (uint8_t)(isr & 0xff);
				mem[0x08 * 4 + 1] = (uint8_t)(isr >> 8);
				mem[0x08 * 4 + 2] = (uint8_t)(dcs & 0xff);
				mem[0x08 * 4 + 3] = (uint8_t)(dcs >> 8);
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
			if (n > 0xFFF0u)
				n = 0xFFF0u;
			if (dst + n < 0x200000u)
				memcpy(mem + dst, f->data, n);
		}
	}
	mem[0x600] = 0x80;
	mem[0x601] = 0xFC;
	mem[0x602] = 0x03;
	mem[0x603] = 0x74;
	mem[0x604] = 0x0C;
	mem[0x605] = 0x80;
	mem[0x606] = 0xFC;
	mem[0x607] = 0x02;
	mem[0x608] = 0x75;
	mem[0x609] = 0x03;
	mem[0x60A] = 0x33;
	mem[0x60B] = 0xC0;
	mem[0x60C] = 0xCF;
	mem[0x60D] = 0xB8;
	mem[0x60E] = (uint8_t)(songHandle & 0xff);
	mem[0x60F] = (uint8_t)((songHandle >> 8) & 0xff);
	mem[0x610] = 0xCF;
	mem[0x611] = 0xB8;
	mem[0x612] = (uint8_t)(songSeg & 0xff);
	mem[0x613] = (uint8_t)((songSeg >> 8) & 0xff);
	mem[0x614] = 0xCF;
	mem[0x50 * 4 + 0] = 0x00;
	mem[0x50 * 4 + 1] = 0x06;
	mem[0x50 * 4 + 2] = 0x00;
	mem[0x50 * 4 + 3] = 0x00;
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
	}
	ValkyReplantIsr(mem);
}

/* ValkyLooksIsr の実装 */
static int ValkyLooksIsr(const uint8_t* mem, unsigned p)
{
	if (!mem || p + 6u >= 0x200000u)
		return 0;
	if (mem[p] == 0xFC && mem[p + 1] == 0x2E && mem[p + 2] == 0xF6)
		return 1;
	if (mem[p] == 0x1E && mem[p + 1] == 0x06 && mem[p + 2] == 0x60)
		return 1;
	if (mem[p] == 0x2E && mem[p + 1] == 0xF6 && mem[p + 2] == 0x06)
		return 1;
	return 0;
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
	if (cs >= 0x1000u && cs < 0xA000u && off != 0
		&& ValkyLooksIsr(mem, (cs << 4) + off))
		return;
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
	add(0x2002u);
	add(0x2800u);
	static const unsigned kOff[] = { 0x0DB5u, 0x2DABu, 0x3151u, 0x3090u, 0x0DD2u };
	for (unsigned i = 0; i < nc; i++) {
		const unsigned dst = cands[i] << 4;
		for (unsigned k = 0; k < 5; k++) {
			if (ValkyLooksIsr(mem, dst + kOff[k])) {
				mem[0x08 * 4 + 0] = (uint8_t)(kOff[k] & 0xff);
				mem[0x08 * 4 + 1] = (uint8_t)(kOff[k] >> 8);
				mem[0x08 * 4 + 2] = (uint8_t)(cands[i] & 0xff);
				mem[0x08 * 4 + 3] = (uint8_t)(cands[i] >> 8);
				if (dst + 0x38A9u < 0x200000u)
					mem[dst + 0x38A9] = 1;
				return;
			}
		}
		for (unsigned o = 0; o + 6u < 0x8000u && dst + o + 6u < 0x200000u; o++) {
			if (ValkyLooksIsr(mem, dst + o)) {
				mem[0x08 * 4 + 0] = (uint8_t)(o & 0xff);
				mem[0x08 * 4 + 1] = (uint8_t)(o >> 8);
				mem[0x08 * 4 + 2] = (uint8_t)(cands[i] & 0xff);
				mem[0x08 * 4 + 3] = (uint8_t)(cands[i] >> 8);
				if (dst + 0x38A9u < 0x200000u)
					mem[dst + 0x38A9] = 1;
				return;
			}
		}
	}
	for (unsigned s = 0x1000u; s < 0xA000u; s++) {
		for (unsigned i = 0; i < 5; i++) {
			if (ValkyLooksIsr(mem, (s << 4) + kOff[i])) {
				mem[0x08 * 4 + 0] = (uint8_t)(kOff[i] & 0xff);
				mem[0x08 * 4 + 1] = (uint8_t)(kOff[i] >> 8);
				mem[0x08 * 4 + 2] = (uint8_t)(s & 0xff);
				mem[0x08 * 4 + 3] = (uint8_t)(s >> 8);
				if (((s << 4) + 0x38A9u) < 0x200000u)
					mem[(s << 4) + 0x38A9] = 1;
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
		if (extraParas <= 0x180u && !isMmd)
			extraParas = 0;

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
		if (isMmd)
			g_mmdLoadSeg = loadSeg;
		/* 古典 MMD.SYS: OPN ポートは AH=0 検出（1a38）が埋める。mmd2.com は AH=0 を送らないので 154A は 0 のまま、05d9/048a 書が 188h ではなくポート 0（PIC）に当たる。 */
		if (g_mmdClassic && mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (lin + 0x154Du < 0x200000u) {
				mem[lin + 0x154A] = 0x88;
				mem[lin + 0x154B] = 0x01;
				mem[lin + 0x154C] = 0x8A;
				mem[lin + 0x154D] = 0x01;
			}
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
		/* MMD.SYS（sbr）: 解析 0619 は duration [ch+2]=1 を置くがゲート [ch+3] は置かない。ノート 0849 はゲート→duration をコピー。ゲート 0 は 0732 がチャネルを永久 RET（A0 補助から keys=1、その後 SILENT）。0143 の AH=3 も 17F4 から `rep stos` しこの poke を消す — 解析後に MmdPlayAssist が再適用。 */
		if (g_mmdClassic && mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			const unsigned base = lin + 0x180Fu;
			for (unsigned ch = 0; ch < 6u; ch++) {
				const unsigned gate = base + ch * 0x33u + 3u;
				if (gate < 0x200000u && mem[gate] == 0)
					mem[gate] = 1;
			}
			/* 古典 INIT は YM ISR を AH=25 しない（それは AH=0 / 1d1b）。mmd2.com 糊は AH=0 を送らないので INT0B/14 はトランポリンのまま、AH=3 は [17F4] でスピン（sbr SILENT）。 */
			if (lin + 0x396u < 0x200000u && mem[lin + 0x392] == 0x2E
				&& mem[lin + 0x393] == 0x8C) {
				if (!IvtHooked(0x14, 1)) {
					Pc98Wr16(mem, 0x14u * 4u, 0x0392);
					Pc98Wr16(mem, 0x14u * 4u + 2u, loadSeg);
				}
				if (!IvtHooked(PC98_OPN_IRQ_VEC, 1)) {
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u, 0x0392);
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u + 2u, loadSeg);
				}
			}
		}
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
		if (!_strnicmp(name, "nmuse", 5) && mem && !IvtHooked(0x14, 1)) {
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
		/* fugam ブート AH=3F BX=5 のあと INT D3 AX=0201。シェル前にバインドしないと 0 バイト読が FMD を毒する。FMD /# もハンドル 0 が要る。 */
		static const char* kFmdFugam[] = { "FMD", "fugam", NULL };
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
	/* mbmusp/MUSDRV: SSG I/O A bits7-6 が INT14 を選ぶ。EOI はスレーブを仮定 */
	static const char* kSsgJumperShell[] = {
		"mbmus", "MBMUS", "musdrv", "MUSDRV", "muse", "MUSE",
		"fplay", "FPLAY",
		"mmd2", "MMD2", "mmd2va",
		"iwaplay", "IWAPLAY",
		NULL
	};
	if (chip_ && DosShellStarts(ge, kSsgJumperShell)) {
		ssgPortAJumper_ = 0xC0;
		chip_->Write(0, 0x0E);
		chip_->Write(1, 0xC0);
		opnLatchedAddr_ = 0x0E;
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
		if (!f4Live && f2s != 0 && f2s != (unsigned)DOS98_TRAMP_SEG) {
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
	if (mem && DosShellStarts(ge, kStarcmd)) {
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
	FmxArmPitIrq0(ge, 1);
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
		static const char* kFmdIntel[] = { "FMD", "fugam", NULL };
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
			wolfBridgeEnable_ = 1;
			WolfBridgeReset();
			MidiCaptureReset();
		} else {
			mpuUart_ = 0;
			midiCapArmed_ = 1;
			/* GS ダンプ後のチャネルボイスでプローブ用に OPN を鳴らせる */
			wolfBridgeEnable_ = 1;
		}
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
		uint8_t* mem = np2_mem();
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);
		if (g_muse2Seg && g_muse2Intr
			&& cs == (uint16_t)g_muse2Seg && ip == 0x0014)
			np2_reg_set(NP2_R_IP, g_muse2Intr);
		if (g_mmdLoadSeg)
			MmdPlayAssist(mem);
		if (g_muse2Seg && g_muse2Intr && mem) {
			const unsigned lin = (unsigned)g_muse2Seg << 4;
			if (lin + 10u < 0x200000u)
				Pc98Wr16(mem, lin + 8u, g_muse2Intr);
		}
		if (g_mmdClassic && g_mmdLoadSeg && chip_ && mem) {
			const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
			if (lin + 0x17F5u < 0x200000u && mem[lin + 0x17F4]
				&& (g_lastTimerCtrl & 0x0C) == 0) {
				chip_->Write(0, 0x27);
				chip_->Write(1, 0x15);
				opnLatchedAddr_ = 0x27;
				g_lastTimerCtrl = 0x15;
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

	if (isDos_) {
		if (!pmdOpnIrq_ && (CEmuPc98GeIsPmd(dosGe_) || CEmuPc98DosHasPmd(dos_)))
			pmdOpnIrq_ = 1;
		if (pmdOpnIrq_)
			pmdPlayArmed_ = 1;
		PC98_CENSUS("pre");
		if (PatchSynth98PaiDest(np2_mem(), dos_.PspSeg()))
			synthIfKeepalive_ = 1;
		PatchSs98SongPtr(np2_mem());
		if (dosGe_)
			BindDosTriggerSong(dosGe_, titleCode);
		else {
			extCmd_ = 0;
			extSong_ = (uint16_t)(titleCode & 0xff);
			extParam_ = 0;
			if (dosSong_[0]) {
				dos_.SetHandle(0, dosSong_);
				dos_.SetHandle(5, dosSong_);
				dos_.SetHandle(0x0B, dosSong_);
				if (song < (unsigned)DOS98_HANDLE_MAX)
					dos_.SetHandle((uint16_t)song, dosSong_);
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
		/* mmd2.com は SYS INIT が 0392 を植えたあと INT0B を 03EC IRET stub へ AH=25 し得る。AH=3 待ちの前にシーケンサを再上げ。 */
		if (g_mmdClassic && g_mmdLoadSeg) {
			uint8_t* mem = np2_mem();
			if (mem) {
				const unsigned lin = (unsigned)g_mmdLoadSeg << 4;
				if (lin + 0x396u < 0x200000u && mem[lin + 0x392] == 0x2E
					&& mem[lin + 0x393] == 0x8C) {
					Pc98Wr16(mem, 0x14u * 4u, 0x0392);
					Pc98Wr16(mem, 0x14u * 4u + 2u, (uint16_t)g_mmdLoadSeg);
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u, 0x0392);
					Pc98Wr16(mem, PC98_OPN_IRQ_VEC * 4u + 2u,
						(uint16_t)g_mmdLoadSeg);
				}
			}
		}
		/* 古典 ISR 03CE だけが 27h=15h 武装。mmd2.com は AH=0 を飛ばすのでタイマが始まらず AH=3 が [17F4] で永久スピン。 */
		if (g_mmdClassic && chip_) {
			chip_->Write(0, 0x27);
			chip_->Write(1, 0x15);
			opnLatchedAddr_ = 0x27;
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
		static const char* kFmdPlay[] = { "FMD", "fugam", NULL };
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
		FmxArmPitIrq0(dosGe_, 1);
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
		np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		np2_interrupt((uint8_t)funcVect_);
		uint64_t playDrain = drainBudget;
		{
			static const char* kOlteusDrain[] = { "olteus", NULL };
			/* MUSIC 01 は約 0.5s フレーズ。全ドレインはそれを食う（peak=78）。他タイトルは 0.5s 武装が要る（MUSIC 02 は 60s OK）。 */
			if (dosGe_ && DosShellStarts(dosGe_, kOlteusDrain)
				&& (titleCode & 0xff) == 1 && cpuHz_ > 20)
				playDrain = (uint64_t)cpuHz_ / 20ull;
		}
		PumpCycles(cpuCycles_ + playDrain);
		if (s_valkyKeepIrq0) {
			ValkyReplantIsr(np2_mem());
			ValkyFixFarApiFromGlue(np2_mem());
		}
		if (s_fmxKeepIrq0 && !modeMidi_) {
			Fmx310ArmSeq(np2_mem(), 0, 0x1000);
			PumpCycles(cpuCycles_ + playDrain);
			Fmx310ArmSeq(np2_mem(), 0, 0x1500);
			PumpCycles(cpuCycles_ + playDrain);
		}
		{
			if (ValkyWantArm(dosGe_, &dos_)
				&& opnKeyOnCount_ == 0) {
				s_valkyKeepIrq0 = 1;
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				extCmd_ = 0;
				uint8_t* vmem = np2_mem();
				ValkyArmSscpPlay(vmem, (uint16_t)(titleCode & 0xffff),
					&dos_, dosSong_[0] ? dosSong_
					: SelectedDosSong(dosGe_, titleCode));
				{
					const char* nm = dosSong_[0] ? dosSong_
						: SelectedDosSong(dosGe_, titleCode);
					if (nm && nm[0])
						dos_.SetHandle((uint16_t)(titleCode & 0xffff), nm);
				}
				np2_reg_set(NP2_R_AX, 0);
				np2_reg_set(NP2_R_FLAGS,
					(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				np2_interrupt((uint8_t)funcVect_);
				PumpCycles(cpuCycles_ + playDrain);
				ValkyReplantIsr(vmem);
				ValkyFixFarApiFromGlue(vmem);
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
		if (modeMidi_ && midiNoteOnCount_ == 0) {
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
		static const char* kSs98Once[] = { "SS_98", "ss_98", NULL };
		/* MMD2 糊 cmd0 INT D2 AH=3 は [f8f] を STI 待ちしてからロード+AH=1。2 回目 INT 7F は AH=3 に再入（0x27 は再組しない）し、レンダポンプがその待ちを出ない（michael/orangerd）。 */
		static const char* kMmdOnce[] = { "mmd2", "MMD2", "mmd2va", NULL };
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
		const int repeatPlay = !(dosGe_ && (DosShellStarts(dosGe_, kMscdPlay)
			|| DosShellStarts(dosGe_, kBgmlOnce)
			|| DosShellStarts(dosGe_, kSs98Once)
			|| DosShellStarts(dosGe_, kMmdOnce)
			|| DosShellStarts(dosGe_, kOpndrvOnce)
			|| DosShellStarts(dosGe_, kOlteusOnce)
			|| DosShellStarts(dosGe_, kNaruOnce)
			|| DosShellStarts(dosGe_, kMfdOnce)
			|| DosShellStarts(dosGe_, kMidiDrvOnce)
			|| DosShellStarts(dosGe_, kAvalonOnce)
			|| DosShellStarts(dosGe_, kSynupsOnce)));
		if (repeatPlay) {
			if (dosGe_)
				BindDosTriggerSong(dosGe_, titleCode);
			np2_reg_set(NP2_R_AX, 0);
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			np2_interrupt((uint8_t)funcVect_);
			PumpCycles(cpuCycles_ + (drainBudget / 2ull));
		}
		}
		PC98_CENSUS("trig");
		Pc98MemDump(np2_mem());
		if (modeBeep_ || modeMidi_) {
			/* BGML_98（他スピーカリップも）は IRQ0/INT08 からメロディを駆動。BootDos は PIC マスク 0xFF で開始。プレイヤは INT 7F で外し得るが、レンダ全体で IF/IRQ0 を生かす。FMD MIDI も同じ IRQ0 解除（INT0B は未フック）。 */
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
					const unsigned sF4 = (unsigned)mem[0xF4 * 4 + 2]
						| ((unsigned)mem[0xF4 * 4 + 3] << 8);
					unsigned sF1 = (unsigned)mem[0xF1 * 4 + 2]
						| ((unsigned)mem[0xF1 * 4 + 3] << 8);
					/* F1 が BootDos 後もトランポリンのときだけ Microsoft PACKED / xor 復号 ADVH を終える（watagolf は既にライブ） */
					if ((sF1 == 0 || sF1 == (unsigned)DOS98_TRAMP_SEG)
						&& s7f && s7f != (unsigned)DOS98_TRAMP_SEG
						&& dos_.FindFile("ADVH.EXE")) {
						const unsigned base = s7f << 4;
						const CEmuDos98File* advh = dos_.FindFile("ADVH.EXE");
						unsigned alloc = (unsigned)mem[base + 0x706]
							| ((unsigned)mem[base + 0x707] << 8);
						if (!alloc || alloc == (unsigned)DOS98_TRAMP_SEG)
							alloc = (unsigned)mem[base + 0x712]
								| ((unsigned)mem[base + 0x713] << 8);
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
								np2_reg_set(NP2_R_SS, (uint16_t)s7f);
								np2_reg_set(NP2_R_SP, 0x1700);
								np2_reg_set(NP2_R_FLAGS,
									(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
								const uint64_t budget = (ip <= 0x20u)
									? ((uint64_t)cpuHz_ * 30ull)
									: ((uint64_t)cpuHz_ * 5ull);
								PumpCycles(cpuCycles_ + budget);
								sF1 = (unsigned)mem[0xF1 * 4 + 2]
									| ((unsigned)mem[0xF1 * 4 + 3] << 8);
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
					const unsigned apiSeg = f4Live ? sF4 : (f1Live ? sF1 : 0);
					const unsigned apiVec = f4Live ? 0xF4u : 0xF1u;
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
						if (songLen && workSeg && songLen < 0xF000 && f4Live) {
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
						if (!f4Live && f1Live) {
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
							/* INT7F が空バッファを残したら DOS ファイル表（実ディスク内容）から USO を実体化 */
							if (dosSong_[0]) {
								const CEmuDos98File* sf = dos_.FindFile(dosSong_);
								const int need = (!advhLen || !songSeg
									|| (songSeg << 4) + 4 >= 0x200000u
									|| (mem[songSeg << 4] == 0 && mem[(songSeg << 4) + 1] == 0));
								if (sf && sf->data && sf->size && sf->size < 0xF000u && need) {
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
							const unsigned ent = (sF1 << 4)
								+ ((unsigned)mem[0xF1 * 4] | ((unsigned)mem[0xF1 * 4 + 1] << 8));
							const int nameLoad = (ent + 10 < 0x200000u
								&& mem[ent] == 0xEB && mem[ent + 1] == 0x06
								&& mem[ent + 2] == 'U');
							const unsigned tramp = 0x50000;
							unsigned ti = 0;
							if (nameLoad && dosSong_[0]) {
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
								/* AL=0（早い CS+0x33 は無傷）、AL=1 bind（BootDos が bind 即値を CS へ付け替え）。bind が @04AB で枠を歩いたあと ISR を復元。 */
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
								/* TriggerPlay に一度だけ再入。直後に戻り、後の ISR/タイマ補助が再再生を mute できないように。 */
								{
									static int s_nameLoadReplay = 0;
									if (!s_nameLoadReplay) {
										s_nameLoadReplay = 1;
										const int ok = TriggerPlay(titleCode);
										s_nameLoadReplay = 0;
										return ok;
									}
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
							/* 名前ロード ADVH: AL=1 はノートを組むが再生／チャネル BSS は薄い。ここで Timer A/B を強制すると OEM ISR がすぐ走り全部キーオフ（peak→0）。メモリロード（EB 0F / watagolf）は自分で武装。 */
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
			const int play5Family = DosShellStarts(dosGe_, kPlay5Fam);
			static const char* kOlteusNotStar[] = { "olteus", NULL };
			const int starPlay = DosShellStarts(dosGe_, kStarPlay)
				&& !play5Family
				&& !DosShellStarts(dosGe_, kOlteusNotStar);
			if (starPlay)
				musicComKeepalive_ = 1;
			static const char* kGluePlay[] = {
				/* Hoot/GMPV4 族: INT7F/D2/60 cmd0 がロード、cmd2 が再生 */
				"FMPP", "FMP", "fmp3", "tglfmp",
				"NLP_HOOT", "nlp_hoot", "NAX", "nax", "NA", "nl", "NL",
				"MAKO_98", "MAKO", "mako", "MAKOP",
				"SDN_98", "SDN", "sdn",
				"FMDRV", "fmdrv", "FMDRV_98",
				"MBMUS", "mbmus", "MBMUSP", "mbmusp",
				"TRPSCHRN", "trpschrn", "TRPSCR98",
				"UFMD", "ufmd", "UFMD_98",
				"MIZ3", "miz3", "MIZ3_98",
				"PLAY5", "play5", "PLAY5_98", "PLAY3",
				"IBGM", "ibgm", "IBGMP",
				"EMD", "emd", "EMD_98", "FMD",
				"EXMUS", "exmus", "MARBLE98",
				"MUSDRV", "musdrv",
				"MDR_98", "mdr_98",
				"wlfpk_98", "wlfpk",
				"ARTDI_98", "artdi",
				"LW1CD", "lw1cd",
				"SYNTH_98", "synth", "SYNTHIA",
				"ELFMUS98", "elfmus",
				"YOUJU_98", "youju",
				"VALKY_98", "valky",
				"onion_98", "onion",
				"muse_98", "muse",
				"usmd_98", "usmd",
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
				/* EMIT_98 INT40 cmd2 は FMDRV AX=0200 停止（TENSH と同じ） */
				"EMIT", "emit",
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
				"usmd_98", "usmd",
				"VALKY_98", "valky",
				"YOUJU_98", "youju",
				"ABIKO", "abiko",
				"MDR_98", "mdr_98",
				"wlfpk_98", "wlfpk",
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
				"hmm", "HMM",
				"gbgm", "gbgmp",
				"INT7C",
				"IKDRV",
				"PARALIBD",
				/* NARU_98 は "NA" 接頭経由でも kGluePlay（NA.COM / NAX）。cmd2 は INT 70 停止。 */
				"naru", "NARU",
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
			} else if (DosShellStarts(dosGe_, kGluePlay)) {
				/* 星付き以外の糊ドライバ用に OPN IRQ を解除 */
				uint8_t* mem = np2_mem();
				static const char* kSlave14[] = {
					"mbmus", "MBMUS", "musdrv", "MUSDRV", "muse", "MUSE",
					"fplay", "FPLAY", NULL
				};
				const int slave14 = DosShellStarts(dosGe_, kSlave14)
					|| ((ssgPortAJumper_ & 0xC0) == 0xC0);
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
					FmxArmPitIrq0(dosGe_, 1);
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
		}
		if (loaded) {
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
			np2_interrupt((uint8_t)funcVect_);
			DrainInterrupt(drainBudget);
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

	/* カタログ dataaddr があるときだけ曲先読み（CS を発明しない） */
	if (fmd98_ && dataAddr_ > 0 && fileSize_ > 0) {
		if (LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0))
			loaded = 1;
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
	return 1;
}

/* CHardPc98::DrainInterrupt の実装 */
void CHardPc98::DrainInterrupt(uint64_t budgetCycles)
{
	/* ブート HLT アイドル近く（CS==bootCs、IP が F4 パッチ内）へ戻るか予算を使い切るまで進める。入れ子タイマ IRQ が動くよう OPN/PIT も tick し続ける。 */
	const uint16_t idleCs = (uint16_t)((bootCs_ != 0 || bootIp_ != 0) ? bootCs_ : 0x0060);
	uint64_t start = cpuCycles_;
	while (cpuCycles_ - start < budgetCycles) {
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
