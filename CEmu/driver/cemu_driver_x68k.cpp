#include "StdAfx.h"
#include "cemu_driver_x68k.h"
#include "../chip/cemu_chip_opm.h"
#include "../machine/cemu_x68k_dos.h"
#include "../cemu_types.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>

/* angdive/bfighter BOOT: IPL 後 $100.. に RTE を植え、$1040 で bra.s *（SR アンマスク、MFP IER=0）。本立ち上げはすぐ下の `move #$2500,sr / jsr init / tst.b $E00000`。$1040 に RTE を重ねず poll へスナップしない（jsr を飛ばす）。 */
static int CDriverX68kIs1040MailboxHang(CHardX68k* hw, unsigned pc)
{
	if (!hw) return 0;
	if (pc != 0x1040u && pc != 0x1042u)
		return 0;
	if (hw->Read16(0x1040u) != 0x60FEu)
		return 0;
	return 1;
}

/* CDriverX68kFindZmusicBootJsr の実装 */
static unsigned CDriverX68kFindZmusicBootJsr(CHardX68k* hw)
{
	if (!hw) return 0;
	for (unsigned a = 0x1080u; a + 16u < 0x1400u; a += 2u) {
		if (hw->Read16(a) != 0x46FCu || hw->Read16(a + 2u) != 0x2500u)
			continue;
		if (hw->Read16(a + 4u) != 0x4EB9u)
			continue;
		if (hw->Read16(a + 10u) != 0x4A39u)
			continue;
		if (hw->Read32(a + 12u) != 0x00E00000u)
			continue;
		return a;
	}
	return 0;
}

/* CDriverX68kResidentZmusic の実装 */
static unsigned CDriverX68kResidentZmusic(CHardX68k* hw)
{
	if (!hw) return 0;
	static unsigned s_key = 0;
	static unsigned s_ent = 0;
	const unsigned key = hw->Read32(0x100u) ^ hw->Read32(0x1F4Eu);
	if (key != s_key) {
		s_key = key;
		s_ent = 0;
	}
	if (s_ent)
		return s_ent;
	/* libzm2internal.h: trap #3 は ident+8、version < $3000、ZMD を飛ばす */
	for (unsigned a = 0; a + 10u < 0x400000u; a += 2u) {
		if (hw->Read8(a) != 'Z' || hw->Read8(a + 1) != 'm') continue;
		if (hw->Read8(a + 2) != 'u' || hw->Read8(a + 3) != 'S') continue;
		if (hw->Read8(a + 4) != 'i' || hw->Read8(a + 5) != 'C') continue;
		if (a >= 1u && hw->Read8(a - 1u) == 0x10u)
			continue;
		if (hw->Read16(a + 6u) >= 0x3000u)
			continue;
		if (hw->Read16(a + 8u) == 0x48e7u) {
			s_ent = a + 8u;
			return s_ent;
		}
	}
	return 0;
}

/* CDriverX68kZmusicIntEntry の実装 */
static unsigned CDriverX68kZmusicIntEntry(CHardX68k* hw, unsigned zmusic)
{
	if (!hw || !zmusic)
		return 0;
	const unsigned hi = (zmusic + 0x20000u < 0x800000u)
		? (zmusic + 0x20000u) : 0x800000u;
	/* zmsc_int.s int_entry: opmwait; move.b #$14,$E90001; move.b #$35,$E90003。ident 後の最初の 48E7 FEFE は別 movem（angdive $1E092）。 */
	for (unsigned a = zmusic; a + 12u < hi; a += 2u) {
		if (hw->Read16(a) != 0x13FCu || hw->Read16(a + 2u) != 0x0014u)
			continue;
		if (hw->Read32(a + 4u) != 0x00E90001u)
			continue;
		unsigned entry = a;
		if (a >= 4u && (hw->Read16(a - 4u) & 0xFF00u) == 0x6100u)
			entry = a - 4u;
		else if (a >= 2u && (hw->Read16(a - 2u) & 0xFF00u) == 0x6100u)
			entry = a - 2u;
		return entry;
	}
	return 0;
}

/* CDriverX68kBindZmusicIsr の実装 */
static void CDriverX68kBindZmusicIsr(CHardX68k* hw)
{
	if (!hw)
		return;
	const unsigned zmusic = CDriverX68kResidentZmusic(hw);
	if (!zmusic)
		return;
	const unsigned isr = CDriverX68kZmusicIntEntry(hw, zmusic);
	if (!isr)
		return;
	hw->Write32(0x10cu, isr);
	hw->Write16(CEMU_X68K_DOS_IRQ6 + 0u, 0x2078u);
	hw->Write16(CEMU_X68K_DOS_IRQ6 + 2u, 0x010Cu);
	hw->Write16(CEMU_X68K_DOS_IRQ6 + 4u, 0x4ED0u);
	hw->Write32(0x78u, CEMU_X68K_DOS_IRQ6);
}

/* CDriverX68kResume1040Hang の実装 */
static void CDriverX68kResume1040Hang(CHardX68k* hw, unsigned code)
{
	if (!hw) return;
	/* TRAP#3 ベクタ $xx001042 は $1040 hang 島へ回る。本物 ZMUSIC は RTE。DOS トランポリン jsr がそのフレームを壊す（PC=$1ED2）。$8C を ident+8 へ。$1040/$1042 に RTE を重ねない。 */
	if (hw->Read16(0x1040u) == 0x60FEu) {
		const unsigned zmusic = CDriverX68kResidentZmusic(hw);
		if (zmusic)
			hw->Write32(0x8cu, zmusic);
		else {
			const unsigned t3 = hw->Read32(0x8cu) & 0xffffffu;
			if (t3 >= 0x1040u && t3 < 0x1080u) {
				const unsigned stub = CEMU_X68K_DOS_TRAP3;
				if (hw->Read16(stub) != 0x2079u && hw->Read16(stub) != 0x7000u) {
					hw->Write16(stub, 0x7000);
					hw->Write16(stub + 2u, 0x4e73u);
				}
				hw->Write32(0x8cu, stub);
			}
		}
		const unsigned lf = hw->Read32(0x2cu) & 0xffffffu;
		if (lf >= 0x1040u && lf < 0x1080u)
			hw->Write32(0x2cu, CEMU_X68K_DOS_LINEF);
		unsigned zmd = 0;
		for (unsigned a = 0x1000u; a + 8u < 0x80000u; a++) {
			if (hw->Read8(a) != 0x10u) continue;
			if (hw->Read8(a + 1u) != 'Z' || hw->Read8(a + 2u) != 'm') continue;
			if (hw->Read8(a + 3u) != 'u' || hw->Read8(a + 4u) != 'S') continue;
			if (hw->Read8(a + 5u) != 'i' || hw->Read8(a + 6u) != 'C') continue;
			zmd = a + 7u;
			break;
		}
		if (zmusic && zmd) {
			unsigned isr = CDriverX68kZmusicIntEntry(hw, zmusic);
			if (!isr) {
				const unsigned hi = (zmusic + 0x10000u < 0x800000u)
					? (zmusic + 0x10000u) : 0x800000u;
				for (unsigned a = zmusic; a + 4u < hi; a += 2u) {
					if (hw->Read16(a) == 0x48e7u && hw->Read16(a + 2u) == 0xfefeu) {
						isr = a;
						break;
					}
				}
			}
			if (isr) {
				hw->Write32(0x10cu, isr);
				hw->Write16(CEMU_X68K_DOS_IRQ6 + 0u, 0x2078u); /* 命令 move.l $10C,a0 */
				hw->Write16(CEMU_X68K_DOS_IRQ6 + 2u, 0x010Cu);
				hw->Write16(CEMU_X68K_DOS_IRQ6 + 4u, 0x4ED0u); /* 命令 jmp (a0) */
				hw->Write32(0x78u, CEMU_X68K_DOS_IRQ6);
			}
			CDriverX68kBindZmusicIsr(hw);
			const unsigned stub = 0x00F08780u;
			static unsigned s_stubKey = 0;
			static int s_stubOnce = 0;
			const unsigned playKey = zmusic ^ zmd ^ (code & 0xffffu);
			if (s_stubKey != playKey) {
				s_stubKey = playKey;
				s_stubOnce = 0;
			}
			if (s_stubOnce) {
				const unsigned boot2 = CDriverX68kFindZmusicBootJsr(hw);
				m68k_set_reg(M68K_REG_SR, 0x2500);
				if (boot2) {
					m68k_set_reg(M68K_REG_PC, boot2 + 10u);
					hw->SetPc(boot2 + 10u);
				}
				hw->SetSongCommand(code);
				return;
			}
			s_stubOnce = 1;
			hw->Write16(stub + 0u, 0x7200u); /* 命令 moveq #0,d1  m_init */
			hw->Write16(stub + 2u, 0x4E43u);
			hw->Write16(stub + 4u, 0x227Cu); /* 命令 move.l #zmd,a1 */
			hw->Write32(stub + 6u, zmd);
			hw->Write16(stub + 10u, 0x7400u); /* 命令 moveq #0,d2 */
			hw->Write16(stub + 12u, 0x7211u); /* 命令 moveq #$11,d1 play_cnv_data */
			hw->Write16(stub + 14u, 0x4E43u);
			/* m_play00 `ori #$0700,sr` / t_dat_ok RTS。trap RTE が IPL7 のままだと YM Timer A が割り込めない。ここで落とす。 */
			hw->Write16(stub + 16u, 0x027Cu); /* 命令 andi.w #$F8FF,sr */
			hw->Write16(stub + 18u, 0xF8FFu);
			const unsigned boot = CDriverX68kFindZmusicBootJsr(hw);
			if (boot) {
				hw->Write16(stub + 20u, 0x4EF9u);
				hw->Write32(stub + 22u, boot + 10u);
			} else {
				hw->Write16(stub + 20u, 0x60FEu);
			}
			m68k_set_reg(M68K_REG_SR, 0x2500);
			m68k_set_reg(M68K_REG_PC, stub);
			hw->SetPc(stub);
			hw->SetSongCommand(code);
			return;
		}
	}
	hw->SetSongCommand(code);
	const unsigned boot = CDriverX68kFindZmusicBootJsr(hw);
	if (boot) {
		m68k_set_reg(M68K_REG_SR, 0x2500);
		m68k_set_reg(M68K_REG_PC, boot);
		hw->SetPc(boot);
	}
}

