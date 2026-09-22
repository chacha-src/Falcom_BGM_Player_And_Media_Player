#pragma once
// MIDI/RCP/WRD などを含む zip/lzh 等。CEmu の zip::0001（4桁曲番）とは別経路。
// プレイリスト fol は archive::inner.mid（旧セーブの arc>inner も読む）。

enum {
	MIDIPACK_PATH = 1024,
	MIDIPACK_INNER = 520,
	MIDIPACK_MAX_SEQ = 512
};

typedef struct MidiPackSeq_ {
	wchar_t inner[MIDIPACK_INNER];
	wchar_t name[260];
	int hasWrd;
} MidiPackSeq;

int MidiPackIsArchiveExt(const wchar_t* path);
int MidiPackIsVirtualPath(const wchar_t* path);
int MidiPackParseVirtual(const wchar_t* path, wchar_t* arc, int arcChars, wchar_t* inner, int innerChars);
int MidiPackFormatVirtual(const wchar_t* arc, const wchar_t* inner, wchar_t* out, int outChars);
int MidiPackIsCemuZip(const wchar_t* zipPath);
int MidiPackIsSeqExt(const wchar_t* path);
int MidiPackListSeq(const wchar_t* arc, MidiPackSeq* out, int maxOut);
int MidiPackHasSidecarWrd(const wchar_t* src);
int MidiPackFindSidecarWrd(const wchar_t* src, wchar_t* out, int outChars);
/* 仮想パスなら一時展開して実ファイルを返す。展開したら 1。 */
int MidiPackMaterialize(const wchar_t* src, wchar_t* out, int outChars);
/* %TEMP%\ogg_midpack / ogg_composer。プレイリストに載せてよいパスではない。 */
int MidiPackIsTempExtractPath(const wchar_t* path);
