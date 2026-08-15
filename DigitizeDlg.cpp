#include "stdafx.h"
#include "ogg.h"
#include "DigitizeDlg.h"
#include "AudioDevSync.h"
#include "TranscodeExport.h"
#include "resource.h"
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <FunctionDiscoveryKeys_devpkey.h>
#include <process.h>
#include <math.h>
#include <ShlObj.h>

#pragma comment(lib, "Ole32.lib")
extern void MpPersistSavedataQuick();

namespace {
static const GUID kFloat = { 3, 0, 0x10,{ 0x80,0,0,0xaa,0,0x38,0x9b,0x71 } };
static const GUID kPcm = { 1, 0, 0x10,{ 0x80,0,0,0xaa,0,0x38,0x9b,0x71 } };

static float DigClamp(float v) { return v > 1.f ? 1.f : (v < -1.f ? -1.f : v); }
static void DigSampleToFloat(const BYTE* p, const WAVEFORMATEX* f, float& l, float& r)
{
	l = r = 0.f; if (!p || !f) return;
	const int ch = f->nChannels, bits = f->wBitsPerSample;
	const BOOL flt = f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
		(f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && bits == 32 &&
			((const WAVEFORMATEXTENSIBLE*)f)->SubFormat == kFloat);
	const BOOL pcm = f->wFormatTag == WAVE_FORMAT_PCM ||
		(f->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
			((const WAVEFORMATEXTENSIBLE*)f)->SubFormat == kPcm);
	if (flt) { const float* q = (const float*)p; l = q[0]; r = ch > 1 ? q[1] : l; }
	else if (pcm && bits == 16) { const short* q = (const short*)p; l = q[0] / 32768.f; r = ch > 1 ? q[1] / 32768.f : l; }
	else if (pcm && bits == 24) {
		int a = p[0] | p[1] << 8 | p[2] << 16; if (a & 0x800000) a |= ~0xffffff; l = a / 8388608.f;
		if (ch > 1) { p += 3; a = p[0] | p[1] << 8 | p[2] << 16; if (a & 0x800000) a |= ~0xffffff; r = a / 8388608.f; } else r = l;
	} else if (pcm && bits == 32) { const int* q = (const int*)p; l = q[0] / 2147483648.f; r = ch > 1 ? q[1] / 2147483648.f : l; }
}
static void DigWriteWavHeader(CFile& f)
{
	BYTE h[80] = {}; memcpy(h, "RIFF", 4); memcpy(h + 8, "WAVE", 4);
	memcpy(h + 12, "JUNK", 4); *(DWORD*)(h + 16) = 28; memcpy(h + 48, "fmt ", 4);
	*(DWORD*)(h + 52) = 16; *(WORD*)(h + 56) = WAVE_FORMAT_PCM; *(WORD*)(h + 58) = 2;
	*(DWORD*)(h + 60) = 48000; *(DWORD*)(h + 64) = 192000; *(WORD*)(h + 68) = 4;
	*(WORD*)(h + 70) = 16; memcpy(h + 72, "data", 4); f.Write(h, sizeof(h));
}
static void DigFinalizeWavHeader(CFile& f)
{
	ULONGLONG len = f.GetLength(), data = len > 80 ? len - 80 : 0;
	f.SeekToBegin();
	if (data <= 0x7fffffff) {
		BYTE h[12]; memcpy(h, "RIFF", 4); *(DWORD*)(h + 4) = (DWORD)(len - 8); memcpy(h + 8, "WAVE", 4); f.Write(h, 12);
		f.Seek(76, CFile::begin); DWORD n = (DWORD)data; f.Write(&n, 4);
	} else {
		BYTE h[48] = {}; memcpy(h, "RF64", 4); *(DWORD*)(h + 4) = 0xffffffff; memcpy(h + 8, "WAVE", 4);
		memcpy(h + 12, "ds64", 4); *(DWORD*)(h + 16) = 28; *(__int64*)(h + 20) = len - 8;
		*(__int64*)(h + 28) = data; *(__int64*)(h + 36) = data / 4; f.Write(h, 48);
		f.Seek(76, CFile::begin); DWORD n = 0xffffffff; f.Write(&n, 4);
	}
}
static CString DigTempPath()
{
	TCHAR d[MAX_PATH] = {}; GetTempPath(MAX_PATH, d);
	CString s; s.Format(L"%sogg_digitize_%u_%u.wav", d, GetCurrentProcessId(), GetTickCount()); return s;
}
static CString DigText(int kind)
{
	if (kind == 0) return LL14(L"アナログ起こし台",L"Analog digitizer",L"Numériseur analogique",L"Digitalizzatore analogico",L"Digitalizador analógico",L"아날로그 디지타이저",L"模拟数字化",L"محول تماثلي رقمي",L"Оцифровка аналога",L"Analog-Digitalisierer",L"Digitalizador analógico",L"Analoge digitizer",L"Digitalizacja analogowa",L"Analog dijitalleştirici");
	if (kind == 1) return LL14(L"録音中…",L"Recording…",L"Enregistrement…",L"Registrazione…",L"Grabando…",L"녹음 중…",L"录音中…",L"جارٍ التسجيل…",L"Запись…",L"Aufnahme…",L"A gravar…",L"Opnemen…",L"Nagrywanie…",L"Kaydediliyor…");
	if (kind == 2) return LL14(L"録音開始",L"Start recording",L"Démarrer",L"Avvia",L"Iniciar",L"녹음 시작",L"开始录音",L"بدء التسجيل",L"Начать",L"Aufnahme starten",L"Iniciar",L"Opname starten",L"Rozpocznij",L"Kaydı başlat");
	return LL14(L"録音停止",L"Stop recording",L"Arrêter",L"Ferma",L"Detener",L"녹음 중지",L"停止录音",L"إيقاف التسجيل",L"Остановить",L"Aufnahme stoppen",L"Parar",L"Opname stoppen",L"Zatrzymaj",L"Kaydı durdur");
}

class CDigHelp : public CDialog {
public: CDigHelp(CWnd* p) : CDialog(IDD_DIG_HELP, p) {}
protected:
	virtual BOOL OnInitDialog() {
		CDialog::OnInitDialog(); SetWindowText(DigText(0));
		if (CWnd* w=GetDlgItem(IDOK)) w->SetWindowText(LL14(L"閉じる",L"Close",L"Fermer",L"Chiudi",L"Cerrar",L"닫기",L"关闭",L"إغلاق",L"Закрыть",L"Schließen",L"Fechar",L"Sluiten",L"Zamknij",L"Kapat")); return TRUE;
	}
	virtual void OnOK(){DestroyWindow();} virtual void OnCancel(){DestroyWindow();}
	virtual void PostNcDestroy();
	afx_msg void OnPaint(); afx_msg BOOL OnEraseBkgnd(CDC* d){CRect r;GetClientRect(r);d->FillSolidRect(r,RGB(248,248,252));return TRUE;}
	afx_msg void OnClose(){DestroyWindow();} DECLARE_MESSAGE_MAP()
};
static CDigHelp* g_help = NULL;
BEGIN_MESSAGE_MAP(CDigHelp,CDialog) ON_WM_PAINT() ON_WM_ERASEBKGND() ON_WM_CLOSE() END_MESSAGE_MAP()
void CDigHelp::PostNcDestroy(){CDialog::PostNcDestroy();if(g_help==this)g_help=NULL;delete this;}
void CDigHelp::OnPaint()
{
	CPaintDC p(this); CCC_GdiHelpPaint h; if(!CCC_GdiHelpBeginPaint(this,p,h))return;
	CDC& d=h.mem; d.SetBkMode(TRANSPARENT); d.SelectObject(GetFont()); int y=10, lh=20;
	d.SetTextColor(RGB(55,45,85)); d.TextOut(10,y,DigText(0)); y+=lh+4; d.SetTextColor(RGB(65,65,80));
	const wchar_t* rows[]={
		LL14(L"入力を選び、WAV / MP3 / FLAC の保存先を指定します。",L"Choose an input and WAV / MP3 / FLAC destination.",L"Choisissez l'entrée et la destination WAV / MP3 / FLAC.",L"Scegli ingresso e destinazione WAV / MP3 / FLAC.",L"Elija entrada y destino WAV / MP3 / FLAC.",L"입력과 WAV / MP3 / FLAC 저장 위치를 선택합니다.",L"选择输入及 WAV / MP3 / FLAC 保存位置。",L"اختر الإدخال ووجهة WAV / MP3 / FLAC.",L"Выберите вход и путь WAV / MP3 / FLAC.",L"Eingang und WAV-/MP3-/FLAC-Ziel wählen.",L"Escolha entrada e destino WAV / MP3 / FLAC.",L"Kies invoer en WAV-/MP3-/FLAC-doel.",L"Wybierz wejście i cel WAV / MP3 / FLAC.",L"Giriş ve WAV / MP3 / FLAC hedefini seçin."),
		LL14(L"HPF は低域ノイズ、Gate は小さな無音時ノイズを抑えます。",L"HPF reduces rumble; Gate suppresses low-level idle noise.",L"Le HPF réduit le grave; Gate coupe le faible bruit.",L"HPF riduce i bassi; Gate attenua il rumore lieve.",L"HPF reduce graves; Gate suprime ruido bajo.",L"HPF는 저역, Gate는 작은 대기 잡음을 줄입니다.",L"HPF 降低低频；Gate 抑制微小底噪。",L"يقلل HPF الجهير وتكبح البوابة الضوضاء الخفيفة.",L"HPF убирает гул; Gate — тихий шум.",L"HPF mindert Rumpeln; Gate leises Grundrauschen.",L"HPF reduz graves; Gate suprime ruído baixo.",L"HPF vermindert laag; Gate zachte ruis.",L"HPF tłumi dół; Gate cichy szum.",L"HPF dip gürültüyü, Gate hafif boşta gürültüyü azaltır."),
		LL14(L"モニタONは処理後の16-bit stereo音を選択出力へ返します。",L"Monitor sends processed 16-bit stereo audio to the selected output.",L"Monitor envoie le son traité 16-bit stéréo vers la sortie.",L"Monitor invia audio elaborato stereo 16-bit all'uscita.",L"Monitor envía audio procesado estéreo 16-bit a la salida.",L"모니터는 처리된 16비트 스테레오를 선택 출력으로 보냅니다.",L"监听将处理后的 16 位立体声送到所选输出。",L"يرسل الرصد صوت ستيريو 16 بت معالجًا إلى الخرج.",L"Монитор выводит обработанный 16-битный стереозвук.",L"Monitor sendet verarbeitetes 16-Bit-Stereo zum Ausgang.",L"Monitor envia estéreo 16-bit processado à saída.",L"Monitor stuurt verwerkt 16-bit stereo naar de uitgang.",L"Monitor wysyła przetworzone stereo 16-bit na wyjście.",L"Monitör işlenmiş 16-bit stereoyu seçili çıkışa yollar."),
		LL14(L"入力レベルを確認し、クリップする場合は Gain を下げてください。",L"Watch the input meter; lower Gain if it clips.",L"Surveillez le niveau; baissez Gain en cas d'écrêtage.",L"Controlla il livello; riduci Gain se distorce.",L"Observe el nivel; baje Gain si satura.",L"입력 미터를 보고 클리핑 시 Gain을 낮추세요.",L"观察输入电平；削波时降低 Gain。",L"راقب المستوى وخفّض الكسب عند القص.",L"Следите за уровнем; при клиппинге снизьте Gain.",L"Pegel beobachten; bei Clipping Gain senken.",L"Observe o nível; reduza Gain se saturar.",L"Let op niveau; verlaag Gain bij clipping.",L"Obserwuj poziom; zmniejsz Gain przy przesterze.",L"Seviyeyi izleyin; kırpılmada Gain'i azaltın.")
	};
	for(int i=0;i<4;i++){d.TextOut(10,y,rows[i]);y+=lh;} CCC_GdiHelpEndPaint(h);
}
}

