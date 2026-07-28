#include "stdafx.h"
#include "FileTagInfo.h"
#include "Id3tagv1.h"
#include "Id3tagv2.h"
#include "vorbis/vorbisfile.h"
#include "opus/opusfile.h"
#ifndef USE_TAGGING
#define USE_TAGGING
#endif
#include "codec/mp4ff.h"
#define FLAC__NO_DLL
#include "flac/metadata.h"
#include <ogg/ogg.h>
#include <io.h>
#include <vector>

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
	else if (key == _T("LOOPEND"))
		out.loop2 = _tstoi(val);
	else if (key == _T("LOOPLENGTH")) {
		int len = _tstoi(val);
		if (out.loop1 > 0 && len > 0)
			out.loop2 = out.loop1 + len;
		else if (len > 0)
			out.loop2 = len;
	}
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
	// コメント内の LOOPSTART/LOOPLENGTH を拾う(書き込み側と対)
	if (!out.comment.IsEmpty()) {
		CString c = out.comment;
		int pos = 0;
		while (pos < c.GetLength()) {
			int nl = c.Find(_T('\n'), pos);
			CString line = (nl < 0) ? c.Mid(pos) : c.Mid(pos, nl - pos);
			pos = (nl < 0) ? c.GetLength() : nl + 1;
			line.TrimRight(_T('\r'));
			ApplyVorbisCommentLine(line, out);
		}
	}
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

// ---- タグ書き込み共通 ----

static CStringA CStringToUtf8(const CString& s)
{
#if defined(UNICODE) || defined(_UNICODE)
	if (s.IsEmpty())
		return CStringA();
	int n = WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), NULL, 0, NULL, NULL);
	if (n <= 0)
		return CStringA();
	CStringA out;
	char* buf = out.GetBuffer(n);
	WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), buf, n, NULL, NULL);
	out.ReleaseBuffer(n);
	return out;
#else
	// ACP → UTF-8
	if (s.IsEmpty())
		return CStringA();
	int wlen = MultiByteToWideChar(CP_ACP, 0, s, s.GetLength(), NULL, 0);
	if (wlen <= 0)
		return CStringA(s);
	std::vector<WCHAR> w(wlen);
	MultiByteToWideChar(CP_ACP, 0, s, s.GetLength(), w.data(), wlen);
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, NULL, 0, NULL, NULL);
	CStringA out;
	char* buf = out.GetBuffer(n);
	WideCharToMultiByte(CP_UTF8, 0, w.data(), wlen, buf, n, NULL, NULL);
	out.ReleaseBuffer(n);
	return out;
#endif
}

static CString BuildLoopAwareComment(const CString& commentIn, int loop1, int loop2)
{
	CString cleaned;
	int pos = 0;
	while (pos < commentIn.GetLength()) {
		int nl = commentIn.Find(_T('\n'), pos);
		CString line = (nl < 0) ? commentIn.Mid(pos) : commentIn.Mid(pos, nl - pos);
		pos = (nl < 0) ? commentIn.GetLength() : nl + 1;
		line.TrimRight(_T('\r'));
		CString u = line;
		u.MakeUpper();
		if (u.Left(10) == _T("LOOPSTART=") || u.Left(11) == _T("LOOPLENGTH=") || u.Left(8) == _T("LOOPEND="))
			continue;
		if (!cleaned.IsEmpty()) cleaned += _T("\r\n");
		cleaned += line;
	}
	if (loop1 > 0 || loop2 > 0) {
		CString loopLines;
		const int start = loop1 > 0 ? loop1 : 0;
		const int end = loop2 > 0 ? loop2 : 0;
		const int len = (end > start) ? (end - start) : 0;
		loopLines.Format(_T("LOOPSTART=%d\r\nLOOPEND=%d\r\nLOOPLENGTH=%d"), start, end, len);
		if (!cleaned.IsEmpty()) cleaned += _T("\r\n");
		cleaned += loopLines;
	}
	return cleaned;
}

