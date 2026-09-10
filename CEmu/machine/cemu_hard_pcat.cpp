#include "StdAfx.h"
#include "cemu_hard_pcat.h"
#include "../chip/cemu_chip_opl.h"
#include "../chip/cemu_chip_saa1099.h"
#include "../chip/cemu_chip_sn76489.h"
#include "../fmmon/fmmon_shadow.h"
#include "../vendor/np2/np2ffi.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* CEMU_PCAT_IVT=<path>: one record per play poke naming the vector we fire,
   the vectors the guest actually owns, and how much the sound hardware moved.
   A family that is uniformly mute is either never reaching its driver API or
   reaching it and writing nothing, and these two numbers separate those. */
namespace {

struct PcatCensus {
	int funcVect;
	unsigned oplWr, oplKey, saaWr, saaTone, spkTone, midi, irq0;
	unsigned pitReload, pitHz;
};

void PcatIvtCensus(const uint8_t* mem, const PcatCensus& c, const char* phase,
	const wchar_t* tag)
{
	static const char* path = NULL;
	static int checked = 0;
	if (!checked) { checked = 1; path = getenv("CEMU_PCAT_IVT"); }
	if (!path || !path[0] || !mem) return;
	FILE* f = NULL;
	if (fopen_s(&f, path, "a") != 0 || !f) return;
	fprintf(f, "IVT %-4s funcvect=%02X opl=%u key=%u saa=%u tone=%u spk=%u"
		" midi=%u irq0=%u pit=%u(%uHz) v08=%04X:%04X v1C=%04X:%04X hooked=",
		phase, c.funcVect, c.oplWr, c.oplKey, c.saaWr, c.saaTone, c.spkTone,
		c.midi, c.irq0, c.pitReload,
		c.pitReload ? (c.pitHz / c.pitReload) : 0u,
		(unsigned)(mem[0x08 * 4 + 2] | (mem[0x08 * 4 + 3] << 8)),
		(unsigned)(mem[0x08 * 4 + 0] | (mem[0x08 * 4 + 1] << 8)),
		(unsigned)(mem[0x1C * 4 + 2] | (mem[0x1C * 4 + 3] << 8)),
		(unsigned)(mem[0x1C * 4 + 0] | (mem[0x1C * 4 + 1] << 8)));
	for (unsigned v = 0; v < 256; v++) {
		const unsigned off = (unsigned)mem[v * 4] | ((unsigned)mem[v * 4 + 1] << 8);
		const unsigned seg = (unsigned)mem[v * 4 + 2] | ((unsigned)mem[v * 4 + 3] << 8);
		if ((seg == 0 && off == 0) || seg == DOS98_TRAMP_SEG) continue;
		fprintf(f, "%02X=%04X:%04X,", v, seg, off);
	}
	/* %ls aborts the whole call on a wide char with no multibyte form, which
	   would swallow the newline for every Japanese title. */
	fputs(" name=", f);
	for (const wchar_t* p = tag; p && *p; p++)
		fputc((*p >= 0x20 && *p < 0x7f) ? (char)*p : '?', f);
	fputc('\n', f);
	fclose(f);
}

/* CEMU_PCAT_IPPROF=<file>: histogram of the linear PC executed during the
   play pump. A driver that installs its ISR and then makes no sound is either
   spinning on one wait or running somewhere it should not be, and the hot
   address says which. */
struct PcatIpProf {
	enum { SLOTS = 4096 };
	enum { RING = 65536 };
	unsigned addr[SLOTS];
	uint64_t hits[SLOTS];
	uint64_t total;
	const char* path;
	/* The histogram cannot show which of a dozen exit(1) branches a guest
	   took, so keep the trail leading up to the moment the host freezes it
	   (the first DOS terminate). */
	unsigned ring[RING];
	unsigned ringPos;
	int frozen;

	PcatIpProf() : total(0), path(NULL), ringPos(0), frozen(0)
	{
		memset(addr, 0xff, sizeof(addr));
		memset(hits, 0, sizeof(hits));
		memset(ring, 0, sizeof(ring));
	}

	void Freeze() { frozen = 1; }

	void Note(unsigned lin)
	{
		total++;
		if (!frozen) ring[ringPos++ % RING] = lin;
		const unsigned h = (lin * 2654435761u) & (SLOTS - 1);
		for (unsigned i = 0; i < 64; i++) {
			const unsigned s = (h + i) & (SLOTS - 1);
			if (addr[s] == 0xffffffffu) { addr[s] = lin; hits[s] = 1; return; }
			if (addr[s] == lin) { hits[s]++; return; }
		}
	}

	~PcatIpProf()
	{
		if (!path || !total) return;
		FILE* f = NULL;
		if (fopen_s(&f, path, "a") != 0 || !f) return;
		fprintf(f, "IPPROF total=%llu\n", (unsigned long long)total);
		for (int rank = 0; rank < 80; rank++) {
			int best = -1;
			for (int s = 0; s < SLOTS; s++)
				if (hits[s] && (best < 0 || hits[s] > hits[best])) best = s;
			if (best < 0) break;
			fprintf(f, "  %2d %05X %10llu %5.1f%%\n", rank, addr[best],
				(unsigned long long)hits[best],
				100.0 * (double)hits[best] / (double)total);
			hits[best] = 0;
		}
		const unsigned n = ringPos < RING ? ringPos : (unsigned)RING;
		fprintf(f, "IPRING n=%u (oldest first)\n", n);
		for (unsigned i = 0; i < n; i++) {
			const unsigned s = (ringPos >= RING)
				? ((ringPos + i) % RING) : i;
			fprintf(f, "%05X%s", ring[s], ((i % 8) == 7) ? "\n" : " ");
		}
		fputc('\n', f);
		fclose(f);
	}
};

/* CEMU_PCAT_MEMDUMP="<linhex>,<len>,<path>": hex of guest memory the first
   time the play pump returns, for reading whatever the profiler pointed at. */
void PcatMemDump(const uint8_t* mem)
{
	static const char* spec = NULL;
	static int checked = 0;
	if (!checked) { checked = 1; spec = getenv("CEMU_PCAT_MEMDUMP"); }
	if (!spec || !spec[0] || !mem) return;
	static int done = 0;
	if (done) return;
	done = 1;
	unsigned lin = 0, len = 0;
	char path[260];
	if (sscanf_s(spec, "%x,%u,%259s", &lin, &len, path, (unsigned)sizeof(path)) != 3)
		return;
	if (len > 0x20000 || lin + len >= 0x200000u) return;
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

/* CEMU_PCAT_IOLOG="<path>": which ports a guest touches that nothing here
   answers. A driver that probes for its card by reading a register back is
   indistinguishable from a silent driver without this. */
void PcatIoLog(char dir, uint16_t port, uint8_t data)
{
	static const char* path = NULL;
	static int checked = 0;
	if (!checked) { checked = 1; path = getenv("CEMU_PCAT_IOLOG"); }
	if (!path || !path[0]) return;
	/* One line per port and direction: the probe loops thousands of times. */
	static uint8_t seen[2][8192];
	const int d = (dir == 'w') ? 1 : 0;
	if (seen[d][port >> 3] & (1u << (port & 7))) return;
	seen[d][port >> 3] |= (uint8_t)(1u << (port & 7));
	FILE* f = NULL;
	if (fopen_s(&f, path, "a") != 0 || !f) return;
	fprintf(f, "IO %c %04X %02X\n", dir, port, data);
	fclose(f);
}

/* MID.DRV byte-scans every track for the next "MTrk". Type-1 files with
   many tracks (or MTrk-like bytes in the payload) either miss the play
   flag or walk off into the IVT. Fold to Type-0 so AH=0 sees one MTrk. */
struct MokSmfEv {
	uint32_t tick;
	uint8_t n;
	uint8_t b[3];
};

static int MokSmfEvCmp(const void* a, const void* b)
{
	const MokSmfEv* x = (const MokSmfEv*)a;
	const MokSmfEv* y = (const MokSmfEv*)b;
	if (x->tick < y->tick) return -1;
	if (x->tick > y->tick) return 1;
	return 0;
}

static unsigned MokReadVlq(const uint8_t* p, unsigned n, unsigned* i)
{
	unsigned v = 0, k = 0;
	while (*i < n && k < 4) {
		const uint8_t b = p[(*i)++];
		v = (v << 7) | (unsigned)(b & 0x7f);
		k++;
		if ((b & 0x80) == 0) break;
	}
	return v;
}

static void MokWriteVlq(uint8_t* o, unsigned* p, unsigned cap, unsigned v)
{
	uint8_t tmp[5];
	int n = 0;
	tmp[n++] = (uint8_t)(v & 0x7f);
	v >>= 7;
	while (v) {
		tmp[n++] = (uint8_t)((v & 0x7f) | 0x80);
		v >>= 7;
	}
	while (n--) {
		if (*p < cap)
			o[(*p)++] = tmp[n];
	}
}

static int MokFlattenSmfType0(const uint8_t* src, unsigned srcN,
	uint8_t* dst, unsigned dstCap, unsigned* outN)
{
	if (!src || !dst || !outN || srcN < 14 || dstCap < 22) return 0;
	*outN = 0;
	if (src[0] != 'M' || src[1] != 'T' || src[2] != 'h' || src[3] != 'd')
		return 0;
	const unsigned hdrLen = ((unsigned)src[4] << 24) | ((unsigned)src[5] << 16)
		| ((unsigned)src[6] << 8) | src[7];
	if (hdrLen < 6 || 8 + hdrLen > srcN) return 0;
	const unsigned fmt = ((unsigned)src[8] << 8) | src[9];
	const unsigned ntrks = ((unsigned)src[10] << 8) | src[11];
	const unsigned div = ((unsigned)src[12] << 8) | src[13];
	if (div & 0x8000) return 0;
	if (fmt == 0 && ntrks == 1) return 0;
	if (ntrks == 0 || ntrks > 64) return 0;

	enum { kMaxEv = 32768 };
	MokSmfEv* ev = new MokSmfEv[kMaxEv];
	unsigned nev = 0;
	unsigned pos = 8 + hdrLen;
	uint8_t tempo[3] = { 0x07, 0xA1, 0x20 };
	int haveTempo = 0;
	for (unsigned t = 0; t < ntrks && pos + 8 <= srcN; t++) {
		if (src[pos] != 'M' || src[pos + 1] != 'T'
			|| src[pos + 2] != 'r' || src[pos + 3] != 'k') {
			delete[] ev;
			return 0;
		}
		const unsigned ln = ((unsigned)src[pos + 4] << 24)
			| ((unsigned)src[pos + 5] << 16)
			| ((unsigned)src[pos + 6] << 8) | src[pos + 7];
		pos += 8;
		const unsigned end = pos + ln;
		if (end > srcN) {
			delete[] ev;
			return 0;
		}
		uint32_t tick = 0;
		uint8_t run = 0;
		while (pos < end && nev < (unsigned)kMaxEv) {
			tick += MokReadVlq(src, end, &pos);
			if (pos >= end) break;
			uint8_t st = src[pos];
			if (st & 0x80) {
				pos++;
				if (st < 0xf0) run = st;
			} else {
				st = run;
				if (!st) { pos++; continue; }
			}
			if (st == 0xff) {
				if (pos >= end) break;
				const uint8_t mt = src[pos++];
				const unsigned ml = MokReadVlq(src, end, &pos);
				if (mt == 0x2f) break;
				if (mt == 0x51 && ml == 3 && pos + 3 <= end && !haveTempo) {
					tempo[0] = src[pos];
					tempo[1] = src[pos + 1];
					tempo[2] = src[pos + 2];
					haveTempo = 1;
				}
				pos += ml;
				continue;
			}
			if (st == 0xf0 || st == 0xf7) {
				const unsigned sl = MokReadVlq(src, end, &pos);
				pos += sl;
				continue;
			}
			if (st >= 0xf0) { run = 0; continue; }
			const int need = ((st & 0xf0) == 0xc0 || (st & 0xf0) == 0xd0) ? 1 : 2;
			if (pos + (unsigned)need > end) break;
			MokSmfEv e;
			e.tick = tick;
			e.n = (uint8_t)(1 + need);
			e.b[0] = st;
			e.b[1] = src[pos++];
			e.b[2] = (need == 2) ? src[pos++] : (uint8_t)0;
			ev[nev++] = e;
		}
		pos = end;
	}
	if (nev == 0) { delete[] ev; return 0; }
	qsort(ev, nev, sizeof(ev[0]), MokSmfEvCmp);

	unsigned o = 0;
	auto put = [&](uint8_t v) {
		if (o < dstCap) dst[o++] = v;
	};
	put('M'); put('T'); put('h'); put('d');
	put(0); put(0); put(0); put(6);
	put(0); put(0); put(0); put(1);
	put((uint8_t)(div >> 8)); put((uint8_t)div);
	put('M'); put('T'); put('r'); put('k');
	const unsigned lenAt = o;
	put(0); put(0); put(0); put(0);
	MokWriteVlq(dst, &o, dstCap, 0);
	put(0xff); put(0x51); put(0x03);
	put(tempo[0]); put(tempo[1]); put(tempo[2]);
	uint32_t prev = 0;
	for (unsigned i = 0; i < nev; i++) {
		if (o + 8 >= dstCap) break;
		const uint32_t dt = ev[i].tick - prev;
		prev = ev[i].tick;
		MokWriteVlq(dst, &o, dstCap, dt);
		for (uint8_t k = 0; k < ev[i].n && k < 3; k++)
			put(ev[i].b[k]);
	}
	MokWriteVlq(dst, &o, dstCap, 0);
	put(0xff); put(0x2f); put(0x00);
	const unsigned body = o - (lenAt + 4);
	dst[lenAt] = (uint8_t)(body >> 24);
	dst[lenAt + 1] = (uint8_t)(body >> 16);
	dst[lenAt + 2] = (uint8_t)(body >> 8);
	dst[lenAt + 3] = (uint8_t)body;
	delete[] ev;
	if (o < 22) return 0;
	*outN = o;
	return 1;
}

/* Mok SMF/HOOT CODE.COM: `call set_busy` is assembled so it lands in the
   `int 18` / `jmp short` idle instead of `out 07E8,80; ret`. INT7F then
   never returns. Retarget those near calls to the out-80 stub. */
int PatchMokHootIdleCalls(uint8_t* mem, uint16_t psp, unsigned imageSize)
{
	if (!mem || psp < 0x0100 || imageSize < 24 || imageSize > 4096)
		return 0;
	const unsigned img = ((unsigned)psp << 4) + 0x100u;
	if (img + imageSize >= 0x200000) return 0;
	static const uint8_t kIdleOut[] = {
		0xB8, 0x01, 0x98, 0xCD, 0x18, 0xEB, 0xF9,
		0xBA, 0xE8, 0x07, 0xB0, 0x80, 0xEE, 0xC3
	};
	int found = -1;
	for (unsigned i = 0; i + sizeof(kIdleOut) <= imageSize; i++) {
		if (memcmp(mem + img + i, kIdleOut, sizeof(kIdleOut)) == 0) {
			found = (int)i;
			break;
		}
	}
	if (found < 0) return 0;
	const uint16_t idleIp = (uint16_t)(0x100u + (unsigned)found);
	const uint16_t outIp = (uint16_t)(idleIp + 7u);
	int n = 0;
	for (unsigned i = 0; i + 3 <= imageSize; i++) {
		if (mem[img + i] != 0xE8) continue;
		const uint16_t next = (uint16_t)(0x100u + i + 3u);
		const int16_t disp = (int16_t)(mem[img + i + 1] | (mem[img + i + 2] << 8));
		const uint16_t tgt = (uint16_t)(next + disp);
		if (tgt < idleIp || tgt >= outIp) continue;
		const int16_t nd = (int16_t)(outIp - next);
		mem[img + i + 1] = (uint8_t)(nd & 0xff);
		mem[img + i + 2] = (uint8_t)((uint16_t)nd >> 8);
		n++;
	}
	return n;
}

/* Infogrames code.com MZ load: `minalloc - header_paras` extra. Real DOS
   adds minalloc paragraphs after the image. ASOUND.DRV BSS (play-enable at
   DGROUP:19AAh, voice tables) sits in that extra; the subtract leaves only
   48 bytes and the tick path sees [19AA]==0 → keys=0. NOP the subtract so
   extra is minalloc*16 (and SS:SP at 0x30A still fits). */
void PatchInfogramesMzExtra(uint8_t* mem, uint16_t psp, CEmuDos98& dos)
{
	(void)dos;
	if (!mem || psp < 0x0100) return;
	const unsigned img = ((unsigned)psp << 4) + 0x100u;
	if (img + 8 >= 0x200000) return;
	/* `sub cx,[033F]` — header paras. Scan the tiny COM; zip/XML spelling
	   of ASOUND.DRV must not gate this. */
	for (unsigned i = 0; i + 4 < 0x400 && img + i + 4 < 0x200000; i++) {
		if (mem[img + i] == 0x2B && mem[img + i + 1] == 0x0E
			&& mem[img + i + 2] == 0x3F && mem[img + i + 3] == 0x03) {
			mem[img + i] = 0x90;
			mem[img + i + 1] = 0x90;
			mem[img + i + 2] = 0x90;
			mem[img + i + 3] = 0x90;
			break;
		}
	}
}

static uint16_t PcatMem16(const uint8_t* mem, unsigned addr)
{
	if (addr + 1 >= 0x200000) return 0;
	return (uint16_t)(mem[addr] | (mem[addr + 1] << 8));
}

static void PcatPut16(uint8_t* mem, unsigned addr, uint16_t v)
{
	if (addr + 1 >= 0x200000) return;
	mem[addr] = (uint8_t)(v & 0xff);
	mem[addr + 1] = (uint8_t)(v >> 8);
}

/* Sword of the Samurai ASOUND.DRV (vtable play=0868 tick=0886): CODE.COM
   copies the MZ header+image to one alloc and applies relocs with that
   base, so image-relative relocs land 512 bytes early and INT 7Fh reads
   the reloc table as the vtable (play offset 0008 → keys=0). tf1942 uses
   a different vtable (10A8/10FD) and already plays — do not touch it.
   Retarget CS:0349/0354/0358 to the image and slide those relocs. */
void RepairAsoundPlayEnable(uint8_t* mem)
{
	if (!mem) return;
	const uint16_t comCs = (uint16_t)(mem[0x7F * 4 + 2] | (mem[0x7F * 4 + 3] << 8));
	if (comCs < 0x0100 || comCs >= 0xA000 || comCs == DOS98_TRAMP_SEG)
		return;
	const unsigned com = (unsigned)comCs << 4;
	if (com + 0x35A >= 0x200000) return;
	/* CODE.COM [0349] often lands on the MZ header or on zeroed extra, not
	   the image. Scan for the play prologue at image+0868 (swords-only). */
	uint16_t imgCs = 0, fileCs = 0, hdrParas = 0;
	for (unsigned s = 0x0100; s < 0xA000; s++) {
		const unsigned a = s << 4;
		if (a + 0x874 >= 0x200000) break;
		if (mem[a + 0x868] != 0x55 || mem[a + 0x869] != 0x8B || mem[a + 0x86A] != 0xEC)
			continue;
		if (mem[a + 0x86E] != 0x83 || mem[a + 0x86F] != 0xFB || mem[a + 0x870] != 0x5C)
			continue;
		imgCs = (uint16_t)s;
		if (s >= 0x20) {
			const unsigned mz = (s - 0x20) << 4;
			if (mz + 0x1C < 0x200000 && mem[mz] == 'M' && mem[mz + 1] == 'Z'
				&& PcatMem16(mem, mz + 8) == 0x20) {
				fileCs = (uint16_t)(s - 0x20);
				hdrParas = 0x20;
			}
		}
		break;
	}
	if (!imgCs) {
		const uint16_t cell = PcatMem16(mem, com + 0x349);
		if (cell < 0x0100 || cell >= 0xA000) return;
		imgCs = cell;
	}
	if (!imgCs || imgCs < 0x0100 || imgCs >= 0xA000) return;
	{
		const char* dbg = getenv("CEMU_ASOUND_DBG");
		if (dbg && dbg[0]) {
			FILE* df = NULL;
			if (fopen_s(&df, dbg, "a") == 0 && df) {
				const unsigned ia = (unsigned)imgCs << 4;
				fprintf(df, "scan img=%04X file=%04X [349]=%04X play=%04X tick=%04X dg=%04X\n",
					imgCs, fileCs, PcatMem16(mem, com + 0x349),
					PcatMem16(mem, ia + 0x34), PcatMem16(mem, ia + 0x36),
					PcatMem16(mem, ia + 0x2A));
				fclose(df);
			}
		}
	}
	const unsigned file = (unsigned)fileCs << 4;
	const unsigned img = (unsigned)imgCs << 4;
	if (img + 0x38 >= 0x200000) return;
	if (PcatMem16(mem, img + 0x34) != 0x0868 || PcatMem16(mem, img + 0x36) != 0x0886)
		return;
	/* Image DGROUP word still 014Fh → relocs were applied 512 bytes early. */
	if (fileCs && hdrParas && PcatMem16(mem, img + 0x2A) == 0x014F) {
		const uint16_t nreloc = PcatMem16(mem, file + 6);
		const uint16_t relOff = PcatMem16(mem, file + 0x18);
		const unsigned hdrBytes = (unsigned)hdrParas << 4;
		uint16_t relOffs[32];
		uint16_t nkeep = 0;
		if (nreloc && nreloc <= 32 && relOff + (unsigned)nreloc * 4u <= hdrBytes) {
			for (uint16_t i = 0; i < nreloc && nkeep < 32; i++) {
				const unsigned ent = file + relOff + (unsigned)i * 4u;
				relOffs[nkeep++] = PcatMem16(mem, ent);
			}
			for (uint16_t i = 0; i < nkeep; i++) {
				const uint16_t off = relOffs[i];
				const unsigned wrong = file + off;
				PcatPut16(mem, wrong, (uint16_t)(PcatMem16(mem, wrong) - fileCs));
				const unsigned right = file + hdrBytes + off;
				PcatPut16(mem, right, (uint16_t)(PcatMem16(mem, right) + imgCs));
			}
		}
	}
	PcatPut16(mem, com + 0x349, imgCs);
	PcatPut16(mem, com + 0x354, imgCs);
	PcatPut16(mem, com + 0x358, imgCs);
	uint16_t dgroup = PcatMem16(mem, img + 0x2A);
	if (dgroup < 0x0100 || dgroup >= 0xA000)
		dgroup = (uint16_t)(imgCs + 0x14F);
	for (unsigned o = 0; o + 3 < 0x3000 && img + o + 2 < 0x200000; o++) {
		if (mem[img + o] == 0xB8 && PcatMem16(mem, img + o + 1) == 0x014F)
			PcatPut16(mem, img + o + 1, dgroup);
	}
	const unsigned aa = ((unsigned)dgroup << 4) + 0x19AAu;
	if (aa < 0x200000)
		mem[aa] = 0xFF;
}

/* Pacific Islands ADLIB.BIN: INT 8 far-calls [21A] (tick=0000, CS at [21C])
   but INT 7Fh song select uses ES=[218]. If the loader only filled the tick
   pointer, [218] stays 0, ES writes hit BIOS, and 0x10/0x11 stay the boot
   default (SAMESONG). volfied's BIN does not contain "Pacific". */
static int PacificBinAt(const uint8_t* mem, unsigned a)
{
	if (a + 0x30 >= 0x200000) return 0;
	static const char kType[] = "Type : AdLib";
	static const char kName[] = "Pacific";
	for (int i = 0; kType[i]; i++) {
		if (mem[a + 0x12 + (unsigned)i] != (uint8_t)kType[i])
			return 0;
	}
	for (int i = 0; kName[i]; i++) {
		if (mem[a + 0x28 + (unsigned)i] != (uint8_t)kName[i])
			return 0;
	}
	return 1;
}

static void RepairPacificIslandsSong(uint8_t* mem, uint16_t extSong)
{
	if (!mem) return;
	const uint16_t comCs = (uint16_t)(mem[0x7F * 4 + 2] | (mem[0x7F * 4 + 3] << 8));
	if (comCs < 0x0100 || comCs >= 0xA000 || comCs == DOS98_TRAMP_SEG)
		return;
	const unsigned com = (unsigned)comCs << 4;
	if (com + 0x21E >= 0x200000) return;
	uint16_t drvCs = 0;
	for (unsigned s = 0x0100; s < 0xA000; s++) {
		const unsigned a = s << 4;
		if (PacificBinAt(mem, a)) {
			drvCs = (uint16_t)s;
			break;
		}
	}
	if (!drvCs) {
		drvCs = PcatMem16(mem, com + 0x21C);
		if (drvCs < 0x0100 || drvCs >= 0xA000)
			drvCs = PcatMem16(mem, com + 0x218);
	}
	if (drvCs < 0x0100 || drvCs >= 0xA000) return;
	const unsigned drv = (unsigned)drvCs << 4;
	if (!PacificBinAt(mem, drv) && drv + 0x40 < 0x200000) {
		int hit = 0;
		for (unsigned i = 0; i + 7 < 0x80 && drv + i + 7 < 0x200000; i++) {
			if (mem[drv + i] == 'P' && mem[drv + i + 1] == 'a'
				&& mem[drv + i + 2] == 'c' && mem[drv + i + 3] == 'i'
				&& mem[drv + i + 4] == 'f' && mem[drv + i + 5] == 'i'
				&& mem[drv + i + 6] == 'c') {
				hit = 1;
				break;
			}
		}
		if (!hit) return;
	}
	/* Point both far pointers at the image before INT 7Fh uses [218]. */
	PcatPut16(mem, com + 0x216, 0x0004);
	PcatPut16(mem, com + 0x218, drvCs);
	PcatPut16(mem, com + 0x21A, 0x0000);
	PcatPut16(mem, com + 0x21C, drvCs);
	if (extSong < 0x10 || extSong > 0x20) return;
	const uint8_t idx = (uint8_t)(extSong - 0x0F);
	mem[drv + 8] = idx;
	mem[drv + 9] = 0;
	mem[drv + 0x5E] = 0;
	mem[drv + 0x62] = 0;
	/* 0x41d may ignore [8] and keep table slot 1 (boot default). Point
	   every used slot at the title's blob so 0x10 and 0x11 diverge. */
	if (drv + 0x9A8 < 0x200000 && idx >= 1 && idx <= 3) {
		const uint16_t blob = PcatMem16(mem, drv + 0x9A0 + (unsigned)idx * 2u);
		if (blob) {
			for (unsigned i = 0; i <= 3; i++)
				PcatPut16(mem, drv + 0x9A0 + i * 2u, blob);
		}
	}
	/* 0x41d skips reading [8] while [9]!=0, then sees [5e]==0 and keeps
	   the boot voices (SAMESONG). Always sample [8]. */
	if (drv + 0x425 < 0x200000
		&& mem[drv + 0x423] == 0x75 && mem[drv + 0x424] == 0x06) {
		mem[drv + 0x423] = 0x90;
		mem[drv + 0x424] = 0x90;
	}
	{
		const char* dbg = getenv("CEMU_PACI_DBG");
		if (dbg && dbg[0]) {
			FILE* df = NULL;
			if (fopen_s(&df, dbg, "a") == 0 && df) {
				fprintf(df, "ext=%04X idx=%u drv=%04X [8]=%02X [9]=%02X [5e]=%02X 423=%02X%02X play=%04X\n",
					extSong, (unsigned)idx, drvCs,
					mem[drv + 8], mem[drv + 9], mem[drv + 0x5E],
					mem[drv + 0x423], mem[drv + 0x424],
					PcatMem16(mem, drv + 0x9A0 + (unsigned)idx * 2u));
				fclose(df);
			}
		}
	}
}

/* Resolved once: the hook is on the per-instruction path. */
PcatIpProf* g_pcatIpProf = NULL;

void PcatIpProfInit()
{
	const char* p = getenv("CEMU_PCAT_IPPROF");
	if (!p || !p[0]) return;
	static PcatIpProf inst;
	inst.path = p;
	g_pcatIpProf = &inst;
}

/* HOOT's own INT8 helper: jnb +4 / sub dx,1000h. Azrael/kyrandia already
   point INT8 here and play; WarCraft 2 / Blackthorne leave AIL CS:0000. */
int PcatHootInt8Sig(const uint8_t* mem, unsigned lin)
{
	if (!mem || lin + 6 >= 0x200000) return 0;
	return (mem[lin] == 0x73 && mem[lin + 1] == 0x04
		&& mem[lin + 2] == 0x81 && mem[lin + 3] == 0xEA
		&& mem[lin + 4] == 0x00 && mem[lin + 5] == 0x10) ? 1 : 0;
}

void PcatAilTrace(const char* fmt, ...);

int PcatFindHootInt8(const uint8_t* mem, uint16_t* segOut, uint16_t* offOut)
{
	if (!mem || !segOut || !offOut) return 0;
	auto csHasHootIo = [&](uint16_t seg) -> int {
		if (seg < 0x0100 || seg >= 0xA000) return 0;
		const unsigned lin = (unsigned)seg << 4;
		for (unsigned i = 0; i < 0x4000 && lin + i + 3 < 0x200000; i++) {
			if (mem[lin + i] == 0xBA && mem[lin + i + 1] == 0xE0
				&& mem[lin + i + 2] == 0x07 && mem[lin + i + 3] == 0xEC)
				return 1;
		}
		return 0;
	};
	static const uint16_t kOff[] = { 0x040E, 0x0411, 0x0417, 0x0400, 0 };
	auto trySeg = [&](uint16_t seg) -> int {
		for (int i = 0; kOff[i]; i++) {
			if (!PcatHootInt8Sig(mem, ((unsigned)seg << 4) + kOff[i]))
				continue;
			if (!csHasHootIo(seg))
				continue;
			*segOut = seg;
			*offOut = kOff[i];
			return 1;
		}
		return 0;
	};
	const uint16_t i7f = (uint16_t)(mem[0x7F * 4 + 2] | (mem[0x7F * 4 + 3] << 8));
	if (trySeg(i7f)) return 1;
	/* Blackthorne's HOOT.EXE keeps `in al,07E0h` at CS:~1FD4h. A match
	   below 1200h (1011:040E) is DOS residue and mutes MPU packs. */
	for (unsigned s = 0x1200; s < 0x4000; s++) {
		if ((uint16_t)s == i7f) continue;
		if (trySeg((uint16_t)s)) return 1;
	}
	return 0;
}

/* AIL2 API stub: `mov ax,APIh / jmp`. HOOT CS:040E is not AIL; FarCallAil
   that lands there hangs (Hanse irq0 stays at boot count). */
int PcatSegHasAilApi(const uint8_t* mem, uint16_t seg, uint16_t api)
{
	if (!mem || seg < 0x0100 || seg >= 0xA000) return 0;
	const unsigned base = (unsigned)seg << 4;
	for (unsigned o = 0; o + 4 < 0x4000; o++) {
		const unsigned a = base + o;
		if (a + 4 >= 0x200000) break;
		if (mem[a] == 0xB8 && mem[a + 1] == (uint8_t)(api & 0xff)
			&& mem[a + 2] == (uint8_t)(api >> 8) && mem[a + 3] == 0xE9)
			return 1;
	}
	return 0;
}

int PcatApiTimerAt(const uint8_t* mem, unsigned a)
{
	/* AIL2 API_timer: `inc word [CS:0006]` (Hanse/Omar) or `[CS:000E]`
	   (Lost Vikings) / cld / 6+ register pushes. */
	if (!mem || a + 24 >= 0x200000) return 0;
	unsigned s = 0;
	if (mem[a] == 0xFF && mem[a + 1] == 0x06 && mem[a + 4] == 0xFC
		&& mem[a + 3] == 0x00 && (mem[a + 2] == 0x06 || mem[a + 2] == 0x0E))
		s = 5;
	else
		return 0;
	int pushes = 0;
	for (unsigned i = s; i < s + 16; i++) {
		if (mem[a + i] >= 0x50 && mem[a + i] <= 0x57)
			pushes++;
	}
	return (pushes >= 6) ? 1 : 0;
}

unsigned PcatFindApiTimerOff(const uint8_t* mem, uint16_t seg)
{
	if (!mem || seg < 0x0100 || seg >= 0xA000) return 0;
	const unsigned base = (unsigned)seg << 4;
	for (unsigned o = 0x10; o + 24 < 0x8000; o++) {
		if (o == 0x040E || o == 0x040F) continue;
		if (PcatApiTimerAt(mem, base + o))
			return o;
	}
	return 0;
}

int PcatSegHasApiTimer(const uint8_t* mem, uint16_t seg)
{
	return PcatFindApiTimerOff(mem, seg) != 0;
}

uint16_t PcatFindAilApiCs(const uint8_t* mem, uint16_t skipSeg)
{
	if (!mem) return 0;
	const uint16_t i8Seg = (uint16_t)(mem[0x08 * 4 + 2] | (mem[0x08 * 4 + 3] << 8));
	const uint16_t i8Off = (uint16_t)(mem[0x08 * 4] | (mem[0x08 * 4 + 1] << 8));
	/* Hanse/Omar: AIL stubs live in HOOT CS (INT8 = CS:040E). A low-memory
	   false match at 1200h FarCalls into INT6 and starves IRQ0. */
	PcatAilTrace("findenter i8=%04X:%04X skip=%04X sig=%d\n",
		i8Seg, i8Off, skipSeg,
		PcatHootInt8Sig(mem, ((unsigned)i8Seg << 4) + i8Off));
	if (i8Seg >= 0x1200 && i8Seg < 0xA000 && i8Seg != skipSeg) {
		const unsigned base = (unsigned)i8Seg << 4;
		unsigned hit150 = 0, hit151 = 0;
		for (unsigned o = 0; o + 4 < 0x8000; o++) {
			const unsigned a = base + o;
			if (a + 4 >= 0x200000) break;
			if (mem[a] != 0xB8 || mem[a + 2] != 0 || mem[a + 3] != 0xE9)
				continue;
			if (mem[a + 1] == 150 && !hit150) hit150 = o;
			if (mem[a + 1] == 151 && !hit151) hit151 = o;
		}
		PcatAilTrace("findhits i8=%04X hit150=%04X hit151=%04X off=%04X\n",
			i8Seg, hit150, hit151, i8Off);
		if (hit150 && hit151)
			return i8Seg;
	}
	for (unsigned s = 0x1200; s < 0x2000; s++) {
		if ((uint16_t)s == skipSeg) continue;
		if ((uint16_t)s == 0x1200 && i8Seg != 0x1200)
			continue;
		if (!PcatSegHasApiTimer(mem, (uint16_t)s)) continue;
		if (PcatSegHasAilApi(mem, (uint16_t)s, 150)
			&& PcatSegHasAilApi(mem, (uint16_t)s, 151))
			return (uint16_t)s;
	}
	return 0;
}

/* XMIDI TIMB: count, then {patch,bank}*. Hanse/Lost Vikings GTL files have
   100+ entries; dumping the first 24 never reaches the patches the song uses. */
int PcatXmiTimbList(const uint8_t* d, unsigned n, uint16_t* out, int maxn)
{
	if (!d || n < 16 || !out || maxn <= 0) return 0;
	for (unsigned i = 0; i + 12 < n; i++) {
		if (d[i] != 'F' || d[i + 1] != 'O' || d[i + 2] != 'R' || d[i + 3] != 'M')
			continue;
		if (d[i + 8] != 'X' || d[i + 9] != 'M' || d[i + 10] != 'I' || d[i + 11] != 'D')
			continue;
		const unsigned formLn = ((unsigned)d[i + 4] << 24) | ((unsigned)d[i + 5] << 16)
			| ((unsigned)d[i + 6] << 8) | (unsigned)d[i + 7];
		const unsigned formEnd = i + 8 + formLn;
		if (formEnd > n) continue;
		for (unsigned t = i + 12; t + 10 < formEnd && t + 10 < n; t++) {
			if (d[t] != 'T' || d[t + 1] != 'I' || d[t + 2] != 'M' || d[t + 3] != 'B')
				continue;
			const unsigned ln = ((unsigned)d[t + 4] << 24) | ((unsigned)d[t + 5] << 16)
				| ((unsigned)d[t + 6] << 8) | (unsigned)d[t + 7];
			if (ln < 2 || t + 8 + ln > n) continue;
			const unsigned cnt = (unsigned)d[t + 8] | ((unsigned)d[t + 9] << 8);
			if (cnt == 0 || cnt > 256 || 2u + cnt * 2u > ln) continue;
			int k = 0;
			for (unsigned j = 0; j < cnt && k < maxn; j++) {
				const unsigned pe = d[t + 10 + j * 2];
				const unsigned be = d[t + 11 + j * 2];
				out[k++] = (uint16_t)((be << 8) | pe);
			}
			return k;
		}
	}
	return 0;
}

/* Hanse XMI EVNT starts with FF 01 08 "*MERGED*" / "*UNDO*". AIL's XMIDI
   parser does not skip generic MIDI text metas, so the rest of the track
   never yields note-ons (OPL writes from 156, keys=0). Zero the tag; XMIDI
   delay-0 bytes are harmless. Omar/LV have no such tag. */
void PcatXmiWipeSeqTextMeta(uint8_t* d, unsigned n)
{
	static const char* tags[] = { "*MERGED*", "*UNDO*", NULL };
	if (!d || n < 16) return;
	for (int t = 0; tags[t]; t++) {
		const unsigned L = (unsigned)strlen(tags[t]);
		if (n < 3 + L) continue;
		for (unsigned p = 0; p + 3 + L <= n; p++) {
			if (d[p] == 0xFF && d[p + 1] >= 1 && d[p + 1] <= 7
				&& d[p + 2] == (uint8_t)L
				&& memcmp(d + p + 3, tags[t], L) == 0)
				memset(d + p, 0, 3 + L);
		}
	}
}

void PcatAilTrace(const char* fmt, ...)
{
	const char* p = getenv("CEMU_PCAT_AIL");
	if (!p || !p[0]) return;
	FILE* f = fopen(p, "a");
	if (!f) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fclose(f);
}

} /* namespace */

static int s_pcatHootNullGtl = 0;

#define PCAT_CENSUS(phase) do { \
	PcatCensus c__; \
	c__.funcVect = funcVect_; c__.oplWr = oplWriteCount_; \
	c__.oplKey = oplKeyOnCount_; c__.saaWr = saaWriteCount_; \
	c__.saaTone = saaToneOnCount_; c__.spkTone = speakerToneCount_; \
	c__.midi = midiCount_; c__.irq0 = irq0Count_; \
	c__.pitReload = pit0Reload_; c__.pitHz = (unsigned)pitClockHz_; \
	PcatIvtCensus(np2_mem(), c__, (phase), dosGe_ ? dosGe_->name : NULL); \
} while (0)

