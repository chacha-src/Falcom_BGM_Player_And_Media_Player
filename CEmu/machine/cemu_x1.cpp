#include "StdAfx.h"
#include "cemu_x1.h"
#include "../cemu_zipfs.h"
#include "../driver/cemu_driver.h"
#include "../fmmon/fmmon_shadow.h"
#include "../fmmon/cemu_fmmon_bind.h"
#include <string.h>

/* X1: zip を開きハード＋ドライバを生成して曲を起動。FmMon は Open 前後で bind。 */
int CEmuX1Open(CEmuX1* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate)
{
	if (!m || !ge || !zipPath) return 0;
	memset(m, 0, sizeof(*m));
	wcsncpy_s(m->zipPath, zipPath, _TRUNCATE);

	m->hard = CEmuHardCreate(ge, sampleRate);
	if (!m->hard) return 0;
	m->driver = CEmuDriverCreate(ge);
	if (!m->driver) {
		CEmuHardDestroy(m->hard);
		m->hard = NULL;
		return 0;
	}

	CEmuZipFs fs;
	memset(&fs, 0, sizeof(fs));
	if (!CEmuZipFsOpen(&fs, zipPath)) {
		CEmuX1Close(m);
		return 0;
	}
	/* FmMon ダンプ開始（Open 失敗時も後で Close が掃除する） */
	CEmuFmMonBeginOpen(ge, zipPath, sampleRate);
	if (!m->driver->Open(m->hard, ge, &fs, titleCode)) {
		CEmuZipFsClose(&fs);
		CEmuX1Close(m);
		return 0;
	}
	CEmuZipFsClose(&fs);

	m->ready = 1;
	/* カタログ subtype から FmMon チップ配置を決める */
	CEmuFmMonBindFromGe(ge);
	return 1;
}

/* ドライバ／ハードを破棄する */
void CEmuX1Close(CEmuX1* m)
{
	if (!m) return;
	if (m->driver) {
		m->driver->Close();
		CEmuDriverDestroy(m->driver);
		m->driver = NULL;
	}
	if (m->hard) {
		CEmuHardDestroy(m->hard);
		m->hard = NULL;
	}
	memset(m, 0, sizeof(*m));
}

/* ステレオ PCM を frames 分合成する */
int CEmuX1Render(CEmuX1* m, int16_t* stereo, int frames)
{
	if (!m || !m->ready || !m->driver || !stereo || frames <= 0) return 0;
	return m->driver->Render(stereo, frames);
}

/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuX1Seek(CEmuX1* m, uint64_t sample)
{
	if (!m || !m->driver) return 0;
	return m->driver->Seek(sample);
}
