#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* Taito F3 セッション: ハード＋ドライバ＋ zip パス */
struct CEmuF3 {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

/* zip を開きハード＋ドライバを生成して曲を起動する */
int CEmuF3Open(CEmuF3* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
/* ドライバ／ハードを破棄する */
void CEmuF3Close(CEmuF3* m);
/* ステレオ PCM を frames 分合成する */
int CEmuF3Render(CEmuF3* m, int16_t* stereo, int frames);
/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuF3Seek(CEmuF3* m, uint64_t sample);
