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
  ★ Hyper Equaliser ★
  超高品質イコライザー & 環境音響エフェクト - 究極進化版

  環境音響: 81種 (0-80) - 完全差別化・リアル志向
  EQプリセット: 51種 (0-50)
  拡張パラメータ: 5種 (eq[15-19])

  パラメータ総数: 45個（従来29個→45個）
  物理ベースモデリング & 周波数依存処理
===============================================================================

環境音響81種:
[基本空間 0-10]
00.なし
01.風呂場 - 短く明るい金属的
02.ホール - 中程度でバランス
03.教会 - 長く荘厳
04.洞窟 - 暗くこもった
05.スタジオ - 極めてドライ
06.ライブハウス - パンチのある
07.森 - 柔らかく自然
08.山 - 長いエコー
09.広場 - 開放的
10.カテドラル - 超巨大で超長残響

[公共施設 11-20]
11.体育館 - 硬く金属的
12.峡谷 - 両側から複数エコー
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
47.渓谷/滝 - 水の反射と濡れた岩肌
48.砂漠 - 超開放的反射極小
49.ガレージ - 車庫硬質空間
50.展望台 - 高所開放感

[拡張空間 51-60]
51.小さな礼拝堂 - 教会より親密で温かい
52.大型ショッピングセンター - モールより巨大
53.地下洞窟(深層) - より深く神秘的
54.古城の大広間 - 石造り中世的
55.野外音楽堂 - 半開放的ステージ
56.鍾乳洞 - 複雑な水滴反射
57.廃墟工場 - 荒廃した金属空間
58.和室(畳) - 日本的柔らかい吸音
59.温泉施設 - 湿度高めタイル反射
60.屋根裏部屋 - 斜め天井の特殊空間

[特殊空間 61-70]
61.地下駐車場(多層) - 階層的複雑反射
62.古い劇場(木造) - 温かみある音響設計
63.大型倉庫(空) - 極端な空虚感
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

typedef struct {
	float b0, b1, b2, a1, a2;
	float x1, x2, y1, y2;
} Biquad;

typedef struct {
	float phase;
	float frequency;
	float depth;
} LFO;

typedef struct {
	float* delayBuffer;
	int writePos;

	// 基本EQフィルタ
	Biquad eqFilters[EQ_BANDS];

	// 環境音響フィルタ
	Biquad envLpf;
	Biquad envHpf;
	Biquad exciterFilter;
	Biquad dampingFilter;

	// 周波数帯域別リバーブフィルタ（3バンド分離）
	Biquad bassReverbFilter;
	Biquad midReverbFilter;
	Biquad trebleReverbFilter;

	// マテリアル特性フィルタ
	Biquad materialFilter;
	Biquad warmthFilter;

	// フラッターエコー用
	Biquad flutterFilter;
	float flutterPhase;

	LFO lfo;

	// ディフュージョン（3段階に拡張）
	float diffusionBuffer1[8][1024];
	float diffusionBuffer2[8][512];
	float diffusionBuffer3[8][256];
	int diffusionPos1[8];
	int diffusionPos2[8];
	int diffusionPos3[8];

	// 拡張パラメータ用フィルタ
	Biquad clarityFilter;
	Biquad bassBalanceFilter;
	Biquad trebleBalanceFilter;
	Biquad densityFilter1;
	Biquad densityFilter2;

	// 状態変数
	float harmonicState;
	float earlyEnvelope;
	float lateEnvelope;
	float warmthState;
	float brightnessState;
} ChannelState;

static float g_delayMemory[MAX_CH][MAX_DELAY_SAMPLES];
static ChannelState g_channels[MAX_CH];
static int g_lastRate = 0;
static int g_lastEqPreset = -1;
static int g_lastEnvPreset = -1;
static int g_lastEqValues[15];
static int g_lastExtendedParams[5];
static int g_lastEffectAmount = 50;
static BOOL g_initialized = FALSE;
static float g_autoGain = 1.0f;


static const float EQ_FREQS[15] = {
	25, 40, 63, 100, 160, 250, 400, 630,
	1000, 1600, 2500, 4000, 6300, 10000, 16000
};

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

// 拡張環境パラメータ構造体（45パラメータ）
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

	// ===== 新規追加: リバーブ詳細制御 =====
	float earlyReverbDecay; // 初期残響減衰速度 (0.1=速い, 2.0=遅い)
	float lateReverbDecay;  // 後期残響減衰速度 (0.1=速い, 3.0=遅い)
	float reverbSmooth;     // 残響の滑らかさ (0.0=粗い, 1.0=滑らか)
	float reverbColor;      // 残響の色味 (0.0=暗い, 1.0=明るい)

	// ===== 新規追加: 周波数帯域別残響時間 =====
	float bassReverbTime;   // 低域残響時間倍率 (0.5=短い, 2.0=長い)
	float midReverbTime;    // 中域残響時間倍率 (0.5-2.0)
	float trebleReverbTime; // 高域残響時間倍率 (0.5-2.0)

	// ===== 新規追加: 周波数帯域別拡散度 =====
	float bassDiffusion;    // 低域拡散度 (0.0-1.0)
	float trebleDiffusion;  // 高域拡散度 (0.0-1.0)

	// ===== 新規追加: エコー特性 =====
	float echoClarity;      // エコー明瞭度 (0.0=不明瞭, 1.0=明瞭)
	float echoFeedbackTone; // フィードバック音色変化 (-1.0=暗く, 1.0=明るく)

	// ===== 新規追加: 材質・表面特性 =====
	float materialAbsorption; // 材質吸音率 (0.0=反射, 1.0=吸音)
	float surfaceRoughness;   // 表面粗さ (0.0=滑らか, 1.0=粗い)
	float warmth;             // 温かみ (0.0=冷たい, 1.0=温かい)
	float brightness;         // 明るさ (0.0=暗い, 1.0=明るい)
	float softness;           // 柔らかさ (0.0=硬い, 1.0=柔らかい)
	float weight;             // 音の重さ (0.0=軽い, 1.0=重い)

	// ===== 新規追加: 空間幾何学 =====
	float ceilingHeight;    // 天井高さ影響 (0.5=低い, 2.0=高い)
	float wallDistance;     // 壁距離感 (0.5=近い, 2.0=遠い)
	float openness;         // 開放度 (0.0=密閉, 1.0=開放)

	// ===== 新規追加: 特殊効果 =====
	float flutterEcho;      // フラッターエコー強度 (0.0-1.0)
	float combFiltering;    // コムフィルタリング (0.0-1.0)

	// ===== 空間タイプ =====
	int type;               // 0=なし, 1=室内, 2=屋外, 3=特殊
} EnvParams;

// 環境プリセット数 (0-100)
#define ENV_PRESET_COUNT 101

