// Render.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "OSVersion.h"
#include "Render.h"
#include "Graph.h"
#include "dsound.h"
#include "ZeroFol.h"
#include "oggDlg.h"
#include "CImageBase.h"
#include "AudioUpscaler.h"
#include <mutex>

extern IGraphBuilder *pGraphBuilder;
extern std::mutex cl2;
extern ULONG oldw;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern save savedata;
CImageBase* renderbase;

extern int sek;
extern void DoEvent();
extern int fade1;
extern int wavbit, wavch, wavsam;
extern LPDIRECTSOUND8 m_ds;
extern LPDIRECTSOUNDBUFFER m_dsb1;
extern LPDIRECTSOUNDBUFFER8 m_dsb;
extern LPDIRECTSOUNDBUFFER m_p;

// mp3.h と同一（Render.cpp から mp3.h を include しない: CWread 内が ExtractI4 等に依存）
#define RENDER_DS_BUFSZ ((UINT)10240 * 6 / 2)
#define RENDER_DS_BUFNUM 5

static void RenderRecreateSecondarySound(COggDlg* og)
{
	if (!og || !og->m_hWnd || !m_ds)
		return;
	std::lock_guard<std::mutex> guard(cl2);
	ConfigurePlaybackOutputAndUpscaler();
	ResetAudioUpscalerPipeline();
	sek = 1;
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

	fade1 = 0;
	for (;;) {
		DSBUFFERDESC dsbd;
		ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
		dsbd.dwSize = sizeof(DSBUFFERDESC);
		dsbd.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME;
		dsbd.dwBufferBytes = g_ds_buffer_bytes;
		wfx1.nSamplesPerSec = dsTryRate;
		wfx1.nAvgBytesPerSec = (DWORD)wfx1.nSamplesPerSec * (DWORD)wfx1.nBlockAlign;
		wfx.Format.nSamplesPerSec = dsTryRate;
		wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
		if (g_ds_pcm_ch > 2)
			dsbd.lpwfxFormat = (LPWAVEFORMATEX)&wfx;
		else
			dsbd.lpwfxFormat = &wfx1;

		og->ReleaseDXSound();
		if (og->WASAPIInit() == 0)
			og->init(og->m_hWnd, dsTryRate);
		HRESULT r = m_ds->CreateSoundBuffer(&dsbd, &m_dsb1, NULL);
		if (m_dsb1 == NULL || m_p == NULL) {
			dsTryRate -= 1000;
			if (dsTryRate <= 0) {
				og->ReleaseDXSound();
				if (og->WASAPIInit() == 0)
					og->init(og->m_hWnd, 44100);
				return;
			}
			wfx.Format.nSamplesPerSec = dsTryRate;
			wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
			wfx1.nSamplesPerSec = dsTryRate;
			wfx1.nAvgBytesPerSec = wfx1.nSamplesPerSec * wfx1.nBlockAlign;
			continue;
		}
		int i;
		for (i = 0; i < 10; i++) {
			r = m_dsb1->QueryInterface(IID_IDirectSoundBuffer8, (void**)&m_dsb);
			if (m_dsb == NULL) { DoEvent(); Sleep(100); continue; }
			break;
		}
		if (m_dsb == NULL)
			return;
		g_ds_pcm_rate = dsTryRate;
		{
			int srcBits = abs(wavsam);
			if (wavsam < 0)
				srcBits = 16;
			if (!(srcBits == 8 || srcBits == 16 || srcBits == 24 || srcBits == 32))
				srcBits = 16;
			g_audioUpscaler.Configure(wavbit, wavch, srcBits, g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits);
			g_pcm_upscale_active = g_audioUpscaler.IsActive() ? 1 : 0;
		}
		g_audioUpscaler.Reset();
		oldw = 0;
		m_dsb->Play(0, 0, DSBPLAY_LOOPING);
		return;
	}
}

/////////////////////////////////////////////////////////////////////////////
// CRender ダイアログ

IMPLEMENT_DYNAMIC(CRender, CCustomBlurDialogExBase)
CRender::CRender(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogExBase(CRender::IDD, pParent)
{
	//{{AFX_DATA_INIT(CRender)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
	//}}AFX_DATA_INIT
}


void CRender::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CRender)
	DDX_Control(pDX, IDC_COMBO1, m_1);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDC_CHECK1, m_evr);
	DDX_Control(pDX, IDC_CHECK2, m_con);
	DDX_Control(pDX, IDC_CHECK3, m_a);
	DDX_Control(pDX, IDC_CHECK27, m_ffd);
	DDX_Control(pDX, IDCANCEL2, m_l);
	DDX_Control(pDX, IDC_CHECK30, m_vob);
	DDX_Control(pDX, IDC_CHECK31, m_haali);
	DDX_Control(pDX, IDC_CHECK32, m_spc2x);
	DDX_Control(pDX, IDC_CHECK33, m_spc4x);
	DDX_Control(pDX, IDC_CHECK34, m_spc8x);
	DDX_Control(pDX, IDC_CHECK35, m_spc1x);
	DDX_Control(pDX, IDC_CHECK36, m_spc16x);
	DDX_Control(pDX, IDC_CHECK40, m_mp31);
	DDX_Control(pDX, IDC_CHECK37, m_mp315);
	DDX_Control(pDX, IDC_CHECK38, m_mp32);
	DDX_Control(pDX, IDC_CHECK39, m_mp325);
	DDX_Control(pDX, IDC_CHECK41, m_mp33);
	DDX_Control(pDX, IDC_CHECK45, m_kpi10);
	DDX_Control(pDX, IDC_CHECK42, m_kpi15);
	DDX_Control(pDX, IDC_CHECK43, m_kpi20);
	DDX_Control(pDX, IDC_CHECK44, m_kpi25);
	DDX_Control(pDX, IDC_CHECK46, m_kpi30);
	DDX_Control(pDX, IDCANCEL3, m_kpi);
	DDX_Control(pDX, IDC_CHECK47, m_mp3orig);
	DDX_Control(pDX, IDC_CHECK48, m_audiost);
	DDX_Control(pDX, IDC_CHECK49, m_24);
	DDX_Control(pDX, IDC_CHECK50, m_m4a);
	DDX_Control(pDX, IDC_CHECK51, m_32bit);
	DDX_Control(pDX, IDC_SLIDER3, m_ms);
	DDX_Control(pDX, IDC_STATIC9, m_ms2);
	DDX_Control(pDX, IDC_SLIDER5, m_hyouji2);
	DDX_Control(pDX, IDC_STATIC10, m_hyouji3);
	DDX_Control(pDX, IDC_COMBO2, m_soundlist);
	DDX_Control(pDX, IDC_BUTTON1, m_ao);
	DDX_Control(pDX, IDC_COMBO3, m_Hz);
	DDX_Control(pDX, IDC_STATIC12, m_wup);
	DDX_Control(pDX, IDC_SLIDER6, w_wups);
	DDX_Control(pDX, IDC_CHECK52, m_speana);
	DDX_Control(pDX, IDC_COMBO4, m_speana_num);
	DDX_Control(pDX, IDC_CHECK_lrc, m_netlrc);
	DDX_Control(pDX, IDOK, m_okdummy);
	DDX_Control(pDX, IDCANCEL5, m_kanren);
	DDX_Control(pDX, IDCANCEL, m_canceldummy);
	DDX_Control(pDX, IDC_COMBO_LANG, m_comboLang);
	DDX_Control(pDX, IDC_CHECK_UPSCALE, m_upscale);
	DDX_Control(pDX, IDC_COMBO_SPEAKER, m_speaker);
}


