// DeviceRecordDlg.cpp
// WASAPI ループバック録音 → WAV / mp3 / FLAC

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "DeviceRecordDlg.h"
#include "TranscodeExport.h"
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <FunctionDiscoveryKeys_devpkey.h>
#include <process.h>
#include <math.h>
#include <ShlObj.h>

#pragma comment(lib, "Ole32.lib")

extern void MpPersistSavedataQuick();

namespace {

static const GUID s_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT =
{ 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID s_KSDATAFORMAT_SUBTYPE_PCM =
{ 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

static float DrClamp1(float v)
{
	if (v > 1.f) return 1.f;
	if (v < -1.f) return -1.f;
	return v;
}

static void DrSampleToFloat(const BYTE* frame, const WAVEFORMATEX* fmt, float& L, float& R)
{
	L = R = 0.f;
	if (!frame || !fmt) return;
	const WORD tag = fmt->wFormatTag;
	const WORD bits = fmt->wBitsPerSample;
	const int ch = (int)fmt->nChannels;
	const BOOL isFloat = (tag == WAVE_FORMAT_IEEE_FLOAT)
		|| (tag == WAVE_FORMAT_EXTENSIBLE && bits == 32
			&& ((const WAVEFORMATEXTENSIBLE*)fmt)->SubFormat == s_KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
	const BOOL isPcmExt = (tag == WAVE_FORMAT_EXTENSIBLE
		&& ((const WAVEFORMATEXTENSIBLE*)fmt)->SubFormat == s_KSDATAFORMAT_SUBTYPE_PCM);
	if (isFloat) {
		const float* f = (const float*)frame;
		L = f[0];
		R = (ch >= 2) ? f[1] : L;
	} else if (bits == 16 || (isPcmExt && bits == 16) || (tag == WAVE_FORMAT_PCM && bits == 16)) {
		const short* s = (const short*)frame;
		L = (float)s[0] / 32768.f;
		R = (ch >= 2) ? ((float)s[1] / 32768.f) : L;
	} else if (bits == 24) {
		int v = (int)(frame[0] | (frame[1] << 8) | (frame[2] << 16));
		if (v & 0x800000) v |= ~0xFFFFFF;
		L = (float)v / 8388608.f;
		if (ch >= 2) {
			int v2 = (int)(frame[3] | (frame[4] << 8) | (frame[5] << 16));
			if (v2 & 0x800000) v2 |= ~0xFFFFFF;
			R = (float)v2 / 8388608.f;
		} else R = L;
	} else if (bits == 32) {
		const int* s = (const int*)frame;
		L = (float)s[0] / 2147483648.f;
		R = (ch >= 2) ? ((float)s[1] / 2147483648.f) : L;
	}
}

static void DrWriteWavHeader(CFile& f, WORD ch, DWORD hz, WORD bits)
{
	BYTE h[80];
	memset(h, 0, sizeof(h));
	const WORD blockAlign = (WORD)(ch * bits / 8);
	memcpy(h + 0, "RIFF", 4);
	*(DWORD*)(h + 4) = 0;
	memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "JUNK", 4);
	*(DWORD*)(h + 16) = 28;
	memcpy(h + 48, "fmt ", 4);
	*(DWORD*)(h + 52) = 16;
	*(WORD*)(h + 56) = WAVE_FORMAT_PCM;
	*(WORD*)(h + 58) = ch;
	*(DWORD*)(h + 60) = hz;
	*(DWORD*)(h + 64) = hz * blockAlign;
	*(WORD*)(h + 68) = blockAlign;
	*(WORD*)(h + 70) = bits;
	memcpy(h + 72, "data", 4);
	*(DWORD*)(h + 76) = 0;
	f.Write(h, 80);
}

static void DrFinalizeWavHeader(CFile& f, WORD ch, WORD bits)
{
	const __int64 fileLen = (__int64)f.GetLength();
	__int64 dataBytes = fileLen - 80;
	if (dataBytes < 0) dataBytes = 0;
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
	} else {
		BYTE hdr[48];
		memcpy(hdr + 0, "RF64", 4);
		*(DWORD*)(hdr + 4) = 0xFFFFFFFF;
		memcpy(hdr + 8, "WAVE", 4);
		memcpy(hdr + 12, "ds64", 4);
		*(DWORD*)(hdr + 16) = 28;
		*(__int64*)(hdr + 20) = fileLen - 8;
		*(__int64*)(hdr + 28) = dataBytes;
		*(__int64*)(hdr + 36) = dataBytes / blockAlign;
		*(DWORD*)(hdr + 44) = 0;
		f.Write(hdr, 48);
		f.Seek(76, CFile::begin);
		DWORD ds = 0xFFFFFFFF;
		f.Write(&ds, 4);
	}
}

static CString DrMakeTempWavPath()
{
	wchar_t dir[MAX_PATH] = {};
	GetTempPath(MAX_PATH, dir);
	static LONG s_seq = 0;
	const LONG n = InterlockedIncrement(&s_seq);
	CString path;
	path.Format(L"%sogg_dr_%u_%u_%ld.wav", dir, GetCurrentProcessId(), GetTickCount(), (long)n);
	return path;
}

enum { DR_MIC_FRAMES = 48000, DR_MIC_CH = 2 };
static float s_micRing[DR_MIC_FRAMES * DR_MIC_CH];
static volatile LONG s_micW = 0;
static volatile LONG s_micR = 0;
static CRITICAL_SECTION s_micCs;
static volatile LONG s_micCsInit = 0;
static volatile LONG s_micCapRate = 0;

static void DrMicEnsureCs()
{
	if (InterlockedCompareExchange(&s_micCsInit, 1, 0) == 0)
		InitializeCriticalSection(&s_micCs);
}

static void DrMicRingWrite(const float* interleaved, int frames)
{
	if (!interleaved || frames <= 0) return;
	EnterCriticalSection(&s_micCs);
	LONG w = s_micW;
	for (int i = 0; i < frames; ++i) {
		const int wi = (int)(w % DR_MIC_FRAMES);
		s_micRing[wi * DR_MIC_CH + 0] = interleaved[i * 2 + 0];
		s_micRing[wi * DR_MIC_CH + 1] = interleaved[i * 2 + 1];
		w++;
	}
	s_micW = w;
	LONG r = s_micR;
	if ((LONG)(w - r) > (DR_MIC_FRAMES - 64))
		s_micR = w - (DR_MIC_FRAMES / 2);
	LeaveCriticalSection(&s_micCs);
}

static void DrMicIntoStereo(float* L, float* R, int frames, int outRate)
{
	if (!L || !R || frames <= 0 || outRate < 8000) return;
	const int capRate = (int)InterlockedCompareExchange(&s_micCapRate, 0, 0);
	if (capRate < 8000) return;
	float gain = (float)savedata.mic_mix_level / 100.f;
	if (gain < 0.f) gain = 0.f;
	if (gain > 2.f) gain = 2.f;
	EnterCriticalSection(&s_micCs);
	LONG r = s_micR;
	const LONG w = s_micW;
	const double step = (double)capRate / (double)outRate;
	double pos = 0.0;
	for (int i = 0; i < frames; ++i) {
		LONG ri = r + (LONG)pos;
		float mL = 0.f, mR = 0.f;
		if (ri < w) {
			const int idx = (int)(ri % DR_MIC_FRAMES);
			mL = s_micRing[idx * DR_MIC_CH + 0];
			mR = s_micRing[idx * DR_MIC_CH + 1];
		}
		L[i] = DrClamp1(L[i] + mL * gain);
		R[i] = DrClamp1(R[i] + mR * gain);
		pos += step;
	}
	s_micR = r + (LONG)pos;
	LeaveCriticalSection(&s_micCs);
}

// IAudioClient は Initialize 失敗後に再利用できないので Activate し直す
static HRESULT DrActivateLoopClient(IMMDevice* renderDev, IAudioClient** outClient, WAVEFORMATEX** outFmt)
{
	if (!renderDev || !outClient || !outFmt) return E_POINTER;
	*outClient = NULL;
	*outFmt = NULL;
	IAudioClient* client = NULL;
	WAVEFORMATEX* fmt = NULL;
	HRESULT hr = renderDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&client);
	if (FAILED(hr) || !client) return FAILED(hr) ? hr : E_FAIL;
	hr = client->GetMixFormat(&fmt);
	if (FAILED(hr) || !fmt) {
		client->Release();
		return FAILED(hr) ? hr : E_FAIL;
	}
	*outClient = client;
	*outFmt = fmt;
	return S_OK;
}

static HRESULT DrInitLoopbackCapture(IMMDevice* renderDev,
	IAudioClient** outClient, IAudioCaptureClient** outCap, WAVEFORMATEX** outFmt, HANDLE* outEvent)
{
	*outClient = NULL;
	*outCap = NULL;
	*outFmt = NULL;
	*outEvent = NULL;

	// 1) ポーリング(ループバックで最も安定)
	IAudioClient* client = NULL;
	WAVEFORMATEX* fmt = NULL;
	HRESULT hr = DrActivateLoopClient(renderDev, &client, &fmt);
	if (FAILED(hr)) return hr;
	hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
		AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
		2000000, 0, fmt, NULL);
	if (FAILED(hr)) {
		client->Release();
		CoTaskMemFree(fmt);
		client = NULL; fmt = NULL;
		// 2) EVENTCALLBACK 付き
		hr = DrActivateLoopClient(renderDev, &client, &fmt);
		if (FAILED(hr)) return hr;
		HANDLE ev = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (!ev) {
			client->Release();
			CoTaskMemFree(fmt);
			return E_OUTOFMEMORY;
		}
		hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
			AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
			2000000, 0, fmt, NULL);
		if (FAILED(hr)) {
			CloseHandle(ev);
			client->Release();
			CoTaskMemFree(fmt);
			return hr;
		}
		hr = client->SetEventHandle(ev);
		if (FAILED(hr)) {
			CloseHandle(ev);
			client->Release();
			CoTaskMemFree(fmt);
			return hr;
		}
		IAudioCaptureClient* cap = NULL;
		hr = client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap);
		if (FAILED(hr) || !cap) {
			CloseHandle(ev);
			client->Release();
			CoTaskMemFree(fmt);
			return FAILED(hr) ? hr : E_FAIL;
		}
		*outClient = client;
		*outCap = cap;
		*outFmt = fmt;
		*outEvent = ev;
		return S_OK;
	}

	IAudioCaptureClient* cap = NULL;
	hr = client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap);
	if (FAILED(hr) || !cap) {
		client->Release();
		CoTaskMemFree(fmt);
		return FAILED(hr) ? hr : E_FAIL;
	}
	*outClient = client;
	*outCap = cap;
	*outFmt = fmt;
	*outEvent = NULL;
	return S_OK;
}

} // namespace

