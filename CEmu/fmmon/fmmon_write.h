#pragma once
#include "sasami_fmmon.h"

#ifdef __cplusplus
extern "C" {
#endif

void FmMonInitDump(SasamiFmMonDump* d);
void FmMonWriteDump(const SasamiFmMonDump* d);
/* 曲切替時にリングを空にする。残すと UI が前曲（＝別チップ）の
   スロットまで drain して type/レジスタ/鍵盤が食い違う。 */
void FmMonWriteRingReset(void);

#ifdef __cplusplus
}
#endif