BEGIN_MESSAGE_MAP(CRender, CCustomBlurDialogExBase)
	//{{AFX_MSG_MAP(CRender)
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDCANCEL2, &CRender::OnBnClickedCancel2)
	ON_BN_CLICKED(IDC_CHECK32, &CRender::Onspc2x)
	ON_BN_CLICKED(IDC_CHECK33, &CRender::Onspc4x)
	ON_BN_CLICKED(IDC_CHECK34, &CRender::Onspc8x)
	ON_BN_CLICKED(IDC_CHECK35, &CRender::Onspc1x)
	ON_BN_CLICKED(IDC_CHECK36, &CRender::Onspc16x)
	ON_BN_CLICKED(IDC_CHECK40, &CRender::Onmp31)
	ON_BN_CLICKED(IDC_CHECK37, &CRender::Onmp315)
	ON_BN_CLICKED(IDC_CHECK38, &CRender::Onmp32)
	ON_BN_CLICKED(IDC_CHECK39, &CRender::Onmp325)
	ON_BN_CLICKED(IDC_CHECK41, &CRender::Onmp33)
	ON_BN_CLICKED(IDC_CHECK45, &CRender::Onkpi10)
	ON_BN_CLICKED(IDC_CHECK42, &CRender::Onkpi15)
	ON_BN_CLICKED(IDC_CHECK43, &CRender::Onkpi20)
	ON_BN_CLICKED(IDC_CHECK44, &CRender::Onkpi25)
	ON_BN_CLICKED(IDC_CHECK46, &CRender::Onkpi30)
	ON_BN_CLICKED(IDCANCEL3, &CRender::Onkpi)
	ON_BN_CLICKED(IDC_FONT, &CRender::OnFontMain)
	ON_BN_CLICKED(IDC_FONT2, &CRender::OnFontList)
	ON_BN_CLICKED(IDOK, &CRender::OnBnClickedOk)
	ON_BN_CLICKED(IDC_CHECK49, &CRender::OnBnClicked24bit)
	ON_BN_CLICKED(IDC_CHECK50, &CRender::OnBnClickedCheck50)
	ON_BN_CLICKED(IDCANCEL4, &CRender::OnBnClickedCancel4)
	ON_WM_TIMER()
	ON_CBN_SELCHANGE(IDC_COMBO2, &CRender::OnCbnSelchangeCombo2)
	ON_BN_CLICKED(IDC_BUTTON1, &CRender::OnBnClickedButton1)
	ON_CBN_SELCHANGE(IDC_COMBO3, &CRender::OnCbnSelchangeCombo3)
	ON_CBN_SELCHANGE(IDC_COMBO_SPEAKER, &CRender::OnCbnSelchangeSpeaker)
	ON_BN_CLICKED(IDC_CHECK_UPSCALE, &CRender::OnBnClickedCheckUpscale)
	ON_BN_CLICKED(IDC_CHECK51, &CRender::OnBnClicked32bit)
	ON_WM_CTLCOLOR()
	ON_WM_CREATE()
	ON_WM_MOVING()
	ON_BN_CLICKED(IDCANCEL, &CRender::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_CHECK52, &CRender::OnBnClickedCheck52)
	ON_CBN_EDITCHANGE(IDC_COMBO4, &CRender::OnCbnEditchangeCombo4)
	ON_CBN_SELCHANGE(IDC_COMBO4, &CRender::OnCbnSelchangeCombo4)
	ON_BN_CLICKED(IDCANCEL5, &CRender::OnBnClickedCancel5)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRender メッセージ ハンドラ
CComboBox *sl;
GUID slg[200];
int slgc;
CString sls[200];
DWORD samp[] = { 11025, 12000, 22050, 24000, 44100, 48000, 96000, 192000, 384000, 768000, 1536000, 3072000 };
extern COggDlg* og;