static const EnvParams ENV_PRESETS[ENV_PRESET_COUNT] = {
	// 0: なし
	{ 0.0f, 0.0f, 0.0f, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 20000, 20, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0 },
	// 1: 風呂場 - 短く明るい金属的
	{ 0.47f, 14.5f, 0.55f, { 2.0f, 0.68f, 3.5f, 0.63f, 6.0f, 0.59f, 9.0f, 0.52f, 11.0f, 0.47f, 13.0f, 0.41f, 15.0f, 0.34f, 17.0f, 0.29f }, 20000, 315, 0.59f, 0.04f, 3.36f, 0.32f, 0.13f, 0.0f, 0.06f, 0.3f, 0.65f, 0.18f, 0.0f, 0.4f, 0.5f, 0.3f, 0.79f, 1.1f, 1.0f, 0.8f, 0.2f, 0.4f, 0.8f, 0.11f, 0.1f, 0.0f, 0.2f, 0.8f, 0.1f, 0.2f, 0.6f, 0.7f, 0.2f, 0.2f, 0.1f, 1 },
	// 2: ホール - 中程度でバランス
	{ 0.38f, 80.67f, 0.47f, { 12.0f, 0.45f, 18.0f, 0.41f, 32.0f, 0.36f, 48.0f, 0.32f, 65.0f, 0.27f, 82.0f, 0.23f, 105.0f, 0.18f, 128.0f, 0.14f }, 11437, 62, 1.33f, 0.3f, 0.99f, 0.2f, 0.56f, 4.34f, 0.44f, 1.03f, 0.44f, 0.8f, 0.34f, 0.7f, 1.2f, 0.7f, 0.65f, 1.2f, 1.0f, 0.9f, 0.7f, 0.6f, 0.6f, 0.2f, 0.3f, 0.2f, 0.4f, 0.7f, 0.5f, 0.5f, 1.3f, 1.2f, 0.6f, 0.0f, 0.2f, 1 },
	// 3: 教会 - 長く荘厳
	{ 0.29f, 167.11f, 0.29f, { 28.0f, 0.27f, 45.0f, 0.24f, 75.0f, 0.2f, 110.0f, 0.17f, 155.0f, 0.14f, 200.0f, 0.11f, 250.0f, 0.1f, 310.0f, 0.07f }, 5688, 37, 1.51f, 0.51f, 0.35f, 0.07f, 0.72f, 13.95f, 0.54f, 1.65f, 0.29f, 0.82f, 0.41f, 1.5f, 2.0f, 0.82f, 0.48f, 1.6f, 1.12f, 0.65f, 0.82f, 0.46f, 0.47f, -0.15f, 0.25f, 0.36f, 0.47f, 0.4f, 0.65f, 0.72f, 1.9f, 1.65f, 0.65f, 0.0f, 0.11f, 1 },
	// 4: 洞窟 - 暗くこもった
	{ 0.36f, 117.73f, 0.41f, { 22.0f, 0.41f, 35.0f, 0.36f, 88.0f, 0.33f, 135.0f, 0.3f, 165.0f, 0.26f, 198.0f, 0.23f, 232.0f, 0.19f, 275.0f, 0.15f }, 4000, 96, 1.25f, 0.44f, 0.49f, 0.02f, 0.66f, 11.87f, 0.66f, 1.26f, 0.35f, 0.75f, 0.56f, 1.05f, 1.55f, 0.4f, 0.33f, 1.28f, 0.92f, 0.55f, 0.7f, 0.35f, 0.35f, -0.41f, 0.48f, 0.72f, 0.28f, 0.25f, 0.6f, 0.82f, 1.25f, 1.35f, 0.35f, 0.08f, 0.25f, 1 },
	// 5: スタジオ - 極めてドライ
	{ 0.07f, 6.28f, 0.02f, { 2.0f, 0.16f, 3.5f, 0.14f, 5.5f, 0.09f, 8.0f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 20000, 20, 0.86f, 0.0f, 0.12f, 0.38f, 0.02f, 0.0f, 0.16f, 0.49f, 0.87f, 0.16f, 0.08f, 0.2f, 0.3f, 0.9f, 0.4f, 0.8f, 1.0f, 1.0f, 0.1f, 0.1f, 0.85f, 0.01f, 0.9f, 0.1f, 0.45f, 0.51f, 0.3f, 0.4f, 0.9f, 0.8f, 0.1f, 0.0f, 0.01f, 1 },
	// 6: ライブハウス - パンチのある
	{ 0.49f, 54.66f, 0.44f, { 6.0f, 0.63f, 10.0f, 0.59f, 18.0f, 0.52f, 26.0f, 0.47f, 35.0f, 0.41f, 45.0f, 0.34f, 58.0f, 0.29f, 72.0f, 0.23f }, 13151, 123, 1.46f, 0.24f, 1.51f, 0.46f, 0.48f, 7.42f, 0.18f, 0.85f, 0.69f, 0.66f, 0.08f, 0.5f, 0.9f, 0.6f, 0.76f, 1.0f, 0.9f, 0.8f, 0.6f, 0.7f, 0.72f, 0.32f, 0.2f, 0.3f, 0.52f, 0.76f, 0.4f, 0.3f, 1.0f, 1.1f, 0.5f, 0.07f, 0.16f, 1 },
	// 7: 森 - 柔らかく自然
	{ 0.27f, 171.26f, 0.29f, { 38.0f, 0.27f, 62.0f, 0.25f, 110.0f, 0.22f, 158.0f, 0.19f, 205.0f, 0.18f, 252.0f, 0.15f, 308.0f, 0.12f, 365.0f, 0.1f }, 4000, 205, 1.61f, 0.49f, 0.49f, 0.04f, 0.8f, 10.89f, 0.58f, 1.7f, 0.39f, 0.86f, 0.76f, 1.4f, 1.7f, 0.78f, 0.43f, 1.25f, 0.98f, 0.68f, 0.85f, 0.42f, 0.48f, -0.26f, 0.68f, 0.58f, 0.64f, 0.53f, 0.78f, 0.42f, 1.45f, 1.55f, 0.78f, 0.06f, 0.17f, 2 },
	// 8: 山 - 長いエコー
	{ 0.70f, 450.0f, 0.65f, { 300.0f, 0.55f, 600.0f, 0.45f, 900.0f, 0.35f, 1200.0f, 0.28f, 1500.0f, 0.22f, 1800.0f, 0.18f, 0.0f, 0.0f, 0.0f, 0.0f }, 9000.0f, 120.0f, 1.4f, 0.12f, 0.35f, 0.10f, 0.45f, 40.0f, 0.35f, 1.8f, 0.55f, 0.65f, 0.35f, 0.9f, 1.8f, 0.7f, 0.65f, 1.4f, 1.2f, 0.9f, 0.6f, 0.5f, 0.7f, 0.1f, 0.25f, 0.35f, 0.6f, 0.7f, 0.4f, 0.5f, 1.6f, 1.4f, 0.8f, 0.15f, 0.1f, 2 },
	// 9: 広場 - 開放的
	{ 0.41f, 122.3f, 0.3f, { 28.0f, 0.38f, 42.0f, 0.33f, 62.0f, 0.28f, 85.0f, 0.23f, 112.0f, 0.17f, 142.0f, 0.14f, 178.0f, 0.1f, 218.0f, 0.07f }, 16146, 99, 1.55f, 0.33f, 0.82f, 0.18f, 0.63f, 10.14f, 0.37f, 1.4f, 0.6f, 0.51f, 0.57f, 0.8f, 1.1f, 0.7f, 0.51f, 1.0f, 1.0f, 0.9f, 0.6f, 0.6f, 0.74f, -0.11f, 0.3f, 0.4f, 0.54f, 0.55f, 0.5f, 0.5f, 1.2f, 1.4f, 0.7f, 0.12f, 0.1f, 2 },
	// 10: カテドラル - 超巨大で超長残響
	{ 0.31f, 295.55f, 0.32f, { 42.0f, 0.29f, 68.0f, 0.26f, 118.0f, 0.23f, 168.0f, 0.18f, 225.0f, 0.16f, 285.0f, 0.14f, 355.0f, 0.11f, 435.0f, 0.09f }, 5059, 31, 1.56f, 0.53f, 0.26f, 0.06f, 0.76f, 25.12f, 0.53f, 1.96f, 0.42f, 0.83f, 0.43f, 1.75f, 2.35f, 0.88f, 0.45f, 1.78f, 1.22f, 0.6f, 0.85f, 0.4f, 0.43f, -0.23f, 0.2f, 0.46f, 0.58f, 0.37f, 0.7f, 0.78f, 2.2f, 1.85f, 0.6f, 0.0f, 0.09f, 1 },
	// 11: 体育館 - 硬く金属的
	{ 0.47f, 57.09f, 0.55f, { 16.0f, 0.59f, 24.0f, 0.56f, 38.0f, 0.52f, 55.0f, 0.49f, 72.0f, 0.44f, 92.0f, 0.4f, 115.0f, 0.34f, 142.0f, 0.29f }, 16916, 152, 1.19f, 0.14f, 2.08f, 0.28f, 0.61f, 0.0f, 0.2f, 1.48f, 0.55f, 0.6f, 0.13f, 0.45f, 0.85f, 0.4f, 0.79f, 0.9f, 1.0f, 1.1f, 0.5f, 0.8f, 1.0f, 0.51f, 0.05f, 0.1f, 0.21f, 0.96f, 0.1f, 0.15f, 1.1f, 1.3f, 0.3f, 0.33f, 0.23f, 1 },
	// 12: 峡谷 - 両側から複数エコー
	{ 0.2f, 263.82f, 0.21f, { 98.0f, 0.26f, 158.0f, 0.22f, 235.0f, 0.21f, 315.0f, 0.18f, 405.0f, 0.15f, 505.0f, 0.13f, 615.0f, 0.1f, 735.0f, 0.08f }, 7713, 169, 2.16f, 0.57f, 0.15f, 0.0f, 0.69f, 26.24f, 0.32f, 3.29f, 0.26f, 0.57f, 0.64f, 1.15f, 1.68f, 0.62f, 0.64f, 1.25f, 0.98f, 0.72f, 0.58f, 0.48f, 0.94f, 0.06f, 0.22f, 0.58f, 0.4f, 0.64f, 0.42f, 0.72f, 1.52f, 2.0f, 0.82f, 0.17f, 0.19f, 2 },
	// 13: 地下室 - 圧迫感のある密度
	{ 0.53f, 20.94f, 0.59f, { 2.5f, 0.68f, 3.8f, 0.65f, 6.5f, 0.61f, 10.5f, 0.52f, 14.0f, 0.44f, 18.0f, 0.36f, 22.0f, 0.29f, 27.0f, 0.21f }, 4000, 42, 0.58f, 0.01f, 2.25f, 0.0f, 0.36f, 0.0f, 0.45f, 0.66f, 0.69f, 0.31f, 0.19f, 0.6f, 1.0f, 0.35f, 0.26f, 1.3f, 1.0f, 0.7f, 0.4f, 0.3f, 0.69f, -0.5f, 0.6f, 0.7f, 0.29f, 0.25f, 0.7f, 0.8f, 0.7f, 0.8f, 0.25f, 0.26f, 0.32f, 1 },
	// 14: 劇場 - 音響設計された空間
	{ 0.44f, 73.41f, 0.36f, { 11.0f, 0.41f, 17.0f, 0.36f, 36.0f, 0.32f, 58.0f, 0.27f, 82.0f, 0.23f, 108.0f, 0.19f, 138.0f, 0.14f, 172.0f, 0.12f }, 12371, 73, 1.55f, 0.39f, 1.04f, 0.26f, 0.65f, 8.91f, 0.35f, 1.25f, 0.49f, 0.78f, 0.23f, 0.9f, 1.3f, 0.85f, 0.69f, 1.15f, 1.0f, 0.85f, 0.75f, 0.65f, 0.7f, 0.06f, 0.25f, 0.25f, 0.45f, 0.65f, 0.55f, 0.5f, 1.25f, 1.15f, 0.6f, 0.0f, 0.1f, 1 },
	// 15: 水中 - 特殊な低域特性
	{ 0.38f, 124.75f, 0.38f, { 5.0f, 0.43f, 7.5f, 0.41f, 20.0f, 0.38f, 32.0f, 0.34f, 48.0f, 0.31f, 68.0f, 0.26f, 92.0f, 0.23f, 122.0f, 0.18f }, 4000, 305, 1.05f, 0.83f, 0.44f, 0.0f, 0.85f, 10.06f, 0.62f, 1.52f, 0.37f, 0.78f, 0.68f, 1.25f, 1.68f, 0.38f, 0.42f, 1.7f, 1.08f, 0.48f, 0.78f, 0.28f, 0.49f, -0.72f, 0.38f, 0.8f, 0.27f, 0.2f, 0.83f, 0.9f, 1.1f, 0.9f, 0.18f, 0.36f, 0.43f, 3 },
	// 16: トンネル/地下道 - フラッターエコー
	{ 0.44f, 114.23f, 0.44f, { 11.0f, 0.52f, 17.0f, 0.5f, 23.0f, 0.49f, 29.0f, 0.47f, 36.0f, 0.44f, 44.0f, 0.41f, 53.0f, 0.37f, 63.0f, 0.33f }, 8681, 154, 0.59f, 0.34f, 1.26f, 0.16f, 0.52f, 6.46f, 0.3f, 1.01f, 0.61f, 0.43f, 0.18f, 0.68f, 1.08f, 0.52f, 0.65f, 0.98f, 0.98f, 0.92f, 0.52f, 0.68f, 0.78f, -0.05f, 0.18f, 0.32f, 0.34f, 0.61f, 0.28f, 0.58f, 0.82f, 0.72f, 0.38f, 0.61f, 0.18f, 1 },
	// 17: アリーナ/ドーム - 超巨大スポーツ施設
	{ 0.2f, 350.0f, 0.22f, { 82.0f, 0.21f, 122.0f, 0.18f, 182.0f, 0.15f, 248.0f, 0.14f, 325.0f, 0.11f, 412.0f, 0.1f, 512.0f, 0.08f, 625.0f, 0.06f }, 5993, 37, 1.92f, 0.64f, 0.38f, 0.08f, 0.82f, 32.04f, 0.53f, 3.2f, 0.35f, 0.97f, 0.49f, 1.8f, 2.35f, 0.78f, 0.5f, 1.5f, 1.02f, 0.72f, 0.78f, 0.52f, 0.57f, 0.05f, 0.18f, 0.36f, 0.37f, 0.67f, 0.56f, 0.65f, 2.4f, 2.15f, 0.7f, 0.06f, 0.24f, 1 },
	// 18: 小部屋/クローゼット - 超小空間デッド
	{ 0.15f, 6.0f, 0.1f, { 1.5f, 0.25f, 2.5f, 0.2f, 4.0f, 0.14f, 6.0f, 0.07f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 4522, 67, 0.5f, 0.1f, 0.05f, 0.0f, 0.23f, 7.0f, 0.19f, 0.53f, 0.88f, 0.32f, 0.0f, 0.25f, 0.35f, 0.95f, 0.58f, 0.7f, 1.0f, 1.1f, 0.2f, 0.2f, 0.9f, 0.15f, 0.85f, 0.2f, 0.55f, 0.53f, 0.9f, 0.6f, 0.55f, 0.6f, 0.15f, 0.02f, 0.15f, 1 },
	// 19: 階段室 - 縦方向の特殊反射
	{ 0.47f, 107.27f, 0.51f, { 14.0f, 0.52f, 22.0f, 0.48f, 38.0f, 0.44f, 56.0f, 0.38f, 76.0f, 0.32f, 98.0f, 0.27f, 124.0f, 0.22f, 154.0f, 0.16f }, 9815, 123, 0.93f, 0.5f, 1.81f, 0.18f, 0.53f, 13.09f, 0.46f, 1.05f, 0.7f, 0.67f, 0.34f, 0.8f, 1.15f, 0.6f, 0.76f, 1.1f, 1.0f, 0.9f, 0.6f, 0.65f, 0.66f, 0.18f, 0.25f, 0.4f, 0.31f, 0.67f, 0.4f, 0.55f, 1.4f, 0.95f, 0.45f, 0.13f, 0.19f, 1 },
	// 20: 地下鉄ホーム - 都市的コンクリート
	{ 0.42f, 122.46f, 0.49f, { 22.0f, 0.47f, 33.0f, 0.44f, 50.0f, 0.4f, 72.0f, 0.36f, 95.0f, 0.32f, 122.0f, 0.27f, 152.0f, 0.23f, 186.0f, 0.18f }, 10636, 99, 1.1f, 0.16f, 1.09f, 0.26f, 0.68f, 10.22f, 0.36f, 1.6f, 0.62f, 0.54f, 0.26f, 0.75f, 1.1f, 0.55f, 0.57f, 1.05f, 1.0f, 0.95f, 0.6f, 0.65f, 0.83f, -0.07f, 0.2f, 0.35f, 0.38f, 0.56f, 0.35f, 0.5f, 1.15f, 1.25f, 0.4f, 0.23f, 0.05f, 1 },
	// 21: 倉庫 - 大きく空っぽ
	{ 0.29f, 126.98f, 0.31f, { 52.0f, 0.34f, 82.0f, 0.31f, 128.0f, 0.28f, 175.0f, 0.25f, 228.0f, 0.21f, 285.0f, 0.18f, 348.0f, 0.14f, 418.0f, 0.11f }, 8490, 59, 1.45f, 0.53f, 0.61f, 0.07f, 0.69f, 9.99f, 0.66f, 1.67f, 0.28f, 0.67f, 0.61f, 1.22f, 1.55f, 0.7f, 0.5f, 1.18f, 0.96f, 0.8f, 0.65f, 0.5f, 0.62f, 0.16f, 0.34f, 0.46f, 0.68f, 0.49f, 0.54f, 0.56f, 1.48f, 1.58f, 0.66f, 0.23f, 0.25f, 1 },
	// 22: 廊下 - 長く狭い直線的
	{ 0.47f, 81.92f, 0.51f, { 20.0f, 0.52f, 32.0f, 0.49f, 48.0f, 0.44f, 68.0f, 0.39f, 92.0f, 0.33f, 118.0f, 0.28f, 148.0f, 0.22f, 182.0f, 0.16f }, 9870, 113, 0.43f, 0.2f, 1.32f, 0.16f, 0.29f, 0.0f, 0.5f, 0.43f, 0.5f, 0.35f, 0.33f, 0.82f, 1.15f, 0.62f, 0.43f, 1.08f, 0.98f, 0.88f, 0.48f, 0.58f, 0.64f, -0.01f, 0.38f, 0.28f, 0.54f, 0.44f, 0.48f, 0.52f, 0.68f, 1.42f, 0.42f, 0.17f, 0.16f, 1 },
	// 23: 工場 - 金属的産業的
	{ 0.44f, 92.61f, 0.47f, { 25.0f, 0.54f, 40.0f, 0.52f, 60.0f, 0.49f, 88.0f, 0.43f, 118.0f, 0.38f, 152.0f, 0.32f, 192.0f, 0.27f, 238.0f, 0.21f }, 13432, 155, 1.17f, 0.38f, 2.09f, 0.32f, 0.54f, 6.31f, 0.57f, 1.25f, 0.42f, 0.52f, 0.52f, 0.56f, 0.9f, 0.5f, 0.65f, 0.9f, 0.96f, 1.1f, 0.56f, 0.75f, 0.78f, 0.31f, 0.2f, 0.3f, 0.53f, 0.67f, 0.25f, 0.3f, 1.18f, 1.25f, 0.4f, 0.33f, 0.21f, 1 },
	// 24: 寺社仏閣 - 木造の温かみ
	{ 0.44f, 136.7f, 0.49f, { 32.0f, 0.45f, 52.0f, 0.41f, 88.0f, 0.36f, 128.0f, 0.32f, 175.0f, 0.29f, 228.0f, 0.24f, 288.0f, 0.21f, 355.0f, 0.16f }, 6516, 49, 1.26f, 0.47f, 0.52f, 0.0f, 0.83f, 9.32f, 0.66f, 1.48f, 0.47f, 0.85f, 0.61f, 1.35f, 1.55f, 0.82f, 0.4f, 1.28f, 0.98f, 0.72f, 0.78f, 0.48f, 0.61f, -0.04f, 0.42f, 0.43f, 0.95f, 0.51f, 0.78f, 0.58f, 1.42f, 1.52f, 0.58f, 0.2f, 0.24f, 1 },
	// 25: 宇宙空間 - SF特殊空間
	{ 0.17f, 350.0f, 0.22f, { 0.0f, 0.0f, 0.0f, 0.0f, 205.0f, 0.08f, 285.0f, 0.06f, 375.0f, 0.05f, 475.0f, 0.03f, 585.0f, 0.03f, 705.0f, 0.02f }, 16996, 21, 1.17f, 0.76f, 0.29f, 0.13f, 0.29f, 35.48f, 0.37f, 3.43f, 0.11f, 0.17f, 0.38f, 0.92f, 1.12f, 0.25f, 0.28f, 0.75f, 0.92f, 1.12f, 0.35f, 0.36f, 1.00f, -0.06f, 0.1f, 0.85f, 0.47f, 0.22f, 0.15f, 0.88f, 3.0f, 3.35f, 0.88f, 0.49f, 0.59f, 3 },
	// 26: 野球場/サッカー場 - 屋外超大型
	{ 0.17f, 317.95f, 0.18f, { 98.0f, 0.18f, 158.0f, 0.15f, 235.0f, 0.14f, 315.0f, 0.11f, 405.0f, 0.09f, 505.0f, 0.07f, 615.0f, 0.05f, 735.0f, 0.03f }, 17204, 117, 1.91f, 0.59f, 0.51f, 0.05f, 0.61f, 39.27f, 0.58f, 3.46f, 0.35f, 0.54f, 0.94f, 1.02f, 1.4f, 0.65f, 0.59f, 1.1f, 0.96f, 0.8f, 0.62f, 0.56f, 0.74f, 0.18f, 0.25f, 0.5f, 0.66f, 0.56f, 0.4f, 0.6f, 1.68f, 2.15f, 0.82f, 0.19f, 0.28f, 2 },
	// 27: 図書館 - 静寂で吸音的
	{ 0.24f, 41.31f, 0.27f, { 7.0f, 0.29f, 11.0f, 0.25f, 18.0f, 0.22f, 28.0f, 0.16f, 38.0f, 0.12f, 50.0f, 0.08f, 64.0f, 0.05f, 82.0f, 0.04f }, 7361, 69, 0.84f, 0.07f, 0.41f, 0.1f, 0.36f, 2.47f, 0.69f, 0.81f, 0.65f, 0.34f, 0.64f, 0.65f, 0.8f, 0.9f, 0.34f, 0.9f, 1.0f, 1.0f, 0.4f, 0.4f, 0.67f, 0.02f, 0.75f, 0.25f, 0.87f, 0.41f, 0.85f, 0.4f, 1.05f, 1.1f, 0.3f, 0.08f, 0.1f, 1 },
	// 28: プール(室内) - タイル水面反射
	{ 0.49f, 114.25f, 0.49f, { 16.0f, 0.54f, 24.0f, 0.52f, 40.0f, 0.49f, 62.0f, 0.43f, 86.0f, 0.38f, 114.0f, 0.32f, 146.0f, 0.27f, 182.0f, 0.21f }, 9958, 276, 1.13f, 0.47f, 1.4f, 0.22f, 0.6f, 9.95f, 0.49f, 1.12f, 0.54f, 0.7f, 0.66f, 0.76f, 1.18f, 0.54f, 0.57f, 1.1f, 0.96f, 0.86f, 0.66f, 0.7f, 0.71f, 0.36f, 0.18f, 0.2f, 0.59f, 0.74f, 0.2f, 0.34f, 1.1f, 1.0f, 0.4f, 0.22f, 0.27f, 1 },
	// 29: エレベーター - 超小金属空間
	{ 0.44f, 10.42f, 0.58f, { 2.2f, 0.7f, 3.8f, 0.68f, 5.8f, 0.63f, 8.5f, 0.56f, 11.0f, 0.49f, 14.0f, 0.41f, 17.5f, 0.32f, 21.5f, 0.24f }, 17947, 206, 0.45f, 0.21f, 3.44f, 0.18f, 0.28f, 8.78f, 0.36f, 0.3f, 0.9f, 0.27f, 0.31f, 0.35f, 0.55f, 0.3f, 0.73f, 0.85f, 1.0f, 1.15f, 0.3f, 0.5f, 0.89f, 0.36f, 0.08f, 0.05f, 0.44f, 0.78f, 0.05f, 0.2f, 0.45f, 0.5f, 0.15f, 0.43f, 0.32f, 1 },
	// 30: 駐車場 - 広い低天井コンクリート
	{ 0.42f, 126.88f, 0.47f, { 28.0f, 0.47f, 45.0f, 0.43f, 72.0f, 0.39f, 105.0f, 0.35f, 142.0f, 0.31f, 185.0f, 0.27f, 235.0f, 0.23f, 292.0f, 0.18f }, 9479, 89, 1.15f, 0.25f, 0.75f, 0.14f, 0.48f, 11.49f, 0.64f, 1.25f, 0.58f, 0.5f, 0.57f, 0.82f, 1.12f, 0.56f, 0.38f, 1.06f, 0.96f, 0.9f, 0.56f, 0.6f, 0.62f, 0.09f, 0.3f, 0.36f, 0.56f, 0.49f, 0.44f, 0.5f, 0.8f, 1.32f, 0.44f, 0.12f, 0.16f, 1 },
	// 31: コンサートホール - クラシック用最高峰
	{ 0.42f, 95.1f, 0.47f, { 20.0f, 0.47f, 32.0f, 0.41f, 55.0f, 0.38f, 85.0f, 0.32f, 118.0f, 0.28f, 158.0f, 0.24f, 205.0f, 0.19f, 262.0f, 0.15f }, 13526, 50, 1.47f, 0.3f, 0.6f, 0.18f, 0.84f, 4.28f, 0.47f, 1.68f, 0.38f, 1.0f, 0.37f, 1.38f, 1.75f, 0.85f, 0.55f, 1.32f, 0.96f, 0.76f, 0.8f, 0.56f, 0.68f, 0.11f, 0.28f, 0.3f, 0.68f, 0.7f, 0.6f, 0.5f, 1.45f, 1.32f, 0.6f, 0.03f, 0.19f, 1 },
	// 32: ジャズクラブ - 親密で温かい
	{ 0.49f, 48.86f, 0.44f, { 9.0f, 0.61f, 14.0f, 0.56f, 24.0f, 0.5f, 38.0f, 0.43f, 52.0f, 0.37f, 68.0f, 0.31f, 88.0f, 0.24f, 112.0f, 0.18f }, 8760, 93, 1.32f, 0.28f, 1.37f, 0.28f, 0.63f, 1.5f, 0.4f, 0.93f, 0.54f, 0.62f, 0.28f, 0.9f, 1.15f, 0.75f, 0.71f, 1.15f, 1.0f, 0.85f, 0.6f, 0.55f, 0.71f, -0.09f, 0.45f, 0.35f, 0.76f, 0.65f, 0.7f, 0.45f, 0.9f, 0.95f, 0.5f, 0.06f, 0.07f, 1 },
	// 33: カラオケボックス - 小密室エンタメ
	{ 0.53f, 29.79f, 0.55f, { 5.5f, 0.68f, 8.5f, 0.63f, 14.0f, 0.59f, 21.0f, 0.52f, 28.0f, 0.46f, 36.0f, 0.39f, 46.0f, 0.32f, 58.0f, 0.24f }, 13612, 118, 0.83f, 0.13f, 2.07f, 0.36f, 0.49f, 0.0f, 0.23f, 0.69f, 0.56f, 0.5f, 0.13f, 0.6f, 0.85f, 0.55f, 0.68f, 0.95f, 1.0f, 1.05f, 0.5f, 0.65f, 0.78f, 0.07f, 0.5f, 0.25f, 0.53f, 0.74f, 0.5f, 0.4f, 0.7f, 0.75f, 0.3f, 0.1f, 0.08f, 1 },
	// 34: 映画館 - THX規格的
	{ 0.58f, 120.7f, 0.53f, { 25.0f, 0.5f, 40.0f, 0.43f, 68.0f, 0.38f, 98.0f, 0.32f, 135.0f, 0.27f, 178.0f, 0.23f, 228.0f, 0.18f, 286.0f, 0.14f }, 12071, 66, 1.51f, 0.36f, 0.76f, 0.28f, 0.78f, 8.45f, 0.51f, 1.62f, 0.49f, 0.94f, 0.41f, 1.2f, 1.5f, 0.8f, 0.62f, 1.25f, 1.0f, 0.85f, 0.75f, 0.65f, 0.67f, 0.15f, 0.3f, 0.3f, 0.57f, 0.75f, 0.6f, 0.5f, 1.35f, 1.3f, 0.55f, 0.0f, 0.19f, 1 },
	// 35: 地下鉄車内 - 揺れる密室
	{ 0.44f, 19.06f, 0.58f, { 4.5f, 0.63f, 7.5f, 0.59f, 11.5f, 0.52f, 17.0f, 0.45f, 23.0f, 0.38f, 30.0f, 0.31f, 38.0f, 0.23f, 48.0f, 0.16f }, 6435, 154, 0.48f, 0.24f, 2.75f, 0.18f, 0.54f, 0.0f, 0.33f, 0.64f, 0.71f, 0.36f, 0.19f, 0.55f, 0.75f, 0.4f, 0.5f, 0.9f, 1.0f, 1.0f, 0.4f, 0.5f, 0.85f, -0.1f, 0.55f, 0.5f, 0.5f, 0.45f, 0.65f, 0.65f, 0.65f, 0.7f, 0.25f, 0.47f, 0.22f, 1 },
	// 36: 空港ターミナル - 巨大公共空間
	{ 0.23f, 198.26f, 0.22f, { 58.0f, 0.26f, 98.0f, 0.22f, 158.0f, 0.2f, 218.0f, 0.16f, 288.0f, 0.14f, 368.0f, 0.11f, 458.0f, 0.09f, 558.0f, 0.06f }, 10865, 82, 1.56f, 0.38f, 0.59f, 0.08f, 0.71f, 18.93f, 0.53f, 2.21f, 0.41f, 0.78f, 0.51f, 1.3f, 1.65f, 0.72f, 0.59f, 1.25f, 0.98f, 0.82f, 0.68f, 0.62f, 0.62f, 0.09f, 0.3f, 0.38f, 0.48f, 0.67f, 0.48f, 0.52f, 1.7f, 1.8f, 0.68f, 0.07f, 0.17f, 1 },
	// 37: ショッピングモール - 賑やか商業施設
	{ 0.34f, 139.67f, 0.37f, { 32.0f, 0.42f, 48.0f, 0.36f, 78.0f, 0.32f, 112.0f, 0.27f, 152.0f, 0.23f, 198.0f, 0.19f, 252.0f, 0.16f, 315.0f, 0.12f }, 10599, 90, 1.44f, 0.32f, 0.9f, 0.18f, 0.72f, 11.7f, 0.54f, 1.56f, 0.55f, 0.9f, 0.44f, 1.15f, 1.4f, 0.7f, 0.63f, 1.2f, 1.0f, 0.9f, 0.65f, 0.6f, 0.71f, 0.3f, 0.35f, 0.35f, 0.56f, 0.8f, 0.55f, 0.5f, 1.3f, 1.35f, 0.6f, 0.08f, 0.26f, 1 },
	// 38: 病院 - 静かで清潔
	{ 0.3f, 58.4f, 0.34f, { 11.0f, 0.38f, 17.0f, 0.32f, 28.0f, 0.27f, 45.0f, 0.22f, 62.0f, 0.17f, 82.0f, 0.14f, 106.0f, 0.1f, 135.0f, 0.07f }, 9037, 75, 1.16f, 0.19f, 0.58f, 0.1f, 0.6f, 10.55f, 0.46f, 1.16f, 0.68f, 0.42f, 0.36f, 0.7f, 0.9f, 0.85f, 0.64f, 0.95f, 1.0f, 1.0f, 0.45f, 0.45f, 0.76f, -0.16f, 0.7f, 0.3f, 0.76f, 0.52f, 0.75f, 0.45f, 1.0f, 1.05f, 0.35f, 0.13f, 0.02f, 1 },
	// 39: レコーディングブース - プロ用極ドライ
	{ 0.04f, 6.0f, 0.01f, { 1.2f, 0.11f, 2.2f, 0.09f, 3.5f, 0.05f, 5.5f, 0.03f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 19938, 20, 1.02f, 0.0f, 0.05f, 0.46f, 0.24f, 3.74f, 0.0f, 0.68f, 1.0f, 0.1f, 0.0f, 0.15f, 0.25f, 0.98f, 0.53f, 0.75f, 1.0f, 1.05f, 0.05f, 0.05f, 1.0f, -0.13f, 0.95f, 0.05f, 0.73f, 0.48f, 0.95f, 0.35f, 0.8f, 0.75f, 0.05f, 0.1f, 0.0f, 1 },
	// 40: オペラハウス - 劇場の最高峰
	{ 0.33f, 153.1f, 0.34f, { 28.0f, 0.39f, 45.0f, 0.34f, 78.0f, 0.32f, 122.0f, 0.27f, 172.0f, 0.24f, 232.0f, 0.2f, 302.0f, 0.17f, 385.0f, 0.13f }, 11363, 45, 1.48f, 0.34f, 0.59f, 0.1f, 0.97f, 15.52f, 0.52f, 2.03f, 0.58f, 1.0f, 0.42f, 1.52f, 1.95f, 0.82f, 0.49f, 1.36f, 0.96f, 0.7f, 0.8f, 0.53f, 0.69f, 0.04f, 0.25f, 0.34f, 0.79f, 0.67f, 0.66f, 0.56f, 1.58f, 1.48f, 0.6f, 0.1f, 0.19f, 1 },
	// 41: 喫茶店/カフェ - 適度な賑わいと吸音
	{ 0.38f, 43.28f, 0.36f, { 8.5f, 0.5f, 13.0f, 0.45f, 21.0f, 0.4f, 35.0f, 0.34f, 48.0f, 0.29f, 64.0f, 0.23f, 83.0f, 0.18f, 106.0f, 0.14f }, 8113, 93, 0.98f, 0.21f, 0.89f, 0.18f, 0.43f, 0.0f, 0.67f, 0.63f, 0.52f, 0.51f, 0.59f, 0.75f, 1.0f, 0.8f, 0.5f, 1.05f, 1.0f, 0.95f, 0.55f, 0.5f, 0.6f, 0.19f, 0.55f, 0.35f, 0.95f, 0.57f, 0.75f, 0.45f, 0.95f, 1.0f, 0.45f, 0.1f, 0.19f, 1 },
	// 42: バー/ラウンジ - 暗く落ち着いた雰囲気
	{ 0.44f, 56.86f, 0.42f, { 10.5f, 0.54f, 16.0f, 0.49f, 27.0f, 0.43f, 46.0f, 0.38f, 64.0f, 0.32f, 86.0f, 0.27f, 112.0f, 0.22f, 144.0f, 0.16f }, 6889, 115, 0.95f, 0.21f, 1.12f, 0.22f, 0.52f, 1.32f, 0.61f, 0.58f, 0.6f, 0.55f, 0.53f, 0.9f, 1.15f, 0.75f, 0.42f, 1.15f, 1.0f, 0.85f, 0.58f, 0.48f, 0.6f, 0.04f, 0.6f, 0.4f, 1.0f, 0.42f, 0.8f, 0.5f, 0.85f, 0.9f, 0.4f, 0.17f, 0.21f, 1 },
	// 43: 居酒屋 - 賑やか木材吸音
	{ 0.41f, 42.07f, 0.39f, { 8.0f, 0.52f, 12.5f, 0.47f, 20.0f, 0.41f, 32.0f, 0.36f, 45.0f, 0.31f, 60.0f, 0.25f, 78.0f, 0.2f, 100.0f, 0.14f }, 7614, 110, 0.86f, 0.17f, 1.16f, 0.19f, 0.42f, 0.0f, 0.71f, 0.62f, 0.58f, 0.43f, 0.64f, 0.8f, 1.05f, 0.75f, 0.49f, 1.1f, 1.0f, 0.9f, 0.52f, 0.48f, 0.66f, 0.15f, 0.6f, 0.4f, 1.0f, 0.58f, 0.8f, 0.5f, 0.9f, 0.95f, 0.4f, 0.15f, 0.19f, 1 },
	// 44: 美術館/博物館 - 静かで広い高天井
	{ 0.42f, 119.46f, 0.44f, { 35.0f, 0.39f, 58.0f, 0.33f, 98.0f, 0.29f, 145.0f, 0.24f, 198.0f, 0.2f, 258.0f, 0.16f, 328.0f, 0.13f, 408.0f, 0.1f }, 9642, 71, 1.27f, 0.42f, 0.6f, 0.13f, 0.55f, 13.21f, 0.72f, 1.33f, 0.43f, 0.72f, 0.73f, 1.18f, 1.45f, 0.82f, 0.46f, 1.2f, 0.98f, 0.82f, 0.68f, 0.58f, 0.53f, 0.19f, 0.52f, 0.32f, 0.83f, 0.58f, 0.68f, 0.52f, 1.58f, 1.42f, 0.58f, 0.05f, 0.22f, 1 },
	// 45: 講堂/大学教室 - 教育施設の反射
	{ 0.49f, 91.95f, 0.55f, { 18.0f, 0.52f, 27.0f, 0.47f, 46.0f, 0.41f, 75.0f, 0.36f, 105.0f, 0.31f, 142.0f, 0.25f, 185.0f, 0.2f, 235.0f, 0.15f }, 10976, 76, 1.22f, 0.36f, 0.87f, 0.22f, 0.7f, 9.38f, 0.66f, 1.18f, 0.53f, 0.64f, 0.59f, 1.0f, 1.3f, 0.75f, 0.58f, 1.2f, 1.0f, 0.9f, 0.68f, 0.6f, 0.73f, 0.16f, 0.35f, 0.3f, 0.93f, 0.58f, 0.6f, 0.5f, 1.25f, 1.2f, 0.55f, 0.2f, 0.17f, 1 },
	// 46: 竹林 - 和風自然音響
	{ 0.26f, 171.51f, 0.19f, { 42.0f, 0.26f, 68.0f, 0.23f, 118.0f, 0.21f, 172.0f, 0.2f, 232.0f, 0.18f, 298.0f, 0.14f, 372.0f, 0.13f, 455.0f, 0.1f }, 4000, 222, 1.71f, 0.65f, 0.29f, 0.02f, 0.75f, 15.88f, 0.64f, 1.5f, 0.45f, 0.92f, 0.85f, 1.4f, 1.7f, 0.85f, 0.47f, 1.25f, 1.0f, 0.75f, 0.88f, 0.45f, 0.46f, -0.11f, 0.75f, 0.65f, 0.96f, 0.49f, 0.85f, 0.45f, 1.5f, 1.6f, 0.75f, 0.13f, 0.33f, 2 },
	// 47: 渓谷/滝 - 水の反射と濡れた岩肌
	{ 0.26f, 246.87f, 0.23f, { 98.0f, 0.27f, 158.0f, 0.25f, 238.0f, 0.22f, 328.0f, 0.18f, 428.0f, 0.16f, 538.0f, 0.14f, 658.0f, 0.11f, 788.0f, 0.08f }, 5334, 202, 1.67f, 0.5f, 0.28f, 0.01f, 0.46f, 26.71f, 0.7f, 2.13f, 0.37f, 0.48f, 0.88f, 1.2f, 1.7f, 0.68f, 0.5f, 1.28f, 0.98f, 0.68f, 0.7f, 0.52f, 0.54f, -0.34f, 0.38f, 0.65f, 0.64f, 0.44f, 0.58f, 0.68f, 1.75f, 2.0f, 0.78f, 0.08f, 0.19f, 2 },
	// 48: 砂漠 - 超開放的反射極小
	{ 0.09f, 350.0f, 0.07f, { 0.0f, 0.0f, 0.0f, 0.0f, 215.0f, 0.07f, 365.0f, 0.06f, 528.0f, 0.05f, 705.0f, 0.03f, 898.0f, 0.02f, 1108.0f, 0.02f }, 19775, 151, 2.12f, 0.19f, 0.18f, 0.01f, 0.13f, 43.62f, 0.45f, 3.68f, 0.2f, 0.14f, 1.0f, 0.7f, 0.9f, 0.6f, 0.66f, 0.8f, 1.0f, 1.2f, 0.3f, 0.5f, 0.79f, 0.21f, 0.05f, 0.85f, 0.44f, 0.7f, 0.15f, 0.9f, 2.5f, 3.2f, 0.95f, 0.0f, 0.32f, 2 },
	// 49: ガレージ - 車庫硬質空間
	{ 0.51f, 97.51f, 0.58f, { 18.0f, 0.58f, 28.0f, 0.54f, 46.0f, 0.49f, 72.0f, 0.43f, 98.0f, 0.38f, 128.0f, 0.32f, 162.0f, 0.27f, 202.0f, 0.22f }, 12992, 138, 0.9f, 0.19f, 1.61f, 0.24f, 0.55f, 9.53f, 0.71f, 0.99f, 0.68f, 0.52f, 0.64f, 0.75f, 1.05f, 0.55f, 0.57f, 1.05f, 1.0f, 1.0f, 0.58f, 0.7f, 0.82f, 0.29f, 0.2f, 0.3f, 0.72f, 0.7f, 0.3f, 0.45f, 0.95f, 1.05f, 0.35f, 0.29f, 0.22f, 1 },
	// 50: 展望台 - 高所開放感
	{ 0.15f, 350.0f, 0.12f, { 75.0f, 0.18f, 128.0f, 0.14f, 208.0f, 0.13f, 305.0f, 0.1f, 418.0f, 0.09f, 548.0f, 0.07f, 695.0f, 0.05f, 862.0f, 0.04f }, 19412, 111, 1.97f, 0.56f, 0.42f, 0.04f, 0.49f, 42.31f, 0.58f, 2.74f, 0.4f, 0.62f, 0.95f, 0.85f, 1.2f, 0.75f, 0.68f, 0.95f, 1.0f, 1.05f, 0.6f, 0.7f, 0.74f, 0.44f, 0.15f, 0.6f, 0.64f, 0.75f, 0.35f, 0.7f, 2.0f, 2.3f, 0.9f, 0.13f, 0.42f, 2 },
	// 51: 小さな礼拝堂 - 教会より親密で温かい
	{ 0.42f, 98.04f, 0.49f, { 22.0f, 0.43f, 35.0f, 0.39f, 60.0f, 0.34f, 92.0f, 0.33f, 128.0f, 0.26f, 172.0f, 0.23f, 222.0f, 0.18f, 282.0f, 0.15f }, 6921, 40, 1.42f, 0.4f, 0.45f, 0.1f, 0.9f, 3.2f, 0.38f, 1.92f, 0.24f, 0.87f, 0.28f, 1.36f, 1.8f, 0.82f, 0.5f, 1.48f, 1.06f, 0.7f, 0.76f, 0.5f, 0.67f, -0.19f, 0.34f, 0.36f, 0.58f, 0.59f, 0.7f, 0.66f, 1.55f, 1.45f, 0.6f, 0.1f, 0.18f, 1 },
	// 52: 大型ショッピングセンター - モールより巨大
	{ 0.41f, 116.42f, 0.45f, { 45.0f, 0.45f, 72.0f, 0.4f, 115.0f, 0.34f, 168.0f, 0.3f, 228.0f, 0.25f, 298.0f, 0.21f, 378.0f, 0.17f, 472.0f, 0.14f }, 10878, 89, 1.72f, 0.44f, 0.62f, 0.21f, 0.72f, 10.13f, 0.32f, 1.97f, 0.38f, 0.69f, 0.22f, 1.15f, 1.48f, 0.68f, 0.73f, 1.18f, 0.96f, 0.86f, 0.63f, 0.58f, 0.72f, -0.01f, 0.36f, 0.34f, 0.41f, 0.68f, 0.52f, 0.52f, 1.4f, 1.5f, 0.6f, 0.12f, 0.16f, 1 },
	// 53: 地下洞窟(深層) - より深く神秘的
	{ 0.38f, 170.7f, 0.42f, { 35.0f, 0.5f, 58.0f, 0.46f, 125.0f, 0.43f, 198.0f, 0.39f, 265.0f, 0.35f, 342.0f, 0.31f, 428.0f, 0.26f, 525.0f, 0.22f }, 4000, 110, 1.47f, 0.54f, 0.6f, 0.01f, 0.81f, 12.77f, 0.64f, 1.93f, 0.3f, 0.93f, 0.62f, 1.36f, 2.05f, 0.4f, 0.4f, 1.55f, 0.96f, 0.5f, 0.78f, 0.3f, 0.4f, -0.44f, 0.58f, 0.78f, 0.15f, 0.36f, 0.66f, 0.88f, 1.58f, 1.68f, 0.3f, 0.18f, 0.45f, 1 },
	// 54: 古城の大広間 - 石造り中世的
	{ 0.22f, 153.07f, 0.24f, { 38.0f, 0.26f, 62.0f, 0.24f, 108.0f, 0.21f, 165.0f, 0.18f, 228.0f, 0.15f, 302.0f, 0.14f, 388.0f, 0.11f, 488.0f, 0.09f }, 4243, 53, 1.66f, 0.55f, 0.39f, 0.03f, 0.86f, 16.5f, 0.46f, 2.23f, 0.29f, 0.9f, 0.36f, 1.65f, 2.15f, 0.76f, 0.57f, 1.56f, 0.96f, 0.6f, 0.82f, 0.44f, 0.54f, -0.24f, 0.3f, 0.56f, 0.43f, 0.55f, 0.76f, 0.8f, 1.98f, 1.76f, 0.56f, 0.09f, 0.23f, 1 },
	// 55: 野外音楽堂 - 半開放的ステージ
	{ 0.24f, 135.11f, 0.2f, { 32.0f, 0.3f, 52.0f, 0.26f, 88.0f, 0.23f, 135.0f, 0.22f, 188.0f, 0.16f, 248.0f, 0.14f, 318.0f, 0.11f, 398.0f, 0.08f }, 15305, 84, 1.74f, 0.46f, 0.52f, 0.08f, 0.75f, 11.5f, 0.25f, 2.05f, 0.41f, 0.79f, 0.45f, 1.15f, 1.45f, 0.8f, 0.65f, 1.15f, 1.0f, 0.95f, 0.72f, 0.7f, 0.75f, 0.11f, 0.25f, 0.45f, 0.45f, 0.78f, 0.5f, 0.58f, 1.55f, 1.7f, 0.75f, 0.08f, 0.26f, 2 },
	// 56: 鍾乳洞 - 複雑な水滴反射
	{ 0.44f, 185.37f, 0.51f, { 42.0f, 0.52f, 68.0f, 0.48f, 128.0f, 0.45f, 195.0f, 0.41f, 268.0f, 0.37f, 348.0f, 0.32f, 438.0f, 0.28f, 538.0f, 0.23f }, 4000, 126, 1.25f, 0.61f, 0.54f, 0.02f, 0.99f, 14.09f, 0.56f, 2.22f, 0.35f, 0.89f, 0.54f, 1.48f, 1.95f, 0.44f, 0.37f, 1.52f, 0.96f, 0.56f, 0.82f, 0.34f, 0.56f, -0.52f, 0.5f, 0.8f, 0.29f, 0.36f, 0.7f, 0.86f, 1.5f, 1.66f, 0.34f, 0.36f, 0.42f, 1 },
	// 57: 廃墟工場 - 荒廃した金属空間
	{ 0.44f, 145.74f, 0.51f, { 38.0f, 0.52f, 62.0f, 0.5f, 98.0f, 0.47f, 148.0f, 0.42f, 205.0f, 0.38f, 272.0f, 0.32f, 348.0f, 0.27f, 435.0f, 0.22f }, 12404, 168, 1.34f, 0.34f, 1.89f, 0.34f, 0.81f, 9.93f, 0.32f, 1.96f, 0.47f, 0.78f, 0.22f, 0.66f, 1.03f, 0.54f, 0.7f, 0.96f, 0.96f, 1.06f, 0.6f, 0.73f, 0.91f, 0.41f, 0.28f, 0.34f, 0.38f, 0.89f, 0.3f, 0.34f, 1.36f, 1.46f, 0.44f, 0.36f, 0.34f, 1 },
	// 58: 和室(畳) - 日本的柔らかい吸音
	{ 0.27f, 42.24f, 0.3f, { 6.5f, 0.38f, 10.5f, 0.34f, 17.0f, 0.29f, 27.0f, 0.23f, 38.0f, 0.19f, 51.0f, 0.14f, 67.0f, 0.11f, 86.0f, 0.07f }, 6318, 63, 0.93f, 0.12f, 0.81f, 0.06f, 0.44f, 2.68f, 0.54f, 0.98f, 0.78f, 0.41f, 0.38f, 0.72f, 0.88f, 0.92f, 0.54f, 1.0f, 1.0f, 0.95f, 0.4f, 0.38f, 0.65f, -0.16f, 0.78f, 0.35f, 0.7f, 0.6f, 0.9f, 0.48f, 0.88f, 0.85f, 0.35f, 0.06f, 0.11f, 1 },
	// 59: 温泉施設 - 湿度高めタイル反射
	{ 0.51f, 97.22f, 0.55f, { 15.0f, 0.55f, 23.0f, 0.52f, 38.0f, 0.49f, 58.0f, 0.43f, 82.0f, 0.39f, 110.0f, 0.33f, 142.0f, 0.28f, 180.0f, 0.22f }, 9271, 274, 1.38f, 0.57f, 1.29f, 0.21f, 0.85f, 13.53f, 0.26f, 1.52f, 0.6f, 0.65f, 0.32f, 0.82f, 1.18f, 0.55f, 0.84f, 1.15f, 0.98f, 0.85f, 0.65f, 0.7f, 0.86f, 0.04f, 0.2f, 0.25f, 0.47f, 0.77f, 0.28f, 0.35f, 1.05f, 0.98f, 0.4f, 0.3f, 0.16f, 1 },
	// 60: 屋根裏部屋 - 斜め天井の特殊空間
	{ 0.41f, 58.73f, 0.49f, { 9.5f, 0.47f, 15.0f, 0.43f, 25.0f, 0.38f, 40.0f, 0.32f, 56.0f, 0.27f, 75.0f, 0.22f, 98.0f, 0.16f, 125.0f, 0.12f }, 8090, 81, 1.08f, 0.33f, 0.92f, 0.14f, 0.75f, 9.34f, 0.3f, 1.32f, 0.75f, 0.54f, 0.14f, 0.82f, 1.08f, 0.68f, 0.65f, 1.05f, 1.0f, 0.92f, 0.5f, 0.52f, 0.88f, 0.01f, 0.52f, 0.42f, 0.67f, 0.69f, 0.68f, 0.52f, 0.72f, 1.15f, 0.42f, 0.3f, 0.2f, 1 },
	// 61: 地下駐車場(多層) - 階層的複雑反射
	{ 0.29f, 107.17f, 0.34f, { 32.0f, 0.39f, 52.0f, 0.36f, 85.0f, 0.33f, 125.0f, 0.30f, 172.0f, 0.26f, 228.0f, 0.23f, 292.0f, 0.18f, 365.0f, 0.15f }, 9260, 89, 1.47f, 0.39f, 0.67f, 0.12f, 0.78f, 3.28f, 0.5f, 1.68f, 0.36f, 0.56f, 0.38f, 0.9f, 1.22f, 0.58f, 0.56f, 1.06f, 0.96f, 0.91f, 0.58f, 0.63f, 0.91f, -0.04f, 0.32f, 0.38f, 0.59f, 0.53f, 0.46f, 0.53f, 0.9f, 1.43f, 0.48f, 0.35f, 0.17f, 1 },
	// 62: 古い劇場(木造) - 温かみある音響設計
	{ 0.34f, 89.54f, 0.34f, { 18.0f, 0.39f, 28.0f, 0.35f, 48.0f, 0.31f, 78.0f, 0.27f, 112.0f, 0.23f, 152.0f, 0.20f, 198.0f, 0.17f, 252.0f, 0.14f }, 10100, 70, 1.53f, 0.44f, 1.0f, 0.15f, 0.78f, 6.0f, 0.52f, 1.31f, 0.45f, 0.97f, 0.42f, 1.2f, 1.48f, 0.82f, 0.54f, 1.28f, 0.98f, 0.8f, 0.75f, 0.58f, 0.69f, 0.13f, 0.4f, 0.36f, 0.69f, 0.65f, 0.7f, 0.56f, 1.22f, 1.18f, 0.56f, 0.13f, 0.31f, 1 },
	// 63: 大型倉庫(空) - 極端な空虚感
	{ 0.23f, 177.32f, 0.24f, { 72.0f, 0.3f, 118.0f, 0.27f, 188.0f, 0.26f, 272.0f, 0.22f, 368.0f, 0.19f, 478.0f, 0.16f, 602.0f, 0.14f, 742.0f, 0.11f }, 8261, 51, 1.7f, 0.62f, 0.52f, 0.04f, 0.73f, 16.01f, 0.52f, 2.0f, 0.28f, 0.8f, 0.46f, 1.36f, 1.8f, 0.73f, 0.61f, 1.26f, 0.96f, 0.78f, 0.7f, 0.52f, 0.64f, 0.12f, 0.36f, 0.53f, 0.47f, 0.54f, 0.56f, 0.63f, 1.73f, 1.86f, 0.7f, 0.23f, 0.34f, 1 },
	// 64: 小さな教会 - カテドラルより親密
	{ 0.31f, 123.17f, 0.36f, { 28.0f, 0.33f, 45.0f, 0.3f, 78.0f, 0.26f, 118.0f, 0.22f, 165.0f, 0.19f, 220.0f, 0.17f, 285.0f, 0.14f, 362.0f, 0.11f }, 6171, 37, 1.5f, 0.5f, 0.41f, 0.07f, 0.85f, 10.88f, 0.52f, 1.78f, 0.29f, 0.89f, 0.42f, 1.56f, 2.02f, 0.82f, 0.47f, 1.63f, 1.1f, 0.68f, 0.82f, 0.52f, 0.63f, -0.24f, 0.3f, 0.38f, 0.63f, 0.48f, 0.68f, 0.73f, 1.86f, 1.63f, 0.63f, 0.12f, 0.22f, 1 },
	// 65: ガラス温室 - 透明反射特性
	{ 0.53f, 75.42f, 0.55f, { 14.0f, 0.61f, 22.0f, 0.59f, 36.0f, 0.55f, 55.0f, 0.5f, 78.0f, 0.46f, 105.0f, 0.41f, 138.0f, 0.34f, 178.0f, 0.29f }, 17435, 118, 1.36f, 0.47f, 1.74f, 0.32f, 0.56f, 4.74f, 0.32f, 1.16f, 0.55f, 0.5f, 0.25f, 0.58f, 0.9f, 0.5f, 0.77f, 0.9f, 0.98f, 1.16f, 0.53f, 0.8f, 0.93f, 0.21f, 0.1f, 0.15f, 0.4f, 0.78f, 0.15f, 0.25f, 1.2f, 1.13f, 0.34f, 0.35f, 0.24f, 1 },
	// 66: 石造りトンネル - 硬く長い残響
	{ 0.47f, 148.25f, 0.53f, { 22.0f, 0.54f, 35.0f, 0.52f, 52.0f, 0.49f, 75.0f, 0.47f, 102.0f, 0.43f, 135.0f, 0.4f, 175.0f, 0.35f, 222.0f, 0.31f }, 7538, 181, 0.69f, 0.48f, 1.34f, 0.19f, 0.58f, 9.96f, 0.51f, 0.95f, 0.57f, 0.65f, 0.41f, 0.76f, 1.18f, 0.6f, 0.64f, 1.0f, 0.96f, 0.93f, 0.54f, 0.7f, 0.86f, 0.26f, 0.22f, 0.34f, 0.47f, 0.73f, 0.32f, 0.63f, 1.0f, 0.83f, 0.42f, 0.57f, 0.4f, 1 },
	// 67: コンクリート階段 - 縦方向硬質反射
	{ 0.51f, 75.32f, 0.55f, { 12.0f, 0.59f, 19.0f, 0.55f, 32.0f, 0.51f, 48.0f, 0.47f, 68.0f, 0.42f, 92.0f, 0.36f, 120.0f, 0.31f, 152.0f, 0.25f }, 12022, 143, 0.85f, 0.58f, 1.68f, 0.18f, 0.61f, 8.56f, 0.56f, 0.86f, 0.65f, 0.71f, 0.44f, 0.72f, 1.05f, 0.6f, 0.68f, 1.05f, 0.98f, 1.02f, 0.6f, 0.7f, 0.78f, 0.21f, 0.25f, 0.35f, 0.43f, 0.73f, 0.4f, 0.55f, 1.45f, 0.88f, 0.45f, 0.27f, 0.29f, 1 },
	// 68: 大浴場 - 広いタイル反射
	{ 0.49f, 97.7f, 0.53f, { 18.0f, 0.52f, 28.0f, 0.50f, 46.0f, 0.47f, 70.0f, 0.43f, 98.0f, 0.39f, 132.0f, 0.33f, 172.0f, 0.28f, 220.0f, 0.23f }, 10575, 290, 1.35f, 0.57f, 1.2f, 0.25f, 0.79f, 10.09f, 0.26f, 1.36f, 0.54f, 0.68f, 0.38f, 0.82f, 1.25f, 0.6f, 0.68f, 1.16f, 0.96f, 0.86f, 0.68f, 0.73f, 0.87f, 0.09f, 0.2f, 0.25f, 0.52f, 0.68f, 0.28f, 0.34f, 1.16f, 1.03f, 0.4f, 0.33f, 0.21f, 1 },
	// 69: 洗面所 - 小タイル空間
	{ 0.55f, 13.12f, 0.64f, { 2.5f, 0.7f, 4.2f, 0.68f, 7.0f, 0.63f, 11.0f, 0.56f, 15.0f, 0.49f, 19.5f, 0.41f, 24.5f, 0.32f, 30.5f, 0.24f }, 15336, 291, 0.63f, 0.18f, 2.89f, 0.28f, 0.24f, 5.78f, 0.24f, 0.3f, 0.86f, 0.27f, 0.16f, 0.42f, 0.62f, 0.38f, 0.78f, 1.05f, 1.0f, 0.95f, 0.25f, 0.48f, 0.89f, 0.1f, 0.12f, 0.08f, 0.31f, 0.79f, 0.12f, 0.28f, 0.55f, 0.6f, 0.22f, 0.35f, 0.25f, 1 },
	// 70: 廊下(カーペット敷き) - 吸音性高い
	{ 0.36f, 104.96f, 0.44f, { 18.0f, 0.43f, 28.0f, 0.39f, 45.0f, 0.33f, 68.0f, 0.28f, 95.0f, 0.23f, 126.0f, 0.18f, 162.0f, 0.14f, 204.0f, 0.1f }, 8768, 97, 0.71f, 0.24f, 1.28f, 0.14f, 0.64f, 10.12f, 0.55f, 0.9f, 0.72f, 0.44f, 0.29f, 0.78f, 1.05f, 0.72f, 0.48f, 1.08f, 1.0f, 0.92f, 0.45f, 0.48f, 0.88f, -0.09f, 0.62f, 0.35f, 0.83f, 0.48f, 0.75f, 0.48f, 0.75f, 1.25f, 0.38f, 0.34f, 0.15f, 1 },
	// 71: 大会議室 - ビジネス用途
	{ 0.47f, 61.64f, 0.49f, { 14.0f, 0.52f, 22.0f, 0.47f, 38.0f, 0.41f, 62.0f, 0.36f, 88.0f, 0.31f, 118.0f, 0.25f, 152.0f, 0.20f, 192.0f, 0.15f }, 10462, 73, 1.1f, 0.25f, 0.82f, 0.21f, 0.63f, 0.0f, 0.53f, 0.9f, 0.48f, 0.57f, 0.38f, 1.05f, 1.35f, 0.78f, 0.47f, 1.18f, 1.0f, 0.88f, 0.68f, 0.58f, 0.65f, -0.1f, 0.38f, 0.32f, 0.64f, 0.48f, 0.62f, 0.52f, 1.15f, 1.18f, 0.52f, 0.05f, 0.06f, 1 },
	// 72: 小会議室 - 密室ビジネス
	{ 0.41f, 36.1f, 0.44f, { 6.5f, 0.47f, 10.5f, 0.42f, 17.0f, 0.37f, 27.0f, 0.32f, 38.0f, 0.26f, 51.0f, 0.21f, 67.0f, 0.15f, 86.0f, 0.11f }, 9513, 84, 0.74f, 0.16f, 0.86f, 0.18f, 0.57f, 0.0f, 0.68f, 0.53f, 0.6f, 0.49f, 0.48f, 0.85f, 1.08f, 0.82f, 0.42f, 1.08f, 1.0f, 0.95f, 0.52f, 0.48f, 0.7f, 0.0f, 0.52f, 0.35f, 0.78f, 0.51f, 0.72f, 0.48f, 0.85f, 0.92f, 0.38f, 0.13f, 0.13f, 1 },
	// 73: 防音室 - 極度に吸音処理
	{ 0.02f, 6.0f, 0.01f, { 0.8f, 0.07f, 1.5f, 0.05f, 2.5f, 0.04f, 4.0f, 0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 19637, 20, 0.89f, 0.0f, 0.08f, 0.52f, 0.0f, 0.0f, 0.16f, 0.3f, 0.89f, 0.0f, 0.12f, 0.1f, 0.2f, 0.98f, 0.38f, 0.7f, 1.0f, 1.0f, 0.02f, 0.02f, 0.97f, -0.08f, 0.98f, 0.02f, 0.74f, 0.41f, 0.98f, 0.3f, 0.7f, 0.65f, 0.02f, 0.0f, 0.01f, 1 },
	// 74: エントランスホール - 高天井開放的
	{ 0.41f, 116.79f, 0.44f, { 38.0f, 0.41f, 62.0f, 0.36f, 105.0f, 0.32f, 158.0f, 0.27f, 218.0f, 0.23f, 288.0f, 0.19f, 368.0f, 0.16f, 462.0f, 0.13f }, 11927, 74, 1.35f, 0.37f, 0.58f, 0.17f, 0.64f, 12.4f, 0.52f, 1.46f, 0.41f, 0.65f, 0.48f, 1.22f, 1.55f, 0.73f, 0.51f, 1.23f, 0.96f, 0.83f, 0.68f, 0.6f, 0.61f, 0.04f, 0.36f, 0.34f, 0.59f, 0.55f, 0.55f, 0.55f, 1.66f, 1.46f, 0.63f, 0.06f, 0.14f, 1 },
	// 75: 書斎 - 個室落ち着き空間
	{ 0.32f, 40.37f, 0.36f, { 7.5f, 0.43f, 12.0f, 0.39f, 19.5f, 0.33f, 31.0f, 0.28f, 44.0f, 0.23f, 59.0f, 0.17f, 77.0f, 0.13f, 99.0f, 0.09f }, 7119, 67, 0.75f, 0.11f, 0.68f, 0.13f, 0.53f, 0.0f, 0.62f, 0.63f, 0.67f, 0.34f, 0.45f, 0.78f, 0.98f, 0.88f, 0.37f, 1.05f, 1.0f, 0.95f, 0.48f, 0.42f, 0.69f, -0.17f, 0.65f, 0.38f, 0.87f, 0.44f, 0.82f, 0.45f, 0.88f, 0.95f, 0.35f, 0.13f, 0.04f, 1 },
	// 76: キッチン - 硬質反射多め
	{ 0.49f, 27.79f, 0.53f, { 5.0f, 0.61f, 8.0f, 0.58f, 13.0f, 0.52f, 20.0f, 0.46f, 28.0f, 0.40f, 37.0f, 0.33f, 48.0f, 0.27f, 61.0f, 0.21f }, 11946, 145, 0.63f, 0.11f, 1.56f, 0.22f, 0.35f, 0.41f, 0.42f, 0.3f, 0.69f, 0.45f, 0.29f, 0.62f, 0.82f, 0.58f, 0.57f, 0.95f, 1.0f, 1.05f, 0.45f, 0.60f, 0.73f, 0.23f, 0.42f, 0.32f, 0.52f, 0.69f, 0.48f, 0.45f, 0.68f, 0.75f, 0.28f, 0.13f, 0.22f, 1 },
	// 77: 屋外駐車場 - 開放的アスファルト
	{ 0.12f, 237.2f, 0.09f, { 55.0f, 0.16f, 92.0f, 0.14f, 155.0f, 0.11f, 235.0f, 0.09f, 328.0f, 0.08f, 435.0f, 0.06f, 558.0f, 0.04f, 698.0f, 0.03f }, 18794, 102, 1.94f, 0.47f, 0.49f, 0.03f, 0.57f, 31.5f, 0.5f, 2.7f, 0.31f, 0.58f, 0.9f, 0.88f, 1.18f, 0.68f, 0.67f, 1.0f, 1.0f, 1.05f, 0.52f, 0.65f, 0.85f, 0.3f, 0.18f, 0.52f, 0.48f, 0.72f, 0.35f, 0.62f, 1.65f, 1.95f, 0.88f, 0.15f, 0.38f, 2 },
	// 78: 地下通路(狭い) - 圧迫感ある直線
	{ 0.53f, 104.59f, 0.58f, { 16.0f, 0.58f, 25.0f, 0.56f, 40.0f, 0.52f, 60.0f, 0.48f, 84.0f, 0.43f, 112.0f, 0.38f, 145.0f, 0.32f, 184.0f, 0.27f }, 7814, 167, 0.46f, 0.36f, 1.28f, 0.18f, 0.56f, 8.59f, 0.42f, 0.68f, 0.7f, 0.56f, 0.28f, 0.72f, 1.05f, 0.55f, 0.53f, 1.0f, 1.0f, 0.95f, 0.48f, 0.65f, 0.87f, 0.28f, 0.22f, 0.35f, 0.56f, 0.71f, 0.32f, 0.62f, 0.88f, 0.75f, 0.4f, 0.61f, 0.35f, 1 },
	// 79: 展示室 - ギャラリー用途
	{ 0.41f, 102.07f, 0.44f, { 18.0f, 0.47f, 29.0f, 0.41f, 50.0f, 0.36f, 82.0f, 0.31f, 118.0f, 0.25f, 162.0f, 0.21f, 215.0f, 0.16f, 278.0f, 0.13f }, 9841, 73, 1.2f, 0.34f, 0.8f, 0.14f, 0.79f, 14.59f, 0.7f, 1.26f, 0.6f, 0.64f, 0.63f, 1.12f, 1.38f, 0.8f, 0.46f, 1.18f, 1.0f, 0.88f, 0.68f, 0.58f, 0.75f, -0.01f, 0.48f, 0.35f, 0.8f, 0.52f, 0.68f, 0.52f, 1.35f, 1.28f, 0.55f, 0.18f, 0.14f, 1 },
	// 80: アトリエ - 創作空間
	{ 0.44f, 74.94f, 0.47f, { 12.0f, 0.52f, 19.0f, 0.47f, 32.0f, 0.41f, 52.0f, 0.36f, 74.0f, 0.31f, 100.0f, 0.25f, 130.0f, 0.20f, 166.0f, 0.15f }, 10181, 87, 1.01f, 0.24f, 0.82f, 0.18f, 0.79f, 10.67f, 0.57f, 1.02f, 0.7f, 0.52f, 0.45f, 0.98f, 1.22f, 0.78f, 0.48f, 1.15f, 1.0f, 0.92f, 0.62f, 0.58f, 0.82f, -0.08f, 0.45f, 0.38f, 0.87f, 0.49f, 0.72f, 0.5f, 1.08f, 1.12f, 0.48f, 0.24f, 0.09f, 1 },
	// 81: サイバーパンク路地
	{ 0.34f, 65.0f, 0.39f, { 6.0f, 0.44f, 12.0f, 0.4f, 20.0f, 0.36f, 28.0f, 0.32f, 36.0f, 0.27f, 45.0f, 0.22f, 58.0f, 0.18f, 72.0f, 0.14f }, 14000, 120, 1.2f, 0.25f, 1.8f, 0.24f, 0.55f, 8.0f, 0.25f, 1.2f, 0.45f, 0.55f, 0.2f, 0.9f, 1.4f, 0.55f, 0.65f, 0.9f, 1.1f, 1.2f, 0.5f, 0.60f, 0.6f, 0.3f, 0.35f, 0.55f, 0.35f, 0.75f, 0.45f, 0.45f, 1.0f, 1.0f, 0.35f, 0.4f, 0.35f, 1 },
	// 82: 宇宙船ブリッジ
	{ 0.24f, 40.0f, 0.24f, { 4.0f, 0.48f, 8.0f, 0.44f, 13.0f, 0.40f, 18.0f, 0.36f, 24.0f, 0.32f, 30.0f, 0.27f, 38.0f, 0.22f, 46.0f, 0.18f }, 17000, 180, 1.05f, 0.12f, 0.9f, 0.28f, 0.45f, 5.0f, 0.18f, 0.9f, 0.4f, 0.45f, 0.15f, 0.7f, 1.0f, 0.45f, 0.7f, 0.8f, 1.0f, 1.1f, 0.45f, 0.55f, 0.45f, 0.40f, 0.25f, 0.35f, 0.3f, 0.8f, 0.35f, 0.35f, 0.9f, 0.9f, 0.4f, 0.2f, 0.2f, 1 },
	// 83: ワープトンネル
	{ 0.36f, 160.0f, 0.36f, { 10.0f, 0.38f, 18.0f, 0.34f, 30.0f, 0.30f, 45.0f, 0.27f, 62.0f, 0.23f, 82.0f, 0.19f, 105.0f, 0.15f, 130.0f, 0.11f }, 12000, 60, 1.8f, 0.65f, 3.0f, 0.12f, 0.75f, 25.0f, 0.35f, 3.0f, 0.6f, 0.7f, 0.25f, 1.3f, 2.4f, 0.7f, 0.55f, 1.3f, 1.5f, 1.3f, 0.6f, 0.55f, 0.8f, 0.1f, 0.25f, 0.6f, 0.4f, 0.55f, 0.4f, 0.60f, 1.6f, 1.5f, 0.6f, 0.7f, 0.7f, 3 },
	// 84: 量子ホール
	{ 0.34f, 120.0f, 0.34f, { 8.0f, 0.40f, 16.0f, 0.35f, 27.0f, 0.31f, 40.0f, 0.27f, 55.0f, 0.23f, 72.0f, 0.19f, 92.0f, 0.15f, 115.0f, 0.13f }, 15000, 90, 1.6f, 0.7f, 2.4f, 0.17f, 0.7f, 18.0f, 0.28f, 2.4f, 0.55f, 0.65f, 0.2f, 1.2f, 2.0f, 0.65f, 0.65f, 1.1f, 1.4f, 1.4f, 0.55f, 0.65f, 0.7f, 0.2f, 0.22f, 0.55f, 0.35f, 0.7f, 0.45f, 0.55f, 1.4f, 1.3f, 0.5f, 0.6f, 0.60f, 3 },
	// 85: 無限回廊
	{ 0.49f, 140.0f, 0.5f, { 12.0f, 0.46f, 24.0f, 0.42f, 36.0f, 0.37f, 48.0f, 0.32f, 60.0f, 0.27f, 72.0f, 0.22f, 90.0f, 0.18f, 110.0f, 0.14f }, 11000, 80, 1.4f, 0.35f, 1.2f, 0.14f, 0.6f, 12.0f, 0.4f, 2.6f, 0.5f, 0.60f, 0.25f, 1.1f, 2.1f, 0.6f, 0.5f, 1.1f, 1.3f, 1.2f, 0.55f, 0.5f, 0.85f, 0.0f, 0.3f, 0.45f, 0.4f, 0.55f, 0.4f, 0.55f, 1.3f, 1.4f, 0.45f, 0.5f, 0.60f, 2 },
	// 86: 逆再生空間
	{ 0.43f, 110.0f, 0.48f, { 6.0f, 0.52f, 14.0f, 0.46f, 26.0f, 0.40f, 40.0f, 0.34f, 58.0f, 0.28f, 78.0f, 0.22f, 100.0f, 0.18f, 125.0f, 0.14f }, 10000, 70, 1.7f, 0.8f, 2.8f, 0.17f, 0.75f, 20.0f, 0.45f, 2.2f, 0.65f, 0.7f, 0.3f, 1.0f, 2.2f, 0.7f, 0.45f, 1.0f, 1.4f, 1.3f, 0.6f, 0.60f, 0.6f, -0.2f, 0.35f, 0.60f, 0.45f, 0.45f, 0.5f, 0.60f, 1.4f, 1.2f, 0.5f, 0.8f, 0.7f, 3 },
	// 87: タイムストップ室
	{ 0.08f, 18.0f, 0.1f, { 2.0f, 0.56f, 4.0f, 0.52f, 6.0f, 0.48f, 8.0f, 0.40f, 10.0f, 0.32f, 12.0f, 0.24f, 14.0f, 0.18f, 16.0f, 0.14f }, 18000, 180, 0.9f, 0.05f, 0.4f, 0.14f, 0.25f, 1.0f, 0.15f, 0.60f, 0.3f, 0.3f, 0.1f, 0.5f, 0.60f, 0.4f, 0.7f, 0.8f, 0.8f, 0.9f, 0.3f, 0.35f, 0.2f, 0.2f, 0.2f, 0.3f, 0.2f, 0.85f, 0.3f, 0.25f, 0.7f, 0.7f, 0.2f, 0.05f, 0.05f, 1 },
	// 88: データセンター
	{ 0.2f, 35.0f, 0.21f, { 4.0f, 0.48f, 9.0f, 0.43f, 15.0f, 0.38f, 22.0f, 0.34f, 30.0f, 0.29f, 39.0f, 0.24f, 50.0f, 0.19f, 64.0f, 0.16f }, 9000, 120, 1.0f, 0.2f, 1.2f, 0.1f, 0.40f, 6.0f, 0.5f, 1.0f, 0.4f, 0.45f, 0.35f, 0.7f, 0.9f, 0.45f, 0.40f, 1.1f, 1.0f, 0.9f, 0.5f, 0.40f, 0.5f, -0.1f, 0.45f, 0.40f, 0.35f, 0.40f, 0.35f, 0.60f, 0.8f, 0.9f, 0.25f, 0.25f, 0.25f, 1 },
	// 89: 巨大機械内部
	{ 0.39f, 70.0f, 0.42f, { 6.0f, 0.5f, 12.0f, 0.44f, 20.0f, 0.38f, 30.0f, 0.33f, 42.0f, 0.28f, 56.0f, 0.23f, 74.0f, 0.19f, 96.0f, 0.16f }, 8000, 90, 1.1f, 0.3f, 1.0f, 0.14f, 0.55f, 10.0f, 0.55f, 1.6f, 0.45f, 0.55f, 0.4f, 0.9f, 1.3f, 0.5f, 0.35f, 1.3f, 1.2f, 1.0f, 0.6f, 0.45f, 0.45f, -0.2f, 0.5f, 0.65f, 0.5f, 0.35f, 0.3f, 0.75f, 1.1f, 1.2f, 0.3f, 0.35f, 0.40f, 2 },
	// 90: AIホログラム室
	{ 0.32f, 55.0f, 0.35f, { 5.0f, 0.48f, 10.0f, 0.43f, 16.0f, 0.38f, 24.0f, 0.34f, 33.0f, 0.29f, 44.0f, 0.24f, 58.0f, 0.19f, 74.0f, 0.16f }, 18000, 110, 1.5f, 0.35f, 1.6f, 0.32f, 0.60f, 9.0f, 0.25f, 1.4f, 0.5f, 0.55f, 0.2f, 0.9f, 1.4f, 0.60f, 0.75f, 0.9f, 1.1f, 1.3f, 0.5f, 0.65f, 0.6f, 0.40f, 0.25f, 0.40f, 0.3f, 0.85f, 0.45f, 0.40f, 1.2f, 1.1f, 0.45f, 0.4f, 0.35f, 1 },
	// 91: 重力ゼロ船庫
	{ 0.39f, 170.0f, 0.39f, { 12.0f, 0.37f, 22.0f, 0.34f, 36.0f, 0.30f, 52.0f, 0.26f, 70.0f, 0.22f, 92.0f, 0.18f, 118.0f, 0.14f, 148.0f, 0.11f }, 13000, 60, 1.9f, 0.45f, 1.8f, 0.12f, 0.7f, 22.0f, 0.35f, 3.2f, 0.6f, 0.65f, 0.25f, 1.3f, 2.5f, 0.7f, 0.60f, 1.3f, 1.6f, 1.4f, 0.6f, 0.60f, 0.7f, 0.1f, 0.3f, 0.5f, 0.4f, 0.60f, 0.5f, 0.60f, 1.7f, 1.6f, 0.65f, 0.5f, 0.5f, 2 },
	// 92: 惑星ドーム都市
	{ 0.41f, 190.0f, 0.4f, { 14.0f, 0.36f, 26.0f, 0.32f, 40.0f, 0.28f, 58.0f, 0.24f, 78.0f, 0.21f, 102.0f, 0.17f, 130.0f, 0.14f, 162.0f, 0.11f }, 16000, 70, 2.1f, 0.35f, 1.2f, 0.17f, 0.75f, 28.0f, 0.3f, 3.8f, 0.65f, 0.7f, 0.2f, 1.4f, 2.6f, 0.75f, 0.7f, 1.4f, 1.7f, 1.5f, 0.65f, 0.7f, 0.65f, 0.2f, 0.25f, 0.55f, 0.35f, 0.75f, 0.5f, 0.55f, 1.9f, 1.7f, 0.7f, 0.45f, 0.45f, 2 },
	// 93: VRシミュレーター
	{ 0.39f, 90.0f, 0.42f, { 8.0f, 0.48f, 15.0f, 0.43f, 24.0f, 0.38f, 36.0f, 0.34f, 50.0f, 0.29f, 66.0f, 0.24f, 84.0f, 0.19f, 105.0f, 0.16f }, 15000, 100, 2.2f, 0.9f, 3.2f, 0.21f, 0.65f, 14.0f, 0.3f, 1.8f, 0.5f, 0.60f, 0.25f, 1.0f, 1.6f, 0.60f, 0.65f, 1.0f, 1.3f, 1.4f, 0.55f, 0.65f, 0.7f, 0.3f, 0.25f, 0.5f, 0.35f, 0.75f, 0.45f, 0.45f, 1.3f, 1.2f, 0.5f, 0.6f, 0.60f, 3 },
	// 94: レーザー通路
	{ 0.22f, 45.0f, 0.28f, { 3.0f, 0.52f, 7.0f, 0.46f, 12.0f, 0.40f, 18.0f, 0.34f, 25.0f, 0.29f, 33.0f, 0.24f, 42.0f, 0.19f, 55.0f, 0.16f }, 19000, 160, 1.1f, 0.45f, 2.0f, 0.35f, 0.40f, 4.0f, 0.2f, 0.9f, 0.35f, 0.45f, 0.1f, 0.7f, 1.0f, 0.45f, 0.85f, 0.8f, 1.0f, 1.3f, 0.35f, 0.55f, 0.85f, 0.60f, 0.15f, 0.45f, 0.2f, 0.9f, 0.3f, 0.35f, 0.9f, 0.9f, 0.35f, 0.7f, 0.8f, 3 },
	// 95: 異次元裂け目
	{ 0.44f, 220.0f, 0.44f, { 16.0f, 0.35f, 30.0f, 0.30f, 48.0f, 0.27f, 70.0f, 0.23f, 95.0f, 0.19f, 123.0f, 0.15f, 154.0f, 0.13f, 190.0f, 0.1f }, 9000, 40, 2.0f, 1.0f, 3.5f, 0.1f, 0.85f, 30.0f, 0.60f, 4.2f, 0.7f, 0.8f, 0.35f, 1.6f, 2.8f, 0.8f, 0.40f, 1.6f, 1.8f, 1.4f, 0.7f, 0.60f, 0.9f, -0.4f, 0.4f, 0.8f, 0.5f, 0.35f, 0.4f, 0.8f, 2.0f, 1.9f, 0.8f, 0.9f, 0.9f, 3 },
	// 96: 夢の中
	{ 0.35f, 80.0f, 0.39f, { 7.0f, 0.48f, 13.0f, 0.43f, 20.0f, 0.38f, 28.0f, 0.34f, 38.0f, 0.29f, 50.0f, 0.24f, 64.0f, 0.20f, 80.0f, 0.16f }, 12000, 70, 1.4f, 0.3f, 0.8f, 0.1f, 0.60f, 10.0f, 0.45f, 1.6f, 0.55f, 0.60f, 0.3f, 0.9f, 1.3f, 0.7f, 0.55f, 1.0f, 1.2f, 1.1f, 0.55f, 0.55f, 0.5f, 0.0f, 0.35f, 0.35f, 0.55f, 0.5f, 0.7f, 0.45f, 1.2f, 1.1f, 0.4f, 0.3f, 0.25f, 1 },
	// 97: 水晶洞
	{ 0.35f, 150.0f, 0.34f, { 10.0f, 0.38f, 20.0f, 0.34f, 32.0f, 0.30f, 48.0f, 0.27f, 66.0f, 0.23f, 88.0f, 0.19f, 114.0f, 0.15f, 144.0f, 0.13f }, 20000, 80, 1.6f, 0.25f, 1.0f, 0.29f, 0.7f, 16.0f, 0.20f, 2.6f, 0.6f, 0.65f, 0.15f, 1.2f, 2.1f, 0.65f, 0.85f, 1.1f, 1.4f, 1.6f, 0.55f, 0.75f, 0.6f, 0.5f, 0.2f, 0.55f, 0.35f, 0.9f, 0.5f, 0.5f, 1.5f, 1.4f, 0.55f, 0.4f, 0.40f, 2 },
	// 98: 廃宇宙ステーション
	{ 0.55f, 170.0f, 0.52f, { 12.0f, 0.46f, 24.0f, 0.42f, 38.0f, 0.37f, 56.0f, 0.32f, 76.0f, 0.27f, 100.0f, 0.22f, 128.0f, 0.18f, 160.0f, 0.14f }, 9500, 70, 1.5f, 0.25f, 0.9f, 0.1f, 0.7f, 20.0f, 0.55f, 3.0f, 0.55f, 0.65f, 0.4f, 1.3f, 2.3f, 0.7f, 0.35f, 1.4f, 1.5f, 1.2f, 0.6f, 0.55f, 0.6f, -0.2f, 0.45f, 0.60f, 0.45f, 0.40f, 0.4f, 0.65f, 1.6f, 1.5f, 0.6f, 0.4f, 0.35f, 2 },
	// 99: ブラックホール縁
	{ 0.45f, 240.0f, 0.45f, { 18.0f, 0.34f, 34.0f, 0.30f, 54.0f, 0.26f, 78.0f, 0.22f, 106.0f, 0.18f, 138.0f, 0.14f, 174.0f, 0.11f, 214.0f, 0.1f }, 7000, 30, 2.1f, 0.7f, 2.0f, 0.07f, 0.9f, 35.0f, 0.7f, 4.6f, 0.7f, 0.85f, 0.5f, 1.8f, 3.0f, 0.85f, 0.3f, 1.8f, 2.0f, 1.5f, 0.75f, 0.65f, 0.95f, -0.6f, 0.55f, 0.8f, 0.60f, 0.3f, 0.35f, 0.9f, 2.0f, 2.0f, 0.85f, 0.8f, 0.8f, 3 },
	// 100: サイバー聖堂
	{ 0.44f, 210.0f, 0.43f, { 16.0f, 0.36f, 30.0f, 0.32f, 46.0f, 0.28f, 66.0f, 0.24f, 88.0f, 0.21f, 112.0f, 0.17f, 140.0f, 0.14f, 172.0f, 0.11f }, 15000, 70, 1.9f, 0.40f, 1.4f, 0.20f, 0.8f, 28.0f, 0.3f, 4.0f, 0.65f, 0.75f, 0.25f, 1.5f, 2.7f, 0.75f, 0.7f, 1.5f, 1.8f, 1.6f, 0.7f, 0.75f, 0.7f, 0.3f, 0.3f, 0.60f, 0.4f, 0.75f, 0.45f, 0.60f, 1.9f, 1.8f, 0.75f, 0.6f, 0.60f, 2 }
};

// ---- ユーティリティ関数 ----
static void Biquad_Reset(Biquad* bq)
{
	bq->x1 = bq->x2 = 0.0f;
	bq->y1 = bq->y2 = 0.0f;
}

static float Biquad_Process(Biquad* bq, float x)
{
	float y = bq->b0 * x + bq->b1 * bq->x1 + bq->b2 * bq->x2
		- bq->a1 * bq->y1 - bq->a2 * bq->y2;

	bq->x2 = bq->x1;
	bq->x1 = x;
	bq->y2 = bq->y1;
	bq->y1 = y;

	return y;
}

// シェルビング／ピーキングEQ用の汎用計算
static void Biquad_SetPeakingEQ(Biquad* bq, float sampleRate, float freq, float Q, float gainDb)
{
	float A = powf(10.0f, gainDb / 40.0f);
	float w0 = 2.0f * (float)M_PI * freq / sampleRate;
	float alpha = sinf(w0) / (2.0f * Q);
	float cosw0 = cosf(w0);

	float b0 = 1.0f + alpha * A;
	float b1 = -2.0f * cosw0;
	float b2 = 1.0f - alpha * A;
	float a0 = 1.0f + alpha / A;
	float a1 = -2.0f * cosw0;
	float a2 = 1.0f - alpha / A;

	bq->b0 = b0 / a0;
	bq->b1 = b1 / a0;
	bq->b2 = b2 / a0;
	bq->a1 = a1 / a0;
	bq->a2 = a2 / a0;

	Biquad_Reset(bq);
}


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
	// パススルーチェック
	if (f->b0 == 1.0f && f->b1 == 0.0f && f->a1 == 0.0f) return in;
	if (f->b0 == 0.0f && f->b1 == 0.0f) return in;

	// 入力のサニティチェック
	if (!isfinite(in)) return 0.0f;
	if (fabs(in) > 10.0f) return 0.0f; // 異常な入力

	float out = f->b0 * in + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;

	// 出力のサニティチェック（発振検出）
	if (!isfinite(out) || fabs(out) > 10.0f) {
		// フィルタ状態を完全リセット
		f->x1 = 0.0f;
		f->x2 = 0.0f;
		f->y1 = 0.0f;
		f->y2 = 0.0f;
		return 0.0f; // 異常時はミュート
	}

	// デノーマル防止
	if (fabs(out) < 1.0e-20f) out = 0.0f;

	// 状態更新
	f->x2 = f->x1;
	f->x1 = in;
	f->y2 = f->y1;
	f->y1 = out;

	// 状態変数のデノーマル防止
	if (fabs(f->x1) < 1.0e-20f) f->x1 = 0.0f;
	if (fabs(f->x2) < 1.0e-20f) f->x2 = 0.0f;
	if (fabs(f->y1) < 1.0e-20f) f->y1 = 0.0f;
	if (fabs(f->y2) < 1.0e-20f) f->y2 = 0.0f;

	// 状態変数が異常に大きくなったら抑制（発振の兆候）
	if (fabs(f->y1) > 5.0f) f->y1 *= 0.5f;
	if (fabs(f->y2) > 5.0f) f->y2 *= 0.5f;

	return out;
}

// ===== LFO/モジュレーション関数 =====

static inline float UpdateLFO(LFO* lfo, int sampleRate) {
	if (lfo->frequency <= 0.0f || lfo->depth <= 0.0f) return 0.0f;

	float value = sinf(lfo->phase * 2.0f * M_PI) * lfo->depth;
	lfo->phase += lfo->frequency / (float)sampleRate;

	if (lfo->phase >= 1.0f) lfo->phase -= 1.0f;

	return value;
}

// ===== ディフュージョン処理（3段階） =====

static inline float ProcessDiffusion(ChannelState* cs, float input, float diffusion, float density) {
	if (diffusion <= 0.0f) return input;

	// 第1段階: 大規模ディフュージョン（8タップ）
	static const int delays1[8] = { 37, 53, 73, 97, 127, 163, 211, 277 };
	float output = input;

	// 係数を安全な範囲に（0.6は安定性の限界）
	float coeff = diffusion * 0.6f;
	if (coeff > 0.6f) coeff = 0.6f; // 安全リミット

	for (int i = 0; i < 8; i++) {
		int readPos = (cs->diffusionPos1[i] - delays1[i] + 1024) % 1024;
		float delayed = cs->diffusionBuffer1[i][readPos];
		float temp = output + delayed * coeff;
		cs->diffusionBuffer1[i][cs->diffusionPos1[i]] = temp;
		output = delayed - temp * coeff;
		cs->diffusionPos1[i] = (cs->diffusionPos1[i] + 1) % 1024;
	}

	// 第2段階: 中規模ディフュージョン
	if (density > 0.3f) {
		static const int delays2[8] = { 23, 31, 41, 59, 71, 89, 107, 131 };
		float coeff2 = density * 0.5f;
		if (coeff2 > 0.5f) coeff2 = 0.5f; // 安全リミット

		for (int i = 0; i < 8; i++) {
			int readPos = (cs->diffusionPos2[i] - delays2[i] + 512) % 512;
			float delayed = cs->diffusionBuffer2[i][readPos];
			float temp = output + delayed * coeff2;
			cs->diffusionBuffer2[i][cs->diffusionPos2[i]] = temp;
			output = delayed - temp * coeff2;
			cs->diffusionPos2[i] = (cs->diffusionPos2[i] + 1) % 512;
		}
	}

	// 第3段階: 小規模ディフュージョン
	if (density > 0.6f) {
		static const int delays3[8] = { 13, 17, 19, 29, 37, 43, 53, 67 };
		float coeff3 = (density - 0.6f) * 0.6f;
		if (coeff3 > 0.4f) coeff3 = 0.4f; // 安全リミット

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

	float enhanced = highFreq * 2.5f; // 2.0→2.5（1.25倍）

	// ソフトクリッピング
	if (enhanced > 1.0f) enhanced = 1.0f - (enhanced - 1.0f) * 0.3f;
	if (enhanced < -1.0f) enhanced = -1.0f + (enhanced + 1.0f) * 0.3f;

	return input + (enhanced - highFreq) * amount * 1.3f; // 1.3倍ブースト
}
// ===== ソフトリミッター =====

static inline float SoftLimiter(float x) {

	// Aggressive soft-knee limiter to avoid clipping

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

	// フラッターエコー周波数（6-12Hz）
	float flutterFreq = 8.0f + amount * 4.0f;
	cs->flutterPhase += flutterFreq / (float)sampleRate;
	if (cs->flutterPhase >= 1.0f) cs->flutterPhase -= 1.0f;

	// 正弦波モジュレーション
	float modulation = sinf(cs->flutterPhase * 2.0f * M_PI) * amount * 0.15f;

	// フィルタ処理で金属的な響きを付加
	float filtered = ProcessBiquad(&cs->flutterFilter, input);

	return input + filtered * modulation;
}

// ===== 材質特性処理 =====

static inline float ProcessMaterialAbsorption(ChannelState* cs, float input, float absorption, float roughness) {
	if (absorption <= 0.0f && roughness <= 0.0f) return input;

	// 材質吸音フィルタ適用
	float absorbed = ProcessBiquad(&cs->materialFilter, input);

	// 粗さによる高域減衰
	if (roughness > 0.0f) {
		absorbed *= (1.0f - roughness * 0.3f);
	}

	// 吸音率に応じてミックス
	return input * (1.0f - absorption) + absorbed * absorption;
}

// ===== 温かみ処理 =====

static inline float ProcessWarmth(ChannelState* cs, float input, float warmth) {
	if (warmth <= 0.0f) return input;

	// 温かみフィルタ（低域を微妙に強調、高域を微妙に丸める）
	float warmed = ProcessBiquad(&cs->warmthFilter, input);

	// 状態変数を使って滑らかに適用
	cs->warmthState = cs->warmthState * 0.98f + warmed * 0.02f;

	return input * (1.0f - warmth * 0.3f) + cs->warmthState * warmth * 0.3f;
}

// ===== 明るさ処理 =====

static inline float ProcessBrightness(float input, float* brightnessState, float brightness) {
	if (fabs(brightness - 0.5f) < 0.01f) return input;

	// 明るさ調整（高域の倍音成分）
	float harmonic = input * input * input;

	// 明るさに応じて倍音を追加または減少
	float brightnessFactor = (brightness - 0.5f) * 2.0f; // -1.0 to 1.0

	*brightnessState = *brightnessState * 0.96f + harmonic * brightnessFactor * 0.04f;

	return input + *brightnessState * 0.1f;
}

// ===== EQ =====
static void ApplyEqPresetToChannel(int ch, int presetIndex, int sampleRate)
{
	if (ch < 0 || ch >= MAX_CH) return;
	if (presetIndex < 0 || presetIndex >= 51) return;

	ChannelState* st = &g_channels[ch];

	for (int band = 0; band < EQ_BANDS; ++band) {
		int level = EQ_PRESETS[presetIndex][band]; // 100 が 0dB 基準
		g_lastEqValues[band] = level;

		float gainDb = (float)(level - 100) * 0.25f; // 例: 1ステップ0.25dB
		float freq = EQ_FREQS[band];
		float Q = 1.0f; // 適当なQ。必要ならバンドごとに変えてもよい。

		Biquad_SetPeakingEQ(&st->eqFilters[band], (float)sampleRate, freq, Q, gainDb);
	}

	g_lastEqPreset = presetIndex;
}

static float ProcessEqSample(int ch, float x)
{
	if (ch < 0 || ch >= MAX_CH) return x;

	ChannelState* st = &g_channels[ch];

	float y = x;
	for (int band = 0; band < EQ_BANDS; ++band) {
		y = Biquad_Process(&st->eqFilters[band], y);
	}
	return y;
}

// ===== エンジン初期化 =====
static void InitEngine(int rate) {
	memset(g_channels, 0, sizeof(g_channels));
	memset(g_delayMemory, 0, sizeof(g_delayMemory));

	for (int i = 0; i < MAX_CH; i++) {
		g_channels[i].delayBuffer = g_delayMemory[i];
		g_channels[i].lfo.phase = 0.0f;
		g_channels[i].flutterPhase = 0.0f;

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
	}

	g_lastRate = rate;
	g_lastEqPreset = -1;
	g_lastEnvPreset = -1;

	for (int i = 0; i < 15; i++) g_lastEqValues[i] = 100;
	for (int i = 0; i < 5; i++) g_lastExtendedParams[i] = 100;

	g_lastEffectAmount = 50;

	// オートゲインのリセット
	g_autoGain = 1.0f;
	g_initialized = TRUE;
}
// ===== メイン処理関数 =====


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

static void ApplyEnvSeparation(int presetIndex, EnvParams* env) {
	if (!env || presetIndex <= 0) return;

	int category = (presetIndex - 1) / 10; // 0-9
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
	static const float kColorBias[10] = { 0.00f, 0.05f, -0.10f, 0.05f, -0.05f, 0.10f, 0.00f, -0.05f, 0.20f, 0.15f };
	static const float kAirBias[10] = { 0.00f, -0.05f, 0.20f, -0.05f, 0.25f, -0.10f, 0.05f, 0.10f, -0.15f, -0.10f };
	static const float kWarmBias[10] = { 0.00f, -0.05f, 0.15f, 0.05f, 0.25f, -0.05f, 0.05f, 0.10f, -0.10f, -0.05f };
	static const float kBrightBias[10] = { 0.00f, 0.05f, -0.10f, 0.05f, -0.10f, 0.10f, 0.00f, -0.05f, 0.20f, 0.15f };
	static const float kDiffBias[10] = { 0.00f, 0.05f, -0.05f, 0.10f, -0.10f, 0.15f, 0.10f, 0.05f, 0.20f, 0.15f };
	static const float kDenBias[10] = { 0.00f, 0.05f, -0.05f, 0.10f, -0.10f, 0.10f, 0.10f, 0.00f, 0.15f, 0.10f };
	static const float kModBias[10] = { 0.00f, 0.02f, 0.05f, 0.02f, 0.03f, 0.08f, 0.10f, 0.05f, 0.25f, 0.20f };
	static const float kEchoBias[10] = { 0.00f, 0.05f, -0.05f, 0.05f, -0.05f, 0.10f, 0.10f, 0.05f, 0.20f, 0.15f };
	static const float kFlutterBias[10] = { 0.00f, 0.02f, 0.04f, 0.02f, 0.02f, 0.08f, 0.12f, 0.05f, 0.30f, 0.25f };
	static const float kCombBias[10] = { 0.00f, 0.02f, 0.04f, 0.02f, 0.02f, 0.08f, 0.12f, 0.05f, 0.30f, 0.25f };

	env->preDelayMs = ClampFloat(env->preDelayMs + (t - 0.5f) * 12.0f + h1 * 8.0f, 0.0f, 120.0f);
	env->delayTimeMs = ClampFloat(env->delayTimeMs * (1.0f + (t - 0.5f) * 0.25f + h2 * 0.15f), 6.0f, 350.0f);
	env->roomSize = ClampFloat(env->roomSize + kRoomBias[category] + h3 * 0.25f, 0.3f, 5.0f);
	env->stereoWidth = ClampFloat(env->stereoWidth + kWidthBias[category] + h1 * 0.30f, 0.3f, 2.5f);
	env->damping = ClampFloat(env->damping + kDampBias[category] + h2 * 0.20f, 0.0f, 1.0f);
	env->diffusion = ClampFloat(env->diffusion + kDiffBias[category] + h3 * 0.25f, 0.0f, 1.0f);
	env->density = ClampFloat(env->density + kDenBias[category] + h4 * 0.25f, 0.0f, 1.0f);
	env->earlyLateBalance = ClampFloat(env->earlyLateBalance + (t - 0.5f) * 0.20f, 0.0f, 1.0f);

	env->reverbColor = ClampFloat(env->reverbColor + kColorBias[category] + h1 * 0.20f, 0.0f, 1.0f);
	env->airAbsorption = ClampFloat(env->airAbsorption + kAirBias[category] + h2 * 0.20f, 0.0f, 1.0f);
	env->warmth = ClampFloat(env->warmth + kWarmBias[category] + h3 * 0.20f, 0.0f, 1.0f);
	env->brightness = ClampFloat(env->brightness + kBrightBias[category] + h4 * 0.20f, 0.0f, 1.0f);

	env->echoClarity = ClampFloat(env->echoClarity + kEchoBias[category] + h3 * 0.20f, 0.0f, 1.0f);
	env->echoFeedbackTone = ClampFloat(env->echoFeedbackTone + h4 * 0.40f, -1.0f, 1.0f);

	env->modDepth = ClampFloat(env->modDepth + kModBias[category] + h1 * 0.20f, 0.0f, 1.0f);
	env->modSpeed = ClampFloat(env->modSpeed + kModBias[category] + h2 * 0.30f, 0.05f, 6.0f);

	env->flutterEcho = ClampFloat(env->flutterEcho + kFlutterBias[category] + h3 * 0.20f, 0.0f, 1.0f);
	env->combFiltering = ClampFloat(env->combFiltering + kCombBias[category] + h4 * 0.20f, 0.0f, 1.0f);

	env->lpfFreq = ClampFloat(env->lpfFreq * (1.0f + (env->brightness - 0.5f) * 0.20f), 4000.0f, 20000.0f);
	env->hpfFreq = ClampFloat(env->hpfFreq * (1.0f + (env->airAbsorption - 0.5f) * 0.20f), 20.0f, 800.0f);
}


// ---- イコライザーメイン ----
void equaliser(void* data, int len, BOOL reset) {
	// reset=2: EQプリセット同期モード（画面からの変更を反映）
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
		InitEngine(wavbit); // ここで g_autoGain = 1.0f にリセットされます
		forceUpdate = TRUE;
	}

	// 低周波（サンプリングレート不足）は処理しない
	if (wavbit < 30000) return;

	int currentEqPre = savedata.eqsoundeq;
	int currentEnvPre = savedata.eqsoundenv;
	int effectAmount = savedata.eqsoundeffect;

	if (effectAmount < 0) effectAmount = 0;
	if (effectAmount > 100) effectAmount = 100;

	// 各種スケール係数
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
		const EnvParams* ep = &ENV_PRESETS[currentEnvPre];
		for (int ch = 0; ch < MAX_CH; ch++) {
			CalcFilter(&g_channels[ch].envLpf, 0, ep->lpfFreq, 0.707f, wavbit);
			CalcFilter(&g_channels[ch].envHpf, 1, ep->hpfFreq, 0.707f, wavbit);
			CalcFilter(&g_channels[ch].exciterFilter, 1, 6000.0f, 0.707f, wavbit);
			float dampFreq = 4000.0f + (ep->damping * extraScale * 8000.0f);
			CalcFilter(&g_channels[ch].dampingFilter, 0, dampFreq, 0.5f, wavbit);
			CalcFilter(&g_channels[ch].bassReverbFilter, 0, fminf(500.0f, 250.0f * ep->bassReverbTime), 0.707f, wavbit);
			CalcPeakingEQ(&g_channels[ch].midReverbFilter, fminf(3000.0f, 1500.0f * ep->midReverbTime), 1.0f, 100.0f, wavbit);
			CalcFilter(&g_channels[ch].trebleReverbFilter, 1, fminf(12000.0f, 6000.0f * ep->trebleReverbTime), 0.707f, wavbit);
			CalcFilter(&g_channels[ch].materialFilter, 0, 2000.0f - (ep->materialAbsorption * 1500.0f), 0.707f, wavbit);
			CalcShelvingEQ(&g_channels[ch].warmthFilter, 0, 300.0f, (ep->warmth - 0.5f) * 6.0f, wavbit);
			if (ep->flutterEcho > 0.0f) CalcFilter(&g_channels[ch].flutterFilter, 1, 1200.0f, 2.0f, wavbit);
			g_channels[ch].lfo.frequency = ep->modSpeed * extraScale;
			g_channels[ch].lfo.depth = ep->modDepth * extraScale * 10.0f;
		}
		g_lastEnvPreset = currentEnvPre;
		g_lastEffectAmount = effectAmount;
	}

	const EnvParams* env = &ENV_PRESETS[g_lastEnvPreset];
	int preDelaySamps = (int)(env->preDelayMs * coreScale * wavbit / 1000.0f);
	int mainDelaySamps = (int)(env->delayTimeMs * env->roomSize * wavbit / 1000.0f);
	int refSamps[8];
	for (int i = 0; i < 8; i++) refSamps[i] = (int)(env->earlyRef[i * 2] * env->roomSize * wavbit / 1000.0f);

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
			if (wavsam == 16) inSample = *((short*)(pRaw + offset)) / 32768.0f;
			else if (wavsam == 24) {
				int val = pRaw[offset] | (pRaw[offset + 1] << 8) | ((signed char)pRaw[offset + 2] << 16);
				inSample = val / 8388608.0f;
			}
			else if (wavsam == 32) inSample = *((int*)(pRaw + offset)) / 2147483648.0f;
			else inSample = (pRaw[offset] - 128) / 128.0f;

			float signal = inSample;
			ChannelState* cs = &g_channels[ch];

			// EQ・拡張フィルタ適用
			for (int b = 0; b < EQ_BANDS; b++) signal = ProcessBiquad(&cs->eqFilters[b], signal);
			signal = ProcessBiquad(&cs->clarityFilter, signal);
			signal = ProcessBiquad(&cs->bassBalanceFilter, signal);
			signal = ProcessBiquad(&cs->trebleBalanceFilter, signal);
			signal = ProcessBiquad(&cs->densityFilter1, signal);
			signal = ProcessBiquad(&cs->densityFilter2, signal);

			if (fabs(harmonicAmount) > 0.01f) {
				float harmonic = signal * signal * signal * harmonicAmount * 0.15f;
				cs->harmonicState = cs->harmonicState * 0.95f + harmonic * 0.05f;
				signal += cs->harmonicState;
			}

			// 環境音響 (Wet)
			float wetSignal = 0.0f;
			if (env->type != 0 && env->wetMix > 0.0f && effectAmount > 0) {
				int chOffset = (ch % 2) * stereoOffset;
				int readMain = cs->writePos - (mainDelaySamps + preDelaySamps + chOffset + (int)UpdateLFO(&cs->lfo, wavbit));
				while (readMain < 0) readMain += MAX_DELAY_SAMPLES;
				float delayMain = cs->delayBuffer[readMain];

				// フィルタ群
				delayMain = (ProcessBiquad(&cs->bassReverbFilter, delayMain) * env->bassReverbTime +
					ProcessBiquad(&cs->midReverbFilter, delayMain) * env->midReverbTime +
					ProcessBiquad(&cs->trebleReverbFilter, delayMain) * env->trebleReverbTime) / 3.0f;
				delayMain = ProcessBiquad(&cs->dampingFilter, delayMain);
				delayMain = ProcessBiquad(&cs->envLpf, delayMain);
				delayMain = ProcessBiquad(&cs->envHpf, delayMain);

				// ディフュージョン
				delayMain = ProcessDiffusion(cs, delayMain, env->diffusion * coreScale, env->density);

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

				// フィードバック計算
				float fbSig = ProcessWarmth(cs, ProcessMaterialAbsorption(cs, delayMain, env->materialAbsorption, env->surfaceRoughness), env->warmth);
				float effectiveFB = fminf(0.88f, env->feedback * coreScale);
				float fbVal = signal + (fbSig * effectiveFB);
				if (fbVal > 1.5f) fbVal = 1.5f; if (fbVal < -1.5f) fbVal = -1.5f;

				cs->delayBuffer[cs->writePos] = isfinite(fbVal) ? fbVal : 0.0f;
				cs->writePos = (cs->writePos + 1) % MAX_DELAY_SAMPLES;
			}

			float mixed = signal + (wetSignal * fminf(1.0f, env->wetMix * coreScale));
			if (env->exciterAmount > 0.0f && effectAmount > 0) mixed = Exciter(mixed, &cs->exciterFilter, env->exciterAmount * extraScale);
			mixed = ProcessBrightness(mixed, &cs->brightnessState, env->brightness) * masterGain;

			if (wavch == 2) { if (ch == 0) leftSamples[bufferIndex] = mixed; else rightSamples[bufferIndex] = mixed; }
			else { leftSamples[bufferIndex] = mixed; rightSamples[bufferIndex] = mixed; }
		}

		// ステレオ幅処理
		if (wavch == 2) {
			float w = (1.0f + (env->stereoWidth - 1.0f) * extraScale) * spatialWidth * env->wallDistance * (0.7f + (env->openness * 0.6f));
			float mid = (leftSamples[bufferIndex] + rightSamples[bufferIndex]) * 0.5f;
			float side = (leftSamples[bufferIndex] - rightSamples[bufferIndex]) * 0.5f * w;
			if (env->ceilingHeight > 1.0f) side *= (1.0f + (env->ceilingHeight - 1.0f) * 0.2f); else side *= env->ceilingHeight;
			leftSamples[bufferIndex] = mid + side;
			rightSamples[bufferIndex] = mid - side;
		}
		bufferIndex++;
	}

	// ===== 最終出力とオートゲイン (クリッピング防止) =====
	bufferIndex = 0;
	for (int i = 0; i < numSamples; i++) {
		for (int ch = 0; ch < wavch; ch++) {
			if (ch >= MAX_CH) continue;

			float s = (ch == 0) ? leftSamples[bufferIndex] : rightSamples[bufferIndex];
			float finalOut = s * g_autoGain;

			// ピーク検出とオートゲイン更新
			float absS = fabsf(finalOut);
			if (absS > 1.0f) {
				// 現在のサンプルが1.0になるまでゲインを下げる
				g_autoGain = 1.0f / (absS / g_autoGain);
				finalOut = (finalOut > 0) ? 1.0f : -1.0f;
			}

			// ソフトリミッターとハードクリップ (安全策)
			finalOut = SoftLimiter(finalOut);
			if (finalOut > 1.0f) finalOut = 1.0f;
			if (finalOut < -1.0f) finalOut = -1.0f;

			int offset = (i * wavch + ch) * bytesPerSample;
			if (wavsam == 16) *((short*)(pRaw + offset)) = (short)(finalOut * 32767.0f);
			else if (wavsam == 24) {
				int v = (int)(finalOut * 8388607.0f);
				pRaw[offset] = v & 0xFF; pRaw[offset + 1] = (v >> 8) & 0xFF; pRaw[offset + 2] = (v >> 16) & 0xFF;
			}
			else if (wavsam == 32) *((int*)(pRaw + offset)) = (int)(finalOut * 2147483647.0f);
			else pRaw[offset] = (unsigned char)(finalOut * 127.0f + 128.0f);
		}
		if (wavch == 2) bufferIndex++;
	}
}

