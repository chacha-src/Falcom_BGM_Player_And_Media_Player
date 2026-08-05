// TranscodeExport.cpp
// WAV書き出し結果を mp3 / FLAC に変換するUIとエンコード。

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "TranscodeExport.h"
#include "DecodeProgress.h"
#include "ExportTagUi.h"
#include "FileTagInfo.h"
#include "CPromptEngine.h"
#include <ShlObj.h>
#include <vector>
#include <algorithm>
#include <math.h>

#define FLAC__NO_DLL
#include "flac/stream_encoder.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

extern COggDlg* og;
extern void DoEvent();

namespace {

enum { TC_FMT_MP3 = 0, TC_FMT_FLAC = 1, TC_FMT_WAV = -1 };
enum { TC_TAB_WAV = 0, TC_TAB_MP3 = 1, TC_TAB_FLAC = 2 };
enum { TC_WAV_HDR = 80 };

struct TcWavInfo {
	WORD ch;
	DWORD hz;
	WORD bits;
	int blockAlign;
	__int64 dataOffset;
	__int64 dataBytes;
};

wchar_t TcMapInvalidFilenameChar(wchar_t c)
{
	switch (c) {
	case L'\\': return L'＼';
	case L'/':  return L'／';
	case L':':  return L'：';
	case L'*':  return L'＊';
	case L'?':  return L'？';
	case L'"':  return L'\xFF02';
	case L'<':  return L'＜';
	case L'>':  return L'＞';
	case L'|':  return L'｜';
	default:
		return (c < 32) ? L'_' : c;
	}
}

void TcTrimTrailingDotsAndSpaces(CString& s)
{
	while (s.GetLength() > 0) {
		const wchar_t c = s[s.GetLength() - 1];
		if (c == L'.' || c == L' ') s.Truncate(s.GetLength() - 1);
		else break;
	}
	if (s.IsEmpty()) s = L"_";
}

bool TcIsReservedDeviceName(const CString& upper)
{
	static const wchar_t* reserved[] = {
		L"CON", L"PRN", L"AUX", L"NUL",
		L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
		L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9",
		NULL
	};
	for (int i = 0; reserved[i]; ++i) {
		const int n = (int)wcslen(reserved[i]);
		if (upper.GetLength() == n && upper == reserved[i]) return true;
		if (upper.GetLength() > n && upper.Left(n) == reserved[i] && upper[n] == L'.') return true;
	}
	return false;
}

CString TcSanitizePathComponent(const CString& component)
{
	CString s = component;
	for (int i = 0; i < s.GetLength(); ++i)
		s.SetAt(i, TcMapInvalidFilenameChar(s[i]));
	TcTrimTrailingDotsAndSpaces(s);
	CString upper = s;
	upper.MakeUpper();
	if (TcIsReservedDeviceName(upper))
		s = L"_" + s;
	return s;
}

CString TcSanitizeFilePath(const CString& pathIn)
{
	if (pathIn.IsEmpty()) return pathIn;
	CString out;
	int i = 0;
	const int len = pathIn.GetLength();
	if (len >= 2 && pathIn[0] == L'\\' && pathIn[1] == L'\\') {
		out = L"\\\\";
		i = 2;
		int j = i;
		while (j < len && pathIn[j] != L'\\') ++j;
		if (j > i) out += TcSanitizePathComponent(pathIn.Mid(i, j - i));
		i = j;
	}
	else if (len >= 2 && pathIn[1] == L':') {
		out = pathIn.Left(2);
		i = 2;
	}
	if (i < len && pathIn[i] == L'\\') {
		out += L'\\';
		++i;
	}
	while (i < len) {
		int j = i;
		while (j < len && pathIn[j] != L'\\') ++j;
		CString part = pathIn.Mid(i, j - i);
		if (!part.IsEmpty())
			out += TcSanitizePathComponent(part);
		i = j;
		if (i < len && pathIn[i] == L'\\') {
			out += L'\\';
			++i;
		}
	}
	return out;
}

CString TcBaseNameFromItem(const playlistdata0& item)
{
	CString name = item.name;
	if (name.IsEmpty()) {
		CString fol = item.fol;
		const int pos = fol.ReverseFind(L'\\');
		if (pos >= 0) name = fol.Mid(pos + 1);
		else name = fol;
	}
	const int dot = name.ReverseFind(L'.');
	if (dot >= 0) name = name.Left(dot);
	return name;
}

CString TcDefaultFolderFromPc(const playlistdata0& item)
{
	CString defPath = item.fol;
	const int pos = defPath.ReverseFind(L'\\');
	if (pos >= 0) defPath = defPath.Left(pos + 1);
	return defPath;
}

bool TcBrowseFolder(CWnd* owner, CString& outFolder)
{
	BROWSEINFO bi = {};
	bi.hwndOwner = owner ? owner->GetSafeHwnd() : NULL;
	bi.lpszTitle = LL14(L"出力フォルダを選択", L"Select output folder", L"Choisir le dossier de sortie", L"Scegli cartella di output",
		L"Seleccionar carpeta de salida", L"출력 폴더 선택", L"选择输出文件夹", L"اختر مجلد الإخراج",
		L"Выберите папку вывода", L"Ausgabeordner wählen", L"Selecionar pasta de saída", L"Selecteer uitvoermap",
		L"Wybierz folder wyjściowy", L"Çıktı klasörünü seç");
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
	if (!pidl) return false;
	wchar_t path[MAX_PATH] = {};
	const BOOL got = SHGetPathFromIDList(pidl, path);
	CoTaskMemFree(pidl);
	if (!got || path[0] == L'\0') return false;
	outFolder = path;
	return true;
}

BOOL TcReadWavInfo(CFile& f, TcWavInfo& info)
{
	const __int64 fileLen = (__int64)f.GetLength();
	if (fileLen <= TC_WAV_HDR) return FALSE;
	BYTE hdr[TC_WAV_HDR];
	f.SeekToBegin();
	if (f.Read(hdr, TC_WAV_HDR) != TC_WAV_HDR) return FALSE;
	info.dataOffset = TC_WAV_HDR;
	info.dataBytes = fileLen - TC_WAV_HDR;
	if (memcmp(hdr, "RF64", 4) == 0)
		info.dataBytes = *(__int64*)(hdr + 28);
	info.ch = *(WORD*)(hdr + 58);
	info.hz = *(DWORD*)(hdr + 60);
	info.bits = *(WORD*)(hdr + 70);
	info.blockAlign = info.ch * info.bits / 8;
	if (info.blockAlign <= 0) info.blockAlign = 4;
	if (info.ch < 1 || info.hz == 0 || info.dataBytes <= 0) return FALSE;
	return TRUE;
}

void TcPcmFrameToInt32(const BYTE* frame, int ch, int bits, FLAC__int32* out)
{
	if (bits == 8) {
		for (int c = 0; c < ch; ++c)
			out[c] = (FLAC__int32)frame[c] - 128;
	}
	else if (bits == 16) {
		const short* s = (const short*)frame;
		for (int c = 0; c < ch; ++c)
			out[c] = (FLAC__int32)s[c];
	}
	else if (bits == 24) {
		for (int c = 0; c < ch; ++c) {
			const int o = c * 3;
			int v = (int)frame[o] | ((int)frame[o + 1] << 8) | ((int)frame[o + 2] << 16);
			if (v & 0x800000) v |= ~0xFFFFFF;
			out[c] = (FLAC__int32)v;
		}
	}
	else if (bits == 32) {
		const int* s = (const int*)frame;
		for (int c = 0; c < ch; ++c)
			out[c] = (FLAC__int32)s[c];
	}
	else {
		for (int c = 0; c < ch; ++c)
			out[c] = 0;
	}
}

void TcPcmFrameToInt16(const BYTE* frame, int ch, int bits, short* out)
{
	if (bits == 16) {
		memcpy(out, frame, ch * 2);
		return;
	}
	FLAC__int32 tmp[32];
	if (ch > 32) ch = 32;
	TcPcmFrameToInt32(frame, ch, bits, tmp);
	if (bits == 8) {
		for (int c = 0; c < ch; ++c) {
			int v = tmp[c] * 256;
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			out[c] = (short)v;
		}
	}
	else if (bits == 24) {
		for (int c = 0; c < ch; ++c) {
			int v = tmp[c] >> 8;
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			out[c] = (short)v;
		}
	}
	else if (bits == 32) {
		for (int c = 0; c < ch; ++c) {
			int v = tmp[c] >> 16;
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			out[c] = (short)v;
		}
	}
	else {
		for (int c = 0; c < ch; ++c)
			out[c] = 0;
	}
}

CString TcMakeTempWavPath()
{
	wchar_t dir[MAX_PATH] = {};
	GetTempPath(MAX_PATH, dir);
	static LONG s_seq = 0;
	const LONG n = InterlockedIncrement(&s_seq);
	CString path;
	path.Format(L"%sogg_tc_%u_%u_%ld.wav", dir, GetCurrentProcessId(), GetTickCount(), (long)n);
	return path;
}

void TcFinalizeWavHeader(CFile& f);

// MP3(MF)向けに 16bit PCM WAV を dstHz へリサンプルして書き出す
BOOL TcResampleWavToRate(const CString& srcPath, const CString& dstPath, DWORD dstHz)
{
	CFile in;
	if (!in.Open(srcPath, CFile::modeRead | CFile::shareDenyNone))
		return FALSE;
	TcWavInfo info = {};
	if (!TcReadWavInfo(in, info) || info.ch < 1 || info.ch > 2 || info.hz == 0 || dstHz == 0)
		return FALSE;

	CFile out;
	if (!out.Open(dstPath, CFile::modeCreate | CFile::modeReadWrite | CFile::shareExclusive))
		return FALSE;

	BYTE h[TC_WAV_HDR];
	memset(h, 0, sizeof(h));
	const WORD ch = info.ch;
	const WORD bits = 16;
	const WORD blockAlign = (WORD)(ch * bits / 8);
	memcpy(h + 0, "RIFF", 4);
	memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "JUNK", 4);
	*(DWORD*)(h + 16) = 28;
	memcpy(h + 48, "fmt ", 4);
	*(DWORD*)(h + 52) = 16;
	*(WORD*)(h + 56) = WAVE_FORMAT_PCM;
	*(WORD*)(h + 58) = ch;
	*(DWORD*)(h + 60) = dstHz;
	*(DWORD*)(h + 64) = dstHz * blockAlign;
	*(WORD*)(h + 68) = blockAlign;
	*(WORD*)(h + 70) = bits;
	memcpy(h + 72, "data", 4);
	out.Write(h, TC_WAV_HDR);

	const int inChunk = 8192;
	std::vector<BYTE> raw((size_t)info.blockAlign * inChunk);
	std::vector<short> inPcm((size_t)info.ch * inChunk);
	std::vector<short> edge((size_t)info.ch, 0);
	BOOL haveEdge = FALSE;
	double pos = 0.0; // 入力フレーム位置(全体)
	__int64 inFrameTotal = 0;
	in.Seek(info.dataOffset, CFile::begin);
	__int64 remain = info.dataBytes;
	const double step = (double)info.hz / (double)dstHz;

	std::vector<short> outPcm((size_t)info.ch * (inChunk * 4 + 16));
	while (remain >= info.blockAlign) {
		int frames = inChunk;
		if ((__int64)frames * info.blockAlign > remain)
			frames = (int)(remain / info.blockAlign);
		const UINT want = (UINT)(frames * info.blockAlign);
		if (in.Read(raw.data(), want) != want)
			return FALSE;
		for (int i = 0; i < frames; ++i)
			TcPcmFrameToInt16(raw.data() + i * info.blockAlign, info.ch, info.bits, inPcm.data() + i * info.ch);

		int outFrames = 0;
		const int maxOut = (int)(outPcm.size() / (size_t)info.ch);
		while (outFrames < maxOut) {
			const double absPos = pos;
			if (absPos >= (double)(inFrameTotal + frames))
				break;
			int local = (int)(absPos - (double)inFrameTotal);
			if (local < 0) {
				// 前チャンク末尾との補間
				if (!haveEdge) { pos += step; continue; }
				const double frac = absPos - floor(absPos);
				for (int c = 0; c < info.ch; ++c) {
					float a = (float)edge[c];
					float b = (float)inPcm[c];
					outPcm[(size_t)outFrames * info.ch + c] = (short)(a + (b - a) * (float)frac);
				}
			} else {
				int local2 = local + 1;
				if (local2 >= frames) local2 = frames - 1;
				const double frac = absPos - (double)(inFrameTotal + local);
				for (int c = 0; c < info.ch; ++c) {
					float a = (float)inPcm[(size_t)local * info.ch + c];
					float b = (float)inPcm[(size_t)local2 * info.ch + c];
					outPcm[(size_t)outFrames * info.ch + c] = (short)(a + (b - a) * (float)frac);
				}
			}
			++outFrames;
			pos += step;
		}
		if (outFrames > 0)
			out.Write(outPcm.data(), outFrames * info.ch * 2);

		for (int c = 0; c < info.ch; ++c)
			edge[c] = inPcm[(size_t)(frames - 1) * info.ch + c];
		haveEdge = TRUE;
		inFrameTotal += frames;
		remain -= want;
	}
	TcFinalizeWavHeader(out);
	out.Close();
	return TRUE;
}

// 本アプリ書き出しWAV(80byteヘッダ)のサイズ欄をファイル実長から確定
void TcFinalizeWavHeader(CFile& f)
{
	const __int64 fileLen = (__int64)f.GetLength();
	__int64 dataBytes = fileLen - TC_WAV_HDR;
	if (dataBytes < 0) dataBytes = 0;
	BYTE hdr[TC_WAV_HDR];
	f.SeekToBegin();
	if (f.Read(hdr, TC_WAV_HDR) != TC_WAV_HDR) return;
	const WORD ch = *(WORD*)(hdr + 58);
	const WORD bits = *(WORD*)(hdr + 70);
	int blockAlign = (int)ch * (int)bits / 8;
	if (blockAlign <= 0) blockAlign = 4;
	f.SeekToBegin();
	if (dataBytes <= (__int64)0x7FFFFFFF) {
		BYTE riff[12];
		memcpy(riff + 0, "RIFF", 4);
		*(DWORD*)(riff + 4) = (DWORD)(fileLen - 8);
		memcpy(riff + 8, "WAVE", 4);
		f.Write(riff, 12);
		f.Seek(76, CFile::begin);
		DWORD ds = (DWORD)dataBytes;
		f.Write(&ds, 4);
	}
	else {
		BYTE rf[48];
		memcpy(rf + 0, "RF64", 4);
		*(DWORD*)(rf + 4) = 0xFFFFFFFF;
		memcpy(rf + 8, "WAVE", 4);
		memcpy(rf + 12, "ds64", 4);
		*(DWORD*)(rf + 16) = 28;
		*(__int64*)(rf + 20) = fileLen - 8;
		*(__int64*)(rf + 28) = dataBytes;
		*(__int64*)(rf + 36) = dataBytes / blockAlign;
		*(DWORD*)(rf + 44) = 0;
		f.Write(rf, 48);
		f.Seek(76, CFile::begin);
		DWORD ds = 0xFFFFFFFF;
		f.Write(&ds, 4);
	}
}

// accum 末尾 xfadeSec 秒へ戻し、next 先頭と等パワーで混ぜてから next 残りを追記
BOOL TcAppendCrossfadeWav(const CString& accumPath, const CString& nextPath, int xfadeSec)
{
	if (xfadeSec < 1) xfadeSec = 1;
	CFile fa, fn;
	if (!fa.Open(accumPath, CFile::modeReadWrite | CFile::shareExclusive))
		return FALSE;
	if (!fn.Open(nextPath, CFile::modeRead | CFile::shareDenyWrite)) {
		fa.Close();
		return FALSE;
	}
	TcWavInfo ia = {}, in = {};
	if (!TcReadWavInfo(fa, ia) || !TcReadWavInfo(fn, in)) {
		fa.Close(); fn.Close();
		return FALSE;
	}
	if (ia.ch != in.ch || ia.hz != in.hz || ia.bits != in.bits || ia.blockAlign != in.blockAlign) {
		fa.Close(); fn.Close();
		return FALSE;
	}
	const int bpf = ia.blockAlign;
	const int ch = ia.ch;
	const int bits = ia.bits;
	if (bpf <= 0 || ch < 1) {
		fa.Close(); fn.Close();
		return FALSE;
	}
	const __int64 framesA = ia.dataBytes / bpf;
	const __int64 framesN = in.dataBytes / bpf;
	__int64 xfadeFrames = (__int64)xfadeSec * (__int64)ia.hz;
	if (xfadeFrames > framesA) xfadeFrames = framesA;
	if (xfadeFrames > framesN) xfadeFrames = framesN;
	if (xfadeFrames < 1) {
		// 重ねられないときは単純連結
		xfadeFrames = 0;
	}
	const __int64 mixStart = framesA - xfadeFrames;
	const int chunkFrames = 1024;
	BYTE bufA[1024 * 32];
	BYTE bufN[1024 * 32];
	if (bpf > 32) {
		fa.Close(); fn.Close();
		return FALSE;
	}

	for (__int64 fi = 0; fi < xfadeFrames; ) {
		int n = chunkFrames;
		if ((__int64)n > xfadeFrames - fi) n = (int)(xfadeFrames - fi);
		const __int64 posA = ia.dataOffset + (mixStart + fi) * bpf;
		const __int64 posN = in.dataOffset + fi * bpf;
		fa.Seek(posA, CFile::begin);
		fn.Seek(posN, CFile::begin);
		if (fa.Read(bufA, n * bpf) != (UINT)(n * bpf)) break;
		if (fn.Read(bufN, n * bpf) != (UINT)(n * bpf)) break;
		for (int i = 0; i < n; ++i) {
			const float t = (xfadeFrames <= 1) ? 1.f : (float)(fi + i) / (float)(xfadeFrames - 1);
			const float gOut = cosf(t * 1.5707963267948966f);
			const float gIn = sinf(t * 1.5707963267948966f);
			BYTE* pa = bufA + i * bpf;
			BYTE* pn = bufN + i * bpf;
			for (int c = 0; c < ch; ++c) {
				float a = 0.f, b = 0.f;
				if (bits == 16) {
					a = ((short*)pa)[c] / 32768.f;
					b = ((short*)pn)[c] / 32768.f;
				}
				else if (bits == 24) {
					const int oa = c * 3, ob = c * 3;
					int va = pa[oa] | (pa[oa + 1] << 8) | ((signed char)pa[oa + 2] << 16);
					int vb = pn[ob] | (pn[ob + 1] << 8) | ((signed char)pn[ob + 2] << 16);
					a = va / 8388608.f;
					b = vb / 8388608.f;
				}
				else if (bits == 32) {
					a = ((int*)pa)[c] / 2147483648.f;
					b = ((int*)pn)[c] / 2147483648.f;
				}
				else {
					a = (pa[c] - 128) / 128.f;
					b = (pn[c] - 128) / 128.f;
				}
				float o = a * gOut + b * gIn;
				if (o > 1.f) o = 1.f;
				if (o < -1.f) o = -1.f;
				if (bits == 16) {
					((short*)pa)[c] = (short)(o * 32767.f);
				}
				else if (bits == 24) {
					int v = (int)(o * 8388607.f);
					const int oa = c * 3;
					pa[oa] = (BYTE)(v & 0xFF);
					pa[oa + 1] = (BYTE)((v >> 8) & 0xFF);
					pa[oa + 2] = (BYTE)((v >> 16) & 0xFF);
				}
				else if (bits == 32) {
					((int*)pa)[c] = (int)(o * 2147483647.f);
				}
				else {
					pa[c] = (BYTE)(o * 127.f + 128.f);
				}
			}
		}
		fa.Seek(posA, CFile::begin);
		fa.Write(bufA, n * bpf);
		fi += n;
	}

	// next の残りを追記
	__int64 remain = framesN - xfadeFrames;
	fn.Seek(in.dataOffset + xfadeFrames * bpf, CFile::begin);
	fa.SeekToEnd();
	while (remain > 0) {
		int n = chunkFrames;
		if ((__int64)n > remain) n = (int)remain;
		const UINT got = fn.Read(bufN, n * bpf);
		const int frames = (int)(got / bpf);
		if (frames <= 0) break;
		fa.Write(bufN, frames * bpf);
		remain -= frames;
	}
	TcFinalizeWavHeader(fa);
	fa.Close();
	fn.Close();
	return TRUE;
}

