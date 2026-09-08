/* eng_ssf/scsp.c includes cpuintrf.h for the MAME CPU interface, but the chip
   core itself only raises IRQs through the interface callbacks supplied in
   SCSPinterface. CEmu drives the sound 68000 itself, so nothing is needed. */
#ifndef _CEMU_SCSP_CPUINTRF_H_
#define _CEMU_SCSP_CPUINTRF_H_
#endif
