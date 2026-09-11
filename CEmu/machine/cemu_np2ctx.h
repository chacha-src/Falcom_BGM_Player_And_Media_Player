#pragma once
#include <stdint.h>

/* NP2 i286c + 2 MiB RAM are process-global. Each PC-98/PC-AT hard keeps its
   own RAM and CPU snapshot; Bind swaps the live core so two sessions can
   run during a crossfade. Callers must hold CEmuNp2Lock around any np2_step
   / np2_init / memory use (the lock is recursive). */

enum { CEMU_NP2_MEM_SIZE = 0x200000 };
enum { CEMU_NP2_CPU_SIZE = 1024 };

void CEmuNp2Lock(void);
void CEmuNp2Unlock(void);
/* Switch the live NP2 core to this owner. haveCpu=0: bind RAM only (Init). */
void CEmuNp2Bind(void* owner, uint8_t* ram, void* cpu, int haveCpu);
void CEmuNp2Unbind(void* owner);
int CEmuNp2IsOwner(const void* owner);
int CEmuNp2CpuBytes(void);

struct CEmuNp2Guard {
	CEmuNp2Guard() { CEmuNp2Lock(); }
	~CEmuNp2Guard() { CEmuNp2Unlock(); }
	CEmuNp2Guard(const CEmuNp2Guard&) = delete;
	CEmuNp2Guard& operator=(const CEmuNp2Guard&) = delete;
};
