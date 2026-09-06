/* CEmu standalone M37702 - adapted from MAME m37710cm.h (Belmont). */
#ifndef CEMU_M37702CM_H
#define CEMU_M37702CM_H

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "m37702core.h"

#ifndef INLINE
#if defined(_MSC_VER)
#define INLINE __inline static
#else
#define INLINE static inline
#endif
#endif

#define M37710_CALL_DEBUGGER(x)         ((void)0)
#define m37710_read_8(addr)             m37702_bus_read8(cpustate, (uint32_t)(addr))
#define m37710_write_8(addr,data)       m37702_bus_write8(cpustate, (uint32_t)(addr), (uint8_t)(data))
#define m37710_read_8_immediate(A)      m37710_read_8(A)
#define m37710_read_16(addr)            m37702_bus_read16(cpustate, (uint32_t)(addr))
#define m37710_write_16(addr,data)      m37702_bus_write16(cpustate, (uint32_t)(addr), (uint16_t)(data))
#define m37710_jumping(A)
#define m37710_branching(A)
#define m37710i_branching(A)
#define m37710i_jumping(A)

#undef uint
#define uint unsigned int
#undef uint8
#define uint8 unsigned char
#undef int8
#if UCHAR_MAX == 0xff
#define int8 char
#define MAKE_INT_8(A) (int8)((A)&0xff)
#else
#define int8 int
INLINE int MAKE_INT_8(int A) {return (A & 0x80) ? A | ~0xff : A & 0xff;}
#endif
#define MAKE_UINT_8(A) ((A)&0xff)
#define MAKE_UINT_16(A) ((A)&0xffff)
#define MAKE_UINT_24(A) ((A)&0xffffff)
#define BIT_0 0x01
#define BIT_1 0x02
#define BIT_2 0x04
#define BIT_3 0x08
#define BIT_4 0x10
#define BIT_5 0x20
#define BIT_6 0x40
#define BIT_7 0x80

#define CLEAR_LINE  0
#define ASSERT_LINE 1
#define HOLD_LINE   2
#define PULSE_LINE  3
#ifndef STATE_GENPCBASE
#define STATE_GENPCBASE (-1)
#endif

typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef int32_t INT32;

#ifndef logerror
#define logerror(...) ((void)0)
#endif

struct m37710i_cpu_struct
{
	uint a,b,ba,bb,x,y,xh,yh,s,pc,ppc,pb,db,d;
	uint flag_e,flag_m,flag_x,flag_n,flag_v,flag_d,flag_i,flag_z,flag_c;
	uint line_irq,ipl,ir,im,im2,im3,im4,irq_delay,irq_level;
	int ICount;
	uint source,destination;
	void *bus_ctx;
	M37702ReadFn bus_read;
	M37702WriteFn bus_write;
	const uint8_t *int_rom;
	unsigned int_rom_size;
	uint8_t m37710_regs[128];
	uint8_t iram[0x200]; /* M37702M2 internal RAM @ 0x80-0x27F */
	int timer_period[8];
	int timer_left[8];
	uint stopped;
	void (*const *opcodes)(struct m37710i_cpu_struct *cpustate);
	void (*const *opcodes42)(struct m37710i_cpu_struct *cpustate);
	void (*const *opcodes89)(struct m37710i_cpu_struct *cpustate);
	uint (*get_reg)(struct m37710i_cpu_struct *cpustate, int regnum);
	void (*set_reg)(struct m37710i_cpu_struct *cpustate, int regnum, uint val);
	void (*set_line)(struct m37710i_cpu_struct *cpustate, int line, int state);
	int  (*execute)(struct m37710i_cpu_struct *cpustate, int cycles);
	uint32_t irq_count;
};

typedef struct m37710i_cpu_struct m37710i_cpu_struct;

struct M37702Cpu { m37710i_cpu_struct s; };

uint8_t m37702_bus_read8(m37710i_cpu_struct *cpustate, uint32_t addr);
void m37702_bus_write8(m37710i_cpu_struct *cpustate, uint32_t addr, uint8_t data);
uint16_t m37702_bus_read16(m37710i_cpu_struct *cpustate, uint32_t addr);
void m37702_bus_write16(m37710i_cpu_struct *cpustate, uint32_t addr, uint16_t data);