enum {
	PCAT_CPU_HZ = 8000000,
	PCAT_OPL_HZ = 3579545,
	PCAT_SAA_HZ = 7159090,
	PCAT_PIT_HZ = 1193182,
	PCAT_TIMER_VEC = 0x08,
	/* Idle HLT must sit past the 0060:0000–01FF trampoline traps
	   (TrapVector = IP/2). Parking on INT 0's HLT;IRET pops the DOS
	   stack and CODE.COM resumes in zeroed RAM (IP 12C0). */
	PCAT_IDLE_IP = 0x0250,
	PIC_SLAVE_CMD = 0xA0,
	PIC_SLAVE_MASK = 0xA1,
	KBC_DATA = 0x60,
	KBC_STAT = 0x64,
	CMOS_ADDR = 0x70,
	CMOS_DATA = 0x71,
	/* AdLib / SB OPL */
	ADLIB_ADDR = 0x388,
	ADLIB_DATA = 0x389,
	/* CMS / Game Blaster (MAME gblaster.cpp) */
	CMS_DATA0 = 0x220,
	CMS_ADDR0 = 0x221,
	CMS_DATA1 = 0x222,
	CMS_ADDR1 = 0x223,
	/* IBM PIT / PIC / PPI */
	PIT_CT0 = 0x40,
	PIT_CT2 = 0x42,
	PIT_CTRL = 0x43,
	PIC_CMD = 0x20,
	PIC_MASK = 0x21,
	PORT_61 = 0x61,
	/* MPU-401 UART (MAME isa mpu401 default) */
	MPU_DATA = 0x330,
	MPU_STAT = 0x331,
	/* hoot EXT (same as PC-98) */
	EXT_CMD = 0x07E0,
	EXT_SONG = 0x07E2,
	EXT_PARAM = 0x07E4,
	EXT_STATE = 0x07E8
};

static CHardPcat* g_pcatActive = NULL;
static int g_pcatEoi = 0;

/* IBM AT CMOS / slave 8259 / 8042. File-static so CHardPcat stays unchanged
   (any new member forces a full k2 rebuild of every object that includes the
   header). */
static uint8_t s_cmosIdx;
static uint8_t s_cmos[128];
static uint8_t s_picSlaveMask;
static uint8_t s_picSlaveIcw;
static uint8_t s_picSlaveIcw1;

static void PcatCmosInit()
{
	memset(s_cmos, 0, sizeof(s_cmos));
	s_cmosIdx = 0;
	s_picSlaveMask = 0xFF;
	s_picSlaveIcw = 0;
	s_picSlaveIcw1 = 0;
	s_cmos[0x0A] = 0x26; /* 32.768 kHz, rate 6 */
	s_cmos[0x0B] = 0x02; /* 24-hour */
	s_cmos[0x0D] = 0x80; /* battery good */
	s_cmos[0x10] = 0x40; /* 1.44M floppy */
	s_cmos[0x14] = 0x21; /* match BDA equipment */
	s_cmos[0x15] = 0x80; /* base 640K */
	s_cmos[0x16] = 0x02;
	s_cmos[0x32] = 0x19; /* century BCD */
	s_cmos[0x07] = 0x24;
	s_cmos[0x08] = 0x12;
	s_cmos[0x09] = 0x96;
	uint16_t sum = 0;
	for (unsigned i = 0x10; i <= 0x2D; i++)
		sum = (uint16_t)(sum + s_cmos[i]);
	s_cmos[0x2E] = (uint8_t)(sum >> 8);
	s_cmos[0x2F] = (uint8_t)sum;
}

static void PlantAtBiosRom(uint8_t* mem)
{
	if (!mem) return;
	/* INT 15h AH=C0 table at F000:E000 — model FC / submodel 01 (5170).
	   Feature 0x74 = 2nd 8259 + RTC + wait; no EBDA bit. */
	mem[0xFE000] = 8;
	mem[0xFE001] = 0;
	mem[0xFE002] = 0xFC;
	mem[0xFE003] = 0x01;
	mem[0xFE004] = 0x00;
	mem[0xFE005] = 0x74;
	mem[0xFE006] = 0;
	mem[0xFE007] = 0;
	mem[0xFE008] = 0;
	mem[0xFE009] = 0;
	/* Dummy POST so a far jump through the reset vector does not fetch 00. */
	mem[0xFE05B] = 0xFB; /* STI */
	mem[0xFE05C] = 0xF4; /* HLT */
	mem[0xFE05D] = 0xEB;
	mem[0xFE05E] = 0xFD;
	mem[0xFFFF0] = 0xEA; /* JMP F000:E05B */
	mem[0xFFFF1] = 0x5B;
	mem[0xFFFF2] = 0xE0;
	mem[0xFFFF3] = 0x00;
	mem[0xFFFF4] = 0xF0;
	memcpy(mem + 0xFFFF5, "01/01/88", 8);
	mem[0xFFFFE] = 0xFC; /* IBM AT */
	mem[0xFFFFF] = 0x00;
}

static void PcatOut8(unsigned port, unsigned char val)
{
	CHardPcat* hw = g_pcatActive;
	if (hw) hw->PortOut((uint16_t)port, (uint8_t)val);
}

static unsigned char PcatIn8(unsigned port)
{
	CHardPcat* hw = g_pcatActive;
	return hw ? hw->PortIn((uint16_t)port) : 0x00;
}

static int CEmuParseOptHex(const CEmuGameEntry* ge, const char* name, int defVal)
{
	if (!ge || !name) return defVal;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, name) != 0) continue;
		const char* v = ge->opt[i].value;
		if (!v || !v[0]) return defVal;
		return (int)strtol(v, NULL, 0);
	}
	return defVal;
}

static void DosStripHash(const char* in, char* out, int outCap)
{
	if (!out || outCap <= 0) return;
	out[0] = 0;
	if (!in) return;
	const char* hash = strchr(in, '#');
	int n = hash ? (int)(hash - in) : (int)strlen(in);
	if (n >= outCap) n = outCap - 1;
	memcpy(out, in, (size_t)n);
	out[n] = 0;
	while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t'))
		out[--n] = 0;
}

static void DosSplitCmd(const char* cmdline, char* name, int nameCap, char* tail, int tailCap)
{
	if (name && nameCap > 0) name[0] = 0;
	if (tail && tailCap > 0) tail[0] = 0;
	if (!cmdline || !name || nameCap <= 0) return;
	while (*cmdline == ' ' || *cmdline == '\t') cmdline++;
	const char* sp = cmdline;
	while (*sp && *sp != ' ' && *sp != '\t') sp++;
	int n = (int)(sp - cmdline);
	if (n >= nameCap) n = nameCap - 1;
	memcpy(name, cmdline, (size_t)n);
	name[n] = 0;
	while (*sp == ' ' || *sp == '\t') sp++;
	if (tail && tailCap > 0)
		strncpy_s(tail, (size_t)tailCap, sp, _TRUNCATE);
}

static int DosIsEngineName(const char* name)
{
	if (!name || !name[0]) return 0;
	const char* ext = strrchr(name, '.');
	if (!ext) return 0;
	return _stricmp(ext, ".EXE") == 0
		|| _stricmp(ext, ".COM") == 0
		|| _stricmp(ext, ".DRV") == 0
		|| _stricmp(ext, ".SYS") == 0;
}

CHardPcat::CHardPcat()
	: cpuHz_(PCAT_CPU_HZ)
	, bootClockMul_(1)
	, oplHz_(PCAT_OPL_HZ)
	, funcVect_(0x7e)
	, cpuCycles_(0)
	, oplWriteCount_(0)
	, oplKeyOnCount_(0)
	, saaWriteCount_(0)
	, saaToneOnCount_(0)
	, speakerToneCount_(0)
	, midiCount_(0)
	, irq0Count_(0)
	, dosStubReady_(0)
	, stubState_(0)
	, extCmd_(0)
	, extSong_(0)
	, extParam_(0)
	, modeCms_(0)
	, modeBeep_(0)
	, modeSb_(0)
	, modeSilp_(0)
	, modePs1_(0)
	, modeMidi_(0)
	, chip_(NULL)
	, saa1_(NULL)
	, saa2_(NULL)
	, sn764_(NULL)
	, sampleRate_(44100)
	, active_(0)
	, dosGe_(NULL)
	, pitClockHz_(PCAT_PIT_HZ)
	, pit0Reload_(0)
	, pit0Counter_(0)
	, pit0Residual_(0)
	, pit0IrqPending_(0)
	, pit0WriteHi_(0)
	, pit0ReadHi_(0)
	, pit0Running_(0)
	, pit2Reload_(0)
	, pit2Counter_(0)
	, pit2Residual_(0)
	, pit2WriteHi_(0)
	, pit2ReadHi_(0)
	, pit2Out_(0)
	, pit2Running_(0)
	, pitCtrlLatch_(0)
	, port61_(0)
	, spkPhase_(0)
	, spkPhaseInc_(0)
	, picMask_(0xff)
	, pic0Isr_(0)
	, picMasterIcw_(0)
	, picMasterIcw1_(0)
	, oplPumpResidual_(0)
	, mpuUart_(0)
	, mpuRx_(0)
	, mpuRxFull_(0)
	, mpuAckR_(0)
	, mpuAckW_(0)
	, midiBytes_(NULL)
	, midiDelta_(NULL)
	, midiLastCycle_(0)
	, silpDrvSeg_(0)
	, silpSongSeg_(0)
	, silpSongBytes_(0)
	, silpScanDone_(0)
	, mokDrvSeg_(0)
	, hootTimerFixed_(0)
	, hootAilCs_(0)
	, sbDspResetting_(0)
	, sbDspReadData_(0xAA)
	, sbDspReadAvail_(0)
	, sbDspQueueR_(0)
	, sbDspQueueW_(0)
{
	hardKind = KIND_PCAT;
	dosSong_[0] = 0;
	hootAdvName_[0] = 0;
	hootAdvSeg_ = 0;
	hootAdvSize_ = 0;
	hootAdvQuantumOff_ = 0;
	hootAdvIoOff_ = 0;
	memset(mpuAckQ_, 0, sizeof(mpuAckQ_));
	memset(sbDspQueue_, 0, sizeof(sbDspQueue_));
	sbMixerIdx_ = 0;
	SbMixerReset();
	memset(saaSel_, 0, sizeof(saaSel_));
	memset(saaAmp_, 0, sizeof(saaAmp_));
	memset(saaFreq_, 0, sizeof(saaFreq_));
	memset(saaOct_, 0, sizeof(saaOct_));
	memset(saaEn_, 0, sizeof(saaEn_));
}

CHardPcat::~CHardPcat()
{
	Shutdown();
}

int CHardPcat::Init(const CEmuGameEntry* ge, int sampleRate)
{
	if (!ge) return 0;
	static int profInit = 0;
	if (!profInit) { profInit = 1; PcatIpProfInit(); }
	sampleRate_ = sampleRate > 0 ? sampleRate : 44100;
	cpuHz_ = PCAT_CPU_HZ;
	/* Catalog clockmul (often 5–8) is for DOS boot only — applying it to
	   realtime Render multiplies host CPU by the same factor. */
	bootClockMul_ = CEmuParseOptHex(ge, "clockmul", 0);
	if (bootClockMul_ <= 0) bootClockMul_ = CEmuParseOptHex(ge, "clock_mul", 1);
	if (bootClockMul_ < 1) bootClockMul_ = 1;
	if (bootClockMul_ > 16) bootClockMul_ = 16;
	oplHz_ = PCAT_OPL_HZ;
	funcVect_ = CEmuParseOptHex(ge, "funcvect", 0x7e) & 0xff;

	modeCms_ = (_stricmp(ge->subtype, "gameblaster") == 0
		|| _stricmp(ge->subtype, "cms") == 0) ? 1 : 0;
	modeMidi_ = 0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "midiout") == 0) { modeMidi_ = 1; break; }
	}
	if (_stricmp(ge->subtype, "midiout") == 0) modeMidi_ = 1;
	modePs1_ = (_stricmp(ge->subtype, "ps1") == 0) ? 1 : 0;
	modeBeep_ = ((_stricmp(ge->subtype, "beep") == 0
		|| _stricmp(ge->subtype, "tandy") == 0
		|| modePs1_) && !modeMidi_) ? 1 : 0;
	/* An AdLib card has no mixer, and answering one lets a Miles driver
	   pick the Sound Blaster output path on a machine that is not one. */
	modeSb_ = (_strnicmp(ge->subtype, "soundblaster", 12) == 0) ? 1 : 0;

	/* Always keep OPL — AdLib/SB and MIDI soft fallback. CMS adds SAA. */
	chip_ = CEmuChipYm3812Create((uint32_t)oplHz_, sampleRate_);
	if (!chip_) return 0;
	if (modeCms_) {
		saa1_ = CEmuChipSaa1099Create(PCAT_SAA_HZ, sampleRate_);
		saa2_ = CEmuChipSaa1099Create(PCAT_SAA_HZ, sampleRate_);
		if (!saa1_ || !saa2_) {
			CEmuChipYm3812Destroy(chip_); chip_ = NULL;
			if (saa1_) { CEmuChipSaa1099Destroy(saa1_); saa1_ = NULL; }
			if (saa2_) { CEmuChipSaa1099Destroy(saa2_); saa2_ = NULL; }
			return 0;
		}
	}
	if (_stricmp(ge->subtype, "tandy") == 0 || modePs1_) {
		sn764_ = CEmuChipSn76489Create((uint32_t)PCAT_OPL_HZ, sampleRate_);
		if (!sn764_) {
			CEmuChipYm3812Destroy(chip_); chip_ = NULL;
			return 0;
		}
	}
	MidiCaptureReset();
	np2_init();
	active_ = 1;
	AttachIoHooks();
	return 1;
}

void CHardPcat::Shutdown()
{
	PCAT_CENSUS("end");
	DetachIoHooks();
	if (chip_) { CEmuChipYm3812Destroy(chip_); chip_ = NULL; }
	if (saa1_) { CEmuChipSaa1099Destroy(saa1_); saa1_ = NULL; }
	if (saa2_) { CEmuChipSaa1099Destroy(saa2_); saa2_ = NULL; }
	if (sn764_) { CEmuChipSn76489Destroy(sn764_); sn764_ = NULL; }
	delete[] midiBytes_; midiBytes_ = NULL;
	delete[] midiDelta_; midiDelta_ = NULL;
	midiCount_ = 0;
	active_ = 0;
	if (g_pcatActive == this) g_pcatActive = NULL;
}

uint8_t* CHardPcat::Mem()
{
	return np2_mem();
}

void CHardPcat::AttachIoHooks()
{
	g_pcatActive = this;
	hootrip_out8 = PcatOut8;
	hootrip_inp8 = PcatIn8;
}

void CHardPcat::DetachIoHooks()
{
	if (g_pcatActive == this) {
		hootrip_out8 = NULL;
		hootrip_inp8 = NULL;
		g_pcatActive = NULL;
	}
}

void CEmuHardPcatSetActive(CHardPcat* hw)
{
	if (!hw) {
		if (g_pcatActive) {
			hootrip_out8 = NULL;
			hootrip_inp8 = NULL;
			g_pcatActive = NULL;
		}
		return;
	}
	g_pcatActive = hw;
	hootrip_out8 = PcatOut8;
	hootrip_inp8 = PcatIn8;
}

