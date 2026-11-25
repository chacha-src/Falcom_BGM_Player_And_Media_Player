/* oxz のタグ編集と wav/flac/mp3 変換。通常の Win32 コントロール。ogg の CCustom 画面とは別。 */
#define FLAC__NO_DLL
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "flac/metadata.h"
#include "flac/stream_encoder.h"
#include "flac/stream_decoder.h"
#include "OxzLang.h"
#include "OxzAudio.h"

static const wchar_t* L2(const wchar_t* ja, const wchar_t* en)
{
	return OxzLang() == 0 ? ja : en;
}

static std::string W8(const std::wstring& w)
{
	if (w.empty()) return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
	if (n <= 0) return std::string();
	std::string s((size_t)n, 0);
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
	return s;
}

static std::wstring U8W(const char* s, int n)
{
	if (!s || n == 0) return L"";
	if (n < 0) n = (int)strlen(s);
	while (n > 0 && s[n - 1] == 0) n--;
	if (n <= 0) return L"";
	int c = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, n, NULL, 0);
	UINT cp = CP_UTF8;
	DWORD fl = MB_ERR_INVALID_CHARS;
	if (c <= 0) { cp = CP_ACP; fl = 0; c = MultiByteToWideChar(cp, fl, s, n, NULL, 0); }
	if (c <= 0) return L"";
	std::wstring w((size_t)c, 0);
	MultiByteToWideChar(cp, fl, s, n, &w[0], c);
	return w;
}

static std::string PathA(const std::wstring& path)
{
	wchar_t shortp[MAX_PATH];
	DWORD sn = GetShortPathNameW(path.c_str(), shortp, MAX_PATH);
	const wchar_t* use = (sn > 0 && sn < MAX_PATH) ? shortp : path.c_str();
	int n = WideCharToMultiByte(CP_ACP, 0, use, -1, NULL, 0, NULL, NULL);
	if (n <= 0) return std::string();
	std::string a((size_t)n, 0);
	WideCharToMultiByte(CP_ACP, 0, use, -1, &a[0], n, NULL, NULL);
	if (!a.empty() && a.back() == 0) a.pop_back();
	return a;
}

static int ExtKind(const std::wstring& path)
{
	size_t n = path.size();
	if (n >= 5 && _wcsicmp(path.c_str() + n - 5, L".flac") == 0) return 2;
	if (n >= 4 && _wcsicmp(path.c_str() + n - 4, L".wav") == 0) return 1;
	if (n >= 4 && _wcsicmp(path.c_str() + n - 4, L".mp3") == 0) return 3;
	return 0;
}

struct OxzTag {
	std::wstring title, artist, album, albumArtist, year, track, disc, genre, comment;
	std::wstring composer, lyricist, copyright, encoder, bpm, lyrics;
	std::wstring loop1, loop2;
	std::vector<unsigned char> cover;
	std::string mime;
	bool dropCover;
	OxzTag() : dropCover(false) {}
};

static void PutIf(std::wstring& dest, const std::wstring& src)
{
	if (dest.empty() && !src.empty()) dest = src;
}

static std::wstring UpKey(std::string k)
{
	for (size_t i = 0; i < k.size(); ++i)
		if (k[i] >= 'a' && k[i] <= 'z') k[i] = (char)(k[i] - 32);
	return U8W(k.c_str(), (int)k.size());
}

static bool ReadFlac(const std::wstring& path, OxzTag& t, bool withPic)
{
	FILE* fp = NULL;
	if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) return false;
	unsigned char b = 0;
	if (fread(&b, 1, 1, fp) != 1) { fclose(fp); return false; }
	fclose(fp);
	if (b == 0xBF || b != 'f') return false;
	std::string pa = PathA(path);
	if (pa.empty()) return false;
	FLAC__Metadata_Chain* chain = FLAC__metadata_chain_new();
	if (!chain) return false;
	if (!FLAC__metadata_chain_read(chain, pa.c_str())) {
		FLAC__metadata_chain_delete(chain);
		return false;
	}
	FLAC__Metadata_Iterator* it = FLAC__metadata_iterator_new();
	if (!it) { FLAC__metadata_chain_delete(chain); return false; }
	FLAC__metadata_iterator_init(it, chain);
	do {
		FLAC__StreamMetadata* b2 = FLAC__metadata_iterator_get_block(it);
		if (!b2) continue;
		if (b2->type == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
			FLAC__StreamMetadata_VorbisComment* vc = &b2->data.vorbis_comment;
			for (unsigned i = 0; i < vc->num_comments; ++i) {
				const char* e = (const char*)vc->comments[i].entry;
				int len = (int)vc->comments[i].length;
				int eq = -1;
				for (int k = 0; k < len; ++k) if (e[k] == '=') { eq = k; break; }
				if (eq <= 0) continue;
				std::wstring key = UpKey(std::string(e, e + eq));
				std::wstring val = U8W(e + eq + 1, len - eq - 1);
				if (key == L"TITLE") PutIf(t.title, val);
				else if (key == L"ARTIST") PutIf(t.artist, val);
				else if (key == L"ALBUM") PutIf(t.album, val);
				else if (key == L"ALBUMARTIST" || key == L"ALBUM ARTIST") { PutIf(t.albumArtist, val); PutIf(t.artist, val); }
				else if (key == L"DATE" || key == L"YEAR") PutIf(t.year, val);
				else if (key == L"TRACKNUMBER" || key == L"TRACK") PutIf(t.track, val);
				else if (key == L"DISCNUMBER" || key == L"DISC") PutIf(t.disc, val);
				else if (key == L"GENRE") PutIf(t.genre, val);
				else if (key == L"COMMENT" || key == L"DESCRIPTION") PutIf(t.comment, val);
				else if (key == L"COMPOSER") PutIf(t.composer, val);
				else if (key == L"LYRICIST") PutIf(t.lyricist, val);
				else if (key == L"COPYRIGHT") PutIf(t.copyright, val);
				else if (key == L"ENCODED-BY" || key == L"ENCODER") PutIf(t.encoder, val);
				else if (key == L"BPM") PutIf(t.bpm, val);
				else if (key == L"LYRICS" || key == L"UNSYNCEDLYRICS") PutIf(t.lyrics, val);
				else if (key == L"LOOPSTART") PutIf(t.loop1, val);
				else if (key == L"LOOPEND") PutIf(t.loop2, val);
			}
		}
		else if (withPic && t.cover.empty() && b2->type == FLAC__METADATA_TYPE_PICTURE) {
			FLAC__StreamMetadata_Picture* pic = &b2->data.picture;
			if (pic->data && pic->data_length > 0 && pic->data_length < 8 * 1024 * 1024) {
				t.cover.assign(pic->data, pic->data + pic->data_length);
				if (pic->mime_type) t.mime = pic->mime_type;
			}
		}
	} while (FLAC__metadata_iterator_next(it));
	FLAC__metadata_iterator_delete(it);
	FLAC__metadata_chain_delete(chain);
	return true;
}

static bool FlacSet(FLAC__StreamMetadata* vc, const char* key, const std::wstring& val, bool clearEmpty)
{
	if (!vc || !key) return false;
	if (val.empty()) {
		if (clearEmpty) FLAC__metadata_object_vorbiscomment_remove_entries_matching(vc, key);
		return true;
	}
	std::string u8 = W8(val);
	FLAC__StreamMetadata_VorbisComment_Entry entry;
	if (!FLAC__metadata_object_vorbiscomment_entry_from_name_value_pair(&entry, key, u8.c_str()))
		return false;
	return FLAC__metadata_object_vorbiscomment_replace_comment(vc, entry, true, false) != 0;
}