// PCM 1フレームを float[-1,1] へ（最大8ch）
void TcPcmFrameToFloat(const BYTE* p, int ch, int bits, float* out)
{
	for (int c = 0; c < ch; ++c) {
		if (bits == 16)
			out[c] = ((const short*)p)[c] / 32768.f;
		else if (bits == 24) {
			const int o = c * 3;
			int iv = p[o] | (p[o + 1] << 8) | ((signed char)p[o + 2] << 16);
			out[c] = iv / 8388608.f;
		}
		else if (bits == 32)
			out[c] = ((const int*)p)[c] / 2147483648.f;
		else
			out[c] = (p[c] - 128) / 128.f;
	}
}

// 同時K曲ミックス。終了枠は末尾 xfadeSec で次曲と等パワー交接（プツ切れ防止）。
// gains[] は相対重み。再生中の重み合計で都度 1.0 に正規化する。
BOOL TcConcurrentMixWav(const CString& outPath, const CString* paths, const float* gains, int n, int K, int xfadeSec)
{
	enum { MAXN = 64, CHUNK = 1024 };
	if (outPath.IsEmpty() || !paths || !gains || n < 2 || n > MAXN) return FALSE;
	if (K < 2) K = 2;
	if (K > n) K = n;

	CFile src[MAXN];
	TcWavInfo infos[MAXN];
	__int64 frames[MAXN];
	TcWavInfo fmt = {};
	for (int i = 0; i < n; ++i) {
		if (!src[i].Open(paths[i], CFile::modeRead | CFile::shareDenyWrite)) {
			for (int j = 0; j < i; ++j) src[j].Close();
			return FALSE;
		}
		infos[i] = {};
		if (!TcReadWavInfo(src[i], infos[i])) {
			for (int j = 0; j <= i; ++j) src[j].Close();
			return FALSE;
		}
		if (i == 0) {
			fmt = infos[i];
		}
		else if (infos[i].ch != fmt.ch || infos[i].hz != fmt.hz || infos[i].bits != fmt.bits
			|| infos[i].blockAlign != fmt.blockAlign) {
			for (int j = 0; j <= i; ++j) src[j].Close();
			return FALSE;
		}
		frames[i] = infos[i].dataBytes / infos[i].blockAlign;
		if (frames[i] < 1) {
			for (int j = 0; j <= i; ++j) src[j].Close();
			return FALSE;
		}
	}

	const int bpf = fmt.blockAlign;
	const int ch = fmt.ch;
	const int bits = fmt.bits;
	if (bpf <= 0 || bpf > 32 || ch < 1 || ch > 8) {
		for (int i = 0; i < n; ++i) src[i].Close();
		return FALSE;
	}

	CFile fo;
	if (!fo.Open(outPath, CFile::modeCreate | CFile::modeReadWrite | CFile::shareExclusive)) {
		for (int i = 0; i < n; ++i) src[i].Close();
		return FALSE;
	}
	BYTE hdr[TC_WAV_HDR];
	src[0].SeekToBegin();
	if (src[0].Read(hdr, TC_WAV_HDR) != TC_WAV_HDR) {
		fo.Close();
		for (int i = 0; i < n; ++i) src[i].Close();
		return FALSE;
	}
	fo.Write(hdr, TC_WAV_HDR);

	__int64 xfadeFramesWant = 0;
	if (xfadeSec >= 1)
		xfadeFramesWant = (__int64)xfadeSec * (__int64)fmt.hz;

	// slot: 現曲。xfade中は next に次曲を重ね、旧曲はフェードアウト。
	int slotTrack[MAXN];
	__int64 slotPos[MAXN];
	int slotNext[MAXN];
	__int64 slotNextPos[MAXN];
	__int64 slotXfTotal[MAXN];
	__int64 slotXfPos[MAXN];
	for (int s = 0; s < MAXN; ++s) {
		slotTrack[s] = -1;
		slotPos[s] = 0;
		slotNext[s] = -1;
		slotNextPos[s] = 0;
		slotXfTotal[s] = 0;
		slotXfPos[s] = 0;
	}
	int nextQ = 0;
	bool wroteAny = false;

	BYTE bufA[CHUNK * 32];
	BYTE bufB[CHUNK * 32];
	BYTE outBuf[CHUNK * 32];
	float acc[CHUNK * 8];
	float wSum[CHUNK];
	float sampA[8];
	float sampB[8];

	for (;;) {
		// 空きスロットへ即時配置（初回／交接後）。xfadeなし時の補充もここ。
		for (int s = 0; s < K; ++s) {
			if (slotTrack[s] >= 0) continue;
			if (nextQ >= n) continue;
			slotTrack[s] = nextQ++;
			slotPos[s] = 0;
			slotNext[s] = -1;
			slotNextPos[s] = 0;
			slotXfTotal[s] = 0;
			slotXfPos[s] = 0;
		}

		// 終了が近いスロットへ、残量分だけ先に次曲を重ねて等パワー交接を開始
		if (xfadeFramesWant >= 1) {
			for (int s = 0; s < K; ++s) {
				const int ti = slotTrack[s];
				if (ti < 0 || slotNext[s] >= 0) continue;
				if (nextQ >= n) continue;
				const __int64 rem = frames[ti] - slotPos[s];
				if (rem <= 0 || rem > xfadeFramesWant) continue;
				const int ni = nextQ;
				__int64 xf = rem;
				if (xf > frames[ni]) xf = frames[ni];
				if (xf < 1) continue;
				slotNext[s] = nextQ++;
				slotNextPos[s] = 0;
				slotXfTotal[s] = xf;
				slotXfPos[s] = 0;
			}
		}

		__int64 minStep = 0x7FFFFFFFFFFFFFFFLL;
		bool any = false;
		for (int s = 0; s < K; ++s) {
			const int ti = slotTrack[s];
			if (ti < 0) continue;
			__int64 rem = frames[ti] - slotPos[s];
			if (rem <= 0) {
				if (slotNext[s] >= 0) {
					slotTrack[s] = slotNext[s];
					slotPos[s] = slotNextPos[s];
					slotNext[s] = -1;
					slotXfTotal[s] = 0;
					slotXfPos[s] = 0;
					rem = frames[slotTrack[s]] - slotPos[s];
					if (rem <= 0) { slotTrack[s] = -1; continue; }
				}
				else {
					slotTrack[s] = -1;
					continue;
				}
			}
			any = true;
			if (slotNext[s] >= 0) {
				const int ni = slotNext[s];
				__int64 xfRem = slotXfTotal[s] - slotXfPos[s];
				__int64 nRem = frames[ni] - slotNextPos[s];
				if (xfRem < rem) rem = xfRem;
				if (nRem < rem) rem = nRem;
			}
			else if (xfadeFramesWant >= 1 && nextQ < n) {
				// 交接開始点までで一度区切る（プツ切れ前に次曲を入れる）
				const __int64 untilXf = (frames[ti] - slotPos[s]) - xfadeFramesWant;
				if (untilXf > 0 && untilXf < rem) rem = untilXf;
			}
			if (rem < 1) rem = 1;
			if (rem < minStep) minStep = rem;
		}
		if (!any) break;

		int nFrames = CHUNK;
		if ((__int64)nFrames > minStep) nFrames = (int)minStep;
		memset(acc, 0, sizeof(float) * (size_t)nFrames * (size_t)ch);
		for (int i = 0; i < nFrames; ++i) wSum[i] = 0.f;

		for (int s = 0; s < K; ++s) {
			const int ti = slotTrack[s];
			if (ti < 0) continue;
			const __int64 pos = slotPos[s];
			src[ti].Seek(infos[ti].dataOffset + pos * bpf, CFile::begin);
			if (src[ti].Read(bufA, nFrames * bpf) != (UINT)(nFrames * bpf)) {
				slotTrack[s] = -1;
				slotNext[s] = -1;
				continue;
			}
			const bool xf = (slotNext[s] >= 0 && slotXfTotal[s] > 0);
			if (xf) {
				const int ni = slotNext[s];
				src[ni].Seek(infos[ni].dataOffset + slotNextPos[s] * bpf, CFile::begin);
				if (src[ni].Read(bufB, nFrames * bpf) != (UINT)(nFrames * bpf)) {
					slotNext[s] = -1;
					slotXfTotal[s] = 0;
				}
			}

			for (int i = 0; i < nFrames; ++i) {
				float gOut = 1.f, gIn = 0.f;
				if (xf) {
					const __int64 xi = slotXfPos[s] + i;
					const float t = (slotXfTotal[s] <= 1)
						? 1.f
						: (float)xi / (float)(slotXfTotal[s] - 1);
					gOut = cosf(t * 1.5707963267948966f);
					gIn = sinf(t * 1.5707963267948966f);
				}
				// サンプルは等パワー(cos/sin)、正規化重みは電力(cos^2/sin^2)で交接中の揺れを抑える
				TcPcmFrameToFloat(bufA + i * bpf, ch, bits, sampA);
				for (int c = 0; c < ch; ++c)
					acc[i * ch + c] += sampA[c] * (gains[ti] * gOut);
				if (xf && slotNext[s] >= 0) {
					TcPcmFrameToFloat(bufB + i * bpf, ch, bits, sampB);
					for (int c = 0; c < ch; ++c)
						acc[i * ch + c] += sampB[c] * (gains[slotNext[s]] * gIn);
					wSum[i] += gains[ti] * (gOut * gOut) + gains[slotNext[s]] * (gIn * gIn);
				}
				else {
					wSum[i] += gains[ti];
				}
			}

			slotPos[s] = pos + nFrames;
			if (xf && slotNext[s] >= 0) {
				slotNextPos[s] += nFrames;
				slotXfPos[s] += nFrames;
				if (slotXfPos[s] >= slotXfTotal[s] || slotPos[s] >= frames[ti]) {
					slotTrack[s] = slotNext[s];
					slotPos[s] = slotNextPos[s];
					slotNext[s] = -1;
					slotXfTotal[s] = 0;
					slotXfPos[s] = 0;
				}
			}
			else if (slotPos[s] >= frames[ti]) {
				slotTrack[s] = -1;
			}
		}

		// 再生中重みの合計で 1.0 に揃える（25+25→実質50+50、2曲同時時の半分音量を防ぐ）
		for (int i = 0; i < nFrames; ++i) {
			const float inv = (wSum[i] > 1.0e-8f) ? (1.f / wSum[i]) : 0.f;
			BYTE* po = outBuf + i * bpf;
			for (int c = 0; c < ch; ++c) {
				float o = acc[i * ch + c] * inv;
				if (o > 1.f) o = 1.f;
				if (o < -1.f) o = -1.f;
				if (bits == 16)
					((short*)po)[c] = (short)(o * 32767.f);
				else if (bits == 24) {
					int v = (int)(o * 8388607.f);
					const int oa = c * 3;
					po[oa] = (BYTE)(v & 0xFF);
					po[oa + 1] = (BYTE)((v >> 8) & 0xFF);
					po[oa + 2] = (BYTE)((v >> 16) & 0xFF);
				}
				else if (bits == 32)
					((int*)po)[c] = (int)(o * 2147483647.f);
				else
					po[c] = (BYTE)(o * 127.f + 128.f);
			}
		}
		fo.Write(outBuf, nFrames * bpf);
		wroteAny = true;
	}

	TcFinalizeWavHeader(fo);
	fo.Close();
	for (int i = 0; i < n; ++i) src[i].Close();
	return wroteAny ? TRUE : FALSE;
}

BOOL TcApplyTailFadeOutWav(const CString& path, int fadeSec)
{
	if (fadeSec <= 0) return TRUE;
	CFile f;
	if (!f.Open(path, CFile::modeReadWrite | CFile::shareExclusive))
		return FALSE;
	TcWavInfo info = {};
	if (!TcReadWavInfo(f, info) || info.blockAlign <= 0) {
		f.Close();
		return FALSE;
	}
	const __int64 totalFrames = info.dataBytes / info.blockAlign;
	__int64 fadeFrames = (__int64)fadeSec * (__int64)info.hz;
	if (fadeFrames > totalFrames) fadeFrames = totalFrames;
	if (fadeFrames <= 1) {
		f.Close();
		return TRUE;
	}
	const __int64 fadeStart = totalFrames - fadeFrames;
	const int bpf = info.blockAlign;
	const int bits = info.bits;
	const int ch = info.ch;
	BYTE frame[32];
	if (bpf > 32) {
		f.Close();
		return FALSE;
	}
	for (__int64 fi = fadeStart; fi < totalFrames; ++fi) {
		const float t = (float)(fi - fadeStart) / (float)(fadeFrames - 1);
		const float g = (1.f - t) * (1.f - t);
		const __int64 pos = info.dataOffset + fi * bpf;
		f.Seek(pos, CFile::begin);
		if (f.Read(frame, bpf) != (UINT)bpf) break;
		for (int c = 0; c < ch; ++c) {
			if (bits == 16) {
				short* s = (short*)frame;
				s[c] = (short)(s[c] * g);
			}
			else if (bits == 24) {
				const int o = c * 3;
				int v = frame[o] | (frame[o + 1] << 8) | ((signed char)frame[o + 2] << 16);
				v = (int)(v * g);
				frame[o] = (BYTE)(v & 0xFF);
				frame[o + 1] = (BYTE)((v >> 8) & 0xFF);
				frame[o + 2] = (BYTE)((v >> 16) & 0xFF);
			}
			else if (bits == 32) {
				int* s = (int*)frame;
				s[c] = (int)(s[c] * g);
			}
			else {
				float v = (frame[c] - 128) * g;
				frame[c] = (BYTE)(v + 128.f);
			}
		}
		f.Seek(pos, CFile::begin);
		f.Write(frame, bpf);
	}
	f.Close();
	return TRUE;
}


class CTcHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_TC_HELP };
	explicit CTcHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CTcHelpDlg* g_tcHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CTcHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CTcHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"音声書き出し操作ガイド", L"Audio Export Guide", L"Guide d'export audio", L"Guida esportazione audio",
		L"Guía de exportación de audio", L"오디오 내보내기 가이드", L"音频导出指南", L"دليل تصدير الصوت",
		L"Руководство экспорта аудио", L"Audio-Export-Anleitung", L"Guia de exportação de áudio", L"Audio-exportgids",
		L"Przewodnik eksportu audio", L"Ses dışa aktarma kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CTcHelpDlg::OnOK() { DestroyWindow(); }
void CTcHelpDlg::OnCancel() { DestroyWindow(); }
void CTcHelpDlg::OnClose() { DestroyWindow(); }

void CTcHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_tcHelpDlg == this)
		g_tcHelpDlg = nullptr;
	delete this;
}

