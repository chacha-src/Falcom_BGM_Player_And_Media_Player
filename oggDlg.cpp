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
int wavbit;
int wavsam = 16;
#include "Id3tagv1.h"
#include "Id3tagv2.h"
#include "mp3.h"
#include "OSVersion.h"
#include "codec/neaacdec.h"
#include "m4a.h"
#include "flac.h"
#include "dsd\dsd.h"
#include "opus.h"
#include <intrin.h>

#include <vector>
#include <algorithm>
#include "LyricsProgressWnd.h"

bool ProcessAudioWithRubberBand(float tempoRate);
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


CImageBase* Games;

#pragma warning(push)
#pragma warning(disable : 4201)
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#pragma warning(pop)
#include <endpointvolume.h>
#include <FunctionDiscoveryKeys_devpkey.h>

static mp3 mp3_;
static m4a m4a_;
static flac flac_;
dsd dsd_;
opus opus_;

int readadpcm(CFile& adpcmf, char* bw, int len);
int readadpcmzwei(CFile& adpcmf, char* bw, int len);
int readadpcmgurumin(CFile& adpcmf, char* bw, int len);
int readadpcmarc(CFile& adpcmf, char* bw, int len);
BOOL wavwait, thend;
int wavch = 2;
int muon;
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
int playwavdsd(BYTE* bw, int old, int l1, int l2);
int readdsd(BYTE* bw, int cnt);
int playwavm4a(BYTE* bw, int old, int l1, int l2);
int readm4a(BYTE* bw, int cnt);
int playwavopus(BYTE* bw, int old, int l1, int l2);
int readopus(BYTE* bw, int cnt);
int playwavmp3(BYTE* bw, int old, int l1, int l2);
int readmp3(BYTE* bw, int cnt);
void playwavds2(BYTE* bw, int old, int l1, int l2);
BOOL playwavadpcm(BYTE* bw, int old, int l1, int l2);
//スレッド
UINT wavread(LPVOID);
extern BYTE bufimage[0x30000f];

CString extn;
int ogpl0 = 0;
/////////////////////////////////////////////////////////////////////////////
// アプリケーションのバージョン情報で使われている CAboutDlg ダイアログ
extern void DoEvent();
#include "CCustomControl.h"
class CAboutDlg : public CCustomDialog
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
	CCustomStandardButton m_okdummy;
};

CAboutDlg::CAboutDlg(CWnd* pParent) : CCustomDialog(CAboutDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDC_STATICin, m_in);
	DDX_Control(pDX, IDC_Link, m_link);
	DDX_Control(pDX, IDC_STATICin2, m_cpu);
	DDX_Control(pDX, IDOK, m_okdummy);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CCustomDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
	// メッセージ ハンドラがありません。
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()
double aa1_ = 0;
BOOL CAboutDlg::OnInitDialog()
{
	CCustomDialog::OnInitDialog();

	// TODO:  ここに初期化を追加してください
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

	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

/////////////////////////////////////////////////////////////////////////////
// COggDlg ダイアログ

COggDlg::COggDlg(CWnd* pParent /*=NULL*/)
	: CCustomDialog(COggDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(COggDlg)
	//}}AFX_DATA_INIT
	// メモ: LoadIcon は Win32 の DestroyIcon のサブシーケンスを要求しません。
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

}

void COggDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
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

BEGIN_MESSAGE_MAP(COggDlg, CCustomDialog)
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
	ON_WM_COPYDATA()
	ON_WM_KEYDOWN()
	ON_WM_SYSKEYDOWN()
	ON_WM_ACTIVATE()
	ON_MESSAGE(WM_HOTKEY, OnHotKey)
	ON_WM_KILLFOCUS()
	ON_WM_SIZE()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_BUTTON57, &COggDlg::OnPlayList)
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
int voldsf;
int timingf, timerf1;
int uTimerId;
void CALLBACK TimeCallback(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);
COggDlg* og;
static char* adbuf, * adbuf2;
BOOL thend1;
BOOL videoonly;
STARTUPINFO si;
PROCESS_INFORMATION pi;
int spc;
int killw1 = 0, ttt_;
CString ext[150][300];
BYTE kvar[150][300];
IKpiDecoderModule* v5mo;
CString kpif[400];
TCHAR kpifs[200][64];
BOOL kpichk[200];
int kpicnt;

/////////////////////////////////////////////////////////////////////////////
// COggDlg メッセージ ハンドラ
extern CString ndd;
ITaskbarList3* ptl;
ICustomDestinationList* pcdl;
IObjectCollection* poc;

int plcnt = 0;





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
	return CCustomDialog::OnCommand(wParam, lParam);
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
		CCustomDialog::OnSysCommand(nID, lParam);
	}
}

#define MDC (88*2+170-8*5)*4
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
	if (s == "▼") {
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
BYTE kvver;
IKpiDecoderModule* ob5 = NULL;
const KPI_DECODER_MODULEINFO* m_ModuleInfo5;
const KPI_MEDIAINFO* pMediaInfo = NULL;
KPI_MEDIAINFO me5;
IKpiDecoder* kpidec = NULL;

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
public:
	CMyDummyConfig() : m_cRef(1) {}
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
	virtual void   WINAPI SetInt(const wchar_t*, const wchar_t*, INT64) {}
	virtual INT64  WINAPI GetInt(const wchar_t*, const wchar_t*, INT64 nDefault) { return nDefault; }
	virtual void   WINAPI SetFloat(const wchar_t*, const wchar_t*, double) {}
	virtual double WINAPI GetFloat(const wchar_t*, const wchar_t*, double dDefault) { return dDefault; }
	virtual void   WINAPI SetStr(const wchar_t*, const wchar_t*, const wchar_t*) {}
	virtual DWORD  WINAPI GetStr(const wchar_t*, const wchar_t*, wchar_t* pszValue, DWORD dwSize, const wchar_t* cszDefault) {
		if (cszDefault) wcsncpy_s(pszValue, dwSize / sizeof(wchar_t), cszDefault, _TRUNCATE);
		else *pszValue = L'\0';
		return (DWORD)(wcslen(pszValue) + 1) * sizeof(wchar_t);
	}
	virtual void   WINAPI SetBin(const wchar_t*, const wchar_t*, const BYTE*, DWORD) {}
	virtual DWORD  WINAPI GetBin(const wchar_t*, const wchar_t*, BYTE*, DWORD dwSize) { return 0; }
};

