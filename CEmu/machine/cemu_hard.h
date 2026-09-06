#pragma once
#include "../cemu_types.h"
#include "../cemu_zipfs.h"
#include "../z80/Ay_Cpu.h"

class CChip;

/* CPU + メモリ + チップ + I/O バス */
class CHard {
public:
	enum { KIND_UNKNOWN = 0, KIND_PC88 = 1, KIND_PC98 = 2, KIND_AC = 3, KIND_X68K = 4, KIND_SG1000 = 5, KIND_X1 = 6, KIND_MSX = 7, KIND_PCAT = 8, KIND_NEO = 9, KIND_F3 = 10, KIND_FM7 = 11 };

	virtual ~CHard() {}

	int hardKind;

	/* Z80 machines override; i286 (PC-98) returns NULL and uses NP2 FFI. */
	virtual Ay_Cpu* Cpu() { return NULL; }
	virtual uint8_t* Mem() = 0;
	virtual CChip* SoundChip() = 0;

	virtual uint8_t PortIn(uint16_t port) = 0;
	virtual void PortOut(uint16_t port, uint8_t data) = 0;

protected:
	CHard() : hardKind(KIND_UNKNOWN) {}
};
