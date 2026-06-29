#include "stdafx.h"
#include "FileTagInfo.h"
#include "Id3tagv1.h"
#include "Id3tagv2.h"
#include "vorbis/vorbisfile.h"
#include "opus/opusfile.h"
#include "codec/mp4ff.h"

extern ov_callbacks callbacks;

namespace {

static const UINT kTagFileShare = CFile::shareDenyNone;

static CString Utf8ToCString(const char* s, int len = -1)
{
	if (!s || !*s || len == 0)
		return _T("");
	if (len < 0)
		len = (int)strlen(s);
#if _UNICODE
	int wlen = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
	UINT cp = CP_UTF8;
	if (wlen <= 0) {
		wlen = MultiByteToWideChar(CP_ACP, 0, s, len, NULL, 0);
		if (wlen <= 0)
			return _T("");
		cp = CP_ACP;
	}
	CString out;
	LPTSTR buf = out.GetBuffer(wlen + 1);
	MultiByteToWideChar(cp, 0, s, len, buf, wlen + 1);
	buf[wlen] = 0;
	out.ReleaseBuffer(wlen);
	return out;
#else
	CString out(s, len);
	return out;
#endif
}

static void SetIfEmpty(CString& dest, const CString& src)
{
	if (dest.IsEmpty() && src.GetLength())
		dest = src;
}

static void MergeFields(FileTagFields& dest, const FileTagFields& src)
{
	SetIfEmpty(dest.title, src.title);
	SetIfEmpty(dest.artist, src.artist);
	SetIfEmpty(dest.album, src.album);
	SetIfEmpty(dest.year, src.year);
	SetIfEmpty(dest.track, src.track);
	SetIfEmpty(dest.genre, src.genre);
	SetIfEmpty(dest.comment, src.comment);
	if (dest.loop1 == 0 && src.loop1)
		dest.loop1 = src.loop1;
	if (dest.loop2 == 0 && src.loop2)
		dest.loop2 = src.loop2;
}

static CString VorbisCommentLineToCString(const char* line)
{
	if (!line)
		return _T("");
#if _UNICODE
	WCHAR f[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, line, -1, f, 1024) > 0)
		return f;
	return CString(line);
#else
	return CString(line);
#endif
}

static void ApplyVorbisCommentLine(const CString& cc, FileTagFields& out)
{
	int eq = cc.Find('=');
	if (eq <= 0)
		return;
	CString key = cc.Left(eq);
	CString val = cc.Mid(eq + 1);
	key.MakeUpper();
	if (key == _T("TITLE"))
		SetIfEmpty(out.title, val);
	else if (key == _T("ARTIST"))
		SetIfEmpty(out.artist, val);
	else if (key == _T("ALBUMARTIST") || key == _T("ALBUM ARTIST"))
		SetIfEmpty(out.artist, val);
	else if (key == _T("ALBUM"))
		SetIfEmpty(out.album, val);
	else if (key == _T("DATE") || key == _T("YEAR") || key == _T("ORIGINALDATE") || key == _T("ORIGINALYEAR"))
		SetIfEmpty(out.year, val);
	else if (key == _T("TRACK") || key == _T("TRACKNUMBER") || key == _T("TRACKNUM"))
		SetIfEmpty(out.track, val);
	else if (key == _T("GENRE"))
		SetIfEmpty(out.genre, val);
	else if (key == _T("COMMENT") || key == _T("DESCRIPTION") || key == _T("UNSYNCEDLYRICS"))
		SetIfEmpty(out.comment, val);
	else if (key == _T("LOOPSTART"))
		out.loop1 = _tstoi(val);
	else if (key == _T("LOOPLENGTH"))
		out.loop2 = _tstoi(val);
}

static void ReadId3Tags(LPCTSTR path, FileTagFields& out)
{
	CId3tagv1 ta1;
	CId3tagv2 ta2;
	int b = ta2.Load(path);
	bool v1loaded = false;
	auto ensureV1 = [&]() { if (!v1loaded) { ta1.Load(path); v1loaded = true; } };
	SetIfEmpty(out.title, ta2.GetTitle());
	if (b == -1) { ensureV1(); SetIfEmpty(out.title, ta1.GetTitle()); }
	SetIfEmpty(out.artist, ta2.GetArtist());
	if (out.artist.IsEmpty()) SetIfEmpty(out.artist, ta2.GetAlbumArtist());
	if (b == -1) { ensureV1(); SetIfEmpty(out.artist, ta1.GetArtist()); }
	SetIfEmpty(out.album, ta2.GetAlbum());
	if (b == -1) { ensureV1(); SetIfEmpty(out.album, ta1.GetAlbum()); }
	SetIfEmpty(out.year, ta2.GetYear());
	if (b == -1) {
		ensureV1();
		SetIfEmpty(out.year, ta1.GetYear());
	}
	SetIfEmpty(out.track, ta2.GetTrackNo());
	if (b == -1)
		SetIfEmpty(out.track, ta1.GetTrackNo());
	SetIfEmpty(out.genre, ta2.GetGenre());
	if (b == -1)
		SetIfEmpty(out.genre, ta1.GetGenre());
	SetIfEmpty(out.comment, ta2.GetComment());
	if (b == -1)
		SetIfEmpty(out.comment, ta1.GetComment());
}

static DWORD ExtractId3v2Size(const BYTE* sizeField)
{
	return ((DWORD)sizeField[0] << 21) | ((DWORD)sizeField[1] << 14) |
		((DWORD)sizeField[2] << 7) | (DWORD)sizeField[3];
}

