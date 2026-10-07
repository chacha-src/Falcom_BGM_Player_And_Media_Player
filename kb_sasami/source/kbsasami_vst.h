#pragma once
#include <windows.h>
#include <stdint.h>

/* raira=0 かつ vst=1 のときだけ。らいら本体の VST 経路には入らない。
   SMF を一時ファイルにし、DLL の PE に合わせて kbsasami_host32 / host64 を起こす。 */

struct KbVstSession {
	int open;
	int arch; /* 32 or 64 */
	int slot;
	uint32_t rate;
	uint64_t lengthSamples;
	wchar_t tempMid[MAX_PATH];
};

int KbVstSessionOpen(const void* smf, uint32_t smfLen,
	const wchar_t* gsPath, const wchar_t* xgPath, KbVstSession* out);
int KbVstSessionRender(KbVstSession* s, void* pcm, uint32_t bytes,
	uint32_t* got, uint32_t* eof);
int KbVstSessionSeek(KbVstSession* s, uint64_t sample);
void KbVstSessionClose(KbVstSession* s);
/* raira=0 のモニタ開閉。再生位置は共有メモリ側。 */
int KbVstMonCmd(int show);
void KbVstDisconnect();
