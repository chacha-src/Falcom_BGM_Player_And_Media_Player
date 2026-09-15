#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* Arcade セッション: ハード＋ドライバ＋ zip パス */
struct CEmuAc {
	CHard* hard;
	CDriver* driver;
	int ready;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

/* zip を開きハード＋ドライバを生成して曲を起動する */
int CEmuAcOpen(CEmuAc* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
/* ドライバ／ハードを破棄する */
void CEmuAcClose(CEmuAc* m);
/* ステレオ PCM を frames 分合成する */
int CEmuAcRender(CEmuAc* m, int16_t* stereo, int frames);
/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuAcSeek(CEmuAc* m, uint64_t sample);