static CString DecodeId3v2TextFrame(const BYTE* data, DWORD size, bool v24)
{
	if (!data || size < 2)
		return _T("");
	BYTE enc = data[0];
	const BYTE* text = data + 1;
	DWORD textLen = size - 1;
	if (enc == 0) {
		return Utf8ToCString((const char*)text);
	}
	if (enc == 3) {
		return Utf8ToCString((const char*)text);
	}
	if (enc == 1 || enc == 2) {
		if (textLen < 2)
			return _T("");
		int wlen = (int)(textLen / 2);
		CString out;
		LPTSTR buf = out.GetBuffer(wlen + 1);
#if _UNICODE
		memcpy(buf, text, textLen);
		buf[wlen] = 0;
		if (enc == 2) {
			for (int i = 0; i < wlen; i++) {
				((WCHAR*)buf)[i] = (WCHAR)(((WCHAR*)text)[i] >> 8) | (WCHAR)(((WCHAR*)text)[i] << 8);
			}
		}
#else
		WCHAR* tmp = new WCHAR[wlen + 1];
		memcpy(tmp, text, textLen);
		tmp[wlen] = 0;
		WideCharToMultiByte(CP_ACP, 0, tmp, -1, buf, wlen + 1, NULL, NULL);
		delete[] tmp;
#endif
		out.ReleaseBuffer();
		return out;
	}
	return Utf8ToCString((const char*)text);
}

static void ApplyId3v2Frame(const char frameId[5], const BYTE* data, DWORD size, bool v24, FileTagFields& out)
{
	if (!memcmp(frameId, "TIT2", 4))
		SetIfEmpty(out.title, DecodeId3v2TextFrame(data, size, v24));
	else if (!memcmp(frameId, "TPE1", 4) || !memcmp(frameId, "TPE2", 4))
		SetIfEmpty(out.artist, DecodeId3v2TextFrame(data, size, v24));
	else if (!memcmp(frameId, "TALB", 4))
		SetIfEmpty(out.album, DecodeId3v2TextFrame(data, size, v24));
	else if (!memcmp(frameId, "TYER", 4) || !memcmp(frameId, "TDRC", 4))
		SetIfEmpty(out.year, DecodeId3v2TextFrame(data, size, v24));
	else if (!memcmp(frameId, "TRCK", 4))
		SetIfEmpty(out.track, DecodeId3v2TextFrame(data, size, v24));
	else if (!memcmp(frameId, "TCON", 4))
		SetIfEmpty(out.genre, DecodeId3v2TextFrame(data, size, v24));
	else if (!memcmp(frameId, "COMM", 4)) {
		if (size > 4) {
			const BYTE* p = data + 1;
			DWORD remain = size - 1;
			if (remain > 3) {
				p += 3;
				remain -= 3;
			}
			while (remain > 0 && *p) {
				p++;
				remain--;
			}
			if (remain > 1)
				SetIfEmpty(out.comment, DecodeId3v2TextFrame(p, remain, v24));
		}
	}
}

static void ScanId3v2FramesInBuffer(const BYTE* buf, int bufLen, FileTagFields& out)
{
	for (int i = 0; i + 10 < bufLen; i++) {
		if (buf[i] != 'I' || buf[i + 1] != 'D' || buf[i + 2] != '3')
			continue;
		if (i + 10 > bufLen)
			break;
		WORD ver = (WORD)((buf[i + 3] << 8) | buf[i + 4]);
		bool v24 = (ver >= 0x0400);
		DWORD tagSize = ExtractId3v2Size(buf + i + 6);
		int frameStart = i + 10;
		int frameEnd = frameStart + (int)tagSize;
		if (frameEnd > bufLen)
			frameEnd = bufLen;
		int pos = frameStart;
		if (ver != 0x0200 && (buf[i + 5] & 0x40)) {
			if (pos + 4 <= frameEnd)
				pos += 4 + (int)ExtractId3v2Size(buf + pos);
		}
		while (pos + 10 <= frameEnd) {
			char frameId[5] = { 0 };
			memcpy(frameId, buf + pos, 4);
			if (frameId[0] == 0)
				break;
			DWORD frameSize;
			if (v24)
				frameSize = (DWORD)buf[pos + 4] << 24 | (DWORD)buf[pos + 5] << 16 |
					(DWORD)buf[pos + 6] << 8 | (DWORD)buf[pos + 7];
			else
				frameSize = ExtractId3v2Size(buf + pos + 4);
			pos += 10;
			if (pos + (int)frameSize > frameEnd)
				break;
			ApplyId3v2Frame(frameId, buf + pos, frameSize, v24, out);
			pos += (int)frameSize;
		}
		break;
	}
}

static void ScanId3v2FramesAtOffset(LPCTSTR path, ULONGLONG offset, FileTagFields& out)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return;
	const ULONGLONG fileLen = f.GetLength();
	if (offset >= fileLen)
		return;
	const int maxRead = 512 * 1024;
	int toRead = (fileLen - offset > (ULONGLONG)maxRead) ? maxRead : (int)(fileLen - offset);
	if (toRead <= 0) {
		f.Close();
		return;
	}
	BYTE* buf = (BYTE*)malloc(toRead);
	if (!buf) {
		f.Close();
		return;
	}
	f.Seek((LONGLONG)offset, CFile::begin);
	if (f.Read(buf, toRead) == toRead)
		ScanId3v2FramesInBuffer(buf, toRead, out);
	free(buf);
	f.Close();
}