static bool WriteFlac(const std::wstring& path, const OxzTag& t, bool clearEmpty)
{
	std::string pa = PathA(path);
	if (pa.empty()) return false;
	FLAC__Metadata_Chain* chain = FLAC__metadata_chain_new();
	if (!chain) return false;
	if (!FLAC__metadata_chain_read(chain, pa.c_str())) {
		FLAC__metadata_chain_delete(chain);
		return false;
	}
	FLAC__Metadata_Iterator* it = FLAC__metadata_iterator_new();
	if (!it) { FLAC__metadata_chain_delete(chain); return false; }
	FLAC__metadata_iterator_init(it, chain);
	FLAC__StreamMetadata* vc = NULL;
	do {
		if (FLAC__metadata_iterator_get_block_type(it) == FLAC__METADATA_TYPE_VORBIS_COMMENT) {
			vc = FLAC__metadata_iterator_get_block(it);
			break;
		}
	} while (FLAC__metadata_iterator_next(it));
	if (!vc) {
		FLAC__metadata_iterator_init(it, chain);
		vc = FLAC__metadata_object_new(FLAC__METADATA_TYPE_VORBIS_COMMENT);
		if (!vc || !FLAC__metadata_iterator_insert_block_after(it, vc)) {
			if (vc) FLAC__metadata_object_delete(vc);
			FLAC__metadata_iterator_delete(it);
			FLAC__metadata_chain_delete(chain);
			return false;
		}
		vc = FLAC__metadata_iterator_get_block(it);
	}
	bool ok = vc != NULL;
	ok = FlacSet(vc, "TITLE", t.title, clearEmpty) && ok;
	ok = FlacSet(vc, "ARTIST", t.artist, clearEmpty) && ok;
	ok = FlacSet(vc, "ALBUM", t.album, clearEmpty) && ok;
	ok = FlacSet(vc, "ALBUMARTIST", t.albumArtist, clearEmpty) && ok;
	ok = FlacSet(vc, "DATE", t.year, clearEmpty) && ok;
	ok = FlacSet(vc, "TRACKNUMBER", t.track, clearEmpty) && ok;
	ok = FlacSet(vc, "DISCNUMBER", t.disc, clearEmpty) && ok;
	ok = FlacSet(vc, "GENRE", t.genre, clearEmpty) && ok;
	ok = FlacSet(vc, "COMMENT", t.comment, clearEmpty) && ok;
	ok = FlacSet(vc, "COMPOSER", t.composer, clearEmpty) && ok;
	ok = FlacSet(vc, "LYRICIST", t.lyricist, clearEmpty) && ok;
	ok = FlacSet(vc, "COPYRIGHT", t.copyright, clearEmpty) && ok;
	ok = FlacSet(vc, "ENCODED-BY", t.encoder, clearEmpty) && ok;
	ok = FlacSet(vc, "BPM", t.bpm, clearEmpty) && ok;
	ok = FlacSet(vc, "LYRICS", t.lyrics, clearEmpty) && ok;
	if (!t.loop1.empty()) ok = FlacSet(vc, "LOOPSTART", t.loop1, false) && ok;
	if (!t.loop2.empty()) ok = FlacSet(vc, "LOOPEND", t.loop2, false) && ok;
		if (t.dropCover || !t.cover.empty()) {
		FLAC__metadata_iterator_init(it, chain);
		for (;;) {
			if (FLAC__metadata_iterator_get_block_type(it) == FLAC__METADATA_TYPE_PICTURE) {
				if (!FLAC__metadata_iterator_delete_block(it, false)) break;
			}
			else if (!FLAC__metadata_iterator_next(it)) break;
		}
		if (!t.cover.empty()) {
			FLAC__StreamMetadata* pic = FLAC__metadata_object_new(FLAC__METADATA_TYPE_PICTURE);
			if (pic) {
				pic->data.picture.type = FLAC__STREAM_METADATA_PICTURE_TYPE_FRONT_COVER;
				const char* mime = t.mime.empty() ? "image/jpeg" : t.mime.c_str();
				FLAC__metadata_object_picture_set_mime_type(pic, (char*)mime, true);
				FLAC__metadata_object_picture_set_data(pic, (FLAC__byte*)t.cover.data(), (FLAC__uint32)t.cover.size(), true);
				FLAC__metadata_iterator_init(it, chain);
				FLAC__metadata_iterator_insert_block_after(it, pic);
			}
		}
	}
	FLAC__bool written = ok && FLAC__metadata_chain_write(chain, true, false);
	FLAC__metadata_iterator_delete(it);
	FLAC__metadata_chain_delete(chain);
	return written != 0;
}

static unsigned RdLe(const unsigned char* p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static void WrLe(std::vector<unsigned char>& o, unsigned n)
{
	o.push_back((unsigned char)n);
	o.push_back((unsigned char)(n >> 8));
	o.push_back((unsigned char)(n >> 16));
	o.push_back((unsigned char)(n >> 24));
}

static void WavChunk(std::vector<unsigned char>& info, const char* id, const std::wstring& text)
{
	if (text.empty()) return;
	std::string u8 = W8(text);
	info.push_back((unsigned char)id[0]);
	info.push_back((unsigned char)id[1]);
	info.push_back((unsigned char)id[2]);
	info.push_back((unsigned char)id[3]);
	unsigned size = (unsigned)u8.size() + 1;
	WrLe(info, size);
	info.insert(info.end(), u8.begin(), u8.end());
	info.push_back(0);
	if (size & 1) info.push_back(0);
}

static bool LoadFile(const std::wstring& path, std::vector<unsigned char>& file, unsigned cap)
{
	FILE* fp = NULL;
	if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) return false;
	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
	long n = ftell(fp);
	if (n < 12 || (unsigned)n > cap) { fclose(fp); return false; }
	fseek(fp, 0, SEEK_SET);
	file.resize((size_t)n);
	size_t got = fread(file.data(), 1, (size_t)n, fp);
	fclose(fp);
	return got == (size_t)n;
}

static bool ReadWav(const std::vector<unsigned char>& file, OxzTag& t)
{
	if (file.size() < 12 || memcmp(file.data(), "RIFF", 4) != 0 || memcmp(file.data() + 8, "WAVE", 4) != 0)
		return false;
	size_t pos = 12;
	while (pos + 8 <= file.size()) {
		unsigned id = RdLe(file.data() + pos);
		unsigned sz = RdLe(file.data() + pos + 4);
		unsigned pad = (sz + 1u) & ~1u;
		size_t next = pos + 8 + pad;
		if (next > file.size()) break;
		if (id == 0x5453494C && sz >= 4 && RdLe(file.data() + pos + 8) == 0x4F464E49) {
			size_t s = pos + 12;
			size_t end = pos + 8 + sz;
			while (s + 8 <= end && s + 8 <= file.size()) {
				unsigned sid = RdLe(file.data() + s);
				unsigned ssz = RdLe(file.data() + s + 4);
				if (s + 8 + ssz > file.size()) break;
				std::wstring val = U8W((const char*)file.data() + s + 8, (int)ssz);
				if (sid == 0x4D414E49) PutIf(t.title, val);
				else if (sid == 0x54524149) PutIf(t.artist, val);
				else if (sid == 0x44525049) PutIf(t.album, val);
				else if (sid == 0x44524349) PutIf(t.year, val);
				else if (sid == 0x524E4749) PutIf(t.genre, val);
				else if (sid == 0x4B525449) PutIf(t.track, val);
				else if (sid == 0x544D4349) PutIf(t.comment, val);
				else if (sid == 0x53554D49) PutIf(t.composer, val);
				else if (sid == 0x504F4349) PutIf(t.copyright, val);
				else if (sid == 0x54465349) PutIf(t.encoder, val);
				else if (sid == 0x52414149) PutIf(t.albumArtist, val);
				else if (sid == 0x594C4149) PutIf(t.lyricist, val);
				else if (sid == 0x4D504249) PutIf(t.bpm, val);
				else if (sid == 0x43534449) PutIf(t.disc, val);
				else if (sid == 0x52594C49) PutIf(t.lyrics, val);
				s += 8 + ((ssz + 1u) & ~1u);
			}
		}
		pos = next;
	}
	return true;
}

static bool WriteWav(const std::wstring& path, const OxzTag& t)
{
	std::vector<unsigned char> file;
	if (!LoadFile(path, file, 512u * 1024u * 1024u)) return false;
	if (file.size() < 12 || memcmp(file.data(), "RIFF", 4) != 0 || memcmp(file.data() + 8, "WAVE", 4) != 0)
		return false;
	std::vector<unsigned char> out;
	out.insert(out.end(), file.begin(), file.begin() + 12);
	size_t pos = 12;
	while (pos + 8 <= file.size()) {
		unsigned id = RdLe(file.data() + pos);
		unsigned sz = RdLe(file.data() + pos + 4);
		unsigned pad = (sz + 1u) & ~1u;
		size_t next = pos + 8 + pad;
		if (next > file.size()) break;
		bool skip = false;
		if (id == 0x5453494C && sz >= 4 && RdLe(file.data() + pos + 8) == 0x4F464E49)
			skip = true;
		if (!skip) out.insert(out.end(), file.begin() + pos, file.begin() + next);
		pos = next;
	}
	std::vector<unsigned char> info;
	info.push_back('I'); info.push_back('N'); info.push_back('F'); info.push_back('O');
	WavChunk(info, "INAM", t.title);
	WavChunk(info, "IART", t.artist);
	WavChunk(info, "IPRD", t.album);
	WavChunk(info, "ICRD", t.year);
	WavChunk(info, "IGNR", t.genre);
	WavChunk(info, "ITRK", t.track);
	WavChunk(info, "ICMT", t.comment);
	WavChunk(info, "IMUS", t.composer);
	WavChunk(info, "ICOP", t.copyright);
	WavChunk(info, "ISFT", t.encoder);
	WavChunk(info, "IAAR", t.albumArtist);
	WavChunk(info, "IALY", t.lyricist);
	WavChunk(info, "IBPM", t.bpm);
	WavChunk(info, "IDSC", t.disc);
	WavChunk(info, "ILYR", t.lyrics);
	out.push_back('L'); out.push_back('I'); out.push_back('S'); out.push_back('T');
	WrLe(out, (unsigned)info.size());
	out.insert(out.end(), info.begin(), info.end());
	if (info.size() & 1) out.push_back(0);
	unsigned riff = (unsigned)out.size() - 8;
	out[4] = (unsigned char)riff; out[5] = (unsigned char)(riff >> 8);
	out[6] = (unsigned char)(riff >> 16); out[7] = (unsigned char)(riff >> 24);
	std::wstring tmp = path + L".oztmp";
	FILE* fp = NULL;
	if (_wfopen_s(&fp, tmp.c_str(), L"wb") != 0 || !fp) return false;
	size_t wr = fwrite(out.data(), 1, out.size(), fp);
	fclose(fp);
	if (wr != out.size()) { DeleteFileW(tmp.c_str()); return false; }
	if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileW(tmp.c_str());
		return false;
	}
	return true;
}

