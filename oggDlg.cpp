// oggDlg.cpp : インプリメンテーション ファイル
//
//#define _DLL
#include "stdafx.h"
int flacmode = 0;
#include "kmp_pi.h"
#include "kpi_decoder.h"
//#include "afxdll_.h"
#include "Dwmapi.h"
//#include "Shobjidl.h"
//#include <shlobj.h> 
//const GUID  IID_ITaskbarList3 = { 0xea1afb91,0x9e28,0x4b86,{0x90,0xe9,0x9e,0x9f,0x8a,0x5e,0xef,0xaf}} ;
//const GUID  IID_ICustomDestinationList = {0x6332debf,0x87b5,0x4670,{0x90,0xc0,0x5e,0x57,0xb4,0x08,0xa4,0x9e}};
//const CLSID CLSID_DestinationList ={ 0x77F10CF0, 0x3DB5, 0x4966, { 0xB5, 0x20, 0xB7, 0xC5, 0x4F, 0xD3, 0x5E, 0xD6 } };
//const CLSID CLSID_EnumerableObjectCollection = {0x2d3468c1,0x36a7,0x43b6,{0xac,0x24,0xd3,0xf0,0x2f,0xd9,0x60,0x7a}};
//#undef NTDDI_VERSION
//#define NTDDI_VERSION NTDDI_VERSION_FROM_WIN32_WINNT(NTDDI_WIN7)
#define INITGUID
#undef NO_SHLWAPI_STRFCNS
#define STRICT_TYPED_ITEMIDS 
#include <propkey.h>
#include <propvarutil.h>
#include "libmad\decoder.h"
#include "mp3info.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CEqualizer.h"
#include "CPianoRoll.h"
#include "CPianoRollTuneDlg.h"
#include "CAnalyzerDlg.h"
#include "CPromptEngine.h"
#include "CMediaPlayerDlg.h"
#include "FileTagInfo.h"
#include "NoteFundamentalPick.h"
#include <math.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>
#include <MMSystem.h>
#include "dsound.h"
#include "Douga.h"
#include "itiran.h"
#include "itiran_FC.h"
#include "itiran_YSF.h"
#include "itiran_YS6.h"
#include "itiran_YSO.h"
#include "ED63rd.h"
#include "ZWEIII.h"
#include "YsC1.h"
#include "YsC2.h"
#include "XA.h"
#include "Ys12_1.h"
#include "Ys12_2.h"
#include "Sor.h"
#include "Zwei.h"
#include "Gurumin.h"
#include "Dino.h"
#include "Br4.h"
#include "ED3.h"
#include "ED4.h"
#include "ED5.h"
#include "TUKI.h"
#include "Nishi.h"
#include "Arc.h"
#include "San1.h"
#include "San2.h"
//#include "vfw.h"
#include "CImageBase.h"

#include <direct.h>
#include "Folder.h"
#include "Render.h"
#include "PlayList.h"
#include "Mp3Image.h"
#include "Kpilist.h"
#include "CInt24.h"
#include "ZeroFol.h"
int wavbit_sample_Hz;
int wavsam_depth = 16;
int wavsam_src = 16; // original decoder bit depth (can be -32/-64)
int g_kpiSourceBitsPerSample = 16;
#include "Id3tagv1.h"
#include "Id3tagv2.h"
#include "mp3.h"
#include "OSVersion.h"
#include "UpdateCheck.h"
#include "SongParams.h"
#include "codec/neaacdec.h"
#include "m4a.h"
#include "flac.h"
#include "dsd\dsd.h"
#include "opus.h"
#include "wav.h"
#include <intrin.h>

#include <vector>
#include <algorithm>
#include <mutex>
#include <process.h>
#include "LyricsProgressWnd.h"
#include "AudioUpscaler.h"
#include "Bufwav3Sync.h"
#include "SpeanaNoteDetector.h"

bool ProcessAudioWithRubberBand(float tempoRate, bool t);
void ConvertRawBytesToFloat(const std::vector<uint8_t>& raw_data,
	uint16_t bits_per_sample, uint16_t channels,
	std::vector<float>& out_float_data);
void ConvertFloatToRawBytes(const std::vector<float>& float_data,
	uint16_t target_bits_per_sample, uint16_t channels,
	std::vector<uint8_t>& out_raw_data);
extern std::vector<float> m_convertedPcmFloatData;
extern std::vector<float> inputFloatData;
extern std::vector<uint8_t> m_bufwav3_1;
std::vector<uint8_t> m_bufwav3_2;

extern std::vector<float> g_loopTailBuffer;
extern size_t g_loopTailPos;

extern std::mutex cl2;
extern volatile LONG g_dsDeviceOpBusy;

CImageBase* Games;

#pragma warning(push)
#pragma warning(disable : 4201)
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#pragma warning(pop)
#include <endpointvolume.h>
#include <FunctionDiscoveryKeys_devpkey.h>

// アプリ→Windows 主音量変更時のイベント文脈。OnNotify で自分自身の変更を無視する。
static const GUID GUID_OggMasterVolCtx =
{ 0xa3c81e42, 0x6b71, 0x4d2e, { 0x9c, 0x1a, 0x5e, 0x8f, 0x3b, 0x2d, 0x7a, 0x11 } };

// timerp と Windows→UI 同期で共有（単位は deve 時: 0〜100、waveOut 時: 0〜1）
static float s_lastAppliedVol = -1.0f;
static bool s_lastUsedEndpoint = false;

namespace {
class CEndpointVolCallback : public IAudioEndpointVolumeCallback
{
	LONG m_cRef;
	HWND m_hwnd;
public:
	explicit CEndpointVolCallback(HWND hwnd) : m_cRef(1), m_hwnd(hwnd) {}
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == __uuidof(IAudioEndpointVolumeCallback)) {
			*ppv = static_cast<IAudioEndpointVolumeCallback*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_cRef); }
	STDMETHODIMP_(ULONG) Release()
	{
		const LONG c = InterlockedDecrement(&m_cRef);
		if (c == 0) delete this;
		return (ULONG)c;
	}
	STDMETHODIMP OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA pNotify)
	{
		if (!pNotify || !m_hwnd || !::IsWindow(m_hwnd)) return S_OK;
		if (InlineIsEqualGUID(pNotify->guidEventContext, GUID_OggMasterVolCtx))
			return S_OK;
		float f = pNotify->fMasterVolume;
		if (f < 0.0f) f = 0.0f;
		if (f > 1.0f) f = 1.0f;
		DWORD bits = 0;
		memcpy(&bits, &f, sizeof(bits));
		::PostMessage(m_hwnd, WM_ENDPOINT_VOLUME, (WPARAM)bits, 0);
		return S_OK;
	}
};
} // namespace

static CEndpointVolCallback* g_epVolCb = nullptr;

static mp3 mp3_;
static m4a m4a_;
static flac flac_;
dsd dsd_;
opus opus_;
static wav wav_;

int readadpcm(CFile& adpcmf, char* bw, int len);
int readadpcmzwei(CFile& adpcmf, char* bw, int len);
int readadpcmgurumin(CFile& adpcmf, char* bw, int len);
int readadpcmarc(CFile& adpcmf, char* bw, int len);
int seekadpcm(int pos);
// adbuf2 全量読み込み形式(mode 10-21, -6, 30, -11〜 等)は seekadpcm。OGG/KPI 等と混同しないこと。
static inline bool ModeUsesSeekAdpcm(int m)
{
	return ((m >= 10 && m <= 21) || m <= -10 || m == 999 || m == -6 || m == 30);
}
static BOOL decode_msadpcm_wav(CFile& f, const wavinfo& wi, char** outBuf, int* outSize);
BOOL wavwait, thend;
int wavchannel = 2;
// MP3 デコード出力のビット深度（mp3_.m_dwBitsPerSample と一致させる。wavsam_depth との不一致で playb が 16/24=1.5 倍ずれるのを防ぐ）
int g_mp3_decoder_bps = 16;
int muon;
int kpi_silence_bytes = 0;

static bool DeserializeLogFont(const TCHAR* str, LOGFONT* lf)
{
	if (!str || !lf || _tcslen(str) == 0) return false;
	if (_tcschr(str, '|') == NULL) {
		memset(lf, 0, sizeof(LOGFONT));
		_tcsncpy(lf->lfFaceName, str, LF_FACESIZE - 1);
		lf->lfFaceName[LF_FACESIZE - 1] = 0;
		return false;
	}
	memset(lf, 0, sizeof(LOGFONT));
	TCHAR faceName[LF_FACESIZE] = { 0 };
	int height = 0, width = 0, escapement = 0, orientation = 0, weight = 0;
	int italic = 0, underline = 0, strikeOut = 0, charSet = 0, outPrecision = 0, clipPrecision = 0, quality = 0, pitchAndFamily = 0;
	int parsed = _stscanf(str, _T("%[^|]|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d"),
		faceName, &height, &width, &escapement, &orientation, &weight,
		&italic, &underline, &strikeOut, &charSet, &outPrecision, &clipPrecision, &quality, &pitchAndFamily);
	if (parsed >= 1) {
		_tcsncpy(lf->lfFaceName, faceName, LF_FACESIZE - 1);
		lf->lfFaceName[LF_FACESIZE - 1] = 0;
	}
	if (parsed >= 14) {
		lf->lfHeight = height;
		lf->lfWidth = width;
		lf->lfEscapement = escapement;
		lf->lfOrientation = orientation;
		lf->lfWeight = weight;
		lf->lfItalic = (BYTE)italic;
		lf->lfUnderline = (BYTE)underline;
		lf->lfStrikeOut = (BYTE)strikeOut;
		lf->lfCharSet = (BYTE)charSet;
		lf->lfOutPrecision = (BYTE)outPrecision;
		lf->lfClipPrecision = (BYTE)clipPrecision;
		lf->lfQuality = (BYTE)quality;
		lf->lfPitchAndFamily = (BYTE)pitchAndFamily;
		return true;
	}
	return false;
}
IMPLEMENT_DYNCREATE(CWread, CWinThread)
CWread::CWread() {}
CWread::~CWread() {}
BOOL CWread::InitInstance() { return TRUE; }
BEGIN_MESSAGE_MAP(CWread, CWinThread)
	ON_THREAD_MESSAGE(WM_APP + 100, wavread1)
END_MESSAGE_MAP()
void CWread::wavread1(WPARAM a, LPARAM b) {
	wavread(); thend = 1; wavwait = 1; AfxEndThread(0); return;
}

#define ID_HOTKEY0 8000
#define ID_HOTKEY1 8001
#define ID_HOTKEY2 8002
#define ID_HOTKEY3 8003

#include <eh.h>
#include "afxwin.h"
class SE_Exception
{
private:
	unsigned int nSE;
public:
	SE_Exception() {}
	SE_Exception(unsigned int n) : nSE(n) {}
	~SE_Exception() {}
	unsigned int getSeNumber() { return nSE; }
};
void trans_func(unsigned int, EXCEPTION_POINTERS*);
void trans_func(unsigned int u, EXCEPTION_POINTERS* pExp)
{
	throw SE_Exception();
}

BOOL sflg = FALSE;
MMRESULT    mmRes;
HWAVEOUT    hwo;
CPlayList* pl = NULL;
CMp3Image* mi = NULL;
BOOL plw, miw;
extern TCHAR karento2[1024];
char kare[256];
extern COggDlg* og;
char cm[10000];
CCriticalSection2 cs;
CString fnn, stitle;
int stf;
int plf, hsc;
char* ogg, * wav;
CDC dc, * cdc0, dcsub;
CBitmap bmp, bmpsub;
float fade, fadeadd;
int mcnt, mcnt2, mcnt1, mcnt3, mcnt4, mcnt5, mcnt6;
char cm1[10000];
int wavbit2;
extern save savedata;
int spelv[400];
int spetm[400];
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

int fade1, playf;
BOOL thn = TRUE;
BOOL thn1 = FALSE;

// stop/stop1 が thn1 を立て、デコーダ解放前に stf も立てる。再生スレッド内の while(true) は必ずこれを見る。
static inline bool IsPlaybackStopRequested()
{
	return thn1 != FALSE || stf != 0;
}

// play/stop/stop1 の DoEvent 再入でデコーダを二重解放しないためのガード（UI スレッド専用）
static bool s_inPlay = false;
static bool s_inStop1 = false;

// 実際に Open 中のデコーダ形式。playlist Get() が mode を次曲に差し替えても、
// stop/stop1 はこちらで正しいデコーダを閉じる（停止ボタンでは mode が一致するため落ちない）。
int g_openDecoderMode = INT_MIN;

static int PeekOpenDecoderMode(int fallbackMode)
{
	return (g_openDecoderMode != INT_MIN) ? g_openDecoderMode : fallbackMode;
}

static void ClearOpenDecoderMode()
{
	g_openDecoderMode = INT_MIN;
}

// 再生スレッド用。playlist Get() が mode を次曲に差し替えても、Open 中の形式だけをデコードする。
// INT_MIN のときはデコーダ無し（解放済み）なので mode にフォールバックしない。
static inline int ActiveDecodeMode()
{
	return g_openDecoderMode;
}
LPDIRECTSOUND8 m_ds;
LPDIRECTSOUNDBUFFER m_dsb1 = NULL;
LPDIRECTSOUNDBUFFER8 m_dsb = NULL;
LPDIRECTSOUND3DBUFFER m_dsb3d = NULL;

LPDIRECTSOUNDBUFFER m_p;
LPDIRECTSOUND3DBUFFER m_lpDS3DBuffer;
HANDLE hNotifyEvent[20];
LPDIRECTSOUNDNOTIFY dsnf1;
LPDIRECTSOUNDNOTIFY dsnf2;
UINT HandleNotifications(LPVOID lpvoid);
UINT WASAPIHandleNotifications(LPVOID lpvoid);
void HandleNotifications_export();  // WAV出力専用（DirectSoundなし、ファイル書き込みのみ）
void SignalPlaybackNotifyThreadStop();
BOOL WaitForPlaybackNotifyThreadExit(DWORD timeoutMs = 2500);
extern DWORD g_playbackNotifyJoinTimeoutMs;
extern volatile LONG g_interactiveTrackChange;
void BeginPlaybackNotifyThread();
ULONG WAVDALen;
UINT WAVDAStartLen;

extern IMMDeviceEnumerator* deviceEnumerator;
extern IMMDeviceCollection* pDeviceCollection;
extern IMMDevice* pDevice;
extern IAudioClient* pAudioClient;
extern IAudioRenderClient* pRenderClient;

int randomno;
int playwavkpi(BYTE* bw, int old, int l1, int l2);
int readkpi(BYTE* bw, int cnt);
int playwavflac(BYTE* bw, int old, int l1, int l2);
int readflac(BYTE* bw, int cnt);
static double GetFloatToInt16Scale(double maxAbs, double meanAbs)
{
	// Base conversion for normalized float PCM.
	double scale = 32767.0;

	// If level is too small (common with some PSF2 paths), apply upward-only gain.
	// We never attenuate here: clipping is safer than silence in this decoder path.
	double ref = maxAbs;
	if (ref <= 0.0 || !_finite(ref)) ref = meanAbs;
	if (ref > 0.0 && _finite(ref) && ref < 0.20) {
		double boost = 0.80 / ref;
		if (boost < 1.0) boost = 1.0;
		if (boost > 65536.0) boost = 65536.0; // guard overflow / extreme amplification
		scale *= boost;
	}
	return scale;
}

static short FloatSampleToInt16(double v, double scale)
{
	if (!_finite(v)) return 0;
	double s = v * scale;
	if (s > 32767.0) s = 32767.0;
	else if (s < -32768.0) s = -32768.0;
	return (short)s;
}

// kbpsf2 (kpi_sources kbpsf2_decoder.cpp): when nBits=-64, Render does
//   double = int16 * (m_volume / 32768)
// Inverse when m_volume==1 (IgnoreVolumeTag=1 + Volume 100%): int16 = round(double * 32768)
static short Kbpsf2ScaledDoubleToInt16(double d)
{
	if (!_finite(d)) return 0;
	double s = d * 32768.0;
	if (s > 32767.0) s = 32767.0;
	else if (s < -32768.0) s = -32768.0;
	return (short)(s >= 0.0 ? (s + 0.5) : (s - 0.5));
}

static int ConvertFloatPcmBufferToInt16InPlace(BYTE* buffer, int inBytes, int bitsPerSample, int channels)
{
	if (!buffer || inBytes <= 0 || channels <= 0) return 0;
	if (!(bitsPerSample == -32 || bitsPerSample == -64)) return inBytes;
	const int srcBytesPerSample = (-bitsPerSample) / 8;
	if (srcBytesPerSample <= 0) return 0;
	const int frames = inBytes / (srcBytesPerSample * channels);
	if (frames <= 0) return 0;
	short* dst = (short*)buffer;
	double maxAbs = 0.0;
	double sumAbs = 0.0;
	int validCount = 0;
	if (bitsPerSample == -32) {
		const float* src = (const float*)buffer;
		const int samples = frames * channels;
		for (int i = 0; i < samples; ++i) {
			double v = (double)src[i];
			if (!_finite(v)) continue;
			double a = fabs(v);
			if (a > maxAbs) maxAbs = a;
			sumAbs += a;
			++validCount;
		}
		const double meanAbs = (validCount > 0) ? (sumAbs / (double)validCount) : 0.0;
		const double scale = GetFloatToInt16Scale(maxAbs, meanAbs);
		for (int i = 0; i < samples; ++i) {
			dst[i] = FloatSampleToInt16((double)src[i], scale);
		}
	}
	else {
		const double* src = (const double*)buffer;
		const int samples = frames * channels;
		for (int i = 0; i < samples; ++i) {
			dst[i] = Kbpsf2ScaledDoubleToInt16(src[i]);
		}
	}
	return frames * channels * (int)sizeof(short);
}

static DWORD ConvertFloatTypedToInt16Buffer(const void* src, DWORD samples, int bitsPerSample, int channels, BYTE* dst, DWORD dstBytes)
{
	if (!src || !dst || samples == 0 || channels <= 0 || dstBytes == 0) return 0;
	if (!(bitsPerSample == -32 || bitsPerSample == -64)) return 0;
	const DWORD totalSamples = samples * (DWORD)channels;
	const DWORD needBytes = totalSamples * (DWORD)sizeof(short);
	if (needBytes > dstBytes) return 0;
	short* out = (short*)dst;
	double maxAbs = 0.0;
	double sumAbs = 0.0;
	DWORD validCount = 0;
	if (bitsPerSample == -32) {
		const float* p = (const float*)src;
		for (DWORD i = 0; i < totalSamples; ++i) {
			double v = (double)p[i];
			if (!_finite(v)) continue;
			double a = fabs(v);
			if (a > maxAbs) maxAbs = a;
			sumAbs += a;
			++validCount;
		}
		const double meanAbs = (validCount > 0) ? (sumAbs / (double)validCount) : 0.0;
		const double scale = GetFloatToInt16Scale(maxAbs, meanAbs);
		for (DWORD i = 0; i < totalSamples; ++i) {
			out[i] = FloatSampleToInt16((double)p[i], scale);
		}
	}
	else {
		const double* p = (const double*)src;
		for (DWORD i = 0; i < totalSamples; ++i) {
			out[i] = Kbpsf2ScaledDoubleToInt16(p[i]);
		}
	}
	return needBytes;
}

static DWORD ConvertFloatRawBytesToInt16Buffer(const BYTE* src, DWORD srcBytes, int bitsPerSample, int channels, BYTE* dst, DWORD dstBytes)
{
	if (!src || !dst || srcBytes == 0 || channels <= 0) return 0;
	if (!(bitsPerSample == -32 || bitsPerSample == -64)) return 0;
	const DWORD srcBytesPerSample = (DWORD)((-bitsPerSample) / 8);
	const DWORD srcBytesPerFrame = srcBytesPerSample * (DWORD)channels;
	if (srcBytesPerFrame == 0) return 0;
	const DWORD frames = srcBytes / srcBytesPerFrame;
	const DWORD outNeed = frames * (DWORD)channels * (DWORD)sizeof(short);
	if (outNeed > dstBytes) return 0;
	short* out = (short*)dst;
	DWORD o = 0;
	double maxAbs = 0.0;
	double sumAbs = 0.0;
	DWORD validCount = 0;

	if (bitsPerSample == -32) {
		for (DWORD i = 0; i < frames * (DWORD)channels; ++i) {
			float v = 0.0f;
			memcpy(&v, src + i * sizeof(float), sizeof(float));
			if (!_finite((double)v)) continue;
			double a = fabs((double)v);
			if (a > maxAbs) maxAbs = a;
			sumAbs += a;
			++validCount;
		}
		const double meanAbs = (validCount > 0) ? (sumAbs / (double)validCount) : 0.0;
		const double scale = GetFloatToInt16Scale(maxAbs, meanAbs);
		for (DWORD i = 0; i < frames * (DWORD)channels; ++i) {
			float v = 0.0f;
			memcpy(&v, src + i * sizeof(float), sizeof(float));
			out[o++] = FloatSampleToInt16((double)v, scale);
		}
	}
	else {
		// kbpsf2: double is scaled int16, not generic IEEE float PCM (see kbpsf2_decoder.cpp Render).
		for (DWORD i = 0; i < frames * (DWORD)channels; ++i) {
			double v = 0.0;
			memcpy(&v, src + i * sizeof(double), sizeof(double));
			out[o++] = Kbpsf2ScaledDoubleToInt16(v);
		}
	}
	return outNeed;
}
int playwavdsd(BYTE* bw, int old, int l1, int l2);
int readdsd(BYTE* bw, int cnt);
int playwavm4a(BYTE* bw, int old, int l1, int l2);
int readm4a(BYTE* bw, int cnt);
int playwavopus(BYTE* bw, int old, int l1, int l2);
int readopus(BYTE* bw, int cnt);
int playwavmp3(BYTE* bw, int old, int l1, int l2);
int readmp3(BYTE* bw, int cnt);
int playwavwav(BYTE* bw, int old, int l1, int l2);
int readwav(BYTE* bw, int cnt);
void playwavds2(BYTE* bw, int old, int l1, int l2);
BOOL playwavBuffwav(BYTE* bw, int old, int l1, int l2);
int g_warmupOutputBytes = 0;
int  g_warmupRBOutputBytes = 0;
bool g_inWarmup = false;
bool g_isWavExportRendering = false;
//スレッド
UINT wavread(LPVOID);
extern BYTE bufimage[0x30000f];

CString extn;
int ogpl0 = 0;
/////////////////////////////////////////////////////////////////////////////
// アプリケーションのバージョン情報で使われている CAboutDlg ダイアログ
extern void DoEvent();
#include "CCustomControl.h"

static CString BuildCpuInstructionListString()
{
	CString avx2 = LL14(L"使用可能命令：", L"Available instructions: ", L"Instructions disponibles : ", L"Istruzioni disponibili: ", L"Instrucciones disponibles: ", L"사용 가능 명령: ", L"可用指令：", L"التعليمات المتاحة: ", L"Доступные инструкции: ", L"Verfugbare Befehle: ", L"Instrucoes disponiveis: ", L"Beschikbare instructies: ", L"Dostepne instrukcje: ", L"Kullanilabilir talimatlar: ");
	int CPUInfo[4] = { -1 };
	__cpuid(CPUInfo, 0x00000001);
	if (CPUInfo[0] >= 2) {
		if (CPUInfo[3] & (1 << 23))  avx2 += L"MMX ";
		if (CPUInfo[3] & (1 << 25))  avx2 += L"SSE ";
		if (CPUInfo[3] & (1 << 26))  avx2 += L"SSE2 ";
		if (CPUInfo[2] & (1))        avx2 += L"SSE3 ";
		if (CPUInfo[2] & (1 << 9))   avx2 += L"SSSE3 ";
		if (CPUInfo[2] & (1 << 12))  avx2 += L"FMA3 ";
		if (CPUInfo[2] & (1 << 19))  avx2 += L"SSE4.1 ";
		if (CPUInfo[2] & (1 << 20))  avx2 += L"SSE4.2 ";
	}
	if (CPUInfo[0] >= 2) {
		__cpuid(CPUInfo, 0x80000001);
		if (CPUInfo[2] & (1 << 6))  avx2 += L"SSE4a ";
	}
	__cpuid(CPUInfo, 0x00000001);
	if (CPUInfo[0] >= 2) {
		if (CPUInfo[2] & (1 << 28))  avx2 += L"AVX ";
	}
	if (CPUInfo[0] >= 7) {
		__cpuid(CPUInfo, 0x00000007);
		if (CPUInfo[1] & (1 << 5))  avx2 += L"AVX2 ";
		if (CPUInfo[1] & (1 << 16))  avx2 += L"AVX512 ";
	}
	return avx2;
}

class CAboutDlg : public CCustomBlurDialogBase
{
public:
	CAboutDlg(CWnd* pParent = NULL);

	// ダイアログ データ
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard は仮想関数のオーバーライドを生成します
	//{{AFX_VIRTUAL(CAboutDlg)
protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV のサポート
	//}}AFX_VIRTUAL

	// インプリメンテーション
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
public:
	CStatic m_in;
	virtual BOOL OnInitDialog();
	CLinkStatic m_link;
	CStatic m_cpu;
	CCustomEdit m_os3;
	CCustomStandardButton m_okdummy;
};

CAboutDlg::CAboutDlg(CWnd* pParent) : CCustomBlurDialogBase(CAboutDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDC_STATICin, m_in);
	DDX_Control(pDX, IDC_Link, m_link);
	DDX_Control(pDX, IDC_STATICin2, m_cpu);
	DDX_Control(pDX, IDC_STATICin3, m_os3);
	DDX_Control(pDX, IDOK, m_okdummy);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CCustomBlurDialogBase)
	//{{AFX_MSG_MAP(CAboutDlg)
	// メッセージ ハンドラがありません。
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

class CKpiLoadingWnd;
static CKpiLoadingWnd* g_pActiveLoadingWnd = NULL;
static int g_nCurrentKpiIndex = 0;
// KPI 読み込み中に二重起動インスタンスから WM_APP+1(再生要求)が届いた場合の遅延フラグ。
// 読み込み中に play() をネスト実行させず、plug() 完了後に再ポストして処理する。
static BOOL g_kpiLoadDeferredPlay = FALSE;

static int CountKpiFiles(CString ff)
{
	int count = 0;
	TCHAR szPrevDir[MAX_PATH];
	::GetCurrentDirectory(MAX_PATH, szPrevDir);

	_tchdir(ff);
	CFileFind f;
	if (f.FindFile(_T("*.kpi"))) {
		int b, c = 1;
		do {
			if (c)
				b = f.FindNextFile();
			c = 1;
			if (f.IsDirectory() == 0) {
				CString s = f.GetFileName();
				if (!(s == "." || s == "..") && s.Right(4) == ".kpi") {
					count++;
				}
			}
		} while (b);
	}
	f.Close();

	CFileFind cf1;
	if (cf1.FindFile(_T("*.*")) != 0) {
		int h = 1;
		for (; h;) {
			h = cf1.FindNextFile();
			CString ss = cf1.GetFileName();
			if (!(ss == "." || ss == "..")) {
				if (cf1.IsDirectory() != 0) {
					if (ff.Right(1) == "\\")
						count += CountKpiFiles(ff + cf1.GetFileName());
					else
						count += CountKpiFiles(ff + _T("\\") + cf1.GetFileName());
					_tchdir(ff);
				}
			}
		}
	}
	cf1.Close();

	_tchdir(szPrevDir);
	return count;
}

class CKpiLoadingWnd : public CWnd
{
public:
	CProgressCtrl m_progress;

	CKpiLoadingWnd()
	{
		m_strText = LL14(
			L"KPI読み込み中…\n（しばらく時間がかかる場合があります）",
			L"Reading KPI list…\n(This may take some time)",
			L"Lecture de la liste KPI…\n(Cela peut prendre du temps)",
			L"Lettura della lista KPI…\n(Questo potrebbe richiedere del tempo)",
			L"Leyendo la lista KPI…\n(Esto puede tardar un poco)",
			L"KPI 목록을 읽는 중…\n(시간이 다소 걸릴 수 있습니다)",
			L"正在读取KPI列表…\n（可能需要一些时间）",
			L"جاري قراءة قائمة KPI…\n(قد يستغرق هذا بعض الوقت)",
			L"Чтение списка KPI…\n(Это может занять некоторое время)",
			L"KPI-Liste wird gelesen…\n(Dies kann einige Zeit dauern)",
			L"Lendo a lista KPI…\n(Isso pode levar algum tempo)",
			L"KPI-lijst lezen…\n(Dit kan even duren)",
			L"Odczytywanie listy KPI…\n(Może to zająć trochę czasu)",
			L"KPI listesi okunuyor…\n(Bu biraz zaman alabilir)"
		);
	}

	virtual ~CKpiLoadingWnd()
	{
		if (m_hWnd != NULL) {
			DestroyWindow();
		}
	}

	BOOL Create(CWnd* pParent = NULL)
	{
		// Register window class
		CString strWndClass = AfxRegisterWndClass(
			CS_HREDRAW | CS_VREDRAW,
			::LoadCursor(NULL, IDC_WAIT), // hourglass cursor
			(HBRUSH)(COLOR_WINDOW + 1),
			NULL
		);

		UINT dpi = GetDpi(m_hWnd);

		// Size & position
		int width = Scale(320, dpi);
		int height = Scale(120, dpi);

		CRect rect(0, 0, width, height);
		if (pParent != NULL && pParent->GetSafeHwnd() != NULL) {
			CRect parentRect;
			pParent->GetWindowRect(&parentRect);
			int x = parentRect.left + (parentRect.Width() - rect.Width()) / 2;
			int y = parentRect.top + (parentRect.Height() - rect.Height()) / 2;
			rect.OffsetRect(x, y);
		}
		else {
			int screenWidth = GetSystemMetrics(SM_CXSCREEN);
			int screenHeight = GetSystemMetrics(SM_CYSCREEN);
			int x = (screenWidth - rect.Width()) / 2;
			int y = (screenHeight - rect.Height()) / 2;
			rect.OffsetRect(x, y);
		}

		// Create popup window with border and topmost
		BOOL result = CreateEx(
			WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
			strWndClass,
			_T(""),
			WS_POPUP | WS_BORDER,
			rect.left, rect.top, rect.Width(), rect.Height(),
			pParent ? pParent->GetSafeHwnd() : NULL,
			NULL
		);

		if (result) {
			m_font.CreatePointFont(110, _T("MS UI Gothic"));

			// Position progress bar near the bottom
			CRect progressRect(Scale(20, dpi), Scale(85, dpi), rect.Width() - Scale(20, dpi), Scale(102, dpi));
			m_progress.Create(WS_CHILD | WS_VISIBLE | PBS_SMOOTH, progressRect, this, 1);
			m_progress.SetRange(0, 100);
			m_progress.SetPos(0);

			SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}

		return result;
	}

	void SetRange(int nMin, int nMax)
	{
		if (m_progress.GetSafeHwnd()) {
			m_progress.SetRange32(nMin, nMax);
		}
	}

	void SetPos(int nPos)
	{
		if (m_progress.GetSafeHwnd()) {
			m_progress.SetPos(nPos);
			m_progress.UpdateWindow();
			UpdateWindow();

			// Pump messages to keep it responsive
			// WM_TIMER は絶対にディスパッチしない（plugloop 側のポンプと同じ理由）。
			// 親 COggDlg の WM_INITDIALOG 中にタイマー 9998(関連付け起動の dp(ndd) 再生)
			// や 5211(プレイリスト Create)が発火すると、KPI 読み込み中に play() が
			// ネスト実行され「読み込み中のまま固まる+裏で再生+メモリエラー」になる。
			MSG msg;
			while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT) {
					::PostQuitMessage((int)msg.wParam);
					break;
				}
				if (msg.message == WM_TIMER)
					continue;
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}
	}

	void Show()
	{
		if (m_hWnd != NULL) {
			ShowWindow(SW_SHOW);
			UpdateWindow();

			// Pump messages once (WM_TIMER は SetPos と同じ理由で除外)
			MSG msg;
			while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
				if (msg.message == WM_QUIT) {
					::PostQuitMessage((int)msg.wParam);
					break;
				}
				if (msg.message == WM_TIMER)
					continue;
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}
		}
	}

	void Hide()
	{
		if (m_hWnd != NULL) {
			ShowWindow(SW_HIDE);
		}
	}

protected:
	CString m_strText;
	CFont m_font;

	static int Scale(int value, UINT dpi)
	{
		return (int)(((float)value) * ((float)dpi) / 96.0f);
	}

	static UINT GetDpi(HWND hWnd)
	{
		HDC hdc = ::GetDC(hWnd);
		UINT dpi = GetDeviceCaps(hdc, LOGPIXELSX);
		::ReleaseDC(hWnd, hdc);
		return dpi;
	}

	afx_msg void OnPaint()
	{
		CPaintDC dc(this);

		CRect clientRect;
		GetClientRect(&clientRect);

		// Background: soft blue/grey theme matching LyricsProgressWnd
		dc.FillSolidRect(&clientRect, RGB(230, 240, 255));

		// Border
		CPen pen(PS_SOLID, 2, RGB(100, 150, 200));
		CPen* pOldPen = dc.SelectObject(&pen);
		dc.Rectangle(&clientRect);
		dc.SelectObject(pOldPen);

		CFont* pOldFont = dc.SelectObject(&m_font);
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(50, 50, 150));

		UINT dpi = GetDpi(m_hWnd);
		const int hPad = Scale(16, dpi);
		const int textAreaBottom = Scale(78, dpi);

		CString title = m_strText;
		CString subtitle;
		const int nl = m_strText.Find(L'\n');
		if (nl >= 0) {
			title = m_strText.Left(nl);
			subtitle = m_strText.Mid(nl + 1);
		}

		// Title: draw as a single line via TextOut (DrawText+DT_WORDBREAK can break
		// between CJK characters and, when the rect is too short, center each fragment
		// on the same baseline — producing the wide gaps seen in the screenshot).
		CSize titleSize = dc.GetTextExtent(title);
		const int titleX = (clientRect.Width() - titleSize.cx) / 2;
		const int titleY = Scale(10, dpi);
		dc.TextOut(titleX, titleY, title);

		if (!subtitle.IsEmpty()) {
			const int wrapWidth = clientRect.Width() - hPad * 2;

			// Measure at a fixed wrap width, then draw with the same width.
			CRect calcRect(0, 0, wrapWidth, 0);
			dc.DrawText(subtitle, &calcRect, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);

			CRect subRect(
				hPad,
				titleY + titleSize.cy + Scale(4, dpi),
				clientRect.right - hPad,
				titleY + titleSize.cy + Scale(4, dpi) + calcRect.Height());
			if (subRect.bottom > textAreaBottom)
				subRect.bottom = textAreaBottom;

			dc.DrawText(subtitle, &subRect, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX);
		}

		dc.SelectObject(pOldFont);
	}

	afx_msg BOOL OnEraseBkgnd(CDC* pDC)
	{
		return TRUE; // anti-flicker
	}

	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CKpiLoadingWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

double aa1_ = 0;
BOOL CAboutDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"oggのバージョン情報", L"ogg Version Info", L"Info version ogg", L"Info versione ogg", L"Info version ogg", L"ogg 버전 정보", L"ogg版本信息", L"معلومات إصدار ogg", L"Информация о версии ogg", L"ogg Versionsinfo", L"Info versao ogg", L"ogg versie-info", L"Informacje o wersji ogg", L"ogg surum bilgisi"));
	SetDlgItemText(IDOK, LL14(L"OK", L"OK", L"OK", L"OK", L"OK", L"확인", L"确定", L"موافق", L"OK", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
	COSVersion os;
	CString s;
	s.Format(_T("%s"), os.GetVersionString());

	m_in.SetWindowText(s);


	char CPUBrandString[0x40];
	int CPUInfo[4] = { -1 };
	__cpuid(CPUInfo, 0x80000002);
	memcpy(CPUBrandString, CPUInfo, sizeof(CPUInfo));
	__cpuid(CPUInfo, 0x80000003);
	memcpy(CPUBrandString + 16, CPUInfo, sizeof(CPUInfo));
	__cpuid(CPUInfo, 0x80000004);
	memcpy(CPUBrandString + 32, CPUInfo, sizeof(CPUInfo));
	s = CPUBrandString;
	m_cpu.SetWindowText(s);

	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->m_os3.GetWindowText(s);
	else
		s = BuildCpuInstructionListString();
	m_os3.SetWindowText(s);
	m_os3.SetReadOnly(TRUE);

	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

/////////////////////////////////////////////////////////////////////////////
// COggDlg ダイアログ

COggDlg::COggDlg(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(COggDlg::IDD, pParent)
	, m_hTimerpVsyncThread(NULL)
	, m_hTimerpVsyncStopEvent(NULL)
{
	//{{AFX_DATA_INIT(COggDlg)
	//}}AFX_DATA_INIT
	// メモ: LoadIcon は Win32 の DestroyIcon のサブシーケンスを要求しません。
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_jacketFocus = 0.0;
	m_lastTick = 0;
	m_EqualizerDlg = new CEqualizer();
	m_PianoRollDlg = new CPianoRoll();
	m_PianoRollTuneDlg = new CPianoRollTuneDlg();
	m_AnalyzerDlg = new CAnalyzerDlg();
}

void COggDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(COggDlg)
	DDX_Control(pDX, IDC_CHECK15, m_ysc2);
	DDX_Control(pDX, IDC_CHECK14, m_ysc1);
	DDX_Control(pDX, IDC_STATICds, m_dsvols);
	DDX_Control(pDX, IDC_SLIDER3, m_dsval);
	DDX_Control(pDX, IDC_CHECK13, m_zweiii);
	DDX_Control(pDX, IDC_CHECK12, m_ed6tc);
	DDX_Control(pDX, IDC_CHECK11, m_yso);
	DDX_Control(pDX, IDC_SLIDER2, m_time);
	DDX_Control(pDX, IDC_EDIT1, m_kaisuu);
	DDX_Control(pDX, IDC_CHECK6, m_junji);
	DDX_Control(pDX, IDC_CHECK5, m_random);
	DDX_Control(pDX, IDC_BUTTON13, m_sita);
	DDX_Control(pDX, IDC_BUTTON12, m_ue);
	DDX_Control(pDX, IDC_CHECK10, m_ed6sc);
	DDX_Control(pDX, IDC_CHECK9, m_ed6fc);
	DDX_Control(pDX, IDC_CHECK8, m_ysf);
	DDX_Control(pDX, IDC_CHECK7, m_ys6);
	DDX_Control(pDX, IDC_CHECK4, m_st);
	DDX_Control(pDX, IDC_CHECK1, m_supe);
	DDX_Control(pDX, IDC_BUTTON3, m_ps);
	DDX_Control(pDX, IDC_STATIC2, m_vol);
	DDX_Control(pDX, IDC_SLIDER1, m_sl);
	DDX_Control(pDX, IDC_CHECK3, m_dou);
	DDX_Control(pDX, IDC_CHECK2, m_c2);
	DDX_Control(pDX, IDC_STATIC11, m_11);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDC_CHECK16, m_xa);
	DDX_Control(pDX, IDC_CHECK17, m_ys121);
	DDX_Control(pDX, IDC_CHECK18, m_ys122);
	DDX_Control(pDX, IDC_CHECK19, m_sor);
	DDX_Control(pDX, IDC_CHECK20, m_zwei);
	DDX_Control(pDX, IDC_CHECK21, m_gurumin);
	DDX_Control(pDX, IDC_BUTTON14, m_rund);
	DDX_Control(pDX, IDC_CHECK22, m_dino);
	DDX_Control(pDX, IDC_BUTTON4, m_saisai);
	DDX_Control(pDX, IDC_CHECK23, m_br4);
	DDX_Control(pDX, IDC_CHECK24, m_ed3);
	DDX_Control(pDX, IDC_CHECK25, m_ed4);
	DDX_Control(pDX, IDC_CHECK26, m_ed5);
	DDX_Control(pDX, IDC_BUTTON8, d_ys6);
	DDX_Control(pDX, IDC_BUTTON7, d_ys3);
	DDX_Control(pDX, IDC_BUTTON15, d_yso);
	DDX_Control(pDX, IDC_BUTTON6, d_ed6fc);
	DDX_Control(pDX, IDC_BUTTON2, d_ed6sc);
	DDX_Control(pDX, IDC_BUTTON17, d_ed6tc);
	DDX_Control(pDX, IDC_BUTTON19, d_z2);
	DDX_Control(pDX, IDC_BUTTON23, d_ysc1);
	DDX_Control(pDX, IDC_BUTTON24, d_ysc2);
	DDX_Control(pDX, IDC_BUTTON25, d_xa);
	DDX_Control(pDX, IDC_BUTTON27, d_ys1);
	DDX_Control(pDX, IDC_BUTTON28, d_ys2);
	DDX_Control(pDX, IDC_BUTTON31, d_sor);
	DDX_Control(pDX, IDC_BUTTON33, d_z1);
	DDX_Control(pDX, IDC_BUTTON35, d_guru);
	DDX_Control(pDX, IDC_BUTTON37, d_dino);
	DDX_Control(pDX, IDC_BUTTON39, d_br4);
	DDX_Control(pDX, IDC_BUTTON44, d_ed3);
	DDX_Control(pDX, IDC_BUTTON45, d_ed4);
	DDX_Control(pDX, IDC_BUTTON46, d_ed5);
	DDX_Control(pDX, IDC_BUTTON47, d_tuki);
	DDX_Control(pDX, IDC_BUTTON48, d_nishi);
	DDX_Control(pDX, IDC_BUTTON51, d_arc);
	DDX_Control(pDX, IDC_BUTTON53, d_san1);
	DDX_Control(pDX, IDC_BUTTON54, d_san2);
	DDX_Control(pDX, IDC_BUTTON57, m_playlist);
	DDX_Control(pDX, IDC_BUTTON58, m_mp3jake);
	DDX_Control(pDX, IDC_STATIC_OS, m_OS);
	DDX_Control(pDX, IDC_SLIDER4, m_kakuVol);
	DDX_Control(pDX, IDC_STATICds2, m_kakuVolval);
	DDX_Control(pDX, IDC_STATIC_OS2, m_cpu);
	DDX_Control(pDX, IDC_STATIC_OS3, m_os3);
	DDX_Control(pDX, IDC_STATIC_LRC, m_lrc);
	DDX_Control(pDX, IDC_SLIDER7, m_tempo_sl);
	DDX_Control(pDX, IDC_STATICds3, m_temp_num);
	DDX_Control(pDX, IDC_STATIC_LRC2, m_lrc2);
	DDX_Control(pDX, IDC_STATIC_LRC3, m_lrc3);
	DDX_Control(pDX, IDC_STATIC_LRC4, m_lrc4);
	DDX_Control(pDX, IDC_STATIC_LRC5, m_lrc5);
	DDX_Control(pDX, IDC_STATICds4, m_pitch);
	DDX_Control(pDX, IDC_SLIDER8, m_pitch_sl);
	DDX_Control(pDX, IDC_STATIC_t, m_temp_s);
	DDX_Control(pDX, IDC_STATIC_p, m_pitch_s);
	DDX_Control(pDX, IDC_BUTTON1, m_stoppp);
	DDX_Control(pDX, IDC_BUTTON21, m_setteiii);
	DDX_Control(pDX, IDC_BUTTON9, m_folderrrr);
	DDX_Control(pDX, IDOK, m_syuryouuuu);
	DDX_Control(pDX, IDC_STATICaaa, dummys1);
	DDX_Control(pDX, IDC_STATICaaab, m_dummys2);
	DDX_Control(pDX, IDC_STATICaaac, m_dummys3);
	DDX_Control(pDX, IDC_STATICaaad, m_dummys4);
	DDX_Control(pDX, IDC_BUTTON5, m_fadedummy);
	DDX_Control(pDX, IDC_BUTTON59, m_eqq);
}

BEGIN_MESSAGE_MAP(COggDlg, CCustomBlurDialogBase)
	//{{AFX_MSG_MAP(COggDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_DROPFILES()
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_BUTTON1, OnButton1)
	ON_BN_CLICKED(IDC_BUTTON2, OnButton2)
	ON_BN_CLICKED(IDC_BUTTON3, OnPause)
	ON_BN_CLICKED(IDC_BUTTON4, OnRestart)
	ON_BN_CLICKED(IDC_BUTTON5, OnButton5)
	ON_BN_CLICKED(IDC_BUTTON6, OnButton6_FC)
	ON_BN_CLICKED(IDC_BUTTON7, OnButton7_YSF)
	ON_BN_CLICKED(IDC_BUTTON8, OnButton8_YS6)
	ON_BN_CLICKED(IDC_BUTTON9, OnButton9_Folder)
	ON_BN_CLICKED(IDC_BUTTON12, OnButton12)
	ON_BN_CLICKED(IDC_CHECK5, OnCheck5)
	ON_BN_CLICKED(IDC_CHECK6, OnCheck6)
	ON_BN_CLICKED(IDC_BUTTON14, OnButton14)
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_BUTTON15, OnYso)
	ON_BN_CLICKED(IDC_BUTTON17, OnButton17_ED6TC)
	ON_BN_CLICKED(IDC_BUTTON19, OnZWEIII)
	ON_BN_CLICKED(IDC_BUTTON21, OnButton21)
	ON_BN_CLICKED(IDC_BUTTON23, OnYsC1)
	ON_BN_CLICKED(IDC_BUTTON24, OnYsC2)
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDC_BUTTON25, &COggDlg::OnBnClickedButton25)
	ON_BN_CLICKED(IDC_BUTTON27, &COggDlg::OnBnClickedButton27)
	ON_BN_CLICKED(IDC_BUTTON28, &COggDlg::OnBnClickedButton28)
	ON_BN_CLICKED(IDC_BUTTON31, &COggDlg::OnBnClickedButton31)
	ON_BN_CLICKED(IDC_BUTTON33, &COggDlg::OnBnClickedButton33)
	ON_BN_CLICKED(IDC_BUTTON35, &COggDlg::OnBnClickedButton35)
	ON_BN_CLICKED(IDC_BUTTON37, &COggDlg::OnBnClickedButton37)
	ON_NOTIFY(NM_RELEASEDCAPTURE, IDC_SLIDER2, &COggDlg::OnNMReleasedcaptureSlider2)
	ON_BN_CLICKED(IDC_BUTTON39, &COggDlg::OnBnClickedButton39)
	ON_BN_CLICKED(IDC_BUTTON44, &COggDlg::OnBnClickedButton44)
	ON_BN_CLICKED(IDC_BUTTON45, &COggDlg::OnBnClickedButton45)
	ON_BN_CLICKED(IDC_BUTTON46, &COggDlg::OnBnClickedButton46)
	ON_BN_CLICKED(IDC_BUTTON47, &COggDlg::OnBnClickedButton47)
	ON_BN_CLICKED(IDC_BUTTON48, &COggDlg::OnBnClickedButton48)
	ON_BN_CLICKED(IDC_BUTTON51, &COggDlg::OnBnClickedButton51)
	ON_BN_CLICKED(IDC_BUTTON53, &COggDlg::OnBnClickedButton53)
	ON_BN_CLICKED(IDC_BUTTON54, &COggDlg::OnBnClickedButton54)

	ON_MESSAGE(WM_APP + 1, dp1)
	ON_MESSAGE(WM_APP + 2, dp2)
	ON_MESSAGE(WM_TIMERP_VSYNC_TICK, &COggDlg::OnTimerpVsyncTick)
	ON_MESSAGE(WM_SPEANA_TICK, &COggDlg::OnSpeanaTick)
	ON_MESSAGE(WM_ENDPOINT_VOLUME, &COggDlg::OnEndpointVolume)
	ON_MESSAGE(WM_REFRESH_AERO_ALL, &COggDlg::OnRefreshAeroAll)
	ON_MESSAGE(WM_APP_UPDATE_AVAILABLE, OnUpdateAvailable)
	ON_MESSAGE(WM_OGG_DEFERRED_HEAVY_INIT, OnDeferredHeavyStartup)
	ON_MESSAGE(WM_OGG_ENTER_MP_MODE, &COggDlg::OnEnterMpModeMsg)
	ON_MESSAGE(WM_PLAYBACK_AUTO_STOPPED, OnPlaybackAutoStopped)
	ON_WM_COPYDATA()
	ON_WM_KEYDOWN()
	ON_WM_SYSKEYDOWN()
	ON_WM_ACTIVATE()
	ON_MESSAGE(WM_HOTKEY, OnHotKey)
	ON_WM_KILLFOCUS()
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_BUTTON57, &COggDlg::OnPlayList)
	ON_BN_CLICKED(IDC_OGG_SWITCHMODE, &COggDlg::OnSwitchMode)
	ON_MESSAGE(WM_MP_ENTER_FALCOM, &COggDlg::OnEnterFalcomMsg)
	ON_MESSAGE(WM_APP_SONGPARAM_RESTORE, &COggDlg::OnSongParamRestore)
	ON_WM_WINDOWPOSCHANGING()
	ON_BN_CLICKED(IDC_BUTTON58, &COggDlg::OnBnmp3jake)
	ON_WM_DESTROY()
	ON_WM_CREATE()
	ON_WM_MOVING()
	ON_WM_SETFOCUS()
	ON_WM_MOUSEACTIVATE()
	ON_WM_ACTIVATEAPP()
	ON_WM_NCACTIVATE()
	ON_STN_CLICKED(IDC_STATIC_t, &COggDlg::OnTempoStatic)
	ON_STN_CLICKED(IDC_STATIC_p, &COggDlg::OnPitchStatic)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_STN_CLICKED(IDC_STATIC2, &COggDlg::OnStnClickedStatic2)
	ON_STN_DBLCLK(IDC_STATIC_p, &COggDlg::OnStnDblclickStaticp)
	ON_STN_DBLCLK(IDC_STATIC_t, &COggDlg::OnStnDblclickStatict)
	ON_BN_CLICKED(IDC_BUTTON59, &COggDlg::OnBnClickedButton59)
END_MESSAGE_MAP()

REFTIME aa1, aa2 = 0;


//FFT関連設定
// FFTライブラリ(fft4g.c)
#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)
	void rdft(int, int, double*, int*, double*);
	void ddst(int, int, double*, int*, double*);
#if defined(__cplusplus)
}
#endif // defined(__cplusplus)

//#define BUFSZ			(8192*4)
#define HIGHDIV			4
#define BUFSZH			(BUFSZ/HIGHDIV)
#define SQRT_BUFSZ2		64
#define M_PI			3.1415926535897932384
#define ABS(N)			( (N)<0 ? -(N) : (N) )

int ipTab2[2 + SQRT_BUFSZ2 * 50];		// FFT sin/cos table  [ >= 2+sqrt(BUFSZH/2) ]
double wTab2[BUFSZH * 50];		// FFT sin/cos table for ddst()
double aFFT2[BUFSZH * 50];			// FFT data
double aFFT2a[BUFSZH * 50];			// FFT data

double fnWFilter[BUFSZ / 2];
//double syuha[88] = { 27.500 ,29.135,30.868,32.703,34.648,36.708,38.891,41.203,43.654,46.249,48.999,51.913,55.000,58.270,61.735,65.406,69.296,73.416,77.782,82.407,87.307,92.499,97.999,103.826,110.000,116.541,123.471,130.813,138.591,146.832,155.563,164.814,174.614,184.997,195.998,207.652,220.000,233.082,246.942,261.626,277.183,293.665,311.127,329.628,349.228,369.994,391.995,415.305,440.000,466.164,493.883,523.251,554.365,587.330,622.254,659.255,698.456,739.989,783.991,830.609,880.000,932.328,987.767,1046.502,1108.731,1174.659,1244.508,1318.510,1396.913,1479.978,1567.982,1661.219,1760.000,1864.655,1975.533,2093.005,2217.461,2349.318,2489.016,2637.020,2793.826,2959.955,3135.963,3322.438,3520.000,3729.310,3951.066,4186.009 };
int			logtbl[100 + 1];
HFONT	hFont;
int mode, modesub;
int wav999_use_adbuf = 0;

std::vector<uint8_t> g_srcScratchUpscale;

// MP3: pack_pcm は常に m_dwBitsPerSample。グローバル g_mp3_decoder_bps / wavsam だけだと 24bit 時に 16 とずれ playb が 1.5 倍になる。
static int Mp3DecoderBitsClampedFromObject(void)
{
	int bits = (int)mp3_.GetBitsPerSample();
	if (bits <= 0 || bits > 32)
		bits = 16;
	if (!(bits == 8 || bits == 16 || bits == 24 || bits == 32))
		bits = 16;
	return bits;
}

// oggDlg_ds.cpp ProcessAudioWithRubberBand 用（bufkpi = MAD 出力バイト列の解釈）
int Mp3GetDecoderBitsForRubberBand(void)
{
	return Mp3DecoderBitsClampedFromObject();
}

// readtempo → ConvertFloatToRawBytes と同じ前提の「デコード後インターリーブ1フレームのバイト数」
static int PcmOutBytesPerFrame()
{
	int bits;
	// 再生スレッドからも呼ばれる。mode ではなく Open 中の形式を見る
	if (ActiveDecodeMode() == -10) {
		bits = Mp3DecoderBitsClampedFromObject();
	}
	else {
		if (wavsam_depth <= 0 || wavsam_depth > 32)
			bits = 16;
		else
			bits = abs(wavsam_depth);
		if (!(bits == 8 || bits == 16 || bits == 24 || bits == 32))
			bits = 16;
	}
	int ch = wavchannel;
	if (ch <= 0)
		ch = 2;
	const int bpf = ch * (bits / 8);
	return (bpf > 0) ? bpf : 4;
}

// bufkpi3 リングバッファ。poss が bufSize に達すると従来コードは OOB memcpy になるため pos>=bufSize で 0 に戻す。
static inline void RingBufWrite(BYTE* buf, int bufSize, int& pos, const void* src, int len)
{
	if (len <= 0 || bufSize <= 0)
		return;
	const uint8_t* p = (const uint8_t*)src;
	pos %= bufSize;
	if (pos < 0)
		pos = 0;
	while (len > 0) {
		int chunk = bufSize - pos;
		if (chunk > len)
			chunk = len;
		memcpy(buf + pos, p, chunk);
		pos += chunk;
		if (pos >= bufSize)
			pos = 0;
		p += chunk;
		len -= chunk;
	}
}

static inline void RingBufRead(void* dest, const BYTE* buf, int bufSize, int& pos, int len)
{
	if (len <= 0 || bufSize <= 0)
		return;
	uint8_t* p = (uint8_t*)dest;
	pos %= bufSize;
	if (pos < 0)
		pos = 0;
	while (len > 0) {
		int chunk = bufSize - pos;
		if (chunk > len)
			chunk = len;
		memcpy(p, buf + pos, chunk);
		pos += chunk;
		if (pos >= bufSize)
			pos = 0;
		p += chunk;
		len -= chunk;
	}
}

// MP3: playb を PCM フレーム数で扱う。従来は内部 playb が「フレーム×4」で、seek へは playb/(stereoなら4) を渡していた。
// フレーム数 F のみ保持する場合、stereo は dwPos=F、mono は dwPos=4F で従来と同じ実シーク位置になる。
static DWORD Mp3SeekDwPosFromPlaybFrames(__int64 playbFrames)
{
	if (wavchannel == 2)
		return (DWORD)playbFrames;
	const __int64 m = (__int64)playbFrames * (__int64)4;
	if (m > (__int64)0x7fffffff)
		return 0x7fffffff;
	return (DWORD)m;
}

// インターリーブPCM（readtempo 出力と同じビット/チャンネル）にフェード（二乗ゲイン）を適用
static void ApplyFadeCubedToInterleavedPcm(void* data, int byteLen)
{
	if (!data || byteLen <= 0) return;
	const int bpf = PcmOutBytesPerFrame();
	if (bpf <= 0) return;
	byteLen -= (byteLen % bpf);
	if (byteLen <= 0) return;

	int bits;
	if (wavsam_depth <= 0 || wavsam_depth > 32)
		bits = 16;
	else
		bits = abs(wavsam_depth);
	if (!(bits == 8 || bits == 16 || bits == 24 || bits == 32))
		bits = 16;

	const float g = fade * fade;

	unsigned char* p = (unsigned char*)data;

	if (bits == 8) {
		for (int o = 0; o < byteLen; o++) {
			float v = ((float)p[o] - 128.f) * g + 128.f;
			int u = (int)floorf(v + 0.5f);
			if (u < 0) u = 0; else if (u > 255) u = 255;
			p[o] = (unsigned char)u;
		}
	}
	else if (bits == 16) {
		short* s = (short*)data;
		const int n = byteLen / 2;
		for (int i = 0; i < n; i++) {
			float v = (float)s[i] * g;
			int x = (int)floorf(v + 0.5f);
			if (x > 32767) x = 32767;
			else if (x < -32768) x = -32768;
			s[i] = (short)x;
		}
	}
	else if (bits == 24) {
		for (int o = 0; o + 2 < byteLen; o += 3) {
			const int val = p[o] | (p[o + 1] << 8) | ((int)(signed char)p[o + 2] << 16);
			float fv = (float)val * g;
			int vi = (int)floorf(fv + 0.5f);
			if (vi > 8388607) vi = 8388607;
			else if (vi < -8388608) vi = -8388608;
			p[o] = (unsigned char)(vi & 0xFF);
			p[o + 1] = (unsigned char)((vi >> 8) & 0xFF);
			p[o + 2] = (unsigned char)((vi >> 16) & 0xFF);
		}
	}
	else {
		int* s = (int*)data;
		const int n = byteLen / 4;
		for (int i = 0; i < n; i++) {
			const double v = (double)s[i] * (double)g;
			__int64 x = (__int64)floor(v + 0.5);
			if (x > 2147483647LL) x = 2147483647LL;
			else if (x < -2147483648LL) x = -2147483648LL;
			s[i] = (int)x;
		}
	}
}

static void DecodeSourceIntoScratch(uint8_t* scratch, int sb)
{
	const int dm = ActiveDecodeMode();
	if (dm == INT_MIN)
		return;
	if ((dm >= 10 && dm <= 21) || dm < -10 || dm == -6 || dm == 30 || (dm == 999 && wav999_use_adbuf))
		playwavBuffwav(scratch, 0, sb, 0);
	else if (dm == -10)
		playwavmp3(scratch, 0, sb, 0);
	else if (dm == 999)
		playwavwav(scratch, 0, sb, 0);
	else if (dm == -3)
		playwavkpi(scratch, 0, sb, 0);
	else if (dm == -7)
		playwavdsd(scratch, 0, sb, 0);
	else if (dm == -8)
		playwavflac(scratch, 0, sb, 0);
	else if (dm == -9)
		playwavm4a(scratch, 0, sb, 0);
	else
		playwavds2(scratch, 0, sb, 0);
}

void ConfigurePlaybackOutputAndUpscaler()
{
	int srcBits = abs(wavsam_depth);
	// フロート系 FLAC は Render 後に int16 に落とすため、Upscaler 入力は 16bit 幅
	if (wavsam_depth < 0)
		srcBits = 16;
	if (!(srcBits == 8 || srcBits == 16 || srcBits == 24 || srcBits == 32))
		srcBits = 16;
	if (!savedata.upscale_enable) {
		g_ds_pcm_ch = wavchannel;
		g_ds_pcm_rate = wavbit_sample_Hz;
		g_ds_pcm_bits = srcBits;
	}
	else {
		if (savedata.speaker_layout == 5) {
			int ch = wavchannel;
			if (ch < 1) ch = 2;
			if (ch > 8) ch = 8;
			g_ds_pcm_ch = ch;
		}
		else {
			g_ds_pcm_ch = SpeakerLayoutToOutChannels(savedata.speaker_layout);
		}
		g_ds_pcm_rate = (int)savedata.samples;
		if (g_ds_pcm_rate < 8000 || g_ds_pcm_rate > 384000)
			g_ds_pcm_rate = 44100;
		g_ds_pcm_bits = savedata.bit32 ? 32 : (savedata.bit24 ? 24 : 16);
	}
	g_audioUpscaler.Configure(wavbit_sample_Hz, wavchannel, srcBits, g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits);
	g_pcm_upscale_active = g_audioUpscaler.IsActive() ? 1 : 0;
}

extern __int64 playb;

void DispatchPlaywavFill(BYTE* bufwav3, ULONG oldw, int len1, int len2)
{
	if (IsPlaybackStopRequested())
		return;
	// mode は Get()/次曲選択で先に書き換わる。再生スレッドは Open 中の形式だけ見る。
	const int dm = ActiveDecodeMode();
	if (dm == INT_MIN)
		return;
	if (!g_pcm_upscale_active || len1 + len2 <= 0) {
		if ((dm >= 10 && dm <= 21) || dm < -10 || dm == -6 || dm == 30 || (dm == 999 && wav999_use_adbuf))
			playwavBuffwav(bufwav3, oldw, len1, len2);
		else if (dm == -10)
			playwavmp3(bufwav3, oldw, len1, len2);
		else if (dm == 999)
			playwavwav(bufwav3, oldw, len1, len2);
		else if (dm == -3)
			playwavkpi(bufwav3, oldw, len1, len2);
		else if (dm == -7)
			playwavdsd(bufwav3, oldw, len1, len2);
		else if (dm == -8)
			playwavflac(bufwav3, oldw, len1, len2);
		else if (dm == -9)
			playwavm4a(bufwav3, oldw, len1, len2);
		else
			playwavds2(bufwav3, oldw, len1, len2);
		return;
	}
	const int total = len1 + len2;
	std::vector<uint8_t> linear((size_t)total);
	int wp = 0;
	int guard = 0;
	while (wp < total && guard < 512) {
		if (IsPlaybackStopRequested())
			break;
		++guard;
		int chunk = total - wp;
		int got = g_audioUpscaler.PullInterleaved(linear.data() + wp, chunk);
		if (got > 0) {
			wp += got;
			continue;
		}
		int sb = g_audioUpscaler.SuggestInputBytes(chunk);
		if (sb < 2048)
			sb = 8192;
		if (sb > (int)g_srcScratchUpscale.capacity())
			g_srcScratchUpscale.reserve((size_t)sb * 2);
		g_srcScratchUpscale.resize((size_t)sb);
		ZeroMemory(g_srcScratchUpscale.data(), sb);
		DecodeSourceIntoScratch(g_srcScratchUpscale.data(), sb);
		g_audioUpscaler.PushInterleaved(g_srcScratchUpscale.data(), sb);
	}
	if (wp < total)
		ZeroMemory(linear.data() + wp, (size_t)(total - wp));
	// アップスケール時も DecodeSourceIntoScratch→playwav* が playb を進める。
	// ここで wp から再度加算すると二重になり時間表示が速くなる（例: MP3 で約2倍）。
	if (len1 > 0)
		memcpy(bufwav3 + oldw, linear.data(), (size_t)len1);
	if (len2 > 0)
		memcpy(bufwav3, linear.data() + len1, (size_t)len2);
}

int voldsf;
int timingf, timerf1;
int uTimerId;
void CALLBACK TimeCallback(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);
COggDlg* og;

CWnd* CCC_GetActiveMainWindow()
{
	extern CMediaPlayerDlg* mp;
	if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
		return mp;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		return og;
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		return mp;
	return nullptr;
}

static char* adbuf, * adbuf2;
BOOL thend1;
BOOL videoonly;
STARTUPINFO si;
PROCESS_INFORMATION pi;
int spc;
int killw1 = 0, ttt_;
CString ext[150][300];
BYTE kvar[150][300];
BYTE kpiarch[150];
IKpiDecoderModule* v5mo;
CString kpif[400];
TCHAR kpifs[200][64];
BOOL kpichk[200];
int kpicnt;

// forward declarations
static WORD GetPeMachine(const CString& path);
static int ResolveKpiArchBits(const CString& kpiPath, const CString& mediaPathIn);
static CString KpiArchLabel(int archBits);

/////////////////////////////////////////////////////////////////////////////
// COggDlg メッセージ ハンドラ
extern CString ndd;
ITaskbarList3* ptl;
ICustomDestinationList* pcdl;
IObjectCollection* poc;

int plcnt = 0;
extern int gameon;

void MpPersistSavedataQuick()
{
	TCHAR tmp[1024];
	_tgetcwd(tmp, 1000);
	_tchdir(karento2);
	CFile ab;
#if _UNICODE
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
	_tchdir(tmp);
}

void MpPushPlayHistory(LPCTSTR path, LPCTSTR displayName)
{
	if (!path || !*path) return;
	CString p = NormalizePlaylistPath(path);
	if (p.IsEmpty()) p = path;
	p.Trim();
	CString n(displayName ? displayName : _T(""));
	n.Trim();
	if (p.IsEmpty()) return;
	if (n.IsEmpty()) {
		int slash = p.ReverseFind(_T('\\'));
		n = (slash >= 0) ? p.Mid(slash + 1) : p;
	}
	// 同一曲は先頭へ
	for (int i = 0; i < savedata.mpHistCnt && i < 8; ++i) {
		if (NormalizePlaylistPath(savedata.mpHistPath[i]).CompareNoCase(p) == 0) {
			for (int j = i; j > 0; --j) {
				_tcscpy(savedata.mpHistPath[j], savedata.mpHistPath[j - 1]);
				_tcscpy(savedata.mpHistName[j], savedata.mpHistName[j - 1]);
			}
			_tcscpy(savedata.mpHistPath[0], p);
			_tcsncpy(savedata.mpHistName[0], n, _countof(savedata.mpHistName[0]) - 1);
			savedata.mpHistName[0][_countof(savedata.mpHistName[0]) - 1] = 0;
			MpPersistSavedataQuick();
			if (savedata.playerMode == 1)
				RefreshTaskbarJumpList(TRUE);
			return;
		}
	}
	const int nMove = (savedata.mpHistCnt < 8) ? savedata.mpHistCnt : 7;
	for (int j = nMove; j > 0; --j) {
		_tcscpy(savedata.mpHistPath[j], savedata.mpHistPath[j - 1]);
		_tcscpy(savedata.mpHistName[j], savedata.mpHistName[j - 1]);
	}
	_tcscpy(savedata.mpHistPath[0], p);
	_tcsncpy(savedata.mpHistName[0], n, _countof(savedata.mpHistName[0]) - 1);
	savedata.mpHistName[0][_countof(savedata.mpHistName[0]) - 1] = 0;
	if (savedata.mpHistCnt < 8)
		savedata.mpHistCnt++;
	MpPersistSavedataQuick();
	if (savedata.playerMode == 1)
		RefreshTaskbarJumpList(TRUE);
}

static volatile LONG s_restartMsgQueued = 0;
static volatile LONG s_restartWanted = 0;

// WM_APP+2(再演奏)を 1 件にまとめる。リストで曲を連打しても stop/play が直列に
// 何十回も走らないようにする(キュー溜めによる UI 固まり対策)。
void RequestPlaybackRestart(HWND hwnd)
{
	if (!hwnd) {
		if (og && ::IsWindow(og->GetSafeHwnd()))
			hwnd = og->GetSafeHwnd();
	}
	if (!hwnd || !::IsWindow(hwnd))
		return;
	InterlockedExchange(&s_restartWanted, 1);
	if (InterlockedCompareExchange(&s_restartMsgQueued, 1, 0) == 0)
		::PostMessage(hwnd, WM_APP + 2, 0, 0);
}

static int MpCurrentPlayIndex()
{
	if (!pl || pl->playcnt <= 0) return -1;
	if (pl->pnt >= 0 && pl->pnt < pl->playcnt) return pl->pnt;
	if (plcnt >= 0 && plcnt < pl->playcnt) return plcnt;
	return 0;
}

void MpTaskbarReplay()
{
	if (pl && pl->playcnt > 0)
		pl->RestoreSavedPlaybackRow();
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());
}

void MpTaskbarNextTrack()
{
	if (!pl || pl->playcnt <= 0) return;
	int idx = MpCurrentPlayIndex();
	if (idx < 0) idx = 0;
	else {
		idx++;
		if (idx >= pl->playcnt) idx = 0;
	}
	pl->Get(idx);
	plcnt = idx;
	gameon = 0;
	MpPushPlayHistory(pl->pc[idx].fol, pl->pc[idx].name);
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());
}

void MpTaskbarPrevTrack()
{
	if (!pl || pl->playcnt <= 0) return;
	int idx = MpCurrentPlayIndex();
	if (idx < 0) idx = 0;
	else {
		idx--;
		if (idx < 0) idx = pl->playcnt - 1;
	}
	pl->Get(idx);
	plcnt = idx;
	gameon = 0;
	MpPushPlayHistory(pl->pc[idx].fol, pl->pc[idx].name);
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());
}

static BOOL MpPlayExistingPlaylistPath(LPCTSTR path)
{
	if (!pl) return FALSE;
	const int idx = pl->FindByPath(path);
	if (idx < 0) return FALSE;
	pl->Get(idx);
	plcnt = idx;
	gameon = 0;
	MpPushPlayHistory(pl->pc[idx].fol, pl->pc[idx].name);
	if (mp && ::IsWindow(mp->GetSafeHwnd()))
		mp->FollowPlayingRow();
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());
	return TRUE;
}

// タスクバー: サムネイルツールバー(再生/停止等)とジャンプリスト(右クリック)をモード別に設定
void SetupTaskbarThumbButtons(HWND hwnd, BOOL mediaPlayerMode)
{
	if (!ptl || !hwnd || !::IsWindow(hwnd)) return;

	static BOOL s_hasThumbOg = FALSE;
	static BOOL s_hasThumbMp = FALSE;
	static HWND s_mpThumbHwnd = NULL;
	BOOL* pHas = &s_hasThumbOg;
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && hwnd == mp->m_hWnd) {
		if (s_mpThumbHwnd != hwnd) { s_hasThumbMp = FALSE; s_mpThumbHwnd = hwnd; }
		pHas = &s_hasThumbMp;
	}

	THUMBBUTTON b[4];
	ZeroMemory(b, sizeof(b));
	b[0].hIcon = ::AfxGetApp()->LoadIcon(IDI_T1); b[0].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
	b[0].dwFlags = THBF_ENABLED; b[0].iId = 0;
	wcscpy(b[0].szTip, L"再演奏");
	b[1].hIcon = ::AfxGetApp()->LoadIcon(IDI_T2); b[1].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
	b[1].dwFlags = THBF_ENABLED; b[1].iId = 1;
	wcscpy(b[1].szTip, L"一時停止");
	b[2].hIcon = ::AfxGetApp()->LoadIcon(IDI_T3); b[2].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
	b[2].dwFlags = THBF_ENABLED; b[2].iId = 2;
	wcscpy(b[2].szTip, L"停止");
	b[3].hIcon = ::AfxGetApp()->LoadIcon(IDI_T4); b[3].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
	b[3].dwFlags = THBF_ENABLED; b[3].iId = 3;
	if (mediaPlayerMode)
		wcscpy(b[3].szTip, L"次の曲");
	else
		wcscpy(b[3].szTip, L"プレイリスト開閉");

	if (*pHas)
		ptl->ThumbBarUpdateButtons(hwnd, 4, b);
	else if (SUCCEEDED(ptl->ThumbBarAddButtons(hwnd, 4, b)))
		*pHas = TRUE;
}

void RefreshTaskbarJumpList(BOOL mediaPlayerMode)
{
	if (!pcdl || !og) return;
	UINT cMinSlots;
	IObjectArray* poaRemoved = NULL;
	if (FAILED(pcdl->BeginList(&cMinSlots, IID_PPV_ARGS(&poaRemoved)))) return;

	IShellLink* psl = NULL;
	auto addHistLink = [&](IObjectCollection* pocHist, LPCTSTR arg, LPCTSTR title) -> bool {
		psl = NULL;
		og->_CreateShellLink((LPTSTR)arg, (LPTSTR)title, &psl, 0, true);
		if (!psl) return false;
		const HRESULT hr = pocHist->AddObject(psl);
		psl->Release();
		psl = NULL;
		return SUCCEEDED(hr);
	};

	// 最近再生した曲は Tasks とは別カテゴリ(ジャンプリスト上部)
	// 壊れた履歴パスはシェル側で ERROR_INVALID_PARAMETER になり得るためスキップ
	if (mediaPlayerMode && savedata.mpHistCnt > 0 && savedata.mpHistCnt <= 8) {
		IObjectCollection* pocHist = NULL;
		if (SUCCEEDED(CoCreateInstance(CLSID_EnumerableObjectCollection, NULL, CLSCTX_INPROC, IID_PPV_ARGS(&pocHist))) && pocHist) {
			UINT histAdded = 0;
			for (int hi = 0; hi < savedata.mpHistCnt && hi < 8; ++hi) {
				const TCHAR* path = savedata.mpHistPath[hi];
				if (!path || path[0] == 0) continue;
				const bool absDrive = (path[1] == _T(':')
					&& (path[2] == _T('\\') || path[2] == _T('/')));
				const bool absUnc = (path[0] == _T('\\') && path[1] == _T('\\'));
				if (!absDrive && !absUnc) continue;
				CString title = savedata.mpHistName[hi];
				if (title.IsEmpty()) title = path;
				if (title.GetLength() > 64)
					title = title.Left(61) + _T("...");
				if (addHistLink(pocHist, path, title))
					histAdded++;
			}
			if (histAdded > 0) {
				IObjectArray* poaHist = NULL;
				if (SUCCEEDED(pocHist->QueryInterface(IID_PPV_ARGS(&poaHist)))) {
					pcdl->AppendCategory(LL14(L"最近再生した曲", L"Recently played", L"Recemment ecoute", L"Riprodotti di recente", L"Reproducido recientemente", L"최근 재생", L"最近播放", L"المشغل مؤخرا", L"Недавно проиграно", L"Zuletzt gespielt", L"Reproduzido recentemente", L"Recent afgespeeld", L"Ostatnio odtwarzane", L"Son calinanlar"), poaHist);
					poaHist->Release();
				}
			}
			pocHist->Release();
		}
	}

	IObjectCollection* pocNew = NULL;
	CoCreateInstance(CLSID_EnumerableObjectCollection, NULL, CLSCTX_INPROC, IID_PPV_ARGS(&pocNew));
	if (!pocNew) { pcdl->AbortList(); if (poaRemoved) poaRemoved->Release(); return; }

	auto addLink = [&](LPCTSTR arg, LPCTSTR title) {
		psl = NULL;
		og->_CreateShellLink((LPTSTR)arg, (LPTSTR)title, &psl, 0, true);
		if (!psl) return;
		pocNew->AddObject(psl);
		psl->Release();
		psl = NULL;
	};
	auto addSep = [&]() {
		psl = NULL;
		og->_CreateShellLink(_T(""), _T(""), &psl, 0, true, FALSE);
		if (!psl) return;
		pocNew->AddObject(psl);
		psl->Release();
		psl = NULL;
	};

	addLink(_T("*1"), _T("再演奏"));
	addLink(_T("*2"), _T("一時停止"));
	addLink(_T("*3"), _T("停止"));
	addSep();
	if (mediaPlayerMode) {
		addLink(_T("*7"), LL14(L"次の曲", L"Next track", L"Piste suivante", L"Traccia succ.", L"Pista sig.", L"다음 곡", L"下一曲", L"التالي", L"Следующий", L"Naechster", L"Proxima", L"Volgende", L"Nastepny", L"Sonraki"));
		addLink(_T("*8"), LL14(L"前の曲", L"Previous track", L"Piste precedente", L"Traccia prec.", L"Pista ant.", L"이전 곡", L"上一曲", L"السابق", L"Предыдущий", L"Vorheriger", L"Anterior", L"Vorige", L"Poprzedni", L"Onceki"));
		addSep();
		addLink(_T("*9"), LL14(L"イコライザー", L"Equalizer", L"Egaliseur", L"Equalizzatore", L"Ecualizador", L"이퀄라이저", L"均衡器", L"المعادل", L"Эквалайзер", L"Equalizer", L"Equalizador", L"Equalizer", L"Korektor", L"Ekolayzer"));
		addLink(_T("*A"), LL14(L"ジャケット", L"Jacket art", L"Pochette", L"Copertina", L"Caratula", L"자켓", L"封面", L"الغلاف", L"Обложка", L"Cover", L"Capa", L"Hoes", L"Okładka", L"Kapak"));
	}
	else {
		addLink(_T("*4"), _T("プレイリスト開閉"));
	}
	addSep();
	addLink(_T("*5"), _T("レンダリング設定"));
	addLink(_T("*6"), _T("フォルダ設定"));

	IObjectArray* poa = NULL;
	if (SUCCEEDED(pocNew->QueryInterface(IID_PPV_ARGS(&poa)))) {
		pcdl->AddUserTasks(poa);
		poa->Release();
	}
	pcdl->CommitList();
	if (poaRemoved) poaRemoved->Release();
	pocNew->Release();
}


BOOL COggDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (HIWORD(wParam) == THBN_CLICKED) {
		UINT CommandID = LOWORD(wParam);
		if (0 == CommandID)
			OnRestart();
		else if (1 == CommandID)
			OnPause();
		else if (2 == CommandID)
			stop();
		else if (3 == CommandID)
			OnPlayList();
	}
	return CCustomBlurDialogBase::OnCommand(wParam, lParam);
}

void COggDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout(this);
		dlgAbout.DoModal();
	}
	else
	{
		CCustomBlurDialogBase::OnSysCommand(nID, lParam);
	}
}

// バージョン情報ダイアログをモーダル表示する(他画面=メディアプレイヤー側から呼ぶ)。
// CAboutDlg は oggDlg.cpp 内のローカルクラスなので、ここで公開ラッパを用意する。
void ShowOggAboutDialog(CWnd* pParent)
{
	CAboutDlg dlgAbout(pParent);
	dlgAbout.DoModal();
}

// バナー値領域の総幅(4x px)。従来は固定ピッチ 5 文字ラベル(8*5)を差し引いて MDC としていた。
#define MDC_TOTAL ((88*2+170)*4)
#define MDC_LABEL_FIXED (8*5*4)
#define MDC (MDC_TOTAL - MDC_LABEL_FIXED)
#define MDCP (88*2+175)*4

// もしダイアログボックスに最小化ボタンを追加するならば、アイコンを描画する
// コードを以下に記述する必要があります。MFC アプリケーションは document/view
// モデルを使っているので、この処理はフレームワークにより自動的に処理されます。

CImageBase* maini;
// システムは、ユーザーが最小化ウィンドウをドラッグしている間、
// カーソルを表示するためにここを呼び出します。
HCURSOR COggDlg::OnQueryDragIcon()
{
	return (HCURSOR)m_hIcon;
}

void COggDlg::Resize()
{
	CString s;
	m_ue.GetWindowText(s);
	if (s == L"▼") {
		m_ue.SetWindowText(_T("▲"));
		CRect rect_1, rect_2;
		GetWindowRect(&rect_1);
		m_ue.GetWindowRect(&rect_2);
		rect_1.bottom = rect_2.bottom + 3;
		rect_1.right = rect_2.right + 5;
		MoveWindow(&rect_1);
		if (maini)
			maini->MoveWindow(&rect_1);
	}
	else {
		m_ue.SetWindowText(_T("▼"));
		CRect rect_1, rect_2;
		GetWindowRect(&rect_1);
		m_sita.GetWindowRect(&rect_2);
		rect_1.bottom = rect_2.bottom + 3;
		rect_1.right = rect_2.right + 5;
		MoveWindow(&rect_1);
		if (maini)
			maini->MoveWindow(&rect_1);
	}
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////M A I N/////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
#include <unknwn.h>
#include "KpiHostClient.h"
#include "KpiV5ConfigStore.h"
BYTE kvver;
IKpiDecoderModule* ob5 = NULL;
const KPI_DECODER_MODULEINFO* m_ModuleInfo5;
const KPI_MEDIAINFO* pMediaInfo = NULL;
KPI_MEDIAINFO me5;
IKpiDecoder* kpidec = NULL;
static bool g_kpiRemote = false;
static int g_kpiPlaybackArch = 0;   // 0=不明 32=x86 64=x64（再生中の KPI arch 表示用）
static KpiHost64Client g_kpiHost;
static KpiHost64Session g_kpiSession;
static std::vector<uint8_t> g_kpiRemoteCache;
static size_t g_kpiRemoteCachePos = 0;
static bool g_kpiRemoteEof = false;

static void ResetKpiRemoteCache()
{
	g_kpiRemoteCache.clear();
	g_kpiRemoteCachePos = 0;
	g_kpiRemoteEof = false;
}

static bool SplitKpiSubsongPath(const CString& in, CString& outPath, uint32_t& outSel)
{
	outPath = in;
	outSel = 1;

	const int len = in.GetLength();
	if (len < 5) return false;
	if (in.GetAt(len - 5) != L':') return false;

	const CString tail = in.Right(4);
	for (int i = 0; i < 4; i++) {
		if (tail[i] < L'0' || tail[i] > L'9') return false;
	}

	outPath = in.Left(len - 6);
	outSel = (uint32_t)_tstoi(tail);
	if (outSel == 0) outSel = 1;
	return true;
}

// WAV 保存用の安全な出力パスを作る。
// filen が "...\foo.kss::0001" 等のサブソング/仮想パスだと、そのまま ".wav" を付けても
// '::' や ':' が Windows のファイル名に使えず Open 失敗→保存できない。
// 物理パス + サニタイズしたトラック識別子 + ".wav" にして、曲ごとに別ファイルへ保存する。
static CString BuildWavExportOutputPath(const CString& filen)
{
	CString base = filen;
	CString subsong;
	const int cor = filen.Find(_T(':'), 6); // ドライブの ':' (index1) は飛ばす
	if (cor != -1) {
		base = filen.Left(cor);
		subsong = filen.Mid(cor);
	}

	CString tag;
	if (!subsong.IsEmpty()) {
		subsong.TrimLeft(_T(':'));
		CString safe;
		for (int i = 0; i < subsong.GetLength(); ++i) {
			TCHAR c = subsong[i];
			if (c == _T('\\') || c == _T('/') || c == _T(':') || c == _T('*') ||
				c == _T('?') || c == _T('"') || c == _T('<') || c == _T('>') || c == _T('|'))
				c = _T('_');
			safe += c;
		}
		safe.Trim();
		if (!safe.IsEmpty())
			tag = _T("_") + safe;
	}

	return base + tag + _T(".wav");
}

static int KpiArchBitsFromIndex(int i)
{
	if (i < 0 || i >= kpicnt) return 0;
	if (kpiarch[i] == 64) return 64;
	if (kpiarch[i] == 32) return 32;
	return 0;
}

static CString KpiArchLabel(int archBits)
{
	if (archBits == 64) return L"x64";
	if (archBits == 32) return L"x86";
	return L"?";
}

// KPI プラグインの CPU アーキテクチャを解決する。
// kpi パス未設定時(プレイリスト復元直後など)は拡張子から kpiarch[] を参照する。
static int ResolveKpiArchBits(const CString& kpiPath, const CString& mediaPathIn)
{
	if (g_kpiRemote) return 64;
	if (!kpiPath.IsEmpty()) {
		const WORD km = GetPeMachine(kpiPath);
		if (km == IMAGE_FILE_MACHINE_AMD64 || km == IMAGE_FILE_MACHINE_ARM64) return 64;
		if (km == IMAGE_FILE_MACHINE_I386) return 32;
		const CString kpiBase = kpiPath.Mid(kpiPath.ReverseFind(L'\\') + 1);
		for (int i = 0; i < kpicnt; ++i) {
			if (kpif[i].CompareNoCase(kpiPath) == 0)
				return KpiArchBitsFromIndex(i);
			const CString kpBase = kpif[i].Mid(kpif[i].ReverseFind(L'\\') + 1);
			if (!kpiBase.IsEmpty() && kpiBase.CompareNoCase(kpBase) == 0)
				return KpiArchBitsFromIndex(i);
		}
	}
	CString mediaPath = mediaPathIn;
	uint32_t sel = 1;
	SplitKpiSubsongPath(mediaPathIn, mediaPath, sel);
	const int dot = mediaPath.ReverseFind(L'.');
	if (dot < 0) return 0;
	CString extPart = mediaPath.Mid(dot);
	extPart.MakeLower();
	for (int i = 0; i < kpicnt; ++i) {
		if (!kpichk[i]) continue;
		for (int j = 0; ; ++j) {
			if (ext[i][j].IsEmpty()) break;
			if (ext[i][j].CompareNoCase(extPart) == 0)
				return KpiArchBitsFromIndex(i);
		}
	}
	return 0;
}

static std::wstring KpiDirOf(const wchar_t* path)
{
	if (!path) return L"";
	std::wstring p(path);
	size_t pos = p.find_last_of(L"\\/");
	if (pos == std::wstring::npos) return L"";
	return p.substr(0, pos + 1);
}

static std::wstring ParentDirOf(const std::wstring& path)
{
	if (path.empty()) return L"";
	size_t end = path.size();
	while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) --end;
	if (end == 0) return L"";
	size_t pos = path.find_last_of(L"\\/", end - 1);
	if (pos == std::wstring::npos) return L"";
	return path.substr(0, pos + 1);
}

static void CollectSubDirsRecursive(const std::wstring& baseDir, int depth, std::vector<std::wstring>& out)
{
	if (depth <= 0 || baseDir.empty()) return;
	std::wstring pat = baseDir;
	if (!pat.empty() && pat.back() != L'\\' && pat.back() != L'/') pat += L'\\';
	pat += L"*";
	WIN32_FIND_DATAW fd{};
	HANDLE h = FindFirstFileW(pat.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
		if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
		std::wstring sub = baseDir;
		if (!sub.empty() && sub.back() != L'\\' && sub.back() != L'/') sub += L'\\';
		sub += fd.cFileName;
		if (!sub.empty() && sub.back() != L'\\' && sub.back() != L'/') sub += L'\\';
		out.push_back(sub);
		CollectSubDirsRecursive(sub, depth - 1, out);
	} while (FindNextFileW(h, &fd));
	FindClose(h);
}

static std::vector<std::wstring> GetExeRelatedDllDirs()
{
	std::vector<std::wstring> dirs;
	wchar_t exePath[MAX_PATH]{};
	if (!GetModuleFileNameW(NULL, exePath, _countof(exePath))) return dirs;
	std::wstring exeDir = KpiDirOf(exePath);
	if (exeDir.empty()) return dirs;
	dirs.push_back(exeDir);
	CollectSubDirsRecursive(exeDir, 3, dirs);
	return dirs;
}

static HMODULE LoadKpiLibraryWithDependencies(const wchar_t* path)
{
	if (!path || !path[0]) return NULL;
	const std::wstring dir = KpiDirOf(path);
	const std::wstring parentDir = ParentDirOf(dir);
	const std::vector<std::wstring> exeDirs = GetExeRelatedDllDirs();
	DLL_DIRECTORY_COOKIE cookie = 0;
	DLL_DIRECTORY_COOKIE cookieParent = 0;
	std::vector<DLL_DIRECTORY_COOKIE> exeCookies;
	if (!dir.empty()) cookie = AddDllDirectory(dir.c_str());
	if (!parentDir.empty()) cookieParent = AddDllDirectory(parentDir.c_str());
	for (size_t i = 0; i < exeDirs.size(); ++i) {
		if (exeDirs[i].empty()) continue;
		DLL_DIRECTORY_COOKIE c = AddDllDirectory(exeDirs[i].c_str());
		if (c) exeCookies.push_back(c);
	}
	HMODULE h = LoadLibraryExW(
		path,
		NULL,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
	);
	if (!h) {
		h = LoadLibraryW(path);
	}
	for (size_t i = exeCookies.size(); i > 0; --i) RemoveDllDirectory(exeCookies[i - 1]);
	if (cookieParent) RemoveDllDirectory(cookieParent);
	if (cookie) RemoveDllDirectory(cookie);
	return h;
}

static HRESULT SafeCreateDecoderModuleInstance(HRESULT(WINAPI* createFn)(REFIID, void**, IKpiUnknown*), void** ppvObject, IKpiUnknown* pUnknown)
{
	HRESULT hr = E_FAIL;
	__try {
		hr = createFn(IID_IKpiDecoderModule, ppvObject, pUnknown);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		hr = E_FAIL;
	}
	return hr;
}

class CMyNullTagInfo : public IKpiTagInfo
{
private:
	long m_cRef;
public:
	CMyNullTagInfo() : m_cRef(1) {}
	virtual ~CMyNullTagInfo() {}

	STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_cRef); }
	STDMETHODIMP_(ULONG) Release() override {
		ULONG ulRef = InterlockedDecrement(&m_cRef);
		if (ulRef == 0) { delete this; }
		return ulRef;
	}
	STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override {
		if (!ppvObject) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IKpiTagInfo) {
			*ppvObject = static_cast<IKpiTagInfo*>(this);
			AddRef();
			return S_OK;
		}
		*ppvObject = NULL;
		return E_NOINTERFACE;
	}
	DWORD WINAPI GetTagInfo(IKpiFile*, IKpiFolder*, DWORD, DWORD) override { return 1; }
	DWORD WINAPI GetValue(const wchar_t*, wchar_t* pszValue, int nSize) override {
		if (pszValue && nSize > 0) pszValue[0] = 0;
		return 0;
	}
	void WINAPI SetOverwrite(BOOL) override {}
	void WINAPI SetPicture(DWORD, const wchar_t*, const wchar_t*, const wchar_t*, DWORD, DWORD, const BYTE*, DWORD) override {}
	void WINAPI aSetValueA(const char*, int, const char*, int) override {}
	void WINAPI aSetValueW(const char*, int, const wchar_t*, int) override {}
	void WINAPI aSetValueU8(const char*, int, const char*, int) override {}
	void WINAPI wSetValueA(const wchar_t*, int, const char*, int) override {}
	void WINAPI wSetValueW(const wchar_t*, int, const wchar_t*, int) override {}
	void WINAPI wSetValueU8(const wchar_t*, int, const char*, int) override {}
	void WINAPI u8SetValueA(const char*, int, const char*, int) override {}
	void WINAPI u8SetValueW(const char*, int, const wchar_t*, int) override {}
	void WINAPI u8SetValueU8(const char*, int, const char*, int) override {}
};

static DWORD SafeKpiDecoderSelectInner(IKpiDecoder* dec, DWORD songNo, const KPI_MEDIAINFO** ppMediaInfo, IKpiTagInfo* pTagInfo)
{
	DWORD selected = 0;
	__try {
		selected = dec->Select(songNo, ppMediaInfo, pTagInfo, KPI_TAGGET_FLAG_NONE);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		selected = 0;
	}
	return selected;
}

static DWORD SafeKpiDecoderSelect(IKpiDecoder* dec, DWORD songNo, const KPI_MEDIAINFO** ppMediaInfo)
{
	CMyNullTagInfo tagInfo;
	return SafeKpiDecoderSelectInner(dec, songNo, ppMediaInfo, &tagInfo);
}

/**
 * @brief ホスト側が実装する、実ファイル操作のための IKpiFile クラス
 * (AddRef/Release/QueryInterface を明示的に実装した完全版)
 */
class CMyHostFile : public IKpiFile
{
private:
	long m_cRef;

	// 1. ハンドルベース I/O 用
	HANDLE m_hFile;

	// 2. GetBuffer 用 (オンデマンドで読み込む)
	BYTE* m_pDataBuffer;
	UINT64 m_dwBufferSize;

	// 3. ファイル情報
	wchar_t m_wszFileName[MAX_PATH];
	wchar_t m_wszFullFilePath[MAX_PATH];

public:
	CMyHostFile() : m_cRef(1), m_hFile(INVALID_HANDLE_VALUE),
		m_pDataBuffer(NULL), m_dwBufferSize(0)
	{
		m_wszFileName[0] = L'\0';
		m_wszFullFilePath[0] = L'\0';
	}

protected:
	virtual ~CMyHostFile() {
		if (m_hFile != INVALID_HANDLE_VALUE) {
			CloseHandle(m_hFile);
		}
		if (m_pDataBuffer) {
			delete[] m_pDataBuffer; // GetBuffer 用のメモリを解放
		}
	}

public:
	// --- IUnknown (IKpiUnknown) の実装 ---
	STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
	STDMETHODIMP_(ULONG) Release() {
		ULONG ulRef = InterlockedDecrement(&m_cRef);
		if (ulRef == 0) { delete this; }
		return ulRef;
	}
	STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
		if (!ppvObject) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IKpiFile) {
			*ppvObject = static_cast<IKpiFile*>(this);
			AddRef();
			return S_OK;
		}
		*ppvObject = NULL;
		return E_NOINTERFACE;
	}

	// --- CMyHostFile の初期化 (ハンドルを開いて保持) ---
	BOOL Open(const wchar_t* lpszFilePath)
	{
		if (m_hFile != INVALID_HANDLE_VALUE) {
			CloseHandle(m_hFile);
		}
		if (m_pDataBuffer) {
			delete[] m_pDataBuffer;
			m_pDataBuffer = NULL;
			m_dwBufferSize = 0;
		}

		// ファイル情報を保存
		wcscpy_s(m_wszFullFilePath, MAX_PATH, lpszFilePath);
		const wchar_t* pFileNameOnly = wcsrchr(lpszFilePath, L'\\');
		pFileNameOnly = pFileNameOnly ? pFileNameOnly + 1 : lpszFilePath;
		wcscpy_s(m_wszFileName, MAX_PATH, pFileNameOnly);

		// ハンドルを開く (kpi 2.0 用)
		m_hFile = CreateFileW(
			lpszFilePath, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
		);

		return (m_hFile != INVALID_HANDLE_VALUE);
	}

	// --- IKpiFile の実装 (ハンドルベース) ---
	virtual DWORD WINAPI Read(void* pBuffer, DWORD dwSize)
	{
		if (m_hFile == INVALID_HANDLE_VALUE) return 0;
		DWORD dwRead = 0;
		ReadFile(m_hFile, pBuffer, dwSize, &dwRead, NULL);
		return dwRead;
	}

	virtual UINT64 WINAPI Seek(INT64 i64Pos, DWORD dwOrigin)
	{
		if (m_hFile == INVALID_HANDLE_VALUE) return KPI_FILE_EOF;
		LARGE_INTEGER liPos, liNewPos;
		liPos.QuadPart = i64Pos;
		DWORD dwMoveMethod;
		if (dwOrigin == FILE_BEGIN) dwMoveMethod = FILE_BEGIN;
		else if (dwOrigin == FILE_CURRENT) dwMoveMethod = FILE_CURRENT;
		else if (dwOrigin == FILE_END) dwMoveMethod = FILE_END;
		else return KPI_FILE_EOF;

		if (SetFilePointerEx(m_hFile, liPos, &liNewPos, dwMoveMethod)) {
			return liNewPos.QuadPart;
		}
		return KPI_FILE_EOF;
	}

	virtual UINT64 WINAPI GetSize(void)
	{
		if (m_hFile == INVALID_HANDLE_VALUE) return KPI_FILE_EOF;
		LARGE_INTEGER liSize;
		if (GetFileSizeEx(m_hFile, &liSize)) {
			return liSize.QuadPart;
		}
		return KPI_FILE_EOF;
	}

	// ★★★ GetBuffer (★オンデマンドで読み込む) ★★★
	virtual BOOL WINAPI GetBuffer(const BYTE** ppBuffer, size_t* pstSize)
	{
		if (!ppBuffer || !pstSize) return FALSE;

		// 既に読み込み済みなら、それを返す
		if (m_pDataBuffer) {
			*ppBuffer = m_pDataBuffer;
			*pstSize = (size_t)m_dwBufferSize;
			return TRUE;
		}

		// --- 初回呼び出し時: メモリに読み込む ---
		if (m_hFile == INVALID_HANDLE_VALUE) return FALSE;

		UINT64 fileSize = GetSize();
		if (fileSize == KPI_FILE_EOF || fileSize == 0) return FALSE;
#include <new>
		m_dwBufferSize = fileSize;
		m_pDataBuffer = new (std::nothrow) BYTE[(size_t)m_dwBufferSize];
		if (m_pDataBuffer == NULL) {
			m_dwBufferSize = 0;
			return FALSE; // メモリ確保失敗
		}

		// ファイルポインタを先頭に戻す
		UINT64 currentPos = Seek(0, FILE_CURRENT);
		Seek(0, FILE_BEGIN);

		// メモリに一括読み込み
		DWORD dwRead = 0;
		if (!ReadFile(m_hFile, m_pDataBuffer, (DWORD)m_dwBufferSize, &dwRead, NULL) || dwRead != m_dwBufferSize) {
			// 読み込み失敗
			delete[] m_pDataBuffer;
			m_pDataBuffer = NULL;
			m_dwBufferSize = 0;
			Seek(currentPos, FILE_BEGIN); // ポインタを元に戻す
			return FALSE;
		}

		// ポインタを元に戻す
		Seek(currentPos, FILE_BEGIN);

		// 成功
		*ppBuffer = m_pDataBuffer;
		*pstSize = (size_t)m_dwBufferSize;
		return TRUE;
	}


	// ★★★ CreateClone (★未実装のまま) ★★★
	virtual BOOL WINAPI CreateClone(IKpiFile** ppFile)
	{
		// クローン非対応（これが原因のプラグインもあるかもしれない）
		*ppFile = NULL;
		return FALSE;
	}

	// --- 他のメソッド (変更なし) ---
	virtual DWORD WINAPI GetFileName(wchar_t* pszName, DWORD dwSize)
	{
		if (!pszName || dwSize < 2) return 0;
		if (m_wszFileName[0] == L'\0') { *pszName = L'\0'; return 0; }
		DWORD len = (DWORD)(wcslen(m_wszFileName) + 1) * sizeof(wchar_t);
		if (dwSize < len) { *pszName = L'\0'; }
		else { wcscpy_s(pszName, dwSize / sizeof(wchar_t), m_wszFileName); }
		return len;
	}

	virtual BOOL WINAPI GetRealFileW(const wchar_t** ppszFileNameW)
	{
		if (!ppszFileNameW) return FALSE;
		if (m_wszFullFilePath[0] == L'\0') { *ppszFileNameW = NULL; return FALSE; }
		*ppszFileNameW = this->m_wszFullFilePath;
		return TRUE;
	}

	virtual BOOL WINAPI GetRealFileA(const char** ppszFileNameA) { *ppszFileNameA = NULL; return FALSE; }
	virtual BOOL WINAPI Abort(void) { return FALSE; }
};

class CMyDummyFolder : public IKpiFolder // IKpiFolder を直接継承
{
private:
	long m_cRef;

public:
	CMyDummyFolder() : m_cRef(1) {} // 参照カウントを1で初期化

protected:
	virtual ~CMyDummyFolder() {}

public:
	// --- IUnknown (IKpiUnknown) の実装 ---
	STDMETHODIMP_(ULONG) AddRef()
	{
		return InterlockedIncrement(&m_cRef);
	}

	STDMETHODIMP_(ULONG) Release()
	{
		ULONG ulRef = InterlockedDecrement(&m_cRef);
		if (ulRef == 0)
		{
			delete this;
		}
		return ulRef;
	}

	STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject)
	{
		if (!ppvObject) return E_POINTER;

		// IUnknown と IKpiFolder の両方に応答する
		if (riid == IID_IUnknown || riid == IID_IKpiFolder) {
			*ppvObject = static_cast<IKpiFolder*>(this);
			AddRef(); // QI成功時はAddRef
			return S_OK;
		}

		*ppvObject = NULL;
		return E_NOINTERFACE;
	}

	// --- IKpiFolder のダミー実装 (中身は空でよい) ---

	virtual DWORD WINAPI GetFolderName(wchar_t* pszName, DWORD dwSize)
	{
		if (pszName && dwSize >= 2) *pszName = L'\0';
		return 0; // 空文字列
	}

	virtual DWORD WINAPI EnumFiles(DWORD dwIndex, wchar_t* pszName, DWORD dwSize, DWORD dwLevel)
	{
		return 0; // ファイルなし
	}

	virtual BOOL WINAPI OpenFile(const wchar_t* cszName, IKpiFile** ppFile)
	{
		if (ppFile) *ppFile = NULL;
		return FALSE; // 開けない
	}

	virtual BOOL WINAPI OpenFolder(const wchar_t* cszName, IKpiFolder** ppFolder)
	{
		if (ppFolder) *ppFolder = NULL;
		return FALSE; // 開けない
	}
};

// ホストクラスの実装 (IKpiUnknown と IKpiUnkProvider を両方実装)
// (IKpiUnkProvider::CreateInstance が返すためのダミー設定オブジェクト)
class CMyDummyConfig : public IKpiConfig
{
	long m_cRef;
	std::wstring m_pluginName;
public:
	CMyDummyConfig(const wchar_t* pluginName) : m_cRef(1), m_pluginName(pluginName ? pluginName : L"") {}
	virtual ~CMyDummyConfig() {}

	// --- IUnknown (IKpiUnknown) の実装 ---
	STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) {
		if (!ppvObject) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IKpiConfig) {
			*ppvObject = static_cast<IKpiConfig*>(this);
			AddRef();
			return S_OK;
		}
		*ppvObject = NULL;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
	STDMETHODIMP_(ULONG) Release() {
		ULONG ulRef = InterlockedDecrement(&m_cRef);
		if (ulRef == 0) { delete this; }
		return ulRef;
	}

	// --- IKpiConfig のダミーメソッド ---
	// (呼ばれるかもしれないので、ダミーでも実装しておく)
	virtual void WINAPI SetInt(const wchar_t* sec, const wchar_t* key, INT64 v) {
		KpiV5SetInt(m_pluginName, sec ? sec : L"", key ? key : L"", v);
	}
	virtual INT64 WINAPI GetInt(const wchar_t* sec, const wchar_t* key, INT64 nDefault) {
		// kbpsf2: 既定で volume タグを無視 → m_volume==1 → nBits=16 (minipsf2 と .psf2 で同じ Gen 経路)
		INT64 def = nDefault;
		if (_wcsicmp(m_pluginName.c_str(), L"kbpsf2") == 0 &&
			sec && key &&
			_wcsicmp(sec, L"General") == 0 &&
			_wcsicmp(key, L"IgnoreVolumeTag") == 0) {
			def = 1;
		}
		return KpiV5GetInt(m_pluginName, sec ? sec : L"", key ? key : L"", def);
	}
	virtual void WINAPI SetFloat(const wchar_t* sec, const wchar_t* key, double v) {
		KpiV5SetFloat(m_pluginName, sec ? sec : L"", key ? key : L"", v);
	}
	virtual double WINAPI GetFloat(const wchar_t* sec, const wchar_t* key, double dDefault) {
		if (key && sec && _wcsicmp(key, L"Volume") == 0 && _wcsicmp(sec, L"General") == 0) {
			const wchar_t* pn = m_pluginName.c_str();
			if (_wcsicmp(pn, L"kbvgm") == 0 || _wcsicmp(pn, L"kbfmoplmidi") == 0)
				return 1.0;
			return 100.0;
		}
		return KpiV5GetFloat(m_pluginName, sec ? sec : L"", key ? key : L"", dDefault);
	}
	virtual void WINAPI SetStr(const wchar_t* sec, const wchar_t* key, const wchar_t* value) {
		KpiV5SetStr(m_pluginName, sec ? sec : L"", key ? key : L"", value ? value : L"");
	}
	virtual DWORD WINAPI GetStr(const wchar_t* sec, const wchar_t* key, wchar_t* pszValue, DWORD dwSize, const wchar_t* cszDefault) {
		const std::wstring value = KpiV5GetStr(m_pluginName, sec ? sec : L"", key ? key : L"", cszDefault ? cszDefault : L"");
		const DWORD need = (DWORD)((value.size() + 1) * sizeof(wchar_t));
		if (pszValue && dwSize >= sizeof(wchar_t)) {
			if (dwSize >= need) {
				wcscpy_s(pszValue, dwSize / sizeof(wchar_t), value.c_str());
			}
			else {
				pszValue[0] = L'\0';
			}
		}
		return need;
	}
	virtual void WINAPI SetBin(const wchar_t* sec, const wchar_t* key, const BYTE* p, DWORD size) {
		KpiV5SetBin(m_pluginName, sec ? sec : L"", key ? key : L"", p, size);
	}
	virtual DWORD WINAPI GetBin(const wchar_t* sec, const wchar_t* key, BYTE* p, DWORD dwSize) {
		return KpiV5GetBin(m_pluginName, sec ? sec : L"", key ? key : L"", p, dwSize);
	}
};

// --- 2. ホストクラスの実装 (IKpiUnkProvider を実装) ---
// (kpi_CreateInstance の第3引数に渡すオブジェクト)
class CMyHost : public IKpiUnkProvider
{
private:
	long m_cRef;
	std::wstring m_pluginName;

public:
	CMyHost(const wchar_t* kpiPath) : m_cRef(1), m_pluginName(KpiV5PluginNameFromPath(kpiPath ? kpiPath : L"")) {}
	virtual ~CMyHost() {}

	// --- IUnknown (IKpiUnknown) の実装 ---
	STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject)
	{
		if (!ppvObject) return E_POINTER;
		*ppvObject = NULL;

		if (riid == IID_IUnknown) {
			*ppvObject = static_cast<IKpiUnknown*>(this);
			AddRef();
			return S_OK;
		}

		// ★最重要★
		// kpi_CreateConfig が要求する IID_IKpiUnkProvider に応答
		if (riid == IID_IKpiUnkProvider) {
			*ppvObject = static_cast<IKpiUnkProvider*>(this);
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	STDMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }
	STDMETHODIMP_(ULONG) Release() {
		ULONG ulRef = InterlockedDecrement(&m_cRef);
		if (ulRef == 0) { delete this; }
		return ulRef;
	}

	// --- IKpiUnkProvider の実装 ---
	STDMETHODIMP_(DWORD) CreateInstance(REFIID riid,
		void* pvParam1, // pGUID
		void* pvParam2, // pdwPlatform
		void* pvParam3, // NULL
		void* pvParam4, // NULL
		void** ppvObj)
	{
		if (!ppvObj) return 0; // 失敗
		*ppvObj = NULL;

		// ★最重要★
		// kpi_CreateConfig (ヘルパー) が要求してくる IID_IKpiConfig に応答
		if (riid == IID_IKpiConfig)
		{
			// ダミーの設定オブジェクトを生成して返す
			*ppvObj = new CMyDummyConfig(m_pluginName.c_str());

			if (pvParam2) // pdwPlatform
			{
				*(DWORD*)pvParam2 = 0; // 0 = 本体が直接呼び出し
			}
			return 1; // 成功 (戻り値は 0 以外)
		}

		return 0; // 失敗
	}
};


//PCMWAVEFORMAT fm;

/* WAVEファイルのヘッダ */
typedef struct {
	char ckidRIFF[4];
	int ckSizeRIFF;
	char fccType[4];
	char ckidFmt[4];
	int ckSizeFmt;
	PCMWAVEFORMAT WaveFmt;
	char ckidData[4];
	int ckSizeData;
} WAVEFILEHEADER;
WAVEFILEHEADER wh;

int mcopy(char* a, int len);
static int McopyAccumulate(char* dst, int wantBytes);
long LoadOggVorbis(const TCHAR* file_name, int word, char** ogg, CSliderCtrl& m_time);
void ReleaseOggVorbis(char**);
void DoEvent();
VOID CALLBACK waveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2);
BOOL AllocOutputBuffer();
void FreeOutputBuffer(void);
void playwav();
void playwavds(char* bw);

CDouga* pMainFrame1 = NULL;
OggVorbis_File vf;
DWORD hw;
PCMWAVEFORMAT p;
CFile cc;
CString filen = _T("");
CSemaphore	m_Smp;

PCMWAVEFORMAT    wfx;
UINT ttt;
int cc1, t, oggsize, dd, loop1, loop2, loop1_2;//,oggsize1,oggsize2;
__int64 wl; // 書き出し累積バイト（G:合計時間）。2GB超WAV(RF64)対応のため64bit
__int64 playb;
// 曲終端のジャスト検出用（DirectSound 再生カーソル基準）。すべて「DS バッファへ書き込んだ総バイト数」の単位。
//   g_dsWrittenBytes  : play() 開始以降に DS バッファへ書き込んだ累積バイト数（単調増加）
//   g_endWrittenBytes : 0=未終端 / >0=実音声が終わる絶対書込みバイト位置（EOF 検出時に確定）
//   g_outBytesPerFrame: 出力 1 フレームのバイト数（短フェード尺の計算用に UI 側から共有）
__int64 g_dsWrittenBytes = 0;
__int64 g_endWrittenBytes = 0;
__int64 g_heardBytes = 0;       // 実際に再生カーソルが消化した累積バイト数（DS スレッドが毎サイクル更新）
int g_outBytesPerFrame = 4;
int ru2 = 0, ru;
int lo, loc, endf, ps = 0, locs;
int poss = 0, poss2 = 0, poss3 = 0, poss4 = 0, poss5 = 0, poss6 = 0, loopcnt, pl_no;

// OGG+RB: デコード位置（入力 PCM サンプル）。playb は出力側、こちらは ov_read 進行用。
static __int64 g_oggPcmDecodePos = 0;
// ループ/シーク直後: リングに溜める最低バイト数（RB レイテンシ吸収）。0 なら不要。
static int g_oggRbPrimingNeed = 0;

static inline int OggRbLatencyReserveBytes()
{
	const int bpf = PcmOutBytesPerFrame();
	if (bpf <= 0 || wavbit_sample_Hz <= 0)
		return 8192;
	// 約 80ms 分を常時リングに残す（32k で ~2560 サンプル = 10KB stereo16）
	return bpf * wavbit_sample_Hz * 80 / 1000;
}

static inline void OggFlushKpi3Ring()
{
	poss2 = 0;
	poss3 = 0;
	poss4 = 0;
}

// 32kHz 等の低レート向け。44.1kHz はループ点がシームレスなことが多く、
// クロスフェード/リングプリフィルは loop1 のダブりを招く。
static inline bool OggUseLowRateLoopExtras()
{
	return (wavbit_sample_Hz > 0 && wavbit_sample_Hz < 44100);
}

// loop2 末尾と loop1 先頭の境界で短いクロスフェード（32k の RB 段差向け）
static void OggApplyLoopBoundaryCrossfade(char* pcmBase, int tailBytes, int headBytes)
{
	if (!OggUseLowRateLoopExtras())
		return;
	if (!pcmBase || tailBytes < 4 || headBytes < 4)
		return;
	const int bpf = PcmOutBytesPerFrame();
	if (bpf <= 0 || wavchannel <= 0)
		return;
	tailBytes -= tailBytes % bpf;
	headBytes -= headBytes % bpf;
	if (tailBytes < bpf || headBytes < bpf)
		return;

	int fadeFrames = (wavbit_sample_Hz > 0) ? (wavbit_sample_Hz / 250) : 128;
	if (fadeFrames < 24)
		fadeFrames = 24;
	if (fadeFrames > 256)
		fadeFrames = 256;

	const int tailFrames = tailBytes / bpf;
	const int headFrames = headBytes / bpf;
	if (fadeFrames > tailFrames)
		fadeFrames = tailFrames;
	if (fadeFrames > headFrames)
		fadeFrames = headFrames;
	if (fadeFrames <= 1)
		return;

	short* pcm = (short*)pcmBase;
	const int ch = wavchannel;

	for (int i = 0; i < fadeFrames; ++i) {
		const float t = (float)(i + 1) / (float)(fadeFrames + 1);
		const float w = t * t * (3.0f - 2.0f * t); // smoothstep
		const int tailFrame = tailFrames - fadeFrames + i;
		const int headFrame = tailFrames + i;
		for (int c = 0; c < ch; ++c) {
			const int ti = tailFrame * ch + c;
			const int hi = headFrame * ch + c;
			const float tailS = (float)pcm[ti];
			const float headS = (float)pcm[hi];
			pcm[ti] = (short)(tailS * (1.0f - w));
			pcm[hi] = (short)(tailS * (1.0f - w) + headS * w);
		}
	}
}

// bufkpi3 リングの poss2/3/4 がずれるとヒープ外書き込み→別スレッドでまちまち AV になる。mcopy 入口で正規化。
static inline void SanitizeKpi3RingState(int maxBufBytes)
{
	if (maxBufBytes <= 0)
		return;
	if (poss2 < 0 || poss3 < 0 || poss4 < 0 ||
		poss2 >= maxBufBytes || poss3 >= maxBufBytes || poss4 > maxBufBytes) {
		poss2 = 0;
		poss3 = 0;
		poss4 = 0;
	}
}

int current_section;
long whsize;
int ret2;

// WAV出力（再生なし）用
CString wavExportPath;
int wavExportLoopCount = 0;
// WAV書き出しの実データバイト数(64bit)。ヘッダ確定/成否判定はファイル実長から求める。
__int64 g_wavExportDataBytes = 0;

//#define OUTPUT_BUFFER_NUM  10
//#define BUFSZ			(4096*6)
#define OUTPUT_BUFFER_SIZE  BUFSZ
#define BUFSZ1 (2048*8)
#define BUFSZH1			(BUFSZ1/HIGHDIV)
BYTE bufwav[OUTPUT_BUFFER_SIZE * 3];
BYTE buf[OUTPUT_BUFFER_NUM][OUTPUT_BUFFER_SIZE];
LPWAVEHDR  g_OutputBuffer[OUTPUT_BUFFER_NUM];
long data_size;

CString ti;
CString s, ss;
int tt = 0;
int killw;
ULONG PlayCursora, WriteCursora;
double oggsize2 = 0;
BYTE bufwav3[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 8];


char abuf[28];
char bbuf[2048];
ULONGLONG gp; int sep;
int lenl = 0;


WCHAR douga[2050];
extern IGraphBuilder* pGraphBuilder;
extern IMediaControl* pMediaControl;
extern IMediaPosition* pMediaPosition;
CString ply = _T("");
int plym = -1;

SOUNDINFO si1;
int kbps = 0;
int Vbr = 0;
DWORD cnt3 = 0;

int loop3;
CString tagname, tagfile, tagalbum;
CString tagtrack;   // 曲番号(トラック番号)。全形式でタグから補完して表示に使用

void ApplyPlaylistRowDisplay(const playlistdata0& row)
{
	tagfile = row.name;
	tagname = row.art;
	tagalbum = row.alb;
	tagtrack = _T("");
	stitle = _T("");
	const int sub = row.sub;
	// タグ/ゲーム曲はプレイリスト保存名を停止中の表示に使う(再生開始で上書き)
	if (sub == -1 || sub == -6 || sub == 999 || sub == 21 ||
		(sub >= 1 && sub <= 30) || sub == -11 || sub == -12 || sub == -13 || sub == -14 || sub == -15) {
		if (row.name[0])
			stitle = row.name;
	}
	extern CMediaPlayerDlg* mp;
	if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd())) {
		mp->InvalidateSidePanels();
		if (!mp->m_bannerRect.IsRectEmpty())
			mp->InvalidateRect(&mp->m_bannerRect, FALSE);
	}
}

int playy = 1;
void st1();
void st2();
int bufzero = 0;
extern 	int syukai;
extern int syukai2;

int horizontalDPI;
int ms2;
int endflg = 0;

static inline int Ms2FrameUnits()
{
	int ms = savedata.ms2;
	if (ms < 16) ms = 16;
	if (ms > 960) ms = 960;
	return (ms + 15) / 16;
}

static inline BOOL Ms2DrawDue(int frameCounter)
{
	return frameCounter >= Ms2FrameUnits();
}

int tempo;
int pitch;

int stflg = TRUE;
int eqflg = TRUE;


UINT TheadLoop(LPVOID l);
BOOL drawth = FALSE;
LARGE_INTEGER freq;
//////////////////////////////////////////////////////////////////////////////
DWORD g_oggUiThreadId = 0;
static volatile LONG g_timerpPosted = 0;
static volatile LONG g_gdiPaintPending = 0;
static volatile LONG g_speanaPosted = 0;
static DWORD g_gdiPaintPendingSince = 0;

// メディアプレイヤーモードでメイン画面(og)が非表示の間は OnPaint が呼ばれず
// g_gdiPaintPending が下がらないため timerp の GDI 合成(bGdiFrame)が止まる。
// メディアプレイヤー側が dc を Blit した後にこれを呼んで合成を継続させる。
void COgg_ClearGdiPaintPending()
{
	InterlockedExchange(&g_gdiPaintPending, 0);
	g_gdiPaintPendingSince = 0;
}

LONG COgg_GetGdiPaintPending()
{
	return InterlockedCompareExchange(&g_gdiPaintPending, 0, 0);
}

static void COgg_RequestTimerp(COggDlg* dlg)
{
	if (!dlg)
		return;
	HWND h = dlg->GetSafeHwnd();
	if (!h || !::IsWindow(h))
		return;
	if (InterlockedCompareExchange(&g_timerpPosted, 1, 0) != 0)
		return;
	::PostMessage(h, WM_TIMERP_VSYNC_TICK, 0, 0);
}

BOOL COggDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	// メディアプレイヤーモード起動時は OnInitDialog 中もメイン画面を出さない
	if (savedata.playerMode == 1)
		ShowWindow(SW_HIDE);
	g_oggUiThreadId = GetCurrentThreadId();
	ms2 = 0;
	QueryPerformanceFrequency(&freq);
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	// Get desktop dc
	sflg = FALSE;
	CDC* desktopDc = GetDC();
	// Get native resolution
	horizontalDPI = GetDeviceCaps(desktopDc->m_hDC, LOGPIXELSX);
	hD = (float)(horizontalDPI) / (96.0f);
	if (hD < 1.02f) hD = 1.02f;
	hD /= 4.0f;
	SetStretchBltMode(dc.m_hDC, COLORONCOLOR);
	ReleaseDC(desktopDc);
	plw = 0;
	pi.dwProcessId = -1;
	randomf = 0; hsc = 0; spc = 0;
	wavbit_sample_Hz = 44100; wavbit2 = 44100; wavchannel = 2;
	g_ds_pcm_ch = 2; g_ds_pcm_rate = 44100; g_ds_pcm_bits = 16;
	fade1 = 0;
	thend1 = TRUE;
	videoonly = FALSE;
	kpicnt = 0;
	kpi[0] = 0;
	mod = NULL; kmp = NULL;
	extn = "";
	mod = NULL;
	kmp = NULL;
	kmp1 = NULL;
	aa1_ = 0.0;
	hDLLk = NULL;
	mp3_.mp3init();


	m_tempo_sl.SetMode(1);
	m_pitch_sl.SetMode(1);

	// "バージョン情報..." メニュー項目をシステム メニューへ追加します。
	fnn = "";
	// IDM_ABOUTBOX はコマンド メニューの範囲でなければなりません。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// このダイアログ用のアイコンを設定します。フレームワークはアプリケーションのメイン
	// ウィンドウがダイアログでない時は自動的に設定しません。
	SetIcon(m_hIcon, TRUE);			// 大きいアイコンを設定
	SetIcon(m_hIcon, FALSE);		// 小さいアイコンを設定
	SetWindowText(LL14(L"mp3/m4a簡易プレイヤ Ver 0.9a", L"mp3/m4a Simple Player Ver 0.9a", L"mp3/m4a Lecteur simple Ver 0.9a", L"mp3/m4a Lettore semplice Ver 0.9a", L"mp3/m4a Reproductor simple Ver 0.9a", L"mp3/m4a 간이 플레이어 Ver 0.9a", L"mp3/m4a 简易播放器 Ver 0.9a", L"mp3/m4a مشغل بسيط Ver 0.9a", L"mp3/m4a Простой плеер Ver 0.9a", L"mp3/m4a Einfacher Player Ver 0.9a", L"mp3/m4a Player simples Ver 0.9a", L"mp3/m4a Eenvoudige speler Ver 0.9a", L"mp3/m4a Prosty odtwarzacz Ver 0.9a", L"mp3/m4a Basit oynat?c? Ver 0.9a"));
	SetDlgItemText(IDC_BUTTON8, LL14(L"Ys6 ナピシュテム", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"이스6 나피쉬팀", L"伊苏6", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim"));
	SetDlgItemText(IDC_BUTTON7, LL14(L"Ys フェルガナ", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"이스 펠가나", L"伊苏菲尔盖纳", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana"));
	SetDlgItemText(IDC_BUTTON15, LL14(L"Ys オリジン", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"이스 오리진", L"伊苏起源", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin"));
	SetDlgItemText(IDC_BUTTON6, LL14(L"空の軌跡FC", L"Trails in the Sky FC", L"Les Sentiers du Ciel FC", L"Trails in the Sky FC", L"Trails in the Sky FC", L"하늘의 궤적 FC", L"空之轨迹FC", L"Trails in the Sky FC", L"Тропы в Небе FC", L"Himmelsleitern FC", L"Trails in the Sky FC", L"Trails in the Sky FC", L"Trails in the Sky FC", L"Trails in the Sky FC"));
	SetDlgItemText(IDC_BUTTON2, LL14(L"空の軌跡SC", L"Trails in the Sky SC", L"Les Sentiers du Ciel SC", L"Trails in the Sky SC", L"Trails in the Sky SC", L"하늘의 궤적 SC", L"空之轨迹SC", L"Trails in the Sky SC", L"Тропы в Небе SC", L"Himmelsleitern SC", L"Trails in the Sky SC", L"Trails in the Sky SC", L"Trails in the Sky SC", L"Trails in the Sky SC"));
	SetDlgItemText(IDC_BUTTON17, LL14(L"空の軌跡The3rd", L"Trails in the Sky The 3rd", L"Les Sentiers du Ciel The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"하늘의 궤적 The 3rd", L"空之轨迹The3rd", L"Trails in the Sky The 3rd", L"Тропы в Небе The 3rd", L"Himmelsleitern The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd"));
	SetDlgItemText(IDC_BUTTON19, L"ZWEI II");
	SetDlgItemText(IDC_BUTTON23, L"Ys I&&II Chronicles 1");
	SetDlgItemText(IDC_BUTTON24, L"Ys I&&II Chronicles 2");
	SetDlgItemText(IDC_BUTTON25, L"XANADU NEXT");
	SetDlgItemText(IDC_BUTTON27, LL14(L"Ys 完全版 Ys1", L"Ys Complete Ys1", L"Ys Integral Ys1", L"Ys Complete Ys1", L"Ys Completo Ys1", L"이스 완전판 Ys1", L"伊苏完全版 Ys1", L"Ys النسخة الكاملة Ys1", L"Ys Complete Ys1", L"Ys Complete Ys1", L"Ys Completo Ys1", L"Ys Complete Ys1", L"Ys Complete Ys1", L"Ys Complete Ys1"));
	SetDlgItemText(IDC_BUTTON28, LL14(L"Ys 完全版 Ys2", L"Ys Complete Ys2", L"Ys Integral Ys2", L"Ys Complete Ys2", L"Ys Completo Ys2", L"이스 완전판 Ys2", L"伊苏完全版 Ys2", L"Ys النسخة الكاملة Ys2", L"Ys Complete Ys2", L"Ys Complete Ys2", L"Ys Completo Ys2", L"Ys Complete Ys2", L"Ys Complete Ys2", L"Ys Complete Ys2"));
	SetDlgItemText(IDC_BUTTON31, L"Sorcerian Original");
	SetDlgItemText(IDC_BUTTON33, L"Zwei!!");
	SetDlgItemText(IDC_BUTTON35, LL14(L"ぐるみん", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"그루민", L"咕噜小天使", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin"));
	SetDlgItemText(IDC_BUTTON37, LL14(L"ダイナソア", L"Dinosaur", L"Dinosaure", L"Dinosauro", L"Dinosaurio", L"공룡", L"恐龙", L"ديناصور", L"Динозавр", L"Dinosaurier", L"Dinossauro", L"Dinosaurus", L"Dinozaur", L"Dinozor"));
	SetDlgItemText(IDC_BUTTON4, LL14(L"再演奏", L"Replay", L"Relecture", L"Ripeti", L"Repetir", L"다시 재생", L"重新播放", L"إعادة التشغيل", L"Повтор", L"Erneut abspielen", L"Repetir", L"Opnieuw afspelen", L"Odtworz ponownie", L"Tekrar cal"));
	SetDlgItemText(IDC_BUTTON3, LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시 정지", L"暂停", L"إيقاف مؤقت", L"Пауза", L"Pause", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"));
	SetDlgItemText(IDC_BUTTON1, LL14(L"停止", L"Stop", L"Arret", L"Stop", L"Detener", L"중지", L"停止", L"إيقاف", L"Стоп", L"Stop", L"Parar", L"Stoppen", L"Stop", L"Durdur"));
	SetDlgItemText(IDC_CHECK1, LL14(L"スペアナ", L"Spectrum", L"Spectre", L"Spettro", L"Espectro", L"스펙트럼", L"频谱", L"طيف", L"Спектр", L"Spektrum", L"Espectro", L"Spectrum", L"Widmo", L"Spektrum"));
	SetDlgItemText(IDC_BUTTON5, LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza", L"Desvanecer", L"페이드 아웃", L"淡出", L"تلاشي", L"Затухание", L"Ausblenden", L"Desvanecer", L"Fade out", L"Zanikanie", L"Solukla?t?r"));
	SetDlgItemText(IDC_BUTTON21, LL14(L"設定", L"Settings", L"Parametres", L"Impostazioni", L"Ajustes", L"설정", L"设置", L"الإعدادات", L"Настройки", L"Einstellungen", L"Configuracoes", L"Instellingen", L"Ustawienia", L"Ayarlar"));
	SetDlgItemText(IDC_BUTTON9, LL14(L"フォルダ設定", L"Folder settings", L"Parametres dossier", L"Impostazioni cartella", L"Configuracion carpeta", L"폴더 설정", L"文件夹设置", L"إعدادات المجلد", L"Настройки папки", L"Ordnereinstellungen", L"Config. pasta", L"Mapinstellingen", L"Ustawienia folderu", L"Klasor ayarlar?"));
	SetDlgItemText(IDOK, LL14(L"終了", L"Exit", L"Quitter", L"Esci", L"Salir", L"종료", L"退出", L"خروج", L"Выход", L"Beenden", L"Sair", L"Afsluiten", L"Zako?cz", L"C?k??"));
	SetDlgItemText(IDC_CHECK5, LL14(L"ランダム再生", L"Random play", L"Lecture aleatoire", L"Riproduzione casuale", L"Reproduccion aleatoria", L"랜덤 재생", L"随机播放", L"تشغيل عشوائي", L"Случайное воспроизведение", L"Zufallswiedergabe", L"Reproducao aleatoria", L"Willekeurig afspelen", L"Losowe odtwarzanie", L"Rastgele calma"));
	SetDlgItemText(IDC_CHECK6, LL14(L"順次再生", L"Sequential play", L"Lecture sequentielle", L"Riproduzione sequenziale", L"Reproduccion secuencial", L"순차 재생", L"顺序播放", L"تشغيل متسلسل", L"Последовательное воспроизведение", L"Sequentielle Wiedergabe", L"Reproducao sequencial", L"Sequentieel afspelen", L"Kolejne odtwarzanie", L"S?ral? calma"));
	SetDlgItemText(IDC_BUTTON14, LL14(L"演奏開始", L"Play", L"Lecture", L"Riproduci", L"Reproducir", L"재생", L"播放", L"تشغيل", L"Воспроизведение", L"Abspielen", L"Reproduzir", L"Afspelen", L"Odtworz", L"Cal"));

	SetDlgItemText(IDC_CHECK7, LL14(L"YS6 ナピシュテムの匣", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 纳比斯汀的方舟", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako"));
	SetDlgItemText(IDC_CHECK8, LL14(L"YS フェルガナの誓い", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys 菲尔盖纳之誓", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai"));
	SetDlgItemText(IDC_CHECK9, LL14(L"英雄伝説6空の軌跡FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legende des Heros VI Les Sentiers du Ciel FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"영웅전설6 하늘의 궤적 FC", L"英雄传说6 空之轨迹FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC"));
	SetDlgItemText(IDC_CHECK10, LL14(L"英雄伝説6空の軌跡SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legende des Heros VI Les Sentiers du Ciel SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"영웅전설6 하늘의 궤적 SC", L"英雄传说6 空之轨迹SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC"));
	SetDlgItemText(IDC_CHECK11, LL14(L"イース・オリジン", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"이스 오리진", L"伊苏起源", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin"));
	SetDlgItemText(IDC_CHECK12, LL14(L"英雄伝説6空の軌跡TC", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legende des Heros VI Les Sentiers du Ciel The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"영웅전설6 하늘의 궤적 The 3rd", L"英雄传说6 空之轨迹The3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd"));
	SetDlgItemText(IDC_CHECK13, L"ZWEI II");
	SetDlgItemText(IDC_CHECK14, L"Ys Chronicles Ys 1");
	SetDlgItemText(IDC_CHECK15, L"Ys Chronicles Ys 2");
	SetDlgItemText(IDC_CHECK16, L"XANADU NEXT");
	SetDlgItemText(IDC_CHECK17, LL14(L"Ys 完全版 Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys 完全版 Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1"));
	SetDlgItemText(IDC_CHECK18, LL14(L"Ys 完全版 Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys 完全版 Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2"));
	SetDlgItemText(IDC_CHECK19, L"Sorcerian Original");
	SetDlgItemText(IDC_CHECK20, L"Zwei!!");
	SetDlgItemText(IDC_CHECK21, LL14(L"ぐるみん", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"그루민", L"咕噜小天使", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin"));
	SetDlgItemText(IDC_CHECK22, LL14(L"ダイナソア リザレクション", L"Dinosaur Resurrection", L"Resurrection Dinosaure", L"Resurrezione Dinosauro", L"Resurreccion Dinosaurio", L"공룡 부활", L"恐龙复活", L"ديناصور القيامة", L"Динозавр: Воскрешение", L"Dinosaurier Auferstehung", L"Ressurreicao Dinossauro", L"Dinosaurus Herrijzenis", L"Dinozaur Zmartwychwstanie", L"Dinozor Dirili?"));
	SetDlgItemText(IDC_STATICaaad, LL14(L"ループ回数", L"Loop count", L"Nombre de boucles", L"Conteggio loop", L"Cuenta de bucle", L"루프 횟수", L"循环次数", L"عدد الحلقات", L"Количество повторов", L"Schleifenzahler", L"Contagem de loop", L"Loopaantal", L"Liczba p?tli", L"Dongu say?s?"));
	SetDlgItemText(IDC_CHECK2, LL14(L"WAVファイルへ保存", L"Save to WAV file", L"Enregistrer en WAV", L"Salva come WAV", L"Guardar como WAV", L"WAV 파일로 저장", L"保存到WAV文件", L"حفظ كـ WAV", L"Сохранить в WAV", L"Als WAV speichern", L"Salvar como WAV", L"Opslaan als WAV", L"Zapisz jako WAV", L"WAV olarak kaydet"));
	SetDlgItemText(IDC_CHECK3, LL14(L"動画も表示する", L"Show video", L"Afficher video", L"Mostra video", L"Mostrar video", L"동영상 표시", L"显示视频", L"عرض الفيديو", L"Показывать видео", L"Video anzeigen", L"Mostrar video", L"Video tonen", L"Poka? wideo", L"Videoyu goster"));
	SetDlgItemText(IDC_STATICaaab, LL14(L"主音量", L"Master volume", L"Volume principal", L"Volume master", L"Volumen maestro", L"마스터 볼륨", L"主音量", L"مستوى الصوت الرئيسي", L"Общая громкость", L"Hauptlautstarke", L"Volume mestre", L"Hoofdvolume", L"G?o?no?? g?owna", L"Ana ses"));
	SetDlgItemText(IDC_STATICaaa, LL14(L"DirectSound音量", L"DirectSound volume", L"Volume DirectSound", L"Volume DirectSound", L"Volumen DirectSound", L"DirectSound 볼륨", L"DirectSound音量", L"مستوى DirectSound", L"Громкость DirectSound", L"DirectSound-Lautstarke", L"Volume DirectSound", L"DirectSound-volume", L"G?o?no?? DirectSound", L"DirectSound sesi"));
	SetDlgItemText(IDC_STATICaaac, LL14(L"拡張音量", L"Extended volume", L"Volume etendu", L"Volume esteso", L"Volumen extendido", L"확장 볼륨", L"扩展音量", L"مستوى الصوت الممتد", L"Доп. громкость", L"Erweiterte Lautstarke", L"Volume estendido", L"Uitgebreid volume", L"G?o?no?? rozszerzona", L"Geni?letilmi? ses"));
	SetDlgItemText(IDC_STATIC_t, LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"템포", L"速度", L"الإيقاع", L"Темп", L"Tempo", L"Andamento", L"Tempo", L"Tempo", L"Tempo"));
	SetDlgItemText(IDC_STATIC_p, LL14(L"ピッチ", L"Pitch", L"Hauteur", L"Tono", L"Tono", L"피치", L"音高", L"درجة الصوت", L"Высота тона", L"Tonhohe", L"Tom", L"Toonhoogte", L"Wysoko??", L"Perde"));
	SetDlgItemText(IDC_BUTTON39, L"Brandish4");
	SetDlgItemText(IDC_CHECK23, LL14(L"ブランディッシュ４", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish 4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4"));
	SetDlgItemText(IDC_BUTTON44, LL14(L"白き魔女", L"White Witch", L"Sorciere Blanche", L"Strega Bianca", L"Bruja Blanca", L"흰 마녀", L"白之魔女", L"الساحرة البيضاء", L"Белая Ведьма", L"Weise Hexe", L"Bruxa Branca", L"Witte Heks", L"Bia?a Czarownica", L"Beyaz Cad?"));
	SetDlgItemText(IDC_BUTTON45, LL14(L"朱紅い雫", L"Tear of Vermillion", L"Larme de Vermillon", L"Lacrima di Vermiglio", L"Lagrima de Bermellon", L"주홍의 눈물", L"朱红之泪", L"دمعة فيرميليون", L"Слеза Вермиллиона", L"Trane der Purpur", L"Lagrima de Vermelho", L"Traan van Vermiljoen", L"?za Vermillion", L"Vermilyon Gozya??"));
	SetDlgItemText(IDC_BUTTON46, LL14(L"海の檻歌", L"Cagesong of the Ocean", L"Chant des Profondeurs", L"Canto dell'Oceano", L"Cantico del Oceano", L"바다의 감옥가", L"海之槛歌", L"أغنية محيط الأقفاص", L"Песнь Океана", L"Kafiglied des Ozeans", L"Cantico do Oceano", L"Kooi van de Oceaan", L"Pie?? Oceanu", L"Okyanus Kafes ?ark?s?"));
	SetDlgItemText(IDC_CHECK24, LL14(L"英雄伝説III 白き魔女", L"Legend of Heroes III White Witch", L"Legende des Heros III Sorciere Blanche", L"Legend of Heroes III Strega Bianca", L"Legend of Heroes III Bruja Blanca", L"영웅전설III 흰 마녀", L"英雄传说III 白之魔女", L"Legend of Heroes III White Witch", L"Legend of Heroes III White Witch", L"Legend of Heroes III Weise Hexe", L"Legend of Heroes III Bruxa Branca", L"Legend of Heroes III White Witch", L"Legend of Heroes III Bia?a Czarownica", L"Legend of Heroes III White Witch"));
	SetDlgItemText(IDC_CHECK25, LL14(L"英雄伝説IV 朱紅い雫", L"Legend of Heroes IV Tear of Vermillion", L"Legende des Heros IV Larme de Vermillon", L"Legend of Heroes IV Lacrima di Vermiglio", L"Legend of Heroes IV Lagrima de Bermellon", L"영웅전설IV 주홍의 눈물", L"英雄传说IV 朱红之泪", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Trane der Purpur", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Tear of Vermillion"));
	SetDlgItemText(IDC_CHECK26, LL14(L"英雄伝説V 海の檻歌", L"Legend of Heroes V Cagesong of the Ocean", L"Legende des Heros V Chant des Profondeurs", L"Legend of Heroes V Canto dell'Oceano", L"Legend of Heroes V Cantico del Oceano", L"영웅전설V 바다의 감옥가", L"英雄传说V 海之槛歌", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Kafiglied des Ozeans", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Cagesong of the Ocean"));
	SetDlgItemText(IDC_BUTTON47, LL14(L"月影", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"月影", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI"));
	SetDlgItemText(IDC_BUTTON48, LL14(L"西風", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"西风", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi"));
	SetDlgItemText(IDC_BUTTON51, LL14(L"アーク", L"Arc", L"Arc", L"Arc", L"Arc", L"아크", L"弧", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc"));
	SetDlgItemText(IDC_BUTTON53, LL14(L"三国志1", L"San1", L"San1", L"San1", L"San1", L"삼국지1", L"三国志1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1"));
	SetDlgItemText(IDC_BUTTON54, LL14(L"三国志2", L"San2", L"San2", L"San2", L"San2", L"삼국지2", L"三国志2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2"));
	SetDlgItemText(IDC_BUTTON57, LL14(L"プレイリスト", L"Playlist", L"Liste de lecture", L"Playlist", L"Lista de reproduccion", L"재생 목록", L"播放列表", L"قائمة التشغيل", L"Плейлист", L"Wiedergabeliste", L"Lista de reproducao", L"Afspeellijst", L"Lista odtwarzania", L"Calma listesi"));
	SetDlgItemText(IDC_BUTTON58, LL14(L"ジャケ", L"Cover", L"Pochette", L"Copertina", L"Caratula", L"커버", L"封面", L"الغلاف", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Ok?adka", L"Kapak"));
	SetDlgItemText(IDC_OGG_SWITCHMODE, LL14(L"MP画面", L"MP screen", L"Ecran MP", L"Schermo MP", L"Pantalla MP", L"MP화면", L"MP画面", L"شاشة MP", L"Экран MP", L"MP-Ansicht", L"Tela MP", L"MP-scherm", L"Ekran MP", L"MP ekran?"));

	// TODO: 特別な初期化を行う時はこの場所に追加してください。
	//フォント設定
	LOGFONT LogFont;
	CClientDC dc1(this);
	dc1.GetCurrentFont()->GetLogFont(&LogFont);
	bool has_font1 = false;
	if (_tcslen(savedata.font1) > 0) {
		has_font1 = DeserializeLogFont(savedata.font1, &LogFont);
	}
	if (has_font1) {
		LogFont.lfHeight *= 4;
		LogFont.lfWidth *= 4;
	} else {
		if (_tcslen(savedata.font1) > 0) {
			_tcscpy(LogFont.lfFaceName, savedata.font1);
		} else {
			_tcscpy(LogFont.lfFaceName, _T("ＭＳ ゴシック"));
		}
		LogFont.lfHeight = 16 * 4;
		LogFont.lfWidth = 8 * 4;
		LogFont.lfQuality = DRAFT_QUALITY;
		LogFont.lfWeight = FW_ULTRABOLD;
	}
	hFont = CreateFontIndirect(&LogFont);
	/*	hFont = CreateFont(16,8,0,0,500,FALSE,FALSE,FALSE,
	SHIFTJIS_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,
	DRAFT_QUALITY,FIXED_PITCH | FF_MODERN,
	_T("Arphic Gothic JIS"));
	if(hFont==NULL)
	hFont = CreateFont(16,8,0,0,500,FALSE,FALSE,FALSE,
	SHIFTJIS_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,
	DRAFT_QUALITY,FIXED_PITCH  | FF_MODERN,
	_T("ＭＳ ゴシック"));
	*/

	ogg = NULL; wav = NULL; adbuf2 = NULL;
	plf = 0;
	timeBeginPeriod(1);
	SetTimer(5656, 2000, NULL);
	SetTimer(5657, 50, NULL);
	StartTimerpVsyncThread();
	SetTimer(6555, 1200, NULL);
	SetTimer(9000, 10, NULL);
	timingf = timerf1 = 0;
	stf = 1;
	m_dou.SetCheck(1);
	// CG: 以下のブロックはツールヒント コンポーネントによって追加されました
	COSVersion os;
	DWORD edition;
	OSVERSIONINFOEX in;
	BOOL dumy;
	os.GetVersionInfo(in, edition, dumy);
	CString cpus;
	char CPUBrandString[0x40];
	int CPUInfo[4] = { -1 };
	__cpuid(CPUInfo, 0x80000002);
	memcpy(CPUBrandString, CPUInfo, sizeof(CPUInfo));
	__cpuid(CPUInfo, 0x80000003);
	memcpy(CPUBrandString + 16, CPUInfo, sizeof(CPUInfo));
	__cpuid(CPUInfo, 0x80000004);
	memcpy(CPUBrandString + 32, CPUInfo, sizeof(CPUInfo));
	cpus = CPUBrandString;

	int i;
	for (i = 0; i < 300; i++) { spelv[i] = 0; spetm[i] = 0; }
	fade = 1.0;
	fadeadd = 0.0;

	CString s;
	s.Format(_T("%d"), savedata.kaisuu);
	m_kaisuu.SetWindowText(s);

	//画面位置
	if (savedata.xx != -10000) {
		MoveWindow(savedata.xx, savedata.yy, 1, 1);
	}

	Resize();

	if (savedata.random) {
		m_junji.SetCheck(1);
		m_random.SetCheck(0);
	}
	else {
		m_junji.SetCheck(0);
		m_random.SetCheck(1);
	}
	m_ys6.SetCheck(savedata.gameflg[0]);
	m_ysf.SetCheck(savedata.gameflg[1]);
	m_ed6fc.SetCheck(savedata.gameflg[2]);
	m_ed6sc.SetCheck(savedata.gameflg[3]);
	m_yso.SetCheck(savedata.gameflg2);
	m_ed6tc.SetCheck(savedata.gameflg3);
	m_zweiii.SetCheck(savedata.gameflg4);
	m_ysc1.SetCheck(savedata.gameflg5);
	m_ysc2.SetCheck(savedata.gameflg6);
	m_xa.SetCheck(savedata.gameflg7);
	m_ys121.SetCheck(savedata.gameflg8);
	m_ys122.SetCheck(savedata.gameflg9);
	m_sor.SetCheck(savedata.gameflg10);
	m_zwei.SetCheck(savedata.gameflg11);
	m_gurumin.SetCheck(savedata.gameflg12);
	m_dino.SetCheck(savedata.gameflg13);
	m_br4.SetCheck(savedata.gameflg14);
	m_ed3.SetCheck(savedata.gameflg15);
	m_ed4.SetCheck(savedata.gameflg16);
	m_ed5.SetCheck(savedata.gameflg17);

	m_dsval.ShowWindow(SW_HIDE);
	m_dsval.SetRange(-498, 1);
	m_dsval.SetPos(-200);
	//	m_dsval.SetPos(savedata.dsvol);
	voldsf = 1;

	cdc0 = GetDC(); //new CClientDC(this);
	dc.CreateCompatibleDC(NULL);
	dcsub.CreateCompatibleDC(NULL);
	bmp.CreateCompatibleBitmap(cdc0, 2000, 1000);
	bmpsub.CreateCompatibleBitmap(cdc0, 5000, 100);
	dc.SelectObject(&bmp);
	dc.FillSolidRect(0, 0, 3000, 1000, RGB(0, 0, 0));
	dcsub.SelectObject(&bmpsub);
	dcsub.FillSolidRect(0, 0, 6000, 399, RGB(0, 0, 0));
	ReleaseDC(cdc0);
	mode = modesub = 0;
	m_supe.SetCheck(savedata.supe);
	m_st.SetCheck(savedata.supe2);
	mcnt = mcnt1 = mcnt2 = mcnt3 = mcnt4 = mcnt5 = mcnt6 = 0;
	m_time.SetRange(0, 1);
	m_time.SetSelection(0, 1);

	m_lpDS3DBuffer = NULL;
	if (WASAPIInit() == 0) init(GetSafeHwnd());


	RegisterHotKey(GetSafeHwnd(), ID_HOTKEY0, 0, VK_UP);
	RegisterHotKey(GetSafeHwnd(), ID_HOTKEY1, 0, VK_DOWN);
	RegisterHotKey(GetSafeHwnd(), ID_HOTKEY2, 0, VK_RIGHT);
	RegisterHotKey(GetSafeHwnd(), ID_HOTKEY3, 0, VK_LEFT);

	ptl = NULL;
	CoInitialize(NULL);
	CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_ITaskbarList3, (void**)&ptl);
	if (ptl) {
		ptl->HrInit();
		ptl->SetProgressState(m_hWnd, TBPF_NOPROGRESS | TBPF_NORMAL);
		SetTimer(5219, 200, NULL);
	}
	pcdl = NULL;
	CoCreateInstance(CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER, IID_ICustomDestinationList, (void**)&pcdl);
	if (pcdl) {
		RefreshTaskbarJumpList(savedata.playerMode == 1);
	}

	m_pDlgColor = NULL;



	//Windows7 / Vista用 ボリュームチェンジ
	deve = NULL; dev = NULL; audio = NULL;
	g_epVolCb = nullptr;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&deve))) {
		if (FAILED(deve->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) || !dev) {
			deve->Release(); deve = NULL;
		}
		else if (FAILED(dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&audio)) || !audio) {
			dev->Release(); dev = NULL;
			deve->Release(); deve = NULL;
		}
		else {
			float lv = 0.0f;
			audio->GetMasterVolumeLevelScalar(&lv);
			m_sl.SetRange(0, 100000);
			m_sl.SetPos((int)((float)lv * 100000.0f + 0.5f));
			s_lastAppliedVol = lv * 100.0f;
			s_lastUsedEndpoint = true;
			g_epVolCb = new CEndpointVolCallback(m_hWnd);
			if (FAILED(audio->RegisterControlChangeNotify(g_epVolCb))) {
				g_epVolCb->Release();
				g_epVolCb = nullptr;
			}
		}
	}
	if (!deve) {
		deve = NULL;
		m_sl.SetRange(0, 1000);
		m_sl.SetPos(600);
		DWORD vol = GetVol();
		union a {
			DWORD vol2;
			WORD b[2];
		} volu;
		volu.vol2 = vol;
		DWORD c = (DWORD)volu.b[0];
		float d = (float)c; d = d / 65535.0f;
		d = 1000.0f * d + 1.0f;
		m_sl.SetPos((int)d);
		s_lastUsedEndpoint = false;
	}

	SetTimer(5211, 20, NULL);
	SetTimer(9998, 1000, NULL);
	Modec();


	m_tempo_sl.SetRange(0, 400);
	m_tempo_sl.SetPos(200);
	m_pitch_sl.SetRange(0, 400);
	m_pitch_sl.SetPos(200);
	tempo = 200;
	pitch = 200;

	ttt_ = 5;
	//	uTimerId = timeSetEvent(1, 0, TimeCallback, NULL, TIME_PERIODIC);
#if WIN64
#else
	CKpiLoadingWnd loadingWnd;
	loadingWnd.Create(NULL);
	loadingWnd.Show();
	g_pActiveLoadingWnd = &loadingWnd;
	g_nCurrentKpiIndex = 0;
	plug(karento2, NULL);
	g_pActiveLoadingWnd = NULL;
	loadingWnd.DestroyWindow();
	// 読み込み中に届いた再生要求(WM_APP+1)を今から処理する。
	// PostMessage なので OnInitDialog 完了後に通常のメッセージループで実行される。
	if (g_kpiLoadDeferredPlay) {
		g_kpiLoadDeferredPlay = FALSE;
		PostMessage(WM_APP + 1, 0, 0);
	}
#endif
	WAVEFORMATEX wfx1;
	wfx1.wFormatTag = WAVE_FORMAT_PCM;
	wfx1.nChannels = 2;
	wfx1.nSamplesPerSec = 48000;
	wfx1.wBitsPerSample = 24;
	wfx1.nBlockAlign = wfx1.nChannels * wfx1.wBitsPerSample / 8;
	wfx1.nAvgBytesPerSec = wfx1.nSamplesPerSec * wfx1.nBlockAlign;
	wfx1.cbSize = 0;

	waveOutOpen(&hwo, WAVE_MAPPER, &wfx1, NULL, NULL, CALLBACK_NULL);

	/////////////////////////////////

	s.Format(_T("%s"), os.GetVersionString());
	m_OS.SetWindowText(s);
	__cpuid(CPUInfo, 0x00000001);
	CString avx2;
	avx2 = LL14(L"SSE2未対応", L"SSE2 not supported", L"SSE2 non pris en charge", L"SSE2 non supportato", L"SSE2 no compatible", L"SSE2 미지원", L"SSE2 不支持", L"SSE2 غير مدعوم", L"SSE2 не поддерживается", L"SSE2 nicht unterstützt", L"SSE2 não suportado", L"SSE2 niet ondersteund", L"SSE2 nieobsługiwane", L"SSE2 desteklenmiyor");

	if (CPUInfo[0] >= 2) {
		__cpuid(CPUInfo, 0x00000001);
		if (CPUInfo[3] & (1 << 26))  avx2 = LL14(L"SSE2対応", L"SSE2 supported", L"SSE2 pris en charge", L"SSE2 supportato", L"SSE2 compatible", L"SSE2 지원", L"SSE2 支持", L"SSE2 مدعوم", L"SSE2 поддерживается", L"SSE2 unterstützt", L"SSE2 suportado", L"SSE2 ondersteund", L"SSE2 obsługiwane", L"SSE2 destekleniyor");
		if (CPUInfo[2] & (1))  avx2 = LL14(L"SSE3対応", L"SSE3 supported", L"SSE3 pris en charge", L"SSE3 supportato", L"SSE3 compatible", L"SSE3 지원", L"SSE3 支持", L"SSE3 مدعوم", L"SSE3 поддерживается", L"SSE3 unterstützt", L"SSE3 suportado", L"SSE3 ondersteund", L"SSE3 obsługiwane", L"SSE3 destekleniyor");
		if (CPUInfo[2] & (1 << 9))  avx2 = LL14(L"SSSE3対応", L"SSSE3 supported", L"SSSE3 pris en charge", L"SSSE3 supportato", L"SSSE3 compatible", L"SSSE3 지원", L"SSSE3 支持", L"SSSE3 مدعوم", L"SSSE3 поддерживается", L"SSSE3 unterstützt", L"SSSE3 suportado", L"SSSE3 ondersteund", L"SSSE3 obsługiwane", L"SSSE3 destekleniyor");
		if (CPUInfo[2] & (1 << 12))  avx2 = LL14(L"FMA3対応", L"FMA3 supported", L"FMA3 pris en charge", L"FMA3 supportato", L"FMA3 compatible", L"FMA3 지원", L"FMA3 支持", L"FMA3 مدعوم", L"FMA3 поддерживается", L"FMA3 unterstützt", L"FMA3 suportado", L"FMA3 ondersteund", L"FMA3 obsługiwane", L"FMA3 destekleniyor");
		if (CPUInfo[2] & (1 << 19))  avx2 = LL14(L"SSE4.1対応", L"SSE4.1 supported", L"SSE4.1 pris en charge", L"SSE4.1 supportato", L"SSE4.1 compatible", L"SSE4.1 지원", L"SSE4.1 支持", L"SSE4.1 مدعوم", L"SSE4.1 поддерживается", L"SSE4.1 unterstützt", L"SSE4.1 suportado", L"SSE4.1 ondersteund", L"SSE4.1 obsługiwane", L"SSE4.1 destekleniyor");
		if (CPUInfo[2] & (1 << 20))  avx2 = LL14(L"SSE4.2対応", L"SSE4.2 supported", L"SSE4.2 pris en charge", L"SSE4.2 supportato", L"SSE4.2 compatible", L"SSE4.2 지원", L"SSE4.2 支持", L"SSE4.2 مدعوم", L"SSE4.2 поддерживается", L"SSE4.2 unterstützt", L"SSE4.2 suportado", L"SSE4.2 ondersteund", L"SSE4.2 obsługiwane", L"SSE4.2 destekleniyor");
	}

	if (CPUInfo[0] >= 2) {
		__cpuid(CPUInfo, 0x80000001);
		if (CPUInfo[2] & (1 << 6))  avx2 = LL14(L"SSE4a対応", L"SSE4a supported", L"SSE4a pris en charge", L"SSE4a supportato", L"SSE4a compatible", L"SSE4a 지원", L"SSE4a 支持", L"SSE4a مدعوم", L"SSE4a поддерживается", L"SSE4a unterstützt", L"SSE4a suportado", L"SSE4a ondersteund", L"SSE4a obsługiwane", L"SSE4a destekleniyor");
	}

	__cpuid(CPUInfo, 0x00000001);
	if (CPUInfo[0] >= 2) {
		if (CPUInfo[2] & (1 << 28))  avx2 = LL14(L"AVX対応", L"AVX supported", L"AVX pris en charge", L"AVX supportato", L"AVX compatible", L"AVX 지원", L"AVX 支持", L"AVX مدعوم", L"AVX поддерживается", L"AVX unterstützt", L"AVX suportado", L"AVX ondersteund", L"AVX obsługiwane", L"AVX destekleniyor");
	}

	if (CPUInfo[0] >= 7) {
		__cpuid(CPUInfo, 0x00000007);
		if (CPUInfo[1] & (1 << 5))  avx2 = LL14(L"AVX2対応", L"AVX2 supported", L"AVX2 pris en charge", L"AVX2 supportato", L"AVX2 compatible", L"AVX2 지원", L"AVX2 支持", L"AVX2 مدعوم", L"AVX2 поддерживается", L"AVX2 unterstützt", L"AVX2 suportado", L"AVX2 ondersteund", L"AVX2 obsługiwane", L"AVX2 destekleniyor");
		if (CPUInfo[1] & (1 << 16))  avx2 = LL14(L"AVX512対応", L"AVX512 supported", L"AVX512 pris en charge", L"AVX512 supportato", L"AVX512 compatible", L"AVX512 지원", L"AVX512 支持", L"AVX512 مدعوم", L"AVX512 поддерживается", L"AVX512 unterstützt", L"AVX512 suportado", L"AVX512 ondersteund", L"AVX512 obsługiwane", L"AVX512 destekleniyor");
	}
	s.Format(_T("%s / %s"), cpus, avx2);
	s.Trim();
	m_cpu.SetWindowText(s);

	// CPU 拡張命令一覧
	avx2 = LL14(L"使用可能命令：", L"Available instructions: ", L"Instructions disponibles : ", L"Istruzioni disponibili: ", L"Instrucciones disponibles: ", L"사용 가능 명령: ", L"可用指令：", L"التعليمات المتاحة: ", L"Доступные инструкции: ", L"Verfugbare Befehle: ", L"Instrucoes disponiveis: ", L"Beschikbare instructies: ", L"Dost?pne instrukcje: ", L"Kullan?labilir talimatlar: ");
	__cpuid(CPUInfo, 0x00000001);
	if (CPUInfo[0] >= 2) {
		if (CPUInfo[3] & (1 << 23))  avx2 += "MMX ";
		if (CPUInfo[3] & (1 << 25))  avx2 += "SSE ";
		if (CPUInfo[3] & (1 << 26))  avx2 += "SSE2 ";
		if (CPUInfo[2] & (1))        avx2 += "SSE3 ";
		if (CPUInfo[2] & (1 << 9))   avx2 += "SSSE3 ";
		if (CPUInfo[2] & (1 << 12))  avx2 += "FMA3 ";
		if (CPUInfo[2] & (1 << 19))  avx2 += "SSE4.1 ";
		if (CPUInfo[2] & (1 << 20))  avx2 += "SSE4.2 ";
	}
	if (CPUInfo[0] >= 2) {
		__cpuid(CPUInfo, 0x80000001);
		if (CPUInfo[2] & (1 << 6))  avx2 += "SSE4a ";
	}
	__cpuid(CPUInfo, 0x00000001);
	if (CPUInfo[0] >= 2) {
		if (CPUInfo[2] & (1 << 28))  avx2 += "AVX ";
	}
	if (CPUInfo[0] >= 7) {
		__cpuid(CPUInfo, 0x00000007);
		if (CPUInfo[1] & (1 << 5))  avx2 += "AVX2 ";
		if (CPUInfo[1] & (1 << 16))  avx2 += "AVX512 ";
	}
	m_os3.SetWindowText(avx2);


	m_kakuVol.SetRange(100, 900);
	s.Format(_T("%.1f%%"), (double)savedata.kakuVal);
	m_kakuVolval.SetWindowText(s);
	m_kakuVol.SetPos(savedata.kakuVol);
	CKpilist kp;
	kp.status = 1;
	kp.Init();
	kp.Save();

	CFont* curFont;
	curFont = m_cpu.GetFont();
	LOGFONTW mylf;
	curFont->GetLogFont(&mylf);
	mylf.lfHeight = (LONG)(16 * hD * 3);
	mylf.lfWidth = (LONG)(7 * hD * 3);
	mylf.lfWeight = FW_NORMAL;
	m_newFont = new CFont;
	m_newFont->CreateFontIndirectW(&mylf);
	m_cpu.SetFont(m_newFont);

	curFont = m_os3.GetFont();
	curFont->GetLogFont(&mylf);
	mylf.lfHeight = (LONG)(16 * hD * 3);
	mylf.lfWidth = (LONG)(5 * hD * 3);
	mylf.lfWeight = FW_NORMAL;
	m_newFont1 = new CFont;
	m_newFont1->CreateFontIndirectW(&mylf);
	m_os3.SetFont(m_newFont1);

	//
	SetTimer(15011, 200, NULL);

	m_lrc.SetWindowText(L"");
	m_lrc2.SetWindowText(LL14(L"歌詞(.lrc)が表示されます", L"Lyrics (.lrc) will be displayed here", L"Paroles (.lrc) affichees ici", L"Testi (.lrc) visualizzati qui", L"Letra (.lrc) mostrada aqui", L"가사(.lrc)가 여기에 표시됩니다", L"歌词(.lrc)将在此显示", L"كلمات (.lrc) ستُعرض هنا", L"Текст (.lrc) отображается здесь", L"Liedtext (.lrc) wird hier angezeigt", L"Letra (.lrc) exibida aqui", L"Songtekst (.lrc) wordt hier getoond", L"Teksty (.lrc) wy?wietlone tutaj", L"Soz (.lrc) burada goruntulenir"));
	m_lrc3.SetWindowText(L"");
	m_lrc4.SetWindowText(L"");
	m_lrc5.SetWindowText(L"");
	lrc_backup = L"";

	stflg = TRUE;

	SetTimer(59877, 500, NULL); // eq

	// ツールチップ・更新チェック・スペアナ窓関数テーブルなど、初回表示後でよい重い処理
	PostMessage(WM_OGG_DEFERRED_HEAVY_INIT, 0, 0);

#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && savedata.playerMode != 1)
	{
		ApplyDwmBlur();
		Invalidate(FALSE);
		UpdateWindow();
	}
#endif

	AfxBeginThread((AFX_THREADPROC)TheadLoop, NULL, THREAD_PRIORITY_ABOVE_NORMAL);
	// d6b90a5(analyzer 前)と同じ: OnInitDialog 内で同期的に MP へ遷移する。
	// cb7e594 以降の PostMessage/SetTimer 遅延は DeferredHeavy(更新チェック)より
	// 後回しになり、KPI〜MP 間で CInvalidArgException が出る順序に変わっていた。
	if (savedata.playerMode == 1)
		EnterMediaPlayerMode();
	return TRUE;  // TRUE を返すとコントロールに設定したフォーカスは失われません。
}
//////////////////////////////////////////////////////////////////////////////
void COggDlg::OnPaint()
{
	CPaintDC dcc(this); // 描画用のデバイス コンテキスト
	if (IsIconic())
	{

		SendMessage(WM_ICONERASEBKGND, (WPARAM)dcc.GetSafeHdc(), 0);

		// クライアントの矩形領域内の中央
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// アイコンを描画します。
		dcc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		const int destW = (int)((MDCP) * hD);
		const int destH = (int)((81 + 16) * hD * 4);
		const int srcW = MDCP + 5;
		const int srcH = (81 + 16) * 4;
		CRect clip;
		dcc.GetClipBox(&clip);
		CRect gdiRect(0, 0, destW, destH);
		CRect gdiClip;
		const BOOL bGdiIntersect = gdiClip.IntersectRect(&clip, &gdiRect);
#if CCUSTOM_AERO_SUPPORT
		if (CCC_IsAeroEnabled() && CCC_IsWin11())
		{
			const BOOL bSpectrumOnly = (Ms2DrawDue(ms2)
				&& clip.bottom <= destH + 8
				&& clip.Height() <= destH + 8);
			if (savedata.aero == 1 && dc.m_hDC != NULL)
			{
				if (Ms2DrawDue(ms2))
				{
					dcc.SelectClipRgn(NULL);
					CCC_BlitStretchNF(dcc.m_hDC, 0, 0, destW, destH, dc.m_hDC, 0, 0, srcW, srcH, RGB(0, 0, 0));
					ms2 = 0;
					InterlockedExchange(&g_gdiPaintPending, 0);
				}
				else if (bGdiIntersect)
				{
					dcc.SelectClipRgn(NULL);
					CCC_BlitStretchChroma(dcc.m_hDC, 0, 0, destW, destH, dc.m_hDC, 0, 0, srcW, srcH, RGB(0, 0, 0));
				}
			}
			if (!bSpectrumOnly)
			{
				dcc.SelectClipRgn(NULL);
				const RECT gdiPreserve = { 0, 0, destW, destH };
				CCC_PaintAeroGaps(dcc, this, &gdiPreserve);
			}
		}
		else
#endif
		if (Ms2DrawDue(ms2)) {
#if CCUSTOM_AERO_SUPPORT
			if (savedata.aero == 1 && CCC_IsWin11())
			{
				CCC_ClipNoChildren(dcc, this);
				CCC_BlitStretchChroma(dcc.m_hDC, 0, 0, destW, destH, dc.m_hDC, 0, 0, srcW, srcH, RGB(0, 0, 0));
			}
			else
#endif
			{
				SetStretchBltMode(dcc.m_hDC, COLORONCOLOR);
				SetBrushOrgEx(dcc.m_hDC, 0, 0, NULL);
				dcc.StretchBlt(0, 0, destW, destH, &dc, 0, 0, srcW, srcH, SRCCOPY);
			}
			ms2 = 0;
			InterlockedExchange(&g_gdiPaintPending, 0);
		}
	}
}
BYTE offenc[7] = { 0xd9,0x3F,0x86,0x7B,0xC7,0x61,0xaa };
int oggf = 0;
// OggVorbisコールバック関数
size_t Callback_Read(
	void* ptr,
	size_t size,
	size_t nmemb,
	void* datasource
) {
	FILE* fp = (FILE*)datasource;
	if (oggf == 0) {
		__int64 iti = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		BYTE buf[2];
		fread(buf, 1, 1, fp);
		if (buf[0] == 0x4f)
			oggf = 1;
		else if (buf[0] == 0x04)
			oggf = 2;
		else if (buf[0] == 0x96)
			oggf = 3;
		fseek(fp, iti, SEEK_SET);
	}

	size_t ret = 0;
	if (oggf == 1)
		ret = fread(ptr, size, nmemb, fp);
	else if (oggf == 2) {
		ret = fread(ptr, size, nmemb, fp);
		size_t ret2 = ret * size;
		BYTE* b = (BYTE*)ptr;
		for (int i = 0; i < (int)ret2; i++) {
			b[i] = b[i] << 4 | b[i] >> 4;
			b[i] ^= 0x0f;
		}
	}
	else if (oggf == 3) {
		int len = ftell(fp);
		int len1 = len % 7;
		ret = fread(ptr, size, nmemb, fp);
		size_t ret2 = ret * size;
		BYTE* b = (BYTE*)ptr;
		for (int i = 0; i < (int)ret2; i++) {
			b[i] ^= offenc[len1];
			len1++; if (len1 > 6) len1 = 0;
		}
	}
	else {
		ret = 0;
	}

	return ret;

}

int Callback_Seek(
	void* datasource,
	ogg_int64_t offset,
	int whence
) {
	FILE* fp = (FILE*)datasource;
	return fseek(fp, offset, whence);
}

int Callback_Close(void* datasource) {
	oggf = 0;
	return 0;
}

long Callback_Tell(void* datasource) {
	FILE* fp = (FILE*)datasource;
	return ftell(fp);
}

ov_callbacks callbacks = {
	Callback_Read,
	Callback_Seek,
	Callback_Close,
	Callback_Tell
};




long LoadOggVorbis(const TCHAR* file_name, int word, char** ogg, CSliderCtrl& m_time)
{
	int eof = 0;
	oggf = 0;
	FILE* fp;
	long size = 0;
	vorbis_info* vi;

	/* 量子化バイト数が正しい値かどうか調べる */
	if (!(word == 1 || word == 2)) {
		return -1;
	}
	/* ファイルを開く */
	fp = _tfopen(file_name, _T("rb"));
	if (fp == NULL) {
		return -1;
	}
	/* OggVorbis初期化 */
	if (ov_open_callbacks(fp, &vf, NULL, 0, callbacks) < 0) {
		fprintf(stderr, "Input does not appear to be an Ogg bitstream.\n");
		fclose(fp);
		return -1;
	}
	else {
		vi = ov_info(&vf, -1);
	}

	/* ヘッダサイズの収得 */
	whsize = sizeof(wh.ckidRIFF) + sizeof(wh.ckSizeRIFF) + sizeof(wh.fccType) +
		sizeof(wh.ckidFmt) + sizeof(wh.ckSizeFmt) + sizeof(wh.WaveFmt) +
		sizeof(wh.ckidData) + sizeof(wh.ckSizeData);

	/* デコード後のデータサイズを求め、メモリ確保 */
	// ★修正点1：時間から計算するのではなく、正確な総データ数を直接取得します
	ogg_int64_t totalFrames = ov_pcm_total(&vf, -1);

	// ★修正点2：正確な総データ数をもとに、データサイズ（バイト数）を算出します
	data_size = (long)(totalFrames * vi->channels * word);

	// ★修正点3：4で決め打ちするのではなく、正確な総データ数をそのままスライダーの範囲に設定します
	og->m_time.SetRange(0, (int)totalFrames, TRUE);

	dd = vi->channels * vi->rate * word;
	*ogg = (char*)malloc(whsize);
	if (ogg == NULL) {
		free(ogg);
		ov_clear(&vf);
		fclose(fp);
		return -1;
	}
	/* ヘッダの初期化 */
	memcpy(wh.ckidRIFF, "RIFF", 4);
	wh.ckSizeRIFF = whsize + size - 8;
	memcpy(wh.fccType, "WAVE", 4);
	memcpy(wh.ckidFmt, "fmt ", 4);
	wh.ckSizeFmt = sizeof(PCMWAVEFORMAT);

	//	wh.WaveFmt.cbSize          = sizeof(WAVEFORMATEX);
	wh.WaveFmt.wf.wFormatTag = WAVE_FORMAT_PCM;
	wh.WaveFmt.wf.nChannels = vi->channels;
	wavchannel = vi->channels;
	wavbit_sample_Hz = vi->rate;
	wh.WaveFmt.wf.nSamplesPerSec = vi->rate;
	wh.WaveFmt.wf.nAvgBytesPerSec = vi->rate * vi->channels * word;
	wh.WaveFmt.wf.nBlockAlign = vi->channels * word;
	wh.WaveFmt.wBitsPerSample = word * 8;
	wavsam_depth = word * 8; // mcopy のループ境界・バイト計算で前曲の wavsam が残らないようにする

	memcpy(wh.ckidData, "data", 4);
	wh.ckSizeData = size;

	/* メモリへのヘッダの書き込み */
	int s = 0;
	memcpy(*ogg, &wh.ckidRIFF, sizeof(wh.ckidRIFF));          s += sizeof(wh.ckidRIFF);
	memcpy(*ogg + s, &wh.ckSizeRIFF, sizeof(wh.ckSizeRIFF));  s += sizeof(wh.ckSizeRIFF);
	memcpy(*ogg + s, &wh.fccType, sizeof(wh.fccType));        s += sizeof(wh.fccType);
	memcpy(*ogg + s, &wh.ckidFmt, sizeof(wh.ckidFmt));        s += sizeof(wh.ckidFmt);
	memcpy(*ogg + s, &wh.ckSizeFmt, sizeof(wh.ckSizeFmt));    s += sizeof(wh.ckSizeFmt);
	memcpy(*ogg + s, &wh.WaveFmt, sizeof(wh.WaveFmt));        s += sizeof(wh.WaveFmt);
	memcpy(*ogg + s, &wh.ckidData, sizeof(wh.ckidData));      s += sizeof(wh.ckidData);
	memcpy(*ogg + s, &wh.ckSizeData, sizeof(wh.ckSizeData));

	return data_size;// + whsize;
}

void wav_start();
static void NormalizePlaybackWaveFormat()
{
	// Guard invalid values from some decoders/plugins.
	if (wavchannel <= 0 || wavchannel > 8) wavchannel = 2;
	if (wavbit_sample_Hz < 8000 || wavbit_sample_Hz > 384000) wavbit_sample_Hz = 44100;
	if (wavsam_depth == 0) wavsam_depth = 16;
	if (wavsam_depth < 0) {
		// This player path does not support float PCM playback.
		// Force to integer PCM to keep the output pipeline consistent.
		wavsam_depth = 16;
	}
	else {
		if (!(wavsam_depth == 8 || wavsam_depth == 16 || wavsam_depth == 24 || wavsam_depth == 32)) {
			wavsam_depth = 16;
		}
	}
}
void wav_start()
{
	whsize = sizeof(wh.ckidRIFF) + sizeof(wh.ckSizeRIFF) + sizeof(wh.fccType) +
		sizeof(wh.ckidFmt) + sizeof(wh.ckSizeFmt) + sizeof(wh.WaveFmt) +
		sizeof(wh.ckidData) + sizeof(wh.ckSizeData);

	/* デコード後のデータサイズを求め、メモリ確保 */
	wav = (char*)malloc(whsize);
	/* ヘッダの初期化 */
	memcpy(wh.ckidRIFF, "RIFF", 4);
	memcpy(wh.fccType, "WAVE", 4);
	memcpy(wh.ckidFmt, "fmt ", 4);
	wh.ckSizeFmt = sizeof(PCMWAVEFORMAT);

	//	wh.WaveFmt.cbSize          = sizeof(WAVEFORMATEX);
	wh.WaveFmt.wf.wFormatTag = WAVE_FORMAT_PCM;
	wh.WaveFmt.wf.nChannels = wavchannel;
	wh.WaveFmt.wf.nSamplesPerSec = wavbit_sample_Hz;
	wh.WaveFmt.wBitsPerSample = wavsam_depth;
	wh.WaveFmt.wf.nBlockAlign = wh.WaveFmt.wf.nChannels * wh.WaveFmt.wBitsPerSample / 8;
	wh.WaveFmt.wf.nAvgBytesPerSec = wh.WaveFmt.wf.nSamplesPerSec * wh.WaveFmt.wf.nBlockAlign;
	wh.ckSizeFmt = 16;
	memcpy(wh.ckidData, "data", 4);

	/* メモリへのヘッダの書き込み */
	int s = 0;
	memcpy(wav, &wh.ckidRIFF, sizeof(wh.ckidRIFF));          s += sizeof(wh.ckidRIFF);
	memcpy(wav + s, &wh.ckSizeRIFF, sizeof(wh.ckSizeRIFF));  s += sizeof(wh.ckSizeRIFF);
	memcpy(wav + s, &wh.fccType, sizeof(wh.fccType));        s += sizeof(wh.fccType);
	memcpy(wav + s, &wh.ckidFmt, sizeof(wh.ckidFmt));        s += sizeof(wh.ckidFmt);
	memcpy(wav + s, &wh.ckSizeFmt, sizeof(wh.ckSizeFmt));    s += sizeof(wh.ckSizeFmt);
	memcpy(wav + s, &wh.WaveFmt, sizeof(wh.WaveFmt));        s += sizeof(wh.WaveFmt);
	memcpy(wav + s, &wh.ckidData, sizeof(wh.ckidData));      s += sizeof(wh.ckidData);
	memcpy(wav + s, &wh.ckSizeData, sizeof(wh.ckSizeData));
}

// ============================================================
// 2GB超対応 WAV(RF64) ストリーム出力ヘルパ
//   通常RIFF WAVは32bitサイズのため2GB(符号付int)〜4GBが壁になる。
//   出力サイズが2GBを超える場合は RF64 として書き出す。
//   ストリーム出力では最終サイズが事前に不明なため、先頭に ds64 と
//   同サイズ(28byte)の 'JUNK' を予約しておき、確定時にサイズが収まれば
//   通常 'RIFF' のまま、2GB超なら 'JUNK'→'ds64' / 'RIFF'→'RF64' に
//   書き換える(libsndfile等と同方式)。ヘッダ長は常に80byte。
//   フォーマット情報は wav_start() が設定した wh.WaveFmt を使用する。
// ============================================================
#define WAVX_HEADER_SIZE 80
#define WAVX_RF64_THRESHOLD ((__int64)0x7FFFFFFF)  // 2GB(符号付32bitの壁)

static void WriteWavStreamHeaderRF64(CFile& f)
{
	BYTE h[WAVX_HEADER_SIZE];
	memset(h, 0, sizeof(h));
	const WORD ch = (wh.WaveFmt.wf.nChannels > 0) ? wh.WaveFmt.wf.nChannels : (WORD)2;
	const DWORD hz = (wh.WaveFmt.wf.nSamplesPerSec > 0) ? wh.WaveFmt.wf.nSamplesPerSec : (DWORD)44100;
	const WORD bits = (wh.WaveFmt.wBitsPerSample > 0) ? wh.WaveFmt.wBitsPerSample : (WORD)16;
	const WORD blockAlign = (WORD)(ch * bits / 8);

	memcpy(h + 0, "RIFF", 4);
	*(DWORD*)(h + 4) = 0;                 // riffSize (確定時に設定)
	memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "JUNK", 4);            // RF64化のための予約 (= ds64 payload 28byte)
	*(DWORD*)(h + 16) = 28;
	// h+20..47 : 28byte 予約(0)
	memcpy(h + 48, "fmt ", 4);
	*(DWORD*)(h + 52) = 16;
	*(WORD*)(h + 56) = WAVE_FORMAT_PCM;
	*(WORD*)(h + 58) = ch;
	*(DWORD*)(h + 60) = hz;
	*(DWORD*)(h + 64) = hz * blockAlign;  // nAvgBytesPerSec
	*(WORD*)(h + 68) = blockAlign;
	*(WORD*)(h + 70) = bits;
	memcpy(h + 72, "data", 4);
	*(DWORD*)(h + 76) = 0;                // dataSize (確定時に設定)
	f.Write(h, WAVX_HEADER_SIZE);
}

static void FinalizeWavStreamHeaderRF64(CFile& f)
{
	const __int64 fileLen = (__int64)f.GetLength();
	__int64 dataBytes = fileLen - WAVX_HEADER_SIZE;
	if (dataBytes < 0) dataBytes = 0;
	WORD ch = (wh.WaveFmt.wf.nChannels > 0) ? wh.WaveFmt.wf.nChannels : (WORD)2;
	WORD bits = (wh.WaveFmt.wBitsPerSample > 0) ? wh.WaveFmt.wBitsPerSample : (WORD)16;
	int blockAlign = ch * bits / 8; if (blockAlign <= 0) blockAlign = 4;

	f.SeekToBegin();
	if (dataBytes <= WAVX_RF64_THRESHOLD) {
		// 通常RIFF(2GB以下)。'JUNK'予約はそのまま残し、サイズのみ確定。
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
		// RF64(2GB超)。'JUNK'→'ds64' に変換し64bitサイズを格納。
		BYTE hdr[48];
		memcpy(hdr + 0, "RF64", 4);
		*(DWORD*)(hdr + 4) = 0xFFFFFFFF;
		memcpy(hdr + 8, "WAVE", 4);
		memcpy(hdr + 12, "ds64", 4);
		*(DWORD*)(hdr + 16) = 28;
		*(__int64*)(hdr + 20) = fileLen - 8;              // riffSize
		*(__int64*)(hdr + 28) = dataBytes;                // dataSize
		*(__int64*)(hdr + 36) = dataBytes / blockAlign;   // sampleCount
		*(DWORD*)(hdr + 44) = 0;                           // tableLength
		f.Write(hdr, 48);
		f.Seek(76, CFile::begin);
		DWORD ds = 0xFFFFFFFF;
		f.Write(&ds, 4);
	}
	g_wavExportDataBytes = dataBytes;
}

struct WavPcmStreamInfo {
	WORD ch;
	DWORD hz;
	WORD bits;
	int blockAlign;
	__int64 dataOffset;
	__int64 dataBytes;
	__int64 totalFrames;
};

static BOOL WavExportReadPcmStreamInfo(CFile& f, WavPcmStreamInfo& info)
{
	const __int64 fileLen = (__int64)f.GetLength();
	if (fileLen <= WAVX_HEADER_SIZE) return FALSE;
	BYTE hdr[WAVX_HEADER_SIZE];
	f.SeekToBegin();
	if (f.Read(hdr, WAVX_HEADER_SIZE) != WAVX_HEADER_SIZE) return FALSE;
	info.dataOffset = WAVX_HEADER_SIZE;
	info.dataBytes = fileLen - WAVX_HEADER_SIZE;
	if (memcmp(hdr, "RF64", 4) == 0)
		info.dataBytes = *(__int64*)(hdr + 28);
	info.ch = *(WORD*)(hdr + 58);
	info.hz = *(DWORD*)(hdr + 60);
	info.bits = *(WORD*)(hdr + 70);
	info.blockAlign = info.ch * info.bits / 8;
	if (info.blockAlign <= 0) info.blockAlign = 4;
	if (info.hz == 0 || info.dataBytes <= 0) return FALSE;
	info.totalFrames = info.dataBytes / info.blockAlign;
	return TRUE;
}

static void WavExportApplyGainToFrame(BYTE* frame, int blockAlign, int bits, float g)
{
	if (!frame || blockAlign <= 0) return;
	if (bits == 8) {
		for (int i = 0; i < blockAlign; ++i) {
			float v = ((float)frame[i] - 128.f) * g + 128.f;
			int u = (int)floorf(v + 0.5f);
			if (u < 0) u = 0; else if (u > 255) u = 255;
			frame[i] = (unsigned char)u;
		}
	}
	else if (bits == 16) {
		short* s = (short*)frame;
		const int n = blockAlign / 2;
		for (int i = 0; i < n; ++i) {
			float v = (float)s[i] * g;
			int x = (int)floorf(v + 0.5f);
			if (x > 32767) x = 32767;
			else if (x < -32768) x = -32768;
			s[i] = (short)x;
		}
	}
	else if (bits == 24) {
		for (int o = 0; o + 2 < blockAlign; o += 3) {
			const int val = frame[o] | (frame[o + 1] << 8) | ((int)(signed char)frame[o + 2] << 16);
			float v = (float)val * g;
			int x = (int)floorf(v + 0.5f);
			if (x > 8388607) x = 8388607;
			else if (x < -8388608) x = -8388608;
			frame[o] = (BYTE)(x & 0xFF);
			frame[o + 1] = (BYTE)((x >> 8) & 0xFF);
			frame[o + 2] = (BYTE)((x >> 16) & 0xFF);
		}
	}
	else if (bits == 32) {
		int* s = (int*)frame;
		const int n = blockAlign / 4;
		for (int i = 0; i < n; ++i) {
			float v = (float)s[i] * g;
			int x = (int)floorf(v + 0.5f);
			s[i] = x;
		}
	}
}

static BOOL ApplyTailFadeOutWavFile(const CString& path, int fadeSec)
{
	if (fadeSec <= 0) return TRUE;
	CFile f;
	if (!f.Open(path, CFile::modeReadWrite | CFile::shareExclusive, NULL))
		return FALSE;
	WavPcmStreamInfo info = {};
	if (!WavExportReadPcmStreamInfo(f, info)) {
		f.Close();
		return FALSE;
	}
	__int64 fadeFrames = (__int64)fadeSec * (__int64)info.hz;
	if (fadeFrames > info.totalFrames) fadeFrames = info.totalFrames;
	if (fadeFrames <= 1) {
		f.Close();
		return TRUE;
	}
	const __int64 fadeStartFrame = info.totalFrames - fadeFrames;
	std::vector<BYTE> frame(info.blockAlign);
	for (__int64 fi = fadeStartFrame; fi < info.totalFrames; ++fi) {
		const float t = (float)(fi - fadeStartFrame) / (float)(fadeFrames - 1);
		const float g = (1.f - t) * (1.f - t);
		const __int64 pos = info.dataOffset + fi * info.blockAlign;
		f.Seek(pos, CFile::begin);
		if (f.Read(frame.data(), info.blockAlign) != (UINT)info.blockAlign) break;
		WavExportApplyGainToFrame(frame.data(), info.blockAlign, info.bits, g);
		f.Seek(pos, CFile::begin);
		f.Write(frame.data(), info.blockAlign);
	}
	f.Close();
	return TRUE;
}

static bool WavExportFrameIsSilent(const BYTE* frame, int blockAlign, int bits, int threshold)
{
	if (!frame || blockAlign <= 0) return true;
	if (bits == 8) {
		for (int i = 0; i < blockAlign; ++i) {
			if (abs((int)frame[i] - 128) > threshold) return false;
		}
		return true;
	}
	if (bits == 16) {
		const short* s = (const short*)frame;
		const int n = blockAlign / 2;
		for (int i = 0; i < n; ++i) {
			if (abs((int)s[i]) > threshold) return false;
		}
		return true;
	}
	if (bits == 24) {
		for (int o = 0; o + 2 < blockAlign; o += 3) {
			const int val = frame[o] | (frame[o + 1] << 8) | ((int)(signed char)frame[o + 2] << 16);
			if (abs(val) > threshold) return false;
		}
		return true;
	}
	if (bits == 32) {
		const int* s = (const int*)frame;
		const int n = blockAlign / 4;
		for (int i = 0; i < n; ++i) {
			if (abs(s[i]) > threshold) return false;
		}
		return true;
	}
	return false;
}

static BOOL TrimLeadingSilenceWavFile(const CString& path, int keepSec)
{
	if (keepSec < 0) keepSec = 0;
	CFile f;
	if (!f.Open(path, CFile::modeReadWrite | CFile::shareExclusive, NULL))
		return FALSE;
	WavPcmStreamInfo info = {};
	if (!WavExportReadPcmStreamInfo(f, info)) {
		f.Close();
		return FALSE;
	}
	const int blockAlign = info.blockAlign;
	const DWORD hz = info.hz;
	const WORD bits = info.bits;
	const __int64 dataBytes = info.dataBytes;

	int threshold = 200;
	if (bits == 8) threshold = 3;
	else if (bits == 24) threshold = 8000;
	else if (bits == 32) threshold = 8000;

	std::vector<BYTE> frame(blockAlign);
	__int64 leadingSilentFrames = 0;
	f.Seek(info.dataOffset, CFile::begin);
	while (leadingSilentFrames * blockAlign < dataBytes) {
		const UINT rd = f.Read(frame.data(), blockAlign);
		if (rd != (UINT)blockAlign) break;
		if (!WavExportFrameIsSilent(frame.data(), blockAlign, bits, threshold)) break;
		++leadingSilentFrames;
	}

	const __int64 keepFrames = (__int64)keepSec * (__int64)hz;
	__int64 trimFrames = leadingSilentFrames - keepFrames;
	if (trimFrames <= 0) {
		f.Close();
		return TRUE;
	}
	const __int64 trimBytes = trimFrames * blockAlign;
	if (trimBytes >= dataBytes) {
		f.Close();
		return FALSE;
	}
	const __int64 newDataBytes = dataBytes - trimBytes;
	const int bufSize = 65536;
	std::vector<BYTE> buf(bufSize);
	__int64 src = info.dataOffset + trimBytes;
	__int64 dst = info.dataOffset;
	__int64 remaining = newDataBytes;
	while (remaining > 0) {
		const int toMove = (int)((remaining > bufSize) ? bufSize : remaining);
		f.Seek(src, CFile::begin);
		if (f.Read(buf.data(), toMove) != (UINT)toMove) {
			f.Close();
			return FALSE;
		}
		f.Seek(dst, CFile::begin);
		f.Write(buf.data(), toMove);
		src += toMove;
		dst += toMove;
		remaining -= toMove;
	}
	f.SetLength(info.dataOffset + newDataBytes);
	FinalizeWavStreamHeaderRF64(f);
	f.Close();
	return TRUE;
}

void ReleaseOggVorbis(char** ogg)
{
	if (ogg != NULL) {
		ov_clear(&vf);
		free(*ogg);
		ogg = NULL;
	}
}

//main
void DoEvent()
{
	MSG msg;
	if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static void PumpUntilFlagOrTimeout(int& flag, DWORD timeoutMs = 10000)
{
	const DWORD t0 = GetTickCount();
	for (; flag == 0;) {
		DoEvent();
		if (GetTickCount() - t0 >= timeoutMs)
			break;
	}
}

// OnHScroll が syukai2 を待つ間に stop が走ると syukai2 は Signal で立つが、
// 再生スレッドが cl2 内でデコード中だと応答が遅れる。タイムアウトで永久待ちを防ぐ。
static void WaitForSyukai2OrPlaybackStop(DWORD timeoutMs = 5000)
{
	const DWORD t0 = GetTickCount();
	for (;;) {
		if (syukai2 == 1 || thn1 || stf != 0)
			break;
		DoEvent();
		if (GetTickCount() - t0 >= timeoutMs) {
			syukai2 = 1;
			break;
		}
	}
}

DWORD COggDlg::GetVol()
{
	WAVEFORMATEX wfx1;
	wfx1.wFormatTag = WAVE_FORMAT_PCM;
	wfx1.nChannels = 2;
	wfx1.nSamplesPerSec = 48000;
	wfx1.wBitsPerSample = 24;
	wfx1.nBlockAlign = wfx1.nChannels * wfx1.wBitsPerSample / 8;
	wfx1.nAvgBytesPerSec = wfx1.nSamplesPerSec * wfx1.nBlockAlign;
	wfx1.cbSize = 0;

	mmRes = waveOutOpen(&hwo,
		WAVE_MAPPER,
		&wfx1,
#ifdef WIN64
		(DWORD_PTR)m_hWnd,
#else
		(DWORD)m_hWnd,
#endif
		(DWORD)NULL,
		CALLBACK_WINDOW);

	DWORD v;
	waveOutGetVolume(hwo, &v);
	waveOutClose(hwo);
	return v;
}

extern int flg3;
void COggDlg::dsdclose() {
	dsd_.kpiClose(kmp);
	kmp = NULL;

}
ULONGLONG po;
void COggDlg::dsdload(CString& filen, CString& tagfile, CString& tagname, CString& tagalbum, ULONGLONG& po1, int flg1) {
	CString ss;
	char buf[1024];
	ss = "";
	ZeroMemory(&sikpi, sizeof(sikpi));
	dsdart = "";
	dsdtitle = "";
	sikpi.dwSamplesPerSec = savedata.samples; sikpi.dwChannels = 6; sikpi.dwSeekable = 1; sikpi.dwLength = -1; sikpi.dwBitsPerSample = ((savedata.bit24 == 1) ? 24 : 16);
	if (savedata.bit32 == 1) {
		sikpi.dwBitsPerSample = 32;
	}
	ULONGLONG pointer;
	if (1) {
		if (ss == "") {
			kpiInit();
#if UNICODE
			TCHAR* f = filen.GetBuffer();
			kmp = dsd_.kpiOpen(f, &sikpi, pointer);
			po = pointer;
			filen.ReleaseBuffer();
#else
			kmp = dsd_.Open(filen, &sikpi);
#endif
			if (kmp == NULL) { m_saisai.EnableWindow(TRUE); return; }
			g_openDecoderMode = -7;
		}
		else {
		}
	}
	wavbit_sample_Hz = sikpi.dwSamplesPerSec;
	wavchannel = sikpi.dwChannels;
	wavsam_depth = sikpi.dwBitsPerSample;
	NormalizePlaybackWaveFormat();
	loop1 = 0;
	if (sikpi.dwLength == (DWORD)-1 || wavbit_sample_Hz <= 0)
		loop2 = 0;
	else
		loop2 = (int)((double)(DWORD)sikpi.dwLength * (double)wavbit_sample_Hz / 1000.0 + 0.5);
	{
		const int bps = (wavsam_depth >= 8) ? (wavsam_depth / 8) : 2;
		const __int64 bytesTotal = (__int64)loop2 * (__int64)wavchannel * (__int64)bps;
		if (bytesTotal > 0 && bytesTotal < (__int64)0x7fffffff)
			data_size = oggsize = (int)bytesTotal;
		else
			data_size = oggsize = (loop2 > 0 && wavchannel > 0) ? (loop2 * wavchannel * (bps > 0 ? bps : 2)) : 0;
	}
	CString s; s.Format(L"%d", oggsize);
	//AfxMessageBox(s);
	si1.dwSamplesPerSec = wavbit_sample_Hz;
	si1.dwChannels = wavchannel;
	si1.dwBitsPerSample = wavsam_depth;
	m_time.SetRange(0, (loop2 > 0) ? loop2 : 1, TRUE);
	dsd_.kpiSetPosition(kmp, 0);
	kbps = 0;
	wav_start();
	CFile ff;
	ff.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL);
	ff.Seek(pointer, CFile::begin);
	int read = ff.Read(bufimage, sizeof(bufimage));
	ff.Close();
	int j;
	int flg = 0;
	ZeroMemory(buf, sizeof(buf));
	tagfile = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
	if (dsdtitle != "") {
		tagfile = dsdtitle;
	}
	for (j = 0; j < read - 6; j++) {
		if (bufimage[j] == 'T' && bufimage[j + 1] == 'I' && bufimage[j + 2] == 'T' && bufimage[j + 3] == '2') {
			int kk = bufimage[j + 7] - 4;
			j += 5 + 4 + 4;
			int l = 0;
			for (int k = j; k < j + kk; k++) {
				buf[l] = bufimage[k];
				l++;
			}
			flg = 1;
			buf[l] = 0;
			buf[l + 1] = 0;
			buf[l + 2] = 0;
			WCHAR* a = (WCHAR*)buf;
			tagfile = a;
			flg = 0;
			break;
		}
	}
	ZeroMemory(buf, sizeof(buf));
	if (dsdart != "") {
		tagname = dsdart;
	}
	flg = 0;
	for (j = 0; j < read - 6; j++) {
		if (bufimage[j] == 'T' && bufimage[j + 1] == 'P' && bufimage[j + 2] == 'E' && bufimage[j + 3] == '1') {
			int kk = bufimage[j + 7] - 4;
			j += 5 + 4 + 4;
			int l = 0;
			for (int k = j; k < j + kk; k++) {
				buf[l] = bufimage[k];
				l++;
			}
			flg = 1;
			buf[l] = 0;
			buf[l + 1] = 0;
			buf[l + 2] = 0;
			WCHAR* a = (WCHAR*)buf;
			tagname = a;
			flg = 0;
			break;
		}
	}
	ZeroMemory(buf, sizeof(buf));
	flg = 0;
	for (j = 0; j < read - 6; j++) {
		if (bufimage[j] == 'T' && bufimage[j + 1] == 'A' && bufimage[j + 2] == 'L' && bufimage[j + 3] == 'B') {
			int kk = bufimage[j + 7] - 4;
			j += 5 + 4 + 4;
			int l = 0;
			for (int k = j; k < j + kk; k++) {
				buf[l] = bufimage[k];
				l++;
			}
			flg = 1;
			buf[l] = 0;
			buf[l + 1] = 0;
			buf[l + 2] = 0;
			WCHAR* a = (WCHAR*)buf;
			tagalbum = a;
			flg = 0;
			break;
		}
	}
	int i;
	for (i = 0; i < read; i++) {// 00 06 5D 6A 64 61 74 61
		if (bufimage[i] == 'i' && bufimage[i + 1] == 'm' && bufimage[i + 2] == 'a' && bufimage[i + 3] == 'g' && bufimage[i + 4] == 'e' && bufimage[i + 5] == '/' && bufimage[i + 6] == 'j' && bufimage[i + 7] == 'p' && bufimage[i + 8] == 'e' && bufimage[i + 9] == 'g') {
			break;
		}
		if (bufimage[i] == 'i' && bufimage[i + 1] == 'm' && bufimage[i + 2] == 'a' && bufimage[i + 3] == 'g' && bufimage[i + 4] == 'e' && bufimage[i + 5] == '/' && bufimage[i + 6] == 'p' && bufimage[i + 7] == 'n' && bufimage[i + 8] == 'g') {
			break;
		}
	}
	if (i != read && flg1 == 1) {
		m_mp3jake.EnableWindow(TRUE);
	}

}
#include <Shlwapi.h>
#include <stdio.h>

#pragma comment(lib, "Shlwapi.lib")
#include "rubberband/RubberBandStretcher.h"
void COggDlg::Modec() {
	int ret;
	ret = _tchdir(savedata.ed6sc);
	ret += _chdir("bgm");
	if (ret != 0) { og->d_ed6sc.EnableWindow(FALSE); og->m_ed6sc.EnableWindow(FALSE); }
	else { og->d_ed6sc.EnableWindow(TRUE); og->m_ed6sc.EnableWindow(TRUE); }
	ret = _tchdir(savedata.ed6fc);
	ret += _chdir("bgm");
	if (ret != 0) { og->d_ed6fc.EnableWindow(FALSE); og->m_ed6fc.EnableWindow(FALSE); }
	else { og->d_ed6fc.EnableWindow(TRUE); og->m_ed6fc.EnableWindow(TRUE); }
	ret = _tchdir(savedata.ysf);
	ret += _chdir("RELEASE\\MUSIC");
	if (ret != 0) { og->d_ys3.EnableWindow(FALSE); og->m_ysf.EnableWindow(FALSE); }
	else { og->d_ys3.EnableWindow(TRUE); og->m_ysf.EnableWindow(TRUE); }
	ret = _tchdir(savedata.ys6);
	ret += _chdir("RELEASE\\MUSIC");
	if (ret != 0) { og->d_ys6.EnableWindow(FALSE); og->m_ys6.EnableWindow(FALSE); }
	else { og->d_ys6.EnableWindow(TRUE); og->m_ys6.EnableWindow(TRUE); }
	ret = _tchdir(savedata.yso);
	ret += _chdir("RELEASE\\MUSIC");
	if (ret != 0) { og->d_yso.EnableWindow(FALSE); og->m_yso.EnableWindow(FALSE); }
	else { og->d_yso.EnableWindow(TRUE); og->m_yso.EnableWindow(TRUE); }
	ret = _tchdir(savedata.ed6tc);
	ret += _chdir("bgm");
	if (ret != 0) { og->d_ed6tc.EnableWindow(FALSE); og->m_ed6tc.EnableWindow(FALSE); }
	else { og->d_ed6tc.EnableWindow(TRUE); og->m_ed6tc.EnableWindow(TRUE); }
	ret = _tchdir(savedata.zweiii);
	ret += _chdir("bgm");
	if (ret != 0) { og->d_z2.EnableWindow(FALSE); og->m_zweiii.EnableWindow(FALSE); }
	else { og->d_z2.EnableWindow(TRUE); og->m_zweiii.EnableWindow(TRUE); }
	ret = _tchdir(savedata.ysc);
	ret += _chdir("bgm\\ys1");
	if (ret != 0) { og->d_ysc1.EnableWindow(FALSE); og->m_ysc1.EnableWindow(FALSE); }
	else { og->d_ysc1.EnableWindow(TRUE); og->m_ysc1.EnableWindow(TRUE); }
	ret = _tchdir(savedata.ysc);
	ret += _chdir("bgm\\ys2");
	if (ret != 0) { og->d_ysc2.EnableWindow(FALSE); og->m_ysc2.EnableWindow(FALSE); }
	else { og->d_ysc2.EnableWindow(TRUE); og->m_ysc2.EnableWindow(TRUE); }
	ret = _tchdir(savedata.xa);
	ret += _chdir("data\\bgm");
	if (ret != 0) { og->d_xa.EnableWindow(FALSE); og->m_xa.EnableWindow(FALSE); }
	else { og->d_xa.EnableWindow(TRUE); og->m_xa.EnableWindow(TRUE); }
	ret = _tchdir(savedata.ys12);
	og->d_ys1.EnableWindow(TRUE); og->m_ys121.EnableWindow(TRUE);
	if (_chdir("wave\\wave_44") == -1) {
		if (_chdir("wave\\wave_22") == -1) { og->d_ys1.EnableWindow(FALSE); og->m_ys121.EnableWindow(FALSE); }
	}
	ret = _tchdir(savedata.ys122);
	og->d_ys2.EnableWindow(TRUE); og->m_ys122.EnableWindow(TRUE);
	if (_chdir("wave\\wave_44") == -1) {
		if (_chdir("wave\\wave_22") == -1) { og->d_ys2.EnableWindow(FALSE); og->m_ys122.EnableWindow(FALSE); }
	}
	ret = _tchdir(savedata.sor);
	og->d_sor.EnableWindow(TRUE); og->m_sor.EnableWindow(TRUE);
	if (_chdir("WAVE\\WAVE44") == -1) {
		if (_chdir("WAVE\\WAVE22") == -1) { og->d_sor.EnableWindow(FALSE); og->m_sor.EnableWindow(FALSE); }
	}
	og->d_z1.EnableWindow(TRUE); og->m_zwei.EnableWindow(TRUE);
	ret = _tchdir(savedata.zwei);
	{ CFile f; if (f.Open(_T("wav.dat"), CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) { og->d_z1.EnableWindow(FALSE); og->m_zwei.EnableWindow(FALSE); } else f.Close(); }
	ret = _tchdir(savedata.gurumin);
	ret += _chdir("bgm");
	if (ret != 0) { og->d_guru.EnableWindow(FALSE); og->m_gurumin.EnableWindow(FALSE); }
	else { og->d_guru.EnableWindow(TRUE); og->m_gurumin.EnableWindow(TRUE); }
	og->d_dino.EnableWindow(TRUE); og->m_dino.EnableWindow(TRUE);
	ret = _tchdir(savedata.dino);
	{ CFile f; if (f.Open(_T("bgm.arc"), CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) { og->d_dino.EnableWindow(FALSE); og->m_dino.EnableWindow(FALSE); } else f.Close(); }
	ret = _tchdir(savedata.br4);
	ret += _chdir("wave");
	if (ret != 0) { og->d_br4.EnableWindow(FALSE); og->m_br4.EnableWindow(FALSE); }
	else { og->d_br4.EnableWindow(TRUE); og->m_br4.EnableWindow(TRUE); }
	ret = _tchdir(savedata.ed3);
	ret += _chdir("wave");
	if (ret != 0) { og->d_ed3.EnableWindow(FALSE); og->m_ed3.EnableWindow(FALSE); }
	else { og->d_ed3.EnableWindow(TRUE); og->m_ed3.EnableWindow(TRUE); }
	og->d_ed4.EnableWindow(TRUE); og->m_ed4.EnableWindow(TRUE);
	ret = _tchdir(savedata.ed4);
	if (_chdir("WAVEDV") == -1) {
		if (_chdir("WAVE") == -1) { og->d_ed4.EnableWindow(FALSE); og->m_ed4.EnableWindow(FALSE); }
	}
	og->d_ed5.EnableWindow(TRUE); og->d_ed5.EnableWindow(TRUE);
	ret = _tchdir(savedata.ed5);
	if (_chdir("WAVEDVD") == -1) {
		if (_chdir("WAVE") == -1) { og->d_ed5.EnableWindow(FALSE); og->m_ed5.EnableWindow(FALSE); }
	}
	ret = _tchdir(savedata.tuki);
	ret += _chdir("MUSIC");
	if (ret != 0) { og->d_tuki.EnableWindow(FALSE); }
	else { og->d_tuki.EnableWindow(TRUE); }
	ret = _tchdir(savedata.nishi);
	ret += _chdir("bgm");
	if (ret != 0) { og->d_nishi.EnableWindow(FALSE); }
	else { og->d_nishi.EnableWindow(TRUE); }
	og->d_arc.EnableWindow(TRUE);
	ret = _tchdir(savedata.arc);
	{ CFile f; if (f.Open(_T("music.pak"), CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) og->d_arc.EnableWindow(FALSE); else f.Close(); }
	ret = _tchdir(savedata.san1);
	ret += _chdir("music");
	if (ret != 0) { og->d_san1.EnableWindow(FALSE); }
	else { og->d_san1.EnableWindow(TRUE); }
	ret = _tchdir(savedata.san2);
	ret += _chdir("music");
	if (ret != 0) { og->d_san2.EnableWindow(FALSE); }
	else { og->d_san2.EnableWindow(TRUE); }
}
int rrr;
#define MUON 80
int flg0 = 0;
int gameon = 1;
extern CImageBase* playbase;
extern RubberBand::RubberBandStretcher* g_rubberBandStretcher;
BYTE bufkpi[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];
BYTE bufkpi_[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];
BYTE bufkpi2[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];
BYTE bufkpi3[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];
BYTE bufkpi4[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];


//ネットlrc取得 - 完全版（MusicBrainz API + キャッシュ対応）
#include <wininet.h>
#include <atlenc.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <map>
#include <string>
#include <sstream>
static ISimpleAudioVolume* FindCurrentProcessSimpleVolume();
static DWORD WINAPI MixerMuteCheckThread(LPVOID);
void CheckCurrentAppMixerMuteModal();
static bool GetMasterMuteState(BOOL* muted);
bool CheckMixerMuteOnPlayModal();

#pragma comment(lib, "wininet.lib")

// =========================================================
// グローバル設定
// =========================================================
#define MUSICBRAINZ_USER_AGENT _T("oggPlayer-LyricsSearcher/1.0 ( ohimesama@example.com )")
#define CACHE_FILE_NAME _T("artist_name_cache.ini")

CString KatakanaToRomaji(const CString& katakana);
CString QueryEnglishTitleFromWikipediaLangLinks(const CString& pageTitle);
CString QueryEnglishTitleFromMusicBrainz(const CString& japaneseTitle, const CString& artistName);
CString QueryEnglishTitleFromWikipedia(const CString& japaneseTitle, const CString& artistName);



// =========================================================
// デバッグログ出力
// =========================================================
void WriteDebugLog(const CString& msg)
{
	CFile file;
	TCHAR tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	CString path = CString(tempPath) + _T("netease_debug.txt");

	// ★デバッグログを有効化（問題診断用）
	if (file.Open(path, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite | CFile::shareExclusive))
	{
		file.SeekToEnd();
		CStringA utf8Msg = CW2A(msg, CP_UTF8);
		utf8Msg += "\r\n";
		file.Write((LPCSTR)utf8Msg, utf8Msg.GetLength());
		file.Close();
	}
}

// =========================================================
// ヘルパー関数群
// =========================================================

// URLエンコード (UTF-8)
CString UrlEncode(const CString& str)
{
	CStringW wideStr = CT2W(str);
	CStringA utf8Str = CW2A(wideStr, CP_UTF8);
	int bufSize = utf8Str.GetLength() * 3 + 1;
	char* buffer = new char[bufSize];
	int pos = 0;
	for (int i = 0; i < utf8Str.GetLength(); i++) {
		unsigned char c = (unsigned char)utf8Str[i];
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') buffer[pos++] = c;
		else { sprintf_s(buffer + pos, bufSize - pos, "%%%02X", c); pos += 3; }
	}
	buffer[pos] = '\0';
	CString result = CA2T(buffer);
	delete[] buffer;
	return result;
}

// HTTP GET (汎用版・タイムアウト付き)
CStringA HttpGet(const CString& url, const CString& userAgent = _T(""), const CString& headers = _T(""))
{
	CStringA response;
	CString ua = userAgent.IsEmpty() ?
		_T("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36") : userAgent;

	HINTERNET hInternet = InternetOpen(ua, INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
	if (!hInternet) return response;

	// ★タイムアウト設定（ミリ秒）
	DWORD timeout = 2000; // 2秒（高速化）
	InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	InternetSetOption(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));

	DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE;
	if (url.Find(_T("https://")) == 0) flags |= INTERNET_FLAG_SECURE;

	HINTERNET hConnect = InternetOpenUrl(hInternet, url, headers, headers.GetLength(), flags, 0);
	if (!hConnect) { InternetCloseHandle(hInternet); return response; }

	char buffer[4096];
	DWORD bytesRead;
	while (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
		buffer[bytesRead] = '\0';
		response += buffer;
	}
	InternetCloseHandle(hConnect);
	InternetCloseHandle(hInternet);
	return response;
}

// 文字列置換
CStringA StringReplace(CStringA str, const CStringA& from, const CStringA& to)
{
	int pos = 0;
	while ((pos = str.Find(from, pos)) != -1) {
		str = str.Left(pos) + to + str.Mid(pos + from.GetLength());
		pos += to.GetLength();
	}
	return str;
}

// Unicodeエスケープ (\uXXXX) をUTF-8に戻す
CStringA UnescapeJsonUnicode(const CStringA& src)
{
	CStringA out;
	int len = src.GetLength();
	const char* p = src.GetString();
	for (int i = 0; i < len; ++i) {
		if (p[i] == '\\' && i + 5 < len && p[i + 1] == 'u') {
			char hex[5] = { p[i + 2], p[i + 3], p[i + 4], p[i + 5], 0 };
			char* endPtr;
			unsigned long code = strtoul(hex, &endPtr, 16);
			if (endPtr == hex + 4) {
				wchar_t w[2] = { (wchar_t)code, 0 };
				char utf8[8];
				int bytes = WideCharToMultiByte(CP_UTF8, 0, w, 1, utf8, 8, NULL, NULL);
				if (bytes > 0) { utf8[bytes] = 0; out += utf8; }
				i += 5; continue;
			}
		}
		out += p[i];
	}
	return out;
}

// 歌詞本文抽出
CStringA ExtractJsonStringSimple(const CStringA& json, const CStringA& key)
{
	CStringA searchKey = "\"" + key + "\":";
	int startPos = json.Find(searchKey);
	if (startPos == -1) return "";
	startPos += searchKey.GetLength();
	while (startPos < json.GetLength() && (json[startPos] == ' ' || json[startPos] == ':' || json[startPos] == '\"')) startPos++;
	int endPos = startPos;
	bool escaped = false;
	while (endPos < json.GetLength()) {
		if (escaped) escaped = false;
		else if (json[endPos] == '\\') escaped = true;
		else if (json[endPos] == '\"') break;
		endPos++;
	}
	CStringA res = json.Mid(startPos, endPos - startPos);
	res = StringReplace(res, "\\n", "\n");
	res = StringReplace(res, "\\\"", "\"");
	return UnescapeJsonUnicode(res);
}

// JSON値抽出 (トップレベル指定版)
CStringA ExtractValueFromBlock(const CStringA& jsonObjectBlock, const CStringA& keyName, bool isString)
{
	const char* pRaw = jsonObjectBlock.GetString();
	int len = jsonObjectBlock.GetLength();
	CStringA searchKey = "\"" + keyName + "\":";
	const char* pKey = searchKey.GetString();
	int keyLen = searchKey.GetLength();
	int depth = 0;
	bool inQuote = false;

	for (int i = 0; i < len; i++) {
		char c = pRaw[i];
		if (!inQuote && depth == 1) {
			if (i + keyLen <= len && strncmp(pRaw + i, pKey, keyLen) == 0) {
				int valStart = i + keyLen;
				while (valStart < len && (pRaw[valStart] == ' ' || pRaw[valStart] == ':')) valStart++;
				int valEnd = valStart;
				if (isString) {
					if (pRaw[valStart] == '\"') {
						valStart++; valEnd = valStart;
						bool escaped = false;
						while (valEnd < len) {
							if (escaped) escaped = false;
							else if (pRaw[valEnd] == '\\') escaped = true;
							else if (pRaw[valEnd] == '\"') break;
							valEnd++;
						}
						return jsonObjectBlock.Mid(valStart, valEnd - valStart);
					}
				}
				else {
					while (valEnd < len && isdigit((unsigned char)pRaw[valEnd])) valEnd++;
					return jsonObjectBlock.Mid(valStart, valEnd - valStart);
				}
			}
		}
		if (c == '\"' && (i == 0 || pRaw[i - 1] != '\\')) { inQuote = !inQuote; }
		if (!inQuote) {
			if (c == '{') depth++; else if (c == '}') depth--;
		}
	}
	return "";
}

// 簡体字 -> 日本語漢字変換
CStringA ConvertSimplifiedToJapanese(const CStringA& utf8Str)
{
	CStringW wideStr = CA2W(utf8Str, CP_UTF8);
	int srcLen = wideStr.GetLength();
	if (srcLen == 0) return "";

	int reqLen = LCMapStringW(0x0804, LCMAP_TRADITIONAL_CHINESE, wideStr, srcLen, NULL, 0);
	CStringW resultW;
	LPWSTR ptr = resultW.GetBuffer(reqLen);
	LCMapStringW(0x0804, LCMAP_TRADITIONAL_CHINESE, wideStr, srcLen, ptr, reqLen);
	resultW.ReleaseBuffer();

	struct KanjiPair {
		const wchar_t* src;
		const wchar_t* dst;
	};

	static const KanjiPair conversionTable[] = {
		{ L"不守", L"不安" }, { L"?", L"猫" }, { L"聲", L"声" }, { L"麼", L"ま" },
		{ L"亞", L"亜" }, { L"惡", L"悪" }, { L"壓", L"圧" }, { L"圍", L"囲" },
		{ L"醫", L"医" }, { L"為", L"為" }, { L"爲", L"為" }, { L"壹", L"壱" },
		{ L"逸", L"逸" }, { L"隱", L"隠" }, { L"營", L"営" }, { L"榮", L"栄" },
		{ L"衛", L"衛" }, { L"衞", L"衛" }, { L"驛", L"駅" }, { L"圓", L"円" },
		{ L"鹽", L"塩" }, { L"奧", L"奥" }, { L"應", L"応" }, { L"歐", L"欧" },
		{ L"毆", L"殴" }, { L"穩", L"穏" }, { L"憶", L"憶" }, { L"橫", L"横" },
		{ L"?", L"温" }, { L"假", L"仮" }, { L"價", L"価" }, { L"畫", L"画" },
		{ L"會", L"会" }, { L"繪", L"絵" }, { L"壞", L"壊" }, { L"懷", L"懐" },
		{ L"擴", L"拡" }, { L"殼", L"殻" }, { L"覺", L"覚" }, { L"學", L"学" },
		{ L"嶽", L"岳" }, { L"樂", L"楽" }, { L"勸", L"勧" }, { L"卷", L"巻" },
		{ L"寬", L"寛" }, { L"歡", L"歓" }, { L"罐", L"缶" }, { L"觀", L"観" },
		{ L"關", L"関" }, { L"陷", L"陥" }, { L"巖", L"巌" }, { L"顏", L"顔" },
		{ L"歸", L"帰" }, { L"氣", L"気" }, { L"龜", L"亀" }, { L"僞", L"偽" },
		{ L"戲", L"戯" }, { L"犧", L"犠" }, { L"舊", L"旧" }, { L"據", L"拠" },
		{ L"舉", L"挙" }, { L"?", L"虚" }, { L"峽", L"峡" }, { L"挾", L"挟" },
		{ L"狹", L"狭" }, { L"曉", L"暁" }, { L"區", L"区" }, { L"驅", L"駆" },
		{ L"勳", L"勲" }, { L"薰", L"薫" }, { L"羣", L"群" }, { L"徑", L"径" },
		{ L"惠", L"恵" }, { L"?", L"掲" }, { L"攜", L"携" }, { L"溪", L"渓" },
		{ L"經", L"経" }, { L"繼", L"継" }, { L"莖", L"茎" }, { L"螢", L"蛍" },
		{ L"輕", L"軽" }, { L"鷄", L"鶏" }, { L"藝", L"芸" }, { L"?", L"撃" },
		{ L"縣", L"県" }, { L"儉", L"倹" }, { L"劍", L"剣" }, { L"圈", L"圏" },
		{ L"檢", L"検" }, { L"權", L"権" }, { L"獻", L"献" }, { L"嚴", L"厳" },
		{ L"?", L"呉" }, { L"?", L"娯" }, { L"效", L"効" }, { L"廣", L"広" },
		{ L"恆", L"恒" }, { L"鑛", L"鉱" }, { L"號", L"号" }, { L"國", L"国" },
		{ L"黑", L"黒" }, { L"穀", L"穀" }, { L"碎", L"砕" }, { L"濟", L"済" },
		{ L"載", L"載" }, { L"劑", L"剤" }, { L"櫻", L"桜" }, { L"雜", L"雑" },
		{ L"參", L"参" }, { L"慘", L"惨" }, { L"蠶", L"蚕" }, { L"贊", L"賛" },
		{ L"殘", L"残" }, { L"絲", L"糸" }, { L"齒", L"歯" }, { L"兒", L"児" },
		{ L"辭", L"辞" }, { L"濕", L"湿" }, { L"實", L"実" }, { L"舍", L"舎" },
		{ L"寫", L"写" }, { L"釋", L"釈" }, { L"壽", L"寿" }, { L"收", L"収" },
		{ L"從", L"従" }, { L"澀", L"渋" }, { L"獸", L"獣" }, { L"縱", L"縦" },
		{ L"肅", L"粛" }, { L"處", L"処" }, { L"敍", L"叙" }, { L"敘", L"叙" },
		{ L"獎", L"奨" }, { L"將", L"将" }, { L"牀", L"床" }, { L"稱", L"称" },
		{ L"證", L"証" }, { L"乘", L"乗" }, { L"剩", L"剰" }, { L"壤", L"壌" },
		{ L"孃", L"嬢" }, { L"條", L"条" }, { L"淨", L"浄" }, { L"疊", L"畳" },
		{ L"寢", L"寝" }, { L"愼", L"慎" }, { L"晉", L"晋" }, { L"盡", L"尽" },
		{ L"圖", L"図" }, { L"粹", L"粋" }, { L"醉", L"酔" }, { L"隨", L"随" },
		{ L"髓", L"髄" }, { L"數", L"数" }, { L"樞", L"枢" }, { L"靜", L"静" },
		{ L"齊", L"斉" }, { L"攝", L"摂" }, { L"竊", L"窃" }, { L"專", L"専" },
		{ L"戰", L"戦" }, { L"淺", L"浅" }, { L"潛", L"潜" }, { L"纖", L"繊" },
		{ L"禪", L"禅" }, { L"雙", L"双" }, { L"壯", L"壮" }, { L"搜", L"捜" },
		{ L"插", L"挿" }, { L"爭", L"争" }, { L"?", L"痩" }, { L"騷", L"騒" },
		{ L"增", L"増" }, { L"藏", L"蔵" }, { L"臟", L"臓" }, { L"屬", L"属" },
		{ L"續", L"続" }, { L"墮", L"堕" }, { L"體", L"体" }, { L"對", L"対" },
		{ L"帶", L"帯" }, { L"滯", L"滞" }, { L"臺", L"台" }, { L"瀧", L"滝" },
		{ L"擇", L"択" }, { L"澤", L"沢" }, { L"單", L"単" }, { L"擔", L"担" },
		{ L"膽", L"胆" }, { L"團", L"団" }, { L"彈", L"弾" }, { L"遲", L"遅" },
		{ L"癡", L"痴" }, { L"蟲", L"虫" }, { L"晝", L"昼" }, { L"鑄", L"鋳" },
		{ L"著", L"着" }, { L"廳", L"庁" }, { L"?", L"徴" }, { L"聽", L"聴" },
		{ L"敕", L"勅" }, { L"鎭", L"鎮" }, { L"墜", L"墜" }, { L"鐵", L"鉄" },
		{ L"點", L"点" }, { L"傳", L"伝" }, { L"黨", L"党" }, { L"盜", L"盗" },
		{ L"燈", L"灯" }, { L"當", L"当" }, { L"鬪", L"闘" }, { L"德", L"徳" },
		{ L"獨", L"独" }, { L"讀", L"読" }, { L"屆", L"届" }, { L"繩", L"縄" },
		{ L"難", L"難" }, { L"貳", L"弐" }, { L"惱", L"悩" }, { L"腦", L"脳" },
		{ L"霸", L"覇" }, { L"廢", L"廃" }, { L"拜", L"拝" }, { L"賣", L"売" },
		{ L"麥", L"麦" }, { L"發", L"発" }, { L"髮", L"髪" }, { L"拔", L"抜" },
		{ L"蠻", L"蛮" }, { L"祕", L"秘" }, { L"?", L"彦" }, { L"?", L"姫" },
		{ L"濱", L"浜" }, { L"拂", L"払" }, { L"佛", L"仏" }, { L"變", L"変" },
		{ L"邊", L"辺" }, { L"辨", L"弁" }, { L"瓣", L"弁" }, { L"辯", L"弁" },
		{ L"舖", L"舗" }, { L"?", L"歩" }, { L"穗", L"穂" }, { L"寶", L"宝" },
		{ L"豐", L"豊" }, { L"沒", L"没" }, { L"飜", L"翻" }, { L"?", L"毎" },
		{ L"萬", L"万" }, { L"滿", L"満" }, { L"默", L"黙" }, { L"彌", L"弥" },
		{ L"藥", L"薬" }, { L"譯", L"訳" }, { L"豫", L"予" }, { L"餘", L"余" },
		{ L"與", L"与" }, { L"譽", L"誉" }, { L"搖", L"揺" }, { L"樣", L"様" },
		{ L"遙", L"遥" }, { L"瑤", L"瑶" }, { L"謠", L"謡" }, { L"來", L"来" },
		{ L"賴", L"頼" }, { L"亂", L"乱" }, { L"覽", L"覧" }, { L"裡", L"里" },
		{ L"龍", L"竜" }, { L"兩", L"両" }, { L"獵", L"猟" }, { L"綠", L"緑" },
		{ L"?", L"涙" }, { L"壘", L"塁" }, { L"勵", L"励" }, { L"禮", L"礼" },
		{ L"隸", L"隷" }, { L"靈", L"霊" }, { L"齡", L"齢" }, { L"?", L"暦" },
		{ L"?", L"歴" }, { L"戀", L"恋" }, { L"?", L"錬" }, { L"爐", L"炉" },
		{ L"勞", L"労" }, { L"樓", L"楼" }, { L"?", L"録" }, { L"灣", L"湾" },
		{ L"轉", L"転" }, { L"仆", L"僕" }, { L"葉", L"叶" }
	};

	for (const auto& pair : conversionTable) {
		resultW.Replace(pair.src, pair.dst);
	}

	return CStringA(CW2A(resultW, CP_UTF8));
}

// =========================================================
// ★カタカナ→英単語辞書（一般的な外来語）
// =========================================================
struct KatakanaWordMap {
	const wchar_t* katakana;
	const wchar_t* english;
};

static const KatakanaWordMap g_katakanaWords[] = {
	// 頻出単語
	{ L"アンド", L"and" }, { L"ザ", L"the" }, { L"オブ", L"of" }, { L"フォー", L"for" },
	{ L"トゥ", L"to" }, { L"イン", L"in" }, { L"ウィズ", L"with" }, { L"オン", L"on" },

	// 音楽関連
	{ L"ロック", L"lock" }, { L"キー", L"key" }, { L"ガール", L"girl" }, { L"ボーイ", L"boy" },
	{ L"フレンド", L"friend" }, { L"ラブ", L"love" }, { L"ハート", L"heart" }, { L"ドリーム", L"dream" },
	{ L"ナイト", L"night" }, { L"デイ", L"day" }, { L"サマー", L"summer" }, { L"ウィンター", L"winter" },
	{ L"スプリング", L"spring" }, { L"オータム", L"autumn" }, { L"レイン", L"rain" }, { L"サン", L"sun" },
	{ L"ムーン", L"moon" }, { L"スター", L"star" }, { L"スカイ", L"sky" }, { L"シー", L"sea" },
	{ L"オーシャン", L"ocean" }, { L"リバー", L"river" }, { L"マウンテン", L"mountain" },

	// 複合語
	{ L"ガールフレンド", L"girlfriend" }, { L"ボーイフレンド", L"boyfriend" },
	{ L"サンシャイン", L"sunshine" }, { L"レインボー", L"rainbow" }, { L"スターライト", L"starlight" },
	{ L"ムーンライト", L"moonlight" }, { L"サンライズ", L"sunrise" }, { L"サンセット", L"sunset" },

	// 色
	{ L"レッド", L"red" }, { L"ブルー", L"blue" }, { L"グリーン", L"green" }, { L"イエロー", L"yellow" },
	{ L"ホワイト", L"white" }, { L"ブラック", L"black" }, { L"ピンク", L"pink" }, { L"パープル", L"purple" },

	// 感情・状態
	{ L"ハッピー", L"happy" }, { L"サッド", L"sad" }, { L"クレイジー", L"crazy" }, { L"ロンリー", L"lonely" },
	{ L"スイート", L"sweet" }, { L"ビター", L"bitter" }, { L"コールド", L"cold" }, { L"ホット", L"hot" },

	// 場所
	{ L"タウン", L"town" }, { L"シティ", L"city" }, { L"ストリート", L"street" }, { L"ロード", L"road" },
	{ L"ホーム", L"home" }, { L"ハウス", L"house" }, { L"ルーム", L"room" }, { L"ドア", L"door" },
	{ L"ウィンドウ", L"window" }, { L"ガーデン", L"garden" }, { L"パーク", L"park" },

	// 時間
	{ L"トゥデイ", L"today" }, { L"トゥモロー", L"tomorrow" }, { L"イエスタデイ", L"yesterday" },
	{ L"ナウ", L"now" }, { L"フォーエバー", L"forever" }, { L"エバー", L"ever" }, { L"ネバー", L"never" },
	{ L"オールウェイズ", L"always" }, { L"サムタイムズ", L"sometimes" },

	// 人
	{ L"レディ", L"lady" }, { L"マン", L"man" }, { L"ウーマン", L"woman" }, { L"ベイビー", L"baby" },
	{ L"チャイルド", L"child" }, { L"エンジェル", L"angel" }, { L"クイーン", L"queen" }, { L"キング", L"king" },

	// 動作
	{ L"ダンス", L"dance" }, { L"ウォーク", L"walk" }, { L"ラン", L"run" }, { L"フライ", L"fly" },
	{ L"ドライブ", L"drive" }, { L"ライド", L"ride" }, { L"シング", L"sing" }, { L"クライ", L"cry" },
	{ L"スマイル", L"smile" }, { L"キス", L"kiss" }, { L"ハグ", L"hug" },

	// 音楽ジャンル・楽器
	{ L"ジャズ", L"jazz" }, { L"ブルース", L"blues" }, { L"ロックンロール", L"rock and roll" },
	{ L"ポップ", L"pop" }, { L"ソウル", L"soul" }, { L"ファンク", L"funk" },
	{ L"ピアノ", L"piano" }, { L"ギター", L"guitar" }, { L"ドラム", L"drum" }, { L"ベース", L"bass" },

	// その他頻出語
	{ L"パワー", L"power" }, { L"マジック", L"magic" }, { L"ワンダー", L"wonder" },
	{ L"ファイア", L"fire" }, { L"ウォーター", L"water" }, { L"エアー", L"air" }, { L"アース", L"earth" },
	{ L"ライト", L"light" }, { L"ダーク", L"dark" }, { L"シャドウ", L"shadow" },
	{ L"ミラクル", L"miracle" }, { L"メモリー", L"memory" }, { L"ストーリー", L"story" },
	{ L"ソング", L"song" }, { L"ミュージック", L"music" }, { L"メロディ", L"melody" },
	{ L"リズム", L"rhythm" }, { L"ビート", L"beat" }, { L"サウンド", L"sound" },

	// 数字・序数
	{ L"ワン", L"one" }, { L"トゥー", L"two" }, { L"スリー", L"three" }, { L"フォー", L"four" },
	{ L"ファイブ", L"five" }, { L"シックス", L"six" }, { L"セブン", L"seven" }, { L"エイト", L"eight" },
	{ L"ナイン", L"nine" }, { L"テン", L"ten" },

	// その他
	{ L"ワールド", L"world" }, { L"ライフ", L"life" }, { L"タイム", L"time" }, { L"スペース", L"space" },
	{ L"フューチャー", L"future" }, { L"パスト", L"past" }, { L"ヒストリー", L"history" },
	{ L"フリーダム", L"freedom" }, { L"ピース", L"peace" }, { L"ホープ", L"hope" },
};

// カタカナ語彙辞書検索（長い単語優先）
CString LookupKatakanaWord(const CString& katakana)
{
	// 完全一致検索
	for (const auto& entry : g_katakanaWords) {
		if (katakana.CompareNoCase(entry.katakana) == 0) {
			return entry.english;
		}
	}
	return _T("");
}

// =========================================================
// ★カタカナ→英単語変換（辞書ベース）
// =========================================================
CString KatakanaToEnglishWords(const CString& katakana)
{
	if (katakana.IsEmpty()) return _T("");

	// 区切り文字で分割（・、スペース、中黒など）
	CString text = katakana;
	text.Replace(L"・", L" ");
	text.Replace(L"　", L" ");
	text.Replace(L"・", L" ");
	text.Replace(L"＆", L" ");
	text.Replace(L"&", L" ");

	std::vector<CString> words;
	int start = 0;
	for (int i = 0; i <= text.GetLength(); i++) {
		if (i == text.GetLength() || text[i] == L' ') {
			if (i > start) {
				CString word = text.Mid(start, i - start);
				word.TrimLeft(); word.TrimRight();
				if (!word.IsEmpty()) {
					words.push_back(word);
				}
			}
			start = i + 1;
		}
	}

	// 各単語を変換
	CString result;
	for (const auto& word : words) {
		if (!result.IsEmpty()) result += L" ";

		// 辞書検索
		CString englishWord = LookupKatakanaWord(word);
		if (!englishWord.IsEmpty()) {
			result += englishWord;
			WriteDebugLog(_T("  語彙辞書ヒット: ") + word + _T(" -> ") + englishWord);
		}
		else {
			// 辞書にない場合はローマ字化
			result += KatakanaToRomaji(word);
		}
	}

	return result;
}

// =========================================================
// カタカナ→ローマ字変換
// =========================================================
CString KatakanaToRomaji(const CString& katakana)
{
	struct KanaMap {
		const wchar_t* kana;
		const wchar_t* romaji;
	};

	static const KanaMap kanaTable[] = {
		// 拗音（2文字）- 先に処理
		{ L"ヴァ", L"va" }, { L"ヴィ", L"vi" }, { L"ヴェ", L"ve" }, { L"ヴォ", L"vo" }, { L"ヴ", L"vu" },
		{ L"キャ", L"kya" }, { L"キュ", L"kyu" }, { L"キョ", L"kyo" },
		{ L"シャ", L"sha" }, { L"シュ", L"shu" }, { L"ショ", L"sho" },
		{ L"チャ", L"cha" }, { L"チュ", L"chu" }, { L"チョ", L"cho" },
		{ L"ニャ", L"nya" }, { L"ニュ", L"nyu" }, { L"ニョ", L"nyo" },
		{ L"ヒャ", L"hya" }, { L"ヒュ", L"hyu" }, { L"ヒョ", L"hyo" },
		{ L"ミャ", L"mya" }, { L"ミュ", L"myu" }, { L"ミョ", L"myo" },
		{ L"リャ", L"rya" }, { L"リュ", L"ryu" }, { L"リョ", L"ryo" },
		{ L"ギャ", L"gya" }, { L"ギュ", L"gyu" }, { L"ギョ", L"gyo" },
		{ L"ジャ", L"ja" }, { L"ジュ", L"ju" }, { L"ジョ", L"jo" },
		{ L"ビャ", L"bya" }, { L"ビュ", L"byu" }, { L"ビョ", L"byo" },
		{ L"ピャ", L"pya" }, { L"ピュ", L"pyu" }, { L"ピョ", L"pyo" },
		{ L"ファ", L"fa" }, { L"フィ", L"fi" }, { L"フェ", L"fe" }, { L"フォ", L"fo" },
		{ L"ウィ", L"wi" }, { L"ウェ", L"we" }, { L"ウォ", L"wo" },
		{ L"ティ", L"ti" }, { L"ディ", L"di" }, { L"デュ", L"du" },
		{ L"チェ", L"che" }, { L"ジェ", L"je" },
		// 清音
		{ L"ア", L"a" }, { L"イ", L"i" }, { L"ウ", L"u" }, { L"エ", L"e" }, { L"オ", L"o" },
		{ L"カ", L"ka" }, { L"キ", L"ki" }, { L"ク", L"ku" }, { L"ケ", L"ke" }, { L"コ", L"ko" },
		{ L"サ", L"sa" }, { L"シ", L"shi" }, { L"ス", L"su" }, { L"セ", L"se" }, { L"ソ", L"so" },
		{ L"タ", L"ta" }, { L"チ", L"chi" }, { L"ツ", L"tsu" }, { L"テ", L"te" }, { L"ト", L"to" },
		{ L"ナ", L"na" }, { L"ニ", L"ni" }, { L"ヌ", L"nu" }, { L"ネ", L"ne" }, { L"ノ", L"no" },
		{ L"ハ", L"ha" }, { L"ヒ", L"hi" }, { L"フ", L"fu" }, { L"ヘ", L"he" }, { L"ホ", L"ho" },
		{ L"マ", L"ma" }, { L"ミ", L"mi" }, { L"ム", L"mu" }, { L"メ", L"me" }, { L"モ", L"mo" },
		{ L"ヤ", L"ya" }, { L"ユ", L"yu" }, { L"ヨ", L"yo" },
		{ L"ラ", L"ra" }, { L"リ", L"ri" }, { L"ル", L"ru" }, { L"レ", L"re" }, { L"ロ", L"ro" },
		{ L"ワ", L"wa" }, { L"ヲ", L"wo" }, { L"ン", L"n" },
		// 濁音
		{ L"ガ", L"ga" }, { L"ギ", L"gi" }, { L"グ", L"gu" }, { L"ゲ", L"ge" }, { L"ゴ", L"go" },
		{ L"ザ", L"za" }, { L"ジ", L"ji" }, { L"ズ", L"zu" }, { L"ゼ", L"ze" }, { L"ゾ", L"zo" },
		{ L"ダ", L"da" }, { L"ヂ", L"ji" }, { L"ヅ", L"zu" }, { L"デ", L"de" }, { L"ド", L"do" },
		{ L"バ", L"ba" }, { L"ビ", L"bi" }, { L"ブ", L"bu" }, { L"ベ", L"be" }, { L"ボ", L"bo" },
		// 半濁音
		{ L"パ", L"pa" }, { L"ピ", L"pi" }, { L"プ", L"pu" }, { L"ペ", L"pe" }, { L"ポ", L"po" },
		// その他
		{ L"ー", L"" }, { L"・", L" " }, { L"　", L" " }
	};

	CString result;
	int len = katakana.GetLength();

	for (int i = 0; i < len; i++) {
		bool found = false;

		// ★促音「ッ」の処理（次の子音を重ねる）
		if (katakana[i] == L'ッ' && i + 1 < len) {
			// 次の文字の子音を取得
			wchar_t nextChar = katakana[i + 1];

			// 2文字の拗音チェック
			if (i + 2 < len) {
				CString twoChar = katakana.Mid(i + 1, 2);
				for (const auto& kana : kanaTable) {
					if (twoChar == kana.kana) {
						CString romaji = kana.romaji;
						if (!romaji.IsEmpty()) {
							// 最初の子音を重ねる
							result += romaji[0];
						}
						found = true;
						break;
					}
				}
			}

			// 1文字でチェック
			if (!found) {
				for (const auto& kana : kanaTable) {
					if (nextChar == kana.kana[0] && wcslen(kana.kana) == 1) {
						CString romaji = kana.romaji;
						if (!romaji.IsEmpty()) {
							// 最初の子音を重ねる（k, s, t, p など）
							wchar_t consonant = romaji[0];
							if (consonant != L'a' && consonant != L'i' &&
								consonant != L'u' && consonant != L'e' && consonant != L'o') {
								result += consonant;
							}
						}
						found = true;
						break;
					}
				}
			}
			continue;
		}

		// 2文字の拗音を優先
		if (i + 1 < len) {
			CString twoChar = katakana.Mid(i, 2);
			for (const auto& kana : kanaTable) {
				if (wcslen(kana.kana) == 2 && twoChar == kana.kana) {
					result += kana.romaji;
					i++; // 2文字消費
					found = true;
					break;
				}
			}
		}

		// 1文字の変換
		if (!found) {
			for (const auto& kana : kanaTable) {
				if (wcslen(kana.kana) == 1 && katakana[i] == kana.kana[0]) {
					result += kana.romaji;
					found = true;
					break;
				}
			}
		}

		// 変換できない文字はそのまま
		if (!found) {
			result += katakana[i];
		}
	}

	result.TrimLeft();
	result.TrimRight();

	// 連続するスペースを1つに
	while (result.Find(L"  ") != -1) {
		result.Replace(L"  ", L" ");
	}

	return result;
}

// =========================================================
// アーティスト名辞書（よく使う200件）
// =========================================================
struct ArtistMapping {
	const wchar_t* katakana;
	const wchar_t* english;
};

static const ArtistMapping g_artistDict[] = {
	// 海外アーティスト
	{ L"ジュリア・フォーダム", L"Julia Fordham" },
	{ L"マドンナ", L"Madonna" },
	{ L"ビートルズ", L"The Beatles" },
	{ L"マイケル・ジャクソン", L"Michael Jackson" },
	{ L"マライア・キャリー", L"Mariah Carey" },
	{ L"セリーヌ・ディオン", L"Celine Dion" },
	{ L"ホイットニー・ヒューストン", L"Whitney Houston" },
	{ L"レディー・ガガ", L"Lady Gaga" },
	{ L"テイラー・スウィフト", L"Taylor Swift" },
	{ L"アリアナ・グランデ", L"Ariana Grande" },
	{ L"ビヨンセ", L"Beyonce" },
	{ L"ブリトニー・スピアーズ", L"Britney Spears" },
	{ L"クイーン", L"Queen" },
	{ L"ボン・ジョヴィ", L"Bon Jovi" },
	{ L"エアロスミス", L"Aerosmith" },
	{ L"ガンズ・アンド・ローゼズ", L"Guns N' Roses" },
	{ L"メタリカ", L"Metallica" },
	{ L"ニルヴァーナ", L"Nirvana" },
	{ L"レッド・ツェッペリン", L"Led Zeppelin" },
	{ L"ピンク・フロイド", L"Pink Floyd" },
	{ L"ザ・ローリング・ストーンズ", L"The Rolling Stones" },
	{ L"エルトン・ジョン", L"Elton John" },
	{ L"デヴィッド・ボウイ", L"David Bowie" },
	{ L"プリンス", L"Prince" },
	{ L"ブルーノ・マーズ", L"Bruno Mars" },
	{ L"エド・シーラン", L"Ed Sheeran" },
	{ L"アデル", L"Adele" },
	{ L"サム・スミス", L"Sam Smith" },
	{ L"ビリー・アイリッシュ", L"Billie Eilish" },
	{ L"ザ・ウィークエンド", L"The Weeknd" },
	{ L"ドレイク", L"Drake" },
	{ L"カニエ・ウェスト", L"Kanye West" },
	{ L"エミネム", L"Eminem" },
	{ L"リアーナ", L"Rihanna" },
	{ L"ケイティ・ペリー", L"Katy Perry" },
	{ L"デュア・リパ", L"Dua Lipa" },
	{ L"コールドプレイ", L"Coldplay" },
	{ L"イマジン・ドラゴンズ", L"Imagine Dragons" },
	{ L"マルーン5", L"Maroon 5" },
	{ L"ワン・ダイレクション", L"One Direction" },
	{ L"バックストリート・ボーイズ", L"Backstreet Boys" },
	{ L"エンシンク", L"NSYNC" },
	{ L"スパイス・ガールズ", L"Spice Girls" },
	{ L"シャナイア・トゥエイン", L"Shania Twain" },
	{ L"キャロル・キング", L"Carole King" },
	{ L"ダイアナ・ロス", L"Diana Ross" },
	{ L"アレサ・フランクリン", L"Aretha Franklin" },
	{ L"スティービー・ワンダー", L"Stevie Wonder" },
	{ L"レイ・チャールズ", L"Ray Charles" },
	{ L"ビリー・ジョエル", L"Billy Joel" },
	{ L"ポール・マッカートニー", L"Paul McCartney" },
	{ L"ジョン・レノン", L"John Lennon" },
	{ L"ボブ・ディラン", L"Bob Dylan" },
	{ L"ブルース・スプリングスティーン", L"Bruce Springsteen" },
	{ L"U2", L"U2" },
	{ L"オアシス", L"Oasis" },
	{ L"レディオヘッド", L"Radiohead" },
	{ L"ミューズ", L"Muse" },
	{ L"アークティック・モンキーズ", L"Arctic Monkeys" },
	{ L"グリーン・デイ", L"Green Day" },
	{ L"リンキン・パーク", L"Linkin Park" },
	{ L"フー・ファイターズ", L"Foo Fighters" },
	{ L"レッド・ホット・チリ・ペッパーズ", L"Red Hot Chili Peppers" },
	{ L"ブリンク182", L"Blink-182" },
	{ L"サム41", L"Sum 41" },
	{ L"オフスプリング", L"The Offspring" },
	{ L"パール・ジャム", L"Pearl Jam" },
	{ L"サウンドガーデン", L"Soundgarden" },
	{ L"ブラック・サバス", L"Black Sabbath" },
	{ L"ディープ・パープル", L"Deep Purple" },
	{ L"AC/DC", L"AC/DC" },
	{ L"アイアン・メイデン", L"Iron Maiden" },
	{ L"ジューダス・プリースト", L"Judas Priest" },
	{ L"スレイヤー", L"Slayer" },
	{ L"メガデス", L"Megadeth" },
	{ L"アンスラックス", L"Anthrax" },
	{ L"パンテラ", L"Pantera" },
	{ L"セパルトゥラ", L"Sepultura" },
	{ L"スリップノット", L"Slipknot" },
	{ L"システム・オブ・ア・ダウン", L"System of a Down" },
	{ L"コーン", L"Korn" },
	{ L"リンプ・ビズキット", L"Limp Bizkit" },
	{ L"デフトーンズ", L"Deftones" },
	{ L"ナイン・インチ・ネイルズ", L"Nine Inch Nails" },
	{ L"マリリン・マンソン", L"Marilyn Manson" },
	{ L"ラムシュタイン", L"Rammstein" },
	{ L"ナイトウィッシュ", L"Nightwish" },
	{ L"エピカ", L"Epica" },
	{ L"ウィズイン・テンプテーション", L"Within Temptation" },
	{ L"エヴァネッセンス", L"Evanescence" },
	{ L"シンフォニー・エックス", L"Symphony X" },
	{ L"ドリーム・シアター", L"Dream Theater" },
	{ L"プログレッシヴ", L"Progressive" },
	{ L"イエス", L"Yes" },
	{ L"ジェネシス", L"Genesis" },
	{ L"キング・クリムゾン", L"King Crimson" },
	{ L"エマーソン・レイク・アンド・パーマー", L"Emerson Lake and Palmer" },
	{ L"ジェスロ・タル", L"Jethro Tull" },
	{ L"ラッシュ", L"Rush" },
	{ L"カンザス", L"Kansas" },
	{ L"ボストン", L"Boston" },
	{ L"ジャーニー", L"Journey" },
	{ L"フォリナー", L"Foreigner" },
	{ L"TOTO", L"Toto" },
	{ L"シカゴ", L"Chicago" },
	{ L"アース・ウィンド・アンド・ファイアー", L"Earth Wind and Fire" },
	{ L"ホール＆オーツ", L"Hall and Oates" },
	{ L"ヒューイ・ルイス＆ザ・ニュース", L"Huey Lewis and the News" },
	{ L"デュラン・デュラン", L"Duran Duran" },
	{ L"カルチャー・クラブ", L"Culture Club" },
	{ L"ボーイ・ジョージ", L"Boy George" },
	{ L"a-ha", L"a-ha" },
	{ L"ペット・ショップ・ボーイズ", L"Pet Shop Boys" },
	{ L"デペッシュ・モード", L"Depeche Mode" },
	{ L"ニュー・オーダー", L"New Order" },
	{ L"ザ・スミス", L"The Smiths" },
	{ L"モリッシー", L"Morrissey" },
	{ L"ザ・キュアー", L"The Cure" },
	{ L"ジョイ・ディヴィジョン", L"Joy Division" },
	{ L"ソフト・セル", L"Soft Cell" },
	{ L"ヒューマン・リーグ", L"The Human League" },
	{ L"ユーリズミックス", L"Eurythmics" },
	{ L"アニー・レノックス", L"Annie Lennox" },
	{ L"シンディ・ローパー", L"Cyndi Lauper" },
	{ L"ジョーン・ジェット", L"Joan Jett" },
	{ L"パット・ベネター", L"Pat Benatar" },
	{ L"ボニー・タイラー", L"Bonnie Tyler" },
	{ L"ティナ・ターナー", L"Tina Turner" },
	{ L"シェール", L"Cher" },
	{ L"ダイアナ・キング", L"Diana King" },
	{ L"ソフィア・ローレン", L"Sophia Loren" },
	{ L"サラ・ブライトマン", L"Sarah Brightman" },
	{ L"シャーデー", L"Sade" },
	{ L"ノラ・ジョーンズ", L"Norah Jones" },
	{ L"アリシア・キーズ", L"Alicia Keys" },
	{ L"ジョン・メイヤー", L"John Mayer" },
	{ L"ジェイソン・ムラーズ", L"Jason Mraz" },
	{ L"コリン・ヘイ", L"Colbie Caillat" },
	{ L"ジャック・ジョンソン", L"Jack Johnson" },
	{ L"ベン・フォールズ", L"Ben Folds" },
	{ L"レジーナ・スペクター", L"Regina Spektor" },
	{ L"ファイスト", L"Feist" },
	{ L"KT・タンストール", L"KT Tunstall" },
	{ L"エイミー・ワインハウス", L"Amy Winehouse" },
	{ L"ジョス・ストーン", L"Joss Stone" },
	{ L"コリーヌ・ベイリー・レイ", L"Corinne Bailey Rae" },
	{ L"ダフィー", L"Duffy" },
	{ L"レオナ・ルイス", L"Leona Lewis" },
	{ L"ジェシー・J", L"Jessie J" },
	{ L"エリー・ゴールディング", L"Ellie Goulding" },
	{ L"フローレンス・アンド・ザ・マシーン", L"Florence and the Machine" },
	{ L"ラナ・デル・レイ", L"Lana Del Rey" },
	{ L"ロード", L"Lorde" },
	{ L"ハルシー", L"Halsey" },
	{ L"セレーナ・ゴメス", L"Selena Gomez" },
	{ L"デミ・ロヴァート", L"Demi Lovato" },
	{ L"マイリー・サイラス", L"Miley Cyrus" },
	{ L"ケシャ", L"Kesha" },
	{ L"ピットブル", L"Pitbull" },
	{ L"フロー・ライダー", L"Flo Rida" },
	{ L"ニッキー・ミナージュ", L"Nicki Minaj" },
	{ L"カーディ・B", L"Cardi B" },
	{ L"ミーゴス", L"Migos" },
	{ L"ポスト・マローン", L"Post Malone" },
	{ L"トラヴィス・スコット", L"Travis Scott" },
	{ L"ジュース・ワールド", L"Juice WRLD" },
	{ L"リル・ナズ・X", L"Lil Nas X" },
	{ L"メーガン・ジー・スタリオン", L"Megan Thee Stallion" },
	{ L"ドージャ・キャット", L"Doja Cat" },
	{ L"オリヴィア・ロドリゴ", L"Olivia Rodrigo" },
	{ L"ビリー・ジョエル", L"Billy Joel" },
	{ L"フィル・コリンズ", L"Phil Collins" },
	{ L"スティング", L"Sting" },
	{ L"ロッド・スチュワート", L"Rod Stewart" },
	{ L"ヴァン・モリソン", L"Van Morrison" },
	{ L"ジェームス・テイラー", L"James Taylor" },
	{ L"ニール・ヤング", L"Neil Young" },
	{ L"トム・ペティ", L"Tom Petty" },
	{ L"ジョージ・ハリスン", L"George Harrison" },
	{ L"エリック・クラプトン", L"Eric Clapton" },
	{ L"ジェフ・ベック", L"Jeff Beck" },
	{ L"ジミー・ペイジ", L"Jimmy Page" },
	{ L"カルロス・サンタナ", L"Carlos Santana" },
	{ L"スティーヴ・ヴァイ", L"Steve Vai" },
	{ L"ジョー・サトリアーニ", L"Joe Satriani" },
	{ L"イングウェイ・マルムスティーン", L"Yngwie Malmsteen" },
	{ L"ジョン・ペトルーシ", L"John Petrucci" },

	// 日本人アーティスト（カタカナ表記されることがあるもの）
	{ L"エックス・ジャパン", L"X Japan" },
	{ L"ビーズ", L"B'z" },
	{ L"ラルク・アン・シエル", L"L'Arc~en~Ciel" },
	{ L"グレイ", L"GLAY" },
	{ L"ルナシー", L"LUNACY" },
	{ L"シド", L"SID" },
	{ L"ディル・アン・グレイ", L"Dir en grey" },
	{ L"ムック", L"MUCC" },
	{ L"ガゼット", L"the GazettE" },
	{ L"アリス・ナイン", L"Alice Nine" }
};

// 辞書検索
CString LookupArtistDict(const CString& katakana)
{
	CString normalized = katakana;
	normalized.Replace(L"　", L""); // 全角スペース削除
	normalized.Replace(L" ", L"");  // 半角スペース削除
	normalized.TrimLeft(); normalized.TrimRight();

	for (const auto& entry : g_artistDict) {
		CString dictKana = entry.katakana;
		dictKana.Replace(L"　", L"");
		dictKana.Replace(L" ", L"");
		if (normalized.Find(dictKana) >= 0) {
			WriteDebugLog(_T("★辞書ヒット: ") + katakana + _T(" -> ") + entry.english);
			return entry.english;
		}
	}
	return _T("");
}

// =========================================================
// キャッシュ機能（INIファイル使用）
// =========================================================
CString GetCachePath()
{
	TCHAR tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	return CString(tempPath) + CACHE_FILE_NAME;
}

CString LoadFromCache(const CString& katakana)
{
	CString cachePath = GetCachePath();
	TCHAR buffer[512];
	GetPrivateProfileString(_T("ArtistCache"), katakana, _T(""), buffer, 512, cachePath);
	CString result = buffer;
	if (!result.IsEmpty()) {
		WriteDebugLog(_T("★キャッシュヒット: ") + katakana + _T(" -> ") + result);
	}
	return result;
}

void SaveToCache(const CString& katakana, const CString& english)
{
	CString cachePath = GetCachePath();
	WritePrivateProfileString(_T("ArtistCache"), katakana, english, cachePath);
	WriteDebugLog(_T("★キャッシュ保存: ") + katakana + _T(" -> ") + english);
}

// =========================================================
// MusicBrainz API検索
// =========================================================
CString QueryMusicBrainzAPI(const CString& artistName)
{
	WriteDebugLog(_T("MusicBrainz API検索: ") + artistName);

	// レート制限対応（1秒待ち）
	static DWORD lastCallTime = 0;
	DWORD currentTime = GetTickCount();
	if (currentTime - lastCallTime < 1000) {
		Sleep(1000 - (currentTime - lastCallTime));
	}
	lastCallTime = GetTickCount();

	// API呼び出し
	CString url;
	url.Format(_T("https://musicbrainz.org/ws/2/artist/?query=%s&fmt=json&limit=5"),
		UrlEncode(artistName));

	CStringA response = HttpGet(url, MUSICBRAINZ_USER_AGENT);
	if (response.IsEmpty()) {
		WriteDebugLog(_T("MusicBrainz APIレスポンスなし"));
		return _T("");
	}

	// JSONパース（簡易版：最初のartistのnameを取得）
	int artistsPos = response.Find("\"artists\":");
	if (artistsPos == -1) return _T("");

	int arrayStart = response.Find("[", artistsPos);
	if (arrayStart == -1) return _T("");

	int firstObjStart = response.Find("{", arrayStart);
	if (firstObjStart == -1) return _T("");

	// name フィールド抽出
	CStringA nameValue = ExtractJsonStringSimple(response.Mid(firstObjStart), "name");
	if (nameValue.IsEmpty()) return _T("");

	// UTF-8 -> Unicode
	CString result = CA2T(nameValue, CP_UTF8);

	// 英数字のみの場合は採用（日本語名は除外）
	bool hasNonAscii = false;
	for (int i = 0; i < result.GetLength(); i++) {
		wchar_t ch = result[i];
		if (ch > 0x7F) { // ASCII範囲外
			hasNonAscii = true;
			break;
		}
	}

	if (!hasNonAscii && !result.IsEmpty()) {
		WriteDebugLog(_T("★MusicBrainz成功: ") + artistName + _T(" -> ") + result);
		return result;
	}

	WriteDebugLog(_T("MusicBrainz: 日本語名のため除外"));
	return _T("");
}

// =========================================================
// ★タイトルキャッシュ（日英タイトル対応の自動学習）
// =========================================================

// タイトルキャッシュに保存
void SaveTitleToCache(const CString& japaneseTitle, const CString& artistName, const CString& englishTitle)
{
	TCHAR tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	CString iniPath = CString(tempPath) + _T("title_cache.ini");

	// キー：「曲名|アーティスト名」
	CString key = japaneseTitle;
	if (!artistName.IsEmpty()) {
		key += _T("|") + artistName;
	}

	WritePrivateProfileString(_T("TitleCache"), key, englishTitle, iniPath);
	WriteDebugLog(_T("★タイトルキャッシュ保存: ") + key + _T(" = ") + englishTitle);
}

// タイトルキャッシュから読み込み
CString LoadTitleFromCache(const CString& japaneseTitle, const CString& artistName)
{
	TCHAR tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	CString iniPath = CString(tempPath) + _T("title_cache.ini");

	// キー：「曲名|アーティスト名」
	CString key = japaneseTitle;
	if (!artistName.IsEmpty()) {
		key += _T("|") + artistName;
	}

	TCHAR buffer[512] = { 0 };
	GetPrivateProfileString(_T("TitleCache"), key, _T(""), buffer, 512, iniPath);

	CString result = buffer;
	if (!result.IsEmpty()) {
		WriteDebugLog(_T("★タイトルキャッシュヒット: ") + key + _T(" -> ") + result);
	}
	return result;
}

// =========================================================
// ★日英タイトル取得（キャッシュ優先→Wikipedia→MusicBrainz）
// =========================================================
CString GetEnglishTitle(const CString& japaneseTitle, const CString& artistName)
{
	// 1. キャッシュ確認（最速）
	CString cached = LoadTitleFromCache(japaneseTitle, artistName);
	if (!cached.IsEmpty()) {
		return cached;
	}

	// 2. 日本語チェック（英語のみの場合は変換不要）
	bool hasJapanese = false;
	for (int i = 0; i < japaneseTitle.GetLength(); i++) {
		wchar_t ch = japaneseTitle[i];
		if ((ch >= 0x3040 && ch <= 0x309F) ||  // ひらがな
			(ch >= 0x30A0 && ch <= 0x30FF) ||  // カタカナ
			(ch >= 0x4E00 && ch <= 0x9FFF)) {  // 漢字
			hasJapanese = true;
			break;
		}
	}

	if (!hasJapanese) {
		return _T(""); // 日本語なし→変換不要
	}

	// 3. Wikipedia APIで英語タイトル取得（優先）
	CString wikipediaTitle = QueryEnglishTitleFromWikipedia(japaneseTitle, artistName);
	if (!wikipediaTitle.IsEmpty() && wikipediaTitle != japaneseTitle) {
		SaveTitleToCache(japaneseTitle, artistName, wikipediaTitle);
		return wikipediaTitle;
	}

	// 4. MusicBrainz APIで英語タイトル取得（バックアップ）
	CString englishTitle = QueryEnglishTitleFromMusicBrainz(japaneseTitle, artistName);
	if (!englishTitle.IsEmpty() && englishTitle != japaneseTitle) {
		SaveTitleToCache(japaneseTitle, artistName, englishTitle);
		return englishTitle;
	}

	return _T("");
}

// =========================================================
// ★タイトル類似度チェック（誤検出防止）
// =========================================================
int CalculateTitleSimilarity(const CString& title1, const CString& title2)
{
	// 正規化（小文字化、スペース削除）
	CString normalized1 = title1;
	CString normalized2 = title2;

	normalized1.MakeLower();
	normalized2.MakeLower();
	normalized1.Replace(L" ", L"");
	normalized2.Replace(L"　", L"");
	normalized1.Replace(L"-", L"");
	normalized2.Replace(L"-", L"");
	normalized1.Replace(L"_", L"");
	normalized2.Replace(L"_", L"");

	// 完全一致
	if (normalized1 == normalized2) return 100;

	// 部分一致（長い方が短い方を含む）
	if (normalized1.GetLength() > normalized2.GetLength()) {
		if (normalized1.Find(normalized2) != -1) return 80;
	}
	else {
		if (normalized2.Find(normalized1) != -1) return 80;
	}

	// レーベンシュタイン距離の簡易版（先頭一致度）
	int matchCount = 0;
	int minLen = min(normalized1.GetLength(), normalized2.GetLength());
	for (int i = 0; i < minLen; i++) {
		if (normalized1[i] == normalized2[i]) {
			matchCount++;
		}
		else {
			break;
		}
	}

	if (minLen > 0) {
		return (matchCount * 100) / minLen;
	}

	return 0;
}

// =========================================================
// 前方宣言
// =========================================================
bool IsVariousArtists(const CString& artistName);
CString QueryEnglishTitleFromWikipediaLangLinks(const CString& pageTitle);

// =========================================================
// ★ハイブリッド変換: カタカナ → 英語名（最適化版）
// =========================================================
CString KatakanaToEnglish(const CString& katakana, bool useMusicBrainz = true)
{
	if (katakana.IsEmpty()) return _T("");

	// カタカナが含まれているか確認
	bool hasKatakana = false;
	for (int i = 0; i < katakana.GetLength(); i++) {
		wchar_t ch = katakana[i];
		if (ch >= 0x30A0 && ch <= 0x30FF) {
			hasKatakana = true;
			break;
		}
	}

	if (!hasKatakana) return katakana; // カタカナなしならそのまま

	// 1. キャッシュ確認（最速）
	CString cached = LoadFromCache(katakana);
	if (!cached.IsEmpty()) return cached;

	// 2. 辞書確認（高速）
	CString dictResult = LookupArtistDict(katakana);
	if (!dictResult.IsEmpty()) {
		SaveToCache(katakana, dictResult);
		return dictResult;
	}

	// 3. ローマ字変換（高速・MusicBrainz APIは使わない）
	CString romaji = KatakanaToRomaji(katakana);

	// ★MusicBrainz APIは明示的に要求された場合のみ使用
	// 通常の検索ではローマ字で妥協して高速化
	if (!useMusicBrainz) {
		WriteDebugLog(_T("★ローマ字変換（高速モード）: ") + katakana + _T(" -> ") + romaji);
		return romaji;
	}

	// 4. MusicBrainz API（最終手段・低速）
	CString apiResult = QueryMusicBrainzAPI(katakana);
	if (!apiResult.IsEmpty()) {
		SaveToCache(katakana, apiResult);
		return apiResult;
	}

	// 5. ローマ字で妥協
	WriteDebugLog(_T("★ローマ字変換: ") + katakana + _T(" -> ") + romaji);
	return romaji;
}

// =========================================================
// ★Wikipedia言語間リンクで英語ページタイトル取得（フォールバック用）
// =========================================================
CString QueryEnglishTitleFromWikipediaLangLinks(const CString& pageTitle)
{
	WriteDebugLog(_T("  英語版ページへの言語間リンク取得: ") + pageTitle);

	CString langlinksUrl;
	langlinksUrl.Format(_T("https://ja.wikipedia.org/w/api.php?action=query&format=json&titles=%s&prop=langlinks&lllang=en&redirects=1"),
		UrlEncode(pageTitle));

	CStringA langlinksResponse = HttpGet(langlinksUrl, _T("LyricsFetcher/1.0"));
	if (langlinksResponse.IsEmpty()) {
		return _T("");
	}

	// 英語版ページタイトルを抽出
	int langlinkPos = langlinksResponse.Find("\"langlinks\":");
	if (langlinkPos == -1) {
		return _T("");
	}

	CStringA englishTitleUtf8 = ExtractJsonStringSimple(langlinksResponse.Mid(langlinkPos), "*");
	if (englishTitleUtf8.IsEmpty()) {
		return _T("");
	}

	CString englishPageTitle = CA2T(englishTitleUtf8, CP_UTF8);

	// クリーンアップ: "Julia Fordham" → "Julia Fordham"（変化なし）
	// アーティストページの場合はそのまま返すのではなく、再度処理が必要だが、
	// ここでは簡易的に括弧を除去して返す
	int parenPos = englishPageTitle.Find(_T(" ("));
	if (parenPos != -1) {
		englishPageTitle = englishPageTitle.Left(parenPos);
	}

	WriteDebugLog(_T("  英語版ページタイトル: ") + englishPageTitle);
	return englishPageTitle;
}

// =========================================================
// ★Wikipedia APIで英語タイトル取得（アーティストページ解析方式）
// =========================================================
CString QueryEnglishTitleFromWikipedia(const CString& japaneseTitle, const CString& artistName)
{
	WriteDebugLog(_T("Wikipedia API検索: ") + japaneseTitle);

	if (artistName.IsEmpty() || IsVariousArtists(artistName)) {
		WriteDebugLog(_T("  アーティスト名なし：Wikipedia検索スキップ"));
		return _T("");
	}

	WriteDebugLog(_T("  アーティストページ検索: ") + artistName);

	// Step 1: アーティスト名でWikipediaページを検索
	CString searchUrl;
	searchUrl.Format(_T("https://ja.wikipedia.org/w/api.php?action=query&format=json&list=search&srsearch=%s&srlimit=1"),
		UrlEncode(artistName));

	CStringA searchResponse = HttpGet(searchUrl, _T("LyricsFetcher/1.0"));
	if (searchResponse.IsEmpty()) {
		WriteDebugLog(_T("  Wikipedia検索レスポンスなし"));
		return _T("");
	}

	// アーティストページタイトルを取得
	int searchPos = searchResponse.Find("\"search\":");
	if (searchPos == -1) {
		WriteDebugLog(_T("  アーティストページ検索結果なし"));
		return _T("");
	}

	CStringA artistPageTitleUtf8 = ExtractJsonStringSimple(searchResponse.Mid(searchPos), "title");
	if (artistPageTitleUtf8.IsEmpty()) {
		WriteDebugLog(_T("  アーティストページタイトル取得失敗"));
		return _T("");
	}

	CString artistPageTitle = CA2T(artistPageTitleUtf8, CP_UTF8);
	WriteDebugLog(_T("  アーティストページ発見: ") + artistPageTitle);

	// Step 2: ページの本文を取得
	CString extractUrl;
	extractUrl.Format(_T("https://ja.wikipedia.org/w/api.php?action=query&format=json&titles=%s&prop=extracts&explaintext=1&exsectionformat=plain"),
		UrlEncode(artistPageTitle));

	CStringA extractResponse = HttpGet(extractUrl, _T("LyricsFetcher/1.0"));
	if (extractResponse.IsEmpty()) {
		WriteDebugLog(_T("  ページ本文取得失敗"));
		return _T("");
	}

	// 本文テキストを抽出
	int extractPos = extractResponse.Find("\"extract\":");
	if (extractPos == -1) {
		WriteDebugLog(_T("  本文なし"));
		return _T("");
	}

	CStringA extractTextUtf8 = ExtractJsonStringSimple(extractResponse.Mid(extractPos), "extract");
	if (extractTextUtf8.IsEmpty()) {
		WriteDebugLog(_T("  本文抽出失敗"));
		return _T("");
	}

	CString pageText = CA2T(extractTextUtf8, CP_UTF8);

	// ★ページテキストの正規化（改行・余分なスペースを除去）
	CString normalizedPageText;
	bool lastWasSpace = false;

	for (int i = 0; i < pageText.GetLength(); i++) {
		wchar_t ch = pageText[i];

		// 改行文字をスペースに変換
		if (ch == L'\n' || ch == L'\r' || ch == L'\t') {
			if (!lastWasSpace) {
				normalizedPageText += L' ';
				lastWasSpace = true;
			}
			continue;
		}

		// 連続するスペースを1つに
		if (ch == L' ' || ch == L'　') {
			if (!lastWasSpace) {
				normalizedPageText += L' ';
				lastWasSpace = true;
			}
			continue;
		}

		normalizedPageText += ch;
		lastWasSpace = false;
	}

	WriteDebugLog(_T("  ページテキスト正規化完了（") +
		CString(std::to_string(pageText.GetLength()).c_str()) + _T("→") +
		CString(std::to_string(normalizedPageText.GetLength()).c_str()) + _T("文字）"));

	// ★ページテキスト全体からルビ（振り仮名）を除去
	CString cleanPageText;
	bool inParen = false;
	bool maybeRuby = false;

	for (int i = 0; i < normalizedPageText.GetLength(); i++) {
		wchar_t ch = normalizedPageText[i];

		if (ch == L'(' || ch == L'（') {
			// 次の文字がひらがなならルビの可能性
			if (i + 1 < normalizedPageText.GetLength()) {
				wchar_t nextCh = normalizedPageText[i + 1];
				if (nextCh >= 0x3040 && nextCh <= 0x309F) {  // ひらがな
					maybeRuby = true;
					inParen = true;
					continue;  // '(' 自体も除去
				}
			}
			inParen = true;
		}
		else if (ch == L')' || ch == L'）') {
			if (maybeRuby) {
				maybeRuby = false;
				inParen = false;
				continue;  // ')' 自体も除去
			}
			inParen = false;
		}

		if (!inParen || !maybeRuby) {
			cleanPageText += ch;
		}
	}

	WriteDebugLog(_T("  ページテキストからルビ除去完了（") +
		CString(std::to_string(normalizedPageText.GetLength() - cleanPageText.GetLength()).c_str()) +
		_T("文字削除）"));

	// ★引用符を除去（検索精度向上）
	CString finalPageText = cleanPageText;
	finalPageText.Replace(L"『", L"");
	finalPageText.Replace(L"』", L"");
	finalPageText.Replace(L"「", L"");
	finalPageText.Replace(L"」", L"");
	finalPageText.Replace(L"'", L"");
	finalPageText.Replace(L"'", L"");
	finalPageText.Replace(L"\"", L"");
	finalPageText.Replace(L"\"", L"");

	WriteDebugLog(_T("  引用符除去完了（") +
		CString(std::to_string(cleanPageText.GetLength() - finalPageText.GetLength()).c_str()) +
		_T("文字削除）"));

	// Step 3: ページ内で日本語タイトルを検索
	// ルビ（振り仮名）を除去した検索用タイトルを作成
	CString searchTitle = japaneseTitle;

	// ()内のルビを除去: 「微笑 (ほほえみ)にふれて」→「微笑にふれて」
	CString cleanedTitle;
	inParen = false;
	for (int i = 0; i < searchTitle.GetLength(); i++) {
		wchar_t ch = searchTitle[i];
		if (ch == L'(' || ch == L'（') {
			inParen = true;
		}
		else if (ch == L')' || ch == L'）') {
			inParen = false;
		}
		else if (!inParen) {
			cleanedTitle += ch;
		}
	}

	cleanedTitle.TrimLeft();
	cleanedTitle.TrimRight();

	WriteDebugLog(_T("  検索タイトル: ") + cleanedTitle);

	// ページ内で複数箇所を検索（最大3箇所）
	std::vector<CString> contexts;
	searchPos = 0;

	for (int attempt = 0; attempt < 5; attempt++) {
		int titlePos = finalPageText.Find(cleanedTitle, searchPos);
		if (titlePos == -1) break;

		// タイトル前後のテキストを抽出
		// ★重要：タイトルの**後ろ**を中心に抽出（前は最小限）
		int contextStart = max(0, titlePos - 20);  // 前は20文字のみ
		int contextEnd = min(finalPageText.GetLength(), titlePos + cleanedTitle.GetLength() + 200);
		CString context = finalPageText.Mid(contextStart, contextEnd - contextStart);

		// このコンテキストにタイトルが含まれているか確認
		if (context.Find(cleanedTitle) != -1) {
			contexts.push_back(context);
			// タイトル位置（コンテキスト内）
			int titlePosInContext = titlePos - contextStart;
			WriteDebugLog(_T("  候補") + CString(std::to_string(attempt + 1).c_str()) +
				_T(": ...") + context.Mid(max(0, titlePosInContext - 10), 60) + _T("..."));
		}

		searchPos = titlePos + cleanedTitle.GetLength();
	}

	if (contexts.empty()) {
		WriteDebugLog(_T("  日本語タイトルが見つからない→英語タイトルパターンを全体検索"));

		// フォールバック: ページ全体から「 - "英語タイトル"」パターンを探す
		// 例: ※日本盤は「微笑にふれて」 - "Porcelain"をリード・トラックとして発売

		int searchPos = 0;
		while (searchPos < finalPageText.GetLength()) {
			// " - " パターンを探す（引用符は既に除去済み）
			int dashPos = finalPageText.Find(_T(" - "), searchPos);
			if (dashPos == -1) break;

			// ダッシュの後のテキストを取得（次のスペースまたは記号まで）
			int afterDash = dashPos + 3;
			CString candidate;

			for (int i = afterDash; i < finalPageText.GetLength(); i++) {
				wchar_t ch = finalPageText[i];
				if (ch == L' ' || ch == L'(' || ch == L'（' || ch == L'\n' ||
					ch == L'。' || ch == L'、' || ch == L'※' || ch == L'を' ||
					ch == L'の' || ch == L'に') {
					break;
				}
				candidate += ch;
			}

			candidate.TrimLeft();
			candidate.TrimRight();

			// ASCII文字が多いか確認
			int asciiCount = 0;
			for (int i = 0; i < candidate.GetLength(); i++) {
				if (candidate[i] <= 0x7F) asciiCount++;
			}

			// アルファベットが含まれているか確認
			bool hasAlpha = false;
			for (int i = 0; i < candidate.GetLength(); i++) {
				if ((candidate[i] >= L'A' && candidate[i] <= L'Z') ||
					(candidate[i] >= L'a' && candidate[i] <= L'z')) {
					hasAlpha = true;
					break;
				}
			}

			if (candidate.GetLength() >= 3 && asciiCount > candidate.GetLength() / 2 && hasAlpha) {
				// この前後のコンテキストに日本語タイトルが含まれているか確認
				int contextStart = max(0, dashPos - 100);
				int contextEnd = min(finalPageText.GetLength(), dashPos + 100);
				CString context = finalPageText.Mid(contextStart, contextEnd - contextStart);

				if (context.Find(cleanedTitle) != -1 || context.Find(japaneseTitle) != -1) {
					WriteDebugLog(_T("  全体検索で発見: ") + candidate);
					WriteDebugLog(_T("★Wikipedia英語タイトル取得: ") + japaneseTitle + _T(" -> ") + candidate);
					return candidate;
				}
			}

			searchPos = dashPos + 3;
		}

		WriteDebugLog(_T("  全体検索でも見つからず→英語版ページを試行"));

		// さらにフォールバック: 英語版ページを取得
		CString englishTitle = QueryEnglishTitleFromWikipediaLangLinks(artistPageTitle);
		if (!englishTitle.IsEmpty()) {
			return englishTitle;
		}

		WriteDebugLog(_T("  ページ内に曲名なし"));
		return _T("");
	}

	// Step 4-5: 各コンテキストから英語タイトルを抽出
	for (size_t ctxIdx = 0; ctxIdx < contexts.size(); ctxIdx++) {
		CString context = contexts[ctxIdx];

		WriteDebugLog(_T("  コンテキスト") + CString(std::to_string(ctxIdx + 1).c_str()) +
			_T("を解析中..."));

		// ★タイトルの位置を特定
		int titlePosInContext = context.Find(cleanedTitle);
		if (titlePosInContext == -1) {
			WriteDebugLog(_T("    タイトル位置不明、スキップ"));
			continue;
		}

		// ★タイトルの直後から検索開始
		int searchStart = titlePosInContext + cleanedTitle.GetLength();
		CString afterTitle = context.Mid(searchStart);

		WriteDebugLog(_T("    タイトル後: ") + afterTitle.Left(50) + _T("..."));

		// 最初の " - " を探す
		int firstDash = afterTitle.Find(_T(" - "));
		if (firstDash == -1) {
			WriteDebugLog(_T("    ダッシュなし、スキップ"));
			continue;
		}

		// ダッシュの後のテキストを取得
		CString afterDash = afterTitle.Mid(firstDash + 3);
		afterDash.TrimLeft();

		// 次の区切り文字までを英語タイトル候補とする
		CString candidate;
		for (int i = 0; i < afterDash.GetLength(); i++) {
			wchar_t ch = afterDash[i];
			// 区切り文字で停止
			if (ch == L' ' && i > 0 &&
				(afterDash[i - 1] == L'-' || (i + 1 < afterDash.GetLength() && afterDash[i + 1] == L'-'))) {
				// " - " パターンなので次のセグメントへ
				break;
			}
			if (ch == L'(' || ch == L'（' || ch == L'。' || ch == L'、' ||
				ch == L'※' || ch == L'\n') {
				break;
			}
			candidate += ch;
		}

		candidate.TrimRight();

		WriteDebugLog(_T("    候補: ") + candidate);

		// 日本語が含まれているか確認
		bool hasJapanese = false;
		for (int j = 0; j < candidate.GetLength(); j++) {
			wchar_t ch = candidate[j];
			if ((ch >= 0x3040 && ch <= 0x309F) ||  // ひらがな
				(ch >= 0x30A0 && ch <= 0x30FF) ||  // カタカナ
				(ch >= 0x4E00 && ch <= 0x9FFF)) {  // 漢字
				hasJapanese = true;
				break;
			}
		}

		if (hasJapanese) {
			WriteDebugLog(_T("      → 日本語あり、スキップ"));
			continue;
		}

		// ASCII文字が50%以上含まれているか確認
		int asciiCount = 0;
		for (int j = 0; j < candidate.GetLength(); j++) {
			if (candidate[j] <= 0x7F) asciiCount++;
		}

		if (candidate.GetLength() >= 3 && asciiCount > candidate.GetLength() / 2) {
			// アルファベットが含まれているか確認
			bool hasAlpha = false;
			for (int j = 0; j < candidate.GetLength(); j++) {
				if ((candidate[j] >= L'A' && candidate[j] <= L'Z') ||
					(candidate[j] >= L'a' && candidate[j] <= L'z')) {
					hasAlpha = true;
					break;
				}
			}

			if (hasAlpha) {
				// 最終クリーンアップ
				candidate.TrimLeft();
				candidate.TrimRight();

				WriteDebugLog(_T("★Wikipedia英語タイトル取得: ") + japaneseTitle + _T(" -> ") + candidate);
				return candidate;
			}
		}

		WriteDebugLog(_T("      → 条件不適合"));
	}

	WriteDebugLog(_T("  全コンテキストで英語タイトル抽出失敗"));
	return _T("");
}

// =========================================================
// ★MusicBrainz APIで英語タイトル取得（改良版・デバッグ強化）
// =========================================================
CString QueryEnglishTitleFromMusicBrainz(const CString& japaneseTitle, const CString& artistName)
{
	WriteDebugLog(_T("MusicBrainz Recording検索: ") + japaneseTitle);

	// レート制限対応（1秒待ち）
	static DWORD lastCallTime = 0;
	DWORD currentTime = GetTickCount();
	if (currentTime - lastCallTime < 1000) {
		Sleep(1000 - (currentTime - lastCallTime));
	}
	lastCallTime = GetTickCount();

	// Recording検索
	CString query = japaneseTitle;
	if (!artistName.IsEmpty() && !IsVariousArtists(artistName)) {
		// アーティスト名を英語化
		CString artistForQuery = artistName;
		CString englishArtist = KatakanaToEnglish(artistName, false); // 高速モード
		if (!englishArtist.IsEmpty() && englishArtist != artistName) {
			artistForQuery = englishArtist;
		}
		query += _T(" AND artist:") + artistForQuery;
	}

	WriteDebugLog(_T("  MusicBrainzクエリ: ") + query);

	CString url;
	url.Format(_T("https://musicbrainz.org/ws/2/recording/?query=%s&fmt=json&limit=5"),
		UrlEncode(query));

	CStringA response = HttpGet(url, MUSICBRAINZ_USER_AGENT);
	if (response.IsEmpty()) {
		WriteDebugLog(_T("  MusicBrainz APIレスポンスなし（タイムアウトまたはネットワークエラー）"));
		return _T("");
	}

	// レスポンスサイズをログ
	WriteDebugLog(_T("  レスポンスサイズ: ") + CString(std::to_string(response.GetLength()).c_str()) + _T(" bytes"));

	// JSONパース：複数結果から最適なものを選択
	int recordingsPos = response.Find("\"recordings\":");
	if (recordingsPos == -1) {
		WriteDebugLog(_T("  recordings フィールドなし（検索結果0件）"));
		return _T("");
	}

	int arrayStart = response.Find("[", recordingsPos);
	if (arrayStart == -1) {
		WriteDebugLog(_T("  recordings 配列が空"));
		return _T("");
	}

	// 空配列チェック
	int firstChar = arrayStart + 1;
	while (firstChar < response.GetLength() && (response[firstChar] == ' ' || response[firstChar] == '\n' || response[firstChar] == '\r')) {
		firstChar++;
	}
	if (firstChar < response.GetLength() && response[firstChar] == ']') {
		WriteDebugLog(_T("  recordings 配列が空（検索結果0件）"));
		return _T("");
	}

	int currentPos = arrayStart + 1;
	CString bestEnglishTitle = _T("");
	int bestScore = 0; // スコア：ASCII率 + 類似度
	int candidateCount = 0;

	// 最大5件チェック
	for (int i = 0; i < 5; i++) {
		int objStart = response.Find("{", currentPos);
		if (objStart == -1) break;

		// オブジェクトの終端を探す
		const char* pResp = response.GetString();
		int len = response.GetLength();
		int depth = 0;
		int objEnd = -1;
		bool inQuote = false;

		for (int k = objStart; k < len; k++) {
			if (pResp[k] == '\"' && (k == 0 || pResp[k - 1] != '\\')) {
				inQuote = !inQuote;
				continue;
			}
			if (inQuote) continue;
			if (pResp[k] == '{') depth++;
			else if (pResp[k] == '}') {
				depth--;
				if (depth == 0) {
					objEnd = k;
					break;
				}
			}
		}

		if (objEnd == -1) break;
		currentPos = objEnd + 1;

		// title フィールド抽出
		CStringA resultObj = response.Mid(objStart, objEnd - objStart + 1);
		CStringA titleUtf8 = ExtractJsonStringSimple(resultObj, "title");
		if (titleUtf8.IsEmpty()) continue;

		CString title = CA2T(titleUtf8, CP_UTF8);
		candidateCount++;

		// ASCII文字の割合を計算
		int asciiCount = 0;
		int totalCount = title.GetLength();
		for (int j = 0; j < totalCount; j++) {
			if (title[j] <= 0x7F) asciiCount++;
		}

		int asciiPercent = (asciiCount * 100) / (totalCount > 0 ? totalCount : 1);

		// 類似度チェック
		int similarity = CalculateTitleSimilarity(japaneseTitle, title);

		// スコア計算：ASCII率 + 類似度
		int score = asciiPercent + similarity;

		WriteDebugLog(_T("  MusicBrainz候補") + CString(std::to_string(candidateCount).c_str()) +
			_T(": ") + title +
			_T(" (ASCII率: ") + CString(std::to_string(asciiPercent).c_str()) +
			_T("%, 類似度: ") + CString(std::to_string(similarity).c_str()) +
			_T("%, スコア: ") + CString(std::to_string(score).c_str()) + _T(")"));

		// ASCII率が50%以上の場合のみ候補とする
		if (asciiPercent < 50) {
			WriteDebugLog(_T("    → 却下（ASCII率不足）"));
			continue;
		}

		// より高スコアなら採用
		if (score > bestScore && title != japaneseTitle) {
			bestScore = score;
			bestEnglishTitle = title;
		}
	}

	WriteDebugLog(_T("  検索結果: ") + CString(std::to_string(candidateCount).c_str()) + _T("件"));

	if (!bestEnglishTitle.IsEmpty()) {
		WriteDebugLog(_T("★MusicBrainz英語タイトル取得: ") + japaneseTitle + _T(" -> ") + bestEnglishTitle);
		return bestEnglishTitle;
	}

	WriteDebugLog(_T("  MusicBrainz: 適切な英語タイトル見つからず"));
	return _T("");
}

// =========================================================
// タイトルバリエーション生成（自動英語タイトル取得対応）
// =========================================================
std::vector<CString> GenerateTitleVariations(const CString& originalTitle, const CString& artistName = _T(""))
{
	std::vector<CString> variations;
	variations.push_back(originalTitle);

	// ★日英タイトル変換（キャッシュ優先→必要時のみMusicBrainz）
	CString englishTitle = GetEnglishTitle(originalTitle, artistName);
	if (!englishTitle.IsEmpty()) {
		variations.push_back(englishTitle);
	}

	CString cleaned = originalTitle;

	// 特殊記号削除版
	CString symbols = _T("♪★☆○●◎◇◆□■△▲▽▼※〒→←↑↓！!？?");
	for (int i = 0; i < symbols.GetLength(); i++) {
		cleaned.Replace(symbols[i], _T(' '));
	}
	cleaned.TrimLeft(); cleaned.TrimRight();
	if (!cleaned.IsEmpty() && cleaned != originalTitle) {
		variations.push_back(cleaned);
	}

	// Part X 除去
	CString withoutPart = originalTitle;
	int partPos = -1;
	if ((partPos = withoutPart.Find(_T("Part"))) != -1 ||
		(partPos = withoutPart.Find(_T("part"))) != -1 ||
		(partPos = withoutPart.Find(_T("PART"))) != -1) {
		withoutPart = withoutPart.Left(partPos);
		withoutPart.TrimRight();
		if (!withoutPart.IsEmpty() && withoutPart != originalTitle) {
			variations.push_back(withoutPart);
		}
	}

	// 〜以降削除
	int tildePos = withoutPart.Find(_T("〜"));
	if (tildePos == -1) tildePos = withoutPart.Find(_T("~"));
	if (tildePos != -1) {
		CString beforeTilde = withoutPart.Left(tildePos);
		beforeTilde.TrimRight();
		if (!beforeTilde.IsEmpty() && beforeTilde != originalTitle) {
			variations.push_back(beforeTilde);
		}
	}

	// ★日本語（ひらがな・漢字）チェック
	bool hasKatakana = false;
	bool hasHiraganaOrKanji = false;

	for (int i = 0; i < originalTitle.GetLength(); i++) {
		wchar_t ch = originalTitle[i];
		if (ch >= 0x30A0 && ch <= 0x30FF) {
			hasKatakana = true;
		}
		if ((ch >= 0x3040 && ch <= 0x309F) ||  // ひらがな
			(ch >= 0x4E00 && ch <= 0x9FFF)) {  // 漢字
			hasHiraganaOrKanji = true;
		}
	}

	// カタカナのみの場合は英単語化（「ロック・アンド・キー」→「lock and key」）
	// ひらがな・漢字が含まれる場合は変換しない（「微笑にふれて」など）
	if (hasKatakana && !hasHiraganaOrKanji) {
		// ★英単語化を試みる（辞書ベース）
		CString englishWords = KatakanaToEnglishWords(originalTitle);
		if (!englishWords.IsEmpty() && englishWords != originalTitle) {
			variations.push_back(englishWords);
			WriteDebugLog(_T("  カタカナ→英単語: ") + englishWords);
		}
	}
	else if (hasHiraganaOrKanji) {
		WriteDebugLog(_T("  日本語タイトル検出: カタカナ変換スキップ"));
	}

	// 重複削除
	std::sort(variations.begin(), variations.end());
	variations.erase(std::unique(variations.begin(), variations.end()), variations.end());

	return variations;
}

// =========================================================
// ★V.A. (Various Artists) 判定
// =========================================================
bool IsVariousArtists(const CString& artistName)
{
	CString normalized = artistName;
	normalized.MakeUpper();
	normalized.Replace(L" ", L"");
	normalized.Replace(L"　", L"");

	// V.A.パターン
	if (normalized == _T("V.A.") || normalized == _T("VA") || normalized == _T("V.A")) return true;

	// VARIOUS ARTISTSパターン
	if (normalized.Find(_T("VARIOUSARTIST")) != -1) return true;

	// オムニバス
	if (normalized.Find(_T("OMNIBUS")) != -1) return true;

	// コンピレーション
	if (normalized.Find(_T("COMPILATION")) != -1) return true;

	return false;
}

// =========================================================
// アーティスト名バリエーション生成（英語化対応・高速版・V.A.対応）
// =========================================================
std::vector<CString> GenerateArtistVariations(const CString& originalArtist, bool useMusicBrainz = false)
{
	std::vector<CString> variations;
	if (originalArtist.IsEmpty()) return variations;

	// ★V.A.の場合は空文字列のみ返す（アーティスト名を無視）
	if (IsVariousArtists(originalArtist)) {
		WriteDebugLog(_T("★V.A.検出: アーティスト名なしで検索"));
		variations.push_back(_T(""));
		return variations;
	}

	variations.push_back(originalArtist);

	// カタカナ→英語変換（高速モード：MusicBrainz APIは使わない）
	CString englishName = KatakanaToEnglish(originalArtist, useMusicBrainz);
	if (!englishName.IsEmpty() && englishName != originalArtist) {
		variations.push_back(englishName);
	}

	// ローマ字版も追加（英語化と異なる場合）
	CString romajiName = KatakanaToRomaji(originalArtist);
	if (!romajiName.IsEmpty() && romajiName != originalArtist && romajiName != englishName) {
		variations.push_back(romajiName);
	}

	// 重複削除
	std::sort(variations.begin(), variations.end());
	variations.erase(std::unique(variations.begin(), variations.end()), variations.end());

	return variations;
}

// =========================================================
// ★LRCタイムスタンプ補正（LRCLIB用）
// =========================================================
CStringA CorrectLRCLIBTimestamp(const CStringA& lrcData)
{
	// LRCLIBの歌詞は、最初の歌詞行のタイムスタンプをチェックして
	// イントロ時間が考慮されているか判定
	std::string s = (LPCSTR)lrcData;
	std::stringstream ss(s);
	std::string line;

	int firstLyricTime = -1;
	while (std::getline(ss, line)) {
		if (line.empty() || line[0] != '[') continue;

		size_t bracketEnd = line.find("]");
		if (bracketEnd == std::string::npos) continue;

		std::string timeStr = line.substr(1, bracketEnd - 1);
		std::string content = line.substr(bracketEnd + 1);

		// 空行やメタデータをスキップ
		if (content.empty() || content.find("作") != std::string::npos) continue;

		// タイムスタンプをミリ秒に変換
		int min = atoi(timeStr.substr(0, 2).c_str());
		int sec = atoi(timeStr.substr(3, 2).c_str());
		int ms = 0;
		if (timeStr.length() > 6) {
			ms = atoi(timeStr.substr(6, 2).c_str()) * 10;
		}
		firstLyricTime = (min * 60 + sec) * 1000 + ms;
		break;
	}

	// 最初の歌詞が5秒以内に始まる場合、LRCLIBのタイムスタンプは正常と判断
	if (firstLyricTime >= 0 && firstLyricTime < 5000) {
		WriteDebugLog(_T("★LRCLIB: タイムスタンプ正常（補正不要）"));
		return lrcData;
	}

	// それ以外の場合は、そのまま返す（補正は困難）
	WriteDebugLog(_T("★LRCLIB: タイムスタンプ要確認"));
	return lrcData;
}

// =========================================================
// LRCデータ整形
// =========================================================
CStringA RefineLrcData(CStringA rawLrc, const CString& trackName, const CString& artistName, bool isFromLRCLIB = false)
{
	std::string s = (LPCSTR)rawLrc;
	CString tt = CA2W(rawLrc, CP_UTF8);
	size_t start_pos = 0;
	while ((start_pos = s.find("\\n", start_pos)) != std::string::npos) {
		s.replace(start_pos, 2, "\n");
		start_pos += 1;
	}

	std::stringstream ss(s);
	std::string line;
	std::string cleanResult;

	const wchar_t* ignoreKeywords[] = {
		L"作?", L"作詞", L"作曲", L"編曲", L"?曲",
		L"収録", L"プロデュース", L"演唱", L"提供", L"作成"
	};

	CStringW wTrack = CT2W(trackName); wTrack.Replace(L" ", L"");
	CStringW wArtist = CT2W(artistName); wArtist.Replace(L" ", L"");
	CStringA utf8Track = CW2A(wTrack, CP_UTF8);
	CStringA utf8Artist = CW2A(wArtist, CP_UTF8);

	while (std::getline(ss, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) continue;

		int timeMs = -1;
		size_t bracketEnd = line.find("]");
		if (bracketEnd != std::string::npos && line.find("[") == 0) {
			int min = atoi(line.substr(1, 2).c_str());
			int sec = atoi(line.substr(4, 2).c_str());
			timeMs = (min * 60 + sec) * 1000;
			if (timeMs == 0) continue;
		}

		bool isGarbage = false;

		if (timeMs == -1) isGarbage = true;

		if (!isGarbage) {
			for (const wchar_t* kw : ignoreKeywords) {
				CStringA utf8Kw = CW2A(kw, CP_UTF8);
				if (line.find((LPCSTR)utf8Kw) != std::string::npos) {
					isGarbage = true;
					break;
				}
			}
		}

		if (!isGarbage && timeMs < 1) {
			if (!utf8Track.IsEmpty()) {
				std::string temp = line.substr(bracketEnd + 1);
				std::string::iterator end_p = std::remove(temp.begin(), temp.end(), ' ');
				temp.erase(end_p, temp.end());
				if (temp.find((LPCSTR)utf8Track) != std::string::npos) {
					if (temp.length() < utf8Track.GetLength() + 10) isGarbage = true;
				}
			}
			if (!utf8Artist.IsEmpty()) {
				std::string temp = line.substr(bracketEnd + 1);
				if (temp.find((LPCSTR)utf8Artist) != std::string::npos) isGarbage = true;
			}
		}

		if (!isGarbage && bracketEnd != std::string::npos) {
			std::string content = line.substr(bracketEnd + 1);
			if (content.find("\xE8\x9C\x9C") != std::string::npos) isGarbage = true;

			std::string::iterator end_p = std::remove(content.begin(), content.end(), ' ');
			content.erase(end_p, content.end());
			if (content.empty()) isGarbage = true;
		}

		if (isGarbage) continue;

		size_t dotPos = line.find(".", 0);
		if (dotPos != std::string::npos && dotPos < bracketEnd) {
			if ((bracketEnd - dotPos) == 4) line.erase(bracketEnd - 1, 1);
		}

		CStringA convertedLine = ConvertSimplifiedToJapanese(line.c_str());
		cleanResult += std::string(convertedLine) + "\n";
	}

	return CStringA(cleanResult.c_str());
}

// =========================================================
// NetEase歌詞取得（複数バリエーション対応・早期終了版）
// =========================================================
CStringA GetLyricsFromNetEase_Multi(const std::vector<CString>& titleVariations,
	const std::vector<CString>& artistVariations,
	int targetDuration,
	std::atomic<bool>& shouldStop)
{
	for (const auto& title : titleVariations) {
		//if (shouldStop.load()) return ""; // ★他のスレッドが見つけたら中断

		for (const auto& artist : artistVariations) {
			//if (shouldStop.load()) return ""; // ★他のスレッドが見つけたら中断

			WriteDebugLog(_T("NetEase検索: ") + title + _T(" / ") + artist);

			CString cleanTrackName = title;
			CString invalidChars = _T("♪★☆○●◎◇◆□■△▲▽▼※〒→←↑↓");
			for (int i = 0; i < invalidChars.GetLength(); i++)
				cleanTrackName.Replace(invalidChars[i], _T(' '));

			CString searchQuery = cleanTrackName;
			if (!artist.IsEmpty()) searchQuery += _T(" ") + artist;

			CString headers = _T("Referer: http://music.163.com/\r\n");
			CString searchUrl;
			searchUrl.Format(_T("http://music.163.com/api/cloudsearch/pc?s=%s&type=1&offset=0&limit=10"),
				UrlEncode(searchQuery));

			CStringA searchResponse = HttpGet(searchUrl, _T(""), headers);
			if (searchResponse.IsEmpty()) continue;

			int songsPos = searchResponse.Find("\"songs\":");
			if (songsPos == -1) continue;
			int arrayStart = searchResponse.Find("[", songsPos);
			if (arrayStart == -1) continue;
			int currentPos = arrayStart + 1;

			CStringA bestId = "";
			int minDiff = 99999999;
			CStringA bestSongName = ""; // ★曲名も保存

			for (int i = 0; i < 10; i++) {
				//if (shouldStop.load()) return ""; // ★他のスレッドが見つけたら中断

				int blockStart = searchResponse.Find("{", currentPos);
				if (blockStart == -1) break;

				const char* pResp = searchResponse.GetString();
				int len = searchResponse.GetLength();
				int depth = 0;
				int blockEnd = -1;
				bool inQuote = false;
				for (int k = blockStart; k < len; k++) {
					if (pResp[k] == '\"' && (k == 0 || pResp[k - 1] != '\\')) {
						inQuote = !inQuote; continue;
					}
					if (inQuote) continue;
					if (pResp[k] == '{') depth++;
					else if (pResp[k] == '}') {
						depth--;
						if (depth == 0) { blockEnd = k; break; }
					}
				}
				if (blockEnd == -1) break;
				currentPos = blockEnd + 1;

				CStringA songJsonBlock = searchResponse.Mid(blockStart, blockEnd - blockStart + 1);
				CStringA songId = ExtractValueFromBlock(songJsonBlock, "id", false);
				CStringA dtStr = ExtractValueFromBlock(songJsonBlock, "dt", false);
				int duration = atoi(dtStr);

				// ★曲名も取得
				CStringA songNameRaw = ExtractValueFromBlock(songJsonBlock, "name", true);
				CStringA songNameDecoded = UnescapeJsonUnicode(songNameRaw);

				if (songId.IsEmpty() || songId == "0") continue;

				if (targetDuration > 0 && duration > 0) {
					int diff = abs(duration - targetDuration);
					if (diff < 5000 && diff < minDiff) {
						minDiff = diff;
						bestId = songId;
						bestSongName = songNameDecoded; // ★曲名も保存
					}
				}
				else {
					if (bestId.IsEmpty()) {
						bestId = songId;
						bestSongName = songNameDecoded; // ★曲名も保存
					}
				}
			}

			if (!bestId.IsEmpty()) {
				CString lyricsUrl;
				lyricsUrl.Format(_T("http://music.163.com/api/song/lyric?os=pc&id=%s&lv=-1&kv=-1&tv=-1"),
					(LPCTSTR)CA2W(bestId));
				CStringA lyricsResponse = HttpGet(lyricsUrl, _T(""), headers);
				CStringA finalLyrics = ExtractJsonStringSimple(lyricsResponse, "lyric");

				int similarity = 0;
				int findd = false;
				if (!finalLyrics.IsEmpty() && finalLyrics.Find("[00:") != -1) {
					// ★NetEaseの検索結果を検証（ログのみ）
					if (!bestSongName.IsEmpty()) {
						CString songName = CA2T(bestSongName, CP_UTF8);
						similarity = CalculateTitleSimilarity(title, songName);

						WriteDebugLog(_T("  NetEase曲名: ") + songName +
							_T(" (類似度: ") + CString(std::to_string(similarity).c_str()) + _T("%)"));

						// 類似度が低すぎる場合は警告（ただし採用はする）
						if (similarity < 20) {
							WriteDebugLog(_T("  ★警告: タイトル類似度が低い（") +
								CString(std::to_string(similarity).c_str()) + _T("%）"));
						}
						else {
							findd = true;
						}
					}

					WriteDebugLog(_T("★NetEase成功: ") + title + _T(" / ") + artist);
					if (findd && finalLyrics.GetLength() > 100) {
						return RefineLrcData(finalLyrics, title, artist, false); // ★NetEase由来フラグ
					}
				}
			}
		}
	}

	return "";
}

// =========================================================
// LRCLIB歌詞取得（複数バリエーション対応・早期終了版・タイトル検証強化）
// =========================================================
CStringA GetLyricsFromLRCLIB_Multi(const std::vector<CString>& titleVariations,
	const std::vector<CString>& artistVariations,
	std::atomic<bool>& shouldStop)
{
	for (const auto& title : titleVariations) {
		if (shouldStop.load()) return ""; // ★他のスレッドが見つけたら中断

		for (const auto& artist : artistVariations) {
			if (shouldStop.load()) return ""; // ★他のスレッドが見つけたら中断

			WriteDebugLog(_T("LRCLIB検索: ") + title + _T(" / ") + artist);

			CString query = title;
			if (!artist.IsEmpty()) query += _T(" ") + artist;

			CString url = _T("https://lrclib.net/api/search?q=") + UrlEncode(query);
			CStringA response = HttpGet(url);
			if (response.IsEmpty()) continue;

			// ★検索結果の配列をパース（複数結果から最適なものを選択）
			int arrayStart = response.Find("[");
			if (arrayStart == -1) continue;

			int currentPos = arrayStart + 1;
			CStringA bestLyrics = "";
			int bestSimilarity = 0;

			// 最大5件の検索結果をチェック
			for (int i = 0; i < 5; i++) {
				if (shouldStop.load()) return "";

				int objStart = response.Find("{", currentPos);
				if (objStart == -1) break;

				// オブジェクトの終端を探す
				const char* pResp = response.GetString();
				int len = response.GetLength();
				int depth = 0;
				int objEnd = -1;
				bool inQuote = false;

				for (int k = objStart; k < len; k++) {
					if (pResp[k] == '\"' && (k == 0 || pResp[k - 1] != '\\')) {
						inQuote = !inQuote;
						continue;
					}
					if (inQuote) continue;
					if (pResp[k] == '{') depth++;
					else if (pResp[k] == '}') {
						depth--;
						if (depth == 0) {
							objEnd = k;
							break;
						}
					}
				}

				if (objEnd == -1) break;
				currentPos = objEnd + 1;

				// このオブジェクトから情報を抽出
				CStringA resultObj = response.Mid(objStart, objEnd - objStart + 1);

				// trackName取得
				CStringA trackNameUtf8 = ExtractJsonStringSimple(resultObj, "trackName");
				if (trackNameUtf8.IsEmpty()) continue;

				CString trackName = CA2T(trackNameUtf8, CP_UTF8);

				// ★タイトル類似度チェック
				int similarity = CalculateTitleSimilarity(title, trackName);
				WriteDebugLog(_T("  候補: ") + trackName + _T(" (類似度: ") +
					CString(std::to_string(similarity).c_str()) + _T("%)"));

				// 類似度が60%以上かつ、これまでで最高の場合
				if (similarity >= 60 && similarity > bestSimilarity) {
					// syncedLyrics取得
					CStringA lyrics = ExtractJsonStringSimple(resultObj, "syncedLyrics");
					if (!lyrics.IsEmpty() && lyrics != "null") {
						bestLyrics = lyrics;
						bestSimilarity = similarity;

						// 完全一致なら即座に採用
						if (similarity == 100) {
							WriteDebugLog(_T("★LRCLIB完全一致: ") + trackName);
							break;
						}
					}
				}
			}

			// 最適な結果が見つかった場合
			if (!bestLyrics.IsEmpty() && bestSimilarity >= 60) {
				WriteDebugLog(_T("★LRCLIB成功（類似度") +
					CString(std::to_string(bestSimilarity).c_str()) +
					_T("%）: ") + title + _T(" / ") + artist);

				// ★LRCLIB補正を適用
				bestLyrics = CorrectLRCLIBTimestamp(bestLyrics);

				return RefineLrcData(bestLyrics, title, artist, true); // ★LRCLIB由来フラグ
			}
			else if (bestSimilarity > 0) {
				WriteDebugLog(_T("★LRCLIB類似度不足（") +
					CString(std::to_string(bestSimilarity).c_str()) +
					_T("%）: 破棄"));
			}
		}
	}

	return "";
}

// =========================================================
// 並列検索用の構造体（早期終了対応・優先順位対応）
// =========================================================
struct LyricsSearchResult {
	CStringA lyrics;
	std::mutex mtx;
	std::atomic<bool> found;
	std::atomic<bool> shouldStop; // ★スレッド停止フラグ
	std::atomic<int> source; // 0=未設定, 1=NetEase, 2=LRCLIB

	LyricsSearchResult() : found(false), shouldStop(false), source(0) {}

	void SetResult(const CStringA& result, int sourceType) {
		if (!result.IsEmpty()) {
			std::lock_guard<std::mutex> lock(mtx);

			// NetEase(1)が優先、すでにNetEaseが見つかっている場合は上書きしない
			if (source.load() == 1 && sourceType == 2) {
				WriteDebugLog(_T("★NetEase優先: LRCLIB結果を破棄"));
				return;
			}

			// 初回または、NetEaseで上書き
			if (!found.load() || sourceType == 1) {
				lyrics = result;
				found.store(true);
				source.store(sourceType);
				shouldStop.store(true); // ★他のスレッドに停止を通知

				if (sourceType == 1) {
					WriteDebugLog(_T("★NetEase結果を採用"));
				}
				else {
					WriteDebugLog(_T("★LRCLIB結果を採用（NetEaseなし）"));
				}
			}
		}
	}

	CStringA GetResult() {
		std::lock_guard<std::mutex> lock(mtx);
		return lyrics;
	}

	int GetSource() {
		return source.load();
	}
};

// =========================================================
// ★メイン関数: 並列検索版（高速化版・NetEase優先・V.A.対応）
// =========================================================
CString GetLrcFromAPI(const CString& trackName, const CString& albumName,
	const CString& artistName, int durationMs = 0)
{
	if (trackName.IsEmpty()) return _T("");

	WriteDebugLog(_T("===== 歌詞検索開始 ====="));
	WriteDebugLog(_T("曲名: ") + trackName);
	WriteDebugLog(_T("アーティスト: ") + artistName);

	// ★V.A.チェック
	if (IsVariousArtists(artistName)) {
		WriteDebugLog(_T("★コンピレーションアルバム検出"));
	}

	// バリエーション生成
	// タイトル: 自動英語タイトル取得（キャッシュ優先）
	std::vector<CString> titleVariations = GenerateTitleVariations(trackName, artistName);

	// アーティスト: 英語化は高速モード（MusicBrainz APIなし）
	std::vector<CString> artistVariations;
	if (!artistName.IsEmpty()) {
		artistVariations = GenerateArtistVariations(artistName, false); // ★高速モード
	}
	else {
		artistVariations.push_back(_T(""));
	}

	WriteDebugLog(_T("タイトルバリエーション数: ") + CString(std::to_string(titleVariations.size()).c_str()));
	WriteDebugLog(_T("アーティストバリエーション数: ") + CString(std::to_string(artistVariations.size()).c_str()));

	// 共有結果オブジェクト
	LyricsSearchResult result;

	// ★並列検索スレッド（NetEase=1, LRCLIB=2）
	std::thread neteaseThread([&]() {
		CStringA lyrics = GetLyricsFromNetEase_Multi(titleVariations, artistVariations, durationMs, result.shouldStop);
		result.SetResult(lyrics, 1); // NetEase = 1
		});

	std::thread lrclibThread([&]() {
		CStringA lyrics = GetLyricsFromLRCLIB_Multi(titleVariations, artistVariations, result.shouldStop);
		result.SetResult(lyrics, 2); // LRCLIB = 2
		});

	// 両方の終了を待つ
	neteaseThread.join();
	lrclibThread.join();

	CStringA lrcContent = result.GetResult();
	int source = result.GetSource();

	if (lrcContent.IsEmpty()) {
		WriteDebugLog(_T("★歌詞が見つかりませんでした"));
		return _T("");
	}

	if (source == 1) {
		WriteDebugLog(_T("★最終採用: NetEase"));
	}
	else if (source == 2) {
		WriteDebugLog(_T("★最終採用: LRCLIB（タイムスタンプ要確認）"));
	}

	// ファイル保存
	TCHAR tempPath[MAX_PATH];
	GetTempPath(MAX_PATH, tempPath);
	CString fileName;
	if (!artistName.IsEmpty() && !IsVariousArtists(artistName))
		fileName.Format(_T("%s - %s.lrc"), artistName, trackName);
	else
		fileName.Format(_T("%s.lrc"), trackName);

	CString invalidChars = _T("/\\:*?\"<>|");
	for (int i = 0; i < invalidChars.GetLength(); i++)
		fileName.Replace(invalidChars[i], _T('_'));

	CString lrcPath = tempPath;
	lrcPath += fileName;

	CFile file;
	if (file.Open(lrcPath, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive)) {
		unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
		file.Write(bom, 3);
		file.Write((LPCSTR)lrcContent, lrcContent.GetLength());
		file.Close();
		WriteDebugLog(_T("★保存成功: ") + lrcPath);
	}
	else {
		WriteDebugLog(_T("★保存失敗"));
		return _T("");
	}

	return lrcPath;
}

extern BOOL reset;

static bool IsFalcomGameBgmMode(int m)
{
	return (m >= 1 && m <= 21) || m == 30 ||
		m == -11 || m == -12 || m == -13 || m == -14 || m == -15;
}

static CString StripToBasename(const CString& path)
{
	CString s = path;
	int slash = s.ReverseFind(_T('\\'));
	if (slash >= 0)
		s = s.Mid(slash + 1);
	slash = s.ReverseFind(_T('/'));
	if (slash >= 0)
		s = s.Mid(slash + 1);
	return s;
}

// mode 30: 旧プレイリストは basename のみ。直近成功ディレクトリと他行のフルパスから .pac を探す。
static CString s_mode30LastPacDir;

static void RememberMode30PacDir(const CString& pacPath)
{
	const int slash = pacPath.ReverseFind(_T('\\'));
	if (slash >= 0)
		s_mode30LastPacDir = pacPath.Left(slash);
}

void RememberMode30PacPath(LPCTSTR pacOrFol)
{
	if (!pacOrFol || !*pacOrFol)
		return;
	const CString phys = PlPhysicalMediaPath(pacOrFol);
	if (!phys.IsEmpty() && (phys.Find(_T('\\')) >= 0 || phys.Find(_T('/')) >= 0))
		RememberMode30PacDir(phys);
}

static CString ResolveMode30PacPath(const CString& pacPath)
{
	if (!pacPath.IsEmpty() && PathFileExists(pacPath)) {
		RememberMode30PacDir(pacPath);
		return pacPath;
	}

	CString base = StripToBasename(pacPath);
	if (base.IsEmpty())
		return pacPath;

	if (!s_mode30LastPacDir.IsEmpty()) {
		const CString cand = s_mode30LastPacDir + _T("\\") + base;
		if (PathFileExists(cand))
			return cand;
	}

	if (pl && pl->pc) {
		for (int i = 0; i < pl->playcnt; ++i) {
			if (pl->pc[i].sub != 30 || !pl->pc[i].fol[0])
				continue;
			const CString phys = PlPhysicalMediaPath(pl->pc[i].fol);
			if (phys.IsEmpty())
				continue;
			if (StripToBasename(phys).CompareNoCase(base) != 0)
				continue;
			if (PathFileExists(phys)) {
				RememberMode30PacDir(phys);
				return phys;
			}
		}
	}
	return pacPath;
}

static CString NormalizeZweiPlaybackFol(const CString& fol, int listIndex)
{
	CString s = fol;
	const int slash = s.ReverseFind(_T('\\'));
	if (slash >= 0)
		s = s.Mid(slash + 1);
	if (s.Find(L"(wav.dat)") >= 0)
		return s;
	if (listIndex >= 0 && listIndex < 36) {
		const CString fixed = ZweiFolFromIndex(listIndex);
		if (!fixed.IsEmpty())
			return fixed;
	}
	if (s.GetLength() >= 5 && s.Left(3).CompareNoCase(_T("bgm")) == 0) {
		CString id = s.Left(5);
		id.TrimRight();
		CString out;
		out.Format(_T("%s(wav.dat)"), (LPCTSTR)id);
		return out;
	}
	return fol;
}

void COggDlg::play()
{
	// stop1/play 実行中の DoEvent 再入で形式切替が重なるとデコーダ UAF になる
	if (s_inPlay)
		return;
	s_inPlay = true;
	struct ClearInPlay { ~ClearInPlay() { s_inPlay = false; } } _clearInPlay;

	// 共有状態を触る前に旧再生を止める（Get() 後でも g_openDecoderMode で正しい形式を閉じる）
	KillTimer(1250);
	KillTimer(9000);
	// stop1 失敗でも再生は続行する。ここで return すると CWread に入らず
	// 0:00/古い loop1,2 のままになる（mode 30 で顕在化）。
	// 曲切替中は Join 上限付き(無限待ちだと通知スレッド固着で UI 永久停止)。
	InterlockedExchange(&g_interactiveTrackChange, 1);
	(void)stop1();
	InterlockedExchange(&g_interactiveTrackChange, 0);
	// CWread が thend1 を見て即 return しないよう、開始前に必ず下ろす
	thend1 = FALSE;
	thend = 0;
	thn1 = FALSE;
	stf = 0;
	if (MpPromptIsActive() || MpPromptHasBackup())
		MpPromptOnTrackChange();
	eqflg = FALSE;
	mode = modesub;

	reset = TRUE;
	stflg = FALSE;
	CheckMixerMuteOnPlayModal();
	//	if (((modesub > 0 && modesub < 22) || (modesub > -16 && modesub < -10))) {
#if 0
	if (gameon == 1) {
		if (playbase)
			::SetWindowPos(playbase->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		if (pl)
			::SetWindowPos(pl->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		if (maini)
			::SetWindowPos(maini->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(og->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}
	else {
		if (maini)
			::SetWindowPos(maini->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(og->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		if (playbase)
			::SetWindowPos(playbase->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		if (pl)
			::SetWindowPos(pl->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}
	gameon = 1;
#endif
	//	}
	muon = MUON;
	kpi_silence_bytes = 0;
	rrr = 1;
	CWaitCursor rrr;
	m_mp3jake.EnableWindow(FALSE);
	mp3file = filen;
	sflg = FALSE;
	tagname = tagfile = tagalbum = "";
	tagtrack = "";
	//	m_rund.EnableWindow(FALSE);
	m_saisai.EnableWindow(FALSE);
	WAVDALen = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM; WAVDAStartLen = OUTPUT_BUFFER_SIZE;
	si1.dwSamplesPerSec = 0; sikpi.dwSamplesPerSec = 0;
	//LOOPSTART=
	//LOOPLENGTH=
	CString s, b;
	playf = 1;
	if (pl && plw) {
		pl->Save();
	}
	{
		TCHAR tmp_savedir[1024];
		_tgetcwd(tmp_savedir, 1000);
		_tchdir(karento2);
		CFile ab;
#if _UNICODE
		if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
		if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
			ab.Write(&savedata, sizeof(save));
			ab.Close();
		}
		_tchdir(tmp_savedir);
	}
	loopcnt = 0;
	CString fl;
	wavbit_sample_Hz = 44100;
	loop3 = 0; fade1 = 0;
	playy = 0;
	ms2 = 0;
	InterlockedExchange(&g_gdiPaintPending, 0);
	cnt3 = 0;
	bufzero = 0;
	if (mi) {
		killw1 = 0;
		mi->DestroyWindow();
		PumpUntilFlagOrTimeout(killw1);
		mi = NULL;
	}
	//
	videoonly = FALSE;
	if (mode == 7) { fl = filen.Right(8); fl = fl.Left(3); }
	else { fl = filen.Right(7); fl = fl.Left(3); }
	int nn = _tstoi(fl);
	pl_no = nn;
	if (mode == 8) { pl_no = ret2; }//ysc1
	if (mode == 9) { pl_no = ret2; }//ysc2
	if (mode == 15) { pl_no = ret2; }//ysc2
	if (mode == 16) { pl_no = ret2; }//ysc2
	fade = 0;
	endflg = 0;
	// stop1 は先頭で済み。CWread 用に停止フラグは下ろしておく。
	stf = 0;
	thn1 = FALSE;
	if (mode == 14)
		filen = NormalizeZweiPlaybackFol(filen, ret2);
	// プレイリストにフルパスが入っていても chdir 先ではファイル名のみ参照する
	// （例: mode16 の wavread は filen.Left(8) で dinow_XX と照合）
	// mode 30 は chdir 無しで .pac をフルパス Open するため basename 化しない。
	if (IsFalcomGameBgmMode(mode) && mode != 30)
		filen = StripToBasename(filen);

	CWaitCursor rrr1;
	wavwait = 0; thend = 1; stitle = "";
	playf = 1;
	int ret = 0;



	if (filen.Find(L"y8_logo.ogg") != -1) {
		ret2 = 1;
	}
	if (filen.Find(L"y8_op.ogg") != -1) {
		ret2 = 2;
	}
	if (filen.Find(L"y8_end.ogg") != -1) {
		ret2 = 3;
	}
	if (filen.Find(L"yc_logo.ogg") != -1) {
		ret2 = 4;
	}
	if (m_dou.GetCheck() == 1)
		gamen(ret2);

	switch (mode) {
	case 1://ED6SC
		ret = _tchdir(savedata.ed6sc);
		ret += _chdir("bgm");
		break;
	case 2://ED6FC
		ret = _tchdir(savedata.ed6fc);
		ret += _chdir("bgm");
		break;
	case 3:
		ret = _tchdir(savedata.ysf);
		ret += _chdir("RELEASE\\MUSIC");
		break;
	case 4:
		ret = _tchdir(savedata.ys6);
		ret += _chdir("RELEASE\\MUSIC");
		break;
	case 5:
		ret = _tchdir(savedata.yso);
		ret += _chdir("RELEASE\\MUSIC");
		break;
	case 6:
		ret = _tchdir(savedata.ed6tc);
		ret += _chdir("bgm");
		break;
	case 7:
		ret = _tchdir(savedata.zweiii);
		ret += _chdir("bgm");
		break;
	case 8:
		ret = _tchdir(savedata.ysc);
		ret += _chdir("bgm\\ys1");
		break;
	case 9:
		ret = _tchdir(savedata.ysc);
		ret += _chdir("bgm\\ys2");
		break;
	case 10:
		ret = _tchdir(savedata.xa);
		ret += _chdir("data\\bgm");
		break;
	case 11:
		ret = _tchdir(savedata.ys12);
		if (_chdir("wave\\wave_44") == -1) {
			if (_chdir("wave\\wave_22") == -1) { ret = -1; break; }
			wavbit_sample_Hz = 22050;
		}
		else wavbit_sample_Hz = 44100;
		{ CFile f; if (f.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 12:
		ret = _tchdir(savedata.ys122);
		if (_chdir("wave\\wave_44") == -1) {
			if (_chdir("wave\\wave_22") == -1) { ret = -1; break; }
			wavbit_sample_Hz = 22050;
		}
		else wavbit_sample_Hz = 44100;
		{ CFile f; if (f.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 13:
		ret = _tchdir(savedata.sor);
		if (_chdir("WAVE\\WAVE44") == -1) {
			if (_chdir("WAVE\\WAVE22") == -1) { ret = -1; break; }
			wavbit_sample_Hz = 22050;
		}
		else wavbit_sample_Hz = 44100;
		{ CFile f; if (f.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 14:
		ret = _tchdir(savedata.zwei);
		{ CFile f; if (f.Open(_T("wav.dat"), CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 15:
		ret = _tchdir(savedata.gurumin);
		ret += _chdir("bgm");
		break;
	case 16:
		ret = _tchdir(savedata.dino);
		{ CFile f; if (f.Open(_T("bgm.arc"), CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 17:
		ret = _tchdir(savedata.br4);
		wavbit_sample_Hz = 22050;
		ret += _chdir("wave");
		break;
	case 18:
		ret = _tchdir(savedata.ed3);
		ret += _chdir("wave");
		break;
	case 19:
		ret = _tchdir(savedata.ed4);
		if (_chdir("WAVEDV") == -1) {
			if (_chdir("WAVE") == -1) { ret = -1; break; }
			filen += ".wav";
		}
		else filen += "DV.wav";
		break;
	case 20:
		ret = _tchdir(savedata.ed5);
		if (_chdir("WAVEDVD") == -1) {
			if (_chdir("WAVE") == -1) { ret = -1; break; }
		}
		break;
	case -11:
		ret = _tchdir(savedata.tuki);
		ret += _chdir("MUSIC");
		break;
	case -12:
		ret = _tchdir(savedata.nishi);
		ret += _chdir("bgm");
		break;
	case -13:
		ret = _tchdir(savedata.arc);
		{ CFile f; if (f.Open(_T("music.pak"), CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case -14:
		ret = _tchdir(savedata.san1);
		ret += _chdir("music");
		break;
	case -15:
		ret = _tchdir(savedata.san2);
		ret += _chdir("music");
		break;
	}

	if (mode == -14) {
		int i;
		if (ret2 == 43 || ret2 == 45 || ret2 == 46 || ret2 == 47) {
			if (ret2 == 43)i = 124; if (ret2 == 45)i = 121; if (ret2 == 46)i = 122; if (ret2 == 47)i = 1;
			_chdir("..\\Image"); CFile f; if (ret2 != 47) f.Open(_T("Stage.BKS"), CFile::modeRead | CFile::shareDenyWrite, NULL); else f.Open(_T("Stage.BKS4"), CFile::modeRead | CFile::shareDenyWrite, NULL);
			_getcwd(kare, 255);	f.Seek(0x2c, CFile::begin);
			int len, st, j;	for (j = 0; j < i; j++) { f.Read(&st, 4);	f.Read(&len, 4); }
			f.Seek(st, CFile::begin); CString a; a.Format(_T("FS%2d.bik"), ret2);
			CFile ff; if (ff.Open(a, CFile::modeRead | CFile::shareDenyWrite, NULL) == 0) {
				ff.Open(a, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL);
				char* bu; bu = (char*)malloc(len);	f.Read(bu, len);	ff.Write(bu, len);	free(bu);
			}
			f.Close();	ff.Close();
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
			}
			endflg = 0;
			return;
		}
	}
	if (mode == -15) {
		int i;
		if (ret2 == 50 || ret2 == 51 || ret2 == 49) {
			if (ret2 == 49)i = 0; if (ret2 == 51)i = 163; if (ret2 == 50)i = 1;
			_chdir("..\\Image"); CFile f; if (ret2 == 50) f.Open(_T("FS2_STAGE.BKS"), CFile::modeRead | CFile::shareDenyWrite, NULL); else f.Open(_T("FS2_STAGE_2.BKS"), CFile::modeRead | CFile::shareDenyWrite, NULL);
			_getcwd(kare, 255);	f.Seek(0x2c, CFile::begin);
			int len, st = 0, j;	if (i != 0) for (j = 0; j < i; j++) { f.Read(&st, 4);	f.Read(&len, 4); }
			f.Seek(st, CFile::begin); CString a; a.Format(_T("FS2%2d.bik"), ret2);
			CFile ff; if (i != 0) if (ff.Open(a, CFile::modeRead | CFile::shareDenyWrite, NULL) == 0) {
				ff.Open(a, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL);
				char* bu; bu = (char*)malloc(len);	f.Read(bu, len);	ff.Write(bu, len);	free(bu);
				ff.Close();
			}
			else ff.Close();
			f.Close();
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
			}
			endflg = 0;
			return;
		}
	}
	if (mode == -13) {
		if (ret2 == 0)
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
				return;
			}
	}
	if (mode == -11) {
		if (ret2 > 27)
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
				endflg = 0;
				return;
			}
	}
	if (mode == 1) {
		if (ret2 > 100)
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
				endflg = 0;
				return;
			}
		if (ret2 == 98) filen = "ED6500.ogg";
		if (ret2 == 99) filen = "ED6011.ogg";
		if (ret2 == 100) filen = "ED6012.ogg";
	}
	if (mode == 19) {
		if (ret2 == 1 || ret2 == 2)
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
				endflg = 0;
				return;
			}

	}
	if (mode == 15) {
		if (ret2 == 40) {
			filen = "bgm01.de2";
			loop1 = loop2 = 0;
		}
	}
	if (mode == 16) {
		if (ret2 == 33) {
			filen = "dinow_01(bgm.arc)";
			loop1 = loop2 = 0;
		}
	}
	if (mode == 10) {
		if (ret2 == 24) filen = "XANA300.dec";
		if (ret2 == 25) filen = "XANA000.dec";
	}
	if (mode == 7) {
		if (ret2 == 65) filen = "ZW2_002.ogg";
		if (ret2 == 66) filen = "ZW2_003.ogg";
	}
	if (mode == 8) {
		if (ret2 >= 72) {
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
				endflg = 0;
				return;
			}
		}
	}
	if (mode == 9) {
		if (ret2 >= 93) {
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
				endflg = 0;
				return;
			}
		}
	}
	if (mode == 11) {
		if (ret2 >= 25) {
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
				endflg = 0;
				return;
			}
		}
	}
	if (mode == 12) {
		if (ret2 >= 31) {
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
				endflg = 0;
				return;
			}
		}
	}
	if (mode == 6) {
		if (ret2 == 141 || ret2 > 143) {
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
				REFTIME aa;
				pMediaPosition->get_Duration(&aa);
				aa1 = oggsize2 = aa;
				m_time.SetRange(0, (int)(aa * 100), TRUE);
				m_time.SetSelection(0, (int)(aa * 100) - 1);
				m_time.Invalidate();
				videoonly = TRUE; fade = 1.0f;
				if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
				endflg = 0;
				return;
			}
		}
		if (ret2 == 142) filen = "ED6021.ogg";
		if (ret2 == 143) filen = "ED6022.ogg";

	}
	if (mode == 5 && ret2 > 39) {
		if (ret2 == 40) filen = "YSO_020.ogg"; else
			if (ret2 == 41) filen = "YSO_019.ogg"; else
				if (ret2 == 43) filen = "YSO_037.ogg"; else
					if (ret2 == 44) filen = "YSO_038.ogg"; else
						if (ret2 == 45) filen = "YSO_039.ogg"; else
							if (ret2 == 46) filen = "YSO_033.ogg"; else
								if (ret2 == 47) filen = "YSO_034.ogg"; else
									if (ret2 == 42) filen = "YSO_032.ogg";
									else {
										plf = 1;
										dougaplay(ret2);
										if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
										if (pMediaControl)pMediaControl->Run();
										m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
										REFTIME aa;
										pMediaPosition->get_Duration(&aa);
										aa1 = oggsize2 = aa;
										m_time.SetRange(0, (int)(aa * 100), TRUE);
										m_time.SetSelection(0, (int)(aa * 100) - 1);
										m_time.Invalidate();
										videoonly = TRUE; fade = 1.0f;
										if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
										endflg = 0;
										return;
									}
	}
	if (ret2 > 54 && mode == 2)
		if (m_dou.GetCheck() == 1)
		{
			plf = 1;
			dougaplay(ret2);
			if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
			if (pMediaControl)pMediaControl->Run();
			m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
			REFTIME aa;
			pMediaPosition->get_Duration(&aa);
			aa1 = oggsize2 = aa;
			m_time.SetRange(0, (int)(aa * 100), TRUE);
			m_time.SetSelection(0, (int)(aa * 100) - 1);
			m_time.Invalidate();
			videoonly = TRUE; fade = 1.0f;
			if (pl && plw)SetAdd(fnn, mode, loop1, loop2, filen, ret2, aa);
			endflg = 0;
			return;
		}


	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		filen = "";		m_saisai.EnableWindow(TRUE); endflg = 0; return;
	}
	wl = 0;

	CFile aaa_1;
	int cor = filen.Find(L":", 6); // c:\\ :は2文字 //\/c: :は5文字目 6文字目からスタート
	ss = filen;
	if (cor != -1) {
		ss = filen.Left(filen.Find(L":", 6));
	}
	if (!aaa_1.Open(ss, CFile::modeRead | CFile::shareDenyWrite) && !(mode > 0 && mode <= 21 || mode == -6 || mode == -11 || mode == -12 || mode == -13 || mode == -14 || mode == -15 || mode == 30)) {
		MessageBox(LL14(
			L"ファイルが開けませんでした。\n削除されたか移動した可能性があります。", /* 日本語 */
			L"Could not open file.\nIt may have been deleted or moved.", /* 英語 */
			L"Impossible d'ouvrir le fichier.\nIl a peut-être été supprimé ou déplacé.", /* フランス語 */
			L"Impossibile aprire il file.\nPotrebbe essere stato eliminato o spostato.", /* イタリア語 */
			L"No se pudo abrir el archivo.\nPuede haber sido eliminado o movido.", /* スペイン語 */
			L"파일을 열 수 없습니다.\n삭제되었거나 이동되었을 가능성이 있습니다.", /* 韓国語 */
			L"无法打开文件。\n该文件可能已被删除或移动。", /* 中国語 */
			L"تعذر فتح الملف.\nربما تم حذفه أو نقله.", /* アラビア語 */
			L"Не удалось открыть файл.\nВозможно, он был удалён или перемещён.", /* ロシア語 */
			L"Datei konnte nicht geöffnet werden.\nSie wurde möglicherweise gelöscht oder verschoben.", /* ドイツ語 */
			L"Não foi possível abrir o arquivo.\nEle pode ter sido excluído ou movido.", /* ポルトガル語 */
			L"Kan het bestand niet openen.\nHet is mogelijk verwijderd of verplaatst.", /* オランダ語 */
			L"Nie można otworzyć pliku.\nMógł zostać usunięty lub przeniesiony.", /* ポーランド語 */
			L"Dosya açılamadı.\nSilinmiş veya taşınmış olabilir.")); /* トルコ語 */		stop1();
		return;
	}
	aaa_1.Close();

	ss = filen.Left(filen.ReverseFind('\\'));
	ss = ss.Left(ss.ReverseFind('\\'));

	fade1 = 0; fade = 1.0f; fadeadd = 0.0f;
	m_mp3jake.EnableWindow(FALSE);
	if (m_dou.GetCheck() == 1) {
		dougaplay(ret2, ss);
		if (pGraphBuilder && pMediaControl) {
			pMediaControl->Pause();
		}

	}

	CBitmap bbbb;
	HBITMAP bbbbb = bbbb;
	//	ReleaseOggVorbis(&ogg);
	//	CFile f;
	//	if(f.Open(filen,CFile::modeRead | CFile::shareDenyWrite,NULL)!=TRUE)
	//		return;
	//	f.Close();
	wavchannel = 2;
	wavsam_depth = 16;
	ZeroMemory(bufwav3, sizeof(bufwav3));
	if (((mode >= 10 && mode <= 21) || mode <= -10) && mode != -10 || mode == -6 || mode == 30) {
		thend1 = FALSE;
		wavwait = 0;
		if (mode == 30) wavbit_sample_Hz = 48000;
		thend = 0;
		// 初期 DispatchPlaywavFill は BeginPlaybackNotifyThread より前。
		// g_openDecoderMode が INT_MIN のままだと ActiveDecodeMode が空振りして無音になる。
		g_openDecoderMode = mode;
		wav_start();
		//		m_thread1 = ::AfxBeginThread((AFX_THREADPROC)wavread, (LPVOID)NULL,THREAD_PRIORITY_ABOVE_NORMAL,0,0,0);
		//		::SetPriorityClass(m_thread1, HIGH_PRIORITY_CLASS);
		//CRuntimeClass *pRuntime = RUNTIME_CLASS(CWread);
		CWread* g_pThread;// = (CWread*)pRuntime->CreateObject();
		//g_pThread->CreateThread(0, 0, NULL);
		g_pThread = (CWread*)AfxBeginThread(RUNTIME_CLASS(CWread), THREAD_PRIORITY_ABOVE_NORMAL, NULL, 0, NULL);
		if (g_pThread) {
			::SetPriorityClass(g_pThread, HIGH_PRIORITY_CLASS);
			// PostThreadMessage はワーカースレッドがメッセージキューを生成する前に呼ぶと
			// 失敗してメッセージを取りこぼす。取りこぼすと wavread1 が実行されず wavwait が
			// 0 のまま残り、下の「wavwait 待ち」ループが永久に固まる（曲切替/終了時のまれな
			// フリーズの主因）。成功するまでリトライしてデコード開始を保証する。
			BOOL posted = FALSE;
			for (int retry = 0; retry < 800 && !posted; ++retry) {
				if (g_pThread->PostThreadMessage(WM_APP + 100, NULL, NULL))
					posted = TRUE;
				else
					Sleep(1);
			}
			// 念のため: 起動に失敗しても待ちループを抜けられるようフラグを立てる。
			if (!posted) { wavwait = 1; thend = 1; }
		}
		else {
			// スレッド生成自体に失敗した場合も永久待ちを防ぐ。
			wavwait = 1; thend = 1;
		}
		for (int k = 0; k < 100; k++)
			DoEvent();
	}
	else if (mode == -3 || mode == -10 || mode == -9 || mode == -8 || mode == -7 || mode == -6 || mode == 30 || mode == 999) {

	}
	else {
		if (mode != -6) { // ogg
			oggsize = LoadOggVorbis(filen, 2, &ogg, m_time);
			if (oggsize < 0) {
				m_saisai.EnableWindow(TRUE);
					fnn = LL14(
						L"ファイル又はフォルダがありません",         /* 日本語 */
						L"File or folder not found",           /* 英語 */
						L"Fichier ou dossier introuvable",     /* フランス語 */
						L"File o cartella non trovati",        /* イタリア語 */
						L"Archivo o carpeta no encontrados",   /* スペイン語 */
						L"파일 또는 폴더가 없습니다",             /* 韓国語 */
						L"找不到文件或文件夹",                   /* 中国語 */
						L"الملف أو المجلد غير موجود",          /* アラビア語 */
						L"Файл или папка не найдены",          /* ロシア語 */
						L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
						L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
						L"Bestand of map niet gevonden",       /* オランダ語 */
						L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
						L"Dosya veya klasör bulunamadı");       /* トルコ語 */
				endflg = 0;
				return;
			}
			loop1 = loop2 = 0; stitle = "";
			if (vf.vc->comments >= 2)
			{
				CString cc;
				for (int iii = 0; iii < vf.vc->comments; iii++) {
#if _UNICODE
					WCHAR* f; f = new WCHAR[0x300000];
					MultiByteToWideChar(CP_UTF8, 0, vf.vc->user_comments[iii], -1, f, 0x300000);
					cc = f;
					delete[] f;
#else
					cc = vf.vc->user_comments[iii];
#endif
					if (cc.Left(6).MakeUpper() == "TITLE=")
					{
#if _UNICODE
						ss = UTF8toUNI(cc.Mid(6));
#else
						ss = UTF8toSJIS(cc.Mid(6));
#endif
						stitle = ss;
					}
					if (cc.Left(10) == "LOOPSTART=")
					{
						loop1 = _tstoi(cc.Mid(10));
					}
					if (cc.Left(11) == "LOOPLENGTH=")
					{
						loop2 = _tstoi(cc.Mid(11));
					}
					if (cc.Left(23) == "METADATA_BLOCK_PICTURE=")
					{
						m_mp3jake.EnableWindow(TRUE);
					}
				}
			}
		}
		else {

		}
	}
	// mode 30: CWread 完了を ys8/零軌 loop 上書きより先に待つ（0:00/古い loop1,2 のまま return するのを防ぐ）
	if (mode == 30) {
		const DWORD wavWaitT0 = GetTickCount();
		for (; wavwait == 0;) {
			Sleep(10);
			if (GetTickCount() - wavWaitT0 >= 60000u)
				break;
		}
		if (adbuf2 == NULL) {
			thend1 = TRUE;
			endflg = 0;
			playf = 0;
			return;
		}
	}
	//ファイル保存用（wavExport時はcc1を後で設定するためここではリセットしない）
	if (wavExportPath.GetLength() == 0) cc1 = 0;
	playb = 0;
	m_time.SetPos((int)playb);
	if (ogg) ov_pcm_seek_lap(&vf, (ogg_int64_t)0);
	poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
	g_oggPcmDecodePos = 0;
	g_oggRbPrimingNeed = 0;
	ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
	ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
	ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
	ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
	ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
	//wavoutで
	playb = 0;
	lo = loc = locs = 0;
	//Stereo 16bit 44kHz
	loc = 0;

	//ys8用（mode 30 は CWread が loop1/2 を設定済み。ここで上書きしない）
	if (mode != 30) {
	CStdioFile f;
	char* buff;
	int looping = 0;
	int igg;
	ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
	ss = ss.Left(ss.ReverseFind('.'));
	char file[256];
	WCHAR outcm[1024];
	WideCharToMultiByte(CP_ACP, 0, ss, 1024, file, 256, NULL, NULL);
	FILE* fp;
	TRY{
	fp = _wfopen(filen.Left(filen.ReverseFind('\\')) + L"\\..\\text\\bgmtbl.tbl", L"r");
	}CATCH_ALL(e) {
		fp = NULL;
	}END_CATCH_ALL
		if (fp) {
			buff = (char*)calloc(256, 1);
			for (;;) {
				if (fgets(buff, 256, fp) == NULL) {
					free(buff); break;
				}
				char* p = strstr(buff, file);
				if (p == NULL) continue;
				if (buff[0] == '/') continue;
				p += strlen(file) + 1;
				for (; *p == 0x09; p++);
				if (*p == '1') looping = 1;
				p++;
				for (; *p == 0x09; p++);
				typedef struct {
					char st[8];
					char a[1];
					char ed[8];
				} aa;
				aa* aa1;
				aa1 = (aa*)p;
				int i, j;
				j = 0;
				for (i = 0; i < 8; i++) {
					switch (aa1->st[i])
					{
					case '0':
						j *= 10; j += 0;
						break;
					case '1':
						j *= 10; j += 1;
						break;
					case '2':
						j *= 10; j += 2;
						break;
					case '3':
						j *= 10; j += 3;
						break;
					case '4':
						j *= 10; j += 4;
						break;
					case '5':
						j *= 10; j += 5;
						break;
					case '6':
						j *= 10; j += 6;
						break;
					case '7':
						j *= 10; j += 7;
						break;
					case '8':
						j *= 10; j += 8;
						break;
					case '9':
						j *= 10; j += 9;
						break;
					}
				}
				loop1 = j;
				j = 0;
				for (i = 0; i < 8; i++) {
					switch (aa1->ed[i])
					{
					case '0':
						j *= 10; j += 0;
						break;
					case '1':
						j *= 10; j += 1;
						break;
					case '2':
						j *= 10; j += 2;
						break;
					case '3':
						j *= 10; j += 3;
						break;
					case '4':
						j *= 10; j += 4;
						break;
					case '5':
						j *= 10; j += 5;
						break;
					case '6':
						j *= 10; j += 6;
						break;
					case '7':
						j *= 10; j += 7;
						break;
					case '8':
						j *= 10; j += 8;
						break;
					case '9':
						j *= 10; j += 9;
						break;
					}
				}
				loop2 = j - loop1;
				p += sizeof(aa) + 1;
				for (; *p == 0x09; p++);
				p += 3;
				char* pp = p;
				for (; *p != 0xd; p++) {
					if (*p == 0x9) {
						*p = 0x20;
					}
				}
				p = pp;
				MultiByteToWideChar(CP_ACP, 0, p, -1, outcm, 1024);
				stitle = outcm;
				stitle.Trim();
				if (looping == 0) {
					loop1 = loop2 = 0;
				}
				free(buff); break;
			}
			fclose(fp);

		}
	//YSC
	ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
	if (ss == "yc_b001.ogg") {
		loop1 = 123438;
		loop2 = 4742104;
		stitle = LL14(L"バトル#58", L"Battle #58", L"Bataille #58", L"Battaglia #58", L"Batalla #58", L"전투 #58", L"战斗 #58", L"معركة #58", L"Битва #58", L"Kampf #58", L"Batalha #58", L"Gevecht #58", L"Bitwa #58", L"Sava? #58");
	}
	if (ss == "yc_b002.ogg") {
		loop1 = 504378;
		loop2 = 5153813;
		stitle = LL14(L"灼熱の炎の中で", L"Within the Blazing Flames", L"Dans les flammes ardentes", L"Tra le fiamme ardenti", L"Entre las llamas ardientes", L"작열의 불꽃 속에서", L"在灼热的火焰中", L"داخل ألسنة اللهب المحرقة", L"В пылающем пламени", L"Im lodernden Feuer", L"Entre as chamas ardentes", L"In de laaiende vlammen", L"W p?on?cych p?omieniach", L"Alevlerin ?cinde");
	}
	if (ss == "yc_b003.ogg") {
		loop1 = 32845;
		loop2 = 6955200;
		stitle = LL14(L"最終決戦", L"Final Battle", L"Bataille finale", L"Battaglia finale", L"Batalla final", L"최종 결전", L"最终决战", L"المعركة النهائية", L"Финальная битва", L"Endkampf", L"Batalha final", L"Eindstrijd", L"Ostateczna bitwa", L"Son Sava?");
	}
	if (ss == "yc_b004.ogg") {
		loop1 = 53237;
		loop2 = 9737128;
		stitle = LL14(L"黒き翼", L"Black Wings", L"Ailes noires", L"Ali nere", L"Alas negras", L"검은 날개", L"黑色之翼", L"الأجنحة السوداء", L"Чёрные крылья", L"Schwarze Flugel", L"Asas negras", L"Zwarte vleugels", L"Czarne skrzyd?a", L"Siyah Kanatlar");
	}
	if (ss == "yc_b005.ogg") {
		loop1 = 1123422;
		loop2 = 7687672;
		stitle = "The False God of Causality";
	}
	if (ss == "yc_d101.ogg") {
		loop1 = 303237;
		loop2 = 2582426;
		stitle = LL14(L"ダンジョン", L"Dungeon", L"Donjon", L"Dungeon", L"Mazmorra", L"던전", L"地牢", L"زنزانة", L"Подземелье", L"Verlies", L"Masmorra", L"Kerker", L"Loch", L"Zindan");
	}
	if (ss == "yc_d201.ogg") {
		loop1 = 447792;
		loop2 = 3479666;
		stitle = LL14(L"道化師の誘い", L"Clown's Invitation", L"L'invitation du bouffon", L"L'invito del giullare", L"La invitacion del bufon", L"광대의 초대", L"小丑的邀请", L"دعوة المهرج", L"Приглашение шута", L"Einladung des Clowns", L"Convite do palhaco", L"Uitnodiging van de clown", L"Zaproszenie klauna", L"Palyaconun Daveti");
	}
	if (ss == "yc_d301.ogg") {
		loop1 = 351836;
		loop2 = 3969072;
		stitle = LL14(L"地下遺跡", L"Underground Ruins", L"Ruines souterraines", L"Rovine sotterranee", L"Ruinas subterraneas", L"지하 유적", L"地下遗迹", L"الآثار تحت الأرض", L"Подземные руины", L"Unterirdische Ruinen", L"Ruinas subterraneas", L"Ondergrondse ruines", L"Podziemne ruiny", L"Yeralt? Harabeleri");
	}
	if (ss == "yc_d401.ogg") {
		loop1 = 93865;
		loop2 = 4349569;
		stitle = LL14(L"導きの塔〜エルディールにくちづけを", L"Tower of Guidance -Kiss for Eldeel-", L"Tour de la Guidance -Un baiser pour Eldeel-", L"Torre della Guida -Un bacio per Eldeel-", L"Torre de la Guia -Un beso para Eldeel-", L"??? ? ~????? ????~", L"引?之塔〜献?埃?迪?的吻", L"??? ??????? -???? ??????-", L"Башня Наставления -Поцелуй для Элдила-", L"Turm der Fuhrung -Kuss fur Eldeel-", L"Torre da Orientacao -Um beijo para Eldeel-", L"Toren van Geleiding -Kus voor Eldeel-", L"Wie?a Przewodnictwa -Poca?unek dla Eldeel-", L"Rehberlik Kulesi -Eldeel icin Opucuk-");
	}
	if (ss == "yc_d501.ogg") {
		loop1 = 832720;
		loop2 = 7219417;
		stitle = LL14(L"失われし仮面を求めて", L"Seeking the Lost Mask", L"A la recherche du masque perdu", L"Alla ricerca della maschera perduta", L"En busca de la mascara perdida", L"잃어버린 가면을 찾아서", L"寻找失落的面具", L"البحث عن القناع المفقود", L"В поисках утерянной маски", L"Auf der Suche nach der verlorenen Maske", L"Em busca da mascara perdida", L"Op zoek naar het verloren masker", L"W poszukiwaniu zaginionej maski", L"Kay?p Maskeyi Ararken");
	}
	if (ss == "yc_d701.ogg") {
		loop1 = 809264;
		loop2 = 6545498;
		stitle = LL14(L"イリス", L"Iris", L"Iris", L"Iris", L"Iris", L"이리스", L"伊莉丝", L"إيريس", L"Ирис", L"Iris", L"Iris", L"Iris", L"Iris", L"Iris");
	}
	if (ss == "yc_d702.ogg") {
		loop1 = 34816;
		loop2 = 1189171;
		stitle = "yc_d702";
	}
	if (ss == "yc_d703.ogg") {
		loop1 = 719876;
		loop2 = 2557197;
		stitle = LL14(L"聖域", L"Sanctuary", L"Sanctuaire", L"Santuario", L"Santuario", L"성역", L"圣域", L"ملاذ مقدس", L"Святилище", L"Heiligtum", L"Santuario", L"Heiligdom", L"Sanktuarium", L"Kutsal Alan");
	}
	if (ss == "yc_e001.ogg") {
		loop1 = 300048;
		loop2 = 3389821;
		stitle = LL14(L"賢者", L"Sage", L"Sage", L"Saggio", L"Sabio", L"현자", L"贤者", L"الحكيم", L"Мудрец", L"Weiser", L"Sabio", L"Wijze", L"M?drzec", L"Bilge");
	}
	if (ss == "yc_e002.ogg") {
		loop1 = 326209;
		loop2 = 3604271;
		stitle = LL14(L"復活の儀式", L"Resurrection Ceremony", L"Ceremonie de resurrection", L"Cerimonia della resurrezione", L"Ceremonia de resurreccion", L"부활 의식", L"复活仪式", L"طقوس القيامة", L"Церемония воскрешения", L"Auferstehungszeremonie", L"Cerimonia de ressurreicao", L"Opstandingsceremonie", L"Ceremonia zmartwychwstania", L"Dirili? Toreni");
	}
	if (ss == "yc_e003.ogg") {
		loop1 = 806906;
		loop2 = 4275899;
		stitle = LL14(L"レファンス", L"Refance", L"Refance", L"Refance", L"Refance", L"레판스", L"雷凡斯", L"ريفانس", L"Рефанс", L"Refance", L"Refance", L"Refance", L"Refance", L"Refance");
	}if (ss == "yc_e004.ogg") {
		loop1 = 326209;
		loop2 = 4945888;
		stitle = LL14(L"涙の少年剣士", L"Young Swordsman in Tears", L"Le jeune épéiste en larmes", L"Il giovane spadaccino in lacrime", L"El joven espadachín en lágrimas", L"눈물의 소년 검사", L"含泪的少年剑士", L"مبارز فتى حزين", L"Юный фехтовальщик в слезах", L"Der junge Schwertkämpfer in Tränen", L"O jovem espadachim em lágrimas", L"De jonge zwaardvechter in tranen", L"Młody szermierz we łzach", L"Gözyaşlarındaki Genç Kılıç Savaşçısı");
	}if (ss == "yc_e005.ogg") {
		loop1 = 24000;
		loop2 = 3605888;
		stitle = LL14(L"エルディール", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"엘딜", L"埃尔迪尔", L"إلديل", L"Элдил", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel");
	}if (ss == "yc_e006.ogg") {
		loop1 = 69040;
		loop2 = 1209633;
		stitle = LL14(L"ロムン帝国 -嗚呼レオ団長-", L"Romun Empire -Alas Captain Leo-", L"Empire Romun -Hélas Capitaine Léo-", L"Impero Romun -Ahimè Capitano Leo-", L"Imperio Romun -¡Ay, Capitán Leo!-", L"로문 제국 -아아 레오 단장-", L"罗曼帝国 -呜呼，雷欧团长-", L"إمبراطورية رومون -وا أسفاه أيها القائد ليو-", L"Империя Ромун -Увы, капитан Лео-", L"Romun-Reich -Ach, Hauptmann Leo-", L"Império Romun -Ai, Capitão Leo-", L"Romun Keizerrijk -Helaas Kapitein Leo-", L"Imperium Romun -Niestety Kapitanie Leo-", L"Romun İmparatorluğu -Ah Kaptan Leo-");
	}if (ss == "yc_e008.ogg") {
		loop1 = 275476;
		loop2 = 3609611;
		stitle = "yc_e008";
	}if (ss == "yc_e010.ogg") {
		loop1 = 807040;
		loop2 = 5159922;
		stitle = LL14(L"冒険家、誕生", L"Birth of an Adventurer", L"Naissance d'un aventurier", L"Nascita di un avventuriero", L"Nacimiento de un aventurero", L"모험가, 탄생", L"冒险家的诞生", L"ولادة مغامر", L"Рождение авантюриста", L"Geburt eines Abenteurers", L"Nascimento de um aventureiro", L"Geboorte van een avonturier", L"Narodziny poszukiwacza przygód", L"Bir Maceracının Doğuşu");
	}if (ss == "yc_f101.ogg") {
		loop1 = 568926;
		loop2 = 5668207;
		stitle = LL14(L"燃ゆる剣", L"Burning Sword", L"L'épée ardente", L"La spada ardente", L"La espada ardiente", L"불타는 검", L"燃烧之剑", L"السيف المشتعل", L"Пылающий меч", L"Das brennende Schwert", L"A espada ardente", L"Het brandende zwaard", L"Płonący miecz", L"Yanan Kılıç");
	}if (ss == "yc_f201.ogg") {
		loop1 = 588624;
		loop2 = 6209316;
		stitle = LL14(L"セルセタの樹海", L"Forest of Celceta", L"Forêt de Celceta", L"Foresta di Celceta", L"Bosque de Celceta", L"셀세타의 수해", L"塞尔塞塔的树海", L"غابة سيلسيتا", L"Лес Селсеты", L"Wald von Celceta", L"Floresta de Celceta", L"Woud van Celceta", L"Las Celcety", L"Celceta Ormanı");
	}if (ss == "yc_f301.ogg") {
		loop1 = 1145404;
		loop2 = 5960203;
		stitle = LL14(L"クレーター", L"Crater", L"Cratère", L"Cratere", L"Cráter", L"크레이터", L"陨石坑", L"فوهة البركان", L"Кратер", L"Krater", L"Cratera", L"Krater", L"Krater", L"Krater");
	}if (ss == "yc_f401.ogg") {
		loop1 = 408974;
		loop2 = 3161454;
		stitle = "THE DAWN OF YS";
	}if (ss == "yc_f501.ogg") {
		loop1 = 2604464;
		loop2 = 4559688;
		stitle = LL14(L"暁の森", L"Forest of Dawn", L"Forêt de l'aube", L"Foresta dell'alba", L"Bosque del amanecer", L"새벽의 숲", L"黎明之森", L"غابة الفجر", L"Лес рассвета", L"Wald der Morgenröte", L"Floresta do amanhecer", L"Woud van de dageraad", L"Las świtu", L"Şafak Ormanı");
	}if (ss == "yc_f601.ogg") {
		loop1 = 581264;
		loop2 = 3661828;
		stitle = LL14(L"一陣の風", L"Gust of Wind", L"Rafale de vent", L"Folata di vento", L"Ráfaga de viento", L"한 줄기 바람", L"一阵风", L"هبة ريح", L"Порыв ветра", L"Windböe", L"Rajada de vento", L"Windvlaag", L"Podmuch wiatru", L"Rüzgar Esintisi");
	}if (ss == "yc_f701.ogg") {
		loop1 = 324287;
		loop2 = 9010870;
		stitle = LL14(L"神代の地", L"Land of the Gods", L"Terre des dieux", L"Terra degli dei", L"Tierra de los dioses", L"신대의 땅", L"神代之地", L"أرض الآلهة", L"Земля богов", L"Land der Götter", L"Terra dos deuses", L"Land der goden", L"Kraina bogów", L"Tanrıların Toprağı");
	}if (ss == "yc_f801.ogg") {
		loop1 = 315435;
		loop2 = 4546653;
		stitle = LL14(L"真実への序曲", L"Overture to Truth", L"Ouverture vers la vérité", L"Ouverture alla verità", L"Obertura hacia la verdad", L"진실을 향한 서곡", L"通往真实的序曲", L"مقدمة نحو الحقيقة", L"Увертюра к истине", L"Ouverture zur Wahrheit", L"Abertura para a verdade", L"Ouverture naar de waarheid", L"Uwertura do prawdy", L"Gerçeğe Uvertür");
	}if (ss == "yc_f901.ogg") {
		loop1 = 178544;
		loop2 = 4786555;
		stitle = LL14(L"雨上がりの朝に", L"Morning After the Rain", L"Matin après la pluie", L"Mattino dopo la pioggia", L"Mañana después de la lluvia", L"비 갠 아침에", L"雨后的早晨", L"الصباح بعد المطر", L"Утро после дождя", L"Morgen nach dem Regen", L"Manhã após a chuva", L"Ochtend na de regen", L"Poranek po deszczu", L"Yağmur Sonrası Sabah");
	}if (ss == "yc_over.ogg") {
		loop1 = 19200;
		loop2 = 4924407;
		stitle = LL14(L"ゲームオーバー", L"Game Over", L"Partie terminée", L"Game Over", L"Fin del juego", L"게임 오버", L"游戏结束", L"نهاية اللعبة", L"Конец игры", L"Spiel vorbei", L"Fim de jogo", L"Spel voorbij", L"Koniec gry", L"Oyun Bitti");
	}if (ss == "yc_t101.ogg") {
		loop1 = 865353;
		loop2 = 4409988;
		stitle = LL14(L"辺境都市《キャスナン》", L"Frontier City Casnan", L"Ville frontalière Casnan", L"Città di frontiera Casnan", L"Ciudad fronteriza Casnan", L"변방 도시 《캐스난》", L"边境都市《卡斯南》", L"مدينة كاسنان الحدودية", L"Пограничный город Каснан", L"Grenzstadt Casnan", L"Cidade de fronteira Casnan", L"Grensstad Casnan", L"Miasto graniczne Casnan", L"Sınır Kenti Casnan");
	}if (ss == "yc_t201.ogg") {
		loop1 = 58906;
		loop2 = 6120526;
		stitle = LL14(L"優しくなりたい", L"I Want to Be Kind", L"Je veux être gentil(le)", L"Voglio essere gentile", L"Quiero ser amable", L"상냥해지고 싶어", L"想要变得温柔", L"أريد أن أكون لطيفاً", L"Я хочу быть добрым", L"Ich möchte freundlich sein", L"Quero ser gentil", L"Ik wil aardig zijn", L"Chcę być uprzejmy", L"İyi Biri Olmak İstiyorum");
	}if (ss == "yc_t301.ogg") {
		loop1 = 425910;
		loop2 = 9606150;
		stitle = LL14(L"古代の伝承", L"Ancient Legend", L"Légende ancienne", L"Leggenda antica", L"Leyenda antigua", L"고대의 전승", L"古代传承", L"الأسطورة القديمة", L"Древняя легенда", L"Alte Legende", L"Lenda antiga", L"Oude legende", L"Starożytna legenda", L"Kadim Efsane");
	}if (ss == "yc_t501.ogg") {
		loop1 = 782252;
		loop2 = 7781799;
		stitle = "RODA";
	}if (ss == "yc_title.ogg") {
		loop1 = 10000;
		loop2 = 4924407;
		stitle = "THEME OF ADOL 2012";
	}if (ss == "yc_op.ogg") {
		stitle = "The Foliage Ocean in CELCETA -Opening size-";
	}if (ss == "yc_end.ogg") {
		stitle = LL14(L"新たな時代のステージへ", L"To the Stage of a New Era", L"Vers la scène d'une nouvelle ère", L"Verso il palcoscenico di una nuova era", L"Al escenario de una nueva era", L"새로운 시대의 스테이지로", L"走向新时代的舞台", L"إلى مرحلة عصر جديد", L"На сцену новой эпохи", L"Auf die Bühne einer neuen Ära", L"Para o palco de uma nova era", L"Naar het podium van een nieuw tijdperk", L"Na scenę nowej ery", L"Yeni Bir Çağın Sahnesine");
	}

	//零の軌跡用
	fp = _wfsopen(filen.Left(filen.ReverseFind('\\')) + L"\\..\\text\\t_bgm._dt", L"rb", 0x40);
	if (fp) {
		struct a_ {
			long start;
			long end;
			long no;
			long w;
		};
		a_ a;
		ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		CString sss;
		sss = ss.Mid(2, 4);
		int no = _ttoi(sss);
		int fg = 0;
		for (;;) {
			fread(&a, 16, 1, fp);
			if (feof(fp)) break;
			if (no == a.no) {
				fg = 1;
				loop1 = a.start;
				loop2 = a.end;
				break;
			}
		}
		fclose(fp);
		sss = savedata.zero;
		if (fg == 0 && sss == L"") {
			int ret = MessageBox(LL14(
				L"おそらく碧の軌跡のbgmデータで、碧の軌跡のbgmテーブルに情報がありません。\n零の軌跡のbgmテーブルを参照しますか？\n(碧の軌跡には零の軌跡のbgmデータも入ってるため、ループ情報は零の軌跡側にあります)",
				L"Likely Ao no Kiseki BGM data, but no info in the Ao BGM table.\nRefer to the Zero no Kiseki BGM table?\n(Ao includes Zero BGM data; loop info is in the Zero table.)",
				L"Probablement des données BGM d'Ao no Kiseki, mais aucune info dans la table BGM d'Ao.\nUtiliser la table BGM de Zero no Kiseki ?\n(Ao contient les données BGM de Zero ; les infos de boucle sont dans Zero.)",
				L"Probabilmente dati BGM di Ao no Kiseki, ma nessuna info nella tabella BGM di Ao.\nUsare la tabella BGM di Zero no Kiseki?\n(Ao include i dati BGM di Zero; le info loop sono in Zero.)",
				L"Probablemente datos BGM de Ao no Kiseki, pero sin info en la tabla BGM de Ao.\n¿Usar la tabla BGM de Zero no Kiseki?\n(Ao incluye datos BGM de Zero; la info de bucle está en Zero.)",
				L"아마도 벽의 궤적의 BGM 데이터이지만, 벽의 궤적 BGM 테이블에 정보가 없습니다.\n영의 궤적 BGM 테이블을 참조하시겠습니까?\n(벽의 궤적에는 영의 궤적 BGM 데이터도 포함되어 있어, 루프 정보는 영의 궤적 쪽에 있습니다)",
				L"可能是碧之轨迹的BGM数据，但碧之轨迹BGM表中没有相关信息。\n是否参照零之轨迹的BGM表？\n(碧之轨迹中也包含零之轨迹的BGM数据，因此循环信息在零之轨迹一侧)",
				L"يبدو أنها بيانات BGM لـ Ao no Kiseki، لكن لا توجد معلومات في جدول Ao.\nهل تريد الرجوع إلى جدول BGM لـ Zero no Kiseki؟\n(تتضمن Ao بيانات Zero؛ معلومات الحلقة موجودة في جدول Zero.)",
				L"Вероятно, это данные BGM из Ao no Kiseki, но в таблице BGM Ao нет информации.\nИспользовать таблицу BGM Zero no Kiseki?\n(Ao включает данные BGM из Zero; информация о петле находится в таблице Zero.)",
				L"Wahrscheinlich Ao no Kiseki BGM-Daten, aber keine Info in der Ao BGM-Tabelle.\nZero no Kiseki BGM-Tabelle verwenden?\n(Ao enthält Zero BGM-Daten; Loop-Infos befinden sich in der Zero-Tabelle.)",
				L"Provavelmente dados BGM de Ao no Kiseki, mas sem informação na tabela BGM de Ao.\nUsar a tabela BGM de Zero no Kiseki?\n(Ao inclui dados BGM de Zero; as informações de loop estão em Zero.)",
				L"Waarschijnlijk Ao no Kiseki BGM-data, maar geen info in de Ao BGM-tabel.\nZero no Kiseki BGM-tabel gebruiken?\n(Ao bevat Zero BGM-data; loop-info staat in de Zero-tabel.)",
				L"Prawdopodobnie dane BGM z Ao no Kiseki, ale brak informacji w tabeli BGM Ao.\nUżyć tabeli BGM Zero no Kiseki?\n(Ao zawiera dane BGM z Zero; informacje o pętli są w tabeli Zero.)",
				L"Muhtemelen Ao no Kiseki BGM verisi, ancak Ao BGM tablosunda bilgi yok.\nZero no Kiseki BGM tablosu kullanılsın mı?\n(Ao, Zero BGM verilerini de içerir; döngü bilgisi Zero tarafındadır.)"),
				LL14(
					L"bgmテーブルに情報がありません。",
					L"No info in the BGM table.",
					L"Aucune info dans la table BGM.",
					L"Nessuna info nella tabella BGM.",
					L"Sin información en la tabla BGM.",
					L"BGM 테이블에 정보가 없습니다.",
					L"BGM表中没有信息。",
					L"لا توجد معلومات في جدول BGM.",
					L"Нет информации в таблице BGM.",
					L"Keine Info in der BGM-Tabelle.",
					L"Sem informação na tabela BGM.",
					L"Geen info in de BGM-tabel.",
					L"Brak informacji w tabeli BGM.",
					L"BGM tablosunda bilgi yok."),
				MB_YESNO); if (ret == IDYES) {
				CZeroFol d;
				if (d.DoModal() == IDOK) {
					FILE* fp = _wfsopen(savedata.zero, L"rb", 0x40);
					if (fp) {
						ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
						CString sss;
						sss = ss.Mid(2, 4);
						int no = _ttoi(sss);
						for (;;) {
							fread(&a, 16, 1, fp);
							if (feof(fp)) break;
							if (no == a.no) {
								loop1 = a.start;
								loop2 = a.end;
								break;
							}
						}
						fclose(fp);
					}
				}
			}
		}
		else if (sss != L"") {
			FILE* fp = _wfsopen(savedata.zero, L"rb", 0x40);
			if (fp) {
				ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
				CString sss;
				sss = ss.Mid(2, 4);
				int no = _ttoi(sss);
				for (;;) {
					fread(&a, 16, 1, fp);
					if (feof(fp)) break;
					if (no == a.no) {
						loop1 = a.start;
						loop2 = a.end;
						break;
					}
				}
				fclose(fp);
			}
		}
	}
	} // mode != 30 (ys8/ysc/零軌 loop)
	//-------------------------------------------------------------------
	if (m_dsb != NULL) m_dsb->Release();
	m_dsb = NULL;
	//	char bufdmy[10000];
	ZeroMemory(bufwav3, sizeof(bufwav3));
	DWORD  dwDataLen = WAVDALen / OUTPUT_BUFFER_NUM;
	if (((mode >= 10 && mode <= 21) || mode <= -10) && mode != -10 || mode == -6 || mode == 30) {
		// mode 30 は ys8 ブロック前に CWread 完了済み
		if (mode != 30) {
			// wavwait はデコード用ワーカースレッド(CWread)が立てる。
			const DWORD wavWaitT0 = GetTickCount();
			DWORD wavWaitLimitMs = g_interactiveTrackChange ? 2500u : 8000u;
			if (IsFalcomGameBgmMode(mode) && wavWaitLimitMs < 30000u)
				wavWaitLimitMs = 30000u;
			for (; wavwait == 0;) {
				CWaitCursor rrr2;
				DoEvent();
				if (GetTickCount() - wavWaitT0 >= wavWaitLimitMs) break;
			}
		}
		if (adbuf2 == NULL) {
			// 打ち切り時に孤児 CWread が後から loop1/2 を書き換えるのを止める
			thend1 = TRUE;
			endflg = 0;
			playf = 0;
			return;
		}
		//		if(mode!=-10)
		//			playwavBuffwav(bufwav3,0,dwDataLen*4,0);
		//		else
		//			playwavBuffwav(bufdmy,0,dwDataLen*4,0);
	}
	else if (mode == -10) { //mp123
		CString s, ss;
		s = filen.Left(filen.ReverseFind('\\')); ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		_tchdir(s);
		CFile ff;
		if (ff.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) == FALSE) {
			MessageBox(LL14(
				L"ファイルが存在しません。\n削除されたかフォルダまたはファイル名が変更された可能性があります。", /* 日本語 */
				L"File does not exist.\nIt may have been deleted or the folder/filename may have changed.", /* 英語 */
				L"Le fichier n'existe pas.\nIl a peut-être été supprimé ou le nom du dossier/fichier a été modifié.", /* フランス語 */
				L"Il file non esiste.\nPotrebbe essere stato eliminato o la cartella/nome file potrebbe essere cambiato.", /* イタリア語 */
				L"El archivo no existe.\nPuede haber sido eliminado o el nombre de la carpeta/archivo puede haber cambiado.", /* スペイン語 */
				L"파일이 존재하지 않습니다.\n삭제되었거나 폴더 또는 파일 이름이 변경되었을 가능성이 있습니다.", /* 韓国語 */
				L"文件不存在。\n该文件可能已被删除，或文件夹/文件名已更改。", /* 中国語 */
				L"الملف غير موجود.\nربما تم حذفه أو تم تغيير اسم المجلد أو الملف.", /* アラビア語 */
				L"Файл не существует.\nВозможно, он был удалён или изменено имя папки/файла.", /* ロシア語 */
				L"Datei existiert nicht.\nSie wurde möglicherweise gelöscht oder der Ordner/Dateiname wurde geändert.", /* ドイツ語 */
				L"O arquivo não existe.\nEle pode ter sido excluído ou a pasta/nome do arquivo pode ter mudado.", /* ポルトガル語 */
				L"Het bestand bestaat niet.\nHet is mogelijk verwijderd of de map/bestandsnaam is gewijzigd.", /* オランダ語 */
				L"Plik nie istnieje.\nMógł zostać usunięty lub zmieniono nazwę folderu/pliku.", /* ポーランド語 */
				L"Dosya mevcut değil.\nSilinmiş veya klasör/dosya adı değiştirilmiş olabilir."), /* トルコ語 */
				LL14(
					L"ファイルが存在しません。", /* 日本語タイトル */
					L"File does not exist.", /* 英語 */
					L"Le fichier n'existe pas.", /* フランス語 */
					L"Il file non esiste.", /* イタリア語 */
					L"El archivo no existe.", /* スペイン語 */
					L"파일이 존재하지 않습니다.", /* 韓国語 */
					L"文件不存在。", /* 中国語 */
					L"الملف غير موجود.", /* アラビア語 */
					L"Файл не существует.", /* ロシア語 */
					L"Datei existiert nicht.", /* ドイツ語 */
					L"O arquivo não existe.", /* ポルトガル語 */
					L"Het bestand bestaat niet.", /* オランダ語 */
					L"Plik nie istnieje.", /* ポーランド語 */
					L"Dosya mevcut değil."), /* トルコ語 */
				MB_OK | MB_ICONERROR); m_saisai.EnableWindow(TRUE); endflg = 0; return;
		}ff.Close();

		BYTE buf[2005];
		ZeroMemory(buf, 2005);
		if (ff.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
			ff.Read(buf, 2000);
			int i;
			for (i = 0; i < 2000; i++) {
				if (buf[i] == 0x41 && buf[i + 1] == 0x50 && buf[i + 2] == 0x49 && buf[i + 3] == 0x43) {
					break;
				}
			}
			if (i != 2000) {
				m_mp3jake.EnableWindow(TRUE);
			}
		}ff.Close();
		mp3_.mp3init();
		si1.dwSamplesPerSec = savedata.samples;
		si1.dwChannels = 2;
		si1.dwBitsPerSample = 24;
		if (savedata.bit24 == 0) si1.dwBitsPerSample = 16;
		mp3_.Open(ss, &si1);
		g_openDecoderMode = -10;
		CMp3Info mp3__;
		mp3__.Load(ss);

		wavchannel = si1.dwChannels;
		wavbit_sample_Hz = si1.dwSamplesPerSec;
		wavsam_depth = si1.dwBitsPerSample;
		g_mp3_decoder_bps = Mp3DecoderBitsClampedFromObject();
		loop1 = 0; stitle = "";
		//		loop2=(int)(((float)(((float)si1.dwLength)*44.1f))/(44100.0f/((float)((wavchannel==2)?wavbit_sample_Hz:(wavbit_sample_Hz/2)))));
		//		loop2=(int)((float)(mp3_.m_mp3info.total_samples)/(wavchannel==2?1.0f:2.0f));
		loop2 = (int)(((double)mp3__.GetMSec()) / 1000.0 * (double)mp3_.m_mp3info.freq);//*(44100.0/((double)((wavchannel==2)?(double)wavbit_sample_Hz:((double)wavbit_sample_Hz/2.0)))));
		//		if(loop2==0){
		if (mp3_.m_mp3info.hasVbrtag) {
			//			loop2 *= 2.29;
		}
		//			loop2=(int)(((float)(((float)si1.dwLength)*44.1f))/(44100.0f/((float)((wavchannel==2)?wavbit_sample_Hz:(wavbit_sample_Hz/2)))));
		//		}
		data_size = oggsize = loop2;
		loop3 = loop2; loop2 = 0;
		// スライダーは OnHScroll で playb=curpos×100（PCM フレーム）。旧式は wavsam_depth を掛け 24bit 時だけ max が 1.5 倍になりシーク／経過がずれる。
		{
			int m = (data_size > 0) ? (int)(data_size / 100) : 0;
			if (m < 1) m = 1;
			m_time.SetRange(0, m, TRUE);
		}
		lenl = 0;
		if (mp3_.m_mp3info.hasVbrtag == 0)
			kbps = mp3_.m_mp3info.bitrate / 1000;
		else
			kbps = mp3_.m_mp3info.bitrate / 1000;
		//kbps=mp3_.m_mp3info.total_samples/mp3_.m_mp3info.freq;
		Vbr = mp3_.m_mp3info.hasVbrtag;
		savedata.mp3orig = 0;
		if (Vbr == 1) savedata.mp3orig = 1;
		CId3tagv1 ta1;
		CId3tagv2 ta2;
		int b = ta2.Load(ss);
		tagname = ta2.GetArtist(); if (b == -1) { ta1.Load(ss); tagname = ta1.GetArtist(); }
		tagfile = ta2.GetTitle(); if (b == -1) tagfile = ta1.GetTitle();
		tagalbum = ta2.GetAlbum(); if (b == -1) tagalbum = ta1.GetAlbum();
		if (tagfile == "") tagfile = ss;
		mp3file = ss;
		wav_start();
		//		playwavmp3(bufwav3,0,dwDataLen*4,0);
	}
	else if (mode == 999) { // wav
		CString s, ss;
		s = filen.Left(filen.ReverseFind('\\')); ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		if (s.GetLength() > 0) _tchdir(s);
		wavinfo wi;
		if (!wav_.Open(filen, &wi)) {
			m_saisai.EnableWindow(TRUE); endflg = 0; return;
		}
		g_openDecoderMode = 999;
		wav999_use_adbuf = 0;
		if (wav_.IsMSADPCM()) {
			CWaitCursor aaaa;
			CFile adpcmf;
			if (!adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
				m_saisai.EnableWindow(TRUE); endflg = 0; return;
			}
			char* decBuf = NULL;
			int decSize = 0;
			if (!decode_msadpcm_wav(adpcmf, wi, &decBuf, &decSize)) {
				adpcmf.Close();
				wav_.Close();
				m_saisai.EnableWindow(TRUE); endflg = 0; return;
			}
			adpcmf.Close();
			wav_.Close();
			if (adbuf2) free(adbuf2);
			adbuf2 = decBuf;
			data_size = oggsize = decSize;
			wavchannel = wi.nChannels;
			wavbit_sample_Hz = wi.nSamplesPerSec;
			wavsam_depth = 16;
			loop1 = 0;
			{
				int chAd = wavchannel;
				if (chAd <= 0) chAd = 1;
				const int pcmBpfDec = chAd * 2;
				loop2 = (pcmBpfDec > 0) ? (data_size / pcmBpfDec) : 0;
			}
			lenl = 0;
			poss = poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			wav999_use_adbuf = 1;
			if (wav) free(wav);
			wav_start();
			m_time.SetRange(0, loop2, TRUE);
			if (pl && plw && plcnt >= 0 && plcnt < pl->playcnt) {
				tagfile = pl->pc[plcnt].name;
				tagname = pl->pc[plcnt].art;
				tagalbum = pl->pc[plcnt].alb;
			}
			else {
				tagfile = (fnn.GetLength() > 0) ? fnn : ss;
				tagname = tagalbum = _T("");
			}
			endf = 1;
		}
		else if (wav_.IsIMAADPCM()) {
			wav_.Close();
			m_saisai.EnableWindow(TRUE); endflg = 0; return;
		}
		else {
			wavchannel = wi.nChannels;
			wavbit_sample_Hz = wi.nSamplesPerSec;
			// ファイルの実際のビット深度を優先（wavsam > bit24）。解釈は実データに合わせる
			wavsam_depth = (wi.wBitsPerSample <= 16) ? 16 : 24;
			loop1 = 0;
			loop2 = (int)wi.totalSamples;
			data_size = oggsize = (int)(loop2 * (wavsam_depth / 8) * wavchannel);
			m_time.SetRange(0, (int)loop2, TRUE);
			if (pl && plw && plcnt >= 0 && plcnt < pl->playcnt) {
				tagfile = pl->pc[plcnt].name;
				tagname = pl->pc[plcnt].art;
				tagalbum = pl->pc[plcnt].alb;
			}
			else {
				tagfile = (fnn.GetLength() > 0) ? fnn : ss;
				tagname = tagalbum = _T("");
			}
			endf = 1;
			wav_start();
		}
	}
	else if (mode == -7) { // dsd
		ULONGLONG po;
		dsdload(filen, tagfile, tagname, tagalbum, po, 1);
	}
	else if (mode == -8) { // flac
		CString ss;
		char buf[1024];
		ss = "";
		ZeroMemory(&sikpi, sizeof(sikpi));
		sikpi.dwSamplesPerSec = savedata.samples; sikpi.dwChannels = 8; sikpi.dwSeekable = 1; sikpi.dwLength = -1; sikpi.dwBitsPerSample = ((savedata.bit24 == 1) ? 24 : 16);
		if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit_sample_Hz;


		if (1) {
			if (ss == "") {
				flacmode = 0;
				CFile pp; pp.Open(filen, CFile::shareDenyWrite | CFile::modeRead | CFile::shareDenyWrite);
				BYTE a; pp.Read(&a, 1); pp.Close();
				if (a == 0xBF) flacmode = 1;
#if UNICODE
				TCHAR* f = filen.GetBuffer();
				kmp = flac_.Open(f, &sikpi);
				filen.ReleaseBuffer();
#else
				kmp = flac_.Open(filen, &sikpi);
#endif
				if (kmp == NULL) { m_saisai.EnableWindow(TRUE); endflg = 0; return; }
				g_openDecoderMode = -8;
			}
			else {
			}
		}
		wavbit_sample_Hz = sikpi.dwSamplesPerSec;
		wavchannel = sikpi.dwChannels;
		wavsam_depth = sikpi.dwBitsPerSample;
		NormalizePlaybackWaveFormat();
		loop1 = 0;
		if (sikpi.dwLength == (DWORD)-1 || wavbit_sample_Hz <= 0)
			loop2 = 0;
		else
			loop2 = (int)((double)(DWORD)sikpi.dwLength * (double)wavbit_sample_Hz / 1000.0 + 0.5);
		{
			const int bps = (wavsam_depth >= 8) ? (wavsam_depth / 8) : 2;
			const __int64 bytesTotal = (__int64)loop2 * (__int64)wavchannel * (__int64)bps;
			if (bytesTotal > 0 && bytesTotal < (__int64)0x7fffffff)
				data_size = oggsize = (int)bytesTotal;
			else
				data_size = oggsize = (loop2 > 0 && wavchannel > 0) ? (loop2 * wavchannel * (bps > 0 ? bps : 2)) : 0;
		}
		CString s; s.Format(L"%d", oggsize);
		//AfxMessageBox(s);
		si1.dwSamplesPerSec = wavbit_sample_Hz;
		si1.dwChannels = wavchannel;
		si1.dwBitsPerSample = wavsam_depth;
		m_time.SetRange(0, (loop2 > 0) ? loop2 : 1, TRUE);
		flac_.SetPosition(kmp, 0);
		kbps = 0;
		CFile ff;
		ff.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL);
		int flg, read = ff.Read(bufimage, sizeof(bufimage));
		ff.Close();

		if (bufimage[0] == 0xBF) {
			CFile fff;
			if (fff.Open(filen, CFile::shareDenyWrite | CFile::modeRead | CFile::shareDenyWrite)) {
				fff.Seek(fff.GetLength() - 8, CFile::begin);
				struct data {
					int l1;
					int l2;
				};
				data d;
				fff.Read(&d, sizeof(d));
				loop1 = d.l1;
				loop2 = d.l2;
				fff.Close();
			}
		}

		if (bufimage[0] == 0xBF) {
			BYTE offenc[7] = { 0xd9,0x3F,0x86,0x7B,0xC7,0x61,0xaa };
			int off = 0;
			for (int ll = 0; ll < sizeof(bufimage); ll++) {
				bufimage[ll] ^= offenc[off];
				off++; off %= 7;
			}
		}
		tagfile = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		flg = 0;
		int i = 0, j;
		for (j = i; j < read - 6; j++) {
			if (bufimage[j] == 'A' && bufimage[j + 1] == 'L' && bufimage[j + 2] == 'B' && bufimage[j + 3] == 'U' && bufimage[j + 4] == 'M' && bufimage[j + 5] == '=') {
				j += 6;
				for (int k = j; k < read - 4; k++) {
					if (bufimage[k] == 0) {
						flg = 1;
						buf[k - j] = 0;
						buf[k - j + 1] = 0;
						buf[k - j + 2] = 0;
						break;
					}
					buf[k - j] = bufimage[k];
				}
			}
			if (flg == 1) {
				const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
				TCHAR* buff = new TCHAR[wlen + 1];
				if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
				{
					buff[wlen] = 0;
				}
				tagalbum = buff;
				delete[] buff;
				flg = 0;
				break;
			}
		}
		for (j = i; j < read - 6; j++) {
			if ((bufimage[j] == 'A' || bufimage[j] == 'a') && bufimage[j + 1] == 'l' && bufimage[j + 2] == 'b' && bufimage[j + 3] == 'u' && bufimage[j + 4] == 'm' && bufimage[j + 5] == '=') {
				j += 6;
				for (int k = j; k < read - 4; k++) {
					if (bufimage[k] == 0) {
						flg = 1;
						buf[k - j] = 0;
						buf[k - j + 1] = 0;
						buf[k - j + 2] = 0;
						break;
					}
					buf[k - j] = bufimage[k];
				}
			}
			if (flg == 1) {
				const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
				TCHAR* buff = new TCHAR[wlen + 1];
				if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
				{
					buff[wlen] = 0;
				}
				tagalbum = buff;
				delete[] buff;
				flg = 0;
				break;
			}
		}
		for (j = i; j < read - 6; j++) {
			if (bufimage[j] == 'A' && bufimage[j + 1] == 'R' && bufimage[j + 2] == 'T' && bufimage[j + 3] == 'I' && bufimage[j + 4] == 'S' && bufimage[j + 5] == 'T' && bufimage[j + 6] == '=') {
				j += 7;
				for (int k = j; k < read - 4; k++) {
					if (bufimage[k] == 0) {
						flg = 1;
						buf[k - j] = 0;
						buf[k - j + 1] = 0;
						buf[k - j + 2] = 0;
						break;
					}
					buf[k - j] = bufimage[k];
				}
			}
			if (flg == 1) {
				const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
				TCHAR* buff = new TCHAR[wlen + 1];
				if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
				{
					buff[wlen] = _T('\0');
				}
				tagname = buff;
				delete[] buff;
				flg = 0;
				break;
			}
		}
		for (j = i; j < read - 6; j++) {
			if ((bufimage[j] == 'A' || bufimage[j] == 'a') && bufimage[j + 1] == 'r' && bufimage[j + 2] == 't' && bufimage[j + 3] == 'i' && bufimage[j + 4] == 's' && bufimage[j + 5] == 't' && bufimage[j + 6] == '=') {
				j += 7;
				for (int k = j; k < read - 4; k++) {
					if (bufimage[k] == 0) {
						flg = 1;
						buf[k - j] = 0;
						buf[k - j + 1] = 0;
						buf[k - j + 2] = 0;
						break;
					}
					buf[k - j] = bufimage[k];
				}
			}
			if (flg == 1) {
				const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
				TCHAR* buff = new TCHAR[wlen + 1];
				if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
				{
					buff[wlen] = _T('\0');
				}
				tagname = buff;
				delete[] buff;
				flg = 0;
				break;
			}
		}
		for (j = i; j < read - 4; j++) {
			if (bufimage[j] == 'T' && bufimage[j + 1] == 'I' && bufimage[j + 2] == 'T' && bufimage[j + 3] == 'L' && bufimage[j + 4] == 'E' && bufimage[j + 5] == '=') {
				j += 6;
				for (int k = j; k < read - 4; k++) {
					if (bufimage[k] == 0) {
						flg = 1;
						buf[k - j] = 0;
						buf[k - j + 1] = 0;
						buf[k - j + 2] = 0;
						break;
					}
					buf[k - j] = bufimage[k];
				}
			}
			if (flg == 1) {
				const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
				TCHAR* buff = new TCHAR[wlen + 1];
				if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
				{
					buff[wlen] = _T('\0');
				}
				tagfile = buff;
				delete[] buff;
				flg = 0;
				break;
			}
		}
		for (j = i; j < read - 4; j++) {
			if ((bufimage[j] == 'T' || bufimage[j] == 't') && bufimage[j + 1] == 'i' && bufimage[j + 2] == 't' && bufimage[j + 3] == 'l' && bufimage[j + 4] == 'e' && bufimage[j + 5] == '=') {
				j += 6;
				for (int k = j; k < read - 4; k++) {
					if (bufimage[k] == 0) {
						flg = 1;
						buf[k - j] = 0;
						buf[k - j + 1] = 0;
						buf[k - j + 2] = 0;
						break;
					}
					buf[k - j] = bufimage[k];
				}
			}
			if (flg == 1) {
				const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
				TCHAR* buff = new TCHAR[wlen + 1];
				if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
				{
					buff[wlen] = _T('\0');
				}
				tagfile = buff;
				delete[] buff;
				flg = 0;
				break;
			}
		}

		for (i = 0; i < 0x300000; i++) {// 00 06 5D 6A 64 61 74 61
			if (bufimage[i] == 'i' && bufimage[i + 1] == 'm' && bufimage[i + 2] == 'a' && bufimage[i + 3] == 'g' && bufimage[i + 4] == 'e' && bufimage[i + 5] == '/' && bufimage[i + 6] == 'j' && bufimage[i + 7] == 'p' && bufimage[i + 8] == 'e' && bufimage[i + 9] == 'g') {
				break;
			}
			if (bufimage[i] == 'i' && bufimage[i + 1] == 'm' && bufimage[i + 2] == 'a' && bufimage[i + 3] == 'g' && bufimage[i + 4] == 'e' && bufimage[i + 5] == '/' && bufimage[i + 6] == 'p' && bufimage[i + 7] == 'n' && bufimage[i + 8] == 'g') {
				break;
			}
		}
		if (i != 0x300000) {
			m_mp3jake.EnableWindow(TRUE);
		}
		wav_start();
	}
	else if (mode == -9) { // M4a
		CString ss;
		char buf[1024];
		ss = "";
		ZeroMemory(&sikpi, sizeof(sikpi));
		sikpi.dwSamplesPerSec = savedata.samples; sikpi.dwChannels = 2; sikpi.dwSeekable = 1; sikpi.dwLength = -1; sikpi.dwBitsPerSample = ((savedata.bit24 == 1) ? 24 : 16);
		if (savedata.bit32 == 1)sikpi.dwBitsPerSample = 16;
		if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit_sample_Hz;

		if (1) {
			if (ss == "") {
#if UNICODE
				TCHAR* f = filen.GetBuffer();
				kmp = m4a_.Open(f, &sikpi);
				filen.ReleaseBuffer();
#else
				kmp = m4a_.Open(filen, &sikpi);
#endif
				if (kmp == NULL) { m_saisai.EnableWindow(TRUE); endflg = 0; return; }
				g_openDecoderMode = -9;
			}
			else {
			}
		}
		wavsam_depth = sikpi.dwBitsPerSample;
		wavbit_sample_Hz = sikpi.dwSamplesPerSec;	wavchannel = sikpi.dwChannels;	loop1 = 0; oggsize = loop2 = (int)((float)sikpi.dwLength / (wavsam_depth / 4) /*/ (float)1000.0f* (float)sikpi.dwSamplesPerSec*/);
		CString s_; s_.Format(L"%d", wavbit_sample_Hz);
		si1.dwSamplesPerSec = sikpi.dwSamplesPerSec;
		si1.dwChannels = wavchannel;
		si1.dwBitsPerSample = wavsam_depth;
		Vbr = 1;
		if (sikpi.dwLength == (DWORD)-1) loop2 = 0;
		data_size = oggsize = loop2 * (wavsam_depth / 4);
		m_time.SetRange(0, (data_size) / (wavsam_depth / 4), TRUE);
		m4a_.SetPosition(kmp, 0);
		kbps = 0;
		CFile ff;
		ff.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL);
		int flg, read = ff.Read(bufimage, 4); read = sizeof(bufimage);
		tagfile = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		flg = 0;
		int i;
		for (;;) {
			if (read != sizeof(bufimage)) {
				break;
			}
			else {
				ff.Seek(-4, CFile::current);
				read = ff.Read(bufimage, sizeof(bufimage));
				for (i = 0; i < read - 4; i++) {
					if (bufimage[i] == 'u' && bufimage[i + 1] == 'd' && bufimage[i + 2] == 't' && bufimage[i + 3] == 'a') {
						int j;
						for (j = i + 4; j < read - 4; j++) {
							if (bufimage[j] == 'a' && bufimage[j + 1] == 'l' && bufimage[j + 2] == 'b' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
								j += 19;
								for (int k = j; k < read - 4; k++) {
									if (bufimage[k] == 0) {
										flg = 1;
										buf[k - j] = 0;
										buf[k - j + 1] = 0;
										buf[k - j + 2] = 0;
										break;
									}
									buf[k - j] = bufimage[k];
								}
							}
							if (flg == 1) {
								const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
								TCHAR* buff = new TCHAR[wlen + 1];
								if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
								{
									buff[wlen] = _T('\0');
								}
								tagalbum = buff;
								delete[] buff;
								flg = 0;
								break;
							}
						}
						for (j = i + 4; j < read - 4; j++) {
							if (bufimage[j] == 'A' && bufimage[j + 1] == 'R' && bufimage[j + 2] == 'T' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
								j += 19;
								for (int k = j; k < read - 4; k++) {
									if (bufimage[k] == 0) {
										flg = 1;
										buf[k - j] = 0;
										buf[k - j + 1] = 0;
										buf[k - j + 2] = 0;
										break;
									}
									buf[k - j] = bufimage[k];
								}
							}
							if (flg == 1) {
								const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
								TCHAR* buff = new TCHAR[wlen + 1];
								if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
								{
									buff[wlen] = _T('\0');
								}
								tagname = buff;
								delete[] buff;
								flg = 0;
								break;
							}
						}
						for (j = i + 4; j < read - 4; j++) {
							if (bufimage[j] == 'n' && bufimage[j + 1] == 'a' && bufimage[j + 2] == 'm' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
								j += 19;
								for (int k = j; k < read - 4; k++) {
									if (bufimage[k] == 0) {
										flg = 1;
										buf[k - j] = 0;
										buf[k - j + 1] = 0;
										buf[k - j + 2] = 0;
										break;
									}
									buf[k - j] = bufimage[k];
								}
							}
							if (flg == 1) {
								const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
								TCHAR* buff = new TCHAR[wlen + 1];
								if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
								{
									buff[wlen] = _T('\0');
								}
								tagfile = buff;
								delete[] buff;
								flg = 0;
								break;
							}
						}
					}
				}
			}
		}
		for (i = 0; i < sizeof(bufimage); i++) {// 00 06 5D 6A 64 61 74 61
			if (bufimage[i] == 0x63 && bufimage[i + 1] == 0x6f && bufimage[i + 2] == 0x76 && bufimage[i + 3] == 0x72 && bufimage[i + 8] == 0x64 && bufimage[i + 9] == 0x61 && bufimage[i + 10] == 0x74 && bufimage[i + 11] == 0x61) {
				break;
			}
		}
		if (i != sizeof(bufimage)) {
			m_mp3jake.EnableWindow(TRUE);
		}
		ff.Close();

		wav_start();

	}
	else if (mode == -3) { // kpi
		ret2 = 0;
		g_kpiRemote = false;
		g_kpiPlaybackArch = ResolveKpiArchBits(CString(kpi), filen);
		ZeroMemory(&g_kpiSession, sizeof(g_kpiSession));
		const WORD km = GetPeMachine(kpi);
		if (km == IMAGE_FILE_MACHINE_AMD64 || km == IMAGE_FILE_MACHINE_ARM64) {
			// x64 KPI は別プロセス(x64ホスト)で開く
			KPI_MEDIAINFO req;
			kpi_InitMediaInfo(&req);
			req.dwSampleRate = savedata.samples;
			// ビット深度はプラグイン選択に任せる(0=制約なし)。
			// 返却された mediaInfo.nBitsPerSample をそのまま wavsam_src に保持する。
			req.nBitsPerSample = 0;
			req.dwChannels = 0;
			req.dwFormatType = KPI_MEDIAINFO::FORMAT_PCM;
			// 要求ビット数を24/32にするとOpen失敗するプラグインがあるため、ここでは要求しない

			CString ssMedia;
			uint32_t sel = 1;
			SplitKpiSubsongPath(filen, ssMedia, sel);

			if (!g_kpiHost.Open((const wchar_t*)kpi, (const wchar_t*)ssMedia, req, sel, g_kpiSession)) {
				CString depMsg;
				depMsg.Format(LL14(
					L"KPIを開けませんでした。\r\n依存DLLが見つからない可能性があります。\r\n\r\nKPI: %s\r\nログ: %%TEMP%%\\ogg_kpi64_host.log", /* 日本語 */
					L"Could not open KPI.\r\nA required dependent DLL may be missing.\r\n\r\nKPI: %s\r\nLog: %%TEMP%%\\ogg_kpi64_host.log", /* 英語 */
					L"Impossible d'ouvrir le KPI.\r\nUne DLL dépendante requise est peut-être manquante.\r\n\r\nKPI : %s\r\nJournal : %%TEMP%%\\ogg_kpi64_host.log", /* フランス語 */
					L"Impossibile aprire il KPI.\r\nPotrebbe mancare una DLL dipendente richiesta.\r\n\r\nKPI: %s\r\nLog: %%TEMP%%\\ogg_kpi64_host.log", /* イタリア語 */
					L"No se pudo abrir el KPI.\r\nPuede faltar una DLL dependiente requerida.\r\n\r\nKPI: %s\r\nRegistro: %%TEMP%%\\ogg_kpi64_host.log", /* スペイン語 */
					L"KPI를 열 수 없습니다.\r\n필요한 종속 DLL이 없을 수 있습니다.\r\n\r\nKPI: %s\r\n로그: %%TEMP%%\\ogg_kpi64_host.log", /* 韓国語 */
					L"无法打开KPI。\r\n可能缺少必需的依赖DLL。\r\n\r\nKPI: %s\r\n日志: %%TEMP%%\\ogg_kpi64_host.log", /* 中国語 */
					L"تعذر فتح KPI.\r\nقد يكون هناك DLL تابع مطلوب مفقود.\r\n\r\nKPI: %s\r\nالسجل: %%TEMP%%\\ogg_kpi64_host.log", /* アラビア語 */
					L"Не удалось открыть KPI.\r\nВозможно, отсутствует требуемая зависимая DLL.\r\n\r\nKPI: %s\r\nЛог: %%TEMP%%\\ogg_kpi64_host.log", /* ロシア語 */
					L"KPI konnte nicht geöffnet werden.\r\nMöglicherweise fehlt eine erforderliche abhängige DLL.\r\n\r\nKPI: %s\r\nLog: %%TEMP%%\\ogg_kpi64_host.log", /* ドイツ語 */
					L"Não foi possível abrir o KPI.\r\nPode faltar uma DLL dependente necessária.\r\n\r\nKPI: %s\r\nLog: %%TEMP%%\\ogg_kpi64_host.log", /* ポルトガル語 */
					L"Kan KPI niet openen.\r\nMogelijk ontbreekt een vereiste afhankelijke DLL.\r\n\r\nKPI: %s\r\nLog: %%TEMP%%\\ogg_kpi64_host.log", /* オランダ語 */
					L"Nie można otworzyć KPI.\r\nMoże brakować wymaganej zależnej biblioteki DLL.\r\n\r\nKPI: %s\r\nDziennik: %%TEMP%%\\ogg_kpi64_host.log", /* ポーランド語 */
					L"KPI açılamadı.\r\nGerekli bir bağımlı DLL eksik olabilir.\r\n\r\nKPI: %s\r\nGünlük: %%TEMP%%\\ogg_kpi64_host.log"), /* トルコ語 */
					(const wchar_t*)kpi);
				MessageBox(depMsg, LL14(
					L"KPI読み込みエラー",
					L"KPI Load Error",
					L"Erreur de chargement KPI",
					L"Errore caricamento KPI",
					L"Error al cargar KPI",
					L"KPI 로드 오류",
					L"KPI加载错误",
					L"خطأ تحميل KPI",
					L"Ошибка загрузки KPI",
					L"KPI-Ladefehler",
					L"Erro de carregamento do KPI",
					L"KPI-laadfout",
					L"Błąd ładowania KPI",
					L"KPI Yükleme Hatası"),
					MB_ICONERROR | MB_OK);
				m_saisai.EnableWindow(TRUE); endflg = 0; return;
			}

			g_kpiRemote = true;
			g_kpiPlaybackArch = 64;
			ResetKpiRemoteCache();
			wavbit_sample_Hz = g_kpiSession.mediaInfo.dwSampleRate;
			wavchannel = g_kpiSession.mediaInfo.dwChannels;
			wavsam_src = g_kpiSession.mediaInfo.nBitsPerSample;
			g_kpiSourceBitsPerSample = wavsam_src;
			// DS生成/再生系は整数PCM前提。元フォーマットは wavsam_src 側で保持する。
			wavsam_depth = (wavsam_src < 0) ? 16 : wavsam_src;
			NormalizePlaybackWaveFormat();
			loop1 = 0;
			loop2 = (int)kpi_100nsToSample(g_kpiSession.mediaInfo.qwLength, g_kpiSession.mediaInfo.dwSampleRate);
			if (g_kpiSession.mediaInfo.qwLength == (UINT64)-1) loop2 = 0;
			data_size = oggsize = loop2 * (wavsam_depth / 4);
			m_time.SetRange(0, (data_size) / (wavsam_depth / 4), TRUE);
			uint64_t np = 0;
			g_kpiHost.Seek(g_kpiSession.sessionId, 0, 0, np);
			wav_start();
			// 以降の共通処理(UI更新/画像抽出など)も実行させる
		}
		else {
			if (!g_kpiPlaybackArch) {
				if (km == IMAGE_FILE_MACHINE_I386)
					g_kpiPlaybackArch = 32;
				else
					g_kpiPlaybackArch = ResolveKpiArchBits(CString(kpi), filen);
			}
			hDLLk = LoadKpiLibraryWithDependencies((const wchar_t*)kpi);
			typedef HRESULT(WINAPI* kpi_CreateInstance)(REFIID riid, void** ppvObject, IKpiUnknown* pUnknown);
			kpi_CreateInstance cr = (kpi_CreateInstance)GetProcAddress(hDLLk, "kpi_CreateInstance");
			pFunck = (pfnGetKMPModule)::GetProcAddress(hDLLk, SZ_KMP_GETMODULE);
			if (kvver == 2) {
				mod = pFunck();
				if (mod == NULL) {
					MessageBox(LL14(
						L"なんらかの要因でkpiが開けませんでした。", /* 日本語 */
						L"Could not open kpi for some reason.", /* 英語 */
						L"Impossible d'ouvrir kpi pour une raison quelconque.", /* フランス語 */
						L"Impossibile aprire kpi per qualche motivo.", /* イタリア語 */
						L"No se pudo abrir kpi por alguna razón.", /* スペイン語 */
						L"어떠한 이유로 kpi를 열 수 없었습니다.", /* 韓国語 */
						L"由于某种原因无法打开kpi。", /* 中国語 */
						L"تعذر فتح kpi لسبب ما.", /* アラビア語 */
						L"Не удалось открыть kpi по какой-то причине.", /* ロシア語 */
						L"kpi konnte aus einem unbekannten Grund nicht geöffnet werden.", /* ドイツ語 */
						L"Não foi possível abrir o kpi por algum motivo.", /* ポルトガル語 */
						L"Kan kpi om een of andere reden niet openen.", /* オランダ語 */
						L"Nie można otworzyć kpi z jakiegoś powodu.", /* ポーランド語 */
						L"Kpi bir nedenle açılamadı."), /* トルコ語 */
						LL14(
							L"ファイルが存在しません。", /* キャプション */
							L"File does not exist.",
							L"Le fichier n'existe pas.",
							L"Il file non esiste.",
							L"El archivo no existe.",
							L"파일이 존재하지 않습니다.",
							L"文件不存在。",
							L"الملف غير موجود.",
							L"Файл не существует.",
							L"Datei existiert nicht.",
							L"O arquivo não existe.",
							L"Het bestand bestaat niet.",
							L"Plik nie istnieje.",
							L"Dosya mevcut değil."));

					fnn = LL14(
						L"kpi構造体を獲得できませんでした。", /* 日本語 */
						L"Could not acquire kpi structure.", /* 英語 */
						L"Impossible d'obtenir la structure kpi.", /* フランス語 */
						L"Impossibile acquisire la struttura kpi.", /* イタリア語 */
						L"No se pudo adquirir la estructura kpi.", /* スペイン語 */
						L"kpi 구조체를 획득할 수 없었습니다.", /* 韓国語 */
						L"无法获取kpi结构体。", /* 中国語 */
						L"تعذر الحصول على هيكل kpi.", /* アラビア語 */
						L"Не удалось получить структуру kpi.", /* ロシア語 */
						L"kpi-Struktur konnte nicht abgerufen werden.", /* ドイツ語 */
						L"Não foi possível obter a estrutura kpi.", /* ポルトガル語 */
						L"Kan kpi-structuur niet verkrijgen.", /* オランダ語 */
						L"Nie można uzyskać struktury kpi.", /* ポーランド語 */
						L"Kpi yapısı edinilemedi."); /* トルコ語 */
					FreeLibrary(hDLLk);
					m_saisai.EnableWindow(TRUE); endflg = 0; return;
				}
				CString ss;
				{
					uint32_t sel = 1;
					SplitKpiSubsongPath(filen, ss, sel);
				}
				ZeroMemory(&sikpi, sizeof(sikpi));
				sikpi.dwSamplesPerSec = savedata.samples; sikpi.dwChannels = 8; sikpi.dwSeekable = 1; sikpi.dwLength = -1; sikpi.dwBitsPerSample = 16;
				if (savedata.bit24 == 1)sikpi.dwBitsPerSample = 24;
				if (savedata.bit32 == 1)sikpi.dwBitsPerSample = 32;
				if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit_sample_Hz;
				if (mod) {
					if (ss == L"") {
						if (mod->Init) mod->Init();
#if UNICODE
						TCHAR* f = filen.GetBuffer();
						char ff[2048];
						WideCharToMultiByte(CP_ACP, 0, f, -1, ff, filen.GetLength() * 2 + 2, 0, 0);
						if (mod->Open) kmp1 = mod->Open(ff, &sikpi);
#else
						if (mod->Open) kmp1 = mod->Open(filen, &sikpi);
#endif
						if (kmp1 == NULL) { m_saisai.EnableWindow(TRUE); endflg = 0; return; }
					}
					else {
						if (mod->Init) mod->Init();
#if UNICODE
						TCHAR* f = ss.GetBuffer();
						char ff[2048];
						WideCharToMultiByte(CP_ACP, 0, f, -1, ff, filen.GetLength() * 2 + 2, 0, 0);
						if (mod->Open) kmp1 = mod->Open(ff, &sikpi);
#else
						if (mod->Open) kmp1 = mod->Open(ss, &sikpi);
#endif
						if (kmp1 == NULL) { m_saisai.EnableWindow(TRUE); endflg = 0; return; }
						if (mod->SetPosition) mod->SetPosition(kmp1, _tstoi(filen.Right(4)) * 1000);
					}
				}
				wavbit_sample_Hz = sikpi.dwSamplesPerSec;	wavchannel = sikpi.dwChannels;	loop1 = 0; loop2 = (int)((double)sikpi.dwLength * (double)sikpi.dwSamplesPerSec / 1000.0);
				wavsam_depth = sikpi.dwBitsPerSample;
			}
			else if (kvver == 5) {
				IUnknown* pMyObject = new CMyHost((const wchar_t*)kpi);
				IKpiDecoderModule* ob = NULL;
				HRESULT hr = SafeCreateDecoderModuleInstance(cr, (void**)&ob, pMyObject);
				if (hr == S_OK) {
					ob5 = (IKpiDecoderModule*)ob;
					kpi_InitMediaInfo(&me5);
					//				me5.cb = sizeof(KPI_MEDIAINFO);
					me5.dwSampleRate = savedata.samples;
					me5.nBitsPerSample = 16;
					if (savedata.bit24 == 1)sikpi.dwBitsPerSample = 24;
					if (savedata.bit32 == 1)sikpi.dwBitsPerSample = 32;
					if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit_sample_Hz;
					IKpiFile* ik;
					IKpiFolder* ik2 = new CMyDummyFolder();
					CMyHostFile* pHostFile = new CMyHostFile();
					{
						uint32_t sel = 1;
						SplitKpiSubsongPath(filen, ss, sel);
					}
					if (ss == L"") {
						if (!pHostFile->Open(filen)) {
							// ファイルが開けない
							pHostFile->Release();
							return;
						}
					}
					else {
						if (!pHostFile->Open(ss)) {
							// ファイルが開けない
							pHostFile->Release();
							return;
						}
					}
					ik = pHostFile;
					ob5->Open(&me5, ik, ik2, &kpidec);
					if (kpidec == NULL) {
						CString depMsg;
						depMsg.Format(LL14(
							L"KPIのOpenに失敗しました。\r\n依存DLLまたは関連ファイル(例: .bin)が不足している可能性があります。\r\n\r\nKPI: %s",
							L"KPI Open failed.\r\nA dependent DLL or related file (e.g. .bin) may be missing.\r\n\r\nKPI: %s",
							L"Échec de l'ouverture du KPI.\r\nUne DLL dépendante ou un fichier associé (ex. .bin) peut être manquant.\r\n\r\nKPI : %s",
							L"Apertura KPI non riuscita.\r\nPotrebbe mancare una DLL dipendente o un file correlato (es. .bin).\r\n\r\nKPI: %s",
							L"Error al abrir KPI.\r\nPuede faltar una DLL dependiente o un archivo relacionado (p. ej. .bin).\r\n\r\nKPI: %s",
							L"KPI Open에 실패했습니다.\r\n종속 DLL 또는 관련 파일(예: .bin)이 없을 수 있습니다.\r\n\r\nKPI: %s",
							L"KPI打开失败。\r\n可能缺少依赖DLL或相关文件（例如 .bin）。\r\n\r\nKPI: %s",
							L"فشل فتح KPI.\r\nقد يكون ملف DLL تابع أو ملف مرتبط (مثل .bin) مفقودًا.\r\n\r\nKPI: %s",
							L"Не удалось выполнить Open KPI.\r\nВозможно, отсутствует зависимая DLL или связанный файл (например, .bin).\r\n\r\nKPI: %s",
							L"KPI-Open fehlgeschlagen.\r\nMöglicherweise fehlt eine abhängige DLL oder eine zugehörige Datei (z. B. .bin).\r\n\r\nKPI: %s",
							L"Falha ao abrir o KPI.\r\nPode faltar uma DLL dependente ou arquivo relacionado (ex.: .bin).\r\n\r\nKPI: %s",
							L"Open van KPI is mislukt.\r\nMogelijk ontbreekt een afhankelijke DLL of gerelateerd bestand (bijv. .bin).\r\n\r\nKPI: %s",
							L"Otwarcie KPI nie powiodło się.\r\nMoże brakować zależnej biblioteki DLL lub powiązanego pliku (np. .bin).\r\n\r\nKPI: %s",
							L"KPI Open başarısız.\r\nBağımlı DLL veya ilgili bir dosya (.bin gibi) eksik olabilir.\r\n\r\nKPI: %s"),
							(const wchar_t*)kpi);
						MessageBox(depMsg, LL14(
							L"KPI読み込みエラー",
							L"KPI Load Error",
							L"Erreur de chargement KPI",
							L"Errore caricamento KPI",
							L"Error al cargar KPI",
							L"KPI 로드 오류",
							L"KPI加载错误",
							L"خطأ تحميل KPI",
							L"Ошибка загрузки KPI",
							L"KPI-Ladefehler",
							L"Erro de carregamento do KPI",
							L"KPI-laadfout",
							L"Błąd ładowania KPI",
							L"KPI Yükleme Hatası"),
							MB_ICONERROR | MB_OK);
						return;
					}
					int sel = 1;
					if (ss != L"")sel = (_tstoi(filen.Right(4)));
					if (sel <= 0) sel = 1;
					DWORD dwSelectedSong = SafeKpiDecoderSelect(
						kpidec,
						sel,
						&pMediaInfo
					);
					if (ik2) {
						ik2->Release();
					}
					if (ik) { // ik (pHostFile) も解放
						ik->Release();
					}
					if (pMediaInfo == NULL) return;
					wavbit_sample_Hz = pMediaInfo->dwSampleRate;	wavchannel = pMediaInfo->dwChannels;	loop1 = 0; loop2 = kpi_100nsToSample(pMediaInfo->qwLength, pMediaInfo->dwSampleRate);;
					wavsam_src = pMediaInfo->nBitsPerSample;
					g_kpiSourceBitsPerSample = wavsam_src;
					// DS生成/再生系は整数PCM前提。元フォーマットは wavsam_src 側で保持する。
					wavsam_depth = (wavsam_src < 0) ? 16 : wavsam_src;
					NormalizePlaybackWaveFormat();
				}
			}

			if (sikpi.dwLength == (DWORD)-1) loop2 = 0;
			data_size = oggsize = loop2 * (wavsam_depth / 4);
			m_time.SetRange(0, (data_size) / (wavsam_depth / 4), TRUE);
			if (kvver == 2 && mod->SetPosition) mod->SetPosition(kmp1, 0);
			if (kvver == 5) kpidec->Seek(0, 0);
			wav_start();
		}
		CFile ff;
		CString ss11 = filen; ss11.MakeLower();
		if (ss11.Right(3) == "m4a") {
			if (ff.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
				mp3file = filen;
				ZeroMemory(bufimage, sizeof(bufimage));
				int i;
				ff.Read(bufimage, sizeof(bufimage));
				for (i = 0; i < 0x300000; i++) {// 00 06 5D 6A 64 61 74 61
					if (bufimage[i] == 0x63 && bufimage[i + 1] == 0x6f && bufimage[i + 2] == 0x76 && bufimage[i + 3] == 0x72 && bufimage[i + 8] == 0x64 && bufimage[i + 9] == 0x61 && bufimage[i + 10] == 0x74 && bufimage[i + 11] == 0x61) {
						break;
					}
				}
				if (i != 0x300000) {
					m_mp3jake.EnableWindow(TRUE);
				}
			}ff.Close();
		}
		if (ss11.Right(4) == "flac") {
			if (ff.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
				mp3file = filen;
				ZeroMemory(bufimage, sizeof(bufimage));
				int i;
				ff.Read(bufimage, sizeof(bufimage));
				for (i = 0; i < 0x300000; i++) {// 00 06 5D 6A 64 61 74 61
					if (bufimage[i] == 'i' && bufimage[i + 1] == 'm' && bufimage[i + 2] == 'a' && bufimage[i + 3] == 'g' && bufimage[i + 4] == 'e' && bufimage[i + 5] == '/' && bufimage[i + 6] == 'j' && bufimage[i + 7] == 'p' && bufimage[i + 8] == 'e' && bufimage[i + 9] == 'g') {
						break;
					}
					if (bufimage[i] == 'i' && bufimage[i + 1] == 'm' && bufimage[i + 2] == 'a' && bufimage[i + 3] == 'g' && bufimage[i + 4] == 'e' && bufimage[i + 5] == '/' && bufimage[i + 6] == 'p' && bufimage[i + 7] == 'n' && bufimage[i + 8] == 'g') {
						break;
					}
				}
				if (i != 0x300000) {
					m_mp3jake.EnableWindow(TRUE);
				}
			}ff.Close();
		}
	}
	else if (mode == -2) {
		CFile ff;
		CString ss11 = ss; ss11.MakeLower();
		if (ss11.Right(3) == "m4a") {
			if (ff.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
				mp3file = ss;
				ZeroMemory(bufimage, sizeof(bufimage));
				int i;
				ff.Read(bufimage, sizeof(bufimage));
				for (i = 0; i < 0x300000; i++) {// 00 06 5D 6A 64 61 74 61
					if (bufimage[i] == 0x63 && bufimage[i + 1] == 0x6f && bufimage[i + 2] == 0x76 && bufimage[i + 3] == 0x72 && bufimage[i + 8] == 0x64 && bufimage[i + 9] == 0x61 && bufimage[i + 10] == 0x74 && bufimage[i + 11] == 0x61) {
						break;
					}
				}
				if (i != 0x300000) {
					m_mp3jake.EnableWindow(TRUE);
				}
			}ff.Close();
		}
	}
	//	else
	//		playwavds2(bufwav3,0,dwDataLen*4,0);

	//lrc
	lrcnum = 0;
	for (int m = 0; m < 300; m++) {
		lrc[m] = L"";
		lrctm[m] = 0;
	}
	int gg = filen.GetLength() * 2;
	int gg2 = filen.ReverseFind(L'.');
	CString filenll = filen.Left(gg2) + L".lrc";
	if (PathFileExistsW(filenll)) {
		lrc[lrcnum] = L"...";
		lrctm[lrcnum] = 0;
		lrcnum++;

		CStdioFile a(_tfopen(filenll, _T("r, ccs=UTF-8")));
		CString buf;
		while (a.ReadString(buf)) {
			if (buf.IsEmpty()) continue;

			// 1. 一番最後の ']' を探して、それより後ろを歌詞テキストとする
			int lastBracket = buf.ReverseFind(']');
			CString text = _T("");
			if (lastBracket != -1) {
				text = buf.Mid(lastBracket + 1);
			}
			else {
				continue; // タグがない行はスキップ
			}

			// 2. 先頭から順に '[' ... ']' を探して時間を取得
			int curPos = 0;
			while (curPos < lastBracket) {
				int startBracket = buf.Find('[', curPos);
				int endBracket = buf.Find(']', curPos);

				if (startBracket != -1 && endBracket != -1 && endBracket > startBracket) {
					// 時間解析 [mm:ss.xx]
					int mm = _wtoi(buf.Mid(startBracket + 1, 2));
					int ss = _wtoi(buf.Mid(startBracket + 4, 2));
					int xx = _wtoi(buf.Mid(startBracket + 7, 2));

					// 配列に追加
					lrc[lrcnum] = text;
					lrctm[lrcnum] = mm * 60 * 100 + ss * 100 + xx;
					lrcnum++;

					curPos = endBracket + 1;
				}
				else {
					break;
				}
			}
		}
		a.Close();

		// ★追加: 時間順にソート (バブルソート)
		// 複数タグによって順番が前後している可能性があるため整列させます
		for (int i = 0; i < lrcnum - 1; i++) {
			for (int j = 0; j < lrcnum - i - 1; j++) {
				if (lrctm[j] > lrctm[j + 1]) {
					// 時間の入れ替え
					int tempTm = lrctm[j];
					lrctm[j] = lrctm[j + 1];
					lrctm[j + 1] = tempTm;
					// 歌詞の入れ替え
					CString tempTxt = lrc[j];
					lrc[j] = lrc[j + 1];
					lrc[j + 1] = tempTxt;
				}
			}
		}

		// 番兵追加 (ソートの後に追加)
		lrctm[lrcnum] = 99 * 60 * 100 + 99 * 100 * 99;
		lrcnum++;
	}
	else
		//ネットを見るかどうか
		if (savedata.lrc_net && (filen.Right(4).MakeLower() == L".mp3" || filen.Right(4).MakeLower() == L".mp2" || filen.Right(4).MakeLower() == L".mp1" || filen.Right(4).MakeLower() == L".rmp"
			|| filen.Right(4).MakeLower() == L".m4a" || filen.Right(4).MakeLower() == L".aac" || filen.Right(5).MakeLower() == L".flac" || filen.Right(4).MakeLower() == L".tta" || filen.Right(4).MakeLower() == L".ape"
			|| filen.Right(4).MakeLower() == L".dsf" || filen.Right(4).MakeLower() == L".dff" || filen.Right(4).MakeLower() == L".wav" || filen.Right(4).MakeLower() == L".ogg" || filen.Right(5).MakeLower() == L".opus")) {
			double wavv[] = { 0,1.0,2.0,3.0 / 0.75,4.0 / 0.75,5.0 / 0.75,6.0 / 0.75 };//(double)(wavbit2/wavv[wavchannel])
			double wavv2[] = { 0,2.0,1.0,2.0 / 3.0,2.0 / 4.0,2.0 / 5.0,2.0 / 6.0 };//(double)(wavbit2/wavv[wavchannel])
			double t3;
			if (mode == -10)
				t3 = (double)oggsize / (double)wavbit_sample_Hz;
			else {
				t3 = (double)oggsize / (double)(wavbit_sample_Hz * 2.0 * wavv[wavchannel]) / (double)(wavsam_depth / 16.0f);
			}
			if ((mode == -9) && wavchannel > 2) t3 *= wavchannel / 2.0;
			t3 *= 1000.0; int tt = (int)t3;
			CLyricsProgressWnd* pProgressWnd = new CLyricsProgressWnd();
			pProgressWnd->Create(AfxGetMainWnd()); // 親ウィンドウを指定
			pProgressWnd->Show();
			CString lrcFilePath = GetLrcFromAPI(tagfile, tagalbum, tagname, tt);
			pProgressWnd->Hide();
			delete pProgressWnd;
			if (!lrcFilePath.IsEmpty())
			{
				lrc[lrcnum] = L"...";
				lrctm[lrcnum] = 0;
				lrcnum++;
				CStdioFile a(_tfopen(lrcFilePath, _T("r, ccs=UTF-8")));
				CString buf;
				while (a.ReadString(buf)) {
					if (buf.IsEmpty()) continue;

					// 1. 一番最後の ']' を探して、それより後ろを歌詞テキストとする
					int lastBracket = buf.ReverseFind(']');
					CString text = _T("");
					if (lastBracket != -1) {
						text = buf.Mid(lastBracket + 1);
					}
					else {
						continue;
					}

					// 2. 先頭から順に '[' ... ']' を探して時間を取得
					int curPos = 0;
					while (curPos < lastBracket) {
						int startBracket = buf.Find('[', curPos);
						int endBracket = buf.Find(']', curPos);

						if (startBracket != -1 && endBracket != -1 && endBracket > startBracket) {
							int mm = _wtoi(buf.Mid(startBracket + 1, 2));
							int ss = _wtoi(buf.Mid(startBracket + 4, 2));
							int xx = _wtoi(buf.Mid(startBracket + 7, 2));

							lrc[lrcnum] = text;
							lrctm[lrcnum] = mm * 60 * 100 + ss * 100 + xx;
							lrcnum++;

							curPos = endBracket + 1;
						}
						else {
							break;
						}
					}
				}
				a.Close();

				// ★追加: 時間順にソート (バブルソート)
				for (int i = 0; i < lrcnum - 1; i++) {
					for (int j = 0; j < lrcnum - i - 1; j++) {
						if (lrctm[j] > lrctm[j + 1]) {
							// 時間の入れ替え
							int tempTm = lrctm[j];
							lrctm[j] = lrctm[j + 1];
							lrctm[j + 1] = tempTm;
							// 歌詞の入れ替え
							CString tempTxt = lrc[j];
							lrc[j] = lrc[j + 1];
							lrc[j + 1] = tempTxt;
						}
					}
				}

				// 番兵追加
				lrctm[lrcnum] = 99 * 60 * 100 + 99 * 100 * 99;
				lrcnum++;
			}
		}

	if (mode == 21 || mode == 30) {
		wavbit_sample_Hz = 48000;
	}
	if (mode == -1) {
		// 零の軌跡用
		CString ss, sss;
		ss = filen.Right(filen.GetLength() - filen.ReverseFind(L'\\') - 1);
		sss = filen.Left(filen.ReverseFind('\\'));
		int fg = 0;
		CFile ffff; if (ffff.Open(sss + L"\\..\\text\\t_bgm._dt", CFile::modeRead | CFile::shareDenyWrite)) { fg = 1; ffff.Close(); }
		if (ss.Mid(0, 3) == L"ed7" && fg == 1) {
			CString a;
			switch (_ttoi(ss.Mid(2, 4))) {
			case 7001:
				a = LL14(L"零の軌跡", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"영의 궤적", L"零之轨迹", L"مسارات من الصفر", L"Тропы от Нуля", L"Trails from Zero", L"Trilhas do Zero", L"Trails from Zero", L"Trails from Zero", L"Sıfırdan İzler");
				break;
			case 7002:
				a = L"way of live -Opening Version-";
				break;
			case 7003:
				a = LL14(L"新しき日々〜予兆", L"New Days -Omen-", L"Nouveaux Jours -Présage-", L"Nuovi Giorni -Presagio-", L"Nuevos Días -Presagio-", L"새로운 날들 ~예조", L"更新之日~预兆", L"أيام جديدة -بشرى-", L"Новые Дни -Предзнаменование-", L"Neue Tage -Omen-", L"Novos Dias -Presságio-", L"Nieuwe Dagen -Voorteken-", L"Nowe Dni -Omen-", L"Yeni Günler -İşaret-");
				break;
			case 7005:
				a = LL14(L"想い破れて・・・", L"Broken Heart...", L"Cœur Brisé...", L"Cuore Spezzato...", L"Corazón Roto...", L"부서진 마음...", L"心意破碎...", L"قلب مكسور...", L"Разбитое Сердце...", L"Gebrochenes Herz...", L"Coração Partido...", L"Gebroken Hart...", L"Złamane Serce...", L"Kırık Kalp...");
				break;
			case 7052:
				a = LL14(L"碧い軌跡 -Opening size-", L"Azure Arbitrator -Opening size-", L"Arbitre Azur -taille ouverture-", L"Arbitro Azzurro -dimensione apertura-", L"Árbitro Azur -tamaño apertura-", L"벽의 궤적 -Opening size-", L"碧之轨迹 -片头版-", L"المحكم الأزرق -حجم الافتتاحية-", L"Лазурный Арбитр -размер открытия-", L"Azur-Schiedsrichter -Eröffnungsgröße-", L"Árbitro Azul -tamanho abertura-", L"Azure Scheidsrechter -openingsgrootte-", L"Lazurowy Arbitr -rozmiar otwierający-", L"Gök Mavisi Hakem -açılış boyutu-");
				break;
			case 7053:
				a = LL14(L"それでも僕らは。", L"Yet We're Still Here.", L"Pourtant Nous Sommes Là.", L"Eppure Siamo Ancora Qui.", L"Pero Seguimos Aquí.", L"그래도 우리는.", L"即便如此我们仍在。", L"ومع ذلك نحن هنا.", L"Но Мы Всё Ещё Здесь.", L"Dennoch Sind Wir Noch Hier.", L"Mas Ainda Estamos Aqui.", L"Toch Zijn We Er Nog.", L"A Jednak Nadal Tu Jesteśmy.", L"Yine de Buradayız.");
				break;
			case 7100:
				a = LL14(L"街角の風景", L"Street Corner Scenery", L"Scène de Rue", L"Scena Angolo Strada", L"Paisaje de Esquina", L"길모퉁이 풍경", L"街角风景", L"مشهد زاوية الشارع", L"Вид Угола Улицы", L"Straßenecken-Szenerie", L"Cenário da Esquina", L"Straathoekscene", L"Scena Ulicznego Rogu", L"Sokak Köşesi Manzarası");
				break;
			case 7101:
				a = LL14(L"明日は明日の風が吹く", L"Tomorrow the Wind Will Blow", L"Demain le Vent Soufflera", L"Domani Soffierà il Vento", L"Mañana Soplara el Viento", L"내일은 내일의 바람이 분다", L"明日自有明日风", L"غداً تهب الرياح", L"Завтра Подует Ветер", L"Morgen Wird der Wind Wehen", L"Amanhã o Vento Soprará", L"Morgen Zal de Wind Waaien", L"Jutro Zawieje Wiatr", L"Yarın Rüzgar Esecek");
				break;
			case 7102:
				a = LL14(L"クロスベルの午後", L"Afternoon in Crossbell", L"Après-midi à Crossbell", L"Pomeriggio a Crossbell", L"Tarde en Crossbell", L"크로스벨의 오후", L"克洛斯贝尔的午后", L"وقت الظهيرة في كروسبيل", L"Послеполуденное время в Кроссбелле", L"Nachmittag in Crossbell", L"Tarde em Crossbell", L"Middag in Crossbell", L"Popołudnie w Crossbell", L"Crossbell'de Öğleden Sonra");
				break;
			case 7103:
				a = L"During Mission Accomplishment";
				break;
			case 7104:
				a = LL14(L"創立記念祭", L"Founding Festival", L"Fête de Fondation", L"Festival della Fondazione", L"Festival Fundacional", L"창립 기념제", L"创立纪念祭", L"مهرجان التأسيس", L"Праздник Основания", L"Gründungsfest", L"Festival da Fundação", L"Stichtingsfeest", L"Święto Założenia", L"Kuruluş Festivali");
				break;
			case 7105:
				a = LL14(L"降水確率10%", L"10% Chance of Rain", L"10% de chances de pluie", L"10% di probabilità di pioggia", L"10% de probabilidad de lluvia", L"강수확률 10%", L"降水概率10%", L"10% فرصة هطول", L"10% Вероятность Дождя", L"10% Regenwahrscheinlichkeit", L"10% de chance de chuva", L"10% kans op regen", L"10% szans na deszcz", L"%10 yağmur ihtimali");
				break;
			case 7106:
				a = LL14(L"風船と紙吹雪", L"Balloons and Confetti", L"Ballons et Confettis", L"Palloncini e Coriandoli", L"Globos y Confeti", L"풍선과 종이 꽃가루", L"气球与纸屑", L"بالونات وقصاصات ملونة", L"Воздушные Шары и Конфетти", L"Luftballons und Konfetti", L"Balões e Confete", L"Ballonnen en Confetti", L"Balony i Konfetti", L"Balonlar ve Konfeti");
				break;
			case 7110:
				a = LL14(L"特務支援課", L"Special Support Section", L"Section Soutien Spécial", L"Sezione Supporto Speciale", L"Sección de Apoyo Especial", L"특무지원과", L"特务支援科", L"قسم الدعم الخاص", L"Специальная Поддержка", L"Sondereinsatztruppe", L"Seção de Suporte Especial", L"Speciale Ondersteuningssectie", L"Specjalna Sekcja Wsparcia", L"Özel Destek Bölümü");
				break;
			case 7111:
				a = LL14(L"C.S.P.D. -クロスベル警察", L"C.S.P.D. -Crossbell Police", L"C.S.P.D. -Police de Crossbell", L"C.S.P.D. -Polizia Crossbell", L"C.S.P.D. -Policía Crossbell", L"C.S.P.D. -크로스벨 경찰", L"C.S.P.D. -克洛斯贝尔警察", L"C.S.P.D. -شرطة كروسبيل", L"C.S.P.D. -Полиция Кроссбелла", L"C.S.P.D. -Crossbell Polizei", L"C.S.P.D. -Polícia Crossbell", L"C.S.P.D. -Crossbell Politie", L"C.S.P.D. -Policja Crossbell", L"C.S.P.D. -Crossbell Polisi");
				break;
			case 7113:
				a = L"Arc-en-ciel";
				break;
			case 7114:
				a = LL14(L"黒月貿易公司", L"Heiyue Trading Company", L"Compagnie Heiyue", L"Heiyue Trading Company", L"Heiyue Trading Company", L"헤이위에 무역공사", L"黑月贸易公司", L"شركة هييو التجارية", L"Торговая Компания Хэйюэ", L"Heiyue Handelsgesellschaft", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company");
				break;
			case 7116:
				a = L"IGNIS";
				break;
			case 7117:
				a = L"TRINITY";
				break;
			case 7120:
				a = LL14(L"アルモリカ村", L"Armorica Village", L"Village d'Armorica", L"Villaggio Armorica", L"Aldea Armorica", L"아르모리카 마을", L"阿莫利卡村", L"قرية أرموريكا", L"Деревня Арморика", L"Armorica-Dorf", L"Vila Armorica", L"Armorica-dorp", L"Wieś Armorica", L"Armorica Köyü");
				break;
			case 7121:
				a = LL14(L"鉱山町マインツ", L"Mines Town Mainz", L"Ville minière Mainz", L"Città mineraria Mainz", L"Ciudad minera Mainz", L"광산마을 마인츠", L"矿山镇マインツ", L"بلدة المناجم ماينز", L"Город Шахт Майнц", L"Bergarbeiterstadt Mainz", L"Cidade das Minas Mainz", L"Mijnstad Mainz", L"Miasto Kopalni Mainz", L"Mainz Maden Kasabası");
				break;
			case 7122:
				a = L"Killing Bear";
				break;
			case 7123:
				a = LL14(L"聖ウルスラ医科大学", L"St. Ursula Medical College", L"Faculté St-Ursule", L"Collegio medico St. Ursula", L"Universidad Médica St. Ursula", L"성 우르술라 의과대학", L"圣乌尔苏拉医科大学", L"كلية سانت أورسولا الطبية", L"Медколледж св. Урсулы", L"St. Ursula Medizinhochschule", L"Faculdade St. Ursula", L"St. Ursula Medische Hogeschool", L"Szpital św. Urszuli", L"Aziz Ursula Tıp Koleji");
				break;
			case 7124:
				a = LL14(L"クロスベル大聖堂", L"Crossbell Cathedral", L"Cathédrale de Crossbell", L"Cattedrale di Crossbell", L"Catedral de Crossbell", L"크로스벨 대성당", L"克洛斯贝尔大教堂", L"كاتدرائية كروسبيل", L"Собор Кроссбелла", L"Crossbell-Kathedrale", L"Catedral de Crossbell", L"Crossbell Kathedraal", L"Katedra Crossbell", L"Crossbell Katedrali");
				break;
			case 7125:
				a = LL14(L"黒の競売会", L"Black Auction", L"Vente aux enchères noire", L"Asta nera", L"Subasta negra", L"검은 경매회", L"黑色拍卖会", L"المزاد الأسود", L"Чёрный Аукцион", L"Schwarze Auktion", L"Leilão negro", L"Zwarte Veiling", L"Czarna Aukcja", L"Kara Müzayede");
				break;
			case 7126:
				a = LL14(L"大国にはさまれて", L"Caught Between Nations", L"Pris entre les Nations", L"Intrappolati tra le Nazioni", L"Atrapados entre Naciones", L"대국 사이에 끼어", L"夹在大国之间", L"محاصرون بين الأمم", L"Зажатые Между Державами", L"Zwischen den Nationen gefangen", L"Preso entre Nações", L"Gevangen tussen Naties", L"Uwięziony między Mocarstwami", L"Uluslar Arasında Sıkışmış");
				break;
			case 7150:
				a = LL14(L"新たなる日常", L"New Daily Life", L"Nouvelle Vie Quotidienne", L"Nuova Vita Quotidiana", L"Nueva Vida Cotidiana", L"새로운 일상", L"崭新的日常", L"حياة يومية جديدة", L"Новые Будни", L"Neuer Alltag", L"Nova Vida Cotidiana", L"Nieuw Dagelijks Leven", L"Nowe Codzienne Życie", L"Yeni Günlük Yaşam");
				break;
			case 7151:
				a = LL14(L"動き始めた事態", L"Events in Motion", L"Événements en Mouvement", L"Eventi in Movimento", L"Eventos en Movimiento", L"움직이기 시작한 사태", L"开始运作的局面", L"الأحداث في حركة", L"События Приходят в Движение", L"Ereignisse in Bewegung", L"Eventos em Movimento", L"Gebeurtenissen in Beweging", L"Zdarzenia w Ruchu", L"Harekete Geçen Olaylar");
				break;
			case 7160:
				a = LL14(L"ミシュラムワンダーランド", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"미슈람 원더랜드", L"米修拉姆乐园", L"أرض عجائب ميشرام", L"Мишрам Уандерленд", L"Mishyram Wunderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Harikalar Diyarı");
				break;
			case 7161:
				a = LL14(L"束の間の休息", L"Brief Respite", L"Bref Répit", L"Breve Respiro", L"Breve Respiro", L"잠시 동안의 휴식", L"短瞬的休息", L"استراحة قصيرة", L"Краткая Передышка", L"Kurze Verschnaufpause", L"Breve Descanso", L"Kort Respijt", L"Krótki Odpoczynek", L"Kısa Mola");
				break;
			case 7162:
				a = LL14(L"ささやかな晩餐", L"Simple Dinner", L"Dîner Simple", L"Cena Semplice", L"Cena Sencilla", L"소박한 만찬", L"简单的晚餐", L"عشاء بسيط", L"Скромный Ужин", L"Einfaches Abendessen", L"Jantar Simples", L"Eenvoudig Diner", L"Prosty Obiad", L"Sade Akşam Yemeği");
				break;
			case 7200:
				a = LL14(L"水と草木と青い空", L"Water, Trees and Blue Sky", L"Eau, Arbres et Ciel Bleu", L"Acqua, Alberi e Cielo Azzurro", L"Agua, Árboles y Cielo Azul", L"물과 풀과 파란 하늘", L"水与草木和蓝天", L"المياه والأشجار والسماء الزرقاء", L"Вода, Деревья и Голубое Небо", L"Wasser, Bäume und blauer Himmel", L"Água, Árvores e Céu Azul", L"Water, Bomen en Blauwe Lucht", L"Woda, Drzewa i Błękitne Niebo", L"Su, Ağaçlar ve Mavi Gökyüzü");
				break;
			case 7201:
				a = LL14(L"片手にはレモネード", L"Lemonade in One Hand", L"Limonade dans une Main", L"Limonata in una Mano", L"Limonada en una Mano", L"한 손에는 레모네이드", L"一手拿着柠檬水", L"ليمونادة في يد واحدة", L"Лимонад в Одной Руке", L"Limonade in einer Hand", L"Limonada em uma Mão", L"Limonade in een Hand", L"Lemoniada w Jednej Ręce", L"Bir Elde Limonata");
				break;
			case 7202:
				a = LL14(L"木霊の道", L"Path of Echoes", L"Chemin des Échos", L"Sentiero degli Echi", L"Senda de los Ecos", L"메아리의 길", L"回声之道", L"طريق الأصداء", L"Тропа Эхо", L"Pfad der Echos", L"Caminho dos Ecos", L"Pad van Echo's", L"Ścieżka Ech", L"Yankılar Yolu");
				break;
			case 7203:
				a = LL14(L"古の鼓動", L"Ancient Pulse", L"Pulsation Ancienne", L"Pulsazione Antica", L"Pulso Antiguo", L"고대의 고동", L"古老的脉动", L"النبض القديم", L"Древний Пульс", L"Alter Puls", L"Pulso Antigo", L"Oude Puls", L"Starożytne Tętno", L"Kadim Nabız");
				break;
			case 7204:
				a = L"On The Green Road";
				break;
			case 7205:
				a = LL14(L"鉄橋を越えて", L"Crossing the Iron Bridge", L"Traverser le Pont de Fer", L"Attraversare il Ponte di Ferro", L"Cruzando el Puente de Hierro", L"철교를 건너", L"越过铁桥", L"عبر الجسر الحديدي", L"Пересекая Железный Мост", L"Die Eisenbrücke überqueren", L"Cruzando a Ponte de Ferro", L"De IJzeren Brug Oversteken", L"Przekraczając Żelazny Most", L"Demir Köprüyü Geçerken");
				break;
			case 7250:
				a = LL14(L"木洩れ日の中の静寂", L"Tranquility in the Dappled Light", L"Tranquillité dans la Lumière Tachetée", L"Tranquillità nella Luce Screziata", L"Tranquilidad en la Luz Moteada", L"햇살 속의 정적", L"斑驳光影中的静谧", L"الهدوء في الضوء المرقط", L"Тишина в Пятнистом Свете", L"Stille im gefilterten Licht", L"Tranquilidade na Luz Filtrada", L"Rust in het Gefiltreerde Licht", L"Spokój w Migotliwym Świetle", L"Işık Süzülürken Huzur");
				break;
			case 7251:
				a = LL14(L"偽りの楽土を越えて", L"Beyond the False Paradise", L"Au-Delà du Faux Paradis", L"Oltre il Falso Paradiso", L"Más Allá del Falso Paraíso", L"거짓 낙원을 넘어", L"超越虚假的乐土", L"ما وراء الجنة المزيفة", L"За пределами Ложного Рая", L"Jenseits des falschen Paradieses", L"Além do Falso Paraíso", L"Voorbij het Valse Paradijs", L"Poza Fałszywym Rajem", L"Sahte Cennetin Ötesinde");
				break;
			case 7300:
				a = LL14(L"ジオフロント", L"Geofront", L"Géofront", L"Geofront", L"Geofront", L"지오프런트", L"地底都市", L"جيوفرونت", L"Геофронт", L"Geofront", L"Geofront", L"Geofront", L"Geofront", L"Geofront");
				break;
			case 7301:
				a = LL14(L"七耀の煌き", L"Septium Radiance", L"Éclat du Septium", L"Splendore del Septium", L"Resplandor del Septium", L"칠요의 광채", L"七曜之光辉", L"تألق السفيتيوم", L"Сияние Септиума", L"Septium-Glanz", L"Resplendor do Septium", L"Septium Glinstering", L"Blask Septium", L"Septium Işıltısı");
				break;
			case 7302:
				a = LL14(L"ルバーチェ商会", L"Revache Trading Company", L"Compagnie Revache", L"Revache Trading Company", L"Revache Trading Company", L"르바체 상회", L"鲁巴切商会", L"شركة ريفاش التجارية", L"Торговая Компания Реваш", L"Revache Handelsgesellschaft", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Ticaret Şirketi");
				break;
			case 7303:
				a = LL14(L"鳴るはずのない鐘", L"The Bell That Shouldn't Ring", L"La Cloche Qui Ne Devrait Pas Sonner", L"La Campana Che Non Dovrebbe Suonare", L"La Campana Que No Debería Sonar", L"울릴 리 없는 종", L"不该鸣起的钟声", L"الجرس الذي لا يجب أن يرن", L"Колокол, Который Не Должен Звонить", L"Die Glocke, die nicht läuten sollte", L"O Sino Que Não Deveria Tocar", L"De Klok Die Niet Zou Moeten Luiden", L"Dzwon, Który Nie Powinien Bić", L"Çalmaması Gereken Çan");
				break;
			case 7304:
				a = LL14(L"忘れられし幻夢の狭間", L"Forgotten Phantasmal Gap", L"Interstice Fantomatique Oublié", L"Varco Fantasmatico Dimenticato", L"Brecha Fantasmal Olvidada", L"잊힌 환몽의 틈새", L"被遗忘的幻梦之间", L"فجوة خيالية منسية", L"Забытый Призрачный Разрыв", L"Vergessene Phantomale Lücke", L"Lacuna Fantasmal Esquecida", L"Vergeten Spookachtige Kloof", L"Zapomniana Fantomalna Szczelina", L"Unutulmuş Hayali Boşluk");
				break;
			case 7305:
				a = L"A Light Illuminating The Depths";
				break;
			case 7350:
				a = LL14(L"Dの残影", L"D's Shadow", L"L'Ombre de D", L"L'Ombra di D", L"La Sombra de D", L"D의 잔영", L"D的残影", L"ظل D", L"Тень D", L"Ds Schatten", L"A Sombra de D", L"D's Schaduw", L"Cień D", L"D'nin Gölgesi");
				break;
			case 7351:
				a = LL14(L"異変の兆し", L"Omen of Change", L"Présage de Changement", L"Presagio di Cambiamento", L"Presagio de Cambio", L"이변의 징조", L"异变的征兆", L"نذير التغيير", L"Предзнаменование Перемен", L"Vorbote des Wandels", L"Presságio de Mudança", L"Voorteken van Verandering", L"Zwiastun Zmiany", L"Değişimin İşareti");
				break;
			case 7352:
				a = L"Mystic Core";
				break;
			case 7353:
				a = LL14(L"最果ての樹", L"Tree at World's End", L"L'Arbre au Bout du Monde", L"L'Albero alla Fine del Mondo", L"El Árbol al Fin del Mundo", L"땅 끝의 나무", L"天涯之树", L"شجرة عند نهاية العالم", L"Дерево на Краю Света", L"Baum am Ende der Welt", L"A Árvore no Fim do Mundo", L"De Boom aan het Einde van de Wereld", L"Drzewo na Końcu Świata", L"Dünyanın Sonundaki Ağaç");
				break;
			case 7354:
				a = LL14(L"暴魔の呼び声", L"Call of the Beast", L"L'Appel de la Bête", L"Il Richiamo della Bestia", L"El Llamado de la Bestia", L"폭마의 부름", L"暴魔的呼唤", L"دعوة الوحش", L"Зов Зверя", L"Ruf des Ungeheuers", L"O Chamado da Besta", L"De Roep van het Beest", L"Wołanie Bestii", L"Canavarın Çağrısı");
				break;
			case 7356:
				a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
				break;
			case 7400:
				a = L"Get Over The Barrier!";
				break;
			case 7401:
				a = L"Arrest The Criminal";
				break;
			case 7402:
				a = L"Formidable Enemy";
				break;
			case 7403:
				a = L"Stand Up Battle Formation Again!";
				break;
			case 7404:
				a = L"Inevitable Struggle";
				break;
			case 7405:
				a = L"Demonic Drive";
				break;
			case 7406:
				a = L"Arrival Existence";
				break;
			case 7408:
				a = LL14(L"これが俺たちの力だ!", L"This Is Our Power!", L"C'est Notre Pouvoir !", L"Questo È il Nostro Potere!", L"¡Este Es Nuestro Poder!", L"이것이 우리들의 힘이다!", L"这就是我们的力量!", L"هذه هي قوتنا!", L"Это Наша Сила!", L"Das Ist Unsere Kraft!", L"Este É o Nosso Poder!", L"Dit Is Onze Kracht!", L"To Jest Nasza Siła!", L"Bu Bizim Gücümüz!");
				break;
			case 7450:
				a = L"Seize The Truth!";
				break;
			case 7451:
				a = L"Concentrate All Firepower!!";
				break;
			case 7452:
				a = L"Conflicting Passions";
				break;
			case 7453:
				a = L"Unexpected Emergency";
				break;
			case 7454:
				a = L"Mythtic Roar";
				break;
			case 7455:
				a = L"Destruction Impulse";
				break;
			case 7458:
				a = L"Unfathomed Force";
				break;
			case 7459:
				a = L"The Azure Arbitrator";
				break;
			case 7460:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
				break;
			case 7500:
				a = LL14(L"金の太陽、銀の月　-陽の熱情", L"Golden Sun, Silver Moon -Solar Passion-", L"Soleil d'Or, Lune d'Argent -Passion Solaire-", L"Sole d'Oro, Luna d'Argento -Passione Solare-", L"Sol Dorado, Luna de Plata -Pasión Solar-", L"황금의 태양, 은의 달 -태양의 열정-", L"黄金之阳，白银之月 -太阳的热情-", L"الشمس الذهبية، القمر الفضي -شغف شمسي-", L"Золотое Солнце, Серебряная Луна -Солнечная Страсть-", L"Goldene Sonne, Silberner Mond -Sonnenleidenschaft-", L"Sol Dourado, Lua de Prata -Paixão Solar-", L"Gouden Zon, Zilveren Maan -Zonnige Passie-", L"Złote Słońce, Srebrny Księżyc -Słoneczna Namiętność-", L"Altın Güneş, Gümüş Ay -Güneş Tutkusu-");
				break;
			case 7501:
				a = LL14(L"金の太陽、銀の月　-月の慕情", L"Golden Sun, Silver Moon -Lunar Affection-", L"Soleil d'Or, Lune d'Argent -Affection Lunaire-", L"Sole d'Oro, Luna d'Argento -Affetto Lunare-", L"Sol Dorado, Luna de Plata -Afecto Lunar-", L"황금의 태양, 은의 달 -달의 모정-", L"黄金之阳，白银之月 -月亮的思慕-", L"الشمس الذهبية، القمر الفضي -مودة قمرية-", L"Золотое Солнце, Серебряная Луна -Лунная Нежность-", L"Goldene Sonne, Silberner Mond -Mondneigung-", L"Sol Dourado, Lua de Prata -Afeição Lunar-", L"Gouden Zon, Zilveren Maan -Maanachtige Genegenheid-", L"Złote Słońce, Srebrny Księżyc -Księżycowe Uczucie-", L"Altın Güneş, Gümüş Ay -Ay Sevgisi-");
				break;
			case 7502:
				a = LL14(L"金の太陽、銀の月　-童心", L"Golden Sun, Silver Moon -Innocence-", L"Soleil d'Or, Lune d'Argent -Innocence-", L"Sole d'Oro, Luna d'Argento -Innocenza-", L"Sol Dorado, Luna de Plata -Inocencia-", L"황금의 태양, 은의 달 -동심-", L"黄金之阳，白银之月 -童心-", L"الشمس الذهبية، القمر الفضي -براءة-", L"Золотое Солнце, Серебряная Луна -Невинность-", L"Goldene Sonne, Silberner Mond -Unschuld-", L"Sol Dourado, Lua de Prata -Inocência-", L"Gouden Zon, Zilveren Maan -Onschuld-", L"Złote Słońce, Srebrny Księżyc -Niewinność-", L"Altın Güneş, Gümüş Ay -Masumiyet-");
				break;
			case 7503:
				a = LL14(L"金の太陽、銀の月　-運命の刻", L"Golden Sun, Silver Moon -Hour of Fate-", L"Soleil d'Or, Lune d'Argent -L'Heure du Destin-", L"Sole d'Oro, Luna d'Argento -L'Ora del Destino-", L"Sol Dorado, Luna de Plata -La Hora del Destino-", L"황금의 태양, 은의 달 -운명의 각-", L"黄金之阳，白银之月 -命运的时刻-", L"الشمس الذهبية، القمر الفضي -ساعة القدر-", L"Золотое Солнце, Серебряная Луна -Час Судьбы-", L"Goldene Sonne, Silberner Mond -Stunde des Schicksals-", L"Sol Dourado, Lua de Prata -A Hora do Destino-", L"Gouden Zon, Zilveren Maan -Het Uur van het Lot-", L"Złote Słońce, Srebrny Księżyc -Godzina Przeznaczenia-", L"Altın Güneş, Gümüş Ay -Kaderin Saati-");
				break;
			case 7504:
				a = LL14(L"金の太陽、銀の月　-譲れぬ想い", L"Golden Sun, Silver Moon -Unyielding Feelings-", L"Soleil d'Or, Lune d'Argent -Sentiments Inébranlables-", L"Sole d'Oro, Luna d'Argento -Sentimenti Irremovibili-", L"Sol Dorado, Luna de Plata -Sentimientos Inquebrantables-", L"황금의 태양, 은의 달 -양보할 수 없는 마음-", L"黄金之阳，白银之月 -不可退让的心意-", L"الشمس الذهبية، القمر الفضي -مشاعر لا تلين-", L"Золотое Солнце, Серебряная Луна -Непреклонные Чувства-", L"Goldene Sonne, Silberner Mond -Unnachgiebige Gefühle-", L"Sol Dourado, Lua de Prata -Sentimentos Inabaláveis-", L"Gouden Zon, Zilveren Maan -Onwrikbare Gevoelens-", L"Złote Słońce, Srebrny Księżyc -Nieustępliwe Uczucia-", L"Altın Güneş, Gümüş Ay -Vazgeçilmez Duygular-");
				break;
			case 7505:
				a = LL14(L"金の太陽、銀の月　-幾千の夜を越えて", L"Golden Sun, Silver Moon -Beyond Countless Nights-", L"Soleil d'Or, Lune d'Argent -Au-Delà de Nuits Sans Nombre-", L"Sole d'Oro, Luna d'Argento -Oltre Innumerevoli Notti-", L"Sol Dorado, Luna de Plata -Más Allá de Incontables Noches-", L"황금의 태양, 은의 달 -수천의 밤을 넘어-", L"黄金之阳，白银之月 -跨越无数夜晚-", L"الشمس الذهبية، القمر الفضي -عبر لا يحصى من الليالي-", L"Золотое Солнце, Серебряная Луна -Сквозь Бесчисленные Ночи-", L"Goldene Sonne, Silberner Mond -Jenseits Unzähliger Nächte-", L"Sol Dourado, Lua de Prata -Além de Incontáveis Noites-", L"Gouden Zon, Zilveren Maan -Voorbij Ontelbare Nachten-", L"Złote Słońce, Srebrny Księżyc -Poza Niezliczonymi Nocami-", L"Altın Güneş, Gümüş Ay -Sayısız Gecelerin Ötesinde-");
				break;
			case 7506:
				a = LL14(L"金の太陽、銀の月　-夜明け〜大団円", L"Golden Sun, Silver Moon -Dawn to Grand Finale-", L"Soleil d'Or, Lune d'Argent -Aube vers le Grand Finale-", L"Sole d'Oro, Luna d'Argento -Alba verso il Gran Finale-", L"Sol Dorado, Luna de Plata -Amanecer hasta el Gran Final-", L"황금의 태양, 은의 달 -새벽~대단원-", L"黄金之阳，白银之月 -黎明~大团圆-", L"الشمس الذهبية، القمر الفضي -الفجر حتى الخاتمة الكبرى-", L"Золотое Солнце, Серебряная Луна -Рассвет до Грандиозного Финала-", L"Goldene Sonne, Silberner Mond -Morgengrauen bis zum großen Finale-", L"Sol Dourado, Lua de Prata -Amanhecer até o Grande Final-", L"Gouden Zon, Zilveren Maan -Dageraad tot het Grote Finale-", L"Złote Słońce, Srebrny Księżyc -Świt do Wielkiego Finału-", L"Altın Güneş, Gümüş Ay -Şafaktan Büyük Finale-");
				break;
			case 7507:
				a = L"Intense Chase";
				break;
			case 7509:
				a = LL14(L"守りぬく意志", L"Unyielding Will", L"Volonté Inébranlable", L"Volontà Irremovibile", L"Voluntad Inquebrantable", L"지켜내는 의지", L"守护的意志", L"إرادة لا تلين", L"Непреклонная Воля", L"Unnachgiebiger Wille", L"Vontade Inabalável", L"Onwrikbare Wil", L"Nieustępliwa Wola", L"Vazgeçmeyen İrade");
				break;
			case 7510:
				a = LL14(L"叡智への誘い", L"Invitation to Wisdom", L"Invitation à la Sagesse", L"Invito alla Saggezza", L"Invitación a la Sabiduría", L"예지로의 유혹", L"通往智慧的邀请", L"دعوة إلى الحكمة", L"Приглашение к Мудрости", L"Einladung zur Weisheit", L"Convite à Sabedoria", L"Uitnodiging tot Wijsheid", L"Zaproszenie do Mądrości", L"Bilgeliğe Davet");
				break;
			case 7511:
				a = LL14(L"危地", L"Perilous Ground", L"Terrain Périlleux", L"Terreno Pericoloso", L"Terreno Peligroso", L"위지", L"危险之地", L"أرض خطرة", L"Опасная Территория", L"Gefährliches Terrain", L"Terreno Perigoso", L"Gevaarlijk Terrein", L"Niebezpieczny Teren", L"Tehlikeli Bölge");
				break;
			case 7512:
				a = LL14(L"揺るぎない強さ", L"Unshakable Strength", L"Force Inébranlable", L"Forza Incrollabile", L"Fuerza Inquebrantable", L"흔들림 없는 강함", L"不可动摇的力量", L"قوة لا تتزعزع", L"Непоколебимая Сила", L"Unerschütterliche Stärke", L"Força Inabalável", L"Onwankelbare Kracht", L"Niezachwiana Siła", L"Sarsılmaz Güç");
				break;
			case 7513:
				a = LL14(L"夜景に霞む星空", L"Starry Sky in the Night", L"Ciel Étoilé dans la Nuit", L"Cielo Stellato nella Notte", L"Cielo Estrellado en la Noche", L"야경에 가려진 별하늘", L"夜色中朦胧的星空", L"سماء مرصعة بالنجوم في الليل", L"Звёздное Небо Ночью", L"Sternenhimmel in der Nacht", L"Céu Estrelado na Noite", L"Sterrenhemel in de Nacht", L"Rozgwieżdżone Niebo w Nocy", L"Gece Yıldızlı Gökyüzü");
				break;
			case 7514:
				a = LL14(L"いつかきっと", L"Someday", L"Un Jour, Sûrement", L"Un Giorno, Di Certo", L"Algún Día, Seguro", L"언젠가 반드시", L"总有一天", L"يوماً ما بالتأكيد", L"Когда-нибудь Обязательно", L"Irgendwann Bestimmt", L"Um Dia, Com Certeza", L"Ooit Zeker", L"Kiedyś Na Pewno", L"Bir Gün Mutlaka");
				break;
			case 7515:
				a = LL14(L"柔らかな心", L"Tender Heart", L"Cœur Tendre", L"Cuore Tenero", L"Corazón Tierno", L"부드러운 마음", L"温柔的心", L"قلب رقيق", L"Нежное Сердце", L"Zartes Herz", L"Coração Terno", L"Teder Hart", L"Czułe Serce", L"Nazik Kalp");
				break;
			case 7516:
				a = LL14(L"点と線", L"Dots and Lines", L"Points et Lignes", L"Punti e Linee", L"Puntos y Líneas", L"점과 선", L"点与线", L"نقاط وخطوط", L"Точки и Линии", L"Punkte und Linien", L"Pontos e Linhas", L"Punten en Lijnen", L"Punkty i Linie", L"Noktalar ve Çizgiler");
				break;
			case 7517:
				a = LL14(L"一触即発", L"Imminent Crisis", L"Crise Imminente", L"Crisi Imminente", L"Crisis Inminente", L"일촉즉발", L"一触即发", L"أزمة وشيكة", L"Надвигающийся Кризис", L"Unmittelbar Bevorstehende Krise", L"Crise Iminente", L"Dreigende Crisis", L"Bezpośredni Kryzys", L"Yaklaşan Kriz");
				break;
			case 7518:
				a = L"Foolish Gig";
				break;
			case 7519:
				a = LL14(L"リベールからの風", L"Wind from Liberl", L"Vent de Liberl", L"Vento da Liberl", L"Viento de Liberl", L"리베르로부터의 바람", L"来自利贝尔的风", L"ريح من ليبرل", L"Ветер из Либерла", L"Wind aus Liberl", L"Vento de Liberl", L"Wind uit Liberl", L"Wiatr z Liberl", L"Liberl'den Rüzgar");
				break;
			case 7520:
				a = LL14(L"とどいた想い", L"Feelings Delivered", L"Sentiments Transmis", L"Sentimenti Consegnati", L"Sentimientos Entregados", L"닿은 마음", L"传达到的心意", L"مشاعر واصلة", L"Переданные Чувства", L"Übermittelte Gefühle", L"Sentimentos Entregues", L"Bezorgde Gevoelens", L"Dostarczone Uczucia", L"İletilen Duygular");
				break;
			case 7521:
				a = L"Underground Kids";
				break;
			case 7522:
				a = L"Terminal Room";
				break;
			case 7523:
				a = LL14(L"響きあう心", L"Resonating Hearts", L"Cœurs en Résonance", L"Cuori in Risonanza", L"Corazones en Resonancia", L"서로 울리는 마음", L"共鸣的心", L"قلوب رنانة متناغمة", L"Резонирующие Сердца", L"Resonierender Herzen", L"Corações em Ressonância", L"Resonerende Harten", L"Rezonujące Serca", L"Rezonans Eden Kalpler");
				break;
			case 7524:
				a = L"Limit Break";
				break;
			case 7525:
				a = LL14(L"パラダイスミ☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"패러다임☆", L"帕拉迪gm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆");
				break;
			case 7526:
				a = L"Gnosis";
				break;
			case 7527:
				a = L"Get Over The Barrier! -Roaring Version-";
				break;
			case 7528:
				a = LL14(L"それぞれの明日", L"Our Tomorrows", L"Nos Lendemains", L"I Nostri Domani", L"Nuestros Mañanas", L"저마다의 내일", L"各自的明天", L"غد كل منا", L"Наши Завтрашние Дни", L"Unsere Morgigen Tage", L"Nossos Amanhãs", L"Onze Morgens", L"Nasze Jutrzejsze Dni", L"Hepimizin Yarınları");
				break;
			case 7529:
				a = LL14(L"効果音楽1", L"Sound Effect Music 1", L"Musique d'Effet Sonore 1", L"Musica Effetto Sonoro 1", L"Música de Efecto de Sonido 1", L"효과 음악 1", L"音效音乐1", L"موسيقى مؤثرات صوتية 1", L"Звуковая Музыка 1", L"Soundeffekt-Musik 1", L"Música de Efeito Sonoro 1", L"Geluidseffect Muziek 1", L"Muzyka Efektów Dźwiękowych 1", L"Ses Efekti Müziği 1");
				break;
			case 7530:
				a = LL14(L"効果音楽2", L"Sound Effect Music 2", L"Musique d'Effet Sonore 2", L"Musica Effetto Sonoro 2", L"Música de Efecto de Sonido 2", L"효과 음악 2", L"音效音乐2", L"موسيقى مؤثرات صوتية 2", L"Звуковая Музыка 2", L"Soundeffekt-Musik 2", L"Música de Efeito Sonoro 2", L"Geluidseffect Muziek 2", L"Muzyka Efektów Dźwiękowych 2", L"Ses Efekti Müziği 2");
				break;
			case 7531:
				a = LL14(L"効果音楽3", L"Sound Effect Music 3", L"Musique d'Effet Sonore 3", L"Musica Effetto Sonoro 3", L"Música de Efecto de Sonido 3", L"효과 음악 3", L"音效音乐3", L"موسيقى مؤثرات صوتية 3", L"Звуковая Музыка 3", L"Soundeffekt-Musik 3", L"Música de Efeito Sonoro 3", L"Geluidseffect Muziek 3", L"Muzyka Efektów Dźwiękowych 3", L"Ses Efekti Müziği 3");
				break;
			case 7532:
				a = LL14(L"効果音楽4", L"Sound Effect Music 4", L"Musique d'Effet Sonore 4", L"Musica Effetto Sonoro 4", L"Música de Efecto de Sonido 4", L"효과 음악 4", L"音效音乐4", L"موسيقى مؤثرات صوتية 4", L"Звуковая Музыка 4", L"Soundeffekt-Musik 4", L"Música de Efeito Sonoro 4", L"Geluidseffect Muziek 4", L"Muzyka Efektów Dźwiękowych 4", L"Ses Efekti Müziği 4");
				break;
			case 7533:
				a = LL14(L"踏み出す勇気", L"Courage to Step Forward", L"Courage d'Avancer", L"Coraggio di Andare Avanti", L"Valentía para Avanzar", L"내딛는 용기", L"踏出的勇气", L"الشجاعة للتقدم للأمام", L"Смелость Шагнуть Вперёд", L"Mut Voranzugehen", L"Coragem de Dar um Passo", L"Moed om Vooruit te Stappen", L"Odwaga by Ruszyć Naprzód", L"İlerleme Cesareti");
				break;
			case 7534:
				a = LL14(L"その背中を見つめて", L"Watching Your Back", L"Regarder ton Dos", L"Guardare le Tue Spalle", L"Mirando tu Espalda", L"그 뒷모습을 바라보며", L"凝视着那背影", L"مراقبة ظهرك", L"Глядя в Твою Спину", L"Deinen Rücken Beobachten", L"Olhando suas Costas", L"Naar Je Rug Kijken", L"Patrząc na Twoje Plecy", L"Sırtına Bakarak");
				break;
			case 7540:
			case 7541:
			case 7542:
			case 7543:
			case 7544:
				a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
				break;
			case 7550:
				a = LL14(L"オルキスタワー", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"오르키스 타워", L"兰花塔", L"برج أوركيس", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower");
				break;
			case 7551:
				a = L"Catastrophe";
				break;
			case 7552:
				a = LL14(L"碧き雫", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"푸른 물방울", L"碧之雫", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator");
				break;
			case 7553:
				a = LL14(L"神機降臨", L"Divine Mechanoid Descent", L"Descente du Mécanisme Divin", L"Discesa del Meccanismo Divino", L"Descenso del Mecanismo Divino", L"신기강림", L"神机降临", L"نزول الآلية الإلهية", L"Нисхождение Божественного Механизма", L"Abstieg des Göttlichen Mechanoids", L"Descida do Mecanismo Divino", L"Afdaling van het Goddelijke Mechanisme", L"Zstąpienie Boskiego Mechanizmu", L"İlahi Mekanizmanın İnişi");
				break;
			case 7554:
				a = LL14(L"ふるわれる奇蹟", L"Shaking Miracle", L"Miracle Tremblant", L"Miracolo Tremante", L"Milagro Tembloroso", L"뒤흔들리는 기적", L"震撼的奇迹", L"معجزة مهتزة", L"Дрожащее Чудо", L"Erschütterndes Wunder", L"Milagre Tremendo", L"Trillend Wonder", L"Drżący Cud", L"Sarsılan Mucize");
				break;
			case 7555:
				a = LL14(L"予定外の奇蹟", L"Unexpected Miracle", L"Miracle Inattendu", L"Miracolo Inaspettato", L"Milagro Inesperado", L"예정 밖의 기적", L"意料之外的奇迹", L"معجزة غير متوقعة", L"Неожиданное Чудо", L"Unerwartetes Wunder", L"Milagre Inesperado", L"Onverwacht Wonder", L"Nieoczekiwany Cud", L"Beklenmedik Mucize");
				break;
			case 7556:
				a = LL14(L"鋼鉄の咆哮 -脅威-", L"Roar of Steel -Threat-", L"Rugissement d'Acier -Menace-", L"Ruggito d'Acciaio -Minaccia-", L"Rugido de Acero -Amenaza-", L"강철의 포효 -위협-", L"钢铁的咆哮 -威胁-", L"زئير الحديد -تهديد-", L"Рёв Стали -Угроза-", L"Stahlgebrüll -Bedrohung-", L"Rugido de Aço -Ameaça-", L"Staalgebulder -Bedreiging-", L"Ryk Stali -Zagrożenie-", L"Çeliğin Kükremesi -Tehdit-");
				break;
			case 7560:
				a = LL14(L"雨の日の真実", L"Truth on a Rainy Day", L"Vérité un Jour de Pluie", L"Verità in un Giorno di Pioggia", L"Verdad en un Día Lluvioso", L"비 오는 날의 진실", L"雨天的真相", L"الحقيقة في يوم ممطر", L"Правда в Дождливый День", L"Wahrheit an einem Regentag", L"Verdade em um Dia Chuvoso", L"Waarheid op een Regenachtige Dag", L"Prawda w Deszczowy Dzień", L"Yağmurlu Bir Günde Gerçek");
				break;
			case 7561:
				a = LL14(L"不穏", L"Troubled", L"Trouble", L"Turbato", L"Perturbado", L"불온", L"不稳", L"قلق", L"Тревожный", L"Unruhig", L"Perturbado", L"Onrustig", L"Niepokój", L"Huzursuz");
				break;
			case 7562:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
				break;
			case 7563:
				a = LL14(
					L"犠牲の先の希望",                   /* 0: ja */
					L"Hope Beyond Sacrifice",          /* 1: en */
					L"Espoir au-Delà du Sacrifice",    /* 2: fr */
					L"Speranza Oltre il Sacrificio",   /* 3: it */
					L"Esperanza Más Allá del Sacrificio", /* 4: es */
					L"희생 뒤의 희망",                   /* 5: ko */
					L"牺牲之后的希望",                   /* 6: zh */
					L"الأمل بعد التضحية",              /* 7: ar */
					L"Надежда за жертвой",             /* 8: ru (修正点) */
					L"Hoffnung Jenseits des Opfers",   /* 9: de */
					L"Esperança Além do Sacrifício",  /* 10: pt */
					L"Hoop Voorbij Opoffering",        /* 11: nl */
					L"Nadzieja Poza Poświęceniem",     /* 12: pl */
					L"Fedakarlığın Ötesinde Umut"      /* 13: tr */
				);					break;
			case 7564:
				a = L"Strange Feel";
				break;
			case 7565:
				a = L"Exhilarating Ride";
				break;
			case 7566:
				a = LL14(L"それぞれの正義", L"Each One's Justice", L"La Justice de Chacun", L"La Giustizia di Ognuno", L"La Justicia de Cada Uno", L"저마다의 정의", L"各自的正义", L"عدالة كل فرد", L"Справедливость Каждого", L"Gerechtigkeit Jedes Einzelnen", L"A Justiça de Cada Um", L"Ieders Gerechtigheid", L"Sprawiedliwość Każdego", L"Herkesin Adaleti");
				break;
			case 7567:
				a = LL14(L"乗り越えるべき壁", L"Wall to Overcome", L"Mur à Surmonter", L"Muro da Superare", L"Muro a Surperar", L"극복해야 할 벽", L"需要翻越的墙", L"جدار يجب عبوره", L"Стена, Которую Нужно Преодолеть", L"Zu Überwindende Wand", L"Muro a Surperar", L"Muur om te Overwinnen", L"Mur do Pokonania", L"Aşılması Gereken Duvar");
				break;
			case 7568:
				a = LL14(L"月下の想い", L"Feelings Under the Moon", L"Sentiments sous la Lune", L"Sentimenti sotto la Luna", L"Sentimientos bajo la Luna", L"월하의 진심", L"月下的心意", L"مشاعر تحت القمر", L"Чувства под Луной", L"Gefühle unter dem Mond", L"Sentimentos sob a Lua", L"Gevoelens onder de Maan", L"Uczucia pod Księżycem", L"Ay Işığında Duygular");
				break;
			case 7569:
				a = L"Miss You";
				break;
			case 7570:
				a = LL14(L"天の車", L"Chariot of Heaven", L"Char Céleste", L"Carro del Cielo", L"Carro Celestial", L"하늘의 수레", L"天之车轮", L"عربة السماء", L"Небесная Колесница", L"Himmelswagen", L"Carruagem do Céu", L"Hemelse Strijdwagen", L"Niebieski Rydwan", L"Gök Arabası");
				break;
			case 7571:
				a = LL14(L"突きつけられた現実", L"Reality Thrust Upon Us", L"Réalité Imposée", L"Realtà Imposta", L"Realidad Impuesta", L"들이닥친 현실", L"被加诸的现实", L"الحقيقة المفروضة علينا", L"Реальность, Навязанная Нам", L"Uns Aufgezwungene Realität", L"Realidade Imposta", L"Opgelegde Realiteit", L"Narzucona Rzeczywistość", L"Üstümüze Dayatılan Gerçek");
				break;
			case 7572:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
				break;
			case 7573:
				a = LL14(L"全てを識るもの", L"The Omniscient", L"L'Omniscient", L"L'Onnisciente", L"El Omnisciente", L"모든 것을 아는 자", L"无所不知者", L"العليم بكل شيء", L"Всезнающий", L"Der Allwissende", L"O Onisciente", L"De Alwetende", L"Wszechwiedzący", L"Her Şeyi Bilen");
				break;
			case 7574:
				a = LL14(L"想い、辿り着く場所", L"Where Feelings Lead", L"Là où Mènent les Sentiments", L"Dove Portano i Sentimenti", L"Adonde Llevan los Sentimientos", L"마음이 가닿는 곳", L"心意所至之处", L"حيث تقود المشاعر", L"Куда Ведут Чувства", L"Wohin Gefühle Führen", L"Para Onde os Sentimentos Levam", L"Waar Gevoelens Naartoe Leiden", L"Dokąd Prowadzą Uczucia", L"Duyguların Götürdüğü Yer");
				break;
			case 7575:
				a = LL14(L"揺れ動く心", L"Wavering Heart", L"Cœur Vacillant", L"Cuore Vacillante", L"Corazón Vacilante", L"동요하는 마음", L"摇曳的心", L"قلب متردد", L"Колеблющееся Сердце", L"Schwankendes Herz", L"Coração Vacilante", L"Weifelend Hart", L"Chwiejące się Serce", L"Kararsız Kalp");
				break;
			case 7576:
				a = LL14(L"星降る夜に", L"On a Starry Night", L"Par une Nuit Étoilée", L"In una Notte Stellata", L"En una Noche Estrellada", L"별 내리는 밤에", L"星降之夜", L"في ليلة مرصعة بالنجوم", L"В Звёздную Ночь", L"In einer Sternennacht", L"Em uma Noite Estrelada", L"Op een Sterrenachtige Nacht", L"W Gwiaździstą Noc", L"Yıldızlı Bir Gecede");
				break;
			case 7577:
			case 7578:
			case 7579:
			case 7580:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
				break;
			case 7581:
				a = LL14(L"本当の絆", L"True Bonds", L"Vrais Liens", L"Veri Legami", L"Lazos Verdaderos", L"진정한 인연", L"真正的羁绊", L"الروابط الحقيقية", L"Настоящие Узы", L"Wahre Bande", L"Laços Verdadeiros", L"Ware Banden", L"Prawdziwe Więzi", L"Gerçek Bağlar");
				break;
			case 7582:
				a = LL14(L"猛き獣たち", L"Fierce Beasts", L"Bêtes Féroces", L"Bestie Feroci", L"Bestias Feroces", L"사나운 짐승들", L"凶猛的野兽们", L"وحوش ضارية", L"Свирепые Звери", L"Wilde Bestien", L"Bestas Ferozes", L"Woeste Beesten", L"Dzikie Bestie", L"Vahşi Canavarlar");
				break;
			case 7583:
				a = LL14(L"西ゼムリア通商会議", L"West Zemuria Trade Conference", L"Conférence Commerciale de Zemuria Occidentale", L"Conferenza Commerciale della Zemuria Occidentale", L"Conferencia Comercial de Zemuria Occidental", L"서제무리아 통상회의", L"西塞姆利亚通商会议", L"مؤتمر تجارة غرب زيموريا", L"Западно-Земурийская Торговая Конференция", L"Westzemuranische Handelskonferenz", L"Conferência Comercial da Zemuria Ocidental", L"West-Zemuria Handelsconferentie", L"Zachodnia Konferencja Handlowa Zemurii", L"Batı Zemuria Ticaret Konferansı");
				break;
			case 7584:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
				break;
			case 7585:
				a = LL14(L"千年の妄執", L"Obsession of Millennia", L"Obsession des Millénaires", L"Ossessione dei Millenni", L"Obsesión de los Milenios", L"천년의 망집", L"千年的妄执", L"هوس الألفية", L"Одержимость Тысячелетий", L"Obsession der Jahrtausende", L"Obsessão dos Milênios", L"Obsessie van Millennia", L"Obsesja Tysiącleci", L"Bin Yılın Takıntısı");
				break;
			case 7586:
				a = LL14(L"鋼鉄の咆哮 -死線-", L"Roar of Steel -Death Line-", L"Rugissement d'Acier -Ligne de Mort-", L"Ruggito d'Acciaio -Linea della Morte-", L"Rugido de Acero -Línea de Muerte-", L"강철의 포효 -사선-", L"钢铁的咆哮 -死线-", L"زئير الحديد -خط الموت-", L"Рёв Стали -Линия Смерти-", L"Stahlgebrüll -Todeslinie-", L"Rugido de Aço -Linha da Morte-", L"Staalgebulder -Doodslijn-", L"Ryk Stali -Linia Śmierci-", L"Çeliğin Kükremesi -Ölüm Hattı-");
				break;
			case 7587:
				a = LL14(L"ポムっと! -お花見団子の逆襲-", L"Pom! -Cherry Blossom Dango Counterattack-", L"Pom ! -Contre-attaque des Dango de Fleurs de Cerisier-", L"Pom! -Contrattacco dei Dango di Fiori di Ciliegio-", L"¡Pom! -Contraataque de los Dango de Flores de Cerezo-", L"폼앗! -꽃구경 경단의 역습-", L"Pom! -赏花团子的反攻-", L"بوم! -هجوم مضاد لدانغو براعم الكرز-", L"Пом! -Контратака Данго из Цветков Сакуры-", L"Pom! -Gegenangriff der Kirschblüten-Dango-", L"Pom! -Contra-ataque dos Dango de Flor de Cerejeira-", L"Pom! -Tegenaanval van Kersenbloesem Dango-", L"Pom! -Kontratak Dango z Kwiatami Wiśni-", L"Pom! -Kiraz Çiçeği Dango'nun Karşı Saldırısı-");
				break;
			case 7588:
				a = L"Fateful Confrontation -Pom! Ver.-";
				break;
			case 7589:
				a = LL14(L"ポムりますか", L"Shall We Pom?", L"On Pomme ?", L"Facciamo Pom ?", L"¿Hacemos Pom?", L"폼 할까요?", L"来一局Pom吗？", L"هل نلعب بوم؟", L"Сыграем в Пом?", L"Sollen Wir Pom Spielen?", L"Vamos Pom?", L"Zullen We Pomme?", L"Czy Zagramy w Pom?", L"Pom Oynayalım mı?");
				break;
			case 7590:
				a = LL14(L"エリィ絶叫コースター", L"Elie Scream Coaster", L"Montagnes Russes des Cris d'Elie", L"Montagne Russe delle Urla di Elie", L"Montaña Rusa de los Gritos de Elie", L"에리 절규 코스터", L"艾莉尖叫云霄飞车", L"أفعوانية صرخة إيلي", L"Американские Горки Воплей Эли", L"Elie-Schrei-Achterbahn", L"Montanha-russa dos Gritos de Elie", L"Elie Schreeuw Achtbaan", L"Kolejka Krzyków Elie", L"Elie Çığlık Roller Coaster");
				break;
			case 7591:
				a = LL14(L"小さな英雄 -オルゴール-", L"Little Hero -Music Box-", L"Petit Héros -Boîte à Musique-", L"Piccolo Eroe -Carillon-", L"Pequeño Héroe -Caja de Música-", L"작은 영웅 -오르골-", L"小小英雄 -音乐盒-", L"البطل الصغير -صندوق الموسيقى-", L"Маленький Герой -Музыкальная Шкатулка-", L"Kleiner Held -Spieluhr-", L"Pequeno Herói -Caixa de Música-", L"Kleine Held -Muziekdoos-", L"Mały Bohater -Pozytywka-", L"Küçük Kahraman -Müzik Kutusu-");
				break;
			case 7592:
				a = L"TOWER OF THE SHADOW OF DEATH -Jukebox-";
				break;
			}
			stitle = a;
		}
	}
	// wavExportPath時はエクスポートパス内でcc.Openするためここではスキップ
	if (m_c2.GetCheck() == 1 && wavExportPath.GetLength() == 0)
	{
		cc1 = 1;
		CString outPath = BuildWavExportOutputPath(filen);
		if (outPath.Right(4).MakeLower() != _T(".wav")) outPath += _T(".wav");
		if (cc.Open(outPath, CFile::modeCreate | CFile::modeReadWrite | CFile::typeBinary | CFile::shareExclusive, NULL) != TRUE) {
			m_saisai.EnableWindow(TRUE);
			endflg = 0;
			return;
		}
		// 2GB超対応(RF64)ヘッダ。確定はstop()/stop1()のFinalizeWavStreamHeaderRF64で行う。
		WriteWavStreamHeaderRF64(cc);
	}
	if (mode == 30) { wavbit_sample_Hz = 48000; wavsam_depth = 16; wavchannel = 2; }
	NormalizePlaybackWaveFormat();

	ConfigurePlaybackOutputAndUpscaler();
	g_audioUpscaler.Reset();

	int dsTryRate = g_ds_pcm_rate;
	static const GUID GUID_SUBTYPE_PCM = { 0x00000001, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

	WAVEFORMATEX wfx1;
	wfx1.wFormatTag = WAVE_FORMAT_PCM;
	wfx1.nChannels = (WORD)((g_ds_pcm_ch <= 2) ? g_ds_pcm_ch : 2);
	wfx1.nSamplesPerSec = dsTryRate;
	wfx1.wBitsPerSample = (WORD)g_ds_pcm_bits;
	wfx1.nBlockAlign = (WORD)(wfx1.nChannels * wfx1.wBitsPerSample / 8);
	wfx1.nAvgBytesPerSec = (DWORD)((DWORD)wfx1.nSamplesPerSec * (DWORD)wfx1.nBlockAlign);
	wfx1.cbSize = 0;

	DWORD targetSpeakers = (DWORD)DirectSoundChannelMaskForOutput(g_ds_pcm_ch, savedata.speaker_layout);
	WAVEFORMATEXTENSIBLE wfx = {};
	wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	wfx.Format.nChannels = (WORD)g_ds_pcm_ch;
	wfx.Format.nSamplesPerSec = dsTryRate;
	wfx.Format.wBitsPerSample = (WORD)g_ds_pcm_bits;
	wfx.Format.nBlockAlign = (WORD)(wfx.Format.wBitsPerSample / 8 * wfx.Format.nChannels);
	wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
	wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	wfx.dwChannelMask = targetSpeakers;
	wfx.SubFormat = GUID_SUBTYPE_PCM;

	wavsam_depth = abs(wavsam_depth);
	wavbit2 = wavbit_sample_Hz;
	int i, iii = 0;
	double ik = 32.0;
	double d2 = 0;
	double il = 8.71712838;
	d2 = 0;

	for (i = 0; i <= 88; i++, iii++) { // 低音域用
		logtbl[i] = (int)(il * pow(2.0, (double)(iii) / ik));// / ((wavbit_sample_Hz / 44100.0)*16.0);// *(double)BUFSZH1 / (double)192000 / 4.0 + 1.0);
		//		double tl = wavbit_sample_Hz / 44100.0;
//		logtbl[i] = (int)((double)syuha[i]) / ((((87/tl) - (i/tl)) / (11.0) + 1.0)) / (tl);//(44100.0/ (double)wavbit_sample_Hz) * ((double)wavsam_depth / 8.0) *

		if (i < 20) {
			if (wavbit_sample_Hz > 90000)
				ik -= (0.15 + d2);// -(((double)wavbit_sample_Hz / 44100.0) / 50.0 - 0.02));
			else
				ik -= (0.20 + d2);// -(((double)wavbit_sample_Hz / 44100.0) / 50.0 - 0.02));
		}
		else {
			if (wavbit_sample_Hz > 90000)
				ik -= (0.14 + d2);// -(((double)wavbit_sample_Hz / 44100.0) / 50.0 - 0.02));
			else
				ik -= (0.18 + d2);// -(((double)wavbit_sample_Hz / 44100.0) / 50.0 - 0.02));
		}

		if (i != 0) {
			if (iii > 240) {
				break;
			}
			if (logtbl[i] <= logtbl[i - 1]) {
				i--; continue;
			}
		}
		//		if( logtbl[i] > BUFSZH1 -1 ) logtbl[i] = BUFSZH1 -1;

	}


	//    mmRes = waveOutOpen(&hwo,WAVE_MAPPER,&wfx1,(DWORD)(LPVOID)0,(DWORD)NULL,CALLBACK_NULL);

	fade1 = 0;
	//-------------------------------------------------------------------
	// WAV出力専用: DirectSoundをスキップしHandleNotifications_exportへ
	if (wavExportPath.GetLength() > 0) {
		// 2回目以降のため前回のccを確実に閉じる
		if (cc1 == 1) { cc.Close(); cc1 = 0; }
		// エクスポート用にcc.Openとヘッダ書き込みをここで実行（8131に依存しない）
		cc1 = 1;
		wl = 0;
		poss = poss2 = poss3 = poss4 = poss5 = poss6 = 0;
		playb = 0;
		lenl = 0;
		fade = 1.0f; fadeadd = 0.0f; fade1 = 0;
		reset = TRUE;
		CString outPath = wavExportPath;
		if (outPath.Right(4).MakeLower() != _T(".wav")) outPath += _T(".wav");
		if (cc.Open(outPath, CFile::modeCreate | CFile::modeReadWrite | CFile::typeBinary | CFile::shareExclusive, NULL) != TRUE) {
			cc1 = 0;
			m_saisai.EnableWindow(TRUE);
			endflg = 0;
			return;
		}
		// 2GB超対応(RF64)ヘッダを書き込み。確定は出力完了後に行う。
		WriteWavStreamHeaderRF64(cc);
		endflg = 0;
		SetTimer(9000, 10, NULL);
		endf = 0;
		if (pl && plw) { if (pl->m_loop.GetCheck() == TRUE) { if (loop2 == 0)loop2 = oggsize / 4; } }
		if (loop2 == 0) endf = 1;
		if (mode == -3 || mode == -8 || mode == -9 || mode == -10 || mode == 999) endf = 1;
		loopcnt = 0;
		HandleNotifications_export();
		// WAV出力時はm_douに関係なくccを閉じる（2回目以降のエクスポートでcc.Openが成功するため）
		if (cc1 == 1) {
			// ファイル実長から64bitでサイズ確定。2GB超なら自動的にRF64へ書き換える。
			FinalizeWavStreamHeaderRF64(cc);
			cc.Close();
			cc1 = 0;
		}
		stop1();
		m_saisai.EnableWindow(TRUE);
		endflg = 0;
		return;
	}
	//if (pAudioClient == NULL) {
	DSBUFFERDESC dsbd;
	flg0 = 0;
	for (;;) {
		ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
		dsbd.dwSize = sizeof(DSBUFFERDESC);
		dsbd.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME;// | DSBCAPS_CTRL3D;
		dsbd.dwBufferBytes = g_ds_buffer_bytes;
		wfx1.nSamplesPerSec = dsTryRate;
		wfx1.nAvgBytesPerSec = (DWORD)wfx1.nSamplesPerSec * (DWORD)wfx1.nBlockAlign;
		wfx.Format.nSamplesPerSec = dsTryRate;
		wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
		if (g_ds_pcm_ch > 2)
			dsbd.lpwfxFormat = (LPWAVEFORMATEX)&wfx;
		else
			dsbd.lpwfxFormat = &wfx1;
		//dsbd.guid3DAlgorithm = DS3DALG_HRTF_LIGHT;
		HRESULT r;
		ReleaseDXSound();
		if (WASAPIInit() == 0) init(m_hWnd, dsTryRate);
		r = m_ds->CreateSoundBuffer(&dsbd, &m_dsb1, NULL);
		if (m_dsb1 == NULL || m_p == NULL) {
			if (flg0 == 0) {
				CString s; s.Format(L"%d", dsTryRate);
				MessageBox(s + LL14(
					L"Hzのサンプリングレートにサウンドカードが対応していません\n低いサンプリングレートを試みます。\n少々時間が掛かる場合があります。", /* 日本語 */
					L"Hz sampling rate not supported by sound card.\nTrying lower rate.\nThis may take a while.", /* 英語 */
					L"Taux d'échantillonnage Hz non pris en charge par la carte son.\nEssai d'un taux inférieur.\nCela peut prendre un moment.", /* フランス語 */
					L"Frequenza di campionamento Hz non supportata dalla scheda audio.\nTentativo con frequenza inferiore.\nPotrebbe richiedere del tempo.", /* イタリア語 */
					L"Tasa de muestreo Hz no compatible con la tarjeta de sonido.\nIntentando una tasa inferior.\nEsto puede tardar un momento.", /* スペイン語 */
					L"Hz 샘플링 레이트를 사운드 카드가 지원하지 않습니다.\n낮은 샘플링 레이트를 시도합니다.\n시간이 다소 걸릴 수 있습니다.", /* 韓国語 */
					L"声卡不支持 Hz 采样率。\n正在尝试较低的采样率。\n可能需要一些时间。", /* 中国語 */
					L"بطاقة الصوت لا تدعم معدل العينة Hz.\nجاري تجربة معدل أقل.\nقد يستغرق هذا بعض الوقت.", /* アラビア語 */
					L"Звуковая карта не поддерживает частоту дискретизации Hz.\nПробуем более низкую частоту.\nЭто может занять некоторое время.", /* ロシア語 */
					L"Hz-Abtastrate wird von der Soundkarte nicht unterstützt.\nVersuche niedrigere Rate.\nDies kann einen Moment dauern.", /* ドイツ語 */
					L"Taxa de amostragem Hz não suportada pela placa de som.\nTentando uma taxa inferior.\nIsso pode demorar um momento.", /* ポルトガル語 */
					L"Hz-samplerate wordt niet ondersteund door de geluidskaart.\nLagere samplerate wordt geprobeerd.\nDit kan even duren.", /* オランダ語 */
					L"Karta dźwiękowa nie obsługuje częstotliwości próbkowania Hz.\nPróba niższej częstotliwości.\nMoże to chwilę potrwać.", /* ポーランド語 */
					L"Ses kartı Hz örnekleme hızını desteklemiyor.\nDaha düşük hız deneniyor.\nBu biraz zaman alabilir."), /* トルコ語 */
					LL14(
						L"ogg/wav簡易プレイヤ", /* 日本語タイトル */
						L"ogg/wav Simple Player",
						L"Lecteur Simple ogg/wav",
						L"Lettore Semplice ogg/wav",
						L"Reproductor Simple ogg/wav",
						L"ogg/wav 간편 플레이어",
						L"ogg/wav 简易播放器",
						L"مشغل ogg/wav البسيط",
						L"Простой Плеер ogg/wav",
						L"ogg/wav Einfacher Player",
						L"Player Simples ogg/wav",
						L"Eenvoudige ogg/wav Speler",
						L"Prosty Odtwarzacz ogg/wav",
						L"ogg/wav Basit Oynatıcı")); flg0 = 1;
			}

			dsTryRate -= 1000; if (dsTryRate <= 0) {
				MessageBox(LL14(
					L"0Hzまで試みましたが、対応するサンプリングレートが存在しませんでした。\nサウンドボード(カード)が存在していない可能性があります。", /* 日本語 */
					L"Tried down to 0Hz but no supported sampling rate found.\nSound card may not be present.", /* 英語 */
					L"Essayé jusqu'à 0Hz mais aucun taux d'échantillonnage compatible trouvé.\nLa carte son est peut-être absente.", /* フランス語 */
					L"Tentato fino a 0Hz ma nessuna frequenza di campionamento supportata trovata.\nLa scheda audio potrebbe essere assente.", /* イタリア語 */
					L"Intentado hasta 0Hz pero no se encontró tasa de muestreo compatible.\nEs posible que no haya tarjeta de sonido.", /* スペイン語 */
					L"0Hz까지 시도했지만, 지원하는 샘플링 레이트가 존재하지 않았습니다.\n사운드 카드(보드)가 존재하지 않을 가능性が 있습니다.", /* 韓国語 */
					L"已尝试至0Hz，但未找到支持的采样率。\n可能不存在声卡。", /* 中国語 */
					L"تمت التجربة حتى 0Hz ولكن لم يتم العثور على معدل عينة مدعوم.\nقد لا تكون بطاقة الصوت موجودة.", /* アラビア語 */
					L"Попытка до 0Hz, но поддерживаемая частота дискретизации не найдена.\nВозможно, звуковая карта отсутствует.", /* ロシア語 */
					L"Bis 0Hz versucht, aber keine unterstützte Abtastrate gefunden.\nDie Soundkarte ist möglicherweise nicht vorhanden.", /* ドイツ語 */
					L"Tentado até 0Hz mas nenhuma taxa de amostragem suportada foi encontrada.\nA placa de som pode estar ausente.", /* ポルトガル語 */
					L"Tot 0Hz geprobeerd maar geen ondersteunde samplerate gevonden.\nGeluidskaart is mogelijk niet aanwezig.", /* オランダ語 */
					L"Próbowano do 0Hz, ale nie znaleziono obsługiwanej częstotliwości próbkowania.\nKarta dźwiękowa może być nieobecna.", /* ポーランド語 */
					L"0Hz'e kadar denendi ancak desteklenen örnekleme hızı bulunamadı.\nSes kartı mevcut olmayabilir."), /* トルコ語 */
					LL14(
						L"ogg/wav簡易プレイヤ", /* 日本語タイトル */
						L"ogg/wav Simple Player",
						L"Lecteur Simple ogg/wav",
						L"Lettore Semplice ogg/wav",
						L"Reproductor Simple ogg/wav",
						L"ogg/wav 간편 플레이어",
						L"ogg/wav 简易播放器",
						L"مشغل ogg/wav البسيط",
						L"Простой Плеер ogg/wav",
						L"ogg/wav Einfacher Player",
						L"Player Simples ogg/wav",
						L"Eenvoudige ogg/wav Speler",
						L"Prosty Odtwarzacz ogg/wav",
						L"ogg/wav Basit Oynatıcı")); tagfile = fnn;
				m_saisai.EnableWindow(TRUE);
				endflg = 0;
				return;
			}
			wfx.Format.nSamplesPerSec = dsTryRate;
			wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
			wfx1.nSamplesPerSec = dsTryRate;
			wfx1.nAvgBytesPerSec = wfx1.nSamplesPerSec * wfx1.nBlockAlign;
			continue;
			endflg = 0;
			return;
		}
		for (i = 0; i < 10; i++) {
			r = m_dsb1->QueryInterface(IID_IDirectSoundBuffer8, (void**)&m_dsb);

			if (m_dsb == NULL) { DoEvent(); Sleep(100); continue; }
			else break;
		}
		if (m_dsb == NULL) {
			AfxMessageBox(LL14(
				L"DirectSoundが開けませんでした。", /* 日本語 */
				L"Could not open DirectSound.", /* 英語 */
				L"Impossible d'ouvrir DirectSound.", /* フランス語 */
				L"Impossibile aprire DirectSound.", /* イタリア語 */
				L"No se pudo abrir DirectSound.", /* スペイン語 */
				L"DirectSound를 열 수 없었습니다.", /* 韓国語 */
				L"无法打开DirectSound。", /* 中国語 */
				L"تعذر فتح DirectSound.", /* アラビア語 */
				L"Не удалось открыть DirectSound.", /* ロシア語 */
				L"DirectSound konnte nicht geöffnet werden.", /* ドイツ語 */
				L"Não foi possível abrir o DirectSound.", /* ポルトガル語 */
				L"Kan DirectSound niet openen.", /* オランダ語 */
				L"Nie można otworzyć DirectSound.", /* ポーランド語 */
				L"DirectSound açılamadı.")); /* トルコ語 */

			if (r == DSERR_ALLOCATED) {
				AfxMessageBox(LL14(
					L"優先レベルなどのリソースが他の呼び出しによって既に使用中であるため、要求は失敗した。",
					L"Request failed because resources such as priority level are already in use by another call.",
					L"La demande a échoué car des ressources telles que le niveau de priorité sont déjà utilisées par un autre appel.",
					L"La richiesta è fallita perché risorse come il livello di priorità sono già in uso da un'altra chiamata.",
					L"La solicitud falló porque recursos como el nivel de prioridad ya están en uso por otra llamada.",
					L"우선 순위 등의 리소스가 다른 호출에 의해 이미 사용 중이므로 요청이 실패했습니다.",
					L"请求失败，因为优先级等资源已被其他调用占用。",
					L"فشل الطلب لأن الموارد مثل مستوى الأولوية قيد الاستخدام بالفعل من قبل مكالمة أخرى.",
					L"Запрос не выполнен, так как ресурсы, такие как уровень приоритета, уже используются другим вызовом.",
					L"Anfrage fehlgeschlagen, da Ressourcen wie die Prioritätsstufe bereits von einem anderen Aufruf verwendet werden.",
					L"A solicitação falhou porque recursos como o nível de prioridade já estão em uso por outra chamada.",
					L"Verzoek mislukt omdat resources zoals prioriteitsniveau al in gebruik zijn door een andere aanroep.",
					L"Żądanie nie powiodło się, ponieważ zasoby takie jak poziom priorytetu są już używane przez inne wywołanie.",
					L"İstek, öncelik düzeyi gibi kaynaklar başka bir çağrı tarafından kullanıldığından başarısız oldu."));
			}
			else if (r == DSERR_CONTROLUNAVAIL) {
				AfxMessageBox(LL14(
					L"呼び出し元が要求するバッファ コントロール (ボリューム、パンなど) は利用できない。",
					L"Buffer control (volume, pan, etc.) requested by caller is not available.",
					L"Le contrôle de tampon (volume, panoramique, etc.) demandé par l'appelant n'est pas disponible.",
					L"Il controllo del buffer (volume, pan, ecc.) richiesto dal chiamante non è disponibile.",
					L"El control de búfer (volumen, paneo, etc.) solicitado por el llamador no está disponible.",
					L"호출자가 요청한 버퍼 컨트롤(볼륨, 팬 등)을 사용할 수 없습니다.",
					L"调用方请求的缓冲区控件（音量、声像等）不可用。",
					L"تحكم المخزن المؤقت (مستوى الصوت، التوازن، إلخ) الذي طلبه المتصل غير متاح.",
					L"Управление буфером (громкость, панорама и т.д.), запрошенное вызывающей стороной, недоступно.",
					L"Die vom Aufrufer angeforderte Puffersteuerung (Lautstärke, Balance usw.) ist nicht verfügbar.",
					L"O controle de buffer (volume, pan, etc.) solicitado pelo chamador não está disponível.",
					L"Bufferbesturing (volume, pan, etc.) gevraagd door de aanroeper is niet beschikbaar.",
					L"Kontrola bufora (głośność, panorama itp.) żądana przez wywołującego jest niedostępna.",
					L"Arayan tarafından istenen arabellek kontrolü (ses, pan vb.) kullanılamıyor."));
			}
			else if (r == DSERR_BADFORMAT) {
				AfxMessageBox(LL14(
					L"指定したウェーブ フォーマットはサポートされていない。",
					L"Specified wave format is not supported.",
					L"Le format d'onde spécifié n'est pas pris en charge.",
					L"Il formato wave specificato non è supportato.",
					L"El formato de onda especificado no es compatible.",
					L"지정된 웨이브 포맷은 지원되지 않습니다.",
					L"指定的波形格式不受支持。",
					L"تنسيق الموجة المحدد غير مدعوم.",
					L"Указанный формат звуковой волны не поддерживается.",
					L"Das angegebene Wellenformat wird nicht unterstützt.",
					L"O formato de onda especificado não é suportado.",
					L"Het opgegeven golfformaat wordt niet ondersteund.",
					L"Określony format fali nie jest obsługiwany.",
					L"Belirtilen dalga biçimi desteklenmiyor."));
			}
			else if (r == DSERR_INVALIDPARAM) {
				AfxMessageBox(LL14(
					L"無効なパラメータが関数に渡された。",
					L"Invalid parameter passed to function.",
					L"Un paramètre invalide a été passé à la fonction.",
					L"Un parametro non valido è stato passato alla funzione.",
					L"Se pasó un parámetro no válido a la función.",
					L"잘못된 매개변수가 함수에 전달되었습니다.",
					L"向函数传递了无效参数。",
					L"تم تمرير معلمة غير صالحة إلى الدالة.",
					L"В функцию передан недопустимый параметр.",
					L"Ein ungültiger Parameter wurde an die Funktion übergeben.",
					L"Um parâmetro inválido foi passado para a função.",
					L"Ongeldige parameter doorgegeven aan functie.",
					L"Do funkcji przekazano nieprawidłowy parametr.",
					L"Fonksiyona geçersiz parametre iletildi."));
			}
			else if (r == DSERR_NOAGGREGATION) {
				AfxMessageBox(LL14(
					L"このオブジェクトは COM 集合化をサポートしない。",
					L"This object does not support COM aggregation.",
					L"Cet objet ne prend pas en charge l'agrégation COM.",
					L"Questo oggetto non supporta l'aggregazione COM.",
					L"Este objeto no admite la agregación COM.",
					L"이 개체는 COM 집합화를 지원하지 않습니다.",
					L"此对象不支持COM聚合。",
					L"هذا الكائن لا يدعم تجميع COM.",
					L"Этот объект не поддерживает агрегирование COM.",
					L"Dieses Objekt unterstützt keine COM-Aggregation.",
					L"Este objeto não suporta agregação COM.",
					L"Dit object ondersteunt geen COM-aggregatie.",
					L"Ten obiekt nie obsługuje agregacji COM.",
					L"Bu nesne COM birleştirmesini desteklemiyor."));
			}
			else if (r == DSERR_OUTOFMEMORY) {
				AfxMessageBox(LL14(
					L"DirectSound サブシステムは、呼び出し元の要求を完了するための十分なメモリを割り当てられなかった。",
					L"DirectSound subsystem could not allocate enough memory to complete the request.",
					L"Le sous-système DirectSound n'a pas pu allouer suffisamment de mémoire pour terminer la demande.",
					L"Il sottosistema DirectSound non è riuscito ad allocare memoria sufficiente per completare la richiesta.",
					L"El subsistema DirectSound no pudo asignar suficiente memoria para completar la solicitud.",
					L"DirectSound 하위 시스템이 호출자의 요청을 완료하기 위한 충분한 메모리를 할당할 수 없습니다.",
					L"DirectSound子系统无法分配足够的内存来完成请求。",
					L"لم يتمكن نظام DirectSound الفرعي من تخصيص ذاكرة كافية لإكمال الطلب.",
					L"Подсистема DirectSound не смогла выделить достаточно памяти для выполнения запроса.",
					L"Das DirectSound-Subsystem konnte nicht genug Speicher zuweisen, um die Anfrage abzuschließen.",
					L"O subsistema DirectSound não pôde alocar memória suficiente para concluir a solicitação.",
					L"DirectSound-subsysteem kon niet genoeg geheugen toewijzen om het verzoek te voltooien.",
					L"Podsystem DirectSound nie mógł przydzielić wystarczającej ilości pamięci do realizacji żądania.",
					L"DirectSound alt sistemi isteği tamamlamak için yeterli bellek tahsis edemedi."));
			}
			else if (r == DSERR_UNINITIALIZED) {
				AfxMessageBox(LL14(
					L"他のメソッドを呼び出す前に IDirectSound::Initialize メソッドを呼び出さなかったか、呼び出しが成功しなかった。",
					L"IDirectSound::Initialize was not called before other methods, or the call failed.",
					L"IDirectSound::Initialize n'a pas été appelé avant les autres méthodes, ou l'appel a échoué.",
					L"IDirectSound::Initialize non è stato chiamato prima degli altri metodi, oppure la chiamata è fallita.",
					L"IDirectSound::Initialize no fue llamado antes que otros métodos, o la llamada falló.",
					L"다른 메서드를 호출하기 전에 IDirectSound::Initialize 메서드를 호출하지 않았거나 호출이 성공하지 않았습니다.",
					L"在调用其他方法之前未调用IDirectSound::Initialize，或调用失败。",
					L"لم يتم استدعاء IDirectSound::Initialize قبل الأساليب الأخرى، أو فشل الاستدعاء.",
					L"IDirectSound::Initialize не был вызван перед другими методами, или вызов завершился неудачно.",
					L"IDirectSound::Initialize wurde nicht vor anderen Methoden aufgerufen, oder der Aufruf ist fehlgeschlagen.",
					L"IDirectSound::Initialize não foi chamado antes de outros métodos, ou a chamada falhou.",
					L"IDirectSound::Initialize werd niet aangeroepen voor andere methoden, of de aanroep is mislukt.",
					L"IDirectSound::Initialize nie zostało wywołane przed innymi metodami lub wywołanie nie powiodło się.",
					L"IDirectSound::Initialize diğer yöntemlerden önce çağrılmadı veya çağrı başarısız oldu."));
			}
			else if (r == DSERR_UNSUPPORTED) {
				AfxMessageBox(LL14(
					L"呼び出した関数はこの時点ではサポートされていない。",
					L"The called function is not supported at this point.",
					L"La fonction appelée n'est pas prise en charge à ce stade.",
					L"La funzione chiamata non è supportata in questo momento.",
					L"La función llamada no es compatible en este momento.",
					L"호출된 함수는 이 시점에서는 지원되지 않습니다.",
					L"调用的函数在此处不受支持。",
					L"الدالة المستدعاة غير مدعومة في هذه المرحلة.",
					L"Вызванная функция не поддерживается в данный момент.",
					L"Die aufgerufene Funktion wird zu diesem Zeitpunkt nicht unterstützt.",
					L"A função chamada não é suportada neste momento.",
					L"De aangeroepen functie wordt op dit punt niet ondersteund.",
					L"Wywołana funkcja nie jest obsługiwana w tym miejscu.",
					L"Çağrılan fonksiyon bu noktada desteklenmiyor."));
			}
			else {}

			tagfile = fnn;
			m_saisai.EnableWindow(TRUE);
			endflg = 0;
			return;
		}

		if (m_dsb) {
			g_ds_pcm_rate = dsTryRate;
			{
				int srcBits = abs(wavsam_depth);
				if (!(srcBits == 8 || srcBits == 16 || srcBits == 24 || srcBits == 32))
					srcBits = 16;
				g_audioUpscaler.Configure(wavbit_sample_Hz, wavchannel, srcBits, g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits);
				g_pcm_upscale_active = g_audioUpscaler.IsActive() ? 1 : 0;
			}
			g_audioUpscaler.Reset();
			if (flg0 == 1) {
				CString s; s.Format(L"%d", g_ds_pcm_rate);
				MessageBox(s + LL14(
					L"Hzのサンプリングレートでヒットしましたため、該当サンプリングレートで演奏します。",
					L"Hz sampling rate matched; playing at that rate.",
					L"Taux d'échantillonnage Hz trouvé ; lecture à ce taux.",
					L"Frequenza di campionamento Hz trovata; riproduzione a quella frequenza.",
					L"Tasa de muestreo Hz encontrada; reproduciendo a esa tasa.",
					L"Hz 샘플링 레이트가 일치하여 해당 샘플링 레이트로 연주합니다.",
					L"已匹配到Hz采样率，将以该采样率进行播放。",
					L"تم العثور على معدل عينة بـ Hz؛ جاري التشغيل بهذا المعدل.",
					L"Частота дискретизации Hz найдена; воспроизведение на этой частоте.",
					L"Hz-Abtastrate gefunden; Wiedergabe mit dieser Rate.",
					L"Taxa de amostragem Hz encontrada; reproduzindo nessa taxa.",
					L"Hz-samplerate gevonden; afspelen op die rate.",
					L"Znaleziono częstotliwość próbkowania Hz; odtwarzanie z tą częstotliwością.",
					L"Hz örnekleme hızı eşleşti; o hızda oynatılıyor."),
					LL14(
						L"ogg/wav簡易プレイヤ",
						L"ogg/wav Simple Player",
						L"Lecteur Simple ogg/wav",
						L"Lettore Semplice ogg/wav",
						L"Reproductor Simple ogg/wav",
						L"ogg/wav 간편 플레이어",
						L"ogg/wav 简易播放器",
						L"مشغل ogg/wav البسيط",
						L"Простой Плеер ogg/wav",
						L"ogg/wav Einfacher Player",
						L"Player Simples ogg/wav",
						L"Eenvoudige ogg/wav Speler",
						L"Prosty Odtwarzacz ogg/wav",
						L"ogg/wav Basit Oynatıcı"));
				SetTimer(9100, 100, NULL);
				return;
			}
			break;
		}
	}
	//}
	//else {
	//		if (wavchannel > 2)
	//		WASAPIChange((LPWAVEFORMATEX)&wfx);
	//		else
	//			WASAPIChange(&wfx1);
	//}
	//m_dsb->QueryInterface(IID_IDirectSound3DBuffer, (LPVOID*)m_dsb3d);

	DWORD le = WAVDAStartLen;
	ttt = WAVDAStartLen;

	// stop1() は Join 前に stf=1 にする。CWread/adbuf の readBuffwav は
	// IsPlaybackStopRequested() で即 return するため、再生開始直前に必ず戻す。
	stf = 0;
	thn1 = FALSE;
	thn = TRUE;
	plf = 1;
	playf = 1;
	fade1 = 0; fade = 1.0f; fadeadd = 0.0f;
	poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
	g_oggPcmDecodePos = 0;
	g_oggRbPrimingNeed = OggRbLatencyReserveBytes();
	mcnt = mcnt1 = mcnt2 = mcnt3 = mcnt4 = mcnt5 = mcnt6 = 0;
	char* pdsb;
	lo = 0; loc = 0;

	if (loop1 == 0 && loop2 == 0) {
		{
			const int bpfT = PcmOutBytesPerFrame();
			const int fullLen = (bpfT > 0) ? (data_size / bpfT) : 0;
			m_time.SetSelection(0, fullLen);
		}
		m_time.Invalidate();
	}
	else {
		m_time.SetSelection(loop1, (loop1 + loop2));
		m_time.Invalidate();
	}

	int len1, len2, len3;
	if (true) {
		ULONG PlayCursor, WriteCursor = 0;
		playb = 0;
		g_oggPcmDecodePos = 0;
		if (m_dsb)m_dsb->GetCurrentPosition(&PlayCursor, &WriteCursor);//再生位置取得
		len1 = (int)WriteCursor;//書き込み範囲取得
		len2 = 0;
		if (len1 < 0) {
			const int bpf = (g_ds_pcm_ch > 0 && g_ds_pcm_bits >= 8) ? (g_ds_pcm_ch * (g_ds_pcm_bits / 8)) : 4;
			len1 = (int)(g_ds_buffer_bytes / (ULONG)((bpf > 0) ? bpf : 4));
			len2 = WriteCursor;
		}
		if (len2 < 0)
			len2 = 0;
		DispatchPlaywavFill(bufwav3, 0, len1, len2);
		if (m_dsb) {
			m_dsb->Lock(0, len1 + len2, (LPVOID*)&pdsb, (DWORD*)&len3, NULL, 0, 0);
			memcpy(pdsb, bufwav3, len3);
			m_dsb->Unlock(pdsb, len3, NULL, 0);
			m_dsb->SetVolume((savedata.dsvol - 1) * 10);
		}
		CFile f123;
		int flggg = 0;
		if (mode != -1) {
			if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
				f123.Close();
				if (IDYES == MessageBox(LL14(
					L"途中再生データが存在します。\n前回中断した部分から再生しますか？\nはい = 途中から再生\nいいえ = はじめから再生", /* 日本語 */
					L"Resume data exists.\nResume from where you left off?\nYes = Resume\nNo = Play from start", /* 英語 */
					L"Des données de reprise existent.\nReprendre là où vous vous êtes arrêté ?\nOui = Reprendre\nNon = Jouer depuis le début", /* フランス語 */
					L"Esistono dati di ripresa.\nRiprendere da dove ci si è fermati?\nSì = Riprendi\nNo = Riproduci dall'inizio", /* イタリア語 */
					L"Existen datos de reanudación.\n¿Reanudar desde donde lo dejó?\nSí = Reanudar\nNo = Reproducir desde el inicio", /* スペイン語 */
					L"중간 재생 데이터가 존재합니다.\n지난번 중단한 부분부터 재생하시겠습니까?\n예 = 중간부터 재생\n아니요 = 처음부터 재생", /* 韓国語 */
					L"存在中途播放数据。\n是否从上次中断处播放？\n是 = 从中途播放\n否 = 从头播放", /* 中国語 */
					L"بيانات الاستئناف موجودة.\nهل تريد الاستئناف من حيث توقفت؟\nنعم = استئناف\nلا = تشغيل من البداية", /* アラビア語 */
					L"Данные возобновления существуют.\nПродолжить с места остановки?\nДа = Продолжить\nНет = Играть с начала", /* ロシア語 */
					L"Fortsetzungsdaten vorhanden.\nVon der Unterbrechungsstelle fortfahren?\nJa = Fortsetzen\nNein = Von Anfang abspielen", /* ドイツ語 */
					L"Dados de retomada existem.\nRetomar de onde parou?\nSim = Retomar\nNão = Reproduzir do início", /* ポルトガル語 */
					L"Hervatgegevens aanwezig.\nHervatten waar u gebleven was?\nJa = Hervatten\nNee = Afspelen vanaf het begin", /* オランダ語 */
					L"Istnieją dane wznowienia.\nWznowić od miejsca przerwania?\nTak = Wznów\nNie = Odtwórz od początku", /* ポーランド語 */
					L"Devam verisi mevcut.\nKaldığınız yerden devam edilsin mi?\nEvet = Devam et\nHayır = Baştan oynat"), /* トルコ語 */
					LL14(
						L"再生確認", /* 日本語タイトル */
						L"Playback confirmation",
						L"Confirmation de lecture",
						L"Conferma riproduzione",
						L"Confirmación de reproducción",
						L"재생 확인",
						L"播放确认",
						L"تأكيد التشغيل",
						L"Подтверждение воспроизведения",
						L"Wiedergabebestätigung",
						L"Confirmação de reprodução",
						L"Afspeelbevestiging",
						L"Potwierdzenie odtwarzania",
						L"Oynatma onayı"), /* トルコ語タイトル */
					MB_YESNO)) {
					flggg = 1;
				}
				else {
					CFile::Remove(filen + _T(".save"));
				}
			}
			if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE && flggg == 1) {
				f123.Close();
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				//if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
				if (mode == -10) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
						f123.Read(&playb, sizeof(__int64));
						// 旧保存は「フレーム×4」で playb > 総フレーム になり得る
						if (oggsize > 0 && playb > (__int64)oggsize)
							playb /= 4;
						if (savedata.mp3orig) {
							mp3_.seek2(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
						}
						else {
							mp3_.seek(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
						}
						f123.Close();
					}
				}
				if (mode == 999) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
						f123.Read(&playb, sizeof(__int64));
						if (wav999_use_adbuf)
							seekadpcm((int)playb);
						else
							wav_.Seek(playb / (wavchannel * (wavsam_depth / 8)));
						f123.Close();
					}
				}
				if (mode == -2) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
						f123.Read(&aa1_, sizeof(double));
						pMainFrame1->seek((LONGLONG)(((float)((float)aa1_ * 100.0f * 100000.0f))));
						f123.Close();
					}
				}
			}
			else {
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				//if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
				if (pMainFrame1) { pMainFrame1->seek(0); }
			}
		}
		else {
			if (pMainFrame1) pMainFrame1->plays2();
			//if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
			if (pMainFrame1) { pMainFrame1->seek(0); }
		}
		syukai = 0;
		if (m_dsb) {
			m_dsb->Play(0, 0, DSBPLAY_LOOPING);
		}
		fade1 = 0;
		sflg = FALSE;
		// 通知スレッド起動直前は DoEvent しない（再入 play が旧デコーダを潰す）
		{
			const DWORD sflgWaitStart = GetTickCount();
			while (sflg != FALSE) {
				Sleep(1);
				if (GetTickCount() - sflgWaitStart >= 3000) {
					sflg = FALSE;
					break;
				}
			}
		}
		// BeginPlayback 内 Wait が stf を立てる前に、再生中フラグを確定しておく
		stf = 0;
		thn1 = FALSE;
		playf = 1;
		plf = 1;
		BeginPlaybackNotifyThread();
	}
	endflg = 0;
	g_dsWrittenBytes = 0;
	g_endWrittenBytes = 0;
	g_heardBytes = 0;
	g_outBytesPerFrame = PcmOutBytesPerFrame();

	// 全ての音声形式で、タイトル/アーティスト/アルバム/曲番号をファイルのタグから補完する。
	// ogg は従来タイトル(stitle)のみ、wav 等はプレイリスト由来のみだったため、空欄を埋める。
	// 既に各形式の読み込みで設定済みの値は上書きしない（タグが無いゲーム形式等は no-op）。
	if (mode == -1 || mode == -6 || mode == -7 || mode == -8 || mode == -9 || mode == -10 || mode == 999) {
		FileTagFields _tf;
		ReadFileTagFields(filen, _tf);
		if (stitle.IsEmpty() && !_tf.title.IsEmpty())   stitle = _tf.title;
		if (tagname.IsEmpty() && !_tf.artist.IsEmpty()) tagname = _tf.artist;
		if (tagalbum.IsEmpty() && !_tf.album.IsEmpty()) tagalbum = _tf.album;
		if (tagfile.IsEmpty() && !_tf.title.IsEmpty())  tagfile = _tf.title;
		tagtrack = _tf.track;
	}

	SetTimer(9000, 10, NULL);
	//	::SetPriorityClass(m_thread, HIGH_PRIORITY_CLASS);
	endf = 0;
	if (pl && plw) { if (pl->m_loop.GetCheck() == TRUE) { if (loop2 == 0)loop2 = oggsize / 4; } }
	if (loop2 == 0) endf = 1;
	if (mode == -3 || mode == -8 || mode == -9 || mode == -10 || mode == 999) endf = 1;
	loopcnt = 0;
	if (pl && plw) {
		int plc = 1;
		if (mode == -10)
			plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, (oggsize / (2 * wavchannel * wavbit_sample_Hz / 4) / ((mode == -9) ? 4 : 1)), 1);
		else if (mode == 999)
			plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, (wavbit_sample_Hz > 0) ? (int)(loop2 / wavbit_sample_Hz) : 0, 1);
		else if (mode == -9 || mode == -8 || mode == -7) {
			double wavv[] = { 0,1.0,2.0,2.0,2.0,2.0,2.0 };//(double)(wavbit2/wavv[wavchannel])
			plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, (int)(
				(double)oggsize / (double)(wavbit_sample_Hz * 2 * wavv[wavchannel]) / (double)(wavsam_depth / 16.0f)
				), 1);
		}
		else if (mode == -3) {
			if (oggsize == 0)
				if (mode == -3 && fnn.Find(L".hes") == -1)
					plc = pl->Add(fnn, mode, loop1, loop2, tagname, tagalbum, filen, 0, -1, 1);
				else
					plc = pl->chk(fnn, mode, tagname, filen, 0);
			else
				if (mode == -3 && fnn.Find(L".hes") == -1)
					plc = pl->Add(fnn, mode, loop1, loop2, tagname, tagalbum, filen, 0, oggsize / (2 * wavchannel * wavbit_sample_Hz), 1);
				else
					plc = pl->chk(fnn, mode, tagname, filen, 0);
		}
		else if (!((pMainFrame1 && mode == -1) || mode == -3))
			plc = pl->Add(fnn, mode, loop1, loop2, stitle, tagalbum, filen, ret2, oggsize / (2 * wavchannel * wavbit_sample_Hz));
		else
			plc = pl->chk(fnn, mode, tagname, filen, 0);

		{
			int syncIdx = plc;
			if (syncIdx < 0 && pl->playcnt > 0)
				syncIdx = pl->playcnt - 1;
			if (syncIdx >= 0 && syncIdx < pl->playcnt) {
				pl->pc[syncIdx].loop1 = loop1;
				pl->pc[syncIdx].loop2 = loop2;
				if (mode != -10 && mode != 999 && mode != -9 && mode != -8 && mode != -7 && mode != -3) {
					if (oggsize > 0 && wavchannel > 0 && wavbit_sample_Hz > 0)
						pl->pc[syncIdx].time = oggsize / (2 * wavchannel * wavbit_sample_Hz);
				}
			}
		}

		if (plc == -1) {
			int i = pl->m_lc.GetItemCount() - 1;
			plcnt = i;
			pl->SIcon(i);
		}
		else {
			plcnt = plc;
			pl->SIcon(plc);
		}
	}
	// plcnt/SIcon 確定後に曲ごとパラメータを復元(通知スレッド開始より後が正しい)
	SongParams_OnSongStarted();
	m_saisai.EnableWindow(TRUE); playy = 1; ResetPauseButtonUi();
	SetTimer(1250, 100, NULL);
	fade1 = 0;
	// メディアプレイヤーモードではメイン画面を前面化しない(裏に隠したまま)
	if (savedata.playerMode != 1) {
		if (maini) maini->SetActiveWindow();
		SetActiveWindow();
	}
	m_jacketFocus = 0.0;
	m_lastTick = 0;
	LoadJacket(mp3file);
}

void COggDlg::SetAdd(CString fnn, int mode, int loop1, int loop2, CString filen, int ret2, REFTIME time)
{
	if (pl && plw) {
		int plc;
		plc = pl->Add(fnn, mode, loop1, loop2, _T(""), _T(""), filen, ret2, (int)time);
		if (plc == -1) {
			int i = pl->m_lc.GetItemCount() - 1;
			plcnt = i;
			pl->SIcon(i);
		}
		else {
			plcnt = plc;
			pl->SIcon(plc);
		}
	}
}

BOOL COggDlg::ExportToWav(playlistdata0* pc, CString outputPath, int loopCount, const WavExportOptions* opts)
{
	if (!pc || outputPath.IsEmpty() || loopCount < 1) return FALSE;
	WavExportOptions localOpts = {};
	if (opts) localOpts = *opts;
	else {
		localOpts.fadeEnable = savedata.wav_export_fade;
		localOpts.fadeSec = savedata.wav_export_fade_sec > 0 ? savedata.wav_export_fade_sec : 15;
		localOpts.trimLeadEnable = savedata.wav_export_trim_lead;
		localOpts.trimKeepSec = savedata.wav_export_trim_keep_sec > 0 ? savedata.wav_export_trim_keep_sec : 1;
	}
	// 2回目以降：初回と同じ状態へリセット（前回のエクスポートで変更されたグローバルを戻す）
	if (cc1 == 1) { cc.Close(); cc1 = 0; }
	wl = 0;
	poss = poss2 = poss3 = poss4 = poss5 = poss6 = 0;
	playb = 0;
	lenl = 0;
	fade = 1.0f; fadeadd = 0.0f; fade1 = 0;
	reset = TRUE;
	// グローバル変数を設定
	filen = pc->fol;
	fnn = pc->name;
	mode = modesub = pc->sub;
	loop1 = pc->loop1;
	loop2 = pc->loop2;
	ret2 = pc->ret2;
	wavExportPath = outputPath;
	wavExportLoopCount = loopCount;
	int saveloop_bak = savedata.saveloop;
	savedata.saveloop = 1;  // ループを有効にしてwavExportLoopCountで制御
	g_wavExportDataBytes = 0;
	play();
	// 成否はファイル実長由来の g_wavExportDataBytes で判定（trim/fade後も正しい）
	BOOL ok = (g_wavExportDataBytes > 0 && cc1 == 0);
	if (ok && localOpts.trimLeadEnable)
		ok = TrimLeadingSilenceWavFile(outputPath, localOpts.trimKeepSec);
	if (ok && localOpts.fadeEnable) {
		const int fadeSec = localOpts.fadeSec > 0 ? localOpts.fadeSec : 15;
		ok = ApplyTailFadeOutWavFile(outputPath, fadeSec);
	}
	wavExportPath.Empty();
	wavExportLoopCount = 0;
	savedata.saveloop = saveloop_bak;
	return ok;
}

HANDLE hp;
//スレッド
void CWread::wavread()
{
	DWORD dl, dwDataLen = (WAVDALen / OUTPUT_BUFFER_NUM) * 4; dl = dwDataLen;
	if (mode == 21) {
		CString a = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		switch (_ttoi(a.Mid(2, 4))) {
		case 8001:
			a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"특과 클래스 《VII반》", L"特科班《VII组》", L"الفئة السابعة", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klasse VII", L"Klasa VII", L"Sınıf VII");
			break;
		case 8002:
			a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours de l'Avant", L"Sempre Avanti", L"Siempre Adelante", L"오직 한결같이, 앞으로", L"唯有向前", L"دائما إلى الأمام", L"Всегда Вперёд", L"Immer Vorwärts", L"Sempre em Frente", L"Altijd Vooruit", L"Zawsze Naprzód", L"Hep İleri");
			break;
		case 8100:
			a = LL14(L"近郊都市トリスタ", L"Suburban City Trista", L"Ville de Banlieue Trista", L"Città Suburbana Trista", L"Ciudad Suburbana Trista", L"근교 도시 트리스타", L"近郊城市特里斯塔", L"مدينة تريستا الضاحية", L"Пригородный Город Триста", L"Vorortstadt Trista", L"Cidade Suburbana Trista", L"Buitenstad Trista", L"Miasto Podmiejskie Trista", L"Banliyö Şehri Trista");
			break;
		case 8101:
			a = LL14(L"交易町ケルディック", L"Trading Town Celdic", L"Ville Marchande Celdic", L"Città Commerciale Celdic", L"Ciudad Comercial Celdic", L"교역 마을 켈딕", L"交易小镇塞尔迪克", L"بلدة سيلديك التجارية", L"Торговый Город Селдик", L"Handelsstadt Celdic", L"Cidade Comercial Celdic", L"Handelsstad Celdic", L"Miasto Handlowe Celdic", L"Ticaret Kasabası Celdic");
			break;
		case 8102:
			a = LL14(L"翡翠の公都バリアハート", L"Jade Capital Bareahard", L"Capitale de Jade Bareahard", L"Capitale di Giada Bareahard", L"Capital de Jade Bareahard", L"비취의 공도 바리아하트", L"翡翠公都巴里亚哈特", L"عاصمة اليشم باري هارد", L"Нефритовая Столица Бэрихард", L"Jade-Hauptstadt Bareahard", L"Capital de Jade Bareahard", L"Jade-Hoofdstad Bareahard", L"Jadeitowa Stolica Bareahard", L"Yeşim Başkenti Bareahard");
			break;
		case 8103:
			a = LL14(L"湖畔の街レグラム", L"Lakeside Town Legram", L"Ville au Bord du Lac Legram", L"Città Lacustre Legram", L"Ciudad Junto al Lago Legram", L"호반의 거리 레그람", L"湖畔小镇勒格拉姆", L"بلدة ليغرام بجانب البحيرة", L"Город у Озера Леграм", L"Seestadt Legram", L"Cidade à Beira do Lago Legram", L"Meerstad Legram", L"Miasto nad Jeziorem Legram", L"Göl Kenarı Kasabası Legram");
			break;
		case 8104:
			a = LL14(L"黒銀の鋼都ルーレ", L"Iron City Roer", L"Cité d'Acier Roer", L"Città d'Acciaio Roer", L"Ciudad de Acero Roer", L"흑은의 강도 루레", L"黑银钢都卢雷", L"مدينة روير الحديدية", L"Стальной Город Рур", L"Stahlstadt Roer", L"Cidade de Aço Roer", L"Staalstad Roer", L"Stalowe Miasto Roer", L"Çelik Şehri Roer");
			break;
		case 8106:
			a = LL14(L"遊牧民の集落", L"Nomad Settlement", L"Campement Nomade", L"Accampamento Nomade", L"Asentamiento Nómada", L"유목민의 집락", L"游牧民聚落", L"مستوطنة البدو", L"Поселение Кочевников", L"Nomadensiedlung", L"Assentamento Nômade", L"Nomadennederzetting", L"Osada Koczowników", L"Göçebe Yerleşimi");
			break;
		case 8107:
			a = LL14(L"緋の帝都ヘイムダル", L"Crimson Capital Heimdallr", L"Capitale Cramoisie Heimdallr", L"Capitale Cremisi Heimdallr", L"Capital Carmesí Heimdallr", L"비의 제도 헤임달", L"绯之帝都海姆达尔", L"عاصمة القرمزي هايمدال", L"Малиновая Столица Хеймдалл", L"Purpurrote Hauptstadt Heimdallr", L"Capital Carmesim Heimdallr", L"Karmozijnrode Hoofdstad Heimdallr", L"Karmazynowa Stolica Heimdallr", L"Kırmızı Başkent Heimdallr");
			break;
		case 8108:
			a = LL14(L"癒しの我が家", L"Healing Home", L"Foyer Apaisant", L"Casa Guaritrice", L"Hogar Sanador", L"치유의 우리 집", L"治愈的家", L"المنزل الشافي", L"Исцеляющий Дом", L"Heilendes Zuhause", L"Lar Curador", L"Helend Thuis", L"Uzdrawiający Dom", L"İyileştirici Ev");
			break;
		case 8109:
			a = LL14(L"ダイニングバー《F》", L"Dining Bar F", L"Bar-Restaurant F", L"Dining Bar F", L"Bar Comedor F", L"다이닝 바 《F》", L"餐厅酒吧《F》", L"بار الطعام F", L"Обеденный Бар F", L"Dining Bar F", L"Bar Restaurante F", L"Dining Bar F", L"Bar Restauracyjny F", L"Yemek Barı F");
			break;
		case 8110:
			a = LL14(L"常在戦場の気概", L"Ever-Present War Spirit", L"Esprit de Guerre Omniprésent", L"Spirito di Guerra Onnipresente", L"Espíritu de Guerra Omnipresente", L"상재전장의 기개", L"常在战场的气概", L"روح الحرب الدائمة", L"Вечный Боевой Дух", L"Allgegenwärtiger Kriegsgeist", L"Espírito de Guerra Onipresente", L"Altijd Aanwezige Oorlogsgeest", L"Wszechobecny Duch Wojenny", L"Her Zaman Savaş Ruhu");
			break;
		case 8111:
			a = LL14(L"ガレリアの巨壁", L"Garelia Fortress", L"Forteresse de Garelia", L"Fortezza di Garelia", L"Fortaleza de Garelia", L"가렐리아의 거벽", L"加勒利亚巨壁", L"قلعة غاريليا", L"Крепость Гарелия", L"Festung Garelia", L"Fortaleza de Garelia", L"Vesting Garelia", L"Twierdza Garelia", L"Garelia Kalesi");
			break;
		case 8120:
			a = LL14(L"足湯の温もり", L"Foot Bath Warmth", L"Chaleur du Bain de Pieds", L"Calore del Pediluvio", L"Calidez del Baño de Pies", L"족탕의 온기", L"足浴的温暖", L"دفء حمام القدمين", L"Тепло Ножной Ванны", L"Wärme des Fußbades", L"Calor do Banho de Pés", L"Warmte van het Voetbad", L"Ciepło Kąpieli Stóp", L"Ayak Banyosunun Sıcaklığı");
			break;
		case 8121:
			a = LL14(L"静寂の郷", L"Silent Village", L"Village Silencieux", L"Villaggio Silenzioso", L"Pueblo Silencioso", L"정적의 고을", L"静寂之乡", L"القرية الصامتة", L"Тихая Деревня", L"Stilles Dorf", L"Vila Silenciosa", L"Stil Dorp", L"Ciche Miasteczko", L"Sessiz Köy");
			break;
		case 8122:
			a = LL14(L"明日への休息", L"Rest for Tomorrow", L"Repos pour Demain", L"Riposo per Domani", L"Descanso para Mañana", L"내일로의 휴식", L"为明日而休息", L"راحة ليوم غد", L"Отдых ради Завтра", L"Ruhe für Morgen", L"Descanso para Amanhã", L"Rust voor Morgen", L"Odpoczynek na Jutro", L"Yarın İçin Dinlenme");
			break;
		case 8123:
			a = LL14(L"春の陽射し", L"Spring Sunshine", L"Soleil de Printemps", L"Sole Primaverile", L"Sol de Primavera", L"봄의 햇살", L"春日阳光", L"أشعة شمس الربيع", L"Весеннее Солнце", L"Frühlingssonne", L"Sol de Primavera", L"Lentezonnestralen", L"Wiosenne Słońce", L"İlkbahar Güneşi");
			break;
		case 8125:
			a = LL14(L"カレイジャス発進！", L"Courageous Launch!", L"Décollage du Courageux !", L"Lancio del Courageous!", L"¡Lanzamiento del Courageous!", L"카레이저스 발진!", L"无畏号出发！", L"انطلاق الشجاعة!", L"Старт Отважного!", L"Courageous startet!", L"Lançamento do Courageous!", L"Courageous lanceert!", L"Start Courageous!", L"Courageous Fırlatıldı!");
			break;
		case 8126:
			a = LL14(L"目覚める意志", L"Awakening Will", L"Volonté qui s'Éveille", L"Volontà che si Risveglia", L"Voluntad que Despierta", L"깨어나는 의지", L"觉醒的意志", L"الإرادة المستيقظة", L"Пробуждающаяся Воля", L"Erwachender Wille", L"Vontade que Desperta", L"Ontwakende Wil", L"Przebudzająca się Wola", L"Uyanış İradesi");
			break;
		case 8127:
			a = LL14(L"白銀の巨船", L"Silver Ship", L"Vaisseau d'Argent", L"Nave d'Argento", L"Nave de Plata", L"백은의 거선", L"白银巨船", L"السفينة الفضية", L"Серебряный Корабль", L"Silbernes Schiff", L"Navio de Prata", L"Zilveren Schip", L"Srebrny Okręt", L"Gümüş Gemi");
			break;
		case 8150:
			a = LL14(L"放課後の時間", L"After School", L"Après l'École", L"Dopo Scuola", L"Después de la Escuela", L"방과 후의 시간", L"放学后的时光", L"وقت ما بعد المدرسة", L"После Уроков", L"Nach der Schule", L"Depois da Escola", L"Na School", L"Po Szkole", L"Okul Sonrası");
			break;
		case 8152:
			a = LL14(L"さわやかな朝", L"Refreshing Morning", L"Matin Rafraîchissant", L"Mattino Rinfrescante", L"Mañana Refrescante", L"상쾌한 아침", L"清爽的早晨", L"صباح منعش", L"Бодрящее Утро", L"Erfrischender Morgen", L"Manhã Refrescante", L"Verfrissende Ochtend", L"Orzeźwiający Poranek", L"Ferah Sabah");
			break;
		case 8153:
			a = LL14(L"雨音の学院", L"Rain-sound Academy", L"Académie sous la Pluie", L"Accademia della Pioggia", L"Academia Bajo la Lluvia", L"빗소리의 학원", L"雨声学院", L"الأكاديمية تحت المطر", L"Академия Дождя", L"Regen-Akademie", L"Academia da Chuva", L"Regen-Academie", L"Akademia Deszczu", L"Yağmur Sesi Akademisi");
			break;
		case 8154:
			a = LL14(L"爽やかな陽射し", L"Clear Sunshine", L"Soleil Clair", L"Sole Limpido", L"Sol Despejado", L"상쾌한 햇살", L"清爽的阳光", L"أشعة الشمس الصافية", L"Ясное Солнце", L"Klarer Sonnenschein", L"Sol Claro", L"Helder Zonlicht", L"Jasne Słońce", L"Berrak Güneş Işığı");
			break;
		case 8156:
			a = LL14(L"トールズ士官学院祭", L"Thors Academy Festival", L"Festival de l'Académie Thors", L"Festival dell'Accademia Thors", L"Festival de la Academia Thors", L"토르즈 사관학원제", L"托尔斯士官学院祭", L"مهرجان أكاديمية ثورز", L"Праздник Академии Торс", L"Thors-Akademie-Festival", L"Festival da Academia Thors", L"Thors Academie Festival", L"Festiwal Akademii Thors", L"Thors Akademisi Festivali");
			break;
		case 8158:
			a = LL14(L"青空の開放感", L"Open Sky", L"Ciel Ouvert", L"Cielo Aperto", L"Cielo Abierto", L"푸른 하늘의 개방감", L"蓝天的开放感", L"السماء المفتوحة", L"Открытое Небо", L"Offener Himmel", L"Céu Aberto", L"Open Lucht", L"Otwarte Niebo", L"Açık Gökyüzü");
			break;
		case 8159:
			a = LL14(L"自由行動日", L"Free Day", L"Journée Libre", L"Giorno Libero", L"Día Libre", L"자유 행동일", L"自由行动日", L"يوم حر", L"Свободный День", L"Freier Tag", L"Dia Livre", L"Vrije Dag", L"Wolny Dzień", L"Serbest Gün");
			break;
		case 8200:
			a = LL14(L"異郷の空", L"Foreign Sky", L"Ciel Étranger", L"Cielo Straniero", L"Cielo Extranjero", L"이향의 하늘", L"异乡的天空", L"سماء غريبة بعيدة", L"Чужое Небо", L"Fremder Himmel", L"Céu Estrangeiro", L"Vreemde Hemel", L"Obce Niebo", L"Yabancı Gökyüzü");
			break;
		case 8201:
			a = LL14(L"峡谷道を往く", L"Through the Canyon", L"À Travers le Canyon", L"Attraverso il Canyon", L"A Través del Cañón", L"협곡길을 가다", L"穿越峡谷之道", L"عبر الوادي", L"Сквозь Каньон", L"Durch die Schlucht", L"Através do Canyon", L"Door de Kloof", L"Przez Kanion", L"Kanyondan Geçerek");
			break;
		case 8202:
			a = LL14(L"精霊の小道", L"Spirit Path", L"Chemin des Esprits", L"Sentiero degli Spiriti", L"Senda de los Espíritus", L"정령의 오솔길", L"精灵小道", L"طريق الأرواح", L"Тропа Духов", L"Geisterpfad", L"Caminho dos Espíritos", L"Geestenpad", L"Ścieżka Duchów", L"Ruh Yolu");
			break;
		case 8203:
			a = LL14(L"蒼穹の大地", L"Azure Skies Land", L"Terre du Ciel Azuré", L"Terra del Cielo Azzurro", L"Tierra del Cielo Azul", L"창궁의 대지", L"苍穹大地", L"أرض السماء الزرقاء", L"Земля Лазурного Неба", L"Land des Azurhimmels", L"Terra do Céu Azul", L"Land van de Azuurblauwe Lucht", L"Kraina Lazurowego Nieba", L"Gök Mavisi Topraklar");
			break;
		case 8210:
			a = LL14(L"戦火を越えて", L"Beyond the Flames of War", L"Au-Delà des Flammes de la Guerre", L"Oltre le Fiamme della Guerra", L"Más Allá de las Llamas de la Guerra", L"전화를 넘어", L"超越战火", L"ما وراء لهيب الحرب", L"За Пламенем Войны", L"Jenseits der Kriegsflammen", L"Além das Chamas da Guerra", L"Voorbij de Vlammen van de Oorlog", L"Poza Płomieniami Wojny", L"Savaşın Alevlerinin Ötesinde");
			break;
		case 8212:
			a = L"Trudge Along";
			break;
		case 8213:
			a = LL14(L"冬の訪れ", L"Arrival of Winter", L"Arrivée de l'Hiver", L"Arrivo dell'Inverno", L"Llegada del Invierno", L"겨울의 방문", L"冬日将来", L"وصول الشتاء", L"Приход Зимы", L"Ankunft des Winters", L"Chegada do Inverno", L"Komst van de Winter", L"Nadejście Zimy", L"Kışın Gelişi");
			break;
		case 8300:
			a = LL14(L"旧校舎の謎", L"Old Schoolhouse Mystery", L"Mystère de l'Ancienne École", L"Mistero della Vecchia Scuola", L"Misterio del Antiguo Edificio Escolar", L"구교사의 수수께끼", L"旧校舍之谜", L"سر مبنى المدرسة القديم", L"Загадка Старого Корпуса", L"Geheimnis des alten Schulgebäudes", L"Mistério do Antigo Prédio Escolar", L"Mysterie van het Oude Schoolgebouw", L"Tajemnica Starego Budynku Szkolnego", L"Eski Okul Binasının Gizemi");
			break;
		case 8301:
			a = LL14(L"探索", L"Exploration", L"Exploration", L"Esplorazione", L"Exploración", L"탐색", L"探索", L"استكشاف", L"Исследование", L"Erkundung", L"Exploração", L"Verkenning", L"Eksploracja", L"Keşif");
			break;
		case 8302:
			a = LL14(L"深淵へ向かう", L"Toward the Abyss", L"Vers l'Abîme", L"Verso l'Abisso", L"Hacia el Abismo", L"심연을 향해", L"走向深渊", L"نحو الهاوية", L"К Бездне", L"In den Abgrund", L"Rumo ao Abismo", L"Naar de Afgrond", L"Ku Otchłani", L"Uçuruma Doğru");
			break;
		case 8303:
			a = LL14(L"聖女の城", L"Saint's Castle", L"Château de la Sainte", L"Castello della Santa", L"Castillo de la Santa", L"성녀의 성", L"圣女之城", L"قلعة القديسة", L"Замок Святой", L"Schloss der Heiligen", L"Castelo da Santa", L"Kasteel van de Heilige", L"Zamek Świętej", L"Aziz Kale");
			break;
		case 8304:
			a = LL14(L"明日を掴むために", L"To Seize Tomorrow", L"Pour Saisir Demain", L"Per Afferrare il Domani", L"Para Aferrar el Mañana", L"내일을 잡기 위해", L"为了抓住明日", L"لإمساك الغد", L"Чтобы Схватить Завтра", L"Um Morgen zu Greifen", L"Para Agarrar o Amanhã", L"Om Morgen te Grijpen", L"By Pochwycić Jutro", L"Yarını Yakalamak İçin");
			break;
		case 8305:
			a = LL14(L"地下に眠る遺構", L"Ruins Beneath", L"Ruines Souterraines", L"Rovine Sotterranee", L"Ruinas Subterráneas", L"지하에 잠든 유구", L"眠地下的遗构", L"الأطلال تحت الأرض", L"Подземные Руины", L"Unterirdische Ruinen", L"Ruínas Subterrâneas", L"Ondergrondse Ruines", L"Podziemne Ruiny", L"Yeraltındaki Harabeler");
			break;
		case 8308:
			a = LL14(L"世の礎たるために", L"To Be the World's Foundation", L"Pour Être le Fondement du Monde", L"Per Essere il Fondamento del Mondo", L"Para Ser el Fundamento del Mundo", L"세상의 초석이 되기 위해", L"成为世界的基石", L"لنكون أساس العالم", L"Чтобы Стать Основой Мира", L"Um das Fundament der Welt zu Sein", L"Para Ser o Fundamento do Mundo", L"Om het Fundament van de Wereld te Zijn", L"By Być Fundamentem Świata", L"Dünyanın Temeli Olmak İçin");
			break;
		case 8310:
			a = LL14(L"精霊窟", L"Spirit Cave", L"Grotte des Esprits", L"Grotta degli Spiriti", L"Cueva de los Espíritus", L"정령굴", L"精灵窟", L"كهف الأرواح", L"Пещера Духов", L"Geisterhöhle", L"Caverna dos Espíritos", L"Geestesgrot", L"Jaskinia Duchów", L"Ruh Mağarası");
			break;
		case 8311:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8312:
			a = L"Phantasmal Blaze";
			break;
		case 8313:
			a = LL14(L"夢幻回廊", L"Phantasmagoria Corridor", L"Couloir Fantasmagorique", L"Corridoio Fantasmagorico", L"Corredor Fantasmagórico", L"몽환 회랑", L"梦幻回廊", L"رواق الفانتازيا", L"Фантасмагорический Коридор", L"Phantasmagorischer Korridor", L"Corredor Fantasmagórico", L"Fantasmagorische Gang", L"Fantasmagoryczny Korytarz", L"Fantazmagori Koridoru");
			break;
		case 8315:
			a = LL14(L"幻煌", L"Phantom Radiance", L"Éclat Fantôme", L"Splendore Fantasma", L"Resplandor Fantasma", L"환황", L"幻煌", L"التألق الخيالي", L"Призрачное Сияние", L"Phantomglanz", L"Resplendor Fantasma", L"Fantoomglinstering", L"Blask Widma", L"Hayalet Işıltı");
			break;
		case 8400:
			a = L"The Glint of Cold Steel";
			break;
		case 8401:
			a = L"Tie a Link of ARCUS!";
			break;
		case 8402:
			a = L"Belief";
			break;
		case 8403:
			a = L"Even if Driven to the Wall";
			break;
		case 8404:
			a = L"Eliminate Crisis!";
			break;
		case 8405:
			a = L"Exceed!";
			break;
		case 8406:
			a = L"Don't be Defeated by a Friend!";
			break;
		case 8407:
			a = L"Machinery Attack";
			break;
		case 8408:
			a = LL14(L"巨イナルチカラ", L"Colossal Power", L"Puissance Colossale", L"Potere Colossale", L"Poder Colosal", L"거대한 힘", L"巨大的力量", L"قوة هائلة", L"Колоссальная Сила", L"Kolossale Kraft", L"Poder Colossal", L"Kolossale Kracht", L"Kolosalna Siła", L"Devasa Güç");
			break;
		case 8409:
			a = L"The Decisive Collision";
			break;
		case 8410:
			a = LL14(L"この手で道を切り拓く!", L"Carve Our Path with These Hands!", L"Traçons Notre Chemin de Ces Mains !", L"Tracciamo il Nostro Cammino con Queste Mani!", L"¡Abramos Nuestro Camino con Estas Manos!", L"이 손으로 길을 개척한다!", L"用这双手开拓道路!", L"سنشق طريقنا بأيدينا!", L"Проложим Путь Этими Руками!", L"Mit diesen Händen unseren Weg bahnen!", L"Abrir Nosso Caminho com Estas Mãos!", L"Ons Pad Banen met Deze Handen!", L"Torujemy Drogę Tymi Rękami!", L"Bu Ellerle Yolumuzu Açalım!");
			break;
		case 8411:
			a = LL14(L"赤点です...", L"Failed...", L"Échec...", L"Fallito...", L"Reprobado...", L"낙제입니다...", L"挂科了...", L"فشل...", L"Провалено...", L"Durchgefallen...", L"Reprovado...", L"Gezakt...", L"Oblany...", L"Başarısız...");
			break;
		case 8412:
			a = L"Unknown Threat";
			break;
		case 8413:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8420:
			a = L"Heated Mind";
			break;
		case 8421:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8423:
			a = L"Impatient";
			break;
		case 8424:
			a = L"Severe Blow";
			break;
		case 8426:
			a = L"Transcend Beat";
			break;
		case 8429:
			a = L"Blue Destination";
			break;
		case 8430:
			a = L"Heteromorphy";
			break;
		case 8431:
			a = LL14(L"輝ける明日へ", L"Toward a Shining Tomorrow", L"Vers un Lendemain Radieux", L"Verso un Domani Splendente", L"Hacia un Mañana Brillante", L"빛나는 내일을 향해", L"走向光辉的明天", L"نحو غد مشرق", L"К Сияющему Завтра", L"Einem Strahlenden Morgen Entgegen", L"Rumo a um Amanhã Brilhante", L"Naar een Stralende Toekomst", L"Ku Jaśniejszemu Jutru", L"Parlayan Bir Yarın Doğu");
			break;
		case 8435:
			a = LL14(L"迫る巨影", L"Approaching Giant Shadow", L"Ombre Géante qui Approche", L"Ombra Gigante che si Avvicina", L"Sombra Gigante que se Acerca", L"다가오는 거영", L"逼近的巨影", L"الظل العملاق يقترب", L"Приближающаяся Гигантская Тень", L"Nahende Riesenschatten", L"Sombra Gigante se Aproximando", L"Naderende Reusachtige Schaduw", L"Zbliżający się Ogromny Cień", L"Yaklaşan Dev Gölge");
			break;
		case 8441:
			a = L"E.O.V";
			break;
		case 8442:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8500:
			a = L"Strain";
			break;
		case 8501:
			a = LL14(L"夜のひととき", L"Nighttime", L"Un Moment Nocturne", L"Momento Notturno", L"Un Momento Nocturno", L"밤의 한때", L"夜晚的片刻", L"وقت الليل", L"Ночное Время", L"Nachtzeit", L"Um Momento Noturno", L"Nachtelijk Moment", L"Nocna Chwila", L"Gece Vakti");
			break;
		case 8502:
			a = LL14(L"トラブル発生", L"Trouble", L"Problème Survenu", L"Problema Sorto", L"Problema Surgido", L"트러블 발생", L"麻烦发生", L"حدثت مشكلة", L"Возникли Неприятности", L"Ärger", L"Problema Surgido", L"Probleem Opgetreden", L"Kłopoty", L"Sorun Çıktı");
			break;
		case 8503:
			a = LL14(L"鉄路遥々", L"Distant Iron Road", L"Longue Route de Fer", L"Lunga Strada di Ferro", L"Larga Ruta de Hierro", L"철로 아득히", L"遥远的铁路", L"طريق حديدي بعيد", L"Далёкий Железный Путь", L"Weite Eisenstraße", L"Longa Estrada de Ferro", L"Verre IJzeren Weg", L"Daleka Żelazna Droga", L"Uzak Demir Yolu");
			break;
		case 8504:
			a = LL14(L"旅愁", L"Travel Melancholy", L"Mélancolie du Voyage", L"Malinconia del Viaggio", L"Melancolía del Viaje", L"여수", L"旅途旅愁", L"حزن السفر", L"Дорожная Меланхолия", L"Reisemelancholie", L"Melancolia da Viagem", L"Reismelancholie", L"Melancholia Podróży", L"Yolculuk Hüznü");
			break;
		case 8505:
			a = LL14(L"皇城にて", L"At the Imperial Castle", L"Au Château Impérial", L"Al Castello Imperiale", L"En el Castillo Imperial", L"황성에서", L"在皇城", L"في القصر الإمبراطوري", L"В Императорском Замке", L"Im Kaiserlichen Schloss", L"No Castelo Imperial", L"In het Keizerlijke Kasteel", L"W Cesarskim Zamku", L"İmparatorluk Kalesinde");
			break;
		case 8506:
			a = L"Let's Study";
			break;
		case 8507:
			a = LL14(L"知恵を絞って", L"Rack Your Brains", L"Se Creuser la Tête", L"Sforzarsi di Pensare", L"Exprimirse el Cerebro", L"지혜를 짜내어", L"尽心竭力", L"عصر الأفكار", L"Напрячь Мозги", L"Den Kopf Zerbrechen", L"Quebrar a Cabeça", L"Hersens Pijnigen", L"Wytężając Umysł", L"Beyin Fırtınası");
			break;
		case 8508:
			a = LL14(L"実技教練", L"Combat Training", L"Entraînement au Combat", L"Addestramento al Combattimento", L"Entrenamiento de Combate", L"실기교련", L"实技教练", L"تدريب قتالي", L"Боевая Тренировка", L"Kampftraining", L"Treinamento de Combate", L"Gevechtstraining", L"Trening Bojowy", L"Muharebe Eğitimi");
			break;
		case 8509:
			a = LL14(L"寮に帰ろう", L"Back to the Dorm", L"Retour au Dortoir", L"Ritorno al Dormitorio", L"De Vuelta al Dormitorio", L"기숙사로 돌아가자", L"回宿舍去吧", L"العودة إلى السكن", L"Обратно в Общежитие", L"Zurück zum Wohnheim", L"De Volta ao Dormitório", L"Terug naar het Dorm", L"Z Powrotem do Akademika", L"Yurda Dönelim");
			break;
		case 8510:
			a = LL14(L"アーベントタイム", L"Evening Time", L"Soirée", L"Ora della Sera", L"Hora de la Tarde", L"아벤트 타임", L"黄昏时光", L"وقت المساء", L"Вечернее Время", L"Abendzeit", L"Hora da Tarde", L"Avondtijd", L"Czas Wieczoru", L"Akşam Vakti");
			break;
		case 8512:
			a = LL14(L"鉄の統率", L"Iron Command", L"Commandement de Fer", L"Comando di Ferro", L"Mando de Hierro", L"철의 통솔", L"铁的统率", L"القيادة الحديدية", L"Железное Командование", L"Eiserner Befehl", L"Comando de Ferro", L"IJzeren Bevel", L"Żelazne Dowodzenie", L"Demir Komuta");
			break;
		case 8513:
			a = LL14(L"暗躍", L"Moving in the Shadows", L"Agissant dans l'Ombre", L"Agendo nell'Ombra", L"Actuando en las Sombras", L"암약", L"暗中活跃", L"التحرك في الظلال", L"Действия в Тени", L"Im Verborgenen Agieren", L"Movendo-se nas Sombras", L"In het Donker Bewegen", L"Działanie w Cieniu", L"Gölgede Hareket");
			break;
		case 8514:
			a = LL14(L"想いの行き先", L"Where Feelings Lead", L"Là où Mènent les Sentiments", L"Dove Portano i Sentimenti", L"Adonde Llevan los Sentimientos", L"진심이 가는 곳", L"心意所去之处", L"حيث تقود المشاعر", L"Куда Ведут Чувства", L"Wohin Gefühle Führen", L"Para Onde os Sentimentos Levam", L"Waar Gevoelens Naartoe Leiden", L"Dokąd Prowadzą Uczucia", L"Duyguların Götürdüğü Yer");
			break;
		case 8515:
			a = LL14(L"傷心", L"Heartbreak", L"Cœur Brisé", L"Cuore Spezzato", L"Corazón Roto", L"상심", L"伤心", L"كسر القلب", L"Разбитое Сердце", L"Herzschmerz", L"Coração Partido", L"Hartenpijn", L"Złamane Serce", L"Kırık Kalp");
			break;
		case 8516:
			a = LL14(L"揺らめく炎を見つめて", L"Watching the Flickering Flames", L"Regarder les Flammes Vacillantes", L"Guardare le Fiamme Tremolanti", L"Mirando las Llamas Parpadeantes", L"어른거리는 불꽃을 바라보며", L"凝视着摇曳的火焰", L"مراقبة اللهب المتذبذب", L"Глядя на Мерцающее Пламя", L"Die Flackernden Flammen Beobachten", L"Observando as Chamas Oscilantes", L"De Flakkerende Vlammen Bekijken", L"Wpatrując się w Migoczące Płomienie", L"Titreyen Alevlere Bakarken");
			break;
		case 8517:
			a = LL14(L"一途な気持ち", L"Single-minded Feelings", L"Sentiments Sincères", L"Sentimenti Sinceri", L"Sentimientos Sinceros", L"한결같은 마음", L"一心一意的心情", L"مشاعر مخلصة", L"Искренние Чувства", L"Aufrichtige Gefühle", L"Sentimentos Sinceros", L"Oprechte Gevoelens", L"Szczere Uczucia", L"Tek Yönlü Duygular");
			break;
		case 8520:
			a = LL14(L"臨戦態勢", L"Combat Ready", L"Prêt au Combat", L"Pronto al Combattimento", L"Listo para el Combate", L"임전태세", L"战备状态", L"الاستعداد للقتال", L"Боевая Готовность", L"Kampfbereit", L"Pronto para o Combate", L"Gevechtsklaar", L"Gotowość Bojowa", L"Muharebe Hazırlığı");
			break;
		case 8521:
			a = L"Seriousness";
			break;
		case 8522:
			a = LL14(L"静かなる昂揚", L"Quiet Exhilaration", L"Exaltation Silencieuse", L"Esaltazione Silenziosa", L"Exaltación Silenciosa", L"조용한 고양", L"静静的昂扬", L"النشوة الهادئة", L"Тихое Воодушевление", L"Stille Begeisterung", L"Exaltação Silenciosa", L"Stille Opwinding", L"Cicha Ekscytacja", L"Sessiz Coşku");
			break;
		case 8523:
			a = LL14(L"暖かな夕餉", L"Warm Dinner", L"Dîner Chaleureux", L"Cena Calda", L"Cena Cálida", L"따뜻한 저녁 식사", L"温暖的晚餐", L"عشاء دافئ", L"Тёплый Ужин", L"Warmes Abendessen", L"Jantar Caloroso", L"Warm Avondeten", L"Ciepła Kolacja", L"Sıcak Akşam Yemeği");
			break;
		case 8524:
			a = L"Atrocious Raid";
			break;
		case 8525:
			a = LL14(L"全てを賭して今、ここに立つ", L"Standing Here, Betting Everything", L"Debout Ici, Tout Misant", L"In Piedi Qui, Scommettendo Tutto", L"De Pie Aquí, Apostándolo Todo", L"모든 것을 걸고 지금, 여기에 선다", L"押上一切，此刻站在这里", L"أقف هنا مراهناً على كل شيء", L"Стоя Здесь, Ставя Всё на Кон", L"Hier Stehend, Alles Einsetzend", L"Aqui de Pé, Apostando Tudo", L"Hier Staand, Alles Inzettend", L"Stojąc tu, Stawiając Wszystko na Szali", L"Burada Durarak Her Şeyi Bahse Girerek");
			break;
		case 8527:
			a = LL14(L"新しい仲間たち", L"New Comrades", L"Nouveaux Camarades", L"Nuovi Compagni", L"Nuevos Compañeros", L"새로운 동료들", L"新的伙伴们", L"رفاق جدد", L"Новые Товарищи", L"Neue Kameraden", L"Novos Camaradas", L"Nieuwe Kameraden", L"Nowi Towarzysze", L"Yeni Yoldaşlar");
			break;
		case 8528:
			a = LL14(L"不透明な事態", L"Opaque Situation", L"Situation Opaque", L"Situazione Opaca", L"Situación Opaca", L"불투명한 사태", L"不透明的局面", L"وضع غامض", L"Непрозрачная Ситуация", L"Undurchsichtige Lage", L"Situação Opaca", L"Ondoorzichtige Situatie", L"Niejasna Sytuacja", L"Belirsiz Durum");
			break;
		case 8529:
			a = LL14(L"鉄血へのレクイエム", L"Requiem for Iron and Blood", L"Requiem pour le Fer et le Sang", L"Requiem per il Ferro e il Sangue", L"Réquiem por el Hierro y la Sangre", L"철혈로의 레퀴엠", L"献给铁与血的安魂曲", L"قداس من أجل الحديد والدم", L"Реквием по Железу и Крови", L"Requiem für Eisen und Blut", L"Requiem pelo Ferro e pelo Sangue", L"Requiem voor IJzer en Bloed", L"Requiem dla Żelaza i Krwi", L"Demir ve Kan İçin Requiem");
			break;
		case 8530:
			a = LL14(L"幻想の唄 -PHANTASMAGORIA-", L"Phantom Song -PHANTASMAGORIA-", L"Chant Fantôme -PHANTASMAGORIA-", L"Canto Fantasma -PHANTASMAGORIA-", L"Canción Fantasma -PHANTASMAGORIA-", L"환상의 노래 -PHANTASMAGORIA-", L"幻想之歌 -PHANTASMAGORIA-", L"أغنية خيالية -PHANTASMAGORIA-", L"Призрачная Песня -PHANTASMAGORIA-", L"Phantomgesang -PHANTASMAGORIA-", L"Canção Fantasma -PHANTASMAGORIA-", L"Spooklied -PHANTASMAGORIA-", L"Pieśń Widmo -PHANTASMAGORIA-", L"Hayalet Şarkı -PHANTASMAGORIA-");
			break;
		case 8531:
			a = LL14(L"刻ハ至レリ", L"The Hour Has Come", L"L'Heure est Venue", L"L'Ora è Giunta", L"La Hora Ha Llegado", L"때는 이르렀다", L"时刻已至", L"لقد حانت الساعة", L"Час Настал", L"Die Stunde ist Gekommen", L"A Hora Chegou", L"Het Uur is Gekomen", L"Godzina Nadeszła", L"Saat Geldi");
			break;
		case 8532:
			a = LL14(L"目覚めし伝承", L"Awakening Legend", L"Légende Éveillée", L"Leggenda Risvegliata", L"Leyenda Despertada", L"깨어난 전승", L"觉醒的传承", L"الأسطورة المستيقظة", L"Пробуждённая Легенда", L"Erwachende Legende", L"Lenda Despertada", L"Ontwakende Legende", L"Przebudzona Legenda", L"Uyanan Efsane");
			break;
		case 8533:
			a = LL14(L"唯一の希望", L"Only Hope", L"Seul Espoir", L"Unica Speranza", L"Única Esperanza", L"유일한 희망", L"唯一的希望", L"الأمل الوحيد", L"Единственная Надежда", L"Einzige Hoffnung", L"Única Esperança", L"Enige Hoop", L"Jedyna Nadzieja", L"Tek Umut");
			break;
		case 8535:
		case 8537:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8538:
			a = LL14(L"今はまだ...", L"Not Yet...", L"Pas Encore...", L"Non Ancora...", L"Todavía No...", L"지금은 아직...", L"现在还不行...", L"ليس بعد...", L"Ещё Нет...", L"Noch Nicht...", L"Ainda Não...", L"Nog Niet...", L"Jeszcze Nie...", L"Henüz Değil...");
			break;
		case 8539:
			a = LL14(L"あの日に見た夜空", L"The Night Sky I Saw That Day", L"Le Ciel Nocturne que j'ai Vu Ce Jour-là", L"Il Cielo Notturno che Vidi Quel Giorno", L"El Cielo Nocturno que Vi Ese Día", L"그날에 본 밤하늘", L"那天看到的夜空", L"سماء الليل التي رأيتها ذلك اليوم", L"Ночное Небо, Которое Я Видел В Тот День", L"Der Nachthimmel, den Ich Damals Sah", L"O Céu Noturno que Vi Naquele Dia", L"De Nachtelijke Hemel die ik Die Dag Zag", L"Nocne Niebo, które Widziałem Tamtego Dnia", L"O Gün Gördüğüm Gece Gökyüzü");
			break;
		case 8540:
			a = LL14(L"偽りの時間", L"False Time", L"Temps Fictif", L"Tempo Falso", L"Tiempo Falso", L"거짓된 시간", L"虚假的时间", L"الوقت المزيف", L"Ложное Время", L"Falsche Zeit", L"Tempo Falso", L"Valse Tijd", L"Fałszywy Czas", L"Sahte Zaman");
			break;
		case 8541:
			a = LL14(L"紅き翼 -新たなる風-", L"Crimson Wings -New Wind-", L"Ailes Cramoisies -Nouveau Vent-", L"Ali Cremisi -Nuovo Vento-", L"Alas Carmesí -Nuevo Viento-", L"붉은 날개 -새로운 바람-", L"红之翼 -新风-", L"الأجنحة القرمزية -رياح جديدة-", L"Багровые Крылья -Новый Ветер-", L"Karmesinrote Flügel -Neuer Wind-", L"Asas Carmesim -Novo Vento-", L"Karmozijnrode Vleugels -Nieuwe Wind-", L"Karmazynowe Skrzydła -Nowy Wiatr-", L"Kırmızı Kanatlar -Yeni Rüzgar-");
			break;
		case 8550:
			a = LL14(L"再会", L"Reunion", L"Retrouvailles", L"Riunione", L"Reencuentro", L"재회", L"重逢", L"لقاء ثان", L"Воссоединение", L"Wiedersehen", L"Reencontro", L"Hereniging", L"Ponowne Spotkanie", L"Yeniden Buluşma");
			break;
		case 8551:
			a = LL14(L"かけがえのない人へ", L"To Someone Irreplaceable", L"À Quelqu'un d'Irremplaçable", L"A Qualcuno di Insostituibile", L"A Alguien Insustituible", L"둘도 없는 소중한 사람에게", L"致无可替代之人", L"إلى شخص لا يعوض", L"Незаменимому Человеку", L"An Jemanden Unersetzlichen", L"A Alguém Insubstituível", L"Aan Iemand Onvervangbaar", L"Do Kogoś Niezastąpionego", L"Vazgeçilmez Birine");
			break;
		case 8552:
			a = LL14(L"惜しむように、愛おしむように", L"Cherishing, Treasuring", L"Chérissant, Précieux", L"Custodendo, Tesaurizzando", L"Atesorando, Valorando", L"아쉬워하듯, 아끼고 사랑하듯", L"如同珍惜，如同爱怜", L"بالاعتزاز والمحبة", L"Дорожа, Храня", L"Kosten, Schätzen", L"Prezando, Valorizando", L"Koesterend, Waarderend", L"Ceniąc, Pielęgnując", L"Değer Vererek, Sevgiyle");
			break;
		case 8553:
			a = LL14(L"ライノの花が咲く頃", L"When the Rhino Flower Blooms", L"Quand la Fleur de Rhino s'Épanouit", L"Quando il Fiore di Rhino Sboccia", L"Cuando Florece la Flor de Rhino", L"라이노 꽃이 필 무렵", L"莱诺花盛开之时", L"عندما تفتح زهرة راينو", L"Когда Цветёт Цветок Райно", L"Wenn die Rhino-Blume Blüht", L"Quando a Flor de Rhino Desabrocha", L"Als de Rhino Bloem Bloeit", L"Gdy Kwitnie Kwiat Rhino", L"Rhino Çiçeği Açtığında");
			break;
		case 8555:
			a = LL14(L"戦場の掟", L"Rules of Battlefield", L"Règles du Champ de Bataille", L"Regole del Campo di Battaglia", L"Reglas del Campo de Batalla", L"전장의 규칙", L"战场的规则", L"قوانين ساحة المعركة", L"Правила Поля Боя", L"Regeln des Schlachtfeldes", L"Regras do Campo de Batalha", L"Regels van het Slagveld", L"Zasady Pola Bitwy", L"Savaş Alanının Kuralları");
			break;
		case 8556:
			a = L"Remaining Glow";
			break;
		case 8557:
			a = LL14(L"深淵の魔女", L"Witch of the Abyss", L"Sorcière de l'Abîme", L"Strega dell'Abisso", L"Bruja del Abismo", L"심연의 마녀", L"深渊的魔女", L"ساحرة الهاوية", L"Ведьма Бездны", L"Hexe des Abgrunds", L"Bruxa do Abismo", L"Heks van de Afgrond", L"Wiedźma Otchłani", L"Uçurumun Cadısı");
			break;
		case 8558:
			a = L"ALTINA";
			break;
		case 8559:
			a = LL14(L"威風", L"Dignity", L"Dignité", L"Dignità", L"Dignidad", L"위풍", L"威风", L"المهابة", L"Достоинство", L"Würde", L"Dignidade", L"Waardigheid", L"Godność", L"Haysiyet");
			break;
		case 8560:
			a = LL14(L"一撃に賭ける", L"Bet on One Strike", L"Miser sur un Seul Coup", L"Scommettere su un Solo Colpo", L"Apostar por un Solo Golpe", L"일격에 건다", L"赌在一击", L"الرهان على ضربة واحدة", L"Ставить на Один Удар", L"Auf einen Schlag Setzen", L"Apostar em um Único Golpe", L"Alles op Een Slag Zetten", L"Postawić na Jeden Cios", L"Tek Darbeye Bahse Girmek");
			break;
		case 8561:
			a = LL14(L"ユミル渓谷道", L"Ymir Valley Road", L"Route de la Vallée de Ymir", L"Strada della Valle di Ymir", L"Camino del Valle de Ymir", L"유미르 계곡길", L"尤弥尔谷道", L"طريق وادي إيمير", L"Дорога Долины Имир", L"Ymir-Talstraße", L"Estrada do Vale de Ymir", L"Ymir Valleiroute", L"Droga Doliny Ymir", L"Ymir Vadi Yolu");
			break;
		case 8562:
			a = L"Awakening";
			break;
		case 8563:
			a = L"Blitzkrieg";
			break;
		case 8564:
			a = LL14(L"魔王の凱歌", L"Demon Lord's Triumph", L"Triomphe du Seigneur Démon", L"Trionfo del Signore dei Demoni", L"Triunfo del Señor Demonio", L"마왕의 개가", L"魔王的凯歌", L"نشيد نصر سيد الشياطين", L"Триумф Повелителя Демонов", L"Triumph des Dämonenkönigs", L"Triunfo do Senhor dos Demônios", L"Triomf van de Demonenkoning", L"Triumf Władcy Demonów", L"Şeytan Lordu'nun Zaferi");
			break;
		case 8566:
			a = LL14(L"内なる黄昏", L"Inner Twilight", L"Crépuscule Intérieur", L"Crepuscolo Interiore", L"Crepúsculo Interior", L"내면의 황혼", L"内心的黄昏", L"الغسق الداخلي", L"Внутренние Сумерки", L"Innere Dämmerung", L"Crepúsculo Interior", L"Innerlijke Schemering", L"Wewnętrzny Zmierzch", L"İç Alacakaranlık");
			break;
		case 8567:
			a = LL14(L"蘇る記憶", L"Awakened Memories", L"Souvenirs Ressuscités", L"Ricordi Risvegliati", L"Recuerdos Resucitados", L"살아나는 기억", L"觉醒的记忆", L"الذكريات المستعادة", L"Пробуждённые Воспоминания", L"Erwachte Erinnerungen", L"Memórias Despertadas", L"Ontwakende Herinneringen", L"Przebudzone Wspomnienia", L"Uyanan Anılar");
			break;
		case 8570:
			a = LL14(L"静かな決意", L"Quiet Resolution", L"Résolution Silencieuse", L"Risoluzione Silenziosa", L"Resolución Silenciosa", L"조용한 결의", L"静静的决意", L"عزيمة هادئة", L"Тихая Решимость", L"Stille Entschlossenheit", L"Resolução Silenciosa", L"Stille Vastberadenheid", L"Cicha Determinacja", L"Sessiz Kararlılık");
			break;
		case 8571:
			a = LL14(L"乾坤一擲", L"All or Nothing", L"Tout ou Rien", L"Tutto o Niente", L"Todo o Nada", L"건곤일척", L"孤注一掷", L"الكل أو لا شيء", L"Всё или Ничего", L"Alles oder Nichts", L"Tudo ou Nada", L"Alles of Niets", L"Wszystko albo Nic", L"Ya Hep Ya Hiç");
			break;
		case 8572:
			a = LL14(L"交戦", L"Combat", L"Combat", L"Combattimento", L"Combate", L"교전", L"交战", L"اشتباك", L"Бой", L"Kampf", L"Combate", L"Gevecht", L"Walka", L"Muharebe");
			break;
		case 8573:
		case 8584:
		case 8605:
		case 8606:
		case 8608:
		case 8610:
		case 8622:
		case 8623:
		case 8624:
		case 8625:
		case 8627:
		case 8629:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"مؤثر صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses efekti");
			break;
		case 8600:
			a = LL14(L"大市の賑わい", L"Bustling Market", L"Marché Animé", L"Mercato Vivace", L"Mercado Animado", L"장터의 북적임", L"大市场的热闹", L"سوق مزدحم", L"Оживлённый Рынок", L"Belebter Markt", L"Mercado Movimentado", L"Drukke Markt", L"Tętniący życiem Rynek", L"Kalabalık Pazar");
			break;
		case 8601:
			a = LL14(L"剣の遊戯", L"Sword Play", L"Jeu d'Épée", L"Gioco di Spada", L"Juego de Espada", L"검의 유희", L"剑的游玩", L"لعب بالسيف", L"Игра на Мечах", L"Schwertkampfspiel", L"Jogo de Espada", L"Zwaardspel", L"Gra na Miecze", L"Kılıç Oyunu");
			break;
		case 8602:
			a = LL14(L"紙一重の攻防", L"Close Fight", L"Combat Serré", L"Combattimento Serrato", L"Combate Reñido", L"종이 한 장 차이의 공방", L"纸之一线的攻防", L"دفاع وهجوم متقارب", L"Напряжённый Бой", L"Knappes Gefecht", L"Luta Apertada", L"Nipt Gevecht", L"Zacięta Walka", L"Çekişmeli Dövüş");
			break;
		case 8603:
			a = LL14(L"走れマッハ号!", L"Run Mach Train!", L"En Avant Mach Train !", L"Corri Treno Mach!", L"¡Corre Tren Mach!", L"달려라 마하 호!", L"快跑马赫号！", L"اركض يا قطار ماخ!", L"Беги, Поезд Мах!", L"Lauf, Mach-Zug!", L"Corra Trem Mach!", L"Ren Mach Trein!", L"Biegnij Pociągu Mach!", L"Koş Mach Treni!");
			break;
		case 8607:
			a = LL14(L"星屑のカンタータ", L"Cantata of Stardust", L"Cantate de Poussière d'Étoiles", L"Cantata di Polvere di Stelle", L"Cantata de Polvo de Estrellas", L"별가루의 칸타타", L"星屑康塔塔", L"كنتاتا غبار النجوم", L"Кантата Звёздной Пыли", L"Kantate des Sternenstaubs", L"Cantata de Poeira Estelar", L"Cantate van Sterrenstof", L"Kantata Gwiazdowego Pyłu", L"Yıldız Tozu Kantası");
			break;
		case 8609:
			a = L"Sonata No.45";
			break;
		case 8620:
			a = LL14(L"雪ウサギを追いかけて", L"Chasing the Snow Rabbit", L"Chasser le Lapin des Neiges", L"Inseguire il Coniglio della Neve", L"Persiguiendo al Conejo de Nieve", L"눈토끼를 쫓아서", L"追逐雪兔", L"ملاحقة أرنب الثلج", L"Погоня за Снежным Кроликом", L"Das Schneekaninchen Jagen", L"Perseguindo o Coelho da Neve", L"Het Sneeuwkonijn Najagen", L"Goniąc śnieżnego Królika", L"Kar Tavşanını Kovalayarak");
			break;
		case 8621:
			a = L"Take The Windward!";
			break;
		case 8628:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8700:
		case 8703:
		case 8704:
		case 8710:
		case 8711:
			a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Música", L"음악", L"音乐", L"موسيقى", L"Музыка", L"Musik", L"Música", L"Muziek", L"Muzyka", L"Müzik");
			break;
		}
		stitle = a;
		lenl = 0;
		FILE* fp = _wfsopen(filen, L"rb", 0x40);
		fseek(fp, 0, SEEK_END);
		long length = ftell(fp);
		fseek(fp, length - 10000, SEEK_SET);
		char* buffer = (char*)malloc(10000);
		fread(buffer, 1, 10000, fp);
		int jj;
		int flg = 0;
		for (jj = 0; jj < 9996; jj++) {
			if (*(buffer + jj) == 's' && *(buffer + jj + 1) == 'm' && *(buffer + jj + 2) == 'p' && *(buffer + jj + 3) == 'l') {
				flg = 1;
				break;
			}
		}
		char* p = buffer + jj;
		if (flg == 1) {
			p = p + 4;//smplをスキップ
			p = p + 0x30;
			loop1 = *(int*)p;
			loop2 = *(int*)(p + 4) - loop1;
		}
		else {
			loop1 = loop2 = 0;
		}
		fclose(fp);
		free(buffer);
		//----------
		CFile adpcmf;
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		long wavdata_length;
		adpcmf.Seek(0x28, CFile::begin);
		adpcmf.Read(&wavdata_length, 4);
		adbuf2 = (char*)calloc(wavdata_length * 2 + dl * 2, 1);
		adpcmf.Read(adbuf2, dwDataLen * 2);
		data_size = oggsize = wavdata_length;
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		wavwait = 1;
		int si = wavdata_length + dl * 2;
		dwDataLen += dl;
		si -= dwDataLen * 2;
		for (; si > 0;) {
			adpcmf.Read(adbuf2 + (int)dwDataLen, dl);
			dwDataLen += dl; si -= dl;
			if (thend1 == TRUE) { thend = 1; adpcmf.Close();  return; }
		}
		adpcmf.Close();
	}
	else if (mode == -6) {
		CString a = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		CString b = a.Mid(6, 1);
		int err;
		int fff = 0;
		// Ys X - 楽曲情報の一括修正
// 前半：ファイル名判定による「音楽」カテゴリ設定
		if (a.Left(2) == L"y_" && a.Right(5) == L".opus") {
			a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Música", L"음악", L"音乐", L"موسيقى", L"Музыка", L"Musik", L"Música", L"Muziek", L"Muzyka", L"Müzik");
			fff = 1;
		}

		// 後半：詳細タイトル設定
		CString ft = filen.Right(filen.GetLength() - filen.ReverseFind(L'\\') - 1);

		if (ft == L"y_act_e002.opus") {
			a = L"Operation SANDRAS"; fff = 1;
		}
		else if (ft == L"y_act_e002_s1.opus") {
			a = LL14(L"Operation SANDRAS(重低音)", L"Operation SANDRAS (Bass Boost)", L"Operation SANDRAS (Renfort graves)", L"Operation SANDRAS (Rinforzo bassi)", L"Operation SANDRAS (Refuerzo graves)", L"Operation SANDRAS (저음 강조)", L"Operation SANDRAS (重低音)", L"Operation SANDRAS (تعزيز الجهير)", L"Operation SANDRAS (Усиление низких)", L"Operation SANDRAS (Bassverstärkung)", L"Operation SANDRAS (Reforço graves)", L"Operation SANDRAS (Basversterking)", L"Operation SANDRAS (Wzmocnienie basów)", L"Operation SANDRAS (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b100.opus") {
			a = L"Overblaze"; fff = 1;
		}
		else if (ft == L"y_b100_s1.opus") {
			a = LL14(L"Overblaze(重低音)", L"Overblaze (Bass Boost)", L"Overblaze (Renfort graves)", L"Overblaze (Rinforzo bassi)", L"Overblaze (Refuerzo graves)", L"Overblaze (저음 강조)", L"Overblaze (重低音)", L"Overblaze (تعزيز الجهير)", L"Overblaze (Усиление низких)", L"Overblaze (Bassverstärkung)", L"Overblaze (Reforço graves)", L"Overblaze (Basversterking)", L"Overblaze (Wzmocnienie basów)", L"Overblaze (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b200.opus") {
			a = L"Through the North Wind"; fff = 1;
		}
		else if (ft == L"y_b200_s1.opus") {
			a = LL14(L"Through the North Wind(重低音)", L"Through the North Wind (Bass Boost)", L"Through the North Wind (Renfort graves)", L"Through the North Wind (Rinforzo bassi)", L"Through the North Wind (Refuerzo graves)", L"Through the North Wind (저음 강조)", L"Through the North Wind (重低音)", L"Through the North Wind (تعزيز الجهير)", L"Through the North Wind (Усиление низких)", L"Through the North Wind (Bassverstärkung)", L"Through the North Wind (Reforço graves)", L"Through the North Wind (Basversterking)", L"Through the North Wind (Wzmocnienie basów)", L"Through the North Wind (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b210.opus") {
			a = LL14(L"高鳴る鼓動", L"Pounding Heartbeat", L"Battement de cœur saccadé", L"Battito accelerato", L"Latido palpitante", L"두근거리는 고동", L"剧烈的心跳", L"نبضات القلب المتسارعة", L"Учащенное сердцебиение", L"Pochendes Herzklopfen", L"Batida forte do coração", L"Bonzend hart", L"Łomoczące serce", L"Küt Küt Atan Kalp");
			fff = 1;
		}
		else if (ft == L"y_b210_s1.opus") {
			a = LL14(L"高鳴る鼓動(重低音)", L"Pounding Heartbeat (Bass Boost)", L"Pounding Heartbeat (Renfort graves)", L"Pounding Heartbeat (Rinforzo bassi)", L"Pounding Heartbeat (Refuerzo graves)", L"Pounding Heartbeat (저음 강조)", L"高鳴る鼓動 (重低音)", L"Pounding Heartbeat (تعزيز الجهير)", L"Pounding Heartbeat (Усиление низких)", L"Pounding Heartbeat (Bassverstärkung)", L"Pounding Heartbeat (Reforço graves)", L"Pounding Heartbeat (Basversterking)", L"Pounding Heartbeat (Wzmocnienie basów)", L"Pounding Heartbeat (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b300.opus") {
			a = LL14(L"石火の如く", L"Like Flint", L"Comme le silex", L"Come la selce", L"Como el sílex", L"전광석화처럼", L"如同火石", L"مثل الصوان", L"Словно кремень", L"Wie Feuerstein", L"Como pederneira", L"Als vuursteen", L"Jak krzemień", L"Çakmak Taşı Gibi");
			fff = 1;
		}
		else if (ft == L"y_b300_s1.opus") {
			a = LL14(L"石火の如く(重低音)", L"Like Flint (Bass Boost)", L"Like Flint (Renfort graves)", L"Like Flint (Rinforzo bassi)", L"Like Flint (Refuerzo graves)", L"Like Flint (저음 강조)", L"石火の如く (重低音)", L"Like Flint (تعزيز الجهير)", L"Like Flint (Усиление низких)", L"Like Flint (Bassverstärkung)", L"Like Flint (Reforço graves)", L"Like Flint (Basversterking)", L"Like Flint (Wzmocnienie basów)", L"Like Flint (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b400.opus") {
			a = L"Can You Do It"; fff = 1;
		}
		else if (ft == L"y_b400_s1.opus") {
			a = LL14(L"Can You Do It(重低音)", L"Can You Do It (Bass Boost)", L"Can You Do It (Renfort graves)", L"Can You Do It (Rinforzo bassi)", L"Can You Do It (Refuerzo graves)", L"Can You Do It (저음 강조)", L"Can You Do It (重低音)", L"Can You Do It (تعزيز الجهير)", L"Can You Do It (Усиление низких)", L"Can You Do It (Bassverstärkung)", L"Can You Do It (Reforço graves)", L"Can You Do It (Basversterking)", L"Can You Do It (Wzmocnienie basów)", L"Can You Do It (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b500.opus") {
			a = LL14(L"BERSERK -戦斧の咆哮-", L"BERSERK -Roar of the Battle Axe-", L"BERSERK -Rugissement de la hache de guerre-", L"BERSERK -Ruggito dell'ascia da battaglia-", L"BERSERK -Rugido del hacha de batalla-", L"BERSERK -전부의 포효-", L"BERSERK -战斧的咆哮-", L"BERSERK - زئير فأس الحرب", L"BERSERK -Рев боевого топора-", L"BERSERK -Brüllen der Streitaxt-", L"BERSERK -Rugido do machado de batalha-", L"BERSERK -Geknal van de strijdbijl-", L"BERSERK -Ryk topora wojennego-", L"BERSERK -Savaş Baltasının Kükreyişi-");
			fff = 1;
		}
		else if (ft == L"y_b500_s1.opus") {
			a = LL14(L"BERSERK -戦斧の咆哮-(重低音)", L"BERSERK -Roar of the Battle Axe- (Bass Boost)", L"BERSERK -Roar of the Battle Axe- (Renfort graves)", L"BERSERK -Roar of the Battle Axe- (Rinforzo bassi)", L"BERSERK -Roar of the Battle Axe- (Refuerzo graves)", L"BERSERK -Roar of the Battle Axe- (저음 강조)", L"BERSERK -戦斧の咆哮- (重低音)", L"BERSERK -Roar of the Battle Axe- (تعزيز الجهير)", L"BERSERK -Roar of the Battle Axe- (Усиление низких)", L"BERSERK -Roar of the Battle Axe- (Bassverstärkung)", L"BERSERK -Roar of the Battle Axe- (Reforço graves)", L"BERSERK -Roar of the Battle Axe- (Basversterking)", L"BERSERK -Roar of the Battle Axe- (Wzmocnienie basów)", L"BERSERK -Roar of the Battle Axe- (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b510.opus") {
			a = LL14(L"悪意の洗礼", L"Baptism of Malice", L"Baptême de malice", L"Battesimo di malizia", L"Bautismo de malicia", L"악의의 세례", L"恶意的洗礼", L"معمودية الخبث", L"Крещение злобой", L"Taufe der Bosheit", L"Batismo de malícia", L"Doop van kwaadaardigheid", L"Chrzest złośliwości", L"Garez Vaftizi");
			fff = 1;
		}
		else if (ft == L"y_b510_s1.opus") {
			a = LL14(L"悪意の洗礼(重低音)", L"Baptism of Malice (Bass Boost)", L"Baptism of Malice (Renfort graves)", L"Baptism of Malice (Rinforzo bassi)", L"Baptism of Malice (Refuerzo graves)", L"Baptism of Malice (저음 강조)", L"悪意の洗礼 (重低音)", L"Baptism of Malice (تعزيز الجهير)", L"Baptism of Malice (Усиление низких)", L"Baptism of Malice (Bassverstärkung)", L"Baptism of Malice (Reforço graves)", L"Baptism of Malice (Basversterking)", L"Baptism of Malice (Wzmocnienie basów)", L"Baptism of Malice (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b520.opus") {
			a = L"The Ultimate Pleasure in My Hands"; fff = 1;
		}
		else if (ft == L"y_b520_s1.opus") {
			a = LL14(L"The Ultimate Pleasure in My Hands(重低音)", L"The Ultimate Pleasure in My Hands (Bass Boost)", L"The Ultimate Pleasure in My Hands (Renfort graves)", L"The Ultimate Pleasure in My Hands (Rinforzo bassi)", L"The Ultimate Pleasure in My Hands (Refuerzo graves)", L"The Ultimate Pleasure in My Hands (저음 강조)", L"The Ultimate Pleasure in My Hands (重低音)", L"The Ultimate Pleasure in My Hands (تعزيز الجهير)", L"The Ultimate Pleasure in My Hands (Усиление низких)", L"The Ultimate Pleasure in My Hands (Bassverstärkung)", L"The Ultimate Pleasure in My Hands (Reforço graves)", L"The Ultimate Pleasure in My Hands (Basversterking)", L"The Ultimate Pleasure in My Hands (Wzmocnienie basów)", L"The Ultimate Pleasure in My Hands (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b610.opus") {
			a = LL14(L"辿り着いた極光の下で", L"Under the Northern Lights", L"Sous les aurores boréales", L"Sotto l'aurora boreale", L"Bajo la aurora boreal", L"다다른 극광 아래에서", L"抵达极光之下", L"تحت أضواء الشفق القطبي", L"Под северным сиянием", L"Unter dem Nordlicht", L"Sob a aurora boreal", L"Onder het noorderlicht", L"Pod zorzą polarną", L"Kuzey Işıkları Altında");
			fff = 1;
		}
		else if (ft == L"y_b610_s1.opus") {
			a = LL14(L"辿り着いた極光の下で(重低音)", L"Under the Northern Lights (Bass Boost)", L"Under the Northern Lights (Renfort graves)", L"Under the Northern Lights (Rinforzo bassi)", L"Under the Northern Lights (Refuerzo graves)", L"Under the Northern Lights (저음 강조)", L"辿り着いた極光の下で (重低音)", L"Under the Northern Lights (تعزيز الجهير)", L"Under the Northern Lights (Усиление низких)", L"Under the Northern Lights (Bassverstärkung)", L"Under the Northern Lights (Reforço graves)", L"Under the Northern Lights (Basversterking)", L"Under the Northern Lights (Wzmocnienie basów)", L"Under the Northern Lights (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b620.opus") {
			a = L"Nordics Saga -The Endless Bloody Sea-"; fff = 1;
		}
		else if (ft == L"y_b620_s1.opus") {
			a = LL14(L"Nordics Saga -The Endless Bloody Sea-(重低音)", L"Nordics Saga (Bass Boost)", L"Nordics Saga (Renfort graves)", L"Nordics Saga (Rinforzo bassi)", L"Nordics Saga (Refuerzo graves)", L"Nordics Saga (저음 강조)", L"Nordics Saga (重低音)", L"Nordics Saga (تعزيز الجهير)", L"Nordics Saga (Усиление низких)", L"Nordics Saga (Bassverstärkung)", L"Nordics Saga (Reforço graves)", L"Nordics Saga (Basversterking)", L"Nordics Saga (Wzmocnienie basów)", L"Nordics Saga (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b700.opus") {
			a = L"Ready to Fire!"; fff = 1;
		}
		else if (ft == L"y_b700_s1.opus") {
			a = LL14(L"Ready to Fire!(重低音)", L"Ready to Fire! (Bass Boost)", L"Ready to Fire! (Renfort graves)", L"Ready to Fire! (Rinforzo bassi)", L"Ready to Fire! (Refuerzo graves)", L"Ready to Fire! (저음 강조)", L"Ready to Fire! (重低音)", L"Ready to Fire! (تعزيز الجهير)", L"Ready to Fire! (Усиление низких)", L"Ready to Fire! (Bassverstärkung)", L"Ready to Fire! (Reforço graves)", L"Ready to Fire! (Basversterking)", L"Ready to Fire! (Wzmocnienie basów)", L"Ready to Fire! (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b710.opus") {
			a = L"Hello, Those Who Can't Die"; fff = 1;
		}
		else if (ft == L"y_b710_s1.opus") {
			a = LL14(L"Hello, Those Who Can't Die(重低音)", L"Hello (Bass Boost)", L"Hello (Renfort graves)", L"Hello (Rinforzo bassi)", L"Hello (Refuerzo graves)", L"Hello (저음 강조)", L"Hello (重低音)", L"Hello (تعزيز الجهير)", L"Hello (Усиление низких)", L"Hello (Bassverstärkung)", L"Hello (Reforço graves)", L"Hello (Basversterking)", L"Hello (Wzmocnienie basów)", L"Hello (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_b720.opus") {
			a = L"Landing Warfare"; fff = 1;
		}
		else if (ft == L"y_b720_s1.opus") {
			a = LL14(L"Landing Warfare(重低音)", L"Landing Warfare (Bass Boost)", L"Landing Warfare (Renfort graves)", L"Landing Warfare (Rinforzo bassi)", L"Landing Warfare (Refuerzo graves)", L"Landing Warfare (저음 강조)", L"Landing Warfare (重低音)", L"Landing Warfare (تعزيز الجهير)", L"Landing Warfare (Усиление низких)", L"Landing Warfare (Bassverstärkung)", L"Landing Warfare (Reforço graves)", L"Landing Warfare (Basversterking)", L"Landing Warfare (Wzmocnienie basów)", L"Landing Warfare (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_bgm_none.opus") {
			a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"무음", L"无音", L"صمت", L"Тишина", L"Stille", L"Silencio", L"Stilte", L"Cisza", L"Sessizlik");
			fff = 1;
		}
		else if (ft == L"y_d100.opus") {
			a = LL14(L"光届かぬその奥に", L"In the Depths Where Light Doesn't Reach", L"Dans les profondeurs hors de portée de la lumière", L"Nelle profondità dove non arriva la luce", L"En las profundidades donde no llega la luz", L"빛이 닿지 않는 그 깊은 곳에", L"光线无法到达的深处", L"في الأعماق حيث لا يصل الضوء", L"В глубинах, куда не доходит свет", L"In den Tiefen, die kein Licht erreicht", L"Nas profundezas onde a luz não chega", L"In de diepten waar geen licht komt", L"W głębinach, gdzie nie sięga światło", L"Işığın Ulaşamadığı Derinliklerde");
			fff = 1;
		}
		else if (ft == L"y_d100_s1.opus") {
			a = LL14(L"光届かぬその奥に(重低音)", L"In the Depths (Bass Boost)", L"In the Depths (Renfort graves)", L"In the Depths (Rinforzo bassi)", L"In the Depths (Refuerzo graves)", L"In the Depths (저음 강조)", L"光届かぬその奥に (重低音)", L"In the Depths (تعزيز الجهير)", L"In the Depths (Усиление низких)", L"In the Depths (Bassverstärkung)", L"In the Depths (Reforço graves)", L"In the Depths (Basversterking)", L"In the Depths (Wzmocnienie basów)", L"In the Depths (Bas güçlendirme)");
			fff = 1;
		}
		else if (ft == L"y_d200.opus") {
			a = L"Eerie Stillness"; fff = 1;
		}
		else if (ft == L"y_d400.opus") {
			a = LL14(L"飽くなき渇望", L"Insatiable Thirst", L"Soif insatiable", L"Sete insaziabile", L"Sed insaciable", L"끝없는 갈망", L"永无止境的渴望", L"عطش لا ينتهي", L"Ненасытная жажда", L"Unstillbares Verlangen", L"Sede insaciável", L"Onverzadigbare dorst", L"Nienasycone pragnienie", L"Doymak Bilmez Susuzluk");
			fff = 1;
		}
		else if (ft == L"y_d410.opus") {
			a = L"The Inner Darkness"; fff = 1;
		}
		else if (ft == L"y_d500.opus") {
			a = L"Hardhearted Rock Line"; fff = 1;
		}
		else if (ft == L"y_d600.opus") {
			a = LL14(L"夢の痕跡", L"Dream Traces", L"Traces de rêves", L"Tracce di sogni", L"Rastros de sueños", L"꿈의 흔적", L"梦的痕迹", L"آثار الأحلام", L"Следы снов", L"Traumspuren", L"Rastros de sonhos", L"Droomsporen", L"Ślady snów", L"Rüya İzleri");
			fff = 1;
		}
		else if (ft == L"y_d710.opus") {
			a = LL14(L"甲鉄戦艦ナグルファ", L"Ironclad Battleship Naglfar", L"Cuirassé Naglfar", L"Corazzata Naglfar", L"Acorazado Naglfar", L"갑철전함 나글파ル", L"甲铁战舰 Naglfar", L"البارجة الحديدية ناجلفار", L"Броненосец Нагльфар", L"Panzerschiff Naglfar", L"Encouraçado Naglfar", L"Slagschip Naglfar", L"Pancernik Naglfar", L"Zırhlı Savaş Gemisi Naglfar");
			fff = 1;
		}
		else if (ft == L"y_d800.opus") {
			a = L"LILA -Innocent Wish-"; fff = 1;
		}
		else if (ft == L"y_d900.opus") {
			a = LL14(L"エギル海底神殿", L"Egil Undersea Temple", L"Temple sous-marin d'Egil", L"Tempio sottomarino di Egil", L"Templo submarino de Egil", L"에길 해저신전", L"Egil 海底神殿", L"معبد إيغيل تحت البحر", L"Подводный храм Эгиля", L"Egil-Unterseetempel", L"Templo submarino de Egil", L"Egil onderzeese tempel", L"Podmorska świątynia Egila", L"Egil Denizaltı Tapınağı");
			fff = 1;
		}
		else if (ft == L"y_d1010.opus") {
			a = L"The Paradise Lost of Norman"; fff = 1;
		}
		else if (ft == L"y_e004.opus") {
			a = LL14(L"あの時からずっと…", L"Ever Since That Day...", L"Depuis ce jour-là...", L"Da quel giorno...", L"Desde aquel día...", L"그때부터 줄곧...", L"从那时起一直...", L"منذ ذلك اليوم...", L"С того самого дня...", L"Seit jenem Tag...", L"Desde aquele dia...", L"Sinds die dag...", L"Od tamtego dnia...", L"O Günden Beri...");
			fff = 1;
		}
		else if (ft == L"y_e006.opus") {
			a = LL14(L"切っても切れない絆", L"Unbreakable Bonds", L"Liens indéfectibles", L"Legami indissolubili", L"Vínculos inquebrantables", L"뗄래야 뗄 수 없는 인연", L"无法割舍的羁绊", L"روابط لا تنفصم", L"Неразрывные узы", L"Unzerbrechliche Bande", L"Laços inquebráveis", L"Onbreekbare banden", L"Nierozerwalne więzi", L"Yıkılmaz Bağlar");
			fff = 1;
		}
		else if (ft == L"y_e007.opus") {
			a = LL14(L"灰色の深層", L"Gray Depths", L"Profondeurs grises", L"Profondità grigie", L"Profundidades grises", L"회색의 심층", L"灰色的深层", L"الأعماق الرمادية", L"Серые глубины", L"Graue Tiefen", L"Profundezas cinzentas", L"Grijze diepten", L"Szare głębiny", L"Gri Derinlikler");
			fff = 1;
		}
		else if (ft == L"y_e009.opus") {
			a = LL14(L"歪な願望", L"Twisted Desire", L"Désir tordu", L"Desiderio distorto", L"Deseo retorcido", L"일그러진 염원", L"歪曲的愿望", L"رغبة ملتوية", L"Искаженное желание", L"Verdrehtes Verlangen", L"Desejo distorcido", L"Verdraaid verlangen", L"Skręcone pragnienie", L"Çarpık Arzu");
			fff = 1;
		}
		else if (ft == L"y_e012.opus") {
			a = LL14(L"手筈通りに", L"As Planned", L"Comme prévu", L"Come pianificato", L"Como se planeó", L"절차대로", L"按照计划", L"كما هو مخطط له", L"Как и планировалось", L"Wie geplant", L"Como planejado", L"Zoals gepland", L"Zgodnie z planem", L"Planlandığı Gibi");
			fff = 1;
		}
		else if (ft == L"y_f160.opus") {
			a = LL14(L"瞳の中の少年剣士", L"Young Swordsman in My Eyes", L"Le jeune épéiste dans mes yeux", L"Il giovane spadaccino nei miei occhi", L"El joven espadachín en mis ojos", L"눈 속의 소년 검사", L"瞳孔中的少年剑士", L"المبارز الفتى في عيني", L"Юный мечник в моих глазах", L"Junger Schwertkämpfer in meinen Augen", L"Jovem espadachim nos meus olhos", L"Jonge zwaardvechter in mijn ogen", L"Młody szermierz w moich oczach", L"Gözlerimdeki Genç Kılıç Ustası");
			fff = 1;
		}
		else if (ft == L"y_f200.opus") {
			a = LL14(L"錨を揚げろ！", L"Weigh Anchor!", L"Levez l'ancre !", L"Leva l'ancora!", L"¡Leven anclas!", L"닻을 올려라!", L"起锚！", L"ارفعوا المرساة!", L"Поднять якорь!", L"Anker lichten!", L"Levantar âncora!", L"Licht het anker!", L"Podnieść kotwicę!", L"Demir Al!");
			fff = 1;
		}
		else if (ft == L"y_f210.opus") {
			a = LL14(L"悠き海に生きる者", L"Those Who Live in the Vast Sea", L"Ceux qui vivent dans la mer vaste", L"Coloro che vivono nel vasto mare", L"Aquellos que viven en el mar vasto", L"유구한 바다에 사는 자", L"生活在悠久大海的人", L"الذين يعيشون في البحر الشاسع", L"Те, кто живет в бескрайнем море", L"Die im weiten Meer leben", L"Aqueles que vivem no mar vasto", L"Zij die in de onmetelijke zee leven", L"Ci, którzy żyją w rozległym morzu", L"Engin Denizlerde Yaşayanlar");
			fff = 1;
		}
		else if (ft == L"y_f220.opus") {
			a = LL14(L"コンパスは踊る", L"The Compass Dances", L"La boussole danse", L"La bussola danza", L"La brújula danza", L"나침반은 춤춘다", L"罗盤在跳舞", L"البوصلة ترقص", L"Компас танцует", L"Der Kompass tanzt", L"A bússola dança", L"Het kompas danst", L"Kompas tańczy", L"Pusula Dans Ediyor");
			fff = 1;
		}
		else if (ft == L"y_f230.opus") {
			a = LL14(L"開闢の海", L"Sea of Genesis", L"Mer de la genèse", L"Mare della genesi", L"Mar de la génesis", L"개벽의 바다", L"开辟之海", L"بحر التكوين", L"Море сотворения", L"Meer der Schöpfung", L"Mar da génese", L"Zee van de genesis", L"Morze genezy", L"Yaratılış Denizi");
			fff = 1;
		}
		else if (ft == L"y_t200.opus") {
			a = LL14(L"根ざすべき場所", L"Where We Belong", L"Là où nous appartenons", L"Il posto a cui apparteniamo", L"El lugar al que pertenecemos", L"뿌리 내려야 할 곳", L"落地生根之处", L"حيث ننتمي", L"Там, где наш дом", L"Wo wir hingehören", L"Onde pertencemos", L"Waar we thuishoren", L"Miejsce, do którego należymy", L"Ait Olduğumuz Yer");
			fff = 1;
		}
		else if (ft == L"y_t500.opus") {
			a = LL14(L"情景に揺蕩う", L"Drifting in the Scene", L"Dérivant dans la scène", L"Oscillando nella scena", L"Derivando en la escena", L"정경 속에 흔들리며", L"浸于情景中", L"تائه في المشهد", L"Дрейфуя в пейзаже", L"In der Szenerie treiben", L"Derivando na cena", L"Drijvend in de scène", L"Dryfując w scenerii", L"Manzarada Süzülmek");
			fff = 1;
		}
		else if (ft == L"y_t600.opus") {
			a = LL14(L"盾の兄弟", L"Shield Brothers", L"Frères de bouclier", L"Fratelli di scudo", L"Hermanos de escudo", L"방패의 형제", L"盾之兄弟", L"إخوة الدروع", L"Братья по щиту", L"Schildbrüder", L"Irmãos de escudo", L"Schildbroeders", L"Bracia tarczy", L"Kalkan Kardeşliği");
			fff = 1;
		}
		else if (ft == L"y_title.opus") {
			a = LL14(L"その優しさは誰のため", L"For Whom Is That Kindness", L"Pour qui est cette gentillesse", L"Per chi è quella gentilezza", L"Para quién es esa amabilidad", L"그 친절은 누구를 위한 것인가", L"那份温柔是为了谁", L"لمن هذا اللطف", L"Для кого эта доброта", L"Wem gilt diese Güte", L"Para quem é essa bondade", L"Voor wie is die vriendelijkheid", L"Dla kogo ta dobroć", L"Bu Nezaket Kimin İçin");
			fff = 1;
		}



		if (fff == 0)
			if (a.Left(2) == "y9") {
				if (a.Mid(4, 4) == "b001") { a = "FEEL FORCE"; }
				if (a.Mid(4, 4) == "b002") { a = "TROUBLEMAKER"; }
				if (a.Mid(4, 4) == "b003") { a = "MONSTRUM SPECTRUM"; }
				if (a.Mid(4, 4) == "b004") { a = "LACRIMA CRISIS"; }
				if (a.Mid(4, 4) == "b005") { a = "WELCOME TO CHAOS"; }
				if (a.Mid(4, 4) == "b006") { a = "JUDGEMENT TIME"; }
				if (a.Mid(4, 4) == "b007") { a = "KNOCK ON NOX"; }
				if (a.Mid(4, 4) == "b008") { a = "ANIMA ERGASTULUM"; }
				if (a.Mid(4, 5) == "b010b") { a = "URBAN TERROR"; }
				if (a.Mid(4, 4) == "b010") { a = LL14(L"URBAN TERROR(イントロあり)", L"URBAN TERROR (With Intro)", L"URBAN TERROR (Avec Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (인트로 있음)", L"URBAN TERROR (含前奏)", L"URBAN TERROR (مع مقدمة)", L"URBAN TERROR (С интро)", L"URBAN TERROR (Mit Intro)", L"URBAN TERROR (Com Intro)", L"URBAN TERROR (Met Intro)", L"URBAN TERROR (Z intro)", L"URBAN TERROR (Girişli)"); }
				if (a.Mid(4, 5) == "b011b") { a = "DREAMING IN THE GRIMWALD"; }
				if (a.Mid(4, 4) == "b011") { a = LL14(L"DREAMING IN THE GRIMWALD(イントロあり)", L"DREAMING IN THE GRIMWALD (With Intro)", L"DREAMING IN THE GRIMWALD (Avec Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (인트로 있음)", L"DREAMING IN THE GRIMWALD (含前奏)", L"DREAMING IN THE GRIMWALD (مع مقدمة)", L"DREAMING IN THE GRIMWALD (С интро)", L"DREAMING IN THE GRIMWALD (Mit Intro)", L"DREAMING IN THE GRIMWALD (Com Intro)", L"DREAMING IN THE GRIMWALD (Met Intro)", L"DREAMING IN THE GRIMWALD (Z intro)", L"DREAMING IN THE GRIMWALD (Girişli)"); }
				if (a.Mid(4, 4) == "b012") { a = "WILD CARD"; }
				if (a.Mid(4, 5) == "b014b") { a = "FULL MOON CEREMONY"; }
				if (a.Mid(4, 4) == "b014") { a = LL14(L"FULL MOON CEREMONY(イントロあり)", L"FULL MOON CEREMONY (With Intro)", L"FULL MOON CEREMONY (Avec Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (인트로 있음)", L"FULL MOON CEREMONY (含前奏)", L"FULL MOON CEREMONY (مع مقدمة)", L"FULL MOON CEREMONY (С интро)", L"FULL MOON CEREMONY (Mit Intro)", L"FULL MOON CEREMONY (Com Intro)", L"FULL MOON CEREMONY (Met Intro)", L"FULL MOON CEREMONY (Z intro)", L"FULL MOON CEREMONY (Girişli)"); }
				if (a.Mid(4, 4) == "d101") { a = "HEART BEAT SHAKER"; }
				if (a.Mid(4, 4) == "d201") { a = "CLOACA MAXIMA"; }
				if (a.Mid(4, 4) == "d301") { a = "RUIN OF DRY MOAT"; }
				if (a.Mid(4, 4) == "d401") { a = "MARIONETTE, MARIONETTE"; }
				if (a.Mid(4, 4) == "d501") { a = "THE CAVE OF GROAN"; }
				if (a.Mid(4, 4) == "d601") { a = "EVAN MACHA"; }
				if (a.Mid(4, 4) == "d701") { a = "A QUARRY RUIN"; }
				if (a.Mid(4, 4) == "d702") { a = "CROSSING A/A"; }
				if (a.Mid(4, 4) == "d801") { a = "CATCH ME IF YOU CAN"; }
				if (a.Mid(4, 4) == "d901") { a = "ALCHEMY LAB"; }
				if (a.Mid(4, 4) == "d911") { a = "STRATEGIC ZONE"; }
				if (a.Mid(4, 5) == "d1001") { a = "FORTRESS UNDERGROUND"; }
				if (a.Mid(4, 5) == "d2001") { a = "DANCE WITH TRAPS"; }
				if (a.Mid(4, 4) == "e001") { a = "APRILIS"; }
				if (a.Mid(4, 4) == "e002") { a = "TAKE IT EASY!"; }
				if (a.Mid(4, 4) == "e003") { a = "PETITE FLEUR"; }
				if (a.Mid(4, 4) == "e004") { a = "EYES ON..."; }
				if (a.Mid(4, 4) == "e005") { a = "FORGOTTEN DAYS"; }
				if (a.Mid(4, 4) == "e006") { a = "PRISON OF BALDUQ -LIVE THE FUTURE-"; }
				if (a.Mid(4, 4) == "e007") { a = "PRISON OF BALDUQ -YEARNING-"; }
				if (a.Mid(4, 4) == "e008") { a = L"IL ÉTAIT UNE FOIS"; }
				if (a.Mid(4, 4) == "e009") { a = "WHO KNOWS THE TRUTH?"; }
				if (a.Mid(4, 4) == "e010") { a = "DECISION"; }
				if (a.Mid(4, 4) == "e011") { a = "STAGNANT POOL"; }
				if (a.Mid(4, 4) == "e013") { a = "INQUISITION"; }
				if (a.Mid(4, 4) == "e014") { a = "SILLY MEETING"; }
				if (a.Mid(4, 4) == "e016") { a = "MONSTRUM NOX"; }
				if (a.Mid(4, 4) == "e017") { a = "CHALLENGER'S ROAD"; }
				if (a.Mid(4, 4) == "e018") { a = "RED MULETA"; }
				if (a.Mid(4, 4) == "e019") { a = "NAB THE TAIL"; }
				if (a.Mid(4, 4) == "e020") { a = "THUS SPOKE AN ALCHEMIST"; }
				if (a.Mid(4, 4) == "e023") { a = "DENOUEMENT"; }
				if (a.Mid(4, 4) == "e024") { a = "INVITATION TO THE CRIMSON NIGHT"; }
				if (a.Mid(4, 4) == "f101") { a = "NORSE WIND"; }
				if (a.Mid(4, 4) == "f201") { a = "TRANQUIL SILENCE"; }
				if (a.Mid(4, 4) == "f301") { a = "GLESSING WAY!"; }
				if (a.Mid(4, 4) == "f501") { a = "DESERT AFTER TEARS"; }
				if (a.Mid(4, 4) == "muon") { a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"무음", L"无音", L"صمت", L"Тишина", L"Stille", L"Silencio", L"Stilte", L"Cisza", L"Sessizlik"); }
				if (a.Mid(4, 4) == "t101") { a = "PRISONCITY"; }
				if (a.Mid(4, 4) == "t102") { a = "IN PROFILE, ON BELFRY"; }
				if (a.Mid(4, 4) == "t103") { a = "NEW LIFE"; }
				if (a.Mid(4, 4) == "t104") { a = "GRIA RECOLLECTION"; }
				if (a.Mid(4, 4) == "t201") { a = "BAR \"DANDELION\""; }
				if (a.Mid(4, 4) == "t301") { a = "AMBIGUOUS TERRITORY"; }
				if (a.Mid(4, 4) == "t402") { a = "WALTZ FOR GRACE"; }
				if (a.Mid(4, 4) == "t501") { a = "HEAT AND SPLENDOR"; }
				if (a.Mid(4, 4) == "t901") { a = "ONLY THE CORPSE GOES OUT"; }
				if (a.Mid(4, 4) == "t902") { a = "A GOLDEN KEY CAN OPEN ANY DOOR"; }
				if (a.Mid(4, 4) == "tbox") { a = "TREASURE BOX -Ys IX-"; }
			}
			else {
				switch (_ttoi(a.Mid(2, 5))) {
				case 81004:
					a = LL14(L"罪と罰と偽りと", L"Sin, Punishment and Falsehood", L"Péché, punition et mensonge", L"Peccato, punizione e falsità", L"Pecado, castigo y falsedad", L"죄와 벌과 거짓과", L"罪、罰與欺偽", L"الخطيئة والعقاب والزور", L"Грех, наказание и ложь", L"Sünde, Strafe und Falschheit", L"Pecado, castigo e falsidade", L"Zonde, straf en valsheid", L"Grzech, kara i fałsz", L"Günah, Ceza ve Sahtelik");
					break;
				case 81005:
					a = LL14(L"昏き鐘の残響", L"Resonance of the Dark Bell", L"Résonance de la cloche sombre", L"Risonanza della campana oscura", L"Resonancia de la campana oscura", L"어두운 종의 잔향", L"昏暗之鐘的殘響", L"رنين الجرس المظلم", L"Резонанс темного колокола", L"Resonanz der dunklen Glocke", L"Ressonância do sino sombrio", L"Resonantie van de duistere klok", L"Rezonans mrocznego dzwonu", L"Karanlık Canın Yankısı");
					break;
				case 81006:
					a = "Right on the Mark";
					break;
				case 81007:
					a = LL14(L"悪夢ふたたび", L"Nightmare Again", L"Le cauchemar recommence", L"Incubo di nuovo", L"Pesadilla de nuevo", L"악몽은 다시", L"噩夢重現", L"الكابوس يعود مجدداً", L"Кошмар снова", L"Albtraum erneut", L"Pesadelo novamente", L"Nachtmerrie opnieuw", L"Koszmar ponownie", L"Kabus Yeniden");
					break;
				case 81008:
					a = "Crossbell Nostalgia";
					break;
				case 81009:
					a = LL14(L"創まりの円庭", L"Garden of Beginnings", L"Jardin des commencements", L"Giardino degli inizi", L"Jardín de los inicios", L"시작의 원정", L"創始之圓庭", L"حديقة البدايات", L"Сад начал", L"Garten der Anfänge", L"Jardim dos começos", L"Tuin van het begin", L"Ogród początków", L"Başlangıç Bahçesi");
					break;
				case 81010:
					a = "Mysterious Element";
					break;
				case 81012:
					a = "Stand Up Again and Again!";
					break;
				case 81014:
					a = "Purgatory Scream";
					break;
				case 81015:
					a = LL14(L"さざめきの途路", L"Path of Tumult", L"Chemin du tumulte", L"Sentiero del tumulto", L"Senda del tumulto", L"웅성거림의 길", L"嘈雜的途徑", L"طريق الاضطراب", L"Путь суматохи", L"Pfad des Tumults", L"Caminho do tumulto", L"Pad van rumoer", L"Ścieżka zgiełku", L"Gürültülü Yol");
					break;
				case 81016:
					a = LL14(L"蒼の大地に生きる者", L"Those Who Live on the Azure Land", L"Ceux qui vivent sur la terre d'azur", L"Coloro che vivono sulla terra azzurra", L"Aquellos que viven en la tierra azul", L"창의 대지에 사는 자", L"生活在蒼之大地的人", L"الذين يعيشون على الأرض الزرقاء", L"Те, кто живет на лазурной земле", L"Die auf dem azurblauen Land leben", L"Aqueles que vivem na terra azul", L"Zij die op het azuurblauwe land leven", L"Ci, którzy żyją na błękitnej ziemi", L"Mavi Topraklarda Yaşayanlar");
					break;
				case 81017:
					a = LL14(L"黎明の鐘", L"Bell of Dawn", L"Cloche de l'aube", L"Campana dell'alba", L"Campana del alba", L"여명의 종", L"黎明之鐘", L"جرس الفجر", L"Колокол рассвета", L"Glocke der Dämmerung", L"Sino da aurora", L"Klok van de dageraad", L"Dzwon świtu", L"Şafak Canı");
					break;
				case 81018:
					a = LL14(L"レメディファンタジア -仲間とともに-", L"Remedi Fantasia -With Comrades-", L"Remedi Fantasia -Avec des camarades-", L"Remedi Fantasia -Con i compagni-", L"Remedi Fantasia -Con camaradas-", L"레메디 판타지아 ~동료와 함께~", L"Remedi Fantasia -與夥伴一起-", L"ريميدي فانتازيا -مع الرفاق-", L"Remedi Fantasia -С товарищами-", L"Remedi Fantasia -Mit Kameraden-", L"Remedi Fantasia -Com camaradas-", L"Remedi Fantasia -Met kameraden-", L"Remedi Fantasia -Z towarzyszami-", L"Remedi Fantasia -Yoldaşlarla-");
					break;
				case 81019:
					a = "Slight Suspicion";
					break;
				case 81020:
					a = "Maliciousness in the Mirror";
					break;
				case 81021:
					a = LL14(L"暗澹たる世界", L"Dark World", L"Monde sombre", L"Mondo oscuro", L"Mundo oscuro", L"암담한 세계", L"暗淡的世界", L"عالم مظلم", L"Мрачный мир", L"Dunkle Welt", L"Mundo sombrio", L"Duistere wereld", L"Mroczny świat", L"Karanlık Dünya");
					break;
				case 81022:
					a = LL14(L"ひとときの温もり", L"Brief Warmth", L"Bref répit de chaleur", L"Breve calore", L"Breve calor", L"한때의 온기", L"片刻的溫暖", L"دفء عابر", L"Краткое тепло", L"Kurze Wärme", L"Breve calor", L"Korte warmte", L"Krótkie ciepło", L"Kısa Süreli Sıcaklık");
					break;
				case 81023:
					a = LL14(L"今、創まりのとき", L"Now, the Moment of Creation", L"Maintenant, le moment de la création", L"Ora, il momento della creazione", L"Ahora, el momento de la creación", L"지금, 시작의 시간", L"現在，創始之時", L"الآن، لحظة التأسيس", L"Теперь момент сотворения", L"Nun, der Moment der Schöpfung", L"Agora, o momento da criação", L"Nu, het moment van creatie", L"Teraz moment stworzenia", L"Şimdi, Yaratılış Anı");
					break;
				case 81024:
					a = "KERAUNOS -Fear and Hatred-";
					break;
				case 81025:
					a = LL14(L"亡失われた魂", L"Lost Souls", L"Âmes perdues", L"Anime perse", L"Almas perdidas", L"상실된 영혼", L"迷失的靈魂", L"الأرواح المفقودة", L"Потерянные души", L"Verlorene Seelen", L"Almas perdidas", L"Verloren zielen", L"Zagubione dusze", L"Kayıp Ruhlar");
					break;
				case 81026:
					a = LL14(L"穏やかな時間", L"Peaceful Time", L"Temps paisible", L"Tempo pacifico", L"Tiempo pacífico", L"평온한 시간", L"平靜的時光", L"وقت هادئ", L"Мирное время", L"Friedliche Zeit", L"Tempo pacífico", L"Vredige tijd", L"Spokojny czas", L"Huzurlu Vakit");
					break;
				case 81028:
					a = LL14(L"運命という名の歯車", L"Gears of Fate", L"Engrenages du destin", L"Ingranaggi del destino", L"Engranajes del destino", L"운명이라는 이름의 톱니바퀴", L"名為命運的齒輪", L"تروس القدر", L"Шестеренки судьбы", L"Zahnräder des Schicksals", L"Engrenagens do destino", L"Raderen van het lot", L"Koła zębate losu", L"Kader Çarkları");
					break;
				case 81200:
					a = "Crossing Causal Lines";
					break;
				case 81201:
					a = "Glittering Mirage";
					break;
				case 81202:
					a = "Like a Whirlwind";
					break;
				case 81203:
					a = "Hide and Seek by Myself";
					break;
				case 81315:
					a = L"Mines Town Mainz -Reverie Ver.-";
					break;
				case 81316:
					a = L"Path of Echoes -Reverie Ver.-";
					break;
				case 81317:
					a = "Raindrops with the Wind";
					break;
				case 81319:
					a = LL14(L"陽溜まりにただいまを", L"Home in the Sunshine", L"Retour au soleil", L"A casa sotto il sole", L"Hogar bajo el sol", L"햇살 아래 다녀왔습니다", L"在陽光下，我回來了", L"العودة للمنزل في ضوء الشمس", L"Домой под лучами солнца", L"Zuhause im Sonnenschein", L"Lar sob o sol", L"Thuis in de zon", L"Dom w słońcu", L"Güneş Işığında Eve Dönüş");
					break;
				case 81320:
					a = "Wind-Up Yesterday!";
					break;
				case 81321:
					a = LL14(L"零の邂逅", L"Zero Encounter", L"Rencontre de zéro", L"Incontro zero", L"Encuentro cero", L"영의 해후", L"零之邂逅", L"لقاء الصفر", L"Встреча Зеро", L"Zero-Begegnung", L"Encontro zero", L"Zero ontmoeting", L"Spotkanie zero", L"Sıfır Karşılaşması");
					break;
				case 81322:
					a = LL14(L"影の見えざる手", L"Invisible Hand in the Shadows", L"Main invisible dans l'ombre", L"Mano invisibile nelle ombre", L"Mano invisible en las sombras", L"그림자의 보이지 않는 손", L"影子那看不見的手", L"اليد الخفية في الظلال", L"Невидимая рука в тени", L"Unsichtbare Hand im Schatten", L"Mão invisível nas sombras", L"Onzichtbare hand in de schaduw", L"Niewidzialna ręka w cieniu", L"Gölgedeki Görünmez El");
					break;
				case 82065:
					a = LL14(L"鋼鉄牙城", L"Iron Fortress", L"Forteresse d'acier", L"Fortezza d'acciaio", L"Fortaleza de acero", L"강철아성", L"鋼鐵牙城", L"القلعة الحديدية", L"Железная крепость", L"Eiserne Festung", L"Fortaleza de aço", L"IJzeren vesting", L"Stalowa twierdza", L"Demir Kale");
					break;
				case 82113:
					a = "Zero Break Battle";
					break;
				case 82114:
					a = "Stake Everything Strategy";
					break;
				case 82124:
					a = "POM's Paradise";
					break;
				case 82125:
					a = LL14(L"波間に弾む心", L"Heart Bouncing on the Waves", L"Cœur bondissant sur les vagues", L"Cuore che rimbalza sulle onde", L"Corazón saltando en las olas", L"물결 속에 설레는 마음", L"在波浪間雀躍的心", L"قلب يقفز فوق الأمواج", L"Сердце, прыгающее на волнах", L"Herz, das auf den Wellen hüpft", L"Coração saltitando nas ondas", L"Hart dat stuitert op de golven", L"Serce skaczące na falach", L"Dalgalarda Hoplayan Kalp");
					break;
				case 82129:
					a = "Reverse Babel";
					break;
				case 82131:
					a = "Aim a Gun at the Bullet";
					break;
				case 82133:
					a = "Section G.F.S. II";
					break;
				case 82135:
					a = "Magical Revolt";
					break;
				case 82136:
					a = LL14(L"流麗闘冴", L"Elegant Battle", L"Combat élégant", L"Battaglia elegante", L"Batalla elegante", L"유려투사", L"流麗鬥冴", L"معركة أنيقة", L"Элегантная битва", L"Eleganter Kampf", L"Batalha elegante", L"Elegant gevecht", L"Elegancka bitwa", L"Zarif Savaş");
					break;
				case 82137:
					a = "The Road to All-Out War";
					break;
				case 82138:
					a = "LAPIS";
					break;
				case 82140:
					a = "Invisible Hilly Country";
					break;
				case 82141:
					a = LL14(L"ひとかけらの光明", L"Sliver of Light", L"Lueur d'espoir", L"Barlume di luce", L"Rayo de luz", L"한 조각의 광명", L"一絲光明", L"بصيص من الأمل", L"Лучик света", L"Ein Schimmer Licht", L"Raio de luz", L"Lichtstraaltje", L"Promyk światła", L"Bir Işık Huzmesi");
					break;
				case 82143:
					a = LL14(L"反攻の烽火", L"Beacon of Counterattack", L"Signal de contre-attaque", L"Segnale di contrattacco", L"Señal de contraataque", L"반격의 봉화", L"反攻的烽火", L"منارة الهجوم المضاد", L"Маяк контратаки", L"Leuchtfeuer des Gegenangriffs", L"Sinal de contra-ataque", L"Baken van de tegenaanval", L"Sygnał kontrataku", L"Karşı Atak İşareti");
					break;
				case 82147:
					a = "Rapid Wind";
					break;
				case 82148:
					a = "NO END NO WORLD -Instrumental Ver.-";
					break;
				case 82150:
					a = "Be Caught Up!";
					break;
				case 82151:
					a = "Breeding Innumerable Arms";
					break;
				case 82152:
					a = "The Destination of FATE";
					break;
				case 82154:
					a = "Twinkle Attack";
					break;
				case 82157:
					a = "Sword of Swords";
					break;
				case 82158:
					a = LL14(L"今宵は宴と参りましょう", L"Tonight We Feast", L"Ce soir, nous festoyons", L"Stasera banchettiamo", L"Esta noche festejamos", L"오늘 밤은 연회를 열지요", L"今夜讓我們舉行宴會吧", L"الليلة سنقيم مأدبة", L"Сегодня мы пируем", L"Heute Abend wird gefeiert", L"Esta noite vamos festejar", L"Vanavond vieren we feest", L"Dziś wieczorem ucztujemy", L"Bu Gece Ziyafet Çekelim");
					break;
				case 82159:
					a = "Flash Your Fighting Spirit";
					break;
				case 82161:
					a = LL14(L"鈍色に這う", L"Crawling in Gray", L"Ramper dans le gris", L"Strisciando nel grigio", L"Gateando en el gris", L"회색빛으로 기어가다", L"在灰色中爬行", L"الزحف في اللون الرمادي", L"Ползти в сером", L"Kriechen im Grau", L"Rastejando no cinza", L"Kruipen in het grijs", L"Pełzanie w szarości", L"Gri İçinde Sürünmek");
					break;
				case 82163:
					a = "Pyro Labyrinth";
					break;
				case 82164:
					a = LL14(L"優しさを未来に託して", L"Entrust Kindness to the Future", L"Confier la gentillesse au futur", L"Affidare la gentilezza al futuro", L"Confiar la amabilidad al futuro", L"상냥함을 미래에 맡기고", L"將溫柔託付給未來", L"إيداع اللطف للمستقبل", L"Вверить доброту будущему", L"Güte der Zukunft anvertrauen", L"Confiar a bondade ao futuro", L"Vriendelijkheid aan de toekomst toevertrouwen", L"Powierzyć dobroć przyszłości", L"Nezaketi Geleceğe Emanet Etmek");
					break;
				case 82166:
					a = LL14(L"高らかに、誇らしく", L"Loud and Proud", L"Fort et fier", L"Forte e fiero", L"Fuerte y orgulloso", L"드높게, 자랑스럽게", L"高聲地，自豪地", L"بصوت عال وبكل فخر", L"Громко и гордо", L"Laut und stolz", L"Alto e orgulhoso", L"Luid en trots", L"Głośno i dumnie", L"Yüksek Sesle ve Gururla");
					break;
				case 82170:
					a = "Infinity Rage";
					break;
				case 82171:
					a = "Heavy Violent Match";
					break;
				case 82173:
					a = "Roar of Evil Spirits";
					break;
				case 82174:
					a = "Bad Dream Invasion";
					break;
				case 82175:
					a = "Golden Fever";
					break;
				case 82177:
					a = "The Perfect Steel of ZERO";
					break;
				case 82178:
					a = "Twilight Hermitage";
					break;
				case 82179:
					a = "Something Luxury...?";
					break;
				case 82183:
					a = "Challenger Invigorated";
					break;
				case 82184:
					a = LL14(L"このあと美味しくいただきました", L"Then We Ate Deliciously", L"Ensuite, nous avons mangé délicieusement", L"Poi abbiamo mangiato deliziosamente", L"Luego comimos deliciosamente", L"이후 맛있게 먹었습니다", L"在那之後我們美味地享用了", L"بعد ذلك استمتعنا بالأكل", L"Затем мы вкусно поели", L"Dann haben wir köstlich gegessen", L"Depois comemos deliciosamente", L"Daarna hebben we heerlijk gegeten", L"Potem zjedliśmy wybornie", L"Sonra Afiyetle Yedik");
					break;
				case 82186:
					a = "Emergency Order";
					break;
				case 82188:
					a = LL14(L"激烈! 撃滅! ミシュナイダー!!", L"Fierce! Crush! Mishnayder!!", L"Féroce ! Écraser ! Mishnayder !!", L"Feroce! Schiaccia! Mishnayder!!", L"¡Feroz! ¡Aplasta! ¡Mishnayder!", L"격렬! 격멸! 미슈나이더!!", L"激烈！擊滅！Mishnayder！！", L"ضارٍ! ساحق! ميشنايدر!!", L"Яростно! Разгромить! Mishnayder!!", L"Heftig! Zerschmettern! Mishnayder!!", L"Feroz! Esmagar! Mishnayder!!", L"Heftig! Verpletter! Mishnayder!!", L"Gwałtownie! Zmiażdży! Mishnayder!!", L"Sert! Ez Geç! Mishnayder!!");
					break;
				case 82189:
					a = "Life Goes On";
					break;
				case 8001:
					a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"특과 클래스 《VII반》", L"特科班《VII組》", L"الفئة السابعة", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klas VII", L"Klasa VII", L"Sınıf VII");
					break;
				case 8002:
					a = LL14(L"スタートライン", L"Start Line", L"Ligne de départ", L"Linea di partenza", L"Linea de salida", L"스타트 라인", L"起跑線", L"خط البداية", L"Стартовая линия", L"Startlinie", L"Linha de partida", L"Startlijn", L"Linia startu", L"Başlangıç Çizgisi");
					break;
				case 8006:
					a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours vers l'avant", L"Sempre avanti", L"Siempre adelante", L"오직 한결같이 앞으로", L"一心一意，向前邁進", L"إلى الأمام دائماً", L"Только вперед", L"Immer vorwärts", L"Sempre em frente", L"Altijd vooruit", L"Zawsze do przodu", L"Daima İleri");
					break;
				case 8007:
					a = LL14(L"縁 -つなぐもの-", L"Fate -Connecting-", L"Destin -Connexion-", L"Destino -Connessione-", L"Destino -Conexión-", L"인연 ~이어주는 것~", L"緣 -連結者-", L"الروابط -ما يجمعنا-", L"Судьба -Связующее звено-", L"Schicksal -Verbindend-", L"Destino -Conectando-", L"Lot -Verbindend-", L"Los -Łączący-", L"Kader -Bağlayıcı-");
					break;
				case 8150:
					a = LL14(L"下校途中にパンケーキ", L"Pancakes on the Way Home", L"Des pancakes sur le chemin du retour", L"Pancake sulla via di casa", L"Tortitas de camino a casa", L"하교 길에 팬케이크", L"下學路上的煎餅", L"بانكيك في طريق العودة", L"Блинчики по дороге домой", L"Pfannkuchen auf dem Heimweg", L"Panquecas no caminho para casa", L"Pannenkoeken op weg naar huis", L"Naleśniki w drodze do domu", L"Eve Giderken Krep");
					break;
				case 8151:
					a = LL14(L"可能性は無限大", L"Infinite Possibilities", L"Possibilités infinies", L"Possibilità infinite", L"Posibilidades infinitas", L"가능성은 무한대", L"可能性是無限的", L"احتمالات لا حصر لها", L"Бесконечные возможности", L"Unbegrenzte Möglichkeiten", L"Possibilidades infinitas", L"Oneindige mogelijkheden", L"Nieskończone możliwości", L"Sonsuz Olasılıklar");
					break;
				case 8152:
					a = LL14(L"夜のしじまに", L"In the Night Silence", L"Dans le silence nocturne", L"Nel silenzio della notte", L"En el silencio de la noche", L"밤의 정적 속에", L"在深夜的靜謐中", L"في صمت الليل", L"В ночной тишине", L"In der nächtlichen Stille", L"No silêncio da noite", L"In de nachtelijke stilte", L"W nocnej ciszy", L"Gece Sessizliğinde");
					break;
				case 8153:
					a = LL14(L"夕景", L"Evening Scene", L"Scène de soirée", L"Scena serale", L"Escena vespertina", L"석양 풍경", L"夕陽美景", L"مشهد المساء", L"Вечерний пейзаж", L"Abendszene", L"Cena noturna", L"Avondtafereel", L"Wieczorna scena", L"Akşam Manzarası");
					break;
				case 8154:
					a = LL14(L"新しい朝", L"New Morning", L"Nouveau matin", L"Nuovo mattino", L"Nueva mañana", L"새로운 아침", L"新的早晨", L"صباح جديد", L"Новое утро", L"Neuer Morgen", L"Nova manhã", L"Nieuwe ochtend", L"Nowy poranek", L"Yeni Sabah");
					break;
				case 8156:
					a = LL14(L"白亜の旧都セントアーク", L"White City St. Ark", L"Vieille capitale blanche St. Ark", L"Antica capitale bianca St. Ark", L"Vieja capital blanca St. Ark", L"백아의 구도 세인트아크", L"白亞舊都 St. Ark", L"مدينة سانت آرك البيضاء", L"Белая старая столица Сент-Арк", L"Weiße alte Hauptstadt St. Ark", L"Antiga capital branca St. Ark", L"Witte oude hoofdstad St. Ark", L"Biała stara stolica St. Ark", L"Beyaz Eski Başkent St. Ark");
					break;
				case 8157:
					a = LL14(L"紡績町パルム", L"Spinning Town Parm", L"Ville textile Parm", L"Città tessile Parm", L"Pueblo textil Parm", L"방직 마을 파름", L"紡織鎮 Parm", L"بلدة بارم للغزل", L"Ткацкий городок Парм", L"Spinnereistadt Parm", L"Vila têxtil Parm", L"Spinnerijstad Parm", L"Tkackie miasto Parm", L"Dokuma Kasabası Parm");
					break;
				case 8158:
					a = LL14(L"籠の中のクロスベル", L"Crossbell in a Cage", L"Crossbell en cage", L"Crossbell in gabbia", L"Crossbell en una jaula", L"장 안의 크로스벨", L"籠中 Crossbell", L"كروسبيل في قفص", L"Кроссбелл в клетке", L"Crossbell im Käfig", L"Crossbell em uma gaiola", L"Crossbell in een kooi", L"Crossbell w klatce", L"Kafesteki Crossbell");
					break;
				case 8159:
					a = LL14(L"今、成すべきこと", L"What Must Be Done Now", L"Ce qui doit être fait maintenant", L"Ciò che deve essere fatto ora", L"Lo que debe hacerse ahora", L"지금, 해야 할 일", L"現在，應做之事", L"ما يجب القيام به الآن", L"Что должно быть сделано сейчас", L"Was jetzt getan werden muss", L"O que deve ser feito agora", L"Wat nu moet worden gedaan", L"Co należy teraz zrobić", L"Şimdi Yapılması Gereken");
					break;
				case 8160:
					a = LL14(L"歓楽都市ラクウェル", L"Pleasure City Raquel", L"Ville de plaisir Raquel", L"Città del piacere Raquel", L"Ciudad del placer Raquel", L"환락 도시 라크웰", L"歡樂都市 Raquel", L"مدينة راكيل للترفيه", L"Город развлечений Ракель", L"Vergnügungsstadt Raquel", L"Cidade do prazer Raquel", L"Plezierstad Raquel", L"Miasto rozrywki Raquel", L"Eğlence Şehri Raquel");
					break;
				case 8161:
					a = LL14(L"静かなる駆け引き", L"Quiet Maneuvering", L"Manœuvres silencieuses", L"Manovre silenziose", L"Maniobras silenciosas", L"조용한 밀고 당기기", L"靜默的周旋", L"مناورة هادئة", L"Тихое маневрирование", L"Stilles Manövrieren", L"Manobras silenciosas", L"Stil manoeuvreren", L"Ciche manewry", L"Sessiz Manevralar");
					break;
				case 8162:
					a = LL14(L"赫奕たるヘイムダル", L"Splendid Heimdallr", L"Heimdallr splendide", L"Splendida Heimdallr", L"Espléndida Heimdallr", L"혁혁한 헤임달", L"赫赫有名的 Heimdallr", L"هايمدال العظيمة", L"Великолепный Хеймдалль", L"Prächtiges Heimdallr", L"Esplêndida Heimdallr", L"Prachtig Heimdallr", L"Wspaniały Heimdallr", L"Görkemli Heimdallr");
					break;
				case 8163:
					a = LL14(L"紺碧の海都オルディス", L"Azure Port City Ordys", L"Ville portuaire d'azur Ordys", L"Città portuale azzurra Ordys", L"Ciudad portuaria azul Ordys", L"금벽의 해도 오르디스", L"紺碧海都 Ordys", L"مدينة أورديس الساحلية الفيروزية", L"Лазурный портовый город Ордис", L"Azurblaue Hafenstadt Ordys", L"Cidade portuaria azul Ordys", L"Azuurblauwe havenstad Ordys", L"Błękitne miasto portowe Ordys", L"Gök Mavisi Liman Şehri Ordys");
					break;
				case 8164:
					a = LL14(L"最前線都市", L"Front-line City", L"Ville de première ligne", L"Città di prima linea", L"Ciudad de primera línea", L"최전선 도시", L"最前線都市", L"مدينة الخطوط الأمامية", L"Прифронтовой город", L"Frontstadt", L"Cidade de linha de frente", L"Frontstad", L"Miasto na linii frontu", L"Cephe Şehri");
					break;
				case 8166:
					a = LL14(L"精強なる兵たち", L"Elite Soldiers", L"Soldats d'élite", L"Soldati d'élite", L"Soldados de elite", L"정예병들", L"精強的士兵們", L"جنود النخبة", L"Элитные солдаты", L"Elitesoldaten", L"Soldados de elite", L"Elitesoldaten", L"Elitarni żołnierze", L"Seçkin Askerler");
					break;
				case 8170:
					a = LL14(L"隠れ里エリン", L"Hidden Village Erin", L"Village caché d'Erin", L"Villaggio nascosto di Erin", L"Aldea oculta de Erin", L"은둔 마을 에린", L"隠之里 Erin", L"قرية إيرين المخفية", L"Скрытая деревня Эрин", L"Verborgenes Dorf Erin", L"Vila oculta de Erin", L"Verborgen dorp Erin", L"Ukryta wioska Erin", L"Gizli Köy Erin");
					break;
				case 8171:
					a = LL14(L"潜入調査", L"Infiltration", L"Infiltration", L"Infiltrazione", L"Infiltración", L"잠입 조사", L"潛入調查", L"استطلاع تسللي", L"Инфильтрация", L"Infiltration", L"Infiltração", L"Infiltratie", L"Infiltracja", L"Sızma Harekatı");
					break;
				case 8173:
					a = LL14(L"紅き閃影 -光まとう翼-", L"Crimson Flash -Wings of Light-", L"Éclat carmin -Ailes de lumière-", L"Lampo cremisi -Ali di luce-", L"Destello carmesí -Alas de luz-", L"붉은 섬영 ~빛을 두른 날개~", L"紅之閃影 -披光之翼-", L"الوميض القرمزي -أجنحة الضوء-", L"Алая вспышка -Крылья света-", L"Purpurroter Blitz -Flügel des Lichts-", L"Lampejo carmesim -Asas de luz-", L"Karmozijnrode flits -Vleugels van licht-", L"Szkarłatny błysk -Skrzydła światła-", L"Kızıl Parıltı -Işık Kanatları-");
					break;
				case 8175:
					a = LL14(L"一抹の不安、一縷の望み", L"Hint of Unease, Ray of Hope", L"Une pointe d'inquiétude, un rayon d'espoir", L"Un briciolo di ansia, un raggio di speranza", L"Un rastro de inquietud, un rayo de esperanza", L"일말의 불안, 한 줄기 희망", L"一抹不安，一縷希望", L"لمسة قلق، شعاع أمل", L"Тень беспокойства, луч надежды", L"Ein Hauch von Unbehagen, ein Hoffnungsschimmer", L"Um toque de inquietação, um raio de esperança", L"Een spoortje van onrust, een straal van hoop", L"Cień niepokoju, promień nadziei", L"Bir Parça Huzursuzluk, Bir Umut Işığı");
					break;
				case 8177:
					a = LL14(L"水面を渡る風", L"Wind Over the Water", L"Vent sur l'eau", L"Vento sull'acqua", L"Viento sobre el agua", L"수면을 건너는 바람", L"拂過水面的風", L"الريح فوق الماء", L"Ветер над водой", L"Wind über dem Wasser", L"Vento sobre a água", L"Wind over het water", L"Wiatr nad wodą", L"Su Üstündeki Rüzgar");
					break;
				case 8452:
					a = LL14(L"剣戟怒涛", L"Sword and Lance Storm", L"Tempête d'épées et de lances", L"Tempesta di spade e lance", L"Tormenta de espadas y lanzas", L"검격노도", L"劍戟怒濤", L"عاصفة السيوف والرماح", L"Шторм мечей и копий", L"Schwert- und Lanzensturm", L"Tempestade de espadas e lanças", L"Zwaard- en lansstorm", L"Burza mieczy i włóczni", L"Kılıç ve Mızrak Fırtınası");
					break;
				case 8475:
					a = LL14(L"古の盟約", L"Ancient Covenant", L"Ancienne alliance", L"Antico patto", L"Antiguo pacto", L"고대의 맹약", L"古代盟約", L"العهد القديم", L"Древний завет", L"Alter Bund", L"Antigo pacto", L"Oud verbond", L"Starożytne przymierze", L"Kadim Sözleşme");
					break;
				case 8714:
					a = LL14(L"巨竜目覚める", L"The Great Dragon Awakens", L"Le grand dragon s'éveille", L"Il grande drago si risveglia", L"El gran dragón despierta", L"거룡 깨어나다", L"巨龍覺醒", L"استيقاظ التنين العظيم", L"Великий дракон пробуждается", L"Der große Drache erwacht", L"O grande dragão desperta", L"De grote draak ontwaakt", L"Wielki smok się budzi", L"Büyük Ejderha Uyanıyor");
					break;
				case 8715:
					a = LL14(L"未来へ。", L"To the Future.", L"Vers le futur.", L"Verso il futuro.", L"Hacia el futuro.", L"미래로.", L"往未來。", L"إلى المستقبل.", L"В будущее.", L"In die Zukunft.", L"Para o futuro.", L"Naar de toekomst.", L"W przyszłość.", L"Geleceğe.");
					break;
				case 8720:
					a = LL14(L"明日への軌跡", L"Trails to Tomorrow", L"Sillage vers demain", L"Tracce verso il domani", L"Estela hacia el mañana", L"내일로의 궤적", L"通向明天的軌跡", L"مسارات الغد", L"Пути в завтрашний день", L"Pfade nach morgen", L"Rastros para o amanhã", L"Sporen naar morgen", L"Ścieżki do jutra", L"Yarına Giden İzler");
					break;
				case 8721:
					a = LL14(L"愛の詩(歌)", L"Poem of Love (vocal)", L"Poème d'amour (vocal)", L"Poema d'amore (vocal)", L"Poema de amor (vocal)", L"사랑의 시 (노래)", L"愛之詩(歌)", L"قصيدة الحب", L"Поэма о любви (вокал)", L"Liebesgedicht (Gesang)", L"Poema de amor (vocal)", L"Liefdesgedicht (vocaal)", L"Poemat miłości (wokal)", L"Aşk Şiiri (vokal)");
					break;
				case 8802:
					a = LL14(L"風よりも駿く", L"Swifter Than the Wind", L"Plus rapide que le vent", L"Più veloce del vento", L"Más rápido que el viento", L"바람보다 빠르게", L"比風更迅捷", L"أسرع من الريح", L"Быстрее ветра", L"Schneller als der Wind", L"Mais rápido que o vento", L"Sneller dan de wind", L"Szybszy niż wiatr", L"Rüzgardan Daha Hızlı");
					break;
				default:
					if (a == L"ed8_inf_ex.opus") {
						a = LL14(L"夢幻の彼方へ", L"To the Realm of Dreams", L"Vers le royaume des rêves", L"Verso il regno dei sogni", L"Hacia el reino de los sueños", L"몽환의 저편으로", L"往夢幻的彼方", L"إلى مملكة الأحلام", L"В царство снов", L"In das Reich der Träume", L"Para o reino dos sonhos", L"Naar het rijk der dromen", L"Do krainy snów", L"Rüyalar Alemine");
					}
					else if (a.Find(L"muon") != -1 || a.Find(L"不明") != -1 || a.Find(L"Unknown") != -1) {
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"미상", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					}
					break;
				}
			}

		if (a.Left(3) == L"ed7") {
			int b = _ttoi(a.Mid(3, 3));
			CString fil = filen.Left(filen.ReverseFind(L'\\')) + L"\\..\\..\\data\\bgm\\info.yaml";
			FILE* fp;
			errno_t ferr;
			ferr = _tfopen_s(&fp, fil, _T("r, ccs=UTF-8"));
			if (ferr == 0) {
				CStdioFile fzero(fp);
				fzero.SeekToBegin();
				CString stf, stl, stn;
				BOOL ck = FALSE;
				for (;;) {
					if (fzero.ReadString(stf) == FALSE) break;
					stl.Format(L"'%d'", b);
					if (stf.Find(stl) != -1) {
						ck = TRUE;
					}
					if (stf.Find(L"jp:") != -1 && ck == TRUE) {
						int k = stf.Find(L"jp:") + 4;
						stn = stf.Mid(k);
						break;
					}
				}
				if (stn != L"") {
					a = stn;
				}
				fzero.Close();
				fclose(fp);
			}
		}

		stitle = a;
		OggOpusFile* m_pOpusFile;
		int ret;
		OpusFileCallbacks cb = { NULL,NULL,NULL,NULL };
		CFile fi;
		fi.Open(filen, CFile::modeRead | CFile::shareDenyWrite);
		char by[255], by2[255];
		fi.Read(by, 255);
		fi.Close();
		int l1 = 0, l2 = 0;
		CString g = L"";
		for (int l = 0; l < 240; l++) {
			if (by[l] == 'l' && by[l + 1] == 'o' && by[l + 2] == 'o' && by[l + 3] == 'p' && by[l + 4] == 's' && by[l + 5] == '=') {
				strcpy(by2, by + l + 6);
				int flg = 0;
				for (int i = 0;; i++) {
					if (by2[i] == 0) {
						break;
					}
					if (by2[i] == '-') {
						flg = 1;
						continue;
					}
					if (flg == 0) {
						l1 *= 10;
						l1 += by2[i] - '0';
					}
					else {
						l2 *= 10;
						l2 += by2[i] - '0';
					}
				}
			}
		}
		// usHead(OpusHead)のサンプルレートは op_open_file 後の op_head() で取得するため削除
		for (int l = 0; l < 240; l++) {
			if (by[l] == 'l' && by[l + 1] == 'o' && by[l + 2] == 'o' && by[l + 3] == 'p' && by[l + 4] == 's' && by[l + 5] == 't') {
				strcpy(by2, by + l + 10);
				int flg = 0;
				for (int i = 0;; i++) {
					if (by2[i] < 0x30) {
						break;
					}
					l1 *= 10;
					l1 += by2[i] - '0';
				}
			}
		}
		for (int l = 0; l < 240; l++) {
			if (by[l] == 'l' && by[l + 1] == 'o' && by[l + 2] == 'o' && by[l + 3] == 'p' && by[l + 4] == 'e' && by[l + 5] == 'n') {
				strcpy(by2, by + l + 8);
				int flg = 0;
				for (int i = 0;; i++) {
					if (by2[i] < 0x30) {
						break;
					}
					l2 *= 10;
					l2 += by2[i] - '0';
				}
			}
		}
		//		m_pOpusFile = op_open_callbacks(op_fopen(&cb, CStringA(filen), "rb"), &cb, NULL, 0, &ret);
		void* bbbb = filen.GetBuffer();
		m_pOpusFile = op_open_file((WCHAR*)bbbb, &err);
		filen.ReleaseBuffer();

		// Opusヘッダのinput_sample_rateで44.1kHz/48kHz等を判定
		// libopusfileは常に48kHzでデコード出力するため、再生用wavbitは48kHzに固定
		{
			const OpusHead* head = op_head(m_pOpusFile, -1);
			if (head && head->input_sample_rate != 0) {
				// ヘッダの元サンプルレートをsikpiに格納（表示・将来のリサンプル用）
				og->sikpi.dwSamplesPerSec = head->input_sample_rate;
			}
			wavbit_sample_Hz = 48000;  // デコード出力は常に48kHz
		}

		ogg_int64_t totalSamples = op_pcm_total(m_pOpusFile, -1);
		op_raw_seek(m_pOpusFile, 1);
		if (adbuf2) {
			free(adbuf2); adbuf2 = NULL;
		}
		adbuf2 = (char*)calloc((size_t)totalSamples * 6 * 2, 1);

		// loops=メタデータは44.1kHz基準のことが多い。Opus出力は48kHzなので変換
		{
			DWORD metaRate = (og->sikpi.dwSamplesPerSec != 0) ? og->sikpi.dwSamplesPerSec : 44100;
			float rate = 48000.0f / (float)metaRate;  // 出力48kHz / メタデータのサンプルレート
			loop1 = (int)(l1 * rate);
			loop2 = (int)((l2 - l1) * rate);
		}
		a = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		if (a.Left(3).MakeLower() == L"ed7") {
			short b = (short)_ttoi(a.Mid(3, 3));
			Sleep(10);
			CFile fzero;
			CString fil = filen.Left(filen.ReverseFind(L'\\')) + L"\\..\\..\\data\\text\\t_bgm._dt";
			if (fzero.Open(fil, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
				fzero.SeekToBegin();
				typedef struct
				{
					int start;
					int end;
					short dm;
					short dm2;
					short track;
					char loop;
					char dm3;
				} zero;
				zero zerobuf;
				for (;;) {
					ZeroMemory(&zerobuf, 16);
					UINT ret1 = fzero.Read(&zerobuf, 16);
					if (ret1 != 16) break;
					if (b == zerobuf.track) break;
				}
				if (zerobuf.track != 0) {
					// t_bgm._dtは44.1kHz基準。出力wavbitに合わせて動的変換（opus=48k,他=44.1k等）
					float rate = (float)wavbit_sample_Hz / 44100.0f;
					loop1 = (int)(zerobuf.start * rate);
					loop2 = (int)(zerobuf.end * rate);
				}
			}
		}

		int i = 0;
		lenl = 0;
		data_size = oggsize = totalSamples * 4;
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		og->m_time.SetSelection(loop1, loop2);
		wavwait = 1;
		if (wav)free(wav);
		wav_start();
		int iii = 0;
		for (;; iii++) {
			BYTE budf[5760 * 4];
			int ret = op_read_stereo(m_pOpusFile, (opus_int16*)budf, 5760);
			if (ret <= 0) {//デコード終了
				break;
			}
			memcpy(adbuf2 + i * 4, budf, ret * 4);
			if (thend1 == TRUE) { thend = 1; op_free(m_pOpusFile);  return; }
			if (i >= dl)
				wavwait = 0;
			i += ret;
		}
		op_free(m_pOpusFile);

	}
	else if (mode == 10) {
		lenl = 0;
		CFile adpcmf;
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		adpcmf.Read(bbuf, 0x2c);
		int si = (int)bbuf[4] + (int)bbuf[5] * 256 + (int)bbuf[6] * 65536 + (int)bbuf[7] * 256 * 65536;
		adbuf2 = (char*)calloc((size_t)adpcmf.GetLength() * 4 * 2, 1);
		data_size = oggsize = (int)adpcmf.GetLength() * 4 - (int)(adpcmf.GetLength() * 14 * 4) / 2048 - (int)(adpcmf.GetLength() * 8 * 4) / 0x5000 - 0x2c;
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		if (readadpcm(adpcmf, adbuf2, (int)adpcmf.GetLength())) { thend = 1; adpcmf.Close(); return; }
		adpcmf.Close();
	}
	else if (mode == 15) {
		lenl = 0;
		CFile adpcmf;
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		adpcmf.Read(bbuf, 16);
		int si, jk, jk2;
		jk = (int)(BYTE)bbuf[12] + (int)(BYTE)bbuf[13] * 256 + (int)(BYTE)bbuf[14] * 65536 + (int)(BYTE)bbuf[15] * 256 * 65536;
		adpcmf.Seek(jk, CFile::begin);
		adpcmf.Read(bbuf, 0x7c);
		for (jk2 = 0;; jk2++) {
			if (bbuf[jk2] == 'd' && bbuf[jk2 + 1] == 'a' && bbuf[jk2 + 2] == 't' && bbuf[jk2 + 3] == 'a') { jk2 += 4; break; }
		}
		si = (int)(BYTE)bbuf[jk2] + (int)(BYTE)bbuf[jk2 + 1] * 256 + (int)(BYTE)bbuf[jk2 + 2] * 65536 + (int)(BYTE)bbuf[jk2 + 3] * 256 * 65536;
		si = si / 4 + jk / 4;
		data_size = oggsize = si * 4 - (si * 14 * 4) / 2048 - (si * 8 * 4) / 0x5000 - jk;
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		adpcmf.Seek(jk + jk2 + 4, CFile::begin);
		adbuf2 = (char*)calloc(si * 6 * 2, 1);
		if (readadpcmgurumin(adpcmf, adbuf2, (int)(adpcmf.GetLength() - jk))) { thend = 1; adpcmf.Close(); return; }
		adpcmf.Close();
	}
	else if (mode == 30) {
		CWaitCursor aaaa;
		CFile adpcmf;
		char aaa[9];
		struct a {
			int jump;
			int d4;
			int moji;
			int d1;
			int datasize;
			int d2;
			int seekpoint;
			int d3;
		};
		a aa;
		a aa1;
		a aa2;
		int st, cnt;
		int fii = filen.Find(L":", 6);
		CString fn = filen;
		if (fii != -1) {
			fn = filen.Left(fii);
		}
		// basename だけの旧プレイリスト向け。照合は 5e7628b の filen.Find(s21)>0 のまま
		fn = ResolveMode30PacPath(fn);
		adpcmf.Open(fn, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		if (adpcmf.m_hFile == CFile::hFileNull) {
			thend = 1;
			wavwait = 1;
			return;
		}

		char moj[300];
		adpcmf.Seek(16, CFile::begin);
		int k = 0, kk = 0;
		int ffff = 0;
		for (int i = 0;; i++) {
			ULONGLONG pos;
			adpcmf.Read(&aa, 32);
			pos = adpcmf.GetPosition();
			adpcmf.Seek(aa.moji, 0);
			adpcmf.Read(moj, 256);
			CStringA a = moj;
			if (ffff) break;
			CStringA s = bbuf;

			//10文字目から、ed6001.wav と入っているので、001だけ抜き出す
			// Mid(12,4)+'.'除去で 100 / 108b / 501e。Find(s21) が独自照合
			CStringA s1 = moj, s11 = moj;
			s1 = s1.Mid(12, 3);
			s11 = s11.Mid(12, 4); s11.Replace(".", "");
			CString s2 = CString(s1);
			CString s21 = CString(s11);
			if (filen.Find(s21) > 0) {
				ffff = 1;
				break;
			}
			k++;
			kk++;
			adpcmf.Seek(pos, 0);
		}

		CString sss, sss1;
		char* bbuf = (char*)malloc(0x1000); //多めに確保
		// RIFF dataのところを取りたいので多めの0x80分読む
		adpcmf.SeekToBegin();
		adpcmf.Seek(aa.seekpoint, 1);
		adpcmf.Read(bbuf, 0x80);
		for (st = 0;; st++)
			if (bbuf[st] == 'd' && bbuf[st + 1] == 'a' && bbuf[st + 2] == 't' && bbuf[st + 3] == 'a') { st += 4; break; }
		cnt = (int)(BYTE)bbuf[st] + (int)(BYTE)bbuf[st + 1] * 256 + (int)(BYTE)bbuf[st + 2] * 65536 + (int)(BYTE)bbuf[st + 3] * 256 * 65536;
		int st2 = st;
		// dataの次のsmplがほしいので、data分最後〜wavファイル最後までの間で検索(時短)
		adpcmf.SeekToBegin();
		adpcmf.Seek(aa.seekpoint, 1); // RIFF
		adpcmf.Seek(st2 + cnt + 4, 1); // dataの最後
		adpcmf.Read(bbuf, aa.datasize - cnt); //dataの最後〜wavの最後まで読む
		for (st = 0; st < aa.datasize - cnt - 4; st++) {
			if (bbuf[st] == 's' && bbuf[st + 1] == 'm' && bbuf[st + 2] == 'p' && bbuf[st + 3] == 'l')
			{
				st += 4; break;
			}
			if (bbuf[st] == 'R' && bbuf[st + 1] == 'I' && bbuf[st + 2] == 'F' && bbuf[st + 3] == 'F')
			{
				st = -1; break;
			}
		}
		if (st == aa.datasize - 4 || st == -1) {
			loop1 = 0;
			loop2 = cnt / 4;
		}
		else {
			loop1 = (int)(BYTE)bbuf[st + 0x30] + (int)(BYTE)bbuf[st + 0x31] * 256 + (int)(BYTE)bbuf[st + 0x32] * 65536 + (int)(BYTE)bbuf[st + 0x33] * 256 * 65536;
			int loop2_end = (int)(BYTE)bbuf[st + 0x34] + (int)(BYTE)bbuf[st + 0x35] * 256 + (int)(BYTE)bbuf[st + 0x36] * 65536 + (int)(BYTE)bbuf[st + 0x37] * 256 * 65536;
			loop2 = loop2_end - loop1;  // smplのstart/endはバッファのサンプル索引と同一。変換不要
		}

		data_size = oggsize = cnt;
		adpcmf.Seek(aa.seekpoint + st2 + 4, CFile::begin);
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		og->m_time.SetSelection(loop1, loop2 + loop1);
		lenl = 0;
		adbuf2 = (char*)calloc((size_t)cnt, 1);
		if (!adbuf2) {
			thend = 1;
			free(bbuf);
			adpcmf.Close();
			return;
		}
		wavbit_sample_Hz = 48000;
		free(bbuf);
		// wavwait は全読み込み後。play() が先に進むと adbuf2 読み書き競合で落ちる
		{
			int remain = cnt;
			int off = 0;
			while (remain > 0) {
				const UINT chunk = (UINT)min((DWORD)remain, dl * 2);
				const UINT n = adpcmf.Read(adbuf2 + off, chunk);
				if (n == 0)
					break;
				off += (int)n;
				remain -= (int)n;
				if (thend1 == TRUE) { thend = 1; wavwait = 1; adpcmf.Close(); return; }
				og->m_time.SetSelection(loop1, loop2 + loop1);
			}
		}
		adpcmf.Close();
		wavwait = 1;
	}
	else if (mode == 16) {
		CWaitCursor aaaa;
		CFile adpcmf;
		char aaa[9];
		struct a {
			char aa[8];
			int p1;
			int p2;
		};
		a aa;
		int st, cnt;
		adpcmf.Open(_T("bgm.arc"), CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		adpcmf.Read(bbuf, 5);
		if (bbuf[4] == 1) {//wav else adpcm
			CString sss, sss1;
			adpcmf.SeekToBegin();
			adpcmf.Read(bbuf, 0x14);
			for (;;) {
				adpcmf.Read(&aa, sizeof(a));	memcpy(aaa, aa.aa, 8); aaa[8] = 0; sss = aaa;	if (sss == filen.Left(8)) break;
			}
			adpcmf.SeekToBegin();
			adpcmf.Seek(aa.p2, CFile::begin);
			__int64 aa = adpcmf.GetPosition();
			adpcmf.Read(bbuf, 0x7f);
			for (st = 0;; st++)
				if (bbuf[st] == 'd' && bbuf[st + 1] == 'a' && bbuf[st + 2] == 't' && bbuf[st + 3] == 'a') { st += 4; break; }
			cnt = (int)(BYTE)bbuf[st] + (int)(BYTE)bbuf[st + 1] * 256 + (int)(BYTE)bbuf[st + 2] * 65536 + (int)(BYTE)bbuf[st + 3] * 256 * 65536;
			data_size = oggsize = cnt;
			adpcmf.Seek(aa + st + 4, CFile::begin);
			og->m_time.SetRange(0, (data_size) / 4, TRUE);
			lenl = 0;
			adbuf2 = (char*)calloc(cnt * 2 * 2, 1);
			//			memcpy(adbuf2,adbuf+st+4,cnt);
			adpcmf.Read(adbuf2, dl * 2);
			dwDataLen += dl;
			cnt -= dwDataLen * 2;
			wavwait = 1;
			for (; cnt > 0;) {
				adpcmf.Read(adbuf2 + (int)dwDataLen, dl);
				dwDataLen += dl; cnt -= dl;
				if (thend1 == TRUE) { thend = 1;  return; }
			}
			adpcmf.Close();
		}
		else {
			CString sss, sss1;
			adpcmf.SeekToBegin();
			adpcmf.Read(bbuf, 0x20);
			for (;;) {
				adpcmf.Read(&aa, sizeof(a));	memcpy(aaa, aa.aa, 8); aaa[8] = 0; sss = aaa;	if (sss == filen.Left(8)) break;
			}
			adpcmf.SeekToBegin();
			adpcmf.Seek(aa.p2, CFile::begin);
			data_size = oggsize = aa.p1 * 4 - (aa.p1 * 14 * 4) / 2048 - (aa.p1 * 8 * 4) / 0x5000 - 0x2c;
			og->m_time.SetRange(0, (data_size) / 4, TRUE);
			lenl = 0;
			adbuf2 = (char*)calloc(aa.p1 * 6 * 2, 1);
			if (readadpcmzwei(adpcmf, adbuf2, aa.p1)) { thend = 1; adpcmf.Close();  return; }
			adpcmf.Close();
		}
	}
	else if (mode == 19) {
		CWaitCursor aaaa;
		CFile adpcmf;
		if (adpcmf.Open(filen.Left(filen.ReverseFind('.')) + _T(".pos"), CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL) == 0) {
			loop1 = loop2 = 0;
		}
		else {
			adpcmf.Read(&loop1, 4);
			adpcmf.Read(&loop2, 4); loop2 = loop2 - loop1;
			adpcmf.Close();
		}
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		adpcmf.Read(bbuf, 0x80);
		wavbit_sample_Hz = (UINT)(int)bbuf[0x18] + (int)(BYTE)bbuf[0x19] * 256;
		if (wav)free(wav);
		wav_start();
		int si, jk;
		for (jk = 0; jk < 0x7c; jk++) {
			if (bbuf[jk] == 'd' && bbuf[jk + 1] == 'a' && bbuf[jk + 2] == 't' && bbuf[jk + 3] == 'a') { jk += 4; break; }
		}
		si = (int)(BYTE)bbuf[jk] + (int)(BYTE)bbuf[jk + 1] * 256 + (int)(BYTE)bbuf[jk + 2] * 65536 + (int)(BYTE)bbuf[jk + 3] * 256 * 65536;
		adpcmf.SeekToBegin();
		adpcmf.Seek(jk + 4, CFile::begin);
		adbuf2 = (char*)calloc(si * 2 * 2, 1);
		data_size = oggsize = si;
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		lenl = 0;
		dwDataLen += dl;
		adpcmf.Read(adbuf2, dwDataLen);
		wavwait = 1;
		si -= dwDataLen;
		for (; si > 0;) {
			adpcmf.Read(adbuf2 + (int)dwDataLen, dl);
			dwDataLen += dl; si -= dl;
			if (thend1 == TRUE) { thend = 1; adpcmf.Close();  return; }
		}
		adpcmf.Close();
	}
	else if (mode == 18 || mode == 20) {
		CWaitCursor aaaa;
		BOOL ff = FALSE;
		CFile adpcmf;
		if (adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL) == 0) {
			if (filen.Left(8) == "ED3119DA") { _chdir(".."); filen = "ED3_DT09.DAT"; ff = TRUE; }
			if (filen.Left(8) == "ED3603DA") { _chdir(".."); filen = "ED3_DT10.DAT"; ff = TRUE; }
			adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		}

		adpcmf.Seek(-32, CFile::end);
		adpcmf.Read(&loop1, 4);
		adpcmf.Read(&loop2, 4); loop2 = loop2 - loop1;
		BYTE a[2], b[5]; b[4] = 0; CString tep;
		adpcmf.Seek(-68, CFile::end);
		adpcmf.Read(a, 2);
		if (!(a[0] == 0x93 && a[1] == 0x58)) loop1 = loop2 = 0; if (loop2 == 0) loop1 = 0;
		adpcmf.Seek(-16, CFile::end);
		adpcmf.Read(b, 4); tep = b;
		adpcmf.SeekToBegin();
		adpcmf.Read(bbuf, 0x80);
		wavbit_sample_Hz = (UINT)(int)bbuf[0x18] + (int)(BYTE)bbuf[0x19] * 256;
		if (wav)free(wav);
		wav_start();
		int si, jk;
		if (ff == FALSE)
			for (jk = 0; jk < 0x7c; jk++) {
				if (bbuf[jk] == 'd' && bbuf[jk + 1] == 'a' && bbuf[jk + 2] == 't' && bbuf[jk + 3] == 'a') { jk += 4; break; }
			}
		else
			jk = 0x28;
		si = (int)(BYTE)bbuf[jk] + (int)(BYTE)bbuf[jk + 1] * 256 + (int)(BYTE)bbuf[jk + 2] * 65536 + (int)(BYTE)bbuf[jk + 3] * 256 * 65536;
		if (filen.Left(8) == "ED5WV001")loop2 = si / 4;
		if (loop1 < 0 || loop2 < 0) { loop1 = 0; loop2 = si / 4; }
		if (filen.Left(8) == "ED3940DA" && fnn.Left(2) == "白") {
			si = 14332500 * 2 * 2;
			if (wavbit_sample_Hz == 22050) { si /= 2; }
		}
		if (filen.Left(8) == "ED3940DA" && fnn.Left(2) == "も") {
			jk = 14376600 * 2 * 2;
			si = 19668600 * 2 * 2;
			if (wavbit_sample_Hz == 22050) { jk /= 2; si /= 2; }
		}
		adpcmf.SeekToBegin();
		adpcmf.Seek(jk + 4, CFile::begin);
		adbuf2 = (char*)calloc((si - jk) * 2 * 2, 1);
		data_size = oggsize = si - jk;
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		lenl = 0;
		dwDataLen += dl;
		adpcmf.Read(adbuf2, dwDataLen);
		wavwait = 1;
		si -= dwDataLen;
		for (; (si - jk) > 0;) {
			adpcmf.Read(adbuf2 + (int)dwDataLen, dl);
			dwDataLen += dl; si -= dl;
			if (thend1 == TRUE) { thend = 1; adpcmf.Close(); return; }
		}
		adpcmf.Close();
	}
	else if (mode == 17 || mode == -12 || (mode == -14 && filen.Right(3) == "wav")) {
		CWaitCursor aaaa;
		CFile adpcmf;
		if (filen == "49music.wav" || filen == "50music.wav" || filen == "51music.wav") _chdir("..\\Cmusic");
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		adpcmf.Read(bbuf, 0x80);
		int si, jk;
		wavbit_sample_Hz = (UINT)(int)bbuf[0x18] + (int)(BYTE)bbuf[0x19] * 256;
		if (wav)free(wav);
		wav_start();
		for (jk = 0; jk < 0x7c; jk++) {
			if (bbuf[jk] == 'd' && bbuf[jk + 1] == 'a' && bbuf[jk + 2] == 't' && bbuf[jk + 3] == 'a') { jk += 4; break; }
		}
		si = (int)(BYTE)bbuf[jk] + (int)(BYTE)bbuf[jk + 1] * 256 + (int)(BYTE)bbuf[jk + 2] * 65536 + (int)(BYTE)bbuf[jk + 3] * 256 * 65536;
		adpcmf.SeekToBegin();
		adpcmf.Read(bbuf, jk + 4);
		adbuf2 = (char*)calloc(si * 2 * 2, 1);
		data_size = oggsize = si;
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		lenl = 0;
		dwDataLen += dl;
		adpcmf.Read(adbuf2, dwDataLen);
		loop1 = 0;
		if (filen == "VT01DA.wav" || filen == "VT02DA.wav" || filen == "VT21DA.wav" ||
			filen == "VT22DA.wav" || filen == "VT31DA.wav" || filen == "VT35DA.wav" ||
			filen == "VT39DA.wav" || filen == "VT40DA.wav" || filen == "VT41DA.wav" ||
			filen == "VT43DA.wav" || filen == "VT44DA.wav" || filen == "VT45DA.wav" ||
			filen == "VT46DA.wav" || filen == "VT47DA.wav" || filen == "VT48DA.wav" ||
			filen == "VT39DA.wav" || filen == "42music.wav") {
			loop2 = 0;
		}
		else {
			loop2 = oggsize / 4;
		}
		wavwait = 1;
		si -= dwDataLen;
		for (; si > 0;) {
			adpcmf.Read(adbuf2 + (int)dwDataLen, dl);
			dwDataLen += dl; si -= dl;
			if (thend1 == TRUE) { thend = 1; adpcmf.Close();  return; }
		}
		adpcmf.Close();
	}
	else if (mode == 11 || mode == 12 || mode == 13) {
		CWaitCursor aaaa;
		CFile adpcmf;
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		adpcmf.Read(bbuf, 0x80);
		int si, jk;
		for (jk = 0; jk < 0x7c; jk++) {
			if (bbuf[jk] == 'd' && bbuf[jk + 1] == 'a' && bbuf[jk + 2] == 't' && bbuf[jk + 3] == 'a') { jk += 4; break; }
		}
		si = (int)(BYTE)bbuf[jk] + (int)(BYTE)bbuf[jk + 1] * 256 + (int)(BYTE)bbuf[jk + 2] * 65536 + (int)(BYTE)bbuf[jk + 3] * 256 * 65536;
		adpcmf.SeekToBegin();
		adpcmf.Read(bbuf, jk + 4);
		adbuf2 = (char*)calloc(si * 2 * 2 + 44100 * 30, 1);
		data_size = oggsize = si;
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		lenl = 0;
		dwDataLen += dl;
		adpcmf.Read(adbuf2, dwDataLen);
		wavwait = 1;
		si -= dwDataLen;
		for (; si > 0;) {
			adpcmf.Read(adbuf2 + (int)dwDataLen, dl);
			dwDataLen += dl; si -= dl;
			if (thend1 == TRUE) { thend = 1; adpcmf.Close();  return; }
		}
		adpcmf.Close();
	}
	else if (mode == 14) {
		CWaitCursor aaaa;
		CFile adpcmf;
		struct a {
			char aa[8];
			int p1;
			int p2;
		};
		a aa;
		int st, cnt;
		adpcmf.Open(_T("wav.dat"), CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		adpcmf.Read(bbuf, 5);
		if (bbuf[4] == 2) {//wav else adpcm
			adpcmf.SeekToBegin();
			adpcmf.Read(bbuf, 0x20);
			CString sss, sss1;
			for (;;) {
				adpcmf.Read(&aa, sizeof(a));	sss = aa.aa;	if (sss == "bgm") break;
			}
			adpcmf.SeekToBegin();
			adpcmf.Seek(aa.p2, CFile::begin);
			adbuf = (char*)malloc(aa.p1 + 1);
			adpcmf.Read(adbuf, aa.p1);
			adbuf[aa.p1] = 0;
			sss = adbuf;
			loop1 = loop2 = 0;
			if (filen.Mid(4, 1) == "(")
				cnt = sss.Find(filen.Left(4));
			else
				cnt = sss.Find(filen.Left(5));
			st = sss.Find(',', cnt);//bgm00.wav,st,end;
			cnt = sss.Find(',', st + 1);		sss1 = sss.Mid(st + 1, (cnt - 1) - (st));		loop1 = _tstoi(sss1);
			st = sss.Find(';', cnt + 1);		sss1 = sss.Mid(cnt + 1, (st - 1) - (cnt));		loop2 = _tstoi(sss1) - loop1;
			if (loop1 == 1)loop1 = loop2 = 0;
			free(adbuf);
			adbuf = NULL;
			if (filen.Left(5) == "bgm75" || filen.Left(5) == "bgm76") {
				filen = filen.Left(5) + _T("(data.dat)");
				adpcmf.Close();
				adpcmf.Open(_T("Plugins\\data.dat"), CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
				adpcmf.SeekToBegin();
				adpcmf.Read(bbuf, 0x44);
				for (;;) {
					adpcmf.Read(&aa, sizeof(a));	sss = aa.aa;	if (sss == filen.Left(5)) break; if (sss.GetLength() == 4 && sss == filen.Left(4)) break;
				}
				adpcmf.SeekToBegin();
				adpcmf.Seek(aa.p2, CFile::begin);
				adbuf2 = (char*)calloc(aa.p1 * 6 * 2, 1);
				data_size = oggsize = aa.p1 * 4 - (aa.p1 * 14 * 4) / 2048 - (aa.p1 * 8 * 4) / 0x5000 - 0x2c;
				og->m_time.SetRange(0, (data_size) / 4, TRUE);
				lenl = 0;
				if (readadpcmzwei(adpcmf, adbuf2, aa.p1)) { thend = 1; adpcmf.Close(); return; }
				adpcmf.Close();
			}
			else {
				adpcmf.SeekToBegin();
				adpcmf.Read(bbuf, 0x20);
				for (;;) {
					adpcmf.Read(&aa, sizeof(a));	sss = aa.aa;	if (sss == filen.Left(5)) break; if (sss.GetLength() == 4 && sss == filen.Left(4)) break;
				}
				adpcmf.SeekToBegin();
				adpcmf.Seek(aa.p2, CFile::begin);
				adpcmf.Read(bbuf, 0x81);
				for (st = 0;; st++)
					if (bbuf[st] == 'd' && bbuf[st + 1] == 'a' && bbuf[st + 2] == 't' && bbuf[st + 3] == 'a') { st += 4; break; }
				cnt = (int)(BYTE)bbuf[st] + (int)(BYTE)bbuf[st + 1] * 256 + (int)(BYTE)bbuf[st + 2] * 65536 + (int)(BYTE)bbuf[st + 3] * 256 * 65536;
				adbuf2 = (char*)calloc(cnt * 2 * 2, 1);
				adpcmf.Seek(aa.p2 + st + 4, CFile::begin);
				dwDataLen += dl;
				adpcmf.Read(adbuf2, dwDataLen);
				data_size = oggsize = cnt;
				og->m_time.SetRange(0, (data_size) / 4, TRUE);
				lenl = 0;
				wavwait = 1;
				cnt -= dwDataLen;
				for (; cnt > 0;) {
					adpcmf.Read(adbuf2 + (int)dwDataLen, dl);
					dwDataLen += dl; cnt -= dl;
					if (thend1 == TRUE) { thend = 1; adpcmf.Close();  return; }
				}
				adpcmf.Close();
			}
		}
		else {//adpcm
			adpcmf.SeekToBegin();
			adpcmf.Read(bbuf, 0x2c);
			CString sss, sss1;
			for (;;) {
				adpcmf.Read(&aa, sizeof(a));	sss = aa.aa;	if (sss == "bgm") break;
			}
			adpcmf.SeekToBegin();
			adpcmf.Seek(aa.p2, CFile::begin);
			adbuf = (char*)malloc(aa.p1 + 1);
			adpcmf.Read(adbuf, aa.p1);
			adbuf[aa.p1] = 0;
			sss = adbuf;
			loop1 = loop2 = 0;
			if (filen.Mid(4, 1) == "(")
				cnt = sss.Find(filen.Left(4));
			else
				cnt = sss.Find(filen.Left(5));
			st = sss.Find(',', cnt);//bgm00.wav,st,end;
			cnt = sss.Find(',', st + 1);		sss1 = sss.Mid(st + 1, (cnt - 1) - (st));		loop1 = _tstoi(sss1);
			st = sss.Find(';', cnt + 1);		sss1 = sss.Mid(cnt + 1, (st - 1) - (cnt));		loop2 = _tstoi(sss1) - loop1;
			if (loop1 == 1)loop1 = loop2 = 0;
			delete[] adbuf;
			adbuf = NULL;
			if (filen.Left(5) == "bgm75" || filen.Left(5) == "bgm76") {
				filen = filen.Left(5) + _T("(data.dat)");
				adpcmf.Close();
				adpcmf.Open(_T("Plugins\\data.dat"), CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
				adpcmf.SeekToBegin();
				adpcmf.Read(bbuf, 0x44);
				for (;;) {
					adpcmf.Read(&aa, sizeof(a));	sss = aa.aa;	if (sss == filen.Left(5)) break; if (sss.GetLength() == 4 && sss == filen.Left(4)) break;
				}
				adpcmf.SeekToBegin();
				adpcmf.Seek(aa.p2, CFile::begin);
				adbuf2 = (char*)calloc(aa.p1 * 6 * 2, 1);
				data_size = oggsize = aa.p1 * 4 - (aa.p1 * 14 * 4) / 2048 - (aa.p1 * 8 * 4) / 0x5000 - 0x2c;
				og->m_time.SetRange(0, (data_size) / 4, TRUE);
				lenl = 0;
				if (readadpcmzwei(adpcmf, adbuf2, aa.p1)) { thend = 1; adpcmf.Close(); return; }
				adpcmf.Close();
			}
			else {
				adpcmf.SeekToBegin();
				adpcmf.Read(bbuf, 0x2c);
				for (;;) {
					adpcmf.Read(&aa, sizeof(a));	sss = aa.aa;	if (sss == filen.Left(5)) break; if (sss.GetLength() == 4 && sss == filen.Left(4)) break;
				}
				adpcmf.SeekToBegin();
				adpcmf.Seek(aa.p2, CFile::begin);
				adbuf2 = (char*)calloc(aa.p1 * 6, 1);
				data_size = oggsize = aa.p1 * 4 - (aa.p1 * 14 * 4) / 2048 - (aa.p1 * 8 * 4) / 0x5000 - 0x2c;
				og->m_time.SetRange(0, (data_size) / 4, TRUE);
				lenl = 0;
				if (readadpcmzwei(adpcmf, adbuf2, aa.p1)) { thend = 1; adpcmf.Close(); return; }
				adpcmf.Close();
			}
		}
		/*	}else if(mode==-100){//なし
		si1.dwSamplesPerSec=44100;
		si1.dwChannels=2;
		si1.dwBitsPerSample=16;
		CString s,ss;
		s=filen.Left(filen.ReverseFind('\\')); ss=filen.Right(filen.GetLength()-filen.ReverseFind('\\')-1);
		_tchdir(s);
		CMp3Info *si2; si2=new CMp3Info;
		si2->Load(ss,TRUE);
		kbps=si2->GetBps();
		Vbr=si2->IsVbr();
		delete si2;
		CId3tagv1 ta1;
		CId3tagv2 ta2;
		int b=ta2.Load(ss);
		tagname=ta2.GetArtist();if(b==-1){ta1.Load(ss); tagname=ta1.GetArtist();}
		tagfile=ta2.GetTitle();if(b==-1) tagfile=ta1.GetTitle();
		tagalbum=ta2.GetAlbum();if(b==-1) tagalbum=ta1.GetAlbum();
		if(Open(ss,&si1)==true){
		loop1=0;stitle="";
		loop2=(int)(((float)si1.dwLength)/1000.0f*44100.0f);
		wavchannel=si1.dwChannels;
		wavbit_sample_Hz=si1.dwSamplesPerSec;
		loop2=(int)(((float)loop2)/(44100.0f/((float)((wavchannel==2)?wavbit_sample_Hz:(wavbit_sample_Hz/2)))));
		data_size=oggsize=loop2*4;
		loop3=loop2;loop2=0;
		adbuf2=(char*)malloc(data_size+44100*10);
		if(adbuf2==0){wavwait=1;thend=1; fnn=LL14(L"メモリの確保に失敗しました。", L"Memory allocation failed.", L"Échec de l'allocation mémoire.", L"Allocazione memoria non riuscita.", L"Error al asignar memoria.", L"메모리 할당에 실패했습니다.", L"内存分配失败。", L"فشل تخصيص الذاكرة.", L"Не удалось выделить память.", L"Speicherzuweisung fehlgeschlagen.", L"Falha na alocação de memória.", L"Geheugentoewijzing mislukt.", L"Alokacja pamięci nie powiodła się.", L"Bellek ayırma başarısız.");return;}
		og->m_time.SetRange(0,(data_size)/4,TRUE);
		lenl= 0;
		if(wav)free(wav);
		wav_start();
		Render();
		}else{wavwait=1;thend=1; fnn=LL14(L"ファイルが開けませんでした。", L"Could not open file.", L"Impossible d'ouvrir le fichier.", L"Impossibile aprire il file.", L"No se pudo abrir el archivo.", L"파일을 열 수 없습니다.", L"无法打开文件。", L"تعذر فتح الملف.", L"Не удалось открыть файл.", L"Datei konnte nicht geöffnet werden.", L"Não foi possível abrir o ficheiro.", L"Kon bestand niet openen.", L"Nie można otworzyć pliku.", L"Dosya açılamadı.");return;}
		*/
	}
	else if (mode == -11 || (mode == -14 && filen.Right(3) == "mp3") || mode == -15) {
		if (mode == -14 && (filen == "49music.mp3" || filen == "50music.mp3" || filen == "51music.mp3")) _chdir("..\\Cmusic");
		int san2 = 0;
		if (filen == "041music.mp3" && fnn.Find(LL14(
			_T("日本語"),
			_T("Japanese"),
			_T("Japonais"),
			_T("Giapponese"),
			_T("Japones"),
			_T("???"),
			_T("日語"),
			_T("?????????"),
			_T("Японская"),
			_T("Japan."),
			_T("Japones"),
			_T("Japanse"),
			_T("Jap."),
			_T("Japonca"))) > 0) san2 = 1;
		if (filen == "041music.mp3" && fnn.Find(LL14(
			_T("中国"),
			_T("Chinese"),
			_T("Chinois"),
			_T("Cinese"),
			_T("Chino"),
			_T("???"),
			_T("中文"),
			_T("???????"),
			_T("Китайская"),
			_T("Chin."),
			_T("Chines"),
			_T("Chinese"),
			_T("Chi?."),
			_T("Cince"))) > 0) san2 = 2;
		SOUNDINFO si;
		si.dwSamplesPerSec = 44100;
		si.dwChannels = 2;
		si.dwBitsPerSample = 16;
		CString s, ss;
		s = filen.Left(filen.ReverseFind('\\')); ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		_tchdir(s);
		Open(ss, &si);
		loop1 = 0; stitle = "";
		loop2 = (int)(((float)si.dwLength) / 1000.0f * 44100.0f);
		wavbit_sample_Hz = si.dwSamplesPerSec;
		loop2 = loop2 / (44100 / wavbit_sample_Hz);
		data_size = oggsize = loop2 * 4;
		adbuf2 = (char*)calloc(data_size * 2 + 44100 * 30, 1);
		if (san2 == 1) loop2 = 225 * 44100;
		if (san2 == 2) loop2 = 247 * 44100;
		loop3 = loop2;
		if (san2)data_size = oggsize = loop2 * 4 + 44100 * 7;
		if (mode == -14 && filen == "42music.mp3")	loop2 = 0;
		if (adbuf2 == 0) {
			wavwait = 1; thend = 1; fnn = LL14(
				L"メモリの確保に失敗しました。",         /* 日本語 */
				L"Memory allocation failed.",           /* 英語 */
				L"Échec de l'allocation mémoire.",      /* フランス語 */
				L"Allocazione della memoria fallita.",  /* イタリア語 */
				L"Error al asignar memoria.",           /* スペイン語 */
				L"메모리 할당에 실패했습니다.",           /* 韓国語 */
				L"内存分配失败。",                       /* 中国語 */
				L"فشل تخصيص الذاكرة.",                  /* アラビア語 */
				L"Ошибка выделения памяти.",            /* ロシア語 */
				L"Speicherzuweisung fehlgeschlagen.",   /* ドイツ語 */
				L"Falha na alocação de memória.",       /* ポルトガル語 */
				L"Geheugentoewijzing mislukt.",         /* オランダ語 */
				L"Błąd alokacji pamięci.",              /* ポーランド語 */
				L"Bellek tahsisi başarısız.");          /* トルコ語 */
			return;
		}
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		lenl = 0;
		if (wav)free(wav);
		wav_start();
		Render((san2 == 2) ? data_size - 44100 * 21 * 4 : 0);
	}
	else if (mode == -13) {
		lenl = 0;
		CFile adpcmf;
		adpcmf.Open(_T("music.pak"), CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
		adpcmf.SeekToEnd();
		adpcmf.Seek(-0x3d - 12, CFile::end);
		//		adpcmf.Open("C:\\FALCOM\\Arcturus\\music.pak",CFile::modeRead|CFile::typeBinary | CFile::shareDenyWrite,NULL);
		char file[19];
		char n[2];
		int st;
		int size;
		int ex;
		for (;;) {
			adpcmf.Read(&st, 4);
			adpcmf.Read(&size, 4);
			adpcmf.Read(&ex, 4);
			adpcmf.Read(file, 19);
			adpcmf.Read(n, 2);
			adpcmf.Seek(-(19 + 2 + 4 + 4 + 4) * 2, CFile::current);
			CString s;
			s = file; if (s.Find(filen.Left(6)) > 0) break;
		}

		adpcmf.Seek(st + 5, CFile::begin);
		adpcmf.Read(&wavbit_sample_Hz, 2);
		data_size = oggsize = (ex) * 4;
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		adbuf = (char*)malloc(size * 10);
		adbuf2 = (char*)calloc(size * 6 * 2, 1);
		ZeroMemory(adbuf2, size * 6);
		ZeroMemory(adbuf, size * 6);
		loop1 = 0;
		loop2 = ex;
		adpcmf.Seek(st, CFile::begin);
		if (readadpcmarc(adpcmf, adbuf, (int)(size))) { thend = 1; free(adbuf); adpcmf.Close(); return; }
		free(adbuf);
		adpcmf.Close();
	}
	thend = 1;
	return;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////ADPCM DATA+READ///////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

int lbuf = 0;
static int MS_Delta[] =
{
	230, 230, 230, 230, 307, 409, 512, 614,
	768, 614, 512, 409, 307, 230, 230, 230
};

static ADPCMCOEFSET MSADPCM_CoeffSet[] =
{
	{ 256, 0 },{ 512, -256 },{ 0, 0 },{ 192, 64 },{ 240, 0 },{ 460, -208 },{ 392, -232 }
};

int                 ideltaL, ideltaR;
int                 sample1L, sample2L;
int                 sample1R, sample2R;
ADPCMCOEFSET        coeffL, coeffR;
int                 nsamp;

int readBuffwav(char* bw, int cnt);
int seekadpcm(int pos);

BOOL playwavBuffwav(BYTE* bw, int old, int l1, int l2)
{
	//	playb+=(l1+l2)/4;
	int rrr = readBuffwav((char*)bw + old, l1);
	if (l1 != rrr) {
		if (endf == 1) {
			l1 = rrr;
			if (savedata.saverenzoku == 0)
				fade1 = 1;
			else
				endflg = 1;
		}
		else {
			loopcnt++;
			playb = loop1;
			poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = loop1 * PcmOutBytesPerFrame(); poss6 = 0;
			seekadpcm(loop1);
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			reset = TRUE;
			readBuffwav((char*)bw + old + rrr, (int)l1 - rrr);
		}
	}
	if (l2) {
		rrr = readBuffwav((char*)bw, l2);
		if (l2 != rrr) {
			if (endf == 1) {
				l2 = rrr;
				if (savedata.saverenzoku == 0)
					fade1 = 1;
				else
					endflg = 1;
			}
			else {
				loopcnt++;
				playb = loop1;
				poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = loop1 * PcmOutBytesPerFrame(); poss6 = 0;
				seekadpcm(loop1);
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				reset = TRUE;
				readBuffwav((char*)bw + rrr, (int)l2 - rrr);
			}
		}
	}
	return FALSE;
}

std::vector<uint8_t> outputRawBytesData;
int readtempo(BYTE* data, int len,bool t = false)
{
	m_bufwav3_1.clear();
	if (len > 0) {
		m_bufwav3_1.resize(len);
		memcpy(m_bufwav3_1.data(), (data), len);
	}
	float te = (float)tempo;
	if (te >= 200.0f) {
		te -= 100.0f;
	}
	else {
		te = te / 3.0f + 33.3f;
	}
	te = 100.0f / te;
	// len<=0 のときは常に final 相当で吐き出す（fade1 中も同様）。
	// fade1 中だけ flush しないと RB 内部にサンプルが残り、無音フェードとぶつかって「ボコ」と消える。
	// g_rubberBandFinalFlushed で2回目以降は空返しなので無限ループにならない。
	bool doFinalFlush = t || (len <= 0);
	if (!ProcessAudioWithRubberBand(te, doFinalFlush)) {
		// 空入力で process されなかったとき、前回の m_converted を再エンコードすると
		// 同じフレームがもう一度出力され末尾が「たぶる」（二重になる）
		m_convertedPcmFloatData.clear();
		outputRawBytesData.clear();
		return 0;
	}
//	ProcessAudioWithSoundTouch(te,t);
	int bits;
	// 再生スレッドから呼ばれる。mode は次曲に差し替わっていることがある
	if (ActiveDecodeMode() == -10) {
		bits = Mp3DecoderBitsClampedFromObject();
	}
	else {
		bits = (wavsam_depth <= 0 || wavsam_depth > 32) ? 16 : abs(wavsam_depth);
		if (!(bits == 8 || bits == 16 || bits == 24 || bits == 32)) bits = 16;
	}
	uint16_t outBps = (uint16_t)bits;
	ConvertFloatToRawBytes(m_convertedPcmFloatData, outBps, wavchannel, outputRawBytesData);
	return outputRawBytesData.size();
}
using namespace std;
void equaliser(void* data, int len, BOOL reset = FALSE);
void EqualiserSetFormatVolContext(int mode, BOOL spcApplicable);
int readBuffwav(char* bw, int cnt)
{
	EqualiserSetFormatVolContext(0, FALSE);
	int r = cnt, rr = cnt;
	int cnt2;
	if (rr == 0)return 0;

	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	if (poss4 <= cnt) {
		int buffRbStallIters = 0;
		const int kBuffRbStallMax = 512;
		while (true) {
			if (IsPlaybackStopRequested())
				return 0;
			int f = 0;
			if (adbuf2 == NULL) return 0;
			// fade は ApplyFadeCubed で音量(例: 1.0=フル)。ここでは「無音に近い」ときだけゼロ埋め+muon。
			// fade==0 判定だと play() 開始時の fade=1.0 で常に else になり、無音のまま lenl だけ進んで即終了する。
			if (fade > 0.0001f)
				memcpy((void*)bufkpi, (void*)(adbuf2 + lenl), cnt);
			else
			{
				ZeroMemory(bufkpi, cnt);
				muon--;
			}

			if (playb > (data_size + 100000) / PcmOutBytesPerFrame() && muon != 0) {
				rrr = 0;
			}
			if (muon <= 0) {
				endf = 1;
				return 0;
			}

			int len2 = readtempo(bufkpi, cnt);
			lenl += cnt;

			if (len2 > 0) {
				buffRbStallIters = 0;
				// 書き込み
				RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
				poss4 += len2;
			}
			// playb はリングから bw へ渡したバイト数で進める（len2 積み上げは cnt を超え表示が実長より長くなる）
			if (poss4 > cnt) break;
			if (len2 <= 0 && fade1 == 1) break;
			if (len2 <= 0) {
				if (++buffRbStallIters >= kBuffRbStallMax)
					break;
			}
		}
	}

	int cnt0 = cnt;
	{
		const int bpfLoop = PcmOutBytesPerFrame();
		// play() 側と同じ: loop1==0 && loop2==0 は「ループ情報なし」で全長再生(data_size バイトまで)
		int loopEndBytes;
		if (loop1 == 0 && loop2 == 0) {
			loopEndBytes = (bpfLoop > 0) ? (int)data_size : 0;
		}
		else {
			loopEndBytes = (loop1 + loop2) * bpfLoop;
		}
		if (loopEndBytes > 0 && loopEndBytes < poss5 + cnt0 && endf == 0) {
			cnt0 = loopEndBytes - poss5;
			if (cnt0 < 0)
				cnt0 = 0;
		}
	}
	cnt2 = cnt0;

	if (cnt2 > 0) {
		RingBufRead(bw, bufkpi3, max_buffer_size, poss3, cnt0);
		poss4 -= cnt0;
		{
			const int bpf = PcmOutBytesPerFrame();
			if (bpf > 0 && cnt0 > 0)
				playb += cnt0 / bpf;
		}
	}

	equaliser(bw, cnt2, reset);
	og->FeedPianoRoll(bw, cnt2);
	reset = FALSE;

	fade += fadeadd;
	if (fade < 0.0001f) { fade = 0.0f; fadeadd = 0.0f; }
	ApplyFadeCubedToInterleavedPcm(bw, cnt2);

	if (cc1 == 1)	cc.Write(bw, cnt0);
	wl += cnt0;

	{
		float te = (float)tempo;
		if (te >= 200.0f) {
			te -= 100.0f;
		}
		else {
			te = te / 3.0f + 33.3f;
		}
		poss5 += (int)(cnt0 * (te / 100.0f));
	}

	return cnt0;
}

int seekadpcm(int pos)
{
	ResetAudioUpscalerPipeline();
	const int bpf = PcmOutBytesPerFrame();
	lenl = pos * bpf;
	playb = pos;
	poss5 = lenl;
	poss = 0;
	if (g_rubberBandStretcher) {
		delete g_rubberBandStretcher;
		g_rubberBandStretcher = NULL;
	}
	return 0;
}

static inline short  R16(const unsigned char* src)
{
	return (short)((unsigned short)src[0] | ((unsigned short)src[1] << 8));
}

static inline void  W16(unsigned char* dst, short s)
{
	dst[0] = LOBYTE(s);
	dst[1] = HIBYTE(s);
}

static inline void clamp_sample(int* sample)
{
	if (*sample < -32768) *sample = -32768;
	if (*sample > 32767) *sample = 32767;
}

static inline void process_nibble(unsigned nibble, int* idelta,
	int* sample1, int* sample2,
	const ADPCMCOEFSET* coeff)
{
	int sample;
	int snibble;

	/* nibble is in fact a signed 4 bit integer => propagate sign if needed */
	snibble = (nibble & 0x08) ? (nibble - 16) : nibble;
	sample = ((*sample1 * coeff->iCoef1) + (*sample2 * coeff->iCoef2)) / 256 +
		snibble * *idelta;
	clamp_sample(&sample);

	*sample2 = *sample1;
	*sample1 = sample;
	*idelta = ((MS_Delta[nibble] * *idelta) / 256);
	if (*idelta < 16) *idelta = 16;
}

unsigned char* bbuf1;
unsigned char* bw2;

int readadpcm(CFile& adpcmf, char* bw, int len)
{
	int c = 0;
	adpcmf.SeekToBegin();
	adpcmf.Read(bbuf, 0x2c);
	int cnt = 2048;
	int ak = 0, lbuf = 0;
	bw2 = (unsigned char*)bw;
	for (;;) {
		if (IsPlaybackStopRequested())
			return 0;
		if (lbuf == 0) {
			adpcmf.Read(abuf, 0x8);
			lbuf = 0x5000;
		}

		if (ak == 0) {
			cnt = adpcmf.Read(bbuf, 2048);
			if (cnt == 0) {
				wavwait = 1;
				return 0;
			}
			bbuf1 = (unsigned char*)bbuf;
			coeffL = MSADPCM_CoeffSet[*bbuf1++];
			coeffR = MSADPCM_CoeffSet[*bbuf1++];
			ideltaL = R16(bbuf1);    bbuf1 += 2;
			ideltaR = R16(bbuf1);    bbuf1 += 2;
			sample1L = R16(bbuf1);    bbuf1 += 2;
			sample1R = R16(bbuf1);    bbuf1 += 2;
			sample2L = R16(bbuf1);    bbuf1 += 2;
			sample2R = R16(bbuf1);    bbuf1 += 2;
			W16(bw2, sample2L);      bw2 += 2;
			W16(bw2, sample2R);      bw2 += 2;
			W16(bw2, sample1L);      bw2 += 2;
			W16(bw2, sample1R);      bw2 += 2;
			lbuf -= 14;
		}
		for (int k = ak; k < (cnt - 14); k++) {
			process_nibble(*bbuf1 >> 4, &ideltaL, &sample1L, &sample2L, &coeffL);
			W16(bw2, sample1L);	bw2 += 2;
			process_nibble(*bbuf1++ & 0x0F, &ideltaR, &sample1R, &sample2R, &coeffR);
			W16(bw2, sample1R);	bw2 += 2;	lbuf--;
			c += 4; if (c >= (int)(WAVDALen / OUTPUT_BUFFER_NUM) * 8)	wavwait = 1;
			if (thend1 == TRUE) return 1;
		}
		ak = 0;
	}
}

static BOOL decode_msadpcm_wav(CFile& f, const wavinfo& wi, char** outBuf, int* outSize)
{
	int nCh = (int)wi.nChannels;
	int blockAlign = wi.nBlockAlign > 0 ? (int)wi.nBlockAlign : 256;
	int samplesPerBlock = wi.nSamplesPerBlock > 0 ? (int)wi.nSamplesPerBlock : 256;
	__int64 dataSize = wi.dataSize;
	int preambleSize = 7 * nCh;
	if (blockAlign <= preambleSize) return FALSE;
	int blocks = (int)(dataSize / blockAlign);
	int totalSamples = blocks * samplesPerBlock;
	int outBytes = totalSamples * 2 * nCh;
	char* buf = (char*)calloc((size_t)outBytes + 4096, 1);
	if (!buf) return FALSE;
	unsigned char* dst = (unsigned char*)buf;
	BYTE blockBuf[512];
	f.Seek((LONG)wi.dataOffset, CFile::begin);
	for (int blk = 0; blk < blocks && !thend1; blk++) {
		if (f.Read(blockBuf, blockAlign) != (UINT)blockAlign) break;
		unsigned char* p = blockBuf;
		int ideltaL, ideltaR, s1L, s2L, s1R, s2R;
		ADPCMCOEFSET coeffL, coeffR;
		coeffL = MSADPCM_CoeffSet[p[0]]; coeffR = (nCh >= 2) ? MSADPCM_CoeffSet[p[1]] : coeffL;
		p += nCh;
		ideltaL = (short)(p[0] | (p[1] << 8)); p += 2;
		ideltaR = (nCh >= 2) ? (short)(p[0] | (p[1] << 8)) : ideltaL; p += (nCh >= 2) ? 2 : 0;
		s1L = (short)(p[0] | (p[1] << 8)); p += 2;
		s1R = (nCh >= 2) ? (short)(p[0] | (p[1] << 8)) : s1L; p += (nCh >= 2) ? 2 : 0;
		s2L = (short)(p[0] | (p[1] << 8)); p += 2;
		s2R = (nCh >= 2) ? (short)(p[0] | (p[1] << 8)) : s2L; p += (nCh >= 2) ? 2 : 0;
		W16(dst, s2L); dst += 2;
		if (nCh >= 2) { W16(dst, s2R); dst += 2; }
		W16(dst, s1L); dst += 2;
		if (nCh >= 2) { W16(dst, s1R); dst += 2; }
		int dataBytes = blockAlign - preambleSize;
		for (int i = 0; i < dataBytes; i++) {
			unsigned char b = p[i];
			if (nCh >= 2) {
				process_nibble(b >> 4, &ideltaL, &s1L, &s2L, &coeffL);
				W16(dst, s1L); dst += 2;
				process_nibble(b & 0x0F, &ideltaR, &s1R, &s2R, &coeffR);
				W16(dst, s1R); dst += 2;
			}
			else {
				process_nibble(b >> 4, &ideltaL, &s1L, &s2L, &coeffL);
				W16(dst, s1L); dst += 2;
				process_nibble(b & 0x0F, &ideltaL, &s1L, &s2L, &coeffL);
				W16(dst, s1L); dst += 2;
			}
		}
	}
	*outBuf = buf;
	*outSize = (int)(dst - (unsigned char*)buf);
	return TRUE;
}

int readadpcmzwei(CFile& adpcmf, char* bw, int len)
{
	adpcmf.Read(bbuf, 0x2c);
	//	adpcmf.Read(bbuf,0x800);
	//	adpcmf.Read(abuf,0x8);
	int c = 0;
	int cnt = 2048;
	int ak = 0, lbuf = 0, o = 0;
	bw2 = (unsigned char*)bw;
	for (;;) {
		if (IsPlaybackStopRequested())
			return 0;
		if (lbuf == 0) {
			adpcmf.Read(abuf, 0x8);
			lbuf = 0x5000;
		}
		if (ak == 0) {
			cnt = adpcmf.Read(bbuf, 2048);
			if (cnt == 0)
				return 0;
			bbuf1 = (unsigned char*)bbuf;
			coeffL = MSADPCM_CoeffSet[*bbuf1++];
			coeffR = MSADPCM_CoeffSet[*bbuf1++];
			ideltaL = R16(bbuf1);    bbuf1 += 2;
			ideltaR = R16(bbuf1);    bbuf1 += 2;
			sample1L = R16(bbuf1);    bbuf1 += 2;
			sample1R = R16(bbuf1);    bbuf1 += 2;
			sample2L = R16(bbuf1);    bbuf1 += 2;
			sample2R = R16(bbuf1);    bbuf1 += 2;
			W16(bw2, sample2L);      bw2 += 2;
			W16(bw2, sample2R);      bw2 += 2;
			W16(bw2, sample1L);      bw2 += 2;
			W16(bw2, sample1R);      bw2 += 2;
			len -= 14; lbuf -= 14;
		}
		for (int k = ak; k < (cnt - 14); k++) {
			process_nibble(*bbuf1 >> 4, &ideltaL, &sample1L, &sample2L, &coeffL);
			if (o == 0) {
				W16(bw2, 0);	bw2 += 2;
			}
			else {
				W16(bw2, sample1L);	bw2 += 2;
			}
			process_nibble(*bbuf1++ & 0x0F, &ideltaR, &sample1R, &sample2R, &coeffR);
			if (o == 0) {
				W16(bw2, 0);	bw2 += 2;
			}
			else {
				W16(bw2, sample1R);	bw2 += 2;
			}
			len--; lbuf--;
			c += 4; if (c >= (int)(WAVDALen / OUTPUT_BUFFER_NUM) * 8)	wavwait = 1;
			if (len <= 0) return 0;
			if (thend1 == TRUE) return 1;
		}
		ak = 0;
		o = 1;
	}
}

int readadpcmgurumin(CFile& adpcmf, char* bw, int len)
{
	int c = 0;
	int cnt = 2048;
	int ak = 0, lbuf = 0;
	bw2 = (unsigned char*)bw;
	for (;;) {
		if (IsPlaybackStopRequested())
			return 0;
		if (lbuf == 0) {
			adpcmf.Read(abuf, 0x8);
			lbuf = 0x5000;
		}
		if (ak == 0) {
			cnt = adpcmf.Read(bbuf, 2048);
			if (cnt == 0)
				return 0;
			bbuf1 = (unsigned char*)bbuf;
			coeffL = MSADPCM_CoeffSet[*bbuf1++];
			coeffR = MSADPCM_CoeffSet[*bbuf1++];
			ideltaL = R16(bbuf1);    bbuf1 += 2;
			ideltaR = R16(bbuf1);    bbuf1 += 2;
			sample1L = R16(bbuf1);    bbuf1 += 2;
			sample1R = R16(bbuf1);    bbuf1 += 2;
			sample2L = R16(bbuf1);    bbuf1 += 2;
			sample2R = R16(bbuf1);    bbuf1 += 2;
			W16(bw2, sample2L);      bw2 += 2;
			W16(bw2, sample2R);      bw2 += 2;
			W16(bw2, sample1L);      bw2 += 2;
			W16(bw2, sample1R);      bw2 += 2;
			len -= 14; lbuf -= 14;
		}
		for (int k = ak; k < (cnt - 14); k++) {
			process_nibble(*bbuf1 >> 4, &ideltaL, &sample1L, &sample2L, &coeffL);
			W16(bw2, sample1L);	bw2 += 2;
			process_nibble(*bbuf1++ & 0x0F, &ideltaR, &sample1R, &sample2R, &coeffR);
			W16(bw2, sample1R);	bw2 += 2;
			len--; lbuf--;
			c += 4; if (c >= (int)(WAVDALen / OUTPUT_BUFFER_NUM) * 8)	wavwait = 1;
			if (len <= 0) return 0;
			if (thend1 == TRUE) return 1;
		}
		ak = 0;
	}
}


typedef struct ADPCMChannelStatus {
	int predictor;
	short int step_index;
	int step;
} ADPCMChannelStatus;

typedef struct ADPCMContext {
	ADPCMChannelStatus status[2];
} ADPCMContext;

static const int index_table[16] = {
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
};

static const int step_table[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
	19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
	130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
	5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

int adpcm_decode_init(ADPCMContext* c);
short adpcm_ima_expand_nibble(ADPCMChannelStatus* c, char nibble);
int adpcm_decode_frame_file(ADPCMContext* c, int channels, CFile& fd, char* fdo, int maxsize, char* bw);
int adpcm_decode_init(ADPCMContext* c) {
	memset(c, 0, sizeof(ADPCMContext));
	return 0;
}

short adpcm_ima_expand_nibble(ADPCMChannelStatus* c, char nibble) {
	int     diff,
		step;

	step = step_table[c->step_index];
	c->step_index += index_table[(unsigned)nibble];
	if (c->step_index < 0) c->step_index = 0;
	else if (c->step_index > 88) c->step_index = 88;
	diff = 0;
	if (nibble & 4) diff = step << 2;
	if (nibble & 2) diff += step << 1;
	if (nibble & 1) diff += step;
	diff >>= 2;
	if (nibble & 8) c->predictor -= diff;
	else c->predictor += diff;
	if (c->predictor < -32768) c->predictor = -32768;
	else if (c->predictor > 32767) c->predictor = 32767;
	return((short)c->predictor);
}


static unsigned int get_word(unsigned char* p) {
	return (p[0] + (p[1] << 8));
}

static unsigned long get_dword(unsigned char* p) {
	return (p[0] + (p[1] << 8) + (p[2] << 16) + (p[3] << 24));
}


static char flag_tbl[256][8];

static void make_flag_tbl(void) {
	int k, n, m;
	int c;
	for (n = 0; 256 > n; ++n) {
		c = -1;
		flag_tbl[n][0] = -1;
		m = n;
		for (k = 0; 8 > k; ++k) {
			if (0 != (m & 1)) {
				c += 1;
				flag_tbl[n][c] = 0;
			}
			else {
				if ((0 > c) || (0 == flag_tbl[n][c])) {
					c += 1;
					flag_tbl[n][c] = 0;
				}
				flag_tbl[n][c] += 1;
			}
			m >>= 1;
		}
		if (7 > c) {
			c += 1;
			flag_tbl[n][c] = -1;
		}
	}
}



static int expand(unsigned char* dest, long dest_size, unsigned char* src, long src_size, ADPCMContext* c, CFile& f) {
	long ofs = 0, d_ofs = 0;
	int mae = 0, cc = 0, rs = 0, rss = 0;
	short   samples[2];
	bw2 = (unsigned char*)adbuf2;
	ofs = 0;
	d_ofs = 0;
	while (ofs < src_size) {
		if (rs < src_size) {
			rss = f.Read(src + rs, 8096); rs += rss;
		}
		char* flag;
		int k;

		flag = flag_tbl[src[ofs]];
		ofs += 1;

		for (k = 0; 8 > k; ++k) {
			long t_ofs, t_len, t;

			if (src_size <= ofs) {
				break;
			}

			t_len = flag[k];
			if (0 > t_len) {
				break;
			}

			if (0 < t_len) {
				memcpy((dest + d_ofs), (src + ofs), t_len);
				d_ofs += t_len;
				ofs += t_len;
				continue;
			}

			t_ofs = get_word(src + ofs);
			t_len = ((t_ofs >> 12) + 2);
			t_ofs = (t_ofs & 0xfff);
			ofs += 2;

			t = t_ofs;
			t_ofs = (d_ofs - t_ofs);
			while (0 < t_len) {
				if (t_len < t) {
					t = t_len;
				}
				memcpy((dest + d_ofs), (dest + t_ofs), t);
				d_ofs += t;
				t_len -= t;
				t += t;
			}
			if (d_ofs > 0x10) {
				samples[0] = adpcm_ima_expand_nibble(&c->status[0], (dest[mae + 0x10] >> 4) & 0x0F);
				samples[1] = adpcm_ima_expand_nibble(&c->status[1], dest[mae + 0x10] & 0x0F);
				memcpy(bw2, &samples, sizeof(samples)); bw2 += sizeof(samples);
				cc += 4; if (cc >= (int)(WAVDALen / OUTPUT_BUFFER_NUM) * 8)	wavwait = 1;
				if (thend1 == TRUE) return 0;
				mae++;
			}
		}
	}
	for (; mae < d_ofs - 16 - 44100;) {
		samples[0] = adpcm_ima_expand_nibble(&c->status[0], (dest[mae + 0x10] >> 4) & 0x0F);
		samples[1] = adpcm_ima_expand_nibble(&c->status[1], dest[mae + 0x10] & 0x0F);
		memcpy(bw2, &samples, sizeof(samples)); bw2 += sizeof(samples);
		cc += 4; if (cc >= (int)(WAVDALen / OUTPUT_BUFFER_NUM) * 8)	wavwait = 1;
		if (thend1 == TRUE) return 0;
		mae++;
	}
	float fa = 1.0f; int cnt;//プチっとノイズは最後の最後をフェードアウトさせて消す(誤魔化す)
	for (cnt = 0; mae < d_ofs - 16; cnt++) {
		samples[0] = adpcm_ima_expand_nibble(&c->status[0], (dest[mae + 0x10] >> 4) & 0x0F);
		samples[1] = adpcm_ima_expand_nibble(&c->status[1], dest[mae + 0x10] & 0x0F);
		samples[0] = (short)((float)samples[0] * fa);
		samples[1] = (short)((float)samples[1] * fa);
		memcpy(bw2, &samples, sizeof(samples)); bw2 += sizeof(samples);
		cc += 4; if (cc >= (int)(WAVDALen / OUTPUT_BUFFER_NUM) * 8)	wavwait = 1;
		if (thend1 == TRUE) return 0;
		mae++;
		fa = (44100.0f - (float)cnt) / 44100.0f;
	}
	return d_ofs;
}


int readadpcmarc(CFile& adpcmf, char* bw, int len)
{
	ADPCMContext    ctx;

	adpcm_decode_init(&ctx);
	make_flag_tbl();
	BYTE* b; b = new BYTE[len * 2];
	//    expand(&ctx, 2, adpcmf, bw, len,bw);
	expand(b, 4096, (BYTE*)bw, len, &ctx, adpcmf);
	delete[] b;

	wavwait = 1;
	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////DirectSond Read///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

int playwavkpi(BYTE* bw, int old, int l1, int l2)
{
	// x64 KPI は別プロセス経由でデコードするため、この時点で kpidec==NULL になり得る
	if (!g_kpiRemote && og->mod == NULL && kpidec == NULL) return 0;
	{
		const int bpf = PcmOutBytesPerFrame();
		playb += (l1 + l2) / bpf;
	}
	//データ読み込み
	int rrr = readkpi(bw + old, l1);
	if (l1 != rrr) {
		if (endf == 1) {
			l1 = rrr;
			if (savedata.saverenzoku == 0)
				fade1 = 1;
			else
				endflg = 1;

		}
		else {
			loopcnt++;
			playb = loop1;
			if (kvver == 2)
				og->mod->SetPosition(og->kmp1, 0);
			else {
				if (g_kpiRemote && g_kpiSession.sessionId != 0) {
					uint64_t np = 0;
					g_kpiHost.Seek(g_kpiSession.sessionId, 0, 1, np);
				}
				else {
					kpidec->Seek(0, 1);
				}
			}
			poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			cnt3 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			reset = TRUE;
			readkpi(bw + old + rrr, l1 - rrr);
		}
	}
	if (l2) {
		rrr = readkpi(bw, l2);
		if (l2 != rrr) {
			if (endf == 1) {
				l2 = rrr;
				if (savedata.saverenzoku == 0)
					fade1 = 1;
				else
					endflg = 1;

			}
			else {
				loopcnt++;
				playb = loop1;
				if (kvver == 2)
					og->mod->SetPosition(og->kmp1, 0);
				else {
					if (g_kpiRemote && g_kpiSession.sessionId != 0) {
						uint64_t np = 0;
						g_kpiHost.Seek(g_kpiSession.sessionId, 0, 1, np);
					}
					else {
						kpidec->Seek(0, 1);
					}
				}
				poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				cnt3 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				reset = TRUE;
				readkpi(bw + rrr, (int)l2 - rrr);
			}
		}
	}
	return l1 + l2;
}



// 16bit, 24bit, 32bit 全ての出力に対応し、巨大なfloat値にも自動対応する万能変換関数ですわ！
// 最もシンプルで確実な形に戻した万能変換関数ですわ
static DWORD ConvertFloatTypedToIntBuffer(const void* src, DWORD samples, int srcBits, int channels, BYTE* dst, DWORD dstBytes, int dstBits)
{
	if (!src || !dst || samples == 0 || channels <= 0 || dstBytes == 0) return 0;
	if (srcBits != -32 && srcBits != -64) return 0;
	const DWORD totalSamples = samples * (DWORD)channels;
	const DWORD dstBytesPerSample = (DWORD)abs(dstBits) / 8;
	if (dstBytesPerSample == 0) return 0;
	const DWORD needBytes = totalSamples * dstBytesPerSample;
	if (needBytes > dstBytes) return 0;

	if (dstBits == 16) {
		short* out = (short*)dst;
		if (srcBits == -32) {
			const float* p = (const float*)src;
			for (DWORD i = 0; i < totalSamples; ++i) {
				float v = p[i];
				if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
				out[i] = (short)(v * 32767.0f);
			}
		}
		else {
			const double* p = (const double*)src;
			for (DWORD i = 0; i < totalSamples; ++i) {
				out[i] = Kbpsf2ScaledDoubleToInt16(p[i]);
			}
		}
	}
	else if (dstBits == 32) {
		int* out = (int*)dst;
		if (srcBits == -32) {
			const float* p = (const float*)src;
			for (DWORD i = 0; i < totalSamples; ++i) {
				float v = p[i];
				if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
				out[i] = (int)(v * 2147483647.0f);
			}
		}
		else {
			const double* p = (const double*)src;
			for (DWORD i = 0; i < totalSamples; ++i) {
				double v = p[i];
				if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
				out[i] = (int)(v * 2147483647.0);
			}
		}
	}
	else if (dstBits == 24) {
		BYTE* out = dst;
		if (srcBits == -32) {
			const float* p = (const float*)src;
			for (DWORD i = 0; i < totalSamples; ++i) {
				float v = p[i];
				if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
				int val = (int)(v * 8388607.0f);
				out[i * 3] = (BYTE)(val & 0xFF);
				out[i * 3 + 1] = (BYTE)((val >> 8) & 0xFF);
				out[i * 3 + 2] = (BYTE)((val >> 16) & 0xFF);
			}
		}
		else {
			const double* p = (const double*)src;
			for (DWORD i = 0; i < totalSamples; ++i) {
				double v = p[i];
				if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
				int val = (int)(v * 8388607.0);
				out[i * 3] = (BYTE)(val & 0xFF);
				out[i * 3 + 1] = (BYTE)((val >> 8) & 0xFF);
				out[i * 3 + 2] = (BYTE)((val >> 16) & 0xFF);
			}
		}
	}
	else {
		return 0;
	}
	return needBytes;
}

static bool IsBlockSilent(const BYTE* buffer, int bytes, int bitDepth)
{
	if (bytes <= 0) return true;
	if (bitDepth == 16) {
		const short* s = (const short*)buffer;
		int count = bytes / 2;
		for (int i = 0; i < count; i++) {
			if (abs((int)s[i]) > 32) return false;
		}
	} else if (bitDepth == 24) {
		int count = bytes / 3;
		for (int i = 0; i < count; i++) {
			int val = (int)buffer[i * 3] | ((int)buffer[i * 3 + 1] << 8) | ((int)(signed char)buffer[i * 3 + 2] << 16);
			if (abs(val) > 8192) return false;
		}
	} else if (bitDepth == 32) {
		const int* s = (const int*)buffer;
		int count = bytes / 4;
		for (int i = 0; i < count; i++) {
			if (abs(s[i]) > 2097152) return false;
		}
	} else {
		for (int i = 0; i < bytes; i++) {
			if (buffer[i] != 0 && buffer[i] != 0x80) return false;
		}
	}
	return true;
}

int readkpi(BYTE* bw, int cnt)
{
	if (cnt == 0) return 0;
	_set_se_translator(trans_func);
	DWORD cnt1 = (kvver == 2) ? og->sikpi.dwUnitRender : 4096, cnt2 = (DWORD)cnt, cnt4 = 0; if (cnt1 == 0) cnt1 = 4096;
	DWORD r = cnt;

	// 無音判定用に、ここで先に拡張子を取得しておきますわ
	CString sss;
	sss = filen.Right(filen.GetLength() - filen.ReverseFind('.') - 1);
	sss.MakeLower();
	EqualiserSetFormatVolContext(1, (sss == "spc" || sss.Left(3) == "hes"));

	try {
		int len3 = 0, len4 = 0;
		int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
		if (poss4 <= cnt) {
			r = 0;
			for (int kl = 0; kl < 5; kl++) {
				for (;;) {
					if (IsPlaybackStopRequested())
						break;
					DWORD requestBytes = cnt1;
					if (cnt2 <= cnt3) {
						r = 1;
						break;
					}
					if (kvver == 2) {
						const int bitsPerSample = abs(wavsam_depth);
						const DWORD bytesPerFrame = (DWORD)max(1, wavchannel * (bitsPerSample / 8));
						const DWORD remainBytes = (cnt > (int)cnt3) ? (DWORD)(cnt - (int)cnt3) : 0;
						requestBytes = min(cnt1, remainBytes);
						if (bytesPerFrame > 1) requestBytes -= (requestBytes % bytesPerFrame);
						if (requestBytes == 0) break;
						if (IsBadCodePtr((FARPROC)og->mod->Render) == 0)
							r = og->mod->Render(og->kmp1, (BYTE*)bufkpi + cnt3, requestBytes);
					}
					if (kvver == 5) {
						const int dstBitsPerSample = abs(wavsam_depth);
						const int dstBytesPerFrame = max(1, wavchannel * (dstBitsPerSample / 8));
						bool rIsBytes = false;

						if (g_kpiRemote && g_kpiSession.sessionId != 0) {
							const DWORD remainBytes = (cnt > (int)cnt3) ? (DWORD)(cnt - (int)cnt3) : 0;
							const bool srcFloat = (wavsam_src == -32 || wavsam_src == -64);
							const DWORD srcBytesPerFrame = srcFloat ? (DWORD)max(1, wavchannel * (abs(wavsam_src) / 8)) : (DWORD)dstBytesPerFrame;

							const DWORD dstFrames = (dstBytesPerFrame > 0) ? (remainBytes / (DWORD)dstBytesPerFrame) : 0;
							DWORD requestBytesLocal = srcFloat ? (dstFrames * srcBytesPerFrame) : remainBytes;
							if (srcBytesPerFrame > 1) requestBytesLocal -= (requestBytesLocal % srcBytesPerFrame);

							const size_t need = (size_t)requestBytesLocal;
							const size_t remain = (g_kpiRemoteCache.size() > g_kpiRemoteCachePos) ? (g_kpiRemoteCache.size() - g_kpiRemoteCachePos) : 0;

							if (remain < need && !g_kpiRemoteEof) {
								const uint32_t want = (uint32_t)max((DWORD)need, (DWORD)(256 * 1024));
								std::vector<uint8_t> pcm;
								bool eof = false;
								if (!g_kpiHost.RenderBytes(g_kpiSession.sessionId, want, pcm, eof)) {
									r = 0;
								}
								else {
									if (g_kpiRemoteCachePos > 0) {
										g_kpiRemoteCache.erase(g_kpiRemoteCache.begin(), g_kpiRemoteCache.begin() + (ptrdiff_t)g_kpiRemoteCachePos);
										g_kpiRemoteCachePos = 0;
									}
									g_kpiRemoteCache.insert(g_kpiRemoteCache.end(), pcm.begin(), pcm.end());
									if (eof) g_kpiRemoteEof = true;
								}
							}

							const size_t avail = (g_kpiRemoteCache.size() > g_kpiRemoteCachePos) ? (g_kpiRemoteCache.size() - g_kpiRemoteCachePos) : 0;
							DWORD copyBytes = (DWORD)min((size_t)requestBytesLocal, avail);
							if (srcBytesPerFrame > 1) copyBytes -= (copyBytes % srcBytesPerFrame);

							if (copyBytes > 0) {
								if (srcFloat) {
									DWORD gotSamples = copyBytes / srcBytesPerFrame;
									const DWORD outBytes = ConvertFloatTypedToIntBuffer(
										g_kpiRemoteCache.data() + g_kpiRemoteCachePos,
										gotSamples,
										wavsam_src,
										wavchannel,
										(BYTE*)bufkpi + cnt3,
										remainBytes,
										dstBitsPerSample);
									g_kpiRemoteCachePos += copyBytes;
									r = outBytes;
									requestBytes = outBytes;
								}
								else {
									memcpy((BYTE*)bufkpi + cnt3, g_kpiRemoteCache.data() + g_kpiRemoteCachePos, copyBytes);
									g_kpiRemoteCachePos += copyBytes;
									r = copyBytes;
									requestBytes = copyBytes;
								}
								rIsBytes = true;
							}
							else {
								r = 0;
								rIsBytes = true;
								requestBytes = remainBytes;
							}
							if (g_kpiRemoteEof && copyBytes < requestBytesLocal) fade1 = 1;
						}
						else
						{
							const int remainBytes = max(0, (int)cnt - (int)cnt3);
							const DWORD requestSamples = (DWORD)(remainBytes / dstBytesPerFrame);

							if (wavsam_src == -32 || wavsam_src == -64) {
								DWORD gotSamples = 0;
								if (requestSamples > 0) {
									if (wavsam_src == -64) {
										std::vector<double> srcD;
										srcD.resize((size_t)requestSamples * wavchannel);
										gotSamples = kpidec->Render((BYTE*)srcD.data(), requestSamples);
										r = ConvertFloatTypedToIntBuffer(srcD.data(), gotSamples, -64, wavchannel, (BYTE*)bufkpi + cnt3, (DWORD)remainBytes, dstBitsPerSample);
									}
									else {
										std::vector<float> srcF;
										srcF.resize((size_t)requestSamples * wavchannel);
										gotSamples = kpidec->Render((BYTE*)srcF.data(), requestSamples);
										r = ConvertFloatTypedToIntBuffer(srcF.data(), gotSamples, -32, wavchannel, (BYTE*)bufkpi + cnt3, (DWORD)remainBytes, dstBitsPerSample);
									}
								}
								else {
									r = 0;
								}
								rIsBytes = true;
							}
							else {
								if (requestSamples > 0) {
									r = kpidec->Render((BYTE*)bufkpi + cnt3, requestSamples);
								}
								else {
									r = 0;
								}
							}
						}
						if (!rIsBytes) {
							r = (DWORD)(r * dstBytesPerFrame);
						}
					}
					if (r > 0 && fade1 != 1) {
						if (IsBlockSilent((const BYTE*)bufkpi + cnt3, (int)r, abs(wavsam_depth))) {
							kpi_silence_bytes += r;
						} else {
							kpi_silence_bytes = 0;
						}
						int maxSilentBytes = (int)((double)wavbit_sample_Hz * (double)wavchannel * (double)(abs(wavsam_depth) / 8) * 4.0);
						if (maxSilentBytes > 0 && kpi_silence_bytes >= maxSilentBytes) {
							fade1 = 1;
						}
					}
					if (r == 0) fade1 = 1;

					if (fade1 == 1) {
						if (muon != 0) {
							if (savedata.saverenzoku == 0) {
								int fill_size = (int)requestBytes;
								if (cnt3 + fill_size > cnt) fill_size = cnt - cnt3;
								if (fill_size > 0) {
									ZeroMemory((BYTE*)bufkpi + cnt3, fill_size);
									r = fill_size;
									muon--;
								}
								else {
									break;
								}
							}
							else {
								endflg = 1;
								break;
							}
						}
						else {
							break;
						}
					}

					cnt3 += r;
				}
				if (IsPlaybackStopRequested())
					break;
				int len2 = readtempo(bufkpi, cnt);

				if (len2 > 0) {
					RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
					poss4 += len2;
					if (cnt3 < cnt)
						return cnt4;
					if (cnt2 <= cnt3) {
						cnt3 -= cnt2;
						if (cnt3 != 0)	memcpy(bufkpi, bufkpi + cnt2, cnt3);
					}
				}
				if (poss4 > cnt) break;
			}
		}

		cnt2 = cnt;

		if (cnt2 > 0) {
			int to_read = cnt;
			RingBufRead(bw, bufkpi3, max_buffer_size, poss3, to_read);
			poss4 -= to_read;
		}

		equaliser(bw, cnt, reset);
		og->FeedPianoRoll(bw, cnt);
		reset = FALSE;

		cnt4 = cnt3;
		if (r == 0) cnt = 0;
		__int64 bfc = 0, bc2 = 0;

		if (wavsam_depth == 32) {
			int* bf1 = (int*)bw;
			bc2 = bf1[0] / 65536;
			for (int i = 0; i < cnt / 4; i++) {
				bfc += (__int64)(bf1[i] / 65536);
			}
			if (cnt) bfc /= (cnt / 4);
			if ((int)bc2 >= (int)bfc - 10 && (int)bc2 <= (int)bfc + 10) bufzero++; else bufzero = 0;
		}
		else if (wavsam_depth == 24) {
			Int24* bf1 = (Int24*)bw;
			bc2 = (int)(Int24)bf1[0] / 256;
			for (int i = 0; i < cnt / 3; i++) {
				bfc += (__int64)((int)(Int24)bf1[i] / 256);
			}
			if (cnt) bfc /= (cnt / 3);
			if ((int)bc2 >= (int)bfc - 10 && (int)bc2 <= (int)bfc + 10) bufzero++; else bufzero = 0;
		}
		else {
			short* bf1 = (short*)bw;
			bc2 = (short)bf1[0];
			for (int i = 0; i < cnt / 2; i++) {
				bfc += (__int64)(short)bf1[i];
			}
			if (cnt) bfc /= (cnt / 2);
			if ((short)bc2 >= (short)bfc - 10 && (short)bc2 <= (short)bfc + 10) bufzero++; else bufzero = 0;
		}

		int looping = loop2 / 100000;
		if (looping < 20) looping = 20;
		if (looping > 80) looping = 80;

		short* b, c;
		b = (short*)bw;
		Int24* b24c;
		b24c = (Int24*)bw;
		int* b32c;
		b32c = (int*)bw;

		fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
		if (wavsam_depth == 32) {
			for (int i = 0; i < cnt / 4; i++) {
				double c4 = (double)b32c[i];
				c4 = c4 * (double)fade * (double)fade;
				if (c4 > 2147483647.0) c4 = 2147483647.0;
				if (c4 < -2147483648.0) c4 = -2147483648.0;
				b32c[i] = (int)c4;
			}
		}
		else if (wavsam_depth == 24) {
			for (int i = 0; i < cnt / 3; i++) {
				double c4 = (double)b24c[i];
				c4 = c4 * (double)fade * (double)fade;
				if (c4 > 8388607.0) c4 = 8388607.0;
				if (c4 < -8388608.0) c4 = -8388608.0;
				b24c[i] = (int)c4;
			}
		}
		else {
			for (int i = 0; i < cnt / 2; i++) {
				double cv = (double)b[i];
				cv = cv * (double)fade * (double)fade;
				if (cv > 32767.0) cv = 32767.0;
				if (cv < -32768.0) cv = -32768.0;
				b[i] = (short)cv;
			}
		}

		if (cc1 == 1)	cc.Write(bw, cnt);
		wl += cnt;
		lenl += cnt;
	}
	catch (SE_Exception e) {
	}
	catch (_EXCEPTION_POINTERS* ep) {
	}
	catch (...) {}

	return cnt;
}

int playwavm4a(BYTE* bw, int old, int l1, int l2)
{
	//データ読み込み
	int rrr = readm4a(bw + old, l1);
	{
		const int bpf = PcmOutBytesPerFrame();
		playb += (l1 + l2) / bpf;
	}
	//	if (oggsize / ((wavchannel == 1) ? 1 : 1) - 44100 <= playb * 4) {
	//	if (savedata.saveloop == FALSE) {
	//	l1 = rrr;  fade1 = 1;
	//			return l1;
	//	}
	//}
/*	if (oggsize / ((wavchannel == 1) ? 2 : 1)- 50000 <= (int)(playb * wavchannel * 2 * (wavsam_depth / 16.0))) {
		if (savedata.saveloop == FALSE) {
			l1 = rrr; fade1 = 1;
			return l1;
		}
	}*/

	if (l1 != rrr) {
		if (savedata.saveloop == 0 && endf == 1) {
			l1 = rrr;
			if (savedata.saverenzoku == 0)
				fade1 = 1;
			else
				endflg = 1;
		}
		else {
			loopcnt++;
			playb = loop1;
			m4a_.SetPosition(og->kmp, 0);
			poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			reset = TRUE;
			readm4a(bw + old + rrr, l1 - rrr);
		}
	}
	if (l2) {
		rrr = readm4a(bw, l2);
		if (l2 != rrr) {
			if (savedata.saveloop == 0 && endf == 1) {
				l2 = rrr;
				if (savedata.saverenzoku == 0)
					fade1 = 1;
				else
					endflg = 1;
			}
			else {
				loopcnt++;
				playb = loop1;
				m4a_.SetPosition(og->kmp, 0);
				poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				reset = TRUE;
				readm4a(bw + rrr, (int)l2 - rrr);
			}
		}
	}
	return l1 + l2;
}

int readm4a(BYTE* bw, int cnt)
{
	if (cnt == 0) return 0;
	if (IsPlaybackStopRequested() || !og || !og->kmp)
		return 0;
	EqualiserSetFormatVolContext(2, FALSE);
	_set_se_translator(trans_func);
	DWORD cnt1 = og->sikpi.dwUnitRender, cnt2 = (DWORD)cnt, cnt4 = 0; if (cnt1 == 0) cnt1 = 4096;
	DWORD r = 0;
	{
		int len3 = 0, len4 = 0;
		int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
		if (poss4 <= cnt) {
			while (true) {
				if (IsPlaybackStopRequested() || !og->kmp)
					break;
				if (rrr != 1)
					break;
				if (rrr == 1) {
					for (;;) {
						if (IsPlaybackStopRequested() || !og->kmp)
							break;
						if (cnt2 <= cnt3) break;
						r = m4a_.Render(og->kmp, (BYTE*)bufkpi + cnt3, cnt1);
						cnt3 += r;
						if (r == 0) break;
					}
					if (fade1 == 1 && muon != 0) {
						r = cnt;
						ZeroMemory(bufkpi, r);
						muon--;
					}
					if (fade1 == 1 && muon == MUON) {
						ZeroMemory(bufkpi + r, cnt - r);
						r = cnt;
						muon--;
					}
					// muon 潰しは fade1 停止時のみ（連続再生 endflg 経路では fade1==0 のままなのでここは通さない）
					if (fade1 == 1 && muon == 0) r = 0;

					int len2 = readtempo(bufkpi, cnt);

					if (len2 > 0) {
						// 書き込み
						RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
						poss4 += len2;
						if (cnt3 < cnt) return cnt4;
						if (cnt2 <= cnt3) {
							cnt3 -= cnt2;
							if (cnt3 != 0)	memcpy(bufkpi, bufkpi + cnt2, cnt3);
						}
					}
					// readtempo が 0 でデコード不足のとき無限ループしないよう抜ける
					if (len2 <= 0 && cnt3 < cnt)
						break;
					if (poss4 > cnt) break;
				}
			}
		}

		cnt2 = cnt;

		if (cnt2 > 0) {
			int to_read = cnt;
			RingBufRead(bw, bufkpi3, max_buffer_size, poss3, to_read);
			poss4 -= to_read;
		}

		equaliser(bw, cnt, reset);
		og->FeedPianoRoll(bw, cnt);
		reset = FALSE;

		cnt4 = cnt3;
		//if (r == 0) cnt = 0;
		memcpy(bufkpi2, bw, cnt);
		unsigned short* bf1, * bf2; bf1 = (unsigned short*)bw; bf2 = (unsigned short*)bufkpi2;
		int cnt1 = cnt / 2;
		switch (wavchannel)
		{
		case 1:
		case 2:
			break;
		case 3: // 2.1   
			for (int sample = 0; sample < cnt1; sample += wavchannel)
			{
				int ChannelMap[3] = { 2,3,1 };
				for (int ch = 0; ch < wavchannel; ch++)
				{
					*bf1++ = bf2[ChannelMap[ch] - 1];
				}
				bf2 += wavchannel;
			}
			break;
		case 4: // Quad   
			for (int sample = 0; sample < cnt1; sample += wavchannel)
			{
				int ChannelMap[4] = { 2,3,1,4 };
				for (int ch = 0; ch < wavchannel; ch++)
				{
					*bf1++ = bf2[ChannelMap[ch] - 1];
				}
				bf2 += wavchannel;
			}
			break;
		case 5: // Surround   
			for (int sample = 0; sample < cnt1; sample += wavchannel)
			{
				int ChannelMap[5] = { 2,3,1,4,5 };
				for (int ch = 0; ch < wavchannel; ch++)
				{
					*bf1++ = bf2[ChannelMap[ch] - 1];
				}
				bf2 += wavchannel;
			}
			break;
		case 6: // 5.1   
			for (int sample = 0; sample < cnt1; sample += wavchannel)
			{
				int ChannelMap[6] = { 2,3,1,6,4,5 };
				for (int ch = 0; ch < wavchannel; ch++)
				{
					*bf1++ = bf2[ChannelMap[ch] - 1];
				}
				bf2 += wavchannel;
			}
			break;
		}
		Int24* b24c;
		b24c = (Int24*)bw;
		short* b, c;
		b = (short*)bw;
		fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
		//fadeを三乗して計算密度を変更
		if (wavsam_depth == 24) {
			float c4;
			int c5;
			for (int i = 0; i < cnt / 3; i++) {
				c5 = b24c[i]; c4 = (float)c5;
				c4 = c4 * fade * fade; c5 = (int)c4;
				b24c[i] = c5;
			}
		}
		else {
			for (int i = 0; i < cnt / 2; i++) { c = b[i]; c = (short)(((float)c) * fade * fade); b[i] = c; }
		}
		if (cc1 == 1)	cc.Write(bw, cnt);
		wl += cnt;
		lenl += cnt;
	}
	return cnt;
}


int playwavflac(BYTE* bw, int old, int l1, int l2)
{
	// HandleNotifications は len1/len2 を値渡しのため、部分読み後も Lock 長は元のまま。
	// 未書き領域をゼロ埋めしないと memcpy で未定義データが DS に渡りクラッシュの原因になる。
	const int l1req = l1;
	const int l2req = l2;
	int rrr = readflac(bw + old, l1);
	if (l1 != rrr) {
		if (savedata.saveloop == 0 && endf == 1 && flacmode == 0) {
			l1 = rrr;
			if (savedata.saverenzoku == 0)
				fade1 = 1;
			else
				endflg = 1;
		}
		else {
			loopcnt++;
			playb = loop1;
			if (flacmode == 0)
				flac_.SetPosition(og->kmp, (LONGLONG)((double)loop1 / (((double)wavbit_sample_Hz * (double)wavchannel) / 2000.0)));
			else
				flac_.SetPosition(og->kmp, loop1);
			::rrr = 1;  // シーク後は Render を再開（グローバル）
			poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			reset = TRUE;
			readflac(bw + old + rrr, l1req - rrr);
			l1 = l1req;
		}
	}
	if (l1 < l1req)
		ZeroMemory(bw + old + l1, (SIZE_T)(l1req - l1));
	int l2out = l2req;
	if (l2req) {
		rrr = readflac(bw, l2req);
		if (l2req != rrr) {
			if (savedata.saveloop == 0 && endf == 1 && flacmode == 0) {
				l2out = rrr;
				if (savedata.saverenzoku == 0)
					fade1 = 1;
				else
					endflg = 1;
			}
			else {
				loopcnt++;
				playb = loop1;
				if (flacmode == 0)
					flac_.SetPosition(og->kmp, (LONGLONG)((double)loop1 / (((double)wavbit_sample_Hz * (double)wavchannel) / 2000.0)));
				else
					flac_.SetPosition(og->kmp, loop1);
				::rrr = 1;  // シーク後は Render を再開（グローバル）
				poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				reset = TRUE;
				readflac(bw + rrr, (int)l2req - rrr);
				l2out = l2req;
			}
		}
		if (l2out < l2req)
			ZeroMemory(bw + l2out, (SIZE_T)(l2req - l2out));
	}
	if (flacmode == 0) {
		const int bpf = PcmOutBytesPerFrame();
		playb += (l1 + l2out) / bpf;
	}
	return l1 + l2out;
}

int readflac(BYTE* bw, int cnt)
{
	if (cnt == 0)return 0;
	if (IsPlaybackStopRequested() || !og || !og->kmp)
		return 0;
	EqualiserSetFormatVolContext(0, FALSE);
	_set_se_translator(trans_func);
	DWORD cnt1 = og->sikpi.dwUnitRender * 2, cnt2 = (DWORD)cnt, cnt4 = 0, lenl = cnt; if (cnt1 == 0) cnt1 = 1024;
	DWORD r = 0;
	if (flacmode == 1)
		if (playb + lenl / 6 > (loop1 + loop2)) lenl = ((loop1 + loop2) - playb) * 6;
	try {
		int len3 = 0, len4 = 0;
		int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
		if (poss4 < lenl) {
			int flacRbStallIters = 0;
			const int kFlacRbStallMax = 512;
			while (true) {
				if (IsPlaybackStopRequested() || !og->kmp)
					break;

				if (rrr == 1)
					r = flac_.Render(og->kmp, (BYTE*)bufkpi, lenl);
				const int flacSrcBits = wavsam_depth;
				if (r > 0 && (flacSrcBits == -32 || flacSrcBits == -64)) {
					r = ConvertFloatPcmBufferToInt16InPlace((BYTE*)bufkpi, (int)r, flacSrcBits, wavchannel);
					if (r > 0) {
						// After conversion, downstream path should treat this block as 16-bit PCM.
						wavsam_depth = 16;
						lenl = r;
					}
				}
				if (r != lenl && savedata.saveloop == 0)
					rrr = 0;
				// EOF の場合は muon を使わず部分読みを返す（曲終了検出のため）。通常のドロップアウト時のみ muon でゼロ埋め
				if (fade1 == 1 && muon != 0) {
					r = lenl;
					ZeroMemory(bufkpi, r);
					muon--;
				}
				if (fade1 == 1 && muon == MUON) {
					ZeroMemory(bufkpi + r, lenl - r);
					r = lenl;
					muon--;
				}
				// fade1 停止フェードで muon を使い切ったら入力を切る（fade1==0 の曲末では muon だけで r を潰さない）
				if (fade1 == 1 && muon == 0) r = 0;
				cnt4 = r;
				if (r == 0) lenl = 0;
				if (r == 0) {
					// デコード EOF: readtempo(0) が RB 尻尾を常に吐く（fade1 含む）
					int tailLen = readtempo(bufkpi, 0);
					if (tailLen > 0) {
						RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), tailLen);
						poss4 += tailLen;
					}
					break;
				}

				int len2 = readtempo(bufkpi, lenl);
				if (len2 > 0) {
					flacRbStallIters = 0;
					// 書き込み
					RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
					poss4 += len2;
					if (cnt4 != lenl) return cnt4;
					if (poss4 > lenl) break;
				}
				// len2==0 でもデコードが部分読みなら上位へ返さないとループし得る
				if (cnt4 != lenl) return cnt4;
				if (len2 <= 0 && (fade1 == 1 || IsPlaybackStopRequested())) break;
				// フルブロック decode 済みだが RB がまだ出さない（readmp3 と同様）
				if (len2 <= 0 && r > 0 && r == (DWORD)lenl) {
					if (++flacRbStallIters >= kFlacRbStallMax)
						break;
					continue;
				}
			}
		}

		cnt2 = lenl;

		if (cnt2 > 0) {
			int to_read = lenl;
			RingBufRead(bw, bufkpi3, max_buffer_size, poss3, to_read);
			poss4 -= to_read;
		}

		equaliser(bw, cnt2, reset);
		og->FeedPianoRoll(bw, cnt2);
		reset = FALSE;

		cnt4 = lenl;
		unsigned short* bf1, * bf2; bf1 = (unsigned short*)bw; bf2 = (unsigned short*)bufkpi2;
		//		int fw = playb % (wavchannel);
		//		bf2 += fw;
		int lenl1 = lenl / 2;
		Int24* b24c;
		b24c = (Int24*)bw;
		short* b, c;
		b = (short*)bw;
		if (wavsam_depth == 24) {
			for (int i = 0; i < lenl / 3; i++) {
				int c4 = b24c[i];
				if (savedata.mp3 == 2)	c4 = (int)((float)c4 * 2.0f);
				else if (savedata.mp3 == 4) c4 = (int)((float)c4 * 3.0f);
				else if (savedata.mp3 == 8) c4 = (int)((float)c4 * 4.0f);
				else if (savedata.mp3 == 16) c4 = (int)((float)c4 * 5.0f);
				if (c4 > 8388607)c4 = 8388607;
				if (c4 < -8388608)c4 = -8388608;
				b24c[i] = c4;
			}
		}
		else {
			for (int i = 0; i < lenl / 2; i++) {
				int c = (int)b[i];
				if (savedata.mp3 == 2)	c = (int)((float)b[i] * 1.5f);
				else if (savedata.mp3 == 3) c = (int)((float)b[i] * 2.0f);
				else if (savedata.mp3 == 4) c = (int)((float)b[i] * 2.5f);
				else if (savedata.mp3 == 5) c = (int)((float)b[i] * 3.0f);
				if (c >= 32768)c = 32767;
				if (c <= -32767)c = -32766;
				b[i] = (short)c;
			}
		}
		fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
		//fadeを三乗して計算密度を変更
		if (wavsam_depth == 24) {
			float c4;
			int c5;
			for (int i = 0; i < lenl / 3; i++) {
				c5 = b24c[i]; c4 = (float)c5;
				c4 = c4 * fade * fade; c5 = (int)c4;
				b24c[i] = c5;
			}
		}
		else {
			for (int i = 0; i < lenl / 2; i++) { c = b[i]; c = (short)(((float)c) * fade * fade); b[i] = c; }
		}
		if (cc1 == 1)	cc.Write(bw, lenl);
		wl += lenl;
		//lenl += lenl;
		//	playb+=lenl/4;
	}
	catch (SE_Exception e) {
	}
	catch (_EXCEPTION_POINTERS* ep) {
	}
	catch (...) {}
	if (cnt4 < lenl) lenl = cnt4;
	if (flacmode == 1) playb += (lenl / 6);
	return lenl;
}

int playwavopus(BYTE* bw, int old, int l1, int l2)
{
	//データ読み込み
	int rrr = readopus(bw + old, l1);
	{
		const int bpf = PcmOutBytesPerFrame();
		playb += (l1 + l2) / bpf;
	}
	if (l1 != rrr) {
		if (savedata.saveloop == 0 && endf == 1) {
			l1 = rrr;
			if (savedata.saverenzoku == 0)
				fade1 = 1;
			else
				endflg = 1;
		}
		else {
			loopcnt++;
			playb = loop1;
			opus_.SetPosition(og->kmp, (DWORD)loop1);
			poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			reset = TRUE;
			readopus(bw + old + rrr, l1 - rrr);
		}
	}
	if (l2) {
		rrr = readopus(bw, l2);
		if (l2 != rrr) {
			if (savedata.saveloop == 0 && endf == 1) {
				l2 = rrr;
				if (savedata.saverenzoku == 0)
					fade1 = 1;
				else
					endflg = 1;
			}
			else {
				loopcnt++;
				playb = loop1;
				opus_.SetPosition(og->kmp, (DWORD)loop1);
				poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				reset = TRUE;
				readopus(bw + rrr, (int)l2 - rrr);
			}
		}
	}
	return l1 + l2;
}

int readopus(BYTE* bw, int cnt)
{
	if (cnt == 0)return 0;
	_set_se_translator(trans_func);
	DWORD cnt1 = og->sikpi.dwUnitRender * 2, cnt2 = (DWORD)cnt, cnt4; if (cnt1 == 0) cnt1 = 1024;
	DWORD r = 0;
	try {
		int len3 = 0, len4 = 0;
		int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
		if (poss4 <= cnt) {
			for (int k = 0; k < 3; k++) {
				if (IsPlaybackStopRequested())
					break;
				if (rrr == 1) {
					r = opus_.Render(og->kmp, (BYTE*)bufkpi, cnt);
					if (r != cnt && savedata.saveloop == 0)
						rrr = 0;
					if (fade1 == 1 && muon != 0) {
						r = cnt;
						ZeroMemory(bufkpi, r);
						muon--;
					}
					if (fade1 == 1 && muon == MUON) {
						ZeroMemory(bufkpi + r, cnt - r);
						r = cnt;
						muon--;
					}
					if (fade1 == 1 && muon == 0) r = 0;

					int len2 = readtempo(bufkpi, r);
					cnt4 = r;
					if (cc1 == 1)	cc.Write(outputRawBytesData.data(), len2);
					wl += len2;

					if (len2 > 0) {
						// 書き込み
						RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
						poss4 += len2;
						if (r < cnt) return cnt4;

					}
					else if (r < cnt)
						return cnt4;
				}
			}

			cnt2 = lenl;

			if (cnt2 > 0) {
				int to_read = cnt;
				RingBufRead(bw, bufkpi3, max_buffer_size, poss3, to_read);
				poss4 -= to_read;
			}
		}

		equaliser(bw, cnt2, reset);
		og->FeedPianoRoll(bw, cnt2);
		reset = FALSE;

		cnt4 = r;
		if (r == 0) cnt = 0;
		unsigned short* bf1, * bf2; bf1 = (unsigned short*)bw; bf2 = (unsigned short*)bufkpi2;
		//		int fw = playb % (wavchannel);
		//		bf2 += fw;
		int cnt1 = cnt / 2;
		Int24* b24c;
		b24c = (Int24*)bw;
		short* b, c;
		b = (short*)bw;
		if (wavsam_depth == 24) {
			for (int i = 0; i < cnt / 3; i++) {
				int c4 = b24c[i];
				if (savedata.mp3 == 2)	c4 = (int)((float)c4 * 2.0f);
				else if (savedata.mp3 == 4) c4 = (int)((float)c4 * 3.0f);
				else if (savedata.mp3 == 8) c4 = (int)((float)c4 * 4.0f);
				else if (savedata.mp3 == 16) c4 = (int)((float)c4 * 5.0f);
				if (c4 > 8388607)c4 = 8388607;
				if (c4 < -8388608)c4 = -8388608;
				b24c[i] = c4;
			}
		}
		else {
			for (int i = 0; i < cnt / 2; i++) {
				int c = (int)b[i];
				if (savedata.mp3 == 2)	c = (int)((float)b[i] * 1.5f);
				else if (savedata.mp3 == 3) c = (int)((float)b[i] * 2.0f);
				else if (savedata.mp3 == 4) c = (int)((float)b[i] * 2.5f);
				else if (savedata.mp3 == 5) c = (int)((float)b[i] * 3.0f);
				if (c >= 32768)c = 32767;
				if (c <= -32767)c = -32766;
				b[i] = (short)c;
			}
		}
		fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
		//fadeを三乗して計算密度を変更
		if (wavsam_depth == 24) {
			float c4;
			int c5;
			for (int i = 0; i < cnt / 3; i++) {
				c5 = b24c[i]; c4 = (float)c5;
				c4 = c4 * fade * fade; c5 = (int)c4;
				b24c[i] = c5;
			}
		}
		else {
			for (int i = 0; i < cnt / 2; i++) { c = b[i]; c = (short)(((float)c) * fade * fade); b[i] = c; }
		}
		//		if ((UINT)wl < (UINT)0x7fff0000) {
		//			if (cc1 == 1)	cc.Write(bw, cnt);
		//			wl += cnt;
		//		}
		lenl += cnt;
		//	playb+=cnt/4;
	}
	catch (SE_Exception e) {
	}
	catch (_EXCEPTION_POINTERS* ep) {
	}
	catch (...) {}
	return cnt;
}


int playwavdsd(BYTE* bw, int old, int l1, int l2)
{
	//データ読み込み
	if (l1 == 0) return 0;
	int rrr = readdsd(bw + old, l1);
	{
		const int bpf = PcmOutBytesPerFrame();
		playb += (l1 + l2) / bpf;
	}
	if (oggsize / ((wavchannel == 1) ? 2 : 1) - 192 * 20 <= (int)(playb * wavchannel * 2 * (wavsam_depth / 16.0))) {
		if (savedata.saveloop == FALSE) {
			l1 = rrr;
			if (savedata.saverenzoku == 0)
				fade1 = 1;
			else
				endflg = 1;
			return l1;
		}
	}
	if (l1 != rrr) {
		if (savedata.saveloop == 0) {
			l1 = rrr;
			if (savedata.saverenzoku == 0)
				fade1 = 1;
			else
				endflg = 1;

		}
		else if (savedata.saveloop == 1) {
			loopcnt++;
			playb = loop1;
			dsd_.kpiSetPosition(og->kmp, 0);
			poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			reset = TRUE;
			readdsd(bw + old + rrr, l1 - rrr);
		}
		else {
			endflg = 1;
		}
	}
	if (l2) {
		rrr = readdsd(bw, l2);
		if (l2 != rrr) {
			if (savedata.saveloop == 0) {
				l2 = rrr;
				if (savedata.saverenzoku == 0)
					fade1 = 1;
				else
					endflg = 1;
			}
			else if (savedata.saveloop == 1) {
				loopcnt++;
				playb = loop1;
				dsd_.kpiSetPosition(og->kmp, 0);
				poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				reset = TRUE;
				readdsd(bw + rrr, (int)l2 - rrr);
			}
			else {
				endflg = 1;
			}
		}
	}
	return l1 + l2;
}
extern int sek;
extern int flg3;
int readdsd(BYTE* bw, int cnt)
{
	if (cnt == 0)return 0;
	if (IsPlaybackStopRequested() || !og || !og->kmp)
		return 0;
	//_set_se_translator(trans_func);
	DWORD cnt2 = (DWORD)cnt;
	DWORD r = 0;
	int len3 = 0, len4 = 0;
	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	const int dsdFrame = wavchannel * (wavsam_depth / 8);
	if (dsdFrame <= 0)
		return 0;
	if (poss4 <= cnt) {
		int dsdRbStallIters = 0;
		int fadeTailIters = 0;
		const int kDsdRbStallMax = 512;
		const int kFadeTailMax = 128;
		while (true) {
			if (IsPlaybackStopRequested() || !og->kmp)
				break;
			if(fade1 == 0)
				cnt3 = dsd_.kpiRender(og->kmp, (BYTE*)bufkpi, cnt / dsdFrame) * dsdFrame;

			if (fade1 == 1 && muon != 0) {
				r = cnt;
				ZeroMemory(bufkpi, r);
				muon--;
			}
			if (fade1 == 1 && muon == MUON) {
				ZeroMemory(bufkpi + r, cnt - r);
				r = cnt;
				muon--;
			}


			int len2 = readtempo(bufkpi, cnt);
			if (sek == 0) {
				if (cnt2 <= cnt3) {
					cnt3 -= cnt2;
					if (cnt3 != 0)	memcpy(bufkpi, bufkpi + cnt2, cnt3);
				}
			}

			if (len2 > 0) {
				dsdRbStallIters = 0;
				// 書き込み
				RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
				poss4 += len2;
			}
			if (len4 != 0) break;
			if (poss4 > cnt) break;
			if (fade1 == 1 && muon == 0) {
				if (len2 <= 0) break;
				if (++fadeTailIters >= kFadeTailMax) break;
			}
			if (len2 <= 0 && fade1 == 0 && poss4 <= cnt) {
				if (++dsdRbStallIters >= kDsdRbStallMax)
					break;
			}
		}
	}

	cnt2 = cnt;
	int to_read = cnt;
	if (len4 != 0) {
		cnt2 = len4;
		to_read = len4;
	}

	if (cnt2 > 0) {
		RingBufRead(bw, bufkpi3, max_buffer_size, poss3, to_read);
		poss4 -= to_read;
	}

	equaliser(bw, cnt2, reset);
	og->FeedPianoRoll(bw, cnt2);
	reset = FALSE;

	if (cc1 == 1)	cc.Write(bw, cnt2);
	wl += cnt2;


	return cnt;
}

// readmp3 はテンポ伸縮で 1 回あたり要求バイトに満たないことがある。それを「EOF」と誤判定すると早めに fade1 になりポコ＋0.5〜1秒手前で止まる
static int ReadMp3Accumulate(BYTE* dst, int wantBytes)
{
	int got = 0;
	int guard = 0;
	const int maxIters = 8192;
	while (got < wantBytes && guard++ < maxIters) {
		if (IsPlaybackStopRequested())
			break;
		int n = readmp3(dst + got, wantBytes - got);
		if (n <= 0)
			break;
		got += n;
	}
	return got;
}

int readme = 0;
int playwavmp3(BYTE* bw, int old, int l1, int l2)
{
	// playb は readmp3 内で readtempo 出力バイト数(len2)から加算（readBuffwav と同じ）。先に l1/l2 で足すと伸縮と不一致になる。
	//データ読み込み
	int rrr = 0, rrr2 = 0;
	rrr = ReadMp3Accumulate(bw + old, l1);
	if (l1 != rrr) {
		if (savedata.saveloop == 0 && endf == 1) {
			if (savedata.saverenzoku == 0) {
				if(fade1 == 0) readme = rrr;
				fade1 = 1;
			}
			else	endflg = 1;

		}
		else {
			loopcnt++;
			playb = loop1;
			mp3_.seek(10, wavchannel); poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			ReadMp3Accumulate(bw + old + rrr, l1 - rrr);
		}
	}
	if (l2) {
		rrr2 = ReadMp3Accumulate(bw, l2);
		if (l2 != rrr2) {
			if (savedata.saveloop == 0 && endf == 1) {
				if (savedata.saverenzoku == 0) {
					if (fade1 == 0)readme = rrr + rrr2;
					fade1 = 1;
				}
				else endflg = 1;
			}
			else {
				loopcnt++;
				playb = loop1;
				mp3_.seek(10, wavchannel); poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				ReadMp3Accumulate(bw + rrr2, (int)l2 - rrr2);
			}
		}
	}
	return l1 + l2;
}

int playwavwav(BYTE* bw, int old, int l1, int l2)
{
	playb += (l1 + l2) / PcmOutBytesPerFrame();
	int rrr = readwav(bw + old, l1);
	if (l1 != rrr) {
		if (savedata.saveloop == 0 && endf == 1) {
			l1 = rrr;
			if (savedata.saverenzoku == 0) fade1 = 1;
			else endflg = 1;
		}
		else {
			loopcnt++;
			playb = loop1;
			wav_.Seek(loop1);
			poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			reset = TRUE;
			readwav(bw + old + rrr, l1 - rrr);
		}
	}
	if (l2) {
		rrr = readwav(bw, l2);
		if (l2 != rrr) {
			if (savedata.saveloop == 0 && endf == 1) {
				l2 = rrr;
				if (savedata.saverenzoku == 0) fade1 = 1;
				else endflg = 1;
			}
			else {
				loopcnt++;
				playb = loop1;
				wav_.Seek(loop1);
				poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				reset = TRUE;
				readwav(bw + rrr, (int)l2 - rrr);
			}
		}
	}
	return l1 + l2;
}

int readwav(BYTE* bw, int cnt)
{
	int r = cnt, rr = cnt;
	int cnt2;
	if (rr == 0) return 0;
	if (IsPlaybackStopRequested())
		return 0;
	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	const int bits = (int)wav_.m_info.wBitsPerSample;
	const int ch = (int)wav_.m_info.nChannels;
	if (bits <= 0 || ch <= 0)
		return 0;
	int bytesPerSample = ((bits + 7) / 8) * ch;
	int outBytesPerSample = (wavsam_depth / 8) * wavchannel;
	// 形式切替直後に depth/ch が未設定だと 0 除算する
	if (bytesPerSample <= 0 || outBytesPerSample <= 0)
		return 0;
	if (poss4 <= cnt) {
		int wavRbStallIters = 0;
		const int kWavRbStallMax = 512;
		while (true) {
			if (IsPlaybackStopRequested())
				break;
			int toRead = rr;
			if (bytesPerSample != outBytesPerSample) {
				toRead = (int)((__int64)rr * bytesPerSample / outBytesPerSample);
				toRead = (toRead / bytesPerSample) * bytesPerSample;
			}
			if (toRead <= 0)
				break;
			r = wav_.Render(bufkpi, toRead);
			// fade1 で break する前に readtempo しないと、曲末の実データがストレッチャを通らず欠ける
			int len2 = 0;
			if (bytesPerSample == outBytesPerSample) {
				len2 = readtempo(bufkpi, r);
			}
			else {
				BYTE convBuf[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];
				int convLen = 0;
				int inBps = bytesPerSample;
				int bpc = (wav_.m_info.wBitsPerSample + 7) / 8;
				int samples = r / inBps;
				for (int i = 0; i < samples; i++) {
					for (int ch = 0; ch < (int)wavchannel; ch++) {
						int val = 0;
						int off = i * inBps + ch * bpc;
						if (wav_.IsFloat()) {
							float f = *(float*)&bufkpi[off];
							val = (int)(f * (f >= 0 ? 32767.0f : 32768.0f));
						}
						else if (wav_.IsALaw()) {
							static const short alaw_table[256] = {
								-5504,-5248,-6016,-5760,-4480,-4224,-4992,-4736,-7552,-7296,-8064,-7808,-6528,-6272,-7040,-6784,
								-2752,-2624,-3008,-2880,-2240,-2112,-2496,-2368,-3776,-3648,-4032,-3904,-3264,-3136,-3520,-3392,
								-22016,-20992,-24064,-23040,-17920,-16896,-19968,-18944,-30208,-29184,-32256,-31232,-26112,-25088,-28160,-27136,
								-11008,-10496,-12032,-11520,-8960,-8448,-9984,-9472,-15104,-14592,-16128,-15616,-13056,-12544,-14080,-13568,
								-344,-328,-376,-360,-280,-264,-312,-296,-472,-456,-504,-488,-408,-392,-440,-424,
								-88,-72,-120,-104,-24,-8,-56,-40,-216,-200,-248,-232,-152,-136,-184,-168,
								-1376,-1312,-1504,-1440,-1120,-1056,-1248,-1184,-1888,-1824,-2016,-1952,-1632,-1568,-1760,-1696,
								-344,-328,-376,-360,-280,-264,-312,-296,-472,-456,-504,-488,-408,-392,-440,-424,
								5504,5248,6016,5760,4480,4224,4992,4736,7552,7296,8064,7808,6528,6272,7040,6784,
								2752,2624,3008,2880,2240,2112,2496,2368,3776,3648,4032,3904,3264,3136,3520,3392,
								22016,20992,24064,23040,17920,16896,19968,18944,30208,29184,32256,31232,26112,25088,28160,27136,
								11008,10496,12032,11520,8960,8448,9984,9472,15104,14592,16128,15616,13056,12544,14080,13568,
								344,328,376,360,280,264,312,296,472,456,504,488,408,392,440,424,
								88,72,120,104,24,8,56,40,216,200,248,232,152,136,184,168,
								1376,1312,1504,1440,1120,1056,1248,1184,1888,1824,2016,1952,1632,1568,1760,1696,
								344,328,376,360,280,264,312,296,472,456,504,488,408,392,440,424
							};
							val = alaw_table[bufkpi[off] & 0xFF];
						}
						else if (wav_.IsMuLaw()) {
							static const short mulaw_table[256] = {
								-32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,-23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
								-15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,-11900,-11388,-10876,-10364,-9852,-9340,-8828,-8316,
								-7932,-7676,-7420,-7164,-6908,-6652,-6396,-6140,-5884,-5628,-5372,-5116,-4860,-4604,-4348,-4092,
								-3900,-3772,-3644,-3516,-3388,-3260,-3132,-3004,-2876,-2748,-2620,-2492,-2364,-2236,-2108,-1980,
								-1884,-1820,-1756,-1692,-1628,-1564,-1500,-1436,-1372,-1308,-1244,-1180,-1116,-1052,-988,-924,
								-876,-844,-812,-780,-748,-716,-684,-652,-620,-588,-556,-524,-492,-460,-428,-396,
								-372,-356,-340,-324,-308,-292,-276,-260,-244,-228,-212,-196,-180,-164,-148,-132,
								-120,-112,-104,-96,-88,-80,-72,-64,-56,-48,-40,-32,-24,-16,-8,0,
								32124,31100,30076,29052,28028,27004,25980,24956,23932,22908,21884,20860,19836,18812,17788,16764,
								15996,15484,14972,14460,13948,13436,12924,12412,11900,11388,10876,10364,9852,9340,8828,8316,
								7932,7676,7420,7164,6908,6652,6396,6140,5884,5628,5372,5116,4860,4604,4348,4092,
								3900,3772,3644,3516,3388,3260,3132,3004,2876,2748,2620,2492,2364,2236,2108,1980,
								1884,1820,1756,1692,1628,1564,1500,1436,1372,1308,1244,1180,1116,1052,988,924,
								876,844,812,780,748,716,684,652,620,588,556,524,492,460,428,396,
								372,356,340,324,308,292,276,260,244,228,212,196,180,164,148,132,
								120,112,104,96,88,80,72,64,56,48,40,32,24,16,8,0
							};
							val = mulaw_table[bufkpi[off] & 0xFF];
						}
						else if (wav_.m_info.wBitsPerSample == 8) {
							val = ((int)bufkpi[off] - 128) * 256;
						}
						else if (wav_.m_info.wBitsPerSample == 16) {
							val = *(short*)&bufkpi[off];
						}
						else if (wav_.m_info.wBitsPerSample == 24) {
							val = (int)bufkpi[off] | ((int)bufkpi[off+1] << 8) | ((int)(signed char)bufkpi[off+2] << 16);
						}
						else if (wav_.m_info.wBitsPerSample == 32) {
							val = *(int*)&bufkpi[off] >> 8;
						}
						if (wavsam_depth == 16) {
							if (val > 32767) val = 32767;
							if (val < -32768) val = -32768;
							*(short*)&convBuf[convLen] = (short)val;
							convLen += 2;
						}
						else {
							if (val > 8388607) val = 8388607;
							if (val < -8388608) val = -8388608;
							convBuf[convLen++] = (BYTE)(val & 0xff);
							convBuf[convLen++] = (BYTE)((val >> 8) & 0xff);
							convBuf[convLen++] = (BYTE)((val >> 16) & 0xff);
						}
					}
				}
				if (convLen > 0) len2 = readtempo(convBuf, convLen);
			}
			if (len2 > 0) {
				wavRbStallIters = 0;
				RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
				poss4 += len2;
				if (rr > r) return r;
				if (poss4 > cnt) break;
			}
			if (rr > r) return r;
			if (fade1 == 1) {
				if (muon != 0) {
					if (savedata.saverenzoku == 0) { ZeroMemory(bufkpi, rr); muon--; }
					else endflg = 1;
				}
				break;
			}
			if (len2 <= 0 && r > 0 && r == rr) {
				if (++wavRbStallIters >= kWavRbStallMax)
					break;
			}
		}
	}
	cnt2 = (poss4 < cnt) ? poss4 : cnt;
	if (cnt2 > 0) {
		int to_read = cnt2;
		RingBufRead(bw, bufkpi3, max_buffer_size, poss3, to_read);
		poss4 -= to_read;
	}
	equaliser(bw, cnt2, reset);
	og->FeedPianoRoll(bw, cnt2);
	reset = FALSE;
	Int24* b24c = (Int24*)bw;
	short* b = (short*)bw;
	fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
	if (wavsam_depth == 24) {
		for (int i = 0; i < cnt2 / 3; i++) {
			float c4f; int c5;
			c5 = b24c[i]; c4f = (float)c5 * fade * fade; b24c[i] = (Int24)(int)c4f;
		}
	}
	else {
		for (int i = 0; i < cnt2 / 2; i++) {
			int c = b[i];
			c = (short)(((float)c) * fade * fade); b[i] = (short)c;
		}
	}
	if (cc1 == 1) cc.Write(bw, cnt2);
	wl += cnt2;
	lenl += cnt2;
	return cnt2;
}

int readmp3(BYTE* bw, int cnt)
{
	int r = cnt, rr = cnt;
	if (cnt == 0) return 0;
	EqualiserSetFormatVolContext(2, FALSE);

	int cnt2;
	int len3 = 0, len4 = 0;
	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	if (poss4 <= cnt) {
		// シーク直後など RB 初期レイテンシで「入力はあるが len2==0」が続く。break すると return 0 になり再生停止するので continue で足す
		int mp3RbStallIters = 0;
		int fadeTailIters = 0;
		const int kMp3RbStallMax = 512;
		const int kFadeTailMax = 128;
		while (true) {
			if (IsPlaybackStopRequested())
				break;
			if (savedata.mp3orig)
				r = mp3_.Render2(bufkpi, rr, kbps);
			else
				r = mp3_.Render(bufkpi, rr);

			// 先にテンポ処理。fade1 で ZeroMemory してからだと最終デコードPCMを捨てる
			int len2 = readtempo(bufkpi, r);

			if (fade1 == 1) {
				if (muon != 0) {
					if (savedata.saverenzoku == 0) {
						ZeroMemory(bufkpi, max_buffer_size);
						muon--;
					}
					else {
						endflg = 1;
						break;
					}
				}
			}

			if (len2 > 0) {
				mp3RbStallIters = 0;
				// 書き込み
				RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
				poss4 += len2;
				// playb はここでは進めない。len2 を積むと「今回 bw に渡す cnt バイト」より多く数えてしまい（例: ~1.5 倍）、表示時間が実長より長くなる。
				// ここで return r すると bw へ未コピーのまま戻り、r はデコードバイトで伸縮後バイトと単位が違う → playwavmp3 が早 EOF・fade1 誤発火・ポコ音
				if (rr > r) break;
				if (poss4 > cnt) break;
			}
			// readtempo が 0 のときは上の if (len2>0) に入らず、ここに来る。
			// r<rr（デコード欠け／EOF）で抜けないと while(true) が無限ループする
			if (rr > r) break;
			if (len2 <= 0 && r == 0) break;
			if (fade1 == 1 && muon == 0) {
				if (len2 <= 0) break;
				if (++fadeTailIters >= kFadeTailMax) break;
			}
			// フルブロック decode 済みだが RB がまだ出さない → 追加デコードで埋める（シーク直後のレイテンシ対策）
			if (len2 <= 0 && r > 0 && rr == r) {
				if (++mp3RbStallIters >= kMp3RbStallMax)
					break;
				continue;
			}
		}
	}

	// リングに溜めた伸縮後データを bw へ。部分デコード時は poss4 < cnt になり得る
	int to_read = cnt;
	if (poss4 < cnt)
		to_read = poss4;
	cnt2 = to_read;

	if (cnt2 > 0) {
		RingBufRead(bw, bufkpi3, max_buffer_size, poss3, cnt2);
		poss4 -= cnt2;
		{
			const int bpf = PcmOutBytesPerFrame();
			if (bpf > 0 && cnt2 > 0)
				playb += cnt2 / bpf;
		}
	}
	else {
		return 0;
	}

	equaliser(bw, cnt2, reset);
	og->FeedPianoRoll(bw, cnt2);
	reset = FALSE;


	Int24* b24c;
	b24c = (Int24*)bw;
	short* b, c;
	b = (short*)bw;
	fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
	if (wavsam_depth == 24) {
		for (int i = 0; i < cnt2 / 3; i++) {
			float c4;
			int c5;
			c5 = b24c[i]; c4 = (float)c5;
			c4 = c4 * fade * fade; c5 = (int)c4;
			b24c[i] = c5;
		}
	}
	else {
		for (int i = 0; i < cnt2 / 2; i++) {
			c = b[i]; c = (short)(((float)c) * fade * fade); b[i] = c;
		}
	}

	if (cc1 == 1)	cc.Write(bw, cnt2);
	wl += cnt2;

	lenl += cnt2;
	//	playb+=cnt;
	return cnt2;
}
BOOL oggyomikomi = FALSE;


extern std::vector<float> inputFloatData;
extern std::vector<uint8_t> m_bufwav3_1;
extern std::vector<float> m_convertedPcmFloatData;
extern int pitch;
extern int tempo; // tempo変数を参照します
float tempoRate2;

// ─────────────────────────────────────────────────────────────────────
// スライダーでのシーク時や、ループ時にRubberBandを温めながらジャンプする万能関数
// 修正版：引数の不整合を解消し、再生位置の同期を最適化しました
// ─────────────────────────────────────────────────────────────────────
bool InitializeRubberBandStretcher();

void SeekAndWarmupRubberBand(int targetPos, bool loopJump)
{
	const int bpfWarm = PcmOutBytesPerFrame();
	if (bpfWarm <= 0)
		return;

	// 44.1kHz ループ: playwavds 互換（RB/リングを触らず seek のみ）
	if (loopJump && !OggUseLowRateLoopExtras()) {
		g_loopTailBuffer.clear();
		g_loopTailPos = 0;
		ov_pcm_seek_lap(&vf, (ogg_int64_t)targetPos);
		playb = targetPos;
		g_oggPcmDecodePos = targetPos;
		poss5 = targetPos;
		poss = 0;
		poss6 = 0;
		g_oggRbPrimingNeed = 0;
		reset = FALSE;
		return;
	}

	ResetAudioUpscalerPipeline();
	g_loopTailBuffer.clear();
	g_loopTailPos = 0;
	OggFlushKpi3Ring();
	poss = 0;
	poss6 = 0;
	const int ovChunkBytes = 4096;
	const int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;

	float te = (float)tempo;
	if (te >= 200.0f) te -= 100.0f;
	else te = te / 3.0f + 33.3f;
	tempoRate2 = te / 100.0f;

	if (!g_rubberBandStretcher) {
		if (!InitializeRubberBandStretcher()) return;
	}
	else {
		g_rubberBandStretcher->reset();
	}

	// 助走長はサンプルレートに比例（32k でも 44.1k と同じ約 186ms）
	int preRollInputSamples = 8192;
	if (wavbit_sample_Hz > 0 && wavbit_sample_Hz != 44100)
		preRollInputSamples = (int)(8192.0 * (double)wavbit_sample_Hz / 44100.0 + 0.5);
	if (preRollInputSamples < 4096)
		preRollInputSamples = 4096;

	int startPos = targetPos - preRollInputSamples;
	if (startPos < 0) {
		preRollInputSamples = targetPos;
		startPos = 0;
	}

	// ── Phase 1: [startPos, targetPos) のみ RB に投入。出力はすべて破棄 ──
	// loop1 以降を読むと seek 後に二重入力になるため、絶対に targetPos を越えない。
	ov_pcm_seek_lap(&vf, (ogg_int64_t)startPos);

	uint16_t bps = (uint16_t)((wavsam_depth <= 0 || wavsam_depth > 32) ? 16 : abs(wavsam_depth));
	std::vector<uint8_t> tempRawBuf((size_t)ovChunkBytes);
	const size_t pullSize = 4096;
	std::vector<std::vector<float>> outBuf(wavchannel, std::vector<float>(pullSize));
	std::vector<float*> outPtrs(wavchannel);
	for (int ch = 0; ch < wavchannel; ++ch)
		outPtrs[ch] = outBuf[ch].data();

	int inputFed = 0;
	while (inputFed < preRollInputSamples) {
		if (IsPlaybackStopRequested())
			break;
		const int remainBeforeTarget = targetPos - startPos - inputFed;
		if (remainBeforeTarget <= 0)
			break;

		long readBytes = ovChunkBytes;
		const int maxChunkBytes = remainBeforeTarget * bpfWarm;
		if (maxChunkBytes > 0 && readBytes > maxChunkBytes)
			readBytes = maxChunkBytes;

		int current_section;
		long bytesRead = ov_read(&vf, (char*)tempRawBuf.data(), readBytes, 0, 2, 1, &current_section);
		if (bytesRead <= 0)
			break;

		int samplesRead = (int)(bytesRead / bpfWarm);
		if (samplesRead > remainBeforeTarget)
			samplesRead = remainBeforeTarget;
		if (samplesRead <= 0)
			break;

		std::vector<uint8_t> chunk(tempRawBuf.begin(), tempRawBuf.begin() + samplesRead * bpfWarm);
		std::vector<float> inFloat;
		ConvertRawBytesToFloat(chunk, bps, wavchannel, inFloat);

		std::vector<std::vector<float>> chData(wavchannel, std::vector<float>((size_t)samplesRead));
		for (int i = 0; i < samplesRead; ++i) {
			for (int ch = 0; ch < wavchannel; ++ch)
				chData[ch][i] = inFloat[(size_t)i * (size_t)wavchannel + (size_t)ch];
		}
		std::vector<float*> chPtrs(wavchannel);
		for (int ch = 0; ch < wavchannel; ++ch)
			chPtrs[ch] = chData[ch].data();

		g_rubberBandStretcher->process(chPtrs.data(), (size_t)samplesRead, false);
		inputFed += samplesRead;

		while (g_rubberBandStretcher->available() > 0) {
			if (IsPlaybackStopRequested())
				break;
			size_t toGet = (std::min)((size_t)g_rubberBandStretcher->available(), pullSize);
			if (g_rubberBandStretcher->retrieve(outPtrs.data(), toGet) == 0)
				break;
		}
	}

	// ── Phase 2: ループ先頭へ seek ──
	ov_pcm_seek_lap(&vf, (ogg_int64_t)targetPos);
	g_oggPcmDecodePos = targetPos;

	// ── Phase 3: 32kHz 等のみリングプリフィル（44.1kHz はシームレスループでダブりになる）
	const int primingTarget = OggRbLatencyReserveBytes();
	if (OggUseLowRateLoopExtras()) {
		int maxBufSamples = (int)(sizeof(bufwav) / (size_t)bpfWarm);
		int safeFillSamples = maxBufSamples - (ovChunkBytes / bpfWarm) - 1;
		if (safeFillSamples < 1)
			safeFillSamples = 1;

		const bool prevWarmup = g_inWarmup;
		g_inWarmup = true;
		g_warmupRBOutputBytes = 0;

		int prefillStall = 0;
		const int kPrefillStallMax = 8192;
		while (poss4 < primingTarget) {
			if (IsPlaybackStopRequested())
				break;

			int ret = 0;
			for (;;) {
				if (IsPlaybackStopRequested())
					break;
				if (poss < 0 || poss > safeFillSamples)
					poss = 0;
				if (poss >= safeFillSamples)
					break;

				long readBytes = ovChunkBytes;
				const int roomSamples = safeFillSamples - poss;
				const int maxChunkBytes = roomSamples * bpfWarm;
				if (maxChunkBytes > 0 && readBytes > maxChunkBytes)
					readBytes = maxChunkBytes;

				int current_section;
				long bytesRead = ov_read(&vf, (char*)(bufwav + poss * bpfWarm), readBytes, 0, 2, 1, &current_section);
				if (bytesRead <= 0) {
					ret = 0;
					break;
				}
				ret = (int)(bytesRead / bpfWarm);
				if (ret <= 0)
					break;
				poss += ret;
				if (poss >= safeFillSamples / 2)
					break;
			}

			int validSamples = poss;
			if (validSamples > safeFillSamples)
				validSamples = safeFillSamples;
			if (validSamples <= 0) {
				if (++prefillStall >= kPrefillStallMax)
					break;
				if (ret == 0)
					break;
				continue;
			}

			int len2 = readtempo(bufwav, validSamples * bpfWarm);
			g_oggPcmDecodePos += validSamples;
			poss = 0;

			if (len2 > 0) {
				prefillStall = 0;
				RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
				poss4 += len2;
				g_warmupRBOutputBytes += len2;
				SanitizeKpi3RingState(max_buffer_size);
			}
			else if (ret > 0) {
				if (++prefillStall >= kPrefillStallMax)
					break;
				continue;
			}
			else {
				break;
			}
		}

		g_inWarmup = prevWarmup;
		poss = 0;
		poss3 = 0;
		g_oggRbPrimingNeed = 0;
	}
	else {
		poss = 0;
		poss3 = 0;
		// 44.1kHz: リングは空のまま mcopy のプライミング待ちで RB レイテンシを吸収
		g_oggRbPrimingNeed = primingTarget;
	}

	playb = targetPos;
	poss5 = targetPos;
	// ループ時 EQ 強制 InitEngine は境界ポツ音の一因になる（フラット時も InitEngine が走る）
	reset = FALSE;
}

void playwavds2(BYTE* bw, int old, int l1, int l2)
{
	//データ読み込み
	if (l1 == 0)return;
	oggyomikomi = TRUE;
	int rrr = McopyAccumulate((char*)bw + old, l1);

	if (l1 != rrr) {
		if (savedata.saveloop == 0 && endf == 1) {
			l1 = rrr; fade1 = 1;
		}
		else {
			loopcnt++;
			if (OggUseLowRateLoopExtras()) {
				OggFlushKpi3Ring();
				poss = 0;
			}
			else {
				poss = 0;
			}
			SeekAndWarmupRubberBand(loop1, true);
			{
				const int got2 = McopyAccumulate((char*)bw + old + rrr, (int)l1 - rrr);
				if (rrr > 0 && got2 > 0)
					OggApplyLoopBoundaryCrossfade((char*)bw + old, rrr, got2);
			}
		}
	}
	if (l2) {
		rrr = McopyAccumulate((char*)bw, l2);
		if (l2 != rrr) {
			if (savedata.saveloop == 0 && endf == 1) {
				l2 = rrr; fade1 = 1;
			}
			else {
				loopcnt++;
				if (OggUseLowRateLoopExtras()) {
					OggFlushKpi3Ring();
					poss = 0;
				}
				else {
					poss = 0;
				}
				SeekAndWarmupRubberBand(loop1, true);
				{
					const int got2 = McopyAccumulate((char*)bw + rrr, (int)l2 - rrr);
					if (rrr > 0 && got2 > 0)
						OggApplyLoopBoundaryCrossfade((char*)bw, rrr, got2);
				}
			}
		}
	}
	oggyomikomi = FALSE;
	return;
}

void playwavds(BYTE* bw)
{
	//データ読み込み
	loc++;
	if (loc == OUTPUT_BUFFER_NUM) loc = 0;
	lo++;
	if (lo == OUTPUT_BUFFER_NUM) lo = 0;
	DWORD dwDataLen = OUTPUT_BUFFER_SIZE;
	int rrr = mcopy((char*)buf[lo], dwDataLen);
	if ((int)dwDataLen != rrr)
	{
		if (savedata.saveloop == 0 && endf == 1)
		{
			dwDataLen = rrr;
			if (savedata.saverenzoku == 0)
				fade1 = 1;
			else
				endflg = 1;
		}
		else {
			loopcnt++;
			playb = loop1;
			ov_pcm_seek_lap(&vf, (ogg_int64_t)loop1); poss = 0;
			mcopy((char*)buf[lo] + rrr, (int)dwDataLen - rrr);
		}
	}
	memcpy(bw, buf[lo], dwDataLen);
}

void playwav()
{
	//データ読み込み
	lo++;
	if (lo == OUTPUT_BUFFER_NUM) lo = 0;
	DWORD dwDataLen = OUTPUT_BUFFER_SIZE;
	int rrr = mcopy((char*)buf[lo], dwDataLen);
	if ((int)dwDataLen != rrr)
	{
		if (endf == 1)
		{
			dwDataLen = rrr;
			stf = 1;
		}
		else {
			loopcnt++;
			playb = loop1;
			ov_pcm_seek_lap(&vf, (ogg_int64_t)loop1); poss = 0;
			mcopy((char*)buf[lo] + rrr, (int)dwDataLen - rrr);
		}
	}

	memcpy(g_OutputBuffer[lo]->lpData, buf[lo], dwDataLen);
}

LRESULT COggDlg::dp1(WPARAM a, LPARAM b) {
	// KPI 読み込み中(OnInitDialog 内)の再生開始は禁止。ネスト play() で
	// 読み込みが固まり、初期化未完了のまま裏で再生が走る。完了後に再ポストする。
	if (g_pActiveLoadingWnd != NULL) {
		g_kpiLoadDeferredPlay = TRUE;
		return 0;
	}
	dp(filen);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
//複数起動時の方から飛んでいるデータ
BOOL COggDlg::OnCopyData(CWnd* pWnd, COPYDATASTRUCT* pCopyDataStruct)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	CString filen_;
	TCHAR* aa = (TCHAR*)pCopyDataStruct->lpData;
	filen_ = aa;
	// KPI 読み込み中(OnInitDialog 内)は再生・画面操作系コマンドを実行しない。
	// ファイルパスの受け取りだけ行い、再生は後続の WM_APP+1(dp1 で遅延)に任せる。
	if (g_pActiveLoadingWnd != NULL) {
		if (filen_.Left(1) != _T("*"))
			filen = filen_;
		return CCustomBlurDialogBase::OnCopyData(pWnd, pCopyDataStruct);
	}
	if (filen_ == "*1") OnRestart();
	else if (filen_ == "*2") OnPause();
	else if (filen_ == "*3") stop();
	else if (filen_ == "*4") OnPlayList();
	else if (filen_ == "*5") OnButton21();
	else if (filen_ == "*6") OnButton9_Folder();
	else if (filen_ == "*7") {
		if (savedata.playerMode == 1)
			MpTaskbarNextTrack();
	}
	else if (filen_ == "*8") {
		if (savedata.playerMode == 1)
			MpTaskbarPrevTrack();
	}
	else if (filen_ == "*9") {
		if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_EQ, BN_CLICKED), 0);
		else
			SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON59, BN_CLICKED), 0);
	}
	else if (filen_ == "*A") {
		if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_JACK, BN_CLICKED), 0);
		else
			OnBnmp3jake();
	}
	else if (savedata.playerMode == 1 && MpPlayExistingPlaylistPath(filen_))
		;
	else
		filen = filen_;
	return CCustomBlurDialogBase::OnCopyData(pWnd, pCopyDataStruct);
}

void COggDlg::dp(CString a)
{
	if (a.Left(1) == "*") {
		if (a == "*1") OnRestart();
		else if (a == "*2") OnPause();
		else if (a == "*3") stop();
		else if (a == "*4") OnPlayList();
		else if (a == "*5") OnButton21();
		else if (a == "*6") OnButton9_Folder();
		else if (a == "*7") {
			if (savedata.playerMode == 1)
				MpTaskbarNextTrack();
		}
		else if (a == "*8") {
			if (savedata.playerMode == 1)
				MpTaskbarPrevTrack();
		}
		else if (a == "*9") {
			if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
				mp->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_EQ, BN_CLICKED), 0);
			else
				SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON59, BN_CLICKED), 0);
		}
		else if (a == "*A") {
			if (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()))
				mp->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_JACK, BN_CLICKED), 0);
			else
				OnBnmp3jake();
		}
		return;
	}
	filen = a;
	if (filen.Left(1) == "\"") filen = filen.Right(filen.GetLength() - 1);
	if (filen.Right(1) == "\"") filen = filen.Left(filen.GetLength() - 1);
	if (savedata.playerMode == 1 && MpPlayExistingPlaylistPath(filen))
		return;
	ti = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
	stop1();
	if (filen.Right(5).MakeLower() == ".opus") {
		fnn = ti;
		mode = -6; modesub = -6;
		playb = 0;
		play();
	}
	else if (filen.Right(4).MakeLower() == ".ogg" || filen.Right(4) == ".OGG" || filen.Right(6).MakeLower() == ".qull3") {
		fnn = ti;
		mode = -1; modesub = -1;
		play();
	}
	else if (filen.Right(5).MakeLower() == ".flac" || filen.Right(5) == ".FLAC" || filen.Right(7).MakeLower() == L".qull3h") {
		fnn = ti;
		mode = -8; modesub = -8;
		play();
	}
	else if ((filen.Right(4).MakeLower() == ".dsf" || filen.Right(5) == ".DSF" || filen.Right(4).MakeLower() == ".dff" || filen.Right(4) == ".DFF" || filen.Right(4).MakeLower() == ".wsd" || filen.Right(4) == ".WSD")) {
		fnn = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		mode = -7; modesub = -7;
		play();
	}
	else if (filen.Right(4).MakeLower() == ".m4a" || filen.Right(4) == ".M4A" || filen.Right(4).MakeLower() == ".aac" || filen.Right(4) == ".AAC") {
		fnn = ti;
		mode = -9; modesub = -9;
		play();
	}
	else if ((filen.Right(4).MakeLower() == ".mp3" || filen.Right(4) == ".MP3" || filen.Right(4).MakeLower() == ".mp2" || filen.Right(4) == ".MP2" ||
		filen.Right(4).MakeLower() == ".mp1" || filen.Right(4) == ".MP1" || filen.Right(4).MakeLower() == ".rmp" || filen.Right(4) == ".RMP")) {
		fnn = ti;
		mode = -10; modesub = -10;
		play();
	}
	else if (filen.Right(4).MakeLower() == ".wav" || filen.Right(4) == ".WAV") {
		fnn = ti;
		mode = 999; modesub = 999;
		play();
	}
	else if (filen.Find(L".pac::") > 0 && filen.Find(L"Trails in the Sky 1st Chapter")) {
		mode = 30; modesub = 30;
		play();
	}
	else {//DirectShow
		stflg = FALSE;
		CFile ff;
		CString ss11 = filen; ss11.MakeLower();
		if (ss11.Right(3) == "m4a") {
			if (ff.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
				mp3file = filen;
				ZeroMemory(bufimage, sizeof(bufimage));
				int i;
				ff.Read(bufimage, sizeof(bufimage));
				for (i = 0; i < 0x300000; i++) {// 00 06 5D 6A 64 61 74 61 // 63 6f 76 72 xx xx xx xx 64 61 74 61
					if (bufimage[i] == 0x63 && bufimage[i + 1] == 0x6f && bufimage[i + 2] == 0x76 && bufimage[i + 3] == 0x72 && bufimage[i + 8] == 0x64 && bufimage[i + 9] == 0x61 && bufimage[i + 10] == 0x74 && bufimage[i + 11] == 0x61) {
						break;
					}
				}
				m_mp3jake.EnableWindow(FALSE);
				if (i != 0x300000) {
					m_mp3jake.EnableWindow(TRUE);
				}
			}ff.Close();
		}
		playlistdata p;
		if (mode == 21) {
			play();
			return;
		}
		if (pl && plw) {
			p.sub = 0;
			CString ss, s;
			s = filen;
			{
				uint32_t sel = 1;
				SplitKpiSubsongPath(s, ss, sel);
			}
			if (ss != "") s = ss;
			kpi[0] = 0;
			pl->plugs(s, &p, kpi, kvver);
			if (p.sub == -3) {//kb medua player
				//				hDLLk = LoadLibrary(kpi);
				//				pFunck = (pfnGetKMPModule)::GetProcAddress(hDLLk, "kmp_GetTestModule");
				mode = -3;
				modesub = -3;
				play();
				return;
			}
		}
		fnn = ti;
		mode = -2; modesub = -2;
		pMainFrame1 = new CDouga;
		pMainFrame1->Create(GetSafeHwnd());
		pMainFrame1->ShowWindow(SW_HIDE);
		pMainFrame1->play(0);
		CFile f123;
		int flggg = 0;
		if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
			f123.Close();
			if (IDYES == MessageBox(LL14(
				L"途中再生データが存在します。\n前回中断した部分から再生しますか？\nはい = 途中から再生\nいいえ = はじめから再生", /* 日本語 */
				L"Resume data exists.\nResume from where you left off?\nYes = Resume\nNo = Play from start", /* 英語 */
				L"Des données de reprise existent.\nReprendre là où vous vous êtes arrêté ?\nOui = Reprendre\nNon = Jouer depuis le début", /* フランス語 */
				L"Esistono dati di ripresa.\nRiprendere da dove ci si è fermati?\nSì = Riprendi\nNo = Riproduci dall'inizio", /* イタリア語 */
				L"Existen datos de reanudación.\n¿Reanudar desde donde lo dejó?\nSí = Reanudar\nNo = Reproducir desde el inicio", /* スペイン語 */
				L"중간 재생 데이터가 존재합니다.\n지난번 중단한 부분부터 재생하시겠습니까?\n예 = 중간부터 재생\n아니요 = 처음부터 재생", /* 韓国語 */
				L"存在中途播放数据。\n是否从上次中断处播放？\n是 = 从中途播放\n否 = 从头播放", /* 中国語 */
				L"بيانات الاستئناف موجودة.\nهل تريد الاستئناف من حيث توقفت؟\nنعم = استئناف\nلا = تشغيل من البداية", /* アラビア語 */
				L"Данные возобновления существуют.\nПродолжить с места остановки?\nДа = Продолжить\nНет = Играть с начала", /* ロシア語 */
				L"Fortsetzungsdaten vorhanden.\nVon der Unterbrechungsstelle fortfahren?\nJa = Fortsetzen\nNein = Von Anfang abspielen", /* ドイツ語 */
				L"Dados de retomada existem.\nRetomar de onde parou?\nSim = Retomar\nNão = Reproduzir do início", /* ポルトガル語 */
				L"Hervatgegevens aanwezig.\nHervatten waar u gebleven was?\nJa = Hervatten\nNee = Afspelen vanaf het begin", /* オランダ語 */
				L"Istnieją dane wznowienia.\nWznowić od miejsca przerwania?\nTak = Wznów\nNie = Odtwórz od początku", /* ポーランド語 */
				L"Devam verisi mevcut.\nKaldığınız yerden devam edilsin mi?\nEvet = Devam et\nHayır = Baştan oynat"), /* トルコ語 */
				LL14(
					L"再生確認", /* 日本語タイトル */
					L"Playback confirmation",
					L"Confirmation de lecture",
					L"Conferma riproduzione",
					L"Confirmación de reproducción",
					L"재생 확인",
					L"播放确认",
					L"تأكيد التشغيل",
					L"Подтверждение воспроизведения",
					L"Wiedergabebestätigung",
					L"Confirmação de reprodução",
					L"Afspeelbevestiging",
					L"Potwierdzenie odtwarzania",
					L"Oynatma onayı"), /* トルコ語タイトル */
				MB_YESNO)) {
				flggg = 1;
			}
			else {
				CFile::Remove(filen + _T(".save"));
			}
		}
		if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE && flggg == 1) {
			f123.Close();
			if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
			if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
			if (mode == -10) {
				if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
					f123.Read(&playb, sizeof(__int64));
					if (oggsize > 0 && playb > (__int64)oggsize)
						playb /= 4;
					if (savedata.mp3orig) {
						mp3_.seek2(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
					}
					else {
						mp3_.seek(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
					}
					f123.Close();
				}
			}
			if (mode == -2) {
				if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
					f123.Read(&aa1_, sizeof(double));
					pMainFrame1->seek((LONGLONG)(((float)((float)aa1_ * 100.0f * 100000.0f))));
					f123.Close();
				}
			}
		}
		else {
			if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
			if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
			if (pMainFrame1) { pMainFrame1->seek(0); }
		}
		//		if(pGraphBuilder)pMainFrame1->plays2();
		//		if(pMediaControl)pMediaControl->Run();
		int a = 0; aa2 = 0;
		REFTIME aa = 0;
		aa2 = 0;
		ResetPauseButtonUi();
		if (pMediaPosition)pMediaPosition->get_StopTime(&aa);
		aa1 = oggsize2 = aa;
		m_time.SetRange(0, (int)((REFTIME)aa * 100.0), TRUE);
		m_time.SetSelection(0, (int)((REFTIME)aa * 100.0) - 1);
		m_time.Invalidate();
		if (pl && plw) {
			int plc;
			plc = pl->Add(fnn, mode, 0, 0, _T(""), _T(""), filen, 0, (int)aa, 1);
			if (plc == -1) {
				int i = pl->m_lc.GetItemCount() - 1;
				plcnt = i;
				pl->SIcon(i);
			}
			else {
				plcnt = plc;
				pl->SIcon(plc);
			}
			pl->Save();
		}

		TCHAR tmp_savedir[1024];
		_tgetcwd(tmp_savedir, 1000);
		_tchdir(karento2);
		CFile ab;
#if _UNICODE
		if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
		if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
			ab.Write(&savedata, sizeof(save));
			ab.Close();
		}
		_tchdir(tmp_savedir);

		plf = 1;
		SongParams_OnSongStarted();
	}

}

void COggDlg::OnDropFiles(HDROP hDropInfo)
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	// プレイリストが存在すれば(表示/非表示問わず)そちらへ追加し、両方のリストビューを整合させる。
	// メディアプレイヤーモードでは pl は裏で生きているのでここに入る。
	if (pl && ::IsWindow(pl->GetSafeHwnd())) {
		pl->OnDropFiles(hDropInfo);
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->RefreshList(TRUE);
		return;
	}

	TCHAR filen_c[1024];
	UINT cnt = DragQueryFile(hDropInfo, (UINT)-1, filen_c, sizeof(filen_c));
	if (cnt != 1)
	{
		MessageBox(LL14(
			L"ファイルは1つだけドロップしてください。\nプレイリストが開いている時は複数でもokです。", /* 日本語 */
			L"Please drop only one file.\nMultiple files are okay when the playlist is open.", /* 英語 */
			L"Veuillez ne déposer qu'un seul fichier.\nPlusieurs fichiers sont acceptés si la liste de lecture est ouverte.", /* フランス語 */
			L"Rilascia un solo file.\nPiù file sono accettati quando la playlist è aperta.", /* イタリア語 */
			L"Suelte solo un archivo.\nSe aceptan varios archivos cuando la lista de reproducción está abierta.", /* スペイン語 */
			L"파일은 하나만 드롭해 주세요.\n플레이리스트가 열려 있을 때는 여러 개라도 ok입니다.", /* 韓国語 */
			L"请只投放一个文件。\n播放列表打开时可以投放多个文件。", /* 中国語 */
			L"يرجى سحب وإفلات ملف واحد فقط.\nيمكن إفلات ملفات متعددة عندما تكون قائمة التشغيل مفتوحة.", /* アラビア語 */
			L"Перетащите только один файл.\nНесколько файлов допустимо, когда плейлист открыт.", /* ロシア語 */
			L"Bitte nur eine Datei ablegen.\nMehrere Dateien sind erlaubt, wenn die Wiedergabeliste geöffnet ist.", /* ドイツ語 */
			L"Solte apenas um arquivo.\nVários arquivos são aceitos quando a lista de reprodução está aberta.", /* ポルトガル語 */
			L"Zet slechts één bestand neer.\nMeerdere bestanden zijn toegestaan als de afspeellijst open is.", /* オランダ語 */
			L"Upuść tylko jeden plik.\nWiele plików jest dozwolonych, gdy lista odtwarzania jest otwarta.", /* ポーランド語 */
			L"Yalnızca bir dosya bırakın.\nOynatma listesi açıkken birden fazla dosya kabul edilir."), /* トルコ語 */
			LL14(
				L"ogg/wav簡易プレイヤ", /* 日本語タイトル */
				L"ogg/wav Simple Player",
				L"Lecteur Simple ogg/wav",
				L"Lettore Semplice ogg/wav",
				L"Reproductor Simple ogg/wav",
				L"ogg/wav 간편 플레이어",
				L"ogg/wav 简易播放器",
				L"مشغل ogg/wav البسيط",
				L"Простой Плеер ogg/wav",
				L"ogg/wav Einfacher Player",
				L"Player Simples ogg/wav",
				L"Eenvoudige ogg/wav Speler",
				L"Prosty Odtwarzacz ogg/wav",
				L"ogg/wav Basit Oynatıcı"), /* トルコ語タイトル */
			MB_ICONEXCLAMATION); CCustomBlurDialogBase::OnDropFiles(hDropInfo);
		return;
	}
	DragQueryFile(hDropInfo, (UINT)0, filen_c, sizeof(filen_c));
	CString ff;
	ff = filen;
	filen = (CString)filen_c;
	CFile f;
	if (f.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) == FALSE) {
		filen = ff;
		MessageBox(LL14(
			L"ほかのプログラムで開かれているためファイルが開けません", /* 日本語 */
			L"Cannot open file because it is being used by another program.", /* 英語 */
			L"Impossible d'ouvrir le fichier car il est utilisé par un autre programme.", /* フランス語 */
			L"Impossibile aprire il file perché è utilizzato da un altro programma.", /* イタリア語 */
			L"No se puede abrir el archivo porque otro programa lo está usando.", /* スペイン語 */
			L"다른 프로그램에서 사용 중이므로 파일을 열 수 없습니다.", /* 韓国語 */
			L"文件无法打开，因为其他程序正在使用它。", /* 中国語 */
			L"لا يمكن فتح الملف لأنه قيد الاستخدام من قبل برنامج آخر.", /* アラビア語 */
			L"Невозможно открыть файл, так как он используется другой программой.", /* ロシア語 */
			L"Datei kann nicht geöffnet werden, da sie von einem anderen Programm verwendet wird.", /* ドイツ語 */
			L"Não é possibile abrir o arquivo pois outro programa o está usando.", /* ポルトガル語 */
			L"Kan het bestand niet openen omdat een ander programma het in gebruik heeft.", /* オランダ語 */
			L"Nie można otworzyć pliku, ponieważ jest używany przez inny program.", /* ポーランド語 */
			L"Dosya başka bir program tarafından kullanıldığı için açılamıyor."), /* トルコ語 */
			LL14(
				L"ogg/wav簡易プレイヤ", /* 日本語タイトル */
				L"ogg/wav Simple Player",
				L"Lecteur Simple ogg/wav",
				L"Lettore Semplice ogg/wav",
				L"Reproductor Simple ogg/wav",
				L"ogg/wav 간편 플레이어",
				L"ogg/wav 简易播放器",
				L"مشغل ogg/wav البسيط",
				L"Простой Плеер ogg/wav",
				L"ogg/wav Einfacher Player",
				L"Player Simples ogg/wav",
				L"Eenvoudige ogg/wav Speler",
				L"Prosty Odtwarzacz ogg/wav",
				L"ogg/wav Basit Oynatıcı"), /* トルコ語タイトル */
			MB_ICONEXCLAMATION);
		CCustomBlurDialogBase::OnDropFiles(hDropInfo);
		return;
	}
	f.Close();
	dp(filen);
	CCustomBlurDialogBase::OnDropFiles(hDropInfo);
}

BOOL CALLBACK pp(HWND hwnd, LPARAM p);
BOOL CALLBACK pp(HWND hwnd, LPARAM p)
{
	DWORD pid;
	GetWindowThreadProcessId(hwnd, &pid);
	if (pid == (LPARAM)p) {
		::PostMessage(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
		return FALSE;
	}
	return TRUE;
}

CString filenback;

void COggDlg::SyncPauseButtonUi()
{
	if (!m_ps.GetSafeHwnd())
		return;
	m_ps.EnableWindow(TRUE);
	m_ps.RepaintClient();
	// メディアプレイヤーモード: 裏側 og の一時停止状態を mp へ反映(短縮ラベル維持)
	if (savedata.playerMode == 1) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->ApplyPauseButtonLabel();
	}
}

void COggDlg::ResetPauseButtonUi()
{
	if (!m_ps.GetSafeHwnd())
		return;
	ps = 0;
	m_ps.SetWindowText(LL14(
		L"一時停止",            /* 日本語 */
		L"Pause",               /* 英語 */
		L"Pause",               /* フランス語 */
		L"Pausa",               /* イタリア語 */
		L"Pausa",               /* スペイン語 */
		L"일시 정지",            /* 韓国語 */
		L"暂停",                /* 中国語 */
		L"إيقاف مؤقت",          /* アラビア語 */
		L"Пауза",               /* ロシア語 */
		L"Pause",               /* ドイツ語 */
		L"Pausar",              /* ポルトガル語 */
		L"Pauzeren",            /* オランダ語 */
		L"Wstrzymaj",           /* ポーランド語 */
		L"Duraklat"));          /* トルコ語 */
	SyncPauseButtonUi();
}

// 再生通知スレッドが動いている／動いていた可能性があるか（形式を問わず）
static inline bool PlaybackNotifyThreadMayBeActive()
{
	return plf != 0 || playf != 0 || ogg != NULL || adbuf2 != NULL || (og && og->mod != NULL)
		|| wav != NULL || mode == 999 || mode == -10 || mode == -9 || mode == -8
		|| mode == -7 || mode == -6 || mode == -3 || mode == -1 || mode == 30
		|| (mode > 0 && mode <= 21);
}

void COggDlg::stop()
{
	// play/stop1 実行中の再入: 停止要求だけ出して本体は触らない
	if (s_inPlay || s_inStop1) {
		playf = 0;
		plf = 0;
		SignalPlaybackNotifyThreadStop();
		if (::IsWindow(m_PianoRollDlg->GetSafeHwnd()))
			m_PianoRollDlg->PauseAnalysis();
		if (::IsWindow(m_AnalyzerDlg->GetSafeHwnd()))
			m_AnalyzerDlg->PauseFeed();
		MpPromptOnPlaybackStop();
		return;
	}
	s_inStop1 = true;
	struct ClearInStop1 { ~ClearInStop1() { s_inStop1 = false; } } _clearInStop1;

	// DoEvent 再入より先に解析を止める（停止ボタンでは起きず曲切替で落ちる主因）
	playf = 0;
	plf = 0;
	if (::IsWindow(m_PianoRollDlg->GetSafeHwnd()))
		m_PianoRollDlg->PauseAnalysis();
	if (::IsWindow(m_AnalyzerDlg->GetSafeHwnd()))
		m_AnalyzerDlg->PauseFeed();

	if (PlaybackNotifyThreadMayBeActive())
		SignalPlaybackNotifyThreadStop();

	fade1 = 0;
	endflg = 0;

	if (!img.IsNull()) {
		img.Destroy();
	}
	jx = -1;
	lrc_backup = L"";
	loop1_2 = -1;
	stflg = TRUE;
	KillTimer(1250);
	if (savedata.savecheck == 1 && (mode == -10 || mode == -2) && filenback == filen) {
		try {
			int flg = 0;
			if (mode == -10) {
				if (oggsize <= playb && oggsize != 0) {
					try {
						CFile::Remove(filen + _T(".save"));
					}
					catch (...) {
					}
					flg = 1;
				}
				if (playb == 0)
					flg = 1;
			}
			if (mode == -2) {
				if (oggsize2 <= aa1_ && oggsize2 != 0.0) {
					try {
						CFile::Remove(filen + _T(".save"));
					}
					catch (...) {
					}
					flg = 1;
				}
				if (aa1_ == 0.0) {
					flg = 1;
				}
			}
			if (flg == 0) {
				if ((savedata.savecheck_mp3 == 1 && mode == -10) || (savedata.savecheck_dshow == 1 && mode == -2)) {
					CFile f;
					if (f.Open(filen + _T(".save"), CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
						if (mode == -10)
							f.Write(&playb, sizeof(__int64));
						if (mode == -2)
							f.Write(&aa1_, sizeof(double));
						f.Close();
					}
				}
			}
			else {
			}
		}
		catch (...) {
		}
	}
	filenback = filen;
	playb = 0;
	if (ptl)ptl->SetProgressValue(m_hWnd, (LONGLONG)0, (LONGLONG)1);
	if (ptl)ptl->SetProgressState(m_hWnd, TBPF_NOPROGRESS);
	if (PlaybackNotifyThreadMayBeActive())
	{
		SignalPlaybackNotifyThreadStop();
		if (m_dsb)m_dsb->SetVolume(DSBVOLUME_MIN);
		ps = 0;
		if (m_dsb)m_dsb->Stop();
		if (pAudioClient) pAudioClient->Stop();
		if (m_dou.GetCheck() == 1)
			if (cc1 == 1) {
				// 2GB超対応(RF64): ファイル実長から64bitでサイズ確定
				FinalizeWavStreamHeaderRF64(cc);
				cc.Close();
				cc1 = 0;
			}
		CCriticalLock _ccl(&cs);
		stf = 1;
		_ccl.Leave();
		// 曲切替中は上限付き。無限 Join は DS Lock 固着で UI 永久停止の原因になる。
		// Join 中は DoEvent しない（再入 UAF 防止）
		const DWORD joinTimeout = g_interactiveTrackChange
			? (g_playbackNotifyJoinTimeoutMs ? g_playbackNotifyJoinTimeoutMs : 2500u)
			: 0u;
		if (!WaitForPlaybackNotifyThreadExit(joinTimeout)) {
			// タイムアウトでも停止フラグを残すと、続く play() の CWread/adbuf が
			// IsPlaybackStopRequested で無音になる。デコーダは触らずフラグだけ戻す。
			thn1 = FALSE;
			stf = 0;
			SongParams_OnSongStopped();
			return;
		}
		SongParams_OnSongStopped();

		Closeds();
		//		FreeOutputBuffer();
		plf = 0;

		if (ogg)ReleaseOggVorbis(&ogg);

		ogg = NULL;

		//		for(int l=0;l<20;l++){Sleep(50);DoEvent();}
		if (adbuf2)free(adbuf2);//delete [] adbuf2;
		adbuf2 = NULL;
		// mode ではなく「実際に Open 中の形式」（曲切替前に Get が mode を差し替えても正しい）
		const int stoppingMode = PeekOpenDecoderMode(mode);
		if (stoppingMode == -10) { mp3_.Close(); g_mp3_decoder_bps = 16; }
		if (stoppingMode == -8) flac_.Close(og->kmp);
		if (stoppingMode == -9) m4a_.Close(og->kmp);
		if (stoppingMode == -7) dsd_.kpiClose(og->kmp);
		if (stoppingMode == 999) wav_.Close();
		kmp = NULL;
		ClearOpenDecoderMode();
		if (mod) {
			if (mod->Close) mod->Close(kmp1);
			if (mod->Deinit) mod->Deinit();
			FreeLibrary(hDLLk);
			mod = NULL; kmp1 = NULL; hDLLk = NULL;
		}
		if (kpidec)
			kpidec->Release();
		if (ob5)
			ob5->Release();
		if (g_kpiRemote && g_kpiSession.sessionId != 0) {
			g_kpiHost.Close(g_kpiSession.sessionId);
			ZeroMemory(&g_kpiSession, sizeof(g_kpiSession));
			g_kpiRemote = false;
			ResetKpiRemoteCache();
		}
		g_kpiPlaybackArch = 0;
		thn1 = FALSE;
		stf = 0;
		// Join/解放後に DoEvent しない（再入で別形式の play が走り UAF になる）
		thend = 1;
		fadeadd = 0; fade = 1.0;
	}
	// 通知スレッドが pMediaControl を触るのを止めてから動画グラフを解放する
	if (pMainFrame1 != NULL)
		pMainFrame1->stop();
	else if ((mode == -2 || videoonly) && pMediaControl)
		pMediaControl->Stop();
	gamenkill();
	videoonly = FALSE;
	plf = 0;
	playf = 0;
	if (::IsWindow(m_PianoRollDlg->GetSafeHwnd()))
		m_PianoRollDlg->ResetPlaybackState();
	if (::IsWindow(m_AnalyzerDlg->GetSafeHwnd()))
		m_AnalyzerDlg->ResetPlaybackState();
	m_analyzerSyncValid = FALSE;
	if (wav) free(wav);
	wav = NULL;
	mode = modesub;
	stflg = FALSE;
	SetTimer(4923, 30, NULL);
	m_lrc.SetWindowText(L"");
	m_lrc2.SetWindowText(LL14(L"歌詞(.lrc)が表示されます", L"Lyrics (.lrc) will be displayed here", L"Paroles (.lrc) affichees ici", L"Testi (.lrc) visualizzati qui", L"Letra (.lrc) mostrada aqui", L"가사(.lrc)가 여기에 표시됩니다", L"歌词(.lrc)将在此显示", L"كلمات (.lrc) ستُعرض هنا", L"Текст (.lrc) отображается здесь", L"Liedtext (.lrc) wird hier angezeigt", L"Letra (.lrc) exibida aqui", L"Songtekst (.lrc) wordt hier getoond", L"Teksty (.lrc) wy?wietlone tutaj", L"Soz (.lrc) burada goruntulenir"));
	m_lrc3.SetWindowText(L"");
	m_lrc4.SetWindowText(L"");
	m_lrc5.SetWindowText(L"");

	ResetPauseButtonUi();
	eqflg = TRUE;
	if (pl && plw && pl->pnt >= 0 && pl->pnt < pl->playcnt)
		ApplyPlaylistRowDisplay(pl->pc[pl->pnt]);
	MpPromptOnPlaybackStop();
}

BOOL COggDlg::stop1()
{

	// 再入（Join 後の旧 DoEvent や play 中のメッセージ）ではデコーダを触らない
	if (s_inStop1) {
		SignalPlaybackNotifyThreadStop();
		// play() 側は戻り値を見ずに続行するため、再入でもフラグは下ろしておく
		thn1 = FALSE;
		stf = 0;
		thend1 = FALSE;
		return TRUE;
	}
	s_inStop1 = true;
	struct ClearInStop1 { ~ClearInStop1() { s_inStop1 = false; } } _clearInStop1;

	// playlist Get() が mode を次曲形式に差し替えた後でも、Open 中の形式で閉じる
	const int stoppingMode = PeekOpenDecoderMode(mode);

	// DoEvent 再入より先に解析を止める（形式違いの曲切替クラッシュ防止）
	playf = 0;
	plf = 0;
	// SongParams_OnSongStopped は Join 後に呼ぶ(再生スレッドが Sync 中に UI/ロックを掴むため)
	if (::IsWindow(m_PianoRollDlg->GetSafeHwnd()))
		m_PianoRollDlg->PauseAnalysis();
	if (::IsWindow(m_AnalyzerDlg->GetSafeHwnd()))
		m_AnalyzerDlg->PauseFeed();

	// 形式を問わず停止要求。Join 前にデコーダを閉じない。
	SignalPlaybackNotifyThreadStop();
	KillTimer(1250);
	KillTimer(9000);

	// CWread 中断要求。完了後は必ず thend1 を下ろす（残すと次の CWread が即死する）
	if (thend == FALSE) {
		thend1 = TRUE;
		for (int kk = 0; kk < 50; kk++) {
			if (thend == 1) break;
			SignalPlaybackNotifyThreadStop();
			Sleep(1);
		}
	}
	// CWread が thend1 を見て return してから AfxEndThread するまでの猶予。
	Sleep(50);
	playb = 0;
	thend = 1;
	thend1 = FALSE;

	fade1 = 0;
	endflg = 0;

	if (!img.IsNull()) {
		img.Destroy();
	}
	jx = -1;
	lrc_backup = L"";
	loop1_2 = -1;

	//	for(int i=0;i<10;i++){DoEvent();Sleep(10);}
	if (ptl)ptl->SetProgressValue(m_hWnd, (LONGLONG)0, (LONGLONG)1);
	if (ptl)ptl->SetProgressState(m_hWnd, TBPF_NOPROGRESS);

	if (m_dsb)m_dsb->SetVolume(DSBVOLUME_MIN);
	ps = 0;
	if (m_dsb)m_dsb->Stop();
	if (pAudioClient) pAudioClient->Stop();
	if (m_dou.GetCheck() == 1)
		if (cc1 == 1) {
			// 2GB超対応(RF64): ファイル実長から64bitでサイズ確定
			FinalizeWavStreamHeaderRF64(cc);
			cc.Close();
			cc1 = 0;
		}
	{
		CCriticalLock _ccl(&cs);
		stf = 1;
		_ccl.Leave();
	}
	// play() 先頭の stop1 は Join 成否に関わらず続行する。
	// FALSE で return すると CWread に入らず 0:00／古い loop のままになる。
	// ただし対話的な曲切替では無限 Join 禁止(DS Lock / 旧 SaveFile 固着で UI 永久停止)。
	BOOL joined = TRUE;
	{
		const DWORD joinTimeout = g_interactiveTrackChange
			? (g_playbackNotifyJoinTimeoutMs ? g_playbackNotifyJoinTimeoutMs : 2500u)
			: 0u;
		joined = WaitForPlaybackNotifyThreadExit(joinTimeout);
	}
	thn1 = FALSE;
	stf = 0;
	thend1 = FALSE;
	SongParams_OnSongStopped();

	// Join 失敗時はデコーダを触らない(生存スレッドの UAF 防止)。play() は続行するが
	// PeekOpenDecoderMode が残っていれば次の Open 前に再停止がかかる。
	if (!joined) {
		playf = 0;
		plf = 0;
		return FALSE;
	}

	Closeds();
	//		FreeOutputBuffer();
	plf = 0;
	if (ogg)ReleaseOggVorbis(&ogg);
	ogg = NULL;

	// デコーダ解放は必ず Join 後（flac/m4a/dsd/wav は通知スレッドが Render 中）
	if (adbuf2) {
		free(adbuf2);
		adbuf2 = NULL;
	}
	wav999_use_adbuf = 0;
	if (stoppingMode == -10) { mp3_.Close(); g_mp3_decoder_bps = 16; }
	if (stoppingMode == -8 && kmp) { flac_.Close(kmp); }
	if (stoppingMode == -9 && kmp) { m4a_.Close(kmp); }
	if (stoppingMode == -7 && kmp) { dsd_.kpiClose(kmp); }
	if (stoppingMode == 999) wav_.Close();
	kmp = NULL;
	ClearOpenDecoderMode();
	if (mod) {
		if (mod->Close) mod->Close(kmp1);
		if (mod->Deinit) mod->Deinit();
		FreeLibrary(hDLLk);
		mod = NULL; kmp1 = NULL; hDLLk = NULL;
	}
	if (kpidec) {
		kpidec->Release();
		kpidec = NULL;
	}
	if (ob5) {
		ob5->Release();
		ob5 = NULL;
	}

	fadeadd = 0; fade = 1.0;
	// 解放完了後に初めて停止フラグを下ろす（play() 先頭でも下ろすが、ここでも戻す）
	thn1 = FALSE;
	stf = 0;

	if (pMainFrame1 != NULL)
		pMainFrame1->stop();
	gamenkill();
	videoonly = FALSE;
	plf = 0;
	playf = 0;
	if (::IsWindow(m_PianoRollDlg->GetSafeHwnd()))
		m_PianoRollDlg->ResetPlaybackState();
	if (::IsWindow(m_AnalyzerDlg->GetSafeHwnd()))
		m_AnalyzerDlg->ResetPlaybackState();
	m_analyzerSyncValid = FALSE;
	if (wav) free(wav);
	wav = NULL;
	mode = modesub;
	m_lrc.SetWindowText(L"");
	m_lrc2.SetWindowText(LL14(L"歌詞(.lrc)が表示されます", L"Lyrics (.lrc) will be displayed here", L"Paroles (.lrc) affichees ici", L"Testi (.lrc) visualizzati qui", L"Letra (.lrc) mostrada aqui", L"가사(.lrc)가 여기에 표시됩니다", L"歌词(.lrc)将在此显示", L"كلمات (.lrc) ستُعرض هنا", L"Текст (.lrc) отображается здесь", L"Liedtext (.lrc) wird hier angezeigt", L"Letra (.lrc) exibida aqui", L"Songtekst (.lrc) wordt hier getoond", L"Teksty (.lrc) wy?wietlone tutaj", L"Soz (.lrc) burada goruntulenir"));
	m_lrc3.SetWindowText(L"");
	m_lrc4.SetWindowText(L"");
	m_lrc5.SetWindowText(L"");
	ResetPauseButtonUi();
	eqflg = TRUE;
	return TRUE;
}


BOOL COggDlg::DestroyWindow()
{
	// TODO: この位置に固有の処理を追加するか、または基本クラスを呼び出してください
	//	ReleaseOggVorbis(&ogg);
	MpPromptOnAppShutdown();
	stop();
	waveOutReset(hwo);
	waveOutClose(hwo);
	if (deve) {
		if (audio && g_epVolCb) {
			audio->UnregisterControlChangeNotify(g_epVolCb);
			g_epVolCb->Release();
			g_epVolCb = nullptr;
		}
		if (audio) audio->Release();
		if (dev) dev->Release();
		deve->Release();
		audio = NULL; dev = NULL; deve = NULL;
	}
	// メディアプレイヤー画面(オーナー無しトップレベル)を後始末
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		mp->SavePos();
		mp->DestroyWindow();
	}
	if (mp) { delete mp; mp = NULL; }
	if (pl && plw) {
		killw1 = 0;
		pl->DestroyWindow();
		PumpUntilFlagOrTimeout(killw1);
		pl = NULL;
		savedata.pl = 1;
	}
	else savedata.pl = 0;
	if (mi) {
		killw1 = 0;
		mi->DestroyWindow();
		PumpUntilFlagOrTimeout(killw1);
		mi = NULL;
	}
	if (::IsWindow(m_EqualizerDlg->GetSafeHwnd())) {
		m_EqualizerDlg->DestroyWindow();
	}
	if (::IsWindow(m_PianoRollDlg->GetSafeHwnd())) {
		m_PianoRollDlg->DetachForDestroy();
		m_PianoRollDlg->DestroyWindow();
	}
	if (::IsWindow(m_AnalyzerDlg->GetSafeHwnd())) {
		m_AnalyzerDlg->DetachForDestroy();
		m_AnalyzerDlg->DestroyWindow();
	}
	if (::IsWindow(m_PianoRollTuneDlg->GetSafeHwnd())) {
		m_PianoRollTuneDlg->DestroyWindow();
	}
	delete m_EqualizerDlg; m_EqualizerDlg = nullptr;
	delete m_PianoRollDlg; m_PianoRollDlg = nullptr;
	delete m_PianoRollTuneDlg; m_PianoRollTuneDlg = nullptr;
	delete m_AnalyzerDlg; m_AnalyzerDlg = nullptr;
	if (m_pDlgColor)delete m_pDlgColor;
	if (ptl) ptl->Release();
	if (pcdl) pcdl->Release();
	CoUninitialize();
	//	timeKillEvent(uTimerId);
	StopTimerpVsyncThread();
	KillTimer(5656);
	KillTimer(5657);
	KillTimer(1233);
	timeEndPeriod(1);
	UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY0);
	UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY1);
	UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY2);
	UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY3);
	ReleaseDXSound();
	CString s;
	m_kaisuu.GetWindowText(s);
	savedata.kaisuu = _tstoi(s);
	savedata.gameflg[0] = m_ys6.GetCheck();
	savedata.gameflg[1] = m_ysf.GetCheck();
	savedata.gameflg[2] = m_ed6fc.GetCheck();
	savedata.gameflg[3] = m_ed6sc.GetCheck();
	savedata.gameflg[3] = m_ed6sc.GetCheck();
	savedata.gameflg2 = m_yso.GetCheck();
	savedata.gameflg3 = m_ed6tc.GetCheck();
	savedata.gameflg4 = m_zweiii.GetCheck();
	savedata.gameflg5 = m_ysc1.GetCheck();
	savedata.gameflg6 = m_ysc2.GetCheck();
	savedata.gameflg7 = m_xa.GetCheck();
	savedata.gameflg8 = m_ys121.GetCheck();
	savedata.gameflg9 = m_ys122.GetCheck();
	savedata.gameflg10 = m_sor.GetCheck();
	savedata.gameflg11 = m_zwei.GetCheck();
	savedata.gameflg12 = m_gurumin.GetCheck();
	savedata.gameflg13 = m_dino.GetCheck();
	savedata.gameflg14 = m_br4.GetCheck();
	savedata.gameflg15 = m_ed3.GetCheck();
	savedata.gameflg16 = m_ed4.GetCheck();
	savedata.gameflg17 = m_ed5.GetCheck();
	savedata.supe = m_supe.GetCheck();
	savedata.supe2 = m_st.GetCheck();
	drawth = TRUE;
	RECT r;
	ShowWindow(SW_SHOWNORMAL);
	GetWindowRect(&r);
	savedata.xx = r.left;
	savedata.yy = r.top;
	DeleteObject(hFont);
	bmp.DeleteObject();
	dc.DeleteDC();
	bmpsub.DeleteObject();
	dcsub.DeleteDC();

	{
		TCHAR tmp_savedir[1024];
		_tgetcwd(tmp_savedir, 1000);
		_tchdir(karento2);
		CFile ab;
#if _UNICODE
		if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
		if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
			ab.Write(&savedata, sizeof(save));
			ab.Close();
		}
		_tchdir(tmp_savedir);
	}

	return CCustomBlurDialogBase::DestroyWindow();
}

// mcopy は RB レイテンシで 1 回の要求バイトに満たないことがある。部分返却を繰り返して DS 要求分を満たす（readmp3 と同じ）
static int McopyAccumulate(char* dst, int wantBytes)
{
	int got = 0;
	int guard = 0;
	int zeroStreak = 0;
	const int maxIters = 8192;
	const int maxZeroStreak = 512;
	while (got < wantBytes && guard++ < maxIters) {
		if (IsPlaybackStopRequested())
			break;
		int n = mcopy(dst + got, wantBytes - got);
		if (n > 0) {
			got += n;
			zeroStreak = 0;
			continue;
		}
		if (n < 0)
			break;
		// n==0: RB プライミング中はリトライ（真の EOF/ループ端は playwavds2 側で処理）
		if (++zeroStreak >= maxZeroStreak)
			break;
	}
	return got;
}

//oggから実際にデータを獲得する
int mcopy(char* a, int len)
{
	if (len == 0) return 0;
	EqualiserSetFormatVolContext(0, FALSE);
	const int bpf = PcmOutBytesPerFrame();
	if (bpf <= 0) return 0;
	int ret = 0, lenl = len / bpf, cnt2;
	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	const int ovChunkBytes = 4096;
	int maxBufSamples = (int)(sizeof(bufwav) / (size_t)bpf);
	int safeFillSamples = maxBufSamples - (ovChunkBytes / bpf) - 1;
	if (safeFillSamples < 1)
		safeFillSamples = 1;
	if (lenl > safeFillSamples)
		lenl = safeFillSamples;
	if (poss < 0 || poss > safeFillSamples)
		poss = 0;
	SanitizeKpi3RingState(max_buffer_size);

	// 1) ループ境界を先に計算し、このコールで返すべきバイト数 to_read を決める
	int to_read = len;
	int loopEndSamples = 0;
	{
		if (loop1 == 0 && loop2 == 0) {
			loopEndSamples = (bpf > 0) ? (int)((__int64)data_size / bpf) : 0;
		}
		else {
			loopEndSamples = loop1 + loop2;
		}
		const int totalSamples = (bpf > 0) ? (int)((__int64)data_size / bpf) : 0;
		if (totalSamples > 0 && loopEndSamples > totalSamples)
			loopEndSamples = totalSamples;
		const int heardPos = (int)playb;
		const int decodePos = (int)g_oggPcmDecodePos;
		// 出力(playb)とデコード位置のずれで loop2 越え/loop1 ダブりが起きるため、
		// ループ残量は「聴こえた位置」と「デコード済み位置」の遅れの小さい方で決める
		int posForLoop = heardPos;
		if (decodePos < heardPos)
			posForLoop = decodePos;
		if (loopEndSamples > 0 && endf == 0) {
			if (posForLoop >= loopEndSamples) {
				to_read = 0;
			}
			else {
				const int remainSamples = loopEndSamples - posForLoop;
				const int remainBytes = remainSamples * bpf;
				if (remainBytes < to_read)
					to_read = remainBytes;
			}
		}
	}
	to_read -= to_read % bpf;
	if (to_read <= 0) {
		if (loopEndSamples > 0) {
			playb = loopEndSamples;
			g_oggPcmDecodePos = loopEndSamples;
			poss5 = loopEndSamples;
			if (OggUseLowRateLoopExtras())
				OggFlushKpi3Ring();
		}
		return 0;
	}

	// 2) リング充填: to_read + レイテンシ余裕（またはプライミング要求）までデコード
	{
		int fillTarget = to_read + OggRbLatencyReserveBytes();
		if (g_oggRbPrimingNeed > fillTarget)
			fillTarget = g_oggRbPrimingNeed;

		int oggRbStallIters = 0;
		const int kOggRbStallMax = 8192;
		while (poss4 < fillTarget) {
			ret = 0;
			if (IsPlaybackStopRequested())
				break;
			if ((int)playb > (data_size + 20000) / bpf && endf == 1) {
				if (g_endWrittenBytes == 0) {
					if (savedata.saverenzoku == 0) fade1 = 1;
					else endflg = 1;
				}
				playb += lenl;
				if (muon != 0) { muon--; ZeroMemory(a, len); }
				if (muon == MUON) { ZeroMemory(a, len); muon--; rrr = 0; }
				if (muon == 0) return 0;
				return len;
			}

			// ループ終端以降は ov_read しない（loop2 越えのデコード→リング残留→だぶりの原因）
			if (loopEndSamples > 0 && endf == 0 && g_oggPcmDecodePos >= loopEndSamples)
				break;

			for (;;) {
				if (IsPlaybackStopRequested())
					break;
				if (loopEndSamples > 0 && endf == 0 && g_oggPcmDecodePos >= loopEndSamples)
					break;
				if (poss < 0 || poss > safeFillSamples)
					poss = 0;
				if (poss >= safeFillSamples)
					break;

				int maxChunkSamples = safeFillSamples - poss;
				if (loopEndSamples > 0 && endf == 0) {
					const __int64 remainIn = (__int64)loopEndSamples - g_oggPcmDecodePos - poss;
					if (remainIn <= 0)
						break;
					if (remainIn < maxChunkSamples)
						maxChunkSamples = (int)remainIn;
				}
				if (maxChunkSamples <= 0)
					break;

				long readBytes = ovChunkBytes;
				const int maxChunkBytes = maxChunkSamples * bpf;
				if (maxChunkBytes > 0 && readBytes > maxChunkBytes)
					readBytes = maxChunkBytes;

				long bytesRead = ov_read(&vf, (char*)(bufwav + poss * bpf), readBytes, 0, 2, 1, &current_section);
				if (bytesRead <= 0) {
					ret = 0;
					break;
				}
				ret = (int)(bytesRead / bpf);
				if (ret <= 0)
					break;
				if (maxChunkSamples > 0 && ret > maxChunkSamples)
					ret = maxChunkSamples;
				if (ret <= 0)
					break;
				poss += ret;
				if (lenl <= poss)
					break;
			}

			int validSamples = poss;
			if (validSamples > lenl)
				validSamples = lenl;
			if (validSamples < 0)
				validSamples = 0;
			if (loopEndSamples > 0 && endf == 0) {
				const __int64 remainIn = (__int64)loopEndSamples - g_oggPcmDecodePos;
				if (remainIn <= 0) {
					validSamples = 0;
				}
				else if (validSamples > remainIn) {
					validSamples = (int)remainIn;
				}
			}

			int len2 = 0;
			if (validSamples > 0)
				len2 = readtempo(bufwav, validSamples * bpf);
			if (IsPlaybackStopRequested())
				break;

			if (validSamples > 0)
				g_oggPcmDecodePos += validSamples;

			if (len2 > 0) {
				oggRbStallIters = 0;
				RingBufWrite(bufkpi3, max_buffer_size, poss2, outputRawBytesData.data(), len2);
				poss4 += len2;
				if (poss4 > max_buffer_size)
					poss4 = max_buffer_size;
				SanitizeKpi3RingState(max_buffer_size);
				if (g_inWarmup)
					g_warmupRBOutputBytes += len2;
			}
			if (validSamples > 0 && validSamples < poss) {
				const int leftover = poss - validSamples;
				if (leftover > 0)
					memmove(bufwav, bufwav + validSamples * bpf, (size_t)leftover * (size_t)bpf);
				poss = leftover;
			}
			else if (lenl <= poss) {
				poss -= lenl;
				if (poss < 0)
					poss = 0;
				if (poss > safeFillSamples)
					poss = safeFillSamples;
				if (poss != 0)
					memmove(bufwav, bufwav + lenl * bpf, poss * bpf);
			}
			if (poss4 >= fillTarget)
				break;
			if (loopEndSamples > 0 && endf == 0 && g_oggPcmDecodePos >= loopEndSamples)
				break;
			if (ret == 0 && len2 <= 0)
				break;
			if (len2 <= 0 && ret > 0) {
				if (++oggRbStallIters >= kOggRbStallMax)
					break;
				continue;
			}
			if (len2 <= 0) {
				if (++oggRbStallIters >= kOggRbStallMax)
					break;
			}
		}
	}

	// ループ/シーク直後: プライミング完了まで出力しない
	if (g_oggRbPrimingNeed > 0 && poss4 < g_oggRbPrimingNeed)
		return 0;
	if (g_oggRbPrimingNeed > 0)
		g_oggRbPrimingNeed = 0;

	// 3) リングから to_read 分だけ返す
	cnt2 = to_read;
	if (poss4 < to_read)
		cnt2 = poss4;
	cnt2 -= cnt2 % bpf;
	if (cnt2 <= 0)
		return 0;

	RingBufRead(a, bufkpi3, max_buffer_size, poss3, cnt2);
	poss4 -= cnt2;
	if (poss4 < 0)
		poss4 = 0;
	SanitizeKpi3RingState(max_buffer_size);

	playb += cnt2 / bpf;
	if (loopEndSamples > 0 && (int)playb > loopEndSamples)
		playb = loopEndSamples;

	// ループ端: 余ったリングデータを破棄（32k 等。44.1k はシームレス維持のため触らない）
	if (OggUseLowRateLoopExtras() && loopEndSamples > 0 && endf == 0 &&
		(int)playb >= loopEndSamples && g_oggPcmDecodePos >= loopEndSamples) {
		playb = loopEndSamples;
		g_oggPcmDecodePos = loopEndSamples;
		OggFlushKpi3Ring();
		poss = 0;
	}

	{
		float te = (float)tempo;
		if (te >= 200.0f) te -= 100.0f;
		else              te = te / 3.0f + 33.3f;
		poss5 += (int)((cnt2 / bpf) * (te / 100.0f));
	}

	if (!g_inWarmup && cnt2 > 0) {
		equaliser(a, cnt2, reset);
		og->FeedPianoRoll(a, cnt2);
		reset = FALSE;
	}

	short* b, c;
	b = (short*)a;
	fade += fadeadd;
	if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
	if (cnt2 > 0) {
		for (int i = 0; i < cnt2 / 2; i++) {
			c = b[i];
			c = (short)(((float)c) * fade * fade);
			b[i] = c;
		}
	}

	if (cc1 == 1 && !g_inWarmup && cnt2 > 0) cc.Write(a, cnt2);
	if (cnt2 > 0) wl += cnt2;

	return cnt2;
}

/*
int li=0;
void CALLBACK TimeCallback(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
timingf++;
if(timingf==16&&timerf1==0){
timerf1++;
timingf=0;
if(li==0){
li=1;
og->timerp();
li=0;
}
}
if(timingf==17&&timerf1>0){
timingf=0;
timerf1++;
if(li==0){
li=1;
og->timerp();
li=0;
}
}
if(timerf1==3)timerf1=0;

}
*/

CString wavb(int d);
CString wavb(int d) {
	CString wavbit1;
	if (d == (int)((int)d / 1000) * 1000) {
		wavbit1.Format(L"%dk", d / 1000);
	}
	else if (d == (int)((int)d / 100) * 100) {
		wavbit1.Format(L"%3.1fk", (float)d / 1000);
	}
	else {
		wavbit1.Format(L"%d", d);
	}
	return wavbit1;
}

static CString FormatBannerDataAudioLine()
{
	const int srcRate = wavbit_sample_Hz;
	const int srcCh = wavchannel;
	const int srcBits = abs(wavsam_depth);
	if (srcRate <= 0 || srcCh <= 0)
		return CString();

	auto specBody = [](LPCTSTR rateStr, int ch, int bits) -> CString {
		CString body;
		body.Format(_T("%sHz %s %dbit"), rateStr, (LPCTSTR)ChannelLayoutLabel(ch), abs(bits));
		return body;
	};

	const CString rateSrc = wavb(srcRate);
	if (!g_pcm_upscale_active
		|| (srcRate == g_ds_pcm_rate && srcCh == g_ds_pcm_ch && srcBits == g_ds_pcm_bits)) {
		CString s;
		s.Format(_T("data:%s"), (LPCTSTR)specBody(rateSrc, srcCh, srcBits));
		return s;
	}

	CString s;
	s.Format(_T("data:%s  %s  %s"),
		(LPCTSTR)specBody(rateSrc, srcCh, srcBits),
		AudioUpscaleFlowSymbol(),
		(LPCTSTR)specBody(wavb(g_ds_pcm_rate), g_ds_pcm_ch, g_ds_pcm_bits));
	return s;
}

extern IBasicAudio* pBasicAudio;
extern IBaseFilter* prend;
extern double rate;
extern int rateflg;
extern RECT rcm;
extern long height, width;
DWORD videocnt = 0, videocnt2 = 0, videocnt3;

int pox, poy;

static int s_lastMs2DrawMs = 0;

// KPI(mode==-11)は無限ループで loopcnt が増えないため、連続再生時は経過時間で次曲へ進める。
static DWORD g_kpiRenzokuTick = 0;   // 現在曲の計測開始tick(0=計測なし/発火済み)
static int   g_kpiRenzokuPnt = -999; // 計測中の曲インデックス(変化で計測リセット)
static const DWORD KPI_RENZOKU_LIMIT_MS = 120000;  // 2分

// スクロールテキスト区切り装飾（定義は mojisub の後）
static void DrawScrollSepDeco(CDC& dc, int x_px, int h_px, int w_px, COLORREF clr = RGB(255, 255, 255));

// hFont での文字列ピクセル幅（描画なし）
static int BannerTextWidthPx(CDC& cdc, LPCTSTR text)
{
	if (!text || !*text) return 0;
	HFONT fo = (HFONT)SelectObject(cdc, hFont);
	SIZE sz = {};
	GetTextExtentPoint32(cdc, text, (int)_tcslen(text), &sz);
	SelectObject(cdc, fo);
	return sz.cx;
}

// ラベル実測幅から値の描画 X とスクロール枠幅を求める（固定ピッチ時は従来の 8*5*4 / MDC と一致）
static void BannerValueLayout(int labelW_px, int& valueX_px, int& viewW_px)
{
	valueX_px = labelW_px;
	if (valueX_px < 0) valueX_px = 0;
	viewW_px = MDC_TOTAL - valueX_px;
	if (viewW_px < 8 * 4) viewW_px = 8 * 4;
}

// フォント変更などで valueX/viewW が変わったらマーキー位置をリセット
static void BannerScrollResetIfLayoutChanged(int rowId, int valueX_px, int viewW_px,
	int& mcnt_scroll, int& mcnt_wrap)
{
	struct LayoutCache { int valueX; int viewW; };
	static LayoutCache s_cache[3] = { {-1, -1}, {-1, -1}, {-1, -1} };
	if (rowId < 0 || rowId >= 3) return;
	if (s_cache[rowId].valueX != valueX_px || s_cache[rowId].viewW != viewW_px) {
		s_cache[rowId].valueX = valueX_px;
		s_cache[rowId].viewW = viewW_px;
		mcnt_scroll = 0;
		mcnt_wrap = 0;
	}
}

// dcsub 上の値テキストをバナーへ BitBlt。はみ出し時は従来どおり 4px/frame でマーキー。
static void BannerBlitScrollValue(CDC& dst, CDC& src, int valueX_px, int viewW_px,
	int y_px, int blitH_px, int& mcnt_scroll, int& mcnt_wrap, int si_px)
{
	const int blitW = 88 * 2 * 4 + 1000;
	if (si_px > viewW_px) {
		dst.BitBlt(valueX_px, y_px, blitW, blitH_px, &src, mcnt_scroll, 0, SRCINVERT);
		if (si_px - mcnt_scroll < viewW_px) {
			mcnt_wrap += 4;
			dst.BitBlt(viewW_px - mcnt_wrap + valueX_px, y_px, blitW, blitH_px, &src, 0, 0, SRCINVERT);
			if (viewW_px - mcnt_wrap <= 0) { mcnt_wrap = 0; mcnt_scroll = 0; }
		}
		else {
			mcnt_wrap = 0;
		}
		mcnt_scroll += 4;
	}
	else {
		dst.BitBlt(valueX_px, y_px, blitW, blitH_px, &src, 0, 0, SRCINVERT);
	}
}

// GDI バナー時間表示(timerp の t3)と同じ実再生位置(秒)。プロンプト実行の基準時刻。
double OggGetGdiPlaybackTimeSec()
{
	if (wavbit_sample_Hz <= 0) return 0.0;
	static const double wavv2[] = { 0, 2.0, 1.0, 2.0 / 3.0, 2.0 / 4.0, 2.0 / 5.0, 2.0 / 6.0 };
	int ch = wavchannel;
	if (ch < 0 || ch > 6) ch = 2;
	const double rateDiv = (double)wavbit_sample_Hz / wavv2[ch];
	if (rateDiv <= 0.0) return 0.0;

	__int64 pb = 0;
	{
		std::lock_guard<std::mutex> lk(cl2);
		pb = playb;
	}

	static long s_lastQSamplesHeard = 0;
	const int bpfHeardNow = PcmOutBytesPerFrame();
	if (m_dsb && InterlockedCompareExchange(&g_dsDeviceOpBusy, 0, 0) == 0) {
		ULONG hp = 0, hw = 0;
		if (m_dsb->GetCurrentPosition(&hp, &hw) == DS_OK)
			s_lastQSamplesHeard = DsQueuedSamples(hp, hw, bpfHeardNow);
	}
	const long qSamplesHeard = s_lastQSamplesHeard;
	if (qSamplesHeard > 0 && pb > qSamplesHeard)
		pb -= qSamplesHeard;
	else if (qSamplesHeard > 0)
		pb = 0;

	double t3 = (double)pb / rateDiv;
	if ((mode == -9) && wavchannel > 2) t3 *= wavchannel / 2.0;
	if (t3 < 0.0) t3 = 0.0;
	return t3;
}

void OggResetRubberBandStretcher()
{
	if (g_rubberBandStretcher) {
		delete g_rubberBandStretcher;
		g_rubberBandStretcher = NULL;
	}
}

void COggDlg::timerp()
{
	if (g_oggUiThreadId != 0 && GetCurrentThreadId() != g_oggUiThreadId) {
		COgg_RequestTimerp(this);
		return;
	}
	if (playy == 0)return;

	if (s_lastMs2DrawMs != savedata.ms2) {
		s_lastMs2DrawMs = savedata.ms2;
		ms2 = 0;
	}

	// Update focus transition (about 2 seconds for a full transition)
	{
		DWORD current_tick = GetTickCount();
		if (m_lastTick == 0) {
			m_lastTick = current_tick;
		}
		double dt = (current_tick - m_lastTick) / 1000.0;
		m_lastTick = current_tick;

		if (dt < 0.0) dt = 0.0;
		if (dt > 0.1) dt = 0.1;

		bool is_hovered = false;
		// メディアプレイヤーモード中は裏のファルコム特化型(og)のクライアント座標で
		// Speana/GDI 帯を見ると、MP 上のカーソル位置が誤って「ホバー」扱いになる。
		// MP ではバナーホバー(g_mpBannerHover)のみを使う。
		extern int g_mpBannerHover;
		extern int g_mpSideJacket;
		if (savedata.playerMode != 1) {
			HWND hWndActive = ::GetActiveWindow();
			HWND hWndForeground = ::GetForegroundWindow();
			if (hWndActive == GetSafeHwnd() || hWndForeground == GetSafeHwnd()) {
				CPoint pt;
				::GetCursorPos(&pt);
				ScreenToClient(&pt);
				CRect drawing_rect(0, 0, (int)(MDCP * hD), (int)((81 + 16) * hD * 4));
				if (drawing_rect.PtInRect(pt)) {
					is_hovered = true;
				}
			}
		}
		else if (g_mpSideJacket) {
			// ミニジャケット分離時はバナー内蔵ジャケが無いのでホバー／前面化アニメは不要
			is_hovered = false;
			m_jacketFocus = 0.0;
		}
		else if (g_mpBannerHover) {
			// バナー上のホバーでアルファ前面化(ジャケット内蔵時のみ)
			is_hovered = true;
		}

		if (is_hovered) {
			m_jacketFocus += dt / 2.0;
			if (m_jacketFocus > 1.0) m_jacketFocus = 1.0;
		} else if (!(savedata.playerMode == 1 && g_mpSideJacket)) {
			m_jacketFocus -= dt / 2.0;
			if (m_jacketFocus < 0.0) m_jacketFocus = 0.0;
		}
	}
	ms2++;
	// ピアノ/アナライザの重い OnPaint で pending が長時間残ると Speana/EQ 供給が止まり
	// 分単位でコード表示がぎこちなくなる。一定時間で強制解除する。
	{
		const LONG pend = InterlockedCompareExchange(&g_gdiPaintPending, 0, 0);
		const DWORD nowPend = GetTickCount();
		if (pend != 0) {
			if (g_gdiPaintPendingSince == 0)
				g_gdiPaintPendingSince = nowPend;
			else if ((nowPend - g_gdiPaintPendingSince) >= 150u) {
				InterlockedExchange(&g_gdiPaintPending, 0);
				g_gdiPaintPendingSince = 0;
			}
		}
		else {
			g_gdiPaintPendingSince = 0;
		}
	}
	const BOOL bGdiFrame = Ms2DrawDue(ms2)
		&& (InterlockedCompareExchange(&g_gdiPaintPending, 0, 0) == 0);
	CString s, ss, sss;
	if (voldsf) {
		voldsf = 0;
		m_dsval.SetPos(savedata.dsvol);
		m_dsval.ShowWindow(SW_SHOWNA);
	}

	if (mode == -2) loop1 = loop2 = loopcnt = wl = 0;

	__int64 snap_playb;
	__int64 snap_wl;
	int snap_oggsize, snap_loop1, snap_loop2;
	double snap_oggsize2;
	{
		std::lock_guard<std::mutex> lk(cl2);
		snap_playb = playb;
		snap_oggsize = oggsize;
		snap_wl = wl;
		snap_oggsize2 = oggsize2;
		snap_loop1 = loop1;
		snap_loop2 = loop2;
	}

	// 実再生位置補正: playb はデコード先頭（先読み分だけ先）。DS 再生カーソルがまだ消化していない
	// キュー分(qSamples)を差し引くと「実際に聴こえている位置」になる。時間表示・スライダーで共用。
	// DS Lock 中は GetCurrentPosition もドライバで固まることがあるため、直前値を使う。
	long qSamplesHeard = 0;
	{
		static long s_lastQSamplesHeard = 0;
		const int bpfHeardNow = PcmOutBytesPerFrame();
		g_outBytesPerFrame = bpfHeardNow; // DS スレッドの短フェード尺計算用に共有
		if (m_dsb && InterlockedCompareExchange(&g_dsDeviceOpBusy, 0, 0) == 0) {
			ULONG hp = 0, hw = 0;
			if (m_dsb->GetCurrentPosition(&hp, &hw) == DS_OK)
				s_lastQSamplesHeard = DsQueuedSamples(hp, hw, bpfHeardNow);
		}
		qSamplesHeard = s_lastQSamplesHeard;
	}

	//時間
	int t1, ta, tb, tc, ta1, tb1, tc1, tag = 0, tbg = 0, tcg = 0, ttt;
	tt++;
	//	if(tt==4)
	//	{
	//		tt=0;
	double t3;
	if ((mode == -2 || (mode != -2 && videoonly == TRUE))) {
		t3 = (double)snap_oggsize2;
		tt = (int)(t3 * 100.0);
		t1 = tt / 100;
		ta = t1 / 60;
		tb = t1 % 60;
		tc = tt % 100;
		if (videocnt > 7) {
			REFTIME aa;
			aa = 0;
			if (pMediaPosition)pMediaPosition->get_CurrentPosition(&aa);
			if (pMediaPosition && (snap_oggsize2 < aa) && plf == 1) {
				aa = 0; plf = 0;
				if (pMainFrame1 != NULL && (mode == -2 || (mode > 0 && videoonly == TRUE))) {
					// 動画終端: ループ再生 / 連続再生 に対応(プレイリスト再生中のみ)。
					// 再生経路は MP_PlayIndex と同じ pl->Get + WM_APP+2 を使い、
					// play() 経由で動画グラフを正しく作り直す(確実・安全)。
					BOOL handled = FALSE;
					if (pl && plw && plcnt >= 0 && plcnt < pl->playcnt) {
						if (pl->m_loop.GetCheck() == TRUE) {
							// ループ再生: 同じ動画を先頭から再生し直す
							pl->Get(plcnt);
							gameon = 0;
							RequestPlaybackRestart(GetSafeHwnd());
							handled = TRUE;
						}
						else if (pl->m_renzoku.GetCheck() == TRUE) {
							// 連続再生: 次の曲へ(末尾なら先頭へ)
							int idx = plcnt + 1;
							if (idx >= pl->playcnt) idx = 0;
							pl->Get(idx);
							plcnt = idx;
							gameon = 0;
							RequestPlaybackRestart(GetSafeHwnd());
							handled = TRUE;
						}
					}
					if (!handled)
						pMainFrame1->pause(0);   // 従来動作: 終端で一時停止
				}
			}
			aa1_ = aa;
			videocnt = 0;
		}
		t3 = (double)aa1_;
		tt = (int)(t3 * 100.0);
		t1 = tt / 100;
		ta1 = t1 / 60;
		tb1 = t1 % 60;
		tc1 = tt % 100;
		ttt = tt;
	}
	else {
		double wavv[] = { 0,1.0,2.0,3.0 / 0.75,4.0 / 0.75,5.0 / 0.75,6.0 / 0.75 };//(double)(wavbit2/wavv[wavchannel])
		double wavv2[] = { 0,2.0,1.0,2.0 / 3.0,2.0 / 4.0,2.0 / 5.0,2.0 / 6.0 };//(double)(wavbit2/wavv[wavchannel])
		// MP3: oggsize / playb はいずれも「PCM フレーム数」（秒 = /wavbit_sample_Hz）。スライダー範囲は m_time.SetRange(F/100) と OnHScroll の curpos×100 で対応。
		if (mode == -10)
			t3 = (double)snap_oggsize / (double)wavbit_sample_Hz;
		else {
			t3 = (double)snap_oggsize / (double)(wavbit_sample_Hz * 2.0 * wavv[wavchannel]) / (double)(wavsam_depth / 16.0f);
		}
		if ((mode == -9) && wavchannel > 2) t3 *= wavchannel / 2.0;
		tt = (int)(t3 * 100.0);
		if (tt < 0) tt = 0;
		t1 = tt / 100;
		ta = t1 / 60;
		tb = t1 % 60;
		tc = tt % 100;
		t3 = (double)snap_playb / (double)(wavbit_sample_Hz / wavv2[wavchannel]);// / (double)(wavsam_depth / 16.0f);
		//先読み分を除去: playb はデコード先頭、実際に聴こえる位置は DS キュー分だけ過去
		// playb は PcmOutBytesPerFrame 基準のフレーム数。MP3 はシークも含めフレーム単位に統一（旧×4は廃止）。
		// 全 DS 出力モード（FLAC/ゲーム系含む）で実再生カーソル基準に統一する。
		if (qSamplesHeard > 0 && snap_playb > qSamplesHeard)
			t3 = (double)(snap_playb - qSamplesHeard) / (double)(wavbit_sample_Hz / wavv2[wavchannel]);
		if ((mode == -9) && wavchannel > 2) t3 *= wavchannel / 2.0;
		if (t3 < 0.0) t3 = 0.0;
		tt = (int)(t3 * 100.0);
		if (tt < 0) tt = 0;
		t1 = tt / 100;
		ta1 = t1 / 60;
		tb1 = t1 % 60;
		tc1 = tt % 100;
		ttt = tt;
		t3 = (double)snap_wl / (double)(wavbit_sample_Hz * 2 * wavv[wavchannel]) / (double)(wavsam_depth / 16.0f);
		tt = (int)(t3 * 100.0);
		t1 = tt / 100;
		tag = t1 / 60;
		tbg = t1 % 60;
		tcg = tt % 100;
	}
	videocnt++;

	if (MpPromptIsActive())
		MpPromptNotifyPlayback(plf, (plf == 1) ? ((double)ttt / 100.0) : 0.0);
	if (plf == 1 && ps == 0 && MpPromptIsActive())
		MpPromptTickAtTime((double)ttt / 100.0);

	t3 = (double)snap_loop1 / (double)(wavbit_sample_Hz);
	tt = (int)(t3 * 100.0);
	t1 = tt / 100;
	int tal1 = t1 / 60;
	int tbl1 = t1 % 60;
	int tcl1 = tt % 100;
	t3 = (double)(snap_loop2 + snap_loop1) / (double)(wavbit_sample_Hz);
	tt = (int)(t3 * 100.0);
	t1 = tt / 100;
	int tal2 = t1 / 60;
	int tbl2 = t1 % 60;
	int tcl2 = tt % 100;

	// EQ コード用 PCM は bGdiFrame/g_gdiPaintPending に依存させない。
	// pending 中 Speana 全体が止まるとコード更新が不規則になる（アナライザ/MP と差が出る主因）。
	{
		CEqualizer* pEq = m_EqualizerDlg;
		const bool eqVis = pEq
			&& ::IsWindow(pEq->GetSafeHwnd())
			&& ::IsWindowVisible(pEq->GetSafeHwnd());
		const bool speanaDraw = bGdiFrame && m_supe.GetCheck() == TRUE && plf == 1 && (wav || ogg);
		if (plf == 1 && (wav || ogg) && eqVis && !speanaDraw) {
			static DWORD s_eqFeedMs = 0;
			const DWORD nowFeed = GetTickCount();
			if (s_eqFeedMs == 0 || (nowFeed - s_eqFeedMs) >= (DWORD)EqCodeIntervalMs()) {
				s_eqFeedMs = nowFeed;
				Speana(FALSE);
			}
		}
	}

	if (bGdiFrame)
	{
	extern int g_mpSideJacket;   // 1=ジャケットを mp 左余白へ分離表示中(内蔵ジャケ抑止)
	dc.FillSolidRect(0, 0, 3000, 2000, RGB(0, 0, 0));
	//		dcsub.FillSolidRect(0,0,3000,30,RGB(1,1,1));

	bool draw_jacket_early = (m_jacketFocus < 0.5);
	if (draw_jacket_early && jx != -1 && !img.IsNull() && !g_mpSideJacket) {
		int h_dest = 388;
		int w_dest = (int)(388.0 * jxy);
		if (w_dest <= 0) w_dest = 388;
		int x_dest = 0;
		int y_dest = 0;

		int alpha = 130 + (int)((220 - 130) * m_jacketFocus);
		img.AlphaBlend(dc.m_hDC, x_dest, y_dest, w_dest, h_dest, 0, 0, jx, jy, alpha);
	}

	for (int lp = 0; lp < lrcnum - 1; lp++) {
		if (lrctm[lp] <= ttt && lrctm[lp + 1] > ttt) {
			CString s;
			m_lrc3.GetWindowText(s);
			if (lrc[lp] == lrc_backup) continue;
			m_lrc.SetWindowText(lp >= 2 ? lrc[lp - 2] : L"");
			m_lrc2.SetWindowText(lp >= 1 ? lrc[lp - 1] : L"");
			m_lrc3.SetWindowText(lrc[lp]);
			m_lrc4.SetWindowText(lrc[lp + 1]);
			m_lrc5.SetWindowText((lp + 2 < lrcnum - 1) ? lrc[lp + 2] : L"");
			lrc_backup = lrc[lp];
		}
	}


	// スペアナは XOR バナー文字(name/arti/albu)より先に dc へ描く。
	// 非同期(WM_SPEANA_TICK)化すると Speana() が文字の後に不透明の棒を上書きし、
	// XOR 合成が棒に覆われて無効化される（バナー文字が見えなくなるデグレ）。
	// ここで TRUE 指定すると EQ 供給も同梱（上の Speana(FALSE) と二重にならない）。
	if (m_supe.GetCheck() == TRUE && plf == 1 && (wav || ogg))
		Speana(TRUE);
	if (plf == 1 && ::IsWindow(m_PianoRollDlg->GetSafeHwnd()) && Ms2DrawDue(ms2))
		m_PianoRollDlg->RequestSyncFromMainUi();
	if (plf == 1 && ::IsWindow(m_AnalyzerDlg->GetSafeHwnd()) && Ms2DrawDue(ms2))
		m_AnalyzerDlg->RequestSyncFromMainUi();
	s = L""; ss = L"";
	s = "name:";
	int nameLabelW = moji(s, 1, 0, 0xffffff);
	int nameValueX = 0, nameViewW = 0;
	BannerValueLayout(nameLabelW, nameValueX, nameViewW);
	BannerScrollResetIfLayoutChanged(0, nameValueX, nameViewW, mcnt, mcnt2);
	if (fnn != L"")		sss = fnn;
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7) {
		if (!tagfile.IsEmpty()) sss = tagfile;
	}
	if ((stitle != "" && mode == -1) || mode == 21 || mode == -6 || (mode == 999 && stitle != ""))
		sss = stitle;
	int si = mojisub(sss, 1, 0, 0xffffff);
	if (si > nameViewW) {
		int sss_w = si;   // 本文ピクセル幅（セパレータ開始位置の計算に使用）
		ss = sss + _T("　　　");   // 全角スペース3文字でセパレータ幅を確保
		si = mojisub(ss, 1, 0, 0xffffff);
		// セパレータ部に GDI 装飾を重ね描き
		// h_px: テキストが使うフォント高さ(16*4)を渡す。行域全体(30*4)では cy がテキスト中心からズレる。
		if (Ms2DrawDue(ms2))
			DrawScrollSepDeco(dcsub, 4 + sss_w, 16 * 4, si - sss_w);
	}
	//枠はみ出し時スクロール処理（値 X / 枠幅はラベル実測に追従）
	BannerBlitScrollValue(dc, dcsub, nameValueX, nameViewW, 0, (24 * 4) * 4, mcnt, mcnt2, si);

	//mcnt1++;
	if (g_pActiveLoadingWnd != NULL) {
		s.Format(LL14(
			L"file:KPI読み込み中…",
			L"file:Loading KPI…",
			L"file:Chargement KPI…",
			L"file:Caricamento KPI…",
			L"file:Cargando KPI…",
			L"file:KPI 로딩 중…",
			L"file:正在加载KPI…",
			L"file:جاري تحميل KPI…",
			L"file:Загрузка KPI…",
			L"file:KPI wird geladen…",
			L"file:Carregando KPI…",
			L"file:KPI laden…",
			L"file:Ładowanie KPI…",
			L"file:KPI yükleniyor…"));
	}
	else if (IsFalcomGameBgmMode(modesub)) {
		CString fileName = filen;
		const int slash = max(fileName.ReverseFind(_T('\\')), fileName.ReverseFind(_T('/')));
		if (slash >= 0)
			fileName = fileName.Mid(slash + 1);

		CString gameName;
		if (pl && plcnt >= 0 && plcnt < pl->playcnt &&
			IsFalcomGameBgmMode(pl->pc[plcnt].sub))
			gameName = pl->pc[plcnt].game;

		if (!gameName.IsEmpty())
			s.Format(_T("file:%s(%s)"), (LPCTSTR)fileName, (LPCTSTR)gameName);
		else
			s.Format(_T("file:%s"), (LPCTSTR)fileName);
	}
	else if (modesub == 5 || modesub == 7 || modesub == 8 || modesub == 9 || modesub == 10)	s.Format(_T("file:%s"), filen);
	else if (mode == 21)
		s.Format(_T("file:%s"), filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1));
	else if (savedata.playerMode == 1 && plf == 0 && fnn != _T(""))
		s.Format(_T("file:%s"), fnn);
	else			s.Format(_T("file:%s"), filen);
	//		if(fnn.Right(4)=="動画"||fnn.Right(5).Left(4)=="動画")		s.Format("file:動画");
	if (filen.Left(2) == L"★")		s.Format(LL14(
		L"file:動画",                   /* 日本語: 厳格に維持いたします */
		L"file:Convulsing Images",     /* 英語: 痙攣する画像 */
		L"file:Images au Galop",       /* フランス語: 全力疾走する画像 */
		L"file:Immagini Insonni",      /* イタリア語: 不眠症の画像 */
		L"file:Cuadros con Prisa",     /* スペイン語: 急いでいるコマ絵 */
		L"file:발광하는 그림",           /* 韓国語: 発狂（発光）する絵 */
		L"file:抽搐的幻燈片",            /* 中国語: ひきつけを起こしたスライド */
		L"file:لوحات ترقص بعنف",       /* アラビア語: 激しく踊る絵画 */
		L"file:Бьющиеся картинки",     /* ロシア語: のたうち回る画像 */
		L"file:Zappelnde Lichtbilder", /* ドイツ語: じたばたする幻灯 */
		L"file:Imagens em Pânico",     /* ポルトガル語: パニックに陥った画像 */
		L"file:Stuiptrekkende Prenten", /* オランダ語: 痙攣（ひきつけ）を起こした版画 */
		L"file:Drgające Widziadła",    /* ポーランド語: 震える幻影 */
		L"file:Titreyen Karalamalar")); /* トルコ語: 震える落書き */
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7 || mode == -6 || mode == 999) {
		CString g; g = L""; g = filen; g.MakeLower();
		if (g.Right(4) == L".mp3") g = L"(mp3)";
		if (g.Right(4) == L".rmp") g = L"(rmp)";
		if (g.Right(4) == L".mp2") g = L"(mp2)";
		if (g.Right(4) == L".mp1") g = L"(mp1)";
		if (g.Right(4) == L".m4a") g = L"(m4a)";
		if (g.Right(4) == L".aac") g = "L(aac)";
		if (g.Right(5) == L".flac") g = L"(flac)";
		if (g.Right(7).MakeLower() == L".qull3h") g = L"(flac)";
		if (g.Right(5) == L".opus") g = L"(opus)";
		if (g.Right(4) == L".dsf") g = L"(DSD(dsf))";
		if (g.Right(4) == L".dff") g = L"(DSD(dff))";
		if (g.Right(4) == L".wsd") g = L"(DSD(wsd))";
		if (g.Right(4) == L".wav") g = L"(wav)";
		s.Format(LL14(L"file:音声ファイル%s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:오디오 %s", L"file:音频 %s", L"file:صوت %s", L"file:Аудио %s", L"file:Audio %s", L"file:Áudio %s", L"file:Audio %s", L"file:Audio %s", L"file:Ses %s"), g);
	}
	if (mode == -2 || mode == -3) sss = filen.Right(filen.GetLength() - filen.ReverseFind('.') - 1);
	if (mode == -3) {
		int archBits = g_kpiPlaybackArch;
		if (!archBits)
			archBits = ResolveKpiArchBits(CString(kpi), filen);
		const CString arch = KpiArchLabel(archBits);
		s.Format(LL14(L"file:kpiプラグイン(%s %s)", L"file:kpi plugin (%s %s)", L"file:Plugin kpi (%s %s)", L"file:Plugin kpi (%s %s)", L"file:Plugin kpi (%s %s)", L"file:kpi 플러그인(%s %s)", L"file:kpi 插件(%s %s)", L"file:إضافة kpi (%s %s)", L"file:Плагин kpi (%s %s)", L"file:kpi-Plugin (%s %s)", L"file:Plugin kpi (%s %s)", L"file:kpi-plugin (%s %s)", L"file:Wtyczka kpi (%s %s)", L"file:kpi eklentisi (%s %s)"), sss, arch);
	}
	if (mode == -1) s.Format(LL14(L"file:oggファイル", L"file:ogg", L"file:ogg", L"file:ogg", L"file:ogg", L"file:ogg 파일", L"file:ogg文件", L"file:ogg", L"file:ogg", L"file:ogg-Datei", L"file:ogg", L"file:ogg", L"file:ogg", L"file:ogg"));
	if (mode == -2 && rate == 0.0) s.Format(LL14(L"file:音声ファイル(%s)", L"file:Audio (%s)", L"file:Fichier audio (%s)", L"file:File audio (%s)", L"file:Archivo de audio (%s)", L"file:오디오 파일(%s)", L"file:音频文件(%s)", L"file:ملف صوت (%s)", L"file:Аудиофайл (%s)", L"file:Audiodatei (%s)", L"file:Arquivo de áudio (%s)", L"file:Audiobestand (%s)", L"file:Plik audio (%s)", L"file:Ses dosyası (%s)"), sss);
	if (mode == -2 && rate != 0.0) s.Format(LL14(
		L"file:動画ファイル(%s)",          /* 日本語: 厳格に維持いたします */
		L"file:Panting Box (%s)",        /* 英語: 息を切らしている箱 */
		L"file:Boîte de Tempête (%s)",    /* フランス語: 嵐の詰まった箱 */
		L"file:Scatola Urlando (%s)",     /* イタリア語: 絶叫している箱 */
		L"file:Caja de Saltos (%s)",      /* スペイン語: 跳ね回る箱 */
		L"file:경련하는 상자 (%s)",         /* 韓国語: 痙攣する箱 */
		L"file:噴火の幻灯機 (%s)",          /* 中国語: 噴火する幻灯機 */
		L"file:صندوق يرتجف خوفاً (%s)",    /* アラビア語: 恐怖で震える箱 */
		L"file:Бушующая папка (%s)",     /* ロシア語: 荒れ狂うフォルダ */
		L"file:Hüpfendes Archiv (%s)",    /* ドイツ語: ぴょんぴょん跳ねる書庫 */
		L"file:Arquivo em Chamas (%s)",   /* ポルトガル語: 炎上しているアーカイブ */
		L"file:Exploderende Doos (%s)",   /* オランダ語: 爆発寸前の箱 */
		L"file:Wściekły Katalog (%s)",    /* ポーランド語: 激怒したカタログ */
		L"file:Zıplayan Torba (%s)"), sss); /* トルコ語: 跳ねている袋 */
	if (mode == 30) s = LL14(L"file:空の軌跡 The 1st", L"file:Trails in the Sky The 1st", L"file:Les Sentiers du Ciel The 1st", L"file:Trails in the Sky The 1st", L"file:Trails in the Sky The 1st", L"file:하늘의 궤적 The 1st", L"file:空之轨迹 The 1st", L"file:Trails in the Sky The 1st", L"file:Тропы в Небе The 1st", L"file:Himmelsleitern The 1st", L"file:Trails in the Sky The 1st", L"file:Trails in the Sky The 1st", L"file:Trails in the Sky The 1st", L"file:Trails in the Sky The 1st");
	moji(s, 1, 16, 0xffffff);
	if (tc1 < 50)
		s.Format(_T("time:%2d:%02d.%02d/%2d:%02d.%02d"), ta1, tb1, tc1, ta, tb, tc);
	else
		s.Format(_T("time:%2d:%02d %02d/%2d:%02d.%02d"), ta1, tb1, tc1, ta, tb, tc);
	if (ta > 59) {
		if (tc1 < 50)
			s.Format(_T("time:%2d:%02d.%02d/%2d:%02d:%02d"), ta1, tb1, tc1, ta / 60, (ta % 60), tb);
		else
			s.Format(_T("time:%2d:%02d %02d/%2d:%02d:%02d"), ta1, tb1, tc1, ta / 60, (ta % 60), tb);
	}
	if (ta1 > 59) {
		if (tc < 50)
			s.Format(_T("time:%2d:%02d:%02d/%2d:%02d.%02d"), ta1 / 60, (ta1 % 60), tb1, ta, tb, tc);
		else
			s.Format(_T("time:%2d:%02d:%02d/%2d:%02d.%02d"), ta1 / 60, (ta1 % 60), tb1, ta, tb, tc);
	}
	if (ta > 59 && ta1 > 59) {
		s.Format(_T("time:%2d:%02d:%02d/%2d:%02d:%02d"), ta1 / 60, (ta1 % 60), tb1, ta / 60, (ta % 60), tb);
	}
	moji(s, 1, 32, 0xffffff);

	if (rateflg)
		if (videocnt2 > 30) {
			videocnt2 = 0;
			if (prend && ps == 0) {
				int framerate;
				CComQIPtr< IQualProp, &IID_IQualProp > ptr(prend);
				ptr->get_AvgFrameRate(&framerate);
				rate = ((double)framerate) / 100.0;
			}
		}
	videocnt2++;
	videocnt3++;


	CString wavbit1 = wavb(wavbit_sample_Hz);
	const int dispRate = (g_pcm_upscale_active) ? g_ds_pcm_rate : wavbit_sample_Hz;
	const int dispCh = (g_pcm_upscale_active) ? g_ds_pcm_ch : wavchannel;
	const int dispSam = (g_pcm_upscale_active) ? g_ds_pcm_bits : wavsam_depth;
	const CString wavbit1_disp = wavb(dispRate);

	if ((mode == -2 || videoonly) && rate != 0.0 && height != 0) {
		s.Format(_T("size:%d x %d"), rcm.right, rcm.bottom);
		moji(s, 1, 48, 0x7fffff);
		s.Format(_T("rate:%3.3ffps"), rate);
		moji(s, 1, 64, 0x7fffff);
	}
	else if ((mode == -2 || videoonly) && rcm.right > 1) {
		s.Format(_T("size:%d x %d"), rcm.right, rcm.bottom);
		moji(s, 1, 48, 0x7fffff);
		s.Format(LL14(L"rate:算出中……", L"rate:Calculating...", L"rate:Calcul en cours...", L"rate:Calcolo in corso...", L"rate:Calculando...", L"rate:계산 중...", L"rate:计算中……", L"rate:جاري الحساب...", L"rate:Вычисление...", L"rate:Berechnung...", L"rate:A calcular...", L"rate:Berekenen...", L"rate:Obliczanie...", L"rate:Hesaplanıyor..."));
		moji(s, 1, 64, 0x7fffff);
	}
	else if (mode == -2 && wavbit_sample_Hz != 0) {
		s.Format(_T("sample:%sHz"), wavbit1_disp);
		moji(s, 1, 48, 0x7fffff);
		s.Format(_T("channel:%dch"), dispCh);
		if (dispCh == 3)s.Format(_T("channel:%s"), _T("2.1ch"));
		if (dispCh == 4)s.Format(_T("channel:%s"), _T("3.1ch"));
		if (dispCh == 5)s.Format(_T("channel:%s"), _T("4.1ch"));
		if (dispCh == 6)s.Format(_T("channel:%s"), _T("5.1ch"));
		if (dispCh == 7)s.Format(_T("channel:%s"), _T("6.1ch"));
		if (dispCh == 8)s.Format(_T("channel:%s"), _T("7.1ch"));
		moji(s, 1, 64, 0x7fffff);
	}
	else if (mode == -2 && wavbit_sample_Hz == 0) {
		s.Format(LL14(
			L"sample:不明",                   /* 日本語: 厳格に維持 */
			L"sample:Vanished in Clouds",    /* 英語: 雲の中に消えた */
			L"sample:Perdu en Mer",          /* フランス語: 海で遭難した */
			L"sample:Pasta Invisibile",      /* イタリア語: 見えないパスタ */
			L"sample:Memoria Fugada",        /* スペイン語: 逃亡した記憶 */
			L"sample:안개 속의 만두",           /* 韓国語: 霧の中の肉まん */
			L"sample:被外星人抓走",            /* 中国語: 宇宙人に連れ去られた */
			L"sample:ضائع في الصحراء",       /* アラビア語: 砂漠で迷子 */
			L"sample:Украдено медведями",    /* ロシア語: クマに盗まれた */
			L"sample:Verschollene Wurst",    /* ドイツ語: 行方不明のソーセージ */
			L"sample:Segredo do Peixe",      /* ポルトガル語: 魚の秘密 */
			L"sample:Verdwaalde Kaas",       /* オランダ語: 迷子のチーズ */
			L"sample:Zjedzone przez Mole",   /* ポーランド語: 蛾に食べられた */
			L"sample:Kayıp Terlik"));         /* トルコ語: 失踪したスリッパ */
		moji(s, 1, 48, 0x7fffff);
		s.Format(LL14(
			L"sample:不明",                   /* 日本語: 二度目も維持 */
			L"sample:Whistling Ghost",       /* 英語: 口笛を吹く幽霊 */
			L"sample:Oignon Mystère",        /* フランス語: 謎の玉ねぎ */
			L"sample:Enigma del Gelato",     /* イタリア語: ジェラートの謎 */
			L"sample:Sombra con Sombrero",   /* スペイン語: 帽子をかぶった影 */
			L"sample:꿈속의 오징어",            /* 韓国語: 夢の中のイカ */
			L"sample:找不到の炒飯",            /* 中国語: 見つからないチャーハン */
			L"sample:لغز الباذنجان",         /* アラビア語: ナスの謎 */
			L"sample:Шёпот водки",           /* ロシア語: ウォッカの囁き */
			L"sample:Rätselhafte Brezel",    /* ドイツ語: 謎めいたプレッツェル */
			L"sample:Fantasma de Bacalhau",  /* ポルトガル語: 干し鱈の幽霊 */
			L"sample:Fluisterende Tulp",     /* オランダ語: 囁くチューリップ */
			L"sample:Zagadka Pieroga",       /* ポーランド語: ピエロギの謎 */
			L"sample:Sırrı Çözülmemiş Çay"));  /* トルコ語: 謎が解けない茶 */
		moji(s, 1, 64, 0x7fffff);
	}
	else if (mode == -3) {
		s = FormatBannerDataAudioLine();
		moji(s, 1, 48, 0x7fffff);
		sss = kpi;
		s.Format(_T("kpi :%s"), sss.Right(sss.GetLength() - sss.ReverseFind('\\') - 1));
		moji(s, 1, 64, 0x7fffff);
	}
	else if (mode == -8 || mode == -7 || mode == 999) {
		s = FormatBannerDataAudioLine();
		moji(s, 1, 48, 0x7fffff);
		s = "artist:";
		int artiLabelW = moji(s, 1, 64, 0x7fffff);
		int artiValueX = 0, artiViewW = 0;
		BannerValueLayout(artiLabelW, artiValueX, artiViewW);
		BannerScrollResetIfLayoutChanged(1, artiValueX, artiViewW, mcnt4, mcnt3);
		int si = mojisub(tagname, 1, 0, 0x7fffff);
		if (si > artiViewW) {
			int sss_w = si;
			CString base = (mode == -8 || mode == -7) ? tagname : fnn;
			ss = base + _T("　　　");
			si = mojisub(ss, 1, 0, 0x7fffff);
			if (Ms2DrawDue(ms2))
				DrawScrollSepDeco(dcsub, 4 + sss_w, 16 * 4, si - sss_w, RGB(200, 240, 255));
		}
		BannerBlitScrollValue(dc, dcsub, artiValueX, artiViewW, 0 + 64 * 4, (16 + 64) * 4, mcnt4, mcnt3, si);
	}
	else if (mode == -10 || mode == -9) {
		const CString hzPlay = g_pcm_upscale_active ? wavbit1_disp : wavb(si1.dwSamplesPerSec);
		if (Vbr & mode == -10)
			s.Format(_T("data:%3dk(VBR) %sHz %dbit"), (kbps == 0) ? mkps : kbps, hzPlay, dispSam);
		else
			if (mode == -9)
				if (((kbps == 0) ? mkps : kbps) == 0)
					s.Format(_T("data:%sHz %dch %dbit (ALAC)"), hzPlay, dispCh, dispSam);
				else
					if (Vbr)
						s.Format(_T("data:%3dk(VBR) %sHz %dch %dbit (AAC)"), mkps, hzPlay, dispCh, dispSam);
					else
						s.Format(_T("data:%3dk(CBR) %sHz %dch %dbit (AAC)"), mkps, hzPlay, dispCh, dispSam);
			else
				s.Format(_T("data:%3dk %sHz %dbit"), (kbps == 0) ? mkps : kbps, hzPlay, dispSam);
		moji(s, 1, 48, 0x7fffff);
		s = "artist:";
		int artiLabelW = moji(s, 1, 64, 0x7fffff);
		int artiValueX = 0, artiViewW = 0;
		BannerValueLayout(artiLabelW, artiValueX, artiViewW);
		BannerScrollResetIfLayoutChanged(1, artiValueX, artiViewW, mcnt4, mcnt3);
		//			dcsub.FillSolidRect(0,0,3000,30,RGB(1,1,1));
		int si = mojisub(tagname, 1, 0, 0x7fffff);
		if (si > artiViewW) {
			int sss_w = si;
			ss = tagname + _T("　　　");
			si = mojisub(ss, 1, 0, 0x7fffff);
			if (Ms2DrawDue(ms2))
				DrawScrollSepDeco(dcsub, 4 + sss_w, 16 * 4, si - sss_w, RGB(200, 240, 255));
		}
		BannerBlitScrollValue(dc, dcsub, artiValueX, artiViewW, 0 + 64 * 4, (16 + 64) * 4, mcnt4, mcnt3, si);
	}
	else {
		s.Format(_T("Loop:%2d:%02d.%02d %2d:%02d.%02d"), tal1, tbl1, tcl1, tal2, tbl2, tcl2);
		moji(s, 1, 48, 0x7fffff);
		// 固定ピッチ時代の "    :" (Loop とコロン位置合わせ) を、"Loop" 実測幅で再現
		const int loopPrefixW = BannerTextWidthPx(dc, _T("Loop"));
		if (snap_loop1 < 10000000000)
			s.Format(_T(":%10d-%6d"), snap_loop1, snap_loop2);
		if (snap_loop1 < 1000000000)
			s.Format(_T(":%9d-%7d"), snap_loop1, snap_loop2);
		if (snap_loop1 < 100000000)
			s.Format(_T(":%8d-%8d"), snap_loop1, snap_loop2);
		if (snap_loop1 < 10000000)
			s.Format(_T(":%7d-%9d"), snap_loop1, snap_loop2);
		mojiPx(s, 1 * 4 + loopPrefixW, 64, 0x7fefef);
	}
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7 || mode == 999) {
		s = "album:";
		int albuLabelW = moji(s, 1, 80, 0x7fffff);
		int albuValueX = 0, albuViewW = 0;
		BannerValueLayout(albuLabelW, albuValueX, albuViewW);
		BannerScrollResetIfLayoutChanged(2, albuValueX, albuViewW, mcnt6, mcnt5);
		//			dcsub.FillSolidRect(0,0,3000,30,RGB(1,1,1));
		int si = mojisub(tagalbum, 1, 0, 0x7fffff);
		if (si > albuViewW) {
			int sss_w = si;
			ss = tagalbum + _T("　　　");
			si = mojisub(ss, 1, 0, 0x7fffff);
			if (Ms2DrawDue(ms2))
				DrawScrollSepDeco(dcsub, 4 + sss_w, 16 * 4, si - sss_w, RGB(200, 240, 255));
		}
		BannerBlitScrollValue(dc, dcsub, albuValueX, albuViewW, 0 + 80 * 4, (16 + 80) * 4, mcnt6, mcnt5, si);
	}
	else {
		if (tcg < 50)
			s.Format(LL14(L"Loop数:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Boucle:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Bucle:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"循环:%3d G:%3d:%02d.%02d", L"حلقة:%3d G:%3d:%02d.%02d", L"Петля:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Pętla:%3d G:%3d:%02d.%02d", L"Döngü:%3d G:%3d:%02d.%02d"), loopcnt, tag, tbg, tcg);
		else
			s.Format(LL14(L"Loop数:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Boucle:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Bucle:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"循环:%3d G:%3d:%02d %02d", L"حلقة:%3d G:%3d:%02d %02d", L"Петля:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Pętla:%3d G:%3d:%02d %02d", L"Döngü:%3d G:%3d:%02d %02d"), loopcnt, tag, tbg, tcg);
		moji(s, 1, 80, 0xefefef);
	}
	if (ss != s)
	{
		ss = s;
		//			m_11.SetWindowText(s);
	}

	if (!draw_jacket_early && jx != -1 && !img.IsNull() && !g_mpSideJacket) {
		int h_dest = 388;
		int w_dest = (int)(388.0 * jxy);
		if (w_dest <= 0) w_dest = 388;
		int x_dest = 0;
		int y_dest = 0;

		int alpha = 130 + (int)((220 - 130) * m_jacketFocus);
		img.AlphaBlend(dc.m_hDC, x_dest, y_dest, w_dest, h_dest, 0, 0, jx, jy, alpha);
	}
	}

	if (pl && plw) {
		if (pl->m_renzoku.GetCheck()) {
			if (plf == 1 && fade == 0.0f && playy == 1) {
				thn = FALSE;
				fade1 = 1;
			}
		}
		else {
			if (plf == 1 && fade == 0.0f && playy == 1) {
				stop1(); fade = 1;
			}
		}
	}
	else {
		if (plf == 1 && fade == 0.0f && playy == 1) {
			stop1(); fade = 1;
		}
	}

	if (wavExportLoopCount > 0) {
		if (loopcnt >= wavExportLoopCount && wavExportPath.IsEmpty())
			OnButton5();
	}
	else if (pl && plw) {
		if (pl->m_renzoku.GetCheck() == TRUE) {
			// KPI(kb media player)は mode==-3。無限ループで loopcnt が増えないため、
			// 連続再生時のみ2分経過で次曲へ進める。
			if (mode == -3) {
				if (g_kpiRenzokuPnt != pl->pnt) {
					g_kpiRenzokuPnt = pl->pnt;
					g_kpiRenzokuTick = GetTickCount();
				}
				else if (g_kpiRenzokuTick != 0 &&
					(DWORD)(GetTickCount() - g_kpiRenzokuTick) >= KPI_RENZOKU_LIMIT_MS) {
					g_kpiRenzokuTick = 0;   // 次曲へ移るまで再発火させない
					OnButton5();
				}
			}
			else {
				CString s; m_kaisuu.GetWindowText(s);
				int kc = _tstoi(s); if (kc < 1) kc = 1;   // 回数0/空は最低1ループ(即フェード防止)
				if (loopcnt >= kc) OnButton5();
			}
		}
	}

	if (bGdiFrame)
	{
		extern CMediaPlayerDlg* mp;
		const bool mediaHidden = (savedata.playerMode == 1 && mp && ::IsWindow(mp->GetSafeHwnd()) && !IsWindowVisible());
		// メディアプレイヤーモード(メイン非表示)では og を再描画しない。
		// mp は自前タイマーで dc を Blit し pending を解除して合成を継続させる。
		if (!mediaHidden) {
			RECT rect;
			rect.top = 0;
			rect.left = 0;
			rect.bottom = (LONG)((101) * hD * 4);
			rect.right = (LONG)((180 + 88 * 2 + 50) * hD * 4);
			InvalidateRect(&rect, FALSE);
			// この後 og の OnPaint が ms2=0 リセットと pending 解除を行う(通常モード)。
		}
		else {
			// メディアプレイヤーモードでは og の OnPaint が走らないため、ここで
			// フレームを「消費」する。ms2 カウンタを 0 に戻さないと伸び続け、
			// Ms2DrawDue が常時真になって簡易ピアノロール等が ms2 設定を無視し 60fps で
			// 描画され重くなる(=ファルコム特化型では起きない現象)。OnPaint と同じ扱いにする。
			ms2 = 0;
			// 新フレームができた時(=ms2レート)だけ mp のバナーを再描画させる。
			// mp の OnPaint が dc を Blit し pending を解除する。これで mp 側の Blit も
			// ms2 レートになり(60fps常時 Blit を避け)ファルコム特化型と同等の負荷になる。
			if (mp) mp->InvalidateRect(&mp->m_bannerRect, FALSE);
		}
		InterlockedExchange(&g_gdiPaintPending, 1);
	}
	//音量
	//	if(tt>=4){
	float vol = (float)m_sl.GetPos();
	vol /= 1000.0f;
	// 毎 tick の waveOut/EndpointVolume 呼び出しは、他プレイヤと違い再生中 UI を独占しやすい。
	// 値が変わったときだけ適用（ユーザー操作時の音量自体は同じ）。
	if (plf == 1) {
		const bool useEndpoint = (deve != NULL && audio != NULL);
		if (s_lastAppliedVol < 0.0f || s_lastUsedEndpoint != useEndpoint
			|| fabsf(vol - s_lastAppliedVol) > 1e-6f) {
			if (!useEndpoint) {
				WORD leftv = (WORD)(0xFFFF * vol);
				WORD rightv = (WORD)(0xFFFF * vol);
				waveOutSetVolume(hwo, MAKELONG(leftv, rightv));
			}
			else {
				// GUID_OggMasterVolCtx: 自分の変更通知を OnNotify で無視
				audio->SetMasterVolumeLevelScalar(vol / 100.0f, &GUID_OggMasterVolCtx);
			}
			s_lastAppliedVol = vol;
			s_lastUsedEndpoint = useEndpoint;
		}
	}
	if (deve)
		s.Format(_T("%.1f%%"), vol);
	else
		s.Format(_T("%.1f%%"), vol * 100.0f);
	m_vol.GetWindowText(ss);
	if (s != ss)
		m_vol.SetWindowText(s);
	//時間表示
	if (pMediaPosition && ((mode == -2 && hsc == 0) || ((mode > 0 || mode < -10) && videoonly == TRUE && hsc == 0))) {
		REFTIME aa;
		pMediaPosition->get_CurrentPosition(&aa);
		m_time.SetPos((int)(aa * 100));
		if (ptl) {
			ptl->SetProgressState(m_hWnd, TBPF_NORMAL);
			ptl->SetProgressValue(m_hWnd, (LONGLONG)aa, (LONGLONG)aa1);
		}
	}
	else if (plf && hsc == 0) {
		__int64 pb;
		int ogs;
		{
			std::lock_guard<std::mutex> lk(cl2);
			pb = playb;
			ogs = oggsize;
		}
		// スライダーも時間表示と同じく実再生位置（DS 先読み分を除去）に揃える。
		if (qSamplesHeard > 0 && pb > qSamplesHeard) pb -= qSamplesHeard;
		else if (qSamplesHeard > 0) pb = 0;
		if (mode == -10) {
			m_time.SetPos((int)(pb / 100));
			if (ptl) {
				ptl->SetProgressState(m_hWnd, TBPF_NORMAL);
				ptl->SetProgressValue(m_hWnd, (LONGLONG)pb, (LONGLONG)ogs);
			}
		}
		else {
			m_time.SetPos((int)pb);
			if (ptl) {
				ptl->SetProgressState(m_hWnd, TBPF_NORMAL);
				ptl->SetProgressValue(m_hWnd, (LONGLONG)pb, (LONGLONG)ogs / (wavsam_depth / 4));
			}
		}
	}
	// MP: playb 更新と同じ UI ターンでシークを追従(Timer3 の遅延 Invalidate を避ける)
	if (savedata.playerMode == 1) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()) && ::IsWindowVisible(mp->GetSafeHwnd()))
			mp->MirrorSeekVol();
	}

	tempo = m_tempo_sl.GetPos();
	float te = (float)tempo;
	if (te >= 200.0f) {
		te -= 100.0f;
	}
	else {
		te = te / 3.0f + 33.3f;
	}
	pitch = m_pitch_sl.GetPos();
	float pi = (float)pitch;
	if (pi >= 200.0f) {
		pi -= 100.0f;
	}
	else {
		pi = pi / 3.0f + 33.3f;
	}
	s.Format(L"%3d%%", (int)te);
	m_temp_num.SetWindowText(s);
	s.Format(L"%3d%%", (int)pi);
	m_pitch.SetWindowText(s);
	pitch = m_pitch_sl.GetPos();

	tt = 0;
	//	}
	//	m_time.Invalidate();

	savedata.kakuVol = m_kakuVol.GetPos();
	savedata.kakuVal = savedata.kakuVol;
	s.Format(_T("%.1f%%"), (double)savedata.kakuVal);
	m_kakuVolval.SetWindowText(s);

	//ランダム演奏用
	if (randomf) {
		if (playf == 0) {//演奏が止まっている
			switch (savedata.random) {
			case 0://ランダム
			{
				//					fnn="ランダム演奏中";
				if (m_yso.GetCheck() == 0 && m_ys6.GetCheck() == 0 && m_ysf.GetCheck() == 0 && m_ed6fc.GetCheck() == 0 && m_ed6sc.GetCheck() == 0 && m_ed6tc.GetCheck() == 0 && m_zweiii.GetCheck() == 0 && m_ysc1.GetCheck() == 0 && m_ysc2.GetCheck() == 0 && m_xa.GetCheck() == 0 && m_ys121.GetCheck() == 0 && m_ys122.GetCheck() == 0 && m_sor.GetCheck() == 0 && m_zwei.GetCheck() == 0 && m_gurumin.GetCheck() == 0 && m_dino.GetCheck() == 0 && m_br4.GetCheck() == 0 && m_ed3.GetCheck() == 0 && m_ed4.GetCheck() == 0 && m_ed5.GetCheck() == 0) { randomf = 0; break; }
				for (;;) {
					int a, b; CString s;
					CString ex;
					char buffer[_MAX_DIR];

					a = rand() % 20;
					if (a == 0 && m_ys6.GetCheck()) {
						b = rand() % 30 + 1; filen.Format(_T("Ys6_%02d.ogg"), b); Citiran_YS6 a; a.Gett(b - 1);
						modesub = 4; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 1 && m_ysf.GetCheck()) {
						b = rand() % 34 + 1; if (b == 22)filen.Format(_T("y3bg22a.ogg")); else filen.Format(_T("y3bg%02d.ogg"), b); Citiran_YSF a; a.Gett(b - 1);
						modesub = 3; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 2 && m_ed6fc.GetCheck()) {
						Citiran_FC a;
						b = rand() % 55; s = a.Gett(b); filen.Format(_T("ED6%s.ogg"), s.Left(3));
						modesub = 2; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 3 && m_ed6sc.GetCheck()) {
						itiran a;
						b = rand() % 97; s = a.Gett(b); filen.Format(_T("ED6%s.ogg"), s.Left(3));
						modesub = 1; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 4 && m_yso.GetCheck()) {
						Citiran_YSO a;
						b = rand() % 40; s = a.Gett(b); filen.Format(_T("YSO_%s.ogg"), s.Left(3));
						modesub = 5; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 5 && m_ed6tc.GetCheck()) {
						CED63rd a;
						b = rand() % 141; s = a.Gett(b); filen.Format(_T("ED6%s.ogg"), s.Left(3));
						modesub = 6; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 6 && m_zweiii.GetCheck()) {
						CZWEIII a;
						b = rand() % 65; s = a.Gett(b); filen.Format(_T("ZW2_%s.ogg"), s.Left(3));
						modesub = 7; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 7 && m_ysc1.GetCheck()) {
						CYsC1 a;
						b = rand() % 72; s = a.Gett(b); filen.Format(_T("%s.ogg"), s);
						modesub = 8; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 8 && m_ysc2.GetCheck()) {
						CYsC2 a;
						b = rand() % 92; s = a.Gett(b); filen.Format(_T("%s.ogg"), s);
						modesub = 9; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 9 && m_xa.GetCheck()) {
						CXA a;
						b = rand() % 24; s = a.Gett(b); filen.Format(_T("XANA%s.dec"), s.Left(3)); loop1 = a.loop1; loop2 = a.loop2 - loop1;
						modesub = 10; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 10 && m_ys121.GetCheck()) {
						CYs12_1 a;
						_getcwd(buffer, _MAX_DIR);
						_tchdir(savedata.ys12);
						if (_chdir("wave\\wave_44") == -1) {
							if (_chdir("wave\\wave_22") == -1) { break; }
							ex = "_22";
						}
						else ex = "_44";
						b = rand() % 24; s = a.Gett(b); filen.Format(_T("%s%s.wav"), s, ex);
						CString sf;
						sf.Format(_T("%s%s.pos"), s, ex);
						loop1 = loop2 = 0;
						struct a_ {
							int l1;
							int l2;
						};
						a_ aa;
						CFile f;
						if (f.Open(sf, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
							f.Read(&aa, 8);
							f.Close();
							if (aa.l1 == 0)aa.l2 = 0;
							loop1 = aa.l1;
							loop2 = aa.l2 - loop1;
						}
						_chdir(buffer);
						modesub = 11; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 11 && m_ys122.GetCheck()) {
						CYs12_2 a;
						_getcwd(buffer, _MAX_DIR);
						_tchdir(savedata.ys122);
						if (_chdir("wave\\wave_44") == -1) {
							if (_chdir("wave\\wave_22") == -1) { break; }
							ex = "_22";
						}
						else ex = "_44";
						b = rand() % 31; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
						CString sf;
						sf.Format(_T("%s.pos"), s);
						loop1 = loop2 = 0;
						struct a_ {
							int l1;
							int l2;
						};
						a_ aa;
						CFile f;
						if (f.Open(sf, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
							f.Read(&aa, 8);
							f.Close();
							if (aa.l1 == 0)aa.l2 = 0;
							loop1 = aa.l1;
							loop2 = aa.l2 - loop1;
						}
						_chdir(buffer);
						modesub = 12; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 12 && m_sor.GetCheck()) {
						CSor a;
						_getcwd(buffer, _MAX_DIR);
						_tchdir(savedata.sor);
						if (_chdir("WAVE\\WAVE44") == -1) {
							if (_chdir("WAVE\\WAVE22") == -1) { break; }
							ex = "_22";
						}
						else ex = "_44";
						b = rand() % 76; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
						CStdioFile f;
						CString sf;
						if (ex == "_22")
							sf = "..\\..\\WAVE_CD.DAT";
						else
							sf = "..\\..\\WAVE_DVD.DAT";
						loop1 = loop2 = 0;
						if (f.Open(sf, CFile::modeRead | CFile::typeText | CFile::shareDenyWrite, NULL)) {
							for (int j = 0;; j++) {
								f.ReadString(sf);
								if (sf.Left(6) == s)break;
							}
							f.Close();
							CString sff;
							sff = sf.Mid(8, 10);//開始ms
							if (sf.Right(1) == "N") {
								loop1 = loop2 = 0;
							}
							else {
								float a, b;
								if (ex == "_22") b = 22.05f; else b = 44.1f;
								loop1 = _tstoi(sff);
								loop2 = _tstoi(sf.Mid(18, 10));
								a = (float)loop1;
								a = a * b;
								loop1 = (int)a;
								a = (float)loop2;
								a = a * b;
								loop2 = (int)a - loop1;
							}
						}
						_chdir(buffer);
						modesub = 13; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 13 && m_zwei.GetCheck()) {
						CZwei a;
						b = rand() % 36; s = a.Gett(b); filen.Format(_T("%s(wav.dat)"), s);
						modesub = 14; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 14 && m_gurumin.GetCheck()) {
						CGurumin a;
						b = rand() % 39; s = a.Gett(b); filen.Format(_T("%s.de2"), s); loop1 = a.loop1; loop2 = a.loop2 - loop1;
						modesub = 15; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 15 && m_dino.GetCheck()) {
						CDino a;
						b = rand() % 33; s = a.Gett(b); filen.Format(_T("%s(bgm.arc)"), s); loop1 = a.loop1; loop2 = a.loop2;
						modesub = 16; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 16 && m_br4.GetCheck()) {
						CBr4 a;
						b = rand() % 42; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
						modesub = 17; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 17 && m_ed3.GetCheck()) {
						CED3 a;
						b = rand() % 67; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
						modesub = 18; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 18 && m_ed4.GetCheck()) {
						CED4 a;
						b = rand() % 66; if (b == 1 || b == 2) b = 0; s = a.Gett(b); filen.Format(_T("%s"), s);
						modesub = 19; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					if (a == 19 && m_ed5.GetCheck()) {
						CED5 a;
						b = rand() % 98; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
						modesub = 20; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
				}
			}
			break;
			case 1://順次
				//				fnn="順次演奏中";
				if (m_yso.GetCheck() == 0 && m_ys6.GetCheck() == 0 && m_ysf.GetCheck() == 0 && m_ed6fc.GetCheck() == 0 && m_ed6sc.GetCheck() == 0 && m_ed6tc.GetCheck() == 0 && m_zweiii.GetCheck() == 0 && m_ysc1.GetCheck() == 0 && m_ysc2.GetCheck() == 0 && m_xa.GetCheck() == 0 && m_ys121.GetCheck() == 0 && m_ys122.GetCheck() == 0 && m_sor.GetCheck() == 0 && m_zwei.GetCheck() == 0 && m_gurumin.GetCheck() == 0 && m_dino.GetCheck() == 0 && m_br4.GetCheck() == 0 && m_ed3.GetCheck() == 0 && m_ed4.GetCheck() == 0 && m_ed5.GetCheck() == 0) { randomf = 0; break; }
				for (;;) {
					randomno++;
					int b; CString s;
					CString ex;
					char buffer[_MAX_DIR];
					if (randomno < 30 + 1) {
						if (!m_ys6.GetCheck())continue;
						b = randomno; filen.Format(_T("Ys6_%02d.ogg"), b); Citiran_YS6 a; a.Gett(b - 1);
						modesub = 4; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
						break;
					}
					else
						if (randomno < 34 + 30 + 1) {
							if (!m_ysf.GetCheck())continue;
							b = randomno - 30; if (b > 34)b = 34; if (b == 22)filen.Format(_T("y3bg22a.ogg")); else filen.Format(_T("y3bg%02d.ogg"), b); Citiran_YSF a; a.Gett(b - 1);
							modesub = 3; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
							break;
						}
						else
							if (randomno < 56 + 34 + 30 + 1) {
								if (!m_ed6fc.GetCheck())continue; Citiran_FC a;
								b = randomno - 34 - 31; s = a.Gett(b); filen.Format(_T("ED6%s.ogg"), s.Left(3));
								modesub = 2; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
								break;
							}
							else
								if (randomno < 98 + 56 + 34 + 30 + 1) {
									if (!m_ed6sc.GetCheck())continue; itiran a;
									b = randomno - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("ED6%s.ogg"), s.Left(3));
									modesub = 1; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
									break;
								}
								else
									if (randomno < 40 + 98 + 56 + 34 + 30 + 1) {
										if (!m_yso.GetCheck())continue; Citiran_YSO a;
										b = randomno - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("YSO_%s.ogg"), s.Left(3));
										modesub = 5; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
										break;
									}
									else
										if (randomno < 141 + 40 + 98 + 56 + 34 + 30 + 1) {
											if (!m_ed6tc.GetCheck())continue; CED63rd a;
											b = randomno - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("ED6%s.ogg"), s.Left(3));
											modesub = 6; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
											break;
										}
										else
											if (randomno < 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
												if (!m_zweiii.GetCheck())continue; CZWEIII a;
												b = randomno - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("ZW2_%s.ogg"), s.Left(3));
												modesub = 7; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
												break;
											}
											else
												if (randomno < 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
													if (!m_ysc1.GetCheck())continue; CYsC1 a;
													b = randomno - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s.ogg"), s);
													modesub = 8; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
													break;
												}
												else
													if (randomno < 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
														if (!m_ysc2.GetCheck())continue; CYsC2 a;
														b = randomno - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s.ogg"), s);
														modesub = 9; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
														break;
													}
													else
														if (randomno < 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
															if (!m_xa.GetCheck())continue; CXA a;
															b = randomno - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("XANA%s.dec"), s.Left(3)); loop1 = a.loop1; loop2 = a.loop2 - loop1;
															modesub = 10; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
															break;
														}
														else
															if (randomno < 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																if (!m_ys121.GetCheck())continue; CYs12_1 a;
																_getcwd(buffer, _MAX_DIR);
																_tchdir(savedata.ys12);
																if (_chdir("wave\\wave_44") == -1) {
																	if (_chdir("wave\\wave_22") == -1) { break; }
																	ex = "_22";
																}
																else ex = "_44";
																b = randomno - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s%s.wav"), s, ex);
																CString sf;
																sf.Format(_T("%s%s.pos"), s, ex);
																loop1 = loop2 = 0;
																struct a_ {
																	int l1;
																	int l2;
																};
																a_ aa;
																CFile f;
																if (f.Open(sf, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
																	f.Read(&aa, 8);
																	f.Close();
																	if (aa.l1 == 0)aa.l2 = 0;
																	loop1 = aa.l1;
																	loop2 = aa.l2 - loop1;
																}
																_chdir(buffer);
																modesub = 11; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																break;
															}
															else
																if (randomno < 31 + 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																	if (!m_ys122.GetCheck())continue; CYs12_2 a;
																	_getcwd(buffer, _MAX_DIR);
																	_tchdir(savedata.ys122);
																	if (_chdir("wave\\wave_44") == -1) {
																		if (_chdir("wave\\wave_22") == -1) { break; }
																		ex = "_22";
																	}
																	else ex = "_44";
																	b = randomno - 24 - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
																	CString sf;
																	sf.Format(_T("%s.pos"), s);
																	loop1 = loop2 = 0;
																	struct a_ {
																		int l1;
																		int l2;
																	};
																	a_ aa;
																	CFile f;
																	if (f.Open(sf, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
																		f.Read(&aa, 8);
																		f.Close();
																		if (aa.l1 == 0)aa.l2 = 0;
																		loop1 = aa.l1;
																		loop2 = aa.l2 - loop1;
																	}
																	_chdir(buffer);
																	modesub = 12; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																	break;
																}
																else
																	if (randomno < 76 + 31 + 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																		if (!m_sor.GetCheck())continue; CSor a;
																		char buffer[_MAX_DIR];
																		_getcwd(buffer, _MAX_DIR);
																		_tchdir(savedata.sor);
																		if (_chdir("WAVE\\WAVE44") == -1) {
																			if (_chdir("WAVE\\WAVE22") == -1) { break; }
																			ex = "_22";
																		}
																		else ex = "_44";
																		b = randomno - 31 - 24 - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
																		CStdioFile f;
																		CString sf;
																		if (ex == "_22")
																			sf = "..\\..\\WAVE_CD.DAT";
																		else
																			sf = "..\\..\\WAVE_DVD.DAT";
																		loop1 = loop2 = 0;
																		if (f.Open(sf, CFile::modeRead | CFile::typeText | CFile::shareDenyWrite, NULL)) {
																			for (int j = 0;; j++) {
																				f.ReadString(sf);
																				if (sf.Left(6) == s)break;
																			}
																			f.Close();
																			CString sff;
																			sff = sf.Mid(8, 10);//開始ms
																			if (sf.Right(1) == "N") {
																				loop1 = loop2 = 0;
																			}
																			else {
																				float a, b;
																				if (ex == "_22") b = 22.05f; else b = 44.1f;
																				loop1 = _tstoi(sff);
																				loop2 = _tstoi(sf.Mid(18, 10));
																				a = (float)loop1;
																				a = a * b;
																				loop1 = (int)a;
																				a = (float)loop2;
																				a = a * b;
																				loop2 = (int)a - loop1;
																			}
																		}
																		_chdir(buffer);
																		modesub = 13; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																		break;
																	}
																	else
																		if (randomno < 36 + 76 + 31 + 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																			if (!m_zwei.GetCheck())continue; CZwei a;
																			b = randomno - 76 - 31 - 24 - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s(wav.dat)"), s);
																			modesub = 14; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																			break;
																		}
																		else
																			if (randomno < 39 + 36 + 76 + 31 + 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																				if (!m_gurumin.GetCheck())continue; CGurumin a;
																				b = randomno - 36 - 76 - 31 - 24 - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s.de2"), s); loop1 = a.loop1; loop2 = a.loop2 - loop1;
																				modesub = 15; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																				break;
																			}
																			else
																				if (randomno < 33 + 39 + 36 + 76 + 31 + 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																					if (!m_dino.GetCheck())continue; CDino a;
																					b = randomno - 39 - 36 - 76 - 31 - 24 - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s(bgm.arc)"), s); loop1 = a.loop1; loop2 = a.loop2;
																					modesub = 16; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																					break;
																				}
																				else
																					if (randomno < 42 + 33 + 39 + 36 + 76 + 31 + 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																						if (!m_br4.GetCheck())continue; CBr4 a;
																						b = randomno - 33 - 39 - 36 - 76 - 31 - 24 - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
																						modesub = 17; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																						break;
																					}
																					else
																						if (randomno < 67 + 42 + 33 + 39 + 36 + 76 + 31 + 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																							if (!m_ed3.GetCheck())continue; CED3 a;
																							b = randomno - 42 - 33 - 39 - 36 - 76 - 31 - 24 - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
																							modesub = 18; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																							break;
																						}
																						else
																							if (randomno < 66 + 67 + 42 + 33 + 39 + 36 + 76 + 31 + 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																								if (!m_ed4.GetCheck())continue; CED4 a;
																								b = randomno - 67 - 42 - 33 - 39 - 36 - 76 - 31 - 24 - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; if (b == 1 || b == 2) { b = 3; randomno += 2; } s = a.Gett(b); filen.Format(_T("%s"), s);
																								modesub = 19; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																								break;
																							}
																							else
																								if (randomno < 98 + 66 + 67 + 42 + 33 + 39 + 36 + 76 + 31 + 24 + 24 + 92 + 72 + 65 + 141 + 40 + 98 + 56 + 34 + 30 + 1) {
																									if (!m_ed5.GetCheck())continue; CED5 a;
																									b = randomno - 66 - 67 - 42 - 33 - 39 - 36 - 76 - 31 - 24 - 24 - 92 - 72 - 65 - 141 - 40 - 98 - 56 - 34 - 30 - 1; s = a.Gett(b); filen.Format(_T("%s.wav"), s);
																									modesub = 20; ret2 = b; b = m_dou.GetCheck(); m_dou.SetCheck(0); playf = 1; play(); m_dou.SetCheck(b);
																									break;
																								}
																								else
																									randomno = 0;
				}
				break;
			}
		}
		else {
			if (wavExportLoopCount > 0) {
				if (loopcnt >= wavExportLoopCount && wavExportPath.IsEmpty())
					OnButton5();
			}
			else {
				CString s; m_kaisuu.GetWindowText(s);
				int kc = _tstoi(s); if (kc < 1) kc = 1;   // 回数0/空は最低1ループ(即フェード防止)
				if (loopcnt >= kc) OnButton5();
			}
		}
	}

	savedata.dsvol = m_dsval.GetPos();
	if (savedata.dsvol == 0)savedata.dsvol = 1;
	s.Format(_T("%.1f%%"), (savedata.dsvol + 499) * 2.0 / 10.0);
	m_dsvols.GetWindowText(ss);
	if (s != ss)
		m_dsvols.SetWindowText(s);

	// DirectShow(KPI mode==-2) など通知スレッド依存が弱い経路でも
	// 曲ごとパラメータの保存/復元が動くよう、メインスレッドからも同期する。
	if (savedata.saveSongParams && plf)
		SongParams_Sync(false);

	if (drawth == TRUE) return;
	if (m_dsb && thn1 == FALSE) {
		if (savedata.dsvol == -498)
			m_dsb->SetVolume(DSBVOLUME_MIN);
		else
			m_dsb->SetVolume((savedata.dsvol - 1) * 7);
	}
	if (drawth == TRUE) return;
	if (pBasicAudio) {
		if (savedata.dsvol == -498)
			pBasicAudio->put_Volume(-10000);
		else
			pBasicAudio->put_Volume((savedata.dsvol - 1) * 7);
	}

}

int timerr = 0;

static unsigned __stdcall COgg_TimerpVsyncThreadProc(void* pParam)
{
	COggDlg* pDlg = (COggDlg*)pParam;
	if (!pDlg || !pDlg->m_hTimerpVsyncStopEvent)
		return 0;

	HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
	typedef HRESULT(WINAPI* PFN_DwmFlush)(void);
	typedef HRESULT(WINAPI* PFN_DwmIsCompositionEnabled)(BOOL*);
	PFN_DwmFlush pfnDwmFlush = (hDwm) ? (PFN_DwmFlush)GetProcAddress(hDwm, "DwmFlush") : nullptr;
	PFN_DwmIsCompositionEnabled pfnDwmIsCompositionEnabled =
		(hDwm) ? (PFN_DwmIsCompositionEnabled)GetProcAddress(hDwm, "DwmIsCompositionEnabled") : nullptr;

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	LARGE_INTEGER lastEmit;
	QueryPerformanceCounter(&lastEmit);
	const double kPeriod = 1.0 / 60.0;

	for (;;) {
		if (WaitForSingleObject(pDlg->m_hTimerpVsyncStopEvent, 0) == WAIT_OBJECT_0)
			break;

		BOOL comp = FALSE;
		const BOOL haveDwm = (pfnDwmIsCompositionEnabled != nullptr && pfnDwmFlush != nullptr
			&& SUCCEEDED(pfnDwmIsCompositionEnabled(&comp)) && comp);

		if (haveDwm)
			pfnDwmFlush();
		else {
			LARGE_INTEGER nowSpin;
			QueryPerformanceCounter(&nowSpin);
			const double dtSpin = (double)(nowSpin.QuadPart - lastEmit.QuadPart) / (double)freq.QuadPart;
			if (dtSpin < kPeriod) {
				DWORD ms = (DWORD)((kPeriod - dtSpin) * 1000.0 + 0.999);
				if (ms < 1) ms = 1;
				Sleep(ms);
			}
		}

		LARGE_INTEGER now2;
		QueryPerformanceCounter(&now2);
		const double elapsed = (double)(now2.QuadPart - lastEmit.QuadPart) / (double)freq.QuadPart;
		if (elapsed < kPeriod * 0.95)
			continue;

		lastEmit = now2;
	}

	if (hDwm)
		FreeLibrary(hDwm);
	return 0;
}

void COggDlg::StartTimerpVsyncThread()
{
	// TheadLoop が timerp を駆動。DwmFlush のみの補助スレッドは DWM/描画と競合するため起動しない。
	return;
	if (m_hTimerpVsyncThread)
		return;
	m_hTimerpVsyncStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!m_hTimerpVsyncStopEvent)
		return;
	unsigned tid = 0;
	m_hTimerpVsyncThread = (HANDLE)_beginthreadex(NULL, 0, COgg_TimerpVsyncThreadProc, this, 0, &tid);
	if (!m_hTimerpVsyncThread) {
		CloseHandle(m_hTimerpVsyncStopEvent);
		m_hTimerpVsyncStopEvent = NULL;
	}
}

void COggDlg::StopTimerpVsyncThread()
{
	if (m_hTimerpVsyncStopEvent)
		SetEvent(m_hTimerpVsyncStopEvent);
	if (m_hTimerpVsyncThread) {
		WaitForSingleObject(m_hTimerpVsyncThread, 20000);
		CloseHandle(m_hTimerpVsyncThread);
		m_hTimerpVsyncThread = NULL;
	}
	if (m_hTimerpVsyncStopEvent) {
		CloseHandle(m_hTimerpVsyncStopEvent);
		m_hTimerpVsyncStopEvent = NULL;
	}
}

LRESULT COggDlg::OnTimerpVsyncTick(WPARAM, LPARAM)
{
	InterlockedExchange(&g_timerpPosted, 0);
	if (!IsWindow(GetSafeHwnd()))
		return 0;
	timerp();
	return 0;
}

LRESULT COggDlg::OnSpeanaTick(WPARAM, LPARAM)
{
	// スペアナは timerp 内で XOR バナー文字より前に同期描画するようになった。
	// ここで（OnPaint 後に）描くと棒が文字を上書きして XOR が無効化されるため描画しない。
	InterlockedExchange(&g_speanaPosted, 0);
	return 0;
}

LRESULT COggDlg::OnEndpointVolume(WPARAM wParam, LPARAM)
{
	// Windows 側の主音量(エンドポイント)変更をスライダー／表示に反映。
	if (!deve || !audio || !::IsWindow(m_hWnd)) return 0;
	DWORD bits = (DWORD)wParam;
	float lv = 0.0f;
	memcpy(&lv, &bits, sizeof(lv));
	if (lv < 0.0f) lv = 0.0f;
	if (lv > 1.0f) lv = 1.0f;
	m_sl.SetRange(0, 100000);
	m_sl.SetPos((int)(lv * 100000.0f + 0.5f));
	s_lastAppliedVol = lv * 100.0f; // timerp の deve 時単位(0〜100)に合わせ再適用を抑止
	s_lastUsedEndpoint = true;
	CString s;
	s.Format(_T("%.1f%%"), lv * 100.0f);
	m_vol.SetWindowText(s);
	return 0;
}

DWORD t1 = 0, t2 = 0, t3 = 0, t4 = 0, t5 = 0, dt = 0;
DWORD fpstiming = 0;
int timec = 0;

// タイミング処理
DWORD GetTiming(DWORD f)
{
	DWORD frate_ = 1;
	if (f < 10000 / 60 + 1) frate_ = 10;
	if (f < 9000 / 60 + 1) frate_ = 9;
	if (f < 8000 / 60 + 1) frate_ = 8;
	if (f < 7000 / 60 + 1) frate_ = 7;
	if (f < 6000 / 60 + 1) frate_ = 6;
	if (f < 5000 / 60 + 1) frate_ = 5;
	if (f < 67) frate_ = 4;
	if (f < 51) frate_ = 3;
	if (f < 34) frate_ = 2;
	if (f <= 17) frate_ = 1;
	return frate_;

}


DWORD timing(BOOL t)
{
	//60fpsならこんな感じ（1/60秒=50/3ミリ秒）
	if (t) { t2 = timeGetTime(); return 0; }
	t4 = timeGetTime();
	t5 = t4 - t2;
	for (;;) {
		t1 = timeGetTime();
		dt = (t1 - t2) * 3 + t3;
		if (dt >= 249) {//12
			t3 = dt % 250;
			return 5;
		}
		if (dt >= 199) {//15
			t3 = dt % 200;
			return 4;
		}
		if (dt >= 99) {//30
			t3 = dt % 100;
			return 2;
		}
		if (dt >= 49) {//60
			t3 = dt % 50;
			return 1;
		}
	}
}
DWORD Timing64(DWORD& Frate, BOOL bl)
{
	LARGE_INTEGER time_before;
	DWORD ss, FrateL = Frate;
	QueryPerformanceCounter(&time_before);
	Frate = (DWORD)(time_before.QuadPart * 1000 / freq.QuadPart);
	ss = (DWORD)((int)Frate - (int)FrateL); return ss;
	return 0;
}
DWORD Timing(DWORD& Frate, BOOL bl)
{
	DWORD ss, FrateL = Frate;
	Frate = timeGetTime();
	ss = Frate - FrateL;
	return ss;
}

DWORD timetb[3] = { 16,16,17 };

void timing1(WORD a, BOOL b, BOOL c)
{
		DWORD fr2 = 0;
		if (b) {
			Timing(fpstiming, FALSE);
		}
		DWORD j = 0;
		for (WORD ii = 0; ii < a; ii++) {
			j += timetb[timec]; timec++; if (timec > 2)timec = 0;
		}
		Timing64(fr2, FALSE);
		// DWORD 差分で比較(signed cast だと QPC ms 折り返しで永久スピンし得る)
		const DWORD target = fpstiming + j;
		if ((DWORD)(target - fr2) > 10 && (int)(target - fr2) > 0) {
			Sleep(5);
		}
		// Sleep(0) の ABOVE_NORMAL ビジー待機は UI を飢餓させる。上限付き Sleep(1)。
		const DWORD t0 = fr2;
		for (;;) {
			Timing64(fr2, FALSE);
			if ((int)(fr2 - target) >= 0) break;
			if ((DWORD)(fr2 - t0) > j + 50) break; // 異常時でも抜けられるように
			Sleep(1);
		}
}

DWORD f1 = 0, f2 = 0;

UINT TheadLoop(LPVOID)
{
	int infoScrollDiv = 0;   // 60fps÷2 = 30fps で info パネルスクロール tick を投げる
	for (;;) {
		if (drawth == TRUE) return TRUE;
		Timing64(f2, FALSE);
		COgg_RequestTimerp(og);

		// info パネルスクロール: TheadLoop の 60fps をそのまま使い、1フレームおきに
		// PostMessage することで ~30fps を実現。多重 Post は CAS で合流。
		if (++infoScrollDiv >= 2) {
			infoScrollDiv = 0;
			extern CMediaPlayerDlg* mp;
			if (mp) {
				HWND hmp = mp->GetSafeHwnd();
				if (::IsWindow(hmp) && mp->m_iscActive
					&& InterlockedCompareExchange(&mp->m_iscScrollPosted, 1, 0) == 0) {
					if (!::PostMessage(hmp, WM_MP_INFO_SCROLL, 0, 0))
						InterlockedExchange(&mp->m_iscScrollPosted, 0);
				}
			}
		}

		timing1(1, FALSE, FALSE);
		Timing64(f2, FALSE);
		Timing64(fpstiming, FALSE);
		Sleep(0);
	}
}

int SC1 = 0;
int ip = 0;
int aaaa = 0, aaaa1 = 0;

void timerog(UINT nIDEvent);
void timerog1(UINT nIDEvent);
void timerog1(UINT nIDEvent)
{
	if (nIDEvent == 59877) {
		og->KillTimer(59877);
		if (savedata.eqwindow == 1) {
			if (!::IsWindow(og->m_EqualizerDlg->GetSafeHwnd()))
			{
				og->m_EqualizerDlg->Create(IDD_EQUALIZER, og);
			}

			og->m_EqualizerDlg->ShowWindow(SW_SHOW);
		}
		if (savedata.pianorollwindow == 1) {
			if (!::IsWindow(og->m_PianoRollDlg->GetSafeHwnd()))
			{
				og->m_PianoRollDlg->Create(IDD_PIANOROLL, og);
			}

			og->m_PianoRollDlg->ShowWindow(SW_SHOW);
		}
		if (savedata.prTunewindow == 1) {
			if (!::IsWindow(og->m_PianoRollTuneDlg->GetSafeHwnd()))
			{
				if (!og->m_PianoRollTuneDlg->Create(IDD_PIANOROLL_TUNE, og))
					savedata.prTunewindow = 0;
			}
			if (::IsWindow(og->m_PianoRollTuneDlg->GetSafeHwnd()))
				og->m_PianoRollTuneDlg->ShowWindow(SW_SHOW);
		}
		if (savedata.analyzerwindow == 1) {
			// 親作成直後の同期 Create は避ける(CreateDialog ネスト対策)。
			// タイマー発火時点では親は既に生きているのでここは安全。
			if (!::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()))
			{
				if (!og->m_AnalyzerDlg->Create(IDD_ANALYZER, og))
					savedata.analyzerwindow = 0;
			}
			if (::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()))
				og->m_AnalyzerDlg->ShowWindow(SW_SHOW);
		}

	}

	if (nIDEvent == 4923) {
		og->KillTimer(4923);
		// このタイマーは遅延発火するため、保留中に他ウィンドウへフォーカスが
		// 移ると、解除処理の後から発火してホットキーを再登録してしまう。
		// 自プロセスが前面でない場合は登録せず、確実に解除しておく。
		{
			HWND fg = ::GetForegroundWindow();
			DWORD fgpid = 0;
			::GetWindowThreadProcessId(fg, &fgpid);
			if (fgpid != ::GetCurrentProcessId()) {
				UnregisterHotKey(og->GetSafeHwnd(), ID_HOTKEY0);
				UnregisterHotKey(og->GetSafeHwnd(), ID_HOTKEY1);
				UnregisterHotKey(og->GetSafeHwnd(), ID_HOTKEY2);
				UnregisterHotKey(og->GetSafeHwnd(), ID_HOTKEY3);
				return;
			}
		}
		if (ip != 0) return;
		if (maini)
			::SetWindowPos(maini->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(og->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		og->SetTimer(4930, 10, NULL);
		RegisterHotKey(og->GetSafeHwnd(), ID_HOTKEY0, 0, VK_UP);
		RegisterHotKey(og->GetSafeHwnd(), ID_HOTKEY1, 0, VK_DOWN);
		RegisterHotKey(og->GetSafeHwnd(), ID_HOTKEY2, 0, VK_RIGHT);
		RegisterHotKey(og->GetSafeHwnd(), ID_HOTKEY3, 0, VK_LEFT);
		ip = 3;
	}
	if (nIDEvent == 4924) {
		og->KillTimer(4924);
		UnregisterHotKey(og->GetSafeHwnd(), ID_HOTKEY0);
		UnregisterHotKey(og->GetSafeHwnd(), ID_HOTKEY1);
		UnregisterHotKey(og->GetSafeHwnd(), ID_HOTKEY2);
		UnregisterHotKey(og->GetSafeHwnd(), ID_HOTKEY3);
	}
	if (nIDEvent == 4930) {
		ip--;
		if (ip == 0) {
			aaaa1 = 0;
			og->KillTimer(4930);
		}
	}

	if (nIDEvent == 9100) {
		og->KillTimer(9100);
		og->OnRestart();
		og->OnRestart();
	}

	if (nIDEvent == 9998) {
		og->KillTimer(9998);
		if (ndd != "") {
			if (pl) {
				const DWORD plwT0 = GetTickCount();
				for (; plw == 0;) {
					DoEvent();
					if (GetTickCount() - plwT0 >= 5000)
						break;
				}
			}
			og->dp(ndd);
		}
	}
	if (nIDEvent == 9000) {
		// 連続再生: 曲末で次曲へ。固定待ち(旧 ≒500ms)ではなく、DS 再生カーソルが実音声の
		// 終端(g_endWrittenBytes)へジャストに到達してから OnRestart する。
		// 曲末の RubberBand 尻尾は各 read* が readtempo(len==0) で吐き切る（g_rubberBandFinalFlushed で二重 final は抑止）。
		if (savedata.saverenzoku == 1 && endflg == 1) {
			// 終端が未確定、または実再生カーソルが終端へ到達するまで次曲へ進めない。
			// g_heardBytes は DS スレッドが毎サイクル更新する実再生位置（バイト）。
			if (g_endWrittenBytes == 0 || g_heardBytes < g_endWrittenBytes)
				return; // まだ実音声が鳴り切っていない。次のティックで再判定。
			plcnt++;
			if (pl && plcnt >= pl->m_lc.GetItemCount()) plcnt = 0;
			endflg = 0;
			if (pl && plcnt < pl->m_lc.GetItemCount()) {
				pl->Get(plcnt);
				pl->SIcon(plcnt);
				fade1 = 0; lenl = 0;
				fade = 1.0f; plf = 0;
				og->KillTimer(9000);
				RequestPlaybackRestart(og->GetSafeHwnd());
			}
		}
	}
	if (nIDEvent == 6555) {
		if (pl)
			pl->SIconTimer(SC1);
		SC1++; SC1 = SC1 % 2;
	}
	if (nIDEvent == 5990) {
		og->KillTimer(5990);
		EnterMediaPlayerMode();
		return;
	}
	if (nIDEvent == 5211) {
		og->KillTimer(5211);
		if (savedata.pl == 1) {
			if (!pl) {
				pl = new CPlayList;
				pl->Create(og);
			}
			if (!plw) {
				const DWORD t0 = GetTickCount();
				for (;;) {
					if (plw) break;
					DoEvent();
					if (GetTickCount() - t0 >= 10000) break;
				}
			}
			// RestoreSavedPlaybackRow 後の filen は既にリストにあることが多い。
			// oggsize==0 の Add は time=0 で既存行を上書きしてしまうためスキップする。
			if (pl && plw && filen != "" && !(wavbit_sample_Hz == 0 || wavchannel == 0 || wavsam_depth == 0)) {
				const int existing = pl->FindByPath(filen);
				if (existing >= 0 && oggsize == 0) {
					plcnt = existing;
					pl->SIcon(existing);
				}
				else {
				int plc;
				if (mode == -10)
					plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, oggsize / (2 * wavchannel * wavbit_sample_Hz / 4), 1);
				else if (mode == -9 || mode == -8) {
					double wavv[] = { 0,1.0,2.0,2.0,2.0,2.0,2.0 };//(double)(wavbit2/wavv[wavchannel])
					plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, (int)(
						(double)oggsize / (double)(wavbit_sample_Hz * 2 * wavv[wavchannel]) / (double)(wavsam_depth / 16.0f)
						), 1);
				}
				else if (mode == -3) {
					if (oggsize == 0)
						plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, -1, 1);
					else
						plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, oggsize / (2 * wavchannel * wavbit_sample_Hz), 1);
				}
				else
					plc = pl->Add(fnn, mode, loop1, loop2, tagname, tagalbum, filen, ret2, oggsize / (2 * wavchannel * wavbit_sample_Hz));
				if (plc == -1) {
					int i = pl->m_lc.GetItemCount() - 1;
					plcnt = i;
					pl->SIcon(i);
				}
				else {
					plcnt = plc;
					pl->SIcon(plc);
				}
				}
			}
		}
	}

	if (nIDEvent == 5219) {
		og->KillTimer(5219);
		const BOOL mpMode = (savedata.playerMode == 1);
		HWND hTarget = og->m_hWnd;
		if (mpMode && mp && ::IsWindow(mp->GetSafeHwnd()))
			hTarget = mp->m_hWnd;
		else if (mpMode)
			return;   // EnterMediaPlayerMode 側で設定
		SetupTaskbarThumbButtons(hTarget, mpMode);
	}
	if (nIDEvent == 5656) {
		for (int i = 0; i < 300; i++)spetm[i] = 1;
	}

	if (nIDEvent == 5657) {
		for (int i = 0; i < 300; i++) {
			if (spetm[i] == 1) {
				spelv[i]--;
				if (spelv[i] < 0) spelv[i] = 0;
			}
		}
	}

	if (nIDEvent == 11251) {
		ttt_++;
		if (ttt_ == 1) {
			og->KillTimer(11251);
			og->OnRestart();
		}
	}

	if (nIDEvent == 15011) {
		og->KillTimer(15011);
		if (maini) {
			delete maini;
			maini = NULL;
		}

		if (savedata.aero == 2) {
			maini = new CImageBase;
			maini->Create(og);
			maini->oya = og;
		}
		RECT r;
		og->GetWindowRect(&r);
		if (maini)
			maini->MoveWindow(&r);
		if (maini)
			::SetWindowPos(maini->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(og->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}

	if (nIDEvent == 1250) {
		// フェードアウト完了後の連続再生(SetTimer(1250) と ID を一致)
		if ((fade1 == 1 && pl && plw)) {
			if (pl->m_renzoku.GetCheck() == TRUE) {
				plcnt++;
				if (plcnt >= pl->m_lc.GetItemCount()) plcnt = 0;
				endflg = 0;
				if (plcnt < pl->m_lc.GetItemCount()) {
					og->KillTimer(1250);
					pl->Get(plcnt);
					pl->SIcon(plcnt);
					thn = FALSE;
					const DWORD t0 = GetTickCount();
					for (int j = 0; j < 200; j++) {
						DoEvent();
						Sleep(10);
						if (thn == TRUE) break;
						if (GetTickCount() - t0 >= 3000) break;
					}
					og->stop();
					fade1 = 0; lenl = 0;
					RequestPlaybackRestart(og->GetSafeHwnd());
				}
			}
		}
	}
}
void timerog(UINT nIDEvent)
{
	_set_se_translator(trans_func);
	try {
		timerog1(nIDEvent);
		//	}__except(EXCEPTION_EXECUTE_HANDLER){}
	}
	catch (SE_Exception e) {
	}
	catch (_EXCEPTION_POINTERS* ep) {
	}
	catch (...) {}
}
#if WIN64
void COggDlg::OnTimer(UINT_PTR nIDEvent)
#else
void COggDlg::OnTimer(UINT nIDEvent)
#endif
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	timerog(nIDEvent);
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}
LRESULT COggDlg::dp2(WPARAM, LPARAM)
{
	InterlockedExchange(&s_restartMsgQueued, 0);
	InterlockedExchange(&s_restartWanted, 0);
	// KPI 読み込み中は再生も始まっていないので再開要求は破棄してよい
	if (g_pActiveLoadingWnd != NULL)
		return 0;
	OnRestart();
	return 0;
}

// 曲末フェード完了後、再生通知スレッドから PostMessage される。
// ワーカースレッド上で stop()/OnPause を呼ばない（UI フリーズ/デッドロック防止）。
// スレッドは既に Join 済みなので、DS/デコーダをここで解放し次曲開始時の
// stop()→play() が二重 Join や停止済みバッファ上でのデコード待ちで固まらないようにする。
LRESULT COggDlg::OnPlaybackAutoStopped(WPARAM, LPARAM)
{
	// play/stop1 実行中は触らない（曲切替中の自動停止メッセージでデコーダを潰さない）
	if (s_inPlay || s_inStop1)
		return 0;
	// スレッドは PostMessage 前に終了済み想定だが、念のため Join（DoEvent なし）
	SignalPlaybackNotifyThreadStop();
	if (!WaitForPlaybackNotifyThreadExit(0))
		return 0;
	fade1 = 0;
	endflg = 0;
	plf = 0;
	playf = 0;
	ps = 0;
	thn = TRUE;
	g_endWrittenBytes = 0;
	g_dsWrittenBytes = 0;
	g_heardBytes = 0;
	eqflg = TRUE;
	KillTimer(1250);
	if (m_dsb) {
		m_dsb->SetVolume(DSBVOLUME_MIN);
		m_dsb->Stop();
	}
	if (pAudioClient)
		pAudioClient->Stop();
	Closeds();
	if (ogg) {
		ReleaseOggVorbis(&ogg);
		ogg = NULL;
	}
	if (adbuf2) {
		free(adbuf2);
		adbuf2 = NULL;
	}
	const int stoppingMode = PeekOpenDecoderMode(mode);
	if (stoppingMode == -10) { mp3_.Close(); g_mp3_decoder_bps = 16; }
	if (stoppingMode == -8 && og) flac_.Close(og->kmp);
	if (stoppingMode == -9 && og) m4a_.Close(og->kmp);
	if (stoppingMode == -7 && og) dsd_.kpiClose(og->kmp);
	if (stoppingMode == 999) wav_.Close();
	kmp = NULL;
	ClearOpenDecoderMode();
	if (mod) {
		if (mod->Close) mod->Close(kmp1);
		if (mod->Deinit) mod->Deinit();
		FreeLibrary(hDLLk);
		mod = NULL; kmp1 = NULL; hDLLk = NULL;
	}
	if (kpidec) {
		kpidec->Release();
		kpidec = NULL;
	}
	if (ob5) {
		ob5->Release();
		ob5 = NULL;
	}
	if (g_kpiRemote && g_kpiSession.sessionId != 0) {
		g_kpiHost.Close(g_kpiSession.sessionId);
		ZeroMemory(&g_kpiSession, sizeof(g_kpiSession));
		g_kpiRemote = false;
		ResetKpiRemoteCache();
	}
	g_kpiPlaybackArch = 0;
	thn1 = FALSE;
	stf = 0;
	thend = 1;
	plf = 0;
	playf = 0;
	if (wav) {
		free(wav);
		wav = NULL;
	}
	if (::IsWindow(m_PianoRollDlg->GetSafeHwnd()))
		m_PianoRollDlg->ResetPlaybackState();
	if (::IsWindow(m_AnalyzerDlg->GetSafeHwnd()))
		m_AnalyzerDlg->ResetPlaybackState();
	m_analyzerSyncValid = FALSE;
	ResetPauseButtonUi();
	if (ptl)
		ptl->SetProgressState(m_hWnd, TBPF_NOPROGRESS);
	return 0;
}

LRESULT COggDlg::OnUpdateAvailable(WPARAM wParam, LPARAM)
{
	UpdateCheckBeginPrompt();
	int ret = AfxMessageBox(LL14(
		L"アップデートファイルがあります。\n今すぐ更新しますか？", /* 日本語 */
		L"An update file is available.\nWould you like to update now?", /* 英語 */
		L"Un fichier de mise à jour est disponible.\nMettre à jour maintenant ?", /* フランス語 */
		L"È disponibile un file di aggiornamento.\nAggiornare ora?", /* イタリア語 */
		L"Hay un archivo de actualización disponible.\n¿Actualizar ahora?", /* スペイン語 */
		L"업데이트 파일이 있습니다.\n지금 업데이트하시겠습니까?", /* 韓国語 */
		L"有更新文件。\n是否立即更新？", /* 中国語 */
		L"يتوفر ملف تحديث.\nهل تريد التحديث الآن؟", /* アラビア語 */
		L"Доступен файл обновления.\nОбновить сейчас?", /* ロシア語 */
		L"Eine Aktualisierungsdatei ist verfügbar.\nJetzt aktualisieren?", /* ドイツ語 */
		L"Um arquivo de atualização está disponível.\nAtualizar agora?", /* ポルトガル語 */
		L"Er is een updatebestand beschikbaar.\nNu bijwerken?", /* オランダ語 */
		L"Dostępny jest plik aktualizacji.\nCzy zaktualizować teraz?", /* ポーランド語 */
		L"Güncelleme dosyası mevcut.\nŞimdi güncellemek istiyor musunuz?" /* トルコ語 */
	), MB_YESNO);
	if (ret == IDYES && DoUpdateAndRestart())
	{
		UpdateCheckEndPrompt(false, 0);
		OnOK();  // ダイアログを閉じてアプリ終了
	}
	else
	{
		const bool dismissed = (ret == IDNO);
		if (dismissed && wParam != 0)
			savedata.lastUpdateCheck = (__int64)wParam;
		UpdateCheckEndPrompt(dismissed, (__int64)wParam);
		if (dismissed && wParam != 0)
			MpPersistSavedataQuick();
	}
	return 0;
}

void COggDlg::OnButton1()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	randomf = 0;
	m_rund.EnableWindow(TRUE);
	stop();
}

CString COggDlg::UTF8toSJIS(const char* a)
{
	WCHAR f[1024];
	char ff[1024];
	int rr = MultiByteToWideChar(CP_UTF8, 0, a, -1, f, 1024);
	int rr2 = WideCharToMultiByte(CP_ACP, 0, f, rr, ff, 0, NULL, NULL);
	WideCharToMultiByte(CP_ACP, 0, f, rr, ff, rr2, NULL, NULL);
	CString s; s = f;
	return s;
	//	return _T("");
}

CString COggDlg::UTF8toUNI(const TCHAR* a)
{
	//	WCHAR f[1024];
	//	char ff[1024];
	//	int rr2=WideCharToMultiByte(CP_ACP,0,a,-1,ff,0,NULL,NULL);
	//	int rr=MultiByteToWideChar(CP_UTF8,0,ff,-1,f,1024);
	//	WideCharToMultiByte(CP_ACP,0,f,rr,ff,rr2,NULL,NULL);
	CString s; s = a;
	return s;
	//	return _T("");
}

//画面表示
void COggDlg::gamen(int uu)
{
	switch (mode) {
	case -1://ogg
		if (filen.Find(L"y8_logo.ogg") != -1) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}
		if (filen.Find(L"y8_op.ogg") != -1) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}
		if (filen.Find(L"y8_end.ogg") != -1) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}
		if (filen.Find(L"yc_logo.ogg") != -1) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}
		break;
	case 1://ED6SC
		if (uu > 97) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 2://ED6FC
		if (uu > 54) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 3://YSF
		if (uu == 32 || uu == 33 || uu == 25) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 4://YS6
		if (uu > 24 && uu < 29 || uu == 1) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 5://YSO
		if (uu > 39) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 6://YSO
		if (uu > 140) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 7://YSO
		if (uu > 64) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 8://YSC1
		if (uu >= 72) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 9://YSC1
		if (uu >= 93) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 10://XANADU
		if (uu >= 24) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 11://ys1
		if (uu >= 25) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 12://ys2
		if (uu >= 31) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 15://gurumin
		if (uu == 40) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 16://dino
		if (uu == 33) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case 19://ed4
		if (uu == 1 || uu == 2) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case -11://ed4
		if (uu >= 28) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case -13://arc
		if (uu == 0) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case -14://san1
		if (uu == 43 || uu == 45 || uu == 46 || uu == 47) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	case -15://san1
		if (uu == 50 || uu == 51 || uu == 49) {
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
		}break;
	}
}

void COggDlg::gamenkill()
{
	if (pMainFrame1 != NULL) {
		killw = 0;
		RECT r;
		pMainFrame1->GetWindowRect(&r);
		savedata.gx = r.left;
		savedata.gy = r.top;
		pMainFrame1->stop();
		::SendMessage(pMainFrame1->m_hWnd, WM_CLOSE, NULL, NULL);
		//		delete pMainFrame1;
		//動画画面が閉じるのを待つ
		PumpUntilFlagOrTimeout(killw, 10000);
		//delete pMainFrame1;
		pMainFrame1 = NULL;
		//		for(int i=0;i<20;i++){DoEvent();Sleep(5);};
	}
}

void COggDlg::dougaplay(int uu, CString ss)
{
	if (pMainFrame1 == NULL) return;
	switch (mode) {
	case -1://ED6SC
		if (filen.Find(L"y8_logo.ogg") != -1) {
			pMainFrame1->play(1, ss);
		}
		if (filen.Find(L"y8_op.ogg") != -1) {

			pMainFrame1->play(2, ss);
		}
		if (filen.Find(L"y8_end.ogg") != -1) {
			;
			pMainFrame1->play(3, ss);
		}
		if (filen.Find(L"yc_logo.ogg") != -1) {
			;
			pMainFrame1->play(4, ss);
		}
		break;
	case 1://ED6SC
		if (uu > 97) {
			pMainFrame1->play(uu);
		}break;
	case 2://ED6FC
		if (uu > 54) {
			pMainFrame1->play(uu);
		}break;
	case 3://YSF
		if (uu == 32 || uu == 33 || uu == 25) {
			pMainFrame1->play(uu);
		}break;
	case 4://YS6
		if (uu > 24 && uu < 29 || uu == 1) {
			pMainFrame1->play(uu);
		}
	case 5://YSO
		if (uu > 39) {
			pMainFrame1->play(uu);
		}break;
	case 6://YSO
		if (uu > 140) {
			pMainFrame1->play(uu);
		}break;
	case 7://YSO
		if (uu > 64) {
			pMainFrame1->play(uu);
		}break;
	case 8://YSC1
		if (uu >= 72) {
			pMainFrame1->play(uu);
		}break;
	case 9://YSC1
		if (uu >= 93) {
			pMainFrame1->play(uu);
		}break;
	case 10://XANADU
		if (uu >= 24) {
			pMainFrame1->play(uu);
		}break;
	case 11://ys1
		if (uu >= 25) {
			pMainFrame1->play(uu);
		}break;
	case 12://ys2
		if (uu >= 31) {
			pMainFrame1->play(uu);
		}break;
	case 15://gurumin
		if (uu == 40) {
			pMainFrame1->play(uu);
		}break;
	case 16://dino
		if (uu == 33) {
			pMainFrame1->play(uu);
		}break;
	case 19://ed4
		if (uu == 1 || uu == 2) {
			pMainFrame1->play(uu);
		}break;
	case -11://ed4
		if (uu >= 28) {
			pMainFrame1->play(uu);
		}break;
	case -13://arc
		if (uu == 0) {
			pMainFrame1->play(uu);
		}break;
	case -14://arc
		if (uu == 43 || uu == 45 || uu == 46 || uu == 47) {
			pMainFrame1->play(uu);
		}break;
	case -15://arc
		if (uu == 50 || uu == 51 || uu == 49) {
			pMainFrame1->play(uu);
		}break;
	}
}

void COggDlg::OnButton2()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ed6sc);
	ret += _chdir("bgm");
	if (ret != 0) {
			fnn = LL14(
				L"ファイル又はフォルダがありません",         /* 日本語 */
				L"File or folder not found",           /* 英語 */
				L"Fichier ou dossier introuvable",     /* フランス語 */
				L"File o cartella non trovati",        /* イタリア語 */
				L"Archivo o carpeta no encontrados",   /* スペイン語 */
				L"파일 또는 폴더가 없습니다",             /* 韓国語 */
				L"找不到文件或文件夹",                   /* 中国語 */
				L"الملف أو المجلد غير موجود",          /* アラビア語 */
				L"Файл или папка не найдены",          /* ロシア語 */
				L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
				L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
				L"Bestand of map niet gevonden",       /* オランダ語 */
				L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
				L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	itiran* a = new itiran(CWnd::FromHandle(GetSafeHwnd()));
	a->ret = pl_no;
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567) {
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ed6sc);
		ret += _chdir("bgm");
		filen.Format(_T("ED6%03d.ogg"), a->ret);
		ret2 = a->ret2;
		modesub = 1;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);
}

void COggDlg::OnButton6_FC()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ed6fc);
	ret += _chdir("bgm");
	if (ret != 0) {
		if (ret != 0) {
			fnn = LL14(
				L"ファイル又はフォルダがありません",         /* 日本語 */
				L"File or folder not found",           /* 英語 */
				L"Fichier ou dossier introuvable",     /* フランス語 */
				L"File o cartella non trovati",        /* イタリア語 */
				L"Archivo o carpeta no encontrados",   /* スペイン語 */
				L"파일 또는 폴더가 없습니다",             /* 韓国語 */
				L"找不到文件或文件夹",                   /* 中国語 */
				L"الملف أو المجلد غير موجود",          /* アラビア語 */
				L"Файл или папка не найдены",          /* ロシア語 */
				L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
				L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
				L"Bestand of map niet gevonden",       /* オランダ語 */
				L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
				L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		}
		return; 
	}
	Citiran_FC* a = new Citiran_FC(CWnd::FromHandle(GetSafeHwnd()));
	a->ret = pl_no;
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		filen.Format(_T("ED6%03d.ogg"), a->ret);
		ret2 = a->ret2;
		modesub = 2;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnButton7_YSF()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ysf);
	ret += _chdir("RELEASE\\MUSIC");
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
	 return; }
	Citiran_YSF* a = new Citiran_YSF(CWnd::FromHandle(GetSafeHwnd()));
	a->ret = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567) {
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ysf);
		ret += _chdir("RELEASE\\MUSIC");
		if (a->ret == 21) {
			filen.Format(_T("y3bg22a.ogg"));
		}
		else
			filen.Format(_T("y3bg%02d.ogg"), a->ret + 1);
		modesub = 3;
		ret2 = a->ret;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnButton8_YS6()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ys6);
	ret += _chdir("RELEASE\\MUSIC");
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
	 return; }
	Citiran_YS6* a = new Citiran_YS6(CWnd::FromHandle(GetSafeHwnd()));
	a->ret = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567) {
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ys6);
		ret += _chdir("RELEASE\\MUSIC");
		filen.Format(_T("Ys6_%02d.ogg"), a->ret + 1);
		modesub = 4;
		ret2 = a->ret;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnYso()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.yso);
	ret += _chdir("RELEASE\\MUSIC");
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
	 return; }
	Citiran_YSO* a = new Citiran_YSO(CWnd::FromHandle(GetSafeHwnd()));
	a->ret = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.yso);
		ret += _chdir("RELEASE\\MUSIC");
		filen.Format(_T("YSO_%03d.ogg"), a->ret);
		ret2 = a->ret2;
		modesub = 5;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnButton17_ED6TC()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ed6tc);
	ret += _chdir("bgm");
	if (ret != 0) return;
	CED63rd* a = new CED63rd(CWnd::FromHandle(GetSafeHwnd()));
	a->ret = pl_no;
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ed6tc);
		ret += _chdir("bgm");
		filen.Format(_T("ED6%03d.ogg"), a->ret);
		ret2 = a->ret2;
		modesub = 6;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}


void COggDlg::OnZWEIII()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.zweiii);
	ret += _chdir("bgm");
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
	 return; }
	CZWEIII* a = new CZWEIII(CWnd::FromHandle(GetSafeHwnd()));
	a->ret = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.zweiii);
		ret += _chdir("bgm");
		filen.Format(_T("ZW2_%03d.ogg"), a->ret);
		ret2 = a->ret2;
		modesub = 7;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnYsC1()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ysc);
	ret += _chdir("bgm\\ys1");
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
	 return; }
	CYsC1* a = new CYsC1(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ysc);
		ret += _chdir("bgm\\ys1");
		filen.Format(_T("%s.ogg"), a->ret);
		ret2 = a->ret2;
		modesub = 8;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnYsC2()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ysc);
	ret += _chdir("bgm\\ys2");
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CYsC2* a = new CYsC2(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ysc);
		ret += _chdir("bgm\\ys2");
		filen.Format(_T("%s.ogg"), a->ret);
		ret2 = a->ret2;
		modesub = 9;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton25()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.xa);
	ret += _chdir("data\\bgm");
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CXA* a = new CXA(CWnd::FromHandle(GetSafeHwnd()));
	a->ret = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.xa);
		ret += _chdir("data\\bgm");
		filen.Format(_T("XANA%03d.dec"), a->ret);
		ret2 = a->ret;
		loop1 = a->loop1;
		loop2 = a->loop2 - loop1;
		modesub = 10;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton27()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	CString ex;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ys12);
	if (_chdir("wave\\wave_44") == -1) {
		if (_chdir("wave\\wave_22") == -1) {
			fnn = LL14(
				L"ファイル又はフォルダがありません",         /* 日本語 */
				L"File or folder not found",           /* 英語 */
				L"Fichier ou dossier introuvable",     /* フランス語 */
				L"File o cartella non trovati",        /* イタリア語 */
				L"Archivo o carpeta no encontrados",   /* スペイン語 */
				L"파일 또는 폴더가 없습니다",             /* 韓国語 */
				L"找不到文件或文件夹",                   /* 中国語 */
				L"الملف أو المجلد غير موجود",          /* アラビア語 */
				L"Файл или папка не найдены",          /* ロシア語 */
				L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
				L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
				L"Bestand of map niet gevonden",       /* オランダ語 */
				L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
				L"Dosya veya klasör bulunamadı");       /* トルコ語 */
			return;
		}
		ex = "_22";
	}
	else ex = "_44";
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CYs12_1* a = new CYs12_1(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ys12);
		if (_chdir("wave\\wave_44") == -1) {
			if (_chdir("wave\\wave_22") == -1) { return; }
			ex = "_22";
		}
		else ex = "_44";
		filen.Format(_T("%s%s.wav"), a->ret, ex);
		ret2 = a->ret2;
		CFile f;
		CString sf;
		sf.Format(_T("%s%s.pos"), a->ret, ex);
		loop1 = loop2 = 0;
		struct a1 {
			int l1;
			int l2;
		};
		a1 aa;
		if (f.Open(sf, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
			f.Read(&aa, 8);
			f.Close();
			if (aa.l1 == 0)aa.l2 = 0;
			loop1 = aa.l1;
			loop2 = aa.l2 - loop1;
		}
		modesub = 11;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton28()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	CString ex;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ys122);
	if (_chdir("wave\\wave_44") == -1) {
		if (_chdir("wave\\wave_22") == -1) {
			fnn = LL14(
				L"ファイル又はフォルダがありません",         /* 日本語 */
				L"File or folder not found",           /* 英語 */
				L"Fichier ou dossier introuvable",     /* フランス語 */
				L"File o cartella non trovati",        /* イタリア語 */
				L"Archivo o carpeta no encontrados",   /* スペイン語 */
				L"파일 또는 폴더가 없습니다",             /* 韓国語 */
				L"找不到文件或文件夹",                   /* 中国語 */
				L"الملف أو المجلد غير موجود",          /* アラビア語 */
				L"Файл или папка не найдены",          /* ロシア語 */
				L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
				L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
				L"Bestand of map niet gevonden",       /* オランダ語 */
				L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
				L"Dosya veya klasör bulunamadı");       /* トルコ語 */
			return;
		}
		ex = "_22";
	}
	else ex = "_44";
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CYs12_2* a = new CYs12_2(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ys122);
		if (_chdir("wave\\wave_44") == -1) {
			if (_chdir("wave\\wave_22") == -1) { return; }
			ex = "_22";
		}
		else ex = "_44";
		filen.Format(_T("%s.wav"), a->ret);
		ret2 = a->ret2;
		CFile f;
		CString sf;
		sf.Format(_T("%s.pos"), a->ret);
		loop1 = loop2 = 0;
		struct a1 {
			int l1;
			int l2;
		};
		a1 aa;
		if (f.Open(sf, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL)) {
			f.Read(&aa, 8);
			f.Close();
			if (aa.l1 == 0)aa.l2 = 0;
			loop1 = aa.l1;
			loop2 = aa.l2 - loop1;
		}
		modesub = 12;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton31()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	CString ex;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.sor);
	if (_chdir("WAVE\\WAVE44") == -1) {
		if (_chdir("WAVE\\WAVE22") == -1) {
			fnn = LL14(
				L"ファイル又はフォルダがありません",         /* 日本語 */
				L"File or folder not found",           /* 英語 */
				L"Fichier ou dossier introuvable",     /* フランス語 */
				L"File o cartella non trovati",        /* イタリア語 */
				L"Archivo o carpeta no encontrados",   /* スペイン語 */
				L"파일 또는 폴더가 없습니다",             /* 韓国語 */
				L"找不到文件或文件夹",                   /* 中国語 */
				L"الملف أو المجلد غير موجود",          /* アラビア語 */
				L"Файл или папка не найдены",          /* ロシア語 */
				L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
				L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
				L"Bestand of map niet gevonden",       /* オランダ語 */
				L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
				L"Dosya veya klasör bulunamadı");       /* トルコ語 */
			return;
		}
		ex = "_22";
	}
	else ex = "_44";
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CSor* a = new CSor(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.sor);
		if (_chdir("WAVE\\WAVE44") == -1) {
			if (_chdir("WAVE\\WAVE22") == -1) { return; }
			ex = "_22";
		}
		else ex = "_44";
		filen.Format(_T("%s.wav"), a->ret);
		ret2 = a->ret2;
		CStdioFile f;
		CString sf;
		if (ex == "_22")
			sf = "..\\..\\WAVE_CD.DAT";
		else
			sf = "..\\..\\WAVE_DVD.DAT";
		loop1 = loop2 = 0;
		if (f.Open(sf, CFile::modeRead | CFile::typeText | CFile::shareDenyWrite, NULL)) {
			for (int j = 0;; j++) {
				f.ReadString(sf);
				if (sf.Left(6) == a->ret)break;
			}
			f.Close();
			CString sff;
			sff = sf.Mid(8, 10);//開始ms
			if (sf.Right(1) == "N") {
				loop1 = loop2 = 0;
			}
			else {
				float a, b;
				if (ex == "_22") b = 22.05f; else b = 44.1f;
				loop1 = _tstoi(sff);
				loop2 = _tstoi(sf.Mid(18, 10));
				a = (float)loop1;
				a = a * b;
				loop1 = (int)a;
				a = (float)loop2;
				a = a * b;
				loop2 = (int)a - loop1;
			}
		}
		modesub = 13;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton33()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.zwei);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */		return;
	}
	CZwei* a = new CZwei(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.zwei);
		filen.Format(_T("%s(wav.dat)"), a->ret);
		ret2 = a->ret2;
		modesub = 14;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton35()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.gurumin);
	ret += _chdir("bgm");
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",      /* 日本語: 厳格に維持 */
			L"Steak was eaten by the mouse",        /* 英語: ステーキはマウス（鼠）に食べられました */
			L"Le vin s'est transformé en eau",     /* フランス語: ワインが水に変わってしまいました */
			L"Pizza rapita dagli alieni",          /* イタリア語: ピザが宇宙人に誘拐されました */
			L"Paella enterrada en la playa",       /* スペイン語: パエリアは砂浜に埋められました */
			L"김밥이 투명해졌습니다",                /* 韓国語: キンパ（海苔巻き）が透明になりました */
			L"餃子飛向了月球",                      /* 中国語: 餃子は月へ飛んでいきました */
			L"الكباب غرق في القهوة",               /* アラビア語: ケバブがコーヒーに沈没しました */
			L"Медведь съел жесткий диск",          /* ロシア語: クマがハードディスクを食べました */
			L"Wurst ist im Weltraum verloren",     /* ドイツ語: ソーセージは宇宙迷子になりました */
			L"Bacalhau viajou para o futuro",      /* ポルトガル語: 干し鱈は未来へ旅立ちました */
			L"De kaas is gesmolten in de zon",      /* オランダ語: チーズは太陽で溶けました */
			L"Pierogi wpadły do wulkanu",          /* ポーランド語: ピエロギが火山に落ちました */
			L"Kebap bulutların üstünde");           /* トルコ語: ケバブは雲の上にあります */
		return;
	}
	CGurumin* a = new CGurumin(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.gurumin);
		ret += _chdir("bgm");
		filen.Format(_T("%s.de2"), a->ret);
		ret2 = a->ret2;
		loop1 = a->loop1;
		loop2 = a->loop2 - loop1;
		modesub = 15;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton37()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.dino);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CDino* a = new CDino(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.dino);
		filen.Format(_T("%s(bgm.arc)"), a->ret);
		ret2 = a->ret2;
		loop1 = a->loop1;
		loop2 = a->loop2;
		modesub = 16;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton39()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.br4);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CBr4* a = new CBr4(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.br4);
		filen.Format(_T("%s.wav"), a->ret);
		ret2 = a->ret2;
		modesub = 17;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton44()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ed3);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CED3* a = new CED3(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ed3);
		filen.Format(_T("%s.wav"), a->ret);
		ret2 = a->ret2;
		modesub = 18;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton45()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ed4);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CED4* a = new CED4(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ed4);
		filen.Format(_T("%s"), a->ret);
		ret2 = a->ret2;
		modesub = 19;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton46()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.ed5);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CED5* a = new CED5(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.ed5);
		filen.Format(_T("%s.wav"), a->ret);
		ret2 = a->ret2;
		modesub = 20;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton47()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.tuki);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CTUKI* a = new CTUKI(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.tuki);
		filen.Format(_T("%s.mp3"), a->ret);
		ret2 = a->ret2;
		modesub = -11;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton48()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.nishi);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CNishi* a = new CNishi(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.nishi);
		filen.Format(_T("%s.wav"), a->ret);
		ret2 = a->ret2;
		modesub = -12;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton51()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.arc);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CArc* a = new CArc(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		loopcnt = 0;
		ret = _tchdir(savedata.arc);
		filen.Format(_T("%s.adp(music.pak)"), a->ret);
		ret2 = a->ret2;
		modesub = -13;
		delete a;
		play();
	}
	else
		delete a;
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton53()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.san1);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CSan1* a = new CSan1(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.san1);
		if (a->ret == "★") {
			filen = "★";
		}
		else {
			filen.Format(_T("%smusic.wav"), a->ret);
			if (filen == "49music.wav" || filen == "50music.wav" || filen == "51music.wav") _chdir("Cmusic"); else _chdir("music");
			CFile f;
			if (f.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) == 0) {
				filen.Format(_T("%smusic.mp3"), a->ret);
				if (f.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) == 0) {
					fnn = LL14(
						L"ファイル又はフォルダがありません",         /* 日本語 */
						L"File or folder not found",           /* 英語 */
						L"Fichier ou dossier introuvable",     /* フランス語 */
						L"File o cartella non trovati",        /* イタリア語 */
						L"Archivo o carpeta no encontrados",   /* スペイン語 */
						L"파일 또는 폴더가 없습니다",             /* 韓国語 */
						L"找不到文件或文件夹",                   /* 中国語 */
						L"الملف أو المجلد غير موجود",          /* アラビア語 */
						L"Файл или папка не найдены",          /* ロシア語 */
						L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
						L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
						L"Bestand of map niet gevonden",       /* オランダ語 */
						L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
						L"Dosya veya klasör bulunamadı");       /* トルコ語 */
					return;
				}
				else f.Close();
			}
			else f.Close();
		}
		ret2 = a->ret2;
		modesub = -14;
		delete a;
		play();
	}
	SetTimer(4923, 20, NULL);

}

void COggDlg::OnBnClickedButton54()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int ret;
	char buffer[_MAX_DIR];
	_getcwd(buffer, _MAX_DIR);
	ret = _tchdir(savedata.san2);
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",         /* 日本語 */
			L"File or folder not found",           /* 英語 */
			L"Fichier ou dossier introuvable",     /* フランス語 */
			L"File o cartella non trovati",        /* イタリア語 */
			L"Archivo o carpeta no encontrados",   /* スペイン語 */
			L"파일 또는 폴더가 없습니다",             /* 韓国語 */
			L"找不到文件或文件夹",                   /* 中国語 */
			L"الملف أو المجلد غير موجود",          /* アラビア語 */
			L"Файл или папка не найдены",          /* ロシア語 */
			L"Datei oder Ordner nicht gefunden",    /* ドイツ語 */
			L"Arquivo ou pasta não encontrados",   /* ポルトガル語 */
			L"Bestand of map niet gevonden",       /* オランダ語 */
			L"Nie znaleziono pliku ani folderu",   /* ポーランド語 */
			L"Dosya veya klasör bulunamadı");       /* トルコ語 */
		return;
	}
	CSan2* a = new CSan2(CWnd::FromHandle(GetSafeHwnd()));
	a->ret2 = ret2;
	CWnd::PostMessage(0x118);
	if (a->DoModal() == 1567)
	{
		_chdir(buffer);
		stop();
		ret = _tchdir(savedata.san2);
		if (a->ret == "★") {
			filen = "★";
		}
		else {
			filen.Format(_T("0%smusic.mp3"), a->ret);
		}
		ret2 = a->ret2;
		modesub = -15;
		delete a;
		play();
	}
}

void COggDlg::OnPause()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	if (plf == 0) return;
	if (ps == 0)
	{
		if (pMainFrame1 != NULL && (mode == -2 || (mode > 0 && videoonly == TRUE)))
		{
			pMainFrame1->pause(0);
		}
		if (ogg != NULL || adbuf2 != NULL || mod != NULL || wav != NULL || mode == -9)
			if (m_dsb && thn == FALSE) {
				m_dsb->GetCurrentPosition(&PlayCursora, &WriteCursora);
				m_dsb->Stop();
			}
		//			waveOutPause(hwo);
		m_ps.SetWindowText(LL14(
			L"再開",            /* 日本語 */
			L"Resume",          /* 英語 */
			L"Reprendre",       /* フランス語 */
			L"Riprendi",        /* イタリア語 */
			L"Reanudar",        /* スペイン語 */
			L"다시 시작",         /* 韓国語 */
			L"恢复",             /* 中国語 */
			L"استئناف",         /* アラビア語 */
			L"Продолжить",      /* ロシア語 */
			L"Fortsetzen",       /* ドイツ語 */
			L"Retomar",         /* ポルトガル語 */
			L"Hervatten",       /* オランダ語 */
			L"Wznów",           /* ポーランド語 */
			L"Devam Et"));      /* トルコ語 */		ps = 1;
		SyncPauseButtonUi();
	}
	else {
		if (ogg != NULL || adbuf2 != NULL || mod != NULL || wav != NULL || mode == -9) {
			//			DSBPOSITIONNOTIFY dsn[20];
			//			ttt = WAVDAStartLen;
			//			hNotifyEvent[0] = CreateEvent(NULL, FALSE, FALSE, NULL);
			//			hNotifyEvent[1] = CreateEvent(NULL, FALSE, FALSE, NULL);
			// setup the first one.
			//			for(int y =0;y<10;y++){
			//				dsn[y].dwOffset = y*(WAVDALen/10);
			//				dsn[y].hEventNotify = hNotifyEvent[0];
			//			}
			//			dsn[y].dwOffset = DSBPN_OFFSETSTOP;
			//			dsn[y].hEventNotify = hNotifyEvent[1];
			//			AfxBeginThread(HandleNotifications,(LPVOID)this);
			//			dsnf1->SetNotificationPositions(10+1,dsn);
			if (m_dsb && thn == FALSE)m_dsb->Play(0, 0, DSBPLAY_LOOPING);
			if (m_dsb && thn == FALSE)m_dsb->SetCurrentPosition(PlayCursora);
		}
		//			waveOutRestart(hwo);
		ps = 0; m_ps.SetWindowText(LL14(
			L"一時停止",            /* 日本語 */
			L"Pause",               /* 英語 */
			L"Pause",               /* フランス語 */
			L"Pausa",               /* イタリア語 */
			L"Pausa",               /* スペイン語 */
			L"일시 정지",            /* 韓国語 */
			L"暂停",                /* 中国語 */
			L"إيقاف مؤقت",          /* アラビア語 */
			L"Пауза",               /* ロシア語 */
			L"Pause",               /* ドイツ語 */
			L"Pausar",              /* ポルトガル語 */
			L"Pauzeren",            /* オランダ語 */
			L"Wstrzymaj",           /* ポーランド語 */
			L"Duraklat"));          /* トルコ語 */		if (pMainFrame1 != NULL && (mode == -2 || (mode > 0 && videoonly == TRUE)))
		{
			pMainFrame1->pause(1);
		}
		ps = 0;
		SyncPauseButtonUi();
	}
}

BOOL COggDlg::PreTranslateMessage(MSG* pMsg)
{
	// メディアプレイヤーモード: DoModal 中は mp の PreTranslateMessage が呼ばれない。
	// あいまい検索欄の Enter をここで中継し、og の IDOK(終了)へ流さない。
	if (savedata.playerMode == 1) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->RelayPreTranslateMessage(pMsg))
			return TRUE;
	}
	if (CCC_InwomanHotkey(pMsg, this))
		return TRUE; // 隠し: F12を5回で淫女モード切替
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void COggDlg::OnOK()
{
	// TODO: この位置にその他の検証用のコードを追加してください
	const DWORD prevJoin = g_playbackNotifyJoinTimeoutMs;
	g_playbackNotifyJoinTimeoutMs = 8000;
	stop();
	g_playbackNotifyJoinTimeoutMs = prevJoin;
	CCustomBlurDialogBase::OnOK();
}
extern IMediaEvent* pMediaEvent;
static volatile LONG s_onRestartBusy = 0;

void COggDlg::OnRestart()
{
	if (InterlockedCompareExchange(&s_onRestartBusy, 1, 0) != 0) {
		InterlockedExchange(&s_restartWanted, 1);
		return;
	}
	// play() 中の DoEvent 再入で stop() がフラグだけ立てると CWread/再生が壊れる。延期する。
	if (s_inPlay || s_inStop1) {
		InterlockedExchange(&s_onRestartBusy, 0);
		RequestPlaybackRestart(GetSafeHwnd());
		return;
	}
	struct RestartBusyGuard {
		COggDlg* dlg;
		~RestartBusyGuard() {
			InterlockedExchange(&s_onRestartBusy, 0);
			if (InterlockedExchange(&s_restartWanted, 0) != 0 && dlg && ::IsWindow(dlg->GetSafeHwnd()))
				RequestPlaybackRestart(dlg->GetSafeHwnd());
		}
	} restartBusyGuard{ this };
	struct InteractiveTrackGuard {
		InteractiveTrackGuard() { InterlockedExchange(&g_interactiveTrackChange, 1); }
		~InteractiveTrackGuard() { InterlockedExchange(&g_interactiveTrackChange, 0); }
	} interactiveTrackGuard;

	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	CString ti;
	stop();
	if (filen == _T("") && pl && pl->playcnt > 0)
		pl->RestoreSavedPlaybackRow();
	if (filen != "") {
		ti = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		int sub_ = mode;
		// ゲーム形式トラックは拡張子による上書きを行わない。
		// 各ゲームのBGMは ED6001.ogg / YS6xxx.ogg / *.wav 等、拡張子が
		// .ogg/.wav でもゲーム専用のデコード経路(BGMフォルダへのchdir等)が必須で、
		// 単体ファイル(modesub=-1等)として再生すると鳴らせない。
		// stop() により mode は再生対象アイテム本来のモード(=pc[i].sub)へ復元済みなので、
		// それを用いてゲーム形式か否かを判定する。
		const bool isGameMode =
			(mode >= 1 && mode <= 30) ||
			mode == -11 || mode == -12 || mode == -13 || mode == -14 || mode == -15;
		// ネイティブ音声形式は拡張子のみで判定（game形式等からの切り替えも可能に）
		if (!isGameMode && filen.Right(5).MakeLower() == ".opus") {
			modesub = -6;
			playb = 0;
			play();
		}
		else if (!isGameMode && (filen.Right(4).MakeLower() == ".ogg" || filen.Right(4) == ".OGG" || filen.Right(6).MakeLower() == ".qull3")) {
			modesub = -1;
			play();
		}
		else if (!isGameMode && (filen.Right(4).MakeLower() == ".dsf" || filen.Right(5) == ".DSF" || filen.Right(4).MakeLower() == ".dff" || filen.Right(4) == ".DFF" || filen.Right(4).MakeLower() == ".wsd" || filen.Right(4) == ".WSD")) {
			modesub = -7;
			play();
		}
		else if (!isGameMode && (filen.Right(5).MakeLower() == ".flac" || filen.Right(5) == ".FLAC" || filen.Right(7).MakeLower() == L".qull3h")) {
			modesub = -8;
			play();
		}
		else if (!isGameMode && (filen.Right(4).MakeLower() == ".m4a" || filen.Right(4) == ".M4A" || filen.Right(4).MakeLower() == ".aac" || filen.Right(4) == ".AAC")) {
			modesub = -9;
			play();
		}
		else if (!isGameMode && (filen.Right(4).MakeLower() == ".mp3" || filen.Right(4) == ".MP3" || filen.Right(4).MakeLower() == ".mp2" || filen.Right(4) == ".MP2" ||
			filen.Right(4).MakeLower() == ".mp1" || filen.Right(4) == ".MP1" || filen.Right(4).MakeLower() == ".rmp" || filen.Right(4) == ".RMP")) {
			modesub = -10;
			play();
		}
		else if (!isGameMode && (filen.Right(4).MakeLower() == ".wav" || filen.Right(4) == ".WAV")) {
			modesub = 999;
			play();
		}
		else if (isGameMode) {
			if (mode == 19) filen = filen.Left(5);
			modesub = mode;
			play();
		}
		else if (mode == -2 || mode == -3) {
			playlistdata p;
			kpi[0] = 0;
			if (pl && plw) {
				p.sub = 0;
				CString ss, s;
				s = filen;
				{
					uint32_t sel = 1;
					SplitKpiSubsongPath(s, ss, sel);
				}
				if (ss != "") s = ss;
				pl->plugs(s, &p, kpi, kvver);
				if (p.sub == -3) {//kb medua player
					s = kpi;
					ss = s.Left(s.ReverseFind('\\'));
					_tchdir(ss);
					//					hDLLk = LoadLibrary(kpi);
					//					pFunck = (pfnGetKMPModule)::GetProcAddress(hDLLk, "kmp_GetTestModule");
					mode = -3;
					modesub = -3;
					play();
					return;
				}
			}
			fnn = ti;
			stflg = FALSE;
			modesub = -2; mode = -2;
			pMainFrame1 = new CDouga;
			pMainFrame1->Create(GetSafeHwnd());
			pMainFrame1->ShowWindow(SW_HIDE);
			pMainFrame1->play(0);
			CFile f123;
			int flggg = 0;
			if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
				f123.Close();
				if (IDYES == MessageBox(LL14(
					L"途中再生データが存在します。\n前回中断した部分から再生しますか？\nはい = 途中から再生\nいいえ = はじめから再生", /* 日本語 */
					L"Resume data exists.\nResume from where you left off?\nYes = Resume\nNo = Play from start", /* 英語 */
					L"Des données de reprise existent.\nReprendre là où vous vous êtes arrêté ?\nOui = Reprendre\nNon = Jouer depuis le début", /* フランス語 */
					L"Esistono dati di ripresa.\nRiprendere da dove ci si è fermati?\nSì = Riprendi\nNo = Riproduci dall'inizio", /* イタリア語 */
					L"Existen datos de reanudación.\n¿Reanudar desde donde lo dejó?\nSí = Reanudar\nNo = Reproducir desde el inicio", /* スペイン語 */
					L"중간 재생 데이터가 존재합니다.\n지난번 중단한 부분부터 재생하시겠습니까?\n예 = 중간부터 재생\n아니요 = 처음부터 재생", /* 韓国語 */
					L"存在中途播放数据。\n是否从上次中断处播放？\n是 = 从中途播放\n否 = 从头播放", /* 中国語 */
					L"توجد بيانات استئناف.\nهل تريد الاستئناف من حيث توقفت؟\nنعم = استئناف\nلا = تشغيل من البداية", /* アラビア語 */
					L"Данные возобновления существуют.\nПродолжить с места остановки?\nДа = Продолжить\nНет = Играть с начала", /* ロシア語 */
					L"Fortsetzungsdaten vorhanden.\nVon der Unterbrechungsstelle fortfahren?\nJa = Fortsetzen\nNein = Von Anfang abspielen", /* ドイツ語 */
					L"Dados de retomada existem.\nRetomar de onde parou?\nSim = Retomar\nNão = Reproduzir do início", /* ポルトガル語 */
					L"Hervatgegevens aanwezig.\nHervatten waar u gebleven was?\nJa = Hervatten\nNee = Afspelen vanaf het begin", /* オランダ語 */
					L"Istnieją dane wznowienia.\nWznowić od miejsca przerwania?\nTak = Wznów\nNie = Odtwórz od początku", /* ポーランド語 */
					L"Devam verisi mevcut.\nKaldığınız yerden devam edilsin mi?\nEvet = Devam et\nHayır = Baştan oynat"), /* トルコ語 */
					LL14(
						L"再生確認", /* 日本語タイトル */
						L"Playback confirmation",
						L"Confirmation de lecture",
						L"Conferma riproduzione",
						L"Confirmación de reproducción",
						L"재생 확인",
						L"播放确认",
						L"تأكيد التشغيل",
						L"Подтверждение воспроизведения",
						L"Wiedergabebestätigung",
						L"Confirmação de reprodução",
						L"Afspeelbevestiging",
						L"Potwierdzenie odtwarzania",
						L"Oynatma onayı"), /* トルコ語タイトル */
					MB_YESNO)) {
					flggg = 1;
				}
				else {
					CFile::Remove(filen + _T(".save"));
				}
			}
			if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE && flggg == 1) {
				f123.Close();
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl) {
					for (int y = 0; y < 45; y++) {
						Sleep(10); DoEvent();
						long eventCode;
						pMediaEvent->WaitForCompletion(-1, &eventCode);
						HRESULT hr = pMediaControl->Run();
						if (hr == S_OK) break;
					}
				}
				if (mode == -10) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
						f123.Read(&playb, sizeof(__int64));
						if (oggsize > 0 && playb > (__int64)oggsize)
							playb /= 4;
						if (savedata.mp3orig) {
							mp3_.seek2(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
						}
						else {
							mp3_.seek(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
						}
						f123.Close();
					}
				}
				if (mode == -2) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
						f123.Read(&aa1_, sizeof(double));
						pMainFrame1->seek((LONGLONG)(((float)((float)aa1_ * 100.0f * 100000.0f))));
						f123.Close();
					}
				}
			}
			else {
				if (pMainFrame1 && pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl) {
					for (int y = 0; y < 45; y++) {
						Sleep(10); DoEvent();
						HRESULT hr = pMediaControl->Run();
						if (hr == S_OK) break;
					}
				}
				if (pMainFrame1) { pMainFrame1->seek(0); }
			}
			//			if(pGraphBuilder)pMainFrame1->plays2();
			//			if(pMediaControl)pMediaControl->Run();
			//			if(pMediaPosition)pMediaPosition->put_CurrentPosition(0);
			int a = 0; aa2 = 0;
			REFTIME aa = 0;
			aa2 = 0;
			ResetPauseButtonUi();
			if (pMediaPosition)pMediaPosition->get_StopTime(&aa);
			aa1 = oggsize2 = aa;
			m_time.SetRange(0, (int)((REFTIME)aa * 100.0), TRUE);
			m_time.SetSelection(0, (int)((REFTIME)aa * 100.0) - 1);
			m_time.Invalidate();
			if (pl && plw) {
				int plc = -1;
				plc = pl->Add(fnn, mode, 0, 0, _T(""), _T(""), filen, 0, (int)aa, 1);
				if (plc == -1) {
					int i = pl->m_lc.GetItemCount() - 1;
					plcnt = i;
					pl->SIcon(i);
				}
				else {
					plcnt = plc;
					pl->SIcon(plc);
				}
			}
			SetTimer(1250, 100, NULL);
			plf = 1;
			CFile ff;
			CString ss11 = filen; ss11.MakeLower();
			if (ss11.Right(3) == "m4a") {
				if (ff.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
					mp3file = filen;
					ZeroMemory(bufimage, sizeof(bufimage));
					int i;
					ff.Read(bufimage, sizeof(bufimage));
					for (i = 0; i < 0x300000; i++) {// 00 06 5D 6A 64 61 74 61
						if (bufimage[i] == 0x63 && bufimage[i + 1] == 0x6f && bufimage[i + 2] == 0x76 && bufimage[i + 3] == 0x72 && bufimage[i + 8] == 0x64 && bufimage[i + 9] == 0x61 && bufimage[i + 10] == 0x74 && bufimage[i + 11] == 0x61) {
							break;
						}
					}
					m_mp3jake.EnableWindow(FALSE);
					if (i != 0x300000) {
						m_mp3jake.EnableWindow(TRUE);
					}
				}ff.Close();
			}
		}
		else {
			if (mode == 19)filen = filen.Left(5);
			play();
		}

	}
}


template<int Radius>
static inline float Lanczos(float x)
{
	if (x == 0.0) return 1.0;
	if (x <= -Radius || x >= Radius) return 0.0;
	float pi_x = x * M_PI;
	return Radius * sin(pi_x) * sin(pi_x / Radius) / (pi_x * pi_x);
}

const int FilterRadius = 3;

static inline void ResampleDouble(const double* source, size_t src_len, double* result, size_t dest_len) {
	if (src_len == dest_len) {
		memcpy(result, source, src_len * sizeof(double));
		return;
	}

	double scale = (double)src_len / (double)dest_len;
	for (size_t i = 0; i < dest_len; i++) {
		double src_pos = i * scale;
		size_t src_idx = (size_t)src_pos;
		double frac = src_pos - src_idx;

		if (src_idx >= src_len - 1) {
			result[i] = source[src_len - 1];
		}
		else {
			result[i] = source[src_idx] * (1.0 - frac) + source[src_idx + 1] * frac;
		}
	}
}

#define BUFSZ1_2 (BUFSZ1 * 4)
//スペアナ表示
ULONG PlayCursor = 0, WriteCursor = 0, PlayCursor2 = 0;
void AnalyzeMusicKey(
	const std::vector<double>& bufChordL, const std::vector<double>& bufChordR,
	int sampleRate)
	;
void PublishEqKeyPcm(const std::vector<double>& bufferL, const std::vector<double>& bufferR, int sampleRate);
void GetCurrentNoteStrengths(float* output108);

namespace {
constexpr int kSpeanaDispKeys = 88;

const double* SpeanaHannTable(int N)
{
	static double h4096[4096];
	static double h8192[8192];
	static bool ready = false;
	if (!ready) {
		for (int n = 0; n < 4096; n++)
			h4096[n] = 0.5 - 0.5 * cos(2.0 * M_PI * (double)n / 4095.0);
		for (int n = 0; n < 8192; n++)
			h8192[n] = 0.5 - 0.5 * cos(2.0 * M_PI * (double)n / 8191.0);
		ready = true;
	}
	return (N == 8192) ? h8192 : h4096;
}

void SpeanaApplyHann(const double* src, double* dst, int N)
{
	const double* h = SpeanaHannTable(N);
	for (int n = 0; n < N; n++)
		dst[n] = src[n] * h[n];
}

void SpeanaEnsureBandCache(int cacheKey, bool mode1_Low, bool mode3_High, int fftSize,
	double* band_edges, int* band_bins)
{
	static int s_key = -1;
	static double s_edges[kSpeanaDispKeys + 1];
	static int s_bins[kSpeanaDispKeys + 1];
	if (cacheKey == s_key) {
		memcpy(band_edges, s_edges, sizeof(s_edges));
		memcpy(band_bins, s_bins, sizeof(s_bins));
		return;
	}
	s_key = cacheKey;
	const double fs = 44100.0;
	const int splitIndex = kSpeanaDispKeys / 2;
	if (mode1_Low) {
		const double lowStart = 20.0, highEnd = 800.0;
		for (int k = 0; k <= kSpeanaDispKeys; ++k)
			s_edges[k] = lowStart * pow(highEnd / lowStart, (double)k / kSpeanaDispKeys);
	}
	else if (mode3_High) {
		const double lowStart = 40.0, splitFreq = 3000.0, highEnd = 22000.0;
		for (int k = 0; k <= kSpeanaDispKeys; ++k) {
			if (k <= splitIndex)
				s_edges[k] = lowStart * pow(splitFreq / lowStart, (double)k / splitIndex);
			else
				s_edges[k] = splitFreq * pow(highEnd / splitFreq, (double)(k - splitIndex) / (kSpeanaDispKeys - splitIndex));
		}
	}
	else {
		const double minFreq = 30.0, maxFreq = 22000.0;
		for (int k = 0; k <= kSpeanaDispKeys; ++k)
			s_edges[k] = minFreq * pow(maxFreq / minFreq, (double)k / kSpeanaDispKeys);
	}
	for (int k = 0; k <= kSpeanaDispKeys; ++k) {
		s_bins[k] = (int)(s_edges[k] * fftSize / fs);
		if (s_bins[k] < 0) s_bins[k] = 0;
		if (s_bins[k] >= fftSize / 2) s_bins[k] = fftSize / 2 - 1;
	}
	memcpy(band_edges, s_edges, sizeof(s_edges));
	memcpy(band_bins, s_bins, sizeof(s_bins));
}

inline void SpeanaDrawBar(CDC& dc, int x, int bar_w, int idx, int d)
{
	if (spelv[idx] < d) { spelv[idx] = d; spetm[idx] = 0; }
	if (spelv[idx] > 0 || d > 0) {
		dc.FillSolidRect(x, (96 - spelv[idx]) * 4, bar_w, (spelv[idx] + 1) * 4, RGB(0, 128, 0));
		dc.FillSolidRect(x, (96 - d) * 4, bar_w, (d + 1) * 4, RGB(0, 255, 0));
		dc.FillSolidRect(x, (96 - spelv[idx]) * 4, bar_w, 4, RGB(255, 255, 0));
	}
}
} // namespace

void COggDlg_SyncPianoRollFast()
{
	if (!og) return;
	og->SyncPianoRollFast();
}

BOOL COgg_IsEqualizerVisible()
{
	if (!og || !og->m_EqualizerDlg) return FALSE;
	HWND h = og->m_EqualizerDlg->GetSafeHwnd();
	return h && ::IsWindow(h) && ::IsWindowVisible(h);
}

void COggDlg_SyncAnalyzerFast()
{
	if (!og) return;
	og->SyncAnalyzerFromPlayCursor();
}

void COggDlg::SyncPianoRollFast()
{
	// wav/ogg 非依存。KPI 等でも bufwav3+DS があれば解析する。
	if (plf != 1) return;
	if (!::IsWindow(m_PianoRollDlg->GetSafeHwnd())) return;
	SyncPianoRollFromPlayCursor();
}

void COggDlg::SyncAnalyzerFromPlayCursor()
{
	if (playf == 0 || thn1 || plf != 1) return;
	if (ps == 1) return;
	if (!::IsWindow(m_AnalyzerDlg->GetSafeHwnd())) return;
	if (!bufwav3 || !m_dsb) return;

	double sampleRate;
	int channels;
	int bitDepth;
	if (g_pcm_upscale_active && g_ds_pcm_ch >= 1 && g_ds_pcm_bits >= 8) {
		sampleRate = (double)g_ds_pcm_rate;
		channels = g_ds_pcm_ch;
		bitDepth = g_ds_pcm_bits;
	}
	else {
		sampleRate = (double)wavbit_sample_Hz;
		channels = wavchannel;
		bitDepth = wavsam_depth;
	}
	if (sampleRate < 8000.0) sampleRate = 44100.0;
	if (channels < 1) channels = 2;
	if (bitDepth != 8 && bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
		bitDepth = abs(bitDepth);
	if (bitDepth != 8 && bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
		return;

	const int bytesPerSample = (bitDepth / 8) < 1 ? 2 : (bitDepth / 8);
	const int bytesPerFrame = bytesPerSample * channels;
	const ULONG ringBytes = Bufwav3RingBytes();
	if (bytesPerFrame <= 0 || ringBytes <= (ULONG)bytesPerFrame) return;

	ULONG playCur = 0, writeCur = 0;
	if (InterlockedCompareExchange(&g_dsDeviceOpBusy, 0, 0) != 0)
		return;
	if (m_dsb->GetCurrentPosition(&playCur, &writeCur) != DS_OK)
		return;

	// endPos = PlayCursor + Speana latency。窓長はリングに収まるよう制限
	// （高レート/多ch アップスケールで probe がリング超過して早期 return しない）
	int winFrames = 4096;
	int winBytes = winFrames * bytesPerFrame;
	const ULONG maxWin = ringBytes / 4;
	if ((ULONG)winBytes > maxWin && bytesPerFrame > 0) {
		winFrames = (int)(maxWin / (ULONG)bytesPerFrame);
		if (winFrames < 64) winFrames = 64;
		winBytes = winFrames * bytesPerFrame;
	}
	if (winBytes <= 0 || (ULONG)winBytes >= ringBytes) return;

	const long readPos = SpeanaAnalysisReadPos(
		playCur, winBytes, bytesPerFrame, (int)ringBytes, sampleRate, 0);
	ULONG endPos = (ULONG)((readPos + winBytes) % (long)ringBytes);
	endPos -= (endPos % (ULONG)bytesPerFrame);

	if (!m_analyzerSyncValid) {
		m_analyzerSyncEndPos = endPos;
		m_analyzerSyncValid = TRUE;
		m_AnalyzerDlg->ResumePlaybackFeed();
		return;
	}

	ULONG advance = (endPos + ringBytes - m_analyzerSyncEndPos) % ringBytes;
	advance -= (advance % (ULONG)bytesPerFrame);
	if (advance == 0) return;

	// シーク等で飛んだときは再同期のみ
	const ULONG maxJump = (ULONG)((sampleRate * (double)bytesPerFrame) / 2.0); // ~0.5s
	if (advance > maxJump || advance > ringBytes / 4) {
		m_analyzerSyncEndPos = endPos;
		return;
	}

	// 1回の Feed は最大 ~50ms（高レートで UI を詰まらせない）
	const ULONG feedCap = (ULONG)((sampleRate * (double)bytesPerFrame) / 20.0);
	if (feedCap >= (ULONG)bytesPerFrame && advance > feedCap) {
		advance = feedCap - (feedCap % (ULONG)bytesPerFrame);
		if (advance == 0) advance = (ULONG)bytesPerFrame;
	}

	const ULONG startPos = (endPos + ringBytes - advance) % ringBytes;
	static std::vector<char> anaRaw;
	if (anaRaw.size() < (size_t)advance)
		anaRaw.resize(advance);

	const char* src = (const char*)bufwav3;
	if (startPos + advance <= ringBytes) {
		memcpy(anaRaw.data(), src + startPos, advance);
	}
	else {
		const ULONG first = ringBytes - startPos;
		memcpy(anaRaw.data(), src + startPos, first);
		memcpy(anaRaw.data() + first, src, advance - first);
	}

	m_analyzerSyncEndPos = endPos;
	m_AnalyzerDlg->ResumePlaybackFeed();
	const int frames = (int)(advance / (ULONG)bytesPerFrame);
	if (frames > 0)
		m_AnalyzerDlg->FeedPCM(anaRaw.data(), frames, (int)(sampleRate + 0.5), bitDepth, channels);
}

void COggDlg::SyncPianoRollFromPlayCursor()
{
	if (playf == 0 || thn1 || plf != 1) return;
	if (ps == 1) return; // 一時停止中は履歴スクロール・解析更新を止める
	if (!::IsWindow(m_PianoRollDlg->GetSafeHwnd())) return;
	if (!bufwav3) return;
	// DS 未生成中は解析しない（形式切替の隙間で旧パラメータを使わない）
	if (!m_dsb) return;

	double sampleRate;
	int channels;
	int bitDepth;
	if (g_pcm_upscale_active && g_ds_pcm_ch >= 1 && g_ds_pcm_bits >= 8) {
		sampleRate = (double)g_ds_pcm_rate;
		channels = g_ds_pcm_ch;
		bitDepth = g_ds_pcm_bits;
	}
	else {
		sampleRate = (double)wavbit_sample_Hz;
		channels = wavchannel;
		bitDepth = wavsam_depth;
	}
	if (sampleRate < 8000.0) sampleRate = 44100.0;
	if (channels < 1) channels = 2;
	if (bitDepth != 8 && bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
		bitDepth = abs(bitDepth);
	if (bitDepth != 8 && bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
		return;

	const int bytesPerSample = (bitDepth / 8) < 1 ? 2 : (bitDepth / 8);
	const int bytesPerFrame = bytesPerSample * channels;
	const int TOTAL_BUF_BYTES = (int)((g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : (ULONG)(OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM));
	if (bytesPerFrame <= 0 || TOTAL_BUF_BYTES <= bytesPerFrame) return;

	m_PianoRollDlg->ResumePlaybackFeed();

	ULONG playCur = 0, writeCur = 0;
	if (InterlockedCompareExchange(&g_dsDeviceOpBusy, 0, 0) != 0)
		return;
	if (m_dsb->GetCurrentPosition(&playCur, &writeCur) != DS_OK)
		return;
	PlayCursor2 = playCur;
	WriteCursor = writeCur;

	const ULONG ringBytes = Bufwav3RingBytes();
	const int kMeterExtraLatencyMs = 700;
	const int kPianoRollExtraLatencyMs = 700;
	const int srInt = (int)(sampleRate + 0.5);

	static std::vector<char> prRaw;
	static std::vector<char> meterRaw;
	static std::vector<double> prMono;

	const char* srcBufBase = (const char*)bufwav3;
	auto ReadRingPcm = [&](long bytePos, int byteCount, char* dst) -> bool {
		if (bytePos < 0 || byteCount <= 0) return false;
		if (bytePos + byteCount <= TOTAL_BUF_BYTES) {
			memcpy(dst, srcBufBase + bytePos, byteCount);
			return true;
		}
		const int firstPart = TOTAL_BUF_BYTES - (int)bytePos;
		const int secondPart = byteCount - firstPart;
		if (firstPart < 0 || secondPart < 0) return false;
		memcpy(dst, srcBufBase + bytePos, firstPart);
		memcpy(dst + firstPart, srcBufBase, secondPart);
		return true;
		};

	auto GetSampleValueFrom = [&](const char* pcmBase, int sampleIndex, int chIndex) -> double {
		const int offset = sampleIndex * bytesPerFrame + chIndex * bytesPerSample;
		if (bitDepth == 16) return (double)(*(const short*)(pcmBase + offset)) / 32768.0;
		if (bitDepth == 24) {
			const unsigned char* p = (const unsigned char*)(pcmBase + offset);
			int val = (p[0]) | (p[1] << 8) | (p[2] << 16);
			if (val & 0x800000) val |= 0xFF000000;
			return (double)val / 8388608.0;
		}
		if (bitDepth == 32) return (double)(*(const int*)(pcmBase + offset)) / 2147483648.0;
		if (bitDepth == 8) return ((double)(*(const unsigned char*)(pcmBase + offset)) - 128.0) / 128.0;
		return 0.0;
		};

	// メーターは軽量窓のみ。解析ジョブ受付不可時でも UI メーターは更新する。
	const int meterCh = (channels < 1) ? 1 : ((channels > CPianoRoll::PIANO_METER_CH_MAX) ? CPianoRoll::PIANO_METER_CH_MAX : channels);
	const int meterFrames = CPianoRoll::ScaleWinSamples(2048, srInt);
	const int meterBytes = meterFrames * bytesPerFrame;
	static std::vector<double> chPeak;
	static std::vector<double> chSumSq;
	if ((int)chPeak.size() < meterCh) chPeak.resize((size_t)meterCh);
	if ((int)chSumSq.size() < meterCh) chSumSq.resize((size_t)meterCh);
	for (int ch = 0; ch < meterCh; ++ch) {
		chPeak[ch] = 0.0;
		chSumSq[ch] = 0.0;
	}
	int meterN = 0;
	if (meterBytes > 0) {
		if (meterRaw.size() < (size_t)meterBytes) meterRaw.resize(meterBytes);
		const long meterPos = SpeanaAnalysisReadPos(playCur, meterBytes, bytesPerFrame, (int)ringBytes, sampleRate, kMeterExtraLatencyMs);
		if (ReadRingPcm(meterPos, meterBytes, meterRaw.data())) {
			const char* mDst = meterRaw.data();
			meterN = meterFrames;
			for (int i = 0; i < meterFrames; ++i) {
				for (int ch = 0; ch < meterCh; ++ch) {
					const double a = fabs(GetSampleValueFrom(mDst, i, ch));
					if (a > chPeak[ch]) chPeak[ch] = a;
					chSumSq[ch] += a * a;
				}
			}
		}
	}

	float chDb[CPianoRoll::PIANO_METER_CH_MAX];
	for (int ch = 0; ch < meterCh; ++ch) {
		double level = chPeak[ch];
		if (meterN > 0) {
			const double rms = sqrt(chSumSq[ch] / (double)meterN);
			if (rms * 4.0 > level) level = rms * 4.0;
		}
		if (level < 1e-9) chDb[ch] = -60.0f;
		else chDb[ch] = (float)(20.0 * log10(level));
	}
	m_PianoRollDlg->SetChannelMeterDb(chDb, meterCh);

	// 解析ビジー/スロットル中に ~8192ch 変換すると EQ/MP の UI が飢える。
	// 受付可能時だけ重いキャプチャ＋モノラル化を行う。
	if (!m_PianoRollDlg->ShouldCaptureAnalyzeJob())
		return;

	const int ringFrames = TOTAL_BUF_BYTES / bytesPerFrame;
	int speanaFramesRef = 4096;
	if (savedata.speanamode == 1 && (savedata.speananum == 0 || savedata.speananum == 1))
		speanaFramesRef = 8192;
	const int speanaFrames = CPianoRoll::ScaleWinSamples(speanaFramesRef, srInt);
	const int capMargin = 128;
	const int ringUsable = ringFrames - capMargin;
	if (ringUsable <= 0) return;
	const int minNeed = CPianoRoll::MinAnalyzeFrameCount(srInt, ringUsable);
	if (ringUsable < minNeed) return;
	int maxPrFrames = ringUsable;
	if (speanaFrames > 0 && ringUsable > speanaFrames + minNeed)
		maxPrFrames = ringUsable - speanaFrames;
	if (maxPrFrames < minNeed)
		maxPrFrames = ringUsable;
	const int prFrames = CPianoRoll::CaptureFrameCount(srInt, maxPrFrames);
	if (prFrames < minNeed) return;
	const int prBytes = prFrames * bytesPerFrame;
	if (prBytes <= 0 || prBytes > TOTAL_BUF_BYTES) return;

	const int speanaBytes = speanaFrames * bytesPerFrame;
	long prPos = PianoRollWideReadPos(playCur, prBytes, speanaBytes, bytesPerFrame, (int)ringBytes, sampleRate, kPianoRollExtraLatencyMs);

	if (prRaw.size() < (size_t)prBytes) prRaw.resize(prBytes);
	if (prMono.size() < (size_t)prFrames) prMono.resize(prFrames);

	char* prDst = prRaw.data();
	if (!ReadRingPcm(prPos, prBytes, prDst))
		return;

	auto GetSampleValue = [&](int sampleIndex, int chIndex) -> double {
		return GetSampleValueFrom(prDst, sampleIndex, chIndex);
		};

	for (int i = 0; i < prFrames; ++i) {
		double smpL = 0.0, smpR = 0.0;
		if (channels <= 2) {
			smpL = GetSampleValue(i, 0);
			smpR = (channels > 1) ? GetSampleValue(i, 1) : smpL;
		}
		else if (g_pcm_upscale_active && savedata.speaker_layout == 5 && channels > 2) {
			smpL = GetSampleValue(i, 0);
			smpR = (channels > 1) ? GetSampleValue(i, 1) : smpL;
		}
		else if (g_pcm_upscale_active && channels == 3 && savedata.speaker_layout == 1) {
			const double fl = GetSampleValue(i, 0), fr = GetSampleValue(i, 1), lfe = GetSampleValue(i, 2);
			smpL = fl + lfe * 0.5;
			smpR = fr + lfe * 0.5;
		}
		else if (g_pcm_upscale_active && channels == 4 && savedata.speaker_layout == 2) {
			const double fl = GetSampleValue(i, 0), fr = GetSampleValue(i, 1);
			const double rl = GetSampleValue(i, 2), rr = GetSampleValue(i, 3);
			smpL = (fl + rl * 0.8) * 0.7;
			smpR = (fr + rr * 0.8) * 0.7;
		}
		else if (g_pcm_upscale_active && channels >= 8) {
			const double fl = GetSampleValue(i, 0), fr = GetSampleValue(i, 1);
			const double c = GetSampleValue(i, 2), lfe = GetSampleValue(i, 3);
			const double bl = GetSampleValue(i, 4), br = GetSampleValue(i, 5);
			const double sl = GetSampleValue(i, 6), sr = GetSampleValue(i, 7);
			smpL = (fl + c * 0.7 + bl * 0.75 + sl * 0.75 + lfe * 0.5) * 0.55;
			smpR = (fr + c * 0.7 + br * 0.75 + sr * 0.75 + lfe * 0.5) * 0.55;
		}
		else {
			const double fl = GetSampleValue(i, 0), fr = GetSampleValue(i, 1);
			const double center = (channels > 2) ? GetSampleValue(i, 2) : 0.0;
			const double lfe = (channels > 3) ? GetSampleValue(i, 3) : 0.0;
			const double rl = (channels > 4) ? GetSampleValue(i, 4) : 0.0;
			const double rr = (channels > 5) ? GetSampleValue(i, 5) : 0.0;
			smpL = (fl + center * 0.7 + rl * 0.8 + lfe * 0.5) * 0.7;
			smpR = (fr + center * 0.7 + rr * 0.8 + lfe * 0.5) * 0.7;
		}
		prMono[i] = (smpL + smpR) * 0.5;
	}

	m_PianoRollDlg->AnalyzePlayCursorMono(prMono.data(), prFrames, (int)(sampleRate + 0.5));
}

void COggDlg::Speana(BOOL bPaintBars)
{
	int i, j, d;
	double dt = 0.0, dta = 0.0;
	locs = loc;

	// ---------------------------------------------------------
	// 共通定数・変数
	// ---------------------------------------------------------
	const int DISP_KEYS = 88;
	const int DETECT_KEYS = 108;
	const int KEY_OFFSET = 9;

	static int dtbl[88];
	static int dtatbl[88];
	const bool stereoSpeana = (m_st.GetCheck() != FALSE);

	// ---------------------------------------------------------
	// モード判定
	// ---------------------------------------------------------
	bool mode0_Note = (savedata.speanamode == 1 && savedata.speananum == 0);
	bool mode1_Low = (savedata.speanamode == 1 && savedata.speananum == 1);
	bool mode3_High = (savedata.speanamode == 1 && savedata.speananum == 3);
	bool mode4_Vox = (savedata.speanamode == 1 && savedata.speananum == 4);
	bool mode2_Std = (!mode0_Note && !mode1_Low && !mode3_High && !mode4_Vox);

	// ---------------------------------------------------------
	// パラメータ設定（アップスケール時は bufwav3 = DS 出力 g_ds_pcm_*）
	// ---------------------------------------------------------
	double sampleRate;
	int channels;
	int bitDepth;
	if (g_pcm_upscale_active && g_ds_pcm_ch >= 1 && g_ds_pcm_bits >= 8) {
		sampleRate = (double)g_ds_pcm_rate;
		channels = g_ds_pcm_ch;
		bitDepth = g_ds_pcm_bits;
	}
	else {
		sampleRate = (double)wavbit_sample_Hz;
		channels = wavchannel;
		bitDepth = wavsam_depth;
	}
	if (sampleRate < 8000.0) sampleRate = 44100.0;
	if (channels < 1) channels = 2;

	int bytesPerSample = bitDepth / 8;
	if (bytesPerSample < 1) bytesPerSample = 2;

	int bytesPerFrame = bytesPerSample * channels;

	// 解析バッファサイズ
	// 低音解像度が必要なモード(0,1)はサイズを大きく取る
	// mode0(音階)は88鍵ノート検出エンジン用に低音窓16384サンプルを確保する。
	// FFT/ddst・Hann テーブルは 4096/8192/16384 固定のためレート比例にしない。
	// readPos+bytesTotalToRead = PlayCursor+latencyBytes なので、読み取り長を
	// 伸ばしても末尾(同期点)はlatencySettingで決まり、表示タイミングは不変。
	int analysisSize = 4096;
	// EQ コード供給のみのときは長窓不要（読み取りコストと揺らぎを抑える）
	if (bPaintBars) {
		if (mode1_Low) analysisSize = 8192;
		if (mode0_Note) analysisSize = 16384;
	}

	// タイミング調整 (Latency) — ユーザー調整値。低音長窓→-1600ms / 4096→-800ms
	int latencySetting = (mode0_Note || analysisSize == 8192) ? -1600 : -800;

	{
		const int rateForLatency = (int)(sampleRate + 0.5);
		if (rateForLatency > 0 && rateForLatency < 44100) {
			latencySetting = (int)((float)latencySetting * (44100.0f / (float)rateForLatency));
		}
	}

	// 遅延バイト数（負の値）
	long latencyBytes = (long)(sampleRate * bytesPerFrame * latencySetting / 1000.0);

	int framesToRead = analysisSize;
	int fftSize = analysisSize;
	int bytesTotalToRead = framesToRead * bytesPerFrame;
	const int TOTAL_BUF_BYTES = (int)((g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : (ULONG)(OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM));

	// バッファ長超過ガード (Safety Clamp)
	// レイテンシがバッファ全長を超えて周回(未来読み)しないよう制限
	long maxSafeLatency = -(long)(TOTAL_BUF_BYTES * 0.9) + bytesTotalToRead;
	if (latencyBytes < maxSafeLatency) {
		latencyBytes = maxSafeLatency;
	}
	if (latencyBytes > 0)
		latencyBytes = 0;

	// ---------------------------------------------------------
	// メモリ確保
	// ---------------------------------------------------------
	static std::vector<double> bufL, bufR, bufM, bufResampled, bufResampledR;
	static std::vector<char> rawBuf;

	if (bufL.size() < (size_t)framesToRead) {
		bufL.resize(framesToRead); bufR.resize(framesToRead); bufM.resize(framesToRead);
	}
	if (bufResampled.size() < (size_t)fftSize) {
		bufResampled.resize(fftSize); bufResampledR.resize(fftSize);
	}
	if (rawBuf.size() < (size_t)bytesTotalToRead) {
		rawBuf.resize(bytesTotalToRead);
	}

	// ---------------------------------------------------------
	// データ読み込み (完全過去データ取得)
	// ---------------------------------------------------------
	HRESULT rett = E_FAIL;
	if (m_dsb && InterlockedCompareExchange(&g_dsDeviceOpBusy, 0, 0) == 0)
		rett = m_dsb->GetCurrentPosition(&PlayCursor, &WriteCursor);
	if (rett == DS_OK) { PlayCursor2 = PlayCursor; }
	else { PlayCursor = PlayCursor2; }

	long readPos = (long)PlayCursor - bytesTotalToRead + latencyBytes;

	while (readPos < 0) readPos += TOTAL_BUF_BYTES;
	while (readPos >= TOTAL_BUF_BYTES) readPos -= TOTAL_BUF_BYTES;
	readPos -= (readPos % bytesPerFrame);

	const char* srcBufBase = (const char*)bufwav3;
	char* dstRaw = rawBuf.data();
	bool readSuccess = true;

	if (readPos + bytesTotalToRead <= TOTAL_BUF_BYTES) {
		memcpy(dstRaw, srcBufBase + readPos, bytesTotalToRead);
	}
	else {
		int firstPart = TOTAL_BUF_BYTES - readPos;
		int secondPart = bytesTotalToRead - firstPart;
		if (firstPart >= 0 && secondPart >= 0) {
			memcpy(dstRaw, srcBufBase + readPos, firstPart);
			memcpy(dstRaw + firstPart, srcBufBase, secondPart);
		}
		else {
			readSuccess = false;
		}
	}

	// ---------------------------------------------------------
	// サンプル抽出
	// ---------------------------------------------------------
	if (readSuccess) {
		auto GetSampleValue = [&](int sampleIndex, int chIndex) -> double {
			int offset = sampleIndex * bytesPerFrame + chIndex * bytesPerSample;
			if (bitDepth == 16) return (double)(*(short*)(dstRaw + offset)) / 32768.0;
			if (bitDepth == 24) {
				unsigned char* p = (unsigned char*)(dstRaw + offset);
				int val = (p[0]) | (p[1] << 8) | (p[2] << 16);
				if (val & 0x800000) val |= 0xFF000000;
				return (double)val / 8388608.0;
			}
			if (bitDepth == 32) return (double)(*(int*)(dstRaw + offset)) / 2147483648.0;
			if (bitDepth == 8) return ((double)(*(unsigned char*)(dstRaw + offset)) - 128.0) / 128.0;
			return 0.0;
			};

		for (i = 0; i < framesToRead; i++) {
			double smpL = 0.0, smpR = 0.0;
			if (channels <= 2) {
				smpL = GetSampleValue(i, 0);
				smpR = (channels > 1) ? GetSampleValue(i, 1) : smpL;
			}
			else if (g_pcm_upscale_active && savedata.speaker_layout == 5 && channels > 2) {
				smpL = GetSampleValue(i, 0);
				smpR = (channels > 1) ? GetSampleValue(i, 1) : smpL;
			}
			else if (g_pcm_upscale_active && channels == 3 && savedata.speaker_layout == 1) {
				// 2.1: L,R,LFE（センターではない）
				double fl = GetSampleValue(i, 0), fr = GetSampleValue(i, 1), lfe = GetSampleValue(i, 2);
				smpL = fl + lfe * 0.5;
				smpR = fr + lfe * 0.5;
			}
			else if (g_pcm_upscale_active && channels == 4 && savedata.speaker_layout == 2) {
				// 4.0 アップミックス: L,R,L,R（前後で同系統）
				double fl = GetSampleValue(i, 0), fr = GetSampleValue(i, 1);
				double rl = GetSampleValue(i, 2), rr = GetSampleValue(i, 3);
				smpL = (fl + rl * 0.8) * 0.7;
				smpR = (fr + rr * 0.8) * 0.7;
			}
			else if (g_pcm_upscale_active && channels >= 8) {
				// 7.1: FL,FR,FC,LFE,BL,BR,SL,SR
				double fl = GetSampleValue(i, 0), fr = GetSampleValue(i, 1);
				double c = GetSampleValue(i, 2), lfe = GetSampleValue(i, 3);
				double bl = GetSampleValue(i, 4), br = GetSampleValue(i, 5);
				double sl = GetSampleValue(i, 6), sr = GetSampleValue(i, 7);
				smpL = (fl + c * 0.7 + bl * 0.75 + sl * 0.75 + lfe * 0.5) * 0.55;
				smpR = (fr + c * 0.7 + br * 0.75 + sr * 0.75 + lfe * 0.5) * 0.55;
			}
			else {
				// 5.1 等: FL,FR,C,LFE,BL,BR
				double fl = GetSampleValue(i, 0), fr = GetSampleValue(i, 1);
				double center = (channels > 2) ? GetSampleValue(i, 2) : 0.0;
				double lfe = (channels > 3) ? GetSampleValue(i, 3) : 0.0;
				double rl = (channels > 4) ? GetSampleValue(i, 4) : 0.0;
				double rr = (channels > 5) ? GetSampleValue(i, 5) : 0.0;
				smpL = (fl + center * 0.7 + rl * 0.8 + lfe * 0.5) * 0.7;
				smpR = (fr + center * 0.7 + rr * 0.8 + lfe * 0.5) * 0.7;
			}
			bufL[i] = smpL; bufR[i] = smpR;
		}
	}
	else {
		std::fill(bufL.begin(), bufL.end(), 0.0);
		std::fill(bufR.begin(), bufR.end(), 0.0);
	}

	// EQ コード用 PCM。描画有無に関係なく供給（pending 中の不規則更新を防ぐ）
	{
		if (COgg_IsEqualizerVisible()) {
			static DWORD s_lastPubMs = 0;
			const DWORD nowPub = GetTickCount();
			if (s_lastPubMs == 0 || (nowPub - s_lastPubMs) >= (DWORD)EqCodeIntervalMs()) {
				s_lastPubMs = nowPub;
				PublishEqKeyPcm(bufL, bufR, (int)sampleRate);
			}
		}
	}
	// EQ 供給のみのとき FFT 用リサンプルは不要（UI スレッドコスト削減）
	if (!bPaintBars)
		return;

	ResampleDouble(bufL.data(), framesToRead, bufResampled.data(), fftSize);
	ResampleDouble(bufR.data(), framesToRead, bufResampledR.data(), fftSize);
	for (int k = 0; k < fftSize; k++) bufM[k] = (bufResampled[k] + bufResampledR[k]) * 0.5;

	auto ValToBarHeight = [&](double amplitude) -> int {
		if (amplitude < 0.0001) return 0;
		double db = 20.0 * log10(amplitude);
		double h = (db + 60.0) * (96.0 / 60.0);
		if (h < 0) return 0; if (h > 96) return 96;
		return (int)h;
		};

	bool useFFT = (!mode0_Note);

	if (useFFT) {
		const int N = fftSize;
		if (!stereoSpeana) {
			SpeanaApplyHann(bufM.data(), aFFT2, N);
			ipTab2[0] = 0; ddst(N, -1, aFFT2, ipTab2, wTab2);
		}
		else {
			SpeanaApplyHann(bufResampled.data(), aFFT2, N);
			ipTab2[0] = 0; ddst(N, -1, aFFT2, ipTab2, wTab2);
			SpeanaApplyHann(bufResampledR.data(), aFFT2a, N);
			ipTab2[0] = 0; ddst(N, -1, aFFT2a, ipTab2, wTab2);
		}
		aFFT2[0] = 0; aFFT2a[0] = 0; // DCカット

		double band_edges[DISP_KEYS + 1];
		int band_bins[DISP_KEYS + 1];
		const double normFactor = 4.0 / fftSize;
		const int bandCacheKey = (mode1_Low ? 1 : 0) | (mode3_High ? 2 : 0) | (mode4_Vox ? 4 : 0) | (fftSize << 4);
		SpeanaEnsureBandCache(bandCacheKey, mode1_Low, mode3_High, fftSize, band_edges, band_bins);

		for (i = 0; i < DISP_KEYS; i++) {
			int bin_start = band_bins[i];
			int bin_end = band_bins[i + 1];
			if (bin_start < 1) bin_start = 1;
			if (bin_end <= bin_start) bin_end = bin_start + 1;
			if (bin_end > fftSize / 2) bin_end = fftSize / 2;

			dt = 0; dta = 0;
			if (!stereoSpeana) {
				for (j = bin_start; j < bin_end; j++) {
					const double v = fabs(aFFT2[j]);
					if (dt < v) dt = v;
				}
			}
			else {
				for (j = bin_start; j < bin_end; j++) {
					const double vl = fabs(aFFT2[j]);
					const double vr = fabs(aFFT2a[j]);
					if (dt < vl) dt = vl;
					if (dta < vr) dta = vr;
				}
			}

			// 高音ブースト (スロープ補正)
			double slope = 1.0 + ((double)i / DISP_KEYS) * 3.0;
			dt *= slope; dta *= slope;

			// ========================================
			// モード4: 音声特化
			// ========================================
			if (mode4_Vox) {
				double centerFreq = (band_edges[i] + band_edges[i + 1]) / 2.0;
				if (dt < 0.005) dt = 0.0; if (dta < 0.005) dta = 0.0;
				if (centerFreq >= 300.0 && centerFreq <= 3400.0) { dt *= 2.0; dta *= 2.0; }
				else { dt *= 0.5; dta *= 0.5; }
			}

			// ★ゲイン調整 (Headroom確保)
			// +2dB程度の入力があっても天井に張り付かないよう、係数を0.8に下げる
			dt *= normFactor * savedata.wup * 0.8;
			dta *= normFactor * savedata.wup * 0.8;

			if (mode3_High && i > DISP_KEYS / 2) { dt *= 3.0; dta *= 3.0; }

			dtbl[i] = ValToBarHeight(dt);
			dtatbl[i] = ValToBarHeight(dta);
		}

		// 描画ループ (FFT)
		if (!stereoSpeana) {
			for (i = 0; i < DISP_KEYS; i++)
				SpeanaDrawBar(dc, (21 * 8 + i * 2) * 4, 8, i, dtbl[i]);
		}
		else {
			for (i = 0; i < DISP_KEYS; i++) {
				SpeanaDrawBar(dc, (21 * 8 + i) * 4, 4, 100 + i, dtbl[i]);
				SpeanaDrawBar(dc, (21 * 8 + 89 + i) * 4, 4, 200 + i, dtatbl[i]);
			}
			dc.FillSolidRect((21 * 8 + 88) * 4, 20, 4, 368, RGB(0, 255, 255));
		}
	}

	// ========================================
	// モード0: 音階モード
	// ========================================
	else if (mode0_Note) {
		// 簡易ピアノロールと同一の88鍵ノート検出エンジンを使用（検出音のみバー表示）。
		// L/R独立検出のため検出器を3つ保持（モノ/左/右）。
		static SpeanaNoteDetector s_detMono, s_detL, s_detR;
		s_detMono.Configure(sampleRate);
		s_detL.Configure(sampleRate);
		s_detR.Configure(sampleRate);

		// バー高の視覚フォールオフ用保持配列（モノ=0 / L=1 / R=2）。
		// 検出のactiveが一瞬落ちても緑本体が即消えないよう、立ち上がりは即時・
		// 下降のみ緩やかにする（スペアナ定石。簡易ピアノロールの履歴帯に相当）。
		static int s_barHold[3][DISP_KEYS] = {};
		auto DrawDetected = [&](const SpeanaNoteDetector& det, int offset_idx, bool isRight) {
			const bool* act = det.Active();
			const float* st = det.Strength();
			static double s_displayAmp[88];
			for (int i = 0; i < DISP_KEYS; ++i)
				s_displayAmp[i] = act[i] ? (double)st[i] : 0.0;

			NormalizeDisplayPeakD(s_displayAmp, DISP_KEYS, 5.0);

			const int bank = (offset_idx == 0) ? 0 : (offset_idx == 100 ? 1 : 2);
			const int FALL_PER_FRAME = 9; // 高さ(0..96)単位。約150ms(@16ms)で消える
			for (int i = 0; i < DISP_KEYS; i++) {
				int target = ValToBarHeight(s_displayAmp[i] * savedata.wup);
				int held = s_barHold[bank][i];
				if (target >= held) held = target;               // 立ち上がりは即時
				else { held -= FALL_PER_FRAME; if (held < target) held = target; } // 下降は緩やか
				s_barHold[bank][i] = held;

				const int idx = offset_idx + i;
				int x, w;
				if (!isRight && !stereoSpeana) { x = (21 * 8 + i * 2) * 4; w = 8; }
				else if (!isRight) { x = (21 * 8 + i) * 4; w = 4; }
				else { x = (21 * 8 + 89 + i) * 4; w = 4; }
				SpeanaDrawBar(dc, x, w, idx, held);
			}
			};

		if (!stereoSpeana) {
			static std::vector<double> monoInput;
			if ((int)monoInput.size() < framesToRead) monoInput.resize(framesToRead);
			for (int k = 0; k < framesToRead; k++) monoInput[k] = (bufL[k] + bufR[k]) * 0.5;
			s_detMono.Process(monoInput.data(), framesToRead);
			DrawDetected(s_detMono, 0, false);
		}
		else {
			s_detL.Process(bufL.data(), framesToRead);
			s_detR.Process(bufR.data(), framesToRead);
			DrawDetected(s_detL, 100, false);
			DrawDetected(s_detR, 200, true);
			dc.FillSolidRect((21 * 8 + 88) * 4, 20, 4, 368, RGB(0, 255, 255));
		}
	}
}

// ========================================
// 補助関数
// ========================================

// Goertzelアルゴリズム実装
double COggDlg::goertzel(const float* data, int N, double target_freq, double sample_rate)
{
	double k = round((double)N * target_freq / sample_rate);
	double omega = (2.0 * M_PI * k) / N;
	double sine = sin(omega);
	double cosine = cos(omega);
	double coeff = 2.0 * cosine;

	double q0 = 0;
	double q1 = 0;
	double q2 = 0;

	for (int i = 0; i < N; i++) {
		q0 = coeff * q1 - q2 + data[i];
		q2 = q1;
		q1 = q0;
	}

	double real = (q1 - q2 * cosine);
	double imag = (q2 * sine);
	double magnitude = sqrt(real * real + imag * imag);

	return magnitude / N;
}

// ハニング窓関数
double COggDlg::hanWindow(int value, int index, int offset, int size)
{
	double w = 0.5 - 0.5 * cos(2.0 * M_PI * (index - offset) / (size - 1));
	return value * w;
}

void COggDlg::OnButton5()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	if (fadeadd == 0.0)
	{
		fadeadd = -0.005f;
		fade = 1.0f;
	}
}


int COggDlg::moji(CString s, int x, int y, COLORREF rgb)
{
	return mojiPx(s, x * 4, y, rgb);
}

int COggDlg::mojiPx(CString s, int x_px, int y, COLORREF rgb)
{
	HFONT fo;
	SIZE szinfo;
	fo = (HFONT)SelectObject(dc, hFont);

	BYTE r = GetRValue(rgb);
	BYTE g = GetGValue(rgb);
	BYTE b = GetBValue(rgb);
	double factor = 1.0 - (0.7 * m_jacketFocus);
	rgb = RGB((BYTE)(r * factor), (BYTE)(g * factor), (BYTE)(b * factor));

	SetTextColor(dc, rgb);
	SetBkColor(dc, RGB(0, 0, 0));
	dc.SetBkMode(TRANSPARENT);
	GetTextExtentPoint32(dc, s, s.GetLength(), &szinfo);
	if (Ms2DrawDue(ms2)) {
		dc.TextOut(x_px, y * 4, s, s.GetLength());
	}
	SelectObject(dc, fo);
	return szinfo.cx;
}

int COggDlg::mojisub(CString s, int x, int y, COLORREF rgb)
{
	CRect rect;
	HFONT fo;
	CSize szinfo;
	fo = (HFONT)SelectObject(dcsub, hFont);

	BYTE r = GetRValue(rgb);
	BYTE g = GetGValue(rgb);
	BYTE b = GetBValue(rgb);
	double factor = 1.0 - (0.7 * m_jacketFocus);
	rgb = RGB((BYTE)(r * factor), (BYTE)(g * factor), (BYTE)(b * factor));

	SetTextColor(dcsub, rgb);
	SetBkColor(dcsub, RGB(0, 0, 0));
	SetBkMode(dcsub, TRANSPARENT);
	szinfo = dcsub.GetOutputTextExtent(s);
	if (Ms2DrawDue(ms2)) {
		if (szinfo.cx < (MDC + 8) * 4)
			dcsub.FillSolidRect(0, 0, (MDC + 8) * 4, 30 * 4, RGB(0, 0, 0));
		else
			dcsub.FillSolidRect(0, 0, (szinfo.cx + MDC + 8) * 4, 30 * 4, RGB(0, 0, 0));
		dcsub.TextOut(x * 4, y * 4, s, s.GetLength());
		SelectObject(dcsub, fo);
	}
	return szinfo.cx;
}

// スクロールテキストの区切り装飾を dcsub に描画（mojisub の直後に呼ぶ）
// x_px : セパレータ描画開始 X 座標（4x スケール済みピクセル、テキスト末尾位置）
// h_px : テキスト行全体の高さ（4x スケール済みピクセル = 30*4）
// w_px : セパレータ全幅（4x スケール済みピクセル）
// 描画内容: 中央ダイヤモンド + 左右ドット + 細線
// SRCINVERT 合成のため白(0xFFFFFF)で描く（黒地→白表示、スペアナ色地→反転で存在感を保つ）
static void DrawScrollSepDeco(CDC& dc, int x_px, int h_px, int w_px, COLORREF clr)
{
	if (w_px < 32) return;
	const int cy = h_px / 2;

	CPen nullPen(PS_NULL, 0, RGB(0, 0, 0));
	CBrush br(clr);
	CBrush* ob = dc.SelectObject(&br);
	CPen* op = dc.SelectObject(&nullPen);

	// 中央ダイヤモンド
	const int cr = h_px / 10; // ダイヤモンドの半径（高さの 1/10）
	if (cr >= 2) {
		int cx = x_px + w_px / 2;
		POINT dia[4] = {
			{cx,      cy - cr},
			{cx + cr, cy},
			{cx,      cy + cr},
			{cx - cr, cy}
		};
		dc.Polygon(dia, 4);
	}

	// 左右の小ドット
	const int dr = max(2, h_px / 16);
	int lx = x_px + w_px / 4;
	int rx = x_px + w_px * 3 / 4;
	dc.Ellipse(lx - dr, cy - dr, lx + dr, cy + dr);
	dc.Ellipse(rx - dr, cy - dr, rx + dr, cy + dr);

	// 細線（ドット〜ダイヤモンド間）
	CPen linePen(PS_SOLID, 2, clr);
	dc.SelectObject(&linePen);
	dc.SelectStockObject(NULL_BRUSH);
	int mid_cx = x_px + w_px / 2;
	dc.MoveTo(lx + dr,       cy); dc.LineTo(mid_cx - cr - 1, cy);
	dc.MoveTo(mid_cx + cr + 1, cy); dc.LineTo(rx - dr,       cy);

	dc.SelectObject(ob);
	dc.SelectObject(op);
}

void COggDlg::OnButton9_Folder()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	CFolder* a = new CFolder(CWnd::FromHandle(GetSafeHwnd()));
	if (savedata.aero == 2) {
		::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		if (maini)::SetWindowPos(maini->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}
	CWnd::PostMessage(0x118);
	a->DoModal();
	Modec();
	char tmp[1024];
	_getcwd(tmp, 1000);
	_tchdir(karento2);
	CFile ab;
#if _UNICODE
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
	_chdir(tmp);
	delete a;
	}

void COggDlg::OnButton12()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	Resize();
}

void COggDlg::OnCheck5()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	m_junji.SetCheck(0);
	m_random.SetCheck(1);
	savedata.random = 0;
	{
		TCHAR tmp_savedir[1024];
		_tgetcwd(tmp_savedir, 1000);
		_tchdir(karento2);
		CFile ab;
#if _UNICODE
		if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
		if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
			ab.Write(&savedata, sizeof(save));
			ab.Close();
		}
		_tchdir(tmp_savedir);
	}
}

void COggDlg::OnCheck6()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	m_junji.SetCheck(1);
	m_random.SetCheck(0);
	savedata.random = 1;
	{
		TCHAR tmp_savedir[1024];
		_tgetcwd(tmp_savedir, 1000);
		_tchdir(karento2);
		CFile ab;
#if _UNICODE
		if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
		if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
			ab.Write(&savedata, sizeof(save));
			ab.Close();
		}
		_tchdir(tmp_savedir);
	}
}

void COggDlg::OnButton14()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	randomf = 1;
	randomno = 0;
	playf = 0;
	loopcnt = 0;
	//	m_rund.EnableWindow(FALSE);
}

BOOL sek = FALSE;
extern int sek4;
extern int syukai, syukai2;
extern BOOL	syoriflg;

void COggDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	// スライダーの特定（ここは共通処理なのでロック外）
	CSliderCtrl* r = (CSliderCtrl*)pScrollBar;
	if (!r || r->GetDlgCtrlID() != IDC_SLIDER2) return;

	int minpos;
	int maxpos;
	int curpos;
	double aa = 2000.0; // デフォルト値

	// サンプリングレートによる係数設定
	if (wavsam_depth == 32 || wavsam_depth == 24 || wavsam_depth == 16 || wavsam_depth == 8) aa = 2000.0;

	// トラック中のフラグ処理
	if (nSBCode == SB_THUMBTRACK) {
		hsc = 1;
		sflg = FALSE;
		return;
	}

	// 実際のシーク処理（移動を伴うもの）
	// SB_THUMBPOSITION: つまみを離した直後（環境によっては SB_ENDSCROLL が来ない）
	if (nSBCode == SB_PAGELEFT || nSBCode == SB_PAGERIGHT || nSBCode == SB_ENDSCROLL || nSBCode == SB_THUMBPOSITION) {

		// ★排他制御開始：再生スレッドとの競合を防ぐ
		std::unique_lock<std::mutex> hscroll_lock(cl2);

		ResetAudioUpscalerPipeline();

		r->GetRange(minpos, maxpos);
		curpos = r->GetPos();

		// ページ移動の計算
		if (nSBCode == SB_PAGELEFT) {
			int info = r->GetLineSize();
			if (curpos > minpos) curpos = max(minpos, curpos - info);
		}
		else if (nSBCode == SB_PAGERIGHT) {
			int info = r->GetLineSize();
			if (curpos < maxpos) curpos = min(maxpos, curpos + info);
			else { hsc = 2; sflg = FALSE; return; }
		}
		// SB_ENDSCROLL / SB_THUMBPOSITION の場合はこの時点の GetPos() が最終位置

		// ゴムバンドの破棄（シーク時は作り直し）
		if (g_rubberBandStretcher) {
			delete g_rubberBandStretcher;
			g_rubberBandStretcher = NULL;
		}

		// 位置の補正（mode==-2 では timerp が loop1=loop2=0 にするため、音声用クランプを掛けると常に先頭へ落ちる）
		if (pMediaPosition && (mode == -2 || (mode > 0 && videoonly == TRUE))) {
			if (curpos < minpos) curpos = minpos;
			if (curpos > maxpos) curpos = maxpos;
		}
		else {
			if ((loop1 + loop2) < curpos && endf == 0) curpos = (loop1 + loop2);
		}
		r->SetPos(curpos);
		playb = (__int64)curpos;

		// 1. 動画・メディアポジションのシーク
		if (pMediaPosition && (mode == -2 || (mode > 0 && videoonly == TRUE))) {
			if (aa2 == 0) pMainFrame1->seek((LONGLONG)((float)curpos * 100000.0f));
			else         pMainFrame1->seek((LONGLONG)((float)curpos * 100000.0f));
			hsc = 0;
			poss = 0;
		}
		else {
			// 2. 音声エンジンのシーク
			if (pMainFrame1) {
				pMainFrame1->seek((LONGLONG)(((float)curpos * 10000000.0f) / (float)wavbit_sample_Hz));
			}
			// 共通のバッファ・フラグ更新
			ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
			ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
			ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
			ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
			ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
			poss = 0;

			if (ModeUsesSeekAdpcm(mode)) {
				if (mode == -10) {
					hsc = 2;
					// m_time レンジは (総フレーム F)/100 程度なので、フレーム位置 = curpos×100
					playb = (__int64)curpos * 100;
					if (playb < 0) playb = 0;

					if (ps == 0) {
						// 一時停止中でない場合、再生スレッドの状態をリセットしてシーク
						OnPause();
						ZeroMemory(bufwav3, sizeof(bufwav3));
						syukai = 1; syukai2 = 0;
						// 旧 playb はフレーム×4 相当だったので、mp3orig 式の先頭に ×4 を入れて互換
						if (savedata.mp3orig) mp3_.seek2(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
						else                  mp3_.seek(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
						poss = 0; sek = TRUE; cnt3 = 0;
						timer.SetEvent();
						syukai = 0;
						OnPause();
					}
					else {
						// 再生中のバッファクリアとシーク
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						if (savedata.mp3orig) mp3_.seek2(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
						else                  mp3_.seek(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel);
					}
				}
				else if (mode == 999) {
					hsc = 2;
					if (ps == 0) {
						OnPause();
						ZeroMemory(bufwav3, sizeof(bufwav3));
						syukai = 1; syukai2 = 0;
						wav_.Seek(playb);
						poss = 0; sek = TRUE; cnt3 = 0;
						timer.SetEvent();
						syukai = 0;
						OnPause();
					}
					else {
						poss = 0; ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						wav_.Seek(playb);
					}
				}
				else {
					seekadpcm((int)playb);
				}
			}
			else if (mode == -3) { // KPI
				if (g_kpiRemote && g_kpiSession.sessionId != 0) {
					// playb は「サンプル位置」扱いなのでそのまま渡す
					uint64_t np = 0;
					g_kpiHost.Seek(g_kpiSession.sessionId, (uint64_t)playb, KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE, np);
					ResetKpiRemoteCache();
				}
				else if (mod && mod->SetPosition && sikpi.dwSeekable) {
					mod->SetPosition(kmp1, (DWORD)((double)playb / (((double)wavbit_sample_Hz * (double)wavchannel) / 2000.0)));
				}
			}
			else if (mode == -7) { // DSD
				dsd_.kpiSetPosition(kmp, (DWORD)((double)playb / (((double)wavbit_sample_Hz * (double)wavchannel) / 2000.0)));
			}
			else if (mode == -8) { // FLAC
				sek4 = TRUE;
				if (flacmode == 1) flac_.SetPosition(kmp, playb);
				else               flac_.SetPosition(kmp, (LONGLONG)((double)playb / (((double)wavbit_sample_Hz * (double)wavchannel) / aa)));
				::rrr = 1;  // シーク後は Render を再開
				sek4 = FALSE;
			}
			else if (mode == -9) { // M4A
				double wavv2[] = { 0, 2.0, 1.0, 1.0 / 2.0, 1.0 / 2.0, 1.0 / 2.0, 1.0 / 2.0 };
				DWORD pla = (DWORD)((double)playb / (((double)(wavbit2 / wavv2[wavchannel])) / ((wavchannel > 2) ? (1069.1 * wavchannel) : 1000.0)));
				pla = (pla / (wavchannel * 2) * (wavchannel * 2));
				m4a_.SetPosition(kmp, pla);
			}
			else { // OGG / Others
				SeekAndWarmupRubberBand((int)curpos, false);
				r->SetPos(curpos);
			}

			sek = TRUE;
			cnt3 = 0;
			timer.SetEvent();
			hsc = 0;
		}
		// ★ここで hscroll_lock がスコープを抜け、自動的に解除される
	}
	sflg = FALSE;
}

void COggDlg::OnButton21()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	CRender* r = new CRender(CWnd::FromHandle(GetSafeHwnd()));
	CWnd::PostMessage(0x118);
	int ret = r->DoModal();
	char tmp[1024];
	_getcwd(tmp, 1000);
	_tchdir(karento2);
	CFile ab;
#if _UNICODE
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
	_chdir(tmp);
	delete r;
	}


void COggDlg::OnNMReleasedcaptureSlider2(NMHDR * pNMHDR, LRESULT * pResult)
{
	*pResult = 0;
	if (!pNMHDR) return;
	HWND hwndFrom = (HWND)(UINT_PTR)pNMHDR->idFrom;
	if (hwndFrom != m_time.GetSafeHwnd()) return;
	if (!pMediaPosition || !pMainFrame1) return;
	if (!(mode == -2 || (mode > 0 && videoonly == TRUE))) return;
	// ドラッグ中のみ THUMBTRACK で hsc=1。SB_ENDSCROLL が来ない環境用の保険
	if (hsc != 1) return;

	std::unique_lock<std::mutex> hscroll_lock(cl2);
	CSliderCtrl* r = (CSliderCtrl*)GetDlgItem(IDC_SLIDER2);
	if (!r) return;

	int minpos, maxpos;
	r->GetRange(minpos, maxpos);
	int curpos = r->GetPos();
	if (curpos < minpos) curpos = minpos;
	if (curpos > maxpos) curpos = maxpos;
	r->SetPos(curpos);
	playb = (__int64)curpos;

	if (aa2 == 0) pMainFrame1->seek((LONGLONG)((float)curpos * 100000.0f));
	else         pMainFrame1->seek((LONGLONG)((float)curpos * 100000.0f));
	hsc = 0;
	poss = 0;
}



void COggDlg::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	CCustomBlurDialogBase::OnKeyDown(nChar, nRepCnt, nFlags);
}

LRESULT COggDlg::OnHotKey(WPARAM wp, LPARAM a)
{
	std::unique_lock<std::mutex> hscroll_lock(cl2);
	switch (wp) {
	case 8000:
		m_dsval.SetPos(m_dsval.GetPos() + 5);
		break;
	case 8001:
		m_dsval.SetPos(m_dsval.GetPos() - 5);
		break;
	case 8002:
		if (!((ogg || adbuf2 || mod || wav || mode == 999) || mode == -2)) break;
		playb = m_time.GetPos();
		if (pMediaPosition && ((mode == -2 && hsc == 0) || ((mode > 0 || mode < -10) && videoonly == TRUE && hsc == 0))) {
			playb += 10 * 100;
			m_time.SetPos((int)playb);
			if (aa2 == 0) {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 100000.0f)));
			}
			else {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * (100000.0f))));
			}
			hsc = 0;
		}
		else {
			if (mode == -10) {
				__int64 fb = (__int64)m_time.GetPos() * 100;
				fb += (__int64)wavbit_sample_Hz * (wavchannel == 2 ? 10 : 5);
				if (oggsize > 0 && fb > (__int64)oggsize) fb = (__int64)oggsize;
				if (fb < 0) fb = 0;
				playb = fb;
				m_time.SetPos((int)(playb / 100));
				if (pMainFrame1) {
					pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit_sample_Hz));
				}
				ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				poss = 0;
				if (ps == 0) {
					OnPause();
					ZeroMemory(bufwav3, sizeof(bufwav3));
					syukai = 1; syukai2 = 0;
					if (thn == FALSE) { hscroll_lock.unlock(); WaitForSyukai2OrPlaybackStop(); hscroll_lock.lock(); }
					if (savedata.mp3orig) {
						if (mp3_.seek2(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return 0; }
					}
					else {
						if (mp3_.seek(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return 0; }
					}
					poss = 0; sek = TRUE;
					timer.SetEvent();
					syukai = 0;
					OnPause();
				}
				else {
					if (savedata.mp3orig) {
						if (mp3_.seek2(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel) == FALSE) { return 0; }
					}
					else {
						if (mp3_.seek(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel) == FALSE) { return 0; }
					}
				}
				poss = 0;
				break;
			}
			playb += wavbit_sample_Hz * (wavchannel == 2 ? 10 : 5);
			if ((loop1 + loop2) < (int)playb && endf == 0) playb = (loop1 + loop2);
			if (mode != -10)m_time.SetPos((int)playb);
			if (pMainFrame1) {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit_sample_Hz));
			}
			if (ModeUsesSeekAdpcm(mode)) {
				seekadpcm((int)playb);
				sek = TRUE;
				timer.SetEvent();
			}
			else if (mode == -3) {
				if (g_kpiRemote && g_kpiSession.sessionId != 0) {
					uint64_t np = 0;
					g_kpiHost.Seek(g_kpiSession.sessionId, (uint64_t)playb, KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE, np);
					ResetKpiRemoteCache();
					sek = TRUE;
					timer.SetEvent();
				}
				else if (mod) {
					if (mod->SetPosition && sikpi.dwSeekable) mod->SetPosition(kmp1, (DWORD)((double)playb / (((double)wavbit_sample_Hz * (double)wavchannel) / 2000.0)));
					sek = TRUE;
					timer.SetEvent();
				}
			}
			else {
				ov_pcm_seek_lap(&vf, (ogg_int64_t)playb);
				sek = TRUE;
				timer.SetEvent();
			}
			poss = 0;
		}
		break;
	case 8003:
		if (!((ogg || adbuf2 || mod || wav || mode == 999) || mode == -2)) break;
		playb = m_time.GetPos();
		if (pMediaPosition && ((mode == -2 && hsc == 0) || ((mode > 0 || mode < -10) && videoonly == TRUE && hsc == 0))) {
			playb -= 10 * 100;
			m_time.SetPos((int)playb);
			if (aa2 == 0) {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 100000.0f)));
			}
			else {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * (100000.0f))));
			}
			hsc = 0;
		}
		else {
			if (mode == -10) {
				__int64 fb = (__int64)m_time.GetPos() * 100;
				fb -= (__int64)wavbit_sample_Hz * (wavchannel == 2 ? 10 : 5);
				if (fb < 0) fb = 0;
				playb = fb;
				m_time.SetPos((int)(playb / 100));
				if (pMainFrame1) {
					pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit_sample_Hz));
				}
				ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
				poss = 0;
				if (ps == 0) {
					OnPause();
					ZeroMemory(bufwav3, sizeof(bufwav3));
					syukai = 1; syukai2 = 0;
					if (thn == FALSE) { hscroll_lock.unlock(); WaitForSyukai2OrPlaybackStop(); hscroll_lock.lock(); }
					if (savedata.mp3orig) {
						if (mp3_.seek2(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return 0; }
					}
					else {
						if (mp3_.seek(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return 0; }
					}
					poss = 0; sek = TRUE;
					timer.SetEvent();
					syukai = 0;
					OnPause();
				}
				else {
					if (savedata.mp3orig) {
						if (mp3_.seek2(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel) == FALSE) { return 0; }
					}
					else {
						if (mp3_.seek(Mp3SeekDwPosFromPlaybFrames(playb), wavchannel) == FALSE) { return 0; }
					}
				}
				poss = 0;
				break;
			}
			playb -= wavbit_sample_Hz * (wavchannel == 2 ? 10 : 5);
			if ((loop1 + loop2) < (int)playb && endf == 0) playb = (loop1 + loop2);
			if (mode != -10)m_time.SetPos((int)playb);
			if (pMainFrame1) {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit_sample_Hz));
			}
			if (ModeUsesSeekAdpcm(mode)) {
				seekadpcm((int)playb);
				sek = TRUE;
				timer.SetEvent();
			}
			else if (mode == -3) {
				if (g_kpiRemote && g_kpiSession.sessionId != 0) {
					uint64_t np = 0;
					g_kpiHost.Seek(g_kpiSession.sessionId, (uint64_t)playb, KPI_MEDIAINFO::SEEK_FLAGS_SAMPLE, np);
					ResetKpiRemoteCache();
					sek = TRUE;
					timer.SetEvent();
				}
				else if (mod) {
					if (mod->SetPosition && sikpi.dwSeekable) mod->SetPosition(kmp1, (DWORD)((double)playb / (((double)wavbit_sample_Hz * (double)wavchannel) / 2000.0)));
					sek = TRUE;
					timer.SetEvent();
				}
			}
			else {
				ov_pcm_seek_lap(&vf, (ogg_int64_t)playb);
				sek = TRUE;
				timer.SetEvent();
			}
			poss = 0;
		}
		break;
	}
	return 0;
}

void COggDlg::rl(int a)
{
	if (pMediaPosition && (mode == -2 || (mode > 0 && videoonly == TRUE))) {
		int minp = 0, maxp = 0;
		m_time.GetRange(minp, maxp);
		playb = (__int64)m_time.GetPos();
		playb += (__int64)(10 * 100 * a);
		if (playb < minp) playb = minp;
		if (playb > maxp) playb = maxp;
		m_time.SetPos((int)playb);
		if (pMainFrame1) {
			if (aa2 == 0) pMainFrame1->seek((LONGLONG)((float)playb * 100000.0f));
			else         pMainFrame1->seek((LONGLONG)((float)playb * 100000.0f));
		}
		poss = 0;
		return;
	}
	playb += wavbit_sample_Hz * 10 * a;
	if ((loop1 + loop2) < (int)playb && endf == 0) playb = (loop1 + loop2);
	if (mode == -10)
		m_time.SetPos((int)(playb / 100));
	else
		m_time.SetPos((int)playb);
	if (pMainFrame1) {
		pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit_sample_Hz));
	}
	if (ModeUsesSeekAdpcm(mode)) {
		if (mode != -10)
			seekadpcm((int)playb);
		sek = TRUE;
		timer.SetEvent();
	}
	else {
		ov_pcm_seek_lap(&vf, (ogg_int64_t)playb);
		sek = TRUE;
		timer.SetEvent();
	}
	poss = 0;
}

void COggDlg::OnActivate(UINT nState, CWnd * pWndOther, BOOL bMinimized)
{

	CCustomBlurDialogBase::OnActivate(nState, pWndOther, bMinimized);
	int l = 5;
	if (plw && savedata.playerMode != 1) {
		if ((nState == WA_ACTIVE || nState == WA_CLICKACTIVE) && bMinimized == 0 && pl->m_saisyo.GetCheck()) {
			ogpl0 = 1;
			pl->ShowWindow(SW_RESTORE);
		}
	}
	if (nState == WA_INACTIVE) //非アクティブ
	{
		// 保留中の再登録タイマーが後から発火して再登録するのを防ぐ。
		KillTimer(4923);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY0);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY1);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY2);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY3);
	}
	else {
		RegisterHotKey(GetSafeHwnd(), ID_HOTKEY0, 0, VK_UP);
		RegisterHotKey(GetSafeHwnd(), ID_HOTKEY1, 0, VK_DOWN);
		RegisterHotKey(GetSafeHwnd(), ID_HOTKEY2, 0, VK_RIGHT);
		RegisterHotKey(GetSafeHwnd(), ID_HOTKEY3, 0, VK_LEFT);
		if (nState == WA_ACTIVE) {
		}
	}
	// TODO: ここにメッセージ ハンドラ コードを追加します。
}

void COggDlg::OnSysKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	CCustomBlurDialogBase::OnSysKeyDown(nChar, nRepCnt, nFlags);
}

void COggDlg::OnKillFocus(CWnd * pNewWnd)
{

	CCustomBlurDialogBase::OnKillFocus(pNewWnd);
	// 保留中の再登録タイマーが後から発火して再登録するのを防ぐ。
	KillTimer(4923);
	UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY0);
	UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY1);
	UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY2);
	UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY3);
	//	if (maini)
	//		::SetWindowPos(maini->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	//	::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	//	extern CImageBase* playbase;
	//	if (playbase)
	//		::SetWindowPos(playbase->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	//	::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void COggDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	// メディアプレイヤーモード中は(起動直後で mp 未生成でも)必ず裏へ隠す
	if (bShow && savedata.playerMode == 1 && GetSafeHwnd()) {
		ShowWindow(SW_HIDE);
		return;
	}
	if (bShow && pl && plw)
		pl->ScheduleRefreshNavControls();
	UNREFERENCED_PARAMETER(nStatus);
}

void COggDlg::PostRefreshAllAeroWindows()
{
	if (!GetSafeHwnd())
		return;
	PostMessage(WM_REFRESH_AERO_ALL, 0, 0);
}

LRESULT COggDlg::OnRefreshAeroAll(WPARAM, LPARAM)
{
	RefreshAllAeroWindows();
	return 0;
}

void COggDlg::RefreshAllAeroWindows()
{
#if CCUSTOM_AERO_SUPPORT
	auto refreshMode = [](CWnd* w) {
		if (!w || !w->GetSafeHwnd() || !::IsWindowVisible(w->m_hWnd))
			return;
		if (auto* pEx = dynamic_cast<CCustomBlurDialogExBase*>(w)) {
			pEx->RefreshAeroMode();
			return;
		}
		if (auto* pBase = dynamic_cast<CCustomBlurDialogBase*>(w)) {
			pBase->RefreshAeroMode();
		}
	};
	refreshMode(this);
	refreshMode(m_EqualizerDlg);
	refreshMode(m_PianoRollDlg);
	refreshMode(m_AnalyzerDlg);
	if (pl) refreshMode(pl);
	{
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()) && ::IsWindowVisible(mp->m_hWnd)) {
			refreshMode(mp);
			// アクリル<->通常の切替時に全コントロールを確実に再描画
			mp->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
		}
	}
	if (mi) refreshMode(mi);
	if (pMainFrame1 && pMainFrame1->GetSafeHwnd() && ::IsWindowVisible(pMainFrame1->m_hWnd)) {
		CCC_ApplyAero(pMainFrame1->m_hWnd, CCC_IsAeroEnabled() ? TRUE : FALSE);
		pMainFrame1->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
	}
#endif
}

BOOL COggDlg::OnEraseBkgnd(CDC* pDC)
{
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11())
		return TRUE;
#endif
	return CCustomBlurDialogBase::OnEraseBkgnd(pDC);
}

void COggDlg::OnSize(UINT nType, int cx, int cy)
{
	extern CImageBase* playbase;
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
#if CCUSTOM_AERO_SUPPORT
	// Finalize 再実行はしない。リサイズ時は DWM 属性の軽い再適用 + 再描画のみ。
	if (nType != SIZE_MINIMIZED && CCC_IsAeroEnabled())
	{
		CCC_RefreshDwmBlur(m_hWnd);
		Invalidate(FALSE);
	}
#endif
	if (nType == SIZE_MINIMIZED) {
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY0);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY1);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY2);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY3);
		//		if (playbase)
		//			playbase->ShowWindow(SW_MINIMIZE);
		if (pl) {
			if (pl->m_saisyo.GetCheck())
				pl->ShowWindow(SW_MINIMIZE);
		}
		if (pMainFrame1) {
			pMainFrame1->ShowWindow(SW_HIDE);
		}
		if (maini)
			maini->ShowWindow(SW_MINIMIZE);
		SetTimer(4924, 100, NULL);
	}
	if (nType == SIZE_RESTORED) {
		RegisterHotKey(GetSafeHwnd(), ID_HOTKEY0, 0, VK_UP);
		RegisterHotKey(GetSafeHwnd(), ID_HOTKEY1, 0, VK_DOWN);
		RegisterHotKey(GetSafeHwnd(), ID_HOTKEY2, 0, VK_RIGHT);
		RegisterHotKey(GetSafeHwnd(), ID_HOTKEY3, 0, VK_LEFT);
		if (ogpl0 == 1) {
			ogpl0 = 0;
			//			return;
		}
		//		if (playbase)
			//		playbase->ShowWindow(SW_RESTORE);
		if (pl && plw) {
			if (pl->m_saisyo.GetCheck())
				pl->ShowWindow(SW_SHOWNORMAL);
			pl->ScheduleRefreshNavControls();
		}
		if (pMainFrame1 && height != 0) {
			pMainFrame1->ShowWindow(SW_SHOWNORMAL);
		}
		CRect r;
		GetWindowRect(&r);
		if (maini)
			maini->ShowWindow(SW_RESTORE);
	}

}
#if _UNICODE
void COggDlg::_CreateShellLink(LPWSTR pszArguments, LPWSTR pszTitle, IShellLink * *ppsl, int iconindex, bool WA, BOOL wa2)
#else
void COggDlg::_CreateShellLink(LPSTR pszArguments, LPSTR pszTitle, IShellLink * *ppsl, int iconindex, bool WA, BOOL wa2)
#endif
{
	if (ppsl) *ppsl = NULL;
	IShellLink* psl;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
	if (SUCCEEDED(hr)) {
		if (WA) {
			TCHAR fname[MAX_PATH];
			TCHAR shortfname[MAX_PATH];
			fname[0] = 0;
			shortfname[0] = 0;
			GetModuleFileName(0, fname, MAX_PATH);
			if (!GetShortPathName(fname, shortfname, MAX_PATH) || shortfname[0] == 0)
				_tcscpy(shortfname, fname);
			CString s = pszArguments;
			if (s == "*4")
				psl->SetIconLocation(fname, 6);
			else
				psl->SetIconLocation(shortfname, 0);
			hr = psl->SetPath(shortfname);
		}
		else
			hr = psl->SetPath(_T("rundll32.exe"));

		if (SUCCEEDED(hr)) {
			hr = psl->SetArguments(pszArguments);
			if (SUCCEEDED(hr)) {
				IPropertyStore* pps;
				hr = psl->QueryInterface(IID_PPV_ARGS(&pps));
				if (SUCCEEDED(hr)) {
					PROPVARIANT propvar;
					WCHAR ss[2050];
					LPWSTR ss1; ss1 = ss;
#if UNICODE
					_tcscpy(ss1, pszTitle);
#else
					MultiByteToWideChar(CP_ACP, 0, pszTitle, -1, ss1, 2000);
#endif
					//propvar.vt=VT_LPWSTR;
					//propvar.pwszVal=ss1;
					if (SUCCEEDED(hr)) {
						if (wa2) {
							hr = InitPropVariantFromString(ss1, &propvar);
							hr = pps->SetValue(PKEY_Title, propvar);
						}
						else {
							InitPropVariantFromBoolean(TRUE, &propvar);
							hr = pps->SetValue(PKEY_AppUserModel_IsDestListSeparator, propvar);
						}
						if (SUCCEEDED(hr)) {
							hr = pps->Commit();
							if (SUCCEEDED(hr)) {
								hr = psl->QueryInterface(IID_PPV_ARGS(ppsl));
							}
						}
						PropVariantClear(&propvar);
					}
					pps->Release();
				}
			}
		}
		else {
			hr = HRESULT_FROM_WIN32(GetLastError());
		}
		psl->Release();
	}
	return;
}

HBRUSH COggDlg::OnCtlColor(CDC * pDC, CWnd * pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogBase::OnCtlColor(pDC, pWnd, nCtlColor);

	// TODO:  ここで DC の属性を変更してください。
	if (savedata.aero == 2) {
		if (nCtlColor == CTLCOLOR_DLG)
		{
			return m_brDlg;
		}
		if (nCtlColor == CTLCOLOR_STATIC)
		{
			SetBkMode(pDC->m_hDC, TRANSPARENT);
			return m_brDlg;
		}
	}
	// TODO:  既定値を使用したくない場合は別のブラシを返します。
	return hbr;
}


void COggDlg::OnPlayList()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	// メディアプレイヤーモードやDnD整合のため、閉じる時は破棄せず非表示にして
	// プレイリスト本体は裏で生かしておく(再表示時は ShowWindow)。
	if (pl && ::IsWindow(pl->GetSafeHwnd())) {
		if (plw) {
			pl->Save();
			::ShowWindow(pl->m_hWnd, SW_HIDE);
			plw = 0;
		}
		else {
			::ShowWindow(pl->m_hWnd, SW_SHOW);
			plw = 1;
		}
	}
	else {
		plw = 1;
		pl = new CPlayList;
		pl->Create(this);
		plw = 1;
	}
	if (pl && plw)
		savedata.pl = 1;
	else
		savedata.pl = 0;

	char tmp[1024];
	_getcwd(tmp, 1000);
	_tchdir(karento2);
	CFile ab;
#if _UNICODE
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#endif
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
	_chdir(tmp);
	}

void COggDlg::OnSwitchMode()
{
	// メディアプレイヤーモードへ切替
	EnterMediaPlayerMode();
}

LRESULT COggDlg::OnEnterFalcomMsg(WPARAM, LPARAM)
{
	// mp の操作ハンドラから抜けた後に安全に切替(mp を破棄)
	EnterFalcomMode();
	return 0;
}

LRESULT COggDlg::OnEnterMpModeMsg(WPARAM, LPARAM)
{
	// 親 DoModal/CreateDialogIndirectParam 完了後に MP を生成する
	EnterMediaPlayerMode();
	return 0;
}

// 再生スレッド(HandleNotifications)から曲開始時に投げられる。
// lParam は new された SongParam*。適用後にここで delete する。
LRESULT COggDlg::OnSongParamRestore(WPARAM, LPARAM lParam)
{
	SongParam* p = (SongParam*)lParam;
	if (p) {
		SongParams_ApplyEntryToMain(*p);
		SongParams_NoteRestored(*p);
		delete p;
	}
	return 0;
}

void COggDlg::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
	// メディアプレイヤーモード中はメイン画面を一切表示させない(初回起動時のちら出し対策)
	if (lpwndpos && savedata.playerMode == 1)
		lpwndpos->flags &= ~SWP_SHOWWINDOW;
	CCustomBlurDialogBase::OnWindowPosChanging(lpwndpos);
}

BOOL COggDlg::PreCreateWindow(CREATESTRUCT& cs)
{
	BOOL r = CCustomBlurDialogBase::PreCreateWindow(cs);
	// メディアプレイヤーモードで起動する場合、メイン画面は最初から非表示・画面外で生成し
	// 一瞬のちらつきを完全に防ぐ(切替時に EnterFalcomMode で表示する)。
	if (savedata.playerMode == 1) {
		cs.style &= ~WS_VISIBLE;
		cs.x = -32000;
		cs.y = -32000;
	}
	return r;
}

void plus1(int& c);
void plus2(int& c);
CString sswk;
HINSTANCE hDLLk1[500];
KMPMODULE* mod1[500];
pfnGetKMPModule pFunck[500];
IKpiUnknown* pU = NULL;

void plus1(int& c)
{
ab:
	try {
		_set_se_translator(trans_func);
		plus2(c);
		if (hDLLk1[kpicnt])FreeLibrary(hDLLk1[kpicnt]);
	}
	catch (SE_Exception e) {
		if (hDLLk1[kpicnt])FreeLibrary(hDLLk1[kpicnt]);
		c = 0;
		goto ab;
	}
	catch (_EXCEPTION_POINTERS* ep) {
		if (hDLLk1[kpicnt])FreeLibrary(hDLLk1[kpicnt]);
		c = 0;
		goto ab;
	}
	catch (...) {
		if (hDLLk1[kpicnt])FreeLibrary(hDLLk1[kpicnt]);
		c = 0;
		goto ab;
	}
}


void SplitString(const CString & input, const CString & delimiters, CStringArray & result)
{
	// 結果を格納する配列をクリア
	result.RemoveAll();

	int nStart = 0; // トークン検索の開始位置

	// input.Tokenize を呼び出す
	CString token = input.Tokenize(delimiters, nStart);

	// トークンが空 (IsEmpty) になるまでループ
	while (!token.IsEmpty())
	{
		result.Add(token); // 見つかったトークンを配列に追加
		token = input.Tokenize(delimiters, nStart); // 次のトークンを取得
	}
}

static WORD GetPeMachine(const CString& path)
{
	CFile f;
	if (!f.Open(path, CFile::modeRead | CFile::shareDenyWrite, NULL)) return 0;

	IMAGE_DOS_HEADER dos{};
	if (f.Read(&dos, sizeof(dos)) != sizeof(dos)) return 0;
	if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
	if (dos.e_lfanew <= 0) return 0;

	f.Seek(dos.e_lfanew, CFile::begin);
	DWORD peSig = 0;
	if (f.Read(&peSig, sizeof(peSig)) != sizeof(peSig)) return 0;
	if (peSig != IMAGE_NT_SIGNATURE) return 0;

	IMAGE_FILE_HEADER fileHdr{};
	if (f.Read(&fileHdr, sizeof(fileHdr)) != sizeof(fileHdr)) return 0;
	return fileHdr.Machine;
}

void plus2(int& c)
{
	CString ss = sswk;
	const WORD machine = GetPeMachine(ss);
	if (machine == IMAGE_FILE_MACHINE_AMD64 || machine == IMAGE_FILE_MACHINE_ARM64) {
		// x86プロセスではLoadLibraryできないため、一覧だけ作る（実処理は別プロセスに委譲する想定）
		kpiarch[kpicnt] = 64;
		kpif[kpicnt] = ss;
		std::wstring exts;
		uint32_t ver = 0;
		if (g_kpiHost.ListExts(ss.GetString(), ver, exts) && !exts.empty()) {
			CString cs(exts.c_str());
			CStringArray parts;
			SplitString(cs, L"/", parts);
			for (INT_PTR i = 0; i < parts.GetCount() && i < 299; ++i) {
				ext[kpicnt][i] = parts.GetAt(i);
				ext[kpicnt][i].MakeLower();
				kvar[kpicnt][i] = (BYTE)(ver ? ver : 5);
			}
			if (parts.GetCount() < 299) ext[kpicnt][(int)parts.GetCount()] = L"";
		}
		else {
			ext[kpicnt][0] = L"";
		}
		ext[kpicnt][299] = L"";
		kvar[kpicnt][0] = (BYTE)(ver ? ver : 5);
		kpicnt++;
		return;
	}

	hDLLk1[kpicnt] = LoadKpiLibraryWithDependencies((const wchar_t*)ss);
	if (hDLLk1[kpicnt]) {
		pFunck[kpicnt] = (pfnGetKMPModule)GetProcAddress(hDLLk1[kpicnt], SZ_KMP_GETMODULE);
		typedef HRESULT(WINAPI* kpi_CreateInstance)(REFIID riid, void** ppvObject, IKpiUnknown* pUnknown);
		kpi_CreateInstance cr = (kpi_CreateInstance)GetProcAddress(hDLLk1[kpicnt], "kpi_CreateInstance");
		if (pFunck[kpicnt] || cr) {
			if (cr) { // kpi 5
				kpiarch[kpicnt] = 32;
				IKpiDecoderModule* ob = NULL;
				IUnknown* pMyObject = new CMyHost((const wchar_t*)ss);
				HRESULT hr = cr(IID_IKpiDecoderModule, (void**)&ob, pMyObject);
				if (hr == S_OK) {
					const KPI_DECODER_MODULEINFO* m_ModuleInfo;
					IKpiDecoderModule* obb = (IKpiDecoderModule*)ob;
					obb->GetModuleInfo(&m_ModuleInfo);
					if (m_ModuleInfo != NULL && m_ModuleInfo->cszSupportExts != NULL) {

						CStringArray parts;
						SplitString(m_ModuleInfo->cszSupportExts, L"/", parts);
						for (INT_PTR i = 0; i < parts.GetCount() && i < 299; ++i)
						{
							ext[kpicnt][i] = parts.GetAt(i);
							ext[kpicnt][i].MakeLower();
							kvar[kpicnt][i] = 5;
						}
						if (parts.GetCount() < 299) {
							ext[kpicnt][(int)parts.GetCount()] = L"";
						}
						ext[kpicnt][299] = L"";

					}

					pMyObject->Release();
					obb->Release();
					kpif[kpicnt] = ss;
					kpicnt++;
				}
			}
			else { // kpi 2
				{
					kpiarch[kpicnt] = 32;
					mod1[kpicnt] = pFunck[kpicnt]();
					kpif[kpicnt] = ss;
					for (int i = 0; i < 299; i++) {
						if (mod1[kpicnt] == NULL) break;
						if (mod1[kpicnt]->ppszSupportExts) {
							if (mod1[kpicnt]->ppszSupportExts[i] == NULL ||
								mod1[kpicnt]->ppszSupportExts[i][0] == NULL) {
								ext[kpicnt][i] = L""; break;
							}
							ext[kpicnt][i] = mod1[kpicnt]->ppszSupportExts[i];
							ext[kpicnt][i].MakeLower();
							kvar[kpicnt][i] = 2;
						}
						else { ext[kpicnt][i] = L""; break; }
					}
					ext[kpicnt][299] = "";
					if (mod1[kpicnt]) {
						if (mod1[kpicnt]->Init)mod1[kpicnt]->Init();
						if (mod1[kpicnt]->Deinit)mod1[kpicnt]->Deinit();
					}
				}
				if (c && mod1[kpicnt])kpicnt++;
			}
		}
	}
}

void COggDlg::plug(CString ff, KMPMODULE * mod)
{
	kpicnt = 0;
	for (int i = 0; i < 500; i++)
		hDLLk1[i] = NULL;

	if (g_pActiveLoadingWnd != NULL) {
		int totalKpis = CountKpiFiles(ff);
		g_pActiveLoadingWnd->SetRange(0, totalKpis);
	}

	plugloop(ff);
	for (int i = kpicnt - 1; i >= 0; i--)
		FreeLibrary(hDLLk1[i]);
}
void COggDlg::plugloop(CString ff)
{
	CString s, ss;
	_tchdir(ff);
	CFileFind f;
	if (f.FindFile(_T("*.kpi"))) {
		int b, c = 1;
		do {
			if (c)
				b = f.FindNextFile();
			c = 1;
			if (f.IsDirectory() == 0) {
				s = f.GetFileName();
				if (!(s == "." || s == "..") && s.Right(4) == ".kpi") {
					ss = f.GetFilePath();
					sswk = ss;
					plus1(c);

					if (g_pActiveLoadingWnd != NULL) {
						g_nCurrentKpiIndex++;
						g_pActiveLoadingWnd->SetPos(g_nCurrentKpiIndex);
					}

					// Pump messages to keep loading window responsive!
					// ただし親 COggDlg の WM_INITDIALOG 中なので、WM_TIMER で
					// pl->Create / 他ダイアログをネスト CreateDialog すると
					// ERROR_INVALID_PARAMETER→「引数が正しくありません」になり得る。
					// 進捗表示は SetPos の Invalidate で足りるため TIMER は後回し。
					MSG msg;
					while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
						if (msg.message == WM_QUIT) {
							::PostQuitMessage((int)msg.wParam);
							break;
						}
						if (msg.message == WM_TIMER)
							continue;
						::TranslateMessage(&msg);
						::DispatchMessage(&msg);
					}
				}
			}
		} while (b);
	}
	f.Close();
	CFileFind cf1;
	if (cf1.FindFile(_T("*.*")) != 0) {
		int h = 1;
		for (; h;) {
			h = cf1.FindNextFile();
			ss = cf1.GetFileName();
			if (!(ss == "." || ss == "..")) {
				if (cf1.IsDirectory() != 0) { //フォルダ？
					if (ff.Right(1) == "\\")
						plugloop(ff + cf1.GetFileName());
					else
						plugloop(ff + _T("\\") + cf1.GetFileName());
				}
			}
		}
	}
	cf1.Close();
}
static bool FindId3ApicInBuffer(const BYTE* bufimage, int scanLen, ULONGLONG baseOffset, ULONGLONG& absImagePos, UINT& size)
{
	if (!bufimage || scanLen < 20)
		return false;
	for (int j = 0; j < scanLen - 10; j++) {
		if (bufimage[j] != 0x41 || bufimage[j + 1] != 0x50 || bufimage[j + 2] != 0x49 || bufimage[j + 3] != 0x43)
			continue;
		size = (UINT)bufimage[j + 4];
		size <<= 8;
		size |= (UINT)bufimage[j + 5];
		size <<= 8;
		size |= (UINT)bufimage[j + 6];
		size <<= 8;
		size |= (UINT)bufimage[j + 7];
		ULONGLONG enc = bufimage[j + 10];
		ULONGLONG rel = (ULONGLONG)j + (4 + 4 + 3 + 6);
		int flg = 0;
		for (; rel < (ULONGLONG)scanLen; rel++) {
			if (bufimage[rel] == 0)
				break;
		}
		rel += 2;
		if (rel < (ULONGLONG)scanLen && (bufimage[rel] == 0xff || bufimage[rel] == 0xfe)) {
			for (; rel < (ULONGLONG)scanLen; rel++) {
				if (enc == 1) {
					if (bufimage[rel] == 0 && bufimage[rel + 1] == 0) {
						if (rel + 2 < (ULONGLONG)scanLen && bufimage[rel + 1] == 0 && bufimage[rel + 2] == 0)
							flg = 1;
						break;
					}
				}
				else {
					if (bufimage[rel] == 0) {
						flg = 1;
						break;
					}
				}
			}
			if (rel >= (ULONGLONG)scanLen)
				return false;
			rel += (ULONGLONG)flg;
			if (enc == 1)
				rel += 2;
		}
		else {
			rel++;
		}
		absImagePos = baseOffset + rel;
		return (size > 0);
	}
	return false;
}

// RIFF/WAVE 内の id3 チャンクから APIC を探す(タグ付き WAV 用)
static bool TryWavRiffId3Apic(CFile& ff, BYTE* buf, int bufCap, ULONGLONG& absImagePos, UINT& size)
{
	const ULONGLONG fileLen = ff.GetLength();
	if (fileLen < 12 || !buf || bufCap < 20)
		return false;
	BYTE riff[12];
	ff.SeekToBegin();
	if (ff.Read(riff, 12) != 12)
		return false;
	if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0)
		return false;
	ULONGLONG pos = 12;
	while (pos + 8 <= fileLen) {
		ff.Seek((LONGLONG)pos, CFile::begin);
		DWORD chunkId = 0, chunkSize = 0;
		if (ff.Read(&chunkId, 4) != 4 || ff.Read(&chunkSize, 4) != 4)
			break;
		ULONGLONG dataOff = pos + 8;
		ULONGLONG nextPos = pos + 8ULL + (ULONGLONG)((chunkSize + 1) & ~1u);
		if (nextPos > fileLen)
			break;
		if (chunkId == 0x20336469) { // 'id3 '
			int readLen = (int)chunkSize;
			if (readLen > bufCap)
				readLen = bufCap;
			if (readLen < 20)
				break;
			ZeroMemory(buf, readLen + 1);
			ff.Seek((LONGLONG)dataOff, CFile::begin);
			if (ff.Read(buf, readLen) != (UINT)readLen)
				return false;
			return FindId3ApicInBuffer(buf, readLen, dataOff, absImagePos, size);
		}
		pos = nextPos;
	}
	return false;
}

CImageBase* jake = NULL;

// ID3 APIC をファイル先頭／末尾／DSD の po ヒント付近から探す。
static bool TryId3ApicRegions(CFile& ff, BYTE* bufimage, ULONGLONG hintOff,
	ULONGLONG& absImagePos, UINT& size)
{
	const int kScan = 512 * 1024;
	const ULONGLONG fLen = ff.GetLength();
	auto tryRegion = [&](ULONGLONG off) -> bool {
		if (off >= fLen)
			return false;
		int scanLen = (fLen - off > (ULONGLONG)kScan) ? kScan : (int)(fLen - off);
		if (scanLen < 20)
			return false;
		ZeroMemory(bufimage, scanLen + 1);
		ff.Seek(off, CFile::begin);
		if ((UINT)ff.Read(bufimage, scanLen) != (UINT)scanLen)
			return false;
		if (hintOff > 0 && off == hintOff) {
			if (bufimage[0] != 'I' || bufimage[1] != 'D' || bufimage[2] != '3')
				return false;
		}
		return FindId3ApicInBuffer(bufimage, scanLen, off, absImagePos, size);
	};
	if (hintOff > 0 && hintOff < fLen && tryRegion(hintOff))
		return true;
	if (tryRegion(0))
		return true;
	if (fLen > (ULONGLONG)kScan && tryRegion(fLen - (ULONGLONG)kScan))
		return true;
	return false;
}

void COggDlg::OnBnmp3jake()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	if (mi) {
		if (::IsWindow(mi->m_hWnd))
			mi->DestroyWindow();
		else
			delete mi;
		mi = NULL;
	}
	if (jake) {
		if (::IsWindow(jake->m_hWnd))
			jake->DestroyWindow();
		else
			delete jake;
		jake = NULL;
	}
	mi = new CMp3Image;
	mi->Create(og);
	if (savedata.aero == 2) {
		jake = new CImageBase;
		if (jake->Create(og) == FALSE) {
			AfxMessageBox(LL14(
				L"Baseの起動に失敗しました",               /* 日本語 */
				L"Failed to start Base.",                 /* 英語 */
				L"Échec du démarrage de Base.",           /* フランス語 */
				L"Avvio di Base fallito.",                /* イタリア語 */
				L"Error al iniciar Base.",                /* スペイン語 */
				L"Base 시작에 실패했습니다.",               /* 韓国語 */
				L"Base 启动失败。",                       /* 中国語 */
				L"فشل تشغيل Base.",                      /* アラビア語 */
				L"Не удалось запустить Base.",            /* ロシア語 */
				L"Base konnte nicht gestartet werden.",    /* ドイツ語 */
				L"Falha ao iniciar o Base.",              /* ポルトガル語 */
				L"Kan Base niet starten.",                /* オランダ語 */
				L"Nie udało się uruchomić Base.",         /* ポーランド語 */
				L"Base başlatılamadı."));                 /* トルコ語 */
		}
		jake->ShowWindow(SW_HIDE);
		jake->oya = mi;
	}
	else {
		jake = NULL;
	}
	mi->Load(mp3file);

}


void COggDlg::LoadJacket(CString s)
{
	if (!img.IsNull()) {
		img.Destroy();
	}
	jx = -1;

	if (s.IsEmpty()) {
		return;
	}

	const CString origPath = s;
	CString s1, s2;
	TCHAR env[256];
	GetEnvironmentVariable(_T("temp"), env, sizeof(env));
	s1 = env; s1 += "\\";
	s2 = s1;

	std::vector<BYTE> bufimage_vec(0x300010, 0);
	BYTE* bufimage = bufimage_vec.data();
	HGLOBAL hG = NULL;
	IStream* stream = NULL;

	CFile ff;
	if (ff.Open(s, CFile::modeRead | CFile::shareDenyWrite, NULL) == FALSE) {
		return;
	}
	UINT size = 0;
	ULONGLONG i = 0, enc = 0;
	s.MakeLower();
	if (s.Right(3) == "mp3") {
		ZeroMemory(bufimage, 2005);
		ff.Read(bufimage, 2005);
		if (bufimage[0x14] == 0 || bufimage[0x14] == 3) enc = 0; else enc = 1;
		for (i = 0; i < 2000; i++) {
			if (bufimage[i] == 0x41 && bufimage[i + 1] == 0x50 && bufimage[i + 2] == 0x49 && bufimage[i + 3] == 0x43) {
				break;
			}
		}
		if (i == 2000) {
			return;
		}
		size = (UINT)bufimage[i + 4];
		size <<= 8;
		size |= (UINT)bufimage[i + 5];
		size <<= 8;
		size |= (UINT)bufimage[i + 6];
		size <<= 8;
		size |= (UINT)bufimage[i + 7];
		enc = bufimage[i + 10];
		i += (4 + 4 + 3 + 6);
		int flg = 0;
		if (bufimage[i] == 'p') { s1 += _T("111.png"); }
		else { s1 += _T("111.jpg"); }
		s2 += _T("111.bmp");
		for (; i < 2000; i++) {
			if (bufimage[i] == 0)
				break;
		}
		i += 2;

		if ((bufimage[i] == 0xff || bufimage[i] == 0xfe)) {
			for (; i < 2000; i++) {
				if (enc == 1) {
					if (bufimage[i] == 0 && bufimage[i + 1] == 0) {
						if (bufimage[i + 1] == 0 && bufimage[i + 2] == 0)
							flg = 1;
						break;
					}
				}
				else {
					if (bufimage[i] == 0)
						flg = 1;
					break;
				}
			}
			if (i == 2000) {
				return;
			}
			i += flg;
			if (enc == 1)
				i += 2;
		}
		else i++;
	}
	else if (s.Right(3) == "m4a") {
		ZeroMemory(bufimage, 0x300000);
		ff.Read(bufimage, 0x300000);
		if (bufimage[0x14] == 0 || bufimage[0x14] == 3) enc = 0; else enc = 1;
		for (i = 0; i < 0x300000; i++) {// 00 06 5D 6A 64 61 74 61
			if (bufimage[i] == 0x63 && bufimage[i + 1] == 0x6f && bufimage[i + 2] == 0x76 && bufimage[i + 3] == 0x72 && bufimage[i + 8] == 0x64 && bufimage[i + 9] == 0x61 && bufimage[i + 10] == 0x74 && bufimage[i + 11] == 0x61) {
				break;
			}
		}
		if (i == 0x300000) {
			return;
		}
		i += 4;
		size = (UINT)bufimage[i];
		size <<= 8;
		size |= (UINT)bufimage[i + 1];
		size <<= 8;
		size |= (UINT)bufimage[i + 2];
		size <<= 8;
		size |= (UINT)bufimage[i + 3];
		size -= 16;

		i += 16;
		if (bufimage[i + 1] == 0x50 && bufimage[i + 2] == 0x4e && bufimage[i + 3] == 0x47) {
			s1 += _T("111.png");
		}
		else {
			s1 += _T("111.jpg");
		}
		s2 += _T("111.bmp");
	}
	else if (s.Right(3) == "ogg" || s.Right(6) == ".qull3") {
		CString cc;
		int vfiii = FALSE;
		for (int iii = 0; iii < vf.vc->comments; iii++) {
#if _UNICODE
			WCHAR* f; f = new WCHAR[0x300000];
			MultiByteToWideChar(CP_UTF8, 0, vf.vc->user_comments[iii], -1, f, 0x300000);
			cc = f;
			delete[] f;
#else
			cc = vf.vc->user_comments[iii];
#endif
			if (cc.Left(23) == "METADATA_BLOCK_PICTURE=") {
				vfiii = TRUE;
				char* buf = vf.vc->user_comments[iii];
				buf += 23;//Base64
				int len;
				char* decode = b64_decode(buf, (int)strlen(buf), len);
				if (decode[16 + 16 + 10] == 0x50 && decode[1 + 16 + 16 + 10] == 0x4e && decode[2 + 16 + 16 + 10] == 0x47) {
					s1 += _T("111.png");
				}
				else {
					s1 += _T("111.jpg");
					decode += 1;
				}
				s2 += _T("111.bmp");
				for (int j = 0; j < len; j++) {
					if (*(decode + len - j - 1) != 0) {
						len -= j;
						break;
					}
				}
				hG = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, len);
				memcpy(hG, decode + 16 + 16 + 9, len);
				CreateStreamOnHGlobal(hG, TRUE, &stream);
				free(decode);
				break;
			}
		}
		if (vfiii == FALSE) {
			return;
		}
	}
	else if (s.Right(4).MakeLower() == "flac" || s.Right(6).MakeLower() == "qull3h") {
		ZeroMemory(bufimage, 0x300000);
		ff.Read(bufimage, 0x300000);
		if (bufimage[0] == 0xBF) {
			BYTE offenc[7] = { 0xd9,0x3F,0x86,0x7B,0xC7,0x61,0xaa };
			int off = 0;
			for (int ll = 0; ll < 0x300000; ll++) {
				bufimage[ll] ^= offenc[off];
				off++; off %= 7;
			}
		}
		for (i = 0; i < 0x300000; i++) {// 00 06 5D 6A 64 61 74 61
			if (bufimage[i] == 'i' && bufimage[i + 1] == 'm' && bufimage[i + 2] == 'a' && bufimage[i + 3] == 'g' && bufimage[i + 4] == 'e' && bufimage[i + 5] == '/' && bufimage[i + 6] == 'j' && bufimage[i + 7] == 'p' && bufimage[i + 8] == 'e' && bufimage[i + 9] == 'g') {
				s1 += _T("111.jpg");
				i++;
				break;
			}
			if (bufimage[i] == 'i' && bufimage[i + 1] == 'm' && bufimage[i + 2] == 'a' && bufimage[i + 3] == 'g' && bufimage[i + 4] == 'e' && bufimage[i + 5] == '/' && bufimage[i + 6] == 'p' && bufimage[i + 7] == 'n' && bufimage[i + 8] == 'g') {
				s1 += _T("111.png");
				break;
			}
		}
		if (i == 0x300000) {
			return;
		}
		i += 29;
		size = (UINT)bufimage[i];
		size <<= 8;
		size |= (UINT)bufimage[i + 1];
		size <<= 8;
		size |= (UINT)bufimage[i + 2];
		size <<= 8;
		size |= (UINT)bufimage[i + 3];

		i += 4;
		s2 += _T("111.bmp");
	}
	else if (s.Right(3) == "wav") {
		const int kScan = 512 * 1024;
		const ULONGLONG fLen = ff.GetLength();
		bool ok = false;
		auto tryRegion = [&](ULONGLONG off) -> bool {
			if (off >= fLen)
				return false;
			int regionLen = (fLen - off > (ULONGLONG)kScan) ? kScan : (int)(fLen - off);
			if (regionLen < 20)
				return false;
			ZeroMemory(bufimage, regionLen + 1);
			ff.Seek(off, CFile::begin);
			if ((UINT)ff.Read(bufimage, regionLen) != (UINT)regionLen)
				return false;
			ULONGLONG imgPos = 0;
			if (!FindId3ApicInBuffer(bufimage, regionLen, off, imgPos, size))
				return false;
			i = imgPos;
			s2 += _T("111.bmp");
			return true;
		};
		ok = tryRegion(0);
		if (!ok && fLen > (ULONGLONG)kScan)
			ok = tryRegion(fLen - (ULONGLONG)kScan);
		if (!ok)
			ok = TryWavRiffId3Apic(ff, bufimage, (int)bufimage_vec.size() - 16, i, size);
		if (!ok) {
			ff.Close();
			static const TCHAR* kSidecarExts[] = { _T(".jpg"), _T(".jpeg"), _T(".png"), _T(".bmp") };
			int dot = origPath.ReverseFind(_T('.'));
			if (dot > 0) {
				CString base = origPath.Left(dot);
				for (int ei = 0; ei < 4; ei++) {
					CString sidecar = base + kSidecarExts[ei];
					if (::GetFileAttributes(sidecar) == INVALID_FILE_ATTRIBUTES)
						continue;
					if (img.Load(sidecar) != E_FAIL && !img.IsNull() && img.GetWidth() > 0) {
						jx = img.GetWidth();
						jy = img.GetHeight();
						jxy = (double)jx / (double)jy;
						if (jx > 0 && ::IsWindow(m_mp3jake.GetSafeHwnd()))
							m_mp3jake.EnableWindow(TRUE);
						return;
					}
					if (!img.IsNull())
						img.Destroy();
				}
			}
			return;
		}
		if (s2.IsEmpty())
			s2 += _T("111.bmp");
	}
	else if (s.Right(3) == "dsf" || s.Right(3) == "dff" || s.Right(3) == "wsd") {
		extern ULONGLONG po;
		ULONGLONG imgPos = 0;
		if (!TryId3ApicRegions(ff, bufimage, po, imgPos, size))
			return;
		i = imgPos;
		s2 += _T("111.bmp");
	}

	if (!(s.Right(3) == "ogg" || s.Right(6) == ".qull3")) {
		hG = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, size);
		if (hG == NULL) return;
		ff.SeekToBegin();
		ff.Seek(i, CFile::begin);
		char* cBit = new char[size];
		ff.Read(cBit, size);
		ff.Close();
		if (s.Right(6).MakeLower() == L"qull3h" && flacmode == 1) {
			BYTE offenc[7] = { 0xd9,0x3F,0x86,0x7B,0xC7,0x61,0xaa };
			int off = i % 7;
			for (int ll = 0; ll < (int)size; ll++) {
				cBit[ll] ^= offenc[off];
				off++; off %= 7;
			}
		}
		memcpy(hG, cBit, size);
		delete[] cBit;
		CreateStreamOnHGlobal(hG, TRUE, &stream);
	}

	if (stream != NULL) {
		if (img.Load(stream) != E_FAIL) {
			jx = img.GetWidth();
			jy = img.GetHeight();
			jxy = (double)jx / (double)jy;
			if (jx > 0 && ::IsWindow(m_mp3jake.GetSafeHwnd()))
				m_mp3jake.EnableWindow(TRUE);
		}
		stream->Release();
	}
}


void COggDlg::OnDestroy()
{
	MpPromptFlushHistoryOnExit();
	CCustomBlurDialogBase::OnDestroy();

	// TODO: ここにメッセージ ハンドラー コードを追加します。
	m_newFont->DeleteObject();
	delete m_newFont;
	m_newFont1->DeleteObject();
	delete m_newFont1;

}


BOOL COggDlg::Create(LPCTSTR lpszTemplateName, CWnd * pParentWnd)
{
	// TODO: ここに特定なコードを追加するか、もしくは基底クラスを呼び出してください。
	return CCustomBlurDialogBase::Create(lpszTemplateName, pParentWnd);
}


int COggDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomBlurDialogBase::OnCreate(lpCreateStruct) == -1)
		return -1;

	// TODO: ここに特定な作成コードを追加してください。
	if (savedata.aero == 2) {
		ModifyStyleEx(0, WS_EX_LAYERED);

		// レイヤードウィンドウの不透明度と透明のカラーキー
		SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);

		// 赤色のブラシを作成する．
		m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	}


	return 0;
}


void COggDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogBase::OnMoving(fwSide, pRect);
	CCC_MainLockOnMainMoving(pRect);
#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled())
		CCC_RefreshDwmBlur(m_hWnd);
#endif
	CRect r;
	GetWindowRect(&r);
	if (maini)
		maini->MoveWindow(&r);
	//if (maini)
//		::SetWindowPos(maini->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
//	::SetWindowPos(og->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


void COggDlg::OnSetFocus(CWnd * pOldWnd)
{
	CCustomBlurDialogBase::OnSetFocus(pOldWnd);

	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


int COggDlg::OnMouseActivate(CWnd * pDesktopWnd, UINT nHitTest, UINT message)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。

	return CCustomBlurDialogBase::OnMouseActivate(pDesktopWnd, nHitTest, message);
}

int npap = 0;
void COggDlg::OnActivateApp(BOOL bActive, DWORD dwThreadID)
{
	CCustomBlurDialogBase::OnActivateApp(bActive, dwThreadID);

	// アプリ非アクティブ時は必ずホットキーを解除する。
	// WM_ACTIVATEはフォーカスを失ったウィンドウにのみ送られるため、
	// mainiやpMainFrame1など他ウィンドウにフォーカスがある場合に
	// ホットキーが解除されないことがある。WM_ACTIVATEAPPは全トップレベル
	// ウィンドウに送られるため、アプリ全体の非アクティブを確実に検知できる。
	if (!bActive && ::IsWindow(GetSafeHwnd())) {
		// 保留中の再登録タイマーが後から発火して再登録するのを防ぐ。
		KillTimer(4923);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY0);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY1);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY2);
		UnregisterHotKey(GetSafeHwnd(), ID_HOTKEY3);
	}
}


BOOL COggDlg::OnNcActivate(BOOL bActive)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	if (plw) {
		if (bActive && pl->m_saisyo.GetCheck()) {
			//	pl->ShowWindow(SW_RESTORE);
		}
	}
	if (bActive) {
		SetTimer(4923, 20, NULL);
	}
	else {
		// 保留中の再登録タイマーが後から発火して再登録するのを防ぐ。
		KillTimer(4923);
		SetTimer(4924, 10, NULL);
	}
	return CCustomBlurDialogBase::OnNcActivate(bActive);
}

void COggDlg::OnTempoStatic()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_tempo_sl.SetPos(200);
	tempo = 200;
}

void COggDlg::OnPitchStatic()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_pitch_sl.SetPos(200);
	pitch = 200;
}

void COggDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	pox = point.x;
	poy = point.y;

	{
		CPoint point;
		::GetCursorPos(&point);

		// 各スタティックコントロールのスクリーン座標を取得
		CRect rectPitch, rectTemp;
		m_pitch_s.GetWindowRect(&rectPitch);
		m_temp_s.GetWindowRect(&rectTemp);

		// マウスカーソルがどちらかのコントロールの範囲内にあるかチェック
		if (rectPitch.PtInRect(point) || rectTemp.PtInRect(point))
		{
			// 範囲内なら、手の形のカーソルを設定
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
		}
		else {
			::SetCursor(::LoadCursor(NULL, IDC_ARROW));
		}
	}

	CCustomBlurDialogBase::OnMouseMove(nFlags, point);
}

void COggDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	CRect rectPitch, rectTemp;
	m_pitch_s.GetWindowRect(&rectPitch);
	m_temp_s.GetWindowRect(&rectTemp);

	// スクリーン座標をダイアログのクライアント座標に変換
	ScreenToClient(&rectPitch);
	ScreenToClient(&rectTemp);

	// マウスクリック位置がどちらかのコントロールの範囲内にあるかチェック
	if (rectPitch.PtInRect(point))
	{
		m_pitch_sl.SetPos(200);
		pitch = 200;
	}
	if (rectTemp.PtInRect(point))
	{
		m_tempo_sl.SetPos(200);
		tempo = 200;
	}
	CCustomBlurDialogBase::OnLButtonDown(nFlags, point);
}

void COggDlg::OnStnClickedStatic2()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
}

void COggDlg::OnStnDblclickStaticp()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_pitch_sl.SetPos(200);
}

void COggDlg::OnStnDblclickStatict()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_tempo_sl.SetPos(200);
}
void COggDlg::OnBnClickedButton59()
{
	if (!::IsWindow(m_EqualizerDlg->GetSafeHwnd()))
	{
		m_EqualizerDlg->Create(IDD_EQUALIZER, this);
		savedata.eqwindow = 1;
		m_EqualizerDlg->ShowWindow(SW_SHOW);
		m_EqualizerDlg->SetFocus();
	}
	else {
		m_EqualizerDlg->DestroyWindow();
		savedata.eqwindow = 0;
	}
}

void COggDlg::TogglePianoRoll()
{
	if (!::IsWindow(m_PianoRollDlg->GetSafeHwnd()))
	{
		m_PianoRollDlg->Create(IDD_PIANOROLL, this);
		savedata.pianorollwindow = 1;
	}
	else {
		m_PianoRollDlg->DetachForDestroy();
		m_PianoRollDlg->DestroyWindow();
		savedata.pianorollwindow = 0;
	}

	if (::IsWindow(m_PianoRollDlg->GetSafeHwnd())) {
		m_PianoRollDlg->ShowWindow(SW_SHOW);
		m_PianoRollDlg->SetFocus();
	}
}

void COggDlg::ToggleAnalyzer()
{
	if (!::IsWindow(m_AnalyzerDlg->GetSafeHwnd()))
	{
		if (!m_AnalyzerDlg->Create(IDD_ANALYZER, this)) {
			savedata.analyzerwindow = 0;
			return;
		}
		savedata.analyzerwindow = 1;
	}
	else {
		m_AnalyzerDlg->DetachForDestroy();
		m_AnalyzerDlg->DestroyWindow();
		savedata.analyzerwindow = 0;
	}

	if (::IsWindow(m_AnalyzerDlg->GetSafeHwnd())) {
		m_AnalyzerDlg->ShowWindow(SW_SHOW);
		m_AnalyzerDlg->SetFocus();
	}
}

void COggDlg::ShowPianoRollTune()
{
	if (!m_PianoRollTuneDlg)
		return;
	if (!::IsWindow(m_PianoRollTuneDlg->GetSafeHwnd())) {
		if (!m_PianoRollTuneDlg->Create(IDD_PIANOROLL_TUNE, this))
			return;
		savedata.prTunewindow = 1;
	}
	if (::IsWindow(m_PianoRollTuneDlg->GetSafeHwnd())) {
		m_PianoRollTuneDlg->ShowWindow(SW_SHOW);
		m_PianoRollTuneDlg->SetFocus();
		savedata.prTunewindow = 1;
	}
}

void COggDlg_ShowPianoRollTune()
{
	if (og)
		og->ShowPianoRollTune();
}

void COggDlg::FeedPianoRoll(const void* pData, int bytes)
{
	if (!pData || bytes <= 0 || playf == 0 || thn1 || stf != 0 || plf != 1)
		return;
	if (!m_dsb)
		return;
	const bool pianoOpen = ::IsWindow(m_PianoRollDlg->GetSafeHwnd()) != FALSE;
	if (!pianoOpen)
		return;

	int feed_rate = (g_pcm_upscale_active && g_ds_pcm_ch >= 1 && g_ds_pcm_bits >= 8) ? g_ds_pcm_rate : wavbit_sample_Hz;
	int feed_ch = (g_pcm_upscale_active && g_ds_pcm_ch >= 1 && g_ds_pcm_bits >= 8) ? g_ds_pcm_ch : wavchannel;
	int feed_bits = (g_pcm_upscale_active && g_ds_pcm_ch >= 1 && g_ds_pcm_bits >= 8) ? g_ds_pcm_bits : wavsam_depth;
	if (feed_rate <= 0) feed_rate = 44100;
	if (feed_ch <= 0) feed_ch = 2;
	if (feed_bits <= 0) feed_bits = 16;
	int bpf = PcmOutBytesPerFrame();
	if (bpf <= 0) return;

	int delaySamples = 0;
	LPDIRECTSOUNDBUFFER8 dsb = m_dsb;
	if (dsb) {
		ULONG playCursor = 0, writeCursor = 0;
		if (dsb->GetCurrentPosition(&playCursor, &writeCursor) == DS_OK) {
			const ULONG ringBytes = (g_ds_buffer_bytes > 0)
				? g_ds_buffer_bytes
				: (ULONG)(OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM);
			const ULONG aheadBytes = (writeCursor + ringBytes - playCursor) % ringBytes;
			delaySamples = (int)(aheadBytes / (ULONG)bpf);
		}
	}
	const int frames = bytes / bpf;
	m_PianoRollDlg->ResumePlaybackFeed();
	m_PianoRollDlg->FeedPCM(pData, frames, feed_rate, feed_bits, feed_ch, delaySamples);
}

void COggDlg::DeferredHeavyStartupImpl()
{
	try {
#include "oggDlg_DeferredHeavy.inc"
	}
	catch (CException* e)
	{
		TCHAR msg[512] = {};
		e->GetErrorMessage(msg, _countof(msg) - 1);
		CString line;
		line.Format(_T("[DeferredHeavy] swallowed %hs: %s\n"),
			e->GetRuntimeClass()->m_lpszClassName, msg);
		OutputDebugString(line);
		e->Delete();
	}
}

LRESULT COggDlg::OnDeferredHeavyStartup(WPARAM, LPARAM)
{
	if (!GetSafeHwnd()) return 0;
	StartUpdateCheckThread(m_hWnd);
	DeferredHeavyStartupImpl();
	return 0;
}