static void ScanId3v2FramesInFile(LPCTSTR path, FileTagFields& out)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return;
	const int maxRead = 512 * 1024;
	const ULONGLONG fileLen = f.GetLength();
	if (fileLen <= 0) {
		f.Close();
		return;
	}
	BYTE* buf = (BYTE*)malloc(maxRead);
	if (!buf) {
		f.Close();
		return;
	}
	auto scanRegion = [&](ULONGLONG offset) {
		if (offset >= fileLen)
			return;
		int toRead = (fileLen - offset > (ULONGLONG)maxRead) ? maxRead : (int)(fileLen - offset);
		if (toRead <= 0)
			return;
		f.Seek((LONGLONG)offset, CFile::begin);
		if (f.Read(buf, toRead) == toRead)
			ScanId3v2FramesInBuffer(buf, toRead, out);
	};
	scanRegion(0);
	if (!out.HasAnyTagField() && fileLen > (ULONGLONG)maxRead)
		scanRegion(fileLen - (ULONGLONG)maxRead);
	free(buf);
	f.Close();
}

static void ReadOggVorbisTags(LPCTSTR path, FileTagFields& out)
{
	FILE* fp = _tfopen(path, _T("rb"));
	if (!fp)
		return;
	OggVorbis_File vf;
	if (ov_open_callbacks(fp, &vf, NULL, 0, callbacks) < 0) {
		fclose(fp);
		return;
	}
	for (int i = 0; i < vf.vc->comments; i++)
		ApplyVorbisCommentLine(VorbisCommentLineToCString(vf.vc->user_comments[i]), out);
	ov_clear(&vf);
	fclose(fp);
}

static void ScanVorbisKeyInBuffer(const BYTE* buf, int buflen, const char* key, CString& dest)
{
	if (!buf || buflen <= 0 || !key || dest.GetLength())
		return;
	const int keyLen = (int)strlen(key);
	if (keyLen <= 0 || buflen < keyLen + 1)
		return;
	for (int j = 0; j < buflen - keyLen; j++) {
		if (memcmp(buf + j, key, keyLen) != 0)
			continue;
		j += keyLen;
		char val[1024];
		int k = 0;
		for (; j < buflen && k < (int)sizeof(val) - 1; j++, k++) {
			if (buf[j] == 0)
				break;
			val[k] = (char)buf[j];
		}
		if (k > 0) {
			val[k] = 0;
			SetIfEmpty(dest, Utf8ToCString(val, k));
		}
		return;
	}
}

static void ScanFlacTagsInBuffer(const BYTE* buf, int buflen, FileTagFields& out)
{
	ScanVorbisKeyInBuffer(buf, buflen, "DATE=", out.year);
	ScanVorbisKeyInBuffer(buf, buflen, "date=", out.year);
	ScanVorbisKeyInBuffer(buf, buflen, "YEAR=", out.year);
	ScanVorbisKeyInBuffer(buf, buflen, "year=", out.year);
	ScanVorbisKeyInBuffer(buf, buflen, "ORIGINALDATE=", out.year);
	ScanVorbisKeyInBuffer(buf, buflen, "ORIGINALYEAR=", out.year);
	ScanVorbisKeyInBuffer(buf, buflen, "TRACKNUMBER=", out.track);
	ScanVorbisKeyInBuffer(buf, buflen, "tracknumber=", out.track);
	ScanVorbisKeyInBuffer(buf, buflen, "TRACK=", out.track);
	ScanVorbisKeyInBuffer(buf, buflen, "track=", out.track);
	ScanVorbisKeyInBuffer(buf, buflen, "GENRE=", out.genre);
	ScanVorbisKeyInBuffer(buf, buflen, "genre=", out.genre);
	ScanVorbisKeyInBuffer(buf, buflen, "COMMENT=", out.comment);
	ScanVorbisKeyInBuffer(buf, buflen, "comment=", out.comment);
	ScanVorbisKeyInBuffer(buf, buflen, "DESCRIPTION=", out.comment);
	ScanVorbisKeyInBuffer(buf, buflen, "description=", out.comment);
}

static bool ReadFileHeader(LPCTSTR path, BYTE* buf, int bufSize, int& outRead)
{
	outRead = 0;
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return false;
	outRead = (int)f.Read(buf, bufSize);
	f.Close();
	return outRead > 0;
}