static unsigned Sync(const unsigned char* p)
{
	return ((p[0] & 0x7f) << 21) | ((p[1] & 0x7f) << 14) | ((p[2] & 0x7f) << 7) | (p[3] & 0x7f);
}

static unsigned Be(const unsigned char* p)
{
	return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

static std::wstring Id3Utf16(const unsigned char* s, int sn, bool be)
{
	int i = 0;
	if (sn >= 2 && ((s[0] == 0xff && s[1] == 0xfe) || (s[0] == 0xfe && s[1] == 0xff))) {
		be = s[0] == 0xfe;
		i = 2;
	}
	std::wstring o;
	for (; i + 1 < sn; i += 2) {
		wchar_t c = be ? (wchar_t)((s[i] << 8) | s[i + 1]) : (wchar_t)(s[i] | (s[i + 1] << 8));
		if (!c) break;
		o.push_back(c);
	}
	return o;
}

static std::wstring Id3Text(const unsigned char* p, int n)
{
	if (n < 1) return L"";
	int enc = p[0];
	const unsigned char* s = p + 1;
	int sn = n - 1;
	if (enc == 0 || enc == 3) return U8W((const char*)s, sn);
	return Id3Utf16(s, sn, enc == 2);
}

static void SplitTerm(const unsigned char* s, int sn, int enc, std::wstring& desc, std::wstring& body)
{
	if (enc == 0 || enc == 3) {
		int i = 0;
		while (i < sn && s[i]) i++;
		desc = U8W((const char*)s, i);
		if (i < sn) i++;
		body = U8W((const char*)s + i, sn - i);
		return;
	}
	bool be = enc == 2;
	int i = 0;
	if (sn >= 2 && ((s[0] == 0xff && s[1] == 0xfe) || (s[0] == 0xfe && s[1] == 0xff))) {
		be = s[0] == 0xfe;
		i = 2;
	}
	for (; i + 1 < sn; i += 2) {
		wchar_t c = be ? (wchar_t)((s[i] << 8) | s[i + 1]) : (wchar_t)(s[i] | (s[i + 1] << 8));
		if (!c) { i += 2; break; }
		desc.push_back(c);
	}
	body = Id3Utf16(s + i, sn - i, be);
}

struct Id3Fr { char id[4]; std::vector<unsigned char> data; };

static void ParseId3(const std::vector<unsigned char>& file, std::vector<Id3Fr>& frames, size_t& audioAt)
{
	audioAt = 0;
	if (file.size() < 10 || memcmp(file.data(), "ID3", 3) != 0) return;
	int ver = file[3];
	unsigned tagSize = Sync(file.data() + 6);
	size_t tagEnd = 10 + (size_t)tagSize;
	if (file[5] & 0x10) tagEnd += 10;
	if (tagEnd > file.size()) tagEnd = file.size();
	audioAt = tagEnd;
	if (file[5] & 0x80) return;
	size_t p = 10;
	if ((file[5] & 0x40) && p + 4 <= tagEnd) {
		unsigned ext = (ver >= 4) ? Sync(file.data() + p) : Be(file.data() + p);
		p += 4 + ext;
	}
	while (p + 10 <= tagEnd && p + 10 <= file.size()) {
		if (file[p] == 0) break;
		Id3Fr fr;
		memcpy(fr.id, file.data() + p, 4);
		unsigned sz = (ver >= 4) ? Sync(file.data() + p + 4) : Be(file.data() + p + 4);
		p += 10;
		if (p + sz > file.size() || p + sz > tagEnd) break;
		fr.data.assign(file.begin() + p, file.begin() + p + sz);
		frames.push_back(fr);
		p += sz;
	}
}

static void ApplyText(OxzTag& t, const char* id, const std::wstring& v)
{
	if (!memcmp(id, "TIT2", 4)) PutIf(t.title, v);
	else if (!memcmp(id, "TPE1", 4)) PutIf(t.artist, v);
	else if (!memcmp(id, "TALB", 4)) PutIf(t.album, v);
	else if (!memcmp(id, "TPE2", 4)) PutIf(t.albumArtist, v);
	else if (!memcmp(id, "TYER", 4) || !memcmp(id, "TDRC", 4)) PutIf(t.year, v);
	else if (!memcmp(id, "TRCK", 4)) PutIf(t.track, v);
	else if (!memcmp(id, "TPOS", 4)) PutIf(t.disc, v);
	else if (!memcmp(id, "TCON", 4)) PutIf(t.genre, v);
	else if (!memcmp(id, "TCOM", 4)) PutIf(t.composer, v);
	else if (!memcmp(id, "TEXT", 4)) PutIf(t.lyricist, v);
	else if (!memcmp(id, "TCOP", 4)) PutIf(t.copyright, v);
	else if (!memcmp(id, "TENC", 4)) PutIf(t.encoder, v);
	else if (!memcmp(id, "TBPM", 4)) PutIf(t.bpm, v);
}

static bool ReadMp3(const std::vector<unsigned char>& file, OxzTag& t, bool withPic)
{
	std::vector<Id3Fr> frames;
	size_t audio = 0;
	ParseId3(file, frames, audio);
	for (size_t i = 0; i < frames.size(); ++i) {
		const Id3Fr& fr = frames[i];
		if (fr.id[0] == 'T') ApplyText(t, fr.id, Id3Text(fr.data.data(), (int)fr.data.size()));
		else if (!memcmp(fr.id, "COMM", 4) && fr.data.size() >= 5) {
			std::wstring desc, body;
			SplitTerm(fr.data.data() + 4, (int)fr.data.size() - 4, fr.data[0], desc, body);
			if (desc.size() >= 4 && desc.compare(0, 4, L"iTun") == 0) continue;
			if (desc == L"LYRICS") PutIf(t.lyrics, body);
			else PutIf(t.comment, body);
		}
		else if (!memcmp(fr.id, "USLT", 4) && fr.data.size() >= 5) {
			std::wstring desc, body;
			SplitTerm(fr.data.data() + 4, (int)fr.data.size() - 4, fr.data[0], desc, body);
			PutIf(t.lyrics, body);
		}
		else if (withPic && t.cover.empty() && !memcmp(fr.id, "APIC", 4) && fr.data.size() > 4) {
			int enc = fr.data[0];
			size_t p = 1;
			while (p < fr.data.size() && fr.data[p]) p++;
			if (p >= fr.data.size()) continue;
			t.mime.assign((const char*)fr.data.data() + 1, (const char*)fr.data.data() + p);
			p++;
			if (p >= fr.data.size()) continue;
			p++;
			if (enc == 0 || enc == 3) { while (p < fr.data.size() && fr.data[p]) p++; if (p < fr.data.size()) p++; }
			else {
				if (p + 1 < fr.data.size() && (fr.data[p] == 0xff || fr.data[p] == 0xfe)) p += 2;
				while (p + 1 < fr.data.size() && (fr.data[p] || fr.data[p + 1])) p += 2;
				if (p + 1 < fr.data.size()) p += 2;
			}
			if (p < fr.data.size() && fr.data.size() - p < 8 * 1024 * 1024)
				t.cover.assign(fr.data.begin() + p, fr.data.end());
		}
	}
	return true;
}

static void PushBe(std::vector<unsigned char>& o, unsigned n)
{
	o.push_back((unsigned char)(n >> 24));
	o.push_back((unsigned char)(n >> 16));
	o.push_back((unsigned char)(n >> 8));
	o.push_back((unsigned char)n);
}

static void PushSync(std::vector<unsigned char>& o, unsigned n)
{
	o.push_back((unsigned char)((n >> 21) & 0x7f));
	o.push_back((unsigned char)((n >> 14) & 0x7f));
	o.push_back((unsigned char)((n >> 7) & 0x7f));
	o.push_back((unsigned char)(n & 0x7f));
}

static void AddUtf16(std::vector<unsigned char>& d, const std::wstring& s)
{
	d.push_back(0xff); d.push_back(0xfe);
	for (size_t i = 0; i < s.size(); ++i) {
		d.push_back((unsigned char)(s[i] & 0xff));
		d.push_back((unsigned char)((s[i] >> 8) & 0xff));
	}
}

static void AddTextFr(std::vector<Id3Fr>& frames, const char* id, const std::wstring& s)
{
	if (s.empty()) return;
	Id3Fr fr;
	memcpy(fr.id, id, 4);
	fr.data.push_back(1);
	AddUtf16(fr.data, s);
	frames.push_back(fr);
}

static void AddComm(std::vector<Id3Fr>& frames, const char* id, const std::wstring& s)
{
	if (s.empty()) return;
	Id3Fr fr;
	memcpy(fr.id, id, 4);
	fr.data.push_back(1);
	fr.data.push_back('e'); fr.data.push_back('n'); fr.data.push_back('g');
	fr.data.push_back(0xff); fr.data.push_back(0xfe);
	fr.data.push_back(0); fr.data.push_back(0);
	AddUtf16(fr.data, s);
	frames.push_back(fr);
}

static void AddApic(std::vector<Id3Fr>& frames, const OxzTag& t)
{
	if (t.cover.empty()) return;
	Id3Fr fr;
	memcpy(fr.id, "APIC", 4);
	fr.data.push_back(0);
	const char* mime = t.mime.empty() ? "image/jpeg" : t.mime.c_str();
	fr.data.insert(fr.data.end(), mime, mime + strlen(mime));
	fr.data.push_back(0);
	fr.data.push_back(3);
	fr.data.push_back(0);
	fr.data.insert(fr.data.end(), t.cover.begin(), t.cover.end());
	frames.push_back(fr);
}

static bool WriteMp3(const std::wstring& path, const OxzTag& t, bool clearEmpty)
{
	std::vector<unsigned char> file;
	if (!LoadFile(path, file, 512u * 1024u * 1024u)) return false;
	std::vector<Id3Fr> oldf;
	size_t audio = 0;
	ParseId3(file, oldf, audio);
	std::vector<Id3Fr> keep;
	for (size_t i = 0; i < oldf.size(); ++i) {
		const char* id = oldf[i].id;
		bool text = id[0] == 'T';
		bool known = text && (
			!memcmp(id, "TIT2", 4) || !memcmp(id, "TPE1", 4) || !memcmp(id, "TALB", 4) || !memcmp(id, "TPE2", 4) ||
			!memcmp(id, "TYER", 4) || !memcmp(id, "TDRC", 4) || !memcmp(id, "TRCK", 4) || !memcmp(id, "TPOS", 4) ||
			!memcmp(id, "TCON", 4) || !memcmp(id, "TCOM", 4) || !memcmp(id, "TEXT", 4) || !memcmp(id, "TCOP", 4) ||
			!memcmp(id, "TENC", 4) || !memcmp(id, "TBPM", 4));
		if (known) continue;
		if (!memcmp(id, "COMM", 4)) {
			std::wstring desc, body;
			if (oldf[i].data.size() >= 5)
				SplitTerm(oldf[i].data.data() + 4, (int)oldf[i].data.size() - 4, oldf[i].data[0], desc, body);
			if (desc.size() >= 4 && desc.compare(0, 4, L"iTun") == 0) { keep.push_back(oldf[i]); continue; }
			continue;
		}
		if (!memcmp(id, "USLT", 4)) continue;
		if (!memcmp(id, "APIC", 4) && (t.dropCover || !t.cover.empty())) continue;
		keep.push_back(oldf[i]);
	}
	OxzTag cur;
	if (!clearEmpty) {
		ReadMp3(file, cur, false);
		if (t.title.empty()) cur.title = cur.title; else cur.title = t.title;
		if (!t.artist.empty()) cur.artist = t.artist;
		if (!t.album.empty()) cur.album = t.album;
		if (!t.albumArtist.empty()) cur.albumArtist = t.albumArtist;
		if (!t.year.empty()) cur.year = t.year;
		if (!t.track.empty()) cur.track = t.track;
		if (!t.disc.empty()) cur.disc = t.disc;
		if (!t.genre.empty()) cur.genre = t.genre;
		if (!t.comment.empty()) cur.comment = t.comment;
		if (!t.composer.empty()) cur.composer = t.composer;
		if (!t.lyricist.empty()) cur.lyricist = t.lyricist;
		if (!t.copyright.empty()) cur.copyright = t.copyright;
		if (!t.encoder.empty()) cur.encoder = t.encoder;
		if (!t.bpm.empty()) cur.bpm = t.bpm;
		if (!t.lyrics.empty()) cur.lyrics = t.lyrics;
	}
	else cur = t;
	AddTextFr(keep, "TIT2", cur.title);
	AddTextFr(keep, "TPE1", cur.artist);
	AddTextFr(keep, "TALB", cur.album);
	AddTextFr(keep, "TPE2", cur.albumArtist);
	AddTextFr(keep, "TYER", cur.year);
	AddTextFr(keep, "TRCK", cur.track);
	AddTextFr(keep, "TPOS", cur.disc);
	AddTextFr(keep, "TCON", cur.genre);
	AddTextFr(keep, "TCOM", cur.composer);
	AddTextFr(keep, "TEXT", cur.lyricist);
	AddTextFr(keep, "TCOP", cur.copyright);
	AddTextFr(keep, "TENC", cur.encoder);
	AddTextFr(keep, "TBPM", cur.bpm);
	AddComm(keep, "COMM", cur.comment);
	AddComm(keep, "USLT", cur.lyrics);
	if (!t.cover.empty()) AddApic(keep, t);
	std::vector<unsigned char> body;
	for (size_t i = 0; i < keep.size(); ++i) {
		body.insert(body.end(), keep[i].id, keep[i].id + 4);
		PushBe(body, (unsigned)keep[i].data.size());
		body.push_back(0); body.push_back(0);
		body.insert(body.end(), keep[i].data.begin(), keep[i].data.end());
	}
	for (int i = 0; i < 256; ++i) body.push_back(0);
	std::vector<unsigned char> tag;
	tag.push_back('I'); tag.push_back('D'); tag.push_back('3');
	tag.push_back(3); tag.push_back(0); tag.push_back(0);
	PushSync(tag, (unsigned)body.size());
	tag.insert(tag.end(), body.begin(), body.end());
	std::wstring tmp = path + L".oztmp";
	FILE* fp = NULL;
	if (_wfopen_s(&fp, tmp.c_str(), L"wb") != 0 || !fp) return false;
	if (fwrite(tag.data(), 1, tag.size(), fp) != tag.size()) { fclose(fp); DeleteFileW(tmp.c_str()); return false; }
	if (audio < file.size()) {
		if (fwrite(file.data() + audio, 1, file.size() - audio, fp) != file.size() - audio) {
			fclose(fp); DeleteFileW(tmp.c_str()); return false;
		}
	}
	fclose(fp);
	if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
		DeleteFileW(tmp.c_str());
		return false;
	}
	return true;
}