/* CDriverX68kSkipDmacScan の実装 */
static void CDriverX68kSkipDmacScan(CHardX68k* hw)
{
	if (!hw || hw->Read16(0x1040u) != 0x60FEu)
		return;
	if (hw->Read16(0x1556u) == 0x60FEu)
		hw->Write16(0x1556u, 0x4E75u);
	/* BOOT `move.l #$14E6,$2C / $F000 / rts` が LINE-F を $14E6 Human68k cmp 連鎖へ奪う（PC=$1ED2）。$2C は OS イメージのまま。 */
	for (unsigned a = 0x1400u; a + 12u < 0x1600u; a += 2u) {
		if (hw->Read16(a) != 0x23FCu)
			continue;
		if (hw->Read32(a + 6u) != 0x0000002cu)
			continue;
		if (hw->Read16(a) != 0x4E75u)
			hw->Write16(a, 0x4E75u);
		break;
	}
	const unsigned lf = hw->Read32(0x2cu) & 0xffffffu;
	if (lf >= 0x1040u && lf < 0x1080u)
		hw->Write32(0x2cu, CEMU_X68K_DOS_LINEF);
	/* $1F16/$243C jsr $15B0/$15A0（Human68k/DMAC + trap #3 play）。その init は残す。$1D42 `cmpi.b #$6B,2(A5) / bne` は PSP を見ない。分岐を NOP し bring-up が play_cnv_data に届くようにする。 */
	for (unsigned a = 0x1C00u; a + 8u < 0x1E80u; a += 2u) {
		const unsigned op = hw->Read16(a);
		if (op != 0x0C2Du && op != 0x0C6Du)
			continue;
		if (hw->Read16(a + 2u) != 0x006Bu)
			continue;
		if (hw->Read16(a + 4u) != 0x0002u)
			continue;
		if ((hw->Read16(a + 6u) & 0xFF00u) == 0x6600u)
			hw->Write16(a + 6u, 0x4E71u);
		break;
	}
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	if (pc < 0x15F0u || pc >= 0x1720u)
		return;
	if (hw->Read16(0x1600u) != 0xB1C9u && hw->Read16(0x1648u) != 0x5488u
		&& hw->Read16(0x1648u) != 0xB1C9u && hw->Read16(0x16B8u) != 0xB1C9u)
		return;
	const unsigned a1 = (unsigned)m68k_get_reg(NULL, M68K_REG_A1) & 0xffffffu;
	m68k_set_reg(M68K_REG_A0, a1);
}

/* IRQ 配送 */
static void CDriverX68kApplyIrq(CHardX68k* hw, CChip* chip, int hold)
{
	if (hold) {
		m68k_set_irq(M68K_IRQ_NONE);
		return;
	}
	if (chip && chip->Irq())
		m68k_set_irq(M68K_IRQ_6);
	else if (hw && hw->MfpIrqPending())
		m68k_set_irq(M68K_IRQ_2);
	else
		m68k_set_irq(M68K_IRQ_NONE);
}

/* StarCraft compile BOOT は $10A48（low）へ $A00000 を植えるが $10A44 は OP.X データ節（~$152D2）のまま。MML コンパイルは ISR を下へ埋める。low 植込が見えたらバンプを DOS ヒープ頂へ移す。 */
static void CDriverX68kRetargetOpxCompileHeap(CHardX68k* hw)
{
	if (!hw) return;
	if (hw->Read32(0x10a48) != 0x00A00000u) return;
	const unsigned bump = hw->Read32(0x10a44);
	if (bump >= 0x8000u && bump < 0x20000u)
		hw->Write32(0x10a44, 0x00A40000u);
}

/* ARTDINK A2.X: $4F38A は C フレーム関数（clr.l -4(a6)）。BOOT が link 無しで jsr。DOS スタックフレームを植え、init 中の空バッファ解析が OPMDRV ISR $11C58 を壊さないようにする。init 後 BOOT は `move.w #$4E75,$4F428` を残し、以降の play で `jsr $487A4` を飛ばす — settle 完了後 jsr を戻す。 */
static int CDriverX68kIsA2Boot(CHardX68k* hw)
{
	if (!hw) return 0;
	if (hw->Read16(0x4f38au) != 0x42aeu || hw->Read16(0x4f38cu) != 0xfffcu)
		return 0;
	if (hw->Read16(0x8c4u) != 0x2040u && hw->Read16(0x8c4u) != 0x4e71u)
		return 0;
	if (hw->Read16(0x8c6u) != 0x46fcu || hw->Read16(0x8c8u) != 0x2500u)
		return 0;
	if (hw->Read16(0x8d0u) != 0x4a39u || hw->Read32(0x8d2u) != 0x00e00000u)
		return 0;
	return 1;
}

/* CDriverX68kPlantA2Frame の実装 */
static void CDriverX68kPlantA2Frame(CHardX68k* hw)
{
	if (!CDriverX68kIsA2Boot(hw)) return;
	if (hw->Read16(0x8c4u) == 0x2040u)
		hw->Write16(0x8c4u, 0x4e71u);
	m68k_set_reg(M68K_REG_A6, 0x00F0FE00u);
	hw->Write32(0x00F0FDF8u, 0);
	hw->Write32(0x00F0FDFCu, 0);
}

/* CDriverX68kRestoreA2Play の実装 */
static void CDriverX68kRestoreA2Play(CHardX68k* hw)
{
	if (!CDriverX68kIsA2Boot(hw)) return;
	if (hw->Read16(0x4f428u) != 0x4e75u) return;
	if (hw->Read32(0x4f42au) != 0x000487a4u) return;
	hw->Write16(0x4f428u, 0x4eb9u);
	if (hw->Read16(0x11c58u) != 0x48e7u
		&& hw->Read16(0x11c5cu) == 0x4a39u
		&& hw->Read32(0x11c5eu) == 0x0001243cu)
		hw->Write32(0x11c58u, 0x48e77ffeu);
}

/* Onion SND.X（cave）: DOS _INTVCS は C ABI `pea handler; move.w #32,-(sp); FF25`。残り d1 が $1F0 だと trap #0 が hang stub のまま、play の `trap #0` が $1021C に届かない。 */
static void CDriverX68kPlantCaveTrap0(CHardX68k* hw)
{
	if (!hw) return;
	if (hw->Read16(0x950u) != 0x4a39u || hw->Read32(0x952u) != 0x00e00000u)
		return;
	if (hw->Read16(0x1021Cu) != 0x48e7u || hw->Read16(0x1026Eu) != 0x4e73u)
		return;
	hw->Write32(0x80u, 0x1021Cu);
	if (hw->Read16(0x104A2u) == 0x48e7u && hw->Read16(0x104D6u) == 0x4e73u)
		hw->Write32(0x10Cu, 0x104A2u);
	if (hw->Read16(0x10150u) == 0x08f9u)
		hw->Write32(0x7Cu, 0x10150u);
}