static uint32_t ReadLe32(const BYTE* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void ApplyVorbisCommentBytes(const char* entry, int entryLen, FileTagFields& out)
{
	if (!entry || entryLen <= 0)
		return;
	ApplyVorbisCommentLine(Utf8ToCString(entry, entryLen), out);
}

static void ParseFlacVorbisCommentBlock(const BYTE* data, uint32_t blockLen, FileTagFields& out)
{
	if (!data || blockLen < 8)
		return;
	uint32_t pos = 0;
	uint32_t vendorLen = ReadLe32(data + pos);
	pos += 4;
	if (pos + vendorLen > blockLen)
		return;
	pos += vendorLen;
	if (pos + 4 > blockLen)
		return;
	uint32_t numComments = ReadLe32(data + pos);
	pos += 4;
	for (uint32_t i = 0; i < numComments; i++) {
		if (pos + 4 > blockLen)
			break;
		uint32_t commentLen = ReadLe32(data + pos);
		pos += 4;
		if (commentLen == 0 || pos + commentLen > blockLen)
			break;
		ApplyVorbisCommentBytes((const char*)(data + pos), (int)commentLen, out);
		pos += commentLen;
	}
}

static void ParseFlacMetadata(const BYTE* buf, int bufLen, FileTagFields& out)
{
	if (!buf || bufLen < 8)
		return;
	int start = -1;
	for (int i = 0; i + 4 <= bufLen; i++) {
		if (memcmp(buf + i, "fLaC", 4) == 0) {
			start = i;
			break;
		}
	}
	if (start < 0)
		return;
	uint32_t pos = (uint32_t)start + 4;
	while (pos + 4 <= (uint32_t)bufLen) {
		BYTE isLast = (buf[pos] & 0x80) ? 1 : 0;
		uint32_t blockType = buf[pos] & 0x7Fu;
		uint32_t blockLen = ((uint32_t)buf[pos + 1] << 16) | ((uint32_t)buf[pos + 2] << 8) | (uint32_t)buf[pos + 3];
		pos += 4;
		if (blockLen == 0 || pos + blockLen > (uint32_t)bufLen)
			break;
		if (blockType == 4)
			ParseFlacVorbisCommentBlock(buf + pos, blockLen, out);
		pos += blockLen;
		if (isLast)
			break;
	}
}

static void DecryptQull3hBuffer(BYTE* buf, int len)
{
	static const BYTE offenc[7] = { 0xd9,0x3F,0x86,0x7B,0xC7,0x61,0xaa };
	int off = 0;
	for (int i = 0; i < len; i++) {
		buf[i] ^= offenc[off];
		off = (off + 1) % 7;
	}
}

static void ReadFlacTags(LPCTSTR path, FileTagFields& out)
{
	const int bufSize = 1024 * 1024;
	BYTE* buf = (BYTE*)malloc(bufSize);
	if (!buf)
		return;
	int read = 0;
	if (!ReadFileHeader(path, buf, bufSize, read)) {
		free(buf);
		return;
	}
	if (read >= 1 && buf[0] == 0xBF)
		DecryptQull3hBuffer(buf, read);
	ParseFlacMetadata(buf, read, out);
	if (!out.HasAnyTagField())
		ScanFlacTagsInBuffer(buf, read, out);
	free(buf);
}

static void ReadOpusTags(LPCTSTR path, FileTagFields& out)
{
	int err = 0;
#if _UNICODE
	OggOpusFile* of = op_open_file((WCHAR*)path, &err);
#else
	OggOpusFile* of = op_open_file((WCHAR*)path, &err);
#endif
	if (!of)
		return;
	const OpusTags* tags = op_tags(of, -1);
	if (tags) {
		for (int i = 0; i < tags->comments; i++)
			ApplyVorbisCommentLine(VorbisCommentLineToCString(tags->user_comments[i]), out);
	}
	op_free(of);
}

static uint32_t Mp4ReadCallback(void* user_data, void* buffer, uint32_t length)
{
	return (uint32_t)fread(buffer, 1, length, (FILE*)user_data);
}

static uint32_t Mp4SeekCallback(void* user_data, uint64_t position)
{
	return fseek((FILE*)user_data, (long)position, SEEK_SET);
}

static void ApplyMp4TagItem(const char* item, const char* value, FileTagFields& out)
{
	if (!item || !value || !*item)
		return;
	CString name = Utf8ToCString(item);
	CString val = Utf8ToCString(value);
	name.MakeLower();
	if (name == _T("title") || name == _T("name"))
		SetIfEmpty(out.title, val);
	else if (name == _T("artist") || name == _T("album artist") || name == _T("albumartist"))
		SetIfEmpty(out.artist, val);
	else if (name == _T("album"))
		SetIfEmpty(out.album, val);
	else if (name == _T("date") || name == _T("year"))
		SetIfEmpty(out.year, val);
	else if (name == _T("track") || name == _T("tracknumber"))
		SetIfEmpty(out.track, val);
	else if (name == _T("genre"))
		SetIfEmpty(out.genre, val);
	else if (name == _T("comment") || name == _T("description"))
		SetIfEmpty(out.comment, val);
}

static void ReadMp4UdtaStringFromBuffer(const BYTE* buf, int read, const char key4[4], CString& dest)
{
	if (!buf || read < 20 || !key4)
		return;
	for (int i = 0; i < read - 20; i++) {
		if (memcmp(buf + i, key4, 4) != 0)
			continue;
		for (int j = i + 4; j < read - 10; j++) {
			if (buf[j] == 'd' && buf[j + 1] == 'a' && buf[j + 2] == 't' && buf[j + 3] == 'a') {
				j += 19;
				int k = j;
				for (; k < read && k - j < 1023; k++) {
					if (buf[k] == 0)
						break;
				}
				if (k > j)
					SetIfEmpty(dest, Utf8ToCString((const char*)(buf + j), k - j));
				return;
			}
		}
	}
}

static void ReadMp4TagsFromBuffer(LPCTSTR path, FileTagFields& out)
{
	const int bufSize = 1024 * 1024;
	BYTE* buf = (BYTE*)malloc(bufSize);
	if (!buf)
		return;
	int read = 0;
	if (!ReadFileHeader(path, buf, bufSize, read)) {
		free(buf);
		return;
	}
	ReadMp4UdtaStringFromBuffer(buf, read, "day", out.year);
	ReadMp4UdtaStringFromBuffer(buf, read, "cmt", out.comment);
	{
		const char namKey[4] = { (char)0xA9, 'n', 'a', 'm' };
		const char artKey[4] = { (char)0xA9, 'A', 'R', 'T' };
		const char albKey[4] = { (char)0xA9, 'a', 'l', 'b' };
		ReadMp4UdtaStringFromBuffer(buf, read, namKey, out.title);
		ReadMp4UdtaStringFromBuffer(buf, read, artKey, out.artist);
		ReadMp4UdtaStringFromBuffer(buf, read, albKey, out.album);
	}
	for (int i = 0; i < read - 12; i++) {
		if (buf[i] == 'g' && buf[i + 1] == 'n' && buf[i + 2] == 'r' && buf[i + 3] == 'e') {
			for (int j = i + 4; j < read - 10; j++) {
				if (buf[j] == 'd' && buf[j + 1] == 'a' && buf[j + 2] == 't' && buf[j + 3] == 'a' && j + 11 < read) {
					uint16_t genreIdx = (uint16_t)((buf[j + 11] << 8) | buf[j + 10]);
					if (genreIdx > 0 && genreIdx <= 148) {
						static const char* kGenres[] = {
							"Blues", "Classic Rock", "Country", "Dance", "Disco", "Funk",
							"Grunge", "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies",
							"Other", "Pop", "R&B", "Rap", "Reggae", "Rock",
							"Techno", "Industrial", "Alternative", "Ska", "Death Metal", "Pranks",
							"Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal", "Jazz+Funk",
							"Fusion", "Trance", "Classical", "Instrumental", "Acid", "House",
							"Game", "Sound Clip", "Gospel", "Noise", "AlternRock", "Bass",
							"Soul", "Punk", "Space", "Meditative", "Instrumental Pop", "Instrumental Rock",
							"Ethnic", "Gothic", "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk",
							"Eurodance", "Dream", "Southern Rock", "Comedy", "Cult", "Gangsta",
							"Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American", "Cabaret",
							"New Wave", "Psychadelic", "Rave", "Showtunes", "Trailer", "Lo-Fi",
							"Tribal", "Acid Punk", "Acid Jazz", "Polka", "Retro", "Musical",
							"Rock & Roll", "Hard Rock", "Folk", "Folk/Rock", "National Folk", "Swing",
							"Fast-Fusion", "Bebob", "Latin", "Revival", "Celtic", "Bluegrass", "Avantgarde",
							"Gothic Rock", "Progressive Rock", "Psychedelic Rock", "Symphonic Rock", "Slow Rock", "Big Band",
							"Chorus", "Easy Listening", "Acoustic", "Humour", "Speech", "Chanson",
							"Opera", "Chamber Music", "Sonata", "Symphony", "Booty Bass", "Primus",
							"Porn Groove", "Satire", "Slow Jam", "Club", "Tango", "Samba",
							"Folklore", "Ballad", "Power Ballad", "Rhythmic Soul", "Freestyle", "Duet",
							"Punk Rock", "Drum Solo", "A capella", "Euro-House", "Dance Hall",
							"Goa", "Drum & Bass", "Club House", "Hardcore", "Terror",
							"Indie", "BritPop", "NegerPunk", "Polsk Punk", "Beat",
							"Christian Gangsta", "Heavy Metal", "Black Metal", "Crossover", "Contemporary C",
							"Christian Rock", "Merengue", "Salsa", "Thrash Metal", "Anime", "JPop",
							"SynthPop"
						};
						SetIfEmpty(out.genre, CString(kGenres[genreIdx - 1]));
					}
					break;
				}
			}
			break;
		}
		if (buf[i] == 't' && buf[i + 1] == 'r' && buf[i + 2] == 'k' && buf[i + 3] == 'n') {
			for (int j = i + 4; j < read - 12; j++) {
				if (buf[j] == 'd' && buf[j + 1] == 'a' && buf[j + 2] == 't' && buf[j + 3] == 'a' && j + 13 < read) {
					uint16_t trackNo = (uint16_t)((buf[j + 11] << 8) | buf[j + 10]);
					if (trackNo > 0) {
						CString s;
						s.Format(_T("%u"), trackNo);
						SetIfEmpty(out.track, s);
					}
					break;
				}
			}
			break;
		}
	}
	free(buf);
}

static void ReadMp4Tags(LPCTSTR path, FileTagFields& out)
{
	FILE* fp = _tfopen(path, _T("rb"));
	if (!fp)
		return;
	mp4ff_callback_t callback = { 0 };
	callback.read = Mp4ReadCallback;
	callback.seek = Mp4SeekCallback;
	callback.user_data = fp;
	mp4ff_t* mp4 = mp4ff_open_read_metaonly(&callback);
	if (mp4) {
		int n = mp4ff_meta_get_num_items(mp4);
		for (int i = 0; i < n; i++) {
			char* item = NULL;
			char* value = NULL;
			if (mp4ff_meta_get_by_index(mp4, (unsigned int)i, &item, &value)) {
				ApplyMp4TagItem(item, value, out);
				if (item) free(item);
				if (value) free(value);
			}
		}
		if (!out.year.GetLength()) {
			char* value = NULL;
			if (mp4ff_meta_get_date(mp4, &value) && value) {
				SetIfEmpty(out.year, Utf8ToCString(value));
				free(value);
			}
		}
		if (!out.track.GetLength()) {
			char* value = NULL;
			if (mp4ff_meta_get_track(mp4, &value) && value) {
				SetIfEmpty(out.track, Utf8ToCString(value));
				free(value);
			}
		}
		if (!out.genre.GetLength()) {
			char* value = NULL;
			if (mp4ff_meta_get_genre(mp4, &value) && value) {
				SetIfEmpty(out.genre, Utf8ToCString(value));
				free(value);
			}
		}
		if (!out.comment.GetLength()) {
			char* value = NULL;
			if (mp4ff_meta_get_comment(mp4, &value) && value) {
				SetIfEmpty(out.comment, Utf8ToCString(value));
				free(value);
			}
		}
		mp4ff_close(mp4);
	}
	fclose(fp);
	if (!out.HasAnyTagField())
		ReadMp4TagsFromBuffer(path, out);
}

static void WavBytesToTchar(const char* val, TCHAR* out, int outCount)
{
	if (!val || !out || outCount <= 0)
		return;
	out[0] = 0;
	// MB_ERR_INVALID_CHARS: Shift-JIS の RIFF INFO を UTF-8 と誤認して文字化けしない
	if (MultiByteToWideChar(CP_UTF8, 8, val, -1, out, outCount) > 0)
		return;
	MultiByteToWideChar(CP_ACP, 0, val, -1, out, outCount);
}

static void ReadWavRiffInfoTags(LPCTSTR path, FileTagFields& out)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return;
	const ULONGLONG fileLen = f.GetLength();
	BYTE riff[12];
	if (f.Read(riff, 12) != 12) {
		f.Close();
		return;
	}
	if (riff[0] != 'R' || riff[1] != 'I' || riff[2] != 'F' || riff[3] != 'F' ||
		riff[8] != 'W' || riff[9] != 'A' || riff[10] != 'V' || riff[11] != 'E') {
		f.Close();
		return;
	}
	ULONGLONG pos = 12ULL;
	while (pos + 8ULL <= fileLen) {
		f.Seek((LONGLONG)pos, CFile::begin);
		DWORD chunkId = 0, chunkSize = 0;
		if (f.Read(&chunkId, 4) != 4)
			break;
		if (f.Read(&chunkSize, 4) != 4)
			break;
		DWORD pad = (chunkSize + 1) & ~1u;
		ULONGLONG nextPos = pos + 8ULL + (ULONGLONG)pad;
		if (nextPos > fileLen)
			break;
		if (chunkId == 0x5453494C) {
			if (chunkSize < 4) {
				pos = nextPos;
				continue;
			}
			DWORD listType = 0;
			if (f.Read(&listType, 4) != 4)
				break;
			if (listType != 0x4F464E49) {
				pos = nextPos;
				continue;
			}
			ULONGLONG innerEnd = pos + 8ULL + (ULONGLONG)chunkSize;
			ULONGLONG k = (ULONGLONG)f.GetPosition();
			while (k + 8ULL <= innerEnd && k <= fileLen) {
				f.Seek((LONGLONG)k, CFile::begin);
				DWORD subid = 0, subsize = 0;
				if (f.Read(&subid, 4) != 4)
					break;
				if (f.Read(&subsize, 4) != 4)
					break;
				if (subsize > 0x100000u)
					break;
				char val[1024];
				UINT toRead = subsize < (sizeof(val) - 1) ? subsize : (UINT)(sizeof(val) - 1);
				if (toRead > 0) {
					if (f.Read(val, toRead) != toRead)
						break;
					if (subsize > toRead)
						f.Seek((LONGLONG)(subsize - toRead), CFile::current);
					val[toRead] = 0;
					TCHAR t[1024];
					WavBytesToTchar(val, t, 1024);
					if (t[0] != 0) {
						if (subid == 0x44524349) // ICRD
							SetIfEmpty(out.year, t);
						else if (subid == 0x524E4749) // IGNR
							SetIfEmpty(out.genre, t);
						else if (subid == 0x544D4349) // ICMT
							SetIfEmpty(out.comment, t);
						else if (subid == 0x4B525449) // ITRK
							SetIfEmpty(out.track, t);
						else if (subid == 0x4D414E49) // INAM (title)
							SetIfEmpty(out.title, t);
						else if (subid == 0x54524149) // IART (artist)
							SetIfEmpty(out.artist, t);
						else if (subid == 0x44525049) // IPRD (album/product)
							SetIfEmpty(out.album, t);
					}
				}
				k += 8ULL + (ULONGLONG)((subsize + 1u) & ~1u);
			}
		}
		pos = nextPos;
	}
	f.Close();
}

