/* NEC V35 (uPD70136) core — see v35core.h for scope.

   Instruction semantics and the on-chip peripheral behaviour follow MAME's
   src/devices/cpu/nec (nec.cpp / v25.cpp / v25sfr.cpp, BSD-3-Clause, Bryan
   McPhail and Alex W. Jackson); the code here is an independent C
   implementation over CEmu's own bus callbacks rather than a device_t port. */

#include "v35core.h"
#include <stdlib.h>
#include <string.h>

/* ---- register file layout (matches the on-chip bank image) --------------
   Each of the eight banks is 32 bytes of the 256-byte internal RAM. Word
   indices are relative to the bank base; byte indices address the same store
   through the low-endian overlay, exactly as the hardware does. */
enum {
	R_VECTOR_PC = 0x02 / 2,
	R_PSW_SAVE = 0x04 / 2,
	R_PC_SAVE = 0x06 / 2,
	S_DS0 = 0x08 / 2,
	S_SS = 0x0a / 2,
	S_PS = 0x0c / 2,
	S_DS1 = 0x0e / 2,
	R_IY = 0x10 / 2,
	R_IX = 0x12 / 2,
	R_BP = 0x14 / 2,
	R_SP = 0x16 / 2,
	R_BW = 0x18 / 2,
	R_DW = 0x1a / 2,
	R_CW = 0x1c / 2,
	R_AW = 0x1e / 2
};

enum {
	B_AL = 0x1e, B_AH = 0x1f,
	B_CL = 0x1c, B_CH = 0x1d,
	B_DL = 0x1a, B_DH = 0x1b,
	B_BL = 0x18, B_BH = 0x19
};

/* interrupt sources (bit masks) */
enum {
	SRC_BRK = 0,
	SRC_INT = 1u << 0,
	SRC_NMI = 1u << 1,
	SRC_INTTU0 = 1u << 2,
	SRC_INTTU1 = 1u << 3,
	SRC_INTTU2 = 1u << 4,
	SRC_INTD0 = 1u << 5,
	SRC_INTD1 = 1u << 6,
	SRC_INTP0 = 1u << 7,
	SRC_INTP1 = 1u << 8,
	SRC_INTP2 = 1u << 9,
	SRC_INTTB = 1u << 16,
	SRC_BRKN = 1u << 17,
	SRC_BRKS = 1u << 18
};

enum {
	VEC_DIVIDE = 0,
	VEC_TRAP = 1,
	VEC_NMI = 2,
	VEC_BRKV = 4,
	VEC_CHKIND = 5,
	VEC_INTD0 = 20,
	VEC_INTD1 = 21,
	VEC_INTP0 = 24,
	VEC_INTP1 = 25,
	VEC_INTP2 = 26,
	VEC_INTTU0 = 28,
	VEC_INTTU1 = 29,
	VEC_INTTU2 = 30,
	VEC_INTTB = 31
};

enum { REP_NONE = 0, REP_NZ, REP_Z, REP_NC, REP_C };

struct V35Cpu {
	uint16_t ram[128];   /* internal RAM = the eight register banks */
	uint8_t sfr[0x100];  /* shadow for SFRs without dedicated state */
	uint32_t rbw, rbb;   /* current bank base, word / byte indexed */
	uint16_t ip;

	int32_t carry, aux, over; /* non-zero = set */
	int32_t sign;             /* < 0 = set */
	int32_t zero;             /* == 0 = set */
	int32_t parity;           /* low byte feeds the parity table */
	uint8_t tf, iflag, df, mf, modeState, ibrk, f0, f1;

	uint32_t idb;   /* SFR page base, 0xffe00 after reset */
	uint8_t ramen;

	uint32_t pendingIrq, unmaskedIrq, macroService, bankswitchIrq;
	uint8_t ispr, irqs;
	uint8_t priInttu, priIntd, priIntp, priInts0, priInts1;
	uint8_t intm, ems[3];
	uint8_t nmiState, intpState[3];
	/* After INTP FINT the Irem Rev3.40 ISR does STI before IRET; a still-
	   asserted YM2151 pin would nest and corrupt song setup. Hold INTP0
	   pending until RETI/RETRBI (same effect as the old STI→CLI ROM patch). */
	uint8_t intp0HoldIret;
	uint8_t halted, noInterrupt;
	uint8_t rfm;
	uint16_t wtc;

	/* timer unit */
	uint8_t tb, pck, tmc0, tmc1;
	uint16_t tm0, md0, tm1, md1;
	int32_t tbCount, t0Count, t1Count, t2Count;
	int32_t tbPeriod, t0Period, t1Period, t2Period;

	int segPrefix; /* 0 = none, else 1 + segment index */
	uint32_t prefixBase;
	int repMode;
	uint32_t ea;
	uint16_t eo;

	int64_t icount;
	const uint8_t* decrypt;

	void* ctx;
	V35ReadFn read;
	V35WriteFn write;
	V35InFn in;
	V35OutFn out;

	uint32_t badOps, lastBadOp, irqCount;
	uint32_t irqSrcCount[16]; /* diagnostic: which ExternalInt sources fire */
	uint32_t irqBankSwitch, irqClassic;
};

static uint8_t kParity[256];
static int kParityReady;

static const uint8_t kWregIdx[8] = {
	R_AW, R_CW, R_DW, R_BW, R_SP, R_BP, R_IX, R_IY
};
static const uint8_t kBregIdx[8] = {
	B_AL, B_CL, B_DL, B_BL, B_AH, B_CH, B_DH, B_BH
};

#define W(i)      (c->ram[c->rbw + (i)])
#define SREG(i)   (c->ram[c->rbw + (i)])
#define B(i)      (((uint8_t*)c->ram)[c->rbb + (i)])
#define REGW(r)   W(kWregIdx[(r)])
#define REGB(r)   B(kBregIdx[(r)])

#define CF  (c->carry != 0)
#define SF  (c->sign < 0)
#define ZF  (c->zero == 0)
#define PF  (kParity[(uint8_t)c->parity])
#define AF  (c->aux != 0)
#define OF  (c->over != 0)
#define RBN ((c->rbw) >> 4)

#define CLK(n) (c->icount -= (n))

static void SetRB(V35Cpu* c, unsigned bank)
{
	c->rbw = (bank & 7u) << 4;
	c->rbb = (bank & 7u) << 5;
}

/* ---- flag helpers ------------------------------------------------------ */

#define SetCFB(x) (c->carry = (int32_t)((x) & 0x100))
#define SetCFW(x) (c->carry = (int32_t)((x) & 0x10000))
#define SetAFv(x, y, z) (c->aux = (int32_t)(((x) ^ ((y) ^ (z))) & 0x10))
#define SetSZPB(x) (c->sign = c->zero = c->parity = (int8_t)(x))
#define SetSZPW(x) (c->sign = c->zero = c->parity = (int16_t)(x))
#define SetOFB_Add(x, y, z) (c->over = (int32_t)(((x) ^ (y)) & ((x) ^ (z)) & 0x80))
#define SetOFW_Add(x, y, z) (c->over = (int32_t)(((x) ^ (y)) & ((x) ^ (z)) & 0x8000))
#define SetOFB_Sub(x, y, z) (c->over = (int32_t)(((z) ^ (y)) & ((z) ^ (x)) & 0x80))
#define SetOFW_Sub(x, y, z) (c->over = (int32_t)(((z) ^ (y)) & ((z) ^ (x)) & 0x8000))

static uint16_t CompressFlags(const V35Cpu* c)
{
	return (uint16_t)((c->carry != 0)
		| (c->ibrk << 1)
		| (kParity[(uint8_t)c->parity] << 2)
		| (c->f0 << 3)
		| ((c->aux != 0) << 4)
		| (c->f1 << 5)
		| ((c->zero == 0) << 6)
		| ((c->sign < 0) << 7)
		| (c->tf << 8)
		| (c->iflag << 9)
		| (c->df << 10)
		| ((c->over != 0) << 11)
		| (RBN << 12)
		| (c->mf << 15));
}

static void ExpandFlags(V35Cpu* c, uint16_t f)
{
	c->carry = f & 0x0001;
	c->ibrk = (f & 0x0002) ? 1 : 0;
	c->parity = !(f & 0x0004);
	c->f0 = (f & 0x0008) ? 1 : 0;
	c->aux = f & 0x0010;
	c->f1 = (f & 0x0020) ? 1 : 0;
	c->zero = !(f & 0x0040);
	c->sign = (f & 0x0080) ? -1 : 0;
	c->tf = (f & 0x0100) ? 1 : 0;
	c->iflag = (f & 0x0200) ? 1 : 0;
	c->df = (f & 0x0400) ? 1 : 0;
	c->over = f & 0x0800;
	c->mf = (f & 0x8000) ? 1 : 0;
}

/* ---- SFR page ---------------------------------------------------------- */

static uint8_t IrqCtlRead(const V35Cpu* c, uint32_t source, uint8_t priority)
{
	return (uint8_t)(((c->pendingIrq & source) ? 0x80 : 0x00)
		| ((c->unmaskedIrq & source) ? 0x00 : 0x40)
		| ((c->macroService & source) ? 0x20 : 0x00)
		| ((c->bankswitchIrq & source) ? 0x10 : 0x00)
		| priority);
}

static void IrqCtlWrite(V35Cpu* c, uint32_t source, uint8_t d)
{
	if (d & 0x80) c->pendingIrq |= source;
	else c->pendingIrq &= ~source;
	if (d & 0x40) c->unmaskedIrq &= ~source;
	else c->unmaskedIrq |= source;
	if (d & 0x20) c->macroService |= source;
	else c->macroService &= ~source;
	if (d & 0x10) c->bankswitchIrq |= source;
	else c->bankswitchIrq &= ~source;
	/* Unmasking must re-sample the external pin. A write of 0x00 clears the
	   software pending bit and would otherwise drop a latch that was already
	   asserted before the IMC enabled INTP1 (Irem M92 sound start). */
	if (!(d & 0x40)) {
		if (source == SRC_INTP0 && c->intpState[0] && !c->intp0HoldIret)
			c->pendingIrq |= SRC_INTP0;
		if (source == SRC_INTP1 && c->intpState[1]) c->pendingIrq |= SRC_INTP1;
		if (source == SRC_INTP2 && c->intpState[2]) c->pendingIrq |= SRC_INTP2;
	}
}

static void TimerReload(V35Cpu* c)
{
	/* MAME v25sfr tmc0_w / tmc1_w, expressed as clock counters instead of
	   attotime so the core stays free of a scheduler. */
	c->t0Period = c->t1Period = c->t2Period = 0;
	if (c->tmc0 & 0x01) { /* one-shot */
		if ((c->tmc0 & 0x80) && c->tm0)
			c->t0Period = (int32_t)(c->pck * c->tm0 * ((c->tmc0 & 0x40) ? 128 : 12));
		if ((c->tmc0 & 0x20) && c->md0)
			c->t1Period = (int32_t)(c->pck * c->md0 * ((c->tmc0 & 0x10) ? 128 : 12));
	} else { /* interval */
		if ((c->tmc0 & 0x80) && c->md0) {
			c->t0Period = (int32_t)(c->pck * c->md0 * ((c->tmc0 & 0x40) ? 128 : 6));
			c->tm0 = c->md0;
		}
	}
	if ((c->tmc1 & 0x80) && c->md1) {
		c->t2Period = (int32_t)(c->pck * c->md1 * ((c->tmc1 & 0x40) ? 128 : 6));
		c->tm1 = c->md1;
	}
	c->t0Count = c->t0Period;
	c->t1Count = c->t1Period;
	c->t2Count = c->t2Period;
}

