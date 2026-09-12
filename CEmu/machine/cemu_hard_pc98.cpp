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

/* Hoot KOEI / addressing=1 code ROMs pack load as high16=seg, low16=off
   (e.g. 0x01000100 → 0100:0100). Flat phys stays when value fits in 2MB. */
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

/* Last OPN DATA0 write. YM2203 FM regs are write-only; PC-98 boards bus-hold
   the byte so OPNDRV can IN-compare a 27h/40h canary (c2gp / dynamo98). */
static uint8_t g_opnDataLatch = 0;
static int g_opnBusHold = 0;
/* MMD2.SYS INT14 ISR: OCW3 0Bh / IN 00h / TEST 80h, then slave EOI.
   Soft-PIC used to return 0 so it skipped OUT 08h,20h and IRQ12 stuck. */
static int g_mmdPicIsr = 0;
/* VALKY/SSCP sequencer is INT 08. Guest OUT 02h = F7 remasks IRQ0. */
static int s_valkyKeepIrq0 = 0;
/* SS:SP before np2_interrupt(OPN). Soft-PIC used to drop opnInService_ as
   soon as the YM line acked, so OPNDRV's STI-before-EOI re-entered INT0B
   on the private CS:24E4 stack (tlove12_98: 40 IRQs, then IF=0 at 9A00).
   Hold in-service until that frame IRETs — real 8259 keeps ISR until EOI. */
static uint16_t g_opnIsrSs = 0;
static uint16_t g_opnIsrSp = 0;
static uint16_t g_pitIsrSs = 0;
static uint16_t g_pitIsrSp = 0;
static int g_pitInService = 0;
static int g_mpuInService = 0;
static uint16_t g_mpuIsrSs = 0;
static uint16_t g_mpuIsrSp = 0;
/* PC-98 8259 is edge-triggered. Level re-fire while DSR stays low traps
   ISRs that latch status without IN E0D0 (old MMD /I auto at CS:16D0). */
static int g_mpuIrqAsserted = 0;
/* Last MPU I/O (FMD intelligent). Ring of 128. */
struct MpuTrRec {
	uint16_t cs, ip, port;
	uint8_t wr, val;
};
static MpuTrRec g_mpuTr[128];
static unsigned g_mpuTrI, g_mpuTrN;
static uint8_t g_mpuLastSt = 0xff;
static char g_fmdLoadedSong[96];

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
/* MMD.SYS (header "MMD200  ", not MMD2 "MMD200OR"): 07FF copies [si+3]
   into duration. Parse leaves gate 0, so the first note stores duration 0
   and 0732 RETs the channel forever (sbr_98 SILENT with song resident). */
static int g_mmdClassic = 0;
static unsigned g_mmdLoadSeg = 0;
static int g_mmdPlayAssist = 0;
static const uint8_t* g_mmd2FnSrc = NULL;
/* MMD2 0x654 writes A0/A4 then never 28h|F0, so F-num slides on mute ops.
   First A0 per channel latches 28h|F0 until a real key-off. */
static uint8_t g_mmdKeyOn = 0;
static unsigned g_sddLoadSeg = 0;
static unsigned g_muse2Seg = 0;
static uint16_t g_muse2Intr = 0;

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
	/* 4655 overlay: COM parks far ptrs at CS:09D0. File F-num writer is
	   still CALLed there, so IP=09D7 hits 0F (#UD). DF is at 09D6. */
	if (g_mmdPlayAssist && g_mmd2FnSrc && lin + 0x9E0u < 0x200000u
		&& mem[lin + 0x9D6] == 0xDF && mem[lin + 0x9D7] == 0x0F)
		memcpy(mem + lin + 0x9C0, g_mmd2FnSrc, 32);
}

/* Real PC-98: ITF @ F000-F7FF, N88 BIOS @ F800-FFFF, text VRAM @ A000,
   attribute VRAM @ A200, DIP/MEMSW in the text page, BIOS work @ 0000:0500.
   Empty F000:0000 used to be 00h (ADD [BX+SI],AL) and F800:0001 was IRET, so a
   far CALL into firmware popped the wrong frame and later hit INT6. */
static void PlantPc98BiosMap(uint8_t* mem)
{
	if (!mem) return;
	auto fillRetf = [&](unsigned lo, unsigned hi) {
		int empty = 1;
		for (unsigned a = lo; a < hi; a++) {
			if (mem[a]) { empty = 0; break; }
		}
		if (!empty) return;
		memset(mem + lo, 0xCB, hi - lo); /* RETF — far CALL firmware */
	};
	fillRetf(0xF0000u, 0xF8000u);
	/* F800 BIOS window: byte0 RETF for far CALL F800:0000; the rest IRET so
	   a corrupt IVT that landed in high ROM still returns from INT. */
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

	/* MEMSW (NP2 A000:3FE2, 16 bytes). Bit0/bit3 at 3FEE = 286 + FM board. */
	if (0xA0000u + 0x3FEFu < 0x200000u) {
		if (mem[0xA0000u + 0x3FE2u] == 0)
			mem[0xA0000u + 0x3FE2u] = 0x48;
		mem[0xA0000u + 0x3FEEu] = (uint8_t)(mem[0xA0000u + 0x3FEEu] | 0x09);
	}

	/* BIOS work: skip 0510-0544 (DOS LOL / non-DOS INT18 stub). */
	mem[0x501] = (uint8_t)(mem[0x501] | 0x08); /* 80286 / QueenSoft FM */
	if (mem[0x504] == 0 && mem[0x505] == 0) {
		mem[0x504] = 0x80; /* conventional 640 KB */
		mem[0x505] = 0x02;
	}
	mem[0x536] = (uint8_t)(mem[0x536] | 0x04); /* sound board present */
	/* Expansion-memory KB at 0584; daily timer at 05A0 (INT 08 trampoline). */
	if (mem[0x584] == 0 && mem[0x585] == 0)
		mem[0x584] = 0;
}

/* Real PC-98 BIOS INT 08: bump 0000:05A0 (and IBM 0040:006C), chain INT 1C,
   EOI the master PIC. Parked at 00C0 — below the DOS arena (1000h) and
   outside the trampoline (0060) / F000 ROM that IvtHooked rejects.
   Drivers that HLT or poll the daily timer need this the same way PC/AT
   already plants a BDA tick on IRQ0. */
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

/* CEMU_PC98_IPPROF=<file>: histogram of the linear PC executed during the
   play pump. A driver that loads its song and then goes mute is almost always
   spinning on one wait condition, and the hot address names the instruction
   to look at. */
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

	/* Static teardown is not guaranteed to run for every host that embeds
	   the core, so the pump's owner flushes this explicitly on Close. */
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

/* CEMU_PC98_MEMDUMP="<linhex>,<len>,<path>": hex of guest memory as it stood
   when the pump last returned, for reading the wait loop the profiler found. */
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

/* CEMU_PC98_IVT=<path>: which vectors the guest actually owns when play is
   poked, next to the vector we are about to poke. A driver whose API sits on
   a vector we never fire is silent no matter how healthy the rest is. */
struct Pc98CensusPair { uint16_t a; uint8_t d; };

struct Pc98CensusCounts {
	unsigned wr, keyOn, tlLive, fnum, timer, irq, pit;
	unsigned line, svc, noVec, masked, ifOff;
	const Pc98CensusPair* tail;
	unsigned tailN;
};

/* Why an asserted OPN IRQ did not reach the guest. One machine is live at a
   time in the sweep, so file statics are enough and cost no header churn. */
unsigned g_censLine = 0;   /* chip Irq() seen asserted */
unsigned g_censSvc = 0;    /* ...but opnInService_ still latched */
unsigned g_censNoVec = 0;  /* ...but nothing hooked the vector */
unsigned g_censMasked = 0; /* ...but the PIC had the line masked */
unsigned g_censIfOff = 0;  /* ...but the CPU had interrupts disabled */

/* Last value the guest wrote to OPN reg 0x27. Bits 2/3 enable timer A/B, so
   a value with both clear means the sequencer's clock is off and nothing more
   will be played until someone re-arms it. */
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
	/* %ls aborts the whole fprintf when a wide char has no multibyte form in
	   the C locale, which silently swallowed the newline for every Japanese
	   title. Fold to ASCII by hand so the record always terminates. */
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

/* Members are private and adding a method would force a full rebuild of every
   object in the probe link, so the snapshot reads them at the call site. */
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

/* Resolved once at first use and then read straight off this pointer: the
   hook sits on the per-instruction path, so it must cost one null test when
   profiling is off. */
Pc98IpProf* g_ipProf = NULL;

void IpProfInit()
{
	const char* p = getenv("CEMU_PC98_IPPROF");
	if (!p || !p[0]) return;
	static Pc98IpProf inst;
	inst.path = p;
	g_ipProf = &inst;
}

} /* namespace */

/* Catalog <rom type="binary">00 a0 00 00</rom> embeds hex in the name —
   there is no zip member. type="string" is raw ASCII. */
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
			/* Not hex — treat whole name as ASCII string payload. */
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
	/* The BIOS timer handler chains this one; drivers that want a tick and
	   nothing else hook it instead of taking IRQ0 over. */
	PC98_USER_TICK_VEC = 0x1C,
	/* Low RAM BIOS tick ISR. IVT08 cannot stay on the DOS trampoline
	   (HLT;IRET): DeliverIrqs treats that segment as unhooked and drops
	   IRQ0, and F000 is rejected as high ROM. 00C0 sits above the idle
	   packet at 00A0:0100 and below the DOS arena at 1000. */
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

/* Later PC-9801 PIT decode at 3FD9–3FDF (odd) aliases 71/73/75/77.
   DOSBox-X and radioc.dat; BGML_98 writes the speaker divisor to 3FDBh
   and never touches 73h, so without this the PPI gate stays on as DC. */
static uint16_t Pc98FoldPitAlias(uint16_t port)
{
	if ((port & 0xfff8u) == 0x3fd8u && (port & 1u))
		return (uint16_t)(0x71u + (unsigned)(port - 0x3fd9u));
	return port;
}

static CHardPc98* g_pc98Active = NULL;
static int g_pc98Eoi = 0;

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

static void Pc98Out8(unsigned port, unsigned char val)
{
	CHardPc98* hw = g_pc98Active;
	if (hw) hw->PortOut((uint16_t)port, (uint8_t)val);
}

static unsigned char Pc98In8(unsigned port)
{
	CHardPc98* hw = g_pc98Active;
	if (!hw) return 0xff;
	return hw->PortIn((uint16_t)port);
}

/* The OPL half of a SOUND ORCHESTRA gets its own nine monitor rows rather
   than sharing the OPN ones: with s_opnaLayout held at the OPN layout the
   shadow keeps the FM rows and appends these as extra channels. */
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
	/* OPL2 pitch: fnum * clock / (72 * 2^(20 - block)). */
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
	, sound86Mask_(0x00) /* MAME reset: ID=0x40; bit0 set by software for OPNA enhance */
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

void CHardPc98::FreeBanks()
{
	for (int i = 0; i < 256; i++) {
		if (bgmBank_[i]) { free(bgmBank_[i]); bgmBank_[i] = NULL; }
		if (bgm2Bank_[i]) { free(bgm2Bank_[i]); bgm2Bank_[i] = NULL; }
		bgmBankSize_[i] = 0;
		bgm2BankSize_[i] = 0;
	}
}

uint8_t* CHardPc98::Mem()
{
	if (CEmuNp2IsOwner(this)) {
		uint8_t* live = np2_mem();
		if (live)
			return live;
	}
	return np2Ram_ ? np2Ram_ : np2_mem();
}

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

void CHardPc98::BindNp2()
{
	if (!EnsureNp2Ram())
		return;
	CEmuNp2Bind(this, np2Ram_, np2Cpu_, np2HaveCpu_);
}

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

int CHardPc98::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	/* type=86 is the PC-9801-86 board, but many catalog 86 rips (flixmix /
	   kolin2) already PLAYS as YM2203 with A460 absent. Turning that on
	   globally silenced them. emit_* FMDRV86 is the one that needs YM2608
	   + ID 0x40 (otherwise PIC stays masked / keyOn=0). */
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
	/* SOUND ORCHESTRA is a 26K clone, so the OPN half stays a YM2203; what
	   makes the board is the extra OPL chip sharing the 0x18C/0x18E pair. */
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
	/* hootrip keeps CPU at 8 MHz; clockmul is reported only. Keep 8 MHz. */
	(void)clockmul;

	bootCs_ = CEmuParseOptHex(ge, "bootcs", 0);
	bootIp_ = CEmuParseOptHex(ge, "bootip", 0);
	funcVect_ = CEmuParseOptHex(ge, "funcvect", 0x7f) & 0xff;
	dataAddr_ = CEmuParseOptHex(ge, "dataaddr", 0);
	dataAddrHost_ = 0;
	fileSize_ = CEmuParseOptHex(ge, "filesize", 0);
	/* Falcom SORC98 catalog uses decimal "1000" for a 0x1000 window. */
	if (fileSize_ == 1000 && dataAddr_ == 0x3000)
		fileSize_ = 0x1000;
	data2Addr_ = CEmuParseOptHex(ge, "data2addr", 0);
	file2Size_ = CEmuParseOptHex(ge, "file2size", 0);
	addressing_ = CEmuParseOptHex(ge, "addressing", 0);
	if (addressing_ == 0)
		addressing_ = CEmuParseOptHex(ge, "adressing", 0);
	wstimer_ = CEmuParseOptHex(ge, "wstimer", 0);
	dummySndRom_ = CEmuParseOptHex(ge, "dummysndrom", 0);
	/* SORC98 v4–v10 share the same boot stub as v1–v3 but omit wstimer in
	   the catalog. Without it TriggerPlay skips the [085A] clear / cmd0
	   re-issue and the sequencer never leaves mute.
	   Catalog often writes filesize as decimal "1000" (strtoul base0), not
	   "0x1000" — accept both.
	   PC-88VA SORCERIAN uses the same INT7F glue with song window @0x11800. */
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
	midiCapArmed_ = 0; /* BootDos shells may OUT 0→E0D0 forever; arm after */
	MidiCaptureReset();

	chip_ = CEmuChipYm2608Create((uint32_t)opnHz_, opnaMode, sampleRate_);
	if (!chip_) return 0;
	memset(ssgEcho_, 0, sizeof(ssgEcho_));
	g_opnDataLatch = 0;
	/* Old OPNDRV.EXE (md5 b5c63c42) is only c2gp/dynamo98. Echoing 27h/FFh
	   DATA0 globally moved bny's OPNA fingerprint; gate the bus-hold. */
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
	/* Both YM3812 and Y8950 run off the board's own 3.579545 MHz colour-burst
	   crystal, not the PC-98 bus clock. The V/VS/LS variants fit a Y8950
	   instead; its FM half is register-compatible with the YM3812, so the
	   music plays — only its 8 KB ADPCM channel is still missing. */
	if (modeSorch_) {
		opl_ = CEmuChipYm3812Create(3579545u, sampleRate_);
		memset(sorchOplRegs_, 0, sizeof(sorchOplRegs_));
		memset(sorchOplOn_, 0, sizeof(sorchOplOn_));
	}
	/* Select clock behavior by driver family. MUSIC.COM relies on SOUND BIOS
	   selecting YM2608 /2. FMP and Falcom RX program their own timer constants
	   and stay at reset /6. ymfm reports timer durations in half master clocks,
	   so FMP needs ×2 timer compensation; the old BIOS-equivalent ×3 overran
	   the driver's own Timer B cadence. */
	const int isFmp = CEmuPc98IsFmp(ge);
	const int needBios = CEmuPc98IsMusicCom(ge);
	const int is46oku = (_stricmp(ge->archive, "46oku98") == 0);
	if (needBios) {
		chip_->Write(0, 0x2F);
		/* Restore the native synthesis rate after 2Fh's 3× prescale.
		   46oku's remaining octave correction is an F-number block shift,
		   so envelope/LFO time is not slowed with the pitch. */
		chip_->SetPitchRateDiv(3u);
		if (is46oku) {
			chip_->SetPitchOctaveShift(-1);
			/* 46.COM repeatedly adds its fade amount to carrier TLs even
			   though this path has no live MUSIC.COM [0290]/[0294] state. */
			chip_->SetCarrierFadeClamp(1);
		}
	} else if (isFmp && modeMidi_) {
		chip_->Write(0, 0x2E); /* MIDI has no audible FM pitch to preserve. */
	}
	/* vg2 / other FMP: keep reset ÷6 pitch. The old ×2/×3 here was covering
	   for the OPNA only receiving ~42% of its master clock; now that every
	   cpuCycles_ path feeds AdvanceOpnClocks, ymfm's Timer-A/B durations are
	   already in master clocks and any scale would just retune the tempo. */
	else if (isFmp && !modeMidi_) {
		chip_->SetTimerClockScale(1u);
		chip_->SetCarrierFadeClamp(1); /* Prevent noise on stop */
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
		/* PC-88VA CPU is V30; keep i286 for classic PC-98.
		   V30 patch is opt-in after bootcs VA probes stabilize — i286 runs
		   the Falcom SORC stub (same as SORC98) reliably. */
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

void CHardPc98::AttachIoHooks()
{
	g_pc98Active = this;
	hootrip_out8 = Pc98Out8;
	hootrip_inp8 = Pc98In8;
}

void CHardPc98::DetachIoHooks()
{
	if (g_pc98Active == this) {
		hootrip_out8 = NULL;
		hootrip_inp8 = NULL;
		g_pc98Active = NULL;
	}
}

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
	/* BirdySoft drivers (MU-era and MF-era reloc builds) reject or mishandle
	   MF magic; streams are structurally MU (cal_98 __30 == calr __30 aside
	   from the letter). Normalize MF→MU for all cal98_ preloads. */
	if (cal98_ && n >= 2
		&& mem[destAddr] == 'M' && mem[destAddr + 1] == 'F')
		mem[destAddr + 1] = 'U';
	/* Wolfteam MS/MU `\x00B` streams (hioden/suzaku) share the `\x01B` layout
	   but keep a zero type byte; bump to 01 so MUSDRV accepts the song. */
	if (wolfteam98_ && n >= 2
		&& mem[destAddr] == 0x00 && mem[destAddr + 1] == 0x42)
		mem[destAddr] = 0x01;
	lastSongLoadOk_ = 1;
	lastSongLoadBytes_ = (int)n;
	return 1;
}

