#include "StdAfx.h"
#include "cemu_x68k_dos.h"
#include "cemu_hard_x68k.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>

/* Emit big-endian 68000 into $F0xxxx via CHardX68k::Write*. */

namespace {

struct Emit {
	CHardX68k* hw;
	unsigned pc;
	explicit Emit(CHardX68k* h, unsigned at) : hw(h), pc(at) {}
	void w16(unsigned v)
	{
		hw->Write16(pc, (uint16_t)(v & 0xffffu));
		pc += 2;
	}
	void w32(unsigned v)
	{
		hw->Write32(pc, v);
		pc += 4;
	}
	unsigned mark() const { return pc; }
	void patch16(unsigned at, unsigned v) { hw->Write16(at, (uint16_t)(v & 0xffffu)); }
};

static int isHangStub(CHardX68k* hw, unsigned vec)
{
	/* BOOT placeholder: nop; bra.s * — do not treat null/odd vectors as hang. */
	if (vec < 0x100u || vec > 0xfffff0u) return 0;
	return hw->Read16(vec) == 0x4e71u && hw->Read16(vec + 2u) == 0x60fcu;
}

static int looksThinStub(CHardX68k* hw, unsigned vec)
{
	if (vec < 8u || vec > 0xfffff0u) return 1;
	if (isHangStub(hw, vec)) return 1;
	const unsigned b0 = hw->Read8(vec);
	const unsigned b1 = hw->Read8(vec + 1);
	/* Entry itself is move.b #imm,$E00010; rte */
	if (b0 == 0x13 && b1 == 0xfc
		&& hw->Read8(vec + 4) == 0x00 && hw->Read8(vec + 5) == 0xe0
		&& hw->Read8(vec + 6) == 0x00 && hw->Read8(vec + 7) == 0x10)
		return 1;
	/* rte-only / empty */
	if (b0 == 0x4e && b1 == 0x73) return 1;
	return 0;
}

static int isBootErrorLineF(CHardX68k* hw, unsigned vec)
{
	if (vec < 8u || vec > 0xfffff0u) return 1;
	/* Real Human68k DOS is large; boot stubs fit in <0x40 and end in $E00010 store. */
	int sawErr = 0;
	for (unsigned off = 0; off < 0x40u; off++) {
		const unsigned a = vec + off;
		if (hw->Read8(a) == 0x13 && hw->Read8(a + 1) == 0xfc
			&& hw->Read8(a + 4) == 0x00 && hw->Read8(a + 5) == 0xe0
			&& hw->Read8(a + 6) == 0x00 && hw->Read8(a + 7) == 0x10)
			sawErr = 1;
	}
	if (!sawErr) return 0;
	/* Exclude handlers that already implement INTVCS/MALLOC (cmpi #$25/#$48). */
	for (unsigned off = 0; off < 0x80u; off++) {
		if (hw->Read8(vec + off) == 0x0c && hw->Read8(vec + off + 1) == 0x40
			&& hw->Read8(vec + off + 2) == 0x00
			&& (hw->Read8(vec + off + 3) == 0x25 || hw->Read8(vec + off + 3) == 0x48
				|| hw->Read8(vec + off + 3) == 0x30))
			return 0;
	}
	return 1;
}

static int lineFNeedsOs(CHardX68k* hw, unsigned vec)
{
	if (looksThinStub(hw, vec)) return 1;
	return isBootErrorLineF(hw, vec);
}

/* BOOT trap15 that cmp-chains a few fns then errors to $E00010 — lacks OPMSET.
   Also: IOCS-table trampoline (movem; lea $400; jsr) — keeps trap15 itself,
   but FEFUNC/$7C0 and OPM slots still need our bodies when thin. */
static int isBootErrorTrap15(CHardX68k* hw, unsigned vec)
{
	if (vec < 8u || vec > 0xfffff0u) return 1;
	const unsigned b0 = hw->Read8(vec);
	const unsigned b1 = hw->Read8(vec + 1);
	/* Must look like a cmpi.b / moveq dispatch, not a full IOCS image. */
	if (!(b0 == 0x0c && b1 == 0x00) && !(b0 == 0x70) && !(b0 == 0x42 && b1 == 0x80))
		return 0;
	int sawErr = 0, sawE900 = 0, saw68 = 0;
	for (unsigned off = 0; off < 0x60u; off++) {
		const unsigned a = vec + off;
		if (hw->Read8(a) == 0x13 && hw->Read8(a + 1) == 0xfc
			&& hw->Read8(a + 4) == 0x00 && hw->Read8(a + 5) == 0xe0
			&& hw->Read8(a + 6) == 0x00 && hw->Read8(a + 7) == 0x10)
			sawErr = 1;
		if (hw->Read8(a) == 0x00 && hw->Read8(a + 1) == 0xe9
			&& hw->Read8(a + 2) == 0x00)
			sawE900 = 1;
		if (hw->Read8(a) == 0x0c && hw->Read8(a + 1) == 0x00
			&& hw->Read8(a + 2) == 0x00 && hw->Read8(a + 3) == 0x68)
			saw68 = 1;
	}
	if (sawE900 || saw68) return 0;
	return sawErr;
}

/* BOOT trap15: `movem; move.l #$400,a6; move.l (a6,d6),a6; jsr (a6)`.
   Correct for IOCS table — do not replace vector; ensure slots via thinIocs. */
static int isBootIocsTableTrap15(CHardX68k* hw, unsigned vec)
{
	if (vec < 8u || vec > 0xfffff0u) return 0;
	for (unsigned off = 0; off + 6u < 0x40u; off++) {
		if (hw->Read8(vec + off) == 0x2c && hw->Read8(vec + off + 1) == 0x7c
			&& hw->Read8(vec + off + 2) == 0x00 && hw->Read8(vec + off + 3) == 0x00
			&& hw->Read8(vec + off + 4) == 0x04 && hw->Read8(vec + off + 5) == 0x00)
			return 1; /* move.l #$400,An */
	}
	return 0;
}

static int trap15NeedsOs(CHardX68k* hw, unsigned vec)
{
	if (looksThinStub(hw, vec)) return 1;
	if (isBootIocsTableTrap15(hw, vec)) return 0; /* keep table trampoline */
	return isBootErrorTrap15(hw, vec);
}

static int looksCode(CHardX68k* hw, unsigned a)
{
	const unsigned w = ((unsigned)hw->Read8(a) << 8) | hw->Read8(a + 1);
	if (w == 0x48e7 || w == 0x4e56 || w == 0x4e75 || w == 0x2e7c) return 1;
	if (w == 0x23cf || w == 0x4eb9 || w == 0x6100) return 1;
	if (w == 0x343c || w == 0x223c || w == 0x22bc || w == 0x4e90) return 1;
	if (w == 0x41f9 || w == 0x43f9 || w == 0x2079 || w == 0x4bfa) return 1;
	if (w == 0xb2bc || w == 0x0c81 || w == 0x0c41) return 1; /* cmp.l / cmpi */
	if (w == 0x4239 || w == 0x4a39 || w == 0x42a9) return 1; /* clr/tst.b abs */
	if ((w & 0xf1ffu) == 0x41f9) return 1; /* lea abs */
	if ((w & 0xff00u) == 0x6000) return 1; /* bra */
	if ((w & 0xff00u) == 0x7000) return 1; /* moveq */
	return 0;
}

/* OPMDRV.X ISR terminates with RTE (expects to own the IRQ frame). Soft10C
   and many hooks use RTS. Scan a short window for which exit appears first. */
static int isrUsesRte(CHardX68k* hw, unsigned isr)
{
	if (!isr || isr >= 0xf00000u) return 0;
	for (unsigned i = 0; i < 0x200u; i += 2u) {
		const unsigned w = hw->Read16(isr + i);
		if (w == 0x4e73u) return 1; /* rte */
		if (w == 0x4e75u) return 0; /* rts */
	}
	return 0;
}

static int iocsSlotIsCodeOverlay(CHardX68k* hw, unsigned fn)
{
	/* Hoot X68k BOOTs often place runnable code on top of the IOCS jump
	   table ($400..$7FF). Writing OPMSET/OPMINTST vectors at $5A0/$5A8
	   (fn $68/$6A) smashes abtengu/albion/columns movem-search loops. */
	const unsigned slot = 0x400u + fn * 4u;
	const unsigned raw = hw->Read32(slot);
	/* Real IOCS vectors are 24-bit with high byte 00 (or FF for IPL). */
	const unsigned hi = (raw >> 24) & 0xffu;
	if (hi != 0x00u && hi != 0xffu)
		return 1;
	return looksCode(hw, slot);
}

static int iocsSlotThin(CHardX68k* hw, unsigned fn)
{
	if (iocsSlotIsCodeOverlay(hw, fn)) return 0;
	const unsigned slot = 0x400u + fn * 4u;
	const unsigned dest = hw->Read32(slot) & 0xffffffu;
	if (dest == 0 || dest == 0xffffffu) return 1;
	if (dest >= 0xff0000u) return 0; /* IPL IOCS — keep */
	return looksThinStub(hw, dest);
}

static unsigned findOpmdrvIsr(CHardX68k* hw)
{
	/* OPMDRV.X (columns/comet/dios): early init waits on a BSS flag (relocated
	   $2490 → load+$2490) before OPMINTST. Soft10C never clears it. Bind the
	   driver's own ISR from `moveq #$6A; lea ISR; trap #15` in the XML image. */
	unsigned fallback = 0;
	for (unsigned a = 0x8000u; a + 12u < 0x180000u; a += 2u) {
		if (hw->Read16(a) != 0x706au) continue;          /* moveq #$6A,d0 */
		if (hw->Read16(a + 2u) != 0x43f9u) continue;     /* lea abs.l,a1 */
		const unsigned isr = hw->Read32(a + 4u) & 0xffffffu;
		if (isr < 0x4000u || isr >= 0x180000u) continue;
		if (!looksCode(hw, isr)) continue;
		/* ISR often starts with clr.b <flag> (relocated abs). */
		if (hw->Read16(isr) == 0x4239u)
			return isr;
		if (!fallback) fallback = isr;
	}
	return fallback;
}

/* IOCS $F0 = _OPMDRV (not FLOAT FEFUNC) once OPMDRV*.X is resident.
   Dispatcher: cmp.l #$10,d1 / bcc / movem — near the "VOPM" signature.
   Signature is often at an odd address (HumanX header padding). */
static unsigned findOpmdrvF0Entry(CHardX68k* hw)
{
	for (unsigned sig = 0x8000u; sig + 8u < 0x40000u; sig++) {
		if (hw->Read8(sig) != 'V' || hw->Read8(sig + 1u) != 'O') continue;
		if (hw->Read8(sig + 2u) != 'P' || hw->Read8(sig + 3u) != 'M') continue;
		const unsigned lo = (sig > 0x800u) ? (sig - 0x800u) : 0x8000u;
		const unsigned hi = sig + 0x800u;
		for (unsigned a = lo & ~1u; a + 12u < hi; a += 2u) {
			/* cmp.l #$00000010,d1 ; bcc.s ; movem */
			if (hw->Read16(a) != 0xb2bcu) continue;
			if (hw->Read32(a + 2u) != 0x00000010u) continue;
			const unsigned br = hw->Read16(a + 6u);
			if ((br & 0xff00u) != 0x6400u && (br & 0xff00u) != 0x6500u) continue;
			if (hw->Read16(a + 8u) != 0x48e7u) continue;
			return a;
		}
	}
	return 0;
}

static void bindOpmdrvIocsF0(CHardX68k* hw)
{
	const unsigned entry = findOpmdrvF0Entry(hw);
	if (!entry) return;
	/* $400+$F0*4 == $7C0 — IOCS shortcut and our trap15 $F0 both jsr here. */
	hw->Write32(0x7c0, entry);
}

static unsigned findZmusicEntry(CHardX68k* hw)
{
	static const unsigned kDeltas[] = {
		0x08u, 0x10u, 0x50cu, 0x6a0u, 0x54cu, 0x400u, 0x200u, 0x100u, 0x80u
	};
	/* Scan low 1MB + mid window for "ZmuSiC". */
	for (unsigned base = 0; base < 0x180000u; base += 0x80000u) {
		const unsigned span = (base == 0) ? 0x100000u : 0x80000u;
		for (unsigned a = base; a + 6u < base + span; a += 2u) {
			if (hw->Read8(a) != 'Z' || hw->Read8(a + 1) != 'm') continue;
			if (hw->Read8(a + 2) != 'u' || hw->Read8(a + 3) != 'S') continue;
			if (hw->Read8(a + 4) != 'i' || hw->Read8(a + 5) != 'C') continue;
			for (unsigned di = 0; di < sizeof(kDeltas) / sizeof(kDeltas[0]); di++) {
				if (a < kDeltas[di]) continue;
				const unsigned e = a - kDeltas[di];
				if (looksCode(hw, e)) return e;
			}
			/* Signature itself sometimes sits at entry+0 (rare). */
			if (looksCode(hw, a)) return a;
		}
	}
	return 0;
}

/* First XML-placed ZMD in low RAM (header 'ZMD\0' or 'zmd\0'). */
static unsigned findZmdBuffer(CHardX68k* hw)
{
	for (unsigned a = 0x1000u; a + 4u < 0x180000u; a += 2u) {
		const unsigned b0 = hw->Read8(a);
		if (b0 != 'Z' && b0 != 'z') continue;
		const unsigned b1 = hw->Read8(a + 1);
		if (b1 != 'M' && b1 != 'm') continue;
		const unsigned b2 = hw->Read8(a + 2);
		if (b2 != 'D' && b2 != 'd') continue;
		if (hw->Read8(a + 3) != 0) continue;
		return a;
	}
	return 0;
}

static void emitIrq6Trampoline(CHardX68k* hw);

static void emitDosImage(CHardX68k* hw, unsigned zmusicEntry, unsigned zmdBuf)
{
	/* --- data --- */
	hw->Write32(CEMU_X68K_DOS_DATA + 0x00, CEMU_X68K_DOS_HEAP); /* heap bump ptr */
	hw->Write32(CEMU_X68K_DOS_DATA + 0x04, zmusicEntry);
	hw->Write32(CEMU_X68K_DOS_DATA + 0x08, zmdBuf);
	/* Minimal Human68k-ish process block for GETPB ($27). */
	const unsigned psp = CEMU_X68K_DOS_DATA + 0x20u;
	for (unsigned i = 0; i < 0x40u; i += 4u)
		hw->Write32(psp + i, 0);
	hw->Write32(psp + 0x00, 0); /* prev */
	hw->Write32(psp + 0x04, 0); /* next */
	hw->Write32(psp + 0x08, CEMU_X68K_DOS_HEAP_END); /* mem end */

	/* ========== LINE-F DOS @ F08000 ========== */
	{
		Emit e(hw, CEMU_X68K_DOS_LINEF);
		/* Musashi stacks REG_PPC = address of the $FFxx word. Human68k must
		   fetch that word and advance the stacked PC by 2 before RTE, else
		   SUPER/INTVCS loop forever (cave @ F08094, many 08=0 WEAKs). */
		e.w16(0x2440);                         /* move.l d0,a2 */
		e.w16(0x206f); e.w16(0x0002);           /* move.l 2(sp),a0 */
		e.w16(0x3018);                         /* move.w (a0)+,d0 */
		e.w16(0x2f48); e.w16(0x0002);           /* move.l a0,2(sp) */
		e.w16(0x0240); e.w16(0x00ff);           /* andi.w #$FF,d0 */

		const unsigned jTab = e.mark();
		/* cmp / beq chain — patch displacements after leaves known */
		struct J { unsigned fn; unsigned atCmp; unsigned atBeq; unsigned tgt; };
		J js[48];
		int nj = 0;
		auto addJ = [&](unsigned fn) {
			js[nj].fn = fn;
			js[nj].atCmp = e.mark();
			e.w16(0x0c40); e.w16((uint16_t)fn); /* cmpi.w #fn,d0 */
			js[nj].atBeq = e.mark();
			e.w16(0x6700); e.w16(0);            /* beq.w X */
			nj++;
		};
		addJ(0x20); /* SUPER */
		addJ(0x25); /* INTVCS */
		addJ(0x27); /* GETPB */
		addJ(0x30); /* VERNUM */
		addJ(0x35); /* INTVCG */
		addJ(0x48); /* MALLOC */
		addJ(0x49); /* MFREE */
		addJ(0x4a); /* SETBLOCK */
		addJ(0x58); /* MALLOC2 */
		addJ(0x88); /* S_MALLOC */
		addJ(0x00); /* DOS $00 */
		addJ(0x21); /* EXIT */
		addJ(0x23); /* PRINT */
		addJ(0x31); /* KEEPPR */
		addJ(0x4c); /* FILES */
		addJ(0x54); /* GETTIM2 */
		addJ(0x0b); /* KEYSNS */
		addJ(0x0c); /* GETC */
		addJ(0x0d); /* INKEY */
		/* Human68k file ops (XML-backed via $E00014..$E0001E mailbox): */
		addJ(0x3c); /* CREATE */
		addJ(0x3d); /* OPEN */
		addJ(0x3e); /* CLOSE */
		addJ(0x3f); /* READ */
		addJ(0x40); /* WRITE */
		addJ(0x43); /* SEEK */
		addJ(0x4e); /* NAMECK */
		/* Drive/path/init — L.X / OPMDRV / EXDOS bring-up (no disk plant): */
		addJ(0x41); /* DELETE */
		addJ(0x42); /* CHKDRV */
		addJ(0x44); /* CHDIR */
		addJ(0x45); /* CURDIR */
		addJ(0x46); /* FATCHK */
		addJ(0x4b); /* EXEC */
		addJ(0x50); /* GETDATE */
		addJ(0x51); /* GETTIME */
		addJ(0x56); /* DSKFRE */
		addJ(0x5a); /* ASSIGN */
		addJ(0x81); /* GETENV */
		(void)jTab;

		/* default success */
		const unsigned defOk = e.mark();
		e.w16(0x7000); /* moveq #0,d0 */
		e.w16(0x4e73); /* rte */

		/* SUPER $20: d0==0 → enter (return SSP); d0!=0 → leave accept */
		const unsigned tSuper = e.mark();
		e.w16(0x200a); /* move.l a2,d0 */
		e.w16(0x6604); /* bne.s leave */
		e.w16(0x200f); /* move.l a7,d0 */
		e.w16(0x4e73);
		/* leave */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* INTVCS $25: d1.w=vec a1=new → a1=old */
		const unsigned tIntvcs = e.mark();
		e.w16(0x3001);                         /* move.w d1,d0 */
		e.w16(0x0240); e.w16(0x00ff);         /* andi.w #$FF,d0 */
		e.w16(0x48c0);                         /* ext.l d0 */
		e.w16(0xe580);                         /* asl.l #2,d0 */
		e.w16(0x2040);                         /* move.l d0,a0 */
		e.w16(0x2010);                         /* move.l (a0),d0  old */
		e.w16(0x2089);                         /* move.l a1,(a0) new */
		e.w16(0x2240);                         /* move.l d0,a1 */
		e.w16(0x7000);                         /* moveq #0,d0 */
		e.w16(0x4e73);

		/* GETPB $27: a0/d0 = process block */
		const unsigned tGetpb = e.mark();
		e.w16(0x207c); e.w32(psp);             /* move.l #psp,a0 */
		e.w16(0x2008);                         /* move.l a0,d0 */
		e.w16(0x4e73);

		/* VERNUM $30 */
		const unsigned tVernum = e.mark();
		e.w16(0x203c); e.w32(0x03011f00u);     /* move.l #$03011F00,d0 */
		e.w16(0x4e73);

		/* INTVCG $35: d1.w=vec → a1=vector */
		const unsigned tIntvcg = e.mark();
		e.w16(0x3001);
		e.w16(0x0240); e.w16(0x00ff);
		e.w16(0x48c0);
		e.w16(0xe580);
		e.w16(0x2040);
		e.w16(0x2250);                         /* move.l (a0),a1 */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* MALLOC $48 / $58 / $88 — size was in d0, saved in a2 */
		auto emitMalloc = [&]() -> unsigned {
			const unsigned t = e.mark();
			e.w16(0x200a);                     /* move.l a2,d0 */
			e.w16(0x0680); e.w32(3);           /* addi.l #3,d0 */
			e.w16(0x0280); e.w32(0xfffffffcu); /* andi.l #~3,d0 */
			e.w16(0x2079); e.w32(CEMU_X68K_DOS_DATA); /* move.l heapPtr,a0 */
			e.w16(0x2208);                     /* move.l a0,d1 */
			e.w16(0xd1c0);                     /* adda.l d0,a0 */
			e.w16(0xb1fc); e.w32(CEMU_X68K_DOS_HEAP_END); /* cmpa.l #end,a0 */
			e.w16(0x620a);                     /* bhi.s nomem (+10) */
			e.w16(0x23c8); e.w32(CEMU_X68K_DOS_DATA); /* move.l a0,heapPtr */
			e.w16(0x2001);                     /* move.l d1,d0 */
			e.w16(0x4e73);
			e.w16(0x70ff);                     /* nomem: moveq #-1,d0 */
			e.w16(0x4e73);
			return t;
		};
		const unsigned tMalloc = emitMalloc();
		const unsigned tMalloc2 = emitMalloc();
		const unsigned tSMalloc = emitMalloc();

		/* MFREE $49 — accept, d0=0 */
		const unsigned tMfree = e.mark();
		e.w16(0x7000);
		e.w16(0x4e73);

		/* SETBLOCK $4A — accept */
		const unsigned tSetblock = e.mark();
		e.w16(0x7000);
		e.w16(0x4e73);

		/* Generic success leaves */
		auto okLeaf = [&]() -> unsigned {
			const unsigned t = e.mark();
			e.w16(0x7000);
			e.w16(0x4e73);
			return t;
		};
		const unsigned tDos0 = okLeaf();
		const unsigned tExit = okLeaf();
		const unsigned tPrint = okLeaf();
		const unsigned tKeep = okLeaf();
		/* FILES $4C — host says no more entries */
		const unsigned tFiles = e.mark();
		e.w16(0x23c9); e.w32(0x00e00018u); /* move.l a1,$E00018 */
		e.w16(0x33fc); e.w16(0x004c); e.w32(0x00e0001eu); /* move.w #$4C,$E0001E */
		e.w16(0x2039); e.w32(0x00e00018u); /* move.l $E00018,d0 */
		e.w16(0x4e73);
		const unsigned tGettim = okLeaf();
		const unsigned tKeysns = e.mark();
		e.w16(0x70ff);
		e.w16(0x4e73);
		auto keyLeaf = [&]() -> unsigned {
			const unsigned t = e.mark();
			e.w16(0x700d);
			e.w16(0x4e73);
			return t;
		};
		const unsigned tGetc = keyLeaf();
		const unsigned tInkey = keyLeaf();

		/* File ops via host mailbox: a1→$E00018, d0(a2)→$E0001C, d1→$E00014, fn→$E0001E */
		auto fileOp = [&](unsigned fn) -> unsigned {
			const unsigned t = e.mark();
			e.w16(0x23c9); e.w32(0x00e00018u); /* move.l a1,$E00018 */
			e.w16(0x23ca); e.w32(0x00e0001cu); /* move.l a2,$E0001C (saved d0) */
			e.w16(0x23c1); e.w32(0x00e00014u); /* move.l d1,$E00014 */
			e.w16(0x33fc); e.w16((uint16_t)fn); e.w32(0x00e0001eu);
			e.w16(0x2039); e.w32(0x00e00018u); /* move.l result,d0 */
			e.w16(0x4e73);
			return t;
		};
		const unsigned tCreate = fileOp(0x3c);
		const unsigned tOpen = fileOp(0x3d);
		const unsigned tClose = fileOp(0x3e);
		const unsigned tRead = fileOp(0x3f);
		const unsigned tWrite = fileOp(0x40);
		const unsigned tSeek = fileOp(0x43);
		const unsigned tNameck = fileOp(0x4e);

		/* DELETE / CHDIR / FATCHK / ASSIGN — accept */
		const unsigned tDelete = okLeaf();
		const unsigned tChdir = okLeaf();
		const unsigned tFatchk = okLeaf();
		const unsigned tAssign = okLeaf();
		/* CHKDRV: report ready 2HD-ish media byte (non-error). */
		const unsigned tChkdrv = e.mark();
		e.w16(0x203c); e.w32(0x00000038u); /* move.l #$38,d0 */
		e.w16(0x4e73);
		/* CURDIR: write "A:\\" + NUL into (a1) */
		const unsigned tCurdir = e.mark();
		e.w16(0x12bc); e.w16(0x0041); /* move.b #'A',(a1) */
		e.w16(0x137c); e.w16(0x003a); e.w16(0x0001); /* move.b #':',1(a1) */
		e.w16(0x137c); e.w16(0x005c); e.w16(0x0002); /* move.b #'\\',2(a1) */
		e.w16(0x4229); e.w16(0x0003); /* clr.b 3(a1) */
		e.w16(0x7000);
		e.w16(0x4e73);
		/* EXEC: refuse (no subprocess) — d0 = -1 */
		const unsigned tExec = e.mark();
		e.w16(0x70ff);
		e.w16(0x4e73);
		/* GETDATE: 2026-09-05 packed-ish */
		const unsigned tGetdate = e.mark();
		e.w16(0x203c); e.w32(0x00260905u);
		e.w16(0x4e73);
		/* GETTIME: noon */
		const unsigned tGettime = e.mark();
		e.w16(0x203c); e.w32(0x000c0000u);
		e.w16(0x4e73);
		/* DSKFRE: claim plenty of free clusters */
		const unsigned tDskfre = e.mark();
		e.w16(0x203c); e.w32(0x00010000u);
		e.w16(0x4e73);
		/* GETENV: not found */
		const unsigned tGetenv = e.mark();
		e.w16(0x70ff);
		e.w16(0x4e73);

		const unsigned tgts[] = {
			tSuper, tIntvcs, tGetpb, tVernum, tIntvcg, tMalloc, tMfree, tSetblock,
			tMalloc2, tSMalloc, tDos0, tExit, tPrint, tKeep, tFiles, tGettim,
			tKeysns, tGetc, tInkey, tCreate, tOpen, tClose, tRead, tWrite, tSeek, tNameck,
			tDelete, tChkdrv, tChdir, tCurdir, tFatchk, tExec, tGetdate, tGettime,
			tDskfre, tAssign, tGetenv
		};
		for (int i = 0; i < nj; i++) {
			const int rel = (int)tgts[i] - (int)(js[i].atBeq + 2);
			e.patch16(js[i].atBeq + 2, (unsigned)(rel & 0xffff));
		}
		(void)defOk;
	}

	/* ========== TRAP #15 IOCS @ F08100 ========== */
	{
		Emit e(hw, CEMU_X68K_DOS_TRAP15);
		/* fn in d0.b */
		e.w16(0x0240); e.w16(0x00ff); /* andi.w #$FF,d0 */

		struct J { unsigned atBeq; };
		unsigned at68, at6a, at6b, at6c, atf0, at86, at80, at60, at66, at67, at69, at04, at00, at01;
		auto beq = [&](unsigned fn, unsigned& slot) {
			e.w16(0x0c40); e.w16((uint16_t)fn);
			slot = e.mark();
			e.w16(0x6700); e.w16(0);
		};
		beq(0x68, at68);
		beq(0x6a, at6a);
		beq(0x6b, at6b); /* TIMERDST — arcus music tick */
		beq(0x6c, at6c); /* VDISPST */
		beq(0xf0, atf0);
		beq(0x86, at86);
		beq(0x80, at80); /* ADPCMAOT / diskred family — success */
		beq(0x60, at60); /* TIMERSET family */
		beq(0x66, at66); /* cave SND.X */
		beq(0x67, at67);
		beq(0x69, at69); /* OPMGET */
		beq(0x04, at04); /* B_SUPER / common */
		beq(0x00, at00); /* B_KEYINP */
		beq(0x01, at01); /* B_KEYSNS */
		/* default success */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $68 OPMSET: d1=reg d2=data → YM2151 */
		const unsigned t68 = e.mark();
		e.w16(0x13c1); e.w32(0x00e90001u); /* move.b d1,$E90001 */
		e.w16(0x13c2); e.w32(0x00e90003u); /* move.b d2,$E90003 */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $6A OPMINTST: a1=isr → $10C; IRQ6 vector → OS trampoline that
		   jsr's through ($10C). (Do not plant a1 directly at $78 — real
		   IOCS also goes through a soft-vector trampoline.) */
		const unsigned t6a = e.mark();
		e.w16(0x21c9); e.w16(0x010c);       /* move.l a1,$10C */
		e.w16(0x23fc); e.w32(CEMU_X68K_DOS_IRQ6); e.w32(0x78); /* move.l #IRQ6,$78 */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $6B TIMERDST: a1=hook (0=cancel), d1=unit:count — host pulses hook. */
		const unsigned t6b = e.mark();
		e.w16(0x23c9); e.w32(CEMU_X68K_DOS_DATA + 0x10u); /* move.l a1,timerD */
		e.w16(0x33c1); e.w32(CEMU_X68K_DOS_DATA + 0x14u); /* move.w d1,timerD_mode */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $6C VDISPST: a1=hook (0=cancel), d1=mode — ~60Hz host pulse. */
		const unsigned t6c = e.mark();
		e.w16(0x23c9); e.w32(CEMU_X68K_DOS_DATA + 0x18u); /* move.l a1,vdisp */
		e.w16(0x33c1); e.w32(CEMU_X68K_DOS_DATA + 0x1cu); /* move.w d1,vdisp_mode */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $F0 _OPMDRV: jsr through ($7C0). Preserve d0 — OPMDRV returns status. */
		const unsigned tf0 = e.mark();
		e.w16(0x2078); e.w16(0x07c0);       /* move.l $7C0,a0 */
		e.w16(0xb1fc); e.w32(0);            /* cmpa.l #0,a0 */
		e.w16(0x6702);                     /* beq.s skip */
		e.w16(0x4e90);                     /* jsr (a0) */
		e.w16(0x4e73);

		/* $80 _B_INTVCS: reuse IOCS-table body (RTS), then RTE. */
		const unsigned t80 = e.mark();
		e.w16(0x4eb9); e.w32(CEMU_X68K_DOS_B_INTVCS); /* jsr B_INTVCS */
		e.w16(0x4e73);

		/* success / key leaves */
		const unsigned t86 = e.mark();
		e.w16(0x7000); e.w16(0x4e73);
		const unsigned t60 = e.mark();
		e.w16(0x7000); e.w16(0x4e73);
		const unsigned t66 = e.mark();
		e.w16(0x7000); e.w16(0x4e73);
		const unsigned t67 = e.mark();
		e.w16(0x7000); e.w16(0x4e73);
		const unsigned t69 = e.mark();
		e.w16(0x7000); e.w16(0x4e73);
		const unsigned t04 = e.mark();
		e.w16(0x7000); e.w16(0x4e73);
		const unsigned t00 = e.mark();
		e.w16(0x700d); e.w16(0x4e73); /* B_KEYINP → CR */
		const unsigned t01 = e.mark();
		e.w16(0x70ff); e.w16(0x4e73); /* B_KEYSNS → key ready */

		auto patch = [&](unsigned at, unsigned tgt) {
			e.patch16(at + 2, (unsigned)((int)tgt - (int)(at + 2)) & 0xffffu);
		};
		patch(at68, t68);
		patch(at6a, t6a);
		patch(at6b, t6b);
		patch(at6c, t6c);
		patch(atf0, tf0);
		patch(at86, t86);
		patch(at80, t80);
		patch(at60, t60);
		patch(at66, t66);
		patch(at67, t67);
		patch(at69, t69);
		patch(at04, t04);
		patch(at00, t00);
		patch(at01, t01);
	}