BOOL CTcHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CTcHelpDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int footerH = 26;
	rc.bottom -= footerH;
	dc.FillSolidRect(CRect(0, 0, rc.right, rc.bottom + footerH), RGB(248, 248, 252));
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 1;
	CBrush frameBrush(RGB(130, 130, 150));

	auto title = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(x, y, t);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(65, 65, 80));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 115));
		dc.TextOut(x, y, t);
	};

	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"音声書き出し操作ガイド", L"Audio Export — Guide", L"Guide export audio", L"Guida esportazione",
		L"Guía exportación", L"오디오 내보내기 가이드", L"音频导出指南", L"دليل تصدير الصوت",
		L"Руководство экспорта", L"Audio-Export-Guide", L"Guia exportação", L"Audio-exportgids",
		L"Przewodnik eksportu", L"Ses dışa aktarma"));
	y += titleLh;
	muted(L, y, LL14(
		L"WAV / mp3 / FLAC へ書き出します。形式・ビットレート・ループ・フェード／クロスフェードを指定します。",
		L"Export to WAV / mp3 / FLAC. Set format, bitrate, loops, fade / crossfade.",
		L"Exporter en WAV / mp3 / FLAC. Format, débit, boucles, fondu / crossfade.",
		L"Esporta in WAV / mp3 / FLAC. Formato, bitrate, loop, fade / crossfade.",
		L"Exportar a WAV / mp3 / FLAC. Formato, bitrate, bucles, fundido / xfade.",
		L"WAV / mp3 / FLAC로 내보냅니다. 형식·비트레이트·루프·페이드/크로스페이드를 지정합니다.",
		L"导出为 WAV / mp3 / FLAC。设置格式、比特率、循环、淡出/交叉淡入。",
		L"صدّر إلى WAV / mp3 / FLAC. عيّن الشكل والمعدل والحلقة والتلاشي.",
		L"Экспорт в WAV / mp3 / FLAC. Формат, битрейт, циклы, fade / xfade.",
		L"Export nach WAV / mp3 / FLAC. Format, Bitrate, Schleifen, Fade / XFade.",
		L"Exportar para WAV / mp3 / FLAC. Formato, bitrate, loops, fade / xfade.",
		L"Exporteren naar WAV / mp3 / FLAC. Formaat, bitrate, loops, fade / xfade.",
		L"Eksport do WAV / mp3 / FLAC. Format, bitrate, pętle, fade / xfade.",
		L"WAV / mp3 / FLAC'e aktarın. Biçim, bitrate, döngü, solma / xfade."));
	y += lh + 4;

	title(L, y, LL14(L"形式 / 品質", L"Format / Quality", L"Format / Qualité", L"Formato / Qualità",
		L"Formato / Calidad", L"형식 / 품질", L"格式 / 质量", L"التنسيق / الجودة",
		L"Формат / Качество", L"Format / Qualität", L"Formato / Qualidade", L"Formaat / Kwaliteit",
		L"Format / Jakość", L"Biçim / Kalite"));
	y += titleLh;
	body(L, y, LL14(L"・タブ …… WAV / mp3 / FLAC。品質コンボはビットレートまたは圧縮レベル", L"· Tabs …… WAV / mp3 / FLAC. Quality combo = bitrate or compression", L"· Onglets …… WAV / mp3 / FLAC. Qualité = débit ou compression", L"· Schede …… WAV / mp3 / FLAC. Qualità = bitrate o compressione",
		L"· Pestañas …… WAV / mp3 / FLAC. Calidad = bitrate o compresión", L"· 탭 …… WAV/mp3/FLAC. 품질=비트레이트 또는 압축", L"· 选项卡 …… WAV/mp3/FLAC。质量=比特率或压缩", L"· تبويبات …… WAV/mp3/FLAC. الجودة=معدل أو ضغط",
		L"· Вкладки …… WAV/mp3/FLAC. Качество = битрейт или сжатие", L"· Register …… WAV/mp3/FLAC. Qualitaet = Bitrate oder Kompression", L"· Separadores …… WAV/mp3/FLAC. Qualidade = bitrate ou compressão", L"· Tabbladen …… WAV/mp3/FLAC. Kwaliteit = bitrate of compressie",
		L"· Karty …… WAV/mp3/FLAC. Jakość = bitrate lub kompresja", L"· Sekmeler …… WAV/mp3/FLAC. Kalite = bitrate veya sıkıştırma")); y += lh;
	body(L, y, LL14(L"・サンプリング …… 出力サンプルレート。未対応時は自動で下げます", L"· Sample rate …… output rate; auto-lowers if unsupported", L"· Échantillonnage …… débit; baisse auto si non supporté", L"· Campionamento …… frequenza; abbassa se non supportata",
		L"· Muestreo …… tasa; baja automáticamente si no es compatible", L"· 샘플링 …… 출력 레이트. 미지원 시 자동 하향", L"· 采样 …… 输出采样率；不支持时自动降低", L"· العينات …… معدل الخرج؛ يُخفض تلقائياً إن لم يُدعم",
		L"· Частота …… выходная; автопонижение при неподдержке", L"· Abtastrate …… Ausgabe; auto-senken wenn nicht unterstuetzt", L"· Amostragem …… taxa; baixa automaticamente se não suportada", L"· Sample …… uitvoer; automatisch lager indien niet ondersteund",
		L"· Próbkowanie …… wyjście; auto-obniżenie gdy brak wsparcia", L"· Örnekleme …… çıkış hızı; desteklenmezse otomatik düşer")); y += lh + 4;

	title(L, y, LL14(L"ループ / フェード", L"Loop / Fade", L"Boucle / Fondu", L"Loop / Fade", L"Bucle / Fundido", L"루프 / 페이드", L"循环 / 淡出", L"حلقة / تلاشي",
		L"Цикл / Затухание", L"Schleife / Fade", L"Loop / Fade", L"Loop / Fade", L"Pętla / Fade", L"Döngü / Solma"));
	y += titleLh;
	body(L, y, LL14(L"・繰返し …… ループ再生して書き出す回数", L"· Loop count …… how many times to loop while exporting", L"· Boucles …… répétitions à l'export", L"· Loop …… ripetizioni in export",
		L"· Repeticiones …… veces al exportar", L"· 반복 …… 내보내기 시 루프 횟수", L"· 循环 …… 导出时循环次数", L"· التكرار …… مرات الحلقة عند التصدير",
		L"· Повторы …… сколько раз зациклить при экспорте", L"· Schleifen …… Wiederholungen beim Export", L"· Repetições …… vezes no export", L"· Herhalingen …… loops bij export",
		L"· Powtórzenia …… ile razy zapętlić przy eksporcie", L"· Döngü …… dışa aktarırken tekrar sayısı")); y += lh;
	body(L, y, LL14(L"・フェードアウト …… 末尾を指定秒でフェード", L"· Fade out …… fade the end over N seconds", L"· Fondu …… fondre la fin sur N sec", L"· Dissolvenza …… fade finale in N sec",
		L"· Fundido …… fundir el final en N seg", L"· 페이드 아웃 …… 끝을 N초 페이드", L"· 淡出 …… 末尾用 N 秒淡出", L"· تلاشي …… تلاشي النهاية خلال N ث",
		L"· Затухание …… затухание конца за N сек", L"· Ausblenden …… Ende ueber N Sek.", L"· Fade out …… esmaecer o fim em N seg", L"· Fade-out …… einde over N sec",
		L"· Wyciszanie …… wycisz koniec przez N sek", L"· Solma …… sonu N sn sol")); y += lh;
	body(L, y, LL14(L"・クロスフェード …… 曲を連結／ミックス枠へ等パワー投入", L"· Crossfade …… join tracks or equal-power refill in mix", L"· Crossfade …… enchaîner ou refill equal-power", L"· Crossfade …… unisci o refill equal-power",
		L"· Crossfade …… unir o rellenar equal-power", L"· 크로스페이드 …… 연결 또는 믹스 등파워 보충", L"· 交叉淡入 …… 连接或混音等功率补充", L"· تلاشي متقاطع …… وصل أو إعادة تعبئة",
		L"· Кроссфейд …… склейка или equal-power в миксе", L"· Crossfade …… verbinden oder Equal-Power-Auffuellen", L"· Crossfade …… juntar ou refill equal-power", L"· Crossfade …… aaneenschakelen of equal-power bijvullen",
		L"· Crossfade …… łączenie lub uzupełnianie equal-power", L"· Crossfade …… birleştir veya equal-power doldur")); y += lh + 4;

	const int gx = L, gy = y, gw = min(340, rc.Width() - L * 2), gh = lh * 2 + 12;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	dc.FillSolidRect(gx + 4, gy + 6, 40, gh - 12, RGB(70, 140, 90));
	dc.FillSolidRect(gx + 52, gy + 6, 40, gh - 12, RGB(180, 140, 60));
	dc.FillSolidRect(gx + 100, gy + 6, 48, gh - 12, RGB(70, 110, 160));
	dc.FillSolidRect(gx + 156, gy + 6, 50, gh - 12, RGB(150, 70, 70));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + 10, gy + 8, L"WAV");
	dc.TextOut(gx + 58, gy + 8, L"mp3");
	dc.TextOut(gx + 106, gy + 8, L"FLAC");
	dc.TextOut(gx + 164, gy + 8, L"Fade");
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
	y = gy + gh + 6;

	title(L, y, LL14(L"パス / タグ", L"Path / Tags", L"Chemin / Tags", L"Percorso / Tag", L"Ruta / Etiquetas", L"경로 / 태그", L"路径 / 标签", L"المسار / الوسوم",
		L"Путь / Теги", L"Pfad / Tags", L"Caminho / Tags", L"Pad / Tags", L"Ścieżka / Tagi", L"Yol / Etiketler"));
	y += titleLh;
	body(L, y, LL14(L"・出力パス …… 単曲はファイル、複数／ミックス時はフォルダ", L"· Path …… file for one track; folder for multi / mix", L"· Chemin …… fichier (1) ou dossier (plusieurs/mix)", L"· Percorso …… file (1) o cartella (più/mix)",
		L"· Ruta …… archivo (1) o carpeta (varios/mix)", L"· 출력 경로 …… 단곡=파일, 다중/믹스=폴더", L"· 输出路径 …… 单曲为文件，多选/混音为文件夹", L"· المسار …… ملف لواحدة أو مجلد لعدة/مزج",
		L"· Путь …… файл (1) или папка (несколько/микс)", L"· Pfad …… Datei (1) oder Ordner (mehr/Mix)", L"· Caminho …… arquivo (1) ou pasta (vários/mix)", L"· Pad …… bestand (1) of map (meer/mix)",
		L"· Ścieżka …… plik (1) lub folder (wiele/mix)", L"· Yol …… tek=dosya, çoklu/mix=klasör")); y += lh;
	body(L, y, LL14(L"・タグ／ジャケット …… コピーまたは手入力。プロンプト適用も可", L"· Tags / cover …… copy or edit; optional prompt apply", L"· Tags / pochette …… copier ou éditer; prompt optionnel", L"· Tag / copertina …… copia o modifica; prompt opzionale",
		L"· Etiquetas / portada …… copiar o editar; prompt opcional", L"· 태그/재킷 …… 복사 또는 입력. 프롬프트 적용 가능", L"· 标签/封面 …… 复制或手填；可应用提示", L"· الوسوم/الغلاف …… نسخ أو تحرير؛ برومبت اختياري",
		L"· Теги / обложка …… копировать или править; опц. промпт", L"· Tags / Cover …… kopieren oder editieren; Prompt optional", L"· Tags / capa …… copiar ou editar; prompt opcional", L"· Tags / cover …… kopiëren of bewerken; prompt optioneel",
		L"· Tagi / okładka …… kopiuj lub edytuj; opcjonalny prompt", L"· Etiket / kapak …… kopyala veya düzenle; isteğe bağlı prompt")); y += lh;
	muted(L, y, LL14(
		L"実行でデコードと書き出しを開始。進捗バーで進行を確認できます。",
		L"Execute starts decode/write. Watch the progress bar.",
		L"Exécuter démarre. Suivre la barre de progression.",
		L"Esegui avvia. Controlla la barra di avanzamento.",
		L"Ejecutar inicia. Mire la barra de progreso.",
		L"실행으로 디코드·쓰기를 시작합니다. 진행 바로 확인.",
		L"执行开始解码与写出。用进度条查看进度。",
		L"التنفيذ يبدأ. راقب شريط التقدم.",
		L"Выполнить запускает. Смотрите прогресс.",
		L"Ausfuehren startet. Fortschrittsbalken beobachten.",
		L"Executar inicia. Veja a barra de progresso.",
		L"Uitvoeren start. Volg de voortgangsbalk.",
		L"Wykonaj startuje. Pilnuj paska postępu.",
		L"Çalıştır decode/yazmayı başlatır. İlerleme çubuğuna bakın."));

	dc.SelectObject(oldFont);
}

} // namespace

BOOL EncodeWavToFlac(const CString& wavPath, const CString& outPath, int compressionLevel){
	if (wavPath.IsEmpty() || outPath.IsEmpty()) return FALSE;
	if (compressionLevel < 0) compressionLevel = 0;
	if (compressionLevel > 8) compressionLevel = 8;

	CFile f;
	// 直後に閉じた一時WAVでも開けるよう shareDenyNone
	if (!f.Open(wavPath, CFile::modeRead | CFile::shareDenyNone))
		return FALSE;
	TcWavInfo info = {};
	if (!TcReadWavInfo(f, info) || info.ch < 1 || info.ch > 8)
		return FALSE;
	// FLAC が受けないビット深度を正規化
	int bitsOut = (int)info.bits;
	if (bitsOut > 24) bitsOut = 24;
	if (bitsOut != 8 && bitsOut != 16 && bitsOut != 24)
		bitsOut = 16;
	if (info.hz < 1 || info.hz > 655350)
		return FALSE;

	MpDecodeProgressReport(80, LL14(
		L"FLACエンコード準備…", L"Preparing FLAC...", L"Prep. FLAC...", L"Prep. FLAC...", L"Prep. FLAC...",
		L"FLAC 준비…", L"准备FLAC…", L"Preparing FLAC...", L"Подготовка FLAC...", L"FLAC vorbereiten...",
		L"Preparando FLAC...", L"FLAC voorbereiden...", L"Przygotowanie FLAC...", L"FLAC hazirlaniyor..."));

	::DeleteFile(outPath);
	FILE* fp = NULL;
	if (_wfopen_s(&fp, outPath, L"w+b") != 0 || !fp)
		return FALSE;

	FLAC__StreamEncoder* enc = FLAC__stream_encoder_new();
	if (!enc) {
		fclose(fp);
		return FALSE;
	}
	FLAC__stream_encoder_set_verify(enc, false);
	// 非サブセットのサンプレートでも通す（アップサンプル書き出し対策）
	FLAC__stream_encoder_set_streamable_subset(enc, false);
	FLAC__stream_encoder_set_compression_level(enc, (unsigned)compressionLevel);
	FLAC__stream_encoder_set_channels(enc, info.ch);
	FLAC__stream_encoder_set_bits_per_sample(enc, (unsigned)bitsOut);
	FLAC__stream_encoder_set_sample_rate(enc, info.hz);
	const __int64 totalFrames = info.dataBytes / info.blockAlign;
	if (totalFrames > 0)
		FLAC__stream_encoder_set_total_samples_estimate(enc, (FLAC__uint64)totalFrames);

	if (FLAC__stream_encoder_init_FILE(enc, fp, NULL, NULL) != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
		FLAC__stream_encoder_delete(enc);
		fclose(fp); // init 失敗時はこちらが FILE を所有
		DeleteFile(outPath);
		return FALSE;
	}
	// init 成功後は finish/delete が FILE を閉じる。二重 fclose 禁止（ここで止まって見えた既知症状）
	fp = NULL;

	const int chunkFrames = 4096;
	std::vector<BYTE> raw((size_t)info.blockAlign * chunkFrames);
	std::vector<FLAC__int32> pcm((size_t)info.ch * chunkFrames);
	BOOL ok = TRUE;
	f.Seek(info.dataOffset, CFile::begin);
	__int64 remain = info.dataBytes;
	const __int64 total = info.dataBytes > 0 ? info.dataBytes : 1;
	__int64 done = 0;
	int lastEncPct = -1;
	while (remain >= info.blockAlign && ok) {
		int frames = chunkFrames;
		if ((__int64)frames * info.blockAlign > remain)
			frames = (int)(remain / info.blockAlign);
		const UINT want = (UINT)(frames * info.blockAlign);
		if (f.Read(raw.data(), want) != want) {
			ok = FALSE;
			break;
		}
		for (int i = 0; i < frames; ++i) {
			FLAC__int32 tmp[8];
			TcPcmFrameToInt32(raw.data() + i * info.blockAlign, info.ch, info.bits, tmp);
			if (bitsOut == 24 && info.bits == 32) {
				for (int c = 0; c < info.ch; ++c)
					tmp[c] >>= 8;
			}
			else if (bitsOut == 16 && info.bits == 24) {
				for (int c = 0; c < info.ch; ++c)
					tmp[c] >>= 8;
			}
			else if (bitsOut == 16 && info.bits == 32) {
				for (int c = 0; c < info.ch; ++c)
					tmp[c] >>= 16;
			}
			for (int c = 0; c < info.ch; ++c)
				pcm[(size_t)i * info.ch + c] = tmp[c];
		}
		if (!FLAC__stream_encoder_process_interleaved(enc, pcm.data(), (unsigned)frames)) {
			ok = FALSE;
			break;
		}
		remain -= want;
		done += want;
		// 中間WAV(〜78%)の続きとして 80〜99% を埋める。1%ごと＋最低でも一定バイトごとにUI更新
		const int encPct = 80 + (int)((done * 19) / total);
		const int showPct = encPct > 99 ? 99 : encPct;
		if (showPct != lastEncPct) {
			lastEncPct = showPct;
			MpDecodeProgressReport(showPct, LL14(
				L"FLACエンコード中…", L"Encoding FLAC...", L"Encodage FLAC...", L"Codifica FLAC...", L"Codificando FLAC...",
				L"FLAC 인코딩…", L"FLAC编码中…", L"Encoding FLAC...", L"Кодирование FLAC...", L"FLAC kodieren...",
				L"Codificando FLAC...", L"FLAC coderen...", L"Kodowanie FLAC...", L"FLAC kodlaniyor..."));
		}
	}
	if (!FLAC__stream_encoder_finish(enc))
		ok = FALSE;
	FLAC__stream_encoder_delete(enc);
	// fp は encoder 側で close 済み
	if (!ok)
		DeleteFile(outPath);
	return ok;
}

BOOL EncodeWavToMp3(const CString& wavPath, const CString& outPath, int bitrateKbps)
{
	if (wavPath.IsEmpty() || outPath.IsEmpty()) return FALSE;
	if (bitrateKbps < 64) bitrateKbps = 64;
	if (bitrateKbps > 320) bitrateKbps = 320;

	CString srcPath = wavPath;
	CString tempResampled;
	{
		CFile probe;
		if (!probe.Open(wavPath, CFile::modeRead | CFile::shareDenyNone))
			return FALSE;
		TcWavInfo info = {};
		if (!TcReadWavInfo(probe, info) || info.ch < 1 || info.ch > 2)
			return FALSE;
		probe.Close();

		DWORD encHz = info.hz;
		static const DWORD kRates[] = { 16000, 22050, 24000, 32000, 44100, 48000 };
		BOOL okRate = FALSE;
		for (int i = 0; i < 6; ++i) {
			if (encHz == kRates[i]) { okRate = TRUE; break; }
		}
		if (!okRate) {
			if (encHz > 44100) encHz = 48000;
			else if (encHz > 32000) encHz = 44100;
			else if (encHz > 24000) encHz = 32000;
			else if (encHz > 22050) encHz = 24000;
			else if (encHz > 16000) encHz = 22050;
			else encHz = 16000;
			tempResampled = TcMakeTempWavPath();
			if (!TcResampleWavToRate(wavPath, tempResampled, encHz)) {
				DeleteFile(tempResampled);
				return FALSE;
			}
			srcPath = tempResampled;
		}
	}

	CFile f;
	if (!f.Open(srcPath, CFile::modeRead | CFile::shareDenyNone)) {
		if (!tempResampled.IsEmpty()) DeleteFile(tempResampled);
		return FALSE;
	}
	TcWavInfo info = {};
	if (!TcReadWavInfo(f, info) || info.ch < 1 || info.ch > 2) {
		if (!tempResampled.IsEmpty()) DeleteFile(tempResampled);
		return FALSE;
	}

	::DeleteFile(outPath);

	HRESULT hr = MFStartup(MF_VERSION);
	if (FAILED(hr)) {
		if (!tempResampled.IsEmpty()) DeleteFile(tempResampled);
		return FALSE;
	}

	IMFSinkWriter* writer = NULL;
	IMFMediaType* outType = NULL;
	IMFMediaType* inType = NULL;
	DWORD streamIndex = 0;
	BOOL ok = FALSE;

	hr = MFCreateSinkWriterFromURL(outPath, NULL, NULL, &writer);
	if (FAILED(hr))
		goto done;

	hr = MFCreateMediaType(&outType);
	if (FAILED(hr))
		goto done;
	hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (FAILED(hr)) goto done;
	hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
	if (FAILED(hr)) goto done;
	hr = outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32)(bitrateKbps * 1000 / 8));
	if (FAILED(hr)) goto done;
	hr = outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, info.ch);
	if (FAILED(hr)) goto done;
	hr = outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, info.hz);
	if (FAILED(hr)) goto done;

	hr = writer->AddStream(outType, &streamIndex);
	if (FAILED(hr)) goto done;

	hr = MFCreateMediaType(&inType);
	if (FAILED(hr)) goto done;
	hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	if (FAILED(hr)) goto done;
	hr = inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	if (FAILED(hr)) goto done;
	hr = inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, info.ch);
	if (FAILED(hr)) goto done;
	hr = inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, info.hz);
	if (FAILED(hr)) goto done;
	hr = inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
	if (FAILED(hr)) goto done;
	hr = inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, info.ch * 2);
	if (FAILED(hr)) goto done;
	hr = inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, info.hz * info.ch * 2);
	if (FAILED(hr)) goto done;
	hr = inType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	if (FAILED(hr)) goto done;

	hr = writer->SetInputMediaType(streamIndex, inType, NULL);
	if (FAILED(hr)) goto done;
	hr = writer->BeginWriting();
	if (FAILED(hr)) goto done;

	{
		const int chunkFrames = 4096;
		std::vector<BYTE> raw((size_t)info.blockAlign * chunkFrames);
		std::vector<short> pcm16((size_t)info.ch * chunkFrames);
		f.Seek(info.dataOffset, CFile::begin);
		__int64 remain = info.dataBytes;
		const __int64 total = info.dataBytes > 0 ? info.dataBytes : 1;
		__int64 done = 0;
		int lastEncPct = -1;
		LONGLONG rt = 0;
		const LONGLONG hnsPerSec = 10000000LL;
		ok = TRUE;
		while (remain >= info.blockAlign) {
			int frames = chunkFrames;
			if ((__int64)frames * info.blockAlign > remain)
				frames = (int)(remain / info.blockAlign);
			const UINT want = (UINT)(frames * info.blockAlign);
			if (f.Read(raw.data(), want) != want) {
				ok = FALSE;
				break;
			}
			for (int i = 0; i < frames; ++i)
				TcPcmFrameToInt16(raw.data() + i * info.blockAlign, info.ch, info.bits, pcm16.data() + i * info.ch);

			const DWORD cb = (DWORD)(frames * info.ch * 2);
			IMFSample* sample = NULL;
			IMFMediaBuffer* buffer = NULL;
			hr = MFCreateSample(&sample);
			if (FAILED(hr)) { ok = FALSE; break; }
			hr = MFCreateMemoryBuffer(cb, &buffer);
			if (FAILED(hr)) {
				sample->Release();
				ok = FALSE;
				break;
			}
			BYTE* pData = NULL;
			hr = buffer->Lock(&pData, NULL, NULL);
			if (SUCCEEDED(hr)) {
				memcpy(pData, pcm16.data(), cb);
				buffer->Unlock();
				buffer->SetCurrentLength(cb);
				sample->AddBuffer(buffer);
				sample->SetSampleTime(rt);
				sample->SetSampleDuration((hnsPerSec * frames) / info.hz);
				hr = writer->WriteSample(streamIndex, sample);
				if (FAILED(hr))
					ok = FALSE;
			}
			else {
				ok = FALSE;
			}
			buffer->Release();
			sample->Release();
			if (!ok) break;
			rt += (hnsPerSec * frames) / info.hz;
			remain -= want;
			done += want;
			const int encPct = 80 + (int)((done * 19) / total);
			const int showPct = encPct > 99 ? 99 : encPct;
			if (showPct != lastEncPct) {
				lastEncPct = showPct;
				MpDecodeProgressReport(showPct, LL14(
					L"MP3エンコード中…", L"Encoding MP3...", L"Encodage MP3...", L"Codifica MP3...", L"Codificando MP3...",
					L"MP3 인코딩…", L"MP3编码中…", L"Encoding MP3...", L"Кодирование MP3...", L"MP3 kodieren...",
					L"Codificando MP3...", L"MP3 coderen...", L"Kodowanie MP3...", L"MP3 kodlaniyor..."));
			}
		}
		if (ok) {
			hr = writer->Finalize();
			if (FAILED(hr))
				ok = FALSE;
		}
	}

done:
	if (inType) inType->Release();
	if (outType) outType->Release();
	if (writer) writer->Release();
	MFShutdown();
	f.Close();
	if (!tempResampled.IsEmpty())
		DeleteFile(tempResampled);
	if (!ok)
		DeleteFile(outPath);
	return ok;
}

IMPLEMENT_DYNAMIC(CTranscodeExport, CCustomBlurDialogBase)