static void ReadWavTags(LPCTSTR path, FileTagFields& out)
{
	ReadWavRiffInfoTags(path, out);
}

static bool ParseApeItems(const BYTE* data, DWORD dataSize, DWORD itemCount, FileTagFields& out)
{
	DWORD pos = 0;
	for (DWORD item = 0; item < itemCount; item++) {
		if (pos + 8 > dataSize)
			return false;
		DWORD valueLen = *(const DWORD*)(data + pos);
		pos += 8;
		const BYTE* keyStart = data + pos;
		DWORD keyPos = 0;
		while (pos + keyPos < dataSize && keyStart[keyPos] != 0)
			keyPos++;
		if (pos + keyPos >= dataSize)
			return false;
		CStringA key((const char*)keyStart, (int)keyPos);
		pos += keyPos + 1;
		if (pos + valueLen > dataSize)
			return false;
		CStringA val((const char*)(data + pos), (int)valueLen);
		pos += valueLen;
		CString keyU = Utf8ToCString(key);
		keyU.MakeUpper();
		CString valS = Utf8ToCString(val);
		if (keyU == _T("TITLE"))
			SetIfEmpty(out.title, valS);
		else if (keyU == _T("ARTIST") || keyU == _T("ALBUM ARTIST"))
			SetIfEmpty(out.artist, valS);
		else if (keyU == _T("ALBUM"))
			SetIfEmpty(out.album, valS);
		else if (keyU == _T("YEAR"))
			SetIfEmpty(out.year, valS);
		else if (keyU == _T("TRACK"))
			SetIfEmpty(out.track, valS);
		else if (keyU == _T("GENRE"))
			SetIfEmpty(out.genre, valS);
		else if (keyU == _T("COMMENT"))
			SetIfEmpty(out.comment, valS);
	}
	return true;
}

