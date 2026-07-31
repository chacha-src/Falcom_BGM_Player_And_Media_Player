// TranscodeExport.cpp
// WAV書き出し結果を mp3 / FLAC に変換するUIとエンコード。

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "TranscodeExport.h"
#include "DecodeProgress.h"
#include "ExportTagUi.h"
#include <ShlObj.h>
#include <vector>
#include <algorithm>

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
	CString path;
	path.Format(L"%sogg_tc_%u_%u.wav", dir, GetCurrentProcessId(), GetTickCount());
	return path;
}

} // namespace

BOOL EncodeWavToFlac(const CString& wavPath, const CString& outPath, int compressionLevel)
{
	if (wavPath.IsEmpty() || outPath.IsEmpty()) return FALSE;
	if (compressionLevel < 0) compressionLevel = 0;
	if (compressionLevel > 8) compressionLevel = 8;

	CFile f;
	if (!f.Open(wavPath, CFile::modeRead | CFile::shareDenyWrite))
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

	CFile f;
	if (!f.Open(wavPath, CFile::modeRead | CFile::shareDenyWrite))
		return FALSE;
	TcWavInfo info = {};
	if (!TcReadWavInfo(f, info) || info.ch < 1 || info.ch > 2)
		return FALSE;

	HRESULT hr = MFStartup(MF_VERSION);
	if (FAILED(hr))
		return FALSE;

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
	if (!ok)
		DeleteFile(outPath);
	return ok;
}

IMPLEMENT_DYNAMIC(CTranscodeExport, CCustomBlurDialogBase)

CTranscodeExport::CTranscodeExport(CWnd* pParent)
	: CCustomBlurDialogBase(CTranscodeExport::IDD, pParent)
	, multiFile(false)
	, m_initialTab(-1)
	, m_coverBmp(NULL)
{
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
	DDX_Control(pDX, IDC_TC_FORMAT, m_format);
	DDX_Control(pDX, IDC_TC_FORMAT_L, m_formatLabel);
	DDX_Control(pDX, IDC_TC_QUALITY, m_quality);
	DDX_Control(pDX, IDC_TC_QUALITY_L, m_qualityLabel);
	DDX_Control(pDX, IDC_TC_SRATE, m_srate);
	DDX_Control(pDX, IDC_TC_SRATE_L, m_srateLabel);
	DDX_Control(pDX, IDC_TC_LOOP, m_loop);
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
	DDX_Control(pDX, IDC_TC_TRIM, m_trimCheck);
	DDX_Control(pDX, IDC_TC_TRIM_SEC, m_trimSec);
	DDX_Control(pDX, IDC_TC_TRIM_L, m_trimLabel);
	DDX_Control(pDX, IDC_TC_COPY_TAGS, m_copyTags);
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
	ON_BN_CLICKED(IDC_TC_EXEC, &CTranscodeExport::OnBnClickedExec)
	ON_BN_CLICKED(IDC_TC_BROWSE, &CTranscodeExport::OnBnClickedBrowse)
	ON_BN_CLICKED(IDC_TC_CLOSE, &CTranscodeExport::OnBnClickedClose)
	ON_BN_CLICKED(IDC_TC_COVER_CLEAR, &CTranscodeExport::OnBnClickedCoverClear)
	ON_CBN_SELCHANGE(IDC_TC_FORMAT, &CTranscodeExport::OnCbnSelchangeFormat)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TC_TABS, &CTranscodeExport::OnTcnSelchangeTabs)
	ON_WM_DROPFILES()