IMPLEMENT_DYNAMIC(CDigitizeDlg,CCustomBlurDialogBase)
static CDigitizeDlg* g_digitize = NULL;
CDigitizeDlg::CDigitizeDlg(CWnd* p):CCustomBlurDialogBase(IDD,p),m_capCnt(0),m_monCnt(0),m_stop(0),m_run(0),m_peak(0),m_pcmBytes(0),m_lastHr(0),m_thread(NULL),m_csInit(FALSE),m_uiLocked(FALSE),m_stopping(FALSE),m_outFmt(0),m_mp3Kbps(192),m_flacLevel(5),m_hpfHz(0),m_gainPct(100),m_gatePct(0),m_startTick(0)
{ memset(m_capIds,0,sizeof(m_capIds));memset(m_monIds,0,sizeof(m_monIds)); }
CDigitizeDlg::~CDigitizeDlg(){InterlockedExchange(&m_stop,1);if(m_thread){WaitForSingleObject(m_thread,5000);CloseHandle(m_thread);}if(m_file.m_hFile!=CFile::hFileNull)m_file.Close();if(m_csInit)DeleteCriticalSection(&m_fileCs);}
void CDigitizeDlg::DoDataExchange(CDataExchange* p)
{
	CCustomBlurDialogBase::DoDataExchange(p);
	DDX_Control(p,IDC_DIG_HELP,m_help);DDX_Control(p,IDC_DIG_CAP_L,m_capL);DDX_Control(p,IDC_DIG_CAP,m_cap);DDX_Control(p,IDC_DIG_CAP_REFRESH,m_capRefresh);DDX_Control(p,IDC_DIG_MON_REFRESH,m_monRefresh);
	DDX_Control(p,IDC_DIG_MON_L,m_monL);DDX_Control(p,IDC_DIG_MON,m_mon);DDX_Control(p,IDC_DIG_MONITOR,m_monitor);
	DDX_Control(p,IDC_DIG_FMT_L,m_fmtL);DDX_Control(p,IDC_DIG_FMT,m_fmt);DDX_Control(p,IDC_DIG_QUAL_L,m_qualL);DDX_Control(p,IDC_DIG_QUAL,m_qual);
	DDX_Control(p,IDC_DIG_PATH_L,m_pathL);DDX_Control(p,IDC_DIG_PATH,m_path);DDX_Control(p,IDC_DIG_BROWSE,m_browse);
	DDX_Control(p,IDC_DIG_HPF_L,m_hpfL);DDX_Control(p,IDC_DIG_HPF,m_hpf);DDX_Control(p,IDC_DIG_GAIN_L,m_gainL);DDX_Control(p,IDC_DIG_GAIN,m_gain);
	DDX_Control(p,IDC_DIG_GATE_L,m_gateL);DDX_Control(p,IDC_DIG_GATE,m_gate);DDX_Control(p,IDC_DIG_METER_L,m_meterL);DDX_Control(p,IDC_DIG_METER,m_meter);
	DDX_Control(p,IDC_DIG_START,m_start);DDX_Control(p,IDC_DIG_CLOSE,m_close);DDX_Control(p,IDC_DIG_TIME,m_time);DDX_Control(p,IDC_DIG_STATUS,m_status);
}
BEGIN_MESSAGE_MAP(CDigitizeDlg,CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_DIG_BROWSE,OnBrowse) ON_BN_CLICKED(IDC_DIG_START,OnStart) ON_BN_CLICKED(IDC_DIG_CLOSE,OnCloseBtn) ON_BN_CLICKED(IDC_DIG_HELP,OnHelp)
	ON_BN_CLICKED(IDC_DIG_CAP_REFRESH,OnMicDevRefresh) ON_BN_CLICKED(IDC_DIG_MON_REFRESH,OnMicDevRefresh) ON_MESSAGE(WM_AUDIODEV_CHANGED,OnAudioDevChanged)
	ON_CBN_SELCHANGE(IDC_DIG_FMT,OnFormat) ON_CBN_SELCHANGE(IDC_DIG_CAP,OnControlChanged) ON_CBN_SELCHANGE(IDC_DIG_MON,OnControlChanged)
	ON_CBN_SELCHANGE(IDC_DIG_HPF,OnControlChanged) ON_CBN_SELCHANGE(IDC_DIG_GAIN,OnControlChanged) ON_CBN_SELCHANGE(IDC_DIG_GATE,OnControlChanged)
	ON_BN_CLICKED(IDC_DIG_MONITOR,OnControlChanged) ON_WM_TIMER() ON_WM_SIZE() ON_WM_DESTROY()