static bool ReadTag(const std::wstring& path, OxzTag& t, bool withPic)
{
	int k = ExtKind(path);
	if (k == 2) return ReadFlac(path, t, withPic);
	std::vector<unsigned char> file;
	if (!LoadFile(path, file, 512u * 1024u * 1024u)) return false;
	if (k == 1) return ReadWav(file, t);
	if (k == 3) return ReadMp3(file, t, withPic);
	return false;
}

static bool WriteTag(const std::wstring& path, const OxzTag& t, bool clearEmpty)
{
	int k = ExtKind(path);
	if (k == 2) return WriteFlac(path, t, clearEmpty);
	if (k == 1) {
		if (!clearEmpty) {
			OxzTag cur;
			ReadTag(path, cur, false);
			if (!t.title.empty()) cur.title = t.title;
			if (!t.artist.empty()) cur.artist = t.artist;
			if (!t.album.empty()) cur.album = t.album;
			if (!t.albumArtist.empty()) cur.albumArtist = t.albumArtist;
			if (!t.year.empty()) cur.year = t.year;
			if (!t.track.empty()) cur.track = t.track;
			if (!t.disc.empty()) cur.disc = t.disc;
			if (!t.genre.empty()) cur.genre = t.genre;
			if (!t.comment.empty()) cur.comment = t.comment;
			if (!t.composer.empty()) cur.composer = t.composer;
			if (!t.lyricist.empty()) cur.lyricist = t.lyricist;
			if (!t.copyright.empty()) cur.copyright = t.copyright;
			if (!t.encoder.empty()) cur.encoder = t.encoder;
			if (!t.bpm.empty()) cur.bpm = t.bpm;
			if (!t.lyrics.empty()) cur.lyrics = t.lyrics;
			return WriteWav(path, cur);
		}
		return WriteWav(path, t);
	}
	if (k == 3) return WriteMp3(path, t, clearEmpty);
	return false;
}

static std::wstring EdGet(HWND w, int id)
{
	HWND e = GetDlgItem(w, id);
	int n = GetWindowTextLengthW(e);
	std::wstring s((size_t)n + 1, 0);
	GetWindowTextW(e, &s[0], n + 1);
	s.resize(wcslen(s.c_str()));
	return s;
}

static void EdSet(HWND w, int id, const std::wstring& s)
{
	SetWindowTextW(GetDlgItem(w, id), s.c_str());
}

enum {
	ED_TITLE = 1001, ED_ARTIST, ED_ALBUM, ED_AA, ED_YEAR, ED_TRACK, ED_DISC, ED_BPM,
	ED_GENRE, ED_COMP, ED_LYR, ED_COPY, ED_ENC, ED_COMMENT, ED_LYRICS, ED_PAT, ED_COVER,
	BT_SAVE = 1100, BT_CLOSE, BT_NUM, BT_NAME, BT_BROWSE, BT_CLEARCOV, ST_STATUS = 1200
};

struct TagUi {
	std::vector<std::wstring> paths;
	HWND wnd;
	bool done;
	HFONT font;
};