static void ReadApeTags(LPCTSTR path, FileTagFields& out)
{
	FILE* fp = _tfopen(path, _T("rb"));
	if (!fp)
		return;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return;
	}
	long fileSize = ftell(fp);
	if (fileSize < 32) {
		fclose(fp);
		return;
	}

	auto loadItems = [&](long headerPos, bool footerBlock) {
		if (headerPos < 0 || headerPos + 32 > fileSize)
			return;
		BYTE header[32];
		if (fseek(fp, headerPos, SEEK_SET) != 0)
			return;
		if (fread(header, 1, 32, fp) != 32)
			return;
		if (memcmp(header, "APETAGEX", 8) != 0)
			return;
		DWORD tagSize = *(DWORD*)(header + 12);
		DWORD itemCount = *(DWORD*)(header + 16);
		DWORD flags = *(DWORD*)(header + 20);
		if (tagSize < 32 || tagSize > 16 * 1024 * 1024)
			return;
		long dataStart = 0;
		long dataSize = 0;
		if (footerBlock) {
			dataStart = headerPos + 32 - (long)tagSize;
			dataSize = (long)tagSize - 32;
		}
		else {
			dataStart = headerPos + 32;
			long dataEnd = (flags & (1U << 30)) ? ((long)tagSize - 32) : (long)tagSize;
			if (dataEnd > fileSize)
				dataEnd = fileSize;
			dataSize = dataEnd - dataStart;
		}
		if (dataStart < 0 || dataSize <= 0)
			return;
		BYTE* data = (BYTE*)malloc(dataSize);
		if (!data)
			return;
		if (fseek(fp, dataStart, SEEK_SET) != 0) {
			free(data);
			return;
		}
		if ((long)fread(data, 1, dataSize, fp) == dataSize)
			ParseApeItems(data, (DWORD)dataSize, itemCount, out);
		free(data);
	};

	loadItems(fileSize - 32, true);
	if (!out.HasAnyTagField())
		loadItems(0, false);
	fclose(fp);
}

