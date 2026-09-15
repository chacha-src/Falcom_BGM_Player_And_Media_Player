#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* FM-7 セッション: ハード＋ドライバ＋ zip パス */
struct CEmuFm7 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

/* zip を開きハード＋ドライバを生成して曲を起動する */
int CEmuFm7Open(CEmuFm7* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
/* ドライバ／ハードを破棄する */
void CEmuFm7Close(CEmuFm7* m);
/* ステレオ PCM を frames 分合成する */
int CEmuFm7Render(CEmuFm7* m, int16_t* stereo, int frames);
/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuFm7Seek(CEmuFm7* m, uint64_t sample);
