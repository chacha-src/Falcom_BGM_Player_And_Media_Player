#include "StdAfx.h"
#include "cemu_driver_x68k.h"
#include "../chip/cemu_chip_opm.h"
#include "../machine/cemu_x68k_dos.h"
#include "../cemu_types.h"
extern "C" {
#include "../vendor/musashi/m68k.h"
}
#include <string.h>

/* StarCraft compile BOOT plants $A00000 at $10A48 (low) but leaves $10A44
   at the OP.X data section (~$152D2). MML compile fills downward through
   the ISR. Move the bump to the DOS heap top once the low plant is visible. */
static void CDriverX68kRetargetOpxCompileHeap(CHardX68k* hw)
{
	if (!hw) return;
	if (hw->Read32(0x10a48) != 0x00A00000u) return;
	const unsigned bump = hw->Read32(0x10a44);
	if (bump >= 0x8000u && bump < 0x20000u)
		hw->Write32(0x10a44, 0x00A40000u);
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

static int driverOpmGlue(CHardX68k* hw)
{
	if (!hw) return 0;
	return ((hw->Read32(0x400) & 0xffffffu) == 0xB06u
		&& hw->Read16(0xB16) == 0x223Cu);
}

/* WRITE (`lea -0x200,sp` at $808C) plus a Timer-B edge lets the ISR rte a
   frame from the scratch pad. Parse continues PAST $88B0 (dispatcher $8A90,
   OPMSET $A804), so a PC-only hold through $88B0 still smashed longer
   compiles. Hold while SSP is on that scratch pad, except in OPMDRV's
   `tst.b $24xx / bne.s` flag wait (haou $88B8 $2490, sshang $88E2 $24EC)
   which WRITE calls and which needs Timer-B. Play's ISR keeps the high
   stack ($F0FFxx) so it is not held. */
static int driverOpmFlagWait(CHardX68k* hw, unsigned pc)
{
	if (!hw) return 0;
	return hw->Read16(pc) == 0x4A39u && hw->Read16(pc + 6u) == 0x66F8u;
}

static int driverOpmHoldIrq(CHardX68k* hw, unsigned pc, unsigned sp)
{
	if (driverOpmFlagWait(hw, pc))
		return 0;
	/* WRITE's lea -0x200,sp. Mailbox poll stays on $F0FFxx so it is not
	   held. Require PC in OPMDRV so cmd6 IOCS at $F08xxx can take Timer-B
	   (flag-wait is already excluded above). */
	if (sp >= 0x00F0F000u && sp < 0x00F0FEF0u
		&& pc >= 0x8000u && pc < 0xE000u)
		return 1;
	return 0;
}

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

/* Finish a live IRQ6 trampoline. skipSpin=1 leaves PC in the trampoline when
   the stacked return is the unassigned-vector nop-slide at $94A. */
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
	/* TRAP#1 stub is $F08740; a smashed nest can RTE onto itself. */
	if (ret >= CEMU_X68K_DOS_IRQ6 && ret < (CEMU_X68K_DOS_IRQ6 + 0x50u))
		return 0;
	if ((ret & 1u) || ret < 0x400u || ret >= 0xf00000u)
		return 0;
	m68k_set_reg(M68K_REG_SR, hw->Read16(sp));
	m68k_set_reg(M68K_REG_PC, ret);
	m68k_set_reg(M68K_REG_SP, (sp + 6u) & 0xffffffu);
	return 1;
}

CDriverX68k::~CDriverX68k()
{
	Close();
}

unsigned CDriverX68k::OpmWrites() const
{
	return hw_ ? hw_->OpmWrites() : 0;
}