static CStringA PathToApiA(LPCTSTR path)
{
	if (!path || !*path)
		return CStringA();
#if defined(UNICODE) || defined(_UNICODE)
	TCHAR shortPath[MAX_PATH * 2] = {};
	DWORD n = ::GetShortPathName(path, shortPath, _countof(shortPath));
	if (n > 0 && n < _countof(shortPath))
		return CStringA(shortPath);
	int bytes = WideCharToMultiByte(CP_ACP, 0, path, -1, NULL, 0, NULL, NULL);
	if (bytes <= 0)
		return CStringA();
	CStringA out;
	WideCharToMultiByte(CP_ACP, 0, path, -1, out.GetBuffer(bytes), bytes, NULL, NULL);
	out.ReleaseBuffer();
	return out;
#else
	return CStringA(path);
#endif
}

static bool WriteMp3Id3Tags(LPCTSTR path, const FileTagFields& in)
{
	CId3tagv2 tag;
	tag.Load(path);
	if (!in.title.IsEmpty()) tag.SetTitle(in.title);
	if (!in.artist.IsEmpty()) tag.SetArtist(in.artist);
	if (!in.album.IsEmpty()) tag.SetAlbum(in.album);
	if (!in.year.IsEmpty()) tag.SetYear(in.year);
	if (!in.track.IsEmpty()) tag.SetTrackNo(in.track);
	if (!in.genre.IsEmpty()) tag.SetGenre(in.genre);

	CString comment = BuildLoopAwareComment(in.comment, in.loop1, in.loop2);
	if (!comment.IsEmpty() || !in.comment.IsEmpty() || in.loop1 > 0 || in.loop2 > 0)
		tag.SetComment(comment);

	return tag.Save(path) == 0;
}

static bool FlacSetVorbisField(FLAC__StreamMetadata* vc, const char* key, const CString& val)
{
	if (!vc || !key)
		return false;
	if (val.IsEmpty())
		return true;
	CStringA u8 = CStringToUtf8(val);
	FLAC__StreamMetadata_VorbisComment_Entry entry;
	if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, key, u8))
		return false;
	// copy=false: entry はオブジェクト側が所有
	return FLAC__metadata_object_vorbiscomment_replace_comment(vc, entry, /*all=*/true, /*copy=*/false) != 0;
}