	/* IOCS table entries use JSR/RTS (not TRAP/RTE).
	   Note: slot $F0 lives at address $7C0 — that word is the FEFUNC/_OPMDRV
	   entry pointer itself, so the stub must NOT read $7C0 again. */
	{
		Emit e(hw, CEMU_X68K_DOS_OPMSET);
		/* $68 OPMSET */
		e.w16(0x13c1); e.w32(0x00e90001u);
		e.w16(0x13c2); e.w32(0x00e90003u);
		e.w16(0x7000);
		e.w16(0x4e75); /* rts */
		/* $6A OPMINTST */
		e.w16(0x21c9); e.w16(0x010c);
		e.w16(0x23fc); e.w32(CEMU_X68K_DOS_IRQ6); e.w32(0x78);
		e.w16(0x7000);
		e.w16(0x4e75);
		/* $F0 empty FEFUNC body (only when no OPMDRV) — must RTS */
		e.w16(0x7000);
		e.w16(0x4e75); /* rts */
		/* $80 _B_INTVCS body @ F085A6 — d1 vec, a1=new, d0=old */
		e.w16(0x3001);                         /* move.w d1,d0 */
		e.w16(0x0280); e.w32(0x000001ffu);     /* andi.l #$1FF,d0 */
		e.w16(0x0c80); e.w32(0x00000100u);     /* cmpi.l #$100,d0 */
		e.w16(0x640a);                         /* bcc.s iocs */
		e.w16(0xe580);
		e.w16(0x2040);
		e.w16(0x2010);
		e.w16(0x2089);
		e.w16(0x4e75);
		e.w16(0x0240); e.w16(0x00ff);
		e.w16(0x48c0);
		e.w16(0xe580);
		e.w16(0xd0bc); e.w32(0x00000400u);
		e.w16(0x2040);
		e.w16(0x2010);
		e.w16(0x2089);
		e.w16(0x4e75);
	}