END_MESSAGE_MAP()
BOOL CDigitizeDlg::PreTranslateMessage(MSG* p){if(m_tooltip.GetSafeHwnd())m_tooltip.RelayEvent(p);return CCustomBlurDialogBase::PreTranslateMessage(p);}
void CDigitizeDlg::PostNcDestroy(){CCustomBlurDialogBase::PostNcDestroy();if(g_digitize==this)g_digitize=NULL;delete this;}
void CDigitizeDlg::LayoutHelpBtn(){CCC_CaptionPlaceHelpBtn(m_hWnd,&m_help);}
void CDigitizeDlg::FillDevices()
{
	AudioMicDevRefresh();m_cap.ResetContent();m_capCnt=0;int sel=0;
	for(int i=0;i<AudioMicDevCount()&&m_capCnt<DIG_DEV_MAX;i++){LPCTSTR id=AudioMicDevId(i);if(i>0&&(!id||!*id))continue;_tcsncpy(m_capIds[m_capCnt],id,255);m_capIds[m_capCnt][255]=0;m_cap.AddString(AudioMicDevName(i));if(!_tcsicmp(savedata.dig_cap_device,id))sel=m_capCnt++ ;else m_capCnt++;}
	if(m_capCnt)m_cap.SetCurSel(sel);
	AudioLoopDevRefresh();m_mon.ResetContent();m_monCnt=0;sel=0;
	for(int i=0;i<AudioLoopDevCount()&&m_monCnt<DIG_DEV_MAX;i++){LPCTSTR id=AudioLoopDevId(i);if(i>0&&(!id||!*id))continue;_tcsncpy(m_monIds[m_monCnt],id,255);m_monIds[m_monCnt][255]=0;m_mon.AddString(AudioLoopDevName(i));if(!_tcsicmp(savedata.dig_mon_device,id))sel=m_monCnt;m_monCnt++;}
	if(m_monCnt)m_mon.SetCurSel(sel);
}
void CDigitizeDlg::FillSettings()
{
	m_fmt.AddString(L"WAV");m_fmt.AddString(L"MP3");m_fmt.AddString(L"FLAC");m_fmt.SetCurSel(max(0,min(2,savedata.dig_format)));
	static const int hp[]={0,40,60,80,100,150,200,300,400};for(int i=0;i<9;i++){CString s;if(hp[i])s.Format(L"%d Hz",hp[i]);else s=L"OFF";m_hpf.AddString(s);if(hp[i]==savedata.dig_hpf_hz)m_hpf.SetCurSel(i);}if(m_hpf.GetCurSel()<0)m_hpf.SetCurSel(0);
	static const int gn[]={0,25,50,75,100,125,150,175,200};for(int i=0;i<9;i++){CString s;s.Format(L"%d%%",gn[i]);m_gain.AddString(s);if(gn[i]==savedata.dig_gain)m_gain.SetCurSel(i);}if(m_gain.GetCurSel()<0)m_gain.SetCurSel(4);
	for(int i=0;i<=10;i++){CString s;s.Format(L"%d%%",i*10);m_gate.AddString(s);if(i*10==savedata.dig_gate)m_gate.SetCurSel(i);}if(m_gate.GetCurSel()<0)m_gate.SetCurSel(0);
	OnFormat();m_monitor.SetCheck(savedata.dig_monitor?BST_CHECKED:BST_UNCHECKED);m_path.SetWindowText(savedata.dig_last_path);m_time.SetWindowText(L"00:00");
}
void CDigitizeDlg::OnFormat()
{
	if(m_uiLocked)return;int f=m_fmt.GetCurSel();m_qual.ResetContent();
	if(f==1){static const int q[]={128,160,192,224,256,320};for(int i=0;i<6;i++){CString s;s.Format(L"%d kbps",q[i]);m_qual.AddString(s);if(q[i]==savedata.dig_mp3_kbps)m_qual.SetCurSel(i);}if(m_qual.GetCurSel()<0)m_qual.SetCurSel(2);}
	else if(f==2){for(int i=0;i<=8;i++){CString s;s.Format(L"%d",i);m_qual.AddString(s);}m_qual.SetCurSel(max(0,min(8,savedata.dig_flac_level)));}
	else {m_qual.AddString(L"PCM 16-bit / 48 kHz");m_qual.SetCurSel(0);}
	m_qual.Invalidate(FALSE);
}
void CDigitizeDlg::PersistUi()
{
	int i=m_cap.GetCurSel();if(i>=0&&i<m_capCnt)_tcsncpy(savedata.dig_cap_device,m_capIds[i],255);savedata.dig_cap_device[255]=0;
	i=m_mon.GetCurSel();if(i>=0&&i<m_monCnt)_tcsncpy(savedata.dig_mon_device,m_monIds[i],255);savedata.dig_mon_device[255]=0;
	savedata.dig_monitor=m_monitor.GetCheck()==BST_CHECKED;savedata.dig_format=max(0,min(2,m_fmt.GetCurSel()));
	static const int kb[]={128,160,192,224,256,320};i=m_qual.GetCurSel();savedata.dig_mp3_kbps=kb[max(0,min(5,i))];savedata.dig_flac_level=max(0,min(8,i));
	static const int hp[]={0,40,60,80,100,150,200,300,400},gn[]={0,25,50,75,100,125,150,175,200};
	savedata.dig_hpf_hz=hp[max(0,min(8,m_hpf.GetCurSel()))];savedata.dig_gain=gn[max(0,min(8,m_gain.GetCurSel()))];savedata.dig_gate=max(0,min(10,m_gate.GetCurSel()))*10;
	CString s;m_path.GetWindowText(s);_tcsncpy(savedata.dig_last_path,s,1023);savedata.dig_last_path[1023]=0;MpPersistSavedataQuick();
}
BOOL CDigitizeDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();InitializeCriticalSection(&m_fileCs);m_csInit=TRUE;CCC_BringDialogToForeground(this);
	m_help.SetWindowText(L"?");m_help.SetFlat(TRUE);m_help.SetGradation(RGB(255,245,220),RGB(240,210,160),0,TRUE);
	m_cap.SetAeroMode(FALSE);m_mon.SetAeroMode(FALSE);m_monitor.SetAeroMode(FALSE);
	m_fmt.SetAeroMode(FALSE);m_qual.SetAeroMode(FALSE);
	m_hpf.SetAeroMode(FALSE);m_gain.SetAeroMode(FALSE);m_gate.SetAeroMode(FALSE);m_meter.SetAeroMode(FALSE);
	SetWindowText(DigText(0));m_capL.SetWindowText(LL14(L"入力",L"Input",L"Entrée",L"Ingresso",L"Entrada",L"입력",L"输入",L"الإدخال",L"Вход",L"Eingang",L"Entrada",L"Invoer",L"Wejście",L"Giriş"));
	m_monL.SetWindowText(LL14(L"モニタ",L"Monitor",L"Moniteur",L"Monitor",L"Monitor",L"모니터",L"监听",L"المراقبة",L"Монитор",L"Monitor",L"Monitor",L"Monitor",L"Monitor",L"Monitör"));
	m_monitor.SetWindowText(LL14(L"モニタON",L"Monitor on",L"Moniteur actif",L"Monitor attivo",L"Monitor activo",L"모니터 켬",L"开启监听",L"تشغيل المراقبة",L"Монитор вкл.",L"Monitor an",L"Monitor ligado",L"Monitor aan",L"Monitor wł.",L"Monitör açık"));
	m_fmtL.SetWindowText(LL14(L"形式",L"Format",L"Format",L"Formato",L"Formato",L"형식",L"格式",L"التنسيق",L"Формат",L"Format",L"Formato",L"Formaat",L"Format",L"Biçim"));
	m_qualL.SetWindowText(LL14(L"品質",L"Quality",L"Qualité",L"Qualità",L"Calidad",L"품질",L"质量",L"الجودة",L"Качество",L"Qualität",L"Qualidade",L"Kwaliteit",L"Jakość",L"Kalite"));
	m_pathL.SetWindowText(LL14(L"保存先",L"Save path",L"Chemin",L"Percorso",L"Ruta",L"저장 위치",L"保存路径",L"مسار الحفظ",L"Путь",L"Speicherpfad",L"Caminho",L"Opslagpad",L"Ścieżka",L"Kayıt yolu"));
	m_hpfL.SetWindowText(L"HPF");m_gainL.SetWindowText(L"Gain");m_gateL.SetWindowText(L"Gate");m_meterL.SetWindowText(L"In");
	m_start.SetWindowText(DigText(2));m_close.SetWindowText(LL14(L"閉じる",L"Close",L"Fermer",L"Chiudi",L"Cerrar",L"닫기",L"关闭",L"إغلاق",L"Закрыть",L"Schließen",L"Fechar",L"Sluiten",L"Zamknij",L"Kapat"));
	FillDevices();FillSettings();AudioDevApplyRescanButton(&m_capRefresh);AudioDevApplyRescanButton(&m_monRefresh);AudioDevRegisterNotifyHwnd(m_hWnd);
	if(CCustomControlUtility::BeginDialogToolTip(m_tooltip,this)){m_tooltip.AddTool(&m_cap,LL14(L"録音する入力端末",L"Input device to record",L"Entrée à enregistrer",L"Ingresso da registrare",L"Entrada a grabar",L"녹음할 입력",L"要录制的输入设备",L"جهاز الإدخال للتسجيل",L"Вход для записи",L"Aufnahmeeingang",L"Entrada a gravar",L"Op te nemen invoer",L"Wejście do nagrania",L"Kaydedilecek giriş"));m_tooltip.AddTool(&m_meter,LL14(L"処理後のピークレベル",L"Processed peak level",L"Niveau traité",L"Livello elaborato",L"Nivel procesado",L"처리 후 피크",L"处理后峰值",L"ذروة بعد المعالجة",L"Пик после обработки",L"Verarbeiteter Spitzenpegel",L"Pico processado",L"Verwerkt piekniveau",L"Szczyt po przetwarzaniu",L"İşlenmiş tepe seviyesi"));m_tooltip.AddTool(&m_start,LL14(L"録音を開始または停止",L"Start or stop recording",L"Démarrer ou arrêter",L"Avvia o ferma",L"Iniciar o detener",L"녹음 시작/중지",L"开始或停止录音",L"بدء التسجيل أو إيقافه",L"Начать или остановить",L"Aufnahme starten/stoppen",L"Iniciar ou parar",L"Opname starten/stoppen",L"Rozpocznij lub zatrzymaj",L"Kaydı başlat/durdur"));CCustomControlUtility::FinalizeDialogToolTip(m_tooltip,340,9000);}
	CCC_CaptionLayout(m_hWnd);LayoutHelpBtn();PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);return TRUE;
}
void CDigitizeDlg::OnControlChanged(){if(!m_uiLocked)PersistUi();}
void CDigitizeDlg::OnBrowse()
{
	if(m_uiLocked)return;int f=max(0,min(2,m_fmt.GetCurSel()));CString ext=f==1?L"mp3":f==2?L"flac":L"wav";
	CString filter=f==1?L"MP3 (*.mp3)|*.mp3||":f==2?L"FLAC (*.flac)|*.flac||":L"WAV (*.wav)|*.wav||";CFileDialog d(FALSE,ext,NULL,OFN_OVERWRITEPROMPT,filter,this);if(d.DoModal()==IDOK)m_path.SetWindowText(d.GetPathName());
}
void CDigitizeDlg::SetRecordingUi(BOOL on){m_uiLocked=on;m_start.SetWindowText(on?DigText(3):DigText(2));PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);}
BOOL CDigitizeDlg::StartRecording()
{
	PersistUi();m_outFmt=savedata.dig_format;m_mp3Kbps=savedata.dig_mp3_kbps;m_flacLevel=savedata.dig_flac_level;m_hpfHz=savedata.dig_hpf_hz;m_gainPct=savedata.dig_gain;m_gatePct=savedata.dig_gate;m_finalPath=savedata.dig_last_path;
	if(m_finalPath.IsEmpty()){m_status.SetWindowText(LL14(L"保存先を指定してください。",L"Specify a save path.",L"Indiquez un chemin.",L"Indicare un percorso.",L"Indique una ruta.",L"저장 위치를 지정하세요.",L"请指定保存路径。",L"حدد مسار الحفظ.",L"Укажите путь.",L"Speicherpfad angeben.",L"Indique um caminho.",L"Geef een pad op.",L"Podaj ścieżkę.",L"Kayıt yolu belirtin."));return FALSE;}
	m_wavPath=m_outFmt?DigTempPath():m_finalPath;CFileException e;if(!m_file.Open(m_wavPath,CFile::modeCreate|CFile::modeReadWrite|CFile::shareExclusive,&e)){m_status.SetWindowText(LL14(L"ファイルを開けません。",L"Cannot open file.",L"Fichier inaccessible.",L"Impossibile aprire il file.",L"No se puede abrir el archivo.",L"파일을 열 수 없습니다.",L"无法打开文件。",L"تعذر فتح الملف.",L"Не открыть файл.",L"Datei kann nicht geöffnet werden.",L"Não é possível abrir.",L"Kan bestand niet openen.",L"Nie można otworzyć pliku.",L"Dosya açılamıyor."));return FALSE;}
	DigWriteWavHeader(m_file);InterlockedExchange(&m_stop,0);InterlockedExchange(&m_peak,0);InterlockedExchange(&m_pcmBytes,0);InterlockedExchange(&m_lastHr,S_OK);
	uintptr_t t=_beginthreadex(NULL,0,CaptureThread,this,0,NULL);if(!t){m_file.Close();DeleteFile(m_wavPath);return FALSE;}m_thread=(HANDLE)t;m_startTick=GetTickCount();SetRecordingUi(TRUE);SetTimer(DIG_TIMER,50,NULL);m_status.SetWindowText(DigText(1));return TRUE;
}
void CDigitizeDlg::StopRecording(BOOL encode)
{
	if(m_stopping)return;m_stopping=TRUE;KillTimer(DIG_TIMER);InterlockedExchange(&m_stop,1);if(m_thread){WaitForSingleObject(m_thread,7000);CloseHandle(m_thread);m_thread=NULL;}
	EnterCriticalSection(&m_fileCs);BOOL open=m_file.m_hFile!=CFile::hFileNull;if(open){m_file.Flush();DigFinalizeWavHeader(m_file);m_file.Close();}LeaveCriticalSection(&m_fileCs);SetRecordingUi(FALSE);
	BOOL ok=open&&InterlockedCompareExchange(&m_pcmBytes,0,0)>0;if(ok&&encode&&m_outFmt==1){ok=EncodeWavToMp3(m_wavPath,m_finalPath,m_mp3Kbps);DeleteFile(m_wavPath);}else if(ok&&encode&&m_outFmt==2){ok=EncodeWavToFlac(m_wavPath,m_finalPath,m_flacLevel);DeleteFile(m_wavPath);}else if(!encode&&m_outFmt)DeleteFile(m_wavPath);
	m_status.SetWindowText(ok?LL14(L"保存しました。",L"Saved.",L"Enregistré.",L"Salvato.",L"Guardado.",L"저장했습니다.",L"已保存。",L"تم الحفظ.",L"Сохранено.",L"Gespeichert.",L"Guardado.",L"Opgeslagen.",L"Zapisano.",L"Kaydedildi."):LL14(L"録音または変換に失敗しました。",L"Recording or conversion failed.",L"Échec de l'enregistrement ou conversion.",L"Registrazione o conversione non riuscita.",L"Falló la grabación o conversión.",L"녹음 또는 변환 실패.",L"录音或转换失败。",L"فشل التسجيل أو التحويل.",L"Ошибка записи или конвертации.",L"Aufnahme oder Konvertierung fehlgeschlagen.",L"Falha na gravação ou conversão.",L"Opname of conversie mislukt.",L"Nagranie lub konwersja nieudana.",L"Kayıt veya dönüştürme başarısız."));m_stopping=FALSE;
}
void CDigitizeDlg::OnStart(){if(m_thread)StopRecording(TRUE);else StartRecording();}
void CDigitizeDlg::OnTimer(UINT_PTR id){if(id==DIG_TIMER){LONG p=InterlockedCompareExchange(&m_peak,0,0);InterlockedExchange(&m_peak,p*85/100);m_meter.SetLevel(min(1000,(int)(sqrt((double)p/1000.0)*1100)));DWORD s=(GetTickCount()-m_startTick)/1000;CString t;t.Format(L"%02u:%02u",s/60,s%60);m_time.SetWindowText(t);}CCustomBlurDialogBase::OnTimer(id);}
UINT __stdcall CDigitizeDlg::CaptureThread(void* q)
{
	CDigitizeDlg* s=(CDigitizeDlg*)q;CoInitializeEx(NULL,COINIT_MULTITHREADED);IMMDeviceEnumerator* en=NULL;IMMDevice* in=NULL,*out=NULL;IAudioClient* ac=NULL,*rc=NULL;IAudioCaptureClient* cap=NULL;IAudioRenderClient* ren=NULL;WAVEFORMATEX* fmt=NULL;HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),NULL,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),(void**)&en);
	if(SUCCEEDED(hr))hr=en->GetDevice(savedata.dig_cap_device,&in);if(FAILED(hr)&&en)hr=en->GetDefaultAudioEndpoint(eCapture,eConsole,&in);
	if(SUCCEEDED(hr))hr=in->Activate(__uuidof(IAudioClient),CLSCTX_ALL,NULL,(void**)&ac);if(SUCCEEDED(hr))hr=ac->GetMixFormat(&fmt);if(SUCCEEDED(hr))hr=ac->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_NOPERSIST,2000000,0,fmt,NULL);if(SUCCEEDED(hr))hr=ac->GetService(__uuidof(IAudioCaptureClient),(void**)&cap);
	WAVEFORMATEX rf={WAVE_FORMAT_PCM,2,48000,192000,4,16,0};UINT32 rbuf=0;if(SUCCEEDED(hr)&&savedata.dig_monitor){hr=en->GetDevice(savedata.dig_mon_device,&out);if(FAILED(hr))hr=en->GetDefaultAudioEndpoint(eRender,eConsole,&out);if(SUCCEEDED(hr))hr=out->Activate(__uuidof(IAudioClient),CLSCTX_ALL,NULL,(void**)&rc);if(SUCCEEDED(hr))hr=rc->Initialize(AUDCLNT_SHAREMODE_SHARED,0,2000000,0,&rf,NULL);if(SUCCEEDED(hr))hr=rc->GetBufferSize(&rbuf);if(SUCCEEDED(hr))hr=rc->GetService(__uuidof(IAudioRenderClient),(void**)&ren);}
	if(SUCCEEDED(hr)){hr=ac->Start();if(SUCCEEDED(hr)&&rc)hr=rc->Start();}InterlockedExchange(&s->m_lastHr,hr);if(FAILED(hr))goto done;InterlockedExchange(&s->m_run,1);
	{ float prevIn=0.f,prevOut=0.f,phase=0.f,lastL=0.f,lastR=0.f;const float step=fmt->nSamplesPerSec/48000.f;short pcm[4096*2];
	while(!InterlockedCompareExchange(&s->m_stop,0,0)){UINT32 n=0;hr=cap->GetNextPacketSize(&n);if(FAILED(hr))break;if(!n){Sleep(3);continue;}BYTE* data=NULL;DWORD flags=0;UINT64 pos=0;hr=cap->GetBuffer(&data,&n,&flags,&pos,NULL);if(FAILED(hr))break;int made=0;while(phase<n&&made<4096){int ix=(int)phase;float l,r;if(flags&AUDCLNT_BUFFERFLAGS_SILENT)l=r=0.f;else DigSampleToFloat(data+(SIZE_T)ix*fmt->nBlockAlign,fmt,l,r);float x=(l+r)*.5f,g=s->m_gainPct/100.f,gate=s->m_gatePct/100.f*.12f;x*=g;if(fabsf(x)<gate)x=0.f;if(s->m_hpfHz){float a=expf(-6.2831853f*s->m_hpfHz/48000.f);float y=a*(prevOut+x-prevIn);prevIn=x;prevOut=y;x=y;}x=DigClamp(x);lastL=lastR=x;pcm[made*2]=(short)(x*32767);pcm[made*2+1]=pcm[made*2];LONG pk=(LONG)(fabsf(x)*1000);LONG old=InterlockedCompareExchange(&s->m_peak,0,0);if(pk>old)InterlockedExchange(&s->m_peak,pk);made++;phase+=step;}phase-=n;cap->ReleaseBuffer(n);if(made){EnterCriticalSection(&s->m_fileCs);if(s->m_file.m_hFile!=CFile::hFileNull)s->m_file.Write(pcm,made*4);LeaveCriticalSection(&s->m_fileCs);InterlockedExchangeAdd(&s->m_pcmBytes,made*4);if(ren){UINT32 pad=0;rc->GetCurrentPadding(&pad);UINT32 avail=rbuf-pad;if(avail>(UINT32)made)avail=made;if(avail){BYTE* b=NULL;if(SUCCEEDED(ren->GetBuffer(avail,&b))){memcpy(b,pcm,avail*4);ren->ReleaseBuffer(avail,0);}}}}}}