IMPLEMENT_DYNAMIC(CDeviceRecordDlg, CCustomBlurDialogBase)

static CDeviceRecordDlg* g_deviceRecordDlg = NULL;

CDeviceRecordDlg::CDeviceRecordDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CDeviceRecordDlg::IDD, pParent)
	, m_devCnt(0)
	, m_stop(0)
	, m_run(0)
	, m_pcmBytes(0)
	, m_lastHr(S_OK)
	, m_thread(NULL)
	, m_csInit(FALSE)
	, m_uiLocked(FALSE)
	, m_stopping(FALSE)
	, m_everStarted(FALSE)
	, m_peakOnly(FALSE)
	, m_outFmt(0)
	, m_mp3Kbps(192)
	, m_flacLevel(5)
	, m_doMixMic(FALSE)
	, m_startTick(0)
	, m_wavCh(2)
	, m_wavHz(48000)
	, m_wavBits(16)
	, m_peakMic(0)
	, m_peakSys(0)
	, m_peakMix(0)
{
	memset(m_devIds, 0, sizeof(m_devIds));
}

CDeviceRecordDlg::~CDeviceRecordDlg()
{
	// DestroyWindow 後は UI に触らない
	m_stopping = TRUE;
	InterlockedExchange(&m_stop, 1);
	if (m_thread) {
		WaitForSingleObject(m_thread, 5000);
		CloseHandle(m_thread);
		m_thread = NULL;
	}
	if (m_csInit) {
		EnterCriticalSection(&m_fileCs);
		if (m_wavFile.m_hFile != CFile::hFileNull)
			m_wavFile.Close();
		LeaveCriticalSection(&m_fileCs);
		DeleteCriticalSection(&m_fileCs);
		m_csInit = FALSE;
	}
}

void CDeviceRecordDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_DR_DEV_L, m_devLabel);
	DDX_Control(pDX, IDC_DR_DEV, m_dev);
	DDX_Control(pDX, IDC_DR_FMT_L, m_fmtLabel);
	DDX_Control(pDX, IDC_DR_FMT, m_fmt);
	DDX_Control(pDX, IDC_DR_QUAL_L, m_qualLabel);
	DDX_Control(pDX, IDC_DR_QUAL, m_qual);
	DDX_Control(pDX, IDC_DR_PATH_L, m_pathLabel);
	DDX_Control(pDX, IDC_DR_PATH, m_path);
	DDX_Control(pDX, IDC_DR_BROWSE, m_browse);
	DDX_Control(pDX, IDC_DR_MIXMIC, m_mixMic);
	DDX_Control(pDX, IDC_DR_METER_MIC_L, m_meterMicL);
	DDX_Control(pDX, IDC_DR_METER_SYS_L, m_meterSysL);
	DDX_Control(pDX, IDC_DR_METER_MIX_L, m_meterMixL);
	DDX_Control(pDX, IDC_DR_METER_MIC, m_meterMic);
	DDX_Control(pDX, IDC_DR_METER_SYS, m_meterSys);
	DDX_Control(pDX, IDC_DR_METER_MIX, m_meterMix);
	DDX_Control(pDX, IDC_DR_START, m_start);
	DDX_Control(pDX, IDC_DR_CLOSE, m_close);
	DDX_Control(pDX, IDC_DR_STATUS, m_status);
	DDX_Control(pDX, IDC_DR_TIME, m_time);
}

BEGIN_MESSAGE_MAP(CDeviceRecordDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_DR_BROWSE, &CDeviceRecordDlg::OnBnClickedBrowse)
	ON_BN_CLICKED(IDC_DR_START, &CDeviceRecordDlg::OnBnClickedStart)
	ON_BN_CLICKED(IDC_DR_CLOSE, &CDeviceRecordDlg::OnBnClickedClose)
	ON_CBN_SELCHANGE(IDC_DR_FMT, &CDeviceRecordDlg::OnCbnSelchangeFormat)
	ON_WM_TIMER()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

void CDeviceRecordDlg::RefreshOpaqueUi()
{
	if (!GetSafeHwnd()) return;
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	if (m_path.GetSafeHwnd())
		m_path.RepaintClient();
	if (m_qual.GetSafeHwnd())
		m_qual.Invalidate(FALSE);
	if (m_dev.GetSafeHwnd())
		m_dev.Invalidate(FALSE);
	if (m_fmt.GetSafeHwnd())
		m_fmt.Invalidate(FALSE);
}

CString CDeviceRecordDlg::ExtForFormat(int fmt) const
{
	if (fmt == 1) return L".mp3";
	if (fmt == 2) return L".flac";
	return L".wav";
}

CString CDeviceRecordDlg::FilterForFormat(int fmt) const
{
	if (fmt == 1)
		return LL14(L"MP3 (*.mp3)|*.mp3|すべてのファイル (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|All Files (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|Tous les fichiers (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|Tutti i file (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|Todos los archivos (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|모든 파일 (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|所有文件 (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|All Files (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|Все файлы (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|Alle Dateien (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|Todos os ficheiros (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|Alle bestanden (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|Wszystkie pliki (*.*)|*.*||",
			L"MP3 (*.mp3)|*.mp3|Tum dosyalar (*.*)|*.*||");
	if (fmt == 2)
		return LL14(L"FLAC (*.flac)|*.flac|すべてのファイル (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|All Files (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|Tous les fichiers (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|Tutti i file (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|Todos los archivos (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|모든 파일 (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|所有文件 (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|All Files (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|Все файлы (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|Alle Dateien (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|Todos os ficheiros (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|Alle bestanden (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|Wszystkie pliki (*.*)|*.*||",
			L"FLAC (*.flac)|*.flac|Tum dosyalar (*.*)|*.*||");
	return LL14(L"WAV (*.wav)|*.wav|すべてのファイル (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|All Files (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|Tous les fichiers (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|Tutti i file (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|Todos los archivos (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|모든 파일 (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|所有文件 (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|All Files (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|Все файлы (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|Alle Dateien (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|Todos os ficheiros (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|Alle bestanden (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|Wszystkie pliki (*.*)|*.*||",
		L"WAV (*.wav)|*.wav|Tum dosyalar (*.*)|*.*||");
}

CString CDeviceRecordDlg::NormalizeOutPath(const CString& pathIn, int fmt) const
{
	CString p = pathIn;
	p.Trim();
	if (p.IsEmpty()) return p;
	const CString ext = ExtForFormat(fmt);
	const int dot = p.ReverseFind(L'.');
	const int slash = (p.ReverseFind(L'\\') > p.ReverseFind(L'/')) ? p.ReverseFind(L'\\') : p.ReverseFind(L'/');
	if (dot > slash)
		p = p.Left(dot) + ext;
	else
		p += ext;
	return p;
}