/* C コンパイル type=x（MAIN.X / GOLF.X）は -4(a6)。BOOT が A6 を 0 クリア。 */
static void CDriverX68kPlantCFrame(CHardX68k* hw)
{
	if (!hw) return;
	int need = 0;
	if (hw->Read16(0x910u) == 0x4eb9u && hw->Read32(0x912u) == 0x00015486u
		&& hw->Read16(0x8d0u) == 0x4a39u) {
		need = 1;
		if (hw->Read16(0x8c4u) == 0x2040u)
			hw->Write16(0x8c4u, 0x4e71u);
	}
	if (hw->Read16(0x4ecu) == 0x41f9u && hw->Read32(0x4eeu) == 0x00015200u
		&& hw->Read16(0x4a4u) == 0x4a39u) {
		need = 1;
		if (hw->Read16(0x498u) == 0x2040u)
			hw->Write16(0x498u, 0x4e71u);
	}
	if (!need) return;
	m68k_set_reg(M68K_REG_A6, 0x00F0FE00u);
	for (unsigned a = 0x00F0FDC0u; a < 0x00F0FE00u; a += 4u)
		hw->Write32(a, 0);
}

/* EAST CUBE 白夜物語: BOOT writes RTS over MAIN.X play/load ($10A42 /
   $10B02) so hoot can skip KEEPPR. The FM.DAT pointer is already in RAM
   at $76E18 — let init's `jsr $10B02` actually load it. */
static void CDriverX68kKeepByakuyaPlay(CHardX68k* hw)
{
	if (!hw) return;
	if (hw->Read16(0x900u) != 0x33fcu || hw->Read16(0x902u) != 0x4e75u
		|| hw->Read32(0x904u) != 0x00010a42u)
		return;
	unsigned a;
	for (a = 0x900u; a < 0x910u; a += 2u)
		hw->Write16(a, 0x4e71u);
}

CDriverX68k::CDriverX68k()
	: hw_(NULL)
	, hostRate_(44100)
	, cpuHz_(10000000)
	, opmHz_(4000000)
	, booted_(0)
	, opmResidual_(0)
	, cpuAcc_(0)
	, nextCmdAt_(0)
	, cmdIndex_(0)
	, songCode_(1)
	, bestSongCode_(1)
	, bestPeak_(0)
	, windowPeak_(0)
	, dwellFrames_(0)
	, dwellLeft_(0)
	, tryCount_(0)
	, irqWas_(0)
	, locked_(0)
	, pinned_(0)
	, opmAtWindow_(0)
	, dwellExtendUsed_(0)
	, timerDAcc_(0)
	, vdispAcc_(0)
	, softTimerBusy_(0)
	, opmSpinRescue_(0)
{
	memset(tryCodes_, 0, sizeof(tryCodes_));
}

/* driverOpmGlue の実装 */
static int driverOpmGlue(CHardX68k* hw)
{
	if (!hw) return 0;
	return ((hw->Read32(0x400) & 0xffffffu) == 0xB06u
		&& hw->Read16(0xB16) == 0x223Cu);
}

/* D.O. / 工画堂 OPMDRV2.X BOOT: `tst.b $E00000` poll plus
   `lea $43FF0540` work pointer. Poll sits at $8B2 (dios), $8C2
   (hsuna168), $8D0 (sabnack) — scan instead of hardcoding. Not
   hoot opmdrv.bin, so the glue slice never runs. A2.X also polls
   at $8D0 but has no $43FF0540 plant. */
static unsigned driverDoOpmdrv2Poll(CHardX68k* hw)
{
	if (!hw) return 0;
	unsigned poll = 0;
	int hasLea = 0;
	unsigned a;
	for (a = 0x400u; a + 6u < 0xc00u; a += 2u) {
		if (!poll && hw->Read16(a) == 0x4a39u
			&& hw->Read32(a + 2u) == 0x00e00000u)
			poll = a;
		if (hw->Read16(a) == 0x41f9u && hw->Read32(a + 2u) == 0x43ff0540u)
			hasLea = 1;
	}
	return (poll && hasLea) ? poll : 0;
}

/* driverDoOpmdrv2 の実装 */
static int driverDoOpmdrv2(CHardX68k* hw)
{
	return driverDoOpmdrv2Poll(hw) != 0;
}

/* WRITE（$808C の `lea -0x200,sp`）＋ Timer-B エッジで ISR がスクラッチパッド上のフレームを RTE。解析は $88B0 を過ぎて続く（ディスパッチャ $8A90、OPMSET $A804）。$88B0 までの PC のみ hold では長いコンパイルが壊れた。SSP がそのパッド上なら hold。ただし OPMDRV の `tst.b $24xx / bne.s` フラグ待ち（haou $88B8 $2490、sshang $88E2 $24EC）は WRITE が呼び Timer-B が要るので除外。play の ISR は高スタック（$F0FFxx）なので hold しない。 */
static int driverOpmFlagWait(CHardX68k* hw, unsigned pc)
{
	if (!hw) return 0;
	return hw->Read16(pc) == 0x4A39u && hw->Read16(pc + 6u) == 0x66F8u;
}

/* IRQ 配送 */
static int driverOpmHoldIrq(CHardX68k* hw, unsigned pc, unsigned sp)
{
	if (driverOpmFlagWait(hw, pc))
		return 0;
	/* WRITE の lea -0x200,sp。メールボックス poll は $F0FFxx のままなので hold しない。PC が OPMDRV 内であることを要求し、$F08xxx の cmd6 IOCS が Timer-B を取れるようにする（フラグ待ちは上で除外済み）。 */
	if (sp >= 0x00F0F000u && sp < 0x00F0FEF0u
		&& pc >= 0x8000u && pc < 0xE000u)
		return 1;
	return 0;
}

/* driverOpmLandmark の実装 */
static unsigned driverOpmLandmark(CHardX68k* hw)
{
	int k;
	for (k = 0; k < 1000; k++) {
		const unsigned a = 0xD200u + (unsigned)k * 2u;
		if (hw->Read32(a) == 0x48E77FFEu)
			return a;
	}
	return 0;
}

/* 生きた IRQ6 トランポリンを完走。skipSpin=1 だと積み戻りが未割当ベクタ nop-slide $94A のとき PC がトランポリンに残る。 */
static int driverRteIrq6(CHardX68k* hw, int skipSpin)
{
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	const unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
	if (pc < CEMU_X68K_DOS_IRQ6 || pc >= (CEMU_X68K_DOS_IRQ6 + 0x50u))
		return 0;
	if (sp < 0xf0c000u || sp > 0xf0fff8u)
		return 0;
	const unsigned ret = hw->Read32(sp + 2u) & 0xffffffu;
	if (skipSpin && ret >= 0x94Au && ret < 0x95Au)
		return 0;
	/* TRAP#1 stub は $F08740。壊れたネストは自分へ RTE し得る。 */
	if (ret >= CEMU_X68K_DOS_IRQ6 && ret < (CEMU_X68K_DOS_IRQ6 + 0x50u))
		return 0;
	if ((ret & 1u) || ret < 0x400u || ret >= 0xf00000u)
		return 0;
	m68k_set_reg(M68K_REG_SR, hw->Read16(sp));
	m68k_set_reg(M68K_REG_PC, ret);
	m68k_set_reg(M68K_REG_SP, (sp + 6u) & 0xffffffu);
	return 1;
}

/* MIDI_DRV.68K は A4=$EAFA09（YM3802 DSR）で `tst.b (a4) / bpl` 待ち。Uninstall / BOOT が A4 を再ロードせず jsr するので A4=0 が ROM 0 を読み、分岐が終わらない。 */
static void CDriverX68kFixMidiA4(CHardX68k* hw)
{
	if (!hw) return;
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	if (hw->Read16(pc) != 0x4a14u) return;
	if (hw->Read32((pc + 2u) & 0xffffffu) != 0x6a00fffcu) return;
	const unsigned a4 = (unsigned)m68k_get_reg(NULL, M68K_REG_A4) & 0xffffffu;
	if ((a4 >= 0xeafa00u && a4 <= 0xeafa0fu)
		|| (a4 >= 0xefa000u && a4 <= 0xefa00fu))
		return;
	m68k_set_reg(M68K_REG_A4, 0x00eafa09u);
}

