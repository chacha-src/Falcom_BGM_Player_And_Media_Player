#pragma once
/* raira=0 のとき kpi が書き、kbsasami_host のモニタが読む。 */
#include <stdint.h>

#define KBSMON_MAGIC 0x4E4F4D4Bu /* 'KMON' */

struct KbsMonShm {
	uint32_t magic;
	volatile long seq;
	int32_t show;
	int32_t fm;
	int32_t sampleRate;
	int32_t playf;
	int64_t playSample; /* デコード先頭。ホストが溜めた終端 */
	int64_t lagSamples; /* playSample よりスピーカが遅れている量 */
	int64_t qpc;
	int64_t qpcFreq;
	wchar_t path[520];
};

#ifdef _WIN64
#define KBSMON_MAP_NAME L"Local\\kbsasami_mon64"
#else
#define KBSMON_MAP_NAME L"Local\\kbsasami_mon32"
#endif
