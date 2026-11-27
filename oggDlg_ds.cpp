#include "stdafx.h"
#include "EqKeyAnalyze.h"
#include "NoteFundamentalPick.h"
#include "ogg.h"
#include "oggDlg.h"
#include "ProAudio.h"
#include "SongParams.h"
#include "MpPlayerAddons.h"

#include "PlayList.h"
//#include <math.h>
//#include <vorbis/codec.h>
//#include <vorbis/vorbisfile.h>
//#include <MMSystem.h>
#include "dsound.h"
//#include "afxmt.h"
//#include "Douga.h"
//#include "itiran.h"
//#include "itiran_FC.h"
//#include "itiran_YSF.h"
//#include "itiran_YS6.h"
//#include "itiran_YSO.h"
//#include "vfw.h"
//#include <direct.h>
//#include "Folder.h"
//#include "dsound.h"

#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <Audioclient.h>
#include <endpointvolume.h>
#include <FunctionDiscoveryKeys_devpkey.h>

#include "rubberband/RubberBandStretcher.h"
#include "AudioUpscaler.h"
#include "XfadePlayback.h"
#include "ProAudio.h"
#include "DecodeProgress.h"
#include "CPromptEngine.h"
#if _MSC_VER >= 1950 || defined(__INTEL_LLVM_COMPILER) || defined(OGG_AVX2_VS2026)
#pragma comment(lib,"rubberband-library_2026")
#else
#pragma comment(lib,"rubberband-library")
#endif
// 曲終端のジャスト検出用（oggDlg.cpp 定義）。単位は「DS バッファへ書き込んだ総バイト数」。
extern volatile LONG g_playGeneration;
extern __int64 g_dsWrittenBytes;
extern __int64 g_endWrittenBytes;
extern __int64 g_heardBytes;
extern __int64 g_expectedDsBytes;
extern int g_outBytesPerFrame;
extern 	LPDIRECTSOUND8 m_ds;
extern 	LPDIRECTSOUNDBUFFER m_dsb1;
extern 	LPDIRECTSOUNDBUFFER8 m_dsb;
extern 	LPDIRECTSOUND3DBUFFER m_dsb3d;
extern	LPDIRECTSOUNDBUFFER m_p;
extern LPDIRECTSOUND3DBUFFER m_lpDS3DBuffer;

extern int	playf;
extern void ReleaseOggVorbis(char**);
extern char* ogg;
extern CString filen;   // 現在再生中の曲フルパス(SongParams 用)
extern DWORD hw;
extern HANDLE hNotifyEvent[2];
extern LPDIRECTSOUNDNOTIFY dsnf1;
extern LPDIRECTSOUNDNOTIFY dsnf2;
extern UINT HandleNotifications(LPVOID lpvoid);
extern UINT WASAPIHandleNotifications(LPVOID lpvoid);
extern ULONG WAVDALen;
extern UINT ttt;
extern int wavchannel, wavbit_sample_Hz, wavsam_depth;
int wavbitbackup;
#define BUFSZ			((UINT)10240*6/2)
#define HIGHDIV			4
#define BUFSZH			(BUFSZ/HIGHDIV)
#define SQRT_BUFSZ2		64
#define M_PI			3.1415926535897932384
#define ABS(N)			( (N)<0 ? -(N) : (N) )
#define OUTPUT_BUFFER_SIZE  BUFSZ
#define OUTPUT_BUFFER_NUM   5
extern void playwavds(BYTE* bw);
extern void playwavds2(BYTE* bw, int len);
extern BOOL playwavBuffwav(BYTE* bw, int old, int l1, int l2);
extern int mode;
extern int oggsize;
extern int loop2;
extern CPlayList* pl;
extern int plcnt;
extern int Mp3GetDecoderBitsForRubberBand(void);
extern int wav999_use_adbuf;
extern save savedata;
extern bool g_isWavExportRendering;
LPDIRECTSOUND3DLISTENER m_listener = NULL;
extern RubberBand::RubberBandStretcher* g_rubberBandStretcher[2];
void RubberBand_DestroyBank(int bank);
void RubberBand_DestroyAll();
bool InitializeRubberBandStretcher(int bank);
bool InitializeRubberBandStretcher();
bool ProcessAudioWithRubberBand(float tempoRate, bool t = false);
bool ProcessAudioWithRubberBandBank(int bank, float tempoRate, bool t,
	const uint8_t* inData, int inBytes, int bits, int ch, int rate,
	float* outFloat, int& outN, int outCap);

#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);

IMMDeviceEnumerator* deviceEnumerator = NULL;
IMMDeviceCollection* pDeviceCollection = NULL;
IMMDevice* pDevice = NULL;
IAudioClient* pAudioClient = NULL;
IAudioRenderClient* pRenderClient = NULL;
REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
WAVEFORMATEX* pwfx = NULL;
UINT32 bufferFrameCount;


CString COggDlg::init(HWND hwnd, int sm)
{
	CoInitialize(NULL);
	GUID strr = savedata.soundguid;
	if (strr.Data1 == 0) {
		DirectSoundCreate8(NULL, &m_ds, NULL);
	}
	else {
		DirectSoundCreate8(&strr, &m_ds, NULL);
		if (m_ds == NULL) {
			DirectSoundCreate8(NULL, &m_ds, NULL);
			savedata.soundguid = { 0,0,0,0 };
			savedata.soundcur = 0;
		}
	}
	if (m_ds == NULL) return LL14(L"DirectSoundを生成できません。\\nDirectX7が正常にインストールされているか確認してください。", L"Could not create DirectSound.\\nPlease verify DirectX7 is properly installed.", L"Impossible de créer DirectSound.\\nVérifiez que DirectX7 est correctement installé.", L"Impossibile creare DirectSound.\\nVerificare che DirectX7 sia installato correttamente.", L"No se pudo crear DirectSound.\\nCompruebe que DirectX7 esté instalado correctamente.", L"DirectSound를 생성할 수 없습니다.\\nDirectX7이 올바르게 설치되어 있는지 확인하세요.", L"无法创建 DirectSound。\\n请确认 DirectX7 已正确安装。", L"تعذر إنشاء DirectSound.\\nتحقق من تثبيت DirectX7 بشكل صحيح.", L"Не удалось создать DirectSound.\\nУбедитесь, что DirectX7 установлен правильно.", L"DirectSound konnte nicht erstellt werden.\\nPrüfen Sie, ob DirectX7 korrekt installiert ist.", L"Não foi possível criar DirectSound.\\nVerifique se o DirectX7 está instalado corretamente.", L"Kon DirectSound niet maken.\\nControleer of DirectX7 correct is geïnstalleerd.", L"Nie można utworzyć DirectSound.\\nSprawdź, czy DirectX7 jest poprawnie zainstalowany.", L"DirectSound oluşturulamadı.\\nDirectX7'nin düzgün kurulduğunu doğrulayın.");
	if (m_ds->SetCooperativeLevel(hwnd, DSSCL_PRIORITY) != DS_OK) {
		MessageBox(LL14(L"SetCooperativeLevelに失敗しました", L"SetCooperativeLevel failed", L"Échec de SetCooperativeLevel", L"SetCooperativeLevel non riuscito", L"SetCooperativeLevel falló", L"SetCooperativeLevel 실패", L"SetCooperativeLevel 失败", L"فشل SetCooperativeLevel", L"SetCooperativeLevel не удался", L"SetCooperativeLevel fehlgeschlagen", L"SetCooperativeLevel falhou", L"SetCooperativeLevel mislukt", L"SetCooperativeLevel nie powiódł się", L"SetCooperativeLevel başarısız"));
		return LL14(L"DirectSoundの強調レベルを設定できません。\\nDirectX7が正常にインストールされているか確認してください。", L"Could not set DirectSound cooperative level.\\nPlease verify DirectX7 is properly installed.", L"Impossible de définir le niveau coopératif DirectSound.\\nVérifiez que DirectX7 est correctement installé.", L"Impossibile impostare il livello cooperativo DirectSound.\\nVerificare che DirectX7 sia installato correttamente.", L"No se pudo establecer el nivel cooperativo de DirectSound.\\nCompruebe que DirectX7 esté instalado correctamente.", L"DirectSound 협력 수준을 설정할 수 없습니다.\\nDirectX7이 올바르게 설치되어 있는지 확인하세요.", L"无法设置 DirectSound 协作级别。\\n请确认 DirectX7 已正确安装。", L"تعذر تعيين مستوى التعاون لـ DirectSound.\\nتحقق من تثبيت DirectX7 بشكل صحيح.", L"Не удалось установить уровень кооперации DirectSound.\\nУбедитесь, что DirectX7 установлен правильно.", L"Kooperatives DirectSound-Level konnte nicht gesetzt werden.\\nPrüfen Sie, ob DirectX7 korrekt installiert ist.", L"Não foi possível definir o nível cooperativo do DirectSound.\\nVerifique se o DirectX7 está instalado corretamente.", L"Kon cooperatief DirectSound-niveau niet instellen.\\nControleer of DirectX7 correct is geïnstalleerd.", L"Nie można ustawić poziomu współpracy DirectSound.\\nSprawdź, czy DirectX7 jest poprawnie zainstalowany.", L"DirectSound işbirliği düzeyi ayarlanamadı.\\nDirectX7'nin düzgün kurulduğunu doğrulayın.");
	}
	hw = 0;
	//	ZeroMemory(&d,sizeof(d));d.dwSize=sizeof(d);HRESULT r =m_ds->GetCaps(&d);
	//	if(r!=DS_OK){
	//		return "DirectSoundの情報を獲得出来ません。\nDirectX7が正常にインストールされているか確認してください。";
	//	}
	//	if(d.dwFlags & (DSCAPS_SECONDARYSTEREO|DSCAPS_PRIMARYSTEREO |DSCAPS_PRIMARY16BIT) && d.dwFreeHwMemBytes!=0){
	//		hw=DSBCAPS_LOCHARDWARE;
	//	}::timeSetEvent
	m_p = NULL;
	DSBUFFERDESC dss;
	ZeroMemory(&dss, sizeof(dss));
	dss.dwSize = sizeof(dss);
	//	dss.dwFlags=DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY|DSBCAPS_PRIMARYBUFFER|hw;
	dss.dwFlags = DSBCAPS_PRIMARYBUFFER;
	dss.lpwfxFormat = NULL;
	dss.dwBufferBytes = 0;
	if (m_ds->CreateSoundBuffer(&dss, &m_p, NULL) != DS_OK) {
		return LL14(L"DirectSoundのプライマリバッファを生成できません。\\nDirectX7が正常にインストールされているか確認してください。", L"Could not create DirectSound primary buffer.\\nPlease verify DirectX7 is properly installed.", L"Impossible de créer le tampon principal DirectSound.\\nVérifiez que DirectX7 est correctement installé.", L"Impossibile creare il buffer primario DirectSound.\\nVerificare che DirectX7 sia installato correttamente.", L"No se pudo crear el búfer primario de DirectSound.\\nCompruebe que DirectX7 esté instalado correctamente.", L"DirectSound 기본 버퍼를 생성할 수 없습니다.\\nDirectX7이 올바르게 설치되어 있는지 확인하세요.", L"无法创建 DirectSound 主缓冲区。\\n请确认 DirectX7 已正确安装。", L"تعذر إنشاء المخزن المؤقت الأساسي لـ DirectSound.\\nتحقق من تثبيت DirectX7 بشكل صحيح.", L"Не удалось создать первичный буфер DirectSound.\\nУбедитесь, что DirectX7 установлен правильно.", L"Primärpuffer von DirectSound konnte nicht erstellt werden.\\nPrüfen Sie, ob DirectX7 korrekt installiert ist.", L"Não foi possível criar o buffer primário do DirectSound.\\nVerifique se o DirectX7 está instalado corretamente.", L"Kon DirectSound-primairbuffer niet maken.\\nControleer of DirectX7 correct is geïnstalleerd.", L"Nie można utworzyć bufora podstawowego DirectSound.\\nSprawdź, czy DirectX7 jest poprawnie zainstalowany.", L"DirectSound birincil tamponu oluşturulamadı.\\nDirectX7'nin düzgün kurulduğunu doğrulayın.");
	}

	if (m_p != NULL) {
		//		//PCMWAVEFORMAT p;
		WAVEFORMATEX p;
		ZeroMemory(&p, sizeof(p));
		p.wFormatTag = WAVE_FORMAT_PCM;
		p.nChannels = g_ds_pcm_ch;
		p.nSamplesPerSec = g_ds_pcm_rate;
		p.wBitsPerSample = (WORD)g_ds_pcm_bits;
		p.nBlockAlign = p.nChannels * p.wBitsPerSample / 8;
		p.nAvgBytesPerSec = p.nSamplesPerSec * p.nBlockAlign;
		p.cbSize = 0;
		static const GUID GUID_SUBTYPE_PCM = { 0x00000001, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

		DWORD targetSpeakers = (DWORD)DirectSoundChannelMaskForOutput(g_ds_pcm_ch, savedata.speaker_layout);
		WAVEFORMATEXTENSIBLE wfx = {};
		wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wfx.Format.nChannels = (WORD)g_ds_pcm_ch;
		wfx.Format.nSamplesPerSec = g_ds_pcm_rate;
		wfx.Format.wBitsPerSample = (WORD)g_ds_pcm_bits;
		wfx.Format.nBlockAlign = (WORD)(wfx.Format.wBitsPerSample / 8 * wfx.Format.nChannels);
		wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
		wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
		wfx.dwChannelMask = targetSpeakers;
		wfx.SubFormat = GUID_SUBTYPE_PCM;
		if (m_p->SetFormat(&p) != DS_OK) {
			if (m_p->SetFormat((LPWAVEFORMATEX)&wfx) != DS_OK)
				if (m_p != NULL) { m_p->Release(); m_p = NULL; }
		}
	}
	else {
	}
	//m_p->QueryInterface(IID_IDirectSound3DListener, (LPVOID*)&m_listener);
	//m_listener->SetPosition(0.0f, 0.0f, 0.0f, DS3D_IMMEDIATE);

	return _T("");
}


extern void DoEvent();
/*
void DoEvent()
{
	MSG msg;
	for(;;){
		if(PeekMessage(&msg,NULL,0,0,PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}else return;
	}
}
*/
void COggDlg::Vol(int vol)
{
	//	if(pAudioClient==NULL)
	//		m_dsb->SetVolume(vol);
	//	else
	//		pAudioClient->
}

void COggDlg::Closeds()
{
	DsOpLock ds;
	if (m_dsb) {
		m_dsb->Stop();
		m_dsb->Release();
		m_dsb = NULL;
	}
	if (m_dsb3d) {
		m_dsb3d->Release();
		m_dsb3d = NULL;
	}
	if (m_dsb1) {
		m_dsb1->Release();
		m_dsb1 = NULL;
	}
	if (pAudioClient) {
		pAudioClient->Stop();
		if (pRenderClient) { pRenderClient->Release(); pRenderClient = NULL; }
		pAudioClient->Release(); pAudioClient = NULL;
	}
}

BOOL COggDlg::ReleaseDXSound(void)
{
	// 二重解放防止: Closeds でバッファ系はすべて落とす
	Closeds();
	if (m_lpDS3DBuffer != NULL) { m_lpDS3DBuffer->Release(); m_lpDS3DBuffer = NULL; }
	if (m_p != NULL) { m_p->Release(); m_p = NULL; }
	if (m_ds) {
		m_ds->Release();
		m_ds = NULL;
	}
	if (pAudioClient) {
		pAudioClient->Stop();
		if (pRenderClient) { pRenderClient->Release(); pRenderClient = NULL; }
		pAudioClient->Release(); pAudioClient = NULL;
		if (pDevice) { pDevice->Release(); pDevice = NULL; }
	}

	// RubberBandストレッチャーのクリーンアップ
	RubberBand_DestroyAll();

	return TRUE;
}

extern void playwavds2(BYTE* bw, int old, int l1, int l2);
extern void DispatchPlaywavFill(BYTE* bufwav3, ULONG oldw, int len1, int len2);
extern "C" int VstLiveThruIsOn(void);
extern int playwavkpi(BYTE* bw, int old, int l1, int l2);
extern int playwavmp3(BYTE* bw, int old, int l1, int l2);
extern int playwavwav(BYTE* bw, int old, int l1, int l2);
extern int playwavflac(BYTE* bw, int old, int l1, int l2);
extern int playwavdsd(BYTE* bw, int old, int l1, int l2);
extern int playwavm4a(BYTE* bw, int old, int l1, int l2);
extern int playwavopus(BYTE* bw, int old, int l1, int l2);
extern BYTE bufwav3[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 8];
extern int ps;
extern COggDlg* og;
extern BOOL thn;
extern BOOL thn1;
extern int stf;
extern int endf;
extern BOOL sek;
extern int wavchannel, wavbit_sample_Hz, wavsam_depth;
//スレッド
int syukai = 0, syukai2 = 0;
extern BOOL sflg;
#define MUON 180
int flg3 = 0;
int sek4;
extern int tempo;


ULONG oldw = OUTPUT_BUFFER_SIZE * 2;
extern float m_convertedPcmFloatData[];
extern int m_convertedPcmN;
extern uint8_t outputRawBytesData[];
extern int outputRawN;

//bool ProcessAudioWithSoundTouch(float tempoRate);
bool ProcessAudioWithRubberBand(float tempoRate, bool t);
void ConvertRawBytesToFloat(const uint8_t* raw_data, int rawN,
	uint16_t bits_per_sample, uint16_t channels,
	float* out_float_data, int outCap, int& outN);
void ConvertFloatToRawBytes(const float* float_data, int nFloat,
	uint16_t target_bits_per_sample, uint16_t channels,
	uint8_t* out_raw_data, int outCap, int& outN);

BYTE bufkpil[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];
BYTE bufkpim[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];


extern IGraphBuilder* pGraphBuilder;
extern IMediaControl* pMediaControl;
extern IMediaSeeking* pMediaSeeking;
extern double TempoPlaybackRateFromPos(int tempoPos);

extern CString wavExportPath;
extern int wavExportLoopCount;
extern float g_wavExportMaxSec;
extern int g_wavExportCancel;
extern int wavbit_sample_Hz;
extern CFile cc;
extern int cc1;
extern int loopcnt;
extern ULONG WAVDALen;
bool PlaybackCcFormatLocked();
void PlaybackCcGetFormat(int& rate, int& ch, int& bits);
#define OUTPUT_BUFFER_NUM_DS 5

void equaliser(void* data, int len, BOOL reset = FALSE);
void equaliserBank(int bank, void* data, int len, BOOL reset);
void equaliserBank(int bank, void* data, int len, BOOL reset, int bitsOverride, int chOverride, int rateOverride);
void equaliserResetBank(int bank);
#include <mutex>
std::mutex cl2;  // OnHScroll(シーク)とHandleNotifications(再生)の排他用。一本で統一。
// DS Lock/Unlock 実行中(cl2 外)。UI の GetCurrentPosition が同一デバイスで固まるのを避ける。
volatile LONG g_dsDeviceOpBusy = 0;
static CRITICAL_SECTION s_dsOpCs;
static volatile LONG s_dsOpCsInit = 0;

static void DsOpCsEnsure()
{
	if (InterlockedCompareExchange(&s_dsOpCsInit, 1, 0) == 0)
		InitializeCriticalSectionAndSpinCount(&s_dsOpCs, 4000);
}

void DsOpEnter()
{
	DsOpCsEnsure();
	EnterCriticalSection(&s_dsOpCs);
}

void DsOpLeave()
{
	LeaveCriticalSection(&s_dsOpCs);
}

BOOL DsOpTryEnter()
{
	DsOpCsEnsure();
	return TryEnterCriticalSection(&s_dsOpCs) ? TRUE : FALSE;
}

BOOL syoriflg;

// 再生通知スレッド: thn==TRUE は「ループが終了シグナルを出した」だけでスレッド本体はまだ動くことがある。
// stop/stop1 から Closeds() やデコーダ解放の前に必ず Join する。
static CWinThread* s_playNotifyThread = nullptr;
static CCriticalSection s_playNotifyThreadCs;

// stop()/BeginPlaybackNotifyThread が Join する上限。曲切替時は短く(UI 応答性)。
DWORD g_playbackNotifyJoinTimeoutMs = 2500;
volatile LONG g_interactiveTrackChange = 0;
volatile LONG g_appExiting = 0;
volatile LONG g_inPlaybackJoinPump = 0;
extern DWORD g_oggUiThreadId;
static void HandleFillWakeAll();

#ifndef PM_QS_SENDMESSAGE
#define PM_QS_SENDMESSAGE (QS_SENDMESSAGE << 16)
#endif

/* Join 中に og の WM_TIMER(9000=10ms 次曲) や PAINT/ULW を Dispatch すると
   play() が入れ子になり、Fill の SendMessage と三者待ちで戻らない。
   KPI/VST の hidden 宛 Post と sent だけ通す。 */
static BOOL OggJoinIsAppUiHwnd(HWND h)
{
	if (!h || !og)
		return FALSE;
	HWND o = og->GetSafeHwnd();
	if (!o)
		return FALSE;
	if (h == o || ::IsChild(o, h))
		return TRUE;
	HWND root = ::GetAncestor(h, GA_ROOT);
	return (root == o);
}

static BOOL OggJoinDropPosted(const MSG& msg)
{
	switch (msg.message) {
	case WM_TIMER:
	case WM_PAINT:
	case WM_ERASEBKGND:
	case WM_NCPAINT:
	case WM_SYNCPAINT:
		return OggJoinIsAppUiHwnd(msg.hwnd);
	case WM_TIMERP_VSYNC_TICK:
	case WM_SPEANA_TICK:
	case WM_ENDPOINT_VOLUME:
	case WM_PLAYBACK_AUTO_STOPPED:
	case WM_OGG_RESUME_PROMPT:
	case WM_OGG_CLOSE_DOUGA:
	case WM_OGG_ENTER_MP_MODE:
	case WM_OGG_TOGGLE_SUBUI:
	case WM_OGG_DEFERRED_HEAVY_INIT:
	case WM_OGG_S3_PLAYBACK:
	case WM_COMMAND:
	case WM_SYSCOMMAND:
	case WM_HSCROLL:
	case WM_VSCROLL:
	case WM_CLOSE:
		return TRUE;
	default:
		break;
	}
	if (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST)
		return TRUE;
	if (msg.message >= WM_NCMOUSEMOVE && msg.message <= WM_NCMBUTTONDBLCLK)
		return TRUE;
	if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST)
		return TRUE;
	return FALSE;
}

/* Join 中に Dispatch すると play() が入れ子になるので今は処理しない。
   捨てると、停止中のダブルクリックや途中再生の確認が二度と届かない。
   待ちが終わってから同じ順で戻す。 */
static BOOL OggJoinKeepForLater(const MSG& msg)
{
	switch (msg.message) {
	case WM_COMMAND:
	case WM_OGG_RESUME_PROMPT:
		return TRUE;
	default:
		break;
	}
	if (msg.message == WM_LBUTTONDOWN || msg.message == WM_LBUTTONUP
		|| msg.message == WM_LBUTTONDBLCLK
		|| msg.message == WM_RBUTTONDOWN || msg.message == WM_RBUTTONUP
		|| msg.message == WM_RBUTTONDBLCLK
		|| msg.message == WM_MBUTTONDOWN || msg.message == WM_MBUTTONUP
		|| msg.message == WM_MBUTTONDBLCLK)
		return TRUE;
	if (msg.message >= WM_NCLBUTTONDOWN && msg.message <= WM_NCMBUTTONDBLCLK)
		return TRUE;
	if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST)
		return TRUE;
	return FALSE;
}

/* WaitForSingleObject は SendMessage を捌かない。KPI/VST/COM は hidden window
   への Post 待ちもあるので、Join 中の UI は sent + posted を回す。
   WAIT_FAILED を成功扱いにすると生存スレッドのまま Closeds して固まる。 */
BOOL UiWaitHandlePumpSent(HANDLE h, DWORD timeoutMs)
{
	if (!h)
		return TRUE;
	const BOOL onUi = (g_oggUiThreadId != 0 && GetCurrentThreadId() == g_oggUiThreadId);
	if (!onUi) {
		const DWORD t = (timeoutMs == 0) ? INFINITE : timeoutMs;
		const DWORD wr = WaitForSingleObject(h, t);
		return (wr == WAIT_OBJECT_0) ? TRUE : FALSE;
	}

	InterlockedIncrement(&g_inPlaybackJoinPump);
	struct DecPump { ~DecPump() { InterlockedDecrement(&g_inPlaybackJoinPump); } } dec;
	struct JoinKept {
		HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam;
	};
	struct JoinKeepInput {
		JoinKept item[64];
		int n;
		JoinKeepInput() : n(0) {}
		~JoinKeepInput() {
			bool promptKept = false;
			bool promptSent = false;
			for (int i = 0; i < n; ++i) {
				if (item[i].message == WM_OGG_RESUME_PROMPT)
					promptKept = true;
				if (!item[i].hwnd || !::IsWindow(item[i].hwnd))
					continue;
				if (::PostMessage(item[i].hwnd, item[i].message, item[i].wParam, item[i].lParam)
					&& item[i].message == WM_OGG_RESUME_PROMPT)
					promptSent = true;
			}
			if (promptKept && !promptSent)
				OggNoteResumePromptDropped();
		}
		void push(const MSG& msg) {
			if (n >= 64)
				return;
			item[n].hwnd = msg.hwnd;
			item[n].message = msg.message;
			item[n].wParam = msg.wParam;
			item[n].lParam = msg.lParam;
			++n;
		}
	} keep;

	const DWORD t0 = GetTickCount();
	for (;;) {
		const DWORD wr0 = WaitForSingleObject(h, 0);
		if (wr0 == WAIT_OBJECT_0)
			return TRUE;
		if (wr0 == WAIT_FAILED)
			return FALSE;
		DWORD slice = 15;
		if (timeoutMs != 0) {
			const DWORD el = GetTickCount() - t0;
			if (el >= timeoutMs)
				return FALSE;
			const DWORD remain = timeoutMs - el;
			if (slice > remain)
				slice = remain;
			if (slice < 1)
				slice = 1;
		}
		const DWORD w = MsgWaitForMultipleObjectsEx(1, &h, slice,
			QS_SENDMESSAGE | QS_POSTMESSAGE, 0);
		if (w == WAIT_OBJECT_0)
			return TRUE;
		if (w == WAIT_FAILED)
			return FALSE;
		MSG msg;
		PeekMessage(&msg, NULL, WM_NULL, WM_NULL, PM_NOREMOVE);
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				::PostQuitMessage((int)msg.wParam);
				return (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) ? TRUE : FALSE;
			}
			if (msg.message == WM_TIMERP_VSYNC_TICK || msg.message == WM_SPEANA_TICK)
				COgg_DropPlaybackUiPostedMsg(msg.message);
			if (OggJoinDropPosted(msg)) {
				if (OggJoinKeepForLater(msg))
					keep.push(msg);
				continue;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0)
				return TRUE;
		}
		HandleFillWakeAll();
		if (og)
			og->timer.SetEvent();
	}
}

/* -------------------------------------------------------------------------
 * HandleFillNotifications
 *   HandleNotifications が担っていた「デコード / CEmu Render」を別スレッドへ。
 *   DS 待ち(WaitForMultipleObjects)の裏で次チャンクを積み、通知スレッドは
 *   ステージ済み PCM を Lock するだけにする。
 *   playwav* / CEmuSessionRender はすべてこの fill スレッドから呼ぶ
 *   （KPI 2SF の JIT はスレッドを跨ぐと落ちるので、途中で HN 側へ戻さない）。
 * ------------------------------------------------------------------------- */
enum { kFillBlkMax = 8 };
enum { kFillBlkCap = 64 * 1024 };
static BYTE s_fillBlk[kFillBlkMax][kFillBlkCap];
static int s_fillBlkBytes[kFillBlkMax];
static int s_fillBlkReadme[kFillBlkMax]; /* 0=終端なし。>0 ならこの塊の有効 PCM バイト */
static int s_fillHead = 0, s_fillTail = 0, s_fillN = 0;
static int s_fillOff = 0; /* 先頭ブロックの消費オフセット */
static std::mutex s_fillCs;
static HANDLE s_fillWake = NULL;    /* 空きができた / 停止 */
static HANDLE s_fillHasData = NULL; /* 1 塊積んだ */
static LONG s_fillStop = 0;
static LONG s_fillEpoch = 0;        /* シークで捨てる世代 */
static LONG s_fillExitReq = 0;
static LONG s_fillInDecode = 0;
static CWinThread* s_fillThread = nullptr;

static void HandleFillWakeAll()
{
	if (s_fillWake)
		SetEvent(s_fillWake);
	if (s_fillHasData)
		SetEvent(s_fillHasData);
}

/* HN 起動直後に thn1 を下ろすと、Resume と Exit の競合で停止要求が消える。
   手動リセットの abort は BeginPlayback だけが下ろす。 */
static HANDLE s_playAbortEvent = NULL;

static void PlayAbortEnsure()
{
	if (!s_playAbortEvent)
		s_playAbortEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

static BOOL PlayAbortIsSet()
{
	if (thn1 || stf != 0 || syukai == 2)
		return TRUE;
	if (InterlockedCompareExchange(&g_appExiting, 0, 0))
		return TRUE;
	if (s_playAbortEvent && WaitForSingleObject(s_playAbortEvent, 0) == WAIT_OBJECT_0)
		return TRUE;
	return FALSE;
}

static void PlayAbortSignal()
{
	PlayAbortEnsure();
	if (s_playAbortEvent)
		SetEvent(s_playAbortEvent);
}

static void PlayAbortArmForRun()
{
	PlayAbortEnsure();
	if (s_playAbortEvent)
		ResetEvent(s_playAbortEvent);
}

static void HandleFillWaitOne(HANDLE extra, DWORD ms)
{
	HANDLE h[2];
	int n = 0;
	if (extra)
		h[n++] = extra;
	PlayAbortEnsure();
	if (s_playAbortEvent)
		h[n++] = s_playAbortEvent;
	if (n <= 0) {
		Sleep(ms);
		return;
	}
	WaitForMultipleObjects(n, h, FALSE, ms);
}

static void HandleFillQueueResetLocked()
{
	s_fillN = 0;
	s_fillHead = 0;
	s_fillTail = 0;
	s_fillOff = 0;
}

static void HandleFillFlush()
{
	InterlockedIncrement(&s_fillEpoch);
	{
		std::lock_guard<std::mutex> lk(s_fillCs);
		HandleFillQueueResetLocked();
	}
	HandleFillWakeAll();
}

static int HandleFillQueuedBytes()
{
	std::lock_guard<std::mutex> lk(s_fillCs);
	int n = 0;
	if (s_fillN <= 0)
		return 0;
	n = s_fillBlkBytes[s_fillHead] - s_fillOff;
	for (int i = 1; i < s_fillN; ++i) {
		const int idx = (s_fillHead + i) % kFillBlkMax;
		n += s_fillBlkBytes[idx];
	}
	return n;
}

/* 1 塊のデコード。cl2 保持中に呼ぶ。dest は線形（旧 bufwav3 ラップをやめる）。 */
static int HandleFillDecodeOne(BYTE* dest, int n, int* pReadme, bool* pExitNow)
{
	if (pReadme)
		*pReadme = 0;
	if (pExitNow)
		*pExitNow = false;
	if (!dest || n <= 0)
		return 0;

	static int s_fade2 = 0;

	if (thn1 || InterlockedCompareExchange(&s_fillStop, 0, 0)
		|| InterlockedCompareExchange(&g_appExiting, 0, 0)) {
		if (pExitNow)
			*pExitNow = true;
		return 0;
	}

	sflg = TRUE;
	/* 混合開始はこのサイクル境界で行う。UI タイマ経由にすると
	 * ジャケ読み込み等で UI が詰まった分だけ開始が遅れ、その遅れ量が
	 * そのままクロス末尾と B の頭のずれになる。 */
	if (!InterlockedCompareExchange(&g_xfInProgress, 0, 0)
		&& InterlockedCompareExchange(&g_xfPrepared, 0, 0)) {
		extern ULONGLONG g_seekUiHoldUntil;
		extern BOOL sek;
		extern int sek4;
		if ((g_seekUiHoldUntil != 0 && GetTickCount64() < g_seekUiHoldUntil)
			|| sek || sek4) {
			/* シーク直後は FIFO が空。ここで混ぜると入り無音になる */
		}
		else {
		const __int64 fadeEnd = XfFadeEndRefBytes(g_endWrittenBytes);
		const __int64 xfWin = XfCrossfadeWindowBytes();
		const __int64 playPos = XfPlayPosBytes();
		/* 終端 − 設定秒に達したら混ぜる。その点を過ぎてから B が開いても
		 * 始めないと、準備済みのまま次曲へも行かず止まる。 */
		const int inWin = (fadeEnd > 0 && xfWin > 0
			&& playPos + xfWin >= fadeEnd);
		if (inWin || endflg || XfSongClockNearEnd(0.0)
			|| XfShouldStartEarly(g_heardBytes, g_endWrittenBytes) || XfRenzokuLimitReached()) {
			const int cur = XfActiveSlot();
			const int oth = XfOtherSlot(cur);
			if (g_openDecoderModeSlot[oth] != INT_MIN) {
				XfBeginMixLocked(cur);
			}
		}
		}
	}
	if (m_dsb) {
		if (InterlockedCompareExchange(&g_xfInProgress, 0, 0)) {
			int mixDoneEarly = (g_xfFadeTotalFrames > 0 && g_xfFadePos >= g_xfFadeTotalFrames);
			if (!mixDoneEarly && g_xfMixStartTick != 0) {
				DWORD lim = (DWORD)(XfSecFromSave() * 1000.0 + 800.0);
				if (lim < 1500)
					lim = 1500;
				if (GetTickCount() - g_xfMixStartTick >= lim)
					mixDoneEarly = 1;
			}
			if (mixDoneEarly)
				XfOnCrossfadeFinished();
		}
		if (InterlockedCompareExchange(&g_xfInProgress, 0, 0)) {
			const int aSlot = XfActiveSlot();
			const int bSlot = (int)InterlockedCompareExchange(&g_xfSecSlot, 0, 0);
			const int total = n;
			static BYTE s_xfA[512 * 1024];
			static BYTE s_xfB[512 * 1024];
			static BYTE s_xfMix[512 * 1024];
			const int cap = (int)sizeof(s_xfA);
			const int nn = (total > cap) ? cap : total;
			ZeroMemory(s_xfA, nn);
			ZeroMemory(s_xfB, nn);
			ZeroMemory(s_xfMix, nn);
			extern int g_pcm_upscale_active;
			/* B を先に取る。A の壊れた末尾フレームでフィルが止まっても、
			 * 次曲の PCM はもう s_xfB にある。 */
			InterlockedExchange(&g_xfFillSlot, bSlot);
			XfLoadSlotDecodeState(bSlot);
			XfApplySlotFormatToGlobals(bSlot);
			if (!XfMixAttachDecoder(bSlot)) {
				ZeroMemory(s_xfB, nn);
			}
			else {
				XfBindSlotUpscalerToSession(bSlot);
				g_pcm_upscale_active = g_audioUpscalerArr[bSlot].IsActive() ? 1 : 0;
				fade1 = 0;
				endflg = 0;
				fade = 1.0f;
				fadeadd = 0.0f;
				XfDispatchMixFill(s_xfB, nn);
			}
			fade1 = 0;
			endflg = 0;
			fade = 1.0f;
			fadeadd = 0.0f;
			XfSaveSlotDecodeState(bSlot);
			if (thn1 || InterlockedCompareExchange(&s_fillStop, 0, 0)
				|| InterlockedCompareExchange(&g_appExiting, 0, 0)) {
				InterlockedExchange(&g_xfFillSlot, -1);
				XfLoadSlotDecodeState(aSlot);
				XfApplySlotFormatToGlobals(aSlot);
				if (pExitNow)
					*pExitNow = true;
				sflg = FALSE;
				return 0;
			}
			/* A: スロット配列→作業用にロードしてからデコード */
			InterlockedExchange(&g_xfFillSlot, aSlot);
			XfLoadSlotDecodeState(aSlot);
			XfApplySlotFormatToGlobals(aSlot);
			if (!XfMixAttachDecoder(aSlot)) {
				ZeroMemory(s_xfA, nn);
			}
			else {
				XfBindSlotUpscalerToSession(aSlot);
				g_pcm_upscale_active = g_audioUpscalerArr[aSlot].IsActive() ? 1 : 0;
				/* スロットに残った fade1 で PCM を 0 にしない。等パワー側で下げる。 */
				fade1 = 0;
				endflg = 0;
				fade = 1.0f;
				fadeadd = 0.0f;
				XfDispatchMixFill(s_xfA, nn);
			}
			fade1 = 0;
			endflg = 0;
			fade = 1.0f;
			fadeadd = 0.0f;
			XfSaveSlotDecodeState(aSlot);
			/* 作業用を本流 A に戻す */
			InterlockedExchange(&g_xfFillSlot, -1);
			XfLoadSlotDecodeState(aSlot);
			XfApplySlotFormatToGlobals(aSlot);
			g_pcm_upscale_active = g_audioUpscalerArr[aSlot].IsActive() ? 1 : 0;
			const int bits = (g_ds_pcm_bits >= 8) ? g_ds_pcm_bits : 16;
			const int ch = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : 2;
			const int bpfMix = (bits / 8) * ch;
			int nMix = nn;
			if (bpfMix > 1)
				nMix -= (nMix % bpfMix);
			if (nMix > 0)
				XfMixEqualPower(s_xfMix, s_xfA, s_xfB, nMix, bits, ch);
			/* 端数は A を残す（未初期化/無音クリック防止） */
			if (nMix < nn)
				memcpy(s_xfMix + nMix, s_xfA + nMix, (size_t)(nn - nMix));
			memcpy(dest, s_xfMix, (size_t)nn);
			if (nn < n)
				ZeroMemory(dest + nn, (size_t)(n - nn));
			int mixDone = (g_xfFadeTotalFrames > 0 && g_xfFadePos >= g_xfFadeTotalFrames);
			if (!mixDone && g_xfMixStartTick != 0) {
				DWORD lim = (DWORD)(XfSecFromSave() * 1000.0 + 800.0);
				if (lim < 1500)
					lim = 1500;
				if (GetTickCount() - g_xfMixStartTick >= lim)
					mixDone = 1;
			}
			if (mixDone)
				XfOnCrossfadeFinished();
		}
		else {
			/* CEmu / KPI / 通常デコード。線形なので old=0, l2=0。 */
			DispatchPlaywavFill(dest, 0, n, 0);
		}
	}
	else {
		ZeroMemory(dest, (size_t)n);
	}

	const int readmeThis = readme;
	if (pReadme)
		*pReadme = readmeThis;
	/* 線形チャンクの余りを無音にする（旧ラップ式 ZeroMemory の置き）。 */
	if (readmeThis > 0 && readmeThis < n
		&& !InterlockedCompareExchange(&g_xfInProgress, 0, 0))
		ZeroMemory(dest + readmeThis, (size_t)(n - readmeThis));

	if (thn1) {
		if (pExitNow)
			*pExitNow = true;
		sflg = FALSE;
		return 0;
	}

	readme = 0;
	s_fade2 = fade1;
	if (flg3 != 0)
		flg3--;
	sflg = FALSE;
	return n;
}

static int HandleFillChunkBytes()
{
	int bpf = (g_outBytesPerFrame > 0) ? g_outBytesPerFrame : 4;
	if (bpf < 1)
		bpf = 4;
	int hz = (g_ds_pcm_rate > 0) ? g_ds_pcm_rate : ((wavbit_sample_Hz > 0) ? wavbit_sample_Hz : 44100);
	int ms = (savedata.ms > 0) ? (int)savedata.ms : 10;
	int n = (hz / 1000) * ms * bpf;
	if (n < bpf * 64)
		n = bpf * 64;
	if (n > kFillBlkCap)
		n = kFillBlkCap;
	n -= n % bpf;
	if (n < bpf)
		n = bpf;
	return n;
}

UINT HandleFillNotifications(LPVOID)
{
	BYTE tmp[kFillBlkCap];
	for (;;) {
		if (InterlockedCompareExchange(&s_fillStop, 0, 0)
			|| thn1 || syukai == 2
			|| InterlockedCompareExchange(&g_appExiting, 0, 0)
			|| PlayAbortIsSet())
			break;
		if (sek || sek4 || ps == 1) {
			HandleFillWaitOne(s_fillWake, 8);
			continue;
		}
		const int chunk = HandleFillChunkBytes();
		int queued = HandleFillQueuedBytes();
		/* 先読みは 3 塊まで。DS 待ちの裏に乗る分だけで、シーク捨て量を抑える。 */
		if (queued >= chunk * 3 || s_fillN >= kFillBlkMax - 1) {
			HandleFillWaitOne(s_fillWake, 8);
			continue;
		}

		const LONG epoch = InterlockedCompareExchange(&s_fillEpoch, 0, 0);
		int readmeBlk = 0;
		bool exitNow = false;
		int got = 0;
		/* デコード中に cl2 を持つと、KPI/VST の SendMessage → UI Join が cl2 待ちで戻らない */
		InterlockedExchange(&s_fillInDecode, 1);
		got = HandleFillDecodeOne(tmp, chunk, &readmeBlk, &exitNow);
		InterlockedExchange(&s_fillInDecode, 0);
		if (exitNow) {
			InterlockedExchange(&s_fillExitReq, 1);
			HandleFillWakeAll();
			break;
		}
		if (got <= 0) {
			HandleFillWaitOne(s_fillWake, 4);
			continue;
		}
		if (InterlockedCompareExchange(&s_fillEpoch, 0, 0) != epoch) {
			/* シーク中にデコードした塊は捨てる */
			continue;
		}

		{
			std::lock_guard<std::mutex> lk(s_fillCs);
			if (s_fillN >= kFillBlkMax) {
				/* 満杯。次周回で待つ */
			}
			else {
				const int idx = s_fillTail;
				if (got > kFillBlkCap)
					got = kFillBlkCap;
				memcpy(s_fillBlk[idx], tmp, (size_t)got);
				s_fillBlkBytes[idx] = got;
				s_fillBlkReadme[idx] = readmeBlk;
				s_fillTail = (s_fillTail + 1) % kFillBlkMax;
				s_fillN++;
			}
		}
		if (s_fillHasData)
			SetEvent(s_fillHasData);
	}
	return 0;
}

int PlaybackFillThreadAlive()
{
	if (!s_fillThread || !s_fillThread->m_hThread)
		return 0;
	return (WaitForSingleObject(s_fillThread->m_hThread, 0) == WAIT_OBJECT_0) ? 0 : 1;
}

int PlaybackFillInDecode()
{
	return InterlockedCompareExchange(&s_fillInDecode, 0, 0) != 0 ? 1 : 0;
}

void PlaybackFillWake()
{
	HandleFillWakeAll();
	if (og)
		og->timer.SetEvent();
}

static int HandleFillTake(BYTE* dest, int need, int* pReadme, bool* pExitNow)
{
	if (pReadme)
		*pReadme = 0;
	if (pExitNow)
		*pExitNow = false;
	if (!dest || need <= 0)
		return 0;

	int copied = 0;
	int readmeOut = 0;
	const DWORD t0 = GetTickCount();
	while (copied < need) {
		if (thn1 || sek || InterlockedCompareExchange(&s_fillStop, 0, 0) || PlayAbortIsSet())
			break;
		if (InterlockedCompareExchange(&s_fillExitReq, 0, 0)) {
			if (pExitNow)
				*pExitNow = true;
			break;
		}
		int take = 0;
		{
			std::lock_guard<std::mutex> lk(s_fillCs);
			if (s_fillN > 0) {
				const int idx = s_fillHead;
				const int avail = s_fillBlkBytes[idx] - s_fillOff;
				take = avail;
				if (take > need - copied)
					take = need - copied;
				if (take > 0) {
					memcpy(dest + copied, s_fillBlk[idx] + s_fillOff, (size_t)take);
					/* この塊に終端が乗っていて、今回その境界を跨いだら readme を合成する */
					const int rm = s_fillBlkReadme[idx];
					if (rm > 0 && readmeOut == 0) {
						if (s_fillOff < rm && s_fillOff + take >= rm)
							readmeOut = copied + (rm - s_fillOff);
						else if (s_fillOff >= rm)
							;
						else if (s_fillOff + take <= rm && copied + take == need)
							readmeOut = 0; /* まだ終端の前 */
					}
					s_fillOff += take;
					copied += take;
					if (s_fillOff >= s_fillBlkBytes[idx]) {
						s_fillOff = 0;
						s_fillHead = (s_fillHead + 1) % kFillBlkMax;
						s_fillN--;
					}
				}
			}
		}
		if (take > 0) {
			if (s_fillWake)
				SetEvent(s_fillWake);
			continue;
		}
		if ((DWORD)(GetTickCount() - t0) > 5000u)
			break;
		HandleFillWaitOne(s_fillHasData, 10);
	}
	if (pReadme)
		*pReadme = readmeOut;
	return copied;
}

static int HandleFillIsRunning()
{
	if (!s_fillThread || !s_fillThread->m_hThread)
		return 0;
	if (WaitForSingleObject(s_fillThread->m_hThread, 0) == WAIT_OBJECT_0)
		return 0;
	return 1;
}

static void HandleFillRequestStop()
{
	InterlockedExchange(&s_fillStop, 1);
	HandleFillWakeAll();
}

static void HandleFillCloseEvents()
{
	if (s_fillWake) {
		CloseHandle(s_fillWake);
		s_fillWake = NULL;
	}
	if (s_fillHasData) {
		CloseHandle(s_fillHasData);
		s_fillHasData = NULL;
	}
}

static BOOL HandleFillThreadExited()
{
	if (!s_fillThread)
		return TRUE;
	HANDLE h = s_fillThread->m_hThread;
	if (!h)
		return TRUE;
	const DWORD w = WaitForSingleObject(h, 0);
	return (w == WAIT_OBJECT_0 || w == WAIT_FAILED) ? TRUE : FALSE;
}

static void HandleFillCleanupIfExited()
{
	if (!HandleFillThreadExited())
		return;
	if (s_fillThread) {
		delete s_fillThread;
		s_fillThread = nullptr;
	}
	{
		std::lock_guard<std::mutex> lk(s_fillCs);
		HandleFillQueueResetLocked();
	}
	HandleFillCloseEvents();
}

/* HN の FillGuard からは待たない。UI が Fill を待っている最中に HN が Fill を待つと、
   Fill の SendMessage → UI Join → HN 待ち で三者が止まる。 */
static void HandleFillStop()
{
	HandleFillRequestStop();
	if (s_fillThread && s_fillThread->m_hThread)
		WaitForSingleObject(s_fillThread->m_hThread, 30000);
	HandleFillCleanupIfExited();
}

static void HandleFillStart()
{
	if (PlayAbortIsSet())
		return;
	HandleFillStop();
	if (PlayAbortIsSet())
		return;
	InterlockedExchange(&s_fillStop, 0);
	InterlockedExchange(&s_fillExitReq, 0);
	InterlockedExchange(&s_fillEpoch, 1);
	s_fillWake = CreateEvent(NULL, FALSE, FALSE, NULL);
	s_fillHasData = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!s_fillWake || !s_fillHasData) {
		HandleFillStop();
		return;
	}
	/* DeSmuME / CEmu 系はスタックを大きく予約する（HN と同じ）。 */
	CWinThread* t = AfxBeginThread((AFX_THREADPROC)HandleFillNotifications, NULL,
		THREAD_PRIORITY_TIME_CRITICAL, 64 * 1024 * 1024,
		CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION);
	if (!t) {
		HandleFillStop();
		return;
	}
	t->m_bAutoDelete = FALSE;
	s_fillThread = t;
	t->ResumeThread();
}

void SignalPlaybackNotifyThreadStop()
{
	thn1 = TRUE;
	stf = 1;
	syukai = 2;
	sek4 = FALSE;
	sflg = FALSE;
	// OnHScroll が syukai2==1 を待っているとき stop で syukai=2 にすると
	// 再生スレッドは syukai2 を立てずに終了するため、ここで必ず解放する。
	syukai2 = 1;
	InterlockedExchange(&g_dsDeviceOpBusy, 0);
	InterlockedExchange(&s_fillStop, 1);
	PlayAbortSignal();
	HandleFillWakeAll();
	if (og)
		og->timer.SetEvent();
}

// 戻り値: 再生スレッドが確実に終了したとき TRUE。FALSE のときデコーダを閉じてはならない。
// デコード側の SendMessage を捌く（WaitForSingleObject だけだと相互待ちで終了が戻らない）。
BOOL WaitForPlaybackNotifyThreadExit(DWORD timeoutMs)
{
	HandleFillRequestStop();
	thn1 = TRUE;
	stf = 1;
	syukai = 2;
	PlayAbortSignal();
	if (og)
		og->timer.SetEvent();
	HandleFillWakeAll();

	HANDLE hFill = (s_fillThread && s_fillThread->m_hThread) ? s_fillThread->m_hThread : NULL;
	HANDLE hThread = NULL;
	{
		CSingleLock lk(&s_playNotifyThreadCs, TRUE);
		if (s_playNotifyThread && s_playNotifyThread->m_hThread)
			hThread = s_playNotifyThread->m_hThread;
	}

	BOOL fillOk = TRUE;
	if (hFill)
		fillOk = UiWaitHandlePumpSent(hFill, timeoutMs);
	if (fillOk)
		HandleFillCleanupIfExited();

	BOOL exited = TRUE;
	if (hThread)
		exited = UiWaitHandlePumpSent(hThread, timeoutMs);

	if (exited) {
		CSingleLock lk(&s_playNotifyThreadCs, TRUE);
		if (s_playNotifyThread) {
			HANDLE ht = s_playNotifyThread->m_hThread;
			if (!hThread || !ht || WaitForSingleObject(ht, 0) == WAIT_OBJECT_0) {
				delete s_playNotifyThread;
				s_playNotifyThread = nullptr;
			} else {
				exited = FALSE;
			}
		}
		if (exited) {
			thn = TRUE;
			syukai = 0;
			syukai2 = 1;
		}
	}
	return (fillOk && exited) ? TRUE : FALSE;
}

void KillPlaybackNotifyThread()
{
	WaitForPlaybackNotifyThreadExit(0);
}

extern int g_openDecoderMode;

void BeginPlaybackNotifyThread()
{
	// 旧スレッドが残っている間は新スレッドを立てない（ポインタ上書きで Join 不能になる）
	const DWORD joinTimeout = g_interactiveTrackChange
		? (g_playbackNotifyJoinTimeoutMs ? g_playbackNotifyJoinTimeoutMs : 2500u)
		: 0u;
	if (!WaitForPlaybackNotifyThreadExit(joinTimeout))
		return;
	// この時点の mode が「実際に再生するデコーダ形式」（stop1 はこれを見て閉じる）
	g_openDecoderMode = mode;
	// Wait/Signal や play 中 DoEvent 再入で残った停止フラグを下ろす。
	// 残ったままだと通知スレッドが即終了し、CWread(adbuf) 系が無音になる
	// （HandleNotifications_export と同じ理由）。
	PlayAbortArmForRun();
	thn1 = FALSE;
	stf = 0;
	syukai = 0;
	syukai2 = 0;
	/* 前曲終端の fade1 が残ると、モニタは動いても PCM が 0 のままになる。 */
	fade1 = 0;
	endflg = 0;
	g_endWrittenBytes = 0;
	// CREATE_SUSPENDED で起動し、スレッド本体が走り出す前に m_bAutoDelete を
	// 落として寿命を自前管理する。これによりスレッドが自己終了しても
	// CWinThread オブジェクトとスレッドハンドルは破棄されず、安全に Join できる。
	// Resume 前に s_playNotifyThread へ登録し、起動直後の stop が「スレッド無し」と
	// 誤認してデコーダを潰し、再生スレッドが Lock で永久待ち→連続固まりになるのを防ぐ。
	// kbvio2sf / DeSmuME 系は Render がスタックを大量に使う。
	// STACK_SIZE_PARAM_IS_A_RESERVATION 無しだと nStackSize は「初期 Commit」扱いになり、
	// 64MB Commit 要求で CreateThread が失敗したり VA を圧迫する。予約のみ大きくする。
	CWinThread* t = AfxBeginThread((AFX_THREADPROC)HandleNotifications, NULL,
		THREAD_PRIORITY_TIME_CRITICAL, 64 * 1024 * 1024,
		CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION);
	if (!t)
		return;
	t->m_bAutoDelete = FALSE;
	{
		CSingleLock lk(&s_playNotifyThreadCs, TRUE);
		s_playNotifyThread = t;
	}
	t->ResumeThread();
}

UINT HandleNotifications(LPVOID)
{
	const LONG playGen = g_playGeneration;
	readme = 0;
	int fade2 = 0;
	syoriflg = FALSE;
	DWORD hr = DS_OK;
	/* 停止要求は BeginPlaybackNotifyThread が下ろす。ここで thn1/stf/syukai を
	   下ろすと Resume 直後の終了が消え、Join が終わらない。 */
	if (PlayAbortIsSet())
		return 0;
	thn = FALSE;
	syukai2 = 0;
	char* pdsb1; char* pdsb2;
	int dougainit = 0;
	int timeee = 0;
	PlayAbortEnsure();
	HANDLE ev[2];
	ev[0] = (HANDLE)og->timer;
	int nev = 1;
	if (s_playAbortEvent) {
		ev[1] = s_playAbortEvent;
		nev = 2;
	}
	ULONG PlayCursor, WriteCursor = 0, len3, len4;

	auto isPlausibleDsb = [](LPDIRECTSOUNDBUFFER8 p) -> bool {
		if (!p)
			return false;
		const ULONG_PTR v = reinterpret_cast<ULONG_PTR>(p);
		// ヒープ破壊で m_dsb が 0x36 等のゴミになると if(p) を通過して AV するため
		return v >= 0x10000 && (v % sizeof(void*)) == 0;
	};

	const ULONG prefilledOldw = oldw; // play() がプリフィル後に進めた値
	ULONG prefillProtectUntil = 0;
	int prefillRestFilled = 0;
	oldw = 0;
	if (isPlausibleDsb(m_dsb)) {
		DsOpLock ds;
		if (isPlausibleDsb(m_dsb)) {
			DWORD st = 0;
			if (m_dsb->GetStatus(&st) == DS_OK && (st & DSBSTATUS_BUFFERLOST))
				m_dsb->Restore();
			m_dsb->SetCurrentPosition(0);
			/* wav 側 Play が前曲 Stop と競合すると、Fill だけ進んで
			   スペアナ(PlayCursor)は止まりスピーカーは無音になる。 */
			if (!VstLiveThruIsOn() && savedata.dsvol != -498)
				m_dsb->SetVolume((savedata.dsvol - 1) * 10);
			m_dsb->Play(0, 0, DSBPLAY_LOOPING);
		}
	}
	// mode ではなく Open 中の形式（曲切替で mode が先に変わる）
	if (g_openDecoderMode == -10 || g_openDecoderMode == 999) {
		oldw = OUTPUT_BUFFER_SIZE * 2;
		og->timer.SetEvent();
	}
	else if (g_openDecoderMode == -7 && prefilledOldw > 0) {
		// DSD の長プリフィル済み区間を再デコードで潰さない。FLAC は KPI と同じ（oldw=0）。
		oldw = prefilledOldw;
		prefillProtectUntil = prefilledOldw;
	}
	fade1 = 0;
	sek4 = FALSE;

	/* DS 待ちの裏で CEmu/KPI を回す。失敗時は従来どおり HN 内デコード。 */
	HandleFillStart();
	struct FillGuard {
		~FillGuard() { HandleFillRequestStop(); }
	} fillGuard;

	auto stopPlaybackAndExit = [&]() -> UINT {
		/* Lock/Stop は UI の Closeds が Join 後にやる。ここで Stop すると
		   Join pump 側の DsOp と相互待ちになる。 */
		playf = 0;
		thn = TRUE;
		reset = TRUE;
		return 0;
	};

	for (;;) {
		// Wait 中は cl2 を取らない（OnHScroll 等がシークできる）

		// 曲ごとパラメータ: 曲頭で復元、再生中の変更をデバウンス保存
		SongParams_Sync(false);

		if (PlayAbortIsSet() || syukai == 2 || thn1) return stopPlaybackAndExit();
		if (syukai == 1) { syukai2 = 1; Sleep(1); continue; }

		// イベント待機（停止要求が来たら長く寝ない）
		const DWORD waitMs = PlayAbortIsSet() ? 10u : (DWORD)savedata.ms;
		::WaitForMultipleObjects(nev, ev, FALSE, waitMs);

		// FLAC等の重いシーク中（sek4）はロックせずに待機
		while (sek4) {
			if (PlayAbortIsSet() || syukai == 2 || thn1) return stopPlaybackAndExit();
			::WaitForMultipleObjects(nev, ev, FALSE, PlayAbortIsSet() ? 10u : (DWORD)savedata.ms);
		}

		if (sek == 1) {
			HandleFillFlush();
			sflg = TRUE; flg3 = 3; sek = FALSE; sflg = FALSE;
			// シーク直後は書き込み位置を再調整する必要があるため continue
			continue;
		}
		if (PlayAbortIsSet() || thn1) return stopPlaybackAndExit();
		if (ps == 1) continue;

		LPDIRECTSOUNDBUFFER8 dsb = m_dsb;
		if (!isPlausibleDsb(dsb))
			continue;

		// 書き込み位置の計算
		{
			DsOpLock ds;
			dsb = m_dsb;
			if (!isPlausibleDsb(dsb))
				continue;
			DWORD st = 0;
			if (!fade1 && dsb->GetStatus(&st) == DS_OK) {
				if (st & DSBSTATUS_BUFFERLOST)
					dsb->Restore();
				if (!(st & DSBSTATUS_PLAYING)) {
					if (!VstLiveThruIsOn() && savedata.dsvol != -498)
						dsb->SetVolume((savedata.dsvol - 1) * 10);
					dsb->Play(0, 0, DSBPLAY_LOOPING);
				}
			}
			dsb->GetCurrentPosition(&PlayCursor, &WriteCursor);
		}
		const ULONG ringBytes = (g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : (ULONG)(OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM);
		if (prefillProtectUntil > 0 && PlayCursor >= prefillProtectUntil) {
			prefillProtectUntil = 0;
			prefillRestFilled = 0;
			oldw = WriteCursor;
			continue;
		}
		if (prefillProtectUntil > 0 && prefillRestFilled && oldw < prefillProtectUntil)
			continue;
		int len1 = (int)WriteCursor - (int)oldw;
		int len2 = 0;

		if (len1 == 0) continue;
		if (len1 < 0) {
			// SetCurrentPosition(0) 直後は WriteCursor が先頭へ戻っただけ。
			// 先頭は触らず、プリフィル以降〜リング末だけ先に埋める。
			if (prefillProtectUntil > 0 && PlayCursor < prefillProtectUntil) {
				len1 = (int)ringBytes - (int)oldw;
				len2 = 0;
				prefillRestFilled = 1;
			}
			else {
				prefillProtectUntil = 0;
				len1 = (int)ringBytes - (int)oldw;
				len2 = (int)WriteCursor;
			}
		}
		/* DS 書込みカーソルは bpf 非整列になり得る（24bit 等）。部分フレームを混ぜると
		 * シーク位相によって数サンプルのクリックが xfade 混合時に出る。 */
		{
			const int bpfSnap = (g_outBytesPerFrame > 0) ? g_outBytesPerFrame : 4;
			if (bpfSnap > 1) {
				int total = len1 + len2;
				const int drop = total % bpfSnap;
				if (drop > 0 && total > drop) {
					total -= drop;
					if (len2 >= drop) {
						len2 -= drop;
					}
					else {
						const int from1 = drop - len2;
						len2 = 0;
						len1 -= from1;
						if (len1 < 0) len1 = 0;
					}
				}
				if (len1 + len2 <= 0) continue;
			}
		}

		// 終端ドレイン中か（実音声が終わった後の純無音サイクル）。終端確定後、書込みヘッドが
		// 終端位置を越えていれば、このサイクルはすべて無音で埋める（古いループ音の漏れ防止）。
		// クロスフェード中は A 終端でも混合 PCM を捨てない（無音化すると次曲も聞こえない）。
		const bool drainSilence = (!InterlockedCompareExchange(&g_xfInProgress, 0, 0)
			&& g_endWrittenBytes != 0 && g_dsWrittenBytes >= g_endWrittenBytes);

		// cl2 はデコード＋状態更新のみ。dsb->Lock はドライバ待ちで数秒固まることがあり、
		// その間 UI(timerp) が同じ cl2 で止まるのを避けるため、PCM をステージしてから Lock する。
		static BYTE s_dsStage[(10240 * 6 / 2) * 5 * 8];
		int stageBytes = 0;
		bool stageFade = false;
		int readmeThisCycle = 0;
		int writtenThisCycle = len1 + len2;
		bool exitAfterCl2 = false;

		/* 動画 Run は DirectShow 側。fill スレッドへ移さない（COM アパート）。
		   GetCheck は SendMessage。終了 Join 中に呼ばない。 */
		/* GetCheck は UI への SendMessage。UI がプラグイン読みで寝ていると
		   通知スレッドが戻らず、UI 側の Join / DS 待ちと相互に固まる。 */
		int douOn = 0;
		if (!PlayAbortIsSet() && og && ::IsWindow(og->m_dou.GetSafeHwnd())) {
			DWORD_PTR chk = 0;
			if (::SendMessageTimeoutW(og->m_dou.GetSafeHwnd(), BM_GETCHECK, 0, 0,
				SMTO_ABORTIFHUNG | SMTO_BLOCK, 15, &chk) != 0)
				douOn = (chk == BST_CHECKED) ? 1 : 0;
		}
		if (douOn && pGraphBuilder && pMediaControl) {
			/* 900ms はテンポ 100 のソース時刻。速いテンポでは壁時計を 1/rate にしないと動画が遅れて始まる。 */
			double rate = TempoPlaybackRateFromPos(tempo);
			if (rate < 0.05) rate = 0.05;
			const int waitMs = (int)(900.0 / rate + 0.5);
			if (timeee > waitMs && dougainit == 0) {
				if (pMediaSeeking) {
					double vr = rate;
					if (vr < 0.1) vr = 0.1;
					if (vr > 4.0) vr = 4.0;
					pMediaSeeking->SetRate(vr);
				}
				pMediaControl->Run();
				dougainit = 1;
			}
		}
		timeee += savedata.ms;

		stageBytes = writtenThisCycle;
		if (stageBytes > (int)sizeof(s_dsStage))
			stageBytes = (int)sizeof(s_dsStage);

		if (HandleFillIsRunning()) {
			/* デコードは Fill 側。ここは DS 空きに合わせて取り出すだけ。 */
			bool fillExit = false;
			int got = HandleFillTake(s_dsStage, stageBytes, &readmeThisCycle, &fillExit);
			if (sek == 1) {
				HandleFillFlush();
				sflg = TRUE; flg3 = 3; sek = FALSE; sflg = FALSE;
				continue;
			}
			if (fillExit || PlayAbortIsSet() || thn1)
				return stopPlaybackAndExit();
			/* 取り出し 0 を無音で埋めて Lock すると、Fill は進んでもスピーカーとスペアナが死ぬ。 */
			if (got <= 0)
				continue;
			stageBytes = got;
			if (len1 > stageBytes) {
				len1 = stageBytes;
				len2 = 0;
			} else if (len2 > 0 && len1 + len2 > stageBytes) {
				len2 = stageBytes - len1;
			}
			writtenThisCycle = len1 + len2;
			stageFade = (!InterlockedCompareExchange(&g_xfInProgress, 0, 0)
				&& drainSilence) ? true : false;
		}
		else {
			if (PlayAbortIsSet() || thn1) {
				exitAfterCl2 = true;
			}
			else {
				bool decExit = false;
				InterlockedExchange(&s_fillInDecode, 1);
				HandleFillDecodeOne(s_dsStage, stageBytes, &readmeThisCycle, &decExit);
				InterlockedExchange(&s_fillInDecode, 0);
				if (decExit)
					exitAfterCl2 = true;
				else {
					stageFade = (!InterlockedCompareExchange(&g_xfInProgress, 0, 0)
						&& drainSilence) ? true : false;
					fade2 = fade1;
				}
			}
		}
		if (exitAfterCl2)
			return stopPlaybackAndExit();

		/* Speana / アナライザ / ピアノロール / EQコードは bufwav3 を
		   DS リングの鏡として PlayCursor から読む。Fill は線形ステージなので、
		   Lock 前に従来どおりラップして戻す。ここが空だと棒もコードも止まる。 */
		if (stageBytes > 0 && stageBytes <= (int)sizeof(s_dsStage)) {
			if (len1 > 0)
				memcpy(bufwav3 + oldw, s_dsStage, (size_t)len1);
			if (len2 > 0)
				memcpy(bufwav3, s_dsStage + len1, (size_t)len2);
		}

		// DirectSound 転送（cl2 外。UI 側 Closeds で m_dsb が NULL でもローカル参照で安全）
		if (PlayAbortIsSet() || thn1)
			return stopPlaybackAndExit();
		dsb = m_dsb;
		BOOL lockedOk = FALSE;
		if (stageBytes > 0 && isPlausibleDsb(dsb) && !thn1 && !sek && !PlayAbortIsSet()) {
			DsOpLock ds;
			dsb = m_dsb;
			if (isPlausibleDsb(dsb) && !thn1 && !sek && !PlayAbortIsSet()) {
			InterlockedExchange(&g_dsDeviceOpBusy, 1);
			hr = dsb->Lock(oldw, (DWORD)stageBytes, (LPVOID*)&pdsb1, &len3, (LPVOID*)&pdsb2, &len4, 0);
			if (hr == DS_OK) {
				lockedOk = TRUE;
				thn = FALSE;
				const int copy1 = (int)len3;
				const int copy2 = (int)len4;
				const int thruMute = VstLiveThruIsOn();
				if (copy1 > 0 && copy1 <= stageBytes) {
					if (thruMute)
						ZeroMemory(pdsb1, (SIZE_T)copy1);
					else
						memcpy(pdsb1, s_dsStage, (size_t)copy1);
				}
				if (stageFade && copy1 > 0) ZeroMemory(pdsb1, (SIZE_T)copy1);
				if (copy2 > 0 && copy1 + copy2 <= stageBytes) {
					if (thruMute)
						ZeroMemory(pdsb2, (SIZE_T)copy2);
					else
						memcpy(pdsb2, s_dsStage + copy1, (size_t)copy2);
				}
				if (stageFade && copy2 > 0) ZeroMemory(pdsb2, (SIZE_T)copy2);
				dsb->Unlock(pdsb1, len3, pdsb2, len4);
			}
			InterlockedExchange(&g_dsDeviceOpBusy, 0);
			}
		}
		if (lockedOk && !stageFade) {
			MpMirrorWritePcm(s_dsStage, stageBytes);
			MpRemoteWritePcm(s_dsStage, stageBytes);
			extern int g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits;
			extern UINT PlaybackCcWriteDsPcm(const void* p, UINT n, int rate, int ch, int bits);
			const int r = (g_ds_pcm_rate >= 8000) ? g_ds_pcm_rate : wavbit_sample_Hz;
			const int c = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : 2;
			const int b = (g_ds_pcm_bits == 16 || g_ds_pcm_bits == 24 || g_ds_pcm_bits == 32) ? g_ds_pcm_bits : 16;
			PlaybackCcWriteDsPcm(s_dsStage, (UINT)stageBytes, r, c, b);
		}

		{
			std::lock_guard<std::mutex> guard(cl2);
			// 書込み累積は従来どおり Lock 成否に依存しない（再生位置進行の一貫性維持）。
			const __int64 writtenBefore = g_dsWrittenBytes;
			g_dsWrittenBytes += (writtenThisCycle > 0) ? writtenThisCycle : 0;
			// EOF（fade1=停止 / endflg=連続）を最初に検出したサイクルで実音声の終端を確定。
			// readme があれば最終チャンク内の実バイト境界。無音のみなら「直前までの書込み」を終端にする。
			// writtenBefore==0（まだ1バイトも書いていない／誤って fade1 が立った直後）では確定しない。
			// 同サイクルの writtenThisCycle で確定すると初回バッファ直後に AUTO_STOPPED→解放レースになる。
			// クロスフェードで A が窓の途中で尽きると endflg が立つ。Xing/Info の無い
			// mp3（例:「まいご」）はヘッダ長が実デコード長より数十ms長いため必ずこうなる。
			// 混合を解除した直後はこの endflg が残っており、g_dsWrittenBytes は前曲の
			// 絶対値のままなので、そこで終端を確定すると新曲は鳴り出した瞬間に EOF と
			// 判定される。結果 1 曲飛ばされ、その次が頭から鳴る。昇格直後は確定しない。
			extern volatile LONG g_xfPromoteTick;
			const LONG promoTick = InterlockedCompareExchange(&g_xfPromoteTick, 0, 0);
			const int justPromoted = (promoTick != 0
				&& (LONG)(GetTickCount() - (DWORD)promoTick) < 1500);
			if (g_endWrittenBytes == 0 && (fade1 || endflg)
				&& !InterlockedCompareExchange(&g_xfInProgress, 0, 0)
				&& !justPromoted) {
				if (readmeThisCycle > 0 && readmeThisCycle <= writtenThisCycle)
					g_endWrittenBytes = writtenBefore + readmeThisCycle;
				else if (writtenBefore > 0)
					g_endWrittenBytes = writtenBefore;
			}
			if (prefillProtectUntil > 0 && len2 == 0 && len1 > 0 && ringBytes > 0) {
				ULONG nw = oldw + (ULONG)len1;
				if (nw >= ringBytes)
					nw -= ringBytes;
				oldw = nw;
			}
			else {
				oldw = WriteCursor;
			}
		}

		// 再生カーソル基準の heard を毎サイクル更新（クロスフェード早期開始に必要）
		{
			DsOpLock ds;
			LPDIRECTSOUNDBUFFER8 dsbb = m_dsb;
			if (isPlausibleDsb(dsbb) && ringBytes > 0) {
				ULONG pc = 0, wc = 0;
				if (dsbb->GetCurrentPosition(&pc, &wc) == DS_OK) {
					const __int64 queued = (__int64)(((ULONG)oldw + ringBytes - pc) % ringBytes);
					g_heardBytes = g_dsWrittenBytes - queued;
				}
			}
		}

		/* B 未準備のときだけ UI に開始を頼む（同期 Open が必要）。準備済みは上の
		 * サイクル境界で自前に開始する。両方から始めると開始位置がぶれる。 */
		if (!InterlockedCompareExchange(&g_xfInProgress, 0, 0)
			&& !InterlockedCompareExchange(&g_xfPrepared, 0, 0) && XfEnabled()) {
			if (XfShouldStartEarly(g_heardBytes, g_endWrittenBytes) || XfRenzokuLimitReached())
				XfTryStartCrossfade();
		}

		// 終端の短フェード＆ジャスト停止判定（ロックを外して終了処理へ）。
		// playb(デコード先頭)ではなく DS 再生カーソルが実音声終端へ到達した瞬間を「曲終わり」とする。
		// 実再生バイト数 = 累積書込み − 未再生キュー(我々の書込みヘッド oldw と再生カーソル pc の差)。
		// oldw はリング上の実書込み位置。g_dsWrittenBytes%ring とは初期オフセット分ずれるため oldw を使う。
		if (g_endWrittenBytes != 0 && !InterlockedCompareExchange(&g_xfInProgress, 0, 0)) {
			LPDIRECTSOUNDBUFFER8 dsbb = m_dsb;
			__int64 heard = g_heardBytes;
			if (isPlausibleDsb(dsbb)) {
				// 終端直前の残り実音声に短いフェードをかけてクリック/プツ音を防ぐ。
				const __int64 remain = g_endWrittenBytes - heard;
				const int bpf = (g_outBytesPerFrame > 0) ? g_outBytesPerFrame : 4;
				extern int g_ds_pcm_rate;
				const int fadeHz = (g_ds_pcm_rate >= 8000) ? g_ds_pcm_rate
					: ((wavbit_sample_Hz > 0) ? wavbit_sample_Hz : 44100);
				const __int64 fadeBytes = (__int64)bpf * (__int64)fadeHz * 35 / 1000; // 約35ms（DS 出力基準）
				if (remain > 0 && remain < fadeBytes && fadeBytes > 0 && !VstLiveThruIsOn()) {
					LONG vol = (LONG)((double)DSBVOLUME_MIN * (1.0 - (double)remain / (double)fadeBytes));
					if (vol > 0) vol = 0;
					if (vol < DSBVOLUME_MIN) vol = DSBVOLUME_MIN;
					{
						DsOpLock dsVol;
						if (isPlausibleDsb(m_dsb))
							m_dsb->SetVolume(vol);
					}
				}
			}

			// fade1(=停止 / 連続でない) のときだけ DS スレッドで停止する。
			// 連続再生(endflg)の次曲遷移は UI 側タイマー 9000 が同じ終端到達判定で行う。
			if (fade1 && heard >= g_endWrittenBytes) {
				if (PlayAbortIsSet() || thn1 || stf != 0 || syukai == 2)
					return stopPlaybackAndExit();
				playf = 0; thn = TRUE; reset = TRUE;
				{
					DsOpLock dsFade;
					LPDIRECTSOUNDBUFFER8 dsbFade = m_dsb;
					if (isPlausibleDsb(dsbFade)) {
						dsbFade->SetVolume(DSBVOLUME_MIN);
						dsbFade->Stop();
					}
				}
				if (og && ::IsWindow(og->GetSafeHwnd()))
					og->PostMessage(WM_PLAYBACK_AUTO_STOPPED, (WPARAM)(ULONG)playGen, 0);
				// AfxEndThread 禁止（stopPlaybackAndExit と同じ理由）。通常 return でスレッド終了。
				return 0;
			}
		}
	}
	return 0;
} //handlenotifications()

// WAV出力専用：DirectSoundを使わずデコード→ファイル書き込みのみ。m_c2チェックに関係なくcc1で出力。
void WavExportBeginPass();
int WavExportSkipSecCap();
int WavExportSasamiLoopDone();
void HandleNotifications_export()
{
	extern volatile LONG g_mpPromptAnalyzeOnly;
	extern __int64 wl;
	if (wavExportPath.GetLength() == 0 && !g_mpPromptAnalyzeOnly) return;
	if (cc1 != 1) return;
	// 曲ごとパラメータ: WAV 出力する曲のパラメータを復元(メインスレッド)
	SongParams_Sync(true);
	g_isWavExportRendering = true;
	struct ExportModeGuard {
		~ExportModeGuard() { g_isWavExportRendering = false; }
	} exportModeGuard;
	// play() 先頭の stop1() が playf==1 のため SignalPlaybackNotifyThreadStop し、
	// syukai==2 / stf!=0 のままだとループ即終了＋DispatchPlaywavFill が無音になる
	thn1 = FALSE;
	stf = 0;
	syukai = 0;
	syukai2 = 0;
	oldw = 0;
	fade1 = 0;
	// 連続再生タイマーが DoEvent 経由で次曲 Restart すると export が壊れる
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->KillTimer(9000);
	const ULONG bufSize = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM_DS;
	const int chunkSize = (int)(WAVDALen / 10);
	if (chunkSize <= 0) return;
	RubberBand_DestroyAll();
	// 通常再生は BeginPlaybackNotifyThread で g_openDecoderMode を載せるが、
	// export はその経路を通らない。OGG(mode==0) 等だと INT_MIN のまま
	// DispatchPlaywavFill が即 return → PCM 無しで永久ループ（進捗1%固定）になる。
	g_openDecoderMode = mode;

	extern bool g_wavExportApplyPrompt;
	if (g_wavExportApplyPrompt && MpPromptIsActive()) {
		// 書き出しは再生タイマーを通らないので、ここで実行開始扱いにする
		MpPromptNotifyPlayback(1, 0.0);
		MpPromptTickAtTime(0.0);
	}

	WavExportBeginPass();
	MpDecodeProgressReport(2, LL14(
		L"デコード中…", L"Decoding...", L"Decodage...", L"Decodifica...", L"Decodificando...",
		L"디코딩 중…", L"解码中…", L"Decoding...", L"Декодирование...", L"Dekodiere...",
		L"Decodificando...", L"Decoderen...", L"Dekodowanie...", L"Kod cozuluyor..."));

	__int64 lastWl = wl;
	int idleIters = 0;
	int iter = 0;
	for (;;) {
		DoEvent();
		if (g_wavExportCancel) { fade1 = 1; break; }
		// DoEvent 再入で stop 要求が立っても、書き出し本体は続行する
		thn1 = FALSE;
		stf = 0;
		if (syukai == 2) break;
		if (fade1) break;
		if (wavExportLoopCount > 0 && loopcnt >= wavExportLoopCount) break;
		if (WavExportSasamiLoopDone()) { fade1 = 1; break; }
		// KPI(mode==-3)等: 終端のないソースは秒数で打ち切る（実書き込み量優先）
		// SASAMI は秒数を見ない（J は回数、J なしは曲末）。
		// wl は変換後バイト。打ち切りもロック済み出力形式で数える（192k→48k で 1/4 短縮しない）
		if (!WavExportSkipSecCap() && g_wavExportMaxSec > 0 && wavbit_sample_Hz > 0) {
			extern int g_pcm_upscale_active;
			extern int g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits;
			int outRate = wavbit_sample_Hz;
			int outCh = (wavchannel > 0) ? wavchannel : 2;
			int outBits = abs(wavsam_depth);
			if (PlaybackCcFormatLocked()) {
				PlaybackCcGetFormat(outRate, outCh, outBits);
			}
			else if (g_pcm_upscale_active && g_ds_pcm_rate >= 8000) {
				outRate = g_ds_pcm_rate;
				outCh = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : outCh;
				if (g_ds_pcm_bits == 16 || g_ds_pcm_bits == 24 || g_ds_pcm_bits == 32)
					outBits = g_ds_pcm_bits;
			}
			if (!(outBits == 16 || outBits == 24 || outBits == 32)) outBits = 16;
			if (outRate < 8000) outRate = wavbit_sample_Hz;
			if (outCh < 1) outCh = 2;
			const int bpfOut = outCh * (outBits / 8);
			/* J ループがある曲は 240 秒打ち切りにしない。周の長さ×回数まで出す。 */
			double capSec = (double)g_wavExportMaxSec;
			if (endf == 0 && loop2 > 0 && wavExportLoopCount > 0) {
				const double one = (double)(loop1 + loop2) / (double)wavbit_sample_Hz;
				const double need = one * (double)wavExportLoopCount + 60.0;
				if (capSec < need) capSec = need;
			}
			const __int64 maxOut = (__int64)(capSec * (double)outRate + 0.5);
			const __int64 maxSrc = (__int64)(capSec * (double)wavbit_sample_Hz + 0.5);
			const __int64 writtenFrames = (bpfOut > 0) ? (wl / bpfOut) : 0;
			if (writtenFrames >= maxOut || playb >= maxSrc) {
				fade1 = 1;
				break;
			}
		}
		int len1 = chunkSize;
		int len2 = 0;
		if (len1 > (int)(bufSize - oldw)) {
			len1 = (int)(bufSize - oldw);
			len2 = chunkSize - len1;
			if (len2 > (int)oldw) len2 = (int)oldw;
		}
		if (len1 <= 0 && len2 <= 0) {
			len1 = chunkSize;
			len2 = 0;
			oldw = 0;
		}
		sflg = TRUE;
		DispatchPlaywavFill(bufwav3, oldw, len1, len2);
		oldw = (oldw + len1 + len2) % bufSize;
		sflg = FALSE;
		if (g_wavExportApplyPrompt && MpPromptIsActive())
			MpPromptTickAtTime(0.0);
		if (fade1) break;
		if (wavExportLoopCount > 0 && loopcnt >= wavExportLoopCount) break;
		if (WavExportSasamiLoopDone()) { fade1 = 1; break; }
		if (!WavExportSkipSecCap() && g_wavExportMaxSec > 0 && wavbit_sample_Hz > 0) {
			extern int g_pcm_upscale_active;
			extern int g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits;
			int outRate = wavbit_sample_Hz;
			int outCh = (wavchannel > 0) ? wavchannel : 2;
			int outBits = abs(wavsam_depth);
			if (PlaybackCcFormatLocked()) {
				PlaybackCcGetFormat(outRate, outCh, outBits);
			}
			else if (g_pcm_upscale_active && g_ds_pcm_rate >= 8000) {
				outRate = g_ds_pcm_rate;
				outCh = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : outCh;
				if (g_ds_pcm_bits == 16 || g_ds_pcm_bits == 24 || g_ds_pcm_bits == 32)
					outBits = g_ds_pcm_bits;
			}
			if (!(outBits == 16 || outBits == 24 || outBits == 32)) outBits = 16;
			if (outRate < 8000) outRate = wavbit_sample_Hz;
			if (outCh < 1) outCh = 2;
			const int bpfOut = outCh * (outBits / 8);
			/* J ループがある曲は 240 秒打ち切りにしない。周の長さ×回数まで出す。 */
			double capSec = (double)g_wavExportMaxSec;
			if (endf == 0 && loop2 > 0 && wavExportLoopCount > 0) {
				const double one = (double)(loop1 + loop2) / (double)wavbit_sample_Hz;
				const double need = one * (double)wavExportLoopCount + 60.0;
				if (capSec < need) capSec = need;
			}
			const __int64 maxOut = (__int64)(capSec * (double)outRate + 0.5);
			const __int64 maxSrc = (__int64)(capSec * (double)wavbit_sample_Hz + 0.5);
			const __int64 writtenFrames = (bpfOut > 0) ? (wl / bpfOut) : 0;
			if (writtenFrames >= maxOut || playb >= maxSrc) {
				fade1 = 1;
				break;
			}
		}

		++iter;
		if (wl != lastWl) {
			lastWl = wl;
			idleIters = 0;
		}
		else {
			++idleIters;
		}
		// RB プライミング中など wl==0 が続くとき、止まっていないことを示す
		if (wl == 0 && (iter % 16) == 0) {
			MpDecodeProgressReport(2 + (iter / 16) % 3, LL14(
				L"デコード準備中…", L"Preparing decode...", L"Prep. decode...", L"Prep. decode...", L"Prep. decode...",
				L"디코드 준비…", L"解码准备中…", L"Preparing...", L"Подготовка...", L"Vorbereitung...",
				L"Preparando...", L"Voorbereiden...", L"Przygotowanie...", L"Hazirlaniyor..."));
		}
		if (idleIters > 50000) {
			// PCM が進まない永久ループ防止（デコーダ未設定の再発など）
			break;
		}
		Sleep(0);
	}
}

extern float inputFloatData[];
extern int inputFloatN;
extern uint8_t m_bufwav3_1[];
extern int m_bufwav3_1_n;
extern int pitch;
extern float tempoRate2;
extern float PitchScaleFromPos(int pitchPos);
enum { kRbBytes = (10240 * 6 / 2) * 5 * 8, kRbCh = 32, kRbPull = 4096 };
float g_loopTailBuffer[kRbBytes];
int g_loopTailN = 0;
size_t g_loopTailPos = 0;
enum { RB_BANKS = 2 };
static bool g_rubberBandFinalFlushed[RB_BANKS] = { false, false };
static int g_rbInitRate[RB_BANKS] = { 0, 0 };
static int g_rbInitCh[RB_BANKS] = { 0, 0 };
static std::mutex g_rbMu[RB_BANKS];
struct RbProcessScratch {
	uint8_t raw[kRbBytes];
	float inputFloat[kRbBytes];
	int inputN = 0;
	float channelFlat[kRbBytes];
	float* channelPointers[kRbCh];
	float outputFlat[kRbPull * kRbCh];
	float* outputPointers[kRbCh];
	float dummyZero = 0.0f;
};
static RbProcessScratch g_rbScratch[RB_BANKS];

void ConvertRawBytesToFloat(const uint8_t* raw_data, int rawN,
	uint16_t bits_per_sample, uint16_t channels,
	float* out_float_data, int outCap, int& outN);
void ConvertFloatToRawBytes(const float* float_data, int nFloat,
	uint16_t target_bits_per_sample, uint16_t channels,
	uint8_t* out_raw_data, int outCap, int& outN);

static void RubberBand_DestroyBankUnlocked(int bank)
{
	if (bank < 0 || bank >= RB_BANKS) return;
	if (g_rubberBandStretcher[bank]) {
		delete g_rubberBandStretcher[bank];
		g_rubberBandStretcher[bank] = NULL;
	}
	g_rubberBandFinalFlushed[bank] = false;
	g_rbInitRate[bank] = 0;
	g_rbInitCh[bank] = 0;
}

void RubberBand_DestroyBank(int bank)
{
	if (bank < 0 || bank >= RB_BANKS) return;
	std::lock_guard<std::mutex> lk(g_rbMu[bank]);
	RubberBand_DestroyBankUnlocked(bank);
}

void RubberBand_DestroyAll()
{
	for (int b = 0; b < RB_BANKS; ++b)
		RubberBand_DestroyBank(b);
}

/* rate/ch を明示。グローバル wavchannel を触ると A/B 並行でヒープ破壊する */
static bool InitializeRubberBandStretcherExUnlocked(int bank, int rate, int ch)
{
	if (bank < 0 || bank >= RB_BANKS) bank = 0;
	if (rate < 8000) rate = 44100;
	if (ch < 1) ch = 2;
	RubberBand_DestroyBankUnlocked(bank);

	try {
		double pitchRatio = (double)PitchScaleFromPos(pitch);
		g_rubberBandStretcher[bank] = new RubberBand::RubberBandStretcher(
			rate,
			ch,
			RubberBand::RubberBandStretcher::OptionProcessRealTime |
			RubberBand::RubberBandStretcher::OptionEngineFaster |
			RubberBand::RubberBandStretcher::OptionTransientsCrisp |
			RubberBand::RubberBandStretcher::OptionPhaseLaminar,
			tempoRate2,
			pitchRatio
		);
		g_rubberBandStretcher[bank]->setDebugLevel(0);
		g_rbInitRate[bank] = rate;
		g_rbInitCh[bank] = ch;
		g_rubberBandFinalFlushed[bank] = false;
		return true;
	}
	catch (...) {
		return false;
	}
}

bool InitializeRubberBandStretcher(int bank)
{
	if (bank < 0 || bank >= RB_BANKS) bank = 0;
	std::lock_guard<std::mutex> lk(g_rbMu[bank]);
	int rate = wavbit_sample_Hz;
	int ch = wavchannel;
	if (rate < 8000) rate = 44100;
	if (ch < 1) ch = 2;
	return InitializeRubberBandStretcherExUnlocked(bank, rate, ch);
}

bool InitializeRubberBandStretcherForFormat(int bank, int rate, int ch)
{
	if (bank < 0 || bank >= RB_BANKS) bank = 0;
	if (rate < 8000) rate = 44100;
	if (ch < 1) ch = 2;
	std::lock_guard<std::mutex> lk(g_rbMu[bank]);
	if (g_rubberBandStretcher[bank]
		&& g_rbInitRate[bank] == rate
		&& g_rbInitCh[bank] == ch) {
		return true;
	}
	return InitializeRubberBandStretcherExUnlocked(bank, rate, ch);
}

bool InitializeRubberBandStretcher()
{
	return InitializeRubberBandStretcher(0);
}

bool ProcessAudioWithRubberBandBank(int bank, float tempoRate, bool t,
	const uint8_t* inData, int inBytes, int bits, int ch, int rate,
	float* outFloat, int& outN, int outCap)
{
	try {
		outN = 0;
		if (!outFloat || outCap <= 0) return false;
		if (bank < 0 || bank >= RB_BANKS) bank = 0;
		if (ch < 1) ch = 2;
		if (ch > kRbCh) ch = kRbCh;
		if (rate < 8000) rate = 44100;
		if (bits <= 0 || bits > 32) bits = 16;

		if ((!inData || inBytes <= 0) && !t) return false;

		std::lock_guard<std::mutex> lk(g_rbMu[bank]);
		RbProcessScratch& scr = g_rbScratch[bank];

		if (!g_rubberBandStretcher[bank]
			|| g_rbInitRate[bank] != rate
			|| g_rbInitCh[bank] != ch) {
			if (t) return false;
			tempoRate2 = tempoRate;
			if (!InitializeRubberBandStretcherExUnlocked(bank, rate, ch)) return false;
		}
		else if (t && g_rubberBandFinalFlushed[bank]) {
			outN = 0;
			return true;
		}

		g_rubberBandStretcher[bank]->setTimeRatio(tempoRate);
		g_rubberBandStretcher[bank]->setPitchScale(PitchScaleFromPos(pitch));

		if (!t) {
			if (inBytes > kRbBytes) inBytes = kRbBytes;
			memcpy(scr.raw, inData, (size_t)inBytes);
			ConvertRawBytesToFloat(scr.raw, inBytes, (uint16_t)bits, (uint16_t)ch, scr.inputFloat, kRbBytes, scr.inputN);
			if (scr.inputN <= 0) return false;

			const size_t samplesIn = (size_t)scr.inputN / (size_t)ch;
			if (samplesIn == 0) return false;
			for (int c = 0; c < ch; ++c)
				scr.channelPointers[c] = scr.channelFlat + (size_t)c * samplesIn;
			for (size_t i = 0; i < samplesIn; ++i) {
				for (int c = 0; c < ch; ++c)
					scr.channelPointers[c][i] = scr.inputFloat[i * (size_t)ch + (size_t)c];
			}

			g_rubberBandStretcher[bank]->process(scr.channelPointers, samplesIn, false);
			g_rubberBandFinalFlushed[bank] = false;
		}
		else {
			for (int c = 0; c < ch; ++c)
				scr.channelPointers[c] = &scr.dummyZero;
			g_rubberBandStretcher[bank]->process(scr.channelPointers, 0, true);
			g_rubberBandFinalFlushed[bank] = true;
		}

		outN = 0;
		const size_t pullSize = kRbPull;
		for (int c = 0; c < ch; ++c)
			scr.outputPointers[c] = scr.outputFlat + (size_t)c * pullSize;

		while (g_rubberBandStretcher[bank]->available() > 0) {
			if (outN >= outCap) break;
			int room = (outCap - outN) / ch;
			if (room <= 0) break;
			size_t toGet = (std::min)((size_t)g_rubberBandStretcher[bank]->available(), pullSize);
			if (toGet > (size_t)room) toGet = (size_t)room;
			size_t retrieved = g_rubberBandStretcher[bank]->retrieve(scr.outputPointers, toGet);
			if (retrieved == 0) break;
			const int add = (int)(retrieved * (size_t)ch);
			const int base = outN;
			outN += add;
			for (size_t i = 0; i < retrieved; ++i) {
				for (int c = 0; c < ch; ++c)
					outFloat[base + (int)i * ch + c] = scr.outputPointers[c][i];
			}
		}

		if (bank == 0 && g_loopTailPos < (size_t)g_loopTailN) {
			size_t tailTotal = (size_t)g_loopTailN;
			for (int i = 0; i < outN; ++i) {
				if (g_loopTailPos >= tailTotal) break;
				float ratio = 1.0f - ((float)g_loopTailPos / (float)tailTotal);
				float fadeFactor = ratio * ratio;
				outFloat[i] += g_loopTailBuffer[g_loopTailPos] * fadeFactor;
				g_loopTailPos++;
			}
			if (g_loopTailPos >= tailTotal) {
				g_loopTailN = 0;
				g_loopTailPos = 0;
			}
		}

		return true;
	}
	catch (...) {
		return false;
	}
}

bool ProcessAudioWithRubberBand(float tempoRate, bool t)
{
	try {
		if (m_bufwav3_1_n <= 0 && !t) return false;

		int bits;
		if (mode == -10)
			bits = Mp3GetDecoderBitsForRubberBand();
		else
			bits = (wavsam_depth <= 0 || wavsam_depth > 32) ? 16 : abs(wavsam_depth);

		const uint8_t* p = (m_bufwav3_1_n <= 0) ? NULL : m_bufwav3_1;
		const int n = m_bufwav3_1_n;
		const int ch = (wavchannel > 0) ? wavchannel : 2;
		const int rate = (wavbit_sample_Hz >= 8000) ? wavbit_sample_Hz : 44100;
		return ProcessAudioWithRubberBandBank(XfDecSlot(), tempoRate, t, p, n, bits, ch, rate,
			m_convertedPcmFloatData, m_convertedPcmN, kRbBytes);
	}
	catch (...) {
		return false;
	}
}


#include <cmath>
// rawバイトデータからfloatデータへの変換
// 8bit, 16bit, 24bit, 32bit PCM (int/float) に対応
// rawバイトデータからfloatデータへの変換
// 8bit, 16bit, 24bit, 32bit PCM (int/float) に対応
void ConvertRawBytesToFloat(const uint8_t* raw_data, int rawN,
	uint16_t bits_per_sample, uint16_t channels,
	float* out_float_data, int outCap, int& outN)
{
	outN = 0;
	if (!raw_data || rawN <= 0 || !out_float_data || channels == 0 || bits_per_sample == 0) {
		return;
	}

	size_t bytes_per_sample = bits_per_sample / 8;
	if (bytes_per_sample == 0) {
		return;
	}
	size_t total_samples_count = (size_t)rawN / bytes_per_sample;
	if ((int)total_samples_count > outCap)
		total_samples_count = (size_t)outCap;
	outN = (int)total_samples_count;

	for (size_t i = 0; i < total_samples_count; ++i) {
		size_t current_byte_pos = i * bytes_per_sample;
		if (current_byte_pos + bytes_per_sample > (size_t)rawN) {
			out_float_data[i] = 0.0f;
			continue;
		}

		if (bits_per_sample == 8) {
			out_float_data[i] = ((float)raw_data[current_byte_pos] - 128.0f) / 128.0f;
		}
		else if (bits_per_sample == 16) {
			int16_t s_val = (int16_t)(raw_data[current_byte_pos] | (raw_data[current_byte_pos + 1] << 8));
			out_float_data[i] = (float)s_val / 32768.0f;
		}
		else if (bits_per_sample == 24) {
			// 24bit LE: 3バイトを読み、符号拡張して -1.0～1.0 に正規化
			uint32_t u = (uint32_t)raw_data[current_byte_pos] |
				((uint32_t)raw_data[current_byte_pos + 1] << 8) |
				((uint32_t)raw_data[current_byte_pos + 2] << 16);
			int32_t i_val = (u & 0x800000u) ? (int32_t)(u | 0xFF000000u) : (int32_t)u;
			out_float_data[i] = (float)i_val / 8388608.0f;
		}
		else if (bits_per_sample == 32) {
			int32_t i_val = (int32_t)(raw_data[current_byte_pos] |
				(raw_data[current_byte_pos + 1] << 8) |
				(raw_data[current_byte_pos + 2] << 16) |
				(raw_data[current_byte_pos + 3] << 24));
			out_float_data[i] = (float)i_val / 2147483648.0f; // 2^31
		}
		else {
			// 未対応ビット深度
			outN = 0;
			return;
		}
	}
}

// floatデータからrawバイトデータへの変換
// 8bit, 16bit, 24bit, 32bit PCM (int/float) に対応
// floatデータからrawバイトデータへの変換
// 8bit, 16bit, 24bit, 32bit PCM (int/float) に対応
void ConvertFloatToRawBytes(const float* float_data, int nFloat,
	uint16_t target_bits_per_sample, uint16_t channels,
	uint8_t* out_raw_data, int outCap, int& outN)
{
	outN = 0;
	if (!float_data || nFloat <= 0 || !out_raw_data || channels == 0 || target_bits_per_sample == 0) {
		return;
	}

	size_t bytes_per_sample = target_bits_per_sample / 8;
	if (bytes_per_sample == 0) return;
	int n = nFloat;
	if ((size_t)n * bytes_per_sample > (size_t)outCap)
		n = (int)((size_t)outCap / bytes_per_sample);
	outN = (int)((size_t)n * bytes_per_sample);

	for (int i = 0; i < n; ++i) {
		float sample_float = float_data[i];
		// クリップ (-1.0から1.0の範囲に収めることでオーバーフロー防止)
		if (sample_float > 1.0f) sample_float = 1.0f;
		else if (sample_float < -1.0f) sample_float = -1.0f;

		size_t current_byte_pos = i * bytes_per_sample;

		if (target_bits_per_sample == 8) { // 8-bit unsigned PCM
			out_raw_data[current_byte_pos] = (uint8_t)(sample_float * 127.0f + 128.0f);
		}
		else if (target_bits_per_sample == 16) { // 16-bit signed PCM (リトルエンディアン)
			// 入力側の /32768.0f と対称になるよう 32768.0f でスケール、丸めてクリップ
			int32_t v = (int32_t)roundf(sample_float * 32768.0f);
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			int16_t s_val = (int16_t)v;
			out_raw_data[current_byte_pos] = (uint8_t)(s_val & 0xFF);
			out_raw_data[current_byte_pos + 1] = (uint8_t)((s_val >> 8) & 0xFF);
		}
		else if (target_bits_per_sample == 24) { // 24-bit signed PCM (リトルエンディアン)
			// 入力側の /8388608.0f と対称になるよう 8388608.0f でスケール、丸めてクリップ
			float vf = roundf(sample_float * 8388608.0f);
			if (vf > 8388607.0f) vf = 8388607.0f;
			if (vf < -8388608.0f) vf = -8388608.0f;
			int32_t i_val = (int32_t)vf;
			out_raw_data[current_byte_pos] = (uint8_t)(i_val & 0xFF);
			out_raw_data[current_byte_pos + 1] = (uint8_t)((i_val >> 8) & 0xFF);
			out_raw_data[current_byte_pos + 2] = (uint8_t)((i_val >> 16) & 0xFF);
		}
		else if (target_bits_per_sample == 32) {
			int32_t i_val = (int32_t)(sample_float * 2147483647.0f); // 2^31 - 1
			out_raw_data[current_byte_pos] = (uint8_t)(i_val & 0xFF);
			out_raw_data[current_byte_pos + 1] = (uint8_t)((i_val >> 8) & 0xFF);
			out_raw_data[current_byte_pos + 2] = (uint8_t)((i_val >> 16) & 0xFF);
			out_raw_data[current_byte_pos + 3] = (uint8_t)((i_val >> 24) & 0xFF);
		}
		else {
			// 未対応ビット深度
			outN = 0;
			return;
		}
	}
}
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioClock = __uuidof(IAudioClock);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

UINT32 bufsize;

int COggDlg::WASAPIInit()
{
	return 0;
	CoInitialize(NULL);
	::CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&deviceEnumerator);
	if (deviceEnumerator == NULL) {
		return 0;
	}
	deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
	pDevice->Activate(IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&pAudioClient);
	return 1;
}

__int64 OggGetAnalogAfterPlayFrames(int sr)
{
	/* 使わない。kbsasami の 900ms は DS キューで、再生カーソルが既に含んでいる。
	   ここに周期や GetStreamLatency を足すと二重。sr は呼び出し互換。 */
	(void)sr;
	return 0;
}

template< typename T, class TFreePolicy >
class base_memory
{
private:
	T* FMemory;
public:
	base_memory(T* AMemory = NULL)
		: FMemory(AMemory) {
	}
	virtual ~base_memory(void)
	{
		reset();
	}
	T* release(void)
	{
		T* tmp = FMemory;
		FMemory = NULL;
		return tmp;
	}
	void reset(T* AMemory = NULL)
	{
		if (AMemory != FMemory)
		{
			if (NULL != FMemory)
				TFreePolicy(FMemory);
			FMemory = AMemory;
		}
	}
	operator T* ()
	{
		return FMemory;
	}
	T* get() { return FMemory; }
	T* operator ->() { return FMemory; }
	T** operator&(void)
	{
		return &FMemory;
	}
};

struct co_task_memory_free_policy
{
	template< typename T >
	void operator()(const T* AMemory) const
	{
		if (NULL != AMemory)
			::CoTaskMemFree(AMemory);
	}
};

template< typename T >
class co_task_memory : public base_memory< T,
	co_task_memory_free_policy >
{
public:
	co_task_memory(T* AMemory = NULL)
		: base_memory< T, co_task_memory_free_policy >(AMemory)
	{
	}
};

void COggDlg::WASAPIChange(WAVEFORMATEX* pwf)
{
	if (pAudioClient) pAudioClient->Stop();
	co_task_memory<WAVEFORMATEX>  alt_format;
	REFERENCE_TIME buffer_period = 40 /* ms */ * 10000;
	REFERENCE_TIME buffer_duration = buffer_period * 4;
	int ret = pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, (WAVEFORMATEX*)pwf, &alt_format);
	if (FAILED(ret)) {
		MessageBox(LL14(L"未サポートのフォーマット", L"Unsupported format", L"Format non pris en charge", L"Formato non supportato", L"Formato no compatible", L"지원되지 않는 형식", L"不支持的格式", L"تنسيق غير مدعوم", L"Неподдерживаемый формат", L"Nicht unterstütztes Format", L"Formato não suportado", L"Niet-ondersteund formaat", L"Nieobsługiwany format", L"Desteklenmeyen biçim"));
		return;
	}
	ret = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, buffer_duration, buffer_period, pwf, NULL);
	ret = pAudioClient->GetBufferSize(&bufsize);
	ret = pAudioClient->GetService(IID_PPV_ARGS(&pRenderClient));
}

/*
===============================================================================
  ★ Hyper DSP Equaliser ★
  超高品質イコライザー & 環境音響エフェクト - 究極進化版

  環境音響: 101種 (0-100) - 物理モデルに基づく完全差別化
  EQプリセット: 51種 (0-50)
  拡張パラメータ: 5種 (eq[15-19])

  パラメータ総数: 63個（従来29個→63個）
  物理ベースモデリング & 周波数依存処理
===============================================================================

環境音響100種:

[基本空間 0-10]
00.なし - 音響処理オフ。ドライ信号をそのまま通す
01.風呂場 - タイル密閉の小空間。2-15msの初期反射、RT60約0.6s、平行壁の金属フラッターと3kHz帯のタイル共鳴、高湿度
02.ホール - 中規模ホール。RT60約1.8s、12-130msの密な初期反射、バランス型の拡散残響、適度な空気吸収
03.教会 - 石造りの教会。RT60約4.5s、20ms以降の長い初期反射、高密度拡散、石の重い低域残響、35msプリディレイ
04.洞窟 - 岩の洞窟。RT60約2.8s、不規則な初期反射、暗いLPF3.5kHz、高湿度、粗い岩面の拡散と低域共鳴
05.スタジオ - 吸音処理されたスタジオ。RT60約0.25s、極小の初期反射、高ダンピングでほぼ無響、ウェット僅少
06.ライブハウス - 小規模ライブハウス。RT60約1.0s、木と機材で密なパンチのある反射、中低域の温かみ、明瞭なエネルギー
07.森 - 森林。明確な壁のない拡散音場、葉による高域吸収、疎らな反射、風のゆらぎ、包み込む柔らかさ
08.山 - 山の谺。遠い斜面からの350/700/1050/1400msの離散エコー、高い開放度・高度感、澄んだ明瞭な反射、風
09.広場 - 都市の広場。舗装面からの反射と開放的な空気感、疎らな拡散、遠い建物の緩いスラップ、微風
10.カテドラル - 巨大カテドラル。RT60約6.0s、45msプリディレイ、超高密度拡散、重い低域残響、圧倒的な空間

[公共施設 11-20]
11.体育館 - 体育館。RT60約2.2s、硬い床壁の金属的反射、平行面のフラッターとコム、木床の中域、明るい高域
12.峡谷 - 峡谷。両岸の壁から交互に返る180/260/420/640msの近接エコー、広いパン、岩の反射、開放的な高度感
13.地下室 - 地下室。RT60約0.9s、狭く密度の高い初期反射、コンクリの中低域、湿り気、暗めで圧迫感のある残響
14.劇場 - 音響設計された劇場。RT60約1.4s、客席吸音、高い拡散と明瞭度、木の温かみ、台詞が通る中域
15.水中 - 水中。LPF約1.1kHzの暗くこもった音、高湿度、遅く揺らぐモジュレーション、密度の高い減衰、重い低域
16.トンネル/地下道 - 長いトンネル。RT60約2.0s、平行壁の強いフラッターとコムフィルタ、コンクリの反射、狭いステレオ、金属質
17.アリーナ/ドーム - 巨大ドーム。RT60約3.5s、観客席の吸音、遅く広い残響、高い天井の空気感、遠いPAスラップ
18.小部屋/クローゼット - 超小空間・クローゼット。RT60約0.3s、極短の初期反射、衣類等の吸音でほぼデッド、密閉感
19.階段室 - 階段室。RT60約1.8s、縦方向に折り返す螺旋的な反射、高い天井係数、コンクリのフラッター、複雑な反射
20.地下鉄ホーム - 地下鉄ホーム。RT60約2.4s、都市コンクリの硬い反射、フラッター、遠いトンネルへの抜け、金属的な高域

[産業・商業 21-30]
21.倉庫 - 倉庫。RT60約2.6s、鉄骨とコンクリの硬い反射、高い天井、平行面のフラッター、乾いた中高域
22.廊下 - 屋内廊下。RT60約1.2s、狭い平行壁のフラッターとコム、中程度の反射、狭いステレオ像
23.工場 - 稼働中の工場。RT60約2.8s、金属機械の強い反射と共鳴、僅かな歪み、コンクリ床、明るく硬い響き
24.寺社仏閣 - 木造の寺社。RT60約1.8s、太い木材の温かい反射と拡散、高い天井、柔らかな中低域、香煙の空気感
25.宇宙空間 - 宇宙空間の演出。反射のほぼ無い真空を象徴する僅かなウェット、極端に広いステレオ、微かなシマーとフェイズ、無限の開放
26.野球場/サッカー場 - 屋外スタジアム。RT60約2.2s、開放的な空気感、観客の吸音、遠いスタンドからのPAスラップ、風
27.図書館 - 図書館。RT60約0.7s、書架による強い吸音と拡散、木の温かみ、静かで柔らかい短めの残響
28.プール(室内) - 室内プール。RT60約2.6s、タイルとガラスの明るい反射、極めて高い湿度、水面のフラッター、2.5kHz共鳴
29.エレベーター - 金属製のエレベーター箱。RT60約0.4s、極小の金属反射、強い密閉感、1.2kHzの箱鳴り共鳴、狭いステレオ
30.駐車場 - 屋内駐車場。RT60約2.4s、低い天井の広いコンクリ空間、強いフラッターとコム、暗い反射、400Hz共鳴

[文化施設 31-40]
31.コンサートホール - 専用コンサートホール。RT60約2.1s、極めて豊かな拡散残響、木の温かみ、広いステレオ、澄んだ明瞭度
32.ジャズクラブ - 親密なジャズクラブ。RT60約0.8s、木の温かい反射、中低域の充実、程よい拡散、暖色の短めの残響
33.カラオケボックス - 防音カラオケ個室。RT60約0.5s、内装の吸音、柔らかく短い反射、密閉、暖色で控えめの残響
34.映画館 - 映画館。RT60約0.9s、台詞明瞭のため吸音処理、柔らかく制御された残響、広いステレオ、豊かな低域
35.地下鉄車内 - 走行中の地下鉄車内。RT60約0.5s、金属車体の反射、密閉、走行のドップラーと低域のランブル、1.5kHz共鳴
36.空港ターミナル - 空港ターミナル。RT60約2.8s、ガラスと鋼の広大な空間、明るい高域、群衆の吸音、遠いアナウンスの残響
37.ショッピングモール - ショッピングモール。RT60約2.0s、吹き抜けの開放的空間、ガラスとコンクリ、群衆の吸音、賑やかな中域
38.病院 - 病院の廊下。RT60約1.1s、清潔な硬い床と吸音天井のバランス、明るく無機質、控えめの反射
39.レコーディングブース - 録音ブース。RT60約0.15s、全面吸音材で極めてデッド、極小の初期反射、ウェットほぼゼロ
40.オペラハウス - 装飾豊かなオペラハウス。RT60約1.9s、木と漆喰の温かい高密度拡散、豊麗な中低域、広く柔らかな残響

[生活空間 41-50]
41.喫茶店/カフェ - 小さなカフェ。RT60約0.6s、木と布の温かい吸音、程よい反射、湯気の湿り気、居心地の良い短め残響
42.バー/ラウンジ - 薄暗いバー。RT60約0.7s、革と木の温かく暗い反射、低域の充実、落ち着いた柔らかな残響
43.居酒屋 - 賑やかな居酒屋。RT60約0.65s、木材主体の明るめの反射、程よい拡散、活気ある中域、湯気の湿り気
44.美術館/博物館 - 美術館。RT60約1.6s、石とガラスの静かな空間、高い天井、程よい拡散、落ち着いた明瞭な残響
45.講堂/大学教室 - 講堂。RT60約1.1s、話声明瞭のための吸音、程よい拡散、木の温かみ、はっきりした中域
46.竹林 - 竹林。RT60約1.0s、無数の竹幹による高い拡散、中空の竹による350Hz帯コム共鳴、風のゆらぎ、柔らかな高域吸収
47.渓谷/滝 - 滝のある渓谷。濡れた岩壁からの220/380/510/780msエコー、非常に高い湿度、水しぶきの微モジュレーション、開放的な峡谷反射
48.砂漠 - 砂漠。反射のほぼ無い極端に開けた乾燥空間、RT60約0.4s、強い空気吸収、極低湿度、風、疎らな高域
49.ガレージ - 家庭用ガレージ。RT60約1.0s、コンクリと金属シャッターの硬い反射、フラッターとコム、550Hz共鳴、密閉感
50.展望台 - 高所の展望台。RT60約0.8s、極めて開放的で高い高度感、強い風、ガラス手すりの僅かな反射、空気吸収

[拡張空間 51-60]
51.小さな礼拝堂 - 小さな礼拝堂。RT60約1.4s、石と木の穏やかな拡散、教会より小さく短い残響、温かく澄んだ響き
52.大型ショッピングセンター - 超大型商業施設。RT60約2.4s、複数階の巨大吹き抜け、ガラスとコンクリ、群衆の吸音、広く長い残響
53.地下洞窟(深層) - 深層の地下洞窟。RT60約4.0s、洞窟(4)より暗く広大、LPF2.5kHz、非常に高い湿度、重い低域、粗い岩面、180Hz共鳴
54.古城の大広間 - 古城の大広間。RT60約2.4s、石壁と木の梁の混合反射、高い天井、タペストリの適度な吸音、重厚で豊かな残響
55.野外音楽堂 - 野外音楽堂。RT60約1.3s、背後の反響板が音を前方へ集める、開放的な空気感、木の温かみ、微風
56.鍾乳洞 - 鍾乳洞。RT60約3.4s、鍾乳石の複雑な反射、水滴の微モジュレーション、高湿度、220Hz共鳴、鉱物のわずかなきらめき
57.廃墟工場 - 廃墟の工場。RT60約3.0s、錆びた金属の粗い反射、割れた窓からの風、僅かな歪みと荒れたコム、寂寥感のある長い残響
58.和室(畳) - 畳の和室。RT60約0.35s、畳と障子の強い吸音、非常に柔らかくデッド、木の温かみ、控えめの反射
59.温泉施設 - 温泉。RT60約1.3s、岩と木の温かい反射、極めて高い湿度と湯気、風呂場より広く温かい、柔らかな中域
60.屋根裏部屋 - 傾斜天井の屋根裏。RT60約0.6s、木材の温かい反射、低く傾いた天井、埃っぽい空気、こもりがちで控えめの残響

[特殊空間 61-70]
61.地下駐車場(多層) - 多層の地下駐車場。RT60約2.8s、広大なコンクリと低い天井の連続、駐車場(30)より長く暗い、強いフラッター、380Hz共鳴
62.古い劇場(木造) - 木造の古い劇場。RT60約1.5s、木材の強い共鳴と温かい拡散、320Hz帯の木の鳴り、劇場(14)より木質が濃い
63.大型倉庫(空) - 空の大型倉庫。RT60約3.2s、吸音物のない巨大空間、鉄骨とコンクリの反射、倉庫(21)より長く空虚、遠いスラップ
64.小さな教会 - 小さな教会。RT60約1.8s、石と木の拡散、礼拝堂(51)より大きく教会(3)より小さい中間、温かく澄んだ残響、ステンドグラスの僅かな反射
65.ガラス温室 - ガラス温室。RT60約1.4s、全面ガラスの明るく硬い反射、高い湿度、ガラスのフラッターと2.8kHz共鳴、非常に明るい高域
66.石造りトンネル - 石造りのトンネル。RT60約2.6s、粗い石壁の強いフラッターとコム、湿り気、トンネル(16)より粗く湿った反射、450Hz共鳴
67.コンクリート階段 - コンクリの階段室。RT60約2.2s、剥き出しコンクリの硬い縦反射、階段室(19)より長くコンクリ質、強いフラッター、550Hz共鳴
68.大浴場 - 大浴場。RT60約1.9s、広いタイル張りの明るい反射、極めて高い湿度、風呂場(1)より大きく残響が長い、2.2kHz共鳴とフラッター
69.洗面所 - 小さな洗面所。RT60約0.4s、狭いタイル張り、風呂場(1)より小さく明るい、強い密閉感、3.6kHzの高い共鳴
70.廊下(カーペット) - カーペット敷きの廊下。RT60約0.7s、床の強い吸音でフラッターが消え、柔らかく短い反射、廊下(22)より遥かにデッド

[専門空間 71-80]
71.会議室(大) - 大会議室。RT60約0.9s、吸音天井と絨毯で明瞭、程よい拡散、ガラスと木の混合、落ち着いた中域
72.会議室(小) - 小会議室。RT60約0.5s、狭く吸音された空間、短く控えめの反射、会議室(大)より密閉で近接感のある響き
73.防音室 - 完全防音室。RT60約0.12s、全面フル吸音でスタジオ(5)やブース(39)よりさらにデッド、ウェットほぼ皆無、無響に近い
74.エントランスホール - 大理石のエントランスホール。RT60約1.7s、硬い石とガラスの明るい反射、高い天井、澄んだ拡散残響
75.書斎 - 書棚に囲まれた書斎。RT60約0.45s、本と木の吸音、図書館(27)より小さく密閉、温かく静かな短い残響
76.キッチン - 家庭のキッチン。RT60約0.7s、タイルと金属器具の硬い明るい反射、僅かな湿り気、フラッターと2.6kHz共鳴
77.屋外駐車場 - 屋外の平面駐車場。RT60約0.9s、開けた舗装面の反射、屋内駐車場と違いフラッターなし、遠い建物からの緩いスラップ、微風
78.地下道(狭) - 非常に狭い地下道。RT60約1.6s、極端に近い平行壁、トンネル(16)より強いフラッターとコム、狭いステレオ、600Hz共鳴、密閉感
79.展示室 - 展示室。RT60約1.0s、中規模の中立的空間、程よい拡散、パネルの吸音、美術館(44)より小さく近接感のある残響
80.アトリエ - 採光の良いアトリエ。RT60約1.1s、木の床と大きなガラス窓の混合反射、明るく風通しの良い響き、程よい拡散

[SF/未来空間 81-100]
81.サイバーパンク路地 - ネオン輝く狭い路地。RT60約1.8s、濡れたコンクリと金属の反射、湿った空気、僅かな歪みとフェイズ、ネオン機器のうなり
82.宇宙船ブリッジ - 宇宙船の艦橋。RT60約1.0s、金属とガラスの制御された反射、清潔なフェイズとシマー、広いステレオ、1.2kHzの機器共鳴
83.ワープトンネル - ワープ航行トンネル。RT60約2.2s、強いドップラーと高速モジュレーション、深いフェイズとシマー、極端に広いステレオ、加速する運動感
84.量子ホール - 量子ホール。RT60約3.5s、きらめくシマーと揺らぐフェイズ、ガラス質の反射、超高密度拡散、広大で幻想的な残響
85.無限回廊 - 果てしない回廊。RT60約4.0s、高いフィードバックで延々と続く反射、フラッターとコム、フェイズ、終わらないエコーの連鎖、600Hz共鳴
86.逆再生空間 - 逆再生の空間。RT60約2.5s、極めて滑らかな盛り上がる残響、フェイズとシマー、明るくなるフィードバック音色、幻想的な広がり
87.タイムストップ室 - 時間停止の部屋。RT60約0.3s、ほぼ凍りついた無響、深いフェイズで固まった静止感、僅かに共鳴する高Q(4.0)、極端に狭いステレオ
88.データセンター - データセンター。RT60約0.9s、金属ラックの反射、低い120Hzの空調ハムと高Q共鳴、コムフィルタ、僅かな歪みと低速モジュ
89.巨大機械内部 - 巨大機械の内部。RT60約2.4s、鋼板の強い金属反射と共鳴、歪みと荒れたコム、フラッター、300Hz共鳴、重く低い響き
90.AIホログラム室 - AIホログラム室。RT60約1.6s、ガラス質の清潔な反射、明るいシマーとフェイズ、広いステレオ、2kHzの澄んだ共鳴、人工的で滑らか
91.重力ゼロ船庫 - 無重力の格納庫。RT60約3.2s、巨大な金属空間、浮遊感のあるフェイズとドップラー、広大なステレオ、500Hzの金属共鳴
92.惑星ドーム都市 - 惑星のドーム都市。RT60約2.8s、巨大なガラスドームの開放的空間、僅かなシマー、風、高度感、広く澄んだ残響
93.VRシミュレーター - VRシミュレーター。RT60約1.4s、絶えず変化する人工音場、高速フェイズとモジュレーション、シマー、僅かな歪み、広いステレオ
94.レーザー通路 - レーザーの狭い通路。RT60約1.2s、金属壁の強いフラッターとコム、明るいシマーとフェイズ、1.8kHzの高Q共鳴、僅かな歪みとドップラー
95.異次元裂け目 - 異次元の裂け目。RT60約3.0s、混沌としたフェイズ・ドップラー・歪み、深いモジュレーション、極端に広いステレオ、高フィードバックの不安定な残響
96.夢の中 - 夢の中。RT60約2.8s、極めて滑らかでぼやけた残響、LPF5kHzの霞、柔らかなフェイズとシマー、高密度拡散、包み込む幻想感
97.水晶洞 - 水晶の洞窟。RT60約3.6s、鍾乳洞(56)と違い明るく澄んで鳴り響く、ガラス質のきらめきと4.2kHz高Q共鳴、水滴、僅かなフェイズ
98.廃宇宙ステーション - 廃棄された宇宙ステーション。RT60約2.6s、空虚な金属の反射、フェイズと僅かな歪み、フラッター、エアリークの風、400Hz共鳴、寂寥感
99.ブラックホール縁 - ブラックホールの縁。RT60約4.5s、極端なドップラーと重力による音程変化、深いフェイズと歪み、LPF6kHz、超広ステレオ、重く長大な残響
100.サイバー聖堂 - サイバー聖堂。RT60約5.0s、カテドラルの超高密度拡散にデジタルのシマーとフェイズを融合、ガラスと金属、重厚かつ幻想的な広大残響

EQプリセット101種:
00.デフォルト, 01.低音ブースト, 02.高音ブースト, 03.ボーカル強調, 04.低音カット,
05.高音カット, 06.ラウドネス, 07.クラシック, 08.ロック, 09.カスタム,
10.ジャズ, 11.ポップ, 12.EDM, 13.メタル, 14.ヒップホップ,
15.アコースティック, 16.V字型, 17.逆V字型, 18.スマイルカーブ, 19.ラジオ/Podcast,
20.映画/ドラマ, 21.ゲーミング, 22.ライブ録音, 23.トレブルブースト, 24.ベースブースト,
25.小音量用, 26.ヘッドホン用, 27.ボーカル除去, 28.重低音強化, 29.ラジオAM,
30.ラジオFM, 31.テレビ音声, 32.電話音声, 33.ビンテージ, 34.モダン,
35.ウォーム, 36.ブライト, 37.フラット+, 38.スーパーベース, 39.クリスタル,
40.パーフェクト, 41.ダンス/クラブ, 42.R&B/ソウル, 43.レゲエ, 44.ブルース,
45.カントリー, 46.ファンク, 47.エレクトロニカ, 48.アンビエント, 49.インストゥルメンタル,
50.ナレーション/オーディオブック

拡張パラメータ5種:
eq[15] = マスターボリューム (0-200, デフォルト100)
eq[16] = 音の鮮明さ (0-200, デフォルト100) - 小:こもる、大:シャープ
eq[17] = 低域と高域のバランス (0-200, デフォルト100) - 小:低域寄り、大:高域寄り
eq[18] = 音の密度/充実度 (0-200, デフォルト100) - 小:軽い、大:充実
eq[19] = 音の立体感/臨場感 (0-200, デフォルト100) - 小:平面的、大:立体的

resetパラメータ:
0 = 通常処理
1 = 完全リセット（エンジン初期化）
2 = EQプリセット変更時の同期モード（savedata.eq[0-14]にプリセット値を反映）
*/

#define MAX_CH 8
#define EQ_BANDS 15
#define MAX_EARLY_REFLECTIONS 16

#define EQ_PRESET_COUNT 101

static const int EQ_PRESETS[EQ_PRESET_COUNT][15] = {
	// 0: デフォルト
	{100,100,100,100,100,100,100,100,100,100,100,100,100,100,100},

	// 1: 低音ブースト
	{180,170,160,145,130,115,105,100,100, 95, 95, 90, 88, 88, 88},

	// 2: 高音ブースト
	{ 88, 88, 88, 90, 92, 98,105,115,125,135,150,165,175,185,195},

	// 3: ボーカル強調
	{ 92, 88, 88, 92,100,115,125,135,145,150,140,125,110,100, 95},

	// 4: 低音カット
	{ 50, 60, 68, 75, 82, 88, 93, 97,100,100,100,100,100,100,100},

	// 5: 高音カット
	{100,100,100,100,100, 98, 92, 85, 78, 70, 62, 55, 48, 42, 38},

	// 6: ラウドネス
	{160,148,135,120,105, 95, 90, 90, 90, 95,105,122,138,150,162},

	// 7: クラシック
	{128,122,117,110, 98, 88, 87, 87, 87, 92,102,112,122,135,142},

	// 8: ロック
	{142,137,130,122,115, 98, 88, 88, 95,105,115,122,128,135,140},

	// 9: カスタム
	{100,100,100,100,100,100,100,100,100,100,100,100,100,100,100},

	// 10: ジャズ
	{135,128,122,115,105, 93, 88, 93,102,112,122,117,108,115,122},

	// 11: ポップ
	{100, 92, 92,102,112,122,132,140,148,148,138,128,122,115,110},

	// 12: EDM
	{195,188,175,162,145,122,100, 88, 88, 98,115,138,158,172,188},

	// 13: メタル
	{160,155,148,142,128,108, 88, 82, 88, 98,115,132,150,165,182},

	// 14: ヒップホップ
	{190,185,170,158,142,122,108,112,128,138,132,118,108,108,115},

	// 15: アコースティック
	{122,117,117,122,128,128,122,117,108,108,117,122,128,128,122},

	// 16: V字型
	{172,168,162,148,130,102, 78, 68, 62, 68, 82,118,145,165,185},

	// 17: 逆V字型
	{ 68, 72, 78, 83, 90,112,128,142,155,142,128,110, 88, 78, 68},

	// 18: スマイルカーブ
	{138,132,125,117,108, 95, 90, 90, 90, 92,102,115,130,142,155},

	// 19: ラジオ/Podcast
	{ 62, 68, 75, 85, 98,130,145,155,155,145,130,110, 88, 78, 68},

	// 20: 映画/ドラマ
	{122,117,108, 98, 88, 93,108,135,148,142,128,117,108,108,118},

	// 21: ゲーミング
	{152,145,138,128,122,108, 98, 88, 93,115,132,145,158,165,172},

	// 22: ライブ録音
	{128,122,117,108, 98, 98,108,117,125,132,138,138,138,142,148},

	// 23: トレブルブースト
	{ 88, 88, 88, 88, 88, 92,102,122,145,165,182,195,200,200,200},

	// 24: ベースブースト
	{200,200,195,180,160,135,115,102, 90, 90, 90, 90, 90, 90, 90},

	// 25: 小音量用
	{175,165,150,132,115, 98, 90, 90, 90, 90, 98,122,145,165,180},

	// 26: ヘッドホン用
	{115,108,102, 92, 90, 95,102,110,118,118,112,105, 98, 92, 88},

	// 27: ボーカル除去
	{100,100,100,100,100, 70, 55, 45, 40, 45, 55, 70,100,100,100},

	// 28: 重低音強化
	{200,200,200,188,172,150,130,115,105,100, 98, 95, 92, 90, 88},

	// 29: ラジオAM
	{ 85, 88, 92, 98,108,120,130,135,135,130,120,105, 92, 85, 80},

	// 30: ラジオFM
	{ 92, 95, 98,105,112,120,128,135,138,135,128,120,115,110,105},

	// 31: テレビ音声
	{ 95, 95, 95,100,108,120,135,145,150,145,135,120,108,100, 95},

	// 32: 電話音声
	{ 80, 80, 80, 85, 90,105,120,135,145,140,130,115,100, 90, 85},

	// 33: ビンテージ
	{125,120,115,108,100, 92, 88, 90, 95,100,105,108,110,112,115},

	// 34: モダン
	{ 98,100,102,105,108,112,118,125,132,135,132,125,120,115,110},

	// 35: ウォーム
	{118,115,112,108,105,102,100,100,102,105,108,112,115,118,120},

	// 36: ブライト
	{ 95, 95, 95, 98,100,105,112,120,130,140,150,160,170,178,185},

	// 37: フラット+
	{102,102,102,102,102,102,102,102,102,102,102,102,102,102,102},

	// 38: スーパーベース
	{200,200,200,195,185,170,150,130,115,105,100, 98, 95, 93, 90},

	// 39: クリスタル
	{ 90, 90, 92, 95,100,108,118,130,145,160,175,188,195,200,200},

	// 40: パーフェクト
	{130,125,122,118,115,112,110,112,115,120,125,130,135,140,145},

	// 41: ダンス/クラブ
	{200,200,195,185,168,145,120,100, 95,105,125,145,165,182,195},

	// 42: R&B/ソウル
	{155,148,140,130,120,108,105,115,125,130,125,118,112,110,108},

	// 43: レゲエ
	{200,200,200,195,180,160,135,110, 90, 85, 82, 80, 78, 75, 72},

	// 44: ブルース
	{120,118,115,112,108,105,110,118,125,128,122,115,108,105,102},

	// 45: カントリー
	{108,105,105,108,112,118,122,125,128,125,120,118,115,115,112},

	// 46: ファンク
	{142,138,132,125,115,105,100,108,120,132,140,135,128,125,122},

	// 47: エレクトロニカ
	{ 98, 98,100,102,105,110,118,128,138,145,148,145,140,135,130},

	// 48: アンビエント
	{105,105,105,105,105,105,105,105,105,105,105,105,105,105,105},

	// 49: インストゥルメンタル
	{112,110,108,105,105,108,115,125,135,140,135,128,120,115,110},

	// 50: ナレーション/オーディオブック
	{ 88, 88, 88, 92, 98,115,135,155,165,160,145,125,105, 95, 88},

	// 51: Deep Bass Safe
	{170,165,155,140,125,110,100, 95, 92, 90, 90, 90, 90, 90, 90},
	// 52: Vocal Clear 2
	{ 90, 90, 92, 95,100,115,130,145,155,150,135,120,108,100, 95},
	// 53: Airy Treble
	{ 92, 92, 94, 96, 98,102,108,118,132,145,158,170,178,185,190},
	// 54: Mid Punch
	{100, 98, 98,100,105,115,125,132,135,130,120,110,102, 98, 95},
	// 55: EDM Safe
	{165,160,152,142,130,115,100, 92, 92,100,115,130,145,158,170},
	// 56: Rock Wide
	{135,132,128,122,115,102, 92, 92, 98,108,118,126,132,138,142},
	// 57: Metal Tight
	{145,142,138,132,122,105, 92, 88, 92,100,115,128,140,150,160},
	// 58: HipHop Club
	{172,168,158,145,132,115,105,108,120,128,122,112,105,105,110},
	// 59: Acoustic Warm 2
	{118,115,112,110,108,108,105,102,100,102,108,112,118,120,118},
	// 60: Flat Monitor
	{100,100,100,100,100,100,100,100,100,100,100,100,100,100,100},
	// 61: Bright Vocal
	{ 95, 95, 95, 98,102,110,120,132,145,152,145,132,120,108,100},
	// 62: Bass&Air
	{150,145,138,128,115,102, 95, 92, 95,102,115,130,145,155,162},
	// 63: Podcast Soft
	{ 88, 90, 92, 96,102,118,132,142,145,140,130,118,105, 95, 90},
	// 64: Retro Radio 2
	{ 82, 85, 90, 98,108,120,128,132,132,126,116,104, 94, 86, 82},
	// 65: TV Dialog+ 
	{ 92, 92, 94,100,108,120,132,142,148,142,132,120,108,100, 95},
	// 66: Phone Narrow+
	{ 78, 80, 82, 86, 92,108,122,136,145,138,126,112, 98, 88, 82},
	// 67: Loudness Safe
	{145,138,128,115,102, 95, 92, 92, 92, 96,105,118,130,140,148},
	// 68: Small Speaker
	{ 88, 90, 94,100,108,118,125,130,132,128,120,110,100, 92, 88},
	// 69: Car Audio
	{145,140,132,122,112,100, 95, 95,100,110,120,128,135,140,145},
	// 70: Night Listening
	{132,126,118,108,100, 95, 92, 92, 92, 95,102,112,122,130,136},
	// 71: Studio Neutral+
	{102,102,102,102,102,102,102,102,102,102,102,102,102,102,102},
	// 72: Cymbal Sparkle
	{ 94, 94, 95, 96, 98,102,110,120,132,145,160,172,180,188,194},
	// 73: Drum Attack
	{150,145,135,122,110,102,100,102,108,115,120,122,120,115,110},
	// 74: Piano Presence
	{105,105,106,108,110,114,120,126,130,132,128,122,115,110,106},
	// 75: Strings Smooth
	{102,102,102,104,106,110,116,124,130,132,126,120,114,108,104},
	// 76: Brass Focus
	{108,106,104,106,110,118,128,136,140,136,126,116,108,102,100},
	// 77: Choir Wide
	{105,104,104,106,110,118,126,132,136,132,124,116,110,106,104},
	// 78: Cinema Impact
	{155,148,138,125,112,102, 96, 98,105,115,125,132,138,142,146},
	// 79: FPS Footstep
	{ 95, 95, 96,100,106,118,132,145,152,145,132,120,110,102, 98},
	// 80: RPG Atmosphere
	{112,110,108,106,104,106,112,120,128,132,128,120,114,110,106},
	// 81: Open World
	{120,116,112,108,104,102,104,110,118,125,128,126,122,118,114},
	// 82: Racing V
	{160,154,145,132,118,102, 90, 82, 80, 86,100,118,135,150,162},
	// 83: Fighting Punch
	{150,145,138,128,118,108,102,102,106,112,118,120,118,114,110},
	// 84: LoFi Mild
	{108,106,104,102,100, 98, 98,100,102,104,106,108,108,106,104},
	// 85: Chill Soft
	{108,106,104,102,100,100,102,106,112,116,116,112,108,104,102},
	// 86: K-Pop Shine
	{130,124,118,110,104,102,106,114,124,134,142,148,152,154,156},
	// 87: J-Pop Air
	{124,120,114,108,102,100,104,112,122,132,140,146,150,152,154},
	// 88: Anime Song
	{128,122,116,108,102,100,106,116,128,138,145,150,152,152,150},
	// 89: Orchestra Hall
	{118,114,110,106,102,100,102,108,116,124,130,132,130,126,122},
	// 90: Live Stage 2
	{130,126,122,116,110,102, 98,100,106,114,122,128,132,136,140},
	// 91: Mastering Light
	{ 98, 98, 99,100,101,102,103,104,104,104,103,102,101,100,100},
	// 92: Sub Tight
	{165,158,148,134,120,108,100, 96, 94, 92, 92, 92, 92, 92, 92},
	// 93: Deep House
	{160,154,145,132,118,105, 98, 96,100,110,122,132,140,146,150},
	// 94: Trance Lift
	{145,138,130,118,108,100, 98,102,112,124,136,146,154,160,165},
	// 95: Techno Edge
	{152,146,138,126,114,104,100,102,110,120,132,142,150,156,160},
	// 96: Drum&Bass
	{170,165,155,140,125,110,102,100,105,115,128,138,146,152,156},
	// 97: Soft Classical
	{112,110,108,106,104,102,102,104,108,114,120,124,126,126,124},
	// 98: Speech Intelligibility
	{ 90, 90, 92, 96,102,118,136,150,158,152,138,120,104, 96, 90},
	// 99: AM Safe Narrow
	{ 84, 86, 90, 96,104,116,124,128,128,122,114,102, 92, 86, 82},
	// 100: FM Hi-Fi Safe
	{ 96, 98,100,104,110,118,126,132,136,134,128,122,116,110,106}
};

// ============================================================
//  高精度 倍精度 Direct Form II Transposed バイクワッドフィルタ
//  25Hz〜16kHz の全帯域（特に極低域 25/40/63Hz）で桁落ちや
//  係数誤差・リミットサイクルノイズを出さないよう内部演算を double 化。
//  bypass==1 のときは完全スルーで演算コストを削減する。
// ============================================================
typedef struct {
	double b0, b1, b2, a1, a2;
	double s1, s2;
	int    bypass;
} BiquadD;

// ============================================================
//  位相整合 3バンド Linkwitz-Riley 4次 (LR4) ダイナミック保護
//  低域 (<220Hz) / 中域 (220〜3200Hz) / 高域 (>3200Hz) に完全位相整合で分割し、
//  過大ブーストされた帯域「だけ」を滑らかにソフト保護する。
//  → 低周波を+12dB上げても中・高域の音量は 1.000 のまま一切下がらない！
//  → 高周波を+12dB上げても低・中域の音量は 1.000 のまま一切下がらない！
//  → 閾値以下ではゲイン 1.000（完全透明・無加工）。
// ============================================================
typedef struct {
	BiquadD lp1a[MAX_CH], lp1b[MAX_CH]; // 220Hz LR4 ローパス (2次Butterworth×2)
	BiquadD hp1a[MAX_CH], hp1b[MAX_CH]; // 220Hz LR4 ハイパス (2次Butterworth×2)
	BiquadD lp2a[MAX_CH], lp2b[MAX_CH]; // 3200Hz LR4 ローパス
	BiquadD hp2a[MAX_CH], hp2b[MAX_CH]; // 3200Hz LR4 ハイパス
	BiquadD apLow[MAX_CH];              // 3200Hz 2次オールパス (Low経路の位相整合用)
	float   env[3];                     // ステレオリンク包絡線 [Low, Mid, High]
	float   envCh[MAX_CH][3];           // 多ch用 チャンネル別包絡線
	float   atkCoeff[3];                // バンド別アタック係数
	float   relCoeff[3];                // バンド別リリース係数
	int     rate;
} MultibandProtector;

// ============================================================
//  チャンネル別 EQ / トーンフィルタ状態 (最大8ch、完全固定配列)
// ============================================================
typedef struct {
	BiquadD eqFilters[EQ_BANDS];        // 15バンド ISO 2/3オクターブ定Q EQ
	BiquadD clarityPeak;                // 鮮明さ: 3.8kHz プレゼンスピーキング
	BiquadD clarityAir;                 // 鮮明さ: 10kHz エアハイシェルフ
	BiquadD bassBalanceFilter;          // バランス: 低域チルトローシェルフ
	BiquadD trebleBalanceFilter;        // バランス: 高域チルトハイシェルフ
	BiquadD densityLow;                 // 密度: 220Hz 中低域の厚み
	BiquadD densityMid;                 // 密度: 700Hz 充実感
	BiquadD bassMonoLP;                 // 立体感(Spatial)時の低域120Hzモノラル基準用
	float   harmonicState;              // 密度ブースト時の微細ハーモニック用DC除去状態
} ChannelState;

// ============================================================
//  立体環境音響エンジン遅延バッファ定数 (すべて2の累乗 → ビットマスク高速化)
//  1バンク合計 約 7.34 MB (VirtualAlloc 一括確保、再生中 malloc/free ゼロ)
// ============================================================
enum {
	ENV_FDN_LINES   = 8,
	ENV_DIFF_STAGES = 4,
	ENV_ER_LEN      = 131072, // 192kHz で約 682ms (プリディレイ＋16タップ初期反射)
	ENV_ER_MASK     = ENV_ER_LEN - 1,
	ENV_FDN_LEN     = 65536,  // 192kHz で約 341ms × 8ライン (Hadamard FDN)
	ENV_FDN_MASK    = ENV_FDN_LEN - 1,
	ENV_YAMA_LEN    = 524288, // 192kHz で約 2.73秒 × 2ch (ステレオピンポン山彦)
	ENV_YAMA_MASK   = ENV_YAMA_LEN - 1,
	ENV_FLUT_LEN    = 16384,  // 192kHz で約 85ms × 2ch (平行壁フラッターエコー)
	ENV_FLUT_MASK   = ENV_FLUT_LEN - 1,
	ENV_COMB_LEN    = 8192,   // 192kHz で約 42ms × 2ch (定在波コム共鳴)
	ENV_COMB_MASK   = ENV_COMB_LEN - 1,
	ENV_DOPP_LEN    = 4096,   // 192kHz で約 21ms × 2ch (分数遅延ドップラー)
	ENV_DOPP_MASK   = ENV_DOPP_LEN - 1,
	ENV_SHIM_LEN    = 8192,   // +1オクターブ デュアルヘッド・シマー用リングバッファ
	ENV_SHIM_MASK   = ENV_SHIM_LEN - 1,
	ENV_DIFF_LEN    = 2048,   // 入力4段オールパスディフューザー
	ENV_DIFF_MASK   = ENV_DIFF_LEN - 1,
	ENV_POOL_FLOATS = (2 * ENV_ER_LEN) + (ENV_FDN_LINES * ENV_FDN_LEN)
	                + (2 * ENV_YAMA_LEN) + (2 * ENV_FLUT_LEN)
	                + (2 * ENV_COMB_LEN) + (2 * ENV_DOPP_LEN)
	                + (2 * ENV_SHIM_LEN) + (2 * ENV_DIFF_STAGES * ENV_DIFF_LEN)
};

// ============================================================
//  プロフェッショナル 100環境 立体物理音響エンジン状態 (ステレオ完全独立)
// ============================================================
typedef struct {
	// VirtualAlloc プールへのスライスポインタ (再生中動的確保ゼロ)
	float* erBuf[2];
	float* fdnBuf[ENV_FDN_LINES];
	float* yamaBuf[2];
	float* flutBuf[2];
	float* combBuf[2];
	float* doppBuf[2];
	float* shimBuf[2];
	float* diffBuf[2][ENV_DIFF_STAGES];

	// リングバッファ書き込み位置
	int erPos;
	int fdnPos[ENV_FDN_LINES];
	int yamaPos;
	int flutPos;
	int combPos;
	int doppPos;
	int shimPos;
	int diffPos[2][ENV_DIFF_STAGES];

	// プリ計算済み空間物理パラメータ (プリセット/効果量変更時のみ再計算)
	int   rate;
	int   presetIdx;
	int   effAmount;
	int   preDelaySamp;
	int   erDelayL[8];          // 左耳 8反射タップ遅延 [samples]
	int   erDelayR[8];          // 右耳 8反射タップ遅延 [samples]
	float erGainLL[8];          // L→L 初期反射ゲイン
	float erGainRL[8];          // R→L 初期反射クロスゲイン
	float erGainRR[8];          // R→R 初期反射ゲイン
	float erGainLR[8];          // L→R 初期反射クロスゲイン
	float erToneCoeff;          // 初期反射の材質吸音 1次LPF係数

	int   fdnDelay[ENV_FDN_LINES];      // FDN 各ライン遅延長 [samples]
	float fdnGainMid[ENV_FDN_LINES];    // FDN 各ライン中域フィードバックゲイン (RT60厳密算出)
	float fdnGainLowMul[ENV_FDN_LINES]; // FDN 各ライン低域ブースト/カット比率
	float fdnGainHigh[ENV_FDN_LINES];   // FDN 各ライン高域目標ゲイン
	float fdnDampAlpha[ENV_FDN_LINES];  // FDN 各ライン高域ダンピングLPF係数
	float fdnLowAlpha;                  // FDN 低域シェルフ分割LPF係数 (280Hz)
	float fdnModDepthSamp;              // FDN 内部モジュレーション深さ [samples]
	float fdnModInc[ENV_FDN_LINES];     // FDN 各ライン独立LFO位相増分 (非同期揺らぎ)
	float fdnModPhase[ENV_FDN_LINES];   // FDN 各ラインLFO位相 [0, 2π)
	float fdnDampState[ENV_FDN_LINES];  // FDN 各ライン高域ダンピングLPF状態
	float fdnLowState[ENV_FDN_LINES];   // FDN 各ライン低域シェルフLPF状態
	float fdnDcPrevIn[ENV_FDN_LINES];   // FDN DCブロッカー入力状態
	float fdnDcPrevOut[ENV_FDN_LINES];  // FDN DCブロッカー出力状態

	int   diffDelay[2][ENV_DIFF_STAGES];// 入力ディフューザー遅延長
	float diffGain;                     // 入力ディフューザー係数 (diffusion由来)

	// 山彦 (ステレオピンポン)
	int   yamaDelayL[4];
	int   yamaDelayR[4];
	float yamaGain[4];
	float yamaPan;
	float yamaToneAlpha;
	float yamaToneState[2];
	float yamaFb;

	// 平行壁フラッターエコー
	int   flutDelay[2];
	float flutFb;
	float flutMix;
	float flutLpState[2];

	// 定在波コム共鳴
	int   combDelay[2];
	float combFb;
	float combMix;
	float combLpState[2];

	// 分数遅延ドップラー
	float doppBaseSamp;
	float doppDepthSamp;
	float doppPhaseInc;
	float doppPhase;
	float doppMix;

	// デュアルヘッド +1オクターブ・シマー
	float shimGrainLen;
	float shimPhase[2];
	float shimMix;
	float shimFb;
	float shimLpState[2];

	// 4段ステレオ・オールパス・フェイザー
	float phaserPhase;
	float phaserInc;
	float phaserDepth;
	float phaserState[2][4];
	float phaserFbState[2];

	// 屋外の風 (非周期3成分ゆらぎ)
	float windPhase1;
	float windPhase2;
	float windAmount;

	// 空間共鳴・材質・音色整形フィルタ (L/R 独立倍精度 Biquad)
	BiquadD wetLpf[2];          // 空間ローパス
	BiquadD wetHpf[2];          // 空間ハイパス
	BiquadD resonanceEq[2];     // 空間固有共鳴 (resonanceFreq / resonanceQ に実ゲイン付与)
	BiquadD materialEq[2];      // 材質特性 (木質/コンクリ/吸音)
	BiquadD metallicEq1[2];     // 金属空間 2.4kHz 高Q共鳴
	BiquadD metallicEq2[2];     // 金属空間 4.8kHz 高Q共鳴
	BiquadD glassEq[2];         // ガラス空間 6.5kHz 透明反射
	BiquadD warmthEq[2];        // 温かみ＋重み ローシェルフ
	BiquadD colorAirEq[2];      // reverbColor / brightness / airAbsorption ハイシェルフ
	BiquadD exciterHp[2];       // エキサイター用 5.5kHz HPF
	BiquadD directMediumLp[2];  // 水中(15)等の媒質ダイレクトLPF
	BiquadD deadProximityEq[2]; // 極デッド小空間の近接吸音カーブ

	// 初期反射トーン状態・柔らかさLPF状態
	float erLpState[2];
	float softLpState[2];
	float softAlpha;
	float directMediumMix;      // 水中等のドライ媒質フィルタ適用率 (通常 0.0)
	float deadProximityMix;     // 極デッド小空間の吸音適用率
	float dryGain;              // 最終ドライレベル (音の芯を維持)
	float wetGain;              // 最終ウェットレベル
	float earlyMix;             // 初期反射ミックス比
	float lateMix;              // 後期FDN残響ミックス比
	float wetStereoWidth;       // ウェット空間のステレオ広がり
	float finalStereoWidth;     // 出力全体のステレオ幅 (安全範囲クランプ済み)
	float exciterAmt;           // エキサイター量
	float distAmt;              // 歪み量 (廃墟・SF空間用)
	int   isReverseSpace;       // 86: 逆再生空間スウェル有効フラグ
	int   isTimeStop;           // 87: タイムストップ室静止フェイザーフラグ
	int   hasYamabiko;          // 山彦有効フラグ
} EnvEngineState;

// ============================================================
//  排他制御 (std::mutex を使わず Win32 SRWLOCK でゼロ確保・超高速化)
// ============================================================
static SRWLOCK g_eqLock = SRWLOCK_INIT;
struct EqSrwGuard {
	SRWLOCK* lk;
	explicit EqSrwGuard(SRWLOCK* p) : lk(p) { AcquireSRWLockExclusive(lk); }
	~EqSrwGuard() { ReleaseSRWLockExclusive(lk); }
	EqSrwGuard(const EqSrwGuard&) = delete;
	EqSrwGuard& operator=(const EqSrwGuard&) = delete;
};

// ===== Global State (完全固定配列 + VirtualAlloc オンデマンドプール) =====
enum { EQ_BANKS = 2 };
static int g_eqCur = 0;
static ChannelState g_channels[EQ_BANKS][MAX_CH];
static MultibandProtector g_mbProt[EQ_BANKS];
static EnvEngineState g_envEngine[EQ_BANKS];
// 環境遅延プール: 1バンク約 7.34MB。静的 BSS を肥大させず必要時 1回だけ VirtualAlloc する。
static float* g_envPoolMem[EQ_BANKS] = { NULL, NULL };

static float* EnsureEnvPool(int bank)
{
	if (bank < 0 || bank >= EQ_BANKS) bank = 0;
	if (!g_envPoolMem[bank]) {
		const SIZE_T bytes = (SIZE_T)ENV_POOL_FLOATS * sizeof(float);
		g_envPoolMem[bank] = (float*)VirtualAlloc(NULL, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	}
	return g_envPoolMem[bank];
}

static void BindEnvPoolPointers(EnvEngineState* es, float* base)
{
	if (!base) {
		for (int c = 0; c < 2; ++c) {
			es->erBuf[c] = NULL;
			es->yamaBuf[c] = NULL;
			es->flutBuf[c] = NULL;
			es->combBuf[c] = NULL;
			es->doppBuf[c] = NULL;
			es->shimBuf[c] = NULL;
			for (int s = 0; s < ENV_DIFF_STAGES; ++s) es->diffBuf[c][s] = NULL;
		}
		for (int k = 0; k < ENV_FDN_LINES; ++k) es->fdnBuf[k] = NULL;
		return;
	}
	float* p = base;
	for (int c = 0; c < 2; ++c) { es->erBuf[c] = p; p += ENV_ER_LEN; }
	for (int k = 0; k < ENV_FDN_LINES; ++k) { es->fdnBuf[k] = p; p += ENV_FDN_LEN; }
	for (int c = 0; c < 2; ++c) { es->yamaBuf[c] = p; p += ENV_YAMA_LEN; }
	for (int c = 0; c < 2; ++c) { es->flutBuf[c] = p; p += ENV_FLUT_LEN; }
	for (int c = 0; c < 2; ++c) { es->combBuf[c] = p; p += ENV_COMB_LEN; }
	for (int c = 0; c < 2; ++c) { es->doppBuf[c] = p; p += ENV_DOPP_LEN; }
	for (int c = 0; c < 2; ++c) { es->shimBuf[c] = p; p += ENV_SHIM_LEN; }
	for (int c = 0; c < 2; ++c) {
		for (int s = 0; s < ENV_DIFF_STAGES; ++s) {
			es->diffBuf[c][s] = p; p += ENV_DIFF_LEN;
		}
	}
}

// ============================================================
//  リサンプリング用 再利用ワークプール (毎ブロック malloc/free を完全撤廃)
// ============================================================
typedef struct {
	unsigned char* pcmBuf;
	int            pcmCap;
	float*         srcFloat;
	int            srcCap;
	float*         dstFloat;
	int            dstCap;
} ResampleWorkPool;

static ResampleWorkPool g_rsPool[EQ_BANKS] = {};

static void* EnsureResampleBuf(void** slot, int* capBytes, int needBytes)
{
	if (needBytes <= 0) return NULL;
	if (*slot && *capBytes >= needBytes) return *slot;
	if (*slot) {
		VirtualFree(*slot, 0, MEM_RELEASE);
		*slot = NULL;
		*capBytes = 0;
	}
	int allocBytes = (needBytes + 65535) & ~65535;
	void* p = VirtualAlloc(NULL, (SIZE_T)allocBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!p) return NULL;
	*slot = p;
	*capBytes = allocBytes;
	return p;
}

static int g_lastRate[EQ_BANKS] = { 0, 0 };
static int g_lastEqPreset[EQ_BANKS] = { -1, -1 };
static int g_lastEnvPreset[EQ_BANKS] = { -1, -1 };
static int g_lastEqValues[EQ_BANKS][15];
static int g_lastExtendedParams[EQ_BANKS][5];
static int g_lastEffectAmount[EQ_BANKS] = { 50, 50 };

// ===== 追加エフェクトパラメータ (リバーブ/コーラス/ディレイ) =====
// equaliser() で savedata から取り込む。
//   値域 0-200、0 = オフ (既定値)
//   1-100   : モードA  強さ = v / 100
//   101-200 : モードB  強さ = (v - 100) / 100
//     g_eqReverb : 1-100 リバーブ / 101-200 パンリバーブ
//     g_eqChorus : 1-100 コーラス / 101-200 コーラスディストーション
//     g_eqDelay  : 1-100 ディレイ / 101-200 マルチディレイ
static int g_eqReverb = 0;
static int g_eqChorus = 0;
static int g_eqDelay = 0;
static BOOL g_initialized[EQ_BANKS] = { FALSE, FALSE };

// ===== EQ 中心周波数 (ISO 2/3オクターブ 15バンド) =====
static const float EQ_FREQS[EQ_BANDS] = {
	   25.0f, 40.0f, 63.0f, 100.0f, 160.0f,
	250.0f, 400.0f, 630.0f, 1000.0f, 1600.0f,
	2500.0f, 4000.0f, 6300.0f, 10000.0f, 16000.0f
};

// External references (assumed to exist in main program)
extern int wavbit_sample_Hz, wavchannel, wavsam_depth;

// ===== 拡張環境パラメータ構造体（65パラメータ） =====
typedef struct {
	// ===== 基本パラメータ（従来互換） =====
	float wetMix;           // ウェット/ドライミックス (0.0-1.0)
	float delayTimeMs;      // メインディレイタイム (ms)
	float feedback;         // フィードバック量 (0.0-1.0)

	// ===== 初期反射（16タップに拡張） =====
	float earlyRef[16];     // [ms, gain, ms, gain, ...] の順で8ペア

	// ===== フィルタ =====
	float lpfFreq;          // ローパスフィルタ周波数 (Hz)
	float hpfFreq;          // ハイパスフィルタ周波数 (Hz)

	// ===== 空間・モジュレーション =====
	float stereoWidth;      // ステレオ幅 (0.3-2.5)
	float modDepth;         // モジュレーション深さ (0.0-1.0)
	float modSpeed;         // モジュレーション速度 (Hz)
	float exciterAmount;    // エキサイター量 (0.0-1.0)
	float diffusion;        // ディフュージョン (0.0-1.0)
	float preDelayMs;       // プリディレイ (ms)
	float damping;          // ダンピング (0.0-1.0)
	float roomSize;         // 部屋サイズ倍率 (0.2-5.0)

	// ===== バランス・密度（従来） =====
	float earlyLateBalance; // 初期/後期残響バランス (0.0-1.0)
	float density;          // 残響密度 (0.0-1.0)
	float airAbsorption;    // 空気吸収 (0.0-1.0)

	// ===== リバーブ詳細制御 =====
	float earlyReverbDecay; // 初期残響減衰速度 (0.1=速い, 2.0=遅い)
	float lateReverbDecay;  // 後期残響減衰速度 (0.1=速い, 3.0=遅い)
	float reverbSmooth;     // 残響の滑らかさ (0.0=粗い, 1.0=滑らか)
	float reverbColor;      // 残響の色味 (0.0=暗い, 1.0=明るい)

	// ===== 周波数帯域別残響時間 =====
	float bassReverbTime;   // 低域残響時間倍率 (0.5=短い, 2.0=長い)
	float midReverbTime;    // 中域残響時間倍率 (0.5-2.0)
	float trebleReverbTime; // 高域残響時間倍率 (0.5-2.0)

	// ===== 周波数帯域別拡散度 =====
	float bassDiffusion;    // 低域拡散度 (0.0-1.0)
	float trebleDiffusion;  // 高域拡散度 (0.0-1.0)

	// ===== エコー特性 =====
	float echoClarity;      // エコー明瞭度 (0.0=不明瞭, 1.0=明瞭)
	float echoFeedbackTone; // フィードバック音色変化 (-1.0=暗く, 1.0=明るく)

	// ===== 材質・表面特性 =====
	float materialAbsorption; // 材質吸音率 (0.0=反射, 1.0=吸音)
	float surfaceRoughness;   // 表面粗さ (0.0=滑らか, 1.0=粗い)
	float warmth;             // 温かみ (0.0=冷たい, 1.0=温かい)
	float brightness;         // 明るさ (0.0=暗い, 1.0=明るい)
	float softness;           // 柔らかさ (0.0=硬い, 1.0=柔らかい)
	float weight;             // 音の重さ (0.0=軽い, 1.0=重い)

	// ===== 空間幾何学 =====
	float ceilingHeight;    // 天井高さ影響 (0.5=低い, 2.0=高い)
	float wallDistance;     // 壁距離感 (0.5=近い, 2.0=遠い)
	float openness;         // 開放度 (0.0=密閉, 1.0=開放)

	// ===== 特殊効果 =====
	float flutterEcho;      // フラッターエコー強度 (0.0-1.0)
	float combFiltering;    // コムフィルタリング (0.0-1.0)

	// ===== 山彦専用パラメータ =====
	float yamabikoDelays[4];    // エコー遅延時間 [ms]
	float yamabikoGains[4];     // 各エコーゲイン (0.0-1.0)
	float yamabikoDecay;        // エコー減衰カーブ (0.5=急, 1.5=緩やか)
	float yamabikoPan;          // エコーのパン広がり (0.0-1.0)

	// ===== 空間特性詳細 =====
	float spaceComplexity;      // 空間複雑さ (0.0=単純, 1.0=複雑)
	float reflectionDensity;    // 反射密度 (0.0=疎, 1.0=密)
	float resonanceFreq;        // 共鳴周波数 [Hz]
	float resonanceQ;           // 共鳴Q値 (0.5-5.0)

	// ===== 材質特性詳細 =====
	float metallic;             // 金属感 (0.0-1.0)
	float glassiness;           // ガラス感 (0.0-1.0)
	float woodiness;            // 木質感 (0.0-1.0)
	float concrete;             // コンクリート感 (0.0-1.0)

	// ===== 環境要素 =====
	float humidity;             // 湿度 (0.0=乾燥, 1.0=多湿)
	float altitude;             // 高度感 (0.0=低地, 1.0=高地)
	float enclosure;            // 密閉度 (0.0=開放, 1.0=密閉)
	float windEffect;           // 風の影響 (0.0-1.0)

	// ===== 特殊効果詳細 =====
	float shimmer;              // きらめき (0.0-1.0)
	float doppler;              // ドップラー効果 (0.0-1.0)
	float distortion;           // 歪み (0.0-1.0)
	float phasing;              // フェイジング (0.0-1.0)

	// ===== 空間タイプ =====
	int type;               // 空間タイプ（新分類）
} EnvParams;

// ===== 空間タイプ定義 =====
enum SpaceType {
	TYPE_NONE = 0,              // なし
	TYPE_SMALL_ROOM = 1,        // 小部屋
	TYPE_MEDIUM_ROOM = 2,       // 中部屋
	TYPE_LARGE_HALL = 3,        // 大ホール
	TYPE_CATHEDRAL = 4,         // 超大空間
	TYPE_OUTDOOR_OPEN = 5,      // 屋外開放
	TYPE_MOUNTAIN_ECHO = 6,     // 山エコー専用
	TYPE_CANYON_ECHO = 7,       // 峡谷エコー専用
	TYPE_CAVE = 8,              // 洞窟系
	TYPE_METAL_SPACE = 9,       // 金属空間
	TYPE_UNDERWATER = 10,       // 水系
	TYPE_CORRIDOR = 11,         // 通路系
	TYPE_SF_SPACE = 12          // SF特殊空間
};

// 環境プリセット数
#define ENV_PRESET_COUNT 101

static const EnvParams ENV_PRESETS[ENV_PRESET_COUNT] = {
	// 0: なし - 音響処理オフ。ドライ信号をそのまま通す
	{
		0.00f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f },
		20000.00f, 20.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.00f, 8.00f, 0.00f, 1.00f, 0.50f, 0.00f, 0.00f,
		1.00f, 1.00f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.30f, 0.27f, 0.67f, 0.00f, 0.00f,
		0.00f, 0.50f, 0.50f, 0.00f, 0.00f, 1.00f, 1.00f, 0.00f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.00f, 0.00f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.00f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_NONE
	},

	// 1: 風呂場 - タイル密閉の小空間。2-15msの初期反射、RT60約0.6s、平行壁の金属フラッターと3kHz帯のタイル共鳴、高湿度
	{
		0.50f, 14.00f, 0.29f,
		{ 2.00f, 0.70f, 3.55f, 0.57f, 5.29f, 0.47f, 7.23f, 0.39f, 9.41f, 0.32f, 11.85f, 0.26f, 14.58f, 0.21f, 17.64f, 0.17f },
		17500.00f, 180.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.28f, 4.00f, 0.30f, 0.50f, 0.50f, 0.65f, 0.05f,
		0.37f, 0.44f, 0.50f, 0.82f, 0.90f, 1.00f, 1.10f, 0.42f, 0.53f, 0.63f, 0.00f, 0.20f,
		0.25f, 0.35f, 0.82f, 0.15f, 0.30f, 0.70f, 0.60f, 0.05f, 0.28f, 0.15f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.30f, 0.60f, 3200.00f, 2.60f,
		0.42f, 0.25f, 0.00f, 0.35f,
		0.85f, 0.00f, 0.90f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 2: ホール - 中規模ホール。RT60約1.8s、12-130msの密な初期反射、バランス型の拡散残響、適度な空気吸収
	{
		0.40f, 80.00f, 0.42f,
		{ 12.00f, 0.50f, 22.00f, 0.43f, 33.50f, 0.37f, 46.73f, 0.32f, 61.93f, 0.27f, 79.42f, 0.24f, 99.54f, 0.20f, 122.67f, 0.17f },
		11000.00f, 65.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.70f, 22.00f, 0.45f, 2.20f, 0.50f, 0.72f, 0.35f,
		0.52f, 0.92f, 0.50f, 0.60f, 1.00f, 1.00f, 1.00f, 0.26f, 0.23f, 0.43f, 0.00f, 0.40f,
		0.35f, 0.55f, 0.60f, 0.50f, 0.50f, 1.50f, 1.40f, 0.35f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.70f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 3: 教会 - 石造りの教会。RT60約4.5s、20ms以降の長い初期反射、高密度拡散、石の重い低域残響、35msプリディレイ
	{
		0.55f, 140.00f, 0.71f,
		{ 20.00f, 0.48f, 38.00f, 0.42f, 58.88f, 0.37f, 83.10f, 0.33f, 111.20f, 0.29f, 143.79f, 0.25f, 181.59f, 0.22f, 225.45f, 0.20f },
		9000.00f, 55.00f, 1.60f, 0.00f, 0.00f, 0.00f, 0.82f, 35.00f, 0.55f, 3.60f, 0.50f, 0.85f, 0.45f,
		0.84f, 2.00f, 0.50f, 0.50f, 1.40f, 1.20f, 0.85f, 0.42f, 0.29f, 0.38f, 0.00f, 0.35f,
		0.50f, 0.55f, 0.50f, 0.50f, 0.70f, 1.90f, 1.80f, 0.40f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.85f, 1000.00f, 1.00f,
		0.00f, 0.20f, 0.00f, 0.60f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CATHEDRAL
	},

	// 4: 洞窟 - 岩の洞窟。RT60約2.8s、不規則な初期反射、暗いLPF3.5kHz、高湿度、粗い岩面の拡散と低域共鳴
	{
		0.60f, 110.00f, 0.53f,
		{ 9.00f, 0.55f, 22.00f, 0.46f, 37.60f, 0.39f, 56.32f, 0.33f, 78.78f, 0.27f, 105.74f, 0.23f, 138.09f, 0.19f, 176.91f, 0.16f },
		3500.00f, 55.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.55f, 18.00f, 0.70f, 2.80f, 0.50f, 0.60f, 0.35f,
		0.64f, 1.32f, 0.50f, 0.25f, 1.50f, 1.00f, 0.60f, 0.33f, 0.14f, 0.50f, 0.00f, 0.40f,
		0.80f, 0.50f, 0.25f, 0.45f, 0.70f, 1.40f, 1.60f, 0.20f, 0.15f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.60f, 250.00f, 1.80f,
		0.00f, 0.00f, 0.00f, 0.30f,
		0.70f, 0.00f, 0.80f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CAVE
	},

	// 5: スタジオ - 吸音処理されたスタジオ。RT60約0.25s、極小の初期反射、高ダンピングでほぼ無響、ウェット僅少
	{
		0.08f, 8.00f, 0.25f,
		{ 3.00f, 0.35f, 6.00f, 0.21f, 9.36f, 0.13f, 13.12f, 0.08f, 17.34f, 0.05f, 22.06f, 0.03f, 27.35f, 0.02f, 33.27f, 0.01f },
		15000.00f, 45.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.30f, 3.00f, 0.75f, 0.60f, 0.50f, 0.30f, 0.10f,
		0.33f, 0.30f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.24f, 0.18f, 0.59f, 0.00f, 0.80f,
		0.20f, 0.50f, 0.50f, 0.50f, 0.35f, 1.00f, 1.00f, 0.30f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.20f, 0.30f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 6: ライブハウス - 小規模ライブハウス。RT60約1.0s、木と機材で密なパンチのある反射、中低域の温かみ、明瞭なエネルギー
	{
		0.35f, 45.00f, 0.33f,
		{ 7.00f, 0.55f, 14.00f, 0.47f, 21.84f, 0.40f, 30.62f, 0.34f, 40.46f, 0.29f, 51.47f, 0.24f, 63.81f, 0.21f, 77.62f, 0.18f },
		12000.00f, 70.00f, 1.20f, 0.00f, 0.00f, 0.00f, 0.60f, 12.00f, 0.40f, 1.40f, 0.50f, 0.70f, 0.20f,
		0.42f, 0.60f, 0.50f, 0.65f, 1.15f, 1.00f, 1.00f, 0.43f, 0.25f, 0.45f, 0.00f, 0.40f,
		0.40f, 0.60f, 0.65f, 0.40f, 0.50f, 1.00f, 1.00f, 0.25f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.55f, 0.68f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.40f, 0.00f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 7: 森 - 森林。明確な壁のない拡散音場、葉による高域吸収、疎らな反射、風のゆらぎ、包み込む柔らかさ
	{
		0.30f, 60.00f, 0.35f,
		{ 11.00f, 0.32f, 28.00f, 0.26f, 49.25f, 0.20f, 75.81f, 0.16f, 109.02f, 0.13f, 150.52f, 0.10f, 202.40f, 0.08f, 267.25f, 0.07f },
		8000.00f, 90.00f, 1.60f, 0.00f, 0.00f, 0.00f, 0.90f, 15.00f, 0.60f, 2.50f, 0.50f, 0.40f, 0.50f,
		0.44f, 0.68f, 0.50f, 0.45f, 1.00f, 1.00f, 0.70f, 0.35f, 0.19f, 0.45f, 0.00f, 0.55f,
		0.60f, 0.55f, 0.45f, 0.70f, 0.35f, 1.00f, 1.00f, 0.90f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.40f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.25f, 0.00f,
		0.50f, 0.00f, 0.15f, 0.30f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_OUTDOOR_OPEN
	},

	// 8: 山 - 山の谺。遠い斜面からの350/700/1050/1400msの離散エコー、高い開放度・高度感、澄んだ明瞭な反射、風
	{
		0.62f, 350.00f, 0.44f,
		{ 15.00f, 0.35f, 35.00f, 0.29f, 59.00f, 0.24f, 87.80f, 0.19f, 122.36f, 0.16f, 163.83f, 0.13f, 213.60f, 0.11f, 273.32f, 0.09f },
		9000.00f, 70.00f, 1.70f, 0.00f, 0.00f, 0.00f, 0.40f, 25.00f, 0.45f, 3.00f, 0.50f, 0.35f, 0.60f,
		0.54f, 1.00f, 0.50f, 0.50f, 1.00f, 1.00f, 0.80f, 0.26f, 0.22f, 0.85f, 0.00f, 0.40f,
		0.50f, 0.50f, 0.50f, 0.40f, 0.40f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f,
		{ 350.00f, 700.00f, 1050.00f, 1400.00f }, { 0.92f, 0.70f, 0.50f, 0.32f }, 1.20f, 0.70f,
		0.50f, 0.35f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.80f, 0.05f, 0.40f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MOUNTAIN_ECHO
	},

	// 9: 広場 - 都市の広場。舗装面からの反射と開放的な空気感、疎らな拡散、遠い建物の緩いスラップ、微風
	{
		0.28f, 70.00f, 0.37f,
		{ 13.00f, 0.34f, 29.00f, 0.28f, 48.52f, 0.23f, 72.33f, 0.19f, 101.39f, 0.15f, 136.83f, 0.13f, 180.08f, 0.10f, 232.83f, 0.08f },
		10000.00f, 80.00f, 1.50f, 0.00f, 0.00f, 0.00f, 0.60f, 18.00f, 0.50f, 2.60f, 0.50f, 0.40f, 0.45f,
		0.47f, 0.76f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.37f, 0.22f, 0.55f, 0.00f, 0.45f,
		0.40f, 0.50f, 0.55f, 0.45f, 0.40f, 1.00f, 1.00f, 0.95f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.42f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.40f,
		0.30f, 0.00f, 0.10f, 0.25f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_OUTDOOR_OPEN
	},

	// 10: カテドラル - 巨大カテドラル。RT60約6.0s、45msプリディレイ、超高密度拡散、重い低域残響、圧倒的な空間
	{
		0.60f, 160.00f, 0.88f,
		{ 24.00f, 0.46f, 46.00f, 0.41f, 71.96f, 0.37f, 102.59f, 0.34f, 138.74f, 0.30f, 181.39f, 0.27f, 231.72f, 0.24f, 291.11f, 0.22f },
		8500.00f, 50.00f, 1.80f, 0.00f, 0.00f, 0.00f, 0.88f, 45.00f, 0.50f, 4.20f, 0.50f, 0.90f, 0.50f,
		1.02f, 2.60f, 0.50f, 0.50f, 1.50f, 1.25f, 0.85f, 0.41f, 0.29f, 0.36f, 0.00f, 0.30f,
		0.45f, 0.55f, 0.50f, 0.50f, 0.80f, 2.00f, 2.00f, 0.45f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.80f, 0.90f, 180.00f, 1.00f,
		0.00f, 0.20f, 0.00f, 0.55f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CATHEDRAL
	},

	// 11: 体育館 - 体育館。RT60約2.2s、硬い床壁の金属的反射、平行面のフラッターとコム、木床の中域、明るい高域
	{
		0.42f, 95.00f, 0.46f,
		{ 9.00f, 0.55f, 20.00f, 0.48f, 32.32f, 0.42f, 46.12f, 0.36f, 61.57f, 0.32f, 78.88f, 0.27f, 98.27f, 0.24f, 119.98f, 0.21f },
		12000.00f, 75.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.45f, 16.00f, 0.35f, 2.60f, 0.50f, 0.60f, 0.30f,
		0.56f, 1.08f, 0.50f, 0.70f, 1.00f, 1.00f, 1.00f, 0.57f, 0.42f, 0.65f, 0.00f, 0.35f,
		0.40f, 0.40f, 0.70f, 0.30f, 0.45f, 1.40f, 1.60f, 0.35f, 0.40f, 0.20f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.60f, 800.00f, 1.80f,
		0.50f, 0.00f, 0.30f, 0.45f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 12: 峡谷 - 峡谷。両岸の壁から交互に返る180/260/420/640msの近接エコー、広いパン、岩の反射、開放的な高度感
	{
		0.60f, 180.00f, 0.49f,
		{ 14.00f, 0.42f, 32.00f, 0.35f, 53.60f, 0.29f, 79.52f, 0.24f, 110.62f, 0.20f, 147.95f, 0.17f, 192.74f, 0.14f, 246.49f, 0.11f },
		9500.00f, 65.00f, 1.90f, 0.00f, 0.00f, 0.00f, 0.45f, 22.00f, 0.45f, 3.40f, 0.50f, 0.40f, 0.50f,
		0.60f, 1.20f, 0.50f, 0.45f, 1.20f, 1.00f, 1.00f, 0.38f, 0.21f, 0.80f, 0.00f, 0.40f,
		0.55f, 0.50f, 0.45f, 0.40f, 0.50f, 1.00f, 1.00f, 0.90f, 0.00f, 0.00f,
		{ 180.00f, 260.00f, 420.00f, 640.00f }, { 0.90f, 0.72f, 0.50f, 0.32f }, 1.00f, 0.85f,
		0.60f, 0.40f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.40f,
		0.30f, 0.60f, 0.20f, 0.35f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CANYON_ECHO
	},

	// 13: 地下室 - 地下室。RT60約0.9s、狭く密度の高い初期反射、コンクリの中低域、湿り気、暗めで圧迫感のある残響
	{
		0.40f, 35.00f, 0.32f,
		{ 5.00f, 0.60f, 10.00f, 0.51f, 15.60f, 0.43f, 21.87f, 0.37f, 28.90f, 0.31f, 36.76f, 0.27f, 45.58f, 0.23f, 55.45f, 0.19f },
		6500.00f, 70.00f, 0.90f, 0.00f, 0.00f, 0.00f, 0.50f, 8.00f, 0.55f, 1.00f, 0.50f, 0.80f, 0.15f,
		0.41f, 0.56f, 0.50f, 0.35f, 1.30f, 1.00f, 0.70f, 0.42f, 0.18f, 0.47f, 0.00f, 0.35f,
		0.50f, 0.40f, 0.35f, 0.40f, 0.60f, 0.80f, 0.80f, 0.10f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.40f, 0.75f, 300.00f, 1.60f,
		0.00f, 0.00f, 0.00f, 0.60f,
		0.50f, 0.00f, 0.85f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 14: 劇場 - 音響設計された劇場。RT60約1.4s、客席吸音、高い拡散と明瞭度、木の温かみ、台詞が通る中域
	{
		0.38f, 70.00f, 0.37f,
		{ 10.00f, 0.48f, 19.00f, 0.41f, 29.08f, 0.35f, 40.37f, 0.29f, 53.01f, 0.25f, 67.18f, 0.21f, 83.04f, 0.18f, 100.80f, 0.15f },
		11000.00f, 70.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.78f, 18.00f, 0.50f, 2.00f, 0.50f, 0.70f, 0.30f,
		0.47f, 0.76f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.44f, 0.22f, 0.80f, 0.00f, 0.50f,
		0.35f, 0.60f, 0.55f, 0.50f, 0.45f, 1.40f, 1.30f, 0.30f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.65f, 0.70f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.45f, 0.00f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 15: 水中 - 水中。LPF約1.1kHzの暗くこもった音、高湿度、遅く揺らぐモジュレーション、密度の高い減衰、重い低域
	{
		0.60f, 40.00f, 0.39f,
		{ 6.00f, 0.50f, 12.00f, 0.40f, 18.72f, 0.32f, 26.25f, 0.26f, 34.68f, 0.20f, 44.12f, 0.16f, 54.69f, 0.13f, 66.53f, 0.10f },
		1100.00f, 50.00f, 1.20f, 0.30f, 0.40f, 0.00f, 0.60f, 6.00f, 0.85f, 1.60f, 0.50f, 0.70f, 0.10f,
		0.48f, 0.80f, 0.50f, 0.10f, 1.60f, 1.00f, 0.40f, 0.23f, 0.10f, 0.44f, 0.00f, 0.50f,
		0.30f, 0.50f, 0.10f, 0.80f, 0.70f, 1.00f, 1.00f, 0.20f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.70f, 350.00f, 1.40f,
		0.00f, 0.00f, 0.00f, 0.00f,
		1.00f, 0.00f, 0.70f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_UNDERWATER
	},

	// 16: トンネル/地下道 - 長いトンネル。RT60約2.0s、平行壁の強いフラッターとコムフィルタ、コンクリの反射、狭いステレオ、金属質
	{
		0.48f, 90.00f, 0.44f,
		{ 8.00f, 0.60f, 16.00f, 0.53f, 24.96f, 0.46f, 35.00f, 0.41f, 46.23f, 0.36f, 58.82f, 0.32f, 72.92f, 0.28f, 88.71f, 0.25f },
		8000.00f, 70.00f, 0.80f, 0.00f, 0.00f, 0.00f, 0.40f, 10.00f, 0.45f, 2.20f, 0.50f, 0.60f, 0.30f,
		0.54f, 1.00f, 0.50f, 0.40f, 1.20f, 1.00f, 1.00f, 0.48f, 0.26f, 0.60f, 0.00f, 0.30f,
		0.40f, 0.40f, 0.40f, 0.35f, 0.55f, 0.90f, 1.20f, 0.20f, 0.60f, 0.40f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.45f, 0.60f, 500.00f, 2.20f,
		0.20f, 0.00f, 0.00f, 0.70f,
		0.30f, 0.00f, 0.70f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CORRIDOR
	},

	// 17: アリーナ/ドーム - 巨大ドーム。RT60約3.5s、観客席の吸音、遅く広い残響、高い天井の空気感、遠いPAスラップ
	{
		0.50f, 130.00f, 0.60f,
		{ 18.00f, 0.42f, 36.00f, 0.37f, 57.24f, 0.32f, 82.30f, 0.28f, 111.88f, 0.24f, 146.78f, 0.21f, 187.96f, 0.18f, 236.55f, 0.16f },
		9000.00f, 60.00f, 1.70f, 0.00f, 0.00f, 0.00f, 0.75f, 30.00f, 0.55f, 3.80f, 0.50f, 0.80f, 0.50f,
		0.72f, 1.60f, 0.50f, 0.50f, 1.30f, 1.00f, 1.00f, 0.40f, 0.20f, 0.55f, 0.00f, 0.55f,
		0.40f, 0.50f, 0.50f, 0.50f, 0.60f, 1.90f, 1.90f, 0.60f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.80f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.50f,
		0.30f, 0.00f, 0.50f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CATHEDRAL
	},

	// 18: 小部屋/クローゼット - 超小空間・クローゼット。RT60約0.3s、極短の初期反射、衣類等の吸音でほぼデッド、密閉感
	{
		0.12f, 6.00f, 0.25f,
		{ 2.00f, 0.40f, 4.00f, 0.28f, 6.24f, 0.20f, 8.75f, 0.14f, 11.56f, 0.10f, 14.71f, 0.07f, 18.23f, 0.05f, 22.18f, 0.03f },
		9000.00f, 90.00f, 0.70f, 0.00f, 0.00f, 0.00f, 0.30f, 3.00f, 0.70f, 0.35f, 0.50f, 0.40f, 0.05f,
		0.34f, 0.32f, 0.50f, 0.40f, 1.00f, 1.00f, 1.00f, 0.36f, 0.17f, 0.54f, 0.00f, 0.70f,
		0.35f, 0.50f, 0.40f, 0.60f, 0.35f, 0.60f, 0.50f, 0.02f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.25f, 0.40f, 1500.00f, 1.00f,
		0.00f, 0.00f, 0.30f, 0.00f,
		0.30f, 0.00f, 0.95f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 19: 階段室 - 階段室。RT60約1.8s、縦方向に折り返す螺旋的な反射、高い天井係数、コンクリのフラッター、複雑な反射
	{
		0.45f, 55.00f, 0.42f,
		{ 6.00f, 0.55f, 13.00f, 0.48f, 21.40f, 0.42f, 31.48f, 0.36f, 43.58f, 0.32f, 58.09f, 0.27f, 75.51f, 0.24f, 96.41f, 0.21f },
		9500.00f, 75.00f, 0.90f, 0.00f, 0.00f, 0.00f, 0.50f, 10.00f, 0.40f, 1.50f, 0.50f, 0.65f, 0.25f,
		0.52f, 0.92f, 0.50f, 0.45f, 1.00f, 1.00f, 1.00f, 0.45f, 0.29f, 0.58f, 0.00f, 0.35f,
		0.40f, 0.40f, 0.45f, 0.35f, 0.50f, 2.00f, 1.00f, 0.20f, 0.35f, 0.30f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.65f, 600.00f, 2.00f,
		0.25f, 0.00f, 0.00f, 0.55f,
		0.30f, 0.00f, 0.75f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CORRIDOR
	},

	// 20: 地下鉄ホーム - 地下鉄ホーム。RT60約2.4s、都市コンクリの硬い反射、フラッター、遠いトンネルへの抜け、金属的な高域
	{
		0.48f, 100.00f, 0.48f,
		{ 9.00f, 0.55f, 21.00f, 0.48f, 34.44f, 0.43f, 49.49f, 0.37f, 66.35f, 0.33f, 85.23f, 0.29f, 106.38f, 0.26f, 130.07f, 0.22f },
		9000.00f, 90.00f, 1.20f, 0.00f, 0.00f, 0.00f, 0.45f, 14.00f, 0.50f, 2.60f, 0.50f, 0.70f, 0.35f,
		0.59f, 1.16f, 0.50f, 0.42f, 1.20f, 1.00f, 1.00f, 0.50f, 0.29f, 0.60f, 0.00f, 0.35f,
		0.45f, 0.40f, 0.42f, 0.35f, 0.55f, 1.00f, 1.30f, 0.25f, 0.30f, 0.25f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.70f, 420.00f, 1.80f,
		0.30f, 0.00f, 0.00f, 0.75f,
		0.30f, 0.00f, 0.70f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CORRIDOR
	},

	// 21: 倉庫 - 倉庫。RT60約2.6s、鉄骨とコンクリの硬い反射、高い天井、平行面のフラッター、乾いた中高域
	{
		0.46f, 105.00f, 0.51f,
		{ 10.00f, 0.52f, 23.00f, 0.46f, 37.56f, 0.40f, 53.87f, 0.35f, 72.13f, 0.31f, 92.59f, 0.27f, 115.50f, 0.24f, 141.16f, 0.21f },
		10000.00f, 65.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.45f, 18.00f, 0.40f, 2.80f, 0.50f, 0.60f, 0.35f,
		0.61f, 1.24f, 0.50f, 0.50f, 1.20f, 1.00f, 1.00f, 0.46f, 0.35f, 0.64f, 0.00f, 0.35f,
		0.45f, 0.45f, 0.50f, 0.35f, 0.55f, 1.60f, 1.60f, 0.35f, 0.35f, 0.25f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.60f, 500.00f, 1.60f,
		0.40f, 0.00f, 0.00f, 0.55f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 22: 廊下 - 屋内廊下。RT60約1.2s、狭い平行壁のフラッターとコム、中程度の反射、狭いステレオ像
	{
		0.40f, 45.00f, 0.35f,
		{ 5.00f, 0.55f, 11.00f, 0.47f, 17.72f, 0.41f, 25.25f, 0.35f, 33.68f, 0.30f, 43.12f, 0.26f, 53.69f, 0.22f, 65.53f, 0.19f },
		10000.00f, 80.00f, 0.70f, 0.00f, 0.00f, 0.00f, 0.40f, 8.00f, 0.45f, 1.30f, 0.50f, 0.55f, 0.20f,
		0.44f, 0.68f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.39f, 0.22f, 0.65f, 0.00f, 0.40f,
		0.40f, 0.45f, 0.50f, 0.40f, 0.45f, 1.00f, 0.70f, 0.15f, 0.45f, 0.35f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.40f, 0.55f, 700.00f, 2.00f,
		0.00f, 0.00f, 0.00f, 0.45f,
		0.30f, 0.00f, 0.70f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CORRIDOR
	},

	// 23: 工場 - 稼働中の工場。RT60約2.8s、金属機械の強い反射と共鳴、僅かな歪み、コンクリ床、明るく硬い響き
	{
		0.48f, 115.00f, 0.53f,
		{ 8.00f, 0.58f, 19.00f, 0.51f, 31.32f, 0.45f, 45.12f, 0.40f, 60.57f, 0.35f, 77.88f, 0.31f, 97.27f, 0.27f, 118.98f, 0.24f },
		11000.00f, 70.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.45f, 16.00f, 0.40f, 3.00f, 0.50f, 0.70f, 0.30f,
		0.64f, 1.32f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.50f, 0.42f, 0.64f, 0.00f, 0.30f,
		0.50f, 0.40f, 0.55f, 0.30f, 0.55f, 1.50f, 1.60f, 0.30f, 0.40f, 0.30f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.70f, 900.00f, 2.20f,
		0.60f, 0.00f, 0.00f, 0.60f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.15f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 24: 寺社仏閣 - 木造の寺社。RT60約1.8s、太い木材の温かい反射と拡散、高い天井、柔らかな中低域、香煙の空気感
	{
		0.42f, 80.00f, 0.42f,
		{ 10.00f, 0.50f, 20.00f, 0.43f, 31.20f, 0.37f, 43.74f, 0.32f, 57.79f, 0.27f, 73.53f, 0.24f, 91.15f, 0.20f, 110.89f, 0.17f },
		9000.00f, 70.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.65f, 18.00f, 0.55f, 2.20f, 0.50f, 0.60f, 0.35f,
		0.52f, 0.92f, 0.50f, 0.45f, 1.20f, 1.00f, 0.80f, 0.54f, 0.20f, 0.44f, 0.00f, 0.50f,
		0.40f, 0.70f, 0.45f, 0.55f, 0.50f, 1.60f, 1.40f, 0.30f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.55f, 0.60f, 280.00f, 1.40f,
		0.00f, 0.00f, 0.70f, 0.00f,
		0.40f, 0.00f, 0.50f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 25: 宇宙空間 - 宇宙空間の演出。反射のほぼ無い真空を象徴する僅かなウェット、極端に広いステレオ、微かなシマーとフェイズ、無限の開放
	{
		0.20f, 20.00f, 0.25f,
		{ 8.00f, 0.20f, 28.00f, 0.14f, 50.40f, 0.10f, 75.49f, 0.07f, 103.59f, 0.05f, 135.06f, 0.03f, 170.30f, 0.02f, 209.78f, 0.02f },
		6000.00f, 60.00f, 2.30f, 0.00f, 0.00f, 0.00f, 0.30f, 5.00f, 0.60f, 5.00f, 0.50f, 0.20f, 0.70f,
		0.36f, 0.40f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.25f, 0.20f, 0.74f, 0.00f, 0.40f,
		0.40f, 0.40f, 0.50f, 0.60f, 0.30f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.40f, 0.20f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.30f, 1.00f, 0.00f, 0.00f,
		0.40f, 0.20f, 0.00f, 0.30f,
		TYPE_SF_SPACE
	},

	// 26: 野球場/サッカー場 - 屋外スタジアム。RT60約2.2s、開放的な空気感、観客の吸音、遠いスタンドからのPAスラップ、風
	{
		0.40f, 120.00f, 0.46f,
		{ 16.00f, 0.40f, 36.00f, 0.33f, 60.00f, 0.27f, 88.80f, 0.22f, 123.36f, 0.18f, 164.83f, 0.15f, 214.60f, 0.12f, 274.32f, 0.10f },
		9000.00f, 75.00f, 1.70f, 0.00f, 0.00f, 0.00f, 0.50f, 25.00f, 0.50f, 3.50f, 0.50f, 0.40f, 0.55f,
		0.56f, 1.08f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.36f, 0.21f, 0.60f, 0.00f, 0.50f,
		0.40f, 0.50f, 0.50f, 0.45f, 0.45f, 1.00f, 1.00f, 0.95f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.55f, 0.40f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.35f,
		0.30f, 0.00f, 0.10f, 0.40f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_OUTDOOR_OPEN
	},

	// 27: 図書館 - 図書館。RT60約0.7s、書架による強い吸音と拡散、木の温かみ、静かで柔らかい短めの残響
	{
		0.28f, 30.00f, 0.30f,
		{ 6.00f, 0.45f, 12.00f, 0.37f, 18.72f, 0.30f, 26.25f, 0.25f, 34.68f, 0.20f, 44.12f, 0.17f, 54.69f, 0.14f, 66.53f, 0.11f },
		9000.00f, 80.00f, 1.10f, 0.00f, 0.00f, 0.00f, 0.55f, 8.00f, 0.65f, 1.20f, 0.50f, 0.50f, 0.20f,
		0.38f, 0.48f, 0.50f, 0.45f, 1.00f, 1.00f, 0.80f, 0.41f, 0.18f, 0.46f, 0.00f, 0.70f,
		0.35f, 0.55f, 0.45f, 0.60f, 0.40f, 1.10f, 1.00f, 0.20f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.45f, 0.50f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.40f, 0.00f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 28: プール(室内) - 室内プール。RT60約2.6s、タイルとガラスの明るい反射、極めて高い湿度、水面のフラッター、2.5kHz共鳴
	{
		0.55f, 100.00f, 0.51f,
		{ 8.00f, 0.60f, 19.00f, 0.53f, 31.32f, 0.46f, 45.12f, 0.41f, 60.57f, 0.36f, 77.88f, 0.32f, 97.27f, 0.28f, 118.98f, 0.25f },
		16000.00f, 90.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.45f, 16.00f, 0.35f, 2.80f, 0.50f, 0.65f, 0.30f,
		0.61f, 1.24f, 0.50f, 0.70f, 1.10f, 1.00f, 1.10f, 0.45f, 0.50f, 0.64f, 0.00f, 0.30f,
		0.30f, 0.40f, 0.70f, 0.35f, 0.45f, 1.30f, 1.50f, 0.30f, 0.40f, 0.30f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.65f, 2500.00f, 2.00f,
		0.35f, 0.30f, 0.00f, 0.50f,
		1.00f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 29: エレベーター - 金属製のエレベーター箱。RT60約0.4s、極小の金属反射、強い密閉感、1.2kHzの箱鳴り共鳴、狭いステレオ
	{
		0.30f, 10.00f, 0.26f,
		{ 2.00f, 0.60f, 4.50f, 0.49f, 7.30f, 0.40f, 10.44f, 0.33f, 13.95f, 0.27f, 17.88f, 0.22f, 22.29f, 0.18f, 27.22f, 0.15f },
		9000.00f, 110.00f, 0.60f, 0.00f, 0.00f, 0.00f, 0.30f, 3.00f, 0.40f, 0.40f, 0.50f, 0.55f, 0.05f,
		0.35f, 0.36f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.39f, 0.46f, 0.61f, 0.00f, 0.25f,
		0.30f, 0.40f, 0.55f, 0.30f, 0.50f, 0.70f, 0.50f, 0.02f, 0.25f, 0.30f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.30f, 0.55f, 1200.00f, 3.00f,
		0.70f, 0.00f, 0.00f, 0.20f,
		0.30f, 0.00f, 0.98f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_METAL_SPACE
	},

	// 30: 駐車場 - 屋内駐車場。RT60約2.4s、低い天井の広いコンクリ空間、強いフラッターとコム、暗い反射、400Hz共鳴
	{
		0.48f, 105.00f, 0.48f,
		{ 9.00f, 0.55f, 21.00f, 0.48f, 34.44f, 0.43f, 49.49f, 0.37f, 66.35f, 0.33f, 85.23f, 0.29f, 106.38f, 0.26f, 130.07f, 0.22f },
		8000.00f, 65.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.40f, 15.00f, 0.45f, 2.90f, 0.50f, 0.65f, 0.30f,
		0.59f, 1.16f, 0.50f, 0.40f, 1.25f, 1.00f, 1.00f, 0.49f, 0.20f, 0.65f, 0.00f, 0.30f,
		0.45f, 0.40f, 0.40f, 0.35f, 0.55f, 0.90f, 1.60f, 0.25f, 0.40f, 0.35f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.65f, 400.00f, 1.80f,
		0.00f, 0.00f, 0.00f, 0.80f,
		0.30f, 0.00f, 0.70f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 31: コンサートホール - 専用コンサートホール。RT60約2.1s、極めて豊かな拡散残響、木の温かみ、広いステレオ、澄んだ明瞭度
	{
		0.45f, 95.00f, 0.45f,
		{ 13.00f, 0.48f, 24.00f, 0.42f, 36.76f, 0.36f, 51.56f, 0.32f, 68.73f, 0.27f, 88.65f, 0.24f, 111.75f, 0.21f, 138.55f, 0.18f },
		12000.00f, 55.00f, 1.60f, 0.00f, 0.00f, 0.00f, 0.85f, 24.00f, 0.50f, 2.60f, 0.50f, 0.82f, 0.40f,
		0.55f, 1.04f, 0.50f, 0.55f, 1.15f, 1.00f, 0.95f, 0.46f, 0.22f, 0.75f, 0.00f, 0.40f,
		0.30f, 0.65f, 0.55f, 0.50f, 0.50f, 1.60f, 1.60f, 0.35f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.75f, 0.82f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.50f, 0.00f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 32: ジャズクラブ - 親密なジャズクラブ。RT60約0.8s、木の温かい反射、中低域の充実、程よい拡散、暖色の短めの残響
	{
		0.34f, 32.00f, 0.31f,
		{ 6.00f, 0.50f, 12.00f, 0.42f, 18.72f, 0.35f, 26.25f, 0.30f, 34.68f, 0.25f, 44.12f, 0.21f, 54.69f, 0.18f, 66.53f, 0.15f },
		10000.00f, 65.00f, 1.10f, 0.00f, 0.00f, 0.00f, 0.55f, 10.00f, 0.50f, 1.10f, 0.50f, 0.60f, 0.20f,
		0.40f, 0.52f, 0.50f, 0.50f, 1.20f, 1.00f, 1.00f, 0.48f, 0.21f, 0.46f, 0.00f, 0.50f,
		0.40f, 0.75f, 0.50f, 0.55f, 0.50f, 0.90f, 0.90f, 0.20f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.60f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.55f, 0.00f,
		0.30f, 0.00f, 0.65f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 33: カラオケボックス - 防音カラオケ個室。RT60約0.5s、内装の吸音、柔らかく短い反射、密閉、暖色で控えめの残響
	{
		0.28f, 18.00f, 0.28f,
		{ 4.00f, 0.45f, 8.00f, 0.36f, 12.48f, 0.29f, 17.50f, 0.23f, 23.12f, 0.18f, 29.41f, 0.15f, 36.46f, 0.12f, 44.36f, 0.09f },
		9500.00f, 85.00f, 0.90f, 0.00f, 0.00f, 0.00f, 0.45f, 6.00f, 0.60f, 0.70f, 0.50f, 0.50f, 0.10f,
		0.36f, 0.40f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.37f, 0.20f, 0.48f, 0.00f, 0.65f,
		0.35f, 0.55f, 0.50f, 0.65f, 0.40f, 0.80f, 0.70f, 0.05f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.35f, 0.50f, 900.00f, 1.00f,
		0.00f, 0.00f, 0.30f, 0.00f,
		0.30f, 0.00f, 0.90f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 34: 映画館 - 映画館。RT60約0.9s、台詞明瞭のため吸音処理、柔らかく制御された残響、広いステレオ、豊かな低域
	{
		0.32f, 55.00f, 0.32f,
		{ 9.00f, 0.42f, 19.00f, 0.34f, 30.20f, 0.28f, 42.74f, 0.23f, 56.79f, 0.19f, 72.53f, 0.16f, 90.15f, 0.13f, 109.89f, 0.10f },
		11000.00f, 45.00f, 1.50f, 0.00f, 0.00f, 0.00f, 0.60f, 16.00f, 0.60f, 2.20f, 0.50f, 0.60f, 0.25f,
		0.41f, 0.56f, 0.50f, 0.45f, 1.30f, 1.00f, 1.00f, 0.37f, 0.19f, 0.45f, 0.00f, 0.75f,
		0.30f, 0.55f, 0.45f, 0.60f, 0.50f, 1.20f, 1.40f, 0.25f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.60f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.30f, 0.00f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 35: 地下鉄車内 - 走行中の地下鉄車内。RT60約0.5s、金属車体の反射、密閉、走行のドップラーと低域のランブル、1.5kHz共鳴
	{
		0.32f, 16.00f, 0.28f,
		{ 3.00f, 0.55f, 6.00f, 0.45f, 9.36f, 0.37f, 13.12f, 0.30f, 17.34f, 0.25f, 22.06f, 0.20f, 27.35f, 0.17f, 33.27f, 0.14f },
		8500.00f, 70.00f, 0.90f, 0.00f, 0.00f, 0.00f, 0.35f, 4.00f, 0.50f, 0.80f, 0.50f, 0.55f, 0.10f,
		0.36f, 0.40f, 0.50f, 0.50f, 1.20f, 1.00f, 1.00f, 0.32f, 0.39f, 0.52f, 0.00f, 0.35f,
		0.35f, 0.45f, 0.50f, 0.40f, 0.55f, 0.80f, 0.70f, 0.05f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.40f, 0.55f, 1500.00f, 2.40f,
		0.55f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.90f, 0.00f,
		0.00f, 0.15f, 0.10f, 0.00f,
		TYPE_METAL_SPACE
	},

	// 36: 空港ターミナル - 空港ターミナル。RT60約2.8s、ガラスと鋼の広大な空間、明るい高域、群衆の吸音、遠いアナウンスの残響
	{
		0.46f, 115.00f, 0.53f,
		{ 12.00f, 0.48f, 26.00f, 0.42f, 41.68f, 0.36f, 59.24f, 0.32f, 78.91f, 0.27f, 100.94f, 0.24f, 125.61f, 0.21f, 153.25f, 0.18f },
		12000.00f, 70.00f, 1.60f, 0.00f, 0.00f, 0.00f, 0.60f, 22.00f, 0.45f, 3.20f, 0.50f, 0.65f, 0.45f,
		0.64f, 1.32f, 0.50f, 0.60f, 1.00f, 1.00f, 1.05f, 0.41f, 0.56f, 0.55f, 0.00f, 0.50f,
		0.30f, 0.45f, 0.60f, 0.40f, 0.45f, 1.70f, 1.70f, 0.50f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.65f, 1000.00f, 1.00f,
		0.35f, 0.50f, 0.00f, 0.40f,
		0.30f, 0.00f, 0.50f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 37: ショッピングモール - ショッピングモール。RT60約2.0s、吹き抜けの開放的空間、ガラスとコンクリ、群衆の吸音、賑やかな中域
	{
		0.42f, 100.00f, 0.44f,
		{ 11.00f, 0.46f, 24.00f, 0.40f, 38.56f, 0.34f, 54.87f, 0.29f, 73.13f, 0.25f, 93.59f, 0.22f, 116.50f, 0.19f, 142.16f, 0.16f },
		11000.00f, 70.00f, 1.50f, 0.00f, 0.00f, 0.00f, 0.60f, 20.00f, 0.50f, 2.80f, 0.50f, 0.60f, 0.40f,
		0.54f, 1.00f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.37f, 0.39f, 0.50f, 0.00f, 0.55f,
		0.35f, 0.50f, 0.55f, 0.45f, 0.45f, 1.70f, 1.60f, 0.50f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.60f, 1000.00f, 1.00f,
		0.00f, 0.40f, 0.00f, 0.40f,
		0.30f, 0.00f, 0.50f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 38: 病院 - 病院の廊下。RT60約1.1s、清潔な硬い床と吸音天井のバランス、明るく無機質、控えめの反射
	{
		0.34f, 45.00f, 0.34f,
		{ 7.00f, 0.48f, 15.00f, 0.40f, 23.96f, 0.34f, 34.00f, 0.28f, 45.23f, 0.24f, 57.82f, 0.20f, 71.92f, 0.17f, 87.71f, 0.14f },
		11000.00f, 90.00f, 1.10f, 0.00f, 0.00f, 0.00f, 0.50f, 12.00f, 0.55f, 1.60f, 0.50f, 0.55f, 0.25f,
		0.43f, 0.64f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.35f, 0.30f, 0.48f, 0.00f, 0.60f,
		0.30f, 0.45f, 0.55f, 0.50f, 0.40f, 1.10f, 1.10f, 0.20f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.45f, 0.55f, 1000.00f, 1.00f,
		0.00f, 0.20f, 0.00f, 0.35f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 39: レコーディングブース - 録音ブース。RT60約0.15s、全面吸音材で極めてデッド、極小の初期反射、ウェットほぼゼロ
	{
		0.05f, 5.00f, 0.24f,
		{ 2.00f, 0.30f, 4.00f, 0.17f, 6.24f, 0.09f, 8.75f, 0.05f, 11.56f, 0.03f, 14.71f, 0.02f, 18.23f, 0.01f, 22.18f, 0.00f },
		16000.00f, 40.00f, 0.70f, 0.00f, 0.48f, 0.00f, 0.25f, 9.20f, 0.80f, 0.50f, 0.50f, 0.25f, 0.05f,
		0.32f, 0.26f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.24f, 0.17f, 0.56f, 0.00f, 0.90f,
		0.15f, 0.50f, 0.50f, 0.70f, 0.35f, 1.00f, 1.00f, 0.02f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.20f, 0.30f, 829.74f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.70f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 40: オペラハウス - 装飾豊かなオペラハウス。RT60約1.9s、木と漆喰の温かい高密度拡散、豊麗な中低域、広く柔らかな残響
	{
		0.44f, 90.00f, 0.43f,
		{ 12.00f, 0.50f, 22.00f, 0.43f, 33.20f, 0.38f, 45.74f, 0.33f, 59.79f, 0.29f, 75.53f, 0.25f, 93.15f, 0.22f, 112.89f, 0.19f },
		10500.00f, 60.00f, 1.75f, 0.00f, 0.24f, 0.00f, 0.82f, 28.00f, 0.50f, 2.50f, 0.50f, 0.80f, 0.35f,
		0.53f, 0.96f, 0.50f, 0.55f, 1.20f, 1.00f, 0.90f, 0.48f, 0.22f, 0.70f, 0.00f, 0.50f,
		0.35f, 0.70f, 0.55f, 0.55f, 0.50f, 1.60f, 1.50f, 0.30f, 0.00f, 0.09f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.80f, 1091.80f, 1.00f,
		0.00f, 0.00f, 0.55f, 0.00f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 41: 喫茶店/カフェ - 小さなカフェ。RT60約0.6s、木と布の温かい吸音、程よい反射、湯気の湿り気、居心地の良い短め残響
	{
		0.30f, 25.00f, 0.29f,
		{ 5.00f, 0.45f, 10.00f, 0.37f, 15.60f, 0.30f, 21.87f, 0.25f, 28.90f, 0.20f, 36.76f, 0.17f, 45.58f, 0.14f, 55.45f, 0.11f },
		9500.00f, 80.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.50f, 8.00f, 0.55f, 0.90f, 0.50f, 0.50f, 0.15f,
		0.37f, 0.44f, 0.50f, 0.50f, 1.10f, 1.00f, 1.00f, 0.46f, 0.20f, 0.47f, 0.00f, 0.55f,
		0.40f, 0.70f, 0.50f, 0.55f, 0.40f, 0.90f, 0.90f, 0.15f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.40f, 0.50f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.50f, 0.00f,
		0.40f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 42: バー/ラウンジ - 薄暗いバー。RT60約0.7s、革と木の温かく暗い反射、低域の充実、落ち着いた柔らかな残響
	{
		0.32f, 28.00f, 0.30f,
		{ 5.00f, 0.48f, 10.50f, 0.40f, 16.66f, 0.33f, 23.56f, 0.27f, 31.29f, 0.23f, 39.94f, 0.19f, 49.63f, 0.16f, 60.49f, 0.13f },
		8500.00f, 60.00f, 1.10f, 0.00f, 0.00f, 0.00f, 0.50f, 9.00f, 0.50f, 1.00f, 0.50f, 0.55f, 0.15f,
		0.38f, 0.48f, 0.50f, 0.42f, 1.25f, 1.00f, 1.00f, 0.48f, 0.20f, 0.47f, 0.00f, 0.50f,
		0.40f, 0.80f, 0.42f, 0.60f, 0.50f, 0.90f, 0.90f, 0.15f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.42f, 0.55f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.55f, 0.00f,
		0.30f, 0.00f, 0.65f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 43: 居酒屋 - 賑やかな居酒屋。RT60約0.65s、木材主体の明るめの反射、程よい拡散、活気ある中域、湯気の湿り気
	{
		0.32f, 26.00f, 0.29f,
		{ 5.00f, 0.50f, 10.00f, 0.41f, 15.60f, 0.34f, 21.87f, 0.29f, 28.90f, 0.24f, 36.76f, 0.20f, 45.58f, 0.16f, 55.45f, 0.14f },
		10000.00f, 75.00f, 0.85f, 0.00f, 0.24f, 0.00f, 0.50f, 11.60f, 0.48f, 0.95f, 0.50f, 0.60f, 0.15f,
		0.38f, 0.46f, 0.50f, 0.52f, 1.10f, 1.00f, 1.00f, 0.50f, 0.22f, 0.47f, 0.00f, 0.45f,
		0.42f, 0.72f, 0.52f, 0.50f, 0.45f, 0.85f, 0.90f, 0.15f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.45f, 0.58f, 911.80f, 1.00f,
		0.00f, 0.00f, 0.60f, 0.00f,
		0.40f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 44: 美術館/博物館 - 美術館。RT60約1.6s、石とガラスの静かな空間、高い天井、程よい拡散、落ち着いた明瞭な残響
	{
		0.38f, 70.00f, 0.40f,
		{ 10.00f, 0.46f, 20.00f, 0.39f, 31.20f, 0.33f, 43.74f, 0.28f, 57.79f, 0.24f, 73.53f, 0.20f, 91.15f, 0.17f, 110.89f, 0.15f },
		11000.00f, 70.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.65f, 16.00f, 0.55f, 2.00f, 0.50f, 0.60f, 0.30f,
		0.49f, 0.84f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.37f, 0.33f, 0.44f, 0.00f, 0.50f,
		0.35f, 0.50f, 0.50f, 0.50f, 0.45f, 1.50f, 1.40f, 0.30f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.55f, 0.60f, 1000.00f, 1.00f,
		0.00f, 0.30f, 0.00f, 0.40f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 45: 講堂/大学教室 - 講堂。RT60約1.1s、話声明瞭のための吸音、程よい拡散、木の温かみ、はっきりした中域
	{
		0.36f, 55.00f, 0.34f,
		{ 8.00f, 0.46f, 17.00f, 0.39f, 27.08f, 0.32f, 38.37f, 0.27f, 51.01f, 0.23f, 65.18f, 0.19f, 81.04f, 0.16f, 98.80f, 0.14f },
		10500.00f, 75.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.60f, 14.00f, 0.55f, 1.80f, 0.50f, 0.60f, 0.30f,
		0.43f, 0.64f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.42f, 0.21f, 0.72f, 0.00f, 0.55f,
		0.35f, 0.55f, 0.55f, 0.50f, 0.45f, 1.30f, 1.30f, 0.25f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.60f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.40f, 0.00f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 46: 竹林 - 竹林。RT60約1.0s、無数の竹幹による高い拡散、中空の竹による350Hz帯コム共鳴、風のゆらぎ、柔らかな高域吸収
	{
		0.32f, 58.00f, 0.33f,
		{ 9.00f, 0.40f, 21.00f, 0.33f, 35.40f, 0.27f, 52.68f, 0.22f, 73.42f, 0.18f, 98.30f, 0.15f, 128.16f, 0.12f, 163.99f, 0.10f },
		9000.00f, 90.00f, 1.50f, 0.00f, 0.00f, 0.00f, 0.85f, 12.00f, 0.55f, 2.20f, 0.50f, 0.40f, 0.45f,
		0.42f, 0.60f, 0.50f, 0.50f, 1.00f, 1.00f, 0.75f, 0.50f, 0.20f, 0.46f, 0.00f, 0.50f,
		0.50f, 0.55f, 0.50f, 0.60f, 0.35f, 1.00f, 1.00f, 0.85f, 0.00f, 0.20f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.40f, 350.00f, 1.80f,
		0.00f, 0.00f, 0.60f, 0.00f,
		0.30f, 0.00f, 0.20f, 0.40f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_OUTDOOR_OPEN
	},

	// 47: 渓谷/滝 - 滝のある渓谷。濡れた岩壁からの220/380/510/780msエコー、非常に高い湿度、水しぶきの微モジュレーション、開放的な峡谷反射
	{
		0.58f, 220.00f, 0.46f,
		{ 13.00f, 0.42f, 29.00f, 0.35f, 48.20f, 0.29f, 71.24f, 0.24f, 98.89f, 0.20f, 132.07f, 0.17f, 171.88f, 0.14f, 219.65f, 0.11f },
		8500.00f, 60.00f, 1.80f, 0.15f, 0.60f, 0.00f, 0.50f, 20.00f, 0.50f, 3.00f, 0.50f, 0.45f, 0.50f,
		0.56f, 1.08f, 0.50f, 0.45f, 1.20f, 1.00f, 1.00f, 0.36f, 0.20f, 0.70f, 0.00f, 0.40f,
		0.60f, 0.50f, 0.45f, 0.50f, 0.50f, 1.00f, 1.00f, 0.85f, 0.00f, 0.00f,
		{ 220.00f, 380.00f, 510.00f, 780.00f }, { 0.88f, 0.66f, 0.46f, 0.28f }, 1.10f, 0.75f,
		0.60f, 0.45f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.35f,
		0.80f, 0.00f, 0.20f, 0.30f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CANYON_ECHO
	},

	// 48: 砂漠 - 砂漠。反射のほぼ無い極端に開けた乾燥空間、RT60約0.4s、強い空気吸収、極低湿度、風、疎らな高域
	{
		0.18f, 40.00f, 0.22f,
		{ 14.00f, 0.20f, 36.00f, 0.12f, 64.60f, 0.07f, 101.78f, 0.04f, 150.11f, 0.03f, 212.95f, 0.02f, 294.63f, 0.01f, 400.82f, 0.01f },
		8000.00f, 80.00f, 1.60f, 0.00f, 0.00f, 0.00f, 0.30f, 15.00f, 0.50f, 3.00f, 0.50f, 0.20f, 0.70f,
		0.35f, 0.36f, 0.50f, 0.55f, 1.00f, 1.00f, 0.70f, 0.26f, 0.22f, 0.74f, 0.00f, 0.40f,
		0.40f, 0.50f, 0.55f, 0.40f, 0.35f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.30f, 0.20f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.05f, 0.30f, 0.00f, 0.50f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_OUTDOOR_OPEN
	},

	// 49: ガレージ - 家庭用ガレージ。RT60約1.0s、コンクリと金属シャッターの硬い反射、フラッターとコム、550Hz共鳴、密閉感
	{
		0.40f, 38.00f, 0.33f,
		{ 5.00f, 0.55f, 11.00f, 0.47f, 17.72f, 0.41f, 25.25f, 0.35f, 33.68f, 0.30f, 43.12f, 0.26f, 53.69f, 0.22f, 65.53f, 0.19f },
		9000.00f, 75.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.40f, 8.00f, 0.45f, 1.20f, 0.50f, 0.60f, 0.15f,
		0.42f, 0.60f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.47f, 0.34f, 0.61f, 0.00f, 0.35f,
		0.45f, 0.40f, 0.50f, 0.35f, 0.50f, 0.90f, 0.90f, 0.10f, 0.35f, 0.30f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.40f, 0.60f, 550.00f, 2.00f,
		0.40f, 0.00f, 0.00f, 0.60f,
		0.30f, 0.00f, 0.80f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 50: 展望台 - 高所の展望台。RT60約0.8s、極めて開放的で高い高度感、強い風、ガラス手すりの僅かな反射、空気吸収
	{
		0.24f, 45.00f, 0.25f,
		{ 12.00f, 0.30f, 28.00f, 0.21f, 48.00f, 0.15f, 73.00f, 0.10f, 104.25f, 0.07f, 143.31f, 0.05f, 192.14f, 0.04f, 253.18f, 0.02f },
		10000.00f, 85.00f, 1.60f, 0.00f, 0.00f, 0.00f, 0.40f, 14.00f, 0.50f, 2.40f, 0.50f, 0.30f, 0.60f,
		0.40f, 0.52f, 0.50f, 0.60f, 1.00f, 1.00f, 0.80f, 0.26f, 0.35f, 0.69f, 0.00f, 0.40f,
		0.35f, 0.45f, 0.60f, 0.40f, 0.30f, 1.00f, 1.00f, 1.00f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.40f, 0.30f, 1000.00f, 1.00f,
		0.00f, 0.30f, 0.00f, 0.00f,
		0.30f, 0.90f, 0.05f, 0.60f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_OUTDOOR_OPEN
	},

	// 51: 小さな礼拝堂 - 小さな礼拝堂。RT60約1.4s、石と木の穏やかな拡散、教会より小さく短い残響、温かく澄んだ響き
	{
		0.42f, 55.00f, 0.37f,
		{ 9.00f, 0.48f, 18.00f, 0.41f, 28.08f, 0.36f, 39.37f, 0.31f, 52.01f, 0.26f, 66.18f, 0.23f, 82.04f, 0.19f, 99.80f, 0.17f },
		9500.00f, 65.00f, 1.20f, 0.00f, 0.00f, 0.00f, 0.70f, 16.00f, 0.55f, 1.60f, 0.50f, 0.65f, 0.30f,
		0.47f, 0.76f, 0.50f, 0.48f, 1.15f, 1.00f, 1.00f, 0.52f, 0.20f, 0.41f, 0.00f, 0.40f,
		0.40f, 0.55f, 0.48f, 0.50f, 0.50f, 1.40f, 1.20f, 0.25f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.65f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.35f, 0.45f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 52: 大型ショッピングセンター - 超大型商業施設。RT60約2.4s、複数階の巨大吹き抜け、ガラスとコンクリ、群衆の吸音、広く長い残響
	{
		0.44f, 110.00f, 0.48f,
		{ 12.00f, 0.48f, 27.00f, 0.42f, 43.80f, 0.36f, 62.62f, 0.32f, 83.69f, 0.27f, 107.29f, 0.24f, 133.73f, 0.21f, 163.34f, 0.18f },
		11000.00f, 65.00f, 1.75f, 0.00f, 0.24f, 0.00f, 0.62f, 28.00f, 0.48f, 3.10f, 0.50f, 0.65f, 0.40f,
		0.59f, 1.16f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.38f, 0.41f, 0.50f, 0.00f, 0.50f,
		0.35f, 0.50f, 0.55f, 0.45f, 0.50f, 1.90f, 1.70f, 0.55f, 0.00f, 0.09f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.65f, 0.65f, 1091.80f, 1.00f,
		0.00f, 0.45f, 0.00f, 0.42f,
		0.30f, 0.00f, 0.50f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CATHEDRAL
	},

	// 53: 地下洞窟(深層) - 深層の地下洞窟。RT60約4.0s、洞窟(4)より暗く広大、LPF2.5kHz、非常に高い湿度、重い低域、粗い岩面、180Hz共鳴
	{
		0.62f, 140.00f, 0.66f,
		{ 11.00f, 0.50f, 27.00f, 0.43f, 46.52f, 0.37f, 70.33f, 0.32f, 99.39f, 0.27f, 134.83f, 0.24f, 178.08f, 0.20f, 230.83f, 0.17f },
		2500.00f, 45.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.50f, 22.00f, 0.75f, 3.40f, 0.50f, 0.65f, 0.35f,
		0.78f, 1.80f, 0.50f, 0.18f, 1.70f, 1.00f, 0.50f, 0.32f, 0.12f, 0.48f, 0.00f, 0.40f,
		0.85f, 0.50f, 0.18f, 0.50f, 0.80f, 1.60f, 1.80f, 0.20f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.65f, 180.00f, 1.80f,
		0.00f, 0.00f, 0.00f, 0.30f,
		0.85f, 0.00f, 0.80f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CAVE
	},

	// 54: 古城の大広間 - 古城の大広間。RT60約2.4s、石壁と木の梁の混合反射、高い天井、タペストリの適度な吸音、重厚で豊かな残響
	{
		0.46f, 100.00f, 0.48f,
		{ 13.00f, 0.50f, 26.00f, 0.43f, 40.56f, 0.38f, 56.87f, 0.33f, 75.13f, 0.29f, 95.59f, 0.25f, 118.50f, 0.22f, 144.16f, 0.19f },
		9500.00f, 60.00f, 1.50f, 0.00f, 0.00f, 0.00f, 0.75f, 22.00f, 0.50f, 2.70f, 0.50f, 0.75f, 0.35f,
		0.59f, 1.16f, 0.50f, 0.48f, 1.30f, 1.00f, 1.00f, 0.58f, 0.21f, 0.39f, 0.00f, 0.40f,
		0.45f, 0.60f, 0.48f, 0.50f, 0.65f, 1.80f, 1.70f, 0.30f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.75f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.45f, 0.50f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 55: 野外音楽堂 - 野外音楽堂。RT60約1.3s、背後の反響板が音を前方へ集める、開放的な空気感、木の温かみ、微風
	{
		0.34f, 75.00f, 0.36f,
		{ 11.00f, 0.42f, 24.00f, 0.34f, 39.60f, 0.28f, 58.32f, 0.23f, 80.78f, 0.19f, 107.74f, 0.16f, 140.09f, 0.13f, 178.91f, 0.10f },
		10000.00f, 75.00f, 1.50f, 0.00f, 0.00f, 0.00f, 0.55f, 16.00f, 0.50f, 2.60f, 0.50f, 0.45f, 0.50f,
		0.46f, 0.72f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.42f, 0.22f, 0.65f, 0.00f, 0.45f,
		0.40f, 0.55f, 0.55f, 0.45f, 0.40f, 1.00f, 1.00f, 0.80f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.45f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.40f, 0.00f,
		0.30f, 0.00f, 0.15f, 0.30f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_OUTDOOR_OPEN
	},

	// 56: 鍾乳洞 - 鍾乳洞。RT60約3.4s、鍾乳石の複雑な反射、水滴の微モジュレーション、高湿度、220Hz共鳴、鉱物のわずかなきらめき
	{
		0.60f, 130.00f, 0.59f,
		{ 10.00f, 0.50f, 25.00f, 0.43f, 43.30f, 0.37f, 65.63f, 0.32f, 92.86f, 0.27f, 126.09f, 0.24f, 166.63f, 0.20f, 216.09f, 0.17f },
		3200.00f, 50.00f, 1.40f, 0.12f, 0.30f, 0.00f, 0.45f, 20.00f, 0.65f, 3.00f, 0.50f, 0.60f, 0.35f,
		0.71f, 1.56f, 0.50f, 0.28f, 1.50f, 1.00f, 0.60f, 0.32f, 0.15f, 0.51f, 0.00f, 0.40f,
		0.80f, 0.50f, 0.28f, 0.50f, 0.70f, 1.50f, 1.60f, 0.20f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.75f, 0.60f, 220.00f, 2.50f,
		0.00f, 0.00f, 0.00f, 0.25f,
		0.90f, 0.00f, 0.78f, 0.00f,
		0.15f, 0.00f, 0.00f, 0.00f,
		TYPE_CAVE
	},

	// 57: 廃墟工場 - 廃墟の工場。RT60約3.0s、錆びた金属の粗い反射、割れた窓からの風、僅かな歪みと荒れたコム、寂寥感のある長い残響
	{
		0.50f, 120.00f, 0.55f,
		{ 9.00f, 0.55f, 22.00f, 0.48f, 36.56f, 0.43f, 52.87f, 0.37f, 71.13f, 0.33f, 91.59f, 0.29f, 114.50f, 0.26f, 140.16f, 0.22f },
		9000.00f, 65.00f, 1.50f, 0.00f, 0.00f, 0.00f, 0.45f, 18.00f, 0.40f, 3.00f, 0.50f, 0.70f, 0.35f,
		0.66f, 1.40f, 0.50f, 0.45f, 1.20f, 1.00f, 1.00f, 0.49f, 0.39f, 0.66f, 0.00f, 0.30f,
		0.80f, 0.40f, 0.45f, 0.30f, 0.60f, 1.50f, 1.60f, 0.35f, 0.45f, 0.35f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.65f, 0.70f, 700.00f, 2.20f,
		0.55f, 0.00f, 0.00f, 0.60f,
		0.30f, 0.00f, 0.55f, 0.20f,
		0.00f, 0.00f, 0.20f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 58: 和室(畳) - 畳の和室。RT60約0.35s、畳と障子の強い吸音、非常に柔らかくデッド、木の温かみ、控えめの反射
	{
		0.20f, 15.00f, 0.26f,
		{ 4.00f, 0.40f, 8.00f, 0.31f, 12.48f, 0.24f, 17.50f, 0.19f, 23.12f, 0.15f, 29.41f, 0.12f, 36.46f, 0.09f, 44.36f, 0.07f },
		8500.00f, 80.00f, 0.90f, 0.00f, 0.00f, 0.00f, 0.40f, 5.00f, 0.70f, 0.70f, 0.50f, 0.40f, 0.10f,
		0.34f, 0.34f, 0.50f, 0.42f, 1.05f, 1.00f, 1.00f, 0.42f, 0.17f, 0.51f, 0.00f, 0.80f,
		0.40f, 0.70f, 0.42f, 0.85f, 0.40f, 0.70f, 0.70f, 0.10f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.30f, 0.40f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.45f, 0.00f,
		0.40f, 0.00f, 0.65f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 59: 温泉施設 - 温泉。RT60約1.3s、岩と木の温かい反射、極めて高い湿度と湯気、風呂場より広く温かい、柔らかな中域
	{
		0.48f, 60.00f, 0.36f,
		{ 7.00f, 0.50f, 15.00f, 0.42f, 23.96f, 0.36f, 34.00f, 0.31f, 45.23f, 0.26f, 57.82f, 0.22f, 71.92f, 0.19f, 87.71f, 0.16f },
		8500.00f, 70.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.60f, 12.00f, 0.50f, 1.90f, 0.50f, 0.60f, 0.30f,
		0.46f, 0.72f, 0.50f, 0.45f, 1.20f, 1.00f, 1.00f, 0.53f, 0.20f, 0.46f, 0.00f, 0.40f,
		0.45f, 0.75f, 0.45f, 0.55f, 0.50f, 1.20f, 1.30f, 0.30f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.60f, 600.00f, 1.60f,
		0.00f, 0.00f, 0.40f, 0.40f,
		1.00f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 60: 屋根裏部屋 - 傾斜天井の屋根裏。RT60約0.6s、木材の温かい反射、低く傾いた天井、埃っぽい空気、こもりがちで控えめの残響
	{
		0.30f, 24.00f, 0.29f,
		{ 4.00f, 0.50f, 9.00f, 0.41f, 14.60f, 0.34f, 20.87f, 0.28f, 27.90f, 0.23f, 35.76f, 0.19f, 44.58f, 0.15f, 54.45f, 0.12f },
		8000.00f, 85.00f, 0.90f, 0.00f, 0.00f, 0.00f, 0.45f, 7.00f, 0.55f, 0.90f, 0.50f, 0.50f, 0.20f,
		0.37f, 0.44f, 0.50f, 0.42f, 1.00f, 1.00f, 1.00f, 0.50f, 0.19f, 0.49f, 0.00f, 0.50f,
		0.50f, 0.60f, 0.42f, 0.50f, 0.45f, 0.70f, 0.80f, 0.10f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.40f, 0.50f, 800.00f, 1.00f,
		0.00f, 0.00f, 0.60f, 0.00f,
		0.30f, 0.00f, 0.70f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 61: 地下駐車場(多層) - 多層の地下駐車場。RT60約2.8s、広大なコンクリと低い天井の連続、駐車場(30)より長く暗い、強いフラッター、380Hz共鳴
	{
		0.50f, 115.00f, 0.53f,
		{ 9.00f, 0.55f, 21.00f, 0.48f, 34.44f, 0.43f, 49.49f, 0.37f, 66.35f, 0.33f, 85.23f, 0.29f, 106.38f, 0.26f, 130.07f, 0.22f },
		7500.00f, 60.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.40f, 16.00f, 0.45f, 3.00f, 0.50f, 0.70f, 0.30f,
		0.64f, 1.32f, 0.50f, 0.38f, 1.30f, 1.00f, 1.00f, 0.50f, 0.19f, 0.66f, 0.00f, 0.30f,
		0.45f, 0.40f, 0.38f, 0.35f, 0.60f, 0.85f, 1.60f, 0.20f, 0.45f, 0.40f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.55f, 0.70f, 380.00f, 2.00f,
		0.00f, 0.00f, 0.00f, 0.85f,
		0.30f, 0.00f, 0.75f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CORRIDOR
	},

	// 62: 古い劇場(木造) - 木造の古い劇場。RT60約1.5s、木材の強い共鳴と温かい拡散、320Hz帯の木の鳴り、劇場(14)より木質が濃い
	{
		0.42f, 72.00f, 0.39f,
		{ 9.00f, 0.50f, 18.00f, 0.42f, 28.08f, 0.36f, 39.37f, 0.31f, 52.01f, 0.26f, 66.18f, 0.22f, 82.04f, 0.19f, 99.80f, 0.16f },
		9000.00f, 65.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.72f, 16.00f, 0.50f, 2.00f, 0.50f, 0.68f, 0.30f,
		0.48f, 0.80f, 0.50f, 0.48f, 1.20f, 1.00f, 1.00f, 0.56f, 0.21f, 0.41f, 0.00f, 0.45f,
		0.40f, 0.72f, 0.48f, 0.55f, 0.50f, 1.40f, 1.30f, 0.30f, 0.00f, 0.20f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.68f, 320.00f, 2.00f,
		0.00f, 0.00f, 0.75f, 0.00f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 63: 大型倉庫(空) - 空の大型倉庫。RT60約3.2s、吸音物のない巨大空間、鉄骨とコンクリの反射、倉庫(21)より長く空虚、遠いスラップ
	{
		0.50f, 125.00f, 0.57f,
		{ 11.00f, 0.50f, 25.00f, 0.45f, 40.68f, 0.40f, 58.24f, 0.35f, 77.91f, 0.31f, 99.94f, 0.28f, 124.61f, 0.25f, 152.25f, 0.22f },
		10000.00f, 60.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.45f, 20.00f, 0.38f, 3.30f, 0.50f, 0.65f, 0.40f,
		0.68f, 1.48f, 0.50f, 0.50f, 1.25f, 1.00f, 1.00f, 0.46f, 0.35f, 0.60f, 0.00f, 0.30f,
		0.45f, 0.42f, 0.50f, 0.35f, 0.55f, 1.70f, 1.70f, 0.35f, 0.42f, 0.30f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.55f, 0.65f, 450.00f, 1.60f,
		0.40f, 0.00f, 0.00f, 0.55f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CATHEDRAL
	},

	// 64: 小さな教会 - 小さな教会。RT60約1.8s、石と木の拡散、礼拝堂(51)より大きく教会(3)より小さい中間、温かく澄んだ残響、ステンドグラスの僅かな反射
	{
		0.46f, 70.00f, 0.42f,
		{ 10.00f, 0.48f, 20.00f, 0.41f, 31.20f, 0.36f, 43.74f, 0.31f, 57.79f, 0.26f, 73.53f, 0.23f, 91.15f, 0.19f, 110.89f, 0.17f },
		9500.00f, 60.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.72f, 18.00f, 0.55f, 1.90f, 0.50f, 0.70f, 0.30f,
		0.52f, 0.92f, 0.50f, 0.48f, 1.20f, 1.00f, 1.00f, 0.54f, 0.28f, 0.40f, 0.00f, 0.40f,
		0.40f, 0.58f, 0.48f, 0.50f, 0.55f, 1.50f, 1.40f, 0.28f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.62f, 0.70f, 1000.00f, 1.00f,
		0.00f, 0.20f, 0.40f, 0.45f,
		0.30f, 0.00f, 0.58f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 65: ガラス温室 - ガラス温室。RT60約1.4s、全面ガラスの明るく硬い反射、高い湿度、ガラスのフラッターと2.8kHz共鳴、非常に明るい高域
	{
		0.44f, 60.00f, 0.37f,
		{ 6.00f, 0.58f, 14.00f, 0.50f, 22.96f, 0.44f, 33.00f, 0.38f, 44.23f, 0.33f, 56.82f, 0.29f, 70.92f, 0.25f, 86.71f, 0.22f },
		13000.00f, 90.00f, 1.30f, 0.00f, 0.00f, 0.00f, 0.45f, 12.00f, 0.35f, 1.80f, 0.50f, 0.60f, 0.30f,
		0.47f, 0.76f, 0.50f, 0.78f, 1.00f, 1.00f, 1.15f, 0.29f, 0.68f, 0.61f, 0.00f, 0.30f,
		0.25f, 0.40f, 0.78f, 0.30f, 0.40f, 1.30f, 1.20f, 0.30f, 0.30f, 0.25f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.60f, 2800.00f, 2.20f,
		0.20f, 0.80f, 0.00f, 0.00f,
		0.80f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 66: 石造りトンネル - 石造りのトンネル。RT60約2.6s、粗い石壁の強いフラッターとコム、湿り気、トンネル(16)より粗く湿った反射、450Hz共鳴
	{
		0.50f, 100.00f, 0.51f,
		{ 8.00f, 0.58f, 17.00f, 0.51f, 27.08f, 0.45f, 38.37f, 0.40f, 51.01f, 0.35f, 65.18f, 0.31f, 81.04f, 0.27f, 98.80f, 0.24f },
		7000.00f, 60.00f, 0.75f, 0.00f, 0.00f, 0.00f, 0.45f, 12.00f, 0.50f, 2.40f, 0.50f, 0.65f, 0.30f,
		0.61f, 1.24f, 0.50f, 0.40f, 1.30f, 1.00f, 1.00f, 0.46f, 0.19f, 0.66f, 0.00f, 0.30f,
		0.70f, 0.45f, 0.40f, 0.35f, 0.60f, 0.95f, 1.10f, 0.20f, 0.55f, 0.45f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.65f, 450.00f, 2.20f,
		0.00f, 0.00f, 0.00f, 0.70f,
		0.50f, 0.00f, 0.75f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CORRIDOR
	},

	// 67: コンクリート階段 - コンクリの階段室。RT60約2.2s、剥き出しコンクリの硬い縦反射、階段室(19)より長くコンクリ質、強いフラッター、550Hz共鳴
	{
		0.48f, 65.00f, 0.46f,
		{ 6.00f, 0.55f, 13.00f, 0.48f, 21.40f, 0.43f, 31.48f, 0.37f, 43.58f, 0.33f, 58.09f, 0.29f, 75.51f, 0.26f, 96.41f, 0.22f },
		8500.00f, 65.00f, 0.85f, 0.00f, 0.00f, 0.00f, 0.45f, 10.00f, 0.45f, 1.70f, 0.50f, 0.68f, 0.25f,
		0.56f, 1.08f, 0.50f, 0.42f, 1.25f, 1.00f, 1.00f, 0.51f, 0.27f, 0.65f, 0.00f, 0.30f,
		0.50f, 0.40f, 0.42f, 0.32f, 0.55f, 2.00f, 1.00f, 0.20f, 0.50f, 0.40f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.68f, 0.68f, 550.00f, 2.20f,
		0.20f, 0.00f, 0.00f, 0.80f,
		0.30f, 0.00f, 0.72f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CORRIDOR
	},

	// 68: 大浴場 - 大浴場。RT60約1.9s、広いタイル張りの明るい反射、極めて高い湿度、風呂場(1)より大きく残響が長い、2.2kHz共鳴とフラッター
	{
		0.55f, 85.00f, 0.43f,
		{ 6.00f, 0.60f, 15.00f, 0.53f, 25.08f, 0.46f, 36.37f, 0.41f, 49.01f, 0.36f, 63.18f, 0.32f, 79.04f, 0.28f, 96.80f, 0.25f },
		16500.00f, 90.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.42f, 14.00f, 0.35f, 2.20f, 0.50f, 0.62f, 0.30f,
		0.53f, 0.96f, 0.50f, 0.72f, 1.00f, 1.00f, 1.10f, 0.45f, 0.50f, 0.55f, 0.00f, 0.30f,
		0.30f, 0.50f, 0.72f, 0.35f, 0.45f, 1.20f, 1.40f, 0.25f, 0.40f, 0.32f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.62f, 2200.00f, 2.20f,
		0.40f, 0.25f, 0.00f, 0.50f,
		1.00f, 0.00f, 0.65f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 69: 洗面所 - 小さな洗面所。RT60約0.4s、狭いタイル張り、風呂場(1)より小さく明るい、強い密閉感、3.6kHzの高い共鳴
	{
		0.42f, 9.00f, 0.26f,
		{ 1.50f, 0.68f, 3.30f, 0.58f, 5.32f, 0.49f, 7.57f, 0.42f, 10.10f, 0.35f, 12.94f, 0.30f, 16.11f, 0.26f, 19.66f, 0.22f },
		18500.00f, 200.00f, 0.60f, 0.00f, 0.00f, 0.00f, 0.28f, 3.00f, 0.30f, 0.40f, 0.50f, 0.60f, 0.05f,
		0.35f, 0.36f, 0.50f, 0.82f, 1.00f, 1.00f, 1.15f, 0.40f, 0.55f, 0.62f, 0.00f, 0.20f,
		0.25f, 0.40f, 0.82f, 0.20f, 0.30f, 0.60f, 0.50f, 0.02f, 0.25f, 0.20f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.28f, 0.60f, 3600.00f, 2.80f,
		0.40f, 0.30f, 0.00f, 0.30f,
		0.75f, 0.00f, 0.95f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 70: 廊下(カーペット) - カーペット敷きの廊下。RT60約0.7s、床の強い吸音でフラッターが消え、柔らかく短い反射、廊下(22)より遥かにデッド
	{
		0.30f, 35.00f, 0.30f,
		{ 5.00f, 0.42f, 11.00f, 0.34f, 17.72f, 0.27f, 25.25f, 0.22f, 33.68f, 0.17f, 43.12f, 0.14f, 53.69f, 0.11f, 65.53f, 0.09f },
		8500.00f, 85.00f, 0.80f, 0.00f, 0.00f, 0.00f, 0.40f, 8.00f, 0.65f, 1.30f, 0.50f, 0.50f, 0.20f,
		0.38f, 0.48f, 0.50f, 0.42f, 1.00f, 1.00f, 1.00f, 0.33f, 0.18f, 0.55f, 0.00f, 0.70f,
		0.35f, 0.55f, 0.42f, 0.70f, 0.45f, 1.00f, 0.70f, 0.15f, 0.10f, 0.05f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.35f, 0.50f, 700.00f, 1.20f,
		0.00f, 0.00f, 0.20f, 0.00f,
		0.30f, 0.00f, 0.65f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CORRIDOR
	},

	// 71: 会議室(大) - 大会議室。RT60約0.9s、吸音天井と絨毯で明瞭、程よい拡散、ガラスと木の混合、落ち着いた中域
	{
		0.34f, 42.00f, 0.32f,
		{ 7.00f, 0.46f, 14.00f, 0.38f, 21.84f, 0.32f, 30.62f, 0.26f, 40.46f, 0.22f, 51.47f, 0.18f, 63.81f, 0.15f, 77.62f, 0.12f },
		10500.00f, 80.00f, 1.20f, 0.00f, 0.00f, 0.00f, 0.55f, 10.00f, 0.55f, 1.60f, 0.50f, 0.55f, 0.25f,
		0.41f, 0.56f, 0.50f, 0.52f, 1.00f, 1.00f, 1.00f, 0.40f, 0.29f, 0.46f, 0.00f, 0.55f,
		0.35f, 0.50f, 0.52f, 0.50f, 0.40f, 1.10f, 1.10f, 0.20f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.45f, 0.55f, 1000.00f, 1.00f,
		0.00f, 0.20f, 0.35f, 0.00f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 72: 会議室(小) - 小会議室。RT60約0.5s、狭く吸音された空間、短く控えめの反射、会議室(大)より密閉で近接感のある響き
	{
		0.28f, 24.00f, 0.28f,
		{ 5.00f, 0.44f, 10.00f, 0.36f, 15.60f, 0.29f, 21.87f, 0.23f, 28.90f, 0.19f, 36.76f, 0.15f, 45.58f, 0.12f, 55.45f, 0.10f },
		10000.00f, 85.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.45f, 7.00f, 0.60f, 0.90f, 0.50f, 0.50f, 0.20f,
		0.36f, 0.40f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.37f, 0.28f, 0.49f, 0.00f, 0.60f,
		0.35f, 0.50f, 0.50f, 0.55f, 0.40f, 0.90f, 0.80f, 0.10f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.35f, 0.50f, 900.00f, 1.00f,
		0.00f, 0.20f, 0.30f, 0.00f,
		0.30f, 0.00f, 0.85f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 73: 防音室 - 完全防音室。RT60約0.12s、全面フル吸音でスタジオ(5)やブース(39)よりさらにデッド、ウェットほぼ皆無、無響に近い
	{
		0.03f, 4.00f, 0.23f,
		{ 1.50f, 0.25f, 3.00f, 0.12f, 4.68f, 0.06f, 6.56f, 0.03f, 8.67f, 0.02f, 11.03f, 0.01f, 13.67f, 0.00f, 16.63f, 0.00f },
		17000.00f, 35.00f, 1.00f, 0.00f, 0.00f, 0.00f, 0.20f, 2.00f, 0.85f, 0.50f, 0.50f, 0.20f, 0.02f,
		0.31f, 0.25f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.23f, 0.17f, 0.58f, 0.00f, 0.95f,
		0.10f, 0.50f, 0.50f, 0.75f, 0.35f, 1.00f, 1.00f, 0.01f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.15f, 0.25f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.98f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 74: エントランスホール - 大理石のエントランスホール。RT60約1.7s、硬い石とガラスの明るい反射、高い天井、澄んだ拡散残響
	{
		0.42f, 75.00f, 0.41f,
		{ 8.00f, 0.52f, 18.00f, 0.45f, 29.20f, 0.38f, 41.74f, 0.33f, 55.79f, 0.28f, 71.53f, 0.24f, 89.15f, 0.21f, 108.89f, 0.18f },
		12000.00f, 70.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.65f, 16.00f, 0.45f, 2.10f, 0.50f, 0.68f, 0.30f,
		0.50f, 0.88f, 0.50f, 0.58f, 1.00f, 1.00f, 1.00f, 0.42f, 0.44f, 0.45f, 0.00f, 0.35f,
		0.30f, 0.50f, 0.58f, 0.40f, 0.50f, 1.60f, 1.40f, 0.35f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.55f, 0.68f, 1000.00f, 1.60f,
		0.20f, 0.35f, 0.00f, 0.50f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_LARGE_HALL
	},

	// 75: 書斎 - 書棚に囲まれた書斎。RT60約0.45s、本と木の吸音、図書館(27)より小さく密閉、温かく静かな短い残響
	{
		0.26f, 20.00f, 0.27f,
		{ 4.00f, 0.42f, 8.00f, 0.34f, 12.48f, 0.27f, 17.50f, 0.22f, 23.12f, 0.17f, 29.41f, 0.14f, 36.46f, 0.11f, 44.36f, 0.09f },
		8800.00f, 80.00f, 0.90f, 0.00f, 0.00f, 0.00f, 0.45f, 6.00f, 0.60f, 0.80f, 0.50f, 0.45f, 0.15f,
		0.35f, 0.38f, 0.50f, 0.45f, 1.00f, 1.00f, 1.00f, 0.45f, 0.19f, 0.49f, 0.00f, 0.65f,
		0.40f, 0.62f, 0.45f, 0.60f, 0.40f, 0.90f, 0.80f, 0.10f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.40f, 0.45f, 850.00f, 1.00f,
		0.00f, 0.00f, 0.50f, 0.00f,
		0.30f, 0.00f, 0.85f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 76: キッチン - 家庭のキッチン。RT60約0.7s、タイルと金属器具の硬い明るい反射、僅かな湿り気、フラッターと2.6kHz共鳴
	{
		0.34f, 26.00f, 0.30f,
		{ 3.50f, 0.55f, 7.50f, 0.47f, 11.98f, 0.40f, 17.00f, 0.34f, 22.62f, 0.29f, 28.91f, 0.24f, 35.96f, 0.21f, 43.86f, 0.18f },
		11500.00f, 95.00f, 0.90f, 0.00f, 0.00f, 0.00f, 0.35f, 6.00f, 0.40f, 0.90f, 0.50f, 0.58f, 0.10f,
		0.38f, 0.48f, 0.50f, 0.65f, 1.00f, 1.00f, 1.10f, 0.41f, 0.48f, 0.60f, 0.00f, 0.30f,
		0.30f, 0.45f, 0.65f, 0.30f, 0.40f, 0.85f, 0.80f, 0.10f, 0.25f, 0.20f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.35f, 0.58f, 2600.00f, 2.40f,
		0.45f, 0.20f, 0.00f, 0.35f,
		0.50f, 0.00f, 0.80f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_SMALL_ROOM
	},

	// 77: 屋外駐車場 - 屋外の平面駐車場。RT60約0.9s、開けた舗装面の反射、屋内駐車場と違いフラッターなし、遠い建物からの緩いスラップ、微風
	{
		0.24f, 55.00f, 0.28f,
		{ 12.00f, 0.35f, 27.00f, 0.26f, 45.75f, 0.20f, 69.19f, 0.15f, 98.48f, 0.11f, 135.11f, 0.08f, 180.88f, 0.06f, 238.10f, 0.05f },
		9500.00f, 80.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.35f, 14.00f, 0.50f, 2.60f, 0.50f, 0.30f, 0.50f,
		0.41f, 0.56f, 0.50f, 0.50f, 1.00f, 1.00f, 0.80f, 0.39f, 0.21f, 0.70f, 0.00f, 0.40f,
		0.40f, 0.45f, 0.50f, 0.40f, 0.40f, 1.00f, 1.00f, 0.95f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.35f, 0.30f, 1000.00f, 1.00f,
		0.00f, 0.00f, 0.00f, 0.45f,
		0.30f, 0.00f, 0.10f, 0.35f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_OUTDOOR_OPEN
	},

	// 78: 地下道(狭) - 非常に狭い地下道。RT60約1.6s、極端に近い平行壁、トンネル(16)より強いフラッターとコム、狭いステレオ、600Hz共鳴、密閉感
	{
		0.46f, 60.00f, 0.40f,
		{ 5.00f, 0.58f, 10.50f, 0.51f, 16.66f, 0.45f, 23.56f, 0.40f, 31.29f, 0.35f, 39.94f, 0.31f, 49.63f, 0.27f, 60.49f, 0.24f },
		8000.00f, 70.00f, 0.60f, 0.00f, 0.00f, 0.00f, 0.40f, 8.00f, 0.45f, 1.60f, 0.50f, 0.62f, 0.25f,
		0.49f, 0.84f, 0.50f, 0.40f, 1.20f, 1.00f, 1.00f, 0.45f, 0.20f, 0.68f, 0.00f, 0.30f,
		0.50f, 0.42f, 0.40f, 0.35f, 0.55f, 0.85f, 0.60f, 0.12f, 0.60f, 0.50f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.45f, 0.62f, 600.00f, 2.40f,
		0.00f, 0.00f, 0.00f, 0.65f,
		0.30f, 0.00f, 0.85f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_CORRIDOR
	},

	// 79: 展示室 - 展示室。RT60約1.0s、中規模の中立的空間、程よい拡散、パネルの吸音、美術館(44)より小さく近接感のある残響
	{
		0.34f, 45.00f, 0.33f,
		{ 7.00f, 0.46f, 15.00f, 0.39f, 23.96f, 0.32f, 34.00f, 0.27f, 45.23f, 0.23f, 57.82f, 0.19f, 71.92f, 0.16f, 87.71f, 0.14f },
		10500.00f, 75.00f, 1.05f, 0.00f, 0.24f, 0.00f, 0.60f, 15.60f, 0.55f, 1.60f, 0.50f, 0.58f, 0.25f,
		0.42f, 0.60f, 0.50f, 0.52f, 1.00f, 1.00f, 1.00f, 0.35f, 0.31f, 0.45f, 0.00f, 0.55f,
		0.35f, 0.50f, 0.52f, 0.50f, 0.40f, 1.20f, 1.20f, 0.25f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.58f, 911.80f, 1.00f,
		0.00f, 0.25f, 0.00f, 0.35f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 80: アトリエ - 採光の良いアトリエ。RT60約1.1s、木の床と大きなガラス窓の混合反射、明るく風通しの良い響き、程よい拡散
	{
		0.36f, 48.00f, 0.34f,
		{ 7.00f, 0.48f, 15.00f, 0.40f, 23.96f, 0.34f, 34.00f, 0.28f, 45.23f, 0.24f, 57.82f, 0.20f, 71.92f, 0.17f, 87.71f, 0.14f },
		11500.00f, 80.00f, 1.20f, 0.00f, 0.00f, 0.00f, 0.55f, 12.00f, 0.50f, 1.50f, 0.50f, 0.55f, 0.30f,
		0.43f, 0.64f, 0.50f, 0.58f, 1.00f, 1.00f, 1.00f, 0.44f, 0.37f, 0.48f, 0.00f, 0.45f,
		0.35f, 0.55f, 0.58f, 0.50f, 0.40f, 1.30f, 1.20f, 0.30f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.55f, 1000.00f, 1.00f,
		0.00f, 0.35f, 0.45f, 0.00f,
		0.35f, 0.00f, 0.50f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.00f,
		TYPE_MEDIUM_ROOM
	},

	// 81: サイバーパンク路地 - ネオン輝く狭い路地。RT60約1.8s、濡れたコンクリと金属の反射、湿った空気、僅かな歪みとフェイズ、ネオン機器のうなり
	{
		0.50f, 90.00f, 0.42f,
		{ 7.00f, 0.55f, 16.00f, 0.48f, 26.08f, 0.42f, 37.37f, 0.36f, 50.01f, 0.32f, 64.18f, 0.27f, 80.04f, 0.24f, 97.80f, 0.21f },
		8500.00f, 65.00f, 1.40f, 0.15f, 0.40f, 0.00f, 0.45f, 12.00f, 0.45f, 2.00f, 0.50f, 0.65f, 0.30f,
		0.52f, 0.92f, 0.50f, 0.50f, 1.20f, 1.00f, 1.00f, 0.47f, 0.34f, 0.62f, 0.00f, 0.30f,
		0.50f, 0.40f, 0.50f, 0.35f, 0.55f, 1.20f, 1.20f, 0.20f, 0.40f, 0.35f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.65f, 800.00f, 2.20f,
		0.40f, 0.00f, 0.00f, 0.60f,
		0.60f, 0.00f, 0.70f, 0.20f,
		0.00f, 0.15f, 0.20f, 0.20f,
		TYPE_CORRIDOR
	},

	// 82: 宇宙船ブリッジ - 宇宙船の艦橋。RT60約1.0s、金属とガラスの制御された反射、清潔なフェイズとシマー、広いステレオ、1.2kHzの機器共鳴
	{
		0.40f, 45.00f, 0.33f,
		{ 6.00f, 0.50f, 13.00f, 0.42f, 20.84f, 0.36f, 29.62f, 0.31f, 39.46f, 0.26f, 50.47f, 0.22f, 62.81f, 0.19f, 76.62f, 0.16f },
		12000.00f, 75.00f, 1.80f, 0.00f, 0.00f, 0.00f, 0.55f, 10.00f, 0.50f, 1.60f, 0.50f, 0.55f, 0.30f,
		0.42f, 0.60f, 0.50f, 0.60f, 1.00f, 1.00f, 1.00f, 0.32f, 0.59f, 0.46f, 0.00f, 0.35f,
		0.25f, 0.40f, 0.60f, 0.40f, 0.40f, 1.00f, 1.10f, 0.20f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.55f, 0.55f, 1200.00f, 2.00f,
		0.60f, 0.40f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.80f, 0.00f,
		0.20f, 0.00f, 0.10f, 0.25f,
		TYPE_SF_SPACE
	},

	// 83: ワープトンネル - ワープ航行トンネル。RT60約2.2s、強いドップラーと高速モジュレーション、深いフェイズとシマー、極端に広いステレオ、加速する運動感
	{
		0.55f, 110.00f, 0.46f,
		{ 6.00f, 0.50f, 15.00f, 0.43f, 25.08f, 0.38f, 36.37f, 0.33f, 49.01f, 0.29f, 63.18f, 0.25f, 79.04f, 0.22f, 96.80f, 0.19f },
		11000.00f, 70.00f, 2.20f, 0.50f, 1.50f, 0.00f, 0.50f, 10.00f, 0.45f, 2.60f, 0.50f, 0.65f, 0.35f,
		0.56f, 1.08f, 0.50f, 0.55f, 1.00f, 1.00f, 1.00f, 0.30f, 0.35f, 0.59f, 0.00f, 0.40f,
		0.40f, 0.40f, 0.55f, 0.40f, 0.50f, 1.00f, 1.00f, 0.30f, 0.30f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.65f, 1000.00f, 1.80f,
		0.40f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.60f, 0.00f,
		0.40f, 0.70f, 0.20f, 0.50f,
		TYPE_SF_SPACE
	},

	// 84: 量子ホール - 量子ホール。RT60約3.5s、きらめくシマーと揺らぐフェイズ、ガラス質の反射、超高密度拡散、広大で幻想的な残響
	{
		0.58f, 125.00f, 0.60f,
		{ 8.00f, 0.50f, 20.00f, 0.45f, 33.44f, 0.41f, 48.49f, 0.36f, 65.35f, 0.33f, 84.23f, 0.30f, 105.38f, 0.27f, 129.07f, 0.24f },
		13000.00f, 60.00f, 2.00f, 0.30f, 0.60f, 0.00f, 0.70f, 20.00f, 0.45f, 3.20f, 0.50f, 0.80f, 0.40f,
		0.72f, 1.60f, 0.50f, 0.60f, 1.00f, 1.00f, 1.10f, 0.29f, 0.54f, 0.44f, 0.00f, 0.40f,
		0.30f, 0.40f, 0.60f, 0.45f, 0.45f, 1.00f, 1.00f, 0.40f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.75f, 0.80f, 1500.00f, 2.00f,
		0.30f, 0.50f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.45f, 0.00f,
		0.70f, 0.00f, 0.00f, 0.40f,
		TYPE_SF_SPACE
	},

	// 85: 無限回廊 - 果てしない回廊。RT60約4.0s、高いフィードバックで延々と続く反射、フラッターとコム、フェイズ、終わらないエコーの連鎖、600Hz共鳴
	{
		0.55f, 150.00f, 0.82f,
		{ 6.00f, 0.60f, 14.00f, 0.54f, 22.96f, 0.49f, 33.00f, 0.44f, 44.23f, 0.39f, 56.82f, 0.35f, 70.92f, 0.32f, 86.71f, 0.29f },
		9500.00f, 65.00f, 1.60f, 0.00f, 0.00f, 0.00f, 0.45f, 10.00f, 0.42f, 2.80f, 0.50f, 0.70f, 0.30f,
		0.78f, 1.80f, 0.50f, 0.45f, 1.20f, 1.00f, 1.00f, 0.41f, 0.32f, 0.70f, 0.00f, 0.40f,
		0.40f, 0.40f, 0.45f, 0.35f, 0.50f, 1.10f, 1.30f, 0.15f, 0.50f, 0.50f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.70f, 600.00f, 2.20f,
		0.35f, 0.00f, 0.00f, 0.40f,
		0.30f, 0.00f, 0.70f, 0.00f,
		0.00f, 0.00f, 0.00f, 0.35f,
		TYPE_SF_SPACE
	},

	// 86: 逆再生空間 - 逆再生の空間。RT60約2.5s、極めて滑らかな盛り上がる残響、フェイズとシマー、明るくなるフィードバック音色、幻想的な広がり
	{
		0.60f, 100.00f, 0.49f,
		{ 8.00f, 0.40f, 18.00f, 0.36f, 29.20f, 0.32f, 41.74f, 0.29f, 55.79f, 0.26f, 71.53f, 0.24f, 89.15f, 0.21f, 108.89f, 0.19f },
		11000.00f, 60.00f, 1.80f, 0.35f, 0.40f, 0.00f, 0.75f, 18.00f, 0.45f, 2.40f, 0.50f, 0.70f, 0.35f,
		0.60f, 1.20f, 0.98f, 0.55f, 1.00f, 1.00f, 1.00f, 0.26f, 0.23f, 0.40f, 0.50f, 0.40f,
		0.30f, 0.45f, 0.55f, 0.50f, 0.45f, 1.00f, 1.00f, 0.35f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.65f, 0.70f, 1200.00f, 1.80f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.50f, 0.00f,
		0.30f, 0.00f, 0.05f, 0.40f,
		TYPE_SF_SPACE
	},

	// 87: タイムストップ室 - 時間停止の部屋。RT60約0.3s、ほぼ凍りついた無響、深いフェイズで固まった静止感、僅かに共鳴する高Q(4.0)、極端に狭いステレオ
	{
		0.15f, 20.00f, 0.20f,
		{ 5.00f, 0.30f, 10.00f, 0.18f, 15.60f, 0.11f, 21.87f, 0.06f, 28.90f, 0.04f, 36.76f, 0.02f, 45.58f, 0.01f, 55.45f, 0.01f },
		9000.00f, 70.00f, 0.50f, 0.05f, 0.05f, 0.00f, 0.30f, 6.00f, 0.70f, 1.00f, 0.50f, 0.35f, 0.10f,
		0.34f, 0.32f, 0.50f, 0.40f, 1.00f, 1.00f, 1.00f, 0.26f, 0.23f, 0.56f, 0.00f, 0.40f,
		0.25f, 0.40f, 0.40f, 0.50f, 0.40f, 1.00f, 1.00f, 0.10f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.30f, 0.35f, 1000.00f, 4.00f,
		0.20f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.70f, 0.00f,
		0.10f, 0.00f, 0.00f, 0.60f,
		TYPE_SF_SPACE
	},

	// 88: データセンター - データセンター。RT60約0.9s、金属ラックの反射、低い120Hzの空調ハムと高Q共鳴、コムフィルタ、僅かな歪みと低速モジュ
	{
		0.36f, 40.00f, 0.32f,
		{ 5.00f, 0.50f, 11.00f, 0.42f, 17.72f, 0.36f, 25.25f, 0.31f, 33.68f, 0.26f, 43.12f, 0.22f, 53.69f, 0.19f, 65.53f, 0.16f },
		9000.00f, 55.00f, 1.20f, 0.10f, 0.20f, 0.00f, 0.40f, 8.00f, 0.50f, 1.60f, 0.50f, 0.60f, 0.20f,
		0.41f, 0.56f, 0.50f, 0.50f, 1.20f, 1.00f, 1.00f, 0.43f, 0.39f, 0.59f, 0.00f, 0.35f,
		0.35f, 0.40f, 0.50f, 0.35f, 0.50f, 1.00f, 1.00f, 0.15f, 0.25f, 0.30f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.50f, 0.60f, 120.00f, 3.50f,
		0.55f, 0.00f, 0.00f, 0.40f,
		0.30f, 0.00f, 0.75f, 0.00f,
		0.00f, 0.00f, 0.15f, 0.00f,
		TYPE_METAL_SPACE
	},

	// 89: 巨大機械内部 - 巨大機械の内部。RT60約2.4s、鋼板の強い金属反射と共鳴、歪みと荒れたコム、フラッター、300Hz共鳴、重く低い響き
	{
		0.52f, 95.00f, 0.48f,
		{ 7.00f, 0.58f, 16.00f, 0.51f, 26.08f, 0.45f, 37.37f, 0.40f, 50.01f, 0.35f, 64.18f, 0.31f, 80.04f, 0.27f, 97.80f, 0.24f },
		8000.00f, 55.00f, 1.40f, 0.00f, 0.00f, 0.00f, 0.45f, 12.00f, 0.40f, 2.40f, 0.50f, 0.70f, 0.25f,
		0.59f, 1.16f, 0.50f, 0.50f, 1.40f, 1.00f, 1.00f, 0.45f, 0.48f, 0.61f, 0.00f, 0.25f,
		0.55f, 0.40f, 0.50f, 0.30f, 0.70f, 1.20f, 1.30f, 0.15f, 0.40f, 0.45f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.65f, 0.70f, 300.00f, 3.00f,
		0.80f, 0.00f, 0.00f, 0.35f,
		0.30f, 0.00f, 0.80f, 0.00f,
		0.00f, 0.10f, 0.30f, 0.00f,
		TYPE_METAL_SPACE
	},

	// 90: AIホログラム室 - AIホログラム室。RT60約1.6s、ガラス質の清潔な反射、明るいシマーとフェイズ、広いステレオ、2kHzの澄んだ共鳴、人工的で滑らか
	{
		0.44f, 65.00f, 0.40f,
		{ 6.00f, 0.50f, 14.00f, 0.43f, 22.96f, 0.38f, 33.00f, 0.33f, 44.23f, 0.29f, 56.82f, 0.25f, 70.92f, 0.22f, 86.71f, 0.19f },
		13000.00f, 70.00f, 1.90f, 0.20f, 0.50f, 0.00f, 0.60f, 12.00f, 0.45f, 1.80f, 0.50f, 0.60f, 0.35f,
		0.49f, 0.84f, 0.50f, 0.68f, 1.00f, 1.00f, 1.10f, 0.29f, 0.60f, 0.46f, 0.00f, 0.40f,
		0.25f, 0.40f, 0.68f, 0.45f, 0.40f, 1.00f, 1.00f, 0.30f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.60f, 2000.00f, 2.00f,
		0.30f, 0.60f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.50f, 0.00f, 0.00f, 0.30f,
		TYPE_SF_SPACE
	},

	// 91: 重力ゼロ船庫 - 無重力の格納庫。RT60約3.2s、巨大な金属空間、浮遊感のあるフェイズとドップラー、広大なステレオ、500Hzの金属共鳴
	{
		0.55f, 130.00f, 0.57f,
		{ 9.00f, 0.50f, 22.00f, 0.45f, 36.56f, 0.40f, 52.87f, 0.35f, 71.13f, 0.31f, 91.59f, 0.28f, 114.50f, 0.25f, 140.16f, 0.22f },
		9500.00f, 55.00f, 2.10f, 0.20f, 0.40f, 0.00f, 0.60f, 18.00f, 0.45f, 3.40f, 0.50f, 0.70f, 0.40f,
		0.68f, 1.48f, 0.50f, 0.52f, 1.25f, 1.00f, 1.00f, 0.42f, 0.41f, 0.50f, 0.00f, 0.40f,
		0.35f, 0.40f, 0.52f, 0.40f, 0.55f, 1.00f, 1.00f, 0.50f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.65f, 0.70f, 500.00f, 2.00f,
		0.60f, 0.00f, 0.00f, 0.35f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.30f, 0.30f, 0.00f, 0.30f,
		TYPE_SF_SPACE
	},

	// 92: 惑星ドーム都市 - 惑星のドーム都市。RT60約2.8s、巨大なガラスドームの開放的空間、僅かなシマー、風、高度感、広く澄んだ残響
	{
		0.50f, 120.00f, 0.53f,
		{ 11.00f, 0.46f, 26.00f, 0.40f, 44.00f, 0.36f, 65.60f, 0.31f, 91.52f, 0.28f, 122.62f, 0.24f, 159.95f, 0.21f, 204.74f, 0.19f },
		11500.00f, 60.00f, 1.90f, 0.00f, 0.00f, 0.00f, 0.65f, 22.00f, 0.48f, 3.60f, 0.50f, 0.70f, 0.50f,
		0.64f, 1.32f, 0.50f, 0.58f, 1.00f, 1.00f, 1.00f, 0.37f, 0.42f, 0.60f, 0.00f, 0.40f,
		0.30f, 0.45f, 0.58f, 0.45f, 0.45f, 1.00f, 1.00f, 0.70f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.65f, 0.70f, 1300.00f, 1.80f,
		0.00f, 0.45f, 0.00f, 0.40f,
		0.30f, 0.40f, 0.40f, 0.25f,
		0.25f, 0.00f, 0.00f, 0.00f,
		TYPE_SF_SPACE
	},

	// 93: VRシミュレーター - VRシミュレーター。RT60約1.4s、絶えず変化する人工音場、高速フェイズとモジュレーション、シマー、僅かな歪み、広いステレオ
	{
		0.46f, 60.00f, 0.37f,
		{ 6.00f, 0.50f, 14.00f, 0.43f, 22.96f, 0.37f, 33.00f, 0.32f, 44.23f, 0.27f, 56.82f, 0.24f, 70.92f, 0.20f, 86.71f, 0.17f },
		12000.00f, 70.00f, 1.90f, 0.40f, 0.80f, 0.00f, 0.55f, 10.00f, 0.48f, 1.80f, 0.50f, 0.60f, 0.30f,
		0.47f, 0.76f, 0.50f, 0.60f, 1.00f, 1.00f, 1.00f, 0.29f, 0.45f, 0.48f, 0.00f, 0.40f,
		0.30f, 0.40f, 0.60f, 0.40f, 0.40f, 1.00f, 1.00f, 0.30f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.60f, 1600.00f, 2.00f,
		0.30f, 0.30f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.55f, 0.00f,
		0.35f, 0.20f, 0.15f, 0.45f,
		TYPE_SF_SPACE
	},

	// 94: レーザー通路 - レーザーの狭い通路。RT60約1.2s、金属壁の強いフラッターとコム、明るいシマーとフェイズ、1.8kHzの高Q共鳴、僅かな歪みとドップラー
	{
		0.44f, 55.00f, 0.35f,
		{ 5.00f, 0.56f, 11.00f, 0.49f, 17.72f, 0.42f, 25.25f, 0.37f, 33.68f, 0.32f, 43.12f, 0.28f, 53.69f, 0.24f, 65.53f, 0.21f },
		12500.00f, 75.00f, 1.50f, 0.00f, 0.00f, 0.00f, 0.40f, 8.00f, 0.42f, 1.60f, 0.50f, 0.62f, 0.25f,
		0.44f, 0.68f, 0.50f, 0.65f, 1.00f, 1.00f, 1.10f, 0.33f, 0.44f, 0.67f, 0.00f, 0.30f,
		0.35f, 0.35f, 0.65f, 0.30f, 0.45f, 0.90f, 0.90f, 0.15f, 0.55f, 0.55f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.55f, 0.62f, 1800.00f, 3.00f,
		0.60f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.75f, 0.00f,
		0.40f, 0.15f, 0.20f, 0.35f,
		TYPE_CORRIDOR
	},

	// 95: 異次元裂け目 - 異次元の裂け目。RT60約3.0s、混沌としたフェイズ・ドップラー・歪み、深いモジュレーション、極端に広いステレオ、高フィードバックの不安定な残響
	{
		0.62f, 120.00f, 0.80f,
		{ 6.00f, 0.55f, 16.00f, 0.50f, 27.20f, 0.45f, 39.74f, 0.40f, 53.79f, 0.36f, 69.53f, 0.32f, 87.15f, 0.29f, 106.89f, 0.26f },
		10000.00f, 60.00f, 2.40f, 0.60f, 1.20f, 0.00f, 0.55f, 12.00f, 0.42f, 3.00f, 0.50f, 0.72f, 0.35f,
		0.66f, 1.40f, 0.50f, 0.50f, 1.00f, 1.00f, 1.00f, 0.30f, 0.32f, 0.60f, 0.00f, 0.40f,
		0.45f, 0.40f, 0.50f, 0.35f, 0.50f, 1.00f, 1.00f, 0.35f, 0.40f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.80f, 0.72f, 900.00f, 2.40f,
		0.30f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.50f, 0.00f,
		0.50f, 0.50f, 0.50f, 0.70f,
		TYPE_SF_SPACE
	},

	// 96: 夢の中 - 夢の中。RT60約2.8s、極めて滑らかでぼやけた残響、LPF5kHzの霞、柔らかなフェイズとシマー、高密度拡散、包み込む幻想感
	{
		0.55f, 105.00f, 0.53f,
		{ 9.00f, 0.45f, 21.00f, 0.41f, 34.44f, 0.36f, 49.49f, 0.33f, 66.35f, 0.30f, 85.23f, 0.27f, 106.38f, 0.24f, 130.07f, 0.22f },
		5000.00f, 55.00f, 1.90f, 0.30f, 0.30f, 0.00f, 0.85f, 20.00f, 0.60f, 2.60f, 0.50f, 0.80f, 0.35f,
		0.64f, 1.32f, 0.98f, 0.40f, 1.30f, 1.00f, 0.70f, 0.25f, 0.18f, 0.36f, 0.00f, 0.40f,
		0.25f, 0.60f, 0.40f, 0.90f, 0.45f, 1.00f, 1.00f, 0.35f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.80f, 800.00f, 1.40f,
		0.00f, 0.00f, 0.00f, 0.00f,
		0.30f, 0.00f, 0.45f, 0.00f,
		0.35f, 0.00f, 0.00f, 0.30f,
		TYPE_SF_SPACE
	},

	// 97: 水晶洞 - 水晶の洞窟。RT60約3.6s、鍾乳洞(56)と違い明るく澄んで鳴り響く、ガラス質のきらめきと4.2kHz高Q共鳴、水滴、僅かなフェイズ
	{
		0.60f, 130.00f, 0.62f,
		{ 8.00f, 0.50f, 20.00f, 0.45f, 34.40f, 0.40f, 51.68f, 0.35f, 72.42f, 0.31f, 97.30f, 0.28f, 127.16f, 0.25f, 162.99f, 0.22f },
		14000.00f, 70.00f, 1.60f, 0.12f, 0.40f, 0.00f, 0.50f, 18.00f, 0.40f, 3.00f, 0.50f, 0.65f, 0.35f,
		0.73f, 1.64f, 0.50f, 0.72f, 1.00f, 1.00f, 1.15f, 0.30f, 0.69f, 0.48f, 0.00f, 0.40f,
		0.30f, 0.40f, 0.72f, 0.40f, 0.50f, 1.40f, 1.50f, 0.20f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.70f, 0.65f, 4200.00f, 3.00f,
		0.35f, 0.75f, 0.00f, 0.00f,
		0.70f, 0.00f, 0.70f, 0.00f,
		0.60f, 0.00f, 0.00f, 0.20f,
		TYPE_CAVE
	},

	// 98: 廃宇宙ステーション - 廃棄された宇宙ステーション。RT60約2.6s、空虚な金属の反射、フェイズと僅かな歪み、フラッター、エアリークの風、400Hz共鳴、寂寥感
	{
		0.52f, 110.00f, 0.51f,
		{ 8.00f, 0.52f, 19.00f, 0.46f, 31.32f, 0.40f, 45.12f, 0.35f, 60.57f, 0.31f, 77.88f, 0.27f, 97.27f, 0.24f, 118.98f, 0.21f },
		8500.00f, 60.00f, 1.70f, 0.15f, 0.20f, 0.00f, 0.45f, 14.00f, 0.45f, 2.80f, 0.50f, 0.68f, 0.35f,
		0.61f, 1.24f, 0.50f, 0.42f, 1.20f, 1.00f, 1.00f, 0.42f, 0.38f, 0.62f, 0.00f, 0.30f,
		0.45f, 0.40f, 0.42f, 0.35f, 0.55f, 1.30f, 1.40f, 0.25f, 0.35f, 0.35f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.60f, 0.68f, 400.00f, 2.20f,
		0.55f, 0.00f, 0.00f, 0.35f,
		0.30f, 0.00f, 0.65f, 0.15f,
		0.00f, 0.10f, 0.25f, 0.35f,
		TYPE_METAL_SPACE
	},

	// 99: ブラックホール縁 - ブラックホールの縁。RT60約4.5s、極端なドップラーと重力による音程変化、深いフェイズと歪み、LPF6kHz、超広ステレオ、重く長大な残響
	{
		0.60f, 160.00f, 0.85f,
		{ 10.00f, 0.50f, 25.00f, 0.46f, 41.80f, 0.42f, 60.62f, 0.39f, 81.69f, 0.36f, 105.29f, 0.33f, 131.73f, 0.30f, 161.34f, 0.28f },
		6000.00f, 45.00f, 2.40f, 0.50f, 0.30f, 0.00f, 0.60f, 20.00f, 0.50f, 4.00f, 0.50f, 0.75f, 0.40f,
		0.84f, 2.00f, 0.50f, 0.35f, 1.60f, 1.00f, 0.50f, 0.29f, 0.28f, 0.48f, 0.00f, 0.40f,
		0.40f, 0.40f, 0.35f, 0.40f, 0.90f, 1.00f, 1.00f, 0.40f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.80f, 0.75f, 700.00f, 2.20f,
		0.30f, 0.00f, 0.00f, 0.00f,
		0.30f, 1.00f, 0.40f, 0.00f,
		0.30f, 0.90f, 0.40f, 0.60f,
		TYPE_SF_SPACE
	},

	// 100: サイバー聖堂 - サイバー聖堂。RT60約5.0s、カテドラルの超高密度拡散にデジタルのシマーとフェイズを融合、ガラスと金属、重厚かつ幻想的な広大残響
	{
		0.60f, 150.00f, 0.77f,
		{ 20.00f, 0.48f, 38.00f, 0.44f, 59.24f, 0.40f, 84.30f, 0.36f, 113.88f, 0.33f, 148.78f, 0.30f, 189.96f, 0.27f, 238.55f, 0.25f },
		12000.00f, 55.00f, 2.10f, 0.20f, 0.40f, 0.00f, 0.85f, 40.00f, 0.48f, 3.80f, 0.50f, 0.88f, 0.45f,
		0.90f, 2.20f, 0.50f, 0.55f, 1.40f, 1.00f, 1.00f, 0.43f, 0.56f, 0.37f, 0.00f, 0.40f,
		0.35f, 0.45f, 0.55f, 0.45f, 0.75f, 2.00f, 1.90f, 0.40f, 0.00f, 0.00f,
		{ 0.00f, 0.00f, 0.00f, 0.00f }, { 0.00f, 0.00f, 0.00f, 0.00f }, 1.00f, 0.00f,
		0.85f, 0.88f, 1400.00f, 1.80f,
		0.40f, 0.50f, 0.00f, 0.45f,
		0.30f, 0.00f, 0.50f, 0.00f,
		0.55f, 0.00f, 0.00f, 0.35f,
		TYPE_SF_SPACE
	},

};


// ============================================================
//  Hyper DSP Equaliser & Physical Acoustic Environment Engine
//  - ISO 2/3オクターブ倍精度15バンドEQ (隣接バンド相互作用自動補正)
//  - 位相整合 3バンド Linkwitz-Riley 4次 ダイナミック・ヘッドルーム保護
//    (低域/高域を上げても他バンドや全体音量を下げず、音割れを防止)
//  - 8ch Hadamard FDN + 16タップ L/R非対称初期反射 + 空間固有物理モデル
//    (ピンポン山彦 / 平行壁フラッター / 定在波コム / +1octシマー /
//     分数遅延ドップラー / 4段ステレオフェイザー / 空間共鳴＆材質モデル)
//  - ゼロレイテンシ真のルックアヘッド・トゥルーピークリミッター (-0.20 dBTP)
//  - std::コンテナ / 再生中 new/delete/malloc/free 一切不使用
// ============================================================

static inline float ClampFloat(float v, float lo, float hi) {
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline double ClampDouble(double v, double lo, double hi) {
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline float FlushDenormalF(float x) {
	return (fabsf(x) < 1e-20f) ? 0.0f : x;
}

static inline double FlushDenormalD(double x) {
	return (fabs(x) < 1e-24) ? 0.0 : x;
}

// ============================================================
//  倍精度バイクワッドフィルタ設計＆処理群 (Direct Form II Transposed)
// ============================================================

static inline void ResetBiquadD(BiquadD* f) {
	f->s1 = 0.0;
	f->s2 = 0.0;
}

static inline void SetBiquadBypass(BiquadD* f) {
	f->b0 = 1.0; f->b1 = 0.0; f->b2 = 0.0;
	f->a1 = 0.0; f->a2 = 0.0;
	f->bypass = 1;
}

// ピーキングEQ係数計算 (gainDb: dB単位、|gainDb| < 0.05dB で完全バイパス)
static void CalcPeakingEQDb(BiquadD* f, double freq, double q, double gainDb, int rate) {
	if (rate < 8000) rate = 8000;
	if (fabs(gainDb) < 0.05) {
		SetBiquadBypass(f);
		return;
	}
	double maxFreq = (double)rate * 0.47;
	freq = ClampDouble(freq, 12.0, maxFreq);
	q = ClampDouble(q, 0.25, 16.0);
	gainDb = ClampDouble(gainDb, -24.0, 24.0);

	double omega = 2.0 * M_PI * freq / (double)rate;
	double sn = sin(omega), cs = cos(omega);
	double alpha = sn / (2.0 * q);
	double A = pow(10.0, gainDb / 40.0);
	double a0 = 1.0 + alpha / A;
	if (!isfinite(a0) || fabs(a0) < 1e-12) {
		SetBiquadBypass(f);
		return;
	}
	f->b0 = (1.0 + alpha * A) / a0;
	f->b1 = (-2.0 * cs) / a0;
	f->b2 = (1.0 - alpha * A) / a0;
	f->a1 = (-2.0 * cs) / a0;
	f->a2 = (1.0 - alpha / A) / a0;
	f->bypass = 0;
	if (!isfinite(f->b0) || !isfinite(f->b1) || !isfinite(f->b2) ||
		!isfinite(f->a1) || !isfinite(f->a2)) {
		SetBiquadBypass(f);
	}
}

// 2次ローパス/ハイパスフィルタ係数計算 (isHigh==0: LPF, isHigh==1: HPF)
static void CalcBiquadFilter(BiquadD* f, int isHigh, double freq, double q, int rate) {
	if (rate < 8000) rate = 8000;
	double maxFreq = (double)rate * 0.48;
	freq = ClampDouble(freq, 10.0, maxFreq);
	q = ClampDouble(q, 0.25, 16.0);

	double omega = 2.0 * M_PI * freq / (double)rate;
	double sn = sin(omega), cs = cos(omega);
	double alpha = sn / (2.0 * q);
	double a0 = 1.0 + alpha;
	if (!isfinite(a0) || fabs(a0) < 1e-12) {
		SetBiquadBypass(f);
		return;
	}
	if (!isHigh) {
		f->b0 = ((1.0 - cs) * 0.5) / a0;
		f->b1 = (1.0 - cs) / a0;
		f->b2 = ((1.0 - cs) * 0.5) / a0;
	}
	else {
		f->b0 = ((1.0 + cs) * 0.5) / a0;
		f->b1 = (-(1.0 + cs)) / a0;
		f->b2 = ((1.0 + cs) * 0.5) / a0;
	}
	f->a1 = (-2.0 * cs) / a0;
	f->a2 = (1.0 - alpha) / a0;
	f->bypass = 0;
}

// 2次オールパスフィルタ係数計算 (LR4 3バンド位相整合用)
static void CalcAllpass2(BiquadD* f, double freq, double q, int rate) {
	if (rate < 8000) rate = 8000;
	double maxFreq = (double)rate * 0.48;
	freq = ClampDouble(freq, 10.0, maxFreq);
	q = ClampDouble(q, 0.25, 16.0);

	double omega = 2.0 * M_PI * freq / (double)rate;
	double sn = sin(omega), cs = cos(omega);
	double alpha = sn / (2.0 * q);
	double a0 = 1.0 + alpha;
	f->b0 = (1.0 - alpha) / a0;
	f->b1 = (-2.0 * cs) / a0;
	f->b2 = (1.0 + alpha) / a0;
	f->a1 = (-2.0 * cs) / a0;
	f->a2 = (1.0 - alpha) / a0;
	f->bypass = 0;
}

// シェルビングEQ係数計算 (isHigh==0: ローシェルフ, isHigh==1: ハイシェルフ)
static void CalcShelvingEQDb(BiquadD* f, int isHigh, double freq, double gainDb, int rate) {
	if (rate < 8000) rate = 8000;
	if (fabs(gainDb) < 0.05) {
		SetBiquadBypass(f);
		return;
	}
	double maxFreq = (double)rate * 0.47;
	freq = ClampDouble(freq, 15.0, maxFreq);
	gainDb = ClampDouble(gainDb, -24.0, 24.0);

	double omega = 2.0 * M_PI * freq / (double)rate;
	double sn = sin(omega), cs = cos(omega);
	double A = pow(10.0, gainDb / 40.0);
	// S = 1.0 (Butterworth 最平坦スロープ): beta = sqrt(2*A)
	double beta = sqrt(2.0 * A);

	if (!isHigh) {
		double a0 = (A + 1.0) + (A - 1.0) * cs + beta * sn;
		f->b0 = (A * ((A + 1.0) - (A - 1.0) * cs + beta * sn)) / a0;
		f->b1 = (2.0 * A * ((A - 1.0) - (A + 1.0) * cs)) / a0;
		f->b2 = (A * ((A + 1.0) - (A - 1.0) * cs - beta * sn)) / a0;
		f->a1 = (-2.0 * ((A - 1.0) + (A + 1.0) * cs)) / a0;
		f->a2 = ((A + 1.0) + (A - 1.0) * cs - beta * sn) / a0;
	}
	else {
		double a0 = (A + 1.0) - (A - 1.0) * cs + beta * sn;
		f->b0 = (A * ((A + 1.0) + (A - 1.0) * cs + beta * sn)) / a0;
		f->b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cs)) / a0;
		f->b2 = (A * ((A + 1.0) + (A - 1.0) * cs - beta * sn)) / a0;
		f->a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cs)) / a0;
		f->a2 = ((A + 1.0) - (A - 1.0) * cs - beta * sn) / a0;
	}
	f->bypass = 0;
	if (!isfinite(f->b0) || !isfinite(f->b1) || !isfinite(f->b2) ||
		!isfinite(f->a1) || !isfinite(f->a2)) {
		SetBiquadBypass(f);
	}
}

// バイクワッドの周波数 freq における振幅応答 [dB] を厳密評価
static double EvalBiquadMagDb(const BiquadD* f, double freq, int rate) {
	if (f->bypass || rate < 8000) return 0.0;
	double w = 2.0 * M_PI * freq / (double)rate;
	double cw = cos(w), sw = sin(w);
	double c2w = cos(2.0 * w), s2w = sin(2.0 * w);
	double numRe = f->b0 + f->b1 * cw + f->b2 * c2w;
	double numIm = -(f->b1 * sw + f->b2 * s2w);
	double denRe = 1.0 + f->a1 * cw + f->a2 * c2w;
	double denIm = -(f->a1 * sw + f->a2 * s2w);
	double num2 = numRe * numRe + numIm * numIm;
	double den2 = denRe * denRe + denIm * denIm;
	if (den2 < 1e-24 || num2 < 1e-24) return 0.0;
	return 10.0 * log10(num2 / den2);
}

// 倍精度 Direct Form II Transposed 1サンプル処理
static inline double ProcessBiquadD(BiquadD* f, double in) {
	if (f->bypass) return in;
	double y = f->b0 * in + f->s1;
	f->s1 = f->b1 * in - f->a1 * y + f->s2;
	f->s2 = f->b2 * in - f->a2 * y;
	if (!isfinite(y)) {
		f->s1 = 0.0;
		f->s2 = 0.0;
		return 0.0;
	}
	return y;
}

// ============================================================
//  15バンド ISO 2/3オクターブ 定QグラフィックEQ 係数更新
//  隣接する複数バンドを同時にブースト/カットした際の過大な重なり（+22dB暴走）を
//  3反復のガウス・ザイデル法で自動補正し、各バンド中心周波数で厳密に目標dBへ一致させる。
//  目標 0dB のバンドは一切逆打ちせず 0.0dB 完全スルーを保つ。
// ============================================================
static void UpdateGraphicEq15(ChannelState* csArr, int numCh, const int eqVals[EQ_BANDS], int rate) {
	static const double kBandQ[EQ_BANDS] = {
		1.85, 2.10, 2.14, 2.14, 2.14,
		2.14, 2.14, 2.14, 2.14, 2.14,
		2.14, 2.14, 2.05, 1.85, 1.55
	};
	double targetDb[EQ_BANDS];
	double effDb[EQ_BANDS];
	int anyActive = 0;

	for (int b = 0; b < EQ_BANDS; ++b) {
		int v = eqVals[b];
		if (v < 0) v = 0;
		if (v > 200) v = 200;
		targetDb[b] = ((double)v - 100.0) * 0.12; // ±12.0 dB
		effDb[b] = targetDb[b];
		if (fabs(targetDb[b]) >= 0.05) anyActive = 1;
	}

	BiquadD proto[EQ_BANDS];
	if (!anyActive) {
		for (int b = 0; b < EQ_BANDS; ++b) SetBiquadBypass(&proto[b]);
	}
	else {
		// 反復相互作用補正: 隣接バンドが同方向に重なって目標を超過する分だけ抑える
		for (int iter = 0; iter < 3; ++iter) {
			for (int j = 0; j < EQ_BANDS; ++j)
				CalcPeakingEQDb(&proto[j], EQ_FREQS[j], kBandQ[j], effDb[j], rate);
			for (int i = 0; i < EQ_BANDS; ++i) {
				if (fabs(targetDb[i]) < 0.05) {
					effDb[i] = 0.0;
					continue;
				}
				double sumDb = 0.0;
				for (int j = 0; j < EQ_BANDS; ++j) {
					if (!proto[j].bypass)
						sumDb += EvalBiquadMagDb(&proto[j], EQ_FREQS[i], rate);
				}
				double err = targetDb[i] - sumDb;
				effDb[i] = ClampDouble(effDb[i] + err * 0.72, -14.5, 14.5);
			}
		}
		for (int b = 0; b < EQ_BANDS; ++b)
			CalcPeakingEQDb(&proto[b], EQ_FREQS[b], kBandQ[b], effDb[b], rate);
	}

	// 全チャンネルへ係数のみ反映 (状態 s1/s2 は維持してスライダー操作時のクリックを防止)
	for (int ch = 0; ch < numCh; ++ch) {
		for (int b = 0; b < EQ_BANDS; ++b) {
			BiquadD* dst = &csArr[ch].eqFilters[b];
			dst->b0 = proto[b].b0;
			dst->b1 = proto[b].b1;
			dst->b2 = proto[b].b2;
			dst->a1 = proto[b].a1;
			dst->a2 = proto[b].a2;
			dst->bypass = proto[b].bypass;
			if (dst->bypass) {
				dst->s1 = 0.0;
				dst->s2 = 0.0;
			}
		}
	}
}

// ============================================================
//  位相整合 3バンド Linkwitz-Riley 4次 (LR4) ダイナミック・ヘッドルーム保護
//  クロスオーバー: Low < 220Hz / Mid 220〜3200Hz / High > 3200Hz
//  Low 経路に 3200Hz 2次オールパスを挿入することで、
//  Low' + Mid + High の合成特性は 20Hz〜20kHz で厳密に 0.000dB フラット。
//  各帯域のピークが閾値以下のときはゲイン 1.000 (完全無加工・透明)。
//  低域や高域を強くブーストしたとき、超過した帯域「だけ」をソフト圧縮し、
//  他帯域の音量や全体ボリュームは一切下げない！
// ============================================================
static void MultibandReset(MultibandProtector* mb, int rate) {
	if (rate < 8000) rate = 8000;
	memset(mb, 0, sizeof(*mb));
	mb->rate = rate;
	const double kButtQ = 0.7071067811865476; // 1 / sqrt(2)
	const double f1 = 220.0;
	const double f2 = ClampDouble(3200.0, 800.0, (double)rate * 0.38);

	for (int ch = 0; ch < MAX_CH; ++ch) {
		CalcBiquadFilter(&mb->lp1a[ch], 0, f1, kButtQ, rate);
		CalcBiquadFilter(&mb->lp1b[ch], 0, f1, kButtQ, rate);
		CalcBiquadFilter(&mb->hp1a[ch], 1, f1, kButtQ, rate);
		CalcBiquadFilter(&mb->hp1b[ch], 1, f1, kButtQ, rate);
		CalcBiquadFilter(&mb->lp2a[ch], 0, f2, kButtQ, rate);
		CalcBiquadFilter(&mb->lp2b[ch], 0, f2, kButtQ, rate);
		CalcBiquadFilter(&mb->hp2a[ch], 1, f2, kButtQ, rate);
		CalcBiquadFilter(&mb->hp2b[ch], 1, f2, kButtQ, rate);
		CalcAllpass2(&mb->apLow[ch], f2, kButtQ, rate);
	}
	// バンド別アタック/リリース時定数 (Low: 2.0ms/75ms, Mid: 1.0ms/55ms, High: 0.4ms/40ms)
	mb->atkCoeff[0] = expf(-1.0f / (0.0020f * (float)rate));
	mb->atkCoeff[1] = expf(-1.0f / (0.0010f * (float)rate));
	mb->atkCoeff[2] = expf(-1.0f / (0.0004f * (float)rate));
	mb->relCoeff[0] = expf(-1.0f / (0.0750f * (float)rate));
	mb->relCoeff[1] = expf(-1.0f / (0.0550f * (float)rate));
	mb->relCoeff[2] = expf(-1.0f / (0.0400f * (float)rate));
}

// C1 連続ソフトニー保護ゲイン計算 (x <= thresh で厳密 1.0f, x→∞ で ceilVal へ漸近)
static inline float CalcSoftBandGain(float envPeak, float thresh, float ceilVal) {
	if (envPeak <= thresh) return 1.0f;
	float span = ceilVal - thresh;
	float arg = (envPeak - thresh) / span;
	if (arg > 6.0f) arg = 6.0f;
	float target = thresh + span * tanhf(arg);
	return target / envPeak;
}

// ステレオ LR4 3バンド独立ヘッドルーム保護 (ステレオリンク + 動的ヘッドルーム配分)
// 原理:
//   通常レベル以下の他バンドはゲイン 1.000 で完全保護しつつ、
//   合算ピークが天井(0.96)を超えそうなときは「突出してブーストされているバンドだけ」を
//   残余ヘッドルームへ滑らかに収める。これにより後段の広帯域リミッターが動かず、
//   低域や高域を+12dB上げても他バンドの音量が一切下がらない！
static void MultibandProcessStereo(MultibandProtector* mb, float* L, float* R, int n) {
	static const float kBaseThresh[3] = { 0.66f, 0.72f, 0.60f };
	static const float kBaseCeil[3]   = { 0.88f, 0.90f, 0.84f };

	for (int i = 0; i < n; ++i) {
		double xL = (double)L[i];
		double xR = (double)R[i];

		// 1段目: 220Hz LR4 分割
		double lowL = ProcessBiquadD(&mb->lp1b[0], ProcessBiquadD(&mb->lp1a[0], xL));
		double mhL  = ProcessBiquadD(&mb->hp1b[0], ProcessBiquadD(&mb->hp1a[0], xL));
		double lowR = ProcessBiquadD(&mb->lp1b[1], ProcessBiquadD(&mb->lp1a[1], xR));
		double mhR  = ProcessBiquadD(&mb->hp1b[1], ProcessBiquadD(&mb->hp1a[1], xR));

		// 2段目: 3200Hz LR4 分割 + Low経路の位相整合オールパス
		lowL = ProcessBiquadD(&mb->apLow[0], lowL);
		lowR = ProcessBiquadD(&mb->apLow[1], lowR);
		double midL  = ProcessBiquadD(&mb->lp2b[0], ProcessBiquadD(&mb->lp2a[0], mhL));
		double highL = ProcessBiquadD(&mb->hp2b[0], ProcessBiquadD(&mb->hp2a[0], mhL));
		double midR  = ProcessBiquadD(&mb->lp2b[1], ProcessBiquadD(&mb->lp2a[1], mhR));
		double highR = ProcessBiquadD(&mb->hp2b[1], ProcessBiquadD(&mb->hp2a[1], mhR));

		float bL[3] = { (float)lowL, (float)midL, (float)highL };
		float bR[3] = { (float)lowR, (float)midR, (float)highR };
		float effP[3];

		for (int b = 0; b < 3; ++b) {
			float aL = fabsf(bL[b]);
			float aR = fabsf(bR[b]);
			float p  = (aL > aR) ? aL : aR;
			float c  = (p > mb->env[b]) ? mb->atkCoeff[b] : mb->relCoeff[b];
			mb->env[b] = FlushDenormalF(p + c * (mb->env[b] - p));
			effP[b] = (p > mb->env[b]) ? (mb->env[b] * 0.45f + p * 0.55f) : mb->env[b];
		}

		// 各バンドの他バンド占有量から残余ヘッドルームを算出 (通常バンド <= 0.42 は一切下げない)
		float sumEnv = mb->env[0] + mb->env[1] + mb->env[2];
		float outL = 0.0f, outR = 0.0f;
		for (int b = 0; b < 3; ++b) {
			float ceilB   = kBaseCeil[b];
			float threshB = kBaseThresh[b];
			if (sumEnv > 0.96f && mb->env[b] > 0.44f) {
				float otherEnv = sumEnv - mb->env[b];
				float avail = ClampFloat(0.99f - otherEnv * 0.80f, 0.48f, ceilB);
				if (avail < ceilB) {
					ceilB = avail;
					threshB = avail * 0.78f;
				}
			}
			float g = CalcSoftBandGain(effP[b], threshB, ceilB);
			outL += bL[b] * g;
			outR += bR[b] * g;
		}
		L[i] = outL;
		R[i] = outR;
	}
}

// 1チャンネル LR4 3バンド独立ヘッドルーム保護 (モノラル / 多ch用)
static void MultibandProcessMonoChannel(MultibandProtector* mb, int ch, float* buf, int n) {
	if (ch < 0 || ch >= MAX_CH) return;
	static const float kBaseThresh[3] = { 0.66f, 0.72f, 0.60f };
	static const float kBaseCeil[3]   = { 0.88f, 0.90f, 0.84f };

	for (int i = 0; i < n; ++i) {
		double x = (double)buf[i];
		double low = ProcessBiquadD(&mb->lp1b[ch], ProcessBiquadD(&mb->lp1a[ch], x));
		double mh  = ProcessBiquadD(&mb->hp1b[ch], ProcessBiquadD(&mb->hp1a[ch], x));
		low = ProcessBiquadD(&mb->apLow[ch], low);
		double mid  = ProcessBiquadD(&mb->lp2b[ch], ProcessBiquadD(&mb->lp2a[ch], mh));
		double high = ProcessBiquadD(&mb->hp2b[ch], ProcessBiquadD(&mb->hp2a[ch], mh));

		float bands[3] = { (float)low, (float)mid, (float)high };
		float effP[3];
		for (int b = 0; b < 3; ++b) {
			float p = fabsf(bands[b]);
			float c = (p > mb->envCh[ch][b]) ? mb->atkCoeff[b] : mb->relCoeff[b];
			mb->envCh[ch][b] = FlushDenormalF(p + c * (mb->envCh[ch][b] - p));
			effP[b] = (p > mb->envCh[ch][b]) ? (mb->envCh[ch][b] * 0.45f + p * 0.55f) : mb->envCh[ch][b];
		}

		float sumEnv = mb->envCh[ch][0] + mb->envCh[ch][1] + mb->envCh[ch][2];
		float out = 0.0f;
		for (int b = 0; b < 3; ++b) {
			float ceilB   = kBaseCeil[b];
			float threshB = kBaseThresh[b];
			if (sumEnv > 0.96f && mb->envCh[ch][b] > 0.44f) {
				float otherEnv = sumEnv - mb->envCh[ch][b];
				float avail = ClampFloat(0.99f - otherEnv * 0.80f, 0.48f, ceilB);
				if (avail < ceilB) {
					ceilB = avail;
					threshB = avail * 0.78f;
				}
			}
			float g = CalcSoftBandGain(effP[b], threshB, ceilB);
			out += bands[b] * g;
		}
		buf[i] = out;
	}
}

// ============================================================
//  プロフェッショナル 100環境 立体物理音響エンジン (ステレオ完全独立)
//  - 16タップ L/R非対称 バイノーラル初期反射 (86: 逆再生スウェル対応)
//  - 4段素数オールパス 入力ディフューザー
//  - 8ch 正規化 Hadamard FDN (厳密RT60 + 帯域別ダンピング + 非同期LFO揺らぎ)
//  - 立体ピンポン山彦 / 平行壁フラッター実ディレイ / 定在波コム共鳴
//  - デュアルヘッド +1オクターブ・シマー / 分数遅延ドップラー / 4段フェイザー
//  - 空間固有共鳴 + 金属/ガラス/木質/コンクリ材質モデル + 屋外風ゆらぎ
//  - 水中(15)ドライ媒質透過 / 超デッド空間近接吸音 / タイムストップ(87)凍結ノッチ
// ============================================================

static void EnvEngineReset(EnvEngineState* es, float* poolBase, int rate) {
	if (!es) return;
	memset(es, 0, sizeof(*es));
	BindEnvPoolPointers(es, poolBase);
	if (poolBase) {
		memset(poolBase, 0, (SIZE_T)ENV_POOL_FLOATS * sizeof(float));
	}
	es->rate = (rate >= 8000) ? rate : 44100;
	es->presetIdx = -1;
	es->effAmount = -1;
	// 各フィルタを初期バイパスへ
	for (int c = 0; c < 2; ++c) {
		SetBiquadBypass(&es->wetLpf[c]);
		SetBiquadBypass(&es->wetHpf[c]);
		SetBiquadBypass(&es->resonanceEq[c]);
		SetBiquadBypass(&es->materialEq[c]);
		SetBiquadBypass(&es->metallicEq1[c]);
		SetBiquadBypass(&es->metallicEq2[c]);
		SetBiquadBypass(&es->glassEq[c]);
		SetBiquadBypass(&es->warmthEq[c]);
		SetBiquadBypass(&es->colorAirEq[c]);
		SetBiquadBypass(&es->exciterHp[c]);
		SetBiquadBypass(&es->directMediumLp[c]);
		SetBiquadBypass(&es->deadProximityEq[c]);
	}
	// FDN 各ライン LFO 初期位相を黄金比で均等分散
	for (int k = 0; k < ENV_FDN_LINES; ++k) {
		es->fdnModPhase[k] = (float)k * (2.0f * (float)M_PI / (float)ENV_FDN_LINES);
	}
	es->shimPhase[0] = 0.0f;
	es->shimPhase[1] = 0.25f;
}

// プリセット番号(0..100)とかかり具合(0..100, 既定50=UI100=1.0x標準空間)から物理定数を計算
static void EnvEngineConfigure(EnvEngineState* es, int presetIdx, int effectAmount, int rate) {
	if (!es) return;
	if (rate < 8000) rate = 44100;
	if (presetIdx < 0 || presetIdx >= ENV_PRESET_COUNT) presetIdx = 0;
	if (effectAmount < 0) effectAmount = 0;
	if (effectAmount > 100) effectAmount = 100;

	const int presetChanged = (es->presetIdx != presetIdx);
	es->rate = rate;
	es->presetIdx = presetIdx;
	es->effAmount = effectAmount;

	const EnvParams* ep = &ENV_PRESETS[presetIdx];
	if (presetIdx == 0 || effectAmount == 0) {
		es->dryGain = 1.0f;
		es->wetGain = 0.0f;
		es->directMediumMix = 0.0f;
		es->deadProximityMix = 0.0f;
		es->finalStereoWidth = 1.0f;
		return;
	}

	// UI 100 (effectAmount=50) が 1.00 = 物理仕様どおりの標準臨場感！
	// 0..50 は 0.0→1.0、50..100 は 1.0→2.0（上限付き強調）
	const float effNorm = (float)effectAmount / 50.0f;
	const float effWetScale = (effNorm <= 1.0f) ? effNorm : (1.0f + (effNorm - 1.0f) * 0.55f);
	const float effFxScale  = (effNorm <= 1.0f) ? effNorm : (1.0f + (effNorm - 1.0f) * 0.45f);
	const float rateScale   = (float)rate / 44100.0f;
	const float msToSamp    = (float)rate / 1000.0f;

	es->isReverseSpace = (presetIdx == 86) ? 1 : 0;
	es->isTimeStop     = (presetIdx == 87) ? 1 : 0;

	// --- 1. プリディレイ & 16タップ L/R非対称 立体初期反射 ---
	float preMs = ClampFloat(ep->preDelayMs, 0.0f, 160.0f);
	es->preDelaySamp = (int)(preMs * msToSamp + 0.5f);
	if (es->preDelaySamp < 1) es->preDelaySamp = 1;
	if (es->preDelaySamp > ENV_ER_LEN - 4096) es->preDelaySamp = ENV_ER_LEN - 4096;

	// 空間幾何学による初期反射タイムスケーリング
	float geomTime = ClampFloat(
		(0.65f + 0.35f * sqrtf(fmaxf(0.15f, ep->roomSize))) *
		(0.75f + 0.25f * ClampFloat(ep->wallDistance, 0.4f, 2.5f)) *
		(0.85f + 0.15f * ClampFloat(ep->ceilingHeight, 0.5f, 2.2f)),
		0.45f, 2.40f);
	// 左右壁の非対称オフセット（自然な頭内/頭外空間定位の決め手）
	static const float kAsymL[8] = { -0.06f,  0.09f, -0.04f,  0.11f, -0.08f,  0.05f, -0.10f,  0.07f };
	static const float kAsymR[8] = {  0.08f, -0.07f,  0.10f, -0.05f,  0.09f, -0.08f,  0.06f, -0.09f };
	// タップごとのステレオパン (0=左壁, 1=右壁, 中間=天井/床/奥壁)
	static const float kPan[8]   = { 0.22f, 0.78f, 0.35f, 0.65f, 0.18f, 0.82f, 0.45f, 0.55f };
	float erDecayPow = ClampFloat(1.0f / fmaxf(0.25f, ep->earlyReverbDecay), 0.35f, 2.5f);
	float erRoughLoss = 1.0f - ClampFloat(ep->surfaceRoughness, 0.0f, 1.0f) * 0.22f;
	float erNormSum = 0.0f;

	for (int r = 0; r < 8; ++r) {
		float baseMs = ep->earlyRef[r * 2];
		float baseG  = ep->earlyRef[r * 2 + 1];
		if (baseMs < 0.5f) baseMs = 2.0f + (float)r * 4.5f;
		float msL = preMs + baseMs * geomTime * (1.0f + kAsymL[r]);
		float msR = preMs + baseMs * geomTime * (1.0f + kAsymR[r]);
		int dL = (int)(msL * msToSamp + 0.5f);
		int dR = (int)(msR * msToSamp + 0.5f);
		if (dL < 2) dL = 2; if (dL > ENV_ER_LEN - 8) dL = ENV_ER_LEN - 8;
		if (dR < 2) dR = 2; if (dR > ENV_ER_LEN - 8) dR = ENV_ER_LEN - 8;
		es->erDelayL[r] = dL;
		es->erDelayR[r] = dR;

		float envG;
		if (es->isReverseSpace) {
			// 逆再生空間(86): 遅い反射ほど大きく盛り上がるリバーススウェル！
			float prog = (float)(r + 1) / 8.0f;
			envG = (0.18f + 0.82f * powf(prog, 1.85f)) * fmaxf(0.35f, baseG);
		}
		else {
			// 全8タップを有効化 (旧 r=7 で 0 になっていたバグを解消)
			float prog = (float)r / 8.5f;
			envG = baseG * powf(1.0f - prog * 0.55f, erDecayPow) * erRoughLoss;
		}
		// 交互位相反転でタップ間コム干渉を抑えつつ、L/Rは同相基準で自然な立体定位を保つ
		float sign = (r & 1) ? -1.0f : 1.0f;
		float p = kPan[r];
		es->erGainLL[r] = sign * envG * (1.0f - p * 0.65f);
		es->erGainRL[r] = sign * envG * (p * 0.38f);
		es->erGainRR[r] = sign * envG * (0.35f + p * 0.65f);
		es->erGainLR[r] = sign * envG * ((1.0f - p) * 0.38f);
		erNormSum += fabsf(envG);
	}
	// 初期反射の総エネルギーを自然正規化
	float erScale = (erNormSum > 1.2f) ? (1.2f / erNormSum) : 1.0f;
	for (int r = 0; r < 8; ++r) {
		es->erGainLL[r] *= erScale;
		es->erGainRL[r] *= erScale;
		es->erGainRR[r] *= erScale;
		es->erGainLR[r] *= erScale;
	}
	// 初期反射の材質吸音カットオフ (吸音率が高いほど柔らかい初期反射)
	float erFc = ClampFloat(12000.0f - ep->materialAbsorption * 8500.0f - ep->damping * 2500.0f, 1600.0f, 16000.0f);
	es->erToneCoeff = 1.0f - expf(-2.0f * (float)M_PI * erFc / (float)rate);

	// --- 2. 入力4段オールパス・ディフューザー (サンプルレート適応) ---
	static const int kDiffBaseL[ENV_DIFF_STAGES] = { 113, 199, 311, 467 };
	static const int kDiffBaseR[ENV_DIFF_STAGES] = { 127, 211, 331, 491 };
	for (int s = 0; s < ENV_DIFF_STAGES; ++s) {
		int dL = (int)((float)kDiffBaseL[s] * rateScale * ClampFloat(0.6f + 0.4f * ep->roomSize, 0.4f, 1.8f));
		int dR = (int)((float)kDiffBaseR[s] * rateScale * ClampFloat(0.6f + 0.4f * ep->roomSize, 0.4f, 1.8f));
		if (dL < 8) dL = 8; if (dL > ENV_DIFF_LEN - 4) dL = ENV_DIFF_LEN - 4;
		if (dR < 8) dR = 8; if (dR > ENV_DIFF_LEN - 4) dR = ENV_DIFF_LEN - 4;
		es->diffDelay[0][s] = dL;
		es->diffDelay[1][s] = dR;
	}
	// 山彦空間はエコーの輪郭をくっきり残すため入力ディフュージョンを抑える
	float diffVal = ClampFloat(ep->diffusion * 0.65f + ep->density * 0.25f, 0.0f, 1.0f);
	if (ep->type == TYPE_MOUNTAIN_ECHO || ep->type == TYPE_CANYON_ECHO)
		diffVal *= 0.25f;
	es->diffGain = ClampFloat(diffVal * 0.68f, 0.0f, 0.68f);

	// --- 3. 8ch 正規化 Hadamard FDN (正確な RT60 + 帯域別ダンピング) ---
	// 互いに素な8本の基本遅延長 (44.1kHz基準 18.8ms〜66.0ms)
	static const int kFdnBase[ENV_FDN_LINES] = { 829, 1021, 1237, 1499, 1789, 2113, 2477, 2909 };
	float sizeScale = ClampFloat(
		(0.42f + 0.58f * sqrtf(fmaxf(0.15f, ep->roomSize))) *
		(0.85f + 0.15f * ClampFloat(ep->ceilingHeight, 0.5f, 2.2f)),
		0.32f, 2.20f);

	// プリセットの lateReverbDecay から設計 RT60 を復元
	// (0.44→0.6s, 0.92→1.8s, 2.00→4.5s, 2.60→6.0s)
	float rt60Base = (ep->lateReverbDecay - 0.20f) * 2.5f;
	if (rt60Base < 0.08f) rt60Base = 0.08f + ep->feedback * 0.25f;
	// かかり具合で RT60 を壊しすぎない (UI100=1.00x完全設計値)
	float rtScale = 0.72f + 0.28f * ClampFloat(effNorm, 0.0f, 1.8f);
	float rt60Mid = ClampFloat(rt60Base * rtScale * ClampFloat(ep->midReverbTime, 0.55f, 1.6f), 0.08f, 9.5f);
	float rt60Low = ClampFloat(rt60Mid * ClampFloat(ep->bassReverbTime, 0.45f, 1.8f), 0.07f, 11.0f);

	// 高域 RT60: damping / airAbsorption / humidity / altitude / materialAbsorption が大きいほど高域が速く減衰！
	// (従来の逆符号バグを完全修正)
	float hfDampFactor = ClampFloat(
		ep->damping * 0.55f + ep->airAbsorption * 0.30f +
		ep->humidity * 0.15f + ep->altitude * 0.12f + ep->materialAbsorption * 0.20f,
		0.0f, 0.95f);
	float trebleRatio = ClampFloat(ep->trebleReverbTime * (1.0f - hfDampFactor * 0.62f), 0.16f, 1.15f);
	float rt60High = ClampFloat(rt60Mid * trebleRatio, 0.05f, rt60Mid);

	// FDN 内部のダンピングカットオフ周波数 (dampingが高いほど低い周波数から減衰)
	float fdnDampFc = ClampFloat(
		11000.0f - hfDampFactor * 8800.0f + (ep->reverbColor - 0.5f) * 2400.0f,
		900.0f, 14000.0f);
	if (ep->type == TYPE_UNDERWATER) fdnDampFc = 950.0f;
	float baseDampAlpha = 1.0f - expf(-2.0f * (float)M_PI * fdnDampFc / (float)rate);
	es->fdnLowAlpha = 1.0f - expf(-2.0f * (float)M_PI * 260.0f / (float)rate);

	// 空間複雑さに応じた微小ディレイ分散
	float compSpread = 1.0f + (ep->spaceComplexity - 0.5f) * 0.18f;
	for (int k = 0; k < ENV_FDN_LINES; ++k) {
		float lineSpread = 1.0f + ((float)k - 3.5f) * 0.025f * compSpread;
		int d = (int)((float)kFdnBase[k] * sizeScale * lineSpread * rateScale + 0.5f);
		if (d < 48) d = 48;
		if (d > ENV_FDN_LEN - 96) d = ENV_FDN_LEN - 96;
		es->fdnDelay[k] = d;

		// 厳密 RT60 ゲイン式: g = 10^(-3 * T_k / RT60)
		// 直交 Hadamard 行列なので全ライン |g| <= 0.988 で数学的に100%発散不可能！
		float tSec = (float)d / (float)rate;
		float gMid  = powf(10.0f, -3.0f * tSec / rt60Mid);
		float gLow  = powf(10.0f, -3.0f * tSec / rt60Low);
		float gHigh = powf(10.0f, -3.0f * tSec / rt60High);

		gMid  = ClampFloat(gMid,  0.05f, 0.985f);
		gLow  = ClampFloat(gLow,  0.05f, 0.988f);
		gHigh = ClampFloat(gHigh, 0.03f, gMid);

		es->fdnGainMid[k]    = gMid;
		es->fdnGainLowMul[k] = ClampFloat(gLow / fmaxf(0.05f, gMid), 0.55f, 1.35f);
		es->fdnGainHigh[k]   = gHigh;
		es->fdnDampAlpha[k]  = ClampFloat(baseDampAlpha * (0.90f + 0.028f * (float)k), 0.02f, 0.92f);

		// FDN 内部の自然な空気揺らぎLFO (定常波・金属リング防止 + modDepth反映)
		float baseRateHz = (ep->modSpeed > 0.05f) ? ep->modSpeed : (0.31f + 0.07f * (float)k);
		float lineRateHz = baseRateHz * (0.78f + 0.075f * (float)k);
		es->fdnModInc[k] = 2.0f * (float)M_PI * lineRateHz / (float)rate;
	}
	// 標準の空気揺らぎ 0.35ms + プリセットの modDepth (最大 3.5ms)
	float modMs = 0.28f + ep->modDepth * effFxScale * 3.2f;
	if (es->isTimeStop) modMs = 0.02f;
	es->fdnModDepthSamp = ClampFloat(modMs * msToSamp, 0.5f, 44.0f);

	// --- 4. 立体ピンポン山彦 (山8 / 峡谷12 / 渓谷47 等) ---
	es->hasYamabiko = (ep->yamabikoDelays[0] > 1.0f && ep->yamabikoGains[0] > 0.01f) ? 1 : 0;
	if (es->hasYamabiko) {
		float decayPow = ClampFloat(1.0f / fmaxf(0.25f, ep->yamabikoDecay), 0.45f, 2.2f);
		for (int i = 0; i < 4; ++i) {
			float ms = ep->yamabikoDelays[i];
			if (ms <= 1.0f) {
				es->yamaDelayL[i] = 0;
				es->yamaDelayR[i] = 0;
				es->yamaGain[i]   = 0.0f;
				continue;
			}
			// 奇数/偶数タップで左右壁の到達時間差を与え立体的な山彦ピンポンを生成
			float spreadMs = (i & 1) ? 14.0f : -14.0f;
			int dL = (int)((ms + spreadMs * ep->yamabikoPan) * msToSamp + 0.5f);
			int dR = (int)((ms - spreadMs * ep->yamabikoPan) * msToSamp + 0.5f);
			if (dL < 16) dL = 16; if (dL > ENV_YAMA_LEN - 16) dL = ENV_YAMA_LEN - 16;
			if (dR < 16) dR = 16; if (dR > ENV_YAMA_LEN - 16) dR = ENV_YAMA_LEN - 16;
			es->yamaDelayL[i] = dL;
			es->yamaDelayR[i] = dR;
			es->yamaGain[i]   = ClampFloat(ep->yamabikoGains[i] * powf(0.92f, (float)i * decayPow), 0.0f, 0.95f);
		}
		es->yamaPan = ClampFloat(ep->yamabikoPan, 0.0f, 1.0f);
		// echoFeedbackTone (-1=暗い..+1=明るい) & echoClarity で遠景の空気吸収を反映
		float yFc = ClampFloat(3200.0f + ep->echoFeedbackTone * 2200.0f + ep->echoClarity * 3000.0f, 800.0f, 11000.0f);
		es->yamaToneAlpha = 1.0f - expf(-2.0f * (float)M_PI * yFc / (float)rate);
		es->yamaFb = ClampFloat(0.22f + ep->feedback * 0.30f, 0.10f, 0.55f);
	}

	// --- 5. 本物の平行壁フラッターエコー (実ディレイライン反復反射) ---
	if (ep->flutterEcho > 0.02f) {
		// 壁間距離に応じた往復時間 (7ms〜34ms、L/Rで7%ズラして立体感)
		float flutMs = ClampFloat(7.5f + ep->wallDistance * 9.0f + ep->roomSize * 3.5f, 6.0f, 36.0f);
		int dL = (int)(flutMs * msToSamp + 0.5f);
		int dR = (int)(flutMs * 1.073f * msToSamp + 0.5f);
		if (dL < 32) dL = 32; if (dL > ENV_FLUT_LEN - 8) dL = ENV_FLUT_LEN - 8;
		if (dR < 32) dR = 32; if (dR > ENV_FLUT_LEN - 8) dR = ENV_FLUT_LEN - 8;
		es->flutDelay[0] = dL;
		es->flutDelay[1] = dR;
		es->flutFb  = ClampFloat(0.30f + ep->flutterEcho * 0.44f, 0.20f, 0.76f);
		es->flutMix = ClampFloat(ep->flutterEcho * 0.48f * effFxScale, 0.0f, 0.55f);
	}
	else {
		es->flutMix = 0.0f;
	}

	// --- 6. 本物の定在波コム共鳴 (resonanceFreq に同調した箱鳴り/筒鳴り) ---
	if (ep->combFiltering > 0.02f) {
		float cFreq = ClampFloat((ep->resonanceFreq > 60.0f) ? ep->resonanceFreq : 280.0f, 65.0f, 2800.0f);
		int dL = (int)((float)rate / cFreq + 0.5f);
		int dR = (int)((float)rate / (cFreq * 1.031f) + 0.5f);
		if (dL < 12) dL = 12; if (dL > ENV_COMB_LEN - 8) dL = ENV_COMB_LEN - 8;
		if (dR < 12) dR = 12; if (dR > ENV_COMB_LEN - 8) dR = ENV_COMB_LEN - 8;
		es->combDelay[0] = dL;
		es->combDelay[1] = dR;
		es->combFb  = ClampFloat(0.28f + ep->combFiltering * 0.46f, 0.15f, 0.76f);
		es->combMix = ClampFloat(ep->combFiltering * 0.42f * effFxScale, 0.0f, 0.48f);
	}
	else {
		es->combMix = 0.0f;
	}

	// --- 7. 本物の分数遅延ドップラー (ピッチシフトを伴う空間移動) ---
	if (ep->doppler > 0.02f) {
		float doppHz = ClampFloat(0.32f + ep->doppler * 0.95f + ep->modSpeed * 0.35f, 0.25f, 2.2f);
		es->doppBaseSamp  = ClampFloat(10.0f * msToSamp, 32.0f, (float)(ENV_DOPP_LEN / 2));
		es->doppDepthSamp = ClampFloat(ep->doppler * 4.2f * effFxScale * msToSamp, 2.0f, es->doppBaseSamp * 0.78f);
		es->doppPhaseInc  = 2.0f * (float)M_PI * doppHz / (float)rate;
		es->doppMix       = ClampFloat(ep->doppler * 0.65f * effFxScale, 0.0f, 0.75f);
	}
	else {
		es->doppMix = 0.0f;
	}

	// --- 8. 本物のデュアルヘッド +1オクターブ・シマー ---
	if (ep->shimmer > 0.02f) {
		es->shimGrainLen = ClampFloat(38.0f * msToSamp, 256.0f, (float)(ENV_SHIM_LEN / 2 - 64));
		es->shimMix      = ClampFloat(ep->shimmer * 0.55f * effFxScale, 0.0f, 0.65f);
		es->shimFb       = ClampFloat(ep->shimmer * 0.32f, 0.0f, 0.38f);
	}
	else {
		es->shimMix = 0.0f;
		es->shimFb  = 0.0f;
	}

	// --- 9. 本物の 4段ステレオ・オールパス・フェイザー ---
	if (ep->phasing > 0.02f || es->isTimeStop) {
		float pHz = es->isTimeStop ? 0.04f : ClampFloat(0.28f + ep->phasing * 0.65f + ep->modSpeed * 0.25f, 0.18f, 1.8f);
		es->phaserInc   = 2.0f * (float)M_PI * pHz / (float)rate;
		es->phaserDepth = ClampFloat((es->isTimeStop ? 0.65f : ep->phasing) * 0.62f * effFxScale, 0.0f, 0.72f);
	}
	else {
		es->phaserDepth = 0.0f;
	}

	// --- 10. 屋外の風ゆらぎ ---
	es->windAmount = ClampFloat(ep->windEffect * 0.22f * effFxScale, 0.0f, 0.28f);

	// --- 11. 空間共鳴・材質・音色整形フィルタ群 (すべて実効ゲイン＆正符号!) ---
	float lpfHz = ClampFloat(ep->lpfFreq, 450.0f, (float)rate * 0.46f);
	float hpfHz = ClampFloat(ep->hpfFreq, 20.0f, 1200.0f);

	// 空間固有共鳴: resonanceQ と空間タイプから明確な実効ゲイン (+3.0〜+9.0dB) を算出！
	// (従来の gainVal=100 (0dBバイパス) バグを完全修正)
	double resDb = 0.0;
	if (ep->resonanceFreq >= 50.0f && ep->resonanceQ > 0.75f) {
		resDb = ClampDouble(((double)ep->resonanceQ - 0.70) * 2.8, 1.5, 8.5);
		if (ep->type == TYPE_METAL_SPACE || ep->type == TYPE_CAVE ||
			ep->type == TYPE_CORRIDOR || es->isTimeStop) {
			resDb = ClampDouble(resDb + 1.8, 2.5, 9.5);
		}
		resDb *= ClampDouble((double)effFxScale, 0.0, 1.25);
	}

	// 材質特性 (木質の温かい胴鳴り vs コンクリートの硬質反射)
	double matFreq = 500.0, matDb = 0.0, matQ = 1.1;
	if (ep->woodiness >= ep->concrete && ep->woodiness > 0.08f) {
		matFreq = 320.0;
		matDb   = (double)ep->woodiness * 4.5 * ClampDouble((double)effFxScale, 0.0, 1.2);
		matQ    = 1.0;
	}
	else if (ep->concrete > 0.08f) {
		matFreq = 1450.0;
		matDb   = (double)ep->concrete * 4.2 * ClampDouble((double)effFxScale, 0.0, 1.2);
		matQ    = 1.4;
	}

	// 温かみ + 重さ ローシェルフ (-4dB..+6.5dB)
	double warmDb = (((double)ep->warmth - 0.5) * 6.5 + ((double)ep->weight - 0.5) * 5.0)
	                * ClampDouble((double)effFxScale, 0.0, 1.2);
	// 明暗 + 空気吸収・湿度・高度 ハイシェルフ (吸収が高いほど高域が自然に遠ざかる！)
	double airCut = ((double)ep->airAbsorption * 5.5 + (double)ep->humidity * 2.5 + (double)ep->altitude * 2.5);
	double brightAdd = (((double)ep->reverbColor - 0.5) * 7.5 + ((double)ep->brightness - 0.5) * 5.5);
	double colorDb = ClampDouble((brightAdd - airCut) * ClampDouble((double)effFxScale, 0.0, 1.2), -11.0, 8.5);

	// 金属感 & ガラス感
	double metalDb = (ep->metallic > 0.05f) ? ((double)ep->metallic * 6.5 * ClampDouble((double)effFxScale, 0.0, 1.2)) : 0.0;
	double glassDb = (ep->glassiness > 0.05f) ? ((double)ep->glassiness * 6.5 * ClampDouble((double)effFxScale, 0.0, 1.2)) : 0.0;

	for (int c = 0; c < 2; ++c) {
		CalcBiquadFilter(&es->wetLpf[c], 0, lpfHz, 0.7071, rate);
		CalcBiquadFilter(&es->wetHpf[c], 1, hpfHz, 0.7071, rate);
		// L/Rで共鳴周波数をわずかに離調(±2.2%)して自然な立体空間共鳴にする
		double rFreq = (double)ep->resonanceFreq * ((c == 0) ? 0.988 : 1.012);
		CalcPeakingEQDb(&es->resonanceEq[c], rFreq, ClampDouble((double)ep->resonanceQ, 0.8, 6.0), resDb, rate);
		CalcPeakingEQDb(&es->materialEq[c], matFreq, matQ, matDb, rate);
		CalcPeakingEQDb(&es->metallicEq1[c], (c == 0) ? 2350.0 : 2490.0, 3.6, metalDb, rate);
		CalcPeakingEQDb(&es->metallicEq2[c], (c == 0) ? 4750.0 : 5020.0, 4.5, metalDb * 0.82, rate);
		CalcPeakingEQDb(&es->glassEq[c], (c == 0) ? 6400.0 : 6850.0, 2.8, glassDb, rate);
		CalcShelvingEQDb(&es->warmthEq[c], 0, 260.0, ClampDouble(warmDb, -6.0, 7.0), rate);
		CalcShelvingEQDb(&es->colorAirEq[c], 1, 3400.0, colorDb, rate);
		CalcBiquadFilter(&es->exciterHp[c], 1, 5500.0, 0.7071, rate);

		// 水中(15): ドライ信号自体にも水中透過LPFを通すことで本当に水中に潜った臨場感！
		if (ep->type == TYPE_UNDERWATER) {
			CalcBiquadFilter(&es->directMediumLp[c], 0, 1150.0, 0.82, rate);
			es->directMediumMix = ClampFloat(0.62f * effWetScale, 0.0f, 0.88f);
		}
		else {
			SetBiquadBypass(&es->directMediumLp[c]);
			es->directMediumMix = 0.0f;
		}

		// 超デッド密閉空間 (スタジオ5/クローゼット18/ブース39/和室58/防音室73):
		// 吸音壁に囲まれた近接感・デッド感を繊細に表現
		if (ep->enclosure >= 0.85f && ep->lateReverbDecay <= 0.36f && ep->type != TYPE_UNDERWATER) {
			double deadCut = -1.8 - (double)ep->materialAbsorption * 2.4;
			CalcShelvingEQDb(&es->deadProximityEq[c], 1, 5200.0, deadCut * ClampDouble((double)effFxScale, 0.0, 1.2), rate);
			es->deadProximityMix = ClampFloat(0.55f * effWetScale, 0.0f, 0.75f);
		}
		else {
			SetBiquadBypass(&es->deadProximityEq[c]);
			es->deadProximityMix = 0.0f;
		}

		// プリセット切替時はフィルタ状態のみクリアして過渡クリックを防ぐ
		if (presetChanged) {
			ResetBiquadD(&es->wetLpf[c]);
			ResetBiquadD(&es->wetHpf[c]);
			ResetBiquadD(&es->resonanceEq[c]);
			ResetBiquadD(&es->materialEq[c]);
			ResetBiquadD(&es->metallicEq1[c]);
			ResetBiquadD(&es->metallicEq2[c]);
			ResetBiquadD(&es->glassEq[c]);
			ResetBiquadD(&es->warmthEq[c]);
			ResetBiquadD(&es->colorAirEq[c]);
			ResetBiquadD(&es->exciterHp[c]);
			ResetBiquadD(&es->directMediumLp[c]);
			ResetBiquadD(&es->deadProximityEq[c]);
		}
	}

	// softness: トランジェント丸め用の穏やかな1次LPF係数
	float softFc = ClampFloat(14000.0f - ep->softness * 9500.0f, 2500.0f, 16000.0f);
	es->softAlpha = (ep->softness > 0.02f)
		? (1.0f - expf(-2.0f * (float)M_PI * softFc / (float)rate)) : 1.0f;
	es->exciterAmt = ClampFloat(ep->exciterAmount * 0.45f * effFxScale, 0.0f, 0.55f);
	es->distAmt    = ClampFloat(ep->distortion * 0.75f * effFxScale, 0.0f, 0.85f);

	// --- 12. 初期反射 / 後期残響 / ドライ・ウェットの最適バランス ---
	// earlyLateBalance: 0=初期反射主体, 1=後期残響主体
	float el = ClampFloat(ep->earlyLateBalance, 0.05f, 0.95f);
	es->earlyMix = 0.38f + (1.0f - el) * 0.85f;
	es->lateMix  = 0.25f + el * 0.95f;
	if (es->hasYamabiko) {
		es->lateMix *= 0.55f; // 山彦の輪郭を最優先
	}

	// wetMix をそのまま活かし、ドライの音量芯を落とさずリッチな空間感を実現
	float effWet = ClampFloat(ep->wetMix * effWetScale, 0.0f, 0.94f);
	// デッド小空間でも初期反射がきちんと聴こえる下限保証
	if (effWet < 0.14f && effNorm > 0.2f) effWet = 0.14f * ClampFloat(effNorm, 0.0f, 1.0f);
	es->dryGain = ClampFloat(1.0f - 0.22f * effWet, 0.72f, 1.0f);
	es->wetGain = effWet * 1.18f;

	// ステレオ空間展開 (ウェットと最終段の二重過剰拡大による逆相感を防ぎ、自然なIACC=+0.05..+0.40へ)
	float wEff = ClampFloat(effNorm, 0.0f, 1.25f);
	float wWet = 0.85f + (ep->stereoWidth - 1.0f) * 0.45f * wEff + ep->openness * 0.15f - ep->enclosure * 0.12f;
	float wFin = 1.00f + (ep->stereoWidth - 1.0f) * 0.28f * wEff + (ep->openness - ep->enclosure) * 0.10f;
	es->wetStereoWidth   = ClampFloat(wWet, 0.45f, 1.48f);
	es->finalStereoWidth = ClampFloat(wFin, 0.45f, 1.42f);
	if (es->isTimeStop) es->finalStereoWidth = 0.40f;
}

// ステレオ 1ブロック環境音響処理 (L/R 固定バッファ・再生中動的確保ゼロ)
static void EnvEngineProcessStereo(EnvEngineState* es, float* L, float* R, int n, float spatialWidth) {
	if (!es || !es->erBuf[0] || !es->fdnBuf[0] || es->wetGain <= 1e-5f || n <= 0)
		return;

	const EnvParams* ep = &ENV_PRESETS[es->presetIdx];
	const float kHadamardNorm = 0.35355339059f; // 1 / sqrt(8)
	const float windInc1 = 2.0f * (float)M_PI * 0.23f / (float)es->rate;
	const float windInc2 = 2.0f * (float)M_PI * 0.61f / (float)es->rate;
	const float invGrain = (es->shimGrainLen > 1.0f) ? (1.0f / es->shimGrainLen) : 0.0f;

	for (int i = 0; i < n; ++i) {
		float inL = L[i];
		float inR = R[i];

		// --- 1. ダイレクト媒質処理 (水中15 / 超デッド密閉空間) ---
		float dryL = inL;
		float dryR = inR;
		if (es->directMediumMix > 0.001f) {
			float mL = (float)ProcessBiquadD(&es->directMediumLp[0], (double)inL);
			float mR = (float)ProcessBiquadD(&es->directMediumLp[1], (double)inR);
			dryL = inL * (1.0f - es->directMediumMix) + mL * es->directMediumMix;
			dryR = inR * (1.0f - es->directMediumMix) + mR * es->directMediumMix;
		}
		if (es->deadProximityMix > 0.001f) {
			float dL = (float)ProcessBiquadD(&es->deadProximityEq[0], (double)dryL);
			float dR = (float)ProcessBiquadD(&es->deadProximityEq[1], (double)dryR);
			dryL = dryL * (1.0f - es->deadProximityMix) + dL * es->deadProximityMix;
			dryR = dryR * (1.0f - es->deadProximityMix) + dR * es->deadProximityMix;
		}

		// --- 2. プリディレイ＋16タップ L/R非対称 立体初期反射 ---
		int epPos = es->erPos;
		es->erBuf[0][epPos] = inL;
		es->erBuf[1][epPos] = inR;

		float erL = 0.0f, erR = 0.0f;
		for (int r = 0; r < 8; ++r) {
			float sL = es->erBuf[0][(epPos - es->erDelayL[r]) & ENV_ER_MASK];
			float sR = es->erBuf[1][(epPos - es->erDelayR[r]) & ENV_ER_MASK];
			erL += sL * es->erGainLL[r] + sR * es->erGainRL[r];
			erR += sR * es->erGainRR[r] + sL * es->erGainLR[r];
		}
		// 材質吸音 1次LPF
		es->erLpState[0] = FlushDenormalF(es->erLpState[0] + es->erToneCoeff * (erL - es->erLpState[0]));
		es->erLpState[1] = FlushDenormalF(es->erLpState[1] + es->erToneCoeff * (erR - es->erLpState[1]));
		erL = es->erLpState[0];
		erR = es->erLpState[1];

		// プリディレイ出力 (FDNへの入力)
		int preIdx = (epPos - es->preDelaySamp) & ENV_ER_MASK;
		float preL = es->erBuf[0][preIdx];
		float preR = es->erBuf[1][preIdx];
		es->erPos = (epPos + 1) & ENV_ER_MASK;

		// --- 3. 入力4段オールパス・ディフューザー ---
		float dInL = preL;
		float dInR = preR;
		if (es->diffGain > 0.01f) {
			const float dg = es->diffGain;
			for (int s = 0; s < ENV_DIFF_STAGES; ++s) {
				int wpL = es->diffPos[0][s];
				int rpL = (wpL - es->diffDelay[0][s]) & ENV_DIFF_MASK;
				float dlL = es->diffBuf[0][s][rpL];
				float vL = dInL + dlL * dg;
				es->diffBuf[0][s][wpL] = FlushDenormalF(vL);
				dInL = dlL - vL * dg;
				es->diffPos[0][s] = (wpL + 1) & ENV_DIFF_MASK;

				int wpR = es->diffPos[1][s];
				int rpR = (wpR - es->diffDelay[1][s]) & ENV_DIFF_MASK;
				float dlR = es->diffBuf[1][s][rpR];
				float vR = dInR + dlR * dg;
				es->diffBuf[1][s][wpR] = FlushDenormalF(vR);
				dInR = dlR - vR * dg;
				es->diffPos[1][s] = (wpR + 1) & ENV_DIFF_MASK;
			}
		}

		// --- 4. 8ch 正規化 Hadamard FDN (非同期LFO分数遅延 + 2バンド周波数依存ダンピング) ---
		float fdnRead[ENV_FDN_LINES];
		float fdnFilt[ENV_FDN_LINES];
		for (int k = 0; k < ENV_FDN_LINES; ++k) {
			es->fdnModPhase[k] += es->fdnModInc[k];
			if (es->fdnModPhase[k] >= 2.0f * (float)M_PI) es->fdnModPhase[k] -= 2.0f * (float)M_PI;
			float modOff = sinf(es->fdnModPhase[k]) * es->fdnModDepthSamp;
			float delayF = (float)es->fdnDelay[k] + modOff;
			if (delayF < 4.0f) delayF = 4.0f;
			int dInt = (int)delayF;
			float frac = delayF - (float)dInt;
			int r0 = (es->fdnPos[k] - dInt) & ENV_FDN_MASK;
			int r1 = (r0 - 1) & ENV_FDN_MASK;
			float raw = es->fdnBuf[k][r0] * (1.0f - frac) + es->fdnBuf[k][r1] * frac;
			fdnRead[k] = raw;

			// 周波数依存ダンピング: 高域LPF + 低域RT60シェルフ + DCブロッカー
			float dampLp = es->fdnDampState[k] + es->fdnDampAlpha[k] * (raw - es->fdnDampState[k]);
			es->fdnDampState[k] = FlushDenormalF(dampLp);
			// 低・中域成分は fdnGainMid、高域残余成分は fdnGainHigh で減衰
			float shaped = dampLp * es->fdnGainMid[k] + (raw - dampLp) * es->fdnGainHigh[k];
			// 低域残響時間倍率 (bassReverbTime) の適用
			float lowPart = es->fdnLowState[k] + es->fdnLowAlpha * (shaped - es->fdnLowState[k]);
			es->fdnLowState[k] = FlushDenormalF(lowPart);
			shaped = (shaped - lowPart) + lowPart * es->fdnGainLowMul[k];

			// 1次DCブロッカー (極低周波ドリフト防止, 15Hz)
			float dc = shaped - es->fdnDcPrevIn[k] + 0.9975f * es->fdnDcPrevOut[k];
			es->fdnDcPrevIn[k] = shaped;
			es->fdnDcPrevOut[k] = FlushDenormalF(dc);
			fdnFilt[k] = dc;
		}

		// 8x8 正規化 Hadamard 高速バタフライ直交混合 (完全エネルギー保存)
		float a0 = fdnFilt[0] + fdnFilt[1], a1 = fdnFilt[0] - fdnFilt[1];
		float a2 = fdnFilt[2] + fdnFilt[3], a3 = fdnFilt[2] - fdnFilt[3];
		float a4 = fdnFilt[4] + fdnFilt[5], a5 = fdnFilt[4] - fdnFilt[5];
		float a6 = fdnFilt[6] + fdnFilt[7], a7 = fdnFilt[6] - fdnFilt[7];
		float b0 = a0 + a2, b1 = a1 + a3, b2 = a0 - a2, b3 = a1 - a3;
		float b4 = a4 + a6, b5 = a5 + a7, b6 = a4 - a6, b7 = a5 - a7;
		float fdnMix[ENV_FDN_LINES];
		fdnMix[0] = (b0 + b4) * kHadamardNorm;
		fdnMix[1] = (b1 + b5) * kHadamardNorm;
		fdnMix[2] = (b2 + b6) * kHadamardNorm;
		fdnMix[3] = (b3 + b7) * kHadamardNorm;
		fdnMix[4] = (b0 - b4) * kHadamardNorm;
		fdnMix[5] = (b1 - b5) * kHadamardNorm;
		fdnMix[6] = (b2 - b6) * kHadamardNorm;
		fdnMix[7] = (b3 - b7) * kHadamardNorm;

		// シマー出力のフィードバック還元量 (前サンプルの+1oct成分)
		float shimFbL = es->shimLpState[0] * es->shimFb;
		float shimFbR = es->shimLpState[1] * es->shimFb;

		// 入力を8ラインへ自然なステレオ配置で注入 (逆位相感ゼロ・自然な包まれ感)
		float injL = (dInL + shimFbL) * 0.40f;
		float injR = (dInR + shimFbR) * 0.40f;
		float injM = (injL + injR) * 0.55f;
		const float injVec[ENV_FDN_LINES] = { injL, injR, injL, injR, injM, injM, injL, injR };
		for (int k = 0; k < ENV_FDN_LINES; ++k) {
			float wv = fdnMix[k] + injVec[k];
			// フィードバックループ内ソフト安全ガード (|wv|>2.5時のみ穏やか制限)
			if (wv > 2.5f) wv = 2.5f + tanhf(wv - 2.5f) * 0.5f;
			else if (wv < -2.5f) wv = -2.5f + tanhf(wv + 2.5f) * 0.5f;
			es->fdnBuf[k][es->fdnPos[k]] = FlushDenormalF(wv);
			es->fdnPos[k] = (es->fdnPos[k] + 1) & ENV_FDN_MASK;
		}

		// L/R 独立素数ライン合成 (自然な低相関 IACC≈0.0〜+0.35 で極上の立体包まれ感)
		float fdnL = (fdnRead[0] + fdnRead[2] - fdnRead[4] + fdnRead[6]
		            + 0.40f * (fdnRead[1] - fdnRead[5])) * 0.42f;
		float fdnR = (fdnRead[1] + fdnRead[3] - fdnRead[5] + fdnRead[7]
		            + 0.40f * (fdnRead[0] - fdnRead[4])) * 0.42f;

		// --- 5. デュアルヘッド +1オクターブ・シマー (本物のグラニュラーピッチシフト) ---
		float shimOutL = 0.0f, shimOutR = 0.0f;
		if (es->shimMix > 0.005f && invGrain > 0.0f) {
			int sp = es->shimPos;
			es->shimBuf[0][sp] = fdnL * 0.7f + preL * 0.3f;
			es->shimBuf[1][sp] = fdnR * 0.7f + preR * 0.3f;
			for (int c = 0; c < 2; ++c) {
				// 読み出し速度 2.0x = 正確な +1オクターブ上昇 (位相進み = +1/grainLen)
				float p1 = es->shimPhase[c];
				float p2 = p1 + 0.5f;
				if (p2 >= 1.0f) p2 -= 1.0f;
				// 遅延量は (1 - p) * grainLen → p が増えると遅延が減る = 2倍速読み出し！
				float d1 = (1.0f - p1) * es->shimGrainLen + 8.0f;
				float d2 = (1.0f - p2) * es->shimGrainLen + 8.0f;
				int i1 = (int)d1; float f1 = d1 - (float)i1;
				int i2 = (int)d2; float f2 = d2 - (float)i2;
				float s1 = es->shimBuf[c][(sp - i1) & ENV_SHIM_MASK] * (1.0f - f1)
				         + es->shimBuf[c][(sp - i1 - 1) & ENV_SHIM_MASK] * f1;
				float s2 = es->shimBuf[c][(sp - i2) & ENV_SHIM_MASK] * (1.0f - f2)
				         + es->shimBuf[c][(sp - i2 - 1) & ENV_SHIM_MASK] * f2;
				// sin^2(pi*p1) + sin^2(pi*p2) == 1.0 (振幅完全一定・クリックゼロ)
				float w1 = sinf((float)M_PI * p1); w1 *= w1;
				float w2 = 1.0f - w1;
				float shifted = s1 * w1 + s2 * w2;
				// シマー帯域を滑らかに整える
				es->shimLpState[c] = FlushDenormalF(es->shimLpState[c] + 0.35f * (shifted - es->shimLpState[c]));
				if (c == 0) shimOutL = es->shimLpState[0];
				else        shimOutR = es->shimLpState[1];

				es->shimPhase[c] += invGrain * ((c == 0) ? 1.0f : 0.93f);
				if (es->shimPhase[c] >= 1.0f) es->shimPhase[c] -= 1.0f;
			}
			es->shimPos = (sp + 1) & ENV_SHIM_MASK;
		}

		// --- 6. 立体ピンポン山彦 (山8 / 峡谷12 / 渓谷47) ---
		float yamaL = 0.0f, yamaR = 0.0f;
		if (es->hasYamabiko) {
			int yp = es->yamaPos;
			for (int t = 0; t < 4; ++t) {
				if (es->yamaGain[t] <= 0.001f) continue;
				float tL = es->yamaBuf[0][(yp - es->yamaDelayL[t]) & ENV_YAMA_MASK];
				float tR = es->yamaBuf[1][(yp - es->yamaDelayR[t]) & ENV_YAMA_MASK];
				float g = es->yamaGain[t];
				// 奇数タップと偶数タップで左右の山肌から交互にピンポン反射！
				if (t & 1) {
					yamaL += (tL * (1.0f - 0.55f * es->yamaPan) + tR * (0.55f * es->yamaPan)) * g;
					yamaR += (tR * (1.0f + 0.35f * es->yamaPan)) * g;
				}
				else {
					yamaL += (tL * (1.0f + 0.35f * es->yamaPan)) * g;
					yamaR += (tR * (1.0f - 0.55f * es->yamaPan) + tL * (0.55f * es->yamaPan)) * g;
				}
			}
			// 距離による空気吸収トーンフィルタ
			es->yamaToneState[0] = FlushDenormalF(es->yamaToneState[0] + es->yamaToneAlpha * (yamaL - es->yamaToneState[0]));
			es->yamaToneState[1] = FlushDenormalF(es->yamaToneState[1] + es->yamaToneAlpha * (yamaR - es->yamaToneState[1]));
			yamaL = es->yamaToneState[0];
			yamaR = es->yamaToneState[1];
			// 穏やかなクロスフィードバックで遠景の余韻を持続
			es->yamaBuf[0][yp] = FlushDenormalF(inL + yamaR * es->yamaFb * 0.45f);
			es->yamaBuf[1][yp] = FlushDenormalF(inR + yamaL * es->yamaFb * 0.45f);
			es->yamaPos = (yp + 1) & ENV_YAMA_MASK;
		}

		// --- 7. 初期反射 + 後期FDN + シマー + 山彦 の合成 ---
		float wetL = erL * es->earlyMix + fdnL * es->lateMix + shimOutL * es->shimMix + yamaL * 0.85f;
		float wetR = erR * es->earlyMix + fdnR * es->lateMix + shimOutR * es->shimMix + yamaR * 0.85f;

		// --- 8. 本物の平行壁フラッターエコー (実ディレイ反復反射) ---
		if (es->flutMix > 0.005f) {
			int fp = es->flutPos;
			for (int c = 0; c < 2; ++c) {
				float x = (c == 0) ? (preL * 0.55f + wetL * 0.45f) : (preR * 0.55f + wetR * 0.45f);
				float d = es->flutBuf[c][(fp - es->flutDelay[c]) & ENV_FLUT_MASK];
				es->flutLpState[c] = FlushDenormalF(es->flutLpState[c] + 0.45f * (d - es->flutLpState[c]));
				es->flutBuf[c][fp] = FlushDenormalF(x + es->flutLpState[c] * es->flutFb);
				if (c == 0) wetL += es->flutLpState[0] * es->flutMix;
				else        wetR += es->flutLpState[1] * es->flutMix;
			}
			es->flutPos = (fp + 1) & ENV_FLUT_MASK;
		}

		// --- 9. 本物の定在波コム共鳴 (筒鳴り/箱鳴り) ---
		if (es->combMix > 0.005f) {
			int cp = es->combPos;
			for (int c = 0; c < 2; ++c) {
				float x = (c == 0) ? wetL : wetR;
				float d = es->combBuf[c][(cp - es->combDelay[c]) & ENV_COMB_MASK];
				es->combLpState[c] = FlushDenormalF(es->combLpState[c] + 0.55f * (d - es->combLpState[c]));
				es->combBuf[c][cp] = FlushDenormalF(x + es->combLpState[c] * es->combFb);
				float out = x * (1.0f - es->combMix * 0.35f) + es->combLpState[c] * es->combMix;
				if (c == 0) wetL = out; else wetR = out;
			}
			es->combPos = (cp + 1) & ENV_COMB_MASK;
		}

		// --- 10. 本物の分数遅延ドップラー (L/R 90度位相差ピッチ変調) ---
		if (es->doppMix > 0.005f) {
			int dp = es->doppPos;
			es->doppBuf[0][dp] = wetL;
			es->doppBuf[1][dp] = wetR;
			es->doppPhase += es->doppPhaseInc;
			if (es->doppPhase >= 2.0f * (float)M_PI) es->doppPhase -= 2.0f * (float)M_PI;
			float dL = es->doppBaseSamp + sinf(es->doppPhase) * es->doppDepthSamp;
			float dR = es->doppBaseSamp + cosf(es->doppPhase) * es->doppDepthSamp;
			int iL = (int)dL; float fL = dL - (float)iL;
			int iR = (int)dR; float fR = dR - (float)iR;
			float vL = es->doppBuf[0][(dp - iL) & ENV_DOPP_MASK] * (1.0f - fL)
			         + es->doppBuf[0][(dp - iL - 1) & ENV_DOPP_MASK] * fL;
			float vR = es->doppBuf[1][(dp - iR) & ENV_DOPP_MASK] * (1.0f - fR)
			         + es->doppBuf[1][(dp - iR - 1) & ENV_DOPP_MASK] * fR;
			wetL = wetL * (1.0f - es->doppMix) + vL * es->doppMix;
			wetR = wetR * (1.0f - es->doppMix) + vR * es->doppMix;
			es->doppPos = (dp + 1) & ENV_DOPP_MASK;
		}

		// --- 11. 空間共鳴・材質・音色整形フィルタ (L/R 独立倍精度) ---
		double wL = (double)wetL;
		double wR = (double)wetR;
		wL = ProcessBiquadD(&es->wetLpf[0], wL);
		wR = ProcessBiquadD(&es->wetLpf[1], wR);
		wL = ProcessBiquadD(&es->wetHpf[0], wL);
		wR = ProcessBiquadD(&es->wetHpf[1], wR);
		wL = ProcessBiquadD(&es->resonanceEq[0], wL);
		wR = ProcessBiquadD(&es->resonanceEq[1], wR);
		wL = ProcessBiquadD(&es->materialEq[0], wL);
		wR = ProcessBiquadD(&es->materialEq[1], wR);
		wL = ProcessBiquadD(&es->metallicEq1[0], wL);
		wR = ProcessBiquadD(&es->metallicEq1[1], wR);
		wL = ProcessBiquadD(&es->metallicEq2[0], wL);
		wR = ProcessBiquadD(&es->metallicEq2[1], wR);
		wL = ProcessBiquadD(&es->glassEq[0], wL);
		wR = ProcessBiquadD(&es->glassEq[1], wR);
		wL = ProcessBiquadD(&es->warmthEq[0], wL);
		wR = ProcessBiquadD(&es->warmthEq[1], wR);
		wL = ProcessBiquadD(&es->colorAirEq[0], wL);
		wR = ProcessBiquadD(&es->colorAirEq[1], wR);
		wetL = (float)wL;
		wetR = (float)wR;

		// エキサイター (高域倍音の艶)
		if (es->exciterAmt > 0.005f) {
			float hiL = (float)ProcessBiquadD(&es->exciterHp[0], (double)wetL);
			float hiR = (float)ProcessBiquadD(&es->exciterHp[1], (double)wetR);
			wetL += (tanhf(hiL * 2.4f) - hiL) * es->exciterAmt;
			wetR += (tanhf(hiR * 2.4f) - hiR) * es->exciterAmt;
		}

		// 柔らかさ (softness) 1次LPF
		if (es->softAlpha < 0.99f) {
			es->softLpState[0] = FlushDenormalF(es->softLpState[0] + es->softAlpha * (wetL - es->softLpState[0]));
			es->softLpState[1] = FlushDenormalF(es->softLpState[1] + es->softAlpha * (wetR - es->softLpState[1]));
			float sm = ep->softness * 0.55f;
			wetL = wetL * (1.0f - sm) + es->softLpState[0] * sm;
			wetR = wetR * (1.0f - sm) + es->softLpState[1] * sm;
		}

		// --- 12. 本物の 4段ステレオ・オールパス・フェイザー ---
		if (es->phaserDepth > 0.005f) {
			es->phaserPhase += es->phaserInc;
			if (es->phaserPhase >= 2.0f * (float)M_PI) es->phaserPhase -= 2.0f * (float)M_PI;
			float lfoL = 0.5f + 0.5f * sinf(es->phaserPhase);
			float lfoR = 0.5f + 0.5f * sinf(es->phaserPhase + 2.0943951f); // 120度位相差
			float aL = 0.12f + lfoL * 0.74f;
			float aR = 0.12f + lfoR * 0.74f;
			float xL = wetL + es->phaserFbState[0] * 0.35f;
			float xR = wetR + es->phaserFbState[1] * 0.35f;
			for (int s = 0; s < 4; ++s) {
				float yL = -aL * xL + es->phaserState[0][s];
				es->phaserState[0][s] = FlushDenormalF(xL + aL * yL);
				xL = yL;
				float yR = -aR * xR + es->phaserState[1][s];
				es->phaserState[1][s] = FlushDenormalF(xR + aR * yR);
				xR = yR;
			}
			es->phaserFbState[0] = FlushDenormalF(xL);
			es->phaserFbState[1] = FlushDenormalF(xR);
			wetL = wetL * (1.0f - es->phaserDepth * 0.5f) + xL * es->phaserDepth;
			wetR = wetR * (1.0f - es->phaserDepth * 0.5f) + xR * es->phaserDepth;
		}

		// --- 13. 屋外の風ゆらぎ (非周期2成分変調) ---
		if (es->windAmount > 0.001f) {
			es->windPhase1 += windInc1;
			if (es->windPhase1 >= 2.0f * (float)M_PI) es->windPhase1 -= 2.0f * (float)M_PI;
			es->windPhase2 += windInc2;
			if (es->windPhase2 >= 2.0f * (float)M_PI) es->windPhase2 -= 2.0f * (float)M_PI;
			float wModL = 1.0f + (0.65f * sinf(es->windPhase1) + 0.35f * sinf(es->windPhase2)) * es->windAmount;
			float wModR = 1.0f + (0.65f * cosf(es->windPhase1) + 0.35f * cosf(es->windPhase2)) * es->windAmount;
			wetL *= wModL;
			wetR *= wModR;
		}

		// --- 14. 質感歪み (廃墟工場・SF異次元等の空間意図サチュレーション) ---
		if (es->distAmt > 0.005f) {
			float drive = 1.0f + es->distAmt * 3.2f;
			float invT = 1.0f / tanhf(drive);
			wetL = wetL * (1.0f - es->distAmt * 0.7f) + tanhf(wetL * drive) * invT * (es->distAmt * 0.7f);
			wetR = wetR * (1.0f - es->distAmt * 0.7f) + tanhf(wetR * drive) * invT * (es->distAmt * 0.7f);
		}

		// ウェット空間のステレオ展開
		{
			float wm = (wetL + wetR) * 0.5f;
			float ws = (wetL - wetR) * 0.5f * es->wetStereoWidth;
			wetL = wm + ws;
			wetR = wm - ws;
		}

		// --- 15. ドライ + ウェット合成 & 安全クランプ済み最終ステレオ定位 ---
		float outL = dryL * es->dryGain + wetL * es->wetGain;
		float outR = dryR * es->dryGain + wetR * es->wetGain;

		float totWidth = ClampFloat(es->finalStereoWidth * spatialWidth, 0.25f, 2.20f);
		if (fabsf(totWidth - 1.0f) > 0.01f) {
			float mid  = (outL + outR) * 0.5f;
			float side = (outL - outR) * 0.5f * totWidth;
			outL = mid + side;
			outR = mid - side;
		}

		L[i] = isfinite(outL) ? outL : 0.0f;
		R[i] = isfinite(outR) ? outR : 0.0f;
	}
}


// ============================================================
//  真のゼロレイテンシ・ルックアヘッド・トゥルーピーク・リミッター (-0.20 dBTP)
//  - 4x Catmull-Rom 補間によるサンプル間トゥルーピーク (ISP) 検出
//  - O(N) 単調減少デック (固定配列、std::deque 不使用) による前方 2.0ms 最大ピーク走査
//  - 後方走査でピークの 2.0ms 前から滑らかに降りる先読みアタック (クリック/割れゼロ)
//  - 前方走査でブロック跨ぎ連続状態 + 4ms ホールド + プログラム依存デュアルリリース
//    (瞬間ピークは 45ms でスッと戻りポンピングなし、持続大音量は 130ms で歪みゼロ)
//  - 最終段に極薄ソフトニー安全確認 (ハードクリップ完全防止、サンプル遅延ゼロ)
// ============================================================
static const float kEqCeiling = 0.97723722f; // -0.20 dBTP

struct EqCeilingState {
	int   la;
	int   holdSamp;
	int   rate;
	int   relHold;
	float gain;
	float gRelFast;
	float gRelSlow;
};

static EqCeilingState g_ceil[EQ_BANKS];
static float* g_ceilWork = NULL;
static int g_ceilWorkCap = 0;
static int* g_ceilDeque = NULL;
static int g_ceilDequeCap = 0;
static float* g_ceilAudio = NULL;
static int g_ceilAudioCap = 0;

static float EqCatmullPeak(float y0, float y1, float y2, float y3)
{
	float peak = fabsf(y1);
	const float a2 = fabsf(y2);
	if (a2 > peak) peak = a2;
	for (int k = 1; k <= 3; ++k) {
		const float t = 0.25f * (float)k;
		const float t2 = t * t;
		const float t3 = t2 * t;
		const float v = 0.5f * (
			(2.0f * y1) +
			(-y0 + y2) * t +
			(2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * t2 +
			(-y0 + 3.0f * y1 - 3.0f * y2 + y3) * t3);
		const float a = fabsf(v);
		if (a > peak) peak = a;
	}
	return peak;
}

static float* EqCeilAlloc(float** slot, int* cap, int nFloats)
{
	if (nFloats <= 0) return NULL;
	if (*slot && *cap >= nFloats) return *slot;
	if (*slot) {
		VirtualFree(*slot, 0, MEM_RELEASE);
		*slot = NULL;
		*cap = 0;
	}
	float* p = (float*)VirtualAlloc(NULL, (SIZE_T)nFloats * sizeof(float), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!p) return NULL;
	*slot = p;
	*cap = nFloats;
	return p;
}

static int* EqCeilAllocInt(int** slot, int* cap, int nInts)
{
	if (nInts <= 0) return NULL;
	if (*slot && *cap >= nInts) return *slot;
	if (*slot) {
		VirtualFree(*slot, 0, MEM_RELEASE);
		*slot = NULL;
		*cap = 0;
	}
	int* p = (int*)VirtualAlloc(NULL, (SIZE_T)nInts * sizeof(int), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!p) return NULL;
	*slot = p;
	*cap = nInts;
	return p;
}

static void EqCeilingPrepare(EqCeilingState* st, int rate)
{
	if (rate < 8000) rate = 8000;
	if (st->rate == rate && st->la > 0)
		return;
	// 2.0ms ルックアヘッド窓 + 4.0ms ピークホールド
	int la = (int)(0.0020f * (float)rate + 0.5f);
	if (la < 8) la = 8;
	if (la > 384) la = 384;
	int hold = (int)(0.0040f * (float)rate + 0.5f);
	if (hold < 8) hold = 8;
	if (hold > 768) hold = 768;

	st->la = la;
	st->holdSamp = hold;
	st->rate = rate;
	st->relHold = 0;
	st->gain = 1.0f;
	st->gRelFast = expf(-1.0f / (0.045f * (float)rate)); // 瞬間トランジェント: 45ms
	st->gRelSlow = expf(-1.0f / (0.130f * (float)rate)); // 持続大音量: 130ms
}

static void EqCeilingReset(int bank, int rate)
{
	if (bank < 0 || bank >= EQ_BANKS) bank = 0;
	g_ceil[bank].rate = 0;
	g_ceil[bank].gain = 1.0f;
	g_ceil[bank].relHold = 0;
	EqCeilingPrepare(&g_ceil[bank], rate > 0 ? rate : 44100);
}

// ブロック末尾エッジ等で万一 kEqCeiling を僅かに超えた場合の滑らかな最終安全ニー
static inline float EqCeilSafetySoftKnee(float x)
{
	const float a = fabsf(x);
	if (a <= 0.960f) return x;
	const float knee = 0.960f;
	const float span = 0.985f - knee; // 0.025
	float arg = (a - knee) / span;
	if (arg > 6.0f) arg = 6.0f;
	float y = knee + span * tanhf(arg);
	return (x >= 0.0f) ? y : -y;
}

// peak[0..n-1] (4x Catmull-Rom 真のピーク) から、滑らかな先読みゲイン gainOut[0..n-1] を計算
static void EqCeilingFillGains(float* peak, float* winReq, float* gainOut, int* dq, int n, int rate)
{
	EqCeilingState* st = &g_ceil[g_eqCur];
	EqCeilingPrepare(st, rate);
	if (n <= 0) return;

	const int la = st->la;
	const float atkSlope = 1.0f / (float)la;

	// 1. O(N) 単調減少デック: 各 i で前方窓 [i, min(n-1, i+la)] の最大ピークを求める
	int head = 0, tail = 0;
	for (int i = n - 1; i >= 0; --i) {
		const float p = peak[i];
		while (tail > head && peak[dq[tail - 1]] <= p)
			--tail;
		dq[tail++] = i;
		while (dq[head] > i + la)
			++head;
		const float wp = peak[dq[head]];
		winReq[i] = (wp > kEqCeiling) ? (kEqCeiling / wp) : 1.0f;
	}

	// 2. 後方走査 (先読み滑らかアタック): ピークの la サンプル前から滑らかに下降させる
	for (int i = n - 2; i >= 0; --i) {
		const float maxAllowed = winReq[i + 1] + atkSlope;
		if (winReq[i] > maxAllowed)
			winReq[i] = maxAllowed;
	}

	// 3. 前方走査 (ブロック跨ぎ状態連続 + ホールド + デュアルリリース)
	float g = st->gain;
	int hold = st->relHold;
	if (!isfinite(g) || g <= 0.0f || g > 1.0f) g = 1.0f;

	for (int i = 0; i < n; ++i) {
		const float req = winReq[i];
		// 瞬時ピーク保護 (ブロック跨ぎ直後の初サンプルでも天井超過を許さない)
		const float imm = (peak[i] > kEqCeiling) ? (kEqCeiling / peak[i]) : 1.0f;
		const float target = (req < imm) ? req : imm;

		if (target < g) {
			g = target;
			hold = st->holdSamp;
		}
		else if (target > g) {
			if (hold > 0) {
				--hold;
			}
			else {
				// 浅い瞬間リダクションは高速45ms復帰、深いリダクションは滑らか130ms復帰
				const float relC = (g > 0.86f) ? st->gRelFast : st->gRelSlow;
				g = target + relC * (g - target);
				if (g > 0.99995f && target >= 1.0f) g = 1.0f;
			}
		}
		gainOut[i] = g;
	}
	st->gain = g;
	st->relHold = hold;
}

static void EqCeilingNotePeaks(float* peak, int n, int nCh, float* const* planar, const float* interleaved)
{
	for (int ch = 0; ch < nCh; ++ch) {
		for (int i = 0; i < n - 1; ++i) {
			const int i0 = (i > 0) ? (i - 1) : 0;
			const int i3 = (i + 2 < n) ? (i + 2) : (n - 1);
			float y0, y1, y2, y3;
			if (planar) {
				const float* p = planar[ch];
				y0 = p[i0]; y1 = p[i]; y2 = p[i + 1]; y3 = p[i3];
			}
			else {
				y0 = interleaved[(size_t)i0 * (size_t)nCh + (size_t)ch];
				y1 = interleaved[(size_t)i * (size_t)nCh + (size_t)ch];
				y2 = interleaved[(size_t)(i + 1) * (size_t)nCh + (size_t)ch];
				y3 = interleaved[(size_t)i3 * (size_t)nCh + (size_t)ch];
			}
			const float seg = EqCatmullPeak(y0, y1, y2, y3);
			if (seg > peak[i]) peak[i] = seg;
			if (seg > peak[i + 1]) peak[i + 1] = seg;
		}
	}
}

static void EqCeilingApplyPlanar(float** ch, int nCh, int n, int rate)
{
	if (!ch || nCh < 1 || n <= 0 || rate <= 0) return;
	if (nCh > MAX_CH) nCh = MAX_CH;
	float* work = EqCeilAlloc(&g_ceilWork, &g_ceilWorkCap, n * 3);
	int* dq = EqCeilAllocInt(&g_ceilDeque, &g_ceilDequeCap, n);
	if (!work || !dq) {
		for (int i = 0; i < n; ++i) {
			for (int c = 0; c < nCh; ++c) {
				float x = ch[c][i];
				ch[c][i] = isfinite(x) ? EqCeilSafetySoftKnee(x) : 0.0f;
			}
		}
		return;
	}
	float* peak = work;
	float* winReq = work + n;
	float* gain = work + n * 2;
	for (int i = 0; i < n; ++i) {
		float p = 0.0f;
		for (int c = 0; c < nCh; ++c) {
			float x = ch[c][i];
			if (!isfinite(x)) { x = 0.0f; ch[c][i] = 0.0f; }
			const float a = fabsf(x);
			if (a > p) p = a;
		}
		peak[i] = p;
	}
	EqCeilingNotePeaks(peak, n, nCh, ch, NULL);
	EqCeilingFillGains(peak, winReq, gain, dq, n, rate);
	for (int i = 0; i < n; ++i) {
		const float g = gain[i];
		for (int c = 0; c < nCh; ++c) {
			float v = ch[c][i] * g;
			ch[c][i] = EqCeilSafetySoftKnee(v);
		}
	}
}

static void EqCeilingApplyInterleaved(float* frames, int nCh, int n, int rate)
{
	if (!frames || nCh < 1 || n <= 0 || rate <= 0) return;
	if (nCh > MAX_CH) nCh = MAX_CH;
	float* work = EqCeilAlloc(&g_ceilWork, &g_ceilWorkCap, n * 3);
	int* dq = EqCeilAllocInt(&g_ceilDeque, &g_ceilDequeCap, n);
	if (!work || !dq) {
		for (int i = 0; i < n; ++i) {
			float* fr = frames + (size_t)i * (size_t)nCh;
			for (int c = 0; c < nCh; ++c)
				fr[c] = isfinite(fr[c]) ? EqCeilSafetySoftKnee(fr[c]) : 0.0f;
		}
		return;
	}
	float* peak = work;
	float* winReq = work + n;
	float* gain = work + n * 2;
	for (int i = 0; i < n; ++i) {
		float p = 0.0f;
		float* fr = frames + (size_t)i * (size_t)nCh;
		for (int c = 0; c < nCh; ++c) {
			if (!isfinite(fr[c])) fr[c] = 0.0f;
			const float a = fabsf(fr[c]);
			if (a > p) p = a;
		}
		peak[i] = p;
	}
	EqCeilingNotePeaks(peak, n, nCh, NULL, frames);
	EqCeilingFillGains(peak, winReq, gain, dq, n, rate);
	for (int i = 0; i < n; ++i) {
		const float g = gain[i];
		float* fr = frames + (size_t)i * (size_t)nCh;
		for (int c = 0; c < nCh; ++c)
			fr[c] = EqCeilSafetySoftKnee(fr[c] * g);
	}
}

static void ApplyLookaheadLimiterStereo(float* L, float* R, int n, int rate)
{
	if (!L || !R || n <= 0 || rate <= 0) return;
	float* ch[2];
	ch[0] = L;
	ch[1] = R;
	EqCeilingApplyPlanar(ch, 2, n, rate);
}
// 銀行あたり ~1.25MB x2。静的 BSS を避けて必要時確保。
static float* g_eqLeftSamples[EQ_BANKS] = {};
static float* g_eqRightSamples[EQ_BANKS] = {};
enum { EQ_SAMPLE_CAP = 8192 * 40 };
static void EnsureEqSampleBuffers(int bank)
{
	if (bank < 0 || bank >= EQ_BANKS) bank = 0;
	if (!g_eqLeftSamples[bank]) {
		g_eqLeftSamples[bank] = (float*)VirtualAlloc(NULL, (SIZE_T)EQ_SAMPLE_CAP * sizeof(float), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
		g_eqRightSamples[bank] = (float*)VirtualAlloc(NULL, (SIZE_T)EQ_SAMPLE_CAP * sizeof(float), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	}
}




// ============================================================
// ★ ユーザー制御エフェクト: リバーブ / コーラス / ディレイ
// ============================================================
//  環境(ENV_PRESETS)エフェクトとは独立した、ユーザーが直接量を決める
//  エフェクト群。値は savedata.eq_reverb / eq_chorus / eq_delay (0-200)。
//    0        : オフ (既定値)
//    1-100    : モードA  強さ = v / 100
//    101-200  : モードB  強さ = (v - 100) / 100
//      eq_reverb  A=リバーブ      B=パンリバーブ
//      eq_chorus  A=コーラス      B=コーラスディストーション
//      eq_delay   A=ディレイ      B=マルチディレイ(ピンポン)
//  L/R バッファ(出力直前)に対してブロック単位で処理する。状態はブロックを
//  跨いで保持。ピーク制御は最終段のルックアヘッドリミッターが担うため、
//  ここでウェット量を足してもハードクリップ(音割れ)には至らない。
// ============================================================

// ---------- リバーブ (Freeverb 風: 並列コム8 + 直列オールパス4) ----------
#define FX_REV_COMBS   8
#define FX_REV_ALLPS   4
#define FX_REV_MAXCOMB 8192   // 192kHz でも最長コム(約7040)を収容
#define FX_REV_MAXALL  3072   // 192kHz でも最長オールパス(約2421)を収容
typedef struct {
	float comb[2][FX_REV_COMBS][FX_REV_MAXCOMB];
	int   combPos[2][FX_REV_COMBS];
	int   combLen[2][FX_REV_COMBS];
	float combLP[2][FX_REV_COMBS];      // ダンピング用 one-pole 状態
	float allp[2][FX_REV_ALLPS][FX_REV_MAXALL];
	int   allpPos[2][FX_REV_ALLPS];
	int   allpLen[2][FX_REV_ALLPS];
	float panPhase;                     // パンリバーブ用 自動パン LFO 位相
	int   rate;
} FxReverb;
static FxReverb g_fxReverb[EQ_BANKS];

static void FxReverbReset(int rate) {
	memset(&g_fxReverb[g_eqCur], 0, sizeof(g_fxReverb[0]));
	// Freeverb の標準チューニング (44100Hz 基準のサンプル数)
	static const int combTune[FX_REV_COMBS] = { 1116,1188,1277,1356,1422,1491,1557,1617 };
	static const int allpTune[FX_REV_ALLPS] = { 556,441,341,225 };
	const int spread = 23;  // 右チャンネルのステレオスプレッド
	for (int ch = 0; ch < 2; ch++) {
		for (int c = 0; c < FX_REV_COMBS; c++) {
			int len = (int)((long long)combTune[c] * rate / 44100) + (ch ? spread : 0);
			if (len < 8) len = 8;
			if (len >= FX_REV_MAXCOMB) len = FX_REV_MAXCOMB - 1;
			g_fxReverb[g_eqCur].combLen[ch][c] = len;
		}
		for (int a = 0; a < FX_REV_ALLPS; a++) {
			int len = (int)((long long)allpTune[a] * rate / 44100) + (ch ? spread : 0);
			if (len < 8) len = 8;
			if (len >= FX_REV_MAXALL) len = FX_REV_MAXALL - 1;
			g_fxReverb[g_eqCur].allpLen[ch][a] = len;
		}
	}
	g_fxReverb[g_eqCur].rate = rate;
}

static void FxProcessReverb(float* L, float* R, int n, int rate, float amount, BOOL panMode) {
	if (amount <= 0.0f || n <= 0) return;
	if (g_fxReverb[g_eqCur].rate != rate) FxReverbReset(rate);
	if (amount > 1.0f) amount = 1.0f;

	const float roomSize = 0.74f + amount * 0.245f;   // フィードバック 0.74..~0.985 (残響を長く)
	const float damp = 0.15f + amount * 0.08f;         // ダンピングを弱め残響を明瞭に
	const float damp1 = damp, damp2 = 1.0f - damp;
	const float inGain = 0.028f;                       // 入力ゲインを上げウェットを増強
	const float wetMix = amount * 0.95f;               // 最大 95% wet (効きを強化)
	const float dryMix = 1.0f - wetMix * 0.30f;        // dry をより多く残し音量感を確保
	const float panRate = 0.20f;                       // パンリバーブの自動パン速度(Hz)

	for (int i = 0; i < n; i++) {
		float inMono = (L[i] + R[i]) * 0.5f * inGain;
		float outCh[2] = { 0.0f, 0.0f };
		for (int ch = 0; ch < 2; ch++) {
			float acc = 0.0f;
			// 並列コムフィルタ (各々ダンピング付きフィードバック)
			for (int c = 0; c < FX_REV_COMBS; c++) {
				int p = g_fxReverb[g_eqCur].combPos[ch][c];
				float y = g_fxReverb[g_eqCur].comb[ch][c][p];
				g_fxReverb[g_eqCur].combLP[ch][c] = y * damp2 + g_fxReverb[g_eqCur].combLP[ch][c] * damp1;
				g_fxReverb[g_eqCur].comb[ch][c][p] = inMono + g_fxReverb[g_eqCur].combLP[ch][c] * roomSize;
				if (++p >= g_fxReverb[g_eqCur].combLen[ch][c]) p = 0;
				g_fxReverb[g_eqCur].combPos[ch][c] = p;
				acc += y;
			}
			// 直列オールパス (拡散)
			for (int a = 0; a < FX_REV_ALLPS; a++) {
				int p = g_fxReverb[g_eqCur].allpPos[ch][a];
				float bufout = g_fxReverb[g_eqCur].allp[ch][a][p];
				float y = -acc + bufout;
				g_fxReverb[g_eqCur].allp[ch][a][p] = acc + bufout * 0.5f;
				if (++p >= g_fxReverb[g_eqCur].allpLen[ch][a]) p = 0;
				g_fxReverb[g_eqCur].allpPos[ch][a] = p;
				acc = y;
			}
			outCh[ch] = acc;
		}
		float wetL = outCh[0];
		float wetR = outCh[1];
		if (panMode) {
			// パンリバーブ: 残響を左右にゆっくり回す
			g_fxReverb[g_eqCur].panPhase += panRate / (float)rate;
			if (g_fxReverb[g_eqCur].panPhase >= 1.0f) g_fxReverb[g_eqCur].panPhase -= 1.0f;
			float lfo = sinf(g_fxReverb[g_eqCur].panPhase * 2.0f * (float)M_PI);
			float gL = 0.5f + 0.5f * lfo;
			float gR = 1.0f - gL;
			float mixL = wetL * gL + wetR * gR;
			float mixR = wetL * gR + wetR * gL;
			wetL = mixL; wetR = mixR;
		}
		float oL = L[i] * dryMix + wetL * wetMix;
		float oR = R[i] * dryMix + wetR * wetMix;
		L[i] = isfinite(oL) ? oL : 0.0f;
		R[i] = isfinite(oR) ? oR : 0.0f;
	}
}

// ---------- フラクショナル遅延読み出し (コーラス用) ----------
static inline float FxReadFrac(const float* buf, int wpos, float delaySamp, int maxLen) {
	if (delaySamp < 1.0f) delaySamp = 1.0f;
	if (delaySamp > (float)(maxLen - 2)) delaySamp = (float)(maxLen - 2);
	float rp = (float)wpos - delaySamp;
	while (rp < 0.0f) rp += (float)maxLen;
	int i0 = (int)rp;
	float frac = rp - (float)i0;
	int i1 = i0 + 1; if (i1 >= maxLen) i1 -= maxLen;
	return buf[i0] * (1.0f - frac) + buf[i1] * frac;
}

// ---------- コーラス (3声モジュレーションディレイ) ----------
#define FX_CHO_MAX 8192
typedef struct {
	float buf[2][FX_CHO_MAX];
	int   wpos[2];
	float lfoPhase;
	int   rate;
} FxChorus;
static FxChorus g_fxChorus[EQ_BANKS];

static void FxChorusReset(int rate) {
	memset(&g_fxChorus[g_eqCur], 0, sizeof(g_fxChorus[0]));
	g_fxChorus[g_eqCur].rate = rate;
}

static void FxProcessChorus(float* L, float* R, int n, int rate, float amount, BOOL distMode) {
	if (amount <= 0.0f || n <= 0) return;
	if (g_fxChorus[g_eqCur].rate != rate) FxChorusReset(rate);
	if (amount > 1.0f) amount = 1.0f;

	const float baseSamp = 16.0f * rate / 1000.0f;          // 基準遅延 16ms
	const float depthSamp = (6.0f + amount * 9.0f) * rate / 1000.0f; // 深さ 6-15ms (うねりを強化)
	const float lfoHz = 0.6f;                                // 変調をやや速く存在感を強化
	const float wetMix = 0.95f * amount;                     // 最大 95% wet (効きを強化)
	const float drive = 1.0f + amount * 3.0f;
	const float driveNorm = 1.0f / tanhf(drive);

	for (int i = 0; i < n; i++) {
		g_fxChorus[g_eqCur].lfoPhase += lfoHz / (float)rate;
		if (g_fxChorus[g_eqCur].lfoPhase >= 1.0f) g_fxChorus[g_eqCur].lfoPhase -= 1.0f;
		for (int ch = 0; ch < 2; ch++) {
			float* buf = g_fxChorus[g_eqCur].buf[ch];
			int wp = g_fxChorus[g_eqCur].wpos[ch];
			float dry = (ch == 0) ? L[i] : R[i];
			buf[wp] = dry;
			float wet = 0.0f;
			for (int v = 0; v < 3; v++) {
				float ph = g_fxChorus[g_eqCur].lfoPhase + (float)v / 3.0f + (ch ? 0.25f : 0.0f);
				float lfo = sinf(ph * 2.0f * (float)M_PI);
				float d = baseSamp + depthSamp * (0.5f + 0.5f * lfo);
				wet += FxReadFrac(buf, wp, d, FX_CHO_MAX);
			}
			wet *= 0.4f;  // 3声合計の正規化
			if (distMode) wet = tanhf(wet * drive) * driveNorm;  // コーラスディストーション
			float out = dry * (1.0f - wetMix * 0.45f) + wet * wetMix;
			if (++wp >= FX_CHO_MAX) wp = 0;
			g_fxChorus[g_eqCur].wpos[ch] = wp;
			if (ch == 0) L[i] = isfinite(out) ? out : 0.0f;
			else         R[i] = isfinite(out) ? out : 0.0f;
		}
	}
}

// ---------- ディレイ / マルチディレイ(ピンポン) ----------
#define FX_DLY_MAX 96000   // 0.5s @192kHz / 約2.18s @44.1kHz
typedef struct {
	float buf[2][FX_DLY_MAX];
	int   wpos[2];
	int   rate;
} FxDelay;
static FxDelay g_fxDelay[EQ_BANKS];

static void FxDelayReset(int rate) {
	memset(&g_fxDelay[g_eqCur], 0, sizeof(g_fxDelay[0]));
	g_fxDelay[g_eqCur].rate = rate;
}

static void FxProcessDelay(float* L, float* R, int n, int rate, float amount, BOOL multiMode) {
	if (amount <= 0.0f || n <= 0) return;
	if (g_fxDelay[g_eqCur].rate != rate) FxDelayReset(rate);
	if (amount > 1.0f) amount = 1.0f;

	int d1 = (int)(0.250f * rate);   // 250ms 主ディレイ
	int d2 = (int)(0.375f * rate);   // 375ms 第2タップ(マルチ用)
	if (d1 >= FX_DLY_MAX) d1 = FX_DLY_MAX - 1;
	if (d2 >= FX_DLY_MAX) d2 = FX_DLY_MAX - 1;
	const float feedback = 0.30f + amount * 0.55f;   // 0.30..0.85 (反復を増やし効きを強化)
	const float wetMix = 0.6f * amount;              // 最大 60% wet (やや強めに)

	float* bL = g_fxDelay[g_eqCur].buf[0];
	float* bR = g_fxDelay[g_eqCur].buf[1];

	for (int i = 0; i < n; i++) {
		int wpL = g_fxDelay[g_eqCur].wpos[0];
		int wpR = g_fxDelay[g_eqCur].wpos[1];

		int rL = wpL - d1; if (rL < 0) rL += FX_DLY_MAX;
		int rR = wpR - d1; if (rR < 0) rR += FX_DLY_MAX;
		float dlL = bL[rL];
		float dlR = bR[rR];

		float dryL = L[i], dryR = R[i];
		float wetL, wetR, fbL, fbR;

		if (multiMode) {
			// マルチディレイ: クロスフィードバックのピンポン + 第2タップ
			int r2L = wpL - d2; if (r2L < 0) r2L += FX_DLY_MAX;
			int r2R = wpR - d2; if (r2R < 0) r2R += FX_DLY_MAX;
			wetL = dlL + bR[r2R] * 0.6f;
			wetR = dlR + bL[r2L] * 0.6f;
			fbL = dryL + dlR * feedback;   // L には R のエコーが返る
			fbR = dryR + dlL * feedback;   // R には L のエコーが返る
		}
		else {
			wetL = dlL;
			wetR = dlR;
			fbL = dryL + dlL * feedback;
			fbR = dryR + dlR * feedback;
		}

		bL[wpL] = isfinite(fbL) ? fbL : 0.0f;
		bR[wpR] = isfinite(fbR) ? fbR : 0.0f;

		float oL = dryL + wetL * wetMix;
		float oR = dryR + wetR * wetMix;
		L[i] = isfinite(oL) ? oL : 0.0f;
		R[i] = isfinite(oR) ? oR : 0.0f;

		if (++wpL >= FX_DLY_MAX) wpL = 0;
		if (++wpR >= FX_DLY_MAX) wpR = 0;
		g_fxDelay[g_eqCur].wpos[0] = wpL;
		g_fxDelay[g_eqCur].wpos[1] = wpR;
	}
}

// ---------- savedata 値(0-200)を解釈してエフェクトを適用 ----------
// チェーン順: コーラス → ディレイ → リバーブ (一般的なシグナルフロー)
static void FxApplyUserEffects(float* L, float* R, int n, int rate) {
	if (n <= 0) return;

	if (g_eqChorus > 0) {
		BOOL dist = (g_eqChorus > 100);
		float amt = dist ? (g_eqChorus - 100) / 100.0f : g_eqChorus / 100.0f;
		FxProcessChorus(L, R, n, rate, amt, dist);
	}
	if (g_eqDelay > 0) {
		BOOL multi = (g_eqDelay > 100);
		float amt = multi ? (g_eqDelay - 100) / 100.0f : g_eqDelay / 100.0f;
		FxProcessDelay(L, R, n, rate, amt, multi);
	}
	if (g_eqReverb > 0) {
		BOOL pan = (g_eqReverb > 100);
		float amt = pan ? (g_eqReverb - 100) / 100.0f : g_eqReverb / 100.0f;
		FxProcessReverb(L, R, n, rate, amt, pan);
	}
}


// ============================================================
// サラウンド化 (savedata.surround 0..100)
// EQ / リミッター前に適用。0=オフ。後段で割れないようヘッドルームを残す。
// ・2ch: マトリクス/Haas/サイド強調（サウンドボードの LR→5.1 展開向け）
// ・4/5.1/7.1: リア（とサイド）を L−R 差分と遅延で強調
// ============================================================
static const int SUR_DLY_MAX = 4096;
struct SurroundState {
	float dL[SUR_DLY_MAX];
	float dR[SUR_DLY_MAX];
	int wpos;
	int dly;
	int rate;
	float apL, apR;
};
static SurroundState g_sur[EQ_BANKS];

static void SurroundResetBank(int bank, int rate)
{
	if (bank < 0 || bank >= EQ_BANKS) bank = 0;
	memset(&g_sur[bank], 0, sizeof(g_sur[bank]));
	if (rate < 8000) rate = 44100;
	g_sur[bank].rate = rate;
	int d = (int)(rate * 0.012f + 0.5f); // ~12ms Haas
	if (d < 32) d = 32;
	if (d >= SUR_DLY_MAX) d = SUR_DLY_MAX - 1;
	g_sur[bank].dly = d;
}

static inline float SurLoad(const unsigned char* p, int bits)
{
	if (bits == 16)
		return (*(const short*)p) / 32768.0f;
	if (bits == 24) {
		int v = p[0] | (p[1] << 8) | ((signed char)p[2] << 16);
		return v / 8388608.0f;
	}
	if (bits == 32)
		return (*(const int*)p) / 2147483648.0f;
	return (p[0] - 128) / 128.0f;
}

static inline void SurStore(unsigned char* p, int bits, float x)
{
	if (x > 1.0f) x = 1.0f;
	if (x < -1.0f) x = -1.0f;
	if (bits == 16) {
		int32_t v = (int32_t)roundf(x * 32768.0f);
		if (v > 32767) v = 32767;
		if (v < -32768) v = -32768;
		*(short*)p = (short)v;
	}
	else if (bits == 24) {
		int32_t v = (int32_t)roundf(x * 8388608.0f);
		if (v > 8388607) v = 8388607;
		if (v < -8388608) v = -8388608;
		p[0] = (unsigned char)(v & 0xFF);
		p[1] = (unsigned char)((v >> 8) & 0xFF);
		p[2] = (unsigned char)((v >> 16) & 0xFF);
	}
	else if (bits == 32)
		*(int*)p = (int)(x * 2147483647.0f);
	else
		p[0] = (unsigned char)(x * 127.0f + 128.0f);
}

static void ApplySurroundProcess(void* data, int len, int rate, int bits, int ch)
{
	int amtI = savedata.surround;
	if (amtI < 0) amtI = 0;
	if (amtI > 100) amtI = 100;
	if (amtI <= 0 || !data || len <= 0 || bits < 8 || ch < 1)
		return;
	const float amt = (float)amtI / 100.0f;
	const int bps = bits / 8;
	const int frame = bps * ch;
	if (frame <= 0 || len < frame) return;
	const int n = len / frame;
	if (rate <= 0) rate = 44100;
	if (g_sur[g_eqCur].rate != rate || g_sur[g_eqCur].dly <= 0)
		SurroundResetBank(g_eqCur, rate);

	SurroundState& st = g_sur[g_eqCur];
	unsigned char* p = (unsigned char*)data;
	const float kSide = 1.0f + 0.55f * amt;
	const float kHaas = 0.22f * amt;
	const float kRear = 0.40f * amt;
	// EQ / リミッター前なのでフルスケールまで張り付けない
	const float head = 1.0f / (1.0f + 0.50f * amt);

	for (int i = 0; i < n; ++i) {
		unsigned char* f = p + (size_t)i * (size_t)frame;
		float L = SurLoad(f + 0 * bps, bits);
		float R = (ch >= 2) ? SurLoad(f + 1 * bps, bits) : L;

		int rp = st.wpos - st.dly;
		if (rp < 0) rp += SUR_DLY_MAX;
		const float dL = st.dL[rp];
		const float dR = st.dR[rp];

		if (ch <= 2) {
			float mid = (L + R) * 0.5f;
			float side = (L - R) * 0.5f;
			// 軽いオールパスでサイドを散らす（相関を下げマトリクスデコードしやすく）
			st.apL = side + 0.55f * st.apL;
			side = side * 0.45f + st.apL * 0.55f;
			side *= kSide;
			float oL = mid + side + kHaas * dR;
			float oR = mid - side + kHaas * dL;
			L = (L + (oL - L) * amt) * head;
			R = (R + (oR - R) * amt) * head;
			SurStore(f + 0 * bps, bits, L);
			if (ch >= 2) SurStore(f + 1 * bps, bits, R);
		}
		else {
			// フロントは軽く広げ、リア/サイドへ差分＋遅延を足す
			float mid = (L + R) * 0.5f;
			float side = (L - R) * 0.5f;
			st.apL = side + 0.50f * st.apL;
			side = side * 0.50f + st.apL * 0.50f;
			L = (L + (L - mid) * (0.15f * amt)) * head;
			R = (R + (R - mid) * (0.15f * amt)) * head;
			SurStore(f + 0 * bps, bits, L);
			SurStore(f + 1 * bps, bits, R);

			if (ch == 4) {
				// FL FR BL BR
				float bl = SurLoad(f + 2 * bps, bits);
				float br = SurLoad(f + 3 * bps, bits);
				SurStore(f + 2 * bps, bits, bl + side * kRear + kHaas * dR * 0.5f);
				SurStore(f + 3 * bps, bits, br - side * kRear + kHaas * dL * 0.5f);
			}
			else if (ch == 6) {
				// FL FR FC LFE BL BR
				float bl = SurLoad(f + 4 * bps, bits);
				float br = SurLoad(f + 5 * bps, bits);
				SurStore(f + 4 * bps, bits, bl + side * kRear + kHaas * dR * 0.5f);
				SurStore(f + 5 * bps, bits, br - side * kRear + kHaas * dL * 0.5f);
			}
			else if (ch >= 8) {
				// FL FR FC LFE BL BR SL SR
				float bl = SurLoad(f + 4 * bps, bits);
				float br = SurLoad(f + 5 * bps, bits);
				float sl = SurLoad(f + 6 * bps, bits);
				float sr = SurLoad(f + 7 * bps, bits);
				SurStore(f + 4 * bps, bits, bl + side * kRear + kHaas * dR * 0.5f);
				SurStore(f + 5 * bps, bits, br - side * kRear + kHaas * dL * 0.5f);
				SurStore(f + 6 * bps, bits, sl + side * (kRear * 0.85f) + kHaas * dR * 0.35f);
				SurStore(f + 7 * bps, bits, sr - side * (kRear * 0.85f) + kHaas * dL * 0.35f);
			}
			else if (ch == 3) {
				// L R LFE: LFE に mid 成分を少し（低域の厚み）
				float lfe = SurLoad(f + 2 * bps, bits);
				SurStore(f + 2 * bps, bits, lfe + mid * (0.12f * amt));
			}
		}

		st.dL[st.wpos] = L;
		st.dR[st.wpos] = R;
		if (++st.wpos >= SUR_DLY_MAX) st.wpos = 0;
	}
}

static void EqTailOut(void* outPtr, int outLen, int rate)
{
	ProAudio_ApplyXfadeIn(outPtr, outLen, rate, wavsam_depth, wavchannel);
	ProAudio_PushTailPcm(outPtr, outLen, rate, wavsam_depth, wavchannel);
}

// ============================================================
//  エンジン初期化 (サンプルレート変更または reset==1 時に呼ばれる)
//  - 全チャンネル EQ/トーンフィルタをバイパスへ初期化
//  - LR4 3バンドプロテクタ / 立体環境音響エンジン(VirtualAlloc一括プール)をリセット
//  - ユーザー追加FX / ルックアヘッドリミッター / サラウンドをリセット
//  - 再生中の malloc / free / new / delete は一切行わない
// ============================================================
static void InitEngine(int rate, int bank = 0) {
	if (bank < 0 || bank >= EQ_BANKS) bank = 0;
	if (rate < 8000) rate = 44100;
	const int prevBank = g_eqCur;
	g_eqCur = bank;

	memset(g_channels[g_eqCur], 0, sizeof(g_channels[g_eqCur]));
	for (int ch = 0; ch < MAX_CH; ++ch) {
		for (int b = 0; b < EQ_BANDS; ++b)
			SetBiquadBypass(&g_channels[g_eqCur][ch].eqFilters[b]);
		SetBiquadBypass(&g_channels[g_eqCur][ch].clarityPeak);
		SetBiquadBypass(&g_channels[g_eqCur][ch].clarityAir);
		SetBiquadBypass(&g_channels[g_eqCur][ch].bassBalanceFilter);
		SetBiquadBypass(&g_channels[g_eqCur][ch].trebleBalanceFilter);
		SetBiquadBypass(&g_channels[g_eqCur][ch].densityLow);
		SetBiquadBypass(&g_channels[g_eqCur][ch].densityMid);
		CalcBiquadFilter(&g_channels[g_eqCur][ch].bassMonoLP, 0, 120.0, 0.7071, rate);
		g_channels[g_eqCur][ch].harmonicState = 0.0f;
	}

	// LR4 3バンド位相整合ダイナミックプロテクタ初期化
	MultibandReset(&g_mbProt[g_eqCur], rate);

	// 立体環境音響エンジン初期化 (プールは初回のみ VirtualAlloc)
	float* envPool = EnsureEnvPool(g_eqCur);
	EnvEngineReset(&g_envEngine[g_eqCur], envPool, rate);

	g_lastRate[g_eqCur] = rate;
	g_lastEqPreset[g_eqCur] = -1;
	g_lastEnvPreset[g_eqCur] = -1;

	for (int i = 0; i < 15; ++i) g_lastEqValues[g_eqCur][i] = 100;
	for (int i = 0; i < 5; ++i) g_lastExtendedParams[g_eqCur][i] = 100;

	g_lastEffectAmount[g_eqCur] = 50;
	g_initialized[g_eqCur] = TRUE;

	// 追加エフェクトパラメータ (0 = オフ)
	g_eqReverb = 0;
	g_eqChorus = 0;
	g_eqDelay = 0;

	FxReverbReset(rate);
	FxChorusReset(rate);
	FxDelayReset(rate);

	EqCeilingReset(g_eqCur, rate);
	SurroundResetBank(g_eqCur, rate);
	g_eqCur = prevBank;
}

// ============================================================
//  エンジンバッファ解放 (アプリ終了時・破棄時に呼ぶ)
// ============================================================
void FreeEngine(void) {
	for (int b = 0; b < EQ_BANKS; ++b) {
		BindEnvPoolPointers(&g_envEngine[b], NULL);
		if (g_envPoolMem[b]) {
			VirtualFree(g_envPoolMem[b], 0, MEM_RELEASE);
			g_envPoolMem[b] = NULL;
		}
		if (g_eqLeftSamples[b]) {
			VirtualFree(g_eqLeftSamples[b], 0, MEM_RELEASE);
			g_eqLeftSamples[b] = NULL;
		}
		if (g_eqRightSamples[b]) {
			VirtualFree(g_eqRightSamples[b], 0, MEM_RELEASE);
			g_eqRightSamples[b] = NULL;
		}
		if (g_rsPool[b].pcmBuf) {
			VirtualFree(g_rsPool[b].pcmBuf, 0, MEM_RELEASE);
			g_rsPool[b].pcmBuf = NULL;
			g_rsPool[b].pcmCap = 0;
		}
		if (g_rsPool[b].srcFloat) {
			VirtualFree(g_rsPool[b].srcFloat, 0, MEM_RELEASE);
			g_rsPool[b].srcFloat = NULL;
			g_rsPool[b].srcCap = 0;
		}
		if (g_rsPool[b].dstFloat) {
			VirtualFree(g_rsPool[b].dstFloat, 0, MEM_RELEASE);
			g_rsPool[b].dstFloat = NULL;
			g_rsPool[b].dstCap = 0;
		}
	}
	if (g_ceilWork) {
		VirtualFree(g_ceilWork, 0, MEM_RELEASE);
		g_ceilWork = NULL;
		g_ceilWorkCap = 0;
	}
	if (g_ceilDeque) {
		VirtualFree(g_ceilDeque, 0, MEM_RELEASE);
		g_ceilDeque = NULL;
		g_ceilDequeCap = 0;
	}
	if (g_ceilAudio) {
		VirtualFree(g_ceilAudio, 0, MEM_RELEASE);
		g_ceilAudio = NULL;
		g_ceilAudioCap = 0;
	}
}


// ============================================================
// 拡張音量 / フォーマット別音量ブースト (マスター音量とは独立)
// COggDlg 拡張音量 + CRender SPC/KPI/MP3/Winamp/CEmu 倍率をここで1本化
//
// mode:
//   NONE(0) … ネイティブ音声等。フォーマット倍率は掛けない
//   KPI (1) … readkpi / mid VST（その他のkpi = savedata.kpivol）
//             SPC/HES は spcApplicable のとき savedata.spc のみ（kpivol と二重掛けしない）
//   MP3 (2) … readmp3/m4a 専用。savedata.mp3
//   WINAMP(3) … Winamp/XMPlay/AIMP。savedata.winampvol
//   CEMU (4) … CEmu PCM。savedata.cemuvol
// sticky なので、デコード側で毎回正しい mode を立てること。
// mid VST の x64 は KpiHost32 が生PCMを返し、本体(x86) playwavvst で equaliser を掛ける。
// ============================================================

static int  g_eqFormatVolMode = EQ_FMT_VOL_NONE;
static BOOL g_eqFormatVolSpc = FALSE;

void EqualiserSetFormatVolContext(int mode, BOOL spcApplicable)
{
	g_eqFormatVolMode = mode;
	g_eqFormatVolSpc = spcApplicable;
}

static float EqCalcSpcVolumeGain(int spcVal)
{
	switch (spcVal) {
	case 2:  return 2.0f;
	case 4:  return 3.0f;
	case 8:  return 4.0f;
	case 16: return 5.0f;
	default: return 1.0f;
	}
}

static float EqCalcKpiVolumeGain(int kpiVal, int /*bitDepth*/)
{
	// CRender「その他のkpi」: 等倍/2倍/3倍/4倍/5倍 (savedata.kpivol = 1..5)
	// bitDepth で分岐しない（24/32bit 時に 3,5 が default→1.0 になっていたのを修正）
	switch (kpiVal) {
	case 1: return 1.0f;
	case 2: return 2.0f;
	case 3: return 3.0f;
	case 4: return 4.0f;
	case 5: return 5.0f;
	default: return 1.0f;
	}
}

static float EqCalcMp3VolumeGain(int mp3Val, int bitDepth)
{
	if (mp3Val == 1) return 1.0f;
	if (bitDepth == 24 || bitDepth == 32) {
		switch (mp3Val) {
		case 2:  return 2.0f;
		case 4:  return 3.0f;
		case 8:  return 4.0f;
		case 16: return 5.0f;
		default: return 1.0f;
		}
	}
	switch (mp3Val) {
	case 2: return 1.5f;
	case 3: return 2.0f;
	case 4: return 2.5f;
	case 5: return 3.0f;
	default: return 1.0f;
	}
}

static float GetFormatVolumeGain(void)
{
	switch (g_eqFormatVolMode) {
	case EQ_FMT_VOL_KPI:
		if (g_eqFormatVolSpc)
			return EqCalcSpcVolumeGain(savedata.spc);
		return EqCalcKpiVolumeGain(savedata.kpivol, wavsam_depth);
	case EQ_FMT_VOL_WINAMP:
		return EqCalcKpiVolumeGain(savedata.winampvol, wavsam_depth);
	case EQ_FMT_VOL_CEMU:
		return EqCalcKpiVolumeGain(savedata.cemuvol, wavsam_depth);
	case EQ_FMT_VOL_MP3:
		return EqCalcMp3VolumeGain(savedata.mp3, wavsam_depth);
	default:
		return 1.0f;
	}
}

static float GetExternalBoostGain(void)
{
	const float kakuGain = ClampFloat((float)savedata.kakuVal / 100.0f, 1.0f, 9.0f);
	return kakuGain * GetFormatVolumeGain() * ProAudio_ReplayGainLinear();
}

static inline float PcmSampleToFloat(const unsigned char* pRaw, int offset, int wavsam)
{
	if (wavsam == 16)
		return *((short*)(pRaw + offset)) / 32768.0f;
	if (wavsam == 24) {
		int val = pRaw[offset] | (pRaw[offset + 1] << 8) | ((signed char)pRaw[offset + 2] << 16);
		return val / 8388608.0f;
	}
	if (wavsam == 32)
		return *((int*)(pRaw + offset)) / 2147483648.0f;
	return (pRaw[offset] - 128) / 128.0f;
}

static inline void FloatToPcmSample(unsigned char* pRaw, int offset, int wavsam, float finalOut)
{
	if (finalOut > 1.0f)  finalOut = 1.0f;
	if (finalOut < -1.0f) finalOut = -1.0f;

	if (wavsam == 16) {
		int32_t v = (int32_t)roundf(finalOut * 32768.0f);
		if (v > 32767) v = 32767; if (v < -32768) v = -32768;
		*((short*)(pRaw + offset)) = (short)v;
	}
	else if (wavsam == 24) {
		int32_t v = (int32_t)roundf(finalOut * 8388608.0f);
		if (v > 8388607) v = 8388607; if (v < -8388608) v = -8388608;
		pRaw[offset] = v & 0xFF;
		pRaw[offset + 1] = (v >> 8) & 0xFF;
		pRaw[offset + 2] = (v >> 16) & 0xFF;
	}
	else if (wavsam == 32)
		*((int*)(pRaw + offset)) = (int)(finalOut * 2147483647.0f);
	else
		pRaw[offset] = (unsigned char)(finalOut * 127.0f + 128.0f);
}

static void ApplyExternalBoostOnlyToBuffer(
	unsigned char* pRaw, int numSamples, int wavch, int wavsam, int bytesPerSample)
{
	const float extGain = GetExternalBoostGain();
	const int rate = wavbitbackup > 0 ? wavbitbackup : 44100;
	const int nCh = (wavch > MAX_CH) ? MAX_CH : wavch;
	if (nCh < 1 || numSamples <= 0) return;
	// ゲイン 1 かつ天井未満なら PCM を書き換えない。割れがあるバッファだけ下げる。
	const float heldGain = g_ceil[g_eqCur].gain;
	if (extGain <= 1.0001f && (!(heldGain > 0.0f) || heldGain >= 0.9999f)) {
		bool hot = false;
		for (int i = 0; i < numSamples && !hot; ++i) {
			for (int ch = 0; ch < nCh; ++ch) {
				const int offset = (i * wavch + ch) * bytesPerSample;
				if (fabsf(PcmSampleToFloat(pRaw, offset, wavsam)) > kEqCeiling) {
					hot = true;
					break;
				}
			}
		}
		if (!hot) return;
	}
	float* audio = (numSamples <= (INT_MAX / nCh))
		? EqCeilAlloc(&g_ceilAudio, &g_ceilAudioCap, numSamples * nCh) : NULL;
	if (!audio) {
		for (int i = 0; i < numSamples; i++) {
			float frame[MAX_CH];
			float p = 0.0f;
			for (int ch = 0; ch < nCh; ch++) {
				const int offset = (i * wavch + ch) * bytesPerSample;
				frame[ch] = PcmSampleToFloat(pRaw, offset, wavsam) * extGain;
				const float a = fabsf(frame[ch]);
				if (a > p) p = a;
			}
			if (p > kEqCeiling) {
				const float g = kEqCeiling / p;
				for (int ch = 0; ch < nCh; ch++) frame[ch] *= g;
			}
			for (int ch = 0; ch < nCh; ch++) {
				const int offset = (i * wavch + ch) * bytesPerSample;
				FloatToPcmSample(pRaw, offset, wavsam, frame[ch]);
			}
		}
		return;
	}
	for (int i = 0; i < numSamples; i++) {
		for (int ch = 0; ch < nCh; ch++) {
			const int offset = (i * wavch + ch) * bytesPerSample;
			audio[(size_t)i * (size_t)nCh + (size_t)ch] =
				PcmSampleToFloat(pRaw, offset, wavsam) * extGain;
		}
	}
	EqCeilingApplyInterleaved(audio, nCh, numSamples, rate);
	for (int i = 0; i < numSamples; i++) {
		for (int ch = 0; ch < nCh; ch++) {
			const int offset = (i * wavch + ch) * bytesPerSample;
			FloatToPcmSample(pRaw, offset, wavsam, audio[(size_t)i * (size_t)nCh + (size_t)ch]);
		}
	}
}


// ============================================================
// ResampleUp() - Lanczos-2 アップサンプリング (再利用プール使用・malloc/freeゼロ)
// srcRate < 32000 の音源のみ 44100Hz に変換して内部処理する
// ※ dstData には g_rsPool[g_eqCur].pcmBuf が返るため呼び出し側で free() 不要
// ============================================================
static inline float LanczosKernel2(float x) {
	if (x == 0.0f) return 1.0f;
	if (fabsf(x) >= 2.0f) return 0.0f;
	float pix = (float)M_PI * x;
	return (sinf(pix) / pix) * (sinf(pix * 0.5f) / (pix * 0.5f));
}

void ResampleUp(void* srcData, int srcLen, void** dstData, int* dstLen,
	int srcRate, int dstRate, int channels, int bitDepth) {
	*dstData = NULL;
	*dstLen = 0;
	if (!srcData || srcLen <= 0 || srcRate <= 0 || dstRate <= 0 || channels <= 0 || bitDepth <= 0)
		return;
	int bytesPerSample = bitDepth / 8;
	int frameBytes = channels * bytesPerSample;
	if (frameBytes <= 0 || srcLen < frameBytes)
		return;
	int srcSamples = srcLen / frameBytes;
	if (srcSamples <= 0)
		return;
	int dstSamples = (int)((double)srcSamples * dstRate / srcRate + 0.5);
	if (dstSamples <= 0)
		return;

	int outBytes = dstSamples * frameBytes;
	ResampleWorkPool* pool = &g_rsPool[g_eqCur];
	unsigned char* pDst = (unsigned char*)EnsureResampleBuf((void**)&pool->pcmBuf, &pool->pcmCap, outBytes);
	float* srcFloat = (float*)EnsureResampleBuf((void**)&pool->srcFloat, &pool->srcCap, srcSamples * channels * (int)sizeof(float));
	float* dstFloat = (float*)EnsureResampleBuf((void**)&pool->dstFloat, &pool->dstCap, dstSamples * channels * (int)sizeof(float));
	if (!pDst || !srcFloat || !dstFloat)
		return;

	*dstData = pDst;
	*dstLen = outBytes;
	unsigned char* pSrc = (unsigned char*)srcData;

	// 整数 PCM → 浮動小数点 [-1.0, 1.0]
	const int totalSrc = srcSamples * channels;
	if (bitDepth == 8) {
		for (int i = 0; i < totalSrc; ++i)
			srcFloat[i] = ((float)pSrc[i] - 128.0f) / 128.0f;
	}
	else if (bitDepth == 16) {
		short* p = (short*)srcData;
		for (int i = 0; i < totalSrc; ++i) srcFloat[i] = p[i] * (1.0f / 32768.0f);
	}
	else if (bitDepth == 24) {
		for (int i = 0; i < totalSrc; ++i) {
			int o = i * 3;
			uint32_t u = (uint32_t)pSrc[o] | ((uint32_t)pSrc[o + 1] << 8) | ((uint32_t)pSrc[o + 2] << 16);
			int32_t s = (u & 0x800000u) ? (int32_t)(u | 0xFF000000u) : (int32_t)u;
			srcFloat[i] = (float)s * (1.0f / 8388608.0f);
		}
	}
	else if (bitDepth == 32) {
		int* p = (int*)srcData;
		for (int i = 0; i < totalSrc; ++i) srcFloat[i] = p[i] * (1.0f / 2147483648.0f);
	}

	// Lanczos-2 正規化補間 (重み和で正規化してゲインリップルを除去)
	double ratio = (double)dstRate / srcRate;
	for (int i = 0; i < dstSamples; ++i) {
		double srcPos = (double)i / ratio;
		int    srcInt = (int)srcPos;
		float  frac   = (float)(srcPos - srcInt);
		float  w[5];
		float  wSum = 0.0f;
		for (int j = -2; j <= 2; ++j) {
			int idx = srcInt + j;
			float kj = (idx >= 0 && idx < srcSamples) ? LanczosKernel2(frac - (float)j) : 0.0f;
			w[j + 2] = kj;
			wSum += kj;
		}
		float invW = (fabsf(wSum) > 1e-6f) ? (1.0f / wSum) : 1.0f;
		for (int ch = 0; ch < channels; ++ch) {
			float sum = 0.0f;
			for (int j = -2; j <= 2; ++j) {
				int idx = srcInt + j;
				if (idx >= 0 && idx < srcSamples)
					sum += srcFloat[idx * channels + ch] * w[j + 2];
			}
			dstFloat[i * channels + ch] = sum * invW;
		}
	}

	// 浮動小数点 → 整数 PCM
	const int totalDst = dstSamples * channels;
	if (bitDepth == 8) {
		for (int i = 0; i < totalDst; ++i) {
			float s = ClampFloat(dstFloat[i], -1.0f, 1.0f);
			pDst[i] = (unsigned char)(s * 127.0f + 128.0f);
		}
	}
	else if (bitDepth == 16) {
		short* p = (short*)pDst;
		for (int i = 0; i < totalDst; ++i) {
			float s = ClampFloat(dstFloat[i], -1.0f, 1.0f);
			int32_t v = (int32_t)roundf(s * 32768.0f);
			if (v > 32767) v = 32767; if (v < -32768) v = -32768;
			p[i] = (short)v;
		}
	}
	else if (bitDepth == 24) {
		for (int i = 0; i < totalDst; ++i) {
			float s = ClampFloat(dstFloat[i], -1.0f, 1.0f);
			int32_t v = (int32_t)roundf(s * 8388608.0f);
			if (v > 8388607) v = 8388607; if (v < -8388608) v = -8388608;
			int o = i * 3;
			pDst[o] = v & 0xFF;
			pDst[o + 1] = (v >> 8) & 0xFF;
			pDst[o + 2] = (v >> 16) & 0xFF;
		}
	}
	else if (bitDepth == 32) {
		int* p = (int*)pDst;
		for (int i = 0; i < totalDst; ++i) {
			float s = ClampFloat(dstFloat[i], -1.0f, 1.0f);
			p[i] = (int)(s * 2147483647.0f);
		}
	}
}

// 簡易ローパスフィルタ (3点移動平均: ダウンサンプル時のエイリアシング抑制)
static void ApplyFastLPF(float* data, int samples, int channels, float /*cutoff*/) {
	for (int ch = 0; ch < channels; ++ch) {
		float prev = data[ch];
		for (int i = 1; i < samples - 1; ++i) {
			int   idx = i * channels + ch;
			float curr = data[idx], next = data[idx + channels];
			data[idx] = (prev + curr + next) * 0.33333334f;
			prev = curr;
		}
	}
}

// ============================================================
// ResampleDown() - Lanczos-2 ダウンサンプリング (再利用プール使用・malloc/freeゼロ)
// ============================================================
void ResampleDown(void* srcData, int srcLen, void* dstData, int dstLen,
	int srcRate, int dstRate, int channels, int bitDepth) {
	if (!srcData || !dstData || srcLen <= 0 || dstLen <= 0 || srcRate <= 0 || dstRate <= 0 || channels <= 0 || bitDepth <= 0)
		return;
	int bytesPerSample = bitDepth / 8;
	int frameBytes = channels * bytesPerSample;
	if (frameBytes <= 0 || srcLen < frameBytes || dstLen < frameBytes)
		return;
	int srcSamples = srcLen / frameBytes;
	int dstSamples = dstLen / frameBytes;
	if (srcSamples <= 0 || dstSamples <= 0)
		return;

	ResampleWorkPool* pool = &g_rsPool[g_eqCur];
	float* srcFloat = (float*)EnsureResampleBuf((void**)&pool->srcFloat, &pool->srcCap, srcSamples * channels * (int)sizeof(float));
	float* dstFloat = (float*)EnsureResampleBuf((void**)&pool->dstFloat, &pool->dstCap, dstSamples * channels * (int)sizeof(float));
	if (!srcFloat || !dstFloat)
		return;

	unsigned char* pSrc = (unsigned char*)srcData;
	const int totalSrc = srcSamples * channels;
	if (bitDepth == 8) {
		for (int i = 0; i < totalSrc; ++i)
			srcFloat[i] = ((float)pSrc[i] - 128.0f) / 128.0f;
	}
	else if (bitDepth == 16) {
		short* p = (short*)srcData;
		for (int i = 0; i < totalSrc; ++i) srcFloat[i] = p[i] * (1.0f / 32768.0f);
	}
	else if (bitDepth == 24) {
		for (int i = 0; i < totalSrc; ++i) {
			int o = i * 3;
			uint32_t u = (uint32_t)pSrc[o] | ((uint32_t)pSrc[o + 1] << 8) | ((uint32_t)pSrc[o + 2] << 16);
			int32_t s = (u & 0x800000u) ? (int32_t)(u | 0xFF000000u) : (int32_t)u;
			srcFloat[i] = (float)s * (1.0f / 8388608.0f);
		}
	}
	else if (bitDepth == 32) {
		int* p = (int*)srcData;
		for (int i = 0; i < totalSrc; ++i) srcFloat[i] = p[i] * (1.0f / 2147483648.0f);
	}

	float cutoff = (float)dstRate / (float)srcRate;
	if (cutoff < 0.9f) ApplyFastLPF(srcFloat, srcSamples, channels, cutoff);

	double ratio = (double)dstRate / srcRate;
	for (int i = 0; i < dstSamples; ++i) {
		double srcPos = (double)i / ratio;
		int    srcInt = (int)srcPos;
		float  frac   = (float)(srcPos - srcInt);
		float  w[5];
		float  wSum = 0.0f;
		for (int j = -2; j <= 2; ++j) {
			int idx = srcInt + j;
			float kj = (idx >= 0 && idx < srcSamples) ? LanczosKernel2(frac - (float)j) : 0.0f;
			w[j + 2] = kj;
			wSum += kj;
		}
		float invW = (fabsf(wSum) > 1e-6f) ? (1.0f / wSum) : 1.0f;
		for (int ch = 0; ch < channels; ++ch) {
			float sum = 0.0f;
			for (int j = -2; j <= 2; ++j) {
				int idx = srcInt + j;
				if (idx >= 0 && idx < srcSamples)
					sum += srcFloat[idx * channels + ch] * w[j + 2];
			}
			dstFloat[i * channels + ch] = sum * invW;
		}
	}

	unsigned char* pDst = (unsigned char*)dstData;
	const int totalDst = dstSamples * channels;
	if (bitDepth == 8) {
		for (int i = 0; i < totalDst; ++i) {
			float s = ClampFloat(dstFloat[i], -1.0f, 1.0f);
			pDst[i] = (unsigned char)(s * 127.0f + 128.0f);
		}
	}
	else if (bitDepth == 16) {
		short* p = (short*)dstData;
		for (int i = 0; i < totalDst; ++i) {
			float s = ClampFloat(dstFloat[i], -1.0f, 1.0f);
			int32_t v = (int32_t)roundf(s * 32768.0f);
			if (v > 32767) v = 32767; if (v < -32768) v = -32768;
			p[i] = (short)v;
		}
	}
	else if (bitDepth == 24) {
		for (int i = 0; i < totalDst; ++i) {
			float s = ClampFloat(dstFloat[i], -1.0f, 1.0f);
			int32_t v = (int32_t)roundf(s * 8388608.0f);
			if (v > 8388607) v = 8388607; if (v < -8388608) v = -8388608;
			int o = i * 3;
			pDst[o] = v & 0xFF; pDst[o + 1] = (v >> 8) & 0xFF; pDst[o + 2] = (v >> 16) & 0xFF;
		}
	}
	else if (bitDepth == 32) {
		int* p = (int*)dstData;
		for (int i = 0; i < totalDst; ++i) {
			float s = ClampFloat(dstFloat[i], -1.0f, 1.0f);
			p[i] = (int)(s * 2147483647.0f);
		}
	}
}


// ============================================================
// equaliser() - メイン処理関数 完全版
//
// 呼び出し規約:
//   data  : 処理対象 PCM バッファ (インプレース処理)
//   len   : バッファバイト長
//   reset : 0=通常, 1=強制初期化, 2=EQプリセット再読み込みのみ
// ============================================================
static void equaliserBankUnlocked(void* data, int len, BOOL reset);

void equaliserResetBank(int bank) {
	if (bank < 0 || bank >= EQ_BANKS) bank = 0;
	EqSrwGuard lk(&g_eqLock);
	int rate = wavbit_sample_Hz;
	if (rate < 8000) rate = 44100;
	InitEngine(rate, bank);
}

void equaliserBank(int bank, void* data, int len, BOOL reset,
	int bitsOverride, int chOverride, int rateOverride) {
	if (bank < 0 || bank >= EQ_BANKS) bank = 0;
	EqSrwGuard lk(&g_eqLock);
	const int prevBank = g_eqCur;
	g_eqCur = bank;
	const int oldDepth = wavsam_depth;
	const int oldCh = wavchannel;
	const int oldRate = wavbit_sample_Hz;
	if (bitsOverride > 0) wavsam_depth = bitsOverride;
	if (chOverride > 0) wavchannel = chOverride;
	if (rateOverride > 0) wavbit_sample_Hz = rateOverride;
	equaliserBankUnlocked(data, len, reset);
	wavsam_depth = oldDepth;
	wavchannel = oldCh;
	wavbit_sample_Hz = oldRate;
	g_eqCur = prevBank;
}

void equaliserBank(int bank, void* data, int len, BOOL reset) {
	equaliserBank(bank, data, len, reset, 0, 0, 0);
}

void equaliser(void* data, int len, BOOL reset) {
	equaliserBank(XfDecSlot(), data, len, reset);
}

static void equaliserBankUnlocked(void* data, int len, BOOL reset) {

	// reset==2: EQプリセット値を savedata.eq に再ロードして即返す
	if (reset == 2) {
		int currentEqPre = savedata.eqsoundeq;
		if (currentEqPre >= 0 && currentEqPre < EQ_PRESET_COUNT && currentEqPre != 9) {
			memcpy(savedata.eq, EQ_PRESETS[currentEqPre], sizeof(int) * 15);
			g_lastEqPreset[g_eqCur] = currentEqPre;
		}
		return;
	}

	if (!data || len <= 0)
		return;

	// ========================================
	// リサンプリング: 32000Hz 未満の低レート音源のみ 44100Hz へ一時変換
	// (g_rsPool[g_eqCur] を再利用するため再生中の malloc/free ゼロ)
	// ========================================
	int   originalRate = wavbit_sample_Hz;
	int   originalLen = len;
	void* processData = data;
	int   processLen = len;
	void* tempBuffer = NULL;
	const int kEqNativeMinRate = 32000;
	BOOL  needsResampling = (originalRate > 0 && originalRate < kEqNativeMinRate);

	wavbitbackup = (originalRate >= 8000) ? originalRate : 44100;

	if (needsResampling) {
		ResampleUp(data, len, &tempBuffer, &processLen, originalRate, 44100, wavchannel, wavsam_depth);
		if (!tempBuffer || processLen <= 0)
			return;
		processData = tempBuffer;
		wavbitbackup = 44100;
	}

	{
		const int bpfEq = (wavsam_depth / 8) * wavchannel;
		if (bpfEq <= 0 || processLen < bpfEq)
			return;
	}

	// ========================================
	// エンジン初期化・パラメータ取り込み
	// ========================================
	BOOL forceUpdate = FALSE;
	if (reset == 1 || !g_initialized[g_eqCur] || g_lastRate[g_eqCur] != wavbitbackup) {
		InitEngine(wavbitbackup, g_eqCur);
		forceUpdate = TRUE;
	}

	int currentEqPre = savedata.eqsoundeq;
	int currentEnvPre = savedata.eqsoundenv;
	int effectAmount = savedata.eqsoundeffect;

	if (currentEnvPre < 0 || currentEnvPre >= ENV_PRESET_COUNT) currentEnvPre = 0;
	if (effectAmount < 0)   effectAmount = 0;
	if (effectAmount > 100) effectAmount = 100;

	// サラウンドは EQ / リミッター前に適用（後段で音割れを防止）
	ApplySurroundProcess(processData, processLen, wavbitbackup, wavsam_depth, wavchannel);

	int masterVolume = (int)ClampFloat((float)savedata.eq[15], 0.0f, 200.0f);
	int clarity      = (int)ClampFloat((float)savedata.eq[16], 0.0f, 200.0f);
	int balance      = (int)ClampFloat((float)savedata.eq[17], 0.0f, 200.0f);
	int density      = (int)ClampFloat((float)savedata.eq[18], 0.0f, 200.0f);
	int spatial      = (int)ClampFloat((float)savedata.eq[19], 0.0f, 200.0f);

	// 追加エフェクトパラメータ (0=オフ / 1-100=モードA / 101-200=モードB)
	g_eqReverb = (int)ClampFloat((float)savedata.eq_reverb, 0.0f, 200.0f);
	g_eqChorus = (int)ClampFloat((float)savedata.eq_chorus, 0.0f, 200.0f);
	g_eqDelay  = (int)ClampFloat((float)savedata.eq_delay,  0.0f, 200.0f);
	savedata.eq_reverb = g_eqReverb;
	savedata.eq_chorus = g_eqChorus;
	savedata.eq_delay  = g_eqDelay;

	// EQプリセット切り替え検出 (バイパス判定より先に同期して切替直後の無反応を防ぐ)
	if (currentEqPre != g_lastEqPreset[g_eqCur]) {
		if (currentEqPre >= 0 && currentEqPre < EQ_PRESET_COUNT && currentEqPre != 9)
			memcpy(savedata.eq, EQ_PRESETS[currentEqPre], sizeof(int) * 15);
		g_lastEqPreset[g_eqCur] = currentEqPre;
		forceUpdate = TRUE;
	}

	// DJパッド有効判定
	const BOOL djActiveCheck = (
		savedata.mpDjEqLow != 100 || savedata.mpDjEqMid != 100 ||
		savedata.mpDjEqHigh != 100 || savedata.mpDjFilter != 100 ||
		(savedata.mpDjEqKill & 7) != 0);

	// ============================================================
	// 完全バイパス判定 (全EQフラット・全トーン中立・環境0・FX/DJオフ)
	// ============================================================
	if ((currentEnvPre == 0 || effectAmount == 0) &&
		masterVolume == 100 && clarity == 100 &&
		balance == 100 && density == 100 && spatial == 100 &&
		g_eqReverb == 0 && g_eqChorus == 0 && g_eqDelay == 0 &&
		savedata.pro_ms_width == 100 && !savedata.pro_ms_mono &&
		savedata.mpVocalCenter == 100 &&
		savedata.surround <= 0 && !djActiveCheck)
	{
		BOOL allFlat = TRUE;
		for (int i = 0; i < EQ_BANDS; ++i) {
			if (savedata.eq[i] != 100) { allFlat = FALSE; break; }
		}
		if (allFlat) {
			ProAudio_FeedMetersFromInterleaved(
				needsResampling ? processData : data,
				needsResampling ? processLen : originalLen,
				needsResampling ? 44100 : originalRate,
				wavsam_depth, wavchannel);
			const float extBoostGain = GetExternalBoostGain();
			if (extBoostGain <= 1.0001f) {
				EqTailOut(data, originalLen, originalRate);
				return;
			}
			const int bytesPerSample = wavsam_depth / 8;
			const int numSamples = processLen / (bytesPerSample * wavchannel);
			ApplyExternalBoostOnlyToBuffer(
				(unsigned char*)processData, numSamples, wavchannel, wavsam_depth, bytesPerSample);
			if (needsResampling)
				ResampleDown(processData, processLen, data, originalLen, 44100, originalRate, wavchannel, wavsam_depth);
			EqTailOut(data, originalLen, originalRate);
			return;
		}
	}

	// ============================================================
	// EQ / トーンフィルタ係数更新 (変更時のみ再計算)
	// ============================================================
	BOOL eqChanged = forceUpdate;
	if (!eqChanged) {
		for (int i = 0; i < EQ_BANDS; ++i) {
			if (savedata.eq[i] != g_lastEqValues[g_eqCur][i]) { eqChanged = TRUE; break; }
		}
	}

	BOOL extendedChanged = forceUpdate;
	if (!extendedChanged) {
		if (masterVolume != g_lastExtendedParams[g_eqCur][0] || clarity != g_lastExtendedParams[g_eqCur][1] ||
			balance != g_lastExtendedParams[g_eqCur][2] || density != g_lastExtendedParams[g_eqCur][3] ||
			spatial != g_lastExtendedParams[g_eqCur][4]) {
			extendedChanged = TRUE;
		}
	}

	if (eqChanged) {
		// [FIX] 旧コードは g_lastEqValues (bank 0固定) へコピーしていたバグを修正
		memcpy(g_lastEqValues[g_eqCur], savedata.eq, sizeof(int) * EQ_BANDS);
		UpdateGraphicEq15(g_channels[g_eqCur], MAX_CH, savedata.eq, wavbitbackup);
	}

	if (extendedChanged) {
		g_lastExtendedParams[g_eqCur][0] = masterVolume;
		g_lastExtendedParams[g_eqCur][1] = clarity;
		g_lastExtendedParams[g_eqCur][2] = balance;
		g_lastExtendedParams[g_eqCur][3] = density;
		g_lastExtendedParams[g_eqCur][4] = spatial;

		// 鮮明さ (Clarity): 3.8kHz プレゼンス (±8.0dB) + 10kHz エアハイシェルフ (±5.0dB)
		double clarityNorm = ((double)clarity - 100.0) / 100.0;
		double clarityPeakDb = clarityNorm * 8.0;
		double clarityAirDb  = clarityNorm * 5.0;

		// バランス (Balance): 1000Hz 支点の対称チルトEQ (低域320Hz / 高域3200Hz、±8.5dB)
		double balanceDb = ((double)balance - 100.0) / 100.0 * 8.5;

		// 密度 (Density): 220Hz 中低域の厚み (±5.5dB) + 700Hz 充実感 (±4.5dB)
		double densityNorm = ((double)density - 100.0) / 100.0;
		double densityLowDb = densityNorm * 5.5;
		double densityMidDb = densityNorm * 4.5;

		for (int ch = 0; ch < MAX_CH; ++ch) {
			ChannelState* cs = &g_channels[g_eqCur][ch];
			CalcPeakingEQDb(&cs->clarityPeak, 3800.0, 1.35, clarityPeakDb, wavbitbackup);
			CalcShelvingEQDb(&cs->clarityAir, 1, 10000.0, clarityAirDb, wavbitbackup);
			CalcShelvingEQDb(&cs->bassBalanceFilter, 0, 320.0, -balanceDb, wavbitbackup);
			CalcShelvingEQDb(&cs->trebleBalanceFilter, 1, 3200.0, balanceDb, wavbitbackup);
			CalcPeakingEQDb(&cs->densityLow, 220.0, 1.05, densityLowDb, wavbitbackup);
			CalcPeakingEQDb(&cs->densityMid, 700.0, 1.15, densityMidDb, wavbitbackup);
		}
	}

	// ============================================================
	// 100環境 立体物理音響エンジンの係数更新 (変更時のみ再計算)
	// ============================================================
	if (currentEnvPre != g_lastEnvPreset[g_eqCur] ||
		effectAmount != g_lastEffectAmount[g_eqCur] ||
		forceUpdate)
	{
		EnvEngineConfigure(&g_envEngine[g_eqCur], currentEnvPre, effectAmount, wavbitbackup);
		g_lastEnvPreset[g_eqCur] = currentEnvPre;
		g_lastEffectAmount[g_eqCur] = effectAmount;
	}

	const int bytesPerSample = wavsam_depth / 8;
	const int numSamples = processLen / (bytesPerSample * wavchannel);
	unsigned char* pRaw = (unsigned char*)processData;

	// マスター音量 (eq[15]: 0→0.0倍, 100→1.0倍, 200→2.0倍)
	// ※ 旧 eqHeadroomGain (-16〜-40dB 一律減衰) は完全撤去！
	//   他帯域や全体音量を一切落とさず、LR4 3バンド保護＋真のルックアヘッドリミッターで音割れを防ぐ。
	const float masterGain = (masterVolume <= 100)
		? ((float)masterVolume / 100.0f)
		: (1.0f + ((float)(masterVolume - 100) / 100.0f));
	const float densityWarmAmt = (density > 100) ? (((float)(density - 100) / 100.0f) * 0.14f) : 0.0f;
	// 立体感 (eq[19]: 0→0.05, 100→1.00, 200→2.20)
	const float spatialWidth = (spatial <= 100)
		? ClampFloat(0.05f + 0.95f * ((float)spatial / 100.0f), 0.05f, 1.0f)
		: ClampFloat(1.0f + 1.20f * ((float)(spatial - 100) / 100.0f), 1.0f, 2.20f);
	const BOOL envActive = (currentEnvPre > 0 && effectAmount > 0);
	const float extBoostGain = GetExternalBoostGain();

	// ============================================================
	// 多ch (5.1 / 7.1) 処理経路
	// 各chに倍精度15バンドEQ + トーン + LR4 3バンド独立ヘッドルーム保護を適用。
	// 環境音響ON時は FL/FR へ立体環境エンジンを適用し、リア/サイドへも自然な空間ウェットを加算。
	// ============================================================
	if (wavchannel > 2) {
		const int nCh = (wavchannel > MAX_CH) ? MAX_CH : wavchannel;
		float* audio = (numSamples > 0 && nCh > 0 && numSamples <= (INT_MAX / nCh))
			? EqCeilAlloc(&g_ceilAudio, &g_ceilAudioCap, numSamples * nCh) : NULL;
		EnsureEqSampleBuffers(g_eqCur);
		float* tmpL = g_eqLeftSamples[g_eqCur];
		float* tmpR = g_eqRightSamples[g_eqCur];

		if (audio) {
			// 1. PCM → float + EQ / トーン処理
			for (int i = 0; i < numSamples; ++i) {
				for (int ch = 0; ch < nCh; ++ch) {
					const int offset = (i * wavchannel + ch) * bytesPerSample;
					double sig = (double)(PcmSampleToFloat(pRaw, offset, wavsam_depth) * masterGain);
					ChannelState* cs = &g_channels[g_eqCur][ch];
					for (int b = 0; b < EQ_BANDS; ++b)
						sig = ProcessBiquadD(&cs->eqFilters[b], sig);
					sig = ProcessBiquadD(&cs->clarityPeak, sig);
					sig = ProcessBiquadD(&cs->clarityAir, sig);
					sig = ProcessBiquadD(&cs->bassBalanceFilter, sig);
					sig = ProcessBiquadD(&cs->trebleBalanceFilter, sig);
					sig = ProcessBiquadD(&cs->densityLow, sig);
					sig = ProcessBiquadD(&cs->densityMid, sig);
					float out = (float)sig;
					if (densityWarmAmt > 0.001f) {
						float sat = tanhf(out * 1.4f) / 1.4f;
						out = out * (1.0f - densityWarmAmt) + sat * densityWarmAmt;
					}
					audio[(size_t)i * (size_t)nCh + (size_t)ch] = out;
				}
			}

			// 2. チャンネル別 LR4 3バンド保護 (他帯域・全体音量を落とさず安全化)
			if (tmpL) {
				for (int ch = 0; ch < nCh; ++ch) {
					for (int start = 0; start < numSamples; start += EQ_SAMPLE_CAP) {
						int lenC = numSamples - start;
						if (lenC > EQ_SAMPLE_CAP) lenC = EQ_SAMPLE_CAP;
						for (int k = 0; k < lenC; ++k)
							tmpL[k] = audio[(size_t)(start + k) * (size_t)nCh + (size_t)ch];
						MultibandProcessMonoChannel(&g_mbProt[g_eqCur], ch, tmpL, lenC);
						for (int k = 0; k < lenC; ++k)
							audio[(size_t)(start + k) * (size_t)nCh + (size_t)ch] = tmpL[k];
					}
				}
			}

			// 3. 環境音響ON時は FL/FR へ立体物理環境エンジンを適用し、リア/サイドへも空間残響を配分
			if (envActive && tmpL && tmpR) {
				for (int start = 0; start < numSamples; start += EQ_SAMPLE_CAP) {
					int lenC = numSamples - start;
					if (lenC > EQ_SAMPLE_CAP) lenC = EQ_SAMPLE_CAP;
					for (int k = 0; k < lenC; ++k) {
						tmpL[k] = audio[(size_t)(start + k) * (size_t)nCh + 0];
						tmpR[k] = audio[(size_t)(start + k) * (size_t)nCh + 1];
					}
					EnvEngineProcessStereo(&g_envEngine[g_eqCur], tmpL, tmpR, lenC, spatialWidth);
					for (int k = 0; k < lenC; ++k) {
						float origL = audio[(size_t)(start + k) * (size_t)nCh + 0];
						float origR = audio[(size_t)(start + k) * (size_t)nCh + 1];
						float wetAddL = (tmpL[k] - origL) * 0.42f;
						float wetAddR = (tmpR[k] - origR) * 0.42f;
						audio[(size_t)(start + k) * (size_t)nCh + 0] = tmpL[k];
						audio[(size_t)(start + k) * (size_t)nCh + 1] = tmpR[k];
						// BL(4)/BR(5) & SL(6)/SR(7) に自然な残響成分を加算 (FC(2)/LFE(3)は明瞭さ保持)
						if (nCh >= 6) {
							audio[(size_t)(start + k) * (size_t)nCh + 4] += wetAddL;
							audio[(size_t)(start + k) * (size_t)nCh + 5] += wetAddR;
						}
						if (nCh >= 8) {
							audio[(size_t)(start + k) * (size_t)nCh + 6] += wetAddL * 0.85f;
							audio[(size_t)(start + k) * (size_t)nCh + 7] += wetAddR * 0.85f;
						}
					}
				}
			}

			// 4. 外部ブースト + 真のルックアヘッド・トゥルーピーク・リミッター
			if (extBoostGain != 1.0f) {
				const size_t tot = (size_t)numSamples * (size_t)nCh;
				for (size_t k = 0; k < tot; ++k) audio[k] *= extBoostGain;
			}
			EqCeilingApplyInterleaved(audio, nCh, numSamples, wavbitbackup);

			// 5. PCM 書き戻し
			for (int i = 0; i < numSamples; ++i) {
				for (int ch = 0; ch < nCh; ++ch) {
					const int offset = (i * wavchannel + ch) * bytesPerSample;
					FloatToPcmSample(pRaw, offset, wavsam_depth, audio[(size_t)i * (size_t)nCh + (size_t)ch]);
				}
			}
		}

		if (needsResampling)
			ResampleDown(processData, processLen, data, originalLen, 44100, originalRate, wavchannel, wavsam_depth);
		EqTailOut(needsResampling ? data : processData, needsResampling ? originalLen : processLen, originalRate);
		return;
	}

	// ============================================================
	// ステレオ / モノラル処理経路 (チャンク分割で任意サイズ安全対応)
	// ============================================================
	EnsureEqSampleBuffers(g_eqCur);
	float* leftSamples = g_eqLeftSamples[g_eqCur];
	float* rightSamples = g_eqRightSamples[g_eqCur];
	if (!leftSamples || !rightSamples) return;

	// DJパッド用 倍精度Biquad状態 (ステレオ)
	static BiquadD s_djLow[2], s_djMid[2], s_djHigh[2], s_djFilt[2];
	static int s_djLowV = -999, s_djMidV = -999, s_djHighV = -999, s_djFiltV = -999, s_djKillV = -999, s_djRate = -1;
	{
		int lowV  = (int)ClampFloat((float)savedata.mpDjEqLow,  0.0f, 200.0f);
		int midV  = (int)ClampFloat((float)savedata.mpDjEqMid,  0.0f, 200.0f);
		int highV = (int)ClampFloat((float)savedata.mpDjEqHigh, 0.0f, 200.0f);
		int filtV = (int)ClampFloat((float)savedata.mpDjFilter, 0.0f, 200.0f);
		int killV = savedata.mpDjEqKill & 7;
		if (lowV != s_djLowV || midV != s_djMidV || highV != s_djHighV ||
			filtV != s_djFiltV || killV != s_djKillV || wavbitbackup != s_djRate) {
			s_djLowV = lowV; s_djMidV = midV; s_djHighV = highV;
			s_djFiltV = filtV; s_djKillV = killV; s_djRate = wavbitbackup;
			for (int ch = 0; ch < 2; ++ch) {
				if (killV & 1)
					CalcShelvingEQDb(&s_djLow[ch], 0, 250.0, -48.0, wavbitbackup);
				else
					CalcPeakingEQDb(&s_djLow[ch], 100.0, 0.7071, ((double)lowV - 100.0) * 0.12, wavbitbackup);

				if (killV & 2)
					CalcPeakingEQDb(&s_djMid[ch], 1000.0, 0.9, -24.0, wavbitbackup);
				else
					CalcPeakingEQDb(&s_djMid[ch], 1000.0, 0.7071, ((double)midV - 100.0) * 0.12, wavbitbackup);

				if (killV & 4)
					CalcShelvingEQDb(&s_djHigh[ch], 1, 4000.0, -48.0, wavbitbackup);
				else
					CalcPeakingEQDb(&s_djHigh[ch], 8000.0, 0.7071, ((double)highV - 100.0) * 0.12, wavbitbackup);

				if (filtV < 98) {
					double freq = ClampDouble(200.0 * pow(100.0, (double)filtV / 100.0), 80.0, 18000.0);
					CalcBiquadFilter(&s_djFilt[ch], 0, freq, 0.7071, wavbitbackup);
				}
				else if (filtV > 102) {
					double freq = ClampDouble(30.0 * pow(300.0, (double)(filtV - 100) / 100.0), 30.0, 10000.0);
					CalcBiquadFilter(&s_djFilt[ch], 1, freq, 0.7071, wavbitbackup);
				}
				else {
					SetBiquadBypass(&s_djFilt[ch]);
				}
			}
		}
	}
	const int djKillV = savedata.mpDjEqKill & 7;

	for (int chunkStart = 0; chunkStart < numSamples; chunkStart += EQ_SAMPLE_CAP) {
		int chunkLen = numSamples - chunkStart;
		if (chunkLen > EQ_SAMPLE_CAP) chunkLen = EQ_SAMPLE_CAP;

		// ---------------------------------------------------------
		// ステージ 1: PCM読込 + マスター音量 + 倍精度15バンドEQ + トーン制御
		// ---------------------------------------------------------
		for (int i = 0; i < chunkLen; ++i) {
			const int frameIdx = chunkStart + i;
			for (int ch = 0; ch < wavchannel; ++ch) {
				const int offset = (frameIdx * wavchannel + ch) * bytesPerSample;
				double sig = (double)(PcmSampleToFloat(pRaw, offset, wavsam_depth) * masterGain);
				ChannelState* cs = &g_channels[g_eqCur][ch];

				for (int b = 0; b < EQ_BANDS; ++b)
					sig = ProcessBiquadD(&cs->eqFilters[b], sig);
				sig = ProcessBiquadD(&cs->clarityPeak, sig);
				sig = ProcessBiquadD(&cs->clarityAir, sig);
				sig = ProcessBiquadD(&cs->bassBalanceFilter, sig);
				sig = ProcessBiquadD(&cs->trebleBalanceFilter, sig);
				sig = ProcessBiquadD(&cs->densityLow, sig);
				sig = ProcessBiquadD(&cs->densityMid, sig);

				float out = (float)sig;
				if (densityWarmAmt > 0.001f) {
					// 密度ブースト時: ごく滑らかなソフトウォームスで中身の詰まった音に
					float sat = tanhf(out * 1.45f) / 1.45f;
					out = out * (1.0f - densityWarmAmt) + sat * densityWarmAmt;
				}

				if (wavchannel == 2) {
					if (ch == 0) leftSamples[i] = out;
					else         rightSamples[i] = out;
				}
				else {
					leftSamples[i] = out;
					rightSamples[i] = out;
				}
			}
		}

		// ---------------------------------------------------------
		// ステージ 2: 位相整合 LR4 3バンド・ダイナミック・ヘッドルーム保護
		// 低域を+12dB上げても中・高域の音量は 1.000 のまま下がらない！
		// 高域を+12dB上げても低・中域の音量は 1.000 のまま下がらない！
		// 閾値未満の帯域は完全無加工(ゲイン1.000)。
		// ---------------------------------------------------------
		MultibandProcessStereo(&g_mbProt[g_eqCur], leftSamples, rightSamples, chunkLen);

		// ---------------------------------------------------------
		// ステージ 3: 100環境 立体物理音響エンジン (or 環境オフ時は Spatial のみ適用)
		// ---------------------------------------------------------
		if (envActive) {
			EnvEngineProcessStereo(&g_envEngine[g_eqCur], leftSamples, rightSamples, chunkLen, spatialWidth);
		}
		else if (wavchannel == 2 && fabsf(spatialWidth - 1.0f) > 0.01f) {
			// 環境オフ時の立体感(Spatial): 120Hz以下をモノラル保持(Bass Mono Anchor)して位相崩れ防止
			ChannelState* csL = &g_channels[g_eqCur][0];
			ChannelState* csR = &g_channels[g_eqCur][1];
			for (int i = 0; i < chunkLen; ++i) {
				float l = leftSamples[i];
				float r = rightSamples[i];
				float lowL = (float)ProcessBiquadD(&csL->bassMonoLP, (double)l);
				float lowR = (float)ProcessBiquadD(&csR->bassMonoLP, (double)r);
				float hiL = l - lowL;
				float hiR = r - lowR;
				float midHi  = (hiL + hiR) * 0.5f;
				float sideHi = (hiL - hiR) * 0.5f * spatialWidth;
				float midLo  = (lowL + lowR) * 0.5f;
				float sideLo = (lowL - lowR) * 0.5f * ClampFloat(spatialWidth, 0.25f, 1.0f);
				leftSamples[i]  = (midLo + sideLo) + (midHi + sideHi);
				rightSamples[i] = (midLo - sideLo) + (midHi - sideHi);
			}
		}

		// ---------------------------------------------------------
		// ステージ 4: ユーザー追加エフェクト (リバーブ / コーラス / ディレイ)
		// ---------------------------------------------------------
		FxApplyUserEffects(leftSamples, rightSamples, chunkLen, wavbitbackup);

		// ---------------------------------------------------------
		// ステージ 5: ProAudio Mid/Side 幅 / ボーカルセンター / DJパッド
		// ---------------------------------------------------------
		if (wavchannel >= 2 && chunkLen > 0) {
			ProAudio_ApplyMS(leftSamples, rightSamples, chunkLen,
				savedata.pro_ms_width, savedata.pro_ms_mono);
		}
		if (wavchannel >= 2 && chunkLen > 0 && savedata.mpVocalCenter != 100) {
			const float midGain = (float)savedata.mpVocalCenter / 100.0f;
			for (int i = 0; i < chunkLen; ++i) {
				const float mid  = (leftSamples[i] + rightSamples[i]) * 0.5f;
				const float side = (leftSamples[i] - rightSamples[i]) * 0.5f;
				const float midS = mid * midGain;
				leftSamples[i]  = midS + side;
				rightSamples[i] = midS - side;
			}
		}

		if (djActiveCheck && chunkLen > 0) {
			const int chN = (wavchannel >= 2) ? 2 : 1;
			for (int i = 0; i < chunkLen; ++i) {
				double l = (double)leftSamples[i];
				l = ProcessBiquadD(&s_djLow[0], l);
				l = ProcessBiquadD(&s_djMid[0], l);
				if (djKillV & 2) l = ProcessBiquadD(&s_djMid[0], l);
				l = ProcessBiquadD(&s_djHigh[0], l);
				l = ProcessBiquadD(&s_djFilt[0], l);
				leftSamples[i] = (float)l;
				if (chN > 1) {
					double r = (double)rightSamples[i];
					r = ProcessBiquadD(&s_djLow[1], r);
					r = ProcessBiquadD(&s_djMid[1], r);
					if (djKillV & 2) r = ProcessBiquadD(&s_djMid[1], r);
					r = ProcessBiquadD(&s_djHigh[1], r);
					r = ProcessBiquadD(&s_djFilt[1], r);
					rightSamples[i] = (float)r;
				}
				else {
					rightSamples[i] = (float)l;
				}
			}
		}

		// ラウドネス計測 / 位相相関メーター
		if (chunkLen > 0) {
			ProAudio_LoudnessFeed(leftSamples, rightSamples, chunkLen, wavbitbackup);
			if (savedata.pro_corr_meter)
				ProAudio_CorrFeed(leftSamples, rightSamples, chunkLen);
		}

		// ---------------------------------------------------------
		// ステージ 6: 外部音量ブースト + 真のルックアヘッド・トゥルーピークリミッター
		// ---------------------------------------------------------
		if (extBoostGain != 1.0f) {
			for (int i = 0; i < chunkLen; ++i) {
				leftSamples[i]  *= extBoostGain;
				rightSamples[i] *= extBoostGain;
			}
		}
		ApplyLookaheadLimiterStereo(leftSamples, rightSamples, chunkLen, wavbitbackup);

		// ---------------------------------------------------------
		// ステージ 7: float → 整数PCM 書き戻し
		// ---------------------------------------------------------
		for (int i = 0; i < chunkLen; ++i) {
			const int frameIdx = chunkStart + i;
			for (int ch = 0; ch < wavchannel; ++ch) {
				if (ch >= MAX_CH) continue;
				const float finalOut = (ch == 0) ? leftSamples[i] : rightSamples[i];
				const int offset = (frameIdx * wavchannel + ch) * bytesPerSample;
				FloatToPcmSample(pRaw, offset, wavsam_depth, finalOut);
			}
		}
	}

	// リサンプリングしていた場合は元のサンプルレートへ戻す (tempBuffer はプール再利用のため free 不要)
	if (needsResampling) {
		ResampleDown(processData, processLen, data, originalLen, 44100, originalRate, wavchannel, wavsam_depth);
	}

	// クロスフェードイン(曲頭) + テール蓄積(曲末用)
	EqTailOut(needsResampling ? data : processData,
		needsResampling ? originalLen : processLen,
		originalRate);
}


// ============================================================
//  ★ Hyper DSP Equaliser ★  全100環境音響モデル / 全51 EQプリセット
//  音楽解析エンジン (コード検出 / Viterbiメロディ追跡 / キー推定)
// ============================================================

#include <algorithm>
#define _USE_MATH_DEFINES
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef WM_EQ_KEY_UPDATE
#define WM_EQ_KEY_UPDATE (WM_APP + 430)
#endif

#ifndef OUTPUT_BUFFER_SIZE
#define OUTPUT_BUFFER_SIZE 176400
#endif
#ifndef OUTPUT_BUFFER_NUM
#define OUTPUT_BUFFER_NUM 2
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

// EQコード解析: 固定バッファのみ（std::vector/map/deque 禁止・長時間ヒープ断片化防止）
// Goertzel 低域は最大4096点。PCM供給もここに揃える。
static const int EQKEY_PCM_MAX = 4096;
static const int EQKEY_HIST_MAX = 4;
static const int EQKEY_NAME_MAX = 160;

static float  g_noteStrength[108];        // MIDI 0-107 の強度
static float  g_noteStrengthPrev[108];    // 指数平滑用
static double g_goertzelCoeffs[108];      // Goertzel 係数 (各ノート周波数)
static double g_hannWindow4096[4096];   // 低域 Goertzel 用 Hann 窓
static double g_blackmanWindow2048[2048]; // 高域 Goertzel 用 Blackman 窓
static bool   g_analysisInitialized = false;
static double g_analysisSampleRate = 0.0; // 係数計算に使ったサンプルレート
static int    g_noteNyquist = 108;        // このインデックス以降はナイキスト超え

static WCHAR g_currentVowel = L' ';

CString KeyCodeLow, KeyCodeMid, KeyCodeHigh, KeyCodeAll;

void AnalyzeMusicKey(const double* bufferL, const double* bufferR, int sampleCount, int sampleRate);

// Speana が PCM を載せ、専用ワーカーが Goertzel 解析（UI/OnTimer を塞がない）
static CRITICAL_SECTION g_eqKeyCs;
static CRITICAL_SECTION g_keyCodeCs;
static volatile LONG g_eqKeyCsInitState = 0; /* 0=未, 1=初期化中, 2=完了 */
static HANDLE g_eqKeyWake = nullptr;
static HANDLE g_eqKeyThread = nullptr;
static volatile LONG g_eqKeyStop = 0;
static volatile LONG g_eqKeyNeed = 0;
static double g_eqKeyL[EQKEY_PCM_MAX];
static double g_eqKeyR[EQKEY_PCM_MAX];
static double g_eqKeyWorkL[EQKEY_PCM_MAX];
static double g_eqKeyWorkR[EQKEY_PCM_MAX];
static int g_eqKeyCount = 0;
static int g_eqKeyRate = 44100;
static LONG g_eqKeySeq = 0;
static HWND g_eqKeyUiHwnd = nullptr;
static volatile LONG g_eqKeyUiPosted = 0;
static DWORD g_eqKeyUiLastPostTick = 0;
static volatile LONG g_eqKeyUiDirty = 0;

static void EnsureEqKeyCs()
{
	/* Speana(UI) と再生スレッドが同時に来る。二重 InitializeCriticalSection は
	   CS を壊し、Enter 時に ntdll が 0x00000014 へ書いて AV する。 */
	const LONG st = InterlockedCompareExchange(&g_eqKeyCsInitState, 1, 0);
	if (st == 0) {
		InitializeCriticalSection(&g_eqKeyCs);
		InitializeCriticalSection(&g_keyCodeCs);
		InterlockedExchange(&g_eqKeyCsInitState, 2);
		return;
	}
	if (st == 2)
		return;
	while (InterlockedCompareExchange(&g_eqKeyCsInitState, 0, 0) != 2)
		Sleep(0);
}

static void NotifyEqKeyUi()
{
	HWND h = g_eqKeyUiHwnd;
	if (!h || !::IsWindow(h)) return;
	if (InterlockedCompareExchange(&g_eqKeyUiDirty, 0, 0) == 0) return;
	const DWORD now = GetTickCount();
	if (g_eqKeyUiLastPostTick != 0 && (now - g_eqKeyUiLastPostTick) < (DWORD)(EqCodeIntervalMs() / 2))
		return;
	if (InterlockedCompareExchange(&g_eqKeyUiPosted, 1, 0) != 0) {
		if (g_eqKeyUiLastPostTick != 0 && (now - g_eqKeyUiLastPostTick) >= 150u) {
			InterlockedExchange(&g_eqKeyUiPosted, 0);
			InterlockedExchange(&g_eqKeyUiDirty, 1);
			if (InterlockedCompareExchange(&g_eqKeyUiPosted, 1, 0) != 0)
				return;
		}
		else {
			return;
		}
	}
	g_eqKeyUiLastPostTick = now;
	InterlockedExchange(&g_eqKeyUiDirty, 0);
	if (!::PostMessage(h, WM_EQ_KEY_UPDATE, 0, 0)) {
		InterlockedExchange(&g_eqKeyUiPosted, 0);
		InterlockedExchange(&g_eqKeyUiDirty, 1);
	}
}

void RegisterEqKeyUiHwnd(HWND h)
{
	g_eqKeyUiHwnd = h;
	InterlockedExchange(&g_eqKeyUiPosted, 0);
	InterlockedExchange(&g_eqKeyUiDirty, 1);
	g_eqKeyUiLastPostTick = 0;
	NotifyEqKeyUi();
}

void UnregisterEqKeyUiHwnd(HWND h)
{
	if (g_eqKeyUiHwnd == h)
		g_eqKeyUiHwnd = nullptr;
	InterlockedExchange(&g_eqKeyUiPosted, 0);
	InterlockedExchange(&g_eqKeyUiDirty, 0);
}

void AckEqKeyUiNotify()
{
	InterlockedExchange(&g_eqKeyUiPosted, 0);
}

static void SetKeyCodesLocked(const CString& lo, const CString& mid, const CString& hi, const CString& all)
{
	EnsureEqKeyCs();
	EnterCriticalSection(&g_keyCodeCs);
	const bool changed = (KeyCodeLow != lo) || (KeyCodeMid != mid)
		|| (KeyCodeHigh != hi) || (KeyCodeAll != all);
	if (changed) {
		KeyCodeLow = lo;
		KeyCodeMid = mid;
		KeyCodeHigh = hi;
		KeyCodeAll = all;
		InterlockedExchange(&g_eqKeyUiDirty, 1);
	}
	LeaveCriticalSection(&g_keyCodeCs);
	NotifyEqKeyUi();
}

void SnapshotEqKeyCodes(CString& lo, CString& mid, CString& hi, CString& all)
{
	EnsureEqKeyCs();
	EnterCriticalSection(&g_keyCodeCs);
	lo = KeyCodeLow;
	mid = KeyCodeMid;
	hi = KeyCodeHigh;
	all = KeyCodeAll;
	LeaveCriticalSection(&g_keyCodeCs);
}

static DWORD WINAPI EqKeyWorkerEntry(LPVOID)
{
	int workN = 0;
	int workRate = 44100;
	for (;;) {
		if (g_eqKeyWake)
			WaitForSingleObject(g_eqKeyWake, 50);
		if (InterlockedCompareExchange(&g_eqKeyStop, 0, 0) != 0)
			break;
		if (InterlockedExchange(&g_eqKeyNeed, 0) == 0)
			continue;
		EnsureEqKeyCs();
		EnterCriticalSection(&g_eqKeyCs);
		workN = g_eqKeyCount;
		if (workN > EQKEY_PCM_MAX) workN = EQKEY_PCM_MAX;
		if (workN < 0) workN = 0;
		if (workN > 0) {
			memcpy(g_eqKeyWorkL, g_eqKeyL, (size_t)workN * sizeof(double));
			memcpy(g_eqKeyWorkR, g_eqKeyR, (size_t)workN * sizeof(double));
		}
		workRate = g_eqKeyRate;
		LeaveCriticalSection(&g_eqKeyCs);
		if (workN <= 0)
			continue;
		AnalyzeMusicKey(g_eqKeyWorkL, g_eqKeyWorkR, workN, workRate);
	}
	return 0;
}

static void EnsureEqKeyWorker()
{
	EnsureEqKeyCs();
	if (InterlockedCompareExchange(&g_eqKeyCsInitState, 0, 0) != 2)
		return;
	EnterCriticalSection(&g_eqKeyCs);
	if (!g_eqKeyThread) {
		InterlockedExchange(&g_eqKeyStop, 0);
		if (!g_eqKeyWake)
			g_eqKeyWake = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (g_eqKeyWake)
			g_eqKeyThread = CreateThread(nullptr, 0, EqKeyWorkerEntry, nullptr, 0, nullptr);
		if (!g_eqKeyThread && g_eqKeyWake) {
			CloseHandle(g_eqKeyWake);
			g_eqKeyWake = nullptr;
		}
	}
	LeaveCriticalSection(&g_eqKeyCs);
}

void PublishEqKeyPcm(const double* bufferL, const double* bufferR, int sampleCount, int sampleRate)
{
	if (!bufferL || sampleCount <= 0) return;
	int n = sampleCount;
	if (n > EQKEY_PCM_MAX) {
		bufferL += (n - EQKEY_PCM_MAX);
		if (bufferR) bufferR += (n - EQKEY_PCM_MAX);
		n = EQKEY_PCM_MAX;
	}
	EnsureEqKeyCs();
	EnsureEqKeyWorker();
	if (InterlockedCompareExchange(&g_eqKeyCsInitState, 0, 0) != 2)
		return;
	EnterCriticalSection(&g_eqKeyCs);
	memcpy(g_eqKeyL, bufferL, (size_t)n * sizeof(double));
	if (bufferR)
		memcpy(g_eqKeyR, bufferR, (size_t)n * sizeof(double));
	else
		memcpy(g_eqKeyR, bufferL, (size_t)n * sizeof(double));
	g_eqKeyCount = n;
	g_eqKeyRate = (sampleRate > 0) ? sampleRate : 44100;
	++g_eqKeySeq;
	LeaveCriticalSection(&g_eqKeyCs);
	InterlockedExchange(&g_eqKeyNeed, 1);
	if (g_eqKeyWake)
		SetEvent(g_eqKeyWake);
}

void TickEqKeyAnalysis()
{
}

static const WCHAR* NOTE_NAMES[12] = {
	L"C ", L"C#", L"D ", L"D#", L"E ", L"F ",
	L"F#", L"G ", L"G#", L"A ", L"A#", L"B "
};

static void InitializeAnalysis(double sampleRate) {
	if (sampleRate < 8000.0) sampleRate = 44100.0;
	const bool firstInit = !g_analysisInitialized;
	const bool rateChanged = g_analysisInitialized
		&& fabs(sampleRate - g_analysisSampleRate) > 0.5;
	if (!firstInit && !rateChanged) return;

	g_analysisSampleRate = sampleRate;
	g_noteNyquist = 108;
	for (int k = 0; k < 108; ++k) {
		double freq = 440.0 * pow(2.0, ((12 + k) - 69.0) / 12.0);
		if (freq >= sampleRate * 0.47) {
			g_noteNyquist = k;
			for (int j = k; j < 108; ++j) g_goertzelCoeffs[j] = 0.0;
			break;
		}
		g_goertzelCoeffs[k] = 2.0 * cos(2.0 * M_PI * freq / sampleRate);
	}
	for (int n = 0; n < 4096; ++n)
		g_hannWindow4096[n] = 0.5 - 0.5 * cos(2.0 * M_PI * n / 4095.0);
	for (int n = 0; n < 2048; ++n)
		g_blackmanWindow2048[n] = 0.355768 - 0.487396 * cos(2.0 * M_PI * n / 2047.0)
		+ 0.144232 * cos(4.0 * M_PI * n / 2047.0)
		- 0.012604 * cos(6.0 * M_PI * n / 2047.0);

	if (firstInit || rateChanged) {
		memset(g_noteStrength, 0, sizeof(g_noteStrength));
		memset(g_noteStrengthPrev, 0, sizeof(g_noteStrengthPrev));
	}
	g_analysisInitialized = true;
}

static double GoertzelMagnitude(const double* samples, int numSamples, double coefficient,
	const double* window)
{
	if (!samples || numSamples <= 0) return 0.0;
	double s_prev = 0.0, s_prev2 = 0.0;
	for (int n = 0; n < numSamples; ++n) {
		const double x = window ? (samples[n] * window[n]) : samples[n];
		double s = x + coefficient * s_prev - s_prev2;
		s_prev2 = s_prev; s_prev = s;
	}
	double power = s_prev2 * s_prev2 + s_prev * s_prev - coefficient * s_prev * s_prev2;
	return sqrt(power > 0.0 ? power : 0.0) * 2.5 / numSamples;
}

// git と同じ帯域集計。倍音を別帯域へ移さない。
static void AggregateNoteClasses(float* bassClass, float* midClass, float* highClass, float* allClass) {
	for (int i = 0; i < 12; i++) bassClass[i] = midClass[i] = highClass[i] = allClass[i] = 0.0f;
	float octaveMax[9] = { 0 }; int octaveMaxNote[9] = { -1 };
	for (int note = 0; note < 108; note++) {
		int oct = note / 12;
		if (g_noteStrength[note] > octaveMax[oct]) {
			octaveMax[oct] = g_noteStrength[note]; octaveMaxNote[oct] = note;
		}
	}
	for (int note = 0; note < 108; note++) {
		float strength = g_noteStrength[note];
		int pc = note % 12, oct = note / 12;
		if (octaveMaxNote[oct] >= 0 && note != octaveMaxNote[oct]) {
			int iv = (pc - octaveMaxNote[oct] % 12 + 12) % 12;
			float r = (octaveMax[oct] > 0.f) ? strength / octaveMax[oct] : 0.f;
			if (iv == 7 && r < 0.4f)  strength *= 0.4f;
			// 完全4度は基音の低い倍音ではない。弱い4度は5度の第7倍音などで、sus4 に誤る。
			else if (iv == 5 && r < 0.32f) strength *= 0.35f;
			else if (iv == 4 && r < 0.3f)  strength *= 0.6f;
			else if (iv == 2 && r < 0.35f) strength *= 0.3f;
			else if (iv == 9 && r < 0.3f)  strength *= 0.5f;
			else if (iv == 11 && r < 0.25f) strength *= 0.4f;
		}
		// k の周波数は MIDI(k+12)。低 C0–B2、中 C3–B4、高 C5 以上。全は三帯域の和。
		if (note < 36)       bassClass[pc] += strength;
		else if (note < 60)  midClass[pc] += strength;
		else                 highClass[pc] += strength;
		allClass[pc] += strength;
	}
}

// コード照合は EqKeyAnalyze（12 ルート全部をテンプレートに当てる）。
static void EstimateChordRawInto(WCHAR* out, int outCap, float* noteClass, float threshold) {
	EqKeyEstimateChord(noteClass, threshold, nullptr, nullptr, out, outCap);
}

static CString EstimateChordRaw(float* noteClass, float threshold) {
	WCHAR buf[EQKEY_NAME_MAX * 3];
	EstimateChordRawInto(buf, _countof(buf), noteClass, threshold);
	return buf;
}

static CString EstimateOverallRaw(float* b, float* m, float* h, float* a) {
	CString c = EstimateChordRaw(a, 0.03f); if (!c.IsEmpty()) return c;
	c = EstimateChordRaw(b, 0.02f);         if (!c.IsEmpty()) return c;
	return L"";
}

// ヒストリー付きコード推定: 前フレームのコードをスコアに加味して安定性を向上
static WCHAR g_prevChordLow[EQKEY_NAME_MAX] = L"";
static WCHAR g_prevChordMid[EQKEY_NAME_MAX] = L"";
static WCHAR g_prevChordHigh[EQKEY_NAME_MAX] = L"";
static WCHAR g_prevChordAll[EQKEY_NAME_MAX] = L"";
// 直近 HISTORY_SIZE フレームのリング（deque/map/CString 禁止）
static WCHAR g_historyLow[EQKEY_HIST_MAX][EQKEY_NAME_MAX];
static WCHAR g_historyMid[EQKEY_HIST_MAX][EQKEY_NAME_MAX];
static WCHAR g_historyHigh[EQKEY_HIST_MAX][EQKEY_NAME_MAX];
static WCHAR g_historyAll[EQKEY_HIST_MAX][EQKEY_NAME_MAX];
static int g_historyLowN = 0, g_historyMidN = 0, g_historyHighN = 0, g_historyAllN = 0;
static int g_historyLowHead = 0, g_historyMidHead = 0, g_historyHighHead = 0, g_historyAllHead = 0;
const int HISTORY_SIZE = EQKEY_HIST_MAX;  // 直近4フレームで多数決
const float SMOOTHING_FACTOR = 0.3f;  // ノート強度の指数平滑。大きいほど前フレームを残す
static float g_prevRMS = 0.0f, g_peakRMS = 0.0f;
static bool  g_isPlaying = false;
static int   g_silenceFrameCount = 0;

// 無音判定閾値
const float SILENCE_THRESHOLD_ABS = 0.002f;    // 絶対値閾値
const float SILENCE_THRESHOLD_REL = 0.15f;     // ピークRMSに対する相対閾値
const float PLAYING_THRESHOLD = 0.01f;     // 再生中判定閾値
const int   SILENCE_FRAMES_FOR_CLEAR = 10;     // このフレーム数無音でヒストリーをクリア
static int  g_soundFrameCount = 0;

static void EqKeyCopy(WCHAR* d, int cap, const WCHAR* s)
{
	if (!d || cap <= 0) return;
	if (!s) { d[0] = 0; return; }
	int i = 0;
	for (; i < cap - 1 && s[i]; ++i) d[i] = s[i];
	d[i] = 0;
}

static void EqKeyTrimInPlace(WCHAR* s)
{
	if (!s) return;
	WCHAR* b = s;
	while (*b == L' ' || *b == L'\t') ++b;
	if (b != s) {
		WCHAR* d = s;
		while (*b) *d++ = *b++;
		*d = 0;
	}
	int n = 0;
	while (s[n]) ++n;
	while (n > 0 && (s[n - 1] == L' ' || s[n - 1] == L'\t')) s[--n] = 0;
}

static void HistoryClear(WCHAR ring[][EQKEY_NAME_MAX], int& n, int& head)
{
	for (int i = 0; i < EQKEY_HIST_MAX; ++i) ring[i][0] = 0;
	n = 0; head = 0;
}

static void HistoryPush(WCHAR ring[][EQKEY_NAME_MAX], int& n, int& head, const WCHAR* v)
{
	if (n < EQKEY_HIST_MAX) {
		EqKeyCopy(ring[(head + n) % EQKEY_HIST_MAX], EQKEY_NAME_MAX, v);
		++n;
	} else {
		EqKeyCopy(ring[head], EQKEY_NAME_MAX, v);
		head = (head + 1) % EQKEY_HIST_MAX;
	}
}

// RMS計算 (モノラル・ステレオ兼用)
static float CalculateRMS(const double* bL, const double* bR, int count, bool stereo) {
	if (!bL || count <= 0) return 0.0f;
	double sL = 0.0, sR = 0.0;
	for (int i = 0; i < count; i++) sL += bL[i] * bL[i];
	if (stereo && bR) {
		for (int i = 0; i < count; i++) sR += bR[i] * bR[i];
		return (float)sqrt((sL + sR) / (count * 2));
	}
	return (float)sqrt(sL / count);
}

// ヒストリー内で最頻出のコード名を返す (多数決安定化・固定配列)
static void GetMostFrequentInto(WCHAR* out, int outCap, const WCHAR ring[][EQKEY_NAME_MAX], int n, int head) {
	out[0] = 0;
	if (!ring || n <= 0) return;
	WCHAR names[EQKEY_HIST_MAX * 3][EQKEY_NAME_MAX];
	int votes[EQKEY_HIST_MAX * 3];
	int nameCount = 0;
	for (int i = 0; i < n; ++i) {
		const WCHAR* s = ring[(head + i) % EQKEY_HIST_MAX];
		if (!s || !s[0]) continue;
		const WCHAR* p = s;
		while (*p) {
			const WCHAR* comma = wcsstr(p, L", ");
			size_t len = comma ? (size_t)(comma - p) : wcslen(p);
			WCHAR item[EQKEY_NAME_MAX];
			if (len >= EQKEY_NAME_MAX) len = EQKEY_NAME_MAX - 1;
			wcsncpy_s(item, EQKEY_NAME_MAX, p, len);
			item[len] = 0;
			EqKeyTrimInPlace(item);
			if (item[0]) {
				int found = -1;
				for (int k = 0; k < nameCount; ++k) {
					if (wcscmp(names[k], item) == 0) { found = k; break; }
				}
				if (found >= 0) votes[found]++;
				else if (nameCount < EQKEY_HIST_MAX * 3) {
					EqKeyCopy(names[nameCount], EQKEY_NAME_MAX, item);
					votes[nameCount] = 1;
					nameCount++;
				}
			}
			if (!comma) break;
			p = comma + 2;
		}
	}
	if (nameCount <= 0) return;

	for (int i = 0; i < nameCount; ++i) {
		int best = i;
		for (int j = i + 1; j < nameCount; ++j)
			if (votes[j] > votes[best]) best = j;
		if (best != i) {
			WCHAR tn[EQKEY_NAME_MAX];
			EqKeyCopy(tn, EQKEY_NAME_MAX, names[i]);
			EqKeyCopy(names[i], EQKEY_NAME_MAX, names[best]);
			EqKeyCopy(names[best], EQKEY_NAME_MAX, tn);
			int tv = votes[i]; votes[i] = votes[best]; votes[best] = tv;
		}
	}

	EqKeyCopy(out, outCap, names[0]);
	int count = 1;
	const int topVotes = votes[0];
	WCHAR bestRoot[8];
	if (names[0][0] && (names[0][1] == L'#' || names[0][1] == L'b'))
		{ bestRoot[0] = names[0][0]; bestRoot[1] = names[0][1]; bestRoot[2] = 0; }
	else
		{ bestRoot[0] = names[0][0]; bestRoot[1] = 0; }

	for (int i = 1; i < nameCount && count < 3; i++) {
		if (votes[i] < 2) continue;
		if (topVotes > 0 && votes[i] < (topVotes + 1) / 2) continue;
		WCHAR root[8];
		if (names[i][0] && (names[i][1] == L'#' || names[i][1] == L'b'))
			{ root[0] = names[i][0]; root[1] = names[i][1]; root[2] = 0; }
		else
			{ root[0] = names[i][0]; root[1] = 0; }
		if (wcscmp(root, bestRoot) == 0) {
			wcsncat_s(out, outCap, L", ", _TRUNCATE);
			wcsncat_s(out, outCap, names[i], _TRUNCATE);
			count++;
		}
	}
}

static bool IsChordInListW(const WCHAR* chord, const WCHAR* list) {
	if (!list || !list[0] || !chord || !chord[0]) return false;
	const WCHAR* p = list;
	while (*p) {
		const WCHAR* comma = wcsstr(p, L", ");
		size_t len = comma ? (size_t)(comma - p) : wcslen(p);
		if (wcsncmp(p, chord, len) == 0 && chord[len] == 0)
			return true;
		if (!comma) break;
		p = comma + 2;
	}
	return false;
}

// ヒストリー付きコード推定 (単一帯域版)
// prev と一致するコードにボーナスを与え、フレーム間の揺れを抑制
static void EstimateChordRawWithHistoryInto(WCHAR* out, int outCap, float* nc, float threshold, const WCHAR* prev) {
	EqKeyEstimateChord(nc, threshold, prev, nullptr, out, outCap);
}

// ヒストリー付きコード推定 (全帯域統合版)
static void EstimateChordRawWithHistoryIntoAll(WCHAR* out, int outCap, float* b, float* m, float* h, float* a, const WCHAR* prev) {
	// 低域クロマをルートの事前確率にする（ベースがルートであることが多い）。
	EqKeyEstimateChord(a, 0.03f, prev, b, out, outCap);
	if (out[0]) return;
	EqKeyEstimateChord(b, 0.02f, prev, nullptr, out, outCap);
}

// ============================================================
// AnalyzeMusicKey() - 音楽キー・コード・メロディ解析 メイン
//
// 処理フロー:
//   1. RMS計算 → 無音検出 → ヒストリークリア
//   2. Goertzel で各MIDI音の強度を計算 (低域:4096点, 高域:2048点)
//   3. 帯域別ピッチクラスに集約
//   4. コード推定 (低域/中域/高域/全体, ヒストリー付き)
//   5. 中央の声だけ母音（あいうえお）と音名。伴奏のみは出さない
//   6. KeyCodeLow/Mid/High/All に結果を格納
// ============================================================
void AnalyzeMusicKey(const double* bufferL, const double* bufferR, int sampleCount, int sampleRate) {
	InitializeAnalysis((double)sampleRate);
	if (!bufferL || sampleCount <= 0) return;
	int totalSamples = sampleCount;
	bool stereo = (bufferR != nullptr);

	// 表示用フォーマット: "ルート, <コード名>" の形式（固定バッファ・CString 嵐禁止）
	auto AppendChord = [](WCHAR* out, int outCap, const WCHAR* root, const WCHAR* body) {
		EqKeyCopy(out, outCap, L"!@B");
		wcsncat_s(out, outCap, root, _TRUNCATE);
		wcsncat_s(out, outCap, L", !@C002525<!@C000000", _TRUNCATE);
		wcsncat_s(out, outCap, body ? body : L"", _TRUNCATE);
		wcsncat_s(out, outCap, L"!@C002525>!@C000000!@B", _TRUNCATE);
	};
	auto FormatChordInto = [&](WCHAR* out, int outCap, const WCHAR* s) {
		if (!s || !s[0]) {
			EqKeyCopy(out, outCap, L"!@B  , <  >!@B");
			return;
		}
		WCHAR root[8];
		if (s[0] && (s[1] == L'#' || s[1] == L'b'))
			{ root[0] = s[0]; root[1] = s[1]; root[2] = 0; }
		else
			{ root[0] = s[0]; root[1] = L' '; root[2] = 0; }
		AppendChord(out, outCap, root, s);
	};

	// key4（全音域）: C  [C4 ] , <コード>。声が無いときは C  [   ] , <コード>。
	auto FormatChordAllInto = [](WCHAR* out, int outCap, const WCHAR* s, const WCHAR* melody) {
		const bool noMel = (!melody || !melody[0] || wcscmp(melody, L"[   ]") == 0);
		const WCHAR* mel = noMel ? L"   " : melody;
		WCHAR root[8] = L"  ";
		if (s && s[0]) {
			if (s[1] == L'#' || s[1] == L'b')
				{ root[0] = s[0]; root[1] = s[1]; root[2] = 0; }
			else
				{ root[0] = s[0]; root[1] = L' '; root[2] = 0; }
		}
		EqKeyCopy(out, outCap, L"!@B");
		wcsncat_s(out, outCap, root, _TRUNCATE);
//		wcsncat_s(out, outCap, L" [", _TRUNCATE);
//		wcsncat_s(out, outCap, mel, _TRUNCATE);
//		wcsncat_s(out, outCap, L"] , !@C002525<!@C000000", _TRUNCATE);
		wcsncat_s(out, outCap, L" , !@C002525<!@C000000", _TRUNCATE);
		if (!s || !s[0])
			wcsncat_s(out, outCap, L"!@F-01 !@F+01", _TRUNCATE);
		else
			wcsncat_s(out, outCap, s, _TRUNCATE);
		wcsncat_s(out, outCap, L"!@C002525>!@C000000!@B", _TRUNCATE);
	};

	float currentRMS = CalculateRMS(bufferL, bufferR, totalSamples, stereo);

	// 再生検出 & ピークRMS追跡
	if (currentRMS > PLAYING_THRESHOLD) {
		g_isPlaying = true; g_soundFrameCount++;
		if (currentRMS > g_peakRMS) g_peakRMS = currentRMS;
		else g_peakRMS *= 0.998f;  // ピークは緩やかに減衰
	}
	else { g_soundFrameCount = 0; }

	// 無音判定: 絶対閾値 + ピーク相対閾値の両方で評価
	bool isSilent = false;
	if (!g_isPlaying || g_peakRMS < 0.001f)
		isSilent = (currentRMS < SILENCE_THRESHOLD_ABS);
	else
		isSilent = (currentRMS < SILENCE_THRESHOLD_ABS)
		|| (currentRMS < g_peakRMS * SILENCE_THRESHOLD_REL);

	if (isSilent) g_silenceFrameCount++;
	else          g_silenceFrameCount = 0;

	// 無音が続いた場合はヒストリーをリセット
	if (g_silenceFrameCount >= SILENCE_FRAMES_FOR_CLEAR) {
		g_isPlaying = false; g_peakRMS = 0.0f; g_soundFrameCount = 0;
		HistoryClear(g_historyLow, g_historyLowN, g_historyLowHead);
		HistoryClear(g_historyMid, g_historyMidN, g_historyMidHead);
		HistoryClear(g_historyHigh, g_historyHighN, g_historyHighHead);
		HistoryClear(g_historyAll, g_historyAllN, g_historyAllHead);
		g_prevChordLow[0] = g_prevChordMid[0] = g_prevChordAll[0] = g_prevChordHigh[0] = 0;
		memset(g_noteStrength, 0, sizeof(g_noteStrength));
		memset(g_noteStrengthPrev, 0, sizeof(g_noteStrengthPrev));
		EqKeyVocalReset();
	}

	// 無音時は空白表示して減衰（ゴーストノート防止）
	if (isSilent) {
		EqKeyVocalReset();
		g_currentVowel = L' ';
		SetKeyCodesLocked(
			L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B",
			L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B",
			L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B",
			L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B");
		for (int i = 0; i < 108; i++) {
			g_noteStrength[i] *= 0.4f;
			g_noteStrengthPrev[i] *= 0.4f;
		}
		g_prevRMS = currentRMS; return;
	}

	g_prevRMS = g_prevRMS * 0.7f + currentRMS * 0.3f;

	// ===== Goertzel によるノート強度計算 =====
	// ビン k の音は MIDI(k+12)。k=0 は C0、k=57 は A4。
	// C0〜D#4 (k 0..51) は 4096 点。E4 以上は 2048 点。
	const int LOW_LIMIT = 52;
	const int LOW_N = (totalSamples >= 4096) ? 4096 : totalSamples;
	const int LOW_S = totalSamples - LOW_N;
	const int HI_N = (totalSamples >= 2048) ? 2048 : totalSamples;
	const int HI_S = totalSamples - HI_N;

	for (int k = 0; k < LOW_LIMIT; k++) {
		const double* win = (LOW_N == 4096) ? g_hannWindow4096 : nullptr;
		double aL = GoertzelMagnitude(bufferL + LOW_S, LOW_N, g_goertzelCoeffs[k], win);
		double aR = stereo ? GoertzelMagnitude(bufferR + LOW_S, LOW_N, g_goertzelCoeffs[k], win) : aL;
		float ns = (float)max(aL, aR) * (1.0f + k / 100.0f);
		g_noteStrength[k] = g_noteStrengthPrev[k] * SMOOTHING_FACTOR + ns * (1.0f - SMOOTHING_FACTOR);
		g_noteStrengthPrev[k] = g_noteStrength[k];
	}
	for (int k = LOW_LIMIT; k < 108; k++) {
		const double* win = (HI_N == 2048) ? g_blackmanWindow2048 : nullptr;
		double aL = GoertzelMagnitude(bufferL + HI_S, HI_N, g_goertzelCoeffs[k], win);
		double aR = stereo ? GoertzelMagnitude(bufferR + HI_S, HI_N, g_goertzelCoeffs[k], win) : aL;
		float ns = (float)max(aL, aR) * (1.0f + k / 50.0f);
		g_noteStrength[k] = g_noteStrengthPrev[k] * SMOOTHING_FACTOR + ns * (1.0f - SMOOTHING_FACTOR);
		g_noteStrengthPrev[k] = g_noteStrength[k];
	}

	// 帯域別ピッチクラス集約 → コード推定（生 Goertzel 強度を使用）
	float bC[12], mC[12], hC[12], aC[12];
	AggregateNoteClasses(bC, mC, hC, aC);

	WCHAR rawBass[EQKEY_NAME_MAX * 3];
	WCHAR rawMid[EQKEY_NAME_MAX * 3];
	WCHAR rawAll[EQKEY_NAME_MAX * 3];
	WCHAR rawHigh[EQKEY_NAME_MAX * 3];
	EstimateChordRawWithHistoryInto(rawBass, _countof(rawBass), bC, 0.02f, g_prevChordLow);
	EstimateChordRawWithHistoryInto(rawMid, _countof(rawMid), mC, 0.03f, g_prevChordMid);
	EstimateChordRawWithHistoryIntoAll(rawAll, _countof(rawAll), bC, mC, hC, aC, g_prevChordAll);
	EstimateChordRawWithHistoryInto(rawHigh, _countof(rawHigh), hC, 0.03f, g_prevChordHigh);

	// ヒストリーリング更新
	HistoryPush(g_historyLow, g_historyLowN, g_historyLowHead, rawBass);
	HistoryPush(g_historyMid, g_historyMidN, g_historyMidHead, rawMid);
	HistoryPush(g_historyHigh, g_historyHighN, g_historyHighHead, rawHigh);
	HistoryPush(g_historyAll, g_historyAllN, g_historyAllHead, rawAll);

	// 多数決安定化
	GetMostFrequentInto(rawBass, _countof(rawBass), g_historyLow, g_historyLowN, g_historyLowHead);
	GetMostFrequentInto(rawMid, _countof(rawMid), g_historyMid, g_historyMidN, g_historyMidHead);
	GetMostFrequentInto(rawAll, _countof(rawAll), g_historyAll, g_historyAllN, g_historyAllHead);
	GetMostFrequentInto(rawHigh, _countof(rawHigh), g_historyHigh, g_historyHighN, g_historyHighHead);

	EqKeyCopy(g_prevChordLow, EQKEY_NAME_MAX, rawBass);
	EqKeyCopy(g_prevChordMid, EQKEY_NAME_MAX, rawMid);
	EqKeyCopy(g_prevChordAll, EQKEY_NAME_MAX, rawAll);
	EqKeyCopy(g_prevChordHigh, EQKEY_NAME_MAX, rawHigh);

	// 歌唱だけ母音＋音名。インストは空。key4 は [あC4] , <コード>。
	WCHAR melodyBuf[24] = L"";
	//EqKeyVocalMelody(bufferL, bufferR, totalSamples, sampleRate, 0, melodyBuf, 24);
	//g_currentVowel = (melodyBuf[0] && melodyBuf[0] != L'[') ? melodyBuf[0] : L' ';
	const WCHAR* rawMelody = melodyBuf;

	// 出力コード文字列生成（ここだけで CString 化）
	WCHAR outLowW[256], outMidW[256], outAllW[256], outHighW[256];
	FormatChordInto(outLowW, _countof(outLowW), rawBass);
	FormatChordInto(outMidW, _countof(outMidW), rawMid);
	FormatChordAllInto(outAllW, _countof(outAllW), rawAll, rawMelody);

	if (!rawHigh[0])
		EqKeyCopy(outHighW, _countof(outHighW), L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B");
	else
		FormatChordInto(outHighW, _countof(outHighW), rawHigh);
	SetKeyCodesLocked(outLowW, outMidW, outHighW, outAllW);
}

// 外部から現在のノート強度配列を取得する (簡易ピアノロール表示等に使用)
void GetCurrentNoteStrengths(float* output108) {
	if (output108) memcpy(output108, g_noteStrength, sizeof(g_noteStrength));
}