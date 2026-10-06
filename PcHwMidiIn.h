#pragma once
#include "CCustomPopupMenu.h"

/* ピアノロール／MIDI・FMモニタ共用の MIDI In 1/2。
   入力デバイスのコンボ、モニタへの反映、SMF 保存、MIDI Out スルー。 */

enum {
	IDM_PCHW_IN1_SMF = 42500,
	IDM_PCHW_IN2_SMF = 42501
};

void PcHwMidiInRestoreFromSave();
void PcHwMidiInShutdown();

void PcHwMidiInAppendToMenu(CCustomPopupMenu* parent);
int PcHwMidiInHandleCmd(UINT cmd, CWnd* owner); /* 1=処理した */

/* MIDI/FM モニタ Drain 用。port 0=In1 (A), 1=In2 (B)。 */
int PcHwMidiInStealShorts(BYTE* ports, DWORD* msgs, int maxN);
int PcHwMidiInStealSysex(int* port, BYTE* dst, int dstMax);