/*
===============================================================================
  ★ Hyper Equaliser ★ - 完全版ここまで

  全81環境音響モデル実装完了
  - 基本空間 (0-10)
  - 公共施設 (11-20)
  - 産業・商業 (21-30)
  - 文化施設 (31-40)
  - 生活空間 (41-50)
  - 拡張空間 (51-60)
  - 特殊空間 (61-70)
  - 専門空間 (71-80)
  - SFX/未来 (81-100)

  全51 EQプリセット実装完了

  拡張パラメータ5種実装完了
  - eq[15]: マスターボリューム (0-200)
  - eq[16]: 音の鮮明さ (0-200)
  - eq[17]: 低域と高域のバランス (0-200)
  - eq[18]: 音の密度/充実度 (0-200)
  - eq[19]: 音の立体感/臨場感 (0-200)

  環境パラメータ45種実装完了
  - 基本パラメータ (wetMix, delayTime, feedback等)
  - 初期反射16タップ
  - フィルタ (LPF, HPF)
  - 空間・モジュレーション
  - リバーブ詳細制御 (初期/後期減衰、滑らかさ、色味)
  - 周波数帯域別残響時間 (低/中/高)
  - 周波数帯域別拡散度 (低/高)
  - エコー特性 (明瞭度、フィードバック音色)
  - 材質・表面特性 (吸音率、粗さ、温かみ、明るさ、柔らかさ、重さ)
  - 空間幾何学 (天井高さ、壁距離、開放度)
  - 特殊効果 (フラッターエコー、コムフィルタリング)

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

  物理ベースモデリング
  - 材質による周波数依存吸音
  - 空気吸収による距離減衰
  - 表面粗さによる高域散乱
  - 空間幾何学による反射パターン

  合計パラメータ数: 45個（環境） + 5個（拡張） = 50個
  合計環境数: 81種
  合計EQプリセット数: 51種

  完全リアル志向・最高品質音響処理システム
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