static uint8_t SfrRead(V35Cpu* c, uint32_t o)
{
	if (o < 0x100)
		return ((uint8_t*)c->ram)[o];
	switch (o) {
	case 0x108: /* P1 doubles as the interrupt pin state, active low */
		return (uint8_t)(0xf0
			| (c->nmiState ? 0x00 : 0x01)
			| (c->intpState[0] ? 0x00 : 0x02)
			| (c->intpState[1] ? 0x00 : 0x04)
			| (c->intpState[2] ? 0x00 : 0x08));
	case 0x140: return c->intm;
	case 0x144: case 0x145: case 0x146: return c->ems[o - 0x144];
	case 0x14c: return IrqCtlRead(c, SRC_INTP0, c->priIntp);
	case 0x14d: return IrqCtlRead(c, SRC_INTP1, 7);
	case 0x14e: return IrqCtlRead(c, SRC_INTP2, 7);
	case 0x180: return (uint8_t)c->tm0;
	case 0x181: return (uint8_t)(c->tm0 >> 8);
	case 0x182: return (uint8_t)c->md0;
	case 0x183: return (uint8_t)(c->md0 >> 8);
	case 0x188: return (uint8_t)c->tm1;
	case 0x189: return (uint8_t)(c->tm1 >> 8);
	case 0x18a: return (uint8_t)c->md1;
	case 0x18b: return (uint8_t)(c->md1 >> 8);
	case 0x19c: return IrqCtlRead(c, SRC_INTTU0, c->priInttu);
	case 0x19d: return IrqCtlRead(c, SRC_INTTU1, 7);
	case 0x19e: return IrqCtlRead(c, SRC_INTTU2, 7);
	case 0x1ac: return IrqCtlRead(c, SRC_INTD0, c->priIntd);
	case 0x1ad: return IrqCtlRead(c, SRC_INTD1, 7);
	case 0x1e1: return c->rfm;
	case 0x1e8: return (uint8_t)c->wtc;
	case 0x1e9: return (uint8_t)(c->wtc >> 8);
	case 0x1ea: return (uint8_t)((c->f0 << 3) | (c->f1 << 5));
	case 0x1eb: {
		uint8_t r = c->ramen ? 0x40 : 0x00;
		if (c->tb == 13) r |= 0x04;
		else if (c->tb == 16) r |= 0x08;
		else if (c->tb == 20) r |= 0x0c;
		if (c->pck == 4) r |= 0x01;
		else if (c->pck == 8) r |= 0x02;
		return r;
	}
	case 0x1ec: return IrqCtlRead(c, SRC_INTTB, 7);
	case 0x1ef: return c->irqs;
	case 0x1fc: return c->ispr;
	case 0x1ff: return (uint8_t)(c->idb >> 12);
	default: break;
	}
	/* Ports and everything unmodelled read back as written / all-ones. */
	return c->sfr[o - 0x100];
}

static void SfrWrite(V35Cpu* c, uint32_t o, uint8_t d)
{
	if (o < 0x100) {
		((uint8_t*)c->ram)[o] = d;
		return;
	}
	c->sfr[o - 0x100] = d;
	switch (o) {
	case 0x140: c->intm = d & 0x55; break;
	case 0x144: case 0x145: case 0x146: c->ems[o - 0x144] = d & 0xf7; break;
	case 0x14c: IrqCtlWrite(c, SRC_INTP0, d); c->priIntp = d & 7; break;
	case 0x14d: IrqCtlWrite(c, SRC_INTP1, d); break;
	case 0x14e: IrqCtlWrite(c, SRC_INTP2, d); break;
	case 0x180: c->tm0 = (uint16_t)((c->tm0 & 0xff00) | d); break;
	case 0x181: c->tm0 = (uint16_t)((c->tm0 & 0x00ff) | (d << 8)); break;
	case 0x182: c->md0 = (uint16_t)((c->md0 & 0xff00) | d); break;
	case 0x183: c->md0 = (uint16_t)((c->md0 & 0x00ff) | (d << 8)); break;
	case 0x188: c->tm1 = (uint16_t)((c->tm1 & 0xff00) | d); break;
	case 0x189: c->tm1 = (uint16_t)((c->tm1 & 0x00ff) | (d << 8)); break;
	case 0x18a: c->md1 = (uint16_t)((c->md1 & 0xff00) | d); break;
	case 0x18b: c->md1 = (uint16_t)((c->md1 & 0x00ff) | (d << 8)); TimerReload(c); break;
	case 0x190: c->tmc0 = d; TimerReload(c); break;
	case 0x191: c->tmc1 = d & 0xc0; TimerReload(c); break;
	case 0x19c: IrqCtlWrite(c, SRC_INTTU0, d); c->priInttu = d & 7; break;
	case 0x19d: IrqCtlWrite(c, SRC_INTTU1, d); break;
	case 0x19e: IrqCtlWrite(c, SRC_INTTU2, d); break;
	case 0x1ac: IrqCtlWrite(c, SRC_INTD0, d); c->priIntd = d & 7; break;
	case 0x1ad: IrqCtlWrite(c, SRC_INTD1, d); break;
	case 0x1e1: c->rfm = d; break;
	case 0x1e8: c->wtc = (uint16_t)((c->wtc & 0xff00) | d); break;
	case 0x1e9: c->wtc = (uint16_t)((c->wtc & 0x00ff) | (d << 8)); break;
	case 0x1ea: c->f0 = (d >> 3) & 1; c->f1 = (d >> 5) & 1; break;
	case 0x1eb: {
		static const uint8_t kTb[4] = { 10, 13, 16, 20 };
		static const uint8_t kPck[4] = { 2, 4, 8, 8 };
		c->ramen = (d & 0x40) ? 1 : 0;
		c->tb = kTb[(d >> 2) & 3];
		c->pck = kPck[d & 3];
		c->tbPeriod = (int32_t)((uint32_t)c->pck << c->tb);
		c->tbCount = c->tbPeriod;
		TimerReload(c);
		break;
	}
	case 0x1ec: IrqCtlWrite(c, SRC_INTTB, (uint8_t)(d & 0xc0)); break;
	case 0x1ff: c->idb = ((uint32_t)d << 12) | 0xe00u; break;
	default: break;
	}
}

/* ---- bus --------------------------------------------------------------- */

static int IsInternal(const V35Cpu* c, uint32_t a)
{
	return (((a & 0xffe00u) == c->idb && (c->ramen || (a & 0x100u))) || a == 0xfffffu);
}

static uint8_t MemRead8(V35Cpu* c, uint32_t a)
{
	a &= 0xfffffu;
	if (IsInternal(c, a)) return SfrRead(c, a & 0x1ffu);
	return c->read ? c->read(c->ctx, a) : 0xff;
}

static void MemWrite8(V35Cpu* c, uint32_t a, uint8_t d)
{
	a &= 0xfffffu;
	if (IsInternal(c, a)) { SfrWrite(c, a & 0x1ffu, d); return; }
	if (c->write) c->write(c->ctx, a, d);
}

static uint16_t MemRead16(V35Cpu* c, uint32_t a)
{
	return (uint16_t)(MemRead8(c, a) | ((uint16_t)MemRead8(c, a + 1) << 8));
}

static void MemWrite16(V35Cpu* c, uint32_t a, uint16_t d)
{
	MemWrite8(c, a, (uint8_t)d);
	MemWrite8(c, a + 1, (uint8_t)(d >> 8));
}

static uint8_t PortIn8(V35Cpu* c, uint16_t p)
{
	return c->in ? c->in(c->ctx, p) : 0xff;
}

static void PortOut8(V35Cpu* c, uint16_t p, uint8_t d)
{
	if (c->out) c->out(c->ctx, p, d);
}

/* Program fetches bypass the internal RAM/SFR window — on real silicon the
   prefetcher is wired straight to the external bus, which is what puts the
   reset vector mirror at FFFF0 inside the IDB page within reach. */
static uint8_t Fetch8(V35Cpu* c)
{
	const uint32_t a = (((uint32_t)SREG(S_PS) << 4) + c->ip) & 0xfffffu;
	c->ip++;
	return c->read ? c->read(c->ctx, a) : 0xff;
}

static uint16_t Fetch16(V35Cpu* c)
{
	uint16_t r = Fetch8(c);
	r = (uint16_t)(r | ((uint16_t)Fetch8(c) << 8));
	return r;
}

static uint8_t FetchOp(V35Cpu* c)
{
	uint8_t r = Fetch8(c);
	if (c->mf == 0 && c->decrypt)
		r = c->decrypt[r];
	return r;
}

/* ---- stack / control transfer ------------------------------------------ */

static void Push(V35Cpu* c, uint16_t v)
{
	W(R_SP) = (uint16_t)(W(R_SP) - 2);
	MemWrite16(c, ((uint32_t)SREG(S_SS) << 4) + W(R_SP), v);
}

static uint16_t Pop(V35Cpu* c)
{
	const uint16_t v = MemRead16(c, ((uint32_t)SREG(S_SS) << 4) + W(R_SP));
	W(R_SP) = (uint16_t)(W(R_SP) + 2);
	return v;
}

static void Interrupt(V35Cpu* c, unsigned vec, uint32_t source)
{
	Push(c, CompressFlags(c));
	c->tf = 0;
	c->iflag = 0;
	c->mf = c->modeState;
	if (source == SRC_BRKN) c->mf = 1;
	else if (source == SRC_BRKS) c->mf = c->decrypt ? 0 : 1;
	{
		const uint16_t off = MemRead16(c, vec * 4);
		const uint16_t seg = MemRead16(c, vec * 4 + 2);
		Push(c, SREG(S_PS));
		Push(c, c->ip);
		c->ip = off;
		SREG(S_PS) = seg;
	}
	c->irqCount++;
}

static void BankSwitch(V35Cpu* c, unsigned bank)
{
	const uint16_t psw = CompressFlags(c);
	const uint16_t pc = c->ip;
	c->tf = 0;
	c->iflag = 0;
	c->mf = c->modeState;
	SetRB(c, bank);
	W(R_PSW_SAVE) = psw;
	W(R_PC_SAVE) = pc;
	c->ip = W(R_VECTOR_PC);
	c->irqCount++;
}

