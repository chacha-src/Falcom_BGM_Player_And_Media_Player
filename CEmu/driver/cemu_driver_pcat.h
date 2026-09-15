#pragma once
#include "cemu_driver.h"

class CHardPcat;

/* PC/AT: NP2 i286＋DOS＋OPL/CMS/BEEP/MIDI 音源 */
class CDriverPcat : public CDriver {
public:
	CDriverPcat();
	~CDriverPcat() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;
	int OverlayTitle(unsigned titleCode) override;

	/* YM3812 レジスタ書込回数（診断） */
	unsigned OplWrites() const;

private:
	CHardPcat* hw_;
	int hostRate_;
	int cpuHz_;
	int oplHz_;
	int booted_;
	int triggered_;
	unsigned titleCode_;
	uint64_t oplResidual_;
	int64_t cpuAcc_;
};
