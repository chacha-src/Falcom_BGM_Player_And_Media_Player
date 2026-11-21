#pragma once
#include <time.h>
/* zpaq 書庫。展開は exe に入れた libzpaq。外部の zpaq.exe は使わない。 */

int ZpaqPathIsW(const wchar_t* path);

/* 書庫を destDir へすべて出す。成功で 0。 */
int ZpaqExtractToDirW(const wchar_t* archive, const wchar_t* destDir);

/* 末尾名が leaf のファイルを destFile へ出す。成功で 1。 */
int ZpaqExtractLeafW(const wchar_t* archive, const wchar_t* leaf, const wchar_t* destFile,
	unsigned long long* written, time_t* mtime);