static void ExternalInt(V35Cpu* c)
{
	const uint32_t sources = SRC_INTTU0 | SRC_INTTU1 | SRC_INTTU2 | SRC_INTD0
		| SRC_INTD1 | SRC_INTP0 | SRC_INTP1 | SRC_INTP2 | SRC_INTTB;
	const uint32_t pending = c->pendingIrq & c->unmaskedIrq;

	if (pending & SRC_NMI) {
		Interrupt(c, VEC_NMI, SRC_NMI);
		c->pendingIrq &= ~SRC_NMI;
		return;
	}
	if (!(pending & sources))
		return;

	{
		uint32_t source = 0;
		unsigned vector = 0;
		int i = -1;
		while (++i < 8) {
			if (c->ispr & (1 << i)) break;
			if (c->priInttu == i) {
				if (pending & SRC_INTTU0) { source = SRC_INTTU0; vector = VEC_INTTU0; break; }
				if (pending & SRC_INTTU1) { source = SRC_INTTU1; vector = VEC_INTTU1; break; }
				if (pending & SRC_INTTU2) { source = SRC_INTTU2; vector = VEC_INTTU2; break; }
			}
			if (c->priIntd == i) {
				if (pending & SRC_INTD0) { source = SRC_INTD0; vector = VEC_INTD0; break; }
				if (pending & SRC_INTD1) { source = SRC_INTD1; vector = VEC_INTD1; break; }
			}
			if (c->priIntp == i) {
				/* Prefer the sound latch (INTP1) over the YM2151 timer (INTP0)
				   when both are pending — otherwise a free-running Timer A/B
				   starves command delivery on Irem M92. */
				if (pending & SRC_INTP1) { source = SRC_INTP1; vector = VEC_INTP1; break; }
				if (pending & SRC_INTP0) { source = SRC_INTP0; vector = VEC_INTP0; break; }
				if (pending & SRC_INTP2) { source = SRC_INTP2; vector = VEC_INTP2; break; }
			}
			if (i == 7 && (pending & SRC_INTTB)) { source = SRC_INTTB; vector = VEC_INTTB; break; }
		}
		if (!source) return;
		c->pendingIrq &= ~source;
		c->irqs = (uint8_t)vector;
		c->ispr |= (uint8_t)(1 << i);
		{
			unsigned si = 0;
			uint32_t bit = source;
			while (bit > 1u) { bit >>= 1; si++; }
			if (si < 16) c->irqSrcCount[si]++;
		}
		if (c->bankswitchIrq & source) {
			c->irqBankSwitch++;
			BankSwitch(c, (unsigned)i);
		} else {
			c->irqClassic++;
			Interrupt(c, vector, source);
		}
	}
}

/* ---- effective address ------------------------------------------------- */

static uint32_t DefaultBase(V35Cpu* c, int seg)
{
	if (c->segPrefix && (seg == S_DS0 || seg == S_SS))
		return c->prefixBase;
	return (uint32_t)SREG(seg) << 4;
}

static void GetEA(V35Cpu* c, uint8_t modrm)
{
	uint16_t off = 0;
	int seg = S_DS0;
	switch (modrm & 7) {
	case 0: off = (uint16_t)(W(R_BW) + W(R_IX)); break;
	case 1: off = (uint16_t)(W(R_BW) + W(R_IY)); break;
	case 2: off = (uint16_t)(W(R_BP) + W(R_IX)); seg = S_SS; break;
	case 3: off = (uint16_t)(W(R_BP) + W(R_IY)); seg = S_SS; break;
	case 4: off = W(R_IX); break;
	case 5: off = W(R_IY); break;
	case 6:
		if ((modrm & 0xc0) == 0) off = Fetch16(c);
		else { off = W(R_BP); seg = S_SS; }
		break;
	default: off = W(R_BW); break;
	}
	if ((modrm & 0xc0) == 0x40) off = (uint16_t)(off + (int8_t)Fetch8(c));
	else if ((modrm & 0xc0) == 0x80) off = (uint16_t)(off + Fetch16(c));
	c->eo = off;
	c->ea = (DefaultBase(c, seg) + off) & 0xfffffu;
	CLK(2);
}

static uint8_t GetRMB(V35Cpu* c, uint8_t modrm)
{
	if (modrm >= 0xc0) return REGB(modrm & 7);
	GetEA(c, modrm);
	return MemRead8(c, c->ea);
}

static uint16_t GetRMW(V35Cpu* c, uint8_t modrm)
{
	if (modrm >= 0xc0) return REGW(modrm & 7);
	GetEA(c, modrm);
	return MemRead16(c, c->ea);
}

static void PutbackRMB(V35Cpu* c, uint8_t modrm, uint8_t v)
{
	if (modrm >= 0xc0) REGB(modrm & 7) = v;
	else MemWrite8(c, c->ea, v);
}

static void PutbackRMW(V35Cpu* c, uint8_t modrm, uint16_t v)
{
	if (modrm >= 0xc0) REGW(modrm & 7) = v;
	else MemWrite16(c, c->ea, v);
}

static void PutRMB(V35Cpu* c, uint8_t modrm, uint8_t v)
{
	if (modrm >= 0xc0) { REGB(modrm & 7) = v; return; }
	GetEA(c, modrm);
	MemWrite8(c, c->ea, v);
}

static void PutRMW(V35Cpu* c, uint8_t modrm, uint16_t v)
{
	if (modrm >= 0xc0) { REGW(modrm & 7) = v; return; }
	GetEA(c, modrm);
	MemWrite16(c, c->ea, v);
}

/* ---- ALU --------------------------------------------------------------- */

static uint8_t AluB(V35Cpu* c, int op, uint32_t dst, uint32_t src)
{
	uint32_t res;
	switch (op) {
	case 0: /* ADD */
		res = dst + src;
		SetCFB(res); SetOFB_Add(res, src, dst); SetAFv(res, src, dst); SetSZPB(res);
		return (uint8_t)res;
	case 1: /* OR */
		res = dst | src;
		c->carry = c->over = c->aux = 0; SetSZPB(res);
		return (uint8_t)res;
	case 2: /* ADC */
		res = dst + src + (CF ? 1u : 0u);
		SetCFB(res); SetOFB_Add(res, src, dst); SetAFv(res, src, dst); SetSZPB(res);
		return (uint8_t)res;
	case 3: /* SBB */
		res = dst - src - (CF ? 1u : 0u);
		SetCFB(res); SetOFB_Sub(res, src, dst); SetAFv(res, src, dst); SetSZPB(res);
		return (uint8_t)res;
	case 4: /* AND */
		res = dst & src;
		c->carry = c->over = c->aux = 0; SetSZPB(res);
		return (uint8_t)res;
	case 5: /* SUB */
	case 7: /* CMP */
		res = dst - src;
		SetCFB(res); SetOFB_Sub(res, src, dst); SetAFv(res, src, dst); SetSZPB(res);
		return (uint8_t)res;
	default: /* XOR */
		res = dst ^ src;
		c->carry = c->over = c->aux = 0; SetSZPB(res);
		return (uint8_t)res;
	}
}

static uint16_t AluW(V35Cpu* c, int op, uint32_t dst, uint32_t src)
{
	uint32_t res;
	switch (op) {
	case 0:
		res = dst + src;
		SetCFW(res); SetOFW_Add(res, src, dst); SetAFv(res, src, dst); SetSZPW(res);
		return (uint16_t)res;
	case 1:
		res = dst | src;
		c->carry = c->over = c->aux = 0; SetSZPW(res);
		return (uint16_t)res;
	case 2:
		res = dst + src + (CF ? 1u : 0u);
		SetCFW(res); SetOFW_Add(res, src, dst); SetAFv(res, src, dst); SetSZPW(res);
		return (uint16_t)res;
	case 3:
		res = dst - src - (CF ? 1u : 0u);
		SetCFW(res); SetOFW_Sub(res, src, dst); SetAFv(res, src, dst); SetSZPW(res);
		return (uint16_t)res;
	case 4:
		res = dst & src;
		c->carry = c->over = c->aux = 0; SetSZPW(res);
		return (uint16_t)res;
	case 5:
	case 7:
		res = dst - src;
		SetCFW(res); SetOFW_Sub(res, src, dst); SetAFv(res, src, dst); SetSZPW(res);
		return (uint16_t)res;
	default:
		res = dst ^ src;
		c->carry = c->over = c->aux = 0; SetSZPW(res);
		return (uint16_t)res;
	}
}

static uint8_t ShiftB(V35Cpu* c, int op, uint32_t dst, unsigned count)
{
	if (count == 0) return (uint8_t)dst;
	switch (op) {
	case 0: /* ROL */
		while (count--) { c->carry = (int32_t)(dst & 0x80); dst = ((dst << 1) | (CF ? 1u : 0u)) & 0xff; }
		break;
	case 1: /* ROR */
		while (count--) { c->carry = (int32_t)(dst & 1); dst = ((dst >> 1) | (CF ? 0x80u : 0u)) & 0xff; }
		break;
	case 2: /* ROLC */
		while (count--) { dst = (dst << 1) | (CF ? 1u : 0u); SetCFB(dst); dst &= 0xff; }
		break;
	case 3: /* RORC */
		while (count--) { dst = (CF ? 0x100u : 0u) | dst; c->carry = (int32_t)(dst & 1); dst >>= 1; }
		break;
	case 4: /* SHL */
	case 6:
		dst <<= count;
		SetCFB(dst);
		SetSZPB(dst);
		break;
	case 5: /* SHR */
		dst >>= (count - 1);
		c->carry = (int32_t)(dst & 1);
		dst >>= 1;
		SetSZPB(dst);
		break;
	default: /* SHRA */
		dst = (uint32_t)(((int8_t)dst) >> (count - 1));
		c->carry = (int32_t)(dst & 1);
		dst = (uint32_t)(((int8_t)(uint8_t)dst) >> 1);
		SetSZPB(dst);
		break;
	}
	return (uint8_t)dst;
}

static uint16_t ShiftW(V35Cpu* c, int op, uint32_t dst, unsigned count)
{
	if (count == 0) return (uint16_t)dst;
	switch (op) {
	case 0:
		while (count--) { c->carry = (int32_t)(dst & 0x8000); dst = ((dst << 1) | (CF ? 1u : 0u)) & 0xffff; }
		break;
	case 1:
		while (count--) { c->carry = (int32_t)(dst & 1); dst = ((dst >> 1) | (CF ? 0x8000u : 0u)) & 0xffff; }
		break;
	case 2:
		while (count--) { dst = (dst << 1) | (CF ? 1u : 0u); SetCFW(dst); dst &= 0xffff; }
		break;
	case 3:
		while (count--) { dst = (CF ? 0x10000u : 0u) | dst; c->carry = (int32_t)(dst & 1); dst >>= 1; }
		break;
	case 4:
	case 6:
		dst <<= count;
		SetCFW(dst);
		SetSZPW(dst);
		break;
	case 5:
		dst >>= (count - 1);
		c->carry = (int32_t)(dst & 1);
		dst >>= 1;
		SetSZPW(dst);
		break;
	default:
		dst = (uint32_t)(((int16_t)dst) >> (count - 1));
		c->carry = (int32_t)(dst & 1);
		dst = (uint32_t)(((int16_t)(uint16_t)dst) >> 1);
		SetSZPW(dst);
		break;
	}
	return (uint16_t)dst;
}

/* ---- string primitives -------------------------------------------------- */

