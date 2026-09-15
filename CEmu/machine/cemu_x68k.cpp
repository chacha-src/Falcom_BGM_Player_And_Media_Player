#include "StdAfx.h"
#include "cemu_x68k.h"
#include "cemu_hard.h"
#include "../cemu_zipfs.h"
#include "../driver/cemu_driver.h"
#include "../fmmon/fmmon_shadow.h"
#include "../fmmon/cemu_fmmon_bind.h"
#include <string.h>

/* X68000: zip を開き 68000 ハード＋ドライバを生成。FmMon は Open 前後で bind。 */
int CEmuX68kOpen(CEmuX68k* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate)
{
	if (!m || !ge || !zipPath) return 0;
	memset(m, 0, sizeof(*m));
	wcsncpy_s(m->zipPath, zipPath, _TRUNCATE);

	m->hard = CEmuHardCreate(ge, sampleRate);
	/* 生成ハードが想定機種でない場合は破棄 */
	if (!m->hard || m->hard->hardKind != CHard::KIND_X68K) {
		if (m->hard) {
			CEmuHardDestroy(m->hard);
			m->hard = NULL;
		}
		return 0;
	}
	m->driver = CEmuDriverCreate(ge);
	if (!m->driver) {
		CEmuHardDestroy(m->hard);
		m->hard = NULL;
		return 0;
	}

	CEmuZipFs fs;
	memset(&fs, 0, sizeof(fs));
	if (!CEmuZipFsOpen(&fs, zipPath)) {
		CEmuX68kClose(m);
		return 0;
	}
	/* FmMon ダンプ開始 */
	CEmuFmMonBeginOpen(ge, zipPath, sampleRate);
	if (!m->driver->Open(m->hard, ge, &fs, titleCode)) {
		CEmuZipFsClose(&fs);
		CEmuX68kClose(m);
		return 0;
	}
	CEmuZipFsClose(&fs);

	m->ready = 1;
	/* カタログ subtype から FmMon チップ配置を決める */
	CEmuFmMonBindFromGe(ge);
	return 1;
}

/* ドライバ／ハードを破棄する */
void CEmuX68kClose(CEmuX68k* m)
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
int CEmuX68kRender(CEmuX68k* m, int16_t* stereo, int frames)
{
	if (!m || !m->ready || !m->driver || !stereo || frames <= 0) return 0;
	return m->driver->Render(stereo, frames);
}

/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuX68kSeek(CEmuX68k* m, uint64_t sample)
{
	if (!m || !m->driver) return 0;
	return m->driver->Seek(sample);
}
