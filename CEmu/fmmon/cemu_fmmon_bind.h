#pragma once
#include "../cemu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* FmMonShadowReset/SetSource/SetSampleRate のあと: FM モニタへ platform+chip を付ける */
void CEmuFmMonBindFromGe(const CEmuGameEntry* ge);
/* ドライバ Open の前に呼ぶ。初期化レジスタ書きを残す。BindFromGe は Open 後のまま */
/* Reset 直後に BindFromGe する。Open settle 中の dump が既定 OPNA のまま残らない。 */
void CEmuFmMonBeginOpen(const CEmuGameEntry* ge, const wchar_t* zipPath, int sampleRate);

#ifdef __cplusplus
}
#endif