	/* ========== TRAP #3 ZMUSIC ==========
	   d1 = ZMUSIC fn. When a1==0 and fn looks like play/compile, supply the
	   first XML-placed ZMD. Do NOT clobber a1 for init (d1=0) — that crashes
	   ZMSC.X bring-up (asuka). */
	{
		Emit e(hw, CEMU_X68K_DOS_TRAP3);
		e.w16(0x2079); e.w32(CEMU_X68K_DOS_DATA + 4); /* move.l zentry,a0 */
		e.w16(0xb1fc); e.w32(0);
		e.w16(0x671e); /* beq.s nozm */
		e.w16(0xb3fc); e.w32(0);            /* cmpa.l #0,a1 */
		e.w16(0x6612);                     /* bne.s call */
		e.w16(0x0c41); e.w16(0x0008);     /* cmpi.w #8,d1 */
		e.w16(0x650c);                     /* bcs.s call (d1<8) */
		e.w16(0x0c41); e.w16(0x0020);     /* cmpi.w #$20,d1 */
		e.w16(0x6406);                     /* bcc.s call (d1>=$20) */
		e.w16(0x2279); e.w32(CEMU_X68K_DOS_DATA + 8); /* move.l zmd,a1 */
		/* call: */
		e.w16(0x4e90);
		e.w16(0x4e73);
		/* nozm: */
		e.w16(0x7000);
		e.w16(0x4e73);
	}

