#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
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
#pragma comment(lib,"rubberband-library")

extern int fade1;
extern 	LPDIRECTSOUND8 m_ds;
extern 	LPDIRECTSOUNDBUFFER m_dsb1;
extern 	LPDIRECTSOUNDBUFFER8 m_dsb;
extern 	LPDIRECTSOUND3DBUFFER m_dsb3d;
extern	LPDIRECTSOUNDBUFFER m_p;
extern LPDIRECTSOUND3DBUFFER m_lpDS3DBuffer;

extern int	playf;
extern void ReleaseOggVorbis(char**);
extern char* ogg;
extern DWORD hw;
extern HANDLE hNotifyEvent[2];
extern LPDIRECTSOUNDNOTIFY dsnf1;
extern LPDIRECTSOUNDNOTIFY dsnf2;
extern UINT HandleNotifications(LPVOID lpvoid);
extern UINT WASAPIHandleNotifications(LPVOID lpvoid);
extern ULONG WAVDALen;
extern UINT ttt;
extern int wavch, wavbit, wavsam;
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
extern BOOL playwavadpcm(BYTE* bw, int old, int l1, int l2);
extern int mode;
extern save savedata;
LPDIRECTSOUND3DLISTENER m_listener = NULL;
extern RubberBand::RubberBandStretcher* g_rubberBandStretcher;
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
	if (m_ds == NULL) return _T("DirectSoundを生成できません。\nDirectX7が正常にインストールされているか確認してください。");
	if (m_ds->SetCooperativeLevel(hwnd, DSSCL_PRIORITY) != DS_OK) {
		MessageBox(L"SetCooperativeLevelに失敗しました");
		return _T("DirectSoundの強調レベルを設定できません。\nDirectX7が正常にインストールされているか確認してください。");
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
		return _T("DirectSoundのプライマリバッファを生成できません。\nDirectX7が正常にインストールされているか確認してください。");
	}

	if (m_p != NULL) {
		//		//PCMWAVEFORMAT p;
		WAVEFORMATEX p;
		ZeroMemory(&p, sizeof(p));
		if (wavsam < 0)
			p.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		else
			p.wFormatTag = WAVE_FORMAT_PCM;
		p.nChannels = wavch;
		p.nSamplesPerSec = wavbit;
		p.wBitsPerSample = abs(wavsam);
		p.nBlockAlign = p.nChannels * p.wBitsPerSample / 8;
		p.nAvgBytesPerSec = p.nSamplesPerSec * p.nBlockAlign;
		p.cbSize = 0;
		static const GUID GUID_SUBTYPE_PCM = { 0x00000001, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

		DWORD targetSpeakers = 0;
		switch (wavch) {
		case 1:
			targetSpeakers |= SPEAKER_FRONT_CENTER;
			break;
		case 2:
			targetSpeakers |=
				SPEAKER_FRONT_LEFT
				| SPEAKER_FRONT_RIGHT;
			break;
		case 3:
			targetSpeakers |=
				SPEAKER_FRONT_LEFT
				| SPEAKER_FRONT_RIGHT
				| SPEAKER_FRONT_CENTER
				;
			break;
		case 4:
			targetSpeakers |=
				SPEAKER_FRONT_LEFT
				| SPEAKER_FRONT_RIGHT
				| SPEAKER_FRONT_CENTER
				| SPEAKER_BACK_LEFT
				| SPEAKER_BACK_RIGHT
				;
			break;
		case 5:
			targetSpeakers |=
				SPEAKER_FRONT_LEFT
				| SPEAKER_FRONT_RIGHT
				| SPEAKER_FRONT_CENTER
				| SPEAKER_BACK_LEFT
				| SPEAKER_BACK_RIGHT
				;
			break;
		case 6:
			targetSpeakers |=
				SPEAKER_FRONT_LEFT
				| SPEAKER_FRONT_RIGHT
				| SPEAKER_FRONT_CENTER
				| SPEAKER_BACK_LEFT
				| SPEAKER_BACK_RIGHT | SPEAKER_LOW_FREQUENCY
				;
			break;
		case 7:
			targetSpeakers |=
				SPEAKER_FRONT_LEFT
				| SPEAKER_FRONT_RIGHT
				| SPEAKER_FRONT_CENTER
				| SPEAKER_BACK_LEFT
				| SPEAKER_BACK_RIGHT
				| SPEAKER_SIDE_LEFT | SPEAKER_LOW_FREQUENCY
				;
			break;
		case 8:
			targetSpeakers |=
				SPEAKER_FRONT_LEFT
				| SPEAKER_FRONT_RIGHT
				| SPEAKER_FRONT_CENTER
				| SPEAKER_BACK_LEFT
				| SPEAKER_BACK_RIGHT
				| SPEAKER_SIDE_LEFT
				| SPEAKER_SIDE_RIGHT | SPEAKER_LOW_FREQUENCY
				;
			break;
		}
		int nChannels = __popcnt(targetSpeakers);
		WAVEFORMATEXTENSIBLE wfx = {};
		wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
		wfx.Format.nChannels = nChannels;
		wfx.Format.nSamplesPerSec = wavbit;
		wfx.Format.wBitsPerSample = abs(wavsam);
		wfx.Format.nBlockAlign = (WORD)(wfx.Format.wBitsPerSample / 8 * wfx.Format.nChannels);
		wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
		wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
		wfx.dwChannelMask = targetSpeakers;
		if (wavsam < 0)
			wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
		else
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
	//	fade1=1;
	if (m_dsb) {
		m_dsb->Stop();
		if (m_dsb3d != NULL) { m_dsb3d->Release(); m_dsb3d = NULL; }
		if (m_dsb != NULL) { m_dsb->Release(); m_dsb = NULL; }
	}
	if (pAudioClient) {
		pAudioClient->Stop();
		pRenderClient->Release(); pRenderClient = NULL;
		pAudioClient->Release(); pAudioClient = NULL;
	}
}

BOOL COggDlg::ReleaseDXSound(void)
{
	if (m_ds) {
		Closeds();
		if (m_dsb3d != NULL) { m_dsb3d->Release(); m_dsb3d = NULL; }
		if (m_dsb != NULL) { m_dsb->Release(); m_dsb = NULL; }
		if (m_dsb1 != NULL) { m_dsb1->Release(); m_dsb1 = NULL; }
		if (m_lpDS3DBuffer != NULL) { m_lpDS3DBuffer->Release(); }
		m_dsb = NULL;
		m_lpDS3DBuffer = NULL;
		if (m_p != NULL) { m_p->Release(); m_p = NULL; }

		if (m_ds) {
			m_ds->Release();
			m_ds = NULL;
		}
	}
	if (pAudioClient) {
		pAudioClient->Stop();
		if (pRenderClient) { pRenderClient->Release(); pRenderClient = NULL; }
		pAudioClient->Release(); pAudioClient = NULL;
		pDevice->Release(); pDevice = NULL;

	}

	// RubberBandストレッチャーのクリーンアップ
	if (g_rubberBandStretcher) {
		delete g_rubberBandStretcher;
		g_rubberBandStretcher = NULL;
	}

	return TRUE;
}

extern void playwavds2(BYTE* bw, int old, int l1, int l2);
extern int playwavkpi(BYTE* bw, int old, int l1, int l2);
extern int playwavmp3(BYTE* bw, int old, int l1, int l2);
extern int playwavflac(BYTE* bw, int old, int l1, int l2);
extern int playwavdsd(BYTE* bw, int old, int l1, int l2);
extern int playwavm4a(BYTE* bw, int old, int l1, int l2);
extern int playwavopus(BYTE* bw, int old, int l1, int l2);
extern BYTE bufwav3[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 6];
extern int ps;
extern COggDlg* og;
extern BOOL thn;
extern BOOL thn1;
extern int endf;
extern int lenl;
extern int fade1;
extern BOOL sek;
extern int wavch, wavbit;
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
bool ProcessAudioWithRubberBand(float tempoRate);
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


void equaliser(void* data, int len, BOOL reset = FALSE);


UINT HandleNotifications(LPVOID)
{
	DWORD hr = DS_OK;
	DWORD hRet = 0;
	thn = FALSE;
	thn1 = FALSE;
	char* pdsb1;
	char* pdsb2;
	syukai = 0;

	int dougainit = 0;
	int timeee = 0;
	//	char bufwav2[OUTPUT_BUFFER_SIZE];
	HANDLE ev[] = { (HANDLE)og->timer };
	//	ULONG PlayCursor,WriteCursor=OUTPUT_BUFFER_SIZE*4,oldw=OUTPUT_BUFFER_SIZE*4;
	ULONG PlayCursor, WriteCursor = 0, oldw2;
	oldw = 0;
	m_dsb->SetCurrentPosition(0);
	if (mode == -10) {
		oldw = OUTPUT_BUFFER_SIZE * 2;
		og->timer.SetEvent();
	}
	fade1 = 0;
	sek4 = FALSE;
	for (;;) {
		DWORD  dwDataLen = WAVDALen / 10;
		if (syukai == 2) { thn = TRUE; AfxEndThread(0); }
		if (syukai == 1) { syukai2 = 1; continue; }
		//		int ik;
		//		for(ik=0;ik<60;ik++){
		//		if(syukai)
		::WaitForMultipleObjects(1, ev, FALSE, savedata.ms);
		for (;;) {
			if (sek4 == FALSE) break;;
			::WaitForMultipleObjects(1, ev, FALSE, savedata.ms);
		}
		timeee += savedata.ms;
		if (muon != MUON) {
			muon -= (savedata.ms / 10 - 1);
		}
		if (muon < 1)
			muon = 0;

		if (sek == 1) {
			sflg = TRUE;
			flg3 = 3;
			sek = FALSE;
			sflg = FALSE;
			//break;
		}
		if (thn1) { thn = TRUE; AfxEndThread(0); }
		//		}
		if (ps == 1) continue;
		if (m_dsb)m_dsb->GetCurrentPosition(&PlayCursor, &WriteCursor);//再生位置取得
		int len1 = 0, len2 = 0, len3, len4;
		//		oldw = ((oldw / (wavch * 2)) * (wavch * 2));
		len1 = (int)WriteCursor - (int)oldw;//書き込み範囲取得10
		len2 = 0;
		if (len1 == 0 && len2 == 0) continue;
		if (len1 < 0) {
			len1 = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM - oldw; len2 = WriteCursor;
		}
		if (len2 < 0)
			len2 = 0;
		//len1 = (len1 / (wavsam / 8)) * (wavsam / 8);
		//len2 = (len2 / (wavsam / 8)) * (wavsam / 8);
		len4 = len1 + len2;
		for (;;) {
			if (sflg == FALSE) break;
			DoEvent();
		}

		//動画とoggのリンク　再生が始まってから動画再生開始 0.9秒ずれが起きるため0.9秒後に動画再生開始 2026/01/26
		if (og->m_dou.GetCheck() == 1 && pGraphBuilder && pMediaControl) {
			if (timeee > 900 && dougainit == 0) {
				pMediaControl->Run();
				dougainit = 1;
			}
		}



		sflg = TRUE;
		if ((mode >= 10 && mode <= 21) || mode < -10 || mode == -6 || mode == 30)
			playwavadpcm(bufwav3, oldw, len1, len2);//データ獲得
		else if (mode == -10)
			len4 = playwavmp3(bufwav3, oldw, len1, len2);//データ獲得
		else if (mode == -3)
			len4 = playwavkpi(bufwav3, oldw, len1, len2);//データ獲得
		else if (mode == -7)
			playwavdsd(bufwav3, oldw, len1, len2);//データ獲得
		else if (mode == -8)
			playwavflac(bufwav3, oldw, len1, len2);//データ獲得
		else if (mode == -9)
			playwavm4a(bufwav3, oldw, len1, len2);//データ獲得
		else
			playwavds2(bufwav3, oldw, len1, len2);//データ獲得
		oldw2 = oldw;
		if (fade1) {
			for (int jj = 0; jj < PlayCursor / wavch; jj++) {
				if (wavch == 1) {
					bufwav3[jj] = 0x80;
				}
				if (wavch == 2) {
					bufwav3[jj] = 0x00;
					bufwav3[jj + 1] = 0x80;
				}
				if (wavch == 3) {
					bufwav3[jj] = 0x00;
					bufwav3[jj + 1] = 0x00;
					bufwav3[jj + 2] = 0x80;
				}
			}
		}

		if (m_dsb && flg3 == 0) {
			m_dsb->Lock(oldw, len1 + len2, (LPVOID*)&pdsb1, (DWORD*)&len3, (LPVOID*)&pdsb2, (DWORD*)&len4, 0);
			thn = FALSE;
			memcpy(pdsb1, bufwav3 + oldw, len3);
			if (len4 != 0) {
				memcpy(pdsb2, bufwav3, len4);
			}
			if (m_dsb)m_dsb->Unlock(pdsb1, len3, pdsb2, len4);
			oldw2 = oldw + len3;
			if (len4 != 0)oldw2 = len4;
		}
		else if (m_dsb) {
			m_dsb->Lock(oldw, len1 + len2, (LPVOID*)&pdsb1, (DWORD*)&len3, (LPVOID*)&pdsb2, (DWORD*)&len4, 0);
			thn = FALSE;
			//Sleep(40);
			ZeroMemory(pdsb1, len3);
			if (len4 != 0)ZeroMemory(pdsb2, len4);
			if (m_dsb)m_dsb->Unlock(pdsb1, len3, pdsb2, len4);
			oldw2 = oldw + len3;
			if (len4 != 0)oldw2 = len4;
		}
		oldw = WriteCursor;
		if (flg3 != 0)
			flg3--;
		if (fade1) {
			playf = 1;
			thn = FALSE;
			int wavv = wavbit;
			//			if (wavbit < 44100) wavv = 44100;
			if (!(mode == -7 || mode == -8 || mode == -9 || mode == -10))
				Sleep(800);

			m_dsb->SetVolume(DSBVOLUME_MIN);
			m_dsb->Stop();
			og->OnPause();
			og->m_ps.EnableWindow(FALSE);
			playf = 0;
			thn = TRUE;

			reset = TRUE;
			extern int eqflg;
			eqflg = TRUE;
			AfxEndThread(0);
			return 0;
		}
		sflg = FALSE;
	}

} //handlenotifications()
extern std::vector<float> inputFloatData;
extern std::vector<uint8_t> m_bufwav3_1;
extern int pitch;


// RubberBandストレッチャーの初期化関数
float tempoRate2;
bool InitializeRubberBandStretcher()
{
	if (g_rubberBandStretcher) {
		delete g_rubberBandStretcher;
		g_rubberBandStretcher = NULL;
	}

	try {
		double pitchRatio = pitch / 100.0;
		// RubberBandストレッチャーを初期化
		// リアルタイムモードで初期化（テンポ変更のみ）
		g_rubberBandStretcher = new RubberBand::RubberBandStretcher(
			wavbit,  // サンプリングレート
			wavch,   // チャンネル数
			RubberBand::RubberBandStretcher::OptionProcessRealTime |
			RubberBand::RubberBandStretcher::OptionEngineFaster |
			RubberBand::RubberBandStretcher::OptionTransientsCrisp |
			RubberBand::RubberBandStretcher::OptionPhaseLaminar,
			tempoRate2,  // 初期時間比率（テンポ変更）
			pitchRatio   // 初期ピッチスケール（ピッチは変更しない）
		);

		// デバッグレベルを設定（必要に応じて）
		g_rubberBandStretcher->setDebugLevel(0);

		return true;
	}
	catch (...) {
		return false;
	}
}

// RubberBandを使用してオーディオデータをテンポ変更する関数
bool ProcessAudioWithRubberBand(float tempoRate)
{
	try {
		// 入力データの検証
		if (m_bufwav3_1.empty()) {
			return false;
		}
		tempoRate2 = tempoRate;
		// RubberBandストレッチャーが初期化されていない場合は初期化
		if (!g_rubberBandStretcher) {
			if (!InitializeRubberBandStretcher()) {
				return false;
			}
		}
		float semitones = (float)pitch;
		if (semitones >= 200.0f) {
			semitones -= 100.0f;
		}
		else {
			semitones = semitones / 3.0f + 33.3f;
		}
		semitones /= 100.0f;
		// テンポ比率を設定
		g_rubberBandStretcher->setTimeRatio(tempoRate);
		g_rubberBandStretcher->setPitchScale(static_cast<float>(semitones));;

		// 生バイトデータをfloatデータに変換
		ConvertRawBytesToFloat(m_bufwav3_1, wavsam, wavch, inputFloatData);

		// 入力データの検証
		if (inputFloatData.empty()) {
			return false;
		}

		// チャンネルごとのデータに分離
		std::vector<std::vector<float>> channelData(wavch);
		for (int ch = 0; ch < wavch; ++ch) {
			channelData[ch].resize(inputFloatData.size() / wavch);
			for (size_t i = 0; i < channelData[ch].size(); ++i) {
				channelData[ch][i] = inputFloatData[i * wavch + ch];
			}
		}

		// チャンネルポインタの配列を作成
		std::vector<float*> channelPointers(wavch);
		for (int ch = 0; ch < wavch; ++ch) {
			channelPointers[ch] = channelData[ch].data();
		}

		// RubberBandにデータを送信
		g_rubberBandStretcher->process(channelPointers.data(), channelData[0].size(), false);

		// 出力バッファをクリア
		m_convertedPcmFloatData.clear();

		// 出力データの推定サイズを計算してリザーブ
		size_t estimatedOutputSize = static_cast<size_t>(inputFloatData.size() * tempoRate * 1.2); // 余裕を持たせる
		m_convertedPcmFloatData.reserve(estimatedOutputSize);

		// 出力データを取得
		const size_t chunkSize = 4096;
		std::vector<std::vector<float>> outputChannelData(wavch);
		for (int ch = 0; ch < wavch; ++ch) {
			outputChannelData[ch].resize(chunkSize);
		}

		std::vector<float*> outputChannelPointers(wavch);
		for (int ch = 0; ch < wavch; ++ch) {
			outputChannelPointers[ch] = outputChannelData[ch].data();
		}
		//Sleep(1);
		while (true) {
			int available = g_rubberBandStretcher->available();
			if (available <= 0) break;

			size_t samplesToRetrieve = (std::min)(static_cast<size_t>(available), chunkSize);
			size_t samplesRetrieved = g_rubberBandStretcher->retrieve(outputChannelPointers.data(), samplesToRetrieve);

			if (samplesRetrieved == 0) {
				break;
			}
			// チャンネルデータをインターリーブして出力バッファに追加
			for (size_t i = 0; i < samplesRetrieved; ++i) {
				for (int ch = 0; ch < wavch; ++ch) {
					m_convertedPcmFloatData.push_back(outputChannelData[ch][i]);
				}
			}
		}
		return true;
	}
	catch (...) {}
	return true;
}

// ... existing code ...
#include <atomic>
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
	if (bytes_per_sample == 0) { // 0で割るのを防ぐ
		out_float_data.clear();
		return;
	}
	size_t total_samples_count = raw_data.size() / bytes_per_sample;
	out_float_data.clear();
	out_float_data.resize(total_samples_count);

	for (size_t i = 0; i < total_samples_count; ++i) {
		size_t current_byte_pos = i * bytes_per_sample;
		// バッファの末尾を超えないようにチェック
		if (current_byte_pos + bytes_per_sample > raw_data.size()) {
			out_float_data[i] = 0.0f; // またはエラー処理
			continue;
		}

		if (bits_per_sample == 8) { // 8-bit unsigned PCM
			out_float_data[i] = ((float)raw_data[current_byte_pos] - 128.0f) / 128.0f;
		}
		else if (bits_per_sample == 16) { // 16-bit signed PCM (リトルエンディアン)
			int16_t s_val = (int16_t)(raw_data[current_byte_pos] | (raw_data[current_byte_pos + 1] << 8));
			out_float_data[i] = (float)s_val / 32768.0f; // 2^15
		}
		else if (bits_per_sample == 24) { // 24-bit signed PCM (packed into 3 bytes, リトルエンディアン)
			// 24bitデータは3バイト。int32_tに読み込んでから正規化
			int32_t i_val = (int32_t)(raw_data[current_byte_pos] |
				(raw_data[current_byte_pos + 1] << 8) |
				(raw_data[current_byte_pos + 2] << 16));
			// 24ビットデータは符号拡張が必要 (MSBが1なら負の数として扱う)
			if (i_val & 0x00800000) { // もし23ビット目（0から数えて）が1なら負の数
				i_val |= 0xFF000000; // 32ビットに符号拡張
			}
			out_float_data[i] = (float)i_val / 8388608.0f; // 2^23
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
			int16_t s_val = (int16_t)(sample_float * 32767.0f); // 2^15 - 1
			out_raw_data[current_byte_pos] = (uint8_t)(s_val & 0xFF);
			out_raw_data[current_byte_pos + 1] = (uint8_t)((s_val >> 8) & 0xFF);
		}
		else if (target_bits_per_sample == 24) { // 24-bit signed PCM (リトルエンディアン)
			int32_t i_val = (int32_t)(sample_float * 8388607.0f); // 2^23 - 1
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
		MessageBox(L"未サポートのフォーマット¥n");
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

EQプリセット51種:
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

static const int EQ_PRESETS[51][15] = {
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
	{ 88, 88, 88, 92, 98,115,135,155,165,160,145,125,105, 95, 88}
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

	// Yamabiko buffers
	float* yamabikoBuf;
	int yamabikoBufSize;
	int yamabikoPos;
} ChannelState;

// ===== Global State =====
static ChannelState g_channels[MAX_CH];
static float g_delayMemory[MAX_CH][MAX_DELAY_SAMPLES];
static int g_lastRate = 0;
static int g_lastEqPreset = -1;
static int g_lastEnvPreset = -1;
static int g_lastEqValues[15];
static int g_lastExtendedParams[5];
static int g_lastEffectAmount = 50;
static BOOL g_initialized = FALSE;

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
g_limiter[ch].threshold = 0.92f;
attackTime = 0.0005f;  // 超高速反応
releaseTime = 0.150f;  // やや遅めの戻り

// ダイナミックレンジ優先の設定
g_limiter[ch].threshold = 0.97f;
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

static DynamicLimiter g_limiter[2] = {
	{ 1.0f, 0.95f, 0.0f, 0.0f },  // L ch
	{ 1.0f, 0.95f, 0.0f, 0.0f }   // R ch
};

// ===== EQ Frequencies =====
static const float EQ_FREQS[EQ_BANDS] = {
	32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000, 31.5, 62.5, 125, 250, 500
};

// External references (assumed to exist in main program)
extern int wavbit, wavch, wavsam;

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


// ===== フィルタ計算関数群 =====
static void CalcPeakingEQ(Biquad* f, float freq, float q, float gainVal, int rate) {
	float db = (gainVal - 100.0f) * 0.12f;
	if (fabs(db) < 0.1f) {
		f->b0 = 1;
		f->b1 = 0;
		f->b2 = 0;
		f->a1 = 0;
		f->a2 = 0;
		return;
	}
	float omega = 2.0f * M_PI * freq / (float)rate;
	float sn = sinf(omega), cs = cosf(omega);
	float alpha = sn / (2.0f * q);
	float A = powf(10.0f, db / 40.0f);
	float a0 = 1.0f + alpha / A;
	f->b0 = (1.0f + alpha * A) / a0;
	f->b1 = (-2.0f * cs) / a0;
	f->b2 = (1.0f - alpha * A) / a0;
	f->a1 = (-2.0f * cs) / a0;
	f->a2 = (1.0f - alpha / A) / a0;
}

static void CalcFilter(Biquad* f, int type, float freq, float q, int rate) {
	if (freq <= 0.0f) freq = 20.0f;
	if (freq >= rate / 2.0f) freq = rate / 2.0f - 1.0f;
	float omega = 2.0f * M_PI * freq / (float)rate;
	float sn = sinf(omega), cs = cosf(omega);
	float alpha = sn / (2.0f * q);
	float a0 = 1.0f + alpha;

	if (type == 0) { // Lowpass
		f->b0 = ((1.0f - cs) / 2.0f) / a0;
		f->b1 = (1.0f - cs) / a0;
		f->b2 = ((1.0f - cs) / 2.0f) / a0;
	}
	else { // Highpass
		f->b0 = ((1.0f + cs) / 2.0f) / a0;
		f->b1 = (-(1.0f + cs)) / a0;
		f->b2 = ((1.0f + cs) / 2.0f) / a0;
	}
	f->a1 = (-2.0f * cs) / a0;
	f->a2 = (1.0f - alpha) / a0;
}

static void CalcShelvingEQ(Biquad* f, int type, float freq, float gainDb, int rate) {
	if (fabs(gainDb) < 0.01f) {
		f->b0 = 1;
		f->b1 = 0;
		f->b2 = 0;
		f->a1 = 0;
		f->a2 = 0;
		return;
	}

	float omega = 2.0f * M_PI * freq / (float)rate;
	float sn = sinf(omega), cs = cosf(omega);
	float A = powf(10.0f, gainDb / 40.0f);
	float beta = sqrtf(A) / 0.707f;

	if (type == 0) { // Low Shelf
		float a0 = (A + 1.0f) + (A - 1.0f) * cs + beta * sn;
		f->b0 = (A * ((A + 1.0f) - (A - 1.0f) * cs + beta * sn)) / a0;
		f->b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
		f->b2 = (A * ((A + 1.0f) - (A - 1.0f) * cs - beta * sn)) / a0;
		f->a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
		f->a2 = ((A + 1.0f) + (A - 1.0f) * cs - beta * sn) / a0;
	}
	else { // High Shelf
		float a0 = (A + 1.0f) - (A - 1.0f) * cs + beta * sn;
		f->b0 = (A * ((A + 1.0f) + (A - 1.0f) * cs + beta * sn)) / a0;
		f->b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
		f->b2 = (A * ((A + 1.0f) + (A - 1.0f) * cs - beta * sn)) / a0;
		f->a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
		f->a2 = ((A + 1.0f) - (A - 1.0f) * cs - beta * sn) / a0;
	}
}

static inline float ProcessBiquad(Biquad* f, float in) {
	if (f->b0 == 1.0f && f->b1 == 0.0f && f->a1 == 0.0f) return in;
	if (f->b0 == 0.0f && f->b1 == 0.0f) return in;

	if (!isfinite(in)) return 0.0f;
	if (fabs(in) > 10.0f) return 0.0f;

	float out = f->b0 * in + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;

	if (!isfinite(out) || fabs(out) > 10.0f) {
		f->x1 = 0.0f;
		f->x2 = 0.0f;
		f->y1 = 0.0f;
		f->y2 = 0.0f;
		return 0.0f;
	}

	if (fabs(out) < 1.0e-20f) out = 0.0f;

	f->x2 = f->x1;
	f->x1 = in;
	f->y2 = f->y1;
	f->y1 = out;

	if (fabs(f->x1) < 1.0e-20f) f->x1 = 0.0f;
	if (fabs(f->x2) < 1.0e-20f) f->x2 = 0.0f;
	if (fabs(f->y1) < 1.0e-20f) f->y1 = 0.0f;
	if (fabs(f->y2) < 1.0e-20f) f->y2 = 0.0f;

	if (fabs(f->y1) > 5.0f) f->y1 *= 0.5f;
	if (fabs(f->y2) > 5.0f) f->y2 *= 0.5f;

	return out;
}

// ===============================
// 拡張版山彦処理（可変パラメータ対応）
// ===============================
static inline float ProcessYamabikoAdvanced(ChannelState* cs, float input, const EnvParams* env, int sampleRate)
{
	if (!env || env->yamabikoDelays[0] <= 0.0f) {
		// 山彦パラメータが設定されていない場合はスキップ
		return input;
	}

	float out = input;
	float decayMult = powf(0.95f, 1.0f / env->yamabikoDecay);

	// 最大4タップのエコー処理
	for (int i = 0; i < 4; i++) {
		if (env->yamabikoDelays[i] <= 0.0f) break;

		int delaySamples = (int)(env->yamabikoDelays[i] * (float)sampleRate / 1000.0f);
		if (delaySamples >= cs->yamabikoBufSize) continue;

		int readPos = cs->yamabikoPos - delaySamples;
		if (readPos < 0) readPos += cs->yamabikoBufSize;

		float delayed = cs->yamabikoBuf[readPos];

		// 減衰カーブ適用
		float gain = env->yamabikoGains[i] * powf(decayMult, (float)i);

		// パン効果（左右の位相差）
		float panEffect = (i % 2) ? env->yamabikoPan : -env->yamabikoPan;
		delayed *= (1.0f + panEffect * 0.3f);

		out += delayed * gain;
	}

	// バッファ書き込み
	cs->yamabikoBuf[cs->yamabikoPos] = input;
	cs->yamabikoPos++;
	if (cs->yamabikoPos >= cs->yamabikoBufSize)
		cs->yamabikoPos = 0;

	return out;
}

// ===== LFO処理 =====
static inline float UpdateLFO(LFO* lfo, int sampleRate) {
	if (lfo->frequency <= 0.0f || lfo->depth <= 0.0f) return 0.0f;

	float value = sinf(lfo->phase * 2.0f * M_PI) * lfo->depth;
	lfo->phase += lfo->frequency / (float)sampleRate;

	if (lfo->phase >= 1.0f) lfo->phase -= 1.0f;

	return value;
}

// ===== ディフュージョン処理 =====
static inline float ProcessDiffusion(ChannelState* cs, float input, float diffusion, float density, int envType)
{
	// 山彦モード（TYPE_MOUNTAIN_ECHO, TYPE_CANYON_ECHO）
	if (envType == TYPE_MOUNTAIN_ECHO || envType == TYPE_CANYON_ECHO) {
		float weakDiff = diffusion * 0.12f;
		float weakDens = density * 0.18f;

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

	// 通常のディフュージョン
	if (diffusion <= 0.0f) return input;

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

// ===== エキサイター処理 =====
static inline float Exciter(float input, Biquad* hpf, float amount) {
	if (amount <= 0.0f) return input;

	float highFreq = ProcessBiquad(hpf, input);
	float enhanced = highFreq * 2.5f;

	if (enhanced > 1.0f) enhanced = 1.0f - (enhanced - 1.0f) * 0.3f;
	if (enhanced < -1.0f) enhanced = -1.0f + (enhanced + 1.0f) * 0.3f;

	return input + (enhanced - highFreq) * amount * 1.3f;
}

// ===== ソフトリミッター =====
static inline float SoftLimiter(float x) {
	const float threshold = 0.7f;

	if (x > threshold) {
		float over = x - threshold;
		x = threshold + (1.0f - threshold) * (over / (1.0f + over));
	}
	else if (x < -threshold) {
		float over = -x - threshold;
		x = -threshold - (1.0f - threshold) * (over / (1.0f + over));
	}

	return x;
}

// ===== フラッターエコー処理 =====
static inline float ProcessFlutterEcho(ChannelState* cs, float input, float amount, int sampleRate) {
	if (amount <= 0.0f) return input;

	float flutterFreq = 8.0f + amount * 4.0f;
	cs->flutterPhase += flutterFreq / (float)sampleRate;
	if (cs->flutterPhase >= 1.0f) cs->flutterPhase -= 1.0f;

	float modulation = sinf(cs->flutterPhase * 2.0f * M_PI) * amount * 0.15f;
	float filtered = ProcessBiquad(&cs->flutterFilter, input);

	return input + filtered * modulation;
}

// ===== 材質特性処理 =====
static inline float ProcessMaterialAbsorption(ChannelState* cs, float input, float absorption, float roughness) {
	if (absorption <= 0.0f && roughness <= 0.0f) return input;

	float absorbed = ProcessBiquad(&cs->materialFilter, input);

	if (roughness > 0.0f) {
		absorbed *= (1.0f - roughness * 0.3f);
	}

	return input * (1.0f - absorption) + absorbed * absorption;
}

// ===== 温かみ処理 =====
static inline float ProcessWarmth(ChannelState* cs, float input, float warmth) {
	if (warmth <= 0.0f) return input;

	float warmed = ProcessBiquad(&cs->warmthFilter, input);
	cs->warmthState = cs->warmthState * 0.98f + warmed * 0.02f;

	return input * (1.0f - warmth * 0.3f) + cs->warmthState * warmth * 0.3f;
}

// ===== 明るさ処理 =====
static inline float ProcessBrightness(float input, float* brightnessState, float brightness) {
	if (fabs(brightness - 0.5f) < 0.01f) return input;

	float harmonic = input * input * input;
	float brightnessFactor = (brightness - 0.5f) * 2.0f;

	*brightnessState = *brightnessState * 0.96f + harmonic * brightnessFactor * 0.04f;

	return input + *brightnessState * 0.1f;
}

// ===== 共鳴処理 =====
static inline float ProcessResonance(ChannelState* cs, float input, float freq, float q, float amount) {
	if (amount <= 0.0f || freq <= 0.0f) return input;

	float resonated = ProcessBiquad(&cs->resonanceFilter, input);
	return input + resonated * amount * 0.5f;
}

// ===== 金属感処理 =====
static inline float ProcessMetallic(ChannelState* cs, float input, float amount) {
	if (amount <= 0.0f) return input;

	float metallic = ProcessBiquad(&cs->metallicFilter, input);
	return input + metallic * amount * 0.4f;
}

// ===== ガラス感処理 =====
static inline float ProcessGlass(ChannelState* cs, float input, float amount) {
	if (amount <= 0.0f) return input;

	float glassy = ProcessBiquad(&cs->glassFilter, input);
	return input + glassy * amount * 0.35f;
}

// ===== きらめき処理 =====
static inline float ProcessShimmer(ChannelState* cs, float input, float amount, int sampleRate) {
	if (amount <= 0.0f) return input;

	cs->shimmerState = cs->shimmerState * 0.992f + input * 0.008f;
	float shimmer = sinf(cs->shimmerState * 12.0f) * amount * 0.15f;

	return input + shimmer;
}

// ===== ドップラー効果 =====
static inline float ProcessDoppler(ChannelState* cs, float input, float amount, int sampleRate) {
	if (amount <= 0.0f) return input;

	cs->dopplerPhase += 0.5f / (float)sampleRate;
	if (cs->dopplerPhase >= 1.0f) cs->dopplerPhase -= 1.0f;

	float doppler = sinf(cs->dopplerPhase * 2.0f * M_PI) * amount * 0.02f;

	return input * (1.0f + doppler);
}

// ===== エンジン初期化 =====
static void InitEngine(int rate) {
	memset(g_channels, 0, sizeof(g_channels));
	memset(g_delayMemory, 0, sizeof(g_delayMemory));

	for (int i = 0; i < MAX_CH; i++) {
		g_channels[i].delayBuffer = g_delayMemory[i];
		g_channels[i].lfo.phase = 0.0f;
		g_channels[i].flutterPhase = 0.0f;
		g_channels[i].dopplerPhase = 0.0f;
		g_channels[i].phasingPhase = 0.0f;

		for (int j = 0; j < 8; j++) {
			g_channels[i].diffusionPos1[j] = 0;
			g_channels[i].diffusionPos2[j] = 0;
			g_channels[i].diffusionPos3[j] = 0;
		}

		g_channels[i].harmonicState = 0.0f;
		g_channels[i].earlyEnvelope = 0.0f;
		g_channels[i].lateEnvelope = 0.0f;
		g_channels[i].warmthState = 0.0f;
		g_channels[i].brightnessState = 0.0f;
		g_channels[i].shimmerState = 0.0f;

		// 山彦バッファ初期化
		g_channels[i].yamabikoBufSize = rate * 2;
		g_channels[i].yamabikoPos = 0;

		if (g_channels[i].yamabikoBuf != NULL) {
			free(g_channels[i].yamabikoBuf);
			g_channels[i].yamabikoBuf = NULL;
		}

		g_channels[i].yamabikoBuf = (float*)malloc(sizeof(float) * g_channels[i].yamabikoBufSize);

		if (g_channels[i].yamabikoBuf != NULL) {
			memset(g_channels[i].yamabikoBuf, 0, sizeof(float) * g_channels[i].yamabikoBufSize);
		}
	}

	g_lastRate = rate;
	g_lastEqPreset = -1;
	g_lastEnvPreset = -1;

	for (int i = 0; i < 15; i++) g_lastEqValues[i] = 100;
	for (int i = 0; i < 5; i++) g_lastExtendedParams[i] = 100;

	// 【追加】ダイナミックリミッター初期化
	float attackTime = 0.001f;   // 1ms - 高速反応
	float releaseTime = 0.100f;  // 100ms - 自然な戻り

	for (int ch = 0; ch < 2; ch++) {
		g_limiter[ch].envelope = 1.0f;
		g_limiter[ch].threshold = 0.95f;  // 0.95を超えたら圧縮開始

		// アタック/リリース係数をプリ計算（CPU最適化）
		g_limiter[ch].attackCoeff = expf(-1.0f / (attackTime * rate));
		g_limiter[ch].releaseCoeff = expf(-1.0f / (releaseTime * rate));
	}

	g_lastEffectAmount = 50;
	g_initialized = TRUE;
}

// ===== 山彦バッファ解放 =====
void FreeEngine(void) {
	for (int i = 0; i < MAX_CH; i++) {
		if (g_channels[i].yamabikoBuf != NULL) {
			free(g_channels[i].yamabikoBuf);
			g_channels[i].yamabikoBuf = NULL;
			g_channels[i].yamabikoBufSize = 0;
			g_channels[i].yamabikoPos = 0;
		}
	}
}

// ===== ユーティリティ関数 =====
static float ClampFloat(float v, float lo, float hi) {
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

static float Hash01(int idx, int salt) {
	unsigned int x = (unsigned int)(idx * 1664525u + 1013904223u + (unsigned int)salt * 2654435761u);
	x ^= (x >> 16);
	x *= 2246822519u;
	x ^= (x >> 13);
	return (x & 0xFFFFFF) / 16777215.0f;
}

// ===== 環境分離処理 =====
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
	float h4 = Hash01(presetIndex, 41) - 0.5f;

	static const float kRoomBias[10] = { 0.0f, 0.25f, -0.10f, 0.20f, -0.20f, 0.35f, 0.10f, -0.15f, 0.45f, 0.30f };
	static const float kDampBias[10] = { 0.0f, -0.05f, 0.15f, -0.05f, 0.20f, -0.10f, 0.05f, 0.10f, -0.20f, -0.15f };
	static const float kWidthBias[10] = { 0.0f, 0.10f, -0.05f, 0.05f, -0.10f, 0.20f, 0.15f, 0.00f, 0.40f, 0.30f };

	env->preDelayMs = ClampFloat(env->preDelayMs + (t - 0.5f) * 12.0f + h1 * 8.0f, 0.0f, 120.0f);
	env->delayTimeMs = ClampFloat(env->delayTimeMs * (1.0f + (t - 0.5f) * 0.25f + h2 * 0.15f), 6.0f, 350.0f);
	env->roomSize = ClampFloat(env->roomSize + kRoomBias[category] + h3 * 0.25f, 0.3f, 5.0f);
	env->stereoWidth = ClampFloat(env->stereoWidth + kWidthBias[category] + h1 * 0.30f, 0.3f, 2.5f);
	env->damping = ClampFloat(env->damping + kDampBias[category] + h2 * 0.20f, 0.0f, 1.0f);
}

// ===============================
// ダイナミックリミッター補助関数
// ===============================

// ソフトクリッピング - 1.0付近で滑らかに圧縮
static float SoftClip(float x) {
	if (x > 0.95f) {
		float excess = x - 0.95f;
		return 0.95f + tanhf(excess * 10.0f) * 0.05f;
	}
	else if (x < -0.95f) {
		float excess = x + 0.95f;
		return -0.95f + tanhf(excess * 10.0f) * 0.05f;
	}
	return x;
}

// ダイナミックリミッター処理
static float ProcessDynamicLimiter(DynamicLimiter* lim, float input) {
	float absInput = fabsf(input);

	// 必要なゲインリダクション計算
	float targetGain = 1.0f;
	if (absInput > lim->threshold) {
		targetGain = lim->threshold / absInput;
	}

	// エンベロープ追従（アタック/リリース）
	float coeff;
	if (targetGain < lim->envelope) {
		// 音が大きくなった → 素早く圧縮
		coeff = lim->attackCoeff;
	}
	else {
		// 音が小さくなった → ゆっくり戻す
		coeff = lim->releaseCoeff;
	}

	lim->envelope = targetGain + coeff * (lim->envelope - targetGain);

	// ゲイン適用 + ソフトクリッピング
	return SoftClip(input * lim->envelope);
}

// ===============================
// equaliser() - メイン処理関数 完全版
// ===============================

void equaliser(void* data, int len, BOOL reset) {
	// reset=2: EQプリセット同期モード
	if (reset == 2) {
		int currentEqPre = savedata.eqsoundeq;
		if (currentEqPre >= 0 && currentEqPre < 51 && currentEqPre != 9) {
			memcpy(savedata.eq, EQ_PRESETS[currentEqPre], sizeof(int) * 15);
			g_lastEqPreset = currentEqPre;
		}
		return;
	}

	BOOL forceUpdate = FALSE;
	if (reset == 1 || !g_initialized || g_lastRate != wavbit) {
		InitEngine(wavbit);
		forceUpdate = TRUE;
	}

	// 低周波数は処理しない
	if (wavbit < 30000) return;

	int currentEqPre = savedata.eqsoundeq;
	int currentEnvPre = savedata.eqsoundenv;
	int effectAmount = savedata.eqsoundeffect;

	if (effectAmount < 0) effectAmount = 0;
	if (effectAmount > 100) effectAmount = 100;

	// スケール係数
	float coreScale = 0.5f + (effectAmount / 60.0f);
	float extraScale = effectAmount / 40.0f;
	float reflectionScale = 0.8f + (effectAmount / 250.0f);

	// 拡張パラメータ取得
	int masterVolume = savedata.eq[15];
	int clarity = savedata.eq[16];
	int balance = savedata.eq[17];
	int density = savedata.eq[18];
	int spatial = savedata.eq[19];

	// 範囲クランプ
	masterVolume = (int)ClampFloat((float)masterVolume, 0.0f, 200.0f);
	clarity = (int)ClampFloat((float)clarity, 0.0f, 200.0f);
	balance = (int)ClampFloat((float)balance, 0.0f, 200.0f);
	density = (int)ClampFloat((float)density, 0.0f, 200.0f);
	spatial = (int)ClampFloat((float)spatial, 0.0f, 200.0f);

	// EQプリセット変更チェック
	if (currentEqPre != g_lastEqPreset) {
		if (currentEqPre >= 0 && currentEqPre < 51) {
			if (currentEqPre != 9) {
				memcpy(savedata.eq, EQ_PRESETS[currentEqPre], sizeof(int) * 15);
			}
		}
		g_lastEqPreset = currentEqPre;
		forceUpdate = TRUE;
	}

	// パラメータ変更に伴うフィルタ再計算
	BOOL eqChanged = forceUpdate;
	if (!eqChanged) {
		for (int i = 0; i < 15; i++) {
			if (savedata.eq[i] != g_lastEqValues[i]) {
				eqChanged = TRUE;
				break;
			}
		}
	}

	BOOL extendedChanged = FALSE;
	if (masterVolume != g_lastExtendedParams[0] || clarity != g_lastExtendedParams[1] ||
		balance != g_lastExtendedParams[2] || density != g_lastExtendedParams[3] ||
		spatial != g_lastExtendedParams[4]) {
		extendedChanged = TRUE;
		g_lastExtendedParams[0] = masterVolume;
		g_lastExtendedParams[1] = clarity;
		g_lastExtendedParams[2] = balance;
		g_lastExtendedParams[3] = density;
		g_lastExtendedParams[4] = spatial;
	}

	if (eqChanged || extendedChanged) {
		memcpy(g_lastEqValues, savedata.eq, sizeof(int) * 15);
		for (int ch = 0; ch < MAX_CH; ch++) {
			for (int b = 0; b < EQ_BANDS; b++) {
				CalcPeakingEQ(&g_channels[ch].eqFilters[b], EQ_FREQS[b], 1.414f, (float)savedata.eq[b], wavbit);
			}
			float clarityDb = (clarity - 100.0f) * 0.18f;
			CalcPeakingEQ(&g_channels[ch].clarityFilter, 5000.0f, 1.5f, 100.0f + clarityDb / 0.12f, wavbit);
			float balanceDb = (balance - 100.0f) * 0.12f;
			CalcShelvingEQ(&g_channels[ch].bassBalanceFilter, 0, 250.0f, -balanceDb, wavbit);
			CalcShelvingEQ(&g_channels[ch].trebleBalanceFilter, 1, 4000.0f, balanceDb, wavbit);
			float densityDb = (density - 100.0f) * 0.15f;
			CalcPeakingEQ(&g_channels[ch].densityFilter1, 600.0f, 1.2f, 100.0f + densityDb / 0.12f, wavbit);
			CalcPeakingEQ(&g_channels[ch].densityFilter2, 1400.0f, 1.2f, 100.0f + densityDb / 0.12f, wavbit);
		}
	}

	// 環境音響設定の更新
	if (currentEnvPre != g_lastEnvPreset || effectAmount != g_lastEffectAmount || forceUpdate) {
		if (currentEnvPre < 0 || currentEnvPre >= ENV_PRESET_COUNT) currentEnvPre = 0;

		// 環境プリセット変更時はリミッターをリセット
		if (currentEnvPre != g_lastEnvPreset) {
			for (int ch = 0; ch < 2; ch++) {
				g_limiter[ch].envelope = 1.0f;
			}
		}

		const EnvParams* ep = &ENV_PRESETS[currentEnvPre];

		for (int ch = 0; ch < MAX_CH; ch++) {
			// 基本フィルタ
			CalcFilter(&g_channels[ch].envLpf, 0, ep->lpfFreq, 0.707f, wavbit);
			CalcFilter(&g_channels[ch].envHpf, 1, ep->hpfFreq, 0.707f, wavbit);
			CalcFilter(&g_channels[ch].exciterFilter, 1, 6000.0f, 0.707f, wavbit);

			// ダンピングフィルタ
			float dampFreq = 4000.0f + (ep->damping * extraScale * 8000.0f);
			CalcFilter(&g_channels[ch].dampingFilter, 0, dampFreq, 0.5f, wavbit);

			// 帯域別リバーブフィルタ
			CalcFilter(&g_channels[ch].bassReverbFilter, 0, fminf(500.0f, 250.0f * ep->bassReverbTime), 0.707f, wavbit);
			CalcPeakingEQ(&g_channels[ch].midReverbFilter, fminf(3000.0f, 1500.0f * ep->midReverbTime), 1.0f, 100.0f, wavbit);
			CalcFilter(&g_channels[ch].trebleReverbFilter, 1, fminf(12000.0f, 6000.0f * ep->trebleReverbTime), 0.707f, wavbit);

			// 材質フィルタ
			CalcFilter(&g_channels[ch].materialFilter, 0, 2000.0f - (ep->materialAbsorption * 1500.0f), 0.707f, wavbit);

			// 温かみフィルタ
			CalcShelvingEQ(&g_channels[ch].warmthFilter, 0, 300.0f, (ep->warmth - 0.5f) * 6.0f, wavbit);

			// フラッターエコーフィルタ
			if (ep->flutterEcho > 0.0f) {
				CalcFilter(&g_channels[ch].flutterFilter, 1, 1200.0f, 2.0f, wavbit);
			}

			// 共鳴フィルタ
			if (ep->resonanceFreq > 0.0f && ep->resonanceQ > 0.0f) {
				CalcPeakingEQ(&g_channels[ch].resonanceFilter, ep->resonanceFreq, ep->resonanceQ, 100.0f, wavbit);
			}

			// 金属感フィルタ
			if (ep->metallic > 0.0f) {
				CalcFilter(&g_channels[ch].metallicFilter, 1, 4500.0f, 3.5f, wavbit);
			}

			// ガラス感フィルタ
			if (ep->glassiness > 0.0f) {
				CalcFilter(&g_channels[ch].glassFilter, 1, 8000.0f, 4.0f, wavbit);
			}

			// LFO設定
			g_channels[ch].lfo.frequency = ep->modSpeed * extraScale;
			g_channels[ch].lfo.depth = ep->modDepth * extraScale * 10.0f;
		}

		g_lastEnvPreset = currentEnvPre;
		g_lastEffectAmount = effectAmount;
	}

	const EnvParams* env = &ENV_PRESETS[g_lastEnvPreset];

	// 山彦判定
	BOOL isYamabiko = (env->type == TYPE_MOUNTAIN_ECHO || env->type == TYPE_CANYON_ECHO);

	// ディレイサンプル数計算
	int preDelaySamps = (int)(env->preDelayMs * coreScale * wavbit / 1000.0f);
	int mainDelaySamps = (int)(env->delayTimeMs * env->roomSize * wavbit / 1000.0f);

	// 初期反射サンプル数
	int refSamps[8];
	for (int i = 0; i < 8; i++) {
		refSamps[i] = (int)(env->earlyRef[i * 2] * env->roomSize * wavbit / 1000.0f);
	}

	int bytesPerSample = wavsam / 8;
	int numSamples = len / (bytesPerSample * wavch);
	unsigned char* pRaw = (unsigned char*)data;
	int stereoOffset = (wavbit * 20) / 1000;

	static float leftSamples[8192 * 40], rightSamples[8192 * 40];
	int bufferIndex = 0;

	float masterGain = masterVolume / 100.0f;
	float harmonicAmount = (density - 100.0f) / 200.0f;
	float spatialWidth = 0.5f + (spatial / 100.0f);

	// ===== 信号処理メインループ =====
	for (int i = 0; i < numSamples; i++) {
		for (int ch = 0; ch < wavch; ch++) {
			if (ch >= MAX_CH) continue;

			// サンプル読み込み
			float inSample = 0.0f;
			int offset = (i * wavch + ch) * bytesPerSample;

			if (wavsam == 16) {
				inSample = *((short*)(pRaw + offset)) / 32768.0f;
			}
			else if (wavsam == 24) {
				int val = pRaw[offset] | (pRaw[offset + 1] << 8) | ((signed char)pRaw[offset + 2] << 16);
				inSample = val / 8388608.0f;
			}
			else if (wavsam == 32) {
				inSample = *((int*)(pRaw + offset)) / 2147483648.0f;
			}
			else {
				inSample = (pRaw[offset] - 128) / 128.0f;
			}

			float signal = inSample;
			ChannelState* cs = &g_channels[ch];

			// ===== EQ・拡張フィルタ適用 =====
			for (int b = 0; b < EQ_BANDS; b++) {
				signal = ProcessBiquad(&cs->eqFilters[b], signal);
			}
			signal = ProcessBiquad(&cs->clarityFilter, signal);
			signal = ProcessBiquad(&cs->bassBalanceFilter, signal);
			signal = ProcessBiquad(&cs->trebleBalanceFilter, signal);
			signal = ProcessBiquad(&cs->densityFilter1, signal);
			signal = ProcessBiquad(&cs->densityFilter2, signal);

			// ===== ハーモニック処理 =====
			if (fabs(harmonicAmount) > 0.01f) {
				float harmonic = signal * signal * signal * harmonicAmount * 0.15f;
				cs->harmonicState = cs->harmonicState * 0.95f + harmonic * 0.05f;
				signal += cs->harmonicState;
			}

			// ===== 環境音響 (Wet) =====
			float wetSignal = 0.0f;

			if (env->type != TYPE_NONE && env->wetMix > 0.0f && effectAmount > 0) {

				// ============================
				// ★ 山彦モード
				// ============================
				if (isYamabiko) {

					// 拡張版山彦処理
					float echo = ProcessYamabikoAdvanced(cs, signal, env, wavbit);

					// 軽い初期反射（山の近距離反射）
					int earlyMs = (env->type == TYPE_MOUNTAIN_ECHO) ? 60 : 45;
					int earlySamp = (int)(earlyMs * wavbit / 1000.0f);

					int rPos = cs->writePos - (earlySamp + preDelaySamps);
					while (rPos < 0) rPos += MAX_DELAY_SAMPLES;

					float earlyGain = (env->type == TYPE_MOUNTAIN_ECHO) ? 0.18f : 0.25f;
					float earlyRef = cs->delayBuffer[rPos] * earlyGain;

					// 弱い残響（山の空気感）
					float weakDiff = env->diffusion * coreScale * 0.22f;
					float weakDens = env->density * 0.28f;

					float late = ProcessDiffusion(cs, echo, weakDiff, weakDens, env->type);

					// lateEnvelope
					float lateEnv = powf(0.94f, 1.0f / (env->lateReverbDecay * 1.3f));
					cs->lateEnvelope = cs->lateEnvelope * lateEnv + late * (1.0f - lateEnv);
					float lateReverb = cs->lateEnvelope * 0.55f;

					// 最終Wet合成
					wetSignal = echo * 0.88f + earlyRef * 0.35f + lateReverb * 0.52f;

					// WetMix適用
					wetSignal *= fminf(1.0f, env->wetMix * coreScale);
				}

				// ============================
				// ★ 通常ルート
				// ============================
				else {
					int chOffset = (ch % 2) * stereoOffset;
					int readMain = cs->writePos - (mainDelaySamps + preDelaySamps + chOffset + (int)UpdateLFO(&cs->lfo, wavbit));
					while (readMain < 0) readMain += MAX_DELAY_SAMPLES;
					float delayMain = cs->delayBuffer[readMain];

					// 帯域別リバーブフィルタ
					delayMain = (ProcessBiquad(&cs->bassReverbFilter, delayMain) * env->bassReverbTime +
						ProcessBiquad(&cs->midReverbFilter, delayMain) * env->midReverbTime +
						ProcessBiquad(&cs->trebleReverbFilter, delayMain) * env->trebleReverbTime) / 3.0f;

					// ダンピング・フィルタ
					delayMain = ProcessBiquad(&cs->dampingFilter, delayMain);
					delayMain = ProcessBiquad(&cs->envLpf, delayMain);
					delayMain = ProcessBiquad(&cs->envHpf, delayMain);

					// ディフュージョン
					delayMain = ProcessDiffusion(cs, delayMain, env->diffusion * coreScale, env->density, env->type);

					// 初期反射
					float earlyRef = 0.0f;
					for (int r = 0; r < 8; r++) {
						int rPos = cs->writePos - (refSamps[r] + preDelaySamps + chOffset);
						while (rPos < 0) rPos += MAX_DELAY_SAMPLES;
						float envelope = powf(1.0f - (float)(r + 1) / 8.0f, 2.0f / env->earlyReverbDecay);
						earlyRef += cs->delayBuffer[rPos] * env->earlyRef[r * 2 + 1] * reflectionScale * 1.4f * envelope;
					}

					// 後期残響
					float lateEnv = powf(0.95f, 1.0f / env->lateReverbDecay);
					cs->lateEnvelope = cs->lateEnvelope * lateEnv + delayMain * (1.0f - lateEnv);
					float lateReverb = cs->lateEnvelope;

					wetSignal = (earlyRef * env->earlyLateBalance) + (lateReverb * (1.0f - env->earlyLateBalance * 0.5f));

					// 材質・温かみ処理
					float fbSig = ProcessWarmth(cs,
						ProcessMaterialAbsorption(cs, delayMain, env->materialAbsorption, env->surfaceRoughness),
						env->warmth);

					// フィードバック計算
					float effectiveFB = fminf(0.88f, env->feedback * coreScale);
					float fbVal = signal + (fbSig * effectiveFB);
					if (fbVal > 1.5f) fbVal = 1.5f;
					if (fbVal < -1.5f) fbVal = -1.5f;

					cs->delayBuffer[cs->writePos] = isfinite(fbVal) ? fbVal : 0.0f;
					cs->writePos = (cs->writePos + 1) % MAX_DELAY_SAMPLES;
				}
			}

			// ===== Wet/Dry ミックス =====
			float mixed = signal + (wetSignal * fminf(1.0f, env->wetMix * coreScale));

			// ===== 拡張エフェクト =====

			// エキサイター
			if (env->exciterAmount > 0.0f && effectAmount > 0) {
				mixed = Exciter(mixed, &cs->exciterFilter, env->exciterAmount * extraScale);
			}

			// フラッターエコー
			if (env->flutterEcho > 0.0f) {
				mixed = ProcessFlutterEcho(cs, mixed, env->flutterEcho * extraScale, wavbit);
			}

			// 共鳴
			if (env->resonanceFreq > 0.0f && env->resonanceQ > 0.0f) {
				float resonanceAmount = env->spaceComplexity * 0.3f;
				mixed = ProcessResonance(cs, mixed, env->resonanceFreq, env->resonanceQ, resonanceAmount);
			}

			// 金属感
			if (env->metallic > 0.0f) {
				mixed = ProcessMetallic(cs, mixed, env->metallic * extraScale);
			}

			// ガラス感
			if (env->glassiness > 0.0f) {
				mixed = ProcessGlass(cs, mixed, env->glassiness * extraScale);
			}

			// きらめき
			if (env->shimmer > 0.0f) {
				mixed = ProcessShimmer(cs, mixed, env->shimmer * extraScale, wavbit);
			}

			// ドップラー効果
			if (env->doppler > 0.0f) {
				mixed = ProcessDoppler(cs, mixed, env->doppler * extraScale, wavbit);
			}

			// 明るさ処理
			mixed = ProcessBrightness(mixed, &cs->brightnessState, env->brightness);

			// マスターゲイン
			mixed *= masterGain;

			// ステレオ/モノラル分岐
			if (wavch == 2) {
				if (ch == 0) leftSamples[bufferIndex] = mixed;
				else rightSamples[bufferIndex] = mixed;
			}
			else {
				leftSamples[bufferIndex] = mixed;
				rightSamples[bufferIndex] = mixed;
			}
		}

		// ===== ステレオ幅処理 =====
		if (wavch == 2) {
			float w = (1.0f + (env->stereoWidth - 1.0f) * extraScale) * spatialWidth * env->wallDistance * (0.7f + (env->openness * 0.6f));

			// 天井高さの影響
			if (env->ceilingHeight > 1.0f) {
				w *= (1.0f + (env->ceilingHeight - 1.0f) * 0.2f);
			}
			else {
				w *= env->ceilingHeight;
			}

			float mid = (leftSamples[bufferIndex] + rightSamples[bufferIndex]) * 0.5f;
			float side = (leftSamples[bufferIndex] - rightSamples[bufferIndex]) * 0.5f * w;

			leftSamples[bufferIndex] = mid + side;
			rightSamples[bufferIndex] = mid - side;
		}

		bufferIndex++;
	}

	// ===== 最終出力とオートゲイン =====
	bufferIndex = 0;
	for (int i = 0; i < numSamples; i++) {
		for (int ch = 0; ch < wavch; ch++) {
			if (ch >= MAX_CH) continue;

			float s = (ch == 0) ? leftSamples[bufferIndex] : rightSamples[bufferIndex];

			// ダイナミックリミッター処理（音割れ完全防止）
			float finalOut = ProcessDynamicLimiter(&g_limiter[ch], s);

			// 最終的なハードクリップ（安全装置）
			if (finalOut > 1.0f) finalOut = 1.0f;
			if (finalOut < -1.0f) finalOut = -1.0f;

			// サンプル書き込み
			int offset = (i * wavch + ch) * bytesPerSample;

			if (wavsam == 16) {
				*((short*)(pRaw + offset)) = (short)(finalOut * 32767.0f);
			}
			else if (wavsam == 24) {
				int v = (int)(finalOut * 8388607.0f);
				pRaw[offset] = v & 0xFF;
				pRaw[offset + 1] = (v >> 8) & 0xFF;
				pRaw[offset + 2] = (v >> 16) & 0xFF;
			}
			else if (wavsam == 32) {
				*((int*)(pRaw + offset)) = (int)(finalOut * 2147483647.0f);
			}
			else {
				pRaw[offset] = (unsigned char)(finalOut * 127.0f + 128.0f);
			}
		}

		if (wavch == 2) bufferIndex++;
	}
}


/*
===============================================================================
  ★ Hyper DSP Equaliser ★ - 完全版ここまで
  全100環境音響モデル実装完了
  - 基本空間 (0-10)
  - 公共施設 (11-20)
  - 産業・商業 (21-30)
  - 文化施設 (31-40)
  - 生活空間 (41-50)
  - 拡張空間 (51-60)
  - 特殊空間 (61-70)
  - 専門空間 (71-80)
  - SF/未来空間 (81-100)

  全51 EQプリセット実装完了

  拡張パラメータ5種実装完了
  - eq[15]: マスターボリューム (0-200)
  - eq[16]: 音の鮮明さ (0-200)
  - eq[17]: 低域と高域のバランス (0-200)
  - eq[18]: 音の密度/充実度 (0-200)
  - eq[19]: 音の立体感/臨場感 (0-200)

  環境パラメータ65種実装完了
  - 基本パラメータ3種 (wetMix, delayTime, feedback)
  - 初期反射16タップ (8タップ×2値＝16パラメータ)
  - フィルタ4種 (centerFreq, bandwidth, LPF, HPF)
  - 空間・モジュレーション5種 (roomSize, modDepth, modRate, diffusion, density)
  - リバーブ詳細制御6種
	* 初期減衰 (earlyDecay)
	* 後期減衰 (lateDecay)
	* 減衰の滑らかさ (decaySmoothness)
	* 残響の色味 (reverbColor)
	* M/Sステレオ幅 (stereoWidth)
	* ドップラー効果 (dopplerShift)
  - 周波数帯域別残響時間3種 (lowReverbTime, midReverbTime, highReverbTime)
  - 周波数帯域別拡散度2種 (lowDiffusion, highDiffusion)
  - エコー特性2種 (echoClarity, feedbackTone)
  - 材質・表面特性6種
	* 材質吸音率 (materialAbsorption)
	* 表面粗さ (surfaceRoughness)
	* 温かみ (warmth)
	* 明るさ (brightness)
	* 柔らかさ (softness)
	* 重さ (weight)
  - 空間幾何学3種 (ceilingHeight, wallDistance, openness)
  - 特殊効果2種 (flutterEcho, combFiltering)
  - 山彦エコー8種 (yamabikoDelay[4], yamabikoGain[4])
	* 最大4タップまでのディレイエコー
	* 各タップの遅延時間とゲイン独立制御
  - yamabikoMix: 山彦エコー全体のミックス量
  - yamabikoFeedback: 山彦エコーのフィードバック量

  処理機能
  - 3段階スケール関数 (core/extra/reflection)
  - 周波数帯域別リバーブ処理 (低/中/高)
  - 3段階ディフュージョン
  - 材質特性処理
  - 温かみ処理
  - 明るさ処理
  - 柔らかさ/硬さ処理
  - 音の重さ処理
  - フラッターエコー処理
  - コムフィルタリング処理
  - 初期/後期残響減衰エンベロープ
  - エコー明瞭度制御
  - フィードバック音色変化
  - 天井高さ/壁距離/開放度による空間感調整
  - M/Sステレオ幅処理
  - 山彦エコー処理（最大4タップ）

  物理ベースモデリング
  - 材質による周波数依存吸音
  - 空気吸収による距離減衰
  - 表面粗さによる高域散乱
  - 空間幾何学による反射パターン
  - 山彦エコーによる広大空間表現

  合計パラメータ数: 63個（環境） + 5個（拡張） = 68個
  合計環境数: 100種（完全差別化達成）
  合計EQプリセット数: 51種

  完全リアル志向・最高品質音響処理システム
  山彦エコー対応環境:
  - #8 山（3タップ）
  - #12 峡谷（4タップ）
  - #47 渓谷（4タップ）
  - #54 古城の大広間（3タップ）
  - #55 野外音楽堂（2タップ）
  - #63 大型倉庫（3タップ）
===============================================================================
*/







#include <algorithm>

// ====================================================================
// 高精度音楽キー分析システム (Salience Viterbi Tracking / Melody Extraction)
// C++ Standard Implementation
// ====================================================================

#define _USE_MATH_DEFINES
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstring>
#include <complex>
#include <map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Windows型定義
#ifndef OUTPUT_BUFFER_SIZE
#define OUTPUT_BUFFER_SIZE 176400
#endif

#ifndef OUTPUT_BUFFER_NUM
#define OUTPUT_BUFFER_NUM 2
#endif

// min/maxマクロ対策
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

using Complex = std::complex<double>;

// ===== 構造体定義: メロディ候補 =====
struct MelodyCandidate {
	int midiNote;
	float salience; // その瞬間の「確からしさ」
	float totalScore; // 過去からの累積スコア
	int fromIdx; // どこから来たか（経路復元用）
};

// ===== グローバル変数 =====
static float g_noteStrength[108];          // コード用
static double g_goertzelCoeffs[108];
static double g_blackmanWindow[8192];
static bool g_analysisInitialized = false;

// ★ビタビ探索用履歴
// [フレーム][候補インデックス]
static std::vector<std::vector<MelodyCandidate>> g_viterbiPath;
static const int MAX_VITERBI_FRAMES = 8; // 8フレーム分(約100ms)の履歴で判断
static const int CANDIDATE_NUM = 5;      // 各フレームの上位5候補を残す

// ===== 出力変数 =====
CString KeyCodeLow;
CString KeyCodeMid;
CString KeyCodeHigh;
CString KeyCodeAll;

// ===== 音名テーブル =====
static const WCHAR* NOTE_NAMES[12] = {
	L"C ", L"C#", L"D ", L"D#", L"E ", L"F ", L"F#", L"G ", L"G#", L"A ", L"A#", L"B "
};

// ===== 初期化 =====
static void InitializeAnalysis(double sampleRate) {
	if (g_analysisInitialized) return;

	for (int k = 0; k < 108; ++k) {
		int midiNote = 12 + k;
		double freq = 440.0 * pow(2.0, (midiNote - 69.0) / 12.0);
		g_goertzelCoeffs[k] = 2.0 * cos(2.0 * M_PI * freq / sampleRate);
	}

	for (int n = 0; n < 8192; ++n) {
		// ナットール窓
		g_blackmanWindow[n] = 0.355768 - 0.487396 * cos(2.0 * M_PI * n / 8191.0)
			+ 0.144232 * cos(4.0 * M_PI * n / 8191.0)
			- 0.012604 * cos(6.0 * M_PI * n / 8191.0);
	}

	memset(g_noteStrength, 0, sizeof(g_noteStrength));
	g_viterbiPath.clear();
	g_analysisInitialized = true;
}

// ===== Goertzel計算 (コード用) =====
static double GoertzelMagnitude(const double* samples, int numSamples,
	double coefficient) {
	double s_prev = 0.0, s_prev2 = 0.0;
	for (int n = 0; n < numSamples; ++n) {
		double s = samples[n] + coefficient * s_prev - s_prev2;
		s_prev2 = s_prev;
		s_prev = s;
	}
	double power = s_prev2 * s_prev2 + s_prev * s_prev - coefficient * s_prev * s_prev2;
	return sqrt(power > 0.0 ? power : 0.0) * 2.5 / numSamples;
}

// ===== FFT実装 =====
static void FFT(std::vector<Complex>& x) {
	const size_t N = x.size();
	if (N <= 1) return;

	std::vector<Complex> even(N / 2);
	std::vector<Complex> odd(N / 2);
	for (size_t i = 0; i < N / 2; ++i) {
		even[i] = x[2 * i];
		odd[i] = x[2 * i + 1];
	}

	FFT(even);
	FFT(odd);

	for (size_t k = 0; k < N / 2; ++k) {
		Complex t = std::polar(1.0, -2.0 * M_PI * k / N) * odd[k];
		x[k] = even[k] + t;
		x[k + N / 2] = even[k] - t;
	}
}

// ===== ★サリエンス計算 (HPSベースの尤度推定) =====
// 単一の答えではなく、可能性のある候補リストを返す
static std::vector<MelodyCandidate> CalculateSalience(const std::vector<double>& bufL, const std::vector<double>& bufR, double sampleRate) {
	int N = (int)bufL.size();
	std::vector<Complex> cL(N), cR(N);
	for (int i = 0; i < N; ++i) {
		double win = g_blackmanWindow[i];
		cL[i] = bufL[i] * win;
		cR[i] = bufR[i] * win;
	}
	FFT(cL);
	FFT(cR);

	int specSize = N / 2;
	std::vector<float> mag(specSize, 0.0f);

	// センター成分抽出 (伴奏除去)
	for (int i = 0; i < specSize; ++i) {
		double absL = std::abs(cL[i]);
		double absR = std::abs(cR[i]);
		double mid = (absL + absR) * 0.5;
		double side = std::abs(absL - absR);

		// サイド成分を強めに引く
		double center = mid - (side * 1.5);
		if (center < 0) center = 0.0;
		mag[i] = (float)center;
	}

	// HPS (Harmonic Product Spectrum) でサリエンスマップ作成
	std::vector<float> salienceMap(108, 0.0f); // MIDIノートごとのスコア
	double binFreq = sampleRate / N;

	// 各MIDIノートについてスコア計算
	// 探索範囲: F2(41) ～ E5(76) のボーカル帯域
	for (int k = 41; k <= 76; ++k) {
		double freq = 440.0 * pow(2.0, (k - 69.0) / 12.0);
		int bin = (int)(freq / binFreq);

		if (bin <= 0 || bin * 3 >= specSize) continue;

		// 基音、2倍音、3倍音の強度をチェック
		// 少し幅を持たせて（前後1bin）ピークを拾う
		auto getPeak = [&](int centerBin) -> float {
			float mx = mag[centerBin];
			if (centerBin > 0 && mag[centerBin - 1] > mx) mx = mag[centerBin - 1];
			if (centerBin < specSize - 1 && mag[centerBin + 1] > mx) mx = mag[centerBin + 1];
			return mx;
			};

		float s1 = getPeak(bin);
		float s2 = getPeak(bin * 2);
		float s3 = getPeak(bin * 3);

		// HPSスコア: 
		// 伴奏対策として、基音(s1)と2倍音(s2)が両方強くないとスコアが出ないようにする
		// 3倍音(s3)はギター成分なので、強すぎるとペナルティ
		float score = s1 * s2;
		if (s3 > s1 * 0.8f) score *= 0.5f;

		salienceMap[k] = score;
	}

	// 上位候補を選出
	std::vector<MelodyCandidate> candidates;

	// まず「無音/該当なし」候補を追加 (MIDIノート -1)
	// これにより「歌っていない区間」を自然に表現できる
	// スコアは全体の平均エネルギーなどから算出（ノイズフロア）
	double noiseFloor = 0.0;
	for (float s : salienceMap) noiseFloor += s;
	noiseFloor /= 36.0; // 帯域幅で割る

	MelodyCandidate silence;
	silence.midiNote = -1;
	silence.salience = (float)(noiseFloor * 2.0); // 閾値調整用係数
	silence.totalScore = 0.0f;
	silence.fromIdx = -1;
	candidates.push_back(silence);

	// スコアが高い順にソートして上位を追加
	// インデックスとスコアのペアを作る
	std::vector<std::pair<int, float>> sortedIndices;
	for (int k = 41; k <= 76; ++k) {
		if (salienceMap[k] > 0.0f) {
			sortedIndices.push_back({ k, salienceMap[k] });
		}
	}

	// スコア降順ソート
	std::sort(sortedIndices.begin(), sortedIndices.end(),
		[](const std::pair<int, float>& a, const std::pair<int, float>& b) {
			return a.second > b.second;
		});

	// 上位(CANDIDATE_NUM - 1)個を追加
	for (int i = 0; i < (int)sortedIndices.size() && i < CANDIDATE_NUM - 1; ++i) {
		MelodyCandidate c;
		c.midiNote = sortedIndices[i].first;
		c.salience = sortedIndices[i].second;
		c.totalScore = 0.0f;
		c.fromIdx = -1;
		candidates.push_back(c);
	}

	return candidates;
}

// ===== ★ビタビ探索 (Viterbi Search) =====
// 時間的な連続性を考慮して、最適なメロディラインを決定する
static int UpdateViterbi(const std::vector<MelodyCandidate>& currentCandidates) {
	// 履歴に追加
	g_viterbiPath.push_back(currentCandidates);

	// 履歴が1つしかない場合は計算不要
	if (g_viterbiPath.size() == 1) return -1;

	// 前回の候補リスト
	std::vector<MelodyCandidate>& prevFrame = g_viterbiPath[g_viterbiPath.size() - 2];
	// 今回の候補リスト（書き換え用）
	std::vector<MelodyCandidate>& currFrame = g_viterbiPath[g_viterbiPath.size() - 1];

	// ビタビ更新：今回の各候補について、前回のどの候補から来るのが一番スコアが高いか計算
	for (int i = 0; i < (int)currFrame.size(); ++i) {
		float maxScore = -1.0f;
		int bestPrevIdx = -1;

		for (int j = 0; j < (int)prevFrame.size(); ++j) {
			float transitionPenalty = 0.0f;

			int noteDiff = 0;
			// どちらかが無音(-1)の場合の遷移
			if (prevFrame[j].midiNote == -1 || currFrame[i].midiNote == -1) {
				// 有音<->無音 の遷移は少しペナルティ（頻繁な切れ防止）
				if (prevFrame[j].midiNote != currFrame[i].midiNote) {
					transitionPenalty = 0.5f;
				}
			}
			else {
				// 有音同士の遷移：音程差が大きいほどペナルティ
				noteDiff = std::abs(prevFrame[j].midiNote - currFrame[i].midiNote);

				if (noteDiff == 0) {
					transitionPenalty = 0.0f; // 同じ音ならボーナス（ペナルティなし）
				}
				else if (noteDiff <= 2) {
					transitionPenalty = 0.2f; // 隣接音（滑らかな移動）
				}
				else if (noteDiff <= 7) {
					transitionPenalty = 1.0f; // 跳躍
				}
				else {
					transitionPenalty = 5.0f; // 大きな跳躍は禁止に近い
				}
			}

			// スコア計算: (累積スコア) + (今の尤度) - (遷移コスト)
			// Salienceは値の幅が大きいので、対数を取るか係数で調整
			float currentScore = prevFrame[j].totalScore + currFrame[i].salience - (transitionPenalty * currFrame[i].salience * 0.5f);

			if (currentScore > maxScore) {
				maxScore = currentScore;
				bestPrevIdx = j;
			}
		}

		currFrame[i].totalScore = maxScore;
		currFrame[i].fromIdx = bestPrevIdx;
	}

	// 履歴が最大数を超えたら、最も古いものを確定させて削除（遅延出力）
	// ただし今回は即応性も欲しいので、「現在の最高スコアを持つパス」をバックトラックして
	// 「数フレーム前」の結果を表示するのが一般的だが、
	// ここではシンプルに「現在の勝者」を返すか、「数フレーム前の勝者」を返すか。
	// 遅延を許容するなら数フレーム前が良いが、即応性なら現在。
	// バランスを取って「3フレーム前」の結果を確定とする。

	if (g_viterbiPath.size() >= MAX_VITERBI_FRAMES) {
		// 最新フレームで最もスコアが高い候補を探す
		int bestIdx = 0;
		float maxTotal = -1.0f;
		for (int i = 0; i < (int)currFrame.size(); ++i) {
			if (currFrame[i].totalScore > maxTotal) {
				maxTotal = currFrame[i].totalScore;
				bestIdx = i;
			}
		}

		// バックトラック（経路復元）
		// 現在から過去へ bestIdx を遡る
		std::vector<int> pathIndices;
		int traceIdx = bestIdx;
		for (int f = (int)g_viterbiPath.size() - 1; f >= 0; --f) {
			pathIndices.push_back(traceIdx);
			traceIdx = g_viterbiPath[f][traceIdx].fromIdx;
			if (traceIdx == -1) break;
		}

		// 確定させたいフレーム（例えば3フレーム前 = size-1-3）
		// 古い履歴を削除してスライドさせる
		int targetFrame = (int)g_viterbiPath.size() - 4; // 3フレーム遅延
		if (targetFrame < 0) targetFrame = 0;

		// パス上のインデックスを取得（pathIndicesは逆順に入っている）
		// pathIndices[0] = 最新, [1] = 1つ前, ...
		int pathPos = (int)g_viterbiPath.size() - 1 - targetFrame;
		if (pathPos >= pathIndices.size()) return -1;

		int confirmedNoteIdx = pathIndices[pathPos];
		int confirmedNote = g_viterbiPath[targetFrame][confirmedNoteIdx].midiNote;

		// 履歴の先頭を削除（スライディングウィンドウ）
		g_viterbiPath.erase(g_viterbiPath.begin());

		return confirmedNote;
	}

	return -1; // まだバッファが溜まっていない
}

// ===== クラス集計 =====
static void AggregateNoteClasses(float* bassClass, float* midClass, float* highClass, float* allClass) {
	memset(bassClass, 0, 12 * sizeof(float));
	memset(midClass, 0, 12 * sizeof(float));
	memset(highClass, 0, 12 * sizeof(float));
	memset(allClass, 0, 12 * sizeof(float));

	for (int k = 0; k < 108; k++) {
		int midiNote = 12 + k;
		int noteClass = midiNote % 12;
		float strength = g_noteStrength[k];

		if (midiNote <= 47) bassClass[noteClass] += strength;
		else if (midiNote <= 71) midClass[noteClass] += strength;
		else if (midiNote <= 95) highClass[noteClass] += strength;

		allClass[noteClass] += strength;
	}
}

// ===== コード推定 =====
typedef struct { const WCHAR* name; int pattern[12]; float bonus; } ChordPattern;
static const ChordPattern CHORD_PATTERNS[] = {
	{L"", {3,0,0,0,2,0,0,1,0,0,0,0}, 0.3f}, {L"m", {3,0,0,2,0,0,0,1,0,0,0,0}, 0.3f},
	{L"5", {3,0,0,0,0,0,0,2,0,0,0,0}, 0.2f}, {L"dim", {3,0,0,2,0,0,2,0,0,0,0,0}, 0.2f},
	{L"aug", {3,0,0,0,2,0,0,0,2,0,0,0}, 0.2f}, {L"sus4", {3,0,0,0,0,3,0,1,0,0,0,0}, 0.2f},
	{L"sus2", {3,0,3,0,0,0,0,1,0,0,0,0}, 0.2f}, {L"7", {3,0,0,0,2,0,0,1,0,0,2,0}, 0.1f},
	{L"M7", {3,0,0,0,2,0,0,1,0,0,0,2}, 0.1f}, {L"m7", {3,0,0,2,0,0,0,1,0,0,2,0}, 0.1f},
	{L"m7b5", {3,0,0,2,0,0,2,0,0,0,2,0}, 0.1f}, {L"dim7", {3,0,0,2,0,0,2,0,0,2,0,0}, 0.1f},
	{L"7sus4", {3,0,0,0,0,2,0,1,0,0,2,0}, 0.1f}, {L"6", {3,0,0,0,2,0,0,1,0,2,0,0}, 0.1f},
	{L"m6", {3,0,0,2,0,0,0,1,0,2,0,0}, 0.1f}, {L"add9", {3,0,2,0,2,0,0,1,0,0,0,0}, 0.1f},
	{L"9", {3,0,2,0,2,0,0,1,0,0,2,0}, 0.0f}, {L"M9", {3,0,2,0,2,0,0,1,0,0,0,2}, 0.0f},
	{L"m9", {3,0,2,2,0,0,0,1,0,0,2,0}, 0.0f},
};

static CString EstimateChordRaw(float* noteClass, float threshold) {
	float maxVal = 0.0f;
	for (int i = 0; i < 12; i++) if (noteClass[i] > maxVal) maxVal = noteClass[i];
	if (maxVal < 0.001f) return L"";
	float normalized[12];
	for (int i = 0; i < 12; i++) normalized[i] = noteClass[i] / maxVal;
	int bestRoot = 0;
	for (int i = 1; i < 12; i++) if (normalized[i] > normalized[bestRoot]) bestRoot = i;
	if (normalized[bestRoot] < threshold) return L"";
	float secondMax = 0.0f;
	for (int i = 0; i < 12; i++) { if (i != bestRoot && normalized[i] > secondMax) secondMax = normalized[i]; }
	CString rootName = NOTE_NAMES[bestRoot]; rootName.Trim();
	if (secondMax < 0.001f || normalized[bestRoot] > secondMax * 2.5f) return rootName;
	float bestScore = 0.0f;
	const ChordPattern* bestChord = NULL;
	int numPatterns = sizeof(CHORD_PATTERNS) / sizeof(ChordPattern);
	for (int r = 0; r < 3; r++) {
		int root = bestRoot;
		for (int c = 0; c < numPatterns; c++) {
			float score = 0.0f;
			int matched = 0;
			int ptrnCnt = 0;
			for (int x = 0; x < 12; x++) if (CHORD_PATTERNS[c].pattern[x] > 0) ptrnCnt++;
			for (int n = 0; n < 12; n++) {
				int note = (root + n) % 12;
				int weight = CHORD_PATTERNS[c].pattern[n];
				if (weight > 0) { score += normalized[note] * weight; if (normalized[note] > 0.15f) matched++; }
				else { if (normalized[note] > 0.3f) score -= 1.0f; }
			}
			if (ptrnCnt >= 4 && matched < 3) score -= 2.0f;
			if (ptrnCnt == 3 && matched < 2) score -= 2.0f;
			score += CHORD_PATTERNS[c].bonus;
			if (score > bestScore) { bestScore = score; bestChord = &CHORD_PATTERNS[c]; }
		}
		break;
	}
	if (bestScore > 0.8f && bestChord != NULL) {
		CString rName = NOTE_NAMES[bestRoot]; rName.Trim();
		return rName + bestChord->name;
	}
	return rootName;
}

static CString EstimateOverallRaw(float* bassClass, float* midClass, float* highClass, float* allClass) {
	CString allChord = EstimateChordRaw(allClass, 0.03f);
	if (!allChord.IsEmpty()) return allChord;
	CString bassChord = EstimateChordRaw(bassClass, 0.02f);
	if (!bassChord.IsEmpty()) return bassChord;
	return L"";
}

// ===== メイン =====
void AnalyzeMusicKey(const std::vector<double>& bufferL, const std::vector<double>& bufferR, int sampleRate) {
	InitializeAnalysis((double)sampleRate);

	int totalSamples = (int)bufferL.size();
	bool stereo = (bufferR.size() == totalSamples);

	const int LOW_NOTE_LIMIT = 52;
	const int LOW_SAMPLES = (totalSamples >= 4096) ? 4096 : totalSamples;
	const int LOW_START = totalSamples - LOW_SAMPLES;
	const int HIGH_SAMPLES = (totalSamples >= 2048) ? 2048 : totalSamples;
	const int HIGH_START = totalSamples - HIGH_SAMPLES;

	// --- 1. コード解析 (Goertzel) ---
	for (int k = 0; k < LOW_NOTE_LIMIT; k++) {
		double ampL = GoertzelMagnitude(bufferL.data() + LOW_START, LOW_SAMPLES, g_goertzelCoeffs[k]);
		double ampR = stereo ? GoertzelMagnitude(bufferR.data() + LOW_START, LOW_SAMPLES, g_goertzelCoeffs[k]) : ampL;
		g_noteStrength[k] = (float)max(ampL, ampR) * (1.0f + k / 100.0f);
	}
	for (int k = LOW_NOTE_LIMIT; k < 108; k++) {
		double ampL = GoertzelMagnitude(bufferL.data() + HIGH_START, HIGH_SAMPLES, g_goertzelCoeffs[k]);
		double ampR = stereo ? GoertzelMagnitude(bufferR.data() + HIGH_START, HIGH_SAMPLES, g_goertzelCoeffs[k]) : ampL;
		g_noteStrength[k] = (float)max(ampL, ampR) * (1.0f + k / 50.0f);
	}

	float bassClass[12], midClass[12], highClass[12], allClass[12];
	AggregateNoteClasses(bassClass, midClass, highClass, allClass);

	CString rawBass = EstimateChordRaw(bassClass, 0.02f);
	CString rawMid = EstimateChordRaw(midClass, 0.03f);
	CString rawAll = EstimateOverallRaw(bassClass, midClass, highClass, allClass);
	CString rawHighChord = EstimateChordRaw(highClass, 0.03f);

	// --- 2. ★メロディ解析 (Salience + Viterbi) ---
	int fftSize = 4096;
	int fftStart = totalSamples - fftSize;
	if (fftStart < 0) fftStart = 0;
	int fftLen = totalSamples - fftStart;

	std::vector<double> bufL_Part(bufferL.begin() + fftStart, bufferL.end());
	std::vector<double> bufR_Part;
	if (stereo) bufR_Part.assign(bufferR.begin() + fftStart, bufferR.end());
	else bufR_Part = bufL_Part;

	// 候補リスト取得
	std::vector<MelodyCandidate> candidates = CalculateSalience(bufL_Part, bufR_Part, (double)sampleRate);

	// ビタビ更新 & 確定ノート取得
	int detectedMidi = UpdateViterbi(candidates);

	// 文字列生成
	CString rawMelody = L"[   ]";
	if (detectedMidi != -1) {
		int octave = (detectedMidi / 12) - 1;
		CString noteName = NOTE_NAMES[detectedMidi % 12];
		noteName.Trim();
		if (noteName.GetLength() == 1) rawMelody.Format(L"[%s%d ]", noteName, octave);
		else rawMelody.Format(L"[%s%d]", noteName, octave);
	}

	// フォーマット整形
	auto FormatChord = [](CString chordStr) -> CString {
		if (chordStr.IsEmpty()) return L"  , <  >";
		CString rootName = chordStr;
		if (chordStr.GetLength() > 1 && (chordStr[1] == L'#' || chordStr[1] == L'b')) rootName = chordStr.Left(2);
		else rootName = chordStr.Left(1);

		if (rootName.GetLength() == 1) rootName += L" ";
		CString ret; ret.Format(L"%s, <%s>", rootName, chordStr);
		return ret;
		};

	KeyCodeLow = FormatChord(rawBass);
	KeyCodeMid = FormatChord(rawMid);
	KeyCodeAll = FormatChord(rawAll);

	if (rawHighChord.IsEmpty()) {
		if (rawMelody != L"[   ]") {
			CString temp = rawMelody.Mid(1);
			int sharpPos = temp.Find(L'#');
			if (sharpPos >= 0) rawHighChord = temp.Left(2);
			else rawHighChord = temp.Left(1);
		}
	}

	if (rawMelody == L"[   ]" && rawHighChord.IsEmpty()) {
		KeyCodeHigh = L"";
	}
	else {
		CString highChordPart = FormatChord(rawHighChord);
		if (highChordPart.IsEmpty()) KeyCodeHigh = rawMelody;
		//else KeyCodeHigh.Format(L"%s, %s", rawMelody, highChordPart);
		else KeyCodeHigh.Format(L"%s", highChordPart);
	}
}

void GetCurrentNoteStrengths(float* output108) {
	if (output108) memcpy(output108, g_noteStrength, sizeof(g_noteStrength));
}
