#include "StdAfx.h"
#include "cemu_pcat.h"
#include "../cemu_zipfs.h"
#include "../driver/cemu_driver.h"
#include "../fmmon/fmmon_shadow.h"
#include "../fmmon/cemu_fmmon_bind.h"
#include "cemu_hard_pcat.h"
#include <string.h>

/* subtype に応じ FmMon を MIDI / BEEP / CMS / OPL へ bind する */
static void CEmuPcatBindFmMon(CHardPcat* hw, const CEmuGameEntry* ge)
{
	if (!hw || !ge) return;
	CEmuFmMonBindFromGe(ge);
	if (hw->modeMidi_) {
		/* UART は MIDI モニタ（GM/GS/LA）。FM 鍵盤ではない */
		FmMonShadowSetIdentity("PC/AT", "MPU-401 MIDI");
	} 		else if (hw->modeBeep_) {
			FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_MIDI);
			if (hw->modePs1_)
				FmMonShadowSetIdentity("PC/AT", "PS/1 Audio");
			else
				FmMonShadowSetIdentity("PC/AT", "BEEP");
		} else if (hw->modeCms_) {
		/* Game Blaster: SAA×2 鍵盤。OPL へ AdLib プローブが残ることがある */
		FmMonShadowEnterKeysOnly(SASAMI_FMMON_KEYS_MIDI);
		FmMonShadowSetIdentity("PC/AT", "GameBlaster SAAx2");
	} else {
		/* AdLib / SB の FM — YM3812（旋律 9ch） */
		FmMonShadowSetOplMode(1);
		FmMonShadowSetOpnaLayout(-1);
		const char* sub = ge->subtype[0] ? ge->subtype : "";
		if (_stricmp(sub, "soundblaster16") == 0 || _stricmp(sub, "sb16") == 0
			|| _stricmp(sub, "soundblaster") == 0 || _stricmp(sub, "sbpro") == 0
			|| _stricmp(sub, "sb") == 0)
			FmMonShadowSetIdentity("PC/AT", "Sound Blaster OPL2x9");
		else
			FmMonShadowSetIdentity("PC/AT", "AdLib OPL2x9");
	}
	FmMonShadowFlush(1);
}

/* zip を開き PC/AT ハード＋ドライバを生成。FmMon は subtype で bind。 */
int CEmuPcatOpen(CEmuPcat* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate)
{
	if (!m || !ge || !zipPath) return 0;
	memset(m, 0, sizeof(*m));
	wcsncpy_s(m->zipPath, zipPath, _TRUNCATE);

	/* silp/AIL + OPL/SAA は 96–192 kHz を実時間維持できない。約 30s 超で
	   ホストがアンダーランし最終鍵が残る。PC/AT は 48 kHz 上限。 */
	int rate = sampleRate > 0 ? sampleRate : 44100;
	if (rate > 48000) rate = 48000;

	m->hard = CEmuHardCreate(ge, rate);
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
		CEmuPcatClose(m);
		return 0;
	}
	CEmuFmMonBeginOpen(ge, zipPath, rate);
	if (!m->driver->Open(m->hard, ge, &fs, titleCode)) {
		CEmuZipFsClose(&fs);
		CEmuPcatClose(m);
		return 0;
	}
	CEmuZipFsClose(&fs);

	m->ready = 1;
	m->sampleRate = rate;
	CEmuPcatBindFmMon((CHardPcat*)m->hard, ge);
	return 1;
}

/* ドライバ／ハードを破棄する */
void CEmuPcatClose(CEmuPcat* m)
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

/* ステレオ PCM を合成し、FmMon を Flush */
int CEmuPcatRender(CEmuPcat* m, int16_t* stereo, int frames)
{
	if (!m || !m->ready || !m->driver || !stereo || frames <= 0) return 0;
	const int got = m->driver->Render(stereo, frames);
	if (got > 0 && m->hard && m->hard->hardKind == CHard::KIND_PCAT) {
		CHardPcat* hw = (CHardPcat*)m->hard;
		/* Beep / CMS / MIDI は鍵盤(+補助)。AdLib/SB は OPL 全 Flush */
		if (hw->modeMidi_ || hw->modeBeep_ || hw->modeCms_)
			FmMonShadowFlushKeysOnly(0);
		else
			FmMonShadowFlush(0);
	}
	return got;
}

/* 再生位置を sample へ移動（未対応なら 0） */
int CEmuPcatSeek(CEmuPcat* m, uint64_t sample)
{
	if (!m || !m->driver) return 0;
	return m->driver->Seek(sample);
}
