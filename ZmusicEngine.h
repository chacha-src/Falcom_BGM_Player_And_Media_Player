#pragma once
#include <stdint.h>

/* Z-MUSIC v2 の ZMD（MEASURE 12）を解釈し、ZMS から同じバイト列を出す。
   未対応オペコードは無視せず失敗を返す。 */

enum {
	ZMUSIC_OK = 0,
	ZMUSIC_ERR_ARG = 1,
	ZMUSIC_ERR_FORMAT = 2,
	ZMUSIC_ERR_OPCODE = 3,
	ZMUSIC_ERR_RANGE = 4,
	ZMUSIC_ERR_FULL = 5
};

struct ZmusicSong {
	const uint8_t* zmd;
	int zmdBytes;
	int tempo;       /* 20..300 */
	int masterClock; /* (Z n)。省略時 192 */
	int trackCount;
	int unknownOp;
};

/* .zmd / メモリ上の ZMD。song->zmd は呼び出し側のバッファを指す。 */
int ZmusicOpenZmd(ZmusicSong* song, const uint8_t* zmd, int n);

/* ZMS テキストを ZMD にする。対応は (O)/T、トラック A-H、o l v @、音符、休符、オクターブ。 */
int ZmusicCompileZms(const char* text, uint8_t* out, int cap, int* outBytes, int* errLine);

/* 44100Hz ステレオ int16。0 で終了、負でエラー。 */
int ZmusicRender(ZmusicSong* song, int16_t* stereo, int frames, int* done);

/* タイトル（.COMMENT / .PRINT）。無ければ 0。 */
int ZmusicTitle(const ZmusicSong* song, char* out, int cap);

/* 再生中の OPM レジスタ 256 バイト。keyOn は直近の $08。無ければ -1。 */
void ZmusicCopyOpmRegs(unsigned char* regs256, int* keyOn);

/* .zms / .zmd を開いて 16bit ステレオ 44100Hz を出す。 */
int ZmusicSessionOpen(const wchar_t* path);
void ZmusicSessionClose();
void ZmusicSessionRewind();
int ZmusicSessionRead(unsigned char* dst, int bytes);
int ZmusicSessionIsOpen();
void ZmusicSessionSeekFrames(int frames);
