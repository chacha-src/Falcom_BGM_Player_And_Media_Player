/* np2ffi.h -- C accessors for vendored NP2 i286c (see np2ffi.c) */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	NP2_R_AX = 0, NP2_R_BX, NP2_R_CX, NP2_R_DX, NP2_R_SI, NP2_R_DI, NP2_R_BP, NP2_R_SP,
	NP2_R_CS, NP2_R_DS, NP2_R_ES, NP2_R_SS, NP2_R_IP, NP2_R_FLAGS
};

void np2_init(void);
void np2_reset(void);
void np2_setextsize(uint32_t sz);
void np2_set_v30(int on);
void np2_set_adrsmask(uint32_t m);
void np2_set_cs_ip(uint16_t cs, uint16_t ip);
void np2_set_ss_sp(uint16_t ss, uint16_t sp);
uint16_t np2_reg_get(int idx);
void np2_reg_set(int idx, uint16_t v);
uint32_t np2_pc_phys(void);
void np2_interrupt(uint8_t vect);
int32_t np2_step(void);
uint8_t* np2_mem(void);

/* From glue/np2io.c */
extern void (*hootrip_out8)(unsigned port, unsigned char val);
extern unsigned char (*hootrip_inp8)(unsigned port);

#ifdef __cplusplus
}
#endif
