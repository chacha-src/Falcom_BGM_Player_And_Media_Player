#pragma once
#include "../cemu_types.h"
#include "../cemu_zipfs.h"

class CHard;

/* hoot ドライバ層 — ブート・IRQ・再生トリガ */
class CDriver {
public:
	virtual ~CDriver() {}

	virtual int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) = 0;
	virtual void Close() = 0;
	virtual int Render(int16_t* stereo, int frames) = 0;
	virtual int Seek(uint64_t sample) = 0;
	/* Same-zip SE while BGM plays: poke the live mailbox / command. 0 = unsupported. */
	virtual int OverlayTitle(unsigned titleCode) { (void)titleCode; return 0; }
};

CHard* CEmuHardCreate(const CEmuGameEntry* ge, int sampleRate);
void CEmuHardDestroy(CHard* hw);

CDriver* CEmuDriverCreate(const CEmuGameEntry* ge);
void CEmuDriverDestroy(CDriver* drv);
