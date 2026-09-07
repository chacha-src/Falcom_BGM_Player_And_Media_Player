#pragma once
#include <stdint.h>

struct CEmuS98Player {
	void* impl;
	DWORD sampleRate;
	UINT64 curSample;
	UINT64 endSample;
	int open;
	wchar_t sourcePath[520];
};

void CEmuS98Init(CEmuS98Player* p);
void CEmuS98Close(CEmuS98Player* p);
int CEmuS98OpenBuffer(CEmuS98Player* p, const BYTE* buf, DWORD size, DWORD sampleRate, const wchar_t* srcPath);
int CEmuS98Seek(CEmuS98Player* p, UINT64 sample, DWORD flags);
int CEmuS98Render(CEmuS98Player* p, short* outStereo, int sampleFrames);
UINT64 CEmuS98LengthSamples(const CEmuS98Player* p);