static void TakeTag(HWND w, OxzTag& t)
{
	t.title = EdGet(w, ED_TITLE);
	t.artist = EdGet(w, ED_ARTIST);
	t.album = EdGet(w, ED_ALBUM);
	t.albumArtist = EdGet(w, ED_AA);
	t.year = EdGet(w, ED_YEAR);
	t.track = EdGet(w, ED_TRACK);
	t.disc = EdGet(w, ED_DISC);
	t.bpm = EdGet(w, ED_BPM);
	t.genre = EdGet(w, ED_GENRE);
	t.composer = EdGet(w, ED_COMP);
	t.lyricist = EdGet(w, ED_LYR);
	t.copyright = EdGet(w, ED_COPY);
	t.encoder = EdGet(w, ED_ENC);
	t.comment = EdGet(w, ED_COMMENT);
	t.lyrics = EdGet(w, ED_LYRICS);
	t.dropCover = SendMessageW(GetDlgItem(w, BT_CLEARCOV), BM_GETCHECK, 0, 0) == BST_CHECKED;
	std::wstring cov = EdGet(w, ED_COVER);
	if (!cov.empty()) {
		std::vector<unsigned char> pic;
		if (LoadFile(cov, pic, 8u * 1024u * 1024u)) {
			t.cover.swap(pic);
			size_t dot = cov.find_last_of(L'.');
			std::wstring ext = dot == std::wstring::npos ? L"" : cov.substr(dot);
			if (_wcsicmp(ext.c_str(), L".png") == 0) t.mime = "image/png";
			else t.mime = "image/jpeg";
		}
	}
}

static void Overlay(OxzTag& cur, const OxzTag& ui)
{
	if (!ui.title.empty()) cur.title = ui.title;
	if (!ui.artist.empty()) cur.artist = ui.artist;
	if (!ui.album.empty()) cur.album = ui.album;
	if (!ui.albumArtist.empty()) cur.albumArtist = ui.albumArtist;
	if (!ui.year.empty()) cur.year = ui.year;
	if (!ui.track.empty()) cur.track = ui.track;
	if (!ui.disc.empty()) cur.disc = ui.disc;
	if (!ui.bpm.empty()) cur.bpm = ui.bpm;
	if (!ui.genre.empty()) cur.genre = ui.genre;
	if (!ui.composer.empty()) cur.composer = ui.composer;
	if (!ui.lyricist.empty()) cur.lyricist = ui.lyricist;
	if (!ui.copyright.empty()) cur.copyright = ui.copyright;
	if (!ui.encoder.empty()) cur.encoder = ui.encoder;
	if (!ui.comment.empty()) cur.comment = ui.comment;
	if (!ui.lyrics.empty()) cur.lyrics = ui.lyrics;
	if (ui.dropCover) cur.dropCover = true;
	if (!ui.cover.empty()) { cur.cover = ui.cover; cur.mime = ui.mime; }
}

static std::wstring ApplyPat(std::wstring pat, const OxzTag& f)
{
	struct Tok { const wchar_t* k; const std::wstring* v; };
	Tok tok[] = {
		{ L"%title%", &f.title }, { L"%artist%", &f.artist }, { L"%album%", &f.album },
		{ L"%albumartist%", &f.albumArtist }, { L"%year%", &f.year }, { L"%track%", &f.track },
		{ L"%disc%", &f.disc }, { L"%genre%", &f.genre }, { L"%composer%", &f.composer }, { L"%bpm%", &f.bpm }
	};
	for (int i = 0; i < 10; ++i) {
		for (;;) {
			size_t at = pat.find(tok[i].k);
			if (at == std::wstring::npos) break;
			pat.replace(at, wcslen(tok[i].k), *tok[i].v);
		}
	}
	const wchar_t* bad = L"\\/:*?\"<>|";
	for (size_t i = 0; i < pat.size(); ++i)
		if (wcschr(bad, pat[i])) pat[i] = L'_';
	while (!pat.empty() && (pat.back() == L' ' || pat.back() == L'.')) pat.pop_back();
	return pat;
}

static void Status(HWND w, const std::wstring& s)
{
	SetWindowTextW(GetDlgItem(w, ST_STATUS), s.c_str());
}

static void DoSave(TagUi* ui)
{
	OxzTag form;
	TakeTag(ui->wnd, form);
	bool multi = ui->paths.size() > 1;
	int okN = 0;
	for (size_t i = 0; i < ui->paths.size(); ++i) {
		OxzTag t;
		if (multi) {
			if (!ReadTag(ui->paths[i], t, false)) continue;
			Overlay(t, form);
			if (WriteTag(ui->paths[i], t, false)) okN++;
		}
		else {
			if (!ReadTag(ui->paths[i], t, false)) t = OxzTag();
			form.loop1 = t.loop1;
			form.loop2 = t.loop2;
			if (WriteTag(ui->paths[i], form, true)) okN++;
		}
	}
	wchar_t buf[128];
	_snwprintf_s(buf, _TRUNCATE, L2(L"%d 件保存しました", L"Saved %d"), okN);
	Status(ui->wnd, buf);
}

static void DoNumber(TagUi* ui)
{
	int okN = 0;
	for (size_t i = 0; i < ui->paths.size(); ++i) {
		OxzTag t;
		if (!ReadTag(ui->paths[i], t, false)) continue;
		wchar_t b[16];
		_snwprintf_s(b, _TRUNCATE, L"%02d", (int)i + 1);
		t.track = b;
		t.dropCover = false;
		t.cover.clear();
		if (WriteTag(ui->paths[i], t, false)) okN++;
	}
	if (ui->paths.size() == 1) EdSet(ui->wnd, ED_TRACK, L"01");
	wchar_t buf[128];
	_snwprintf_s(buf, _TRUNCATE, L2(L"%d 件に番号を振りました", L"Numbered %d"), okN);
	Status(ui->wnd, buf);
}

static void DoName(TagUi* ui)
{
	std::wstring pat = EdGet(ui->wnd, ED_PAT);
	if (pat.empty()) return;
	OxzTag form;
	TakeTag(ui->wnd, form);
	bool multi = ui->paths.size() > 1;
	int okN = 0;
	for (size_t i = 0; i < ui->paths.size(); ++i) {
		OxzTag t;
		ReadTag(ui->paths[i], t, false);
		if (multi) Overlay(t, form);
		else {
			if (!form.title.empty() || ui->paths.size() == 1) {
				t.title = form.title; t.artist = form.artist; t.album = form.album;
				t.albumArtist = form.albumArtist; t.year = form.year; t.track = form.track;
				t.disc = form.disc; t.genre = form.genre; t.composer = form.composer; t.bpm = form.bpm;
			}
		}
		std::wstring base = ApplyPat(pat, t);
		if (base.empty()) continue;
		const std::wstring& path = ui->paths[i];
		size_t slash = path.find_last_of(L"\\/");
		std::wstring dir = slash == std::wstring::npos ? L"" : path.substr(0, slash + 1);
		size_t dot = path.find_last_of(L'.');
		std::wstring ext = (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) ? path.substr(dot) : L"";
		std::wstring dest = dir + base + ext;
		if (_wcsicmp(dest.c_str(), path.c_str()) == 0) { okN++; continue; }
		if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
		if (!MoveFileW(path.c_str(), dest.c_str())) continue;
		ui->paths[i] = dest;
		okN++;
	}
	wchar_t buf[128];
	_snwprintf_s(buf, _TRUNCATE, L2(L"%d 件の名前を変えました", L"Renamed %d"), okN);
	Status(ui->wnd, buf);
}

static void BrowseCover(HWND w)
{
	wchar_t file[MAX_PATH];
	file[0] = 0;
	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = w;
	ofn.lpstrFilter = L"JPEG/PNG\0*.jpg;*.jpeg;*.png\0\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	if (GetOpenFileNameW(&ofn)) SetWindowTextW(GetDlgItem(w, ED_COVER), file);
}