void CHardPc98::HostService(uint8_t func)
{
	hostStatus_ = 0xff;
	uint8_t* mem = np2_mem();
	if (!mem) return;
	/* DOFMD/BRANM glue OUT 07D4/07D6 as real-mode off/seg (SI/DS). Catalog
	   adressing=0 would otherwise form a flat 00FA11FBh and miss the buffer. */
	/* DKS/FQ stubs pass ES:BX as real-mode song/table pointers on 07D4/07D6
	   (same shape as DOFMD). Flat (seg<<16)|off lands past 2MB and never loads. */
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
	case 0x20: /* primary BGM load */
		if (LoadSongToAddr(song, dest > 0 ? dest : dataAddr_, fileSize_, 0))
			hostStatus_ = 0x00;
		break;
	case 0x21: /* secondary BGM load */
		if (LoadSongToAddr(song, dest > 0 ? dest : data2Addr_, file2Size_, 1))
			hostStatus_ = 0x00;
		break;
	case 0x10: /* set dataaddr from real-mode DS:BX (hostParam3:hostParam2) */
		/* Ys/Ys2 Falcom glue OUT 07D4/07D6 then OUT 07D0,10h. Catalog leaves
		   dataaddr=0 — without this, TriggerPlay never preloads / skips cmd1. */
		{
			const int addr = ((int)hostParam3_ << 4) + (int)hostParam2_;
			if (addr > 0 && addr < 0x200000) {
				dataAddr_ = addr;
				/* The guest named this address itself, so preloading there
				   is not the "invent a load address" guess the family gates
				   below exist to prevent. */
				dataAddrHost_ = 1;
			}
			hostStatus_ = 0x00;
		}
		break;
	case 0x11:
		/* DOFMD_98.BIN / BRANM_98 play path: IN AX,07D4/07D6 → SI/DS as
		   real-mode song pointer, then INT 45h into MSC/MV22/MUSIC.BIN. */
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

void CHardPc98::BeepSetGateFromPpi()
{
	BeepMonUpdate();
}

void CHardPc98::BeepMonUpdate()
{
	const int gate = ((ppiC_ & 0x08) == 0) ? 1 : 0;
	double hz = 0;
	if (pit1Reload_ > 0 && pitClockHz_ > 0)
		hz = (double)pitClockHz_ / (double)pit1Reload_;
	int mid = (hz > 0) ? FmMonShadowHzToMidi(hz) : -1;
	if (mid < 0) {
		/* 1-bit DAC / IRQ0 square: PPI bit3 is the waveform, PIT ch1 is idle.
		   Rising-edge period → pitch; held gate still shows a key. */
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

void CHardPc98::PitOut(uint16_t port, uint8_t data)
{
	if (port == PIT_CTRL) {
		const int ch = (data >> 6) & 3;
		const int access = (data >> 4) & 3;
		/* RW=00 is the counter-latch command, not a mode word: it freezes
		   the count for reading and leaves the mode and any half-written
		   reload alone. */
		if (access == 0x00) {
			if (ch == 0) {
				pitLatch_ = (uint16_t)(pitCounter_ & 0xffff);
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
	/* The live count, not the reload: C-Class FMX loads FFFF, spins a fixed
	   loop, latches and reads back, then divides by (FFFF − count) to get a
	   CPU-speed constant.  Echoing the reload made that zero and the driver
	   died in a divide-by-zero loop before it ever played a note. */
	const uint16_t v = pitLatched_ ? pitLatch_ : (uint16_t)(pitCounter_ & 0xffff);
	if (!pitReadHi_) {
		pitReadHi_ = 1;
		return (uint8_t)(v & 0xff);
	}
	pitReadHi_ = 0;
	pitLatched_ = 0;
	return (uint8_t)(v >> 8);
}

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
		/* accumulate OPN clocks separately in driver; here track IRQ edge */
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
		/* tempo * timebase is clocks/minute (MPU C2=48 @ 120 BPM → 5760). */
		hz /= 60u;
		if (hz < 60u) hz = 60u;
		if (hz > 4000u) hz = 4000u;
		mpuCthResidual_ += cpuCycles * (uint64_t)hz;
		while (mpuCthResidual_ >= (uint64_t)cpuHz_) {
			/* Do not burn a CTH slot while a command ACK is still queued.
			   MMD /I auto OUT B9h then CLI-polls FE; the next empty-queue
			   tick must still be pending so FD arrives in the short probe
			   (CX=1000) instead of 1/60s later. */
			if (mpuAckR_ != mpuAckW_ || mpuRxFull_ || mpuResetBusy_)
				break;
			/* Old MMD CS:1C6 ROL-polls bit0 (idle 80h never waits) under
			   CLI. A CTH FD injected here is IN'd as "not FE", then a
			   phantom FE succeeds and /I auto never latches INT 0E.
			   Hold residual until IF=1 (plant + STI). */
			if ((np2_reg_get(NP2_R_FLAGS) & 0x0200) == 0)
				break;
			mpuCthResidual_ -= (uint64_t)cpuHz_;
			MpuClockTick();
		}
	}
	/* olteus_va: host advances DS:[003C]/[CC4D] only through handshake (≤0x11).
	   Past that, IRQ0 → MAP:09BC so the real sequencer owns the counter.
	   Use ~60Hz for IRQ pulses (VA picture tick); 600Hz starved REP STOSW
	   VRAM clears in MAP:32DD and blocked song load. */
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
				/* Handshake: several soft steps per IRQ slot so boot
				   still reaches 0x11 quickly without 600 IRQs/sec. */
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
				/* Handshake done — request sequencer tick. */
				olteusIrqPulse_ = 1;
			}
		}
	}
	if (olteusDataSeg_) {
		uint8_t* mem = np2_mem();
		const unsigned base = (unsigned)olteusDataSeg_ << 4;
		if (mem && base + 0x5BCFu < 0x200000u) {
			/* play: CMP [5BCE],03E7 / JE spin — force off the magic wait value */
			if (mem[base + 0x5BCE] == 0xE7 && mem[base + 0x5BCF] == 0x03) {
				mem[base + 0x5BCE] = 0x00;
				mem[base + 0x5BCF] = 0x00;
			}
			if (mem[base + 0x5BCA] == 0 && mem[base + 0x5BCB] == 0)
				mem[base + 0x5BCA] = 0x01;
		}
	}
}

void CHardPc98::ArmOlteusVaTimer(uint16_t mapSeg)
{
	if (!mapSeg || mapSeg == (uint16_t)DOS98_TRAMP_SEG)
		return;
	olteusMapSeg_ = mapSeg;
	uint8_t* mem = np2_mem();
	if (!mem)
		return;
	/* MAP entry: MOV AX,ss; MOV SS,AX; MOV AX,ds; MOV DS,AX — DS is CS+0x0F86. */
	const unsigned ent = (unsigned)mapSeg << 4;
	uint16_t dataSeg = (uint16_t)(mapSeg + 0x0F86u);
	if (ent + 10u < 0x200000u
		&& mem[ent] == 0xB8 && mem[ent + 3] == 0x8E && mem[ent + 4] == 0xD0
		&& mem[ent + 5] == 0xB8 && mem[ent + 8] == 0x8E && mem[ent + 9] == 0xD8) {
		dataSeg = (uint16_t)(mem[ent + 6] | ((unsigned)mem[ent + 7] << 8));
	}
	olteusDataSeg_ = dataSeg;
	/* MAP image >64K; plant inside first paragraph at a BSS zero-run (FE86).
	   near CALL 09BC / IRET — music tick ends in near RET. */
	const unsigned trampOff = 0xFE86u;
	const unsigned base = (unsigned)mapSeg << 4;
	const unsigned tramp = base + trampOff;
	if (tramp + 16u >= 0x200000u)
		return;
	if (!olteusTrampOk_) {
		/* PUSH ES; PUSHA; PUSH DS; MOV AX,dataSeg; MOV DS,AX; CALL 09BC;
		   POP DS; POPA; POP ES; IRET
		   09BC clobbers AX/CX/ES — without a full save, IRQ mid REP STOSW
		   (MAP VRAM clear @32C2) never finishes and song load never runs. */
		mem[tramp + 0] = 0x06; /* PUSH ES */
		mem[tramp + 1] = 0x60; /* PUSHA */
		mem[tramp + 2] = 0x1E; /* PUSH DS */
		mem[tramp + 3] = 0xB8;
		mem[tramp + 4] = (uint8_t)(dataSeg & 0xff);
		mem[tramp + 5] = (uint8_t)(dataSeg >> 8);
		mem[tramp + 6] = 0x8E;
		mem[tramp + 7] = 0xD8;
		/* disp = 09BC - (FE86+8+3) = 09BC - FE91 = 0B2B */
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
		/* Keep IVT08 on the trampoline if something rewrote it. */
		mem[tramp + 4] = (uint8_t)(dataSeg & 0xff);
		mem[tramp + 5] = (uint8_t)(dataSeg >> 8);
		mem[0x08 * 4 + 0] = (uint8_t)(trampOff & 0xff);
		mem[0x08 * 4 + 1] = (uint8_t)((trampOff >> 8) & 0xff);
		mem[0x08 * 4 + 2] = (uint8_t)(mapSeg & 0xff);
		mem[0x08 * 4 + 3] = (uint8_t)((mapSeg >> 8) & 0xff);
	}
	picMask_ = (uint8_t)(picMask_ & 0xfeu);
	np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
	/* Arm soft IRQ0 even before OUT 10A — boot waits on [CC4D] via this tick. */
	olteusTimerOn_ = 1;
	/* Skip MAP VRAM plane clears (CS:32C2 REP STOSW ×4). Audio does not
	   need them; under host IRQ0 they burn the play budget and never finish. */
	if (base + 0x32C2u < 0x200000u && mem[base + 0x32C2] == 0x8B)
		mem[base + 0x32C2] = 0xC3;
}

static int IvtHooked(uint8_t vec, int dosMode)
{
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	unsigned b = (unsigned)vec * 4u;
	uint16_t off = (uint16_t)(mem[b] | (mem[b + 1] << 8));
	uint16_t seg = (uint16_t)(mem[b + 2] | (mem[b + 3] << 8));
	/* Null vector. SORC98 installs handlers at 0000:xxxx (CS=0) — that is
	   valid; only reject 0000:0000. Also reject PC BIOS ROM (F000) and
	   PC-98 high-ROM aliases (F800–FFFF) left by a corrupt INT 18 boot. */
	if (seg == 0 && off == 0) return 0;
	if (seg >= 0xF000) return 0;
	if (dosMode && seg == DOS98_TRAMP_SEG) return 0;
	/* DOS INT08 into the BIOS work page is not a PIT ISR. Treating 0000:05xx
	   as hooked fires IRQ0 at PIT rate and starves PMD's OPN Timer B. */
	if (dosMode && vec == 0x08 && seg == 0 && off < 0x800)
		return 0;
	return 1;
}

/* Old MMD /I auto ISR (50 52 BA D2 E0…) latches CS:[imm] on DSR but does
   not IN E0D0. Host CLI-respect + edge IRQ can miss the CX=0 poll window;
   poke the latch once INT 0E is that ISR and the guest has STI'd. */
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

static void PlantFmdSongBank(CEmuDos98* dos)
{
	/* FMD CS:[0007] starts as a small heap next to the TSR. AH=2 then
	   AH=3F-reads the .GS (up to ~30KB) there and overwrites the next COM
	   (fugam). AH=1 XOR-decodes tracks into the same arena (TLOVE_13 ~30KB). */
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

int CHardPc98::Int60Hooked() const
{
	return IvtHooked(0x60, isDos_);
}

int CHardPc98::DeliverIrqs()
{
	if (g_pc98Eoi) {
		g_pc98Eoi = 0;
		/* MMD2 acks YM (27h=2Ah) then PIC EOI. Level-triggered ymfm
		   re-asserts before IRET; keeping opnInService_ latched then
		   starves AH=3's STI wait and the HLT idle (michael pick 1
		   line/svc tens of millions, irq frozen). */
		if (g_mmdPicIsr)
			opnInService_ = 0;
		/* Do not clear opnInService_ on PIC EOI alone for other cores.
		   YM2608 IRQs are level-triggered; clearing here before the ISR
		   acks timer status (reg 0x27 / status read) re-enters forever
		   and hangs PumpCycles (pc88vados tetrisva/shinrava). */
	}
	/* Release when the injected IRQ frame IRETs back onto the pre-INT
	   stack. Line-drop used to do this and nested OPNDRV (STI before
	   EOI) after the status IN acked YM. NOPNDRV still IRETs after
	   0x27 ack, so the unwind covers that path too. */
	if (opnInService_) {
		const uint16_t ss = np2_reg_get(NP2_R_SS);
		const uint16_t sp = np2_reg_get(NP2_R_SP);
		/* Exact SS:SP — not SP>=. OPNDRV INT D2 and INT0B both switch
		   SS=CS; the ISR's SP=24E4 sits above INT D2's SP=0180, so SP>=
		   treated the private stack as IRET'd and nested ~200k IRQs/s. */
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

	/* MUSIC.COM long-BGM keepalive: while play-enable [0290]=1, hold the
	   auto-mute counter [0294] at 0 so AH=2's 16-bar mute never trips.
	   Also clear the channel-mute bytes [ch+3] that AH=1 leave stuck. */
	if (musicComKeepalive_ && IvtHooked(0x70, 1)) {
		uint8_t* mem = np2_mem();
		if (mem) {
			const unsigned s70 = (unsigned)mem[0x70 * 4 + 2]
				| ((unsigned)mem[0x70 * 4 + 3] << 8);
			const unsigned b70 = s70 << 4;
			if (b70 + 0x2A0u < 0x200000u && mem[b70 + 0x290] == 1) {
				mem[b70 + 0x294] = 0;
				/* Channel control blocks sit at CS:0003/0013/… — bit0 mute. */
				for (unsigned ch = 0; ch < 6; ++ch) {
					const unsigned off = b70 + 0x03u + ch * 0x10u;
					if (off < 0x200000u && (mem[off] & 1))
						mem[off] = (uint8_t)(mem[off] & ~1u);
				}
			}
		}
	}
	/* 46oku / MUSIC.COM: also poke INT14 CS when keepalive is armed but
	   INT70 was not the park vector (fakecall mirror). */
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
		/* Speaker rips (BGML_98) sequence notes on IRQ0. After INT 7F they
		   often sit in a CLI wait; without IF the PIT never reaches INT08
		   and MixBeep is a DC gate. Real BIOS would still raise IRQ0.
		   FMD intelligent helpers CLI around MPU ACK — forcing IF here
		   lets INT 0E steal FE and 0713 hangs or RET-smashes (portOut=2). */
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
		const int fmdIntel = modeMidi_ && !mpuUart_ && IvtHooked(0x0E, isDos_);
		if (!fmdIntel) {
			flags = (uint16_t)(flags | 0x0200);
			np2_reg_set(NP2_R_FLAGS, flags);
		}
	}
	/* Old MMD /I auto STI, prints via INT 21 AH=9, then polls [1778] for a
	   CTH byte on INT 0E. AH=9 can return with IF clear; treating any INT 0E
	   hook as FMD then refuses MPU IRQs and /I auto STC-fails (int61).
	   Only FMD shares INT 0E CS with INT D3. */
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
	if ((flags & 0x200) == 0) { /* IF clear */
		if (chip_ && chip_->Irq()) g_censIfOff++;
		return 0;
	}
	/* MMD2 ISR never STI. IF set means it IRET'd; ymfm may still hold
	   the level line so the old latch starved the HLT idle. */
	if (g_mmdPicIsr)
		opnInService_ = 0;
	if (chip_ && chip_->Irq()) {
		g_censLine++;
		if (opnInService_) g_censSvc++;
	}

	if (pitIrqPending_ && (picMask_ & 0x01) == 0 && !g_pitInService) {
		/* PMD's clock is OPN Timer B (IRQ3). A guest-programmed PIT plus a
		   real INT08 CS starves that ISR (~3kHz IRQ0 in 250ms) and leaves
		   IF=0, so key-ons freeze. Drop IRQ0 once the OPN vector is live. */
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
	/* FMD parks the MPU DSR ISR on INT 0E (IRQ6). Command ACK FE is
	   polled by CS:0713 under CLI — raising IRQ6 on FE steals the byte.
	   Clock-to-host F8 is unsolicited and goes through the ISR. */
	if (modeMidi_ && !mpuUart_ && (picMask_ & 0x40) == 0 && !g_mpuInService
		&& guestIf && IvtHooked(0x0E, isDos_)) {
		int wantMpuIrq = 0;
		if (mpuRxFull_)
			wantMpuIrq = 1;
		else if (mpuAckR_ != mpuAckW_) {
			const uint8_t front = mpuAckQ_[mpuAckR_ & 31];
			/* FE is polled by FMD CS:0713 under CLI — IRQ would steal it.
			   F8 (clock) and FD (FMD sequencer tick) must interrupt. */
			if (front != (uint8_t)0xfe)
				wantMpuIrq = 1;
		}
		if (wantMpuIrq) {
			/* Edge, not level: old MMD probe never INs the CTH byte, so a
			   level line would nest INT 0E until the 8s shell budget. */
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
	/* Level-triggered OPN IRQ (matches PC88). Edge latch alone missed
	   asserts that happened in TickOpn after the previous DeliverIrqs. */
	if (chip_ && chip_->Irq() && !opnInService_) {
		uint8_t* mem = np2_mem();
		/* famistava plants OPN on INT14 during play — mirror only when INT0B
		   is still vacant (rtype keeps an INT0A thunk on INT0B). */
		/* Guest OPN ISR on INT14 (MDR / famistava / MUSE-class): DeliverIrqs
		   ticks INT0B. Mirror while 0B is still the trampoline; skip lone
		   IRET serial stubs. */
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
		/* mbmusp/MUSE: SSG I/O A = 0xC0 → driver hooks INT14 and EOIs the
		   slave. Deliver there (do not mirror onto INT0B). */
		uint8_t vec = PC98_OPN_IRQ_VEC;
		/* PMD owns IRQ3/INT0B (Timer B). INT14 is a DOS/MUSE hook — sending
		   OPN there runs the wrong ISR, 30 IRQs then silence. */
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
		/* USMD overlay plants the YM sequencer on INT15 (IRQ13). INT14 is
		   only a master-PIC chain stub (IN AL,2 / far old 14); delivering
		   there left keyOn=0 with opnInService stuck on the level line.
		   Unpacked titles sit overlay at CS+0x470/471; PIYO+EXEPACK packs
		   leave INT7E and INT15 on different load copies (es95: +0x772). */
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
		} /* !pmdOpnIrq_ — keep PMD on INT0B */
		if (!IvtHooked(vec, isDos_)) {
			/* Do not fall back to VSYNC (0x0A) or other IRQ lines — that
			   mis-delivered OPN timer IRQs into SORC98's VSYNC stub. */
			g_censNoVec++;
			return 0;
		}
		/* Guest ISR OUT 02h often restores a boot-time IMR that still
		   masks IRQ3. PMD-class and INT14-mirrored drivers need the tick. */
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

/* --- PC-98 MPU-401 UART @ E0D0/E0D2 (FMP3 -m / midiout catalog) ----------- */

static uint8_t s_pc98MidiRun, s_pc98MidiNeed, s_pc98MidiD0;

void CHardPc98::MidiCaptureReset()
{
	if (!midiBytes_) midiBytes_ = new uint8_t[CEMU_PC98_MIDI_CAP];
	if (!midiDelta_) midiDelta_ = new uint32_t[CEMU_PC98_MIDI_CAP];
	midiCount_ = 0;
	midiNoteOnCount_ = 0;
	midiPortOutCount_ = 0;
	midiLastCycle_ = cpuCycles_;
	mpuRxFull_ = 0;
	/* Do NOT queue a power-on FE here. FMP3 -m detect (CS:1790) INs E0D2
	   and requires status != 0 && bit6 clear (idle 0x80). A pending ACK
	   makes MidiStatusIn return 0x00, so detect fails → [1DBE]=0 and the
	   sequencer never emits MIDI (midiBytes=0). FE is pushed from RESET
	   (FFh) / UART-mode (3Fh) command handlers only. */
	mpuAckR_ = mpuAckW_ = 0;
	g_mpuIrqAsserted = 0;
	mpuResetBusy_ = 0;
	mpuResetUntil_ = 0;
	s_pc98MidiRun = s_pc98MidiNeed = s_pc98MidiD0 = 0;
	g_mpuTrI = g_mpuTrN = 0;
	g_mpuLastSt = 0xff;
}

void CHardPc98::MidiPushAck(uint8_t v)
{
	mpuAckQ_[mpuAckW_ & 31] = v;
	mpuAckW_++;
}

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

	/* SysEx (F0..F7) and realtime (F8..FF) must not become running status
	   or the GS bulk dump leaves F0 armed and play notes never count. */
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
	/* Data inside SysEx is not channel voice. A new 80-EF status must
	   abort an un-terminated F0 (NARU / GS dumps that drop F7) or note-ons
	   after the dump never count. Matches PC/AT MidiNoteOnCount. */
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

void CHardPc98::MidiDataOut(uint8_t data)
{
	MpuTrace(0xE0D0, 1, data);
	midiPortOutCount_++;
	/* Intelligent cmds E0/E7/… take the next data-port byte as payload,
	   not as MIDI. FMD play is `OUT E0` then `OUT [1798]` as tempo. */
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
	/* FMP3 -m probe loops OUT 00h to E0D0 during resident install and would
	   fill the capture buffer with zeros before the first song byte. */
	if (!midiCapArmed_)
		return;
	if (mpuUart_ || modeMidi_) {
		MidiCaptureByte(data);
		/* FMP3 -m emits a standard MIDI UART stream (same as Wolf MUSDRV).
		   Bridge to OPN so probes / non-VST paths have audible output. */
		if (wolfBridgeEnable_)
			WolfCmdByte(data);
	}
}

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
		/* FMD AH=0: OUT FFh then IN E0D2; AND 40h / JZ fail. The first
		   status read after RESET must have bit6 set. ACK FE is queued
		   as a side effect of that poll so CALL 0296 can drain it. */
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
	/* MPU-401 intelligent firmware. FMD /# never sends 3Fh; it talks
	   E0/C2/E7/95 and expects version/tempo replies, not UART capture. */
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
	case 0xac: /* request version */
		MidiPushAck(0xfe);
		MidiPushAck(0x15);
		return;
	case 0xad: /* request revision */
		MidiPushAck(0xfe);
		MidiPushAck(0x01);
		return;
	case 0xaf: /* request tempo */
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
	/* MMD.COM init ends with B9h then /I auto waits for CTH FD on INT 0E.
	   94h left the clock off; without this the probe never latches.
	   Seed residual with one full period so the first FD is produced on
	   the next empty-queue TickSide (old MMD's CX=1000 poll is << 16ms). */
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

void CHardPc98::MpuFinishReset()
{
	if (!mpuResetBusy_)
		return;
	/* Consumed by the first status poll — see MidiStatusIn. TickSide must
	   not retire RESET or FMD's IN after OUT FFh sees ready and fails. */
}

void CHardPc98::MpuClockTick()
{
	if (!mpuClockToHost_ || mpuUart_ || mpuResetBusy_)
		return;
	/* Do not clobber a pending command ACK — FMD's helper waits for FEh. */
	if (mpuAckR_ != mpuAckW_)
		return;
	/* FMD's INT 0E ISR special-cases FD (CALL 0748 → 07D3 sequencer).
	   MMD.COM /I auto does the same: the probe ISR only latches [18A9]
	   on FD, so generic F8 left INT 61 uninstalled. Any guest INT 0E
	   MPU ISR wants FD; UART mode never reaches here. */
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

uint8_t CHardPc98::MidiStatusIn()
{
	/* Standard MPU-401 status (Roland / RBIL / FMP3):
	   bit7=1 → no RX data; bit6=1 → not ready for write.
	   Idle ready = 0x80. FMP3 Wait1 accepts only nonzero+bit6clear; OUT
	   helpers spin while bit6 set. Returning 0x40 (PC/AT swapped polarity)
	   made FMP3 -m detect fail (midiBytes=0) and hang any E0D0 write. */
	uint8_t st;
	if (mpuResetBusy_) {
		mpuResetBusy_ = 0;
		MidiPushAck(0xfe);
		st = 0xC0; /* this poll: write busy. next poll sees ACK. */
	} else if (mpuAckR_ != mpuAckW_ || mpuRxFull_)
		st = 0x00; /* RX available, write OK */
	else
		st = 0x80; /* no RX, write OK */
	if (st != g_mpuLastSt) {
		MpuTrace(0xE0D2, 0, st);
		g_mpuLastSt = st;
	}
	return st;
}

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

/* --- Wolfteam E0D0 MUSDRV command stream → OPN soft bridge ---------------

   Confirmed by capture (.cursor/_cemu_wolf_e0d0_probe): the Wolfteam MUSDRV
   timer ISR emits a *standard MIDI byte stream* out port E0D0 (an MPU-401 /
   MIDI-board UART). It never programs the YM2203 for notes; the FM chip only
   receives an init/mute block. Titles that detect no MIDI board still stall.

   Since Hoot has no MIDI-board synth, we translate the live MIDI stream into
   YM2203/2608 FM voices so the song is actually audible on the OPN. This is a
   real (if reduced-polyphony) synthesis of the observed note events — a
   generic FM patch is voiced per note-on and keyed off on note-off. */

/* One-octave OPN(A) F-number table (C..B); block carries the octave. The same
   values work for OPN (3.9936 MHz /72) and OPNA (7.9872 MHz /144) since both
   yield the ~55.5 kHz FM base rate. */
static const uint16_t kWolfFnum[12] = {
	0x0269, 0x028E, 0x02B4, 0x02DE, 0x030B, 0x0339,
	0x036B, 0x03A0, 0x03D7, 0x0412, 0x0450, 0x0492
};

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

void CHardPc98::WolfOpnW(int bank, uint8_t reg, uint8_t val)
{
	if (!chip_) return;
	chip_->Write((uint32_t)bank, reg);
	chip_->Write((uint32_t)(bank | 1), val);
}

/* Voice v: 0-2 → FM1-3 (bank0), 3-5 → FM4-6 (bank1, OPNA only). Program a
   generic 4-carrier (algorithm 7) patch with level scaled by velocity and the
   MIDI channel volume/expression. */
void CHardPc98::WolfProgramVoice(int v, int vel, int midiCh)
{
	const int bank = (v < 3) ? 0 : 0x100;
	const int ci = (v < 3) ? v : (v - 3);
	int eff = vel;
	eff = eff * (int)wolfChVol_[midiCh & 15] / 127;
	eff = eff * (int)wolfChExpr_[midiCh & 15] / 127;
	if (eff < 0) eff = 0; if (eff > 127) eff = 127;
	/* Louder notes → smaller TL (0x10 loudest .. ~0x38 quiet). */
	uint8_t tl = (uint8_t)(0x10 + ((127 - eff) * 40 / 127));
	for (int op = 0; op < 4; op++) {
		const uint8_t o = (uint8_t)((op << 2) + ci);
		WolfOpnW(bank, (uint8_t)(0x30 + o), 0x01); /* DT=0 MUL=1 */
		WolfOpnW(bank, (uint8_t)(0x40 + o), tl);   /* TL */
		WolfOpnW(bank, (uint8_t)(0x50 + o), 0x1F); /* KS=0 AR=31 */
		WolfOpnW(bank, (uint8_t)(0x60 + o), 0x00); /* DR=0 */
		WolfOpnW(bank, (uint8_t)(0x70 + o), 0x00); /* SR=0 */
		WolfOpnW(bank, (uint8_t)(0x80 + o), 0x0A); /* SL=0 RR=10 */
		WolfOpnW(bank, (uint8_t)(0x90 + o), 0x00); /* SSG-EG off */
	}
	WolfOpnW(bank, (uint8_t)(0xB0 + ci), 0x07); /* algorithm 7, FB 0 */
	WolfOpnW(bank, (uint8_t)(0xB4 + ci), 0xC0); /* L+R on */
}

void CHardPc98::WolfNoteOn(int midiCh, int note, int vel)
{
	if (!chip_ || note < 0 || note > 127) return;
	wolfNoteOnCount_++;
	const int nv = wolfVoiceCount_ > 0 ? wolfVoiceCount_ : 3;
	/* Reuse a voice already holding this (ch,note); else a free one; else the
	   oldest active voice. */
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
	/* Key off before retune to force a clean re-attack. */
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
	WolfOpnW(0, 0x28, (uint8_t)(0xF0 | chBits)); /* key on all 4 slots */
	wolfVoiceActive_[v] = 1;
	wolfVoiceMidiCh_[v] = midiCh;
	wolfVoiceNote_[v] = note;
	wolfVoiceAge_[v] = ++wolfVoiceClock_;
}

void CHardPc98::WolfNoteOff(int midiCh, int note)
{
	if (!chip_) return;
	wolfNoteOffCount_++;
	const int nv = wolfVoiceCount_ > 0 ? wolfVoiceCount_ : 3;
	for (int i = 0; i < nv; i++) {
		if (wolfVoiceActive_[i] && wolfVoiceMidiCh_[i] == midiCh && wolfVoiceNote_[i] == note) {
			const int chBits = (i < 3) ? i : (0x04 + (i - 3));
			WolfOpnW(0, 0x28, (uint8_t)chBits); /* key off */
			wolfVoiceActive_[i] = 0;
			wolfVoiceNote_[i] = -1;
		}
	}
}

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
		if (d0 == 0x07) wolfChVol_[ch] = d1;        /* channel volume */
		else if (d0 == 0x0B) wolfChExpr_[ch] = d1;  /* expression */
		else if (d0 == 0x78 || d0 == 0x7B) WolfAllNotesOff(); /* all sound/notes off */
		break;
	default:
		/* Program change / pitch bend / aftertouch: not voiced by this bridge. */
		break;
	}
}

/* Parse the raw MIDI byte stream (handles running status, 2/3-byte channel
   messages, sysex skip, and 0xFF stream reset). */
void CHardPc98::WolfCmdByte(uint8_t data)
{
	if (data & 0x80) {
		if (data >= 0xF8)
			return; /* realtime: ignore */
		if (data == 0xF0) { wolfInSysex_ = 1; return; }
		if (data == 0xF7) { wolfInSysex_ = 0; wolfRunStatus_ = 0; return; }
		if (data == 0xFF) { /* system reset within stream */
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
		/* Channel voice status. */
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
	wolfDataIdx_ = 0; /* running status: keep wolfRunStatus_ */
	if (wolfBridgeEnable_)
		WolfMidiDispatch(wolfRunStatus_, wolfData_[0], wolfData_[1]);
}

uint8_t CHardPc98::PortIn(uint16_t port)
{
	port = Pc98FoldPitAlias(port);
	/* PC-98 display status: bit 5 changes across vertical retrace.  Several
	   resident glues synchronize command hand-off by waiting for a low->high
	   transition (mscd_98 does this for 18 frames).  Returning the generic
	   open-bus FF here trapped those programs in their first wait loop. */
	/* Both µPD7220s answer here: 0x60 is the text master, 0xA0 the graphic
	   slave.  Only 0xA0 used to be answered, so a program that frame-synced
	   off the text GDC (C-Class FMX waits for vsync to fall and rise before
	   probing the sound board) spun in its first wait loop forever. */
	if (port == 0x0060 || port == 0x00A0) {
		uint8_t s;
		if (port == 0x00A0) {
			/* tky98 glue waits for bit5 to fall and rise 60 times before
			   INT F1 play. A 60 Hz clock needs ~1s; DrainInterrupt is 0.5s
			   so older TKYDRV packs never left the wait (dumps=1). Toggle
			   every poll — OPN timers still pace the song. */
			gdcA0Poll_ = (uint8_t)(gdcA0Poll_ + 1);
			s = (uint8_t)((gdcA0Poll_ & 1) ? 0x20 : 0x00);
		} else {
			const uint64_t halfFrame =
				(cpuHz_ > 120) ? (uint64_t)cpuHz_ / 120ull : 1ull;
			s = ((cpuCycles_ / halfFrame) & 1ull) ? 0x20 : 0x00;
			/* An idle GDC has drained its command FIFO and is not drawing; a
			   caller that waits for FIFO-empty before writing needs to see it.
			   Only 0x60 reports it: 0xA0 has answered bare vsync since the
			   glues that poll it were tuned, and they mask for bit 5 anyway. */
			s |= 0x04;
		}
		return s;
	}
	/* PC-88VA: PC-88 OPN ports read the same chip status/data. */
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
		/* olteus MAP DA40: IN 44h / TEST 80h busy-wait. ymfm stays busy
		   unless clocks advance between OUT and IN — mask for VA play. */
		if (pc88VaIo_)
			s = (uint8_t)(s & (uint8_t)~0x80);
		/* MMD2.SYS ISR 0x3ff / 0x4a3: OUT addr / IN 188h / TEST 80h.
		   Nested INT14 has IF clear, so a sticky ymfm busy bit parks the
		   ISR forever (opnInService stuck, key-on 0x28 never written). */
		if (g_mmdPicIsr)
			s = (uint8_t)(s & (uint8_t)~0x80);
		return s;
	}
	case OPN_DATA0:
		/* SSG I/O A (reg 0x0E): board IRQ jumper. MUSE/mbmusp read bits7-6
		   to pick INT14h; default open-bus 0 makes them hook INT0B while EOI
		   goes to the slave (hootrip preset_muse_irq_jumper). */
		if (chip_ && opnLatchedAddr_ == 0x0E && (ssgPortAJumper_ & 0x80))
			return ssgPortAJumper_;
		/* YM2203/2608 SSG $00-$0F are readable. PLAY5 / MMD2.SYS / F.COM
		   write a canary (0x55 or 1) and IN-compare; ymfm read_data() is
		   status, not the register. Serve the last DATA0 write. */
		if (opnLatchedAddr_ <= 0x0F)
			return ssgEcho_[opnLatchedAddr_];
		/* Old TKY/OPNDRV (c2gp, dynamo98) probes YM by OUT 27h/40h then
		   IN DATA expecting 0x40, then OUT addr FFh / IN DATA not-1.
		   Real YM2203 27h is write-only; PC-98 boards bus-hold the last
		   data-port write. Newer OPNDRV NOPs both compares (rolling95).
		   Only those two latched addrs echo: a blanket DATA0 latch moved
		   rolling95's first audible window (SIL.MDT fp). */
		if (g_opnBusHold && (opnLatchedAddr_ == 0x27 || opnLatchedAddr_ == 0xFF))
			return g_opnDataLatch;
		return chip_ ? chip_->ReadData() : 0xff;
	case OPN_ADDR1: {
		if (modeSorch_)
			return opl_ ? opl_->ReadStatus() : 0x06; /* OPL2 ID pattern */
		uint8_t s = chip_ ? chip_->ReadStatusHi() : 0xff;
		if (pc88VaIo_)
			s = (uint8_t)(s & (uint8_t)~0x80);
		return s;
	}
	case OPN_DATA1:
		if (modeSorch_) return 0xff; /* OPL2 has no readable data port */
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
		/* PC-9801-86 @ 0188h: upper nibble Sound ID = 4 (MAME/NP2/Undocumented9801).
		   bit0 = YM2608 enhanced; bit1 = OPNA mask. Default mask=0 → ID 0x40. */
		if (!opnaMode)
			return 0xff; /* 26K / OPN-only: port absent */
		return (uint8_t)(0x40 | (sound86Mask_ & 0x03));
	/* A466–A66E: leave open-bus unless a title needs soft 86PCM.
	   Stubbing empty-FIFO here made FMP3 take a silent PCM path (vg2). */
	case 0x506:
		/* PC-88VA: MAP polls IN 506h bit0 as busy (olteus CS:7968).
		   Open-bus 0xFF spun forever before song load / sequencer. */
		if (pc88VaIo_)
			return 0x00;
		return 0xff;
	case PIC_CMD:
	case SLAVE_PIC_CMD:
		/* OCW3 IRR/ISR polls (e.g. ys_98 MANPR1 CS:5123 after arming
		   timer B). Unhandled reads were 0xFF and spun forever (opnW
		   hundreds of thousands, key=0). Soft-PIC has no latched ISR.
		   PC-88VA MAP (olteus CS:0C50): when DS:[00C0]!=0 wait for bit6
		   then clear; when [00C0]==0 bit6 must be clear or it re-spins.
		   MMD2 INT14: IN master ISR bit7 decides whether to EOI the slave.
		   Returning 0 skipped OUT 08h,20h and left IRQ12 in-service. */
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
		/* System PPI port A = DIP SW2. QEMU returns 0x73 in input mode. */
		return 0x73;
	case PPI_B:
		/* TYP=10 (not original 9801), MOD=1 (8 MHz / 2 MHz PIT). */
		return 0xA0;
	case PPI_C:
		return ppiC_;
	case 0x41:
		/* Keyboard 8251 data. No scan code queued. */
		return 0x00;
	case 0x43:
		/* 8251 status (TxRDY|TxEMPTY) / system port: printer not busy. */
		return 0x06;
	case WOLF_SYNC0:
	case 0xC0D0: /* alternate PC-98 MIDI data port */
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

void CHardPc98::PortOut(uint16_t port, uint8_t data)
{
	port = Pc98FoldPitAlias(port);
	/* olteus_va: OUT 10A,0022 arms the picture/interval tick; 00/0C disarms.
	   Capture CS as MAP seg if the far-table hook has not run yet. */
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
	/* PC-88VA: music uses classic PC-88 OPN (44h/A8h); BIOSD also pokes
	   PC-98 188h. Stage address per port family so interleaved OUTs cannot
	   steal the latch (SSG C / mixer corruption). */
	if (pc88VaIo_) {
		switch (port) {
		case 0x44: case 0xA8:
			/* Latch + commit address. BPS tetrisva OPNA detect does
			   OUT 44h,FFh / IN 45h and expects ym2608 ID code 01 — without
			   Write(0) the chip address stays stale and [851A] never sets. */
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
			break; /* fall through — keep PC-98 path with re-assert */
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
		/* On a SOUND ORCHESTRA these two ports are a whole second chip, not
		   the OPNA's high bank: sending them to the OPN is what made the
		   board sound like a plain OPN however the mode was selected. */
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
		/* midiout / FMP -m: capture UART MIDI. Wolfteam FM: command bridge. */
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
			if (s_valkyKeepIrq0)
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
			/* 8255 bit set/reset: 0x06 clears bit3 (speaker on), 0x07 sets it. */
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
		/* Preserve Sound ID nibble; update mask/enhance bits (MAME mask_w). */
		if (opnaMode)
			sound86Mask_ = (uint8_t)(data & 0x03);
		break;
	case SOUND86_FIFO_STAT:
	case SOUND86_FIFO_CTL:
	case SOUND86_DAC_CTL:
	case SOUND86_FIFO_DAT:
	case SOUND86_MUTE:
		/* Accept writes so probes don't fault; no soft PCM engine yet. */
		if (opnaMode) {
			if (port == SOUND86_FIFO_CTL) sound86FifoCtl_ = data;
			else if (port == SOUND86_DAC_CTL) sound86DacCtl_ = data;
			else if (port == SOUND86_MUTE) sound86Mute_ = (uint8_t)(data & 1);
		}
		break;
	default: break;
	}
}

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

	/* Rhythm ROM before the DOS branch: ADPCM-A reads of an empty ROM decode
	   into a wrapping accumulator ramp, so FMP/PMD drum tracks came out as
	   sawtooth noise on every pc98dos title. */
	if (opnaMode && chip_)
		CEmuLoadExternalYm2608Adpcm(chip_);

	if (isDos_)
		return BootDos(fs, ge, titleCode);

	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "code") != 0 && _stricmp(r->type, "binary") != 0
			&& _stricmp(r->type, "string") != 0)
			continue;
		/* KOEI packs code as seg:off dword (0xSSSSOOOO); others use flat phys. */
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
		/* DOFMD_98 and BRANM_98 share the INT 45 + host-0x11 play path
		   (seg:off song ptr via 07D4/07D6). BRANM skips INT 14h. */
		if (r->name[0] && (_strnicmp(r->name, "DOFMD", 5) == 0
			|| _strnicmp(r->name, "BRANM", 5) == 0))
			dofmd_ = 1;
		if (r->name[0] && _strnicmp(r->name, "FMD98", 5) == 0)
			fmd98_ = 1;
		if (r->name[0] && _stricmp(r->name, "RX.BIN") == 0)
			rx98_ = 1;
		/* Ys2 / Brandish-era Falcom OPN driver without RX.BIN glue name. */
		if (r->name[0] && (_stricmp(r->name, "2608.BIN") == 0
			|| _stricmp(r->name, "2203.BIN") == 0
			|| _stricmp(r->name, "10_005.BIN") == 0)
			&& dataAddr_ <= 0 && fileSize_ > 0)
			rx98_ = 1;
		/* Falcom PROG.BIN glue: only when catalog dataaddr is set (no invent). */
		if (data && n >= 0x90 && n <= 512 && dataAddr_ > 0 && fileSize_ > 0
			&& r->name[0] && _stricmp(r->name, "PROG.BIN") == 0) {
			prog98_ = 1;
			progSongAddr_ = dataAddr_;
		}
		if (r->name[0] && _strnicmp(r->name, "KOEI98", 6) == 0)
			koei98_ = 1;
		/* BirdySoft CAL/PAL/BEAST family: glue stub + OPN driver install INT60
		   but never hook IRQ3 (IVT 0x0B). Play spins on wait-flag [DS:269B]
		   until the relocated OPN ISR runs. Detect by stub/bin name. */
		if (r->name[0] && (_strnicmp(r->name, "CAL", 3) == 0
			|| _strnicmp(r->name, "PAL", 3) == 0
			|| _strnicmp(r->name, "THANATOS", 8) == 0
			|| _strnicmp(r->name, "BEAST", 5) == 0
			|| _strnicmp(r->name, "BST3", 4) == 0))
			cal98_ = 1;
		/* Beast3: 64K OPN driver at 0xFC00; glue cmd0 uses AH!=0 to pick
		   load (AH==0 is stop). Small title codes never take the load path. */
		if (r->name[0] && (_strnicmp(r->name, "BST3", 4) == 0
			|| _stricmp(r->name, "0FC00.BIN") == 0))
			bst398_ = 1;
		/* QueenSoft MADP: catalog binary at 0x100 plants INT40 → driver
		   (AL-indexed API @0xA000/0x7000). Glue INT7F maps cmd→INT40 AL. */
		if (r->name[0] && _strnicmp(r->name, "MADP", 4) == 0)
			madp98_ = 1;
		if (r->name[0] && _strnicmp(r->name, "N3GOLF", 6) == 0)
			n3golf98_ = 1;
		/* KSK DKS/FQ family: dks.bin/fq3.bin glue + BGMDK/BGMDRV @0x35000.
		   INT7F cmd1 → INT69 AH=0; songs are size-prefixed banks; host 07D4/07D6
		   are real-mode ES:BX (table/BSS). No catalog dataaddr → cmd1 never ran. */
		if (r->name[0] && (_stricmp(r->name, "DKS.BIN") == 0
			|| _stricmp(r->name, "FQ3.BIN") == 0
			|| _strnicmp(r->name, "BGMDK", 5) == 0
			|| _strnicmp(r->name, "BGM_DS", 5) == 0
			|| _strnicmp(r->name, "BGMFQ", 5) == 0
			|| _strnicmp(r->name, "BGMDRV", 6) == 0))
			dks98_ = 1;
		/* Glodia MDPLAY.BIN (etembl/ragnrk/biblem2): INT7F play uses INT 4A/40.
		   Driver installs INT40–4D and a PIT ISR, but the ISR is only written to
		   IVT08 from a late path — ensure INT08 is hooked after boot. MDPLAYD
		   (difrlm) already installs INT08 in init and must stay untouched. */
		if (r->name[0] && (_stricmp(r->name, "MDPLAY.BIN") == 0
			|| _strnicmp(r->name, "MDPLAY", 6) == 0
			|| _stricmp(r->name, "MDRIVE.BIN") == 0
			|| _strnicmp(r->name, "MDRIVE", 6) == 0)
			&& _strnicmp(r->name, "MDPLAYD", 7) != 0)
			mdplay98_ = 1;
		/* gulfwr nests boot as 1/000_BOOT — match by basename. */
		{
			const char* bootBase = r->name;
			const char* slash = strrchr(r->name, '/');
			if (!slash) slash = strrchr(r->name, '\\');
			if (slash) bootBase = slash + 1;
			if (r->name[0] && _stricmp(bootBase, "000_BOOT") == 0) {
			wolfteam98_ = 1;
			/* Discover relocated play-gate / flag / title BSS via d_98 opcode
			   context. Sibling MU* boots keep the same pre/post bytes but move
			   abs16 (gou 560C/062F/5EFE, zan2 57AA/062F/6E84, …). Writing the
			   d_98-only 0662 assist into relocated boots can force silence. */
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
				/* Classic: OUT 64 / POP ES / POP DS / POPA / IRET.
				   dmdply: OUT 64 / POPA / POP DS / POP ES / IRET. */
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
					/* Canonical layout: play = stop+0x12, song far-ptr @stop+0x15.
					   dmdply folds play into flagA+2 (060B) — still usable. */
					wolfGateStop_ = gs;
					wolfGatePlay_ = gp;
					if ((uint16_t)(gp - gs) == 0x0012)
						wolfSongPtr_ = (uint16_t)(gs + 0x15);
					else
						wolfSongPtr_ = (uint16_t)(gs + 0x15);
				}
				/* INT4C play-armed byte: C6 06 fa,FF / OUT 64h / … / IRET */
				for (unsigned p = 0; p + 11 < n; p++) {
					if (data[p] != 0xC6 || data[p + 1] != 0x06 || data[p + 4] != 0xFF)
						continue;
					if (memcmp(data + p + 5, kFlagPost, 6) == 0
						|| memcmp(data + p + 5, kFlagPostAlt, 6) == 0) {
						wolfFlagA_ = (uint16_t)(data[p + 2] | (data[p + 3] << 8));
						break;
					}
				}
				/* Title word: CMP [tw],AX / JE / MOV [tw],AX / C6 [fa+1],FF */
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
				/* Song shadow buffer: MOV SI/DI,imm near INT 4C (AH=08 path).
				   dmdply has no INT4C — detect REP STOSW clear of DI buffer. */
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

	/* biblem2 OPN twin boots MAIN.EXE (CS=6000) and stays silent. Zip also
	   ships etembl-identical mdplay.bin — stage it at 0x600 and boot CS=0060
	   like ragnrk/etembl (MDDRV already at 0x10000; FMV at dataaddr). */
	if (mdplay98_ && bootCs_ == 0x6000 && fs) {
		unsigned sz = 0;
		const unsigned char* stub = CEmuZipFsFind(fs, "mdplay.bin", &sz);
		if (stub && sz >= 64 && sz <= 256) {
			memcpy(mem + 0x600, stub, sz < 0x200u ? sz : 0x200u);
			bootCs_ = 0x0060;
			bootIp_ = 0;
		}
	}

	/* DOFMD_98.BIN boot: INT 45h (MSC init) then INT 14h, then hooks INT 7Fh.
	   Without a BIOS serial stub INT 14h vector is 0000:0000 and boot never
	   reaches the INT 7Fh install — park a lone IRET below the glue at 0x600. */
	if (dofmd_) {
		mem[0x500] = 0xCF;
		mem[0x14 * 4 + 0] = 0x00;
		mem[0x14 * 4 + 1] = 0x05;
		mem[0x14 * 4 + 2] = 0x00;
		mem[0x14 * 4 + 3] = 0x00;
	}

	/* Wolfteam d_98.bin: after CALL 464E / INT 4C AH=08 it walks a file-id
	   list at 7000:0000 (LODSB / CMP AL,F9 / INT 4C AH=F0). Real game path
	   loads that list via INT 43 AX=005F after FS mount (INT 43 AX=8000), but
	   the stub patches 80D7→RET and skips mount — 7000 stays zeroed and boot
	   spins forever, never reaching INT 7Fh install or PIT enable. Park a
	   lone F9 terminator so the loop exits; TriggerPlay starts PIT + [5B5A].

	   Also: CALL 464E → CALL 5A9A polls ports E0D0/E0D2; open-bus 0xFF makes
	   TEST AL,40 spin. That hang is after INT 08 install, so the stub never
	   returns to OUT 07E8=81. NOP the handshake to a single RET (shared
	   across Wolfteam 000_BOOT builds that keep this helper near 5A9A —
	   locate by the E0D2 busy-wait signature). */
	if (wolfteam98_) {
		/* MUSDRV streams standard MIDI out E0D0; bridge it to the OPN. */
		wolfBridgeEnable_ = 1;
		WolfBridgeReset();
		mem[0x70000] = 0xF9;
		/* Only RET the 464E handshake busy-wait (first hit). ISR delay stubs
		   at 5B00+ are left intact and use WOLF_SYNC1=0 (not busy). */
		static const uint8_t kSyncBusy[] = { 0xBA, 0xD2, 0xE0, 0xEC, 0xA8, 0x40, 0x75, 0xFB };
		for (unsigned p = 0x600; p + 8 < 0x10000u; p++) {
			if (memcmp(mem + p, kSyncBusy, sizeof(kSyncBusy)) == 0) {
				mem[p] = 0xC3;
				break;
			}
		}
		/* Prefer explicit MI* banks over incidental MF (suzaku BL50). */
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
			/* Prefer real instrument banks (MM, MD, OPNM). Tiny MI stubs
			   (zanyks 0B8_MI01 at 1K) must not beat MM01. */
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
			/* apros ships songs only — plant a minimal MF header so INT4C
			   AH=00 / bank init has a non-bogus instrument block. */
			static const uint8_t kMinMf[] = {
				'M', 'F', 0x01, 0x00, 0x20, 0x00, 0x00, 0x00,
				0x18, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00
			};
			memcpy(mem + 0x90000, kMinMf, sizeof(kMinMf));
			wolfMiSeg_ = 0x9000;
		}
	}

	PlantPc98BiosMap(mem);

	/* QueenSoft MADP: INT40 is an AL-indexed API (not the OPN ISR). Boot
	   glue INT40 AL=19 TESTs ES:[0501] bit3 (FM present) before programming
	   YM — plant before CPU start. Play INT7F maps cmd→AL=1D/1B. */
	if (madp98_)
		mem[0x501] = (uint8_t)(mem[0x501] | 0x08);

	/* SORC98 (and similar bootcs stubs): after CALL BIOS init they idle on
	   INT 18h (AH=98h). Catalog BIOS never hooks INT 18 — vector stays
	   0000:0000 and the first idle iteration executes IVT as code, flipping
	   handler segments to FFFF. Park IRET at 0x510 before CPU start. */
	{
		mem[0x510] = 0xCF;
		mem[0x18 * 4 + 0] = 0x10;
		mem[0x18 * 4 + 1] = 0x05;
		mem[0x18 * 4 + 2] = 0x00;
		mem[0x18 * 4 + 3] = 0x00;
		/* Same trap one vector along: a rip has no floppy, so a driver that
		   calls the disk BIOS (Telenet VIS reads a 256-byte sector before it
		   will start) otherwise runs the garbage IVT as code. Report "no
		   error" — CF is cleared in the caller's pushed flags — because the
		   callers treat a failed read as a fatal disk error. */
		static const uint8_t kDiskStub[] = {
			0x55,                    /* push bp                */
			0x89, 0xE5,              /* mov  bp,sp             */
			0x83, 0x66, 0x06, 0xFE,  /* and  word [bp+6],0FFFE */
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

	/* Falcom 00BIOS / PR.* (ys3/xana2/…, catalog dummysndrom=1): second boot
	   CALL reads A000:3FEE and word [0536], then only runs FM init when the
	   derived flag at [0702] is non-zero. Zeroed RAM skips OPN setup entirely
	   (INT51/52 stay inert, opnW=0). Plant the BIOS equipment bit that the
	   probe tests (bit2 of [0536]) so FM init runs like a machine with a
	   sound board — same role as hoot's dummysndrom.
	   xana2 PR.NO0/PR.NO5/xana2e also require A000:0FEE bit3 set; without it
	   they take the no-FM path and never plant INT14/OPN. Keep bit0 for ys3. */
	if (dummySndRom_ || (bootCs_ == 0 && bootIp_ == 0x0600)) {
		mem[0x536] = (uint8_t)(mem[0x536] | 0x04);
		if (0xA0000u + 0x3FEEu < 0x200000u)
			mem[0xA0000u + 0x3FEEu] = (uint8_t)(mem[0xA0000u + 0x3FEEu] | 0x09);
	}

	np2_reset();
	np2_set_adrsmask(0x000FFFFFu);
	np2_setextsize(0);
	np2_set_v30(0);

	/* Honor bootcs=0 when bootip is set (SORC98: CS=0000 IP=F000 → phys 0xF000).
	   Only default CS to 0x60 when both bootcs and bootip are unset/zero. */
	uint16_t cs = (uint16_t)((bootCs_ != 0 || bootIp_ != 0) ? bootCs_ : 0x0060);
	uint16_t ip = (uint16_t)bootIp_;
	np2_set_cs_ip(cs, ip);
	np2_set_ss_sp(0x1000, 0xFFFE);
	np2_reg_set(NP2_R_DS, cs);
	np2_reg_set(NP2_R_ES, cs);
	np2_reg_set(NP2_R_FLAGS, 0x0202); /* IF set */

	extSong_ = (uint16_t)(titleCode & 0xffff);
	extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);
	extCmd_ = 0;
	stubState_ = 0;
	cpuCycles_ = 0;
	opnPumpResidual_ = 0;
	picMask_ = 0x00; /* unmask all for bootcs drivers that never program PIC */
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

/* CONFIG `mmd.sys /f12 4096` — `/f` is a switch, not a path separator.
   Stem is the first whitespace token; only then take the last \/: in it. */
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

void CHardPc98::MaterializeDosFiles(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge) return;
	/* Engines first so a 256+ song list cannot fill files_[] before the
	   glue COM/EXE is copied (night_s USMD: 132 files + 130 conin). */
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
		/* Stem first: `MMD2.SYS 4096` used to ZipFsFind the CONFIG string
		   and the no-ext fallback returned mmd2.com (same stem, earlier
		   zip member), clobbering the type=file SYS image. */
		const unsigned char* data = NULL;
		if (stem[0])
			data = CEmuZipFsFind(fs, stem, &sz);
		if ((!data || !sz) && r->name && r->name[0]
			&& (!stem[0] || strcmp(stem, r->name) != 0))
			data = CEmuZipFsFind(fs, r->name, &sz);
		unsigned char donorBuf[256 * 1024];
		unsigned donorSz = 0;
		/* Song-only zips (gdm_mo/guyna/kizuato/nekoex) omit PMD_98.COM even
		   though the catalog lists it — pull the driver from a sibling pack. */
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

void CHardPc98::BindDosRomHandles(const CEmuGameEntry* ge)
{
	if (!ge) return;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "conin") != 0)
			continue;
		const int off = r->offset;
		/* Handles go up to DOS98_HANDLE_MAX-1 (fc98v12 songs past 0x30). */
		if (off < 0 || off >= DOS98_HANDLE_MAX) continue;
		const char* base = r->name;
		for (const char* p = r->name; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		if (_stricmp(r->type, "conin") == 0) {
			dos_.SetHandleText((uint16_t)off, base);
			/* hoot conin@0x10 is stdin (AH=3F BX=0), not DOS handle 0x10.
			   cplay still binds the title-numbered handle above. */
			if (off == 0x10)
				dos_.SetHandleText(0, base);
		} else
			dos_.SetHandle((uint16_t)off, base);
	}
}

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


/* Name-load ADVH (EB 06 USDdrv, no "03 30"): install writes mov ax,CS+0x33
   for bind/data while the OEM ISR keeps mov ds,cs. watagolf finishes a
   +0x330 offset reloc (ISR ~069B); name-load never does. At BootDos, retarget
   bind-path immediates (off>=0x800) to the live INT F1 CS and plant INT0B.
   Early CS+0x33 refs (@034B/@0442) stay — AL=0 needs them. AL=1 still walks
   channel slots through @04AB, so TriggerPlay save/restores the ISR prologue. */
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
	/* Do NOT memcpy install bytes to +0x330 (corrupts name-load BSS).
	   Do NOT plant INT0B yet: thin AL=1 channel state + early OEM ISR
	   key-offs the bind notes. Bind→CS retarget alone restores audible AL=1. */
	return 1;
}

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
		/* CS: jmp [reg+disp16] → command tables 1340/1380 (+0x330). */
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
	/* Abs16 data refs into the pre-reloc island (gate, flags) → +0x330. */
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

/* olteus MAP keeps `A:\MUSIC F.MUS` / `.MTB` (space = default). Digit-poke
   both the VFS EXE (before LoadExe) and RAM so 01 vs 02 open different files. */
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

const char* CHardPc98::SelectedDosSong(const CEmuGameEntry* ge, unsigned titleCode) const
{
	if (!ge) return NULL;
	/* olteus_va: all songs are file@-1; MAP builds A:\MUSIC#F/P.MUS from title. */
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
	/* cplay98/mdrv list song banks as conin (offset == title low byte). */
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
	/* Bio_100%/BGML_98 and similar: one shared bank listed as conin@0x10
	   (hoot stdin) plus type=file@-1. Title codes pick a track inside that
	   bank, so no rom offset equals the title byte. */
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

/* SYNTH_98 / HHD / similar: rom offset 5 is the overlay (S20.BIN), not
   the song. Rebinding handle 5 to the PAI at TriggerPlay is harmless once
   the overlay has TSR'd, but keep the catalog mapping for any AH=3F BX=5
   that still runs. */
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

void CHardPc98::BindDosTriggerSong(const CEmuGameEntry* ge, unsigned titleCode)
{
	const char* sf = SelectedDosSong(ge, titleCode);
	/* hootrip: cplay/fplay open by ASCIIZ name; mdrv_98/mddrv_98 same (INT D2 AL=2).
	   Do NOT match bare mdrv98+mlp_hoot (content on handle 0). */
	static const char* kCplay[] = { "cplay", "fplay", NULL };
	/* mlalf_98 is deliberately absent: its INT 7F cmd0 reads handle 0 and
	   hands the buffer straight to the ANNEX driver, which starts with
	   `CMP WORD ES:[SI],1` — every .MLO song begins 01 00, so the driver
	   wants the song bytes and rejects a filename outright. */
	/* PLAY5_98 is not here: INT7F cmd0 AH=3F-reads handle 0 into a buffer
	   and INT F2 AX=0 loads those bytes. Filename-text on handle 0 left
	   PLAY5/PLAY3/MUSIC + PLAY5_98 silent.
	   IBGMP.COM the same: cmd0 AH=3F-reads handle 0 then INT52 AX=200.
	   Prefix "ibgm" also matches IBGMP, so it must not open-by-name. */
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
	/* famistava conin: INT7F AH=3F reads the ASCIIZ name from handle 0, then
	   AH=3D opens the real file — must not overwrite with song bytes. */
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
		/* Shared-bank conin@0x10 is hoot stdin (filename), not a song handle. */
		if (!opensByName && nConin == 1 && stdinConin)
			opensByName = 1;
	}
	/* usd_98 (ADVBIOS/ADVH): INT7F AH=3F reads song BYTES from BX=0
	   (CX=4000/FFFF). ADVH packs list songs only as conin@title — the
	   famistava heuristic above would bind the filename text (len=10 for
	   "DC_02P.USO") and leave keyOn=0. Always use binary handles.
	   usmd_98 is NOT here: glue AH=3F-reads the title handle then INT 7D
	   AH=3D-opens DS:SI as an ASCIIZ .USO name. Binary on that handle
	   made Open AX=0002. */
	{
		static const char* kUsdSong[] = {
			"usd_98", "usd98",
			NULL
		};
		if (DosShellStarts(ge, kUsdSong))
			opensByName = 0;
	}
	/* magpa_98: kOpenName includes "musdrv" (mbmusp packs need the filename
	   on handle 0), but magpa's INT7F cmd0 is AH=3F BX=0 of song bytes then
	   INT40 AX=2000. Filename text on handle 0 left MUSDRV reading ASCII. */
	{
		static const char* kMagpaBin[] = { "magpa_98", "magpa", NULL };
		if (DosShellStarts(ge, kMagpaBin))
			opensByName = 0;
	}

	if (sf) {
		strncpy_s(dosSong_, sf, _TRUNCATE);
		if (opensByName) {
			/* Filename text on handle 0 — driver opens via INT21 AH=3D. */
			dos_.SetHandleText(0, sf);
		} else {
			dos_.SetHandle(0, sf);
			/* VALKY_98 reads SSCP/CSCP from handle 5 then 6 at install.
			   Catalog parks the driver on 6; putting the .DAT on 5 made
			   the first AH=3F succeed and CALL FAR into song bytes (#UD). */
			static const char* kValkyH5[] = { "VALKY_98", "valky", NULL };
			if (!DosShellStarts(ge, kValkyH5)
				&& !DosHandleBoundToOtherFile(ge, 5, sf))
				dos_.SetHandle(5, sf);
			if (!DosHandleBoundToOtherFile(ge, 0x0B, sf))
				dos_.SetHandle(0x0B, sf);
			/* PMD_98 reads the song handle == title low byte (pre-bound at install). */
			const unsigned low = titleCode & 0xff;
			if (low < (unsigned)DOS98_HANDLE_MAX
				&& !DosHandleBoundToOtherFile(ge, (int)low, sf))
				dos_.SetHandle((uint16_t)low, sf);
		}
	}
	extCmd_ = 0;
	const unsigned byte2 = (titleCode >> 16) & 0xff;
	const unsigned hiByte = (titleCode >> 8) & 0xff;
	/* ARTDI packed NTL.PAC: title 0xHHSS — high byte = pack handle, low = index.
	   Stub wants the full word on EXT_SONG (hootrip). */
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
		/* INT 7F AH=9: in-bank index on EXT param (0x7E4). */
		extSong_ = 0;
		extParam_ = (uint16_t)byte2;
	} else if (DosShellStarts(ge, kLudyMagic)) {
		/* LUDY: IN 7E2 AX → xchg AH,BL uses AH as the .MCG handle (6) and
		   AL as the in-pack index. MAGIC_98: AH=instrument handle (SND),
		   AL=song handle. Low-byte-only EXT_SONG skipped the bank and left
		   keyOn=0 / dumps=4. */
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = 0;
	} else if (pacTitle) {
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = 0;
	} else if (voiTitle) {
		/* MDR external-voice: EXT_PARAM = voice handle (byte2). */
		extSong_ = (uint16_t)(titleCode & 0xff);
		extParam_ = (uint16_t)byte2;
	} else if (DosShellStarts(ge, kElfMus)) {
		/* ELFMUS98 cmd0 `IN AX,7E2`: AH>=0x0A is the packed-bank DOS
		   handle (aress BGM.MDT titles 0x10nn / SE.MDT 0x06nn). Low-byte
		   EXT_SONG left BX=0 and AH=3F transferred 0. 8-bit titles
		   (birthd 0x23) keep AH=0 and still read handle 0. */
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = 0;
	} else if (DosShellStarts(ge, kExtParamVoice)) {
		/* mmd2/iwaplay: IN 7E4 is the voice/TON handle (catalog byte2, or
		   handle 5 when titles are 0x10-style). byte2!=0 used to set
		   EXT_SONG=voice and skip the bank — dumps>0 / keyOn=0. */
		extSong_ = (uint16_t)(titleCode & 0xff);
		extParam_ = byte2 ? (uint16_t)byte2 : 5;
	} else if (pc88VaIo_) {
		/* PC-88VA DOS overlay glue (tetrisva/rtypeva/shinrava/famista*):
		   IN 7E4 reads only the low byte of EXT_PARAM as play mode.
		   Titles are either 0x0001xxxx (tetrisva) or 0xNN0000xx (rtype
		   0x01000010 / famista 0x04000010) — take byte2, or byte3 if zero.
		   olteus.com INT7F cmd0 = far 00DF (init) then IN AX,7E2 + far 03F0
		   (play). cmd2 is init-only — keep EXT_CMD=0 so play runs. */
		extSong_ = (uint16_t)(titleCode & 0xff);
		unsigned mode = (titleCode >> 16) & 0xff;
		if (mode == 0)
			mode = (titleCode >> 24) & 0xff;
		extParam_ = (uint16_t)mode;
	} else if (DosShellStarts(ge, kBgmlSong)) {
		/* Bio_100% BGML_98: INT 7F cmd0 reads EXT_SONG as a 16-bit title
		   (07E2/07E3). Catalog 0x01nn are one-shots; 0x00nn are looping BGM. */
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = 0;
	} else if (ge) {
		static const char* kAvalonSong[] = { "avalon", NULL };
		if (DosShellStarts(ge, kAvalonSong)) {
			/* avalon.com IN 7E4 → INT F1 AH=0B AL=in-bank index.
			   Catalog 0xTT00HH: TT is the DAT-internal title, HH the conin. */
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

static unsigned Pc98DosLin(uint16_t seg, uint16_t off)
{
	return ((unsigned)seg << 4) + (unsigned)off;
}

static void Pc98Wr16(uint8_t* mem, unsigned addr, uint16_t v)
{
	mem[addr] = (uint8_t)(v & 0xff);
	mem[addr + 1] = (uint8_t)(v >> 8);
}

/* SYNTH_98.COM (365-byte ylz glue): INT60 AH=0x0F returns DX=destOff in
   the overlay (S20:3088 / S20S_4:1B86). The COM did `mov ax,ds; mov es,ax;
   mov di,dx` so PAI landed past the AH=4A-shrunk COM and AH=0 parsed the
   overlay's leftover init. After INIT stores the overlay at CS:0260, jump
   to a cave that loads ES from CS:0260 (DS may still be the overlay). Skip
   when 0260 is 0 (resident SYNTHIA — crim). */
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
	/* E9 disp16 → 0270; pad through the old mov ds,cs:[025C]. */
	mem[at + 0] = 0xE9;
	mem[at + 1] = 0xC8;
	mem[at + 2] = 0x00;
	memset(mem + at + 3, 0x90, 8);
	/* cave@0270: mov es,[cs:0260]; mov di,dx; mov ds,[cs:025C]; jmp 01B0 */
	static const uint8_t kCave[] = {
		0x2E, 0x8E, 0x06, 0x60, 0x02,
		0x8B, 0xFA,
		0x2E, 0x8E, 0x1E, 0x5C, 0x02,
		0xE9, 0x31, 0xFF
	};
	memcpy(mem + cave, kCave, sizeof(kCave));
	return 1;
}

/* SS_98.COM cmd0 AH=3F-reads the ASCIIZ name at CS:0196 then INT 41 AH=1.
   The glue does `mov si,ds / xor di,dx` (DX=0196) so SI:DI is a far pointer
   to that name — xor assumes DI=0. BootDos leaves DI dirty, FindFirst at
   the driver's DS:260A then misses TITLE.DAT (AX=0012). `mov di,dx` keeps
   the pointer on the name. */
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

/* USMD.EXE plants INT 7E at CS:0005 then AH=31 TSR. The insn after that
   INT21 is the "already loaded" uninstaller (pushf; mov ax,3; int 7e…).
   feti puts it at 0270; hhg shifted it to 027C. Leaving CS:IP on the
   INT21 trampoline with AX=3100 made TriggerPlay's PumpCycles abort on
   RESIDENT before glue INT 7D opened the .USO. IRET onto the uninstaller
   and park there. */
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

/* FairyDust MFD.EXE (koukan2/madol MIDI): Borland TSR. Shell `MFD L` wants
   argv[1]='L' → keep() + setvect(0x42, ISR). The EXE's switch table offset
   (CS:0422/0419) points at heap code, so L never matches and it prints the
   menu then AH=4C-exits. INT 42 stays the trampoline; mfd_98.com's play
   path (`INT 42 AX=3`) IRETs as a no-op (midi=0/0).
   The real ISR is the pusha frame at CS:1AE5 (jmp cs:[bx+1F8] on AX-1).
   Launch also setvects CS:00D5 (CRT abort), not that ISR. Point INT 42 at
   the pusha frame and skip the argv switch straight into launch/keep. */
static int s_mfdInt42Host;
static int s_midiDrvHostSmf;

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
	/* CRT `mov dx, DGROUP` at CS:0000 is relocated; the ISR's
	   `mov bp, 0x8A0` is not. */
	if (mem[base] == 0xBA && mem[base + (unsigned)isr + 9] == 0xBD
		&& mem[base + (unsigned)isr + 12] == 0x8E
		&& mem[base + (unsigned)isr + 13] == 0xDD) {
		mem[base + (unsigned)isr + 10] = mem[base + 1];
		mem[base + (unsigned)isr + 11] = mem[base + 2];
	}
	/* ISR far calls (9A off,seg) were emitted without MZ relocs, so they
	   still hold the link-time segments (01A1/01FA/02C2) and #UD. Same
	   for the rest of the driver image. Skip words already relocated
	   (>= 0x1000) or BIOS-ish. */
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
	/* Same missing-reloc class: `mov ax/ds/bp, DGROUP` still holds 08A0h. */
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
	/* jmp cs:[bx+1F8] was aimed at CRT bytes. The 7-word case table
	   sits right after the ISR IRET (CS:1C08). */
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

/* 400-byte mfd_98.com (INT 42 glue): hooks INT 7F then INT 18 AX=9801 once
   and falls into the ISR as mainline (pusha / IRET smash). Night_s's
   143-byte COM loops INT 18. Stop after setvect so BootDos proceeds with
   the 64KB COM alloc still intact (AH=31 / 30h paras clipped CS:0290). */
static uint16_t s_mfd98GlueCs;

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

/* SYNUPS .MDI: after 6xFF or 01 00, 9x is a channel prefix with one
   parameter, then (note, duration) pairs. Duration ticks the capture clock
   so VST/KPI tempo is not a zero-delta cluster. SYNUP_98 hits #UD before
   it can OUT E0D0, so TriggerPlay walks the resident song. */
template<typename Cap, typename Tick>
static void HostWalkSynupsMdi(Cap cap, Tick tick, const uint8_t* p, unsigned n)
{
	if (!p || n < 40u)
		return;
	if (memcmp(p, "SYNUPS", 6) != 0)
		return;
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

/* Recomposer RCP v2. Event is [cmd, delay, p1, p2]; cmd<0x80 is a note
   (p1 gate, p2 vel). Delay is in header timebase ticks (usually 48). */
template<typename Cap, typename Tick>
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

/* Studio Twin'kle compact MD1: two LE32s, then FF 12 header chunks.
   Skip the first two FF records; remaining pairs are (note, duration).
   Do not match red/mirage MD1 (no FF 12). */
template<typename Cap, typename Tick>
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

static void MfdRestoreInt42Trampoline(uint8_t* mem)
{
	if (!mem) return;
	mem[0x42 * 4 + 0] = (uint8_t)((0x42u * 2u) & 0xff);
	mem[0x42 * 4 + 1] = 0;
	mem[0x42 * 4 + 2] = (uint8_t)(DOS98_TRAMP_SEG & 0xff);
	mem[0x42 * 4 + 3] = (uint8_t)((DOS98_TRAMP_SEG >> 8) & 0xff);
}

/* VALKY/SSCP: cmd8 tests CS:[384B]/[384D] then INT 50 AH=3 for the song
   buffer segment. SSCP never hooks INT 50. */
static void ValkyArmSscpPlay(uint8_t* mem, uint16_t songHandle)
{
	if (!mem)
		return;
	unsigned songSeg = 0;
	const unsigned s7f = (unsigned)mem[0x7F * 4 + 2]
		| ((unsigned)mem[0x7F * 4 + 3] << 8);
	if (s7f && s7f != (unsigned)DOS98_TRAMP_SEG) {
		const unsigned vb = s7f << 4;
		if (vb + 0x422u < 0x200000u)
			songSeg = (unsigned)mem[vb + 0x420]
				| ((unsigned)mem[vb + 0x421] << 8);
	}
	mem[0x600] = 0x80;
	mem[0x601] = 0xFC;
	mem[0x602] = 0x03;
	mem[0x603] = 0x75;
	mem[0x604] = 0x04;
	mem[0x605] = 0xB8;
	mem[0x606] = (uint8_t)(songSeg & 0xff);
	mem[0x607] = (uint8_t)((songSeg >> 8) & 0xff);
	mem[0x608] = 0xCF;
	mem[0x609] = 0xB8;
	mem[0x60A] = (uint8_t)(songHandle & 0xff);
	mem[0x60B] = (uint8_t)((songHandle >> 8) & 0xff);
	mem[0x60C] = 0xCF;
	mem[0x50 * 4 + 0] = 0x00;
	mem[0x50 * 4 + 1] = 0x06;
	mem[0x50 * 4 + 2] = 0x00;
	mem[0x50 * 4 + 3] = 0x00;
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
	for (unsigned i = 0; i < nc; i++) {
		const unsigned base = cands[i] << 4;
		if (base + 0x390Au < 0x200000u) {
			mem[base + 0x384B] = 1;
			mem[base + 0x384D] = 1;
			mem[base + 0x390A] = 1;
		}
	}
}

static void ValkyRewindCmd8Read(CEmuDos98& dos, const char* song, uint8_t vec)
{
	if (!s_valkyKeepIrq0 || vec != 0x21 || !song || !song[0])
		return;
	if ((uint8_t)(np2_reg_get(NP2_R_AX) >> 8) != 0x3F)
		return;
	if (np2_reg_get(NP2_R_CX) != 0x400)
		return;
	const uint16_t bx = np2_reg_get(NP2_R_BX);
	if (bx)
		dos.SetHandle(bx, song);
}

/* FairyDust MFD.EXE (koukan2/madol MIDI): Borland TSR. */

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

/* 142-byte NC_98.com (3x3eyes MIDI): INT 7F cmd0 reads the conin name into
   CS:017E then XOR SI,SI / INT 42 AX=0. NC.COM AX=0 REP MOVSB 128 bytes from
   DS:SI (filename) — SI=0 copies the COM header instead of the name, so the
   PIT ISR only emits CC all-notes-off (midi=0/2880). */
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
		/* Keep the full CONFIG string for extraParas / INIT packet.
		   Stem-only name is for LoadDeviceImage — last-slash on
		   `mmd.sys /f12 4096` used to look up "f12" (sbr_98 SILENT). */
		const char* base = r->name ? r->name : "";
		char name[DOS98_NAME];
		DosCfgFileStem(base, name, (int)sizeof(name));
		if (!name[0]) continue;
		/* NMUSE CONFIG -d/-k sizes are byte buffers past the image.
		   Small -d2048 -k1024 already fits the default alloc; applying
		   it anyway moved the COM AH=48 block and GAPPY'd pod OPEN. */
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
		/* MMD2.SYS 4096 — bare decimal is the work-buffer size, not -d/-k.
		   Without extra paras the next COM (mmd2.com) lands on CS:0xFBA
		   (voice+song copy dest) and INT D2 AH=10 copies into overwritten RAM. */
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
				/* 4096 buffer + 0x400 work + 0x200 IRQ SP at [c7c]+0x200.
				   0x40 paras stopped at image+0x1400 (0x23BA) while ISR SP
				   is 0x25BA, so the stack landed in the next COM. */
				extraParas += 0xA0u;
		}
		if (extraParas <= 0x180u && !isMmd)
			extraParas = 0;

		/* MUSE/SDD devices pick IRQ from SSG I/O A bits7-6; force INT14 path. */
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
		/* Classic MMD.SYS: OPN ports are filled by AH=0 detect (1a38).
		   mmd2.com never sends AH=0, so 154A stays 0 and every 05d9/048a
		   write hits port 0 (PIC) instead of 188h. */
		if (g_mmdClassic && mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			if (lin + 0x154Du < 0x200000u) {
				mem[lin + 0x154A] = 0x88;
				mem[lin + 0x154B] = 0x01;
				mem[lin + 0x154C] = 0x8A;
				mem[lin + 0x154D] = 0x01;
			}
		}
		/* wiz6 $MUSE2$ keeps `MOV SP,005Fh` at CS:E2 (derby's image does
		   not). Raise it in the loaded copy even if the SYS-name patch missed. */
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
		/* Char-device INIT +12h is a far pointer to the CONFIG.SYS tail
		   (space + args + CR). MMD2.SYS walks it for the 4096-byte buffer
		   size; a NULL ptr made LDS SI from 0000:0000 and left CX=0 so
		   mmd2.com's AH=3F song read transferred nothing (dumps=1).
		   Packet used to live at 0050:0100 (lin 0x600) — that is the INT
		   trampoline (0060:0000). memset 0x60 wiped INT 21's HLT stub so
		   SYS/glue AH=25 never hooked D2/7F. */
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
			/* MMD2.SYS INIT does LDS SI,ES:[2C] then LDS SI,[SI+12] to reach
			   the CONFIG tail (4026-byte: orangerd/michael). Newer 4655-byte
			   (mjclnc/sbp/shikinjo) uses ES:[34] the same way. When ES is
			   still the packet segment the far pointer at packet+2C/+34
			   must be the packet itself so [SI+12] is the CONFIG ptr. */
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
		/* MOV AX,reqSeg; MOV ES,AX; MOV BX,reqOff; CALL FAR [stratPtr] */
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

		/* MUSE3 plants INT14 on DEVICE OPEN (cmd 0x0D), not INIT: DOS 5+
		   INIT skips call 6cd. muse_98.com then takes IVT[0x52] as the
		   driver CS and far-calls [CS:8]. Trampoline CS writes 0060:0012
		   and #BRs before AH=25 INT7F. MUSIC.SYS is not this family. */
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
				/* MUSE/NMUSE/SDD/MUSE2 device stacks are tiny; a nested
				   INT14 tick during INIT/OPEN re-enters the ISR on the same SP.
				   MMD2 plants INT14 then STI before RETF — skip nested IRQ
				   the same way. */
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
		/* wiz6 MUSE2 OPEN `MOV SP,005Fh` can smash the header including
		   the interrupt pointer at [CS:8]; muse_98 then far-calls 0014.
		   Re-raise SP in case INIT/OPEN ran before the first patch. */
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
				/* muse_98 far-calls 0014 when OPEN smashed [CS:8].
				   Near JMP to the interrupt routine (header name+4). */
				if (lin + 0x17u < 0x200000u && intrOff > 0x17u) {
					mem[lin + 0x14] = 0xE9;
					Pc98Wr16(mem, lin + 0x15u,
						(uint16_t)(intrOff - 0x17u));
				}
			}
		}
		/* MMD.SYS (sbr): parse 0619 sets duration [ch+2]=1 but never gate
		   [ch+3]. Note 0849 copies gate→duration; gate 0 makes 0732 RET
		   the channel forever (keys=1 from the A0 assist, then SILENT).
		   AH=3 at 0143 also `rep stos` from 17F4 and wipes this poke —
		   MmdPlayAssist re-applies after parse. */
		if (g_mmdClassic && mem && loadSeg) {
			const unsigned lin = Pc98DosLin(loadSeg, 0);
			const unsigned base = lin + 0x180Fu;
			for (unsigned ch = 0; ch < 6u; ch++) {
				const unsigned gate = base + ch * 0x33u + 3u;
				if (gate < 0x200000u && mem[gate] == 0)
					mem[gate] = 1;
			}
			/* Classic INIT does not AH=25 the YM ISR (that is AH=0 /
			   1d1b). mmd2.com glue never sends AH=0, so INT0B/14 stay
			   trampolines and AH=3 spins on [17F4] (sbr SILENT). */
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
		/* SDD 26 + DOS 5: INIT skips plant and OPEN's [152E]==0 path
		   never writes IVT 14 (ishido dumps=1). ISR lives at CS:09BF.
		   Force-plant even if INT14 already has a stub — IvtHooked
		   skipped the real 09BF and left dumps=1. */
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
		/* MUDRV3 SYS-in-EXE: strategy stores the request at [CS:1B]; INIT
		   often returns before AH=25 INT 43. The API lives at CS:0248
		   (PUSHA / CLD / CLI / AH dispatch). LW1CD_98 talks INT 43. */
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
			/* Genuine idle HLT — advance timers. */
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
	/* DOS packs can still depend on fixed firmware/code images.  In
	   particular, PONYCA's MSCDRV front end probes the SOUND.ROM signature
	   at CEE0:0004 and installs INT D2 from that ROM before exposing INT 7E.
	   The early isDos_ return in LoadRoms used to skip every code/binary ROM,
	   leaving a valid-looking INT 7E wrapper backed by the DOS D2 trampoline. */
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
	/* PC-98 BIOS ROM window + text VRAM + MEMSW + BIOS work. */
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
		/* Pearlsoft OPN rows are catalog midiout=1 (same zip as GS /m).
		   Leave modeMidi_ on and IRQ0 storms; MUSDRV /f then never ticks
		   Timer B (dumps 174 vs historical 1032). */
		{
			const char* ext = strrchr(sf, '.');
			if (ext && (_stricmp(ext, ".FM") == 0 || _stricmp(ext, ".OPN") == 0))
				modeMidi_ = 0;
		}
		/* fugam boot AH=3F BX=5 then INT D3 AX=0201; bind before shells or
		   the 0-byte read poisons FMD. FMD /# also needs handle 0. */
		static const char* kFmdFugam[] = { "FMD", "fugam", NULL };
		if (DosShellStarts(ge, kFmdFugam)) {
			/* Boot: AH=3F BX=5 then INT D3 AX=0201 AH=3D-opens DS:SI.
			   Filename text on 5; raw .GS on 0 would make AH=3D fail. */
			dos_.SetHandleText(5, sf);
			dos_.SetHandle(0, sf);
		}
	}
	/* VALKY_98 AH=3F BX=5 first (CF stays 0 on a 0-byte unused handle),
	   so SSCP on catalog handle 6 is never reached. Alias 5←6. */
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

	/* BIOS PIT counts from power-on. IRQ0 stays masked on FM (OPN Timer B
	   is the sequencer); midi/beep need the daily timer during TSR detect
	   (FMD stores the MPU version at CS:[1798] after a timed ACK wait). */
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
	/* MMD.COM /I auto plants INT 0E and waits for MPU clock-to-host FD.
	   It never OUTs PIC mask port 02h, so IRQ6 must already be live or
	   the probe STC-fails and INT 61 is never hooked (dosmiss=int61). */
	if (modeMidi_)
		picMask_ = (uint8_t)(picMask_ & (uint8_t)~(1u << 6));

	/* Generous shell budget: PMDB2+PMDPCM packs need several seconds.
	   imd_1 (PMDB2 without #/Mxx): catalog PMD→PCM→glue re-inits and drops
	   the PPC bank — run glue before PCM. Packs with #/Mxx (imd_2..4,
	   fc98v13) need catalog order (glue last). */
	const uint64_t setupBudget = (uint64_t)cpuHz_ * 8ull; /* ~8s per shell */
	/* mbmusp/MUSDRV: SSG I/O A bits7-6 select INT14; EOI assumes slave. */
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
	/* shangva/demo_va: rom type=device (MUSIC.SYS/DEMO2.SYS) must INIT
	   before the glue shell so INT C8/C3 exist for play/stop. */
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
			/* olteus_va: MUSIC.EXE is a 1KB OPN probe that OUT 44/45 and
			   AH=4C-exits. Running it first stamps the same SSG blip onto
			   every title; MAP.EXE overlay (olteus.com AH=4B03) is the player. */
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
	/* PC-88VA DOS overlays (tetrisva/shinrava/famista89): OPN ISR on INT14.
	   Mirror to INT0B when missing so DeliverIrqs can tick the sequencer.
	   olteus plays via IRQ0→MAP:09BC; INT14-only starved MUSIC 02. */
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
	/* rtypeva: COM plants OPN on INT0A → far 0C1B (VA ports). MAIN also
	   parks a PC-98-port ISR on INT14 — do NOT prefer that for VA play. */
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
	/* olteus_va: remember MAP.EXE load seg (COM far-table [01C4]) and plant
	   INT08 → near tick trampoline once play is armed. */
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
		/* Finish MAP handshake before INT18 idle — COM returns early. */
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
	/* PMD often installs the OPN ISR then leaves master mask FF; if IVT0B is
	   hooked, unmask IRQ3 so OPN timers can run. */
	if (mem && IvtHooked(PC98_OPN_IRQ_VEC, 1))
		picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
	/* usd_98 (ASCII USO): USD hooks only INT F2 (memcpy stub). Play goes
	   through INT F4 which ADVBIOS owns (AH=0 load / AH=1 play / …).
	   Do NOT mirror F2→F4 — that wiped the ADVBIOS API and left keyOn=0.
	   If F4 was never hooked, fall back to F2. INT F3 is an AH-multiplex
	   API (not the OPN timer ISR) — do not plant it on INT0B.
	   ADVBIOS.OVL packs often hang in far486 waiting on IN 60h bit5
	   (never toggles here) after F2/F4/far-table are ready but before
	   INT7F is planted — finish the install from the host. */
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
				/* Park at USD HLT idle (CS:01BD). */
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
		/* ADVH USD (1585/1599): INT7F idle at HLT @022E after install.
		   Some ADVH.EXE builds use a DS=loadSeg decrypt stub at MZ entry
		   (IP in 0x120..0x200, xor-loop then jmp 0010) that USD's 0395/0402
		   miss when the TC0/EXEPACK signatures differ — F1 stays trampoline
		   while the MZ header + overlay body sit at [0706]/[0706]+10.
		   Finish that entry on the host, then plant INT7F + park. */
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
		/* Name-load ADVH: finish dual-seg data / ISR DS consistency and
		   plant INT0B on the OEM music ISR while BootDos still owns the
		   resident image (before TriggerPlay song I/O). */
		{
			const unsigned sF1 = (unsigned)mem[0xF1 * 4 + 2]
				| ((unsigned)mem[0xF1 * 4 + 3] << 8);
			if (sF1 && sF1 != (unsigned)DOS98_TRAMP_SEG
				&& AdvhNormalizeNameLoadResident(mem, sF1))
				picMask_ = (uint8_t)(picMask_ & ~(1u << 3));
		}
	}
	/* Crowd CMD/CMDP: OPN probe at CS:2393 programs the chip then clears
	   [CS:1792] on the all-CF=0 path. INT60 play (idx1) spins while
	   [1792] < 1, so song reads succeed but keyOns never start. Later play
	   steps also require [1792]==1 exactly (not 2/3/4 from probe stages).
	   Music ISR is installed on INT14 (same as MADP/N3GOLF); mirror to
	   INT0B so DeliverIrqs can fire OPN timer ticks. */
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
				/* Install path was skipped ([1792]==0 at hook time). Locate
				   ISR prologue in the resident CMD image and plant INT14. */
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
	/* HuLinks fakecall→music→46: MUSIC.COM parks the OPN ISR on INT14.
	   Mirror to INT0B only — do NOT also plant the same ISR on INT08/PIT.
	   Dual delivery (OPN + PIT) double-ticks the sequencer: 46oku fades out
	   as [0294] hits 0x10 early and mute-alls. Channel freeze was from the
	   cmd2→AH=1 mute path, not from missing PIT. */
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
	/* BIOS PIT always counts. IRQ0 stays masked unless midi/beep/INT 1C. */
	if (!pitRunning_) {
		pitReload_ = (uint16_t)(PC98_PIT_CLOCK_HZ / 60);
		if (pitReload_ == 0) pitReload_ = 1;
		pitCounter_ = pitReload_;
		pitRunning_ = 1;
		pitIrqPending_ = 0;
		pitResidual_ = 0;
	}
	/* Arm MPU capture only after shells finish (FMP -m probe OUTs zeros).
	   FMD /# AH=2 D58 already streamed GS sysex during fugam boot — do not
	   wipe that, and stay in intelligent mode.
	   MMD.COM MIDI (INT 61 + MMP_HOOT) is the same: UART-force + CaptureReset
	   after /K /D install drops CTH and leaves INT 61 unhooked. Do not match
	   bare "MMD" — that is also MMD2.SYS.
	   Pearlsoft MUSDRV /f .FM is catalog-tagged midiout (same zip as GS /m).
	   UART-force + CaptureReset there made /f skip OPN (historical peak
	   ~24k went silent). Leave the MPU alone when the bound song is FM. */
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
		/* HOT-B MIDIDRV.EXE (7colors): SMF player. AH=81 play is MPU cmds
		   88/EC/01/B8/0A then the INT 0E ISR walks the track on CTH FD.
		   Forcing UART after install made B8 a no-op (midi=01 01). */
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
			/* Channel-voice after the GS dump can voice OPN for probes. */
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
		static const char* kValkyArm[] = { "VALKY_98", "valky", NULL };
		if (DosShellStarts(ge, kValkyArm))
			ValkyArmSscpPlay(np2_mem(), (uint16_t)(titleCode & 0xffff));
	}
	return 1;
}