static bool WriteFlacTags(LPCTSTR path, const FileTagFields& in)
{
	// 暗号化 qull3h (先頭 0xBF) は非対応
	{
		CFile f;
		if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
			return false;
		BYTE b = 0;
		if (f.Read(&b, 1) != 1) {
			f.Close();
			return false;
		}
		f.Close();
		if (b == 0xBF)
			return false;
		if (b != 'f')
			return false;
	}

	CStringA pathA = PathToApiA(path);
	if (pathA.IsEmpty())
		return false;

	FLAC__Metadata_Chain* chain = FLAC__metadata_chain_new();
	if (!chain)
		return false;
	if (!FLAC__metadata_chain_read(chain, pathA)) {
		FLAC__metadata_chain_delete(chain);
		return false;
	}

	FLAC__Metadata_Iterator* it = FLAC__metadata_iterator_new();
	if (!it) {
		FLAC__metadata_chain_delete(chain);
		return false;
	}
	FLAC__metadata_iterator_init(it, chain);

	FLAC__StreamMetadata* vc = NULL;
	bool found = false;
	do {
		if (FLAC__metadata_iterator_get_block_type(it) == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
			vc = FLAC__metadata_iterator_get_block(it);
			found = (vc != NULL);
			break;
		}
	} while (FLAC__metadata_iterator_next(it));

	if (!found) {
		FLAC__metadata_iterator_init(it, chain); // STREAMINFO
		vc = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
		if (!vc || !FLAC__metadata_iterator_insert_block_after(it, vc)) {
			if (vc) FLAC__metadata_object_delete(vc);
			FLAC__metadata_iterator_delete(it);
			FLAC__metadata_chain_delete(chain);
			return false;
		}
		// insert 後は chain 所有。再取得
		vc = FLAC__metadata_iterator_get_block(it);
	}
	if (!vc) {
		FLAC__metadata_iterator_delete(it);
		FLAC__metadata_chain_delete(chain);
		return false;
	}

	bool ok = true;
	ok = FlacSetVorbisField(vc, "TITLE", in.title) && ok;
	ok = FlacSetVorbisField(vc, "ARTIST", in.artist) && ok;
	ok = FlacSetVorbisField(vc, "ALBUM", in.album) && ok;
	ok = FlacSetVorbisField(vc, "DATE", in.year) && ok;
	ok = FlacSetVorbisField(vc, "TRACKNUMBER", in.track) && ok;
	ok = FlacSetVorbisField(vc, "GENRE", in.genre) && ok;
	{
		CString cmt = BuildLoopAwareComment(in.comment, in.loop1, in.loop2);
		ok = FlacSetVorbisField(vc, "COMMENT", cmt) && ok;
		if (in.loop1 > 0) {
			CString s; s.Format(_T("%d"), in.loop1);
			ok = FlacSetVorbisField(vc, "LOOPSTART", s) && ok;
		}
		if (in.loop2 > 0) {
			CString s; s.Format(_T("%d"), in.loop2);
			ok = FlacSetVorbisField(vc, "LOOPEND", s) && ok;
		}
		if (in.loop1 > 0 && in.loop2 > in.loop1) {
			CString s; s.Format(_T("%d"), in.loop2 - in.loop1);
			ok = FlacSetVorbisField(vc, "LOOPLENGTH", s) && ok;
		}
	}

	FLAC__metadata_chain_sort_padding(chain);
	const bool written = FLAC__metadata_chain_write(chain, /*use_padding=*/true, /*preserve_file_stats=*/true) != 0;
	FLAC__metadata_iterator_delete(it);
	FLAC__metadata_chain_delete(chain);
	return ok && written;
}

static void WavAppendInfoChunk(std::vector<BYTE>& info, DWORD fourcc, const CString& text)
{
	if (text.IsEmpty())
		return;
	CStringA u8 = CStringToUtf8(text);
	DWORD size = (DWORD)u8.GetLength() + 1; // NUL
	BYTE id[4] = {
		(BYTE)(fourcc & 0xFF),
		(BYTE)((fourcc >> 8) & 0xFF),
		(BYTE)((fourcc >> 16) & 0xFF),
		(BYTE)((fourcc >> 24) & 0xFF)
	};
	info.insert(info.end(), id, id + 4);
	BYTE sz[4] = {
		(BYTE)(size & 0xFF),
		(BYTE)((size >> 8) & 0xFF),
		(BYTE)((size >> 16) & 0xFF),
		(BYTE)((size >> 24) & 0xFF)
	};
	info.insert(info.end(), sz, sz + 4);
	info.insert(info.end(), (const BYTE*)(LPCSTR)u8, (const BYTE*)(LPCSTR)u8 + u8.GetLength());
	info.push_back(0);
	if (size & 1)
		info.push_back(0); // word align
}