void CDeviceRecordDlg::FillDeviceCombo()
{
	m_devCnt = 0;
	m_dev.ResetContent();
	m_dev.AddString(LL14(
		L"(既定の再生デバイス)", L"(Default playback device)", L"(Périphérique de lecture par défaut)",
		L"(Dispositivo di riproduzione predefinito)", L"(Dispositivo de reproducción predeterminado)",
		L"(기본 재생 장치)", L"(默认播放设备)", L"(جهاز التشغيل الافتراضي)",
		L"(Устройство воспроизведения по умолчанию)", L"(Standardwiedergabegerät)",
		L"(Dispositivo de reprodução padrão)", L"(Standaard afspeelapparaat)",
		L"(Domyślne urządzenie odtwarzania)", L"(Varsayılan oynatma aygıtı)"));
	m_devIds[0][0] = 0;
	m_devCnt = 1;

	IMMDeviceEnumerator* enumer = NULL;
	IMMDeviceCollection* coll = NULL;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumer);
	if (SUCCEEDED(hr) && enumer) {
		hr = enumer->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll);
		if (SUCCEEDED(hr) && coll) {
			UINT cnt = 0;
			coll->GetCount(&cnt);
			for (UINT i = 0; i < cnt && m_devCnt < DR_DEV_MAX; ++i) {
				IMMDevice* device = NULL;
				if (FAILED(coll->Item(i, &device)) || !device) continue;
				LPWSTR id = NULL;
				if (FAILED(device->GetId(&id)) || !id) { device->Release(); continue; }
				IPropertyStore* props = NULL;
				CString name = id;
				if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props)) && props) {
					PROPVARIANT var;
					PropVariantInit(&var);
					if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &var)) && var.vt == VT_LPWSTR && var.pwszVal)
						name = var.pwszVal;
					PropVariantClear(&var);
					props->Release();
				}
				_tcsncpy(m_devIds[m_devCnt], id, _countof(m_devIds[0]) - 1);
				m_devIds[m_devCnt][_countof(m_devIds[0]) - 1] = 0;
				m_dev.AddString(name);
				CoTaskMemFree(id);
				device->Release();
				m_devCnt++;
			}
			coll->Release();
		}
		enumer->Release();
	}

	int sel = 0;
	if (savedata.loop_device[0]) {
		for (int i = 1; i < m_devCnt; ++i) {
			if (_tcscmp(m_devIds[i], savedata.loop_device) == 0) { sel = i; break; }
		}
	} else if (savedata.loop_device_cur > 0 && savedata.loop_device_cur < m_devCnt) {
		sel = savedata.loop_device_cur;
	}
	m_dev.SetCurSelPhysical(sel);
}

void CDeviceRecordDlg::RefreshQualityCombo()
{
	const int fmt = m_fmt.GetCurSel();
	m_qual.ResetContent();
	if (fmt == 1) {
		m_qualLabel.SetWindowText(LL14(L"ビットレート", L"Bitrate", L"Debit", L"Bitrate",
			L"Tasa de bits", L"비트레이트", L"比特率", L"معدل البت",
			L"Битрейт", L"Bitrate", L"Taxa de bits", L"Bitrate",
			L"Bitrate", L"Bit hizi"));
		static const int kbps[] = { 128, 160, 192, 224, 256, 320 };
		int sel = 2;
		for (int i = 0; i < 6; ++i) {
			CString s; s.Format(L"%d kbps", kbps[i]);
			m_qual.AddString(s);
			if (savedata.record_mp3_kbps == kbps[i]) sel = i;
		}
		m_qual.SetCurSel(sel);
	} else if (fmt == 2) {
		m_qualLabel.SetWindowText(LL14(L"圧縮レベル", L"Compression", L"Compression", L"Compressione",
			L"Compresion", L"압축 레벨", L"压缩等级", L"مستوى الضغط",
			L"Сжатие", L"Kompression", L"Compressao", L"Compressie",
			L"Kompresja", L"Sikistirma"));
		for (int i = 0; i <= 8; ++i) {
			CString s; s.Format(L"%d", i);
			m_qual.AddString(s);
		}
		int lv = savedata.record_flac_level;
		if (lv < 0 || lv > 8) lv = 5;
		m_qual.SetCurSel(lv);
	} else {
		m_qualLabel.SetWindowText(LL14(L"品質", L"Quality", L"Qualité", L"Qualità",
			L"Calidad", L"품질", L"质量", L"الجودة",
			L"Качество", L"Qualität", L"Qualidade", L"Kwaliteit",
			L"Jakosc", L"Kalite"));
		m_qual.AddString(L"PCM 16-bit");
		m_qual.SetCurSel(0);
	}
	RefreshOpaqueUi();
}

void CDeviceRecordDlg::PersistUiToSavedata()
{
	if (!GetSafeHwnd()) return;
	int sel = m_dev.GetCurSelPhysical();
	if (sel < 0) sel = 0;
	if (sel >= m_devCnt) sel = 0;
	savedata.loop_device_cur = sel;
	_tcsncpy(savedata.loop_device, m_devIds[sel], _countof(savedata.loop_device) - 1);
	savedata.loop_device[_countof(savedata.loop_device) - 1] = 0;

	int fmt = m_fmt.GetCurSel();
	if (fmt < 0 || fmt > 2) fmt = 0;
	savedata.record_format = fmt;

	if (fmt == 1) {
		static const int kbps[] = { 128, 160, 192, 224, 256, 320 };
		int q = m_qual.GetCurSel();
		if (q < 0 || q > 5) q = 2;
		savedata.record_mp3_kbps = kbps[q];
	} else if (fmt == 2) {
		int q = m_qual.GetCurSel();
		if (q < 0 || q > 8) q = 5;
		savedata.record_flac_level = q;
	}
	savedata.record_mix_mic = m_mixMic.GetCheck() ? 1 : 0;

	CString path;
	m_path.GetWindowText(path);
	path = NormalizeOutPath(path, fmt);
	_tcsncpy(savedata.record_last_path, path, _countof(savedata.record_last_path) - 1);
	savedata.record_last_path[_countof(savedata.record_last_path) - 1] = 0;
	MpPersistSavedataQuick();
}