static HWND Mk(HWND p, const wchar_t* cls, const wchar_t* txt, DWORD st, int x, int y, int w, int h, int id)
{
	return CreateWindowExW(cls == std::wstring(L"EDIT") || wcscmp(cls, L"EDIT") == 0 ? WS_EX_CLIENTEDGE : 0,
		cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h, p, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
}

static LRESULT CALLBACK TagProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	TagUi* ui = (TagUi*)GetWindowLongPtrW(w, GWLP_USERDATA);
	if (m == WM_CREATE) {
		ui = (TagUi*)((CREATESTRUCTW*)lp)->lpCreateParams;
		SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)ui);
		ui->wnd = w;
		ui->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		struct Row { int id; const wchar_t* ja; const wchar_t* en; int y; int h; DWORD st; };
		Row rows[] = {
			{ ED_TITLE, L"タイトル", L"Title", 8, 22, ES_AUTOHSCROLL },
			{ ED_ARTIST, L"アーティスト", L"Artist", 34, 22, ES_AUTOHSCROLL },
			{ ED_ALBUM, L"アルバム", L"Album", 60, 22, ES_AUTOHSCROLL },
			{ ED_AA, L"アルバムの人", L"Album artist", 86, 22, ES_AUTOHSCROLL },
			{ ED_YEAR, L"年", L"Year", 112, 22, ES_AUTOHSCROLL },
			{ ED_GENRE, L"ジャンル", L"Genre", 138, 22, ES_AUTOHSCROLL },
			{ ED_COMP, L"作曲", L"Composer", 164, 22, ES_AUTOHSCROLL },
			{ ED_LYR, L"作詞", L"Lyricist", 190, 22, ES_AUTOHSCROLL },
			{ ED_COPY, L"著作権", L"Copyright", 216, 22, ES_AUTOHSCROLL },
			{ ED_ENC, L"エンコーダ", L"Encoder", 242, 22, ES_AUTOHSCROLL },
			{ ED_COMMENT, L"コメント", L"Comment", 268, 22, ES_AUTOHSCROLL },
			{ ED_LYRICS, L"歌詞", L"Lyrics", 294, 72, ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL },
			{ ED_COVER, L"ジャケット", L"Cover", 372, 22, ES_AUTOHSCROLL },
			{ ED_PAT, L"ファイル名", L"Filename", 430, 22, ES_AUTOHSCROLL }
		};
		for (int i = 0; i < 14; ++i) {
			Mk(w, L"STATIC", L2(rows[i].ja, rows[i].en), 0, 8, rows[i].y + 2, 110, 18, 0);
			int ew = rows[i].id == ED_YEAR ? 110 : (rows[i].id == ED_COVER ? 390 : 420);
			Mk(w, L"EDIT", L"", WS_TABSTOP | rows[i].st, 124, rows[i].y, ew, rows[i].h, rows[i].id);
		}
		Mk(w, L"STATIC", L2(L"トラック", L"Track"), 0, 250, 114, 60, 18, 0);
		Mk(w, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 312, 112, 48, 22, ED_TRACK);
		Mk(w, L"STATIC", L2(L"ディスク", L"Disc"), 0, 368, 114, 60, 18, 0);
		Mk(w, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 430, 112, 40, 22, ED_DISC);
		Mk(w, L"STATIC", L"BPM", 0, 476, 114, 30, 18, 0);
		Mk(w, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, 508, 112, 48, 22, ED_BPM);
		Mk(w, L"BUTTON", L"...", WS_TABSTOP, 530, 372, 28, 22, BT_BROWSE);
		Mk(w, L"BUTTON", L2(L"ジャケットを外す", L"Remove cover"), WS_TABSTOP | BS_AUTOCHECKBOX, 124, 400, 200, 20, BT_CLEARCOV);
		Mk(w, L"BUTTON", L2(L"名前へ", L"To name"), WS_TABSTOP, 360, 428, 90, 24, BT_NAME);
		Mk(w, L"BUTTON", L2(L"連番", L"Number"), WS_TABSTOP, 456, 428, 90, 24, BT_NUM);
		Mk(w, L"BUTTON", OxzTxt(OS_SAVE), WS_TABSTOP, 124, 468, 120, 28, BT_SAVE);
		Mk(w, L"BUTTON", OxzTxt(OS_CLOSE), WS_TABSTOP, 254, 468, 120, 28, BT_CLOSE);
		Mk(w, L"STATIC", L"", 0, 8, 504, 540, 36, ST_STATUS);
		SetWindowTextW(GetDlgItem(w, ED_PAT), L"%artist% - %title%");
		SendMessageW(GetDlgItem(w, ED_LYRICS), EM_LIMITTEXT, 256 * 1024, 0);
		if (ui->paths.size() == 1) {
			OxzTag t;
			if (ReadTag(ui->paths[0], t, false)) {
				EdSet(w, ED_TITLE, t.title); EdSet(w, ED_ARTIST, t.artist); EdSet(w, ED_ALBUM, t.album);
				EdSet(w, ED_AA, t.albumArtist); EdSet(w, ED_YEAR, t.year); EdSet(w, ED_TRACK, t.track);
				EdSet(w, ED_DISC, t.disc); EdSet(w, ED_BPM, t.bpm); EdSet(w, ED_GENRE, t.genre);
				EdSet(w, ED_COMP, t.composer); EdSet(w, ED_LYR, t.lyricist); EdSet(w, ED_COPY, t.copyright);
				EdSet(w, ED_ENC, t.encoder); EdSet(w, ED_COMMENT, t.comment); EdSet(w, ED_LYRICS, t.lyrics);
			}
		}
		else {
			SetWindowTextW(GetDlgItem(w, ST_STATUS), L2(L"複数: 空欄は変更しません。", L"Multi: blank fields stay as they are."));
		}
		HWND ch = GetWindow(w, GW_CHILD);
		while (ch) { SendMessageW(ch, WM_SETFONT, (WPARAM)ui->font, TRUE); ch = GetWindow(ch, GW_HWNDNEXT); }
		return 0;
	}
	if (m == WM_COMMAND) {
		int id = LOWORD(wp);
		if (id == BT_SAVE) DoSave(ui);
		else if (id == BT_CLOSE) DestroyWindow(w);
		else if (id == BT_NUM) DoNumber(ui);
		else if (id == BT_NAME) DoName(ui);
		else if (id == BT_BROWSE) BrowseCover(w);
		return 0;
	}
	if (m == WM_CLOSE) { DestroyWindow(w); return 0; }
	if (m == WM_DESTROY) {
		if (ui) { ui->done = true; if (ui->font) DeleteObject(ui->font); ui->font = NULL; }
		return 0;
	}
	return DefWindowProcW(w, m, wp, m == WM_NCCREATE ? lp : lp);
}

void OxzShowTagEdit(HWND owner, const std::vector<std::wstring>& paths)
{
	if (paths.empty()) return;
	TagUi ui;
	ui.paths = paths;
	ui.wnd = NULL;
	ui.done = false;
	ui.font = NULL;
	WNDCLASSW wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = TagProc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"OxzTagEdit";
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	RegisterClassW(&wc);
	HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"OxzTagEdit", OxzTxt(OS_TAGEDIT),
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, 590, 590, owner, NULL, wc.hInstance, &ui);
	if (!w) return;
	if (owner) EnableWindow(owner, FALSE);
	MSG msg;
	while (!ui.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
		if (!IsDialogMessageW(w, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
}

struct Pcm { int hz, ch; std::vector<short> s; };

static FLAC__StreamDecoderWriteStatus FlacWriteCb(const FLAC__StreamDecoder*, const FLAC__Frame* frame, const FLAC__int32* const buffer[], void* client)
{
	Pcm* p = (Pcm*)client;
	unsigned n = frame->header.blocksize;
	unsigned ch = frame->header.channels;
	if (ch < 1 || ch > 8) return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
	p->ch = (int)ch;
	p->hz = (int)frame->header.sample_rate;
	int shift = (int)frame->header.bits_per_sample - 16;
	for (unsigned i = 0; i < n; ++i) {
		for (unsigned c = 0; c < ch; ++c) {
			int s = buffer[c][i];
			if (shift > 0) s >>= shift;
			else if (shift < 0) s <<= -shift;
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			p->s.push_back((short)s);
		}
	}
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}
static void FlacMetaCb(const FLAC__StreamDecoder*, const FLAC__StreamMetadata*, void*) {}
static void FlacErrCb(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*) {}

static void SampleTo16(const unsigned char* p, int bits, int& o)
{
	if (bits == 16) o = (short)(p[0] | (p[1] << 8));
	else if (bits == 8) o = ((int)p[0] - 128) << 8;
	else if (bits == 24) o = (int)(char)p[2] << 8 | p[1];
	else if (bits == 32) o = (short)((p[3] << 8) | p[2]);
	else o = 0;
	if (o > 32767) o = 32767;
	if (o < -32768) o = -32768;
}

static bool ReadWavPcm(const std::wstring& path, Pcm& pcm)
{
	std::vector<unsigned char> file;
	if (!LoadFile(path, file, 512u * 1024u * 1024u)) return false;
	if (file.size() < 12 || memcmp(file.data(), "RIFF", 4) || memcmp(file.data() + 8, "WAVE", 4)) return false;
	int format = 0, ch = 0, hz = 0, bits = 0, block = 0;
	const unsigned char* data = NULL;
	unsigned dataN = 0;
	size_t pos = 12;
	while (pos + 8 <= file.size()) {
		unsigned id = RdLe(file.data() + pos);
		unsigned sz = RdLe(file.data() + pos + 4);
		unsigned pad = (sz + 1u) & ~1u;
		if (pos + 8 + pad > file.size()) break;
		if (id == 0x20746d66 && sz >= 16) {
			format = file[pos + 8] | (file[pos + 9] << 8);
			ch = file[pos + 10] | (file[pos + 11] << 8);
			hz = (int)RdLe(file.data() + pos + 12);
			block = file[pos + 20] | (file[pos + 21] << 8);
			bits = file[pos + 22] | (file[pos + 23] << 8);
			if (format == 0xFFFE && sz >= 26) {
				int cb = file[pos + 24] | (file[pos + 25] << 8);
				if (cb >= 2) bits = file[pos + 26] | (file[pos + 27] << 8);
				if (sz >= 40) format = file[pos + 32] | (file[pos + 33] << 8);
			}
		}
		else if (id == 0x61746164) { data = file.data() + pos + 8; dataN = sz; }
		pos += 8 + pad;
	}
	if (!data || ch < 1 || ch > 8 || hz < 1 || block < ch) return false;
	if (format != 1 && format != 3) return false;
	pcm.ch = ch; pcm.hz = hz;
	unsigned frames = dataN / (unsigned)block;
	pcm.s.reserve((size_t)frames * ch);
	for (unsigned i = 0; i < frames; ++i) {
		const unsigned char* fr = data + (size_t)i * block;
		for (int c = 0; c < ch; ++c) {
			int s = 0;
			if (format == 3 && bits == 32) {
				float f; memcpy(&f, fr + c * 4, 4);
				s = (int)(f * 32767.f);
			}
			else SampleTo16(fr + c * (bits / 8), bits, s);
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			pcm.s.push_back((short)s);
		}
	}
	return !pcm.s.empty();
}

static bool ReadFlacPcm(const std::wstring& path, Pcm& pcm)
{
	FILE* fp = NULL;
	if (_wfopen_s(&fp, path.c_str(), L"rb") != 0 || !fp) return false;
	unsigned char b = 0;
	if (fread(&b, 1, 1, fp) != 1) { fclose(fp); return false; }
	if (b == 0xBF) { fclose(fp); return false; }
	fseek(fp, 0, SEEK_SET);
	FLAC__StreamDecoder* dec = FLAC__stream_decoder_new();
	if (!dec) { fclose(fp); return false; }
	if (FLAC__stream_decoder_init_FILE(dec, fp, FlacWriteCb, FlacMetaCb, FlacErrCb, &pcm) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		FLAC__stream_decoder_delete(dec);
		fclose(fp);
		return false;
	}
	bool ok = FLAC__stream_decoder_process_until_end_of_stream(dec) != 0;
	FLAC__stream_decoder_delete(dec);
	return ok && !pcm.s.empty() && pcm.ch > 0;
}

static bool ReadMp3Pcm(const std::wstring& path, Pcm& pcm)
{
	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if (FAILED(hr)) return false;
	IMFSourceReader* reader = NULL;
	hr = MFCreateSourceReaderFromURL(path.c_str(), NULL, &reader);
	if (FAILED(hr)) { MFShutdown(); return false; }
	IMFMediaType* native = NULL;
	reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &native);
	UINT32 ch = 2, hz = 44100;
	if (native) {
		native->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);
		native->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &hz);
		native->Release();
	}
	if (ch < 1) ch = 1;
	if (ch > 8) ch = 8;
	IMFMediaType* out = NULL;
	MFCreateMediaType(&out);
	out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, ch);
	out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, hz);
	out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	out->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, ch * 2);
	out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, hz * ch * 2);
	hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, out);
	out->Release();
	if (FAILED(hr)) { reader->Release(); MFShutdown(); return false; }
	pcm.ch = (int)ch; pcm.hz = (int)hz;
	for (;;) {
		DWORD flags = 0;
		IMFSample* sample = NULL;
		hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, NULL, &sample);
		if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) { if (sample) sample->Release(); break; }
		if (!sample) continue;
		IMFMediaBuffer* buf = NULL;
		sample->ConvertToContiguousBuffer(&buf);
		if (buf) {
			BYTE* p = NULL; DWORD n = 0;
			buf->Lock(&p, NULL, &n);
			size_t was = pcm.s.size();
			pcm.s.resize(was + n / 2);
			if (n) memcpy(&pcm.s[was], p, n & ~1u);
			buf->Unlock();
			buf->Release();
		}
		sample->Release();
	}
	reader->Release();
	MFShutdown();
	return !pcm.s.empty();
}

