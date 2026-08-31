#pragma once

#include <stdint.h>
#include <vector>

// ============================================================================
// KpiHost64 VST ライブ（鍵盤・パートグリッド）
// ----------------------------------------------------------------------------
// VST ホスト画面の 1..32 パート。ノートと PCM は共有メモリ、ここはパイプ側の
// ライフサイクル（ロード／アンロード／エディタ／プログラム一覧）。
// プラグインの GUI はパイプスレッドではなく専用 UI スレッドへ Marshal する
// （SC-VA のエディタをパイプスレッドで作ると一度描画して止まる）。
// ============================================================================

uint32_t VstHost64_LiveLoad(uint32_t part1to32, const wchar_t* path, uint32_t isVst3);
uint32_t VstHost64_LiveUnload(uint32_t part1to32);
uint32_t VstHost64_LiveUnloadAll();
uint32_t VstHost64_LiveMidi(uint32_t port, uint32_t msg); // フォールバック。通常は MIDI SHM
uint32_t VstHost64_LiveSysex(uint32_t port, const uint8_t* data, uint32_t len);
uint32_t VstHost64_LiveRender(uint32_t frames, std::vector<uint8_t>& reply); // フォールバック。通常は音声 SHM
uint32_t VstHost64_LiveAudioStart(); // 共有メモリ＋レンダースレッド開始
uint32_t VstHost64_LiveAudioStop();
uint32_t VstHost64_LiveEditorOpen(uint32_t part1to32);
uint32_t VstHost64_LiveEditorClose(uint32_t part1to32);
uint32_t VstHost64_LiveEditorCloseAll();
/* SampleTank: defer DestroyWindow so close-notify path returns immediately. */
void VstHost64_PostSoftTeardown(uint32_t part1to32, void* hwnd);
/* Signal ogg that editor was closed on this part (ednotify SHM + audio SHM).
   clearOpenMask=0: bump seq only (SampleTank soft-close while teardown still runs). */
void VstHost64_NotifyEditorClosed(int part1to32, int prog);
void VstHost64_NotifyEditorClosedEx(int part1to32, int prog, int clearOpenMask);
void VstHost64_ClearEditorOpenMask(int part1to32);
void VstHost64_NotifyEditorOpened(int part1to32);
/* Optional: publish close-snap byte counts into ednotify SHM. */
void VstHost64_NotifyEditorSnapLens(int part1to32, uint32_t compLen, uint32_t ctrlLen);
/* Pause/resume live render around Vst3EditorClose (avoid deadlock with process). */
void VstHost64_EditorCloseBegin(void);
void VstHost64_EditorCloseEnd(void);
uint32_t VstHost64_LiveSetSendChannel(uint32_t part1to32, int32_t sendCh); // -1=受信 ch のまま
uint32_t VstHost64_LivePrograms(uint32_t part1to32, uint32_t first, uint32_t count,
	std::vector<uint8_t>& reply);
uint32_t VstHost64_LiveSetProgram(uint32_t part1to32, uint32_t index);
uint32_t VstHost64_LiveGetState(uint32_t part1to32, uint32_t which, std::vector<uint8_t>& reply);
uint32_t VstHost64_LiveSetState(uint32_t part1to32, uint32_t which,
	const uint8_t* bytes, uint32_t len);

// パートが載っているかレンダースレッドが動いていれば非 0。
// アイドルタイムアウトがこのプロセスを落とすと、載っている音源まで消える。
int VstHost64_LiveActive();