CTranscodeExport::CTranscodeExport(CWnd* pParent)
	: CCustomBlurDialogBase(CTranscodeExport::IDD, pParent)
	, multiFile(false)
	, m_initialTab(-1)
	, m_preferXfade(false)
	, m_coverBmp(NULL)
	, m_mixPctCount(0)
	, m_mixEditRow(-1)
{
	memset(m_mixPct, 0, sizeof(m_mixPct));
}

CTranscodeExport::~CTranscodeExport()
{
	if (m_coverBmp) {
		::DeleteObject(m_coverBmp);
		m_coverBmp = NULL;
	}
}


void CTranscodeExport::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TC_HELP, m_help);
	DDX_Control(pDX, IDC_TC_FORMAT, m_format);	DDX_Control(pDX, IDC_TC_FORMAT_L, m_formatLabel);
	DDX_Control(pDX, IDC_TC_QUALITY, m_quality);
	DDX_Control(pDX, IDC_TC_QUALITY_L, m_qualityLabel);
	DDX_Control(pDX, IDC_TC_SRATE, m_srate);
	DDX_Control(pDX, IDC_TC_SRATE_L, m_srateLabel);
	DDX_Control(pDX, IDC_TC_LOOP, m_loop);
	DDX_Control(pDX, IDC_TC_KPI_SEC, m_kpiSec);
	DDX_Control(pDX, IDC_TC_KPI_SEC_L, m_kpiSecLabel);
	DDX_Control(pDX, IDC_TC_PATH, m_path);
	DDX_Control(pDX, IDC_TC_STATUS, m_status);
	DDX_Control(pDX, IDC_TC_LOOP_L, m_loopLabel);
	DDX_Control(pDX, IDC_TC_PATH_L, m_pathLabel);
	DDX_Control(pDX, IDC_TC_BROWSE, m_browse);
	DDX_Control(pDX, IDC_TC_EXEC, m_exec);
	DDX_Control(pDX, IDC_TC_CLOSE, m_close);
	DDX_Control(pDX, IDC_TC_FADE, m_fadeCheck);
	DDX_Control(pDX, IDC_TC_FADE_SEC, m_fadeSec);
	DDX_Control(pDX, IDC_TC_FADE_L, m_fadeLabel);
	DDX_Control(pDX, IDC_TC_XFADE, m_xfadeCheck);
	DDX_Control(pDX, IDC_TC_XFADE_SEC, m_xfadeSec);
	DDX_Control(pDX, IDC_TC_XFADE_L, m_xfadeLabel);
	DDX_Control(pDX, IDC_TC_TRIM, m_trimCheck);
	DDX_Control(pDX, IDC_TC_TRIM_SEC, m_trimSec);
	DDX_Control(pDX, IDC_TC_TRIM_L, m_trimLabel);
	DDX_Control(pDX, IDC_TC_COPY_TAGS, m_copyTags);
	DDX_Control(pDX, IDC_TC_PROMPT, m_promptCheck);
	DDX_Control(pDX, IDC_TC_MIX, m_mixCheck);
	DDX_Control(pDX, IDC_TC_MIX_N_L, m_mixNLabel);
	DDX_Control(pDX, IDC_TC_MIX_N, m_mixN);
	DDX_Control(pDX, IDC_TC_MIX_VOL, m_mixVol);
	DDX_Control(pDX, IDC_TC_TITLE_L, m_titleL);
	DDX_Control(pDX, IDC_TC_TITLE, m_title);
	DDX_Control(pDX, IDC_TC_ARTIST_L, m_artistL);
	DDX_Control(pDX, IDC_TC_ARTIST, m_artist);
	DDX_Control(pDX, IDC_TC_ALBUM_L, m_albumL);
	DDX_Control(pDX, IDC_TC_ALBUM, m_album);
	DDX_Control(pDX, IDC_TC_COVER_L, m_coverL);
	DDX_Control(pDX, IDC_TC_COVER_PIC, m_coverPic);
	DDX_Control(pDX, IDC_TC_COVER, m_cover);
	DDX_Control(pDX, IDC_TC_COVER_CLEAR, m_coverClear);
}


BEGIN_MESSAGE_MAP(CTranscodeExport, CCustomBlurDialogBase)
	ON_WM_SHOWWINDOW()
	ON_WM_LBUTTONDOWN()
	ON_MESSAGE(WM_TC_LAYOUT_TABS, &CTranscodeExport::OnLayoutTabsMsg)
	ON_BN_CLICKED(IDC_TC_EXEC, &CTranscodeExport::OnBnClickedExec)
	ON_BN_CLICKED(IDC_TC_BROWSE, &CTranscodeExport::OnBnClickedBrowse)
	ON_BN_CLICKED(IDC_TC_CLOSE, &CTranscodeExport::OnBnClickedClose)
	ON_BN_CLICKED(IDC_TC_HELP, &CTranscodeExport::OnBnClickedHelp)
	ON_BN_CLICKED(IDC_TC_COVER_CLEAR, &CTranscodeExport::OnBnClickedCoverClear)
	ON_BN_CLICKED(IDC_TC_XFADE, &CTranscodeExport::OnBnClickedXfade)
	ON_BN_CLICKED(IDC_TC_MIX, &CTranscodeExport::OnBnClickedMix)
	ON_CBN_SELCHANGE(IDC_TC_FORMAT, &CTranscodeExport::OnCbnSelchangeFormat)
	ON_CBN_SELCHANGE(IDC_TC_MIX_N, &CTranscodeExport::OnCbnSelchangeMixN)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TC_TABS, &CTranscodeExport::OnTcnSelchangeTabs)
	ON_WM_DROPFILES()
	ON_WM_SIZE()
	ON_WM_DESTROY()
END_MESSAGE_MAP()
void CTranscodeExport::LayoutTabsBelowCaption()
{
	if (!m_tabs.GetSafeHwnd())
		return;
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH <= 0)
		return;

	CRect rcClient;
	GetClientRect(&rcClient);
	CRect rTabs;
	m_tabs.GetWindowRect(&rTabs);
	ScreenToClient(&rTabs);
	const int wantTop = capH + 6;
	const int stripH = (rTabs.Height() > 8) ? rTabs.Height() : 32;
	const int wantLeft = 7;
	const int wantW = max(200, rcClient.right - 14);
	const int oldTop = rTabs.top;
	const int dy = wantTop - oldTop;

	// 毎回キャプション直下へ強制配置（早期returnで取り残さない）
	m_tabs.SetWindowPos(&CWnd::wndBottom, wantLeft, wantTop, wantW, stripH,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);

	// 下方向に空けるときだけ他コントロールを追従（上方向へ引き上げてキャプションに食い込ませない）
	if (dy > 0) {
		HWND hChild = ::GetWindow(m_hWnd, GW_CHILD);
		while (hChild) {
			const UINT id = (UINT)::GetDlgCtrlID(hChild);

			if (hChild != m_tabs.GetSafeHwnd()
				&& id != IDC_CAP_CLOSE && id != IDC_CAP_MIN && id != IDC_CAP_MAX
				&& id != IDC_CAP_SETTINGS && id != IDC_CAP_PIN
				&& id != IDC_TC_HELP) {
				RECT r;
				::GetWindowRect(hChild, &r);
				::ScreenToClient(m_hWnd, (LPPOINT)&r.left);
				::ScreenToClient(m_hWnd, (LPPOINT)&r.right);
				if (r.top >= oldTop || (r.top < wantTop + stripH && r.bottom > wantTop))
					::SetWindowPos(hChild, NULL, r.left, r.top + dy, 0, 0,
						SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
			}			hChild = ::GetWindow(hChild, GW_HWNDNEXT);
		}
	}

	// タブ帯と重なる本文を、相対位置を保ったまま帯の下へまとめて押し下げ
	{
		const int clearTop = wantTop + stripH + 4;
		int overlapMinTop = INT_MAX;
		HWND hChild = ::GetWindow(m_hWnd, GW_CHILD);
		while (hChild) {
			const UINT id = (UINT)::GetDlgCtrlID(hChild);

			if (hChild != m_tabs.GetSafeHwnd()
				&& id != IDC_CAP_CLOSE && id != IDC_CAP_MIN && id != IDC_CAP_MAX
				&& id != IDC_CAP_SETTINGS && id != IDC_CAP_PIN
				&& id != IDC_TC_HELP) {
				RECT r;
				::GetWindowRect(hChild, &r);
				::ScreenToClient(m_hWnd, (LPPOINT)&r.left);
				::ScreenToClient(m_hWnd, (LPPOINT)&r.right);
				if (r.top < clearTop && r.bottom > wantTop && r.top >= capH && r.top < overlapMinTop)
					overlapMinTop = r.top;
			}			hChild = ::GetWindow(hChild, GW_HWNDNEXT);
		}
		const int push = (overlapMinTop < INT_MAX) ? (clearTop - overlapMinTop) : 0;
		if (push > 0) {
			hChild = ::GetWindow(m_hWnd, GW_CHILD);
			while (hChild) {
				const UINT id = (UINT)::GetDlgCtrlID(hChild);

				if (hChild != m_tabs.GetSafeHwnd()
					&& id != IDC_CAP_CLOSE && id != IDC_CAP_MIN && id != IDC_CAP_MAX
					&& id != IDC_CAP_SETTINGS && id != IDC_CAP_PIN
					&& id != IDC_TC_HELP) {
					RECT r;
					::GetWindowRect(hChild, &r);
					::ScreenToClient(m_hWnd, (LPPOINT)&r.left);
					::ScreenToClient(m_hWnd, (LPPOINT)&r.right);
					if (r.top >= overlapMinTop)
						::SetWindowPos(hChild, NULL, r.left, r.top + push, 0, 0,
							SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
				}				hChild = ::GetWindow(hChild, GW_HWNDNEXT);
			}
		}
	}


	// キャプションボタンを最前面に
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
}
LRESULT CTranscodeExport::OnLayoutTabsMsg(WPARAM, LPARAM)
{
	LayoutTabsBelowCaption();
	return 0;
}

void CTranscodeExport::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	if (bShow) {
		LayoutTabsBelowCaption();
		// キャプション導入直後の再レイアウト（初回だけ取りこぼす環境向け）
		PostMessage(WM_TC_LAYOUT_TABS, 0, 0);
	}
}

void CTranscodeExport::OnLButtonDown(UINT nFlags, CPoint point)
{
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CWnd* pHit = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
		// キャプション帯に食い込んだタブはドラッグ扱いにし、誤反応を防ぐ
		if (pHit == &m_tabs || pHit == this || !pHit) {
			SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
			return;
		}
		const UINT id = pHit ? (UINT)pHit->GetDlgCtrlID() : 0;
		if (id == IDC_CAP_CLOSE || id == IDC_CAP_MIN || id == IDC_CAP_MAX
			|| id == IDC_CAP_SETTINGS || id == IDC_CAP_PIN) {
			CCustomBlurDialogBase::OnLButtonDown(nFlags, point);
			return;
		}
		SendMessage(WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(point.x, point.y));
		return;
	}
	CCustomBlurDialogBase::OnLButtonDown(nFlags, point);
}

bool CTranscodeExport::IsXfadeMode()
{
	return multiFile && m_xfadeCheck.GetSafeHwnd() && m_xfadeCheck.GetCheck() == BST_CHECKED && pcs.size() >= 2;
}

bool CTranscodeExport::IsMixMode()
{
	return multiFile && m_mixCheck.GetSafeHwnd() && m_mixCheck.GetCheck() == BST_CHECKED && pcs.size() >= 2;
}

void CTranscodeExport::NormalizeMixPercents()
{
	int n = m_mixPctCount;
	if (n < 1) return;
	if (n > 64) n = 64;
	int sum = 0;
	for (int i = 0; i < n; ++i) {
		if (m_mixPct[i] < 0) m_mixPct[i] = 0;
		if (m_mixPct[i] > 1000) m_mixPct[i] = 1000;
		sum += m_mixPct[i];
	}
	if (sum <= 0) {
		const int each = 100 / n;
		int rem = 100 - each * n;
		for (int i = 0; i < n; ++i) {
			m_mixPct[i] = each + ((i < rem) ? 1 : 0);
		}
		return;
	}
	if (sum == 100) return;
	int acc = 0;
	for (int i = 0; i < n; ++i) {
		int v = (int)(((__int64)m_mixPct[i] * 100 + sum / 2) / sum);
		m_mixPct[i] = v;
		acc += v;
	}
	int diff = 100 - acc;
	for (int i = 0; diff != 0 && i < n; ++i) {
		if (diff > 0) { m_mixPct[i]++; --diff; }
		else if (m_mixPct[i] > 0) { m_mixPct[i]--; ++diff; }
	}
}

void CTranscodeExport::RebuildMixVolList()
{
	if (!m_mixVol.GetSafeHwnd()) return;
	m_mixVol.DeleteAllItems();
	const int n = (int)pcs.size();
	m_mixPctCount = (n > 64) ? 64 : n;
	if (m_mixPctCount < 1) return;
	// 選択数が減ったときの古い％残りを消す
	for (int i = m_mixPctCount; i < 64; ++i)
		m_mixPct[i] = 0;
	bool needInit = true;
	for (int i = 0; i < m_mixPctCount; ++i) {
		if (m_mixPct[i] != 0) { needInit = false; break; }
	}
	if (needInit) {
		const int each = 100 / m_mixPctCount;
		int rem = 100 - each * m_mixPctCount;
		for (int i = 0; i < m_mixPctCount; ++i)
			m_mixPct[i] = each + ((i < rem) ? 1 : 0);
	}
	// 25+25→50+50 など、相対比を保ったまま合計100へ
	NormalizeMixPercents();
	for (int i = 0; i < m_mixPctCount; ++i) {
		CString pct;
		pct.Format(L"%d", m_mixPct[i]);
		const int row = m_mixVol.InsertItem(i, pct);
		CString name = TcBaseNameFromItem(pcs[i]);
		if (name.IsEmpty()) name.Format(L"#%d", i + 1);
		m_mixVol.SetItemText(row, 1, name);
	}
}

void CTranscodeExport::ApplyMixUi()
{
	const BOOL showMix = multiFile && pcs.size() >= 2;
	if (m_mixCheck.GetSafeHwnd())
		m_mixCheck.ShowWindow(showMix ? SW_SHOW : SW_HIDE);
	if (m_mixNLabel.GetSafeHwnd())
		m_mixNLabel.ShowWindow(showMix ? SW_SHOW : SW_HIDE);
	if (m_mixN.GetSafeHwnd())
		m_mixN.ShowWindow(showMix ? SW_SHOW : SW_HIDE);
	if (m_mixVol.GetSafeHwnd())
		m_mixVol.ShowWindow(showMix ? SW_SHOW : SW_HIDE);
	if (!showMix) {
		if (m_mixCheck.GetSafeHwnd())
			m_mixCheck.SetCheck(BST_UNCHECKED);
		return;
	}
	const bool mixOn = IsMixMode();
	if (m_mixN.GetSafeHwnd()) {
		m_mixN.EnableWindow(mixOn);
		const int n = (int)pcs.size();
		int want = m_mixN.GetCurSel() + 2;
		m_mixN.ResetContent();
		for (int k = 2; k <= n && k <= 64; ++k) {
			CString s;
			s.Format(L"%d", k);
			m_mixN.AddString(s);
		}
		int maxK = n;
		if (maxK > 64) maxK = 64;
		if (want < 2) want = savedata.wav_export_mix_n;
		if (want < 2) want = 2;
		if (want > maxK) want = maxK;
		m_mixN.SetCurSel(want - 2);
	}
	if (m_mixNLabel.GetSafeHwnd())
		m_mixNLabel.EnableWindow(mixOn);
	if (m_mixVol.GetSafeHwnd()) {
		m_mixVol.EnableWindow(mixOn);
		if (mixOn)
			RebuildMixVolList();
	}
}

void CTranscodeExport::ApplyPathModeUi(bool keepPathText)
{
	const bool singleOut = IsMixMode() || IsXfadeMode();
	const bool folderMode = multiFile && !singleOut;
	if (folderMode) {
		m_pathLabel.SetWindowText(LL14(L"出力フォルダ", L"Output folder", L"Dossier de sortie", L"Cartella di output",
			L"Carpeta de salida", L"출력 폴더", L"输出文件夹", L"مجلد الإخراج",
			L"Папка вывода", L"Ausgabeordner", L"Pasta de saída", L"Uitvoermap",
			L"Folder wyjściowy", L"Çıktı klasörü"));
		if (!keepPathText)
			m_path.SetWindowText(TcDefaultFolderFromPc(pc));
	}
	else {
		m_pathLabel.SetWindowText(LL14(L"出力ファイル名", L"Output file", L"Fichier de sortie", L"File di output",
			L"Archivo de salida", L"출력 파일", L"输出文件名", L"اسم الملف",
			L"Выходной файл", L"Ausgabedatei", L"Arquivo de saída", L"Uitvoerbestand",
			L"Plik wyjściowy", L"Çıktı dosyası"));
		if (!keepPathText)
			m_path.SetWindowText(OutputPathForItem(TcDefaultFolderFromPc(pc), pc, CurrentFormat()));
	}
	SyncTitleFieldForPathMode();
}

void CTranscodeExport::SyncTitleFieldForPathMode()
{
	if (!m_title.GetSafeHwnd()) return;
	if (IsMixMode() || IsXfadeMode() || !multiFile) {
		if (!m_title.IsWindowEnabled()) {
			FileTagFields src;
			if (pc.fol[0] != 0)
				ReadFileTagFields(pc.fol, src);
			CString t = src.title;
			if (t.IsEmpty() && pc.name[0]) t = pc.name;
			m_title.SetWindowText(t);
			m_title.EnableWindow(TRUE);
		}
	}
	else {
		m_title.SetWindowText(LL14(L"(複数のため変更不可)", L"(locked for multi)", L"(verrouille)", L"(bloccato)", L"(bloqueado)",
			L"(다중 선택 잠금)", L"(多选不可改)", L"(locked)", L"(заблокировано)", L"(gesperrt)",
			L"(bloqueado)", L"(vergrendeld)", L"(zablokowane)", L"(kilitli)"));
		m_title.EnableWindow(FALSE);
	}
}

void CTranscodeExport::OnBnClickedXfade()
{
	// ミックスと併用可: ミックス時は補充クロスフェードのON/秒、非ミックス時は連結クロスフェード
	ApplyMixUi();
	ApplyPathModeUi(false);
}

void CTranscodeExport::OnBnClickedMix()
{
	// クロスフェードと併用可（同時ONなら同時ミックス＋補充クロスフェード）
	ApplyMixUi();
	ApplyPathModeUi(false);
}

void CTranscodeExport::OnCbnSelchangeMixN()
{
	// 同時曲数を保存。音量リストは選択曲数ぶん（N とは独立）のまま再構築。
	int mixN = 2;
	if (m_mixN.GetSafeHwnd() && m_mixN.GetCount() > 0) {
		mixN = m_mixN.GetCurSel() + 2;
		if (mixN < 2) mixN = 2;
	}
	savedata.wav_export_mix_n = mixN;
	if (IsMixMode())
		RebuildMixVolList();
}

int CTranscodeExport::CurrentFormat() const
{
	if (!m_tabs.GetSafeHwnd()) {
		const int f = m_format.GetCurSel();
		return (f == TC_FMT_FLAC) ? TC_FMT_FLAC : TC_FMT_MP3;
	}
	const int t = m_tabs.GetCurSel();
	if (t == TC_TAB_WAV) return TC_FMT_WAV;
	if (t == TC_TAB_FLAC) return TC_FMT_FLAC;
	return TC_FMT_MP3;
}

void CTranscodeExport::ApplyTabUi()
{
	const int fmt = CurrentFormat();
	const BOOL showQ = (fmt != TC_FMT_WAV);
	if (m_quality.GetSafeHwnd()) {
		m_quality.ShowWindow(showQ ? SW_SHOW : SW_HIDE);
		m_quality.EnableWindow(showQ);
	}
	if (m_qualityLabel.GetSafeHwnd()) {
		m_qualityLabel.ShowWindow(showQ ? SW_SHOW : SW_HIDE);
		m_qualityLabel.EnableWindow(showQ);
	}
	if (fmt == TC_FMT_WAV) {
		if (m_format.GetSafeHwnd()) m_format.SetCurSel(TC_FMT_MP3);
	}
	else if (m_format.GetSafeHwnd())
		m_format.SetCurSel(fmt == TC_FMT_FLAC ? TC_FMT_FLAC : TC_FMT_MP3);
	if (showQ)
		RefreshQualityLabels();
	if (!multiFile || IsXfadeMode() || IsMixMode()) {
		CString path;
		m_path.GetWindowText(path);
		const int dot = path.ReverseFind(L'.');
		if (dot >= 0) path = path.Left(dot);
		m_path.SetWindowText(NormalizeOutPath(path + ExtForFormat(fmt), fmt));
	}
	if (m_tabs.GetSafeHwnd())
		m_tabs.Invalidate(FALSE);
	ApplyKpiDurationUi();
}

