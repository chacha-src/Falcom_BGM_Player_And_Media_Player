#include "StdAfx.h"
#include "cemu_x68k_dos.h"
#include "cemu_hard_x68k.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>

/* CHardX68k::Write* 経由で $F0xxxx へビッグエンディアン 68000 を出す */

namespace {

struct Emit {
	CHardX68k* hw;
	unsigned pc;
	explicit Emit(CHardX68k* h, unsigned at) : hw(h), pc(at) {}
	/* w16 の実装 */
	void w16(unsigned v)
	{
		hw->Write16(pc, (uint16_t)(v & 0xffffu));
		pc += 2;
	}
	/* w32 の実装 */
	void w32(unsigned v)
	{
		hw->Write32(pc, v);
		pc += 4;
	}
	unsigned mark() const { return pc; }
	void patch16(unsigned at, unsigned v) { hw->Write16(at, (uint16_t)(v & 0xffffu)); }
};

/* isHangStub の実装 */
static int isHangStub(CHardX68k* hw, unsigned vec)
{
	/* BOOT プレースホルダ: nop; bra.s * — 空／奇数ベクタを hang 扱いしない */
	if (vec < 0x100u || vec > 0xfffff0u) return 0;
	return hw->Read16(vec) == 0x4e71u && hw->Read16(vec + 2u) == 0x60fcu;
}

/* looksThinStub の実装 */
static int looksThinStub(CHardX68k* hw, unsigned vec)
{
	if (vec < 8u || vec > 0xfffff0u) return 1;
	if (isHangStub(hw, vec)) return 1;
	const unsigned b0 = hw->Read8(vec);
	const unsigned b1 = hw->Read8(vec + 1);
	/* 入口自体は move.b #imm,$E00010; rte */
	if (b0 == 0x13 && b1 == 0xfc
		&& hw->Read8(vec + 4) == 0x00 && hw->Read8(vec + 5) == 0xe0
		&& hw->Read8(vec + 6) == 0x00 && hw->Read8(vec + 7) == 0x10)
		return 1;
	/* rte のみ／空 */
	if (b0 == 0x4e && b1 == 0x73) return 1;
	return 0;
}

/* isBootErrorLineF の実装 */
static int isBootErrorLineF(CHardX68k* hw, unsigned vec)
{
	if (vec < 8u || vec > 0xfffff0u) return 1;
	/* 本物 Human68k DOS は大きい。ブート stub は <0x40 に収まり $E00010 ストアで終わる */
	int sawErr = 0;
	for (unsigned off = 0; off < 0x40u; off++) {
		const unsigned a = vec + off;
		if (hw->Read8(a) == 0x13 && hw->Read8(a + 1) == 0xfc
			&& hw->Read8(a + 4) == 0x00 && hw->Read8(a + 5) == 0xe0
			&& hw->Read8(a + 6) == 0x00 && hw->Read8(a + 7) == 0x10)
			sawErr = 1;
	}
	if (!sawErr) return 0;
	/* 既に INTVCS/MALLOC を実装するハンドラを除外（cmpi #$25/#$48） */
	for (unsigned off = 0; off < 0x80u; off++) {
		if (hw->Read8(vec + off) == 0x0c && hw->Read8(vec + off + 1) == 0x40
			&& hw->Read8(vec + off + 2) == 0x00
			&& (hw->Read8(vec + off + 3) == 0x25 || hw->Read8(vec + off + 3) == 0x48
				|| hw->Read8(vec + off + 3) == 0x30))
			return 0;
	}
	return 1;
}

/* lineFNeedsOs の実装 */
static int lineFNeedsOs(CHardX68k* hw, unsigned vec)
{
	if (looksThinStub(hw, vec)) return 1;
	return isBootErrorLineF(hw, vec);
}

/* BOOT trap15 は数 fn を cmp 連鎖したあと $E00010 へエラー — OPMSET が無い。IOCS 表トランポリン（movem; lea $400; jsr）も — trap15 自体は残すが FEFUNC/$7C0 と OPM スロットは薄いとき我々の本体が要る。 */
static int isBootErrorTrap15(CHardX68k* hw, unsigned vec)
{
	if (vec < 8u || vec > 0xfffff0u) return 1;
	const unsigned b0 = hw->Read8(vec);
	const unsigned b1 = hw->Read8(vec + 1);
	/* cmpi.b／moveq ディスパッチに見える必要。完全 IOCS イメージではない */
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

/* BOOT trap15: `movem; move.l #$400,a6; move.l (a6,d6),a6; jsr (a6)`。IOCS 表には正しい — ベクタを置換しない。thinIocs 経由でスロットを確保。 */
static int isBootIocsTableTrap15(CHardX68k* hw, unsigned vec)
{
	if (vec < 8u || vec > 0xfffff0u) return 0;
	for (unsigned off = 0; off + 6u < 0x40u; off++) {
		if (hw->Read8(vec + off) == 0x2c && hw->Read8(vec + off + 1) == 0x7c
			&& hw->Read8(vec + off + 2) == 0x00 && hw->Read8(vec + off + 3) == 0x00
			&& hw->Read8(vec + off + 4) == 0x04 && hw->Read8(vec + off + 5) == 0x00)
			return 1; /* 命令 move.l #$400,An */
	}
	return 0;
}

/* trap15NeedsOs の実装 */
static int trap15NeedsOs(CHardX68k* hw, unsigned vec)
{
	if (looksThinStub(hw, vec)) return 1;
	if (isBootIocsTableTrap15(hw, vec)) return 0; /* 表トランポリンを残す */
	return isBootErrorTrap15(hw, vec);
}

/* looksCode の実装 */
static int looksCode(CHardX68k* hw, unsigned a)
{
	const unsigned w = ((unsigned)hw->Read8(a) << 8) | hw->Read8(a + 1);
	if (w == 0x48e7 || w == 0x4e56 || w == 0x4e75 || w == 0x2e7c) return 1;
	if (w == 0x23cf || w == 0x4eb9 || w == 0x6100) return 1;
	if (w == 0x343c || w == 0x223c || w == 0x22bc || w == 0x4e90) return 1;
	if (w == 0x41f9 || w == 0x43f9 || w == 0x2079 || w == 0x4bfa) return 1;
	if (w == 0xb2bc || w == 0x0c81 || w == 0x0c41) return 1; /* 命令 cmp.l／cmpi */
	if (w == 0x4239 || w == 0x4a39 || w == 0x42a9) return 1; /* 命令 clr/tst.b abs */
	if ((w & 0xf1ffu) == 0x41f9) return 1; /* 命令 lea abs */
	if ((w & 0xff00u) == 0x6000) return 1; /* bra 命令 */
	if ((w & 0xff00u) == 0x7000) return 1; /* moveq 命令 */
	return 0;
}

/* OPMDRV.X ISR は RTE で終わる（IRQ フレームを所有する想定）。Soft10C と多くのフックは RTS。短い窓でどちらが先に出るかスキャン。 */
static int isrUsesRte(CHardX68k* hw, unsigned isr)
{
	if (!isr || isr >= 0xf00000u) return 0;
	for (unsigned i = 0; i < 0x800u; i += 2u) {
		const unsigned w = hw->Read16(isr + i);
		if (w == 0x4e73u) return 1; /* rte 命令 */
		if (w == 0x4e75u) return 0; /* rts 命令 */
	}
	return 0;
}

/* iocsSlotIsCodeOverlay の実装 */
static int iocsSlotIsCodeOverlay(CHardX68k* hw, unsigned fn)
{
	/* Hoot X68k BOOT はしばしば実行コードを IOCS ジャンプ表（$400..$7FF）の上に置く。$5A0/$5A8（fn $68/$6A）へ OPMSET/OPMINTST ベクタを書くと abtengu/albion/columns の movem 探索ループが壊れる。 */
	const unsigned slot = 0x400u + fn * 4u;
	/* AliceSoft OPMDRV2 BOOT: init は $400..$7FF に居て $48E77FFE をスキャン。$5A0/$5A8 は 3 回目スキャン／M_INTON（dps $5A6）に入る。そこのバイトは 00xxxxxx 「ベクタ」に見える（abtengu $5A0=$00010300）か $FFxxxx（dps $5A0=$FF042348）なので上位バイト検査が外す。 */
	if (slot < 0x800u) {
		for (unsigned a = 0x400u; a + 6u < 0x800u; a += 2u) {
			if (hw->Read16(a) == 0x223Cu
				&& hw->Read32(a + 2u) == 0x48E77FFEu)
				return 1;
		}
	}
	const unsigned raw = hw->Read32(slot);
	/* 本物 IOCS ベクタは上位バイト 00 の 24bit（IPL は FF） */
	const unsigned hi = (raw >> 24) & 0xffu;
	if (hi != 0x00u && hi != 0xffu)
		return 1;
	if (hi == 0xffu && slot < 0x800u && (raw & 0xff0000u) != 0xff0000u)
		return 1;
	return looksCode(hw, slot);
}

/* iocsSlotThin の実装 */
static int iocsSlotThin(CHardX68k* hw, unsigned fn)
{
	if (iocsSlotIsCodeOverlay(hw, fn)) return 0;
	const unsigned slot = 0x400u + fn * 4u;
	const unsigned dest = hw->Read32(slot) & 0xffffffu;
	if (dest == 0 || dest == 0xffffffu) return 1;
	if (dest >= 0xff0000u) return 0; /* IPL IOCS — 残す */
	return looksThinStub(hw, dest);
}

/* hoot opmdrv.bin: $400→$B06 init、次いで jsr ($8006) と $D200 から $48E77FFE の 1000 ワードスキャン。OPMDRV.X の IRQ6 ISR を先バインド（findOpmdrvIsr）するとタイマエッジがスキャン前にその目印を壊し、init が $B32 で回り M_ALLOC/M_INIT に届かない。columns/comet は別グルーでまだ早期 ISR バインドが要る。 */
static int isOpmdrvBinGlue(CHardX68k* hw)
{
	if ((hw->Read32(0x400) & 0xffffffu) != 0xB06u) return 0;
	if (hw->Read16(0xB16) != 0x223Cu) return 0;
	if (hw->Read32(0xB18) != 0x48E77FFEu) return 0;
	return 1;
}

/* グルー init はまだ OPMDRV.X で $48E77FFE を歩いている。ISR は未バインドのまま */
static int opmdrvBinScanPending(CHardX68k* hw)
{
	if (!isOpmdrvBinGlue(hw)) return 0;
	int k;
	for (k = 0; k < 1000; k++) {
		const unsigned a = 0xD200u + (unsigned)k * 2u;
		if (hw->Read32(a) == 0x48E77FFEu) return 1;
	}
	return 0;
}

/* OPDRV.X（SystemSoft bltzkr/bomber/mofm2）: OPMINTST `moveq #$6A` 無し。Init は `lea ISR(pc),a0; move.l a0,$10C`（または `move.l #ISR,$10C`）し、hang $10C がクリアしない BSS フラグを待つ。 */
static unsigned findOpdrvIsr(CHardX68k* hw)
{
	unsigned fallback = 0;
	for (unsigned a = 0x8000u; a + 12u < 0x40000u; a += 2u) {
		unsigned isr = 0;
		if (hw->Read16(a) == 0x41fau && hw->Read16(a + 4u) == 0x23c8u
			&& hw->Read32(a + 6u) == 0x10cu) {
			/* lea d16(pc),a0; move.l a0,$10C。d16 の PC は拡張ワード */
			const int disp = (int)(int16_t)hw->Read16(a + 2u);
			isr = (unsigned)((int)a + 2 + disp) & 0xffffffu;
		} else if (hw->Read16(a) == 0x23fcu && hw->Read32(a + 6u) == 0x10cu) {
			isr = hw->Read32(a + 2u) & 0xffffffu;
		} else {
			continue;
		}
		if (isr < 0x8000u || isr >= 0x40000u) continue;
		if (!looksCode(hw, isr)) continue;
		if (hw->Read16(isr) == 0x48e7u)
			return isr;
		if (!fallback) fallback = isr;
	}
	return fallback;
}

/* findOpmdrvIsr の実装 */
static unsigned findOpmdrvIsr(CHardX68k* hw)
{
	/* OPMDRV.X（columns/comet/dios）: 早期 init は OPMINTST 前に BSS フラグ（再配置 $2490 → load+$2490）を待つ。Soft10C はクリアしない。XML イメージの `moveq #$6A; lea ISR; trap #15` からドライバ自身の ISR をバインド。 */
	unsigned fallback = 0;
	for (unsigned a = 0x8000u; a + 12u < 0x180000u; a += 2u) {
		if (hw->Read16(a) != 0x706au) continue;          /* 命令 moveq #$6A,d0 */
		if (hw->Read16(a + 2u) != 0x43f9u) continue;     /* 命令 lea abs.l,a1 */
		const unsigned isr = hw->Read32(a + 4u) & 0xffffffu;
		if (isr < 0x4000u || isr >= 0x180000u) continue;
		if (!looksCode(hw, isr)) continue;
		/* ISR はしばしば clr.b <flag>（再配置 abs）で始まる */
		if (hw->Read16(isr) == 0x4239u)
			return isr;
		if (!fallback) fallback = isr;
	}
	if (fallback)
		return fallback;
	return findOpdrvIsr(hw);
}

/* IOCS $F0 = _OPMDRV（FLOAT FEFUNC ではない）。OPMDRV*.X 常駐後。ディスパッチャ: cmp.l #$10,d1／bcc／movem — 「VOPM」シグネチャ付近。シグネチャは奇数番地が多い（HumanX ヘッダパディング）。 */
static unsigned findOpmdrvF0Entry(CHardX68k* hw)
{
	for (unsigned sig = 0x8000u; sig + 8u < 0x40000u; sig++) {
		if (hw->Read8(sig) != 'V' || hw->Read8(sig + 1u) != 'O') continue;
		if (hw->Read8(sig + 2u) != 'P' || hw->Read8(sig + 3u) != 'M') continue;
		const unsigned lo = (sig > 0x800u) ? (sig - 0x800u) : 0x8000u;
		const unsigned hi = sig + 0x800u;
		for (unsigned a = lo & ~1u; a + 12u < hi; a += 2u) {
			/* 命令 cmp.l #$00000010,d1 ; bcc.s ; movem */
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

/* bindOpmdrvIocsF0 の実装 */
static void bindOpmdrvIocsF0(CHardX68k* hw)
{
	const unsigned entry = findOpmdrvF0Entry(hw);
	if (!entry) return;
	/* $400+$F0*4 == $7C0 — IOCS ショートカットと trap15 $F0 の両方がここを jsr */
	hw->Write32(0x7c0, entry);
}

/* findZmusicEntry の実装 */
static unsigned findZmusicEntry(CHardX68k* hw)
{
	/* 公式 Z-MUSIC v2 常駐検査（libzm2internal.h／zmusic2 README）:
	     move.l $8c,a0／subq.w #8,a0／cmpi.l #'ZmuS'／cmpi.w #'iC'
	     entry-2 のバージョンワードは < $3000（ZMSC3.X を除外）。
	   よって trap #3 は ident+8。ZMD 曲は "\x10ZmuSiC" — それらを飛ばす。
	   ZMSC.X ファイルレイアウトはまだ ident を entry+$50C／+$6A0 に使う。 */
	static const unsigned kFileDeltas[] = { 0x50cu, 0x6a0u, 0x54cu };
	unsigned fileHit = 0;
	for (unsigned base = 0; base < 0x400000u; base += 0x100000u) {
		const unsigned span = 0x100000u;
		for (unsigned a = base; a + 8u < base + span; a += 2u) {
			if (hw->Read8(a) != 'Z' || hw->Read8(a + 1) != 'm') continue;
			if (hw->Read8(a + 2) != 'u' || hw->Read8(a + 3) != 'S') continue;
			if (hw->Read8(a + 4) != 'i' || hw->Read8(a + 5) != 'C') continue;
			if (a >= 1u && hw->Read8(a - 1u) == 0x10u)
				continue;
			const unsigned ver = hw->Read16(a + 6u);
			if (ver >= 0x3000u)
				continue;
			const unsigned e = a + 8u;
			if (hw->Read16(e) == 0x48e7u)
				return e;
			if (!fileHit) {
				for (unsigned di = 0; di < sizeof(kFileDeltas) / sizeof(kFileDeltas[0]); di++) {
					if (a < kFileDeltas[di]) continue;
					const unsigned fe = a - kFileDeltas[di];
					const unsigned w = hw->Read16(fe);
					if (w == 0x4e75u)
						continue;
					if (looksCode(hw, fe)) {
						fileHit = fe;
						break;
					}
				}
			}
		}
	}
	return fileHit;
}

/* 低 RAM に XML が置いた最初の ZMD（ヘッダ 'ZMD\0' または 'zmd\0'） */
static unsigned findZmdBuffer(CHardX68k* hw)
{
	for (unsigned a = 0x1000u; a + 4u < 0x400000u; a += 2u) {
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

/* emitDosImage の実装 */
static void emitDosImage(CHardX68k* hw, unsigned zmusicEntry, unsigned zmdBuf)
{
	/* --- データ --- */
	hw->Write32(CEMU_X68K_DOS_DATA + 0x00, CEMU_X68K_DOS_HEAP); /* ヒープバンプポインタ */
	hw->Write32(CEMU_X68K_DOS_DATA + 0x04, zmusicEntry);
	hw->Write32(CEMU_X68K_DOS_DATA + 0x08, zmdBuf);
	/* GETPB（$27）用の最小 Human68k 風プロセスブロック */
	const unsigned psp = CEMU_X68K_DOS_DATA + 0x20u;
	for (unsigned i = 0; i < 0x40u; i += 4u)
		hw->Write32(psp + i, 0);
	hw->Write32(psp + 0x00, 0); /* 前 */
	hw->Write32(psp + 0x04, 0); /* 次 */
	hw->Write32(psp + 0x08, CEMU_X68K_DOS_HEAP_END); /* メモリ終端 */

	/* Drive/path/init — L.X／OPMDRV／EXDOS 立ち上げ（ディスク植込無し） */
	{
		Emit e(hw, CEMU_X68K_DOS_LINEF);
		/* Musashi は REG_PPC に $FFxx ワードの番地を積む。Human68k はそのワードを fetch し RTE 前に積んだ PC を 2 進める必要がある。さもなくば SUPER/INTVCS が永久ループ（cave @ F08094、多くの 08=0 WEAK）。 */
		/* Human68k は DOS 呼び出しが d0 以外の全レジスタを保存すると保証するので、入ってくる d0 は a2 ではなくメモリへ置く。a2 に持つと呼び出し元が黙って壊れた: OPMDRV.X は _SUPER を跨いで a2 からコマンドラインを読むのでゴミを解析し usage を出して OPM を組まず終了した。 */
		e.w16(0x23c0); e.w32(CEMU_X68K_DOS_DATA + 0x0cu); /* 命令 move.l d0,argD0 */
		e.w16(0x206f); e.w16(0x0002);           /* 命令 move.l 2(sp),a0 */
		e.w16(0x3018);                         /* 命令 move.w (a0)+,d0 */
		e.w16(0x2f48); e.w16(0x0002);           /* 命令 move.l a0,2(sp) */
		e.w16(0x0240); e.w16(0x00ff);           /* 命令 andi.w #$FF,d0 */

		const unsigned jTab = e.mark();
		/* cmp／beq 連鎖 — リーフ確定後に変位をパッチ */
		struct J { unsigned fn; unsigned atCmp; unsigned atBeq; unsigned tgt; };
		J js[48];
		int nj = 0;
		auto addJ = [&](unsigned fn) {
			js[nj].fn = fn;
			js[nj].atCmp = e.mark();
			e.w16(0x0c40); e.w16((uint16_t)fn); /* 命令 cmpi.w #fn,d0 */
			js[nj].atBeq = e.mark();
			e.w16(0x6700); e.w16(0);            /* 命令 beq.w X */
			nj++;
		};
		addJ(0x20); /* SUPER コール */
		addJ(0x25); /* INTVCS コール */
		addJ(0x27); /* GETPB コール */
		addJ(0x30); /* VERNUM コール */
		addJ(0x35); /* INTVCG コール */
		addJ(0x48); /* MALLOC コール */
		addJ(0x49); /* MFREE コール */
		addJ(0x4a); /* SETBLOCK コール */
		addJ(0x58); /* MALLOC2 コール */
		addJ(0x88); /* S_MALLOC コール */
		addJ(0x00); /* DOS コール $00 */
		addJ(0x21); /* EXIT コール */
		addJ(0x23); /* PRINT コール */
		addJ(0x31); /* KEEPPR コール */
		addJ(0x4c); /* FILES コール */
		addJ(0x54); /* GETTIM2 コール */
		addJ(0x0b); /* KEYSNS コール */
		addJ(0x0c); /* GETC コール */
		addJ(0x0d); /* INKEY コール */
		/* Human68k ファイル操作（$E00014..$E0001E メールボックス経由 XML 裏） */
		addJ(0x3c); /* CREATE コール */
		addJ(0x3d); /* OPEN コール */
		addJ(0x3e); /* CLOSE コール */
		addJ(0x3f); /* READ コール */
		addJ(0x40); /* WRITE コール */
		addJ(0x43); /* SEEK コール */
		addJ(0x4e); /* NAMECK コール */
		/* DELETE コール */
		addJ(0x41); /* CHKDRV コール */
		addJ(0x42); /* CHDIR コール */
		addJ(0x44); /* CURDIR コール */
		addJ(0x45); /* FATCHK コール */
		addJ(0x46); /* GETDATE コール */
		addJ(0x4b); /* EXEC */
		addJ(0x50); /* GETTIME コール */
		addJ(0x51); /* DSKFRE コール */
		addJ(0x56); /* ASSIGN コール */
		addJ(0x5a); /* GETENV コール */
		addJ(0x81); /* 既定成功 */
		(void)jTab;

		/* 命令 moveq #0,d0 */
		const unsigned defOk = e.mark();
		e.w16(0x7000); /* 命令 rte */
		e.w16(0x4e73); /* SUPER $20: d0==0 → 入る（SSP を返す）。d0!=0 → 離脱を受け入れる */

		/* 命令 move.l argD0,d0 */
		const unsigned tSuper = e.mark();
		e.w16(0x2039); e.w32(CEMU_X68K_DOS_DATA + 0x0cu); /* 命令 bne.s leave */
		e.w16(0x6604); /* 命令 move.l a7,d0 */
		e.w16(0x200f); /* 離脱 leave */
		e.w16(0x4e73);
		/* INTVCS $25: d1.w=vec a1=new → a1=old（ベクタ入替） */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* 命令 move.w d1,d0 */
		const unsigned tIntvcs = e.mark();
		e.w16(0x3001);                         /* 命令 andi.w #$FF,d0 */
		e.w16(0x0240); e.w16(0x00ff);         /* 命令 ext.l d0 */
		e.w16(0x48c0);                         /* 命令 asl.l #2,d0 */
		e.w16(0xe580);                         /* 命令 move.l d0,a0 */
		e.w16(0x2040);                         /* 命令 move.l (a0),d0  旧 */
		e.w16(0x2010);                         /* 命令 move.l a1,(a0) 新 */
		e.w16(0x2089);                         /* 命令 move.l d0,a1 */
		e.w16(0x2240);                         /* 命令 moveq #0,d0 */
		e.w16(0x7000);                         /* GETPB $27: a0/d0 = プロセスブロック */
		e.w16(0x4e73);

		/* 命令 move.l #psp,a0 */
		const unsigned tGetpb = e.mark();
		e.w16(0x207c); e.w32(psp);             /* 命令 move.l a0,d0 */
		e.w16(0x2008);                         /* VERNUM $30 コール */
		e.w16(0x4e73);

		/* 命令 move.l #$03011F00,d0 */
		const unsigned tVernum = e.mark();
		e.w16(0x203c); e.w32(0x03011f00u);     /* INTVCG $35: d1.w=vec → a1=ベクタ */
		e.w16(0x4e73);

		/* 命令 move.l (a0),a1 */
		const unsigned tIntvcg = e.mark();
		e.w16(0x3001);
		e.w16(0x0240); e.w16(0x00ff);
		e.w16(0x48c0);
		e.w16(0xe580);
		e.w16(0x2040);
		e.w16(0x2250);                         /* MALLOC $48／$58／$88 — サイズは d0、argD0 に退避 */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* 命令 move.l argD0,d0 */
		auto emitMalloc = [&]() -> unsigned {
			const unsigned t = e.mark();
			e.w16(0x2039); e.w32(CEMU_X68K_DOS_DATA + 0x0cu); /* 命令 addi.l #3,d0 */
			e.w16(0x0680); e.w32(3);           /* 命令 andi.l #~3,d0 */
			e.w16(0x0280); e.w32(0xfffffffcu); /* 命令 move.l heapPtr,a0 */
			e.w16(0x2079); e.w32(CEMU_X68K_DOS_DATA); /* 命令 move.l a0,d1 */
			e.w16(0x2208);                     /* 命令 adda.l d0,a0 */
			e.w16(0xd1c0);                     /* 命令 cmpa.l #end,a0 */
			e.w16(0xb1fc); e.w32(CEMU_X68K_DOS_HEAP_END); /* 命令 bhi.s nomem (+10) */
			e.w16(0x620a);                     /* 命令 move.l a0,heapPtr */
			e.w16(0x23c8); e.w32(CEMU_X68K_DOS_DATA); /* 命令 move.l d1,d0 */
			e.w16(0x2001);                     /* nomem: moveq #-1,d0 メモリ不足 */
			e.w16(0x4e73);
			e.w16(0x70ff);                     /* MFREE $49 — 受け入れ、d0=0 */
			e.w16(0x4e73);
			return t;
		};
		const unsigned tMalloc = emitMalloc();
		const unsigned tMalloc2 = emitMalloc();
		const unsigned tSMalloc = emitMalloc();

		/* SETBLOCK $4A — 受け入れ */
		const unsigned tMfree = e.mark();
		e.w16(0x7000);
		e.w16(0x4e73);

		/* 汎用成功リーフ */
		const unsigned tSetblock = e.mark();
		e.w16(0x7000);
		e.w16(0x4e73);

		/* FILES $4C — ホストはこれ以上エントリ無し */
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
		/* 命令 move.l a1,$E00018 */
		const unsigned tFiles = e.mark();
		e.w16(0x23c9); e.w32(0x00e00018u); /* 命令 move.w #$4C,$E0001E */
		e.w16(0x33fc); e.w16(0x004c); e.w32(0x00e0001eu); /* 命令 move.l $E00018,d0 */
		e.w16(0x2039); e.w32(0x00e00018u); /* ホストメールボックス経由のファイル操作: a1→$E00018、argD0→$E0001C、d1→$E00014、fn→$E0001E */
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

		/* 命令 move.l a1,$E00018 */
		auto fileOp = [&](unsigned fn) -> unsigned {
			const unsigned t = e.mark();
			e.w16(0x23c9); e.w32(0x00e00018u); /* 命令 move.l argD0,$E0001C */
			e.w16(0x23f9); e.w32(CEMU_X68K_DOS_DATA + 0x0cu);
			e.w32(0x00e0001cu);                /* 命令 move.l d1,$E00014 */
			e.w16(0x23c1); e.w32(0x00e00014u); /* 命令 move.l result,d0 */
			e.w16(0x33fc); e.w16((uint16_t)fn); e.w32(0x00e0001eu);
			e.w16(0x2039); e.w32(0x00e00018u); /* DELETE／CHDIR／FATCHK／ASSIGN — 受け入れ */
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

		/* CHKDRV: ready な 2HD 風メディアバイトを報告（エラーではない） */
		const unsigned tDelete = okLeaf();
		const unsigned tChdir = okLeaf();
		const unsigned tFatchk = okLeaf();
		const unsigned tAssign = okLeaf();
		/* 命令 move.l #$38,d0 */
		const unsigned tChkdrv = e.mark();
		e.w16(0x203c); e.w32(0x00000038u); /* CURDIR: (a1) へ "A:\\"＋NUL を書く */
		e.w16(0x4e73);
		/* 命令 move.b #'A',(a1) */
		const unsigned tCurdir = e.mark();
		e.w16(0x12bc); e.w16(0x0041); /* 命令 move.b #':',1(a1) */
		e.w16(0x137c); e.w16(0x003a); e.w16(0x0001); /* 命令 move.b #'\\',2(a1) */
		e.w16(0x137c); e.w16(0x005c); e.w16(0x0002); /* 命令 clr.b 3(a1) */
		e.w16(0x4229); e.w16(0x0003); /* EXEC: 拒否（サブプロセス無し）— d0 = -1 */
		e.w16(0x7000);
		e.w16(0x4e73);
		/* GETDATE: 2026-09-05 パック風 */
		const unsigned tExec = e.mark();
		e.w16(0x70ff);
		e.w16(0x4e73);
		/* GETTIME: 正午 */
		const unsigned tGetdate = e.mark();
		e.w16(0x203c); e.w32(0x00260905u);
		e.w16(0x4e73);
		/* DSKFRE: 空きクラスタを十分あると主張 */
		const unsigned tGettime = e.mark();
		e.w16(0x203c); e.w32(0x000c0000u);
		e.w16(0x4e73);
		/* GETENV: 見つからない */
		const unsigned tDskfre = e.mark();
		e.w16(0x203c); e.w32(0x00010000u);
		e.w16(0x4e73);
		/* ========== TRAP #15 IOCS @ F08100（入口） ========== */
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

	/* fn は d0.b */
	{
		Emit e(hw, CEMU_X68K_DOS_TRAP15);
		/* 命令 andi.w #$FF,d0 */
		e.w16(0x0240); e.w16(0x00ff); /* TIMERDST — arcus 音楽 tick */

		struct J { unsigned atBeq; };
		unsigned at68, at6a, at6b, at6c, atf0, at86, at80, at69, at04, at00, at01;
		auto beq = [&](unsigned fn, unsigned& slot) {
			e.w16(0x0c40); e.w16((uint16_t)fn);
			slot = e.mark();
			e.w16(0x6700); e.w16(0);
		};
		beq(0x68, at68);
		beq(0x6a, at6a);
		beq(0x6b, at6b); /* VDISPST コール */
		beq(0x6c, at6c); /* _B_INTVCS コール */
		beq(0xf0, atf0);
		beq(0x86, at86);
		beq(0x80, at80); /* _B_INTVCS 入口 */
		beq(0x69, at69); /* OPMSNS 入口 */
		beq(0x04, at04); /* B_SUPER／共通 */
		beq(0x00, at00); /* B_KEYINP 入口 */
		beq(0x01, at01); /* B_KEYSNS 入口 */
		/* $60..$67 は MSM6258 呼び出し（_ADPCMOUT.._ADPCMMOD）。CODE-ZERO の MUCO.X 等は IOCS ジャンプ表に独自本体を入れ HD63450 を組む。ここで「成功」と返すとそれらのリップは OPM は鳴るが PCM ノートが 1 つも出ない。スロットを所有していればゲストハンドラを走らせる。null、我々（$F0xxxx）、BOOT hang stub は缶詰成功へ落ちる。ADPCM 範囲に限定 — 未知 fn を全部表経由にすると BOOT コードオーバーレイへ歩く。 */
		unsigned atUnder, atOver, atMask, atLow, atHigh, atStub, atGo;
		e.w16(0x0c40); e.w16(0x0060);       /* 命令 cmpi.w #$60,d0 */
		atUnder = e.mark(); e.w16(0x6500);  /* 命令 bcs.w fail */ e.w16(0);
		e.w16(0x0c40); e.w16(0x0068);       /* 命令 cmpi.w #$68,d0 */
		atOver = e.mark(); e.w16(0x6400);   /* 命令 bcc.w fail */ e.w16(0);
		/* インストール時許可ビット: $400..$7FF に実行コードを重ねる hoot BOOT はディスパッチするベクタが無く、そのバイトスープへ jsr するとリップが死ぬ（xenon）。movem はフラグを触らないので btst と分岐の間で d0 を戻す。IOCS は d0 で戻り他は保存、a0 も含む — a0 経由で表を歩き保存しないと呼び出し元が黙って壊れた（gramcat2）。スタックではなく DOS ワークへ置く: rte で戻るゲストハンドラは rts ではなく保存コピーを残し SP を呼び出し毎に下へ歩く。 */
		e.w16(0x23c8); e.w32(CEMU_X68K_DOS_DATA + 0x60u); /* 命令 move.l a0,saveA0 */
		e.w16(0x3f00);                      /* 命令 move.w d0,-(sp) */
		e.w16(0x0440); e.w16(0x0060);       /* 命令 subi.w #$60,d0 */
		e.w16(0x0139); e.w32(CEMU_X68K_DOS_DATA + 0x1eu); /* 命令 btst d0,mask */
		e.w16(0x4c9f); e.w16(0x0001);       /* 命令 movem.w (sp)+,d0 */
		atMask = e.mark(); e.w16(0x6700);   /* 命令 beq.w fail */ e.w16(0);
		e.w16(0x3f00);                      /* 命令 move.w d0,-(sp) */
		e.w16(0xe548);                      /* 命令 lsl.w #2,d0 */
		e.w16(0x207c); e.w32(0x00000400u);  /* 命令 movea.l #$400,a0 */
		e.w16(0xd0c0);                      /* 命令 adda.w d0,a0 */
		e.w16(0x2050);                      /* 命令 movea.l (a0),a0 */
		e.w16(0x301f);                      /* 命令 move.w (sp)+,d0 */
		e.w16(0xb1fc); e.w32(0x00001000u);  /* 命令 cmpa.l #$1000,a0 */
		atLow = e.mark(); e.w16(0x6500);    /* 命令 bcs.w fail */ e.w16(0);
		e.w16(0xb1fc); e.w32(0x00f00000u);  /* 命令 cmpa.l #$F00000,a0 */
		atHigh = e.mark(); e.w16(0x6400);   /* 命令 bcc.w fail */ e.w16(0);
		e.w16(0x0c50); e.w16(0x4e71);       /* 命令 cmpi.w #$4E71,(a0) */
		atGo = e.mark(); e.w16(0x6600);     /* 命令 bne.w go */ e.w16(0);
		e.w16(0x0c68); e.w16(0x60fc); e.w16(0x0002); /* 命令 cmpi.w #$60FC,2(a0) */
		atStub = e.mark(); e.w16(0x6700);   /* 命令 beq.w fail */ e.w16(0);
		const unsigned tGo = e.mark();
		e.w16(0x4e90);                      /* 命令 jsr (a0) */
		e.w16(0x2079); e.w32(CEMU_X68K_DOS_DATA + 0x60u); /* 命令 movea.l saveA0,a0 */
		e.w16(0x4e73);                      /* 命令 rte */
		/* a0 をパークしたあとの脱出はそれを戻す。他 fn が落ちる素の既定は a0 を触ってはいけない。 */
		const unsigned tFailA0 = e.mark();
		e.w16(0x2079); e.w32(CEMU_X68K_DOS_DATA + 0x60u); /* 命令 movea.l saveA0,a0 */
		const unsigned tFail = e.mark();
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $68 OPMSET: d1=reg d2=data → YM2151 書き */
		const unsigned t68 = e.mark();
		e.w16(0x13c1); e.w32(0x00e90001u); /* 命令 move.b d1,$E90001 */
		e.w16(0x13c2); e.w32(0x00e90003u); /* 命令 move.b d2,$E90003 */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $6A OPMINTST: a1=isr → $10C。IRQ6 ベクタ → ($10C) を jsr する OS トランポリン。（a1 を $78 へ直接植えない — 実 IOCS もソフトベクタトランポリン経由。） */
		const unsigned t6a = e.mark();
		e.w16(0x21c9); e.w16(0x010c);       /* 命令 move.l a1,$10C */
		e.w16(0x23fc); e.w32(CEMU_X68K_DOS_IRQ6); e.w32(0x78); /* 命令 move.l #IRQ6,$78 */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $6B TIMERDST: a1=hook（0=取消）、d1=unit:count — ホストが hook をパルス */
		const unsigned t6b = e.mark();
		e.w16(0x23c9); e.w32(CEMU_X68K_DOS_DATA + 0x10u); /* 命令 move.l a1,timerD */
		e.w16(0x33c1); e.w32(CEMU_X68K_DOS_DATA + 0x14u); /* 命令 move.w d1,timerD_mode */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $6C VDISPST: a1=hook（0=取消）、d1=mode — ホスト約 60Hz パルス */
		const unsigned t6c = e.mark();
		e.w16(0x23c9); e.w32(CEMU_X68K_DOS_DATA + 0x18u); /* 命令 move.l a1,vdisp */
		e.w16(0x33c1); e.w32(CEMU_X68K_DOS_DATA + 0x1cu); /* 命令 move.w d1,vdisp_mode */
		e.w16(0x7000);
		e.w16(0x4e73);

		/* $F0 _OPMDRV: ($7C0) を jsr。d0 を保存 — OPMDRV はステータスを返す */
		const unsigned tf0 = e.mark();
		e.w16(0x2078); e.w16(0x07c0);       /* 命令 move.l $7C0,a0 */
		e.w16(0xb1fc); e.w32(0);            /* 命令 cmpa.l #0,a0 */
		e.w16(0x6702);                     /* 命令 beq.s skip */
		e.w16(0x4e90);                     /* 命令 jsr (a0) */
		e.w16(0x4e73);

		/* $80 _B_INTVCS: IOCS 表本体（RTS）を再利用し RTE */
		const unsigned t80 = e.mark();
		e.w16(0x4eb9); e.w32(CEMU_X68K_DOS_B_INTVCS); /* 命令 jsr B_INTVCS */
		e.w16(0x4e73);

		/* 成功／キーリーフ */
		const unsigned t86 = e.mark();
		e.w16(0x7000); e.w16(0x4e73);
		const unsigned t69 = e.mark();
		e.w16(0x7000); e.w16(0x4e73);
		const unsigned t04 = e.mark();
		e.w16(0x7000); e.w16(0x4e73);
		const unsigned t00 = e.mark();
		e.w16(0x700d); e.w16(0x4e73); /* B_KEYINP → CR 入力 */
		const unsigned t01 = e.mark();
		e.w16(0x70ff); e.w16(0x4e73); /* B_KEYSNS → キー ready */

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
		patch(at69, t69);
		patch(atUnder, tFail);
		patch(atOver, tFail);
		patch(atMask, tFailA0);
		patch(atLow, tFailA0);
		patch(atHigh, tFailA0);
		patch(atStub, tFailA0);
		patch(atGo, tGo);
		patch(at04, t04);
		patch(at00, t00);
		patch(at01, t01);
	}

	/* IOCS 表エントリは JSR/RTS（TRAP/RTE ではない）。スロット $F0 は番地 $7C0 — そのワードは FEFUNC/_OPMDRV 入口ポインタ自身なので stub は $7C0 を再読してはいけない。 */
	{
		Emit e(hw, CEMU_X68K_DOS_OPMSET);
		/* $68 OPMSET 入口 */
		e.w16(0x13c1); e.w32(0x00e90001u);
		e.w16(0x13c2); e.w32(0x00e90003u);
		e.w16(0x7000);
		e.w16(0x4e75); /* 命令 rts */
		/* $6A OPMINTST 入口 */
		e.w16(0x21c9); e.w16(0x010c);
		e.w16(0x23fc); e.w32(CEMU_X68K_DOS_IRQ6); e.w32(0x78);
		e.w16(0x7000);
		e.w16(0x4e75);
		/* $F0 空 FEFUNC 本体（OPMDRV が無いときだけ）— RTS 必須 */
		e.w16(0x7000);
		e.w16(0x4e75); /* 命令 rts */
		/* $80 _B_INTVCS 本体 @ F085A6 — d1 vec、a1=new、d0=old */
		e.w16(0x3001);                         /* 命令 move.w d1,d0 */
		e.w16(0x0280); e.w32(0x000001ffu);     /* 命令 andi.l #$1FF,d0 */
		e.w16(0x0c80); e.w32(0x00000100u);     /* 命令 cmpi.l #$100,d0 */
		e.w16(0x640a);                         /* 命令 bcc.s iocs */
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
	   d1 = ZMUSIC fn。a1==0 かつ fn が play/compile に見えるとき、XML が置いた最初の ZMD を渡す。init（d1=0）で a1 を壊さない — ZMSC.X 立ち上げがクラッシュする（asuka）。 */
	{
		Emit e(hw, CEMU_X68K_DOS_TRAP3);
		e.w16(0x2079); e.w32(CEMU_X68K_DOS_DATA + 4); /* 命令 move.l zentry,a0 */
		e.w16(0xb1fc); e.w32(0);
		e.w16(0x671e); /* 命令 beq.s nozm */
		e.w16(0xb3fc); e.w32(0);            /* 命令 cmpa.l #0,a1 */
		e.w16(0x6612);                     /* 命令 bne.s call */
		e.w16(0x0c41); e.w16(0x0008);     /* 命令 cmpi.w #8,d1 */
		e.w16(0x650c);                     /* 命令 bcs.s call（d1<8） */
		e.w16(0x0c41); e.w16(0x0020);     /* 命令 cmpi.w #$20,d1 */
		e.w16(0x6406);                     /* 命令 bcc.s call（d1>=$20） */
		e.w16(0x2279); e.w32(CEMU_X68K_DOS_DATA + 8); /* 命令 move.l zmd,a1 */
		/* 呼び出し call: */
		e.w16(0x4e90);
		e.w16(0x4e73);
		/* 失敗 nozm: */
		e.w16(0x7000);
		e.w16(0x4e73);
	}

	/* ========== IRQ6 トランポリン ========== */
	emitIrq6Trampoline(hw);

	/* TRAP#1／例外成功（RTE）。IOCS hang スロット成功（RTS） */
	{
		Emit e(hw, CEMU_X68K_DOS_TRAP1);
		e.w16(0x700d);
		e.w16(0x4e73);
		e.w16(0x7000);
		e.w16(0x4e75);
	}

	/* Soft $10C hang 置換 — RTS（IRQ6 トランポリンは $10C を jsr。ここで RTE すると例外フレームが壊れ tick を飛ばす）。 */
	{
		Emit e(hw, CEMU_X68K_DOS_SOFT10C);
		e.w16(0x7000);
		e.w16(0x4e75); /* 命令 rts */
	}
}

/* IRQ 配送 */
static void emitIrq6Trampoline(CHardX68k* hw)
{
	const unsigned hook = hw->Read32(0x10c) & 0xffffffu;
	Emit e(hw, CEMU_X68K_DOS_IRQ6);
	if (isrUsesRte(hw, hook)) {
		/* OPMDRV.X: ISR は本物 IRQ フレーム上で movem＋…＋rte。jsr だと ISR の下に 4 バイト戻りが残り rte がゴミ SR/PC を pop（ユーザモード、USP=0、PC が野）。ここで IPL を上げない — Musashi は既に IRQ レベルでマスク。move #$2700 は SSP が歩いたとき sticky IPL7 を残した（a268 @ $10000）。 */
		e.w16(0x2078); e.w16(0x010c);       /* 命令 move.l $10C,a0 */
		e.w16(0xb1fc); e.w32(0);
		e.w16(0x6702);
		e.w16(0x4ed0);                     /* 命令 jmp (a0) */
		e.w16(0x4e73);                     /* rte（null フック） */
		/* 長い jsr トランポリンの残りを、Soft10C を叩かず消す */
		e.w16(0x4e71); e.w16(0x4e71); e.w16(0x4e71); e.w16(0x4e71);
		return;
	}
	/* RTS 風 Soft10C／フック: マスク、jsr、rte が積んだ IPL を戻す。rte 前に move #$2500,sr しない — レベル保持 IRQ6 が再入し例外フレームでこの stub を壊す。 */
	e.w16(0x46fc); e.w16(0x2700);       /* 命令 move #$2700,sr */
	e.w16(0x48e7); e.w16(0xfffe);       /* 命令 movem.l d0-a6,-(sp) */
	e.w16(0x2078); e.w16(0x010c);       /* 命令 move.l $10C,a0 */
	e.w16(0xb1fc); e.w32(0);
	e.w16(0x6702);
	e.w16(0x4e90);                     /* 命令 jsr (a0) */
	e.w16(0x4cdf); e.w16(0x7fff);       /* 命令 movem.l (sp)+,d0-a6 */
	e.w16(0x4e73);                     /* 命令 rte */
}

/* $48E77FFE スキャン後、OPMDRV fn $0D は本物 ISR だけがクリアする BSS フラグ（ayayo $243C）で回る。目印が消えたら一度バインド。 */
static int bindOpmdrvIsrIfSoft(CHardX68k* hw)
{
	if (opmdrvBinScanPending(hw)) return 0;
	const unsigned h10 = hw->Read32(0x10c) & 0xffffffu;
	const int soft = (h10 == (CEMU_X68K_DOS_SOFT10C & 0xffffffu))
		|| isHangStub(hw, h10) || looksThinStub(hw, h10);
	if (!soft) return 0;
	const unsigned isr = findOpmdrvIsr(hw);
	if (!isr) return 0;
	hw->Write32(0x10c, isr);
	emitIrq6Trampoline(hw);
	hw->Write32(0x78, CEMU_X68K_DOS_IRQ6);
	return 1;
}

} /* 名前空間 */

/* FLOAT2.X（ARTDINK A2／Alice リップ）: FPU オペコードは Human68k DOS $FFxx と LINE-F を共有。PlantDos はすべての F-line を DOS と見るので A2.X `jsr $4F38A` が戻らない。FLOAT2 のハンドラは $load+0x6A。非 $FE オペコード（DOS $FFxx）を $load+0x86 に保存した前 $2C へチェイン。KEEPPR/EXIT 入口を JSR しない。 */
void CEmuX68kHookFloat2(CHardX68k* hw)
{
	if (!hw) return;
	static const unsigned kLoad[] = { 0x2a000u, 0x2e000u, 0xa0000u, 0xb0000u };
	unsigned load = 0;
	for (unsigned i = 0; i < sizeof(kLoad) / sizeof(kLoad[0]); i++) {
		const unsigned a = kLoad[i];
		if (hw->Read8(a + 0x0eu) == 'F' && hw->Read8(a + 0x0fu) == 'L'
			&& hw->Read8(a + 0x10u) == 'O' && hw->Read8(a + 0x11u) == 'A'
			&& hw->Read8(a + 0x12u) == 'T') {
			load = a;
			break;
		}
	}
	if (!load) return;
	const unsigned handler = load + 0x6au;
	const unsigned slot = load + 0x86u;
	if (hw->Read16(handler) != 0x48e7u) return;
	const unsigned cur = hw->Read32(0x2c) & 0xffffffu;
	if (cur == (handler & 0xffffffu)) return;
	hw->Write32(slot, hw->Read32(0x2c));
	hw->Write32(0x2c, handler);
}

/* CEmuX68kDosInstall の実装 */
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
	const int mailboxTrap3 = (trap3 >= 0x1040u && trap3 < 0x1080u
		&& hw->Read16(0x1040u) == 0x60FEu);
	const int thinTrap3 = isHangStub(hw, trap3);
	const int thin10c = isHangStub(hw, hook10c);
	const int soft10c = ((hook10c & 0xffffffu) == (CEMU_X68K_DOS_SOFT10C & 0xffffffu));

	/* 出す／付け替えるものが無い — まだ OPMDRV Soft10C→ISR アップグレード＋$F0 を許可 */
	if (!thinF && !thin15 && !thinIocs && !thinTrap3 && !thinTrap1 && !thin10c
		&& !soft10c && !mailboxTrap3 && (onOsF || onOs15)) {
		bindOpmdrvIocsF0(hw);
		if (isOpmdrvBinGlue(hw)) {
			bindOpmdrvIsrIfSoft(hw);
			/* ゲスト M_INIT が $10C を植える。トランポリンを更新（jmp vs jsr） */
			const unsigned h10 = hw->Read32(0x10c) & 0xffffffu;
			const int live10 = (h10 >= 0x400u && h10 < 0xf00000u
				&& !isHangStub(hw, h10) && !looksThinStub(hw, h10));
			if (live10) {
				emitIrq6Trampoline(hw);
				hw->Write32(0x78, CEMU_X68K_DOS_IRQ6);
			}
			CEmuX68kHookFloat2(hw);
			return 1;
		}
		const unsigned isr = findOpmdrvIsr(hw);
		if (!isr) {
			CEmuX68kHookFloat2(hw);
			return 0;
		}
		hw->Write32(0x10c, isr);
		emitIrq6Trampoline(hw);
		hw->Write32(0x78, CEMU_X68K_DOS_IRQ6);
		CEmuX68kHookFloat2(hw);
		return 1;
	}
	if (!thinF && !thin15 && !thinIocs && !thinTrap3 && !thinTrap1 && !thin10c
		&& !soft10c && !mailboxTrap3) {
		CEmuX68kHookFloat2(hw);
		return 0;
	}

	const unsigned zmusic = findZmusicEntry(hw);
	const unsigned zmd = findZmdBuffer(hw);
	/* 主ハンドラを入れ替え／置換するとき再出力。settle が hang stub を再植えたあと trap3/$10C だけ直すなら飛ばす */
	if (thinF || thin15 || thinIocs || thinTrap1 || !onOsF)
		emitDosImage(hw, zmusic, zmd);
	else {
		/* バンプヒープをリセットせず ZMUSIC/ZMD ポインタを更新 */
		hw->Write32(CEMU_X68K_DOS_DATA + 0x04, zmusic);
		hw->Write32(CEMU_X68K_DOS_DATA + 0x08, zmd);
	}

	/* 実行時 TRAP #15 がディスパッチしてよい MSM6258 IOCS スロット（$60.._ADPCMOUT .. $67.._ADPCMMOD）。BOOT コードオーバーレイを載せるスロットはベクタではないので缶詰成功経路のまま。 */
	{
		unsigned mask = 0xffu;
		for (unsigned fn = 0x60u; fn <= 0x67u; fn++) {
			/* BOOT バイトスープの 1 スロットが妥当な番地に読めることがある（xenon $67 = $6608）。範囲のどこかにオーバーレイが 1 つでもあれば範囲にベクタは無い。 */
			if (iocsSlotIsCodeOverlay(hw, fn)) { mask = 0; break; }
		}
		hw->Write8(CEMU_X68K_DOS_DATA + 0x1eu, (uint8_t)mask);
	}

	if (thinF)
		hw->Write32(0x2c, CEMU_X68K_DOS_LINEF);
	if (thin15) {
		hw->Write32(0xbc, CEMU_X68K_DOS_TRAP15);
	}
	/* IOCS ジャンプ表 — BOOT コードオーバーレイを壊さない。それらのスロットが書けなくても trap15 はまだ OPMSET/OPMINTST/FEFUNC をディスパッチ。BOOT IOCS 表トランポリン（akiko/can4）は trap15 を残すが薄いときはまだ $68/$6A/$F0 本体が要る。 */
	if (thin15 || thinIocs || isBootIocsTableTrap15(hw, trap15)) {
		if (!iocsSlotIsCodeOverlay(hw, 0x68) && (thin15 || thinIocs || iocsSlotThin(hw, 0x68)))
			hw->Write32(0x400u + 0x68u * 4u, CEMU_X68K_DOS_OPMSET);
		if (!iocsSlotIsCodeOverlay(hw, 0x6a) && (thin15 || thinIocs || iocsSlotThin(hw, 0x6a)))
			hw->Write32(0x400u + 0x6au * 4u, CEMU_X68K_DOS_OPMINTST);
		if (!iocsSlotIsCodeOverlay(hw, 0x80) && (thin15 || thinIocs || iocsSlotThin(hw, 0x80)))
			hw->Write32(0x400u + 0x80u * 4u, CEMU_X68K_DOS_B_INTVCS);
		/* $400+$F0*4 == $7C0: VOPM＋fn 表が見つかったときの OPMDRV ディスパッチャ */
		if (!iocsSlotIsCodeOverlay(hw, 0xf0) && (thin15 || thinIocs || iocsSlotThin(hw, 0xf0))) {
			const unsigned opm = findOpmdrvF0Entry(hw);
			hw->Write32(0x400u + 0xf0u * 4u, opm ? opm : CEMU_X68K_DOS_FEFUNC);
		}
		/* 薄い OPM 隣接スロット用の汎用 IOCS 成功（RTS）。$00/$01 は飛ばす — $400 上書きが dios OPMDRV 立ち上げを壊した。$80/$F0 は上で処理。 */
		static const unsigned kOkFns[] = {
			0x60u, 0x66u, 0x67u, 0x69u, 0x86u
		};
		for (unsigned i = 0; i < sizeof(kOkFns) / sizeof(kOkFns[0]); i++) {
			const unsigned fn = kOkFns[i];
			if (!iocsSlotIsCodeOverlay(hw, fn) && iocsSlotThin(hw, fn))
				hw->Write32(0x400u + fn * 4u, CEMU_X68K_DOS_IOCS_OK);
		}
	}

	/* 薄い TRAP#1 はしばしば nop;bra*（BOOT プレースホルダ）。Hoot BOOT はほぼ全例外ベクタ（$08..$FC）と空 IOCS 表スロットの既定として同じ番地を植える。例外は RTE、IOCS 表は JSR 到達で RTS — stub を共有しない。 */
	if (thinTrap1) {
		/* stub は既に emitDosImage @ F08340/F08344 */
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
			if (fn < 0x08u) continue; /* 低 IOCS／あり得るコードオーバーレイを残す */
			if (iocsSlotIsCodeOverlay(hw, fn))
				continue;
			if ((hw->Read32(slot) & 0xffffffu) == trap1)
				hw->Write32(slot, CEMU_X68K_DOS_IOCS_OK);
		}
	}

	/* 薄い TRAP#3 はしばしば nop;bra*（BOOT hang）。常に付け替える — ZMUSIC が見つからなければゲートは no-op。Line-F が OS 上のあと settle がこれを再植し得る。本物 ZMUSIC（ident は entry-8）が $8C を所有しなければならない: ハンドラは RTE するので DOS トランポリンの jsr が例外フレームを壊す。 */
	if (thinTrap3) {
		hw->Write32(0x8c, zmusic ? zmusic : CEMU_X68K_DOS_TRAP3);
		/* nop;bra* → rte。旧 stub へ競った trap が戻れる */
		hw->Write16(trap3, 0x4e73);
		hw->Write16(trap3 + 2u, 0x4e73);
	}
	/* angdive/bfighter リセット trap3 は $1042（$1040 bra.s * 島へ BSR）。$1040/$1042 に RTE を重ねない — $8C だけ付け替える。 */
	if (mailboxTrap3) {
		hw->Write32(0x8c, zmusic ? zmusic : CEMU_X68K_DOS_TRAP3);
		if (lineF >= 0x1040u && lineF < 0x1080u)
			hw->Write32(0x2c, CEMU_X68K_DOS_LINEF);
	}

	/* Soft OPM フック $10C: nop;bra* hang だけ OS RTS stub へ付け替える（IRQ6 トランポリンは jsr — RTE ではなく RTS）。 */
	if (thin10c) {
		Emit e(hw, CEMU_X68K_DOS_SOFT10C);
		e.w16(0x7000);
		e.w16(0x4e75); /* 命令 rts */
		hw->Write32(0x10c, CEMU_X68K_DOS_SOFT10C);
		hw->Write16(hook10c, 0x4e73);
		hw->Write16(hook10c + 2u, 0x4e73);
	}

	/* OPMDRV は置いたがまだ Soft10C: ISR をバインドし $2490／$243C 待ちが AssistSoftWaits 無しで完了する（ドライバがフラグを自分でクリア）。hoot opmdrv.bin: $48E77FFE スキャン飛行中は飛ばす。 */
	bindOpmdrvIsrIfSoft(hw);

	/* ゲストは IOCS OPMINTST 無しで $10C を入れ得る — タイマエッジがシーケンサに届くよう IRQ6 を OS トランポリン経由にルーティング（OPM_WRITES WEAK）。$78 が既にここを指すときは常にトランポリンバイトを更新 — 先の IRQ ネスト嵐が $F08700 を積んだ SR/PC フレームで上書きし得る。 */
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

	/* IOCS $F0（$7C0）: 空 FEFUNC より XML 置き OPMDRV ディスパッチャを優先。FLOAT 「FEfn」バインドは遅延のまま — 未ロード FLOAT へ早期ジャンプすると、ベクタが全部 hang サイトをエイリアスする hoot BOOT が例外嵐になる。 */
	{
		const unsigned opm = findOpmdrvF0Entry(hw);
		if (opm)
			hw->Write32(0x7c0, opm);
		else if ((thinF || thin15 || thinIocs) && (hw->Read32(0x7c0) & 0xffffffu) == 0)
			hw->Write32(0x7c0, CEMU_X68K_DOS_FEFUNC);
	}

	/* PC が今パッチした BOOT hang サイトにいるなら例外を完了する */
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

	CEmuX68kHookFloat2(hw);
	return 1;
}