BOOL CRender::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();

	m_bakSoundGuid = savedata.soundguid;
	m_bakSoundCur = savedata.soundcur;
	m_bakSamples = savedata.samples;
	m_bakUpscale = savedata.upscale_enable;
	m_bakSpeaker = savedata.speaker_layout;
	m_bakBit24 = savedata.bit24;
	m_bakBit32 = savedata.bit32;

	SetWindowText(LL14(L"レンダリング選択", L"Rendering Options", L"Options de rendu", L"Opzioni di rendering", L"Opciones de renderizado", L"렌더링 옵션", L"渲染选项", L"خيارات العرض", L"Параметры рендеринга", L"Rendering-Optionen", L"Opções de renderização", L"Renderopties", L"Opcje renderowania", L"Render seçenekleri"));
	SetDlgItemText(IDOK, LL14(L"OK", L"OK", L"OK", L"OK", L"OK", L"확인", L"确定", L"موافق", L"OK", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
	SetDlgItemText(IDCANCEL, LL14(L"キャンセル", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"İptal"));
	SetDlgItemText(IDCANCEL2, LL14(L"DirectShowフィルタ一覧", L"DirectShow Filter List", L"Liste des filtres DirectShow", L"Elenco filtri DirectShow", L"Lista de filtros DirectShow", L"DirectShow 필터 목록", L"DirectShow 过滤器列表", L"قائمة مرشحات DirectShow", L"Список фильтров DirectShow", L"DirectShow-Filterliste", L"Lista de filtros DirectShow", L"DirectShow-filterlijst", L"Lista filtrów DirectShow", L"DirectShow Filtre Listesi"));
	SetDlgItemText(IDCANCEL3, LL14(L"kpi一覧", L"kpi List", L"Liste kpi", L"Elenco kpi", L"Lista kpi", L"kpi 목록", L"kpi 列表", L"قائمة kpi", L"Список kpi", L"kpi-Liste", L"Lista kpi", L"kpi-lijst", L"Lista kpi", L"kpi Listesi"));
	SetDlgItemText(IDCANCEL5, LL14(L"関連付け", L"File Association", L"Association de fichiers", L"Associazione file", L"Asociación de archivos", L"파일 연결", L"文件关联", L"ربط الملفات", L"Связь файлов", L"Dateizuordnung", L"Associação de ficheiros", L"Bestandskoppeling", L"Powiazanie plików", L"Dosya ilişkilendirme"));
	SetDlgItemText(IDC_CHECK1, LL14(L"デフォルトでEVR使用(Vista以降)", L"Default EVR use (Vista+)", L"EVR par défaut (Vista+)", L"Uso EVR predefinito (Vista+)", L"Uso EVR predeterminado (Vista+)", L"기본 EVR 사용(Vista+)", L"默认使用 EVR（Vista+）", L"استخدام EVR افتراضي (Vista+)", L"Использовать EVR по умолчанию (Vista+)", L"EVR standardmäßig (Vista+)", L"Usar EVR por defeito (Vista+)", L"Standaard EVR (Vista+)", L"Domyślne EVR (Vista+)", L"Varsayılan EVR kullan (Vista+)"));
	SetDlgItemText(IDC_CHECK2, LL14(L"デスクトップコンポジションを使用する", L"Use desktop composition", L"Utiliser la composition du bureau", L"Usa composizione desktop", L"Usar composición de escritorio", L"데스크톱 컴포지션 사용", L"使用桌面合成", L"استخدام تركيب سطح المكتب", L"Использовать композицию рабочего стола", L"Desktop-Komposition verwenden", L"Usar composição do ambiente de trabalho", L"Bureaubladcompositie gebruiken", L"Użyj kompozycji pulpitu", L"Masaüstü birleşimini kullan"));
	SetDlgItemText(IDC_CHECK3, LL14(L"AeroやGrassを使用する", L"Use Aero and Grass", L"Utiliser Aero et Grass", L"Usa Aero e Grass", L"Usar Aero y Grass", L"Aero 및 Grass 사용", L"使用 Aero 和 Grass", L"استخدام Aero و Grass", L"Использовать Aero и Grass", L"Aero und Grass verwenden", L"Usar Aero e Grass", L"Aero en Grass gebruiken", L"Użyj Aero i Grass", L"Aero ve Grass kullan"));
	SetDlgItemText(IDC_CHECK27, LL14(L"ffdshow使用", L"Use ffdshow", L"Utiliser ffdshow", L"Usa ffdshow", L"Usar ffdshow", L"ffdshow 사용", L"使用 ffdshow", L"استخدام ffdshow", L"Использовать ffdshow", L"ffdshow verwenden", L"Usar ffdshow", L"ffdshow gebruiken", L"Użyj ffdshow", L"ffdshow kullan"));
	SetDlgItemText(IDC_CHECK30, LL14(L"vobやdatはHaaliスキップ対応とする", L"vob/dat skip Haali by default", L"vob/dat ignorer Haali par défaut", L"vob/dat salta Haali per default", L"vob/dat omitir Haali por defecto", L"vob/dat 기본 Haali 건너뛰기", L"vob/dat 默认跳过 Haali", L"تخطي Haali لـ vob/dat افتراضيًا", L"vob/dat пропускать Haali по умолчанию", L"vob/dat Haali standardmäßig überspringen", L"vob/dat omitir Haali por defeito", L"vob/dat standaard Haali overslaan", L"vob/dat domyślnie pomiń Haali", L"vob/dat varsayılan Haali atla"));
	SetDlgItemText(IDC_CHECK31, LL14(L"Haaliを使用しない", L"Do not use Haali", L"Ne pas utiliser Haali", L"Non usare Haali", L"No usar Haali", L"Haali 사용 안 함", L"不使用 Haali", L"عدم استخدام Haali", L"Не использовать Haali", L"Haali nicht verwenden", L"Não usar Haali", L"Haali niet gebruiken", L"Nie używaj Haali", L"Haali kullanma"));
	SetDlgItemText(IDC_CHECK47, LL14(L"mp3 オリジナルデコーダを使う", L"Use original mp3 decoder", L"Utiliser le décodeur mp3 d'origine", L"Usa decoder mp3 originale", L"Usar decodificador mp3 original", L"원본 mp3 디코더 사용", L"使用原始 mp3 解码器", L"استخدام وحدة فك ترميز mp3 الأصلية", L"Использовать оригинальный декодер mp3", L"Originalen MP3-Decoder verwenden", L"Usar descodificador mp3 original", L"Originele mp3-decoder gebruiken", L"Użyj oryginalnego dekodera mp3", L"Orijinal mp3 çözücü kullan"));
	SetDlgItemText(IDC_CHECK48, LL14(L"複数音声の動画の時は音声選択画面を出す", L"Show audio selection for multi-audio video", L"Afficher la sélection audio pour vidéo multi-audio", L"Mostra selezione audio per video multi-audio", L"Mostrar selección de audio para vídeo multi-audio", L"다중 오디오 동영상 재생 시 오디오 선택 화면 표시", L"多音轨视频时显示音频选择", L"عرض اختيار الصوت للفيديو متعدد الصوت", L"Показывать выбор аудио для мультиаудио видео", L"Tonauswahl bei Multi-Audio-Video anzeigen", L"Mostrar seleção de áudio para vídeo multi-áudio", L"Audio-selectie tonen voor multi-audiovideo", L"Pokaż wybór dźwięku dla wideo wielodźwiękowego", L"Çoklu sesli videoda ses seçimini göster"));
	SetDlgItemText(IDC_CHECK49, LL14(L"24bit使用", L"Use 24bit", L"Utiliser 24 bits", L"Usa 24 bit", L"Usar 24 bits", L"24bit 사용", L"使用 24 位", L"استخدام 24 بت", L"Использовать 24 бит", L"24 Bit verwenden", L"Usar 24 bits", L"24 bit gebruiken", L"Użyj 24 bitów", L"24 bit kullan"));
	SetDlgItemText(IDC_CHECK50, LL14(L"m4aを内蔵エンジンで演奏する", L"Play m4a with built-in engine", L"Lire m4a avec moteur intégré", L"Riproduci m4a con motore integrato", L"Reproducir m4a con motor integrado", L"내장 엔진으로 m4a 재생", L"使用内置引擎播放 m4a", L"تشغيل m4a بمحرك مدمج", L"Воспроизводить m4a встроенным движком", L"m4a mit integrierter Engine abspielen", L"Reproduzir m4a com motor integrado", L"m4a afspelen met ingebouwde engine", L"Odtwarzaj m4a wbudowanym silnikiem", L"Yerleşik motorla m4a çal"));
	SetDlgItemText(IDC_CHECK51, LL14(L"32bit使用", L"Use 32bit", L"Utiliser 32 bits", L"Usa 32 bit", L"Usar 32 bits", L"32bit 사용", L"使用 32 位", L"استخدام 32 بت", L"Использовать 32 бит", L"32 Bit verwenden", L"Usar 32 bits", L"32 bit gebruiken", L"Użyj 32 bitów", L"32 bit kullan"));
	SetDlgItemText(IDC_CHECK52, LL14(L"スペアナのモード切り替える", L"Switch spectrum analyzer mode", L"Changer le mode analyseur de spectre", L"Cambia modalità analizzatore di spettro", L"Cambiar modo del analizador de espectro", L"스펙트럼 분석기 모드 전환", L"切换频谱分析仪模式", L"تبديل وضع محلل الطيف", L"Переключить режим анализатора спектра", L"Spektrumanalysator-Modus wechseln", L"Mudar modo do analisador de espetro", L"Modus spectrumanalyser wijzigen", L"Przełącz tryb analizatora widma", L"Spektrum analizörü modunu değiştir"));
	SetDlgItemText(IDC_CHECK_lrc, LL14(L"lrcをネットから取得する(LRCLib/NetEase等による)", L"Fetch lrc from network (LRCLib/NetEase etc.)", L"Récupérer les paroles depuis le réseau (LRCLib/NetEase etc.)", L"Recupera lrc da rete (LRCLib/NetEase ecc.)", L"Obtener lrc de la red (LRCLib/NetEase etc.)", L"네트워크에서 lrc 가져오기 (LRCLib/NetEase 등)", L"从网络获取 lrc（LRCLib/NetEase 等）", L"جلب lrc من الشبكة (LRCLib/NetEase إلخ)", L"Загружать lrc из сети (LRCLib/NetEase и т.д.)", L"Lrc aus dem Netz laden (LRCLib/NetEase usw.)", L"Obter lrc da rede (LRCLib/NetEase etc.)", L"Lrc ophalen van netwerk (LRCLib/NetEase etc.)", L"Pobierz lrc z sieci (LRCLib/NetEase itp.)", L"Ağdan lrc al (LRCLib/NetEase vb.)"));
	SetDlgItemText(IDC_BUTTON1, LL14(L"碧の軌跡用t_bgm._dt", L"t_bgm._dt for Ao no Kiseki", L"t_bgm._dt pour Ao no Kiseki", L"t_bgm._dt per Ao no Kiseki", L"t_bgm._dt para Ao no Kiseki", L"아오의 궤적용 t_bgm._dt", L"碧之轨迹用 t_bgm._dt", L"t_bgm._dt لـ Ao no Kiseki", L"t_bgm._dt для Ao no Kiseki", L"t_bgm._dt für Ao no Kiseki", L"t_bgm._dt para Ao no Kiseki", L"t_bgm._dt voor Ao no Kiseki", L"t_bgm._dt dla Ao no Kiseki", L"Ao no Kiseki için t_bgm._dt"));
	SetDlgItemText(IDC_FONT, LL14(L"メイン用フォント", L"Main font", L"Police principale", L"Carattere principale", L"Fuente principal", L"메인 글꼴", L"主字体", L"الخط الرئيسي", L"Основной шрифт", L"Hauptschriftart", L"Fonte principal", L"Hoofdlettertype", L"Czcionka główna", L"Ana yazı tipi"));
	SetDlgItemText(IDC_FONT2, LL14(L"リスト用フォント", L"List font", L"Police liste", L"Carattere lista", L"Fuente de lista", L"목록 글꼴", L"列表字体", L"خط القائمة", L"Шрифт списка", L"Listenschriftart", L"Fonte da lista", L"Lijstlettertype", L"Czcionka listy", L"Liste yazı tipi"));
	SetDlgItemText(IDC_STATIC_LANG, LL14(L"言語", L"Language", L"Langue", L"Lingua", L"Idioma", L"언어", L"语言", L"اللغة", L"Язык", L"Sprache", L"Idioma", L"Taal", L"Język", L"Dil"));
	SetDlgItemText(IDC_STATIC_R_BUF, LL14(L"割込間隔", L"Buffer interval", L"Intervalle tampon", L"Intervallo buffer", L"Intervalo de búfer", L"버퍼 간격", L"缓冲区间隔", L"فاصل المخزن المؤقت", L"Интервал буфера", L"Pufferintervall", L"Intervalo de buffer", L"Bufferinterval", L"Interwał bufora", L"Ara belleği aralığı"));
	SetDlgItemText(IDC_STATIC_R_MP3, LL14(L"mp3音量", L"mp3 volume", L"Volume mp3", L"Volume mp3", L"Volumen mp3", L"mp3 볼륨", L"mp3 音量", L"حجم mp3", L"Громкость mp3", L"MP3-Lautstärke", L"Volume mp3", L"mp3-volume", L"Głośność mp3", L"mp3 sesi"));
	SetDlgItemText(IDC_STATIC_R_KPI, LL14(L"その他のkpi", L"Other kpi", L"Autres kpi", L"Altri kpi", L"Otros kpi", L"기타 kpi", L"其他 kpi", L"kpi أخرى", L"Другие kpi", L"Andere kpi", L"Outros kpi", L"Andere kpi", L"Inne kpi", L"Diğer kpi"));
	SetDlgItemText(IDC_STATIC_R_DISP, LL14(L"表示間隔", L"Display interval", L"Intervalle d'affichage", L"Intervallo display", L"Intervalo de pantalla", L"표시 간격", L"显示间隔", L"فاصل العرض", L"Интервал отображения", L"Anzeigeintervall", L"Intervalo de exibição", L"Weergave-interval", L"Interwał wyświetlania", L"Görüntüleme aralığı"));
	SetDlgItemText(IDC_STATIC_R_DEV, LL14(L"再生デバイス", L"Playback device", L"Périphérique lecture", L"Dispositivo riproduzione", L"Dispositivo reproducción", L"재생 장치", L"播放设备", L"جهاز التشغيل", L"Устройство воспроизведения", L"Wiedergabegerät", L"Dispositivo reprodução", L"Afspeelapparaat", L"Urządzenie odtwarzania", L"Oynatma cihazı"));
	SetDlgItemText(IDC_STATIC_R_SAMP, LL14(L"MAXサンプルレート：", L"MAX sample rate:", L"Freq. échantillonnage max:", L"Freq. campionamento max:", L"Frec. muestreo máx.:", L"최대 샘플레이트:", L"最大采样率：", L"معدل العينات الأقصى:", L"Макс. частота дискретизации:", L"Max. Abtastrate:", L"Taxa amostragem máx.:", L"Max. samplefrequentie:", L"Maks. częstotliwość:", L"Maks. örnekleme oranı:"));
	SetDlgItemText(IDC_STATIC_R_SPEANA, LL14(L"スペアナ倍率", L"Spectrum scale", L"Échelle spectre", L"Scala spettro", L"Escala espectro", L"스펙트럼 배율", L"频谱倍率", L"مقياس الطيف", L"Масштаб спектра", L"Spektrumskala", L"Escala espectro", L"Spectrumschaal", L"Skala widma", L"Spektrum ölçeği"));
	SetDlgItemText(IDC_STATIC_R_SPC, LL14(L".SPC,.HES音量(kpi)", L".SPC,.HES volume(kpi)", L"Volume .SPC,.HES (kpi)", L"Volume .SPC,.HES (kpi)", L"Volumen .SPC,.HES (kpi)", L".SPC,.HES 볼륨(kpi)", L".SPC,.HES 音量(kpi)", L"حجم .SPC,.HES (kpi)", L"Громкость .SPC,.HES (kpi)", L".SPC,.HES-Lautstärke (kpi)", L"Volume .SPC,.HES (kpi)", L".SPC,.HES-volume (kpi)", L"Głośność .SPC,.HES (kpi)", L".SPC,.HES sesi (kpi)"));
	SetDlgItemText(IDC_STATIC_R_BIT, LL14(L"演奏bit深度：", L"Playback bits:", L"Bits lecture:", L"Bit riproduzione:", L"Bits reproducción:", L"재생 비트:", L"播放位深：", L"بت التشغيل:", L"Битность воспроизведения:", L"Wiedergabe-Bits:", L"Bits reprodução:", L"Afspeelbits:", L"Bity odtwarzania:", L"Oynatma bitleri:"));
	SetDlgItemText(IDC_STATIC_R_SPEAKER, LL14(L"出力チャンネル", L"Output channels", L"Canaux de sortie", L"Canali di uscita", L"Canales de salida", L"출력 채널", L"输出声道", L"قنوات الإخراج", L"Выходные каналы", L"Ausgabekanäle", L"Canais de saída", L"Uitgangskanalen", L"Kanały wyjściowe", L"Çıkış kanalları"));
	SetDlgItemText(IDC_CHECK_UPSCALE, LL14(L"アップスケール出力", L"Upscale output", L"Sortie upscalée", L"Upscaling in uscita", L"Salida con upscaling", L"업스케일 출력", L"升频输出", L"تحسين الدقة للإخراج", L"Апскейл вывода", L"Upscale-Ausgabe", L"Saída com upscaling", L"Upscale uitvoer", L"Wyjście upscaling", L"Yükseltmeli çıkış"));
	SetDlgItemText(IDC_STATIC12, LL14(L"倍", L"x", L"x", L"x", L"x", L"x", L"倍", L"x", L"x", L"x", L"x", L"x", L"x", L"x"));
	// SPC volume checkboxes (x1, x2, x4, x8, x16)
	SetDlgItemText(IDC_CHECK35, LL14(L"等倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x"));
	SetDlgItemText(IDC_CHECK32, LL14(L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x"));
	SetDlgItemText(IDC_CHECK33, LL14(L"4倍", L"4x", L"4x", L"4x", L"4x", L"4x", L"4倍", L"4x", L"4x", L"4x", L"4x", L"4x", L"4x", L"4x"));
	SetDlgItemText(IDC_CHECK34, LL14(L"8倍", L"8x", L"8x", L"8x", L"8x", L"8x", L"8倍", L"8x", L"8x", L"8x", L"8x", L"8x", L"8x", L"8x"));
	SetDlgItemText(IDC_CHECK36, LL14(L"16倍", L"16x", L"16x", L"16x", L"16x", L"16x", L"16倍", L"16x", L"16x", L"16x", L"16x", L"16x", L"16x", L"16x"));
	// MP3 volume checkboxes (1x, 1.5x, 2x, 2.5x, 3x)
	SetDlgItemText(IDC_CHECK40, LL14(L"等倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x"));
	SetDlgItemText(IDC_CHECK37, LL14(L"1.5倍", L"1.5x", L"1,5x", L"1,5x", L"1,5x", L"1.5x", L"1.5倍", L"1.5x", L"1,5x", L"1,5x", L"1,5x", L"1,5x", L"1,5x", L"1.5x"));
	SetDlgItemText(IDC_CHECK38, LL14(L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x"));
	SetDlgItemText(IDC_CHECK39, LL14(L"2.5倍", L"2.5x", L"2,5x", L"2,5x", L"2,5x", L"2.5x", L"2.5倍", L"2.5x", L"2,5x", L"2,5x", L"2,5x", L"2,5x", L"2,5x", L"2.5x"));
	SetDlgItemText(IDC_CHECK41, LL14(L"3倍", L"3x", L"3x", L"3x", L"3x", L"3x", L"3倍", L"3x", L"3x", L"3x", L"3x", L"3x", L"3x", L"3x"));
	// KPI volume checkboxes (1x, 2x, 3x, 4x, 5x)
	SetDlgItemText(IDC_CHECK45, LL14(L"等倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1倍", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x", L"1x"));
	SetDlgItemText(IDC_CHECK42, LL14(L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2倍", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x", L"2x"));
	SetDlgItemText(IDC_CHECK43, LL14(L"3倍", L"3x", L"3x", L"3x", L"3x", L"3x", L"3倍", L"3x", L"3x", L"3x", L"3x", L"3x", L"3x", L"3x"));
	SetDlgItemText(IDC_CHECK44, LL14(L"4倍", L"4x", L"4x", L"4x", L"4x", L"4x", L"4倍", L"4x", L"4x", L"4x", L"4x", L"4x", L"4x", L"4x"));
	SetDlgItemText(IDC_CHECK46, LL14(L"5倍", L"5x", L"5x", L"5x", L"5x", L"5x", L"5倍", L"5x", L"5x", L"5x", L"5x", L"5x", L"5x", L"5x"));
	m_comboLang.AddString(LL14(L"日本語", L"Japanese", L"Japanese", L"Japanese", L"Japanese", L"Japanese", L"Japanese", L"Japanese", L"Japanese", L"Japanese", L"Japanese", L"Japanese", L"Japanese", L"Japanese"));
	m_comboLang.AddString(L"English");
	m_comboLang.AddString(L"Français");
	m_comboLang.AddString(L"Italiano");
	m_comboLang.AddString(L"Español");
	m_comboLang.AddString(L"한국어");
	m_comboLang.AddString(L"中文");
	m_comboLang.AddString(L"العربية");
	m_comboLang.AddString(L"Русский");
	m_comboLang.AddString(L"Deutsch");
	m_comboLang.AddString(L"Português");
	m_comboLang.AddString(L"Nederlands");
	m_comboLang.AddString(L"Polski");
	m_comboLang.AddString(L"Türkçe");
	m_comboLang.SetCurSel(savedata.lang >= 0 && savedata.lang <= 13 ? savedata.lang : 0);
	OSVERSIONINFO in; ZeroMemory(&in, sizeof(in)); in.dwOSVersionInfoSize = sizeof(OSVERSIONINFO); GetVersionEx(&in);
	if (in.dwMajorVersion <= 5)
		m_1.AddString(LL14(L"デフォルト", L"Default", L"Default", L"Default", L"Default", L"Default", L"Default", L"Default", L"Default", L"Default", L"Default", L"Default", L"Default", L"Default"));
	else
		m_1.AddString(LL14(L"デフォルト(普通/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)", L"Default (normal/EVR)"));
	m_1.AddString(L"VMR7");
	m_1.AddString(L"VMR9");
	m_1.SetCurSel(savedata.render);
	switch (savedata.spc) {
	case 1:m_spc1x.SetCheck(TRUE); break;
	case 2:m_spc2x.SetCheck(TRUE); break;
	case 4:m_spc4x.SetCheck(TRUE); break;
	case 8:m_spc8x.SetCheck(TRUE); break;
	case 16:m_spc16x.SetCheck(TRUE); break;
	}
	switch (savedata.mp3) {
	case 1:m_mp31.SetCheck(TRUE); break;
	case 2:m_mp315.SetCheck(TRUE); break;
	case 3:m_mp32.SetCheck(TRUE); break;
	case 4:m_mp325.SetCheck(TRUE); break;
	case 5:m_mp33.SetCheck(TRUE); break;
	}
	switch (savedata.kpivol) {
	case 1:m_kpi10.SetCheck(TRUE); break;
	case 2:m_kpi15.SetCheck(TRUE); break;
	case 3:m_kpi20.SetCheck(TRUE); break;
	case 4:m_kpi25.SetCheck(TRUE); break;
	case 5:m_kpi30.SetCheck(TRUE); break;
	}
	if (in.dwMajorVersion <= 5) {
		m_evr.EnableWindow(FALSE);
		m_con.EnableWindow(FALSE);
		m_a.EnableWindow(FALSE);
	}
	m_mp3orig.SetCheck(savedata.mp3orig);
	m_audiost.SetCheck(savedata.audiost);
	m_24.SetCheck(savedata.bit24);
	m_32bit.SetCheck(savedata.bit32);
	m_m4a.SetCheck(savedata.m4a);
	m_upscale.SetCheck(savedata.upscale_enable ? BST_CHECKED : BST_UNCHECKED);
	m_speaker.ResetContent();
	m_speaker.AddString(LL14(L"ステレオ (2ch)", L"Stereo (2ch)", L"Stéréo (2ch)", L"Stereo (2ch)", L"Estéreo (2ch)", L"스테레오 (2ch)", L"立体声 (2ch)", L"ستيريو (2ch)", L"Стерео (2ch)", L"Stereo (2ch)", L"Estéreo (2ch)", L"Stereo (2ch)", L"Stereo (2ch)", L"Stereo (2ch)"));
	m_speaker.AddString(LL14(L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2,1ch (G+D+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (左+右+低音)", L"2.1ش (يسار+يمين+باس)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)", L"2.1ch (L+R+LFE)"));
	m_speaker.AddString(LL14(L"4ch (クアッド)", L"4ch (quad)", L"4 canaux (quad)", L"4ch (quad)", L"4 canales (cuad.)", L"4채널 (쿼드)", L"四声道（环绕）", L"4 قنوات", L"4 канала (квад)", L"4 Kanäle (Quad)", L"4 canais (quad)", L"4 kanalen (quad)", L"4 kanały (quad)", L"4 kanal (quad)"));
	m_speaker.AddString(LL14(L"5.1ch サラウンド", L"5.1 surround", L"Surround 5.1", L"Surround 5.1", L"Sonido envolvente 5.1", L"5.1 서라운드", L"5.1 环绕声", L"صوت محيطي 5.1", L"Объёмный 5.1", L"5.1 Surround", L"Surround 5.1", L"5.1 surround", L"Dźwięk przestrzenny 5.1", L"5.1 surround"));
	m_speaker.AddString(LL14(L"7.1ch サラウンド", L"7.1 surround", L"Surround 7.1", L"Surround 7.1", L"Sonido envolvente 7.1", L"7.1 서라운드", L"7.1 环绕声", L"صوت محيطي 7.1", L"Объёмный 7.1", L"7.1 Surround", L"Surround 7.1", L"7.1 surround", L"Dźwięk przestrzenny 7.1", L"7.1 surround"));
	m_speaker.AddString(LL14(L"オリジナル（マッピングなし）", L"Original (no channel mapping)", L"Original (sans mappage)", L"Originale (nessun mapping)", L"Original (sin mapeo)", L"원본(채널 매핑 없음)", L"原始（不映射声道）", L"أصلي (بدون تعيين قنوات)", L"Исходный канал без микширования", L"Original (kein Kanal-Mapping)", L"Original (sem mapeamento)", L"Origineel (geen kanaal-mapping)", L"Oryginał (bez mapowania kanałów)", L"Orijinal (kanal eşlemesi yok)"));
	{
		int sp = savedata.speaker_layout;
		if (sp < 0 || sp > 5) sp = 0;
		m_speaker.SetCurSel(sp);
	}

	m_tooltip.Create(this);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), LL14(L"保存して閉じます", L"Save and close", L"Save and close", L"Save and close", L"Save and close", L"Save and close", L"Save and close", L"Save and close", L"Save and close", L"Save and close", L"Save and close", L"Save and close", L"Save and close", L"Save and close"));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL), LL14(L"保存せずに閉じます", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving", L"Close without saving"));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL2), LL14(L"再生中の使用DirectShowフィルタを表示します。", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback.", L"Show DirectShow filters in use during playback."));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL3), LL14(L"kpi一覧を表示します。", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list.", L"Show kpi list."));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL4), LL14(L"各種ファイルを簡易プレイヤに関連づけします。\nうまくいかない場合もあります。", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work.", L"Associate files with simple player.\nMay not always work."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK1), LL14(L"Windows Vista/7以降で有効です。\nIndeoを用いた動画の場合OFFにしてください。\nそれ以外はONでいいです。", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON.", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK2), LL14(L"Windows Vista/7以降で有効です。\nデスクトップコンポジション(Aero)を使用するかどうかを選択します。\n使用しないにするとEVRじゃなくても動画画面はきれいになります。", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR.", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK3), LL14(L"Windows 10以降で有効です。\nAero Grassを使用するかどうか決めます。", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass.", L"Effective on Windows 10+.\nEnable Aero Grass."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK27), LL14(L"動画にffdshowを使うかどうか選択します。\nWindows7の場合、デフォルトでDivxなどを再生できるのでそちらを使いたい人はOFFにしてください。", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that.", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK30), LL14(L"vobとdatファイルはHaaliを通さないように作られていますが、\nvobに複数音声があるときにはチェックを入れて下さい。", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks.", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK31), LL14(L"動画にHaaliを使いません。\n動画が重いと思った時や複数音声が無い時はチェックを入れると軽くなります。", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio.", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK32), LL14(L"kpi SPC/NEZplug++等のSPCの音量を2倍にします。", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC.", L"2x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK33), LL14(L"kpi SPC/NEZplug++等のSPCの音量を3倍にします。", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC.", L"3x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK34), LL14(L"kpi SPC/NEZplug++等のSPCの音量を4倍にします。", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC.", L"4x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK35), LL14(L"kpi SPC/NEZplug++等のSPCの音量を等倍にします。", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC.", L"1x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK36), LL14(L"kpi SPC/NEZplug++等のSPCの音量を5倍にします。", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC.", L"5x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK40), LL14(L"mp3の音量を等倍にします。", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume.", L"1x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK37), LL14(L"mp3の音量を1.5倍にします。", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume.", L"1.5x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK38), LL14(L"mp3の音量を2倍にします。", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume.", L"2x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK39), LL14(L"mp3の音量を2.5倍にします。", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume.", L"2.5x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK41), LL14(L"mp3の音量を3倍にします。", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume.", L"3x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK45), LL14(L"kpiの音量を等倍にします。", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume.", L"1x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK42), LL14(L"kpiの音量を2倍にします。", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume.", L"2x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK43), LL14(L"kpiの音量を3倍にします。", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume.", L"3x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK44), LL14(L"kpiの音量を4倍にします。", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume.", L"4x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK46), LL14(L"kpiの音量を5倍にします。", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume.", L"5x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK47), LL14(L"mp3のデコーダをオリジナルのデコーダを使わずに、独自で使ったデコーダを使う。\nエラーなどで演奏できないときにチェック入れて下さい。\nまた独自で正常にならない時ははずして下さい。", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues.", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK48), LL14(L"複数音声のある動画を再生する時に、再生前に\n音声ストリームの選択画面を表示します。\n通常ストリーム1がメインとして使われ、ストリーム2以降はコメンタリや英語音声などに使われています。", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc.", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK49), LL14(L"対応しているkpiを24bit(ハイレゾ)で再生します。\n通常は16bitですが、まれに対応しているものがあります。\n音割れについては考慮されていないため、spcなど倍率を上げないといけないものは気をつけて下さい。", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc.", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK51), LL14(L"対応しているkpiを32bit(ハイレゾ)で再生します。\n通常は16bitですが、まれに対応しているものがあります。\n音割れについては考慮されていないため、spcなど倍率を上げないといけないものは気をつけて下さい。", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc.", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK50), LL14(L"m4aを内蔵エンジンで演奏します。", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine.", L"Play m4a with built-in engine."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK52), LL14(L"スペアナの表示モードを切り替えます", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode", L"Switch spectrum analyzer display mode"));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO3), LL14(L"再生するサンプルレートを設定します。\nサウンドカードが対応していない場合自動的に再生時対応上限まで下げます。", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported.", L"Set playback sample rate.\nAuto-lowers if sound card unsupported."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK_UPSCALE), LL14(L"設定したサンプルレート・ビット深度・チャンネルでDirectSoundに出力します。\nオフにするとソース形式のまま出力します。", L"Output to DirectSound at configured rate, bit depth, and channels.\nOff keeps the source format.", L"Sortie DirectSound au débit / bits / canaux configurés.\nDésactivé = format source.", L"Uscita DirectSound con frequenza, bit e canali impostati.\nSpento = formato sorgente.", L"Salida DirectSound con frecuencia, bits y canales configurados.\nApagado = formato de origen.", L"설정한 샘플레이트·비트·채널로 DirectSound 출력.\n끄면 소스 형식 유지.", L"按设置的采样率、位深和声道输出到 DirectSound。\n关闭则保持源格式。", L"إخراج DirectSound بالمعدل والبت والقنوات المضبوطة.\nإيقاف = تنسيق المصدر.", L"Вывод в DirectSound с заданной частотой, битностью и каналами.\nВыкл. — формат источника.", L"Ausgabe an DirectSound mit eingestellter Rate, Bittiefe und Kanälen.\nAus = Quellformat.", L"Saída DirectSound com taxa, bits e canais configurados.\nDesligado = formato da fonte.", L"DirectSound-uitvoer met ingestelde rate, bits en kanalen.\nUit = bronformaat.", L"Wyjście DirectSound z ustawioną częstotliwością, bitami i kanałami.\nWył. = format źródła.", L"Ayarlanan hız, bit ve kanallarla DirectSound çıkışı.\nKapalı = kaynak biçimi."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO_SPEAKER), LL14(L"アップスケール時の出力チャンネル配置（2ch / 2.1 / 4ch / 5.1 / 7.1 / マッピングなし）を選びます。マッピングなしはソースのチャンネル数のまま、レート・ビット深度のみ変換します。", L"Speaker layout when upscaling (2ch / 2.1 / 4ch / 5.1 / 7.1 / no mapping). No mapping keeps source channel count; only rate and bit depth change.", L"Disposition haut-parleurs en upscaling (2ch / 2.1 / 4ch / 5.1 / 7.1 / sans mappage). Sans mappage : même nombre de canaux, seuls débit et bits changent.", L"Layout altoparlanti (2ch / 2.1 / 4ch / 5.1 / 7.1 / nessun mapping). Nessun mapping: stessi canali, solo frequenza e bit.", L"Disposición de altavoces (2ch / 2.1 / 4ch / 5.1 / 7.1 / sin mapeo). Sin mapeo: mismos canales; solo tasa y bits.", L"업스케일 시 스피커(2ch/2.1/4ch/5.1/7.1/매핑 없음). 매핑 없음은 소스 채널 수 유지, 레이트·비트만 변환.", L"升频时的扬声器布局（含不映射声道）。不映射则保持源声道数，仅转换采样率与位深。", L"تخطيط السماعات مع خيار بدون تعيين. بدون تعيين: نفس عدد القنوات؛ تغيير المعدل والبت فقط.", L"Раскладка каналов при апскейле; «без маппинга» сохраняет число каналов источника, меняются только частота и битность.", L"Lautsprecher-Layout; „kein Mapping“ behält Kanalzahl, nur Rate/Bits.", L"Layout de altifalante; sem mapeamento mantém canais da fonte, só taxa e bits.", L"Luidsprekerindeling; geen mapping behoudt bronkanalen, alleen rate en bits.", L"Układ kanałów; bez mapowania = ta sama liczba kanałów, zmiana tylko częstotliwości i bitów.", L"Hoparlör düzeni; eşleme yok kaynak kanal sayısını korur, yalnızca hız ve bit derinliği değişir."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO4), LL14(L"スペアナで表示する表示方法を選択します。\n使う時は横のチェックボックスにチェックを入れてください\n音階：88鍵盤として表示します\n周波数帯：周波数として表示します\n標準：既定の見やすい形のスペアナで表示します", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum"));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON1), LL14(L"碧の軌跡用のt_bgm._dtを設定します。", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki.", L"Set t_bgm._dt for Ao no Kiseki."));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL5), LL14(L"win7くらいまで対応。関連付けに追加します。\nwin10以降でも追加はされるとは思いますがされないときもあります。", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always.", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK_lrc), LL14(L"歌詞情報をネットから参照するようにします。\n数パターン試すため少し再生までに時間かかります。", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback.", L"Fetch lyrics from network.\nMay take longer to start playback."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER3), LL14(L"演奏のバッファ処理での割り込み時間を設定します。\n少なすぎると音飛びする可能性があります。", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches.", L"Set buffer interrupt time.\nToo low may cause audio glitches."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER5), LL14(L"描画の間隔時間を設定します。\nCPU使用が高いときに上げます。", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high.", L"Set render interval.\nIncrease when CPU usage is high."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER6), LL14(L"スペアナの表示倍率を設定します。", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale.", L"Set spectrum display scale."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO_LANG), LL14(L"UI表示言語を切り替えます。\n設定を保存して再起動後に反映されます。", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting.", L"Switch UI language.\nTakes effect after saving and restarting."));
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);

	m_ms.SetMode(1);
	m_hyouji2.SetMode(1);
	w_wups.SetMode(1);
	m_evr.SetCheck(savedata.evr);
	m_con.SetCheck(savedata.con);
	m_a.SetCheck(savedata.aero);
	COSVersion os;
	os.GetVersionString();
	if (os.in.dwMajorVersion < 6)
		m_a.ShowWindow(SW_HIDE);

	m_ffd.SetCheck(savedata.ffd);
	m_vob.SetCheck(savedata.vob);
	m_haali.SetCheck(savedata.haali);
	m_speana.SetCheck(savedata.speanamode);
	extern CPlayList* pl;
	extern COggDlg* og;
	extern int ip;
	ip = 0;
	og->KillTimer(4923);
	og->KillTimer(4924);
	if (pl) {
		pl->KillTimer(4923);
		pl->KillTimer(4924);
	}
#if WIN64
	m_kpi.EnableWindow(FALSE);
#else
#endif
	m_ms.SetRange(30, 80);
	if (savedata.ms < 30) savedata.ms = 30;
	m_ms.SetPos(savedata.ms);
	if (savedata.ms > 80) savedata.ms = 80;
	m_hyouji2.SetRange(1, 60);
	m_hyouji2.SetPos(savedata.ms2);
	{
		const wchar_t* msUnit = LL14(L"ms", L"ms", L"ms", L"ms", L"ms", L"ms", L"毫秒", L"ms", L"мс", L"ms", L"ms", L"ms", L"ms", L"ms");
		CString s; s.Format(L"%d%s", savedata.ms, msUnit);
		m_ms2.SetWindowText(s);
	}
	SetTimer(11, 100, NULL);
	w_wups.SetRange(100, 1000);
	w_wups.SetPos((int)savedata.wup);

	//sl = &m_soundlist;
	slgc = 0;
	m_soundlist.Clear();
	DirectSoundEnumerate(DSEnumCallback, NULL);
	for (int k = 0; k < slgc; k++) {
		m_soundlist.AddString(sls[k]);
	}
	m_soundlist.SetCurSel(savedata.soundcur);
	if (!pGraphBuilder)
		m_l.EnableWindow(FALSE);
	CString abc = savedata.zero;
	if (abc == L"") {
		m_ao.ShowWindow(FALSE);
	}
	// { 11025, 12000, 22050, 24000, 44100, 48000, 96000, 192000, 384000, 768000, 1536000, 3072000 };
	m_Hz.AddString(LL14(L"--低周波数帯- イコライザーでアップスケール対応し処理される", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed", L"--Low freq- EQ upscale processed"),TRUE);
	m_Hz.AddString(L"11025");
	m_Hz.AddString(L"12000");
	m_Hz.AddString(L"22050");
	m_Hz.AddString(L"24000");
	m_Hz.AddString(LL14(L"--通常波数帯- イコライザー通常処理される", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed", L"--Normal freq- EQ normal processed"), TRUE);
	m_Hz.AddString(L"44100");
	m_Hz.AddString(L"48000");
	m_Hz.AddString(L"96000");
	m_Hz.AddString(L"192000");
	m_Hz.AddString(LL14(L"--高周波数帯- イコライザー処理されない場合がある", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process", L"--High freq- EQ may not process"), TRUE);
	m_Hz.AddString(L"384000");
	m_Hz.AddString(L"768000");
	m_Hz.AddString(L"1536000");
	m_Hz.AddString(L"3072000 ");
	for (int l = 0; l < 12; l++) {
		if (savedata.samples == samp[l]) {
			m_Hz.SetCurSel(l);
			break;
		}
	}

	m_speana_num.AddString(LL14(L"音階", L"Scale", L"Gamme", L"Scala", L"Escala", L"음계", L"音阶", L"مقياس", L"Гамма", L"Tonleiter", L"Escala", L"Toonladder", L"Skala", L"Ölçek"));
	m_speana_num.AddString(LL14(L"低周波帯特化", L"Low freq focus", L"Focus basse fréquence", L"Focus basse frequenze", L"Enfoque bajas frecuencias", L"저주파 특화", L"低频特化", L"تركيز التردد المنخفض", L"Фокус низких частот", L"Tieffrequenz-Fokus", L"Foco baixas frequências", L"Laagfrequent focus", L"Fokus niskich częstotliwości", L"Düşük frekans odak"));
	m_speana_num.AddString(LL14(L"標準", L"Standard", L"Standard", L"Standard", L"Estándar", L"표준", L"标准", L"قياسي", L"Стандарт", L"Standard", L"Padrão", L"Standaard", L"Standard", L"Standart"));
	m_speana_num.AddString(LL14(L"高周波帯", L"High freq", L"Haute fréquence", L"Alta frequenza", L"Alta frecuencia", L"고주파대", L"高频带", L"التردد العالي", L"Высокие частоты", L"Hochfrequenz", L"Alta frequência", L"Hogefrequent", L"Wysokie częstotliwości", L"Yüksek frekans"));
	m_speana_num.AddString(LL14(L"音声特化", L"Voice focus", L"Focus vocal", L"Focus voce", L"Enfoque voz", L"음성 특화", L"人声特化", L"تركيز الصوت", L"Фокус голоса", L"Stimmen-Fokus", L"Foco vocal", L"Stemfocus", L"Fokus głosu", L"Ses odak"));
	m_speana_num.SetCurSel(savedata.speananum);


	m_netlrc.SetCheck(savedata.lrc_net);

	if (savedata.aero){
		renderbase = new CImageBase;
	renderbase->Create(NULL);
	renderbase->oya = this;
	}
	else {
		renderbase = NULL;
	}
	CRect r;
	GetWindowRect(&r);
	if (savedata.aero)
	renderbase->MoveWindow(&r);
	if(renderbase)
		::SetWindowPos(renderbase->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	return TRUE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}

BOOL CALLBACK CRender::DSEnumCallback(LPGUID pGUID, LPCWSTR strDesc,LPCWSTR strDrvName, LPVOID pContext)
{
	if (pGUID)
	{
		sls[slgc] = strDesc;
		CopyMemory(&slg[slgc], pGUID, sizeof(GUID));
		slgc++;
	}

	return TRUE;
}

void CRender::OnOK() 
{
	// TODO: この位置にその他の検証用のコードを追加してください
	savedata.render=m_1.GetCurSel();
	savedata.evr=m_evr.GetCheck();
	savedata.con=m_con.GetCheck();
	savedata.aero=m_a.GetCheck();
	savedata.ffd=m_ffd.GetCheck();
	savedata.vob=m_vob.GetCheck();
	savedata.haali=m_haali.GetCheck();
	savedata.audiost=m_audiost.GetCheck();
	savedata.bit24 = m_24.GetCheck();
	savedata.bit32 = m_32bit.GetCheck();
	savedata.m4a = m_m4a.GetCheck();
	savedata.ms = m_ms.GetPos();
	savedata.samples = samp[m_Hz.GetCurSel()];
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
	savedata.lang = m_comboLang.GetCurSel();

	//	savedata.mp3orig=m_mp3orig.GetCheck();
	if (savedata.aero)
	delete renderbase;
	CCustomBlurDialogExBase::OnOK();
}

INT_PTR CRender::OnToolHitTest(CPoint point, TOOLINFO* pTI) const
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。

	return CCustomBlurDialogExBase::OnToolHitTest(point, pTI);
}

BOOL CRender::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
		m_tooltip.RelayEvent(pMsg);

	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CRender::OnBnClickedCancel2()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CGraph *a = new CGraph(CWnd::FromHandle(GetSafeHwnd()));
	::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	if (renderbase)::SetWindowPos(renderbase->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	if(pGraphBuilder)
		a->DoModal();
	delete a;
}

void CRender::Onspc2x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(TRUE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=2;
}

void CRender::Onspc4x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(TRUE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=4;
}

void CRender::Onspc8x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(TRUE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=8;
}

void CRender::Onspc1x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(TRUE);
	savedata.spc=1;
}

void CRender::Onspc16x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(TRUE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=16;
}

void CRender::Onmp31()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(TRUE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=1;
}

void CRender::Onmp315()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(TRUE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=2;
}

void CRender::Onmp32()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(TRUE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=3;
}

void CRender::Onmp325()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(TRUE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=4;
}

void CRender::Onmp33()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(TRUE);
	savedata.mp3=5;
}

void CRender::Onkpi10()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(TRUE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=1;
}

void CRender::Onkpi15()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(TRUE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=2;
}

void CRender::Onkpi20()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(TRUE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=3;
}

void CRender::Onkpi25()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(TRUE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=4;
}

void CRender::Onkpi30()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(TRUE);
	savedata.kpivol=5;
}

void CRender::OnBnClicked24bit()
{
	savedata.bit24 = m_24.GetCheck();
	if (og) RenderRecreateSecondarySound(og);
}

void CRender::OnBnClicked32bit()
{
	savedata.bit32 = m_32bit.GetCheck();
	if (og) RenderRecreateSecondarySound(og);
}

void CRender::OnBnClickedCheck50()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.m4a = m_m4a.GetCheck();
}



#include "Kpilist.h"
void CRender::Onkpi()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	SetTimer(7000, 300, NULL);
}

extern HFONT	hFont;
#include "afxdlgs.h"
void CRender::OnFontMain()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	LOGFONT      logFont;
	CFont* f=CFont::FromHandle(hFont);
	f->GetLogFont(&logFont);
	CFontDialog fontDlg(&logFont);
	if (fontDlg.DoModal() == IDOK){
		DeleteObject(hFont);
		hFont=CreateFontIndirect(fontDlg.m_cf.lpLogFont);
	}

}
#include "PlayList.h"
extern CPlayList *pl;
void CRender::OnFontList()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	LOGFONT      logFont;
	CFont* f=pl->m_lc.GetFont();
	f->GetLogFont(&logFont);
	CFontDialog fontDlg(&logFont,CF_SCREENFONTS);
	if (fontDlg.DoModal() == IDOK && pl){
		pl->m_lc.SetFont(fontDlg.GetFont());
	}
}