static void StringStep(V35Cpu* c, uint8_t op)
{
	const int d = c->df ? -1 : 1;
	switch (op) {
	case 0x6c: /* INM byte */
		MemWrite8(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY), PortIn8(c, W(R_DW)));
		W(R_IY) = (uint16_t)(W(R_IY) + d);
		CLK(8);
		break;
	case 0x6d: /* INM word */
		MemWrite16(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY),
			(uint16_t)(PortIn8(c, W(R_DW)) | (PortIn8(c, (uint16_t)(W(R_DW) + 1)) << 8)));
		W(R_IY) = (uint16_t)(W(R_IY) + 2 * d);
		CLK(8);
		break;
	case 0x6e: /* OUTM byte */
		PortOut8(c, W(R_DW), MemRead8(c, DefaultBase(c, S_DS0) + W(R_IX)));
		W(R_IX) = (uint16_t)(W(R_IX) + d);
		CLK(8);
		break;
	case 0x6f: /* OUTM word */
	{
		const uint16_t v = MemRead16(c, DefaultBase(c, S_DS0) + W(R_IX));
		PortOut8(c, W(R_DW), (uint8_t)v);
		PortOut8(c, (uint16_t)(W(R_DW) + 1), (uint8_t)(v >> 8));
		W(R_IX) = (uint16_t)(W(R_IX) + 2 * d);
		CLK(8);
		break;
	}
	case 0xa4: /* MOVBK byte */
		MemWrite8(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY),
			MemRead8(c, DefaultBase(c, S_DS0) + W(R_IX)));
		W(R_IY) = (uint16_t)(W(R_IY) + d);
		W(R_IX) = (uint16_t)(W(R_IX) + d);
		CLK(6);
		break;
	case 0xa5: /* MOVBK word */
		MemWrite16(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY),
			MemRead16(c, DefaultBase(c, S_DS0) + W(R_IX)));
		W(R_IY) = (uint16_t)(W(R_IY) + 2 * d);
		W(R_IX) = (uint16_t)(W(R_IX) + 2 * d);
		CLK(6);
		break;
	case 0xa6: /* CMPBK byte */
		AluB(c, 7, MemRead8(c, DefaultBase(c, S_DS0) + W(R_IX)),
			MemRead8(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY)));
		W(R_IY) = (uint16_t)(W(R_IY) + d);
		W(R_IX) = (uint16_t)(W(R_IX) + d);
		CLK(14);
		break;
	case 0xa7: /* CMPBK word */
		AluW(c, 7, MemRead16(c, DefaultBase(c, S_DS0) + W(R_IX)),
			MemRead16(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY)));
		W(R_IY) = (uint16_t)(W(R_IY) + 2 * d);
		W(R_IX) = (uint16_t)(W(R_IX) + 2 * d);
		CLK(14);
		break;
	case 0xaa: /* STM byte */
		MemWrite8(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY), B(B_AL));
		W(R_IY) = (uint16_t)(W(R_IY) + d);
		CLK(4);
		break;
	case 0xab: /* STM word */
		MemWrite16(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY), W(R_AW));
		W(R_IY) = (uint16_t)(W(R_IY) + 2 * d);
		CLK(4);
		break;
	case 0xac: /* LDM byte */
		B(B_AL) = MemRead8(c, DefaultBase(c, S_DS0) + W(R_IX));
		W(R_IX) = (uint16_t)(W(R_IX) + d);
		CLK(4);
		break;
	case 0xad: /* LDM word */
		W(R_AW) = MemRead16(c, DefaultBase(c, S_DS0) + W(R_IX));
		W(R_IX) = (uint16_t)(W(R_IX) + 2 * d);
		CLK(4);
		break;
	case 0xae: /* CMPM byte */
		AluB(c, 7, B(B_AL), MemRead8(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY)));
		W(R_IY) = (uint16_t)(W(R_IY) + d);
		CLK(4);
		break;
	default: /* 0xaf CMPM word */
		AluW(c, 7, W(R_AW), MemRead16(c, ((uint32_t)SREG(S_DS1) << 4) + W(R_IY)));
		W(R_IY) = (uint16_t)(W(R_IY) + 2 * d);
		CLK(4);
		break;
	}
}

static int StringIsCompare(uint8_t op)
{
	return (op == 0xa6 || op == 0xa7 || op == 0xae || op == 0xaf);
}

static void DoString(V35Cpu* c, uint8_t op)
{
	if (!c->repMode) {
		StringStep(c, op);
		return;
	}
	while (W(R_CW) != 0) {
		StringStep(c, op);
		W(R_CW) = (uint16_t)(W(R_CW) - 1);
		if (c->repMode == REP_Z || c->repMode == REP_NZ) {
			if (!StringIsCompare(op)) continue;
			if (c->repMode == REP_Z && !ZF) break;
			if (c->repMode == REP_NZ && ZF) break;
		} else { /* V30 REPC / REPNC */
			if (!StringIsCompare(op)) continue;
			if (c->repMode == REP_C && !CF) break;
			if (c->repMode == REP_NC && CF) break;
		}
	}
}

/* ---- 0x0f extension group ---------------------------------------------- */

static void ExecV25Group(V35Cpu* c)
{
	/* MAME v25instr i_pre_v25 uses fetch() (NOT fetchop()) for the 0F
	   sub-opcode. Irem Software Guard encrypts only the leading opcode
	   byte; FINT is stored as <enc 0F> <raw 0x92>. Decrypting the second
	   byte breaks sets whose table[0x92] is a real opcode (gunforce BE =
	   MOV SI), and the old table[0x92]=0x92 force then corrupted init. */
	const uint8_t sub = Fetch8(c);
	uint8_t modrm;
	uint32_t tmp, tmp2;
	switch (sub) {
	/* bit manipulation: register-specified (0x10-0x17) / immediate (0x18-0x1f) */
	case 0x10: case 0x11: case 0x12: case 0x13:
	case 0x14: case 0x15: case 0x16: case 0x17:
	case 0x18: case 0x19: case 0x1a: case 0x1b:
	case 0x1c: case 0x1d: case 0x1e: case 0x1f: {
		const int isWord = (sub & 1);
		const int imm = (sub >= 0x18);
		const int fn = ((sub >> 1) & 3);
		modrm = Fetch8(c);
		tmp = isWord ? GetRMW(c, modrm) : GetRMB(c, modrm);
		tmp2 = imm ? Fetch8(c) : B(B_CL);
		tmp2 &= isWord ? 0xfu : 0x7u;
		switch (fn) {
		case 0: /* TEST1 */
			c->zero = (tmp & (1u << tmp2)) ? 1 : 0;
			c->carry = c->over = 0;
			break;
		case 1: /* CLR1 */
			tmp &= ~(1u << tmp2);
			if (isWord) PutbackRMW(c, modrm, (uint16_t)tmp); else PutbackRMB(c, modrm, (uint8_t)tmp);
			break;
		case 2: /* SET1 */
			tmp |= (1u << tmp2);
			if (isWord) PutbackRMW(c, modrm, (uint16_t)tmp); else PutbackRMB(c, modrm, (uint8_t)tmp);
			break;
		default: /* NOT1 */
			tmp ^= (1u << tmp2);
			if (isWord) PutbackRMW(c, modrm, (uint16_t)tmp); else PutbackRMB(c, modrm, (uint8_t)tmp);
			break;
		}
		CLK(5);
		break;
	}
	case 0x20: /* ADD4S */
	case 0x22: /* SUB4S */
	case 0x26: /* CMP4S */
	{
		const int count = (B(B_CL) + 1) / 2;
		int i;
		c->zero = 0;
		c->carry = 0;
		for (i = 0; i < count; i++) {
			int v1, v2, result;
			if (sub == 0x20) {
				tmp = MemRead8(c, DefaultBase(c, S_DS0) + (uint16_t)(W(R_IX) + i));
				tmp2 = MemRead8(c, ((uint32_t)SREG(S_DS1) << 4) + (uint16_t)(W(R_IY) + i));
				v1 = (int)((tmp >> 4) * 10 + (tmp & 0xf));
				v2 = (int)((tmp2 >> 4) * 10 + (tmp2 & 0xf));
				result = v1 + v2 + (c->carry ? 1 : 0);
				c->carry = result > 99 ? 1 : 0;
				result %= 100;
			} else {
				tmp = MemRead8(c, ((uint32_t)SREG(S_DS1) << 4) + (uint16_t)(W(R_IY) + i));
				tmp2 = MemRead8(c, DefaultBase(c, S_DS0) + (uint16_t)(W(R_IX) + i));
				v1 = (int)((tmp >> 4) * 10 + (tmp & 0xf));
				v2 = (int)((tmp2 >> 4) * 10 + (tmp2 & 0xf));
				if (v1 < v2 + (c->carry ? 1 : 0)) {
					result = v1 + 100 - (v2 + (c->carry ? 1 : 0));
					c->carry = 1;
				} else {
					result = v1 - (v2 + (c->carry ? 1 : 0));
					c->carry = 0;
				}
			}
			v1 = ((result / 10) << 4) | (result % 10);
			if (sub != 0x26)
				MemWrite8(c, ((uint32_t)SREG(S_DS1) << 4) + (uint16_t)(W(R_IY) + i), (uint8_t)v1);
			if (v1) c->zero = 1;
			CLK(19);
		}
		break;
	}
	case 0x25: /* MOVSPA */
		tmp = (uint32_t)((W(R_PSW_SAVE) & 0x7000) >> 8);
		SREG(S_SS) = c->ram[tmp + S_SS];
		W(R_SP) = c->ram[tmp + R_SP];
		CLK(16);
		break;
	case 0x28: /* ROL4 */
		modrm = Fetch8(c);
		tmp = GetRMB(c, modrm);
		tmp <<= 4;
		tmp |= B(B_AL) & 0xf;
		B(B_AL) = (uint8_t)((B(B_AL) & 0xf0) | ((tmp >> 8) & 0xf));
		PutbackRMB(c, modrm, (uint8_t)tmp);
		CLK(13);
		break;
	case 0x2a: /* ROR4 */
		modrm = Fetch8(c);
		tmp = GetRMB(c, modrm);
		tmp2 = (uint32_t)((B(B_AL) & 0xf) << 4);
		B(B_AL) = (uint8_t)((B(B_AL) & 0xf0) | (tmp & 0xf));
		PutbackRMB(c, modrm, (uint8_t)(tmp2 | (tmp >> 4)));
		CLK(17);
		break;
	case 0x2d: /* BRKCS */
		modrm = Fetch8(c);
		BankSwitch(c, modrm >= 0xc0 ? (REGW(modrm & 7) & 7u) : 0u);
		CLK(15);
		break;
	case 0x91: /* RETRBI */
		tmp = (uint32_t)((W(R_PSW_SAVE) & 0x7000) >> 12);
		c->ip = W(R_PC_SAVE);
		ExpandFlags(c, W(R_PSW_SAVE));
		SetRB(c, tmp);
		if (c->intp0HoldIret) {
			c->intp0HoldIret = 0;
			if (c->intpState[0])
				c->pendingIrq |= SRC_INTP0;
		}
		CLK(12);
		break;
	case 0x92: /* FINT */
	case 0x90: /* Irem Software Guard often leaves FINT's 2nd byte as MAME
	             xxxx (mapped to 0x90). Treating 0F 90 as FINT clears ISPR so
	             a single INTP0 cannot permanently starve INTP1/latch. */
		for (tmp = 1; tmp < 0x100; tmp <<= 1) {
			if (c->ispr & tmp) {
				c->ispr &= (uint8_t)~tmp;
				/* After INTP0's FINT the Irem ISR does STI before IRET; a still-
				   asserted YM line would nest and corrupt song setup. Hold only
				   for INTP0 (irqs==VEC_INTP0), not after INTP1's FINT. */
				if (c->irqs == VEC_INTP0) {
					c->intp0HoldIret = 1;
					c->pendingIrq &= ~SRC_INTP0;
				}
				break;
			}
		}
		c->noInterrupt = 1;
		CLK(2);
		break;
	case 0x9c: /* BTCLR sfr, imm3, rel8 — branch if SFR bit set, then clear */
	{
		const uint8_t sfr = Fetch8(c);
		const uint8_t imm3 = Fetch8(c) & 7u;
		const int8_t rel = (int8_t)Fetch8(c);
		uint8_t val = SfrRead(c, sfr);
		if (val & (1u << imm3)) {
			val = (uint8_t)(val & ~(1u << imm3));
			SfrWrite(c, sfr, val);
			c->ip = (uint16_t)(c->ip + rel);
		}
		CLK(10);
		break;
	}
	case 0x94: /* TSKSW */
		modrm = Fetch8(c);
		tmp = modrm >= 0xc0 ? (REGW(modrm & 7) & 7u) : 0u;
		W(R_PSW_SAVE) = CompressFlags(c);
		W(R_PC_SAVE) = c->ip;
		SetRB(c, tmp);
		c->ip = W(R_PC_SAVE);
		ExpandFlags(c, W(R_PSW_SAVE));
		CLK(20);
		break;
	case 0x95: /* MOVSPB */
		modrm = Fetch8(c);
		tmp = (modrm >= 0xc0 ? (REGW(modrm & 7) & 7u) : 0u) << 4;
		c->ram[tmp + S_SS] = SREG(S_SS);
		c->ram[tmp + R_SP] = W(R_SP);
		CLK(11);
		break;
	case 0x9e: /* STOP */
		c->halted = 1;
		CLK(2);
		break;
	default:
		c->badOps++;
		c->lastBadOp = 0x0f00u | sub;
		CLK(2);
		break;
	}
}