CDriverX68k::~CDriverX68k()
{
	Close();
}

/* CDriverX68k::OpmWrites の実装 */
unsigned CDriverX68k::OpmWrites() const
{
	return hw_ ? hw_->OpmWrites() : 0;
}

/* CDriverX68k::Pc の実装 */
unsigned CDriverX68k::Pc() const
{
	return hw_ ? ((unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu) : 0;
}

/* CDriverX68kPushTry の実装 */
static void CDriverX68kPushTry(unsigned* dst, int* n, int cap, unsigned code)
{
	if (!dst || !n || *n >= cap) return;
	/* メールボックス連打では Stop=0x5f を飛ばす。0xffff は有効な play。 */
	if (code == 0x5f) return;
	for (int i = 0; i < *n; i++) {
		if (dst[i] == code) return;
	}
	dst[(*n)++] = code;
}

/* Konami gra2 等: stop=0xf0、fade=0xf9 — 単独固定だと無音。0x00FF は数値 0xFF だが Humming Bird Laplace は DOORWAY とカタログする。タイトルリスト命中は XML stop= がそう言わない限りハンター専用デッド cmd ではない。 */
static int CDriverX68kIsDeadCmd(unsigned code, unsigned stopCode,
	const CEmuGameEntry* ge)
{
	/* XML に載っていても常にハンター死（gra2 FADE 0xF9）。 */
	if (code == 0x5f || code == 0xf9) return 1;
	if (stopCode && code == stopCode) return 1;
	if (ge) {
		for (int i = 0; i < ge->titleCount; i++) {
			if (ge->title[i].code == code)
				return 0;
		}
	}
	if (code == 0xff) return 1;
	return 0;
}

/* BGM っぽいコード（0xA0..0xEF）を SFX／ユーティリティより先に */
static int CDriverX68kCmdPriority(unsigned code, unsigned stopCode,
	const CEmuGameEntry* ge)
{
	if (CDriverX68kIsDeadCmd(code, stopCode, ge)) return 3;
	if (code >= 0xa0 && code <= 0xef) return 0;
	if (code >= 0x80 && code < 0xa0) return 1;
	return 2;
}

/* CDriverX68kStopCode の実装 */
static unsigned CDriverX68kStopCode(const CEmuGameEntry* ge)
{
	if (!ge) return 0xf0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "stop") == 0) {
			unsigned v = (unsigned)strtoul(ge->opt[i].value, NULL, 0);
			return v ? v : 0xf0;
		}
	}
	return 0x5f; /* 汎用 OPMDRV / ZMUSIC */
}