void CRender::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.render = m_1.GetCurSel();
	savedata.evr = m_evr.GetCheck();
	savedata.con = m_con.GetCheck();
	savedata.aero = m_a.GetCheck();
	savedata.ffd = m_ffd.GetCheck();
	savedata.vob = m_vob.GetCheck();
	savedata.haali = m_haali.GetCheck();
	savedata.audiost = m_audiost.GetCheck();
	savedata.bit24 = m_24.GetCheck();
	savedata.bit32 = m_32bit.GetCheck();
	savedata.m4a = m_m4a.GetCheck();
	savedata.lrc_net = m_netlrc.GetCheck();
	savedata.ms = m_ms.GetPos();
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
	savedata.lang = m_comboLang.GetCurSel();
	int ihz = m_Hz.GetCurSel();
	if (ihz >= 0 && ihz < 12)
		savedata.samples = samp[ihz];
	savedata.upscale_enable = m_upscale.GetCheck() ? 1 : 0;
	int sp = m_speaker.GetCurSel();
	savedata.speaker_layout = (sp >= 0 && sp <= 5) ? sp : 0;
	int dev = m_soundlist.GetCurSel();
	if (dev >= 0) {
		memcpy(&savedata.soundguid, &slg[dev], sizeof(GUID));
		if (dev == 0)
			savedata.soundguid = { 0,0,0,0 };
		savedata.soundcur = dev;
	}
	if (og)
		RenderRecreateSecondarySound(og);
	extern int gameon;
	if(savedata.aero)
	delete renderbase;
	CCustomBlurDialogExBase::OnOK();
}