/* ---- main decode -------------------------------------------------------- */

static void ExecOne(V35Cpu* c, uint8_t op)
{
	uint8_t modrm;
	uint32_t src, dst;

	switch (op) {
	/* --- ALU r/m,reg and reg,r/m and acc,imm --- */
	case 0x00: case 0x08: case 0x10: case 0x18:
	case 0x20: case 0x28: case 0x30: case 0x38: {
		const int alu = op >> 3;
		modrm = Fetch8(c);
		dst = GetRMB(c, modrm);
		src = REGB((modrm >> 3) & 7);
		dst = AluB(c, alu, dst, src);
		if (alu != 7) PutbackRMB(c, modrm, (uint8_t)dst);
		CLK(2);
		break;
	}
	case 0x01: case 0x09: case 0x11: case 0x19:
	case 0x21: case 0x29: case 0x31: case 0x39: {
		const int alu = op >> 3;
		modrm = Fetch8(c);
		dst = GetRMW(c, modrm);
		src = REGW((modrm >> 3) & 7);
		dst = AluW(c, alu, dst, src);
		if (alu != 7) PutbackRMW(c, modrm, (uint16_t)dst);
		CLK(2);
		break;
	}
	case 0x02: case 0x0a: case 0x12: case 0x1a:
	case 0x22: case 0x2a: case 0x32: case 0x3a: {
		const int alu = op >> 3;
		modrm = Fetch8(c);
		src = GetRMB(c, modrm);
		dst = REGB((modrm >> 3) & 7);
		dst = AluB(c, alu, dst, src);
		if (alu != 7) REGB((modrm >> 3) & 7) = (uint8_t)dst;
		CLK(2);
		break;
	}
	case 0x03: case 0x0b: case 0x13: case 0x1b:
	case 0x23: case 0x2b: case 0x33: case 0x3b: {
		const int alu = op >> 3;
		modrm = Fetch8(c);
		src = GetRMW(c, modrm);
		dst = REGW((modrm >> 3) & 7);
		dst = AluW(c, alu, dst, src);
		if (alu != 7) REGW((modrm >> 3) & 7) = (uint16_t)dst;
		CLK(2);
		break;
	}
	case 0x04: case 0x0c: case 0x14: case 0x1c:
	case 0x24: case 0x2c: case 0x34: case 0x3c: {
		const int alu = op >> 3;
		src = Fetch8(c);
		dst = AluB(c, alu, B(B_AL), src);
		if (alu != 7) B(B_AL) = (uint8_t)dst;
		CLK(2);
		break;
	}
	case 0x05: case 0x0d: case 0x15: case 0x1d:
	case 0x25: case 0x2d: case 0x35: case 0x3d: {
		const int alu = op >> 3;
		src = Fetch16(c);
		dst = AluW(c, alu, W(R_AW), src);
		if (alu != 7) W(R_AW) = (uint16_t)dst;
		CLK(2);
		break;
	}

	case 0x06: Push(c, SREG(S_DS1)); CLK(8); break;
	case 0x07: SREG(S_DS1) = Pop(c); CLK(8); break;
	case 0x0e: Push(c, SREG(S_PS)); CLK(8); break;
	case 0x16: Push(c, SREG(S_SS)); CLK(8); break;
	case 0x17: SREG(S_SS) = Pop(c); c->noInterrupt = 1; CLK(8); break;
	case 0x1e: Push(c, SREG(S_DS0)); CLK(8); break;
	case 0x1f: SREG(S_DS0) = Pop(c); CLK(8); break;

	case 0x0f: ExecV25Group(c); break;

	case 0x27: /* ADJ4A (DAA) */
		if (AF || ((B(B_AL) & 0xf) > 9)) {
			const uint16_t t = (uint16_t)(B(B_AL) + 6);
			B(B_AL) = (uint8_t)t;
			c->aux = 1;
			c->carry |= (int32_t)(t & 0x100);
		}
		if (CF || (B(B_AL) > 0x9f)) { B(B_AL) = (uint8_t)(B(B_AL) + 0x60); c->carry = 1; }
		SetSZPB(B(B_AL));
		CLK(10);
		break;
	case 0x2f: /* ADJ4S (DAS) */
		if (AF || ((B(B_AL) & 0xf) > 9)) {
			const uint16_t t = (uint16_t)(B(B_AL) - 6);
			B(B_AL) = (uint8_t)t;
			c->aux = 1;
			c->carry |= (int32_t)(t & 0x100);
		}
		if (CF || (B(B_AL) > 0x9f)) { B(B_AL) = (uint8_t)(B(B_AL) - 0x60); c->carry = 1; }
		SetSZPB(B(B_AL));
		CLK(10);
		break;
	case 0x37: /* ADJBA (AAA) */
		if (AF || ((B(B_AL) & 0xf) > 9)) {
			B(B_AL) = (uint8_t)(B(B_AL) + 6);
			B(B_AH) = (uint8_t)(B(B_AH) + 1);
			c->aux = 1; c->carry = 1;
		} else { c->aux = 0; c->carry = 0; }
		B(B_AL) &= 0x0f;
		CLK(9);
		break;
	case 0x3f: /* ADJBS (AAS) */
		if (AF || ((B(B_AL) & 0xf) > 9)) {
			B(B_AL) = (uint8_t)(B(B_AL) - 6);
			B(B_AH) = (uint8_t)(B(B_AH) - 1);
			c->aux = 1; c->carry = 1;
		} else { c->aux = 0; c->carry = 0; }
		B(B_AL) &= 0x0f;
		CLK(9);
		break;

	case 0x40: case 0x41: case 0x42: case 0x43:
	case 0x44: case 0x45: case 0x46: case 0x47: {
		const unsigned r = kWregIdx[op & 7];
		const uint32_t t = W(r);
		const uint32_t t1 = t + 1;
		c->over = (t == 0x7fff);
		SetAFv(t1, t, 1);
		SetSZPW(t1);
		W(r) = (uint16_t)t1;
		CLK(2);
		break;
	}
	case 0x48: case 0x49: case 0x4a: case 0x4b:
	case 0x4c: case 0x4d: case 0x4e: case 0x4f: {
		const unsigned r = kWregIdx[op & 7];
		const uint32_t t = W(r);
		const uint32_t t1 = t - 1;
		c->over = (t == 0x8000);
		SetAFv(t1, t, 1);
		SetSZPW(t1);
		W(r) = (uint16_t)t1;
		CLK(2);
		break;
	}
	case 0x50: case 0x51: case 0x52: case 0x53:
	case 0x54: case 0x55: case 0x56: case 0x57:
		Push(c, REGW(op & 7));
		CLK(8);
		break;
	case 0x58: case 0x59: case 0x5a: case 0x5b:
	case 0x5c: case 0x5d: case 0x5e: case 0x5f:
		REGW(op & 7) = Pop(c);
		CLK(8);
		break;

	case 0x60: { /* PUSH R */
		const uint16_t sp = W(R_SP);
		Push(c, W(R_AW)); Push(c, W(R_CW)); Push(c, W(R_DW)); Push(c, W(R_BW));
		Push(c, sp); Push(c, W(R_BP)); Push(c, W(R_IX)); Push(c, W(R_IY));
		CLK(35);
		break;
	}
	case 0x61: /* POP R */
		W(R_IY) = Pop(c); W(R_IX) = Pop(c); W(R_BP) = Pop(c);
		W(R_SP) = (uint16_t)(W(R_SP) + 2);
		W(R_BW) = Pop(c); W(R_DW) = Pop(c); W(R_CW) = Pop(c); W(R_AW) = Pop(c);
		CLK(43);
		break;
	case 0x62: { /* CHKIND */
		modrm = Fetch8(c);
		if (modrm >= 0xc0) { CLK(13); break; }
		GetEA(c, modrm);
		{
			const int16_t lo = (int16_t)MemRead16(c, c->ea);
			const int16_t hi = (int16_t)MemRead16(c, c->ea + 2);
			const int16_t v = (int16_t)REGW((modrm >> 3) & 7);
			if (v < lo || v > hi) Interrupt(c, VEC_CHKIND, SRC_BRK);
		}
		CLK(20);
		break;
	}
	case 0x63: Interrupt(c, Fetch8(c), SRC_BRKN); CLK(50); break;
	case 0x68: Push(c, Fetch16(c)); CLK(9); break;
	case 0x69: { /* MUL reg,r/m,imm16 */
		modrm = Fetch8(c);
		src = GetRMW(c, modrm);
		dst = Fetch16(c);
		{
			const int32_t r = (int32_t)(int16_t)src * (int32_t)(int16_t)dst;
			REGW((modrm >> 3) & 7) = (uint16_t)r;
			c->carry = c->over = (r >> 15 != 0 && r >> 15 != -1);
		}
		CLK(24);
		break;
	}
	case 0x6a: Push(c, (uint16_t)(int16_t)(int8_t)Fetch8(c)); CLK(9); break;
	case 0x6b: { /* MUL reg,r/m,imm8 */
		modrm = Fetch8(c);
		src = GetRMW(c, modrm);
		dst = (uint32_t)(int32_t)(int8_t)Fetch8(c);
		{
			const int32_t r = (int32_t)(int16_t)src * (int32_t)dst;
			REGW((modrm >> 3) & 7) = (uint16_t)r;
			c->carry = c->over = (r >> 15 != 0 && r >> 15 != -1);
		}
		CLK(24);
		break;
	}
	case 0x6c: case 0x6d: case 0x6e: case 0x6f:
		DoString(c, op);
		break;

	case 0x70: case 0x71: case 0x72: case 0x73:
	case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7a: case 0x7b:
	case 0x7c: case 0x7d: case 0x7e: case 0x7f: {
		const int8_t rel = (int8_t)Fetch8(c);
		int take = 0;
		switch (op & 0x0e) {
		case 0x00: take = OF; break;
		case 0x02: take = CF; break;
		case 0x04: take = ZF; break;
		case 0x06: take = (CF || ZF); break;
		case 0x08: take = SF; break;
		case 0x0a: take = PF; break;
		case 0x0c: take = (SF != OF); break;
		default: take = ((SF != OF) || ZF); break;
		}
		if (op & 1) take = !take;
		if (take) { c->ip = (uint16_t)(c->ip + rel); CLK(10); }
		else CLK(3);
		break;
	}

	case 0x80: case 0x82: { /* ALU r/m8, imm8 */
		modrm = Fetch8(c);
		dst = GetRMB(c, modrm);
		src = Fetch8(c);
		dst = AluB(c, (modrm >> 3) & 7, dst, src);
		if (((modrm >> 3) & 7) != 7) PutbackRMB(c, modrm, (uint8_t)dst);
		CLK(4);
		break;
	}
	case 0x81: { /* ALU r/m16, imm16 */
		modrm = Fetch8(c);
		dst = GetRMW(c, modrm);
		src = Fetch16(c);
		dst = AluW(c, (modrm >> 3) & 7, dst, src);
		if (((modrm >> 3) & 7) != 7) PutbackRMW(c, modrm, (uint16_t)dst);
		CLK(4);
		break;
	}
	case 0x83: { /* ALU r/m16, imm8 sign-extended */
		modrm = Fetch8(c);
		dst = GetRMW(c, modrm);
		src = (uint32_t)(uint16_t)(int16_t)(int8_t)Fetch8(c);
		dst = AluW(c, (modrm >> 3) & 7, dst, src);
		if (((modrm >> 3) & 7) != 7) PutbackRMW(c, modrm, (uint16_t)dst);
		CLK(4);
		break;
	}
	case 0x84:
		modrm = Fetch8(c);
		dst = GetRMB(c, modrm);
		AluB(c, 4, dst, REGB((modrm >> 3) & 7));
		CLK(2);
		break;
	case 0x85:
		modrm = Fetch8(c);
		dst = GetRMW(c, modrm);
		AluW(c, 4, dst, REGW((modrm >> 3) & 7));
		CLK(2);
		break;
	case 0x86: {
		modrm = Fetch8(c);
		dst = GetRMB(c, modrm);
		src = REGB((modrm >> 3) & 7);
		PutbackRMB(c, modrm, (uint8_t)src);
		REGB((modrm >> 3) & 7) = (uint8_t)dst;
		CLK(4);
		break;
	}
	case 0x87: {
		modrm = Fetch8(c);
		dst = GetRMW(c, modrm);
		src = REGW((modrm >> 3) & 7);
		PutbackRMW(c, modrm, (uint16_t)src);
		REGW((modrm >> 3) & 7) = (uint16_t)dst;
		CLK(4);
		break;
	}
	case 0x88:
		modrm = Fetch8(c);
		PutRMB(c, modrm, REGB((modrm >> 3) & 7));
		CLK(2);
		break;
	case 0x89:
		modrm = Fetch8(c);
		PutRMW(c, modrm, REGW((modrm >> 3) & 7));
		CLK(2);
		break;
	case 0x8a:
		modrm = Fetch8(c);
		REGB((modrm >> 3) & 7) = GetRMB(c, modrm);
		CLK(2);
		break;
	case 0x8b:
		modrm = Fetch8(c);
		REGW((modrm >> 3) & 7) = GetRMW(c, modrm);
		CLK(2);
		break;
	case 0x8c: /* sreg field: 0=DS1(ES) 1=PS(CS) 2=SS 3=DS0(DS) */
		modrm = Fetch8(c);
		PutRMW(c, modrm, SREG(S_DS1 - ((modrm >> 3) & 3)));
		CLK(2);
		break;
	case 0x8d: /* LDEA */
		modrm = Fetch8(c);
		if (modrm >= 0xc0) { CLK(2); break; }
		GetEA(c, modrm);
		REGW((modrm >> 3) & 7) = c->eo;
		CLK(2);
		break;
	case 0x8e: {
		uint16_t v;
		modrm = Fetch8(c);
		v = GetRMW(c, modrm);
		SREG(S_DS1 - ((modrm >> 3) & 3)) = v;
		c->noInterrupt = 1;
		CLK(2);
		break;
	}
	case 0x8f: {
		modrm = Fetch8(c);
		{
			const uint16_t v = Pop(c);
			PutRMW(c, modrm, v);
		}
		CLK(8);
		break;
	}

	case 0x90: CLK(3); break; /* NOP */
	case 0x91: case 0x92: case 0x93: case 0x94:
	case 0x95: case 0x96: case 0x97: {
		const uint16_t t = REGW(op & 7);
		REGW(op & 7) = W(R_AW);
		W(R_AW) = t;
		CLK(3);
		break;
	}
	case 0x98: W(R_AW) = (uint16_t)(int16_t)(int8_t)B(B_AL); CLK(2); break;
	case 0x99: W(R_DW) = (W(R_AW) & 0x8000) ? 0xffff : 0x0000; CLK(4); break;
	case 0x9a: { /* CALL far */
		const uint16_t off = Fetch16(c);
		const uint16_t seg = Fetch16(c);
		Push(c, SREG(S_PS));
		Push(c, c->ip);
		c->ip = off;
		SREG(S_PS) = seg;
		CLK(20);
		break;
	}
	case 0x9b: CLK(9); break; /* POLL — external ready is always asserted */
	case 0x9c: Push(c, CompressFlags(c)); CLK(8); break;
	case 0x9d: ExpandFlags(c, Pop(c)); CLK(8); break;
	case 0x9e: { /* MOV PSW.low, AH */
		const uint16_t f = (uint16_t)((CompressFlags(c) & 0xff00) | B(B_AH));
		ExpandFlags(c, f);
		CLK(4);
		break;
	}
	case 0x9f: B(B_AH) = (uint8_t)CompressFlags(c); CLK(2); break;

	case 0xa0: B(B_AL) = MemRead8(c, DefaultBase(c, S_DS0) + Fetch16(c)); CLK(4); break;
	case 0xa1: W(R_AW) = MemRead16(c, DefaultBase(c, S_DS0) + Fetch16(c)); CLK(4); break;
	case 0xa2: MemWrite8(c, DefaultBase(c, S_DS0) + Fetch16(c), B(B_AL)); CLK(4); break;
	case 0xa3: MemWrite16(c, DefaultBase(c, S_DS0) + Fetch16(c), W(R_AW)); CLK(4); break;

	case 0xa4: case 0xa5: case 0xa6: case 0xa7:
	case 0xaa: case 0xab: case 0xac: case 0xad:
	case 0xae: case 0xaf:
		DoString(c, op);
		break;

	case 0xa8: AluB(c, 4, B(B_AL), Fetch8(c)); CLK(2); break;
	case 0xa9: AluW(c, 4, W(R_AW), Fetch16(c)); CLK(2); break;

	case 0xb0: case 0xb1: case 0xb2: case 0xb3:
	case 0xb4: case 0xb5: case 0xb6: case 0xb7:
		REGB(op & 7) = Fetch8(c);
		CLK(2);
		break;
	case 0xb8: case 0xb9: case 0xba: case 0xbb:
	case 0xbc: case 0xbd: case 0xbe: case 0xbf:
		REGW(op & 7) = Fetch16(c);
		CLK(2);
		break;

	case 0xc0: { /* shift r/m8, imm8 */
		modrm = Fetch8(c);
		dst = GetRMB(c, modrm);
		src = Fetch8(c) & 0x1f;
		PutbackRMB(c, modrm, ShiftB(c, (modrm >> 3) & 7, dst, src));
		CLK(5);
		break;
	}
	case 0xc1: {
		modrm = Fetch8(c);
		dst = GetRMW(c, modrm);
		src = Fetch8(c) & 0x1f;
		PutbackRMW(c, modrm, ShiftW(c, (modrm >> 3) & 7, dst, src));
		CLK(5);
		break;
	}
	case 0xc2: { const uint16_t n = Fetch16(c); c->ip = Pop(c); W(R_SP) = (uint16_t)(W(R_SP) + n); CLK(24); break; }
	case 0xc3: c->ip = Pop(c); CLK(19); break;
	case 0xc4: case 0xc5: { /* LDS1 (LES) / LDS0 (LDS) */
		modrm = Fetch8(c);
		if (modrm >= 0xc0) { CLK(6); break; }
		GetEA(c, modrm);
		REGW((modrm >> 3) & 7) = MemRead16(c, c->ea);
		if (op == 0xc4) SREG(S_DS1) = MemRead16(c, c->ea + 2);
		else SREG(S_DS0) = MemRead16(c, c->ea + 2);
		CLK(6);
		break;
	}
	case 0xc6: modrm = Fetch8(c); if (modrm >= 0xc0) { REGB(modrm & 7) = Fetch8(c); } else { GetEA(c, modrm); MemWrite8(c, c->ea, Fetch8(c)); } CLK(3); break;
	case 0xc7: modrm = Fetch8(c); if (modrm >= 0xc0) { REGW(modrm & 7) = Fetch16(c); } else { GetEA(c, modrm); MemWrite16(c, c->ea, Fetch16(c)); } CLK(3); break;
	case 0xc8: { /* PREPARE (ENTER) */
		const uint16_t nb = Fetch16(c);
		const uint8_t level = Fetch8(c);
		unsigned i;
		Push(c, W(R_BP));
		W(R_BP) = W(R_SP);
		W(R_SP) = (uint16_t)(W(R_SP) - nb);
		for (i = 1; i < level; i++)
			Push(c, MemRead16(c, ((uint32_t)SREG(S_SS) << 4) + (uint16_t)(W(R_BP) - i * 2)));
		CLK(23);
		break;
	}
	case 0xc9: W(R_SP) = W(R_BP); W(R_BP) = Pop(c); CLK(8); break;
	case 0xca: { const uint16_t n = Fetch16(c); c->ip = Pop(c); SREG(S_PS) = Pop(c); W(R_SP) = (uint16_t)(W(R_SP) + n); CLK(32); break; }
	case 0xcb: c->ip = Pop(c); SREG(S_PS) = Pop(c); CLK(29); break;
	case 0xcc: Interrupt(c, 3, SRC_BRK); CLK(50); break;
	case 0xcd: Interrupt(c, Fetch8(c), SRC_BRK); CLK(50); break;
	case 0xce: if (OF) { Interrupt(c, VEC_BRKV, SRC_BRK); CLK(52); } else CLK(3); break;
	case 0xcf: /* RETI */
		c->ip = Pop(c);
		SREG(S_PS) = Pop(c);
		ExpandFlags(c, Pop(c));
		if (c->intp0HoldIret) {
			c->intp0HoldIret = 0;
			if (c->intpState[0])
				c->pendingIrq |= SRC_INTP0;
		}
		CLK(39);
		break;

	case 0xd0: case 0xd1: case 0xd2: case 0xd3: {
		const int isWord = (op & 1);
		const unsigned count = (op & 2) ? (unsigned)B(B_CL) : 1u;
		modrm = Fetch8(c);
		if (isWord) {
			dst = GetRMW(c, modrm);
			PutbackRMW(c, modrm, ShiftW(c, (modrm >> 3) & 7, dst, count & 0x1f));
		} else {
			dst = GetRMB(c, modrm);
			PutbackRMB(c, modrm, ShiftB(c, (modrm >> 3) & 7, dst, count & 0x1f));
		}
		CLK(2 + (int)(count & 0x1f));
		break;
	}
	case 0xd4: { /* CVTBD (AAM) */
		const uint8_t base = Fetch8(c);
		if (base == 0) { Interrupt(c, VEC_DIVIDE, SRC_BRK); CLK(17); break; }
		B(B_AH) = (uint8_t)(B(B_AL) / base);
		B(B_AL) = (uint8_t)(B(B_AL) % base);
		SetSZPW(W(R_AW));
		CLK(17);
		break;
	}
	case 0xd5: { /* CVTDB (AAD) */
		const uint8_t base = Fetch8(c);
		B(B_AL) = (uint8_t)(B(B_AH) * base + B(B_AL));
		B(B_AH) = 0;
		SetSZPB(B(B_AL));
		CLK(6);
		break;
	}
	case 0xd6: B(B_AL) = CF ? 0xff : 0x00; CLK(3); break;
	case 0xd7: /* TRANS (XLAT) */
		B(B_AL) = MemRead8(c, DefaultBase(c, S_DS0) + (uint16_t)(W(R_BW) + B(B_AL)));
		CLK(9);
		break;
	case 0xd8: case 0xd9: case 0xda: case 0xdb:
	case 0xdc: case 0xdd: case 0xde: case 0xdf:
		/* FPO — no coprocessor on M92; still consume the operand bytes. */
		modrm = Fetch8(c);
		if (modrm < 0xc0) GetEA(c, modrm);
		CLK(2);
		break;

	case 0xe0: case 0xe1: case 0xe2: { /* DBNZNE / DBNZE / DBNZ */
		const int8_t rel = (int8_t)Fetch8(c);
		W(R_CW) = (uint16_t)(W(R_CW) - 1);
		{
			int take = (W(R_CW) != 0);
			if (op == 0xe0) take = take && !ZF;
			else if (op == 0xe1) take = take && ZF;
			if (take) { c->ip = (uint16_t)(c->ip + rel); CLK(14); }
			else CLK(5);
		}
		break;
	}
	case 0xe3: { /* BCWZ */
		const int8_t rel = (int8_t)Fetch8(c);
		if (W(R_CW) == 0) { c->ip = (uint16_t)(c->ip + rel); CLK(13); }
		else CLK(5);
		break;
	}
	case 0xe4: B(B_AL) = PortIn8(c, Fetch8(c)); CLK(9); break;
	case 0xe5: { const uint16_t p = Fetch8(c); W(R_AW) = (uint16_t)(PortIn8(c, p) | (PortIn8(c, (uint16_t)(p + 1)) << 8)); CLK(9); break; }
	case 0xe6: { const uint16_t p = Fetch8(c); PortOut8(c, p, B(B_AL)); CLK(8); break; }
	case 0xe7: { const uint16_t p = Fetch8(c); PortOut8(c, p, B(B_AL)); PortOut8(c, (uint16_t)(p + 1), B(B_AH)); CLK(8); break; }
	case 0xe8: { const int16_t rel = (int16_t)Fetch16(c); Push(c, c->ip); c->ip = (uint16_t)(c->ip + rel); CLK(16); break; }
	case 0xe9: { const int16_t rel = (int16_t)Fetch16(c); c->ip = (uint16_t)(c->ip + rel); CLK(13); break; }
	case 0xea: { const uint16_t off = Fetch16(c); const uint16_t seg = Fetch16(c); c->ip = off; SREG(S_PS) = seg; CLK(15); break; }
	case 0xeb: { const int8_t rel = (int8_t)Fetch8(c); c->ip = (uint16_t)(c->ip + rel); CLK(12); break; }
	case 0xec: B(B_AL) = PortIn8(c, W(R_DW)); CLK(8); break;
	case 0xed: W(R_AW) = (uint16_t)(PortIn8(c, W(R_DW)) | (PortIn8(c, (uint16_t)(W(R_DW) + 1)) << 8)); CLK(8); break;
	case 0xee: PortOut8(c, W(R_DW), B(B_AL)); CLK(8); break;
	case 0xef: PortOut8(c, W(R_DW), B(B_AL)); PortOut8(c, (uint16_t)(W(R_DW) + 1), B(B_AH)); CLK(8); break;

	case 0xf1: Interrupt(c, Fetch8(c), SRC_BRKS); CLK(50); break;
	case 0xf4: c->halted = 1; CLK(2); break;
	case 0xf5: c->carry = !CF; CLK(4); break;

	case 0xf6: { /* group3 byte */
		modrm = Fetch8(c);
		dst = GetRMB(c, modrm);
		switch ((modrm >> 3) & 7) {
		case 0: case 1: AluB(c, 4, dst, Fetch8(c)); CLK(4); break;
		case 2: PutbackRMB(c, modrm, (uint8_t)~dst); CLK(3); break;
		case 3: { /* NEG */
			const uint32_t r = 0u - dst;
			SetCFB(r ? 0x100u : 0u);
			SetOFB_Sub(r, dst, 0u);
			SetAFv(r, dst, 0u);
			SetSZPB(r);
			PutbackRMB(c, modrm, (uint8_t)r);
			CLK(3);
			break;
		}
		case 4: { /* MULU */
			const uint32_t r = (uint32_t)B(B_AL) * dst;
			W(R_AW) = (uint16_t)r;
			c->carry = c->over = (B(B_AH) != 0);
			CLK(21);
			break;
		}
		case 5: { /* MUL */
			const int32_t r = (int32_t)(int8_t)B(B_AL) * (int32_t)(int8_t)dst;
			W(R_AW) = (uint16_t)r;
			c->carry = c->over = ((r >> 7) != 0 && (r >> 7) != -1);
			CLK(21);
			break;
		}
		case 6: { /* DIVU */
			if (dst == 0) { Interrupt(c, VEC_DIVIDE, SRC_BRK); CLK(25); break; }
			{
				const uint32_t q = W(R_AW) / dst;
				const uint32_t r = W(R_AW) % dst;
				const int ovf = (q > 0xff);
				c->carry = c->over = !ovf;
				if (ovf) Interrupt(c, VEC_DIVIDE, SRC_BRK);
				else { B(B_AL) = (uint8_t)q; B(B_AH) = (uint8_t)r; }
			}
			CLK(25);
			break;
		}
		default: { /* DIV */
			if (dst == 0) { Interrupt(c, VEC_DIVIDE, SRC_BRK); CLK(29); break; }
			{
				const int32_t n = (int16_t)W(R_AW);
				const int32_t d = (int8_t)dst;
				const int32_t q = n / d;
				const int32_t r = n % d;
				const int ovf = (q > 0x7f || q < -0x7f);
				c->carry = c->over = !ovf;
				if (ovf) Interrupt(c, VEC_DIVIDE, SRC_BRK);
				else { B(B_AL) = (uint8_t)q; B(B_AH) = (uint8_t)r; }
			}
			CLK(29);
			break;
		}
		}
		break;
	}
	case 0xf7: { /* group3 word */
		modrm = Fetch8(c);
		dst = GetRMW(c, modrm);
		switch ((modrm >> 3) & 7) {
		case 0: case 1: AluW(c, 4, dst, Fetch16(c)); CLK(4); break;
		case 2: PutbackRMW(c, modrm, (uint16_t)~dst); CLK(3); break;
		case 3: {
			const uint32_t r = 0u - dst;
			SetCFW(r ? 0x10000u : 0u);
			SetOFW_Sub(r, dst, 0u);
			SetAFv(r, dst, 0u);
			SetSZPW(r);
			PutbackRMW(c, modrm, (uint16_t)r);
			CLK(3);
			break;
		}
		case 4: {
			const uint32_t r = (uint32_t)W(R_AW) * dst;
			W(R_AW) = (uint16_t)r;
			W(R_DW) = (uint16_t)(r >> 16);
			c->carry = c->over = (W(R_DW) != 0);
			CLK(29);
			break;
		}
		case 5: {
			const int32_t r = (int32_t)(int16_t)W(R_AW) * (int32_t)(int16_t)dst;
			W(R_AW) = (uint16_t)r;
			W(R_DW) = (uint16_t)(r >> 16);
			c->carry = c->over = ((r >> 15) != 0 && (r >> 15) != -1);
			CLK(29);
			break;
		}
		case 6: {
			if (dst == 0) { Interrupt(c, VEC_DIVIDE, SRC_BRK); CLK(33); break; }
			{
				const uint32_t n = ((uint32_t)W(R_DW) << 16) | W(R_AW);
				const uint32_t q = n / dst;
				const uint32_t r = n % dst;
				const int ovf = (q > 0xffff);
				c->carry = c->over = !ovf;
				if (ovf) Interrupt(c, VEC_DIVIDE, SRC_BRK);
				else { W(R_AW) = (uint16_t)q; W(R_DW) = (uint16_t)r; }
			}
			CLK(33);
			break;
		}
		default: {
			if (dst == 0) { Interrupt(c, VEC_DIVIDE, SRC_BRK); CLK(38); break; }
			{
				const int32_t n = (int32_t)(((uint32_t)W(R_DW) << 16) | W(R_AW));
				const int32_t d = (int16_t)dst;
				const int32_t q = n / d;
				const int32_t r = n % d;
				const int ovf = (q > 0x7fff || q < -0x7fff);
				c->carry = c->over = !ovf;
				if (ovf) Interrupt(c, VEC_DIVIDE, SRC_BRK);
				else { W(R_AW) = (uint16_t)q; W(R_DW) = (uint16_t)r; }
			}
			CLK(38);
			break;
		}
		}
		break;
	}

	case 0xf8: c->carry = 0; CLK(4); break;
	case 0xf9: c->carry = 1; CLK(4); break;
	case 0xfa: c->iflag = 0; CLK(4); break;
	case 0xfb: c->iflag = 1; c->noInterrupt = 1; CLK(4); break;
	case 0xfc: c->df = 0; CLK(4); break;
	case 0xfd: c->df = 1; CLK(4); break;

	case 0xfe: { /* INC/DEC r/m8 */
		modrm = Fetch8(c);
		dst = GetRMB(c, modrm);
		if (((modrm >> 3) & 7) == 0) {
			const uint32_t t1 = dst + 1;
			c->over = (dst == 0x7f);
			SetAFv(t1, dst, 1);
			SetSZPB(t1);
			PutbackRMB(c, modrm, (uint8_t)t1);
		} else {
			const uint32_t t1 = dst - 1;
			c->over = (dst == 0x80);
			SetAFv(t1, dst, 1);
			SetSZPB(t1);
			PutbackRMB(c, modrm, (uint8_t)t1);
		}
		CLK(3);
		break;
	}
	case 0xff: { /* group5 */
		modrm = Fetch8(c);
		switch ((modrm >> 3) & 7) {
		case 0: case 1: {
			dst = GetRMW(c, modrm);
			if (((modrm >> 3) & 7) == 0) {
				const uint32_t t1 = dst + 1;
				c->over = (dst == 0x7fff);
				SetAFv(t1, dst, 1);
				SetSZPW(t1);
				PutbackRMW(c, modrm, (uint16_t)t1);
			} else {
				const uint32_t t1 = dst - 1;
				c->over = (dst == 0x8000);
				SetAFv(t1, dst, 1);
				SetSZPW(t1);
				PutbackRMW(c, modrm, (uint16_t)t1);
			}
			CLK(3);
			break;
		}
		case 2: { /* CALL near r/m */
			const uint16_t target = GetRMW(c, modrm);
			Push(c, c->ip);
			c->ip = target;
			CLK(16);
			break;
		}
		case 3: { /* CALL far [mem] */
			if (modrm >= 0xc0) { CLK(4); break; }
			GetEA(c, modrm);
			{
				const uint16_t off = MemRead16(c, c->ea);
				const uint16_t seg = MemRead16(c, c->ea + 2);
				Push(c, SREG(S_PS));
				Push(c, c->ip);
				c->ip = off;
				SREG(S_PS) = seg;
			}
			CLK(23);
			break;
		}
		case 4: c->ip = GetRMW(c, modrm); CLK(11); break;
		case 5: { /* JMP far [mem] */
			if (modrm >= 0xc0) { CLK(4); break; }
			GetEA(c, modrm);
			{
				const uint16_t off = MemRead16(c, c->ea);
				const uint16_t seg = MemRead16(c, c->ea + 2);
				c->ip = off;
				SREG(S_PS) = seg;
			}
			CLK(15);
			break;
		}
		case 6: Push(c, GetRMW(c, modrm)); CLK(8); break;
		default: c->badOps++; c->lastBadOp = 0xff00u | modrm; CLK(2); break;
		}
		break;
	}

	default:
		c->badOps++;
		c->lastBadOp = op;
		CLK(2);
		break;
	}
}

