#pragma once
#include <stdint.h>

/* Minimal MS-DOS for PC-98 PMD / glue-stub rehost (hootrip MiniDos port). */

enum {
	DOS98_TRAMP_SEG = 0x0060,
	DOS98_CALL_RET_OFF = 0x0200,
	DOS98_SYSVARS_SEG = 0x0050,
	DOS98_LOL_OFF = 0x0010,
	/* Environment block (PSP:002C). Must sit above trampoline
	   0x60:0000..01FF (lin 0x600..0x7FF) — 0x70 overlapped and was wiped.
	   The block needs a real arena header one paragraph below it: TSRs that
	   shrink their environment read the size out of it (OPNDRV.COM did, got
	   the trampoline's tail as a paragraph count, and copied garbage over
	   its own code).  0x7F:0000 is the last paragraph of the trampoline
	   table, holding the stubs for INT F8-FF; those go to a bare IRET
	   instead (see InstallTrampolines) because nothing on PC-98 uses them,
	   and moving the environment itself instead broke titles that reach it
	   at a fixed address. */
	DOS98_ENV_MCB_SEG = 0x007F,
	DOS98_ENV_SEG = 0x0080,
	DOS98_ARENA_START = 0x1000,
	DOS98_ARENA_END = 0xA000,
	DOS98_IDLE_SEG = 0x00A0,
	DOS98_FILE_MAX = 256,
	DOS98_HANDLE_MAX = 256,
	DOS98_NAME = 96
};

enum CEmuDos98Result {
	DOS98_CONTINUE = 0,
	DOS98_TERMINATED = 1,
	DOS98_RESIDENT = 2
};

struct CEmuDos98File {
	char name[DOS98_NAME];
	unsigned char* data;
	unsigned size;
};

struct CEmuDos98Handle {
	int used;
	char name[DOS98_NAME];
	unsigned pos;
};

class CEmuDos98 {
public:
	CEmuDos98();
	~CEmuDos98();

	void Reset();
	void InstallTrampolines(uint8_t* mem);
	void InitArena(uint8_t* mem);
	void InstallDosStructures(uint8_t* mem, unsigned memKb = 640,
		const char* blaster = "BLASTER=A220 I5 D1 H5 T6");

	void AddFile(const char* name, const unsigned char* data, unsigned size);
	void SetHandle(uint16_t handle, const char* name);
	void SetHandleText(uint16_t handle, const char* text);

	int ResolveProgram(const char* name, const unsigned char** outData, unsigned* outSize, int* outIsExe) const;
	int LoadCom(uint8_t* mem, const unsigned char* image, unsigned imageSize, const char* tail);
	int LoadExe(uint8_t* mem, const unsigned char* image, unsigned imageSize, const char* tail);
	void LoadOverlay(uint8_t* mem, const unsigned char* image, unsigned imageSize, uint16_t loadSeg, uint16_t reloc) const;
	/* Load a .SYS character device image; returns load segment (CS=loadSeg). */
	int LoadDeviceImage(uint8_t* mem, const char* name, uint16_t* outSeg,
		uint16_t* outStratOff, uint16_t* outIntrOff) const;

	int TrapVector(uint16_t cs, uint16_t ip, uint8_t* outVec) const;
	CEmuDos98Result ServiceInt(uint8_t* mem, uint8_t vec);
	CEmuDos98Result ServiceIntInner(uint8_t* mem, uint8_t vec);
	void IretReturn(uint8_t* mem);

	/* INT 1Ah is the BIOS time-of-day on PC/AT but the CG access BIOS on
	   PC-98, so the host machine has to opt in. */
	void SetPcAtBios(int on) { pcAtBios_ = on ? 1 : 0; }

	int VectorInstalled(uint8_t vec) const;
	uint16_t PspSeg() const { return pspSeg_; }
	const CEmuDos98File* FindFile(const char* name) const;
	int AllocBlock(uint8_t* mem, uint16_t paras, uint16_t* outSeg);

