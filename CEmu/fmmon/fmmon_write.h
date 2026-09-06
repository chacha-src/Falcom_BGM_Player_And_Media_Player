#pragma once
#include "sasami_fmmon.h"

#ifdef __cplusplus
extern "C" {
#endif

void FmMonInitDump(SasamiFmMonDump* d);
void FmMonWriteDump(const SasamiFmMonDump* d);

#ifdef __cplusplus
}
#endif
