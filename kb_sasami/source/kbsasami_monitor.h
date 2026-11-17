#pragma once
/* raira=0 かつ kbsasami.fmmidimonitor=1 のときだけ。窓は kbsasami_host。
   raira=1 は ogg 側のモニタ。fmmidimonitor は見ない。 */

void KbsMonConfigure(int enable);
/* 本家モニタの待ち。0..10000。既定 1700。 */
void KbsMonSetDeviceLag(int ms);
/* 最後の J と Q。無ければ 0,0。音が J を過ぎたときモニタを Q へ戻す。 */
void KbsMonSetLoop(__int64 startSample, __int64 endSample);
int KbsMonIsOn();
void KbsMonBegin(void* owner, int fm, const wchar_t* path, const char* titleSjis);
void KbsMonEnd(void* owner);
void KbsMonPlay(__int64 sample, int rate);
void KbsMonForgetPos();
void KbsMonHold(int on);
/* Seek / 再オープン後の位置。lag を積まず、可聴位置をここに置く。 */
void KbsMonSeek(__int64 sample, int rate);
/* デコード先頭ではなく、タイマが出している可聴サンプル。lag は 0。 */
void KbsMonPublishHeard(__int64 heard, int rate);
void KbsMonMidi(int port, unsigned int msg);
void KbsMonNotesOff();
/* raira=0: 測ったホスト遅れ（サンプル）。raira=1 は -1（tick ごとに dump）。 */
__int64 KbsMonHostLagSamples();