int CHardPcat::IvtHooked(uint8_t vec) const
{
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	const unsigned off = (unsigned)mem[vec * 4] | ((unsigned)mem[vec * 4 + 1] << 8);
	const unsigned seg = (unsigned)mem[vec * 4 + 2] | ((unsigned)mem[vec * 4 + 3] << 8);
	if (seg == 0 && off == 0) return 0;
	/* Trampoline segment = still default HLT/IRET. */
	if (seg == DOS98_TRAMP_SEG) return 0;
	return 1;
}

/* DOS trampoline HLT;IRET stubs. INT 08's BIOS INT 1C chain is delivered
   from DeliverIrqs — SOUND.COM often stops chaining the previous vector. */
static int PcatTrampolineHlt(CEmuDos98* dos, uint8_t* mem,
	uint16_t cs, uint16_t ip, CEmuDos98Result* outRes)
{
	uint8_t vec = 0;
	if (!dos->TrapVector(cs, ip, &vec))
		return 0;
	const CEmuDos98Result res = dos->ServiceInt(mem, vec);
	if (outRes)
		*outRes = res;
	if (res == DOS98_TERMINATED || res == DOS98_RESIDENT || res == DOS98_EXEC)
		return 1;
	dos->IretReturn(mem);
	return 1;
}

void CHardPcat::PitOut(uint16_t port, uint8_t data)
{
	if (port == PIT_CTRL) {
		pitCtrlLatch_ = data;
		const int ch = (data >> 6) & 3;
		if (ch == 0) { pit0WriteHi_ = 0; pit0ReadHi_ = 0; }
		else if (ch == 2) { pit2WriteHi_ = 0; pit2ReadHi_ = 0; }
		return;
	}
	if (port == PIT_CT0) {
		if (!pit0WriteHi_) {
			pit0Reload_ = (pit0Reload_ & 0xff00) | data;
			pit0WriteHi_ = 1;
		} else {
			pit0Reload_ = (pit0Reload_ & 0x00ff) | ((uint16_t)data << 8);
			pit0WriteHi_ = 0;
			pit0Counter_ = pit0Reload_ ? pit0Reload_ : 65536u;
			pit0Running_ = 1;
			pit0IrqPending_ = 0;
		}
		return;
	}
	if (port == PIT_CT2) {
		if (!pit2WriteHi_) {
			pit2Reload_ = (pit2Reload_ & 0xff00) | data;
			pit2WriteHi_ = 1;
		} else {
			pit2Reload_ = (pit2Reload_ & 0x00ff) | ((uint16_t)data << 8);
			pit2WriteHi_ = 0;
			pit2Counter_ = pit2Reload_ ? pit2Reload_ : 65536u;
			pit2Running_ = 1;
			/* Mode 3 square: phase increment per host sample. */
			if (pit2Reload_ > 0 && sampleRate_ > 0) {
				const double hz = (double)pitClockHz_ / (double)pit2Reload_;
				spkPhaseInc_ = (uint64_t)(hz * 4294967296.0 / (double)sampleRate_);
				if (spkPhaseInc_ == 0) spkPhaseInc_ = 1;
				speakerToneCount_++;
				if (modeBeep_) {
					const int mid = FmMonShadowHzToMidi(hz);
					const int on = ((port61_ & 0x03) == 0x03 && mid >= 0) ? 1 : 0;
					FmMonShadowWriteAuxReg(0x00, (unsigned)(pit2Reload_ & 0xff));
					FmMonShadowWriteAuxReg(0x01, (unsigned)(pit2Reload_ >> 8));
					FmMonShadowWriteAuxReg(0x02, (unsigned)port61_);
					FmMonShadowWriteAuxReg(0x03, on ? 1u : 0u);
					if (mid >= 0)
						FmMonShadowWriteAuxReg(0x04, (unsigned)mid);
					FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MIDI);
					FmMonShadowMidiNote(0, on ? mid : 0, on);
				}
			}
		}
	}
}

uint8_t CHardPcat::PitIn(uint16_t port)
{
	if (port == PIT_CT0) {
		uint16_t v = (uint16_t)(pit0Counter_ ? pit0Counter_ : pit0Reload_);
		if (!pit0ReadHi_) { pit0ReadHi_ = 1; return (uint8_t)(v & 0xff); }
		pit0ReadHi_ = 0;
		return (uint8_t)(v >> 8);
	}
	if (port == PIT_CT2) {
		/* ADLIBM.COM (Infogrames) calibrates delays by latching PIT2 in a
		   CLI spin. Each ISA IN burns time; kick the counter so the 0x0200
		   countdown's high byte can reach 0 instead of timing out at CX=FFFF. */
		if (pit2Running_ && (port61_ & 0x01) && cpuHz_ > 0 && pitClockHz_ > 0) {
			uint64_t one = ((uint64_t)cpuHz_ + (uint64_t)pitClockHz_ - 1ull)
				/ (uint64_t)pitClockHz_;
			if (one < 16ull) one = 16ull;
			PitTick(one);
		}
		uint16_t v = (uint16_t)(pit2Counter_ ? pit2Counter_ : pit2Reload_);
		if (!pit2ReadHi_) { pit2ReadHi_ = 1; return (uint8_t)(v & 0xff); }
		pit2ReadHi_ = 0;
		return (uint8_t)(v >> 8);
	}
	return 0xff;
}

void CHardPcat::PitTick(uint64_t cpuCycles)
{
	if (cpuHz_ <= 0) return;
	if (pit0Running_) {
		pit0Residual_ += cpuCycles * (uint64_t)pitClockHz_;
		uint64_t ticks = pit0Residual_ / (uint64_t)cpuHz_;
		pit0Residual_ %= (uint64_t)cpuHz_;
		while (ticks > 0) {
			uint32_t step = pit0Counter_ ? pit0Counter_ : 65536u;
			if (ticks < step) {
				pit0Counter_ = step - (uint32_t)ticks;
				ticks = 0;
			} else {
				ticks -= step;
				pit0Counter_ = pit0Reload_ ? pit0Reload_ : 65536u;
				pit0IrqPending_ = 1;
			}
		}
	}
	if (pit2Running_ && (port61_ & 0x01)) {
		pit2Residual_ += cpuCycles * (uint64_t)pitClockHz_;
		uint64_t ticks = pit2Residual_ / (uint64_t)cpuHz_;
		pit2Residual_ %= (uint64_t)cpuHz_;
		while (ticks > 0) {
			uint32_t step = pit2Counter_ ? pit2Counter_ : 65536u;
			if (ticks < step) {
				pit2Counter_ = step - (uint32_t)ticks;
				ticks = 0;
			} else {
				ticks -= step;
				pit2Counter_ = pit2Reload_ ? pit2Reload_ : 65536u;
				pit2Out_ ^= 1;
			}
		}
	}
}

void CHardPcat::TickSide(uint64_t cpuCycles)
{
	PitTick(cpuCycles);
}

void CHardPcat::RepairSilpDriverFar()
{
	if (!modeSilp_) return;
	/* silp_at.com keeps a far ptr at CS:026B (off) / CS:026D (seg) to the
	   loaded *.DRV. Sierra drivers store their OPL base (0x220/0x388) at
	   DS:026D when DS still points at silp — clobbering the far segment and
	   sending later calls to 0220:0000 (silence, no key-on).
	   CS:0275 (song buffer seg) is similarly overwritten during BP=2 init,
	   which runs *before* INT 7Fh is hooked — snapshot via PSP early.
	   Driver far-ptr restore stays INT-7Fh-only (avoids sex_at false scans). */
	uint8_t* mem = np2_mem();
	if (!mem) return;

	uint16_t silpCs = 0;
	if (IvtHooked(0x7F))
		silpCs = (uint16_t)(mem[0x7F * 4 + 2] | (mem[0x7F * 4 + 3] << 8));
	else if (IvtHooked(PCAT_TIMER_VEC))
		silpCs = (uint16_t)(mem[PCAT_TIMER_VEC * 4 + 2] | (mem[PCAT_TIMER_VEC * 4 + 3] << 8));
	else
		silpCs = dos_.PspSeg();
	if (silpCs < 0x0100) return;
	const unsigned base = (unsigned)silpCs << 4;
	if (base + 0x278 >= 0x200000) return;

	/* Hot path: only heal dig ptr. Full song/DRV repair is rare. */
	if (IvtHooked(0x7F)) {
		const uint16_t p284 = (uint16_t)(mem[base + 0x284] | (mem[base + 0x285] << 8));
		if (p284 != 0x0273) {
			mem[base + 0x284] = 0x73;
			mem[base + 0x285] = 0x02;
		}
		if (silpDrvSeg_) {
			const uint16_t seg = (uint16_t)(mem[base + 0x26D] | (mem[base + 0x26E] << 8));
			if (seg == silpDrvSeg_) {
				if (silpSongSeg_) {
					const uint16_t songSeg = (uint16_t)(mem[base + 0x275] | (mem[base + 0x276] << 8));
					if (songSeg == silpSongSeg_)
						return;
				} else
					return;
			}
		}
	}

	auto looksDrv = [&](uint16_t s) -> int {
		if (s < 0x1000 || s >= 0xA000) return 0;
		const unsigned a = (unsigned)s << 4;
		/* Sierra DRV: near jmp + magic / "xxxdrv" name field. */
		if (mem[a] == 0xE9 && mem[a + 4] == 0x21) return 1;
		if (mem[a + 8] == 'd' && mem[a + 9] == 'r' && mem[a + 10] == 'v') return 1;
		return 0;
	};
	auto looksSongBuf = [&](uint16_t s) -> int {
		/* Song alloc is third (after DRV / PATCH). Sorcerian lands near 0x113C.
		   Prefer SCI magic; also accept empty/non-DRV/non-PATCH paras. */
		if (s < 0x1000 || s >= 0xA000 || looksDrv(s)) return 0;
		const unsigned a = (unsigned)s << 4;
		if (a + 2 >= 0x200000) return 0;
		if (mem[a] == 0x84 && mem[a + 1] == 0x00) return 1;
		if (mem[a] == 0x89) return 0; /* PATCH.00x */
		return 1;
	};

	uint16_t songSeg = (uint16_t)(mem[base + 0x275] | (mem[base + 0x276] << 8));
	if (looksSongBuf(songSeg))
		silpSongSeg_ = songSeg;
	else if (silpSongSeg_ && looksSongBuf(silpSongSeg_)) {
		mem[base + 0x275] = (uint8_t)(silpSongSeg_ & 0xff);
		mem[base + 0x276] = (uint8_t)(silpSongSeg_ >> 8);
	}

	/* Far-ptr restore only once INT 7Fh identifies this as Sierra silp. */
	if (!IvtHooked(0x7F)) return;

	uint16_t seg = (uint16_t)(mem[base + 0x26D] | (mem[base + 0x26E] << 8));
	if (looksDrv(seg)) {
		silpDrvSeg_ = seg;
		return;
	}
	if (silpDrvSeg_ && looksDrv(silpDrvSeg_)) {
		mem[base + 0x26B] = 0;
		mem[base + 0x26C] = 0;
		mem[base + 0x26D] = (uint8_t)(silpDrvSeg_ & 0xff);
		mem[base + 0x26E] = (uint8_t)(silpDrvSeg_ >> 8);
		return;
	}
	if (silpScanDone_) return;
	for (uint16_t s = 0x1000; s < 0x9000; s++) {
		if (!looksDrv(s)) continue;
		silpDrvSeg_ = s;
		silpScanDone_ = 1;
		mem[base + 0x26B] = 0;
		mem[base + 0x26C] = 0;
		mem[base + 0x26D] = (uint8_t)(s & 0xff);
		mem[base + 0x26E] = (uint8_t)(s >> 8);
		return;
	}
	silpScanDone_ = 1;
}

void CHardPcat::SbMixerReset()
{
	memset(sbMixer_, 0, sizeof(sbMixer_));
	sbMixer_[0x04] = 0xEE; /* voice   L/R */
	sbMixer_[0x0A] = 0x06; /* mic */
	sbMixer_[0x22] = 0xEE; /* master  L/R */
	sbMixer_[0x26] = 0xEE; /* FM      L/R */
	sbMixer_[0x28] = 0x00; /* CD */
	sbMixer_[0x2E] = 0x00; /* line */
}

void CHardPcat::SbDspPush(uint8_t v)
{
	sbDspQueue_[sbDspQueueW_ & 3] = v;
	sbDspQueueW_++;
	sbDspReadAvail_ = 1;
}

void CHardPcat::CmsTrackSaa(int chip, uint8_t data)
{
	if (!modeCms_ || chip < 0 || chip > 1) return;
	const int reg = saaSel_[chip] & 0x1f;
	FmMonShadowWriteAuxReg((unsigned)(chip * 0x20 + reg), data);
	if (reg <= 0x05) {
		saaAmp_[chip][reg] = data;
	} else if (reg >= 0x08 && reg <= 0x0d) {
		saaFreq_[chip][reg - 0x08] = data;
	} else if (reg >= 0x10 && reg <= 0x12) {
		const int base = (reg - 0x10) << 1;
		saaOct_[chip][base] = (uint8_t)(data & 7);
		saaOct_[chip][base + 1] = (uint8_t)((data >> 4) & 7);
	} else if (reg == 0x14) {
		saaEn_[chip] = data;
	} else {
		return;
	}
	FmMonShadowSetKeysProfile(SASAMI_FMMON_KEYS_MIDI);
	for (int ch = 0; ch < 6; ch++) {
		const int en = (saaEn_[chip] >> ch) & 1;
		const int amp = (saaAmp_[chip][ch] & 0x0f) | ((saaAmp_[chip][ch] >> 4) & 0x0f);
		const int on = (en && amp > 0) ? 1 : 0;
		/* Match chip Freq(): clock / (2 * ((511-f) << (8-oct))). */
		const int freq = saaFreq_[chip][ch];
		const int oct = saaOct_[chip][ch] & 7;
		const unsigned period = (unsigned)((511 - freq) > 0 ? (511 - freq) : 1) << (8 - oct);
		const double hz = (period > 0)
			? ((double)PCAT_SAA_HZ / (2.0 * (double)period)) : 0.0;
		const int mid = on ? FmMonShadowHzToMidi(hz) : -1;
		FmMonShadowMidiNote(chip * 6 + ch, (mid >= 0) ? mid : 0, mid >= 0);
	}
}

/* AIL OPL ADVs keep an XMIDI "quantum" word in CS BSS. Timer serve skips
   voice updates while it stays 0. ADLIB.ADV uses [232D]; SBP2FM.ADV (larger
   code) shifts the same slot to [295B]. Discover via clear+inc pair. */
static uint16_t FindHootAdvQuantumOff(const uint8_t* data, unsigned size)
{
	if (!data || size < 16) return 0;
	uint16_t bestLow = 0;
	for (unsigned off = 0; off + 7 <= size; off++) {
		if (data[off] != 0x2E || data[off + 1] != 0xC7 || data[off + 2] != 0x06)
			continue;
		if (data[off + 5] != 0 || data[off + 6] != 0) continue;
		const uint16_t t = (uint16_t)(data[off + 3] | (data[off + 4] << 8));
		/* OPL ADVs keep the pair in 1800h–3800h (ADLIB 232Dh, SBP2FM 295Bh).
		   MIDI.ADV is a 7KB MPU driver whose pair sits at 04BFh/04C3h — the
		   OPL window skipped it, the 232Dh fallback wrote past the image. */
		if (t > 0x3800) continue;
		if (t < 0x0400) continue;
		if (t < 0x1800 && size > 0x3000) continue;
		int hasInc = 0;
		for (unsigned o2 = 0; o2 + 5 <= size; o2++) {
			if (data[o2] == 0x2E && data[o2 + 1] == 0xFF && data[o2 + 2] == 0x06
				&& data[o2 + 3] == (uint8_t)(t & 0xff)
				&& data[o2 + 4] == (uint8_t)(t >> 8)) {
				hasInc = 1;
				break;
			}
		}
		if (!hasInc) continue;
		/* Paired counters (ADLIB 232D/2331) — feed the low one. MIDI.ADV
		   serve tests 04C3h; poking that word desyncs native serve. */
		if (!bestLow || t < bestLow)
			bestLow = t;
	}
	return bestLow;
}

/* Runtime OPL base port lives in ADV CS (mov dx,cs:[imm]). Detect/init should
   fill it from default_IO; when detect fails it stays 0 and every OUT DX hits
   port 0 — silent SBP2FM with oplW≈detect-only. */
static uint16_t FindHootAdvIoOff(const uint8_t* data, unsigned size)
{
	if (!data || size < 16) return 0;
	unsigned bestOff = 0, bestN = 0;
	for (unsigned off = 0; off + 5 <= size; off++) {
		if (data[off] != 0x2E || data[off + 1] != 0x8B || data[off + 2] != 0x16)
			continue;
		const uint16_t t = (uint16_t)(data[off + 3] | (data[off + 4] << 8));
		if (t < 0x100 || t >= size) continue;
		unsigned n = 0;
		for (unsigned o2 = 0; o2 + 5 <= size; o2++) {
			if (data[o2] == 0x2E && data[o2 + 1] == 0x8B && data[o2 + 2] == 0x16
				&& data[o2 + 3] == (uint8_t)(t & 0xff)
				&& data[o2 + 4] == (uint8_t)(t >> 8))
				n++;
		}
		if (n > bestN) {
			bestN = n;
			bestOff = t;
		}
	}
	return (bestN >= 2) ? (uint16_t)bestOff : (uint16_t)0;
}

static uint16_t FindHootAdvDefaultIo(const uint8_t* data, unsigned size)
{
	/* describe_driver default_IO sits after the "OPL\0" / near "Ad Lib" label. */
	if (!data || size < 12) return 0;
	for (unsigned i = 0; i + 12 < size; i++) {
		if (data[i] == 'O' && data[i + 1] == 'P' && data[i + 2] == 'L' && data[i + 3] == 0) {
			const uint16_t v = (uint16_t)(data[i + 8] | (data[i + 9] << 8));
			if (v == 0x220 || v == 0x240 || v == 0x388) return v;
		}
		if (data[i] == 'A' && data[i + 1] == 'd' && data[i + 2] == ' '
			&& data[i + 3] == 'L' && data[i + 4] == 'i' && data[i + 5] == 'b') {
			/* ADLIB.ADV: default_IO dword-aligned before the name table. */
			if (i >= 4) {
				const uint16_t v = (uint16_t)(data[i - 4] | (data[i - 3] << 8));
				if (v == 0x220 || v == 0x240 || v == 0x388) return v;
			}
		}
	}
	return 0;
}

void CHardPcat::PrepHootAilState()
{
	/* Observe HOOT DS after INT 7Fh is live — do not patch guest binaries.
	   Record ADV name / quantum / IO offsets for IRQ0 quantum feed only. */
	if (!IvtHooked(0x7F)) return;
	uint8_t* mem = np2_mem();
	if (!mem) return;
	const unsigned off = (unsigned)mem[0x7F * 4] | ((unsigned)mem[0x7F * 4 + 1] << 8);
	const unsigned seg = (unsigned)mem[0x7F * 4 + 2] | ((unsigned)mem[0x7F * 4 + 3] << 8);
	const unsigned lin = (seg << 4) + off;
	if (lin + 0x40 >= 0x200000) return;
	int isHoot = 0;
	for (unsigned i = 0; i < 40 && lin + i + 3 < 0x200000; i++) {
		if (mem[lin + i] == 0xBA && mem[lin + i + 1] == 0xE0 && mem[lin + i + 2] == 0x07
			&& mem[lin + i + 3] == 0xEC) {
			isHoot = 1;
			break;
		}
	}
	if (!isHoot) return;
	uint16_t ds = 0;
	for (unsigned i = 0x10; i < 0x80 && lin + i + 4 < 0x200000; i++) {
		if (mem[lin + i] == 0xB8 && mem[lin + i + 3] == 0x8E && mem[lin + i + 4] == 0xD8) {
			ds = (uint16_t)(mem[lin + i + 1] | (mem[lin + i + 2] << 8));
			break;
		}
		/* Tiny-model HOOT: mov ax,cs / mov ds,ax — data lives in INT7F CS. */
		if (mem[lin + i] == 0x8C && mem[lin + i + 1] == 0xC8
			&& mem[lin + i + 2] == 0x8E && mem[lin + i + 3] == 0xD8) {
			ds = (uint16_t)seg;
			break;
		}
	}
	if (!ds && seg >= 0x0100 && seg < 0xA000)
		ds = (uint16_t)seg;
	if (!ds) return;
	const unsigned base = (unsigned)ds << 4;
	if (base + 0x448 >= 0x200000) return;
	auto rd = [&](unsigned o) -> uint16_t {
		return (uint16_t)(mem[base + o] | (mem[base + o + 1] << 8));
	};
	auto wr = [&](unsigned o, uint16_t v) {
		mem[base + o] = (uint8_t)(v & 0xff);
		mem[base + o + 1] = (uint8_t)(v >> 8);
	};
	/* HOOT idle sentinels: unused handles are FFFF. Do NOT coerce 0→FFFF —
	   AIL driver/sequence handle 0 is valid; wiping it after INT7F play
	   undoes a successful AIL_register_driver. */

	const uint16_t drvOff = rd(0x444);
	const uint16_t drvSeg = rd(0x446);
	const unsigned nameLin = ((unsigned)drvSeg << 4) + drvOff;
	int retargetLoader = 0;
	if (nameLin + 12 < 0x200000 && drvSeg >= 0x100 && drvSeg < 0xA000) {
		if (mem[nameLin] == 0x2D && mem[nameLin + 1] == 0x00
			&& mem[nameLin + 2] == 'C' && mem[nameLin + 3] == 'o') {
			/* [0444] already points at the resident ADV image, not the
			   filename. MIDI.ADV is 7KB; treating it as a 16KB OPL blob made
			   the 232Dh quantum fallback write past the driver. */
			hootAdvSeg_ = drvSeg;
			const CEmuDos98File* adv = NULL;
			auto matchAdv = [&](const CEmuDos98File* f) -> int {
				if (!f || !f->data || f->size < 64) return 0;
				unsigned n = 32;
				if (n > f->size) n = f->size;
				if (nameLin + n >= 0x200000) return 0;
				return memcmp(mem + nameLin, f->data, n) == 0;
			};
			if (dosGe_) {
				for (int i = 0; i < dosGe_->romCount && !adv; i++) {
					const char* nm = dosGe_->rom[i].name;
					const char* base = nm;
					for (const char* p = nm; *p; p++) {
						if (*p == '\\' || *p == '/' || *p == ':')
							base = p + 1;
					}
					const char* ext = strrchr(base, '.');
					if (!ext || _stricmp(ext, ".ADV") != 0) continue;
					const CEmuDos98File* f = dos_.FindFile(base);
					if (matchAdv(f)) adv = f;
				}
			}
			if (!adv) {
				static const char* kAdv[] = {
					"MIDI.ADV", "ADLIB.ADV", "SBP2FM.ADV", "MT32MPU.ADV",
					"OPL3.ADV", "PAS.ADV", NULL
				};
				for (int i = 0; kAdv[i] && !adv; i++) {
					const CEmuDos98File* f = dos_.FindFile(kAdv[i]);
					if (matchAdv(f)) adv = f;
				}
			}
			if (adv) {
				hootAdvSize_ = adv->size;
				strncpy_s(hootAdvName_, adv->name, _TRUNCATE);
				if (!hootAdvQuantumOff_)
					hootAdvQuantumOff_ = FindHootAdvQuantumOff(adv->data, adv->size);
				if (!hootAdvIoOff_)
					hootAdvIoOff_ = FindHootAdvIoOff(adv->data, adv->size);
			} else {
				unsigned cap = 0x1C00;
				if (nameLin + cap > 0xA0000u)
					cap = 0xA0000u - nameLin;
				if (!hootAdvSize_) hootAdvSize_ = cap;
				if (!hootAdvQuantumOff_)
					hootAdvQuantumOff_ = FindHootAdvQuantumOff(mem + nameLin, cap);
			}
		} else {
			char advName[16] = {};
			int n = 0;
			for (; n < 12 && mem[nameLin + n]; n++) {
				char c = (char)mem[nameLin + n];
				if (c < 32 || c > 126) { n = 0; break; }
				advName[n] = c;
			}
			advName[n] = 0;
			for (int i = 0; advName[i]; i++) {
				if (advName[i] >= 'a' && advName[i] <= 'z')
					advName[i] = (char)(advName[i] - 'a' + 'A');
			}
			if (n >= 5 && strstr(advName, ".ADV")) {
				strncpy_s(hootAdvName_, advName, _TRUNCATE);
				const CEmuDos98File* adv = dos_.FindFile(advName);
				if (adv && adv->data && adv->size >= 64) {
					hootAdvSize_ = adv->size;
					if (!hootAdvQuantumOff_)
						hootAdvQuantumOff_ = FindHootAdvQuantumOff(adv->data, adv->size);
					if (!hootAdvIoOff_)
						hootAdvIoOff_ = FindHootAdvIoOff(adv->data, adv->size);
					/* Keep filename at [0444]; also stage a resident copy so the
					   HOOT ADV-loader far target can return DX:AX = image when
					   a second INT21 open/alloc fails mid-play. */
					if (!hootAdvSeg_) {
						const uint16_t paras = (uint16_t)((adv->size + 15u) / 16u + 0x80u);
						uint16_t segOut = 0;
						if (dos_.AllocBlock(mem, paras, &segOut) && segOut) {
							hootAdvSeg_ = segOut;
							memcpy(mem + ((unsigned)segOut << 4), adv->data, adv->size);
						}
					} else {
						memcpy(mem + ((unsigned)hootAdvSeg_ << 4), adv->data, adv->size);
					}
					retargetLoader = 1;
				}
			}
		}
	}
	if (!hootAdvSeg_) {
		/* INT7F play already mapped the ADV; [0444] is sometimes still a
		   filename or 0. Locate the resident Miles header by its bytes. */
		const CEmuDos98File* adv = dos_.FindFile("MIDI.ADV");
		if (!adv && dosGe_) {
			for (int i = 0; i < dosGe_->romCount; i++) {
				const char* nm = dosGe_->rom[i].name;
				const char* b = nm;
				for (const char* p = nm; *p; p++) {
					if (*p == '\\' || *p == '/' || *p == ':')
						b = p + 1;
				}
				const char* ext = strrchr(b, '.');
				if (!ext || _stricmp(ext, ".ADV") != 0) continue;
				adv = dos_.FindFile(b);
				if (adv) break;
			}
		}
		if (adv && adv->data && adv->size >= 64) {
			for (unsigned a = 0x10000; a + 32 < 0xA0000; a += 16) {
				if (memcmp(mem + a, adv->data, 32) == 0) {
					hootAdvSeg_ = (uint16_t)(a >> 4);
					hootAdvSize_ = adv->size;
					strncpy_s(hootAdvName_, adv->name, _TRUNCATE);
					if (!hootAdvQuantumOff_)
						hootAdvQuantumOff_ = FindHootAdvQuantumOff(adv->data, adv->size);
					if (!hootAdvIoOff_)
						hootAdvIoOff_ = FindHootAdvIoOff(adv->data, adv->size);
					break;
				}
			}
		}
	}
	if (hootAdvSeg_ && retargetLoader) {
		/* HOOT play: push [446]; push [444]; CALL FAR loader.
		   Retarget only that loader entry to return the staged ADV segment
		   (mov dx,seg / xor ax,ax / retf) — same result as a successful load. */
		for (unsigned a = 0x10000; a + 13 < 0xA0000; a++) {
			if (mem[a] != 0xFF || mem[a + 1] != 0x36 || mem[a + 2] != 0x46 || mem[a + 3] != 0x04)
				continue;
			if (mem[a + 4] != 0xFF || mem[a + 5] != 0x36 || mem[a + 6] != 0x44 || mem[a + 7] != 0x04)
				continue;
			if (mem[a + 8] != 0x9A) continue;
			const uint16_t tOff = (uint16_t)(mem[a + 9] | (mem[a + 10] << 8));
			const uint16_t tSeg = (uint16_t)(mem[a + 11] | (mem[a + 12] << 8));
			const unsigned tLin = ((unsigned)tSeg << 4) + tOff;
			if (tLin + 6 >= 0x200000) continue;
			const int isLoader = (mem[tLin] == 0x55 && mem[tLin + 1] == 0x8B && mem[tLin + 2] == 0xEC);
			const int isRet = (mem[tLin] == 0xBA && mem[tLin + 3] == 0x33 && mem[tLin + 4] == 0xC0
				&& mem[tLin + 5] == 0xCB);
			if (!isLoader && !isRet) continue;
			mem[tLin + 0] = 0xBA;
			mem[tLin + 1] = (uint8_t)(hootAdvSeg_ & 0xff);
			mem[tLin + 2] = (uint8_t)(hootAdvSeg_ >> 8);
			mem[tLin + 3] = 0x33;
			mem[tLin + 4] = 0xC0;
			mem[tLin + 5] = 0xCB;
		}
	}
	if (!modeMidi_)
		FixHootAilTimer();
}