/* ROM を載せ、ブートして曲を起動する */
int CDriverX68k::Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode)
{
	if (!hw || !ge || !fs || hw->hardKind != CHard::KIND_X68K) return 0;
	hw_ = (CHardX68k*)hw;
	hostRate_ = hw_->SampleRate();
	cpuHz_ = hw_->cpuHz_ > 0 ? hw_->cpuHz_ : 10000000;
	opmHz_ = hw_->opmHz_ > 0 ? hw_->opmHz_ : 4000000;
	opmResidual_ = 0;
	cpuAcc_ = 0;
	booted_ = 0;
	cmdIndex_ = 0;
	songCode_ = titleCode ? titleCode : 1;
	bestSongCode_ = songCode_;
	bestPeak_ = 0;
	windowPeak_ = 0;
	dwellFrames_ = hostRate_ > 0 ? hostRate_ / 2 : 22050;
	if (dwellFrames_ < 1) dwellFrames_ = 1;
	dwellLeft_ = 0;

	tryCount_ = 0;
	locked_ = 0;
	pinned_ = 0;
	irqWas_ = 0;
	opmAtWindow_ = 0;
	dwellExtendUsed_ = 0;
	timerDAcc_ = 0;
	vdispAcc_ = 0;
	softTimerBusy_ = 0;
	const unsigned stopCode = CDriverX68kStopCode(ge);

	/* カタログタイトルを先に（XML 順、BGM 優先ソート）。Open(...,1) がカタログ 0x18 等より先にスロット 0 を奪ってはいけない — aquales INTRO が固まり、メールボックス再開無しでは後続コードが聞こえない。 */
	unsigned catalog[CEMU_TITLE_MAX];
	int catalogN = 0;
	unsigned deferred[64];
	int deferredN = 0;
	for (int i = 0; i < ge->titleCount && catalogN < (int)_countof(catalog); i++) {
		const unsigned c = ge->title[i].code;
		if (c == 0 || CDriverX68kIsDeadCmd(c, stopCode, ge)) continue;
		char labA[CEMU_GAME_NAME];
		WideCharToMultiByte(932, 0, ge->title[i].label, -1, labA, (int)sizeof(labA), NULL, NULL);
		char fileTok[CEMU_GAME_NAME];
		fileTok[0] = 0;
		{
			const char* colon = strchr(labA, ':');
			int n = colon ? (int)(colon - labA) : (int)strlen(labA);
			while (n > 0 && (labA[n - 1] == ' ' || labA[n - 1] == '\t')) n--;
			int s = 0;
			while (s < n && (labA[s] == ' ' || labA[s] == '\t')) s++;
			if (n > s) {
				int ln = n - s;
				if (ln >= (int)sizeof(fileTok)) ln = (int)sizeof(fileTok) - 1;
				memcpy(fileTok, labA + s, (size_t)ln);
				fileTok[ln] = 0;
			}
		}
		int missing = 0;
		if (fileTok[0] && strchr(fileTok, '.')) {
			unsigned sz = 0;
			if (!CEmuZipFsFind(fs, fileTok, &sz) || sz == 0)
				missing = 1;
		}
		if (missing) {
			if (deferredN < (int)_countof(deferred))
				deferred[deferredN++] = c;
			continue;
		}
		catalog[catalogN++] = c;
	}
	/* カタログをソート: BGM（0xA0+）を SFX より前 */
	for (int a = 0; a < catalogN; a++) {
		for (int b = a + 1; b < catalogN; b++) {
			if (CDriverX68kCmdPriority(catalog[b], stopCode, ge)
				< CDriverX68kCmdPriority(catalog[a], stopCode, ge)) {
				unsigned t = catalog[a]; catalog[a] = catalog[b]; catalog[b] = t;
			}
		}
	}
	for (int i = 0; i < catalogN; i++)
		CDriverX68kPushTry(tryCodes_, &tryCount_, (int)_countof(tryCodes_), catalog[i]);
	for (int i = 0; i < deferredN; i++)
		CDriverX68kPushTry(tryCodes_, &tryCount_, (int)_countof(tryCodes_), deferred[i]);

	/* Open(titleCode) がカタログ項目なら先頭へ（プレイリスト／バッチ Open(...,1)）。死んだ INTRO 固まりは後続コード探索時 ResumeMailboxForSong で回復（aquales 0x18）。非カタログプレイリスト選びは 0xA0+ BGM 前置を維持。 */
	if (titleCode && !CDriverX68kIsDeadCmd(titleCode, stopCode, ge)) {
		int found = -1;
		for (int i = 0; i < tryCount_; i++) {
			if (tryCodes_[i] == titleCode) { found = i; break; }
		}
		if (found > 0) {
			for (int i = found; i > 0; i--)
				tryCodes_[i] = tryCodes_[i - 1];
			tryCodes_[0] = titleCode;
		} else if (found < 0) {
			if (CDriverX68kCmdPriority(titleCode, stopCode, ge) == 0
				&& tryCount_ < (int)_countof(tryCodes_)) {
				for (int i = tryCount_; i > 0; i--)
					tryCodes_[i] = tryCodes_[i - 1];
				tryCodes_[0] = titleCode;
				tryCount_++;
			} else {
				CDriverX68kPushTry(tryCodes_, &tryCount_, (int)_countof(tryCodes_), titleCode);
			}
		}
		/* 明示カタログ選択は権威。静かなイントロ／効果のあとオーディションリストへ落ちると、要求したメールボックスバイトが最初の大きい BGM に置換され、別選択が同じ曲へ収束しループ位置が再起動する。 */
		for (int i = 0; i < ge->titleCount; i++) {
			if (ge->title[i].code == titleCode) { pinned_ = 1; break; }
		}
	}

	/* プレイリストがデッド cmd（FADE OUT）を要求したら、BGM ハント失敗後に一度試す — 稀。要求デッドコードは末尾へ置く方がよい。 */
	if (titleCode && CDriverX68kIsDeadCmd(titleCode, stopCode, ge))
		CDriverX68kPushTry(tryCodes_, &tryCount_, (int)_countof(tryCodes_), titleCode);

	static const unsigned kFallback[] = {
		0xffff, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
		0x18, 0x20, 0x21, 0x28, 0x30, 0x3c, 0x40, 0x41, 0x48, 0x49, 0x4a,
		0x50, 0x60, 0x80, 0x81, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
		0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
		0xb4, 0xb5, 0xb6, 0xb7,
		0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108,
		0x10a, 0x10b, 0x10c, 0x10d, 0x10e, 0x10f,
		0x110, 0x118, 0x120, 0x130, 0x200, 0x201, 0x202, 0x203,
		0x0102, 0x0202, 0x0302, 0x0402, 0x0502, 0x0602, 0x0702, 0x0802,
		0x0902, 0x0a02, 0x0b02, 0x0c02, 0x0d02, 0x0e02, 0x0f02, 0x1002,
		0x1102, 0x1202, 0x1302, 0x1402, 0x1502, 0x1602, 0x1702, 0x1802,
		0x1e02, 0x1f02, 0x2002, 0x2102, 0x2802, 0x3002
	};
	for (int i = 0; i < (int)(sizeof(kFallback) / sizeof(kFallback[0])); i++) {
		if (CDriverX68kIsDeadCmd(kFallback[i], stopCode, NULL)) continue;
		CDriverX68kPushTry(tryCodes_, &tryCount_, (int)_countof(tryCodes_), kFallback[i]);
	}
	if (tryCount_ < 1) {
		tryCodes_[0] = songCode_ ? songCode_ : 1;
		tryCount_ = 1;
	}

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;

	CEmuHardX68kSetActive(hw_);
	CDriverX68kPlantA2Frame(hw_);
	CDriverX68kPlantCFrame(hw_);
	CDriverX68kKeepByakuyaPlay(hw_);
	/* ブート settle: 複数ファイル／trap_f コピーは約 0.5s、単一 BOOT は約 0.35s。チップ Irq() から OPM IRQ エッジが打てるようスライスする。 */
	const int settleHundredths = (ge->romCount > 2) ? 50 : 35;
	const int opmGlue = driverOpmGlue(hw_);
	const unsigned opmLandmark = opmGlue ? driverOpmLandmark(hw_) : 0;
	int opmPostScanOs = 0;
	if (opmGlue) {
		m68k_set_irq(M68K_IRQ_NONE);
		m68k_set_reg(M68K_REG_SR, 0x2700);
	}
	{
		const int total = cpuHz_ * settleHundredths / 100;
		const int slice = cpuHz_ / 200;
		int slices = 0;
		for (int left = total; left > 0; ) {
			CDriverX68kRetargetOpxCompileHeap(hw_);
			CDriverX68kPlantCFrame(hw_);
			const int n = left > slice ? slice : left;
			RunCycles(n);
			CDriverX68kSkipDmacScan(hw_);
			left -= n;
			slices++;
			const int holdScan = (opmGlue && opmLandmark
				&& hw_->Read32(opmLandmark) == 0x48E77FFEu);
			if (holdScan) {
				const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
				if (pc >= 0x94Au && pc < 0x95Au) {
					m68k_set_reg(M68K_REG_SR, 0x2700);
					m68k_set_reg(M68K_REG_PC, 0xB16);
				}
				m68k_set_irq(M68K_IRQ_NONE);
				continue;
			}
			/* Settle 専用再バインド。BOOT が nop;bra* hang ベクタを再植えしたら。PC が DOS イメージ内なら書き換えを飛ばす — 実行中ハンドラの下へトランポリン／trap15 を再出力すると壊す。 */
			if ((slices % 10) == 0) {
				const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
				if (pc < 0xf08000u || pc >= 0xf0c000u)
					CEmuX68kDosInstall(hw_);
			}
			if (hw_->SoundChip()) {
				if (opmGlue && !opmPostScanOs) {
					CEmuX68kDosInstall(hw_);
					opmPostScanOs = 1;
				}
				const int irq = hw_->SoundChip()->Irq() ? 1 : 0;
				/* ack コールバックがチップのイベントラッチをクリアする。Musashi は現在レベルから駆動: 1 CPU スライス内で IRQ が ack され再アサートされ得るので、ホスト側エッジフィルタは新しいタイマイベントを落とす。 */
				m68k_set_irq(irq ? M68K_IRQ_6 : M68K_IRQ_NONE);
				irqWas_ = irq;
			}
		}
	}
	CDriverX68kRetargetOpxCompileHeap(hw_);
	{
		const unsigned isr = hw_->Read32(0x10c) & 0xffffffu;
		const unsigned bak = 0x00A3F800u;
		if (hw_->Read32(0x10a48) == 0x00A00000u
			&& isr >= 0x11000u && isr < 0x14000u
			&& hw_->Read16(isr) != 0x48e7u
			&& hw_->Read16(bak) == 0x48e7u) {
			for (unsigned i = 0; i < 0x80u; i++)
				hw_->Write8(isr + i, hw_->Read8(bak + i));
		}
	}
	m68k_set_irq(M68K_IRQ_NONE);
	/* Settle は irqWas_ を残しチップラッチが生きたまま（または壊れたトランポリン後 IPL が 6 で固まる）ことがある。エッジを再武装しアンマスク。先の JSR vs RTE 不一致でユーザモード＋USP=0（空 mid RAM を走る）ならスーパーバイザも回復。 */
	irqWas_ = 0;
	{
		unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR);
		const unsigned usp = (unsigned)m68k_get_reg(NULL, M68K_REG_USP) & 0xffffffu;
		unsigned isp = (unsigned)m68k_get_reg(NULL, M68K_REG_ISP) & 0xffffffu;
		unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
		const int ipl = (int)((sr >> 8) & 7);
		const int inDos = (pc >= 0xf08000u && pc < 0xf0c000u);
		const int spBad = (isp < 0x200u || isp > 0xfffff0u
			|| (isp >= 0xf08000u && isp < 0xf08700u));
		/* hoot opmdrv.bin: IRQ6 トランポリンは $F08xxx。それを壊れた扱いすると BOOT を再起動し、$48E77FFE スキャン目印を上書きしたあと init を再走し $B32 で回る。columns/comet は inDos 再起動が要る（トランポリンで SSP 破壊）。 */
		const int opmGlue = ((hw_->Read32(0x400) & 0xffffffu) == 0xB06u
			&& hw_->Read16(0xB16) == 0x223Cu);
		const int wrecked = (opmGlue ? (pc == 0x10000u || spBad
				|| (((sr & 0x2000u) == 0u) && usp < 0x100u))
			: (inDos || pc == 0x10000u || spBad
				|| (((sr & 0x2000u) == 0u) && usp < 0x100u)));
		/* Double Eagle: poll は OP.X と同じ $4A4 だが、init が先に $4D6 を jsr し DOS（M_INTON）へ入る。init 途中で $4A4 へ飛ぶと GOLF.X の RTS パッチを飛ばしプローブがハング。$5A7D8 が RTS になるまで DOS に残す。 */
		const int dbleagleBusy = (hw_->Read16(0x4ecu) == 0x41f9u
			&& hw_->Read32(0x4eeu) == 0x00015200u
			&& hw_->Read16(0x5a7d8u) != 0x4e75u);
		if (wrecked && !dbleagleBusy) {
			if (spBad || isp < 0xf0c000u || isp > 0xf0fffeu)
				m68k_set_reg(M68K_REG_ISP, 0xf0fffeu);
			sr = 0x2500u;
			m68k_set_reg(M68K_REG_SR, sr);
			/* OP.X BOOT（rougea/m_and_m）: poll は $4A4 の tst.b $E00000。ここで FindMailboxPoll() しない — Alice の poll は $4F2 で、M_INTON/ADV 前に飛ぶとコンパイルを飛ばす（ayakata コード 5）。 */
			if (hw_->Read16(0x4a4) == 0x4a39u
				&& hw_->Read32(0x4a6) == 0x00e00000u)
				m68k_set_reg(M68K_REG_PC, 0x4a4);
			else if (hw_->Read16(0x4ae) == 0x4a39u
				&& hw_->Read32(0x4b0) == 0x00e00000u)
				m68k_set_reg(M68K_REG_PC, 0x4ae);
			else if (hw_->Read16(0x4b0) == 0x4a39u
				&& hw_->Read32(0x4b2) == 0x00e00000u)
				m68k_set_reg(M68K_REG_PC, 0x4b0);
			else if (hw_->Read16(0x8d0) == 0x4a39u
				&& hw_->Read32(0x8d2) == 0x00e00000u
				&& hw_->Read16(0x4f428) == 0x4e75u) {
				CDriverX68kPlantA2Frame(hw_);
				m68k_set_reg(M68K_REG_PC, 0x8d0);
			}
			else if (driverDoOpmdrv2Poll(hw_))
				m68k_set_reg(M68K_REG_PC, driverDoOpmdrv2Poll(hw_));
			else if (hw_->Read16(0x950) == 0x4a39u
				&& hw_->Read32(0x952) == 0x00e00000u
				&& hw_->Read16(0x1021C) == 0x48e7u) {
				CDriverX68kPlantCaveTrap0(hw_);
				m68k_set_reg(M68K_REG_PC, 0x950);
			}
			else {
				/* 固定 poll 以外の OPMDRV.X BOOT。$4F2 は Alice コンパイル前なので飛ばす。 */
				const unsigned poll = FindMailboxPoll();
				if (poll && poll != 0x4f2u)
					m68k_set_reg(M68K_REG_PC, poll);
				else if (!opmGlue && (inDos || pc == 0x10000u || pc >= 0xf00000u)) {
					const unsigned boot = hw_->Read32(4) & 0xffffffu;
					if (boot >= 0x400u && boot < 0x10000u)
						m68k_set_reg(M68K_REG_PC, boot);
				} else if (opmGlue && (spBad || pc == 0x10000u)) {
					const unsigned boot = hw_->Read32(4) & 0xffffffu;
					if (boot >= 0x400u && boot < 0x10000u)
						m68k_set_reg(M68K_REG_PC, boot);
				}
			}
		} else if (ipl >= 6) {
			m68k_set_reg(M68K_REG_SR, (sr & ~0x0700u) | 0x2000u);
		}
	}
	/* BOOT settle が薄い DOS/IOCS stub を再植えすることがある — 必要なら OS を一度入れ直す */
	CEmuX68kDosInstall(hw_);
	CDriverX68kRestoreA2Play(hw_);
	CDriverX68kPlantA2Frame(hw_);
	CDriverX68kPlantCaveTrap0(hw_);
	CDriverX68kPlantCFrame(hw_);
	{
		/* PC が我々が nop;bra* の上に書いた中和 hang stub（rte;rte）にいるなら、例外フレームから trap RTE を完了する。二重 rte を要求し、生きた IRQ の単一 rte（$546）を奪わない。 */
		const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
		const int ourHangRte = (hw_->Read16(pc) == 0x4e73u
			&& hw_->Read16(pc + 2u) == 0x4e73u
			&& pc >= 0x400u && pc < 0x800u);
		if (ourHangRte) {
			const unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
			const unsigned sr = hw_->Read16(sp);
			const unsigned ret = hw_->Read32(sp + 2u) & 0xffffffu;
			const int retOk = ((ret & 1u) == 0u
				&& ret > 0x100u && ret < 0xf00000u && ret != pc
				&& ret != (pc + 2u) && ret != (pc + 4u));
			if (retOk) {
				m68k_set_reg(M68K_REG_SR, sr);
				m68k_set_reg(M68K_REG_PC, ret);
				m68k_set_reg(M68K_REG_SP, (sp + 6u) & 0xffffffu);
			} else if (hw_->Read16(0x4f2) == 0x4a39u
				&& hw_->Read32(0x4f4) == 0x00e00000u) {
				/* abtengu 系の曲待ち — メールボックス poll を再開 */
				m68k_set_reg(M68K_REG_SR, 0x2500);
				m68k_set_reg(M68K_REG_PC, 0x4f2);
			} else {
				m68k_set_reg(M68K_REG_D0, 0);
				m68k_set_reg(M68K_REG_PC, (pc + 4u) & 0xffffffu);
			}
		}
	}
	hw_->SetPc((unsigned)m68k_get_reg(NULL, M68K_REG_PC));
	booted_ = 1;
	songCode_ = tryCodes_[0];
	cmdIndex_ = 1;
	hw_->SetSongCommand(songCode_);
	opmSpinRescue_ = 0;
	/* D.O. 星の砂物語: settle can land on the IRQ6 trampoline. Snap to the
	   $E00000 poll. Do not pump play here — compiling FM.OPM with IRQ live
	   overwrites OPMDRV2's ISR at $12F8A. */
	{
		const unsigned poll = driverDoOpmdrv2Poll(hw_);
		if (poll) {
			m68k_set_reg(M68K_REG_SR, 0x2500);
			m68k_set_reg(M68K_REG_ISP, 0xf0fffeu);
			m68k_set_reg(M68K_REG_PC, poll);
			hw_->SetPc(poll);
		}
	}
	/* $94A は未割当ベクタ hang（nop;bra*）。opmdrv.bin は M_INIT までそこで回る。Laplace / VMFA BOOT は init 後 IRQ-RTE で同じ stub に乗り $E00000 poll に届かず、固定 16bit コードは無音のまま。$10C が生きた ISR になったら一度スナップ。 */
	if (opmGlue)
		driverRteIrq6(hw_, 1);
	{
		unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
		const unsigned h10 = hw_->Read32(0x10c) & 0xffffffu;
		const int inited = (h10 >= 0x8000u && h10 < 0xf00000u);
		if (pc >= 0x94Au && pc < 0x95Au && inited && hw_->Read16(0x94A) == 0x4e71u)
			ResumeMailboxForSong(songCode_);
		pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
		if (CDriverX68kIs1040MailboxHang(hw_, pc))
			CDriverX68kResume1040Hang(hw_, songCode_);
		hw_->SetPc((unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu);
	}
	/* プレイリスト／カタログ選択をラウドネスハンターで置換しない。メールボックスコマンドは既に武装済み。ロックはフォールスルーを止めるだけ。 */
	if (pinned_)
		locked_ = 1;
	nextCmdAt_ = (uint64_t)cpuHz_ / 60;
	if (hostRate_ > 0) {
		dwellFrames_ = (ge->romCount > 8) ? (hostRate_ + hostRate_ / 2) : (hostRate_ / 2);
		/* プレイリスト選択: フォールスルー前に選択コードへ約 2s 与える */
		if (pinned_)
			dwellFrames_ = hostRate_ * 2;
	} else {
		dwellFrames_ = 22050;
	}
	if (dwellFrames_ < 1) dwellFrames_ = 1;
	dwellLeft_ = dwellFrames_;
	opmAtWindow_ = hw_->OpmWrites();
	dwellExtendUsed_ = 0;
	return 1;
}