END_MESSAGE_MAP()

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
	if (!multiFile) {
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

	// 形式コンボを隠し、上部に横置きタブ帯。品質コンボは繰返し行へ寄せる
	m_formatLabel.ShowWindow(SW_HIDE);
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
		// 見出し帯だけの高さ（ページ空き枠を出さない）
		CRect rTabs(7, 4, tabRight, 4 + 32);
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

	m_loopLabel.SetWindowText(LL14(L"繰返し回数", L"Loop count", L"Nombre de boucles", L"Conteggio loop",
		L"Repeticiones", L"반복 횟수", L"循环次数", L"عدد التكرار",
		L"Количество повторов", L"Schleifenzahl", L"Repetições", L"Aantal herhalingen",
		L"Liczba powtórzeń", L"Döngü sayısı"));
	if (multiFile) {
		m_pathLabel.SetWindowText(LL14(L"出力フォルダ", L"Output folder", L"Dossier de sortie", L"Cartella di output",
			L"Carpeta de salida", L"출력 폴더", L"输出文件夹", L"مجلد الإخراج",
			L"Папка вывода", L"Ausgabeordner", L"Pasta de saída", L"Uitvoermap",
			L"Folder wyjściowy", L"Çıktı klasörü"));
	}
	else {
		m_pathLabel.SetWindowText(LL14(L"出力ファイル名", L"Output file", L"Fichier de sortie", L"File di output",
			L"Archivo de salida", L"출력 파일", L"输出文件名", L"اسم الملف",
			L"Выходной файл", L"Ausgabedatei", L"Arquivo de saída", L"Uitvoerbestand",
			L"Plik wyjściowy", L"Çıktı dosyası"));
	}
	m_fadeCheck.SetWindowText(LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza",
		L"Fundido", L"페이드 아웃", L"淡出", L"تلاشي",
		L"Затухание", L"Ausblenden", L"Fade out", L"Fade-out",
		L"Wyciszanie", L"Solma"));
	m_fadeLabel.SetWindowText(LL14(L"秒", L"sec", L"sec", L"sec",
		L"seg", L"초", L"秒", L"ث",
		L"сек", L"Sek", L"seg", L"sec",
		L"sek", L"sn"));
	m_trimCheck.SetWindowText(LL14(L"先頭無音カット", L"Trim leading silence", L"Couper silence initial", L"Taglia silenzio iniziale",
		L"Cortar silencio inicial", L"앞 무음 제거", L"切除开头静音", L"قص الصمت الابتدائي",
		L"Обрезать нач. тишину", L"Stille am Anfang kürzen", L"Cortar silêncio inicial", L"Stilte begin trimmen",
		L"Przytnij ciszę na początku", L"Baştaki sessizliği kes"));
	m_trimLabel.SetWindowText(LL14(L"保持秒", L"Keep sec", L"Garder sec", L"Mantieni sec",
		L"Mantener seg", L"유지 초", L"保留秒", L"احتفظ ث",
		L"Оставить сек", L"Behalten Sek", L"Manter seg", L"Bewaar sec",
		L"Zostaw sek", L"Tut sn"));
	m_copyTags.SetWindowText(LL14(L"タグとジャケットをコピー", L"Copy tags and cover art", L"Copier les tags et la pochette", L"Copia tag e copertina",
		L"Copiar etiquetas y portada", L"태그와 재킷 복사", L"复制标签和封面", L"نسخ الوسوم والغلاف",
		L"Копировать теги и обложку", L"Tags und Cover kopieren", L"Copiar tags e capa", L"Tags en hoes kopiëren",
		L"Kopiuj tagi i okładkę", L"Etiketleri ve kapağı kopyala"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi",
		L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten",
		L"Zamknij", L"Kapat"));
	m_exec.SetWindowText(LL14(L"実行", L"Execute", L"Exécuter", L"Esegui",
		L"Ejecutar", L"실행", L"执行", L"تنفيذ",
		L"Выполнить", L"Ausführen", L"Executar", L"Uitvoeren",
		L"Wykonaj", L"Çalıştır"));

	m_loop.SetWindowText(L"1");
	// KPI秒数はサンプリング行の左側へ（繰返し行の圧縮レベルと重ねない）
	{
		CRect rLoop, rLoopL, rSrateL, rSrate;
		m_loop.GetWindowRect(&rLoop); ScreenToClient(&rLoop);
		m_loopLabel.GetWindowRect(&rLoopL); ScreenToClient(&rLoopL);
		m_srateLabel.GetWindowRect(&rSrateL); ScreenToClient(&rSrateL);
		m_srate.GetWindowRect(&rSrate); ScreenToClient(&rSrate);
		const int labW = 58;
		const int edW = 72;
		const int top = rSrate.top;
		const int labTop = rSrateL.top;
		CRect rLab(rLoopL.left, labTop, rLoopL.left + labW, labTop + (std::max)(14, rLoopL.Height()));
		CRect rEd(rLoop.left, top, rLoop.left + edW, top + (std::max)(18, rLoop.Height()));
		// サンプリングラベルと被るなら編集を左に収める
		if (rEd.right > rSrateL.left - 8) {
			rEd.right = rSrateL.left - 8;
			rEd.left = (std::max)(rLoop.left, rEd.right - edW);
			rLab.right = rEd.left - 4;
			rLab.left = (std::max)(rLoopL.left, rLab.right - labW);
		}
		m_kpiSecLabel.Create(L"", WS_CHILD | SS_LEFT, rLab, this, IDC_TC_KPI_SEC_L);
		m_kpiSec.Create(WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER, rEd, this, IDC_TC_KPI_SEC);
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
	CString s;
	s.Format(L"%d", fadeSec);
	m_fadeSec.SetWindowText(s);
	s.Format(L"%d", trimKeep);
	m_trimSec.SetWindowText(s);
	m_fadeCheck.SetCheck(savedata.wav_export_fade ? BST_CHECKED : BST_UNCHECKED);
	m_trimCheck.SetCheck(savedata.wav_export_trim_lead ? BST_CHECKED : BST_UNCHECKED);
	m_copyTags.SetCheck(savedata.wav_export_copy_tags ? BST_CHECKED : BST_UNCHECKED);

	if (multiFile)
		m_path.SetWindowText(TcDefaultFolderFromPc(pc));
	else
		m_path.SetWindowText(OutputPathForItem(TcDefaultFolderFromPc(pc), pc, CurrentFormat()));
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
	ExportTagUi_InitFields(multiFile, pc, m_title, m_artist, m_album,
		m_titleL, m_artistL, m_albumL, m_coverL, m_coverPic, m_cover, m_coverClear, m_coverPath, m_coverBmp);
	DragAcceptFiles(TRUE);

	// パス行を下げたあと下段が食い込む／余白が空きすぎるのを整列し、ダイアログ高さを詰める
	{
		auto placeY = [this](CWnd& w, int y) {
			if (!w.GetSafeHwnd()) return;
			CRect r; w.GetWindowRect(&r); ScreenToClient(&r);
			w.SetWindowPos(NULL, r.left, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		};
		auto placeXY = [this](CWnd& w, int x, int y) {
			if (!w.GetSafeHwnd()) return;
			w.SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
		};
		auto heightOf = [this](CWnd& w) -> int {
			if (!w.GetSafeHwnd()) return 18;
			CRect r; w.GetWindowRect(&r); return r.Height();
		};

		// 繰返し行: 左=繰返し、右=圧縮/ビットレート（長さ秒は置かない）
		CRect rLoop, rLoopL, rQ, rQL;
		m_loop.GetWindowRect(&rLoop); ScreenToClient(&rLoop);
		m_loopLabel.GetWindowRect(&rLoopL); ScreenToClient(&rLoopL);
		m_quality.GetWindowRect(&rQ); ScreenToClient(&rQ);
		m_qualityLabel.GetWindowRect(&rQL); ScreenToClient(&rQL);
		placeXY(m_qualityLabel, rQL.left, rLoopL.top);
		placeXY(m_quality, rQ.left, rLoop.top);

		// サンプリング行: 左=長さ(秒)、右=サンプリング
		const int srateY = rLoop.bottom + 6;
		const int labW = 58;
		const int edW = 72;
		placeXY(m_srateLabel, rQL.left, srateY + 2);
		placeXY(m_srate, rQ.left, srateY);
		if (m_kpiSec.GetSafeHwnd()) {
			placeXY(m_kpiSecLabel, rLoopL.left, srateY + 2);
			placeXY(m_kpiSec, rLoop.left, srateY);
			// サンプリングと被る場合は幅を落とす
			CRect rSrateL; m_srateLabel.GetWindowRect(&rSrateL); ScreenToClient(&rSrateL);
			CRect rEd; m_kpiSec.GetWindowRect(&rEd); ScreenToClient(&rEd);
			if (rEd.right > rSrateL.left - 8) {
				const int newRight = rSrateL.left - 8;
				const int newLeft = (std::max)((int)rLoop.left, newRight - edW);
				m_kpiSec.SetWindowPos(NULL, newLeft, srateY, (std::max)(48, newRight - newLeft), rEd.Height(), SWP_NOZORDER);
				m_kpiSecLabel.SetWindowPos(NULL, rLoopL.left, srateY + 2, labW, heightOf(m_kpiSecLabel), SWP_NOZORDER);
			}
		}

		CRect rSrate;
		m_srate.GetWindowRect(&rSrate); ScreenToClient(&rSrate);
		int yPath = (std::max)(srateY + heightOf(m_srate), (int)rSrate.bottom) + 8;
		CRect rPathL, rPath, rBr;
		m_pathLabel.GetWindowRect(&rPathL); ScreenToClient(&rPathL);
		m_path.GetWindowRect(&rPath); ScreenToClient(&rPath);
		m_browse.GetWindowRect(&rBr); ScreenToClient(&rBr);
		placeXY(m_pathLabel, rPathL.left, yPath);
		placeXY(m_path, rPath.left, yPath + (rPathL.Height() + 2));
		placeXY(m_browse, rBr.left, yPath + (rPathL.Height() + 1));
		m_path.GetWindowRect(&rPath); ScreenToClient(&rPath);

		int y = rPath.bottom + 14; // 出力パスとフェードの間隔
		const int fadeY = y;
		placeY(m_fadeCheck, fadeY);
		placeY(m_fadeSec, fadeY - 2);
		placeY(m_fadeLabel, fadeY + 1);
		y = fadeY + (std::max)(heightOf(m_fadeCheck), heightOf(m_fadeSec)) + 8;
		const int trimY = y;
		placeY(m_trimCheck, trimY);
		placeY(m_trimSec, trimY - 2);
		placeY(m_trimLabel, trimY + 1);
		placeY(m_copyTags, trimY);
		y = trimY + (std::max)(heightOf(m_trimCheck), heightOf(m_trimSec)) + 10;
		const int titleY = y;
		placeY(m_titleL, titleY + 2);
		placeY(m_title, titleY);
		y = titleY + heightOf(m_title) + 6;
		placeY(m_artistL, y + 2);
		placeY(m_artist, y);
		y += heightOf(m_artist) + 6;
		placeY(m_albumL, y + 2);
		placeY(m_album, y);
		y += heightOf(m_album) + 8;
		const int coverY = y;
		placeY(m_coverL, coverY + 2);
		placeY(m_coverPic, coverY);
		placeY(m_cover, coverY + 8);
		placeY(m_coverClear, coverY + 18);
		y = coverY + (std::max)(heightOf(m_coverPic), 56) + 10;
		const int btnY = y;
		placeY(m_exec, btnY);
		placeY(m_close, btnY);
		y = btnY + heightOf(m_exec) + 8;
		if (CWnd* pProgL = GetDlgItem(IDC_TC_PROG_L))
			placeY(*pProgL, y);
		y += 14;
		if (m_progress.GetSafeHwnd())
			placeY(m_progress, y);
		else if (CWnd* pPh = GetDlgItem(IDC_TC_PROGRESS))
			placeY(*pPh, y);
		y += (std::max)(heightOf(m_progress), 16) + 6;
		placeY(m_status, y);
		y += heightOf(m_status) + 8;

		CRect rcClient; GetClientRect(&rcClient);
		CRect rcWin; GetWindowRect(&rcWin);
		const int chrome = rcWin.Height() - rcClient.Height();
		if (y + chrome > 120)
			SetWindowPos(NULL, 0, 0, rcWin.Width(), y + chrome, SWP_NOMOVE | SWP_NOZORDER);
	}
	return TRUE;
}

void CTranscodeExport::OnBnClickedCoverClear()
{
	ExportTagUi_ClearCover(m_coverPic, m_cover, m_coverPath, m_coverBmp);
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
	if (multiFile) return;
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
	return CCustomBlurDialogBase::OnNotify(wParam, lParam, pResult);
}

void CTranscodeExport::OnBnClickedBrowse()
{
	CString path;
	m_path.GetWindowText(path);
	if (multiFile) {
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
	CString loopStr, pathStr, fadeStr, trimStr;
	m_loop.GetWindowText(loopStr);
	m_path.GetWindowText(pathStr);
	m_fadeSec.GetWindowText(fadeStr);
	m_trimSec.GetWindowText(trimStr);
	int loopCount = _tstoi(loopStr);
	if (loopCount < 1) loopCount = 1;
	int fadeSec = _tstoi(fadeStr);
	if (fadeSec < 1) fadeSec = 15;
	int trimKeepSec = _tstoi(trimStr);
	if (trimKeepSec < 0) trimKeepSec = 1;

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

	WavExportOptions opts = {};
	opts.fadeEnable = m_fadeCheck.GetCheck() ? 1 : 0;
	opts.fadeSec = fadeSec;
	opts.trimLeadEnable = m_trimCheck.GetCheck() ? 1 : 0;
	opts.trimKeepSec = trimKeepSec;
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
	if (fmt == TC_FMT_MP3 || fmt == TC_FMT_FLAC)
		savedata.tc_format = fmt;
	savedata.tc_mp3_kbps = mp3Kbps;
	savedata.tc_flac_level = flacLevel;
	savedata.wav_export_copy_tags = m_copyTags.GetCheck() ? 1 : 0;
	ExportTagUi_Collect(multiFile, savedata.wav_export_copy_tags, m_title, m_artist, m_album, m_coverPath, opts);

	if (pathStr.IsEmpty()) {
		m_status.SetWindowText(multiFile
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
	if (multiFile) {
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