BOOL CRender::MySetFileType(LPCTSTR lpExt, LPCTSTR lpDocName, LPCTSTR lpDocType, LPCTSTR lpPath, LPCTSTR lpPath1)
{
	CRegKey reg;

	// lpExtをlpDocNameに関連付ける
	if (reg.SetValue(HKEY_CLASSES_ROOT, lpExt, lpDocName) != ERROR_SUCCESS)
		return FALSE;
	// lpDocName作成
	CString strDocNameTmp = lpDocName;
	CString strIcon = lpPath1; strIcon += ",0";
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp, lpDocType) != ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp + _T("\\shell"), _T("open"))
		!= ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp + _T("\\shell\\open\\command"),
		lpPath) != ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp + _T("\\DefaultIcon"),
		strIcon) != ERROR_SUCCESS)
		return FALSE;

	if (reg.SetValue(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\RegisteredApplications"),
		_T("SOFTWARE\\PrePrayerPowerSoft\\oggYSEDbgm_uni\\Capabilities"),_T("oggYSEDbgm_uni")) != ERROR_SUCCESS)
		return FALSE;
	if (reg.Create(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\PrePrayerPowerSoft\\oggYSEDbgm_uni\\Capabilities")) != ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\PrePrayerPowerSoft\\oggYSEDbgm_uni\\Capabilities"),
		strDocNameTmp, lpExt) != ERROR_SUCCESS)
		return FALSE;
	strIcon = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\");
	strIcon += lpExt;
	strIcon += _T("\\UserChoice");
	reg.Open(HKEY_CURRENT_USER, _T(""));
	if(reg.DeleteSubKey(strIcon) != ERROR_SUCCESS)
		return FALSE;
	reg.Close();
	if (reg.SetValue(HKEY_CURRENT_USER, strIcon,
		lpDocName, _T("Progid")) != ERROR_SUCCESS)
		return FALSE;

	// 関連付けが変更された事をシステムに通知
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	return TRUE;
}

