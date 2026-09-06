/* CEmu-adapted Musashi 3.3 config (from aosdk eng_ssf). */
#ifndef M68KCONF__HEADER
#define M68KCONF__HEADER

#define OPT_OFF             0
#define OPT_ON              1
#define OPT_SPECIFY_HANDLER 2

#ifndef M68K_COMPILE_FOR_MAME
#define M68K_COMPILE_FOR_MAME      OPT_OFF
#endif

#if M68K_COMPILE_FOR_MAME == OPT_ON
#include "m68kmame.h"
#else

#define M68K_EMULATE_010            OPT_OFF
#define M68K_EMULATE_EC020          OPT_OFF
#define M68K_EMULATE_020            OPT_OFF

#define M68K_SEPARATE_READS         OPT_OFF
#define M68K_SIMULATE_PD_WRITES     OPT_OFF

#define M68K_EMULATE_INT_ACK        OPT_ON
#define M68K_INT_ACK_CALLBACK(A)

#define M68K_EMULATE_BKPT_ACK       OPT_OFF
#define M68K_BKPT_ACK_CALLBACK()

#define M68K_EMULATE_TRACE          OPT_OFF

#define M68K_EMULATE_RESET          OPT_OFF
#define M68K_RESET_CALLBACK()

#define M68K_EMULATE_FC             OPT_OFF
#define M68K_SET_FC_CALLBACK(A)

#define M68K_MONITOR_PC             OPT_OFF
#define M68K_SET_PC_CALLBACK(A)

#define M68K_INSTRUCTION_HOOK       OPT_OFF
#define M68K_INSTRUCTION_CALLBACK()

#define M68K_EMULATE_PREFETCH       OPT_OFF
#define M68K_EMULATE_ADDRESS_ERROR  OPT_OFF

#define M68K_LOG_ENABLE             OPT_OFF
#define M68K_LOG_1010_1111          OPT_OFF

#define M68K_USE_64_BIT             OPT_OFF

#ifndef INLINE
#ifdef _MSC_VER
#define INLINE static __inline
#else
#define INLINE static inline
#endif
#endif

#endif /* M68K_COMPILE_FOR_MAME */

#endif /* M68KCONF__HEADER */