done: if(ac)ac->Stop();if(rc)rc->Stop();if(ren)ren->Release();if(cap)cap->Release();if(rc)rc->Release();if(ac)ac->Release();if(out)out->Release();if(in)in->Release();if(en)en->Release();if(fmt)CoTaskMemFree(fmt);InterlockedExchange(&s->m_run,0);CoUninitialize();return 0;
}
void CDigitizeDlg::ShowHelpSheet(){if(g_help&&g_help->GetSafeHwnd()){g_help->SetForegroundWindow();return;}g_help=new CDigHelp(this);if(!g_help->Create(IDD_DIG_HELP,this)){delete g_help;g_help=NULL;return;}CCC_PresentOwnedHelp(this,g_help);}
void CDigitizeDlg::OnHelp(){ShowHelpSheet();}void CDigitizeDlg::OnCloseBtn(){DestroyWindow();}void CDigitizeDlg::OnOK(){}void CDigitizeDlg::OnCancel(){DestroyWindow();}
void CDigitizeDlg::OnSize(UINT t,int x,int y){CCustomBlurDialogBase::OnSize(t,x,y);if(GetSafeHwnd()){CCC_CaptionLayout(m_hWnd);LayoutHelpBtn();}}
void CDigitizeDlg::OnDestroy(){AudioDevUnregisterNotifyHwnd(m_hWnd);if(m_thread)StopRecording(FALSE);PersistUi();CCustomBlurDialogBase::OnDestroy();}
void CDigitizeDlg::OnMicDevRefresh(){AudioDevRebuildAll();FillDevices();}
LRESULT CDigitizeDlg::OnAudioDevChanged(WPARAM,LPARAM){FillDevices();return 0;}
void OpenDigitizeModeless(CWnd* p){if(g_digitize&&g_digitize->GetSafeHwnd()){g_digitize->SetForegroundWindow();return;}g_digitize=new CDigitizeDlg(p);if(!g_digitize->Create(IDD_DIGITIZE,p)){delete g_digitize;g_digitize=NULL;return;}g_digitize->ShowWindow(SW_SHOW);}
void CloseDigitizeIfOpen(){if(g_digitize&&g_digitize->GetSafeHwnd())g_digitize->DestroyWindow();}
