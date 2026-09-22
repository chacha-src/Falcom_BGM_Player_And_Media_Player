#pragma once
#include "../cemu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FmMonShadowReset/SetSource/SetSampleRate のあと: FM モニタへ platform+chip を付ける */
void CEmuFmMonBindFromGe(const CEmuGameEntry* ge);
/* ドライバ Open の前に呼ぶ。初期化レジスタ書きを残す。BindFromGe は Open 後のまま */
void CEmuFmMonBeginOpen(const CEmuGameEntry* ge, const wchar_t* zipPath, int sampleRate);

#ifdef __cplusplus
}
#endif