static void Step(V35Cpu* c)
{
	uint8_t op;
	c->segPrefix = 0;
	c->repMode = REP_NONE;
	for (;;) {
		op = FetchOp(c);
		if (op == 0x26) { c->segPrefix = 1 + S_DS1; c->prefixBase = (uint32_t)SREG(S_DS1) << 4; CLK(2); continue; }
		if (op == 0x2e) { c->segPrefix = 1 + S_PS; c->prefixBase = (uint32_t)SREG(S_PS) << 4; CLK(2); continue; }
		if (op == 0x36) { c->segPrefix = 1 + S_SS; c->prefixBase = (uint32_t)SREG(S_SS) << 4; CLK(2); continue; }
		if (op == 0x3e) { c->segPrefix = 1 + S_DS0; c->prefixBase = (uint32_t)SREG(S_DS0) << 4; CLK(2); continue; }
		if (op == 0xf0) { CLK(2); continue; }               /* BUSLOCK */
		if (op == 0xf2) { c->repMode = REP_NZ; CLK(2); continue; }
		if (op == 0xf3) { c->repMode = REP_Z; CLK(2); continue; }
		if (op == 0x64) { c->repMode = REP_NC; CLK(2); continue; }
		if (op == 0x65) { c->repMode = REP_C; CLK(2); continue; }
		break;
	}
	ExecOne(c, op);
}

