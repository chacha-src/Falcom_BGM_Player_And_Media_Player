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
	// 空文字も書く（フレーム削除＝クリア）。呼び出し側が「変更なし」を意図するときは既存値を渡す。
	tag.SetTitle(in.title);
	tag.SetArtist(in.artist);
	tag.SetAlbum(in.album);
	tag.SetYear(in.year);
	tag.SetTrackNo(in.track);
	tag.SetGenre(in.genre);
	tag.SetComment(BuildLoopAwareComment(in.comment, in.loop1, in.loop2));
	return tag.Save(path) == 0;
}

#pragma pack(push, 1)
struct DsfFileHeaderWrite {
	uint32_t signature;
	uint64_t chunkSize;
	uint64_t fileSize;
	uint64_t id3v2Pointer;
};
#pragma pack(pop)

static bool BuildId3v2FileBlob(const FileTagFields& in, const BYTE* cover, int coverLen, const char* mime,
	BYTE** outBlob, DWORD* outLen)
{
	if (!outBlob || !outLen)
		return false;
	*outBlob = NULL;
	*outLen = 0;

	TCHAR tmpDir[MAX_PATH] = {};
	TCHAR tmpFile[MAX_PATH] = {};
	if (!::GetTempPath(MAX_PATH, tmpDir) || !::GetTempFileName(tmpDir, _T("id3"), 0, tmpFile))
		return false;
	::DeleteFile(tmpFile);
	{
		CFile f;
		if (!f.Open(tmpFile, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL))
			return false;
		f.Close();
	}
	{
		CId3tagv2 tag;
		if (tag.MakeTag(tmpFile) != ERROR_SUCCESS) {
			::DeleteFile(tmpFile);
			return false;
		}
	}
	{
		CId3tagv2 tag;
		tag.Load(tmpFile);
		if (!tag.IsEnable()) {
			::DeleteFile(tmpFile);
			return false;
		}
		if (tag.GetVer() < 0x0300)
			tag.SetVer(0x0300);
		tag.SetUnSynchronization(FALSE);
		tag.SetTitle(in.title);
		tag.SetArtist(in.artist);
		tag.SetAlbum(in.album);
		tag.SetYear(in.year);
		tag.SetTrackNo(in.track);
		tag.SetGenre(in.genre);
		tag.SetComment(BuildLoopAwareComment(in.comment, in.loop1, in.loop2));
		if (cover && coverLen > 0)
			tag.SetPicture(cover, (DWORD)coverLen, mime && mime[0] ? mime : "image/jpeg");
		if (tag.Save(tmpFile) != ERROR_SUCCESS) {
			::DeleteFile(tmpFile);
			return false;
		}
	}

	CFile f;
	if (!f.Open(tmpFile, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
		::DeleteFile(tmpFile);
		return false;
	}
	const ULONGLONG len64 = f.GetLength();
	if (len64 < 10 || len64 > (ULONGLONG)(32 * 1024 * 1024)) {
		f.Close();
		::DeleteFile(tmpFile);
		return false;
	}
	const DWORD len = (DWORD)len64;
	BYTE* buf = (BYTE*)malloc(len);
	if (!buf) {
		f.Close();
		::DeleteFile(tmpFile);
		return false;
	}
	const UINT got = f.Read(buf, len);
	f.Close();
	::DeleteFile(tmpFile);
	if (got != len || buf[0] != 'I' || buf[1] != 'D' || buf[2] != '3') {
		free(buf);
		return false;
	}
	*outBlob = buf;
	*outLen = len;
	return true;
}

static bool WriteDsfId3Blob(LPCTSTR path, const BYTE* id3, DWORD id3Len)
{
	if (!path || !*path || !id3 || id3Len < 10)
		return false;
	CFile f;
	if (!f.Open(path, CFile::modeReadWrite | CFile::shareExclusive, NULL))
		return false;
	DsfFileHeaderWrite hdr;
	if (f.Read(&hdr, sizeof(hdr)) != sizeof(hdr)) {
		f.Close();
		return false;
	}
	const uint32_t dsdSig = 0x20445344u; // 'DSD '
	if (hdr.signature != dsdSig) {
		f.Close();
		return false;
	}
	const ULONGLONG fileLen = f.GetLength();
	ULONGLONG id3Off = hdr.id3v2Pointer;
	if (id3Off == 0 || id3Off > fileLen)
		id3Off = fileLen;
	try {
		f.SetLength(id3Off);
		f.Seek((LONGLONG)id3Off, CFile::begin);
		f.Write(id3, id3Len);
		hdr.id3v2Pointer = id3Off;
		hdr.fileSize = id3Off + id3Len;
		f.Seek(0, CFile::begin);
		f.Write(&hdr, sizeof(hdr));
	}
	catch (CException* e) {
		e->Delete();
		f.Close();
		return false;
	}
	f.Close();
	return true;
}

static bool WriteDsfTags(LPCTSTR path, const FileTagFields& in)
{
	// 渡されたフィールドをそのまま書く（空＝クリア）。呼び出し側がマージする。
	FileTagFields cur = in;

	BYTE* cover = (BYTE*)malloc(FILETAG_COVER_MAX);
	char mime[64] = {};
	int coverLen = 0;
	if (cover)
		coverLen = ExtractCoverArt(path, cover, FILETAG_COVER_MAX, mime, (int)sizeof(mime));
	if (coverLen <= 0)
		coverLen = 0;

	BYTE* blob = NULL;
	DWORD blobLen = 0;
	const bool built = BuildId3v2FileBlob(cur, coverLen > 0 ? cover : NULL, coverLen,
		mime[0] ? mime : NULL, &blob, &blobLen);
	if (cover) free(cover);
	if (!built)
		return false;
	const bool ok = WriteDsfId3Blob(path, blob, blobLen);
	free(blob);
	return ok;
}

static bool FlacSetVorbisField(FLAC__StreamMetadata* vc, const char* key, const CString& val)
{
	if (!vc || !key)
		return false;
	if (val.IsEmpty()) {
		// 空＝該当コメント削除（タグクリア）
		FLAC__metadata_object_vorbiscomment_remove_entries_matching(vc, key);
		return true;
	}
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
	if (IsExt(ext, _T(".dsf")))
		return WriteDsfTags(path, in);
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

static bool FileTag_EmbedCoverDsf(LPCTSTR path, const BYTE* data, int dataLen, const char* mime)
{
	if (!path || !*path || !data || dataLen <= 0)
		return false;
	FileTagFields fields;
	ReadFileTagFields(path, fields);
	BYTE* blob = NULL;
	DWORD blobLen = 0;
	if (!BuildId3v2FileBlob(fields, data, dataLen, mime, &blob, &blobLen))
		return false;
	const bool ok = WriteDsfId3Blob(path, blob, blobLen);
	free(blob);
	return ok;
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

// ==========================================================================
//  ジャケット(カバーアート)の取り出し / 埋め込み
// ==========================================================================

namespace {

// ID3v2 タグ以外の形式を総当りするときの走査幅
const int kCoverScanChunk = 512 * 1024;
// m4a の moov / flac のメタデータを探す幅
const int kCoverMetaChunk = 4 * 1024 * 1024;

DWORD CoverBe32(const BYTE* p)
{
	return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | (DWORD)p[3];
}

DWORD CoverSyncSafe32(const BYTE* p)
{
	return ((DWORD)(p[0] & 0x7F) << 21) | ((DWORD)(p[1] & 0x7F) << 14) |
		((DWORD)(p[2] & 0x7F) << 7) | (DWORD)(p[3] & 0x7F);
}

void CoverPutLe32(BYTE* p, DWORD v)
{
	p[0] = (BYTE)(v & 0xFF);
	p[1] = (BYTE)((v >> 8) & 0xFF);
	p[2] = (BYTE)((v >> 16) & 0xFF);
	p[3] = (BYTE)((v >> 24) & 0xFF);
}

void CoverPutBe32(BYTE* p, DWORD v)
{
	p[0] = (BYTE)((v >> 24) & 0xFF);
	p[1] = (BYTE)((v >> 16) & 0xFF);
	p[2] = (BYTE)((v >> 8) & 0xFF);
	p[3] = (BYTE)(v & 0xFF);
}

void CoverPutSyncSafe32(BYTE* p, DWORD v)
{
	p[0] = (BYTE)((v >> 21) & 0x7F);
	p[1] = (BYTE)((v >> 14) & 0x7F);
	p[2] = (BYTE)((v >> 7) & 0x7F);
	p[3] = (BYTE)(v & 0x7F);
}

void CoverSetMime(char* mimeOut, int mimeCap, const char* mime)
{
	if (!mimeOut || mimeCap <= 0)
		return;
	mimeOut[0] = 0;
	if (!mime || !*mime)
		return;
	int n = (int)strlen(mime);
	if (n > mimeCap - 1)
		n = mimeCap - 1;
	memcpy(mimeOut, mime, n);
	mimeOut[n] = 0;
}

const char* CoverMimeFromSignature(const BYTE* p, int len)
{
	if (!p)
		return NULL;
	if (len >= 3 && p[0] == 0xFF && p[1] == 0xD8 && p[2] == 0xFF)
		return "image/jpeg";
	if (len >= 8 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G')
		return "image/png";
	if (len >= 6 && memcmp(p, "GIF8", 4) == 0)
		return "image/gif";
	if (len >= 2 && p[0] == 'B' && p[1] == 'M')
		return "image/bmp";
	if (len >= 12 && memcmp(p, "RIFF", 4) == 0 && memcmp(p + 8, "WEBP", 4) == 0)
		return "image/webp";
	return NULL;
}

const char* CoverMimeFromExt(const CString& nameLower)
{
	if (IsExt(nameLower, _T(".png")))
		return "image/png";
	if (IsExt(nameLower, _T(".gif")))
		return "image/gif";
	if (IsExt(nameLower, _T(".bmp")))
		return "image/bmp";
	if (IsExt(nameLower, _T(".webp")))
		return "image/webp";
	return "image/jpeg";
}

// 画像本体を buf へ写す。先頭に余分なバイトがあれば署名位置まで読み飛ばす。
int CoverStore(const BYTE* src, int len, BYTE* buf, int bufCap, char* mimeOut, int mimeCap, const char* mimeHint)
{
	if (!src || len <= 0 || !buf || bufCap <= 0)
		return 0;
	const char* mime = CoverMimeFromSignature(src, len);
	if (!mime) {
		const int limit = (len < 64) ? len : 64;
		for (int i = 1; i < limit; i++) {
			mime = CoverMimeFromSignature(src + i, len - i);
			if (mime) {
				src += i;
				len -= i;
				break;
			}
		}
	}
	if (!mime) {
		// 署名不明。mime が image/* を主張しているものだけ通す
		if (!mimeHint || _strnicmp(mimeHint, "image/", 6) != 0)
			return 0;
		mime = mimeHint;
	}
	if (len > bufCap)
		return 0;
	memcpy(buf, src, len);
	CoverSetMime(mimeOut, mimeCap, mime);
	return len;
}

// ---- ID3v2 APIC ----

// 非同期化(0xFF 0x00 → 0xFF)を解除。src と dst が同一でも縮むだけなので安全。
int CoverUnsync(const BYTE* src, int len, BYTE* dst)
{
	int n = 0;
	for (int i = 0; i < len; i++) {
		dst[n++] = src[i];
		if (src[i] == 0xFF && i + 1 < len && src[i + 1] == 0x00)
			i++;
	}
	return n;
}

int CoverFromApicBody(const BYTE* d, int len, bool v22, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	if (!d || len < 6)
		return 0;
	int p = 0;
	const BYTE enc = d[p++];
	char mime[64] = {};
	if (v22) {
		// v2.2 PIC は mime ではなく 3 文字の画像形式
		if (p + 3 > len)
			return 0;
		if (_strnicmp((const char*)(d + p), "PNG", 3) == 0)
			strcpy_s(mime, "image/png");
		else
			strcpy_s(mime, "image/jpeg");
		p += 3;
	}
	else {
		int m = 0;
		while (p < len && d[p] != 0) {
			if (m < (int)sizeof(mime) - 1)
				mime[m++] = (char)d[p];
			p++;
		}
		mime[m] = 0;
		if (p >= len)
			return 0;
		p++;
		if (strcmp(mime, "-->") == 0)
			return 0; // URL 参照は非対応
		if (_stricmp(mime, "JPG") == 0 || _stricmp(mime, "JPEG") == 0)
			strcpy_s(mime, "image/jpeg");
		else if (_stricmp(mime, "PNG") == 0)
			strcpy_s(mime, "image/png");
	}
	if (p >= len)
		return 0;
	p++; // picture type
	if (enc == 1 || enc == 2) {
		while (p + 1 < len && !(d[p] == 0 && d[p + 1] == 0))
			p += 2;
		p += 2;
	}
	else {
		while (p < len && d[p] != 0)
			p++;
		p++;
	}
	if (p >= len)
		return 0;
	return CoverStore(d + p, len - p, buf, bufCap, mimeOut, mimeCap, mime);
}

// tag は ID3v2 ヘッダ直後(フレーム列の先頭)。タグ単位の非同期化は解除済みであること。
int CoverFromId3v2Frames(const BYTE* tag, int tagLen, WORD ver, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	const bool v22 = (ver < 0x0300);
	const bool v24 = (ver >= 0x0400);
	const int hdrLen = v22 ? 6 : 10;
	int pos = 0;
	while (pos + hdrLen <= tagLen) {
		if (tag[pos] == 0)
			break; // パディング
		int frameSize;
		WORD flags = 0;
		bool isPic;
		if (v22) {
			isPic = (memcmp(tag + pos, "PIC", 3) == 0);
			frameSize = (int)(((DWORD)tag[pos + 3] << 16) | ((DWORD)tag[pos + 4] << 8) | (DWORD)tag[pos + 5]);
		}
		else {
			isPic = (memcmp(tag + pos, "APIC", 4) == 0);
			frameSize = v24 ? (int)CoverSyncSafe32(tag + pos + 4) : (int)CoverBe32(tag + pos + 4);
			flags = (WORD)(((WORD)tag[pos + 8] << 8) | tag[pos + 9]);
		}
		if (frameSize <= 0 || pos + hdrLen + frameSize > tagLen)
			break;
		if (isPic) {
			const BYTE* body = tag + pos + hdrLen;
			int bodyLen = frameSize;
			if (v24 && (flags & 0x0001)) { // データ長インジケータ
				if (bodyLen <= 4)
					return 0;
				body += 4;
				bodyLen -= 4;
			}
			if (v24 && (flags & 0x0002)) { // フレーム単位の非同期化
				BYTE* tmp = (BYTE*)malloc(bodyLen);
				if (!tmp)
					return 0;
				const int n = CoverUnsync(body, bodyLen, tmp);
				const int got = CoverFromApicBody(tmp, n, v22, buf, bufCap, mimeOut, mimeCap);
				free(tmp);
				return got;
			}
			return CoverFromApicBody(body, bodyLen, v22, buf, bufCap, mimeOut, mimeCap);
		}
		pos += hdrLen + frameSize;
	}
	return 0;
}

int CoverFromId3v2AtOffset(LPCTSTR path, ULONGLONG offset, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return 0;
	const ULONGLONG fileLen = f.GetLength();
	if (offset + 10ULL > fileLen) {
		f.Close();
		return 0;
	}
	f.Seek((LONGLONG)offset, CFile::begin);
	BYTE head[10];
	if (f.Read(head, 10) != 10 || memcmp(head, "ID3", 3) != 0) {
		f.Close();
		return 0;
	}
	const WORD ver = (WORD)(((WORD)head[3] << 8) | head[4]);
	if (ver < 0x0200 || ver > 0x0400) {
		f.Close();
		return 0;
	}
	DWORD tagSize = CoverSyncSafe32(head + 6);
	if (tagSize < 10 || tagSize > (DWORD)FILETAG_COVER_MAX + 1024u * 1024u) {
		f.Close();
		return 0;
	}
	if (offset + 10ULL + (ULONGLONG)tagSize > fileLen)
		tagSize = (DWORD)(fileLen - offset - 10ULL);
	BYTE* tag = (BYTE*)malloc(tagSize);
	if (!tag) {
		f.Close();
		return 0;
	}
	if (f.Read(tag, tagSize) != tagSize) {
		free(tag);
		f.Close();
		return 0;
	}
	f.Close();

	int tagLen = (int)tagSize;
	if (head[5] & 0x80)
		tagLen = CoverUnsync(tag, tagLen, tag);
	int pos = 0;
	if (ver != 0x0200 && (head[5] & 0x40) && tagLen >= 4) {
		// 拡張ヘッダを読み飛ばす(v2.3 はサイズにヘッダ4byteを含まない)
		pos = (ver >= 0x0400) ? (int)CoverSyncSafe32(tag) : (4 + (int)CoverBe32(tag));
		if (pos < 0 || pos >= tagLen)
			pos = 0;
	}
	const int n = CoverFromId3v2Frames(tag + pos, tagLen - pos, ver, buf, bufCap, mimeOut, mimeCap);
	free(tag);
	return n;
}

// 先頭以外(dff/wsd/末尾付加タグ等)に置かれた ID3v2 ヘッダを探す
bool CoverFindId3Offset(LPCTSTR path, ULONGLONG& outOffset)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return false;
	const ULONGLONG fileLen = f.GetLength();
	BYTE* scan = (BYTE*)malloc(kCoverScanChunk);
	if (!scan) {
		f.Close();
		return false;
	}
	ULONGLONG regions[2] = { 0ULL, 0ULL };
	int regionCnt = 1;
	if (fileLen > (ULONGLONG)kCoverScanChunk) {
		regions[1] = fileLen - (ULONGLONG)kCoverScanChunk;
		regionCnt = 2;
	}
	bool found = false;
	for (int r = 0; r < regionCnt && !found; r++) {
		const ULONGLONG remain = fileLen - regions[r];
		const int toRead = (remain > (ULONGLONG)kCoverScanChunk) ? kCoverScanChunk : (int)remain;
		if (toRead < 20)
			continue;
		f.Seek((LONGLONG)regions[r], CFile::begin);
		if (f.Read(scan, toRead) != (UINT)toRead)
			continue;
		for (int i = 0; i + 10 <= toRead; i++) {
			if (scan[i] != 'I' || scan[i + 1] != 'D' || scan[i + 2] != '3')
				continue;
			const WORD ver = (WORD)(((WORD)scan[i + 3] << 8) | scan[i + 4]);
			if (ver < 0x0200 || ver > 0x0400)
				continue;
			if ((scan[i + 6] | scan[i + 7] | scan[i + 8] | scan[i + 9]) & 0x80)
				continue; // sync-safe でないのでタグヘッダではない
			outOffset = regions[r] + (ULONGLONG)i;
			found = true;
			break;
		}
	}
	free(scan);
	f.Close();
	return found;
}

int CoverFromId3v2Anywhere(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	int n = CoverFromId3v2AtOffset(path, 0ULL, buf, bufCap, mimeOut, mimeCap);
	if (n > 0)
		return n;
	ULONGLONG off = 0;
	if (CoverFindId3Offset(path, off) && off != 0)
		n = CoverFromId3v2AtOffset(path, off, buf, bufCap, mimeOut, mimeCap);
	return n;
}

// ---- FLAC / Ogg の PICTURE ブロック ----

// FLAC METADATA_BLOCK_PICTURE のペイロード(ブロックヘッダを含まない)を解析
int CoverFromFlacPicturePayload(const BYTE* d, int len, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	if (!d || len < 32)
		return 0;
	int p = 4; // picture type
	const DWORD mimeLen = CoverBe32(d + p);
	p += 4;
	if (mimeLen > 255u || p + (int)mimeLen + 4 > len)
		return 0;
	char mime[64] = {};
	{
		const int m = (int)((mimeLen < sizeof(mime) - 1) ? mimeLen : sizeof(mime) - 1);
		memcpy(mime, d + p, m);
		mime[m] = 0;
	}
	p += (int)mimeLen;
	const DWORD descLen = CoverBe32(d + p);
	p += 4;
	if (descLen > (DWORD)len || p + (int)descLen + 20 > len)
		return 0;
	p += (int)descLen + 16; // description + width/height/depth/colors
	const DWORD dataLen = CoverBe32(d + p);
	p += 4;
	if (dataLen == 0 || dataLen > (DWORD)(len - p))
		return 0;
	return CoverStore(d + p, (int)dataLen, buf, bufCap, mimeOut, mimeCap, mime);
}

// fLaC メタデータブロック列から PICTURE を探す(qull3h の復号後バッファ用)
int CoverFromFlacBuffer(const BYTE* data, int len, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	if (!data || len < 8)
		return 0;
	int start = -1;
	const int searchEnd = (len < 4096) ? len : 4096;
	for (int i = 0; i + 4 <= searchEnd; i++) {
		if (memcmp(data + i, "fLaC", 4) == 0) {
			start = i;
			break;
		}
	}
	if (start < 0)
		return 0;
	int pos = start + 4;
	int best = 0;
	while (pos + 4 <= len) {
		const BYTE isLast = (data[pos] & 0x80) ? 1 : 0;
		const DWORD blockType = data[pos] & 0x7Fu;
		const int blockLen = (int)(((DWORD)data[pos + 1] << 16) | ((DWORD)data[pos + 2] << 8) | (DWORD)data[pos + 3]);
		pos += 4;
		if (blockLen <= 0 || pos + blockLen > len)
			break;
		if (blockType == 6) { // PICTURE
			const int n = CoverFromFlacPicturePayload(data + pos, blockLen, buf, bufCap, mimeOut, mimeCap);
			if (n > 0) {
				best = n;
				if (data[pos + 3] == 3) // front cover
					break;
			}
		}
		pos += blockLen;
		if (isLast)
			break;
	}
	return best;
}

int CoverB64Val(char c)
{
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

// 戻り値は復号バイト数。outCap を超えるものは -1。
int CoverB64Decode(const char* s, int inLen, BYTE* out, int outCap)
{
	int n = 0;
	DWORD acc = 0;
	int bits = 0;
	for (int i = 0; i < inLen; i++) {
		const int v = CoverB64Val(s[i]);
		if (v < 0)
			continue; // '=' や改行は無視
		acc = (acc << 6) | (DWORD)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (n >= outCap)
				return -1;
			out[n++] = (BYTE)((acc >> bits) & 0xFF);
		}
	}
	return n;
}

int CoverFromB64PictureBlock(const char* b64, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	if (!b64 || !*b64)
		return 0;
	const int inLen = (int)strlen(b64);
	if (inLen < 44)
		return 0;
	const int cap = inLen / 4 * 3 + 4;
	if (cap > FILETAG_COVER_MAX + 4096)
		return 0;
	BYTE* dec = (BYTE*)malloc(cap);
	if (!dec)
		return 0;
	const int len = CoverB64Decode(b64, inLen, dec, cap);
	const int n = (len > 32) ? CoverFromFlacPicturePayload(dec, len, buf, bufCap, mimeOut, mimeCap) : 0;
	free(dec);
	return n;
}

// 旧仕様の COVERART=(画像そのものの base64)
int CoverFromB64RawImage(const char* b64, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	if (!b64 || !*b64)
		return 0;
	const int inLen = (int)strlen(b64);
	if (inLen < 16 || inLen / 4 * 3 + 4 > FILETAG_COVER_MAX + 4096)
		return 0;
	const int len = CoverB64Decode(b64, inLen, buf, bufCap);
	if (len <= 0)
		return 0;
	const char* mime = CoverMimeFromSignature(buf, len);
	if (!mime)
		return 0;
	CoverSetMime(mimeOut, mimeCap, mime);
	return len;
}

int CoverFromVorbisComment(const char* comment, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	if (!comment)
		return 0;
	if (_strnicmp(comment, "METADATA_BLOCK_PICTURE=", 23) == 0)
		return CoverFromB64PictureBlock(comment + 23, buf, bufCap, mimeOut, mimeCap);
	if (_strnicmp(comment, "COVERART=", 9) == 0)
		return CoverFromB64RawImage(comment + 9, buf, bufCap, mimeOut, mimeCap);
	return 0;
}

int CoverFromFlacFile(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	CStringA pathA = PathToApiA(path);
	if (pathA.IsEmpty())
		return 0;
	FLAC__Metadata_Chain* chain = FLAC__metadata_chain_new();
	if (!chain)
		return 0;
	if (!FLAC__metadata_chain_read(chain, pathA)) {
		FLAC__metadata_chain_delete(chain);
		return 0;
	}
	FLAC__Metadata_Iterator* it = FLAC__metadata_iterator_new();
	if (!it) {
		FLAC__metadata_chain_delete(chain);
		return 0;
	}
	FLAC__metadata_iterator_init(it, chain);
	int best = 0;
	do {
		if (FLAC__metadata_iterator_get_block_type(it) != FLAC__METADATA_TYPE_PICTURE)
			continue;
		const FLAC__StreamMetadata* b = FLAC__metadata_iterator_get_block(it);
		if (!b || !b->data.picture.data || b->data.picture.data_length == 0)
			continue;
		const int n = CoverStore(b->data.picture.data, (int)b->data.picture.data_length,
			buf, bufCap, mimeOut, mimeCap, b->data.picture.mime_type);
		if (n > 0) {
			best = n;
			if (b->data.picture.type == FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER)
				break;
		}
	} while (FLAC__metadata_iterator_next(it));
	FLAC__metadata_iterator_delete(it);
	FLAC__metadata_chain_delete(chain);
	return best;
}

// 暗号化 flac(.qull3h, 先頭 0xBF) 向けの生バッファ走査
int CoverFromFlacRawScan(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	BYTE* scan = (BYTE*)malloc(kCoverMetaChunk);
	if (!scan)
		return 0;
	int read = 0;
	if (!ReadFileHeader(path, scan, kCoverMetaChunk, read)) {
		free(scan);
		return 0;
	}
	if (read >= 1 && scan[0] == 0xBF)
		DecryptQull3hBuffer(scan, read);
	const int n = CoverFromFlacBuffer(scan, read, buf, bufCap, mimeOut, mimeCap);
	free(scan);
	return n;
}

// ---- Ogg Vorbis / Opus ----

size_t CoverOggRead(void* ptr, size_t size, size_t nmemb, void* datasource)
{
	return fread(ptr, size, nmemb, (FILE*)datasource);
}

int CoverOggSeek(void* datasource, ogg_int64_t offset, int whence)
{
	return fseek((FILE*)datasource, (long)offset, whence);
}

int CoverOggClose(void* /*datasource*/)
{
	return 0; // FILE* は呼び出し側で閉じる
}

long CoverOggTell(void* datasource)
{
	return ftell((FILE*)datasource);
}

int CoverFromOggVorbis(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	FILE* fp = _tfopen(path, _T("rb"));
	if (!fp)
		return 0;
	// 再生用グローバル(oggf)を触らないローカルコールバックで開く
	ov_callbacks cb = { CoverOggRead, CoverOggSeek, CoverOggClose, CoverOggTell };
	OggVorbis_File vf;
	if (ov_open_callbacks(fp, &vf, NULL, 0, cb) < 0) {
		fclose(fp);
		return 0;
	}
	int n = 0;
	if (vf.vc) {
		for (int i = 0; i < vf.vc->comments && n == 0; i++)
			n = CoverFromVorbisComment(vf.vc->user_comments[i], buf, bufCap, mimeOut, mimeCap);
	}
	ov_clear(&vf);
	fclose(fp);
	return n;
}

int CoverFromOpus(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	int err = 0;
	OggOpusFile* of = op_open_file((WCHAR*)path, &err);
	if (!of)
		return 0;
	int n = 0;
	const OpusTags* tags = op_tags(of, -1);
	if (tags) {
		for (int i = 0; i < tags->comments && n == 0; i++)
			n = CoverFromVorbisComment(tags->user_comments[i], buf, bufCap, mimeOut, mimeCap);
	}
	op_free(of);
	return n;
}

// ---- M4A / MP4 covr ----

int CoverFromM4aBuffer(const BYTE* d, int len, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	for (int i = 0; i + 24 <= len; i++) {
		if (d[i] != 'c' || d[i + 1] != 'o' || d[i + 2] != 'v' || d[i + 3] != 'r')
			continue;
		if (memcmp(d + i + 8, "data", 4) != 0)
			continue;
		// covr / dataサイズ / 'data' / version+flags / reserved / 画像
		const DWORD dataSize = CoverBe32(d + i + 4);
		if (dataSize <= 16u)
			continue;
		const int start = i + 20;
		int payload = (int)(dataSize - 16u);
		if (start + payload > len)
			payload = len - start;
		if (payload <= 0)
			continue;
		const int n = CoverStore(d + start, payload, buf, bufCap, mimeOut, mimeCap, NULL);
		if (n > 0)
			return n;
	}
	return 0;
}

int CoverFromM4a(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return 0;
	const ULONGLONG fileLen = f.GetLength();
	BYTE* scan = (BYTE*)malloc(kCoverMetaChunk);
	if (!scan) {
		f.Close();
		return 0;
	}
	ULONGLONG regions[2] = { 0ULL, 0ULL };
	int regionCnt = 1;
	if (fileLen > (ULONGLONG)kCoverMetaChunk) {
		regions[1] = fileLen - (ULONGLONG)kCoverMetaChunk;
		regionCnt = 2;
	}
	int n = 0;
	for (int r = 0; r < regionCnt && n == 0; r++) {
		const ULONGLONG remain = fileLen - regions[r];
		const int toRead = (remain > (ULONGLONG)kCoverMetaChunk) ? kCoverMetaChunk : (int)remain;
		if (toRead < 24)
			continue;
		f.Seek((LONGLONG)regions[r], CFile::begin);
		if (f.Read(scan, toRead) != (UINT)toRead)
			continue;
		n = CoverFromM4aBuffer(scan, toRead, buf, bufCap, mimeOut, mimeCap);
	}
	free(scan);
	f.Close();
	return n;
}

// ---- WAV(RIFF/RF64) チャンク走査 ----

struct WavChunkLayout {
	bool rf64;
	ULONGLONG ds64Offset;   // 0 = なし
	ULONGLONG id3Offset;    // 'id3 ' チャンクのヘッダ位置。0 = なし
	ULONGLONG id3DataOffset;
	ULONGLONG endOffset;    // 最後のチャンクの終端(追記位置)
};

bool WavScanChunks(CFile& f, WavChunkLayout& out)
{
	memset(&out, 0, sizeof(out));
	const ULONGLONG fileLen = f.GetLength();
	if (fileLen < 12ULL)
		return false;
	BYTE riff[12];
	f.SeekToBegin();
	if (f.Read(riff, 12) != 12)
		return false;
	if (memcmp(riff + 8, "WAVE", 4) != 0)
		return false;
	if (memcmp(riff, "RIFF", 4) == 0)
		out.rf64 = false;
	else if (memcmp(riff, "RF64", 4) == 0)
		out.rf64 = true;
	else
		return false;

	ULONGLONG ds64DataSize = 0;
	bool haveDs64 = false;
	ULONGLONG pos = 12ULL;
	ULONGLONG end = 12ULL;
	while (pos + 8ULL <= fileLen) {
		f.Seek((LONGLONG)pos, CFile::begin);
		BYTE hdr[8];
		if (f.Read(hdr, 8) != 8)
			break;
		const DWORD sz32 = ReadLe32(hdr + 4);
		ULONGLONG chunkSize = (ULONGLONG)sz32;
		if (memcmp(hdr, "ds64", 4) == 0) {
			out.ds64Offset = pos;
			BYTE d[28];
			if (f.Read(d, 28) == 28) {
				memcpy(&ds64DataSize, d + 8, 8);
				haveDs64 = true;
			}
		}
		else if (memcmp(hdr, "data", 4) == 0 && sz32 == 0xFFFFFFFFu && haveDs64) {
			chunkSize = ds64DataSize;
		}
		else if (memcmp(hdr, "id3 ", 4) == 0) {
			out.id3Offset = pos;
			out.id3DataOffset = pos + 8ULL;
		}
		const ULONGLONG next = pos + 8ULL + chunkSize + (chunkSize & 1ULL);
		if (next <= pos || next > fileLen) {
			end = fileLen;
			break;
		}
		end = next;
		pos = next;
	}
	out.endOffset = end;
	return true;
}

int CoverFromWav(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	ULONGLONG id3DataOffset = 0;
	{
		CFile f;
		if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
			return 0;
		WavChunkLayout lay;
		const bool ok = WavScanChunks(f, lay);
		f.Close();
		if (ok)
			id3DataOffset = lay.id3DataOffset;
	}
	if (id3DataOffset != 0) {
		const int n = CoverFromId3v2AtOffset(path, id3DataOffset, buf, bufCap, mimeOut, mimeCap);
		if (n > 0)
			return n;
	}
	return CoverFromId3v2Anywhere(path, buf, bufCap, mimeOut, mimeCap);
}

int CoverFromDsf(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return 0;
	DsfFileHeader hdr;
	const bool got = (f.Read(&hdr, sizeof(hdr)) == sizeof(hdr));
	f.Close();
	if (!got || hdr.signature != 0x20445344u || hdr.id3v2Pointer == 0)
		return 0;
	return CoverFromId3v2AtOffset(path, hdr.id3v2Pointer, buf, bufCap, mimeOut, mimeCap);
}

// ---- サイドカー画像 ----

int CoverFromSidecarFile(const CString& imagePath, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	if (::GetFileAttributes(imagePath) == INVALID_FILE_ATTRIBUTES)
		return 0;
	CFile f;
	if (!f.Open(imagePath, CFile::modeRead | kTagFileShare, NULL))
		return 0;
	const ULONGLONG len = f.GetLength();
	if (len == 0ULL || len > (ULONGLONG)bufCap) {
		f.Close();
		return 0;
	}
	const UINT got = f.Read(buf, (UINT)len);
	f.Close();
	if (got != (UINT)len)
		return 0;
	const char* sig = CoverMimeFromSignature(buf, (int)got);
	if (sig)
		CoverSetMime(mimeOut, mimeCap, sig);
	else {
		CString lower = imagePath;
		lower.MakeLower();
		CoverSetMime(mimeOut, mimeCap, CoverMimeFromExt(lower));
	}
	return (int)got;
}

int CoverFromSidecar(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	static const TCHAR* kNear[] = {
		_T(".jpg"), _T(".jpeg"), _T(".png"), _T(".bmp"), _T(".gif")
	};
	static const TCHAR* kFolder[] = {
		_T("folder.jpg"), _T("cover.jpg"), _T("front.jpg"), _T("AlbumArt.jpg"),
		_T("folder.png"), _T("cover.png"), _T("front.png")
	};
	const CString orig = path;
	const int dot = orig.ReverseFind(_T('.'));
	if (dot > 0) {
		const CString base = orig.Left(dot);
		for (int i = 0; i < 5; i++) {
			const int n = CoverFromSidecarFile(base + kNear[i], buf, bufCap, mimeOut, mimeCap);
			if (n > 0)
				return n;
		}
	}
	int slash = orig.ReverseFind(_T('\\'));
	if (slash < 0)
		slash = orig.ReverseFind(_T('/'));
	if (slash > 0) {
		const CString dir = orig.Left(slash + 1);
		for (int i = 0; i < 7; i++) {
			const int n = CoverFromSidecarFile(dir + kFolder[i], buf, bufCap, mimeOut, mimeCap);
			if (n > 0)
				return n;
		}
	}
	return 0;
}

// ---- 埋め込み ----

// CId3tagv2 は ID3v2 が無いファイルを Save できないので、空タグを先頭に足しておく
bool Mp3EnsureId3v2(LPCTSTR path)
{
	{
		CFile f;
		if (!f.Open(path, CFile::modeRead | kTagFileShare, NULL))
			return false;
		BYTE head[3] = {};
		const UINT got = f.Read(head, 3);
		const ULONGLONG fileLen = f.GetLength();
		f.Close();
		if (got == 3 && head[0] == 'I' && head[1] == 'D' && head[2] == '3')
			return true;
		if (fileLen == 0ULL)
			return false;
	}

	const DWORD kPad = 2048;
	CString tmp = path;
	tmp += _T(".covtmp");
	CFile src, dst;
	if (!src.Open(path, CFile::modeRead | kTagFileShare, NULL))
		return false;
	if (!dst.Open(tmp, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL)) {
		src.Close();
		return false;
	}
	bool ok = true;
	try {
		BYTE hdr[10] = { 'I', 'D', '3', 0x03, 0x00, 0x00, 0, 0, 0, 0 };
		CoverPutSyncSafe32(hdr + 6, kPad);
		dst.Write(hdr, 10);
		BYTE zero[512] = {};
		for (DWORD w = 0; w < kPad; w += (DWORD)sizeof(zero))
			dst.Write(zero, sizeof(zero));
		BYTE copy[64 * 1024];
		for (;;) {
			const UINT r = src.Read(copy, sizeof(copy));
			if (r == 0)
				break;
			dst.Write(copy, r);
		}
	}
	catch (CException* e) {
		e->Delete();
		ok = false;
	}
	src.Close();
	if (ok)
		dst.Close();
	else
		dst.Abort();
	if (!ok) {
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

bool EmbedCoverMp3(LPCTSTR path, const BYTE* data, int dataLen, const char* mime)
{
	if (!Mp3EnsureId3v2(path))
		return false;
	CId3tagv2 tag;
	tag.Load(path);
	if (!tag.IsEnable())
		return false;
	if (tag.GetVer() < 0x0300)
		tag.SetVer(0x0300); // v2.2 の PIC 形式は作らない
	// 非同期化すると JPEG に 0x00 が挿入され、本体の APIC 走査で読めなくなる
	tag.SetUnSynchronization(FALSE);
	tag.SetPicture(data, (DWORD)dataLen, mime);
	return tag.Save(path) == ERROR_SUCCESS;
}

bool EmbedCoverFlac(LPCTSTR path, const BYTE* data, int dataLen, const char* mime)
{
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

	// 既存の PICTURE をすべて外す(削除でイテレータ位置が変わるため毎回頭から)
	for (;;) {
		bool removed = false;
		FLAC__metadata_iterator_init(it, chain);
		do {
			if (FLAC__metadata_iterator_get_block_type(it) == FLAC__METADATA_TYPE_PICTURE) {
				FLAC__metadata_iterator_delete_block(it, /*replace_with_padding=*/true);
				removed = true;
				break;
			}
		} while (FLAC__metadata_iterator_next(it));
		if (!removed)
			break;
	}

	// VORBIS_COMMENT の直後、無ければ STREAMINFO の直後へ挿入
	FLAC__metadata_iterator_init(it, chain);
	do {
		if (FLAC__metadata_iterator_get_block_type(it) == FLAC__METADATA_TYPE_VORBIS_COMMENT)
			break;
	} while (FLAC__metadata_iterator_next(it));
	if (FLAC__metadata_iterator_get_block_type(it) != FLAC__METADATA_TYPE_VORBIS_COMMENT)
		FLAC__metadata_iterator_init(it, chain);

	bool ok = false;
	FLAC__StreamMetadata* pic = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PICTURE);
	if (pic) {
		char mimeBuf[64];
		CoverSetMime(mimeBuf, (int)sizeof(mimeBuf), (mime && *mime) ? mime : "image/jpeg");
		char descBuf[1] = { 0 };
		pic->data.picture.type = FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER;
		ok = FLAC__metadata_object_picture_set_mime_type(pic, mimeBuf, true) != 0;
		if (ok)
			ok = FLAC__metadata_object_picture_set_description(pic, (FLAC__byte*)descBuf, true) != 0;
		if (ok)
			ok = FLAC__metadata_object_picture_set_data(pic, (FLAC__byte*)data, (FLAC__uint32)dataLen, true) != 0;
		if (ok)
			ok = FLAC__metadata_iterator_insert_block_after(it, pic) != 0;
		if (!ok)
			FLAC__metadata_object_delete(pic);
	}
	if (ok) {
		FLAC__metadata_chain_sort_padding(chain);
		ok = FLAC__metadata_chain_write(chain, /*use_padding=*/true, /*preserve_file_stats=*/true) != 0;
	}
	FLAC__metadata_iterator_delete(it);
	FLAC__metadata_chain_delete(chain);
	return ok;
}

// WAV は末尾に 'id3 ' チャンク(APIC だけの ID3v2.3)を足す。
// 全読み込みはしないので RF64 / 2GB超でも動く。
bool EmbedCoverWav(LPCTSTR path, const BYTE* data, int dataLen, const char* mime)
{
	char mimeBuf[64];
	CoverSetMime(mimeBuf, (int)sizeof(mimeBuf), (mime && *mime) ? mime : "image/jpeg");
	const int mimeLen = (int)strlen(mimeBuf);
	const int bodyLen = 1 + mimeLen + 1 + 1 + 1 + dataLen; // enc + mime + NUL + type + desc + 画像
	const int tagLen = 10 + 10 + bodyLen;                  // ID3ヘッダ + APICヘッダ + 本体
	if (tagLen <= 0 || tagLen > 0x0FFFFFFF)
		return false;

	CFile f;
	if (!f.Open(path, CFile::modeReadWrite | CFile::shareExclusive, NULL))
		return false;
	bool ok = true;
	try {
		WavChunkLayout lay;
		if (!WavScanChunks(f, lay)) {
			f.Close();
			return false;
		}
		// 既存の 'id3 ' は RIFF の詰め物 'JUNK' へ潰し、末尾に付け直す
		if (lay.id3Offset != 0) {
			f.Seek((LONGLONG)lay.id3Offset, CFile::begin);
			f.Write("JUNK", 4);
		}
		ULONGLONG end = lay.endOffset;
		if (end < 12ULL) {
			f.Close();
			return false;
		}
		f.SetLength(end);
		f.SeekToEnd();
		if (end & 1ULL) {
			const BYTE pad = 0;
			f.Write(&pad, 1);
		}
		BYTE chunk[8];
		memcpy(chunk, "id3 ", 4);
		CoverPutLe32(chunk + 4, (DWORD)tagLen);
		f.Write(chunk, 8);

		BYTE id3[10] = { 'I', 'D', '3', 0x03, 0x00, 0x00, 0, 0, 0, 0 };
		CoverPutSyncSafe32(id3 + 6, (DWORD)(tagLen - 10));
		f.Write(id3, 10);

		BYTE frame[10];
		memcpy(frame, "APIC", 4);
		CoverPutBe32(frame + 4, (DWORD)bodyLen);
		frame[8] = 0;
		frame[9] = 0;
		f.Write(frame, 10);

		BYTE pre[80];
		int n = 0;
		pre[n++] = 0;                      // encoding = ISO-8859-1
		memcpy(pre + n, mimeBuf, mimeLen);
		n += mimeLen;
		pre[n++] = 0;                      // mime 終端
		pre[n++] = 3;                      // front cover
		pre[n++] = 0;                      // description(空)
		f.Write(pre, n);
		f.Write(data, (UINT)dataLen);
		if (tagLen & 1) {
			const BYTE pad = 0;
			f.Write(&pad, 1);
		}

		const ULONGLONG newLen = f.GetLength();
		if (lay.rf64) {
			// RF64 は riffSize を ds64 が持つ(先頭は 0xFFFFFFFF のまま)
			if (lay.ds64Offset != 0) {
				const __int64 riffSize = (__int64)newLen - 8;
				f.Seek((LONGLONG)(lay.ds64Offset + 8ULL), CFile::begin);
				f.Write(&riffSize, 8);
			}
			else {
				ok = false;
			}
		}
		else {
			BYTE sz[4];
			CoverPutLe32(sz, (DWORD)(newLen - 8ULL));
			f.Seek(4, CFile::begin);
			f.Write(sz, 4);
		}
	}
	catch (CException* e) {
		e->Delete();
		ok = false;
	}
	f.Close();
	return ok;
}

} // namespace

int ExtractCoverArt(LPCTSTR path, BYTE* buf, int bufCap, char* mimeOut, int mimeCap)
{
	if (mimeOut && mimeCap > 0)
		mimeOut[0] = 0;
	if (!path || !*path || !buf || bufCap <= 0)
		return 0;
	if (bufCap > FILETAG_COVER_MAX)
		bufCap = FILETAG_COVER_MAX;

	CString ext = path;
	int slash = ext.ReverseFind('\\');
	if (slash < 0)
		slash = ext.ReverseFind('/');
	if (slash >= 0)
		ext = ext.Mid(slash + 1);
	ext.MakeLower();

	int n = 0;
	if (IsExt(ext, _T(".flac")) || IsExt(ext, _T(".qull3h"))) {
		n = CoverFromFlacFile(path, buf, bufCap, mimeOut, mimeCap);
		if (n == 0)
			n = CoverFromFlacRawScan(path, buf, bufCap, mimeOut, mimeCap);
	}
	else if (IsExt(ext, _T(".ogg")) || IsExt(ext, _T(".qull3")))
		n = CoverFromOggVorbis(path, buf, bufCap, mimeOut, mimeCap);
	else if (IsExt(ext, _T(".opus")))
		n = CoverFromOpus(path, buf, bufCap, mimeOut, mimeCap);
	else if (IsExt(ext, _T(".m4a")) || IsExt(ext, _T(".aac")) || IsExt(ext, _T(".mp4")))
		n = CoverFromM4a(path, buf, bufCap, mimeOut, mimeCap);
	else if (IsExt(ext, _T(".wav")))
		n = CoverFromWav(path, buf, bufCap, mimeOut, mimeCap);
	else if (IsExt(ext, _T(".dsf")))
		n = CoverFromDsf(path, buf, bufCap, mimeOut, mimeCap);

	// mp3 / dff / wsd / tta / ape / その他は ID3v2 を総当り
	if (n == 0)
		n = CoverFromId3v2Anywhere(path, buf, bufCap, mimeOut, mimeCap);
	if (n == 0)
		n = CoverFromSidecar(path, buf, bufCap, mimeOut, mimeCap);
	if (n <= 0 && mimeOut && mimeCap > 0)
		mimeOut[0] = 0;
	return (n > 0) ? n : 0;
}

bool EmbedCoverArt(LPCTSTR path, const BYTE* data, int dataLen, const char* mime)
{
	if (!path || !*path || !data || dataLen <= 0 || dataLen > FILETAG_COVER_MAX)
		return false;

	CString ext = path;
	int slash = ext.ReverseFind('\\');
	if (slash < 0)
		slash = ext.ReverseFind('/');
	if (slash >= 0)
		ext = ext.Mid(slash + 1);
	ext.MakeLower();

	if (IsExt(ext, _T(".mp3")) || IsExt(ext, _T(".mp2")) || IsExt(ext, _T(".mp1")))
		return EmbedCoverMp3(path, data, dataLen, mime);
	if (IsExt(ext, _T(".flac")))
		return EmbedCoverFlac(path, data, dataLen, mime);
	if (IsExt(ext, _T(".wav")))
		return EmbedCoverWav(path, data, dataLen, mime);
	if (IsExt(ext, _T(".dsf")))
		return FileTag_EmbedCoverDsf(path, data, dataLen, mime);
	return false;
}

bool CopyTagsAndCoverToExport(LPCTSTR srcPath, LPCTSTR dstPath, int copyCover)
{
	if (!srcPath || !*srcPath || !dstPath || !*dstPath)
		return false;

	FileTagFields fields;
	ReadFileTagFields(srcPath, fields);
	WriteFileTagFields(dstPath, fields);

	if (copyCover) {
		BYTE* cover = (BYTE*)malloc(FILETAG_COVER_MAX);
		if (cover) {
			char mime[64] = {};
			const int n = ExtractCoverArt(srcPath, cover, FILETAG_COVER_MAX, mime, (int)sizeof(mime));
			if (n > 0)
				EmbedCoverArt(dstPath, cover, n, mime);
			free(cover);
		}
	}
	return true;
}

static bool EmbedCoverFromImageFile(LPCTSTR dstPath, LPCTSTR imagePath)
{
	if (!dstPath || !*dstPath || !imagePath || !*imagePath)
		return false;
	CString ext = imagePath;
	ext.MakeLower();
	const char* mime = nullptr;
	if (ext.GetLength() >= 4 && ext.Right(4) == _T(".png"))
		mime = "image/png";
	else if ((ext.GetLength() >= 5 && ext.Right(5) == _T(".jpeg"))
		|| (ext.GetLength() >= 4 && ext.Right(4) == _T(".jpg")))
		mime = "image/jpeg";
	else
		return false;

	CFile f;
	if (!f.Open(imagePath, CFile::modeRead | CFile::shareDenyWrite))
		return false;
	const ULONGLONG len64 = f.GetLength();
	if (len64 == 0 || len64 > (ULONGLONG)FILETAG_COVER_MAX) {
		f.Close();
		return false;
	}
	const int len = (int)len64;
	BYTE* buf = (BYTE*)malloc((size_t)len);
	if (!buf) {
		f.Close();
		return false;
	}
	const UINT got = f.Read(buf, (UINT)len);
	f.Close();
	bool ok = false;
	if ((int)got == len)
		ok = EmbedCoverArt(dstPath, buf, len, mime);
	free(buf);
	return ok;
}

bool ApplyExportTagsAndCover(LPCTSTR srcPath, LPCTSTR dstPath, int copyTags,
	const FileTagFields* fillIfEmpty, LPCTSTR coverImagePath)
{
	if (!dstPath || !*dstPath)
		return false;

	FileTagFields fields;
	if (copyTags && srcPath && *srcPath)
		ReadFileTagFields(srcPath, fields);

	if (fillIfEmpty) {
		// ユーザ入力は空でなければ上書き（既存タグも変更可）
		if (!fillIfEmpty->title.IsEmpty())
			fields.title = fillIfEmpty->title;
		if (!fillIfEmpty->artist.IsEmpty())
			fields.artist = fillIfEmpty->artist;
		if (!fillIfEmpty->album.IsEmpty())
			fields.album = fillIfEmpty->album;
	}

	const bool hasText = fields.HasTitleArtistAlbum() || fields.HasAnyTagField();
	if (hasText)
		WriteFileTagFields(dstPath, fields);

	bool coverOk = true;
	if (coverImagePath && *coverImagePath) {
		coverOk = EmbedCoverFromImageFile(dstPath, coverImagePath);
	}
	else if (copyTags && srcPath && *srcPath) {
		BYTE* cover = (BYTE*)malloc(FILETAG_COVER_MAX);
		if (cover) {
			char mime[64] = {};
			const int n = ExtractCoverArt(srcPath, cover, FILETAG_COVER_MAX, mime, (int)sizeof(mime));
			if (n > 0)
				coverOk = EmbedCoverArt(dstPath, cover, n, mime);
			free(cover);
		}
	}
	return hasText || coverOk || (!copyTags && !(coverImagePath && *coverImagePath));
}
