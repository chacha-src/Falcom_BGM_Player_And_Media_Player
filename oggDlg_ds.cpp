#include "stdafx.h"
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
#include "ProAudio.h"
#include "DecodeProgress.h"
#include "CPromptEngine.h"
#if _MSC_VER >= 1950
#pragma comment(lib,"rubberband-library_2026")
#else
#pragma comment(lib,"rubberband-library")
#endif
extern int fade1;
extern int endflg;
// 曲終端のジャスト検出用（oggDlg.cpp 定義）。単位は「DS バッファへ書き込んだ総バイト数」。
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
extern __int64 playb;
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
	std::vector<float>& outFloat);
BOOL reset = TRUE;

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
extern int lenl;
extern int fade1;
extern BOOL sek;
extern int wavchannel, wavbit_sample_Hz, wavsam_depth;
//スレッド
int syukai = 0, syukai2 = 0;
extern BOOL sflg;
extern int muon;
#define MUON 180
int flg3 = 0;
int sek4;
extern int tempo;


ULONG oldw = OUTPUT_BUFFER_SIZE * 2;
extern std::vector<float> m_convertedPcmFloatData;
extern std::vector<uint8_t> outputRawBytesData;

//bool ProcessAudioWithSoundTouch(float tempoRate);
bool ProcessAudioWithRubberBand(float tempoRate, bool t);
void ConvertRawBytesToFloat(const std::vector<uint8_t>& raw_data,
	uint16_t bits_per_sample, uint16_t channels,
	std::vector<float>& out_float_data);
void ConvertFloatToRawBytes(const std::vector<float>& float_data,
	uint16_t target_bits_per_sample, uint16_t channels,
	std::vector<uint8_t>& out_raw_data);

BYTE bufkpil[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];
BYTE bufkpim[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3];


extern IGraphBuilder* pGraphBuilder;
extern IMediaControl* pMediaControl;

extern CString wavExportPath;
extern int wavExportLoopCount;
extern int g_wavExportMaxSec;
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
BOOL syoriflg;
extern int readme;

// 再生通知スレッド: thn==TRUE は「ループが終了シグナルを出した」だけでスレッド本体はまだ動くことがある。
// stop/stop1 から Closeds() やデコーダ解放の前に必ず Join する。
static CWinThread* s_playNotifyThread = nullptr;
static CCriticalSection s_playNotifyThreadCs;

// stop()/BeginPlaybackNotifyThread が Join する上限。曲切替時は短く(UI 応答性)。
DWORD g_playbackNotifyJoinTimeoutMs = 2500;
volatile LONG g_interactiveTrackChange = 0;

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
	if (og)
		og->timer.SetEvent();
}

