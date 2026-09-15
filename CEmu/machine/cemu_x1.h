#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* X1 セッション: ハード＋ドライバ＋ zip パス */
struct CEmuX1 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

/* zip を開きハード＋ドライバを生成して曲を起動する */
int CEmuX1Open(CEmuX1* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
/* ドライバ／ハードを破棄する */
void CEmuX1Close(CEmuX1* m);
/* ステレオ PCM を frames 分合成する */
int CEmuX1Render(CEmuX1* m, int16_t* stereo, int frames);
/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuX1Seek(CEmuX1* m, uint64_t sample);