extern TCHAR karento2[1024];

void CRender::OnBnClickedCancel4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CString s,ss;
	s = "\"";
	s += karento2;
	s += "oggYSEDbgm_uni.exe\" \"%1\"";
	ss = karento2;
	ss += "oggYSEDbgm_uni.exe";
	MySetFileType(_T(".mp3"), _T("oggYSEDbgm_uni.exe.mp3"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mp2"), _T("oggYSEDbgm_uni.exe.mp2"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mp1"), _T("oggYSEDbgm_uni.exe.mp1"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".rmp"), _T("oggYSEDbgm_uni.exe.rmp"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".flac"), _T("oggYSEDbgm_uni.exe.flac"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".m4a"), _T("oggYSEDbgm_uni.exe.m4a"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".aac"), _T("oggYSEDbgm_uni.exe.aac"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".avi"), _T("oggYSEDbgm_uni.exe.avi"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mp4"), _T("oggYSEDbgm_uni.exe.mp4"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mkv"), _T("oggYSEDbgm_uni.exe.mkv"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".wmv"), _T("oggYSEDbgm_uni.exe.wmv"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mpg"), _T("oggYSEDbgm_uni.exe.mpg"), LL14(L"簡易プレイヤで開く", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player", L"Open with Simple Player"), s, ss);
	// 関連付けが変更された事をシステムに通知
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	MessageBox(LL14(L"一応関連づけを走らせてみました。\nmp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpgに関連をつけました。", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg.", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg."));
	// 関連付けが変更された事をシステムに通知
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}


