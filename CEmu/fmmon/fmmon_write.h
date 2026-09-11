#pragma once
#include "sasami_fmmon.h"

#ifdef __cplusplus
extern "C" {
#endif

void FmMonInitDump(SasamiFmMonDump* d);
void FmMonWriteDump(const SasamiFmMonDump* d);
/* CEmu 専用 %TEMP%\ogg_cemu\ 。KPI/SASAMI の ogg_kbsasami とは別ファイル。 */
void FmMonWriteRingReset(void);

#ifdef __cplusplus
}
#endif