void CHardPcat::RestoreHootIdleTrampoline(uint8_t* mem)
{
	if (!mem) return;
	const unsigned tb = (unsigned)DOS98_TRAMP_SEG << 4;
	if (mem[tb + 0] != 0xF4 || mem[tb + 1] != 0xEB || mem[tb + 2] != 0xFD) {
		mem[tb + 0] = 0xF4;
		mem[tb + 1] = 0xEB;
		mem[tb + 2] = 0xFD;
	}
	/* Play-pump idle: not a trap vector. */
	if (mem[tb + PCAT_IDLE_IP] != 0xF4) {
		mem[tb + PCAT_IDLE_IP] = 0xF4;
		mem[tb + PCAT_IDLE_IP + 1] = 0xEB;
		mem[tb + PCAT_IDLE_IP + 2] = 0xFD;
	}
	/* AIL chains pushf;call far to the previous INT8 (0060:0010). Advance
	   BDA timer ticks so ADV's XMIDI quantum [232D] is non-zero. */
	if (mem[tb + 0x10] == 0x1E && mem[tb + 0x11] == 0x50 && mem[tb + 0x1F] == 0xCF)
		return;
	static const uint8_t kBiosTick[] = {
		0x1E, 0x50, 0xB8, 0x40, 0x00, 0x8E, 0xD8,
		0x83, 0x06, 0x6C, 0x00, 0x01,
		0x83, 0x16, 0x6E, 0x00, 0x00,
		0x58, 0x1F, 0xCF
	};
	memcpy(mem + tb + 0x10, kBiosTick, sizeof(kBiosTick));
}

/* The API_timer signature (an INC word, an FC, and six pushes nearby) is weak
   enough to hit ordinary code, and a hit rewrites INT 8, zeroes two bytes at
   CS:000E and rewrites four at CS:0124. In an 8KB sequencer that is a coin
   flip: MDI.DRV and friends were being redirected into the middle of
   themselves and ran off into the interrupt vector table. Only go looking
   when the archive actually booted a HOOT .ADV driver, or when AIL has
   already been confirmed for this title. */
int CHardPcat::HootAilPossible() const
{
	return (hootAdvSeg_ != 0 || hootAdvName_[0] != 0 || hootAilCs_ != 0) ? 1 : 0;
}

void CHardPcat::FixHootMidiInt8()
{
	/* Azrael already has HOOT CS:040Eh and plays. WarCraft 2 / Blackthorne
	   leave AIL CS:0000 (EXE entry) and INT 7Fh is often the AIL CS, so the
	   old 7Fh:040Eh-only plant never fired. Scan for the helper. Do not
	   plant API_timer or rewrite AIL [000E] (hang / Death mute). */
	if (!modeMidi_ || modeSilp_) return;
	if (!hootAdvSeg_ && hootAdvName_[0] == 0) return;
	uint8_t* mem = np2_mem();
	if (!mem) return;
	const uint16_t i8Off = (uint16_t)(mem[0x08 * 4] | (mem[0x08 * 4 + 1] << 8));
	const uint16_t i8Seg = (uint16_t)(mem[0x08 * 4 + 2] | (mem[0x08 * 4 + 3] << 8));
	if (i8Seg >= 0x0100 && i8Seg < 0xA000 && i8Off != 0
		&& PcatHootInt8Sig(mem, ((unsigned)i8Seg << 4) + i8Off))
		return;
	/* Azrael already parked a working helper at a non-zero offset. Only
	   retarget the EXE-entry leftover (CS:0000) so we do not steal INT8
	   from a live sequence. */
	if (i8Off != 0) return;
	/* Blackthorne / WarCraft 2 leave INT8 at HOOT CS:0000 (MZ CS:IP). The
	   helper is the same CS:040Eh; INT 7Fh often points at AIL instead. */
	if (i8Seg >= 0x1200 && i8Seg < 0xA000
		&& PcatHootInt8Sig(mem, ((unsigned)i8Seg << 4) + 0x040Eu)) {
		mem[0x08 * 4] = 0x0E;
		mem[0x08 * 4 + 1] = 0x04;
		mem[0x08 * 4 + 2] = (uint8_t)(i8Seg & 0xff);
		mem[0x08 * 4 + 3] = (uint8_t)(i8Seg >> 8);
		return;
	}
	uint16_t hs = 0, ho = 0;
	if (!PcatFindHootInt8(mem, &hs, &ho)) return;
	if (hs == hootAdvSeg_ || hs == DOS98_TRAMP_SEG) return;
	mem[0x08 * 4] = (uint8_t)(ho & 0xff);
	mem[0x08 * 4 + 1] = (uint8_t)(ho >> 8);
	mem[0x08 * 4 + 2] = (uint8_t)(hs & 0xff);
	mem[0x08 * 4 + 3] = (uint8_t)(hs >> 8);
}

void CHardPcat::RepairMokMidiPlay()
{
	/* Mok CODE.COM + MID.DRV: INT7F already AH=3F'd the SMF (read log matches
	   the catalog size) but AH=0's `les si,[03A4]` often does not see MThd —
	   nested IRQ0 during the glue call, or DS at INT 21h not the song block.
	   Copy the catalog SMF into CS:[0200] and far-call MID.DRV AH=0 with
	   CX:DX = song:0 on a clean stack (no TickSide / IRQ). */
	if (!modeMidi_ || modeSilp_) return;
	if (hootAdvSeg_ || hootAdvName_[0]) return;
	uint8_t* mem = np2_mem();
	if (!mem || !IvtHooked(0x7F)) return;
	const uint16_t comCs = (uint16_t)(mem[0x7F * 4 + 2] | (mem[0x7F * 4 + 3] << 8));
	if (comCs < 0x0100 || comCs >= 0xA000) return;
	const unsigned com = (unsigned)comCs << 4;
	if (com + 0x220 >= 0x200000) return;
	if (memcmp(mem + com + 0x20A, "SMF HOOT", 8) != 0) return;
	const uint16_t songSeg = (uint16_t)(mem[com + 0x200] | (mem[com + 0x201] << 8));
	uint16_t drvSeg = (uint16_t)(mem[com + 0x204] | (mem[com + 0x205] << 8));
	const uint16_t i8Off = (uint16_t)(mem[0x08 * 4] | (mem[0x08 * 4 + 1] << 8));
	const uint16_t i8Seg = (uint16_t)(mem[0x08 * 4 + 2] | (mem[0x08 * 4 + 3] << 8));
	if (i8Off == 0x0078 && i8Seg >= 0x0100 && i8Seg < 0xA000)
		drvSeg = i8Seg;
	if (songSeg < 0x0100 || songSeg >= 0xA000) return;
	if (drvSeg < 0x0100 || drvSeg >= 0xA000) return;
	const unsigned drv = (unsigned)drvSeg << 4;
	if (drv + 0x3B0 >= 0x200000) return;
	if (mem[drv + 0x78] != 0x60) return;
	mokDrvSeg_ = 0;

	const CEmuDos98File* sf = dosSong_[0] ? dos_.FindFile(dosSong_) : NULL;
	if (!sf || !sf->data || sf->size < 14) return;
	if (sf->data[0] != 'M' || sf->data[1] != 'T'
		|| sf->data[2] != 'h' || sf->data[3] != 'd')
		return;
	uint8_t* flat = new uint8_t[0x10000];
	unsigned flatN = 0;
	const uint8_t* use = sf->data;
	unsigned useN = sf->size;
	if (MokFlattenSmfType0(sf->data, sf->size, flat, 0xFFF0u, &flatN) && flatN >= 14) {
		use = flat;
		useN = flatN;
	}
	uint16_t bufSeg = 0;
	{
		const uint16_t paras = (uint16_t)((useN + 32u) / 16u);
		if (!dos_.AllocBlock(mem, paras, &bufSeg) || bufSeg < 0x0100)
			bufSeg = 0x9000;
	}
	if (bufSeg < 0x0100 || bufSeg >= 0xA000) {
		delete[] flat;
		return;
	}
	const unsigned dst = (unsigned)bufSeg << 4;
	if (dst + useN >= 0x200000) {
		delete[] flat;
		return;
	}
	memcpy(mem + dst, use, useN);
	delete[] flat;
	{
		const CEmuDos98File* df = dos_.FindFile("MID.DRV");
		if (df && df->data && df->size && drv + df->size < 0x200000)
			memcpy(mem + drv, df->data, df->size);
	}
	if (mem[drv + 0x78] != 0x60) return;

	mem[0x08 * 4] = 0x78;
	mem[0x08 * 4 + 1] = 0x00;
	mem[0x08 * 4 + 2] = (uint8_t)(drvSeg & 0xff);
	mem[0x08 * 4 + 3] = (uint8_t)(drvSeg >> 8);
	mem[drv + 0x3A3] = 1;
	/* Overflow path of INT8 is `jmp far [03B0]` to the previous vector.
	   That is our HLT trampoline, which never IRETs — stack/IVT die after
	   ~40 ticks. Point the chain at EOI+IRET in the driver's BSS. */
	mem[drv + 0x5C0] = 0xB0;
	mem[drv + 0x5C1] = 0x20;
	mem[drv + 0x5C2] = 0xE6;
	mem[drv + 0x5C3] = 0x20;
	mem[drv + 0x5C4] = 0xCF;
	mem[drv + 0x3B0] = 0xC0;
	mem[drv + 0x3B1] = 0x05;
	mem[drv + 0x3B2] = (uint8_t)(drvSeg & 0xff);
	mem[drv + 0x3B3] = (uint8_t)(drvSeg >> 8);

	const uint16_t ss = comCs;
	const uint16_t sp0 = 0x7E00;
	np2_reg_set(NP2_R_SS, ss);
	np2_reg_set(NP2_R_SP, sp0);
	uint16_t sp = (uint16_t)(sp0 - 4);
	{
		const unsigned sl = ((unsigned)ss << 4) + sp;
		mem[sl] = 0;
		mem[sl + 1] = 0;
		mem[sl + 2] = (uint8_t)(DOS98_TRAMP_SEG & 0xff);
		mem[sl + 3] = (uint8_t)(DOS98_TRAMP_SEG >> 8);
	}
	np2_reg_set(NP2_R_SP, sp);
	np2_reg_set(NP2_R_AX, 0x0000);
	np2_reg_set(NP2_R_CX, bufSeg);
	np2_reg_set(NP2_R_DX, 0);
	np2_reg_set(NP2_R_DS, drvSeg);
	np2_reg_set(NP2_R_ES, drvSeg);
	np2_reg_set(NP2_R_CS, drvSeg);
	np2_reg_set(NP2_R_IP, 0);
	np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) & ~0x0200));
	const uint64_t budget = (uint64_t)cpuHz_ * 2ull;
	const uint64_t start = cpuCycles_;
	while (cpuCycles_ - start < budget) {
		if (np2_reg_get(NP2_R_CS) == (uint16_t)DOS98_TRAMP_SEG)
			break;
		const int32_t c = np2_step();
		const uint64_t u = (c > 0) ? (uint64_t)c : 1ull;
		cpuCycles_ += u;
	}
	mem[0x08 * 4] = 0x78;
	mem[0x08 * 4 + 1] = 0x00;
	mem[0x08 * 4 + 2] = (uint8_t)(drvSeg & 0xff);
	mem[0x08 * 4 + 3] = (uint8_t)(drvSeg >> 8);
	mokDrvSeg_ = drvSeg;
}

void CHardPcat::FixHootAilTimer()
{
	/* AIL hook_timer should point INT 8 at API_timer. HOOT / nested IRQ0 often
	   leave IVT at ailCS:0000 or restore the DOS trampoline — re-check always. */
	if (!HootAilPossible()) return;
	uint8_t* mem = np2_mem();
	if (!mem) return;
	const uint16_t i8Off = (uint16_t)(mem[0x08 * 4] | (mem[0x08 * 4 + 1] << 8));
	const uint16_t i8Seg = (uint16_t)(mem[0x08 * 4 + 2] | (mem[0x08 * 4 + 3] << 8));

	auto findApiTimer = [&](uint16_t seg) -> unsigned {
		if (hootAdvSeg_ && seg == hootAdvSeg_) return 0;
		return PcatFindApiTimerOff(mem, seg);
	};

	auto plant = [&](uint16_t seg, unsigned found) {
		const unsigned base = (unsigned)seg << 4;
		mem[0x08 * 4] = (uint8_t)(found & 0xff);
		mem[0x08 * 4 + 1] = (uint8_t)(found >> 8);
		mem[0x08 * 4 + 2] = (uint8_t)(seg & 0xff);
		mem[0x08 * 4 + 3] = (uint8_t)(seg >> 8);
		/* Clear AIL re-entry counter so a prior nested IRQ0 fault doesn't stick. */
		mem[base + 0x0E] = 0;
		mem[base + 0x0F] = 0;
		/* HOOT/AIL wipe DOS trampoline RAM; restore idle + BIOS tick chain. */
		RestoreHootIdleTrampoline(mem);
		/* If AIL's saved BIOS timer ptr is present, force it to the IRET stub. */
		if (base + 0x128 < 0x200000
			&& mem[base + 0x124] == 0x10 && mem[base + 0x125] == 0x00
			&& mem[base + 0x126] == (uint8_t)(DOS98_TRAMP_SEG & 0xff)
			&& mem[base + 0x127] == (uint8_t)(DOS98_TRAMP_SEG >> 8)) {
			/* already 0060:0010 — stub restored above */
		} else if (base + 0x128 < 0x200000) {
			/* Common AIL2 layout: dword BIOS_timer @ CS:0124 */
			const uint16_t bo = (uint16_t)(mem[base + 0x124] | (mem[base + 0x125] << 8));
			const uint16_t bs = (uint16_t)(mem[base + 0x126] | (mem[base + 0x127] << 8));
			if (bs == DOS98_TRAMP_SEG || (bs < 0x0100 && bo < 0x200)) {
				mem[base + 0x124] = 0x10;
				mem[base + 0x125] = 0x00;
				mem[base + 0x126] = (uint8_t)(DOS98_TRAMP_SEG & 0xff);
				mem[base + 0x127] = (uint8_t)(DOS98_TRAMP_SEG >> 8);
			}
		}
		/* Do not rewrite AIL stack_check immediates — keep guest binary intact. */
		hootAilCs_ = seg;
		hootTimerFixed_ = 1;
	};

	/* Already a plausible ISR? Keep it (but heal a stuck re-entry word).
	   Do not replace API_timer with a HOOT 040Eh helper: Lost Vikings clocks
	   AIL CS:0417 and a swap to a false 040Eh made it SILENT. */
	if (i8Seg >= 0x0100 && i8Seg < 0xA000 && i8Seg != DOS98_TRAMP_SEG && i8Off != 0) {
		const unsigned cur = ((unsigned)i8Seg << 4) + i8Off;
		const int looksInc = (cur + 16 < 0x200000 && mem[cur] == 0xFF && mem[cur + 1] == 0x06);
		if (looksInc) {
			const unsigned base = (unsigned)i8Seg << 4;
			const uint16_t re = (uint16_t)(mem[base + 0x0E] | (mem[base + 0x0F] << 8));
			if (re > 1 && np2_reg_get(NP2_R_CS) != i8Seg) {
				mem[base + 0x0E] = 0;
				mem[base + 0x0F] = 0;
			}
			RestoreHootIdleTrampoline(mem);
			hootAilCs_ = i8Seg;
			hootTimerFixed_ = 1;
			return;
		}
		if (PcatHootInt8Sig(mem, cur)) {
			RestoreHootIdleTrampoline(mem);
			hootTimerFixed_ = 1;
			return;
		}
		if (i8Off != 0) return; /* unknown non-zero handler */

	}

	/* Prefer known AIL CS, else current INT8 seg if it looks like AIL data/code. */
	uint16_t trySeg = hootAilCs_;
	if (!trySeg && i8Seg >= 0x0100 && i8Seg < 0xA000 && i8Seg != DOS98_TRAMP_SEG)
		trySeg = i8Seg;
	if (!trySeg) {
		/* INT 66h is AIL's multiplex on some builds; fall back to scan. */
		const uint16_t s66 = (uint16_t)(mem[0x66 * 4 + 2] | (mem[0x66 * 4 + 3] << 8));
		if (s66 >= 0x0100 && s66 < 0xA000 && s66 != DOS98_TRAMP_SEG)
			trySeg = s66;
	}
	if (trySeg) {
		const unsigned found = findApiTimer(trySeg);
		if (found) {
			plant(trySeg, found);
			return;
		}
	}
}

void CHardPcat::PreloadSilpSong(unsigned titleCode)
{
	/* silp play path: AH=3F BX=0 into DS=[0275]. If CS:0275 was clobbered or
	   handle 0 missed the bind, seed the buffer so BP=6 sees real SCI bytes. */
	if (!modeSilp_ || !IvtHooked(0x7F)) return;
	uint8_t* mem = np2_mem();
	if (!mem) return;
	RepairSilpDriverFar();
	uint16_t silpCs = (uint16_t)(mem[0x7F * 4 + 2] | (mem[0x7F * 4 + 3] << 8));
	if (silpCs < 0x0100) return;
	const unsigned base = (unsigned)silpCs << 4;
	if (base + 0x278 >= 0x200000) return;
	uint16_t songSeg = (uint16_t)(mem[base + 0x275] | (mem[base + 0x276] << 8));
	auto songOk = [&](uint16_t s) -> int {
		if (s < 0x1000 || s >= 0xA000) return 0;
		const unsigned a = (unsigned)s << 4;
		if (a + 2 >= 0x200000) return 0;
		/* Empty alloc is OK to fill; reject obvious DRV images. */
		if (mem[a] == 0xE9 && mem[a + 8] == 'd') return 0;
		return 1;
	};
	if (silpSongSeg_ && !songOk(songSeg))
		songSeg = silpSongSeg_;
	if (!songOk(songSeg)) return;

	const char* sf = dosSong_[0] ? dosSong_ : NULL;
	if (!sf && dosGe_)
		sf = SelectedDosSong(dosGe_, titleCode);
	if (!sf) return;
	const CEmuDos98File* file = dos_.FindFile(sf);
	if (!file || !file->data || !file->size) return;

	/* Sorcerian SS* (~15KB+) was allocated just above silp COM. With
	   SS=DS=silp and SP=7F00 the IRQ stack grows down into that buffer and
	   truncates MIDI mid-phrase (~16s / pos 0xE29 on SS001). Relocate the
	   SCI image to a high paragraph well clear of silp's 64K. */
	enum { kSafeSongSeg = 0x7000 };
	const unsigned need = file->size + 16u;
	const unsigned safeBase = (unsigned)kSafeSongSeg << 4;
	if (safeBase + need < 0xA0000u && safeBase + need < 0x200000u) {
		const unsigned silpEnd = base + 0x10000u;
		const unsigned curBase = (unsigned)songSeg << 4;
		/* Move when song lies inside silp's 64K (or overlaps it). */
		if (curBase >= base && curBase < silpEnd) {
			memset(mem + safeBase, 0, need);
			songSeg = (uint16_t)kSafeSongSeg;
		}
	}

	const unsigned dst = (unsigned)songSeg << 4;
	unsigned n = file->size;
	if (dst + n > 0x200000) n = 0x200000 - dst;
	if (!n) return;
	memcpy(mem + dst, file->data, n);
	silpSongSeg_ = songSeg;
	silpSongBytes_ = n;
	mem[base + 0x275] = (uint8_t)(songSeg & 0xff);
	mem[base + 0x276] = (uint8_t)(songSeg >> 8);
	/* Match silp's digital-skip: offset = [1] + 2 (SCI magic 84 00 …). */
	unsigned dig = (unsigned)mem[dst + 1] + 2u;
	mem[base + 0x273] = (uint8_t)(dig & 0xff);
	mem[base + 0x274] = (uint8_t)((dig >> 8) & 0xff);
	dos_.SetHandle(0, sf);
	dos_.SetHandle(5, sf);
	dos_.SetHandle(0x0B, sf);
	const unsigned low = titleCode & 0xff;
	if (low < (unsigned)DOS98_HANDLE_MAX)
		dos_.SetHandle((uint16_t)low, sf);
}

/* Tiny-model silp/ADL need SS=DS=silp. Stack must sit above the SCI song
   (often mapped inside the same 64K — silpheed RESOURCE ~26KB). Gap-below-
   song SP smashes dig/param; SP into the song corrupts the sequence.
   Fixed SP=7F00 is the waterline that keeps both short (sorc) and long
   (silpheed) SCI advancing; per-song SP=6000 rewound sorc [288]. */
static uint16_t SilpIrqStackSp(uint8_t* mem, uint16_t silpCs, uint16_t songSegHint,
	unsigned songBytes)
{
	(void)mem;
	(void)silpCs;
	(void)songSegHint;
	(void)songBytes;
	return 0x7F00;
}

