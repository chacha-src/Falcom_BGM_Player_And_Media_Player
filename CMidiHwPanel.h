#pragma once
#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
// MIDI モニタ右上の実機 LCD。SC-88 橙 / XG 緑 / MT-32 緑などバックライトを機種に合わせる。
// 16ch は 16 本バー。32ch もパネルは 1 枚で、上段 A01-16・下段 B01-16。

struct MidiHwLcdState {
	BYTE letter[32];
	int letterN;
	BYTE page[10][64]; // GS 16x16 × 10 ページ（d00-d63）
	BYTE pageOn[10];
	int showPage; // 0=バー 1-10=ドットページ
	int dispTime; // -1=未指定（4秒）, 0-15=SysEx 10 20 01（0-7.2秒）
	DWORD shownMs; // 最後にカスタムパネルを出した TickCount
	int mode; // 0=パート 1=文字（左の INSTRUMENT 欄）
	unsigned gen; // 表示データが変わったら++。アニメの再描画用
};

struct MidiHwLcdPartSnap {
	int pc;
	int vol;
	int pan;
	int rev;
	int crs;
	int kshift;
	int midiCh; // 0-15, 16=off
	int port;   // 0=A 1=B
	int isDrum;
	int heard;
	int held;
	float lev;
	wchar_t name[24];
};

void MidiHwLcdReset(MidiHwLcdState* s);
int MidiHwLcdApplySysex(MidiHwLcdState* s, const BYTE* d, int n);
int MidiHwLcdKind(int sysMode, int gsMapKind);
const wchar_t* MidiHwLcdModelName(int sysMode, int gsMapKind);
int MidiHwLcdCh32(int gs32, int heardHi);
int MidiHwLcdHeadH(UINT dpi);
int MidiHwLcdAnim(const MidiHwLcdState* s);
int MidiHwLcdTick(MidiHwLcdState* s, DWORD nowMs);
int MidiHwLcdScrollIdx(const MidiHwLcdState* s, DWORD nowMs);
int MidiHwLcdReserve(int w, UINT dpi, int ch32, CRect* outAll, CRect* outA, CRect* outB);
void MidiHwLcdDraw(CDC& dc, const CRect& rc, UINT dpi,
	int kind, const wchar_t* model,
	const MidiHwLcdState& st,
	const MidiHwLcdPartSnap partsA[16],
	const BYTE keyBitsA[16],
	int selA,
	const MidiHwLcdPartSnap* partsB,
	const BYTE* keyBitsB,
	int selB);
int MidiHwLcdSeg(float lev, int held);