// --- 2. ホストクラスの実装 (IKpiUnkProvider を実装) ---
// (kpi_CreateInstance の第3引数に渡すオブジェクト)
class CMyHost : public IKpiUnkProvider
{
private:
	long m_cRef;

public:
	CMyHost() : m_cRef(1) {}
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
			*ppvObj = new CMyDummyConfig();

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
int cc1, wl, t, oggsize, dd, loop1, loop2, loop1_2;//,oggsize1,oggsize2;
__int64 playb;
int ru2 = 0, ru;
int lo, loc, endf, ps = 0, locs;
int poss = 0, poss2 = 0, poss3 = 0, poss4 = 0, poss5 = 0, poss6 = 0, loopcnt, pl_no;
int current_section;
long whsize;
int ret2;

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
BYTE bufwav3[OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 6];


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
int playy = 1;
void st1();
void st2();
int bufzero = 0;
extern 	int syukai;

int horizontalDPI;
int ms2;
int endflg = 0;

int tempo;
int pitch;

int stflg = TRUE;
int eqflg = TRUE;
//////////////////////////////////////////////////////////////////////////////
BOOL COggDlg::OnInitDialog()
{
	CCustomDialog::OnInitDialog();
	ms2 = 0;
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
	wavbit = 44100; wavbit2 = 44100; wavch = 2;
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

	// TODO: 特別な初期化を行う時はこの場所に追加してください。
	//フォント設定
	LOGFONT LogFont;
	CClientDC dc1(this);
	dc1.GetCurrentFont()->GetLogFont(&LogFont);
	_tcscpy(LogFont.lfFaceName, _T("ＭＳ ゴシック"));
	LogFont.lfHeight = 16 * 4;
	LogFont.lfWidth = 8 * 4;
	LogFont.lfQuality = DRAFT_QUALITY;
	LogFont.lfWeight = FW_ULTRABOLD;
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
	SetTimer(1233, 17, NULL);
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
	{
		// ツールヒント コントロールを作成します
		m_tooltip.Create(this);
		m_tooltip.Activate(TRUE);

		// TODO: コントロールを追加するために以下のフォームの 1 つを使用してください:
		// m_tooltip.AddTool(GetDlgItem(IDC_<name>), <string-table-id>);
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON1), _T("演奏中のogg/wav/mp3/avi/kpiファイルを停止します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON2), _T("曲一覧表からoggを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON6), _T("曲一覧表からoggを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON7), _T("曲一覧表からoggを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON8), _T("曲一覧表からoggを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON15), _T("曲一覧表からoggを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON17), _T("曲一覧表からoggを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON19), _T("曲一覧表からoggを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON25), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON23), _T("曲一覧表からoggを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON24), _T("曲一覧表からoggを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON27), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON28), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON31), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON35), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON33), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON37), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON39), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON44), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON45), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON46), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON47), _T("曲一覧表からmp3を選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON48), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON51), _T("曲一覧表からwav(adp)を選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON53), _T("曲一覧表からwavを選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON54), _T("曲一覧表からmp3を選択し再生します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON57), _T("プレイリストを表示／非表示します。\n表示されている時に演奏を開始またはドロップで演奏するとリストに追加されます。\n非表示の時はリストには追加されません。\n本体へのドロップは1つだけでしたがプレイリストへのドロップは複数出来ます。"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON59), _T("イコライザーを設定します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON3), _T("演奏中のogg/wav/mp3/avi/kpiファイルを一時停止/再開します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON4), _T("演奏中だった曲を頭から再演奏します"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON5), _T("フェードアウトして停止します。(内蔵デコーダのみ)"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON9), _T("各ゲームのフォルダ位置を指定します"));
		m_tooltip.AddTool(GetDlgItem(IDOK), _T("簡易プレイヤを終了します"));
		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER1), _T("音量を変更します\nWindows全体の音量が関係してきます。"));
		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER3), _T("DirectSound音量を変更します\nこの簡易プレイヤのみの変更でとどまります。\nWindowsの音量は変化しません。"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK1), _T("スペクトルアナライザー(波形)を表示/非表示します"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK2), _T("演奏中の曲をwavで保存します"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK3), _T("動画(OPやイベント)のoggの時に動画画面も表示します"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK4), _T("スペクトルアナライザー(波形)をモノラル表示、ステレオ表示切り替えを行います"));

		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON12), _T("拡張パネルを開く/閉じる"));

		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON21), _T("各種設定を行います。"));

		m_tooltip.AddTool(GetDlgItem(IDC_CHECK5), _T("「再生するゲーム」で選択されているゲームをランダムに演奏します"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK6), _T("「再生するゲーム」で選択されているゲームを順番に演奏します"));

		m_tooltip.AddTool(GetDlgItem(IDC_CHECK7), _T("イース6 ナピシュテムの匣"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK8), _T("イース フェルガナの誓い"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK9), _T("空の軌跡 First Chapter"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK10), _T("空の軌跡 Second Chapter"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK11), _T("イース オリジン"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK12), _T("空の軌跡 The 3rd"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK13), _T("Zweii II"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK14), _T("YS I&&II Chronicles Ys 1"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK15), _T("YS I&&II Chronicles Ys 2"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK16), _T("XANADU NEXT"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK17), _T("Ys 完全版 Ys 1"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK18), _T("Ys 完全版 Ys 2"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK19), _T("Sorcerian Original"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK20), _T("Zwei!!"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK21), _T("ぐるみん"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK22), _T("ダイナソア リザレクション"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK23), _T("Brandish4 - ブランディッシュ4 眠れる神の塔"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK24), _T("英雄伝説III 白き魔女"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK25), _T("英雄伝説IV 朱紅い雫"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK26), _T("英雄伝説V 海の檻歌"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON58), _T("mp3/m4a/ogg/flacに埋め込まれているジャケットを表示します。"));

		m_tooltip.AddTool(GetDlgItem(IDC_EDIT1), _T("次の曲へいくためのループ回数を設定します"));

		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER4), _T("100%以上の音量を設定できます"));
		m_tooltip.AddTool(GetDlgItem(IDC_STATICds2), _T("100%以上の音量を設定できます"));
		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER7), _T("mp3,ogg,flac,m4a,opus,adpcm,dsd,kpiのテンポを変えます。wav,動画は変わりません。"));
		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER8), _T("mp3,ogg,flac,m4a,opus,adpcm,dsd,kpiのピッチを変えます。wav,動画は変わりません。"));

		m_tooltip.AddTool(GetDlgItem(IDC_STATIC_t), _T("テンポを100に戻します。"));
		m_tooltip.AddTool(GetDlgItem(IDC_STATIC_p), _T("ピッチを100に戻します。"));

		CString s;
		s.Format(_T("%s\n↑の情報が間違っている時は↓の内容を作者へ\n詳細：Ver %d.%d(%d) Build %d\n\n%s"), os.GetVersionString(), in.dwMajorVersion, in.dwMinorVersion, edition, in.dwBuildNumber, cpus);
		m_tooltip.AddTool(GetDlgItem(IDC_STATIC_OS), s);
		m_tooltip.AddTool(GetDlgItem(IDC_STATIC_OS2), s);
	}
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);
	// FFT三角関数テーブルの初期化
	ipTab2[0] = 0;
	int i;
	// 窓関数テーブルの初期化
	for (i = 0; i < 15360; i++) {
		//fnWFilter[i/2] = (1-cos(2*M_PI*(i/2)/BUFSZ))/2; // Hanning窓
		fnWFilter[i] = 0.54f - 0.46f * (float)cos((2.0f * M_PI * i) / (i - 1));//sin(M_PI*(i + 0.5) / 4096); // sin窓
		//fnWFilter[i] = sin(syuha[i/47]/1000.0);
	}


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
		UINT cMinSlots;
		IObjectArray* poaRemoved;
		pcdl->BeginList(&cMinSlots, IID_PPV_ARGS(&poaRemoved));
		CoCreateInstance(CLSID_EnumerableObjectCollection, NULL, CLSCTX_INPROC, IID_PPV_ARGS(&poc));
		IShellLink* psl = NULL;
		_CreateShellLink(_T("*1"), _T("再演奏"), &psl, 0, true);
		poc->AddObject(psl);	psl->Release();
		_CreateShellLink(_T("*2"), _T("一時停止"), &psl, 0, true);
		poc->AddObject(psl);	psl->Release();
		_CreateShellLink(_T("*3"), _T("停止"), &psl, 0, true);
		poc->AddObject(psl);	psl->Release();
		_CreateShellLink(_T(""), _T(""), &psl, 0, true, FALSE);
		poc->AddObject(psl);	psl->Release();
		_CreateShellLink(_T("*4"), _T("プレイリスト開閉"), &psl, 0, true);
		poc->AddObject(psl);	psl->Release();
		_CreateShellLink(_T(""), _T(""), &psl, 0, true, FALSE);
		poc->AddObject(psl);	psl->Release();
		_CreateShellLink(_T("*5"), _T("レンダリング設定"), &psl, 0, true);
		poc->AddObject(psl);	psl->Release();
		_CreateShellLink(_T("*6"), _T("フォルダ設定"), &psl, 0, true);
		poc->AddObject(psl);	psl->Release();
		IObjectArray* poa; poc->QueryInterface(IID_PPV_ARGS(&poa));
		pcdl->AddUserTasks(poa); poa->Release();
		pcdl->CommitList(); poaRemoved->Release();
		poc->Release();
	}

	m_pDlgColor = NULL;



	//Windows7 / Vista用 ボリュームチェンジ
	deve = NULL; dev = NULL; audio = NULL;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&deve))) {
		deve->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
		dev->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, NULL, (void**)&audio);
		float lv;
		audio->GetMasterVolumeLevelScalar(&lv);
		m_sl.SetRange(0, 100000);
		m_sl.SetPos((int)((float)lv * 100000.0f));
	}
	else {
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
	plug(karento2, NULL);
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
	avx2 = "SSE2未対応";
	if (CPUInfo[0] >= 2) {
		__cpuid(CPUInfo, 0x00000001);
		if (CPUInfo[1] & (1 << 26))  avx2 = "SSE2対応";
		if (CPUInfo[2] & (1))  avx2 = "SSE3対応";
		if (CPUInfo[2] & (1 << 9))  avx2 = "SSSE3対応";
		if (CPUInfo[2] & (1 << 12))  avx2 = "FMA3対応";
		if (CPUInfo[2] & (1 << 19))  avx2 = "SSE4.1対応";
		if (CPUInfo[2] & (1 << 20))  avx2 = "SSE4.2対応";
	}
	if (CPUInfo[0] >= 2) {
		__cpuid(CPUInfo, 0x80000001);
		if (CPUInfo[2] & (1 << 6))  avx2 = "SSE4a対応";
	}
	__cpuid(CPUInfo, 0x00000001);
	if (CPUInfo[0] >= 2) {
		if (CPUInfo[2] & (1 << 28))  avx2 = "AVX対応";
	}
	if (CPUInfo[0] >= 7) {
		__cpuid(CPUInfo, 0x00000007);
		if (CPUInfo[1] & (1 << 5))  avx2 = "AVX2対応";
		if (CPUInfo[1] & (1 << 16))  avx2 = "AVX512対応";
	}
	s.Format(_T("%s / %s"), cpus, avx2);
	s.Trim();
	m_cpu.SetWindowText(s);

	// CPU 拡張命令一覧
	avx2 = L"使用可能命令：";
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
	s.Format(_T("%3d"), savedata.kakuVal);
	m_kakuVolval.SetWindowText(s);
	m_kakuVol.SetPos(savedata.kakuVol);
	m_kakuVol.SetRange(100, 900);
	s.Format(_T("%3d"), savedata.kakuVal);
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
	m_lrc2.SetWindowText(L"歌詞(.lrc)が表示されます");
	m_lrc3.SetWindowText(L"");
	lrc_backup = L"";

	stflg = TRUE;

	SetTimer(59877, 500, NULL); // eq

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


		if (savedata.ms2 <= ms2) {
			SetStretchBltMode(dcc.m_hDC, COLORONCOLOR); //高画質モード
			SetBrushOrgEx(dcc.m_hDC, 0, 0, NULL); //ブラシのずれを防止
			dcc.StretchBlt(0, 0, (int)((MDCP)*hD), (int)((81 + 16) * hD * 4), &dc, 0, 0, MDCP + 5, (81 + 16) * 4, SRCCOPY);
			ms2 = 0;
		}
		///CCustomDialog::OnPaint();
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
	data_size = (long)ceil(vi->channels * vi->rate * ov_time_total(&vf, -1) * word);
	og->m_time.SetRange(0, (data_size) / 4, TRUE);
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
	wavch = vi->channels;
	wavbit = vi->rate;
	wh.WaveFmt.wf.nSamplesPerSec = vi->rate;
	wh.WaveFmt.wf.nAvgBytesPerSec = vi->rate * vi->channels * word;
	wh.WaveFmt.wf.nBlockAlign = vi->channels * word;
	wh.WaveFmt.wBitsPerSample = word * 8;

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
	wh.WaveFmt.wf.nChannels = wavch;
	wh.WaveFmt.wf.nSamplesPerSec = wavbit;
	wh.WaveFmt.wBitsPerSample = wavsam;
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
		}
		else {
		}
	}
	wavbit = sikpi.dwSamplesPerSec;	wavch = sikpi.dwChannels;	loop1 = 0; oggsize = loop2 = (int)((double)sikpi.dwLength * (double)sikpi.dwSamplesPerSec / 1000.0 / (wavsam / 16.0));
	wavsam = sikpi.dwBitsPerSample;
	CString s; s.Format(L"%d", oggsize);
	//AfxMessageBox(s);
	si1.dwSamplesPerSec = wavbit;
	si1.dwChannels = wavch;
	si1.dwBitsPerSample = wavsam;
	if (sikpi.dwLength == (DWORD)-1) loop2 = 0;
	data_size = oggsize = loop2 * (wavsam / 4);
	if (wavch == 1) oggsize /= 2;
	if (wavch == 1) data_size /= 2;
	m_time.SetRange(0, (data_size) / (wavsam / 4), TRUE);
	dsd_.kpiSetPosition(kmp, 0);
	kbps = 0;
	wav_start();
	CFile ff;
	ff.Open(filen, CFile::modeRead | CFile::shareDenyNone, NULL);
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
	{ CFile f; if (f.Open(_T("wav.dat"), CFile::modeRead, NULL) != TRUE) { og->d_z1.EnableWindow(FALSE); og->m_zwei.EnableWindow(FALSE); } else f.Close(); }
	ret = _tchdir(savedata.gurumin);
	ret += _chdir("bgm");
	if (ret != 0) { og->d_guru.EnableWindow(FALSE); og->m_gurumin.EnableWindow(FALSE); }
	else { og->d_guru.EnableWindow(TRUE); og->m_gurumin.EnableWindow(TRUE); }
	og->d_dino.EnableWindow(TRUE); og->m_dino.EnableWindow(TRUE);
	ret = _tchdir(savedata.dino);
	{ CFile f; if (f.Open(_T("bgm.arc"), CFile::modeRead, NULL) != TRUE) { og->d_dino.EnableWindow(FALSE); og->m_dino.EnableWindow(FALSE); } else f.Close(); }
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
	{ CFile f; if (f.Open(_T("music.pak"), CFile::modeRead, NULL) != TRUE) og->d_arc.EnableWindow(FALSE); else f.Close(); }
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
#define MUON 180
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
	if (file.Open(path, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite))
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
		{ L"不守", L"不安" }, { L"貓", L"猫" }, { L"聲", L"声" }, { L"麼", L"ま" },
		{ L"亞", L"亜" }, { L"惡", L"悪" }, { L"壓", L"圧" }, { L"圍", L"囲" },
		{ L"醫", L"医" }, { L"為", L"為" }, { L"爲", L"為" }, { L"壹", L"壱" },
		{ L"逸", L"逸" }, { L"隱", L"隠" }, { L"營", L"営" }, { L"榮", L"栄" },
		{ L"衛", L"衛" }, { L"衞", L"衛" }, { L"驛", L"駅" }, { L"圓", L"円" },
		{ L"鹽", L"塩" }, { L"奧", L"奥" }, { L"應", L"応" }, { L"歐", L"欧" },
		{ L"毆", L"殴" }, { L"穩", L"穏" }, { L"憶", L"憶" }, { L"橫", L"横" },
		{ L"溫", L"温" }, { L"假", L"仮" }, { L"價", L"価" }, { L"畫", L"画" },
		{ L"會", L"会" }, { L"繪", L"絵" }, { L"壞", L"壊" }, { L"懷", L"懐" },
		{ L"擴", L"拡" }, { L"殼", L"殻" }, { L"覺", L"覚" }, { L"學", L"学" },
		{ L"嶽", L"岳" }, { L"樂", L"楽" }, { L"勸", L"勧" }, { L"卷", L"巻" },
		{ L"寬", L"寛" }, { L"歡", L"歓" }, { L"罐", L"缶" }, { L"觀", L"観" },
		{ L"關", L"関" }, { L"陷", L"陥" }, { L"巖", L"巌" }, { L"顏", L"顔" },
		{ L"歸", L"帰" }, { L"氣", L"気" }, { L"龜", L"亀" }, { L"僞", L"偽" },
		{ L"戲", L"戯" }, { L"犧", L"犠" }, { L"舊", L"旧" }, { L"據", L"拠" },
		{ L"舉", L"挙" }, { L"虛", L"虚" }, { L"峽", L"峡" }, { L"挾", L"挟" },
		{ L"狹", L"狭" }, { L"曉", L"暁" }, { L"區", L"区" }, { L"驅", L"駆" },
		{ L"勳", L"勲" }, { L"薰", L"薫" }, { L"羣", L"群" }, { L"徑", L"径" },
		{ L"惠", L"恵" }, { L"揭", L"掲" }, { L"攜", L"携" }, { L"溪", L"渓" },
		{ L"經", L"経" }, { L"繼", L"継" }, { L"莖", L"茎" }, { L"螢", L"蛍" },
		{ L"輕", L"軽" }, { L"鷄", L"鶏" }, { L"藝", L"芸" }, { L"擊", L"撃" },
		{ L"縣", L"県" }, { L"儉", L"倹" }, { L"劍", L"剣" }, { L"圈", L"圏" },
		{ L"檢", L"検" }, { L"權", L"権" }, { L"獻", L"献" }, { L"嚴", L"厳" },
		{ L"吳", L"呉" }, { L"娛", L"娯" }, { L"效", L"効" }, { L"廣", L"広" },
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
		{ L"插", L"挿" }, { L"爭", L"争" }, { L"瘦", L"痩" }, { L"騷", L"騒" },
		{ L"增", L"増" }, { L"藏", L"蔵" }, { L"臟", L"臓" }, { L"屬", L"属" },
		{ L"續", L"続" }, { L"墮", L"堕" }, { L"體", L"体" }, { L"對", L"対" },
		{ L"帶", L"帯" }, { L"滯", L"滞" }, { L"臺", L"台" }, { L"瀧", L"滝" },
		{ L"擇", L"択" }, { L"澤", L"沢" }, { L"單", L"単" }, { L"擔", L"担" },
		{ L"膽", L"胆" }, { L"團", L"団" }, { L"彈", L"弾" }, { L"遲", L"遅" },
		{ L"癡", L"痴" }, { L"蟲", L"虫" }, { L"晝", L"昼" }, { L"鑄", L"鋳" },
		{ L"著", L"着" }, { L"廳", L"庁" }, { L"徵", L"徴" }, { L"聽", L"聴" },
		{ L"敕", L"勅" }, { L"鎭", L"鎮" }, { L"墜", L"墜" }, { L"鐵", L"鉄" },
		{ L"點", L"点" }, { L"傳", L"伝" }, { L"黨", L"党" }, { L"盜", L"盗" },
		{ L"燈", L"灯" }, { L"當", L"当" }, { L"鬪", L"闘" }, { L"德", L"徳" },
		{ L"獨", L"独" }, { L"讀", L"読" }, { L"屆", L"届" }, { L"繩", L"縄" },
		{ L"難", L"難" }, { L"貳", L"弐" }, { L"惱", L"悩" }, { L"腦", L"脳" },
		{ L"霸", L"覇" }, { L"廢", L"廃" }, { L"拜", L"拝" }, { L"賣", L"売" },
		{ L"麥", L"麦" }, { L"發", L"発" }, { L"髮", L"髪" }, { L"拔", L"抜" },
		{ L"蠻", L"蛮" }, { L"祕", L"秘" }, { L"彥", L"彦" }, { L"姬", L"姫" },
		{ L"濱", L"浜" }, { L"拂", L"払" }, { L"佛", L"仏" }, { L"變", L"変" },
		{ L"邊", L"辺" }, { L"辨", L"弁" }, { L"瓣", L"弁" }, { L"辯", L"弁" },
		{ L"舖", L"舗" }, { L"步", L"歩" }, { L"穗", L"穂" }, { L"寶", L"宝" },
		{ L"豐", L"豊" }, { L"沒", L"没" }, { L"飜", L"翻" }, { L"每", L"毎" },
		{ L"萬", L"万" }, { L"滿", L"満" }, { L"默", L"黙" }, { L"彌", L"弥" },
		{ L"藥", L"薬" }, { L"譯", L"訳" }, { L"豫", L"予" }, { L"餘", L"余" },
		{ L"與", L"与" }, { L"譽", L"誉" }, { L"搖", L"揺" }, { L"樣", L"様" },
		{ L"遙", L"遥" }, { L"瑤", L"瑶" }, { L"謠", L"謡" }, { L"來", L"来" },
		{ L"賴", L"頼" }, { L"亂", L"乱" }, { L"覽", L"覧" }, { L"裡", L"里" },
		{ L"龍", L"竜" }, { L"兩", L"両" }, { L"獵", L"猟" }, { L"綠", L"緑" },
		{ L"淚", L"涙" }, { L"壘", L"塁" }, { L"勵", L"励" }, { L"禮", L"礼" },
		{ L"隸", L"隷" }, { L"靈", L"霊" }, { L"齡", L"齢" }, { L"曆", L"暦" },
		{ L"歷", L"歴" }, { L"戀", L"恋" }, { L"鍊", L"錬" }, { L"爐", L"炉" },
		{ L"勞", L"労" }, { L"樓", L"楼" }, { L"錄", L"録" }, { L"灣", L"湾" },
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

	// ～以降削除
	int tildePos = withoutPart.Find(_T("～"));
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
		L"作词", L"作詞", L"作曲", L"編曲", L"编曲",
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
	if (file.Open(lrcPath, CFile::modeCreate | CFile::modeWrite)) {
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


void COggDlg::play()
{
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
	rrr = 1;
	m_ps.EnableWindow(TRUE);
	CWaitCursor rrr;
	m_mp3jake.EnableWindow(FALSE);
	mp3file = filen;
	sflg = FALSE;
	tagname = tagfile = tagalbum = "";
	//	m_rund.EnableWindow(FALSE);
	m_saisai.EnableWindow(FALSE);
	WAVDALen = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM; WAVDAStartLen = OUTPUT_BUFFER_SIZE;
	si1.dwSamplesPerSec = 0; sikpi.dwSamplesPerSec = 0;
	//LOOPSTART=
	//LOOPLENGTH=
	CString s, b;
	playf = 1;
	loopcnt = 0;
	CString fl;
	wavbit = 44100;
	loop3 = 0; fade1 = 0;
	playy = 0;
	cnt3 = 0;
	bufzero = 0;
	if (mi) {
		killw1 = 0;
		mi->DestroyWindow();
		for (; killw1 == 0;)
			DoEvent();
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
	stop1();
	eqflg = FALSE;

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
			wavbit = 22050;
		}
		else wavbit = 44100;
		{ CFile f; if (f.Open(filen, CFile::modeRead, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 12:
		ret = _tchdir(savedata.ys122);
		if (_chdir("wave\\wave_44") == -1) {
			if (_chdir("wave\\wave_22") == -1) { ret = -1; break; }
			wavbit = 22050;
		}
		else wavbit = 44100;
		{ CFile f; if (f.Open(filen, CFile::modeRead, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 13:
		ret = _tchdir(savedata.sor);
		if (_chdir("WAVE\\WAVE44") == -1) {
			if (_chdir("WAVE\\WAVE22") == -1) { ret = -1; break; }
			wavbit = 22050;
		}
		else wavbit = 44100;
		{ CFile f; if (f.Open(filen, CFile::modeRead, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 14:
		ret = _tchdir(savedata.zwei);
		{ CFile f; if (f.Open(_T("wav.dat"), CFile::modeRead, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 15:
		ret = _tchdir(savedata.gurumin);
		ret += _chdir("bgm");
		break;
	case 16:
		ret = _tchdir(savedata.dino);
		{ CFile f; if (f.Open(_T("bgm.arc"), CFile::modeRead, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 17:
		ret = _tchdir(savedata.br4);
		wavbit = 22050;
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
		{ CFile f; if (f.Open(_T("music.pak"), CFile::modeRead, NULL) != TRUE) ret = -1; else f.Close(); }
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
			_chdir("..\\Image"); CFile f; if (ret2 != 47) f.Open(_T("Stage.BKS"), CFile::modeRead, NULL); else f.Open(_T("Stage.BKS4"), CFile::modeRead, NULL);
			_getcwd(kare, 255);	f.Seek(0x2c, CFile::begin);
			int len, st, j;	for (j = 0; j < i; j++) { f.Read(&st, 4);	f.Read(&len, 4); }
			f.Seek(st, CFile::begin); CString a; a.Format(_T("FS%2d.bik"), ret2);
			CFile ff; if (ff.Open(a, CFile::modeRead, NULL) == 0) {
				ff.Open(a, CFile::modeCreate | CFile::modeWrite, NULL);
				char* bu; bu = (char*)malloc(len);	f.Read(bu, len);	ff.Write(bu, len);	free(bu);
			}
			f.Close();	ff.Close();
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
			_chdir("..\\Image"); CFile f; if (ret2 == 50) f.Open(_T("FS2_STAGE.BKS"), CFile::modeRead, NULL); else f.Open(_T("FS2_STAGE_2.BKS"), CFile::modeRead, NULL);
			_getcwd(kare, 255);	f.Seek(0x2c, CFile::begin);
			int len, st = 0, j;	if (i != 0) for (j = 0; j < i; j++) { f.Read(&st, 4);	f.Read(&len, 4); }
			f.Seek(st, CFile::begin); CString a; a.Format(_T("FS2%2d.bik"), ret2);
			CFile ff; if (i != 0) if (ff.Open(a, CFile::modeRead, NULL) == 0) {
				ff.Open(a, CFile::modeCreate | CFile::modeWrite, NULL);
				char* bu; bu = (char*)malloc(len);	f.Read(bu, len);	ff.Write(bu, len);	free(bu);
				ff.Close();
			}
			else ff.Close();
			f.Close();
			if (m_dou.GetCheck() == 1) {
				plf = 1;
				dougaplay(ret2);
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
				if (pGraphBuilder)pMainFrame1->plays2();
				if (pMediaControl)pMediaControl->Run();
				m_saisai.EnableWindow(TRUE); playy = 1;
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
										if (pGraphBuilder)pMainFrame1->plays2();
										if (pMediaControl)pMediaControl->Run();
										m_saisai.EnableWindow(TRUE); playy = 1;
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
			if (pGraphBuilder)pMainFrame1->plays2();
			if (pMediaControl)pMediaControl->Run();
			m_saisai.EnableWindow(TRUE); playy = 1;
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
		fnn = "ファイル又はフォルダがありません"; filen = "";
		m_saisai.EnableWindow(TRUE); endflg = 0; return;
	}
	wl = 0;

	CFile aaa_1;
	int cor = filen.Find(L":", 6); // c:\\ :は2文字 //\/c: :は5文字目 6文字目からスタート
	ss = filen;
	if (cor != -1) {
		ss = filen.Left(filen.Find(L":", 6));
	}
	if (!aaa_1.Open(ss, CFile::modeRead && CFile::shareDenyNone) && !(mode > 0 && mode <= 21 || mode == -6 || mode == -11 || mode == -12 || mode == -13 || mode == -14 || mode == -15 || mode == 30)) {
		MessageBox(L"ファイルが開けませんでした。\n削除されたか移動した可能性があります。");
		stop1();
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
	//	if(f.Open(filen,CFile::modeRead,NULL)!=TRUE)
	//		return;
	//	f.Close();
	wavch = 2;
	wavsam = 16;
	ZeroMemory(bufwav3, sizeof(bufwav3));
	if (((mode >= 10 && mode <= 21) || mode <= -10) && mode != -10 || mode == -6 || mode == 30) {
		thend1 = FALSE;
		wavwait = 0;
		if (mode == 30) wavbit = 48000;
		thend = 0;
		wav_start();
		//		m_thread1 = ::AfxBeginThread((AFX_THREADPROC)wavread, (LPVOID)NULL,THREAD_PRIORITY_ABOVE_NORMAL,0,0,0);
		//		::SetPriorityClass(m_thread1, HIGH_PRIORITY_CLASS);
		//CRuntimeClass *pRuntime = RUNTIME_CLASS(CWread);
		CWread* g_pThread;// = (CWread*)pRuntime->CreateObject();
		//g_pThread->CreateThread(0, 0, NULL);
		g_pThread = (CWread*)AfxBeginThread(RUNTIME_CLASS(CWread), THREAD_PRIORITY_ABOVE_NORMAL, NULL, 0, NULL);
		::SetPriorityClass(g_pThread, HIGH_PRIORITY_CLASS);
		g_pThread->PostThreadMessage(WM_APP + 100, NULL, NULL);
		for (int k = 0; k < 100; k++)
			DoEvent();
	}
	else if (mode == -3 || mode == -10 || mode == -9 || mode == -8 || mode == -7 || mode == -6 || mode == 30) {

	}
	else {
		if (mode != -6) {
			oggsize = LoadOggVorbis(filen, 2, &ogg, m_time);
			if (oggsize < 0) {
				m_saisai.EnableWindow(TRUE);
				fnn = "ファイル又はフォルダがありません";
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
	//ファイル保存用
	cc1 = 0;
	playb = 0;
	m_time.SetPos((int)playb);
	ov_pcm_seek(&vf, (ogg_int64_t)0); poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
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

	//ys8用
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
		stitle = "バトル#58";
	}
	if (ss == "yc_b002.ogg") {
		loop1 = 504378;
		loop2 = 5153813;
		stitle = "灼熱の炎の中で";
	}
	if (ss == "yc_b003.ogg") {
		loop1 = 32845;
		loop2 = 6955200;
		stitle = "最終決戦";
	}
	if (ss == "yc_b004.ogg") {
		loop1 = 53237;
		loop2 = 9737128;
		stitle = "黒き翼";
	}
	if (ss == "yc_b005.ogg") {
		loop1 = 1123422;
		loop2 = 7687672;
		stitle = "The False God of Causality";
	}
	if (ss == "yc_d101.ogg") {
		loop1 = 303237;
		loop2 = 2582426;
		stitle = "ダンジョン";
	}
	if (ss == "yc_d201.ogg") {
		loop1 = 447792;
		loop2 = 3479666;
		stitle = "道化師の誘い";
	}
	if (ss == "yc_d301.ogg") {
		loop1 = 351836;
		loop2 = 3969072;
		stitle = "地下遺跡";
	}
	if (ss == "yc_d401.ogg") {
		loop1 = 93865;
		loop2 = 4349569;
		stitle = "導きの塔～エルディールにくちづけを";
	}
	if (ss == "yc_d501.ogg") {
		loop1 = 832720;
		loop2 = 7219417;
		stitle = "失われし仮面を求めて";
	}
	if (ss == "yc_d701.ogg") {
		loop1 = 809264;
		loop2 = 6545498;
		stitle = "イリス";
	}
	if (ss == "yc_d702.ogg") {
		loop1 = 34816;
		loop2 = 1189171;
		stitle = "yc_d702";
	}
	if (ss == "yc_d703.ogg") {
		loop1 = 719876;
		loop2 = 2557197;
		stitle = "聖域";
	}
	if (ss == "yc_e001.ogg") {
		loop1 = 300048;
		loop2 = 3389821;
		stitle = "賢者";
	}
	if (ss == "yc_e002.ogg") {
		loop1 = 326209;
		loop2 = 3604271;
		stitle = "復活の儀式";
	}
	if (ss == "yc_e003.ogg") {
		loop1 = 806906;
		loop2 = 4275899;
		stitle = "レファンス";
	}
	if (ss == "yc_e004.ogg") {
		loop1 = 326209;
		loop2 = 4945888;
		stitle = "涙の少年剣士";
	}
	if (ss == "yc_e005.ogg") {
		loop1 = 24000;
		loop2 = 3605888;
		stitle = "エルディール";
	}
	if (ss == "yc_e006.ogg") {
		loop1 = 69040;
		loop2 = 1209633;
		stitle = "ロムン帝国 -嗚呼レオ団長-";
	}
	if (ss == "yc_e008.ogg") {
		loop1 = 275476;
		loop2 = 3609611;
		stitle = "yc_e008";
	}
	if (ss == "yc_e010.ogg") {
		loop1 = 807040;
		loop2 = 5159922;
		stitle = "冒険家、誕生";
	}
	if (ss == "yc_f101.ogg") {
		loop1 = 568926;
		loop2 = 5668207;
		stitle = "燃ゆる剣";
	}
	if (ss == "yc_f201.ogg") {
		loop1 = 588624;
		loop2 = 6209316;
		stitle = "セルセタの樹海";
	}
	if (ss == "yc_f301.ogg") {
		loop1 = 1145404;
		loop2 = 5960203;
		stitle = "クレーター";
	}
	if (ss == "yc_f401.ogg") {
		loop1 = 408974;
		loop2 = 3161454;
		stitle = "THE DAWN OF YS";
	}
	if (ss == "yc_f501.ogg") {
		loop1 = 2604464;
		loop2 = 4559688;
		stitle = "暁の森";
	}
	if (ss == "yc_f601.ogg") {
		loop1 = 581264;
		loop2 = 3661828;
		stitle = "一陣の風";
	}
	if (ss == "yc_f701.ogg") {
		loop1 = 324287;
		loop2 = 9010870;
		stitle = "神代の地";
	}
	if (ss == "yc_f801.ogg") {
		loop1 = 315435;
		loop2 = 4546653;
		stitle = "真実への序曲";
	}
	if (ss == "yc_f901.ogg") {
		loop1 = 178544;
		loop2 = 4786555;
		stitle = "雨上がりの朝に";
	}
	if (ss == "yc_over.ogg") {
		loop1 = 19200;
		loop2 = 4924407;
		stitle = "ゲームオーバー";
	}
	if (ss == "yc_t101.ogg") {
		loop1 = 865353;
		loop2 = 4409988;
		stitle = "辺境都市《キャスナン》";
	}
	if (ss == "yc_t201.ogg") {
		loop1 = 58906;
		loop2 = 6120526;
		stitle = "優しくなりたい";
	}
	if (ss == "yc_t301.ogg") {
		loop1 = 425910;
		loop2 = 9606150;
		stitle = "古代の伝承";
	}
	if (ss == "yc_t501.ogg") {
		loop1 = 782252;
		loop2 = 7781799;
		stitle = "RODA";
	}
	if (ss == "yc_title.ogg") {
		loop1 = 10000;
		loop2 = 4924407;
		stitle = "THEME OF ADOL 2012";
	}
	if (ss == "yc_op.ogg") {
		stitle = "The Foliage Ocean in CELCETA -Opening size-";
	}
	if (ss == "yc_end.ogg") {
		stitle = "新たな時代のステージへ";
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
			int ret = MessageBox(L"おそらく碧の軌跡のbgmデータで、碧の軌跡のbgmテーブルに情報がありません。\n零の軌跡のbgmテーブルを参照しますか？\n(碧の軌跡には零の軌跡のbgmデータも入ってるため、ループ情報は零の軌跡側にあります)", L"bgmテーブルに情報がありません。", MB_YESNO);
			if (ret == IDYES) {
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
	//-------------------------------------------------------------------
	if (m_dsb != NULL) m_dsb->Release();
	m_dsb = NULL;
	//	char bufdmy[10000];
	ZeroMemory(bufwav3, sizeof(bufwav3));
	DWORD  dwDataLen = WAVDALen / OUTPUT_BUFFER_NUM;
	if (((mode >= 10 && mode <= 21) || mode <= -10) && mode != -10 || mode == -6 || mode == 30) {
		for (; wavwait == 0;) { CWaitCursor rrr2; DoEvent(); }
		if (adbuf2 == NULL) { endflg = 0; return; }
		//		if(mode!=-10)
		//			playwavadpcm(bufwav3,0,dwDataLen*4,0);
		//		else
		//			playwavadpcm(bufdmy,0,dwDataLen*4,0);
	}
	else if (mode == -10) { //mp123
		CString s, ss;
		s = filen.Left(filen.ReverseFind('\\')); ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		_tchdir(s);
		CFile ff;
		if (ff.Open(ss, CFile::modeRead | CFile::shareDenyNone, NULL) == FALSE) {
			MessageBox(_T("ファイルが存在しません。\n削除されたかフォルダまたはファイル名が変更された可能性があります。"), _T("ファイルが存在しません。"));
			m_saisai.EnableWindow(TRUE); endflg = 0; return;
		}ff.Close();

		BYTE buf[2005];
		ZeroMemory(buf, 2005);
		if (ff.Open(ss, CFile::modeRead | CFile::shareDenyNone, NULL) == TRUE) {
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
		CMp3Info mp3__;
		mp3__.Load(ss);

		wavch = si1.dwChannels;
		wavbit = si1.dwSamplesPerSec;
		wavsam = si1.dwBitsPerSample;
		loop1 = 0; stitle = "";
		//		loop2=(int)(((float)(((float)si1.dwLength)*44.1f))/(44100.0f/((float)((wavch==2)?wavbit:(wavbit/2)))));
		//		loop2=(int)((float)(mp3_.m_mp3info.total_samples)/(wavch==2?1.0f:2.0f));
		loop2 = (int)(((double)mp3__.GetMSec()) / 1000.0 * (double)mp3_.m_mp3info.freq);//*(44100.0/((double)((wavch==2)?(double)wavbit:((double)wavbit/2.0)))));
		//		if(loop2==0){
		if (mp3_.m_mp3info.hasVbrtag) {
			//			loop2 *= 2.29;
		}
		//			loop2=(int)(((float)(((float)si1.dwLength)*44.1f))/(44100.0f/((float)((wavch==2)?wavbit:(wavbit/2)))));
		//		}
		data_size = oggsize = loop2;
		loop3 = loop2; loop2 = 0;
		m_time.SetRange(0, (int)(((data_size / 2.0) * (wavsam / 8.0)) / (100)), TRUE);
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
		if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit;


		if (1) {
			if (ss == "") {
				flacmode = 0;
				CFile pp; pp.Open(filen, CFile::shareDenyNone | CFile::modeRead);
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
			}
			else {
			}
		}
		wavbit = sikpi.dwSamplesPerSec;
		wavch = sikpi.dwChannels;
		loop1 = 0; oggsize = loop2 = (int)((double)sikpi.dwLength * (double)sikpi.dwSamplesPerSec / 1000.0 / (wavsam / 16.0));
		wavsam = sikpi.dwBitsPerSample;
		CString s; s.Format(L"%d", oggsize);
		//AfxMessageBox(s);
		si1.dwSamplesPerSec = wavbit;
		si1.dwChannels = wavch;
		si1.dwBitsPerSample = wavsam;
		if (sikpi.dwLength == (DWORD)-1) loop2 = 0;
		data_size = oggsize = loop2 * (wavsam / 4);
		if (wavch == 1) oggsize /= 2;
		if (wavch == 1) data_size /= 2;
		m_time.SetRange(0, (data_size) / (wavsam / 4), TRUE);
		flac_.SetPosition(kmp, 0);
		kbps = 0;
		CFile ff;
		ff.Open(filen, CFile::modeRead | CFile::shareDenyNone, NULL);
		int flg, read = ff.Read(bufimage, sizeof(bufimage));
		ff.Close();

		if (bufimage[0] == 0xBF) {
			CFile fff;
			if (fff.Open(filen, CFile::shareDenyNone | CFile::modeRead)) {
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
		if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit;

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
			}
			else {
			}
		}
		wavsam = sikpi.dwBitsPerSample;
		wavbit = sikpi.dwSamplesPerSec;	wavch = sikpi.dwChannels;	loop1 = 0; oggsize = loop2 = (int)((float)sikpi.dwLength / (wavsam / 4) /*/ (float)1000.0f* (float)sikpi.dwSamplesPerSec*/);
		CString s_; s_.Format(L"%d", wavbit);
		si1.dwSamplesPerSec = sikpi.dwSamplesPerSec;
		si1.dwChannels = wavch;
		si1.dwBitsPerSample = wavsam;
		Vbr = 1;
		if (sikpi.dwLength == (DWORD)-1) loop2 = 0;
		data_size = oggsize = loop2 * (wavsam / 4);
		m_time.SetRange(0, (data_size) / (wavsam / 4), TRUE);
		m4a_.SetPosition(kmp, 0);
		kbps = 0;
		CFile ff;
		ff.Open(filen, CFile::modeRead | CFile::shareDenyNone, NULL);
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
	else if (mode == -8) { // flac
		CString ss;
		char buf[1024];
		ss = "";
		ZeroMemory(&sikpi, sizeof(sikpi));
		sikpi.dwSamplesPerSec = savedata.samples; sikpi.dwChannels = 8; sikpi.dwSeekable = 1; sikpi.dwLength = -1; sikpi.dwBitsPerSample = ((savedata.bit24 == 1) ? 24 : 16);
		if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit;


		if (1) {
			if (ss == "") {
#if UNICODE
				TCHAR* f = filen.GetBuffer();
				kmp = flac_.Open(f, &sikpi);
				filen.ReleaseBuffer();
#else
				kmp = flac_.Open(filen, &sikpi);
#endif
				if (kmp == NULL) { m_saisai.EnableWindow(TRUE); endflg = 0; return; }
			}
			else {
			}
		}
		wavbit = sikpi.dwSamplesPerSec;
		wavch = sikpi.dwChannels;
		loop1 = 0; oggsize = loop2 = (int)((double)sikpi.dwLength * (double)sikpi.dwSamplesPerSec / 1000.0 / (wavsam / 16.0));
		wavsam = sikpi.dwBitsPerSample;
		CString s; s.Format(L"%d", oggsize);
		//AfxMessageBox(s);
		si1.dwSamplesPerSec = wavbit;
		si1.dwChannels = wavch;
		si1.dwBitsPerSample = wavsam;
		if (sikpi.dwLength == (DWORD)-1) loop2 = 0;
		data_size = oggsize = loop2 * (wavsam / 4);
		if (wavch == 1) oggsize /= 2;
		if (wavch == 1) data_size /= 2;
		m_time.SetRange(0, (data_size) / (wavsam / 4), TRUE);
		flac_.SetPosition(kmp, 0);
		kbps = 0;
		CFile ff;
		ff.Open(filen, CFile::modeRead | CFile::shareDenyNone, NULL);
		int flg, read = ff.Read(bufimage, sizeof(bufimage));
		ff.Close();
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
	else if (mode == -3) { // kpi
		ret2 = 0;
		hDLLk = LoadLibrary(kpi);
		typedef HRESULT(WINAPI* kpi_CreateInstance)(REFIID riid, void** ppvObject, IKpiUnknown* pUnknown);
		kpi_CreateInstance cr = (kpi_CreateInstance)GetProcAddress(hDLLk, "kpi_CreateInstance");
		pFunck = (pfnGetKMPModule)::GetProcAddress(hDLLk, SZ_KMP_GETMODULE);
		if (kvver == 2) {
			mod = pFunck();
			if (mod == NULL) {
				MessageBox(_T("なんらかの要因でkpiが開けませんでした。"), _T("ファイルが存在しません。"));
				fnn = "kpi構造体を獲得できませんでした。";
				FreeLibrary(hDLLk);
				m_saisai.EnableWindow(TRUE); endflg = 0; return;
			}
			CString ss;
			ss = filen.Left(filen.ReverseFind(':') - 1);
			ZeroMemory(&sikpi, sizeof(sikpi));
			sikpi.dwSamplesPerSec = savedata.samples; sikpi.dwChannels = 8; sikpi.dwSeekable = 1; sikpi.dwLength = -1; sikpi.dwBitsPerSample = 16;
			if (savedata.bit24 == 1)sikpi.dwBitsPerSample = 24;
			if (savedata.bit32 == 1)sikpi.dwBitsPerSample = 32;
			if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit;
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
			wavbit = sikpi.dwSamplesPerSec;	wavch = sikpi.dwChannels;	loop1 = 0; loop2 = (int)((double)sikpi.dwLength * (double)sikpi.dwSamplesPerSec / 1000.0);
			wavsam = sikpi.dwBitsPerSample;
		}
		else if (kvver == 5) {
			IUnknown* pMyObject = new CMyHost();
			IKpiDecoderModule* ob = NULL;
			HRESULT hr = cr(IID_IKpiDecoderModule, (void**)&ob, pMyObject);
			if (hr == S_OK) {
				ob5 = (IKpiDecoderModule*)ob;
				kpi_InitMediaInfo(&me5);
				//				me5.cb = sizeof(KPI_MEDIAINFO);
				me5.dwSampleRate = savedata.samples;
				me5.nBitsPerSample = 16;
				if (savedata.bit24 == 1)sikpi.dwBitsPerSample = 24;
				if (savedata.bit32 == 1)sikpi.dwBitsPerSample = 32;
				if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit;
				IKpiFile* ik;
				IKpiFolder* ik2 = new CMyDummyFolder();
				CMyHostFile* pHostFile = new CMyHostFile();
				ss = filen.Left(filen.ReverseFind(':') - 1);
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
				if (kpidec == NULL) return;
				int sel = 1;
				if (ss != L"")sel = (_tstoi(filen.Right(4)));
				if (sel <= 0) sel = 1;
				DWORD dwSelectedSong = kpidec->Select(
					sel,              // [in] 曲番号 (1ベース)
					&pMediaInfo,    // [out] 曲情報
					NULL,           // [in] IKpiTagInfo (NULL)
					0               // [in] dwTagGetFlags (KPI_TAGGET_FLAG_NONE)
				);
				if (ik2) {
					ik2->Release();
				}
				if (ik) { // ik (pHostFile) も解放
					ik->Release();
				}
				if (pMediaInfo == NULL) return;
				wavbit = pMediaInfo->dwSampleRate;	wavch = pMediaInfo->dwChannels;	loop1 = 0; loop2 = kpi_100nsToSample(pMediaInfo->qwLength, pMediaInfo->dwSampleRate);;
				wavsam = pMediaInfo->nBitsPerSample;
			}
		}

		if (sikpi.dwLength == (DWORD)-1) loop2 = 0;
		data_size = oggsize = loop2 * (wavsam / 4);
		m_time.SetRange(0, (data_size) / (wavsam / 4), TRUE);
		if (kvver == 2 && mod->SetPosition) mod->SetPosition(kmp1, 0);
		if (kvver == 5) kpidec->Seek(0, 0);
		wav_start();
		CFile ff;
		CString ss11 = filen; ss11.MakeLower();
		if (ss11.Right(3) == "m4a") {
			if (ff.Open(filen, CFile::modeRead | CFile::shareDenyNone, NULL) == TRUE) {
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
			if (ff.Open(filen, CFile::modeRead | CFile::shareDenyNone, NULL) == TRUE) {
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
			if (ff.Open(ss, CFile::modeRead | CFile::shareDenyNone, NULL) == TRUE) {
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
			double wavv[] = { 0,1.0,2.0,3.0 / 0.75,4.0 / 0.75,5.0 / 0.75,6.0 / 0.75 };//(double)(wavbit2/wavv[wavch])
			double wavv2[] = { 0,2.0,1.0,2.0 / 3.0,2.0 / 4.0,2.0 / 5.0,2.0 / 6.0 };//(double)(wavbit2/wavv[wavch])
			double t3 = (double)oggsize / (double)(wavbit * 2.0 * wavv[wavch]) / (double)(wavsam / 16.0f);
			if (mode == -10) t3 *= (wavsam / 16.0f) * 4.0;
			if ((mode == -9) && wavch > 2) t3 *= wavch / 2.0;
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
		wavbit = 48000;
	}
	if (mode == -1) {
		// 零の軌跡用
		CString ss, sss;
		ss = filen.Right(filen.GetLength() - filen.ReverseFind(L'\\') - 1);
		sss = filen.Left(filen.ReverseFind('\\'));
		int fg = 0;
		CFile ffff; if (ffff.Open(sss + L"\\..\\text\\t_bgm._dt", CFile::modeRead)) { fg = 1; ffff.Close(); }
		if (ss.Mid(0, 3) == L"ed7" && fg == 1) {
			CString a;
			switch (_ttoi(ss.Mid(2, 4))) {
			case 7001:
				a = L"零の軌跡";
				break;
			case 7002:
				a = L"way of live -Opening Version-";
				break;
			case 7003:
				a = L"新しき日々～予兆";
				break;
			case 7005:
				a = L"想い破れて・・・";
				break;
			case 7052:
				a = L"碧い軌跡 -Opening size-";
				break;
			case 7053:
				a = L"それでも僕らは。";
				break;
			case 7100:
				a = L"街角の風景";
				break;
			case 7101:
				a = L"明日は明日の風が吹く";
				break;
			case 7102:
				a = L"クロスベルの午後";
				break;
			case 7103:
				a = L"During Mission Accomplishment";
				break;
			case 7104:
				a = L"創立記念祭";
				break;
			case 7105:
				a = L"降水確率10%";
				break;
			case 7106:
				a = L"風船と紙吹雪";
				break;
			case 7110:
				a = L"特務支援課";
				break;
			case 7111:
				a = L"C.S.P.D. -クロスベル警察";
				break;
			case 7113:
				a = L"Arc-en-ciel";
				break;
			case 7114:
				a = L"黒月貿易公司";
				break;
			case 7116:
				a = L"IGNIS";
				break;
			case 7117:
				a = L"TRINITY";
				break;
			case 7120:
				a = L"アルモリカ村";
				break;
			case 7121:
				a = L"鉱山町マインツ";
				break;
			case 7122:
				a = L"Killing Bear";
				break;
			case 7123:
				a = L"聖ウルスラ医科大学";
				break;
			case 7124:
				a = L"クロスベル大聖堂";
				break;
			case 7125:
				a = L"黒の競売会";
				break;
			case 7126:
				a = L"大国にはさまれて";
				break;
			case 7150:
				a = L"新たなる日常";
				break;
			case 7151:
				a = L"動き始めた事態";
				break;
			case 7160:
				a = L"ミシュラムワンダーランド";
				break;
			case 7161:
				a = L"束の間の休息";
				break;
			case 7162:
				a = L"ささやかな晩餐";
				break;
			case 7200:
				a = L"水と草木と青い空";
				break;
			case 7201:
				a = L"片手にはレモネード";
				break;
			case 7202:
				a = L"木霊の道";
				break;
			case 7203:
				a = L"古の鼓動";
				break;
			case 7204:
				a = L"On The Green Road";
				break;
			case 7205:
				a = L"鉄橋を越えて";
				break;
			case 7250:
				a = L"木洩れ日の中の静寂";
				break;
			case 7251:
				a = L"偽りの楽土を越えて";
				break;
			case 7300:
				a = L"ジオフロント";
				break;
			case 7301:
				a = L"七耀の煌き";
				break;
			case 7302:
				a = L"ルバーチェ商会";
				break;
			case 7303:
				a = L"鳴るはずのない鐘";
				break;
			case 7304:
				a = L"忘れられし幻夢の狭間";
				break;
			case 7305:
				a = L"A Light Illuminating The Depths";
				break;
			case 7350:
				a = L"Dの残影";
				break;
			case 7351:
				a = L"異変の兆し";
				break;
			case 7352:
				a = L"Mystic Core";
				break;
			case 7353:
				a = L"最果ての樹";
				break;
			case 7354:
				a = L"暴魔の呼び声";
				break;
			case 7356:
				a = L"不明";
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
				a = L"これが俺たちの力だ!";
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
				a = L"効果音";
				break;
			case 7500:
				a = L"金の太陽、銀の月　-陽の熱情";
				break;
			case 7501:
				a = L"金の太陽、銀の月　-月の慕情";
				break;
			case 7502:
				a = L"金の太陽、銀の月　-童心";
				break;
			case 7503:
				a = L"金の太陽、銀の月　-運命の刻";
				break;
			case 7504:
				a = L"金の太陽、銀の月　-譲れぬ想い";
				break;
			case 7505:
				a = L"金の太陽、銀の月　-幾千の夜を越えて";
				break;
			case 7506:
				a = L"金の太陽、銀の月　-夜明け～大団円";
				break;
			case 7507:
				a = L"Intense Chase";
				break;
			case 7509:
				a = L"守りぬく意志";
				break;
			case 7510:
				a = L"叡智への誘い";
				break;
			case 7511:
				a = L"危地";
				break;
			case 7512:
				a = L"揺るぎない強さ";
				break;
			case 7513:
				a = L"夜景に霞む星空";
				break;
			case 7514:
				a = L"いつかきっと";
				break;
			case 7515:
				a = L"柔らかな心";
				break;
			case 7516:
				a = L"点と線";
				break;
			case 7517:
				a = L"一触即発";
				break;
			case 7518:
				a = L"Foolish Gig";
				break;
			case 7519:
				a = L"リベールからの風";
				break;
			case 7520:
				a = L"とどいた想い";
				break;
			case 7521:
				a = L"Underground Kids";
				break;
			case 7522:
				a = L"Terminal Room";
				break;
			case 7523:
				a = L"響きあう心";
				break;
			case 7524:
				a = L"Limit Break";
				break;
			case 7525:
				a = L"パラダイスミ☆";
				break;
			case 7526:
				a = L"Gnosis";
				break;
			case 7527:
				a = L"Get Over The Barrier! -Roaring Version-";
				break;
			case 7528:
				a = L"それぞれの明日";
				break;
			case 7529:
				a = L"効果音楽1";
				break;
			case 7530:
				a = L"効果音楽2";
				break;
			case 7531:
				a = L"効果音楽3";
				break;
			case 7532:
				a = L"効果音楽4";
				break;
			case 7533:
				a = L"踏み出す勇気";
				break;
			case 7534:
				a = L"その背中を見つめて";
				break;
			case 7540:
				a = L"不明";
				break;
			case 7541:
				a = L"不明";
				break;
			case 7542:
				a = L"不明";
				break;
			case 7543:
				a = L"不明";
				break;
			case 7544:
				a = L"不明";
				break;
			case 7550:
				a = L"オルキスタワー";
				break;
			case 7551:
				a = L"Catastrophe";
				break;
			case 7552:
				a = L"碧き雫";
				break;
			case 7553:
				a = L"神機降臨";
				break;
			case 7554:
				a = L"ふるわれる奇蹟";
				break;
			case 7555:
				a = L"予定外の奇蹟";
				break;
			case 7556:
				a = L"鋼鉄の咆哮 -脅威-";
				break;
			case 7560:
				a = L"雨の日の真実";
				break;
			case 7561:
				a = L"不穏";
				break;
			case 7562:
				a = L"効果音";
				break;
			case 7563:
				a = L"犠牲の先の希望";
				break;
			case 7564:
				a = L"Strange Feel";
				break;
			case 7565:
				a = L"Exhilarating Ride";
				break;
			case 7566:
				a = L"それぞれの正義";
				break;
			case 7567:
				a = L"乗り越えるべき壁";
				break;
			case 7568:
				a = L"月下の想い";
				break;
			case 7569:
				a = L"Miss You";
				break;
			case 7570:
				a = L"天の車";
				break;
			case 7571:
				a = L"突きつけられた現実";
				break;
			case 7572:
				a = L"効果音";
				break;
			case 7573:
				a = L"全てを識るもの";
				break;
			case 7574:
				a = L"想い、辿り着く場所";
				break;
			case 7575:
				a = L"揺れ動く心";
				break;
			case 7576:
				a = L"星降る夜に";
				break;
			case 7577:
				a = L"効果音";
				break;
			case 7578:
				a = L"効果音";
				break;
			case 7579:
				a = L"効果音";
				break;
			case 7580:
				a = L"効果音";
				break;
			case 7581:
				a = L"本当の絆";
				break;
			case 7582:
				a = L"猛き獣たち";
				break;
			case 7583:
				a = L"西ゼムリア通商会議";
				break;
			case 7584:
				a = L"効果音";
				break;
			case 7585:
				a = L"千年の妄執";
				break;
			case 7586:
				a = L"鋼鉄の咆哮 -死線-";
				break;
			case 7587:
				a = L"ポムっと! -お花見団子の逆襲-";
				break;
			case 7588:
				a = L"Fateful Confrontation -ポムっと! Ver.-";
				break;
			case 7589:
				a = L"ポムりますか";
				break;
			case 7590:
				a = L"エリィ絶叫コースター";
				break;
			case 7591:
				a = L"小さな英雄 -オルゴール-";
				break;
			case 7592:
				a = L"TOWER OF THE SHADOW OF DEATH -Jukebox-";
				break;
			}
			stitle = a;
		}
	}
	if (m_c2.GetCheck() == 1)
	{
		cc1 = 1;
		if (cc.Open(filen + _T(".wav"), CFile::modeCreate | CFile::modeReadWrite | CFile::typeBinary, NULL) != TRUE) {
			m_saisai.EnableWindow(TRUE);
			endflg = 0;
			return;
		}
		if (ogg)	cc.Write(ogg, whsize);
		if (wav) cc.Write(wav, whsize);
	}
	if (mode == 30) { wavbit = 48000; wavsam = 16; wavch = 2; }


	WAVEFORMATEX wfx1;
	if (wavsam < 0)
		wfx1.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	else
		wfx1.wFormatTag = WAVE_FORMAT_PCM;
	wfx1.nChannels = wavch;
	wfx1.nSamplesPerSec = wavbit;
	wfx1.wBitsPerSample = abs(wavsam);
	wfx1.nBlockAlign = wfx1.nChannels * wfx1.wBitsPerSample / 8;
	wfx1.nAvgBytesPerSec = wfx1.nSamplesPerSec * wfx1.nBlockAlign;
	wfx1.cbSize = 0;

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
	//if (wavbit2 != wavbit || wavsam == 24 || si1.dwBitsPerSample || sikpi.dwBitsPerSample) {
	ReleaseDXSound();
	if (WASAPIInit() == 0) init(m_hWnd, wavbit);
	//}
	wavsam = abs(wavsam);
	wavbit2 = wavbit;
	int i, iii = 0;
	double ik = 32.0;
	double d2 = 0;
	double il = 8.71712838;
	d2 = 0;

	for (i = 0; i <= 88; i++, iii++) { // 低音域用
		logtbl[i] = (int)(il * pow(2.0, (double)(iii) / ik));// / ((wavbit / 44100.0)*16.0);// *(double)BUFSZH1 / (double)192000 / 4.0 + 1.0);
		//		double tl = wavbit / 44100.0;
//		logtbl[i] = (int)((double)syuha[i]) / ((((87/tl) - (i/tl)) / (11.0) + 1.0)) / (tl);//(44100.0/ (double)wavbit) * ((double)wavsam / 8.0) *

		if (i < 20) {
			if (wavbit > 90000)
				ik -= (0.15 + d2);// -(((double)wavbit / 44100.0) / 50.0 - 0.02));
			else
				ik -= (0.20 + d2);// -(((double)wavbit / 44100.0) / 50.0 - 0.02));
		}
		else {
			if (wavbit > 90000)
				ik -= (0.14 + d2);// -(((double)wavbit / 44100.0) / 50.0 - 0.02));
			else
				ik -= (0.18 + d2);// -(((double)wavbit / 44100.0) / 50.0 - 0.02));
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
	//if (pAudioClient == NULL) {
	DSBUFFERDESC dsbd;
	flg0 = 0;
	for (;;) {
		ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
		dsbd.dwSize = sizeof(DSBUFFERDESC);
		dsbd.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME;// | DSBCAPS_CTRL3D;
		dsbd.dwBufferBytes = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM;
		if (wavch > 2)
			dsbd.lpwfxFormat = (LPWAVEFORMATEX)&wfx;
		else
			dsbd.lpwfxFormat = &wfx1;
		//dsbd.guid3DAlgorithm = DS3DALG_HRTF_LIGHT;
		HRESULT r;
		ReleaseDXSound();
		if (WASAPIInit() == 0) init(m_hWnd, wavbit);
		r = m_ds->CreateSoundBuffer(&dsbd, &m_dsb1, NULL);
		if (m_dsb1 == NULL || m_p == NULL) {
			if (flg0 == 0) {
				CString s; s.Format(L"%d", savedata.samples);
				MessageBox(s + L"Hzのサンプリングレートにサウンドカードが対応していません\n低いサンプリングレートを試みます。\n少々時間が掛かる場合があります。", _T("ogg/wav簡易プレイヤ"));
				flg0 = 1;
			}
			wavbit -= 1000;
			if (wavbit <= 0) {
				MessageBox(L"0Hzまで試みましたが、対応するサンプリングレートが存在しませんでした。\nサウンドボード(カード)が存在していない可能性があります。", _T("ogg/wav簡易プレイヤ"));
				tagfile = fnn;
				m_saisai.EnableWindow(TRUE);
				endflg = 0;
				return;
			}
			wfx.Format.nSamplesPerSec = wavbit;
			wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
			wfx1.nSamplesPerSec = wavbit;
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
			AfxMessageBox(_T("DirectSoundが開けませんでした。"));
			if (r == DSERR_ALLOCATED) {
				AfxMessageBox(_T("優先レベルなどのリソースが他の呼び出しによって既に使用中であるため、要求は失敗した。"));
			}
			else if (r == DSERR_CONTROLUNAVAIL) {
				AfxMessageBox(_T("呼び出し元が要求するバッファ コントロール (ボリューム、パンなど) は利用できない。"));
			}
			else if (r == DSERR_BADFORMAT) {
				AfxMessageBox(_T("指定したウェーブ フォーマットはサポートされていない。"));
			}
			else if (r == DSERR_INVALIDPARAM) {
				AfxMessageBox(_T("無効なパラメータが関数に渡された。"));
			}
			else if (r == DSERR_NOAGGREGATION) {
				AfxMessageBox(_T("このオブジェクトは COM 集合化をサポートしない。"));
			}
			else if (r == DSERR_OUTOFMEMORY) {
				AfxMessageBox(_T("DirectSound サブシステムは、呼び出し元の要求を完了するための十分なメモリを割り当てられなかった。"));
			}
			else if (r == DSERR_UNINITIALIZED) {
				AfxMessageBox(_T("他のメソッドを呼び出す前に IDirectSound::Initialize メソッドを呼び出さなかったか、呼び出しが成功しなかった。"));
			}
			else if (r == DSERR_UNSUPPORTED) {
				AfxMessageBox(_T("呼び出した関数はこの時点ではサポートされていない。"));
			}
			else {}

			tagfile = fnn;
			m_saisai.EnableWindow(TRUE);
			endflg = 0;
			return;
		}
		if (m_dsb) {
			if (flg0 == 1) {
				CString s; s.Format(L"%d", wavbit);
				MessageBox(s + L"Hzのサンプリングレートでヒットしましたため、該当サンプリングレートで演奏します。", _T("ogg/wav簡易プレイヤ"));
				savedata.samples = wavbit;
				SetTimer(9100, 100, NULL);
				return;
			}
			break;
		}
	}
	//}
	//else {
	//		if (wavch > 2)
	//		WASAPIChange((LPWAVEFORMATEX)&wfx);
	//		else
	//			WASAPIChange(&wfx1);
	//}
	//m_dsb->QueryInterface(IID_IDirectSound3DBuffer, (LPVOID*)m_dsb3d);

	DWORD le = WAVDAStartLen;
	ttt = WAVDAStartLen;

	stf = 0;
	plf = 1; fade1 = 0; fade = 1.0f; fadeadd = 0.0f;
	poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
	mcnt = mcnt1 = mcnt2 = mcnt3 = mcnt4 = mcnt5 = mcnt6 = 0;
	char* pdsb;
	lo = 0; loc = 0;

	if (loop1 == 0 && loop2 == 0) {
		m_time.SetSelection(0, data_size / 4);
		m_time.Invalidate();
	}
	else {
		m_time.SetSelection(loop1, (loop1 + loop2));
		m_time.Invalidate();
	}

	int len1, len2, len3;
	if (true) {
		ULONG PlayCursor, WriteCursor = 0;
		if (m_dsb)m_dsb->GetCurrentPosition(&PlayCursor, &WriteCursor);//再生位置取得
		len1 = (int)WriteCursor;//書き込み範囲取得
		len2 = 0;
		if (len1 < 0) {
			len1 = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM / 24; len2 = WriteCursor;
		}
		if (len2 < 0)
			len2 = 0;
		if ((mode >= 10 && mode <= 21) || mode < -10 || mode == -6 || mode == 30)
			playwavadpcm(bufwav3, 0, len1, len2);//データ獲得
		else if (mode == -10)
			playwavmp3(bufwav3, 0, len1, len2);//データ獲得
		else if (mode == -3)
			playwavkpi(bufwav3, 0, len1, len2);//データ獲得
		else if (mode == -7)
			playwavdsd(bufwav3, 0, len1, len2);//データ獲得
		else if (mode == -8)
			playwavflac(bufwav3, 0, len1, len2);//データ獲得
		else if (mode == -9)
			playwavm4a(bufwav3, 0, len1, len2);//データ獲得
		else
			playwavds2(bufwav3, 0, len1, len2);//データ獲得
		m_dsb->Lock(0, len1 + len2, (LPVOID*)&pdsb, (DWORD*)&len3, NULL, 0, 0);
		memcpy(pdsb, bufwav3, len3);
		m_dsb->Unlock(pdsb, len3, NULL, 0);
		m_dsb->SetVolume((savedata.dsvol - 1) * 10);
		CFile f123;
		int flggg = 0;
		if (mode != -1) {
			if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE) {
				f123.Close();
				if (IDYES == MessageBox(_T("途中再生データが存在します。\n前回中断した部分から再生しますか？\nはい = 途中から再生\nいいえ = はじめから再生"), _T("再生確認"), MB_YESNO)) {
					flggg = 1;
				}
				else {
					CFile::Remove(filen + _T(".save"));
				}
			}
			if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE && flggg == 1) {
				f123.Close();
				if (pGraphBuilder)pMainFrame1->plays2();
				//if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
				if (mode == -10) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE) {
						f123.Read(&playb, sizeof(__int64));
						if (savedata.mp3orig) {
							mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch);
						}
						else {
							mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch);
						}
						f123.Close();
					}
				}
				if (mode == -2) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE) {
						f123.Read(&aa1_, sizeof(double));
						pMainFrame1->seek((LONGLONG)(((float)((float)aa1_ * 100.0f * 100000.0f))));
						f123.Close();
					}
				}
			}
			else {
				if (pGraphBuilder)pMainFrame1->plays2();
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
		m_dsb->Play(0, 0, DSBPLAY_LOOPING);
		fade1 = 0;
		sflg = FALSE;
		DoEvent();
		for (;;) {
			if (sflg == FALSE) break;
			DoEvent();
		}
		AfxBeginThread((AFX_THREADPROC)HandleNotifications, NULL, THREAD_PRIORITY_TIME_CRITICAL);
	}
	endflg = 0;
	SetTimer(9000, 10, NULL);
	//	::SetPriorityClass(m_thread, HIGH_PRIORITY_CLASS);
	endf = 0;
	if (pl && plw) { if (pl->m_loop.GetCheck() == TRUE) { if (loop2 == 0)loop2 = oggsize / 4; } }
	if (loop2 == 0) endf = 1;
	if (mode == -3 || mode == -8 || mode == -9 || mode == -10) endf = 1;
	loopcnt = 0;
	if (pl && plw) {
		int plc = 1;
		if (mode == -10)
			plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, (oggsize / (2 * wavch * wavbit / 4) / ((mode == -9) ? 4 : 1)), 1);
		else if (mode == -9 || mode == -8 || mode == -7) {
			double wavv[] = { 0,1.0,2.0,2.0,2.0,2.0,2.0 };//(double)(wavbit2/wavv[wavch])
			plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, (int)(
				(double)oggsize / (double)(wavbit * 2 * wavv[wavch]) / (double)(wavsam / 16.0f)
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
					plc = pl->Add(fnn, mode, loop1, loop2, tagname, tagalbum, filen, 0, oggsize / (2 * wavch * wavbit), 1);
				else
					plc = pl->chk(fnn, mode, tagname, filen, 0);
		}
		else if (!((pMainFrame1 && mode == -1) || mode == -3))
			plc = pl->Add(fnn, mode, loop1, loop2, stitle, tagalbum, filen, ret2, oggsize / (2 * wavch * wavbit));
		else
			plc = pl->chk(fnn, mode, tagname, filen, 0);

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
	m_saisai.EnableWindow(TRUE); playy = 1;
	ps = 0;
	m_ps.SetWindowText(_T("一時停止"));
	SetTimer(1250, 100, NULL);
	fade1 = 0;
	if (maini) maini->SetActiveWindow();
	SetActiveWindow();
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

HANDLE hp;
//スレッド
void CWread::wavread()
{
	DWORD dl, dwDataLen = (WAVDALen / OUTPUT_BUFFER_NUM) * 4; dl = dwDataLen;
	if (mode == 21) {
		CString a = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		switch (_ttoi(a.Mid(2, 4))) {
		case 8001:
			a = L"特科クラス《VII組》";
			break;
		case 8002:
			a = L"ただひたすらに、前へ";
			break;
		case 8100:
			a = L"近郊都市トリスタ";
			break;
		case 8101:
			a = L"交易町ケルディック";
			break;
		case 8102:
			a = L"翡翠の公都バリアハート";
			break;
		case 8103:
			a = L"湖畔の街レグラム";
			break;
		case 8104:
			a = L"黒銀の鋼都ルーレ";
			break;
		case 8106:
			a = L"遊牧民の集落";
			break;
		case 8107:
			a = L"緋の帝都ヘイムダル";
			break;
		case 8108:
			a = L"癒しの我が家";
			break;
		case 8109:
			a = L"ダイニングバー《F》";
			break;
		case 8110:
			a = L"常在戦場の気概";
			break;
		case 8111:
			a = L"ガレリアの巨壁";
			break;
		case 8120:
			a = L"足湯の温もり";
			break;
		case 8121:
			a = L"静寂の郷";
			break;
		case 8122:
			a = L"明日への休息";
			break;
		case 8123:
			a = L"春の陽射し";
			break;
		case 8125:
			a = L"カレイジャス発進！";
			break;
		case 8126:
			a = L"目覚める意志";
			break;
		case 8127:
			a = L"白銀の巨船";
			break;
		case 8150:
			a = L"放課後の時間";
			break;
		case 8152:
			a = L"さわやかな朝";
			break;
		case 8153:
			a = L"雨音の学院";
			break;
		case 8154:
			a = L"爽やかな陽射し";
			break;
		case 8156:
			a = L"トールズ士官学院祭";
			break;
		case 8158:
			a = L"青空の開放感";
			break;
		case 8159:
			a = L"自由行動日";
			break;
		case 8200:
			a = L"異郷の空";
			break;
		case 8201:
			a = L"峡谷道を往く";
			break;
		case 8202:
			a = L"精霊の小道";
			break;
		case 8203:
			a = L"蒼穹の大地";
			break;
		case 8210:
			a = L"戦火を越えて";
			break;
		case 8212:
			a = L"Trudge Along";
			break;
		case 8213:
			a = L"冬の訪れ";
			break;
		case 8300:
			a = L"旧校舎の謎";
			break;
		case 8301:
			a = L"探索";
			break;
		case 8302:
			a = L"深淵へ向かう";
			break;
		case 8303:
			a = L"聖女の城";
			break;
		case 8304:
			a = L"明日を掴むために";
			break;
		case 8305:
			a = L"地下に眠る遺構";
			break;
		case 8308:
			a = L"世の礎たるために";
			break;
		case 8310:
			a = L"精霊窟";
			break;
		case 8311:
			a = L"不明";
			break;
		case 8312:
			a = L"Phantasmal Blaze";
			break;
		case 8313:
			a = L"夢幻回廊";
			break;
		case 8315:
			a = L"幻煌";
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
			a = L"巨イナルチカラ";
			break;
		case 8409:
			a = L"The Decisive Collision";
			break;
		case 8410:
			a = L"この手で道を切り拓く!";
			break;
		case 8411:
			a = L"赤点です...";
			break;
		case 8412:
			a = L"Unknown Threat";
			break;
		case 8413:
			a = L"不明";
			break;
		case 8420:
			a = L"Heated Mind";
			break;
		case 8421:
			a = L"不明";
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
			a = L"輝ける明日へ";
			break;
		case 8435:
			a = L"迫る巨影";
			break;
		case 8441:
			a = L"E.O.V";
			break;
		case 8442:
			a = L"不明";
			break;
		case 8500:
			a = L"Strain";
			break;
		case 8501:
			a = L"夜のひととき";
			break;
		case 8502:
			a = L"トラブル発生";
			break;
		case 8503:
			a = L"鉄路遥々";
			break;
		case 8504:
			a = L"旅愁";
			break;
		case 8505:
			a = L"皇城にて";
			break;
		case 8506:
			a = L"Let's Study";
			break;
		case 8507:
			a = L"知恵を絞って";
			break;
		case 8508:
			a = L"実技教練";
			break;
		case 8509:
			a = L"寮に帰ろう";
			break;
		case 8510:
			a = L"アーベントタイム";
			break;
		case 8512:
			a = L"鉄の統率";
			break;
		case 8513:
			a = L"暗躍";
			break;
		case 8514:
			a = L"想いの行き先";
			break;
		case 8515:
			a = L"傷心";
			break;
		case 8516:
			a = L"揺らめく炎を見つめて";
			break;
		case 8517:
			a = L"一途な気持ち";
			break;
		case 8520:
			a = L"臨戦態勢";
			break;
		case 8521:
			a = L"Seriousness";
			break;
		case 8522:
			a = L"静かなる昂揚";
			break;
		case 8523:
			a = L"暖かな夕餉";
			break;
		case 8524:
			a = L"Atrocious Raid";
			break;
		case 8525:
			a = L"全てを賭して今、ここに立つ";
			break;
		case 8527:
			a = L"新しい仲間たち";
			break;
		case 8528:
			a = L"不透明な事態";
			break;
		case 8529:
			a = L"鉄血へのレクイエム";
			break;
		case 8530:
			a = L"幻想の唄 -PHANTASMAGORIA-";
			break;
		case 8531:
			a = L"刻ハ至レリ";
			break;
		case 8532:
			a = L"目覚めし伝承";
			break;
		case 8533:
			a = L"唯一の希望";
			break;
		case 8535:
			a = L"不明";
			break;
		case 8537:
			a = L"不明";
			break;
		case 8538:
			a = L"今はまだ...";
			break;
		case 8539:
			a = L"あの日に見た夜空";
			break;
		case 8540:
			a = L"偽りの時間";
			break;
		case 8541:
			a = L"紅き翼 -新たなる風-";
			break;
		case 8550:
			a = L"再会";
			break;
		case 8551:
			a = L"かけがえのない人へ";
			break;
		case 8552:
			a = L"惜しむように、愛おしむように";
			break;
		case 8553:
			a = L"ライノの花が咲く頃";
			break;
		case 8555:
			a = L"戦場の掟";
			break;
		case 8556:
			a = L"Remaining Glow";
			break;
		case 8557:
			a = L"深淵の魔女";
			break;
		case 8558:
			a = L"ALTINA";
			break;
		case 8559:
			a = L"威風";
			break;
		case 8560:
			a = L"一撃に賭ける";
			break;
		case 8561:
			a = L"ユミル渓谷道";
			break;
		case 8562:
			a = L"Awakening";
			break;
		case 8563:
			a = L"Blitzkrieg";
			break;
		case 8564:
			a = L"魔王の凱歌";
			break;
		case 8566:
			a = L"内なる黄昏";
			break;
		case 8567:
			a = L"蘇る記憶";
			break;
		case 8570:
			a = L"静かな決意";
			break;
		case 8571:
			a = L"乾坤一擲";
			break;
		case 8572:
			a = L"交戦";
			break;
		case 8573:
			a = L"効果音";
			break;
		case 8600:
			a = L"大市の賑わい";
			break;
		case 8601:
			a = L"剣の遊戯";
			break;
		case 8602:
			a = L"紙一重の攻防";
			break;
		case 8603:
			a = L"走れマッハ号!";
			break;
		case 8605:
			a = L"効果音";
			break;
		case 8606:
			a = L"効果音";
			break;
		case 8607:
			a = L"星屑のカンタータ";
			break;
		case 8608:
			a = L"効果音";
			break;
		case 8609:
			a = L"Sonata No.45";
			break;
		case 8610:
			a = L"効果音";
			break;
		case 8620:
			a = L"雪ウサギを追いかけて";
			break;
		case 8621:
			a = L"Take The Windward!";
			break;
		case 8622:
			a = L"効果音";
			break;
		case 8623:
			a = L"効果音";
			break;
		case 8624:
			a = L"効果音";
			break;
		case 8625:
			a = L"効果音";
			break;
		case 8627:
			a = L"効果音";
			break;
		case 8628:
			a = L"不明";
			break;
		case 8629:
			a = L"効果音";
			break;
		case 8700:
			a = L"音楽";
			break;
		case 8703:
			a = L"音楽";
			break;
		case 8704:
			a = L"音楽";
			break;
		case 8710:
			a = L"音楽";
			break;
		case 8711:
			a = L"音楽";
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
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary, NULL);
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
		//Ys X
		if (a == L"y_act_e002.opus") {
			a = L"Operation SANDRAS";
			fff = 1;
		}
		if (a == L"y_act_e002_s1.opus") {
			a = L"Operation SANDRAS(重低音)";
			fff = 1;
		}
		if (a == L"y_b100.opus") {
			a = L"Overblaze";
			fff = 1;
		}
		if (a == L"y_b100_s1.opus") {
			a = L"Overblaze(重低音)";
			fff = 1;
		}
		if (a == L"y_b200.opus") {
			a = L"Through the North Wind";
			fff = 1;
		}
		if (a == L"y_b200_s1.opus") {
			a = L"Through the North Wind(重低音)";
			fff = 1;
		}
		if (a == L"y_b210.opus") {
			a = L"高鳴る鼓動";
			fff = 1;
		}
		if (a == L"y_b210_s1.opus") {
			a = L"高鳴る鼓動(重低音)";
			fff = 1;
		}
		if (a == L"y_b300.opus") {
			a = L"石火の如く";
			fff = 1;
		}
		if (a == L"y_b300_s1.opus") {
			a = L"石火の如く(重低音)";
			fff = 1;
		}
		if (a == L"y_b400.opus") {
			a = L"Can You Do It";
			fff = 1;
		}
		if (a == L"y_b400_s1.opus") {
			a = L"Can You Do It(重低音)";
			fff = 1;
		}
		if (a == L"y_b500.opus") {
			a = L"BERSERK -戦斧の咆哮-";
			fff = 1;
		}
		if (a == L"y_b500_s1.opus") {
			a = L"BERSERK -戦斧の咆哮-(重低音)";
			fff = 1;
		}
		if (a == L"y_b510.opus") {
			a = L"悪意の洗礼";
			fff = 1;
		}
		if (a == L"y_b510_s1.opus") {
			a = L"悪意の洗礼(重低音)";
			fff = 1;
		}
		if (a == L"y_b520.opus") {
			a = L"The Ultimate Pleasure in My Hands";
			fff = 1;
		}
		if (a == L"y_b520_s1.opus") {
			a = L"The Ultimate Pleasure in My Hands(重低音)";
			fff = 1;
		}
		if (a == L"y_b610.opus") {
			a = L"辿り着いた極光の下で";
			fff = 1;
		}
		if (a == L"y_b610_s1.opus") {
			a = L"辿り着いた極光の下で(重低音)";
			fff = 1;
		}
		if (a == L"y_b620.opus") {
			a = L"Nordics Saga -The Endless Bloody Sea-";
			fff = 1;
		}
		if (a == L"y_b620_s1.opus") {
			a = L"Nordics Saga -The Endless Bloody Sea-(重低音)";
			fff = 1;
		}
		if (a == L"y_b700.opus") {
			a = L"Ready to Fire!";
			fff = 1;
		}
		if (a == L"y_b700_s1.opus") {
			a = L"Ready to Fire!(重低音)";
			fff = 1;
		}
		if (a == L"y_b710.opus") {
			a = L"Hello, Those Who Can't Die";
			fff = 1;
		}
		if (a == L"y_b710_s1.opus") {
			a = L"Hello, Those Who Can't Die(重低音)";
			fff = 1;
		}
		if (a == L"y_b720.opus") {
			a = L"Landing Warfare";
			fff = 1;
		}
		if (a == L"y_b720_s1.opus") {
			a = L"Landing Warfare(重低音)";
			fff = 1;
		}
		if (a == L"y_bgm_none.opus") {
			a = L"無音";
			fff = 1;
		}
		if (a == L"y_d100.opus") {
			a = L"光届かぬその奥に";
			fff = 1;
		}
		if (a == L"y_d100_s1.opus") {
			a = L"光届かぬその奥に(重低音)";
			fff = 1;
		}
		if (a == L"y_d200.opus") {
			a = L"Eerie Stillness";
			fff = 1;
		}
		if (a == L"y_d200_s1.opus") {
			a = L"Eerie Stillness(重低音)";
			fff = 1;
		}
		if (a == L"y_d400.opus") {
			a = L"飽くなき渇望";
			fff = 1;
		}
		if (a == L"y_d400_s1.opus") {
			a = L"飽くなき渇望(重低音)";
			fff = 1;
		}
		if (a == L"y_d410.opus") {
			a = L"The Inner Darkness";
			fff = 1;
		}
		if (a == L"y_d410_s1.opus") {
			a = L"The Inner Darkness(重低音)";
			fff = 1;
		}
		if (a == L"y_d500.opus") {
			a = L"Hardhearted Rock Line";
			fff = 1;
		}
		if (a == L"y_d500_s1.opus") {
			a = L"Hardhearted Rock Line(重低音)";
			fff = 1;
		}
		if (a == L"y_d600.opus") {
			a = L"夢の痕跡";
			fff = 1;
		}
		if (a == L"y_d600_s1.opus") {
			a = L"夢の痕跡(重低音)";
			fff = 1;
		}
		if (a == L"y_d710.opus") {
			a = L"甲鉄戦艦ナグルファ";
			fff = 1;
		}
		if (a == L"y_d710_s1.opus") {
			a = L"甲鉄戦艦ナグルファ(重低音)";
			fff = 1;
		}
		if (a == L"y_d800.opus") {
			a = L"LILA -Innocent Wish-";
			fff = 1;
		}
		if (a == L"y_d800_s1.opus") {
			a = L"LILA -Innocent Wish-(重低音)";
			fff = 1;
		}
		if (a == L"y_d900.opus") {
			a = L"エギル海底神殿";
			fff = 1;
		}
		if (a == L"y_d900_s1.opus") {
			a = L"エギル海底神殿(重低音)";
			fff = 1;
		}
		if (a == L"y_d1010.opus") {
			a = L"The Paradise Lost of Norman";
			fff = 1;
		}
		if (a == L"y_d1010_s1.opus") {
			a = L"The Paradise Lost of Norman(重低音)";
			fff = 1;
		}
		if (a == L"y_e001.opus") {
			a = L"Yesterday's Journey, Tomorrow's Dream";
			fff = 1;
		}
		if (a == L"y_e002.opus") {
			a = L"Surging Pressure";
			fff = 1;
		}
		if (a == L"y_e003.opus") {
			a = L"Turn of the Tide";
			fff = 1;
		}
		if (a == L"y_e004.opus") {
			a = L"あの時からずっと…";
			fff = 1;
		}
		if (a == L"y_e005.opus") {
			a = L"Waver as the Wave";
			fff = 1;
		}
		if (a == L"y_e006.opus") {
			a = L"切っても切れない絆";
			fff = 1;
		}
		if (a == L"y_e007.opus") {
			a = L"灰色の深層";
			fff = 1;
		}
		if (a == L"y_e007_s1.opus") {
			a = L"灰色の深層(重低音)";
			fff = 1;
		}
		if (a == L"y_e008.opus") {
			a = L"Premonition of Turmoil";
			fff = 1;
		}
		if (a == L"y_e009.opus") {
			a = L"歪な願望";
			fff = 1;
		}
		if (a == L"y_e010.opus") {
			a = L"The Road so Far, the Future Ahead";
			fff = 1;
		}
		if (a == L"y_e011.opus") {
			a = L"Violent Warriors";
			fff = 1;
		}
		if (a == L"y_e011_s1.opus") {
			a = L"Violent Warriors(重低音)";
			fff = 1;
		}
		if (a == L"y_e012.opus") {
			a = L"手筈通りに";
			fff = 1;
		}
		if (a == L"y_e013.opus") {
			a = L"不明";
			fff = 1;
		}
		if (a == L"y_e014.opus") {
			a = L"ROLLO -Because of Its Purity-";
			fff = 1;
		}
		if (a == L"y_e015.opus") {
			a = L"Deep Unconscious";
			fff = 1;
		}
		if (a == L"y_e015_s1.opus") {
			a = L"Deep Unconscious(重低音)";
			fff = 1;
		}
		if (a == L"y_f100.opus") {
			a = L"TO BE FREE";
			fff = 1;
		}
		if (a == L"y_f100_s1.opus") {
			a = L"TO BE FREE(重低音)";
			fff = 1;
		}
		if (a == L"y_f110.opus") {
			a = L"Brother's Footsteps on the Island";
			fff = 1;
		}
		if (a == L"y_f110_s1.opus") {
			a = L"Brother's Footsteps on the Island(重低音)";
			fff = 1;
		}
		if (a == L"y_f120.opus") {
			a = L"Burn with You";
			fff = 1;
		}
		if (a == L"y_f120_s1.opus") {
			a = L"Burn with You(重低音)";
			fff = 1;
		}
		if (a == L"y_f130.opus") {
			a = L"Destined to Keep Running";
			fff = 1;
		}
		if (a == L"y_f130_s1.opus") {
			a = L"Destined to Keep Running(重低音)";
			fff = 1;
		}
		if (a == L"y_f140.opus") {
			a = L"Ride on Mana!";
			fff = 1;
		}
		if (a == L"y_f140_s1.opus") {
			a = L"Ride on Mana!(重低音)";
			fff = 1;
		}
		if (a == L"y_f150.opus") {
			a = L"Heat Hazard";
			fff = 1;
		}
		if (a == L"y_f150_s1.opus") {
			a = L"Heat Hazard(重低音)";
			fff = 1;
		}
		if (a == L"y_f160.opus") {
			a = L"瞳の中の少年剣士";
			fff = 1;
		}
		if (a == L"y_f160_s1.opus") {
			a = L"瞳の中の少年剣士(重低音)";
			fff = 1;
		}
		if (a == L"y_f200.opus") {
			a = L"錨を揚げろ！";
			fff = 1;
		}
		if (a == L"y_f200_s1.opus") {
			a = L"錨を揚げろ！(重低音)";
			fff = 1;
		}
		if (a == L"y_f210.opus") {
			a = L"悠き海に生きる者";
			fff = 1;
		}
		if (a == L"y_f210_s1.opus") {
			a = L"悠き海に生きる者(重低音)";
			fff = 1;
		}
		if (a == L"y_f220.opus") {
			a = L"コンパスは踊る";
			fff = 1;
		}
		if (a == L"y_f220_s1.opus") {
			a = L"コンパスは踊る(重低音)";
			fff = 1;
		}
		if (a == L"y_f230.opus") {
			a = L"開闢の海";
			fff = 1;
		}
		if (a == L"y_f230_s1.opus") {
			a = L"開闢の海(重低音)";
			fff = 1;
		}
		if (a == L"y_f310.opus") {
			a = L"If I Could Go Back to Those Days";
			fff = 1;
		}
		if (a == L"y_f310_s1.opus") {
			a = L"If I Could Go Back to Those Days(重低音)";
			fff = 1;
		}
		if (a == L"y_gameover.opus") {
			a = L"SO MUCH FOR TODAY (Ys X Ver.)";
			fff = 1;
		}
		if (a == L"y_op.opus") {
			a = L"Facing the Distant Horizon";
			fff = 1;
		}
		if (a == L"y_op_lp.opus") {
			a = L"Facing the Distant Horizon(lp)";
			fff = 1;
		}
		if (a == L"y_t100.opus") {
			a = L"Our Hometown";
			fff = 1;
		}
		if (a == L"y_t100_s1.opus") {
			a = L"Our Hometown(重低音)";
			fff = 1;
		}
		if (a == L"y_t200.opus") {
			a = L"根ざすべき場所";
			fff = 1;
		}
		if (a == L"y_t200_s1.opus") {
			a = L"根ざすべき場所(重低音)";
			fff = 1;
		}
		if (a == L"y_t300.opus") {
			a = L"Sometime Siesta";
			fff = 1;
		}
		if (a == L"y_t300_s1.opus") {
			a = L"Sometime Siesta(重低音)";
			fff = 1;
		}
		if (a == L"y_t301.opus") {
			a = L"Innermost Feelings";
			fff = 1;
		}
		if (a == L"y_t301_s1.opus") {
			a = L"Innermost Feelings(重低音)";
			fff = 1;
		}
		if (a == L"y_t500.opus") {
			a = L"情景に揺蕩う";
			fff = 1;
		}
		if (a == L"y_t500_s1.opus") {
			a = L"情景に揺蕩う(重低音)";
			fff = 1;
		}
		if (a == L"y_t600.opus") {
			a = L"盾の兄弟";
			fff = 1;
		}
		if (a == L"y_t600_s1.opus") {
			a = L"盾の兄弟(重低音)";
			fff = 1;
		}
		if (a == L"y_title.opus") {
			a = L"その優しさは誰のため";
			fff = 1;
		}

		if (fff == 0)
			if (a.Left(2) == "y9") {
				if (a.Mid(4, 4) = "b001") { a = "FEEL FORCE"; }
				if (a.Mid(4, 4) = "b002") { a = "TROUBLEMAKER"; }
				if (a.Mid(4, 4) = "b003") { a = "MONSTRUM SPECTRUM"; }
				if (a.Mid(4, 4) = "b004") { a = "LACRIMA CRISIS"; }
				if (a.Mid(4, 4) = "b005") { a = "WELCOME TO CHAOS"; }
				if (a.Mid(4, 4) = "b006") { a = "JUDGEMENT TIME"; }
				if (a.Mid(4, 4) = "b007") { a = "KNOCK ON NOX"; }
				if (a.Mid(4, 4) = "b008") { a = "ANIMA ERGASTULUM"; }
				if (a.Mid(4, 5) = "b010b") { a = "URBAN TERROR"; }
				if (a.Mid(4, 4) = "b010") { a = "URBAN TERROR(イントロあり)"; }
				if (a.Mid(4, 5) = "b011b") { a = "DREAMING IN THE GRIMWALD"; }
				if (a.Mid(4, 4) = "b011") { a = "DREAMING IN THE GRIMWALD(イントロあり)"; }
				if (a.Mid(4, 4) = "b012") { a = "WILD CARD"; }
				if (a.Mid(4, 5) = "b014b") { a = "FULL MOON CEREMONY"; }
				if (a.Mid(4, 4) = "b014") { a = "FULL MOON CEREMONY(イントロあり)"; }
				if (a.Mid(4, 4) = "d101") { a = "HEART BEAT SHAKER"; }
				if (a.Mid(4, 4) = "d201") { a = "CLOACA MAXIMA"; }
				if (a.Mid(4, 4) = "d301") { a = "RUIN OF DRY MOAT"; }
				if (a.Mid(4, 4) = "d401") { a = "MARIONETTE, MARIONETTE"; }
				if (a.Mid(4, 4) = "d501") { a = "THE CAVE OF GROAN"; }
				if (a.Mid(4, 4) = "d601") { a = "EVAN MACHA"; }
				if (a.Mid(4, 4) = "d701") { a = "A QUARRY RUIN"; }
				if (a.Mid(4, 4) = "d702") { a = "CROSSING A/A"; }
				if (a.Mid(4, 4) = "d801") { a = "CATCH ME IF YOU CAN"; }
				if (a.Mid(4, 4) = "d901") { a = "ALCHEMY LAB"; }
				if (a.Mid(4, 4) = "d911") { a = "STRATEGIC ZONE"; }
				if (a.Mid(4, 5) = "d1001") { a = "FORTRESS UNDERGROUND"; }
				if (a.Mid(4, 5) = "d2001") { a = "DANCE WITH TRAPS"; }
				if (a.Mid(4, 4) = "e001") { a = "APRILIS"; }
				if (a.Mid(4, 4) = "e002") { a = "TAKE IT EASY!"; }
				if (a.Mid(4, 4) = "e003") { a = "PETITE FLEUR"; }
				if (a.Mid(4, 4) = "e004") { a = "EYES ON..."; }
				if (a.Mid(4, 4) = "e005") { a = "FORGOTTEN DAYS"; }
				if (a.Mid(4, 4) = "e006") { a = "PRISON OF BALDUQ -LIVE THE FUTURE-"; }
				if (a.Mid(4, 4) = "e007") { a = "PRISON OF BALDUQ -YEARNING-"; }
				if (a.Mid(4, 4) = "e008") { a = L"IL ETAIT UNE FOIS"; }
				if (a.Mid(4, 4) = "e009") { a = "WHO KNOWS THE TRUTH?"; }
				if (a.Mid(4, 4) = "e010") { a = "DECISION"; }
				if (a.Mid(4, 4) = "e011") { a = "STAGNANT POOL"; }
				if (a.Mid(4, 4) = "e013") { a = "INQUISITION"; }
				if (a.Mid(4, 4) = "e014") { a = "SILLY MEETING"; }
				if (a.Mid(4, 4) = "e016") { a = "MONSTRUM NOX"; }
				if (a.Mid(4, 4) = "e017") { a = "CHALLENGER'S ROAD"; }
				if (a.Mid(4, 4) = "e018") { a = "RED MULETA"; }
				if (a.Mid(4, 4) = "e019") { a = "NAB THE TAIL"; }
				if (a.Mid(4, 4) = "e020") { a = "THUS SPOKE AN ALCHEMIST"; }
				if (a.Mid(4, 4) = "e023") { a = "DENOUEMENT"; }
				if (a.Mid(4, 4) = "e024") { a = "INVITATION TO THE CRIMSON NIGHT"; }
				if (a.Mid(4, 4) = "f101") { a = "NORSE WIND"; }
				if (a.Mid(4, 4) = "f201") { a = "TRANQUIL SILENCE"; }
				if (a.Mid(4, 4) = "f301") { a = "GLESSING WAY!"; }
				if (a.Mid(4, 4) = "f501") { a = "DESERT AFTER TEARS"; }
				if (a.Mid(4, 4) = "muon") { a = "無音"; }
				if (a.Mid(4, 4) = "t101") { a = "PRISONCITY"; }
				if (a.Mid(4, 4) = "t102") { a = "IN PROFILE, ON BELFRY"; }
				if (a.Mid(4, 4) = "t103") { a = "NEW LIFE"; }
				if (a.Mid(4, 4) = "t104") { a = "GRIA RECOLLECTION"; }
				if (a.Mid(4, 4) = "t201") { a = "BAR \"DANDELION\""; }
				if (a.Mid(4, 4) = "t301") { a = "AMBIGUOUS TERRITORY"; }
				if (a.Mid(4, 4) = "t402") { a = "WALTZ FOR GRACE"; }
				if (a.Mid(4, 4) = "t501") { a = "HEAT AND SPLENDOR"; }
				if (a.Mid(4, 4) = "t901") { a = "ONLY THE CORPSE GOES OUT"; }
				if (a.Mid(4, 4) = "t902") { a = "A GOLDEN KEY CAN OPEN ANY DOOR"; }
				if (a.Mid(4, 4) = "tbox") { a = "TREASURE BOX -Ys IX-"; }
			}
			else {
				switch (_ttoi(a.Mid(2, 5))) {
				case 81004:
					a = "罪と罰と偽りと";
					break;
				case 81005:
					a = "昏き鐘の残響";
					break;
				case 81006:
					a = "Right on the Mark";
					break;
				case 81007:
					a = "悪夢ふたたび";
					break;
				case 81008:
					a = "Crossbell Nostalgia";
					break;
				case 81009:
					a = "創まりの円庭";
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
					a = "さざめきの途路";
					break;
				case 81016:
					a = "蒼の大地に生きる者";
					break;
				case 81017:
					a = "黎明の鐘";
					break;
				case 81018:
					a = "レメディファンタジア -仲間とともに-";
					break;
				case 81019:
					a = "Slight Suspicion";
					break;
				case 81020:
					a = "Maliciousness in the Mirror";
					break;
				case 81021:
					a = "暗澹たる世界";
					break;
				case 81022:
					a = "ひとときの温もり";
					break;
				case 81023:
					a = "今、創まりのとき";
					break;
				case 81024:
					a = "KERAUNOS -Fear and Hatred-";
					break;
				case 81025:
					a = "亡失われた魂";
					break;
				case 81026:
					a = "穏やかな時間";
					break;
				case 81027:
					break;
				case 81028:
					a = "運命という名の歯車";
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
					a = "鉱山町マインツ -創Ver.-";
					break;
				case 81316:
					a = "木霊の道 -創Ver.-";
					break;
				case 81317:
					a = "Raindrops with the Wind";
					break;
				case 81319:
					a = "陽溜まりにただいまを";
					break;
				case 81320:
					a = "Wind-Up Yesterday!";
					break;
				case 81321:
					a = "零の邂逅";
					break;
				case 81322:
					a = "影の見えざる手";
					break;
				case 81950:
					break;
				case 81951:
					break;
				case 81952:
					break;
				case 81953:
					break;
				case 81954:
					break;
				case 81955:
					break;
				case 81956:
					break;
				case 81957:
					break;
				case 81958:
					break;
				case 81961:
					break;
				case 81962:
					break;
				case 81963:
					break;
				case 81964:
					break;
				case 81965:
					break;
				case 81966:
					break;
				case 81967:
					break;
				case 81968:
					break;
				case 81969:
					break;
				case 82065:
					a = "鋼鉄牙城";
					break;
				case 82113:
					a = "Zero Break Battle";
					break;
				case 82114:
					a = "Stake Everything Strategy";
					break;
				case 82123:
					break;
				case 82124:
					a = "POM's Paradise";
					break;
				case 82125:
					a = "波間に弾む心";
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
					a = "流麗闘冴";
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
					a = "ひとかけらの光明";
					break;
				case 82143:
					a = "反攻の烽火";
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
					a = "今宵は宴と参りましょう";
					break;
				case 82159:
					a = "Flash Your Fighting Spirit";
					break;
				case 82161:
					a = "鈍色に這う";
					break;
				case 82163:
					a = "Pyro Labyrinth";
					break;
				case 82164:
					a = "優しさを未来に託して";
					break;
				case 82166:
					a = "高らかに、誇らしく";
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
					a = "このあと美味しくいただきました";
					break;
				case 82186:
					a = "Emergency Order";
					break;
				case 82188:
					a = "激烈! 撃滅! ミシュナイダー!!";
					break;
				case 82189:
					a = "Life Goes On";
					break;
				default:
					if (a == L"ed8_inf_ex.opus") {
						a = "夢幻の彼方へ";
					}
				}
				switch (_ttoi(a.Mid(2, 4))) {
				case 8001:
					a = "特科クラス《VII組》";
					break;
				case 8002:
					a = "スタートライン";
					break;
				case 8003:
					a = "不明";
					break;
				case 8004:
					a = "Youthful Victory";
					break;
				case 8006:
					a = "ただひたすらに、前へ";
					break;
				case 8007:
					a = "縁 -つなぐもの-";
					break;
				case 8102:
					a = "翡翠の公都バリアハート";;
					break;
				case 8104:
					a = "黒銀の鋼都ルーレ";
					break;
				case 8150:
					a = "下校途中にパンケーキ";
					break;
				case 8151:
					a = "可能性は無限大";
					break;
				case 8152:
					a = "夜のしじまに";
					break;
				case 8153:
					a = "夕景";
					break;
				case 8154:
					a = "新しい朝";
					break;
				case 8155:
					a = "束の間の里帰り";
					break;
				case 8156:
					a = "白亜の旧都セントアーク";
					break;
				case 8157:
					a = "紡績町パルム";
					break;
				case 8158:
					a = "籠の中のクロスベル";
					break;
				case 8159:
					a = "今、成すべきこと";
					break;
				case 8160:
					a = "歓楽都市ラクウェル";
					break;
				case 8161:
					a = "静かなる駆け引き";
					break;
				case 8162:
					a = "赫奕たるヘイムダル";
					break;
				case 8163:
					a = "紺碧の海都オルディス";
					break;
				case 8164:
					a = "最前線都市";
					break;
				case 8165:
					a = "Base Camp";
					break;
				case 8166:
					a = "精強なる兵たち";
					break;
				case 8168:
					a = "不明";
					break;
				case 8170:
					a = "隠れ里エリン";
					break;
				case 8171:
					a = "潜入調査";
					break;
				case 8172:
					a = "昏冥の中で";
					break;
				case 8173:
					a = "紅き閃影 -光まとう翼-";
					break;
				case 8174:
					a = "聖ウルスラ医科大学 -閃Ver.-";
					break;
				case 8175:
					a = "一抹の不安、一縷の望み";
					break;
				case 8176:
					a = "Lyrical Amber";
					break;
				case 8177:
					a = "水面を渡る風";
					break;
				case 8250:
					a = "流れる雲の彼方に";
					break;
				case 8251:
					a = "静寂の小路";
					break;
				case 8252:
					a = "崖谷の狭間";
					break;
				case 8253:
					a = "Weathering Road";
					break;
				case 8260:
					a = "彼の地へ向かって";
					break;
				case 8261:
					a = "終焉の途へ";
					break;
				case 8262:
					a = "全てを識るもの -閃Ver.-";
					break;
				case 8263:
					a = "たそがれ緑道";
					break;
				case 8311:
					a = "不明";
					break;
				case 8350:
					a = "アインヘル小要塞";
					break;
				case 8351:
					a = "伝承の裏で";
					break;
				case 8352:
					a = "Unplanned Residue";
					break;
				case 8353:
					a = "忘れられし幻夢の狭間 -閃Ver.-";
					break;
				case 8354:
					a = "幽世の気配";
					break;
				case 8355:
					a = "solid as the Rock of JUNO";
					break;
				case 8356:
					a = "地下に巣喰う";
					break;
				case 8359:
					a = "Spiral of Erebos";
					break;
				case 8360:
					a = "鋼の障壁";
					break;
				case 8363:
					a = "Break In";
					break;
				case 8365:
					a = "サングラール迷宮";
					break;
				case 8366:
					a = "静けき森の魔女";
					break;
				case 8367:
					a = "Mystic Core -閃Ver.-";
					break;
				case 8368:
					a = "斉いし舞台";
					break;
				case 8369:
					a = "シンクロニシティ #23";
					break;
				case 8371:
					a = "世界の命運を賭けて";
					break;
				case 8372:
					a = "The End of -SAGA-";
					break;
				case 8429:
					a = "不明";
					break;
				case 8450:
					a = "Brave Steel";
					break;
				case 8451:
					a = "Toughness!!";
					break;
				case 8452:
					a = "剣戟怒涛";
					break;
				case 8453:
					a = "Proud Grudge";
					break;
				case 8454:
					a = "チープ・トラップ";
					break;
				case 8455:
					a = "STEP AHEAD";
					break;
				case 8456:
					a = "劣勢を挽回せよ！";
					break;
				case 8457:
					a = "Abrupt Visitor";
					break;
				case 8458:
					a = "行き着く先 -Opening Size-";
					break;
				case 8460:
					a = "Lift-off!";
					break;
				case 8461:
					a = "Accursed Tycoon";
					break;
				case 8464:
					a = "One-Way to the Netherworld";
					break;
				case 8465:
					a = "不明";
					break;
				case 8466:
					a = "Erosion of Madness";
					break;
				case 8467:
					a = "DOOMSDAY TRANCE";
					break;
				case 8468:
					a = "不明";
					break;
				case 8472:
					a = "Malicious Fiend";
					break;
				case 8473:
					a = "Unlikely Combination";
					break;
				case 8474:
					a = "Robust One";
					break;
				case 8475:
					a = "古の盟約";
					break;
				case 8476:
					a = "七の相克 -EXCELLION KRIEG-";
					break;
				case 8477:
					a = "Burning Throb";
					break;
				case 8478:
					a = "Neck or Nothing";
					break;
				case 8479:
					a = "Majestic Roar";
					break;
				case 8480:
					a = "With Our Own Hands!!";
					break;
				case 8500:
					a = "授業は合同で";
					break;
				case 8501:
					a = "Power or Technique";
					break;
				case 8502:
					a = "Briefing Time";
					break;
				case 8503:
					a = "第II分校の日常";
					break;
				case 8504:
					a = "充実したひととき";
					break;
				case 8505:
					a = "異端の研究者";
					break;
				case 8506:
					a = "君に伝えたいこと";
					break;
				case 8507:
					a = "不明";
					break;
				case 8508:
					a = "不明";
					break;
				case 8509:
					a = "張り詰めた思惑";
					break;
				case 8510:
					a = "混迷の対立";
					break;
				case 8511:
					a = "急転直下";
					break;
				case 8512:
					a = "蠢く陰謀";
					break;
				case 8513:
					a = "託されたもの";
					break;
				case 8514:
					a = "羅刹の薫陶";
					break;
				case 8515:
					a = "ハーメル -遺されたもの-";
					break;
				case 8516:
					a = "Welcome Back! アーベントタイム(ラジオ)";
					break;
				case 8517:
					a = "夏至祭";
					break;
				case 8519:
					a = "夏至祭";
					break;
				case 8520:
					a = "翡翠庭園";
					break;
				case 8521:
					a = "初めての円舞曲";
					break;
				case 8522:
					a = "真打ち登場！";
					break;
				case 8524:
					a = "Tragedy";
					break;
				case 8528:
					a = "僅かな希望の先に";
					break;
				case 8530:
					a = "帰路へ";
					break;
				case 8532:
					a = "Roots of Scar";
					break;
				case 8534:
					a = "想い千里を走り";
					break;
				case 8536:
					a = "光射す空の下で";
					break;
				case 8539:
					a = "不明";
					break;
				case 8541:
					if (b == L"b")
						a = "空を見上げて -Eliot Ver.-";
					else
						a = "空を見上げて -Eliot Ver.-";
					break;
				case 8542:
					a = "不明";
					break;
				case 8543:
					a = "不明";
					break;
				case 8544:
					a = "Little Rain";
					break;
				case 8545:
					a = "暗雲";
					break;
				case 8546:
					a = "鐘、鳴り響く時";
					break;
				case 8547:
					a = "巨イナル黄昏";
					break;
				case 8548:
					a = "あの日の約束";
					break;
				case 8551:
					a = "不明";
					break;
				case 8553:
					a = "Sensitive Talk";
					break;
				case 8554:
					a = "哀花";
					break;
				case 8555:
					a = "Feel at Home";
					break;
				case 8556:
					a = "幾千万の夜を越えて";
					break;
				case 8557:
					a = "不明";
					break;
				case 8558:
					a = "不明";
					break;
				case 8559:
					a = "優しき微睡み";
					break;
				case 8560:
					a = "最悪の最善手";
					break;
				case 8562:
					a = "黒の真実";
					break;
				case 8563:
					a = "いつでもそばに";
					break;
				case 8564:
					a = "その温もりは小さいけれど。";
					break;
				case 8566:
					a = "それでも前へ";
					break;
				case 8570:
					a = "想いひとつに";
					break;
				case 8571:
					a = "千年要塞";
					break;
				case 8572:
					a = "不明";
					break;
				case 8573:
					a = "せめてこの夜に誓って";
					break;
				case 8574:
					a = "Constraint";
					break;
				case 8575:
					a = "過ぎ去りし日々";
					break;
				case 8576:
					a = "不明";
					break;
				case 8577:
					a = "それぞれの覚悟";
					break;
				case 8578:
					a = "無明の闇の中で";
					break;
				case 8579:
					a = "変わる世界 -闇の底から-";
					break;
				case 8600:
					a = "不明";
					break;
				case 8601:
					a = "ゲートイン";
					break;
				case 8602:
					a = "不明(空の軌跡)";
					break;
				case 8603:
					a = "女神はいつも見ています";
					break;
				case 8604:
					a = "不明(空の軌跡)";
					break;
				case 8605:
					a = "不明";
					break;
				case 8606:
					a = "不明";
					break;
				case 8608:
					a = "不明";
					break;
				case 8610:
					a = "不明";
					break;
				case 8611:
					a = "不明";
					break;
				case 8612:
					a = "不明";
					break;
				case 8613:
					a = "不明";
					break;
				case 8614:
					a = "不明";
					break;
				case 8616:
					a = "不明";
					break;
				case 8617:
					a = "不明";
					break;
				case 8618:
					a = "不明";
					break;
				case 8619:
					a = "不明";
					break;
				case 8620:
					a = "不明";
					break;
				case 8621:
					a = "不明";
					break;
				case 8702:
					a = "Master's Vertex";
					break;
				case 8706:
					a = "Endure Grief";
					break;
				case 8707:
					a = "Intuition and Insight";
					break;
				case 8708:
					a = "Bold Assailants";
					break;
				case 8709:
					a = "Seductive Shudder";
					break;
				case 8711:
					a = "Blue Stardust";
					break;
				case 8713:
					a = "Pleasure Smile";
					break;
				case 8714:
					a = "巨竜目覚める";
					break;
				case 8715:
					a = "未来へ。";
					break;
				case 8716:
					a = "明日への軌跡 -Instrumental Ver.-";
					break;
				case 8717:
					a = "Deep Carnival";
					break;
				case 8718:
					a = "不明";
					break;
				case 8719:
					a = "Chain Chain Chain!";
					break;
				case 8720:
					a = "明日への軌跡";
					break;
				case 8721:
					a = "愛の詩(歌)";
					break;
				case 8722:
					a = "Celestial Coalescence";
					break;
				case 8800:
					a = "Vantage Masters";
					break;
				case 8801:
					a = "Concept H.M.I.";
					break;
				case 8802:
					a = "風よりも駿く";
					break;
				case 8803:
					a = "Brilliant Escape";
					break;
				case 8810:
					a = "不明";
					break;
				case 8811:
					a = "不明";
					break;
				case 8812:
					a = "不明";
					break;
				case 8910:
					a = "不明";
					break;
				case 8911:
					a = "不明";
					break;
				case 8912:
					a = "不明";
					break;
				case 8913:
					a = "不明";
					break;
				case 8916:
					a = "不明";
					break;
				case 8917:
					a = "不明";
					break;
				case 8918:
					a = "不明";
					break;
				case 8919:
					a = "不明";
					break;
				case 8920:
					a = "不明";
					break;
				case 8921:
					a = "不明";
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
		fi.Open(filen, CFile::modeRead);
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
		for (int l = 0; l < 240; l++) {
			if (by[l] == 'u' && by[l + 1] == 's' && by[l + 2] == 'H' && by[l + 3] == 'e' && by[l + 4] == 'a' && by[l + 5] == 'd') {
				struct abc
				{
					union {
						int a;
						char b[4];
					};
				};

				abc abc_;
				abc_.b[0] = abc_.b[1] = abc_.b[2] = abc_.b[3] = 0;
				abc_.b[0] = by[l + 10];
				abc_.b[1] = by[l + 11];
				wavbit = abc_.a;

			}
		}
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

		ogg_int64_t totalSamples = op_pcm_total(m_pOpusFile, -1);
		op_raw_seek(m_pOpusFile, 1);
		if (adbuf2) {
			free(adbuf2); adbuf2 = NULL;
		}
		adbuf2 = (char*)calloc((size_t)totalSamples * 6 * 2, 1);

		loop1 = l1;
		loop2 = (l2 - l1);
		a = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		if (a.Left(3).MakeLower() == L"ed7") {
			short b = (short)_ttoi(a.Mid(3, 3));
			Sleep(10);
			CFile fzero;
			CString fil = filen.Left(filen.ReverseFind(L'\\')) + L"\\..\\..\\data\\text\\t_bgm._dt";
			if (fzero.Open(fil, CFile::modeRead | CFile::shareDenyNone, NULL)) {
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
					loop1 = (int)(zerobuf.start * 1.08843537414966); //44.1kHz -> 48kHz
					loop2 = (int)(zerobuf.end * 1.08843537414966); //44.1kHz -> 48kHz
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
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary, NULL);
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
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary, NULL);
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
		adpcmf.Open(fn, CFile::modeRead | CFile::typeBinary | CFile::shareDenyNone, NULL);

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
		// dataの次のsmplがほしいので、data分最後～wavファイル最後までの間で検索(時短)
		adpcmf.SeekToBegin();
		adpcmf.Seek(aa.seekpoint, 1); // RIFF
		adpcmf.Seek(st2 + cnt + 4, 1); // dataの最後
		adpcmf.Read(bbuf, aa.datasize - cnt); //dataの最後～wavの最後まで読む
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
			loop1 = (int)(BYTE)bbuf[st + 0x30] + (int)(BYTE)bbuf[st + 0x31] * 256 + (int)(BYTE)bbuf[st + 0x32] * 65536 + (int)(BYTE)bbuf[st + 0x33] * 256 * 65536; //loop1 = (int)(loop1 * (48.0f / 44.1f)); //ループ開始ポイント
			loop2 = (int)(BYTE)bbuf[st + 0x34] + (int)(BYTE)bbuf[st + 0x35] * 256 + (int)(BYTE)bbuf[st + 0x36] * 65536 + (int)(BYTE)bbuf[st + 0x37] * 256 * 65536; loop2 -= loop1;//ループ長
		}

		data_size = oggsize = cnt;
		adpcmf.Seek(aa.seekpoint + st2 + 4, CFile::begin);
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		og->m_time.SetSelection(loop1, loop2 + loop1);
		lenl = 0;
		adbuf2 = (char*)calloc(cnt * 2 * 2, 1);
		adpcmf.Read(adbuf2, dl * 2);
		dwDataLen += dl;
		cnt -= dwDataLen * 2;
		wavbit = 48000;
		free(bbuf);
		wavwait = 1;
		for (; cnt > 0;) {
			adpcmf.Read(adbuf2 + (int)dwDataLen, dl);
			dwDataLen += dl; cnt -= dl;
			if (thend1 == TRUE) { thend = 1;  return; }
			og->m_time.SetSelection(loop1, loop2 + loop1);
		}
		adpcmf.Close();
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
		adpcmf.Open(_T("bgm.arc"), CFile::modeRead | CFile::typeBinary, NULL);
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
		if (adpcmf.Open(filen.Left(filen.ReverseFind('.')) + _T(".pos"), CFile::modeRead | CFile::typeBinary, NULL) == 0) {
			loop1 = loop2 = 0;
		}
		else {
			adpcmf.Read(&loop1, 4);
			adpcmf.Read(&loop2, 4); loop2 = loop2 - loop1;
			adpcmf.Close();
		}
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary, NULL);
		adpcmf.Read(bbuf, 0x80);
		wavbit = (UINT)(int)bbuf[0x18] + (int)(BYTE)bbuf[0x19] * 256;
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
		if (adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary, NULL) == 0) {
			if (filen.Left(8) == "ED3119DA") { _chdir(".."); filen = "ED3_DT09.DAT"; ff = TRUE; }
			if (filen.Left(8) == "ED3603DA") { _chdir(".."); filen = "ED3_DT10.DAT"; ff = TRUE; }
			adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary, NULL);
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
		wavbit = (UINT)(int)bbuf[0x18] + (int)(BYTE)bbuf[0x19] * 256;
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
			if (wavbit == 22050) { si /= 2; }
		}
		if (filen.Left(8) == "ED3940DA" && fnn.Left(2) == "も") {
			jk = 14376600 * 2 * 2;
			si = 19668600 * 2 * 2;
			if (wavbit == 22050) { jk /= 2; si /= 2; }
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
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary, NULL);
		adpcmf.Read(bbuf, 0x80);
		int si, jk;
		wavbit = (UINT)(int)bbuf[0x18] + (int)(BYTE)bbuf[0x19] * 256;
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
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary, NULL);
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
		adpcmf.Open(_T("wav.dat"), CFile::modeRead | CFile::typeBinary, NULL);
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
				adpcmf.Open(_T("Plugins\\data.dat"), CFile::modeRead | CFile::typeBinary, NULL);
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
				adpcmf.Open(_T("Plugins\\data.dat"), CFile::modeRead | CFile::typeBinary, NULL);
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
		wavch=si1.dwChannels;
		wavbit=si1.dwSamplesPerSec;
		loop2=(int)(((float)loop2)/(44100.0f/((float)((wavch==2)?wavbit:(wavbit/2)))));
		data_size=oggsize=loop2*4;
		loop3=loop2;loop2=0;
		adbuf2=(char*)malloc(data_size+44100*10);
		if(adbuf2==0){wavwait=1;thend=1; fnn="メモリの確保に失敗しました。";return;}
		og->m_time.SetRange(0,(data_size)/4,TRUE);
		lenl= 0;
		if(wav)free(wav);
		wav_start();
		Render();
		}else{wavwait=1;thend=1; fnn="ファイルが開けませんでした。";return;}
		*/
	}
	else if (mode == -11 || (mode == -14 && filen.Right(3) == "mp3") || mode == -15) {
		if (mode == -14 && (filen == "49music.mp3" || filen == "50music.mp3" || filen == "51music.mp3")) _chdir("..\\Cmusic");
		int san2 = 0;
		if (filen == "041music.mp3" && fnn.Find(_T("日本語")) > 0) san2 = 1;
		if (filen == "041music.mp3" && fnn.Find(_T("中国")) > 0) san2 = 2;
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
		wavbit = si.dwSamplesPerSec;
		loop2 = loop2 / (44100 / wavbit);
		data_size = oggsize = loop2 * 4;
		adbuf2 = (char*)calloc(data_size * 2 + 44100 * 30, 1);
		if (san2 == 1) loop2 = 225 * 44100;
		if (san2 == 2) loop2 = 247 * 44100;
		loop3 = loop2;
		if (san2)data_size = oggsize = loop2 * 4 + 44100 * 7;
		if (mode == -14 && filen == "42music.mp3")	loop2 = 0;
		if (adbuf2 == 0) { wavwait = 1; thend = 1; fnn = "メモリの確保に失敗しました。"; return; }
		og->m_time.SetRange(0, (data_size) / 4, TRUE);
		lenl = 0;
		if (wav)free(wav);
		wav_start();
		Render((san2 == 2) ? data_size - 44100 * 21 * 4 : 0);
	}
	else if (mode == -13) {
		lenl = 0;
		CFile adpcmf;
		adpcmf.Open(_T("music.pak"), CFile::modeRead | CFile::typeBinary, NULL);
		adpcmf.SeekToEnd();
		adpcmf.Seek(-0x3d - 12, CFile::end);
		//		adpcmf.Open("C:\\FALCOM\\Arcturus\\music.pak",CFile::modeRead|CFile::typeBinary,NULL);
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
		adpcmf.Read(&wavbit, 2);
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

int readadpcm2(char* bw, int cnt);
int seekadpcm(int pos);

BOOL playwavadpcm(BYTE* bw, int old, int l1, int l2)
{
	//	playb+=(l1+l2)/4;
	int rrr = readadpcm2((char*)bw + old, l1);
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
			poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = loop1 * 4; poss6 = 0;
			seekadpcm(loop1);
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			readadpcm2((char*)bw + old + rrr, (int)l1 - rrr);
		}
	}
	if (l2) {
		rrr = readadpcm2((char*)bw, l2);
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
				poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = loop1 * 4; poss6 = 0;
				seekadpcm(loop1);
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				readadpcm2((char*)bw + rrr, (int)l2 - rrr);
			}
		}
	}
	return FALSE;
}

std::vector<uint8_t> outputRawBytesData;
int readtempo(BYTE* data, int len)
{
	//テンポ
	m_bufwav3_1.clear();
	m_bufwav3_1.resize(len);
	memcpy(m_bufwav3_1.data(), (data), len);
	float te = (float)tempo;
	if (te >= 200.0f) {
		te -= 100.0f;
	}
	else {
		te = te / 3.0f + 33.3f;
	}
	te = 100.0f / te;
	ProcessAudioWithRubberBand(te);
	ConvertFloatToRawBytes(m_convertedPcmFloatData, wavsam, wavch, outputRawBytesData);
	return outputRawBytesData.size();
}
using namespace std;
void equaliser(void* data, int len, BOOL reset = FALSE);
int readadpcm2(char* bw, int cnt)
{
	int r = cnt, rr = cnt;
	int cnt2;
	if (rr == 0)return 0;

	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	if (poss4 <= cnt) {
		while (true) {
			int f = 0;
			if (adbuf2 == NULL) return 0;
			if (rrr == 1)
				memcpy((void*)bufkpi, (void*)(adbuf2 + lenl), cnt);
			else
			{
				ZeroMemory(bufkpi, cnt);
				muon--;
			}

			if (playb > (data_size + 100000) / 4 && muon != 0) {
				rrr = 0;
			}
			if (muon <= 0) {
				endf = 1;
				return 0;
			}

			int len2 = readtempo(bufkpi, cnt);
			lenl += cnt;

			if (len2 > 0) {
				// 書き込み
				if (poss2 + len2 > max_buffer_size) {
					int first = max_buffer_size - poss2;
					memcpy(bufkpi3 + poss2, outputRawBytesData.data(), first);
					memcpy(bufkpi3, outputRawBytesData.data() + first, len2 - first);
					poss2 = (poss2 + len2) % max_buffer_size;
				}
				else {
					memcpy(bufkpi3 + poss2, outputRawBytesData.data(), len2);
					poss2 += len2;
				}
				poss4 += len2;
			}
			playb += (len2 / 4);
			if (poss4 > cnt) break;
		}
	}

	int cnt0 = cnt;
	if (loop1 * 4 + loop2 * 4 < poss5 + cnt0 && endf == 0) {
		cnt0 = (loop1 * 4 + loop2 * 4) - poss5;
	}
	cnt2 = cnt0;

	if (cnt2 > 0) {
		if (poss3 + cnt0 > max_buffer_size) {
			int first = max_buffer_size - poss3;
			memcpy(bw, bufkpi3 + poss3, first);
			memcpy(bw + first, bufkpi3, cnt0 - first);
			poss3 = (poss3 + cnt0) % max_buffer_size;
		}
		else {
			memcpy(bw, bufkpi3 + poss3, cnt0);
			poss3 += cnt0;
		}
		poss4 -= cnt0;
	}

	equaliser(bw, cnt2, reset);
	reset = FALSE;

	short* b, c;
	b = (short*)bw;
	fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
	//fadeを三乗して計算密度を変更
	for (int i = 0; i < cnt0 / 2; i++) { c = b[i]; c = (short)(((float)c) * fade * fade); b[i] = c; }

	if ((UINT)wl < (UINT)0x7fff0000) {
		//				if (cc1 == 1)	cc.Write(bw, cnt);
		if (cc1 == 1)	cc.Write(bw, cnt0);
		//				if (cc1 == 1)	cc.Write(bufkpi, r);
		wl += cnt0;
	}

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
	lenl = pos * 4;
	playb = pos;
	poss5 = lenl;
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
			sample1L = (int)((float)sample1L * (float)savedata.kakuVal / 100.0f);
			sample2L = (int)((float)sample2L * (float)savedata.kakuVal / 100.0f);
			sample1R = (int)((float)sample1R * (float)savedata.kakuVal / 100.0f);
			sample2R = (int)((float)sample2R * (float)savedata.kakuVal / 100.0f);
			W16(bw2, sample2L);      bw2 += 2;
			W16(bw2, sample2R);      bw2 += 2;
			W16(bw2, sample1L);      bw2 += 2;
			W16(bw2, sample1R);      bw2 += 2;
			lbuf -= 14;
		}
		for (int k = ak; k < (cnt - 14); k++) {
			process_nibble(*bbuf1 >> 4, &ideltaL, &sample1L, &sample2L, &coeffL);
			sample1L = (int)((float)sample1L * (float)savedata.kakuVal / 100.0f);
			W16(bw2, sample1L);	bw2 += 2;
			process_nibble(*bbuf1++ & 0x0F, &ideltaR, &sample1R, &sample2R, &coeffR);
			sample1R = (int)((float)sample1R * (float)savedata.kakuVal / 100.0f);
			W16(bw2, sample1R);	bw2 += 2;	lbuf--;
			c += 4; if (c >= (int)(WAVDALen / OUTPUT_BUFFER_NUM) * 8)	wavwait = 1;
			if (thend1 == TRUE) return 1;
		}
		ak = 0;
	}
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
			sample1L = (int)((float)sample1L * (float)savedata.kakuVal / 100.0f);
			sample2L = (int)((float)sample2L * (float)savedata.kakuVal / 100.0f);
			sample1R = (int)((float)sample1R * (float)savedata.kakuVal / 100.0f);
			sample2R = (int)((float)sample2R * (float)savedata.kakuVal / 100.0f);
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
				sample1L = (int)((float)sample1L * (float)savedata.kakuVal / 100.0f);
				W16(bw2, sample1L);	bw2 += 2;
			}
			process_nibble(*bbuf1++ & 0x0F, &ideltaR, &sample1R, &sample2R, &coeffR);
			if (o == 0) {
				W16(bw2, 0);	bw2 += 2;
			}
			else {
				sample1R = (int)((float)sample1R * (float)savedata.kakuVal / 100.0f);
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
			sample1L = (int)((float)sample1L * (float)savedata.kakuVal / 100.0f);
			sample2L = (int)((float)sample2L * (float)savedata.kakuVal / 100.0f);
			sample1R = (int)((float)sample1R * (float)savedata.kakuVal / 100.0f);
			sample2R = (int)((float)sample2R * (float)savedata.kakuVal / 100.0f);
			W16(bw2, sample2L);      bw2 += 2;
			W16(bw2, sample2R);      bw2 += 2;
			W16(bw2, sample1L);      bw2 += 2;
			W16(bw2, sample1R);      bw2 += 2;
			len -= 14; lbuf -= 14;
		}
		for (int k = ak; k < (cnt - 14); k++) {
			process_nibble(*bbuf1 >> 4, &ideltaL, &sample1L, &sample2L, &coeffL);
			sample1L = (int)((float)sample1L * (float)savedata.kakuVal / 100.0f);
			W16(bw2, sample1L);	bw2 += 2;
			process_nibble(*bbuf1++ & 0x0F, &ideltaR, &sample1R, &sample2R, &coeffR);
			sample1R = (int)((float)sample1R * (float)savedata.kakuVal / 100.0f);
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
	if (og->mod == NULL && kpidec == NULL) return 0;
	playb += (l1 + l2) / (wavsam / 4);
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
			else
				kpidec->Seek(0, 1);
			poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
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
				else
					kpidec->Seek(0, 1);
				poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				readkpi(bw + rrr, (int)l2 - rrr);
			}
		}
	}
	return l1 + l2;
}



int readkpi(BYTE* bw, int cnt)
{
	if (cnt == 0) return 0;
	_set_se_translator(trans_func);
	DWORD cnt1 = (kvver == 2) ? og->sikpi.dwUnitRender * 2 : 4096, cnt2 = (DWORD)cnt, cnt4 = 0; if (cnt1 == 0) cnt1 = 1024;
	DWORD r = cnt;
	try {
		int len3 = 0, len4 = 0;
		int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
		if (poss4 <= cnt) {
			r = 0;
			while (true) {
				for (;;) {
					if (cnt2 <= cnt3) { r = 1; break; }
					if (kvver == 2)
						if (IsBadCodePtr((FARPROC)og->mod->Render) == 0)
							r = og->mod->Render(og->kmp1, (BYTE*)bufkpi + cnt3, cnt1);
					if (kvver == 5) {
						r = kpidec->Render((BYTE*)bufkpi + (int)((float)cnt3 / (2.0 * wavch * (wavsam / 16.0))), (int)(cnt / (2.0 * wavch * (wavsam / 16.0))));
						r = (DWORD)(r * (2.0 * wavch * (wavsam / 16.0)));
					}
					if (r == 0) break;
					//		mod->Render(kmp,(BYTE*)bw,cnt);
					cnt3 += r;
				}
				int len2 = readtempo(bufkpi, cnt);

				if (len2 > 0) {
					// 書き込み
					if (poss2 + len2 > max_buffer_size) {
						int first = max_buffer_size - poss2;
						memcpy(bufkpi3 + poss2, outputRawBytesData.data(), first);
						memcpy(bufkpi3, outputRawBytesData.data() + first, len2 - first);
						poss2 = (poss2 + len2) % max_buffer_size;
					}
					else {
						memcpy(bufkpi3 + poss2, outputRawBytesData.data(), len2);
						poss2 += len2;
					}
					poss4 += len2;
					if (cnt3 < cnt) return cnt4;
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
			if (poss3 + to_read > max_buffer_size) {
				int first = max_buffer_size - poss3;
				memcpy(bw, bufkpi3 + poss3, first);
				memcpy(bw + first, bufkpi3, to_read - first);
				poss3 = (poss3 + to_read) % max_buffer_size;
			}
			else {
				memcpy(bw, bufkpi3 + poss3, to_read);
				poss3 += to_read;
			}
			poss4 -= to_read;
		}

		equaliser(bw, cnt, reset);
		reset = FALSE;

		cnt4 = cnt3;
		if (r == 0) cnt = 0;
		__int64 bfc, bc2;
		bfc = 0;
		if (wavsam == 24) {
			Int24* bf1; bf1 = (Int24*)bw;
			bc2 = (int)(Int24)bf1[0] / 256;
			for (int i = 0; i < cnt / 2; i++) {
				bfc += (__int64)(int)(Int24)bf1[i] / 256;
			}
			if (cnt)
				bfc /= (cnt / 2);
			if ((short)bc2 >= (short)bfc - 10 && (short)bc2 <= (short)bfc + 10) bufzero++; else bufzero = 0;
		}
		else {
			unsigned short* bf1; bf1 = (unsigned short*)bw;
			bc2 = (short)bf1[0];
			for (int i = 0; i < cnt / 2; i++) {
				bfc += (__int64)(short)bf1[i];
			}
			if (cnt)
				bfc /= (cnt / 2);
			if ((short)bc2 >= (short)bfc - 10 && (short)bc2 <= (short)bfc + 10) bufzero++; else bufzero = 0;
		}
		int looping = loop2 / 100000;
		if (looping < 20) looping = 20;
		if (looping > 80) looping = 80;

		short* b, c;
		b = (short*)bw;
		Int24* b24c;
		b24c = (Int24*)bw;
		//	CString sss=og->kpi;
		CString sss;
		sss = filen.Right(filen.GetLength() - filen.ReverseFind('.') - 1);
		sss.MakeLower();
		if (wavsam == 24) {
			for (int i = 0; i < cnt / 3; i++) {
				int c4 = b24c[i];
				c4 = (int)((float)c4 * ((float)savedata.kakuVal / 100.0f));
				b24c[i] = c4;
			}
		}
		else {
			for (int i = 0; i < cnt / 2; i++) {
				int c = (int)b[i];
				c = (int)((float)c * ((float)savedata.kakuVal / 100.0f));
				b[i] = (short)c;
			}
		}
		if (sss == "spc" || sss.Left(3) == "hes") {
			if (savedata.spc != 1)
				if (wavsam == 24) {
					for (int i = 0; i < cnt / 3; i++) {
						int c4 = b24c[i];
						if (savedata.spc == 2)	c4 = (int)((float)c4 * 2.0f);
						else if (savedata.spc == 4) c4 = (int)((float)c4 * 3.0f);
						else if (savedata.spc == 8) c4 = (int)((float)c4 * 4.0f);
						else if (savedata.spc == 16) c4 = (int)((float)c4 * 5.0f);
						if (c4 > 8388607)c4 = 8388607;
						if (c4 < -8388608)c4 = -8388608;
						b24c[i] = c4;
					}
				}
				else {
					for (int i = 0; i < cnt / 2; i++) {
						int c = (int)b[i];
						if (savedata.spc == 2)	c = (int)((float)b[i] * 2.0f);
						else if (savedata.spc == 4) c = (int)((float)b[i] * 3.0f);
						else if (savedata.spc == 8) c = (int)((float)b[i] * 4.0f);
						else if (savedata.spc == 16) c = (int)((float)b[i] * 5.0f);
						if (c >= 32768)c = 32767;
						if (c < -32768)c = -32768;
						b[i] = (short)c;
					}
				}
		}
		if (savedata.kpivol != 1) {
			if (wavsam == 24) {
				for (int i = 0; i < cnt / 3; i++) {
					int c4 = b24c[i];
					if (savedata.kpivol == 2)	c4 = (int)((float)c4 * 2.0f);
					else if (savedata.kpivol == 4) c4 = (int)((float)c4 * 3.0f);
					else if (savedata.kpivol == 8) c4 = (int)((float)c4 * 4.0f);
					else if (savedata.kpivol == 16) c4 = (int)((float)c4 * 5.0f);
					if (c4 > 8388607)c4 = 8388607;
					if (c4 < -8388608)c4 = -8388608;
					b24c[i] = c4;
				}
			}
			else {
				for (int i = 0; i < cnt / 2; i++) {
					int c = (int)b[i];
					if (savedata.kpivol == 2)	c = (int)((float)b[i] * 2.0f);
					else if (savedata.kpivol == 3) c = (int)((float)b[i] * 3.0f);
					else if (savedata.kpivol == 4) c = (int)((float)b[i] * 4.0f);
					else if (savedata.kpivol == 5) c = (int)((float)b[i] * 5.0f);
					if (c >= 32768.0f)c = 32767;
					if (c < -32768.0f)c = -32768;
					b[i] = (short)c;
				}
			}
		}
		fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
		//fadeを三乗して計算密度を変更
		if (wavsam == 24) {
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
		if ((UINT)wl < (UINT)0x7fff0000) {
			if (cc1 == 1)	cc.Write(bw, cnt);
			wl += cnt;
		}
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

int playwavm4a(BYTE* bw, int old, int l1, int l2)
{
	//データ読み込み
	int rrr = readm4a(bw + old, l1);
	playb += (int)((float)(l1 + l2) / ((wavch == 1 || wavch == 2) ? (float)4 : (float)(wavch * 2)) / ((float)wavsam / (float)16));
	//	if (oggsize / ((wavch == 1) ? 1 : 1) - 44100 <= playb * 4) {
	//	if (savedata.saveloop == FALSE) {
	//	l1 = rrr;  fade1 = 1;
	//			return l1;
	//	}
	//}
/*	if (oggsize / ((wavch == 1) ? 2 : 1)- 50000 <= (int)(playb * wavch * 2 * (wavsam / 16.0))) {
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
				readm4a(bw + rrr, (int)l2 - rrr);
			}
		}
	}
	return l1 + l2;
}

int readm4a(BYTE* bw, int cnt)
{
	if (cnt == 0) return 0;
	_set_se_translator(trans_func);
	DWORD cnt1 = og->sikpi.dwUnitRender, cnt2 = (DWORD)cnt, cnt4 = 0; if (cnt1 == 0) cnt1 = 4096;
	DWORD r = 0;
	{
		int len3 = 0, len4 = 0;
		int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
		if (poss4 <= cnt) {
			while (true) {
				if (rrr == 1) {
					for (;;) {
						if (cnt2 <= cnt3) break;
						r = m4a_.Render(og->kmp, (BYTE*)bufkpi + cnt3, cnt1);
						cnt3 += r;
						if (r == 0) break;
					}
					if (rrr == 0 && muon != 0) {
						r = cnt;
						ZeroMemory(bufkpi, r);
						muon--;
					}
					if (rrr == 0 && muon == MUON) {
						ZeroMemory(bufkpi + r, cnt - r);
						r = cnt;
						muon--;
					}
					if (muon == 0) r = 0;

					int len2 = readtempo(bufkpi, cnt);

					if (len2 > 0) {
						// 書き込み
						if (poss2 + len2 > max_buffer_size) {
							int first = max_buffer_size - poss2;
							memcpy(bufkpi3 + poss2, outputRawBytesData.data(), first);
							memcpy(bufkpi3, outputRawBytesData.data() + first, len2 - first);
							poss2 = (poss2 + len2) % max_buffer_size;
						}
						else {
							memcpy(bufkpi3 + poss2, outputRawBytesData.data(), len2);
							poss2 += len2;
						}
						poss4 += len2;
						if (cnt3 < cnt) return cnt4;
						if (cnt2 <= cnt3) {
							cnt3 -= cnt2;
							if (cnt3 != 0)	memcpy(bufkpi, bufkpi + cnt2, cnt3);
						}
					}
					if (poss4 > cnt) break;
				}
			}
		}

		cnt2 = cnt;

		if (cnt2 > 0) {
			int to_read = cnt;
			if (poss3 + to_read > max_buffer_size) {
				int first = max_buffer_size - poss3;
				memcpy(bw, bufkpi3 + poss3, first);
				memcpy(bw + first, bufkpi3, to_read - first);
				poss3 = (poss3 + to_read) % max_buffer_size;
			}
			else {
				memcpy(bw, bufkpi3 + poss3, to_read);
				poss3 += to_read;
			}
			poss4 -= to_read;
		}

		equaliser(bw, cnt, reset);
		reset = FALSE;

		cnt4 = cnt3;
		//if (r == 0) cnt = 0;
		memcpy(bufkpi2, bw, cnt);
		unsigned short* bf1, * bf2; bf1 = (unsigned short*)bw; bf2 = (unsigned short*)bufkpi2;
		int cnt1 = cnt / 2;
		switch (wavch)
		{
		case 1:
		case 2:
			break;
		case 3: // 2.1   
			for (int sample = 0; sample < cnt1; sample += wavch)
			{
				int ChannelMap[3] = { 2,3,1 };
				for (int ch = 0; ch < wavch; ch++)
				{
					*bf1++ = bf2[ChannelMap[ch] - 1];
				}
				bf2 += wavch;
			}
			break;
		case 4: // Quad   
			for (int sample = 0; sample < cnt1; sample += wavch)
			{
				int ChannelMap[4] = { 2,3,1,4 };
				for (int ch = 0; ch < wavch; ch++)
				{
					*bf1++ = bf2[ChannelMap[ch] - 1];
				}
				bf2 += wavch;
			}
			break;
		case 5: // Surround   
			for (int sample = 0; sample < cnt1; sample += wavch)
			{
				int ChannelMap[5] = { 2,3,1,4,5 };
				for (int ch = 0; ch < wavch; ch++)
				{
					*bf1++ = bf2[ChannelMap[ch] - 1];
				}
				bf2 += wavch;
			}
			break;
		case 6: // 5.1   
			for (int sample = 0; sample < cnt1; sample += wavch)
			{
				int ChannelMap[6] = { 2,3,1,6,4,5 };
				for (int ch = 0; ch < wavch; ch++)
				{
					*bf1++ = bf2[ChannelMap[ch] - 1];
				}
				bf2 += wavch;
			}
			break;
		}
		Int24* b24c;
		b24c = (Int24*)bw;
		short* b, c;
		b = (short*)bw;
		if (wavsam == 24) {
			for (int i = 0; i < cnt / 3; i++) {
				int c4 = b24c[i];
				c4 = (int)((float)c4 * ((float)savedata.kakuVal / 100.0f));
				b24c[i] = c4;
			}
		}
		else {
			for (int i = 0; i < cnt / 2; i++) {
				int c = (int)b[i];
				c = (int)((float)c * ((float)savedata.kakuVal / 100.0f));
				b[i] = (short)c;
			}
		}
		if (wavsam == 24) {
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
		if (wavsam == 24) {
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
		if ((UINT)wl < (UINT)0x7fff0000) {
			if (cc1 == 1)	cc.Write(bw, cnt);
			wl += cnt;
		}
		lenl += cnt;
	}
	return cnt;
}

int playwavflac(BYTE* bw, int old, int l1, int l2)
{
	//データ読み込み
	int rrr = readflac(bw + old, l1);
	if (flacmode == 0)
		playb += (l1 + l2) / (wavsam / 4);
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
			//rrr /= 6;
			//rrr *= 6;
			flac_.SetPosition(og->kmp, loop1);
			poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			readflac(bw + old + rrr, l1 - rrr);
		}
	}
	if (l2) {
		rrr = readflac(bw, l2);
		if (l2 != rrr) {
			if (savedata.saveloop == 0 && endf == 1 && flacmode == 0) {
				l2 = rrr;
				if (savedata.saverenzoku == 0)
					fade1 = 1;
				else
					endflg = 1;
			}
			else {
				loopcnt++;
				playb = loop1;
				//rrr /= 6;
				//rrr *= 6;
				flac_.SetPosition(og->kmp, loop1);
				poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				readflac(bw + rrr, (int)l2 - rrr);
			}
		}
	}
	return l1 + l2;
}

int readflac(BYTE* bw, int cnt)
{
	if (cnt == 0)return 0;
	_set_se_translator(trans_func);
	DWORD cnt1 = og->sikpi.dwUnitRender * 2, cnt2 = (DWORD)cnt, cnt4 = 0, lenl = cnt; if (cnt1 == 0) cnt1 = 1024;
	DWORD r = 0;
	if (flacmode == 1)
		if (playb + lenl / 6 > (loop1 + loop2)) lenl = ((loop1 + loop2) - playb) * 6;
	try {
		int len3 = 0, len4 = 0;
		int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
		if (poss4 < lenl) {
			while (true) {

				if (rrr == 1)
					r = flac_.Render(og->kmp, (BYTE*)bufkpi, lenl);
				if (r != lenl && savedata.saveloop == 0)
					rrr = 0;
				if (rrr == 0 && muon != 0) {
					r = lenl;
					ZeroMemory(bufkpi, r);
					muon--;
				}
				if (rrr == 0 && muon == MUON) {
					ZeroMemory(bufkpi + r, lenl - r);
					r = lenl;
					muon--;
				}
				if (muon == 0) r = 0;
				cnt4 = r;
				if (r == 0) lenl = 0;
				if (r == 0) break;

				int len2 = readtempo(bufkpi, lenl);
				if (len2 > 0) {
					// 書き込み
					if (poss2 + len2 > max_buffer_size) {
						int first = max_buffer_size - poss2;
						memcpy(bufkpi3 + poss2, outputRawBytesData.data(), first);
						memcpy(bufkpi3, outputRawBytesData.data() + first, len2 - first);
						poss2 = (poss2 + len2) % max_buffer_size;
					}
					else {
						memcpy(bufkpi3 + poss2, outputRawBytesData.data(), len2);
						poss2 += len2;
					}
					poss4 += len2;
					if (cnt4 != lenl) return cnt4;
					if (poss4 > lenl) break;
				}
			}
		}

		cnt2 = lenl;

		if (cnt2 > 0) {
			int to_read = lenl;
			if (poss3 + to_read > max_buffer_size) {
				int first = max_buffer_size - poss3;
				memcpy(bw, bufkpi3 + poss3, first);
				memcpy(bw + first, bufkpi3, to_read - first);
				poss3 = (poss3 + to_read) % max_buffer_size;
			}
			else {
				memcpy(bw, bufkpi3 + poss3, to_read);
				poss3 += to_read;
			}
			poss4 -= to_read;
		}

		equaliser(bw, cnt2, reset);
		reset = FALSE;

		cnt4 = lenl;
		unsigned short* bf1, * bf2; bf1 = (unsigned short*)bw; bf2 = (unsigned short*)bufkpi2;
		//		int fw = playb % (wavch);
		//		bf2 += fw;
		int lenl1 = lenl / 2;
		Int24* b24c;
		b24c = (Int24*)bw;
		short* b, c;
		b = (short*)bw;
		if (wavsam == 24) {
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
		if (wavsam == 24) {
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
		if ((UINT)wl < (UINT)0x7fff0000) {
			if (cc1 == 1)	cc.Write(bw, lenl);
			wl += lenl;
		}
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
	playb += (l1 + l2) / (wavsam / 4);
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
			opus_.SetPosition(og->kmp, 0);
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
				opus_.SetPosition(og->kmp, 0);
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
				if (rrr == 1) {
					r = opus_.Render(og->kmp, (BYTE*)bufkpi, cnt);
					if (r != cnt && savedata.saveloop == 0)
						rrr = 0;
					if (rrr == 0 && muon != 0) {
						r = cnt;
						ZeroMemory(bufkpi, r);
						muon--;
					}
					if (rrr == 0 && muon == MUON) {
						ZeroMemory(bufkpi + r, cnt - r);
						r = cnt;
						muon--;
					}
					if (muon == 0) r = 0;

					int len2 = readtempo(bufkpi, r);
					if ((UINT)wl < (UINT)0x7fff0000) {
						//				if (cc1 == 1)	cc.Write(bw, cnt);
						if (cc1 == 1)	cc.Write(outputRawBytesData.data(), len2);
						//				if (cc1 == 1)	cc.Write(bufkpi, r);
						wl += len2;
					}

					if (len2 > 0) {
						// 書き込み
						if (poss2 + len2 > max_buffer_size) {
							int first = max_buffer_size - poss2;
							memcpy(bufkpi3 + poss2, outputRawBytesData.data(), first);
							memcpy(bufkpi3, outputRawBytesData.data() + first, len2 - first);
							poss2 = (poss2 + len2) % max_buffer_size;
						}
						else {
							memcpy(bufkpi3 + poss2, outputRawBytesData.data(), len2);
							poss2 += len2;
						}
						poss4 += len2;
						if (r < cnt) return cnt4;

					}
				}
			}

			cnt2 = lenl;

			if (cnt2 > 0) {
				int to_read = cnt;
				if (poss3 + to_read > max_buffer_size) {
					int first = max_buffer_size - poss3;
					memcpy(bw, bufkpi3 + poss3, first);
					memcpy(bw + first, bufkpi3, to_read - first);
					poss3 = (poss3 + to_read) % max_buffer_size;
				}
				else {
					memcpy(bw, bufkpi3 + poss3, to_read);
					poss3 += to_read;
				}
				poss4 -= to_read;
			}
		}

		equaliser(bw, cnt2, reset);
		reset = FALSE;

		cnt4 = r;
		if (r == 0) cnt = 0;
		unsigned short* bf1, * bf2; bf1 = (unsigned short*)bw; bf2 = (unsigned short*)bufkpi2;
		//		int fw = playb % (wavch);
		//		bf2 += fw;
		int cnt1 = cnt / 2;
		Int24* b24c;
		b24c = (Int24*)bw;
		short* b, c;
		b = (short*)bw;
		if (wavsam == 24) {
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
		if (wavsam == 24) {
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
	playb += (l1 + l2) / (wavsam / 4);
	if (oggsize / ((wavch == 1) ? 2 : 1) - 192 * 20 <= (int)(playb * wavch * 2 * (wavsam / 16.0))) {
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
	//_set_se_translator(trans_func);
	DWORD cnt2 = (DWORD)cnt;
	DWORD r = 0;
	int len3 = 0, len4 = 0;
	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	if (poss4 <= cnt) {
		while (true) {
			cnt3 = dsd_.kpiRender(og->kmp, (BYTE*)bufkpi, cnt / (wavch * wavsam / 8)) * (wavch * wavsam / 8);
			int len2 = readtempo(bufkpi, cnt);
			if (sek == 0) {
				if (cnt2 <= cnt3) {
					cnt3 -= cnt2;
					if (cnt3 != 0)	memcpy(bufkpi, bufkpi + cnt2, cnt3);
				}
			}

			if (len2 > 0) {
				// 書き込み
				if (poss2 + len2 > max_buffer_size) {
					int first = max_buffer_size - poss2;
					memcpy(bufkpi3 + poss2, outputRawBytesData.data(), first);
					memcpy(bufkpi3, outputRawBytesData.data() + first, len2 - first);
					poss2 = (poss2 + len2) % max_buffer_size;
				}
				else {
					memcpy(bufkpi3 + poss2, outputRawBytesData.data(), len2);
					poss2 += len2;
				}
				poss4 += len2;
			}
			if (len4 != 0) break;
			if (poss4 > cnt) break;
		}
	}

	cnt2 = cnt;
	int to_read = cnt;
	if (len4 != 0) {
		cnt2 = len4;
		to_read = len4;
	}

	if (cnt2 > 0) {
		if (poss3 + to_read > max_buffer_size) {
			int first = max_buffer_size - poss3;
			memcpy(bw, bufkpi3 + poss3, first);
			memcpy(bw + first, bufkpi3, to_read - first);
			poss3 = (poss3 + to_read) % max_buffer_size;
		}
		else {
			memcpy(bw, bufkpi3 + poss3, to_read);
			poss3 += to_read;
		}
		poss4 -= to_read;
	}

	equaliser(bw, cnt2, reset);
	reset = FALSE;

	if ((UINT)wl < (UINT)0x7fff0000) {
		if (cc1 == 1)	cc.Write(bw, cnt2);
		wl += cnt2;
	}


	return cnt;
}


int playwavmp3(BYTE* bw, int old, int l1, int l2)
{
	playb += (l1 + l2);
	//データ読み込み
	int rrr = 0, rrr2 = 0;
	rrr = readmp3(bw + old, l1);
	if (l1 != rrr) {
		if (savedata.saveloop == 0 && endf == 1) {
			l1 = rrr;
			if (savedata.saverenzoku == 0) {
				fade1 = 1;
			}
			else	endflg = 1;

		}
		else {
			loopcnt++;
			playb = loop1;
			mp3_.seek(10, wavch); poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			readmp3(bw + old + rrr, l1 - rrr);
		}
	}
	if (l2) {
		rrr2 = readmp3(bw, l2);
		if (l2 != rrr2) {
			if (savedata.saveloop == 0 && endf == 1) {
				l2 = rrr2;
				if (savedata.saverenzoku == 0) {
					fade1 = 1;
				}
				else endflg = 1;
			}
			else {
				loopcnt++;
				playb = loop1;
				mp3_.seek(10, wavch); poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				readmp3(bw + rrr2, (int)l2 - rrr2);
			}
		}
	}
	return l1 + l2;
}

int readmp3(BYTE* bw, int cnt)
{
	int r = cnt, rr = cnt;
	int cnt2;
	if (rr == 0)return 0;
	int len3 = 0, len4 = 0;
	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	if (poss4 <= cnt) {
		while (true) {
			if (savedata.mp3orig)
				r = mp3_.Render2(bufkpi, rr, kbps);
			else
				r = mp3_.Render(bufkpi, rr);

			if (r <= 0) {
				if (muon != 0) {
					if (savedata.saverenzoku == 0) {
						ZeroMemory(bufkpi, rr);
						muon--;
					}
					else {
						endflg = 1;
					}
					break;
				}
			}
			int len2 = readtempo(bufkpi, r);

			if (len2 > 0) {
				// 書き込み
				if (poss2 + len2 > max_buffer_size) {
					int first = max_buffer_size - poss2;
					memcpy(bufkpi3 + poss2, outputRawBytesData.data(), first);
					memcpy(bufkpi3, outputRawBytesData.data() + first, len2 - first);
					poss2 = (poss2 + len2) % max_buffer_size;
				}
				else {
					memcpy(bufkpi3 + poss2, outputRawBytesData.data(), len2);
					poss2 += len2;
				}
				poss4 += len2;
				if (rr > r) return 1;
				if (poss4 > cnt) break;
			}
		}
	}

	cnt2 = cnt;

	if (cnt2 > 0) {
		int to_read = cnt;
		if (poss3 + to_read > max_buffer_size) {
			int first = max_buffer_size - poss3;
			memcpy(bw, bufkpi3 + poss3, first);
			memcpy(bw + first, bufkpi3, to_read - first);
			poss3 = (poss3 + to_read) % max_buffer_size;
		}
		else {
			memcpy(bw, bufkpi3 + poss3, to_read);
			poss3 += to_read;
		}
		poss4 -= to_read;
	}

	equaliser(bw, cnt2, reset);
	reset = FALSE;


	Int24* b24c;
	b24c = (Int24*)bw;
	short* b, c;
	b = (short*)bw;
	fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
	if (wavsam == 24) {
		for (int i = 0; i < cnt / 3; i++) {
			int c4 = b24c[i];
			c4 = (int)((float)c4 * ((float)savedata.kakuVal / 100.0f));
			b24c[i] = c4;
			c4 = b24c[i];
			if (savedata.mp3 == 2)	c4 = (int)((float)c4 * 2.0f);
			else if (savedata.mp3 == 4) c4 = (int)((float)c4 * 3.0f);
			else if (savedata.mp3 == 8) c4 = (int)((float)c4 * 4.0f);
			else if (savedata.mp3 == 16) c4 = (int)((float)c4 * 5.0f);
			if (c4 > 8388607)c4 = 8388607;
			if (c4 < -8388608)c4 = -8388608;
			b24c[i] = c4;
			{
				float c4;
				int c5;
				c5 = b24c[i]; c4 = (float)c5;
				c4 = c4 * fade * fade; c5 = (int)c4;
				b24c[i] = c5;
			}
		}
	}
	else {
		for (int i = 0; i < cnt / 2; i++) {
			int c = (int)b[i];
			c = (int)((float)c * ((float)savedata.kakuVal / 100.0f));
			b[i] = (short)c;
			c = (int)b[i];
			if (savedata.mp3 == 2)	c = (int)((float)b[i] * 1.5f);
			else if (savedata.mp3 == 3) c = (int)((float)b[i] * 2.0f);
			else if (savedata.mp3 == 4) c = (int)((float)b[i] * 2.5f);
			else if (savedata.mp3 == 5) c = (int)((float)b[i] * 3.0f);
			if (c >= 32768)c = 32767;
			if (c <= -32767)c = -32766;
			b[i] = (short)c;
			c = b[i]; c = (short)(((float)c) * fade * fade); b[i] = c;
		}
	}

	if ((UINT)wl < (UINT)0x7fff0000) {
		if (cc1 == 1)	cc.Write(bw, cnt2);
		wl += cnt2;
	}

	lenl += cnt;
	//	playb+=cnt;
	return cnt;
}
BOOL oggyomikomi = FALSE;

void playwavds2(BYTE* bw, int old, int l1, int l2)
{
	//	return;
	//	playb+=(l1+l2)/4;
	//データ読み込み
	if (l1 == 0)return;
	oggyomikomi = TRUE;
	int rrr = mcopy((char*)bw + old, l1);
	if (l1 != rrr) {
		if (savedata.saveloop == 0 && endf == 1) {
			l1 = rrr; fade1 = 1;
		}
		else {
			loopcnt++;
			playb = loop1;
			ov_pcm_seek(&vf, (ogg_int64_t)loop1); poss = 0; poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			poss5 = loop1;
			if (g_rubberBandStretcher) {
				delete g_rubberBandStretcher;
				g_rubberBandStretcher = NULL;
			}
			mcopy((char*)bw + old + rrr, (int)l1 - rrr);
		}
	}
	if (l2) {
		rrr = mcopy((char*)bw, l2);
		if (l2 != rrr) {
			if (savedata.saveloop == 0 && endf == 1) {
				l2 = rrr; fade1 = 1;
			}
			else {
				loopcnt++;
				playb = loop1;
				ov_pcm_seek(&vf, (ogg_int64_t)loop1); poss = 0; poss2 = poss3 = poss4 = poss5 = poss6 = 0;
				poss5 = loop1;
				if (g_rubberBandStretcher) {
					delete g_rubberBandStretcher;
					g_rubberBandStretcher = NULL;
				}
				mcopy((char*)bw + rrr, (int)l2 - rrr);
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
			ov_pcm_seek(&vf, (ogg_int64_t)loop1); poss = 0;
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
			ov_pcm_seek(&vf, (ogg_int64_t)loop1); poss = 0;
			mcopy((char*)buf[lo] + rrr, (int)dwDataLen - rrr);
		}
	}

	memcpy(g_OutputBuffer[lo]->lpData, buf[lo], dwDataLen);
}

LRESULT COggDlg::dp1(WPARAM a, LPARAM b) {
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
	if (filen_ == "*1") OnRestart();
	else if (filen_ == "*2") OnPause();
	else if (filen_ == "*3") stop();
	else if (filen_ == "*4") OnPlayList();
	else if (filen_ == "*5") OnButton21();
	else if (filen_ == "*6") OnButton9_Folder();
	else filen = filen_;
	return CCustomDialog::OnCopyData(pWnd, pCopyDataStruct);
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
		return;
	}
	filen = a;
	if (filen.Left(1) == "\"") filen = filen.Right(filen.GetLength() - 1);
	if (filen.Right(1) == "\"") filen = filen.Left(filen.GetLength() - 1);
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
	else if (filen.Find(L".pac::") > 0 && filen.Find(L"Trails in the Sky 1st Chapter")) {
		mode = 30; modesub = 30;
		play();
	}
	else {//DirectShow
		stflg = FALSE;
		CFile ff;
		CString ss11 = filen; ss11.MakeLower();
		if (ss11.Right(3) == "m4a") {
			if (ff.Open(filen, CFile::modeRead | CFile::shareDenyNone, NULL) == TRUE) {
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
			ss = s.Left(s.ReverseFind(':') - 1);
			if (ss != "") s = ss;
			kpi[0] = 0;
			pl->plugs(s, &p, kpi, kvver);
			if (p.sub == -3) {//kb medua player
				//				hDLLk = LoadLibrary(kpi);
				//				pFunck = (pfnGetKMPModule)::GetProcAddress(hDLLk, "kmp_GetTestModule");
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
		if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE) {
			f123.Close();
			if (IDYES == MessageBox(_T("途中再生データが存在します。\n前回中断した部分から再生しますか？\nはい = 途中から再生\nいいえ = はじめから再生"), _T("再生確認"), MB_YESNO)) {
				flggg = 1;
			}
			else {
				CFile::Remove(filen + _T(".save"));
			}
		}
		if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE && flggg == 1) {
			f123.Close();
			if (pGraphBuilder)pMainFrame1->plays2();
			if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
			if (mode == -10) {
				if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE) {
					f123.Read(&playb, sizeof(__int64));
					if (savedata.mp3orig) {
						mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch);
					}
					else {
						mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch);
					}
					f123.Close();
				}
			}
			if (mode == -2) {
				if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE) {
					f123.Read(&aa1_, sizeof(double));
					pMainFrame1->seek((LONGLONG)(((float)((float)aa1_ * 100.0f * 100000.0f))));
					f123.Close();
				}
			}
		}
		else {
			if (pGraphBuilder)pMainFrame1->plays2();
			if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
			if (pMainFrame1) { pMainFrame1->seek(0); }
		}
		//		if(pGraphBuilder)pMainFrame1->plays2();
		//		if(pMediaControl)pMediaControl->Run();
		int a = 0; aa2 = 0;
		REFTIME aa = 0;
		aa2 = 0;
		ps = 0; m_ps.SetWindowText(_T("一時停止"));
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
		}

		plf = 1;
	}

}

void COggDlg::OnDropFiles(HDROP hDropInfo)
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	if (pl && plw) {
		pl->OnDropFiles(hDropInfo);
		return;
	}

	TCHAR filen_c[1024];
	UINT cnt = DragQueryFile(hDropInfo, (UINT)-1, filen_c, sizeof(filen_c));
	if (cnt != 1)
	{
		MessageBox(_T("ファイルは1つだけドロップしてください。\nプレイリストが開いている時は複数でもokです。"), _T("ogg/wav簡易プレイヤ"), MB_ICONEXCLAMATION);
		CCustomDialog::OnDropFiles(hDropInfo);
		return;
	}
	DragQueryFile(hDropInfo, (UINT)0, filen_c, sizeof(filen_c));
	CString ff;
	ff = filen;
	filen = (CString)filen_c;
	CFile f;
	if (f.Open(filen, CFile::modeRead | CFile::shareDenyNone, NULL) == FALSE) {
		filen = ff;
		MessageBox(_T("ほかのプログラムで開かれているためファイルが開けません"), _T("ogg/wav簡易プレイヤ"), MB_ICONEXCLAMATION);
		CCustomDialog::OnDropFiles(hDropInfo);
		return;
	}
	f.Close();
	dp(filen);
	CCustomDialog::OnDropFiles(hDropInfo);
}

BOOL thn = TRUE;
BOOL thn1 = FALSE;

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
void COggDlg::stop()
{
	lrc_backup = L"";
	loop1_2 = -1;
	stflg = TRUE;
	KillTimer(1250);
	gamenkill();
	videoonly = FALSE;
	fade1 = 1;
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
					if (f.Open(filen + _T(".save"), CFile::modeCreate | CFile::modeWrite, NULL) == TRUE) {
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
	if ((ogg || adbuf2 || mod || wav) && mode != -2)
	{
		thn1 = TRUE;
		if (m_dsb)m_dsb->SetVolume(DSBVOLUME_MIN);
		if (ps == 1) {
			OnPause();
		}
		m_ps.SetWindowText(_T("一時停止"));
		ps = 0;
		if (m_dsb)m_dsb->Stop();
		if (pAudioClient) pAudioClient->Stop();
		if (m_dou.GetCheck() == 1)
			if (cc1 == 1) {
				cc.SeekToBegin();
				WAVEFILEHEADER wh1;
				cc.Read(&wh1, sizeof(wh1));
				wh1.ckSizeRIFF = wl + 44 - 8;
				wh1.ckSizeData = wl;
				cc.SeekToBegin();
				cc.Write(&wh1, sizeof(wh1));
				cc.Close();
				cc1 = 0;
			}
		CCriticalLock _ccl(&cs);
		stf = 1;
		_ccl.Leave();
		timer.SetEvent();
		if (thn == FALSE) {
			for (int i = 0; i < 100; i++) {
				if (thn == TRUE) break;
				DoEvent();
				Sleep(10);
			}
		}


		Closeds();
		//		FreeOutputBuffer();
		plf = 0;

		if (ogg)ReleaseOggVorbis(&ogg);

		ogg = NULL;

		//		for(int l=0;l<20;l++){Sleep(50);DoEvent();}
		if (adbuf2)free(adbuf2);//delete [] adbuf2;
		adbuf2 = NULL;
		if (mode == -10) mp3_.Close();
		if (mode == -8) flac_.Close(og->kmp);
		if (mode == -9) m4a_.Close(og->kmp);
		if (mode == -7) dsd_.kpiClose(og->kmp);
		kmp = NULL;
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
		DoEvent();
		thend = 1;
		fadeadd = 0; fade = 1.0;
	}
	if (wav) free(wav);
	wav = NULL;
	playf = 0;
	mode = modesub;
	stflg = FALSE;
	SetTimer(4923, 30, NULL);
	m_lrc.SetWindowText(L"");
	m_lrc2.SetWindowText(L"歌詞(.lrc)が表示されます");
	m_lrc3.SetWindowText(L"");

	eqflg = TRUE;
}

void COggDlg::stop1()
{
	lrc_backup = L"";
	loop1_2 = -1;

	//	for(int i=0;i<10;i++){DoEvent();Sleep(10);}
	gamenkill();
	videoonly = FALSE;
	if (ptl)ptl->SetProgressValue(m_hWnd, (LONGLONG)0, (LONGLONG)1);
	if (ptl)ptl->SetProgressState(m_hWnd, TBPF_NOPROGRESS);
	if (ogg != NULL || adbuf2 != NULL || wav)
	{
		thn1 = TRUE;
		timer.SetEvent();
		if (m_dsb)m_dsb->SetVolume(DSBVOLUME_MIN);
		if (ps == 1) {
			OnPause();
		}
		if (m_dou.GetCheck() == 1)
			if (cc1 == 1) {
				cc.SeekToBegin();
				WAVEFILEHEADER wh1;
				cc.Read(&wh1, sizeof(wh1));
				wh1.ckSizeRIFF = wl + 44 - 8;
				wh1.ckSizeData = wl;
				cc.SeekToBegin();
				cc.Write(&wh1, sizeof(wh1));
				cc.Close();
				cc1 = 0;
			}
		CCriticalLock _ccl(&cs);
		stf = 1;
		_ccl.Leave();
		if (thn == FALSE)
			for (;;) {
				if (thn == TRUE) break;
				DoEvent();
			}
		Closeds();
		//		FreeOutputBuffer();
		plf = 0;
		if (ogg)ReleaseOggVorbis(&ogg);
		ogg = NULL;

		if (thend == FALSE) {
			thend1 = TRUE;
			for (int kk = 0; kk < 50; kk++) {
				if (thend == 1) break;
				DoEvent();
			}
		}
		Sleep(50);
		playb = 0;
		if (adbuf2)free(adbuf2);//delete [] adbuf2;
		adbuf2 = NULL;
		if (mode == -10) mp3_.Close();
		if (mode == -8) flac_.Close(og->kmp);
		if (mode == -9) m4a_.Close(og->kmp);
		if (mode == -7) dsd_.kpiClose(og->kmp);
		kmp = NULL;
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

		DoEvent();
		thend = 1;
		fadeadd = 0; fade = 1.0;
	}
	if (wav) free(wav);
	wav = NULL;
	playf = 0;
	mode = modesub;
	m_lrc.SetWindowText(L"");
	m_lrc2.SetWindowText(L"歌詞(.lrc)が表示されます");
	m_lrc3.SetWindowText(L"");
	eqflg = TRUE;
}


BOOL COggDlg::DestroyWindow()
{
	// TODO: この位置に固有の処理を追加するか、または基本クラスを呼び出してください
	//	ReleaseOggVorbis(&ogg);
	stop();
	waveOutReset(hwo);
	waveOutClose(hwo);
	if (deve) {
		audio->Release();
		dev->Release();
		deve->Release();
	}
	if (pl && plw) {
		killw1 = 0;
		pl->DestroyWindow();
		for (; killw1 == 0;)
			DoEvent();
		pl = NULL;
		savedata.pl = 1;
	}
	else savedata.pl = 0;
	if (mi) {
		killw1 = 0;
		mi->DestroyWindow();
		for (; killw1 == 0;)
			DoEvent();
		mi = NULL;
	}
	if (m_pDlgColor)delete m_pDlgColor;
	if (ptl) ptl->Release();
	if (pcdl) pcdl->Release();
	CoUninitialize();
	//	timeKillEvent(uTimerId);
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
	return CCustomDialog::DestroyWindow();
}
//oggから実際にデータを獲得する
int mcopy(char* a, int len)
{
	if (len == 0) return 0;
	int ch = (wavch == 2 ? 1 : 2);
	//poss = 0;
	int ret = 0, lenl = len / (wavch * 2), cnt2;
	//ret=ov_pcm_seek(&vf,playb+poss);

	int len3 = 0, len4 = 0;
	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	if (poss4 <= len) {
		for (int k = 0; k < 5; k++) {
			ret = 0;
			if ((int)playb > (data_size + 20000) / (wavch * 2) && endf == 1) {
				playb += lenl;
				if (muon != 0) {
					muon--;
					ZeroMemory(a, len);
				}
				if (muon == MUON) {
					ZeroMemory(a, len);
					muon--;
					rrr = 0;
				}
				if (muon == 0) return 0;
				return len;
			}

			int read = len / 4096;
			int read2 = len % 4096;
			int i = 0;
			for (;;) {
				ret = ov_read(&vf, (char*)(bufwav + poss * 4), 4096, 0, 2, 1, &current_section) / 4;
				poss += ret;
				if (ret == 0) break;
				if (lenl <= poss)	break;
			}

			int len2 = readtempo(bufwav, lenl * 4);
			playb += len2 / 4;

			if (len2 > 0) {
				// 書き込み
				if (poss2 + len2 > max_buffer_size) {
					int first = max_buffer_size - poss2;
					memcpy(bufkpi3 + poss2, outputRawBytesData.data(), first);
					memcpy(bufkpi3, outputRawBytesData.data() + first, len2 - first);
					poss2 = (poss2 + len2) % max_buffer_size;
				}
				else {
					memcpy(bufkpi3 + poss2, outputRawBytesData.data(), len2);
					poss2 += len2;
				}
				poss4 += len2;
			}
			if (lenl <= poss) {
				poss -= lenl;
				if (poss != 0)	memcpy(bufwav, bufwav + lenl * 4, poss * 4);
			}
			if (len4 != 0) break;
			if (poss4 > len) break;
		}
	}

	int cnt0 = len / 4;
	if (loop1 + loop2 < poss5 + cnt0 && endf == 0) {
		cnt0 = (loop1 + loop2) - poss5;
	}

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
	cnt0 *= 4;


	cnt2 = cnt0;
	int to_read = cnt0;

	if (cnt2 > 0) {
		if (poss3 + to_read > max_buffer_size) {
			int first = max_buffer_size - poss3;
			memcpy(a, bufkpi3 + poss3, first);
			memcpy(a + first, bufkpi3, to_read - first);
			poss3 = (poss3 + to_read) % max_buffer_size;
		}
		else {
			memcpy(a, bufkpi3 + poss3, to_read);
			poss3 += to_read;
		}
		poss4 -= to_read;
	}

	equaliser(a, cnt2, reset);
	reset = FALSE;

	//fade計算
	short* b, c;
	b = (short*)a;
	CString sss;
	sss = filen.Right(filen.GetLength() - filen.ReverseFind('.') - 1);
	sss.MakeLower();
	fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
	//fadeを三乗して計算密度を変更
	for (int i = 0; i < (cnt0 * wavch) / 4; i++) { c = b[i]; c = (short)(((float)c) * fade * fade); b[i] = c; }

	if ((UINT)wl < (UINT)0x7fff0000) {
		//				if (cc1 == 1)	cc.Write(bw, cnt);
		if (cc1 == 1)	cc.Write(a, cnt0);
		//				if (cc1 == 1)	cc.Write(bufkpi, r);
		wl += cnt0;
	}

	return cnt0;
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

extern IBasicAudio* pBasicAudio;
extern IBaseFilter* prend;
extern double rate;
extern int rateflg;
extern RECT rcm;
extern long height, width;
DWORD videocnt = 0, videocnt2 = 0, videocnt3;

int pox, poy;


void COggDlg::timerp()
{
	if (playy == 0)return;
	ms2++;
	CString s, ss, sss;
	if (voldsf) {
		voldsf = 0;
		m_dsval.SetPos(savedata.dsvol);
		m_dsval.ShowWindow(SW_SHOWNA);
	}

	if (mode == -2) loop1 = loop2 = loopcnt = wl = 0;
	//時間
	int t1, ta, tb, tc, ta1, tb1, tc1, tag = 0, tbg = 0, tcg = 0, ttt;
	tt++;
	//	if(tt==4)
	//	{
	//		tt=0;
	double t3;
	if ((mode == -2 || (mode != -2 && videoonly == TRUE))) {
		t3 = (double)oggsize2;
		tt = (int)(t3 * 100.0);
		t1 = tt / 100;
		ta = t1 / 60;
		tb = t1 % 60;
		tc = tt % 100;
		if (videocnt > 7) {
			REFTIME aa;
			aa = 0;
			if (pMediaPosition)pMediaPosition->get_CurrentPosition(&aa);
			if (pMediaPosition && (oggsize2 < aa) && plf == 1) {
				aa = 0; plf = 0;
				if (pMainFrame1 != NULL && (mode == -2 || (mode > 0 && videoonly == TRUE))) {
					pMainFrame1->pause(0);
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
		double wavv[] = { 0,1.0,2.0,3.0 / 0.75,4.0 / 0.75,5.0 / 0.75,6.0 / 0.75 };//(double)(wavbit2/wavv[wavch])
		double wavv2[] = { 0,2.0,1.0,2.0 / 3.0,2.0 / 4.0,2.0 / 5.0,2.0 / 6.0 };//(double)(wavbit2/wavv[wavch])
		t3 = (double)oggsize / (double)(wavbit * 2.0 * wavv[wavch]) / (double)(wavsam / 16.0f);
		if (mode == -10) t3 *= (wavsam / 16.0f) * 4.0;
		if ((mode == -9) && wavch > 2) t3 *= wavch / 2.0;
		tt = (int)(t3 * 100.0);
		if (tt < 0) tt = 0;
		t1 = tt / 100;
		ta = t1 / 60;
		tb = t1 % 60;
		tc = tt % 100;
		t3 = (double)playb / (double)(wavbit / wavv2[wavch]);// / (double)(wavsam / 16.0f);
		//先読み分を除去

		if (mode == -10) t3 /= (wavsam / 16.0f) * 4.0;
		if ((mode == -9) && wavch > 2) t3 *= wavch / 2.0;
		if (m_dsb && !(mode == -8 || mode >= 1)) {
			//t3 -= 1.0;
		}
		//t3 -= 1500;
		if (t3 < 0.0) t3 = 0.0;
		tt = (int)(t3 * 100.0);
		if (tt < 0) tt = 0;
		t1 = tt / 100;
		ta1 = t1 / 60;
		tb1 = t1 % 60;
		tc1 = tt % 100;
		ttt = tt;
		t3 = (double)wl / (double)(wavbit * 2 * wavv[wavch]) / (double)(wavsam / 16.0f);
		tt = (int)(t3 * 100.0);
		t1 = tt / 100;
		tag = t1 / 60;
		tbg = t1 % 60;
		tcg = tt % 100;
	}
	videocnt++;

	t3 = (double)loop1 / (double)(wavbit);
	tt = (int)(t3 * 100.0);
	t1 = tt / 100;
	int tal1 = t1 / 60;
	int tbl1 = t1 % 60;
	int tcl1 = tt % 100;
	t3 = (double)(loop2 + loop1) / (double)(wavbit);
	tt = (int)(t3 * 100.0);
	t1 = tt / 100;
	int tal2 = t1 / 60;
	int tbl2 = t1 % 60;
	int tcl2 = tt % 100;

	dc.FillSolidRect(0, 0, 3000, 2000, RGB(0, 0, 0));
	//		dcsub.FillSolidRect(0,0,3000,30,RGB(1,1,1));

	if (jx != -1) {
		ULONG_PTR gdiplusToken;
		Gdiplus::GdiplusStartupInput gdiplusStartupInput;

		// Initialize GDI+.
		Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

		Gdiplus::Bitmap clImage(img, NULL);
		Gdiplus::Bitmap* pImage = NULL;
		pImage = Gdiplus::Bitmap::FromHBITMAP(img, NULL);

		// GDI+による描画
		Gdiplus::Graphics graphics(dc);
		graphics.DrawImage(&clImage, 10000, 10000);

		// HBITMAPによるBitBlt描画
		Gdiplus::Color bkcolor(0x0, 0x0, 0xff);
		HBITMAP hBmp;
		HBITMAP hOld;
		HDC hMem;

		clImage.GetHBITMAP(bkcolor, &hBmp);
		hMem = CreateCompatibleDC(dc);
		hOld = (HBITMAP)SelectObject(hMem, hBmp);
		CRect r;
		GetClientRect(&r);
		//			r.right = r.bottom * xy;
		SetStretchBltMode(dc.m_hDC, HALFTONE); //高画質モード
		SetBrushOrgEx(dc.m_hDC, 0, 0, NULL); //ブラシのずれを防止
		if ((r.right - 100) * (jxy) > jx) {
			//			StretchBlt(dc, MDC - 60, 0, (60), 60 / (jxy), hMem, 0, 0, jx, jy, SRCPAINT); //伸縮
		}
		else {
			//			StretchBlt(dc, MDC - 60, 0, (60) * (jxy), 60, hMem, 0, 0, jx, jy, SRCPAINT); //伸縮
		}
		//	BitBlt(dcc, 0, 0, clImage.GetWidth(), clImage.GetHeight(), hMem, 0, 0, SRCCOPY);
		SelectObject(hMem, hOld);

		DeleteDC(hMem);

		// Finalize GDI+
		Gdiplus::GdiplusShutdown(gdiplusToken);
	}

	for (int lp = 0; lp < lrcnum - 1; lp++) {
		if (lrctm[lp] <= ttt && lrctm[lp + 1] > ttt) {
			CString s;
			m_lrc2.GetWindowText(s);
			if (lrc[lp] == lrc_backup) continue;
			if (lp != 0) m_lrc.SetWindowText(lrc[lp - 1]);
			m_lrc2.SetWindowText(lrc[lp]);
			m_lrc3.SetWindowText(lrc[lp + 1]);
			lrc_backup = lrc[lp];
		}
	}


	if (m_supe.GetCheck() == TRUE && plf == 1 && (wav || ogg)) Speana();
	s = L""; ss = L"";
	s = "name:";
	moji(s, 1, 0, 0xffffff);
	if (fnn != L"")		sss = fnn;
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7) sss = tagfile;
	if (stitle != "" && mode == -1 || mode == 21 || mode == -6)	sss = stitle;
	int si = mojisub(sss, 1, 0, 0xffffff);
	if (si > MDC) {
		ss = sss + _T("》---《");
		if (mode == -10) ss = sss + _T("》---《");
		si = mojisub(ss, 1, 0, 0xffffff);
	}
	//枠はみ出し時スクロール処理
	if (si > MDC) {
		dc.BitBlt(8 * 5 * 4, 0, 88 * 2 * 4 + 1000, (24 * 4) * 4, &dcsub, mcnt, 0, SRCINVERT);
		if (si - mcnt < MDC) {
			mcnt2++;
			mcnt2++;
			mcnt2++;
			mcnt2++;
			dc.BitBlt(MDC - mcnt2 + 8 * 5 * 4, 0, 88 * 2 * 4 + 1000, (24 * 4) * 4, &dcsub, 0, 0, SRCINVERT);
			if (MDC - mcnt2 <= 0) { mcnt2 = 0; mcnt = 0; }
		}
		else mcnt2 = 0;
		mcnt++;
		mcnt++;
		mcnt++;
		mcnt++;
	}
	else {
		dc.BitBlt(8 * 5 * 4, 0, 88 * 2 * 4 + 1000, (24 * 4) * 4, &dcsub, 0, 0, SRCINVERT);
	}

	//mcnt1++;
	if (modesub == 5 || modesub == 7 || modesub == 8 || modesub == 9 || modesub == 10)	s.Format(_T("file:%s"), filen);
	else if (mode == 21)
		s.Format(_T("file:%s"), filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1));
	else			s.Format(_T("file:%s"), filen);
	//		if(fnn.Right(4)=="動画"||fnn.Right(5).Left(4)=="動画")		s.Format("file:動画");
	if (filen.Left(2) == L"★")		s.Format(_T("file:動画"));
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7 || mode == -6) {
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
		s.Format(_T("file:音声ファイル%s"), g);
	}
	if (mode == -2 || mode == -3) sss = filen.Right(filen.GetLength() - filen.ReverseFind('.') - 1);
	if (mode == -3) s.Format(_T("file:kpiファイル(%s)"), sss);
	if (mode == -1) s.Format(_T("file:oggファイル"), sss);
	if (mode == -2 && rate == 0.0) s.Format(_T("file:音声ファイル(%s)"), sss);
	if (mode == -2 && rate != 0.0) s.Format(_T("file:動画ファイル(%s)"), sss);
	if (mode == 30)s = "file:空の軌跡 The 1st";
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


	CString wavbit1 = wavb(wavbit);

	if ((mode == -2 || videoonly) && rate != 0.0 && height != 0) {
		s.Format(_T("size:%d x %d"), rcm.right, rcm.bottom);
		moji(s, 1, 48, 0x7fffff);
		s.Format(_T("rate:%3.3ffps"), rate);
		moji(s, 1, 64, 0x7fffff);
	}
	else if ((mode == -2 || videoonly) && rcm.right > 1) {
		s.Format(_T("size:%d x %d"), rcm.right, rcm.bottom);
		moji(s, 1, 48, 0x7fffff);
		s.Format(_T("rate:算出中……"));
		moji(s, 1, 64, 0x7fffff);
	}
	else if (mode == -2 && wavbit != 0) {
		s.Format(_T("sample:%sHz"), wavbit1);
		moji(s, 1, 48, 0x7fffff);
		s.Format(_T("channel:%dch"), wavch);
		if (wavch == 3)s.Format(_T("channel:%s"), _T("2.1ch"));
		if (wavch == 4)s.Format(_T("channel:%s"), _T("3.1ch"));
		if (wavch == 5)s.Format(_T("channel:%s"), _T("4.1ch"));
		if (wavch == 6)s.Format(_T("channel:%s"), _T("5.1ch"));
		if (wavch == 7)s.Format(_T("channel:%s"), _T("6.1ch"));
		if (wavch == 8)s.Format(_T("channel:%s"), _T("7.1ch"));
		moji(s, 1, 64, 0x7fffff);
	}
	else if (mode == -2 && wavbit == 0) {
		s.Format(_T("sample:不明"), wavbit);
		moji(s, 1, 48, 0x7fffff);
		s.Format(_T("channel:不明"), wavch);
		moji(s, 1, 64, 0x7fffff);
	}
	else if (mode == -3) {
		s.Format(_T("data:%sHz %s %dbit"), wavbit1, (wavch == 1) ? _T("mono") : _T("stereo"), wavsam);
		if (wavch == 3)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("3ch"), wavsam);
		if (wavch == 4)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("4ch"), wavsam);
		if (wavch == 5)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("4.1ch"), wavsam);
		if (wavch == 6)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("5.1ch"), wavsam);
		if (wavch == 7)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("6.1ch"), wavsam);
		if (wavch == 8)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("7.1ch"), wavsam);
		moji(s, 1, 48, 0x7fffff);
		sss = kpi;
		s.Format(_T("kpi :%s"), sss.Right(sss.GetLength() - sss.ReverseFind('\\') - 1));
		moji(s, 1, 64, 0x7fffff);
	}
	else if (mode == -8 || mode == -7) {
		s.Format(_T("data:%sHz %s %dbit"), wavbit1, (wavch == 1) ? _T("mono") : _T("stereo"), wavsam);
		if (wavch == 3)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("3ch"), wavsam);
		if (wavch == 4)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("4ch"), wavsam);
		if (wavch == 5)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("4.1ch"), wavsam);
		if (wavch == 6)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("5.1ch"), wavsam);
		if (wavch == 7)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("6.1ch"), wavsam);
		if (wavch == 8)s.Format(_T("data:%sHz %s %dbit"), wavbit1, _T("7.1ch"), wavsam);
		moji(s, 1, 48, 0x7fffff);
		s = "Arti:";
		moji(s, 1, 64, 0x7fffff);
		int si = mojisub(tagname, 1, 0, 0x7fffff);
		if (si > MDC) {
			ss = fnn + _T("》---《");
			if (mode == -8 || mode == -7) ss = tagname + _T("》---《");
			si = mojisub(ss, 1, 0, 0x7fffff);
		}
		//枠はみ出し時スクロール処理
		if (si > MDC) {
			dc.BitBlt(8 * 5 * 4, 0 + 64 * 4, 88 * 2 * 4 + 1000, (16 + 64) * 4, &dcsub, mcnt4, 0, SRCINVERT);
			if (si - mcnt4 < MDC) {
				mcnt3++;
				mcnt3++;
				mcnt3++;
				mcnt3++;
				dc.BitBlt(MDC - mcnt3 + 8 * 5 * 4, 0 + 64 * 4, 88 * 2 * 4 + 1000, (16 + 64) * 4, &dcsub, 0, 0, SRCINVERT);
				if (MDC - mcnt3 <= 0) { mcnt3 = 0; mcnt4 = 0; }
			}
			else mcnt3 = 0;
			mcnt4++;
			mcnt4++;
			mcnt4++;
			mcnt4++;
		}
		else {
			dc.BitBlt(8 * 5 * 4, 0 + 64 * 4, 88 * 2 * 4 + 1000, (16 + 64) * 4, &dcsub, 0, 0, SRCINVERT);
		}
	}
	else if (mode == -10 || mode == -9) {
		if (Vbr & mode == -10)
			s.Format(_T("data:%3dk(VBR) %sHz %dbit"), (kbps == 0) ? mkps : kbps, wavb(si1.dwSamplesPerSec), wavsam);
		else
			if (mode == -9)
				if (((kbps == 0) ? mkps : kbps) == 0)
					s.Format(_T("data:%sHz %dch %dbit (ALAC)"), wavb(si1.dwSamplesPerSec), wavch, wavsam);
				else
					if (Vbr)
						s.Format(_T("data:%3dk(VBR) %sHz %dch %dbit (AAC)"), mkps, wavb(si1.dwSamplesPerSec), wavch, wavsam);
					else
						s.Format(_T("data:%3dk(CBR) %sHz %dch %dbit (AAC)"), mkps, wavb(si1.dwSamplesPerSec), wavch, wavsam);
			else
				s.Format(_T("data:%3dk %sHz %dbit"), (kbps == 0) ? mkps : kbps, wavb(si1.dwSamplesPerSec), wavsam);
		moji(s, 1, 48, 0x7fffff);
		s = "Arti:";
		moji(s, 1, 64, 0x7fffff);
		//			dcsub.FillSolidRect(0,0,3000,30,RGB(1,1,1));
		int si = mojisub(tagname, 1, 0, 0x7fffff);
		if (si > MDC) {
			ss = fnn + _T("》---《");
			if (mode == -10 || mode == -9) ss = tagname + _T("》---《");
			si = mojisub(ss, 1, 0, 0x7fffff);
		}
		//枠はみ出し時スクロール処理
		if (si > MDC) {
			dc.BitBlt(8 * 5 * 4, 0 + 64 * 4, 88 * 2 * 4 + 1000, (16 + 64) * 4, &dcsub, mcnt4, 0, SRCINVERT);
			if (si - mcnt4 < MDC) {
				mcnt3++;
				mcnt3++;
				mcnt3++;
				mcnt3++;
				dc.BitBlt(MDC - mcnt3 + 8 * 5 * 4, 0 + 64 * 4, 88 * 2 * 4 + 1000, (16 + 64) * 4, &dcsub, 0, 0, SRCINVERT);
				if (MDC - mcnt3 <= 0) { mcnt3 = 0; mcnt4 = 0; }
			}
			else mcnt3 = 0;
			mcnt4++;
			mcnt4++;
			mcnt4++;
			mcnt4++;
		}
		else {
			dc.BitBlt(8 * 5 * 4, 0 + 64 * 4, 88 * 2 * 4 + 1000, (16 + 64) * 4, &dcsub, 0, 0, SRCINVERT);
		}
	}
	else {
		s.Format(_T("Loop:%2d:%02d.%02d %2d:%02d.%02d"), tal1, tbl1, tcl1, tal2, tbl2, tcl2);
		moji(s, 1, 48, 0x7fffff);
		if (loop1 < 10000000000)
			s.Format(_T("    :%10d-%6d"), loop1, loop2);
		if (loop1 < 1000000000)
			s.Format(_T("    :%9d-%7d"), loop1, loop2);
		if (loop1 < 100000000)
			s.Format(_T("    :%8d-%8d"), loop1, loop2);
		if (loop1 < 10000000)
			s.Format(_T("    :%7d-%9d"), loop1, loop2);
		moji(s, 1, 64, 0x7fefef);
	}
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7) {
		s = "Albu:";
		moji(s, 1, 80, 0x7fffff);
		//			dcsub.FillSolidRect(0,0,3000,30,RGB(1,1,1));
		int si = mojisub(tagalbum, 1, 0, 0x7fffff);
		if (si > MDC) {
			ss = tagalbum + _T("》---《");
			si = mojisub(ss, 1, 0, 0x7fffff);
		}
		//枠はみ出し時スクロール処理
		if (si > MDC) {
			dc.BitBlt(8 * 5 * 4, 0 + 80 * 4, 88 * 2 * 4 + 1000, (16 + 80) * 4, &dcsub, mcnt6, 0, SRCINVERT);
			if (si - mcnt6 < MDC) {
				mcnt5++;
				mcnt5++;
				mcnt5++;
				mcnt5++;
				dc.BitBlt(MDC - mcnt5 + 8 * 5 * 4, 0 + 80 * 4, 88 * 2 * 4 + 1000, (16 + 80) * 4, &dcsub, 0, 0, SRCINVERT);
				if (MDC - mcnt5 <= 0) { mcnt5 = 0; mcnt6 = 0; }
			}
			else mcnt5 = 0;
			mcnt6++;
			mcnt6++;
			mcnt6++;
			mcnt6++;
		}
		else {
			dc.BitBlt(8 * 5 * 4, 0 + 80 * 4, 88 * 2 * 4 + 1000, (16 + 80) * 4, &dcsub, 0, 0, SRCINVERT);
		}
	}
	else {
		if (tcg < 50)
			s.Format(_T("Loop数:%3d G:%3d:%02d.%02d"), loopcnt, tag, tbg, tcg);
		else
			s.Format(_T("Loop数:%3d G:%3d:%02d %02d"), loopcnt, tag, tbg, tcg);
		moji(s, 1, 80, 0xefefef);
	}
	if (ss != s)
	{
		ss = s;
		//			m_11.SetWindowText(s);
	}

	//	}




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

	if (pl && plw) {
		if (pl->m_renzoku.GetCheck() == TRUE) {
			CString s; m_kaisuu.GetWindowText(s);
			if (loopcnt >= _tstoi(s)) OnButton5();
		}
	}

	RECT rect;
	rect.top = 0;
	rect.left = 0;
	rect.bottom = (LONG)((101) * hD * 4);
	rect.right = (LONG)((180 + 88 * 2 + 50) * hD * 4);
	if (savedata.ms2 <= ms2) {
		InvalidateRect(&rect, FALSE);
	}
	//音量
	//	if(tt>=4){
	float vol = (float)m_sl.GetPos();
	vol /= 1000.0f;
	if (plf == 1) {
		if (deve == NULL) {
			WORD leftv = (WORD)(0xFFFF * vol);
			WORD rightv = (WORD)(0xFFFF * vol);
			waveOutSetVolume(hwo, MAKELONG(leftv, rightv));
		}
		else {
			audio->SetMasterVolumeLevelScalar(vol / 100.0f, &GUID_NULL);
		}
	}
	if (deve)
		s.Format(_T("%3d%%"), (int)vol);
	else
		s.Format(_T("%3d%%"), (int)(vol * 100));
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
		if (mode == -10) {
			m_time.SetPos((int)playb / 400);
			if (ptl) {
				ptl->SetProgressState(m_hWnd, TBPF_NORMAL);
				ptl->SetProgressValue(m_hWnd, (LONGLONG)playb / 4, (LONGLONG)oggsize);
			}
		}
		else {
			m_time.SetPos((int)playb);
			if (ptl) {
				ptl->SetProgressState(m_hWnd, TBPF_NORMAL);
				ptl->SetProgressValue(m_hWnd, (LONGLONG)playb, (LONGLONG)oggsize / (wavsam / 4));
			}
		}
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
	s.Format(_T("%3d%%"), savedata.kakuVal);
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
						if (f.Open(sf, CFile::modeRead | CFile::typeBinary, NULL)) {
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
						if (f.Open(sf, CFile::modeRead | CFile::typeBinary, NULL)) {
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
						if (f.Open(sf, CFile::modeRead | CFile::typeText, NULL)) {
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
																if (f.Open(sf, CFile::modeRead | CFile::typeBinary, NULL)) {
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
																	if (f.Open(sf, CFile::modeRead | CFile::typeBinary, NULL)) {
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
																		if (f.Open(sf, CFile::modeRead | CFile::typeText, NULL)) {
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
			CString s; m_kaisuu.GetWindowText(s);
			if (loopcnt >= _tstoi(s)) OnButton5();
		}
	}

	savedata.dsvol = m_dsval.GetPos();
	if (savedata.dsvol == 0)savedata.dsvol = 1;
	s.Format(_T("%3d%%"), (savedata.dsvol + 499) * 2 / 10);
	m_dsvols.GetWindowText(ss);
	if (s != ss)
		m_dsvols.SetWindowText(s);
	if (m_dsb && thn1 == FALSE) {
		if (savedata.dsvol == -498)
			m_dsb->SetVolume(DSBVOLUME_MIN);
		else
			m_dsb->SetVolume((savedata.dsvol - 1) * 7);
	}
	if (pBasicAudio) {
		if (savedata.dsvol == -498)
			pBasicAudio->put_Volume(-10000);
		else
			pBasicAudio->put_Volume((savedata.dsvol - 1) * 7);
	}

}

int timerr = 0;
int tim = 0;
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
			if (!::IsWindow(og->m_EqualizerDlg.GetSafeHwnd()))
			{
				og->m_EqualizerDlg.Create(IDD_EQUALIZER, og);
			}

			og->m_EqualizerDlg.ShowWindow(SW_SHOW);
		}

	}

	if (nIDEvent == 4923) {
		og->KillTimer(4923);
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
				for (; plw == 0;) { DoEvent(); }
			}
			og->dp(ndd);
		}
	}
	if (nIDEvent == 9000) {
		if (savedata.saverenzoku == 1 && endflg == 1) {
			plcnt++;
			if (plcnt < pl->m_lc.GetItemCount()) {
				pl->Get(plcnt);
				pl->SIcon(plcnt);
				fade1 = 0; lenl = 0;
				fade = 1.0f; plf = 0;
				og->KillTimer(9000);
				og->play();
			}
		}
	}
	if (nIDEvent == 6555) {
		if (pl)
			pl->SIconTimer(SC1);
		SC1++; SC1 = SC1 % 2;
	}
	if (nIDEvent == 5211) {
		og->KillTimer(5211);
		if (savedata.pl == 1) {
			pl = new CPlayList;
			pl->Create(og);
			if (!plw) {
				for (;;) { if (plw) break; }
			}
			if (pl && plw && filen != "" && !(wavbit == 0 || wavch == 0 || wavsam == 0)) {
				int plc;
				if (mode == -10)
					plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, oggsize / (2 * wavch * wavbit / 4), 1);
				else if (mode == -9 || mode == -8) {
					double wavv[] = { 0,1.0,2.0,2.0,2.0,2.0,2.0 };//(double)(wavbit2/wavv[wavch])
					plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, (int)(
						(double)oggsize / (double)(wavbit * 2 * wavv[wavch]) / (double)(wavsam / 16.0f)
						), 1);
				}
				else if (mode == -3) {
					if (oggsize == 0)
						plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, -1, 1);
					else
						plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, oggsize / (2 * wavch * wavbit), 1);
				}
				else
					plc = pl->Add(fnn, mode, loop1, loop2, tagname, tagalbum, filen, ret2, oggsize / (2 * wavch * wavbit));
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

	if (nIDEvent == 5219) {
		og->KillTimer(5219);
		THUMBBUTTON b[4];
		b[0].hIcon = ::AfxGetApp()->LoadIcon(IDI_T1);	b[0].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
		b[0].dwFlags = THBF_ENABLED;	b[0].iId = 0;
		wcscpy(b[0].szTip, L"再演奏");
		b[1].hIcon = ::AfxGetApp()->LoadIcon(IDI_T2);	b[1].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
		b[1].dwFlags = THBF_ENABLED;	b[1].iId = 1;
		wcscpy(b[1].szTip, L"一時停止");
		b[2].hIcon = ::AfxGetApp()->LoadIcon(IDI_T3);	b[2].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
		b[2].dwFlags = THBF_ENABLED;	b[2].iId = 2;
		wcscpy(b[2].szTip, L"停止");
		b[3].hIcon = ::AfxGetApp()->LoadIcon(IDI_T4);	b[3].dwMask = THB_ICON | THB_TOOLTIP | THB_FLAGS;
		b[3].dwFlags = THBF_ENABLED;	b[3].iId = 3;
		wcscpy(b[3].szTip, L"プレイリスト開閉");
		if (ptl)ptl->ThumbBarAddButtons(og->m_hWnd, 4, b);

	}
	if (nIDEvent == 5656) {
		for (int i = 0; i < 300; i++)spetm[i] = 1;
	}

	if (nIDEvent == 5657) {
		int i;
		for (i = 0; i < 300; i++)
			if (spetm[i] == 1) spelv[i]--;
		if (spelv[i] < 0) spelv[i] = 0;
	}

	if (nIDEvent == 1233) {
		if (tim == 0) {
			tim = 1;
			og->timerp();
			tim = 0;
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

		if (savedata.aero) {
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

	if (nIDEvent == 1251) {
		if (pMediaPosition && pl && plw) {
			if ((mode == -2 || videoonly == TRUE)) {
				REFTIME aa, bb;
				pMediaPosition->get_CurrentPosition(&aa);
				pMediaPosition->get_Duration(&bb);
				if (aa >= bb) {
					if (pl->m_renzoku.GetCheck() == TRUE) {
						plcnt++;
						if (plcnt < pl->m_lc.GetItemCount()) {
							pl->Get(plcnt);
							pl->SIcon(plcnt);
							og->KillTimer(1250);
							fade1 = 0; lenl = 0;
							fade = 1.0f; plf = 0;
							int m = mode;
							og->OnRestart();
						}
					}
				}
			}
		}

		if ((fade1 == 1 && pl && plw)) {
			if (pl->m_renzoku.GetCheck() == TRUE) {
				plcnt++;
				if (plcnt < pl->m_lc.GetItemCount()) {
					og->KillTimer(1250);
					pl->Get(plcnt);
					pl->SIcon(plcnt);
					thn = FALSE;
					for (int j = 0; j < 200; j++) {
						DoEvent();
						Sleep(10);
						if (thn == TRUE) {
							break;
						}
					}
					og->stop();
					fade1 = 0; lenl = 0;
					og->OnRestart();
					//					if(mode==-2){og->SetTimer(11251,300,NULL); ttt_=0;}
					//SendMessage(WM_USER+2,0,0);//OnRestart();
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
	CCustomDialog::OnTimer(nIDEvent);
}
LRESULT COggDlg::dp2(WPARAM, LPARAM)
{
	OnRestart();
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
		//		pMainFrame1->stop();
		//		pMainFrame1->DestroyWindow(); 
		::SendMessage(pMainFrame1->m_hWnd, WM_CLOSE, NULL, NULL);
		//		delete pMainFrame1;
		//動画画面が閉じるのを待つ
		for (; killw == 0;)
			DoEvent();
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
		if (_chdir("wave\\wave_22") == -1) { fnn = "ファイル又はフォルダがありません"; return; }
		ex = "_22";
	}
	else ex = "_44";
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
		if (f.Open(sf, CFile::modeRead | CFile::typeBinary, NULL)) {
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
		if (_chdir("wave\\wave_22") == -1) { fnn = "ファイル又はフォルダがありません"; return; }
		ex = "_22";
	}
	else ex = "_44";
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
		if (f.Open(sf, CFile::modeRead | CFile::typeBinary, NULL)) {
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
		if (_chdir("WAVE\\WAVE22") == -1) { fnn = "ファイル又はフォルダがありません"; return; }
		ex = "_22";
	}
	else ex = "_44";
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
		if (f.Open(sf, CFile::modeRead | CFile::typeText, NULL)) {
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
			if (f.Open(filen, CFile::modeRead, NULL) == 0) {
				filen.Format(_T("%smusic.mp3"), a->ret);
				if (f.Open(filen, CFile::modeRead, NULL) == 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
	if (ret != 0) { fnn = "ファイル又はフォルダがありません"; return; }
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
		m_ps.SetWindowText(_T("再開"));
		ps = 1;
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
		m_ps.SetWindowText(_T("一時停止"));
		if (pMainFrame1 != NULL && (mode == -2 || (mode > 0 && videoonly == TRUE)))
		{
			pMainFrame1->pause(1);
		}
		ps = 0;
	}
}

BOOL COggDlg::PreTranslateMessage(MSG* pMsg)
{
	// CG: 以下のブロックはツールヒント コンポーネントによって追加されました
	{
		// ツールヒントにこのメッセージを処理させます
		m_tooltip.RelayEvent(pMsg);
	}
	return CCustomDialog::PreTranslateMessage(pMsg);	// CG: 以下のブロックはツールヒント コンポーネントによって追加されました
}

void COggDlg::OnOK()
{
	// TODO: この位置にその他の検証用のコードを追加してください
	stop();
	CCustomDialog::OnOK();
}
extern IMediaEvent* pMediaEvent;
void COggDlg::OnRestart()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	CString ti;
	stop();
	m_ps.EnableWindow(TRUE);
	if (filen != "") {
		ti = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		int sub_ = mode;
		if ((filen.Right(4).MakeLower() == ".ogg" || filen.Right(4) == ".OGG" || filen.Right(6).MakeLower() == ".qull3") && mode < 1) {
			modesub = -1;
			play();
		}
		else if ((filen.Right(4).MakeLower() == ".dsf" || filen.Right(5) == ".DSF" || filen.Right(4).MakeLower() == ".dff" || filen.Right(4) == ".DFF" || filen.Right(4).MakeLower() == ".wsd" || filen.Right(4) == ".WSD") && mode < 1) {
			modesub = -7;
			play();
		}
		else if ((filen.Right(5).MakeLower() == ".flac" || filen.Right(5) == ".FLAC" || filen.Right(7).MakeLower() == L".qull3h") && mode < 1) {
			modesub = -8;
			play();
		}
		else if ((filen.Right(4).MakeLower() == ".m4a" || filen.Right(4) == ".M4A" || filen.Right(4).MakeLower() == ".aac" || filen.Right(4) == ".AAC") && mode < 1) {
			modesub = -9;
			play();
		}
		else if (mode == -10 && (filen.Right(4).MakeLower() == ".mp3" || filen.Right(4) == ".MP3" || filen.Right(4).MakeLower() == ".mp2" || filen.Right(4) == ".MP2" ||
			filen.Right(4).MakeLower() == ".mp1" || filen.Right(4) == ".MP1" || filen.Right(4).MakeLower() == ".rmp" || filen.Right(4) == ".RMP")) {
			modesub = -10;
			play();
		}
		else if (mode == -2 || mode == -3) {
			playlistdata p;
			kpi[0] = 0;
			if (pl && plw) {
				p.sub = 0;
				CString ss, s;
				s = filen;
				ss = s.Left(s.ReverseFind(':') - 1);
				if (ss != "") s = ss;
				pl->plugs(s, &p, kpi, kvver);
				if (p.sub == -3) {//kb medua player
					s = kpi;
					ss = s.Left(s.ReverseFind('\\'));
					_tchdir(ss);
					//					hDLLk = LoadLibrary(kpi);
					//					pFunck = (pfnGetKMPModule)::GetProcAddress(hDLLk, "kmp_GetTestModule");
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
			if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE) {
				f123.Close();
				if (IDYES == MessageBox(_T("途中再生データが存在します。\n前回中断した部分から再生しますか？\nはい = 途中から再生\nいいえ = はじめから再生"), _T("再生確認"), MB_YESNO)) {
					flggg = 1;
				}
				else {
					CFile::Remove(filen + _T(".save"));
				}
			}
			if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE && flggg == 1) {
				f123.Close();
				if (pGraphBuilder)pMainFrame1->plays2();
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
					if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE) {
						f123.Read(&playb, sizeof(__int64));
						if (savedata.mp3orig) {
							mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch);
						}
						else {
							mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch);
						}
						f123.Close();
					}
				}
				if (mode == -2) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead, NULL) == TRUE) {
						f123.Read(&aa1_, sizeof(double));
						pMainFrame1->seek((LONGLONG)(((float)((float)aa1_ * 100.0f * 100000.0f))));
						f123.Close();
					}
				}
			}
			else {
				if (pGraphBuilder)pMainFrame1->plays2();
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
			ps = 0; m_ps.SetWindowText(_T("一時停止"));
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
				if (ff.Open(filen, CFile::modeRead | CFile::shareDenyNone, NULL) == TRUE) {
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
void COggDlg::Speana()
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
	memset(dtbl, 0, sizeof(dtbl));
	memset(dtatbl, 0, sizeof(dtatbl));

	// ---------------------------------------------------------
	// モード判定
	// ---------------------------------------------------------
	bool mode0_Note = (savedata.speanamode == 1 && savedata.speananum == 0);
	bool mode1_Low = (savedata.speanamode == 1 && savedata.speananum == 1);
	bool mode3_High = (savedata.speanamode == 1 && savedata.speananum == 3);
	bool mode4_Vox = (savedata.speanamode == 1 && savedata.speananum == 4);
	bool mode2_Std = (!mode0_Note && !mode1_Low && !mode3_High && !mode4_Vox);

	// ---------------------------------------------------------
	// パラメータ設定
	// ---------------------------------------------------------
	double sampleRate = (double)wavbit;
	if (sampleRate < 8000.0) sampleRate = 44100.0;

	int channels = wavch;
	if (channels < 1) channels = 2;

	int bitDepth = wavsam;
	int bytesPerSample = bitDepth / 8;
	if (bytesPerSample < 1) bytesPerSample = 2;

	int bytesPerFrame = bytesPerSample * channels;

	// 解析バッファサイズ
	// 低音解像度が必要なモード(0,1)はサイズを大きく取る
	int analysisSize = 4096;
	if (mode0_Note || mode1_Low) analysisSize = 8192;

	// タイミング調整 (Latency)
	// 未来のデータを読まないよう、十分に過去へ戻します。
	// 4096なら -800ms、8192なら -1600ms を基準とします。
	int latencySetting = (analysisSize == 8192) ? -1600 : -800;

	if (wavbit < 44100) {
		latencySetting = (int)((float)latencySetting * (44100.0f / (float)wavbit));
	}

	// 遅延バイト数（負の値）
	long latencyBytes = (long)(sampleRate * bytesPerFrame * latencySetting / 1000.0);

	int framesToRead = analysisSize;
	int fftSize = analysisSize;
	int bytesTotalToRead = framesToRead * bytesPerFrame;
	const int TOTAL_BUF_BYTES = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM;

	// バッファ長超過ガード (Safety Clamp)
	// レイテンシがバッファ全長を超えて周回(未来読み)しないよう制限
	long maxSafeLatency = -(long)(TOTAL_BUF_BYTES * 0.9) + bytesTotalToRead;
	if (latencyBytes < maxSafeLatency) {
		latencyBytes = maxSafeLatency;
	}

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

	std::fill(bufL.begin(), bufL.end(), 0.0);
	std::fill(bufR.begin(), bufR.end(), 0.0);
	std::fill(bufM.begin(), bufM.end(), 0.0);
	std::fill(bufResampled.begin(), bufResampled.end(), 0.0);
	std::fill(bufResampledR.begin(), bufResampledR.end(), 0.0);

	// ---------------------------------------------------------
	// データ読み込み (完全過去データ取得)
	// ---------------------------------------------------------
	HRESULT rett;
	if (m_dsb) rett = m_dsb->GetCurrentPosition(&PlayCursor, &WriteCursor);
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
			else {
				// ダウンミックス
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

	ResampleDouble(bufL.data(), framesToRead, bufResampled.data(), fftSize);
	ResampleDouble(bufR.data(), framesToRead, bufResampledR.data(), fftSize);
	for (int k = 0; k < fftSize; k++) bufM[k] = (bufResampled[k] + bufResampledR[k]) * 0.5;

	// =========================================================
	// ② 解析用データの読み込み (分離した専用バッファ)
	// =========================================================

	int keySize = 8192; // 常に高解像度
	int keyLatencyMs = -800 * 2; // 最適なオフセット
	long keyLatencyBytes = (long)(sampleRate * bytesPerFrame * keyLatencyMs / 1000.0);
	int keyBytesToRead = keySize * bytesPerFrame;

	// 解析専用バッファ
	static std::vector<double> bufKeyL, bufKeyR;
	static std::vector<char> rawBufKey;

	if (bufKeyL.size() < (size_t)keySize) {
		bufKeyL.resize(keySize); bufKeyR.resize(keySize);
	}
	if (rawBufKey.size() < (size_t)keyBytesToRead) {
		rawBufKey.resize(keyBytesToRead);
	}

	// 解析用の読み出し位置計算
	long readPosKey = (long)PlayCursor - keyBytesToRead + keyLatencyBytes;

	// 安全ガード
	long maxSafeKey = -(long)(TOTAL_BUF_BYTES * 0.9) + keyBytesToRead;
	if (keyLatencyBytes < maxSafeKey) {
		// 安全圏外なら読み出し位置を補正 (バッファの整合性優先)
		readPosKey = (long)PlayCursor - keyBytesToRead + maxSafeKey;
	}

	while (readPosKey < 0) readPosKey += TOTAL_BUF_BYTES;
	while (readPosKey >= TOTAL_BUF_BYTES) readPosKey -= TOTAL_BUF_BYTES;
	readPosKey -= (readPosKey % bytesPerFrame);

	// 解析用データコピー
	char* dstRawKey = rawBufKey.data();
	if (readPosKey + keyBytesToRead <= TOTAL_BUF_BYTES) {
		memcpy(dstRawKey, srcBufBase + readPosKey, keyBytesToRead);
	}
	else {
		int firstPart = TOTAL_BUF_BYTES - readPosKey;
		int secondPart = keyBytesToRead - firstPart;
		if (firstPart >= 0 && secondPart >= 0) {
			memcpy(dstRawKey, srcBufBase + readPosKey, firstPart);
			memcpy(dstRawKey + firstPart, srcBufBase, secondPart);
		}
	}

	// 解析用サンプル抽出 (bufKeyL, bufKeyRへ)
	for (i = 0; i < keySize; i++) {
		auto GetSampleValue = [&](const char* buf, int sampleIndex, int chIndex) -> double {
			int offset = sampleIndex * bytesPerFrame + chIndex * bytesPerSample;

			if (bitDepth == 16) {
				return (double)(*(short*)(buf + offset)) / 32768.0;
			}
			if (bitDepth == 24) {
				unsigned char* p = (unsigned char*)(buf + offset);
				int val = (p[0]) | (p[1] << 8) | (p[2] << 16);
				if (val & 0x800000) val |= 0xFF000000; // 24bit符号拡張
				return (double)val / 8388608.0;
			}
			if (bitDepth == 32) {
				return (double)(*(int*)(buf + offset)) / 2147483648.0;
			}
			if (bitDepth == 8) {
				return ((double)(*(unsigned char*)(buf + offset)) - 128.0) / 128.0;
			}
			return 0.0;
			};

		if (channels <= 2) {
			bufKeyL[i] = GetSampleValue(dstRawKey, i, 0);
			bufKeyR[i] = (channels > 1) ? GetSampleValue(dstRawKey, i, 1) : bufKeyL[i];
		}
		else {
			auto GetSampleValue = [&](const char* buf, int sampleIndex, int chIndex) -> double {
				int offset = sampleIndex * bytesPerFrame + chIndex * bytesPerSample;

				if (bitDepth == 16) {
					return (double)(*(short*)(buf + offset)) / 32768.0;
				}
				if (bitDepth == 24) {
					unsigned char* p = (unsigned char*)(buf + offset);
					int val = (p[0]) | (p[1] << 8) | (p[2] << 16);
					if (val & 0x800000) val |= 0xFF000000; // 24bit符号拡張
					return (double)val / 8388608.0;
				}
				if (bitDepth == 32) {
					return (double)(*(int*)(buf + offset)) / 2147483648.0;
				}
				if (bitDepth == 8) {
					return ((double)(*(unsigned char*)(buf + offset)) - 128.0) / 128.0;
				}
				return 0.0;
				};

			// ダウンミックス
			double fl = GetSampleValue(dstRawKey, i, 0);
			double fr = GetSampleValue(dstRawKey, i, 1);
			double c = (channels > 2) ? GetSampleValue(dstRawKey, i, 2) : 0.0;
			double lfe = (channels > 3) ? GetSampleValue(dstRawKey, i, 3) : 0.0;
			double rl = (channels > 4) ? GetSampleValue(dstRawKey, i, 4) : 0.0;
			double rr = (channels > 5) ? GetSampleValue(dstRawKey, i, 5) : 0.0;

			bufKeyL[i] = (fl + c * 0.7 + rl * 0.8 + lfe * 0.5) * 0.7;
			bufKeyR[i] = (fr + c * 0.7 + rr * 0.8 + lfe * 0.5) * 0.7;
		}
	}
	// ===== 音楽キー分析 =====
	AnalyzeMusicKey(bufL, bufR, (int)sampleRate);

	auto ValToBarHeight = [&](double amplitude) -> int {
		if (amplitude < 0.0001) return 0;
		double db = 20.0 * log10(amplitude);
		double h = (db + 60.0) * (96.0 / 60.0);
		if (h < 0) return 0; if (h > 96) return 96;
		return (int)h;
		};

	bool useFFT = (!mode0_Note);

	if (useFFT) {
		int N = fftSize;
		auto ApplyWindow = [&](const std::vector<double>& src, double* dst) {
			for (int n = 0; n < N; n++) {
				double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)n / (double)(N - 1));
				dst[n] = src[n] * w;
			}
			};

		if (m_st.GetCheck() == FALSE) {
			ApplyWindow(bufM, aFFT2); ipTab2[0] = 0; ddst(N, -1, aFFT2, ipTab2, wTab2);
		}
		else {
			ApplyWindow(bufResampled, aFFT2); ipTab2[0] = 0; ddst(N, -1, aFFT2, ipTab2, wTab2);
			ApplyWindow(bufResampledR, aFFT2a); ipTab2[0] = 0; ddst(N, -1, aFFT2a, ipTab2, wTab2);
		}
		aFFT2[0] = 0; aFFT2a[0] = 0; // DCカット

		double fs = 44100.0;
		double band_edges[DISP_KEYS + 1];
		int band_bins[DISP_KEYS + 1];
		double normFactor = 4.0 / fftSize;
		int splitIndex = DISP_KEYS / 2;

		// ========================================
		// モード1: 低周波部分特化
		// ========================================
		if (mode1_Low) {
			double lowStart = 20.0; double highEnd = 800.0;
			for (int k = 0; k <= DISP_KEYS; ++k) band_edges[k] = lowStart * pow(highEnd / lowStart, (double)k / DISP_KEYS);
		}
		// ========================================
		// モード3: 高周波数
		// ========================================
		else if (mode3_High) {
			double lowStart = 40.0; double splitFreq = 3000.0; double highEnd = 22000.0;
			for (int k = 0; k <= DISP_KEYS; ++k) {
				if (k <= splitIndex) band_edges[k] = lowStart * pow(splitFreq / lowStart, (double)k / splitIndex);
				else band_edges[k] = splitFreq * pow(highEnd / splitFreq, (double)(k - splitIndex) / (DISP_KEYS - splitIndex));
			}
		}
		// ========================================
		// モード2: 全音域カバー(標準) & モード4: 音声特化
		// ========================================
		else {
			double minFreq = 30.0; double maxFreq = 22000.0;
			for (int k = 0; k <= DISP_KEYS; ++k) band_edges[k] = minFreq * pow(maxFreq / minFreq, (double)k / DISP_KEYS);
		}

		for (int k = 0; k <= DISP_KEYS; ++k) {
			band_bins[k] = (int)(band_edges[k] * fftSize / fs);
			if (band_bins[k] < 0) band_bins[k] = 0;
			if (band_bins[k] >= fftSize / 2) band_bins[k] = fftSize / 2 - 1;
		}

		for (i = 0; i < DISP_KEYS; i++) {
			int bin_start = band_bins[i];
			int bin_end = band_bins[i + 1];
			if (bin_start < 1) bin_start = 1;
			if (bin_end <= bin_start) bin_end = bin_start + 1;
			if (bin_end > fftSize / 2) bin_end = fftSize / 2;

			dt = 0; dta = 0;
			if (m_st.GetCheck() == FALSE) {
				for (j = bin_start; j < bin_end; j++) if (dt < fabs(aFFT2[j])) dt = fabs(aFFT2[j]);
			}
			else {
				for (j = bin_start; j < bin_end; j++) {
					if (dt < fabs(aFFT2[j])) dt = fabs(aFFT2[j]);
					if (dta < fabs(aFFT2a[j])) dta = fabs(aFFT2a[j]);
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
		for (i = 0; i < DISP_KEYS; i++) {
			d = dtbl[i];
			if (spelv[i] < d) { spelv[i] = d; spetm[i] = 0; }
			int x_pos, bar_w;
			if (m_st.GetCheck() == FALSE) {
				x_pos = (21 * 8 + i * 2) * 4; bar_w = 8;
				if (spelv[i] > 0 || d > 0) {
					dc.FillSolidRect(x_pos, (96 - spelv[i]) * 4, bar_w, (spelv[i] + 1) * 4, RGB(0, 128, 0));
					dc.FillSolidRect(x_pos, (96 - d) * 4, bar_w, (d + 1) * 4, RGB(0, 255, 0));
					dc.FillSolidRect(x_pos, (96 - spelv[i]) * 4, bar_w, 4, RGB(255, 255, 0));
				}
			}
			else {
				d = dtbl[i];
				if (spelv[100 + i] < d) { spelv[100 + i] = d; spetm[100 + i] = 0; }
				x_pos = (21 * 8 + i) * 4; bar_w = 4;
				if (spelv[100 + i] > 0 || d > 0) {
					dc.FillSolidRect(x_pos, (96 - spelv[100 + i]) * 4, bar_w, (spelv[100 + i] + 1) * 4, RGB(0, 128, 0));
					dc.FillSolidRect(x_pos, (96 - d) * 4, bar_w, (d + 1) * 4, RGB(0, 255, 0));
					dc.FillSolidRect(x_pos, (96 - spelv[100 + i]) * 4, bar_w, 4, RGB(255, 255, 0));
				}
				d = dtatbl[i];
				if (spelv[200 + i] < d) { spelv[200 + i] = d; spetm[200 + i] = 0; }
				x_pos = (21 * 8 + 89 + i) * 4; bar_w = 4;
				if (spelv[200 + i] > 0 || d > 0) {
					dc.FillSolidRect(x_pos, (96 - spelv[200 + i]) * 4, bar_w, (spelv[200 + i] + 1) * 4, RGB(0, 128, 0));
					dc.FillSolidRect(x_pos, (96 - d) * 4, bar_w, (d + 1) * 4, RGB(0, 255, 0));
					dc.FillSolidRect(x_pos, (96 - spelv[200 + i]) * 4, bar_w, 4, RGB(255, 255, 0));
				}
				dc.FillSolidRect((21 * 8 + 88) * 4, 20, 4, 368, RGB(0, 255, 255));
			}
		}
	}

	// ========================================
	// モード0: 音階モード
	// ========================================
	else if (mode0_Note) {
		static std::vector<double> noteFreqsExpanded;
		static std::vector<double> goertzelCoeffs;
		static std::vector<double> blackmanWindow;
		static bool coeffsInit = false;

		if (!coeffsInit) {
			noteFreqsExpanded.resize(DETECT_KEYS);
			goertzelCoeffs.resize(DETECT_KEYS);
			blackmanWindow.resize(4096);

			for (int k = 0; k < DETECT_KEYS; ++k) {
				int midiNote = 12 + k;
				double freq = 440.0 * pow(2.0, (midiNote - 69.0) / 12.0);
				noteFreqsExpanded[k] = freq;
				goertzelCoeffs[k] = 2.0 * cos(2.0 * M_PI * freq / sampleRate);
			}
			for (int n = 0; n < 4096; ++n) {
				blackmanWindow[n] = 0.42 - 0.5 * cos(2.0 * M_PI * (double)n / 4095.0) + 0.08 * cos(4.0 * M_PI * (double)n / 4095.0);
			}
			coeffsInit = true;
		}

		auto ProcessGoertzel = [&](const std::vector<double>& input, int offset_idx, bool isRight) {
			std::vector<double> rawResults(DETECT_KEYS, 0.0);
			int maxLen = (int)input.size();

			// 低音域解析 (8192サンプル)
			const int LOW_KEY_LIMIT = 50;
			const int LEN_LOW = 8192;
			int useLenLow = (maxLen < LEN_LOW) ? maxLen : LEN_LOW;
			int startLow = maxLen - useLenLow;

			for (int k = 0; k < LOW_KEY_LIMIT; k++) {
				double coeff = goertzelCoeffs[k];
				double s_prev = 0.0, s_prev2 = 0.0;
				for (int n = 0; n < useLenLow; ++n) {
					double w = 0.5 - 0.5 * cos(2.0 * M_PI * n / (useLenLow - 1));
					double val = input[startLow + n] * w;
					double s = val + coeff * s_prev - s_prev2;
					s_prev2 = s_prev;
					s_prev = s;
				}
				double p = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
				rawResults[k] = sqrt(p > 0 ? p : 0) * 2.5 / useLenLow;
			}

			// 中高音域解析 (4096サンプル)
			const int LEN_HIGH = 4096;
			int startHigh = (maxLen > LEN_HIGH) ? (maxLen - LEN_HIGH) : 0;

			for (int k = LOW_KEY_LIMIT; k < DETECT_KEYS; k++) {
				double coeff = goertzelCoeffs[k];
				double s_prev = 0.0, s_prev2 = 0.0;
				for (int n = 0; n < LEN_HIGH; ++n) {
					double val = input[startHigh + n] * blackmanWindow[n];
					double s = val + coeff * s_prev - s_prev2;
					s_prev2 = s_prev;
					s_prev = s;
				}
				double p = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;
				rawResults[k] = sqrt(p > 0 ? p : 0) * 2.5 / LEN_HIGH;
			}

			// ダウンミックス & 描画
			std::vector<double> displayAmp(DISP_KEYS, 0.0);

			for (int k = 0; k < DETECT_KEYS; k++) {
				double amp = rawResults[k];

				// スロープ補正
				double slope = 1.0 + ((double)k / DETECT_KEYS) * 3.0;
				amp *= slope;

				// コントラスト (2乗)
				if (amp > 0.0001) {
					double boost = amp * 50.0;
					amp = boost * boost * 0.002;
					if (amp > 10.0) amp = 10.0;
				}
				else {
					amp = 0.0;
				}

				if (k < KEY_OFFSET) {
					if (amp > displayAmp[0]) displayAmp[0] = amp;
				}
				else if (k >= KEY_OFFSET + DISP_KEYS) {
					if (amp > displayAmp[DISP_KEYS - 1]) displayAmp[DISP_KEYS - 1] = amp;
				}
				else {
					displayAmp[k - KEY_OFFSET] = amp;
				}
			}

			for (int i = 0; i < DISP_KEYS; i++) {
				int h = ValToBarHeight(displayAmp[i] * savedata.wup);

				int idx = offset_idx + i;
				if (spelv[idx] < h) { spelv[idx] = h; spetm[idx] = 0; }
				d = h;

				int x, w;
				if (!isRight && m_st.GetCheck() == FALSE) { x = (21 * 8 + i * 2) * 4; w = 8; }
				else if (!isRight) { x = (21 * 8 + i) * 4; w = 4; }
				else { x = (21 * 8 + 89 + i) * 4; w = 4; }

				if (d > 0 || spelv[idx] > 0) {
					dc.FillSolidRect(x, (96 - spelv[idx]) * 4, w, (spelv[idx] + 1) * 4, RGB(0, 128, 0));
					dc.FillSolidRect(x, (96 - d) * 4, w, (d + 1) * 4, RGB(0, 255, 0));
					dc.FillSolidRect(x, (96 - spelv[idx]) * 4, w, 4, RGB(255, 255, 0));
				}
			}
			};

		if (m_st.GetCheck() == FALSE) {
			std::vector<double> monoInput(framesToRead);
			for (int k = 0; k < framesToRead; k++) monoInput[k] = (bufL[k] + bufR[k]) * 0.5;
			ProcessGoertzel(monoInput, 0, false);
		}
		else {
			ProcessGoertzel(bufL, 100, false);
			ProcessGoertzel(bufR, 200, true);
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


void COggDlg::moji(CString s, int x, int y, COLORREF rgb)
{
	CRect rect;
	HFONT fo;
	SIZE szinfo;
	fo = (HFONT)SelectObject(dc, hFont);
	SetTextColor(dc, rgb);
	SetBkColor(dc, RGB(0, 0, 0));
	dc.SetBkMode(TRANSPARENT);
	GetTextExtentPoint32(dc, s, s.GetLength(), &szinfo);
	if (savedata.ms2 <= ms2) {
		dc.TextOut(x * 4, y * 4, s, s.GetLength());
	}
	SelectObject(dc, fo);
}

int COggDlg::mojisub(CString s, int x, int y, COLORREF rgb)
{
	CRect rect;
	HFONT fo;
	CSize szinfo;
	fo = (HFONT)SelectObject(dcsub, hFont);
	SetTextColor(dcsub, rgb);
	SetBkColor(dcsub, RGB(0, 0, 0));
	SetBkMode(dcsub, TRANSPARENT);
	szinfo = dcsub.GetOutputTextExtent(s);
	if (savedata.ms2 <= ms2) {
		if (szinfo.cx < (MDC + 8) * 4)
			dcsub.FillSolidRect(0, 0, (MDC + 8) * 4, 30 * 4, RGB(0, 0, 0));
		else
			dcsub.FillSolidRect(0, 0, (szinfo.cx + MDC + 8) * 4, 30 * 4, RGB(0, 0, 0));
		dcsub.TextOut(x * 4, y * 4, s, s.GetLength());
		SelectObject(dcsub, fo);
	}
	return szinfo.cx;
}

void COggDlg::OnButton9_Folder()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	CFolder* a = new CFolder(CWnd::FromHandle(GetSafeHwnd()));
	if (savedata.aero) {
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
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite, NULL) == TRUE) {
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
}

void COggDlg::OnCheck6()
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	m_junji.SetCheck(1);
	m_random.SetCheck(0);
	savedata.random = 1;
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
void COggDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar * pScrollBar)
{
	{
		CSliderCtrl* r = (CSliderCtrl*)pScrollBar;
		if (r->GetDlgCtrlID() != IDC_SLIDER2) return;
	}

	for (;;) {
		if (sflg == FALSE) break;
		DoEvent();
	}

	int minpos;
	int maxpos;
	int curpos;
	double aa;
	if (wavsam == 32) aa = 2000.0;
	if (wavsam == 24) aa = 2000.0;
	if (wavsam == 16) aa = 2000.0;
	if (wavsam == 8) aa = 2000.0;

	if (nSBCode == SB_THUMBTRACK) {
		CSliderCtrl* r = (CSliderCtrl*)pScrollBar;
		r->GetRange(minpos, maxpos);
		switch (r->GetDlgCtrlID()) {
		case IDC_SLIDER2:
			hsc = 1; sflg = FALSE; return;
			break;
		}
	}
	if (nSBCode == SB_PAGELEFT) {
		CSliderCtrl* r = (CSliderCtrl*)pScrollBar;
		r->GetRange(minpos, maxpos);
		if (g_rubberBandStretcher) {
			delete g_rubberBandStretcher;
			g_rubberBandStretcher = NULL;
		}

		switch (r->GetDlgCtrlID()) {
		case IDC_SLIDER2:
			CCriticalLock _ccl(&cs);
			int info;
			info = r->GetLineSize();
			curpos = r->GetPos();
			if (curpos > minpos) curpos = max(minpos, curpos - info);
			//			playb=curpos;
			if (pMediaPosition && (mode == -2 || (mode > 0 && videoonly == TRUE))) {
				r->SetPos((int)curpos);
				if (aa2 == 0) {
					pMainFrame1->seek((LONGLONG)(((float)((float)curpos) * 100000.0f)));
				}
				else {
					pMainFrame1->seek((LONGLONG)(((float)((float)curpos) * (100000.0f))));
				}
			}
			else {
				if ((loop1 + loop2) < curpos && endf == 0) curpos = (loop1 + loop2);
				r->SetPos((int)curpos);
				if (pMainFrame1) {
					pMainFrame1->seek((LONGLONG)(((float)((float)curpos) * 10000000.0f) / (float)wavbit));
					//				pMainFrame1->seek((long)(((float)((float)playb)*29.97f)/(44100.0f)));
				}
				if ((mode >= 10 && mode <= 21) || mode <= -10) {
					if (mode == -10) {
						hsc = 2;
						//						return;
						//						playb*=4;
						//						r->SetPos((int)playb/4);
						//						playb-=wavbit*40;
						playb = curpos * 400;
						if (playb < 0)playb = 0;
						if (ps == 0) {
							OnPause();
							ZeroMemory(bufwav3, sizeof(bufwav3));
							syukai = 1; syukai2 = 0;
							if (thn == FALSE) for (;;) { if (syukai2 == 1)break; DoEvent(); }
							if (savedata.mp3orig) {
								if (mp3_.seek2((__int64)(playb / ((wavch == 2 ? 4 : 1) * (wavsam / 16.0f))), (DWORD)(wavch)) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return; }
							}
							else {
								if (mp3_.seek((__int64)(playb / ((wavch == 2 ? 4 : 1) * (wavsam / 16.0f))), (DWORD)(wavch)) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return; }
							}
							poss = 0; sek = TRUE;
							cnt3 = 0;
							timer.SetEvent();
							syukai = 0;
							OnPause();
						}
						else
							if (savedata.mp3orig) {
								poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
								ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								if (mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return; }
							}
							else {
								poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
								ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								if (mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return; }
							}
					}
					else {
						playb = curpos;
						seekadpcm((int)playb);
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}

				}
				else if (mode == -3) {
					playb = curpos;
					if (mod) {
						if (mod->SetPosition && sikpi.dwSeekable) mod->SetPosition(kmp1, (DWORD)((double)playb / (((double)wavbit * (double)wavch) / 2000.0)));
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -7) {
					playb = curpos;
					if (1) {
						if (m_dsb)m_dsb->Stop();
						dsd_.kpiSetPosition(kmp, (DWORD)((double)playb / ((((double)wavbit) * (double)wavch) / 2000.0)));							sek = TRUE;
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -8) {
					playb = curpos;
					if (1) {
						sek4 = TRUE;
						if (flacmode == 1)
							flac_.SetPosition(kmp, playb);
						else
							flac_.SetPosition(kmp, (((LONGLONG)((double)playb / (((double)wavbit * (double)wavch) / (aa))))));
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						sek4 = FALSE;
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -6 || mode == 30) {
					playb = curpos;
					poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
					seekadpcm((int)playb);
					sek = TRUE;
					cnt3 = 0;
					timer.SetEvent();
				}
				else if (mode == -9) {
					playb = curpos;
					if (1) {
						double wavv2[] = { 0,2.0,1.0,1.0 / 2.0,1.0 / 2.0,1.0 / 2.0,1.0 / 2.0 };
						DWORD pla = (DWORD)((double)playb / ((((double)(wavbit2 / wavv2[wavch]))) / ((wavch > 2) ? (1069.1 * wavch) : 1000.0)));
						pla = ((pla / (wavch * 2) * (wavch * 2)));
						m4a_.SetPosition(kmp, pla);
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else {
					for (; oggyomikomi;) {
						DoEvent();
						Sleep(1);
					}
					playb = curpos;
					ov_pcm_seek(&vf, (ogg_int64_t)playb);
					poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
					poss5 = curpos;
					ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					sek = TRUE;
					cnt3 = 0;
					timer.SetEvent();
				}
				poss = 0;
				//			playl+=whsize;
				hsc = 0;
			}
			_ccl.Leave();
			break;
		}
		return;
	}
	if (nSBCode == SB_PAGERIGHT) {
		CSliderCtrl* r = (CSliderCtrl*)pScrollBar;
		r->GetRange(minpos, maxpos);
		if (g_rubberBandStretcher) {
			delete g_rubberBandStretcher;
			g_rubberBandStretcher = NULL;
		}

		switch (r->GetDlgCtrlID()) {
		case IDC_SLIDER2:
			CCriticalLock _ccl(&cs);
			int info;
			info = r->GetLineSize();
			curpos = r->GetPos();
			if (curpos < maxpos) { curpos = min(maxpos, curpos + info); }
			else { hsc = 2; sflg = FALSE; return; }//
			//			playb=curpos;
			if (pMediaPosition && (mode == -2 || (mode > 0 && videoonly == TRUE))) {
				r->SetPos((int)curpos);
				if (aa2 == 0) {
					pMainFrame1->seek((LONGLONG)(((float)((float)curpos) * 100000.0f)));
				}
				else {
					pMainFrame1->seek((LONGLONG)(((float)((float)curpos) * (100000.0f))));
				}
			}
			else {
				if ((loop1 + loop2) < curpos && endf == 0) curpos = (loop1 + loop2);
				r->SetPos((int)curpos);
				if (pMainFrame1) {
					pMainFrame1->seek((LONGLONG)(((float)((float)curpos) * 10000000.0f) / (float)wavbit));
					//				pMainFrame1->seek((long)(((float)((float)playb)*29.97f)/(44100.0f)));
				}
				if ((mode >= 10 && mode <= 21) || mode <= -10) {
					if (mode == -10) {
						hsc = 2;
						//						return;
						playb = curpos * 400;
						//						playb*=4;
						//						playb+=wavbit*40;
						//r->SetPos((int)playb/4);
						if (ps == 0) {
							OnPause();
							ZeroMemory(bufwav3, sizeof(bufwav3));
							syukai = 1; syukai2 = 0;
							if (thn == FALSE) for (;;) { if (syukai2 == 1)break; DoEvent(); }
							if (savedata.mp3orig) {
								if (mp3_.seek2((__int64)(playb / ((wavch == 2 ? 4 : 1) * (wavsam / 16.0f))), (DWORD)(wavch)) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return; }
							}
							else {
								if (mp3_.seek((__int64)(playb / ((wavch == 2 ? 4 : 1) * (wavsam / 16.0f))), (DWORD)(wavch)) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return; }
							}
							poss = 0; sek = TRUE;
							cnt3 = 0;
							timer.SetEvent();
							syukai = 0;
							OnPause();
						}
						else
							if (savedata.mp3orig) {
								poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
								ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								if (mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return; }
							}
							else {
								poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
								ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								if (mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return; }
							}
					}
					else {
						playb = curpos;
						seekadpcm((int)playb);
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -3) {
					playb = curpos;
					if (mod) {
						if (mod->SetPosition && sikpi.dwSeekable) mod->SetPosition(kmp1, (DWORD)((double)playb / (((double)wavbit * (double)wavch) / 2000.0)));
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -7) {
					playb = curpos;
					if (1) {
						if (m_dsb)m_dsb->Stop();
						dsd_.kpiSetPosition(kmp, (DWORD)((double)playb / ((((double)wavbit) * (double)wavch) / 2000.0)));							sek = TRUE;
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -6 || mode == 30) {
					playb = curpos;
					poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
					seekadpcm((int)playb);
					sek = TRUE;
					cnt3 = 0;
					timer.SetEvent();
				}
				else if (mode == -8) {
					playb = curpos;
					if (1) {
						sek4 = TRUE;
						if (flacmode == 1)
							flac_.SetPosition(kmp, playb);
						else
							flac_.SetPosition(kmp, (((LONGLONG)((double)playb / (((double)wavbit * (double)wavch) / (aa))))));
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						sek4 = FALSE;
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -9) {
					playb = curpos;
					if (1) {
						double wavv2[] = { 0,2.0,1.0,1.0 / 2.0,1.0 / 2.0,1.0 / 2.0,1.0 / 2.0 };
						DWORD pla = (DWORD)((double)playb / ((((double)(wavbit2 / wavv2[wavch]))) / ((wavch > 2) ? (1069.1 * wavch) : 1000.0)));
						pla = ((pla / (wavch * 2) * (wavch * 2)));
						m4a_.SetPosition(kmp, pla);
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else {
					for (; oggyomikomi;) {
						DoEvent();
						Sleep(1);
					}
					playb = curpos;
					ov_pcm_seek(&vf, (ogg_int64_t)playb);
					poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
					ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					poss5 = curpos;
					sek = TRUE;
					cnt3 = 0;
					timer.SetEvent();
				}
				poss = 0;
				//			playl+=whsize;
				hsc = 0;
			}
			_ccl.Leave();
			break;
		}
		return;
	}

	if (nSBCode == SB_ENDSCROLL) {
		//if (hsc == 2) { hsc = 0; sflg = FALSE; return; }
		CSliderCtrl* r = (CSliderCtrl*)pScrollBar;
		if (g_rubberBandStretcher) {
			delete g_rubberBandStretcher;
			g_rubberBandStretcher = NULL;
		}

		switch (r->GetDlgCtrlID()) {
		case IDC_SLIDER2:
			CCriticalLock _ccl(&cs);
			playb = r->GetPos();
			if (pMediaPosition && (mode == -2 || (mode > 0 && videoonly == TRUE))) {
				r->SetPos((int)playb);
				if (aa2 == 0) {
					pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 100000.0f)));
				}
				else {
					pMainFrame1->seek((LONGLONG)(((float)((float)playb) * (100000.0f))));
				}
				hsc = 0;
			}
			else {
				if ((loop1 + loop2) < playb && endf == 0) playb = (loop1 + loop2);
				r->SetPos((int)playb);
				if (pMainFrame1) {
					pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit));
					//				pMainFrame1->seek((long)(((float)((float)playb)*29.97f)/(44100.0f)));
				}
				if ((mode >= 10 && mode <= 21) || mode <= -10) {
					if (mode == -10) {
						playb *= 400;
						r->SetPos((int)playb / 400);
						if (ps == 0) {
							OnPause();
							ZeroMemory(bufwav3, sizeof(bufwav3));
							syukai = 1; syukai2 = 0;
							if (thn == FALSE) for (;;) { if (syukai2 == 1)break; DoEvent(); }
							if (savedata.mp3orig) {
								if (mp3_.seek2((__int64)(playb / ((wavch == 2 ? 4 : 1) * (wavsam / 16.0f))), (DWORD)(wavch)) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return; }
							}
							else {
								if (mp3_.seek((__int64)(playb / ((wavch == 2 ? 4 : 1) * (wavsam / 16.0f))), (DWORD)(wavch)) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return; }
							}
							poss = 0; sek = TRUE;
							cnt3 = 0;
							timer.SetEvent();
							syukai = 0;
							OnPause();
						}
						else
							if (savedata.mp3orig) {
								poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
								ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								if (mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return; }
							}
							else {
								poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
								ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
								if (mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return; }
							}
					}
					else {
						seekadpcm((int)playb);
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -3) {
					if (mod) {
						if (mod->SetPosition && sikpi.dwSeekable) mod->SetPosition(kmp1, (DWORD)((double)playb / ((((double)wavbit) * (double)wavch) / 2000.0)));
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -7) {

					if (sek == FALSE) {
						if (m_dsb)m_dsb->Stop();
						dsd_.kpiSetPosition(kmp, (DWORD)((double)playb / ((((double)wavbit) * (double)wavch) / 2000.0)));							sek = TRUE;
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -6 || mode == 30) {

					poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
					seekadpcm((int)playb);
					ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					sek = TRUE;
					cnt3 = 0;
					timer.SetEvent();
				}
				else if (mode == -8) {
					if (1) {
						sek4 = TRUE;
						if (flacmode == 1)
							flac_.SetPosition(kmp, playb);
						else
							flac_.SetPosition(kmp, (((LONGLONG)((double)playb / (((double)wavbit * (double)wavch) / (aa))))));
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						sek4 = FALSE;
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else if (mode == -9) {
					if (1) {
						double wavv2[] = { 0,2.0,1.0,1.0 / 3.0,1.0 / 4.0,1.0 / 5.0,1.0 / 6.0 };
						DWORD pla = (DWORD)((double)playb / ((((double)(wavbit2 / wavv2[wavch]))) / ((wavch > 2) ? (1069.1 * wavch) : 1000.0)));
						pla = ((pla / (wavch * 2) * (wavch * 2)));
						poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
						ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
						m4a_.SetPosition(kmp, pla);
						sek = TRUE;
						cnt3 = 0;
						timer.SetEvent();
					}
				}
				else {
					for (; oggyomikomi;) {
						DoEvent();
						Sleep(1);
					}
					ov_pcm_seek(&vf, (ogg_int64_t)playb);
					poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
					ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
					poss5 = playb;
					sek = TRUE;
					cnt3 = 0;
					timer.SetEvent();
				}
				poss = 0;
				//			playl+=whsize;
				hsc = 0;
			}
			_ccl.Leave();
			break;
		}
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
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite, NULL) == TRUE) {
#endif
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
	_chdir(tmp);
	delete r;
	}


void COggDlg::OnNMReleasedcaptureSlider2(NMHDR * pNMHDR, LRESULT * pResult)
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	*pResult = 0;
}



void COggDlg::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	CCustomDialog::OnKeyDown(nChar, nRepCnt, nFlags);
}

LRESULT COggDlg::OnHotKey(WPARAM wp, LPARAM a)
{
	switch (wp) {
	case 8000:
		m_dsval.SetPos(m_dsval.GetPos() + 5);
		break;
	case 8001:
		m_dsval.SetPos(m_dsval.GetPos() - 5);
		break;
	case 8002:
		if (!((ogg || adbuf2 || mod || wav) || mode == -2)) break;
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
			playb += wavbit * (wavch == 2 ? 10 : 5);
			if ((loop1 + loop2) < (int)playb && endf == 0) playb = (loop1 + loop2);
			if (mode != -10)m_time.SetPos((int)playb);
			if (pMainFrame1) {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit));
			}
			if ((mode >= 10 && mode <= 21) || mode <= -10) {
				if (mode == -10) {
					playb -= wavbit * (wavch == 2 ? 10 : 5);
					playb *= 400;
					playb += wavbit * (wavch == 2 ? 40 : 20);
					if (ps == 0) {
						OnPause();
						ZeroMemory(bufwav3, sizeof(bufwav3));
						syukai = 1; syukai2 = 0;
						if (thn == FALSE) for (;;) { if (syukai2 == 1)break; DoEvent(); }
						if (savedata.mp3orig) {
							if (mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return 0; }
						}
						else {
							if (mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return 0; }
						}
						poss = 0; sek = TRUE;
						timer.SetEvent();
						syukai = 0;
						OnPause();
					}
					else
						if (savedata.mp3orig) {
							if (mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return 0; }
						}
						else {
							if (mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return 0; }
						}
					//						m_time.SetPos((int)playb/400);
				}
				else {
					seekadpcm((int)playb);
					sek = TRUE;
					timer.SetEvent();
				}
			}
			else if (mode == -3) {
				if (mod) {
					if (mod->SetPosition && sikpi.dwSeekable) mod->SetPosition(kmp1, (DWORD)((double)playb / (((double)wavbit * (double)wavch) / 2000.0)));
					sek = TRUE;
					timer.SetEvent();
				}
			}
			else {
				ov_pcm_seek(&vf, (ogg_int64_t)playb);
				sek = TRUE;
				timer.SetEvent();
			}
			poss = 0;
		}
		break;
	case 8003:
		if (!((ogg || adbuf2 || mod || wav) || mode == -2)) break;
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
			playb -= wavbit * (wavch == 2 ? 10 : 5);
			if ((loop1 + loop2) < (int)playb && endf == 0) playb = (loop1 + loop2);
			if (mode != -10)m_time.SetPos((int)playb);
			if (pMainFrame1) {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit));
			}
			if ((mode >= 10 && mode <= 21) || mode <= -10) {
				if (mode == -10) {
					playb += wavbit * (wavch == 2 ? 10 : 5);
					playb *= 400;
					playb -= wavbit * (wavch == 2 ? 40 : 20);
					if (playb < 0)playb = 0;
					if (ps == 0) {
						OnPause();
						ZeroMemory(bufwav3, sizeof(bufwav3));
						syukai = 1; syukai2 = 0;
						if (thn == FALSE) for (;;) { if (syukai2 == 1)break; DoEvent(); }
						if (savedata.mp3orig) {
							if (mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return 0; }
						}
						else {
							if (mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb)m_dsb->Stop(); }return 0; }
						}
						poss = 0; sek = TRUE;
						timer.SetEvent();
						syukai = 0;
						OnPause();
					}
					else
						if (savedata.mp3orig) {
							if (mp3_.seek2(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return 0; }
						}
						else {
							if (mp3_.seek(playb / (wavch == 2 ? 4 : 1), wavch) == FALSE) { return 0; }
						}
					//						m_time.SetPos((int)playb/400);
				}
				else {
					seekadpcm((int)playb);
					sek = TRUE;
					timer.SetEvent();
				}
			}
			else if (mode == -3) {
				if (mod) {
					if (mod->SetPosition && sikpi.dwSeekable) mod->SetPosition(kmp1, (DWORD)((double)playb / (((double)wavbit * (double)wavch) / 2000.0)));
					sek = TRUE;
					timer.SetEvent();
				}
			}
			else {
				ov_pcm_seek(&vf, (ogg_int64_t)playb);
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
	playb += wavbit * 10 * a;
	if ((loop1 + loop2) < (int)playb && endf == 0) playb = (loop1 + loop2);
	m_time.SetPos((int)playb);
	if (pMainFrame1) {
		pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit));
	}
	if ((mode >= 10 && mode <= 21) || mode <= -10) {
		seekadpcm((int)playb);
		sek = TRUE;
		timer.SetEvent();
	}
	else {
		ov_pcm_seek(&vf, (ogg_int64_t)playb);
		sek = TRUE;
		timer.SetEvent();
	}
	poss = 0;
}

void COggDlg::OnActivate(UINT nState, CWnd * pWndOther, BOOL bMinimized)
{

	CCustomDialog::OnActivate(nState, pWndOther, bMinimized);
	int l = 5;
	if (plw) {
		if ((nState == WA_ACTIVE || nState == WA_CLICKACTIVE) && bMinimized == 0 && pl->m_saisyo.GetCheck()) {
			ogpl0 = 1;
			pl->ShowWindow(SW_RESTORE);
		}
	}
	if (nState == WA_INACTIVE) //非アクティブ
	{
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
	CCustomDialog::OnSysKeyDown(nChar, nRepCnt, nFlags);
}

void COggDlg::OnKillFocus(CWnd * pNewWnd)
{

	CCustomDialog::OnKillFocus(pNewWnd);
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

void COggDlg::OnSize(UINT nType, int cx, int cy)
{
	extern CImageBase* playbase;
	CCustomDialog::OnSize(nType, cx, cy);
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
		if (pl) {
			if (pl->m_saisyo.GetCheck()) {
				//pl->ShowWindow(SW_RESTORE);
//				::SetWindowPos(pl->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			}
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
	IShellLink* psl;
	HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl));
	if (SUCCEEDED(hr)) {
		if (WA) {
			TCHAR fname[MAX_PATH];
			TCHAR shortfname[MAX_PATH];
			GetModuleFileName(0, fname, MAX_PATH);
			GetShortPathName(fname, shortfname, MAX_PATH);
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
	HBRUSH hbr = CCustomDialog::OnCtlColor(pDC, pWnd, nCtlColor);

	// TODO:  ここで DC の属性を変更してください。
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
	// TODO:  既定値を使用したくない場合は別のブラシを返します。
	return hbr;
}


void COggDlg::OnPlayList()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	if (plw == 0) pl = NULL;
	if (pl) {
		if (plw)
			plw = 0;
		killw1 = 0;
		if (pl) {
			pl->nnn = 1;
			//			pl->DestroyWindow();
			pl->OnClose();
			for (; killw1 == 0;)
				DoEvent();
			delete pl;
			pl = NULL;
		}
	}
	else {
		plw = 1;
		pl = new CPlayList;
		pl->Create(this);
		plw = 1;
	}
	if (pl)
		savedata.pl = 1;
	else
		savedata.pl = 0;

	char tmp[1024];
	_getcwd(tmp, 1000);
	_tchdir(karento2);
	CFile ab;
#if _UNICODE
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite, NULL) == TRUE) {
#endif
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
	_chdir(tmp);
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

void plus2(int& c)
{
	CString ss = sswk;
	hDLLk1[kpicnt] = LoadLibrary(ss);
	if (hDLLk1[kpicnt]) {
		pFunck[kpicnt] = (pfnGetKMPModule)GetProcAddress(hDLLk1[kpicnt], SZ_KMP_GETMODULE);
		typedef HRESULT(WINAPI* kpi_CreateInstance)(REFIID riid, void** ppvObject, IKpiUnknown* pUnknown);
		kpi_CreateInstance cr = (kpi_CreateInstance)GetProcAddress(hDLLk1[kpicnt], "kpi_CreateInstance");
		if (pFunck[kpicnt] || cr) {
			if (cr) { // kpi 5
				IKpiDecoderModule* ob = NULL;
				IUnknown* pMyObject = new CMyHost();
				HRESULT hr = cr(IID_IKpiDecoderModule, (void**)&ob, pMyObject);
				if (hr == S_OK) {
					const KPI_DECODER_MODULEINFO* m_ModuleInfo;
					IKpiDecoderModule* obb = (IKpiDecoderModule*)ob;
					obb->GetModuleInfo(&m_ModuleInfo);
					if (m_ModuleInfo != NULL && m_ModuleInfo->cszSupportExts != NULL) {

						CStringArray parts;
						SplitString(m_ModuleInfo->cszSupportExts, L"/", parts);
						for (INT_PTR i = 0; i < parts.GetCount(); ++i)
						{
							ext[kpicnt][i] = parts.GetAt(i);
							ext[kpicnt][i].MakeLower();
							kvar[kpicnt][i] = 5;
						}


					}

					pMyObject->Release();
					obb->Release();
					kpif[kpicnt] = ss;
					kpicnt++;
				}
			}
			else { // kpi 2
				{
					mod1[kpicnt] = pFunck[kpicnt]();
					kpif[kpicnt] = ss;
					for (int i = 0; i < 299; i++) {
						if (mod1[kpicnt] == NULL) break;
						if (mod1[kpicnt]->ppszSupportExts) {
							if (mod1[kpicnt]->ppszSupportExts[i] == NULL ||
								mod1[kpicnt]->ppszSupportExts[i][0] == NULL) {
								ext[kpicnt][i] == L""; break;
							}
							ext[kpicnt][i] = mod1[kpicnt]->ppszSupportExts[i];
							ext[kpicnt][i].MakeLower();
							kvar[kpicnt][i] = 2;
						}
						else { ext[kpicnt][i] == ""; break; }
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
CImageBase* jake = NULL;
void COggDlg::OnBnmp3jake()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	if (mi) {
		delete mi;
		delete jake;
	}
	mi = new CMp3Image;
	mi->Create(og);
	if (savedata.aero == 1) {
		jake = new CImageBase;
		if (jake->Create(og) == FALSE) {
			AfxMessageBox(L"Baseの起動に失敗しました");
		}
		jake->ShowWindow(SW_HIDE);
		jake->oya = mi;
	}
	else {
		jake = NULL;
	}
	mi->Load(mp3file);

}


void COggDlg::OnDestroy()
{
	CCustomDialog::OnDestroy();

	// TODO: ここにメッセージ ハンドラー コードを追加します。
	m_newFont->DeleteObject();
	delete m_newFont;
	m_newFont1->DeleteObject();
	delete m_newFont1;

}


BOOL COggDlg::Create(LPCTSTR lpszTemplateName, CWnd * pParentWnd)
{
	// TODO: ここに特定なコードを追加するか、もしくは基底クラスを呼び出してください。
	return CCustomDialog::Create(lpszTemplateName, pParentWnd);
}


int COggDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomDialog::OnCreate(lpCreateStruct) == -1)
		return -1;

	// TODO: ここに特定な作成コードを追加してください。
	if (savedata.aero == 1) {
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
	CCustomDialog::OnMoving(fwSide, pRect);
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
	CCustomDialog::OnSetFocus(pOldWnd);

	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


int COggDlg::OnMouseActivate(CWnd * pDesktopWnd, UINT nHitTest, UINT message)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。

	return CCustomDialog::OnMouseActivate(pDesktopWnd, nHitTest, message);
}

int npap = 0;
void COggDlg::OnActivateApp(BOOL bActive, DWORD dwThreadID)
{
	CCustomDialog::OnActivateApp(bActive, dwThreadID);

	// TODO: ここにメッセージ ハンドラー コードを追加します。
	if (bActive) {
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
		SetTimer(4924, 10, NULL);
	}
	return CCustomDialog::OnNcActivate(bActive);
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

	CCustomDialog::OnMouseMove(nFlags, point);
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
	CCustomDialog::OnLButtonDown(nFlags, point);
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
	if (!::IsWindow(m_EqualizerDlg.GetSafeHwnd()))
	{
		m_EqualizerDlg.Create(IDD_EQUALIZER, this);
		savedata.eqwindow = 1;
	}
	else {
		m_EqualizerDlg.DestroyWindow();
		savedata.eqwindow = 0;
	}

	m_EqualizerDlg.ShowWindow(SW_SHOW);
	m_EqualizerDlg.SetFocus();
}