BOOL CTranscodeExport::SelectionHasKpi() const
{
	if (multiFile) {
		for (size_t i = 0; i < pcs.size(); ++i) {
			if (pcs[i].sub == -3)
				return TRUE;
		}
		return FALSE;
	}
	return pc.sub == -3;
}

int CTranscodeExport::DefaultKpiDurationSec() const
{
	// プレイリストの time ではなく、ユーザーが保存した秒数を優先する
	int sec = savedata.wav_export_kpi_sec;
	if (sec < 1)
		sec = 240; // 既定4分
	if (sec > 36000)
		sec = 36000;
	return sec;
}

void CTranscodeExport::ApplyKpiDurationUi()
{
	const BOOL show = SelectionHasKpi();
	if (m_kpiSec.GetSafeHwnd())
		m_kpiSec.ShowWindow(show ? SW_SHOW : SW_HIDE);
	if (m_kpiSecLabel.GetSafeHwnd())
		m_kpiSecLabel.ShowWindow(show ? SW_SHOW : SW_HIDE);
}

void CTranscodeExport::PersistKpiDurationFromUi()
{
	if (!m_kpiSec.GetSafeHwnd())
		return;
	// KPI以外でも欄に値があれば保存（次回KPI書き出しの既定になる）
	CString kpiStr;
	m_kpiSec.GetWindowText(kpiStr);
	int kpiSec = _tstoi(kpiStr);
	if (kpiSec < 1)
		kpiSec = DefaultKpiDurationSec();
	if (kpiSec > 36000)
		kpiSec = 36000;
	savedata.wav_export_kpi_sec = kpiSec;
}

CString CTranscodeExport::ExtForFormat(int fmt) const
{
	if (fmt == TC_FMT_WAV) return L".wav";
	return (fmt == TC_FMT_FLAC) ? L".flac" : L".mp3";
}

CString CTranscodeExport::FilterForFormat(int fmt) const
{
	if (fmt == TC_FMT_WAV) {
		return LL14(L"WAVファイル (*.wav)|*.wav|すべてのファイル (*.*)|*.*||",
			L"WAV files (*.wav)|*.wav|All files (*.*)|*.*||",
			L"Fichiers WAV (*.wav)|*.wav|Tous les fichiers (*.*)|*.*||",
			L"File WAV (*.wav)|*.wav|Tutti i file (*.*)|*.*||",
			L"Archivos WAV (*.wav)|*.wav|Todos los archivos (*.*)|*.*||",
			L"WAV 파일 (*.wav)|*.wav|모든 파일 (*.*)|*.*||",
			L"WAV文件 (*.wav)|*.wav|所有文件 (*.*)|*.*||",
			L"ملفات WAV (*.wav)|*.wav|جميع الملفات (*.*)|*.*||",
			L"Файлы WAV (*.wav)|*.wav|Все файлы (*.*)|*.*||",
			L"WAV-Dateien (*.wav)|*.wav|Alle Dateien (*.*)|*.*||",
			L"Arquivos WAV (*.wav)|*.wav|Todos os arquivos (*.*)|*.*||",
			L"WAV-bestanden (*.wav)|*.wav|Alle bestanden (*.*)|*.*||",
			L"Pliki WAV (*.wav)|*.wav|Wszystkie pliki (*.*)|*.*||",
			L"WAV dosyalari (*.wav)|*.wav|Tum dosyalar (*.*)|*.*||");
	}
	if (fmt == TC_FMT_FLAC) {
		return LL14(L"FLACファイル (*.flac)|*.flac|すべてのファイル (*.*)|*.*||",
			L"FLAC files (*.flac)|*.flac|All files (*.*)|*.*||",
			L"Fichiers FLAC (*.flac)|*.flac|Tous les fichiers (*.*)|*.*||",
			L"File FLAC (*.flac)|*.flac|Tutti i file (*.*)|*.*||",
			L"Archivos FLAC (*.flac)|*.flac|Todos los archivos (*.*)|*.*||",
			L"FLAC 파일 (*.flac)|*.flac|모든 파일 (*.*)|*.*||",
			L"FLAC文件 (*.flac)|*.flac|所有文件 (*.*)|*.*||",
			L"ملفات FLAC (*.flac)|*.flac|جميع الملفات (*.*)|*.*||",
			L"Файлы FLAC (*.flac)|*.flac|Все файлы (*.*)|*.*||",
			L"FLAC-Dateien (*.flac)|*.flac|Alle Dateien (*.*)|*.*||",
			L"Arquivos FLAC (*.flac)|*.flac|Todos os arquivos (*.*)|*.*||",
			L"FLAC-bestanden (*.flac)|*.flac|Alle bestanden (*.*)|*.*||",
			L"Pliki FLAC (*.flac)|*.flac|Wszystkie pliki (*.*)|*.*||",
			L"FLAC dosyalari (*.flac)|*.flac|Tum dosyalar (*.*)|*.*||");
	}
	return LL14(L"mp3ファイル (*.mp3)|*.mp3|すべてのファイル (*.*)|*.*||",
		L"mp3 files (*.mp3)|*.mp3|All files (*.*)|*.*||",
		L"Fichiers mp3 (*.mp3)|*.mp3|Tous les fichiers (*.*)|*.*||",
		L"File mp3 (*.mp3)|*.mp3|Tutti i file (*.*)|*.*||",
		L"Archivos mp3 (*.mp3)|*.mp3|Todos los archivos (*.*)|*.*||",
		L"mp3 파일 (*.mp3)|*.mp3|모든 파일 (*.*)|*.*||",
		L"mp3文件 (*.mp3)|*.mp3|所有文件 (*.*)|*.*||",
		L"ملفات mp3 (*.mp3)|*.mp3|جميع الملفات (*.*)|*.*||",
		L"Файлы mp3 (*.mp3)|*.mp3|Все файлы (*.*)|*.*||",
		L"mp3-Dateien (*.mp3)|*.mp3|Alle Dateien (*.*)|*.*||",
		L"Arquivos mp3 (*.mp3)|*.mp3|Todos os arquivos (*.*)|*.*||",
		L"mp3-bestanden (*.mp3)|*.mp3|Alle bestanden (*.*)|*.*||",
		L"Pliki mp3 (*.mp3)|*.mp3|Wszystkie pliki (*.*)|*.*||",
		L"mp3 dosyalari (*.mp3)|*.mp3|Tum dosyalar (*.*)|*.*||");
}

CString CTranscodeExport::NormalizeOutPath(const CString& pathIn, int fmt) const
{
	CString path = pathIn;
	CString ext = ExtForFormat(fmt);
	CString lower = path;
	lower.MakeLower();
	if (lower.GetLength() < ext.GetLength() || lower.Right(ext.GetLength()) != ext)
		path += ext;
	return TcSanitizeFilePath(path);
}

CString CTranscodeExport::OutputPathForItem(const CString& folderIn, const playlistdata0& item, int fmt) const
{
	CString folder = folderIn;
	if (!folder.IsEmpty() && folder[folder.GetLength() - 1] != L'\\')
		folder += L'\\';
	return NormalizeOutPath(folder + TcBaseNameFromItem(item) + ExtForFormat(fmt), fmt);
}

void CTranscodeExport::RefreshQualityLabels()
{
	const int fmt = CurrentFormat();
	if (fmt == TC_FMT_WAV) return;
	m_quality.ResetContent();
	if (fmt == TC_FMT_FLAC) {
		m_qualityLabel.SetWindowText(LL14(L"圧縮レベル", L"Compression", L"Compression", L"Compressione",
			L"Compresion", L"압축 레벨", L"压缩等级", L"مستوى الضغط",
			L"Сжатие", L"Kompression", L"Compressao", L"Compressie",
			L"Kompresja", L"Sikistirma"));
		for (int i = 0; i <= 8; ++i) {
			CString s;
			s.Format(L"%d", i);
			m_quality.AddString(s);
		}
		int lv = savedata.tc_flac_level;
		if (lv < 0 || lv > 8) lv = 5;
		m_quality.SetCurSel(lv);
	}
	else {
		m_qualityLabel.SetWindowText(LL14(L"ビットレート", L"Bitrate", L"Debit", L"Bitrate",
			L"Tasa de bits", L"비트레이트", L"比特率", L"معدل البت",
			L"Битрейт", L"Bitrate", L"Taxa de bits", L"Bitrate",
			L"Bitrate", L"Bit hizi"));
		static const int kbps[] = { 128, 160, 192, 224, 256, 320 };
		int sel = 2;
		for (int i = 0; i < 6; ++i) {
			CString s;
			s.Format(L"%d kbps", kbps[i]);
			m_quality.AddString(s);
			if (savedata.tc_mp3_kbps == kbps[i])
				sel = i;
		}
		m_quality.SetCurSel(sel);
	}
}


