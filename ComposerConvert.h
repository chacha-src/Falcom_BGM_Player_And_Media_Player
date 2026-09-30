#pragma once
// RCP/R36/G36/G18/MCP/MTD (Recomposer PC-98/88)、EUP (FM Towns)、
// SNG (BALLADE)、ZMS (Z-MUSIC MML)、KAR/RMI/SMF/MFF/SEQ。
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
#ifdef __cplusplus
#include <vector>
/* メモリ上の RCP/R36/G36/EUP/SNG/ZMS 等 → SMF。MThd なら何もしない。
   ホスト変換済みなら呼ばれない。本家（変換なし）や raira=1 残りでも使う。 */
int ComposerConvertMemToMidi(const unsigned char* data, unsigned size, const wchar_t* pathHint, std::vector<unsigned char>& mid);
#endif
int ComposerFindSidecar(const wchar_t* src, const wchar_t* ext, wchar_t* out, int outChars);
int ComposerFindSidecarWrd(const wchar_t* src, wchar_t* out, int outChars);
int ComposerHasSidecarWrd(const wchar_t* src);
void ComposerTempMidiPath(const wchar_t* src, wchar_t* dest, int destChars);
