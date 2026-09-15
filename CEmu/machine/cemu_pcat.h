#pragma once
#include "cemu_types.h"

class CHard;
class CDriver;

/* PC/AT セッション: ハード＋ドライバ＋実サンプルレート */
struct CEmuPcat {
	CHard* hard;
	CDriver* driver;
	int ready;
	int sampleRate;
	wchar_t zipPath[CEMU_ZIP_PATH];
};

/* zip を開きハード＋ドライバを生成。FmMon は subtype で bind */
int CEmuPcatOpen(CEmuPcat* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate);
/* ドライバ／ハードを破棄する */
void CEmuPcatClose(CEmuPcat* m);
/* ステレオ PCM を合成し、FmMon を Flush */
int CEmuPcatRender(CEmuPcat* m, int16_t* stereo, int frames);
/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuPcatSeek(CEmuPcat* m, uint64_t sample);
