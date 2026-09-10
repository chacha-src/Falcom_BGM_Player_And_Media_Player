#pragma once
#include "../cemu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* After FmMonShadowReset/SetSource/SetSampleRate: tag platform+chip for FM monitor. */
void CEmuFmMonBindFromGe(const CEmuGameEntry* ge);
/* Call BEFORE driver Open so init register writes are kept. BindFromGe stays after Open. */
void CEmuFmMonBeginOpen(const CEmuGameEntry* ge, const wchar_t* zipPath, int sampleRate);

#ifdef __cplusplus
}
#endif
