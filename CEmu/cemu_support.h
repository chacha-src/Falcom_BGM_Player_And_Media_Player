#pragma once

/* 再生確認済みアーカイブ一覧。

   カタログは実際に鳴る zip より遥かに多い。cemu_support.cpp の生成テーブルで
   「このアーカイブは本当に鳴るか」を答え、未対応を UI が隠して無音再生を避ける。

   archive は CEmuGameEntry::archive（zip stem。任意で "stem,companion"）。
   照合は大文字小文字を無視する。 */

#ifdef __cplusplus
extern "C" {
#endif

int CEmuArchiveIsSupported(const char* archive);
int CEmuSupportedArchiveCount(void);

#ifdef __cplusplus
}
#endif