static void TickTimers(V35Cpu* c, int used)
{
	if (c->tbPeriod > 0) {
		c->tbCount -= used;
		while (c->tbCount <= 0) {
			c->tbCount += c->tbPeriod;
			c->pendingIrq |= SRC_INTTB;
		}
	}
	if (c->t0Period > 0) {
		c->t0Count -= used;
		while (c->t0Count <= 0) {
			c->t0Count += c->t0Period;
			c->pendingIrq |= SRC_INTTU0;
			if (c->tmc0 & 0x01) { c->t0Period = 0; break; } /* one-shot */
		}
	}
	if (c->t1Period > 0) {
		c->t1Count -= used;
		while (c->t1Count <= 0) {
			c->t1Count += c->t1Period;
			c->pendingIrq |= SRC_INTTU1;
			if (c->tmc0 & 0x01) { c->t1Period = 0; break; }
		}
	}
	if (c->t2Period > 0) {
		c->t2Count -= used;
		while (c->t2Count <= 0) {
			c->t2Count += c->t2Period;
			c->pendingIrq |= SRC_INTTU1 | SRC_INTTU2;
		}
	}
}

/* ---- public API --------------------------------------------------------- */

V35Cpu* V35Create(void)
{
	V35Cpu* c = (V35Cpu*)calloc(1, sizeof(V35Cpu));
	if (!c) return NULL;
	if (!kParityReady) {
		unsigned i;
		for (i = 0; i < 256; i++) {
			unsigned j = i, n = 0;
			for (; j; j >>= 1) if (j & 1) n++;
			kParity[i] = (uint8_t)(!(n & 1));
		}
		kParityReady = 1;
	}
	V35Reset(c);
	return c;
}

