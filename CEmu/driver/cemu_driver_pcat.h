#pragma once
#include "cemu_driver.h"

class CHardPcat;

class CDriverPcat : public CDriver {
public:
	CDriverPcat();
	~CDriverPcat() override;

	int Open(CHard* hw, const CEmuGameEntry* ge, CEmuZipFs* fs, unsigned titleCode) override;
	void Close() override;
	int Render(int16_t* stereo, int frames) override;
	int Seek(uint64_t sample) override;

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