static bool DecodeAny(const std::wstring& path, Pcm& pcm)
{
	int k = ExtKind(path);
	if (k == 1) return ReadWavPcm(path, pcm);
	if (k == 2) return ReadFlacPcm(path, pcm);
	if (k == 3) return ReadMp3Pcm(path, pcm);
	return false;
}

static int SnapRate(int hz)
{
	static const int r[] = { 16000, 22050, 24000, 32000, 44100, 48000 };
	for (int i = 0; i < 6; ++i) if (hz == r[i]) return hz;
	if (hz > 44100) return 48000;
	if (hz > 32000) return 44100;
	if (hz > 24000) return 32000;
	if (hz > 22050) return 24000;
	if (hz > 16000) return 22050;
	return 16000;
}

static void Resample(Pcm& p, int dst)
{
	if (p.hz == dst || p.ch < 1 || p.s.empty()) { p.hz = dst; return; }
	int frames = (int)(p.s.size() / (size_t)p.ch);
	double step = (double)p.hz / (double)dst;
	int outF = (int)(frames / step);
	if (outF < 1) outF = 1;
	std::vector<short> o((size_t)outF * p.ch);
	for (int i = 0; i < outF; ++i) {
		double src = i * step;
		int i0 = (int)src;
		if (i0 >= frames) i0 = frames - 1;
		int i1 = i0 + 1; if (i1 >= frames) i1 = frames - 1;
		double f = src - (int)src;
		for (int c = 0; c < p.ch; ++c) {
			int a = p.s[(size_t)i0 * p.ch + c];
			int b = p.s[(size_t)i1 * p.ch + c];
			o[(size_t)i * p.ch + c] = (short)(a + (b - a) * f);
		}
	}
	p.s.swap(o);
	p.hz = dst;
}

static bool WriteWavPcm(const std::wstring& path, const Pcm& p)
{
	if (p.ch < 1 || p.s.empty()) return false;
	unsigned dataN = (unsigned)(p.s.size() * 2);
	unsigned block = (unsigned)p.ch * 2;
	FILE* fp = NULL;
	if (_wfopen_s(&fp, path.c_str(), L"wb") != 0 || !fp) return false;
	unsigned riff = 36 + dataN;
	unsigned char h[44];
	memcpy(h, "RIFF", 4); memcpy(h + 8, "WAVE", 4); memcpy(h + 12, "fmt ", 4); memcpy(h + 36, "data", 4);
	h[4] = (unsigned char)riff; h[5] = (unsigned char)(riff >> 8); h[6] = (unsigned char)(riff >> 16); h[7] = (unsigned char)(riff >> 24);
	h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0;
	h[20] = 1; h[21] = 0;
	h[22] = (unsigned char)p.ch; h[23] = 0;
	h[24] = (unsigned char)p.hz; h[25] = (unsigned char)(p.hz >> 8); h[26] = (unsigned char)(p.hz >> 16); h[27] = (unsigned char)(p.hz >> 24);
	unsigned avg = (unsigned)p.hz * block;
	h[28] = (unsigned char)avg; h[29] = (unsigned char)(avg >> 8); h[30] = (unsigned char)(avg >> 16); h[31] = (unsigned char)(avg >> 24);
	h[32] = (unsigned char)block; h[33] = (unsigned char)(block >> 8);
	h[34] = 16; h[35] = 0;
	h[40] = (unsigned char)dataN; h[41] = (unsigned char)(dataN >> 8); h[42] = (unsigned char)(dataN >> 16); h[43] = (unsigned char)(dataN >> 24);
	bool ok = fwrite(h, 1, 44, fp) == 44 && fwrite(p.s.data(), 1, dataN, fp) == dataN;
	fclose(fp);
	if (!ok) DeleteFileW(path.c_str());
	return ok;
}

static bool WriteFlacPcm(const std::wstring& path, const Pcm& pcm)
{
	if (pcm.ch < 1 || pcm.ch > 8 || pcm.s.empty()) return false;
	FILE* fp = NULL;
	if (_wfopen_s(&fp, path.c_str(), L"w+b") != 0 || !fp) return false;
	FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
	if (!enc) { fclose(fp); return false; }
	FLAC__stream_encoder_set_verify(enc, false);
	FLAC__stream_encoder_set_streamable_subset(enc, false);
	FLAC__stream_encoder_set_compression_level(enc, 5);
	FLAC__stream_encoder_set_channels(enc, (unsigned)pcm.ch);
	FLAC__stream_encoder_set_bits_per_sample(enc, 16);
	FLAC__stream_encoder_set_sample_rate(enc, (unsigned)pcm.hz);
	FLAC__stream_encoder_set_total_samples_estimate(enc, (FLAC__uint64)(pcm.s.size() / (size_t)pcm.ch));
	if (FLAC__stream_encoder_init_FILE(enc, fp, NULL, NULL) != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
		FLAC__stream_encoder_delete(enc);
		fclose(fp);
		DeleteFileW(path.c_str());
		return false;
	}
	const int chunk = 4096;
	std::vector<FLAC__int32> buf((size_t)chunk * pcm.ch);
	bool ok = true;
	size_t frames = pcm.s.size() / (size_t)pcm.ch;
	size_t at = 0;
	while (at < frames && ok) {
		unsigned n = (unsigned)((frames - at) > (size_t)chunk ? chunk : (frames - at));
		for (unsigned i = 0; i < n * (unsigned)pcm.ch; ++i) buf[i] = pcm.s[at * pcm.ch + i];
		if (!FLAC__stream_encoder_process_interleaved(enc, buf.data(), n)) ok = false;
		at += n;
	}
	if (!FLAC__stream_encoder_finish(enc)) ok = false;
	FLAC__stream_encoder_delete(enc);
	if (!ok) DeleteFileW(path.c_str());
	return ok;
}