/* ハード参照を捨てる */
void CDriverX68k::Close()
{
	hw_ = NULL;
	booted_ = 0;
}

/* 同一 zip の別曲をライブで切替する */
int CDriverX68k::OverlayTitle(unsigned titleCode)
{
	if (!hw_) return 0;
	songCode_ = titleCode ? titleCode : 1;
	pinned_ = 1;
	locked_ = 1;
	hw_->SetSongCommand(songCode_);
	ResumeMailboxForSong(songCode_);
	return 1;
}

/* OPM クロックを CPU 比で進める */
void CDriverX68k::TickOpm(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;
	opmResidual_ += cpuCycles * (uint64_t)opmHz_;
	const uint64_t opmTicks = opmResidual_ / (uint64_t)cpuHz_;
	opmResidual_ %= (uint64_t)cpuHz_;
	if (opmTicks)
		hw_->SoundChip()->AdvanceClocks(opmTicks);
}

/* CDriverX68k::FindMailboxPoll の実装 */
unsigned CDriverX68k::FindMailboxPoll() const
{
	if (!hw_) return 0;
	/* 低い BOOT／早期 RAM を優先。EXDOS が poll を再配置していれば mid も */
	static const unsigned kRanges[][2] = {
		{ 0x0400u, 0x3000u },
		{ 0x10000u, 0x20000u },
		{ 0x80000u, 0xa0000u },
	};
	for (unsigned ri = 0; ri < sizeof(kRanges) / sizeof(kRanges[0]); ri++) {
		const unsigned lo = kRanges[ri][0];
		const unsigned hi = kRanges[ri][1];
		for (unsigned a = lo; a + 6u < hi; a += 2u) {
			if (hw_->Read16(a) != 0x4a39u) continue;
			if (hw_->Read32(a + 2u) != 0x00e00000u) continue;
			return a;
		}
	}
	return 0;
}

