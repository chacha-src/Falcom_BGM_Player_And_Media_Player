#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* SG-1000 セッション: ハード＋ドライバ＋ zip パス */
struct CEmuSg1000 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

/* zip を開きハード＋ドライバを生成して曲を起動する */
int CEmuSg1000Open(CEmuSg1000* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
/* ドライバ／ハードを破棄する */
void CEmuSg1000Close(CEmuSg1000* m);
/* ステレオ PCM を frames 分合成する */
int CEmuSg1000Render(CEmuSg1000* m, int16_t* stereo, int frames);
/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuSg1000Seek(CEmuSg1000* m, uint64_t sample);
