#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* PC-88 セッション: ハード＋ドライバ＋ zip パス */
struct CEmuPc88 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

/* zip を開きハード＋ドライバを生成して曲を起動する */
int CEmuPc88Open(CEmuPc88* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
/* ドライバ／ハードを破棄する */
void CEmuPc88Close(CEmuPc88* m);
/* ステレオ PCM を frames 分合成する */
int CEmuPc88Render(CEmuPc88* m, int16_t* stereo, int frames);
/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuPc88Seek(CEmuPc88* m, uint64_t sample);