int CHardPcat::DeliverIrqs()
{
	uint8_t* memEarly = np2_mem();
	const uint16_t i7Early = memEarly
		? (uint16_t)(memEarly[0x7F * 4 + 2] | (memEarly[0x7F * 4 + 3] << 8)) : (uint16_t)0;
	const uint16_t i8Early = memEarly
		? (uint16_t)(memEarly[0x08 * 4 + 2] | (memEarly[0x08 * 4 + 3] << 8)) : (uint16_t)0;
	/* Sierra silp_at COM uses the same CS:IP for INT 7Fh and INT 8. Matching
	   CS alone also hits CODE.COM (same CS, different IP), which then gets
	   the silp ISR park / 80k-step cut-off / trampoline restore and never
	   finishes a play or a tick. Gate on modeSilp_ (rom name SILP*). */
	const int silpHot = (modeSilp_ && i7Early == i8Early && i7Early >= 0x0100);
	/* HOOT AIL timer repair scans guest RAM — skip on Sierra silp (INT8==INT7F). */
	if (silpHot)
		RepairSilpDriverFar();
	else if (!modeMidi_)
		FixHootAilTimer();
	if (pit0IrqPending_ && (picMask_ & 0x01) == 0) {
		/* Real 8259 honors IF. CODE.COM CLI around the INT 8 far-call tick;
		   injecting anyway nested a second IRQ0 into Sound Images AH=3
		   (re-entry byte → no-op ticks, held chords). Sierra silp is one
		   exception: after the 80k-step ISR we force CS back to the
		   trampoline without IRET, so IF stays clear.
		   CODE.COM is the other: every ~4 ticks it ADD 5000h / JC and
		   JMP FAR to the saved INT 8, which is our HLT trampoline, and
		   never IRETs. Parked there with IF=0 we still have to accept
		   the next IRQ0 or the song dies after the first BIOS chain. */
		if (!silpHot && (np2_reg_get(NP2_R_FLAGS) & 0x0200) == 0) {
			const uint16_t csIf = np2_reg_get(NP2_R_CS);
			const uint16_t ipIf = np2_reg_get(NP2_R_IP);
			/* Idle HLT past the trap stubs may sit with IF=0 after
			   CODE.COM's JMP FAR chain; accept the next tick there.
			   IP<200h is a trampoline stub — let TrapVector IRET. */
			if (!(csIf == (uint16_t)DOS98_TRAMP_SEG && ipIf >= (uint16_t)PCAT_IDLE_IP))
				return 0;
		}
		uint8_t* mem = memEarly;
		const uint16_t i8Seg = i8Early;
		const uint16_t i8Off = mem
			? (uint16_t)(mem[0x08 * 4] | (mem[0x08 * 4 + 1] << 8)) : (uint16_t)0;
		/* AIL API_timer uses CS:[000E] as a re-entry guard. Nested IRQ0 while
		   IF=1 inside the ISR increments it past 1 and AIL restores the old
		   (trampoline) vector — XMI never advances. Hold the pending bit.
		   Sierra silp shares INT8 CS with INT7F; its [000E] is PSP junk — do
		   not apply the AIL guard there. */
		const int silpOwnsIrq = silpHot;
		/* ...and so is every other PC/AT driver that is simply not AIL. Their
		   [000E] is ordinary code or data, and a non-zero word there held the
		   pending bit forever: CODE.COM installs its sequencer on INT 8 and
		   never received a single tick, so it emitted an OPL init and then no
		   notes at all. hootAilCs_ is only set once the API_timer signature
		   has been found, so it is the right thing to key this on. */
		const int ailOwnsIrq = (hootAilCs_ != 0 && i8Seg == hootAilCs_);
		if (!silpOwnsIrq && ailOwnsIrq && mem
			&& i8Seg >= 0x0100 && i8Seg < 0xA000 && i8Off != 0) {
			const unsigned base = (unsigned)i8Seg << 4;
			const uint16_t re = (uint16_t)(mem[base + 0x0E] | (mem[base + 0x0F] << 8));
			const uint16_t cs = np2_reg_get(NP2_R_CS);
			if (re != 0 || cs == i8Seg)
				return 0;
		}
		pit0IrqPending_ = 0;
		irq0Count_++;
		if (mokDrvSeg_ >= 0x0100 && mokDrvSeg_ < 0xA000 && mem && !silpHot) {
			const unsigned db = (unsigned)mokDrvSeg_ << 4;
			if (db + 0x79 < 0x200000 && mem[db + 0x78] == 0x60
				&& (i8Off != 0x0078 || i8Seg != mokDrvSeg_)) {
				mem[0x08 * 4] = 0x78;
				mem[0x08 * 4 + 1] = 0x00;
				mem[0x08 * 4 + 2] = (uint8_t)(mokDrvSeg_ & 0xff);
				mem[0x08 * 4 + 3] = (uint8_t)(mokDrvSeg_ >> 8);
			}
		}
		/* Sierra silp_at COM ISR uses DS-relative [027B]/[026B] without
		   reloading DS. On a real boot DS stays = CS after the COM entry;
		   our idle trampoline often leaves DS elsewhere, so the IRQ corrupts
		   dig/param block and never reaches ADL.DRV note-ons. */
		if (mem && i8Seg >= 0x0100 && i8Seg < 0xA000) {
			const unsigned sb = (unsigned)i8Seg << 4;
			const int silpIsr = silpHot;
			if (silpIsr) {
				np2_reg_set(NP2_R_DS, i8Seg);
				np2_reg_set(NP2_R_ES, i8Seg);
				if (silpSongBytes_ == 0 && dosSong_[0]) {
					const CEmuDos98File* sf = dos_.FindFile(dosSong_);
					if (sf && sf->size)
						silpSongBytes_ = sf->size;
				}
				/* SS=DS for tiny-model locals inside ADL; SP above SCI song. */
				const uint16_t spSilp = SilpIrqStackSp(mem, i8Seg, silpSongSeg_, silpSongBytes_);
				np2_reg_set(NP2_R_SS, i8Seg);
				np2_reg_set(NP2_R_SP, spSilp);
				np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) & ~0x0200));
			}
			(void)sb;
		}
		/* AIL XMIDI quanta come from BDA timer (0040:006C). HOOT's prior
		   INT8 was our trampoline, so the BIOS tick never advanced and
		   ADV skipped all OPL voice updates ([232D]==0). */
		if (mem) {
			unsigned t = (unsigned)mem[0x46C] | ((unsigned)mem[0x46D] << 8);
			t++;
			mem[0x46C] = (uint8_t)(t & 0xff);
			mem[0x46D] = (uint8_t)(t >> 8);
			if ((t & 0xffff) == 0) {
				unsigned t2 = (unsigned)mem[0x46E] | ((unsigned)mem[0x46F] << 8);
				t2++;
				mem[0x46E] = (uint8_t)(t2 & 0xff);
				mem[0x46F] = (uint8_t)(t2 >> 8);
			}
			/* ADLIB [232D] is the registered-sequence count (inc at 151,
			   loop CX in serve), not an XMIDI quantum. Host-bumping it to 8
			   made serve walk empty 230Dh slots; leftover BSS there is not
			   always 230F==0 and corrupts the voice allocator (Hanse keys=0
			   with seq PLAYING). Do not poke 2331 either (serve reentry). */
		}
		int skipMidiExeEntry = 0;
		if (modeMidi_ && hootAdvSeg_ && i8Off == 0
			&& i8Seg >= 0x0100 && i8Seg < 0xA000
			&& i8Seg != (uint16_t)DOS98_TRAMP_SEG)
			skipMidiExeEntry = 1;
		if (!skipMidiExeEntry)
			np2_interrupt((uint8_t)PCAT_TIMER_VEC);
		if (skipMidiExeEntry)
			return 0;
		/* IBM BIOS INT 08 far-calls INT 1Ch. SOUND.COM keeps IRQ0 and
		   stops chaining the previous vector once ROL.DRV sets a custom
		   PIT rate, so the AdLib sequencer starves unless we deliver 1C
		   ourselves. Skip when 1C shares INT 8's CS — that driver already
		   owns the tick — and skip AIL/silp, whose ISR loop below is not
		   expecting a nested 1C. */
		if (!silpHot && !ailOwnsIrq) {
			if (mem) {
				const unsigned o1c = (unsigned)mem[0x1C * 4]
					| ((unsigned)mem[0x1C * 4 + 1] << 8);
				const unsigned s1c = (unsigned)mem[0x1C * 4 + 2]
					| ((unsigned)mem[0x1C * 4 + 3] << 8);
				if (!((s1c == 0 && o1c == 0) || s1c == (unsigned)DOS98_TRAMP_SEG)
					&& s1c != (unsigned)i8Seg)
					np2_interrupt(0x1C);
			}
			return 1;
		}
		/* Run ISR to completion without TickSide (AIL switches SS:SP). */
		{
			const int silpIsr = silpHot;
			const uint16_t ailSeg = (i8Seg >= 0x0100 && i8Seg < 0xA000) ? i8Seg : hootAilCs_;
			const unsigned ailBase = mem && ailSeg ? ((unsigned)ailSeg << 4) : 0;
			const uint16_t drvSeg = (silpIsr && mem)
				? (uint16_t)(mem[((unsigned)i8Seg << 4) + 0x26D]
					| (mem[((unsigned)i8Seg << 4) + 0x26E] << 8)) : (uint16_t)0;
			int done = 0;
			/* silp far-calls ADL.DRV; needs a generous step budget. Do not
			   charge those steps to cpuCycles_ (below) or PIT/audio crawl. */
			const int guardMax = silpIsr ? 80000 : 4000;
			for (int guard = 0; guard < guardMax; guard++) {
				const uint16_t cs = np2_reg_get(NP2_R_CS);
				const uint16_t re = (mem && ailBase)
					? (uint16_t)(mem[ailBase + 0x0E] | (mem[ailBase + 0x0F] << 8)) : (uint16_t)1;
				if (cs == (uint16_t)DOS98_TRAMP_SEG
					&& (silpIsr || !ailOwnsIrq || re == 0)) {
					done = 1;
					break;
				}
				/* AIL-only early exit. silp far-calls ADL.DRV (other CS) for
				   every tick — treating that as "left ISR" aborts mid-note and
				   corrupts dig/[288]. */
				if (!silpIsr && re == 0 && cs != ailSeg && cs != hootAdvSeg_ && guard > 20) {
					done = 1;
					break;
				}
				if (silpIsr && guard > 20
					&& cs != i8Seg && cs != drvSeg && cs != (uint16_t)DOS98_TRAMP_SEG
					&& cs < 0x0100) {
					done = 1;
					break;
				}
				const int32_t c = np2_step();
				/* Do not charge silp ISR steps to the audio clock — burning
				   tens of kcycles here used to freeze PIT relative to Render
				   and make playback crawl. Finish the far-call off-budget. */
				if (!silpIsr)
					cpuCycles_ += (c > 0) ? (uint64_t)c : 1ull;
			}
			/* Only AIL keeps a counter there — zeroing it on any other driver
			   overwrote two bytes of the guest's own data. */
			if (mem && ailBase && !silpIsr && ailOwnsIrq) {
				mem[ailBase + 0x0E] = 0;
				mem[ailBase + 0x0F] = 0;
			}
			RestoreHootIdleTrampoline(mem);
			if (silpIsr) {
				/* Keep SS=DS=silp for the idle stretch: silpheed's ADL path
				   faults if SS drifts to the DOS trampoline stack. SP stays
				   above the SCI song (see SilpIrqStackSp). */
				if (silpSongBytes_ == 0 && dosSong_[0]) {
					const CEmuDos98File* sf = dos_.FindFile(dosSong_);
					if (sf && sf->size)
						silpSongBytes_ = sf->size;
				}
				const uint16_t spSilp = SilpIrqStackSp(mem, i8Seg, silpSongSeg_, silpSongBytes_);
				np2_reg_set(NP2_R_SS, i8Seg);
				np2_reg_set(NP2_R_SP, spSilp);
				np2_reg_set(NP2_R_DS, i8Seg);
				np2_reg_set(NP2_R_ES, i8Seg);
			} else {
				np2_reg_set(NP2_R_SS, 0x1000);
				np2_reg_set(NP2_R_SP, 0xFF00);
			}
			np2_reg_set(NP2_R_CS, (uint16_t)DOS98_TRAMP_SEG);
			np2_reg_set(NP2_R_IP, (uint16_t)PCAT_IDLE_IP);
			(void)done;
		}
		return 1;
	}
	(void)g_pcatEoi;
	return 0;
}

uint8_t CHardPcat::PortIn(uint16_t port)
{
	/* IBM PS/1 Audio: detect reads 0203h/0205h/0206h as 0. */
	if (modePs1_ && port >= 0x200 && port <= 0x206)
		return 0x00;
	/* AdLib detect uses `in al,dx` / `loop` busy-waits. Each IN on real ISA
	   burns time; advance OPL clocks so timer flags appear without needing
	   host wall-clock. ~80 chip clocks ≈ one classic poll step. */
	auto oplStatus = [this]() -> uint8_t {
		if (!chip_) return 0x00;
		if (oplHz_ > 0)
			chip_->AdvanceClocks(80);
		/* MAME YM3812Read ORs 0x06 (unused bits stuck high). Real ISA AdLib
		   reads 0 when timers are idle. Silky's MUSICV.COM does
		   `in al,388h / or al,al / jnz fail` and otherwise never sets the
		   INT D0 "board present" flag — PLAY5_AT loads the .M/.WM and stays
		   silent. Mask those ID bits; timer/IRQ flags (0xE0) stay intact. */
		return (uint8_t)(chip_->ReadStatus() & (uint8_t)~0x06);
	};
	switch (port) {
	case ADLIB_ADDR:
	case ADLIB_DATA:
		return oplStatus();
	case 0x228: case 0x229:
		return oplStatus();
	case CMS_DATA0: case CMS_ADDR0: case CMS_DATA1: case CMS_ADDR1:
		/* Game Blaster: SAA is write-only. Sound Blaster: same ports are OPL. */
		if (modeCms_)
			return 0xff;
		return oplStatus();
	case 0x224: /* SB Pro mixer index */
		return modeSb_ ? sbMixerIdx_ : (uint8_t)0x00;
	case 0x225: /* SB Pro mixer data */
		return modeSb_ ? sbMixer_[sbMixerIdx_] : (uint8_t)0x00;
	case 0x226: /* SB DSP write-status: bit7=0 → OK to write */
		return 0x00;
	case 0x22A: /* SB DSP read data */
		if (sbDspQueueW_ != sbDspQueueR_) {
			uint8_t v = sbDspQueue_[sbDspQueueR_ & 3];
			sbDspQueueR_++;
			sbDspReadAvail_ = (sbDspQueueW_ != sbDspQueueR_) ? 1 : 0;
			return v;
		}
		if (sbDspReadAvail_) {
			sbDspReadAvail_ = 0;
			return sbDspReadData_;
		}
		return 0x00;
	case 0x22C: /* SB DSP write-status (alt): bit7=0 ready */
		return 0x00;
	case 0x22E: /* SB DSP data-available: bit7=1 when byte ready */
		return (sbDspReadAvail_ || sbDspQueueW_ != sbDspQueueR_) ? 0x80 : 0x00;
	case EXT_CMD: return extCmd_;
	case EXT_SONG: return (uint8_t)(extSong_ & 0xff);
	case EXT_SONG + 1: return (uint8_t)(extSong_ >> 8);
	case EXT_PARAM: return (uint8_t)(extParam_ & 0xff);
	case EXT_PARAM + 1: return (uint8_t)(extParam_ >> 8);
	case EXT_STATE: return stubState_;
	case PIT_CT0: case PIT_CT2: return PitIn(port);
	case PORT_61:
		/* bit4 = refresh toggle; bit5 = PIT2 out (common poll). */
		return (uint8_t)((port61_ & 0x0f)
			| ((pit2Out_ ? 0x20 : 0))
			| (((cpuCycles_ >> 15) & 1) ? 0x10 : 0));
	case PIC_MASK: return picMask_;
	case PIC_CMD: return 0x00;
	case PIC_SLAVE_MASK: return s_picSlaveMask;
	case PIC_SLAVE_CMD: return 0x00;
	case KBC_STAT:
		/* bit0=0 no scancode, bit1=0 input empty, bit4=1 keyboard enabled */
		return 0x10;
	case KBC_DATA:
		return 0x00;
	case CMOS_DATA:
		return s_cmos[s_cmosIdx & 0x7F];
	case CMOS_ADDR:
		return s_cmosIdx;
	case MPU_DATA: return MidiDataIn();
	case MPU_STAT: return MidiStatusIn();
	case 0x188: case 0x18A: case 0x18C: case 0x18E:
		return 0x00;
	default:
		PcatIoLog('r', port, 0);
		return 0x00;
	}
}

void CHardPcat::PortOut(uint16_t port, uint8_t data)
{
	if (sn764_ && modePs1_ && (port == 0x200 || port == 0x205)) {
		sn764_->Write(0, data);
		return;
	}
	if (sn764_ && !modePs1_ && (port & 0xFFE0) == 0x00C0) {
		sn764_->Write(0, data);
		return;
	}
	auto oplAddr = [this](uint8_t d) {
		if (chip_) chip_->Write(0, d);
	};
	auto oplData = [this](uint8_t d) {
		if (chip_) {
			chip_->Write(1, d);
			oplWriteCount_ = CEmuChipYm3812WriteCount(chip_);
			oplKeyOnCount_ = CEmuChipYm3812KeyOnCount(chip_);
		}
	};

	switch (port) {
	case ADLIB_ADDR:
		oplAddr(data);
		break;
	case ADLIB_DATA:
		oplData(data);
		break;
	case 0x228: /* Sound Blaster FM address mirror */
		oplAddr(data);
		break;
	case 0x229:
		oplData(data);
		break;
	case CMS_DATA0:
		if (modeCms_) {
			if (saa1_) saa1_->Write(0, data);
			saaWriteCount_ = (saa1_ ? CEmuChipSaa1099WriteCount(saa1_) : 0)
				+ (saa2_ ? CEmuChipSaa1099WriteCount(saa2_) : 0);
			saaToneOnCount_ = (saa1_ ? CEmuChipSaa1099ToneOnCount(saa1_) : 0)
				+ (saa2_ ? CEmuChipSaa1099ToneOnCount(saa2_) : 0);
			CmsTrackSaa(0, data);
		} else {
			oplAddr(data); /* SB Pro / SBP2FM OPL @ 0x220 */
		}
		break;
	case CMS_ADDR0:
		if (modeCms_) {
			if (saa1_) saa1_->Write(1, data);
			saaSel_[0] = (uint8_t)(data & 0x1f);
		} else {
			oplData(data); /* SB OPL data @ 0x221 */
		}
		break;
	case CMS_DATA1:
		if (modeCms_) {
			if (saa2_) saa2_->Write(0, data);
			saaWriteCount_ = (saa1_ ? CEmuChipSaa1099WriteCount(saa1_) : 0)
				+ (saa2_ ? CEmuChipSaa1099WriteCount(saa2_) : 0);
			saaToneOnCount_ = (saa1_ ? CEmuChipSaa1099ToneOnCount(saa1_) : 0)
				+ (saa2_ ? CEmuChipSaa1099ToneOnCount(saa2_) : 0);
			CmsTrackSaa(1, data);
		} else {
			oplAddr(data); /* dual-OPL left @ 0x222 */
		}
		break;
	case CMS_ADDR1:
		if (modeCms_) {
			if (saa2_) saa2_->Write(1, data);
			saaSel_[1] = (uint8_t)(data & 0x1f);
		} else {
			oplData(data);
		}
		break;
	/* SB Pro mixer. SBP2FM.ADV decides whether a card is present by writing
	   a mixer register, reading it back and comparing, so the register file
	   has to be real storage rather than a stub. */
	case 0x224:
		if (modeSb_) sbMixerIdx_ = data;
		break;
	case 0x225:
		if (!modeSb_)
			break;
		if (sbMixerIdx_ == 0x00)
			SbMixerReset();
		else
			sbMixer_[sbMixerIdx_] = data;
		break;
	case 0x226: /* SB DSP reset: 1 then 0 → DSP returns 0xAA */
		if (data & 1) {
			sbDspResetting_ = 1;
			sbDspReadAvail_ = 0;
			sbDspQueueR_ = sbDspQueueW_ = 0;
		} else if (sbDspResetting_) {
			sbDspResetting_ = 0;
			SbDspPush(0xAA);
		}
		break;
	case 0x22C: /* SB DSP command/data write */
		if (data == 0xE1) { /* get DSP version → major, minor */
			SbDspPush(0x03);
			SbDspPush(0x01);
		} else if (data == 0xE3) {
			SbDspPush(0x01);
		} else if (data == 0xE0) { /* DSP status / embedded DAC ack */
			SbDspPush(0xAA);
		} else if (data == 0xD3 || data == 0xD1 || data == 0xD0 || data == 0xD4) {
			/* speaker on/off / DMA — accept silently */
		}
		break;
	case EXT_CMD: extCmd_ = data; break;
	case EXT_SONG: extSong_ = (extSong_ & 0xff00) | data; break;
	case EXT_SONG + 1: extSong_ = (extSong_ & 0x00ff) | ((uint16_t)data << 8); break;
	case EXT_PARAM: extParam_ = (extParam_ & 0xff00) | data; break;
	case EXT_PARAM + 1: extParam_ = (extParam_ & 0x00ff) | ((uint16_t)data << 8); break;
	case EXT_STATE: stubState_ = data; break;
	case PIT_CT0: case PIT_CT2: case PIT_CTRL: PitOut(port, data); break;
	case PORT_61:
		port61_ = data;
		if (modeBeep_) {
			FmMonShadowWriteAuxReg(0x02, (unsigned)port61_);
			if ((data & 0x03) != 0x03)
				FmMonShadowMidiNote(0, 0, 0);
			else if (pit2Reload_ > 0 && sampleRate_ > 0) {
				const double hz = (double)pitClockHz_ / (double)pit2Reload_;
				const int mid = FmMonShadowHzToMidi(hz);
				FmMonShadowWriteAuxReg(0x00, (unsigned)(pit2Reload_ & 0xff));
				FmMonShadowWriteAuxReg(0x01, (unsigned)(pit2Reload_ >> 8));
				FmMonShadowWriteAuxReg(0x03, (mid >= 0) ? 1u : 0u);
				if (mid >= 0) {
					FmMonShadowWriteAuxReg(0x04, (unsigned)mid);
					FmMonShadowMidiNote(0, mid, 1);
				}
			}
		}
		break;
	case MPU_DATA: MidiDataOut(data); break;
	case MPU_STAT: MidiCmdOut(data); break;
	case PIC_CMD:
		if ((data & 0x10) != 0) {
			picMask_ = 0;
			picMasterIcw1_ = data;
			picMasterIcw_ = 1;
		} else if ((data & 0x20) != 0) {
			g_pcatEoi = 1;
		}
		break;
	case PIC_MASK:
		if (picMasterIcw_) {
			picMasterIcw_++;
			if (picMasterIcw_ >= 3 + (picMasterIcw1_ & 1))
				picMasterIcw_ = 0;
		} else {
			picMask_ = data;
		}
		break;
	case PIC_SLAVE_CMD:
		if ((data & 0x10) != 0) {
			s_picSlaveMask = 0xFF;
			s_picSlaveIcw1 = data;
			s_picSlaveIcw = 1;
		}
		break;
	case PIC_SLAVE_MASK:
		if (s_picSlaveIcw) {
			s_picSlaveIcw++;
			if (s_picSlaveIcw >= 3 + (s_picSlaveIcw1 & 1))
				s_picSlaveIcw = 0;
		} else {
			s_picSlaveMask = data;
		}
		break;
	case KBC_STAT:
	case KBC_DATA:
		break;
	case CMOS_ADDR:
		s_cmosIdx = (uint8_t)(data & 0x7F);
		break;
	case CMOS_DATA:
		s_cmos[s_cmosIdx & 0x7F] = data;
		break;
	default:
		PcatIoLog('w', port, data);
		break;
	}
}

void CHardPcat::MaterializeDosFiles(CEmuZipFs* fs, const CEmuGameEntry* ge)
{
	if (!fs || !ge) return;
	/* Some HOOT AIL2 .ADV dumps have truncated `mov cx,xxxxh` / IN delay
	   loops (10h/20h) that cannot wait out YM3812 timer1 (~255*80 clocks).
	   Restore classic 1000h busy-waits. Also heal bad `mov si,00C8h` reloc. */
	auto repairMilesAdv = [](unsigned char* d, unsigned n) {
		if (!d || n < 0x200) return;
		int miles = 0;
		for (unsigned i = 0; i + 34 < n; i++) {
			if (memcmp(d + i, "Copyright (C) 1991,1992 Miles Design", 36) == 0) {
				miles = 1;
				break;
			}
		}
		if (!miles) return;
		for (unsigned i = 0; i + 8 < n; i++) {
			if (d[i] == 0xB9 && d[i + 3] == 0xEC && d[i + 4] == 0xE2) {
				const uint16_t cx = (uint16_t)(d[i + 1] | (d[i + 2] << 8));
				if (cx > 0 && cx < 0x1000) {
					d[i + 1] = 0x00;
					d[i + 2] = 0x10; /* 1000h */
				}
			}
			if (d[i] == 0x83 && d[i + 1] == 0xC4 && d[i + 2] == 0x04
				&& d[i + 3] == 0xBE && d[i + 4] == 0xC8 && d[i + 5] == 0x00
				&& d[i + 6] == 0x0E && d[i + 7] == 0xE8) {
				d[i + 4] = 0x00;
				d[i + 5] = 0x10;
			}
		}
	};
	auto addMaybeAdv = [&](const char* base, const unsigned char* data, unsigned sz) {
		const char* ext = strrchr(base, '.');
		if (ext && _stricmp(ext, ".ADV") == 0 && data && sz >= 0x200) {
			unsigned char* copy = (unsigned char*)malloc(sz);
			if (copy) {
				memcpy(copy, data, sz);
				repairMilesAdv(copy, sz);
				dos_.AddFile(base, copy, sz);
				free(copy);
				return;
			}
		}
		dos_.AddFile(base, data, sz);
	};
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "conin") != 0)
			continue;
		unsigned sz = 0;
		const unsigned char* data = CEmuZipFsFind(fs, r->name, &sz);
		if (!data || !sz) continue;
		const char* base = r->name;
		for (const char* p = r->name; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		addMaybeAdv(base, data, sz);
	}
	/* Also expose every zip member so shell-resolved COMs / songs resolve. */
	for (int i = 0; i < fs->fileCount; i++) {
		char pathA[CEMU_ZIP_PATH];
		WideCharToMultiByte(932, 0, fs->files[i].path, -1, pathA, (int)sizeof(pathA), NULL, NULL);
		const char* base = pathA;
		for (const char* p = pathA; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		if (!base[0]) continue;
		/* Zip extras exist so shell-resolved COMs / songs resolve. Do not
		   also dump every .ADV in the archive: hanse/bchess OPL catalogs
		   list ADLIB.ADV, but the same zip carries MIDI.ADV for the SC-55
		   sibling, and HOOT then registers the MPU driver on an OPL machine
		   (irq0 stays in the dozens, keys=0). Once the romlist named an ADV,
		   only those ADVs belong on the DOS disk. */
		{
			const char* ext = strrchr(base, '.');
			if (ext && _stricmp(ext, ".ADV") == 0) {
				int romHasAdv = 0, inRom = 0;
				for (int j = 0; j < ge->romCount; j++) {
					const CEmuRomEntry* r = &ge->rom[j];
					if (_stricmp(r->type, "file") != 0) continue;
					const char* b = r->name;
					for (const char* p = r->name; *p; p++) {
						if (*p == '\\' || *p == '/' || *p == ':')
							b = p + 1;
					}
					const char* e = strrchr(b, '.');
					if (e && _stricmp(e, ".ADV") == 0)
						romHasAdv = 1;
					if (_stricmp(b, base) == 0)
						inRom = 1;
				}
				if (romHasAdv && !inRom)
					continue;
			}
		}
		addMaybeAdv(base, fs->files[i].data, fs->files[i].size);
	}
	/* Do NOT materialize a fake NULL/NONE file — HOOT treats open-failure as
	   "no timbre bank" and continues; a zero-byte success aborts ADV install. */
}

void CHardPcat::BindDosRomHandles(const CEmuGameEntry* ge)
{
	if (!ge) return;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "file") != 0 && _stricmp(r->type, "conin") != 0)
			continue;
		const int off = r->offset;
		if (off < 0 || off >= DOS98_HANDLE_MAX) continue;
		const char* base = r->name;
		for (const char* p = r->name; *p; p++) {
			if (*p == '\\' || *p == '/' || *p == ':')
				base = p + 1;
		}
		/* Battle Chess 4000 lists ADLIB.ADV at offset 9 and SAMPLE.AD at 10.
		   Those are catalog slots, not DOS handles. Pre-opening them as
		   handles 9/10 makes HOOT inherit a GTL it was told to skip (NULL),
		   and detect/init then leaves hDrvr=FFFF. Omar Sharif uses offset -1
		   for the same files and plays. */
		if (off < 0x10) {
			const char* ext = strrchr(base, '.');
			if (ext && _stricmp(ext, ".ADV") == 0)
				continue;
			if (_strnicmp(base, "SAMPLE.", 7) == 0)
				continue;
		}
		if (_stricmp(r->type, "conin") == 0)
			dos_.SetHandleText((uint16_t)off, base);
		else
			dos_.SetHandle((uint16_t)off, base);
	}
}

const char* CHardPcat::SelectedDosSong(const CEmuGameEntry* ge, unsigned titleCode) const
{
	if (!ge) return NULL;
	const int low = (int)(titleCode & 0xff);
	const int hi = (int)((titleCode >> 8) & 0xff);
	const int full = (int)titleCode;
	/* HOOT/AIL packs often use title 0x1n00 → DOS handle/offset 0x1n. */
	for (int pass = 0; pass < 3; pass++) {
		const int want = pass == 0 ? full : (pass == 1 ? hi : low);
		if (want == 0 && pass != 0) continue;
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "file") != 0) continue;
			if (r->offset != want) continue;
			if (DosIsEngineName(r->name)) continue;
			const char* base = r->name;
			for (const char* p = r->name; *p; p++) {
				if (*p == '\\' || *p == '/' || *p == ':')
					base = p + 1;
			}
			return base;
		}
	}
	return NULL;
}

