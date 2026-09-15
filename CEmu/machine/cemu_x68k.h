#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* X68000 セッション: ハード＋ドライバ＋ zip パス */
struct CEmuX68k {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

/* zip を開きハード＋ドライバを生成して曲を起動する */
int CEmuX68kOpen(CEmuX68k* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
/* ドライバ／ハードを破棄する */
void CEmuX68kClose(CEmuX68k* m);
/* ステレオ PCM を frames 分合成する */
int CEmuX68kRender(CEmuX68k* m, int16_t* stereo, int frames);
/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuX68kSeek(CEmuX68k* m, uint64_t sample);
