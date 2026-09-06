/* Minimal M6800/M6803 header for CEmu (Fuzix-derived core). */
#ifndef CEMU_M6803_H
#define CEMU_M6803_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	CPU_6800 = 0,
	CPU_6803 = 1,
	CPU_6303 = 2,
	CPU_6802 = 3,
	CPU_68HC11 = 4
};

enum {
	INTIO_NONE = 0,
	INTIO_6802 = 1,
	INTIO_6803 = 2,
	INTIO_HC11 = 3
};

enum {
	P_C = 0x01,
	P_V = 0x02,
	P_Z = 0x04,
	P_N = 0x08,
	P_I = 0x10,
	P_H = 0x20,
	P_X = 0x40,
	P_S = 0x80
};

enum {
	IRQ_NMI = 0x80000000u,
	IRQ_IRQ1 = 0x00000001u,
	IRQ_ICF = 0x00000002u,
	IRQ_OCF = 0x00000004u,
	IRQ_TOF = 0x00000008u,
	IRQ_SCI = 0x00000010u
};

enum {
	RAMCR_RAME = 0x40,
	TCSR_TOF = 0x20,
	TCSR_OCF = 0x40,
	TCSR_ICF = 0x80,
	TCSR_EICI = 0x10,
	TCSR_EOCI = 0x08,
	TCSR_ETOI = 0x04,
	TCSR_OLVL = 0x01,
	TRCSR_TDRE = 0x20,
	TRCSR_RDRF = 0x80,
	TRCSR_ORFE = 0x40,
	TRCSR_TE = 0x02,
	TRCSR_RE = 0x01,
	TRCSR_TIE = 0x10,
	TRCSR_RIE = 0x08
};

struct m6800;

typedef uint8_t (*m6800_read_fn)(struct m6800* cpu, uint16_t addr);
typedef void (*m6800_write_fn)(struct m6800* cpu, uint16_t addr, uint8_t val);
typedef uint8_t (*m6800_port_in_fn)(struct m6800* cpu, int port);
typedef void (*m6800_port_out_fn)(struct m6800* cpu, int port, uint8_t val);

struct m6800 {
	uint16_t pc;
	uint16_t x, y, s;
	uint8_t a, b, p;
	uint8_t type;
	uint8_t intio;
	uint8_t mode;
	uint8_t wait;
	uint8_t debug;
	uint8_t iram[256];
	uint8_t iram_base;
	uint8_t ramcr;
	uint8_t p1ddr, p2ddr;
	uint8_t p1dr, p2dr;
	uint8_t tcsr;
	uint8_t rmcr, trcsr, rdr;
	uint16_t counter, ocr;
	uint8_t oc_hold;
	uint32_t irq;
	void* ctx;
	m6800_read_fn read;
	m6800_write_fn write;
	m6800_port_in_fn port_in;
	m6800_port_out_fn port_out;
};

/* Bus hooks used by the core (override via function pointers above). */
uint8_t m6800_read(struct m6800* cpu, uint16_t addr);
void m6800_write(struct m6800* cpu, uint16_t addr, uint8_t val);
uint8_t m6800_port_input(struct m6800* cpu, int port);
void m6800_port_output(struct m6800* cpu, int port);
void m6800_sci_change(struct m6800* cpu);
void m6800_sci_ints(struct m6800* cpu);
void m6800_counter_ints(struct m6800* cpu);
void m6800_tx_byte(struct m6800* cpu, uint8_t val);

uint8_t m6800_do_read(struct m6800* cpu, uint16_t addr);
void m6800_do_write(struct m6800* cpu, uint16_t addr, uint8_t val);
uint8_t m6800_read_io(struct m6800* cpu, uint8_t addr);
void m6800_write_io(struct m6800* cpu, uint8_t addr, uint8_t val);

void m6800_reset(struct m6800* cpu, int type, int io, int mode);
int m6800_execute(struct m6800* cpu);
void m6800_raise_interrupt(struct m6800* cpu, int irq);
void m6800_clear_interrupt(struct m6800* cpu, int irq);

#ifdef __cplusplus
}
#endif

#endif
