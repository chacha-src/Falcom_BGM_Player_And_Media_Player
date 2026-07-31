#pragma once

struct FileTagFields {
	CString title;
	CString artist;
	CString album;
	CString year;
	CString track;
	CString genre;
	CString comment;
	int loop1;
	int loop2;

	FileTagFields() : loop1(0), loop2(0) {}
	void Clear() {
		title.Empty();
		artist.Empty();
		album.Empty();
		year.Empty();
		track.Empty();
		genre.Empty();
		comment.Empty();
		loop1 = loop2 = 0;
	}
	bool HasAnyTagField() const {
		return year.GetLength() || track.GetLength() || genre.GetLength() || comment.GetLength();
	}
	bool HasTitleArtistAlbum() const {
		return title.GetLength() || artist.GetLength() || album.GetLength();
	}
};

void ReadFileTagFields(LPCTSTR path, FileTagFields& out);
// 対応: MP3 ID3v2 / FLAC VorbisComment / WAV RIFF INFO / M4A(AAC) iTunes meta / Ogg Vorbis comment / DSF(ID3v2追記)
// 非対応例: 暗号化 flac(.qull3h), Opus 書き込み, DFF/WSD など。成功で true。
bool WriteFileTagFields(LPCTSTR path, const FileTagFields& in);

// ジャケット(カバーアート)の取り出しと埋め込み。
enum { FILETAG_COVER_MAX = 8 * 1024 * 1024 };

// 埋め込みジャケット、無ければ同名/folder.jpg 等のサイドカーを buf へ取り出す。
// 戻り値は画像バイト数(0=見つからない)。mimeOut には "image/jpeg" 等が入る。
int ExtractCoverArt(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap);

// 既存の mp3 / flac / wav / dsf へジャケットを埋め込む。成功で true。
bool EmbedCoverArt(LPCTSTR path, const BYTE* data, int dataLen, const char* mime);

// srcPath のテキストタグ(+copyCover ならジャケット)を、書き出し済みの dstPath へ写す。
bool CopyTagsAndCoverToExport(LPCTSTR srcPath, LPCTSTR dstPath, int copyCover);

// 書き出し後のメタデータ適用:
//  copyTags: 元タグをコピー / fillIfEmpty: 空の項目だけ埋める / coverImagePath: jpg/png をジャケットに
bool ApplyExportTagsAndCover(LPCTSTR srcPath, LPCTSTR dstPath, int copyTags,
	const FileTagFields* fillIfEmpty, LPCTSTR coverImagePath);