BOOL CTranscodeExport::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	SetWindowText(LL14(L"音声書き出し", L"Audio export", L"Export audio", L"Esporta audio",
		L"Exportar audio", L"오디오 내보내기", L"音频导出", L"تصدير الصوت",
		L"Экспорт аудио", L"Audio exportieren", L"Exportar audio", L"Audio exporteren",
		L"Eksport audio", L"Ses disa aktar"));
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();

	// 形式コンボを隠し、上部に横置きタブ帯。品質コンボは繰返し行へ寄せる	m_formatLabel.ShowWindow(SW_HIDE);
	m_format.ShowWindow(SW_HIDE);
	{
		CRect rcClient;
		GetClientRect(&rcClient);
		CRect rLoopL, rLoop, rQL, rQ;
		m_loopLabel.GetWindowRect(&rLoopL); ScreenToClient(&rLoopL);
		m_loop.GetWindowRect(&rLoop); ScreenToClient(&rLoop);
		m_qualityLabel.GetWindowRect(&rQL); ScreenToClient(&rQL);
		m_quality.GetWindowRect(&rQ); ScreenToClient(&rQ);

		int tabRight = rcClient.right - 7;
		if (tabRight < 200) tabRight = 200;
		// 見出し帯だけの高さ。初期Yはキャプション推定下（導入後に LayoutTabsBelowCaption で確定）
		const int estCap = ::GetSystemMetrics(SM_CYCAPTION) + 8;
		CRect rTabs(7, estCap, tabRight, estCap + 32);
		m_tabs.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | TCS_TABS | TCS_SINGLELINE | TCS_FIXEDWIDTH,
			rTabs, this, IDC_TC_TABS);
		m_tabs.SetAeroMode(FALSE);
		TCITEM ti = {};
		ti.mask = TCIF_TEXT;
		CString sWav = L"WAV";
		CString sMp3 = L"mp3";
		CString sFlac = L"FLAC";
		ti.pszText = sWav.GetBuffer(); m_tabs.InsertItem(TC_TAB_WAV, &ti); sWav.ReleaseBuffer();
		ti.pszText = sMp3.GetBuffer(); m_tabs.InsertItem(TC_TAB_MP3, &ti); sMp3.ReleaseBuffer();
		ti.pszText = sFlac.GetBuffer(); m_tabs.InsertItem(TC_TAB_FLAC, &ti); sFlac.ReleaseBuffer();
		m_tabs.LayoutEqualTabs(3);
		{
			CRect rcItem;
			if (m_tabs.GetItemRect(0, &rcItem)) {
				const int stripH = rcItem.Height() + 6;
				m_tabs.SetWindowPos(NULL, 0, 0, rTabs.Width(), stripH, SWP_NOMOVE | SWP_NOZORDER);
			}
		}

		// 品質コンボを繰返し行の右側へ（タブ帯と重ならない位置）
		m_qualityLabel.SetWindowPos(NULL, rQL.left, rLoopL.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		m_quality.SetWindowPos(NULL, rQ.left, rLoop.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		// サンプリングは品質の下（リソース＋DDX。動的 Create はしない）
		{
			const int top = rLoop.bottom + 6;
			const int rowH = 24;
			m_srateLabel.SetWindowPos(NULL, rQL.left, top + 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
			m_srate.SetWindowPos(NULL, rQ.left, top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
			CRect rPathL, rPath, rBr;
			m_pathLabel.GetWindowRect(&rPathL); ScreenToClient(&rPathL);
			m_path.GetWindowRect(&rPath); ScreenToClient(&rPath);
			m_browse.GetWindowRect(&rBr); ScreenToClient(&rBr);
			const int needTop = top + rowH + 6;
			if (rPathL.top < needTop) {
				const int dy = needTop - rPathL.top;
				m_pathLabel.SetWindowPos(NULL, rPathL.left, rPathL.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
				m_path.SetWindowPos(NULL, rPath.left, rPath.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
				m_browse.SetWindowPos(NULL, rBr.left, rBr.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
			}
		}

		int tab = m_initialTab;
		if (tab < TC_TAB_WAV || tab > TC_TAB_FLAC) {
			// 既定は前回の mp3/FLAC（品質UIが見える状態から開始）
			tab = (savedata.tc_format == TC_FMT_FLAC) ? TC_TAB_FLAC : TC_TAB_MP3;
		}
		m_tabs.SetCurSel(tab);
	}
	m_format.AddString(L"mp3");
	m_format.AddString(L"FLAC");
	m_format.SetCurSel((savedata.tc_format == TC_FMT_FLAC) ? TC_FMT_FLAC : TC_FMT_MP3);
	ApplyTabUi();
	if (m_tabs.GetSafeHwnd())
		m_tabs.Invalidate(FALSE);
	PostMessage(WM_TC_LAYOUT_TABS, 0, 0);

	m_loopLabel.SetWindowText(LL14(L"繰返し回数", L"Loop count", L"Nombre de boucles", L"Conteggio loop",
		L"Repeticiones", L"반복 횟수", L"循环次数", L"عدد التكرار",
		L"Количество повторов", L"Schleifenzahl", L"Repetições", L"Aantal herhalingen",
		L"Liczba powtórzeń", L"Döngü sayısı"));
	m_fadeCheck.SetWindowText(LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza",
		L"Fundido", L"페이드 아웃", L"淡出", L"تلاشي",
		L"Затухание", L"Ausblenden", L"Fade out", L"Fade-out",
		L"Wyciszanie", L"Solma"));
	m_fadeLabel.SetWindowText(LL14(L"秒", L"sec", L"sec", L"sec",
		L"seg", L"초", L"秒", L"ث",
		L"сек", L"Sek", L"seg", L"sec",
		L"sek", L"sn"));
	m_xfadeCheck.SetWindowText(LL14(L"クロスフェード", L"Crossfade", L"Fondu enchainé", L"Crossfade",
		L"Fundido cruzado", L"크로스페이드", L"交叉淡入淡出", L"تلاشي متقاطع",
		L"Кроссфейд", L"Überblenden", L"Crossfade", L"Crossfade",
		L"Przejscie", L"Capraz solma"));
	m_xfadeLabel.SetWindowText(LL14(L"秒", L"sec", L"sec", L"sec",
		L"seg", L"초", L"秒", L"ث",
		L"сек", L"Sek", L"seg", L"sec",
		L"sek", L"sn"));
	if (m_mixCheck.GetSafeHwnd())
		m_mixCheck.SetWindowText(LL14(L"ミックス", L"Mix", L"Mix", L"Mix",
			L"Mezcla", L"믹스", L"混音", L"Mix",
			L"Микс", L"Mix", L"Mix", L"Mix",
			L"Mix", L"Mix"));
	if (m_mixNLabel.GetSafeHwnd())
		m_mixNLabel.SetWindowText(LL14(L"同時曲数", L"Concurrent", L"Simultane", L"Simultanee",
			L"Simultaneas", L"동시 곡수", L"同时曲数", L"Concurrent",
			L"Одновременно", L"Gleichzeitig", L"Simultaneas", L"Gelijktijdig",
			L"Jednoczesne", L"Eszamanli"));
	m_trimCheck.SetWindowText(LL14(L"先頭無音を揃える", L"Align leading silence", L"Aligner silence initial", L"Allinea silenzio iniziale",
		L"Alinear silencio inicial", L"앞 무음 맞추기", L"对齐开头静音", L"مواءمة الصمت الابتدائي",
		L"Выровнять нач. тишину", L"Anfangsstille angleichen", L"Alinhar silencio inicial", L"Beginstilte uitlijnen",
		L"Wyrównaj ciszę na początku", L"Bastaki sessizligi hizala"));
	m_trimLabel.SetWindowText(LL14(L"秒", L"sec", L"sec", L"sec",
		L"seg", L"초", L"秒", L"ث",
		L"сек", L"Sek", L"seg", L"sec",
		L"sek", L"sn"));
	m_copyTags.SetWindowText(LL14(L"タグとジャケットをコピー", L"Copy tags and cover art", L"Copier les tags et la pochette", L"Copia tag e copertina",
		L"Copiar etiquetas y portada", L"태그와 재킷 복사", L"复制标签和封面", L"نسخ الوسوم والغلاف",
		L"Копировать теги и обложку", L"Tags und Cover kopieren", L"Copiar tags e capa", L"Tags en hoes kopiëren",
		L"Kopiuj tagi i okładkę", L"Etiketleri ve kapağı kopyala"));
	if (m_promptCheck.GetSafeHwnd())
		m_promptCheck.SetWindowText(LL14(L"プロンプト実行を適用", L"Apply prompt execution", L"Appliquer le prompt", L"Applica esecuzione prompt",
			L"Aplicar ejecucion del prompt", L"프롬프트 실행 적용", L"应用提示执行", L"Apply prompt",
			L"Применить промпт", L"Prompt anwenden", L"Aplicar prompt", L"Prompt toepassen",
			L"Zastosuj prompt", L"Prompt uygula"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi",
		L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten",
		L"Zamknij", L"Kapat"));
	m_exec.SetWindowText(LL14(L"実行", L"Execute", L"Exécuter", L"Esegui",
		L"Ejecutar", L"실행", L"执行", L"تنفيذ",
		L"Выполнить", L"Ausführen", L"Executar", L"Uitvoeren",
		L"Wykonaj", L"Çalıştır"));

	m_loop.SetWindowText(L"1");
	// KPI秒数はリソース＋DDX（動的 Create だとアクリル下で白抜けする）
	{
		m_kpiSecLabel.SetWindowText(LL14(L"長さ(秒)", L"Length (sec)", L"Duree (s)", L"Durata (s)",
			L"Duracion (s)", L"길이(초)", L"长度(秒)", L"المدة (ث)",
			L"Длина (сек)", L"Dauer (s)", L"Duracao (s)", L"Duur (s)",
			L"Dlugosc (s)", L"Sure (sn)"));
		CString sec;
		sec.Format(L"%d", DefaultKpiDurationSec());
		m_kpiSec.SetWindowText(sec);
		ApplyKpiDurationUi();
	}
	// サンプリングレート（リソース＋DDX済み。動的 Create はハング原因のため使わない）
	{
		m_srateLabel.SetWindowText(LL14(L"サンプリング", L"Sample rate", L"Echantillonnage", L"Campionamento",
			L"Muestreo", L"샘플링", L"采样率", L"معدل العينة",
			L"Частота", L"Abtastrate", L"Amostragem", L"Samplefreq.",
			L"Probkowanie", L"Ornekleme"));
		m_srate.ResetContent();
		m_srate.AddString(LL14(L"ソースのまま", L"Source", L"Source", L"Sorgente",
			L"Fuente", L"원본", L"原始", L"المصدر",
			L"Исходный", L"Quelle", L"Origem", L"Bron",
			L"Zrodlo", L"Kaynak"));
		m_srate.AddString(L"44100 Hz");
		m_srate.AddString(L"48000 Hz");
		m_srate.AddString(L"96000 Hz");
		m_srate.AddString(L"192000 Hz");
		int sel = 2; // 既定 48000
		switch (savedata.wav_export_sample_rate) {
		case 0: sel = 0; break;
		case 44100: sel = 1; break;
		case 48000: sel = 2; break;
		case 96000: sel = 3; break;
		case 192000: sel = 4; break;
		default: sel = 2; break;
		}
		m_srate.SetCurSel(sel);
		m_srate.SetAeroMode(FALSE);
	}
	int fadeSec = savedata.wav_export_fade_sec;
	if (fadeSec <= 0) fadeSec = 15;
	int trimKeep = savedata.wav_export_trim_keep_sec;
	if (trimKeep <= 0) trimKeep = 1;
	int xfadeSec = savedata.wav_export_xfade_sec;
	if (xfadeSec < 1) xfadeSec = 5;
	CString s;
	s.Format(L"%d", fadeSec);
	m_fadeSec.SetWindowText(s);
	s.Format(L"%d", trimKeep);
	m_trimSec.SetWindowText(s);
	s.Format(L"%d", xfadeSec);
	m_xfadeSec.SetWindowText(s);
	m_fadeCheck.SetCheck(savedata.wav_export_fade ? BST_CHECKED : BST_UNCHECKED);
	m_trimCheck.SetCheck(savedata.wav_export_trim_lead ? BST_CHECKED : BST_UNCHECKED);
	m_copyTags.SetCheck(savedata.wav_export_copy_tags ? BST_CHECKED : BST_UNCHECKED);
	if (m_promptCheck.GetSafeHwnd())
		m_promptCheck.SetCheck((savedata.wav_export_apply_prompt || MpPromptIsActive()) ? BST_CHECKED : BST_UNCHECKED);

	// クロスフェード／ミックスは複数選択時のみ。チェック時は単体ファイル名モード。
	const BOOL showXfade = multiFile && pcs.size() >= 2;
	m_xfadeCheck.ShowWindow(showXfade ? SW_SHOW : SW_HIDE);
	m_xfadeSec.ShowWindow(showXfade ? SW_SHOW : SW_HIDE);
	m_xfadeLabel.ShowWindow(showXfade ? SW_SHOW : SW_HIDE);
	if (showXfade) {
		const bool xfadeOn = m_preferXfade || (savedata.wav_export_xfade != 0);
		m_xfadeCheck.SetCheck(xfadeOn ? BST_CHECKED : BST_UNCHECKED);
		// ミックスとクロスフェードは併用可能（排他にしない）
		if (m_mixCheck.GetSafeHwnd())
			m_mixCheck.SetCheck((savedata.wav_export_mix != 0) ? BST_CHECKED : BST_UNCHECKED);
	}
	else {
		m_xfadeCheck.SetCheck(BST_UNCHECKED);
		if (m_mixCheck.GetSafeHwnd())
			m_mixCheck.SetCheck(BST_UNCHECKED);
	}
	if (m_mixVol.GetSafeHwnd()) {
		m_mixVol.SetAeroMode(FALSE);
		m_mixVol.SetExtendedStyle(m_mixVol.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
		m_mixVol.ModifyStyle(0, LVS_EDITLABELS);
		while (m_mixVol.DeleteColumn(0)) {}
		m_mixVol.InsertColumn(0, LL14(L"割合(%)", L"%", L"%", L"%", L"%", L"%", L"%", L"%", L"%", L"%", L"%", L"%", L"%", L"%"), LVCFMT_RIGHT, 56);
		m_mixVol.InsertColumn(1, LL14(L"曲名", L"Track", L"Piste", L"Traccia", L"Pista", L"곡명", L"曲名", L"Track",
			L"Трек", L"Titel", L"Faixa", L"Nummer", L"Utwor", L"Parca"), LVCFMT_LEFT, 170);
	}
	if (m_mixN.GetSafeHwnd())
		m_mixN.SetAeroMode(FALSE);
	ApplyMixUi();

	const bool singleOutNow = IsXfadeMode() || IsMixMode();
	ExportTagUi_InitFields(multiFile && !singleOutNow, pc, m_title, m_artist, m_album,
		m_titleL, m_artistL, m_albumL, m_coverL, m_coverPic, m_cover, m_coverClear, m_coverPath, m_coverBmp);
	ApplyPathModeUi(false);
	m_status.SetWindowText(L"");
	if (CWnd* pPh = GetDlgItem(IDC_TC_PROGRESS)) {
		CRect rc; pPh->GetWindowRect(&rc); ScreenToClient(&rc);
		pPh->DestroyWindow();
		m_progress.Create(WS_CHILD | WS_VISIBLE, rc, this, IDC_TC_PROGRESS);
		m_progress.SetRange(0, 100);
		m_progress.SetPos(0);
		m_progress.SetShowPercent(TRUE);
		m_progress.SetColors(RGB(255, 236, 246), RGB(255, 170, 200), RGB(200, 120, 220));
		// 書き出し中の進捗は不透明描画（Aero透過だとエンコード中に親が再描画されず止まる）
		m_progress.SetAeroMode(FALSE);
	}
	if (CWnd* pProgL = GetDlgItem(IDC_TC_PROG_L))
		pProgL->SetWindowText(LL14(L"進捗", L"Progress", L"Progression", L"Avanzamento", L"Progreso", L"진행", L"进度", L"Progress", L"Прогресс", L"Fortschritt", L"Progresso", L"Voortgang", L"Postep", L"Ilerleme"));
	DragAcceptFiles(TRUE);

	// カラム整列:
	//  [✓箱] 左テキスト揃え | 左入力揃え |  [✓箱] 右テキスト揃え | 右入力揃え
	{
		UINT dpi = 96;
		if (HDC hdcDpi = ::GetDC(GetSafeHwnd())) {
			dpi = (UINT)GetDeviceCaps(hdcDpi, LOGPIXELSX);
			::ReleaseDC(GetSafeHwnd(), hdcDpi);
			if (dpi == 0) dpi = 96;
		}
		auto scale = [dpi](int v96) -> int { return MulDiv(v96, (int)dpi, 96); };
		auto heightOf = [this](CWnd& w) -> int {
			if (!w.GetSafeHwnd()) return 18;
			CRect r; w.GetWindowRect(&r); return r.Height();
		};
		auto textW = [this](CWnd& w) -> int {
			if (!w.GetSafeHwnd()) return 0;
			CString t; w.GetWindowText(t);
			if (t.IsEmpty()) t = L"W";
			int cx = 0;
			if (CDC* pDc = GetDC()) {
				CFont* pFont = w.GetFont();
				if (!pFont) pFont = GetFont();
				CFont* pOld = pDc->SelectObject(pFont);
				cx = pDc->GetTextExtent(t).cx;
				if (pOld) pDc->SelectObject(pOld);
				ReleaseDC(pDc);
			}
			return cx;
		};
		auto placeSized = [this](CWnd& w, int x, int y, int width, int height) {
			if (!w.GetSafeHwnd()) return;
			w.SetWindowPos(NULL, x, y, width, height, SWP_NOZORDER);
		};
		auto ctlW = [](CWnd& w, int defW) -> int {
			if (!w.GetSafeHwnd()) return defW;
			CRect r; w.GetWindowRect(&r);
			return r.Width() > 0 ? r.Width() : defW;
		};
		auto ctlH = [&](CWnd& w, int defH) -> int {
			const int h = heightOf(w);
			return h > 0 ? h : defH;
		};

		CRect rcClient; GetClientRect(&rcClient);
		const int marginL = scale(7);
		const int clientRight = rcClient.right - scale(7);
		const int box = scale(18);          // チェック箱
		const int boxGap = scale(8);        // 箱→文字
		const int xCheckL = marginL;        // 左チェック箱の左端
		const int xTextL = xCheckL + box + boxGap; // 左テキスト（静的もチェック文言もここ揃え）

		// 左テキスト列の最大幅（繰返し／長さ／フェード／無音／ミックス）
		int maxTextL = textW(m_loopLabel);
		if (m_kpiSecLabel.GetSafeHwnd()) maxTextL = (std::max)(maxTextL, textW(m_kpiSecLabel));
		maxTextL = (std::max)(maxTextL, textW(m_fadeCheck));
		maxTextL = (std::max)(maxTextL, textW(m_trimCheck));
		if (m_mixCheck.GetSafeHwnd()) maxTextL = (std::max)(maxTextL, textW(m_mixCheck));
		maxTextL += scale(4);

		int xValL = xTextL + maxTextL + scale(12); // 左入力（1 / 30 / 15 / 1）
		const int secEditW = (std::max)(ctlW(m_fadeSec, scale(40)), scale(40));
		const int unitW = (std::max)(textW(m_fadeLabel) + scale(4), scale(16));
		const int leftValBlockW = (std::max)(ctlW(m_loop, scale(45)),
			(std::max)(ctlW(m_kpiSec, scale(45)), secEditW + scale(4) + unitW));

		// 右テキスト列（はみ出す場合は列間ギャップを詰める）
		int maxTextR = textW(m_qualityLabel);
		maxTextR = (std::max)(maxTextR, textW(m_srateLabel));
		if (showXfade) maxTextR = (std::max)(maxTextR, textW(m_xfadeCheck));
		maxTextR = (std::max)(maxTextR, textW(m_copyTags));
		maxTextR += scale(4);
		const int rightComboMin = scale(100);
		int colGap = scale(24);
		int xCheckR = xValL + leftValBlockW + colGap;
		int xTextR = xCheckR + box + boxGap;
		int xValR = xTextR + maxTextR + scale(12);
		while (xValR + rightComboMin > clientRight && colGap > scale(8)) {
			colGap -= scale(2);
			xCheckR = xValL + leftValBlockW + colGap;
			xTextR = xCheckR + box + boxGap;
			xValR = xTextR + maxTextR + scale(12);
		}
		if (xValR + rightComboMin > clientRight) {
			// それでも足りなければ右テキスト列幅をやや縮める（表示は省略なし・入力優先）
			const int need = xValR + rightComboMin - clientRight;
			maxTextR = (std::max)(scale(48), maxTextR - need);
			xValR = xTextR + maxTextR + scale(12);
		}

		auto rowHOf = [&](int a, int b) { return (std::max)(a, b); };

		// 静的ラベルを xTextL/R に、入力を xValL/R に（縦中央）
		auto placeStaticVal = [&](CWnd& lab, CWnd& ed, int xText, int xVal, int yTop, int edW, int* outH) {
			const int labH = (std::max)(ctlH(lab, scale(14)), scale(14));
			const int edH = (std::max)(ctlH(ed, scale(22)), scale(22));
			const int rh = rowHOf(labH, edH);
			placeSized(lab, xText, yTop + (rh - labH) / 2, textW(lab) + scale(6), labH);
			placeSized(ed, xVal, yTop + (rh - edH) / 2, edW, edH);
			if (outH) *outH = rh;
		};
		// チェックを xCheck に置き、文言開始が xText になる幅にする + 秒欄
		auto placeCheckSec = [&](CWnd& chk, CWnd& sec, CWnd& unit, int xCheck, int xText, int xVal, int yTop, int* outH) {
			const int tw = textW(chk);
			const int chkW = (xText - xCheck) + tw + scale(10);
			const int chkH = (std::max)(ctlH(chk, scale(18)), scale(18));
			const int secH = (std::max)(ctlH(sec, scale(22)), scale(22));
			const int unitH = (std::max)(ctlH(unit, scale(14)), scale(14));
			const int rh = (std::max)(chkH, (std::max)(secH, unitH));
			placeSized(chk, xCheck, yTop + (rh - chkH) / 2, chkW, chkH);
			placeSized(sec, xVal, yTop + (rh - secH) / 2, secEditW, secH);
			placeSized(unit, xVal + secEditW + scale(4), yTop + (rh - unitH) / 2, unitW, unitH);
			if (outH) *outH = rh;
		};
		auto placeCheckAt = [&](CWnd& chk, int xCheck, int xText, int yTop, int rowH) {
			const int tw = textW(chk);
			const int chkW = (xText - xCheck) + tw + scale(10);
			const int chkH = (std::max)(ctlH(chk, scale(18)), scale(18));
			placeSized(chk, xCheck, yTop + (rowH - chkH) / 2, chkW, chkH);
		};

		CRect rLoopL; m_loopLabel.GetWindowRect(&rLoopL); ScreenToClient(&rLoopL);
		int y = rLoopL.top;

		// 行: 繰返し | 品質
		{
			int hL = 0, hR = 0;
			placeStaticVal(m_loopLabel, m_loop, xTextL, xValL, y, ctlW(m_loop, scale(45)), &hL);
			placeStaticVal(m_qualityLabel, m_quality, xTextR, xValR, y, ctlW(m_quality, scale(90)), &hR);
			y += rowHOf(hL, hR) + scale(6);
		}
		// 行: 長さ | サンプリング
		{
			int hL = 0, hR = 0;
			if (m_kpiSec.GetSafeHwnd())
				placeStaticVal(m_kpiSecLabel, m_kpiSec, xTextL, xValL, y, ctlW(m_kpiSec, scale(45)), &hL);
			placeStaticVal(m_srateLabel, m_srate, xTextR, xValR, y, ctlW(m_srate, scale(110)), &hR);
			y += rowHOf(hL, hR) + scale(8);
		}

		// 出力パス（全幅）
		{
			const int pathLabH = (std::max)(ctlH(m_pathLabel, scale(14)), scale(14));
			placeSized(m_pathLabel, xCheckL, y, textW(m_pathLabel) + scale(6), pathLabH);
			y += pathLabH + scale(2);
			const int brW = ctlW(m_browse, scale(35));
			const int brH = ctlH(m_browse, scale(22));
			const int pathH = ctlH(m_path, scale(22));
			const int pathW = (std::max)(scale(80), clientRight - brW - scale(6) - xCheckL);
			const int rh = rowHOf(pathH, brH);
			placeSized(m_path, xCheckL, y + (rh - pathH) / 2, pathW, pathH);
			placeSized(m_browse, xCheckL + pathW + scale(6), y + (rh - brH) / 2, brW, brH);
			y += rh + scale(12);
		}

		// 行: フェード | クロスフェード
		{
			int hL = 0, hR = 0;
			placeCheckSec(m_fadeCheck, m_fadeSec, m_fadeLabel, xCheckL, xTextL, xValL, y, &hL);
			if (showXfade && m_xfadeCheck.GetSafeHwnd())
				placeCheckSec(m_xfadeCheck, m_xfadeSec, m_xfadeLabel, xCheckR, xTextR, xValR, y, &hR);
			y += rowHOf(hL, hR) + scale(8);
		}
		// 行: 先頭無音 | タグコピー
		{
			int hL = 0;
			placeCheckSec(m_trimCheck, m_trimSec, m_trimLabel, xCheckL, xTextL, xValL, y, &hL);
			int hR = hL;
			if (m_copyTags.GetSafeHwnd()) {
				const int chkH = (std::max)(ctlH(m_copyTags, scale(18)), scale(18));
				hR = (std::max)(hL, chkH);
				placeCheckAt(m_copyTags, xCheckR, xTextR, y, hR);
			}
			y += rowHOf(hL, hR) + scale(8);
		}

		// ミックス（左テキスト列にチェック、同時曲数は左入力列付近）
		const BOOL showMixRow = multiFile && pcs.size() >= 2;
		if (showMixRow && m_mixCheck.GetSafeHwnd()) {
			CRect rSrate; m_srate.GetWindowRect(&rSrate);
			int comboH = rSrate.Height();
			if (comboH < scale(22)) comboH = scale(22);
			int textW88 = scale(18);
			int fontH = scale(16);
			if (CDC* pDc = GetDC()) {
				CFont* pOldF = pDc->SelectObject(GetFont());
				textW88 = pDc->GetTextExtent(L"88").cx;
				TEXTMETRIC tm = {};
				pDc->GetTextMetrics(&tm);
				if (tm.tmHeight > 0) fontH = tm.tmHeight;
				if (pOldF) pDc->SelectObject(pOldF);
				ReleaseDC(pDc);
			}
			const int crown = (std::max)(scale(8), (comboH - scale(8)) / 2);
			const int btnW = ::GetSystemMetrics(SM_CXVSCROLL) + scale(12);
			int comboW = scale(12) + crown * 2 + scale(4) + textW88 + scale(8) + btnW;
			if (comboW < scale(100)) comboW = scale(100);
			if (comboW < rSrate.Width()) comboW = rSrate.Width();

			const int nLabW = textW(m_mixNLabel) + scale(6);
			const int nLabH = (std::max)(ctlH(m_mixNLabel, scale(14)), scale(14));
			const int mixH = (std::max)(ctlH(m_mixCheck, scale(18)), scale(18));
			const int rh = (std::max)(mixH, (std::max)(nLabH, comboH));
			placeCheckAt(m_mixCheck, xCheckL, xTextL, y, rh);
			// 同時曲数は左入力列から
			placeSized(m_mixNLabel, xValL, y + (rh - nLabH) / 2, nLabW, nLabH);
			placeSized(m_mixN, xValL + nLabW + scale(6), y + (rh - comboH) / 2, comboW, comboH);
			y += rh + scale(6);

			int rows = (int)pcs.size();
			if (rows < 2) rows = 2;
			if (rows > 10) rows = 10;
			const int volW = (std::max)(scale(120), clientRight - xCheckL);
			int guessRow = fontH + scale(12);
			int guessHdr = fontH + scale(10);
			int listH = guessHdr + guessRow * rows + scale(8);
			m_mixVol.SetWindowPos(NULL, xCheckL, y, volW, listH, SWP_NOZORDER);
			m_mixVol.SetColumnWidth(0, (std::max)(scale(64), textW88 + scale(28)));
			m_mixVol.SetColumnWidth(1, (std::max)(scale(80), volW - m_mixVol.GetColumnWidth(0) - scale(4)));
			int headerH = 0;
			if (CHeaderCtrl* pHdr = m_mixVol.GetHeaderCtrl()) {
				CRect rhdr; pHdr->GetWindowRect(&rhdr);
				headerH = rhdr.Height();
			}
			int itemH = 0;
			if (m_mixVol.GetItemCount() > 0) {
				CRect ri;
				if (m_mixVol.GetItemRect(0, &ri, LVIR_BOUNDS))
					itemH = ri.Height();
			}
			if (headerH < scale(18)) headerH = (std::max)(guessHdr, fontH + scale(8));
			if (itemH < scale(18)) itemH = (std::max)(guessRow, fontH + scale(12));
			listH = headerH + itemH * rows + scale(6);
			m_mixVol.SetWindowPos(NULL, xCheckL, y, volW, listH, SWP_NOZORDER);
			y += listH + scale(8);
		}
		else if (m_mixCheck.GetSafeHwnd()) {
			m_mixCheck.ShowWindow(SW_HIDE);
			if (m_mixNLabel.GetSafeHwnd()) m_mixNLabel.ShowWindow(SW_HIDE);
			if (m_mixN.GetSafeHwnd()) m_mixN.ShowWindow(SW_HIDE);
			if (m_mixVol.GetSafeHwnd()) m_mixVol.ShowWindow(SW_HIDE);
		}

		if (m_promptCheck.GetSafeHwnd()) {
			const int ph = (std::max)(ctlH(m_promptCheck, scale(18)), scale(18));
			placeCheckAt(m_promptCheck, xCheckL, xTextL, y, ph);
			y += ph + scale(10);
		}

		auto placeMetaRow = [&](CWnd& lab, CWnd& ed) {
			const int labW = (std::max)(textW(lab) + scale(6), scale(50));
			const int edH = (std::max)(ctlH(ed, scale(22)), scale(22));
			const int labH = (std::max)(ctlH(lab, scale(14)), scale(14));
			const int rh = rowHOf(labH, edH);
			const int edX = xTextL + labW + scale(6);
			const int edW = (std::max)(scale(80), clientRight - edX);
			placeSized(lab, xTextL, y + (rh - labH) / 2, labW, labH);
			placeSized(ed, edX, y + (rh - edH) / 2, edW, edH);
			y += rh + scale(6);
		};
		placeMetaRow(m_titleL, m_title);
		placeMetaRow(m_artistL, m_artist);
		placeMetaRow(m_albumL, m_album);
		y += scale(2);

		{
			const int coverY = y;
			const int labW = textW(m_coverL) + scale(6);
			const int labH = (std::max)(ctlH(m_coverL, scale(14)), scale(14));
			placeSized(m_coverL, xTextL, coverY + scale(2), labW, labH);
			const int picW = ctlW(m_coverPic, scale(56));
			const int picH = ctlH(m_coverPic, scale(56));
			const int picX = xTextL + labW + scale(6);
			placeSized(m_coverPic, picX, coverY, picW, picH);
			const int dropH = ctlH(m_cover, scale(40));
			const int clrW = ctlW(m_coverClear, scale(58));
			const int clrH = ctlH(m_coverClear, scale(16));
			const int dropX = picX + picW + scale(6);
			const int dropW = (std::max)(scale(80), clientRight - clrW - scale(6) - dropX);
			placeSized(m_cover, dropX, coverY + scale(8), dropW, dropH);
			placeSized(m_coverClear, dropX + dropW + scale(6), coverY + scale(18), clrW, clrH);
			y = coverY + (std::max)(picH, dropH + scale(8)) + scale(10);
		}

		{
			const int bw = ctlW(m_exec, scale(80));
			const int bh = ctlH(m_exec, scale(22));
			const int gap = scale(12);
			const int total = bw * 2 + gap;
			const int x0 = (std::max)(marginL, (rcClient.Width() - total) / 2);
			placeSized(m_exec, x0, y, bw, bh);
			placeSized(m_close, x0 + bw + gap, y, bw, bh);
			y += bh + scale(10);
		}
		if (CWnd* pProgL = GetDlgItem(IDC_TC_PROG_L)) {
			const int lh = (std::max)(heightOf(*pProgL), scale(14));
			placeSized(*pProgL, xCheckL, y, textW(*pProgL) + scale(6), lh);
			y += lh + scale(4);
		}
		{
			const int ph = (std::max)(heightOf(m_progress), scale(16));
			placeSized(m_progress, xCheckL, y, (std::max)(scale(80), clientRight - xCheckL), ph);
			y += ph + scale(8);
		}
		{
			const int sh = (std::max)(heightOf(m_status), scale(14));
			placeSized(m_status, xCheckL, y, (std::max)(scale(80), clientRight - xCheckL), sh);
			y += sh + scale(10);
		}

		CRect rcWin; GetWindowRect(&rcWin);
		const int chrome = rcWin.Height() - rcClient.Height();
		if (y + chrome > 120)
			SetWindowPos(NULL, 0, 0, rcWin.Width(), y + chrome, SWP_NOMOVE | SWP_NOZORDER);
	}


	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		auto addTip = [this](CWnd& w, LPCWSTR text) {
			if (w.GetSafeHwnd() && text && text[0])
				m_tooltip.AddTool(&w, text);
		};
		addTip(m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
		// チェックボックス
		addTip(m_fadeCheck, LL14(			L"出力の末尾だけ指定秒でフェードアウトします（ミックス時も合成後の末尾に適用）",
			L"Fade out only the end of the output for the given seconds (also after mix)",
			L"Fondu sortant en fin de sortie uniquement",
			L"Dissolvenza solo in coda all'uscita",
			L"Fundido solo al final de la salida",
			L"출력 끝만 지정 초로 페이드 아웃(믹스 후에도 적용)",
			L"仅对输出末尾按指定秒淡出（混音后也适用）",
			L"Fade out only the end of the output",
			L"Затухание только в конце выхода",
			L"Nur das Ende der Ausgabe ausblenden",
			L"Fade out so no final da saida",
			L"Alleen einde van uitvoer faden",
			L"Wycisz tylko koniec wyjsciowego pliku",
			L"Cikti sonunu verilen sn solutur"));
		addTip(m_xfadeCheck, LL14(
			L"単独:曲を順に連結してクロスフェード。ミックス併用:終わった枠へ次曲を指定秒で等パワー投入",
			L"Alone: join tracks with crossfade. With Mix: equal-power refill of ended slots",
			L"Seul: enchaine avec fondu. Avec Mix: insertion a puissance egale",
			L"Solo: unisce con crossfade. Con Mix: riempimento a potenza uguale",
			L"Solo: une con fundido. Con Mix: relleno a potencia igual",
			L"단독: 순차 연결 크로스페이드. 믹스 병용: 끝난 자리에 등파워 보충",
			L"单独:顺序交叉淡入连接。与混音并用:等功率补充下一曲",
			L"Alone: sequential crossfade. With Mix: equal-power refill",
			L"Отдельно: склейка с кроссфейдом. С Mix: дозаполнение",
			L"Allein: Ueberblend-Sequenz. Mit Mix: Auffuellen gleicher Leistung",
			L"Sozinho: junta com crossfade. Com Mix: reposicao equal-power",
			L"Alleen: crossfade-reeks. Met Mix: equal-power bijvullen",
			L"Samodzielnie: przejscie kolejno. Z Mix: uzupelnianie equal-power",
			L"Tek: sirayla capraz solma. Mix ile: es guc ekleme"));
		addTip(m_trimCheck, LL14(
			L"先頭の無音を指定秒に揃えます。長い場合はカット、短い場合は無音を足します",
			L"Align leading silence to the given seconds (trim if longer, pad if shorter)",
			L"Aligne le silence initial (coupe si trop long, complete si trop court)",
			L"Allinea il silenzio iniziale (taglia se lungo, riempie se corto)",
			L"Alinea el silencio inicial (corta si sobra, rellena si falta)",
			L"앞 무음을 지정 초에 맞춥니다. 길면 자르고 짧으면 무음을 넣습니다",
			L"将开头静音对齐到指定秒：过长则切除，过短则补静音",
			L"Align leading silence to N sec (trim or pad)",
			L"Выровнять начальную тишину: обрезать или дополнить",
			L"Anfangsstille auf N Sek. (kuerzen oder auffuellen)",
			L"Alinha silencio inicial (corta ou completa)",
			L"Beginstilte op N sec (trim of pad)",
			L"Wyrównaj ciszę początkową (przytnij lub uzupełnij)",
			L"Bastaki sessizligi sn'ye hizalar (keser veya doldurur)"));
		addTip(m_copyTags, LL14(
			L"元ファイルのタグとジャケット画像を出力へコピーします",
			L"Copy tags and cover art from the source to the output",
			L"Copie tags et pochette de la source vers la sortie",
			L"Copia tag e copertina dalla sorgente all'uscita",
			L"Copia etiquetas y portada del origen a la salida",
			L"원본 태그와 재킷을 출력으로 복사합니다",
			L"将源文件的标签和封面复制到输出",
			L"Copy tags and cover from source to output",
			L"Копировать теги и обложку из источника",
			L"Tags und Cover von der Quelle kopieren",
			L"Copia tags e capa da origem para a saida",
			L"Kopieer tags en hoes van bron naar uitvoer",
			L"Kopiuj tagi i okladke ze zrodla",
			L"Kaynaktan etiket ve kapagi kopyalar"));
		addTip(m_promptCheck, LL14(
			L"書き出し時にプロンプト実行（再生エフェクト等）を適用します",
			L"Apply prompt execution (playback effects etc.) during export",
			L"Appliquer l'execution du prompt a l'export",
			L"Applica l'esecuzione del prompt in esportazione",
			L"Aplicar la ejecucion del prompt al exportar",
			L"내보내기 시 프롬프트 실행(재생 효과 등)을 적용합니다",
			L"导出时应用提示执行（播放效果等）",
			L"Apply prompt execution during export",
			L"Применить выполнение промпта при экспорте",
			L"Prompt-Ausfuehrung beim Export anwenden",
			L"Aplicar execucao do prompt na exportacao",
			L"Prompt-uitvoering toepassen bij export",
			L"Zastosuj wykonanie promptu przy eksporcie",
			L"Disa aktarimda prompt uygulamasini kullanir"));
		addTip(m_mixCheck, LL14(
			L"選択曲を同時に重ねて1ファイルにします。クロスフェードONなら終了枠へ次曲を指定秒で投入",
			L"Layer selected tracks into one file; with Crossfade ON refill ended slots over given seconds",
			L"Superpose les pistes en un fichier; avec Fondu, inserte sur la duree",
			L"Sovrappone le tracce in un file; con Crossfade inserisce nella durata",
			L"Superpone pistas en un archivo; con Fundido rellena en los segundos",
			L"선택 곡을 동시 겹쳐 한 파일로. 크로스페이드 ON이면 지정 초로 보충",
			L"同时叠加选曲为单文件；交叉淡入ON时按秒补充下一曲",
			L"Layer tracks into one file; with Crossfade ON refill over seconds",
			L"Накладывает треки в один файл; с кроссфейдом дозаполняет",
			L"Mischt Titel in eine Datei; mit Ueberblendung nachfuellen",
			L"Sobrepoe faixas num arquivo; com Crossfade repoe em segundos",
			L"Laagt nummers in een bestand; met Crossfade bijvullen",
			L"Naklada utwory do jednego pliku; z Crossfade uzupelnia",
			L"Parcalari tek dosyada karistirir; Capraz solma ile ekler"));
		// 秒数欄（関連UI）
		addTip(m_fadeSec, LL14(
			L"フェードアウトする秒数",
			L"Fade-out duration in seconds",
			L"Duree du fondu en secondes",
			L"Durata dissolvenza in secondi",
			L"Duracion del fundido en segundos",
			L"페이드 아웃 초",
			L"淡出秒数",
			L"Fade-out seconds",
			L"Секунды затухания",
			L"Ausblend-Sekunden",
			L"Segundos de fade out",
			L"Fade-out-seconden",
			L"Sekundy wyciszania",
			L"Solma suresi (sn)"));
		addTip(m_xfadeSec, LL14(
			L"クロスフェード秒（順次連結の重ね／ミックス補充の交接）",
			L"Crossfade seconds (sequential overlap or mix handoff)",
			L"Secondes de fondu (enchaine ou insertion mix)",
			L"Secondi di crossfade (sequenza o passaggio mix)",
			L"Segundos de fundido (secuencia o relevo mix)",
			L"크로스페이드 초(순차 겹침/믹스 교대)",
			L"交叉淡入秒数（顺序重叠或混音交接）",
			L"Crossfade seconds (sequence or mix handoff)",
			L"Секунды кроссфейда (склейка или смена в миксе)",
			L"Ueberblend-Sekunden (Sequenz oder Mix-Uebergabe)",
			L"Segundos de crossfade (sequencia ou troca mix)",
			L"Crossfade-seconden (reeks of mix-overdracht)",
			L"Sekundy przejscia (sekwencja lub zmiana mix)",
			L"Capraz solma sn (sira veya mix devir)"));
		addTip(m_trimSec, LL14(
			L"先頭に置く無音の目標秒数",
			L"Target seconds of leading silence",
			L"Secondes cibles de silence initial",
			L"Secondi obiettivo di silenzio iniziale",
			L"Segundos objetivo de silencio inicial",
			L"앞 무음의 목표 초",
			L"开头静音的目标秒数",
			L"Target leading silence seconds",
			L"Целевые секунды начальной тишины",
			L"Ziel-Sekunden der Anfangsstille",
			L"Segundos alvo de silencio inicial",
			L"Doel-seconden beginstilte",
			L"Docelowe sekundy ciszy poczatkowej",
			L"Bastaki sessizlik hedef sn"));
		addTip(m_mixN, LL14(
			L"同時に重ねる曲数（2〜選択数）",
			L"Number of tracks mixed at once (2 to selection count)",
			L"Nombre de pistes superposees (2 a la selection)",
			L"Tracce sovrapposte (da 2 al numero selezionato)",
			L"Pistas simultaneas (2 al numero seleccionado)",
			L"동시에 겹칠 곡 수(2~선택 수)",
			L"同时叠加曲数（2至选择数）",
			L"Concurrent tracks (2 to selection count)",
			L"Число одновременных треков (2…число выбранных)",
			L"Gleichzeitige Titel (2 bis Auswahlanzahl)",
			L"Faixas simultaneas (2 ate a selecao)",
			L"Gelijktijdige nummers (2 tot selectie)",
			L"Jednoczesne utwory (2 do liczby wybranych)",
			L"Eszamanli parca (2..secim sayisi)"));
		// ボタン
		addTip(m_browse, LL14(
			L"出力ファイルまたはフォルダを選びます",
			L"Choose the output file or folder",
			L"Choisir le fichier ou dossier de sortie",
			L"Scegli file o cartella di output",
			L"Elegir archivo o carpeta de salida",
			L"출력 파일 또는 폴더를 선택합니다",
			L"选择输出文件或文件夹",
			L"Choose output file or folder",
			L"Выбрать выходной файл или папку",
			L"Ausgabedatei oder -ordner waehlen",
			L"Escolher arquivo ou pasta de saida",
			L"Kies uitvoerbestand of map",
			L"Wybierz plik lub folder wyjsciowy",
			L"Cikti dosyasi veya klasoru sec"));
		addTip(m_coverClear, LL14(
			L"指定したジャケット画像を解除します",
			L"Clear the selected cover image",
			L"Effacer l'image de pochette",
			L"Rimuovi l'immagine di copertina",
			L"Quitar la imagen de portada",
			L"지정한 재킷 이미지를 해제합니다",
			L"清除已指定的封面图",
			L"Clear the cover image",
			L"Сбросить изображение обложки",
			L"Cover-Bild entfernen",
			L"Limpar a imagem de capa",
			L"Omslag wissen",
			L"Wyczysc obraz okladki",
			L"Secilen kapak resmini kaldirir"));
		addTip(m_exec, LL14(
			L"現在の設定で書き出しを開始します",
			L"Start export with the current settings",
			L"Demarrer l'export avec les reglages actuels",
			L"Avvia l'esportazione con le impostazioni attuali",
			L"Iniciar la exportacion con la configuracion actual",
			L"현재 설정으로 내보내기를 시작합니다",
			L"按当前设置开始导出",
			L"Start export with current settings",
			L"Начать экспорт с текущими настройками",
			L"Export mit aktuellen Einstellungen starten",
			L"Iniciar exportacao com as configuracoes atuais",
			L"Start export met huidige instellingen",
			L"Rozpocznij eksport z biezacymi ustawieniami",
			L"Gecerli ayarlarla disa aktarimi baslatir"));
		addTip(m_close, LL14(
			L"書き出さずにダイアログを閉じます",
			L"Close the dialog without exporting",
			L"Fermer sans exporter",
			L"Chiudi senza esportare",
			L"Cerrar sin exportar",
			L"내보내지 않고 대화상자를 닫습니다",
			L"不导出并关闭对话框",
			L"Close without exporting",
			L"Закрыть без экспорта",
			L"Schliessen ohne Export",
			L"Fechar sem exportar",
			L"Sluiten zonder exporteren",
			L"Zamknij bez eksportu",
			L"Disa aktarmadan kapatir"));

		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 10000);
	}
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;
}

void CTranscodeExport::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CTranscodeExport::ShowHelpSheet()
{
	if (g_tcHelpDlg && ::IsWindow(g_tcHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_tcHelpDlg, this);
		return;
	}
	if (g_tcHelpDlg && !::IsWindow(g_tcHelpDlg->GetSafeHwnd()))
		g_tcHelpDlg = nullptr;
	CTcHelpDlg* dlg = new CTcHelpDlg(this);
	if (!dlg->Create(IDD_TC_HELP, this)) {
		delete dlg;
		return;
	}
	g_tcHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CTranscodeExport::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CTranscodeExport::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CTranscodeExport::OnDestroy()
{
	if (g_tcHelpDlg && ::IsWindow(g_tcHelpDlg->GetSafeHwnd()))
		g_tcHelpDlg->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}

void CTranscodeExport::OnBnClickedCoverClear()
{
	ExportTagUi_ClearCover(m_coverPic, m_cover, m_coverPath, m_coverBmp);
}
BOOL CTranscodeExport::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CTranscodeExport::OnDropFiles(HDROP hDropInfo)
{
	ExportTagUi_OnDropFiles(hDropInfo, m_coverPic, m_cover, m_coverPath, m_coverBmp);
}

void CTranscodeExport::ExportProgressThunk(int percent, LPCTSTR status, void* user)
{
	CTranscodeExport* self = reinterpret_cast<CTranscodeExport*>(user);
	if (!self || !::IsWindow(self->GetSafeHwnd())) return;
	if (self->m_progress.GetSafeHwnd()) {
		self->m_progress.SetPos(percent);
		// Aero透過だと親再描画待ちでバーが進まない。強制不透明描画。
		self->m_progress.SetAeroMode(FALSE);
		self->m_progress.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE);
	}
	if (status && status[0])
		self->m_status.SetWindowText(status);
	// エンコード中は DoEvent 全通しすると危険なので、自ダイアログ配下の描画だけ通す
	MSG msg;
	HWND hDlg = self->GetSafeHwnd();
	while (::PeekMessage(&msg, hDlg, WM_PAINT, WM_PAINT, PM_REMOVE))
		::DispatchMessage(&msg);
	if (self->m_progress.GetSafeHwnd()) {
		while (::PeekMessage(&msg, self->m_progress.GetSafeHwnd(), WM_PAINT, WM_PAINT, PM_REMOVE))
			::DispatchMessage(&msg);
	}
	if (self->m_status.GetSafeHwnd()) {
		while (::PeekMessage(&msg, self->m_status.GetSafeHwnd(), WM_PAINT, WM_PAINT, PM_REMOVE))
			::DispatchMessage(&msg);
	}
	::SetCursor(::LoadCursor(NULL, IDC_ARROW));
}

void CTranscodeExport::OnCbnSelchangeFormat()
{
	RefreshQualityLabels();
	if (multiFile && !IsXfadeMode() && !IsMixMode()) return;
	CString path;
	m_path.GetWindowText(path);
	const int fmt = CurrentFormat();
	const int dot = path.ReverseFind(L'.');
	if (dot >= 0)
		path = path.Left(dot);
	m_path.SetWindowText(NormalizeOutPath(path + ExtForFormat(fmt), fmt));
}

void CTranscodeExport::OnTcnSelchangeTabs(NMHDR*, LRESULT* pResult)
{
	ApplyTabUi();
	if (pResult) *pResult = 0;
}

// タブクリック直後の保険（reflect 取りこぼし対策）
BOOL CTranscodeExport::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	NMHDR* pNm = reinterpret_cast<NMHDR*>(lParam);
	if (pNm && pNm->idFrom == IDC_TC_TABS &&
		(pNm->code == TCN_SELCHANGE || pNm->code == TCN_SELCHANGING)) {
		if (pNm->code == TCN_SELCHANGE)
			ApplyTabUi();
	}
	if (pNm && pNm->idFrom == IDC_TC_MIX_VOL && m_mixVol.GetSafeHwnd()) {
		if (pNm->code == NM_DBLCLK) {
			LPNMITEMACTIVATE pAct = reinterpret_cast<LPNMITEMACTIVATE>(lParam);
			if (pAct && pAct->iItem >= 0) {
				m_mixEditRow = pAct->iItem;
				m_mixVol.EditLabel(pAct->iItem);
				if (pResult) *pResult = 0;
				return TRUE;
			}
		}
		else if (pNm->code == LVN_ENDLABELEDIT) {
			NMLVDISPINFO* pDi = reinterpret_cast<NMLVDISPINFO*>(lParam);
			if (pDi && pDi->item.iItem >= 0 && pDi->item.pszText) {
				int v = _tstoi(pDi->item.pszText);
				if (v < 0) v = 0;
				if (v > 1000) v = 1000;
				if (pDi->item.iItem < m_mixPctCount)
					m_mixPct[pDi->item.iItem] = v;
				NormalizeMixPercents();
				RebuildMixVolList();
				if (pResult) *pResult = FALSE;
				return TRUE;
			}
		}
	}
	return CCustomBlurDialogBase::OnNotify(wParam, lParam, pResult);
}

void CTranscodeExport::OnBnClickedBrowse()
{
	CString path;
	m_path.GetWindowText(path);
	if (multiFile && !IsXfadeMode() && !IsMixMode()) {
		if (TcBrowseFolder(this, path))
			m_path.SetWindowText(path);
		return;
	}
	const int fmt = CurrentFormat();
	CString ext = ExtForFormat(fmt);
	ext = ext.Mid(1);
	CFileDialog fd(FALSE, ext, path, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, FilterForFormat(fmt));
	if (fd.DoModal() == IDOK)
		m_path.SetWindowText(fd.GetPathName());
}

void CTranscodeExport::OnBnClickedExec()
{
	CString loopStr, pathStr, fadeStr, trimStr, xfadeStr;
	m_loop.GetWindowText(loopStr);
	m_path.GetWindowText(pathStr);
	m_fadeSec.GetWindowText(fadeStr);
	m_trimSec.GetWindowText(trimStr);
	m_xfadeSec.GetWindowText(xfadeStr);
	int loopCount = _tstoi(loopStr);
	if (loopCount < 1) loopCount = 1;
	int fadeSec = _tstoi(fadeStr);
	if (fadeSec < 1) fadeSec = 15;
	int trimKeepSec = _tstoi(trimStr);
	if (trimKeepSec < 0) trimKeepSec = 1;
	int xfadeSec = _tstoi(xfadeStr);
	if (xfadeSec < 1) xfadeSec = 5;
	if (xfadeSec > 120) xfadeSec = 120;

	const int fmt = CurrentFormat();
	int mp3Kbps = 192;
	int flacLevel = 5;
	if (fmt == TC_FMT_FLAC) {
		flacLevel = m_quality.GetCurSel();
		if (flacLevel < 0) flacLevel = 5;
	}
	else if (fmt == TC_FMT_MP3) {
		static const int kbps[] = { 128, 160, 192, 224, 256, 320 };
		int qi = m_quality.GetCurSel();
		if (qi < 0 || qi > 5) qi = 2;
		mp3Kbps = kbps[qi];
	}

	const bool mix = IsMixMode();
	const bool xfadeChecked = IsXfadeMode();
	// ミックス優先。ミックスOFF時のみ順次クロスフェード連結。
	const bool xfade = !mix && xfadeChecked;
	WavExportOptions opts = {};
	opts.fadeEnable = m_fadeCheck.GetCheck() ? 1 : 0;
	opts.fadeSec = fadeSec;
	opts.trimLeadEnable = m_trimCheck.GetCheck() ? 1 : 0;
	opts.trimKeepSec = trimKeepSec;
	opts.applyPrompt = (m_promptCheck.GetSafeHwnd() && m_promptCheck.GetCheck()) ? 1 : 0;
	opts.kpiDurationSec = 0;
	if (SelectionHasKpi() && m_kpiSec.GetSafeHwnd()) {
		PersistKpiDurationFromUi();
		opts.kpiDurationSec = savedata.wav_export_kpi_sec;
	}
	opts.sampleRate = 0;
	if (m_srate.GetSafeHwnd()) {
		static const int rates[] = { 0, 44100, 48000, 96000, 192000 };
		int si = m_srate.GetCurSel();
		if (si < 0 || si > 4) si = 2;
		opts.sampleRate = rates[si];
		savedata.wav_export_sample_rate = opts.sampleRate;
	}
	savedata.wav_export_fade = opts.fadeEnable;
	savedata.wav_export_fade_sec = opts.fadeSec;
	savedata.wav_export_trim_lead = opts.trimLeadEnable;
	savedata.wav_export_trim_keep_sec = opts.trimKeepSec;
	savedata.wav_export_apply_prompt = opts.applyPrompt;
	savedata.wav_export_xfade = (multiFile && m_xfadeCheck.GetSafeHwnd() && m_xfadeCheck.GetCheck()) ? 1 : 0;
	savedata.wav_export_xfade_sec = xfadeSec;
	savedata.wav_export_mix = (multiFile && m_mixCheck.GetSafeHwnd() && m_mixCheck.GetCheck()) ? 1 : 0;
	{
		int mixN = 2;
		if (m_mixN.GetSafeHwnd() && m_mixN.GetCount() > 0) {
			mixN = m_mixN.GetCurSel() + 2;
			if (mixN < 2) mixN = 2;
		}
		savedata.wav_export_mix_n = mixN;
	}
	if (fmt == TC_FMT_MP3 || fmt == TC_FMT_FLAC)
		savedata.tc_format = fmt;
	savedata.tc_mp3_kbps = mp3Kbps;
	savedata.tc_flac_level = flacLevel;
	savedata.wav_export_copy_tags = m_copyTags.GetCheck() ? 1 : 0;
	// クロスフェード／ミックスは単体出力なのでタイトルも適用対象
	ExportTagUi_Collect(multiFile && !xfade && !mix, savedata.wav_export_copy_tags, m_title, m_artist, m_album, m_coverPath, opts);

	if (pathStr.IsEmpty()) {
		m_status.SetWindowText((multiFile && !xfade && !mix)
			? LL14(L"フォルダを指定してください", L"Please specify folder", L"Veuillez specifier le dossier", L"Specificare la cartella",
				L"Especifique la carpeta", L"폴더를 지정하세요", L"请指定文件夹", L"يرجى تحديد المجلد",
				L"Укажите папку", L"Bitte Ordner angeben", L"Especifique a pasta", L"Geef map op",
				L"Podaj folder", L"Klasor belirtin")
			: LL14(L"ファイル名を指定してください", L"Please specify file name", L"Veuillez specifier le nom du fichier",
				L"Specificare il nome del file", L"Especifique el nombre del archivo", L"파일 이름을 지정하세요", L"请指定文件名",
				L"يرجى تحديد اسم الملف", L"Укажите имя файла", L"Bitte Dateinamen angeben", L"Especifique o nome do arquivo",
				L"Geef bestandsnaam op", L"Podaj nazwę pliku", L"Dosya adini belirtin"));
		return;
	}

	// 再生中の状態が書き出しに混ざらないよう、先に停止する
	if (og) og->stop1();
	m_status.SetWindowText(LL14(L"出力中...", L"Exporting...", L"Export en cours...", L"Esportazione in corso...",
		L"Exportando...", L"내보내는 중...", L"导出中...", L"جاري التصدير...",
		L"Экспорт...", L"Exportiere...", L"Exportando...", L"Exporteren...",
		L"Eksportowanie...", L"Dışa aktarılıyor..."));
	m_exec.EnableWindow(FALSE);
	if (m_progress.GetSafeHwnd()) {
		m_progress.SetPos(0);
		m_progress.ShowWindow(SW_SHOW);
	}
	MpDecodeProgressReset();
	MpDecodeProgressSetCb(&CTranscodeExport::ExportProgressThunk, this);
	UpdateWindow();

	BOOL ok = TRUE;
	if (mix) {
		CString finalPath = NormalizeOutPath(pathStr, fmt);
		m_path.SetWindowText(finalPath);
		const int total = (int)pcs.size();
		int n = total;
		if (n > 64) n = 64;
		int K = savedata.wav_export_mix_n;
		if (m_mixN.GetSafeHwnd() && m_mixN.GetCount() > 0)
			K = m_mixN.GetCurSel() + 2;
		if (K < 2) K = 2;
		if (K > n) K = n;
		// リスト表示値を取り込み、選択曲数ぶんだけ合計100へ正規化（25,25→50,50）
		m_mixPctCount = n;
		for (int i = n; i < 64; ++i)
			m_mixPct[i] = 0;
		if (m_mixVol.GetSafeHwnd()) {
			const int rows = m_mixVol.GetItemCount();
			for (int i = 0; i < n && i < rows; ++i) {
				int v = _tstoi(m_mixVol.GetItemText(i, 0));
				if (v < 0) v = 0;
				m_mixPct[i] = v;
			}
		}
		NormalizeMixPercents();
		if (m_mixVol.GetSafeHwnd()) {
			for (int i = 0; i < n && i < m_mixVol.GetItemCount(); ++i) {
				CString pct;
				pct.Format(L"%d", m_mixPct[i]);
				m_mixVol.SetItemText(i, 0, pct);
			}
		}
		float gains[64];
		for (int i = 0; i < n; ++i)
			gains[i] = (float)m_mixPct[i]; // 相対重み（合成時に再生中合計で正規化）

		CString temps[64];
		WavExportOptions pieceOpts = opts;
		pieceOpts.copyTags = 0;
		pieceOpts.multiFile = 0;
		pieceOpts.fadeEnable = 0; // 末尾フェードは合成後に一括
		for (int i = 0; i < n && ok; ++i) {
			temps[i] = TcMakeTempWavPath();
			const int base = (int)((i * 70) / n);
			const int span = (int)(((i + 1) * 70) / n) - base;
			MpDecodeProgressSetSegment(base, span > 0 ? span : 1);
			MpDecodeProgressSetPcmCap(95);
			CString st;
			st.Format(LL14(L"ミックス用PCM出力中... (%d/%d)", L"Mix PCM export... (%d/%d)", L"Export PCM mix... (%d/%d)", L"Export PCM mix... (%d/%d)",
				L"Export PCM mix... (%d/%d)", L"믹스 PCM 출력 중... (%d/%d)", L"混音PCM导出中... (%d/%d)", L"Mix PCM export... (%d/%d)",
				L"PCM для микса... (%d/%d)", L"Mix-PCM Export... (%d/%d)", L"Export PCM mix... (%d/%d)", L"Mix-PCM-export... (%d/%d)",
				L"Eksport PCM mix... (%d/%d)", L"Mix PCM... (%d/%d)"),
				i + 1, n);
			m_status.SetWindowText(st);
			UpdateWindow();
			DoEvent();
			ok = og->ExportToWav(&pcs[i], temps[i], loopCount, &pieceOpts, false);
			if (ok && i == 0) {
				CFile f;
				if (f.Open(temps[i], CFile::modeRead | CFile::shareDenyWrite)) {
					TcWavInfo wi = {};
					if (TcReadWavInfo(f, wi) && wi.hz > 0 && wi.ch >= 1) {
						if (opts.sampleRate == 0)
							pieceOpts.sampleRate = (int)wi.hz;
						pieceOpts.forceChannels = (int)wi.ch;
						if (wi.bits == 16 || wi.bits == 24 || wi.bits == 32)
							pieceOpts.forceBits = (int)wi.bits;
					}
					f.Close();
				}
			}
		}
		CString mixWav = (fmt == TC_FMT_WAV) ? finalPath : TcMakeTempWavPath();
		if (ok) {
			m_status.SetWindowText(LL14(L"同時ミックス合成中...", L"Mixing concurrent tracks...", L"Mixage simultane...", L"Mix simultaneo...",
				L"Mezcla simultanea...", L"동시 믹스 중...", L"同时混音中...", L"Mixing...",
				L"Сведение микса...", L"Gleichzeitiges Mischen...", L"Misturando...", L"Mixen...",
				L"Mieszanie...", L"Karistiriliyor..."));
			UpdateWindow();
			MpDecodeProgressReport(75, NULL);
			// クロスフェードONなら補充投入に秒数を使う。OFFなら即時投入。
			const int mixXfadeSec = xfadeChecked ? xfadeSec : 0;
			ok = TcConcurrentMixWav(mixWav, temps, gains, n, K, mixXfadeSec);
		}
		for (int i = 0; i < n; ++i) {
			if (!temps[i].IsEmpty())
				DeleteFile(temps[i]);
		}
		if (ok && opts.fadeEnable)
			ok = TcApplyTailFadeOutWav(mixWav, opts.fadeSec);
		if (ok && fmt != TC_FMT_WAV) {
			MpDecodeProgressReport(85, LL14(
				L"エンコード中…", L"Encoding...", L"Encodage...", L"Codifica...", L"Codificando...",
				L"인코딩 중…", L"编码中…", L"Encoding...", L"Кодирование...", L"Kodiere...",
				L"Codificando...", L"Coderen...", L"Kodowanie...", L"Kodlaniyor..."));
			if (fmt == TC_FMT_FLAC)
				ok = EncodeWavToFlac(mixWav, finalPath, flacLevel);
			else
				ok = EncodeWavToMp3(mixWav, finalPath, mp3Kbps);
			DeleteFile(mixWav);
		}
		if (ok && pcs[0].fol[0] != 0) {
			FileTagFields fill;
			fill.title = opts.tagTitle;
			fill.artist = opts.tagArtist;
			fill.album = opts.tagAlbum;
			const int copyTags = opts.copyTags ? 1 : 0;
			const bool needMeta = copyTags
				|| !fill.title.IsEmpty() || !fill.artist.IsEmpty() || !fill.album.IsEmpty()
				|| !opts.coverImagePath.IsEmpty();
			if (needMeta)
				ApplyExportTagsAndCover(pcs[0].fol, finalPath, copyTags, &fill, opts.coverImagePath);
		}
	}
	else if (xfade) {
		CString finalPath = NormalizeOutPath(pathStr, fmt);
		m_path.SetWindowText(finalPath);
		CString accumWav = (fmt == TC_FMT_WAV) ? finalPath : TcMakeTempWavPath();
		const size_t total = pcs.size();
		WavExportOptions pieceOpts = opts;
		pieceOpts.copyTags = 0;
		pieceOpts.multiFile = 0;
		for (size_t i = 0; i < total && ok; ++i) {
			const bool last = (i + 1 == total);
			pieceOpts.fadeEnable = (last && opts.fadeEnable) ? 1 : 0;
			CString piecePath;
			if (i == 0)
				piecePath = accumWav;
			else
				piecePath = TcMakeTempWavPath();
			const int base = (int)((i * 100) / total);
			const int span = (int)(((i + 1) * 100) / total) - base;
			MpDecodeProgressSetSegment(base, span > 0 ? span : 1);
			if (fmt != TC_FMT_WAV)
				MpDecodeProgressSetPcmCap(78);
			else
				MpDecodeProgressSetPcmCap(95);
			CString st;
			st.Format(LL14(L"クロスフェード出力中... (%d/%d)", L"Crossfade export... (%d/%d)", L"Export fondu... (%d/%d)", L"Export crossfade... (%d/%d)",
				L"Export fundido... (%d/%d)", L"크로스페이드 출력 중... (%d/%d)", L"交叉淡入淡出导出中... (%d/%d)", L"Crossfade export... (%d/%d)",
				L"Кроссфейд... (%d/%d)", L"Ueberblend-Export... (%d/%d)", L"Export crossfade... (%d/%d)", L"Crossfade-export... (%d/%d)",
				L"Eksport przejścia... (%d/%d)", L"Capraz solma... (%d/%d)"),
				(int)(i + 1), (int)total);
			m_status.SetWindowText(st);
			UpdateWindow();
			DoEvent();
			ok = og->ExportToWav(&pcs[i], piecePath, loopCount, &pieceOpts, false);
			// 2曲目以降を先頭WAVのHz/ch/bitへ揃える（ソースのままでも合成可能に）
			if (ok && i == 0) {
				CFile f;
				if (f.Open(piecePath, CFile::modeRead | CFile::shareDenyWrite)) {
					TcWavInfo wi = {};
					if (TcReadWavInfo(f, wi) && wi.hz > 0 && wi.ch >= 1) {
						if (opts.sampleRate == 0)
							pieceOpts.sampleRate = (int)wi.hz;
						pieceOpts.forceChannels = (int)wi.ch;
						if (wi.bits == 16 || wi.bits == 24 || wi.bits == 32)
							pieceOpts.forceBits = (int)wi.bits;
					}
					f.Close();
				}
			}
			if (ok && i > 0) {
				m_status.SetWindowText(LL14(L"クロスフェード合成中...", L"Mixing crossfade...", L"Mixage fondu...", L"Mix crossfade...",
					L"Mezclando fundido...", L"크로스페이드 합성 중...", L"交叉淡入淡出合成中...", L"Mixing crossfade...",
					L"Сведение кроссфейда...", L"Ueberblendung mischen...", L"Misturando crossfade...", L"Crossfade mixen...",
					L"Mieszanie przejścia...", L"Capraz solma karistiriliyor..."));
				UpdateWindow();
				ok = TcAppendCrossfadeWav(accumWav, piecePath, xfadeSec);
				DeleteFile(piecePath);
			}
		}
		if (ok && fmt != TC_FMT_WAV) {
			MpDecodeProgressReport(80, LL14(
				L"エンコード中…", L"Encoding...", L"Encodage...", L"Codifica...", L"Codificando...",
				L"인코딩 중…", L"编码中…", L"Encoding...", L"Кодирование...", L"Kodiere...",
				L"Codificando...", L"Coderen...", L"Kodowanie...", L"Kodlaniyor..."));
			if (fmt == TC_FMT_FLAC)
				ok = EncodeWavToFlac(accumWav, finalPath, flacLevel);
			else
				ok = EncodeWavToMp3(accumWav, finalPath, mp3Kbps);
		}
		if (fmt != TC_FMT_WAV)
			DeleteFile(accumWav);
		if (ok && pcs[0].fol[0] != 0) {
			FileTagFields fill;
			fill.title = opts.tagTitle;
			fill.artist = opts.tagArtist;
			fill.album = opts.tagAlbum;
			const int copyTags = opts.copyTags ? 1 : 0;
			const bool needMeta = copyTags
				|| !fill.title.IsEmpty() || !fill.artist.IsEmpty() || !fill.album.IsEmpty()
				|| !opts.coverImagePath.IsEmpty();
			if (needMeta)
				ApplyExportTagsAndCover(pcs[0].fol, finalPath, copyTags, &fill, opts.coverImagePath);
		}
	}
	else if (multiFile) {
		CString folder = pathStr;
		CString lower = folder;
		lower.MakeLower();
		if (lower.Right(4) == L".mp3" || lower.Right(5) == L".flac" || lower.Right(4) == L".wav") {
			int pos = folder.ReverseFind(L'\\');
			if (pos >= 0) folder = folder.Left(pos + 1);
		}
		if (!folder.IsEmpty() && folder[folder.GetLength() - 1] != L'\\')
			folder += L'\\';
		const size_t total = pcs.size();
		for (size_t i = 0; i < total; ++i) {
			CString outPath = OutputPathForItem(folder, pcs[i], fmt);
			const int base = (int)((i * 100) / total);
			const int span = (int)(((i + 1) * 100) / total) - base;
			MpDecodeProgressSetSegment(base, span > 0 ? span : 1);
			CString st;
			st.Format(LL14(L"出力中... (%d/%d)", L"Exporting... (%d/%d)", L"Export en cours... (%d/%d)", L"Esportazione... (%d/%d)",
				L"Exportando... (%d/%d)", L"내보내는 중... (%d/%d)", L"导出中... (%d/%d)", L"جاري التصدير... (%d/%d)",
				L"Экспорт... (%d/%d)", L"Exportiere... (%d/%d)", L"Exportando... (%d/%d)", L"Exporteren... (%d/%d)",
				L"Eksportowanie... (%d/%d)", L"Dışa aktarılıyor... (%d/%d)"),
				(int)(i + 1), (int)total);
			m_status.SetWindowText(st);
			UpdateWindow();
			DoEvent();
			if (fmt == TC_FMT_WAV)
				ok = og->ExportToWav(&pcs[i], outPath, loopCount, &opts, true) && ok;
			else
				ok = og->ExportToTranscode(&pcs[i], outPath, loopCount, &opts, fmt, mp3Kbps, flacLevel) && ok;
		}
	}
	else {
		CString path = NormalizeOutPath(pathStr, fmt);
		m_path.SetWindowText(path);
		MpDecodeProgressSetSegment(0, 100);
		if (fmt == TC_FMT_WAV)
			ok = og->ExportToWav(&pc, path, loopCount, &opts, true);
		else
			ok = og->ExportToTranscode(&pc, path, loopCount, &opts, fmt, mp3Kbps, flacLevel);
	}

	if (ok && m_progress.GetSafeHwnd())
		m_progress.SetPos(100);
	MpDecodeProgressClearCb();
	m_exec.EnableWindow(TRUE);
	const CString title = LL14(L"音声書き出し", L"Audio export", L"Export audio", L"Esporta audio",
		L"Exportar audio", L"오디오 내보내기", L"音频导出", L"تصدير الصوت",
		L"Экспорт аудио", L"Audio exportieren", L"Exportar audio", L"Audio exporteren",
		L"Eksport audio", L"Ses disa aktar");
	if (ok) {
		CString msg = LL14(L"完了", L"Complete", L"Termine", L"Completato",
			L"Completado", L"완료", L"完成", L"اكتمل",
			L"Завершено", L"Abgeschlossen", L"Concluido", L"Voltooid",
			L"Zakończono", L"Tamamlandı");
		m_status.SetWindowText(msg);
		MessageBox(msg, title, MB_OK | MB_ICONINFORMATION);
	}
	else {
		CString msg = LL14(L"エラー", L"Error", L"Erreur", L"Errore",
			L"Error", L"오류", L"错误", L"خطأ",
			L"Ошибка", L"Fehler", L"Erro", L"Fout",
			L"Błąd", L"Hata");
		m_status.SetWindowText(msg);
		MessageBox(msg, title, MB_OK | MB_ICONERROR);
	}
}

void CTranscodeExport::OnBnClickedClose()
{
	PersistKpiDurationFromUi();
	EndDialog(IDCANCEL);
}
