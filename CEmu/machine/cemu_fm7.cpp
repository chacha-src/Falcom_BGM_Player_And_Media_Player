#include "StdAfx.h"
#include "cemu_fm7.h"
#include "../cemu_zipfs.h"
#include "../driver/cemu_driver.h"
#include "../fmmon/fmmon_shadow.h"
#include "../fmmon/cemu_fmmon_bind.h"
#include <string.h>

int CEmuFm7Open(CEmuFm7* m, const CEmuGameEntry* ge, const wchar_t* zipPath, unsigned titleCode, int sampleRate)
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
		CEmuFm7Close(m);
		return 0;
	}
	if (!m->driver->Open(m->hard, ge, &fs, titleCode)) {
		CEmuZipFsClose(&fs);
		CEmuFm7Close(m);
		return 0;
	}
	CEmuZipFsClose(&fs);

	m->ready = 1;
	FmMonShadowReset();
	FmMonShadowSetSource(zipPath);
	FmMonShadowSetSampleRate((uint32_t)(sampleRate > 0 ? sampleRate : 44100));
	CEmuFmMonBindFromGe(ge);
	return 1;
}

void CEmuFm7Close(CEmuFm7* m)
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

int CEmuFm7Render(CEmuFm7* m, int16_t* stereo, int frames)
{
	if (!m || !m->ready || !m->driver || !stereo || frames <= 0) return 0;
	return m->driver->Render(stereo, frames);
}

int CEmuFm7Seek(CEmuFm7* m, uint64_t sample)
{
	if (!m || !m->driver) return 0;
	return m->driver->Seek(sample);
}