/* CDriverX68k::ResumeMailboxForSong の実装 */
void CDriverX68k::ResumeMailboxForSong(unsigned code)
{
	if (!hw_) return;
	hw_->SetSongCommand(code);
	const unsigned poll = FindMailboxPoll();
	if (!poll) return;
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	/* 既に poll ループ内／直後 — メールボックス poke で足りる */
	if (pc >= poll && pc < poll + 0x40u)
		return;
	/* 死んだ INTRO／EXDOS 固まりは PC を mid RAM に残す。曲待ちへ戻し次カタログコードを観測する（BOOT 植込なし — 既存 poll を再開）。 */
	m68k_set_reg(M68K_REG_SR, 0x2500);
	m68k_set_reg(M68K_REG_PC, poll);
	hw_->SetPc(poll);
}

/* CDriverX68k::CallUserHook の実装 */
void CDriverX68k::CallUserHook(unsigned hook, int tickOpmDuring)
{
	if (!hw_ || !hook || softTimerBusy_) return;
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR) & 0xffffu;
	unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
	if (sp < 8u || sp > 0xfffff8u) return;
	softTimerBusy_ = 1;
	/* TIMERDST/VDISPST コールバックは割り込みハンドラで RTE 戻り。サブルーチン RTS ではない。68000 format-0 例外フレーム（SR, PC）を植える。RTS のみだと RTE が偽 SR/PC を消費し、高レート Arcus タイマ tick が毎回安全上限まで走る。 */
	sp = (sp - 6u) & 0xffffffu;
	hw_->Write16(sp, (uint16_t)sr);
	hw_->Write32(sp + 2u, pc);
	m68k_set_reg(M68K_REG_SP, sp);
	m68k_set_reg(M68K_REG_PC, hook);
	int ok = 0;
	for (int n = 0; n < 200000; n += 64) {
		m68k_execute(64);
		/* Soft SD_DRV IRQ6: RunCycles は既にその量子を TickOpm 済み。ここでネスト TickOpm すると「停止」窓中に YM Timer-B が再武装し gra268snd が倍速になる（HW TB が約 16s 後に引き継ぐまで）。 */
		if (tickOpmDuring)
			TickOpm(64);
		const unsigned p = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
		if (p == pc) { ok = 1; break; }
	}
	if (!ok) {
		m68k_set_reg(M68K_REG_PC, pc);
		m68k_set_reg(M68K_REG_SR, sr);
		m68k_set_reg(M68K_REG_SP, (sp + 6u) & 0xffffffu);
	}
	hw_->SetPc((unsigned)m68k_get_reg(NULL, M68K_REG_PC));
	softTimerBusy_ = 0;
}

/* CDriverX68k::CallUserSubroutine の実装 */
void CDriverX68k::CallUserSubroutine(unsigned hook)
{
	if (!hw_ || !hook || softTimerBusy_) return;
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR) & 0xffffu;
	unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
	if (sp < 6u || sp > 0xfffff8u) return;
	softTimerBusy_ = 1;
	sp = (sp - 4u) & 0xffffffu;
	hw_->Write32(sp, pc);
	m68k_set_reg(M68K_REG_SP, sp);
	m68k_set_reg(M68K_REG_PC, hook);
	int ok = 0;
	for (int n = 0; n < 200000; n += 64) {
		m68k_execute(64);
		TickOpm(64);
		if (((unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu) == pc) {
			ok = 1;
			break;
		}
	}
	if (!ok) {
		m68k_set_reg(M68K_REG_PC, pc);
		m68k_set_reg(M68K_REG_SR, sr);
		m68k_set_reg(M68K_REG_SP, (sp + 4u) & 0xffffffu);
	}
	hw_->SetPc((unsigned)m68k_get_reg(NULL, M68K_REG_PC));
	softTimerBusy_ = 0;
}

/* CDriverX68k::ServiceSoftTimers の実装 */
void CDriverX68k::ServiceSoftTimers(int cycles)
{
	if (!hw_ || cycles <= 0 || softTimerBusy_) return;
	/* TIMERDST ($6B): d1.hb=単位（1..7 µs スケール）、d1.b=カウント（0→256） */
	const unsigned timerHook = hw_->Read32(CEMU_X68K_DOS_DATA + 0x10u) & 0xffffffu;
	if (timerHook) {
		const unsigned d1 = hw_->Read16(CEMU_X68K_DOS_DATA + 0x14u);
		const int unit = (int)((d1 >> 8) & 0xffu);
		int count = (int)(d1 & 0xffu);
		if (count == 0) count = 256;
		static const int kUnitUs[8] = { 0, 1, 3, 4, 13, 16, 25, 50 };
		const int us = (unit >= 1 && unit <= 7) ? (kUnitUs[unit] * count) : 1000;
		int periodCy = (int)(((int64_t)cpuHz_ * (us > 0 ? us : 1000)) / 1000000);
		if (periodCy < (cpuHz_ / 4000)) periodCy = cpuHz_ / 4000; /* 上限約 4kHz */
		if (periodCy < 1) periodCy = 1;
		timerDAcc_ += cycles;
		while (timerDAcc_ >= periodCy) {
			timerDAcc_ -= periodCy;
			CallUserHook(timerHook);
		}
	} else {
		const unsigned irq6 = hw_->Read32(0x78) & 0xffffffu;
		const unsigned work = 0x00e81eu;
		/* SD_DRV の非常駐 FM モードは OPM IRQ6 から $500 チャネルバンクをサービス。コマンド遅延経路は最初のシーケンスタick 直前に Timer B を止める。Human68k 常駐 OPM サービスが継続カデンツを供給する。そのバンクが生き HW タイマが止まっている間だけ、ドライバが組んだ $F0 Timer-B レートで既インストール IRQ ベクタをパルスする。 */
		if (hw_->Read16(0x8308u) == 0x48e7u
			&& hw_->Read8(work + 0xd28u) == 0
			&& hw_->Read8(work + 0xd39u) == 0
			&& hw_->Read8(work + 0x501u) >= 0x80
			&& hw_->Read8(work + 0x501u) <= 0x8f
			&& irq6 != 0) {
			/* TB=$F0 壁周期（CPU クロック）: cpuHz * 16*1024 / opmHz。soft IRQ6 内で TickOpm しない（CallUserHook 参照）。 */
			int periodCy = 1;
			if (opmHz_ > 0)
				periodCy = (int)(((int64_t)cpuHz_ * 16384) / (int64_t)opmHz_);
			if (periodCy < 1) periodCy = 1;
			timerDAcc_ += cycles;
			while (periodCy > 0 && timerDAcc_ >= periodCy) {
				timerDAcc_ -= periodCy;
				CallUserHook(irq6, 0);
			}
		}
		/* MUX.R/X（arkanoid2/twinbee/salamander/...）: シーケンサはベクタ $110 の MFP Timer D、ISR は `movem #$F8F4` + a5 相対ワーク。OPMINTST 無し、TCDCR は OPDRV の $75 でもないので YM IRQ6 は走らず、ワンショットキーオンで停止。$110 を tick し続ける（YM Timer B へ渡す OPDRV と違う）。 */
		else {
			const unsigned tdIsr = hw_->Read32(0x110u) & 0xffffffu;
			const int muxTd = (tdIsr >= 0x8000u && tdIsr < 0x40000u
				&& hw_->Read16(tdIsr) == 0x48e7u
				&& hw_->Read16(tdIsr + 2u) == 0xf8f4u);
			if (muxTd) {
				const int periodCy = cpuHz_ / 120;
				timerDAcc_ += cycles;
				while (periodCy > 0 && timerDAcc_ >= periodCy) {
					timerDAcc_ -= periodCy;
					CallUserHook(tdIsr, 0);
				}
			} else if (CEmuChipYm2151KeyOnCount(hw_->SoundChip()) == 0) {
				const unsigned tcdcr = hw_->Read8(0xe8801du);
				const unsigned tw = (tdIsr >= 0x8000u && tdIsr < 0x40000u)
					? hw_->Read16(tdIsr) : 0;
				const int tdLive = ((tcdcr & 7u) != 0
					&& (tw == 0x4a79u || tw == 0x08b9u || tw == 0x48e7u));
				if (tdLive && !(hw_->SoundChip() && hw_->SoundChip()->Irq())) {
					const int periodCy = cpuHz_ / 120;
					timerDAcc_ += cycles;
					while (periodCy > 0 && timerDAcc_ >= periodCy) {
						timerDAcc_ -= periodCy;
						CallUserHook(tdIsr, 0);
						if (hw_->SoundChip() && hw_->SoundChip()->Irq())
							break;
					}
				} else {
					timerDAcc_ = 0;
				}
			} else if (irq6 == 0x001142u
				&& CEmuChipYm2151KeyOnCount(hw_->SoundChip()) > 0) {
				const int periodCy = cpuHz_ / 61;
				timerDAcc_ += cycles;
				while (periodCy > 0 && timerDAcc_ >= periodCy) {
					timerDAcc_ -= periodCy;
					CallUserHook(irq6);
				}
			} else {
				timerDAcc_ = 0;
			}
		}
	}
	/* VDISPST ($6C): 約 60Hz */
	const unsigned vdispHook = hw_->Read32(CEMU_X68K_DOS_DATA + 0x18u) & 0xffffffu;
	if (vdispHook) {
		const int periodCy = cpuHz_ / 60;
		vdispAcc_ += cycles;
		while (periodCy > 0 && vdispAcc_ >= periodCy) {
			vdispAcc_ -= periodCy;
			CallUserHook(vdispHook);
		}
	} else {
		vdispAcc_ = 0;
	}
}