static bool IsExt(const CString& ext, LPCTSTR suffix)
{
	return ext.GetLength() >= (int)_tcslen(suffix) && ext.Right((int)_tcslen(suffix)) == suffix;
}

#pragma pack(push, 1)
struct DsfFileHeader {
	uint32_t signature;
	uint64_t chunkSize;
	uint64_t fileSize;
	uint64_t id3v2Pointer;
};

struct WsdGeneralInfo {
	uint8_t fileID[4];
	uint32_t reserved1_1;
	uint8_t version;
	uint8_t reserved1_2;
	uint16_t reserved1_3;
	uint64_t fileSize;
	uint32_t textOffset;
	uint32_t dataOffset;
	uint32_t reserved1_4;
};

struct WsdTextBlock {
	uint8_t title[128];
	uint8_t composer[128];
	uint8_t songWriter[128];
	uint8_t artist[128];
	uint8_t album[128];
	uint8_t genre[32];
	uint8_t dateAndTime[32];
	uint8_t location[32];
	uint8_t comment[512];
	uint8_t userSpecific[512];
	uint8_t reserved2[160];
};
#pragma pack(pop)

static CString FixedTextFieldToCString(const char* s, int maxLen)
{
	if (!s || maxLen <= 0)
		return _T("");
	int len = 0;
	while (len < maxLen && s[len] != 0)
		len++;
	while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t'))
		len--;
	if (len <= 0)
		return _T("");
	CString out = Utf8ToCString(s, len);
	if (out.GetLength())
		return out;
#if _UNICODE
	int wlen = MultiByteToWideChar(CP_ACP, 0, s, len, NULL, 0);
	if (wlen <= 0)
		return _T("");
	LPTSTR buf = out.GetBuffer(wlen + 1);
	MultiByteToWideChar(CP_ACP, 0, s, len, buf, wlen + 1);
	buf[wlen] = 0;
	out.ReleaseBuffer(wlen);
#endif
	return out;
}

static void ReadDsfTags(LPCTSTR path, FileTagFields& out)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return;
	DsfFileHeader hdr;
	if (f.Read(&hdr, sizeof(hdr)) != sizeof(hdr)) {
		f.Close();
		return;
	}
	f.Close();
	const uint32_t dsdSig = 0x20445344u; // 'DSD '
	if (hdr.signature != dsdSig || hdr.id3v2Pointer == 0)
		return;
	ScanId3v2FramesAtOffset(path, hdr.id3v2Pointer, out);
}

