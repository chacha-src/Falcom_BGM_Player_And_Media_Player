#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* PC-98 セッション: ハード＋ドライバ＋ zip パス */
struct CEmuPc98 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

/* zip を開きハード＋ドライバを生成して曲を起動する */
int CEmuPc98Open(CEmuPc98* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
/* ドライバ／ハードを破棄する */
void CEmuPc98Close(CEmuPc98* m);
/* ステレオ PCM を frames 分合成する */
int CEmuPc98Render(CEmuPc98* m, int16_t* stereo, int frames);
/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuPc98Seek(CEmuPc98* m, uint64_t sample);