void CHardPcat::BindDosTriggerSong(const CEmuGameEntry* ge, unsigned titleCode)
{
	const char* sf = SelectedDosSong(ge, titleCode);
	if (sf) {
		strncpy_s(dosSong_, sf, _TRUNCATE);
		dos_.SetHandle(0, sf);
		dos_.SetHandle(5, sf);
		dos_.SetHandle(0x0B, sf);
		const unsigned low = titleCode & 0xff;
		const unsigned hi = (titleCode >> 8) & 0xff;
		if (low < (unsigned)DOS98_HANDLE_MAX)
			dos_.SetHandle((uint16_t)low, sf);
		if (hi && hi < (unsigned)DOS98_HANDLE_MAX)
			dos_.SetHandle((uint16_t)hi, sf);
	}
	extCmd_ = 0;
	/* PMDIBM titles are single-byte (EXT_SONG=code). KOEI FMDRV_AT /
	   Infogrames CODE.COM pack song in the high word (EXT_PARAM) and the
	   bank/file selector in the low word (EXT_SONG) — same as PC-98 KOEI.
	   HOOT/AIL uses the full EXT_SONG word (often 0x1n00). */
	extSong_ = (uint16_t)(titleCode & 0xffff);
	extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);
}

/* Rewrite HOOT's argv so the song file and subsong index follow the title
   code. Songs are catalogued at offset >= 0x10; drivers and samples sit at
   0x8/0x9 and glue at -1, so those tokens are left untouched. */
void CHardPcat::HootSubstArgv(char* tail, int tailCap)
{
	char out[160];
	char* d = out;
	char* dend = out + tailCap - 1;
	if (dend > out + (int)sizeof(out) - 1) dend = out + (int)sizeof(out) - 1;
	const char* s = tail;
	while (*s && d < dend) {
		while (*s == ' ' || *s == '\t') {
			*d++ = *s++;
			if (d >= dend) break;
		}
		const char* tok = s;
		while (*s && *s != ' ' && *s != '\t') s++;
		const int len = (int)(s - tok);
		if (len <= 0 || len >= 96) {
			for (int i = 0; i < len && d < dend; i++) *d++ = tok[i];
			continue;
		}
		char t[96];
		memcpy(t, tok, (size_t)len);
		t[len] = 0;

		const char* rep = NULL;
		char num[8];
		int isNum = 1;
		for (int i = 0; i < len; i++)
			if (t[i] < '0' || t[i] > '9') isNum = 0;
		if (isNum) {
			const unsigned low = (unsigned)(extSong_ & 0xff);
			if (low) {
				_snprintf_s(num, _TRUNCATE, "%u", low);
				rep = num;
			}
		} else {
			for (int i = 0; i < dosGe_->romCount; i++) {
				const CEmuRomEntry* r = &dosGe_->rom[i];
				if (_stricmp(r->type, "file") != 0) continue;
				if (r->offset < 0x10) continue;
				if (_stricmp(r->name, t) != 0) continue;
				rep = dosSong_;
				break;
			}
		}
		const char* w = rep ? rep : t;
		while (*w && d < dend) *d++ = *w++;
	}
	*d = 0;
	strncpy_s(tail, (size_t)tailCap, out, _TRUNCATE);
}

int CHardPcat::RunDosCommand(const char* cmdline, uint64_t budgetCycles, int stopWhenReady)
{
	char stripped[256];
	char name[96];
	char tail[160];
	DosStripHash(cmdline, stripped, (int)sizeof(stripped));
	DosSplitCmd(stripped, name, (int)sizeof(name), tail, (int)sizeof(tail));
	/* Catalog ABI: HOOT's no-GTL token is NULL (not NONE). Normalize argv here
	   so cached catalogs that still say NONE match the guest binary. */
	if (_stricmp(name, "HOOT.EXE") == 0 && tail[0]) {
		char fixed[160];
		const char* s = tail;
		char* d = fixed;
		char* dend = fixed + (int)sizeof(fixed) - 1;
		while (*s && d < dend) {
			if ((_strnicmp(s, "NONE", 4) == 0)
				&& (s[4] == 0 || s[4] == ' ' || s[4] == '\t')
				&& (s == tail || s[-1] == ' ' || s[-1] == '\t')) {
				*d++ = 'N'; *d++ = 'U'; *d++ = 'L'; *d++ = 'L';
				s += 4;
				continue;
			}
			*d++ = *s++;
		}
		*d = 0;
		strncpy_s(tail, fixed, _TRUNCATE);
	}
	/* HOOT argv carries the song file for a few packs (kyrandia, roabod,
	   stuntisland) instead of the NULL placeholder. The catalog writes one
	   fixed name there, so leaving it alone plays that same track for every
	   title code. Songs are the roms at offset >= 0x10; swap such a token for
	   the code-selected song, and a bare number for the code's low byte. */
	if (_stricmp(name, "HOOT.EXE") == 0 && tail[0] && dosGe_ && dosSong_[0])
		HootSubstArgv(tail, (int)sizeof(tail));
	if (_stricmp(name, "HOOT.EXE") == 0) {
		const char* t = tail;
		while (*t == ' ' || *t == '\t') t++;
		if (_strnicmp(t, "NULL", 4) == 0 && (t[4] == 0 || t[4] == ' ' || t[4] == '\t'))
			s_pcatHootNullGtl = 1;
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
	if (!isExe) {
		PatchMokHootIdleCalls(mem, dos_.PspSeg(), imageSize);
		PatchInfogramesMzExtra(mem, dos_.PspSeg(), dos_);
	}

	/* silp play load: mov bl,[1]; add bx,2 → offset 2 after SCI magic 84 00.
	   Rewriting to mov bx,[0] (0x84) makes ADL.DRV BP=6 reject (es:[si]!=0/2). */

	dosStubReady_ = 0;
	silpDrvSeg_ = 0;
	silpSongSeg_ = 0;
	silpSongBytes_ = 0;
	int swordsHold = 0;
	const uint64_t start = cpuCycles_;
	while (cpuCycles_ - start < budgetCycles) {
		RepairSilpDriverFar();
		/* PMD_98 / Sierra silp_at: EXT_STATE=0x81 means ready.
		   Infogrames CODE.COM only installs INT 7Fh then idles (never
		   touches EXT_STATE). Sierra writes 0x80 first, then hooks 7Fh,
		   then 0x81 — do NOT treat 7Fh alone as ready while state is 0x80. */
		/* Sierra: EXT_STATE 0x80 while hooking, then 0x81.
		   HOOT/AIL: often leaves EXT_STATE at 0x80 after INT 7Fh is live —
		   accept 7Fh once the vector has been stable (not still mid-CRT). */
		if (stopWhenReady && stubState_ == 0x81) {
			/* Sierra silp: 0x81 means ready. HOOT also writes 0x81 before
			   finishing DS handle init — only accept if INT 7Fh is not HOOT
			   or HOOT handles are no longer 0/0. */
			int accept = 1;
			if (IvtHooked(0x7F)) {
				uint8_t* hm = np2_mem();
				if (hm) {
					const unsigned o7 = (unsigned)hm[0x7F * 4] | ((unsigned)hm[0x7F * 4 + 1] << 8);
					const unsigned s7 = (unsigned)hm[0x7F * 4 + 2] | ((unsigned)hm[0x7F * 4 + 3] << 8);
					const unsigned l7 = (s7 << 4) + o7;
					int isHoot = 0;
					for (unsigned i = 0; i < 40 && l7 + i + 3 < 0x200000; i++) {
						if (hm[l7 + i] == 0xBA && hm[l7 + i + 1] == 0xE0
							&& hm[l7 + i + 2] == 0x07 && hm[l7 + i + 3] == 0xEC) {
							isHoot = 1;
							break;
						}
					}
					if (isHoot) {
						uint16_t dsH = 0;
						for (unsigned i = 0x18; i < 0x40 && l7 + i + 4 < 0x200000; i++) {
							if (hm[l7 + i] == 0xB8 && hm[l7 + i + 3] == 0x8E && hm[l7 + i + 4] == 0xD8) {
								dsH = (uint16_t)(hm[l7 + i + 1] | (hm[l7 + i + 2] << 8));
								break;
							}
						}
						accept = 0;
						if (dsH) {
							const unsigned db = (unsigned)dsH << 4;
							const uint16_t hs = (uint16_t)(hm[db + 0x43C] | (hm[db + 0x43D] << 8));
							const uint16_t hd = (uint16_t)(hm[db + 0x43E] | (hm[db + 0x43F] << 8));
							if (!(hs == 0 && hd == 0))
								accept = 1;
						}
						if (!accept && (IvtHooked(PCAT_TIMER_VEC) || IvtHooked(0x66)))
							accept = 1;
					}
				}
			}
			if (accept) {
				dosStubReady_ = 1;
				return 1;
			}
		}
		if (stopWhenReady && IvtHooked(0x7F)) {
			if (stubState_ != 0x80) {
				/* HOOT may hook INT 7Fh before writing idle sentinels FFFF into
				   DS:[043C]/[043E]. Returning here leaves hDrv=0 and play does
				   free(0) / abort. Wait until AIL timer is live or handles heal. */
				uint8_t* hm = np2_mem();
				int hootIdle = 0;
				if (hm) {
					const unsigned o7 = (unsigned)hm[0x7F * 4] | ((unsigned)hm[0x7F * 4 + 1] << 8);
					const unsigned s7 = (unsigned)hm[0x7F * 4 + 2] | ((unsigned)hm[0x7F * 4 + 3] << 8);
					const unsigned l7 = (s7 << 4) + o7;
					uint16_t dsH = 0;
					for (unsigned i = 0x18; i < 0x40 && l7 + i + 4 < 0x200000; i++) {
						if (hm[l7 + i] == 0xB8 && hm[l7 + i + 3] == 0x8E && hm[l7 + i + 4] == 0xD8) {
							dsH = (uint16_t)(hm[l7 + i + 1] | (hm[l7 + i + 2] << 8));
							break;
						}
					}
					if (dsH) {
						const unsigned db = (unsigned)dsH << 4;
						const uint16_t hs = (uint16_t)(hm[db + 0x43C] | (hm[db + 0x43D] << 8));
						const uint16_t hd = (uint16_t)(hm[db + 0x43E] | (hm[db + 0x43F] << 8));
						if ((hs == 0xFFFF || hs != 0) && (hd == 0xFFFF || hd != 0))
							hootIdle = 1;
						/* Still 0/0: HOOT mid-init — keep stepping. */
						if (hs == 0 && hd == 0)
							hootIdle = 0;
					}
				}
				if (hootIdle || IvtHooked(PCAT_TIMER_VEC) || IvtHooked(0x66)) {
					dosStubReady_ = 1;
					return 1;
				}
				/* Fall through and keep running HOOT init. */
				} else {
				/* HOOT/AIL leaves EXT_STATE=0x80. Wait until the timer (IRQ0 /
				   INT 8) or AIL (INT 66h) is also hooked — otherwise we stop
				   mid-install and XMI never clocks.
				   Sword of the Samurai CODE.COM hooks 7Fh then far-calls
				   ASOUND init (es:[3Ch]) before INT 18h. INT 8 is already
				   live, so a naive ready-check skips that init (keys=0). */
				uint8_t* sm = np2_mem();
				int swordsInit = 0;
				if (sm) {
					const uint16_t csS = np2_reg_get(NP2_R_CS);
					const uint16_t ipS = np2_reg_get(NP2_R_IP);
					const unsigned physS = ((unsigned)csS << 4) + (unsigned)ipS;
					if (physS + 5 < 0x200000
						&& sm[physS] == 0x2E && sm[physS + 1] == 0x8E
						&& sm[physS + 2] == 0x06 && sm[physS + 3] == 0x49
						&& sm[physS + 4] == 0x03)
						swordsInit = 1;
				}
				if (swordsInit)
					swordsHold = 1;
				if (swordsHold) {
					/* Stay in the pump until ASOUND init returns and
					   CODE.COM writes EXT_STATE=0x81 (then the 0x81
					   branch above accepts). */
				} else if (IvtHooked(PCAT_TIMER_VEC) || IvtHooked(0x66)) {
					dosStubReady_ = 1;
					return 1;
				}
			}
		}
		if (DeliverIrqs())
			continue;
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);
		uint8_t* m = np2_mem();
		const unsigned phys = ((unsigned)cs << 4) + (unsigned)ip;
		if (m && phys < 0x200000 && m[phys] == 0xF4) {
			CEmuDos98Result res = DOS98_CONTINUE;
			if (PcatTrampolineHlt(&dos_, m, cs, ip, &res)) {
				if (res == DOS98_EXEC)
					continue;
				if (res == DOS98_TERMINATED || res == DOS98_RESIDENT) {
					if (g_pcatIpProf) g_pcatIpProf->Freeze();
					/* Buffers a loader frees on the way out are gone by the
					   time the play pump ends, so snapshot here instead. */
					PcatMemDump(m);
					return 1;
				}
				continue;
			}
			const uint64_t q = 200;
			cpuCycles_ += q;
			TickSide(q);
			if (chip_ && cpuHz_ > 0 && oplHz_ > 0) {
				/* Residual: clockmul=8 truncates q*opl/cpu to 0. */
				oplPumpResidual_ += q * (uint64_t)oplHz_;
				uint64_t ot = oplPumpResidual_ / (uint64_t)cpuHz_;
				oplPumpResidual_ %= (uint64_t)cpuHz_;
				if (ot) chip_->AdvanceClocks(ot);
			}
			continue;
		}
		if (g_pcatIpProf)
			g_pcatIpProf->Note(phys);
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		cpuCycles_ += u;
		TickSide(u);
		if (chip_ && cpuHz_ > 0 && oplHz_ > 0) {
			oplPumpResidual_ += u * (uint64_t)oplHz_;
			uint64_t ot = oplPumpResidual_ / (uint64_t)cpuHz_;
			oplPumpResidual_ %= (uint64_t)cpuHz_;
			if (ot) chip_->AdvanceClocks(ot);
		}
	}
	return stubState_ == 0x81 ? 1 : 0;
}

int CHardPcat::BootDos(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	uint8_t* mem = np2_mem();
	if (!mem) return 0;

	modeSilp_ = 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_strnicmp(ge->rom[i].name, "SILP", 4) == 0) {
			modeSilp_ = 1;
			break;
		}
	}

	const int playHz = PCAT_CPU_HZ;
	const int bootHz = playHz * (bootClockMul_ > 1 ? bootClockMul_ : 1);
	cpuHz_ = bootHz;

	np2_reset();
	np2_set_adrsmask(0x000FFFFFu);
	np2_setextsize(0);
	np2_set_v30(0);
	memset(mem, 0, 0xA0000);
	/* Tandy 1000: wibarmat.com does `cmp es:[C000h], 21h` with ES=F000h
	   before enabling SN76496 outs. F000:C000 is outside the 640K clear. */
	if (_stricmp(ge->subtype, "tandy") == 0)
		mem[0xFC000] = 0x21;
	PcatCmosInit();
	PlantAtBiosRom(mem);

	dos_.Reset();
	dos_.InitArena(mem);
	dos_.InstallTrampolines(mem);
	{
		const char* blaster = "BLASTER=A220 I5 D1 H5 T6";
		if (_stricmp(ge->subtype, "adlib") == 0 || _stricmp(ge->subtype, "opl") == 0)
			blaster = NULL;
		else if (_stricmp(ge->subtype, "gameblaster") == 0 || _stricmp(ge->subtype, "cms") == 0)
			blaster = "BLASTER=A220 I5 D1 H5 T6";
		else if (modeMidi_)
			blaster = "BLASTER=A220 I5 D1 H5 T6";
		for (int oi = 0; oi < ge->optCount; oi++) {
			if (_stricmp(ge->opt[oi].name, "blaster") == 0 && ge->opt[oi].value[0]) {
				static char blastBuf[96];
				if (_strnicmp(ge->opt[oi].value, "BLASTER=", 8) == 0)
					strncpy_s(blastBuf, ge->opt[oi].value, _TRUNCATE);
				else
					_snprintf_s(blastBuf, _TRUNCATE, "BLASTER=%s", ge->opt[oi].value);
				blaster = blastBuf;
				break;
			}
		}
		unsigned memKb = 640;
		for (int oi = 0; oi < ge->optCount; oi++) {
			if (_stricmp(ge->opt[oi].name, "memsize") == 0 || _stricmp(ge->opt[oi].name, "memory") == 0) {
				memKb = (unsigned)strtoul(ge->opt[oi].value, NULL, 0);
				if (memKb < 64 || memKb > 640) memKb = 640;
				break;
			}
		}
		dos_.InstallDosStructures(mem, memKb, blaster);
	}
	/* Arm PIT0 during shell boot so AIL can hook INT8 while HOOT installs. */
	picMask_ = (uint8_t)(picMask_ & 0xfeu);
	pit0Reload_ = (uint16_t)(PCAT_PIT_HZ / 18);
	if (pit0Reload_ == 0) pit0Reload_ = 1;
	pit0Counter_ = pit0Reload_;
	pit0Running_ = 1;
	pit0IrqPending_ = 0;
	pit0Residual_ = 0;
	MaterializeDosFiles(fs, ge);
	BindDosRomHandles(ge);
	dosGe_ = ge;
	dosSong_[0] = 0;
	const char* sf = SelectedDosSong(ge, titleCode);
	if (sf) {
		strncpy_s(dosSong_, sf, _TRUNCATE);
		dos_.SetHandle(0x0B, sf);
	}

	extSong_ = (uint16_t)(titleCode & 0xffff);
	extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);
	extCmd_ = 0;
	stubState_ = 0;
	cpuCycles_ = 0;
	oplPumpResidual_ = 0;
	/* Keep IRQ0 live for the shells. TIMER.COM+CMD.COM (kinbaku/kurodan)
	   INT 61 fn5 spins on a tick counter decremented from INT 8; masking
	   here left INT 60 unhooked (dosmiss=int60) and CMDP silent.
	   HOOT/AIL also calibrates against IRQ0 during ADV install.
	   Sierra silp's INT 7Fh loader is not re-entrant with its INT 8 —
	   leave IRQ0 masked for that family (TriggerPlay unmasks after park). */
	picMask_ = modeSilp_ ? (uint8_t)0xff : (uint8_t)0xfe;
	picMasterIcw_ = 0;
	oplWriteCount_ = 0;
	oplKeyOnCount_ = 0;
	silpDrvSeg_ = 0;
	silpSongSeg_ = 0;
	silpSongBytes_ = 0;
	hootAdvSeg_ = 0;
	hootAdvSize_ = 0;
	hootAdvName_[0] = 0;
	hootAdvQuantumOff_ = 0;
	hootAdvIoOff_ = 0;
	hootTimerFixed_ = 0;
	hootAilCs_ = 0;
	s_pcatHootNullGtl = 0;

	const uint64_t setupBudget = (uint64_t)cpuHz_ * 8ull;
	int ranShell = 0;
	int shellTotal = 0;
	for (int i = 0; i < ge->romCount; i++) {
		if (_stricmp(ge->rom[i].type, "shell") == 0)
			shellTotal++;
	}
	/* Run every shell in order (MUSICV → PLAY5_AT, SOUND → CODE, …).
	   Earlier shells only exit on DOS RESIDENT/TERMINATED; the last may
	   stop when EXT_STATE/INT 7Fh look ready. */
	int shellIdx = 0;
	char ranNames[8][96];
	int ranNameCount = 0;
	for (int i = 0; i < ge->romCount; i++) {
		const CEmuRomEntry* r = &ge->rom[i];
		if (_stricmp(r->type, "shell") != 0) continue;
		shellIdx++;
		const int last = (shellIdx >= shellTotal) ? 1 : 0;
		RunDosCommand(r->name, setupBudget, last);
		if (ranNameCount < 8) {
			char nm[96], tl[160];
			DosSplitCmd(r->name, nm, (int)sizeof(nm), tl, (int)sizeof(tl));
			strncpy_s(ranNames[ranNameCount++], nm, _TRUNCATE);
		}
		ranShell = 1;
	}
	/* Sierra / hoot: <rom type="file" offset="-1">silp_at.com</rom> is glue,
	   not a DOS handle — run it when no explicit shells. */
	if (!ranShell || !(dosStubReady_ || stubState_ == 0x81 || IvtHooked(0x7F))) {
		for (int i = 0; i < ge->romCount; i++) {
			const CEmuRomEntry* r = &ge->rom[i];
			if (_stricmp(r->type, "file") != 0) continue;
			if (r->offset >= 0) continue;
			/* A shell already ran this program with its real arguments.
			   Starting a second copy with none makes it fail, exit, and
			   trample the resident state the first copy left behind. */
			int already = 0;
			for (int k = 0; k < ranNameCount; k++)
				if (_stricmp(ranNames[k], r->name) == 0) already = 1;
			if (already) continue;
			RunDosCommand(r->name, setupBudget);
			if (dosStubReady_ || stubState_ == 0x81 || IvtHooked(0x7F))
				break;
		}
	}
	dosStubReady_ = (stubState_ == 0x81 || IvtHooked(0x7F) || IvtHooked(0x66)) ? 1 : dosStubReady_;
	/* Catalog defaults funcvect=0x7E (PMD_98). KOEI FMDRV_AT / Infogrames
	   CODE.COM / PMDL_AT / Sierra silp_at / HOOT.EXE install INT 7Fh instead. */
	if (IvtHooked(0x7F) && !IvtHooked((uint8_t)funcVect_))
		funcVect_ = 0x7F;
	RepairSilpDriverFar();
	PrepHootAilState();
	cpuHz_ = playHz; /* realtime Render stays at base ISA clock */
	return 1;
}

int CHardPcat::LoadRoms(CEmuZipFs* fs, const CEmuGameEntry* ge, unsigned titleCode)
{
	if (!fs || !ge) return 0;
	uint8_t* mem = np2_mem();
	if (!mem) return 0;
	memset(mem, 0, 0x200000);
	dosStubReady_ = 0;
	silpDrvSeg_ = 0;
	silpSongSeg_ = 0;
	silpSongBytes_ = 0;
	silpScanDone_ = 0;
	mokDrvSeg_ = 0;
	hootAdvSeg_ = 0;
	hootAdvSize_ = 0;
	hootAdvName_[0] = 0;
	hootAdvQuantumOff_ = 0;
	hootAdvIoOff_ = 0;
	hootTimerFixed_ = 0;
	hootAilCs_ = 0;
	dos_.SetPcAtBios(1);
	return BootDos(fs, ge, titleCode);
}

int CHardPcat::TriggerPlay(unsigned titleCode)
{
	const uint64_t drainBudget = (uint64_t)cpuHz_ / 2ull;
	PCAT_CENSUS("pre");
	if (dosGe_) {
		/* HOOT CRT may close catalog song handles (0x10+) during boot.
		   INT7F cmd0 then AH=3F BX=title>>8 and reads 0 bytes. */
		BindDosRomHandles(dosGe_);
		BindDosTriggerSong(dosGe_, titleCode);
	}
	else {
		extCmd_ = 0;
		extSong_ = (uint16_t)(titleCode & 0xffff);
		extParam_ = (uint16_t)((titleCode >> 16) & 0xffff);
		if (dosSong_[0]) {
			dos_.SetHandle(0, dosSong_);
			dos_.SetHandle(5, dosSong_);
			dos_.SetHandle(0x0B, dosSong_);
			const unsigned song = titleCode & 0xff;
			if (song < (unsigned)DOS98_HANDLE_MAX)
				dos_.SetHandle((uint16_t)song, dosSong_);
		}
	}
	/* PMD_98 glue: cmd0 then cmd1 (load/play).
	   KOEI FMDRV_AT / Infogrames CODE.COM (INT 7Fh): cmd0=play, cmd2=stop;
	   CODE.COM treats any other cmd as teardown — do not issue cmd1.
	   Sierra silp_at / HOOT.EXE also use INT 7Fh with cmd0=load+play. */
	const int use7f = IvtHooked(0x7F) ? 1 : 0;
	if (use7f)
		funcVect_ = 0x7F;
	const int cmd0Only = (funcVect_ == 0x7F);
	/* Only fire a hooked vector — never INT into the DOS trampoline. */
	const int haveVect = IvtHooked((uint8_t)funcVect_);
	PreloadSilpSong(titleCode);
	if (!modeMidi_ && !use7f)
		PrepHootAilState();
	np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
	extCmd_ = 0;
	RepairSilpDriverFar();
	/* CODE.COM+ROL.DRV play waits for ten BIOS INT 1C ticks after STI.
	   BootDos remasks PIC at the end of setup, so IRQ0 must be live
	   before the drain or that wait never completes.
	   Sierra silp's INT 7Fh loader is not re-entrant with its INT 8 —
	   leave IRQ0 masked until the park below. */
	if (!(use7f && modeSilp_))
		picMask_ = (uint8_t)(picMask_ & 0xfeu);
	/* Neverending Story II CODE.COM AH=42 uses leftover CX (boot AH=3F
	   left 8000h → origin-0 offset 80000000h → EOF) and leftover DX from
	   IN AX,7E2h (seek to 2018). Zeroing here covers CX; dos98 AH=42
	   also treats DX=07E2h as port leftover. Hoot play pokes 7E0/7E2, not CX. */
	if (!modeSilp_) {
		np2_reg_set(NP2_R_CX, 0);
		np2_reg_set(NP2_R_DX, 0);
	}
	{
		uint8_t* mem = np2_mem();
		if (mem && use7f && !modeSilp_ && !modeMidi_) {
			RepairAsoundPlayEnable(mem);
			RepairPacificIslandsSong(mem, extSong_);
		}
	}
	if (haveVect)
		np2_interrupt((uint8_t)funcVect_);
	{
		uint8_t* mem = np2_mem();
		if (mem && use7f && !modeSilp_ && !modeMidi_)
			RepairPacificIslandsSong(mem, extSong_);
	}
	PumpCycles(cpuCycles_ + drainBudget);
	{
		uint8_t* mem = np2_mem();
		if (mem && use7f && !modeSilp_ && !modeMidi_)
			RepairPacificIslandsSong(mem, extSong_);
	}
	if (!cmd0Only) {
		if (dosGe_)
			BindDosTriggerSong(dosGe_, titleCode);
		np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
		extCmd_ = 1;
		RepairSilpDriverFar();
		if (!modeSilp_) {
			np2_reg_set(NP2_R_CX, 0);
			np2_reg_set(NP2_R_DX, 0);
		}
		if (haveVect)
			np2_interrupt((uint8_t)funcVect_);
		PumpCycles(cpuCycles_ + drainBudget);
	}
	RepairMokMidiPlay();
	{
		uint8_t* mem = np2_mem();
		if (mem && use7f && !modeSilp_ && !modeMidi_)
			RepairAsoundPlayEnable(mem);
	}
	PCAT_CENSUS("trig");
	picMask_ = (uint8_t)(picMask_ & 0xfeu);
	if (!pit0Running_) {
		pit0Reload_ = (uint16_t)(PCAT_PIT_HZ / 240);
		if (pit0Reload_ == 0) pit0Reload_ = 1;
		pit0Counter_ = pit0Reload_;
		pit0Running_ = 1;
		pit0IrqPending_ = 0;
		pit0Residual_ = 0;
	}
	if (!modeSilp_)
		PrepHootAilState();
	if (modeMidi_ && !modeSilp_)
		FixHootMidiInt8();
	{
		uint8_t* mem = np2_mem();
		if (mem && !modeMidi_ && !use7f)
			RestoreHootIdleTrampoline(mem);
		/* Park on idle trampoline so IRQ0 can enter cleanly.
		   Sierra silp's ISR is tiny-model SS=DS=CS; other INT 7Fh glues
		   (CODE.COM) keep a normal DOS stack.
		   Do not yank CS/SS if INT 7Fh is still inside the guest — Kingsoft
		   CODE.COM+ROL.DRV waits for ten BIOS INT 1C ticks (~0.55s) before
		   it even reads the .ROL, and 0.5s of drain is not always enough. */
		RestoreHootIdleTrampoline(mem);
		const uint16_t csNow = np2_reg_get(NP2_R_CS);
		const int guestBusy = (csNow >= 0x0100 && csNow != (uint16_t)DOS98_TRAMP_SEG);
		if (use7f && modeSilp_) {
			/* Keep SS=DS=silp — same as IRQ0 path. */
			uint16_t silpCs = (uint16_t)(mem
				? (mem[0x7F * 4 + 2] | (mem[0x7F * 4 + 3] << 8)) : 0);
			if (silpCs >= 0x0100) {
				const uint16_t spSilp = SilpIrqStackSp(mem, silpCs, silpSongSeg_, silpSongBytes_);
				np2_reg_set(NP2_R_SS, silpCs);
				np2_reg_set(NP2_R_SP, spSilp);
				np2_reg_set(NP2_R_DS, silpCs);
				np2_reg_set(NP2_R_ES, silpCs);
			} else {
				np2_reg_set(NP2_R_SS, 0x1000);
				np2_reg_set(NP2_R_SP, 0xFF00);
			}
			np2_reg_set(NP2_R_CS, (uint16_t)DOS98_TRAMP_SEG);
			np2_reg_set(NP2_R_IP, (uint16_t)PCAT_IDLE_IP);
		} else if (!guestBusy) {
			np2_reg_set(NP2_R_SS, 0x1000);
			np2_reg_set(NP2_R_SP, 0xFF00);
			np2_reg_set(NP2_R_CS, (uint16_t)DOS98_TRAMP_SEG);
			np2_reg_set(NP2_R_IP, (uint16_t)PCAT_IDLE_IP);
		}
		np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
	}
	/* HOOT AIL timbres — never after Sierra silp/MT32 play (tears down song).
	   HOOT.EXE uses INT 7Fh, so the old `!use7f` skip never ran NULL-GTL
	   packs. Restrict to argv NULL so already-playing ADV packs (exassault /
	   privateer) are not given a second sequence. */
	if (!modeMidi_ && !modeSilp_ && s_pcatHootNullGtl
		&& (hootAdvSeg_ || hootAdvName_[0])) {
		/* Lost Vikings keys one note during INT 7Fh, so oplKeyOnCount_ is
		   already 1 here. Skipping the GTL install left seq=1 / STOPS.
		   Omar Sharif still has keys=0 at this point; a second 170 is a
		   restart of the same XMI, not a different song. */
		InstallHootAilTimbres();
		FixHootAilTimer();
		if (oplKeyOnCount_ == 0) {
			const uint16_t cs = np2_reg_get(NP2_R_CS);
			if (cs != (uint16_t)DOS98_TRAMP_SEG && cs != 0x0060) {
				uint8_t* mem = np2_mem();
				if (mem) RestoreHootIdleTrampoline(mem);
				np2_reg_set(NP2_R_CS, (uint16_t)DOS98_TRAMP_SEG);
				np2_reg_set(NP2_R_IP, (uint16_t)PCAT_IDLE_IP);
				np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
			}
		}
	}
	return 1;
}