static bool WriteWavTags(LPCTSTR path, const FileTagFields& in)
{
	CFile src;
	if (!src.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return false;
	const ULONGLONG fileLen = src.GetLength();
	if (fileLen < 12 || fileLen > 512ull * 1024ull * 1024ull) {
		src.Close();
		return false;
	}
	std::vector<BYTE> file((size_t)fileLen);
	if (src.Read(file.data(), (UINT)fileLen) != fileLen) {
		src.Close();
		return false;
	}
	src.Close();
	if (memcmp(file.data(), "RIFF", 4) != 0 || memcmp(file.data() + 8, "WAVE", 4) != 0)
		return false;

	std::vector<BYTE> out;
	out.reserve((size_t)fileLen + 512);
	out.insert(out.end(), file.begin(), file.begin() + 12); // RIFF....WAVE

	size_t pos = 12;
	while (pos + 8 <= file.size()) {
		DWORD chunkId = ReadLe32(file.data() + pos);
		DWORD chunkSize = ReadLe32(file.data() + pos + 4);
		DWORD pad = (chunkSize + 1u) & ~1u;
		size_t next = pos + 8 + (size_t)pad;
		if (next > file.size())
			break;
		// 既存 LIST/INFO はスキップして差し替え
		bool skip = false;
		if (chunkId == 0x5453494C && chunkSize >= 4) { // LIST
			DWORD listType = ReadLe32(file.data() + pos + 8);
			if (listType == 0x4F464E49) // INFO
				skip = true;
		}
		if (!skip)
			out.insert(out.end(), file.begin() + pos, file.begin() + next);
		pos = next;
	}

	std::vector<BYTE> infoBody;
	infoBody.push_back('I'); infoBody.push_back('N'); infoBody.push_back('F'); infoBody.push_back('O');
	WavAppendInfoChunk(infoBody, 0x4D414E49, in.title);   // INAM
	WavAppendInfoChunk(infoBody, 0x54524149, in.artist);  // IART
	WavAppendInfoChunk(infoBody, 0x44525049, in.album);   // IPRD
	WavAppendInfoChunk(infoBody, 0x44524349, in.year);    // ICRD
	WavAppendInfoChunk(infoBody, 0x524E4749, in.genre);   // IGNR
	WavAppendInfoChunk(infoBody, 0x4B525449, in.track);   // ITRK
	WavAppendInfoChunk(infoBody, 0x544D4349, BuildLoopAwareComment(in.comment, in.loop1, in.loop2)); // ICMT

	if (infoBody.size() > 4) {
		DWORD listSize = (DWORD)infoBody.size();
		out.push_back('L'); out.push_back('I'); out.push_back('S'); out.push_back('T');
		BYTE sz[4] = {
			(BYTE)(listSize & 0xFF),
			(BYTE)((listSize >> 8) & 0xFF),
			(BYTE)((listSize >> 16) & 0xFF),
			(BYTE)((listSize >> 24) & 0xFF)
		};
		out.insert(out.end(), sz, sz + 4);
		out.insert(out.end(), infoBody.begin(), infoBody.end());
		if (listSize & 1)
			out.push_back(0);
	}

	DWORD riffDataSize = (DWORD)out.size() - 8;
	out[4] = (BYTE)(riffDataSize & 0xFF);
	out[5] = (BYTE)((riffDataSize >> 8) & 0xFF);
	out[6] = (BYTE)((riffDataSize >> 16) & 0xFF);
	out[7] = (BYTE)((riffDataSize >> 24) & 0xFF);

	CString tmp = path;
	tmp += _T(".tagtmp");
	CFile dst;
	if (!dst.Open(tmp, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL))
		return false;
	try {
		dst.Write(out.data(), (UINT)out.size());
		dst.Close();
	}
	catch (...) {
		dst.Abort();
		::DeleteFile(tmp);
		return false;
	}
	if (!::ReplaceFile(path, tmp, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
		if (!::MoveFileEx(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
			::DeleteFile(tmp);
			return false;
		}
	}
	return true;
}

static uint32_t Mp4WriteCallback(void* user_data, void* buffer, uint32_t length)
{
	return (uint32_t)fwrite(buffer, 1, length, (FILE*)user_data);
}

static uint32_t Mp4TruncateCallback(void* user_data)
{
	FILE* fp = (FILE*)user_data;
	if (!fp)
		return (uint32_t)-1;
	long pos = ftell(fp);
	if (pos < 0)
		return (uint32_t)-1;
	return (_chsize(_fileno(fp), pos) == 0) ? 0 : (uint32_t)-1;
}

static void Mp4MetaAdd(mp4ff_metadata_t& meta, const char* item, const CString& val)
{
	if (!item || val.IsEmpty())
		return;
	CStringA u8 = CStringToUtf8(val);
	mp4ff_tag_t* neu = (mp4ff_tag_t*)realloc(meta.tags, (meta.count + 1) * sizeof(mp4ff_tag_t));
	if (!neu)
		return;
	meta.tags = neu;
	meta.tags[meta.count].item = _strdup(item);
	meta.tags[meta.count].value = _strdup(u8);
	meta.tags[meta.count].len = (uint32_t)strlen(meta.tags[meta.count].value ? meta.tags[meta.count].value : "");
	meta.count++;
}

static void Mp4MetaFree(mp4ff_metadata_t& meta)
{
	for (uint32_t i = 0; i < meta.count; i++) {
		free(meta.tags[i].item);
		free(meta.tags[i].value);
	}
	free(meta.tags);
	meta.tags = NULL;
	meta.count = 0;
}

static bool WriteMp4Tags(LPCTSTR path, const FileTagFields& in)
{
	FILE* fp = _tfopen(path, _T("r+b"));
	if (!fp)
		return false;
	mp4ff_callback_t cb = {};
	cb.read = Mp4ReadCallback;
	cb.write = Mp4WriteCallback;
	cb.seek = Mp4SeekCallback;
	cb.truncate = Mp4TruncateCallback;
	cb.user_data = fp;

	mp4ff_metadata_t meta = {};
	Mp4MetaAdd(meta, "title", in.title);
	Mp4MetaAdd(meta, "artist", in.artist);
	Mp4MetaAdd(meta, "album", in.album);
	Mp4MetaAdd(meta, "date", in.year);
	Mp4MetaAdd(meta, "track", in.track);
	Mp4MetaAdd(meta, "genre", in.genre);
	Mp4MetaAdd(meta, "comment", BuildLoopAwareComment(in.comment, in.loop1, in.loop2));

	const bool ok = (meta.count > 0) && (mp4ff_meta_update(&cb, &meta) != 0);
	Mp4MetaFree(meta);
	fclose(fp);
	return ok;
}

// Ogg Vorbis: 先頭ヘッダ 3 パケットのうちコメントを差し替え、残りページをコピー
static void OggWriteU32(std::vector<BYTE>& b, uint32_t v)
{
	b.push_back((BYTE)(v & 0xFF));
	b.push_back((BYTE)((v >> 8) & 0xFF));
	b.push_back((BYTE)((v >> 16) & 0xFF));
	b.push_back((BYTE)((v >> 24) & 0xFF));
}

static std::vector<BYTE> BuildVorbisCommentPacket(const FileTagFields& in)
{
	std::vector<BYTE> pkt;
	pkt.push_back(0x03);
	pkt.push_back('v'); pkt.push_back('o'); pkt.push_back('r');
	pkt.push_back('b'); pkt.push_back('i'); pkt.push_back('s');

	const char* vendor = "oggYSE";
	OggWriteU32(pkt, (uint32_t)strlen(vendor));
	pkt.insert(pkt.end(), (const BYTE*)vendor, (const BYTE*)vendor + strlen(vendor));

	std::vector<CStringA> comments;
	auto add = [&](const char* key, const CString& val) {
		if (!key || val.IsEmpty()) return;
		CStringA line = key;
		line += "=";
		line += CStringToUtf8(val);
		comments.push_back(line);
	};
	add("TITLE", in.title);
	add("ARTIST", in.artist);
	add("ALBUM", in.album);
	add("DATE", in.year);
	add("TRACKNUMBER", in.track);
	add("GENRE", in.genre);
	add("COMMENT", BuildLoopAwareComment(in.comment, in.loop1, in.loop2));
	if (in.loop1 > 0) {
		CString s; s.Format(_T("%d"), in.loop1);
		add("LOOPSTART", s);
	}
	if (in.loop2 > 0) {
		CString s; s.Format(_T("%d"), in.loop2);
		add("LOOPEND", s);
	}

	OggWriteU32(pkt, (uint32_t)comments.size());
	for (size_t i = 0; i < comments.size(); i++) {
		OggWriteU32(pkt, (uint32_t)comments[i].GetLength());
		pkt.insert(pkt.end(), (const BYTE*)(LPCSTR)comments[i], (const BYTE*)(LPCSTR)comments[i] + comments[i].GetLength());
	}
	pkt.push_back(1); // framing bit
	return pkt;
}

static bool WriteOggVorbisTags(LPCTSTR path, const FileTagFields& in)
{
	FILE* fp = _tfopen(path, _T("rb"));
	if (!fp)
		return false;

	ogg_sync_state oy;
	ogg_sync_init(&oy);

	std::vector<ogg_packet> headers; // id, comment, setup
	headers.reserve(3);
	ogg_stream_state os_in;
	bool streamInited = false;
	long serialno = 0;
	bool gotHeaders = false;

	char* buf = ogg_sync_buffer(&oy, 4096);
	while (!gotHeaders) {
		size_t n = fread(buf, 1, 4096, fp);
		ogg_sync_wrote(&oy, (long)n);
		if (n == 0)
			break;
		ogg_page og;
		while (ogg_sync_pageout(&oy, &og) == 1) {
			if (!streamInited) {
				serialno = ogg_page_serialno(&og);
				ogg_stream_init(&os_in, serialno);
				streamInited = true;
			}
			if (ogg_page_serialno(&og) != serialno)
				continue;
			ogg_stream_pagein(&os_in, &og);
			ogg_packet op;
			while (ogg_stream_packetout(&os_in, &op) == 1) {
				ogg_packet copy = {};
				copy.bytes = op.bytes;
				copy.b_o_s = op.b_o_s;
				copy.e_o_s = op.e_o_s;
				copy.granulepos = op.granulepos;
				copy.packetno = op.packetno;
				copy.packet = (unsigned char*)malloc(op.bytes > 0 ? op.bytes : 1);
				if (!copy.packet) {
					fclose(fp);
					ogg_stream_clear(&os_in);
					ogg_sync_clear(&oy);
					return false;
				}
				if (op.bytes > 0)
					memcpy(copy.packet, op.packet, op.bytes);
				headers.push_back(copy);
				if (headers.size() >= 3) {
					gotHeaders = true;
					break;
				}
			}
			if (gotHeaders)
				break;
		}
		buf = ogg_sync_buffer(&oy, 4096);
	}

	if (!gotHeaders || headers.size() < 3 ||
		headers[0].bytes < 7 || memcmp(headers[0].packet, "\x01vorbis", 7) != 0) {
		for (size_t i = 0; i < headers.size(); i++)
			free(headers[i].packet);
		if (streamInited) ogg_stream_clear(&os_in);
		ogg_sync_clear(&oy);
		fclose(fp);
		return false;
	}

	std::vector<BYTE> newComment = BuildVorbisCommentPacket(in);
	free(headers[1].packet);
	headers[1].packet = (unsigned char*)malloc(newComment.size());
	if (!headers[1].packet) {
		for (size_t i = 0; i < headers.size(); i++)
			if (i != 1) free(headers[i].packet);
		ogg_stream_clear(&os_in);
		ogg_sync_clear(&oy);
		fclose(fp);
		return false;
	}
	memcpy(headers[1].packet, newComment.data(), newComment.size());
	headers[1].bytes = (long)newComment.size();

	CString tmp = path;
	tmp += _T(".tagtmp");
	FILE* out = _tfopen(tmp, _T("wb"));
	if (!out) {
		for (size_t i = 0; i < headers.size(); i++)
			free(headers[i].packet);
		ogg_stream_clear(&os_in);
		ogg_sync_clear(&oy);
		fclose(fp);
		return false;
	}

	ogg_stream_state os_out;
	ogg_stream_init(&os_out, serialno);
	for (int i = 0; i < 3; i++)
		ogg_stream_packetin(&os_out, &headers[i]);
	{
		ogg_page og;
		while (ogg_stream_flush(&os_out, &og)) {
			fwrite(og.header, 1, og.header_len, out);
			fwrite(og.body, 1, og.body_len, out);
		}
	}

	// 残り: 入力の続きから audio ページをコピー(シーケンス番号は os_out に合わせる)
	// シンプルに: 同期し直して serial 一致ページを pagein せず raw ではずれるため、
	// packetout で audio パケットを再ページ化する
	ogg_packet op;
	while (ogg_stream_packetout(&os_in, &op) == 1) {
		ogg_stream_packetin(&os_out, &op);
		ogg_page og;
		while (ogg_stream_pageout(&os_out, &og)) {
			fwrite(og.header, 1, og.header_len, out);
			fwrite(og.body, 1, og.body_len, out);
		}
	}
	for (;;) {
		char* b2 = ogg_sync_buffer(&oy, 4096);
		size_t n = fread(b2, 1, 4096, fp);
		ogg_sync_wrote(&oy, (long)n);
		ogg_page og;
		int po;
		while ((po = ogg_sync_pageout(&oy, &og)) == 1) {
			if (ogg_page_serialno(&og) != serialno)
				continue;
			ogg_stream_pagein(&os_in, &og);
			ogg_packet op2;
			while (ogg_stream_packetout(&os_in, &op2) == 1) {
				ogg_stream_packetin(&os_out, &op2);
				ogg_page ogw;
				while (ogg_stream_pageout(&os_out, &ogw)) {
					fwrite(ogw.header, 1, ogw.header_len, out);
					fwrite(ogw.body, 1, ogw.body_len, out);
				}
			}
		}
		if (n == 0)
			break;
	}
	{
		ogg_page ogw;
		while (ogg_stream_flush(&os_out, &ogw)) {
			fwrite(ogw.header, 1, ogw.header_len, out);
			fwrite(ogw.body, 1, ogw.body_len, out);
		}
	}

	for (size_t i = 0; i < headers.size(); i++)
		free(headers[i].packet);
	ogg_stream_clear(&os_out);
	ogg_stream_clear(&os_in);
	ogg_sync_clear(&oy);
	fclose(fp);
	fclose(out);

	if (!::ReplaceFile(path, tmp, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
		if (!::MoveFileEx(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
			::DeleteFile(tmp);
			return false;
		}
	}
	return true;
}

static bool WriteFileTagFieldsImpl(LPCTSTR path, const FileTagFields& in)
{
	if (!path || !*path)
		return false;
	CString ext = path;
	int slash = ext.ReverseFind('\\');
	if (slash < 0)
		slash = ext.ReverseFind('/');
	if (slash >= 0)
		ext = ext.Mid(slash + 1);
	ext.MakeLower();

	if (IsExt(ext, _T(".mp3")) || IsExt(ext, _T(".mp2")) || IsExt(ext, _T(".mp1")))
		return WriteMp3Id3Tags(path, in);
	if (IsExt(ext, _T(".flac")))
		return WriteFlacTags(path, in);
	if (IsExt(ext, _T(".wav")))
		return WriteWavTags(path, in);
	if (IsExt(ext, _T(".m4a")) || IsExt(ext, _T(".aac")))
		return WriteMp4Tags(path, in);
	if (IsExt(ext, _T(".ogg")) || IsExt(ext, _T(".qull3")))
		return WriteOggVorbisTags(path, in);
	return false;
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

bool WriteFileTagFields(LPCTSTR path, const FileTagFields& in)
{
	return WriteFileTagFieldsImpl(path, in);
}