BOOL CDeviceRecordDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	if (!m_csInit) {
		InitializeCriticalSection(&m_fileCs);
		m_csInit = TRUE;
	}

	m_dev.SetAeroMode(FALSE);
	m_fmt.SetAeroMode(FALSE);
	m_qual.SetAeroMode(FALSE);
	m_mixMic.SetAeroMode(FALSE);

	SetWindowText(LL14(
		L"デバイス録音", L"Device recording", L"Enregistrement périphérique", L"Registrazione dispositivo",
		L"Grabación de dispositivo", L"장치 녹음", L"设备录音", L"تسجيل الجهاز",
		L"Запись с устройства", L"Geräteaufnahme", L"Gravação de dispositivo", L"Apparaatopname",
		L"Nagrywanie urządzenia", L"Cihaz kaydı"));

	m_devLabel.SetWindowText(LL14(
		L"再生端末", L"Playback device", L"Périphérique lecture", L"Dispositivo riproduzione",
		L"Dispositivo reproducción", L"재생 장치", L"播放设备", L"جهاز التشغيل",
		L"Устройство воспр.", L"Wiedergabegerät", L"Dispositivo reprodução", L"Afspeelapparaat",
		L"Urządzenie odtwarzania", L"Oynatma aygıtı"));
	m_fmtLabel.SetWindowText(LL14(
		L"形式", L"Format", L"Format", L"Formato",
		L"Formato", L"형식", L"格式", L"التنسيق",
		L"Формат", L"Format", L"Formato", L"Formaat",
		L"Format", L"Biçim"));
	m_pathLabel.SetWindowText(LL14(
		L"保存先", L"Save path", L"Chemin", L"Percorso",
		L"Ruta", L"저장 위치", L"保存路径", L"المسار",
		L"Путь", L"Pfad", L"Caminho", L"Pad",
		L"Ścieżka", L"Yol"));
	m_mixMic.SetWindowText(LL14(
		L"マイクもミックス", L"Also mix microphone", L"Mixer aussi le micro", L"Mixa anche il microfono",
		L"Mezclar también el micrófono", L"마이크도 믹스", L"同时混入麦克风", L"مزج الميكروفون أيضاً",
		L"Также микшировать микрофон", L"Mikrofon mitmischen", L"Misturar também o microfone", L"Microfoon ook mixen",
		L"Miksuj także mikrofon", L"Mikrofonu da karıştır"));
	m_start.SetWindowText(LL14(
		L"録音開始", L"Start recording", L"Démarrer", L"Avvia",
		L"Iniciar", L"녹음 시작", L"开始录音", L"بدء التسجيل",
		L"Начать запись", L"Aufnahme starten", L"Iniciar gravação", L"Opname starten",
		L"Rozpocznij nagrywanie", L"Kaydı başlat"));
	m_close.SetWindowText(LL14(
		L"閉じる", L"Close", L"Fermer", L"Chiudi",
		L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten",
		L"Zamknij", L"Kapat"));
	m_status.SetWindowText(LL14(
		L"再生端末で音を出しながら録音してください（無音だとデータが来ないことがあります）。",
		L"Play audio on the selected device while recording (silence may yield no data).",
		L"Faites jouer de l'audio sur le périphérique pendant l'enregistrement.",
		L"Riproduci audio sul dispositivo durante la registrazione.",
		L"Reproduzca audio en el dispositivo mientras graba.",
		L"선택한 장치에서 소리를 재생하면서 녹음하세요(무음이면 데이터가 없을 수 있음).",
		L"请在所选设备上播放声音再录音（静音可能没有数据）。",
		L"شغّل الصوت على الجهاز أثناء التسجيل.",
		L"Воспроизводите звук на устройстве во время записи.",
		L"Spielen Sie während der Aufnahme Audio auf dem Gerät ab.",
		L"Reproduza áudio no dispositivo durante a gravação.",
		L"Speel audio af op het apparaat tijdens opname.",
		L"Odtwarzaj dźwięk na urządzeniu podczas nagrywania.",
		L"Kayıt sırasında aygıtta ses çalın."));

	FillDeviceCombo();
	m_fmt.AddString(L"WAV");
	m_fmt.AddString(L"mp3");
	m_fmt.AddString(L"FLAC");
	int fmt = savedata.record_format;
	if (fmt < 0 || fmt > 2) fmt = 0;
	m_fmt.SetCurSel(fmt);
	RefreshQualityCombo();
	m_mixMic.SetCheck(savedata.record_mix_mic ? BST_CHECKED : BST_UNCHECKED);

	CString path = savedata.record_last_path;
	if (path.IsEmpty()) {
		wchar_t music[MAX_PATH] = {};
		if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_MYMUSIC, NULL, SHGFP_TYPE_CURRENT, music)))
			path.Format(L"%s\\record%s", music, (LPCTSTR)ExtForFormat(fmt));
		else
			path.Format(L"record%s", (LPCTSTR)ExtForFormat(fmt));
	} else {
		path = NormalizeOutPath(path, fmt);
	}
	m_path.SetWindowText(path);
	m_time.SetWindowText(L"00:00");

	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_dev, LL14(
			L"録音する再生(出力)端末。この端末で鳴っている音をキャプチャします。",
			L"Playback device to record. Captures audio playing on this device.",
			L"Périphérique de lecture à enregistrer.",
			L"Dispositivo di riproduzione da registrare.",
			L"Dispositivo de reproducción a grabar.",
			L"녹음할 재생 장치. 이 장치에서 재생 중인 소리를 캡처합니다.",
			L"要录制的播放设备。捕获该设备上正在播放的声音。",
			L"جهاز التشغيل المراد تسجيله.",
			L"Устройство воспроизведения для записи.",
			L"Aufzunehmendes Wiedergabegerät.",
			L"Dispositivo de reprodução a gravar.",
			L"Op te nemen afspeelapparaat.",
			L"Urządzenie odtwarzania do nagrania.",
			L"Kaydedilecek oynatma aygıtı."));
		m_tooltip.AddTool(&m_mixMic, LL14(
			L"CRender で選んだマイクも同時にミックスします（マイク音量スライダーを使用）。",
			L"Also mixes the mic selected in CRender (uses mic level slider).",
			L"Mixe aussi le micro choisi dans CRender.",
			L"Mixa anche il micro selezionato in CRender.",
			L"También mezcla el micrófono elegido en CRender.",
			L"CRender에서 선택한 마이크도 함께 믹스합니다.",
			L"同时混入 CRender 所选麦克风（使用麦克风音量）。",
			L"يمزج أيضاً الميكروفون المحدد في CRender.",
			L"Также микширует микрофон из CRender.",
			L"Mischt auch das in CRender gewählte Mikrofon.",
			L"Também mistura o microfone escolhido em CRender.",
			L"Mixt ook de in CRender gekozen microfoon.",
			L"Miksuje też mikrofon z CRender.",
			L"CRender’de seçilen mikrofonu da karıştırır."));
		m_tooltip.AddTool(&m_meterMic, LL14(L"マイク入力レベル(リアルタイム)", L"Mic level (live)", L"Niveau micro (live)", L"Livello microfono (live)", L"Nivel de micrófono (en vivo)", L"마이크 레벨(실시간)", L"麦克风电平(实时)", L"مستوى الميكروفون (مباشر)", L"Уровень микрофона (live)", L"Mikrofonpegel (live)", L"Nível do microfone (ao vivo)", L"Microfoonniveau (live)", L"Poziom mikrofonu (na żywo)", L"Mikrofon seviyesi (canlı)"));
		m_tooltip.AddTool(&m_meterSys, LL14(L"システム(ループバック)レベル = 演奏中を含む(リアルタイム)", L"System (loopback) level including playback (live)", L"Niveau système (lecture incluse, live)", L"Livello sistema (include riproduzione, live)", L"Nivel del sistema (incluye reproducción, en vivo)", L"시스템 레벨(재생 포함, 실시간)", L"系统电平（含播放，实时）", L"مستوى النظام (يشمل التشغيل، مباشر)", L"Уровень системы (с воспроизведением, live)", L"Systempegel inkl. Wiedergabe (live)", L"Nível do sistema (inclui reprodução, ao vivo)", L"Systeemniveau (inclusief afspelen, live)", L"Poziom systemu (z odtwarzaniem, na żywo)", L"Sistem seviyesi (oynatma dahil, canlı)"));
		m_tooltip.AddTool(&m_meterMix, LL14(L"マイク+システム ミックス後(リアルタイム)", L"After mic+system mix (live)", L"Après mix micro+système (live)", L"Dopo mix micro+sistema (live)", L"Tras mezcla micro+sistema (en vivo)", L"마이크+시스템 믹스 후(실시간)", L"麦克风+系统混合后(实时)", L"بعد مزج الميكروفون والنظام (مباشر)", L"После микса микрофон+система (live)", L"Nach Mikrofon+System-Mix (live)", L"Após mix micro+sistema (ao vivo)", L"Na mic+systeem-mix (live)", L"Po miksie mikrofon+system (na żywo)", L"Mikrofon+sistem karışımı sonrası (canlı)"));
		m_tooltip.AddTool(&m_fmt, LL14(L"保存フォーマット (WAV/MP3/FLAC)", L"Save format (WAV/MP3/FLAC)", L"Format de sortie", L"Formato di salvataggio", L"Formato de guardado", L"저장 형식", L"保存格式", L"صيغة الحفظ", L"Формат сохранения", L"Speicherformat", L"Formato de gravação", L"Opslagformaat", L"Format zapisu", L"Kayıt biçimi"));
		m_tooltip.AddTool(&m_qual, LL14(L"ビットレート/品質", L"Bitrate / quality", L"Débit / qualité", L"Bitrate / qualità", L"Bitrate / calidad", L"비트레이트/품질", L"比特率/质量", L"معدل البت/الجودة", L"Битрейт / качество", L"Bitrate / Qualität", L"Bitrate / qualidade", L"Bitrate / kwaliteit", L"Bitrate / jakość", L"Bit hızı / kalite"));
		m_tooltip.AddTool(&m_path, LL14(L"録音ファイルの保存先", L"Recording save path", L"Chemin d'enregistrement", L"Percorso registrazione", L"Ruta de grabación", L"녹음 저장 경로", L"录音保存路径", L"مسار حفظ التسجيل", L"Путь записи", L"Aufnahmepfad", L"Caminho da gravação", L"Opnamepad", L"Ścieżka nagrania", L"Kayıt yolu"));
		m_tooltip.AddTool(&m_browse, LL14(L"保存先を参照", L"Browse save location", L"Parcourir", L"Sfoglia", L"Examinar", L"찾아보기", L"浏览", L"استعراض", L"Обзор", L"Durchsuchen", L"Procurar", L"Bladeren", L"Przeglądaj", L"Göz at"));
		m_tooltip.AddTool(&m_start, LL14(L"録音開始/停止。レベルメーターは常時リアルタイム表示", L"Start/stop recording. Level meters stay live", L"Démarrer/arrêter. Compteurs toujours live", L"Avvia/ferma. Livelli sempre live", L"Iniciar/detener. Medidores siempre en vivo", L"녹음 시작/중지. 레벨 미터는 상시 실시간", L"开始/停止录音。电平表始终实时", L"بدء/إيقاف. العدادات مباشرة دائماً", L"Старт/стоп. Индикаторы всегда live", L"Start/Stop. Pegel bleiben live", L"Iniciar/parar. Medidores sempre ao vivo", L"Start/stop. Meters blijven live", L"Start/stop. Mierniki zawsze na żywo", L"Başlat/durdur. Seviye göstergeleri canlı kalır"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 10000);
	}
	m_meterMicL.SetWindowText(LL14(L"Mic", L"Mic", L"Mic", L"Mic", L"Mic", L"Mic", L"Mic", L"Mic", L"Mic", L"Mic", L"Mic", L"Mic", L"Mic", L"Mic"));
	m_meterSysL.SetWindowText(LL14(L"Sys", L"Sys", L"Sys", L"Sys", L"Sys", L"Sys", L"Sys", L"Sys", L"Sys", L"Sys", L"Sys", L"Sys", L"Sys", L"Sys"));
	m_meterMixL.SetWindowText(LL14(L"Mix", L"Mix", L"Mix", L"Mix", L"Mix", L"Mix", L"Mix", L"Mix", L"Mix", L"Mix", L"Mix", L"Mix", L"Mix", L"Mix"));
	RefreshOpaqueUi();
	StartPeakMonitor();
	return TRUE;
}