	unsigned readLogCount_;
	uint16_t readLogHandle_[32];
	unsigned readLogBytes_[32];

	/* A DOS call this layer does not implement returns "success, did
	   nothing", which is indistinguishable from a working call until the
	   driver ends up silent. Record what fell through so a sweep can rank
	   the missing services by how many archives need them. */
	uint8_t unhandledFn_[256];  /* INT 21h AH */
	uint8_t unhandledVec_[256]; /* other INT vectors */
	/* INT 18h is the PC-98 keyboard/CRT BIOS and only AH=00/01 are answered;
	   everything else returns whatever was in the registers. Rank the AH
	   values that fall through the same way as the INT 21h ones. */
	uint8_t unhandledInt18_[256];

	/* Optional call log. The interesting window is short — what the resident
	   driver asks for between the play trigger and the first note — so a
	   small ring that the host arms on demand is enough. */
	struct Call {
		uint8_t vec;
		uint16_t ax, bx, cx, dx; /* on entry */
		uint16_t rax;            /* on return */
		uint16_t cs, ip;         /* caller, for disassembly */
		int8_t cf;
		char name[24];
	};
	void TraceReset(int on) { traceOn_ = on; traceCount_ = 0; }
	/* Boot-time tracing has to survive Reset(), which the shell run calls
	   before the host has any handle on the DOS object. */
	static void TraceDefault(int on) { traceDefault_ = on; }
	/* Ring: a boot sequence makes thousands of calls and the interesting part
	   is the tail, so keep the newest kTraceMax and report the true total. */
	unsigned TraceTotal() const { return traceCount_; }
	unsigned TraceCount() const
	{
		return traceCount_ < kTraceMax ? traceCount_ : (unsigned)kTraceMax;
	}
	const Call& TraceAt(unsigned i) const
	{
		if (traceOn_ == 2) return trace_[i];
		const unsigned n = TraceCount();
		return trace_[(traceCount_ - n + i) % kTraceMax];
	}

private:
	void FreeFiles();
	void UpperCopy(char* dst, int dstCap, const char* src) const;
	CEmuDos98File* FindFileMut(const char* name);
	void WriteMcb(uint8_t* mem, uint16_t seg, uint8_t sig, uint16_t owner, uint16_t size) const;
	uint16_t MaxFreeBlock(const uint8_t* mem) const;
	int Alloc(uint8_t* mem, uint16_t paras, uint16_t owner, uint16_t* outSeg) const;
	void FreeBlock(uint8_t* mem, uint16_t dataSeg) const;
	void Coalesce(uint8_t* mem) const;
	int Resize(uint8_t* mem, uint16_t dataSeg, uint16_t paras, uint16_t* errMax) const;
	void BuildPsp(uint8_t* mem, uint16_t pspSeg, uint16_t memTop, const char* tail, uint16_t envSeg) const;
	CEmuDos98Result Int21(uint8_t* mem);
	void Int18();
	void ReadCstr(const uint8_t* mem, uint16_t seg, uint16_t off, char* out, int outCap) const;
	void SetCf(int on);
	void SetAl(uint8_t v);
	uint8_t Ah() const;
	uint8_t Al() const;

	CEmuDos98File files_[DOS98_FILE_MAX];
	int fileCount_;
	CEmuDos98Handle handles_[DOS98_HANDLE_MAX];
	uint16_t nextHandle_;
	uint16_t pspSeg_;
	uint16_t dtaSeg_;
	uint16_t dtaOff_;
	uint8_t installed_[256];
	uint16_t instSeg_[256];
	uint16_t instOff_[256];

	/* AH=4E/4F state. The search set is the archive's file list, so the
	   pattern and a cursor into files_ is the whole handle. */
	int FindMatch(uint8_t* mem);
	char findPat_[DOS98_NAME];
	int findNext_;
	uint16_t allocStrategy_;
	int pcAtBios_;

	enum { kTraceMax = 512 };
	static int traceDefault_;
	int traceOn_;
	unsigned traceCount_;
	Call trace_[kTraceMax];
};
