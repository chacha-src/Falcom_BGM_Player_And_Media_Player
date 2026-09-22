#pragma once
#include "sasami_fmmon.h"

#ifdef __cplusplus
extern "C" {
#endif

/* dump をゼロ初期化 */
void FmMonInitDump(SasamiFmMonDump* d);
/* ライブリングへ 1 フレーム書く（モニタ UI が読む） */
void FmMonWriteDump(const SasamiFmMonDump* d);
/* CEmu 専用 %TEMP%\ogg_cemu\ 。KPI/SASAMI の ogg_kbsasami とは別ファイル。 */
void FmMonWriteRingReset(void);

#ifdef __cplusplus
}
#endif