int CHardPcat::FarCallAil(uint16_t api, uint16_t* stackWords, int nWords, uint64_t budget)
{
	uint8_t* mem = np2_mem();
	if (!mem || nWords < 0 || nWords > 16) return 0;
	/* Prefer known AIL CS; INT8 may be HOOT CS:040E (Hanse / Omar Sharif).
	   Calling AIL APIs in HOOT hangs inside the helper and starves IRQ0. */
	uint16_t ailCs = hootAilCs_;
	if (ailCs < 0x1200)
		ailCs = 0;
	if (ailCs && PcatHootInt8Sig(mem, ((unsigned)ailCs << 4) + 0x040Eu)
		&& !PcatSegHasAilApi(mem, ailCs, 151))
		ailCs = 0;
	if (!ailCs || ailCs < 0x0100 || ailCs >= 0xA000 || ailCs == DOS98_TRAMP_SEG) {
		const uint16_t i8Seg = (uint16_t)(mem[0x08 * 4 + 2] | (mem[0x08 * 4 + 3] << 8));
		const uint16_t i8Off = (uint16_t)(mem[0x08 * 4] | (mem[0x08 * 4 + 1] << 8));
		const int i8IsHoot = (i8Off == 0x040E) || PcatHootInt8Sig(mem, ((unsigned)i8Seg << 4) + i8Off);
		if (i8Seg >= 0x0100 && i8Seg < 0xA000 && i8Seg != DOS98_TRAMP_SEG
			&& !i8IsHoot
			&& PcatSegHasAilApi(mem, i8Seg, 150)
			&& PcatSegHasAilApi(mem, i8Seg, 151))
			ailCs = i8Seg;
		else
			ailCs = PcatFindAilApiCs(mem, hootAdvSeg_);
		if (ailCs >= 0x1200 && ailCs < 0xA000 && ailCs != DOS98_TRAMP_SEG)
			hootAilCs_ = ailCs;
	}
	if (!ailCs || ailCs < 0x0100 || ailCs >= 0xA000 || ailCs == DOS98_TRAMP_SEG)
		return 0;
	const unsigned base = (unsigned)ailCs << 4;
	unsigned wrap151 = 0;
	for (unsigned o = 0; o + 4 < 0x8000; o++) {
		if (mem[base + o] == 0xB8 && mem[base + o + 1] == 151
			&& mem[base + o + 2] == 0 && mem[base + o + 3] == 0xE9) {
			wrap151 = o;
			break;
		}
	}
	unsigned wrapOff = 0;
	int foundWrap = 0;
	/* HOOT CS thunks: 151 at ~0CC0h, 150/156/170 sit at fixed deltas from it
	   (file 3640h table). First-match 170 can be a decoy that does not start. */
	if (wrap151) {
		unsigned rel = 0;
		int haveRel = 0;
		if (api == 150) { rel = wrap151 - 6u; haveRel = wrap151 >= 6u; }
		else if (api == 151) { rel = wrap151; haveRel = 1; }
		else if (api == 152) { rel = wrap151 + 0x06u; haveRel = 1; }
		else if (api == 153) { rel = wrap151 + 0x0Cu; haveRel = 1; }
		else if (api == 154) { rel = wrap151 + 0x12u; haveRel = 1; }
		else if (api == 155) { rel = wrap151 + 0x18u; haveRel = 1; }
		else if (api == 156) { rel = wrap151 + 0x1Eu; haveRel = 1; }
		else if (api == 159) { rel = wrap151 + 0x30u; haveRel = 1; }
		else if (api == 170) { rel = wrap151 + 0x36u; haveRel = 1; }
		else if (api == 171) { rel = wrap151 + 0x3Cu; haveRel = 1; }
		else if (api == 174) { rel = wrap151 + 0x48u; haveRel = 1; }
		if (haveRel && rel + 4 < 0x8000
			&& mem[base + rel] == 0xB8 && mem[base + rel + 1] == (uint8_t)(api & 0xff)
			&& mem[base + rel + 2] == (uint8_t)(api >> 8) && mem[base + rel + 3] == 0xE9) {
			wrapOff = rel;
			foundWrap = 1;
		}
	}
	if (!foundWrap) {
		for (unsigned o = 0; o + 5 < 0x8000; o++) {
			if (mem[base + o] == 0xB8 && mem[base + o + 1] == (uint8_t)(api & 0xff)
				&& mem[base + o + 2] == (uint8_t)(api >> 8)
				&& mem[base + o + 3] == 0xE9) {
				wrapOff = o;
				foundWrap = 1;
				break;
			}
		}
	}
	if (!foundWrap) {
		if (api == 151)
			PcatAilTrace("far151 miss cs=%04X\n", ailCs);
		return 0;
	}
	if (api == 151 || api == 170)
		PcatAilTrace("far%u cs=%04X wrap=%04X\n", api, ailCs, wrapOff);
	uint16_t ss = 0x1000, sp = 0xFE00;
	np2_reg_set(NP2_R_SS, ss);
	np2_reg_set(NP2_R_SP, sp);
	for (int i = 0; i < nWords; i++) {
		sp = (uint16_t)(sp - 2);
		const unsigned sl = ((unsigned)ss << 4) + sp;
		mem[sl] = (uint8_t)(stackWords[i] & 0xff);
		mem[sl + 1] = (uint8_t)(stackWords[i] >> 8);
	}
	np2_reg_set(NP2_R_SP, sp);
	sp = (uint16_t)(sp - 4);
	{
		const unsigned sl = ((unsigned)ss << 4) + sp;
		mem[sl] = 0; mem[sl + 1] = 0; mem[sl + 2] = 0x60; mem[sl + 3] = 0;
	}
	np2_reg_set(NP2_R_SP, sp);
	np2_reg_set(NP2_R_CS, ailCs);
	np2_reg_set(NP2_R_IP, (uint16_t)wrapOff);
	np2_reg_set(NP2_R_DS, ailCs);
	np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
	const uint64_t start = cpuCycles_;
	int ok = 0;
	while (cpuCycles_ - start < budget) {
		if (np2_reg_get(NP2_R_CS) == 0x0060 && np2_reg_get(NP2_R_IP) == 0) {
			ok = 1;
			break;
		}
		const int32_t c = np2_step();
		const uint64_t u = (c > 0) ? (uint64_t)c : 1ull;
		cpuCycles_ += u;
		if (chip_ && cpuHz_ > 0 && oplHz_ > 0) {
			oplPumpResidual_ += u * (uint64_t)oplHz_;
			uint64_t ot = oplPumpResidual_ / (uint64_t)cpuHz_;
			oplPumpResidual_ %= (uint64_t)cpuHz_;
			if (ot) chip_->AdvanceClocks(ot);
		}
	}
	if (api == 151)
		PcatAilTrace("far%u done ok=%d ax=%04X cs=%04X ip=%04X\n",
			api, ok, np2_reg_get(NP2_R_AX), np2_reg_get(NP2_R_CS), np2_reg_get(NP2_R_IP));
	return ok;
}

void CHardPcat::InstallHootAilTimbres()
{
	/* Drive guest AIL APIs after INT 7Fh play. HOOT.EXE NULL skips GTL open;
	   XMI TIMB still needs SAMPLE.* via AIL_timbre_request / install_timbre.
	   If HOOT registered the ADV but detect/init failed, finish init here. */
	uint8_t* mem = np2_mem();
	if (!mem || !IvtHooked(0x7F)) return;
	const int isSb = (hootAdvName_[0]
		&& (_strnicmp(hootAdvName_, "SB", 2) == 0
			|| _stricmp(hootAdvName_, "PASOPL.ADV") == 0)) ? 1 : 0;
	const int isMt = (hootAdvName_[0]
		&& (_strnicmp(hootAdvName_, "MT", 2) == 0
			|| strstr(hootAdvName_, "MPU"))) ? 1 : 0;
	const CEmuDos98File* gtl = NULL;
	if (isMt) {
		gtl = dos_.FindFile("SAMPLE.MT");
		if (!gtl) gtl = dos_.FindFile("SAMPLE.OPL");
	} else if (isSb) {
		gtl = dos_.FindFile("SAMPLE.OPL");
		if (!gtl) gtl = dos_.FindFile("SAMPLE.AD");
	} else {
		gtl = dos_.FindFile("SAMPLE.AD");
		if (!gtl) gtl = dos_.FindFile("SAMPLE.OPL");
		if (!gtl) gtl = dos_.FindFile("SAMPLE.MT");
	}
	if (!gtl || !gtl->data || gtl->size < 16)
		return;

	uint16_t dsHoot = 0;
	{
		const unsigned off = (unsigned)mem[0x7F * 4] | ((unsigned)mem[0x7F * 4 + 1] << 8);
		const unsigned seg = (unsigned)mem[0x7F * 4 + 2] | ((unsigned)mem[0x7F * 4 + 3] << 8);
		const unsigned lin = (seg << 4) + off;
		for (unsigned i = 0x10; i < 0x80 && lin + i + 4 < 0x200000; i++) {
			if (mem[lin + i] == 0xB8 && mem[lin + i + 3] == 0x8E && mem[lin + i + 4] == 0xD8) {
				dsHoot = (uint16_t)(mem[lin + i + 1] | (mem[lin + i + 2] << 8));
				break;
			}
			if (mem[lin + i] == 0x8C && mem[lin + i + 1] == 0xC8
				&& mem[lin + i + 2] == 0x8E && mem[lin + i + 3] == 0xD8) {
				dsHoot = (uint16_t)seg;
				break;
			}
		}
		if (!dsHoot && seg >= 0x0100 && seg < 0xA000)
			dsHoot = (uint16_t)seg;
	}
	if (!dsHoot) return;
	const unsigned dbase = (unsigned)dsHoot << 4;
	uint16_t hDrvr = (uint16_t)(mem[dbase + 0x43E] | (mem[dbase + 0x43F] << 8));
	const uint64_t budget = (uint64_t)cpuHz_ / 2ull;
	const uint64_t tbudget = (uint64_t)cpuHz_ / 16ull;
	uint16_t words[8];

	/* Capture AIL CS before register_driver — FarCallAil scans that CS. */
	FixHootAilTimer();
	/* Handle 0 is a valid AIL handle; only FFFF means unused. Battle Chess
	   often never finishes AIL_register_driver (hDrvr stays FFFF). */
	if (hDrvr == 0xFFFF && hootAdvSeg_) {
		words[0] = hootAdvSeg_;
		words[1] = 0;
		if (FarCallAil(100, words, 2, budget)) {
			const uint16_t ax = np2_reg_get(NP2_R_AX);
			if (ax != 0xFFFF) {
				hDrvr = ax;
				mem[dbase + 0x43E] = (uint8_t)(hDrvr & 0xff);
				mem[dbase + 0x43F] = (uint8_t)(hDrvr >> 8);
			}
		}
	}
	if (hDrvr == 0xFFFF) {
		PcatAilTrace("ail song=%s no-hDrvr ds=%04X\n",
			dosSong_[0] ? dosSong_ : "-", dsHoot);
		return;
	}

	/* Ensure detect+init even when HOOT 0383 aborted after register.
	   Skip when INT8 is already HOOT CS:040E — FarCall 101 hits a decoy
	   stub that wipes AIL 151 (Hanse / Omar). */
	{
		const uint16_t i8OffNow = (uint16_t)(mem[0x08 * 4] | (mem[0x08 * 4 + 1] << 8));
		if (i8OffNow != 0x040E) {
			uint16_t ailCs = hootAilCs_ ? hootAilCs_ : (uint16_t)0x1272;
			uint16_t io = 0x388, irq = 0xFFFF, dma = 0xFFFF, drq = 0xFFFF;
			if (hootAdvIoOff_ && hootAdvSeg_) {
				const unsigned ol = ((unsigned)hootAdvSeg_ << 4) + hootAdvIoOff_;
				if (ol + 1 < 0x200000) {
					uint16_t v = (uint16_t)(mem[ol] | (mem[ol + 1] << 8));
					if (v == 0x220 || v == 0x240 || v == 0x388) io = v;
				}
			}
			words[0] = drq; words[1] = dma; words[2] = irq; words[3] = io; words[4] = hDrvr;
			FarCallAil(101, words, 5, budget);
			uint16_t ini = 0;
			const unsigned ab = (unsigned)ailCs << 4;
			for (unsigned o = 0; o + 5 < 0x8000; o++) {
				if (mem[ab + o] == 0xB8 && mem[ab + o + 1] == 102 && mem[ab + o + 2] == 0
					&& mem[ab + o + 3] == 0xE9) {
					ini = (uint16_t)o;
					break;
				}
			}
			if (ini) {
				FarCallAil(102, words, 5, budget);
			} else {
				uint16_t ss = 0x1000, sp = 0xFE00;
				np2_reg_set(NP2_R_SS, ss);
				np2_reg_set(NP2_R_SP, sp);
				for (int i = 0; i < 5; i++) {
					sp = (uint16_t)(sp - 2);
					const unsigned sl = ((unsigned)ss << 4) + sp;
					mem[sl] = (uint8_t)(words[i] & 0xff);
					mem[sl + 1] = (uint8_t)(words[i] >> 8);
				}
				sp = (uint16_t)(sp - 4);
				{
					const unsigned sl = ((unsigned)ss << 4) + sp;
					mem[sl] = 0; mem[sl + 1] = 0; mem[sl + 2] = 0x60; mem[sl + 3] = 0;
				}
				np2_reg_set(NP2_R_SP, sp);
				np2_reg_set(NP2_R_CS, ailCs);
				np2_reg_set(NP2_R_IP, 0x0B83);
				np2_reg_set(NP2_R_DS, ailCs);
				np2_reg_set(NP2_R_FLAGS, (uint16_t)(np2_reg_get(NP2_R_FLAGS) | 0x0200));
				const uint64_t start = cpuCycles_;
				while (cpuCycles_ - start < budget) {
					if (np2_reg_get(NP2_R_CS) == 0x0060 && np2_reg_get(NP2_R_IP) == 0)
						break;
					const int32_t c = np2_step();
					const uint64_t u = (c > 0) ? (uint64_t)c : 1ull;
					cpuCycles_ += u;
					if (chip_ && cpuHz_ > 0 && oplHz_ > 0) {
						oplPumpResidual_ += u * (uint64_t)oplHz_;
						uint64_t ot = oplPumpResidual_ / (uint64_t)cpuHz_;
						oplPumpResidual_ %= (uint64_t)cpuHz_;
						if (ot) chip_->AdvanceClocks(ot);
					}
				}
			}
			FixHootAilTimer();
		}
	}

	uint16_t timb[64];
	int nTimb = 0;
	if (dosSong_[0]) {
		const CEmuDos98File* xf = dos_.FindFile(dosSong_);
		if (xf && xf->data && xf->size >= 12)
			nTimb = PcatXmiTimbList(xf->data, xf->size, timb, 64);
	}
	/* Privateer .ADL and other non-XMI HOOT songs already play via INT 7Fh.
	   151/170 on a dummy FORM restarted them into SILENT/STOPS. */
	if (nTimb <= 0) {
		RestoreHootIdleTrampoline(mem);
		np2_reg_set(NP2_R_CS, (uint16_t)DOS98_TRAMP_SEG);
		np2_reg_set(NP2_R_IP, (uint16_t)PCAT_IDLE_IP);
		return;
	}

	uint16_t hSeqUse = 0xFFFF;
	unsigned xmidLin = 0;
	{
		auto injectSong = [&]() {
			if (!dosSong_[0]) return;
			const CEmuDos98File* xf = dos_.FindFile(dosSong_);
			if (!xf || !xf->data || xf->size < 12) return;
			const uint16_t put = 0x9A00;
			const unsigned n = xf->size < 0x4000u ? xf->size : 0x4000u;
			memcpy(mem + ((unsigned)put << 4), xf->data, n);
			PcatXmiWipeSeqTextMeta(mem + ((unsigned)put << 4), n);
			for (unsigned o = 0; o + 12 < n; o++) {
				if (xf->data[o] == 'F' && xf->data[o + 1] == 'O'
					&& xf->data[o + 2] == 'R' && xf->data[o + 3] == 'M'
					&& xf->data[o + 8] == 'X' && xf->data[o + 9] == 'M'
					&& xf->data[o + 10] == 'I' && xf->data[o + 11] == 'D') {
					xmidLin = ((unsigned)put << 4) + o;
					break;
				}
			}
		};
		auto inAdv = [&](unsigned lin) -> int {
			if (!hootAdvSeg_ || !hootAdvSize_) return 0;
			const unsigned a0 = (unsigned)hootAdvSeg_ << 4;
			return (lin >= a0 && lin < a0 + hootAdvSize_) ? 1 : 0;
		};
		/* NULL-GTL packs often never map the XMI into 0x10000+, so the
		   RAM scan hits a dummy FORM in the ADV and play stays silent. */
		if (s_pcatHootNullGtl)
			injectSong();
		if (!xmidLin) {
			for (unsigned a = 0x10000; a + 12 < 0xA0000; a++) {
				if (mem[a] == 'F' && mem[a + 1] == 'O' && mem[a + 2] == 'R' && mem[a + 3] == 'M'
					&& mem[a + 8] == 'X' && mem[a + 9] == 'M' && mem[a + 10] == 'I' && mem[a + 11] == 'D') {
					if (inAdv(a)) continue;
					xmidLin = a;
					break;
				}
			}
		}
		if (xmidLin && inAdv(xmidLin))
			xmidLin = 0;
		if (!xmidLin)
			injectSong();
		if (xmidLin && !inAdv(xmidLin))
			PcatXmiWipeSeqTextMeta(mem + xmidLin, 0x4000u);
		/* HOOT's own XMI buffer (handle 10) still has the tag; native seq 0
		   uses that copy. */
		if (s_pcatHootNullGtl) {
			for (unsigned a = 0x10000; a + 16 < 0xA0000; a++) {
				if (mem[a] != 0xFF || mem[a + 1] < 1 || mem[a + 1] > 7)
					continue;
				if (inAdv(a)) continue;
				PcatXmiWipeSeqTextMeta(mem + a, 16);
			}
		}
		if (xmidLin) {
			/* AIL 150 in HOOT CS hits a decoy at 0CBAh that does not RETF.
			   520 bytes was under ADLIB's state table (~590+); seq 4 then
			   reports PLAYING with no note-ons (Hanse). Omar keys on seq 0. */
			uint16_t stSize = 2048;
			uint16_t stSeg = 0x8F00;
			uint16_t ctSeg = 0x8E80;
			memset(mem + ((unsigned)stSeg << 4), 0, ((stSize + 15u) / 16u) * 16u);
			memset(mem + ((unsigned)ctSeg << 4), 0, 1024);
			const uint16_t formSeg = (uint16_t)(xmidLin >> 4);
			const uint16_t formOff = (uint16_t)(xmidLin & 0xF);
			words[0] = ctSeg; words[1] = 0;
			words[2] = stSeg; words[3] = 0;
			words[4] = 0;
			words[5] = formSeg; words[6] = formOff;
			words[7] = hDrvr;
			if (FarCallAil(151, words, 8, budget)) {
				const uint16_t ns = np2_reg_get(NP2_R_AX);
				if (ns != 0xFFFF)
					hSeqUse = ns;
			}
		}
	}
	if (hSeqUse == 0xFFFF) {
		const uint16_t exist = (uint16_t)(mem[dbase + 0x43C] | (mem[dbase + 0x43D] << 8));
		if (exist && exist != 0xFFFF)
			hSeqUse = exist;
	}

	words[0] = hDrvr;
	uint16_t tcSize = 4096;
	if (FarCallAil(153, words, 1, budget)) {
		tcSize = np2_reg_get(NP2_R_AX);
		if (tcSize < 16 || tcSize > 0x8000) tcSize = 4096;
	}
	uint16_t tcSeg = 0;
	const uint16_t tcParas = (uint16_t)((tcSize + 15u) / 16u + 1u);
	if (!dos_.AllocBlock(mem, tcParas, &tcSeg) || !tcSeg) {
		tcSeg = 0x9000;
		memset(mem + ((unsigned)tcSeg << 4), 0, tcParas * 16u);
	} else {
		memset(mem + ((unsigned)tcSeg << 4), 0, tcParas * 16u);
	}
	words[0] = tcSize;
	words[1] = tcSeg;
	words[2] = 0;
	words[3] = hDrvr;
	FarCallAil(154, words, 4, budget);

	uint16_t bumpSeg = 0x9200;
	auto loadTimbre = [&](unsigned bank, unsigned patch) -> int {
		unsigned p = 0;
		int foundOff = -1;
		while (p + 6 <= gtl->size) {
			const int pe = (int8_t)gtl->data[p];
			const int be = (int8_t)gtl->data[p + 1];
			const unsigned off = (unsigned)gtl->data[p + 2] | ((unsigned)gtl->data[p + 3] << 8)
				| ((unsigned)gtl->data[p + 4] << 16) | ((unsigned)gtl->data[p + 5] << 24);
			p += 6;
			if (be == -1) break;
			if ((unsigned)be == bank && (unsigned)pe == patch) {
				foundOff = (int)off;
				break;
			}
		}
		if (foundOff < 0 || (unsigned)foundOff + 2 > gtl->size) return 0;
		const unsigned len = (unsigned)gtl->data[foundOff] | ((unsigned)gtl->data[foundOff + 1] << 8);
		if (len < 2 || (unsigned)foundOff + len > gtl->size) return 0;
		const uint16_t need = (uint16_t)((len + 15u) / 16u);
		if ((unsigned)bumpSeg + need >= 0xA000) return 0;
		const uint16_t tSeg = bumpSeg;
		bumpSeg = (uint16_t)(bumpSeg + need);
		memcpy(mem + ((unsigned)tSeg << 4), gtl->data + foundOff, len);
		words[0] = tSeg; words[1] = 0;
		words[2] = (uint16_t)patch;
		words[3] = (uint16_t)bank;
		words[4] = hDrvr;
		FarCallAil(156, words, 5, tbudget);
		return 1;
	};

	int installed = 0;
	for (int i = 0; i < nTimb; i++) {
		if (loadTimbre(timb[i] >> 8, timb[i] & 0xff))
			installed++;
	}
	uint16_t lastTreq = 0xFFFF;
	if (hSeqUse != 0xFFFF) {
		for (int n = 0; n < 32; n++) {
			words[0] = hSeqUse;
			words[1] = hDrvr;
			if (!FarCallAil(155, words, 2, tbudget)) break;
			const uint16_t treq = np2_reg_get(NP2_R_AX);
			if (treq == 0xFFFF) break;
			if (treq == lastTreq) break;
			lastTreq = treq;
			if (loadTimbre(treq >> 8, treq & 0xff))
				installed++;
		}
	}
	if (installed == 0) {
		unsigned p = 0;
		while (p + 6 <= gtl->size && installed < 24) {
			const int pe = (int8_t)gtl->data[p];
			const int be = (int8_t)gtl->data[p + 1];
			p += 6;
			if (be == -1) break;
			if (loadTimbre((unsigned)be, (unsigned)pe))
				installed++;
		}
	}
	(void)installed;
	/* Skip 101/102 when INT8 is already CS:040E (Omar). Hanse takes that
	   path too, so detect never writes the ADV IO word and serve_driver
	   OUTs DX=0. Fill default 388/220 here; a live Omar word is already
	   388 and is left alone. */
	if (hootAdvIoOff_ && hootAdvSeg_ >= 0x0100 && hootAdvSeg_ < 0xA000) {
		const unsigned ol = ((unsigned)hootAdvSeg_ << 4) + hootAdvIoOff_;
		if (ol + 1 < 0x200000) {
			uint16_t v = (uint16_t)(mem[ol] | (mem[ol + 1] << 8));
			if (v != 0x220 && v != 0x240 && v != 0x388) {
				const uint16_t io = isSb ? (uint16_t)0x220 : (uint16_t)0x388;
				mem[ol] = (uint8_t)(io & 0xff);
				mem[ol + 1] = (uint8_t)(io >> 8);
				PcatAilTrace("ail poke io %04X:%04X %04X->%04X\n",
					hootAdvSeg_, hootAdvIoOff_, v, io);
			}
		}
	}
	PcatAilTrace("ail song=%s hDrvr=%04X hSeq=%04X xmid=%05X nTimb=%d inst=%d ailCs=%04X keys=%u wr=%u\n",
		dosSong_[0] ? dosSong_ : "-", hDrvr, hSeqUse, xmidLin, nTimb, installed,
		hootAilCs_, oplKeyOnCount_, oplWriteCount_);
	if (hSeqUse != 0xFFFF) {
		words[0] = hSeqUse;
		words[1] = hDrvr;
		FarCallAil(171, words, 2, budget);
		words[0] = hSeqUse;
		words[1] = hDrvr;
		FarCallAil(170, words, 2, budget);
		words[0] = hSeqUse;
		words[1] = hDrvr;
		if (FarCallAil(174, words, 2, tbudget))
			PcatAilTrace("ail 174 h=%u ax=%04X wr=%u keys=%u\n",
				hSeqUse, np2_reg_get(NP2_R_AX), oplWriteCount_, oplKeyOnCount_);
		mem[dbase + 0x43C] = (uint8_t)(hSeqUse & 0xff);
		mem[dbase + 0x43D] = (uint8_t)(hSeqUse >> 8);
	}
	mem[dbase + 0x43E] = (uint8_t)(hDrvr & 0xff);
	mem[dbase + 0x43F] = (uint8_t)(hDrvr >> 8);
	/* Hanse copies API_timer onto CS:040Eh (`2E FF 06 06 00 FC` / pushes).
	   INT8 already points there — do not plant a second ISR over it. */
	if (oplKeyOnCount_ == 0 && hootAilCs_ >= 0x1200 && hootAilCs_ < 0xA000) {
		const uint16_t i8Off = (uint16_t)(mem[0x08 * 4] | (mem[0x08 * 4 + 1] << 8));
		const unsigned base = (unsigned)hootAilCs_ << 4;
		const int already = PcatApiTimerAt(mem, base + i8Off);
		const unsigned tOff = already ? 0 : ((i8Off == 0x040E || i8Off == 0)
			? PcatFindApiTimerOff(mem, hootAilCs_) : 0);
		if (tOff) {
			mem[0x08 * 4] = (uint8_t)(tOff & 0xff);
			mem[0x08 * 4 + 1] = (uint8_t)(tOff >> 8);
			mem[0x08 * 4 + 2] = (uint8_t)(hootAilCs_ & 0xff);
			mem[0x08 * 4 + 3] = (uint8_t)(hootAilCs_ >> 8);
			mem[base + 0x0E] = 0;
			mem[base + 0x0F] = 0;
			PcatAilTrace("ail plant timer %04X:%04X\n", hootAilCs_, tOff);
		}
	}
	RestoreHootIdleTrampoline(mem);
	np2_reg_set(NP2_R_CS, (uint16_t)DOS98_TRAMP_SEG);
	np2_reg_set(NP2_R_IP, (uint16_t)PCAT_IDLE_IP);
}

