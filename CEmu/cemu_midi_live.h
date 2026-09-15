#pragma once

/* リアルタイム MPU UART → 伸長 SMF + ショートメッセージ列 (VST inject)。
   先に PC/AT (SC-55/MT-32/SC-88 を midiout glue)。PC98 MIDI zip も Pump/Stop を共有。 */

struct CEmuMidiLiveShort {
	DWORD msg;
	int sampleOfs; /* 直近 Pump 窓内。-1 = すぐ出す */
};

int CEmuMidiLiveActive(void);

/* PCAT/PC98 midiout を起動し、スタブ SMF（数日長 + CC#111 開始）を書いてパスを返す */
int CEmuMidiLiveStartPcat(const wchar_t* zipPath, unsigned titleCode,
	wchar_t* outMidPath, int outCap);

void CEmuMidiLiveStop(void);

/* ライブ UART BGM 中の同一 zip SE: 曲を差し替えず title を注入する */
int CEmuMidiLiveSameZip(const wchar_t* zipPath);
int CEmuMidiLiveOverlayTitle(unsigned titleCode);

/* VST に既に SMF BGM があるとき: ライブ MPU を起動し SMF を置き換えずに SE を注入 */
int CEmuMidiLiveStartOverlayPcat(const wchar_t* zipPath, unsigned titleCode);

/* セッションレートで frames 進める。Steal 用ショートをキューする */
int CEmuMidiLivePump(int frames);

int CEmuMidiLiveStealShorts(CEmuMidiLiveShort* out, int maxCount);

/* セッション開始からの Pump 累積フレーム。steal した short の sampleOfs は
   これに相対なので、呼び出し側が可聴タイムラインへスタンプできる。 */
__int64 CEmuMidiLiveAudioFrames(void);

/* 最初の NoteOn を見たあと 1（プレイリスト time=-1 / ループヒント用） */
int CEmuMidiLiveHasNotes(void);

/* UART 捕捉が持っていたものと、inject/defer リングが運べたものの差。
   hw* はマシン側キャプチャからパースするので、ドライバが PC を出さなかったのか
   チェーンが落としたのかをプローブで切り分けられる。 */
struct CEmuMidiLiveDiag {
	unsigned injDropped;
	unsigned holdDropped;
	unsigned injPeak;
	unsigned holdPeak;
	unsigned hwBytes;
	unsigned hwNoteOn;
	unsigned hwNoteOff;
	unsigned hwProgram;
	unsigned hwControl;
	__int64 midiSample;
	__int64 audioSample;
};
int CEmuMidiLiveGetDiag(struct CEmuMidiLiveDiag* out);