static int DrMeterUiLevel(LONG peak)
{
	if (peak <= 0) return 0;
	if (peak > 1000) peak = 1000;
	const double n = (double)peak / 1000.0;
	int ui = (int)(sqrt(n) * 1000.0 * 1.15);
	if (ui < 1) ui = 1;
	if (ui > 1000) ui = 1000;
	return ui;
}

void CDeviceRecordDlg::PaintMetersFromPeaks()
{
	LONG mic = InterlockedCompareExchange(&m_peakMic, 0, 0);
	LONG sys = InterlockedCompareExchange(&m_peakSys, 0, 0);
	LONG mix = InterlockedCompareExchange(&m_peakMix, 0, 0);
	InterlockedExchange(&m_peakMic, mic * 88 / 100);
	InterlockedExchange(&m_peakSys, sys * 88 / 100);
	InterlockedExchange(&m_peakMix, mix * 88 / 100);
	if (m_meterMic.GetSafeHwnd()) m_meterMic.SetLevel(DrMeterUiLevel(mic));
	if (m_meterSys.GetSafeHwnd()) m_meterSys.SetLevel(DrMeterUiLevel(sys));
	if (m_meterMix.GetSafeHwnd()) m_meterMix.SetLevel(DrMeterUiLevel(mix));
}

void CDeviceRecordDlg::StartPeakMonitor()
{
	if (m_thread || InterlockedCompareExchange(&m_run, 0, 0) != 0) return;
	m_peakOnly = TRUE;
	m_doMixMic = (m_mixMic.GetSafeHwnd() && m_mixMic.GetCheck() == BST_CHECKED);
	InterlockedExchange(&m_stop, 0);
	InterlockedExchange(&m_run, 0);
	InterlockedExchange(&m_pcmBytes, 0);
	InterlockedExchange(&m_lastHr, S_OK);
	InterlockedExchange(&s_micW, 0);
	InterlockedExchange(&s_micR, 0);
	uintptr_t th = _beginthreadex(NULL, 0, CaptureThread, this, 0, NULL);
	if (!th) return;
	m_thread = (HANDLE)th;
	SetTimer(DR_TIMER, 50, NULL);
}

void CDeviceRecordDlg::StopPeakMonitor()
{
	if (!m_peakOnly) return;
	InterlockedExchange(&m_stop, 1);
	if (m_thread) {
		WaitForSingleObject(m_thread, 3000);
		CloseHandle(m_thread);
		m_thread = NULL;
	}
	InterlockedExchange(&m_run, 0);
	m_peakOnly = FALSE;
	if (GetSafeHwnd())
		KillTimer(DR_TIMER);
}

void CDeviceRecordDlg::OnCbnSelchangeFormat()
{
	if (m_uiLocked) return;
	RefreshQualityCombo();
	CString path;
	m_path.GetWindowText(path);
	int fmt = m_fmt.GetCurSel();
	if (fmt < 0 || fmt > 2) fmt = 0;
	m_path.SetWindowText(NormalizeOutPath(path, fmt));
	RefreshOpaqueUi();
}

void CDeviceRecordDlg::OnBnClickedBrowse()
{
	if (m_uiLocked) return;
	int fmt = m_fmt.GetCurSel();
	if (fmt < 0 || fmt > 2) fmt = 0;
	CString path;
	m_path.GetWindowText(path);
	path = NormalizeOutPath(path, fmt);
	CFileDialog dlg(FALSE, ExtForFormat(fmt).Mid(1), path,
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, FilterForFormat(fmt), this);
	if (dlg.DoModal() == IDOK) {
		m_path.SetWindowText(NormalizeOutPath(dlg.GetPathName(), fmt));
		RefreshOpaqueUi();
	}
}

void CDeviceRecordDlg::SetRecordingUi(BOOL recording)
{
	if (!GetSafeHwnd()) return;
	m_uiLocked = recording;
	// EnableWindow(FALSE) はアクリル上で透過しやすいので使わない
	if (m_start.GetSafeHwnd()) {
		m_start.SetWindowText(recording
			? LL14(L"録音停止", L"Stop recording", L"Arrêter", L"Ferma",
				L"Detener", L"녹음 중지", L"停止录音", L"إيقاف التسجيل",
				L"Остановить", L"Aufnahme stoppen", L"Parar gravação", L"Opname stoppen",
				L"Zatrzymaj nagrywanie", L"Kaydı durdur")
			: LL14(L"録音開始", L"Start recording", L"Démarrer", L"Avvia",
				L"Iniciar", L"녹음 시작", L"开始录音", L"بدء التسجيل",
				L"Начать запись", L"Aufnahme starten", L"Iniciar gravação", L"Opname starten",
				L"Rozpocznij nagrywanie", L"Kaydı başlat"));
	}
	RefreshOpaqueUi();
}

void CDeviceRecordDlg::UpdateElapsedUi()
{
	if (!GetSafeHwnd() || !m_time.GetSafeHwnd()) return;
	DWORD ms = 0;
	if (m_startTick)
		ms = GetTickCount() - m_startTick;
	const int sec = (int)(ms / 1000);
	CString t;
	t.Format(L"%02d:%02d", sec / 60, sec % 60);
	m_time.SetWindowText(t);
}