extern uint m37710i_adc_tbl[];
extern uint m37710i_sbc_tbl[];
extern void (*const *const m37710i_opcodes[])(m37710i_cpu_struct *cpustate);
extern void (*const *const m37710i_opcodes2[])(m37710i_cpu_struct *cpustate);
extern void (*const *const m37710i_opcodes3[])(m37710i_cpu_struct *cpustate);
extern uint (*const m37710i_get_reg[])(m37710i_cpu_struct *cpustate, int regnum);
extern void (*const m37710i_set_reg[])(m37710i_cpu_struct *cpustate, int regnum, uint val);
extern void (*const m37710i_set_line[])(m37710i_cpu_struct *cpustate, int line, int state);
extern int (*const m37710i_execute[])(m37710i_cpu_struct *cpustate, int cycles);
extern const int m37710_irq_levels[];

#define REG_A           cpustate->a     /* Accumulator */
#define REG_B           cpustate->b     /* Accumulator hi byte */
#define REG_BA          cpustate->ba        /* Secondary Accumulator */
#define REG_BB          cpustate->bb        /* Secondary Accumulator hi byte */
#define REG_X           cpustate->x     /* Index X Register */
#define REG_Y           cpustate->y     /* Index Y Register */
#define REG_XH          cpustate->xh        /* X high byte */
#define REG_YH          cpustate->yh        /* Y high byte */
#define REG_S           cpustate->s     /* Stack Pointer */
#define REG_PC          cpustate->pc        /* Program Counter */
#define REG_PPC         cpustate->ppc       /* Previous Program Counter */
#define REG_PB          cpustate->pb        /* Program Bank */
#define REG_DB          cpustate->db        /* Data Bank */
#define REG_D           cpustate->d     /* Direct Register */
#define FLAG_M          cpustate->flag_m    /* Memory/Accumulator Select Flag */
#define FLAG_X          cpustate->flag_x    /* Index Select Flag */
#define FLAG_N          cpustate->flag_n    /* Negative Flag */
#define FLAG_V          cpustate->flag_v    /* Overflow Flag */
#define FLAG_D          cpustate->flag_d    /* Decimal Mode Flag */
#define FLAG_I          cpustate->flag_i    /* Interrupt Mask Flag */
#define FLAG_Z          cpustate->flag_z    /* Zero Flag (inverted) */
#define FLAG_C          cpustate->flag_c    /* Carry Flag */
#define LINE_IRQ        cpustate->line_irq  /* Status of the IRQ line */
#define REG_IR          cpustate->ir        /* Instruction Register */
#define REG_IM          cpustate->im        /* Immediate load value */
#define REG_IM2         cpustate->im2       /* Immediate load target */
#define REG_IM3         cpustate->im3       /* Immediate load target */
#define REG_IM4         cpustate->im4       /* Immediate load target */
#define INT_ACK         ((void*)0)   /* no IRQ acknowledge callback */
#define CLOCKS          cpustate->ICount        /* Clock cycles remaining */
#define IRQ_DELAY       cpustate->irq_delay /* Delay 1 instruction before checking IRQ */
#define CPU_STOPPED     cpustate->stopped   /* Stopped status of the CPU */

#define FTABLE_GET_REG  cpustate->get_reg
#define FTABLE_SET_REG  cpustate->set_reg
#define FTABLE_SET_LINE cpustate->set_line

#define SRC         cpustate->source        /* Source Operand */
#define DST         cpustate->destination   /* Destination Operand */

#define STOP_LEVEL_WAI  1
#define STOP_LEVEL_STOP 2

#define EXECUTION_MODE_M0X0 0
#define EXECUTION_MODE_M0X1 1
#define EXECUTION_MODE_M1X0 2
#define EXECUTION_MODE_M1X1 3

INLINE void m37710i_set_execution_mode(m37710i_cpu_struct *cpustate, uint mode)
{
	cpustate->opcodes = m37710i_opcodes[mode];
	cpustate->opcodes42 = m37710i_opcodes2[mode];
	cpustate->opcodes89 = m37710i_opcodes3[mode];
	FTABLE_GET_REG = m37710i_get_reg[mode];
	FTABLE_SET_REG = m37710i_set_reg[mode];
	FTABLE_SET_LINE = m37710i_set_line[mode];
	cpustate->execute = m37710i_execute[mode];
}

/* ======================================================================== */
/* ================================= CLOCK ================================ */
/* ======================================================================== */

#define CLK_OP          1
#define CLK_R8          1
#define CLK_R16         2
#define CLK_R24         3
#define CLK_W8          1
#define CLK_W16         2
#define CLK_W24         3
#define CLK_RMW8        3
#define CLK_RMW16       5