// 戻り値: 再生スレッドが確実に終了したとき TRUE。FALSE のときデコーダを閉じてはならない。
// Join 中は DoEvent しない（再入で stop1/play が走り、生存中スレッドのデコーダを
// 二重解放したり、新スレッドのポインタを上書きして UAF になる）。
BOOL WaitForPlaybackNotifyThreadExit(DWORD timeoutMs)
{
	HANDLE hThread = NULL;
	{
		CSingleLock lk(&s_playNotifyThreadCs, TRUE);
		if (s_playNotifyThread && s_playNotifyThread->m_hThread)
			hThread = s_playNotifyThread->m_hThread;
	}
	// スレッド未起動なら thn 待ちでブロックしない
	if (!hThread) {
		CSingleLock lk(&s_playNotifyThreadCs, TRUE);
		if (s_playNotifyThread) {
			delete s_playNotifyThread;
			s_playNotifyThread = nullptr;
		}
		thn = TRUE;
		syukai = 0;
		syukai2 = 1;
		// Signal 済みの thn1/stf は呼び出し側がデコーダ事情で維持したい場合もあるが、
		// 「スレッド無し」なら停止待ちは完了しているのでここでは触らない。
		return TRUE;
	}

	// 再生スレッドが終わるまで待つ。途中で破棄すると flac/m4a/dsd/wav 切替時に
	// playwav* が解放済みデコーダ/adbuf2 を触ってクラッシュする。
	// timeoutMs==0 のときは無限待ち。非0 は上限（通常は無限で呼ぶ）。
	const DWORD pollMs = 10;
	DWORD elapsed = 0;
	BOOL exited = FALSE;
	for (;;) {
		// 停止要求を維持（Join 中に他経路で thn1/stf が落ちても再開させない）
		thn1 = TRUE;
		stf = 1;
		syukai = 2;
		if (og)
			og->timer.SetEvent();
		// DS Lock 中は Stop でドライバ待ちを解く（固まりの主因の一つ）
		// ただし二重DS昇格中の B は止めない（keep 中 Stop が「数秒で無音」になる）
		if (InterlockedCompareExchange(&g_dsDeviceOpBusy, 0, 0) != 0) {
			if (m_dsb)
				m_dsb->Stop();
			if (pAudioClient)
				pAudioClient->Stop();
		}
		const DWORD w = WaitForSingleObject(hThread, pollMs);
		if (w == WAIT_OBJECT_0) {
			exited = TRUE;
			break;
		}
		// 無効/閉じ済みハンドルで無限ループしない（ダングリング時の連続固まり防止）
		if (w == WAIT_FAILED) {
			exited = TRUE;
			break;
		}
		if (timeoutMs != 0) {
			elapsed += pollMs;
			if (elapsed >= timeoutMs)
				break;
		}
	}

	// スレッド生存中に CWinThread を delete しない（UAF）
	if (exited) {
		CSingleLock lk(&s_playNotifyThreadCs, TRUE);
		if (s_playNotifyThread) {
			delete s_playNotifyThread;
			s_playNotifyThread = nullptr;
		}
		thn = TRUE;
		syukai = 0;
		syukai2 = 1;
	}
	// thn1/stf は呼び出し側がデコーダ解放後に落とす（ここで落とすと解放前にデコード再開し得る）
	return exited;
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
	thn1 = FALSE;
	stf = 0;
	syukai = 0;
	syukai2 = 0;
	// CREATE_SUSPENDED で起動し、スレッド本体が走り出す前に m_bAutoDelete を
	// 落として寿命を自前管理する。これによりスレッドが自己終了しても
	// CWinThread オブジェクトとスレッドハンドルは破棄されず、安全に Join できる。
	// Resume 前に s_playNotifyThread へ登録し、起動直後の stop が「スレッド無し」と
	// 誤認してデコーダを潰し、再生スレッドが Lock で永久待ち→連続固まりになるのを防ぐ。
	CWinThread* t = AfxBeginThread((AFX_THREADPROC)HandleNotifications, NULL,
		THREAD_PRIORITY_TIME_CRITICAL, 0, CREATE_SUSPENDED);
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
	readme = 0;
	int fade2 = 0;
	syoriflg = FALSE;
	DWORD hr = DS_OK;
	// stop1/Wait/Signal で残った停止フラグを必ず下ろす。
	// stf!=0 のままだと IsPlaybackStopRequested() が真になり、
	// DispatchPlaywavFill→readBuffwav が即 return して無音になる
	// （HandleNotifications_export と同じ。CWread/adbuf 系で顕在化しやすい）。
	thn = FALSE;
	thn1 = FALSE;
	stf = 0;
	char* pdsb1; char* pdsb2;
	syukai = 0;
	syukai2 = 0;
	int dougainit = 0;
	int timeee = 0;
	HANDLE ev[] = { (HANDLE)og->timer };
	ULONG PlayCursor, WriteCursor = 0, len3, len4;

	auto isPlausibleDsb = [](LPDIRECTSOUNDBUFFER8 p) -> bool {
		if (!p)
			return false;
		const ULONG_PTR v = reinterpret_cast<ULONG_PTR>(p);
		// ヒープ破壊で m_dsb が 0x36 等のゴミになると if(p) を通過して AV するため
		return v >= 0x10000 && (v % sizeof(void*)) == 0;
	};

	const ULONG prefilledOldw = oldw; // play() がプリフィル後に進めた値
	oldw = 0;
	if (isPlausibleDsb(m_dsb))
		m_dsb->SetCurrentPosition(0);
	// mode ではなく Open 中の形式（曲切替で mode が先に変わる）
	if (g_openDecoderMode == -10 || g_openDecoderMode == 999) {
		oldw = OUTPUT_BUFFER_SIZE * 2;
		og->timer.SetEvent();
	}
	else if ((g_openDecoderMode == -7 || g_openDecoderMode == -8) && prefilledOldw > 0) {
		// DSD/FLAC の長プリフィル済み区間を再デコードで潰さない
		oldw = prefilledOldw;
	}
	fade1 = 0;
	sek4 = FALSE;

	auto stopPlaybackAndExit = [&]() -> UINT {
		playf = 1;
		thn = FALSE;
		LPDIRECTSOUNDBUFFER8 dsbStop = m_dsb;
		if (isPlausibleDsb(dsbStop)) {
			dsbStop->SetVolume(DSBVOLUME_MIN);
			dsbStop->Stop();
		}
		playf = 0;
		thn = TRUE;
		reset = TRUE;
		// AfxEndThread はスタックを巻き戻さない。cl2 の lock_guard 保持中に呼ぶと
		// mutex が解放されず、以降 timerp / 次の HandleNotifications が永久待ちになる。
		return 0;
	};

	for (;;) {
		// Wait 中は cl2 を取らない（OnHScroll 等がシークできる）

		// 曲ごとパラメータ: 曲頭で復元、再生中の変更をデバウンス保存
		SongParams_Sync(false);

		if (syukai == 2 || thn1) return stopPlaybackAndExit();
		if (syukai == 1) { syukai2 = 1; Sleep(1); continue; }

		// イベント待機（停止要求が来たら長く寝ない）
		const DWORD waitMs = thn1 ? 10u : (DWORD)savedata.ms;
		::WaitForMultipleObjects(1, ev, FALSE, waitMs);

		// FLAC等の重いシーク中（sek4）はロックせずに待機
		while (sek4) {
			if (syukai == 2 || thn1) return stopPlaybackAndExit();
			::WaitForMultipleObjects(1, ev, FALSE, thn1 ? 10u : (DWORD)savedata.ms);
		}

		if (sek == 1) {
			sflg = TRUE; flg3 = 3; sek = FALSE; sflg = FALSE;
			// シーク直後は書き込み位置を再調整する必要があるため continue
			continue;
		}
		if (thn1) return stopPlaybackAndExit();
		if (ps == 1) continue;

		LPDIRECTSOUNDBUFFER8 dsb = m_dsb;
		if (!isPlausibleDsb(dsb))
			continue;

		// 書き込み位置の計算
		dsb->GetCurrentPosition(&PlayCursor, &WriteCursor);
		const ULONG ringBytes = (g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : (ULONG)(OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM);
		int len1 = (int)WriteCursor - (int)oldw;
		int len2 = 0;

		if (len1 == 0) continue;
		if (len1 < 0) {
			len1 = (int)ringBytes - (int)oldw;
			len2 = (int)WriteCursor;
		}

		// 終端ドレイン中か（実音声が終わった後の純無音サイクル）。終端確定後、書込みヘッドが
		// 終端位置を越えていれば、このサイクルはすべて無音で埋める（古いループ音の漏れ防止）。
		const bool drainSilence = (g_endWrittenBytes != 0 && g_dsWrittenBytes >= g_endWrittenBytes);

		// cl2 はデコード＋状態更新のみ。dsb->Lock はドライバ待ちで数秒固まることがあり、
		// その間 UI(timerp) が同じ cl2 で止まるのを避けるため、PCM をステージしてから Lock する。
		static std::vector<BYTE> s_dsStage;
		int stageBytes = 0;
		bool stageFade = false;
		int readmeThisCycle = 0;
		const int writtenThisCycle = len1 + len2;
		bool exitAfterCl2 = false;

		{
			std::lock_guard<std::mutex> guard(cl2);

			// 3. 各種デコード処理（ロック内で実行）
			if (og->m_dou.GetCheck() == 1 && pGraphBuilder && pMediaControl) {
				if (timeee > 900 && dougainit == 0) {
					pMediaControl->Run();
					dougainit = 1;
				}
			}
			timeee += savedata.ms;

			if (thn1) {
				exitAfterCl2 = true;
			}
			else {
				sflg = TRUE;
				if (m_dsb) DispatchPlaywavFill(bufwav3, oldw, len1, len2);
				// 曲最後まで行ったとき
				readmeThisCycle = readme; // 終端確定に使うため reset 前に退避
				if (readme) {
					if (len1 > readme)
						ZeroMemory(bufwav3 + readme, len2);
					else
						ZeroMemory(bufwav3 + oldw + readme, len1 - readme);
				}
				if (thn1) {
					exitAfterCl2 = true;
					sflg = FALSE;
				}
				else {
					stageFade = (fade2 || drainSilence) ? true : false;
					stageBytes = writtenThisCycle;
					if (stageBytes > 0) {
						if ((int)s_dsStage.size() < stageBytes)
							s_dsStage.resize((size_t)stageBytes);
						if (len1 > 0)
							memcpy(s_dsStage.data(), bufwav3 + oldw, (size_t)len1);
						if (len2 > 0)
							memcpy(s_dsStage.data() + len1, bufwav3, (size_t)len2);
					}

					readme = 0;
					fade2 = fade1;
					if (flg3 != 0) flg3--;
					sflg = FALSE;
				}
			}
		} // guard(cl2) — Lock 前に必ず解放
		if (exitAfterCl2)
			return stopPlaybackAndExit();

		// DirectSound 転送（cl2 外。UI 側 Closeds で m_dsb が NULL でもローカル参照で安全）
		dsb = m_dsb;
		if (stageBytes > 0 && isPlausibleDsb(dsb) && !thn1 && !sek) {
			InterlockedExchange(&g_dsDeviceOpBusy, 1);
			hr = dsb->Lock(oldw, (DWORD)stageBytes, (LPVOID*)&pdsb1, &len3, (LPVOID*)&pdsb2, &len4, 0);
			if (hr == DS_OK) {
				thn = FALSE;
				const int copy1 = (int)len3;
				const int copy2 = (int)len4;
				if (copy1 > 0 && copy1 <= stageBytes)
					memcpy(pdsb1, s_dsStage.data(), (size_t)copy1);
				if (stageFade && copy1 > 0) ZeroMemory(pdsb1, (SIZE_T)copy1);
				if (copy2 > 0 && copy1 + copy2 <= stageBytes)
					memcpy(pdsb2, s_dsStage.data() + copy1, (size_t)copy2);
				if (stageFade && copy2 > 0) ZeroMemory(pdsb2, (SIZE_T)copy2);
				dsb->Unlock(pdsb1, len3, pdsb2, len4);
				if (!stageFade)
					MpMirrorWritePcm(s_dsStage.data(), stageBytes);
				if (!stageFade)
					MpRemoteWritePcm(s_dsStage.data(), stageBytes);
			}
			InterlockedExchange(&g_dsDeviceOpBusy, 0);
		}

		{
			std::lock_guard<std::mutex> guard(cl2);
			// 書込み累積は従来どおり Lock 成否に依存しない（再生位置進行の一貫性維持）。
			const __int64 writtenBefore = g_dsWrittenBytes;
			g_dsWrittenBytes += (writtenThisCycle > 0) ? writtenThisCycle : 0;
			// EOF（fade1=停止 / endflg=連続）を最初に検出したサイクルで実音声の終端を確定。
			// readme があれば最終チャンク内の実バイト境界が分かるのでそれを使う。無ければこのサイクル末尾。
			if (g_endWrittenBytes == 0 && (fade1 || endflg)) {
				if (readmeThisCycle > 0 && readmeThisCycle <= writtenThisCycle)
					g_endWrittenBytes = writtenBefore + readmeThisCycle;
				else
					g_endWrittenBytes = g_dsWrittenBytes;
			}
			oldw = WriteCursor;
		}

		// 終端の短フェード＆ジャスト停止判定（ロックを外して終了処理へ）。
		// playb(デコード先頭)ではなく DS 再生カーソルが実音声終端へ到達した瞬間を「曲終わり」とする。
		// 実再生バイト数 = 累積書込み − 未再生キュー(我々の書込みヘッド oldw と再生カーソル pc の差)。
		// oldw はリング上の実書込み位置。g_dsWrittenBytes%ring とは初期オフセット分ずれるため oldw を使う。
		if (g_endWrittenBytes != 0) {
			LPDIRECTSOUNDBUFFER8 dsbb = m_dsb;
			__int64 heard = g_endWrittenBytes; // 既定: 終端到達扱い（dsb 取得失敗時の保険）
			if (isPlausibleDsb(dsbb)) {
				ULONG pc = 0, wc = 0;
				if (dsbb->GetCurrentPosition(&pc, &wc) == DS_OK && ringBytes > 0) {
					const __int64 queued = (__int64)(((ULONG)oldw + ringBytes - pc) % ringBytes);
					heard = g_dsWrittenBytes - queued;
				}
				g_heardBytes = heard; // 連続再生のタイマー 9000 などが参照
				// 終端直前の残り実音声に短いフェードをかけてクリック/プツ音を防ぐ。
				const __int64 remain = g_endWrittenBytes - heard;
				const int bpf = (g_outBytesPerFrame > 0) ? g_outBytesPerFrame : 4;
				const __int64 fadeBytes = (__int64)bpf * (__int64)wavbit_sample_Hz * 35 / 1000; // 約35ms
				if (remain > 0 && remain < fadeBytes && fadeBytes > 0) {
					LONG vol = (LONG)((double)DSBVOLUME_MIN * (1.0 - (double)remain / (double)fadeBytes));
					if (vol > 0) vol = 0;
					if (vol < DSBVOLUME_MIN) vol = DSBVOLUME_MIN;
					dsbb->SetVolume(vol);
				}
			}
			else {
				g_heardBytes = heard;
			}

			// fade1(=停止 / 連続でない) のときだけ DS スレッドで停止する。
			// 連続再生(endflg)の次曲遷移は UI 側タイマー 9000 が同じ終端到達判定で行う。
			if (fade1 && heard >= g_endWrittenBytes) {
				if (thn1 || stf != 0 || syukai == 2)
					return stopPlaybackAndExit();
				playf = 0; thn = TRUE; reset = TRUE;
				LPDIRECTSOUNDBUFFER8 dsbFade = m_dsb;
				if (isPlausibleDsb(dsbFade)) {
					dsbFade->SetVolume(DSBVOLUME_MIN);
					dsbFade->Stop();
				}
				if (og && ::IsWindow(og->GetSafeHwnd()))
					og->PostMessage(WM_PLAYBACK_AUTO_STOPPED, 0, 0);
				// AfxEndThread 禁止（stopPlaybackAndExit と同じ理由）。通常 return でスレッド終了。
				return 0;
			}
		}
	}
	return 0;
} //handlenotifications()

// WAV出力専用：DirectSoundを使わずデコード→ファイル書き込みのみ。m_c2チェックに関係なくcc1で出力。
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

	MpDecodeProgressReport(2, LL14(
		L"デコード中…", L"Decoding...", L"Decodage...", L"Decodifica...", L"Decodificando...",
		L"디코딩 중…", L"解码中…", L"Decoding...", L"Декодирование...", L"Dekodiere...",
		L"Decodificando...", L"Decoderen...", L"Dekodowanie...", L"Kod cozuluyor..."));

	__int64 lastWl = wl;
	int idleIters = 0;
	int iter = 0;
	for (;;) {
		DoEvent();
		// DoEvent 再入で stop 要求が立っても、書き出し本体は続行する
		thn1 = FALSE;
		stf = 0;
		if (syukai == 2) break;
		if (fade1) break;
		if (wavExportLoopCount > 0 && loopcnt >= wavExportLoopCount) break;
		// KPI(mode==-3)等: 終端のないソースは秒数で打ち切る（実書き込み量優先）
		// wl は変換後バイト。打ち切りもロック済み出力形式で数える（192k→48k で 1/4 短縮しない）
		if (g_wavExportMaxSec > 0 && wavbit_sample_Hz > 0) {
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
			const __int64 maxOut = (__int64)g_wavExportMaxSec * (__int64)outRate;
			const __int64 maxSrc = (__int64)g_wavExportMaxSec * (__int64)wavbit_sample_Hz;
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
		if (g_wavExportMaxSec > 0 && wavbit_sample_Hz > 0) {
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
			const __int64 maxOut = (__int64)g_wavExportMaxSec * (__int64)outRate;
			const __int64 maxSrc = (__int64)g_wavExportMaxSec * (__int64)wavbit_sample_Hz;
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

extern std::vector<float> inputFloatData;
extern std::vector<uint8_t> m_bufwav3_1;
extern int pitch;
extern float tempoRate2;
extern float PitchScaleFromPos(int pitchPos);
std::vector<float> g_loopTailBuffer;
size_t g_loopTailPos = 0;
enum { RB_BANKS = 2 };
static bool g_rubberBandFinalFlushed[RB_BANKS] = { false, false };
static int g_rbInitRate[RB_BANKS] = { 0, 0 };
static int g_rbInitCh[RB_BANKS] = { 0, 0 };
static std::mutex g_rbMu[RB_BANKS];
// 毎バッファ vector 新規確保を避け、容量は伸ばすのみ（断片化抑制）
struct RbProcessScratch {
	std::vector<uint8_t> raw;
	std::vector<float> inputFloat;
	std::vector<float> channelFlat;
	std::vector<float*> channelPointers;
	std::vector<float> outputFlat;
	std::vector<float*> outputPointers;
	float dummyZero = 0.0f;
};
static RbProcessScratch g_rbScratch[RB_BANKS];

void ConvertRawBytesToFloat(const std::vector<uint8_t>& raw_data,
	uint16_t bits_per_sample, uint16_t channels,
	std::vector<float>& out_float_data);
void ConvertFloatToRawBytes(const std::vector<float>& float_data,
	uint16_t target_bits_per_sample, uint16_t channels,
	std::vector<uint8_t>& out_raw_data);

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
	std::vector<float>& outFloat)
{
	try {
		if (bank < 0 || bank >= RB_BANKS) bank = 0;
		if (ch < 1) ch = 2;
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
			outFloat.clear();
			return true;
		}

		g_rubberBandStretcher[bank]->setTimeRatio(tempoRate);
		g_rubberBandStretcher[bank]->setPitchScale(PitchScaleFromPos(pitch));

		if (!t) {
			scr.raw.resize((size_t)inBytes);
			memcpy(scr.raw.data(), inData, (size_t)inBytes);
			ConvertRawBytesToFloat(scr.raw, (uint16_t)bits, (uint16_t)ch, scr.inputFloat);
			if (scr.inputFloat.empty()) return false;

			const size_t samplesIn = scr.inputFloat.size() / (size_t)ch;
			if (samplesIn == 0) return false;
			scr.channelFlat.resize(samplesIn * (size_t)ch);
			scr.channelPointers.resize((size_t)ch);
			for (int c = 0; c < ch; ++c)
				scr.channelPointers[c] = scr.channelFlat.data() + (size_t)c * samplesIn;
			for (size_t i = 0; i < samplesIn; ++i) {
				for (int c = 0; c < ch; ++c)
					scr.channelPointers[c][i] = scr.inputFloat[i * (size_t)ch + (size_t)c];
			}

			g_rubberBandStretcher[bank]->process(scr.channelPointers.data(), samplesIn, false);
			g_rubberBandFinalFlushed[bank] = false;
		}
		else {
			scr.channelPointers.resize((size_t)ch);
			for (int c = 0; c < ch; ++c)
				scr.channelPointers[c] = &scr.dummyZero;
			g_rubberBandStretcher[bank]->process(scr.channelPointers.data(), 0, true);
			g_rubberBandFinalFlushed[bank] = true;
		}

		outFloat.clear();
		const size_t pullSize = 4096;
		scr.outputFlat.resize(pullSize * (size_t)ch);
		scr.outputPointers.resize((size_t)ch);
		for (int c = 0; c < ch; ++c)
			scr.outputPointers[c] = scr.outputFlat.data() + (size_t)c * pullSize;

		while (g_rubberBandStretcher[bank]->available() > 0) {
			size_t toGet = (std::min)((size_t)g_rubberBandStretcher[bank]->available(), pullSize);
			size_t retrieved = g_rubberBandStretcher[bank]->retrieve(scr.outputPointers.data(), toGet);
			if (retrieved == 0) break;
			const size_t base = outFloat.size();
			outFloat.resize(base + retrieved * (size_t)ch);
			for (size_t i = 0; i < retrieved; ++i) {
				for (int c = 0; c < ch; ++c)
					outFloat[base + i * (size_t)ch + (size_t)c] = scr.outputPointers[c][i];
			}
		}

		if (bank == 0 && g_loopTailPos < g_loopTailBuffer.size()) {
			size_t tailTotal = g_loopTailBuffer.size();
			for (size_t i = 0; i < outFloat.size(); ++i) {
				if (g_loopTailPos >= tailTotal) break;
				float ratio = 1.0f - ((float)g_loopTailPos / (float)tailTotal);
				float fadeFactor = ratio * ratio;
				outFloat[i] += g_loopTailBuffer[g_loopTailPos] * fadeFactor;
				g_loopTailPos++;
			}
			if (g_loopTailPos >= tailTotal) {
				g_loopTailBuffer.clear();
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
		if (m_bufwav3_1.empty() && !t) return false;

		int bits;
		if (mode == -10)
			bits = Mp3GetDecoderBitsForRubberBand();
		else
			bits = (wavsam_depth <= 0 || wavsam_depth > 32) ? 16 : abs(wavsam_depth);

		const uint8_t* p = m_bufwav3_1.empty() ? NULL : m_bufwav3_1.data();
		const int n = (int)m_bufwav3_1.size();
		const int ch = (wavchannel > 0) ? wavchannel : 2;
		const int rate = (wavbit_sample_Hz >= 8000) ? wavbit_sample_Hz : 44100;
		return ProcessAudioWithRubberBandBank(0, tempoRate, t, p, n, bits, ch, rate,
			m_convertedPcmFloatData);
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
void ConvertRawBytesToFloat(const std::vector<uint8_t>& raw_data,
	uint16_t bits_per_sample, uint16_t channels,
	std::vector<float>& out_float_data)
{
	if (raw_data.empty() || channels == 0 || bits_per_sample == 0) {
		out_float_data.clear();
		return;
	}

	size_t bytes_per_sample = bits_per_sample / 8;
	if (bytes_per_sample == 0) {
		out_float_data.clear();
		return;
	}
	size_t total_samples_count = raw_data.size() / bytes_per_sample;
	out_float_data.clear();
	out_float_data.resize(total_samples_count);

	for (size_t i = 0; i < total_samples_count; ++i) {
		size_t current_byte_pos = i * bytes_per_sample;
		if (current_byte_pos + bytes_per_sample > raw_data.size()) {
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
			out_float_data.clear();
			return;
		}
	}
}

// floatデータからrawバイトデータへの変換
// 8bit, 16bit, 24bit, 32bit PCM (int/float) に対応
// floatデータからrawバイトデータへの変換
// 8bit, 16bit, 24bit, 32bit PCM (int/float) に対応
void ConvertFloatToRawBytes(const std::vector<float>& float_data,
	uint16_t target_bits_per_sample, uint16_t channels,
	std::vector<uint8_t>& out_raw_data)
{
	if (float_data.empty() || channels == 0 || target_bits_per_sample == 0) {
		out_raw_data.clear();
		return;
	}

	size_t bytes_per_sample = target_bits_per_sample / 8;
	out_raw_data.clear();
	out_raw_data.resize(float_data.size() * bytes_per_sample);

	for (size_t i = 0; i < float_data.size(); ++i) {
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
			out_raw_data.clear();
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

  環境音響: 100種 (0-99) - 完全差別化・リアル志向
  EQプリセット: 51種 (0-50)
  拡張パラメータ: 5種 (eq[15-19])

  パラメータ総数: 63個（従来29個→63個）
  物理ベースモデリング & 周波数依存処理
===============================================================================

環境音響100種:
[基本空間 0-10]
00.なし
01.風呂場 - 短く明るい金属的
02.ホール - 中程度でバランス
03.教会 - 長く荘厳
04.洞窟 - 暗くこもった
05.スタジオ - 極めてドライ
06.ライブハウス - パンチのある
07.森 - 柔らかく自然
08.山 - 長いエコー（3タップ）
09.広場 - 開放的
10.カテドラル - 超巨大で超長残響

[公共施設 11-20]
11.体育館 - 硬く金属的
12.峡谷 - 両側から複数エコー（4タップ）
13.地下室 - 圧迫感のある密度
14.劇場 - 音響設計された空間
15.水中 - 特殊な低域特性
16.トンネル/地下道 - フラッターエコー
17.アリーナ/ドーム - 超巨大スポーツ施設
18.小部屋/クローゼット - 超小空間デッド
19.階段室 - 縦方向の特殊反射
20.地下鉄ホーム - 都市的コンクリート

[産業・商業 21-30]
21.倉庫 - 大きく空っぽ
22.廊下 - 長く狭い直線的
23.工場 - 金属的産業的
24.寺社仏閣 - 木造の温かみ
25.宇宙空間 - SF特殊空間
26.野球場/サッカー場 - 屋外超大型
27.図書館 - 静寂で吸音的
28.プール(室内) - タイル水面反射
29.エレベーター - 超小金属空間
30.駐車場 - 広い低天井コンクリート

[文化施設 31-40]
31.コンサートホール - クラシック用最高峰
32.ジャズクラブ - 親密で温かい
33.カラオケボックス - 小密室エンタメ
34.映画館 - THX規格的
35.地下鉄車内 - 揺れる密室
36.空港ターミナル - 巨大公共空間
37.ショッピングモール - 賑やか商業施設
38.病院 - 静かで清潔
39.レコーディングブース - プロ用極ドライ
40.オペラハウス - 劇場の最高峰

[生活空間 41-50]
41.喫茶店/カフェ - 適度な賑わいと吸音
42.バー/ラウンジ - 暗く落ち着いた雰囲気
43.居酒屋 - 賑やか木材吸音
44.美術館/博物館 - 静かで広い高天井
45.講堂/大学教室 - 教育施設の反射
46.竹林 - 和風自然音響
47.渓谷/滝 - 水の反射と濡れた岩肌（4タップ）
48.砂漠 - 超開放的反射極小
49.ガレージ - 車庫硬質空間
50.展望台 - 高所開放感

[拡張空間 51-60]
51.小さな礼拝堂 - 教会より親密で温かい
52.大型ショッピングセンター - モールより巨大
53.地下洞窟(深層) - より深く神秘的
54.古城の大広間 - 石造り中世的（3タップ）
55.野外音楽堂 - 半開放的ステージ（2タップ）
56.鍾乳洞 - 複雑な水滴反射
57.廃墟工場 - 荒廃した金属空間
58.和室(畳) - 日本的柔らかい吸音
59.温泉施設 - 湿度高めタイル反射
60.屋根裏部屋 - 斜め天井の特殊空間

[特殊空間 61-70]
61.地下駐車場(多層) - 階層的複雑反射
62.古い劇場(木造) - 温かみある音響設計
63.大型倉庫(空) - 極端な空虚感（3タップ）
64.小さな教会 - カテドラルより親密
65.ガラス温室 - 硬質ガラス反射
66.石造りトンネル - 硬く長い残響
67.コンクリート階段 - 硬質縦方向反射
68.大浴場 - 広いタイル反射
69.洗面所 - 極小タイル空間
70.廊下(カーペット) - 吸音的柔らかい

[専門空間 71-80]
71.会議室(大) - ビジネス空間
72.会議室(小) - より密閉的
73.防音室 - 極端なデッド空間
74.エントランスホール - 高天井開放的
75.書斎 - 本による吸音
76.キッチン - 硬質多反射
77.屋外駐車場 - 開放的反射少
78.地下道(狭) - 圧迫的狭小空間
79.展示室 - 美術館より吸音的
80.アトリエ - 創作空間の独特さ

[SF/未来空間 81-100]
81.サイバーパンク路地 - 金属反射＋狭い空間、ネオン感
82.宇宙船ブリッジ - クリーンで硬質、短い反射
83.ワープトンネル - 揺らぎと長い残響、引き伸ばし
84.量子ホール - 不安定拡散、浮遊感
85.無限回廊 - 規則的エコー、長く続く反射
86.逆再生空間 - 早い反射と遅い尾、異常な広がり
87.タイムストップ室 - ほぼ無響＋硬い反射
88.データセンター - 低域振動、機械的反射
89.巨大機械内部 - 金属共鳴、重い反射
90.AIホログラム室 - 透明感、明るい反射
91.重力ゼロ船庫 - 低密度で長残響
92.惑星ドーム都市 - 超巨大＋ガラス反射
93.VRシミュレーター - 過剰ステレオ＋揺れ
94.レーザー通路 - 鋭いフラッター、硬質
95.異次元裂け目 - 不規則ディレイ、崩れる残響
96.夢の中 - 柔らかく滲む、低コントラスト
97.水晶洞 - 高域きらめき、長い余韻
98.廃宇宙ステーション - 冷たく乾いた残響
99.ブラックホール縁 - 超長残響＋低域膨張
100.サイバー聖堂 - 金属×巨大空間、光沢残響

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
#define MAX_DELAY_SAMPLES 3072000*2
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

typedef struct {
	float b0, b1, b2, a1, a2;
	float x1, x2, y1, y2;
} Biquad;

// ===== LFO =====
typedef struct {
	float phase;
	float frequency;
	float depth;
} LFO;

// ===== Channel State =====
typedef struct {
	Biquad eqFilters[EQ_BANDS];
	Biquad clarityFilter, bassBalanceFilter, trebleBalanceFilter;
	Biquad densityFilter1, densityFilter2;
	Biquad envLpf, envHpf, exciterFilter, dampingFilter;
	Biquad bassReverbFilter, midReverbFilter, trebleReverbFilter;
	Biquad materialFilter, warmthFilter, flutterFilter;
	Biquad resonanceFilter, metallicFilter, glassFilter;

	float* delayBuffer;
	int writePos;
	LFO lfo;

	float diffusionBuffer1[8][1024], diffusionBuffer2[8][512], diffusionBuffer3[8][256];
	int diffusionPos1[8], diffusionPos2[8], diffusionPos3[8];

	float harmonicState, earlyEnvelope, lateEnvelope;
	float warmthState, brightnessState, shimmerState;
	float flutterPhase, dopplerPhase, phasingPhase;

	// ===== 各プリセットの音響キャラクター差異付け用の状態 =====
	// (従来 DSP で未使用だった環境パラメータを活かすために追加)
	float wetAirState;     // airAbsorption/humidity/altitude による残響尾の高域吸収
	float wetColorState;   // reverbColor (残響の明暗) 用ローパス状態
	float echoToneState;   // echoFeedbackTone (山彦の明暗) 用ローパス状態
	float wetSmoothState;  // reverbSmooth (残響の滑らかさ) 用平滑状態
	float combBuf[2048];   // combFiltering 用ショートディレイ
	int   combPos;
	float windPhase;       // windEffect 用の超低周波ゆらぎ位相
	float wetWeightState;  // weight (低域の量感) 用
	float bodyToneState;   // woodiness/concrete (中低域の色付け) 用
	float softState;       // softness (高域/トランジェントの丸め) 用
	float phaserLfo;       // phasing 用 LFO 位相
	float phaserState[2];  // phasing 用オールパス段

	// Yamabiko buffers
	float* yamabikoBuf;
	int yamabikoBufSize;
	int yamabikoPos;
} ChannelState;

// ===== Global State =====
enum { EQ_BANKS = 2 };
static int g_eqCur = 0;
static std::mutex g_eqMu;
static ChannelState g_channels[EQ_BANKS][MAX_CH];
static float g_delayMemory[EQ_BANKS][MAX_CH][MAX_DELAY_SAMPLES];
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

/*
■ リミッターの動作を調整したい場合

1. threshold（圧縮開始レベル）
- 0.90f: 安全重視、早めに圧縮開始（ダイナミックレンジやや狭い）
- 0.95f : バランス良好（推奨）
- 0.98f : ダイナミックレンジ優先（僅かなクリップリスク）

2. attackTime（アタックタイム）
- 0.0005f (0.5ms) : 超高速、瞬時のピーク対応
- 0.001f (1ms) : 高速（推奨）
- 0.005f (5ms) : やや緩やか、自然

3. releaseTime（リリースタイム）
- 0.050f (50ms) : 速い戻り、パンチ重視
- 0.100f (100ms) : バランス良好（推奨）
- 0.200f (200ms) : 自然な戻り、滑らか重視
- 0.300f (300ms) : 非常に滑らか

■ 調整例

// より安全重視の設定
g_limiter[g_eqCur][ch].threshold = 0.92f;
attackTime = 0.0005f;  // 超高速反応
releaseTime = 0.150f;  // やや遅めの戻り

// ダイナミックレンジ優先の設定
g_limiter[g_eqCur][ch].threshold = 0.97f;
attackTime = 0.002f;   // 少しゆったり
releaseTime = 0.200f;  // 自然な戻り

■ 動作原理

1. 入力信号がthresholdを超えたら、超えた分だけゲインを下げる
2. アタックタイムで素早くゲインを下げる（ピーク防止）
3. リリースタイムでゆっくりゲインを戻す（自然な音）
4. 最後にソフトクリッピングで安全装置

■ メリット

✓ 静かな部分は影響を受けない
✓ ピーク部分だけ自然に圧縮
✓ 音割れ完全防止
✓ 音響モデルの特性を保持
✓ g_autoGainのような「小さいまま」問題が解消
*/
// ダイナミックリミッター構造体定義
typedef struct {
	float envelope;      // 現在のゲインリダクション
	float threshold;     // 圧縮開始レベル（0.95f推奨）
	float attackCoeff;   // アタック係数（プリ計算済み）
	float releaseCoeff;  // リリース係数（プリ計算済み）
} DynamicLimiter;

static DynamicLimiter g_limiter[EQ_BANKS][2];
static DynamicLimiter g_extBoostLimiter[EQ_BANKS][2];

// ===== EQ Frequencies =====
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
	// 0: なし - 音響処理オフ
	{
		0.0f, 0.0f, 0.0f,
		{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		20000, 20, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.0f,
		1.0f, 1.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f,
		0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 0.5f, 0.0f, 0.0f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.5f, 0.5f, 1000.0f, 1.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_NONE
	},

	// 1: 風呂場 - 小空間・タイル・金属的響き
	{
		0.50f, 14.5f, 0.58f,
		{ 2.0f, 0.70f, 3.5f, 0.65f, 6.0f, 0.60f, 9.0f, 0.53f, 11.0f, 0.48f, 13.0f, 0.42f, 15.0f, 0.35f, 17.0f, 0.30f },
		19500, 320, 0.58f, 0.05f, 3.5f, 0.35f, 0.15f, 0.0f, 0.08f, 0.28f, 0.65f, 0.20f, 0.0f,
		0.38f, 0.48f, 0.32f, 0.82f, 1.08f, 0.98f, 0.82f, 0.22f, 0.42f, 0.82f, 0.12f, 0.12f,
		0.02f, 0.22f, 0.82f, 0.12f, 0.22f, 0.58f, 0.68f, 0.22f, 0.22f, 0.12f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.15f, 0.85f, 3200.0f, 2.5f,
		0.15f, 0.85f, 0.05f, 0.25f,
		0.78f, 0.0f, 0.95f, 0.0f,
		0.25f, 0.0f, 0.15f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 2: ホール - 中規模バランス型
	{
		0.40f, 82.0f, 0.48f,
		{ 12.0f, 0.46f, 18.0f, 0.42f, 32.0f, 0.37f, 48.0f, 0.33f, 65.0f, 0.28f, 82.0f, 0.24f, 105.0f, 0.19f, 128.0f, 0.15f },
		11200, 65, 1.35f, 0.32f, 1.02f, 0.22f, 0.58f, 4.5f, 0.46f, 1.05f, 0.45f, 0.82f, 0.36f,
		0.72f, 1.22f, 0.72f, 0.67f, 1.22f, 1.02f, 0.92f, 0.72f, 0.62f, 0.62f, 0.22f, 0.32f,
		0.22f, 0.42f, 0.72f, 0.52f, 0.52f, 1.32f, 1.22f, 0.62f, 0.0f, 0.22f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.42f, 0.58f, 800.0f, 1.2f,
		0.0f, 0.0f, 0.48f, 0.0f,
		0.0f, 0.0f, 0.35f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 3: 教会 - 長残響・荘厳
	{
		0.32f, 172.0f, 0.31f,
		{ 28.0f, 0.28f, 45.0f, 0.25f, 75.0f, 0.21f, 110.0f, 0.18f, 155.0f, 0.15f, 200.0f, 0.12f, 250.0f, 0.11f, 310.0f, 0.08f },
		5500, 38, 1.52f, 0.52f, 0.36f, 0.08f, 0.74f, 14.2f, 0.56f, 1.68f, 0.31f, 0.84f, 0.43f,
		1.52f, 2.02f, 0.84f, 0.50f, 1.62f, 1.14f, 0.67f, 0.84f, 0.48f, 0.49f, -0.16f, 0.27f,
		0.38f, 0.49f, 0.42f, 0.67f, 0.74f, 1.92f, 1.67f, 0.67f, 0.0f, 0.12f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.32f, 0.68f, 450.0f, 0.8f,
		0.0f, 0.0f, 0.85f, 0.0f,
		0.0f, 0.0f, 0.22f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CATHEDRAL
	},

	// 4: 洞窟 - 暗く湿った反射
	{
		0.40f, 128.0f, 0.43f,
		{ 24.0f, 0.42f, 38.0f, 0.37f, 92.0f, 0.34f, 142.0f, 0.31f, 172.0f, 0.27f, 205.0f, 0.24f, 242.0f, 0.20f, 288.0f, 0.16f },
		3900, 98, 1.28f, 0.46f, 0.52f, 0.03f, 0.68f, 12.2f, 0.68f, 1.28f, 0.37f, 0.77f, 0.58f,
		1.08f, 1.58f, 0.42f, 0.35f, 1.30f, 0.94f, 0.57f, 0.72f, 0.37f, 0.37f, -0.43f, 0.50f,
		0.74f, 0.30f, 0.27f, 0.62f, 0.84f, 1.27f, 1.37f, 0.37f, 0.10f, 0.27f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.58f, 0.42f, 320.0f, 1.8f,
		0.0f, 0.0f, 0.0f, 0.85f,
		0.85f, 0.0f, 0.75f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CAVE
	},

	// 5: スタジオ - 極デッド・フラット
	{
		0.05f, 6.5f, 0.02f,
		{ 2.0f, 0.14f, 3.5f, 0.12f, 5.5f, 0.08f, 8.0f, 0.04f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		19800, 22, 0.88f, 0.0f, 0.14f, 0.40f, 0.03f, 0.0f, 0.18f, 0.50f, 0.88f, 0.18f, 0.10f,
		0.22f, 0.32f, 0.92f, 0.42f, 0.82f, 1.02f, 1.02f, 0.12f, 0.12f, 0.87f, 0.02f, 0.92f,
		0.12f, 0.47f, 0.53f, 0.32f, 0.42f, 0.92f, 0.82f, 0.12f, 0.0f, 0.02f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.02f, 0.98f, 15000.0f, 0.7f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.98f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 6: ライブハウス - パンチ重視
	{
		0.52f, 56.0f, 0.46f,
		{ 6.0f, 0.65f, 10.0f, 0.61f, 18.0f, 0.54f, 26.0f, 0.49f, 35.0f, 0.43f, 45.0f, 0.36f, 58.0f, 0.31f, 72.0f, 0.25f },
		13000, 125, 1.48f, 0.26f, 1.55f, 0.48f, 0.50f, 7.6f, 0.20f, 0.87f, 0.71f, 0.68f, 0.10f,
		0.52f, 0.92f, 0.62f, 0.78f, 1.02f, 0.92f, 0.82f, 0.62f, 0.72f, 0.74f, 0.34f, 0.22f,
		0.32f, 0.54f, 0.78f, 0.42f, 0.32f, 1.02f, 1.12f, 0.52f, 0.08f, 0.18f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.48f, 0.52f, 2200.0f, 1.5f,
		0.0f, 0.0f, 0.35f, 0.45f,
		0.0f, 0.0f, 0.42f, 0.0f,
		0.22f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 7: 森 - 柔らかい自然音響
	{
		0.28f, 175.0f, 0.30f,
		{ 38.0f, 0.28f, 62.0f, 0.26f, 110.0f, 0.23f, 158.0f, 0.20f, 205.0f, 0.19f, 252.0f, 0.16f, 308.0f, 0.13f, 365.0f, 0.11f },
		4200, 210, 1.65f, 0.52f, 0.52f, 0.05f, 0.82f, 11.2f, 0.60f, 1.72f, 0.41f, 0.88f, 0.78f,
		1.42f, 1.72f, 0.80f, 0.45f, 1.27f, 1.00f, 0.70f, 0.87f, 0.44f, 0.50f, -0.28f, 0.70f,
		0.60f, 0.66f, 0.55f, 0.80f, 0.44f, 1.47f, 1.57f, 0.80f, 0.08f, 0.19f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.72f, 0.28f, 600.0f, 0.9f,
		0.0f, 0.0f, 0.82f, 0.0f,
		0.0f, 0.0f, 0.85f, 0.48f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_OUTDOOR_OPEN
	},

	// 8: 山 - シンプルな山彦（3タップ）
	{
		0.88f, 500.0f, 0.10f,
		{ 320.0f, 0.88f, 640.0f, 0.52f, 960.0f, 0.28f, 1280.0f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		16500, 42, 0.38f, 0.03f, 0.15f, 0.03f, 0.08f, 5.0f, 0.10f, 0.32f, 0.08f, 0.18f, 0.03f,
		0.22f, 0.38f, 0.10f, 0.08f, 0.32f, 0.28f, 0.22f, 0.18f, 0.15f, 0.08f, -0.22f, 0.03f,
		0.08f, 0.05f, 0.10f, 0.12f, 0.08f, 0.05f, 0.03f, 0.18f, 0.15f, 0.08f,
		{ 350.0f, 700.0f, 1050.0f, 0.0f }, { 0.82f, 0.52f, 0.28f, 0.0f }, 1.25f, 0.25f,
		0.08f, 0.92f, 450.0f, 0.6f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.95f, 0.92f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_MOUNTAIN_ECHO
	},

	// 9: 広場 - 開放的
	{
		0.43f, 125.0f, 0.32f,
		{ 28.0f, 0.39f, 42.0f, 0.34f, 62.0f, 0.29f, 85.0f, 0.24f, 112.0f, 0.18f, 142.0f, 0.15f, 178.0f, 0.11f, 218.0f, 0.08f },
		16000, 102, 1.58f, 0.35f, 0.85f, 0.20f, 0.65f, 10.5f, 0.39f, 1.42f, 0.62f, 0.53f, 0.59f,
		0.82f, 1.12f, 0.72f, 0.53f, 1.02f, 1.02f, 0.92f, 0.62f, 0.62f, 0.76f, -0.12f, 0.32f,
		0.42f, 0.56f, 0.57f, 0.52f, 0.52f, 1.22f, 1.42f, 0.72f, 0.14f, 0.12f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.52f, 0.48f, 1200.0f, 0.8f,
		0.0f, 0.0f, 0.42f, 0.55f,
		0.0f, 0.0f, 0.75f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_OUTDOOR_OPEN
	},

	// 10: カテドラル - 超巨大空間
	{
		0.33f, 305.0f, 0.34f,
		{ 42.0f, 0.30f, 68.0f, 0.27f, 118.0f, 0.24f, 168.0f, 0.19f, 225.0f, 0.17f, 285.0f, 0.15f, 355.0f, 0.12f, 435.0f, 0.10f },
		5000, 33, 1.58f, 0.55f, 0.28f, 0.07f, 0.78f, 26.0f, 0.55f, 1.98f, 0.44f, 0.85f, 0.45f,
		1.77f, 2.37f, 0.90f, 0.47f, 1.80f, 1.24f, 0.62f, 0.87f, 0.42f, 0.45f, -0.24f, 0.22f,
		0.48f, 0.60f, 0.39f, 0.72f, 0.80f, 2.22f, 1.87f, 0.62f, 0.0f, 0.10f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.28f, 0.72f, 380.0f, 0.7f,
		0.0f, 0.0f, 0.88f, 0.0f,
		0.0f, 0.0f, 0.18f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CATHEDRAL
	},

	// 11: 体育館 - 硬質金属的
	{
		0.49f, 59.0f, 0.57f,
		{ 16.0f, 0.61f, 24.0f, 0.58f, 38.0f, 0.54f, 55.0f, 0.51f, 72.0f, 0.46f, 92.0f, 0.42f, 115.0f, 0.36f, 142.0f, 0.31f },
		16800, 155, 1.22f, 0.16f, 2.12f, 0.30f, 0.63f, 0.0f, 0.22f, 1.50f, 0.57f, 0.62f, 0.15f,
		0.47f, 0.87f, 0.42f, 0.81f, 0.92f, 1.02f, 1.12f, 0.52f, 0.82f, 1.02f, 0.53f, 0.07f,
		0.12f, 0.23f, 0.98f, 0.12f, 0.17f, 1.12f, 1.32f, 0.32f, 0.35f, 0.25f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.22f, 0.78f, 4500.0f, 3.2f,
		0.92f, 0.08f, 0.05f, 0.22f,
		0.0f, 0.0f, 0.15f, 0.0f,
		0.38f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 12: 峡谷 - 複雑な両壁エコー（4タップ、強め）
	{
		0.22f, 270.0f, 0.23f,
		{ 98.0f, 0.27f, 158.0f, 0.23f, 235.0f, 0.22f, 315.0f, 0.19f, 405.0f, 0.16f, 505.0f, 0.14f, 615.0f, 0.11f, 735.0f, 0.09f },
		7600, 172, 2.20f, 0.60f, 0.18f, 0.0f, 0.71f, 27.0f, 0.34f, 3.32f, 0.28f, 0.59f, 0.66f,
		1.17f, 1.70f, 0.64f, 0.66f, 1.27f, 1.00f, 0.74f, 0.60f, 0.50f, 0.96f, 0.08f, 0.24f,
		0.60f, 0.42f, 0.66f, 0.44f, 0.74f, 1.54f, 2.02f, 0.84f, 0.19f, 0.21f,
		{ 280.0f, 560.0f, 840.0f, 1120.0f }, { 0.88f, 0.72f, 0.52f, 0.32f }, 0.75f, 0.68f,
		0.32f, 0.68f, 380.0f, 0.9f,
		0.0f, 0.0f, 0.0f, 0.82f,
		0.0f, 0.88f, 0.85f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CANYON_ECHO
	},

	// 13: 地下室 - 圧迫感・高密度
	{
		0.55f, 22.0f, 0.61f,
		{ 2.5f, 0.70f, 3.8f, 0.67f, 6.5f, 0.63f, 10.5f, 0.54f, 14.0f, 0.46f, 18.0f, 0.38f, 22.0f, 0.31f, 27.0f, 0.23f },
		3900, 45, 0.60f, 0.02f, 2.30f, 0.0f, 0.38f, 0.0f, 0.47f, 0.68f, 0.71f, 0.33f, 0.21f,
		0.62f, 1.02f, 0.37f, 0.28f, 1.32f, 1.02f, 0.72f, 0.42f, 0.32f, 0.71f, -0.52f, 0.62f,
		0.72f, 0.31f, 0.27f, 0.72f, 0.82f, 0.72f, 0.82f, 0.27f, 0.28f, 0.34f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.82f, 0.18f, 850.0f, 1.5f,
		0.0f, 0.0f, 0.0f, 0.88f,
		0.0f, 0.0f, 0.95f, 0.0f,
		0.32f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 14: 劇場 - 音響設計
	{
		0.46f, 75.0f, 0.38f,
		{ 11.0f, 0.42f, 17.0f, 0.37f, 36.0f, 0.33f, 58.0f, 0.28f, 82.0f, 0.24f, 108.0f, 0.20f, 138.0f, 0.15f, 172.0f, 0.13f },
		12200, 76, 1.58f, 0.41f, 1.07f, 0.28f, 0.67f, 9.2f, 0.37f, 1.27f, 0.51f, 0.80f, 0.25f,
		0.92f, 1.32f, 0.87f, 0.71f, 1.17f, 1.02f, 0.87f, 0.77f, 0.67f, 0.72f, 0.08f, 0.27f,
		0.27f, 0.47f, 0.67f, 0.57f, 0.52f, 1.27f, 1.17f, 0.62f, 0.0f, 0.12f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.52f, 0.48f, 1500.0f, 1.2f,
		0.0f, 0.0f, 0.62f, 0.0f,
		0.0f, 0.0f, 0.38f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 15: 水中 - 低域特殊
	{
		0.40f, 135.0f, 0.40f,
		{ 5.5f, 0.44f, 8.0f, 0.42f, 21.0f, 0.39f, 34.0f, 0.35f, 51.0f, 0.32f, 72.0f, 0.27f, 98.0f, 0.24f, 128.0f, 0.19f },
		4000, 310, 1.08f, 0.86f, 0.47f, 0.0f, 0.87f, 10.3f, 0.64f, 1.54f, 0.39f, 0.80f, 0.70f,
		1.27f, 1.70f, 0.40f, 0.44f, 1.72f, 1.10f, 0.50f, 0.80f, 0.30f, 0.51f, -0.74f, 0.40f,
		0.82f, 0.29f, 0.22f, 0.85f, 0.92f, 1.12f, 0.92f, 0.20f, 0.38f, 0.45f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.72f, 0.28f, 280.0f, 1.2f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.95f, 0.0f, 0.85f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_UNDERWATER
	},

	// 16: トンネル - フラッターエコー
	{
		0.50f, 118.0f, 0.48f,
		{ 11.0f, 0.54f, 17.0f, 0.52f, 23.0f, 0.51f, 29.0f, 0.49f, 36.0f, 0.46f, 44.0f, 0.43f, 53.0f, 0.39f, 63.0f, 0.35f },
		8600, 157, 0.62f, 0.36f, 1.30f, 0.18f, 0.54f, 6.7f, 0.32f, 1.03f, 0.63f, 0.45f, 0.20f,
		0.70f, 1.10f, 0.54f, 0.67f, 1.00f, 1.00f, 0.94f, 0.54f, 0.70f, 0.80f, -0.06f, 0.20f,
		0.34f, 0.36f, 0.63f, 0.30f, 0.60f, 0.84f, 0.74f, 0.40f, 0.63f, 0.20f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.54f, 0.46f, 1800.0f, 1.8f,
		0.0f, 0.0f, 0.0f, 0.78f,
		0.0f, 0.0f, 0.72f, 0.0f,
		0.68f, 0.0f, 0.0f, 0.0f,
		TYPE_CORRIDOR
	},

	// 17: アリーナ - 超大スポーツ
	{
		0.22f, 360.0f, 0.24f,
		{ 82.0f, 0.22f, 122.0f, 0.19f, 182.0f, 0.16f, 248.0f, 0.15f, 325.0f, 0.12f, 412.0f, 0.11f, 512.0f, 0.09f, 625.0f, 0.07f },
		5900, 39, 1.95f, 0.66f, 0.40f, 0.10f, 0.84f, 33.0f, 0.55f, 3.22f, 0.37f, 0.99f, 0.51f,
		1.82f, 2.37f, 0.80f, 0.52f, 1.52f, 1.04f, 0.74f, 0.80f, 0.54f, 0.59f, 0.07f, 0.20f,
		0.38f, 0.39f, 0.69f, 0.58f, 0.67f, 2.42f, 2.17f, 0.72f, 0.08f, 0.26f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.38f, 0.62f, 1500.0f, 0.9f,
		0.0f, 0.0f, 0.45f, 0.42f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CATHEDRAL
	},

	// 18: クローゼット - 超小デッド
	{
		0.12f, 5.8f, 0.08f,
		{ 1.5f, 0.22f, 2.5f, 0.18f, 4.0f, 0.12f, 6.0f, 0.06f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		4500, 70, 0.48f, 0.12f, 0.06f, 0.0f, 0.20f, 7.2f, 0.18f, 0.50f, 0.90f, 0.30f, 0.0f,
		0.22f, 0.32f, 0.97f, 0.55f, 0.68f, 1.02f, 1.12f, 0.18f, 0.18f, 0.92f, 0.12f, 0.87f,
		0.18f, 0.52f, 0.50f, 0.92f, 0.58f, 0.52f, 0.58f, 0.12f, 0.01f, 0.12f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.05f, 0.95f, 18000.0f, 0.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.98f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 19: 階段室 - 縦方向反射
	{
		0.49f, 110.0f, 0.53f,
		{ 14.0f, 0.54f, 22.0f, 0.50f, 38.0f, 0.46f, 56.0f, 0.40f, 76.0f, 0.34f, 98.0f, 0.29f, 124.0f, 0.24f, 154.0f, 0.18f },
		9700, 126, 0.96f, 0.52f, 1.85f, 0.20f, 0.55f, 13.5f, 0.48f, 1.07f, 0.72f, 0.69f, 0.36f,
		0.82f, 1.17f, 0.62f, 0.78f, 1.12f, 1.02f, 0.92f, 0.62f, 0.67f, 0.68f, 0.20f, 0.27f,
		0.42f, 0.33f, 0.69f, 0.42f, 0.57f, 1.42f, 0.97f, 0.47f, 0.15f, 0.21f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.48f, 0.52f, 2800.0f, 2.2f,
		0.0f, 0.0f, 0.0f, 0.68f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.28f, 0.0f, 0.0f, 0.0f,
		TYPE_CORRIDOR
	},

	// 20: 地下鉄ホーム - コンクリート
	{
		0.44f, 132.0f, 0.51f,
		{ 24.0f, 0.49f, 36.0f, 0.46f, 54.0f, 0.42f, 78.0f, 0.38f, 102.0f, 0.34f, 130.0f, 0.29f, 162.0f, 0.25f, 198.0f, 0.20f },
		10500, 102, 1.12f, 0.18f, 1.12f, 0.28f, 0.70f, 10.5f, 0.38f, 1.62f, 0.64f, 0.56f, 0.28f,
		0.77f, 1.12f, 0.57f, 0.59f, 1.07f, 1.02f, 0.97f, 0.62f, 0.67f, 0.85f, -0.08f, 0.22f,
		0.37f, 0.40f, 0.58f, 0.37f, 0.52f, 1.17f, 1.27f, 0.42f, 0.25f, 0.07f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.42f, 0.58f, 2200.0f, 1.5f,
		0.0f, 0.0f, 0.0f, 0.88f,
		0.0f, 0.0f, 0.65f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CORRIDOR
	},

	// 21: 倉庫 - 大空っぽ
	{
		0.31f, 132.0f, 0.33f,
		{ 52.0f, 0.35f, 82.0f, 0.32f, 128.0f, 0.29f, 175.0f, 0.26f, 228.0f, 0.22f, 285.0f, 0.19f, 348.0f, 0.15f, 418.0f, 0.12f },
		8400, 62, 1.48f, 0.55f, 0.64f, 0.08f, 0.71f, 10.3f, 0.68f, 1.69f, 0.30f, 0.69f, 0.63f,
		1.24f, 1.57f, 0.72f, 0.52f, 1.20f, 0.98f, 0.82f, 0.67f, 0.52f, 0.64f, 0.18f, 0.36f,
		0.48f, 0.70f, 0.51f, 0.56f, 0.58f, 1.50f, 1.60f, 0.68f, 0.25f, 0.27f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.58f, 0.42f, 1200.0f, 1.0f,
		0.0f, 0.0f, 0.0f, 0.75f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.38f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 22: 廊下 - 長く狭い
	{
		0.49f, 85.0f, 0.53f,
		{ 20.0f, 0.54f, 32.0f, 0.51f, 48.0f, 0.46f, 68.0f, 0.41f, 92.0f, 0.35f, 118.0f, 0.30f, 148.0f, 0.24f, 182.0f, 0.18f },
		9800, 116, 0.46f, 0.22f, 1.35f, 0.18f, 0.31f, 0.0f, 0.52f, 0.45f, 0.52f, 0.37f, 0.35f,
		0.84f, 1.17f, 0.64f, 0.45f, 1.10f, 1.00f, 0.90f, 0.50f, 0.60f, 0.66f, -0.02f, 0.40f,
		0.30f, 0.56f, 0.46f, 0.50f, 0.54f, 0.70f, 1.44f, 0.44f, 0.19f, 0.18f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.42f, 0.58f, 3200.0f, 1.8f,
		0.0f, 0.0f, 0.0f, 0.72f,
		0.0f, 0.0f, 0.68f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CORRIDOR
	},

	// 23: 工場 - 金属産業
	{
		0.48f, 88.0f, 0.49f,
		{ 22.0f, 0.56f, 35.0f, 0.54f, 54.0f, 0.51f, 80.0f, 0.45f, 108.0f, 0.40f, 140.0f, 0.34f, 178.0f, 0.29f, 220.0f, 0.23f },
		13300, 158, 1.20f, 0.40f, 2.15f, 0.34f, 0.56f, 6.5f, 0.59f, 1.27f, 0.44f, 0.54f, 0.54f,
		0.58f, 0.92f, 0.52f, 0.67f, 0.92f, 0.98f, 1.12f, 0.58f, 0.77f, 0.80f, 0.33f, 0.22f,
		0.32f, 0.55f, 0.69f, 0.27f, 0.32f, 1.20f, 1.27f, 0.42f, 0.35f, 0.23f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.38f, 0.62f, 3800.0f, 2.8f,
		0.85f, 0.05f, 0.0f, 0.62f,
		0.0f, 0.0f, 0.42f, 0.0f,
		0.42f, 0.0f, 0.0f, 0.0f,
		TYPE_METAL_SPACE
	},

	// 24: 寺社 - 木造温かみ
	{
		0.46f, 148.0f, 0.51f,
		{ 34.0f, 0.46f, 55.0f, 0.42f, 92.0f, 0.37f, 135.0f, 0.33f, 182.0f, 0.30f, 238.0f, 0.25f, 302.0f, 0.22f, 372.0f, 0.17f },
		6400, 52, 1.28f, 0.49f, 0.55f, 0.0f, 0.85f, 9.6f, 0.68f, 1.50f, 0.49f, 0.87f, 0.63f,
		1.37f, 1.57f, 0.84f, 0.42f, 1.30f, 1.00f, 0.74f, 0.80f, 0.50f, 0.63f, -0.05f, 0.44f,
		0.45f, 0.97f, 0.53f, 0.80f, 0.60f, 1.44f, 1.54f, 0.60f, 0.22f, 0.26f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.52f, 0.48f, 1200.0f, 1.0f,
		0.0f, 0.0f, 0.92f, 0.0f,
		0.0f, 0.0f, 0.48f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 25: 宇宙空間 - SF特殊
	{
		0.19f, 380.0f, 0.24f,
		{ 0.0f, 0.0f, 0.0f, 0.0f, 205.0f, 0.09f, 285.0f, 0.07f, 375.0f, 0.06f, 475.0f, 0.04f, 585.0f, 0.04f, 705.0f, 0.03f },
		17000, 24, 1.20f, 0.78f, 0.32f, 0.15f, 0.32f, 36.5f, 0.39f, 3.45f, 0.13f, 0.19f, 0.40f,
		0.94f, 1.14f, 0.27f, 0.30f, 0.77f, 0.94f, 1.14f, 0.37f, 0.38f, 1.02f, -0.07f, 0.12f,
		0.87f, 0.49f, 0.24f, 0.17f, 0.90f, 3.02f, 3.37f, 0.90f, 0.51f, 0.61f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.82f, 0.18f, 12000.0f, 0.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.95f, 0.0f,
		0.0f, 0.62f, 0.0f, 0.38f,
		TYPE_SF_SPACE
	},

	// 26: 野球場 - 屋外超大型（広場との差別化：より乾いた響き）
	{
		0.14f, 340.0f, 0.16f,
		{ 108.0f, 0.16f, 168.0f, 0.13f, 245.0f, 0.12f, 325.0f, 0.09f, 415.0f, 0.07f, 515.0f, 0.05f, 625.0f, 0.03f, 745.0f, 0.02f },
		17500, 122, 1.98f, 0.62f, 0.55f, 0.06f, 0.63f, 40.5f, 0.60f, 3.50f, 0.37f, 0.56f, 0.96f,
		1.04f, 1.42f, 0.67f, 0.61f, 1.12f, 0.98f, 0.82f, 0.64f, 0.58f, 0.76f, 0.20f, 0.27f,
		0.52f, 0.68f, 0.58f, 0.42f, 0.62f, 1.70f, 2.17f, 0.84f, 0.21f, 0.30f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.28f, 0.72f, 18000.0f, 0.6f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.98f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_OUTDOOR_OPEN
	},

	// 27: 図書館 - 静寂吸音
	{
		0.26f, 43.0f, 0.29f,
		{ 7.0f, 0.30f, 11.0f, 0.26f, 18.0f, 0.23f, 28.0f, 0.17f, 38.0f, 0.13f, 50.0f, 0.09f, 64.0f, 0.06f, 82.0f, 0.05f },
		7300, 72, 0.86f, 0.08f, 0.43f, 0.12f, 0.38f, 2.6f, 0.71f, 0.83f, 0.67f, 0.36f, 0.66f,
		0.67f, 0.82f, 0.92f, 0.36f, 0.92f, 1.02f, 1.02f, 0.42f, 0.42f, 0.69f, 0.03f, 0.77f,
		0.27f, 0.89f, 0.43f, 0.87f, 0.42f, 1.07f, 1.12f, 0.32f, 0.10f, 0.12f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.12f, 0.88f, 12000.0f, 0.7f,
		0.0f, 0.0f, 0.72f, 0.0f,
		0.0f, 0.0f, 0.88f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 28: プール(室内) - タイル水面
	{
		0.52f, 118.0f, 0.51f,
		{ 16.0f, 0.56f, 24.0f, 0.54f, 40.0f, 0.51f, 62.0f, 0.45f, 86.0f, 0.40f, 114.0f, 0.34f, 146.0f, 0.29f, 182.0f, 0.23f },
		9900, 280, 1.16f, 0.49f, 1.44f, 0.24f, 0.62f, 10.2f, 0.51f, 1.14f, 0.56f, 0.72f, 0.68f,
		0.78f, 1.20f, 0.56f, 0.59f, 1.12f, 0.98f, 0.88f, 0.68f, 0.72f, 0.73f, 0.38f, 0.20f,
		0.22f, 0.61f, 0.76f, 0.22f, 0.36f, 1.12f, 1.02f, 0.42f, 0.24f, 0.29f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.48f, 0.52f, 2800.0f, 2.5f,
		0.28f, 0.72f, 0.0f, 0.0f,
		0.72f, 0.0f, 0.58f, 0.0f,
		0.32f, 0.0f, 0.0f, 0.0f,
		TYPE_UNDERWATER
	},

	// 29: エレベーター - 超小金属（風呂との差別化：より金属的）
	{
		0.48f, 11.5f, 0.62f,
		{ 2.2f, 0.74f, 3.8f, 0.72f, 5.8f, 0.67f, 8.5f, 0.60f, 11.0f, 0.53f, 14.0f, 0.45f, 17.5f, 0.36f, 21.5f, 0.28f },
		17800, 210, 0.42f, 0.24f, 3.55f, 0.20f, 0.28f, 9.0f, 0.35f, 0.28f, 0.92f, 0.25f, 0.30f,
		0.32f, 0.52f, 0.28f, 0.72f, 0.82f, 0.98f, 1.12f, 0.28f, 0.48f, 0.88f, 0.35f, 0.06f,
		0.03f, 0.42f, 0.76f, 0.03f, 0.18f, 0.42f, 0.48f, 0.12f, 0.42f, 0.30f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.08f, 0.92f, 5200.0f, 4.5f,
		0.95f, 0.05f, 0.0f, 0.05f,
		0.0f, 0.0f, 0.98f, 0.0f,
		0.52f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 30: 駐車場 - 低天井コンクリート（地下室との差別化：より広い）
	{
		0.38f, 135.0f, 0.48f,
		{ 32.0f, 0.45f, 50.0f, 0.41f, 78.0f, 0.37f, 112.0f, 0.33f, 150.0f, 0.29f, 195.0f, 0.25f, 248.0f, 0.21f, 308.0f, 0.16f },
		9400, 92, 1.18f, 0.27f, 0.78f, 0.16f, 0.50f, 11.8f, 0.66f, 1.27f, 0.60f, 0.52f, 0.59f,
		0.84f, 1.14f, 0.58f, 0.40f, 1.08f, 0.98f, 0.92f, 0.58f, 0.62f, 0.64f, 0.10f, 0.32f,
		0.38f, 0.58f, 0.51f, 0.46f, 0.52f, 0.82f, 1.34f, 0.46f, 0.14f, 0.18f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.52f, 0.48f, 2200.0f, 1.5f,
		0.0f, 0.0f, 0.0f, 0.92f,
		0.0f, 0.0f, 0.62f, 0.0f,
		0.28f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 31: コンサートホール - クラシック最高峰（ホールとの差別化：より洗練）
	{
		0.44f, 98.0f, 0.49f,
		{ 20.0f, 0.48f, 32.0f, 0.42f, 55.0f, 0.39f, 85.0f, 0.33f, 118.0f, 0.29f, 158.0f, 0.25f, 205.0f, 0.20f, 262.0f, 0.16f },
		13400, 52, 1.50f, 0.32f, 0.63f, 0.20f, 0.86f, 4.5f, 0.49f, 1.70f, 0.40f, 1.02f, 0.39f,
		1.40f, 1.77f, 0.87f, 0.57f, 1.34f, 0.98f, 0.78f, 0.82f, 0.58f, 0.70f, 0.12f, 0.30f,
		0.32f, 0.70f, 0.72f, 0.62f, 0.52f, 1.47f, 1.34f, 0.62f, 0.04f, 0.21f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 1800.0f, 1.1f,
		0.0f, 0.0f, 0.75f, 0.0f,
		0.0f, 0.0f, 0.42f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 32: ジャズクラブ - 親密温かい
	{
		0.48f, 55.0f, 0.46f,
		{ 10.0f, 0.63f, 15.5f, 0.58f, 26.0f, 0.52f, 41.0f, 0.45f, 56.0f, 0.39f, 74.0f, 0.33f, 95.0f, 0.26f, 120.0f, 0.20f },
		8700, 96, 1.35f, 0.30f, 1.40f, 0.30f, 0.65f, 1.8f, 0.42f, 0.95f, 0.56f, 0.64f, 0.30f,
		0.92f, 1.17f, 0.77f, 0.73f, 1.17f, 1.02f, 0.87f, 0.62f, 0.57f, 0.73f, -0.10f, 0.47f,
		0.37f, 0.78f, 0.67f, 0.72f, 0.47f, 0.92f, 0.97f, 0.52f, 0.08f, 0.09f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.58f, 0.42f, 2400.0f, 1.3f,
		0.0f, 0.0f, 0.82f, 0.0f,
		0.0f, 0.0f, 0.52f, 0.0f,
		0.12f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 33: カラオケボックス - 小密室
	{
		0.56f, 32.0f, 0.58f,
		{ 5.5f, 0.71f, 8.5f, 0.66f, 14.0f, 0.62f, 21.0f, 0.55f, 28.0f, 0.49f, 36.0f, 0.42f, 46.0f, 0.35f, 58.0f, 0.27f },
		13500, 121, 0.85f, 0.15f, 2.12f, 0.38f, 0.51f, 0.0f, 0.25f, 0.71f, 0.58f, 0.52f, 0.15f,
		0.62f, 0.87f, 0.57f, 0.70f, 0.97f, 1.02f, 1.07f, 0.52f, 0.67f, 0.80f, 0.08f, 0.52f,
		0.27f, 0.55f, 0.76f, 0.52f, 0.42f, 0.72f, 0.77f, 0.32f, 0.12f, 0.10f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.42f, 0.58f, 3800.0f, 2.2f,
		0.0f, 0.0f, 0.55f, 0.0f,
		0.0f, 0.0f, 0.82f, 0.0f,
		0.18f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 34: 映画館 - THX規格
	{
		0.60f, 125.0f, 0.55f,
		{ 25.0f, 0.51f, 40.0f, 0.44f, 68.0f, 0.39f, 98.0f, 0.33f, 135.0f, 0.28f, 178.0f, 0.24f, 228.0f, 0.19f, 286.0f, 0.15f },
		12000, 68, 1.54f, 0.38f, 0.79f, 0.30f, 0.80f, 8.7f, 0.53f, 1.64f, 0.51f, 0.96f, 0.43f,
		1.22f, 1.52f, 0.82f, 0.64f, 1.27f, 1.02f, 0.87f, 0.77f, 0.67f, 0.69f, 0.16f, 0.32f,
		0.32f, 0.59f, 0.77f, 0.62f, 0.52f, 1.37f, 1.32f, 0.57f, 0.0f, 0.21f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 2200.0f, 1.2f,
		0.0f, 0.0f, 0.68f, 0.0f,
		0.0f, 0.0f, 0.48f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 35: 地下鉄車内 - 揺れる密室
	{
		0.46f, 21.0f, 0.60f,
		{ 4.5f, 0.65f, 7.5f, 0.61f, 11.5f, 0.54f, 17.0f, 0.47f, 23.0f, 0.40f, 30.0f, 0.33f, 38.0f, 0.25f, 48.0f, 0.18f },
		6300, 157, 0.50f, 0.26f, 2.80f, 0.20f, 0.56f, 0.0f, 0.35f, 0.66f, 0.73f, 0.38f, 0.21f,
		0.57f, 0.77f, 0.42f, 0.52f, 0.92f, 1.02f, 1.02f, 0.42f, 0.52f, 0.87f, -0.11f, 0.57f,
		0.52f, 0.52f, 0.47f, 0.67f, 0.67f, 0.67f, 0.72f, 0.27f, 0.49f, 0.24f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 2800.0f, 1.8f,
		0.45f, 0.0f, 0.0f, 0.65f,
		0.0f, 0.0f, 0.95f, 0.0f,
		0.0f, 0.42f, 0.0f, 0.22f,
		TYPE_SMALL_ROOM
	},

	// 36: 空港ターミナル - 巨大公共
	{
		0.25f, 205.0f, 0.24f,
		{ 58.0f, 0.27f, 98.0f, 0.23f, 158.0f, 0.21f, 218.0f, 0.17f, 288.0f, 0.15f, 368.0f, 0.12f, 458.0f, 0.10f, 558.0f, 0.07f },
		10800, 85, 1.59f, 0.40f, 0.62f, 0.10f, 0.73f, 19.5f, 0.55f, 2.23f, 0.43f, 0.80f, 0.53f,
		1.32f, 1.67f, 0.74f, 0.61f, 1.27f, 1.00f, 0.84f, 0.70f, 0.64f, 0.64f, 0.10f, 0.32f,
		0.40f, 0.50f, 0.69f, 0.50f, 0.54f, 1.72f, 1.82f, 0.70f, 0.09f, 0.19f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.48f, 0.52f, 2200.0f, 1.0f,
		0.0f, 0.0f, 0.0f, 0.72f,
		0.0f, 0.0f, 0.62f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CATHEDRAL
	},

	// 37: ショッピングモール - 賑やか商業（大型との差別化：より活気）
	{
		0.36f, 148.0f, 0.39f,
		{ 32.0f, 0.43f, 48.0f, 0.37f, 78.0f, 0.33f, 112.0f, 0.28f, 152.0f, 0.24f, 198.0f, 0.20f, 252.0f, 0.17f, 315.0f, 0.13f },
		10500, 93, 1.47f, 0.34f, 0.93f, 0.20f, 0.74f, 12.0f, 0.56f, 1.58f, 0.57f, 0.92f, 0.46f,
		1.17f, 1.42f, 0.72f, 0.65f, 1.22f, 1.02f, 0.92f, 0.67f, 0.62f, 0.73f, 0.32f, 0.37f,
		0.37f, 0.58f, 0.82f, 0.57f, 0.52f, 1.32f, 1.37f, 0.62f, 0.10f, 0.28f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.58f, 0.42f, 2800.0f, 1.2f,
		0.0f, 0.0f, 0.42f, 0.48f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.22f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 38: 病院 - 静寂清潔
	{
		0.32f, 61.0f, 0.36f,
		{ 11.0f, 0.39f, 17.0f, 0.33f, 28.0f, 0.28f, 45.0f, 0.23f, 62.0f, 0.18f, 82.0f, 0.15f, 106.0f, 0.11f, 135.0f, 0.08f },
		9000, 78, 1.18f, 0.21f, 0.61f, 0.12f, 0.62f, 10.9f, 0.48f, 1.18f, 0.70f, 0.44f, 0.38f,
		0.72f, 0.92f, 0.87f, 0.66f, 0.97f, 1.02f, 1.02f, 0.47f, 0.47f, 0.78f, -0.17f, 0.72f,
		0.32f, 0.78f, 0.54f, 0.77f, 0.47f, 1.02f, 1.07f, 0.37f, 0.15f, 0.03f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.28f, 0.72f, 8500.0f, 0.8f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.88f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 39: レコーディングブース - 極ドライプロ（スタジオとの差別化：より無響）
	{
		0.02f, 6.2f, 0.005f,
		{ 1.2f, 0.09f, 2.2f, 0.07f, 3.5f, 0.04f, 5.5f, 0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		19900, 22, 1.00f, 0.0f, 0.04f, 0.48f, 0.22f, 3.9f, 0.0f, 0.66f, 1.02f, 0.08f, 0.0f,
		0.12f, 0.22f, 0.99f, 0.50f, 0.72f, 1.00f, 1.02f, 0.02f, 0.02f, 0.98f, -0.10f, 0.96f,
		0.02f, 0.70f, 0.45f, 0.96f, 0.32f, 0.78f, 0.72f, 0.02f, 0.08f, 0.0f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.01f, 0.99f, 19500.0f, 0.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.99f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 40: オペラハウス - 劇場最高峰
	{
		0.35f, 158.0f, 0.36f,
		{ 28.0f, 0.40f, 45.0f, 0.35f, 78.0f, 0.33f, 122.0f, 0.28f, 172.0f, 0.25f, 232.0f, 0.21f, 302.0f, 0.18f, 385.0f, 0.14f },
		11300, 47, 1.51f, 0.36f, 0.62f, 0.12f, 0.99f, 16.0f, 0.54f, 2.05f, 0.60f, 1.02f, 0.44f,
		1.54f, 1.97f, 0.84f, 0.51f, 1.38f, 0.98f, 0.72f, 0.82f, 0.55f, 0.71f, 0.05f, 0.27f,
		0.36f, 0.81f, 0.69f, 0.68f, 0.58f, 1.60f, 1.50f, 0.62f, 0.12f, 0.21f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.58f, 0.42f, 1500.0f, 1.0f,
		0.0f, 0.0f, 0.82f, 0.0f,
		0.0f, 0.0f, 0.38f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CATHEDRAL
	},

	// 41: カフェ - 適度賑わい
	{
		0.40f, 45.0f, 0.38f,
		{ 8.5f, 0.51f, 13.0f, 0.46f, 21.0f, 0.41f, 35.0f, 0.35f, 48.0f, 0.30f, 64.0f, 0.24f, 83.0f, 0.19f, 106.0f, 0.15f },
		8000, 96, 1.01f, 0.23f, 0.92f, 0.20f, 0.45f, 0.0f, 0.69f, 0.65f, 0.54f, 0.53f, 0.61f,
		0.77f, 1.02f, 0.82f, 0.52f, 1.07f, 1.02f, 0.97f, 0.57f, 0.52f, 0.62f, 0.20f, 0.57f,
		0.37f, 0.97f, 0.59f, 0.77f, 0.47f, 0.97f, 1.02f, 0.47f, 0.12f, 0.21f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.38f, 0.62f, 4200.0f, 1.2f,
		0.0f, 0.0f, 0.72f, 0.0f,
		0.0f, 0.0f, 0.68f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 42: バー - 暗く落ち着き
	{
		0.46f, 59.0f, 0.44f,
		{ 10.5f, 0.56f, 16.0f, 0.51f, 27.0f, 0.45f, 46.0f, 0.40f, 64.0f, 0.34f, 86.0f, 0.29f, 112.0f, 0.24f, 144.0f, 0.18f },
		6800, 118, 0.98f, 0.23f, 1.15f, 0.24f, 0.54f, 1.5f, 0.63f, 0.60f, 0.62f, 0.57f, 0.55f,
		0.92f, 1.17f, 0.77f, 0.44f, 1.17f, 1.02f, 0.87f, 0.60f, 0.50f, 0.62f, 0.05f, 0.62f,
		0.42f, 1.02f, 0.44f, 0.82f, 0.52f, 0.87f, 0.92f, 0.42f, 0.19f, 0.23f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.52f, 0.48f, 3200.0f, 1.3f,
		0.0f, 0.0f, 0.88f, 0.0f,
		0.0f, 0.0f, 0.72f, 0.0f,
		0.22f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 43: 居酒屋 - 賑やか木材
	{
		0.43f, 44.0f, 0.41f,
		{ 8.0f, 0.53f, 12.5f, 0.48f, 20.0f, 0.42f, 32.0f, 0.37f, 45.0f, 0.32f, 60.0f, 0.26f, 78.0f, 0.21f, 100.0f, 0.15f },
		7500, 113, 0.89f, 0.19f, 1.19f, 0.21f, 0.44f, 0.0f, 0.73f, 0.64f, 0.60f, 0.45f, 0.66f,
		0.82f, 1.07f, 0.77f, 0.51f, 1.12f, 1.02f, 0.92f, 0.54f, 0.50f, 0.68f, 0.16f, 0.62f,
		0.42f, 1.02f, 0.60f, 0.82f, 0.52f, 0.92f, 0.97f, 0.42f, 0.17f, 0.21f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.48f, 0.52f, 3800.0f, 1.4f,
		0.0f, 0.0f, 0.82f, 0.0f,
		0.0f, 0.0f, 0.72f, 0.0f,
		0.18f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 44: 美術館 - 静か広い高天井
	{
		0.44f, 124.0f, 0.46f,
		{ 35.0f, 0.40f, 58.0f, 0.34f, 98.0f, 0.30f, 145.0f, 0.25f, 198.0f, 0.21f, 258.0f, 0.17f, 328.0f, 0.14f, 408.0f, 0.11f },
		9600, 74, 1.30f, 0.44f, 0.63f, 0.15f, 0.57f, 13.6f, 0.74f, 1.35f, 0.45f, 0.74f, 0.75f,
		1.20f, 1.47f, 0.84f, 0.48f, 1.22f, 1.00f, 0.84f, 0.70f, 0.60f, 0.55f, 0.20f, 0.54f,
		0.34f, 0.85f, 0.60f, 0.70f, 0.54f, 1.60f, 1.44f, 0.60f, 0.07f, 0.24f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.52f, 0.48f, 2800.0f, 1.0f,
		0.0f, 0.0f, 0.68f, 0.0f,
		0.0f, 0.0f, 0.62f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 45: 講堂 - 教育施設
	{
		0.54f, 95.0f, 0.57f,
		{ 19.0f, 0.54f, 28.5f, 0.49f, 48.5f, 0.43f, 79.0f, 0.38f, 110.0f, 0.33f, 149.0f, 0.27f, 194.0f, 0.22f, 246.0f, 0.17f },
		10900, 79, 1.25f, 0.38f, 0.90f, 0.24f, 0.72f, 9.7f, 0.68f, 1.20f, 0.55f, 0.66f, 0.61f,
		1.02f, 1.32f, 0.77f, 0.60f, 1.22f, 1.02f, 0.92f, 0.70f, 0.62f, 0.75f, 0.17f, 0.37f,
		0.32f, 0.95f, 0.60f, 0.62f, 0.52f, 1.27f, 1.22f, 0.57f, 0.22f, 0.19f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.58f, 0.42f, 3200.0f, 1.3f,
		0.0f, 0.0f, 0.52f, 0.0f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 46: 竹林 - 和風自然
	{
		0.28f, 178.0f, 0.21f,
		{ 42.0f, 0.27f, 68.0f, 0.24f, 118.0f, 0.22f, 172.0f, 0.21f, 232.0f, 0.19f, 298.0f, 0.15f, 372.0f, 0.14f, 455.0f, 0.11f },
		4100, 227, 1.74f, 0.68f, 0.32f, 0.03f, 0.77f, 16.4f, 0.66f, 1.52f, 0.47f, 0.94f, 0.87f,
		1.42f, 1.72f, 0.87f, 0.49f, 1.27f, 1.02f, 0.77f, 0.90f, 0.47f, 0.48f, -0.12f, 0.77f,
		0.67f, 0.98f, 0.51f, 0.87f, 0.47f, 1.52f, 1.62f, 0.77f, 0.15f, 0.35f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.78f, 0.22f, 580.0f, 0.8f,
		0.0f, 0.0f, 0.92f, 0.0f,
		0.0f, 0.0f, 0.88f, 0.58f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_OUTDOOR_OPEN
	},

	// 47: 渓谷 - 水の反射
	{
		0.28f, 255.0f, 0.25f,
		{ 98.0f, 0.28f, 158.0f, 0.26f, 238.0f, 0.23f, 328.0f, 0.19f, 428.0f, 0.17f, 538.0f, 0.15f, 658.0f, 0.12f, 788.0f, 0.09f },
		5200, 207, 1.70f, 0.53f, 0.31f, 0.02f, 0.48f, 27.5f, 0.72f, 2.15f, 0.39f, 0.50f, 0.90f,
		1.22f, 1.72f, 0.70f, 0.52f, 1.30f, 1.00f, 0.70f, 0.72f, 0.54f, 0.56f, -0.35f, 0.40f,
		0.67f, 0.66f, 0.46f, 0.60f, 0.70f, 1.77f, 2.02f, 0.80f, 0.10f, 0.21f,
		{ 380.0f, 760.0f, 1140.0f, 1520.0f }, { 0.72f, 0.48f, 0.28f, 0.15f }, 0.85f, 0.55f,
		0.62f, 0.38f, 680.0f, 0.9f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.88f, 0.0f, 0.78f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_OUTDOOR_OPEN
	},

	// 48: 砂漠 - 超開放反射極小
	{
		0.07f, 380.0f, 0.05f,
		{ 0.0f, 0.0f, 0.0f, 0.0f, 235.0f, 0.05f, 395.0f, 0.04f, 568.0f, 0.03f, 755.0f, 0.02f, 958.0f, 0.01f, 1178.0f, 0.01f },
		19900, 158, 2.18f, 0.22f, 0.22f, 0.02f, 0.16f, 45.0f, 0.48f, 3.72f, 0.23f, 0.17f, 1.02f,
		0.72f, 0.92f, 0.62f, 0.68f, 0.82f, 1.02f, 1.22f, 0.32f, 0.52f, 0.81f, 0.22f, 0.07f,
		0.87f, 0.46f, 0.72f, 0.17f, 0.92f, 2.52f, 3.22f, 0.97f, 0.0f, 0.34f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.05f, 0.95f, 19200.0f, 0.4f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.98f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_OUTDOOR_OPEN
	},

	// 49: ガレージ - 車庫硬質
	{
		0.53f, 101.0f, 0.60f,
		{ 18.0f, 0.60f, 28.0f, 0.56f, 46.0f, 0.51f, 72.0f, 0.45f, 98.0f, 0.40f, 128.0f, 0.34f, 162.0f, 0.29f, 202.0f, 0.24f },
		12900, 141, 0.93f, 0.21f, 1.65f, 0.26f, 0.57f, 9.8f, 0.73f, 1.01f, 0.70f, 0.54f, 0.66f,
		0.77f, 1.07f, 0.57f, 0.59f, 1.07f, 1.02f, 1.02f, 0.60f, 0.72f, 0.84f, 0.30f, 0.22f,
		0.32f, 0.74f, 0.72f, 0.32f, 0.47f, 0.97f, 1.07f, 0.37f, 0.31f, 0.24f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.48f, 0.52f, 3800.0f, 2.2f,
		0.68f, 0.12f, 0.0f, 0.52f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.38f, 0.0f, 0.0f, 0.0f,
		TYPE_METAL_SPACE
	},

	// 50: 展望台 - 高所開放（広場・野球場との差別化：より高所感）
	{
		0.12f, 390.0f, 0.10f,
		{ 85.0f, 0.15f, 145.0f, 0.12f, 228.0f, 0.11f, 335.0f, 0.08f, 458.0f, 0.07f, 598.0f, 0.05f, 755.0f, 0.03f, 932.0f, 0.02f },
		19500, 116, 2.05f, 0.60f, 0.48f, 0.06f, 0.53f, 44.5f, 0.62f, 2.82f, 0.44f, 0.66f, 0.98f,
		0.89f, 1.24f, 0.79f, 0.72f, 0.99f, 1.04f, 1.09f, 0.64f, 0.74f, 0.78f, 0.48f, 0.19f,
		0.64f, 0.68f, 0.79f, 0.39f, 0.74f, 2.04f, 2.34f, 0.94f, 0.17f, 0.46f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.18f, 0.82f, 18500.0f, 0.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.92f, 0.95f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_OUTDOOR_OPEN
	},

	// 51: 小礼拝堂 - 教会より親密
	{
		0.42f, 108.0f, 0.51f,
		{ 24.0f, 0.44f, 38.0f, 0.40f, 65.0f, 0.35f, 100.0f, 0.34f, 138.0f, 0.27f, 185.0f, 0.24f, 238.0f, 0.19f, 302.0f, 0.16f },
		6850, 42, 1.45f, 0.42f, 0.48f, 0.12f, 0.92f, 3.4f, 0.40f, 1.94f, 0.26f, 0.89f, 0.30f,
		1.38f, 1.82f, 0.84f, 0.52f, 1.50f, 1.08f, 0.72f, 0.78f, 0.52f, 0.69f, -0.20f, 0.36f,
		0.38f, 0.60f, 0.61f, 0.72f, 0.68f, 1.57f, 1.47f, 0.62f, 0.12f, 0.20f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.42f, 0.58f, 1200.0f, 0.9f,
		0.0f, 0.0f, 0.88f, 0.0f,
		0.0f, 0.0f, 0.48f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CATHEDRAL
	},

	// 52: 大型ショッピングセンター - モールより巨大・より冷たい
	{
		0.38f, 135.0f, 0.42f,
		{ 52.0f, 0.42f, 82.0f, 0.37f, 128.0f, 0.31f, 182.0f, 0.27f, 245.0f, 0.22f, 318.0f, 0.18f, 402.0f, 0.14f, 502.0f, 0.11f },
		11200, 92, 1.78f, 0.48f, 0.68f, 0.24f, 0.76f, 11.5f, 0.36f, 2.05f, 0.42f, 0.73f, 0.26f,
		1.19f, 1.52f, 0.72f, 0.77f, 1.22f, 1.00f, 0.90f, 0.67f, 0.62f, 0.76f, 0.02f, 0.40f,
		0.38f, 0.45f, 0.72f, 0.56f, 0.56f, 1.44f, 1.54f, 0.64f, 0.16f, 0.20f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 1800.0f, 1.1f,
		0.0f, 0.0f, 0.28f, 0.58f,
		0.0f, 0.0f, 0.45f, 0.0f,
		0.28f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 53: 地下洞窟(深層) - より深く神秘的
	{
		0.44f, 218.0f, 0.50f,
		{ 45.0f, 0.60f, 90.0f, 0.54f, 160.0f, 0.49f, 240.0f, 0.44f, 330.0f, 0.38f, 430.0f, 0.33f, 540.0f, 0.28f, 660.0f, 0.24f },
		3500, 145, 1.76f, 0.65f, 0.45f, 0.03f, 0.90f, 16.5f, 0.74f, 2.28f, 0.30f, 0.90f, 0.57f,
		1.58f, 2.28f, 0.44f, 0.40f, 1.68f, 0.94f, 0.50f, 0.84f, 0.30f, 0.54f, -0.54f, 0.64f,
		0.84f, 0.20f, 0.44f, 0.74f, 0.94f, 1.88f, 1.95f, 0.34f, 0.24f, 0.50f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.72f, 0.28f, 280.0f, 2.2f,
		0.0f, 0.0f, 0.0f, 0.92f,
		0.92f, 0.0f, 0.82f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.12f,
		TYPE_CAVE
	},

	// 54: 古城の大広間 - 石造り中世的
	{
		0.32f, 185.0f, 0.30f,
		{ 43.0f, 0.34f, 75.0f, 0.31f, 128.0f, 0.28f, 192.0f, 0.24f, 265.0f, 0.21f, 348.0f, 0.18f, 442.0f, 0.15f, 548.0f, 0.12f },
		4700, 63, 1.86f, 0.61f, 0.36f, 0.06f, 0.94f, 19.0f, 0.54f, 2.48f, 0.34f, 0.97f, 0.44f,
		1.78f, 2.35f, 0.80f, 0.64f, 1.65f, 1.00f, 0.64f, 0.87f, 0.50f, 0.60f, -0.30f, 0.40f,
		0.64f, 0.50f, 0.64f, 0.84f, 0.87f, 2.18f, 1.88f, 0.60f, 0.14f, 0.30f,
		{ 280.0f, 560.0f, 840.0f, 0.0f }, { 0.68f, 0.42f, 0.24f, 0.0f }, 1.05f, 0.48f,
		0.58f, 0.42f, 680.0f, 1.5f,
		0.0f, 0.0f, 0.0f, 0.85f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.28f, 0.0f, 0.0f, 0.0f,
		TYPE_CATHEDRAL
	},

	// 55: 野外音楽堂 - 半開放的ステージ
	{
		0.24f, 158.0f, 0.20f,
		{ 28.0f, 0.30f, 48.0f, 0.26f, 82.0f, 0.23f, 130.0f, 0.21f, 185.0f, 0.17f, 245.0f, 0.14f, 315.0f, 0.12f, 395.0f, 0.09f },
		16200, 73, 1.86f, 0.45f, 0.51f, 0.08f, 0.75f, 12.8f, 0.25f, 2.18f, 0.41f, 0.85f, 0.45f,
		1.25f, 1.58f, 0.85f, 0.71f, 1.21f, 1.03f, 0.95f, 0.78f, 0.75f, 0.81f, 0.10f, 0.25f,
		0.51f, 0.51f, 0.85f, 0.55f, 0.65f, 1.65f, 1.78f, 0.81f, 0.15f, 0.31f,
		{ 220.0f, 440.0f, 0.0f, 0.0f }, { 0.58f, 0.32f, 0.0f, 0.0f }, 1.15f, 0.32f,
		0.45f, 0.55f, 14500.0f, 0.7f,
		0.0f, 0.0f, 0.52f, 0.0f,
		0.0f, 0.0f, 0.82f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_OUTDOOR_OPEN
	},

	// 56: 鍾乳洞 - 複雑な水滴反射
	{
		0.50f, 218.0f, 0.57f,
		{ 50.0f, 0.60f, 80.0f, 0.56f, 140.0f, 0.52f, 210.0f, 0.48f, 290.0f, 0.42f, 380.0f, 0.36f, 480.0f, 0.31f, 590.0f, 0.26f },
		3700, 145, 1.36f, 0.71f, 0.65f, 0.04f, 1.15f, 16.8f, 0.65f, 2.48f, 0.45f, 0.95f, 0.65f,
		1.65f, 2.15f, 0.50f, 0.45f, 1.65f, 1.00f, 0.60f, 0.91f, 0.41f, 0.65f, -0.60f, 0.58f,
		0.88f, 0.35f, 0.45f, 0.78f, 0.95f, 1.65f, 1.75f, 0.41f, 0.45f, 0.51f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.68f, 0.32f, 420.0f, 2.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.95f, 0.0f, 0.78f, 0.0f,
		0.42f, 0.0f, 0.0f, 0.52f,
		TYPE_CAVE
	},

	// 57: 廃墟工場 - 荒廃した金属空間
	{
		0.48f, 168.0f, 0.55f,
		{ 40.0f, 0.57f, 65.0f, 0.54f, 110.0f, 0.50f, 165.0f, 0.46f, 230.0f, 0.40f, 305.0f, 0.34f, 390.0f, 0.29f, 485.0f, 0.24f },
		12800, 185, 1.46f, 0.41f, 2.20f, 0.38f, 0.95f, 11.2f, 0.38f, 2.18f, 0.55f, 0.85f, 0.28f,
		0.75f, 1.15f, 0.61f, 0.78f, 1.08f, 1.03f, 1.15f, 0.65f, 0.78f, 0.98f, 0.48f, 0.35f,
		0.41f, 0.45f, 0.95f, 0.35f, 0.41f, 1.48f, 1.58f, 0.51f, 0.41f, 0.41f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.52f, 0.48f, 4200.0f, 3.5f,
		0.88f, 0.05f, 0.0f, 0.45f,
		0.0f, 0.0f, 0.28f, 0.15f,
		0.55f, 0.0f, 0.22f, 0.0f,
		TYPE_METAL_SPACE
	},

	// 58: 和室(畳) - 日本的柔らかい吸音
	{
		0.20f, 40.0f, 0.20f,
		{ 6.0f, 0.30f, 10.0f, 0.26f, 16.0f, 0.22f, 26.0f, 0.18f, 36.0f, 0.14f, 48.0f, 0.10f, 62.0f, 0.07f, 80.0f, 0.04f },
		6900, 58, 0.80f, 0.08f, 0.70f, 0.03f, 0.36f, 2.0f, 0.46f, 0.86f, 0.84f, 0.36f, 0.30f,
		0.63f, 0.80f, 0.90f, 0.50f, 1.03f, 0.98f, 0.90f, 0.36f, 0.33f, 0.60f, -0.16f, 0.80f,
		0.30f, 0.70f, 0.60f, 0.90f, 0.50f, 0.90f, 0.86f, 0.30f, 0.03f, 0.08f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.28f, 0.72f, 5800.0f, 0.8f,
		0.0f, 0.0f, 0.95f, 0.0f,
		0.0f, 0.0f, 0.30f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 59: 温泉施設 - 湿度高めタイル反射
	{
		0.58f, 108.0f, 0.61f,
		{ 17.0f, 0.61f, 26.0f, 0.57f, 42.0f, 0.53f, 64.0f, 0.48f, 89.0f, 0.43f, 117.0f, 0.37f, 150.0f, 0.32f, 188.0f, 0.27f },
		8900, 308, 1.48f, 0.65f, 1.42f, 0.24f, 0.95f, 14.8f, 0.31f, 1.65f, 0.65f, 0.71f, 0.38f,
		0.91f, 1.28f, 0.61f, 0.91f, 1.21f, 1.03f, 0.91f, 0.71f, 0.75f, 0.95f, 0.08f, 0.25f,
		0.31f, 0.55f, 0.85f, 0.35f, 0.41f, 1.15f, 1.08f, 0.45f, 0.35f, 0.21f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.55f, 0.45f, 2600.0f, 2.8f,
		0.32f, 0.68f, 0.0f, 0.0f,
		0.78f, 0.0f, 0.61f, 0.0f,
		0.38f, 0.0f, 0.0f, 0.0f,
		TYPE_UNDERWATER
	},

	// 60: 屋根裏部屋 - 斜め天井の特殊空間
	{
		0.47f, 65.0f, 0.54f,
		{ 10.0f, 0.52f, 16.0f, 0.48f, 26.0f, 0.42f, 42.0f, 0.36f, 60.0f, 0.31f, 82.0f, 0.26f, 108.0f, 0.20f, 138.0f, 0.16f },
		7700, 93, 1.15f, 0.41f, 1.01f, 0.18f, 0.85f, 10.6f, 0.35f, 1.45f, 0.81f, 0.59f, 0.21f,
		0.91f, 1.15f, 0.75f, 0.71f, 1.15f, 1.03f, 0.98f, 0.55f, 0.58f, 0.95f, 0.04f, 0.58f,
		0.48f, 0.75f, 0.75f, 0.75f, 0.58f, 0.81f, 1.25f, 0.48f, 0.35f, 0.25f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 2800.0f, 1.5f,
		0.0f, 0.0f, 0.75f, 0.0f,
		0.0f, 0.0f, 0.78f, 0.0f,
		0.35f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 61: 地下駐車場(多層) - 階層的複雑反射
	{
		0.34f, 125.0f, 0.40f,
		{ 36.0f, 0.44f, 58.0f, 0.40f, 92.0f, 0.37f, 135.0f, 0.34f, 185.0f, 0.30f, 245.0f, 0.26f, 315.0f, 0.21f, 395.0f, 0.17f },
		8900, 98, 1.58f, 0.45f, 0.75f, 0.16f, 0.85f, 3.8f, 0.55f, 1.85f, 0.41f, 0.61f, 0.45f,
		0.98f, 1.31f, 0.65f, 0.61f, 1.15f, 1.01f, 0.98f, 0.65f, 0.68f, 0.98f, 0.00f, 0.38f,
		0.45f, 0.65f, 0.61f, 0.51f, 0.58f, 0.98f, 1.55f, 0.55f, 0.41f, 0.25f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.58f, 0.42f, 2400.0f, 1.6f,
		0.0f, 0.0f, 0.0f, 0.95f,
		0.0f, 0.0f, 0.68f, 0.0f,
		0.42f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 62: 古い劇場(木造) - 温かみある音響設計
	{
		0.38f, 98.0f, 0.38f,
		{ 20.0f, 0.43f, 32.0f, 0.39f, 52.0f, 0.35f, 82.0f, 0.31f, 118.0f, 0.27f, 160.0f, 0.23f, 210.0f, 0.19f, 268.0f, 0.15f },
		9700, 78, 1.65f, 0.51f, 1.08f, 0.20f, 0.85f, 6.8f, 0.58f, 1.45f, 0.51f, 1.05f, 0.48f,
		1.31f, 1.58f, 0.88f, 0.61f, 1.35f, 1.01f, 0.85f, 0.81f, 0.65f, 0.75f, 0.16f, 0.45f,
		0.41f, 0.75f, 0.71f, 0.75f, 0.61f, 1.35f, 1.28f, 0.61f, 0.18f, 0.35f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.61f, 0.39f, 2200.0f, 1.2f,
		0.0f, 0.0f, 0.95f, 0.0f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.22f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 63: 大型倉庫(空) - 極端な空虚感
	{
		0.16f, 225.0f, 0.16f,
		{ 95.0f, 0.20f, 158.0f, 0.18f, 242.0f, 0.16f, 345.0f, 0.13f, 465.0f, 0.10f, 598.0f, 0.08f, 745.0f, 0.06f, 908.0f, 0.04f },
		7500, 43, 1.91f, 0.75f, 0.33f, 0.03f, 0.58f, 19.0f, 0.43f, 2.35f, 0.23f, 0.68f, 0.33f,
		1.13f, 1.58f, 0.63f, 0.78f, 1.13f, 0.93f, 0.73f, 0.58f, 0.43f, 0.73f, 0.08f, 0.28f,
		0.43f, 0.38f, 0.48f, 0.43f, 0.58f, 1.98f, 2.13f, 0.68f, 0.33f, 0.43f,
		{ 320.0f, 640.0f, 960.0f, 0.0f }, { 0.52f, 0.28f, 0.15f, 0.0f }, 0.95f, 0.38f,
		0.28f, 0.72f, 12000.0f, 0.6f,
		0.0f, 0.0f, 0.0f, 0.78f,
		0.0f, 0.0f, 0.68f, 0.0f,
		0.48f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 64: 小さな教会 - カテドラルより親密
	{
		0.40f, 115.0f, 0.44f,
		{ 24.0f, 0.41f, 40.0f, 0.37f, 70.0f, 0.33f, 108.0f, 0.29f, 150.0f, 0.25f, 200.0f, 0.21f, 260.0f, 0.17f, 330.0f, 0.13f },
		6400, 48, 1.38f, 0.48f, 0.58f, 0.12f, 0.95f, 9.9f, 0.63f, 1.58f, 0.43f, 0.98f, 0.58f,
		1.48f, 1.88f, 0.93f, 0.58f, 1.58f, 1.08f, 0.78f, 0.73f, 0.58f, 0.63f, -0.11f, 0.43f,
		0.48f, 0.73f, 0.58f, 0.78f, 0.83f, 1.73f, 1.58f, 0.73f, 0.18f, 0.28f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.48f, 0.52f, 1400.0f, 1.0f,
		0.0f, 0.0f, 0.92f, 0.0f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CATHEDRAL
	},

	// 65: ガラス温室 - 透明反射特性
	{
		0.63f, 72.0f, 0.63f,
		{ 12.0f, 0.73f, 20.0f, 0.69f, 34.0f, 0.63f, 52.0f, 0.58f, 75.0f, 0.53f, 102.0f, 0.47f, 135.0f, 0.41f, 175.0f, 0.33f },
		18400, 135, 1.54f, 0.58f, 1.95f, 0.38f, 0.73f, 5.4f, 0.43f, 1.33f, 0.68f, 0.63f, 0.33f,
		0.73f, 1.23f, 0.63f, 0.88f, 1.08f, 1.13f, 1.28f, 0.63f, 0.88f, 0.98f, 0.33f, 0.18f,
		0.23f, 0.48f, 0.88f, 0.23f, 0.33f, 1.38f, 1.28f, 0.43f, 0.43f, 0.33f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.38f, 0.62f, 5800.0f, 4.5f,
		0.18f, 0.82f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.28f, 0.0f,
		0.52f, 0.0f, 0.0f, 0.48f,
		TYPE_METAL_SPACE
	},

	// 66: 石造りトンネル - 硬く長い残響
	{
		0.52f, 172.0f, 0.60f,
		{ 25.0f, 0.63f, 40.0f, 0.59f, 60.0f, 0.55f, 85.0f, 0.51f, 115.0f, 0.47f, 150.0f, 0.43f, 190.0f, 0.38f, 240.0f, 0.33f },
		7100, 195, 0.63f, 0.58f, 1.54f, 0.23f, 0.53f, 11.5f, 0.58f, 1.08f, 0.63f, 0.73f, 0.48f,
		0.83f, 1.33f, 0.68f, 0.73f, 1.13f, 1.03f, 0.98f, 0.53f, 0.78f, 0.93f, 0.33f, 0.28f,
		0.43f, 0.53f, 0.83f, 0.38f, 0.73f, 1.13f, 0.93f, 0.48f, 0.63f, 0.48f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 2200.0f, 2.0f,
		0.0f, 0.0f, 0.0f, 0.92f,
		0.0f, 0.0f, 0.68f, 0.0f,
		0.78f, 0.0f, 0.0f, 0.0f,
		TYPE_CORRIDOR
	},

	// 67: コンクリート階段 - 縦方向硬質反射
	{
		0.58f, 86.0f, 0.63f,
		{ 14.0f, 0.68f, 22.0f, 0.63f, 36.0f, 0.58f, 54.0f, 0.53f, 76.0f, 0.48f, 102.0f, 0.41f, 132.0f, 0.35f, 168.0f, 0.29f },
		11700, 155, 0.93f, 0.68f, 1.85f, 0.23f, 0.73f, 9.4f, 0.63f, 0.98f, 0.73f, 0.78f, 0.51f,
		0.83f, 1.18f, 0.68f, 0.78f, 1.13f, 1.08f, 1.13f, 0.68f, 0.78f, 0.88f, 0.28f, 0.33f,
		0.43f, 0.53f, 0.83f, 0.48f, 0.63f, 1.58f, 0.98f, 0.53f, 0.33f, 0.35f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.52f, 0.48f, 3500.0f, 2.8f,
		0.0f, 0.0f, 0.0f, 0.92f,
		0.0f, 0.0f, 0.42f, 0.0f,
		0.48f, 0.0f, 0.0f, 0.0f,
		TYPE_CORRIDOR
	},

	// 68: 大浴場 - 湿ったタイル反射
	{
		0.55f, 105.0f, 0.61f,
		{ 19.0f, 0.61f, 30.0f, 0.58f, 49.0f, 0.53f, 73.0f, 0.49f, 101.0f, 0.45f, 136.0f, 0.39f, 176.0f, 0.33f, 223.0f, 0.28f },
		10900, 312, 1.43f, 0.68f, 1.34f, 0.33f, 0.93f, 12.0f, 0.33f, 1.48f, 0.63f, 0.73f, 0.43f,
		0.93f, 1.38f, 0.68f, 0.78f, 1.23f, 1.08f, 0.93f, 0.73f, 0.78f, 0.93f, 0.18f, 0.28f,
		0.33f, 0.58f, 0.78f, 0.38f, 0.43f, 1.28f, 1.13f, 0.48f, 0.38f, 0.28f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 2800.0f, 2.5f,
		0.38f, 0.62f, 0.0f, 0.0f,
		0.82f, 0.0f, 0.68f, 0.0f,
		0.42f, 0.0f, 0.0f, 0.0f,
		TYPE_UNDERWATER
	},

	// 69: 洗面所 - 小タイル空間
	{
		0.65f, 12.0f, 0.73f,
		{ 2.0f, 0.78f, 3.8f, 0.73f, 6.5f, 0.68f, 10.0f, 0.61f, 14.0f, 0.53f, 18.5f, 0.45f, 23.5f, 0.37f, 29.0f, 0.29f },
		16200, 308, 0.68f, 0.25f, 3.18f, 0.35f, 0.28f, 6.2f, 0.28f, 0.33f, 0.97f, 0.28f, 0.18f,
		0.46f, 0.68f, 0.43f, 0.87f, 1.08f, 1.03f, 0.98f, 0.28f, 0.53f, 0.97f, 0.13f, 0.13f,
		0.08f, 0.33f, 0.83f, 0.16f, 0.30f, 0.58f, 0.63f, 0.26f, 0.36f, 0.28f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.12f, 0.88f, 5500.0f, 4.0f,
		0.28f, 0.72f, 0.0f, 0.08f,
		0.0f, 0.0f, 0.98f, 0.0f,
		0.42f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 70: 廊下(カーペット敷き) - 吸音性高い
	{
		0.26f, 125.0f, 0.28f,
		{ 20.0f, 0.28f, 32.0f, 0.24f, 50.0f, 0.20f, 75.0f, 0.16f, 105.0f, 0.12f, 140.0f, 0.09f, 180.0f, 0.06f, 230.0f, 0.04f },
		8100, 83, 0.58f, 0.18f, 1.08f, 0.08f, 0.48f, 9.3f, 0.48f, 0.73f, 0.58f, 0.38f, 0.23f,
		0.68f, 0.93f, 0.68f, 0.38f, 0.98f, 0.98f, 0.88f, 0.38f, 0.43f, 0.78f, -0.13f, 0.53f,
		0.28f, 0.73f, 0.43f, 0.68f, 0.43f, 0.68f, 1.18f, 0.33f, 0.28f, 0.10f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.32f, 0.68f, 6500.0f, 0.9f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.92f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_CORRIDOR
	},

	// 71: 大会議室 - ビジネス用途
	{
		0.53f, 73.0f, 0.55f,
		{ 16.0f, 0.58f, 25.0f, 0.53f, 42.0f, 0.48f, 68.0f, 0.41f, 95.0f, 0.35f, 128.0f, 0.29f, 165.0f, 0.25f, 210.0f, 0.19f },
		10900, 83, 1.23f, 0.33f, 0.93f, 0.28f, 0.73f, 0.0f, 0.63f, 1.03f, 0.58f, 0.68f, 0.48f,
		1.13f, 1.43f, 0.83f, 0.58f, 1.28f, 1.08f, 0.93f, 0.73f, 0.63f, 0.73f, -0.06f, 0.43f,
		0.38f, 0.73f, 0.58f, 0.68f, 0.58f, 1.28f, 1.28f, 0.58f, 0.13f, 0.13f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.58f, 0.42f, 3200.0f, 1.3f,
		0.0f, 0.0f, 0.55f, 0.0f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 72: 小会議室 - 密室ビジネス
	{
		0.33f, 34.0f, 0.38f,
		{ 6.0f, 0.48f, 10.0f, 0.43f, 16.0f, 0.38f, 25.0f, 0.32f, 36.0f, 0.26f, 50.0f, 0.20f, 66.0f, 0.14f, 85.0f, 0.10f },
		8900, 93, 0.68f, 0.16f, 0.78f, 0.18f, 0.53f, 0.0f, 0.73f, 0.58f, 0.63f, 0.48f, 0.48f,
		0.88f, 1.08f, 0.83f, 0.43f, 1.08f, 0.98f, 0.93f, 0.48f, 0.48f, 0.73f, -0.01f, 0.53f,
		0.38f, 0.78f, 0.53f, 0.73f, 0.48f, 0.88f, 0.93f, 0.38f, 0.13f, 0.13f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.42f, 0.58f, 7200.0f, 1.1f,
		0.0f, 0.0f, 0.42f, 0.0f,
		0.0f, 0.0f, 0.88f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 73: 防音室 - 極度に吸音処理
	{
		0.012f, 5.0f, 0.005f,
		{ 0.7f, 0.03f, 1.3f, 0.02f, 2.2f, 0.01f, 3.8f, 0.005f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
		19850, 19, 0.80f, 0.0f, 0.03f, 0.60f, 0.0f, 0.0f, 0.10f, 0.20f, 0.94f, 0.0f, 0.06f,
		0.03f, 0.16f, 0.995f, 0.30f, 0.60f, 0.98f, 0.98f, 0.005f, 0.005f, 0.99f, -0.10f, 0.995f,
		0.005f, 0.66f, 0.33f, 0.995f, 0.23f, 0.60f, 0.56f, 0.005f, 0.0f, 0.005f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.005f, 0.995f, 19700.0f, 0.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.995f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 74: エントランスホール - 高天井開放的
	{
		0.48f, 135.0f, 0.48f,
		{ 42.0f, 0.45f, 69.0f, 0.40f, 117.0f, 0.35f, 178.0f, 0.30f, 246.0f, 0.26f, 324.0f, 0.22f, 413.0f, 0.19f, 512.0f, 0.15f },
		12000, 81, 1.45f, 0.42f, 0.65f, 0.21f, 0.71f, 13.6f, 0.58f, 1.55f, 0.48f, 0.72f, 0.55f,
		1.31f, 1.65f, 0.78f, 0.58f, 1.31f, 1.03f, 0.89f, 0.73f, 0.65f, 0.66f, 0.08f, 0.41f,
		0.39f, 0.65f, 0.60f, 0.61f, 0.60f, 1.75f, 1.55f, 0.69f, 0.10f, 0.18f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.55f, 0.45f, 2400.0f, 1.1f,
		0.0f, 0.0f, 0.42f, 0.48f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 75: 書斎 - 個室落ち着き空間
	{
		0.28f, 38.5f, 0.32f,
		{ 7.0f, 0.38f, 11.5f, 0.34f, 18.0f, 0.29f, 29.0f, 0.24f, 42.0f, 0.19f, 57.0f, 0.14f, 74.0f, 0.10f, 96.0f, 0.06f },
		6800, 66, 0.68f, 0.08f, 0.60f, 0.10f, 0.46f, 0.0f, 0.56f, 0.56f, 0.61f, 0.30f, 0.40f,
		0.72f, 0.93f, 0.83f, 0.32f, 1.00f, 0.98f, 0.90f, 0.43f, 0.38f, 0.64f, -0.16f, 0.60f,
		0.34f, 0.82f, 0.40f, 0.76f, 0.40f, 0.83f, 0.90f, 0.31f, 0.10f, 0.02f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.28f, 0.72f, 6200.0f, 0.9f,
		0.0f, 0.0f, 0.88f, 0.0f,
		0.0f, 0.0f, 0.62f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_SMALL_ROOM
	},

	// 76: キッチン - 硬質反射多め
	{
		0.58f, 32.5f, 0.61f,
		{ 5.8f, 0.66f, 9.0f, 0.63f, 14.2f, 0.57f, 22.0f, 0.51f, 30.5f, 0.45f, 40.0f, 0.38f, 52.5f, 0.32f, 67.0f, 0.25f },
		12000, 155, 0.64f, 0.10f, 1.67f, 0.22f, 0.36f, 0.4f, 0.43f, 0.30f, 0.70f, 0.46f, 0.29f,
		0.63f, 0.83f, 0.58f, 0.58f, 0.96f, 0.98f, 1.06f, 0.46f, 0.60f, 0.73f, 0.23f, 0.43f,
		0.32f, 0.53f, 0.70f, 0.48f, 0.45f, 0.68f, 0.76f, 0.28f, 0.12f, 0.21f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.48f, 0.52f, 4200.0f, 2.2f,
		0.38f, 0.62f, 0.0f, 0.12f,
		0.0f, 0.0f, 0.42f, 0.0f,
		0.28f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 77: 屋外駐車場 - 開放的アスファルト
	{
		0.08f, 265.0f, 0.06f,
		{ 68.0f, 0.13f, 108.0f, 0.11f, 172.0f, 0.08f, 260.0f, 0.06f, 362.0f, 0.05f, 478.0f, 0.03f, 608.0f, 0.02f, 752.0f, 0.01f },
		19200, 112, 2.08f, 0.52f, 0.50f, 0.04f, 0.64f, 34.5f, 0.56f, 2.88f, 0.37f, 0.64f, 0.96f,
		0.94f, 1.24f, 0.74f, 0.74f, 1.06f, 1.04f, 1.12f, 0.58f, 0.70f, 0.92f, 0.36f, 0.24f,
		0.58f, 0.54f, 0.78f, 0.40f, 0.68f, 1.74f, 2.04f, 0.94f, 0.20f, 0.44f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.18f, 0.82f, 18600.0f, 0.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.98f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_OUTDOOR_OPEN
	},

	// 78: 地下通路(狭い) - 圧迫感ある直線
	{
		0.62f, 118.0f, 0.66f,
		{ 18.0f, 0.63f, 27.5f, 0.60f, 44.0f, 0.56f, 66.0f, 0.52f, 92.0f, 0.47f, 124.0f, 0.42f, 160.0f, 0.36f, 202.0f, 0.31f },
		7900, 180, 0.46f, 0.41f, 1.36f, 0.22f, 0.61f, 9.4f, 0.47f, 0.68f, 0.75f, 0.61f, 0.33f,
		0.78f, 1.11f, 0.60f, 0.58f, 1.05f, 1.03f, 1.00f, 0.53f, 0.69f, 0.92f, 0.33f, 0.27f,
		0.39f, 0.61f, 0.76f, 0.37f, 0.67f, 0.93f, 0.81f, 0.45f, 0.66f, 0.39f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.68f, 0.32f, 2400.0f, 1.9f,
		0.0f, 0.0f, 0.0f, 0.88f,
		0.0f, 0.0f, 0.82f, 0.0f,
		0.72f, 0.0f, 0.0f, 0.0f,
		TYPE_CORRIDOR
	},

	// 79: 展示室 - ギャラリー用途
	{
		0.37f, 113.0f, 0.40f,
		{ 19.0f, 0.43f, 30.0f, 0.38f, 52.0f, 0.33f, 84.0f, 0.28f, 120.0f, 0.23f, 165.0f, 0.19f, 220.0f, 0.14f, 285.0f, 0.10f },
		9800, 78, 1.21f, 0.36f, 0.81f, 0.17f, 0.79f, 15.5f, 0.71f, 1.25f, 0.61f, 0.65f, 0.63f,
		1.13f, 1.38f, 0.81f, 0.48f, 1.18f, 1.03f, 0.89f, 0.69f, 0.59f, 0.76f, 0.01f, 0.49f,
		0.37f, 0.81f, 0.53f, 0.69f, 0.53f, 1.35f, 1.28f, 0.57f, 0.20f, 0.17f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.58f, 0.42f, 2800.0f, 1.1f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.0f, 0.0f, 0.62f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		TYPE_LARGE_HALL
	},

	// 80: アトリエ - 創作空間
	{
		0.42f, 76.0f, 0.46f,
		{ 12.0f, 0.48f, 18.5f, 0.44f, 31.5f, 0.38f, 51.0f, 0.33f, 72.0f, 0.28f, 97.0f, 0.22f, 128.0f, 0.17f, 162.0f, 0.12f },
		10200, 93, 1.08f, 0.29f, 0.88f, 0.22f, 0.85f, 11.4f, 0.63f, 1.08f, 0.75f, 0.57f, 0.51f,
		1.03f, 1.28f, 0.83f, 0.53f, 1.21f, 1.03f, 0.97f, 0.67f, 0.63f, 0.87f, -0.04f, 0.50f,
		0.43f, 0.91f, 0.53f, 0.77f, 0.55f, 1.13f, 1.18f, 0.53f, 0.28f, 0.13f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 3400.0f, 1.3f,
		0.0f, 0.0f, 0.72f, 0.0f,
		0.0f, 0.0f, 0.68f, 0.0f,
		0.32f, 0.0f, 0.0f, 0.0f,
		TYPE_MEDIUM_ROOM
	},

	// 81-100: SF・特殊空間（個性化）

	// 81: サイバーパンク路地 - 人工的金属反射
	{
		0.38f, 73.0f, 0.43f,
		{ 6.5f, 0.48f, 12.5f, 0.44f, 21.0f, 0.40f, 30.0f, 0.35f, 38.0f, 0.30f, 48.0f, 0.25f, 60.0f, 0.21f, 75.0f, 0.17f },
		14100, 130, 1.28f, 0.30f, 1.90f, 0.28f, 0.61f, 8.8f, 0.31f, 1.28f, 0.51f, 0.61f, 0.25f,
		0.98f, 1.48f, 0.61f, 0.71f, 0.98f, 1.15f, 1.28f, 0.55f, 0.65f, 0.65f, 0.35f, 0.41f,
		0.61f, 0.41f, 0.81f, 0.51f, 0.51f, 1.08f, 1.08f, 0.41f, 0.45f, 0.39f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.55f, 0.45f, 4800.0f, 3.2f,
		0.72f, 0.18f, 0.0f, 0.35f,
		0.0f, 0.0f, 0.42f, 0.0f,
		0.0f, 0.55f, 0.28f, 0.42f,
		TYPE_SF_SPACE
	},

	// 82: 宇宙船ブリッジ - 無反響＋機械的
	{
		0.24f, 45.0f, 0.24f,
		{ 4.5f, 0.48f, 8.5f, 0.44f, 14.0f, 0.39f, 19.0f, 0.35f, 25.0f, 0.31f, 32.0f, 0.26f, 40.0f, 0.21f, 48.0f, 0.17f },
		17100, 190, 1.08f, 0.12f, 0.93f, 0.28f, 0.46f, 5.3f, 0.18f, 0.93f, 0.40f, 0.46f, 0.15f,
		0.73f, 1.03f, 0.46f, 0.70f, 0.80f, 1.03f, 1.13f, 0.46f, 0.56f, 0.46f, 0.40f, 0.26f,
		0.36f, 0.30f, 0.80f, 0.36f, 0.36f, 0.90f, 0.90f, 0.40f, 0.20f, 0.20f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.38f, 0.62f, 8200.0f, 2.5f,
		0.58f, 0.32f, 0.0f, 0.22f,
		0.0f, 0.0f, 0.42f, 0.0f,
		0.0f, 0.32f, 0.0f, 0.52f,
		TYPE_SF_SPACE
	},

	// 83: ワープトンネル - 高速渦巻きと時空歪み
	{
		0.58f, 205.0f, 0.63f,
		{ 8.0f, 0.52f, 18.0f, 0.46f, 32.0f, 0.40f, 48.0f, 0.34f, 66.0f, 0.28f, 88.0f, 0.22f, 114.0f, 0.17f, 144.0f, 0.13f },
		9200, 58, 2.28f, 0.88f, 3.85f, 0.08f, 0.92f, 32.5f, 0.38f, 3.68f, 0.75f, 0.82f, 0.28f,
		1.65f, 2.88f, 0.82f, 0.68f, 1.58f, 1.78f, 1.55f, 0.71f, 0.68f, 0.92f, 0.22f, 0.35f,
		0.75f, 0.52f, 0.68f, 0.52f, 0.75f, 1.95f, 1.78f, 0.75f, 0.88f, 0.88f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.82f, 0.18f, 2800.0f, 1.8f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.22f, 0.0f,
		0.0f, 0.92f, 0.0f, 0.82f,
		TYPE_SF_SPACE
	},

	// 84: 量子ホール - 干渉縞反射
	{
		0.35f, 138.0f, 0.38f,
		{ 9.0f, 0.44f, 18.0f, 0.39f, 30.0f, 0.35f, 44.0f, 0.31f, 60.0f, 0.27f, 78.0f, 0.22f, 100.0f, 0.18f, 126.0f, 0.15f },
		15100, 98, 1.75f, 0.78f, 2.60f, 0.21f, 0.78f, 20.0f, 0.33f, 2.58f, 0.63f, 0.73f, 0.25f,
		1.35f, 2.18f, 0.73f, 0.73f, 1.25f, 1.51f, 1.51f, 0.61f, 0.73f, 0.78f, 0.25f, 0.28f,
		0.63f, 0.41f, 0.78f, 0.51f, 0.63f, 1.55f, 1.45f, 0.55f, 0.65f, 0.65f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.68f, 0.32f, 8200.0f, 3.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.38f, 0.0f,
		0.48f, 0.0f, 0.0f, 0.68f,
		TYPE_SF_SPACE
	},

	// 85: 無限回廊 - 永続的残響と錯覚
	{
		0.62f, 178.0f, 0.68f,
		{ 12.0f, 0.58f, 24.0f, 0.54f, 36.0f, 0.49f, 50.0f, 0.44f, 64.0f, 0.39f, 80.0f, 0.34f, 100.0f, 0.29f, 124.0f, 0.24f },
		10200, 78, 1.68f, 0.48f, 1.55f, 0.21f, 0.78f, 15.8f, 0.52f, 3.18f, 0.65f, 0.72f, 0.35f,
		1.35f, 2.48f, 0.72f, 0.65f, 1.28f, 1.48f, 1.38f, 0.68f, 0.62f, 0.95f, 0.08f, 0.38f,
		0.58f, 0.52f, 0.68f, 0.52f, 0.68f, 1.58f, 1.68f, 0.58f, 0.62f, 0.72f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.68f, 0.32f, 7200.0f, 2.8f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.0f, 0.75f, 0.0f, 0.62f,
		TYPE_SF_SPACE
	},

	// 86: 逆再生空間 - 時間巻き戻り
	{
		0.47f, 125.0f, 0.52f,
		{ 7.0f, 0.56f, 15.0f, 0.50f, 28.0f, 0.44f, 44.0f, 0.38f, 62.0f, 0.32f, 82.0f, 0.26f, 105.0f, 0.21f, 132.0f, 0.17f },
		10100, 77, 1.85f, 0.88f, 3.00f, 0.21f, 0.83f, 21.5f, 0.51f, 2.38f, 0.73f, 0.78f, 0.35f,
		1.15f, 2.38f, 0.75f, 0.51f, 1.15f, 1.51f, 1.38f, 0.65f, 0.65f, 0.65f, -0.28f, 0.41f,
		0.65f, 0.51f, 0.51f, 0.55f, 0.65f, 1.51f, 1.31f, 0.55f, 0.88f, 0.78f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.65f, 0.35f, 7200.0f, 2.8f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.42f, 0.0f,
		0.0f, 0.72f, 0.0f, 0.62f,
		TYPE_SF_SPACE
	},

	// 87: タイムストップ室 - 残響が止まる
	{
		0.04f, 15.0f, 0.06f,
		{ 1.8f, 0.56f, 3.5f, 0.52f, 5.5f, 0.48f, 7.5f, 0.40f, 9.5f, 0.32f, 11.5f, 0.24f, 13.5f, 0.18f, 15.5f, 0.13f },
		18100, 190, 0.80f, 0.02f, 0.32f, 0.10f, 0.20f, 1.0f, 0.10f, 0.53f, 0.26f, 0.26f, 0.06f,
		0.43f, 0.53f, 0.33f, 0.63f, 0.73f, 0.76f, 0.86f, 0.26f, 0.30f, 0.16f, 0.16f, 0.16f,
		0.26f, 0.16f, 0.80f, 0.26f, 0.20f, 0.63f, 0.63f, 0.16f, 0.02f, 0.02f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.08f, 0.92f, 16200.0f, 1.2f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.92f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.82f,
		TYPE_SF_SPACE
	},

	// 88: データセンター - 規則的金属ラック
	{
		0.20f, 40.0f, 0.21f,
		{ 4.5f, 0.48f, 10.0f, 0.43f, 16.0f, 0.38f, 24.0f, 0.33f, 32.0f, 0.28f, 42.0f, 0.23f, 54.0f, 0.18f, 70.0f, 0.14f },
		9100, 130, 1.03f, 0.20f, 1.26f, 0.10f, 0.40f, 6.3f, 0.50f, 1.03f, 0.40f, 0.46f, 0.36f,
		0.70f, 0.93f, 0.46f, 0.40f, 1.13f, 1.00f, 0.90f, 0.50f, 0.40f, 0.50f, -0.10f, 0.46f,
		0.40f, 0.36f, 0.40f, 0.36f, 0.60f, 0.83f, 0.93f, 0.26f, 0.26f, 0.26f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.42f, 0.58f, 4200.0f, 2.5f,
		0.68f, 0.22f, 0.0f, 0.48f,
		0.0f, 0.0f, 0.58f, 0.0f,
		0.0f, 0.38f, 0.0f, 0.28f,
		TYPE_SF_SPACE
	},

	// 89: 巨大機械内部 - 周期的機械共振
	{
		0.44f, 82.0f, 0.47f,
		{ 7.0f, 0.54f, 13.0f, 0.48f, 22.0f, 0.42f, 32.0f, 0.37f, 45.0f, 0.32f, 60.0f, 0.27f, 78.0f, 0.22f, 100.0f, 0.18f },
		8100, 98, 1.21f, 0.35f, 1.15f, 0.18f, 0.63f, 11.2f, 0.61f, 1.75f, 0.51f, 0.61f, 0.45f,
		0.98f, 1.41f, 0.55f, 0.41f, 1.41f, 1.28f, 1.08f, 0.65f, 0.51f, 0.51f, -0.25f, 0.55f,
		0.73f, 0.55f, 0.41f, 0.35f, 0.81f, 1.21f, 1.31f, 0.35f, 0.41f, 0.45f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 1800.0f, 3.8f,
		0.78f, 0.12f, 0.0f, 0.58f,
		0.0f, 0.0f, 0.38f, 0.0f,
		0.0f, 0.48f, 0.0f, 0.35f,
		TYPE_SF_SPACE
	},

	// 90: AIホログラム室 - 透明多層反射
	{
		0.36f, 63.0f, 0.40f,
		{ 5.5f, 0.52f, 11.0f, 0.47f, 17.0f, 0.42f, 25.0f, 0.38f, 34.0f, 0.33f, 46.0f, 0.28f, 60.0f, 0.23f, 78.0f, 0.19f },
		18100, 120, 1.61f, 0.41f, 1.75f, 0.37f, 0.68f, 10.2f, 0.31f, 1.51f, 0.55f, 0.61f, 0.25f,
		0.98f, 1.51f, 0.65f, 0.81f, 0.98f, 1.15f, 1.38f, 0.55f, 0.71f, 0.65f, 0.45f, 0.31f,
		0.45f, 0.35f, 0.91f, 0.51f, 0.45f, 1.28f, 1.18f, 0.51f, 0.45f, 0.41f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.62f, 0.38f, 9200.0f, 3.8f,
		0.42f, 0.58f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.48f, 0.0f,
		0.0f, 0.58f, 0.0f, 0.52f,
		TYPE_SF_SPACE
	},

	// 91: 重力ゼロ船庫 - 無重力の無指向反射
	{
		0.44f, 192.0f, 0.44f,
		{ 14.0f, 0.42f, 24.0f, 0.38f, 38.0f, 0.34f, 55.0f, 0.30f, 74.0f, 0.26f, 98.0f, 0.22f, 125.0f, 0.18f, 158.0f, 0.14f },
		13100, 65, 2.08f, 0.53f, 2.00f, 0.17f, 0.81f, 24.5f, 0.41f, 3.48f, 0.68f, 0.73f, 0.31f,
		1.45f, 2.68f, 0.78f, 0.68f, 1.41f, 1.75f, 1.51f, 0.65f, 0.65f, 0.78f, 0.15f, 0.35f,
		0.58f, 0.48f, 0.65f, 0.55f, 0.65f, 1.85f, 1.75f, 0.73f, 0.58f, 0.58f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.68f, 0.32f, 8500.0f, 2.2f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.85f, 0.48f, 0.0f,
		0.0f, 0.65f, 0.0f, 0.45f,
		TYPE_SF_SPACE
	},

	// 92: 惑星ドーム都市 - 巨大ドームの段階的反射
	{
		0.54f, 228.0f, 0.52f,
		{ 14.0f, 0.46f, 28.0f, 0.42f, 44.0f, 0.38f, 64.0f, 0.34f, 86.0f, 0.30f, 112.0f, 0.26f, 142.0f, 0.22f, 178.0f, 0.18f },
		15800, 72, 2.38f, 0.48f, 1.48f, 0.24f, 0.88f, 32.5f, 0.42f, 4.28f, 0.78f, 0.82f, 0.28f,
		1.65f, 2.95f, 0.85f, 0.82f, 1.62f, 1.92f, 1.68f, 0.75f, 0.82f, 0.78f, 0.28f, 0.35f,
		0.68f, 0.48f, 0.85f, 0.58f, 0.65f, 2.18f, 1.92f, 0.78f, 0.55f, 0.55f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.65f, 0.35f, 11800.0f, 2.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.78f, 0.68f, 0.0f,
		0.0f, 0.62f, 0.0f, 0.48f,
		TYPE_SF_SPACE
	},

	// 93: VRシミュレーター - 人工的過補正
	{
		0.44f, 98.0f, 0.47f,
		{ 9.0f, 0.52f, 16.0f, 0.47f, 26.0f, 0.42f, 38.0f, 0.38f, 52.0f, 0.32f, 70.0f, 0.27f, 90.0f, 0.22f, 112.0f, 0.19f },
		15100, 108, 2.38f, 0.98f, 3.50f, 0.25f, 0.73f, 15.5f, 0.35f, 1.98f, 0.58f, 0.68f, 0.31f,
		1.15f, 1.78f, 0.65f, 0.73f, 1.15f, 1.45f, 1.51f, 0.61f, 0.73f, 0.78f, 0.35f, 0.31f,
		0.58f, 0.41f, 0.81f, 0.51f, 0.51f, 1.45f, 1.35f, 0.55f, 0.65f, 0.65f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.68f, 0.32f, 8800.0f, 3.2f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.52f, 0.0f,
		0.0f, 0.72f, 0.0f, 0.68f,
		TYPE_SF_SPACE
	},

	// 94: レーザー通路 - 極端な高域一次反射
	{
		0.22f, 50.0f, 0.28f,
		{ 3.5f, 0.53f, 7.5f, 0.48f, 13.0f, 0.42f, 19.0f, 0.36f, 26.0f, 0.30f, 35.0f, 0.24f, 45.0f, 0.19f, 58.0f, 0.15f },
		19100, 170, 1.16f, 0.46f, 2.13f, 0.36f, 0.43f, 4.3f, 0.20f, 0.93f, 0.36f, 0.46f, 0.10f,
		0.73f, 1.03f, 0.46f, 0.88f, 0.83f, 1.03f, 1.33f, 0.36f, 0.56f, 0.86f, 0.60f, 0.16f,
		0.46f, 0.20f, 0.93f, 0.30f, 0.36f, 0.93f, 0.93f, 0.36f, 0.70f, 0.80f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.28f, 0.72f, 12800.0f, 4.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.32f, 0.0f,
		0.0f, 0.78f, 0.0f, 0.68f,
		TYPE_SF_SPACE
	},

	// 95: 異次元裂け目 - 非線形減衰と極端不安定
	{
		0.68f, 268.0f, 0.75f,
		{ 20.0f, 0.48f, 36.0f, 0.42f, 56.0f, 0.36f, 82.0f, 0.30f, 112.0f, 0.24f, 146.0f, 0.19f, 184.0f, 0.15f, 226.0f, 0.12f },
		8200, 38, 2.38f, 1.18f, 4.20f, 0.12f, 0.98f, 36.0f, 0.75f, 4.78f, 0.85f, 0.92f, 0.48f,
		1.88f, 3.28f, 0.92f, 0.52f, 1.88f, 2.08f, 1.62f, 0.85f, 0.72f, 1.05f, -0.58f, 0.55f,
		0.95f, 0.65f, 0.48f, 0.55f, 0.95f, 2.32f, 2.22f, 0.95f, 1.05f, 1.05f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.92f, 0.08f, 3600.0f, 2.5f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.25f, 0.0f,
		0.0f, 0.95f, 0.18f, 0.92f,
		TYPE_SF_SPACE
	},

	// 96: 夢の中 - 柔らかい輪郭と曖昧な反射
	{
		0.31f, 88.0f, 0.35f,
		{ 7.5f, 0.44f, 13.5f, 0.40f, 21.0f, 0.35f, 30.0f, 0.31f, 40.0f, 0.26f, 52.0f, 0.21f, 66.0f, 0.17f, 82.0f, 0.13f },
		12100, 75, 1.43f, 0.30f, 0.83f, 0.10f, 0.60f, 10.3f, 0.46f, 1.70f, 0.56f, 0.60f, 0.30f,
		0.93f, 1.36f, 0.70f, 0.56f, 1.03f, 1.23f, 1.13f, 0.56f, 0.56f, 0.50f, 0.00f, 0.36f,
		0.36f, 0.56f, 0.50f, 0.70f, 0.46f, 1.23f, 1.13f, 0.40f, 0.30f, 0.26f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.52f, 0.48f, 7200.0f, 1.5f,
		0.0f, 0.0f, 0.68f, 0.0f,
		0.0f, 0.0f, 0.82f, 0.0f,
		0.0f, 0.0f, 0.42f, 0.32f,
		TYPE_SF_SPACE
	},

	// 97: 水晶洞 - 高域共鳴と透明多重反射
	{
		0.52f, 188.0f, 0.55f,
		{ 10.0f, 0.48f, 20.0f, 0.44f, 32.0f, 0.40f, 48.0f, 0.36f, 68.0f, 0.32f, 92.0f, 0.27f, 120.0f, 0.22f, 152.0f, 0.18f },
		19500, 78, 1.88f, 0.38f, 1.38f, 0.38f, 0.88f, 19.5f, 0.22f, 3.08f, 0.75f, 0.78f, 0.18f,
		1.48f, 2.48f, 0.78f, 0.98f, 1.28f, 1.58f, 1.88f, 0.68f, 0.88f, 0.72f, 0.62f, 0.28f,
		0.68f, 0.45f, 0.98f, 0.58f, 0.62f, 1.78f, 1.68f, 0.68f, 0.52f, 0.52f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.75f, 0.25f, 9800.0f, 4.8f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.88f, 0.0f, 0.0f, 0.78f,
		TYPE_CAVE
	},

	// 98: 廃宇宙ステーション - 金属疲労と不規則反射
	{
		0.70f, 218.0f, 0.76f,
		{ 18.0f, 0.62f, 34.0f, 0.56f, 52.0f, 0.50f, 74.0f, 0.44f, 100.0f, 0.38f, 130.0f, 0.32f, 164.0f, 0.26f, 202.0f, 0.21f },
		8400, 62, 1.88f, 0.42f, 1.35f, 0.22f, 0.92f, 26.5f, 0.72f, 3.78f, 0.72f, 0.82f, 0.58f,
		1.65f, 2.82f, 0.86f, 0.52f, 1.72f, 1.82f, 1.48f, 0.76f, 0.72f, 0.76f, -0.38f, 0.62f,
		0.76f, 0.62f, 0.58f, 0.58f, 0.82f, 1.98f, 1.88f, 0.76f, 0.58f, 0.52f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.72f, 0.28f, 4400.0f, 3.0f,
		0.85f, 0.15f, 0.0f, 0.48f,
		0.0f, 0.0f, 0.32f, 0.0f,
		0.0f, 0.68f, 0.32f, 0.58f,
		TYPE_SF_SPACE
	},

	// 99: ブラックホール縁 - 重力歪みによる極端非線形減衰
	{
		0.72f, 285.0f, 0.78f,
		{ 22.0f, 0.42f, 40.0f, 0.38f, 64.0f, 0.34f, 92.0f, 0.29f, 126.0f, 0.24f, 164.0f, 0.19f, 206.0f, 0.15f, 252.0f, 0.12f },
		6400, 28, 2.48f, 0.88f, 2.55f, 0.08f, 1.05f, 40.0f, 0.82f, 5.18f, 0.85f, 0.98f, 0.62f,
		2.08f, 3.48f, 0.95f, 0.42f, 2.08f, 2.28f, 1.72f, 0.88f, 0.78f, 1.08f, -0.75f, 0.68f,
		0.95f, 0.72f, 0.42f, 0.48f, 1.05f, 2.28f, 2.28f, 0.98f, 0.92f, 0.92f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.95f, 0.05f, 2800.0f, 1.8f,
		0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.15f, 0.0f,
		0.0f, 0.98f, 0.22f, 0.95f,
		TYPE_SF_SPACE
	},

	// 100: サイバー聖堂 - 電子的荘厳さと精密多層反射
	{
		0.56f, 248.0f, 0.58f,
		{ 16.0f, 0.46f, 30.0f, 0.42f, 48.0f, 0.38f, 70.0f, 0.34f, 94.0f, 0.30f, 122.0f, 0.26f, 154.0f, 0.22f, 190.0f, 0.18f },
		14800, 72, 2.18f, 0.52f, 1.78f, 0.28f, 0.92f, 33.5f, 0.42f, 4.48f, 0.78f, 0.88f, 0.35f,
		1.78f, 3.08f, 0.88f, 0.85f, 1.78f, 2.05f, 1.85f, 0.82f, 0.88f, 0.85f, 0.42f, 0.38f,
		0.72f, 0.52f, 0.88f, 0.58f, 0.72f, 2.22f, 2.08f, 0.88f, 0.72f, 0.72f,
		{ 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, 1.0f, 0.0f,
		0.78f, 0.22f, 10800.0f, 3.8f,
		0.58f, 0.42f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.48f, 0.0f,
		0.0f, 0.75f, 0.0f, 0.65f,
		TYPE_SF_SPACE
	}

};


// ============================================================
//  equaliser_dsp_full.c
//  Hyper DSP Equaliser & Acoustic Environment Engine
//
//  [修正履歴]
//  FIX-1 : coreScale/extraScale を正規化範囲 [0,1] に修正
//           旧: 最大2.17/2.50 → 音割れ・籠もりの主因
//  FIX-2 : ProfessionalSoftSaturate の knee を 0.65→0.78 に変更
//           旧: 通常音楽ピーク(0.70-0.90)が常にサチュレーション領域
//  FIX-3 : BlockAnalysis による透明ゲインステージング
//  FIX-4 : チップチューン/FM音源検出時のウェット・ハーモニック削減
//  FIX-5 : 24bit PCM リトルエンディアン バイト順修正
//  FIX-BYPASS : EQ全帯域フラット + 環境0 → 完全スルーパス
//  FIX-COMP   : stagingGain 閾値緩和(0.60→0.90) + ブロック間平滑化
//               急激な大音量時の過圧縮・籠もり感を解消
// ============================================================

// ===== フィルタ計算関数群 =====

// ピーキングEQ (bell型) バイクワッドフィルタ係数を計算する
// gainVal: 0-200 (100=フラット, 0=最大カット, 200=最大ブースト)
// db変換: (gainVal - 100) * 0.12 → ±12dB 範囲
// |db| < 1.2dB のときは恒等フィルタを設定してバイパス
static void CalcPeakingEQ(Biquad* f, float freq, float q, float gainVal, int rate) {
	if (gainVal < 0.0f) gainVal = 0.0f;
	if (gainVal > 200.0f) gainVal = 200.0f;

	float db = (gainVal - 100.0f) * 0.12f;

	// 微小ゲイン時はフィルタを恒等変換(スルー)に設定
	if (fabs(db) < 1.2f) {
		f->b0 = 1.0f; f->b1 = 0.0f; f->b2 = 0.0f;
		f->a1 = 0.0f; f->a2 = 0.0f;
		return;
	}

	// ナイキスト周波数を超えないようにクランプ
	float maxFreq = (float)rate * 0.45f;
	if (freq > maxFreq) freq = maxFreq;
	if (freq < 10.0f) freq = 10.0f;
	if (q < 0.1f) q = 0.1f;
	if (q > 10.0f) q = 10.0f;

	float omega = 2.0f * M_PI * freq / (float)rate;
	float sn = sinf(omega), cs = cosf(omega);
	float alpha = sn / (2.0f * q);

	// A = 10^(db/40) : 振幅比 (電圧ゲイン換算)
	float A = powf(10.0f, db / 40.0f);

	// 数値異常ガード
	if (!isfinite(A) || A < 0.01f || A > 100.0f) {
		f->b0 = 1.0f; f->b1 = 0.0f; f->b2 = 0.0f;
		f->a1 = 0.0f; f->a2 = 0.0f;
		return;
	}

	float a0 = 1.0f + alpha / A;

	// a0≒0は除算不能 → スルーに退避
	if (fabs(a0) < 1e-10f) {
		f->b0 = 1.0f; f->b1 = 0.0f; f->b2 = 0.0f;
		f->a1 = 0.0f; f->a2 = 0.0f;
		return;
	}

	// 標準ピーキングEQ係数 (Audio EQ Cookbook 準拠)
	f->b0 = (1.0f + alpha * A) / a0;
	f->b1 = (-2.0f * cs) / a0;
	f->b2 = (1.0f - alpha * A) / a0;
	f->a1 = (-2.0f * cs) / a0;
	f->a2 = (1.0f - alpha / A) / a0;

	// 最終数値検証 → 異常なら恒等変換
	if (!isfinite(f->b0) || !isfinite(f->b1) || !isfinite(f->b2) ||
		!isfinite(f->a1) || !isfinite(f->a2)) {
		f->b0 = 1.0f; f->b1 = 0.0f; f->b2 = 0.0f;
		f->a1 = 0.0f; f->a2 = 0.0f;
	}
}

// 汎用 2次フィルタ係数計算
// type==0: ローパスフィルタ (Butterworth 2次)
// type==1: ハイパスフィルタ (Butterworth 2次)
static void CalcFilter(Biquad* f, int type, float freq, float q, int rate) {
	if (freq <= 0.0f) freq = 20.0f;
	if (freq >= rate / 2.0f) freq = rate / 2.0f - 1.0f;
	float omega = 2.0f * M_PI * freq / (float)rate;
	float sn = sinf(omega), cs = cosf(omega);
	float alpha = sn / (2.0f * q);
	float a0 = 1.0f + alpha;

	if (type == 0) {
		// ローパス
		f->b0 = ((1.0f - cs) * 0.5f) / a0;
		f->b1 = (1.0f - cs) / a0;
		f->b2 = ((1.0f - cs) * 0.5f) / a0;
	}
	else {
		// ハイパス
		f->b0 = ((1.0f + cs) * 0.5f) / a0;
		f->b1 = (-(1.0f + cs)) / a0;
		f->b2 = ((1.0f + cs) * 0.5f) / a0;
	}
	f->a1 = (-2.0f * cs) / a0;
	f->a2 = (1.0f - alpha) / a0;
}

// シェルビングEQ係数計算
// type==0: ローシェルフ (低域ブースト/カット)
// type==1: ハイシェルフ (高域ブースト/カット)
// gainDb: dB単位。|gainDb| < 0.01 のときは恒等変換
static void CalcShelvingEQ(Biquad* f, int type, float freq, float gainDb, int rate) {
	if (fabs(gainDb) < 0.01f) {
		f->b0 = 1; f->b1 = 0; f->b2 = 0; f->a1 = 0; f->a2 = 0;
		return;
	}
	float omega = 2.0f * M_PI * freq / (float)rate;
	float sn = sinf(omega), cs = cosf(omega);
	float A = powf(10.0f, gainDb / 40.0f);
	float beta = sqrtf(A) / 0.707f;   // Q=0.707 (Butterworth 最平坦)

	if (type == 0) {
		// ローシェルフ
		float a0 = (A + 1.0f) + (A - 1.0f) * cs + beta * sn;
		f->b0 = (A * ((A + 1.0f) - (A - 1.0f) * cs + beta * sn)) / a0;
		f->b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
		f->b2 = (A * ((A + 1.0f) - (A - 1.0f) * cs - beta * sn)) / a0;
		f->a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
		f->a2 = ((A + 1.0f) + (A - 1.0f) * cs - beta * sn) / a0;
	}
	else {
		// ハイシェルフ
		float a0 = (A + 1.0f) - (A - 1.0f) * cs + beta * sn;
		f->b0 = (A * ((A + 1.0f) + (A - 1.0f) * cs + beta * sn)) / a0;
		f->b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
		f->b2 = (A * ((A + 1.0f) + (A - 1.0f) * cs - beta * sn)) / a0;
		f->a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
		f->a2 = ((A + 1.0f) - (A - 1.0f) * cs - beta * sn) / a0;
	}
}

// バイクワッドフィルタ 1サンプル処理 (Direct Form II Transposed)
// デノーマル対策: 1e-15未満をゼロクリア
// オーバーフロー対策: ±10.0でハードリミット (通常動作では到達しない)
static float ProcessBiquad(Biquad* f, float in) {
	if (!isfinite(in)) return 0.0f;

	float out = f->b0 * in + f->b1 * f->x1 + f->b2 * f->x2
		- f->a1 * f->y1 - f->a2 * f->y2;

	// デノーマル対策 (FTZ未設定環境向け)
	if (fabs(out) < 1e-15f) out = 0.0f;
	f->x2 = f->x1; f->x1 = in;
	f->y2 = f->y1; f->y1 = out;
	if (fabs(f->y1) < 1e-15f) f->y1 = 0.0f;
	if (fabs(f->y2) < 1e-15f) f->y2 = 0.0f;

	// NaN/Inf 発生時はバッファリセットして 0 を返す
	if (!isfinite(out)) {
		f->x1 = f->x2 = f->y1 = f->y2 = 0.0f;
		return 0.0f;
	}
	// 過大振幅クランプ (ガード用: 通常到達しない)
	if (out > 10.0f)  out = 10.0f;
	if (out < -10.0f) out = -10.0f;
	return out;
}

// ============================================================
// 拡張版山彦(やまびこ)処理
// 山岳・渓谷エコー用: 最大4タップのマルチタップディレイ
// yamabikoPan: 奇数タップに左右パンを付与し自然な広がりを再現
// decayMult  : タップ番号が増えるごとに指数的に減衰
// ============================================================
static inline float ProcessYamabikoAdvanced(ChannelState* cs, float input, const EnvParams* env, int sampleRate)
{
	if (!env || env->yamabikoDelays[0] <= 0.0f) return input;

	float out = input;
	// 各タップの減衰係数: 1フレーム当たりの減衰率
	float decayMult = powf(0.95f, 1.0f / env->yamabikoDecay);

	for (int i = 0; i < 4; i++) {
		if (env->yamabikoDelays[i] <= 0.0f) break;
		int delaySamples = (int)(env->yamabikoDelays[i] * (float)sampleRate / 1000.0f);
		if (delaySamples >= cs->yamabikoBufSize) continue;
		int readPos = cs->yamabikoPos - delaySamples;
		if (readPos < 0) readPos += cs->yamabikoBufSize;
		float delayed = cs->yamabikoBuf[readPos];
		float gain = env->yamabikoGains[i] * powf(decayMult, (float)i);
		// 奇数タップにパン効果を付与 (左右交互)
		float panEffect = (i % 2) ? env->yamabikoPan : -env->yamabikoPan;
		delayed *= (1.0f + panEffect * 0.3f);
		out += delayed * gain;
	}

	// 入力信号を循環バッファに書き込む
	cs->yamabikoBuf[cs->yamabikoPos] = input;
	cs->yamabikoPos++;
	if (cs->yamabikoPos >= cs->yamabikoBufSize) cs->yamabikoPos = 0;
	return out;
}

// LFO (低周波発振器) 出力計算
// サイン波 × depth を返し、位相を sampleRate で正規化して進める
static inline float UpdateLFO(LFO* lfo, int sampleRate) {
	if (lfo->frequency <= 0.0f || lfo->depth <= 0.0f) return 0.0f;
	float value = sinf(lfo->phase * 2.0f * M_PI) * lfo->depth;
	lfo->phase += lfo->frequency / (float)sampleRate;
	if (lfo->phase >= 1.0f) lfo->phase -= 1.0f;
	return value;
}

// ============================================================
// ディフュージョン処理 (拡散反射シミュレーション)
// 最大3段のオールパスフィルタカスケードで密度を制御
// 山岳・渓谷エコー時は弱いディフュージョンのみ適用 (clear感を維持)
// density > 0.3: 第2段追加
// density > 0.6: 第3段追加
// ============================================================
static inline float ProcessDiffusion(ChannelState* cs, float input, float diffusion, float density, int envType)
{
	// 山岳・渓谷エコー: ディフュージョンを最小限に抑えてエコーの輪郭を保つ
	if (envType == TYPE_MOUNTAIN_ECHO || envType == TYPE_CANYON_ECHO) {
		float weakDiff = diffusion * 0.12f;
		if (weakDiff <= 0.001f) return input;
		static const int delays1[8] = { 37, 53, 73, 97, 127, 163, 211, 277 };
		float output = input;
		float coeff = weakDiff * 0.35f;
		if (coeff > 0.35f) coeff = 0.35f;
		for (int i = 0; i < 8; i++) {
			int readPos = (cs->diffusionPos1[i] - delays1[i] + 1024) % 1024;
			float delayed = cs->diffusionBuffer1[i][readPos];
			float temp = output + delayed * coeff;
			cs->diffusionBuffer1[i][cs->diffusionPos1[i]] = temp;
			output = delayed - temp * coeff;
			cs->diffusionPos1[i] = (cs->diffusionPos1[i] + 1) % 1024;
		}
		return output;
	}

	if (diffusion <= 0.0f) return input;

	// 第1段: 主ディフュージョン (素数遅延を使用してコムフィルタを回避)
	static const int delays1[8] = { 37, 53, 73, 97, 127, 163, 211, 277 };
	float output = input;
	float coeff = diffusion * 0.6f;
	if (coeff > 0.6f) coeff = 0.6f;
	for (int i = 0; i < 8; i++) {
		int readPos = (cs->diffusionPos1[i] - delays1[i] + 1024) % 1024;
		float delayed = cs->diffusionBuffer1[i][readPos];
		float temp = output + delayed * coeff;
		cs->diffusionBuffer1[i][cs->diffusionPos1[i]] = temp;
		output = delayed - temp * coeff;
		cs->diffusionPos1[i] = (cs->diffusionPos1[i] + 1) % 1024;
	}

	// 第2段: density > 0.3 で追加 (より密な反射)
	if (density > 0.3f) {
		static const int delays2[8] = { 23, 31, 41, 59, 71, 89, 107, 131 };
		float coeff2 = density * 0.5f;
		if (coeff2 > 0.5f) coeff2 = 0.5f;
		for (int i = 0; i < 8; i++) {
			int readPos = (cs->diffusionPos2[i] - delays2[i] + 512) % 512;
			float delayed = cs->diffusionBuffer2[i][readPos];
			float temp = output + delayed * coeff2;
			cs->diffusionBuffer2[i][cs->diffusionPos2[i]] = temp;
			output = delayed - temp * coeff2;
			cs->diffusionPos2[i] = (cs->diffusionPos2[i] + 1) % 512;
		}
	}

	// 第3段: density > 0.6 でさらに追加 (超高密度反射)
	if (density > 0.6f) {
		static const int delays3[8] = { 13, 17, 19, 29, 37, 43, 53, 67 };
		float coeff3 = (density - 0.6f) * 0.6f;
		if (coeff3 > 0.4f) coeff3 = 0.4f;
		for (int i = 0; i < 8; i++) {
			int readPos = (cs->diffusionPos3[i] - delays3[i] + 256) % 256;
			float delayed = cs->diffusionBuffer3[i][readPos];
			float temp = output + delayed * coeff3;
			cs->diffusionBuffer3[i][cs->diffusionPos3[i]] = temp;
			output = delayed - temp * coeff3;
			cs->diffusionPos3[i] = (cs->diffusionPos3[i] + 1) % 256;
		}
	}
	return output;
}

// エキサイター: 高域を高調波歪みで倍音付加
// HPFで抽出した高域成分を非線形処理し元信号に加算
static inline float Exciter(float input, Biquad* hpf, float amount) {
	if (amount <= 0.0f) return input;
	float highFreq = ProcessBiquad(hpf, input);
	float enhanced = highFreq * 2.5f;
	// ソフトクリップ (折り返し型)
	if (enhanced > 1.0f) enhanced = 1.0f - (enhanced - 1.0f) * 0.3f;
	if (enhanced < -1.0f) enhanced = -1.0f + (enhanced + 1.0f) * 0.3f;
	return input + (enhanced - highFreq) * amount * 1.3f;
}

// ソフトリミッター: threshold=0.7 のスムーズな膝特性
// knee領域: x/(1+|x-th|) 型の双曲線で自然に圧縮
static inline float SoftLimiter(float x) {
	const float threshold = 0.7f;
	if (x > threshold) { float o = x - threshold; x = threshold + (1.0f - threshold) * (o / (1.0f + o)); }
	else if (x < -threshold) { float o = -x - threshold; x = -threshold - (1.0f - threshold) * (o / (1.0f + o)); }
	return x;
}

// フラッターエコー: テープヘッド揺れを模したビブラート的エコー
// flutterPhase で変調周波数を制御し filtered 成分を加算
static inline float ProcessFlutterEcho(ChannelState* cs, float input, float amount, int sampleRate) {
	if (amount <= 0.0f) return input;
	float flutterFreq = 8.0f + amount * 4.0f;
	cs->flutterPhase += flutterFreq / (float)sampleRate;
	if (cs->flutterPhase >= 1.0f) cs->flutterPhase -= 1.0f;
	float modulation = sinf(cs->flutterPhase * 2.0f * M_PI) * amount * 0.15f;
	float filtered = ProcessBiquad(&cs->flutterFilter, input);
	return input + filtered * modulation;
}

// 材質吸収シミュレーション: LPFで材料固有の高域吸収を再現
// roughness が高いほど全体レベルも低下 (拡散損失)
static inline float ProcessMaterialAbsorption(ChannelState* cs, float input, float absorption, float roughness) {
	if (absorption <= 0.0f && roughness <= 0.0f) return input;
	float absorbed = ProcessBiquad(&cs->materialFilter, input);
	if (roughness > 0.0f) absorbed *= (1.0f - roughness * 0.3f);
	return input * (1.0f - absorption) + absorbed * absorption;
}

// ウォームス(温もり)処理: 低域シェルフ + スムージングで音の温もりを付与
// warmthState: 指数平滑フィルタ (係数0.98/0.02)
static inline float ProcessWarmth(ChannelState* cs, float input, float warmth) {
	if (warmth <= 0.0f) return input;
	float warmed = ProcessBiquad(&cs->warmthFilter, input);
	cs->warmthState = cs->warmthState * 0.98f + warmed * 0.02f;
	return input * (1.0f - warmth * 0.3f) + cs->warmthState * warmth * 0.3f;
}

// ブライトネス: 奇数高調波 (3次) を加算して輝きを調整
// brightness = 0.5 のとき完全スルー (±デッドバンド 0.01)
// brightnessState: 指数平滑 (係数0.96/0.04) でポップ防止
static inline float ProcessBrightness(float input, float* brightnessState, float brightness) {
	if (fabs(brightness - 0.5f) < 0.01f) return input;
	float harmonic = input * input * input;   // 3次歪み成分
	float brightnessFactor = (brightness - 0.5f) * 2.0f;
	*brightnessState = *brightnessState * 0.96f + harmonic * brightnessFactor * 0.04f;
	return input + *brightnessState * 0.1f;
}

// 共鳴処理: 特定周波数のピーキングEQで空間共鳴を模倣
static inline float ProcessResonance(ChannelState* cs, float input, float freq, float q, float amount) {
	if (amount <= 0.0f || freq <= 0.0f) return input;
	return input + ProcessBiquad(&cs->resonanceFilter, input) * amount * 0.5f;
}

// 金属質感: 高Qハイパスで金属的な倍音を付加
static inline float ProcessMetallic(ChannelState* cs, float input, float amount) {
	if (amount <= 0.0f) return input;
	return input + ProcessBiquad(&cs->metallicFilter, input) * amount * 0.4f;
}

// ガラス質感: より高域のハイパスで透明感・鋭さを付加
static inline float ProcessGlass(ChannelState* cs, float input, float amount) {
	if (amount <= 0.0f) return input;
	return input + ProcessBiquad(&cs->glassFilter, input) * amount * 0.35f;
}

// シマー: 高調波ゆらぎで空間的な輝きを付加
// shimmerState: 指数平滑値を高速サイン波で変調
static inline float ProcessShimmer(ChannelState* cs, float input, float amount, int sampleRate) {
	if (amount <= 0.0f) return input;
	cs->shimmerState = cs->shimmerState * 0.992f + input * 0.008f;
	return input + sinf(cs->shimmerState * 12.0f) * amount * 0.15f;
}

// ドップラー効果: 低速正弦波変調でピッチ感覚的な変化を模倣
// dopplerPhase: 0.5Hz 固定 (0.5/sampleRate per sample)
static inline float ProcessDoppler(ChannelState* cs, float input, float amount, int sampleRate) {
	if (amount <= 0.0f) return input;
	cs->dopplerPhase += 0.5f / (float)sampleRate;
	if (cs->dopplerPhase >= 1.0f) cs->dopplerPhase -= 1.0f;
	return input * (1.0f + sinf(cs->dopplerPhase * 2.0f * M_PI) * amount * 0.02f);
}

// ============================================================
// ★ ウェット(残響/エコー)信号のキャラクター付与
// ============================================================
//  従来 DSP で宣言だけされ未使用だった環境パラメータ
//  (airAbsorption / humidity / altitude / reverbColor / reverbSmooth /
//   combFiltering / windEffect) をここで残響成分に反映する。
//  これにより 100 個の音響プリセットが「残響の明暗・吸収・揺らぎ・筒鳴り」
//  といった質感で互いに聴き分けられるようになる。
//  音量(ピーク)は最終段のルックアヘッドリミッターが抑えるため、
//  ここでは音割れを気にせずトーン/質感の差異付けに専念できる。
static inline float ProcessWetCharacter(ChannelState* cs, float wet,
	const EnvParams* env, int sampleRate)
{
	if (!isfinite(wet)) return 0.0f;

	// --- 空気吸収: 距離・湿度・高度が高いほど残響尾の高域を減衰 ---
	float airHF = env->airAbsorption + env->humidity * 0.4f + env->altitude * 0.3f;
	if (airHF > 0.0f) {
		if (airHF > 1.0f) airHF = 1.0f;
		float a = 0.10f + airHF * 0.80f;            // airHF大→平滑強→高域減衰
		cs->wetAirState += a * (wet - cs->wetAirState);
		wet = wet * (1.0f - airHF) + cs->wetAirState * airHF;
	}

	// --- 残響の色味 (reverbColor: 0=暗い, 0.5=中立, 1=明るい) ---
	if (fabsf(env->reverbColor - 0.5f) > 0.01f) {
		cs->wetColorState += 0.25f * (wet - cs->wetColorState);
		float lowPart = cs->wetColorState;
		float highPart = wet - lowPart;
		float tilt = (env->reverbColor - 0.5f) * 1.6f;  // -0.8..+0.8
		wet = lowPart + highPart * (1.0f + tilt);
	}

	// --- 残響の滑らかさ (reverbSmooth: 0=粗い, 1=滑らか) ---
	if (env->reverbSmooth > 0.0f) {
		float sc = 0.06f + env->reverbSmooth * 0.40f;
		cs->wetSmoothState += sc * (wet - cs->wetSmoothState);
		float mix = env->reverbSmooth * 0.6f;
		wet = wet * (1.0f - mix) + cs->wetSmoothState * mix;
	}

	// --- コムフィルタリング (筒・金属・反復反射の色付け) ---
	if (env->combFiltering > 0.0f) {
		float baseFreq = (env->resonanceFreq > 80.0f) ? env->resonanceFreq : 220.0f;
		int d = (int)((float)sampleRate / baseFreq);
		if (d < 8) d = 8;
		if (d > 2047) d = 2047;
		int rp = cs->combPos - d; if (rp < 0) rp += 2048;
		float dl = cs->combBuf[rp];
		float g = env->combFiltering * 0.55f;
		float out = wet + dl * g;
		cs->combBuf[cs->combPos] = wet;
		cs->combPos = (cs->combPos + 1) & 2047;
		wet = out * (1.0f - g * 0.4f);              // ピーク膨張を補正
	}

	// --- 風の効果 (屋外プリセットの自然な揺らぎ) ---
	if (env->windEffect > 0.0f) {
		cs->windPhase += 0.27f / (float)sampleRate; // ~0.27Hz の超低周波ゆらぎ
		if (cs->windPhase >= 1.0f) cs->windPhase -= 1.0f;
		float w = sinf(cs->windPhase * 2.0f * (float)M_PI)
			+ 0.5f * sinf(cs->windPhase * 6.3f * (float)M_PI);
		wet *= (1.0f + w * env->windEffect * 0.12f);
	}

	return isfinite(wet) ? wet : 0.0f;
}

// ============================================================
// ★ 最終ミックス(ドライ+ウェット)のキャラクター付与
// ============================================================
//  未使用だった weight / woodiness / concrete / softness / phasing /
//  distortion を最終ミックスに反映し、プリセット固有の「音の重さ・素材感・
//  柔らかさ・うねり・質感歪み」を付ける。
//  distortion はプリセット意図に基づく軽いサチュレーションで、
//  最終リミッターによりハードクリップ(音割れ)には至らない。
static inline float ProcessMixCharacter(ChannelState* cs, float mixed,
	const EnvParams* env, int sampleRate)
{
	if (!isfinite(mixed)) return 0.0f;

	// --- 重さ (weight: 0=軽い, 0.5=中立, 1=重い) 低域の量感 ---
	if (fabsf(env->weight - 0.5f) > 0.01f) {
		cs->wetWeightState += 0.05f * (mixed - cs->wetWeightState);
		float w = (env->weight - 0.5f) * 1.2f;      // -0.6..+0.6
		mixed += cs->wetWeightState * w * 0.6f;
	}

	// --- 素材感 (木質=暖色寄り / コンクリート=硬く乾いた色) ---
	float bodyTone = env->woodiness * 0.5f - env->concrete * 0.4f;
	if (fabsf(bodyTone) > 0.01f) {
		cs->bodyToneState += 0.12f * (mixed - cs->bodyToneState);
		mixed += cs->bodyToneState * bodyTone * 0.25f;
	}

	// --- 柔らかさ (softness: 高域/トランジェントを軽く丸める) ---
	if (env->softness > 0.0f) {
		float a = 0.5f - env->softness * 0.35f;
		cs->softState += a * (mixed - cs->softState);
		float mix = env->softness * 0.4f;
		mixed = mixed * (1.0f - mix) + cs->softState * mix;
	}

	// --- フェイジング (うねり: 2段オールパス) ---
	if (env->phasing > 0.0f) {
		cs->phaserLfo += 0.5f / (float)sampleRate;
		if (cs->phaserLfo >= 1.0f) cs->phaserLfo -= 1.0f;
		float mod = 0.4f + 0.4f * sinf(cs->phaserLfo * 2.0f * (float)M_PI);
		float x = mixed;
		for (int s = 0; s < 2; s++) {
			float y = -mod * x + cs->phaserState[s];
			cs->phaserState[s] = x + mod * y;
			x = y;
		}
		float mix = env->phasing * 0.5f;
		mixed = mixed * (1.0f - mix) + x * mix;
	}

	// --- 質感歪み (distortion: プリセット意図の軽いサチュレーション) ---
	if (env->distortion > 0.0f) {
		float drive = 1.0f + env->distortion * 2.5f;
		float d = tanhf(mixed * drive) / tanhf(drive);
		mixed = mixed * (1.0f - env->distortion) + d * env->distortion;
	}

	return isfinite(mixed) ? mixed : 0.0f;
}


// ============================================================
// ★ プロフェッショナル・ソフトサチュレーション [FIX-2]
// ============================================================
//
// 【knee改訂理由: 0.65 → 0.78】
//   旧 knee=0.65 ではマスタード楽曲の典型ピーク (0.70-0.90) が
//   常にサチュレーション領域に入り、3次高調波が全ピークに付加された。
//   これが「音割れ感」「張り付き感」として聴取される原因だった。
//   新 knee=0.78 では正常処理後の信号(≤0.70想定)は完全透明。
//
// 【収束特性】
//   入力 0.50 → 出力 0.500  (無変化)
//   入力 0.78 → 出力 0.780  (ニー点、無変化)
//   入力 0.85 → 出力 0.844  (-0.7%、ほぼ透明)
//   入力 1.00 → 出力 0.929  (-7.1%、軽微サチュレーション)
//   入力 1.30 → 出力 0.968  (ceiling近傍)
//   入力 ∞   → 出力 0.970  (漸近上限、ハードクリップなし)
// ============================================================
static inline float ProfessionalSoftSaturate(float x)
{
	if (!isfinite(x)) return 0.0f;

	const float knee = 0.78f;          // [FIX-2] 旧 0.65
	const float ceiling = 0.97f;          // 漸近上限
	const float r = ceiling - knee; // = 0.19

	float absX = fabsf(x);
	if (absX <= knee) return x;           // knee以下は完全透明

	// tanh で滑らかに上限へ収束 (arg上限8: 精度で十分)
	float arg = (absX - knee) / r;
	if (arg > 8.0f) arg = 8.0f;

	float sign = (x >= 0.0f) ? 1.0f : -1.0f;
	return sign * (knee + r * tanhf(arg));
}


// ============================================================
// [FIX-3] BlockAnalysis — ブロック解析・透明ゲインステージング
// ============================================================
//
// 【クレストファクターによるコンテンツ判定】
//   矩形波    : CF ≈ 1.0  → isChiptune=TRUE
//   のこぎり波 : CF ≈ 1.73 → isChiptune=TRUE
//   FM合成音  : CF ≈ 2.5  → isChiptune=TRUE
//   通常音楽  : CF ≈ 4-8  → 標準処理
//   音声のみ  : CF > 9    → isVoice=TRUE
//
// 【FIX-COMP: stageTarget 変更】
//   旧: isChiptune=0.65 / 通常=0.60
//   新: isChiptune=0.82 / 通常=0.90
//   理由: 旧値では通常音楽(ピーク0.7-0.9)が常に圧縮対象となり
//         籠もり感の原因だった。最終保護は ProfessionalSoftSaturate
//         (knee=0.78) に完全委譲するため、staging閾値を大幅緩和。
// ============================================================
typedef struct {
	float peak;
	float rms;
	float crestFactor;
	BOOL  isChiptune;
	BOOL  isVoice;
	float stagingGain;   // masterGain適用後にstageTargetを超えた分の補正ゲイン
} BlockAnalysis;

static BlockAnalysis AnalyzeBlock(
	const unsigned char* pRaw,
	int numSamples, int wavch, int wavsam, int bytesPerSample,
	float masterGain)
{
	BlockAnalysis ba = { 0.0f, 0.001f, 1.0f, FALSE, FALSE, 1.0f };
	double sumSq = 0.0;
	int    count = 0;

	// ピーク・RMS を一括計算
	for (int i = 0; i < numSamples; i++) {
		for (int ch = 0; ch < wavch && ch < MAX_CH; ch++) {
			float s = 0.0f;
			int offset = (i * wavch + ch) * bytesPerSample;
			if (wavsam == 16) s = *((short*)(pRaw + offset)) / 32768.0f;
			else if (wavsam == 24) {
				int val = pRaw[offset] | (pRaw[offset + 1] << 8) | ((signed char)pRaw[offset + 2] << 16);
				s = val / 8388608.0f;
			}
			else if (wavsam == 32) s = *((int*)(pRaw + offset)) / 2147483648.0f;
			else                   s = (pRaw[offset] - 128) / 128.0f;

			float absS = fabsf(s);
			if (absS > ba.peak) ba.peak = absS;
			sumSq += (double)s * s;
			count++;
		}
	}

	if (count > 0 && sumSq > 0.0)
		ba.rms = (float)sqrt(sumSq / count);

	// クレストファクター = ピーク / RMS
	ba.crestFactor = (ba.rms > 0.0002f) ? (ba.peak / ba.rms) : 1.0f;

	// コンテンツ分類
	ba.isChiptune = (ba.crestFactor < 3.5f && ba.peak > 0.04f);
	ba.isVoice = (ba.crestFactor > 9.0f && ba.peak > 0.02f);

	// [FIX-COMP] stageTarget 緩和: 通常音楽はほぼ無処理
	// ProfessionalSoftSaturate(knee=0.78) が最終保護を担う
	const float stageTarget = ba.isChiptune ? 0.82f : 0.90f;

	float postGainPeak = ba.peak * masterGain;
	if (postGainPeak > stageTarget && postGainPeak > 0.001f)
		ba.stagingGain = stageTarget / postGainPeak;

	return ba;
}


// ============================================================
// グローバル状態: ブロック間ゲイン平滑値 [FIX-COMP]
// ============================================================
// InitEngine でリセット。attack/release 非対称で自然な圧縮感を実現。
// attack =0.08: 1ブロックで最大8%圧縮 → 急激な大音量でも緩やかに追従
// release=0.30: 3〜4ブロックで元のゲインに復帰 → 不自然な揺り戻しなし
static float g_stagingGainSmooth[EQ_BANKS] = { 1.0f, 1.0f };
static float g_laLimEnv[EQ_BANKS] = { 1.0f, 1.0f };
static float g_eqLeftSamples[EQ_BANKS][8192 * 40];
static float g_eqRightSamples[EQ_BANKS][8192 * 40];



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
	memset(&g_fxReverb[g_eqCur], 0, sizeof(g_fxReverb));
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
	memset(&g_fxChorus[g_eqCur], 0, sizeof(g_fxChorus));
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
	memset(&g_fxDelay[g_eqCur], 0, sizeof(g_fxDelay));
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


// ===== エンジン初期化 =====
// サンプルレート変更または reset==1 時に呼ばれる
// 全チャンネル状態のクリア、ディレイバッファのゼロ埋め、
// リミッター係数の再計算を行う
static void InitEngine(int rate, int bank = 0) {
	if (bank < 0 || bank >= EQ_BANKS) bank = 0;
	const int prevBank = g_eqCur;
	g_eqCur = bank;
	// memset 前に山彦バッファを退避して解放する。
	// 先に memset するとポインタが消え、毎回 malloc 分がリークする。
	float* oldYamabiko[MAX_CH];
	for (int i = 0; i < MAX_CH; i++)
		oldYamabiko[i] = g_channels[g_eqCur][i].yamabikoBuf;

	memset(g_channels[g_eqCur], 0, sizeof(g_channels[g_eqCur]));
	memset(g_delayMemory[g_eqCur], 0, sizeof(g_delayMemory[g_eqCur]));

	for (int i = 0; i < MAX_CH; i++) {
		if (oldYamabiko[i] != NULL)
			free(oldYamabiko[i]);

		g_channels[g_eqCur][i].delayBuffer = g_delayMemory[g_eqCur][i];
		g_channels[g_eqCur][i].lfo.phase = 0.0f;
		g_channels[g_eqCur][i].flutterPhase = 0.0f;
		g_channels[g_eqCur][i].dopplerPhase = 0.0f;
		g_channels[g_eqCur][i].phasingPhase = 0.0f;

		for (int j = 0; j < 8; j++) {
			g_channels[g_eqCur][i].diffusionPos1[j] = 0;
			g_channels[g_eqCur][i].diffusionPos2[j] = 0;
			g_channels[g_eqCur][i].diffusionPos3[j] = 0;
		}

		g_channels[g_eqCur][i].harmonicState = 0.0f;
		g_channels[g_eqCur][i].earlyEnvelope = 0.0f;
		g_channels[g_eqCur][i].lateEnvelope = 0.0f;
		g_channels[g_eqCur][i].warmthState = 0.0f;
		g_channels[g_eqCur][i].brightnessState = 0.0f;
		g_channels[g_eqCur][i].shimmerState = 0.0f;

		// 山彦バッファ: rate×2秒分 (最大遅延 2秒)
		g_channels[g_eqCur][i].yamabikoBufSize = rate * 2;
		g_channels[g_eqCur][i].yamabikoPos = 0;
		g_channels[g_eqCur][i].yamabikoBuf = (float*)malloc(sizeof(float) * g_channels[g_eqCur][i].yamabikoBufSize);
		if (g_channels[g_eqCur][i].yamabikoBuf != NULL)
			memset(g_channels[g_eqCur][i].yamabikoBuf, 0, sizeof(float) * g_channels[g_eqCur][i].yamabikoBufSize);
	}

	g_lastRate[g_eqCur] = rate;
	g_lastEqPreset[g_eqCur] = -1;
	g_lastEnvPreset[g_eqCur] = -1;

	for (int i = 0; i < 15; i++) g_lastEqValues[g_eqCur][i] = 100;
	for (int i = 0; i < 5; i++) g_lastExtendedParams[g_eqCur][i] = 100;

	// リミッター係数: attack=1ms, release=100ms
	for (int ch = 0; ch < 2; ch++) {
		g_limiter[g_eqCur][ch].envelope = 1.0f;
		g_limiter[g_eqCur][ch].threshold = 0.95f;
		g_limiter[g_eqCur][ch].attackCoeff = expf(-1.0f / (0.001f * rate));
		g_limiter[g_eqCur][ch].releaseCoeff = expf(-1.0f / (0.100f * rate));
		g_extBoostLimiter[g_eqCur][ch].envelope = 1.0f;
		g_extBoostLimiter[g_eqCur][ch].threshold = 0.95f;
		g_extBoostLimiter[g_eqCur][ch].attackCoeff = g_limiter[g_eqCur][ch].attackCoeff;
		g_extBoostLimiter[g_eqCur][ch].releaseCoeff = g_limiter[g_eqCur][ch].releaseCoeff;
	}

	g_lastEffectAmount[g_eqCur] = 50;
	g_initialized[g_eqCur] = TRUE;

	// 追加エフェクトパラメータ (0 = オフ)
	g_eqReverb = 0;
	g_eqChorus = 0;
	g_eqDelay = 0;

	// 追加エフェクト(リバーブ/コーラス/ディレイ)の状態とディレイ尾をクリア
	FxReverbReset(rate);
	FxChorusReset(rate);
	FxDelayReset(rate);

	// [FIX-COMP] ブロック間平滑ゲインをリセット
	g_stagingGainSmooth[g_eqCur] = 1.0f;
	g_laLimEnv[g_eqCur] = 1.0f;
	g_eqCur = prevBank;
}

// ===== 山彦バッファ解放 =====
// アプリ終了時またはエンジン破棄時に呼ぶ
void FreeEngine(void) {
	for (int b = 0; b < EQ_BANKS; b++) {
		for (int i = 0; i < MAX_CH; i++) {
			if (g_channels[b][i].yamabikoBuf != NULL) {
				free(g_channels[b][i].yamabikoBuf);
				g_channels[b][i].yamabikoBuf = NULL;
				g_channels[b][i].yamabikoBufSize = 0;
				g_channels[b][i].yamabikoPos = 0;
			}
		}
	}
}

// 汎用クランプ
static float ClampFloat(float v, float lo, float hi) {
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

// 擬似乱数 (ハッシュベース, 範囲[0,1])
// ApplyEnvSeparation でプリセット毎の個性付けに使用
static float Hash01(int idx, int salt) {
	unsigned int x = (unsigned int)(idx * 1664525u + 1013904223u + (unsigned int)salt * 2654435761u);
	x ^= (x >> 16); x *= 2246822519u; x ^= (x >> 13);
	return (x & 0xFFFFFF) / 16777215.0f;
}

// 環境プリセット間の分離調整
// 同カテゴリ内の各プリセットに微妙な差異を付与して聴き分けを容易にする
// presetIndex: 1-100 (0は無処理)
// 各パラメータにカテゴリバイアス + ハッシュオフセットを加算
static void ApplyEnvSeparation(int presetIndex, EnvParams* env) {
	if (!env || presetIndex <= 0) return;
	int category = (presetIndex - 1) / 10;
	if (category < 0) category = 0;
	if (category > 9) category = 9;
	int pos = (presetIndex - 1) % 10;
	float t = (pos <= 0) ? 0.0f : (pos >= 9) ? 1.0f : (float)pos / 9.0f;

	float h1 = Hash01(presetIndex, 11) - 0.5f;
	float h2 = Hash01(presetIndex, 23) - 0.5f;
	float h3 = Hash01(presetIndex, 37) - 0.5f;

	// カテゴリ別バイアステーブル (roomSize/damping/stereoWidth)
	static const float kRoomBias[10] = { 0.0f, 0.25f,-0.10f, 0.20f,-0.20f, 0.35f, 0.10f,-0.15f, 0.45f, 0.30f };
	static const float kDampBias[10] = { 0.0f,-0.05f, 0.15f,-0.05f, 0.20f,-0.10f, 0.05f, 0.10f,-0.20f,-0.15f };
	static const float kWidthBias[10] = { 0.0f, 0.10f,-0.05f, 0.05f,-0.10f, 0.20f, 0.15f, 0.00f, 0.40f, 0.30f };

	env->preDelayMs = ClampFloat(env->preDelayMs + (t - 0.5f) * 12.0f + h1 * 8.0f, 0.0f, 120.0f);
	env->delayTimeMs = ClampFloat(env->delayTimeMs * (1.0f + (t - 0.5f) * 0.25f + h2 * 0.15f), 6.0f, 350.0f);
	env->roomSize = ClampFloat(env->roomSize + kRoomBias[category] + h3 * 0.25f, 0.3f, 5.0f);
	env->stereoWidth = ClampFloat(env->stereoWidth + kWidthBias[category] + h1 * 0.30f, 0.3f, 2.5f);
	env->damping = ClampFloat(env->damping + kDampBias[category] + h2 * 0.20f, 0.0f, 1.0f);
}

static float ProcessDynamicLimiter(DynamicLimiter* lim, float input) {
	float absInput = fabsf(input);
	float targetGain = (absInput > lim->threshold) ? lim->threshold / absInput : 1.0f;
	float coeff = (targetGain < lim->envelope) ? lim->attackCoeff : lim->releaseCoeff;
	lim->envelope = targetGain + coeff * (lim->envelope - targetGain);
	return input * lim->envelope;
}

// ============================================================
// ★ ルックアヘッド・ピークリミッター（最終段・ステレオリンク）
// ============================================================
//  ブロック全体がバッファ済みなので「先読み」が可能。各サンプルで
//  今後 La サンプル以内の最大ピークを見て、ピーク到達前にゲインを下げる。
//  → オーバーシュートが出ないので tanh サチュレーションが不要になり、
//    0.78〜0.97 域の高調波歪み(音割れ感/濁り)が乗らない。
//  → 静部は無処理・ピークのみ動的減衰なので音量(ラウドネス)は維持。
//  L/R 同一ゲインで処理しステレオ像を保つ。遅延はバッファ内先読みのため0。

static void ApplyLookaheadLimiterStereo(float* L, float* R, int n, int rate, float ceiling)
{
	if (!L || !R || n <= 0 || rate <= 0) return;
	static const int CAP = 8192 * 40;
	if (n > CAP) n = CAP;

	int La = rate / 700;            // 先読み ≈ 1.4ms
	if (La < 8)    La = 8;
	if (La > 1024) La = 1024;

	static float req[CAP];          // 各サンプルの必要ゲイン(<=1)
	static int   dq[CAP];           // 単調デック(スライディング最小)

	for (int i = 0; i < n; ++i) {
		const float p = fmaxf(fabsf(L[i]), fabsf(R[i]));
		req[i] = (p > ceiling) ? (ceiling / p) : 1.0f;
	}

	int dqHead = 0, dqTail = 0;
	auto pushBack = [&](int k) {
		while (dqTail > dqHead && req[dq[dqTail - 1]] >= req[k]) --dqTail;
		dq[dqTail++] = k;
	};
	for (int k = 0; k <= La && k < n; ++k) pushBack(k);

	const float atkCoeff = expf(-1.0f / (0.0003f * (float)rate)); // ~0.3ms (La内で収束)
	const float relCoeff = expf(-1.0f / (0.080f * (float)rate));  // ~80ms

	for (int i = 0; i < n; ++i) {
		const float laMin = req[dq[dqHead]];      // [i, i+La] の最小必要ゲイン
		const int add = i + La + 1;
		if (add < n) pushBack(add);
		if (dq[dqHead] == i) ++dqHead;

		const float coeff = (laMin < g_laLimEnv[g_eqCur]) ? atkCoeff : relCoeff;
		g_laLimEnv[g_eqCur] = laMin + coeff * (g_laLimEnv[g_eqCur] - laMin);
		if (g_laLimEnv[g_eqCur] > 1.0f) g_laLimEnv[g_eqCur] = 1.0f;
		if (g_laLimEnv[g_eqCur] < 0.0f) g_laLimEnv[g_eqCur] = 0.0f;

		L[i] *= g_laLimEnv[g_eqCur];
		R[i] *= g_laLimEnv[g_eqCur];
	}
}

// ============================================================
// 拡張音量 / フォーマット別音量ブースト (マスター音量とは独立)
// COggDlg 拡張音量 + CRender SPC/KPI/MP3 倍率をここで1本化
// ============================================================
#define EQ_FMT_VOL_NONE 0
#define EQ_FMT_VOL_KPI  1
#define EQ_FMT_VOL_MP3  2

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

static float EqCalcKpiVolumeGain(int kpiVal, int bitDepth)
{
	if (kpiVal == 1) return 1.0f;
	if (bitDepth == 16) {
		switch (kpiVal) {
		case 2: return 2.0f;
		case 3: return 3.0f;
		case 4: return 4.0f;
		case 5: return 5.0f;
		default: return 1.0f;
		}
	}
	switch (kpiVal) {
	case 2:  return 2.0f;
	case 4:  return 3.0f;
	case 8:  return 4.0f;
	case 16: return 5.0f;
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
	case EQ_FMT_VOL_KPI: {
		float g = EqCalcKpiVolumeGain(savedata.kpivol, wavsam_depth);
		if (g_eqFormatVolSpc)
			g *= EqCalcSpcVolumeGain(savedata.spc);
		return g;
	}
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

static float ApplyExternalBoostSample(float sample, int ch, float extGain)
{
	const int limCh = (ch < 2) ? ch : (ch & 1);
	sample *= extGain;
	sample = ProcessDynamicLimiter(&g_extBoostLimiter[g_eqCur][limCh], sample);
	return ProfessionalSoftSaturate(sample);
}

static void ApplyExternalBoostOnlyToBuffer(
	unsigned char* pRaw, int numSamples, int wavch, int wavsam, int bytesPerSample)
{
	const float extGain = GetExternalBoostGain();
	for (int i = 0; i < numSamples; i++) {
		for (int ch = 0; ch < wavch; ch++) {
			int offset = (i * wavch + ch) * bytesPerSample;
			float s = PcmSampleToFloat(pRaw, offset, wavsam);
			s = ApplyExternalBoostSample(s, ch, extGain);
			FloatToPcmSample(pRaw, offset, wavsam, s);
		}
	}
}


// ============================================================
// ResampleUp() - Lanczos-2 アップサンプリング
// ============================================================
// srcRate < 32000 の音源のみ 44100Hz に変換して内部処理する（32kHz OGG 等はネイティブ）
// Lanczos カーネル (a=2): sinc(x) * sinc(x/a) の積
// タップ数: ±2 (計5点) — 品質とCPU負荷のバランス点
// ============================================================
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

	*dstLen = dstSamples * frameBytes;
	*dstData = malloc(*dstLen);
	if (!(*dstData)) return;

	float* srcFloat = (float*)malloc(srcSamples * channels * sizeof(float));
	float* dstFloat = (float*)malloc(dstSamples * channels * sizeof(float));
	if (!srcFloat || !dstFloat) {
		free(srcFloat);
		free(dstFloat);
		free(*dstData);
		*dstData = NULL;
		*dstLen = 0;
		return;
	}
	unsigned char* pSrc = (unsigned char*)srcData;

	// 整数 PCM → 浮動小数点 [-1.0, 1.0] に変換
	if (bitDepth == 8) {
		for (int i = 0; i < srcSamples * channels; i++)
			srcFloat[i] = ((float)pSrc[i] - 128.0f) / 128.0f;
	}
	else if (bitDepth == 16) {
		short* p = (short*)srcData;
		for (int i = 0; i < srcSamples * channels; i++) srcFloat[i] = p[i] * (1.0f / 32768.0f);
	}
	else if (bitDepth == 24) {
		for (int i = 0; i < srcSamples * channels; i++) {
			int o = i * 3;
			// 24bit LE: byte0|(byte1<<8)|(byte2<<16)、符号拡張して 2^23 で正規化
			uint32_t u = (uint32_t)pSrc[o] | ((uint32_t)pSrc[o + 1] << 8) | ((uint32_t)pSrc[o + 2] << 16);
			int32_t s = (u & 0x800000u) ? (int32_t)(u | 0xFF000000u) : (int32_t)u;
			srcFloat[i] = (float)s * (1.0f / 8388608.0f);
		}
	}
	else if (bitDepth == 32) {
		int* p = (int*)srcData;
		for (int i = 0; i < srcSamples * channels; i++) srcFloat[i] = p[i] * (1.0f / 2147483648.0f);
	}

	// Lanczos-2 補間
	double ratio = (double)dstRate / srcRate;
	for (int i = 0; i < dstSamples; i++) {
		double srcPos = i / ratio;
		int    srcInt = (int)srcPos;
		float  frac = (float)(srcPos - srcInt);
		for (int ch = 0; ch < channels; ch++) {
			float sum = 0.0f;
			for (int j = -2; j <= 2; j++) {
				int idx = srcInt + j;
				if (idx >= 0 && idx < srcSamples)
					sum += srcFloat[idx * channels + ch] * LanczosKernel2(frac - j);
			}
			dstFloat[i * channels + ch] = sum;
		}
	}

	// 浮動小数点 → 整数 PCM に変換
	unsigned char* pDst = (unsigned char*)(*dstData);
	if (bitDepth == 8) {
		for (int i = 0; i < dstSamples * channels; i++) {
			float s = dstFloat[i];
			if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
			pDst[i] = (unsigned char)(s * 127.0f + 128.0f);
		}
	}
	else if (bitDepth == 16) {
		short* p = (short*)(*dstData);
		for (int i = 0; i < dstSamples * channels; i++) {
			float s = dstFloat[i];
			if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
			int32_t v = (int32_t)roundf(s * 32768.0f);
			if (v > 32767) v = 32767; if (v < -32768) v = -32768;
			p[i] = (short)v;
		}
	}
	else if (bitDepth == 24) {
		for (int i = 0; i < dstSamples * channels; i++) {
			float s = dstFloat[i];
			if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
			int32_t v = (int32_t)roundf(s * 8388608.0f);
			if (v > 8388607) v = 8388607; if (v < -8388608) v = -8388608;
			int o = i * 3;
			// [FIX-5] リトルエンディアン 24bit PCM 正規バイト順
			pDst[o] = v & 0xFF;
			pDst[o + 1] = (v >> 8) & 0xFF;
			pDst[o + 2] = (v >> 16) & 0xFF;
		}
	}
	else if (bitDepth == 32) {
		int* p = (int*)(*dstData);
		for (int i = 0; i < dstSamples * channels; i++) {
			float s = dstFloat[i];
			if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
			p[i] = (int)(s * 2147483647.0f);
		}
	}
	free(srcFloat); free(dstFloat);
}

// 簡易ローパスフィルタ (3点移動平均)
// ResampleDown 前のエイリアシング抑制プレフィルタとして使用
static void ApplyFastLPF(float* data, int samples, int channels, float cutoff) {
	for (int ch = 0; ch < channels; ch++) {
		float prev = data[ch];
		for (int i = 1; i < samples - 1; i++) {
			int   idx = i * channels + ch;
			float curr = data[idx], next = data[idx + channels];
			data[idx] = (prev + curr + next) * 0.333333f;
			prev = curr;
		}
	}
}

// ResampleDown() - Lanczos-2 ダウンサンプリング
// 44100Hz 内部処理後に元のサンプルレートへ戻す
// cutoff < 0.9 の場合は3点LPFでエイリアシングを抑制
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

	float* srcFloat = (float*)malloc(srcSamples * channels * sizeof(float));
	float* dstFloat = (float*)malloc(dstSamples * channels * sizeof(float));
	if (!srcFloat || !dstFloat) {
		free(srcFloat);
		free(dstFloat);
		return;
	}
	unsigned char* pSrc = (unsigned char*)srcData;

	if (bitDepth == 8) {
		for (int i = 0; i < srcSamples * channels; i++)
			srcFloat[i] = ((float)pSrc[i] - 128.0f) / 128.0f;
	}
	else if (bitDepth == 16) {
		short* p = (short*)srcData;
		for (int i = 0; i < srcSamples * channels; i++) srcFloat[i] = p[i] * (1.0f / 32768.0f);
	}
	else if (bitDepth == 24) {
		for (int i = 0; i < srcSamples * channels; i++) {
			int o = i * 3;
			uint32_t u = (uint32_t)pSrc[o] | ((uint32_t)pSrc[o + 1] << 8) | ((uint32_t)pSrc[o + 2] << 16);
			int32_t s = (u & 0x800000u) ? (int32_t)(u | 0xFF000000u) : (int32_t)u;
			srcFloat[i] = (float)s * (1.0f / 8388608.0f);
		}
	}
	else if (bitDepth == 32) {
		int* p = (int*)srcData;
		for (int i = 0; i < srcSamples * channels; i++) srcFloat[i] = p[i] * (1.0f / 2147483648.0f);
	}

	// エイリアシング防止: ダウンサンプル比が大きいときのみ適用
	float cutoff = (float)dstRate / srcRate;
	if (cutoff < 0.9f) ApplyFastLPF(srcFloat, srcSamples, channels, cutoff);

	double ratio = (double)dstRate / srcRate;
	for (int i = 0; i < dstSamples; i++) {
		double srcPos = i / ratio;
		int    srcInt = (int)srcPos;
		float  frac = (float)(srcPos - srcInt);
		for (int ch = 0; ch < channels; ch++) {
			float sum = 0.0f;
			for (int j = -2; j <= 2; j++) {
				int idx = srcInt + j;
				if (idx >= 0 && idx < srcSamples)
					sum += srcFloat[idx * channels + ch] * LanczosKernel2(frac - j);
			}
			dstFloat[i * channels + ch] = sum;
		}
	}

	unsigned char* pDst = (unsigned char*)dstData;
	if (bitDepth == 8) {
		for (int i = 0; i < dstSamples * channels; i++) {
			float s = dstFloat[i]; if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
			pDst[i] = (unsigned char)(s * 127.0f + 128.0f);
		}
	}
	else if (bitDepth == 16) {
		short* p = (short*)dstData;
		for (int i = 0; i < dstSamples * channels; i++) {
			float s = dstFloat[i]; if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
			int32_t v = (int32_t)roundf(s * 32768.0f);
			if (v > 32767) v = 32767; if (v < -32768) v = -32768;
			p[i] = (short)v;
		}
	}
	else if (bitDepth == 24) {
		for (int i = 0; i < dstSamples * channels; i++) {
			float s = dstFloat[i]; if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
			int32_t v = (int32_t)roundf(s * 8388608.0f);
			if (v > 8388607) v = 8388607; if (v < -8388608) v = -8388608;
			int o = i * 3;
			pDst[o] = v & 0xFF; pDst[o + 1] = (v >> 8) & 0xFF; pDst[o + 2] = (v >> 16) & 0xFF;
		}
	}
	else if (bitDepth == 32) {
		int* p = (int*)dstData;
		for (int i = 0; i < dstSamples * channels; i++) {
			float s = dstFloat[i]; if (s > 1.0f) s = 1.0f; else if (s < -1.0f) s = -1.0f;
			p[i] = (int)(s * 2147483647.0f);
		}
	}
	free(srcFloat); free(dstFloat);
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
	std::lock_guard<std::mutex> lk(g_eqMu);
	int rate = wavbit_sample_Hz;
	if (rate < 8000) rate = 44100;
	InitEngine(rate, bank);
}

void equaliserBank(int bank, void* data, int len, BOOL reset,
	int bitsOverride, int chOverride, int rateOverride) {
	if (bank < 0 || bank >= EQ_BANKS) bank = 0;
	std::lock_guard<std::mutex> lk(g_eqMu);
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
	equaliserBank(0, data, len, reset);
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

	// ========================================
	// リサンプリング: 22050Hz 等の低レートのみ 44100Hz へ一時変換。
	// 32000Hz / 44100Hz / 48000Hz はネイティブレートで EQ 処理（32kHz OGG 向け）。
	// ========================================
	if (len <= 0)
		return;

	int  originalRate = wavbit_sample_Hz;
	int  originalLen = len;
	void* processData = data;
	int   processLen = len;
	void* tempBuffer = NULL;
	const int kEqNativeMinRate = 32000;
	BOOL  needsResampling = (originalRate > 0 && originalRate < kEqNativeMinRate);

	wavbitbackup = originalRate;

	if (needsResampling) {
		ResampleUp(data, len, &tempBuffer, &processLen, originalRate, 44100, wavchannel, wavsam_depth);
		if (!tempBuffer || processLen <= 0) {
			if (tempBuffer) free(tempBuffer);
			return;
		}
		processData = tempBuffer;
		wavbitbackup = 44100;
	}

	{
		const int bpfEq = (wavsam_depth / 8) * wavchannel;
		if (bpfEq <= 0 || processLen < bpfEq) {
			if (needsResampling && tempBuffer)
				free(tempBuffer);
			return;
		}
	}

	// ========================================
	// 初期化・パラメータ取得
	// ========================================
	BOOL forceUpdate = FALSE;
	if (reset == 1 || !g_initialized[g_eqCur] || g_lastRate[g_eqCur] != wavbitbackup) {
		InitEngine(wavbitbackup, g_eqCur);
		forceUpdate = TRUE;
	}

	int currentEqPre = savedata.eqsoundeq;
	int currentEnvPre = savedata.eqsoundenv;
	int effectAmount = savedata.eqsoundeffect;

	if (effectAmount < 0)   effectAmount = 0;
	if (effectAmount > 100) effectAmount = 100;

	int masterVolume = savedata.eq[15];
	int clarity = savedata.eq[16];
	int balance = savedata.eq[17];
	int density = savedata.eq[18];
	int spatial = savedata.eq[19];

	masterVolume = (int)ClampFloat((float)masterVolume, 0.0f, 200.0f);
	clarity = (int)ClampFloat((float)clarity, 0.0f, 200.0f);
	balance = (int)ClampFloat((float)balance, 0.0f, 200.0f);
	density = (int)ClampFloat((float)density, 0.0f, 200.0f);
	spatial = (int)ClampFloat((float)spatial, 0.0f, 200.0f);

	// ============================================================
	// 追加エフェクトパラメータ (リバーブ/コーラス/ディレイ) の取り込み
	//   0 = オフ / 1-100 = モードA / 101-200 = モードB
	//   eq_reverb : 1-100 リバーブ / 101-200 パンリバーブ
	//   eq_chorus : 1-100 コーラス / 101-200 コーラスディストーション
	//   eq_delay  : 1-100 ディレイ / 101-200 マルチディレイ
	// ============================================================
	g_eqReverb = (int)ClampFloat((float)savedata.eq_reverb, 0.0f, 200.0f);
	g_eqChorus = (int)ClampFloat((float)savedata.eq_chorus, 0.0f, 200.0f);
	g_eqDelay = (int)ClampFloat((float)savedata.eq_delay, 0.0f, 200.0f);
	savedata.eq_reverb = g_eqReverb;
	savedata.eq_chorus = g_eqChorus;
	savedata.eq_delay = g_eqDelay;
	// TODO: g_eqReverb / g_eqChorus / g_eqDelay を用いた
	//       リバーブ・コーラス・ディレイの実処理をここに追加する。

	// ============================================================
	// [FIX-BYPASS] 完全バイパス判定
	// 条件:
	//   ・環境プリセット == 0 (無処理環境)
	//   ・masterVolume/clarity/balance/density/spatial がすべて100
	//   ・EQ帯域 eq[0-14] がすべて100 (フラット)
	//   ・追加エフェクト(リバーブ/コーラス/ディレイ)がすべて0 (オフ)
	// 上記すべて満たす場合は一切の処理をせず即返す。
	// リサンプリングが不要な場合(≥44100Hz)はそのまま、
	// リサンプリングが走っていた場合は tempBuffer を解放して返す。
	// ============================================================
	if (currentEnvPre == 0 &&
		masterVolume == 100 && clarity == 100 &&
		balance == 100 && density == 100 && spatial == 100 &&
		g_eqReverb == 0 && g_eqChorus == 0 && g_eqDelay == 0 &&
		savedata.pro_ms_width == 100 && !savedata.pro_ms_mono &&
		savedata.mpVocalCenter == 100)
	{
		bool allFlat = true;
		for (int i = 0; i < 15; i++) {
			if (savedata.eq[i] != 100) { allFlat = false; break; }
		}
		if (allFlat) {
			const float extBoostGain = GetExternalBoostGain();
			auto finishOutNoMeter = [&]() {
				void* outPtr = data;
				int outLen = originalLen;
				if (needsResampling) {
					ResampleDown(processData, processLen, data, originalLen, 44100, originalRate, wavchannel, wavsam_depth);
					free(tempBuffer);
				}
				else {
					outPtr = processData;
					outLen = processLen;
				}
				ProAudio_ApplyXfadeIn(outPtr, outLen, originalRate, wavsam_depth, wavchannel);
				ProAudio_PushTailPcm(outPtr, outLen, originalRate, wavsam_depth, wavchannel);
			};
			// 計測は RG/拡張ブースト前(EQ経路と同じ位置)
			ProAudio_FeedMetersFromInterleaved(
				needsResampling ? processData : data,
				needsResampling ? processLen : originalLen,
				needsResampling ? 44100 : originalRate,
				wavsam_depth, wavchannel);
			if (extBoostGain <= 1.0001f) {
				if (needsResampling && tempBuffer) {
					free(tempBuffer);
					ProAudio_ApplyXfadeIn(data, originalLen, originalRate, wavsam_depth, wavchannel);
					ProAudio_PushTailPcm(data, originalLen, originalRate, wavsam_depth, wavchannel);
					return;
				}
				ProAudio_ApplyXfadeIn(data, originalLen, originalRate, wavsam_depth, wavchannel);
				ProAudio_PushTailPcm(data, originalLen, originalRate, wavsam_depth, wavchannel);
				return;
			}
			{
				const int bytesPerSample = wavsam_depth / 8;
				const int numSamples = processLen / (bytesPerSample * wavchannel);
				ApplyExternalBoostOnlyToBuffer(
					(unsigned char*)processData, numSamples, wavchannel, wavsam_depth, bytesPerSample);
			}
			finishOutNoMeter();
			return;
		}
	}

	// ============================================================
	// [FIX-1] スケールファクター修正
	// 旧: coreScale最大2.17 / extraScale最大2.50 → 音割れ・籠もりの主因
	// 新: 正規化範囲 [0,1] で体感的エフェクト強度を維持
	// ============================================================
	float coreScale = 0.25f + (effectAmount / 100.0f) * 0.75f;  // [0.25, 1.00]
	float extraScale = effectAmount / 100.0f;                      // [0.00, 1.00]
	float reflectionScale = 0.50f + (effectAmount / 100.0f) * 0.50f;  // [0.50, 1.00]

	// EQプリセット切り替え検出
	if (currentEqPre != g_lastEqPreset[g_eqCur]) {
		if (currentEqPre >= 0 && currentEqPre < EQ_PRESET_COUNT && currentEqPre != 9)
			memcpy(savedata.eq, EQ_PRESETS[currentEqPre], sizeof(int) * 15);
		g_lastEqPreset[g_eqCur] = currentEqPre;
		forceUpdate = TRUE;
	}

	// EQ値変化検出
	BOOL eqChanged = forceUpdate;
	if (!eqChanged) {
		for (int i = 0; i < 15; i++)
			if (savedata.eq[i] != g_lastEqValues[g_eqCur][i]) { eqChanged = TRUE; break; }
	}

	// 拡張パラメータ変化検出
	BOOL extendedChanged = FALSE;
	if (masterVolume != g_lastExtendedParams[g_eqCur][0] || clarity != g_lastExtendedParams[g_eqCur][1] ||
		balance != g_lastExtendedParams[g_eqCur][2] || density != g_lastExtendedParams[g_eqCur][3] ||
		spatial != g_lastExtendedParams[g_eqCur][4]) {
		extendedChanged = TRUE;
		g_lastExtendedParams[g_eqCur][0] = masterVolume; g_lastExtendedParams[g_eqCur][1] = clarity;
		g_lastExtendedParams[g_eqCur][2] = balance;      g_lastExtendedParams[g_eqCur][3] = density;
		g_lastExtendedParams[g_eqCur][4] = spatial;
	}

	// EQ/拡張パラメータが変化したときのみフィルタ係数を再計算 (CPU節約)
	if (eqChanged || extendedChanged) {
		memcpy(g_lastEqValues, savedata.eq, sizeof(int) * 15);
		for (int ch = 0; ch < MAX_CH; ch++) {
			// 15バンドEQ: 帯域10以降はQ=1.0に変更 (高域帯域をやや広めに)
			for (int b = 0; b < EQ_BANDS; b++) {
				float qVal = (b >= 10) ? 1.0f : 1.414f;
				CalcPeakingEQ(&g_channels[g_eqCur][ch].eqFilters[b], EQ_FREQS[b], qVal, (float)savedata.eq[b], wavbitbackup);
			}
			// クラリティ: 5kHz 前後のプレゼンス調整 (感度2倍)
			float clarityDb = (clarity - 100.0f) * 0.36f;
			CalcPeakingEQ(&g_channels[g_eqCur][ch].clarityFilter, 5000.0f, 1.5f, 100.0f + clarityDb / 0.12f, wavbitbackup);

			// バランス: 低域/高域のシェルフで輪郭/温もりのバランスを調整 (感度2倍)
			float balanceDb = (balance - 100.0f) * 0.24f;
			CalcShelvingEQ(&g_channels[g_eqCur][ch].bassBalanceFilter, 0, 250.0f, -balanceDb, wavbitbackup);
			CalcShelvingEQ(&g_channels[g_eqCur][ch].trebleBalanceFilter, 1, 4000.0f, balanceDb, wavbitbackup);

			// デンシティ: 600Hz/1400Hz 近傍の中域密度を調整 (感度2倍)
			float densityDb = (density - 100.0f) * 0.30f;
			CalcPeakingEQ(&g_channels[g_eqCur][ch].densityFilter1, 600.0f, 1.2f, 100.0f + densityDb / 0.12f, wavbitbackup);
			CalcPeakingEQ(&g_channels[g_eqCur][ch].densityFilter2, 1400.0f, 1.2f, 100.0f + densityDb / 0.12f, wavbitbackup);
		}
	}

	// 環境プリセット/エフェクト量が変化したときのみフィルタ係数を再計算
	if (currentEnvPre != g_lastEnvPreset[g_eqCur] || effectAmount != g_lastEffectAmount[g_eqCur] || forceUpdate) {
		if (currentEnvPre < 0 || currentEnvPre >= ENV_PRESET_COUNT) currentEnvPre = 0;

		const EnvParams* ep = &ENV_PRESETS[currentEnvPre];
		for (int ch = 0; ch < MAX_CH; ch++) {
			CalcFilter(&g_channels[g_eqCur][ch].envLpf, 0, ep->lpfFreq, 0.707f, wavbitbackup);
			CalcFilter(&g_channels[g_eqCur][ch].envHpf, 1, ep->hpfFreq, 0.707f, wavbitbackup);
			CalcFilter(&g_channels[g_eqCur][ch].exciterFilter, 1, 6000.0f, 0.707f, wavbitbackup);

			// ダンピングフィルタ: damping値に応じてカットオフ周波数を変化
			float dampFreq = 4000.0f + (ep->damping * extraScale * 8000.0f);
			CalcFilter(&g_channels[g_eqCur][ch].dampingFilter, 0, dampFreq, 0.5f, wavbitbackup);

			// 帯域別リバーブ特性フィルタ
			CalcFilter(&g_channels[g_eqCur][ch].bassReverbFilter, 0, fminf(500.0f, 250.0f * ep->bassReverbTime), 0.707f, wavbitbackup);
			CalcPeakingEQ(&g_channels[g_eqCur][ch].midReverbFilter, fminf(3000.0f, 1500.0f * ep->midReverbTime), 1.0f, 100.0f, wavbitbackup);
			CalcFilter(&g_channels[g_eqCur][ch].trebleReverbFilter, 1, fminf(12000.0f, 6000.0f * ep->trebleReverbTime), 0.707f, wavbitbackup);

			// 材質・ウォームス関連フィルタ
			CalcFilter(&g_channels[g_eqCur][ch].materialFilter, 0, 2000.0f - (ep->materialAbsorption * 1500.0f), 0.707f, wavbitbackup);
			CalcShelvingEQ(&g_channels[g_eqCur][ch].warmthFilter, 0, 300.0f, (ep->warmth - 0.5f) * 6.0f, wavbitbackup);

			if (ep->flutterEcho > 0.0f)
				CalcFilter(&g_channels[g_eqCur][ch].flutterFilter, 1, 1200.0f, 2.0f, wavbitbackup);
			if (ep->resonanceFreq > 0.0f && ep->resonanceQ > 0.0f)
				CalcPeakingEQ(&g_channels[g_eqCur][ch].resonanceFilter, ep->resonanceFreq, ep->resonanceQ, 100.0f, wavbitbackup);
			if (ep->metallic > 0.0f)
				CalcFilter(&g_channels[g_eqCur][ch].metallicFilter, 1, 4500.0f, 3.5f, wavbitbackup);
			if (ep->glassiness > 0.0f)
				CalcFilter(&g_channels[g_eqCur][ch].glassFilter, 1, 8000.0f, 4.0f, wavbitbackup);

			// LFO設定
			g_channels[g_eqCur][ch].lfo.frequency = ep->modSpeed * extraScale;
			g_channels[g_eqCur][ch].lfo.depth = ep->modDepth * extraScale * 10.0f;
		}
		g_lastEnvPreset[g_eqCur] = currentEnvPre;
		g_lastEffectAmount[g_eqCur] = effectAmount;
	}

	const EnvParams* env = &ENV_PRESETS[g_lastEnvPreset[g_eqCur]];
	BOOL isYamabiko = (env->type == TYPE_MOUNTAIN_ECHO || env->type == TYPE_CANYON_ECHO);

	// プリディレイ・メインディレイ サンプル数計算
	int preDelaySamps = (int)(env->preDelayMs * coreScale * wavbitbackup / 1000.0f);
	int mainDelaySamps = (int)(env->delayTimeMs * env->roomSize * wavbitbackup / 1000.0f);

	int refSamps[8];
	for (int i = 0; i < 8; i++)
		refSamps[i] = (int)(env->earlyRef[i * 2] * env->roomSize * wavbitbackup / 1000.0f);

	int bytesPerSample = wavsam_depth / 8;
	int numSamples = processLen / (bytesPerSample * wavchannel);
	unsigned char* pRaw = (unsigned char*)processData;
	int stereoOffset = (wavbitbackup * 20) / 1000;  // L/R間の微小ずれ (約20ms)

	// ============================================================
	// [FIX-3][FIX-4] ブロック解析
	// ブロック単位でコンテンツ検出 + ゲインステージング
	// ============================================================
	// eq[15]: 100=1.0倍、偏差を2倍にしてスライダー差を明確化
	float masterGain = fmaxf(0.0f, 1.0f + (masterVolume - 100.0f) / 50.0f);

	// 0dB(100)を超えるブースト量に応じて内部ヘッドルームを自動確保する。
	// 体感音量はなるべく維持しつつ、EQ強ブースト時のハードクリップを抑える。
	float maxBandBoostDb = 0.0f;
	for (int b = 0; b < EQ_BANDS; b++) {
		float bandBoostDb = (savedata.eq[b] - 100.0f) * 0.12f;
		if (bandBoostDb > maxBandBoostDb) maxBandBoostDb = bandBoostDb;
	}
	float maxExtendedBoostDb = 0.0f;
	{
		const float clarityDb = (clarity - 100.0f) * 0.36f;
		const float balanceDb = fabsf((balance - 100.0f) * 0.24f);
		const float densityDb = fabsf((density - 100.0f) * 0.30f);
		maxExtendedBoostDb = fmaxf(clarityDb, fmaxf(balanceDb, densityDb));
	}
	const float totalBoostDb = maxBandBoostDb + maxExtendedBoostDb;
	const float headroomDb = ClampFloat(totalBoostDb - 6.0f, 0.0f, 18.0f);
	const float eqHeadroomGain = powf(10.0f, -headroomDb / 20.0f);
	const float eqMakeupGain = 1.0f + (1.0f - eqHeadroomGain) * 0.55f;

	BlockAnalysis ba = AnalyzeBlock(pRaw, numSamples, wavchannel, wavsam_depth, bytesPerSample, masterGain);

	// ============================================================
	// [FIX-COMP] ブロック間ゲイン平滑化
	//
	// attack=0.08 : 急激な大音量に対しては「ゆっくり」追従
	//               → 圧縮が一気にかかって音が籠もるのを防ぐ
	// release=0.30: ゲインが戻るときは「素早く」追従
	//               → 静音部への戻りが遅れず自然
	//
	// 旧: ブロック単位で瞬時にgain変化 → 切れ目が不連続で籠もり感
	// 新: 指数平滑でフレーム間をなめらかにつなぐ
	// ============================================================
	{
		const float kAttack = 0.08f;   // 1ブロック最大8%圧縮
		const float kRelease = 0.30f;   // 3〜4ブロックで復帰

		if (ba.stagingGain < g_stagingGainSmooth[g_eqCur])
			// 大音量側: 緩やかに追従 (attack)
			g_stagingGainSmooth[g_eqCur] += (ba.stagingGain - g_stagingGainSmooth[g_eqCur]) * kAttack;
		else
			// 静音側: 素早く復帰 (release)
			g_stagingGainSmooth[g_eqCur] += (ba.stagingGain - g_stagingGainSmooth[g_eqCur]) * kRelease;

		g_stagingGainSmooth[g_eqCur] = ClampFloat(g_stagingGainSmooth[g_eqCur], 0.10f, 1.0f);
	}

	// 実効マスターゲイン = ユーザー設定 × 平滑化済みstagingGain
	float effectiveMasterGain = masterGain * g_stagingGainSmooth[g_eqCur];

	// [FIX-4 改] チップチューン/FM音源検出時のスケーリング係数
	//
	// 【変更理由: 100音響プリセットが同じに聞こえる問題の修正】
	//   本アプリは SPC/NEZ/チップチューン等の低クレストファクター音源が主対象のため
	//   ba.isChiptune がほぼ常時 TRUE になる。旧値 wetScale=0.22 / diffusionScale=0.12
	//   では残響・拡散がほぼ消え、100 個の環境プリセットの差が聴き取れず
	//   「全部同じ音」になっていた。
	//   音割れ防止は最終段のルックアヘッドリミッターが担うため、ここで残響を
	//   潰す必要はない。環境キャラクターが分かる範囲まで緩和する。
	//   harmonicScale のみ 0.0 を維持 (元々歪みの少ない信号に倍音を足さない)。
	float wetScale = ba.isChiptune ? 0.70f : 1.0f;
	float harmonicScale = ba.isChiptune ? 0.00f : 1.0f;
	float diffusionScale = ba.isChiptune ? 0.55f : 1.0f;

	// 出力用サンプルバッファ (最大約40ブロック分)
	float* leftSamples = g_eqLeftSamples[g_eqCur];
	float* rightSamples = g_eqRightSamples[g_eqCur];
	int bufferIndex = 0;

	float harmonicAmount = (density - 100.0f) / 100.0f;
	// eq[19]: 100=ニュートラル(幅1.0)、偏差2倍 (0→0.05, 200→3.0)
	float spatialWidth = fmaxf(0.05f, 1.0f + (spatial - 100.0f) / 50.0f);

	// ===== 信号処理メインループ =====
	for (int i = 0; i < numSamples; i++) {
		for (int ch = 0; ch < wavchannel; ch++) {
			if (ch >= MAX_CH) continue;

			// PCM → float 変換
			float inSample = 0.0f;
			int offset = (i * wavchannel + ch) * bytesPerSample;
			if (wavsam_depth == 16)
				inSample = *((short*)(pRaw + offset)) / 32768.0f;
			else if (wavsam_depth == 24) {
				int val = pRaw[offset] | (pRaw[offset + 1] << 8) | ((signed char)pRaw[offset + 2] << 16);
				inSample = val / 8388608.0f;
			}
			else if (wavsam_depth == 32)
				inSample = *((int*)(pRaw + offset)) / 2147483648.0f;
			else
				inSample = (pRaw[offset] - 128) / 128.0f;

			float signal = inSample;
			ChannelState* cs = &g_channels[g_eqCur][ch];

			// [FIX-3] 平滑化済みゲインを適用
			signal *= (effectiveMasterGain * eqHeadroomGain);

			// 15バンドEQ + 拡張フィルタ群を順次通過
			for (int b = 0; b < EQ_BANDS; b++) signal = ProcessBiquad(&cs->eqFilters[b], signal);
			signal = ProcessBiquad(&cs->clarityFilter, signal);
			signal = ProcessBiquad(&cs->bassBalanceFilter, signal);
			signal = ProcessBiquad(&cs->trebleBalanceFilter, signal);
			signal = ProcessBiquad(&cs->densityFilter1, signal);
			signal = ProcessBiquad(&cs->densityFilter2, signal);

			// 3次高調波付加 (harmonicScale=0 のとき完全無効)
			// [FIX-4] チップチューン時は 0.00 で呼び出し自体をスキップ
			if (harmonicScale > 0.0f && fabs(harmonicAmount) > 0.01f) {
				float harmonic = signal * signal * signal * harmonicAmount * 0.30f * harmonicScale;
				cs->harmonicState = cs->harmonicState * 0.95f + harmonic * 0.05f;
				signal += cs->harmonicState;
			}

			float wetSignal = 0.0f;

			// ===== 環境エフェクト処理 =====
			if (env->type != TYPE_NONE && env->wetMix > 0.0f && effectAmount > 0) {

				if (isYamabiko) {
					// --- 山彦エコー処理 ---
					float echo = ProcessYamabikoAdvanced(cs, signal, env, wavbitbackup);
					// 早期反射音 (マルチタップの最初の反射)
					int earlyMs = (env->type == TYPE_MOUNTAIN_ECHO) ? 60 : 45;
					int earlySamp = (int)(earlyMs * wavbitbackup / 1000.0f);
					int rPos = cs->writePos - (earlySamp + preDelaySamps);
					while (rPos < 0) rPos += MAX_DELAY_SAMPLES;
					float earlyGain = (env->type == TYPE_MOUNTAIN_ECHO) ? 0.18f : 0.25f;
					float earlyRef = cs->delayBuffer[rPos] * earlyGain;
					// echoClarity が高いほど拡散を弱めてエコーの輪郭を保つ (従来は未使用)
					float clarityCut = 1.0f - env->echoClarity * 0.6f;
					// 弱いディフュージョン (山彦の輪郭を保ちつつ自然な拡散を付加)
					float weakDiff = env->diffusion * coreScale * 0.22f * diffusionScale * clarityCut;
					float weakDens = env->density * 0.28f * diffusionScale * clarityCut;
					float late = ProcessDiffusion(cs, echo, weakDiff, weakDens, env->type);
					// echoFeedbackTone で反射音の明暗を傾ける (-1=暗く, +1=明るく / 従来は未使用)
					if (fabsf(env->echoFeedbackTone) > 0.01f) {
						cs->echoToneState += 0.25f * (late - cs->echoToneState);
						float highPart = late - cs->echoToneState;
						late = cs->echoToneState + highPart * (1.0f + env->echoFeedbackTone * 0.8f);
					}
					// 後期残響エンベロープ
					float lateEnv = powf(0.94f, 1.0f / (env->lateReverbDecay * 1.3f));
					cs->lateEnvelope = cs->lateEnvelope * lateEnv + late * (1.0f - lateEnv);
					// ウェット信号合成: 直接エコー + 早期反射 + 後期残響
					wetSignal = echo * 0.88f + earlyRef * 0.35f + cs->lateEnvelope * 0.55f * 0.52f;
					// 残響キャラクター付与 (空気吸収/色味/滑らかさ/コム/風)
					wetSignal = ProcessWetCharacter(cs, wetSignal, env, wavbitbackup);
					wetSignal *= fminf(0.90f, env->wetMix * coreScale) * wetScale;
				}
				else {
					// --- 通常リバーブ処理 ---
					// LFO変調 + ステレオオフセット込みの読み出しポジション
					int chOffset = (ch % 2) * stereoOffset;
					int readMain = cs->writePos - (mainDelaySamps + preDelaySamps + chOffset
						+ (int)UpdateLFO(&cs->lfo, wavbitbackup));
					while (readMain < 0) readMain += MAX_DELAY_SAMPLES;
					float delayMain = cs->delayBuffer[readMain];

					// 帯域別減衰特性をブレンド (低域・中域・高域で異なる減衰時定数)
					delayMain = (ProcessBiquad(&cs->bassReverbFilter, delayMain) * env->bassReverbTime +
						ProcessBiquad(&cs->midReverbFilter, delayMain) * env->midReverbTime +
						ProcessBiquad(&cs->trebleReverbFilter, delayMain) * env->trebleReverbTime) / 3.0f;

					// ダンピング・LPF/HPF による空間特性付与
					delayMain = ProcessBiquad(&cs->dampingFilter, delayMain);
					delayMain = ProcessBiquad(&cs->envLpf, delayMain);
					delayMain = ProcessBiquad(&cs->envHpf, delayMain);

					// ディフュージョン (チップチューン時は削減)
					// bassDiffusion/trebleDiffusion を密度に反映し、プリセット毎に
					// 拡散プロファイルが変わるようにする (従来は未使用パラメータ)
					float diffProfile = 0.5f + (env->bassDiffusion + env->trebleDiffusion) * 0.5f;
					delayMain = ProcessDiffusion(cs, delayMain,
						env->diffusion * coreScale * diffusionScale,
						env->density * diffProfile * diffusionScale, env->type);

					// 早期反射: 8タップ、時間経過とともに指数減衰
					// reflectionDensity で反射の密度感を可変 (従来は未使用パラメータ)
					float reflDens = 0.6f + env->reflectionDensity * 0.8f;
					float earlyRef = 0.0f;
					for (int r = 0; r < 8; r++) {
						int rPos = cs->writePos - (refSamps[r] + preDelaySamps + chOffset);
						while (rPos < 0) rPos += MAX_DELAY_SAMPLES;
						float envelope = powf(1.0f - (float)(r + 1) / 8.0f, 2.0f / env->earlyReverbDecay);
						earlyRef += cs->delayBuffer[rPos] * env->earlyRef[r * 2 + 1]
							* reflectionScale * 1.4f * envelope;
					}
					earlyRef *= reflDens;

					// 後期残響エンベロープ (exponential decay)
					float lateEnv = powf(0.95f, 1.0f / env->lateReverbDecay);
					cs->lateEnvelope = cs->lateEnvelope * lateEnv + delayMain * (1.0f - lateEnv);

					// 早期反射 + 後期残響 合成
					wetSignal = (earlyRef * env->earlyLateBalance)
						+ (cs->lateEnvelope * (1.0f - env->earlyLateBalance * 0.5f));
					// 残響キャラクター付与 (空気吸収/色味/滑らかさ/コム/風)
					wetSignal = ProcessWetCharacter(cs, wetSignal, env, wavbitbackup);
					wetSignal *= wetScale;  // [FIX-4]

					// フィードバック: ウォームス + 材質吸収を適用した後にバッファ書き込み
					float fbSig = ProcessWarmth(cs,
						ProcessMaterialAbsorption(cs, delayMain, env->materialAbsorption, env->surfaceRoughness),
						env->warmth);
					float effectiveFB = fminf(0.88f, env->feedback * coreScale);
					float fbVal = signal + (fbSig * effectiveFB);
					// フィードバック発散防止クランプ
					if (fbVal > 1.5f) fbVal = 1.5f;
					if (fbVal < -1.5f) fbVal = -1.5f;
					cs->delayBuffer[cs->writePos] = isfinite(fbVal) ? fbVal : 0.0f;
					cs->writePos = (cs->writePos + 1) % MAX_DELAY_SAMPLES;
				}
			}

			// [FIX-1] ウェットミックス量を [0, 0.90] で厳格にクランプ
			float effectiveWetMix = fminf(0.90f, env->wetMix * coreScale);
			// [FIX-CLIP] ドライ/ウェットのクロスフェード(ドライダッキング):
			//   ウェット(=残響=環境キャラクター)が増えるほどドライを下げる。
			//   ・dry+wet の合算ピーク肥大を抑え、最終段ルックアヘッドリミッターが
			//     全環境を同じレベルまで潰す(=どの環境も同じ音に聞こえる/音割れ感)
			//     のを防ぐ。
			//   ・残響の相対レベルが上がるため、100環境それぞれの差が一目瞭然になる。
			//   低wet環境(スタジオ/防音室など wet≒0)はほぼ無影響(dryGain≒1.0)。
			float dryGain = 1.0f - 0.45f * effectiveWetMix;
			float mixed = signal * dryGain + wetSignal * effectiveWetMix;

			// エフェクト追加処理 (各パラメータが有効な場合のみ)
			if (env->exciterAmount > 0.0f && effectAmount > 0)
				mixed = Exciter(mixed, &cs->exciterFilter, env->exciterAmount * extraScale);
			if (env->flutterEcho > 0.0f)
				mixed = ProcessFlutterEcho(cs, mixed, env->flutterEcho * extraScale, wavbitbackup);
			if (env->resonanceFreq > 0.0f && env->resonanceQ > 0.0f)
				mixed = ProcessResonance(cs, mixed, env->resonanceFreq, env->resonanceQ, env->spaceComplexity * 0.3f);
			if (env->metallic > 0.0f)    mixed = ProcessMetallic(cs, mixed, env->metallic * extraScale);
			if (env->glassiness > 0.0f)  mixed = ProcessGlass(cs, mixed, env->glassiness * extraScale);
			if (env->shimmer > 0.0f)     mixed = ProcessShimmer(cs, mixed, env->shimmer * extraScale, wavbitbackup);
			if (env->doppler > 0.0f)     mixed = ProcessDoppler(cs, mixed, env->doppler * extraScale, wavbitbackup);

			mixed = ProcessBrightness(mixed, &cs->brightnessState, env->brightness);

			// 質感キャラクター付与 (weight/woodiness/concrete/softness/phasing/distortion)
			mixed = ProcessMixCharacter(cs, mixed, env, wavbitbackup);

			// L/R バッファへ格納 (モノラル時は両方に同値)
			if (wavchannel == 2) {
				if (ch == 0) leftSamples[bufferIndex] = mixed;
				else         rightSamples[bufferIndex] = mixed;
			}
			else {
				leftSamples[bufferIndex] = mixed;
				rightSamples[bufferIndex] = mixed;
			}
		}

		// ===== ステレオ幅処理 =====
		// Mid/Side 変換で stereoWidth を変更し元の L/R に戻す
		// wallDistance/openness/ceilingHeight で環境空間の広がりを調節
		if (wavchannel == 2) {
			float w = (1.0f + (env->stereoWidth - 1.0f) * extraScale)
				* spatialWidth * env->wallDistance * (0.7f + env->openness * 0.6f);
			w *= (env->ceilingHeight > 1.0f)
				? (1.0f + (env->ceilingHeight - 1.0f) * 0.2f)
				: env->ceilingHeight;
			// enclosure (密閉度) が高いほどステレオ像を狭め包まれ感を出す (従来は未使用)
			w *= (1.0f - env->enclosure * 0.35f);
			float mid = (leftSamples[bufferIndex] + rightSamples[bufferIndex]) * 0.5f;
			float side = (leftSamples[bufferIndex] - rightSamples[bufferIndex]) * 0.5f * w;
			leftSamples[bufferIndex] = mid + side;
			rightSamples[bufferIndex] = mid - side;
		}

		bufferIndex++;
	}

	// ===================================================
	// 【追加エフェクト】リバーブ / コーラス / ディレイ
	// 環境処理後・メイクアップ/リミッター前に適用 (ピークは最終段で制御)
	// ===================================================
	FxApplyUserEffects(leftSamples, rightSamples, bufferIndex, wavbitbackup);

	// ProAudio: Mid/Side 幅 / モノ互換（環境ステレオ幅の後段で明示制御）
	if (wavchannel >= 2 && bufferIndex > 0) {
		ProAudio_ApplyMS(leftSamples, rightSamples, bufferIndex,
			savedata.pro_ms_width, savedata.pro_ms_mono);
	}
	if (wavchannel >= 2 && bufferIndex > 0 && savedata.mpVocalCenter != 100) {
		const float midGain = savedata.mpVocalCenter / 100.f;
		for (int i = 0; i < bufferIndex; ++i) {
			const float mid = (leftSamples[i] + rightSamples[i]) * 0.5f;
			const float side = (leftSamples[i] - rightSamples[i]) * 0.5f;
			const float midS = mid * midGain;
			leftSamples[i] = midS + side;
			rightSamples[i] = midS - side;
		}
	}

	// DJパッド: 3バンドEQ + Kill + フィルタ（ボーカル後・メイクアップ前）
	if (bufferIndex > 0) {
		static Biquad s_djLow[2], s_djMid[2], s_djHigh[2], s_djFilt[2];
		static int s_djLowV = -999, s_djMidV = -999, s_djHighV = -999, s_djFiltV = -999, s_djKillV = -999, s_djRate = -1;
		int lowV = savedata.mpDjEqLow;
		int midV = savedata.mpDjEqMid;
		int highV = savedata.mpDjEqHigh;
		int filtV = savedata.mpDjFilter;
		int killV = savedata.mpDjEqKill & 7;
		if (lowV < 0) lowV = 0; if (lowV > 200) lowV = 200;
		if (midV < 0) midV = 0; if (midV > 200) midV = 200;
		if (highV < 0) highV = 0; if (highV > 200) highV = 200;
		if (filtV < 0) filtV = 0; if (filtV > 200) filtV = 200;
		const int rate = wavbitbackup > 0 ? wavbitbackup : 44100;
		if (lowV != s_djLowV || midV != s_djMidV || highV != s_djHighV || filtV != s_djFiltV || killV != s_djKillV || rate != s_djRate) {
			s_djLowV = lowV; s_djMidV = midV; s_djHighV = highV; s_djFiltV = filtV; s_djKillV = killV; s_djRate = rate;
			for (int ch = 0; ch < 2; ++ch) {
				if (killV & 1)
					CalcShelvingEQ(&s_djLow[ch], 0, 250.0f, -48.0f, rate);
				else
					CalcPeakingEQ(&s_djLow[ch], 100.0f, 0.707f, (float)lowV, rate);
				if (killV & 2) {
					CalcPeakingEQ(&s_djMid[ch], 1000.0f, 0.9f, 0.0f, rate);
				} else
					CalcPeakingEQ(&s_djMid[ch], 1000.0f, 0.707f, (float)midV, rate);
				if (killV & 4)
					CalcShelvingEQ(&s_djHigh[ch], 1, 4000.0f, -48.0f, rate);
				else
					CalcPeakingEQ(&s_djHigh[ch], 8000.0f, 0.707f, (float)highV, rate);
				if (filtV < 98) {
					const float t = (float)filtV / 100.0f;
					float freq = 200.0f * powf(100.0f, t);
					if (freq < 80.0f) freq = 80.0f;
					if (freq > 18000.0f) freq = 18000.0f;
					CalcFilter(&s_djFilt[ch], 0, freq, 0.707f, rate);
				} else if (filtV > 102) {
					const float t = (float)(filtV - 100) / 100.0f;
					float freq = 30.0f * powf(300.0f, t);
					if (freq < 30.0f) freq = 30.0f;
					if (freq > 10000.0f) freq = 10000.0f;
					CalcFilter(&s_djFilt[ch], 1, freq, 0.707f, rate);
				} else {
					s_djFilt[ch].b0 = 1.0f; s_djFilt[ch].b1 = 0.0f; s_djFilt[ch].b2 = 0.0f;
					s_djFilt[ch].a1 = 0.0f; s_djFilt[ch].a2 = 0.0f;
				}
			}
		}
		const BOOL djActive = (lowV != 100 || midV != 100 || highV != 100 || filtV != 100 || killV != 0);
		if (djActive) {
			const int chN = (wavchannel >= 2) ? 2 : 1;
			for (int i = 0; i < bufferIndex; ++i) {
				float l = leftSamples[i];
				l = ProcessBiquad(&s_djLow[0], l);
				l = ProcessBiquad(&s_djMid[0], l);
				if (killV & 2) l = ProcessBiquad(&s_djMid[0], l);
				l = ProcessBiquad(&s_djHigh[0], l);
				l = ProcessBiquad(&s_djFilt[0], l);
				leftSamples[i] = l;
				if (chN > 1) {
					float r = rightSamples[i];
					r = ProcessBiquad(&s_djLow[1], r);
					r = ProcessBiquad(&s_djMid[1], r);
					if (killV & 2) r = ProcessBiquad(&s_djMid[1], r);
					r = ProcessBiquad(&s_djHigh[1], r);
					r = ProcessBiquad(&s_djFilt[1], r);
					rightSamples[i] = r;
				} else {
					rightSamples[i] = l;
				}
			}
		}
	}

	// ===================================================
	// 【最終段前】メイクアップゲインの適用
	// ===================================================
	for (int i = 0; i < bufferIndex; i++) {
		leftSamples[i] *= eqMakeupGain;
		rightSamples[i] *= eqMakeupGain;
	}

	// ラウドネス計測 / 相関（リミッター前の実聴感に近い信号）
	if (bufferIndex > 0) {
		ProAudio_LoudnessFeed(leftSamples, rightSamples, bufferIndex, wavbitbackup);
		if (savedata.pro_corr_meter)
			ProAudio_CorrFeed(leftSamples, rightSamples, bufferIndex);
	}

	// ===================================================
	// 【最終段】拡張音量 + フォーマット別ブースト → ルックアヘッド・ピークリミッター
	// マスター音量(eq[15])とは独立。ピークのみ動的に抑え、静部は減衰しない。
	// 以前は tanh サチュレーション(knee0.78)で 0.78〜0.97 域に高調波歪みが常時乗り、
	// それが「音割れ感/濁り」の主因だった。先読みリミッターでオーバーシュートを出さず
	// 上限へ収めるため、サチュレーションを使わずクリア＆音量維持を両立する。
	// (最終の整数化で ±1.0 ハードクランプが究極の安全装置として残る)
	{
		const float extBoostGain = GetExternalBoostGain();
		if (extBoostGain != 1.0f) {
			for (int i = 0; i < bufferIndex; i++) {
				leftSamples[i] *= extBoostGain;
				rightSamples[i] *= extBoostGain;
			}
		}
		ApplyLookaheadLimiterStereo(leftSamples, rightSamples, bufferIndex, wavbitbackup, 0.97f);
	}

	// ===== 最終出力: float → 整数PCM 書き戻し =====
	{
		int bi = 0;
		for (int i = 0; i < numSamples; i++) {
			for (int ch = 0; ch < wavchannel; ch++) {
				if (ch >= MAX_CH) continue;

				float finalOut = (ch == 0) ? leftSamples[bi] : rightSamples[bi];

				// ハードクリップ安全装置: ProfessionalSoftSaturate 正常動作時は到達しない
				if (finalOut > 1.0f)  finalOut = 1.0f;
				if (finalOut < -1.0f) finalOut = -1.0f;

				int offset = (i * wavchannel + ch) * bytesPerSample;
				if (wavsam_depth == 16) {
					int32_t v = (int32_t)roundf(finalOut * 32768.0f);
					if (v > 32767) v = 32767; if (v < -32768) v = -32768;
					*((short*)(pRaw + offset)) = (short)v;
				}
				else if (wavsam_depth == 24) {
					int32_t v = (int32_t)roundf(finalOut * 8388608.0f);
					if (v > 8388607) v = 8388607; if (v < -8388608) v = -8388608;
					pRaw[offset] = v & 0xFF;
					pRaw[offset + 1] = (v >> 8) & 0xFF;
					pRaw[offset + 2] = (v >> 16) & 0xFF;
				}
				else if (wavsam_depth == 32)
					*((int*)(pRaw + offset)) = (int)(finalOut * 2147483647.0f);
				else
					pRaw[offset] = (unsigned char)(finalOut * 127.0f + 128.0f);
			}
			// left/rightSamples はフレーム単位。mono/多ch でも毎フレーム進める
			// （旧: wavchannel==2 のときだけ → mono は全サンプルが leftSamples[0] になりノイズ化）
			bi++;
		}
	}

	// リサンプリングしていた場合は元のサンプルレートに戻して tempBuffer を解放
	if (needsResampling) {
		ResampleDown(processData, processLen, data, originalLen, 44100, originalRate, wavchannel, wavsam_depth);
		free(tempBuffer);
	}

	// クロスフェードイン(曲頭) + テール蓄積(曲末用)。リサンプリング後の実出力に適用。
	{
		void* outPtr = data;
		int outLen = originalLen;
		if (!needsResampling) {
			outPtr = processData;
			outLen = processLen;
		}
		ProAudio_ApplyXfadeIn(outPtr, outLen, originalRate, wavsam_depth, wavchannel);
		ProAudio_PushTailPcm(outPtr, outLen, originalRate, wavsam_depth, wavchannel);
	}
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
static const int EQKEY_CHORD_CAND_MAX = 32;

static float  g_noteStrength[108];        // MIDI 0-107 の強度
static float  g_noteStrengthPrev[108];    // 指数平滑用
static double g_goertzelCoeffs[108];      // Goertzel 係数 (各ノート周波数)
static double g_hannWindow4096[4096];   // 低域 Goertzel 用 Hann 窓
static double g_blackmanWindow2048[2048]; // 高域 Goertzel 用 Blackman 窓
static bool   g_analysisInitialized = false;
static double g_analysisSampleRate = 0.0; // 係数計算に使ったサンプルレート

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
	double workL[EQKEY_PCM_MAX];
	double workR[EQKEY_PCM_MAX];
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
		if (workN > 0) {
			memcpy(workL, g_eqKeyL, (size_t)workN * sizeof(double));
			memcpy(workR, g_eqKeyR, (size_t)workN * sizeof(double));
		}
		workRate = g_eqKeyRate;
		LeaveCriticalSection(&g_eqKeyCs);
		if (workN <= 0)
			continue;
		AnalyzeMusicKey(workL, workR, workN, workRate);
	}
	return 0;
}

static void EnsureEqKeyWorker()
{
	EnsureEqKeyCs();
	if (g_eqKeyThread) return;
	InterlockedExchange(&g_eqKeyStop, 0);
	if (!g_eqKeyWake) {
		g_eqKeyWake = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (!g_eqKeyWake) return;
	}
	g_eqKeyThread = CreateThread(nullptr, 0, EqKeyWorkerEntry, nullptr, 0, nullptr);
	if (!g_eqKeyThread) {
		CloseHandle(g_eqKeyWake);
		g_eqKeyWake = nullptr;
	}
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
	for (int k = 0; k < 108; ++k) {
		double freq = 440.0 * pow(2.0, ((12 + k) - 69.0) / 12.0);
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
	double s_prev = 0.0, s_prev2 = 0.0;
	for (int n = 0; n < numSamples; ++n) {
		const double x = window ? (samples[n] * window[n]) : samples[n];
		double s = x + coefficient * s_prev - s_prev2;
		s_prev2 = s_prev; s_prev = s;
	}
	double power = s_prev2 * s_prev2 + s_prev * s_prev - coefficient * s_prev * s_prev2;
	return sqrt(power > 0.0 ? power : 0.0) * 2.5 / numSamples;
}

// ノート強度を低域/中域/高域/全体のピッチクラスに集約
// オクターブ内での最大ノートを基準に協和音程の弱化を行い
// 支配的なルート音の誤検出を抑制する
static void AggregateNoteClasses(float* bassClass, float* midClass, float* highClass, float* allClass) {
	for (int i = 0; i < 12; i++) bassClass[i] = midClass[i] = highClass[i] = allClass[i] = 0.0f;
	float octaveMax[9] = { 0 }; int octaveMaxNote[9] = { -1 };
	// 各オクターブの最大強度ノートを特定
	for (int note = 0; note < 108; note++) {
		int oct = note / 12;
		if (g_noteStrength[note] > octaveMax[oct]) {
			octaveMax[oct] = g_noteStrength[note]; octaveMaxNote[oct] = note;
		}
	}
	// 協和音程バイアス補正 & 帯域別集計
	for (int note = 0; note < 108; note++) {
		float strength = g_noteStrength[note];
		int pc = note % 12, oct = note / 12;
		if (octaveMaxNote[oct] >= 0 && note != octaveMaxNote[oct]) {
			int iv = (pc - octaveMaxNote[oct] % 12 + 12) % 12;
			float r = strength / octaveMax[oct];
			// 完全5度・長3度等の弱い協和音程をさらに弱める
			if (iv == 7 && r < 0.4f)  strength *= 0.4f;
			else if (iv == 4 && r < 0.3f)  strength *= 0.6f;
			else if (iv == 2 && r < 0.35f) strength *= 0.3f;
			else if (iv == 9 && r < 0.3f)  strength *= 0.5f;
			else if (iv == 11 && r < 0.25f) strength *= 0.4f;
		}
		if (note < 36)       bassClass[pc] += strength;
		else if (note < 60)  midClass[pc] += strength;
		else                 highClass[pc] += strength;
		allClass[pc] += strength;
	}
}

// コードパターン定義
// pattern[12]: 各音程ウェイト (3=ルート, 2=5度, 1=3度, 0=不使用)
// bonus: パターンマッチ時の追加スコア (複雑なコードは負値)
typedef struct { const WCHAR* name; int pattern[12]; float bonus; } ChordPattern;
static const ChordPattern CHORD_PATTERNS[] = {
	{L"",                                    {3,0,0,0,2,0,0,1,0,0,0,0}, 0.5f},   // メジャー
	{L"!@C0066bbm!@C000000",                 {3,0,0,2,0,0,0,1,0,0,0,0}, 0.5f},   // マイナー
	{L"!@Cff55005!@C000000",                 {3,0,0,0,0,0,0,2,0,0,0,0}, 0.4f},   // 5度 (Power)
	{L"!@C8844ccsus!@Cff55004!@C000000",     {3,0,0,0,0,3,0,1,0,0,0,0}, 0.4f},   // sus4
	{L"!@C8844ccsus!@Cff55002!@C000000",     {3,0,3,0,0,0,0,1,0,0,0,0}, 0.4f},   // sus2
	{L"!@Caa7744dim!@C000000",               {3,0,0,2,0,0,2,0,0,0,0,0}, 0.3f},   // ディミニッシュ
	{L"!@Ccc4400aug!@C000000",               {3,0,0,0,2,0,0,0,2,0,0,0}, 0.3f},   // オーギュメント
	{L"!@Cff55007!@C000000",                 {3,0,0,0,2,0,0,1,0,0,2,0}, 0.3f},   // 7th
	{L"!@C00aa77M!@Cff55007!@C000000",       {3,0,0,0,2,0,0,1,0,0,0,2}, 0.3f},   // メジャー7th
	{L"!@C0066bbm!@Cff55007!@C000000",       {3,0,0,2,0,0,0,1,0,0,2,0}, 0.3f},   // マイナー7th
	{L"!@Cff55006!@C000000",                 {3,0,0,0,2,0,0,1,0,2,0,0}, 0.2f},   // 6th
	{L"!@C0066bbm!@Cff55006!@C000000",       {3,0,0,2,0,0,0,1,0,2,0,0}, 0.2f},   // マイナー6th
	{L"!@Cbb7733add!@Cff55009!@C000000",     {3,0,2,0,2,0,0,1,0,0,0,0}, 0.2f},   // add9
	{L"!@Cff55007!@C8844ccsus!@Cff55004!@C000000", {3,0,0,0,0,2,0,1,0,0,2,0}, 0.2f},  // 7sus4
	{L"!@C0066bbm!@Cff55007!@Cdd2222b!@Cff55005!@C000000", {3,0,0,2,0,0,2,0,0,0,2,0}, 0.2f}, // m7b5
	{L"!@Caa7744dim!@Cff55007!@C000000",     {3,0,0,2,0,0,2,0,0,2,0,0}, 0.2f},   // ディミニッシュ7th
	{L"!@Cff55009!@C000000",                 {3,0,2,0,2,0,0,1,0,0,2,0}, -0.5f},  // 9th (複雑)
	{L"!@C00aa77M!@Cff55009!@C000000",       {3,0,2,0,2,0,0,1,0,0,0,2}, -0.5f},  // メジャー9th
	{L"!@C0066bbm!@Cff55009!@C000000",       {3,0,2,2,0,0,0,1,0,0,2,0}, -0.5f}   // マイナー9th
};

static const int EQKEY_NAME_MAX = 160;

struct ChordCandidate { WCHAR name[EQKEY_NAME_MAX]; float score; int complexity; };

static void EqKeyCopy(WCHAR* dst, int cap, const WCHAR* src)
{
	if (!dst || cap <= 0) return;
	wcsncpy_s(dst, cap, src ? src : L"", _TRUNCATE);
}

static void EqKeyCat2(WCHAR* dst, int cap, const WCHAR* a, const WCHAR* b)
{
	EqKeyCopy(dst, cap, a);
	wcsncat_s(dst, cap, b ? b : L"", _TRUNCATE);
}

static void EqKeyTrimInPlace(WCHAR* s)
{
	if (!s || !*s) return;
	WCHAR* p = s;
	while (*p == L' ') ++p;
	if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(WCHAR));
	size_t n = wcslen(s);
	while (n > 0 && s[n - 1] == L' ') s[--n] = 0;
}

static void EqKeyRootName(WCHAR* dst, int cap, int rootIdx)
{
	EqKeyCopy(dst, cap, NOTE_NAMES[rootIdx]);
	EqKeyTrimInPlace(dst);
}

// コード推定 (ヒストリーなし版)
// 各コードパターンとピッチクラス強度のマッチングスコアで最適コードを選ぶ
static void EstimateChordRawInto(WCHAR* out, int outCap, float* noteClass, float threshold) {
	out[0] = 0;
	float maxVal = 0.0f;
	for (int i = 0; i < 12; i++) if (noteClass[i] > maxVal) maxVal = noteClass[i];
	if (maxVal < 0.001f) return;
	float n[12];
	for (int i = 0; i < 12; i++) { n[i] = noteClass[i] / maxVal; if (n[i] < 0.08f) n[i] = 0.0f; }
	int bestRoot = 0;
	for (int i = 1; i < 12; i++) if (n[i] > n[bestRoot]) bestRoot = i;
	if (n[bestRoot] < threshold) return;

	int active = 0;
	for (int i = 0; i < 12; i++) if (n[i] > 0.08f) active++;
	WCHAR root[8];
	EqKeyRootName(root, _countof(root), bestRoot);
	if (active <= 1) { EqKeyCopy(out, outCap, root); return; }

	float third = max(n[(bestRoot + 3) % 12], n[(bestRoot + 4) % 12]);
	float fifth = n[(bestRoot + 7) % 12];
	// パワーコード判定 (5度音のみ、3度なし)
	if (fifth > 0.3f && third < 0.15f && active <= 3) {
		EqKeyCat2(out, outCap, root, L"!@B[!@Cff0000Power!@Cffffff]!@B");
		return;
	}

	ChordCandidate cands[EQKEY_CHORD_CAND_MAX];
	int candCount = 0;
	int np = sizeof(CHORD_PATTERNS) / sizeof(ChordPattern);
	for (int c = 0; c < np; c++) {
		float sc = 0.0f; int matched = 0, req = 0;
		for (int x = 0; x < 12; x++) if (CHORD_PATTERNS[c].pattern[x] > 0) req++;
		bool is9 = (req >= 5);
		for (int nn = 0; nn < 12; nn++) {
			int note = (bestRoot + nn) % 12, w = CHORD_PATTERNS[c].pattern[nn];
			if (w > 0) { sc += n[note] * w * 2.0f; if (n[note] > 0.12f) matched++; }
			else if (n[note] > 0.25f) sc -= n[note] * 1.5f;
		}
		if (is9) {
			float mr = (req > 0) ? (float)matched / req : 0.0f;
			if (mr < 0.8f) sc -= 10.0f;
			if (n[(bestRoot + 2) % 12] < 0.2f) sc -= 5.0f;
		}
		else {
			if ((req > 0) && (float)matched / req < 0.4f) sc -= 3.0f;
			if (req == 4) {
				int tensionNote = -1;
				for (int x = 11; x >= 1; x--) {
					if (x != 3 && x != 4 && x != 7 && x != 5) {
						if (CHORD_PATTERNS[c].pattern[x] > 0) {
							tensionNote = x;
							break;
						}
					}
				}
				if (tensionNote != -1) {
					float tVal = n[(bestRoot + tensionNote) % 12];
					if (tVal < 0.15f) sc -= 4.0f;
					else if (tVal < 0.22f) sc -= 1.5f;
				}
			}
		}
		sc -= (active - matched) * 1.0f;
		sc += CHORD_PATTERNS[c].bonus;
		if (req == 3) sc += 1.2f; if (req == 4) sc += 0.5f; if (req >= 5) sc -= 1.0f;
		if (sc > (is9 ? 3.5f : 0.8f) && candCount < EQKEY_CHORD_CAND_MAX) {
			EqKeyCat2(cands[candCount].name, EQKEY_NAME_MAX, root, CHORD_PATTERNS[c].name);
			cands[candCount].score = sc; cands[candCount].complexity = req; candCount++;
		}
	}
	if (candCount <= 0) { EqKeyCopy(out, outCap, root); return; }
	for (int i = 0; i < candCount; ++i) {
		int best = i;
		for (int j = i + 1; j < candCount; ++j) {
			const bool jBetter = (abs(cands[j].score - cands[best].score) < 0.3f)
				? (cands[j].complexity < cands[best].complexity)
				: (cands[j].score > cands[best].score);
			if (jBetter) best = j;
		}
		if (best != i) { ChordCandidate tmp = cands[i]; cands[i] = cands[best]; cands[best] = tmp; }
	}
	// 上位3候補を ", " で連結して返す
	EqKeyCopy(out, outCap, cands[0].name); int count = 1;
	for (int i = 1; i < candCount && count < 3; i++) {
		if (cands[0].score - cands[i].score > 2.0f) break;
		if (cands[i].score < cands[0].score * 0.55f) continue;
		if (wcscmp(cands[i].name, out) == 0) continue;
		if (wcsstr(cands[i].name, L"9") && cands[0].score - cands[i].score > 1.2f) continue;
		wcsncat_s(out, outCap, L", ", _TRUNCATE);
		wcsncat_s(out, outCap, cands[i].name, _TRUNCATE);
		count++;
	}
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
const float SMOOTHING_FACTOR = 0.3f;  // ノート強度の指数平滑係数
static float g_prevRMS = 0.0f, g_peakRMS = 0.0f;
static bool  g_isPlaying = false;
static int   g_silenceFrameCount = 0;

// 無音判定閾値
const float SILENCE_THRESHOLD_ABS = 0.002f;    // 絶対値閾値
const float SILENCE_THRESHOLD_REL = 0.15f;     // ピークRMSに対する相対閾値
const float PLAYING_THRESHOLD = 0.01f;     // 再生中判定閾値
const int   SILENCE_FRAMES_FOR_CLEAR = 10;     // このフレーム数無音でヒストリーをクリア
static int  g_soundFrameCount = 0;

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
	out[0] = 0;
	float maxVal = 0.0f;
	for (int i = 0; i < 12; i++) if (nc[i] > maxVal) maxVal = nc[i];
	if (maxVal < 0.001f) return;
	float n[12];
	for (int i = 0; i < 12; i++) { n[i] = nc[i] / maxVal; if (n[i] < 0.08f) n[i] = 0.0f; }
	int bestRoot = 0;
	for (int i = 1; i < 12; i++) if (n[i] > n[bestRoot]) bestRoot = i;
	if (n[bestRoot] < threshold) return;

	int active = 0;
	for (int i = 0; i < 12; i++) if (n[i] > 0.08f) active++;
	WCHAR root[8];
	EqKeyRootName(root, _countof(root), bestRoot);
	if (active <= 1) { EqKeyCopy(out, outCap, root); return; }

	float third = max(n[(bestRoot + 3) % 12], n[(bestRoot + 4) % 12]);
	float fifth = n[(bestRoot + 7) % 12];
	if (fifth > 0.3f && third < 0.15f && active <= 3) {
		EqKeyCat2(out, outCap, root, L"!@B!@I[Power]!@B!@I");
		return;
	}

	ChordCandidate cands[EQKEY_CHORD_CAND_MAX];
	int candCount = 0;
	int np = sizeof(CHORD_PATTERNS) / sizeof(ChordPattern);
	for (int c = 0; c < np; c++) {
		float sc = 0.0f; int matched = 0, req = 0;
		for (int x = 0; x < 12; x++) if (CHORD_PATTERNS[c].pattern[x] > 0) req++;
		bool is9 = (req >= 5);
		for (int nn = 0; nn < 12; nn++) {
			int note = (bestRoot + nn) % 12, w = CHORD_PATTERNS[c].pattern[nn];
			if (w > 0) { sc += n[note] * w * 2.0f; if (n[note] > 0.12f) matched++; }
			else if (n[note] > 0.25f) sc -= n[note] * 1.5f;
		}
		if (is9) {
			float mr = (req > 0) ? (float)matched / req : 0.0f;
			if (mr < 0.8f) sc -= 10.0f;
			if (n[(bestRoot + 2) % 12] < 0.2f) sc -= 5.0f;
		}
		else {
			if ((req > 0) && (float)matched / req < 0.4f) sc -= 3.0f;
			if (req == 4) {
				int tensionNote = -1;
				for (int x = 11; x >= 1; x--) {
					if (x != 3 && x != 4 && x != 7 && x != 5) {
						if (CHORD_PATTERNS[c].pattern[x] > 0) {
							tensionNote = x;
							break;
						}
					}
				}
				if (tensionNote != -1) {
					float tVal = n[(bestRoot + tensionNote) % 12];
					if (tVal < 0.15f) sc -= 4.0f;
					else if (tVal < 0.22f) sc -= 1.5f;
				}
			}
		}
		sc -= (active - matched) * 1.0f;
		sc += CHORD_PATTERNS[c].bonus;
		if (req == 3) sc += 1.2f; if (req == 4) sc += 0.5f; if (req >= 5) sc -= 1.0f;
		WCHAR cur[EQKEY_NAME_MAX];
		EqKeyCat2(cur, EQKEY_NAME_MAX, root, CHORD_PATTERNS[c].name);
		if (prev && prev[0] && IsChordInListW(cur, prev)) sc += 1.5f;
		if (sc > (is9 ? 3.5f : 0.8f) && candCount < EQKEY_CHORD_CAND_MAX) {
			EqKeyCopy(cands[candCount].name, EQKEY_NAME_MAX, cur);
			cands[candCount].score = sc; cands[candCount].complexity = req; candCount++;
		}
	}
	if (candCount <= 0) { EqKeyCopy(out, outCap, root); return; }
	for (int i = 0; i < candCount; ++i) {
		int best = i;
		for (int j = i + 1; j < candCount; ++j) {
			const bool jBetter = (abs(cands[j].score - cands[best].score) < 0.3f)
				? (cands[j].complexity < cands[best].complexity)
				: (cands[j].score > cands[best].score);
			if (jBetter) best = j;
		}
		if (best != i) { ChordCandidate tmp = cands[i]; cands[i] = cands[best]; cands[best] = tmp; }
	}

	EqKeyCopy(out, outCap, cands[0].name); int count = 1;
	for (int i = 1; i < candCount && count < 3; i++) {
		if (cands[0].score - cands[i].score > 2.0f) break;
		if (cands[i].score < cands[0].score * 0.55f) continue;
		if (wcscmp(cands[i].name, out) == 0) continue;
		if (wcsstr(cands[i].name, L"9") && cands[0].score - cands[i].score > 1.2f) continue;
		wcsncat_s(out, outCap, L", ", _TRUNCATE);
		wcsncat_s(out, outCap, cands[i].name, _TRUNCATE);
		count++;
	}
}

// ヒストリー付きコード推定 (全帯域統合版)
static void EstimateChordRawWithHistoryIntoAll(WCHAR* out, int outCap, float* b, float* m, float* h, float* a, const WCHAR* prev) {
	EstimateChordRawWithHistoryInto(out, outCap, a, 0.03f, prev);
	if (out[0]) return;
	EstimateChordRawWithHistoryInto(out, outCap, b, 0.02f, prev);
}

// ============================================================
// AnalyzeMusicKey() - 音楽キー・コード・メロディ解析 メイン
//
// 処理フロー:
//   1. RMS計算 → 無音検出 → ヒストリークリア
//   2. Goertzel で各MIDI音の強度を計算 (低域:4096点, 高域:2048点)
//   3. 帯域別ピッチクラスに集約
//   4. コード推定 (低域/中域/高域/全体, ヒストリー付き)
//   5. FFT + Viterbi でメロディ音を推定
//   6. KeyCodeLow/Mid/High/All に結果を格納
// ============================================================
void AnalyzeMusicKey(const double* bufferL, const double* bufferR, int sampleCount, int sampleRate) {
	InitializeAnalysis((double)sampleRate);
	if (!bufferL || sampleCount <= 0) return;
	int totalSamples = sampleCount;
	bool stereo = (bufferR != nullptr);

	// 表示用フォーマット: "ルート, <コード名>" の形式（固定バッファ・CString 嵐禁止）
	auto FormatChordInto = [](WCHAR* out, int outCap, const WCHAR* s) {
		if (!s || !s[0]) {
			EqKeyCopy(out, outCap, L"!@B  , <  >!@B");
			return;
		}
		WCHAR root[8];
		if (s[0] && (s[1] == L'#' || s[1] == L'b'))
			{ root[0] = s[0]; root[1] = s[1]; root[2] = 0; }
		else
			{ root[0] = s[0]; root[1] = L' '; root[2] = 0; }
		swprintf_s(out, outCap, L"!@B%s, !@C002525<!@C000000%s!@C002525>!@C000000!@B", root, s);
	};

	auto FormatChordAllInto = [](WCHAR* out, int outCap, const WCHAR* s, const WCHAR* melody) {
		if (wcscmp(melody, L"[   ]") == 0) {
			if (!s || !s[0]) {
				EqKeyCopy(out, outCap, L"!@B  , <  >!@B");
				return;
			}
			WCHAR root[8];
			if (s[0] && (s[1] == L'#' || s[1] == L'b'))
				{ root[0] = s[0]; root[1] = s[1]; root[2] = 0; }
			else
				{ root[0] = s[0]; root[1] = L' '; root[2] = 0; }
			swprintf_s(out, outCap, L"!@B%s, !@C002525<!@C000000%s!@C002525>!@C000000!@B", root, s);
			return;
		}
		if (!s || !s[0]) {
			swprintf_s(out, outCap, L"!@B   %s, !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B", melody);
			return;
		}
		WCHAR root[8];
		if (s[0] && (s[1] == L'#' || s[1] == L'b'))
			{ root[0] = s[0]; root[1] = s[1]; root[2] = 0; }
		else
			{ root[0] = s[0]; root[1] = L' '; root[2] = 0; }
		swprintf_s(out, outCap, L"!@B%s %s, !@C002525<!@C000000%s!@C002525>!@C000000!@B", root, melody, s);
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
	}

	// 無音時は空白表示して減衰（ゴーストノート防止）
	if (isSilent) {
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
	// 低域(MIDI 0-51): 4096点 (精度優先、低周波数分解能確保)
	// 高域(MIDI 52-107): 2048点 (速度優先、高周波は短窓でOK)
	const int LOW_LIMIT = 52;
	const int LOW_N = (totalSamples >= 4096) ? 4096 : totalSamples;
	const int LOW_S = totalSamples - LOW_N;
	const int HI_N = (totalSamples >= 2048) ? 2048 : totalSamples;
	const int HI_S = totalSamples - HI_N;

	for (int k = 0; k < LOW_LIMIT; k++) {
		const double* win = (LOW_N == 4096) ? g_hannWindow4096 : nullptr;
		double aL = GoertzelMagnitude(bufferL + LOW_S, LOW_N, g_goertzelCoeffs[k], win);
		double aR = stereo ? GoertzelMagnitude(bufferR + LOW_S, LOW_N, g_goertzelCoeffs[k], win) : aL;
		float ns = (float)max(aL, aR) * (1.0f + k / 100.0f);  // 低域補正
		g_noteStrength[k] = g_noteStrengthPrev[k] * SMOOTHING_FACTOR + ns * (1.0f - SMOOTHING_FACTOR);
		g_noteStrengthPrev[k] = g_noteStrength[k];
	}
	for (int k = LOW_LIMIT; k < 108; k++) {
		const double* win = (HI_N == 2048) ? g_blackmanWindow2048 : nullptr;
		double aL = GoertzelMagnitude(bufferL + HI_S, HI_N, g_goertzelCoeffs[k], win);
		double aR = stereo ? GoertzelMagnitude(bufferR + HI_S, HI_N, g_goertzelCoeffs[k], win) : aL;
		float ns = (float)max(aL, aR) * (1.0f + k / 50.0f);   // 高域補正
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

	// メロディ Viterbi/FFT は表示が #if 0 で無効なため実行しない。
	// 毎 Speana で 4096点×2 FFT + ヒープ確保すると UI が分単位で死ぬ。
	const WCHAR* rawMelody = L"[   ]";

	// 出力コード文字列生成（ここだけで CString 化）
	WCHAR outLowW[256], outMidW[256], outAllW[256], outHighW[256];
	FormatChordInto(outLowW, _countof(outLowW), rawBass);
	FormatChordInto(outMidW, _countof(outMidW), rawMid);
	FormatChordAllInto(outAllW, _countof(outAllW), rawAll, rawMelody);

	// 高域コードが空のときはメロディ音名で補完
	if (!rawHigh[0] && wcscmp(rawMelody, L"[   ]") != 0) {
		const WCHAR* t = rawMelody + 1;
		if (t[0] && t[1] == L'#')
			{ rawHigh[0] = t[0]; rawHigh[1] = t[1]; rawHigh[2] = 0; }
		else
			{ rawHigh[0] = t[0]; rawHigh[1] = 0; }
	}
	if (wcscmp(rawMelody, L"[   ]") == 0 && !rawHigh[0])
		EqKeyCopy(outHighW, _countof(outHighW), L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B");
	else
		FormatChordInto(outHighW, _countof(outHighW), rawHigh);
	SetKeyCodesLocked(outLowW, outMidW, outHighW, outAllW);
}

// 外部から現在のノート強度配列を取得する (簡易ピアノロール表示等に使用)
void GetCurrentNoteStrengths(float* output108) {
	if (output108) memcpy(output108, g_noteStrength, sizeof(g_noteStrength));
}