BOOL CDeviceRecordDlg::StartRecording()
{
	StopPeakMonitor();
	if (m_thread || InterlockedCompareExchange(&m_run, 0, 0) != 0) return FALSE;
	m_peakOnly = FALSE;
	PersistUiToSavedata();

	m_outFmt = savedata.record_format;
	m_mp3Kbps = savedata.record_mp3_kbps;
	m_flacLevel = savedata.record_flac_level;
	m_doMixMic = savedata.record_mix_mic != 0;
	m_finalPath = savedata.record_last_path;
	if (m_finalPath.IsEmpty()) {
		m_status.SetWindowText(LL14(
			L"保存先を指定してください。", L"Please specify a save path.", L"Indiquez un chemin.",
			L"Specificare un percorso.", L"Especifique una ruta.", L"저장 위치를 지정하세요.",
			L"请指定保存路径。", L"يرجى تحديد مسار.", L"Укажите путь сохранения.",
			L"Bitte einen Speicherpfad angeben.", L"Indique um caminho.", L"Geef een pad op.",
			L"Podaj ścieżkę zapisu.", L"Lütfen bir yol belirtin."));
		return FALSE;
	}
	m_finalPath = NormalizeOutPath(m_finalPath, m_outFmt);
	m_path.SetWindowText(m_finalPath);

	if (m_outFmt == 0)
		m_wavPath = m_finalPath;
	else
		m_wavPath = DrMakeTempWavPath();

	{
		CFileException ex;
		if (!m_wavFile.Open(m_wavPath, CFile::modeCreate | CFile::modeReadWrite | CFile::shareExclusive, &ex)) {
			m_status.SetWindowText(LL14(
				L"ファイルを開けません。", L"Cannot open file.", L"Impossible d'ouvrir le fichier.",
				L"Impossibile aprire il file.", L"No se puede abrir el archivo.", L"파일을 열 수 없습니다.",
				L"无法打开文件。", L"تعذر فتح الملف.", L"Не удалось открыть файл.",
				L"Datei kann nicht geöffnet werden.", L"Não é possível abrir o ficheiro.", L"Kan bestand niet openen.",
				L"Nie można otworzyć pliku.", L"Dosya açılamıyor."));
			return FALSE;
		}
	}
	m_wavCh = 2; m_wavHz = 48000; m_wavBits = 16;
	DrWriteWavHeader(m_wavFile, m_wavCh, m_wavHz, m_wavBits);

	InterlockedExchange(&m_stop, 0);
	InterlockedExchange(&m_run, 0);
	InterlockedExchange(&m_pcmBytes, 0);
	InterlockedExchange(&m_lastHr, S_OK);
	InterlockedExchange(&s_micW, 0);
	InterlockedExchange(&s_micR, 0);
	uintptr_t th = _beginthreadex(NULL, 0, CaptureThread, this, 0, NULL);
	if (!th) {
		m_wavFile.Close();
		DeleteFile(m_wavPath);
		m_status.SetWindowText(LL14(
			L"録音スレッドを開始できません。", L"Cannot start capture thread.", L"Impossible de démarrer le thread.",
			L"Impossibile avviare il thread.", L"No se puede iniciar el hilo.", L"녹음 스레드를 시작할 수 없습니다.",
			L"无法启动录音线程。", L"تعذر بدء مؤشر الترابط.", L"Не удалось запустить поток.",
			L"Aufnahmethread kann nicht gestartet werden.", L"Não é possível iniciar a thread.", L"Kan thread niet starten.",
			L"Nie można uruchomić wątku.", L"Kayıt iş parçacığı başlatılamıyor."));
		return FALSE;
	}
	m_thread = (HANDLE)th;
	m_everStarted = TRUE;
	m_startTick = GetTickCount();
	SetRecordingUi(TRUE);
	SetTimer(DR_TIMER, 200, NULL);
	m_status.SetWindowText(LL14(
		L"録音中…（再生端末で音を出してください）", L"Recording… (play audio on the device)",
		L"Enregistrement… (jouez de l'audio)", L"Registrazione… (riproduci audio)",
		L"Grabando… (reproduzca audio)", L"녹음 중…(장치에서 소리를 재생하세요)",
		L"录音中…（请在设备上播放声音）", L"جارٍ التسجيل… (شغّل الصوت)",
		L"Идёт запись… (воспроизведите звук)", L"Aufnahme… (Audio abspielen)",
		L"A gravar… (reproduza áudio)", L"Bezig met opnemen… (speel audio)",
		L"Nagrywanie… (odtwarzaj dźwięk)", L"Kaydediliyor… (ses çalın)"));
	return TRUE;
}

void CDeviceRecordDlg::StopRecording(BOOL encodeAfter)
{
	if (m_stopping) return;
	m_stopping = TRUE;

	if (GetSafeHwnd())
		KillTimer(DR_TIMER);
	InterlockedExchange(&m_stop, 1);
	if (m_thread) {
		WaitForSingleObject(m_thread, 8000);
		CloseHandle(m_thread);
		m_thread = NULL;
	}
	InterlockedExchange(&m_run, 0);
	InterlockedExchange(&m_stop, 0);

	const LONG pcmBytes = InterlockedCompareExchange(&m_pcmBytes, 0, 0);
	const HRESULT capHr = (HRESULT)InterlockedCompareExchange(&m_lastHr, 0, 0);

	BOOL hadFile = FALSE;
	if (m_csInit) {
		EnterCriticalSection(&m_fileCs);
		hadFile = (m_wavFile.m_hFile != CFile::hFileNull);
		if (hadFile) {
			m_wavFile.Flush();
			DrFinalizeWavHeader(m_wavFile, m_wavCh, m_wavBits);
			m_wavFile.Flush();
			m_wavFile.Close();
		}
		LeaveCriticalSection(&m_fileCs);
	}

	const BOOL uiAlive = (GetSafeHwnd() != NULL);
	if (uiAlive)
		SetRecordingUi(FALSE);

	if (!encodeAfter) {
		if (!m_wavPath.IsEmpty() && m_outFmt != 0)
			DeleteFile(m_wavPath);
		m_stopping = FALSE;
		if (uiAlive) StartPeakMonitor();
		return;
	}

	if (!hadFile || pcmBytes < 4) {
		if (!m_wavPath.IsEmpty())
			DeleteFile(m_wavPath);
		if (uiAlive) {
			CString msg;
			if (FAILED(capHr)) {
				msg.Format(LL14(
					L"録音を開始できませんでした (HRESULT=0x%08X)。\n別の再生端末を試すか、端末で音を出してから再試行してください。",
					L"Could not start capture (HRESULT=0x%08X).\nTry another device or play audio, then retry.",
					L"Impossible de démarrer (HRESULT=0x%08X).",
					L"Avvio cattura non riuscito (HRESULT=0x%08X).",
					L"No se pudo iniciar (HRESULT=0x%08X).",
					L"녹음을 시작할 수 없습니다 (HRESULT=0x%08X).",
					L"无法开始录音 (HRESULT=0x%08X)。",
					L"تعذر بدء الالتقاط (HRESULT=0x%08X).",
					L"Не удалось начать захват (HRESULT=0x%08X).",
					L"Aufnahme startete nicht (HRESULT=0x%08X).",
					L"Não foi possível iniciar (HRESULT=0x%08X).",
					L"Kon opname niet starten (HRESULT=0x%08X).",
					L"Nie można rozpocząć (HRESULT=0x%08X).",
					L"Kayıt başlatılamadı (HRESULT=0x%08X)."), (unsigned)capHr);
			} else {
				msg = LL14(
					L"録音データがありません。\n選択した再生端末で音を出しながら録音してください。",
					L"No audio was captured.\nPlay sound on the selected playback device while recording.",
					L"Aucune donnée capturée. Jouez de l'audio sur le périphérique.",
					L"Nessun dato catturato. Riproduci audio sul dispositivo.",
					L"No se capturó audio. Reproduzca sonido en el dispositivo.",
					L"녹음 데이터가 없습니다. 선택한 재생 장치에서 소리를 내며 녹음하세요.",
					L"没有录到数据。请在所选播放设备上播放声音再录音。",
					L"لا توجد بيانات. شغّل الصوت على جهاز التشغيل.",
					L"Нет данных. Воспроизведите звук на устройстве.",
					L"Keine Audiodaten. Spielen Sie Audio auf dem Gerät ab.",
					L"Sem dados. Reproduza áudio no dispositivo.",
					L"Geen data. Speel audio af op het apparaat.",
					L"Brak danych. Odtwarzaj dźwięk na urządzeniu.",
					L"Veri yok. Seçili aygıtta ses çalarak kaydedin.");
			}
			m_status.SetWindowText(msg);
		}
		m_stopping = FALSE;
		if (uiAlive) StartPeakMonitor();
		return;
	}

	BOOL ok = TRUE;
	if (uiAlive && m_outFmt == 1) {
		m_status.SetWindowText(LL14(
			L"mp3 変換中…", L"Encoding mp3…", L"Encodage mp3…", L"Codifica mp3…",
			L"Codificando mp3…", L"mp3 변환 중…", L"正在编码 mp3…", L"ترميز mp3…",
			L"Кодирование mp3…", L"mp3 wird kodiert…", L"A codificar mp3…", L"mp3 coderen…",
			L"Kodowanie mp3…", L"mp3 kodlanıyor…"));
	} else if (uiAlive && m_outFmt == 2) {
		m_status.SetWindowText(LL14(
			L"FLAC 変換中…", L"Encoding FLAC…", L"Encodage FLAC…", L"Codifica FLAC…",
			L"Codificando FLAC…", L"FLAC 변환 중…", L"正在编码 FLAC…", L"ترميز FLAC…",
			L"Кодирование FLAC…", L"FLAC wird kodiert…", L"A codificar FLAC…", L"FLAC coderen…",
			L"Kodowanie FLAC…", L"FLAC kodlanıyor…"));
	}

	::DeleteFile(m_finalPath);
	// 閉じた直後の一時WAVを確実に読めるよう短く待つ
	Sleep(30);
	if (m_outFmt == 1) {
		ok = EncodeWavToMp3(m_wavPath, m_finalPath, m_mp3Kbps);
		DeleteFile(m_wavPath);
	} else if (m_outFmt == 2) {
		ok = EncodeWavToFlac(m_wavPath, m_finalPath, m_flacLevel);
		DeleteFile(m_wavPath);
	}

	if (uiAlive) {
		if (ok) {
			CString msg;
			msg.Format(LL14(
				L"保存しました:\n%s", L"Saved:\n%s", L"Enregistré:\n%s", L"Salvato:\n%s",
				L"Guardado:\n%s", L"저장됨:\n%s", L"已保存:\n%s", L"تم الحفظ:\n%s",
				L"Сохранено:\n%s", L"Gespeichert:\n%s", L"Guardado:\n%s", L"Opgeslagen:\n%s",
				L"Zapisano:\n%s", L"Kaydedildi:\n%s"), (LPCTSTR)m_finalPath);
			m_status.SetWindowText(msg);
		} else {
			m_status.SetWindowText(LL14(
				L"変換または保存に失敗しました。", L"Encode or save failed.", L"Échec de l'encodage ou de l'enregistrement.",
				L"Codifica o salvataggio non riusciti.", L"Error al codificar o guardar.", L"변환 또는 저장에 실패했습니다.",
				L"编码或保存失败。", L"فشل الترميز أو الحفظ.", L"Ошибка кодирования или сохранения.",
				L"Kodierung oder Speichern fehlgeschlagen.", L"Falha ao codificar ou guardar.", L"Coderen of opslaan mislukt.",
				L"Kodowanie lub zapis nie powiodły się.", L"Kodlama veya kaydetme başarısız."));
		}
		UpdateElapsedUi();
	}
	m_stopping = FALSE;
	if (uiAlive) StartPeakMonitor();
}