	/* ========== IRQ6 trampoline ========== */
	emitIrq6Trampoline(hw);

	/* TRAP#1 / exception success (RTE); IOCS hang-slot success (RTS) */
	{
		Emit e(hw, CEMU_X68K_DOS_TRAP1);
		e.w16(0x700d);
		e.w16(0x4e73);
		e.w16(0x7000);
		e.w16(0x4e75);
	}

	/* Soft $10C hang replacement — RTS (IRQ6 trampoline jsr's through $10C;
	   RTE here corrupted the exception frame and skipped ticks). */
	{
		Emit e(hw, CEMU_X68K_DOS_SOFT10C);
		e.w16(0x7000);
		e.w16(0x4e75); /* rts */
	}
}

static void emitIrq6Trampoline(CHardX68k* hw)
{
	const unsigned hook = hw->Read32(0x10c) & 0xffffffu;
	Emit e(hw, CEMU_X68K_DOS_IRQ6);
	if (isrUsesRte(hw, hook)) {
		/* OPMDRV.X: ISR does movem + … + rte on the real IRQ frame.
		   jsr would leave a 4-byte return under the ISR and rte would
		   pop garbage SR/PC (user mode, USP=0, PC in the weeds).
		   Do NOT raise IPL here — Musashi already masks at IRQ level;
		   move #$2700 left sticky IPL7 when SSP walked (a268 @ $10000). */
		e.w16(0x2078); e.w16(0x010c);       /* move.l $10C,a0 */
		e.w16(0xb1fc); e.w32(0);
		e.w16(0x6702);
		e.w16(0x4ed0);                     /* jmp (a0) */
		e.w16(0x4e73);                     /* rte (null hook) */
		/* Wipe residue of the longer jsr trampoline without hitting Soft10C. */
		e.w16(0x4e71); e.w16(0x4e71); e.w16(0x4e71); e.w16(0x4e71);
		return;
	}
	/* RTS-style Soft10C / hooks: mask, jsr, rte restores stacked IPL.
	   Do NOT move #$2500,sr before rte — level-held IRQ6 re-enters and
	   smashes this stub with exception frames. */
	e.w16(0x46fc); e.w16(0x2700);       /* move #$2700,sr */
	e.w16(0x48e7); e.w16(0xfffe);       /* movem.l d0-a6,-(sp) */
	e.w16(0x2078); e.w16(0x010c);       /* move.l $10C,a0 */
	e.w16(0xb1fc); e.w32(0);
	e.w16(0x6702);
	e.w16(0x4e90);                     /* jsr (a0) */
	e.w16(0x4cdf); e.w16(0x7fff);       /* movem.l (sp)+,d0-a6 */
	e.w16(0x4e73);                     /* rte */
}

} /* namespace */

