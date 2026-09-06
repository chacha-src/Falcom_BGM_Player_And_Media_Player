#pragma once
#include "../cemu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* After FmMonShadowReset/SetSource/SetSampleRate: tag platform+chip for FM monitor. */
void CEmuFmMonBindFromGe(const CEmuGameEntry* ge);

#ifdef __cplusplus
}
#endif