UINT __stdcall CDeviceRecordDlg::CaptureThread(void* p)
{
	CDeviceRecordDlg* self = (CDeviceRecordDlg*)p;
	HRESULT hrCo = CoInitializeEx(NULL, COINIT_MULTITHREADED);

	IMMDeviceEnumerator* enumer = NULL;
	IMMDevice* renderDev = NULL;
	IAudioClient* loopClient = NULL;
	IAudioCaptureClient* loopCap = NULL;
	WAVEFORMATEX* mixFmt = NULL;
	HANDLE hEvent = NULL;

	IMMDevice* micDev = NULL;
	IAudioClient* micClient = NULL;
	IAudioCaptureClient* micCap = NULL;
	WAVEFORMATEX* micFmt = NULL;
	HANDLE hMicEvent = NULL;

	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator), (void**)&enumer);
	if (FAILED(hr) || !enumer) { InterlockedExchange(&self->m_lastHr, hr); goto done; }

	if (savedata.loop_device[0]) {
		hr = enumer->GetDevice(savedata.loop_device, &renderDev);
		if (FAILED(hr) || !renderDev) {
			if (renderDev) { renderDev->Release(); renderDev = NULL; }
			hr = enumer->GetDefaultAudioEndpoint(eRender, eConsole, &renderDev);
		}
	} else {
		hr = enumer->GetDefaultAudioEndpoint(eRender, eConsole, &renderDev);
	}
	if (FAILED(hr) || !renderDev) { InterlockedExchange(&self->m_lastHr, hr); goto done; }

	hr = DrInitLoopbackCapture(renderDev, &loopClient, &loopCap, &mixFmt, &hEvent);
	if (FAILED(hr) || !loopClient || !loopCap || !mixFmt) {
		InterlockedExchange(&self->m_lastHr, FAILED(hr) ? hr : E_FAIL);
		goto done;
	}

	self->m_wavHz = 48000; // mp3(MF) 互換のため出力は常に 48kHz
	self->m_wavCh = 2;
	self->m_wavBits = 16;
	const DWORD srcHz = mixFmt->nSamplesPerSec ? mixFmt->nSamplesPerSec : 48000;
	{
		EnterCriticalSection(&self->m_fileCs);
		if (self->m_wavFile.m_hFile != CFile::hFileNull) {
			self->m_wavFile.SeekToBegin();
			DrWriteWavHeader(self->m_wavFile, self->m_wavCh, self->m_wavHz, self->m_wavBits);
		}
		LeaveCriticalSection(&self->m_fileCs);
	}

	if (self->m_doMixMic) {
		DrMicEnsureCs();
		HRESULT hm = S_OK;
		if (savedata.mic_device[0]) {
			hm = enumer->GetDevice(savedata.mic_device, &micDev);
			if (FAILED(hm) || !micDev) {
				if (micDev) { micDev->Release(); micDev = NULL; }
				hm = enumer->GetDefaultAudioEndpoint(eCapture, eConsole, &micDev);
			}
		} else {
			hm = enumer->GetDefaultAudioEndpoint(eCapture, eConsole, &micDev);
		}
		if (SUCCEEDED(hm) && micDev) {
			hm = micDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&micClient);
			if (SUCCEEDED(hm) && micClient) {
				hm = micClient->GetMixFormat(&micFmt);
				if (SUCCEEDED(hm) && micFmt) {
					hMicEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
					BOOL micOk = FALSE;
					if (hMicEvent) {
						hm = micClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
							AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
							2000000, 0, micFmt, NULL);
						if (SUCCEEDED(hm))
							hm = micClient->SetEventHandle(hMicEvent);
						if (SUCCEEDED(hm)) {
							hm = micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&micCap);
							if (SUCCEEDED(hm) && micCap) micOk = TRUE;
						}
					}
					if (!micOk) {
						if (hMicEvent) { CloseHandle(hMicEvent); hMicEvent = NULL; }
						if (micCap) { micCap->Release(); micCap = NULL; }
						if (micClient) { micClient->Release(); micClient = NULL; }
						if (micFmt) { CoTaskMemFree(micFmt); micFmt = NULL; }
						hm = micDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&micClient);
						if (SUCCEEDED(hm) && micClient) {
							hm = micClient->GetMixFormat(&micFmt);
							if (SUCCEEDED(hm) && micFmt) {
								hm = micClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
									AUDCLNT_STREAMFLAGS_NOPERSIST,
									2000000, 0, micFmt, NULL);
								if (SUCCEEDED(hm)) {
									hm = micClient->GetService(__uuidof(IAudioCaptureClient), (void**)&micCap);
									if (SUCCEEDED(hm) && micCap) micOk = TRUE;
								}
							}
						}
					}
					if (micOk && micCap && micFmt) {
						InterlockedExchange(&s_micCapRate, (LONG)micFmt->nSamplesPerSec);
						micClient->Start();
					} else {
						if (micCap) { micCap->Release(); micCap = NULL; }
						if (micClient) { micClient->Release(); micClient = NULL; }
						if (micFmt) { CoTaskMemFree(micFmt); micFmt = NULL; }
						if (hMicEvent) { CloseHandle(hMicEvent); hMicEvent = NULL; }
					}
				}
			}
		}
	}

	hr = loopClient->Start();
	if (FAILED(hr)) { InterlockedExchange(&self->m_lastHr, hr); goto done; }
	InterlockedExchange(&self->m_run, 1);
	InterlockedExchange(&self->m_lastHr, S_OK);

	while (InterlockedCompareExchange(&self->m_stop, 0, 0) == 0) {
		if (hEvent) {
			HANDLE waits[2];
			DWORD nWait = 1;
			waits[0] = hEvent;
			if (hMicEvent) { waits[1] = hMicEvent; nWait = 2; }
			WaitForMultipleObjects(nWait, waits, FALSE, 50);
		} else {
			if (hMicEvent)
				WaitForSingleObject(hMicEvent, 10);
			else
				Sleep(10);
		}

		if (micCap && micFmt) {
			UINT32 packet = 0;
			HRESULT hm = micCap->GetNextPacketSize(&packet);
			while (SUCCEEDED(hm) && packet > 0 && InterlockedCompareExchange(&self->m_stop, 0, 0) == 0) {
				BYTE* data = NULL;
				UINT32 frames = 0;
				DWORD flags = 0;
				hm = micCap->GetBuffer(&data, &frames, &flags, NULL, NULL);
				if (FAILED(hm)) break;
				if (frames > 0 && data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
					float conv[4096 * 2];
					UINT32 done = 0;
					while (done < frames) {
						UINT32 n = frames - done;
						if (n > 4096) n = 4096;
						float pk = 0.f;
						for (UINT32 i = 0; i < n; ++i) {
							float L, R;
							DrSampleToFloat(data + (done + i) * micFmt->nBlockAlign, micFmt, L, R);
							conv[i * 2 + 0] = L;
							conv[i * 2 + 1] = R;
							const float aL = (L < 0.f) ? -L : L;
							const float aR = (R < 0.f) ? -R : R;
							if (aL > pk) pk = aL;
							if (aR > pk) pk = aR;
						}
						{
							const LONG v = (LONG)(pk * 1000.f);
							LONG cur = InterlockedCompareExchange(&self->m_peakMic, 0, 0);
							if (v > cur) InterlockedExchange(&self->m_peakMic, v > 1000 ? 1000 : v);
						}
						DrMicRingWrite(conv, (int)n);
						done += n;
					}
				}
				micCap->ReleaseBuffer(frames);
				hm = micCap->GetNextPacketSize(&packet);
			}
		}

		UINT32 packet = 0;
		hr = loopCap->GetNextPacketSize(&packet);
		while (SUCCEEDED(hr) && packet > 0 && InterlockedCompareExchange(&self->m_stop, 0, 0) == 0) {
			BYTE* data = NULL;
			UINT32 frames = 0;
			DWORD flags = 0;
			hr = loopCap->GetBuffer(&data, &frames, &flags, NULL, NULL);
			if (FAILED(hr)) break;
			if (frames > 0) {
				float fL[4096], fR[4096];
				short pcm[8192 * 2];
				UINT32 done = 0;
				while (done < frames) {
					UINT32 n = frames - done;
					if (n > 4096) n = 4096;
					float pkSys = 0.f;
					for (UINT32 i = 0; i < n; ++i) {
						float L = 0.f, R = 0.f;
						if (data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
							DrSampleToFloat(data + (done + i) * mixFmt->nBlockAlign, mixFmt, L, R);
						fL[i] = L;
						fR[i] = R;
						const float aL = (L < 0.f) ? -L : L;
						const float aR = (R < 0.f) ? -R : R;
						if (aL > pkSys) pkSys = aL;
						if (aR > pkSys) pkSys = aR;
					}
					{
						const LONG v = (LONG)(pkSys * 1000.f);
						LONG cur = InterlockedCompareExchange(&self->m_peakSys, 0, 0);
						if (v > cur) InterlockedExchange(&self->m_peakSys, v > 1000 ? 1000 : v);
					}
					if (self->m_doMixMic)
						DrMicIntoStereo(fL, fR, (int)n, (int)srcHz);

					int outFrames = (int)(((__int64)n * 48000) / (srcHz ? srcHz : 48000));
					if (outFrames < 1 && n > 0) outFrames = 1;
					if (outFrames > 8192) outFrames = 8192;
					float pkMix = 0.f;
					for (int o = 0; o < outFrames; ++o) {
						double srcPos = (srcHz == 48000) ? (double)o : ((double)o * (double)srcHz / 48000.0);
						int i0 = (int)srcPos;
						int i1 = i0 + 1;
						if (i0 < 0) i0 = 0;
						if (i0 >= (int)n) i0 = (int)n - 1;
						if (i1 >= (int)n) i1 = (int)n - 1;
						const float frac = (float)(srcPos - (double)i0);
						float L = fL[i0] + (fL[i1] - fL[i0]) * frac;
						float R = fR[i0] + (fR[i1] - fR[i0]) * frac;
						L = DrClamp1(L);
						R = DrClamp1(R);
						const float aL = (L < 0.f) ? -L : L;
						const float aR = (R < 0.f) ? -R : R;
						if (aL > pkMix) pkMix = aL;
						if (aR > pkMix) pkMix = aR;
						int iL = (int)(L * 32767.f);
						int iR = (int)(R * 32767.f);
						if (iL > 32767) iL = 32767; if (iL < -32768) iL = -32768;
						if (iR > 32767) iR = 32767; if (iR < -32768) iR = -32768;
						pcm[o * 2 + 0] = (short)iL;
						pcm[o * 2 + 1] = (short)iR;
					}
					{
						const LONG v = (LONG)(pkMix * 1000.f);
						LONG cur = InterlockedCompareExchange(&self->m_peakMix, 0, 0);
						if (v > cur) InterlockedExchange(&self->m_peakMix, v > 1000 ? 1000 : v);
					}
					if (!self->m_peakOnly) {
						EnterCriticalSection(&self->m_fileCs);
						if (self->m_wavFile.m_hFile != CFile::hFileNull) {
							self->m_wavFile.Write(pcm, outFrames * 4);
							InterlockedExchangeAdd(&self->m_pcmBytes, (LONG)(outFrames * 4));
						}
						LeaveCriticalSection(&self->m_fileCs);
					}
					done += n;
				}
			}
			loopCap->ReleaseBuffer(frames);
			hr = loopCap->GetNextPacketSize(&packet);
		}
	}

	loopClient->Stop();
	if (micClient) micClient->Stop();

done:
	InterlockedExchange(&self->m_run, 0);
	if (loopCap) loopCap->Release();
	if (loopClient) loopClient->Release();
	if (mixFmt) CoTaskMemFree(mixFmt);
	if (renderDev) renderDev->Release();
	if (micCap) micCap->Release();
	if (micClient) micClient->Release();
	if (micFmt) CoTaskMemFree(micFmt);
	if (micDev) micDev->Release();
	if (enumer) enumer->Release();
	if (hEvent) CloseHandle(hEvent);
	if (hMicEvent) CloseHandle(hMicEvent);
	if (SUCCEEDED(hrCo) || hrCo == S_FALSE) CoUninitialize();
	return 0;
}