static bool WriteMp3Pcm(const std::wstring& path, const Pcm& pcm, int kbps)
{
	if (pcm.ch < 1 || pcm.ch > 2 || pcm.s.empty()) return false;
	if (kbps < 64) kbps = 64;
	if (kbps > 320) kbps = 320;
	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
	if (FAILED(hr)) return false;
	IMFSinkWriter* writer = NULL;
	hr = MFCreateSinkWriterFromURL(path.c_str(), NULL, NULL, &writer);
	if (FAILED(hr)) { MFShutdown(); return false; }
	IMFMediaType* outType = NULL;
	IMFMediaType* inType = NULL;
	DWORD stream = 0;
	bool ok = false;
	MFCreateMediaType(&outType);
	outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
	outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(kbps * 1000 / 8));
	outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)pcm.ch);
	outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)pcm.hz);
	hr = writer->AddStream(outType, &stream);
	if (FAILED(hr)) goto done;
	MFCreateMediaType(&inType);
	inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, (UINT32)pcm.ch);
	inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32)pcm.hz);
	inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, (UINT32)pcm.ch * 2);
	inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)pcm.hz * pcm.ch * 2);
	inType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	hr = writer->SetInputMediaType(stream, inType, NULL);
	if (FAILED(hr)) goto done;
	hr = writer->BeginWriting();
	if (FAILED(hr)) goto done;
	{
		const int chunk = 4096;
		size_t frames = pcm.s.size() / (size_t)pcm.ch;
		size_t at = 0;
		LONGLONG rt = 0;
		ok = true;
		while (at < frames) {
			unsigned n = (unsigned)((frames - at) > (size_t)chunk ? chunk : (frames - at));
			DWORD cb = n * (DWORD)pcm.ch * 2;
			IMFSample* sample = NULL;
			IMFMediaBuffer* buffer = NULL;
			if (FAILED(MFCreateSample(&sample))) { ok = false; break; }
			if (FAILED(MFCreateMemoryBuffer(cb, &buffer))) { sample->Release(); ok = false; break; }
			BYTE* p = NULL;
			buffer->Lock(&p, NULL, NULL);
			memcpy(p, &pcm.s[at * pcm.ch], cb);
			buffer->Unlock();
			buffer->SetCurrentLength(cb);
			sample->AddBuffer(buffer);
			sample->SetSampleTime(rt);
			LONGLONG dur = (LONGLONG)n * 10000000LL / pcm.hz;
			sample->SetSampleDuration(dur);
			rt += dur;
			if (FAILED(writer->WriteSample(stream, sample))) ok = false;
			buffer->Release();
			sample->Release();
			if (!ok) break;
			at += n;
		}
		if (ok && FAILED(writer->Finalize())) ok = false;
	}
done:
	if (inType) inType->Release();
	if (outType) outType->Release();
	if (writer) writer->Release();
	MFShutdown();
	if (!ok) DeleteFileW(path.c_str());
	return ok;
}

static std::wstring DestPath(const std::wstring& src, const wchar_t* ext)
{
	size_t slash = src.find_last_of(L"\\/");
	size_t dot = src.find_last_of(L'.');
	std::wstring dir = slash == std::wstring::npos ? L"" : src.substr(0, slash + 1);
	std::wstring stem = src.substr(slash == std::wstring::npos ? 0 : slash + 1,
		(dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) ? dot - (slash == std::wstring::npos ? 0 : slash + 1) : std::wstring::npos);
	std::wstring dest = dir + stem + ext;
	if (_wcsicmp(dest.c_str(), src.c_str()) == 0) dest = dir + stem + L"_conv" + ext;
	for (int n = 2; GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES && n < 100; ++n) {
		wchar_t num[16];
		_snwprintf_s(num, _TRUNCATE, L" (%d)", n);
		dest = dir + stem + num + ext;
	}
	return dest;
}

struct ConvUi { std::vector<std::wstring> paths; HWND wnd; bool done; HFONT font; };

static void DoConvert(ConvUi* ui)
{
	int kind = 2;
	if (SendMessageW(GetDlgItem(ui->wnd, 1301), BM_GETCHECK, 0, 0) == BST_CHECKED) kind = 1;
	else if (SendMessageW(GetDlgItem(ui->wnd, 1303), BM_GETCHECK, 0, 0) == BST_CHECKED) kind = 3;
	int kbps = _wtoi(EdGet(ui->wnd, 1304).c_str());
	if (kbps <= 0) kbps = 192;
	const wchar_t* ext = kind == 1 ? L".wav" : (kind == 3 ? L".mp3" : L".flac");
	int okN = 0;
	for (size_t i = 0; i < ui->paths.size(); ++i) {
		wchar_t st[256];
		_snwprintf_s(st, _TRUNCATE, L"%u / %u", (unsigned)(i + 1), (unsigned)ui->paths.size());
		Status(ui->wnd, st);
		Pcm pcm;
		if (!DecodeAny(ui->paths[i], pcm)) continue;
		if (kind == 3) {
			if (pcm.ch > 2) continue;
			Resample(pcm, SnapRate(pcm.hz));
		}
		std::wstring dest = DestPath(ui->paths[i], ext);
		bool ok = false;
		if (kind == 1) ok = WriteWavPcm(dest, pcm);
		else if (kind == 2) ok = WriteFlacPcm(dest, pcm);
		else ok = WriteMp3Pcm(dest, pcm, kbps);
		if (!ok) continue;
		OxzTag tag;
		if (ReadTag(ui->paths[i], tag, true)) {
			tag.dropCover = false;
			WriteTag(dest, tag, true);
		}
		okN++;
	}
	wchar_t buf[128];
	_snwprintf_s(buf, _TRUNCATE, L2(L"%d 件変換しました", L"Converted %d"), okN);
	Status(ui->wnd, buf);
}

static LRESULT CALLBACK ConvProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
	ConvUi* ui = (ConvUi*)GetWindowLongPtrW(w, GWLP_USERDATA);
	if (m == WM_CREATE) {
		ui = (ConvUi*)((CREATESTRUCTW*)lp)->lpCreateParams;
		SetWindowLongPtrW(w, GWLP_USERDATA, (LONG_PTR)ui);
		ui->wnd = w;
		ui->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
		Mk(w, L"LISTBOX", L"", WS_VSCROLL | LBS_NOINTEGRALHEIGHT, 8, 8, 460, 180, 1300);
		for (size_t i = 0; i < ui->paths.size(); ++i)
			SendMessageW(GetDlgItem(w, 1300), LB_ADDSTRING, 0, (LPARAM)ui->paths[i].c_str());
		Mk(w, L"BUTTON", L"WAV", WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP, 8, 198, 80, 22, 1301);
		Mk(w, L"BUTTON", L"FLAC", WS_TABSTOP | BS_AUTORADIOBUTTON, 96, 198, 80, 22, 1302);
		Mk(w, L"BUTTON", L"MP3", WS_TABSTOP | BS_AUTORADIOBUTTON, 184, 198, 80, 22, 1303);
		SendMessageW(GetDlgItem(w, 1302), BM_SETCHECK, BST_CHECKED, 0);
		Mk(w, L"STATIC", L"MP3 kbps", 0, 280, 200, 70, 18, 1306);
		Mk(w, L"EDIT", L"192", WS_TABSTOP | ES_AUTOHSCROLL, 354, 198, 60, 22, 1304);
		ShowWindow(GetDlgItem(w, 1306), SW_HIDE);
		ShowWindow(GetDlgItem(w, 1304), SW_HIDE);
		Mk(w, L"BUTTON", OxzTxt(OS_CONVERT), WS_TABSTOP, 8, 232, 140, 28, 1305);
		Mk(w, L"BUTTON", OxzTxt(OS_CLOSE), WS_TABSTOP, 156, 232, 120, 28, BT_CLOSE);
		Mk(w, L"STATIC", L2(L"同じ場所に新しいファイルを作ります。元のタグは写します。", L"Writes a new file beside the source and copies tags."), 0, 8, 268, 460, 36, ST_STATUS);
		HWND ch = GetWindow(w, GW_CHILD);
		while (ch) { SendMessageW(ch, WM_SETFONT, (WPARAM)ui->font, TRUE); ch = GetWindow(ch, GW_HWNDNEXT); }
		return 0;
	}
	if (m == WM_COMMAND) {
		int id = LOWORD(wp);
		if (id == 1301 || id == 1302 || id == 1303) {
			int show = id == 1303 ? SW_SHOW : SW_HIDE;
			ShowWindow(GetDlgItem(w, 1306), show);
			ShowWindow(GetDlgItem(w, 1304), show);
		}
		else if (id == 1305) DoConvert(ui);
		else if (id == BT_CLOSE) DestroyWindow(w);
		return 0;
	}
	if (m == WM_CLOSE) { DestroyWindow(w); return 0; }
	if (m == WM_DESTROY) {
		if (ui) { ui->done = true; if (ui->font) DeleteObject(ui->font); ui->font = NULL; }
		return 0;
	}
	return DefWindowProcW(w, m, wp, lp);
}

void OxzShowConvert(HWND owner, const std::vector<std::wstring>& paths)
{
	if (paths.empty()) return;
	ConvUi ui;
	ui.paths = paths;
	ui.wnd = NULL;
	ui.done = false;
	ui.font = NULL;
	WNDCLASSW wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = ConvProc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"OxzConvert";
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	RegisterClassW(&wc);
	HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"OxzConvert", OxzTxt(OS_CONVERT),
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 360, owner, NULL, wc.hInstance, &ui);
	if (!w) return;
	if (owner) EnableWindow(owner, FALSE);
	MSG msg;
	while (!ui.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
		if (!IsDialogMessageW(w, &msg)) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}
	if (owner) { EnableWindow(owner, TRUE); SetForegroundWindow(owner); }
}