#define CLK_IMPLIED     1
#define CLK_IMPLIED     1
#define CLK_RELATIVE_8  1
#define CLK_RELATIVE_16 2
#define CLK_IMM         0
#define CLK_AI          4
#define CLK_AXI         4
#define CLK_A           2
#define CLK_AL          3
#define CLK_ALX         3
#define CLK_AX          2
#define CLK_AY          2
#define CLK_D           1
#define CLK_DI          3
#define CLK_DIY         3
#define CLK_DLI         4
#define CLK_DLIY        4
#define CLK_DX          2
#define CLK_DXI         4
#define CLK_DY          2
#define CLK_S           2
#define CLK_SIY         5

/* AX and AY addressing modes take 1 extra cycle when writing */
#define CLK_W_IMM       0
#define CLK_W_AI        4
#define CLK_W_AXI       4
#define CLK_W_A         2
#define CLK_W_AL        3
#define CLK_W_ALX       3
#define CLK_W_AX        3
#define CLK_W_AY        3
#define CLK_W_D         1
#define CLK_W_DI        3
#define CLK_W_DIY       3
#define CLK_W_DLI       4
#define CLK_W_DLIY      4
#define CLK_W_DX        2
#define CLK_W_DXI       4
#define CLK_W_DY        2
#define CLK_W_S         2
#define CLK_W_SIY       5

#define CLK(A)          CLOCKS -= (A)
#define USE_ALL_CLKS()  CLOCKS = 0


/* ======================================================================== */
/* ============================ STATUS REGISTER =========================== */
/* ======================================================================== */

/* Flag positions in Processor Status Register */
/* common */
#define FLAGPOS_N       BIT_7   /* Negative         */
#define FLAGPOS_V       BIT_6   /* Overflow         */
#define FLAGPOS_D       BIT_3   /* Decimal Mode     */
#define FLAGPOS_I       BIT_2   /* Interrupt Mask   */
#define FLAGPOS_Z       BIT_1   /* Zero             */
#define FLAGPOS_C       BIT_0   /* Carry            */
/* emulation */
#define FLAGPOS_R       BIT_5   /* Reserved         */
#define FLAGPOS_B       BIT_4   /* BRK Instruction  */
/* native */
#define FLAGPOS_M       BIT_5   /* Mem/Reg Select   */
#define FLAGPOS_X       BIT_4   /* Index Select     */

#define EFLAG_SET       1
#define EFLAG_CLEAR     0
#define MFLAG_SET       FLAGPOS_M
#define MFLAG_CLEAR     0
#define XFLAG_SET       FLAGPOS_X
#define XFLAG_CLEAR     0
#define NFLAG_SET       0x80
#define NFLAG_CLEAR     0
#define VFLAG_SET       0x80
#define VFLAG_CLEAR     0
#define DFLAG_SET       FLAGPOS_D
#define DFLAG_CLEAR     0
#define IFLAG_SET       FLAGPOS_I
#define IFLAG_CLEAR     0
#define BFLAG_SET       FLAGPOS_B
#define BFLAG_CLEAR     0
#define ZFLAG_SET       0
#define ZFLAG_CLEAR     1
#define CFLAG_SET       0x100
#define CFLAG_CLEAR     0

/* Codition code tests */
#define COND_CC()       (!(FLAG_C&0x100))   /* Carry Clear */
#define COND_CS()       (FLAG_C&0x100)      /* Carry Set */
#define COND_EQ()       (!FLAG_Z)           /* Equal */
#define COND_NE()       FLAG_Z              /* Not Equal */
#define COND_MI()       (FLAG_N&0x80)       /* Minus */
#define COND_PL()       (!(FLAG_N&0x80))    /* Plus */
#define COND_VC()       (!(FLAG_V&0x80))    /* Overflow Clear */
#define COND_VS()       (FLAG_V&0x80)       /* Overflow Set */

/* Set Overflow flag in math operations */
#define VFLAG_ADD_8(S, D, R)    ((S^R) & (D^R))
#define VFLAG_ADD_16(S, D, R)   (((S^R) & (D^R))>>8)
#define VFLAG_SUB_8(S, D, R)    ((S^D) & (R^D))
#define VFLAG_SUB_16(S, D, R)   (((S^D) & (R^D))>>8)

#define CFLAG_8(A)      (A)
#define CFLAG_16(A)     ((A)>>8)
#define NFLAG_8(A)      (A)
#define NFLAG_16(A)     ((A)>>8)

#define CFLAG_AS_1()    ((FLAG_C>>8)&1)

/* update IRQ state (internal use only) */

void m37710i_update_irqs(m37710i_cpu_struct *cpustate);

#endif /* CEMU_M37702CM_H */