int CEmuX68kDosInstall(CHardX68k* hw)
{
	if (!hw) return 0;

	const unsigned lineF = hw->Read32(0x2c) & 0xffffffu;
	const unsigned trap15 = hw->Read32(0xbc) & 0xffffffu;
	const unsigned trap3 = hw->Read32(0x8c) & 0xffffffu;
	const unsigned hook10c = hw->Read32(0x10c) & 0xffffffu;

	const int onOsF = ((lineF & 0xffff00u) == (CEMU_X68K_DOS_BASE & 0xffff00u));
	const int onOs15 = ((trap15 & 0xffff00u) == (CEMU_X68K_DOS_BASE & 0xffff00u));
	const int thinF = !onOsF && lineFNeedsOs(hw, lineF);
	const int thin15 = !onOs15 && trap15NeedsOs(hw, trap15);
	const int thinIocs = iocsSlotThin(hw, 0x68) || iocsSlotThin(hw, 0x6a);
	const unsigned trap1 = hw->Read32(0x84) & 0xffffffu;
	const int thinTrap1 = isHangStub(hw, trap1);
	const int thinTrap3 = isHangStub(hw, trap3);
	const int thin10c = isHangStub(hw, hook10c);
	const int soft10c = ((hook10c & 0xffffffu) == (CEMU_X68K_DOS_SOFT10C & 0xffffffu));

	/* Nothing to emit/retarget — still allow OPMDRV Soft10C→ISR upgrade + $F0. */
	if (!thinF && !thin15 && !thinIocs && !thinTrap3 && !thinTrap1 && !thin10c
		&& !soft10c && (onOsF || onOs15)) {
		bindOpmdrvIocsF0(hw);
		const unsigned isr = findOpmdrvIsr(hw);
		if (!isr) return 0;
		hw->Write32(0x10c, isr);
		emitIrq6Trampoline(hw);
		hw->Write32(0x78, CEMU_X68K_DOS_IRQ6);
		return 1;
	}
	if (!thinF && !thin15 && !thinIocs && !thinTrap3 && !thinTrap1 && !thin10c && !soft10c)
		return 0;

	const unsigned zmusic = findZmusicEntry(hw);
	const unsigned zmd = findZmdBuffer(hw);
	/* Re-emit when installing/replacing primary handlers; skip if only
	   repairing trap3/$10C after settle re-planted hang stubs. */
	if (thinF || thin15 || thinIocs || thinTrap1 || !onOsF)
		emitDosImage(hw, zmusic, zmd);
	else {
		/* Refresh ZMUSIC/ZMD pointers without resetting the bump heap. */
		hw->Write32(CEMU_X68K_DOS_DATA + 0x04, zmusic);
		hw->Write32(CEMU_X68K_DOS_DATA + 0x08, zmd);
	}

	if (thinF)
		hw->Write32(0x2c, CEMU_X68K_DOS_LINEF);
	if (thin15) {
		hw->Write32(0xbc, CEMU_X68K_DOS_TRAP15);
	}
	/* IOCS jump table — never smash BOOT code overlays; trap15 still
	   dispatches OPMSET/OPMINTST/FEFUNC when those slots are unwritable.
	   BOOT IOCS-table trampoline (akiko/can4) keeps trap15 but still needs
	   $68/$6A/$F0 bodies when thin. */
	if (thin15 || thinIocs || isBootIocsTableTrap15(hw, trap15)) {
		if (!iocsSlotIsCodeOverlay(hw, 0x68) && (thin15 || thinIocs || iocsSlotThin(hw, 0x68)))
			hw->Write32(0x400u + 0x68u * 4u, CEMU_X68K_DOS_OPMSET);
		if (!iocsSlotIsCodeOverlay(hw, 0x6a) && (thin15 || thinIocs || iocsSlotThin(hw, 0x6a)))
			hw->Write32(0x400u + 0x6au * 4u, CEMU_X68K_DOS_OPMINTST);
		if (!iocsSlotIsCodeOverlay(hw, 0x80) && (thin15 || thinIocs || iocsSlotThin(hw, 0x80)))
			hw->Write32(0x400u + 0x80u * 4u, CEMU_X68K_DOS_B_INTVCS);
		/* $400+$F0*4 == $7C0: OPMDRV dispatcher when VOPM+fn table found. */
		if (!iocsSlotIsCodeOverlay(hw, 0xf0) && (thin15 || thinIocs || iocsSlotThin(hw, 0xf0))) {
			const unsigned opm = findOpmdrvF0Entry(hw);
			hw->Write32(0x400u + 0xf0u * 4u, opm ? opm : CEMU_X68K_DOS_FEFUNC);
		}
		/* Generic IOCS success (RTS) for thin OPM-adjacent slots.
		   Skip $00/$01 — overwriting $400 smashed dios OPMDRV bring-up.
		   Skip $80/$F0 — handled above. */
		static const unsigned kOkFns[] = {
			0x60u, 0x66u, 0x67u, 0x69u, 0x86u
		};
		for (unsigned i = 0; i < sizeof(kOkFns) / sizeof(kOkFns[0]); i++) {
			const unsigned fn = kOkFns[i];
			if (!iocsSlotIsCodeOverlay(hw, fn) && iocsSlotThin(hw, fn))
				hw->Write32(0x400u + fn * 4u, CEMU_X68K_DOS_IOCS_OK);
		}
	}

	/* Thin TRAP#1 is often nop;bra* (BOOT placeholder). Hoot BOOTs also
	   plant THAT SAME address as the default for almost every exception
	   vector ($08..$FC) AND as empty IOCS table slots. Exceptions need RTE;
	   IOCS table is reached by JSR and needs RTS — never share one stub. */
	if (thinTrap1) {
		/* Stubs already in emitDosImage @ F08340/F08344. */
		hw->Write16(trap1, 0x7000);
		hw->Write16(trap1 + 2u, 0x4e75);
		for (unsigned v = 0x08u; v < 0x100u; v += 4u) {
			if ((hw->Read32(v) & 0xffffffu) != trap1)
				continue;
			if (v == 0x78u)
				hw->Write32(v, CEMU_X68K_DOS_IRQ6);
			else
				hw->Write32(v, CEMU_X68K_DOS_TRAP1);
		}
		for (unsigned slot = 0x400u; slot < 0x800u; slot += 4u) {
			const unsigned fn = (slot - 0x400u) / 4u;
			if (fn < 0x08u) continue; /* keep low IOCS / possible code overlays */
			if (iocsSlotIsCodeOverlay(hw, fn))
				continue;
			if ((hw->Read32(slot) & 0xffffffu) == trap1)
				hw->Write32(slot, CEMU_X68K_DOS_IOCS_OK);
		}
	}

	/* Thin TRAP#3 is often nop;bra* (BOOT hang). Always retarget — gate no-ops
	   if ZMUSIC was not found. Settle may re-plant this after Line-F is on OS. */
	if (thinTrap3) {
		/* Gate already in image from first emit; do not re-emit (resets heap). */
		hw->Write32(0x8c, CEMU_X68K_DOS_TRAP3);
		/* nop;bra* → rte so a trap that races into the old stub returns. */
		hw->Write16(trap3, 0x4e73);
		hw->Write16(trap3 + 2u, 0x4e73);
	}

	/* Soft OPM hook $10C: only retarget nop;bra* hang to OS RTS stub
	   (IRQ6 trampoline uses jsr — must RTS, not RTE). */
	if (thin10c) {
		Emit e(hw, CEMU_X68K_DOS_SOFT10C);
		e.w16(0x7000);
		e.w16(0x4e75); /* rts */
		hw->Write32(0x10c, CEMU_X68K_DOS_SOFT10C);
		hw->Write16(hook10c, 0x4e73);
		hw->Write16(hook10c + 2u, 0x4e73);
	}

	/* OPMDRV placed but still on Soft10C: bind its ISR so $2490 waits complete
	   without AssistSoftWaits (driver clears the flag itself). */
	{
		const unsigned h10 = hw->Read32(0x10c) & 0xffffffu;
		const int soft = (h10 == (CEMU_X68K_DOS_SOFT10C & 0xffffffu))
			|| isHangStub(hw, h10) || looksThinStub(hw, h10);
		if (soft) {
			const unsigned isr = findOpmdrvIsr(hw);
			if (isr) {
				hw->Write32(0x10c, isr);
				/* Always route IRQ6 through trampoline→$10C; BOOT's $4DE-style
				   vectors never call the OPMDRV handshake ISR. */
				emitIrq6Trampoline(hw);
				hw->Write32(0x78, CEMU_X68K_DOS_IRQ6);
			}
		}
	}

	/* Guest may install $10C without IOCS OPMINTST — still route IRQ6 through
	   the OS trampoline so timer edges reach the sequencer (OPM_WRITES WEAK).
	   Always refresh trampoline bytes when $78 already points here — a prior
	   IRQ nest storm can overwrite $F08700 with stacked SR/PC frames. */
	{
		const unsigned h10 = hw->Read32(0x10c) & 0xffffffu;
		const unsigned i6 = hw->Read32(0x78) & 0xffffffu;
		const int live10 = (h10 >= 0x400u && h10 < 0xf00000u
			&& !isHangStub(hw, h10) && !looksThinStub(hw, h10));
		const int already = (i6 == (CEMU_X68K_DOS_IRQ6 & 0xffffffu));
		const int thin6 = !already && ((i6 < 0x100u) || isHangStub(hw, i6) || looksThinStub(hw, i6));
		if (live10 && (thin6 || already)) {
			emitIrq6Trampoline(hw);
			hw->Write32(0x78, CEMU_X68K_DOS_IRQ6);
		}
	}

	/* IOCS $F0 ($7C0): prefer XML-placed OPMDRV dispatcher over empty FEFUNC.
	   FLOAT "FEfn" bind stays deferred — jumping into unloaded FLOAT early
	   exception-storms hoot BOOTs whose vectors all alias the hang site. */
	{
		const unsigned opm = findOpmdrvF0Entry(hw);
		if (opm)
			hw->Write32(0x7c0, opm);
		else if ((thinF || thin15 || thinIocs) && (hw->Read32(0x7c0) & 0xffffffu) == 0)
			hw->Write32(0x7c0, CEMU_X68K_DOS_FEFUNC);
	}

	/* If PC sits on the BOOT hang site we just patched, finish the exception. */
	if (CEmuHardX68kGetActive() == hw && thinTrap1) {
		const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
		if (pc == trap1 || pc == (trap1 + 2u)) {
			const unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
			const unsigned sr = hw->Read16(sp);
			const unsigned ret = hw->Read32(sp + 2u) & 0xffffffu;
			const int retOk = ((ret & 1u) == 0u
				&& ret > 0x100u && ret < 0xf00000u
				&& ret != pc && ret != (pc + 2u) && !isHangStub(hw, ret));
			if (retOk) {
				m68k_set_reg(M68K_REG_SR, sr);
				m68k_set_reg(M68K_REG_PC, ret);
				m68k_set_reg(M68K_REG_SP, (sp + 6u) & 0xffffffu);
				hw->SetPc(ret);
			}
		}
	}

	return 1;
}