void CHardPc98::AdvanceOpnClocks(uint64_t cpuCycles)
{
	if (!chip_ || cpuCycles == 0 || cpuHz_ <= 0 || opnHz_ <= 0) return;
	/* Residual carried across calls: the old per-call truncation threw away
	   most of a clock per instruction, and the DOS INT service path advanced
	   cpuCycles_ without feeding the chip at all. Together the OPNA saw ~42%
	   of its master clock, which dragged every FM timer (and so the tempo)
	   down by the same factor. */
	opnPumpResidual_ += cpuCycles * (uint64_t)opnHz_;
	const uint64_t ot = opnPumpResidual_ / (uint64_t)cpuHz_;
	opnPumpResidual_ %= (uint64_t)cpuHz_;
	if (ot) chip_->AdvanceClocks(ot);
}

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

		/* olteus: after handshake, pulse real IRQ0 → IVT08 trampoline at
		   MAP:FE86 (PUSH DS; DS=CS; CALL 09BC; POP DS; IRET). Soft near-call
		   into 8419 nested badly from INT18 idle / MUSIC loops. */
		if (olteusIrqPulse_ && olteusMapSeg_) {
			olteusIrqPulse_ = 0;
			const unsigned base = (unsigned)olteusMapSeg_ << 4;
			const unsigned dbase = (unsigned)olteusDataSeg_ << 4;
			if (mem && base + 0xFE95u < 0x200000u) {
				/* Keep DS:[5BBA]=0 so 09BC→8419 does not take the early JMP. */
				if (olteusDataSeg_ && dbase + 0x5BBBu < 0x200000u) {
					mem[dbase + 0x5BBA] = 0;
					mem[dbase + 0x5BBB] = 0;
				}
				/* Re-plant trampoline + IVT each pulse — MAP may rewrite IVT08. */
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
		/* Sample before the HLT handling below: a driver parked on a HLT that
		   is not a DOS trap spins here without ever reaching np2_step, which
		   used to leave the histogram empty for exactly the hangs it exists
		   to diagnose. */
		if (g_ipProf)
			g_ipProf->Note(phys);
		if (isDos_ && mem && phys < 0x200000 && mem[phys] == 0xF4) {
			uint8_t vec = 0;
			if (dos_.TrapVector(cs, ip, &vec)) {
				ValkyRewindCmd8Read(dos_,
					dosSong_[0] ? dosSong_
						: SelectedDosSong(dosGe_, extSong_), vec);
				CEmuDos98Result res = dos_.ServiceInt(mem, vec);
				/* olteus MAP music keeps ticking after COM/EXE TSR or "exit";
				   aborting PumpCycles froze host timer assist. */
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
		/* olteus: MAP opens A:\MUSIC#F/P.MUS — poke digit in CS and all RAM
		   copies (image is >64K so a 128K CS window can miss DS). */
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
		/* mmd2.com may AH=25 INT0B to the 03EC IRET stub after SYS INIT
		   planted 0392. Re-raise the sequencer before the AH=3 wait. */
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
		/* Classic ISR 03CE is the only 27h=15h arm; mmd2.com skips AH=0
		   so the timer never starts and AH=3 spins on [17F4] forever. */
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
		/* fugam INT 7F cmd0: AH=3F-reads handle 0 (.GS), INT D3 AH=1
		   (XOR 0xA5 → FMD tracks into CS:[7]), then AH=3 play.
		   Boot already INT D3 AX=0201 (GS SysEx to the module). Reload
		   GS only when the title changes, then always run INT 7F. */
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
		/* MFD_98 cmd0: `MOV AL,1` / `INT 7C` (AH left stale) before the
		   AH=3F read. That INT 7C never returns (reads stay 0, only the
		   105-byte all-notes-off from AH=1/0 533), so skip it and let
		   the glue load handle 0 then INT 7C AH=0 play. */
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
			static const char* kValkyArm[] = { "VALKY_98", "valky", NULL };
			if (dosGe_ && DosShellStarts(dosGe_, kValkyArm)) {
				s_valkyKeepIrq0 = 1;
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				ValkyArmSscpPlay(np2_mem(), (uint16_t)(titleCode & 0xffff));
			}
		}
		np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		np2_interrupt((uint8_t)funcVect_);
		uint64_t playDrain = drainBudget;
		{
			static const char* kOlteusDrain[] = { "olteus", NULL };
			/* MUSIC 01 is a ~0.5s phrase; a full drain eats it (peak=78).
			   Other titles need the 0.5s arm (MUSIC 02 OK 60s). */
			if (dosGe_ && DosShellStarts(dosGe_, kOlteusDrain)
				&& (titleCode & 0xff) == 1 && cpuHz_ > 20)
				playDrain = (uint64_t)cpuHz_ / 20ull;
		}
		PumpCycles(cpuCycles_ + playDrain);
		{
			static const char* kValkyArm[] = { "VALKY_98", "valky", NULL };
			if (dosGe_ && DosShellStarts(dosGe_, kValkyArm)
				&& opnKeyOnCount_ == 0) {
				s_valkyKeepIrq0 = 1;
				picMask_ = (uint8_t)(picMask_ & 0xfeu);
				uint8_t* vmem = np2_mem();
				ValkyArmSscpPlay(vmem, (uint16_t)(titleCode & 0xffff));
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
		/* Some PMD glue paths (love_ed2 `/i`) need a second play poke after the
		   song buffer is resident — matches the itest double-trigger behavior.
		   MSCD_98 cmd0 is not idempotent: it stops, waits 18 retraces, then
		   starts.  A second poke's shorter budget stopped the new song and
		   stranded the CPU halfway through its wait. */
		static const char* kMscdPlay[] = { "MSCDRV", "mscd_98", NULL };
		static const char* kBgmlOnce[] = { "BGML_98", "bgml", NULL };
		static const char* kSs98Once[] = { "SS_98", "ss_98", NULL };
		/* MMD2 glue cmd0 INT D2 AH=3 STI-waits [f8f] then loads+AH=1.
		   A second INT 7F re-enters AH=3 (which does not reprogram 0x27)
		   and the render pump never leaves that wait (michael/orangerd). */
		static const char* kMmdOnce[] = { "mmd2", "MMD2", "mmd2va", NULL };
		static const char* kOpndrvOnce[] = { "fugam", "fgplay", NULL };
		/* olteus overlay play (INT7F cmd2 → MAP:D471) loads 20KB MUS+MTB;
		   a second poke restarts the load mid-DEF1. */
		static const char* kOlteusOnce[] = { "olteus", NULL };
		/* NARU_98 cmd0 is INT 70 stop + AH=3F + load + play. A second INT 7F
		   re-enters at stop (all-notes-off) and the shorter drain often never
		   reaches AH=1, so [232] stays 0 and the PIT ISR emits no notes. */
		static const char* kNaruOnce[] = { "naru", "NARU", NULL };
		static const char* kMfdOnce[] = { "mfd", "MFD", NULL };
		/* 7COLM cmd0 is INT 41 AH=82 (stop) then load+AH=81. A second poke
		   re-enters at stop. */
		static const char* kMidiDrvOnce[] = {
			"MIDIDRV", "mididrv", "7COLM", "7colm", NULL
		};
		static const char* kAvalonOnce[] = { "avalon", NULL };
		/* SYNUP_98 cmd0 already INT D3 play; a second poke #UD's (C1) and
		   can trash IVT before OverlayTitle of pick 2. */
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
			/* BGML_98 (and other speaker rips) drive melody from IRQ0/INT08.
			   BootDos starts with PIC mask 0xFF; the player may unmask in
			   INT 7F, but IF/IRQ0 must stay live for the whole render.
			   FMD MIDI uses the same IRQ0 unmask (INT0B is not hooked). */
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
		/* famistava installs OPN ISR on INT14 during the play far-call — BootDos
		   is too early. Mirror only when INT0B is still vacant. */
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
			/* rtypeva: start parses channels but leaves the ISR stream ([01C2]) and
			   [000F] idle; mute also clears OPN timer. Arm stream from ch0 + timer. */
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
		/* usd_98 / ADVBIOS: INT7F does F4 AH=0 (load→55D1) + AH=1 (arm).
		   Timers + music ISR start via F4 AH=0x30, which plants ADVBIOS
		   CS:0690 on INT16. Mirror that ISR to INT0B for OPN timer ticks.
		   ADVH.EXE packs use INT F1 (mode=1) with song words at CS:0712/0716
		   instead of classic 0480/0484 — F4 stays trampoline. */
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
					/* Finish Microsoft PACKED / xor-decrypt ADVH only when F1 is
					   still trampoline after BootDos (watagolf already live). */
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
						/* Song workspace: ADVBIOS F4 AH=0 does mov di,imm16. */
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
							/* ADVBIOS: AH=0x30 arms timers+ISR; AH=1 plays. */
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
						/* ADVH INT F1: call the driver's published API only.
						   EB 06 = filename open (AL=0 -> INT21 AH=3D);
						   EB 0F = memory load (AL=0 needs DS:0 + CX=len). */
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
							/* If INT7F left an empty buffer, materialize USO
							   from the DOS file table (real disk contents). */
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
								/* AL=0 (early CS+0x33 intact), AL=1 bind (BootDos
								   retargeted bind immediates to CS). Restore ISR
								   after bind walks slots through @04AB. */
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
								/* Re-enter TriggerPlay once; return immediately so
								   later ISR/timer assists cannot mute the replay. */
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
							/* Memory-load: if AL=0 left the play flag clear
							   (INT7F often never set CX=len into the driver's
							   copy), place the USO into the work buffer and
							   re-issue AL=1. */
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
						/* Bind OPN IRQ3 (INT 0B) to the driver's real music ISR.
						   nameLoadAdvh: INT F1 entry is EB 06 'U' filename-load ADVH. */
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
							/* Filename-load ADVH leaves OEM ISR near 04AB. */
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
							/* Name-load ADVH: AL=1 programs notes but leaves the
							   play/channel BSS thin. Forcing Timer A/B here makes
							   the OEM ISR run immediately and key-off everything
							   (peak→0). Memory-load (EB 0F / watagolf) arms itself. */
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
		/* FMPP / NLP_HOOT / MAKO_98: INT7F cmd0 loads; play is cmd2 (INT D2 /
		   INT60 / INT40). TriggerPlay only fired cmd0 — re-fire with EXT_CMD=2.
		   HuLinks fakecall/music/46 is different: 46.com cmd2 → INT70 AH=1 which
		   is MUSIC.COM mute-all (keys off + sets each channel [CS:ch+3]=1 so the
		   sequencer early-returns forever). Play-enable is INT70 AH=2.

		   TGLFMP/TGLFMP2 are NOT cmd2-play: their cmd0 already does INT D2 AL=0
		   stop + AH=3F read + AL=1 play. cmd2 is `MOV AX,1009 / INT D2` → FMP3
		   fn09 which sets [29AE]=1 and fade counts to 0x10, arming a fade-out
		   that stops vg2 after the first phrase. Keep them in kGluePlay for IRQ
		   unmask below, but do not re-fire cmd2. */
		{
			static const char* kStarPlay[] = {
				"fakecall", "music", "MUSIC", "46", NULL
			};
			/* TAM PLAY5/PLAY3 shipped as MUSIC.COM + PLAY5_98; that is
			   INT F2, not HuLinks INT70. Prefix "MUSIC" must not steal it. */
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
				/* Hoot/GMPV4 families: INT7F/D2/60 cmd0 loads, cmd2 plays. */
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
				/* midiout catalog shells (GS/MPU). cmd2 is stop — kSkipCmd2. */
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
			/* TGLFMP cmd2 fades. PLAY5_98 cmd2 is INT F2 AX=2 → $4F9F
			   mute/init; play is already cmd0 (copy + $4F9F + $4EDC).
			   MIZ3_98 cmd2 is INT40 AH=6 stop; cmd0 already AH=6 then AH=5 play.
			   ELFMUS98 cmd2 is INT60 AX=0 stop; cmd0 already INT60 AH=1 play.
			   SYNTH_98 cmd2 is INT60 AH=1; play is cmd0 AH=0.
			   MAGIC_98 cmd2 is INT EF AX=4; play is cmd0 AX=5.
			   ARTDI_98 cmd2 is far [3da](1); load/play is cmd0 [3d6].
			   LUDY_98 cmd2 is INT52 AX=1; play is cmd0 AX=0.
			   MUSE_98 cmd2 far-calls stop; cmd0 already loads+plays.
			   MDR_98 glue cmd2 INT40 BX=6 waits on [1AC2] forever; cmd0
			   already loads and 0x127a/D78 stops busy tracks. Skip cmd2.
			   mmd2 cmd2 INT D2 AX=608; cmd0 already AH=1 play.
			   iwaplay cmd2 INT EB AX=308; cmd0 already AH=1 play.
			   SPLIT_98 cmd2 INT D2 AX=100; play is AX=101.
			   tky98 cmd2 INT F1 AL=12; play is AL=11.
			   bgmdrv98/bp/FMXP/NC cmd2 repeats the stop half of cmd0.
			   FMD /# + fugam cmd2 is INT D3 AX=01FF (stop) after cmd0 play.
			   fgplay_h cmd2 is INT D2 AX=3 CL=8 (stop); play is cmd0 AX=1. */
			static const char* kSkipCmd2[] = {
				"tglfmp", "TGLFMP",
				"PLAY5", "play5", "PLAY5_98", "PLAY3",
				"MIZ3", "miz3", "MIZ3_98",
				"ELFMUS98", "elfmus", "ELFMUS",
				"SYNTH_98", "synth", "SYNTHIA",
				"MAGIC_98", "magic_", "MAGIC_",
				"ARTDI_98", "artdi",
				/* EMIT_98 INT40 cmd2 is FMDRV AX=0200 stop (same as TENSH). */
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
				/* NARU_98 is also kGluePlay via the "NA" prefix (NA.COM / NAX).
				   cmd2 is INT 70 stop. */
				"naru", "NARU",
				NULL
			};
			if (DosShellStarts(dosGe_, kGluePlay)
				&& !DosShellStarts(dosGe_, kSkipCmd2)) {
				/* The list is matched by command prefix, and "cmd2 plays" is
				   only true for part of it — MAKO_98 answers cmd2 with its
				   mute-all (reg 27 timers off, every TL to 7F, SSG mixer off),
				   which silenced a song that cmd0 had already started. Rather
				   than keep guessing per shell, notice when the re-fire
				   stopped the sequencer instead of starting it and put the
				   working command back. */
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
				/* AH=2: [0290]=1 enables ISR; AL → [0292]/[0293] countdown.
				   When [0294] hits 0x10 the ISR mute-alls and clears [0290].
				   AL=FFh + reset [0294] keeps BGM alive. Beat = OPN INT0B
				   only (no PIT twin). */
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
				/* Unmask OPN IRQ for non-star glue drivers. */
				uint8_t* mem = np2_mem();
				static const char* kSlave14[] = {
					"mbmus", "MBMUS", "musdrv", "MUSDRV", "muse", "MUSE",
					"fplay", "FPLAY", NULL
				};
				const int slave14 = DosShellStarts(dosGe_, kSlave14)
					|| ((ssgPortAJumper_ & 0xC0) == 0xC0);
				if (mem) {
					if (slave14 && IvtHooked(0x14, 1)) {
						/* Keep ISR on INT14; unmask cascade + IRQ12. */
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
					/* S20 INT60 AH=0 returns with IF=0; SYNTH_98 IRET then
					   leaves the render pump deaf (hsj GIRL dumps=1, ifoff). */
					np2_reg_set(NP2_R_FLAGS,
						(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
					/* FMD MIDI clock: no OPN ISR (INT0B stays trampoline).
					   Unmask IRQ0 and run the PIT so INT D3 can tick. */
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
					/* ABIKO-class: hooks INT 1C, leaves INT 08 to BIOS. */
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
					/* VALKY/SSCP plants the sequencer on INT 08 (not OPN
					   Timer B). IRQ0 stayed masked so pitirq=1 and dumps
					   stuck at FM_TONE init. */
					{
						static const char* kValkyPit[] = {
							"VALKY_98", "valky", NULL
						};
						if (DosShellStarts(dosGe_, kValkyPit)
							&& IvtHooked(PC98_TIMER_VEC, 1)) {
							picMask_ = (uint8_t)(picMask_ & 0xfeu);
							ValkyArmSscpPlay(mem, (uint16_t)(titleCode & 0xffff));
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
		}
		return 1;
	}

	int loaded = 0;
	/* Ys/Ys2 Falcom glue sets dataAddr_ inside cmd0 (HostService 0x10) per
	   bank — do not preload against a stale address from the previous title. */
	if (dataAddr_ > 0 && bootCs_ != 0x0160)
		loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
	if (data2Addr_ > 0)
		LoadSongToAddr(song & 0xff, data2Addr_, file2Size_, 1);

	/* BirdySoft CAL/PAL: driver relocates an OPN ISR but never writes IVT 0x0B.
	   Play (INT60) sets wait-flag [DS:269B]=FF and spins until the ISR clears
	   it — without the vector DeliverIrqs refuses OPN IRQs and play hangs.
	   Install the relocated ISR (FB50… or PUSH…/OUT 0Ah/STI variant) and park
	   IRET on INT08 when the chain target is still null. */
	if (cal98_) {
		uint8_t* mem = np2_mem();
		if (mem) {
			const unsigned o60 = (unsigned)mem[0x60 * 4] | ((unsigned)mem[0x60 * 4 + 1] << 8);
			const unsigned s60 = (unsigned)mem[0x60 * 4 + 2] | ((unsigned)mem[0x60 * 4 + 3] << 8);
			/* Prefer relocated INT60 segment; MF-era __02.DAT also lives at
			   phys 0x1000 before/without a finished INT60 install. */
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
				/* Fix INT60 near-call table if reloc left sentinel EB00. */
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

	/* QueenSoft MADP: INT40 is AL-indexed API; AL=1A plants OPN ISR on
	   INT14 (IVT@0x50) like N3GOLF. Mirror INT14→INT0B + unmask IRQ3. */
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

	/* N3GOLF: glue parks the OPN timer ISR on INT 14h (IRQ12 path on the
	   real board) but never writes IVT 0x0B. Our OPN IRQ is delivered as
	   master IRQ3 → INT 0x0B, so copy the INT14 handler there and unmask.
	   Songs live inside fm.bin (filesize=0); play is INT7F cmd0 + EXT_SONG. */
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
			/* Fallback: scan fm.bin segment 0x0100 for CLI;PUSH DS;PUSH ES;PUSHA. */
			if (!ok) {
				const unsigned base = 0x0100u << 4;
				for (unsigned p = base; p + 32 < base + 0x8000u; p++) {
					if (mem[p] == 0xFA && mem[p + 1] == 0x1E
						&& mem[p + 2] == 0x06 && mem[p + 3] == 0x60
						&& mem[p + 4] == 0x8C && mem[p + 5] == 0xC8) {
						/* Prefer the EOI-bearing ISR (OUT 00h,20h nearby). */
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
		/* Fall through: cmd0 + song in EXT_SONG starts play (no dataaddr). */
	}

	/* Falcom PROG.BIN (Alm/LM): OPN detect success plants the music ISR on
	   INT14/INT15 (not INT0B). Mirror like MADP/N3GOLF so YM timer IRQs run.
	   Detect-fail path uses INT08 — keep PIT alive either way. */
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

	/* KSK DKS/FQ (dks/duelsc/fq3/fq4): after boot INT69 AH=0A, CS:[tableVar]
	   points at the size-prefixed song bank (BSS past the driver image).
	   Locate the AH=0A prologue (06 1E 60 0E 1F B8 xx xx … A3 table), load
	   the catalog song there, and force cmd1 (AH=0 play). */
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
					/* Common: 06 1E 60 0E 1F B8 … 03 C1 89 1E .. A3 table */
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
					/* FQ3 BGMDRV: … 03 C1; CS: MOV [seg],BX; CS: MOV [table],AX */
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

	/* Glodia MDPLAY.BIN / MDRIVE.BIN: ensure INT08 has the driver's PIT ISR.
	   Prefer an EOI-bearing stub that also CALLs (sequencer), not a lone EOI/IRET. */
	if (mdplay98_ && !IvtHooked(PC98_TIMER_VEC, 0)) {
		uint8_t* mem = np2_mem();
		if (mem) {
			unsigned bases[3];
			int nBase = 0;
			bases[nBase++] = 0x1000u << 4;
			/* biblem MDRIVE @0x2B000; INT40 seg is a good hint when hooked. */
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

	/* Wolfteam 000_BOOT: glue INT 7Fh cmd1 → INT 4Ah (song in AX). INT 4A
	   calls INT 43h to (re)load BX:0000 from the game filesystem, which wipes
	   our dataaddr preload. Park IRET on INT 43 and issue cmd1 only (cmd0 is
	   stop / AX=FFFFh). */
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
			wolfSyncRun_ = 1; /* ISR delay stubs need not-busy E0D2 */
			/* Boot PIC ICWs often leave IRQ0 masked; music ticks need PIT. */
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
				/* INT 4C AH=00 expects DS:SI → MF instrument (byte13=0x28 after
				   ADD SI,8). Skip when no MI (apros) — bogus song-as-MI hangs
				   the bank load and never arms the sequencer. */
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
					/* dmdply never installs INT4C — only call when hooked. */
					if (IvtHooked(0x4C, 0)) {
						mem[wolfGateStop_] = 0xFF;
						mem[wolfGatePlay_] = 0x00;
						np2_reg_set(NP2_R_AX, 0x0000);
						np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
						np2_interrupt(0x4C);
						DrainInterrupt(drainBudget / 2);
					}
				}
				/* Re-bind song ptr for ISR. Channel stream offsets from 6692
				   are relative to the dataaddr load (7800:0000), not CS:songBuf —
				   keep DS=dataaddr>>4 so SI like 0603 hits the song. */
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
					/* INT4C epilogue + sequencer arm (see 000_BOOT @~6692):
					   MOV BYTE [fa],FF / MOV WORD [fa+3],1 / MOV BYTE [fa+2],0.
					   dmdply folds play-gate into fa+2 — keep that byte FF. */
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

	/* KOEI98 glue (funcvect INT 40h): cmd0=play, cmd2=stop. Title high word is
	   the music-bank segment (EXT 0x7E4); low word is song (0x7E2). Song data
	   already resides in packed code ROMs — no dataaddr preload. */
	if (koei98_ || funcVect_ == 0x40) {
		extCmd_ = 0;
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget);
		return 1;
	}

	/* Song preload only when catalog dataaddr is set (no CS invent). */
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

	/* Beast3 BST3 glue: cmd0 with AH==0 far-calls stop; AH!=0 selects load
	   (then clears AH). Catalog titles are 00xx so default cmd0 never loads. */
	if (bst398_)
		extSong_ = (uint16_t)((song & 0xff) | 0x0100);

	/* DOFMD_98 play path (gated by dofmd_): glue INT 7Fh cmd=1 queries host
	   0x11 for a real-mode song ptr (SI/DS), then INT 45h into MSC/MV22/etc.
	   Boot also needs the INT 14h IRET stub installed in LoadRoms. Default
	   cmd0/cmd1 INT sequence below is sufficient once those are in place. */

	/* NOPNDRV keeps song ptr at DS:19F4 (off) / DS:19F6 (seg) with DS=driver
	   CS (0x1000). AH=3 normally sets these from INT BX/ES, but software-INT
	   nesting made that unreliable — poke the words then INT7F play.
	   Only for actual NOPNDRV.COM loads (MUSIC.SYS / MUSDRV2 differ). */
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

			/* Also issue AH=3 with IF clear so the driver-side bind matches. */
			np2_reg_set(NP2_R_ES, songSeg);
			np2_reg_set(NP2_R_BX, songOff);
			np2_reg_set(NP2_R_AX, 0x0300);
			np2_reg_set(NP2_R_FLAGS, 0x0002); /* IF clear — no nested IRQs */
			np2_interrupt(0x42);
			DrainInterrupt(drainBudget / 4);

			/* Re-assert ptr in case AH=3 clobbered it with a bad stack read. */
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

	/* Xanadu Scenario II (xana2e @1F000 / PR.NO0 @4000 / PR.NO5 @10000):
	   INT7F reads EXT_SONG (07E2) as the command and EXT_CMD (07E0) as the
	   play selector — ports swapped vs Ys/00BIOS glue. Cmd FD far-calls
	   xana2e → PR.NO5 (INT14/15 + OPN). Play is cmd0 + selector 1 with the
	   song already at dataaddr; only selector 1 takes the INT7E+INT41 path. */
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
			extSong_ = 0; /* command = play */
			extCmd_ = 1;  /* selector = INT7E + INT41 AH=2 */
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
			/* Re-park on the glue INT18 idle so DrainInterrupt/Render stay sane. */
			np2_set_cs_ip(0x0000, 0x063C);
			np2_reg_set(NP2_R_FLAGS,
				(uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			(void)loaded;
			return 1;
		}
	}

	/* An INT 1C driver expects the BIOS to already be ticking IRQ0; nothing
	   here programs the PIT on its behalf, so give it the tick it is waiting
	   for.  Only when it owns no OPN timer — a driver that runs off the chip
	   does not need this, and an extra tick would double-drive it. */
	extCmd_ = 0;
	np2_interrupt((uint8_t)funcVect_);
	DrainInterrupt(drainBudget);
	/* After cmd0, Falcom glue may HostService(0x10) a song dest — load then
	   cmd1. Catalog dataaddr alone is also enough (no CS invent). */
	if ((bootCs_ == 0x0160 || rx98_ || fmd98_ || prog98_ || dataAddrHost_)
		&& dataAddr_ > 0 && fileSize_ > 0) {
		loaded = LoadSongToAddr(song & 0xff, dataAddr_, fileSize_, 0);
		/* Telenet splits a song into a bgm/bgm2 pair; the driver reads both,
		   so loading only the primary leaves it waiting on half a song.
		   Confined to the guest-supplied case so the Falcom families above
		   keep their existing single-file behaviour. */
		if (loaded && dataAddrHost_ && data2Addr_ > 0 && file2Size_ > 0)
			LoadSongToAddr(song & 0xff, data2Addr_, file2Size_, 1);
	}
	if (loaded || lastSongLoadOk_) {
		extCmd_ = 1;
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget);
		/* After cmd1, some glues (Ys MANPRG, Beast3, Wolf SS) leave the YM
		   ISR on INT14/15 only. Mirror to INT0B for DeliverIrqs.
		   Skip lone-IRET stubs (DOFMD/BRANM park serial INT14 at 0000:0500
		   — copying that onto INT0B wipes MSC's OPN ISR). */
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
				/* DOFMD serial stub: single IRET (and our 0x500 park). */
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
	/* SORC98 glue only (boot CS=0, song @3000/VA@11800, wstimer): INT 7Fh
	   maps cmd0→INT D2 AL=3 (start) and cmd1→AL=0 (channel init). Default
	   cmd0-then-cmd1 leaves start cleared — re-issue cmd0 so play sticks.
	   Do NOT force-clear [085A].7 (old TickSide hammer): that made CALL 1CEE
	   run every quantum and stuck SSG3 R0A at 0x0F. Native BIOS keeps .7 set
	   and advances music on OPN Timer B (hoot-correct SSG gating).
	   Do NOT apply to other wstimer games (Ys CS=0160 etc.). */
	if (sorcGlue_) {
		extCmd_ = 0;
		np2_interrupt((uint8_t)funcVect_);
		DrainInterrupt(drainBudget / 2);
		/* Do NOT clear [085A] bit7 here or in TickSide. BIOS uses bit7 to
		   gate INT08→CALL 1CEE; forcing it clear made FM advance but stuck
		   SSG3 volume at 0x0F (hoot-correct gating needs the native skip).
		   Music continues on OPN Timer B while [085A].7 stays set. */
		/* Ensure PIT ticks remain available if the boot left IRQ0 masked. */
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

void CHardPc98::DrainInterrupt(uint64_t budgetCycles)
{
	/* Step until we return near the boot HLT idle (CS==bootCs, IP in F4 patch)
	   or consume budget. Also keep OPN/PIT ticking so nested timer IRQs work. */
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

int CHardPc98::TriggerStop()
{
	extCmd_ = 2;
	np2_interrupt((uint8_t)funcVect_);
	return 1;
}

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

CHardPc98* CEmuHardPc98GetActive()
{
	return g_pc98Active;
}