void CRender::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	if (nIDEvent == 7000) {
		KillTimer(7000);
		CKpilist k;
		k.status = 0;
		k.DoModal();
		return;
	}
	savedata.ms = m_ms.GetPos();
	{
		const wchar_t* msUnit = LL14(L"ms", L"ms", L"ms", L"ms", L"ms", L"ms", L"毫秒", L"ms", L"мс", L"ms", L"ms", L"ms", L"ms", L"ms");
		CString s; s.Format(L"%d%s", savedata.ms, msUnit);
		m_ms2.SetWindowText(s);
	}
	savedata.ms2 = m_hyouji2.GetPos();
	{
		const wchar_t* msUnit = LL14(L"ms", L"ms", L"ms", L"ms", L"ms", L"ms", L"毫秒", L"ms", L"мс", L"ms", L"ms", L"ms", L"ms", L"ms");
		CString s2; s2.Format(L"%d%s", savedata.ms2*16, msUnit);
		m_hyouji3.SetWindowText(s2);
	}
	savedata.wup = w_wups.GetPos()/ 100.0;
	CString s;
	{
		const wchar_t* fmt = LL14(L"%1.2lf倍", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%.2f倍", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx", L"%1.2lfx");
		s.Format(fmt, savedata.wup);
	}
	m_wup.SetWindowText(s);
	if (nIDEvent == 90) {
		KillTimer(90);
//		::SetWindowPos(renderbase->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CRender::OnCbnSelchangeCombo2()
{
	int dev = m_soundlist.GetCurSel();
	if (dev < 0) return;
	memcpy(&savedata.soundguid, &slg[dev], sizeof(GUID));
	if (dev == 0)
		savedata.soundguid = { 0,0,0,0 };
	savedata.soundcur = dev;
	if (og)
		RenderRecreateSecondarySound(og);
}

void CRender::OnCbnSelchangeSpeaker()
{
	int s = m_speaker.GetCurSel();
	if (s >= 0 && s <= 5)
		savedata.speaker_layout = s;
	if (og)
		RenderRecreateSecondarySound(og);
}

void CRender::OnBnClickedCheckUpscale()
{
	savedata.upscale_enable = m_upscale.GetCheck() ? 1 : 0;
	if (og)
		RenderRecreateSecondarySound(og);
}


void CRender::OnBnClickedButton1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CZeroFol z;
	z.DoModal();
}