unsigned CDriverX68k::Pc() const
{
	return hw_ ? ((unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu) : 0;
}

static void CDriverX68kPushTry(unsigned* dst, int* n, int cap, unsigned code)
{
	if (!dst || !n || *n >= cap) return;
	/* Skip Stop=0x5f for mailbox spam; 0xffff is valid play. */
	if (code == 0x5f) return;
	for (int i = 0; i < *n; i++) {
		if (dst[i] == code) return;
	}
	dst[(*n)++] = code;
}

/* Konami gra2 etc.: stop=0xf0, fade=0xf9 — silent if pinned alone. */
static int CDriverX68kIsDeadCmd(unsigned code, unsigned stopCode)
{
	if (code == 0x5f || code == 0xf9 || code == 0xff) return 1;
	if (stopCode && code == stopCode) return 1;
	return 0;
}

/* Prefer BGM-ish codes (0xA0..0xEF) before SFX / utility. */
static int CDriverX68kCmdPriority(unsigned code, unsigned stopCode)
{
	if (CDriverX68kIsDeadCmd(code, stopCode)) return 3;
	if (code >= 0xa0 && code <= 0xef) return 0;
	if (code >= 0x80 && code < 0xa0) return 1;
	return 2;
}

static unsigned CDriverX68kStopCode(const CEmuGameEntry* ge)
{
	if (!ge) return 0xf0;
	for (int i = 0; i < ge->optCount; i++) {
		if (_stricmp(ge->opt[i].name, "stop") == 0) {
			unsigned v = (unsigned)strtoul(ge->opt[i].value, NULL, 0);
			return v ? v : 0xf0;
		}
	}
	return 0x5f; /* generic OPMDRV / ZMUSIC */
}

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

	/* Catalog titles first (XML order, BGM-priority sorted). Open(...,1) must
	   NOT steal slot 0 ahead of catalog 0x18/etc — aquales INTRO sticks and
	   never hears later codes without mailbox resume. */
	unsigned catalog[CEMU_TITLE_MAX];
	int catalogN = 0;
	unsigned deferred[64];
	int deferredN = 0;
	for (int i = 0; i < ge->titleCount && catalogN < (int)_countof(catalog); i++) {
		const unsigned c = ge->title[i].code;
		if (c == 0 || CDriverX68kIsDeadCmd(c, stopCode)) continue;
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
	/* Sort catalog: BGM (0xA0+) before SFX. */
	for (int a = 0; a < catalogN; a++) {
		for (int b = a + 1; b < catalogN; b++) {
			if (CDriverX68kCmdPriority(catalog[b], stopCode)
				< CDriverX68kCmdPriority(catalog[a], stopCode)) {
				unsigned t = catalog[a]; catalog[a] = catalog[b]; catalog[b] = t;
			}
		}
	}
	for (int i = 0; i < catalogN; i++)
		CDriverX68kPushTry(tryCodes_, &tryCount_, (int)_countof(tryCodes_), catalog[i]);
	for (int i = 0; i < deferredN; i++)
		CDriverX68kPushTry(tryCodes_, &tryCount_, (int)_countof(tryCodes_), deferred[i]);

	/* Put Open(titleCode) first when it is a catalog entry (playlist / batch
	   Open(...,1)). Dead INTRO sticks are recovered by ResumeMailboxForSong
	   when hunting later codes (aquales 0x18). Keep 0xA0+ BGM prepend for
	   non-catalog playlist picks. */
	if (titleCode && !CDriverX68kIsDeadCmd(titleCode, stopCode)) {
		int found = -1;
		for (int i = 0; i < tryCount_; i++) {
			if (tryCodes_[i] == titleCode) { found = i; break; }
		}
		if (found > 0) {
			for (int i = found; i > 0; i--)
				tryCodes_[i] = tryCodes_[i - 1];
			tryCodes_[0] = titleCode;
		} else if (found < 0) {
			if (CDriverX68kCmdPriority(titleCode, stopCode) == 0
				&& tryCount_ < (int)_countof(tryCodes_)) {
				for (int i = tryCount_; i > 0; i--)
					tryCodes_[i] = tryCodes_[i - 1];
				tryCodes_[0] = titleCode;
				tryCount_++;
			} else {
				CDriverX68kPushTry(tryCodes_, &tryCount_, (int)_countof(tryCodes_), titleCode);
			}
		}
		/* Any explicit catalog selection is authoritative. Falling through
		   the audition list after a quiet intro/effect replaced the requested
		   mailbox byte with the first loud BGM, making different selections
		   converge on the same song and restarting its loop position. */
		for (int i = 0; i < ge->titleCount; i++) {
			if (ge->title[i].code == titleCode) { pinned_ = 1; break; }
		}
	}

	/* If playlist asked for a dead cmd (FADE OUT), still try it once after BGM
	   hunt fails — rare. Prefer putting requested dead code at end. */
	if (titleCode && CDriverX68kIsDeadCmd(titleCode, stopCode))
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
		if (CDriverX68kIsDeadCmd(kFallback[i], stopCode)) continue;
		CDriverX68kPushTry(tryCodes_, &tryCount_, (int)_countof(tryCodes_), kFallback[i]);
	}
	if (tryCount_ < 1) {
		tryCodes_[0] = songCode_ ? songCode_ : 1;
		tryCount_ = 1;
	}

	if (!hw_->LoadRoms(fs, ge, titleCode))
		return 0;

	CEmuHardX68kSetActive(hw_);
	/* Boot settle: multi-file / trap_f copy needs ~0.5s; single BOOT ~0.35s.
	   Slice so OPM IRQ edges can fire from chip Irq(). */
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
			const int n = left > slice ? slice : left;
			RunCycles(n);
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
			/* Settle-only rebind if BOOT re-plants nop;bra* hang vectors.
			   Skip rewrite while PC sits in our DOS image — re-emitting
			   trampoline/trap15 under the PC smashes the running handler. */
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
				/* The acknowledge callback clears the chip's event latch.
				   Drive Musashi from its current level: an IRQ can be acked and
				   reasserted entirely inside one CPU slice, so host-side edge
				   filtering here loses the new timer event. */
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
	/* Settle may leave irqWas_ set while the chip latch is still live (or
	   IPL stuck at 6 after a smashed trampoline). Re-arm edges and unmask.
	   Also recover supervisor if a prior JSR-vs-RTE mismatch left us in
	   user mode with USP=0 (PC running through empty mid RAM). */
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
		/* hoot opmdrv.bin: IRQ6 trampoline lives in $F08xxx. Treating that as
		   wrecked restarted BOOT, re-ran init after it had overwritten the
		   $48E77FFE scan landmark, and spun at $B32. columns/comet still need
		   the inDos restart (smashed SSP at the trampoline). */
		const int opmGlue = ((hw_->Read32(0x400) & 0xffffffu) == 0xB06u
			&& hw_->Read16(0xB16) == 0x223Cu);
		const int wrecked = (opmGlue ? (pc == 0x10000u || spBad
				|| (((sr & 0x2000u) == 0u) && usp < 0x100u))
			: (inDos || pc == 0x10000u || spBad
				|| (((sr & 0x2000u) == 0u) && usp < 0x100u)));
		if (wrecked) {
			if (spBad || isp < 0xf0c000u || isp > 0xf0fffeu)
				m68k_set_reg(M68K_REG_ISP, 0xf0fffeu);
			sr = 0x2500u;
			m68k_set_reg(M68K_REG_SR, sr);
			/* OP.X BOOT (rougea/m_and_m): poll is tst.b $E00000 at $4A4.
			   Do NOT FindMailboxPoll() here — Alice's poll is $4F2 and
			   jumping there before M_INTON/ADV skips compile (ayakata code 5). */
			if (hw_->Read16(0x4a4) == 0x4a39u
				&& hw_->Read32(0x4a6) == 0x00e00000u)
				m68k_set_reg(M68K_REG_PC, 0x4a4);
			else if (hw_->Read16(0x4ae) == 0x4a39u
				&& hw_->Read32(0x4b0) == 0x00e00000u)
				m68k_set_reg(M68K_REG_PC, 0x4ae);
			else if (hw_->Read16(0x4b0) == 0x4a39u
				&& hw_->Read32(0x4b2) == 0x00e00000u)
				m68k_set_reg(M68K_REG_PC, 0x4b0);
			else if (!opmGlue && (inDos || pc == 0x10000u || pc >= 0xf00000u)) {
				const unsigned boot = hw_->Read32(4) & 0xffffffu;
				if (boot >= 0x400u && boot < 0x10000u)
					m68k_set_reg(M68K_REG_PC, boot);
			} else if (opmGlue && (spBad || pc == 0x10000u)) {
				const unsigned boot = hw_->Read32(4) & 0xffffffu;
				if (boot >= 0x400u && boot < 0x10000u)
					m68k_set_reg(M68K_REG_PC, boot);
			}
		} else if (ipl >= 6) {
			m68k_set_reg(M68K_REG_SR, (sr & ~0x0700u) | 0x2000u);
		}
	}
	/* BOOT settle may re-plant thin DOS/IOCS stubs — reinstall OS once if needed. */
	CEmuX68kDosInstall(hw_);
	{
		/* If PC sits on a neutralized hang stub (rte;rte) that WE wrote over
		   nop;bra*, complete the trap RTE from the exception frame.
		   Require double-rte so we never steal a live IRQ's single rte ($546). */
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
				/* abtengu-family song wait — resume mailbox poll. */
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
	/* hoot opmdrv.bin: $94A is the unassigned-vector spin. Snap to the mailbox
	   only after M_INIT ($10C live) so we never skip OPMDRV bring-up. */
	if (opmGlue) {
		driverRteIrq6(hw_, 1);
		const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
		const unsigned h10 = hw_->Read32(0x10c) & 0xffffffu;
		const int inited = (h10 >= 0x8000u && h10 < 0xf00000u);
		if (pc >= 0x94Au && pc < 0x95Au && inited)
			ResumeMailboxForSong(songCode_);
		hw_->SetPc((unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu);
	}
	/* A playlist/catalog pick must never be replaced by the loudness hunter.
	   The mailbox command is already armed; locking only disables fallthrough. */
	if (pinned_)
		locked_ = 1;
	nextCmdAt_ = (uint64_t)cpuHz_ / 60;
	if (hostRate_ > 0) {
		dwellFrames_ = (ge->romCount > 8) ? (hostRate_ + hostRate_ / 2) : (hostRate_ / 2);
		/* Playlist pick: give selected code ~2s before falling through. */
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

void CDriverX68k::Close()
{
	hw_ = NULL;
	booted_ = 0;
}

void CDriverX68k::TickOpm(uint64_t cpuCycles)
{
	if (!hw_ || !hw_->SoundChip() || cpuCycles == 0) return;
	opmResidual_ += cpuCycles * (uint64_t)opmHz_;
	const uint64_t opmTicks = opmResidual_ / (uint64_t)cpuHz_;
	opmResidual_ %= (uint64_t)cpuHz_;
	if (opmTicks)
		hw_->SoundChip()->AdvanceClocks(opmTicks);
}

unsigned CDriverX68k::FindMailboxPoll() const
{
	if (!hw_) return 0;
	/* Prefer low BOOT / early RAM; also mid if EXDOS relocated the poll. */
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

void CDriverX68k::ResumeMailboxForSong(unsigned code)
{
	if (!hw_) return;
	hw_->SetSongCommand(code);
	const unsigned poll = FindMailboxPoll();
	if (!poll) return;
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	/* Already in / just after the poll loop — mailbox poke is enough. */
	if (pc >= poll && pc < poll + 0x40u)
		return;
	/* Dead INTRO / EXDOS sticks leave PC in mid-RAM; return to song wait so
	   the next catalog code is observed (no BOOT plant — resume existing poll). */
	m68k_set_reg(M68K_REG_SR, 0x2500);
	m68k_set_reg(M68K_REG_PC, poll);
	hw_->SetPc(poll);
}

void CDriverX68k::CallUserHook(unsigned hook, int tickOpmDuring)
{
	if (!hw_ || !hook || softTimerBusy_) return;
	const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
	const unsigned sr = (unsigned)m68k_get_reg(NULL, M68K_REG_SR) & 0xffffu;
	unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
	if (sp < 8u || sp > 0xfffff8u) return;
	softTimerBusy_ = 1;
	/* TIMERDST/VDISPST callbacks are interrupt handlers and return with RTE,
	   not subroutines returning with RTS. Plant a 68000 format-0 exception
	   frame (SR, PC); an RTS-only frame makes RTE consume a bogus SR/PC and
	   each high-rate Arcus timer tick runs to the safety limit. */
	sp = (sp - 6u) & 0xffffffu;
	hw_->Write16(sp, (uint16_t)sr);
	hw_->Write32(sp + 2u, pc);
	m68k_set_reg(M68K_REG_SP, sp);
	m68k_set_reg(M68K_REG_PC, hook);
	int ok = 0;
	for (int n = 0; n < 200000; n += 64) {
		m68k_execute(64);
		/* Soft SD_DRV IRQ6: RunCycles already TickOpm'd the quantum. Nested
		   TickOpm here re-armed YM Timer-B during "stopped" windows and
		   double-stepped gra268snd (~2× until HW TB took over ~16s later). */
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

void CDriverX68k::ServiceSoftTimers(int cycles)
{
	if (!hw_ || cycles <= 0 || softTimerBusy_) return;
	/* TIMERDST ($6B): d1.hb=unit (1..7 µs scale), d1.b=count (0→256). */
	const unsigned timerHook = hw_->Read32(CEMU_X68K_DOS_DATA + 0x10u) & 0xffffffu;
	if (timerHook) {
		const unsigned d1 = hw_->Read16(CEMU_X68K_DOS_DATA + 0x14u);
		const int unit = (int)((d1 >> 8) & 0xffu);
		int count = (int)(d1 & 0xffu);
		if (count == 0) count = 256;
		static const int kUnitUs[8] = { 0, 1, 3, 4, 13, 16, 25, 50 };
		const int us = (unit >= 1 && unit <= 7) ? (kUnitUs[unit] * count) : 1000;
		int periodCy = (int)(((int64_t)cpuHz_ * (us > 0 ? us : 1000)) / 1000000);
		if (periodCy < (cpuHz_ / 4000)) periodCy = cpuHz_ / 4000; /* cap ~4kHz */
		if (periodCy < 1) periodCy = 1;
		timerDAcc_ += cycles;
		while (timerDAcc_ >= periodCy) {
			timerDAcc_ -= periodCy;
			CallUserHook(timerHook);
		}
	} else {
		const unsigned irq6 = hw_->Read32(0x78) & 0xffffffu;
		const unsigned work = 0x00e81eu;
		/* SD_DRV's non-resident FM mode services the $500 channel bank from
		   OPM IRQ6. Its command-delay path stops Timer B immediately before
		   the first sequence tick; Human68k's resident OPM service supplies
		   the continuing cadence. Pulse the already-installed IRQ vector at
		   the driver's programmed $F0 Timer-B rate only while that bank is
		   active and the hardware timer is stopped. */
		if (hw_->Read16(0x8308u) == 0x48e7u
			&& hw_->Read8(work + 0xd28u) == 0
			&& hw_->Read8(work + 0xd39u) == 0
			&& hw_->Read8(work + 0x501u) >= 0x80
			&& hw_->Read8(work + 0x501u) <= 0x8f
			&& irq6 != 0) {
			/* TB=$F0 wall period in CPU clocks: cpuHz * 16*1024 / opmHz.
			   Do not TickOpm inside the soft IRQ6 (see CallUserHook). */
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
		/* MUX.R/X (arkanoid2/twinbee/salamander/...): sequencer is MFP Timer D
		   at vector $110, ISR `movem #$F8F4` + a5-relative work. No OPMINTST
		   and TCDCR is not OPDRV's $75, so YM IRQ6 never runs — one-shot
		   key-on then STOPS. Keep ticking $110 (unlike OPDRV which hands off
		   to YM Timer B). */
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
				const int tdLive = (tcdcr == 0x75u
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
	/* VDISPST ($6C): ~60Hz. */
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

void CDriverX68k::RunCycles(int cycles)
{
	if (!hw_ || cycles <= 0) return;
	CEmuHardX68kSetActive(hw_);
	/* Always advance OPM by the full wall-time quantum. m68k_execute may
	   return early on $E00800 idle / end_timeslice — tying chip time to
	   `got` made Timer B (and music) run ~half speed while audio kept
	   real time. Hoot drives YM2151 from the sound timebase, not CPU ICount. */
	if (driverOpmGlue(hw_)) {
		/* Mask IRQ6 in WRITE parse + helpers; $88B8 flag-wait stays live. */
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
	} else {
		(void)m68k_execute(cycles);
	}
	TickOpm((uint64_t)cycles);
	ServiceSoftTimers(cycles);
	hw_->SetPc((unsigned)m68k_get_reg(NULL, M68K_REG_PC));
}

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
				/* High OPM traffic but still silent: give one extra dwell before
				   hunting the next code (avoids restarting mid-phrase on $94A
				   packs that key late). */
				const unsigned opmNow = hw_->OpmWrites();
				if (!dwellExtendUsed_ && bestPeak_ <= 800
					&& opmNow > opmAtWindow_ + 800u) {
					dwellExtendUsed_ = 1;
					dwellLeft_ = dwellFrames_;
					opmAtWindow_ = opmNow;
				} else if (bestPeak_ > 800) {
					/* Lock once audible — do NOT re-SetSongCommand every second
					   (that restarts ZMUSIC/OPMDRV mid-phrase). */
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
		{
			/* OP.X/rougea: nest RTE lands on TRAP#1 ($F08740) with PC looping
			   on the stub. Finish a real frame, else resume the mailbox poll. */
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
			if (pc >= CEMU_X68K_DOS_TRAP1 && pc < (CEMU_X68K_DOS_TRAP1 + 8u)) {
				if (!driverRteIrq6(hw_, 0))
					ResumeMailboxForSong(songCode_);
			}
		}
		if (!opmSpinRescue_ && driverOpmGlue(hw_)) {
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
			const unsigned h10 = hw_->Read32(0x10c) & 0xffffffu;
			if (pc >= 0x94Au && pc < 0x95Au && h10 >= 0x8000u && h10 < 0xf00000u) {
				opmSpinRescue_ = 1;
				ResumeMailboxForSong(songCode_);
			}
		}
		/* YM2151 IRQ6. CEmuX68kIntAck clears the chip's event latch, so the
		   current level is safe even when the guest leaves YM status set.
		   Do not edge-filter: ack+reassert can occur within RunCycles(). */
		{
			const unsigned pc = (unsigned)m68k_get_reg(NULL, M68K_REG_PC) & 0xffffffu;
			const unsigned sp = (unsigned)m68k_get_reg(NULL, M68K_REG_SP) & 0xffffffu;
			const int hold = (driverOpmGlue(hw_) && driverOpmHoldIrq(hw_, pc, sp));
			if (hold) {
				m68k_set_irq(M68K_IRQ_NONE);
			} else {
				const int irq = chip->Irq() ? 1 : 0;
				m68k_set_irq(irq ? M68K_IRQ_6 : M68K_IRQ_NONE);
				irqWas_ = irq;
			}
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

int CDriverX68k::Seek(uint64_t sample)
{
	(void)sample;
	return 0;
}
