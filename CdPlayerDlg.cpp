#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CdPlayerDlg.h"
#include "TranscodeExport.h"
#include "PlayList.h"
#include "CCustomPopupMenu.h"
#include "GdiSoft2D.h"
#include <winioctl.h>
#include <ntddcdrm.h>
#include <ntddscsi.h>
#include <wininet.h>
#include <wincrypt.h>
#include <process.h>
#include <dbt.h>
#include <mmsystem.h>
#include <atlimage.h>
#include <imapi2.h>
#include <imapi2error.h>
#include <imapi2fs.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlobj.h>
#include <commdlg.h>
#include <cmath>
#include <mmreg.h>
#include "rubberband/RubberBandStretcher.h"
#include "AudioUpscaler.h"
#include "MpPlayerAddons.h"
#include "CAnalyzerDlg.h"
#include "CPianoRoll.h"

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#if _MSC_VER >= 1950
#pragma comment(lib, "rubberband-library_2026")
#else
#pragma comment(lib, "rubberband-library")
#endif

extern CPlayList* pl;
extern void MpPersistSavedataQuick();
extern int pitch;
extern int tempo;
extern COggDlg* og;
extern int g_ds_pcm_ch;
extern int g_ds_pcm_rate;
extern int g_ds_pcm_bits;
extern int g_outBytesPerFrame;
void equaliserBank(int bank, void* data, int len, BOOL reset,
	int bitsOverride, int chOverride, int rateOverride);
void EqualiserSetFormatVolContext(int mode, BOOL spcApplicable);

enum {
	WM_CD_POS = WM_APP + 90,
	WM_CD_ENDED,
	WM_CD_RIPPROG,
	WM_CD_RIPDONE,
	WM_CD_LOOKUP,
	WM_CD_COVER,
	WM_CD_BURNPROG,
	WM_CD_BURNDONE,
	WM_CD_LOADTOC,
	WM_CD_MEDIA
};

enum {
	IDM_CD_REFRESH = 0xCD10,
	IDM_CD_EJECT,
	IDM_CD_LOAD,
	IDM_CD_PLAY,
	IDM_CD_PAUSE,
	IDM_CD_STOP,
	IDM_CD_PREV,
	IDM_CD_NEXT,
	IDM_CD_REPEAT,
	IDM_CD_SHUFFLE,
	IDM_CD_LOOKUP,
	IDM_CD_SEARCH,
	IDM_CD_RIPSEL_WAV,
	IDM_CD_RIPSEL_MP3,
	IDM_CD_RIPSEL_FLAC,
	IDM_CD_RIPALL_WAV,
	IDM_CD_RIPALL_MP3,
	IDM_CD_RIPALL_FLAC,
	IDM_CD_RIPONE_WAV,
	IDM_CD_RIPONE_MP3,
	IDM_CD_RIPONE_FLAC,
	IDM_CD_ADDPL,
	IDM_CD_BURNAUD,
	IDM_CD_BURNDATA,
	IDM_CD_ERASE,
	IDM_CD_HELP,
	IDM_CD_FOLLOW,
	IDM_CD_CLOSE,
	IDM_CD_EDITCELL,
	IDM_CD_COPY,
	IDM_CD_PASTE,
	IDM_CD_ABA,
	IDM_CD_ABB,
	IDM_CD_ABCLR
};

#ifndef IDC_CD_CELLEDIT
#define IDC_CD_CELLEDIT 39991
#endif

static CCdPlayerDlg* g_cdDlg = NULL;
static CDialog* g_cdHelp = NULL;

static DWORD CdMsfToLba(UCHAR m, UCHAR s, UCHAR f)
{
	const DWORD tot = ((DWORD)m * 60u + (DWORD)s) * 75u + (DWORD)f;
	if (tot < 150u) return 0;
	return tot - 150u;
}

static void CdLbaToMsf(DWORD lba, int* m, int* s)
{
	const DWORD fr = lba + 150u;
	*m = (int)(fr / 75u / 60u);
	*s = (int)((fr / 75u) % 60u);
}

static BOOL CdReadSectors(HANDLE h, DWORD lba, DWORD nsec, BYTE* out, DWORD outBytes)
{
	if (!h || h == INVALID_HANDLE_VALUE || !out || nsec == 0) return FALSE;
	const DWORD need = nsec * 2352u;
	if (outBytes < need) return FALSE;

	RAW_READ_INFO ri;
	ZeroMemory(&ri, sizeof(ri));
	ri.DiskOffset.QuadPart = (LONGLONG)lba * 2048;
	ri.SectorCount = nsec;
	ri.TrackMode = CDDA;
	DWORD br = 0;
	if (DeviceIoControl(h, IOCTL_CDROM_RAW_READ, &ri, sizeof(ri), out, need, &br, NULL) && br >= need)
		return TRUE;

	BYTE pkt[sizeof(SCSI_PASS_THROUGH_DIRECT) + 32];
	ZeroMemory(pkt, sizeof(pkt));
	SCSI_PASS_THROUGH_DIRECT* sp = (SCSI_PASS_THROUGH_DIRECT*)pkt;
	sp->Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
	sp->CdbLength = 12;
	sp->DataIn = SCSI_IOCTL_DATA_IN;
	sp->DataTransferLength = need;
	sp->TimeOutValue = 20;
	sp->DataBuffer = out;
	sp->SenseInfoLength = 32;
	sp->SenseInfoOffset = sizeof(SCSI_PASS_THROUGH_DIRECT);
	sp->Cdb[0] = 0xBE;
	sp->Cdb[2] = (BYTE)((lba >> 24) & 0xFF);
	sp->Cdb[3] = (BYTE)((lba >> 16) & 0xFF);
	sp->Cdb[4] = (BYTE)((lba >> 8) & 0xFF);
	sp->Cdb[5] = (BYTE)(lba & 0xFF);
	sp->Cdb[6] = (BYTE)((nsec >> 16) & 0xFF);
	sp->Cdb[7] = (BYTE)((nsec >> 8) & 0xFF);
	sp->Cdb[8] = (BYTE)(nsec & 0xFF);
	sp->Cdb[9] = 0x10;
	br = 0;
	return DeviceIoControl(h, IOCTL_SCSI_PASS_THROUGH_DIRECT, sp, sizeof(pkt), sp, sizeof(pkt), &br, NULL)
		&& sp->DataTransferLength >= need;
}

static BOOL CdReadSectorsVerified(HANDLE h, DWORD lba, DWORD nsec, BYTE* a, BYTE* b, DWORD bytes)
{
	int tryN = 0;
	for (;;) {
		if (!CdReadSectors(h, lba, nsec, a, bytes)) return FALSE;
		if (!CdReadSectors(h, lba, nsec, b, bytes)) return FALSE;
		if (memcmp(a, b, nsec * 2352) == 0) return TRUE;
		tryN++;
		if (tryN >= 8) return FALSE;
	}
}

static void CdWriteWavHdr(CFile& f, int rate, int ch, int bits)
{
	if (rate < 8000) rate = 44100;
	if (ch < 1) ch = 2;
	if (bits != 16 && bits != 24 && bits != 32) bits = 16;
	const WORD nch = (WORD)ch;
	const WORD bps = (WORD)bits;
	const WORD align = (WORD)(nch * (bps / 8));
	BYTE h[80];
	ZeroMemory(h, sizeof(h));
	memcpy(h + 0, "RIFF", 4);
	memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "JUNK", 4);
	*(DWORD*)(h + 16) = 28;
	memcpy(h + 48, "fmt ", 4);
	*(DWORD*)(h + 52) = 16;
	*(WORD*)(h + 56) = WAVE_FORMAT_PCM;
	*(WORD*)(h + 58) = nch;
	*(DWORD*)(h + 60) = (DWORD)rate;
	*(DWORD*)(h + 64) = (DWORD)rate * align;
	*(WORD*)(h + 68) = align;
	*(WORD*)(h + 70) = bps;
	memcpy(h + 72, "data", 4);
	f.Write(h, 80);
}

static void CdFinishWavHdr(CFile& f)
{
	const ULONGLONG len = f.GetLength();
	DWORD riff = (DWORD)(len - 8);
	DWORD data = (DWORD)(len - 80);
	f.Seek(4, CFile::begin);
	f.Write(&riff, 4);
	f.Seek(76, CFile::begin);
	f.Write(&data, 4);
}

static void CdSanitizeName(TCHAR* s)
{
	if (!s) return;
	for (TCHAR* p = s; *p; ++p) {
		if (*p == _T('\\') || *p == _T('/') || *p == _T(':') || *p == _T('*') || *p == _T('?')
			|| *p == _T('"') || *p == _T('<') || *p == _T('>') || *p == _T('|'))
			*p = _T('_');
	}
	while (*s == _T(' ') || *s == _T('.')) {
		memmove(s, s + 1, (lstrlen(s)) * sizeof(TCHAR));
	}
	int n = lstrlen(s);
	while (n > 0 && (s[n - 1] == _T(' ') || s[n - 1] == _T('.'))) {
		s[--n] = 0;
	}
	if (s[0] == 0) lstrcpy(s, _T("track"));
}

static BOOL CdSha1(const BYTE* data, DWORD len, BYTE out[20])
{
	HCRYPTPROV prov = 0;
	HCRYPTHASH hash = 0;
	BOOL ok = FALSE;
	if (!CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		return FALSE;
	if (CryptCreateHash(prov, CALG_SHA1, 0, 0, &hash)) {
		if (CryptHashData(hash, data, len, 0)) {
			DWORD n = 20;
			ok = CryptGetHashParam(hash, HP_HASHVAL, out, &n, 0);
		}
		CryptDestroyHash(hash);
	}
	CryptReleaseContext(prov, 0);
	return ok;
}

static void CdMbBase64(const BYTE* sha, char* out)
{
	static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
	int o = 0;
	for (int i = 0; i < 18; i += 3) {
		const int v = (sha[i] << 16) | (sha[i + 1] << 8) | sha[i + 2];
		out[o++] = abc[(v >> 18) & 63];
		out[o++] = abc[(v >> 12) & 63];
		out[o++] = abc[(v >> 6) & 63];
		out[o++] = abc[v & 63];
	}
	const int v = (sha[18] << 16) | (sha[19] << 8);
	out[o++] = abc[(v >> 18) & 63];
	out[o++] = abc[(v >> 12) & 63];
	out[o++] = abc[(v >> 6) & 63];
	out[o++] = '-';
	out[o] = 0;
}

static void CdDiscId(int first, int last, const DWORD* startLba, int n, DWORD leadout, char* out)
{
	if (!out) return;
	out[0] = 0;
	if (!startLba || n <= 0) return;
	char ascii[820];
	int p = 0;
	wsprintfA(ascii + p, "%02X", first); p += 2;
	wsprintfA(ascii + p, "%02X", last); p += 2;
	DWORD offs[100];
	ZeroMemory(offs, sizeof(offs));
	offs[0] = leadout + 150;
	for (int i = 0; i < n && (first + i) < 100; ++i)
		offs[first + i] = startLba[i] + 150;
	for (int i = 0; i < 100; ++i) {
		wsprintfA(ascii + p, "%08X", offs[i]);
		p += 8;
	}
	BYTE sha[20];
	if (!CdSha1((const BYTE*)ascii, (DWORD)p, sha)) {
		out[0] = 0;
		return;
	}
	CdMbBase64(sha, out);
}

static unsigned CdCddbSum(unsigned n)
{
	unsigned s = 0;
	while (n > 0) { s += n % 10; n /= 10; }
	return s;
}

static void CdCddbId(int ntrk, const DWORD* startLba, DWORD leadout, char* out, DWORD* offs, int* nsecs)
{
	if (out) out[0] = 0;
	if (nsecs) *nsecs = 0;
	if (!out || !startLba || ntrk <= 0) return;
	unsigned n = 0;
	for (int i = 0; i < ntrk; ++i) {
		const DWORD fr = startLba[i] + 150;
		if (offs) offs[i] = fr;
		n += CdCddbSum(fr / 75);
	}
	const unsigned tot = (leadout + 150) / 75;
	if (nsecs) *nsecs = (int)tot;
	const unsigned id = ((n % 255) << 24) | (tot << 8) | (unsigned)ntrk;
	wsprintfA(out, "%08x", id);
}

static BOOL CdHttpGet(LPCWSTR url, BYTE* buf, int bufMax, int* got);

static BOOL CdHttpGetTry(LPCWSTR url, BYTE* buf, int bufMax, int* got)
{
	if (CdHttpGet(url, buf, bufMax, got)) return TRUE;
	if (url && _wcsnicmp(url, L"https://", 8) == 0) {
		WCHAR u2[2048];
		wsprintfW(u2, L"http://%s", url + 8);
		return CdHttpGet(u2, buf, bufMax, got);
	}
	return FALSE;
}

static BOOL CdLookupAbort(CCdPlayerDlg* self)
{
	return !self || !self->m_alive || InterlockedCompareExchange(&self->m_lookupStop, 0, 0) != 0;
}

static BOOL CddbGetField(const char* text, const char* key, char* dst, int dstMax)
{
	if (!text || !key || !dst || dstMax <= 0) return FALSE;
	dst[0] = 0;
	char pat[80];
	wsprintfA(pat, "%s=", key);
	const char* p = strstr(text, pat);
	if (!p) return FALSE;
	p += strlen(pat);
	int i = 0;
	while (*p && *p != '\r' && *p != '\n' && i < dstMax - 1)
		dst[i++] = *p++;
	dst[i] = 0;
	return i > 0;
}

static BOOL CddbParseHit(const char* line, char* cat, int catMax, char* id, int idMax)
{
	if (!line || !cat || !id) return FALSE;
	cat[0] = id[0] = 0;
	while (*line == ' ' || *line == '\t') line++;
	if (line[0] >= '0' && line[0] <= '9' && line[3] == ' ')
		line += 4;
	while (*line == ' ') line++;
	int i = 0;
	while (*line && *line != ' ' && i < catMax - 1) cat[i++] = *line++;
	cat[i] = 0;
	while (*line == ' ') line++;
	i = 0;
	while (*line && *line != ' ' && i < idMax - 1) id[i++] = *line++;
	id[i] = 0;
	return cat[0] && id[0];
}

static BOOL CdHttpGet(LPCWSTR url, BYTE* buf, int bufMax, int* got)
{
	*got = 0;
	if (!url || !url[0] || !buf || bufMax <= 1) return FALSE;
	HINTERNET ses = InternetOpen(L"oggYSEDbgm-CD/1.0 ( https://github.com/ )", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
	if (!ses) return FALSE;
	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
	if (_wcsnicmp(url, L"https://", 8) == 0)
		flags |= INTERNET_FLAG_SECURE;
	HINTERNET req = InternetOpenUrl(ses, url, NULL, 0, flags, 0);
	if (!req) {
		InternetCloseHandle(ses);
		return FALSE;
	}
	DWORD n = 0;
	int total = 0;
	while (total < bufMax - 1) {
		if (!InternetReadFile(req, buf + total, (DWORD)(bufMax - 1 - total), &n) || n == 0)
			break;
		total += (int)n;
	}
	buf[total] = 0;
	*got = total;
	InternetCloseHandle(req);
	InternetCloseHandle(ses);
	return total > 0;
}

static BOOL JsonGetStr(const char* json, const char* key, char* dst, int dstMax)
{
	char pat[80];
	wsprintfA(pat, "\"%s\":\"", key);
	const char* p = strstr(json, pat);
	if (!p) return FALSE;
	p += strlen(pat);
	int i = 0;
	while (*p && *p != '"' && i < dstMax - 1) {
		if (*p == '\\' && p[1]) { p++; dst[i++] = *p++; }
		else dst[i++] = *p++;
	}
	dst[i] = 0;
	return i > 0;
}

static const char* JsonBraceEnd(const char* p)
{
	if (!p || (*p != '{' && *p != '[')) return NULL;
	const char open = *p, close = (*p == '{') ? '}' : ']';
	int d = 0;
	BOOL inS = FALSE;
	for (const char* q = p; *q; ++q) {
		if (inS) {
			if (*q == '\\' && q[1]) { ++q; continue; }
			if (*q == '"') inS = FALSE;
			continue;
		}
		if (*q == '"') { inS = TRUE; continue; }
		if (*q == open) d++;
		else if (*q == close) { d--; if (d == 0) return q; }
	}
	return NULL;
}

static BOOL JsonGetStrRange(const char* a, const char* b, const char* key, char* dst, int dstMax)
{
	if (!dst || dstMax <= 0) return FALSE;
	dst[0] = 0;
	if (!a || !b || a >= b) return FALSE;
	char pat[80];
	wsprintfA(pat, "\"%s\":\"", key);
	const int plen = (int)strlen(pat);
	for (const char* p = a; p + plen <= b; ++p) {
		if (strncmp(p, pat, plen) != 0) continue;
		p += plen;
		int i = 0;
		while (p < b && *p && *p != '"' && i < dstMax - 1) {
			if (*p == '\\' && p + 1 < b) { p++; dst[i++] = *p++; }
			else dst[i++] = *p++;
		}
		dst[i] = 0;
		return i > 0;
	}
	return FALSE;
}

static BOOL JsonGetIntRange(const char* a, const char* b, const char* key, int* out)
{
	if (out) *out = 0;
	if (!a || !b || a >= b || !out) return FALSE;
	char pat[80];
	wsprintfA(pat, "\"%s\":", key);
	const int plen = (int)strlen(pat);
	for (const char* p = a; p + plen <= b; ++p) {
		if (strncmp(p, pat, plen) != 0) continue;
		p += plen;
		while (p < b && (*p == ' ' || *p == '\t')) p++;
		if (p >= b || *p < '0' || *p > '9') return FALSE;
		int v = 0;
		while (p < b && *p >= '0' && *p <= '9') {
			v = v * 10 + (*p - '0');
			p++;
		}
		*out = v;
		return TRUE;
	}
	return FALSE;
}

static void Utf8ToT(const char* u, TCHAR* d, int dmax)
{
	if (!d || dmax <= 0) return;
	d[0] = 0;
	if (!u || !u[0]) return;
	if (MultiByteToWideChar(CP_UTF8, 0, u, -1, d, dmax) <= 0)
		d[0] = 0;
	else
		d[dmax - 1] = 0;
}

static void UrlEnc(const TCHAR* in, TCHAR* out, int outMax)
{
	int o = 0;
	for (int i = 0; in[i] && o < outMax - 4; ++i) {
		const unsigned char c = (unsigned char)in[i] > 127 ? 0 : (unsigned char)in[i];
		WCHAR wc = in[i];
		if ((wc >= L'A' && wc <= L'Z') || (wc >= L'a' && wc <= L'z') || (wc >= L'0' && wc <= L'9')
			|| wc == L'-' || wc == L'_' || wc == L'.') {
			out[o++] = wc;
		} else {
			char u8[8];
			const int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, u8, 8, NULL, NULL);
			for (int k = 0; k < n && o < outMax - 4; ++k) {
				const unsigned char b = (unsigned char)u8[k];
				out[o++] = L'%';
				static const TCHAR hx[] = L"0123456789ABCDEF";
				out[o++] = hx[b >> 4];
				out[o++] = hx[b & 15];
			}
		}
	}
	out[o] = 0;
}

static BOOL CdMfToWav(LPCTSTR src, LPCTSTR wav)
{
	if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return FALSE;
	IMFSourceReader* rdr = NULL;
	BOOL ok = FALSE;
	if (SUCCEEDED(MFCreateSourceReaderFromURL(src, NULL, &rdr))) {
		IMFMediaType* mt = NULL;
		if (SUCCEEDED(MFCreateMediaType(&mt))) {
			mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
			mt->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
			mt->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
			mt->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
			mt->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
			mt->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
			mt->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 44100 * 4);
			if (SUCCEEDED(rdr->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mt))) {
				CFile f;
				if (f.Open(wav, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary)) {
					CdWriteWavHdr(f, 44100, 2, 16);
					for (;;) {
						DWORD flags = 0;
						IMFSample* smp = NULL;
						if (FAILED(rdr->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &flags, NULL, &smp)))
							break;
						if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
							if (smp) smp->Release();
							break;
						}
						if (smp) {
							IMFMediaBuffer* buf = NULL;
							if (SUCCEEDED(smp->ConvertToContiguousBuffer(&buf))) {
								BYTE* p = NULL; DWORD cb = 0;
								if (SUCCEEDED(buf->Lock(&p, NULL, &cb)) && p && cb) {
									f.Write(p, cb);
									buf->Unlock();
								}
								buf->Release();
							}
							smp->Release();
						}
					}
					CdFinishWavHdr(f);
					f.Close();
					ok = TRUE;
				}
			}
			mt->Release();
		}
		rdr->Release();
	}
	MFShutdown();
	return ok;
}

// ---- cover ----
IMPLEMENT_DYNAMIC(CCdCoverCtrl, CCustomStatic)

CCdCoverCtrl::CCdCoverCtrl() : m_bmp(NULL), m_w(0), m_h(0) {}
CCdCoverCtrl::~CCdCoverCtrl() { ClearImage(); }

void CCdCoverCtrl::ClearImage()
{
	if (m_bmp) { DeleteObject(m_bmp); m_bmp = NULL; }
	m_w = m_h = 0;
	if (GetSafeHwnd()) Invalidate(FALSE);
}

void CCdCoverCtrl::SetImage(HBITMAP hbmp)
{
	ClearImage();
	m_bmp = hbmp;
	if (m_bmp) {
		BITMAP bm = {};
		GetObject(m_bmp, sizeof(bm), &bm);
		m_w = bm.bmWidth; m_h = bm.bmHeight;
	}
	if (GetSafeHwnd()) Invalidate(FALSE);
}

void CCdCoverCtrl::PaintToDC(CDC& dc)
{
	CRect rc; GetClientRect(&rc);
	if (rc.Width() <= 0 || rc.Height() <= 0) return;
	dc.FillSolidRect(&rc, RGB(255, 228, 236));
	if (m_bmp) {
		CDC mem; mem.CreateCompatibleDC(&dc);
		HGDIOBJ old = mem.SelectObject(m_bmp);
		const int s = min(rc.Width(), rc.Height());
		const int x = rc.left + (rc.Width() - s) / 2;
		const int y = rc.top + (rc.Height() - s) / 2;
		dc.SetStretchBltMode(HALFTONE);
		dc.StretchBlt(x, y, s, s, &mem, 0, 0, m_w, m_h, SRCCOPY);
		mem.SelectObject(old);
		return;
	}
	const int cx = rc.left + rc.Width() / 2;
	const int cy = rc.top + rc.Height() / 2;
	const int r = min(rc.Width(), rc.Height()) / 2 - 4;
	if (r < 8) return;
	CPen pn(PS_SOLID, 1, RGB(180, 140, 150));
	CBrush brOuter(RGB(210, 205, 195));
	CBrush brRing(RGB(120, 118, 110));
	CBrush brInner(RGB(230, 228, 220));
	CBrush brHole(RGB(50, 50, 55));
	CPen* oldPn = dc.SelectObject(&pn);
	CBrush* oldBr = dc.SelectObject(&brOuter);
	dc.Ellipse(cx - r, cy - r, cx + r, cy + r);
	dc.SelectObject(&brRing);
	dc.Ellipse(cx - (r - 6), cy - (r - 6), cx + (r - 6), cy + (r - 6));
	dc.SelectObject(&brInner);
	dc.Ellipse(cx - (r - 10), cy - (r - 10), cx + (r - 10), cy + (r - 10));
	dc.SelectObject(&brHole);
	dc.Ellipse(cx - 10, cy - 10, cx + 10, cy + 10);
	dc.SelectObject(oldBr);
	dc.SelectObject(oldPn);
}

BEGIN_MESSAGE_MAP(CCdCoverCtrl, CCustomStatic)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_MESSAGE(WM_PRINTCLIENT, OnPrintClient)
END_MESSAGE_MAP()

void CCdCoverCtrl::OnPaint()
{
	CPaintDC dc(this);
	PaintToDC(dc);
}
BOOL CCdCoverCtrl::OnEraseBkgnd(CDC*) { return TRUE; }
LRESULT CCdCoverCtrl::OnPrintClient(WPARAM wParam, LPARAM)
{
	CDC dc; dc.Attach((HDC)wParam);
	PaintToDC(dc);
	dc.Detach();
	return 0;
}

// ---- help ----
class CCdHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_CD_HELP };
	explicit CCdHelpDlg(CWnd* p) : CDialog(IDD, p) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK() { DestroyWindow(); }
	virtual void OnCancel() { DestroyWindow(); }
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose() { DestroyWindow(); }
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CCdHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CCdHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	SetWindowText(LL14(L"CD 操作ガイド", L"CD Guide", L"Guide CD", L"Guida CD", L"Guía CD",
		L"CD 가이드", L"CD 指南", L"دليل القرص", L"Руководство CD", L"CD-Anleitung",
		L"Guia de CD", L"CD-gids", L"Przewodnik CD", L"CD kilavuzu"));
	if (CWnd* ok = GetDlgItem(IDOK))
		ok->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}
void CCdHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_cdHelp == this) g_cdHelp = NULL;
	delete this;
}
BOOL CCdHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}
void CCdHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp)) return;
	CDC& dc = hp.mem;
	dc.SetBkMode(TRANSPARENT);
	CFont* old = dc.SelectObject(GetFont());
	TEXTMETRIC tm = {}; dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + 1);
	int y = 8, x = 12;
	dc.SetTextColor(RGB(72, 48, 120));
	dc.TextOut(x, y, LL14(L"CDプレイヤー", L"CD player", L"Lecteur CD", L"Lettore CD", L"Reproductor CD",
		L"CD 플레이어", L"CD 播放器", L"مشغل أقراص", L"CD-плеер", L"CD-Player",
		L"Leitor de CD", L"CD-speler", L"Odtwarzacz CD", L"CD oynatici"));
	y += lh + 6;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, x, y, hp.rc.Width() - x * 2, 110, CCC_HELPDEMO_KWAVE);
	{
		const int dw = min(160, hp.rc.Width() / 4);
		GdiSoft2D::Context ctx;
		if (ctx.Create(dw, 90, false)) {
			ctx.Clear(RGB(248, 248, 252), 255);
			ctx.FillEllipse(dw / 2, 45, 40, 40, RGB(210, 205, 195), 255);
			ctx.FillEllipse(dw / 2, 45, 34, 34, RGB(90, 90, 95), 255);
			ctx.FillEllipse(dw / 2, 45, 8, 8, RGB(40, 40, 45), 255);
			ctx.Present(dc, hp.rc.right - dw - 16, 8);
		}
	}
	dc.SetTextColor(RGB(48, 48, 64));
	const wchar_t* lines[12];
	lines[0] = LL14(L"・下段の「CD」から開きます。ドライブを選び「読込」で目次を取ります。",
		L"· Open from the bottom-bar CD button. Pick a drive and Load the TOC.",
		L"· Ouvrez depuis le bouton CD. Choisissez le lecteur et chargez la TOC.",
		L"· Apri dal pulsante CD. Scegli il drive e carica la TOC.",
		L"· Abra desde el boton CD. Elija la unidad y cargue la TOC.",
		L"· 하단 CD 버튼으로 엽니다. 드라이브를 고르고 목차를 읽습니다.",
		L"· 从底栏「CD」打开。选择驱动器并读取目录。",
		L"· افتح من زر CD. اختر المحرك واقرأ الفهرس.",
		L"· Откройте кнопкой CD. Выберите привод и загрузите TOC.",
		L"· Uber die CD-Taste unten offnen. Laufwerk wahlen und TOC laden.",
		L"· Abra pelo botao CD. Escolha o drive e carregue a TOC.",
		L"· Open via de CD-knop. Kies het station en laad de TOC.",
		L"· Otworz przyciskiem CD. Wybierz naped i wczytaj TOC.",
		L"· Alt cubuk CD ile acin. Surucuyu secip TOC yukleyin.");
	lines[1] = LL14(L"· 再生／一時停止／停止／前後／音量。シーク横の A / B / 解除で A-B ループ（［ ］ でも可）。音量、リピート、シャッフルもあります。",
		L"· Play / pause / stop / prev / next / volume. A / B / Clear beside the seek bar set an A-B loop (or [ ]). Repeat and shuffle too.",
		L"· Lecture / pause / stop / prec. / suiv. / volume. A / B / Effacer a cote du seek font A-B (ou [ ]). Repetition et aleatoire aussi.",
		L"· Play / pausa / stop / prec. / succ. / volume. A / B / Cancella accanto al seek fanno A-B (o [ ]). Ripetizione e casuale anche.",
		L"· Reproducir / pausa / stop / ant. / sig. / volumen. A / B / Quitar junto al seek hacen A-B (o [ ]). Repeticion y aleatorio tambien.",
		L"· 재생/일시정지/정지/이전/다음/볼륨. 시크 옆 A / B / 해제로 A-B 루프([ ]도 가능). 반복과 셔플도 있습니다.",
		L"· 播放／暂停／停止／前后／音量。定位条旁 A / B / 解除做 A-B 循环（也可用 ［］）。也有循环和随机。",
		L"· تشغيل/إيقاف مؤقت/إيقاف/سابق/تالٍ/صوت. أزرار A / B / إلغاء بجانب الشريط لحلقة A-B (أو [ ]). التكرار والخلط أيضاً.",
		L"· Воспр. / пауза / стоп / пред. / след. / громкость. A / B / Сброс у полосы — петля A-B (или [ ]). Повтор и перемешивание тоже.",
		L"· Play / Pause / Stop / Zuruck / Vor / Lautstarke. A / B / Loeschen neben der Seekleiste setzen A-B (oder [ ]). Repeat und Zufall auch.",
		L"· Play / pausa / parar / ant. / prox. / volume. A / B / Limpar ao lado do seek fazem A-B (ou [ ]). Repetir e aleatorio tambem.",
		L"· Play / pauze / stop / vorige / volgende / volume. A / B / Wissen naast de seek maken A-B (of [ ]). Herhalen en shuffle ook.",
		L"· Odtwarzaj / pauza / stop / poprz. / nastep. / glosnosc. A / B / Wyczysc przy pasku daja A-B (lub [ ]). Powtorz i losowo tez.",
		L"· Oynat / duraklat / durdur / onceki / sonraki / ses. Seek yanindaki A / B / Temizle A-B yapar ([ ] de olur). Tekrar ve karistir da var.");
	lines[2] = LL14(L"・曲名・アルバム名・アーティストは直接編集でき、コピーして他の行やセルへ貼れます。CD-TEXT やネット検索の結果も上書きできます。",
		L"· Title, album and artist can be edited in place. Copy and paste across rows or cells. CD-TEXT and lookup results can be overwritten.",
		L"· Titre, album et artiste s'edite sur place. Copiez-collez vers d'autres lignes ou cellules. CD-TEXT et recherches restent ecrasables.",
		L"· Titolo, album e artista si modificano sul posto. Copia e incolla su altre righe o celle. CD-TEXT e ricerche si possono sovrascrivere.",
		L"· Titulo, album y artista se editan in situ. Copie y pegue en otras filas o celdas. CD-TEXT y busquedas se pueden sobrescribir.",
		L"· 곡명, 앨범, 아티스트를 바로 고치고 다른 행이나 셀에 붙여넣을 수 있습니다. CD-TEXT와 검색 결과도 덮어씁니다.",
		L"· 曲名、专辑、艺术家可直接改，并能复制到其他行或单元格。CD-TEXT 和联网查找的结果也能覆盖。",
		L"· يمكن تحرير العنوان والألبوم والفنان مباشرة ولصقها في صفوف أو خلايا أخرى. ويمكن استبدال CD-TEXT والبحث.",
		L"· Название, альбом и исполнитель правятся на месте. Копируйте в другие строки и ячейки. CD-TEXT и поиск можно перезаписать.",
		L"· Titel, Album und Interpret sind direkt editierbar. Kopieren in andere Zeilen oder Zellen. CD-TEXT und Suche lassen sich ueberschreiben.",
		L"· Titulo, album e artista editam-se no sitio. Copie para outras linhas ou celulas. CD-TEXT e buscas podem ser substituidos.",
		L"· Titel, album en artiest zijn ter plekke te bewerken. Kopieer naar andere rijen of cellen. CD-TEXT en zoekresultaten overschrijfbaar.",
		L"· Tytul, album i wykonawca edytujesz w miejscu. Kopiuj do innych wierszy lub komorek. CD-TEXT i wyszukiwanie mozna nadpisac.",
		L"· Baslik, album ve sanatci yerinde duzenlenir. Diger satir veya hucrelere yapistirin. CD-TEXT ve arama uzerine yazilabilir.");
	lines[3] = LL14(L"・取り込みは WAV / MP3 / FLAC。選択曲、全曲を1ファイルずつ、全曲を1本にまとめられます。",
		L"· Rip to WAV / MP3 / FLAC: selected tracks, all as separate files, or all as one file.",
		L"· Extraction WAV / MP3 / FLAC : selection, toutes separees, ou tout en un fichier.",
		L"· Estrazione WAV / MP3 / FLAC: selezionate, tutte separate, o tutte in un file.",
		L"· Extraccion WAV / MP3 / FLAC: seleccion, todas sueltas, o todas en un archivo.",
		L"· WAV/MP3/FLAC으로 추출. 선택 곡, 전곡 개별, 전곡 하나의 파일.",
		L"· 可抓成 WAV/MP3/FLAC：所选曲、每曲一个文件、或整盘合成一个文件。",
		L"· استخراج WAV/MP3/FLAC: المحدد، الكل منفصلاً، أو الكل في ملف واحد.",
		L"· Извлечение WAV/MP3/FLAC: выбранные, все по файлам, или всё в один файл.",
		L"· Rip nach WAV/MP3/FLAC: Auswahl, alle einzeln, oder alles in einer Datei.",
		L"· Extrair WAV/MP3/FLAC: selecionadas, todas separadas, ou todas num arquivo.",
		L"· Rippen naar WAV/MP3/FLAC: selectie, alle apart, of alles in een bestand.",
		L"· Zgrywanie WAV/MP3/FLAC: wybrane, wszystkie osobno, lub wszystko w jednym pliku.",
		L"· WAV/MP3/FLAC aktarim: secilenler, hepsi ayri, veya hepsi tek dosya.");
	lines[4] = LL14(L"・取り込んだファイルは「プレイリストへ追加」で本編のリストへ載せられます。",
		L"· Ripped files can be added to the main playlist with Add to playlist.",
		L"· Les fichiers extraits peuvent aller dans la playlist principale.",
		L"· I file estratti si possono aggiungere alla playlist principale.",
		L"· Los archivos extraidos se pueden anadir a la lista principal.",
		L"· 추출한 파일은 재생목록에 넣을 수 있습니다.",
		L"· 抓下来的文件可以用「加入播放列表」放到主列表。",
		L"· يمكن إضافة الملفات المستخرجة إلى قائمة التشغيل.",
		L"· Извлечённые файлы можно добавить в основной плейлист.",
		L"· Gerippte Dateien lassen sich der Hauptplaylist hinzufugen.",
		L"· Os arquivos extraidos podem ir para a playlist principal.",
		L"· Geripte bestanden kunnen naar de hoofdplaylist.",
		L"· Zgrane pliki mozna dodac do glownej listy.",
		L"· Aktarilan dosyalar ana listeye eklenebilir.");
	lines[5] = LL14(L"・書き込みは音声CD／データCD、RWの消去もここから。再生中の取り込みは一旦止めます。",
		L"· Burn audio or data CDs, or erase RW, from this window. Playback stops while ripping.",
		L"· Gravez audio/donnees ou effacez un RW ici. La lecture s'arrete pendant l'extraction.",
		L"· Masterizza audio/dati o cancella RW da qui. La riproduzione si ferma in estrazione.",
		L"· Grabe audio/datos o borre RW aqui. La reproduccion se detiene al extraer.",
		L"· 오디오/데이터 CD 굽기와 RW 지우기도 여기. 추출 중에는 재생을 멈춥니다.",
		L"· 可刻录音频/数据光盘或擦除 RW。抓轨时会先停止播放。",
		L"· انسخ صوت/بيانات أو امسح RW من هنا. يتوقف التشغيل أثناء الاستخراج.",
		L"· Запись Audio/Data и стирание RW. При извлечении воспроизведение останавливается.",
		L"· Audio-/Daten-CD brennen oder RW loschen. Beim Rippen stoppt die Wiedergabe.",
		L"· Grave audio/dados ou apague RW aqui. A leitura para durante a extracao.",
		L"· Brand audio/data of wis RW hier. Afspelen stopt tijdens rippen.",
		L"· Wypal audio/dane lub wyczysc RW. Podczas zgrywania odtwarzanie sie zatrzymuje.",
		L"· Ses/veri CD yazma ve RW silme. Aktarirken oynatma durur.");
	lines[6] = LL14(L"・右クリックでドライブ／再生／取り込み／書き込みがサブメニューにまとまっています。",
		L"· Right-click groups drive, playback, rip and burn into submenus.",
		L"· Clic droit : lecteurs, lecture, extraction et gravure en sous-menus.",
		L"· Clic destro: unita, riproduzione, estrazione e masterizzazione nei sottomenu.",
		L"· Clic derecho: unidad, reproduccion, extraccion y grabacion en submenus.",
		L"· 우클릭으로 드라이브/재생/추출/굽기가 하위 메뉴에 모입니다.",
		L"· 右键把驱动器、播放、抓轨、刻录收进子菜单。",
		L"· النقر الأيمن يجمع المحرك والتشغيل والاستخراج والنسخ في قوائم فرعية.",
		L"· ПКМ: привод, воспроизведение, извлечение и запись в подменю.",
		L"· Rechtsklick bundelt Laufwerk, Wiedergabe, Rip und Brennen in Untermenues.",
		L"· Clique direito agrupa drive, leitura, extracao e gravacao em submenus.",
		L"· Rechtsklik groepeert station, afspelen, rippen en branden in submenu's.",
		L"· PPM grupuje naped, odtwarzanie, zgrywanie i wypalanie w podmenu.",
		L"· Sag tik surucu, oynatma, aktarma ve yazmayi alt menulerde toplar.");
	lines[7] = LL14(L"・一度選んだ検索結果は TEMP に覚えます。キャッシュが無いときだけ取り直し、候補一覧が出ます。",
		L"· An applied lookup is cached in TEMP. With no cache, it fetches again and shows the match list.",
		L"· Un choix applique est mis en cache dans TEMP. Sans cache, nouvelle recherche et liste.",
		L"· Una scelta applicata resta in TEMP. Senza cache, nuova ricerca e elenco.",
		L"· Una eleccion aplicada se guarda en TEMP. Sin cache, busca de nuevo y muestra la lista.",
		L"· 한번 고른 검색은 TEMP에 남습니다. 캐시가 없으면 다시 받아 목록을 냅니다.",
		L"· 选过的查找会记在 TEMP。没有缓存时才再取，并列出候选。",
		L"· النتيجة المطبقة تُحفظ في TEMP. بلا ذاكرة مؤقتة يُعاد الجلب مع القائمة.",
		L"· Выбранный поиск помнится в TEMP. Без кэша — снова запрос и список.",
		L"· Eine angewandte Suche liegt in TEMP. Ohne Cache erneut holen und Liste zeigen.",
		L"· Uma escolha aplicada fica no TEMP. Sem cache, busca de novo e mostra a lista.",
		L"· Een toegepaste zoekactie staat in TEMP. Zonder cache opnieuw ophalen en lijst.",
		L"· Wybrany wynik zostaje w TEMP. Bez cache znow pobiera i pokazuje liste.",
		L"· Uygulanan arama TEMP'te tutulur. Onbellek yoksa yeniden alir ve liste gosterir.");
	for (int i = 0; i < 8; ++i) {
		CRect tr(x, y, hp.rc.right - x, y + lh * 2);
		dc.DrawText(lines[i], &tr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
		y += lh * 2;
	}
	dc.SelectObject(old);
	CCC_GdiHelpEndPaint(hp);
}

// ---- dialog ----
IMPLEMENT_DYNAMIC(CCdPlayerDlg, CCustomBlurDialogBase)

CCdPlayerDlg::CCdPlayerDlg(CWnd* pParent)
	: CCustomBlurDialogBase(IDD_CDPLAYER, pParent)
{
	m_hCd = INVALID_HANDLE_VALUE;
	InitializeCriticalSection(&m_cdCs);
	m_alive = 1;
	m_playStop = m_ripStop = m_lookupStop = m_burnStop = 0;
	m_playTh = m_ripTh = m_lookupTh = m_coverTh = m_burnTh = NULL;
	m_hwo = NULL;
	m_waveEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_trackN = m_firstTrack = 0;
	ZeroMemory(m_startLba, sizeof(m_startLba));
	ZeroMemory(m_endLba, sizeof(m_endLba));
	m_leadout = 0;
	ZeroMemory(m_title, sizeof(m_title));
	ZeroMemory(m_trArtist, sizeof(m_trArtist));
	ZeroMemory(m_isrc, sizeof(m_isrc));
	m_albumName[0] = m_albumArtist[0] = 0;
	m_discidA[0] = 0;
	m_cddbId[0] = 0;
	m_cddbNsec = 0;
	ZeroMemory(m_cddbOff, sizeof(m_cddbOff));
	m_mbid[0] = 0;
	m_curTrack = -1;
	m_listPlayTrack = -1;
	m_playLba = m_playEnd = 0;
	m_playFrames = 0;
	m_paused = FALSE;
	m_seekDrag = FALSE;
	m_seekDragTarget = 0;
	m_abA = m_abB = m_abTrack = -1;
	m_loopStartLba = m_loopEndLba = 0;
	m_lastTimeShown = -1;
	m_ripMode = m_ripN = 0;
	m_ripFolder[0] = 0;
	m_ripFmt = m_ripQual = 0;
	m_burnKind = m_burnN = 0;
	m_searchQ[0] = 0;
	m_statusBuf[0] = 0;
	m_coverJpg = NULL;
	m_coverJpgLen = 0;
	m_parentX = m_parentY = 0;
	m_lastTime[0] = 0;
	m_ready = FALSE;
	m_tocBusy = FALSE;
	m_fillingDrives = FALSE;
	m_mediaRetry = FALSE;
	m_candUi = NULL;
	m_candN = 0;
	m_cellEditing = FALSE;
	m_endingEdit = FALSE;
	m_editRow = m_editCol = -1;
	m_cellRow = m_cellCol = -1;
	m_pendingRow = m_pendingCol = -1;
	m_clickRow = m_clickCol = -1;
	m_clickWasSel = FALSE;
}

CCdPlayerDlg::~CCdPlayerDlg()
{
	if (m_coverJpg) { delete[] m_coverJpg; m_coverJpg = NULL; }
	if (m_waveEvt) CloseHandle(m_waveEvt);
	DeleteCriticalSection(&m_cdCs);
}

void CCdPlayerDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CD_HELP, m_help);
	DDX_Control(pDX, IDC_CD_CLOSE, m_close);
	DDX_Control(pDX, IDC_CD_REFRESH, m_refresh);
	DDX_Control(pDX, IDC_CD_EJECT, m_eject);
	DDX_Control(pDX, IDC_CD_LOAD, m_load);
	DDX_Control(pDX, IDC_CD_PLAY, m_play);
	DDX_Control(pDX, IDC_CD_PAUSE, m_pause);
	DDX_Control(pDX, IDC_CD_STOP, m_stop);
	DDX_Control(pDX, IDC_CD_PREV, m_prev);
	DDX_Control(pDX, IDC_CD_NEXT, m_next);
	DDX_Control(pDX, IDC_CD_ABA, m_abABtn);
	DDX_Control(pDX, IDC_CD_ABB, m_abBBtn);
	DDX_Control(pDX, IDC_CD_ABCLR, m_abClrBtn);
	DDX_Control(pDX, IDC_CD_LOOKUP, m_lookup);
	DDX_Control(pDX, IDC_CD_SEARCHGO, m_searchGo);
	DDX_Control(pDX, IDC_CD_BROWSE, m_browse);
	DDX_Control(pDX, IDC_CD_RIPSEL, m_ripSel);
	DDX_Control(pDX, IDC_CD_RIPALL, m_ripAll);
	DDX_Control(pDX, IDC_CD_RIPONE, m_ripOne);
	DDX_Control(pDX, IDC_CD_BURNAUD, m_burnAudio);
	DDX_Control(pDX, IDC_CD_BURNDATA, m_burnData);
	DDX_Control(pDX, IDC_CD_ERASE, m_erase);
	DDX_Control(pDX, IDC_CD_DRIVE, m_drive);
	DDX_Control(pDX, IDC_CD_FMT, m_fmt);
	DDX_Control(pDX, IDC_CD_QUAL, m_qual);
	DDX_Control(pDX, IDC_CD_LIST, m_list);
	DDX_Control(pDX, IDC_CD_COVER, m_cover);
	DDX_Control(pDX, IDC_CD_ALBUM, m_album);
	DDX_Control(pDX, IDC_CD_ARTIST, m_artist);
	DDX_Control(pDX, IDC_CD_DISCID, m_discid);
	DDX_Control(pDX, IDC_CD_TIME, m_time);
	DDX_Control(pDX, IDC_CD_STATUS, m_status);
	DDX_Control(pDX, IDC_CD_DRIVEL, m_driveL);
	DDX_Control(pDX, IDC_CD_FMTL, m_fmtL);
	DDX_Control(pDX, IDC_CD_QUALL, m_qualL);
	DDX_Control(pDX, IDC_CD_FOLDERL, m_folderL);
	DDX_Control(pDX, IDC_CD_VOLL, m_volL);
	DDX_Control(pDX, IDC_CD_SEEK, m_seek);
	DDX_Control(pDX, IDC_CD_VOL, m_vol);
	DDX_Control(pDX, IDC_CD_REPEAT, m_repeat);
	DDX_Control(pDX, IDC_CD_SHUFFLE, m_shuffle);
	DDX_Control(pDX, IDC_CD_ADDPL, m_addPl);
	DDX_Control(pDX, IDC_CD_SEARCH, m_search);
	DDX_Control(pDX, IDC_CD_FOLDER, m_folder);
}

BEGIN_MESSAGE_MAP(CCdPlayerDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_CD_HELP, OnHelp)
	ON_BN_CLICKED(IDC_CD_CLOSE, OnCloseButton)
	ON_BN_CLICKED(IDC_CD_REFRESH, OnRefresh)
	ON_BN_CLICKED(IDC_CD_EJECT, OnEject)
	ON_BN_CLICKED(IDC_CD_LOAD, OnLoad)
	ON_BN_CLICKED(IDC_CD_PLAY, OnPlay)
	ON_BN_CLICKED(IDC_CD_PAUSE, OnPause)
	ON_BN_CLICKED(IDC_CD_STOP, OnStop)
	ON_BN_CLICKED(IDC_CD_PREV, OnPrev)
	ON_BN_CLICKED(IDC_CD_NEXT, OnNext)
	ON_BN_CLICKED(IDC_CD_ABA, OnAbA)
	ON_BN_CLICKED(IDC_CD_ABB, OnAbB)
	ON_BN_CLICKED(IDC_CD_ABCLR, OnAbClr)
	ON_BN_CLICKED(IDC_CD_LOOKUP, OnLookup)
	ON_BN_CLICKED(IDC_CD_SEARCHGO, OnSearchGo)
	ON_BN_CLICKED(IDC_CD_BROWSE, OnBrowse)
	ON_BN_CLICKED(IDC_CD_RIPSEL, OnRipSel)
	ON_BN_CLICKED(IDC_CD_RIPALL, OnRipAll)
	ON_BN_CLICKED(IDC_CD_RIPONE, OnRipOne)
	ON_BN_CLICKED(IDC_CD_BURNAUD, OnBurnAudio)
	ON_BN_CLICKED(IDC_CD_BURNDATA, OnBurnData)
	ON_BN_CLICKED(IDC_CD_ERASE, OnErase)
	ON_CBN_SELCHANGE(IDC_CD_FMT, OnFmtChange)
	ON_CBN_SELCHANGE(IDC_CD_DRIVE, OnDriveChange)
	ON_NOTIFY(NM_CLICK, IDC_CD_LIST, OnListClick)
	ON_NOTIFY(NM_DBLCLK, IDC_CD_LIST, OnListDblClk)
	ON_EN_KILLFOCUS(IDC_CD_ALBUM, OnAlbumKillFocus)
	ON_EN_KILLFOCUS(IDC_CD_ARTIST, OnArtistKillFocus)
	ON_EN_KILLFOCUS(IDC_CD_CELLEDIT, OnCellKillFocus)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_HSCROLL()
	ON_WM_CONTEXTMENU()
	ON_WM_DEVICECHANGE()
	ON_MESSAGE(WM_CD_POS, OnPosMsg)
	ON_MESSAGE(WM_CD_ENDED, OnEndedMsg)
	ON_MESSAGE(WM_CD_RIPPROG, OnRipProgMsg)
	ON_MESSAGE(WM_CD_RIPDONE, OnRipDoneMsg)
	ON_MESSAGE(WM_CD_LOOKUP, OnLookupMsg)
	ON_MESSAGE(WM_CD_COVER, OnCoverMsg)
	ON_MESSAGE(WM_CD_BURNPROG, OnBurnProgMsg)
	ON_MESSAGE(WM_CD_BURNDONE, OnBurnDoneMsg)
	ON_MESSAGE(WM_CD_LOADTOC, OnLoadTocMsg)
	ON_MESSAGE(WM_CD_MEDIA, OnMediaMsg)
END_MESSAGE_MAP()

void CCdPlayerDlg::LayoutHelpBtn() { CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help); }
void CCdPlayerDlg::LayoutHelpBtnAndCaption()
{
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
}

int CCdPlayerDlg::CurrentDriveLetter() const
{
	CString t;
	if (m_drive.GetSafeHwnd()) m_drive.GetWindowText(t);
	if (t.GetLength() >= 1) return (int)t[0];
	return 0;
}

HANDLE CCdPlayerDlg::OpenCdHandle()
{
	const int letter = CurrentDriveLetter();
	if (!letter) return INVALID_HANDLE_VALUE;
	TCHAR path[8];
	wsprintf(path, _T("\\\\.\\%c:"), letter);
	return CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
}

void CCdPlayerDlg::CloseCdHandle()
{
	EnterCriticalSection(&m_cdCs);
	if (m_hCd && m_hCd != INVALID_HANDLE_VALUE) {
		CloseHandle(m_hCd);
		m_hCd = INVALID_HANDLE_VALUE;
	}
	LeaveCriticalSection(&m_cdCs);
}

void CCdPlayerDlg::SetStatus(LPCTSTR text)
{
	if (text) lstrcpyn(m_statusBuf, text, 256);
	if (m_status.GetSafeHwnd())
		m_status.SetWindowText(m_statusBuf);
}

void CCdPlayerDlg::ApplyLang()
{
	SetWindowText(LL14(L"CD", L"CD", L"CD", L"CD", L"CD", L"CD", L"CD", L"CD", L"CD", L"CD", L"CD", L"CD", L"CD", L"CD"));
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	{
		CCustomStandardButton* bt[21] = {
			&m_close, &m_refresh, &m_eject, &m_load, &m_play, &m_pause, &m_stop, &m_prev, &m_next,
			&m_abABtn, &m_abBBtn, &m_abClrBtn,
			&m_lookup, &m_searchGo, &m_browse, &m_ripSel, &m_ripAll, &m_ripOne, &m_burnAudio, &m_burnData, &m_erase
		};
		for (int i = 0; i < 21; ++i) {
			bt[i]->SetFlat(TRUE);
			bt[i]->SetGradation(RGB(250, 248, 240), RGB(200, 190, 160), 0, TRUE);
		}
	}
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_refresh.SetWindowText(LL14(L"更新", L"Refresh", L"Actualiser", L"Aggiorna", L"Actualizar", L"새로고침", L"刷新", L"تحديث",
		L"Обновить", L"Aktualisieren", L"Atualizar", L"Vernieuwen", L"Odswiez", L"Yenile"));
	m_eject.SetWindowText(LL14(L"取り出し", L"Eject", L"Ejecter", L"Espelli", L"Expulsar", L"꺼내기", L"弹出", L"إخراج",
		L"Извлечь", L"Auswerfen", L"Ejetar", L"Uitwerpen", L"Wysun", L"Cikar"));
	m_load.SetWindowText(LL14(L"読込", L"Load", L"Charger", L"Carica", L"Cargar", L"읽기", L"读取", L"تحميل",
		L"Загрузить", L"Laden", L"Carregar", L"Laden", L"Wczytaj", L"Yukle"));
	m_play.SetWindowText(LL14(L"再生", L"Play", L"Lecture", L"Play", L"Reproducir", L"재생", L"播放", L"تشغيل",
		L"Играть", L"Play", L"Play", L"Play", L"Odtwarzaj", L"Oynat"));
	m_pause.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف مؤقت",
		L"Пауза", L"Pause", L"Pausa", L"Pauze", L"Pauza", L"Duraklat"));
	m_stop.SetWindowText(LL14(L"停止", L"Stop", L"Stop", L"Stop", L"Detener", L"정지", L"停止", L"إيقاف",
		L"Стоп", L"Stopp", L"Parar", L"Stop", L"Stop", L"Durdur"));
	m_prev.SetWindowText(LL14(L"前へ", L"Prev", L"Prec.", L"Prec.", L"Ant.", L"이전", L"上一首", L"السابق",
		L"Пред.", L"Zuruck", L"Ant.", L"Vorige", L"Poprz.", L"Onceki"));
	m_next.SetWindowText(LL14(L"次へ", L"Next", L"Suiv.", L"Succ.", L"Sig.", L"다음", L"下一首", L"التالي",
		L"След.", L"Vor", L"Prox.", L"Volgende", L"Nastep.", L"Sonraki"));
	m_abABtn.SetWindowText(L"A");
	m_abBBtn.SetWindowText(L"B");
	m_abClrBtn.SetWindowText(_T("A-B×"));
	RefreshAbButtons();
	m_lookup.SetWindowText(LL14(L"ネット検索", L"Lookup", L"Recherche", L"Cerca", L"Buscar", L"검색", L"联网查找", L"بحث",
		L"Поиск", L"Suchen", L"Buscar", L"Opzoeken", L"Szukaj", L"Ara"));
	m_searchGo.SetWindowText(LL14(L"検索", L"Search", L"Chercher", L"Cerca", L"Buscar", L"찾기", L"搜索", L"بحث",
		L"Искать", L"Suche", L"Pesquisar", L"Zoek", L"Szukaj", L"Ara"));
	m_browse.SetWindowText(LL14(L"参照", L"Browse", L"Parcourir", L"Sfoglia", L"Examinar", L"찾아보기", L"浏览", L"استعراض",
		L"Обзор", L"Durchsuchen", L"Procurar", L"Bladeren", L"Przegladaj", L"Gozat"));
	m_ripSel.SetWindowText(LL14(L"選択を取り込み", L"Rip selected", L"Extraire selection", L"Estrai selezionate", L"Extraer seleccion",
		L"선택 추출", L"抓取所选", L"استخراج المحدد", L"Извлечь выбранные", L"Auswahl rippen",
		L"Extrair selecionadas", L"Selectie rippen", L"Zgraj zaznaczone", L"Secileni aktar"));
	m_ripAll.SetWindowText(LL14(L"全曲を1曲ずつ", L"Rip all separately", L"Tout separe", L"Tutte separate", L"Todas sueltas",
		L"전곡 개별", L"整盘分轨", L"الكل منفصلاً", L"Все по файлам", L"Alle einzeln",
		L"Todas separadas", L"Alle apart", L"Wszystkie osobno", L"Hepsi ayri"));
	m_ripOne.SetWindowText(LL14(L"全曲を1本に", L"Rip all as one", L"Tout en un", L"Tutto in uno", L"Todo en uno",
		L"전곡 하나로", L"整盘合成", L"الكل في ملف", L"Всё в один", L"Alles in einer Datei",
		L"Tudo em um", L"Alles in een", L"Wszystko w jednym", L"Hepsi tek dosya"));
	m_burnAudio.SetWindowText(LL14(L"音声CD書き込み", L"Burn audio CD", L"Graver CD audio", L"Masterizza CD audio", L"Grabar CD audio",
		L"오디오 CD 굽기", L"刻录音频光盘", L"نسخ قرص صوتي", L"Записать Audio CD", L"Audio-CD brennen",
		L"Gravar CD de audio", L"Audio-CD branden", L"Wypal CD audio", L"Ses CD yaz"));
	m_burnData.SetWindowText(LL14(L"データCD書き込み", L"Burn data CD", L"Graver CD donnees", L"Masterizza CD dati", L"Grabar CD datos",
		L"데이터 CD 굽기", L"刻录数据光盘", L"نسخ قرص بيانات", L"Записать Data CD", L"Daten-CD brennen",
		L"Gravar CD de dados", L"Data-CD branden", L"Wypal CD danych", L"Veri CD yaz"));
	m_erase.SetWindowText(LL14(L"RW消去", L"Erase RW", L"Effacer RW", L"Cancella RW", L"Borrar RW",
		L"RW 지우기", L"擦除 RW", L"مسح RW", L"Стереть RW", L"RW loschen",
		L"Apagar RW", L"RW wissen", L"Wyczysc RW", L"RW sil"));
	m_repeat.SetWindowText(LL14(L"リピート", L"Repeat", L"Repeter", L"Ripeti", L"Repetir", L"반복", L"循环", L"تكرار",
		L"Повтор", L"Wiederholen", L"Repetir", L"Herhalen", L"Powtarzaj", L"Tekrar"));
	m_shuffle.SetWindowText(LL14(L"シャッフル", L"Shuffle", L"Aleatoire", L"Casuale", L"Aleatorio", L"셔플", L"随机", L"خلط",
		L"Перемешать", L"Zufall", L"Aleatorio", L"Shuffle", L"Losowo", L"Karistir"));
	m_addPl.SetWindowText(LL14(L"プレイリストへ追加", L"Add to playlist", L"Ajouter a la playlist", L"Aggiungi alla playlist", L"Anadir a la lista",
		L"재생목록에 추가", L"加入播放列表", L"إضافة للقائمة", L"В плейлист", L"Zur Playlist",
		L"Adicionar a playlist", L"Naar playlist", L"Dodaj do listy", L"Listeye ekle"));
	m_driveL.SetWindowText(LL14(L"ドライブ", L"Drive", L"Lecteur", L"Unita", L"Unidad", L"드라이브", L"驱动器", L"محرك",
		L"Привод", L"Laufwerk", L"Unidade", L"Station", L"Naped", L"Surucu"));
	m_fmtL.SetWindowText(LL14(L"形式", L"Format", L"Format", L"Formato", L"Formato", L"형식", L"格式", L"تنسيق",
		L"Формат", L"Format", L"Formato", L"Formaat", L"Format", L"Bicim"));
	m_qualL.SetWindowText(LL14(L"品質", L"Quality", L"Qualite", L"Qualita", L"Calidad", L"품질", L"质量", L"جودة",
		L"Качество", L"Qualitat", L"Qualidade", L"Kwaliteit", L"Jakosc", L"Kalite"));
	m_folderL.SetWindowText(LL14(L"保存先", L"Folder", L"Dossier", L"Cartella", L"Carpeta", L"폴더", L"文件夹", L"مجلد",
		L"Папка", L"Ordner", L"Pasta", L"Map", L"Folder", L"Klasor"));
	m_volL.SetWindowText(LL14(L"音量", L"Vol", L"Vol", L"Vol", L"Vol", L"볼륨", L"音量", L"صوت",
		L"Громк.", L"Laut", L"Vol", L"Vol", L"Glosn.", L"Ses"));
}

void CCdPlayerDlg::RefreshRipQuality()
{
	m_qual.ResetContent();
	m_qual.EnableWindow(TRUE);
	const int fmt = m_fmt.GetCurSel();
	if (fmt <= 0) {
		m_qual.AddString(L"PCM 16-bit");
		m_qual.SetCurSel(0);
		return;
	}
	if (fmt == 1) {
		m_qual.AddString(L"128 kbps");
		m_qual.AddString(L"192 kbps");
		m_qual.AddString(L"256 kbps");
		m_qual.AddString(L"320 kbps");
		int s = 1;
		if (savedata.cdRipQual == 128) s = 0;
		else if (savedata.cdRipQual == 256) s = 2;
		else if (savedata.cdRipQual == 320) s = 3;
		m_qual.SetCurSel(s);
	} else {
		for (int i = 0; i <= 8; ++i) {
			TCHAR b[24];
			wsprintf(b, _T("%d"), i);
			m_qual.AddString(b);
		}
		int s = savedata.cdRipQual;
		if (s < 0 || s > 8) s = 5;
		m_qual.SetCurSel(s);
	}
}