void CRender::OnCbnSelchangeCombo3()
{
	int ihz = m_Hz.GetCurSel();
	if (ihz >= 0 && ihz < 12)
		savedata.samples = samp[ihz];
	if (og)
		RenderRecreateSecondarySound(og);
}


HBRUSH CRender::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);

	// TODO: ここで DC の属性を変更してください。
	if (savedata.aero == 1) {
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
	// TODO: 既定値を使用したくない場合は別のブラシを返します。
	return hbr;
}


int CRender::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomBlurDialogExBase::OnCreate(lpCreateStruct) == -1)
		return -1;

	// TODO: ここに特定な作成コードを追加してください。
	ModifyStyleEx(0, WS_EX_LAYERED);

	// レイヤードウィンドウの不透明度と透明のカラーキー
	SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);

	// 赤色のブラシを作成する．
	m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	return 0;
}


void CRender::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogExBase::OnMoving(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if (savedata.aero)

	renderbase->MoveWindow(&r);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}

int CRender::Create(CWnd* pWnd)
{
	m_pParent = NULL;
	BOOL bret = CCustomBlurDialogExBase::Create(CRender::IDD, this);
	if (savedata.aero == 1) {
		ModifyStyleEx(0, WS_EX_LAYERED);

		// レイヤードウィンドウの不透明度と透明のカラーキー
		SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);

		// 赤色のブラシを作成する．
		m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	}
	if (bret == TRUE)
		ShowWindow(SW_SHOW);
	return bret;
}

void CRender::OnBnClickedCancel()
{
	savedata.soundguid = m_bakSoundGuid;
	savedata.soundcur = m_bakSoundCur;
	savedata.samples = m_bakSamples;
	savedata.upscale_enable = m_bakUpscale;
	savedata.speaker_layout = m_bakSpeaker;
	savedata.bit24 = m_bakBit24;
	savedata.bit32 = m_bakBit32;
	m_24.SetCheck(savedata.bit24);
	m_32bit.SetCheck(savedata.bit32);
	m_upscale.SetCheck(savedata.upscale_enable ? BST_CHECKED : BST_UNCHECKED);
	{
		int sp = savedata.speaker_layout;
		if (sp < 0 || sp > 5) sp = 0;
		m_speaker.SetCurSel(sp);
	}
	if (m_soundlist.GetCount() > 0 && savedata.soundcur >= 0 && savedata.soundcur < m_soundlist.GetCount())
		m_soundlist.SetCurSel(savedata.soundcur);
	for (int l = 0; l < 12; l++) {
		if (savedata.samples == samp[l]) {
			m_Hz.SetCurSel(l);
			break;
		}
	}
	if (og)
		RenderRecreateSecondarySound(og);
	if (savedata.aero)

	delete renderbase;
	CCustomBlurDialogExBase::OnCancel();
}

void CRender::OnBnClickedCheck52()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
}

void CRender::OnCbnEditchangeCombo4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_speana.SetCheck(TRUE);
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
}

void CRender::OnCbnSelchangeCombo4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_speana.SetCheck(TRUE);
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
}

void CRender::OnBnClickedCancel5()
{
	// プログラムの実行ファイルパスを取得
	TCHAR szExePath[MAX_PATH];
	GetModuleFileName(NULL, szExePath, MAX_PATH);

	CString strProgID = _T("falcombgm.mediaplayer");
	CString strAppName = LL14(L"Falcom BGM&メディアプレイヤー", L"Falcom BGM&Media Player", L"Falcom BGM et lecteur média", L"Falcom BGM e lettore multimediale", L"Falcom BGM y reproductor multimedia", L"Falcom BGM 미디어 플레이어", L"Falcom BGM 媒体播放器", L"Falcom BGM ومشغل الوسائط", L"Falcom BGM и медиаплеер", L"Falcom BGM & Media Player", L"Falcom BGM e reprodutor multimédia", L"Falcom BGM & media player", L"Falcom BGM i odtwarzacz multimediów", L"Falcom BGM ve medya oynatıcı");

	// 対応拡張子一覧
	const TCHAR* extensions[] = {
		_T(".mp3"), _T(".mp2"), _T(".mp1"), _T(".rmp"),
		_T(".ogg"), _T(".flac"), _T(".m4a"), _T(".aac"),
		_T(".dsf"), _T(".dff"), _T(".mp4"), _T(".mkv"), _T(".avi")
	};

	HKEY hKey;
	LONG result;

	// 1. ProgIDの登録
	CString strProgIDKey = _T("Software\\Classes\\") + strProgID;
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strProgIDKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		RegSetValueEx(hKey, NULL, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strAppName,
			(strAppName.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 2. DefaultIconの設定
	CString strIconKey = strProgIDKey + _T("\\DefaultIcon");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strIconKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strIcon;
		strIcon.Format(_T("%s,0"), szExePath);
		RegSetValueEx(hKey, NULL, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strIcon,
			(strIcon.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 3. shell\open\commandの設定
	CString strCommandKey = strProgIDKey + _T("\\shell\\open\\command");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strCommandKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strCommand;
		strCommand.Format(_T("\"%s\" \"%%1\""), szExePath);
		RegSetValueEx(hKey, NULL, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strCommand,
			(strCommand.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 4. 各拡張子にOpenWithProgidsを設定
	for (int i = 0; i < _countof(extensions); i++)
	{
		CString strExtKey;
		strExtKey.Format(_T("Software\\Classes\\%s\\OpenWithProgids"), extensions[i]);

		result = RegCreateKeyEx(HKEY_CURRENT_USER, strExtKey, 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
		if (result == ERROR_SUCCESS)
		{
			// 値は空でOK(値の存在自体が意味を持つ)
			RegSetValueEx(hKey, strProgID, 0, REG_NONE, NULL, 0);
			RegCloseKey(hKey);
		}
	}

	// 5. アプリケーションの登録
	CString strAppKey = _T("Software\\") + strAppName;
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strAppKey + _T("\\Capabilities"),
		0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strDesc = LL14(L"ファルコムゲームBGMのループ再生対応メディアプレイヤー", L"Falcom game BGM loop playback media player", L"Lecteur média pour BGM Falcom avec boucle", L"Lettore media BGM Falcom con loop", L"Reproductor de BGM Falcom con bucle", L"팔콤 게임 BGM 루프 재생 미디어 플레이어", L"支持 Falcom 游戏 BGM 循环播放的媒体播放器", L"مشغل وسائط BGM ألعاب فالكوم مع تكرار", L"Медиаплеер для BGM Falcom с зацикливанием", L"Falcom-Spiel-BGM-Loop-Medienplayer", L"Reprodutor BGM Falcom com loop", L"Falcom BGM loop-mediaspeler", L"Odtwarzacz mediów BGM Falcom z pętlą", L"Falcom oyun BGM döngü medya oynatıcı");
		RegSetValueEx(hKey, _T("ApplicationDescription"), 0, REG_SZ,
			(BYTE*)(LPCTSTR)strDesc,
			(strDesc.GetLength() + 1) * sizeof(TCHAR));
		RegSetValueEx(hKey, _T("ApplicationName"), 0, REG_SZ,
			(BYTE*)(LPCTSTR)strAppName,
			(strAppName.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 6. ファイル関連付けの登録
	CString strFileAssocKey = strAppKey + _T("\\Capabilities\\FileAssociations");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strFileAssocKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		for (int i = 0; i < _countof(extensions); i++)
		{
			RegSetValueEx(hKey, extensions[i], 0, REG_SZ,
				(BYTE*)(LPCTSTR)strProgID,
				(strProgID.GetLength() + 1) * sizeof(TCHAR));
		}
		RegCloseKey(hKey);
	}

	// 7. Registered Applicationsに登録
	CString strRegAppKey = _T("Software\\RegisteredApplications");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strRegAppKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strCapPath = _T("Software\\") + strAppName + _T("\\Capabilities");
		RegSetValueEx(hKey, strAppName, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strCapPath,
			(strCapPath.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 8. 変更をシステムに通知
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

	AfxMessageBox(LL14(L"ファイルの関連付け登録が完了しました。", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed.", L"File association registration completed."), MB_ICONINFORMATION);
}