void CDeviceRecordDlg::OnBnClickedStart()
{
	if (m_thread || InterlockedCompareExchange(&m_run, 0, 0) != 0) {
		StopRecording(TRUE);
		return;
	}
	StartRecording();
}

void CDeviceRecordDlg::CloseModeless()
{
	if (!m_peakOnly && (m_thread || InterlockedCompareExchange(&m_run, 0, 0) != 0))
		StopRecording(TRUE);
	else
		StopPeakMonitor();
	if (GetSafeHwnd())
		PersistUiToSavedata();
	if (GetSafeHwnd())
		DestroyWindow();
}

void CDeviceRecordDlg::OnBnClickedClose()
{
	CloseModeless();
}

void CDeviceRecordDlg::OnCancel()
{
	CloseModeless();
}

void CDeviceRecordDlg::OnOK()
{
	OnBnClickedStart();
}

void CDeviceRecordDlg::PostNcDestroy()
{
	CCustomBlurDialogBase::PostNcDestroy();
	if (g_deviceRecordDlg == this)
		g_deviceRecordDlg = NULL;
	delete this;
}

void CDeviceRecordDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == DR_TIMER) {
		PaintMetersFromPeaks();
		if (!m_peakOnly)
			UpdateElapsedUi();
		if (m_thread) {
			DWORD code = 0;
			if (GetExitCodeThread(m_thread, &code) && code != STILL_ACTIVE) {
				if (m_peakOnly)
					StopPeakMonitor();
				else
					StopRecording(TRUE);
				return;
			}
		}
		if (m_path.GetSafeHwnd() && ::GetFocus() == m_path.GetSafeHwnd())
			m_path.RepaintClient();
	}
	CCustomBlurDialogBase::OnTimer(nIDEvent);
}

void OpenDeviceRecordModeless(CWnd* parent)
{
	if (g_deviceRecordDlg && ::IsWindow(g_deviceRecordDlg->GetSafeHwnd())) {
		g_deviceRecordDlg->ShowWindow(SW_SHOW);
		g_deviceRecordDlg->SetForegroundWindow();
		return;
	}
	g_deviceRecordDlg = new CDeviceRecordDlg(parent);
	if (!g_deviceRecordDlg->Create(IDD_DEVICERECORD, parent)) {
		delete g_deviceRecordDlg;
		g_deviceRecordDlg = NULL;
		return;
	}
	g_deviceRecordDlg->ShowWindow(SW_SHOW);
	g_deviceRecordDlg->SetForegroundWindow();
}

void CloseDeviceRecordIfOpen()
{
	if (g_deviceRecordDlg && ::IsWindow(g_deviceRecordDlg->GetSafeHwnd()))
		g_deviceRecordDlg->DestroyWindow();
}

void CDeviceRecordDlg::OnDestroy()
{
	KillTimer(DR_TIMER);
	m_peakOnly = TRUE; // StopPeakMonitor 経路でも落とす
	InterlockedExchange(&m_stop, 1);
	if (m_thread) {
		WaitForSingleObject(m_thread, 8000);
		CloseHandle(m_thread);
		m_thread = NULL;
	}
	m_peakOnly = FALSE;
	if (m_csInit) {
		EnterCriticalSection(&m_fileCs);
		if (m_wavFile.m_hFile != CFile::hFileNull) {
			DrFinalizeWavHeader(m_wavFile, m_wavCh, m_wavBits);
			m_wavFile.Close();
		}
		LeaveCriticalSection(&m_fileCs);
	}
	CCustomBlurDialogBase::OnDestroy();
}

BOOL CDeviceRecordDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (m_uiLocked && pMsg) {
		if (pMsg->message == WM_KEYDOWN || pMsg->message == WM_CHAR) {
			HWND h = pMsg->hwnd;
			if (h == m_path.GetSafeHwnd() || h == m_dev.GetSafeHwnd()
				|| h == m_fmt.GetSafeHwnd() || h == m_qual.GetSafeHwnd())
				return TRUE;
		}
	}
	if (pMsg && pMsg->message == WM_LBUTTONDOWN && m_path.GetSafeHwnd()) {
		PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
		m_path.RepaintClient();
	}
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}