/* CPU を cycles 進める */
void CDriverX68k::RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	CEmuHardX68kSetActive(hw_);
	CDriverX68kFixMidiA4(hw_);
	/* OPM は常に壁時間量子ぶん進める。m68k_execute は $E00800 idle / end_timeslice で早めに戻ることがあり、チップ時間を `got` に縛ると Timer B（と曲）が約半速、音声はリアルタイムのまま。Hoot は YM2151 を CPU ICount ではなくサウンド時基で駆動する。 */
	if (driverOpmGlue(hw_)) {
		/* WRITE 解析＋ヘルパでは IRQ6 をマスク。$88B8 フラグ待ちは生きたまま */
		int left = cycles;
		while (left > 0) {
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
			const unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
			if (driverOpmHoldIrq(hw_, pc, sp))
				m68k_set_irq(M68K_IRQ_NONE);
			const int n = (left > 16) ? 16 : left;
			(void)m68k_execute(n);
			left -= n;
		}
		TickOpm((uint64_t)cycles);
	} else if (driverDoOpmdrv2(hw_) && hw_->SoundChip()) {
		/* 1 量子内で $E00800 end_timeslice を越えて続ける（k4 cmd6log）。IRQ hold はこの系統のコンパイルスタックのみ — グローバルな深い SP hold ではない。poll は $F0FFxx にあり Timer-B を取る。 */
		CChip* chip = hw_->SoundChip();
		int left = cycles;
		while (left > 0) {
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
			const unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
			/* $13E86 busy-wait（tst.b / bne.s）は OPM ISR がクリアする。ここで IRQ を hold すると $10A60 でコンパイルがデッドロック。 */
			if (!driverOpmFlagWait(hw_, pc)
				&& sp >= 0x00F0F000u && sp < 0x00F0FEF0u)
				CDriverX68kApplyIrq(hw_, chip, 1);
			else
				CDriverX68kApplyIrq(hw_, chip, 0);
			const int n = (left > 16) ? 16 : left;
			(void)m68k_execute(n);
			TickOpm((uint64_t)n);
			left -= n;
		}
	} else {
		(void)m68k_execute(cycles);
		TickOpm((uint64_t)cycles);
	}
	hw_->TickMfp(cycles);
	ServiceSoftTimers(cycles);
	hw_->SetPc((unsigned)m68k_get_reg(NULL, M68K_REG_PC));
}

/* CPU とチップを進めステレオ PCM を合成する */
int CDriverX68k::Render(int16_t* stereo, int frames)
{
	if (!hw_ || !stereo || frames <= 0) return 0;
	CChip* chip = hw_->SoundChip();
	if (!chip) return 0;
	CEmuHardX68kSetActive(hw_);

	if (hostRate_ < 1 || cpuHz_ < 1) return 0;

	for (int i = 0; i < frames; i++) {
		if (!locked_) {
			if (dwellLeft_ <= 0) {
				if (windowPeak_ > bestPeak_) {
					bestPeak_ = windowPeak_;
					bestSongCode_ = songCode_;
				}
				windowPeak_ = 0;
				/* OPM トラフィックは多いがまだ無音: 次コード探索の前に追加ドウェル 1 回（遅いキーオンの $94A パックでフレーズ途中再起動を避ける） */
				const unsigned opmNow = hw_->OpmWrites();
				if (!dwellExtendUsed_ && bestPeak_ <= 800
					&& opmNow > opmAtWindow_ + 800u) {
					dwellExtendUsed_ = 1;
					dwellLeft_ = dwellFrames_;
					opmAtWindow_ = opmNow;
				} else if (bestPeak_ > 800) {
					/* 聞こえたらロック — 毎秒 SetSongCommand し直さない（ZMUSIC/OPMDRV をフレーズ途中で再起動する） */
					locked_ = 1;
					if (songCode_ != bestSongCode_) {
						songCode_ = bestSongCode_;
						hw_->SetSongCommand(songCode_);
					}
				} else if (cmdIndex_ < tryCount_) {
					songCode_ = tryCodes_[cmdIndex_++];
					ResumeMailboxForSong(songCode_);
					dwellLeft_ = dwellFrames_;
					opmAtWindow_ = opmNow;
					dwellExtendUsed_ = 0;
				} else {
					locked_ = 1;
					songCode_ = bestSongCode_ ? bestSongCode_ : 1;
					ResumeMailboxForSong(songCode_);
				}
			} else {
				dwellLeft_--;
			}
		}

		cpuAcc_ += (int64_t)cpuHz_;
		int cyclesPerSample = (int)(cpuAcc_ / (int64_t)hostRate_);
		cpuAcc_ %= (int64_t)hostRate_;
		if (cyclesPerSample < 1) cyclesPerSample = 1;
		RunCycles(cyclesPerSample);
		CDriverX68kSkipDmacScan(hw_);
		{
			/* OP.X/rougea: ネスト RTE が TRAP#1（$F08740）に着地し PC が stub で回る。本物フレームを完了し、だめならメールボックス poll を再開。 */
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
			if (pc >= CEMU_X68K_DOS_TRAP1 && pc < (CEMU_X68K_DOS_TRAP1 + 8u)) {
				if (!driverRteIrq6(hw_, 0) && !driverDoOpmdrv2(hw_))
					ResumeMailboxForSong(songCode_);
			}
		}
		if (!opmSpinRescue_) {
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
			const unsigned h10 = hw_->Read32(0x10c) & 0xffffffu;
			/* $94A hang は opmdrv.bin の nop;bra*。D.O. OPMDRV2 play は $950（moveq #3,d7 / rol.l）— hang として奪わない。 */
			if (pc >= 0x94Au && pc < 0x95Au && h10 >= 0x8000u && h10 < 0xf00000u
				&& hw_->Read16(0x94A) == 0x4e71u) {
				opmSpinRescue_ = 1;
				ResumeMailboxForSong(songCode_);
			} else if (CDriverX68kIs1040MailboxHang(hw_, pc)) {
				CDriverX68kResume1040Hang(hw_, songCode_);
			}
		}
		/* YM2151 IRQ6、なければ MFP Timer C/D IRQ2 */
		{
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
			const unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
			const int hold = (driverOpmGlue(hw_) && driverOpmHoldIrq(hw_, pc, sp))
				|| (driverDoOpmdrv2(hw_)
					&& !driverOpmFlagWait(hw_, pc)
					&& sp >= 0x00F0F000u && sp < 0x00F0FEF0u);
			CDriverX68kApplyIrq(hw_, chip, hold);
			irqWas_ = chip->Irq() ? 1 : 0;
		}
		chip->Render(stereo + i * 2, 1);
		hw_->MixAdpcm(stereo + i * 2, 1);
		if (!locked_) {
			const int16_t l = stereo[i * 2];
			const int16_t r = stereo[i * 2 + 1];
			int a = l < 0 ? -l : l;
			int b = r < 0 ? -r : r;
			if (b > a) a = b;
			if (a > windowPeak_) windowPeak_ = a;
		}
	}
	return frames;
}

/* Seek は未対応 */
int CDriverX68k::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