void CHardPcat::DrainInterrupt(uint64_t budgetCycles)
{
	const uint16_t idleCs = 0x0060;
	uint64_t start = cpuCycles_;
	while (cpuCycles_ - start < budgetCycles) {
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);
		uint8_t* mem = np2_mem();
		if (cs == idleCs && mem) {
			uint32_t phys = ((uint32_t)cs << 4) + ip;
			if (phys < 0x200000 && mem[phys] == 0xF4)
				break;
		}
		if (DeliverIrqs())
			continue;
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		cpuCycles_ += u;
		TickSide(u);
		if (chip_ && cpuHz_ > 0 && oplHz_ > 0) {
			oplPumpResidual_ += u * (uint64_t)oplHz_;
			uint64_t ot = oplPumpResidual_ / (uint64_t)cpuHz_;
			oplPumpResidual_ %= (uint64_t)cpuHz_;
			if (ot) chip_->AdvanceClocks(ot);
		}
	}
}

void CHardPcat::PumpCycles(uint64_t endCycle)
{
	CEmuHardPcatSetActive(this);
	while (cpuCycles_ < endCycle) {
		uint8_t* mem = np2_mem();
		/* While AIL API_timer is live, only step — TickSide+nested DeliverIrqs
		   mid-ISR trashes AIL's private stack switch and IRET. */
		int inAilIsr = 0;
		if (mem && hootAilCs_ >= 0x0100 && hootAilCs_ < 0xA000) {
			const unsigned base = (unsigned)hootAilCs_ << 4;
			uint16_t re = (uint16_t)(mem[base + 0x0E] | (mem[base + 0x0F] << 8));
			const uint16_t cs0 = np2_reg_get(NP2_R_CS);
			/* Abandoned ISR: back on idle trampoline but re-entry left set. */
			if (re != 0 && cs0 == (uint16_t)DOS98_TRAMP_SEG) {
				mem[base + 0x0E] = 0;
				mem[base + 0x0F] = 0;
				re = 0;
			}
			if (re != 0 || cs0 == hootAilCs_)
				inAilIsr = 1;
		}
		if (!inAilIsr) {
			if (DeliverIrqs())
				continue;
		}
		uint16_t cs = np2_reg_get(NP2_R_CS);
		uint16_t ip = np2_reg_get(NP2_R_IP);
		const unsigned phys = ((unsigned)cs << 4) + (unsigned)ip;
		/* DOS trampoline HLTs are real traps; do not skip 200 cycles — AIL's
		   DDA/XMIDI timing and post-HLT IP (= next opcode) both care. */
		if (!inAilIsr && mem && phys < 0x200000 && mem[phys] == 0xF4) {
			CEmuDos98Result res = DOS98_CONTINUE;
			if (PcatTrampolineHlt(&dos_, mem, cs, ip, &res)) {
				if (res == DOS98_TERMINATED || res == DOS98_RESIDENT)
					return;
				if (res == DOS98_EXEC)
					continue;
				continue;
			}
			/* Batch idle HLT retires in small hops — large skips desync silp. */
			if (cs == (uint16_t)DOS98_TRAMP_SEG && !pit0IrqPending_) {
				uint64_t remain = endCycle - cpuCycles_;
				uint64_t skip = remain > 64ull ? 64ull : remain;
				if (skip > 0) {
					cpuCycles_ += skip;
					TickSide(skip);
					if (chip_ && cpuHz_ > 0 && oplHz_ > 0) {
						oplPumpResidual_ += skip * (uint64_t)oplHz_;
						uint64_t ot = oplPumpResidual_ / (uint64_t)cpuHz_;
						oplPumpResidual_ %= (uint64_t)cpuHz_;
						if (ot) chip_->AdvanceClocks(ot);
					}
					continue;
				}
			}
		}
		if (g_pcatIpProf)
			g_pcatIpProf->Note(phys);
		const int32_t cyc = np2_step();
		const uint64_t u = (cyc > 0) ? (uint64_t)cyc : 1ull;
		cpuCycles_ += u;
		if (!inAilIsr)
			TickSide(u);
		if (chip_ && cpuHz_ > 0 && oplHz_ > 0) {
			oplPumpResidual_ += u * (uint64_t)oplHz_;
			uint64_t ot = oplPumpResidual_ / (uint64_t)cpuHz_;
			oplPumpResidual_ %= (uint64_t)cpuHz_;
			if (ot) chip_->AdvanceClocks(ot);
		}
	}
	PcatMemDump(np2_mem());
}

unsigned CHardPcat::MidiNoteOnCount() const
{
	if (!midiBytes_ || midiCount_ == 0) return 0;
	unsigned noteOns = 0;
	uint8_t run = 0;
	int need = 0, haveD0 = 0;
	uint8_t d0 = 0;
	for (unsigned i = 0; i < midiCount_; i++) {
		const uint8_t v = midiBytes_[i];
		if (v >= 0xf8) continue;
		if (v & 0x80) {
			haveD0 = 0;
			if (v >= 0xf0) { run = 0; need = 0; continue; }
			run = v;
			need = ((v & 0xf0) == 0xc0 || (v & 0xf0) == 0xd0) ? 1 : 2;
			continue;
		}
		if (!run || need <= 0) continue;
		if (need == 1) {
			if ((run & 0xf0) == 0x90 && v > 0) noteOns++;
			continue;
		}
		if (!haveD0) { d0 = v; haveD0 = 1; continue; }
		if ((run & 0xf0) == 0x90 && v > 0) noteOns++;
		haveD0 = 0;
		(void)d0;
	}
	return noteOns;
}

void CHardPcat::MidiCaptureReset()
{
	if (!midiBytes_) midiBytes_ = new uint8_t[CEMU_PCAT_MIDI_CAP];
	if (!midiDelta_) midiDelta_ = new uint32_t[CEMU_PCAT_MIDI_CAP];
	midiCount_ = 0;
	midiLastCycle_ = cpuCycles_;
	/* Keep UART mode if already entered (0x3F). Clearing it mid-session after
	   DOS/MT32 boot made Capture miss note traffic / confuse the driver. */
	mpuRxFull_ = 0;
	mpuAckR_ = mpuAckW_ = 0;
	/* Power-on / reset ACK only when not yet in UART — MT32.DRV waits for
	   bit6 clear (data ready) on 0x331 before the first command. */
	if (!mpuUart_)
		MidiPushAck(0xfe);
}

void CHardPcat::MidiPushAck(uint8_t v)
{
	mpuAckQ_[mpuAckW_ & 7] = v;
	mpuAckW_++;
}

void CHardPcat::MidiCaptureByte(uint8_t v)
{
	if (!midiBytes_ || !midiDelta_) return;
	if (midiCount_ >= (unsigned)CEMU_PCAT_MIDI_CAP) return;
	uint32_t delta = 0;
	if (cpuHz_ > 0) {
		const uint64_t dc = cpuCycles_ - midiLastCycle_;
		/* SMF div 480 @ tempo 500000µs → 960 ticks/sec: ticks = dc * 960 / cpuHz.
		   Before first notes, clamp hard (0.25s) so DOS/MT32 boot stalls do not
		   inflate SMF. After notes / MIDI-out mode: no musical clamp — live inject
		   and export need real note lengths (half/whole, phrase gaps). */
		uint64_t ticks = (dc * 960ull) / ((uint64_t)cpuHz_ + 1ull);
		if (!modeMidi_ && MidiNoteOnCount() == 0) {
			const uint64_t cap = 960ull / 4ull; /* 0.25s boot only */
			if (ticks > cap) ticks = cap;
		} else if (!modeMidi_) {
			const uint64_t cap = 960ull * 8ull;
			if (ticks > cap) ticks = cap;
		}
		/* modeMidi_: leave ticks unclamped (still uint32 delta below). */
		if (ticks > 0xffffffffull) ticks = 0xffffffffull;
		delta = (uint32_t)ticks;
	}
	midiLastCycle_ = cpuCycles_;
	midiDelta_[midiCount_] = delta;
	midiBytes_[midiCount_] = v;
	midiCount_++;
}

void CHardPcat::MidiDataOut(uint8_t data)
{
	/* Capture in UART mode; also accept post-reset traffic once any ACK
	   handshake started (Sierra MT32.DRV may stream after 0x3F). */
	if (mpuUart_ || modeMidi_)
		MidiCaptureByte(data);
}

void CHardPcat::MidiCmdOut(uint8_t data)
{
	if (data == 0xff) {
		mpuUart_ = 0;
		MidiPushAck(0xfe);
		return;
	}
	if (data == 0x3f) {
		mpuUart_ = 1;
		MidiPushAck(0xfe);
		return;
	}
	/* Ignore other intelligent-mode cmds; ACK so detect loops proceed. */
	MidiPushAck(0xfe);
}

uint8_t CHardPcat::MidiStatusIn()
{
	/* MPU-401: bit7=1 write busy; bit6=1 no data to read.
	   Only report DSReady when we actually have an ACK/RX byte.
	   Always returning ready+0xFE made MT32.DRV spin on phantom input
	   and never reach note-ons (capture was init sysex + "THANKS" only). */
	if (mpuAckR_ != mpuAckW_ || mpuRxFull_)
		return 0x00;
	return 0x40;
}

uint8_t CHardPcat::MidiDataIn()
{
	if (mpuAckR_ != mpuAckW_) {
		uint8_t v = mpuAckQ_[mpuAckR_ & 7];
		mpuAckR_++;
		return v;
	}
	if (mpuRxFull_) {
		mpuRxFull_ = 0;
		return mpuRx_;
	}
	return 0xfe;
}

void CHardPcat::MuteAllSound()
{
	/* Clear OPL key-on bits (B0–B8 bit5) so sustain does not hang after stop. */
	if (chip_) {
		for (int ch = 0; ch < 9; ch++) {
			chip_->Write(0, (uint32_t)(0xB0 + ch));
			chip_->Write(1, 0);
		}
	}
	if (saa1_) {
		saa1_->Write(0, 0x14); saa1_->Write(1, 0);
		saa1_->Write(0, 0x1C); saa1_->Write(1, 0);
	}
	if (saa2_) {
		saa2_->Write(0, 0x14); saa2_->Write(1, 0);
		saa2_->Write(0, 0x1C); saa2_->Write(1, 0);
	}
	memset(saaEn_, 0, sizeof(saaEn_));
	port61_ = (uint8_t)(port61_ & ~0x03);
	spkPhaseInc_ = 0;
	for (int i = 0; i < 16; i++)
		FmMonShadowMidiNote(i, 0, 0);
	FmMonShadowFlush(1);
}

void CHardPcat::MixExtra(int16_t* stereo, int frames)
{
	if (!stereo || frames <= 0) return;
	if (sn764_) {
		enum { kChunk = 512 };
		int16_t tmp[kChunk * 2];
		for (int off = 0; off < frames; ) {
			const int n = (frames - off > kChunk) ? kChunk : (frames - off);
			int16_t* dst = stereo + off * 2;
			sn764_->Render(tmp, n);
			for (int i = 0; i < n; i++) {
				int32_t l = (int32_t)dst[i * 2] + tmp[i * 2];
				int32_t r = (int32_t)dst[i * 2 + 1] + tmp[i * 2 + 1];
				if (l > 32767) l = 32767; if (l < -32768) l = -32768;
				if (r > 32767) r = 32767; if (r < -32768) r = -32768;
				dst[i * 2] = (int16_t)l;
				dst[i * 2 + 1] = (int16_t)r;
			}
			off += n;
		}
	}
	if (saa1_ || saa2_) {
		enum { kChunk = 512 };
		int16_t tmp[kChunk * 2];
		for (int off = 0; off < frames; ) {
			const int n = (frames - off > kChunk) ? kChunk : (frames - off);
			int16_t* dst = stereo + off * 2;
			if (saa1_) {
				saa1_->Render(tmp, n);
				for (int i = 0; i < n; i++) {
					int32_t l = (int32_t)dst[i * 2] + tmp[i * 2] / 2;
					int32_t r = (int32_t)dst[i * 2 + 1] + tmp[i * 2 + 1] / 2;
					if (l > 32767) l = 32767; if (l < -32768) l = -32768;
					if (r > 32767) r = 32767; if (r < -32768) r = -32768;
					dst[i * 2] = (int16_t)l;
					dst[i * 2 + 1] = (int16_t)r;
				}
			}
			if (saa2_) {
				saa2_->Render(tmp, n);
				for (int i = 0; i < n; i++) {
					int32_t l = (int32_t)dst[i * 2] + tmp[i * 2] / 2;
					int32_t r = (int32_t)dst[i * 2 + 1] + tmp[i * 2 + 1] / 2;
					if (l > 32767) l = 32767; if (l < -32768) l = -32768;
					if (r > 32767) r = 32767; if (r < -32768) r = -32768;
					dst[i * 2] = (int16_t)l;
					dst[i * 2 + 1] = (int16_t)r;
				}
			}
			off += n;
		}
	}
	/* PC speaker: port61 bit0=gate2, bit1=spkrdata; out = pit2 & spkrdata (MAME). */
	if ((port61_ & 0x03) == 0x03 && spkPhaseInc_ > 0) {
		for (int i = 0; i < frames; i++) {
			spkPhase_ += spkPhaseInc_;
			const int bit = (spkPhase_ >> 31) & 1;
			const int16_t s = bit ? (int16_t)4000 : (int16_t)-4000;
			int32_t l = (int32_t)stereo[i * 2] + s;
			int32_t r = (int32_t)stereo[i * 2 + 1] + s;
			if (l > 32767) l = 32767; if (l < -32768) l = -32768;
			if (r > 32767) r = 32767; if (r < -32768) r = -32768;
			stereo[i * 2] = (int16_t)l;
			stereo[i * 2 + 1] = (int16_t)r;
		}
	}
}

static void SmfPutVar(uint8_t* track, unsigned* tp, uint32_t v)
{
	uint8_t tmp[5];
	int n = 0;
	tmp[n++] = (uint8_t)(v & 0x7f);
	while (v >>= 7) {
		for (int i = n; i > 0; i--) tmp[i] = tmp[i - 1];
		tmp[0] = (uint8_t)(0x80 | (v & 0x7f));
		n++;
	}
	for (int i = 0; i < n; i++)
		track[(*tp)++] = tmp[i];
}

int CHardPcat::ExportCapturedSmf(const wchar_t* path) const
{
	if (!path || !path[0] || midiCount_ < 16 || !midiBytes_ || !midiDelta_) return 0;
	HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;

	/* Locate first/last note; trim at long silence (in-driver loop / idle). */
	uint32_t firstNoteTick = 0, lastNoteTick = 0;
	int sawNote = 0, isMt32 = 0;
	{
		uint32_t tickAt = 0, lastEvtTick = 0;
		uint8_t run = 0;
		int need = 0, haveD0 = 0;
		for (unsigned i = 0; i < midiCount_; i++) {
			tickAt += midiDelta_[i];
			const uint8_t v = midiBytes_[i];
			if (v >= 0xf8) continue;
			if (v & 0x80) {
				haveD0 = 0;
				if (v == 0xf0) {
					/* Peek Roland model id at status+3. */
					if (i + 3 < midiCount_ && midiBytes_[i + 1] == 0x41
						&& midiBytes_[i + 3] == 0x16)
						isMt32 = 1;
					run = 0; need = 0;
					continue;
				}
				if (v >= 0xf0) { run = 0; need = 0; continue; }
				run = v;
				need = ((v & 0xf0) == 0xc0 || (v & 0xf0) == 0xd0) ? 1 : 2;
				continue;
			}
			if (!run || need <= 0) continue;
			if (need == 1 || haveD0) {
				const uint8_t hi = (uint8_t)(run & 0xf0);
				if (hi == 0x90 || hi == 0x80) {
					/* Ignore short rests — SCI/MT-32 phrases often gap >1.5s.
					   Only treat a hush as loop/end after ~45s of music
					   (960 ticks/sec) and ≥4s of silence. */
					const uint32_t minBody = 960u * 45u;
					const uint32_t gapTicks = 960u * 4u;
					if (sawNote && (tickAt - firstNoteTick) >= minBody
						&& tickAt > lastEvtTick + gapTicks) {
						break;
					}
					if (!sawNote) { firstNoteTick = tickAt; sawNote = 1; }
					lastNoteTick = tickAt;
					lastEvtTick = tickAt;
				}
				haveD0 = 0;
			} else {
				haveD0 = 1;
			}
		}
	}

	uint8_t* track = (uint8_t*)malloc(midiCount_ * 10 + 512);
	if (!track) { CloseHandle(h); return 0; }
	unsigned tp = 0;

	SmfPutVar(track, &tp, 0);
	track[tp++] = 0xff; track[tp++] = 0x51; track[tp++] = 0x03;
	track[tp++] = 0x07; track[tp++] = 0xa1; track[tp++] = 0x20;

	/* Sequence/track name: DOS song file (SS032 etc.) so UI/debug match capture. */
	{
		char seqName[64];
		seqName[0] = 0;
		if (dosSong_[0])
			_snprintf_s(seqName, _TRUNCATE, "%s", dosSong_);
		else if (isMt32)
			_snprintf_s(seqName, _TRUNCATE, "MT-32");
		if (seqName[0]) {
			const int n = (int)strlen(seqName);
			SmfPutVar(track, &tp, 0);
			track[tp++] = 0xff; track[tp++] = 0x03;
			track[tp++] = (uint8_t)((n > 32) ? 32 : n);
			for (int i = 0; i < n && i < 32; i++)
				track[tp++] = (uint8_t)seqName[i];
		}
	}

	if (isMt32) {
		/* SC-VA Capital Tone / LA map = Bank MSB 127 (melodic only). */
		for (int ch = 0; ch < 16; ch++) {
			if (ch == 9) continue;
			SmfPutVar(track, &tp, 0);
			track[tp++] = (uint8_t)(0xb0 | ch); track[tp++] = 0; track[tp++] = 127;
			SmfPutVar(track, &tp, 0);
			track[tp++] = (uint8_t)(0xb0 | ch); track[tp++] = 32; track[tp++] = 0;
		}
	}

	/* UART bytes -> complete SMF messages. Raw dump put delta before every
	   0x80+ byte (broke running status / fake statuses -> silent + ~45min). */
	uint32_t pending = 0, tickAt = 0;
	uint8_t run = 0, d0 = 0;
	int need = 0, haveD0 = 0, msgCount = 0;
	int loopStartDone = 0, loopEndDone = 0;
	const int wantLoop = (sawNote && lastNoteTick > firstNoteTick + 480) ? 1 : 0;
	uint8_t chBank[16];
	memset(chBank, 0xff, sizeof(chBank));

	for (unsigned i = 0; i < midiCount_; i++) {
		pending += midiDelta_[i];
		tickAt += midiDelta_[i];
		if (sawNote && tickAt > lastNoteTick + 480u)
			break;
		const uint8_t v = midiBytes_[i];
		if (v >= 0xf8) continue;

		if (v & 0x80) {
			haveD0 = 0;
			if (v == 0xf0) {
				/* Absorb MT-32 SysEx timing into pending; do not write to SMF.
				   Feeding F0 41 xx 16 into SC-VA/GS after Bank127 mutes playback. */
				int mt = 0;
				if (i + 3 < midiCount_ && midiBytes_[i + 1] == 0x41
					&& midiBytes_[i + 3] == 0x16)
					mt = 1;
				for (unsigned j = i + 1; j < midiCount_; j++) {
					pending += midiDelta_[j];
					tickAt += midiDelta_[j];
					const uint8_t b = midiBytes_[j];
					if (b == 0xf7) { i = j; break; }
					if (b >= 0xf8) continue;
					if (j - i > 1024) { i = j; break; }
				}
				if (mt) {
					isMt32 = 1;
					run = 0; need = 0;
					continue;
				}
				/* Non-MT sysex: rewrite from buffered range (rare on this path). */
				SmfPutVar(track, &tp, pending);
				pending = 0;
				track[tp++] = 0xf0;
				/* Already consumed; cannot re-emit easily — skip body. */
				run = 0; need = 0; msgCount++;
				continue;
			}
			if ((v & 0xf0) == 0xf0) {
				if (v == 0xf1 || v == 0xf3) { run = v; need = 1; }
				else if (v == 0xf2) { run = v; need = 2; }
				else { run = 0; need = 0; }
				continue;
			}
			run = v;
			need = ((v & 0xf0) == 0xc0 || (v & 0xf0) == 0xd0) ? 1 : 2;
			continue;
		}

		if (!run || need <= 0) continue;

		uint8_t data[2];
		int nData = 0;
		if (need == 1) {
			data[0] = (uint8_t)(v & 0x7f);
			nData = 1;
		} else if (!haveD0) {
			d0 = (uint8_t)(v & 0x7f);
			haveD0 = 1;
			continue;
		} else {
			data[0] = d0;
			data[1] = (uint8_t)(v & 0x7f);
			nData = 2;
			haveD0 = 0;
		}

		const uint8_t hi = (uint8_t)(run & 0xf0);
		const int ch = (int)(run & 0x0f);
		if (hi == 0xb0 && nData == 2 && data[0] == 0)
			chBank[ch] = data[1];

		if (wantLoop && !loopStartDone && tickAt >= firstNoteTick
			&& (hi == 0x90 || hi == 0x80)) {
			SmfPutVar(track, &tp, pending);
			pending = 0;
			track[tp++] = 0xb0; track[tp++] = 111; track[tp++] = 0;
			loopStartDone = 1;
		}

		/* Ensure LA bank sticks before each Program Change (not rhythm ch). */
		if (isMt32 && hi == 0xc0 && ch != 9 && chBank[ch] != 127) {
			SmfPutVar(track, &tp, pending);
			pending = 0;
			track[tp++] = (uint8_t)(0xb0 | ch); track[tp++] = 0; track[tp++] = 127;
			SmfPutVar(track, &tp, 0);
			track[tp++] = (uint8_t)(0xb0 | ch); track[tp++] = 32; track[tp++] = 0;
			chBank[ch] = 127;
			msgCount += 2;
		}

		SmfPutVar(track, &tp, pending);
		pending = 0;
		track[tp++] = run;
		for (int k = 0; k < nData; k++)
			track[tp++] = data[k];
		msgCount++;

		if (wantLoop && loopStartDone && !loopEndDone && tickAt >= lastNoteTick
			&& (hi == 0x90 || hi == 0x80)) {
			SmfPutVar(track, &tp, 0);
			track[tp++] = 0xb0; track[tp++] = 111; track[tp++] = 127;
			loopEndDone = 1;
		}
	}

	if (wantLoop && loopStartDone && !loopEndDone) {
		SmfPutVar(track, &tp, 0);
		track[tp++] = 0xb0; track[tp++] = 111; track[tp++] = 127;
	}

	if (msgCount < 4) {
		CloseHandle(h);
		free(track);
		DeleteFileW(path);
		return 0;
	}

	SmfPutVar(track, &tp, 0);
	track[tp++] = 0xff; track[tp++] = 0x2f; track[tp++] = 0x00;

	const uint8_t hdr[14] = {
		'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0x01,0xe0
	};
	DWORD wr = 0;
	WriteFile(h, hdr, 14, &wr, NULL);
	uint8_t th[8] = { 'M','T','r','k', 0,0,0,0 };
	th[4] = (uint8_t)((tp >> 24) & 0xff);
	th[5] = (uint8_t)((tp >> 16) & 0xff);
	th[6] = (uint8_t)((tp >> 8) & 0xff);
	th[7] = (uint8_t)(tp & 0xff);
	WriteFile(h, th, 8, &wr, NULL);
	WriteFile(h, track, tp, &wr, NULL);
	CloseHandle(h);
	free(track);
	return (wr == tp) ? 1 : 0;
}