BOOL CCdPlayerDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	EnableMainWindowLock(&savedata.cdMainLock);
	CCC_BringDialogToForeground(this);
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	ApplyLang();

	m_drive.SetAeroMode(FALSE);
	m_fmt.SetAeroMode(FALSE);
	m_qual.SetAeroMode(FALSE);
	m_list.SetAeroMode(FALSE);
	m_search.SetAeroMode(FALSE);
	m_folder.SetAeroMode(FALSE);
	m_abABtn.SetAeroMode(FALSE);
	m_abBBtn.SetAeroMode(FALSE);
	m_abClrBtn.SetAeroMode(FALSE);
	m_repeat.SetAeroMode(FALSE);
	m_shuffle.SetAeroMode(FALSE);
	m_addPl.SetAeroMode(FALSE);
	m_cover.SetAeroMode(FALSE);
	m_album.SetAeroMode(FALSE);
	m_artist.SetAeroMode(FALSE);
	{
		CCustomStatic* lab[8] = {
			&m_discid, &m_time, &m_status,
			&m_driveL, &m_fmtL, &m_qualL, &m_folderL, &m_volL
		};
		for (int i = 0; i < 8; ++i) {
			lab[i]->SetAeroMode(FALSE);
			lab[i]->SetSolidFill(TRUE, COLOR_DIALOG_BG);
		}
	}
	m_seek.SetAeroMode(TRUE);
	m_vol.SetAeroMode(TRUE);

	m_list.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
	m_list.InsertColumn(0, LL14(L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#"), LVCFMT_LEFT, 36);
	m_list.InsertColumn(1, LL14(L"曲名", L"Title", L"Titre", L"Titolo", L"Titulo", L"제목", L"曲名", L"عنوان", L"Название", L"Titel", L"Titulo", L"Titel", L"Tytul", L"Baslik"), LVCFMT_LEFT, 240);
	m_list.InsertColumn(2, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"فنان", L"Исполнитель", L"Interpret", L"Artista", L"Artiest", L"Wykonawca", L"Sanatci"), LVCFMT_LEFT, 160);
	m_list.InsertColumn(3, LL14(L"時間", L"Time", L"Duree", L"Durata", L"Tiempo", L"시간", L"时间", L"مدة", L"Время", L"Dauer", L"Tempo", L"Tijd", L"Czas", L"Sure"), LVCFMT_LEFT, 56);
	m_list.InsertColumn(4, L"ISRC", LVCFMT_LEFT, 110);

	m_seek.SetRange(0, 1000);
	m_seek.SetSelection(0, 0);
	m_seek.SetSelectionLocked(TRUE);
	m_seek.SetAB(-1, -1);
	m_seek.SetTimeBaseHz(75);
	m_vol.SetRange(0, 100);
	int vol = savedata.cdVolume;
	if (vol < 0 || vol > 100) vol = 80;
	m_vol.SetPos(vol);

	m_fmt.ResetContent();
	m_fmt.AddString(L"WAV");
	m_fmt.AddString(L"MP3");
	m_fmt.AddString(L"FLAC");
	int fm = savedata.cdRipFmt;
	if (fm < 0 || fm > 2) fm = 0;
	m_fmt.SetCurSel(fm);
	RefreshRipQuality();

	if (savedata.cdRipFolder[0])
		m_folder.SetWindowText(savedata.cdRipFolder);
	else {
		TCHAR mus[MAX_PATH] = {};
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_MYMUSIC, NULL, SHGFP_TYPE_CURRENT, mus))) {
			lstrcat(mus, _T("\\CD"));
			m_folder.SetWindowText(mus);
		}
	}
	m_repeat.SetCheck(savedata.cdRepeat ? BST_CHECKED : BST_UNCHECKED);
	m_shuffle.SetCheck(savedata.cdShuffle ? BST_CHECKED : BST_UNCHECKED);
	m_addPl.SetCheck(savedata.cdRipAddPl ? BST_CHECKED : BST_UNCHECKED);

	CRect prc; GetClientRect(&prc);
	m_progress.Create(WS_CHILD | WS_VISIBLE, CRect(0, 0, 10, 10), this, IDC_CD_PROGRESS);
	m_progress.SetAeroMode(TRUE);
	m_progress.SetRange(0, 100);
	m_progress.SetPos(0);

	LayoutHelpBtnAndCaption();
	FillDrives();
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_help, LL14(L"この窓の操作ガイドを開きます", L"Open this window's guide", L"Ouvrir le guide", L"Apri la guida", L"Abrir la guia", L"이 창의 가이드를 엽니다", L"打开本窗口的操作指南", L"فتح دليل هذه النافذة", L"Открыть руководство", L"Anleitung oeffnen", L"Abrir o guia", L"Gids openen", L"Otworz przewodnik", L"Kilavuzu ac"));
		m_tooltip.AddTool(&m_close, LL14(L"CDプレイヤーを閉じます", L"Close the CD player", L"Fermer le lecteur CD", L"Chiudi il lettore CD", L"Cerrar el reproductor CD", L"CD 플레이어를 닫습니다", L"关闭 CD 播放器", L"إغلاق مشغل الأقراص", L"Закрыть CD-плеер", L"CD-Player schliessen", L"Fechar o leitor de CD", L"CD-speler sluiten", L"Zamknij odtwarzacz CD", L"CD oynaticiyi kapat"));
		m_tooltip.AddTool(&m_refresh, LL14(L"ドライブ一覧を更新します", L"Refresh the drive list", L"Actualiser la liste des lecteurs", L"Aggiorna l'elenco unita", L"Actualizar la lista de unidades", L"드라이브 목록을 새로고칩니다", L"刷新驱动器列表", L"تحديث قائمة المحركات", L"Обновить список приводов", L"Laufwerksliste aktualisieren", L"Atualizar a lista de drives", L"Stationslijst vernieuwen", L"Odswiez liste napedow", L"Surucu listesini yenile"));
		m_tooltip.AddTool(&m_eject, LL14(L"トレイを開けます", L"Open the tray", L"Ouvrir le tiroir", L"Apri il vassoio", L"Abrir la bandeja", L"트레이를 엽니다", L"打开托盘", L"فتح الدرج", L"Открыть лоток", L"Schublade oeffnen", L"Abrir a bandeja", L"Lade openen", L"Otworz tacke", L"Tepsiyi ac"));
		m_tooltip.AddTool(&m_load, LL14(L"ディスクの目次を読みます", L"Read the disc table of contents", L"Lire la TOC", L"Leggi la TOC", L"Leer la TOC", L"디스크 목차 읽기", L"读取光盘目录", L"قراءة فهرس القرص", L"Прочитать TOC", L"TOC lesen", L"Ler a TOC", L"TOC lezen", L"Wczytaj TOC", L"TOC oku"));
		m_tooltip.AddTool(&m_play, LL14(L"選択した曲を再生します", L"Play the selected track", L"Lire la piste selectionnee", L"Riproduci la traccia selezionata", L"Reproducir la pista seleccionada", L"선택한 곡을 재생합니다", L"播放所选曲目", L"تشغيل المقطع المحدد", L"Играть выбранный трек", L"Auswahl wiedergeben", L"Tocar a faixa selecionada", L"Selectie afspelen", L"Odtwarzaj zaznaczone", L"Secilen parcayi oynat"));
		m_tooltip.AddTool(&m_pause, LL14(L"再生を一時停止／再開します", L"Pause or resume playback", L"Pause ou reprise", L"Pausa o riprendi", L"Pausar o reanudar", L"재생을 일시정지하거나 재개합니다", L"暂停或继续播放", L"إيقاف مؤقت أو استئناف", L"Пауза или продолжить", L"Pause oder fortsetzen", L"Pausar ou retomar", L"Pauzeren of hervatten", L"Pauza lub wznowienie", L"Duraklat veya surdur"));
		m_tooltip.AddTool(&m_stop, LL14(L"再生を止めます", L"Stop playback", L"Arreter la lecture", L"Ferma la riproduzione", L"Detener la reproduccion", L"재생을 멈춥니다", L"停止播放", L"إيقاف التشغيل", L"Остановить воспроизведение", L"Wiedergabe stoppen", L"Parar a reproducao", L"Afspelen stoppen", L"Zatrzymaj odtwarzanie", L"Oynatmayı durdur"));
		m_tooltip.AddTool(&m_prev, LL14(L"前の曲へ", L"Previous track", L"Piste precedente", L"Traccia precedente", L"Pista anterior", L"이전 곡", L"上一首", L"المقطع السابق", L"Предыдущий трек", L"Vorheriger Titel", L"Faixa anterior", L"Vorige track", L"Poprzedni utwor", L"Onceki parca"));
		m_tooltip.AddTool(&m_next, LL14(L"次の曲へ", L"Next track", L"Piste suivante", L"Traccia successiva", L"Pista siguiente", L"다음 곡", L"下一首", L"المقطع التالي", L"Следующий трек", L"Naechster Titel", L"Faixa seguinte", L"Volgende track", L"Nastepny utwor", L"Sonraki parca"));
		m_tooltip.AddTool(&m_abABtn, LL14(L"いまの位置を A 点にします。［ キーでも可。もう一度［］で解除", L"Set A at the playhead. Also [ key. [ or ] again clears", L"Poser A ici. Touche [ aussi. [ ou ] encore pour effacer", L"Imposta A qui. Anche tasto [. [ o ] di nuovo toglie", L"Poner A aqui. Tambien tecla [. [ o ] otra vez quita", L"지금 위치를 A로. [ 키도 가능. 다시 [ ] 로 해제", L"把当前位置设为 A。也可用 ［。再按 ［］ 解除", L"تعيين A هنا. مفتاح [ أيضاً. [ أو ] مرة أخرى للإلغاء", L"Поставить A здесь. Клавиша [ тоже. Ещё раз [ ] снимает", L"A hier setzen. Auch Taste [. Nochmal [ ] loescht", L"Definir A aqui. Tecla [ tambem. [ ou ] de novo limpa", L"A hier zetten. Ook toets [. [ of ] opnieuw wist", L"Ustaw A tutaj. Takze klawisz [. Ponowne [ ] kasuje", L"Burayi A yap. [ tusu da olur. Tekrar [ ] kaldirir"));
		m_tooltip.AddTool(&m_abBBtn, LL14(L"いまの位置を B 点にして A-B ループを開始します。］ キーでも可", L"Set B at the playhead and start A-B loop. Also ] key", L"Poser B ici et lancer A-B. Touche ] aussi", L"Imposta B qui e avvia A-B. Anche tasto ]", L"Poner B aqui y empezar A-B. Tambien tecla ]", L"지금 위치를 B로 하고 A-B 루프 시작. ] 키도 가능", L"把当前位置设为 B 并开始 A-B 循环。也可用 ］", L"تعيين B هنا وبدء حلقة A-B. مفتاح ] أيضاً", L"Поставить B здесь и начать A-B. Клавиша ] тоже", L"B hier setzen und A-B starten. Auch Taste ]", L"Definir B aqui e iniciar A-B. Tecla ] tambem", L"B hier zetten en A-B starten. Ook toets ]", L"Ustaw B tutaj i uruchom A-B. Takze klawisz ]", L"Burayi B yap ve A-B baslat. ] tusu da olur"));
		m_tooltip.AddTool(&m_abClrBtn, LL14(L"A-B ループを解除します。［ ］ Backspace Delete でも可", L"Clear the A-B loop. Also [, ], Backspace or Delete", L"Effacer la boucle A-B. Aussi [, ], Retour ou Suppr", L"Togli il loop A-B. Anche [, ], Backspace o Canc", L"Quitar el bucle A-B. Tambien [, ], Retroceso o Supr", L"A-B 루프 해제. [ ] Backspace Delete 도 가능", L"解除 A-B 循环。也可用 ［］、Backspace、Delete", L"إلغاء حلقة A-B. أيضاً [ ] Backspace أو Delete", L"Снять петлю A-B. Также [, ], Backspace или Delete", L"A-B-Schleife loeschen. Auch [, ], Backspace oder Entf", L"Limpar o loop A-B. Tambem [, ], Backspace ou Delete", L"A-B-lus wissen. Ook [, ], Backspace of Delete", L"Wyczysc petle A-B. Takze [, ], Backspace lub Delete", L"A-B dongusunu kaldir. [, ], Backspace veya Delete de olur"));
		m_tooltip.AddTool(&m_lookup, LL14(L"MusicBrainz / GnuDB / iTunes / Deezer を試し、候補から選びます。一度適用した内容は TEMP に覚え、無ければまた取ります", L"Try MusicBrainz, GnuDB, iTunes and Deezer, then pick. An applied match is cached in TEMP; without a cache it fetches again", L"Essayer MusicBrainz, GnuDB, iTunes et Deezer. Un choix va dans TEMP ; sans cache, nouvelle recherche", L"Prova MusicBrainz, GnuDB, iTunes e Deezer. La scelta resta in TEMP; senza cache, nuova ricerca", L"Probar MusicBrainz, GnuDB, iTunes y Deezer. La eleccion se guarda en TEMP; sin cache, busca de nuevo", L"MusicBrainz·GnuDB·iTunes·Deezer를 시험한 뒤 고릅니다. 적용분은 TEMP에 남고, 없으면 다시 받습니다", L"试 MusicBrainz / GnuDB / iTunes / Deezer。选过的记在 TEMP，没有缓存才再取", L"تجربة MusicBrainz وGnuDB وiTunes وDeezer. النتيجة في TEMP وإن غابت يُعاد الجلب", L"MusicBrainz, GnuDB, iTunes и Deezer. Выбор в TEMP; без кэша — снова запрос", L"MusicBrainz, GnuDB, iTunes und Deezer. Treffer in TEMP; ohne Cache erneut holen", L"Tentar MusicBrainz, GnuDB, iTunes e Deezer. A escolha fica no TEMP; sem cache busca de novo", L"MusicBrainz, GnuDB, iTunes en Deezer. De keuze staat in TEMP; zonder cache opnieuw ophalen", L"Sprobuj MusicBrainz, GnuDB, iTunes i Deezer. Wybor w TEMP; bez cache znow pobiera", L"MusicBrainz, GnuDB, iTunes ve Deezer dene. Uygulanan TEMP'te kalir; yoksa yeniden alir"));
		m_tooltip.AddTool(&m_searchGo, LL14(L"検索欄の名前でネット検索します", L"Search the net by the name in the box", L"Chercher sur le net par le nom saisi", L"Cerca in rete col nome nel riquadro", L"Buscar en la red por el nombre escrito", L"칸의 이름으로 인터넷 검색합니다", L"用搜索栏的名字上网查找", L"البحث في الشبكة بالاسم المكتوب", L"Искать в сети по имени в поле", L"Im Netz nach dem Namen suchen", L"Pesquisar na rede pelo nome na caixa", L"Zoek op het net met de naam in het vak", L"Szukaj w sieci po nazwie w polu", L"Kutudaki ada gore agda ara"));
		m_tooltip.AddTool(&m_browse, LL14(L"取り込み先フォルダを選びます", L"Choose the rip destination folder", L"Choisir le dossier d'extraction", L"Scegli la cartella di estrazione", L"Elegir la carpeta de extraccion", L"추출 폴더를 고릅니다", L"选择抓轨保存文件夹", L"اختيار مجلد الاستخراج", L"Выбрать папку извлечения", L"Zielordner fuer Rip waehlen", L"Escolher a pasta de extracao", L"Kies de rip-map", L"Wybierz folder zgrywania", L"Aktarma klasorunu sec"));
		m_tooltip.AddTool(&m_ripSel, LL14(L"選択した曲を取り込みます", L"Rip the selected tracks", L"Extraire la selection", L"Estrai le selezionate", L"Extraer la seleccion", L"선택한 곡을 추출합니다", L"抓取所选曲目", L"استخراج المقاطع المحددة", L"Извлечь выбранные", L"Auswahl rippen", L"Extrair selecionadas", L"Selectie rippen", L"Zgraj zaznaczone", L"Secilenleri aktar"));
		m_tooltip.AddTool(&m_ripAll, LL14(L"全曲を1ファイルずつ取り込みます", L"Rip every track as its own file", L"Extraire chaque piste dans un fichier", L"Estrai ogni traccia in un file", L"Extraer cada pista en un archivo", L"전곡을 파일마다 따로 추출합니다", L"整盘每曲一个文件抓取", L"استخراج كل مقطع في ملف", L"Извлечь каждый трек в файл", L"Jeden Titel als eigene Datei rippen", L"Extrair cada faixa num arquivo", L"Elke track als eigen bestand rippen", L"Zgraj kazdy utwor do osobnego pliku", L"Her parcayi ayri dosyaya aktar"));
		m_tooltip.AddTool(&m_ripOne, LL14(L"全曲を1本のファイルにまとめます", L"Rip the whole disc as one file", L"Extraire tout le disque en un fichier", L"Estrai tutto il disco in un file", L"Extraer todo el disco en un archivo", L"전곡을 하나의 파일로 추출합니다", L"整盘合成一个文件抓取", L"استخراج القرص كله في ملف واحد", L"Извлечь весь диск в один файл", L"Die ganze Disc in einer Datei rippen", L"Extrair o disco inteiro num arquivo", L"De hele schijf als een bestand rippen", L"Zgraj cala plyte do jednego pliku", L"Tum diski tek dosyaya aktar"));
		m_tooltip.AddTool(&m_burnAudio, LL14(L"音声CDを書き込みます", L"Burn an audio CD", L"Graver un CD audio", L"Masterizza un CD audio", L"Grabar un CD de audio", L"오디오 CD를 굽습니다", L"刻录音频光盘", L"نسخ قرص صوتي", L"Записать Audio CD", L"Audio-CD brennen", L"Gravar um CD de audio", L"Een audio-CD branden", L"Wypal CD audio", L"Ses CD yaz"));
		m_tooltip.AddTool(&m_burnData, LL14(L"データCDを書き込みます", L"Burn a data CD", L"Graver un CD de donnees", L"Masterizza un CD dati", L"Grabar un CD de datos", L"데이터 CD를 굽습니다", L"刻录数据光盘", L"نسخ قرص بيانات", L"Записать Data CD", L"Daten-CD brennen", L"Gravar um CD de dados", L"Een data-CD branden", L"Wypal CD danych", L"Veri CD yaz"));
		m_tooltip.AddTool(&m_erase, LL14(L"書き換えできるディスクを消します", L"Erase a rewritable disc", L"Effacer un disque reenregistrable", L"Cancella un disco riscrivibile", L"Borrar un disco regrabable", L"다시 쓸 수 있는 디스크를 지웁니다", L"擦除可擦写光盘", L"مسح قرص قابل لإعادة الكتابة", L"Стереть перезаписываемый диск", L"Wiederbeschreibbare Disc loeschen", L"Apagar um disco regravavel", L"Een herschrijfbare schijf wissen", L"Wyczysc plyte wielokrotnego zapisu", L"Yeniden yazilabilir diski sil"));
		m_tooltip.AddTool(&m_drive, LL14(L"使う光学ドライブ", L"Optical drive to use", L"Lecteur optique a utiliser", L"Unita ottica da usare", L"Unidad optica a usar", L"사용할 광학 드라이브", L"要使用的光驱", L"المحرك البصري المستخدم", L"Оптический привод", L"Zu verwendendes Laufwerk", L"Drive optico a usar", L"Te gebruiken optische drive", L"Naped optyczny", L"Kullanilacak optik surucu"));
		m_tooltip.AddTool(&m_fmt, LL14(L"取り込みの形式（WAV / MP3 / FLAC）", L"Rip format (WAV / MP3 / FLAC)", L"Format d'extraction (WAV / MP3 / FLAC)", L"Formato di estrazione (WAV / MP3 / FLAC)", L"Formato de extraccion (WAV / MP3 / FLAC)", L"추출 형식 (WAV / MP3 / FLAC)", L"抓轨格式（WAV / MP3 / FLAC）", L"تنسيق الاستخراج (WAV / MP3 / FLAC)", L"Формат извлечения (WAV / MP3 / FLAC)", L"Rip-Format (WAV / MP3 / FLAC)", L"Formato de extracao (WAV / MP3 / FLAC)", L"Rip-indeling (WAV / MP3 / FLAC)", L"Format zgrywania (WAV / MP3 / FLAC)", L"Aktarma bicimi (WAV / MP3 / FLAC)"));
		m_tooltip.AddTool(&m_qual, LL14(L"取り込みの品質", L"Rip quality", L"Qualite d'extraction", L"Qualita di estrazione", L"Calidad de extraccion", L"추출 품질", L"抓轨质量", L"جودة الاستخراج", L"Качество извлечения", L"Rip-Qualitaet", L"Qualidade da extracao", L"Rip-kwaliteit", L"Jakosc zgrywania", L"Aktarma kalitesi"));
		m_tooltip.AddTool(&m_list, LL14(L"曲リスト。ダブルクリックで再生。選んだセルをもう一度クリックするか F2 で名前を編集。Ctrl+C / Ctrl+V でコピーと貼り付け", L"Track list. Double-click to play. Click the selected cell again or press F2 to edit. Ctrl+C / Ctrl+V to copy and paste", L"Liste des pistes. Double-clic pour lire. Reclic ou F2 pour editer. Ctrl+C / Ctrl+V pour copier-coller", L"Elenco brani. Doppio clic per riprodurre. Di nuovo clic o F2 per modificare. Ctrl+C / Ctrl+V copia e incolla", L"Lista de pistas. Doble clic para reproducir. Otro clic o F2 para editar. Ctrl+C / Ctrl+V copiar y pegar", L"곡 목록. 더블클릭으로 재생. 선택한 셀을 다시 클릭하거나 F2로 편집. Ctrl+C / Ctrl+V로 복사와 붙여넣기", L"曲目列表。双击播放。再点选中的格子或 F2 改名。Ctrl+C / Ctrl+V 复制粘贴", L"قائمة المقاطع. نقر مزدوج للتشغيل. نقرة أخرى أو F2 للتحرير. Ctrl+C / Ctrl+V للنسخ واللصق", L"Список треков. Двойной щелчок — играть. Повторный щелчок или F2 — правка. Ctrl+C / Ctrl+V — копировать", L"Titelliste. Doppelklick spielt. Nochmal klicken oder F2 bearbeitet. Ctrl+C / Ctrl+V kopieren und einfuegen", L"Lista de faixas. Duplo clique toca. Clique de novo ou F2 edita. Ctrl+C / Ctrl+V copia e cola", L"Tracklijst. Dubbelklik speelt. Opnieuw klikken of F2 bewerkt. Ctrl+C / Ctrl+V kopieert en plakt", L"Lista utworow. Dwuklik odtwarza. Ponowne klikniecie lub F2 edytuje. Ctrl+C / Ctrl+V kopiuje i wkleja", L"Parca listesi. Cift tik oynatir. Secili hucreye tekrar tik veya F2 duzenler. Ctrl+C / Ctrl+V kopyala yapistir"));
		m_tooltip.AddTool(&m_cover, LL14(L"ジャケット。ネット検索で取れることがあります", L"Cover art. Lookup can fill this", L"Pochette. La recherche peut la remplir", L"Copertina. La ricerca puo riempirla", L"Portada. La busqueda puede rellenarla", L"재킷. 검색으로 채울 수 있습니다", L"封面。联网查找有时能填上", L"الغلاف. البحث قد يملأه", L"Обложка. Поиск может её заполнить", L"Cover. Die Suche kann es fuellen", L"Capa. A busca pode preenche-la", L"Omslag. Zoeken kan dit vullen", L"Okladka. Wyszukiwanie moze ja wypelnic", L"Kapak. Arama doldurabilir"));
		m_tooltip.AddTool(&m_album, LL14(L"アルバム名（直接編集できます）", L"Album name (editable)", L"Nom d'album (modifiable)", L"Nome album (modificabile)", L"Nombre de album (editable)", L"앨범 이름 (직접 편집)", L"专辑名（可直接编辑）", L"اسم الألبوم (قابل للتحرير)", L"Название альбома (можно править)", L"Albumname (editierbar)", L"Nome do album (editavel)", L"Albumnaam (bewerkbaar)", L"Nazwa albumu (edytowalna)", L"Album adi (duzenlenebilir)"));
		m_tooltip.AddTool(&m_artist, LL14(L"アーティスト名（直接編集できます）", L"Artist name (editable)", L"Nom d'artiste (modifiable)", L"Nome artista (modificabile)", L"Nombre de artista (editable)", L"아티스트 이름 (직접 편집)", L"艺术家名（可直接编辑）", L"اسم الفنان (قابل للتحرير)", L"Исполнитель (можно править)", L"Interpret (editierbar)", L"Nome do artista (editavel)", L"Artiestnaam (bewerkbaar)", L"Wykonawca (edytowalny)", L"Sanatci adi (duzenlenebilir)"));
		m_tooltip.AddTool(&m_discid, LL14(L"このディスクの ID。ネット検索に使います", L"Disc ID used for lookup", L"ID du disque pour la recherche", L"ID del disco per la ricerca", L"ID del disco para la busqueda", L"검색에 쓰는 디스크 ID", L"用于联网查找的光盘 ID", L"معرّف القرص للبحث", L"ID диска для поиска", L"Disc-ID fuer die Suche", L"ID do disco para a busca", L"Schijf-ID voor zoeken", L"ID plyty do wyszukiwania", L"Arama icin disk kimligi"));
		m_tooltip.AddTool(&m_time, LL14(L"再生位置と曲の長さ", L"Play position and track length", L"Position et duree de la piste", L"Posizione e durata della traccia", L"Posicion y duracion de la pista", L"재생 위치와 곡 길이", L"播放位置和曲长", L"موضع التشغيل وطول المقطع", L"Позиция и длина трека", L"Position und Titellaenge", L"Posicao e duracao da faixa", L"Afspeelpositie en tracklengte", L"Pozycja i dlugosc utworu", L"Oynatma konumu ve parca suresi"));
		m_tooltip.AddTool(&m_status, LL14(L"いまの動作", L"Current status", L"Etat actuel", L"Stato attuale", L"Estado actual", L"지금 동작", L"当前状态", L"الحالة الحالية", L"Текущее состояние", L"Aktueller Status", L"Estado atual", L"Huidige status", L"Biezacy stan", L"Guncel durum"));
		m_tooltip.AddTool(&m_driveL, LL14(L"光学ドライブ", L"Optical drive", L"Lecteur optique", L"Unita ottica", L"Unidad optica", L"광학 드라이브", L"光驱", L"محرك بصري", L"Оптический привод", L"Laufwerk", L"Drive optico", L"Optische drive", L"Naped optyczny", L"Optik surucu"));
		m_tooltip.AddTool(&m_fmtL, LL14(L"取り込みの形式", L"Rip format", L"Format d'extraction", L"Formato di estrazione", L"Formato de extraccion", L"추출 형식", L"抓轨格式", L"تنسيق الاستخراج", L"Формат извлечения", L"Rip-Format", L"Formato de extracao", L"Rip-indeling", L"Format zgrywania", L"Aktarma bicimi"));
		m_tooltip.AddTool(&m_qualL, LL14(L"取り込みの品質", L"Rip quality", L"Qualite d'extraction", L"Qualita di estrazione", L"Calidad de extraccion", L"추출 품질", L"抓轨质量", L"جودة الاستخراج", L"Качество извлечения", L"Rip-Qualitaet", L"Qualidade da extracao", L"Rip-kwaliteit", L"Jakosc zgrywania", L"Aktarma kalitesi"));
		m_tooltip.AddTool(&m_folderL, LL14(L"取り込んだファイルの保存先", L"Folder for ripped files", L"Dossier des fichiers extraits", L"Cartella dei file estratti", L"Carpeta de los archivos extraidos", L"추출 파일 저장 폴더", L"抓轨文件的保存位置", L"مجلد الملفات المستخرجة", L"Папка извлечённых файлов", L"Ordner fuer gerippte Dateien", L"Pasta dos arquivos extraidos", L"Map voor geripte bestanden", L"Folder zgranych plikow", L"Aktarilan dosyalarin klasoru"));
		m_tooltip.AddTool(&m_volL, LL14(L"再生音量", L"Playback volume", L"Volume de lecture", L"Volume di riproduzione", L"Volumen de reproduccion", L"재생 음량", L"播放音量", L"مستوى صوت التشغيل", L"Громкость воспроизведения", L"Wiedergabelautstaerke", L"Volume de reproducao", L"Afspeelvolume", L"Glosnosc odtwarzania", L"Oynatma sesi"));
		m_tooltip.AddTool(&m_seek, LL14(L"ドラッグでシーク。A / B / 解除ボタン、または［ ］で A-B ループ。つまみでも区間を動かせます", L"Drag to seek. A / B / Clear buttons, or [ ], set the A-B loop. Drag the thumbs to move it", L"Glisser pour seek. Boutons A / B / Effacer, ou [ ], pour A-B. Poignees pour deplacer", L"Trascina per seek. Pulsanti A / B / Cancella, o [ ], per A-B. Maniglie per spostare", L"Arrastre para buscar. Botones A / B / Quitar, o [ ], para A-B. Asas para mover", L"드래그로 시크. A / B / 해제 버튼 또는 [ ] 로 A-B 루프. 손잡이로 구간 이동", L"拖动定位。A / B / 解除按钮或 ［］ 做 A-B 循环。也可用滑块", L"اسحب للبحث. أزرار A / B / إلغاء أو [ ] لحلقة A-B. المقابض للنقل", L"Перетащите для перемотки. Кнопки A / B / Сброс или [ ] — петля A-B. Ручки двигают диапазон", L"Ziehen zum Suchen. Tasten A / B / Loeschen oder [ ] fuer A-B. Griffe verschieben den Bereich", L"Arraste para buscar. Botoes A / B / Limpar, ou [ ], para A-B. Alcas movem o trecho", L"Sleep om te zoeken. Knoppen A / B / Wissen, of [ ], voor A-B. Grepen verplaatsen het bereik", L"Przeciagnij by przewinac. Przyciski A / B / Wyczysc lub [ ] dla A-B. Uchwyty przesuwaja zakres", L"Surukleyerek sar. A / B / Temizle dugmeleri veya [ ] ile A-B. Tutamaclar araligi tasir"));
		m_tooltip.AddTool(&m_vol, LL14(L"再生音量", L"Playback volume", L"Volume de lecture", L"Volume di riproduzione", L"Volumen de reproduccion", L"재생 음량", L"播放音量", L"مستوى صوت التشغيل", L"Громкость воспроизведения", L"Wiedergabelautstaerke", L"Volume de reproducao", L"Afspeelvolume", L"Glosnosc odtwarzania", L"Oynatma sesi"));
		m_tooltip.AddTool(&m_repeat, LL14(L"最後まで行ったら最初から繰り返します", L"Repeat the disc from the start when it ends", L"Repeter le disque depuis le debut", L"Ripeti il disco dall'inizio", L"Repetir el disco desde el principio", L"끝나면 가면 처음부터 반복합니다", L"播完后从第一首再来", L"تكرار القرص من البداية عند الانتهاء", L"Повторить диск с начала", L"Die Disc von vorn wiederholen", L"Repetir o disco do comeco", L"De schijf vanaf het begin herhalen", L"Powtorz plyte od poczatku", L"Disk bitince bastan tekrarla"));
		m_tooltip.AddTool(&m_shuffle, LL14(L"次の曲をランダムにします", L"Pick the next track at random", L"Choisir la piste suivante au hasard", L"Scegli la traccia successiva a caso", L"Elegir la siguiente pista al azar", L"다음 곡을 무작위로 고릅니다", L"下一首随机", L"اختيار المقطع التالي عشوائياً", L"Следующий трек случайно", L"Naechsten Titel zufaellig waehlen", L"Escolher a proxima faixa ao acaso", L"Volgende track willekeurig", L"Nastepny utwor losowo", L"Sonraki parcayi rastgele sec"));
		m_tooltip.AddTool(&m_addPl, LL14(L"取り込み後に本編のプレイリストへ載せます", L"Add ripped files to the main playlist", L"Ajouter les extraits a la playlist principale", L"Aggiungi i file estratti alla playlist principale", L"Anadir los extraidos a la lista principal", L"추출한 파일을 메인 재생목록에 넣습니다", L"抓完后加入主播放列表", L"إضافة الملفات المستخرجة إلى القائمة الرئيسية", L"Добавить извлечённое в основной плейлист", L"Gerippte Dateien zur Hauptplaylist", L"Adicionar extraidos a playlist principal", L"Geripte bestanden naar de hoofdplaylist", L"Dodaj zgrane do glownej listy", L"Aktarilanlari ana listeye ekle"));
		m_tooltip.AddTool(&m_search, LL14(L"名前で検索するとき入力します", L"Type a name to search", L"Tapez un nom a chercher", L"Scrivi un nome da cercare", L"Escriba un nombre para buscar", L"찾을 이름을 입력합니다", L"按名称搜索时在此输入", L"اكتب اسماً للبحث", L"Введите имя для поиска", L"Namen zum Suchen eingeben", L"Digite um nome para buscar", L"Typ een naam om te zoeken", L"Wpisz nazwe do wyszukania", L"Aramak icin ad yazin"));
		m_tooltip.AddTool(&m_folder, LL14(L"取り込んだファイルの保存先", L"Folder for ripped files", L"Dossier des fichiers extraits", L"Cartella dei file estratti", L"Carpeta de los archivos extraidos", L"추출 파일 저장 폴더", L"抓轨文件的保存位置", L"مجلد الملفات المستخرجة", L"Папка извлечённых файлов", L"Ordner fuer gerippte Dateien", L"Pasta dos arquivos extraidos", L"Map voor geripte bestanden", L"Folder zgranych plikow", L"Aktarilan dosyalarin klasoru"));
		m_tooltip.AddTool(&m_progress, LL14(L"取り込みと書き込みの進み", L"Rip and burn progress", L"Avancement extraction et gravure", L"Avanzamento estrazione e masterizzazione", L"Progreso de extraccion y grabacion", L"추출과 굽기 진행", L"抓轨和刻录进度", L"تقدم الاستخراج والنسخ", L"Ход извлечения и записи", L"Fortschritt von Rip und Brennen", L"Progresso da extracao e gravacao", L"Voortgang van rippen en branden", L"Postep zgrywania i wypalania", L"Aktarma ve yazma ilerlemesi"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 420, 10000);
	}
	RestoreWindowPos();
	CRect rc; GetClientRect(&rc);
	LayoutChildren(rc.Width(), rc.Height());
	SetTimer(CD_TIMER_UI, 200, NULL);
	SetTimer(CD_TIMER_DISC, 1500, NULL);
	SetTimer(CD_TIMER_FOLLOW, 400, NULL);
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	SetStatus(LL14(L"ドライブを選んで読込", L"Pick a drive and Load", L"Choisissez un lecteur", L"Scegli unita e Carica", L"Elija unidad y Cargar",
		L"드라이브를 고르고 읽기", L"选择驱动器并读取", L"اختر محركاً وحمل", L"Выберите привод", L"Laufwerk wahlen und Laden",
		L"Escolha o drive e Carregar", L"Kies station en Laden", L"Wybierz naped i Wczytaj", L"Surucu secip Yukle"));
	m_time.SetWindowText(_T("0:00 / 0:00"));
	UpdateMetaUi();
	m_ready = TRUE;
	PostMessage(WM_CD_LOADTOC, 0, 0);
	return TRUE;
}

