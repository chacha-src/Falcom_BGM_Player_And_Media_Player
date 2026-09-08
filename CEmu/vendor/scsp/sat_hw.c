/* Backing store for the vendored SCSP core.

   eng_ssf keeps the 512KB sound RAM as a Saturn-global array and hands the
   SCSP a pointer to it (SCSP->SCSPRAM = &sat_ram[0]). Sega Model 2A/3 wire
   the same 512KB DRAM to each SCSP, so CEmu keeps that layout and lets the
   chip wrapper stage wave data into it. */
#include "ao.h"
#include "sat_hw.h"
#include <string.h>

uint8 sat_ram[512 * 1024];

/* Per-slot mute mask the AO player exposes to the UI. CEmu never mutes. */
UINT32 dwChannelMute = 0;

void sat_hw_init(void)
{
	memset(sat_ram, 0, sizeof(sat_ram));
}

void CEmuScspLogError(const char* fmt, ...)
{
	/* The core only logs the unimplemented main-CPU interrupt path. */
	(void)fmt;
}