void V35Destroy(V35Cpu* c)
{
	free(c);
}

void V35SetBus(V35Cpu* c, void* ctx,
	V35ReadFn read, V35WriteFn write, V35InFn in, V35OutFn out)
{
	if (!c) return;
	c->ctx = ctx;
	c->read = read;
	c->write = write;
	c->in = in;
	c->out = out;
}

void V35SetDecryptionTable(V35Cpu* c, const uint8_t* table256)
{
	if (!c) return;
	c->decrypt = table256;
	c->modeState = c->mf = table256 ? 0 : 1;
}

void V35Reset(V35Cpu* c)
{
	const uint8_t* table;
	void* ctx;
	V35ReadFn read; V35WriteFn write; V35InFn in; V35OutFn out;
	if (!c) return;
	table = c->decrypt;
	ctx = c->ctx; read = c->read; write = c->write; in = c->in; out = c->out;
	memset(c, 0, sizeof(*c));
	c->decrypt = table;
	c->ctx = ctx; c->read = read; c->write = write; c->in = in; c->out = out;

	memset(c->sfr, 0xff, sizeof(c->sfr));
	c->ip = 0;
	c->ibrk = 1;
	c->zero = 1;
	c->parity = 1;
	c->pendingIrq = 0;
	c->unmaskedIrq = SRC_INT | SRC_NMI;
	c->priInttu = c->priIntd = c->priIntp = c->priInts0 = c->priInts1 = 7;
	c->intp0HoldIret = 0;
	c->modeState = c->mf = c->decrypt ? 0 : 1;
	c->ramen = 1;
	c->tb = 20;
	c->pck = 8;
	c->rfm = 0xfc;
	c->wtc = 0xffff;
	c->idb = 0xffe00u;
	c->tbPeriod = (int32_t)((uint32_t)c->pck << c->tb);
	c->tbCount = c->tbPeriod;
	SetRB(c, 7);
	SREG(S_PS) = 0xffff;
	SREG(S_SS) = 0;
	SREG(S_DS0) = 0;
	SREG(S_DS1) = 0;
}

int V35Execute(V35Cpu* c, int cycles)
{
	if (!c || cycles <= 0) return 0;
	c->icount = cycles;
	while (c->icount > 0) {
		int64_t before;
		int used;
		uint32_t pend = c->pendingIrq & c->unmaskedIrq;
		if (c->halted) {
			if (!pend || !(c->iflag || (pend & SRC_NMI))) {
				/* Nothing can wake it inside this slice; timers still run. */
				TickTimers(c, (int)c->icount);
				c->icount = 0;
				break;
			}
			c->halted = 0;
		}
		if (pend && !c->noInterrupt && ((pend & SRC_NMI) || c->iflag))
			ExternalInt(c);
		c->noInterrupt = 0;
		before = c->icount;
		Step(c);
		used = (int)(before - c->icount);
		if (used <= 0) { c->icount -= 1; used = 1; }
		TickTimers(c, used);
	}
	return cycles - (int)c->icount;
}

void V35SetInputLine(V35Cpu* c, int line, int state)
{
	if (!c) return;
	if (line == V35_LINE_NMI) {
		if (c->nmiState == (uint8_t)state) return;
		c->nmiState = (uint8_t)state;
		if (state != V35_CLEAR_LINE) {
			c->pendingIrq |= SRC_NMI;
			c->halted = 0;
		}
		return;
	}
	if (line >= V35_LINE_INTP0 && line <= V35_LINE_INTP2) {
		const int idx = line - V35_LINE_INTP0;
		if (c->intpState[idx] == (uint8_t)state) return;
		c->intpState[idx] = (uint8_t)state;
		if (state != V35_CLEAR_LINE) {
			/* INTP0 held until IRET after FINT;STI (Rev 3.40 YM line). */
			if (idx == 0 && c->intp0HoldIret)
				return;
			c->pendingIrq |= (SRC_INTP0 << idx);
			c->halted = 0;
		}
	}
}

uint32_t V35Pc(const V35Cpu* c)
{
	if (!c) return 0;
	return (((uint32_t)c->ram[c->rbw + S_PS] << 4) + c->ip) & 0xfffffu;
}

uint16_t V35Reg(const V35Cpu* c, int index)
{
	if (!c || index < 0 || index > 15) return 0;
	return c->ram[c->rbw + index];
}

int V35Halted(const V35Cpu* c) { return c ? c->halted : 0; }
uint32_t V35BadOpCount(const V35Cpu* c) { return c ? c->badOps : 0; }
uint32_t V35LastBadOp(const V35Cpu* c) { return c ? c->lastBadOp : 0; }
uint32_t V35IrqCount(const V35Cpu* c) { return c ? c->irqCount : 0; }
uint32_t V35IrqSrcCount(const V35Cpu* c, unsigned srcIndex)
{
	return (c && srcIndex < 16) ? c->irqSrcCount[srcIndex] : 0;
}
uint32_t V35IrqBankSwitchCount(const V35Cpu* c) { return c ? c->irqBankSwitch : 0; }
uint32_t V35IrqClassicCount(const V35Cpu* c) { return c ? c->irqClassic : 0; }
uint32_t V35UnmaskedIrq(const V35Cpu* c) { return c ? c->unmaskedIrq : 0; }
uint32_t V35BankswitchIrqMask(const V35Cpu* c) { return c ? c->bankswitchIrq : 0; }
uint16_t V35BankWord(const V35Cpu* c, unsigned bank, unsigned index)
{
	if (!c || bank > 7 || index > 15) return 0;
	return c->ram[(bank << 4) + index];
}
