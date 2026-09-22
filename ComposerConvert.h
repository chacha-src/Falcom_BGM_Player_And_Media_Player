#pragma once
// PC-98/88/X68k 系コンポーザー → 中間 SMF。
// RCP/R36/G36/G18/MCP/MTD (Recomposer)、EUP (FM Towns)、素の SMF(.mff 等)。
// 出力は %TEMP%\ogg_composer\<hash>_<stem>.mid

enum {
	COMPOSER_KIND_NONE = 0,
	COMPOSER_KIND_SMF = 1,
	COMPOSER_KIND_RCP = 2,   // RCP/R36 (v2)
	COMPOSER_KIND_G36 = 3,   // G36/G18 (v3)
	COMPOSER_KIND_MCP = 4,   // MCP/MTD (v1 近似)
	COMPOSER_KIND_EUP = 5,
	COMPOSER_KIND_GSD = 6,
	COMPOSER_KIND_CM6 = 7,
	COMPOSER_KIND_SNG = 8,  /* BALLADE / ミュージクン */
	COMPOSER_KIND_ZMS = 9   /* Z-MUSIC MIDI MML */
};

int ComposerEqExt(const wchar_t* path, const wchar_t* ext);
int ComposerIsSeqExt(const wchar_t* path);
int ComposerKindOf(const wchar_t* path);
int ComposerKindOfMem(const unsigned char* data, unsigned size);
int ComposerConvertToMidi(const wchar_t* src, wchar_t* dest, int destChars);
int ComposerFindSidecar(const wchar_t* src, const wchar_t* ext, wchar_t* out, int outChars);
int ComposerFindSidecarWrd(const wchar_t* src, wchar_t* out, int outChars);
int ComposerHasSidecarWrd(const wchar_t* src);
void ComposerTempMidiPath(const wchar_t* src, wchar_t* dest, int destChars);