void CCdPlayerDlg::LayoutChildren(int cx, int cy)
{
	if (!GetSafeHwnd() || !m_list.GetSafeHwnd()) return;
	if (m_cellEditing) EndCellEdit(TRUE);
	const int capH = GetCustomCaptionHeight();
	const int pad = 8, rowH = 24, gap = 5;
	int lblH = 16;
	{
		CClientDC dc(this);
		CFont* prev = dc.SelectObject(GetFont());
		TEXTMETRIC tm = {};
		if (dc.GetTextMetrics(&tm) && tm.tmHeight > 0) lblH = tm.tmHeight + 3;
		dc.SelectObject(prev);
	}
	int y = capH + pad;
	HDWP dwp = BeginDeferWindowPos(52);
	if (!dwp) return;
	const int driveW = 110, btnW = 72, loadW = 64;
	if (m_driveL.GetSafeHwnd())
		dwp = DeferWindowPos(dwp, m_driveL, NULL, pad, y, 70, lblH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += lblH + 2;
	dwp = DeferWindowPos(dwp, m_drive, NULL, pad, y, driveW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_refresh, NULL, pad + driveW + gap, y, btnW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_eject, NULL, pad + driveW + gap + btnW + gap, y, btnW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_load, NULL, pad + driveW + gap + (btnW + gap) * 2, y, loadW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + gap + 2;
	const int cover = 120;
	dwp = DeferWindowPos(dwp, m_cover, NULL, pad, y, cover, cover, SWP_NOZORDER | SWP_NOACTIVATE);
	const int metaX = pad + cover + gap;
	const int metaW = max(120, cx - metaX - pad);
	const int editH = max(22, lblH + 6);
	dwp = DeferWindowPos(dwp, m_album, NULL, metaX, y, metaW, editH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_artist, NULL, metaX, y + editH + 3, metaW, editH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_discid, NULL, metaX, y + (editH + 3) * 2, metaW, lblH + 2, SWP_NOZORDER | SWP_NOACTIVATE);
	const int searchW = min(280, metaW - 90);
	dwp = DeferWindowPos(dwp, m_lookup, NULL, metaX, y + cover - rowH, 90, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_search, NULL, metaX + 95, y + cover - rowH, searchW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_searchGo, NULL, metaX + 95 + searchW + gap, y + cover - rowH, 64, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += cover + gap;
	const int botBlock = 8 * (rowH + gap) + 36;
	const int listH = max(80, cy - y - botBlock);
	dwp = DeferWindowPos(dwp, m_list, NULL, pad, y, max(40, cx - pad * 2), listH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += listH + gap;
	int x = pad;
	const int tw = 58;
	dwp = DeferWindowPos(dwp, m_play, NULL, x, y, tw, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += tw + gap;
	dwp = DeferWindowPos(dwp, m_pause, NULL, x, y, tw + 8, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += tw + 8 + gap;
	dwp = DeferWindowPos(dwp, m_stop, NULL, x, y, tw, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += tw + gap;
	dwp = DeferWindowPos(dwp, m_prev, NULL, x, y, tw, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += tw + gap;
	dwp = DeferWindowPos(dwp, m_next, NULL, x, y, tw, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += tw + gap;
	dwp = DeferWindowPos(dwp, m_repeat, NULL, x, y, 90, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += 90 + gap;
	dwp = DeferWindowPos(dwp, m_shuffle, NULL, x, y, 90, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + gap;
	const int timeW = 110, abW = 32, abClrW = 56, abGap = 4;
	const int abBlock = abW + abGap + abW + abGap + abClrW + abGap + timeW;
	const int seekW = max(40, cx - pad * 2 - abBlock);
	int abX = pad + seekW + abGap;
	dwp = DeferWindowPos(dwp, m_seek, NULL, pad, y, seekW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_abABtn, NULL, abX, y, abW, rowH, SWP_NOZORDER | SWP_NOACTIVATE); abX += abW + abGap;
	dwp = DeferWindowPos(dwp, m_abBBtn, NULL, abX, y, abW, rowH, SWP_NOZORDER | SWP_NOACTIVATE); abX += abW + abGap;
	dwp = DeferWindowPos(dwp, m_abClrBtn, NULL, abX, y, abClrW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_time, NULL, cx - pad - timeW, y, timeW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + gap;
	dwp = DeferWindowPos(dwp, m_volL, NULL, pad, y, 40, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_vol, NULL, pad + 44, y, 140, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + gap;
	const int labW = 48, fmtW = 112, qualW = 156;
	dwp = DeferWindowPos(dwp, m_fmtL, NULL, pad, y, labW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_fmt, NULL, pad + labW + 4, y, fmtW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_qualL, NULL, pad + labW + 4 + fmtW + 8, y, labW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_qual, NULL, pad + labW + 4 + fmtW + 8 + labW + 4, y, qualW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	const int folderX = pad + labW + 4 + fmtW + 8 + labW + 4 + qualW + 8;
	dwp = DeferWindowPos(dwp, m_folderL, NULL, folderX, y, labW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_folder, NULL, folderX + labW + 4, y, max(40, cx - pad - (folderX + labW + 4) - 80), rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_browse, NULL, cx - pad - 72, y, 72, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + gap;
	x = pad;
	dwp = DeferWindowPos(dwp, m_ripSel, NULL, x, y, 120, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += 125;
	dwp = DeferWindowPos(dwp, m_ripAll, NULL, x, y, 120, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += 125;
	dwp = DeferWindowPos(dwp, m_ripOne, NULL, x, y, 120, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += 125;
	dwp = DeferWindowPos(dwp, m_addPl, NULL, x, y, 150, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + gap;
	x = pad;
	dwp = DeferWindowPos(dwp, m_burnAudio, NULL, x, y, 130, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += 135;
	dwp = DeferWindowPos(dwp, m_burnData, NULL, x, y, 130, rowH, SWP_NOZORDER | SWP_NOACTIVATE); x += 135;
	dwp = DeferWindowPos(dwp, m_erase, NULL, x, y, 90, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + gap;
	const int closeW = 82;
	if (m_progress.GetSafeHwnd())
		dwp = DeferWindowPos(dwp, m_progress, NULL, pad, y, max(40, cx - pad * 2 - closeW - gap), 16, SWP_NOZORDER | SWP_NOACTIVATE);
	dwp = DeferWindowPos(dwp, m_close, NULL, cx - pad - closeW, y, closeW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + 2;
	dwp = DeferWindowPos(dwp, m_status, NULL, pad, y, max(40, cx - pad * 2), lblH + 2, SWP_NOZORDER | SWP_NOACTIVATE);
	EndDeferWindowPos(dwp);
	{
		CCustomComboBox* cbs[3] = { &m_drive, &m_fmt, &m_qual };
		for (int i = 0; i < 3; ++i) {
			if (!cbs[i]->GetSafeHwnd()) continue;
			cbs[i]->SetItemHeight(-1, rowH);
			cbs[i]->SetItemHeight(0, max(26, rowH + 4));
			cbs[i]->SetMinVisibleItems(8);
		}
	}
	if (m_list.GetSafeHwnd()) {
		CRect lr; m_list.GetClientRect(&lr);
		int w = lr.Width();
		if (w > 100) {
			m_list.SetColumnWidth(0, 44);
			m_list.SetColumnWidth(3, 58);
			m_list.SetColumnWidth(4, 100);
			int rest = w - 44 - 58 - 100 - 8;
			if (rest < 80) rest = 80;
			const int t1 = rest * 58 / 100;
			m_list.SetColumnWidth(1, t1);
			m_list.SetColumnWidth(2, rest - t1);
		}
	}
	if (m_seek.GetSafeHwnd()) m_seek.Invalidate(FALSE);
	if (m_vol.GetSafeHwnd()) m_vol.Invalidate(FALSE);
	if (m_cover.GetSafeHwnd()) m_cover.Invalidate(FALSE);
	if (m_progress.GetSafeHwnd()) m_progress.Invalidate(FALSE);
	if (m_ready)
		PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
}

void CCdPlayerDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		LayoutHelpBtnAndCaption();
		LayoutChildren(cx, cy);
	}
}

void CCdPlayerDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	CCustomBlurDialogBase::OnGetMinMaxInfo(lpMMI);
	lpMMI->ptMinTrackSize.x = 780;
	lpMMI->ptMinTrackSize.y = 560;
}

void CCdPlayerDlg::RestoreWindowPos()
{
	if (savedata.cdWinW >= 780 && savedata.cdWinH >= 520) {
		SetWindowPos(NULL, savedata.cdWinX, savedata.cdWinY, savedata.cdWinW, savedata.cdWinH, SWP_NOZORDER);
	} else {
		SetWindowPos(NULL, 0, 0, 900, 640, SWP_NOZORDER | SWP_NOMOVE);
	}
}

void CCdPlayerDlg::SaveWindowPos()
{
	CRect r; GetWindowRect(&r);
	savedata.cdWinX = r.left; savedata.cdWinY = r.top;
	savedata.cdWinW = r.Width(); savedata.cdWinH = r.Height();
	MpPersistSavedataQuick();
}

void CCdPlayerDlg::PersistUi()
{
	savedata.cdRipFmt = m_fmt.GetCurSel();
	if (savedata.cdRipFmt < 0) savedata.cdRipFmt = 0;
	const int fmt = savedata.cdRipFmt;
	const int q = m_qual.GetCurSel();
	if (fmt == 1) {
		const int kb[] = { 128, 192, 256, 320 };
		savedata.cdRipQual = (q >= 0 && q < 4) ? kb[q] : 192;
	} else if (fmt == 2) {
		savedata.cdRipQual = (q >= 0 && q <= 8) ? q : 5;
	}
	CString fol; m_folder.GetWindowText(fol);
	lstrcpyn(savedata.cdRipFolder, fol, 260);
	savedata.cdRipAddPl = m_addPl.GetCheck() ? 1 : 0;
	savedata.cdRepeat = m_repeat.GetCheck() ? 1 : 0;
	savedata.cdShuffle = m_shuffle.GetCheck() ? 1 : 0;
	savedata.cdVolume = m_vol.GetPos();
}

BOOL CCdPlayerDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd()) m_tooltip.RelayEvent(pMsg);
	if (pMsg->message == WM_MOUSEWHEEL || pMsg->message == WM_MOUSEHWHEEL) {
		if (m_cellEditing) EndCellEdit(TRUE);
		if (m_list.GetSafeHwnd() && m_trackN > 0
			&& (pMsg->hwnd == m_list.m_hWnd || ::IsChild(m_list.m_hWnd, pMsg->hwnd))) {
			const short z = GET_WHEEL_DELTA_WPARAM(pMsg->wParam);
			if (z != 0) {
				int sel = m_list.GetNextItem(-1, LVNI_FOCUSED);
				if (sel < 0) sel = m_list.GetNextItem(-1, LVNI_SELECTED);
				if (sel < 0) sel = 0;
				int n = sel + ((z > 0) ? -1 : 1);
				if (n < 0) n = 0;
				if (n >= m_trackN) n = m_trackN - 1;
				if (n != sel) {
					m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
					m_list.SetItemState(n, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					m_list.EnsureVisible(n, FALSE);
				}
				return TRUE;
			}
		}
	}
	if (pMsg->message == WM_LBUTTONDOWN && m_list.GetSafeHwnd() && pMsg->hwnd == m_list.m_hWnd) {
		if (m_cellEditing) EndCellEdit(TRUE);
		KillTimer(CD_TIMER_EDIT);
		CPoint pt((short)LOWORD(pMsg->lParam), (short)HIWORD(pMsg->lParam));
		LVHITTESTINFO ht = {};
		ht.pt = pt;
		m_list.SubItemHitTest(&ht);
		m_clickRow = ht.iItem;
		m_clickCol = ht.iSubItem;
		m_clickWasSel = (ht.iItem >= 0 && (m_list.GetItemState(ht.iItem, LVIS_SELECTED) & LVIS_SELECTED));
		if (ht.iItem >= 0) {
			m_cellRow = ht.iItem;
			m_cellCol = ht.iSubItem;
		}
	}
	if (pMsg->message == WM_KEYDOWN) {
		const HWND h = pMsg->hwnd;
		const BOOL cellFocus = (m_cellEdit.GetSafeHwnd() && h == m_cellEdit.m_hWnd);
		const BOOL fieldFocus = (h == m_album.m_hWnd || h == m_artist.m_hWnd
			|| h == m_search.m_hWnd || h == m_folder.m_hWnd);
		if (cellFocus) {
			if (pMsg->wParam == VK_RETURN) { EndCellEdit(TRUE); return TRUE; }
			if (pMsg->wParam == VK_ESCAPE) { EndCellEdit(FALSE); return TRUE; }
			if (pMsg->wParam == VK_TAB) {
				const int row = m_editRow, col = m_editCol;
				EndCellEdit(TRUE);
				const BOOL back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
				int nr = row, nc = col;
				if (back) {
					if (nc == 2) nc = 1;
					else { nc = 2; nr--; }
				} else {
					if (nc == 1) nc = 2;
					else { nc = 1; nr++; }
				}
				if (nr >= 0 && nr < m_trackN) BeginCellEdit(nr, nc);
				return TRUE;
			}
		}
		if (!fieldFocus && !cellFocus) {
			if (pMsg->wParam == VK_F2) {
				int row = m_cellRow, col = m_cellCol;
				if (row < 0) row = SelectedTrack();
				if (col != 1 && col != 2) col = 1;
				BeginCellEdit(row, col);
				return TRUE;
			}
			if ((GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)
				&& !(GetKeyState(VK_SHIFT) & 0x8000)) {
				if (pMsg->wParam == 'C') { CopyMetaCells(); return TRUE; }
				if (pMsg->wParam == 'V') { PasteMetaCells(); return TRUE; }
			}
			if (!(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
				const BOOL isA = (pMsg->wParam == VK_OEM_4 || pMsg->wParam == (WPARAM)'[');
				const BOOL isB = (pMsg->wParam == VK_OEM_6 || pMsg->wParam == (WPARAM)']');
				if (isA || isB) {
					if (m_abA >= 0 && m_abB > m_abA) ClearAbLoop(TRUE);
					else SetAbAtPlayhead(isB);
					return TRUE;
				}
				if ((pMsg->wParam == VK_BACK || pMsg->wParam == VK_DELETE)
					&& (m_abA >= 0 || m_abB >= 0)) {
					ClearAbLoop(TRUE);
					return TRUE;
				}
			}
		}
	}
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CCdPlayerDlg::AbortDiscIo()
{
	HWND hCand = (m_candUi && m_candUi->GetSafeHwnd()) ? m_candUi->GetSafeHwnd() : NULL;
	if (hCand && ::IsWindow(hCand))
		::EndDialog(hCand, IDCANCEL);
	StopPlay(TRUE);
	InterlockedExchange(&m_ripStop, 1);
	InterlockedExchange(&m_lookupStop, 1);
	if (m_ripTh) {
		WaitForSingleObject(m_ripTh, 4000);
		CloseHandle(m_ripTh);
		m_ripTh = NULL;
	}
	if (m_lookupTh) {
		WaitForSingleObject(m_lookupTh, 8000);
		CloseHandle(m_lookupTh);
		m_lookupTh = NULL;
	}
	CloseCdHandle();
}

void CCdPlayerDlg::FillDrives()
{
	TCHAR cur[8] = {};
	if (m_drive.GetSafeHwnd())
		m_drive.GetWindowText(cur, 8);
	m_fillingDrives = TRUE;
	m_drive.ResetContent();
	DWORD mask = GetLogicalDrives();
	for (int i = 0; i < 26; ++i) {
		if (!(mask & (1u << i))) continue;
		TCHAR root[4] = { (TCHAR)(_T('A') + i), _T(':'), _T('\\'), 0 };
		if (GetDriveType(root) == DRIVE_CDROM) {
			TCHAR lab[8];
			wsprintf(lab, _T("%c:"), _T('A') + i);
			m_drive.AddString(lab);
		}
	}
	int sel = 0;
	for (int i = 0; i < m_drive.GetCount(); ++i) {
		TCHAR t[8] = {};
		m_drive.GetLBText(i, t);
		if (cur[0] && !lstrcmpi(t, cur)) { sel = i; break; }
	}
	if (m_drive.GetCount() > 0) m_drive.SetCurSel(sel);
	m_fillingDrives = FALSE;
}

void CCdPlayerDlg::OnRefresh() { FillDrives(); if (m_ready) LoadToc(); }
void CCdPlayerDlg::OnDriveChange() { if (m_fillingDrives || !m_ready) return; LoadToc(); }

LRESULT CCdPlayerDlg::OnLoadTocMsg(WPARAM, LPARAM)
{
	if (m_drive.GetCount() > 0) LoadToc();
	return 0;
}

LRESULT CCdPlayerDlg::OnMediaMsg(WPARAM wParam, LPARAM)
{
	if (!m_ready) return 0;
	if (wParam == DBT_DEVICEREMOVECOMPLETE) {
		AbortDiscIo();
		m_trackN = 0;
		m_albumName[0] = m_albumArtist[0] = 0;
		m_discidA[0] = 0;
		m_cddbId[0] = 0;
		m_mbid[0] = 0;
		m_cover.ClearImage();
		FillTrackList();
		UpdateMetaUi();
		SetStatus(LL14(L"ディスクが取り外されました", L"Disc removed", L"Disque retire", L"Disco rimosso", L"Disco extraido",
			L"디스크가 제거되었습니다", L"光盘已取出", L"تمت إزالة القرص", L"Диск извлечён", L"Disc entfernt",
			L"Disco removido", L"Schijf verwijderd", L"Wyjeto plyte", L"Disk cikarildi"));
	}
	FillDrives();
	if (wParam == DBT_DEVICEARRIVAL) {
		AbortDiscIo();
		m_mediaRetry = FALSE;
		KillTimer(CD_TIMER_MEDIA);
		SetTimer(CD_TIMER_MEDIA, 1200, NULL);
	}
	return 0;
}

void CCdPlayerDlg::OnEject()
{
	AbortDiscIo();
	HANDLE h = OpenCdHandle();
	if (h != INVALID_HANDLE_VALUE) {
		DWORD br = 0;
		DeviceIoControl(h, IOCTL_STORAGE_EJECT_MEDIA, NULL, 0, NULL, 0, &br, NULL);
		CloseHandle(h);
	}
	m_trackN = 0;
	m_albumName[0] = m_albumArtist[0] = 0;
	m_discidA[0] = 0;
	m_cddbId[0] = 0;
	m_mbid[0] = 0;
	m_cover.ClearImage();
	FillTrackList();
	UpdateMetaUi();
	SetStatus(LL14(L"取り出しました", L"Ejected", L"Ejecte", L"Espulso", L"Expulsado",
		L"꺼냈습니다", L"已弹出", L"تم الإخراج", L"Лоток открыт", L"Ausgeworfen",
		L"Ejetado", L"Uitgeworpen", L"Wysunieto", L"Cikarildi"));
}

void CCdPlayerDlg::OnLoad() { LoadToc(); }

void CCdPlayerDlg::LoadToc()
{
	if (m_tocBusy) return;
	m_tocBusy = TRUE;
	AbortDiscIo();
	ClearAbLoop();
	m_trackN = 0;
	m_albumName[0] = m_albumArtist[0] = 0;
	m_discidA[0] = 0;
	m_cddbId[0] = 0;
	m_cddbNsec = 0;
	m_mbid[0] = 0;
	m_cover.ClearImage();
	HANDLE h = OpenCdHandle();
	if (h == INVALID_HANDLE_VALUE) {
		FillTrackList();
		UpdateMetaUi();
		SetStatus(LL14(L"ドライブを開けません", L"Cannot open drive", L"Impossible d'ouvrir le lecteur", L"Impossibile aprire l'unita", L"No se puede abrir la unidad",
			L"드라이브를 열 수 없습니다", L"无法打开驱动器", L"تعذر فتح المحرك", L"Не открыть привод", L"Laufwerk nicht zu offnen",
			L"Nao foi possivel abrir o drive", L"Station niet te openen", L"Nie mozna otworzyc napedu", L"Surucu acilamadi"));
		m_tocBusy = FALSE;
		return;
	}
	CDROM_TOC toc;
	ZeroMemory(&toc, sizeof(toc));
	DWORD br = 0;
	if (!DeviceIoControl(h, IOCTL_CDROM_READ_TOC, NULL, 0, &toc, sizeof(toc), &br, NULL)) {
		CloseHandle(h);
		FillTrackList();
		UpdateMetaUi();
		SetStatus(LL14(L"ディスクがありません", L"No disc", L"Pas de disque", L"Nessun disco", L"No hay disco",
			L"디스크 없음", L"没有光盘", L"لا يوجد قرص", L"Нет диска", L"Keine Disc",
			L"Sem disco", L"Geen schijf", L"Brak plyty", L"Disk yok"));
		m_tocBusy = FALSE;
		return;
	}
	m_firstTrack = toc.FirstTrack;
	m_trackN = (int)toc.LastTrack - (int)toc.FirstTrack + 1;
	if (m_trackN < 1) m_trackN = 0;
	if (m_trackN > CD_MAX_TRACK) m_trackN = CD_MAX_TRACK;
	const int nent = ((int)toc.Length[0] << 8) | (int)toc.Length[1];
	int entries = (nent - 2) / 8;
	if (entries < 1) entries = toc.LastTrack + 1;
	DWORD lead = 0;
	for (int i = 0; i < entries && i < 100; ++i) {
		const TRACK_DATA& td = toc.TrackData[i];
		const DWORD lba = CdMsfToLba(td.Address[1], td.Address[2], td.Address[3]);
		if (td.TrackNumber == 0xAA) { lead = lba; continue; }
		const int idx = (int)td.TrackNumber - m_firstTrack;
		if (idx >= 0 && idx < m_trackN)
			m_startLba[idx] = lba;
	}
	m_leadout = lead;
	for (int i = 0; i < m_trackN; ++i) {
		DWORD end = (i + 1 < m_trackN) ? m_startLba[i + 1] : m_leadout;
		if (end <= m_startLba[i]) end = m_startLba[i] + 75;
		m_endLba[i] = end;
		wsprintf(m_title[i], LL14(L"トラック %02d", L"Track %02d", L"Piste %02d", L"Traccia %02d", L"Pista %02d",
			L"트랙 %02d", L"曲目 %02d", L"مقطع %02d", L"Дорожка %02d", L"Titel %02d",
			L"Faixa %02d", L"Nummer %02d", L"Sciezka %02d", L"Parca %02d"), m_firstTrack + i);
		m_trArtist[i][0] = 0;
		m_isrc[i][0] = 0;
	}

	CDROM_READ_TOC_EX ex;
	ZeroMemory(&ex, sizeof(ex));
	ex.Format = CDROM_READ_TOC_EX_FORMAT_CDTEXT;
	BYTE cdbuf[4096];
	ZeroMemory(cdbuf, sizeof(cdbuf));
	if (DeviceIoControl(h, IOCTL_CDROM_READ_TOC_EX, &ex, sizeof(ex), cdbuf, sizeof(cdbuf), &br, NULL) && br > 4) {
		const int nbytes = ((cdbuf[0] << 8) | cdbuf[1]) + 2;
		char pack[128][16];
		ZeroMemory(pack, sizeof(pack));
		int used[128];
		ZeroMemory(used, sizeof(used));
		for (int off = 4; off + 18 <= nbytes && off + 18 <= (int)br; off += 18) {
			const BYTE* pk = cdbuf + off;
			const int type = pk[0];
			const int tr = pk[1];
			const int seq = pk[2];
			if (seq < 0 || seq >= 128) continue;
			if (type < 0x80) continue;
			memcpy(pack[seq], pk + 4, 12);
			used[seq] = (type << 16) | tr;
		}
		char acc[256]; int alen = 0; int curType = -1; int curTr = -1;
		acc[0] = 0;
		for (int s = 0; s < 128; ++s) {
			if (!used[s] && alen == 0) continue;
			const int type = used[s] >> 16;
			const int tr = used[s] & 0xFFFF;
			if (type != curType || tr != curTr) {
				if (alen > 0 && curType >= 0x80) {
					TCHAR w[256]; Utf8ToT(acc, w, 256);
					if (w[0]) {
						if (curType == 0x80) {
							if (curTr <= 1) lstrcpyn(m_albumName, w, 256);
							else if (curTr - m_firstTrack >= 0 && curTr - m_firstTrack < m_trackN)
								lstrcpyn(m_title[curTr - m_firstTrack], w, 128);
						} else if (curType == 0x81) {
							if (curTr <= 1) lstrcpyn(m_albumArtist, w, 256);
							else if (curTr - m_firstTrack >= 0 && curTr - m_firstTrack < m_trackN)
								lstrcpyn(m_trArtist[curTr - m_firstTrack], w, 128);
						}
					}
				}
				curType = type; curTr = tr; alen = 0; acc[0] = 0;
			}
			for (int k = 0; k < 12 && alen < 250; ++k) {
				if (pack[s][k] == 0) { acc[alen] = 0; break; }
				acc[alen++] = pack[s][k];
				acc[alen] = 0;
			}
		}
		if (alen > 0 && curType >= 0x80) {
			TCHAR w[256]; Utf8ToT(acc, w, 256);
			if (w[0]) {
				if (curType == 0x80 && curTr <= 1) lstrcpyn(m_albumName, w, 256);
				if (curType == 0x81 && curTr <= 1) lstrcpyn(m_albumArtist, w, 256);
			}
		}
	}

	CdDiscId(m_firstTrack, m_firstTrack + m_trackN - 1, m_startLba, m_trackN, m_leadout, m_discidA);
	CdCddbId(m_trackN, m_startLba, m_leadout, m_cddbId, m_cddbOff, &m_cddbNsec);
	EnterCriticalSection(&m_cdCs);
	m_hCd = h;
	LeaveCriticalSection(&m_cdCs);
	FillTrackList();
	UpdateMetaUi();
	SetStatus(LL14(L"目次を読みました", L"TOC loaded", L"TOC chargee", L"TOC caricata", L"TOC cargada",
		L"목차를 읽었습니다", L"已读取目录", L"تم تحميل الفهرس", L"TOC загружен", L"TOC geladen",
		L"TOC carregada", L"TOC geladen", L"Wczytano TOC", L"TOC yuklendi"));
	m_tocBusy = FALSE;
	if (m_discidA[0]) LookupNet(FALSE);
}

void CCdPlayerDlg::FillTrackList()
{
	EndCellEdit(FALSE);
	m_list.SetRedraw(FALSE);
	m_list.DeleteAllItems();
	for (int i = 0; i < m_trackN; ++i) {
		TCHAR num[8]; wsprintf(num, _T("%02d"), m_firstTrack + i);
		const int row = m_list.InsertItem(i, num);
		m_list.SetItemText(row, 1, m_title[i]);
		m_list.SetItemText(row, 2, m_trArtist[i][0] ? m_trArtist[i] : m_albumArtist);
		int mm, ss;
		CdLbaToMsf(m_endLba[i] - m_startLba[i], &mm, &ss);
		TCHAR tm[16]; wsprintf(tm, _T("%d:%02d"), mm, ss);
		m_list.SetItemText(row, 3, tm);
		m_list.SetItemText(row, 4, m_isrc[i]);
	}
	m_list.SetRedraw(TRUE);
	m_list.Invalidate(FALSE);
	if (m_trackN > 0)
		m_list.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CCdPlayerDlg::UpdateMetaUi()
{
	CWnd* f = GetFocus();
	if (f != &m_album)
		m_album.SetWindowText(m_albumName);
	if (f != &m_artist)
		m_artist.SetWindowText(m_albumArtist);
	TCHAR d[80];
	if (m_discidA[0]) {
		TCHAR w[48];
		MultiByteToWideChar(CP_ACP, 0, m_discidA, -1, w, 48);
		wsprintf(d, _T("DiscID  %s"), w);
	} else lstrcpy(d, L"DiscID  -");
	m_discid.SetWindowText(d);
}

int CCdPlayerDlg::SelectedTrack() const
{
	POSITION p = m_list.GetFirstSelectedItemPosition();
	if (!p) return (m_curTrack >= 0) ? m_curTrack : 0;
	return m_list.GetNextSelectedItem(p);
}

void CCdPlayerDlg::CollectSelTracks(int* idx, int* n) const
{
	*n = 0;
	POSITION p = m_list.GetFirstSelectedItemPosition();
	while (p && *n < CD_MAX_TRACK) {
		idx[*n] = m_list.GetNextSelectedItem(p);
		(*n)++;
	}
	if (*n == 0 && m_trackN > 0) { idx[0] = SelectedTrack(); *n = 1; }
}

BOOL CCdPlayerDlg::GetMetaCellRect(int row, int col, CRect& rcDlg)
{
	CRect rc;
	if (!m_list.GetSubItemRect(row, col, LVIR_BOUNDS, rc)) return FALSE;
	if (col > 0) {
		CRect left;
		m_list.GetSubItemRect(row, col - 1, LVIR_BOUNDS, left);
		rc.left = left.right;
	}
	CRect lr; m_list.GetClientRect(&lr);
	if (rc.bottom < 0 || rc.top > lr.bottom || rc.right < 0 || rc.left > lr.right) return FALSE;
	if (rc.left < lr.left) rc.left = lr.left;
	m_list.ClientToScreen(&rc);
	ScreenToClient(&rc);
	rcDlg = rc;
	return rc.Width() > 8 && rc.Height() > 8;
}

void CCdPlayerDlg::ApplyCellText(int row, int col, LPCTSTR text)
{
	if (row < 0 || row >= m_trackN) return;
	CString s(text ? text : _T(""));
	s.Trim();
	if (col == 1) {
		if (s.IsEmpty()) {
			wsprintf(m_title[row], LL14(L"トラック %02d", L"Track %02d", L"Piste %02d", L"Traccia %02d", L"Pista %02d",
				L"트랙 %02d", L"曲目 %02d", L"مقطع %02d", L"Дорожка %02d", L"Titel %02d",
				L"Faixa %02d", L"Nummer %02d", L"Sciezka %02d", L"Parca %02d"), m_firstTrack + row);
		} else
			lstrcpyn(m_title[row], s, 128);
		m_list.SetItemText(row, 1, m_title[row]);
	} else if (col == 2) {
		lstrcpyn(m_trArtist[row], s, 128);
		m_list.SetItemText(row, 2, m_trArtist[row][0] ? m_trArtist[row] : m_albumArtist);
	}
}

void CCdPlayerDlg::EndCellEdit(BOOL commit)
{
	if (!m_cellEditing) return;
	const int row = m_editRow, col = m_editCol;
	CString text;
	if (m_cellEdit.GetSafeHwnd() && commit)
		m_cellEdit.GetWindowText(text);
	m_endingEdit = TRUE;
	m_cellEditing = FALSE;
	m_editRow = m_editCol = -1;
	if (m_cellEdit.GetSafeHwnd())
		m_cellEdit.ShowWindow(SW_HIDE);
	m_endingEdit = FALSE;
	if (commit && row >= 0)
		ApplyCellText(row, col, text);
}

void CCdPlayerDlg::BeginCellEdit(int row, int col)
{
	KillTimer(CD_TIMER_EDIT);
	EndCellEdit(TRUE);
	if (row < 0 || row >= m_trackN) return;
	if (col != 1 && col != 2) return;
	m_list.EnsureVisible(row, FALSE);
	CRect rc;
	if (!GetMetaCellRect(row, col, rc)) return;
	if (!m_cellEdit.GetSafeHwnd()) {
		m_cellEdit.Create(WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, rc, this, IDC_CD_CELLEDIT);
		m_cellEdit.SetAeroMode(FALSE);
		m_cellEdit.SetFont(GetFont());
	} else
		m_cellEdit.MoveWindow(&rc);
	m_cellEdit.SetWindowText(m_list.GetItemText(row, col));
	m_cellEdit.ShowWindow(SW_SHOW);
	m_cellEdit.SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	m_cellEdit.SetFocus();
	m_cellEdit.SetSel(0, -1);
	m_cellEditing = TRUE;
	m_editRow = row;
	m_editCol = col;
	m_cellRow = row;
	m_cellCol = col;
}

void CCdPlayerDlg::CommitAlbumFields()
{
	if (!m_album.GetSafeHwnd()) return;
	CString a, r;
	m_album.GetWindowText(a);
	m_artist.GetWindowText(r);
	lstrcpyn(m_albumName, a, 256);
	lstrcpyn(m_albumArtist, r, 256);
	for (int i = 0; i < m_trackN; ++i)
		m_list.SetItemText(i, 2, m_trArtist[i][0] ? m_trArtist[i] : m_albumArtist);
}

void CCdPlayerDlg::CopyMetaCells()
{
	EndCellEdit(TRUE);
	int col = (m_cellCol == 1 || m_cellCol == 2) ? m_cellCol : 1;
	int idx[CD_MAX_TRACK], n = 0;
	CollectSelTracks(idx, &n);
	if (n <= 0) return;
	CString out;
	for (int i = 0; i < n; ++i) {
		if (i) out += _T("\r\n");
		out += m_list.GetItemText(idx[i], col);
	}
	if (!::OpenClipboard(m_hWnd)) return;
	::EmptyClipboard();
	const SIZE_T bytes = (SIZE_T)(out.GetLength() + 1) * sizeof(WCHAR);
	HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (!h) { ::CloseClipboard(); return; }
	memcpy(::GlobalLock(h), (LPCWSTR)out, bytes);
	::GlobalUnlock(h);
	::SetClipboardData(CF_UNICODETEXT, h);
	::CloseClipboard();
}

void CCdPlayerDlg::PasteMetaCells()
{
	EndCellEdit(TRUE);
	if (!::OpenClipboard(m_hWnd)) return;
	HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
	CString clip;
	if (h) {
		LPCWSTR p = (LPCWSTR)::GlobalLock(h);
		if (p) clip = p;
		::GlobalUnlock(h);
	}
	::CloseClipboard();
	if (clip.IsEmpty()) return;
	clip.Replace(_T("\r\n"), _T("\n"));
	clip.Replace(_T('\r'), _T('\n'));
	clip.TrimRight(_T("\n"));
	int col = (m_cellCol == 1 || m_cellCol == 2) ? m_cellCol : 1;
	int idx[CD_MAX_TRACK], nSel = 0;
	CollectSelTracks(idx, &nSel);
	CString lines[CD_MAX_TRACK];
	int nLines = 0;
	int start = 0;
	clip += _T("\n");
	for (int i = 0; i < clip.GetLength() && nLines < CD_MAX_TRACK; ++i) {
		if (clip[i] == _T('\n')) {
			lines[nLines++] = clip.Mid(start, i - start);
			start = i + 1;
		}
	}
	if (nLines <= 0) return;
	auto applyLine = [this](int row, int col0, const CString& line) {
		if (row < 0 || row >= m_trackN) return;
		const int tab = line.Find(_T('\t'));
		if (tab < 0) {
			ApplyCellText(row, col0, line);
			return;
		}
		CString f0 = line.Left(tab);
		CString f1 = line.Mid(tab + 1);
		const int t2 = f1.Find(_T('\t'));
		if (t2 >= 0) f1 = f1.Left(t2);
		ApplyCellText(row, col0, f0);
		if (col0 == 1) ApplyCellText(row, 2, f1);
	};
	if (nLines == 1) {
		for (int i = 0; i < nSel; ++i)
			applyLine(idx[i], col, lines[0]);
	} else {
		int row0 = (m_cellRow >= 0) ? m_cellRow : (nSel ? idx[0] : 0);
		for (int k = 0; k < nLines; ++k)
			applyLine(row0 + k, col, lines[k]);
	}
}

void CCdPlayerDlg::OnAlbumKillFocus() { CommitAlbumFields(); }
void CCdPlayerDlg::OnArtistKillFocus() { CommitAlbumFields(); }
void CCdPlayerDlg::OnCellKillFocus()
{
	if (!m_endingEdit)
		EndCellEdit(TRUE);
}

void CCdPlayerDlg::OnListClick(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	NMITEMACTIVATE* p = (NMITEMACTIVATE*)pNMHDR;
	if (p->iItem >= 0) {
		m_cellRow = p->iItem;
		m_cellCol = p->iSubItem;
	}
	if (m_clickWasSel && m_clickRow == p->iItem && (p->iSubItem == 1 || p->iSubItem == 2)
		&& !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_CONTROL) & 0x8000)) {
		m_pendingRow = p->iItem;
		m_pendingCol = p->iSubItem;
		SetTimer(CD_TIMER_EDIT, GetDoubleClickTime(), NULL);
	}
}

void CCdPlayerDlg::ApplyVolume()
{
	if (!m_hwo) return;
	const int v = m_vol.GetPos();
	DWORD w = (DWORD)((v * 0xFFFFu) / 100);
	waveOutSetVolume(m_hwo, (w << 16) | w);
}

void CCdPlayerDlg::StopPlay(BOOL join)
{
	InterlockedExchange(&m_playStop, 1);
	if (m_waveEvt) SetEvent(m_waveEvt);
	if (join && m_playTh) {
		WaitForSingleObject(m_playTh, 8000);
		CloseHandle(m_playTh);
		m_playTh = NULL;
	}
	m_paused = FALSE;
}

enum { CD_RB_MAXFR = CCdPlayerDlg::CD_PLAY_SECS * 588 };

static void CdApplyEq(void* pcm, int bytes, BOOL reset)
{
	if (!pcm || bytes <= 0) return;
	EqualiserSetFormatVolContext(0, FALSE);
	equaliserBank(0, pcm, bytes, reset, 16, 2, 44100);
}

static void CdGetOutFormat(int* rate, int* ch, int* bits)
{
	*rate = 44100;
	*ch = 2;
	*bits = 16;
	if (!savedata.upscale_enable)
		return;
	if (savedata.speaker_layout == 5)
		*ch = 2;
	else {
		*ch = SpeakerLayoutToOutChannels(savedata.speaker_layout);
		if (*ch < 1) *ch = 2;
		if (*ch > 8) *ch = 8;
	}
	*rate = (int)savedata.samples;
	if (*rate < 8000 || *rate > 384000)
		*rate = 44100;
	*bits = savedata.bit32 ? 32 : (savedata.bit24 ? 24 : 16);
}

static void CdFeedLive(const BYTE* pcm, int bytes, int rate, int ch, int bits)
{
	if (!pcm || bytes <= 0 || ch < 1 || bits < 8) return;
	const int bpf = ch * (bits / 8);
	if (bpf < 1) return;
	g_ds_pcm_ch = ch;
	g_ds_pcm_bits = bits;
	g_ds_pcm_rate = rate;
	g_outBytesPerFrame = bpf;
	MpRemoteWritePcm(pcm, bytes);
	if (!og) return;
	const int frames = bytes / bpf;
	if (frames <= 0) return;
	if (og->m_AnalyzerDlg && ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd())) {
		og->m_AnalyzerDlg->ResumePlaybackFeed();
		og->m_AnalyzerDlg->FeedPCM(pcm, frames, rate, bits, ch);
	}
	if (og->m_PianoRollDlg && ::IsWindow(og->m_PianoRollDlg->GetSafeHwnd()))
		og->m_PianoRollDlg->FeedPCM(pcm, frames, rate, bits, ch);
}

static void CdMixEqualPower(BYTE* cur, const BYTE* nxt, int bytes, float t0, float t1)
{
	if (!cur || !nxt || bytes < 4) return;
	const int frames = bytes / 4;
	INT16* a = (INT16*)cur;
	const INT16* b = (const INT16*)nxt;
	for (int i = 0; i < frames; ++i) {
		float t = (frames <= 1) ? t1 : t0 + (t1 - t0) * ((float)i / (float)(frames - 1));
		if (t < 0.f) t = 0.f;
		if (t > 1.f) t = 1.f;
		const float ga = cosf(t * 1.57079632f);
		const float gb = sinf(t * 1.57079632f);
		for (int c = 0; c < 2; ++c) {
			float v = (float)a[i * 2 + c] * ga + (float)b[i * 2 + c] * gb;
			int iv = (int)floorf(v + 0.5f);
			if (iv > 32767) iv = 32767;
			if (iv < -32768) iv = -32768;
			a[i * 2 + c] = (INT16)iv;
		}
	}
}

static float CdPlayXfadeSec()
{
	if (savedata.play_xfade == 0) {
		if (!(og && og->m_xfade.GetSafeHwnd() && og->m_xfade.GetCheck()))
			return 0.f;
	}
	int c = savedata.play_xfade_sec100;
	if (c < 10) c = 500;
	if (c > 12000) c = 12000;
	return (float)c / 100.f;
}

static float CdRipXfadeSec()
{
	if (!savedata.wav_export_xfade)
		return 0.f;
	float s = savedata.wav_export_xfade_sec;
	if (s < 0.1f) s = 5.f;
	if (s > 60.f) s = 60.f;
	return s;
}

static void CdGetRipOutFormat(int ripFmt, int* rate, int* ch, int* bits)
{
	CdGetOutFormat(rate, ch, bits);
	if (ripFmt == 1) {
		*ch = 2;
		*bits = 16;
	}
	AudioUpscaler probe;
	probe.Configure(44100, 2, 16, *rate, *ch, *bits);
	if (!probe.IsActive()) {
		*rate = 44100;
		*ch = 2;
		*bits = 16;
	}
}

static void CdRipKeepResult(int fmt, int qual, LPCTSTR wavPath, LPCTSTR outPath,
	TCHAR made[][MAX_PATH], int* madeN)
{
	if (!wavPath || !madeN || *madeN >= 99) return;
	BOOL enc = TRUE;
	if (fmt == 1) enc = EncodeWavToMp3(CString(wavPath), CString(outPath), qual);
	else if (fmt == 2) enc = EncodeWavToFlac(CString(wavPath), CString(outPath), qual);
	if (fmt != 0 && enc) {
		DeleteFile(wavPath);
		lstrcpyn(made[(*madeN)++], outPath, MAX_PATH);
	}
	else
		lstrcpyn(made[(*madeN)++], wavPath, MAX_PATH);
}

static RubberBand::RubberBandStretcher* CdRbCreate()
{
	try {
		RubberBand::RubberBandStretcher* sh = new RubberBand::RubberBandStretcher(
			44100, 2,
			RubberBand::RubberBandStretcher::OptionProcessRealTime |
			RubberBand::RubberBandStretcher::OptionEngineFaster |
			RubberBand::RubberBandStretcher::OptionThreadingNever |
			RubberBand::RubberBandStretcher::OptionTransientsCrisp |
			RubberBand::RubberBandStretcher::OptionPhaseLaminar,
			(double)TempoTimeRatioFromPos(tempo),
			(double)PitchScaleFromPos(pitch));
		sh->setDebugLevel(0);
		sh->setMaxProcessSize(8192);
		return sh;
	}
	catch (...) {
		return NULL;
	}
}

static void CdRbDestroy(RubberBand::RubberBandStretcher*& sh)
{
	if (!sh) return;
	try { delete sh; }
	catch (...) {}
	sh = NULL;
}

static void CdRbPull(RubberBand::RubberBandStretcher* sh, BYTE* lo, int* loN, int loCap)
{
	if (!sh || !lo || !loN) return;
	float outL[4096];
	float outR[4096];
	float* op[2] = { outL, outR };
	try {
		while (sh->available() > 0) {
			const int room = (loCap - *loN) / 4;
			if (room <= 0) break;
			size_t want = 4096;
			if ((int)want > room) want = (size_t)room;
			const size_t av = (size_t)sh->available();
			if (want > av) want = av;
			const size_t got = sh->retrieve(op, want);
			if (got == 0) break;
			INT16* dst = (INT16*)(lo + *loN);
			for (size_t i = 0; i < got; ++i) {
				float L = outL[i], R = outR[i];
				if (L > 1.f) L = 1.f; if (L < -1.f) L = -1.f;
				if (R > 1.f) R = 1.f; if (R < -1.f) R = -1.f;
				int vL = (int)floorf(L * 32767.f + 0.5f);
				int vR = (int)floorf(R * 32767.f + 0.5f);
				if (vL > 32767) vL = 32767; if (vL < -32768) vL = -32768;
				if (vR > 32767) vR = 32767; if (vR < -32768) vR = -32768;
				dst[i * 2] = (INT16)vL;
				dst[i * 2 + 1] = (INT16)vR;
			}
			*loN += (int)got * 4;
		}
	}
	catch (...) {}
}

static void CdRbFeed(RubberBand::RubberBandStretcher* sh, const BYTE* pcm, int bytes, BYTE* lo, int* loN, int loCap)
{
	if (!sh || !pcm || bytes < 4) return;
	const int frames = bytes / 4;
	if (frames <= 0 || frames > CD_RB_MAXFR) return;
	float inL[CD_RB_MAXFR];
	float inR[CD_RB_MAXFR];
	const INT16* s = (const INT16*)pcm;
	for (int i = 0; i < frames; ++i) {
		inL[i] = (float)s[i * 2] / 32768.f;
		inR[i] = (float)s[i * 2 + 1] / 32768.f;
	}
	float* ip[2] = { inL, inR };
	try {
		sh->setTimeRatio((double)TempoTimeRatioFromPos(tempo));
		sh->setPitchScale((double)PitchScaleFromPos(pitch));
		sh->process(ip, (size_t)frames, false);
	}
	catch (...) {
		return;
	}
	CdRbPull(sh, lo, loN, loCap);
}

static int CdProcessSrc(BYTE* src, int srcBytes, BOOL eqReset, AudioUpscaler* up,
	BYTE* dst, int dstCap, BOOL live, int dRate, int dCh, int dBits)
{
	if (!src || srcBytes <= 0) return 0;
	CdApplyEq(src, srcBytes, eqReset);
	int n = srcBytes;
	const BYTE* feed = src;
	int fr = 44100, fc = 2, fb = 16;
	if (up && up->IsActive() && dst && dstCap > 0) {
		up->PushInterleaved((const uint8_t*)src, srcBytes);
		n = up->PullInterleaved(dst, dstCap);
		feed = dst;
		fr = dRate; fc = dCh; fb = dBits;
	}
	else if (dst && dst != src && srcBytes <= dstCap) {
		memcpy(dst, src, (size_t)srcBytes);
		n = srcBytes;
	}
	if (live && n > 0)
		CdFeedLive(feed, n, fr, fc, fb);
	return n;
}

static void CdWaveWait(CCdPlayerDlg* self, WAVEHDR* hdr)
{
	while (!InterlockedCompareExchange(&self->m_playStop, 0, 0)) {
		int queued = 0;
		for (int i = 0; i < CCdPlayerDlg::CD_PLAY_BUFS; ++i)
			if (hdr[i].dwFlags & WHDR_INQUEUE) queued++;
		if (queued < CCdPlayerDlg::CD_PLAY_BUFS - 1) break;
		WaitForSingleObject(self->m_waveEvt, 80);
	}
}

void CALLBACK CCdPlayerDlg::WaveOutProc(HWAVEOUT, UINT msg, DWORD_PTR inst, DWORD_PTR, DWORD_PTR)
{
	CCdPlayerDlg* self = (CCdPlayerDlg*)inst;
	if (msg == WOM_DONE && self && self->m_waveEvt)
		SetEvent(self->m_waveEvt);
}

UINT __stdcall CCdPlayerDlg::PlayThread(void* p)
{
	CCdPlayerDlg* self = (CCdPlayerDlg*)p;
	int dRate = 44100, dCh = 2, dBits = 16;
	CdGetOutFormat(&dRate, &dCh, &dBits);
	AudioUpscaler up;
	up.Configure(44100, 2, 16, dRate, dCh, dBits);
	BOOL useUp = up.IsActive() ? TRUE : FALSE;
	if (!useUp) { dRate = 44100; dCh = 2; dBits = 16; }

	WAVEFORMATEX wf = {};
	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = (WORD)dCh;
	wf.nSamplesPerSec = (DWORD)dRate;
	wf.wBitsPerSample = (WORD)dBits;
	wf.nBlockAlign = (WORD)(dCh * (dBits / 8));
	wf.nAvgBytesPerSec = (DWORD)dRate * wf.nBlockAlign;
	HWAVEOUT hwo = NULL;
	MMRESULT wo = waveOutOpen(&hwo, WAVE_MAPPER, &wf, (DWORD_PTR)WaveOutProc, (DWORD_PTR)self, CALLBACK_FUNCTION);
	if (wo != MMSYSERR_NOERROR && (dCh > 2 || dBits != 16)) {
		WAVEFORMATEXTENSIBLE wfx = {};
		wfx.Format = wf;
		wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wfx.Format.cbSize = 22;
		wfx.Samples.wValidBitsPerSample = (WORD)dBits;
		wfx.dwChannelMask = (DWORD)DirectSoundChannelMaskForOutput(dCh, savedata.speaker_layout);
		static const GUID kPcm = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
		wfx.SubFormat = kPcm;
		wo = waveOutOpen(&hwo, WAVE_MAPPER, (WAVEFORMATEX*)&wfx, (DWORD_PTR)WaveOutProc, (DWORD_PTR)self, CALLBACK_FUNCTION);
	}
	if (wo != MMSYSERR_NOERROR) {
		dRate = 44100; dCh = 2; dBits = 16; useUp = FALSE;
		wf.wFormatTag = WAVE_FORMAT_PCM;
		wf.nChannels = 2;
		wf.nSamplesPerSec = 44100;
		wf.wBitsPerSample = 16;
		wf.nBlockAlign = 4;
		wf.nAvgBytesPerSec = 44100 * 4;
		wf.cbSize = 0;
		wo = waveOutOpen(&hwo, WAVE_MAPPER, &wf, (DWORD_PTR)WaveOutProc, (DWORD_PTR)self, CALLBACK_FUNCTION);
	}
	if (wo != MMSYSERR_NOERROR) {
		if (self->GetSafeHwnd())
			self->PostMessage(WM_CD_ENDED, 2, 0);
		return 0;
	}
	self->m_hwo = hwo;
	self->ApplyVolume();
	const int bufBytes = CD_PLAY_SECS * 2352;
	const int dstBpf = dCh * (dBits / 8);
	int dstChunk = (int)((__int64)CD_PLAY_SECS * 588 * dRate / 44100 * dstBpf);
	if (dstChunk < bufBytes) dstChunk = bufBytes;
	const int dstCap = dstChunk * 4;
	BYTE pcm[CD_PLAY_BUFS][CD_PLAY_SECS * 2352];
	BYTE xfBuf[CD_PLAY_SECS * 2352];
	BYTE* outP[CD_PLAY_BUFS];
	WAVEHDR hdr[CD_PLAY_BUFS];
	ZeroMemory(hdr, sizeof(hdr));
	BOOL outAlloc = FALSE;
	for (int i = 0; i < CD_PLAY_BUFS; ++i) {
		if (useUp) {
			outP[i] = (BYTE*)malloc((size_t)dstCap);
			if (!outP[i]) { useUp = FALSE; outP[i] = pcm[i]; }
			else outAlloc = TRUE;
		}
		else outP[i] = pcm[i];
		hdr[i].lpData = (LPSTR)outP[i];
		hdr[i].dwBufferLength = (DWORD)(useUp ? dstChunk : bufBytes);
		waveOutPrepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
	}
	BYTE* dLo = useUp ? (BYTE*)malloc((size_t)dstCap * 2) : NULL;
	int dLoN = 0;
	const int dLoCap = dstCap * 2;
	int next = 0;
	BOOL eqReset = TRUE;
	BOOL discEnded = FALSE;
	const int loCap = bufBytes * 8;
	BYTE* leftover = (BYTE*)malloc((size_t)loCap);
	int loN = 0;
	RubberBand::RubberBandStretcher* rb = NULL;
	int xfNext = -1;
	DWORD xfLba = 0;
	BYTE* xfHold = NULL;
	int xfHoldN = 0;
	int xfHoldPos = 0;
	int xfHoldCap = 0;
	const DWORD xfSecsWant = (DWORD)(CdPlayXfadeSec() * 75.f + 0.5f);
	AudioUpscaler* upPtr = useUp ? &up : NULL;

	while (!InterlockedCompareExchange(&self->m_playStop, 0, 0) && self->m_alive) {
		if (self->m_paused) { Sleep(30); continue; }
		DWORD lba = self->m_playLba;
		DWORD end = self->m_playEnd;
		const DWORD loopA = self->m_loopStartLba;
		const DWORD loopB = self->m_loopEndLba;
		if (loopB > loopA + 8) {
			if (lba >= loopB) {
				self->m_playLba = loopA;
				const int tr = self->m_curTrack;
				if (tr >= 0 && tr < self->m_trackN)
					self->m_playFrames = (int)(loopA - self->m_startLba[tr]);
				self->PostMessage(WM_CD_POS, self->m_playFrames, tr);
				loN = 0; dLoN = 0;
				CdRbDestroy(rb);
				if (useUp) up.Reset();
				eqReset = TRUE;
				discEnded = FALSE;
				xfNext = -1;
				xfHoldN = xfHoldPos = 0;
				if (xfHold) { free(xfHold); xfHold = NULL; xfHoldCap = 0; }
				continue;
			}
			if (end > loopB) end = loopB;
		}
		if (lba >= end)
			discEnded = TRUE;

		const float tratio = TempoTimeRatioFromPos(tempo);
		const float pscale = PitchScaleFromPos(pitch);
		const BOOL wantRb = leftover && (fabsf(tratio - 1.f) > 0.02f
			|| fabsf(pscale - 1.f) > 0.02f || rb != NULL || loN > 0);

		DWORD nsec = CD_PLAY_SECS;
		BYTE* work = pcm[next];
		int workN = 0;
		BOOL haveSrc = FALSE;

		if (!wantRb) {
			if (discEnded) {
				self->PostMessage(WM_CD_ENDED, 0, 0);
				break;
			}
			if (lba + nsec > end) nsec = end - lba;
			if (nsec == 0) {
				self->PostMessage(WM_CD_ENDED, 0, 0);
				break;
			}
			EnterCriticalSection(&self->m_cdCs);
			HANDLE h = self->m_hCd;
			BOOL rd = (h && h != INVALID_HANDLE_VALUE) ? CdReadSectors(h, lba, nsec, pcm[next], bufBytes) : FALSE;
			LeaveCriticalSection(&self->m_cdCs);
			if (!rd) {
				self->PostMessage(WM_CD_ENDED, 1, 0);
				break;
			}
			workN = (int)(nsec * 2352);
			haveSrc = TRUE;
		}
		else if (!discEnded && loN < bufBytes) {
			if (lba + nsec > end) nsec = end - lba;
			if (nsec == 0)
				discEnded = TRUE;
			else {
				EnterCriticalSection(&self->m_cdCs);
				HANDLE h = self->m_hCd;
				BOOL rd = (h && h != INVALID_HANDLE_VALUE) ? CdReadSectors(h, lba, nsec, pcm[next], bufBytes) : FALSE;
				LeaveCriticalSection(&self->m_cdCs);
				if (!rd) {
					self->PostMessage(WM_CD_ENDED, 1, 0);
					break;
				}
				workN = (int)(nsec * 2352);
				haveSrc = TRUE;
			}
		}

		if (haveSrc && workN > 0) {
			const BOOL abOn = (loopB > loopA + 8);
			if (!abOn && xfSecsWant >= 8) {
				DWORD remain = (end > lba) ? (end - lba) : 0;
				if (remain <= xfSecsWant) {
					if (xfNext < 0) {
						if (self->m_shuffle.GetCheck() && self->m_trackN > 1) {
							xfNext = (int)((GetTickCount() / 17) % (DWORD)self->m_trackN);
							if (xfNext == self->m_curTrack) xfNext = (xfNext + 1) % self->m_trackN;
						}
						else {
							xfNext = self->m_curTrack + 1;
							if (xfNext >= self->m_trackN)
								xfNext = self->m_repeat.GetCheck() ? 0 : -1;
						}
						if (xfNext >= 0) xfLba = self->m_startLba[xfNext];
					}
					if (xfNext >= 0 && xfNext < self->m_trackN && xfHoldN == 0) {
						if (!xfHold) {
							xfHoldCap = (int)xfSecsWant * 2352;
							if (xfHoldCap < bufBytes) xfHoldCap = bufBytes;
							xfHold = (BYTE*)malloc((size_t)xfHoldCap);
							xfHoldPos = 0;
						}
						while (xfHold && xfHoldN + 2352 <= xfHoldCap && xfLba < self->m_endLba[xfNext]
							&& !InterlockedCompareExchange(&self->m_playStop, 0, 0)) {
							DWORD n2 = 32;
							if ((int)(n2 * 2352) > xfHoldCap - xfHoldN)
								n2 = (DWORD)((xfHoldCap - xfHoldN) / 2352);
							if (xfLba + n2 > self->m_endLba[xfNext])
								n2 = self->m_endLba[xfNext] - xfLba;
							if (n2 == 0) break;
							EnterCriticalSection(&self->m_cdCs);
							HANDLE h2 = self->m_hCd;
							BOOL rd2 = (h2 && h2 != INVALID_HANDLE_VALUE)
								? CdReadSectors(h2, xfLba, n2, xfHold + xfHoldN, (DWORD)(xfHoldCap - xfHoldN)) : FALSE;
							LeaveCriticalSection(&self->m_cdCs);
							if (!rd2) break;
							xfHoldN += (int)(n2 * 2352);
							xfLba += n2;
						}
					}
					if (xfHold && xfHoldPos < xfHoldN) {
						int mixN = workN;
						if (xfHoldPos + mixN > xfHoldN) mixN = xfHoldN - xfHoldPos;
						if (mixN > 0) {
							const BYTE* nxt = xfHold + xfHoldPos;
							if (mixN < workN) {
								memcpy(xfBuf, nxt, (size_t)mixN);
								ZeroMemory(xfBuf + mixN, (size_t)(workN - mixN));
								nxt = xfBuf;
							}
							const float t0 = 1.f - (float)remain / (float)xfSecsWant;
							const float t1 = 1.f - (float)(remain > nsec ? remain - nsec : 0) / (float)xfSecsWant;
							CdMixEqualPower(pcm[next], nxt, workN, t0, t1);
							xfHoldPos += mixN;
						}
					}
				}
			}
			self->m_playLba = lba + nsec;
			self->m_playFrames += (int)nsec;
			self->PostMessage(WM_CD_POS, self->m_playFrames, self->m_curTrack);
			if (self->m_playLba >= end && xfNext >= 0 && xfNext < self->m_trackN) {
				self->m_curTrack = xfNext;
				self->m_playLba = xfLba;
				self->m_playEnd = self->m_endLba[xfNext];
				self->m_playFrames = (int)(xfLba - self->m_startLba[xfNext]);
				self->PostMessage(WM_CD_POS, self->m_playFrames, xfNext);
				eqReset = TRUE;
				discEnded = FALSE;
				xfNext = -1;
				xfHoldN = xfHoldPos = 0;
				if (xfHold) { free(xfHold); xfHold = NULL; xfHoldCap = 0; }
				if (useUp) up.Reset();
			}
		}

		if (wantRb && haveSrc) {
			if (!rb) rb = CdRbCreate();
			if (rb)
				CdRbFeed(rb, pcm[next], workN, leftover, &loN, loCap);
			else {
				int n = workN;
				if (loN + n > loCap) n = loCap - loN;
				if (n > 0) { memcpy(leftover + loN, pcm[next], (size_t)n); loN += n; }
			}
		}
		if (wantRb && discEnded && rb) {
			try {
				float z = 0.f;
				float* zp[2] = { &z, &z };
				rb->process(zp, 0, true);
			}
			catch (...) {}
			CdRbPull(rb, leftover, &loN, loCap);
			CdRbDestroy(rb);
		}

		BYTE* srcPtr = pcm[next];
		int srcN = workN;
		if (wantRb) {
			if (loN <= 0) {
				if (discEnded) {
					self->PostMessage(WM_CD_ENDED, 0, 0);
					break;
				}
				continue;
			}
			srcN = loN;
			if (srcN > bufBytes) srcN = bufBytes;
			memcpy(pcm[next], leftover, (size_t)srcN);
			if (loN > srcN)
				memmove(leftover, leftover + srcN, (size_t)(loN - srcN));
			loN -= srcN;
			srcPtr = pcm[next];
		}
		else if (!haveSrc) {
			if (discEnded) {
				self->PostMessage(WM_CD_ENDED, 0, 0);
				break;
			}
			continue;
		}

		BYTE* dest = useUp ? outP[next] : pcm[next];
		int destCap = useUp ? dstCap : srcN;
		int got = CdProcessSrc(srcPtr, srcN, eqReset, upPtr, dest, destCap, TRUE, dRate, dCh, dBits);
		eqReset = FALSE;
		if (useUp && dLo) {
			if (got > 0) {
				if (dLoN + got > dLoCap) dLoN = 0;
				if (dLoN + got <= dLoCap) {
					memcpy(dLo + dLoN, dest, (size_t)got);
					dLoN += got;
				}
			}
			int take = dstChunk;
			if (take > dLoN) take = dLoN;
			if (take <= 0) continue;
			memcpy(outP[next], dLo, (size_t)take);
			if (dLoN > take)
				memmove(dLo, dLo + take, (size_t)(dLoN - take));
			dLoN -= take;
			got = take;
			dest = outP[next];
		}
		if (got <= 0) continue;
		hdr[next].lpData = (LPSTR)dest;
		hdr[next].dwBufferLength = (DWORD)got;
		hdr[next].dwFlags &= ~WHDR_DONE;
		waveOutWrite(hwo, &hdr[next], sizeof(WAVEHDR));
		next = (next + 1) % CD_PLAY_BUFS;
		CdWaveWait(self, hdr);
	}
	CdRbDestroy(rb);
	if (leftover) free(leftover);
	if (dLo) free(dLo);
	if (xfHold) free(xfHold);
	waveOutReset(hwo);
	for (int i = 0; i < CD_PLAY_BUFS; ++i) {
		waveOutUnprepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
		if (outAlloc && outP[i] && outP[i] != pcm[i]) free(outP[i]);
	}
	waveOutClose(hwo);
	self->m_hwo = NULL;
	return 0;
}

void CCdPlayerDlg::StartPlay(int trackIndex, DWORD startLba)
{
	if (trackIndex < 0 || trackIndex >= m_trackN) return;
	const int prev = m_curTrack;
	StopPlay(TRUE);
	if (m_abTrack >= 0 && trackIndex != m_abTrack) ClearAbLoop();
	else if (prev >= 0 && trackIndex != prev && m_abTrack < 0) ClearAbLoop();
	InterlockedExchange(&m_playStop, 0);
	m_curTrack = trackIndex;
	SyncSeekRange();
	ApplyAbLoop();
	DWORD start = startLba ? startLba : m_startLba[trackIndex];
	if (!startLba && m_loopEndLba > m_loopStartLba + 8)
		start = m_loopStartLba;
	if (start < m_startLba[trackIndex]) start = m_startLba[trackIndex];
	if (start >= m_endLba[trackIndex]) start = m_startLba[trackIndex];
	m_playLba = start;
	m_playEnd = (m_loopEndLba > m_loopStartLba + 8) ? m_loopEndLba : m_endLba[trackIndex];
	m_playFrames = (int)(m_playLba - m_startLba[trackIndex]);
	m_paused = FALSE;
	m_playTh = (HANDLE)_beginthreadex(NULL, 0, PlayThread, this, 0, NULL);
	m_listPlayTrack = trackIndex;
	m_list.SetItemState(-1, 0, LVIS_SELECTED);
	m_list.SetItemState(trackIndex, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void CCdPlayerDlg::OnPlay()
{
	const int want = SelectedTrack();
	if (m_paused && m_playTh && want == m_curTrack) { m_paused = FALSE; return; }
	StartPlay(want, 0);
}
void CCdPlayerDlg::PausePlay()
{
	OnPause();
}
void CCdPlayerDlg::EraseRw()
{
	BurnDisc(2);
}
void CCdPlayerDlg::OnPause()
{
	if (!m_playTh) return;
	m_paused = !m_paused;
	if (m_hwo) {
		if (m_paused) waveOutPause(m_hwo);
		else waveOutRestart(m_hwo);
	}
}
void CCdPlayerDlg::OnStop() { StopPlay(TRUE); }
void CCdPlayerDlg::PlayPrev()
{
	int t = m_curTrack;
	if (t < 0) t = SelectedTrack();
	t--;
	if (t < 0) t = m_trackN - 1;
	StartPlay(t, 0);
}
void CCdPlayerDlg::PlayNext()
{
	if (m_shuffle.GetCheck() && m_trackN > 1) {
		int t = (GetTickCount() / 17) % m_trackN;
		if (t == m_curTrack) t = (t + 1) % m_trackN;
		StartPlay(t, 0);
		return;
	}
	int t = m_curTrack;
	if (t < 0) t = SelectedTrack();
	t++;
	if (t >= m_trackN) {
		if (m_repeat.GetCheck()) t = 0;
		else { StopPlay(TRUE); return; }
	}
	StartPlay(t, 0);
}
void CCdPlayerDlg::OnPrev() { PlayPrev(); }
void CCdPlayerDlg::OnNext() { PlayNext(); }

void CCdPlayerDlg::OnListDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
	KillTimer(CD_TIMER_EDIT);
	m_pendingRow = m_pendingCol = -1;
	int tr = SelectedTrack();
	NMITEMACTIVATE* p = (NMITEMACTIVATE*)pNMHDR;
	if (p && p->iItem >= 0 && p->iItem < m_trackN) tr = p->iItem;
	StartPlay(tr, 0);
	*pResult = 0;
}

void CCdPlayerDlg::SeekToRatio(int pos)
{
	SeekToPos(pos);
}

void CCdPlayerDlg::SeekToPos(int pos)
{
	if (m_curTrack < 0 || m_curTrack >= m_trackN) return;
	const DWORD span = m_endLba[m_curTrack] - m_startLba[m_curTrack];
	if (pos < 0) pos = 0;
	if ((DWORD)pos > span) pos = (int)span;
	StartPlay(m_curTrack, m_startLba[m_curTrack] + (DWORD)pos);
}

void CCdPlayerDlg::SyncSeekRange()
{
	if (!m_seek.GetSafeHwnd()) return;
	int tot = 1000;
	int hz = 1;
	int tr = m_curTrack;
	if (tr < 0 || tr >= m_trackN) tr = SelectedTrack();
	if (tr >= 0 && tr < m_trackN) {
		tot = (int)(m_endLba[tr] - m_startLba[tr]);
		if (tot < 1) tot = 1;
		hz = 75;
	}
	m_seek.SetRange(0, tot, TRUE);
	m_seek.SetSelection(0, 0);
	m_seek.SetSelectionLocked(TRUE);
	m_seek.SetTimeBaseHz(hz);
	m_seek.SetAB(m_abA, m_abB);
}

void CCdPlayerDlg::ApplyAbLoop()
{
	int a = -1, b = -1;
	if (m_seek.GetSafeHwnd()) m_seek.GetAB(a, b);
	m_abA = a;
	m_abB = b;
	int tr = m_curTrack;
	if (tr < 0 || tr >= m_trackN) tr = SelectedTrack();
	if (tr < 0 || tr >= m_trackN || a < 0 || b < 0 || b <= a + 8) {
		m_loopStartLba = 0;
		m_loopEndLba = 0;
		if (m_curTrack >= 0 && m_curTrack < m_trackN)
			m_playEnd = m_endLba[m_curTrack];
		if (a < 0 && b < 0) m_abTrack = -1;
		RefreshAbButtons();
		return;
	}
	const DWORD ts = m_startLba[tr];
	const DWORD te = m_endLba[tr];
	DWORD la = ts + (DWORD)a;
	DWORD lb = ts + (DWORD)b;
	if (lb > te) lb = te;
	if (la < ts) la = ts;
	if (lb <= la + 8) {
		m_loopStartLba = 0;
		m_loopEndLba = 0;
		if (m_curTrack >= 0 && m_curTrack < m_trackN)
			m_playEnd = m_endLba[m_curTrack];
		RefreshAbButtons();
		return;
	}
	m_abTrack = tr;
	m_loopStartLba = la;
	m_loopEndLba = lb;
	if (m_curTrack == tr)
		m_playEnd = lb;
	RefreshAbButtons();
}

void CCdPlayerDlg::ClearAbLoop(BOOL announce)
{
	m_abA = m_abB = m_abTrack = -1;
	m_loopStartLba = m_loopEndLba = 0;
	if (m_seek.GetSafeHwnd()) m_seek.SetAB(-1, -1);
	if (m_curTrack >= 0 && m_curTrack < m_trackN)
		m_playEnd = m_endLba[m_curTrack];
	RefreshAbButtons();
	if (announce)
		SetStatus(LL14(L"A-B ループを解除しました", L"A-B loop cleared", L"Boucle A-B effacee", L"Loop A-B rimosso", L"Bucle A-B quitado", L"A-B 루프 해제", L"已解除 A-B 循环", L"أُلغيت حلقة A-B", L"Петля A-B снята", L"A-B-Schleife geloescht", L"Loop A-B limpo", L"A-B-lus gewist", L"Petla A-B wyczyszczona", L"A-B dongusu kaldirildi"));
}

void CCdPlayerDlg::SetAbAtPlayhead(BOOL isB)
{
	int tr = m_curTrack;
	if (tr < 0 || tr >= m_trackN) tr = SelectedTrack();
	if (tr < 0 || tr >= m_trackN) return;
	SyncSeekRange();
	int pos = m_seek.GetPos();
	const int mx = m_seek.GetMaxValue();
	if (pos < 0) pos = 0;
	if (pos > mx) pos = mx;
	if (isB) {
		if (m_abA < 0) m_abA = 0;
		m_abB = pos;
		if (m_abB < m_abA) { int t = m_abA; m_abA = m_abB; m_abB = t; }
		if (m_abB <= m_abA + 8) m_abB = (mx > m_abA + 8) ? min(mx, m_abA + 75) : mx;
	} else {
		m_abA = pos;
		if (m_abB >= 0 && m_abB <= m_abA + 8) m_abB = -1;
	}
	m_abTrack = tr;
	m_seek.SetAB(m_abA, m_abB);
	ApplyAbLoop();
	if (m_playTh && m_loopEndLba > m_loopStartLba + 8 && m_playLba >= m_loopEndLba)
		StartPlay(m_curTrack, m_loopStartLba);
	if (isB && m_abA >= 0 && m_abB > m_abA)
		SetStatus(LL14(L"A-B ループを開始しました", L"A-B loop started", L"Boucle A-B demarree", L"Loop A-B avviato", L"Bucle A-B iniciado", L"A-B 루프 시작", L"已开始 A-B 循环", L"بدأت حلقة A-B", L"Петля A-B начата", L"A-B-Schleife gestartet", L"Loop A-B iniciado", L"A-B-lus gestart", L"Petla A-B uruchomiona", L"A-B dongusu basladi"));
	else
		SetStatus(isB
			? LL14(L"B 点を設定しました", L"B point set", L"Point B defini", L"Punto B impostato", L"Punto B fijado", L"B 지점 설정", L"已设置 B 点", L"تم تعيين النقطة B", L"Точка B задана", L"Punkt B gesetzt", L"Ponto B definido", L"Punt B gezet", L"Punkt B ustawiony", L"B noktasi ayarlandi")
			: LL14(L"A 点を設定しました", L"A point set", L"Point A defini", L"Punto A impostato", L"Punto A fijado", L"A 지점 설정", L"已设置 A 点", L"تم تعيين النقطة A", L"Точка A задана", L"Punkt A gesetzt", L"Ponto A definido", L"Punt A gezet", L"Punkt A ustawiony", L"A noktasi ayarlandi"));
}

void CCdPlayerDlg::RefreshAbButtons()
{
	if (!m_abABtn.GetSafeHwnd()) return;
	const BOOL hasA = (m_abA >= 0);
	const BOOL hasB = (m_abB >= 0 && m_abB > m_abA);
	m_abABtn.SetGradation(
		hasA ? RGB(160, 220, 255) : RGB(220, 245, 255),
		hasA ? RGB(60, 150, 220) : RGB(160, 210, 240), 0, TRUE);
	m_abBBtn.SetGradation(
		hasB ? RGB(160, 220, 255) : RGB(220, 245, 255),
		hasB ? RGB(60, 150, 220) : RGB(160, 210, 240), 0, TRUE);
	m_abClrBtn.SetGradation(RGB(255, 230, 230), RGB(255, 180, 180), 0, TRUE);
	m_abClrBtn.EnableWindow(m_abA >= 0 || m_abB >= 0);
	m_abABtn.Invalidate(FALSE);
	m_abBBtn.Invalidate(FALSE);
	m_abClrBtn.Invalidate(FALSE);
}

void CCdPlayerDlg::OnAbA()
{
	int tr = m_curTrack;
	if (tr < 0 || tr >= m_trackN) tr = SelectedTrack();
	if (tr < 0 || tr >= m_trackN) {
		SetStatus(LL14(L"曲がありません", L"No track", L"Pas de piste", L"Nessuna traccia", L"No hay pista", L"곡이 없습니다", L"没有曲目", L"لا مقطع", L"Нет трека", L"Kein Titel", L"Sem faixa", L"Geen track", L"Brak utworu", L"Parca yok"));
		return;
	}
	SetAbAtPlayhead(FALSE);
}

void CCdPlayerDlg::OnAbB()
{
	int tr = m_curTrack;
	if (tr < 0 || tr >= m_trackN) tr = SelectedTrack();
	if (tr < 0 || tr >= m_trackN) {
		SetStatus(LL14(L"曲がありません", L"No track", L"Pas de piste", L"Nessuna traccia", L"No hay pista", L"곡이 없습니다", L"没有曲目", L"لا مقطع", L"Нет трека", L"Kein Titel", L"Sem faixa", L"Geen track", L"Brak utworu", L"Parca yok"));
		return;
	}
	SetAbAtPlayhead(TRUE);
}

void CCdPlayerDlg::OnAbClr()
{
	ClearAbLoop(TRUE);
}

void CCdPlayerDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (pScrollBar && pScrollBar->m_hWnd == m_vol.m_hWnd) {
		ApplyVolume();
		savedata.cdVolume = m_vol.GetPos();
	} else if (pScrollBar && pScrollBar->m_hWnd == m_seek.m_hWnd) {
		const int tgt = m_seek.GetDragTarget();
		if (nSBCode == TB_THUMBTRACK || nSBCode == SB_THUMBTRACK) {
			m_seekDrag = TRUE;
			m_seekDragTarget = tgt;
			if (tgt == 4 || tgt == 5) {
				m_seek.GetAB(m_abA, m_abB);
				ApplyAbLoop();
			}
		}
		if (nSBCode == TB_ENDTRACK || nSBCode == TB_THUMBPOSITION) {
			const int t = m_seekDragTarget ? m_seekDragTarget : tgt;
			m_seekDrag = FALSE;
			m_seekDragTarget = 0;
			if (t == 4 || t == 5) {
				m_seek.GetAB(m_abA, m_abB);
				ApplyAbLoop();
			} else if (t != 1 && t != 2) {
				SeekToPos(m_seek.GetPos());
			}
		}
	}
	CCustomBlurDialogBase::OnHScroll(nSBCode, nPos, pScrollBar);
}

LRESULT CCdPlayerDlg::OnPosMsg(WPARAM wParam, LPARAM lParam)
{
	const int tr = (int)lParam;
	if (tr < 0 || tr >= m_trackN) return 0;
	const int frames = (int)wParam;
	const int tot = (int)(m_endLba[tr] - m_startLba[tr]);
	if (!m_seekDrag && !m_seek.IsDragging() && tot > 0) {
		int pos = frames;
		if (pos < 0) pos = 0;
		if (pos > tot) pos = tot;
		m_seek.SetPlaybackMirror(pos, 0, 0, 0, tot, m_abA, m_abB);
	}
	const int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
	if (tr != m_listPlayTrack) {
		m_listPlayTrack = tr;
		if (sel != tr) {
			m_list.SetItemState(-1, 0, LVIS_SELECTED | LVIS_FOCUSED);
			m_list.SetItemState(tr, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_list.EnsureVisible(tr, FALSE);
		}
	}
	int cm, cs, tm, ts;
	CdLbaToMsf((DWORD)frames, &cm, &cs);
	CdLbaToMsf((DWORD)tot, &tm, &ts);
	TCHAR buf[64];
	wsprintf(buf, _T("%d:%02d / %d:%02d"), cm, cs, tm, ts);
	if (lstrcmp(buf, m_lastTime) != 0) {
		lstrcpy(m_lastTime, buf);
		m_time.SetWindowText(buf);
	}
	return 0;
}

LRESULT CCdPlayerDlg::OnEndedMsg(WPARAM wParam, LPARAM)
{
	if (wParam == 0) PlayNext();
	else if (wParam == 1)
		SetStatus(LL14(L"読み取りエラー", L"Read error", L"Erreur de lecture", L"Errore di lettura", L"Error de lectura",
			L"읽기 오류", L"读取错误", L"خطأ قراءة", L"Ошибка чтения", L"Lesefehler",
			L"Erro de leitura", L"Leesfout", L"Blad odczytu", L"Okuma hatasi"));
	return 0;
}

void CCdPlayerDlg::OnTimer(UINT_PTR id)
{
	if (id == CD_TIMER_MEDIA) {
		KillTimer(CD_TIMER_MEDIA);
		if (m_ready) {
			LoadToc();
			if (m_trackN <= 0 && !m_mediaRetry) {
				m_mediaRetry = TRUE;
				SetTimer(CD_TIMER_MEDIA, 1800, NULL);
			}
		}
		return;
	}
	if (id == CD_TIMER_EDIT) {
		KillTimer(CD_TIMER_EDIT);
		BeginCellEdit(m_pendingRow, m_pendingCol);
		return;
	}
	if (id == CD_TIMER_FOLLOW && savedata.cdMainLock) {
		CWnd* p = GetParent();
		if (p && p->GetSafeHwnd()) {
			CRect pr; p->GetWindowRect(&pr);
			if (m_parentX || m_parentY) {
				const int dx = pr.left - m_parentX, dy = pr.top - m_parentY;
				if (dx || dy) {
					CRect wr; GetWindowRect(&wr);
					SetWindowPos(NULL, wr.left + dx, wr.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				}
			}
			m_parentX = pr.left; m_parentY = pr.top;
		}
	}
	CCustomBlurDialogBase::OnTimer(id);
}

BOOL CCdPlayerDlg::OnDeviceChange(UINT nEventType, DWORD_PTR)
{
	if (nEventType == DBT_DEVICEARRIVAL || nEventType == DBT_DEVICEREMOVECOMPLETE)
		PostMessage(WM_CD_MEDIA, nEventType, 0);
	return TRUE;
}

void CCdPlayerDlg::OnFmtChange() { RefreshRipQuality(); PersistUi(); }
void CCdPlayerDlg::OnQualChange() { PersistUi(); }

void CCdPlayerDlg::OnBrowse()
{
	BROWSEINFO bi = {};
	TCHAR name[MAX_PATH] = {};
	bi.hwndOwner = m_hWnd;
	bi.pszDisplayName = name;
	bi.lpszTitle = LL14(L"取り込み先フォルダ", L"Rip folder", L"Dossier d'extraction", L"Cartella estrazione", L"Carpeta de extraccion",
		L"추출 폴더", L"抓轨文件夹", L"مجلد الاستخراج", L"Папка извлечения", L"Rip-Ordner",
		L"Pasta de extracao", L"Rip-map", L"Folder zgrywania", L"Aktarma klasoru");
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
	if (pidl) {
		TCHAR path[MAX_PATH];
		if (SHGetPathFromIDList(pidl, path)) m_folder.SetWindowText(path);
		CoTaskMemFree(pidl);
	}
}

void CCdPlayerDlg::RipTracks(int mode)
{
	if (m_trackN <= 0) return;
	CommitAlbumFields();
	EndCellEdit(TRUE);
	StopPlay(TRUE);
	PersistUi();
	m_ripMode = mode;
	if (mode == 0) CollectSelTracks(m_ripIdx, &m_ripN);
	else {
		m_ripN = m_trackN;
		for (int i = 0; i < m_trackN; ++i) m_ripIdx[i] = i;
	}
	CString fol; m_folder.GetWindowText(fol);
	lstrcpyn(m_ripFolder, fol, MAX_PATH);
	m_ripFmt = m_fmt.GetCurSel();
	m_ripQual = savedata.cdRipQual;
	CreateDirectory(m_ripFolder, NULL);
	InterlockedExchange(&m_ripStop, 0);
	m_progress.SetPos(0);
	m_ripTh = (HANDLE)_beginthreadex(NULL, 0, RipThread, this, 0, NULL);
}

void CCdPlayerDlg::OnRipSel() { RipTracks(0); }
void CCdPlayerDlg::OnRipAll() { RipTracks(1); }
void CCdPlayerDlg::OnRipOne() { RipTracks(2); }

BOOL CCdPlayerDlg::RipRangeToWav(CCdPlayerDlg* self, DWORD lba0, DWORD lba1, LPCTSTR wavPath,
	int dRate, int dCh, int dBits, BOOL live, int progIndex, int progCount)
{
	if (!self || !wavPath || lba1 <= lba0) return FALSE;
	CFile f;
	if (!f.Open(wavPath, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary))
		return FALSE;

	AudioUpscaler up;
	up.Configure(44100, 2, 16, dRate, dCh, dBits);
	AudioUpscaler* upPtr = up.IsActive() ? &up : NULL;
	if (!upPtr) {
		dRate = 44100;
		dCh = 2;
		dBits = 16;
	}
	CdWriteWavHdr(f, dRate, dCh, dBits);

	BYTE a[16 * 2352], b[16 * 2352];
	const int srcChunk = 16 * 2352;
	int dstCap = (int)((__int64)srcChunk * 8 * (__int64)dRate / 44100 * dCh / 2 * dBits / 16);
	if (dstCap < srcChunk * 4) dstCap = srcChunk * 4;
	BYTE* dst = (BYTE*)malloc((size_t)dstCap);
	if (!dst) {
		f.Close();
		DeleteFile(wavPath);
		return FALSE;
	}
	const int loCap = srcChunk * 8;
	BYTE* leftover = (BYTE*)malloc((size_t)loCap);
	int loN = 0;
	RubberBand::RubberBandStretcher* rb = NULL;
	BOOL eqReset = TRUE;
	DWORD lba = lba0;

	const float tratio = TempoTimeRatioFromPos(tempo);
	const float pscale = PitchScaleFromPos(pitch);
	const BOOL wantRb = leftover && (fabsf(tratio - 1.f) > 0.02f || fabsf(pscale - 1.f) > 0.02f);

	while (lba < lba1 && !InterlockedCompareExchange(&self->m_ripStop, 0, 0)) {
		DWORD n = 16;
		if (lba + n > lba1) n = lba1 - lba;
		EnterCriticalSection(&self->m_cdCs);
		HANDLE hcd = self->m_hCd;
		BOOL rd = (hcd && hcd != INVALID_HANDLE_VALUE)
			? CdReadSectorsVerified(hcd, lba, n, a, b, sizeof(a)) : FALSE;
		LeaveCriticalSection(&self->m_cdCs);
		if (!rd) break;

		BYTE* srcPtr = a;
		int srcN = (int)(n * 2352);
		if (wantRb) {
			if (!rb) rb = CdRbCreate();
			if (rb)
				CdRbFeed(rb, a, srcN, leftover, &loN, loCap);
			else {
				int take = srcN;
				if (loN + take > loCap) take = loCap - loN;
				if (take > 0) { memcpy(leftover + loN, a, (size_t)take); loN += take; }
			}
			if (loN <= 0) {
				lba += n;
				continue;
			}
			srcN = loN;
			if (srcN > srcChunk) srcN = srcChunk;
			memcpy(a, leftover, (size_t)srcN);
			if (loN > srcN)
				memmove(leftover, leftover + srcN, (size_t)(loN - srcN));
			loN -= srcN;
			srcPtr = a;
		}

		int got = CdProcessSrc(srcPtr, srcN, eqReset, upPtr, dst, dstCap, live, dRate, dCh, dBits);
		eqReset = FALSE;
		if (got > 0) f.Write(dst, got);
		if (upPtr) {
			for (;;) {
				int extra = upPtr->PullInterleaved(dst, dstCap);
				if (extra <= 0) break;
				f.Write(dst, extra);
				if (live) CdFeedLive(dst, extra, dRate, dCh, dBits);
			}
		}
		lba += n;
		if (progCount > 0 && lba1 > lba0) {
			const int local = (int)(((__int64)(lba - lba0) * 100) / (lba1 - lba0));
			const int pct = (progIndex * 100 + local) / progCount;
			self->PostMessage(WM_CD_RIPPROG, (WPARAM)pct, 0);
		}
	}

	if (rb) {
		try {
			float z = 0.f;
			float* zp[2] = { &z, &z };
			rb->process(zp, 0, true);
		}
		catch (...) {}
		CdRbPull(rb, leftover, &loN, loCap);
		CdRbDestroy(rb);
	}
	while (leftover && loN > 0) {
		int take = loN;
		if (take > srcChunk) take = srcChunk;
		int got = CdProcessSrc(leftover, take, eqReset, upPtr, dst, dstCap, live, dRate, dCh, dBits);
		eqReset = FALSE;
		if (got > 0) f.Write(dst, got);
		if (loN > take)
			memmove(leftover, leftover + take, (size_t)(loN - take));
		loN -= take;
	}
	if (upPtr) {
		BYTE zsec[2352];
		ZeroMemory(zsec, sizeof(zsec));
		upPtr->PushInterleaved(zsec, (int)sizeof(zsec));
		for (;;) {
			int extra = upPtr->PullInterleaved(dst, dstCap);
			if (extra <= 0) break;
			f.Write(dst, extra);
			if (live) CdFeedLive(dst, extra, dRate, dCh, dBits);
		}
	}

	if (leftover) free(leftover);
	free(dst);
	CdFinishWavHdr(f);
	f.Close();
	return TRUE;
}

UINT __stdcall CCdPlayerDlg::RipThread(void* p)
{
	CCdPlayerDlg* self = (CCdPlayerDlg*)p;
	TCHAR made[99][MAX_PATH];
	int madeN = 0;
	int dRate = 44100, dCh = 2, dBits = 16;
	CdGetRipOutFormat(self->m_ripFmt, &dRate, &dCh, &dBits);
	const float xf = CdRipXfadeSec();
	const TCHAR* ext = (self->m_ripFmt == 1) ? _T(".mp3") : (self->m_ripFmt == 2) ? _T(".flac") : _T(".wav");
	const BOOL joinXfade = (xf > 0.f) && (
		(self->m_ripMode == 2 && self->m_trackN >= 2) ||
		(self->m_ripMode != 2 && self->m_ripN >= 2));

	if (joinXfade) {
		TCHAR base[128];
		lstrcpyn(base, self->m_albumName[0] ? self->m_albumName : _T("CD"), 120);
		CdSanitizeName(base);
		TCHAR outp[MAX_PATH];
		wsprintf(outp, _T("%s\\%s%s"), self->m_ripFolder, base, ext);
		CString accum = (self->m_ripFmt == 0) ? CString(outp) : TcMakeTempWavPath();
		BOOL have = FALSE;
		const int n = (self->m_ripMode == 2) ? self->m_trackN : self->m_ripN;
		for (int k = 0; k < n && !InterlockedCompareExchange(&self->m_ripStop, 0, 0); ++k) {
			const int tr = (self->m_ripMode == 2) ? k : self->m_ripIdx[k];
			if (tr < 0 || tr >= self->m_trackN) continue;
			CString piece = have ? TcMakeTempWavPath() : accum;
			if (!RipRangeToWav(self, self->m_startLba[tr], self->m_endLba[tr], piece,
				dRate, dCh, dBits, TRUE, k, n)) {
				if (have) DeleteFile(piece);
				break;
			}
			if (!have)
				have = TRUE;
			else {
				TcAppendCrossfadeWav(accum, piece, xf);
				DeleteFile(piece);
			}
		}
		if (have)
			CdRipKeepResult(self->m_ripFmt, self->m_ripQual, accum, outp, made, &madeN);
	}
	else if (self->m_ripMode == 2) {
		TCHAR base[128];
		lstrcpyn(base, self->m_albumName[0] ? self->m_albumName : _T("CD"), 120);
		CdSanitizeName(base);
		TCHAR wav[MAX_PATH], outp[MAX_PATH];
		wsprintf(wav, _T("%s\\%s.wav"), self->m_ripFolder, base);
		wsprintf(outp, _T("%s\\%s%s"), self->m_ripFolder, base, ext);
		if (RipRangeToWav(self, self->m_startLba[0], self->m_leadout, wav,
			dRate, dCh, dBits, TRUE, 0, 1))
			CdRipKeepResult(self->m_ripFmt, self->m_ripQual, wav, outp, made, &madeN);
	}
	else {
		for (int k = 0; k < self->m_ripN && !InterlockedCompareExchange(&self->m_ripStop, 0, 0); ++k) {
			const int tr = self->m_ripIdx[k];
			if (tr < 0 || tr >= self->m_trackN) continue;
			TCHAR base[160];
			wsprintf(base, _T("%02d - %s"), self->m_firstTrack + tr, self->m_title[tr]);
			CdSanitizeName(base);
			TCHAR wav[MAX_PATH], outp[MAX_PATH];
			wsprintf(wav, _T("%s\\%s.wav"), self->m_ripFolder, base);
			wsprintf(outp, _T("%s\\%s%s"), self->m_ripFolder, base, ext);
			if (!RipRangeToWav(self, self->m_startLba[tr], self->m_endLba[tr], wav,
				dRate, dCh, dBits, TRUE, k, self->m_ripN))
				continue;
			CdRipKeepResult(self->m_ripFmt, self->m_ripQual, wav, outp, made, &madeN);
		}
	}
	if (savedata.cdRipAddPl && pl) {
		for (int i = 0; i < madeN; ++i) pl->AddFilePath(made[i]);
	}
	self->PostMessage(WM_CD_RIPDONE, (WPARAM)madeN, 0);
	return 0;
}

LRESULT CCdPlayerDlg::OnRipProgMsg(WPARAM wParam, LPARAM)
{
	m_progress.SetPos((int)wParam);
	return 0;
}
LRESULT CCdPlayerDlg::OnRipDoneMsg(WPARAM wParam, LPARAM)
{
	m_progress.SetPos(100);
	TCHAR buf[80];
	wsprintf(buf, LL14(L"%d ファイル取り込み完了", L"%d file(s) ripped", L"%d fichier(s) extraits", L"%d file estratti", L"%d archivo(s) extraidos",
		L"%d개 추출 완료", L"已抓取 %d 个文件", L"تم استخراج %d", L"Извлечено: %d", L"%d Datei(en) gerippt",
		L"%d arquivo(s) extraidos", L"%d bestand(en) geript", L"Zgrano %d plik(ow)", L"%d dosya aktarildi"), (int)wParam);
	SetStatus(buf);
	if (m_ripTh) { CloseHandle(m_ripTh); m_ripTh = NULL; }
	return 0;
}

class CCdCandDlg : public CCustomBlurDialogBase
{
public:
	CCdPlayerDlg* m_owner;
	int m_sel;
	CCustomListCtrl m_lc;
	CCustomStandardButton m_ok, m_no;
	CCdCandDlg(CCdPlayerDlg* o)
		: CCustomBlurDialogBase(IDD_MP_MBPICK, o), m_owner(o), m_sel(-1) {}
	~CCdCandDlg()
	{
		if (m_owner && m_owner->m_candUi == this) m_owner->m_candUi = NULL;
	}
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_MMP_LIST, m_lc);
		DDX_Control(pDX, IDC_MMP_APPLY, m_ok);
		DDX_Control(pDX, IDC_MMP_CANCEL, m_no);
	}
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		if (m_owner) m_owner->m_candUi = this;
		CCC_BringDialogToForeground(this);
		SetWindowText(LL14(L"CD 候補", L"CD matches", L"Correspondances CD", L"Corrispondenze CD", L"Coincidencias CD",
			L"CD 후보", L"CD 候选", L"نتائج القرص", L"Совпадения CD", L"CD-Treffer",
			L"Correspondencias CD", L"CD-treffers", L"Trafienia CD", L"CD eslesmeleri"));
		m_ok.SetWindowText(LL14(L"これを使う", L"Use this", L"Utiliser", L"Usa questo", L"Usar este",
			L"이것 사용", L"使用此项", L"استخدم هذا", L"Использовать", L"Diesen nehmen",
			L"Usar este", L"Deze gebruiken", L"Uzyj tego", L"Bunu kullan"));
		m_no.SetWindowText(LL14(L"キャンセル", L"Cancel", L"Annuler", L"Annulla", L"Cancelar",
			L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen",
			L"Cancelar", L"Annuleren", L"Anuluj", L"Iptal"));
		m_ok.SetFlat(TRUE);
		m_no.SetFlat(TRUE);
		m_ok.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
		m_no.SetGradation(RGB(250, 248, 240), RGB(200, 190, 160), 0, TRUE);
		m_lc.SetAeroMode(FALSE);
		m_lc.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
		m_lc.InsertColumn(0, _T("#"), LVCFMT_RIGHT, 28);
		m_lc.InsertColumn(1, LL14(L"出典", L"Source", L"Source", L"Fonte", L"Fuente", L"출처", L"来源", L"المصدر", L"Источник", L"Quelle", L"Fonte", L"Bron", L"Zrodlo", L"Kaynak"), LVCFMT_LEFT, 88);
		m_lc.InsertColumn(2, LL14(L"アルバム", L"Album", L"Album", L"Album", L"Album", L"앨범", L"专辑", L"الألبوم", L"Альбом", L"Album", L"Album", L"Album", L"Album", L"Album"), LVCFMT_LEFT, 180);
		m_lc.InsertColumn(3, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"فنان", L"Исполнитель", L"Interpret", L"Artista", L"Artiest", L"Wykonawca", L"Sanatci"), LVCFMT_LEFT, 140);
		m_lc.InsertColumn(4, LL14(L"曲数", L"Tracks", L"Pistes", L"Tracce", L"Pistas", L"곡수", L"曲数", L"مقاطع", L"Треки", L"Titel", L"Faixas", L"Nummers", L"Utwory", L"Parca"), LVCFMT_RIGHT, 50);
		const int n = m_owner ? m_owner->m_candN : 0;
		for (int i = 0; i < n; ++i) {
			TCHAR num[8]; wsprintf(num, _T("%d"), i + 1);
			const int row = m_lc.InsertItem(i, num);
			m_lc.SetItemText(row, 1, m_owner->m_candSrc[i]);
			m_lc.SetItemText(row, 2, m_owner->m_candAlbum[i]);
			m_lc.SetItemText(row, 3, m_owner->m_candArtist[i]);
			TCHAR tc[12]; wsprintf(tc, _T("%d"), m_owner->m_candTrkN[i] > 0 ? m_owner->m_candTrkN[i] : m_owner->m_trackN);
			m_lc.SetItemText(row, 4, tc);
			m_lc.SetItemData(row, (DWORD_PTR)i);
		}
		if (n > 0) {
			m_lc.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_sel = 0;
		}
		return TRUE;
	}
	afx_msg void OnUse()
	{
		POSITION pos = m_lc.GetFirstSelectedItemPosition();
		if (!pos) { EndDialog(IDCANCEL); return; }
		m_sel = (int)m_lc.GetItemData(m_lc.GetNextSelectedItem(pos));
		EndDialog(IDOK);
	}
	afx_msg void OnNo() { EndDialog(IDCANCEL); }
	afx_msg void OnDbl(NMHDR*, LRESULT* p) { *p = 0; OnUse(); }
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CCdCandDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MMP_APPLY, &CCdCandDlg::OnUse)
	ON_BN_CLICKED(IDC_MMP_CANCEL, &CCdCandDlg::OnNo)
	ON_NOTIFY(NM_DBLCLK, IDC_MMP_LIST, &CCdCandDlg::OnDbl)
END_MESSAGE_MAP()

BOOL CCdPlayerDlg::LookupCachePath(TCHAR* path, int cch, LPCTSTR ext) const
{
	if (!path || cch < 48 || !ext) return FALSE;
	path[0] = 0;
	if (!m_discidA[0] && !m_cddbId[0]) return FALSE;
	TCHAR tmp[MAX_PATH];
	const DWORD n = GetTempPath(MAX_PATH, tmp);
	if (n == 0 || n >= MAX_PATH) return FALSE;
	TCHAR id[48];
	id[0] = 0;
	if (m_discidA[0])
		MultiByteToWideChar(CP_ACP, 0, m_discidA, -1, id, 48);
	else
		MultiByteToWideChar(CP_ACP, 0, m_cddbId, -1, id, 48);
	if (!id[0]) return FALSE;
	for (int i = 0; id[i]; ++i) {
		const TCHAR c = id[i];
		if (c <= 32 || c == _T('\\') || c == _T('/') || c == _T(':') || c == _T('*')
			|| c == _T('?') || c == _T('"') || c == _T('<') || c == _T('>') || c == _T('|'))
			id[i] = _T('_');
	}
	if (lstrlen(tmp) + 16 + lstrlen(id) + lstrlen(ext) >= cch) return FALSE;
	wsprintf(path, _T("%soggysedbgm_cd_%s%s"), tmp, id, ext);
	return TRUE;
}

BOOL CCdPlayerDlg::TryApplyLookupCache()
{
	TCHAR path[MAX_PATH];
	if (!LookupCachePath(path, MAX_PATH, _T(".txt"))) return FALSE;
	if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES) return FALSE;
	CFile f;
	if (!f.Open(path, CFile::modeRead | CFile::typeBinary)) return FALSE;
	const ULONGLONG sz64 = f.GetLength();
	if (sz64 < 16 || sz64 > 65536 || (sz64 & 1)) { f.Close(); return FALSE; }
	const int nw = (int)(sz64 / sizeof(WCHAR));
	WCHAR* buf = new WCHAR[nw + 1];
	if (!buf) { f.Close(); return FALSE; }
	f.Read(buf, (UINT)sz64);
	f.Close();
	buf[nw] = 0;
	const WCHAR* p = buf;
	if (buf[0] == 0xFEFF) p = buf + 1;

	BOOL haveHdr = FALSE;
	int tracks = -1;
	const WCHAR* pass = p;
	while (*pass) {
		WCHAR line[512];
		int ln = 0;
		while (*pass && *pass != L'\r' && *pass != L'\n' && ln < 511)
			line[ln++] = *pass++;
		line[ln] = 0;
		while (*pass == L'\r' || *pass == L'\n') pass++;
		if (!line[0]) continue;
		if (lstrcmp(line, L"OGGCD1") == 0) { haveHdr = TRUE; continue; }
		if (wcsncmp(line, L"tracks=", 7) == 0)
			tracks = _wtoi(line + 7);
	}
	if (!haveHdr || tracks != m_trackN || m_trackN <= 0) {
		delete[] buf;
		return FALSE;
	}

	m_albumName[0] = m_albumArtist[0] = m_mbid[0] = 0;
	pass = p;
	while (*pass) {
		WCHAR line[512];
		int ln = 0;
		while (*pass && *pass != L'\r' && *pass != L'\n' && ln < 511)
			line[ln++] = *pass++;
		line[ln] = 0;
		while (*pass == L'\r' || *pass == L'\n') pass++;
		WCHAR* eq = line;
		while (*eq && *eq != L'=') eq++;
		if (*eq != L'=') continue;
		*eq = 0;
		const WCHAR* val = eq + 1;
		if (lstrcmp(line, L"album") == 0) lstrcpyn(m_albumName, val, 256);
		else if (lstrcmp(line, L"artist") == 0) lstrcpyn(m_albumArtist, val, 256);
		else if (lstrcmp(line, L"mbid") == 0) lstrcpyn(m_mbid, val, 48);
		else if (line[0] == L't' && line[1] >= L'0' && line[1] <= L'9' && line[2] >= L'0' && line[2] <= L'9' && !line[3]) {
			const int i = (line[1] - L'0') * 10 + (line[2] - L'0');
			if (i >= 0 && i < m_trackN) lstrcpyn(m_title[i], val, 128);
		} else if (line[0] == L'a' && line[1] >= L'0' && line[1] <= L'9' && line[2] >= L'0' && line[2] <= L'9' && !line[3]) {
			const int i = (line[1] - L'0') * 10 + (line[2] - L'0');
			if (i >= 0 && i < m_trackN) lstrcpyn(m_trArtist[i], val, 128);
		}
	}
	delete[] buf;
	if (!m_albumName[0] && !m_title[0][0]) return FALSE;

	FillTrackList();
	UpdateMetaUi();
	BOOL gotJpg = FALSE;
	TCHAR jpg[MAX_PATH];
	if (LookupCachePath(jpg, MAX_PATH, _T(".jpg")) && GetFileAttributes(jpg) != INVALID_FILE_ATTRIBUTES) {
		CImage img;
		if (SUCCEEDED(img.Load(jpg))) {
			HBITMAP hb = img.Detach();
			m_cover.SetImage(hb);
			gotJpg = TRUE;
		}
	}
	if (!gotJpg && m_mbid[0] && lstrlen(m_mbid) >= 32 && !m_coverTh)
		m_coverTh = (HANDLE)_beginthreadex(NULL, 0, CoverThread, this, 0, NULL);
	return TRUE;
}

void CCdPlayerDlg::SaveLookupCache()
{
	TCHAR path[MAX_PATH];
	if (!LookupCachePath(path, MAX_PATH, _T(".txt"))) return;
	if (m_trackN <= 0) return;
	WCHAR* body = new WCHAR[32768];
	if (!body) return;
	int n = wsprintf(body, L"OGGCD1\r\ntracks=%d\r\n", m_trackN);
	if (m_discidA[0] && n < 32000) {
		TCHAR idw[48];
		idw[0] = 0;
		MultiByteToWideChar(CP_ACP, 0, m_discidA, -1, idw, 48);
		n += wsprintf(body + n, L"discid=%s\r\n", idw);
	}
	const TCHAR* keys[3] = { L"album", L"artist", L"mbid" };
	const TCHAR* vals[3] = { m_albumName, m_albumArtist, m_mbid };
	for (int k = 0; k < 3 && n < 31000; ++k) {
		n += wsprintf(body + n, L"%s=", keys[k]);
		for (const TCHAR* s = vals[k]; *s && n < 32000; ++s) {
			if (*s != L'\r' && *s != L'\n')
				body[n++] = *s;
		}
		body[n++] = L'\r';
		body[n++] = L'\n';
		body[n] = 0;
	}
	for (int i = 0; i < m_trackN && n < 30000; ++i) {
		n += wsprintf(body + n, L"t%02d=", i);
		for (const TCHAR* s = m_title[i]; *s && n < 32000; ++s) {
			if (*s != L'\r' && *s != L'\n')
				body[n++] = *s;
		}
		body[n++] = L'\r';
		body[n++] = L'\n';
		n += wsprintf(body + n, L"a%02d=", i);
		for (const TCHAR* s = m_trArtist[i]; *s && n < 32000; ++s) {
			if (*s != L'\r' && *s != L'\n')
				body[n++] = *s;
		}
		body[n++] = L'\r';
		body[n++] = L'\n';
		body[n] = 0;
	}
	CFile f;
	if (f.Open(path, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary)) {
		WORD bom = 0xFEFF;
		f.Write(&bom, 2);
		f.Write(body, n * sizeof(WCHAR));
		f.Close();
	}
	delete[] body;
}

void CCdPlayerDlg::LookupNet(BOOL useSearch)
{
	if (m_lookupTh) {
		InterlockedExchange(&m_lookupStop, 1);
		if (WaitForSingleObject(m_lookupTh, 3000) == WAIT_OBJECT_0) {
			CloseHandle(m_lookupTh);
			m_lookupTh = NULL;
		} else
			return;
	}
	if (useSearch) m_search.GetWindowText(m_searchQ, 256);
	else m_searchQ[0] = 0;
	if (!useSearch && TryApplyLookupCache()) {
		SetStatus(LL14(L"キャッシュから適用しました", L"Applied from cache", L"Applique depuis le cache", L"Applicato dalla cache", L"Aplicado desde la cache",
			L"캐시에서 적용했습니다", L"已从缓存应用", L"طُبّق من الذاكرة المؤقتة", L"Применено из кэша", L"Aus dem Cache angewendet",
			L"Aplicado do cache", L"Toegepast uit cache", L"Zastosowano z cache", L"Onbellekten uygulandi"));
		return;
	}
	InterlockedExchange(&m_lookupStop, 0);
	SetStatus(LL14(L"ネット検索中…", L"Looking up…", L"Recherche…", L"Ricerca…", L"Buscando…",
		L"검색 중…", L"正在查找…", L"جارٍ البحث…", L"Поиск…", L"Suche…",
		L"Buscando…", L"Opzoeken…", L"Szukanie…", L"Araniyor…"));
	m_lookupTh = (HANDLE)_beginthreadex(NULL, 0, LookupThread, this, 0, NULL);
}
void CCdPlayerDlg::OnLookup() { LookupNet(FALSE); }
void CCdPlayerDlg::OnSearchGo() { LookupNet(TRUE); }

int CCdPlayerDlg::ParseMbJson(const char* json)
{
	if (!json) return m_candN;
	const char* relKey = strstr(json, "\"releases\"");
	if (!relKey) return m_candN;
	const char* arr = strchr(relKey, '[');
	if (!arr) return m_candN;
	const char* arrEnd = JsonBraceEnd(arr);
	if (!arrEnd) return m_candN;
	const char* p = arr + 1;
	while (p < arrEnd && m_candN < CD_MAX_CAND) {
		while (p < arrEnd && *p != '{') p++;
		if (p >= arrEnd) break;
		const char* e = JsonBraceEnd(p);
		if (!e || e > arrEnd) break;
		char id[48], title[256], name[256];
		id[0] = title[0] = name[0] = 0;
		JsonGetStrRange(p, e, "id", id, 48);
		JsonGetStrRange(p, e, "title", title, 256);
		JsonGetStrRange(p, e, "name", name, 256);
		BOOL dup = FALSE;
		if (id[0]) {
			for (int i = 0; i < m_candN; ++i)
				if (lstrcmpA(m_candMbid[i], id) == 0) { dup = TRUE; break; }
		}
		if (!dup && title[0]) {
			const int c = m_candN;
			lstrcpynA(m_candMbid[c], id, 48);
			Utf8ToT(title, m_candAlbum[c], 256);
			Utf8ToT(name, m_candArtist[c], 128);
			lstrcpyn(m_candSrc[c], _T("MusicBrainz"), 24);
			m_candTrkN[c] = 0;
			const char* tk = NULL;
			for (const char* q = p; q + 10 < e; ++q) {
				if (strncmp(q, "\"tracks\":", 9) == 0) {
					tk = q + 9;
					while (tk < e && *tk && *tk != '[') tk++;
					break;
				}
			}
			if (tk && tk < e && *tk == '[') {
				const char* te = JsonBraceEnd(tk);
				if (te && te <= e) {
					const char* tp = tk + 1;
					while (tp < te && m_candTrkN[c] < CD_MAX_TRACK) {
						while (tp < te && *tp != '{') tp++;
						if (tp >= te) break;
						const char* tend = JsonBraceEnd(tp);
						if (!tend || tend > te) break;
						char tn[128]; tn[0] = 0;
						JsonGetStrRange(tp, tend, "title", tn, 128);
						if (tn[0]) Utf8ToT(tn, m_candTrk[c][m_candTrkN[c]++], 128);
						tp = tend + 1;
					}
				}
			}
			m_candN++;
		}
		p = e + 1;
	}
	return m_candN;
}

int CCdPlayerDlg::ParseCddbRead(const char* text)
{
	if (!text || m_candN >= CD_MAX_CAND) return m_candN;
	char dt[256]; dt[0] = 0;
	if (!CddbGetField(text, "DTITLE", dt, 256))
		return m_candN;
	char art[256] = {}, alb[256] = {};
	const char* sl = strstr(dt, " / ");
	if (sl) {
		int n = (int)(sl - dt);
		if (n > 255) n = 255;
		memcpy(art, dt, n); art[n] = 0;
		lstrcpynA(alb, sl + 3, 256);
	} else {
		lstrcpynA(alb, dt, 256);
	}
	TCHAR album[256], artist[128];
	Utf8ToT(alb[0] ? alb : dt, album, 256);
	Utf8ToT(art, artist, 128);
	if (!album[0]) return m_candN;
	for (int i = 0; i < m_candN; ++i) {
		if (lstrcmpi(m_candAlbum[i], album) == 0 && lstrcmpi(m_candArtist[i], artist) == 0)
			return m_candN;
	}
	const int c = m_candN;
	lstrcpyn(m_candAlbum[c], album, 256);
	lstrcpyn(m_candArtist[c], artist, 128);
	lstrcpyn(m_candSrc[c], _T("GnuDB"), 24);
	m_candMbid[c][0] = 0;
	m_candTrkN[c] = 0;
	for (int t = 0; t < CD_MAX_TRACK; ++t) {
		char key[32]; wsprintfA(key, "TTITLE%d", t);
		char tn[128]; tn[0] = 0;
		if (!CddbGetField(text, key, tn, 128)) break;
		if (tn[0]) Utf8ToT(tn, m_candTrk[c][m_candTrkN[c]++], 128);
	}
	m_candN++;
	return m_candN;
}

int CCdPlayerDlg::AddCand(LPCTSTR album, LPCTSTR artist, const char* mbid, LPCTSTR src)
{
	if (m_candN >= CD_MAX_CAND || !album || !album[0]) return -1;
	for (int i = 0; i < m_candN; ++i) {
		if (lstrcmpi(m_candAlbum[i], album) == 0 && lstrcmpi(m_candArtist[i], artist ? artist : _T("")) == 0)
			return -1;
	}
	const int c = m_candN;
	lstrcpyn(m_candAlbum[c], album, 256);
	lstrcpyn(m_candArtist[c], artist ? artist : _T(""), 128);
	lstrcpyn(m_candSrc[c], src ? src : _T(""), 24);
	if (mbid && mbid[0]) lstrcpynA(m_candMbid[c], mbid, 48);
	else m_candMbid[c][0] = 0;
	m_candTrkN[c] = 0;
	m_candN++;
	return c;
}

int CCdPlayerDlg::ParseItunesJson(const char* json)
{
	if (!json) return m_candN;
	const char* key = strstr(json, "\"results\"");
	if (!key) return m_candN;
	const char* arr = strchr(key, '[');
	if (!arr) return m_candN;
	const char* arrEnd = JsonBraceEnd(arr);
	if (!arrEnd) return m_candN;
	const char* p = arr + 1;
	while (p < arrEnd && m_candN < CD_MAX_CAND) {
		while (p < arrEnd && *p != '{') p++;
		if (p >= arrEnd) break;
		const char* e = JsonBraceEnd(p);
		if (!e || e > arrEnd) break;
		char title[256], name[256];
		title[0] = name[0] = 0;
		JsonGetStrRange(p, e, "collectionName", title, 256);
		JsonGetStrRange(p, e, "artistName", name, 256);
		if (title[0]) {
			TCHAR alb[256], art[128];
			Utf8ToT(title, alb, 256);
			Utf8ToT(name, art, 128);
			AddCand(alb, art, NULL, _T("iTunes"));
		}
		p = e + 1;
	}
	return m_candN;
}

int CCdPlayerDlg::ParseDeezerJson(const char* json)
{
	if (!json) return m_candN;
	const char* key = strstr(json, "\"data\"");
	if (!key) return m_candN;
	const char* arr = strchr(key, '[');
	if (!arr) return m_candN;
	const char* arrEnd = JsonBraceEnd(arr);
	if (!arrEnd) return m_candN;
	const char* p = arr + 1;
	while (p < arrEnd && m_candN < CD_MAX_CAND) {
		while (p < arrEnd && *p != '{') p++;
		if (p >= arrEnd) break;
		const char* e = JsonBraceEnd(p);
		if (!e || e > arrEnd) break;
		char title[256], name[256];
		title[0] = name[0] = 0;
		JsonGetStrRange(p, e, "title", title, 256);
		JsonGetStrRange(p, e, "name", name, 256);
		if (title[0]) {
			TCHAR alb[256], art[128];
			Utf8ToT(title, alb, 256);
			Utf8ToT(name, art, 128);
			AddCand(alb, art, NULL, _T("Deezer"));
		}
		p = e + 1;
	}
	return m_candN;
}

void CCdPlayerDlg::ParseCdstubJson(const char* json)
{
	if (!json) return;
	const char* key = strstr(json, "\"cdstubs\"");
	if (!key) return;
	const char* arr = strchr(key, '[');
	if (!arr) return;
	const char* arrEnd = JsonBraceEnd(arr);
	if (!arrEnd) return;
	const char* p = arr + 1;
	while (p < arrEnd && m_candN < CD_MAX_CAND) {
		while (p < arrEnd && *p != '{') p++;
		if (p >= arrEnd) break;
		const char* e = JsonBraceEnd(p);
		if (!e || e > arrEnd) break;
		char title[256], name[256];
		title[0] = name[0] = 0;
		JsonGetStrRange(p, e, "title", title, 256);
		JsonGetStrRange(p, e, "artist", name, 256);
		if (title[0]) {
			TCHAR alb[256], art[128];
			Utf8ToT(title, alb, 256);
			Utf8ToT(name, art, 128);
			AddCand(alb, art, NULL, _T("CDStub"));
		}
		p = e + 1;
	}
}

void CCdPlayerDlg::ApplyCand(int idx)
{
	if (idx < 0 || idx >= m_candN) return;
	lstrcpyn(m_albumName, m_candAlbum[idx], 256);
	lstrcpyn(m_albumArtist, m_candArtist[idx], 256);
	MultiByteToWideChar(CP_ACP, 0, m_candMbid[idx], -1, m_mbid, 48);
	const int n = min(m_trackN, m_candTrkN[idx]);
	for (int i = 0; i < n; ++i) {
		lstrcpyn(m_title[i], m_candTrk[idx][i], 128);
		lstrcpyn(m_trArtist[i], m_albumArtist, 128);
	}
	FillTrackList();
	UpdateMetaUi();
	if (m_mbid[0] && lstrlen(m_mbid) >= 32 && !m_coverTh)
		m_coverTh = (HANDLE)_beginthreadex(NULL, 0, CoverThread, this, 0, NULL);
	SaveLookupCache();
	SetStatus(LL14(L"候補を適用しました", L"Match applied", L"Correspondance appliquee", L"Corrispondenza applicata", L"Coincidencia aplicada",
		L"후보를 적용했습니다", L"已应用候选", L"تم تطبيق النتيجة", L"Совпадение применено", L"Treffer angewendet",
		L"Correspondencia aplicada", L"Treffer toegepast", L"Zastosowano trafienie", L"Esleme uygulandi"));
}

void CCdPlayerDlg::ShowCandPicker()
{
	if (m_candN <= 0) return;
	CCdCandDlg dlg(this);
	if (dlg.DoModal() != IDOK || dlg.m_sel < 0) {
		SetStatus(LL14(L"候補の選択をやめました", L"No match selected", L"Aucune selection", L"Nessuna scelta", L"Sin seleccion",
			L"후보를 선택하지 않았습니다", L"未选择候选", L"لم يُختر", L"Совпадение не выбрано", L"Kein Treffer gewaehlt",
			L"Nenhuma escolha", L"Geen treffer gekozen", L"Nie wybrano", L"Esleme secilmedi"));
		return;
	}
	ApplyCand(dlg.m_sel);
}

UINT __stdcall CCdPlayerDlg::LookupThread(void* p)
{
	CCdPlayerDlg* self = (CCdPlayerDlg*)p;
	BYTE* buf = new BYTE[512 * 1024];
	if (!buf) {
		self->PostMessage(WM_CD_LOOKUP, 0, 0);
		return 0;
	}
	self->m_candN = 0;
	TCHAR url[1024];
	TCHAR idw[48] = {};
	if (self->m_discidA[0])
		MultiByteToWideChar(CP_ACP, 0, self->m_discidA, -1, idw, 48);
	int got = 0;
	if (idw[0] && !CdLookupAbort(self)) {
		wsprintf(url, _T("https://musicbrainz.org/ws/2/discid/%s?inc=artists+recordings&fmt=json"), idw);
		if (CdHttpGet(url, buf, 512 * 1024, &got))
			self->ParseMbJson((const char*)buf);
	}
	if (self->m_candN == 0 && idw[0] && !CdLookupAbort(self)) {
		Sleep(1100);
		if (CdLookupAbort(self)) goto lookup_done;
		wsprintf(url, _T("https://musicbrainz.org/ws/2/discid/%s?inc=artist-credits+recordings&fmt=json"), idw);
		if (CdHttpGet(url, buf, 512 * 1024, &got))
			self->ParseMbJson((const char*)buf);
	}
	if (self->m_candN == 0 && idw[0] && !CdLookupAbort(self)) {
		Sleep(1100);
		if (CdLookupAbort(self)) goto lookup_done;
		wsprintf(url, _T("https://musicbrainz.org/ws/2/release/?query=discid:%s&fmt=json&limit=10"), idw);
		if (CdHttpGet(url, buf, 512 * 1024, &got))
			self->ParseMbJson((const char*)buf);
	}
	if (self->m_searchQ[0] && !CdLookupAbort(self)) {
		if (idw[0]) Sleep(1100);
		if (CdLookupAbort(self)) goto lookup_done;
		TCHAR enc[512]; UrlEnc(self->m_searchQ, enc, 512);
		wsprintf(url, _T("https://musicbrainz.org/ws/2/release/?query=%s&fmt=json&limit=10"), enc);
		if (CdHttpGet(url, buf, 512 * 1024, &got))
			self->ParseMbJson((const char*)buf);
		if (self->m_candN == 0 && self->m_trackN > 0 && !CdLookupAbort(self)) {
			Sleep(1100);
			if (CdLookupAbort(self)) goto lookup_done;
			wsprintf(url, _T("https://musicbrainz.org/ws/2/release/?query=%s%%20AND%%20tracks:%d&fmt=json&limit=10"), enc, self->m_trackN);
			if (CdHttpGet(url, buf, 512 * 1024, &got))
				self->ParseMbJson((const char*)buf);
		}
	}
	if (self->m_candN < CD_MAX_CAND && self->m_trackN > 0 && self->m_leadout && !CdLookupAbort(self)) {
		Sleep(1100);
		if (CdLookupAbort(self)) goto lookup_done;
		TCHAR toc[1200];
		int tp = wsprintf(toc, _T("https://musicbrainz.org/ws/2/discid/-?toc=%d+%d+%u"),
			self->m_firstTrack, self->m_firstTrack + self->m_trackN - 1, self->m_leadout + 150);
		for (int i = 0; i < self->m_trackN && tp < 1000; ++i)
			tp += wsprintf(toc + tp, _T("+%u"), self->m_startLba[i] + 150);
		lstrcat(toc, _T("&inc=artists+recordings&fmt=json"));
		if (CdHttpGet(toc, buf, 512 * 1024, &got))
			self->ParseMbJson((const char*)buf);
	}
	if (idw[0] && self->m_candN < CD_MAX_CAND && !CdLookupAbort(self)) {
		Sleep(1100);
		if (CdLookupAbort(self)) goto lookup_done;
		wsprintf(url, _T("https://musicbrainz.org/ws/2/cdstub/?query=discid:%s&fmt=json&limit=8"), idw);
		if (CdHttpGet(url, buf, 512 * 1024, &got))
			self->ParseCdstubJson((const char*)buf);
	}
	if (self->m_cddbId[0] && self->m_trackN > 0 && self->m_candN < CD_MAX_CAND && !CdLookupAbort(self)) {
		static const TCHAR* gdbHost[] = {
			_T("https://gnudb.gnudb.org/~cddb/cddb.cgi"),
			_T("https://gnudb.org/~cddb/cddb.cgi"),
			_T("https://freedb.dbpoweramp.com/~cddb/cddb.cgi")
		};
		for (int hs = 0; hs < 3 && self->m_candN < CD_MAX_CAND && !CdLookupAbort(self); ++hs) {
			TCHAR qurl[2048];
			int n = wsprintf(qurl, _T("%s?cmd=cddb+query+%S+%d"), gdbHost[hs], self->m_cddbId, self->m_trackN);
			for (int i = 0; i < self->m_trackN && n < 1800; ++i)
				n += wsprintf(qurl + n, _T("+%u"), self->m_cddbOff[i]);
			wsprintf(qurl + n, _T("+%d&hello=oggysedbgm+local.oggysedbgm+oggYSEDbgm+1.0&proto=6"), self->m_cddbNsec);
			if (!CdHttpGetTry(qurl, buf, 512 * 1024, &got) || got <= 8) continue;
			char cat[8][32];
			char did[8][16];
			int nh = 0;
			const char* p = (const char*)buf;
			const int code = atoi(p);
			if (code == 200) {
				if (CddbParseHit(p, cat[0], 32, did[0], 16)) nh = 1;
			} else if (code == 210 || code == 211) {
				const char* e = strchr(p, '\n');
				if (e) p = e + 1;
				while (nh < 8 && *p && *p != '.') {
					char line[512];
					int li = 0;
					while (*p && *p != '\r' && *p != '\n' && li < 511) line[li++] = *p++;
					line[li] = 0;
					while (*p == '\r' || *p == '\n') p++;
					if (!line[0] || line[0] == '.') break;
					if (CddbParseHit(line, cat[nh], 32, did[nh], 16)) nh++;
				}
			}
			for (int h = 0; h < nh && self->m_candN < CD_MAX_CAND && !CdLookupAbort(self); ++h) {
				Sleep(250);
				TCHAR rurl[768];
				wsprintf(rurl, _T("%s?cmd=cddb+read+%S+%S&hello=oggysedbgm+local.oggysedbgm+oggYSEDbgm+1.0&proto=6"),
					gdbHost[hs], cat[h], did[h]);
				if (CdHttpGetTry(rurl, buf, 512 * 1024, &got) && got > 8)
					self->ParseCddbRead((const char*)buf);
			}
		}
	}
	{
		TCHAR term[260] = {};
		if (self->m_searchQ[0]) lstrcpyn(term, self->m_searchQ, 260);
		else {
			if (self->m_albumArtist[0]) {
				lstrcpyn(term, self->m_albumArtist, 120);
				if (self->m_albumName[0]) lstrcat(term, _T(" "));
			}
			if (self->m_albumName[0]) {
				const int n = lstrlen(term);
				lstrcpyn(term + n, self->m_albumName, 260 - n);
			}
		}
		if (term[0] && self->m_candN < CD_MAX_CAND && !CdLookupAbort(self)) {
			TCHAR enc[512]; UrlEnc(term, enc, 512);
			wsprintf(url, _T("https://itunes.apple.com/search?media=music&entity=album&limit=8&term=%s"), enc);
			if (CdHttpGetTry(url, buf, 512 * 1024, &got) && got > 8)
				self->ParseItunesJson((const char*)buf);
			if (self->m_candN < CD_MAX_CAND && !CdLookupAbort(self)) {
				wsprintf(url, _T("https://api.deezer.com/search/album?q=%s"), enc);
				if (CdHttpGetTry(url, buf, 512 * 1024, &got) && got > 8)
					self->ParseDeezerJson((const char*)buf);
			}
		}
		if (self->m_albumName[0] && self->m_candN < CD_MAX_CAND && !CdLookupAbort(self)) {
			Sleep(1100);
			if (CdLookupAbort(self)) goto lookup_done;
			TCHAR ae[256] = {}, re[256];
			if (self->m_albumArtist[0]) UrlEnc(self->m_albumArtist, ae, 256);
			UrlEnc(self->m_albumName, re, 256);
			if (ae[0])
				wsprintf(url, _T("https://musicbrainz.org/ws/2/release/?query=artist:%%22%s%%22%%20AND%%20release:%%22%s%%22&fmt=json&limit=8"), ae, re);
			else
				wsprintf(url, _T("https://musicbrainz.org/ws/2/release/?query=release:%%22%s%%22&fmt=json&limit=8"), re);
			if (CdHttpGet(url, buf, 512 * 1024, &got))
				self->ParseMbJson((const char*)buf);
		}
	}
lookup_done:
	delete[] buf;
	if (self->m_alive && self->GetSafeHwnd())
		self->PostMessage(WM_CD_LOOKUP, (!CdLookupAbort(self) && self->m_candN > 0) ? 1 : 0, 0);
	return 0;
}

LRESULT CCdPlayerDlg::OnLookupMsg(WPARAM wParam, LPARAM)
{
	if (m_lookupTh) { CloseHandle(m_lookupTh); m_lookupTh = NULL; }
	if (wParam && m_trackN > 0 && m_candN > 0) {
		TCHAR buf[80];
		wsprintf(buf, LL14(L"候補 %d 件", L"%d match(es)", L"%d correspondance(s)", L"%d corrispondenze", L"%d coincidencia(s)",
			L"후보 %d건", L"%d 个候选", L"%d نتيجة", L"Совпадений: %d", L"%d Treffer",
			L"%d correspondencia(s)", L"%d treffer(s)", L"Trafien: %d", L"%d esleme"), m_candN);
		SetStatus(buf);
		ShowCandPicker();
	} else if (m_trackN > 0) {
		SetStatus(LL14(L"検索できませんでした", L"Lookup failed", L"Recherche echouee", L"Ricerca non riuscita", L"Busqueda fallida",
			L"검색 실패", L"查找失败", L"فشل البحث", L"Поиск не удался", L"Suche fehlgeschlagen",
			L"Falha na busca", L"Opzoeken mislukt", L"Wyszukiwanie nieudane", L"Arama basarisiz"));
	}
	return 0;
}

UINT __stdcall CCdPlayerDlg::CoverThread(void* p)
{
	CCdPlayerDlg* self = (CCdPlayerDlg*)p;
	TCHAR url[256];
	wsprintf(url, _T("https://coverartarchive.org/release/%s/front-250"), self->m_mbid);
	BYTE* buf = new BYTE[400000];
	if (!buf) return 0;
	int got = 0;
	if (CdHttpGet(url, buf, 400000, &got) && got > 32) {
		if (self->m_coverJpg) delete[] self->m_coverJpg;
		self->m_coverJpg = buf;
		self->m_coverJpgLen = got;
		buf = NULL;
		self->PostMessage(WM_CD_COVER, 1, 0);
	}
	if (buf) delete[] buf;
	return 0;
}

LRESULT CCdPlayerDlg::OnCoverMsg(WPARAM wParam, LPARAM)
{
	if (m_coverTh) { CloseHandle(m_coverTh); m_coverTh = NULL; }
	if (!wParam || !m_coverJpg || m_coverJpgLen <= 0) return 0;
	TCHAR tmp[MAX_PATH];
	if (!LookupCachePath(tmp, MAX_PATH, _T(".jpg"))) {
		GetTempPath(MAX_PATH, tmp);
		lstrcat(tmp, _T("ogg_cd_cover.jpg"));
	}
	CFile f;
	if (f.Open(tmp, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary)) {
		f.Write(m_coverJpg, m_coverJpgLen);
		f.Close();
		CImage img;
		if (SUCCEEDED(img.Load(tmp))) {
			HBITMAP hb = img.Detach();
			m_cover.SetImage(hb);
		}
	}
	return 0;
}

void CCdPlayerDlg::BurnDisc(int kind)
{
	TCHAR buf[32768];
	ZeroMemory(buf, sizeof(buf));
	OPENFILENAME ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = m_hWnd;
	ofn.lpstrFile = buf;
	ofn.nMaxFile = 32768;
	ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
	if (kind == 0)
		ofn.lpstrFilter = _T("Audio (*.wav;*.mp3;*.flac)\0*.wav;*.mp3;*.flac\0All\0*.*\0");
	else
		ofn.lpstrFilter = _T("All\0*.*\0");
	if (!GetOpenFileName(&ofn)) return;
	m_burnN = 0;
	TCHAR dir[MAX_PATH];
	lstrcpy(dir, buf);
	TCHAR* p = buf + lstrlen(buf) + 1;
	if (*p == 0) {
		lstrcpy(m_burnFiles[m_burnN++], buf);
	} else {
		while (*p && m_burnN < 64) {
			wsprintf(m_burnFiles[m_burnN], _T("%s\\%s"), dir, p);
			m_burnN++;
			p += lstrlen(p) + 1;
		}
	}
	m_burnKind = kind;
	InterlockedExchange(&m_burnStop, 0);
	m_progress.SetPos(0);
	m_burnTh = (HANDLE)_beginthreadex(NULL, 0, BurnThread, this, 0, NULL);
}
void CCdPlayerDlg::OnBurnAudio() { BurnDisc(0); }
void CCdPlayerDlg::OnBurnData() { BurnDisc(1); }
void CCdPlayerDlg::OnErase() { BurnDisc(2); }

UINT __stdcall CCdPlayerDlg::BurnThread(void* p)
{
	CCdPlayerDlg* self = (CCdPlayerDlg*)p;
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	HRESULT hr = E_FAIL;
	IDiscMaster2* master = NULL;
	if (FAILED(CoCreateInstance(__uuidof(MsftDiscMaster2), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&master))) || !master) {
		self->PostMessage(WM_CD_BURNDONE, 0, 0);
		CoUninitialize();
		return 0;
	}
	IDiscRecorder2* rec = NULL;
	LONG nrec = 0;
	master->get_Count(&nrec);
	const int letter = self->CurrentDriveLetter();
	for (LONG i = 0; i < nrec; ++i) {
		CComBSTR uid;
		if (FAILED(master->get_Item(i, &uid))) continue;
		IDiscRecorder2* r = NULL;
		if (FAILED(CoCreateInstance(__uuidof(MsftDiscRecorder2), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&r))) || !r)
			continue;
		if (SUCCEEDED(r->InitializeDiscRecorder(uid))) {
			SAFEARRAY* sa = NULL;
			if (SUCCEEDED(r->get_VolumePathNames(&sa)) && sa) {
				BSTR* paths = NULL;
				SafeArrayAccessData(sa, (void**)&paths);
				LONG ub = 0; SafeArrayGetUBound(sa, 1, &ub);
				for (LONG k = 0; k <= ub; ++k) {
					if (paths[k] && paths[k][0] == letter) { rec = r; r = NULL; break; }
				}
				SafeArrayUnaccessData(sa);
				SafeArrayDestroy(sa);
			}
		}
		if (r) r->Release();
		if (rec) break;
	}
	master->Release();
	if (!rec) {
		self->PostMessage(WM_CD_BURNDONE, 0, 0);
		CoUninitialize();
		return 0;
	}
	if (self->m_burnKind == 2) {
		IDiscFormat2Erase* er = NULL;
		if (SUCCEEDED(CoCreateInstance(__uuidof(MsftDiscFormat2Erase), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&er))) && er) {
			er->put_Recorder(rec);
			er->put_FullErase(VARIANT_TRUE);
			hr = er->EraseMedia();
			er->Release();
		}
	} else if (self->m_burnKind == 0) {
		IDiscFormat2TrackAtOnce* tao = NULL;
		if (SUCCEEDED(CoCreateInstance(__uuidof(MsftDiscFormat2TrackAtOnce), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&tao))) && tao) {
			tao->put_Recorder(rec);
			tao->put_ClientName(CComBSTR(L"oggYSEDbgm"));
			hr = tao->PrepareMedia();
			TCHAR tmpdir[MAX_PATH]; GetTempPath(MAX_PATH, tmpdir);
			for (int i = 0; i < self->m_burnN && SUCCEEDED(hr) && !InterlockedCompareExchange(&self->m_burnStop, 0, 0); ++i) {
				TCHAR wav[MAX_PATH];
				lstrcpy(wav, self->m_burnFiles[i]);
				const TCHAR* ext = _tcsrchr(self->m_burnFiles[i], _T('.'));
				TCHAR tmpw[MAX_PATH];
				BOOL tempMade = FALSE;
				if (ext && (_tcsicmp(ext, _T(".mp3")) == 0 || _tcsicmp(ext, _T(".flac")) == 0)) {
					wsprintf(tmpw, _T("%sogg_cd_burn_%d.wav"), tmpdir, i);
					if (!CdMfToWav(self->m_burnFiles[i], tmpw)) continue;
					lstrcpy(wav, tmpw);
					tempMade = TRUE;
				}
				IStream* st = NULL;
				if (SUCCEEDED(SHCreateStreamOnFileEx(wav, STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, NULL, &st)) && st) {
					STATSTG sg = {};
					st->Stat(&sg, STATFLAG_NONAME);
					LARGE_INTEGER skip;
					skip.QuadPart = 80;
					st->Seek(skip, STREAM_SEEK_SET, NULL);
					hr = tao->AddAudioTrack(st);
					st->Release();
				}
				if (tempMade) DeleteFile(tmpw);
				self->PostMessage(WM_CD_BURNPROG, (WPARAM)((i + 1) * 100 / max(1, self->m_burnN)), 0);
			}
			if (SUCCEEDED(hr)) hr = tao->ReleaseMedia();
			tao->Release();
		}
	} else {
		IFileSystemImage* fs = NULL;
		if (SUCCEEDED(CoCreateInstance(__uuidof(MsftFileSystemImage), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fs))) && fs) {
			fs->put_FileSystemsToCreate((FsiFileSystems)(FsiFileSystemISO9660 | FsiFileSystemJoliet));
			IFsiDirectoryItem* root = NULL;
			fs->get_Root(&root);
			if (root) {
				for (int i = 0; i < self->m_burnN; ++i) {
					CComBSTR name(PathFindFileName(self->m_burnFiles[i]));
					IStream* stm = NULL;
					if (FAILED(SHCreateStreamOnFileEx(self->m_burnFiles[i],
						STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, NULL, &stm)) || !stm)
						continue;
					root->AddFile(name, stm);
					stm->Release();
				}
				root->Release();
			}
			IStream* result = NULL;
			IFileSystemImageResult* img = NULL;
			if (SUCCEEDED(fs->CreateResultImage(&img)) && img) {
				img->get_ImageStream(&result);
				img->Release();
			}
			fs->Release();
			IDiscFormat2Data* data = NULL;
			if (result && SUCCEEDED(CoCreateInstance(__uuidof(MsftDiscFormat2Data), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&data))) && data) {
				data->put_Recorder(rec);
				data->put_ClientName(CComBSTR(L"oggYSEDbgm"));
				hr = data->Write(result);
				data->Release();
			}
			if (result) result->Release();
		}
	}
	rec->Release();
	self->PostMessage(WM_CD_BURNDONE, SUCCEEDED(hr) ? 1 : 0, 0);
	CoUninitialize();
	return 0;
}

LRESULT CCdPlayerDlg::OnBurnProgMsg(WPARAM wParam, LPARAM)
{
	m_progress.SetPos((int)wParam);
	return 0;
}
LRESULT CCdPlayerDlg::OnBurnDoneMsg(WPARAM wParam, LPARAM)
{
	if (m_burnTh) { CloseHandle(m_burnTh); m_burnTh = NULL; }
	m_progress.SetPos(wParam ? 100 : 0);
	SetStatus(wParam
		? LL14(L"書き込み／消去が終わりました", L"Burn / erase finished", L"Gravure / effacement termine", L"Masterizzazione / cancellazione finita", L"Grabacion / borrado terminado",
			L"굽기/지우기 완료", L"刻录/擦除完成", L"انتهى النسخ/المسح", L"Запись / стирание готово", L"Brennen / Loschen fertig",
			L"Gravacao / apagamento concluido", L"Branden / wissen klaar", L"Wypalanie / czyszczenie zakonczone", L"Yazma / silme bitti")
		: LL14(L"書き込み／消去に失敗しました", L"Burn / erase failed", L"Echec gravure / effacement", L"Masterizzazione / cancellazione non riuscita", L"Fallo de grabacion / borrado",
			L"굽기/지우기 실패", L"刻录/擦除失败", L"فشل النسخ/المسح", L"Ошибка записи / стирания", L"Brennen / Loschen fehlgeschlagen",
			L"Falha na gravacao / apagamento", L"Branden / wissen mislukt", L"Blad wypalania / czyszczenia", L"Yazma / silme basarisiz"));
	return 0;
}

void CCdPlayerDlg::ShowHelpSheet()
{
	if (g_cdHelp && ::IsWindow(g_cdHelp->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_cdHelp, this);
		return;
	}
	CCdHelpDlg* dlg = new CCdHelpDlg(this);
	if (!dlg->Create(IDD_CD_HELP, this)) { delete dlg; return; }
	g_cdHelp = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}
void CCdPlayerDlg::OnHelp() { ShowHelpSheet(); }

void CCdPlayerDlg::PopupMenu(CPoint screen)
{
	CCustomPopupMenu menu;
	if (m_trackN > 0) {
		menu.AddCommand(IDM_CD_EDITCELL, LL14(L"セルを編集", L"Edit cell", L"Modifier la cellule", L"Modifica cella", L"Editar celda",
			L"셀 편집", L"编辑单元格", L"تحرير الخلية", L"Править ячейку", L"Zelle bearbeiten",
			L"Editar celula", L"Cel bewerken", L"Edytuj komorke", L"Hucreyi duzenle"));
		menu.AddCommand(IDM_CD_COPY, LL14(L"コピー", L"Copy", L"Copier", L"Copia", L"Copiar", L"복사", L"复制", L"نسخ", L"Копировать", L"Kopieren", L"Copiar", L"Kopieren", L"Kopiuj", L"Kopyala"));
		menu.AddCommand(IDM_CD_PASTE, LL14(L"貼り付け", L"Paste", L"Coller", L"Incolla", L"Pegar", L"붙여넣기", L"粘贴", L"لصق", L"Вставить", L"Einfuegen", L"Colar", L"Plakken", L"Wklej", L"Yapistir"));
		menu.AddSeparator();
	}
	CCustomPopupMenu* drv = menu.AddSubMenu(LL14(L"ドライブ", L"Drive", L"Lecteur", L"Unita", L"Unidad", L"드라이브", L"驱动器", L"محرك", L"Привод", L"Laufwerk", L"Unidade", L"Station", L"Naped", L"Surucu"));
	if (drv) {
		drv->AddCommand(IDM_CD_REFRESH, LL14(L"更新", L"Refresh", L"Actualiser", L"Aggiorna", L"Actualizar", L"새로고침", L"刷新", L"تحديث", L"Обновить", L"Aktualisieren", L"Atualizar", L"Vernieuwen", L"Odswiez", L"Yenile"));
		drv->AddCommand(IDM_CD_LOAD, LL14(L"読込", L"Load", L"Charger", L"Carica", L"Cargar", L"읽기", L"读取", L"تحميل", L"Загрузить", L"Laden", L"Carregar", L"Laden", L"Wczytaj", L"Yukle"));
		drv->AddCommand(IDM_CD_EJECT, LL14(L"取り出し", L"Eject", L"Ejecter", L"Espelli", L"Expulsar", L"꺼내기", L"弹出", L"إخراج", L"Извлечь", L"Auswerfen", L"Ejetar", L"Uitwerpen", L"Wysun", L"Cikar"));
	}
	CCustomPopupMenu* pb = menu.AddSubMenu(LL14(L"再生", L"Playback", L"Lecture", L"Riproduzione", L"Reproduccion", L"재생", L"播放", L"تشغيل", L"Воспроизведение", L"Wiedergabe", L"Reproducao", L"Afspelen", L"Odtwarzanie", L"Oynatma"));
	if (pb) {
		pb->AddCommand(IDM_CD_PLAY, LL14(L"再生", L"Play", L"Lecture", L"Play", L"Reproducir", L"재생", L"播放", L"تشغيل", L"Играть", L"Play", L"Play", L"Play", L"Odtwarzaj", L"Oynat"));
		pb->AddCommand(IDM_CD_PAUSE, LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف مؤقت", L"Пауза", L"Pause", L"Pausa", L"Pauze", L"Pauza", L"Duraklat"));
		pb->AddCommand(IDM_CD_STOP, LL14(L"停止", L"Stop", L"Stop", L"Stop", L"Detener", L"정지", L"停止", L"إيقاف", L"Стоп", L"Stopp", L"Parar", L"Stop", L"Stop", L"Durdur"));
		pb->AddCommand(IDM_CD_PREV, LL14(L"前へ", L"Prev", L"Prec.", L"Prec.", L"Ant.", L"이전", L"上一首", L"السابق", L"Пред.", L"Zuruck", L"Ant.", L"Vorige", L"Poprz.", L"Onceki"));
		pb->AddCommand(IDM_CD_NEXT, LL14(L"次へ", L"Next", L"Suiv.", L"Succ.", L"Sig.", L"다음", L"下一首", L"التالي", L"След.", L"Vor", L"Prox.", L"Volgende", L"Nastep.", L"Sonraki"));
		pb->AddSeparator();
		pb->AddCheck(IDM_CD_REPEAT, LL14(L"リピート", L"Repeat", L"Repeter", L"Ripeti", L"Repetir", L"반복", L"循环", L"تكرار", L"Повтор", L"Wiederholen", L"Repetir", L"Herhalen", L"Powtarzaj", L"Tekrar"), m_repeat.GetCheck() != 0);
		pb->AddCheck(IDM_CD_SHUFFLE, LL14(L"シャッフル", L"Shuffle", L"Aleatoire", L"Casuale", L"Aleatorio", L"셔플", L"随机", L"خلط", L"Перемешать", L"Zufall", L"Aleatorio", L"Shuffle", L"Losowo", L"Karistir"), m_shuffle.GetCheck() != 0);
		pb->AddSeparator();
		pb->AddCommand(IDM_CD_ABA, LL14(L"ここを A（［）", L"Set A here ([)", L"Definir A ici ([)", L"Imposta A qui ([)", L"Poner A aqui ([)", L"여기를 A ([)", L"把这里设为 A（［）", L"تعيين A هنا ([)", L"Поставить A здесь ([)", L"A hier setzen ([)", L"Definir A aqui ([)", L"A hier zetten ([)", L"Ustaw A tutaj ([)", L"Burayi A yap ([)"));
		pb->AddCommand(IDM_CD_ABB, LL14(L"ここを B（］）", L"Set B here (])", L"Definir B ici (])", L"Imposta B qui (])", L"Poner B aqui (])", L"여기를 B (])", L"把这里设为 B（］）", L"تعيين B هنا (])", L"Поставить B здесь (])", L"B hier setzen (])", L"Definir B aqui (])", L"B hier zetten (])", L"Ustaw B tutaj (])", L"Burayi B yap (])"));
		pb->AddCommand(IDM_CD_ABCLR, LL14(L"A-B ループ解除", L"Clear A-B loop", L"Effacer la boucle A-B", L"Togli il loop A-B", L"Quitar el bucle A-B", L"A-B 루프 해제", L"解除 A-B 循环", L"إلغاء حلقة A-B", L"Снять петлю A-B", L"A-B-Schleife loeschen", L"Limpar loop A-B", L"A-B-lus wissen", L"Wyczysc petle A-B", L"A-B dongusunu kaldir"));
	}
	CCustomPopupMenu* rip = menu.AddSubMenu(LL14(L"取り込み", L"Rip", L"Extraction", L"Estrazione", L"Extraccion", L"추출", L"抓轨", L"استخراج", L"Извлечение", L"Rippen", L"Extracao", L"Rippen", L"Zgrywanie", L"Aktarma"));
	if (rip) {
		CCustomPopupMenu* sel = rip->AddSubMenu(LL14(L"選択曲", L"Selected", L"Selection", L"Selezionate", L"Seleccion", L"선택", L"所选", L"المحدد", L"Выбранные", L"Auswahl", L"Selecionadas", L"Selectie", L"Zaznaczone", L"Secilen"));
		if (sel) {
			sel->AddCommand(IDM_CD_RIPSEL_WAV, L"WAV");
			sel->AddCommand(IDM_CD_RIPSEL_MP3, L"MP3");
			sel->AddCommand(IDM_CD_RIPSEL_FLAC, L"FLAC");
		}
		CCustomPopupMenu* all = rip->AddSubMenu(LL14(L"全曲 1曲ずつ", L"All separately", L"Toutes separees", L"Tutte separate", L"Todas sueltas", L"전곡 개별", L"整盘分轨", L"الكل منفصل", L"Все по файлам", L"Alle einzeln", L"Todas separadas", L"Alle apart", L"Wszystkie osobno", L"Hepsi ayri"));
		if (all) {
			all->AddCommand(IDM_CD_RIPALL_WAV, L"WAV");
			all->AddCommand(IDM_CD_RIPALL_MP3, L"MP3");
			all->AddCommand(IDM_CD_RIPALL_FLAC, L"FLAC");
		}
		CCustomPopupMenu* one = rip->AddSubMenu(LL14(L"全曲 1本", L"All as one", L"Tout en un", L"Tutto in uno", L"Todo en uno", L"전곡 하나", L"整盘合成", L"الكل ملف واحد", L"Всё в один", L"Alles in einer Datei", L"Tudo em um", L"Alles in een", L"Wszystko w jednym", L"Hepsi tek"));
		if (one) {
			one->AddCommand(IDM_CD_RIPONE_WAV, L"WAV");
			one->AddCommand(IDM_CD_RIPONE_MP3, L"MP3");
			one->AddCommand(IDM_CD_RIPONE_FLAC, L"FLAC");
		}
		rip->AddSeparator();
		rip->AddCheck(IDM_CD_ADDPL, LL14(L"プレイリストへ追加", L"Add to playlist", L"Ajouter a la playlist", L"Aggiungi alla playlist", L"Anadir a la lista", L"재생목록에 추가", L"加入播放列表", L"إضافة للقائمة", L"В плейлист", L"Zur Playlist", L"Adicionar a playlist", L"Naar playlist", L"Dodaj do listy", L"Listeye ekle"), m_addPl.GetCheck() != 0);
	}
	CCustomPopupMenu* burn = menu.AddSubMenu(LL14(L"書き込み", L"Burn", L"Gravure", L"Masterizzazione", L"Grabacion", L"굽기", L"刻录", L"نسخ", L"Запись", L"Brennen", L"Gravacao", L"Branden", L"Wypalanie", L"Yazma"));
	if (burn) {
		burn->AddCommand(IDM_CD_BURNAUD, LL14(L"音声CD", L"Audio CD", L"CD audio", L"CD audio", L"CD audio", L"오디오 CD", L"音频光盘", L"قرص صوتي", L"Audio CD", L"Audio-CD", L"CD de audio", L"Audio-CD", L"CD audio", L"Ses CD"));
		burn->AddCommand(IDM_CD_BURNDATA, LL14(L"データCD", L"Data CD", L"CD donnees", L"CD dati", L"CD datos", L"데이터 CD", L"数据光盘", L"قرص بيانات", L"Data CD", L"Daten-CD", L"CD de dados", L"Data-CD", L"CD danych", L"Veri CD"));
		burn->AddCommand(IDM_CD_ERASE, LL14(L"RW消去", L"Erase RW", L"Effacer RW", L"Cancella RW", L"Borrar RW", L"RW 지우기", L"擦除 RW", L"مسح RW", L"Стереть RW", L"RW loschen", L"Apagar RW", L"RW wissen", L"Wyczysc RW", L"RW sil"));
	}
	menu.AddSeparator();
	{
		CCustomPopupMenu* net = menu.AddSubMenu(LL14(L"ネット", L"Network", L"Reseau", L"Rete", L"Red", L"네트워크", L"联网", L"شبكة", L"Сеть", L"Netz", L"Rede", L"Netwerk", L"Siec", L"Ag"));
		if (net) {
			net->AddCommand(IDM_CD_LOOKUP, LL14(L"DiscID で検索", L"Lookup by DiscID", L"Recherche par DiscID", L"Cerca per DiscID", L"Buscar por DiscID", L"DiscID로 검색", L"按 DiscID 查找", L"بحث بـ DiscID", L"Поиск по DiscID", L"Suche per DiscID", L"Buscar por DiscID", L"Zoek via DiscID", L"Szukaj po DiscID", L"DiscID ile ara"));
			net->AddCommand(IDM_CD_SEARCH, LL14(L"名前で検索", L"Search by name", L"Chercher par nom", L"Cerca per nome", L"Buscar por nombre", L"이름으로 찾기", L"按名称搜索", L"بحث بالاسم", L"Поиск по имени", L"Suche nach Name", L"Pesquisar por nome", L"Zoek op naam", L"Szukaj po nazwie", L"Ada gore ara"));
		}
	}
	menu.AddCheck(IDM_CD_FOLLOW, LL14(L"メインに追随", L"Follow main", L"Suivre la fenetre principale", L"Segui la finestra principale", L"Seguir la ventana principal",
		L"메인 창을 따라가기", L"跟随主窗口", L"اتباع النافذة الرئيسية", L"Следовать за главным", L"Hauptfenster folgen",
		L"Seguir a janela principal", L"Hoofdvenster volgen", L"Podazaj za glownym", L"Ana pencereyi izle"), savedata.cdMainLock != 0);
	menu.AddCommand(IDM_CD_HELP, LL14(L"操作ガイド", L"Guide", L"Guide", L"Guida", L"Guia", L"가이드", L"操作指南", L"دليل", L"Руководство", L"Anleitung", L"Guia", L"Gids", L"Przewodnik", L"Kilavuz"));
	menu.AddSeparator();
	menu.AddCommand(IDM_CD_CLOSE, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	const UINT cmd = menu.Track(screen, this);
	switch (cmd) {
	case IDM_CD_REFRESH: OnRefresh(); break;
	case IDM_CD_LOAD: OnLoad(); break;
	case IDM_CD_EJECT: OnEject(); break;
	case IDM_CD_PLAY: OnPlay(); break;
	case IDM_CD_PAUSE: OnPause(); break;
	case IDM_CD_STOP: OnStop(); break;
	case IDM_CD_PREV: OnPrev(); break;
	case IDM_CD_NEXT: OnNext(); break;
	case IDM_CD_REPEAT: m_repeat.SetCheck(m_repeat.GetCheck() ? BST_UNCHECKED : BST_CHECKED); PersistUi(); break;
	case IDM_CD_SHUFFLE: m_shuffle.SetCheck(m_shuffle.GetCheck() ? BST_UNCHECKED : BST_CHECKED); PersistUi(); break;
	case IDM_CD_LOOKUP: OnLookup(); break;
	case IDM_CD_SEARCH: OnSearchGo(); break;
	case IDM_CD_RIPSEL_WAV: m_fmt.SetCurSel(0); RefreshRipQuality(); OnRipSel(); break;
	case IDM_CD_RIPSEL_MP3: m_fmt.SetCurSel(1); RefreshRipQuality(); OnRipSel(); break;
	case IDM_CD_RIPSEL_FLAC: m_fmt.SetCurSel(2); RefreshRipQuality(); OnRipSel(); break;
	case IDM_CD_RIPALL_WAV: m_fmt.SetCurSel(0); RefreshRipQuality(); OnRipAll(); break;
	case IDM_CD_RIPALL_MP3: m_fmt.SetCurSel(1); RefreshRipQuality(); OnRipAll(); break;
	case IDM_CD_RIPALL_FLAC: m_fmt.SetCurSel(2); RefreshRipQuality(); OnRipAll(); break;
	case IDM_CD_RIPONE_WAV: m_fmt.SetCurSel(0); RefreshRipQuality(); OnRipOne(); break;
	case IDM_CD_RIPONE_MP3: m_fmt.SetCurSel(1); RefreshRipQuality(); OnRipOne(); break;
	case IDM_CD_RIPONE_FLAC: m_fmt.SetCurSel(2); RefreshRipQuality(); OnRipOne(); break;
	case IDM_CD_ADDPL: m_addPl.SetCheck(m_addPl.GetCheck() ? BST_UNCHECKED : BST_CHECKED); PersistUi(); break;
	case IDM_CD_BURNAUD: OnBurnAudio(); break;
	case IDM_CD_BURNDATA: OnBurnData(); break;
	case IDM_CD_ERASE: OnErase(); break;
	case IDM_CD_HELP: OnHelp(); break;
	case IDM_CD_FOLLOW: savedata.cdMainLock = savedata.cdMainLock ? 0 : 1; MpPersistSavedataQuick(); break;
	case IDM_CD_CLOSE: OnCloseButton(); break;
	case IDM_CD_EDITCELL: {
		int row = m_cellRow, col = m_cellCol;
		if (row < 0) row = SelectedTrack();
		if (col != 1 && col != 2) col = 1;
		BeginCellEdit(row, col);
		break;
	}
	case IDM_CD_COPY: CopyMetaCells(); break;
	case IDM_CD_PASTE: PasteMetaCells(); break;
	case IDM_CD_ABA: SetAbAtPlayhead(FALSE); break;
	case IDM_CD_ABB: SetAbAtPlayhead(TRUE); break;
	case IDM_CD_ABCLR: ClearAbLoop(TRUE); break;
	default: break;
	}
}

void CCdPlayerDlg::OnContextMenu(CWnd*, CPoint point)
{
	if (point.x < 0) { CRect r; GetWindowRect(&r); point = r.CenterPoint(); }
	if (m_list.GetSafeHwnd()) {
		CPoint pt = point;
		m_list.ScreenToClient(&pt);
		LVHITTESTINFO ht = {}; ht.pt = pt;
		if (m_list.SubItemHitTest(&ht) >= 0 && ht.iItem >= 0) {
			m_cellRow = ht.iItem;
			m_cellCol = ht.iSubItem;
			if (!(m_list.GetItemState(ht.iItem, LVIS_SELECTED) & LVIS_SELECTED)) {
				for (int i = 0; i < m_list.GetItemCount(); ++i)
					m_list.SetItemState(i, 0, LVIS_SELECTED);
				m_list.SetItemState(ht.iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			}
		}
	}
	PopupMenu(point);
}

void CCdPlayerDlg::OnCloseButton() { DestroyWindow(); }
void CCdPlayerDlg::OnCancel() { DestroyWindow(); }
void CCdPlayerDlg::OnOK() {}

void CCdPlayerDlg::OnDestroy()
{
	KillTimer(CD_TIMER_EDIT);
	EndCellEdit(FALSE);
	if (m_cellEdit.GetSafeHwnd()) m_cellEdit.DestroyWindow();
	KillTimer(CD_TIMER_UI);
	KillTimer(CD_TIMER_DISC);
	KillTimer(CD_TIMER_FOLLOW);
	PersistUi();
	SaveWindowPos();
	InterlockedExchange(&m_alive, 0);
	StopPlay(TRUE);
	InterlockedExchange(&m_ripStop, 1);
	InterlockedExchange(&m_lookupStop, 1);
	InterlockedExchange(&m_burnStop, 1);
	if (m_ripTh) { WaitForSingleObject(m_ripTh, 15000); CloseHandle(m_ripTh); m_ripTh = NULL; }
	if (m_lookupTh) { WaitForSingleObject(m_lookupTh, 8000); CloseHandle(m_lookupTh); m_lookupTh = NULL; }
	if (m_coverTh) { WaitForSingleObject(m_coverTh, 8000); CloseHandle(m_coverTh); m_coverTh = NULL; }
	if (m_burnTh) { WaitForSingleObject(m_burnTh, 20000); CloseHandle(m_burnTh); m_burnTh = NULL; }
	CloseCdHandle();
	if (g_cdHelp && ::IsWindow(g_cdHelp->GetSafeHwnd()))
		g_cdHelp->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}

void CCdPlayerDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_cdDlg == this) g_cdDlg = NULL;
	delete this;
}

void OpenCdPlayerModeless(CWnd* parent)
{
	if (g_cdDlg && ::IsWindow(g_cdDlg->GetSafeHwnd())) {
		g_cdDlg->ShowWindow(SW_SHOW);
		g_cdDlg->SetForegroundWindow();
		return;
	}
	g_cdDlg = new CCdPlayerDlg(parent);
	if (!g_cdDlg->Create(IDD_CDPLAYER, parent)) {
		delete g_cdDlg; g_cdDlg = NULL; return;
	}
	g_cdDlg->ShowWindow(SW_SHOW);
	g_cdDlg->SetForegroundWindow();
}

void CloseCdPlayerIfOpen()
{
	if (g_cdDlg && ::IsWindow(g_cdDlg->GetSafeHwnd()))
		g_cdDlg->DestroyWindow();
}