static void ReadWsdTags(LPCTSTR path, FileTagFields& out)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return;
	WsdGeneralInfo hdr;
	if (f.Read(&hdr, sizeof(hdr)) != sizeof(hdr)) {
		f.Close();
		return;
	}
	if (memcmp(hdr.fileID, "1bit", 4) != 0 || hdr.textOffset == 0) {
		f.Close();
		return;
	}
	const ULONGLONG fileLen = f.GetLength();
	if (hdr.textOffset >= fileLen) {
		f.Close();
		return;
	}
	WsdTextBlock text;
	f.Seek((LONGLONG)hdr.textOffset, CFile::begin);
	if (f.Read(&text, sizeof(text)) != sizeof(text)) {
		f.Close();
		return;
	}
	f.Close();
	SetIfEmpty(out.title, FixedTextFieldToCString((const char*)text.title, 128));
	SetIfEmpty(out.artist, FixedTextFieldToCString((const char*)text.artist, 128));
	SetIfEmpty(out.album, FixedTextFieldToCString((const char*)text.album, 128));
	SetIfEmpty(out.year, FixedTextFieldToCString((const char*)text.dateAndTime, 32));
	SetIfEmpty(out.genre, FixedTextFieldToCString((const char*)text.genre, 32));
	SetIfEmpty(out.comment, FixedTextFieldToCString((const char*)text.comment, 512));
}

static void ReadDffTags(LPCTSTR path, FileTagFields& out)
{
	ScanId3v2FramesInFile(path, out);
	if (out.comment.GetLength())
		return;
	const int bufSize = 1024 * 1024;
	BYTE* buf = (BYTE*)malloc(bufSize);
	if (!buf)
		return;
	int read = 0;
	if (!ReadFileHeader(path, buf, bufSize, read)) {
		free(buf);
		return;
	}
	for (int i = 0; i < read - 8; i++) {
		if (buf[i] == 'C' && buf[i + 1] == 'O' && buf[i + 2] == 'M' && buf[i + 3] == 'T') {
			if (i + 24 >= read)
				break;
			uint32_t textLen = ReadLe32(buf + i + 20);
			int textStart = i + 24;
			if (textLen > 0 && textLen < 4096 && textStart + (int)textLen <= read)
				SetIfEmpty(out.comment, Utf8ToCString((const char*)(buf + textStart), (int)textLen));
			break;
		}
	}
	free(buf);
}

static void ReadDsdTags(LPCTSTR path, const CString& extLower, FileTagFields& out)
{
	if (IsExt(extLower, _T(".dsf")))
		ReadDsfTags(path, out);
	else if (IsExt(extLower, _T(".wsd")))
		ReadWsdTags(path, out);
	else if (IsExt(extLower, _T(".dff")))
		ReadDffTags(path, out);
	FileTagFields id3;
	ReadId3Tags(path, id3);
	MergeFields(out, id3);
	if (!out.HasAnyTagField()) {
		FileTagFields scanned;
		ScanId3v2FramesInFile(path, scanned);
		MergeFields(out, scanned);
	}
}

} // namespace

void ReadFileTagFields(LPCTSTR path, FileTagFields& out)
{
	out.Clear();
	if (!path || !*path)
		return;

	CString ext = path;
	int slash = ext.ReverseFind('\\');
	if (slash < 0)
		slash = ext.ReverseFind('/');
	if (slash >= 0)
		ext = ext.Mid(slash + 1);
	ext.MakeLower();

	if (IsExt(ext, _T(".mp3")) || IsExt(ext, _T(".mp2")) || IsExt(ext, _T(".mp1")) || IsExt(ext, _T(".rmp"))) {
		ReadId3Tags(path, out);
		return;
	}
	if (IsExt(ext, _T(".ogg")) || IsExt(ext, _T(".qull3"))) {
		ReadOggVorbisTags(path, out);
		return;
	}
	if (IsExt(ext, _T(".opus"))) {
		ReadOpusTags(path, out);
		return;
	}
	if (IsExt(ext, _T(".flac")) || IsExt(ext, _T(".qull3h"))) {
		ReadFlacTags(path, out);
		if (!out.HasAnyTagField())
			ReadOggVorbisTags(path, out);
		FileTagFields id3;
		ReadId3Tags(path, id3);
		MergeFields(out, id3);
		return;
	}
	if (IsExt(ext, _T(".m4a")) || IsExt(ext, _T(".aac"))) {
		ReadMp4Tags(path, out);
		FileTagFields id3;
		ReadId3Tags(path, id3);
		MergeFields(out, id3);
		return;
	}
	if (IsExt(ext, _T(".wav"))) {
		ReadWavTags(path, out);
		FileTagFields id3;
		ReadId3Tags(path, id3);
		MergeFields(out, id3);
		return;
	}
	if (IsExt(ext, _T(".ape"))) {
		ReadApeTags(path, out);
		FileTagFields id3;
		ReadId3Tags(path, id3);
		MergeFields(out, id3);
		return;
	}
	if (IsExt(ext, _T(".tta"))) {
		FileTagFields id3;
		ReadId3Tags(path, id3);
		MergeFields(out, id3);
		FileTagFields ape;
		ReadApeTags(path, ape);
		MergeFields(out, ape);
		return;
	}
	if (IsExt(ext, _T(".dsf")) || IsExt(ext, _T(".dff")) || IsExt(ext, _T(".wsd"))) {
		ReadDsdTags(path, ext, out);
		return;
	}

	FileTagFields id3;
	ReadId3Tags(path, id3);
	MergeFields(out, id3);
	if (!out.HasAnyTagField()) {
		FileTagFields ape;
		ReadApeTags(path, ape);
		MergeFields(out, ape);
	}
	if (!out.HasAnyTagField()) {
		FileTagFields scanned;
		ScanId3v2FramesInFile(path, scanned);
		MergeFields(out, scanned);
	}
}
