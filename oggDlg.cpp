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
#include "wav.h"
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
static wav wav_;

int readadpcm(CFile& adpcmf, char* bw, int len);
int readadpcmzwei(CFile& adpcmf, char* bw, int len);
int readadpcmgurumin(CFile& adpcmf, char* bw, int len);
int readadpcmarc(CFile& adpcmf, char* bw, int len);
int seekadpcm(int pos);
static BOOL decode_msadpcm_wav(CFile& f, const wavinfo& wi, char** outBuf, int* outSize);
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
void HandleNotifications_export();  // WAV出力専用（DirectSoundなし、ファイル書き込みのみ）
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
int playwavwav(BYTE* bw, int old, int l1, int l2);
int readwav(BYTE* bw, int cnt);
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

	SetWindowText(LL14(L"oggのバージョン情報", L"ogg Version Info", L"Info version ogg", L"Info versione ogg", L"Info version ogg", L"ogg ?? ??", L"ogg版本信息", L"??????? ????? ogg", L"Информация о версии ogg", L"ogg Versionsinfo", L"Info versao ogg", L"ogg versie-info", L"Informacje o wersji ogg", L"ogg surum bilgisi"));
	SetDlgItemText(IDOK, LL14(L"OK", L"OK", L"OK", L"OK", L"OK", L"??", L"?定", L"?????", L"OK", L"OK", L"OK", L"OK", L"OK", L"Tamam"));
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
int wav999_use_adbuf = 0;
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

// WAV出力（再生なし）用
CString wavExportPath;
int wavExportLoopCount = 0;
BOOL wavExportMute = FALSE;

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
	SetWindowText(LL14(L"mp3/m4a簡易プレイヤ Ver 0.8g", L"mp3/m4a Simple Player Ver 0.8g", L"mp3/m4a Lecteur simple Ver 0.8g", L"mp3/m4a Lettore semplice Ver 0.8g", L"mp3/m4a Reproductor simple Ver 0.8g", L"mp3/m4a ?? ???? Ver 0.8g", L"mp3/m4a ?易播放器 Ver 0.8g", L"mp3/m4a ???? ???? Ver 0.8g", L"mp3/m4a Простой плеер Ver 0.8g", L"mp3/m4a Einfacher Player Ver 0.8g", L"mp3/m4a Player simples Ver 0.8g", L"mp3/m4a Eenvoudige speler Ver 0.8g", L"mp3/m4a Prosty odtwarzacz Ver 0.8g", L"mp3/m4a Basit oynat?c? Ver 0.8g"));
	SetDlgItemText(IDC_BUTTON8, LL14(L"Ys6 ナピシュテム", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"??6 ????", L"伊?6", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim", L"Ys6 Napishtim"));
	SetDlgItemText(IDC_BUTTON7, LL14(L"Ys フェルガナ", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"?? ???", L"伊?菲?盖?", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana", L"Ys Felghana"));
	SetDlgItemText(IDC_BUTTON15, LL14(L"Ys オリジン", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"?? ???", L"伊?起源", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin"));
	SetDlgItemText(IDC_BUTTON6, LL14(L"空の軌跡FC", L"Trails in the Sky FC", L"Les Sentiers du Ciel FC", L"Trails in the Sky FC", L"Trails in the Sky FC", L"??? ?? FC", L"空之?迹FC", L"Trails in the Sky FC", L"Тропы в Небе FC", L"Himmelsleitern FC", L"Trails in the Sky FC", L"Trails in the Sky FC", L"Trails in the Sky FC", L"Trails in the Sky FC"));
	SetDlgItemText(IDC_BUTTON2, LL14(L"空の軌跡SC", L"Trails in the Sky SC", L"Les Sentiers du Ciel SC", L"Trails in the Sky SC", L"Trails in the Sky SC", L"??? ?? SC", L"空之?迹SC", L"Trails in the Sky SC", L"Тропы в Небе SC", L"Himmelsleitern SC", L"Trails in the Sky SC", L"Trails in the Sky SC", L"Trails in the Sky SC", L"Trails in the Sky SC"));
	SetDlgItemText(IDC_BUTTON17, LL14(L"空の軌跡The3rd", L"Trails in the Sky The 3rd", L"Les Sentiers du Ciel The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"??? ?? The 3rd", L"空之?迹The3rd", L"Trails in the Sky The 3rd", L"Тропы в Небе The 3rd", L"Himmelsleitern The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd", L"Trails in the Sky The 3rd"));
	SetDlgItemText(IDC_BUTTON19, L"ZWEI II");
	SetDlgItemText(IDC_BUTTON23, L"Ys I&&II Chronicles 1");
	SetDlgItemText(IDC_BUTTON24, L"Ys I&&II Chronicles 2");
	SetDlgItemText(IDC_BUTTON25, L"XANADU NEXT");
	SetDlgItemText(IDC_BUTTON27, LL14(L"Ys 完全版 Ys1", L"Ys Complete Ys1", L"Ys Integral Ys1", L"Ys Complete Ys1", L"Ys Completo Ys1", L"?? ??? Ys1", L"伊?完全版 Ys1", L"Ys ?????? Ys1", L"Ys Complete Ys1", L"Ys Complete Ys1", L"Ys Completo Ys1", L"Ys Complete Ys1", L"Ys Complete Ys1", L"Ys Complete Ys1"));
	SetDlgItemText(IDC_BUTTON28, LL14(L"Ys 完全版 Ys2", L"Ys Complete Ys2", L"Ys Integral Ys2", L"Ys Complete Ys2", L"Ys Completo Ys2", L"?? ??? Ys2", L"伊?完全版 Ys2", L"Ys ?????? Ys2", L"Ys Complete Ys2", L"Ys Complete Ys2", L"Ys Completo Ys2", L"Ys Complete Ys2", L"Ys Complete Ys2", L"Ys Complete Ys2"));
	SetDlgItemText(IDC_BUTTON31, L"Sorcerian Original");
	SetDlgItemText(IDC_BUTTON33, L"Zwei!!");
	SetDlgItemText(IDC_BUTTON35, LL14(L"ぐるみん", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"???", L"??小天使", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin"));
	SetDlgItemText(IDC_BUTTON37, LL14(L"ダイナソア", L"Dinosaur", L"Dinosaure", L"Dinosauro", L"Dinosaurio", L"??", L"恐?", L"???????", L"Динозавр", L"Dinosaurier", L"Dinossauro", L"Dinosaurus", L"Dinozaur", L"Dinozor"));
	SetDlgItemText(IDC_BUTTON4, LL14(L"再演奏", L"Replay", L"Relecture", L"Ripeti", L"Repetir", L"?? ??", L"重新播放", L"????? ???????", L"Повтор", L"Erneut abspielen", L"Repetir", L"Opnieuw afspelen", L"Odtworz ponownie", L"Tekrar cal"));
	SetDlgItemText(IDC_BUTTON3, LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"?? ??", L"?停", L"????? ????", L"Пауза", L"Pause", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"));
	SetDlgItemText(IDC_BUTTON1, LL14(L"停止", L"Stop", L"Arret", L"Stop", L"Detener", L"??", L"停止", L"?????", L"Стоп", L"Stop", L"Parar", L"Stoppen", L"Stop", L"Durdur"));
	SetDlgItemText(IDC_CHECK1, LL14(L"スペアナ", L"Spectrum", L"Spectre", L"Spettro", L"Espectro", L"????", L"??", L"?????", L"Спектр", L"Spektrum", L"Espectro", L"Spectrum", L"Widmo", L"Spektrum"));
	SetDlgItemText(IDC_BUTTON5, LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza", L"Desvanecer", L"??? ??", L"淡出", L"?????", L"Затухание", L"Ausblenden", L"Desvanecer", L"Fade out", L"Zanikanie", L"Solukla?t?r"));
	SetDlgItemText(IDC_BUTTON21, LL14(L"設定", L"Settings", L"Parametres", L"Impostazioni", L"Ajustes", L"??", L"?置", L"?????????", L"Настройки", L"Einstellungen", L"Configuracoes", L"Instellingen", L"Ustawienia", L"Ayarlar"));
	SetDlgItemText(IDC_BUTTON9, LL14(L"フォルダ設定", L"Folder settings", L"Parametres dossier", L"Impostazioni cartella", L"Configuracion carpeta", L"?? ??", L"文件??置", L"??????? ??????", L"Настройки папки", L"Ordnereinstellungen", L"Config. pasta", L"Mapinstellingen", L"Ustawienia folderu", L"Klasor ayarlar?"));
	SetDlgItemText(IDOK, LL14(L"終了", L"Exit", L"Quitter", L"Esci", L"Salir", L"??", L"退出", L"????", L"Выход", L"Beenden", L"Sair", L"Afsluiten", L"Zako?cz", L"C?k??"));
	SetDlgItemText(IDC_CHECK5, LL14(L"ランダム再生", L"Random play", L"Lecture aleatoire", L"Riproduzione casuale", L"Reproduccion aleatoria", L"??? ??", L"随机播放", L"????? ??????", L"Случайное воспроизведение", L"Zufallswiedergabe", L"Reproducao aleatoria", L"Willekeurig afspelen", L"Losowe odtwarzanie", L"Rastgele calma"));
	SetDlgItemText(IDC_CHECK6, LL14(L"順次再生", L"Sequential play", L"Lecture sequentielle", L"Riproduzione sequenziale", L"Reproduccion secuencial", L"?? ??", L"?序播放", L"????? ??????", L"Последовательное воспроизведение", L"Sequentielle Wiedergabe", L"Reproducao sequencial", L"Sequentieel afspelen", L"Kolejne odtwarzanie", L"S?ral? calma"));
	SetDlgItemText(IDC_BUTTON14, LL14(L"演奏開始", L"Play", L"Lecture", L"Riproduci", L"Reproducir", L"??", L"播放", L"?????", L"Воспроизведение", L"Abspielen", L"Reproduzir", L"Afspelen", L"Odtworz", L"Cal"));
	SetDlgItemText(IDC_CHECK7, LL14(L"YS6 ナピシュテムの匣", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako"));
	SetDlgItemText(IDC_CHECK8, LL14(L"YS フェルガナの誓い", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai"));
	SetDlgItemText(IDC_CHECK9, LL14(L"英雄伝説6空の軌跡FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legende des Heros VI Les Sentiers du Ciel FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"????6 ??? ?? FC", L"英雄??6 空之?迹FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC", L"Legend of Heroes VI Trails in the Sky FC"));
	SetDlgItemText(IDC_CHECK10, LL14(L"英雄伝説6空の軌跡SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legende des Heros VI Les Sentiers du Ciel SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"????6 ??? ?? SC", L"英雄??6 空之?迹SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC", L"Legend of Heroes VI Trails in the Sky SC"));
	SetDlgItemText(IDC_CHECK11, LL14(L"YS オリジン", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin"));
	SetDlgItemText(IDC_CHECK12, LL14(L"英雄伝説6空の軌跡TC", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legende des Heros VI Les Sentiers du Ciel The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"????6 ??? ?? The 3rd", L"英雄??6 空之?迹The3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd", L"Legend of Heroes VI Trails in the Sky The 3rd"));
	SetDlgItemText(IDC_CHECK13, L"ZWEI II");
	SetDlgItemText(IDC_CHECK14, L"Ys Chronicles Ys 1");
	SetDlgItemText(IDC_CHECK15, L"Ys Chronicles Ys 2");
	SetDlgItemText(IDC_CHECK16, L"XANADU NEXT");
	SetDlgItemText(IDC_CHECK17, LL14(L"Ys 完全版 Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1"));
	SetDlgItemText(IDC_CHECK18, LL14(L"Ys 完全版 Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2"));
	SetDlgItemText(IDC_CHECK19, L"Sorcerian Original");
	SetDlgItemText(IDC_CHECK20, L"Zwei!!");
	SetDlgItemText(IDC_CHECK21, LL14(L"ぐるみん", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin"));
	SetDlgItemText(IDC_CHECK22, LL14(L"ダイナソア リザレクション", L"Dinosaur Resurrection", L"Resurrection Dinosaure", L"Resurrezione Dinosauro", L"Resurreccion Dinosaurio", L"?? ??", L"恐??活", L"??????? ???????", L"Динозавр: Воскрешение", L"Dinosaurier Auferstehung", L"Ressurreicao Dinossauro", L"Dinosaurus Herrijzenis", L"Dinozaur Zmartwychwstanie", L"Dinozor Dirili?"));
	SetDlgItemText(IDC_STATICaaad, LL14(L"ループ回数", L"Loop count", L"Nombre de boucles", L"Conteggio loop", L"Cuenta de bucle", L"?? ??", L"循?次数", L"??? ?????????", L"Количество повторов", L"Schleifenzahler", L"Contagem de loop", L"Loopaantal", L"Liczba p?tli", L"Dongu say?s?"));
	SetDlgItemText(IDC_CHECK2, LL14(L"WAVファイルへ保存", L"Save to WAV file", L"Enregistrer en WAV", L"Salva come WAV", L"Guardar como WAV", L"WAV ??? ??", L"保存?WAV文件", L"??? ?? WAV", L"Сохранить в WAV", L"Als WAV speichern", L"Salvar como WAV", L"Opslaan als WAV", L"Zapisz jako WAV", L"WAV olarak kaydet"));
	SetDlgItemText(IDC_CHECK3, LL14(L"動画も表示する", L"Show video", L"Afficher video", L"Mostra video", L"Mostrar video", L"??? ??", L"?示??", L"????? ???????", L"Показывать видео", L"Video anzeigen", L"Mostrar video", L"Video tonen", L"Poka? wideo", L"Videoyu goster"));
	SetDlgItemText(IDC_STATICaaab, LL14(L"主音量", L"Master volume", L"Volume principal", L"Volume master", L"Volumen maestro", L"??? ??", L"主音量", L"????? ???????", L"Общая громкость", L"Hauptlautstarke", L"Volume mestre", L"Hoofdvolume", L"G?o?no?? g?owna", L"Ana ses"));
	SetDlgItemText(IDC_STATICaaa, LL14(L"DirectSound音量", L"DirectSound volume", L"Volume DirectSound", L"Volume DirectSound", L"Volumen DirectSound", L"DirectSound ??", L"DirectSound音量", L"??? DirectSound", L"Громкость DirectSound", L"DirectSound-Lautstarke", L"Volume DirectSound", L"DirectSound-volume", L"G?o?no?? DirectSound", L"DirectSound sesi"));
	SetDlgItemText(IDC_STATICaaac, LL14(L"拡張音量", L"Extended volume", L"Volume etendu", L"Volume esteso", L"Volumen extendido", L"?? ??", L"?展音量", L"????? ??????", L"Доп. громкость", L"Erweiterte Lautstarke", L"Volume estendido", L"Uitgebreid volume", L"G?o?no?? rozszerzona", L"Geni?letilmi? ses"));
	SetDlgItemText(IDC_STATIC_t, LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"??", L"速度", L"??????", L"Темп", L"Tempo", L"Andamento", L"Tempo", L"Tempo", L"Tempo"));
	SetDlgItemText(IDC_STATIC_p, LL14(L"ピッチ", L"Pitch", L"Hauteur", L"Tono", L"Tono", L"??", L"音高", L"??????", L"Высота тона", L"Tonhohe", L"Tom", L"Toonhoogte", L"Wysoko??", L"Perde"));
	SetDlgItemText(IDC_BUTTON39, L"Brandish4");
	SetDlgItemText(IDC_CHECK23, LL14(L"ブランディッシュ４", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4", L"Brandish4"));
	SetDlgItemText(IDC_BUTTON44, LL14(L"白き魔女", L"White Witch", L"Sorciere Blanche", L"Strega Bianca", L"Bruja Blanca", L"?? ??", L"白之魔女", L"??????? ???????", L"Белая Ведьма", L"Weise Hexe", L"Bruxa Branca", L"Witte Heks", L"Bia?a Czarownica", L"Beyaz Cad?"));
	SetDlgItemText(IDC_BUTTON45, LL14(L"朱紅い雫", L"Tear of Vermillion", L"Larme de Vermillon", L"Lacrima di Vermiglio", L"Lagrima de Bermellon", L"??? ???", L"朱?之泪", L"???? ???????", L"Слеза Вермиллиона", L"Trane der Purpur", L"Lagrima de Vermelho", L"Traan van Vermiljoen", L"?za Vermillion", L"Vermilyon Gozya??"));
	SetDlgItemText(IDC_BUTTON46, LL14(L"海の檻歌", L"Cagesong of the Ocean", L"Chant des Profondeurs", L"Canto dell'Oceano", L"Cantico del Oceano", L"??? ???", L"海之?歌", L"?????? ??????", L"Песнь Океана", L"Kafiglied des Ozeans", L"Cantico do Oceano", L"Kooi van de Oceaan", L"Pie?? Oceanu", L"Okyanus Kafes ?ark?s?"));
	SetDlgItemText(IDC_CHECK24, LL14(L"英雄伝説III 白き魔女", L"Legend of Heroes III White Witch", L"Legende des Heros III Sorciere Blanche", L"Legend of Heroes III Strega Bianca", L"Legend of Heroes III Bruja Blanca", L"????III ?? ??", L"英雄??III 白之魔女", L"Legend of Heroes III White Witch", L"Legend of Heroes III White Witch", L"Legend of Heroes III Weise Hexe", L"Legend of Heroes III Bruxa Branca", L"Legend of Heroes III White Witch", L"Legend of Heroes III Bia?a Czarownica", L"Legend of Heroes III White Witch"));
	SetDlgItemText(IDC_CHECK25, LL14(L"英雄伝説IV 朱紅い雫", L"Legend of Heroes IV Tear of Vermillion", L"Legende des Heros IV Larme de Vermillon", L"Legend of Heroes IV Lacrima di Vermiglio", L"Legend of Heroes IV Lagrima de Bermellon", L"????IV ??? ???", L"英雄??IV 朱?之泪", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Trane der Purpur", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Tear of Vermillion", L"Legend of Heroes IV Tear of Vermillion"));
	SetDlgItemText(IDC_CHECK26, LL14(L"英雄伝説V 海の檻歌", L"Legend of Heroes V Cagesong of the Ocean", L"Legende des Heros V Chant des Profondeurs", L"Legend of Heroes V Canto dell'Oceano", L"Legend of Heroes V Cantico del Oceano", L"????V ??? ???", L"英雄??V 海之?歌", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Kafiglied des Ozeans", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Cagesong of the Ocean", L"Legend of Heroes V Cagesong of the Ocean"));
	SetDlgItemText(IDC_BUTTON47, LL14(L"月影", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI", L"TSUKI"));
	SetDlgItemText(IDC_BUTTON48, LL14(L"西風", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi", L"Nishi"));
	SetDlgItemText(IDC_BUTTON51, LL14(L"アーク", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc", L"Arc"));
	SetDlgItemText(IDC_BUTTON53, LL14(L"三国志1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1", L"San1"));
	SetDlgItemText(IDC_BUTTON54, LL14(L"三国志2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2", L"San2"));
	SetDlgItemText(IDC_BUTTON57, LL14(L"プレイリスト", L"Playlist", L"Liste de lecture", L"Playlist", L"Lista de reproduccion", L"?? ??", L"播放列表", L"????? ???????", L"Плейлист", L"Wiedergabeliste", L"Lista de reproducao", L"Afspeellijst", L"Lista odtwarzania", L"Calma listesi"));
	SetDlgItemText(IDC_BUTTON58, LL14(L"ジャケ", L"Cover", L"Pochette", L"Copertina", L"Caratula", L"??", L"封面", L"??????", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Ok?adka", L"Kapak"));

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
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON1), LL14(
			L"演奏中のogg/wav/mp3/avi/kpiファイルを停止します",
			L"Stop playing ogg/wav/mp3/avi/kpi file",
			L"Arreter la lecture du fichier ogg/wav/mp3/avi/kpi",
			L"Interrompi la riproduzione del file ogg/wav/mp3/avi/kpi",
			L"Detener la reproduccion del archivo ogg/wav/mp3/avi/kpi",
			L"ogg/wav/mp3/avi/kpi ?? ??? ?????",
			L"停止播放ogg/wav/mp3/avi/kpi文件",
			L"????? ????? ??? ogg/wav/mp3/avi/kpi",
			L"Остановить воспроизведение файла ogg/wav/mp3/avi/kpi",
			L"Wiedergabe der ogg/wav/mp3/avi/kpi-Datei stoppen",
			L"Parar a reproducao do arquivo ogg/wav/mp3/avi/kpi",
			L"Afspelen van ogg/wav/mp3/avi/kpi-bestand stoppen",
			L"Zatrzymaj odtwarzanie pliku ogg/wav/mp3/avi/kpi",
			L"ogg/wav/mp3/avi/kpi dosyas?n?n oynat?lmas?n? durdur"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON2), LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"???? ogg? ???? ?????",
			L"从曲目列表中??并播放ogg",
			L"????? ?????? ogg ?? ????? ????????",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON6), LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"???? ogg? ???? ?????",
			L"从曲目列表中??并播放ogg",
			L"????? ?????? ogg ?? ????? ????????",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON7), LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"???? ogg? ???? ?????",
			L"从曲目列表中??并播放ogg",
			L"????? ?????? ogg ?? ????? ????????",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON8), LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"???? ogg? ???? ?????",
			L"从曲目列表中??并播放ogg",
			L"????? ?????? ogg ?? ????? ????????",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON15), LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"???? ogg? ???? ?????",
			L"从曲目列表中??并播放ogg",
			L"????? ?????? ogg ?? ????? ????????",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON17), LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"???? ogg? ???? ?????",
			L"从曲目列表中??并播放ogg",
			L"????? ?????? ogg ?? ????? ????????",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON19), LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"???? ogg? ???? ?????",
			L"从曲目列表中??并播放ogg",
			L"????? ?????? ogg ?? ????? ????????",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON25), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON23), LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"???? ogg? ???? ?????",
			L"从曲目列表中??并播放ogg",
			L"????? ?????? ogg ?? ????? ????????",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON24), LL14(
			L"曲一覧表からoggを選択し再生します",
			L"Select and play ogg from track list",
			L"Selectionner et lire un ogg depuis la liste",
			L"Seleziona e riproduci ogg dalla lista",
			L"Seleccionar y reproducir ogg de la lista",
			L"???? ogg? ???? ?????",
			L"从曲目列表中??并播放ogg",
			L"????? ?????? ogg ?? ????? ????????",
			L"Выбрать и воспроизвести ogg из списка",
			L"ogg aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir ogg da lista de faixas",
			L"ogg selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz ogg z listy utworow",
			L"Parca listesinden ogg sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON27), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON28), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON31), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON35), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON33), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON37), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON39), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON44), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON45), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON46), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON47), LL14(
			L"曲一覧表からmp3を選択し再生します",
			L"Select and play mp3 from track list",
			L"Selectionner et lire un mp3 depuis la liste",
			L"Seleziona e riproduci mp3 dalla lista",
			L"Seleccionar y reproducir mp3 de la lista",
			L"???? mp3? ???? ?????",
			L"从曲目列表中??并播放mp3",
			L"????? ?????? mp3 ?? ????? ????????",
			L"Выбрать и воспроизвести mp3 из списка",
			L"mp3 aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir mp3 da lista de faixas",
			L"mp3 selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz mp3 z listy utworow",
			L"Parca listesinden mp3 sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON48), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON51), LL14(
			L"曲一覧表からwav(adp)を選択し再生します",
			L"Select and play wav(adp) from track list",
			L"Selectionner et lire un wav(adp) depuis la liste",
			L"Seleziona e riproduci wav(adp) dalla lista",
			L"Seleccionar y reproducir wav(adp) de la lista",
			L"???? wav(adp)? ???? ?????",
			L"从曲目列表中??并播放wav(adp)",
			L"????? ?????? wav(adp) ?? ????? ????????",
			L"Выбрать и воспроизвести wav(adp) из списка",
			L"wav(adp) aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav(adp) da lista de faixas",
			L"wav(adp) selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav(adp) z listy utworow",
			L"Parca listesinden wav(adp) sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON53), LL14(
			L"曲一覧表からwavを選択し再生します",
			L"Select and play wav from track list",
			L"Selectionner et lire un wav depuis la liste",
			L"Seleziona e riproduci wav dalla lista",
			L"Seleccionar y reproducir wav de la lista",
			L"???? wav? ???? ?????",
			L"从曲目列表中??并播放wav",
			L"????? ?????? wav ?? ????? ????????",
			L"Выбрать и воспроизвести wav из списка",
			L"wav aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir wav da lista de faixas",
			L"wav selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz wav z listy utworow",
			L"Parca listesinden wav sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON54), LL14(
			L"曲一覧表からmp3を選択し再生します",
			L"Select and play mp3 from track list",
			L"Selectionner et lire un mp3 depuis la liste",
			L"Seleziona e riproduci mp3 dalla lista",
			L"Seleccionar y reproducir mp3 de la lista",
			L"???? mp3? ???? ?????",
			L"从曲目列表中??并播放mp3",
			L"????? ?????? mp3 ?? ????? ????????",
			L"Выбрать и воспроизвести mp3 из списка",
			L"mp3 aus der Trackliste auswahlen und abspielen",
			L"Selecionar e reproduzir mp3 da lista de faixas",
			L"mp3 selecteren en afspelen uit de tracklist",
			L"Wybierz i odtworz mp3 z listy utworow",
			L"Parca listesinden mp3 sec ve oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON57), LL14(
			L"プレイリストを表示／非表示します。\n表示されている時に演奏を開始またはドロップで演奏するとリストに追加されます。\n非表示の時はリストには追加されません。\n本体へのドロップは1つだけでしたがプレイリストへのドロップは複数出来ます。",
			L"Show/hide playlist.\nWhen visible, playback or drop adds to list.\nWhen hidden, items are not added.\nMain window accepts single drop; playlist accepts multiple.",
			L"Afficher/masquer la liste de lecture.\nVisible : la lecture ou le depot ajoute a la liste.\nMasque : les elements ne sont pas ajoutes.\nFenetre principale : un seul depot ; liste : plusieurs.",
			L"Mostra/nascondi playlist.\nVisibile: riproduzione o trascinamento aggiunge alla lista.\nNascosto: gli elementi non vengono aggiunti.\nFinestra principale: un file; playlist: piu file.",
			L"Mostrar/ocultar lista de reproduccion.\nVisible: reproducir o soltar anade a la lista.\nOculto: los elementos no se anaden.\nVentana principal: un archivo; lista: varios.",
			L"????? ??/????.\n?? ?? ? ?? ?? ?? ???? ??? ?????.\n??? ?? ?? ??? ???? ????.\n???? ???, ?????? ?? ? ?? ?????.",
			L"?示/?藏播放列表。\n?示?，?始播放或?放将添加到列表中。\n?藏?，不会添加到列表。\n主窗口只接受?个?放，播放列表可接受多个。",
			L"?????/????? ????? ???????.\n??? ??????? ???? ??????? ?? ??????? ??? ???????.\n??? ???????? ?? ????? ???????.\n??????? ????????: ??? ????? ???????: ??? ?????.",
			L"Показать/скрыть плейлист.\nПри отображении воспроизведение или перетаскивание добавляет в список.\nПри скрытии элементы не добавляются.\nГлавное окно: один файл; плейлист: несколько.",
			L"Wiedergabeliste anzeigen/ausblenden.\nBei Anzeige: Wiedergabe oder Drop fugt zur Liste hinzu.\nBei Ausblenden: Elemente nicht hinzugefugt.\nHauptfenster: ein Drop; Playlist: mehrere.",
			L"Mostrar/ocultar lista de reproducao.\nQuando visivel, reproduzir ou soltar adiciona a lista.\nQuando oculto, itens nao sao adicionados.\nJanela principal: um arquivo; lista: varios.",
			L"Afspeellijst tonen/verbergen.\nZichtbaar: afspelen of neerzetten voegt toe aan lijst.\nVerborgen: items worden niet toegevoegd.\nHoofdvenster: een bestand; afspeellijst: meerdere.",
			L"Poka?/ukryj list? odtwarzania.\nGdy widoczna: odtwarzanie lub upuszczenie dodaje do listy.\nGdy ukryta: elementy nie s? dodawane.\nG?owne okno: jeden plik; lista: wiele.",
			L"Calma listesini goster/gizle.\nGorunurken, oynatma veya b?rakma listeye ekler.\nGizliyken, o?eler eklenmez.\nAna pencere: tek dosya; calma listesi: birden fazla."));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON59), LL14(
			L"イコライザーを設定します",
			L"Open equalizer settings",
			L"Ouvrir les parametres de l'egaliseur",
			L"Apri le impostazioni dell'equalizzatore",
			L"Abrir configuracion del ecualizador",
			L"?????? ?????",
			L"打?均衡器?置",
			L"??? ??????? ??????? ??????",
			L"Открыть настройки эквалайзера",
			L"Equalizer-Einstellungen offnen",
			L"Abrir configuracoes do equalizador",
			L"Equalizerinstellingen openen",
			L"Otworz ustawienia korektora",
			L"Ekolayzer ayarlar?n? ac"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON3), LL14(
			L"演奏中のogg/wav/mp3/avi/kpiファイルを一時停止/再開します",
			L"Pause/resume playing ogg/wav/mp3/avi/kpi file",
			L"Suspendre/reprendre la lecture du fichier ogg/wav/mp3/avi/kpi",
			L"Metti in pausa/riprendi la riproduzione del file ogg/wav/mp3/avi/kpi",
			L"Pausar/reanudar la reproduccion del archivo ogg/wav/mp3/avi/kpi",
			L"ogg/wav/mp3/avi/kpi ?? ??? ????/?????",
			L"?停/恢?播放ogg/wav/mp3/avi/kpi文件",
			L"????? ????/??????? ????? ??? ogg/wav/mp3/avi/kpi",
			L"Приостановить/возобновить воспроизведение файла ogg/wav/mp3/avi/kpi",
			L"Wiedergabe der ogg/wav/mp3/avi/kpi-Datei pausieren/fortsetzen",
			L"Pausar/retomar a reproducao do arquivo ogg/wav/mp3/avi/kpi",
			L"Afspelen van ogg/wav/mp3/avi/kpi-bestand pauzeren/hervatten",
			L"Wstrzymaj/wznow odtwarzanie pliku ogg/wav/mp3/avi/kpi",
			L"ogg/wav/mp3/avi/kpi dosyas?n?n oynat?lmas?n? duraklat/devam ettir"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON4), LL14(
			L"演奏中だった曲を頭から再演奏します",
			L"Restart current track from beginning",
			L"Redemarrer la piste depuis le debut",
			L"Riavvia la traccia corrente dall'inizio",
			L"Reiniciar la pista actual desde el principio",
			L"?? ?? ???? ?? ?????",
			L"从?重新播放当前曲目",
			L"????? ????? ?????? ?????? ?? ???????",
			L"Перезапустить текущий трек с начала",
			L"Aktuellen Titel von Anfang an neu starten",
			L"Reiniciar a faixa atual do inicio",
			L"Huidig nummer vanaf het begin opnieuw starten",
			L"Uruchom ponownie bie??cy utwor od pocz?tku",
			L"Gecerli parcay? ba?tan yeniden ba?lat"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON5), LL14(
			L"フェードアウトして停止します。(内蔵デコーダのみ)",
			L"Fade out and stop (built-in decoder only)",
			L"Fondu et arret (decodeur integre uniquement)",
			L"Dissolvenza e stop (solo decodificatore integrato)",
			L"Fundido y detener (solo decodificador integrado)",
			L"??????? ?????. (?? ????)",
			L"淡出并停止（?内置解?器）",
			L"????? ?????? (?????? ?????? ???)",
			L"Плавное затухание и остановка (только встроенный декодер)",
			L"Ausblenden und stoppen (nur integrierter Decoder)",
			L"Fade out e parar (somente decodificador integrado)",
			L"Uitfaden en stoppen (alleen ingebouwde decoder)",
			L"Wycisz i zatrzymaj (tylko wbudowany dekoder)",
			L"Solukla?t?r ve durdur (yaln?zca dahili kod cozucu)"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON9), LL14(
			L"各ゲームのフォルダ位置を指定します",
			L"Specify folder location for each game",
			L"Specifier l'emplacement du dossier pour chaque jeu",
			L"Specificare la posizione della cartella per ogni gioco",
			L"Especificar la ubicacion de la carpeta para cada juego",
			L"? ??? ?? ??? ?????",
			L"指定?个游?的文件?位置",
			L"????? ???? ?????? ??? ????",
			L"Указать расположение папки для каждой игры",
			L"Ordnerpfad fur jedes Spiel festlegen",
			L"Especificar a localizacao da pasta para cada jogo",
			L"Maplocatie voor elk spel opgeven",
			L"Okre?l lokalizacj? folderu dla ka?dej gry",
			L"Her oyun icin klasor konumunu belirt"));
		m_tooltip.AddTool(GetDlgItem(IDOK), LL14(
			L"簡易プレイヤを終了します",
			L"Exit simple player",
			L"Quitter le lecteur simple",
			L"Esci dal lettore semplice",
			L"Salir del reproductor simple",
			L"?? ????? ?????",
			L"退出?易播放器",
			L"????? ?????? ??????",
			L"Выйти из простого плеера",
			L"Einfachen Player beenden",
			L"Sair do player simples",
			L"Eenvoudige speler afsluiten",
			L"Zamknij prosty odtwarzacz",
			L"Basit oynat?c?dan c?k"));
		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER1), LL14(
			L"音量を変更します\nWindows全体の音量が関係してきます。",
			L"Change volume\nAffects overall Windows volume.",
			L"Modifier le volume\nAffecte le volume global de Windows.",
			L"Cambia volume\nInfluisce sul volume generale di Windows.",
			L"Cambiar volumen\nAfecta el volumen general de Windows.",
			L"??? ?????\nWindows ??? ??? ??? ????.",
			L"更改音量\n影?Windows整体音量。",
			L"????? ????? ?????\n???? ??? ????? ??? Windows ?????.",
			L"Изменить громкость\nВлияет на общую громкость Windows.",
			L"Lautstarke andern\nBeeinflusst die globale Windows-Lautstarke.",
			L"Alterar volume\nAfeta o volume geral do Windows.",
			L"Volume aanpassen\nBeinvloedt het algehele Windows-volume.",
			L"Zmie? g?o?no??\nWp?ywa na ogoln? g?o?no?? systemu Windows.",
			L"Sesi de?i?tir\nWindows genel ses seviyesini etkiler."));
		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER3), LL14(
			L"DirectSound音量を変更します\nこの簡易プレイヤのみの変更でとどまります。\nWindowsの音量は変化しません。",
			L"Change DirectSound volume\nOnly affects this player.\nWindows volume unchanged.",
			L"Modifier le volume DirectSound\nAffecte uniquement ce lecteur.\nLe volume Windows reste inchange.",
			L"Cambia volume DirectSound\nInfluisce solo su questo lettore.\nIl volume di Windows rimane invariato.",
			L"Cambiar volumen DirectSound\nSolo afecta este reproductor.\nEl volumen de Windows no cambia.",
			L"DirectSound ??? ?????\n? ?? ????? ?????.\nWindows ??? ??? ????.",
			L"更改DirectSound音量\n?影?此播放器。\nWindows音量不?。",
			L"????? ????? ??? DirectSound\n???? ??? ??? ?????? ???.\n????? ??? Windows ?? ?????.",
			L"Изменить громкость DirectSound\nВлияет только на этот плеер.\nГромкость Windows не изменяется.",
			L"DirectSound-Lautstarke andern\nBetrifft nur diesen Player.\nWindows-Lautstarke bleibt unverandert.",
			L"Alterar volume DirectSound\nAfeta apenas este player.\nVolume do Windows inalterado.",
			L"DirectSound-volume aanpassen\nBeinvloedt alleen deze speler.\nWindows-volume blijft ongewijzigd.",
			L"Zmie? g?o?no?? DirectSound\nWp?ywa tylko na ten odtwarzacz.\nG?o?no?? Windows pozostaje bez zmian.",
			L"DirectSound sesini de?i?tir\nYaln?zca bu oynat?c?y? etkiler.\nWindows ses seviyesi de?i?mez."));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK1), LL14(
			L"スペクトルアナライザー(波形)を表示/非表示します",
			L"Show/hide spectrum analyzer (waveform)",
			L"Afficher/masquer l'analyseur de spectre (forme d'onde)",
			L"Mostra/nascondi analizzatore di spettro (forma d'onda)",
			L"Mostrar/ocultar analizador de espectro (forma de onda)",
			L"???? ???(??)? ??/????",
			L"?示/?藏??分析?（波形）",
			L"?????/????? ???? ????? (??? ??????)",
			L"Показать/скрыть анализатор спектра (осциллограмма)",
			L"Spektrumanalysator (Wellenform) anzeigen/ausblenden",
			L"Mostrar/ocultar analisador de espectro (forma de onda)",
			L"Spectrumanalyzer (golfvorm) tonen/verbergen",
			L"Poka?/ukryj analizator widma (kszta?t fali)",
			L"Spektrum analizorunu (dalga formu) goster/gizle"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK2), LL14(
			L"演奏中の曲をwavで保存します",
			L"Save playing track as wav",
			L"Enregistrer la piste en cours de lecture en wav",
			L"Salva la traccia in riproduzione come wav",
			L"Guardar la pista en reproduccion como wav",
			L"?? ?? ?? wav? ?????",
			L"将正在播放的曲目保存?wav",
			L"??? ?????? ?????? ?????? ?????? wav",
			L"Сохранить воспроизводимый трек как wav",
			L"Wiedergegebenen Titel als wav speichern",
			L"Salvar faixa em reproducao como wav",
			L"Afspelend nummer opslaan als wav",
			L"Zapisz odtwarzany utwor jako wav",
			L"Oynat?lan parcay? wav olarak kaydet"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK3), LL14(
			L"動画(OPやイベント)のoggの時に動画画面も表示します",
			L"Show video window for video ogg (OP, events)",
			L"Afficher la fenetre video pour les ogg video (OP, evenements)",
			L"Mostra la finestra video per ogg video (OP, eventi)",
			L"Mostrar ventana de video para ogg de video (OP, eventos)",
			L"???(OP???)? ogg ?? ? ??? ??? ?????",
			L"播放??ogg（OP、事件）?同??示??画面",
			L"????? ????? ??????? ?????? ogg ??????? (OP? ???????)",
			L"Показывать окно видео для видео-ogg (OP, события)",
			L"Videofenster fur Video-ogg anzeigen (OP, Ereignisse)",
			L"Mostrar janela de video para ogg de video (OP, eventos)",
			L"Videovenster tonen voor video-ogg (OP, events)",
			L"Poka? okno wideo dla ogg wideo (OP, zdarzenia)",
			L"Video ogg (OP, olaylar) icin video penceresini goster"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK4), LL14(
			L"スペクトルアナライザー(波形)をモノラル表示、ステレオ表示切り替えを行います",
			L"Switch spectrum analyzer between mono and stereo",
			L"Basculer l'analyseur de spectre entre mono et stereo",
			L"Passa l'analizzatore di spettro tra mono e stereo",
			L"Cambiar el analizador de espectro entre mono y estereo",
			L"???? ???(??)? ??/????? ?????",
			L"切???分析?的?声道/立体声?示",
			L"????? ???? ????? ??? ??????? ?????????",
			L"Переключить анализатор спектра между моно и стерео",
			L"Spektrumanalysator zwischen Mono und Stereo umschalten",
			L"Alternar analisador de espectro entre mono e estereo",
			L"Spectrumanalyzer wisselen tussen mono en stereo",
			L"Prze??cz analizator widma mi?dzy mono a stereo",
			L"Spektrum analizorunu mono ve stereo aras?nda de?i?tir"));

		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON12), LL14(
			L"拡張パネルを開く/閉じる",
			L"Open/close extended panel",
			L"Ouvrir/fermer le panneau etendu",
			L"Apri/chiudi pannello esteso",
			L"Abrir/cerrar panel extendido",
			L"?? ??? ??/??",
			L"打?/???展面板",
			L"???/????? ?????? ???????",
			L"Открыть/закрыть расширенную панель",
			L"Erweitertes Panel offnen/schliesen",
			L"Abrir/fechar painel estendido",
			L"Uitgebreid paneel openen/sluiten",
			L"Otworz/zamknij rozszerzony panel",
			L"Geni?letilmi? paneli ac/kapat"));

		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON21), LL14(
			L"各種設定を行います。",
			L"Open settings.",
			L"Ouvrir les parametres.",
			L"Apri le impostazioni.",
			L"Abrir configuracion.",
			L"?? ??? ???.",
			L"打??置。",
			L"??? ?????????.",
			L"Открыть настройки.",
			L"Einstellungen offnen.",
			L"Abrir configuracoes.",
			L"Instellingen openen.",
			L"Otworz ustawienia.",
			L"Ayarlar? ac."));

		m_tooltip.AddTool(GetDlgItem(IDC_CHECK5), LL14(
			L"「再生するゲーム」で選択されているゲームをランダムに演奏します",
			L"Random play from selected games",
			L"Lecture aleatoire depuis les jeux selectionnes",
			L"Riproduzione casuale dai giochi selezionati",
			L"Reproduccion aleatoria de los juegos seleccionados",
			L"「??? ??」?? ??? ??? ???? ?????",
			L"从所?游?中随机播放",
			L"????? ?????? ?? ??????? ???????",
			L"Случайное воспроизведение из выбранных игр",
			L"Zufallige Wiedergabe aus ausgewahlten Spielen",
			L"Reproducao aleatoria dos jogos selecionados",
			L"Willekeurig afspelen van geselecteerde spellen",
			L"Losowe odtwarzanie z wybranych gier",
			L"Secilen oyunlardan rastgele oynat"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK6), LL14(
			L"「再生するゲーム」で選択されているゲームを順番に演奏します",
			L"Sequential play from selected games",
			L"Lecture sequentielle depuis les jeux selectionnes",
			L"Riproduzione sequenziale dai giochi selezionati",
			L"Reproduccion secuencial de los juegos seleccionados",
			L"「??? ??」?? ??? ??? ???? ?????",
			L"从所?游?中?序播放",
			L"????? ?????? ?? ??????? ???????",
			L"Последовательное воспроизведение из выбранных игр",
			L"Sequentielle Wiedergabe aus ausgewahlten Spielen",
			L"Reproducao sequencial dos jogos selecionados",
			L"Opeenvolgend afspelen van geselecteerde spellen",
			L"Sekwencyjne odtwarzanie z wybranych gier",
			L"Secilen oyunlardan s?ral? oynat"));

		m_tooltip.AddTool(GetDlgItem(IDC_CHECK7), LL14(L"イース6 ナピシュテムの匣", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK8), LL14(L"イース フェルガナの誓い", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK9), LL14(L"空の軌跡 First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter", L"Sora no Kiseki First Chapter"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK10), LL14(L"空の軌跡 Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter", L"Sora no Kiseki Second Chapter"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK11), LL14(L"イース オリジン", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK12), LL14(L"空の軌跡 The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd", L"Sora no Kiseki The 3rd"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK13), LL14(L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II", L"Zweii II"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK14), LL14(L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1", L"YS I&&II Chronicles Ys 1"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK15), LL14(L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2", L"YS I&&II Chronicles Ys 2"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK16), LL14(L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT", L"XANADU NEXT"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK17), LL14(L"Ys 完全版 Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1", L"Ys Complete Ys 1"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK18), LL14(L"Ys 完全版 Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2", L"Ys Complete Ys 2"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK19), LL14(L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original", L"Sorcerian Original"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK20), LL14(L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!", L"Zwei!!"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK21), LL14(L"ぐるみん", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin", L"Gurumin"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK22), LL14(L"ダイナソア リザレクション", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK23), LL14(L"Brandish4 - ブランディッシュ4 眠れる神の塔", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God", L"Brandish4 - Tower of the Sleeping God"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK24), LL14(L"英雄伝説III 白き魔女", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch", L"Legend of Heroes III - White Witch"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK25), LL14(L"英雄伝説IV 朱紅い雫", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion", L"Legend of Heroes IV - A Tear of Vermillion"));
		m_tooltip.AddTool(GetDlgItem(IDC_CHECK26), LL14(L"英雄伝説V 海の檻歌", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean", L"Legend of Heroes V - Cagesong of the Ocean"));
		m_tooltip.AddTool(GetDlgItem(IDC_BUTTON58), LL14(
			L"mp3/m4a/ogg/flacに埋め込まれているジャケットを表示します。",
			L"Show embedded album art from mp3/m4a/ogg/flac",
			L"Afficher la pochette integree dans mp3/m4a/ogg/flac",
			L"Mostra la copertina incorporata da mp3/m4a/ogg/flac",
			L"Mostrar portada incrustada de mp3/m4a/ogg/flac",
			L"mp3/m4a/ogg/flac? ??? ?? ??? ?????.",
			L"?示mp3/m4a/ogg/flac中嵌入的??封面",
			L"??? ???? ??????? ??????? ?? mp3/m4a/ogg/flac",
			L"Показать встроенную обложку альбома из mp3/m4a/ogg/flac",
			L"Eingebettetes Albumcover aus mp3/m4a/ogg/flac anzeigen",
			L"Mostrar capa do album incorporada em mp3/m4a/ogg/flac",
			L"Ingebedde albumhoes uit mp3/m4a/ogg/flac tonen",
			L"Poka? ok?adk? albumu osadzon? w mp3/m4a/ogg/flac",
			L"mp3/m4a/ogg/flac icindeki gomulu album kapa??n? goster"));

		m_tooltip.AddTool(GetDlgItem(IDC_EDIT1), LL14(
			L"次の曲へいくためのループ回数を設定します",
			L"Set loop count before next track",
			L"Definir le nombre de boucles avant la piste suivante",
			L"Imposta il numero di loop prima del brano successivo",
			L"Establecer el numero de bucles antes de la siguiente pista",
			L"?? ??? ???? ?? ?? ??? ?????",
			L"?置?入下一曲目前的循?次数",
			L"????? ??? ????????? ??? ?????? ??????",
			L"Установить количество повторов перед следующим треком",
			L"Anzahl der Wiederholungen vor dem nachsten Titel festlegen",
			L"Definir numero de loops antes da proxima faixa",
			L"Aantal herhalingen instellen voor volgend nummer",
			L"Ustaw liczb? p?tli przed nast?pnym utworem",
			L"Sonraki parcaya gecmeden once dongu say?s?n? ayarla"));

		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER4), LL14(
			L"100%以上の音量を設定できます",
			L"Volume can exceed 100%",
			L"Le volume peut depasser 100%",
			L"Il volume puo superare il 100%",
			L"El volumen puede superar el 100%",
			L"100% ??? ??? ??? ? ????",
			L"音量可?置?100%以上",
			L"???? ?? ?????? ????? ????? 100%",
			L"Громкость может превышать 100%",
			L"Lautstarke kann 100% uberschreiten",
			L"Volume pode exceder 100%",
			L"Volume kan meer dan 100% zijn",
			L"G?o?no?? mo?e przekracza? 100%",
			L"Ses seviyesi %100'u a?abilir"));
		m_tooltip.AddTool(GetDlgItem(IDC_STATICds2), LL14(
			L"100%以上の音量を設定できます",
			L"Volume can exceed 100%",
			L"Le volume peut depasser 100%",
			L"Il volume puo superare il 100%",
			L"El volumen puede superar el 100%",
			L"100% ??? ??? ??? ? ????",
			L"音量可?置?100%以上",
			L"???? ?? ?????? ????? ????? 100%",
			L"Громкость может превышать 100%",
			L"Lautstarke kann 100% uberschreiten",
			L"Volume pode exceder 100%",
			L"Volume kan meer dan 100% zijn",
			L"G?o?no?? mo?e przekracza? 100%",
			L"Ses seviyesi %100'u a?abilir"));
		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER7), LL14(
			L"mp3,ogg,flac,m4a,opus,adpcm,dsd,kpiのテンポを変えます。wav,動画は変わりません。",
			L"Change tempo for mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video unchanged.",
			L"Modifier le tempo pour mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video inchanges.",
			L"Cambia il tempo per mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video invariati.",
			L"Cambiar el tempo para mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video sin cambios.",
			L"mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi? ??? ?????. wav,???? ???? ????.",
			L"更改mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi的速度。wav,??不?。",
			L"????? ??????? ?? mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav ???????? ?? ???????.",
			L"Изменить темп для mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,видео без изменений.",
			L"Tempo fur mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi andern. wav,Video unverandert.",
			L"Alterar o tempo para mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video inalterados.",
			L"Tempo wijzigen voor mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video ongewijzigd.",
			L"Zmie? tempo dla mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,wideo bez zmian.",
			L"mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi icin tempoyu de?i?tir. wav,video de?i?mez."));
		m_tooltip.AddTool(GetDlgItem(IDC_SLIDER8), LL14(
			L"mp3,ogg,flac,m4a,opus,adpcm,dsd,kpiのピッチを変えます。wav,動画は変わりません。",
			L"Change pitch for mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video unchanged.",
			L"Modifier la hauteur pour mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video inchanges.",
			L"Cambia il tono per mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video invariati.",
			L"Cambiar el tono para mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video sin cambios.",
			L"mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi? ??? ?????. wav,???? ???? ????.",
			L"更改mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi的音?。wav,??不?。",
			L"????? ???? ????? ?? mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav ???????? ?? ???????.",
			L"Изменить высоту тона для mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,видео без изменений.",
			L"Tonhohe fur mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi andern. wav,Video unverandert.",
			L"Alterar o pitch para mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video inalterados.",
			L"Toonhoogte wijzigen voor mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,video ongewijzigd.",
			L"Zmie? wysoko?? tonu dla mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi. wav,wideo bez zmian.",
			L"mp3,ogg,flac,m4a,opus,adpcm,dsd,kpi icin perdeyi de?i?tir. wav,video de?i?mez."));

		m_tooltip.AddTool(GetDlgItem(IDC_STATIC_t), LL14(
			L"テンポを100に戻します。",
			L"Reset tempo to 100.",
			L"Reinitialiser le tempo a 100.",
			L"Reimposta il tempo a 100.",
			L"Restablecer el tempo a 100.",
			L"??? 100?? ?????.",
			L"将速度重置?100。",
			L"????? ??? ??????? ??? 100.",
			L"Сбросить темп до 100.",
			L"Tempo auf 100 zurucksetzen.",
			L"Redefinir o tempo para 100.",
			L"Tempo terugzetten naar 100.",
			L"Zresetuj tempo do 100.",
			L"Tempoyu 100'e s?f?rla."));
		m_tooltip.AddTool(GetDlgItem(IDC_STATIC_p), LL14(
			L"ピッチを100に戻します。",
			L"Reset pitch to 100.",
			L"Reinitialiser la hauteur a 100.",
			L"Reimposta il tono a 100.",
			L"Restablecer el tono a 100.",
			L"??? 100?? ?????.",
			L"将音?重置?100。",
			L"????? ??? ???? ????? ??? 100.",
			L"Сбросить высоту тона до 100.",
			L"Tonhohe auf 100 zurucksetzen.",
			L"Redefinir o pitch para 100.",
			L"Toonhoogte terugzetten naar 100.",
			L"Zresetuj wysoko?? tonu do 100.",
			L"Perdeyi 100'e s?f?rla."));

		CString s;
		s.Format(LL14(
			L"%s\n↑の情報が間違っている時は↓の内容を作者へ\n詳細：Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nIf the info above is wrong, send the content below to the author\nDetails: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nSi les informations ci-dessus sont incorrectes, envoyez le contenu ci-dessous a l'auteur\nDetails : Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nSe le informazioni sopra sono errate, invia il contenuto sottostante all'autore\nDettagli: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nSi la informacion anterior es incorrecta, envia el contenido a continuacion al autor\nDetalles: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\n? ??? ??? ?? ?? ??? ????? ?????\n??: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\n如果上方信息有?，?将以下内容?送?作者\n??：Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\n??? ???? ????????? ????? ?????? ???? ??????? ????? ??? ??????\n????????: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nЕсли информация выше неверна, отправьте содержимое ниже автору\nПодробности: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nWenn die obigen Informationen falsch sind, senden Sie den folgenden Inhalt an den Autor\nDetails: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nSe as informacoes acima estiverem erradas, envie o conteudo abaixo ao autor\nDetalhes: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nAls de bovenstaande informatie onjuist is, stuur de onderstaande inhoud naar de auteur\nDetails: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nJe?li powy?sze informacje s? b??dne, wy?lij poni?sz? tre?? do autora\nSzczego?y: Ver %d.%d(%d) Build %d\n\n%s",
			L"%s\nYukar?daki bilgiler yanl??sa, a?a??daki iceri?i yazara gonderin\nAyr?nt?lar: Ver %d.%d(%d) Build %d\n\n%s"),
			os.GetVersionString(), in.dwMajorVersion, in.dwMinorVersion, edition, in.dwBuildNumber, cpus);		m_tooltip.AddTool(GetDlgItem(IDC_STATIC_OS), s);
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
	avx2 = LL14(L"SSE2未対応", L"SSE2 not supported", L"SSE2 non pris en charge", L"SSE2 non supportato", L"SSE2 no compatible", L"SSE2 ???", L"SSE2 不支持", L"SSE2 ??? ?????", L"SSE2 не поддерживается", L"SSE2 nicht unterstutzt", L"SSE2 nao suportado", L"SSE2 niet ondersteund", L"SSE2 nieobs?ugiwane", L"SSE2 desteklenmiyor");
	if (CPUInfo[0] >= 2) {
		__cpuid(CPUInfo, 0x00000001);
		if (CPUInfo[1] & (1 << 26))  avx2 = LL14(L"SSE2対応", L"SSE2 supported", L"SSE2 pris en charge", L"SSE2 supportato", L"SSE2 compatible", L"SSE2 ??", L"SSE2 支持", L"SSE2 ?????", L"SSE2 поддерживается", L"SSE2 unterstutzt", L"SSE2 suportado", L"SSE2 ondersteund", L"SSE2 obs?ugiwane", L"SSE2 destekleniyor");
		if (CPUInfo[2] & (1))  avx2 = LL14(L"SSE3対応", L"SSE3 supported", L"SSE3 pris en charge", L"SSE3 supportato", L"SSE3 compatible", L"SSE3 ??", L"SSE3 支持", L"SSE3 ?????", L"SSE3 поддерживается", L"SSE3 unterstutzt", L"SSE3 suportado", L"SSE3 ondersteund", L"SSE3 obs?ugiwane", L"SSE3 destekleniyor");
		if (CPUInfo[2] & (1 << 9))  avx2 = LL14(L"SSSE3対応", L"SSSE3 supported", L"SSSE3 pris en charge", L"SSSE3 supportato", L"SSSE3 compatible", L"SSSE3 ??", L"SSSE3 支持", L"SSSE3 ?????", L"SSSE3 поддерживается", L"SSSE3 unterstutzt", L"SSSE3 suportado", L"SSSE3 ondersteund", L"SSSE3 obs?ugiwane", L"SSSE3 destekleniyor");
		if (CPUInfo[2] & (1 << 12))  avx2 = LL14(L"FMA3対応", L"FMA3 supported", L"FMA3 pris en charge", L"FMA3 supportato", L"FMA3 compatible", L"FMA3 ??", L"FMA3 支持", L"FMA3 ?????", L"FMA3 поддерживается", L"FMA3 unterstutzt", L"FMA3 suportado", L"FMA3 ondersteund", L"FMA3 obs?ugiwane", L"FMA3 destekleniyor");
		if (CPUInfo[2] & (1 << 19))  avx2 = LL14(L"SSE4.1対応", L"SSE4.1 supported", L"SSE4.1 pris en charge", L"SSE4.1 supportato", L"SSE4.1 compatible", L"SSE4.1 ??", L"SSE4.1 支持", L"SSE4.1 ?????", L"SSE4.1 поддерживается", L"SSE4.1 unterstutzt", L"SSE4.1 suportado", L"SSE4.1 ondersteund", L"SSE4.1 obs?ugiwane", L"SSE4.1 destekleniyor");
		if (CPUInfo[2] & (1 << 20))  avx2 = LL14(L"SSE4.2対応", L"SSE4.2 supported", L"SSE4.2 pris en charge", L"SSE4.2 supportato", L"SSE4.2 compatible", L"SSE4.2 ??", L"SSE4.2 支持", L"SSE4.2 ?????", L"SSE4.2 поддерживается", L"SSE4.2 unterstutzt", L"SSE4.2 suportado", L"SSE4.2 ondersteund", L"SSE4.2 obs?ugiwane", L"SSE4.2 destekleniyor");
	}
	if (CPUInfo[0] >= 2) {
		__cpuid(CPUInfo, 0x80000001);
		if (CPUInfo[2] & (1 << 6))  avx2 = LL14(L"SSE4a対応", L"SSE4a supported", L"SSE4a pris en charge", L"SSE4a supportato", L"SSE4a compatible", L"SSE4a ??", L"SSE4a 支持", L"SSE4a ?????", L"SSE4a поддерживается", L"SSE4a unterstutzt", L"SSE4a suportado", L"SSE4a ondersteund", L"SSE4a obs?ugiwane", L"SSE4a destekleniyor");
	}
	__cpuid(CPUInfo, 0x00000001);
	if (CPUInfo[0] >= 2) {
		if (CPUInfo[2] & (1 << 28))  avx2 = LL14(L"AVX対応", L"AVX supported", L"AVX pris en charge", L"AVX supportato", L"AVX compatible", L"AVX ??", L"AVX 支持", L"AVX ?????", L"AVX поддерживается", L"AVX unterstutzt", L"AVX suportado", L"AVX ondersteund", L"AVX obs?ugiwane", L"AVX destekleniyor");
	}
	if (CPUInfo[0] >= 7) {
		__cpuid(CPUInfo, 0x00000007);
		if (CPUInfo[1] & (1 << 5))  avx2 = LL14(L"AVX2対応", L"AVX2 supported", L"AVX2 pris en charge", L"AVX2 supportato", L"AVX2 compatible", L"AVX2 ??", L"AVX2 支持", L"AVX2 ?????", L"AVX2 поддерживается", L"AVX2 unterstutzt", L"AVX2 suportado", L"AVX2 ondersteund", L"AVX2 obs?ugiwane", L"AVX2 destekleniyor");
		if (CPUInfo[1] & (1 << 16))  avx2 = LL14(L"AVX512対応", L"AVX512 supported", L"AVX512 pris en charge", L"AVX512 supportato", L"AVX512 compatible", L"AVX512 ??", L"AVX512 支持", L"AVX512 ?????", L"AVX512 поддерживается", L"AVX512 unterstutzt", L"AVX512 suportado", L"AVX512 ondersteund", L"AVX512 obs?ugiwane", L"AVX512 destekleniyor");
	}
	s.Format(_T("%s / %s"), cpus, avx2);
	s.Trim();
	m_cpu.SetWindowText(s);

	// CPU 拡張命令一覧
	avx2 = LL14(L"使用可能命令：", L"Available instructions: ", L"Instructions disponibles : ", L"Istruzioni disponibili: ", L"Instrucciones disponibles: ", L"?? ??? ??: ", L"可用指令：", L"????????? ???????: ", L"Доступные инструкции: ", L"Verfugbare Befehle: ", L"Instrucoes disponiveis: ", L"Beschikbare instructies: ", L"Dost?pne instrukcje: ", L"Kullan?labilir talimatlar: ");
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
	m_lrc2.SetWindowText(LL14(L"歌詞(.lrc)が表示されます", L"Lyrics (.lrc) will be displayed here", L"Paroles (.lrc) affichees ici", L"Testi (.lrc) visualizzati qui", L"Letra (.lrc) mostrada aqui", L"??(.lrc)? ??? ?????", L"歌?(.lrc)将在此?示", L"????? (.lrc) ???? ???", L"Текст (.lrc) отображается здесь", L"Liedtext (.lrc) wird hier angezeigt", L"Letra (.lrc) exibida aqui", L"Songtekst (.lrc) wordt hier getoond", L"Teksty (.lrc) wy?wietlone tutaj", L"Soz (.lrc) burada goruntulenir"));
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
		{ CFile f; if (f.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 12:
		ret = _tchdir(savedata.ys122);
		if (_chdir("wave\\wave_44") == -1) {
			if (_chdir("wave\\wave_22") == -1) { ret = -1; break; }
			wavbit = 22050;
		}
		else wavbit = 44100;
		{ CFile f; if (f.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) ret = -1; else f.Close(); }
		break;
	case 13:
		ret = _tchdir(savedata.sor);
		if (_chdir("WAVE\\WAVE44") == -1) {
			if (_chdir("WAVE\\WAVE22") == -1) { ret = -1; break; }
			wavbit = 22050;
		}
		else wavbit = 44100;
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
		fnn = LL14(
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Aucun fichier ou dossier",
			L"Nessun file o cartella",
			L"No hay archivo o carpeta",
			L"?? ?? ??? ????",
			L"没有文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Keine Datei oder Ordner",
			L"Nenhum arquivo ou pasta",
			L"Geen bestand of map",
			L"Brak pliku lub folderu",
			L"Dosya veya klasor yok");
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
			L"ファイルが開けませんでした。\n削除されたか移動した可能性があります。",
			L"Could not open file.\nIt may have been deleted or moved.",
			L"Impossible d'ouvrir le fichier.\nIl a peut-etre ete supprime ou deplace.",
			L"Impossibile aprire il file.\nPotrebbe essere stato eliminato o spostato.",
			L"No se pudo abrir el archivo.\nPuede haber sido eliminado o movido.",
			L"??? ? ? ????.\n?????? ????? ???? ????.",
			L"无法打?文件。\n?文件可能已被?除或移?。",
			L"???? ??? ?????.\n???? ?? ???? ?? ????.",
			L"Не удалось открыть файл.\nВозможно, он был удалён или перемещён.",
			L"Datei konnte nicht geoffnet werden.\nSie wurde moglicherweise geloscht oder verschoben.",
			L"Nao foi possivel abrir o arquivo.\nEle pode ter sido excluido ou movido.",
			L"Kan het bestand niet openen.\nHet is mogelijk verwijderd of verplaatst.",
			L"Nie mo?na otworzy? pliku.\nMog? zosta? usuni?ty lub przeniesiony.",
			L"Dosya ac?lamad?.\nSilinmi? veya ta??nm?? olabilir."));		stop1();
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
	else if (mode == -3 || mode == -10 || mode == -9 || mode == -8 || mode == -7 || mode == -6 || mode == 30 || mode == 999) {

	}
	else {
		if (mode != -6) {
			oggsize = LoadOggVorbis(filen, 2, &ogg, m_time);
			if (oggsize < 0) {
				m_saisai.EnableWindow(TRUE);
				fnn = LL14(
					L"ファイル又はフォルダがありません",
					L"No file or folder",
					L"Aucun fichier ou dossier",
					L"Nessun file o cartella",
					L"No hay archivo o carpeta",
					L"?? ?? ??? ????",
					L"没有文件或文件?",
					L"?? ???? ??? ?? ????",
					L"Файл или папка не найдены",
					L"Keine Datei oder Ordner",
					L"Nenhum arquivo ou pasta",
					L"Geen bestand of map",
					L"Brak pliku lub folderu",
					L"Dosya veya klasor yok"); endflg = 0;
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
	//ファイル保存用（wavExport時はcc1を後で設定するためここではリセットしない）
	if (wavExportPath.GetLength() == 0) cc1 = 0;
	playb = 0;
	m_time.SetPos((int)playb);
	if (ogg) ov_pcm_seek(&vf, (ogg_int64_t)0);
	poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
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
		stitle = LL14(L"バトル#58", L"Battle #58", L"Bataille #58", L"Battaglia #58", L"Batalla #58", L"?? #58", L"?斗 #58", L"????? #58", L"Битва #58", L"Kampf #58", L"Batalha #58", L"Gevecht #58", L"Bitwa #58", L"Sava? #58");
	}
	if (ss == "yc_b002.ogg") {
		loop1 = 504378;
		loop2 = 5153813;
		stitle = LL14(L"灼熱の炎の中で", L"Within the Blazing Flames", L"Dans les flammes ardentes", L"Tra le fiamme ardenti", L"Entre las llamas ardientes", L"???? ?? ???", L"在灼?的火?中", L"?? ??? ????? ??????", L"В пылающем пламени", L"Im lodernden Feuer", L"Entre as chamas ardentes", L"In de laaiende vlammen", L"W p?on?cych p?omieniach", L"Alevlerin ?cinde");
	}
	if (ss == "yc_b003.ogg") {
		loop1 = 32845;
		loop2 = 6955200;
		stitle = LL14(L"最終決戦", L"Final Battle", L"Bataille finale", L"Battaglia finale", L"Batalla final", L"?? ??", L"最?决?", L"??????? ???????", L"Финальная битва", L"Endkampf", L"Batalha final", L"Eindstrijd", L"Ostateczna bitwa", L"Son Sava?");
	}
	if (ss == "yc_b004.ogg") {
		loop1 = 53237;
		loop2 = 9737128;
		stitle = LL14(L"黒き翼", L"Black Wings", L"Ailes noires", L"Ali nere", L"Alas negras", L"?? ??", L"黑色之翼", L"??????? ???????", L"Чёрные крылья", L"Schwarze Flugel", L"Asas negras", L"Zwarte vleugels", L"Czarne skrzyd?a", L"Siyah Kanatlar");
	}
	if (ss == "yc_b005.ogg") {
		loop1 = 1123422;
		loop2 = 7687672;
		stitle = "The False God of Causality";
	}
	if (ss == "yc_d101.ogg") {
		loop1 = 303237;
		loop2 = 2582426;
		stitle = LL14(L"ダンジョン", L"Dungeon", L"Donjon", L"Dungeon", L"Mazmorra", L"??", L"地牢", L"??????", L"Подземелье", L"Verlies", L"Masmorra", L"Kerker", L"Loch", L"Zindan");
	}
	if (ss == "yc_d201.ogg") {
		loop1 = 447792;
		loop2 = 3479666;
		stitle = LL14(L"道化師の誘い", L"Clown's Invitation", L"L'invitation du bouffon", L"L'invito del giullare", L"La invitacion del bufon", L"??? ??", L"小丑的邀?", L"???? ??????", L"Приглашение шута", L"Einladung des Clowns", L"Convite do palhaco", L"Uitnodiging van de clown", L"Zaproszenie klauna", L"Palyaconun Daveti");
	}
	if (ss == "yc_d301.ogg") {
		loop1 = 351836;
		loop2 = 3969072;
		stitle = LL14(L"地下遺跡", L"Underground Ruins", L"Ruines souterraines", L"Rovine sotterranee", L"Ruinas subterraneas", L"?? ??", L"地下?迹", L"????? ??? ?????", L"Подземные руины", L"Unterirdische Ruinen", L"Ruinas subterraneas", L"Ondergrondse ruines", L"Podziemne ruiny", L"Yeralt? Harabeleri");
	}
	if (ss == "yc_d401.ogg") {
		loop1 = 93865;
		loop2 = 4349569;
		stitle = LL14(L"導きの塔〜エルディールにくちづけを", L"Tower of Guidance -Kiss for Eldeel-", L"Tour de la Guidance -Un baiser pour Eldeel-", L"Torre della Guida -Un bacio per Eldeel-", L"Torre de la Guia -Un beso para Eldeel-", L"??? ? ~????? ????~", L"引?之塔〜献?埃?迪?的吻", L"??? ??????? -???? ??????-", L"Башня Наставления -Поцелуй для Элдила-", L"Turm der Fuhrung -Kuss fur Eldeel-", L"Torre da Orientacao -Um beijo para Eldeel-", L"Toren van Geleiding -Kus voor Eldeel-", L"Wie?a Przewodnictwa -Poca?unek dla Eldeel-", L"Rehberlik Kulesi -Eldeel icin Opucuk-");
	}
	if (ss == "yc_d501.ogg") {
		loop1 = 832720;
		loop2 = 7219417;
		stitle = LL14(L"失われし仮面を求めて", L"Seeking the Lost Mask", L"A la recherche du masque perdu", L"Alla ricerca della maschera perduta", L"En busca de la mascara perdida", L"???? ??? ???", L"?找失落的面具", L"????? ?? ?????? ???????", L"В поисках утерянной маски", L"Auf der Suche nach der verlorenen Maske", L"Em busca da mascara perdida", L"Op zoek naar het verloren masker", L"W poszukiwaniu zaginionej maski", L"Kay?p Maskeyi Ararken");
	}
	if (ss == "yc_d701.ogg") {
		loop1 = 809264;
		loop2 = 6545498;
		stitle = LL14(L"イリス", L"Iris", L"Iris", L"Iris", L"Iris", L"???", L"伊莉?", L"?????", L"Ирис", L"Iris", L"Iris", L"Iris", L"Iris", L"Iris");
	}
	if (ss == "yc_d702.ogg") {
		loop1 = 34816;
		loop2 = 1189171;
		stitle = "yc_d702";
	}
	if (ss == "yc_d703.ogg") {
		loop1 = 719876;
		loop2 = 2557197;
		stitle = LL14(L"聖域", L"Sanctuary", L"Sanctuaire", L"Santuario", L"Santuario", L"??", L"?域", L"?????? ??????", L"Святилище", L"Heiligtum", L"Santuario", L"Heiligdom", L"Sanktuarium", L"Kutsal Alan");
	}
	if (ss == "yc_e001.ogg") {
		loop1 = 300048;
		loop2 = 3389821;
		stitle = LL14(L"賢者", L"Sage", L"Sage", L"Saggio", L"Sabio", L"??", L"?者", L"??????", L"Мудрец", L"Weiser", L"Sabio", L"Wijze", L"M?drzec", L"Bilge");
	}
	if (ss == "yc_e002.ogg") {
		loop1 = 326209;
		loop2 = 3604271;
		stitle = LL14(L"復活の儀式", L"Resurrection Ceremony", L"Ceremonie de resurrection", L"Cerimonia della resurrezione", L"Ceremonia de resurreccion", L"??? ??", L"?活?式", L"???? ?????", L"Церемония воскрешения", L"Auferstehungszeremonie", L"Cerimonia de ressurreicao", L"Opstandingsceremonie", L"Ceremonia zmartwychwstania", L"Dirili? Toreni");
	}
	if (ss == "yc_e003.ogg") {
		loop1 = 806906;
		loop2 = 4275899;
		stitle = LL14(L"レファンス", L"Refance", L"Refance", L"Refance", L"Refance", L"???", L"雷凡斯", L"??????", L"Рефанс", L"Refance", L"Refance", L"Refance", L"Refance", L"Refance");
	}
	if (ss == "yc_e004.ogg") {
		loop1 = 326209;
		loop2 = 4945888;
		stitle = LL14(L"涙の少年剣士", L"Young Swordsman in Tears", L"Le jeune epeiste en larmes", L"Il giovane spadaccino in lacrime", L"El joven espadachin en lagrimas", L"??? ?? ??", L"含泪的少年?士", L"?????? ????? ???????", L"Юный фехтовальщик в слезах", L"Der junge Schwertkampfer in Tranen", L"O jovem espadachim em lagrimas", L"De jonge zwaardvechter in tranen", L"M?ody szermierz we ?zach", L"Gozya?lar?ndaki Genc K?l?c Sava?c?s?");
	}
	if (ss == "yc_e005.ogg") {
		loop1 = 24000;
		loop2 = 3605888;
		stitle = LL14(L"エルディール", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"???", L"埃?迪?", L"?????", L"Элдил", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel");
	}
	if (ss == "yc_e006.ogg") {
		loop1 = 69040;
		loop2 = 1209633;
		stitle = LL14(L"ロムン帝国 -嗚呼レオ団長-", L"Romun Empire -Alas Captain Leo-", L"Empire Romun -Helas Capitaine Leo-", L"Impero Romun -Ahime Capitano Leo-", L"Imperio Romun -!Ay, Capitan Leo!-", L"?? ?? -?? ?? ??-", L"?曼帝国 -?，雷欧??-", L"?????????? ???? -?? ???? ??????? ???-", L"Империя Ромун -Увы, капитан Лео-", L"Romun-Reich -Ach, Hauptmann Leo-", L"Imperio Romun -Ai, Capitao Leo-", L"Romun Keizerrijk -Helaas Kapitein Leo-", L"Imperium Romun -Niestety Kapitanie Leo-", L"Romun ?mparatorlu?u -Ah Kaptan Leo-");
	}
	if (ss == "yc_e008.ogg") {
		loop1 = 275476;
		loop2 = 3609611;
		stitle = "yc_e008";
	}
	if (ss == "yc_e010.ogg") {
		loop1 = 807040;
		loop2 = 5159922;
		stitle = LL14(L"冒険家、誕生", L"Birth of an Adventurer", L"Naissance d'un aventurier", L"Nascita di un avventuriero", L"Nacimiento de un aventurero", L"???, ??", L"冒?家的?生", L"????? ?????", L"Рождение авантюриста", L"Geburt eines Abenteurers", L"Nascimento de um aventureiro", L"Geboorte van een avonturier", L"Narodziny poszukiwacza przygod", L"Bir Macerac?n?n Do?u?u");
	}
	if (ss == "yc_f101.ogg") {
		loop1 = 568926;
		loop2 = 5668207;
		stitle = LL14(L"燃ゆる剣", L"Burning Sword", L"L'epee ardente", L"La spada ardente", L"La espada ardiente", L"??? ?", L"燃?之?", L"????? ??????", L"Пылающий меч", L"Das brennende Schwert", L"A espada ardente", L"Het brandende zwaard", L"P?on?cy miecz", L"Yanan K?l?c");
	}
	if (ss == "yc_f201.ogg") {
		loop1 = 588624;
		loop2 = 6209316;
		stitle = LL14(L"セルセタの樹海", L"Forest of Celceta", L"Foret de Celceta", L"Foresta di Celceta", L"Bosque de Celceta", L"???? ??", L"塞?塞塔的?海", L"???? ???????", L"Лес Селсеты", L"Wald von Celceta", L"Floresta de Celceta", L"Woud van Celceta", L"Las Celcety", L"Celceta Orman?");
	}
	if (ss == "yc_f301.ogg") {
		loop1 = 1145404;
		loop2 = 5960203;
		stitle = LL14(L"クレーター", L"Crater", L"Cratere", L"Cratere", L"Crater", L"????", L"?石坑", L"?????? ?????????", L"Кратер", L"Krater", L"Cratera", L"Krater", L"Krater", L"Krater");
	}
	if (ss == "yc_f401.ogg") {
		loop1 = 408974;
		loop2 = 3161454;
		stitle = "THE DAWN OF YS";
	}
	if (ss == "yc_f501.ogg") {
		loop1 = 2604464;
		loop2 = 4559688;
		stitle = LL14(L"暁の森", L"Forest of Dawn", L"Foret de l'aube", L"Foresta dell'alba", L"Bosque del amanecer", L"??? ?", L"黎明之森", L"???? ?????", L"Лес рассвета", L"Wald der Morgenrote", L"Floresta do amanhecer", L"Woud van de dageraad", L"Las ?witu", L"?afak Orman?");
	}
	if (ss == "yc_f601.ogg") {
		loop1 = 581264;
		loop2 = 3661828;
		stitle = LL14(L"一陣の風", L"Gust of Wind", L"Rafale de vent", L"Folata di vento", L"Rafaga de viento", L"? ?? ??", L"一??", L"??? ???", L"Порыв ветра", L"Windboe", L"Rajada de vento", L"Windvlaag", L"Podmuch wiatru", L"Ruzgar Esintisi");
	}
	if (ss == "yc_f701.ogg") {
		loop1 = 324287;
		loop2 = 9010870;
		stitle = LL14(L"神代の地", L"Land of the Gods", L"Terre des dieux", L"Terra degli dei", L"Tierra de los dioses", L"??? ?", L"神代之地", L"??? ??????", L"Земля богов", L"Land der Gotter", L"Terra dos deuses", L"Land der goden", L"Kraina bogow", L"Tanr?lar?n Topra??");
	}
	if (ss == "yc_f801.ogg") {
		loop1 = 315435;
		loop2 = 4546653;
		stitle = LL14(L"真実への序曲", L"Overture to Truth", L"Ouverture vers la verite", L"Ouverture alla verita", L"Obertura hacia la verdad", L"???? ??", L"通往真?的序曲", L"????? ??? ???????", L"Увертюра к истине", L"Ouverture zur Wahrheit", L"Abertura para a verdade", L"Ouverture naar de waarheid", L"Uwertura do prawdy", L"Gerce?e Uvertur");
	}
	if (ss == "yc_f901.ogg") {
		loop1 = 178544;
		loop2 = 4786555;
		stitle = LL14(L"雨上がりの朝に", L"Morning After the Rain", L"Matin apres la pluie", L"Mattino dopo la pioggia", L"Manana despues de la lluvia", L"?? ???", L"雨后的早晨", L"???? ?? ??? ?????", L"Утро после дождя", L"Morgen nach dem Regen", L"Manha apos a chuva", L"Ochtend na de regen", L"Poranek po deszczu", L"Ya?mur Sonras? Sabah");
	}
	if (ss == "yc_over.ogg") {
		loop1 = 19200;
		loop2 = 4924407;
		stitle = LL14(L"ゲームオーバー", L"Game Over", L"Partie terminee", L"Game Over", L"Fin del juego", L"?? ??", L"游??束", L"????? ??????", L"Конец игры", L"Spiel vorbei", L"Fim de jogo", L"Spel voorbij", L"Koniec gry", L"Oyun Bitti");
	}
	if (ss == "yc_t101.ogg") {
		loop1 = 865353;
		loop2 = 4409988;
		stitle = LL14(L"辺境都市《キャスナン》", L"Frontier City Casnan", L"Ville frontaliere Casnan", L"Citta di frontiera Casnan", L"Ciudad fronteriza Casnan", L"?? ?? 《???》", L"?境都市《?斯南》", L"????? ?????? ??????", L"Пограничный город Каснан", L"Grenzstadt Casnan", L"Cidade de fronteira Casnan", L"Grensstad Casnan", L"Miasto graniczne Casnan", L"S?n?r Kenti Casnan");
	}
	if (ss == "yc_t201.ogg") {
		loop1 = 58906;
		loop2 = 6120526;
		stitle = LL14(L"優しくなりたい", L"I Want to Be Kind", L"Je veux etre gentil(le)", L"Voglio essere gentile", L"Quiero ser amable", L"????? ??", L"想要?得温柔", L"???? ?? ???? ??????", L"Я хочу быть добрым", L"Ich mochte freundlich sein", L"Quero ser gentil", L"Ik wil aardig zijn", L"Chc? by? uprzejmy", L"?yi Biri Olmak ?stiyorum");
	}
	if (ss == "yc_t301.ogg") {
		loop1 = 425910;
		loop2 = 9606150;
		stitle = LL14(L"古代の伝承", L"Ancient Legend", L"Legende ancienne", L"Leggenda antica", L"Leyenda antigua", L"??? ??", L"古代??", L"???????? ???????", L"Древняя легенда", L"Alte Legende", L"Lenda antiga", L"Oude legende", L"Staro?ytna legenda", L"Kadim Efsane");
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
		stitle = LL14(L"新たな時代のステージへ", L"To the Stage of a New Era", L"Vers la scene d'une nouvelle ere", L"Verso il palcoscenico di una nuova era", L"Al escenario de una nueva era", L"??? ??? ???", L"走向新?代的舞台", L"??? ???? ??? ????", L"На сцену новой эпохи", L"Auf die Buhne einer neuen Ara", L"Para o palco de uma nova era", L"Naar het podium van een nieuw tijdperk", L"Na scen? nowej ery", L"Yeni Bir Ca??n Sahnesine");
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
				L"Likely Ao no Kiseki BGM data, but no info in Ao BGM table.\nUse Zero no Kiseki BGM table?\n(Ao includes Zero BGM data; loop info is in Zero.)",
				L"Probablement des donnees BGM d'Ao no Kiseki, mais aucune info dans la table BGM d'Ao.\nUtiliser la table BGM de Zero no Kiseki?\n(Ao contient les donnees BGM de Zero ; les infos de boucle sont dans Zero.)",
				L"Probabilmente dati BGM di Ao no Kiseki, ma nessuna info nella tabella BGM di Ao.\nUsare la tabella BGM di Zero no Kiseki?\n(Ao include i dati BGM di Zero; le info loop sono in Zero.)",
				L"Probablemente datos BGM de Ao no Kiseki, pero sin info en la tabla BGM de Ao.\n?Usar la tabla BGM de Zero no Kiseki?\n(Ao incluye datos BGM de Zero; la info de bucle esta en Zero.)",
				L"??? ?? ?? BGM ??????, ?? ?? BGM ???? ??? ????.\n?? ?? BGM ???? ??????\n(?? ???? ?? ?? BGM ???? ???? ??, ?? ??? ?? ?? ?? ????)",
				L"可能是碧之?迹的BGM数据，但碧之?迹BGM表中没有相?信息。\n是否参照零之?迹的BGM表？\n(碧之?迹中也包含零之?迹的BGM数据，因此循?信息在零之?迹一?)",
				L"???? ??? ?????? BGM ?? Ao no Kiseki? ??? ?? ???? ??????? ?? ???? BGM ????? ??.\n?? ???? ??????? ???? BGM ?? Zero no Kiseki?\n(????? Ao ?????? BGM ?? Zero? ??????? ?????? ?????? ?? Zero.)",
				L"Вероятно, данные BGM из Ao no Kiseki, но в таблице BGM Ao нет информации.\nИспользовать таблицу BGM Zero no Kiseki?\n(Ao включает данные BGM из Zero; информация о петле находится в Zero.)",
				L"Wahrscheinlich Ao no Kiseki BGM-Daten, aber keine Info in der Ao BGM-Tabelle.\nZero no Kiseki BGM-Tabelle verwenden?\n(Ao enthalt Zero BGM-Daten; Loop-Info befindet sich in Zero.)",
				L"Provavelmente dados BGM de Ao no Kiseki, mas sem informacao na tabela BGM de Ao.\nUsar a tabela BGM de Zero no Kiseki?\n(Ao inclui dados BGM de Zero; informacoes de loop estao em Zero.)",
				L"Waarschijnlijk Ao no Kiseki BGM-data, maar geen info in de Ao BGM-tabel.\nZero no Kiseki BGM-tabel gebruiken?\n(Ao bevat Zero BGM-data; loop-info staat in Zero.)",
				L"Prawdopodobnie dane BGM z Ao no Kiseki, ale brak informacji w tabeli BGM Ao.\nU?y? tabeli BGM Zero no Kiseki?\n(Ao zawiera dane BGM z Zero; informacje o p?tli s? w Zero.)",
				L"Muhtemelen Ao no Kiseki BGM verisi, ancak Ao BGM tablosunda bilgi yok.\nZero no Kiseki BGM tablosu kullan?ls?n m??\n(Ao, Zero BGM verilerini de icerir; dongu bilgisi Zero taraf?ndad?r.)"),
				LL14(
					L"bgmテーブルに情報がありません。",
					L"No info in BGM table.",
					L"Aucune info dans la table BGM.",
					L"Nessuna info nella tabella BGM.",
					L"Sin informacion en la tabla BGM.",
					L"BGM ???? ??? ????.",
					L"BGM表中没有信息。",
					L"?? ???? ??????? ?? ???? BGM.",
					L"Нет информации в таблице BGM.",
					L"Keine Info in der BGM-Tabelle.",
					L"Sem informacao na tabela BGM.",
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
		if (ff.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) == FALSE) {
			MessageBox(LL14(
				L"ファイルが存在しません。\n削除されたかフォルダまたはファイル名が変更された可能性があります。",
				L"File does not exist.\nIt may have been deleted or the folder/filename may have changed.",
				L"Le fichier n'existe pas.\nIl a peut-etre ete supprime ou le dossier/nom de fichier a ete modifie.",
				L"Il file non esiste.\nPotrebbe essere stato eliminato o la cartella/nome file potrebbe essere cambiato.",
				L"El archivo no existe.\nPuede haber sido eliminado o la carpeta/nombre de archivo puede haber cambiado.",
				L"??? ???? ????.\n?????? ?? ?? ???? ????? ???? ????.",
				L"文件不存在。\n?文件可能已被?除，或文件?/文件名已更改。",
				L"????? ??? ?????.\n???? ?? ???? ?? ????? ??? ?????? ?? ?????.",
				L"Файл не существует.\nВозможно, он был удалён или изменено имя папки/файла.",
				L"Datei existiert nicht.\nSie wurde moglicherweise geloscht oder der Ordner/Dateiname wurde geandert.",
				L"O arquivo nao existe.\nEle pode ter sido excluido ou a pasta/nome do arquivo pode ter mudado.",
				L"Het bestand bestaat niet.\nHet is mogelijk verwijderd of de map/bestandsnaam is gewijzigd.",
				L"Plik nie istnieje.\nMog? zosta? usuni?ty lub zmieniono nazw? folderu/pliku.",
				L"Dosya mevcut de?il.\nSilinmi? veya klasor/dosya ad? de?i?tirilmi? olabilir."),
				LL14(
					L"ファイルが存在しません。",
					L"File does not exist.",
					L"Le fichier n'existe pas.",
					L"Il file non esiste.",
					L"El archivo no existe.",
					L"??? ???? ????.",
					L"文件不存在。",
					L"????? ??? ?????.",
					L"Файл не существует.",
					L"Datei existiert nicht.",
					L"O arquivo nao existe.",
					L"Het bestand bestaat niet.",
					L"Plik nie istnieje.",
					L"Dosya mevcut de?il.")); m_saisai.EnableWindow(TRUE); endflg = 0; return;
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
	else if (mode == 999) { // wav
		CString s, ss;
		s = filen.Left(filen.ReverseFind('\\')); ss = filen.Right(filen.GetLength() - filen.ReverseFind('\\') - 1);
		if (s.GetLength() > 0) _tchdir(s);
		wavinfo wi;
		if (!wav_.Open(filen, &wi)) {
			m_saisai.EnableWindow(TRUE); endflg = 0; return;
		}
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
			wavch = wi.nChannels;
			wavbit = wi.nSamplesPerSec;
			wavsam = 16;
			loop1 = 0;
			loop2 = data_size / 4;
			lenl = 0;
			poss = poss2 = poss3 = poss4 = poss5 = poss6 = 0;
			wav999_use_adbuf = 1;
			if (wav) free(wav);
			wav_start();
			m_time.SetRange(0, data_size / 4, TRUE);
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
			wavch = wi.nChannels;
			wavbit = wi.nSamplesPerSec;
			// ファイルの実際のビット深度を優先（wavsam > bit24）。解釈は実データに合わせる
			wavsam = (wi.wBitsPerSample <= 16) ? 16 : 24;
			loop1 = 0;
			loop2 = (int)wi.totalSamples;
			data_size = oggsize = (int)(loop2 * (wavsam / 8) * wavch);
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
		if (flg0 == 1) sikpi.dwSamplesPerSec = wavbit;


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
		ff.Open(filen, CFile::modeRead | CFile::shareDenyWrite, NULL);
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
				MessageBox(LL14(
					L"なんらかの要因でkpiが開けませんでした。",
					L"Could not open kpi for some reason.",
					L"Impossible d'ouvrir kpi pour une raison quelconque.",
					L"Impossibile aprire kpi per qualche motivo.",
					L"No se pudo abrir kpi por alguna razon.",
					L"??? ???? kpi? ? ? ?????.",
					L"由于某?原因无法打?kpi。",
					L"???? ??? kpi ???? ??.",
					L"Не удалось открыть kpi по какой-то причине.",
					L"kpi konnte aus einem unbekannten Grund nicht geoffnet werden.",
					L"Nao foi possivel abrir o kpi por algum motivo.",
					L"Kan kpi om een of andere reden niet openen.",
					L"Nie mo?na otworzy? kpi z jakiego? powodu.",
					L"Kpi bir nedenle ac?lamad?."),
					LL14(
						L"ファイルが存在しません。",
						L"File does not exist.",
						L"Le fichier n'existe pas.",
						L"Il file non esiste.",
						L"El archivo no existe.",
						L"??? ???? ????.",
						L"文件不存在。",
						L"????? ??? ?????.",
						L"Файл не существует.",
						L"Datei existiert nicht.",
						L"O arquivo nao existe.",
						L"Het bestand bestaat niet.",
						L"Plik nie istnieje.",
						L"Dosya mevcut de?il."));
				fnn = LL14(
					L"kpi構造体を獲得できませんでした。",
					L"Could not acquire kpi structure.",
					L"Impossible d'obtenir la structure kpi.",
					L"Impossibile acquisire la struttura kpi.",
					L"No se pudo adquirir la estructura kpi.",
					L"kpi ???? ??? ? ?????.",
					L"无法?取kpi??体。",
					L"???? ?????? ??? ???? kpi.",
					L"Не удалось получить структуру kpi.",
					L"kpi-Struktur konnte nicht abgerufen werden.",
					L"Nao foi possivel obter a estrutura kpi.",
					L"Kan kpi-structuur niet verkrijgen.",
					L"Nie mo?na uzyska? struktury kpi.",
					L"Kpi yap?s? edinilemedi."); FreeLibrary(hDLLk);
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
		CFile ffff; if (ffff.Open(sss + L"\\..\\text\\t_bgm._dt", CFile::modeRead | CFile::shareDenyWrite)) { fg = 1; ffff.Close(); }
		if (ss.Mid(0, 3) == L"ed7" && fg == 1) {
			CString a;
			switch (_ttoi(ss.Mid(2, 4))) {
			case 7001:
				a = LL14(L"零の軌跡", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"?? ??", L"零之?迹", L"?????? ?? ?????", L"Тропы от Нуля", L"Trails from Zero", L"Trilhas do Zero", L"Trails from Zero", L"Trails from Zero", L"S?f?rdan ?zler");
				break;
			case 7002:
				a = L"way of live -Opening Version-";
				break;
			case 7003:
				a = LL14(L"新しき日々〜予兆", L"New Days -Omen-", L"Nouveaux Jours -Presage-", L"Nuovi Giorni -Presagio-", L"Nuevos Dias -Presagio-", L"??? ??~?兆", L"?新之日~?兆", L"???? ????? -????-", L"Новые Дни -Предзнаменование-", L"Neue Tage -Omen-", L"Novos Dias -Pressagio-", L"Nieuwe Dagen -Voorteken-", L"Nowe Dni -Omen-", L"Yeni Gunler -??aret-");
				break;
			case 7005:
				a = LL14(L"想い破れて・・・", L"Broken Heart...", L"C?ur Brise...", L"Cuore Spezzato...", L"Corazon Roto...", L"??? ???...", L"心意破碎...", L"??? ?????...", L"Разбитое Сердце...", L"Gebrochenes Herz...", L"Coracao Partido...", L"Gebroken Hart...", L"Z?amane Serce...", L"K?r?k Kalp...");
				break;
			case 7052:
				a = LL14(L"碧い軌跡 -Opening size-", L"Azure Arbitrator -Opening size-", L"Arbitre Azure -taille ouverture-", L"Arbitro Azzurro -dimensione apertura-", L"Arbitro Azur -tamano apertura-", L"?? ?? -Opening size-", L"碧之?迹 -片?版-", L"??????? ?????? -??? ??????????-", L"Лазурный Арбитр -размер открытия-", L"Azur-Schiedsrichter -Eroffnungsgrose-", L"Arbitro Azul -tamanho abertura-", L"Azure Scheidsrechter -openingsgrootte-", L"Lazurowy Arbitr -rozmiar otwieraj?cy-", L"Gok Mavisi Hakem -ac?l?? boyutu-");
				break;
			case 7053:
				a = LL14(L"それでも僕らは。", L"Yet We're Still Here.", L"Pourtant Nous Sommes La.", L"Eppure Siamo Ancora Qui.", L"Pero Seguimos Aqui.", L"??? ???.", L"即便如此我?仍在。", L"????? ?? ???? ???.", L"Но Мы Всё Ещё Здесь.", L"Dennoch Sind Wir Noch Hier.", L"Mas Ainda Estamos Aqui.", L"Toch Zijn We Er Nog.", L"A Jednak Nadal Tu Jeste?my.", L"Yine de Buraday?z.");
				break;
			case 7100:
				a = LL14(L"街角の風景", L"Street Corner Scenery", L"Scene de Rue", L"Scena Angolo Strada", L"Paisaje de Esquina", L"?? ??? ??", L"街角?景", L"???? ????? ??????", L"Вид Угола Улицы", L"Strasenecken-Szenerie", L"Cenario da Esquina", L"Straathoekscene", L"Scena Ulicznego Rogu", L"Sokak Ko?esi Manzaras?");
				break;
			case 7101:
				a = LL14(L"明日は明日の風が吹く", L"Tomorrow the Wind Will Blow", L"Demain le Vent Soufflera", L"Domani Soffiera il Vento", L"Manana Soplara el Viento", L"??? ??? ??? ??", L"明日自有明日?", L"???? ???? ??????", L"Завтра Подует Ветер", L"Morgen Wird der Wind Wehen", L"Amanha o Vento Soprara", L"Morgen Zal de Wind Waaien", L"Jutro Zawieje Wiatr", L"Yar?n Ruzgar Esecek");
				break;
			case 7102:
				a = LL14(L"クロスベルの午後", L"Afternoon in Crossbell", L"Apres-midi a Crossbell", L"Pomeriggio a Crossbell", L"Tarde en Crossbell", L"????? ??", L"克洛斯??的午后", L"??? ????? ?? ??????", L"Послеполуденное время в Кроссбелле", L"Nachmittag in Crossbell", L"Tarde em Crossbell", L"Middag in Crossbell", L"Popo?udnie w Crossbell", L"Crossbell'de O?leden Sonra");
				break;
			case 7103:
				a = L"During Mission Accomplishment";
				break;
			case 7104:
				a = LL14(L"創立記念祭", L"Founding Festival", L"Fete de Fondation", L"Festival della Fondazione", L"Festival Fundacional", L"?? ???", L"?立?念祭", L"?????? ???????", L"Праздник Основания", L"Grundungsfest", L"Festival da Fundacao", L"Stichtingsfeest", L"?wi?to Za?o?enia", L"Kurulu? Festivali");
				break;
			case 7105:
				a = LL14(L"降水確率10%", L"10% Chance of Rain", L"10% de chances de pluie", L"10% di probabilita di pioggia", L"10% de probabilidad de lluvia", L"???? 10%", L"降水概率10%", L"10% ???? ?????", L"10% Вероятность Дождя", L"10% Regenwahrscheinlichkeit", L"10% de chance de chuva", L"10% kans op regen", L"10% szans na deszcz", L"%10 ya?mur ihtimali");
				break;
			case 7106:
				a = LL14(L"風船と紙吹雪", L"Balloons and Confetti", L"Ballons et Confettis", L"Palloncini e Coriandoli", L"Globos y Confeti", L"??? ?? ??", L"气球与?屑", L"??????? ??????? ?????", L"Воздушные Шары и Конфетти", L"Luftballons und Konfetti", L"Baloes e Confete", L"Ballonnen en Confetti", L"Balony i Konfetti", L"Balonlar ve Konfeti");
				break;
			case 7110:
				a = LL14(L"特務支援課", L"Special Support Section", L"Section Soutien Special", L"Sezione Supporto Speciale", L"Seccion de Apoyo Especial", L"?????", L"特?支援科", L"??? ????? ?????", L"Специальная Поддержка", L"Sondereinsatztruppe", L"Secao de Suporte Especial", L"Speciale Ondersteuningssectie", L"Specjalna Sekcja Wsparcia", L"Ozel Destek Bolumu");
				break;
			case 7111:
				a = LL14(L"C.S.P.D. -クロスベル警察", L"C.S.P.D. -Crossbell Police", L"C.S.P.D. -Police de Crossbell", L"C.S.P.D. -Polizia Crossbell", L"C.S.P.D. -Policia Crossbell", L"C.S.P.D. -???? ??", L"C.S.P.D. -克洛斯??警察", L"C.S.P.D. -???? ??????", L"C.S.P.D. -Полиция Кроссбелла", L"C.S.P.D. -Crossbell Polizei", L"C.S.P.D. -Policia Crossbell", L"C.S.P.D. -Crossbell Politie", L"C.S.P.D. -Policja Crossbell", L"C.S.P.D. -Crossbell Polisi");
				break;
			case 7113:
				a = L"Arc-en-ciel";
				break;
			case 7114:
				a = LL14(L"黒月貿易公司", L"Heiyue Trading Company", L"Compagnie Heiyue", L"Heiyue Trading Company", L"Heiyue Trading Company", L"???? ????", L"黑月?易公司", L"???? ???? ????????", L"Торговая Компания Хэйюэ", L"Heiyue Handelsgesellschaft", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company");
				break;
			case 7116:
				a = L"IGNIS";
				break;
			case 7117:
				a = L"TRINITY";
				break;
			case 7120:
				a = LL14(L"アルモリカ村", L"Armorica Village", L"Village d'Armorica", L"Villaggio Armorica", L"Aldea Armorica", L"????? ??", L"阿莫利?村", L"???? ????????", L"Деревня Арморика", L"Armorica-Dorf", L"Vila Armorica", L"Armorica-dorp", L"Wie? Armorica", L"Armorica Koyu");
				break;
			case 7121:
				a = LL14(L"鉱山町マインツ", L"Mines Town Mainz", L"Ville miniere Mainz", L"Citta mineraria Mainz", L"Ciudad minera Mainz", L"???? ???", L"?山??因茨", L"???? ??????? ??????", L"Город Шахт Майнц", L"Bergarbeiterstadt Mainz", L"Cidade das Minas Mainz", L"Mijnstad Mainz", L"Miasto Kopalni Mainz", L"Mainz Maden Kasabas?");
				break;
			case 7122:
				a = L"Killing Bear";
				break;
			case 7123:
				a = LL14(L"聖ウルスラ医科大学", L"St. Ursula Medical College", L"Faculte St-Ursule", L"Collegio medico St. Ursula", L"Universidad Medica St. Ursula", L"? ???? ????", L"????拉医科大学", L"???? ???? ??????? ??????", L"Медколледж св. Урсулы", L"St. Ursula Medizinhochschule", L"Faculdade St. Ursula", L"St. Ursula Medische Hogeschool", L"Szpital ?w. Urszuli", L"Aziz Ursula T?p Koleji");
				break;
			case 7124:
				a = LL14(L"クロスベル大聖堂", L"Crossbell Cathedral", L"Cathedrale de Crossbell", L"Cattedrale di Crossbell", L"Catedral de Crossbell", L"???? ???", L"克洛斯??大教堂", L"????????? ??????", L"Собор Кроссбелла", L"Crossbell-Kathedrale", L"Catedral de Crossbell", L"Crossbell Kathedraal", L"Katedra Crossbell", L"Crossbell Katedrali");
				break;
			case 7125:
				a = LL14(L"黒の競売会", L"Black Auction", L"Vente aux encheres noire", L"Asta nera", L"Subasta negra", L"?? ???", L"黑色拍?会", L"?????? ??????", L"Чёрный Аукцион", L"Schwarze Auktion", L"Leilao negro", L"Zwarte Veiling", L"Czarna Aukcja", L"Kara Muzayede");
				break;
			case 7126:
				a = LL14(L"大国にはさまれて", L"Caught Between Nations", L"Pris entre les Nations", L"Intrappolati tra le Nazioni", L"Atrapados entre Naciones", L"??? ????", L"?在大国之?", L"??????? ??? ?????", L"Зажатые Между Державами", L"Zwischen den Nationen gefangen", L"Preso entre Nacoes", L"Gevangen tussen Naties", L"Uwi?ziony mi?dzy Mocarstwami", L"Uluslar Aras?nda S?k??m??");
				break;
			case 7150:
				a = LL14(L"新たなる日常", L"New Daily Life", L"Nouvelle Vie Quotidienne", L"Nuova Vita Quotidiana", L"Nueva Vida Cotidiana", L"??? ??", L"?新的日常", L"???? ????? ?????", L"Новые Будни", L"Neuer Alltag", L"Nova Vida Cotidiana", L"Nieuw Dagelijks Leven", L"Nowe Codzienne ?ycie", L"Yeni Gunluk Ya?am");
				break;
			case 7151:
				a = LL14(L"動き始めた事態", L"Events in Motion", L"Evenements en Mouvement", L"Eventi in Movimento", L"Eventos en Movimiento", L"???? ??? ??", L"?始??的局?", L"????? ?? ????", L"События Приходят в Движение", L"Ereignisse in Bewegung", L"Eventos em Movimento", L"Gebeurtenissen in Beweging", L"Zdarzenia w Ruchu", L"Harekete Gecen Olaylar");
				break;
			case 7160:
				a = LL14(L"ミシュラムワンダーランド", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"??? ????", L"米修拉姆??", L"??????? ?????????", L"Мишрам Уандерленд", L"Mishyram Wunderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Harikalar Diyar?");
				break;
			case 7161:
				a = LL14(L"束の間の休息", L"Brief Respite", L"Bref Repit", L"Breve Respiro", L"Breve Respiro", L"??? ??", L"短?的休息", L"??????? ?????", L"Краткая Передышка", L"Kurze Verschnaufpause", L"Breve Descanso", L"Kort Respijt", L"Krotki Odpoczynek", L"K?sa Mola");
				break;
			case 7162:
				a = LL14(L"ささやかな晩餐", L"Simple Dinner", L"Diner Simple", L"Cena Semplice", L"Cena Sencilla", L"??? ??", L"??的?餐", L"???? ????", L"Скромный Ужин", L"Einfaches Abendessen", L"Jantar Simples", L"Eenvoudig Diner", L"Prosty Obiad", L"Sade Ak?am Yeme?i");
				break;
			case 7200:
				a = LL14(L"水と草木と青い空", L"Water, Trees and Blue Sky", L"Eau, Arbres et Ciel Bleu", L"Acqua, Alberi e Cielo Azzurro", L"Agua, Arboles y Cielo Azul", L"?? ??? ?? ??", L"水与草木和?天", L"????? ???????? ??????? ???????", L"Вода, Деревья и Голубое Небо", L"Wasser, Baume und blauer Himmel", L"Agua, Arvores e Ceu Azul", L"Water, Bomen en Blauwe Lucht", L"Woda, Drzewa i B??kitne Niebo", L"Su, A?aclar ve Mavi Gokyuzu");
				break;
			case 7201:
				a = LL14(L"片手にはレモネード", L"Lemonade in One Hand", L"Limonade dans une Main", L"Limonata in una Mano", L"Limonada en una Mano", L"? ??? ?????", L"一手拿着?檬水", L"???????? ?? ?? ?????", L"Лимонад в Одной Руке", L"Limonade in einer Hand", L"Limonada em uma Mao", L"Limonade in een Hand", L"Lemoniada w Jednej R?ce", L"Bir Elde Limonata");
				break;
			case 7202:
				a = LL14(L"木霊の道", L"Path of Echoes", L"Chemin des Echos", L"Sentiero degli Echi", L"Senda de los Ecos", L"???? ?", L"回声之道", L"??? ???????", L"Тропа Эхо", L"Pfad der Echos", L"Caminho dos Ecos", L"Pad van Echo's", L"?cie?ka Ech", L"Yank?lar Yolu");
				break;
			case 7203:
				a = LL14(L"古の鼓動", L"Ancient Pulse", L"Pulsation Ancienne", L"Pulsazione Antica", L"Pulso Antiguo", L"??? ??", L"古老的脉?", L"????? ??????", L"Древний Пульс", L"Alter Puls", L"Pulso Antigo", L"Oude Puls", L"Staro?ytne T?tno", L"Kadim Nab?z");
				break;
			case 7204:
				a = L"On The Green Road";
				break;
			case 7205:
				a = LL14(L"鉄橋を越えて", L"Crossing the Iron Bridge", L"Traverser le Pont de Fer", L"Attraversare il Ponte di Ferro", L"Cruzando el Puente de Hierro", L"??? ???", L"越???", L"???? ????? ???????", L"Пересекая Железный Мост", L"Die Eisenbrucke uberqueren", L"Cruzando a Ponte de Ferro", L"De IJzeren Brug Oversteken", L"Przekraczaj?c ?elazny Most", L"Demir Kopruyu Gecerken");
				break;
			case 7250:
				a = LL14(L"木洩れ日の中の静寂", L"Tranquility in the Dappled Light", L"Tranquillite dans la Lumiere Tachetee", L"Tranquillita nella Luce Screziata", L"Tranquilidad en la Luz Moteada", L"??? ??? ??? ? ?? ??", L"斑?光影中的静?", L"?????? ?? ????? ???????", L"Тишина в Пятнистом Свете", L"Stille im gefilterten Licht", L"Tranquilidade na Luz Filtrada", L"Rust in het Gefiltreerde Licht", L"Spokoj w Migotliwym ?wietle", L"I??k Suzulurken Huzur");
				break;
			case 7251:
				a = LL14(L"偽りの楽土を越えて", L"Beyond the False Paradise", L"Au-Dela du Faux Paradis", L"Oltre il Falso Paradiso", L"Mas Alla del Falso Paraiso", L"?? ??? ???", L"超越虚假的?土", L"?? ???? ????? ???????", L"За пределами Ложного Рая", L"Jenseits des falschen Paradieses", L"Alem do Falso Paraiso", L"Voorbij het Valse Paradijs", L"Poza Fa?szywym Rajem", L"Sahte Cennetin Otesinde");
				break;
			case 7300:
				a = LL14(L"ジオフロント", L"Geofront", L"Geofront", L"Geofront", L"Geofront", L"?????", L"地底都市", L"????????", L"Геофронт", L"Geofront", L"Geofront", L"Geofront", L"Geofront", L"Geofront");
				break;
			case 7301:
				a = LL14(L"七耀の煌き", L"Septium Radiance", L"Eclat du Septium", L"Splendore del Septium", L"Resplandor del Septium", L"??? ??", L"七曜之?耀", L"???? ????????", L"Сияние Септиума", L"Septium-Glanz", L"Resplendor do Septium", L"Septium Glinstering", L"Blask Septium", L"Septium I??lt?s?");
				break;
			case 7302:
				a = LL14(L"ルバーチェ商会", L"Revache Trading Company", L"Compagnie Revache", L"Revache Trading Company", L"Revache Trading Company", L"??? ??", L"?巴切商会", L"???? ????? ????????", L"Торговая Компания Реваш", L"Revache Handelsgesellschaft", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Ticaret ?irketi");
				break;
			case 7303:
				a = LL14(L"鳴るはずのない鐘", L"The Bell That Shouldn't Ring", L"La Cloche Qui Ne Devrait Pas Sonner", L"La Campana Che Non Dovrebbe Suonare", L"La Campana Que No Deberia Sonar", L"???? ? ? ?", L"不??起的?声", L"????? ???? ?? ????? ?? ???", L"Колокол, Который Не Должен Звонить", L"Die Glocke, die nicht lauten sollte", L"O Sino Que Nao Deveria Tocar", L"De Klok Die Niet Zou Moeten Luiden", L"Dzwon, Ktory Nie Powinien Bi?", L"Calmamas? Gereken Can");
				break;
			case 7304:
				a = LL14(L"忘れられし幻夢の狭間", L"Forgotten Phantasmal Gap", L"Interstice Fantomatique Oublie", L"Varco Fantasmatico Dimenticato", L"Brecha Fantasmal Olvidada", L"??? ??? ??", L"被?忘的幻梦之?", L"???? ????? ??????", L"Забытый Призрачный Разрыв", L"Vergessene Phantomale Lucke", L"Lacuna Fantasmal Esquecida", L"Vergeten Spookachtige Kloof", L"Zapomniana Fantomalna Szczelina", L"Unutulmu? Hayali Bo?luk");
				break;
			case 7305:
				a = L"A Light Illuminating The Depths";
				break;
			case 7350:
				a = LL14(L"Dの残影", L"D's Shadow", L"L'Ombre de D", L"L'Ombra di D", L"La Sombra de D", L"D? ??", L"D的残影", L"?? D", L"Тень D", L"Ds Schatten", L"A Sombra de D", L"D's Schaduw", L"Cie? D", L"D'nin Golgesi");
				break;
			case 7351:
				a = LL14(L"異変の兆し", L"Omen of Change", L"Presage de Changement", L"Presagio di Cambiamento", L"Presagio de Cambio", L"??? ??", L"??的征兆", L"???? ???????", L"Предзнаменование Перемен", L"Vorbote des Wandels", L"Pressagio de Mudanca", L"Voorteken van Verandering", L"Zwiastun Zmiany", L"De?i?imin ??areti");
				break;
			case 7352:
				a = L"Mystic Core";
				break;
			case 7353:
				a = LL14(L"最果ての樹", L"Tree at World's End", L"L'Arbre au Bout du Monde", L"L'Albero alla Fine del Mondo", L"El Arbol al Fin del Mundo", L"?? ?? ??", L"天涯之?", L"?????? ??? ????? ??????", L"Дерево на Краю Света", L"Baum am Ende der Welt", L"A Arvore no Fim do Mundo", L"De Boom aan het Einde van de Wereld", L"Drzewo na Ko?cu ?wiata", L"Dunyan?n Sonundaki A?ac");
				break;
			case 7354:
				a = LL14(L"暴魔の呼び声", L"Call of the Beast", L"L'Appel de la Bete", L"Il Richiamo della Bestia", L"El Llamado de la Bestia", L"??? ??", L"暴魔的呼?", L"???? ?????", L"Зов Зверя", L"Ruf des Ungeheuers", L"O Chamado da Besta", L"De Roep van het Beest", L"Wo?anie Bestii", L"Canavar?n Ca?r?s?");
				break;
			case 7356:
				a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
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
				a = LL14(L"これが俺たちの力だ!", L"This Is Our Power!", L"C'est Notre Pouvoir!", L"Questo E il Nostro Potere!", L"!Este Es Nuestro Poder!", L"??? ???? ???!", L"?就是我?的力量!", L"??? ?? ?????!", L"Это Наша Сила!", L"Das Ist Unsere Kraft!", L"Este E o Nosso Poder!", L"Dit Is Onze Kracht!", L"To Jest Nasza Si?a!", L"Bu Bizim Gucumuz!");
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
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
				break;
			case 7500:
				a = LL14(L"金の太陽、銀の月　-陽の熱情", L"Golden Sun, Silver Moon -Solar Passion-", L"Soleil d'Or, Lune d'Argent -Passion Solaire-", L"Sole d'Oro, Luna d'Argento -Passione Solare-", L"Sol Dorado, Luna de Plata -Pasion Solar-", L"??? ??, ??? ? -??? ??-", L"黄金之?，白?之月 -太?的?情-", L"????? ???????? ????? ????? -??? ????-", L"Золотое Солнце, Серебряная Луна -Солнечная Страсть-", L"Goldene Sonne, Silberner Mond -Sonnenleidenschaft-", L"Sol Dourado, Lua de Prata -Paixao Solar-", L"Gouden Zon, Zilveren Maan -Zonnige Passie-", L"Z?ote S?o?ce, Srebrny Ksi??yc -S?oneczna Nami?tno??-", L"Alt?n Gune?, Gumu? Ay -Gune? Tutkusu-");
				break;
			case 7501:
				a = LL14(L"金の太陽、銀の月　-月の慕情", L"Golden Sun, Silver Moon -Lunar Affection-", L"Soleil d'Or, Lune d'Argent -Affection Lunaire-", L"Sole d'Oro, Luna d'Argento -Affetto Lunare-", L"Sol Dorado, Luna de Plata -Afecto Lunar-", L"??? ??, ??? ? -?? ??-", L"黄金之?，白?之月 -月亮的思慕-", L"????? ???????? ????? ????? -???? ?????-", L"Золотое Солнце, Серебряная Луна -Лунная Нежность-", L"Goldene Sonne, Silberner Mond -Mondneigung-", L"Sol Dourado, Lua de Prata -Afeicao Lunar-", L"Gouden Zon, Zilveren Maan -Maanachtige Genegenheid-", L"Z?ote S?o?ce, Srebrny Ksi??yc -Ksi??ycowe Uczucie-", L"Alt?n Gune?, Gumu? Ay -Ay Sevgisi-");
				break;
			case 7502:
				a = LL14(L"金の太陽、銀の月　-童心", L"Golden Sun, Silver Moon -Innocence-", L"Soleil d'Or, Lune d'Argent -Innocence-", L"Sole d'Oro, Luna d'Argento -Innocenza-", L"Sol Dorado, Luna de Plata -Inocencia-", L"??? ??, ??? ? -??-", L"黄金之?，白?之月 -童心-", L"????? ???????? ????? ????? -???????-", L"Золотое Солнце, Серебряная Луна -Невинность-", L"Goldene Sonne, Silberner Mond -Unschuld-", L"Sol Dourado, Lua de Prata -Inocencia-", L"Gouden Zon, Zilveren Maan -Onschuld-", L"Z?ote S?o?ce, Srebrny Ksi??yc -Niewinno??-", L"Alt?n Gune?, Gumu? Ay -Masumiyet-");
				break;
			case 7503:
				a = LL14(L"金の太陽、銀の月　-運命の刻", L"Golden Sun, Silver Moon -Hour of Fate-", L"Soleil d'Or, Lune d'Argent -L'Heure du Destin-", L"Sole d'Oro, Luna d'Argento -L'Ora del Destino-", L"Sol Dorado, Luna de Plata -La Hora del Destino-", L"??? ??, ??? ? -??? ??-", L"黄金之?，白?之月 -命?的?刻-", L"????? ???????? ????? ????? -???? ?????-", L"Золотое Солнце, Серебряная Луна -Час Судьбы-", L"Goldene Sonne, Silberner Mond -Stunde des Schicksals-", L"Sol Dourado, Lua de Prata -A Hora do Destino-", L"Gouden Zon, Zilveren Maan -Het Uur van het Lot-", L"Z?ote S?o?ce, Srebrny Ksi??yc -Godzina Przeznaczenia-", L"Alt?n Gune?, Gumu? Ay -Kaderin Saati-");
				break;
			case 7504:
				a = LL14(L"金の太陽、銀の月　-譲れぬ想い", L"Golden Sun, Silver Moon -Unyielding Feelings-", L"Soleil d'Or, Lune d'Argent -Sentiments Inebranlables-", L"Sole d'Oro, Luna d'Argento -Sentimenti Irremovibili-", L"Sol Dorado, Luna de Plata -Sentimientos Inquebrantables-", L"??? ??, ??? ? -??? ? ?? ??-", L"黄金之?，白?之月 -不可退?的心意-", L"????? ???????? ????? ????? -????? ?? ????-", L"Золотое Солнце, Серебряная Луна -Непреклонные Чувства-", L"Goldene Sonne, Silberner Mond -Unnachgiebige Gefuhle-", L"Sol Dourado, Lua de Prata -Sentimentos Inabalaveis-", L"Gouden Zon, Zilveren Maan -Onwrikbare Gevoelens-", L"Z?ote S?o?ce, Srebrny Ksi??yc -Nieust?pliwe Uczucia-", L"Alt?n Gune?, Gumu? Ay -Vazgecilmez Duygular-");
				break;
			case 7505:
				a = LL14(L"金の太陽、銀の月　-幾千の夜を越えて", L"Golden Sun, Silver Moon -Beyond Countless Nights-", L"Soleil d'Or, Lune d'Argent -Au-Dela de Nuits Sans Nombre-", L"Sole d'Oro, Luna d'Argento -Oltre Innumerevoli Notti-", L"Sol Dorado, Luna de Plata -Mas Alla de Incontables Noches-", L"??? ??, ??? ? -??? ?? ???-", L"黄金之?，白?之月 -跨越无数夜?-", L"????? ???????? ????? ????? -??? ????? ?? ?????-", L"Золотое Солнце, Серебряная Луна -Сквозь Бесчисленные Ночи-", L"Goldene Sonne, Silberner Mond -Jenseits Unzahliger Nachte-", L"Sol Dourado, Lua de Prata -Alem de Incontaveis Noites-", L"Gouden Zon, Zilveren Maan -Voorbij Ontelbare Nachten-", L"Z?ote S?o?ce, Srebrny Ksi??yc -Poza Niezliczonymi Nocami-", L"Alt?n Gune?, Gumu? Ay -Say?s?z Gecelerin Otesinde-");
				break;
			case 7506:
				a = LL14(L"金の太陽、銀の月　-夜明け〜大団円", L"Golden Sun, Silver Moon -Dawn to Grand Finale-", L"Soleil d'Or, Lune d'Argent -Aube vers le Grand Finale-", L"Sole d'Oro, Luna d'Argento -Alba verso il Gran Finale-", L"Sol Dorado, Luna de Plata -Amanecer hasta el Gran Final-", L"??? ??, ??? ? -??~???-", L"黄金之?，白?之月 -黎明~大??-", L"????? ???????? ????? ????? -????? ??? ?????? ??????-", L"Золотое Солнце, Серебряная Луна -Рассвет до Грандиозного Финала-", L"Goldene Sonne, Silberner Mond -Morgengrauen bis zum grosen Finale-", L"Sol Dourado, Lua de Prata -Amanhecer ate o Grande Final-", L"Gouden Zon, Zilveren Maan -Dageraad tot het Grote Finale-", L"Z?ote S?o?ce, Srebrny Ksi??yc -?wit do Wielkiego Fina?u-", L"Alt?n Gune?, Gumu? Ay -?afaktan Buyuk Finale-");
				break;
			case 7507:
				a = L"Intense Chase";
				break;
			case 7509:
				a = LL14(L"守りぬく意志", L"Unyielding Will", L"Volonte Inebranlable", L"Volonta Irremovibile", L"Voluntad Inquebrantable", L"????? ??", L"?守的意志", L"????? ?? ????", L"Непреклонная Воля", L"Unnachgiebiger Wille", L"Vontade Inabalavel", L"Onwrikbare Wil", L"Nieust?pliwa Wola", L"Vazgecmeyen ?rade");
				break;
			case 7510:
				a = LL14(L"叡智への誘い", L"Invitation to Wisdom", L"Invitation a la Sagesse", L"Invito alla Saggezza", L"Invitacion a la Sabiduria", L"???? ??", L"通往智慧的邀?", L"???? ??? ??????", L"Приглашение к Мудрости", L"Einladung zur Weisheit", L"Convite a Sabedoria", L"Uitnodiging tot Wijsheid", L"Zaproszenie do M?dro?ci", L"Bilgeli?e Davet");
				break;
			case 7511:
				a = LL14(L"危地", L"Perilous Ground", L"Terrain Perilleux", L"Terreno Pericoloso", L"Terreno Peligroso", L"?? ??", L"危?之地", L"??? ????", L"Опасная Территория", L"Gefahrliches Terrain", L"Terreno Perigoso", L"Gevaarlijk Terrein", L"Niebezpieczny Teren", L"Tehlikeli Bolge");
				break;
			case 7512:
				a = LL14(L"揺るぎない強さ", L"Unshakable Strength", L"Force Inebranlable", L"Forza Incrollabile", L"Fuerza Inquebrantable", L"???? ?? ??", L"不可??的力量", L"??? ?? ??????", L"Непоколебимая Сила", L"Unerschutterliche Starke", L"Forca Inabalavel", L"Onwankelbare Kracht", L"Niezachwiana Si?a", L"Sars?lmaz Guc");
				break;
			case 7513:
				a = LL14(L"夜景に霞む星空", L"Starry Sky in the Night", L"Ciel Etoile dans la Nuit", L"Cielo Stellato nella Notte", L"Cielo Estrellado en la Noche", L"??? ????? ???", L"夜色中朦?的星空", L"???? ????? ??????? ?? ?????", L"Звёздное Небо Ночью", L"Sternenhimmel in der Nacht", L"Ceu Estrelado na Noite", L"Sterrenhemel in de Nacht", L"Rozgwie?d?one Niebo w Nocy", L"Gece Y?ld?zl? Gokyuzu");
				break;
			case 7514:
				a = LL14(L"いつかきっと", L"Someday", L"Un Jour, Surement", L"Un Giorno, Di Certo", L"Algun Dia, Seguro", L"??? ???", L"?有一天", L"????? ?? ????????", L"Когда-нибудь Обязательно", L"Irgendwann Bestimmt", L"Um Dia, Com Certeza", L"Ooit Zeker", L"Kiedy? Na Pewno", L"Bir Gun Mutlaka");
				break;
			case 7515:
				a = LL14(L"柔らかな心", L"Tender Heart", L"C?ur Tendre", L"Cuore Tenero", L"Corazon Tierno", L"???? ??", L"温柔的心", L"??? ????", L"Нежное Сердце", L"Zartes Herz", L"Coracao Terno", L"Teder Hart", L"Czu?e Serce", L"Nazik Kalp");
				break;
			case 7516:
				a = LL14(L"点と線", L"Dots and Lines", L"Points et Lignes", L"Punti e Linee", L"Puntos y Lineas", L"?? ?", L"点与?", L"???? ?????", L"Точки и Линии", L"Punkte und Linien", L"Pontos e Linhas", L"Punten en Lijnen", L"Punkty i Linie", L"Noktalar ve Cizgiler");
				break;
			case 7517:
				a = LL14(L"一触即発", L"Imminent Crisis", L"Crise Imminente", L"Crisi Imminente", L"Crisis Inminente", L"????", L"一触即?", L"???? ?????", L"Надвигающийся Кризис", L"Unmittelbar Bevorstehende Krise", L"Crise Iminente", L"Dreigende Crisis", L"Bezpo?redni Kryzys", L"Yakla?an Kriz");
				break;
			case 7518:
				a = L"Foolish Gig";
				break;
			case 7519:
				a = LL14(L"リベールからの風", L"Wind from Liberl", L"Vent de Liberl", L"Vento da Liberl", L"Viento de Liberl", L"????? ? ??", L"来自利??的?", L"??? ?? ??????", L"Ветер из Либерла", L"Wind aus Liberl", L"Vento de Liberl", L"Wind uit Liberl", L"Wiatr z Liberl", L"Liberl'den Ruzgar");
				break;
			case 7520:
				a = LL14(L"とどいた想い", L"Feelings Delivered", L"Sentiments Transmis", L"Sentimenti Consegnati", L"Sentimientos Entregados", L"??? ??", L"??到的心意", L"????? ????", L"Переданные Чувства", L"Ubermittelte Gefuhle", L"Sentimentos Entregues", L"Bezorgde Gevoelens", L"Dostarczone Uczucia", L"?letilen Duygular");
				break;
			case 7521:
				a = L"Underground Kids";
				break;
			case 7522:
				a = L"Terminal Room";
				break;
			case 7523:
				a = LL14(L"響きあう心", L"Resonating Hearts", L"C?urs en Resonance", L"Cuori in Risonanza", L"Corazones en Resonancia", L"?? ??? ??", L"共?的心", L"???? ????? ?????", L"Резонирующие Сердца", L"Resonierender Herzen", L"Coracoes em Ressonancia", L"Resonerende Harten", L"Rezonuj?ce Serca", L"Rezonans Eden Kalpler");
				break;
			case 7524:
				a = L"Limit Break";
				break;
			case 7525:
				a = LL14(L"パラダイスミ☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆");
				break;
			case 7526:
				a = L"Gnosis";
				break;
			case 7527:
				a = L"Get Over The Barrier! -Roaring Version-";
				break;
			case 7528:
				a = LL14(L"それぞれの明日", L"Our Tomorrows", L"Nos Lendemains", L"I Nostri Domani", L"Nuestros Mananas", L"??? ??", L"各自的明天", L"??? ??? ???", L"Наши Завтрашние Дни", L"Unsere Morgigen Tage", L"Nossos Amanhas", L"Onze Morgens", L"Nasze Jutrzejsze Dni", L"Hepimizin Yar?nlar?");
				break;
			case 7529:
				a = LL14(L"効果音楽1", L"Sound Effect Music 1", L"Musique d'Effet Sonore 1", L"Musica Effetto Sonoro 1", L"Musica de Efecto de Sonido 1", L"?? ?? 1", L"音效音?1", L"?????? ????? ???? 1", L"Звуковая Музыка 1", L"Soundeffekt-Musik 1", L"Musica de Efeito Sonoro 1", L"Geluidseffect Muziek 1", L"Muzyka Efektow D?wi?kowych 1", L"Ses Efekti Muzi?i 1");
				break;
			case 7530:
				a = LL14(L"効果音楽2", L"Sound Effect Music 2", L"Musique d'Effet Sonore 2", L"Musica Effetto Sonoro 2", L"Musica de Efecto de Sonido 2", L"?? ?? 2", L"音效音?2", L"?????? ????? ???? 2", L"Звуковая Музыка 2", L"Soundeffekt-Musik 2", L"Musica de Efeito Sonoro 2", L"Geluidseffect Muziek 2", L"Muzyka Efektow D?wi?kowych 2", L"Ses Efekti Muzi?i 2");
				break;
			case 7531:
				a = LL14(L"効果音楽3", L"Sound Effect Music 3", L"Musique d'Effet Sonore 3", L"Musica Effetto Sonoro 3", L"Musica de Efecto de Sonido 3", L"?? ?? 3", L"音效音?3", L"?????? ????? ???? 3", L"Звуковая Музыка 3", L"Soundeffekt-Musik 3", L"Musica de Efeito Sonoro 3", L"Geluidseffect Muziek 3", L"Muzyka Efektow D?wi?kowych 3", L"Ses Efekti Muzi?i 3");
				break;
			case 7532:
				a = LL14(L"効果音楽4", L"Sound Effect Music 4", L"Musique d'Effet Sonore 4", L"Musica Effetto Sonoro 4", L"Musica de Efecto de Sonido 4", L"?? ?? 4", L"音效音?4", L"?????? ????? ???? 4", L"Звуковая Музыка 4", L"Soundeffekt-Musik 4", L"Musica de Efeito Sonoro 4", L"Geluidseffect Muziek 4", L"Muzyka Efektow D?wi?kowych 4", L"Ses Efekti Muzi?i 4");
				break;
			case 7533:
				a = LL14(L"踏み出す勇気", L"Courage to Step Forward", L"Courage d'Avancer", L"Coraggio di Andare Avanti", L"Valentia para Avanzar", L"??? ??", L"踏出的勇气", L"??????? ????? ?????", L"Смелость Шагнуть Вперёд", L"Mut Voranzugehen", L"Coragem de Dar um Passo", L"Moed om Vooruit te Stappen", L"Odwaga by Ruszy? Naprzod", L"?lerleme Cesareti");
				break;
			case 7534:
				a = LL14(L"その背中を見つめて", L"Watching Your Back", L"Regarder ton Dos", L"Guardare le Tue Spalle", L"Mirando tu Espalda", L"? ?? ????", L"凝?着那背影", L"???? ??? ????", L"Глядя в Твою Спину", L"Deinen Rucken Beobachten", L"Olhando suas Costas", L"Naar Je Rug Kijken", L"Patrz?c na Twoje Plecy", L"S?rt?na Bakarak");
				break;
			case 7540:
				a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
				break;
			case 7541:
				a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
				break;
			case 7542:
				a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
				break;
			case 7543:
				a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
				break;
			case 7544:
				a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
				break;
			case 7550:
				a = LL14(L"オルキスタワー", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower", L"Orchis Tower");
				break;
			case 7551:
				a = L"Catastrophe";
				break;
			case 7552:
				a = LL14(L"碧き雫", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator");
				break;
			case 7553:
				a = LL14(L"神機降臨", L"Divine Mechanoid Descent", L"Descente du Mecanisme Divin", L"Discesa del Meccanismo Divino", L"Descenso del Mecanismo Divino", L"?? ??", L"神机降?", L"???? ????? ???????", L"Нисхождение Божественного Механизма", L"Abstieg des Gottlichen Mechanoids", L"Descida do Mecanismo Divino", L"Afdaling van het Goddelijke Mechanisme", L"Zst?pienie Boskiego Mechanizmu", L"?lahi Mekanizman?n ?ni?i");
				break;
			case 7554:
				a = LL14(L"ふるわれる奇蹟", L"Shaking Miracle", L"Miracle Tremblant", L"Miracolo Tremante", L"Milagro Tembloroso", L"?? ??? ??", L"震撼的奇迹", L"????? ??????", L"Дрожащее Чудо", L"Erschutterndes Wunder", L"Milagre Tremendo", L"Trillend Wonder", L"Dr??cy Cud", L"Sars?lan Mucize");
				break;
			case 7555:
				a = LL14(L"予定外の奇蹟", L"Unexpected Miracle", L"Miracle Inattendu", L"Miracolo Inaspettato", L"Milagro Inesperado", L"?? ?? ??", L"意料之外的奇迹", L"????? ??? ??????", L"Неожиданное Чудо", L"Unerwartetes Wunder", L"Milagre Inesperado", L"Onverwacht Wonder", L"Nieoczekiwany Cud", L"Beklenmedik Mucize");
				break;
			case 7556:
				a = LL14(L"鋼鉄の咆哮 -脅威-", L"Roar of Steel -Threat-", L"Rugissement d'Acier -Menace-", L"Ruggito d'Acciaio -Minaccia-", L"Rugido de Acero -Amenaza-", L"??? ?? -??-", L"??的咆哮 -威?-", L"???? ??????? -?????-", L"Рёв Стали -Угроза-", L"Stahlgebrull -Bedrohung-", L"Rugido de Aco -Ameaca-", L"Staalgebulder -Bedreiging-", L"Ryk Stali -Zagro?enie-", L"Celi?in Kukremesi -Tehdit-");
				break;
			case 7560:
				a = LL14(L"雨の日の真実", L"Truth on a Rainy Day", L"Verite un Jour de Pluie", L"Verita in un Giorno di Pioggia", L"Verdad en un Dia Lluvioso", L"? ?? ?? ??", L"雨天的真相", L"??????? ?? ??? ????", L"Правда в Дождливый День", L"Wahrheit an einem Regentag", L"Verdade em um Dia Chuvoso", L"Waarheid op een Regenachtige Dag", L"Prawda w Deszczowy Dzie?", L"Ya?murlu Bir Gunde Gercek");
				break;
			case 7561:
				a = LL14(L"不穏", L"Troubled", L"Trouble", L"Turbato", L"Perturbado", L"??", L"不安?", L"???", L"Тревожный", L"Unruhig", L"Perturbado", L"Onrustig", L"Niepokoj", L"Huzursuz");
				break;
			case 7562:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
				break;
			case 7563:
				a = LL14(L"犠牲の先の希望", L"Hope Beyond Sacrifice", L"Espoir au-Dela du Sacrifice", L"Speranza Oltre il Sacrificio", L"Esperanza Mas Alla del Sacrificio", L"?? ??? ??", L"?牲之后的希望", L"??? ?????? ???????", L"Надежда за Жертвой", L"Hoffnung Jenseits des Opfers", L"Esperanca Alem do Sacrificio", L"Hoop Voorbij Opoffering", L"Nadzieja Poza Po?wi?ceniem", L"Fedakarl???n Otesinde Umut");
				break;
			case 7564:
				a = L"Strange Feel";
				break;
			case 7565:
				a = L"Exhilarating Ride";
				break;
			case 7566:
				a = LL14(L"それぞれの正義", L"Each One's Justice", L"La Justice de Chacun", L"La Giustizia di Ognuno", L"La Justicia de Cada Uno", L"??? ??", L"各自的正?", L"????? ?? ???", L"Справедливость Каждого", L"Gerechtigkeit Jedes Einzelnen", L"A Justica de Cada Um", L"Ieders Gerechtigheid", L"Sprawiedliwo?? Ka?dego", L"Herkesin Adaleti");
				break;
			case 7567:
				a = LL14(L"乗り越えるべき壁", L"Wall to Overcome", L"Mur a Surmonter", L"Muro da Superare", L"Muro a Superar", L"??? ? ?", L"需要翻越的?", L"???? ??? ??????", L"Стена, Которую Нужно Преодолеть", L"Zu Uberwindende Wand", L"Muro a Superar", L"Muur om te Overwinnen", L"Mur do Pokonania", L"A??lmas? Gereken Duvar");
				break;
			case 7568:
				a = LL14(L"月下の想い", L"Feelings Under the Moon", L"Sentiments sous la Lune", L"Sentimenti sotto la Luna", L"Sentimientos bajo la Luna", L"?? ??? ??", L"月下的心意", L"????? ??? ?????", L"Чувства под Луной", L"Gefuhle unter dem Mond", L"Sentimentos sob a Lua", L"Gevoelens onder de Maan", L"Uczucia pod Ksi??ycem", L"Ay I????nda Duygular");
				break;
			case 7569:
				a = L"Miss You";
				break;
			case 7570:
				a = LL14(L"天の車", L"Chariot of Heaven", L"Char Celeste", L"Carro del Cielo", L"Carro Celestial", L"??? ??", L"天之??", L"???? ??????", L"Небесная Колесница", L"Himmelswagen", L"Carruagem do Ceu", L"Hemelse Strijdwagen", L"Niebieski Rydwan", L"Gok Arabas?");
				break;
			case 7571:
				a = LL14(L"突きつけられた現実", L"Reality Thrust Upon Us", L"Realite Imposee", L"Realta Imposta", L"Realidad Impuesta", L"????? ??", L"被?加的??", L"?????? ??????? ?????", L"Реальность, Навязанная Нам", L"Uns Aufgezwungene Realitat", L"Realidade Imposta", L"Opgelegde Realiteit", L"Narzucona Rzeczywisto??", L"Ustumuze Dayat?lan Gercek");
				break;
			case 7572:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
				break;
			case 7573:
				a = LL14(L"全てを識るもの", L"The Omniscient", L"L'Omniscient", L"L'Onnisciente", L"El Omnisciente", L"?? ?? ?? ?", L"无所不知者", L"?????? ??? ???", L"Всезнающий", L"Der Allwissende", L"O Onisciente", L"De Alwetende", L"Wszechwiedz?cy", L"Her ?eyi Bilen");
				break;
			case 7574:
				a = LL14(L"想い、辿り着く場所", L"Where Feelings Lead", L"La ou Menent les Sentiments", L"Dove Portano i Sentimenti", L"Adonde Llevan los Sentimientos", L"??? ?? ?", L"心意所至之?", L"??? ???? ???????", L"Куда Ведут Чувства", L"Wohin Gefuhle Fuhren", L"Para Onde os Sentimentos Levam", L"Waar Gevoelens Naartoe Leiden", L"Dok?d Prowadz? Uczucia", L"Duygular?n Goturdu?u Yer");
				break;
			case 7575:
				a = LL14(L"揺れ動く心", L"Wavering Heart", L"C?ur Vacillant", L"Cuore Vacillante", L"Corazon Vacilante", L"???? ??", L"?曳的心", L"??? ??????", L"Колеблющееся Сердце", L"Schwankendes Herz", L"Coracao Vacilante", L"Weifelend Hart", L"Chwiej?ce si? Serce", L"Karars?z Kalp");
				break;
			case 7576:
				a = LL14(L"星降る夜に", L"On a Starry Night", L"Par une Nuit Etoilee", L"In una Notte Stellata", L"En una Noche Estrellada", L"?? ??? ??", L"星降之夜", L"?? ???? ????? ???????", L"В Звёздную Ночь", L"In einer Sternennacht", L"Em uma Noite Estrelada", L"Op een Sterrenachtige Nacht", L"W Gwia?dzist? Noc", L"Y?ld?zl? Bir Gecede");
				break;
			case 7577:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
				break;
			case 7578:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
				break;
			case 7579:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音効", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
				break;
			case 7580:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
				break;
			case 7581:
				a = LL14(L"本当の絆", L"True Bonds", L"Vrais Liens", L"Veri Legami", L"Lazos Verdaderos", L"??? ??", L"真正的??", L"????? ??????", L"Настоящие Узы", L"Wahre Bande", L"Lacos Verdadeiros", L"Ware Banden", L"Prawdziwe Wi?zi", L"Gercek Ba?lar");
				break;
			case 7582:
				a = LL14(L"猛き獣たち", L"Fierce Beasts", L"Betes Feroces", L"Bestie Feroci", L"Bestias Feroces", L"??? ???", L"凶猛的野??", L"???? ????", L"Свирепые Звери", L"Wilde Bestien", L"Bestas Ferozes", L"Woeste Beesten", L"Dzikie Bestie", L"Vah?i Canavarlar");
				break;
			case 7583:
				a = LL14(L"西ゼムリア通商会議", L"West Zemuria Trade Conference", L"Conference Commerciale de Zemuria Occidentale", L"Conferenza Commerciale della Zemuria Occidentale", L"Conferencia Comercial de Zemuria Occidental", L"?? ???? ?? ??", L"西?姆利?通商会?", L"????? ????? ??? ???????", L"Западно-Земурийская Торговая Конференция", L"Westzemuranische Handelskonferenz", L"Conferencia Comercial da Zemuria Ocidental", L"West-Zemuria Handelsconferentie", L"Zachodnia Konferencja Handlowa Zemurii", L"Bat? Zemuria Ticaret Konferans?");
				break;
			case 7584:
				a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
				break;
			case 7585:
				a = LL14(L"千年の妄執", L"Obsession of Millennia", L"Obsession des Millenaires", L"Ossessione dei Millenni", L"Obsesion de los Milenios", L"??? ??", L"千年的妄?", L"??? ????????", L"Одержимость Тысячелетий", L"Obsession der Jahrtausende", L"Obsessao dos Milenios", L"Obsessie van Millennia", L"Obsesja Tysi?cleci", L"Bin Y?l?n Tak?nt?s?");
				break;
			case 7586:
				a = LL14(L"鋼鉄の咆哮 -死線-", L"Roar of Steel -Death Line-", L"Rugissement d'Acier -Ligne de Mort-", L"Ruggito d'Acciaio -Linea della Morte-", L"Rugido de Acero -Linea de Muerte-", L"??? ?? -??-", L"??的咆哮 -死?-", L"???? ??????? -?? ?????-", L"Рёв Стали -Линия Смерти-", L"Stahlgebrull -Todeslinie-", L"Rugido de Aco -Linha da Morte-", L"Staalgebulder -Doodslijn-", L"Ryk Stali -Linia ?mierci-", L"Celi?in Kukremesi -Olum Hatt?-");
				break;
			case 7587:
				a = LL14(L"ポムっと! -お花見団子の逆襲-", L"Pom! -Cherry Blossom Dango Counterattack-", L"Pom! -Contre-attaque des Dango de Fleurs de Cerisier-", L"Pom! -Contrattacco dei Dango di Fiori di Ciliegio-", L"Pom! -Contraataque de los Dango de Flores de Cerezo-", L"?! -??? ??? ??-", L"?! -?花?子的反攻-", L"!Pom -???? ????? ????? ????? ?????-", L"Пом! -Контратака Данго из Цветков Сакуры-", L"Pom! -Gegenangriff der Kirschbluten-Dango-", L"Pom! -Contra-ataque dos Dango de Flor de Cerejeira-", L"Pom! -Tegenaanval van Kersenbloesem Dango-", L"Pom! -Kontratak Dango z Kwiatami Wi?ni-", L"Pom! -Kiraz Cice?i Dango'nun Kar?? Sald?r?s?-");
				break;
			case 7588:
				a = LL14(L"Fateful Confrontation -ポムっと! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Fateful Confrontation -Pom! Ver.-");
				break;
			case 7589:
				a = LL14(L"ポムりますか", L"Shall We Pom?", L"On Pomme?", L"Facciamo Pom?", L"?Hacemos Pom?", L"? ??????", L"来一局?？", L"?? ???? ????", L"Сыграем в Пом?", L"Sollen Wir Pom Spielen?", L"Vamos Pom?", L"Zullen We Pomme?", L"Czy Zagramy w Pom?", L"Pom Oynayal?m m??");
				break;
			case 7590:
				a = LL14(L"エリィ絶叫コースター", L"Elie Scream Coaster", L"Montagnes Russes des Cris d'Elie", L"Montagne Russe delle Urla di Elie", L"Montana Rusa de los Gritos de Elie", L"?? ?? ???", L"艾莉尖叫?山?", L"????? ???? ????", L"Американские Горки Воплей Эли", L"Elie-Schrei-Achterbahn", L"Montanha-russa dos Gritos de Elie", L"Elie Schreeuw Achtbaan", L"Kolejka Krzykow Elie", L"Elie C??l?k Roller Coaster");
				break;
			case 7591:
				a = LL14(L"小さな英雄 -オルゴール-", L"Little Hero -Music Box-", L"Petit Heros -Boite a Musique-", L"Piccolo Eroe -Carillon-", L"Pequeno Heroe -Caja de Musica-", L"?? ?? -???-", L"小小英雄 -音?盒-", L"????? ?????? -????? ????????-", L"Маленький Герой -Музыкальная Шкатулка-", L"Kleiner Held -Spieluhr-", L"Pequeno Heroi -Caixa de Musica-", L"Kleine Held -Muziekdoos-", L"Ma?y Bohater -Pozytywka-", L"Kucuk Kahraman -Muzik Kutusu-");
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
		CString outPath = filen + _T(".wav");
		if (outPath.Right(4).MakeLower() != _T(".wav")) outPath += _T(".wav");
		if (cc.Open(outPath, CFile::modeCreate | CFile::modeReadWrite | CFile::typeBinary | CFile::shareExclusive, NULL) != TRUE) {
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
		if (ogg) cc.Write(ogg, whsize);
		if (wav) cc.Write(wav, whsize);
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
				MessageBox(s + LL14(
					L"Hzのサンプリングレートにサウンドカードが対応していません\n低いサンプリングレートを試みます。\n少々時間が掛かる場合があります。",
					L"Hz sampling rate not supported by sound card.\nTrying lower rate.\nThis may take a while.",
					L"Taux d'echantillonnage Hz non pris en charge par la carte son.\nEssai d'un taux inferieur.\nCela peut prendre un moment.",
					L"Frequenza di campionamento Hz non supportata dalla scheda audio.\nTentativo con frequenza inferiore.\nPotrebbe richiedere del tempo.",
					L"Tasa de muestreo Hz no compatible con la tarjeta de sonido.\nIntentando una tasa inferior.\nEsto puede tardar un momento.",
					L"Hz ??? ???? ??? ??? ???? ????\n?? ??? ???? ?????.\n?? ??? ?? ? ????.",
					L"声?不支持?Hz采?率\n正在???低的采?率。\n可能需要一些??。",
					L"????? ????? ?? ???? ???? ??? ??????? Hz\n???? ????? ???? ???.\n?? ?????? ??? ??? ?????.",
					L"Звуковая карта не поддерживает частоту дискретизации Hz\nПробуем более низкую частоту.\nЭто может занять некоторое время.",
					L"Hz-Abtastrate wird von der Soundkarte nicht unterstutzt.\nVersuche niedrigere Rate.\nDies kann einen Moment dauern.",
					L"Taxa de amostragem Hz nao suportada pela placa de som.\nTentando uma taxa inferior.\nIsso pode demorar um momento.",
					L"Hz-samplerate wordt niet ondersteund door de geluidskaart.\nLagere samplerate wordt geprobeerd.\nDit kan even duren.",
					L"Karta d?wi?kowa nie obs?uguje cz?stotliwo?ci probkowania Hz.\nProba ni?szej cz?stotliwo?ci.\nMo?e to chwil? potrwa?.",
					L"Ses kart? Hz ornekleme h?z?n? desteklemiyor.\nDaha du?uk h?z deneniyor.\nBu biraz zaman alabilir."),
					LL14(
						L"ogg/wav簡易プレイヤ",
						L"ogg/wav Simple Player",
						L"Lecteur Simple ogg/wav",
						L"Lettore Semplice ogg/wav",
						L"Reproductor Simple ogg/wav",
						L"ogg/wav ?? ????",
						L"ogg/wav?易播放器",
						L"???? ogg/wav ??????",
						L"Простой Плеер ogg/wav",
						L"ogg/wav Einfacher Player",
						L"Player Simples ogg/wav",
						L"Eenvoudige ogg/wav Speler",
						L"Prosty Odtwarzacz ogg/wav",
						L"ogg/wav Basit Oynat?c?")); flg0 = 1;
			}
			wavbit -= 1000;
			if (wavbit <= 0) {
				MessageBox(LL14(
					L"0Hzまで試みましたが、対応するサンプリングレートが存在しませんでした。\nサウンドボード(カード)が存在していない可能性があります。",
					L"Tried down to 0Hz but no supported sampling rate found.\nSound card may not be present.",
					L"Essaye jusqu'a 0Hz mais aucun taux d'echantillonnage compatible trouve.\nLa carte son est peut-etre absente.",
					L"Tentato fino a 0Hz ma nessuna frequenza di campionamento supportata trovata.\nLa scheda audio potrebbe essere assente.",
					L"Intentado hasta 0Hz pero no se encontro tasa de muestreo compatible.\nEs posible que no haya tarjeta de sonido.",
					L"0Hz?? ????? ???? ??? ???? ???? ?????.\n??? ??(??)? ???? ?? ???? ????.",
					L"已??至0Hz，但未找到支持的采?率。\n可能不存在声?。",
					L"??? ???????? ??? 0Hz ??? ?? ????? ??? ???? ??? ????? ?????.\n?? ???? ????? ????? ??? ??????.",
					L"Попытка до 0Hz, но поддерживаемая частота дискретизации не найдена.\nВозможно, звуковая карта отсутствует.",
					L"Bis 0Hz versucht, aber keine unterstutzte Abtastrate gefunden.\nDie Soundkarte ist moglicherweise nicht vorhanden.",
					L"Tentado ate 0Hz mas nenhuma taxa de amostragem suportada foi encontrada.\nA placa de som pode estar ausente.",
					L"Tot 0Hz geprobeerd maar geen ondersteunde samplerate gevonden.\nGeluidskaart is mogelijk niet aanwezig.",
					L"Probowano do 0Hz, ale nie znaleziono obs?ugiwanej cz?stotliwo?ci probkowania.\nKarta d?wi?kowa mo?e by? nieobecna.",
					L"0Hz'e kadar denendi ancak desteklenen ornekleme h?z? bulunamad?.\nSes kart? mevcut olmayabilir."),
					LL14(
						L"ogg/wav簡易プレイヤ",
						L"ogg/wav Simple Player",
						L"Lecteur Simple ogg/wav",
						L"Lettore Semplice ogg/wav",
						L"Reproductor Simple ogg/wav",
						L"ogg/wav ?? ????",
						L"ogg/wav?易播放器",
						L"???? ogg/wav ??????",
						L"Простой Плеер ogg/wav",
						L"ogg/wav Einfacher Player",
						L"Player Simples ogg/wav",
						L"Eenvoudige ogg/wav Speler",
						L"Prosty Odtwarzacz ogg/wav",
						L"ogg/wav Basit Oynat?c?")); tagfile = fnn;
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
			AfxMessageBox(LL14(
				L"DirectSoundが開けませんでした。",
				L"Could not open DirectSound.",
				L"Impossible d'ouvrir DirectSound.",
				L"Impossibile aprire DirectSound.",
				L"No se pudo abrir DirectSound.",
				L"DirectSound? ? ? ?????.",
				L"无法打?DirectSound。",
				L"???? ??? DirectSound.",
				L"Не удалось открыть DirectSound.",
				L"DirectSound konnte nicht geoffnet werden.",
				L"Nao foi possivel abrir o DirectSound.",
				L"Kan DirectSound niet openen.",
				L"Nie mo?na otworzy? DirectSound.",
				L"DirectSound ac?lamad?."));
			if (r == DSERR_ALLOCATED) {
				AfxMessageBox(LL14(
					L"優先レベルなどのリソースが他の呼び出しによって既に使用中であるため、要求は失敗した。",
					L"Request failed because resources such as priority level are already in use by another call.",
					L"La demande a echoue car des ressources telles que le niveau de priorite sont deja utilisees par un autre appel.",
					L"La richiesta e fallita perche risorse come il livello di priorita sono gia in uso da un'altra chiamata.",
					L"La solicitud fallo porque recursos como el nivel de prioridad ya estan en uso por otra llamada.",
					L"?? ?? ?? ???? ?? ??? ?? ?? ?? ???? ??? ??????.",
					L"?求失?，因??先?等?源已被其他?用占用。",
					L"??? ????? ??? ????? ??? ????? ???????? ??????? ?????? ?? ???? ??????? ???.",
					L"Запрос не выполнен, так как ресурсы, такие как уровень приоритета, уже используются другим вызовом.",
					L"Anfrage fehlgeschlagen, da Ressourcen wie die Prioritatsstufe bereits von einem anderen Aufruf verwendet werden.",
					L"A solicitacao falhou porque recursos como o nivel de prioridade ja estao em uso por outra chamada.",
					L"Verzoek mislukt omdat resources zoals prioriteitsniveau al in gebruik zijn door een andere aanroep.",
					L"??danie nie powiod?o si?, poniewa? zasoby takie jak poziom priorytetu s? ju? u?ywane przez inne wywo?anie.",
					L"?stek, oncelik duzeyi gibi kaynaklar ba?ka bir ca?r? taraf?ndan kullan?ld???ndan ba?ar?s?z oldu."));
			}
			else if (r == DSERR_CONTROLUNAVAIL) {
				AfxMessageBox(LL14(
					L"呼び出し元が要求するバッファ コントロール (ボリューム、パンなど) は利用できない。",
					L"Buffer control (volume, pan, etc.) requested by caller is not available.",
					L"Le controle de tampon (volume, panoramique, etc.) demande par l'appelant n'est pas disponible.",
					L"Il controllo del buffer (volume, pan, ecc.) richiesto dal chiamante non e disponibile.",
					L"El control de bufer (volumen, paneo, etc.) solicitado por el llamador no esta disponible.",
					L"???? ??? ?? ???(??, ? ?)? ??? ? ????.",
					L"?用方?求的?冲区控件（音量、声像等）不可用。",
					L"???? ?????? ?? ?????? ?????? (?????? ???????? ???) ???? ???? ?????? ??? ????.",
					L"Управление буфером (громкость, панорама и т.д.), запрошенное вызывающей стороной, недоступно.",
					L"Die vom Aufrufer angeforderte Puffersteuerung (Lautstarke, Balance usw.) ist nicht verfugbar.",
					L"O controle de buffer (volume, pan, etc.) solicitado pelo chamador nao esta disponivel.",
					L"Bufferbesturing (volume, pan, etc.) gevraagd door de aanroeper is niet beschikbaar.",
					L"Kontrola bufora (g?o?no??, panorama itp.) ??dana przez wywo?uj?cego jest niedost?pna.",
					L"Arayan taraf?ndan istenen arabellek kontrolu (ses, pan vb.) kullan?lam?yor."));
			}
			else if (r == DSERR_BADFORMAT) {
				AfxMessageBox(LL14(
					L"指定したウェーブ フォーマットはサポートされていない。",
					L"Specified wave format is not supported.",
					L"Le format d'onde specifie n'est pas pris en charge.",
					L"Il formato wave specificato non e supportato.",
					L"El formato de onda especificado no es compatible.",
					L"??? ??? ??? ???? ????.",
					L"指定的波形格式不受支持。",
					L"????? ?????? ?????? ??? ?????.",
					L"Указанный формат звуковой волны не поддерживается.",
					L"Das angegebene Wellenformat wird nicht unterstutzt.",
					L"O formato de onda especificado nao e suportado.",
					L"Het opgegeven golfformaat wordt niet ondersteund.",
					L"Okre?lony format fali nie jest obs?ugiwany.",
					L"Belirtilen dalga bicimi desteklenmiyor."));
			}
			else if (r == DSERR_INVALIDPARAM) {
				AfxMessageBox(LL14(
					L"無効なパラメータが関数に渡された。",
					L"Invalid parameter passed to function.",
					L"Un parametre invalide a ete passe a la fonction.",
					L"Un parametro non valido e stato passato alla funzione.",
					L"Se paso un parametro no valido a la funcion.",
					L"??? ??? ????? ???????.",
					L"向函数??了无效参数。",
					L"?? ????? ????? ??? ???? ??? ??????.",
					L"В функцию передан недопустимый параметр.",
					L"Ein ungultiger Parameter wurde an die Funktion ubergeben.",
					L"Um parametro invalido foi passado para a funcao.",
					L"Ongeldige parameter doorgegeven aan functie.",
					L"Do funkcji przekazano nieprawid?owy parametr.",
					L"Fonksiyona gecersiz parametre iletildi."));
			}
			else if (r == DSERR_NOAGGREGATION) {
				AfxMessageBox(LL14(
					L"このオブジェクトは COM 集合化をサポートしない。",
					L"This object does not support COM aggregation.",
					L"Cet objet ne prend pas en charge l'agregation COM.",
					L"Questo oggetto non supporta l'aggregazione COM.",
					L"Este objeto no admite la agregacion COM.",
					L"? ??? COM ???? ???? ????.",
					L"此?象不支持COM聚合。",
					L"??? ?????? ?? ???? ????? COM.",
					L"Этот объект не поддерживает агрегирование COM.",
					L"Dieses Objekt unterstutzt keine COM-Aggregation.",
					L"Este objeto nao suporta agregacao COM.",
					L"Dit object ondersteunt geen COM-aggregatie.",
					L"Ten obiekt nie obs?uguje agregacji COM.",
					L"Bu nesne COM birle?tirmesini desteklemiyor."));
			}
			else if (r == DSERR_OUTOFMEMORY) {
				AfxMessageBox(LL14(
					L"DirectSound サブシステムは、呼び出し元の要求を完了するための十分なメモリを割り当てられなかった。",
					L"DirectSound subsystem could not allocate enough memory to complete the request.",
					L"Le sous-systeme DirectSound n'a pas pu allouer suffisamment de memoire pour terminer la demande.",
					L"Il sottosistema DirectSound non e riuscito ad allocare memoria sufficiente per completare la richiesta.",
					L"El subsistema DirectSound no pudo asignar suficiente memoria para completar la solicitud.",
					L"DirectSound ?????? ??? ???? ?? ??? ???? ??? ? ?????.",
					L"DirectSound子系?无法分配足?的内存来完成?求。",
					L"?? ????? ?????? ?????? DirectSound ?? ????? ????? ????? ?????? ?????.",
					L"Подсистема DirectSound не смогла выделить достаточно памяти для выполнения запроса.",
					L"Das DirectSound-Subsystem konnte nicht genug Speicher zuweisen, um die Anfrage abzuschliesen.",
					L"O subsistema DirectSound nao pode alocar memoria suficiente para concluir a solicitacao.",
					L"DirectSound-subsysteem kon niet genoeg geheugen toewijzen om het verzoek te voltooien.",
					L"Podsystem DirectSound nie mog? przydzieli? wystarczaj?cej ilo?ci pami?ci do realizacji ??dania.",
					L"DirectSound alt sistemi iste?i tamamlamak icin yeterli bellek tahsis edemedi."));
			}
			else if (r == DSERR_UNINITIALIZED) {
				AfxMessageBox(LL14(
					L"他のメソッドを呼び出す前に IDirectSound::Initialize メソッドを呼び出さなかったか、呼び出しが成功しなかった。",
					L"IDirectSound::Initialize was not called before other methods, or the call failed.",
					L"IDirectSound::Initialize n'a pas ete appele avant les autres methodes, ou l'appel a echoue.",
					L"IDirectSound::Initialize non e stato chiamato prima degli altri metodi, oppure la chiamata e fallita.",
					L"IDirectSound::Initialize no fue llamado antes que otros metodos, o la llamada fallo.",
					L"?? ???? ???? ?? IDirectSound::Initialize ???? ???? ???? ??? ??????.",
					L"在?用其他方法之前未?用IDirectSound::Initialize，或?用失?。",
					L"?? ??? ??????? IDirectSound::Initialize ??? ????? ??????? ?? ??? ?????????.",
					L"IDirectSound::Initialize не был вызван перед другими методами, или вызов завершился неудачно.",
					L"IDirectSound::Initialize wurde nicht vor anderen Methoden aufgerufen, oder der Aufruf ist fehlgeschlagen.",
					L"IDirectSound::Initialize nao foi chamado antes de outros metodos, ou a chamada falhou.",
					L"IDirectSound::Initialize werd niet aangeroepen voor andere methoden, of de aanroep is mislukt.",
					L"IDirectSound::Initialize nie zosta?o wywo?ane przed innymi metodami lub wywo?anie nie powiod?o si?.",
					L"IDirectSound::Initialize di?er yontemlerden once ca?r?lmad? veya ca?r? ba?ar?s?z oldu."));
			}
			else if (r == DSERR_UNSUPPORTED) {
				AfxMessageBox(LL14(
					L"呼び出した関数はこの時点ではサポートされていない。",
					L"The called function is not supported at this point.",
					L"La fonction appelee n'est pas prise en charge a ce stade.",
					L"La funzione chiamata non e supportata in questo momento.",
					L"La funcion llamada no es compatible en este momento.",
					L"??? ??? ? ????? ???? ????.",
					L"?用的函数在此?不受支持。",
					L"?????? ????????? ??? ?????? ?? ??? ???????.",
					L"Вызванная функция не поддерживается в данный момент.",
					L"Die aufgerufene Funktion wird zu diesem Zeitpunkt nicht unterstutzt.",
					L"A funcao chamada nao e suportada neste momento.",
					L"De aangeroepen functie wordt op dit punt niet ondersteund.",
					L"Wywo?ana funkcja nie jest obs?ugiwana w tym miejscu.",
					L"Ca?r?lan fonksiyon bu noktada desteklenmiyor."));
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
				MessageBox(s + LL14(
					L"Hzのサンプリングレートでヒットしましたため、該当サンプリングレートで演奏します。",
					L"Hz sampling rate matched; playing at that rate.",
					L"Taux d'echantillonnage Hz trouve ; lecture a ce taux.",
					L"Frequenza di campionamento Hz trovata; riproduzione a quella frequenza.",
					L"Tasa de muestreo Hz encontrada; reproduciendo a esa tasa.",
					L"Hz ??? ????? ?????? ?? ??? ???? ?????.",
					L"已匹配到Hz采?率，将以?采?率?行播放。",
					L"?? ?????? ??? ???? ??? ??????? Hz? ???? ??????? ???? ??????.",
					L"Частота дискретизации Hz найдена; воспроизведение на этой частоте.",
					L"Hz-Abtastrate gefunden; Wiedergabe mit dieser Rate.",
					L"Taxa de amostragem Hz encontrada; reproduzindo nessa taxa.",
					L"Hz-samplerate gevonden; afspelen op die rate.",
					L"Znaleziono cz?stotliwo?? probkowania Hz; odtwarzanie z t? cz?stotliwo?ci?.",
					L"Hz ornekleme h?z? e?le?ti; o h?zda oynat?l?yor."),
					LL14(
						L"ogg/wav簡易プレイヤ",
						L"ogg/wav Simple Player",
						L"Lecteur Simple ogg/wav",
						L"Lettore Semplice ogg/wav",
						L"Reproductor Simple ogg/wav",
						L"ogg/wav ?? ????",
						L"ogg/wav?易播放器",
						L"???? ogg/wav ??????",
						L"Простой Плеер ogg/wav",
						L"ogg/wav Einfacher Player",
						L"Player Simples ogg/wav",
						L"Eenvoudige ogg/wav Speler",
						L"Prosty Odtwarzacz ogg/wav",
						L"ogg/wav Basit Oynat?c?")); savedata.samples = wavbit;
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
		if ((mode >= 10 && mode <= 21) || mode < -10 || mode == -6 || mode == 30 || (mode == 999 && wav999_use_adbuf))
			playwavadpcm(bufwav3, 0, len1, len2);//データ獲得
		else if (mode == -10)
			playwavmp3(bufwav3, 0, len1, len2);//データ獲得
		else if (mode == 999)
			playwavwav(bufwav3, 0, len1, len2);//データ獲得
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
			if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
				f123.Close();
				if (IDYES == MessageBox(LL14(
					L"途中再生データが存在します。\n前回中断した部分から再生しますか？\nはい = 途中から再生\nいいえ = はじめから再生",
					L"Resume data exists.\nResume from where you left off?\nYes = Resume\nNo = Play from start",
					L"Des donnees de reprise existent.\nReprendre la ou vous vous etes arrete?\nOui = Reprendre\nNon = Jouer depuis le debut",
					L"Esistono dati di ripresa.\nRiprendere da dove ci si e fermati?\nSi = Riprendi\nNo = Riproduci dall'inizio",
					L"Existen datos de reanudacion.\n?Reanudar desde donde lo dejo?\nSi = Reanudar\nNo = Reproducir desde el inicio",
					L"?? ?? ???? ?????.\n??? ??? ???? ??????\n? = ???? ??\n??? = ???? ??",
					L"存在中途播放数据。\n是否从上次中断???播放？\n是 = 从中途播放\n否 = 从?播放",
					L"???? ?????? ???????.\n?? ???? ????????? ?? ??? ??????\n??? = ???????\n?? = ????? ?? ???????",
					L"Данные возобновления существуют.\nПродолжить с места остановки?\nДа = Продолжить\nНет = Играть с начала",
					L"Fortsetzungsdaten vorhanden.\nVon der Unterbrechungsstelle fortfahren?\nJa = Fortsetzen\nNein = Von Anfang abspielen",
					L"Dados de retomada existem.\nRetomar de onde parou?\nSim = Retomar\nNao = Reproduzir do inicio",
					L"Hervatgegevens aanwezig.\nHervatten waar u gebleven was?\nJa = Hervatten\nNee = Afspelen vanaf het begin",
					L"Istniej? dane wznowienia.\nWznowi? od miejsca przerwania?\nTak = Wznow\nNie = Odtworz od pocz?tku",
					L"Devam verisi mevcut.\nKald???n?z yerden devam edilsin mi?\nEvet = Devam et\nHay?r = Ba?tan oynat"),
					LL14(
						L"再生確認",
						L"Playback confirmation",
						L"Confirmation de lecture",
						L"Conferma riproduzione",
						L"Confirmacion de reproduccion",
						L"?? ??",
						L"播放??",
						L"????? ???????",
						L"Подтверждение воспроизведения",
						L"Wiedergabebestatigung",
						L"Confirmacao de reproducao",
						L"Afspeelbevestiging",
						L"Potwierdzenie odtwarzania",
						L"Oynatma onay?"),
					MB_YESNO)) {
					flggg = 1;
				}
				else {
					CFile::Remove(filen + _T(".save"));
				}
			}
			if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE && flggg == 1) {
				f123.Close();
				if (pGraphBuilder)pMainFrame1->plays2();
				//if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
				if (mode == -10) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
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
				if (mode == 999) {
					if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
						f123.Read(&playb, sizeof(__int64));
						if (wav999_use_adbuf)
							seekadpcm((int)playb);
						else
							wav_.Seek(playb / (wavch * (wavsam / 8)));
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
	if (mode == -3 || mode == -8 || mode == -9 || mode == -10 || mode == 999) endf = 1;
	loopcnt = 0;
	if (pl && plw) {
		int plc = 1;
		if (mode == -10)
			plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, (oggsize / (2 * wavch * wavbit / 4) / ((mode == -9) ? 4 : 1)), 1);
		else if (mode == 999)
			plc = pl->Add(tagfile, mode, loop1, loop2, tagname, tagalbum, filen, 0, (wavbit > 0) ? (int)(loop2 / wavbit) : 0, 1);
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
	m_ps.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"?? ??", L"?停", L"????? ????", L"Пауза", L"Pause", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"));
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

BOOL COggDlg::ExportToWav(playlistdata0* pc, CString outputPath, int loopCount)
{
	if (!pc || outputPath.IsEmpty() || loopCount < 1) return FALSE;
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
	wavExportMute = TRUE;
	int saveloop_bak = savedata.saveloop;
	savedata.saveloop = 1;  // ループを有効にしてwavExportLoopCountで制御
	if (wavExportMute) m_sl.SetPos(0);
	play();
	wavExportPath.Empty();
	wavExportLoopCount = 0;
	wavExportMute = FALSE;
	savedata.saveloop = saveloop_bak;
	return TRUE;
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
			a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"?? ??? 《VII?》", L"特科班《VII?》", L"????? ??????", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klasse VII", L"Klasa VII", L"S?n?f VII");
			break;
		case 8002:
			a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours de l'Avant", L"Sempre Avanti", L"Siempre Adelante", L"???, ???", L"唯有向前", L"?????? ??? ??????", L"Всегда Вперёд", L"Immer Vorwarts", L"Sempre em Frente", L"Altijd Vooruit", L"Zawsze Naprzod", L"Hep ?leri");
			break;
		case 8100:
			a = LL14(L"近郊都市トリスタ", L"Suburban City Trista", L"Ville de Banlieue Trista", L"Citta Suburbana Trista", L"Ciudad Suburbana Trista", L"?? ?? ????", L"近郊城市特里斯塔", L"????? ?????? ???????", L"Пригородный Город Триста", L"Vorortstadt Trista", L"Cidade Suburbana Trista", L"Buitenstad Trista", L"Miasto Podmiejskie Trista", L"Banliyo ?ehri Trista");
			break;
		case 8101:
			a = LL14(L"交易町ケルディック", L"Trading Town Celdic", L"Ville Marchande Celdic", L"Citta Commerciale Celdic", L"Ciudad Comercial Celdic", L"?? ?? ??", L"?易小?塞?迪克", L"????? ?????? ????????", L"Торговый Город Селдик", L"Handelsstadt Celdic", L"Cidade Comercial Celdic", L"Handelsstad Celdic", L"Miasto Handlowe Celdic", L"Ticaret Kasabas? Celdic");
			break;
		case 8102:
			a = LL14(L"翡翠の公都バリアハート", L"Jade Capital Bareahard", L"Capitale de Jade Bareahard", L"Capitale di Giada Bareahard", L"Capital de Jade Bareahard", L"??? ?? ?????", L"翡翠公都巴里?哈特", L"????? ????? ?????????", L"Нефритовая Столица Бэрихард", L"Jade-Hauptstadt Bareahard", L"Capital de Jade Bareahard", L"Jade-Hoofdstad Bareahard", L"Jadeitowa Stolica Bareahard", L"Ye?im Ba?kenti Bareahard");
			break;
		case 8103:
			a = LL14(L"湖畔の街レグラム", L"Lakeside Town Legram", L"Ville au Bord du Lac Legram", L"Citta Lacustre Legram", L"Ciudad Junto al Lago Legram", L"??? ?? ???", L"湖畔小?勒格拉姆", L"????? ?????? ??? ??? ???????", L"Город у Озера Леграм", L"Seestadt Legram", L"Cidade a Beira do Lago Legram", L"Meerstad Legram", L"Miasto nad Jeziorem Legram", L"Gol Kenar? Kasabas? Legram");
			break;
		case 8104:
			a = LL14(L"黒銀の鋼都ルーレ", L"Iron City Roer", L"Cite d'Acier Roer", L"Citta d'Acciaio Roer", L"Ciudad de Acero Roer", L"??? ?? ??", L"黑??都??", L"????? ????? ???", L"Стальной Город Рур", L"Stahlstadt Roer", L"Cidade de Aco Roer", L"Staalstad Roer", L"Stalowe Miasto Roer", L"Celik ?ehri Roer");
			break;
		case 8106:
			a = LL14(L"遊牧民の集落", L"Nomad Settlement", L"Campement Nomade", L"Accampamento Nomade", L"Asentamiento Nomada", L"???? ??", L"游牧民聚落", L"??????? ?????", L"Поселение Кочевников", L"Nomadensiedlung", L"Assentamento Nomade", L"Nomadennederzetting", L"Osada Koczownikow", L"Gocebe Yerle?imi");
			break;
		case 8107:
			a = LL14(L"緋の帝都ヘイムダル", L"Crimson Capital Heimdallr", L"Capitale Cramoisie Heimdallr", L"Capitale Cremisi Heimdallr", L"Capital Carmesi Heimdallr", L"?? ?? ???", L"??帝都海姆??", L"??????? ???????? ???????", L"Малиновая Столица Хеймдалл", L"Purpurrote Hauptstadt Heimdallr", L"Capital Carmesim Heimdallr", L"Karmozijnrode Hoofdstad Heimdallr", L"Karmazynowa Stolica Heimdallr", L"K?rm?z? Ba?kent Heimdallr");
			break;
		case 8108:
			a = LL14(L"癒しの我が家", L"Healing Home", L"Foyer Apaisant", L"Casa Guaritrice", L"Hogar Sanador", L"??? ?? ?", L"治愈的家", L"????? ??????", L"Исцеляющий Дом", L"Heilendes Zuhause", L"Lar Curador", L"Helend Thuis", L"Uzdrawiaj?cy Dom", L"?yile?tirici Ev");
			break;
		case 8109:
			a = LL14(L"ダイニングバー《F》", L"Dining Bar F", L"Bar-Restaurant F", L"Dining Bar F", L"Bar Comedor F", L"??? ? 《F》", L"餐?酒?《F》", L"??? ?????? F", L"Обеденный Бар F", L"Dining Bar F", L"Bar Restaurante F", L"Dining Bar F", L"Bar Restauracyjny F", L"Yemek Bar? F");
			break;
		case 8110:
			a = LL14(L"常在戦場の気概", L"Ever-Present War Spirit", L"Esprit de Guerre Omnipresent", L"Spirito di Guerra Onnipresente", L"Espiritu de Guerra Omnipresente", L"?? ??? ??", L"常???的气概", L"??? ????? ???????", L"Вечный Боевой Дух", L"Allgegenwartiger Kriegsgeist", L"Espirito de Guerra Onipresente", L"Altijd Aanwezige Oorlogsgeest", L"Wszechobecny Duch Wojenny", L"Her Zaman Sava? Ruhu");
			break;
		case 8111:
			a = LL14(L"ガレリアの巨壁", L"Garelia Fortress", L"Forteresse de Garelia", L"Fortezza di Garelia", L"Fortaleza de Garelia", L"????? ??", L"加勒利?巨壁", L"??? ???????", L"Крепость Гарелия", L"Festung Garelia", L"Fortaleza de Garelia", L"Vesting Garelia", L"Twierdza Garelia", L"Garelia Kalesi");
			break;
		case 8120:
			a = LL14(L"足湯の温もり", L"Foot Bath Warmth", L"Chaleur du Bain de Pieds", L"Calore del Pediluvio", L"Calidez del Bano de Pies", L"??? ??", L"足浴的温暖", L"??? ???? ?????", L"Тепло Ножной Ванны", L"Warme des Fusbades", L"Calor do Banho de Pes", L"Warmte van het Voetbad", L"Ciep?o K?pieli Stop", L"Ayak Banyosunun S?cakl???");
			break;
		case 8121:
			a = LL14(L"静寂の郷", L"Silent Village", L"Village Silencieux", L"Villaggio Silenzioso", L"Pueblo Silencioso", L"??? ??", L"静寂之?", L"?????? ???????", L"Тихая Деревня", L"Stilles Dorf", L"Vila Silenciosa", L"Stil Dorp", L"Ciche Miasteczko", L"Sessiz Koy");
			break;
		case 8122:
			a = LL14(L"明日への休息", L"Rest for Tomorrow", L"Repos pour Demain", L"Riposo per Domani", L"Descanso para Manana", L"??? ?? ??", L"?明日而休息", L"???? ???? ????", L"Отдых ради Завтра", L"Ruhe fur Morgen", L"Descanso para Amanha", L"Rust voor Morgen", L"Odpoczynek na Jutro", L"Yar?n ?cin Dinlenme");
			break;
		case 8123:
			a = LL14(L"春の陽射し", L"Spring Sunshine", L"Soleil de Printemps", L"Sole Primaverile", L"Sol de Primavera", L"? ??", L"春日?光", L"??? ??????", L"Весеннее Солнце", L"Fruhlingssonne", L"Sol de Primavera", L"Lentezonnestralen", L"Wiosenne S?o?ce", L"?lkbahar Gune?i");
			break;
		case 8125:
			a = LL14(L"カレイジャス発進！", L"Courageous Launch!", L"Decollage du Courageux!", L"Lancio del Courageous!", L"!Lanzamiento del Courageous!", L"????? ??!", L"无畏号出?！", L"?????? ???????!", L"Старт Отважного!", L"Courageous startet!", L"Lancamento do Courageous!", L"Courageous lanceert!", L"Start Courageous!", L"Courageous F?rlat?ld?!");
			break;
		case 8126:
			a = LL14(L"目覚める意志", L"Awakening Will", L"Volonte qui s'Eveille", L"Volonta che si Risveglia", L"Voluntad que Despierta", L"??? ??", L"?醒的意志", L"????? ??????", L"Пробуждающаяся Воля", L"Erwachender Wille", L"Vontade que Desperta", L"Ontwakende Wil", L"Przebudzaj?ca si? Wola", L"Uyan?? ?radesi");
			break;
		case 8127:
			a = LL14(L"白銀の巨船", L"Silver Ship", L"Vaisseau d'Argent", L"Nave d'Argento", L"Nave de Plata", L"??? ??", L"白?巨?", L"??????? ??????", L"Серебряный Корабль", L"Silbernes Schiff", L"Navio de Prata", L"Zilveren Schip", L"Srebrny Okr?t", L"Gumu? Gemi");
			break;
		case 8150:
			a = LL14(L"放課後の時間", L"After School", L"Apres l'Ecole", L"Dopo Scuola", L"Despues de la Escuela", L"?? ?? ??", L"放学后的?光", L"??? ???????", L"После Уроков", L"Nach der Schule", L"Depois da Escola", L"Na School", L"Po Szkole", L"Okul Sonras?");
			break;
		case 8152:
			a = LL14(L"さわやかな朝", L"Refreshing Morning", L"Matin Rafraichissant", L"Mattino Rinfrescante", L"Manana Refrescante", L"??? ??", L"清爽的早晨", L"???? ????", L"Бодрящее Утро", L"Erfrischender Morgen", L"Manha Refrescante", L"Verfrissende Ochtend", L"Orze?wiaj?cy Poranek", L"Ferah Sabah");
			break;
		case 8153:
			a = LL14(L"雨音の学院", L"Rain-sound Academy", L"Academie sous la Pluie", L"Accademia della Pioggia", L"Academia Bajo la Lluvia", L"???? ??", L"雨声学院", L"???????? ??? ?????", L"Академия Дождя", L"Regen-Akademie", L"Academia da Chuva", L"Regen-Academie", L"Akademia Deszczu", L"Ya?mur Sesi Akademisi");
			break;
		case 8154:
			a = LL14(L"爽やかな陽射し", L"Clear Sunshine", L"Soleil Clair", L"Sole Limpido", L"Sol Despejado", L"??? ??", L"清爽的?光", L"??? ?????", L"Ясное Солнце", L"Klarer Sonnenschein", L"Sol Claro", L"Helder Zonlicht", L"Jasne S?o?ce", L"Berrak Gune? I????");
			break;
		case 8156:
			a = LL14(L"トールズ士官学院祭", L"Thors Academy Festival", L"Festival de l'Academie Thors", L"Festival dell'Accademia Thors", L"Festival de la Academia Thors", L"??? ?????", L"托?斯士官学院祭", L"?????? ???????? ????", L"Праздник Академии Торс", L"Thors-Akademie-Festival", L"Festival da Academia Thors", L"Thors Academie Festival", L"Festiwal Akademii Thors", L"Thors Akademisi Festivali");
			break;
		case 8158:
			a = LL14(L"青空の開放感", L"Open Sky", L"Ciel Ouvert", L"Cielo Aperto", L"Cielo Abierto", L"?? ??? ???", L"?天的??感", L"???? ??????", L"Открытое Небо", L"Offener Himmel", L"Ceu Aberto", L"Open Lucht", L"Otwarte Niebo", L"Ac?k Gokyuzu");
			break;
		case 8159:
			a = LL14(L"自由行動日", L"Free Day", L"Journee Libre", L"Giorno Libero", L"Dia Libre", L"?? ???", L"自由行?日", L"??? ??", L"Свободный День", L"Freier Tag", L"Dia Livre", L"Vrije Dag", L"Wolny Dzie?", L"Serbest Gun");
			break;
		case 8200:
			a = LL14(L"異郷の空", L"Foreign Sky", L"Ciel Etranger", L"Cielo Straniero", L"Cielo Extranjero", L"??? ??", L"??的天空", L"???? ???? ??????", L"Чужое Небо", L"Fremder Himmel", L"Ceu Estrangeiro", L"Vreemde Hemel", L"Obce Niebo", L"Yabanc? Gokyuzu");
			break;
		case 8201:
			a = LL14(L"峡谷道を往く", L"Through the Canyon", L"A Travers le Canyon", L"Attraverso il Canyon", L"A Traves del Canon", L"???? ??", L"穿越峡谷之道", L"??? ??????", L"Сквозь Каньон", L"Durch die Schlucht", L"Atraves do Canyon", L"Door de Kloof", L"Przez Kanion", L"Kanyondan Gecerek");
			break;
		case 8202:
			a = LL14(L"精霊の小道", L"Spirit Path", L"Chemin des Esprits", L"Sentiero degli Spiriti", L"Senda de los Espiritus", L"??? ???", L"精?小道", L"???? ???????", L"Тропа Духов", L"Geisterpfad", L"Caminho dos Espiritos", L"Geestenpad", L"?cie?ka Duchow", L"Ruh Yolu");
			break;
		case 8203:
			a = LL14(L"蒼穹の大地", L"Azure Skies Land", L"Terre du Ciel Azure", L"Terra del Cielo Azzurro", L"Tierra del Cielo Azul", L"??? ??", L"?穹大地", L"??? ?????? ???????", L"Земля Лазурного Неба", L"Land des Azurhimmels", L"Terra do Ceu Azul", L"Land van de Azuurblauwe Lucht", L"Kraina Lazurowego Nieba", L"Gok Mavisi Topraklar");
			break;
		case 8210:
			a = LL14(L"戦火を越えて", L"Beyond the Flames of War", L"Au-Dela des Flammes de la Guerre", L"Oltre le Fiamme della Guerra", L"Mas Alla de las Llamas de la Guerra", L"??? ???", L"超越?火", L"?? ???? ????? ?????", L"За Пламенем Войны", L"Jenseits der Kriegsflammen", L"Alem das Chamas da Guerra", L"Voorbij de Vlammen van de Oorlog", L"Poza P?omieniami Wojny", L"Sava??n Alevlerinin Otesinde");
			break;
		case 8212:
			a = L"Trudge Along";
			break;
		case 8213:
			a = LL14(L"冬の訪れ", L"Arrival of Winter", L"Arrivee de l'Hiver", L"Arrivo dell'Inverno", L"Llegada del Invierno", L"??? ??", L"冬日来?", L"???? ??????", L"Приход Зимы", L"Ankunft des Winters", L"Chegada do Inverno", L"Komst van de Winter", L"Nadej?cie Zimy", L"K???n Geli?i");
			break;
		case 8300:
			a = LL14(L"旧校舎の謎", L"Old Schoolhouse Mystery", L"Mystere de l'Ancienne Ecole", L"Mistero della Vecchia Scuola", L"Misterio del Antiguo Edificio Escolar", L"???? ????", L"旧校舍之?", L"??? ?????? ??????? ??????", L"Загадка Старого Корпуса", L"Geheimnis des alten Schulgebaudes", L"Misterio do Antigo Predio Escolar", L"Mysterie van het Oude Schoolgebouw", L"Tajemnica Starego Budynku Szkolnego", L"Eski Okul Binas?n?n Gizemi");
			break;
		case 8301:
			a = LL14(L"探索", L"Exploration", L"Exploration", L"Esplorazione", L"Exploracion", L"??", L"探索", L"???????", L"Исследование", L"Erkundung", L"Exploracao", L"Verkenning", L"Eksploracja", L"Ke?if");
			break;
		case 8302:
			a = LL14(L"深淵へ向かう", L"Toward the Abyss", L"Vers l'Abime", L"Verso l'Abisso", L"Hacia el Abismo", L"??? ???", L"走向深渊", L"??? ???????", L"К Бездне", L"In den Abgrund", L"Rumo ao Abismo", L"Naar de Afgrond", L"Ku Otch?ani", L"Ucuruma Do?ru");
			break;
		case 8303:
			a = LL14(L"聖女の城", L"Saint's Castle", L"Chateau de la Sainte", L"Castello della Santa", L"Castillo de la Santa", L"??? ?", L"?女之城", L"???? ???????", L"Замок Святой", L"Schloss der Heiligen", L"Castelo da Santa", L"Kasteel van de Heilige", L"Zamek ?wi?tej", L"Aziz Kale");
			break;
		case 8304:
			a = LL14(L"明日を掴むために", L"To Seize Tomorrow", L"Pour Saisir Demain", L"Per Afferrare il Domani", L"Para Aferrar el Manana", L"??? ?? ??", L"?了抓住明日", L"???? ?????? ????", L"Чтобы Схватить Завтра", L"Um Morgen zu Greifen", L"Para Agarrar o Amanha", L"Om Morgen te Grijpen", L"By Pochwyci? Jutro", L"Yar?n? Yakalamak ?cin");
			break;
		case 8305:
			a = LL14(L"地下に眠る遺構", L"Ruins Beneath", L"Ruines Souterraines", L"Rovine Sotterranee", L"Ruinas Subterraneas", L"??? ?? ??", L"?眠地下的??", L"??????? ???????", L"Подземные Руины", L"Unterirdische Ruinen", L"Ruinas Subterraneas", L"Ondergrondse Ruines", L"Podziemne Ruiny", L"Yeralt?ndaki Harabeler");
			break;
		case 8308:
			a = LL14(L"世の礎たるために", L"To Be the World's Foundation", L"Pour Etre le Fondement du Monde", L"Per Essere il Fondamento del Mondo", L"Para Ser el Fundamento del Mundo", L"??? 礎? ?? ??", L"?成?世界的基石", L"???? ?? ???? ???? ??????", L"Чтобы Стать Основой Мира", L"Um das Fundament der Welt zu Sein", L"Para Ser o Fundamento do Mundo", L"Om het Fundament van de Wereld te Zijn", L"By By? Fundamentem ?wiata", L"Dunyan?n Temeli Olmak ?cin");
			break;
		case 8310:
			a = LL14(L"精霊窟", L"Spirit Cave", L"Grotte des Esprits", L"Grotta degli Spiriti", L"Cueva de los Espiritus", L"???", L"精?窟", L"??? ???????", L"Пещера Духов", L"Geisterhohle", L"Caverna dos Espiritos", L"Geestesgrot", L"Jaskinia Duchow", L"Ruh Ma?aras?");
			break;
		case 8311:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8312:
			a = L"Phantasmal Blaze";
			break;
		case 8313:
			a = LL14(L"夢幻回廊", L"Phantasmagoria Corridor", L"Couloir Fantasmagorique", L"Corridoio Fantasmagorico", L"Corredor Fantasmagorico", L"?? ??", L"梦幻回廊", L"??? ??????", L"Фантасмагорический Коридор", L"Phantasmagorischer Korridor", L"Corredor Fantasmagorico", L"Fantasmagorische Gang", L"Fantasmagoryczny Korytarz", L"Fantazmagori Koridoru");
			break;
		case 8315:
			a = LL14(L"幻煌", L"Phantom Radiance", L"Eclat Fantome", L"Splendore Fantasma", L"Resplandor Fantasma", L"??", L"幻煌", L"???? ??????", L"Призрачное Сияние", L"Phantomglanz", L"Resplendor Fantasma", L"Fantoomglinstering", L"Blask Widma", L"Hayalet I??lt?");
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
			a = LL14(L"巨イナルチカラ", L"Colossal Power", L"Puissance Colossale", L"Potere Colossale", L"Poder Colosal", L"??? ?", L"巨大的力量", L"??? ?????", L"Колоссальная Сила", L"Kolossale Kraft", L"Poder Colossal", L"Kolossale Kracht", L"Kolosalna Si?a", L"Devasa Guc");
			break;
		case 8409:
			a = L"The Decisive Collision";
			break;
		case 8410:
			a = LL14(L"この手で道を切り拓く!", L"Carve Our Path with These Hands!", L"Tracons Notre Chemin de Ces Mains!", L"Tracciamo il Nostro Cammino con Queste Mani!", L"!Abramos Nuestro Camino con Estas Manos!", L"? ??? ?? ????!", L"用?双手?辟道路!", L"????? ??? ?????? ?????? ??????!", L"Проложим Путь Этими Руками!", L"Mit diesen Handen unseren Weg bahnen!", L"Abrir Nosso Caminho com Estas Maos!", L"Ons Pad Banen met Deze Handen!", L"Torujemy Drog? Tymi R?kami!", L"Bu Ellerle Yolumuzu Acal?m!");
			break;
		case 8411:
			a = LL14(L"赤点です...", L"Failed...", L"Echec...", L"Fallito...", L"Reprobado...", L"??????...", L"挂科了...", L"????...", L"Провалено...", L"Durchgefallen...", L"Reprovado...", L"Gezakt...", L"Oblany...", L"Ba?ar?s?z...");
			break;
		case 8412:
			a = L"Unknown Threat";
			break;
		case 8413:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8420:
			a = L"Heated Mind";
			break;
		case 8421:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
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
			a = LL14(L"輝ける明日へ", L"Toward a Shining Tomorrow", L"Vers un Lendemain Radieux", L"Verso un Domani Splendente", L"Hacia un Manana Brillante", L"??? ???", L"走向光?的明天", L"??? ??? ????", L"К Сияющему Завтра", L"Einem Strahlenden Morgen Entgegen", L"Rumo a um Amanha Brilhante", L"Naar een Stralende Toekomst", L"Ku Ja?niej?cemu Jutru", L"Parlayan Bir Yar?na Do?ru");
			break;
		case 8435:
			a = LL14(L"迫る巨影", L"Approaching Giant Shadow", L"Ombre Geante qui Approche", L"Ombra Gigante che si Avvicina", L"Sombra Gigante que se Acerca", L"???? ??? ???", L"逼近的巨影", L"???? ??????? ???????", L"Приближающаяся Гигантская Тень", L"Nahende Riesenschatten", L"Sombra Gigante se Aproximando", L"Naderende Reusachtige Schaduw", L"Zbli?aj?cy si? Ogromny Cie?", L"Yakla?an Dev Golge");
			break;
		case 8441:
			a = L"E.O.V";
			break;
		case 8442:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8500:
			a = L"Strain";
			break;
		case 8501:
			a = LL14(L"夜のひととき", L"Nighttime", L"Un Moment Nocturne", L"Momento Notturno", L"Un Momento Nocturno", L"?? ??", L"夜?的片刻", L"???? ?????", L"Ночное Время", L"Nachtzeit", L"Um Momento Noturno", L"Nachtelijk Moment", L"Nocna Chwila", L"Gece Vakti");
			break;
		case 8502:
			a = LL14(L"トラブル発生", L"Trouble", L"Probleme Survenu", L"Problema Sorto", L"Problema Surgido", L"??? ??", L"麻??生", L"???? ?????", L"Возникли Неприятности", L"Arger", L"Problema Surgido", L"Probleem Opgetreden", L"K?opoty", L"Sorun C?kt?");
			break;
		case 8503:
			a = LL14(L"鉄路遥々", L"Distant Iron Road", L"Longue Route de Fer", L"Lunga Strada di Ferro", L"Larga Ruta de Hierro", L"??? ??", L"遥?的?路", L"???? ?????? ??????", L"Далёкий Железный Путь", L"Weite Eisenstrase", L"Longa Estrada de Ferro", L"Verre IJzeren Weg", L"Daleka ?elazna Droga", L"Uzak Demir Yolu");
			break;
		case 8504:
			a = LL14(L"旅愁", L"Travel Melancholy", L"Melancolie du Voyage", L"Malinconia del Viaggio", L"Melancolia del Viaje", L"??", L"旅途?愁", L"??? ?????", L"Дорожная Меланхолия", L"Reisemelancholie", L"Melancolia da Viagem", L"Reismelancholie", L"Melancholia Podro?y", L"Yolculuk Huznu");
			break;
		case 8505:
			a = LL14(L"皇城にて", L"At the Imperial Castle", L"Au Chateau Imperial", L"Al Castello Imperiale", L"En el Castillo Imperial", L"????", L"在皇城", L"?? ?????? ????????????", L"В Императорском Замке", L"Im Kaiserlichen Schloss", L"No Castelo Imperial", L"In het Keizerlijke Kasteel", L"W Cesarskim Zamku", L"?mparatorluk Kalesinde");
			break;
		case 8506:
			a = L"Let's Study";
			break;
		case 8507:
			a = LL14(L"知恵を絞って", L"Rack Your Brains", L"Se Creuser la Tete", L"Sforzarsi di Pensare", L"Exprimirse el Cerebro", L"??? ???", L"?尽?汁", L"???? ??????", L"Напрячь Мозги", L"Den Kopf Zerbrechen", L"Quebrar a Cabeca", L"Hersens Pijnigen", L"Wyt??y? Umys?", L"Beyin F?rt?nas?");
			break;
		case 8508:
			a = LL14(L"実技教練", L"Combat Training", L"Entrainement au Combat", L"Addestramento al Combattimento", L"Entrenamiento de Combate", L"?? ??", L"????", L"????? ?????", L"Боевая Тренировка", L"Kampftraining", L"Treinamento de Combate", L"Gevechtsdraining", L"Trening Bojowy", L"Muharebe E?itimi");
			break;
		case 8509:
			a = LL14(L"寮に帰ろう", L"Back to the Dorm", L"Retour au Dortoir", L"Ritorno al Dormitorio", L"De Vuelta al Dormitorio", L"???? ????", L"回宿舍去?", L"?????? ??? ?????", L"Обратно в Общежитие", L"Zuruck zum Wohnheim", L"De Volta ao Dormitorio", L"Terug naar het Dorm", L"Z Powrotem do Akademika", L"Yurda Donelim");
			break;
		case 8510:
			a = LL14(L"アーベントタイム", L"Evening Time", L"Soiree", L"Ora della Sera", L"Hora de la Tarde", L"?? ??", L"黄昏?光", L"??? ??????", L"Вечернее Время", L"Abendzeit", L"Hora da Tarde", L"Avondtijd", L"Czas Wieczoru", L"Ak?am Vakti");
			break;
		case 8512:
			a = LL14(L"鉄の統率", L"Iron Command", L"Commandement de Fer", L"Comando di Ferro", L"Mando de Hierro", L"?? ??", L"?的?率", L"??????? ????????", L"Железное Командование", L"Eiserner Befehl", L"Comando de Ferro", L"IJzeren Bevel", L"?elazne Dowodzenie", L"Demir Komuta");
			break;
		case 8513:
			a = LL14(L"暗躍", L"Moving in the Shadows", L"Agissant dans l'Ombre", L"Agendo nell'Ombra", L"Actuando en las Sombras", L"??", L"暗中活?", L"?????? ?? ??????", L"Действия в Тени", L"Im Verborgenen Agieren", L"Movendo-se nas Sombras", L"In het Donker Bewegen", L"Dzia?anie w Cieniu", L"Golgede Hareket");
			break;
		case 8514:
			a = LL14(L"想いの行き先", L"Where Feelings Lead", L"La ou Menent les Sentiments", L"Dove Portano i Sentimenti", L"Adonde Llevan los Sentimientos", L"??? ?? ?", L"心意所去之?", L"??? ???? ???????", L"Куда Ведут Чувства", L"Wohin Gefuhle Fuhren", L"Para Onde os Sentimentos Levam", L"Waar Gevoelens Naartoe Leiden", L"Dok?d Prowadz? Uczucia", L"Duygular?n Goturdu?u Yer");
			break;
		case 8515:
			a = LL14(L"傷心", L"Heartbreak", L"C?ur Brise", L"Cuore Spezzato", L"Corazon Roto", L"??", L"?心", L"??? ?????", L"Разбитое Сердце", L"Herzschmerz", L"Coracao Partido", L"Hartenpijn", L"Z?amane Serce", L"K?r?k Kalp");
			break;
		case 8516:
			a = LL14(L"揺らめく炎を見つめて", L"Watching the Flickering Flames", L"Regarder les Flammes Vacillantes", L"Guardare le Fiamme Tremolanti", L"Mirando las Llamas Parpadeantes", L"???? ??? ????", L"凝?着?曳的火?", L"???? ?? ????? ????????", L"Глядя на Мерцающее Пламя", L"Die Flackernden Flammen Beobachten", L"Observando as Chamas Oscilantes", L"De Flakkerende Vlammen Bekijken", L"Wpatruj?c si? w Migocz?ce P?omienie", L"Titreyen Alevlere Bakarken");
			break;
		case 8517:
			a = LL14(L"一途な気持ち", L"Single-minded Feelings", L"Sentiments Sinceres", L"Sentimenti Sinceri", L"Sentimientos Sinceros", L"???? ??", L"一心一意的心情", L"????? ?????", L"Искренние Чувства", L"Aufrichtige Gefuhle", L"Sentimentos Sinceros", L"Oprechte Gevoelens", L"Szczere Uczucia", L"Tek Yonlu Duygular");
			break;
		case 8520:
			a = LL14(L"臨戦態勢", L"Combat Ready", L"Pret au Combat", L"Pronto al Combattimento", L"Listo para el Combate", L"?? ??", L"??状?", L"????????? ???????", L"Боевая Готовность", L"Kampfbereit", L"Pronto para o Combate", L"Gevechtsklaaar", L"Gotowo?? Bojowa", L"Muharebe Haz?rl???");
			break;
		case 8521:
			a = L"Seriousness";
			break;
		case 8522:
			a = LL14(L"静かなる昂揚", L"Quiet Exhilaration", L"Exaltation Silencieuse", L"Esaltazione Silenziosa", L"Exaltacion Silenciosa", L"??? 昂揚", L"静静的昂?", L"?????? ????", L"Тихое Воодушевление", L"Stille Begeisterung", L"Exaltacao Silenciosa", L"Stille Opwinding", L"Cicha Ekscytacja", L"Sessiz Co?ku");
			break;
		case 8523:
			a = LL14(L"暖かな夕餉", L"Warm Dinner", L"Diner Chaleureux", L"Cena Calda", L"Cena Calida", L"??? ?? ??", L"温暖的?餐", L"???? ????", L"Тёплый Ужин", L"Warmes Abendessen", L"Jantar Caloroso", L"Warm Avondeten", L"Ciep?a Kolacja", L"S?cak Ak?am Yeme?i");
			break;
		case 8524:
			a = L"Atrocious Raid";
			break;
		case 8525:
			a = LL14(L"全てを賭して今、ここに立つ", L"Standing Here, Betting Everything", L"Debout Ici, Tout Misant", L"In Piedi Qui, Scommettendo Tutto", L"De Pie Aqui, Apostandolo Todo", L"?? ?? ?? ??, ??? ??", L"押上一切，此刻站在?里", L"?????? ??? ??????? ??? ???", L"Стоя Здесь, Ставя Всё на Кон", L"Hier Stehend, Alles Einsetzend", L"Aqui de Pe, Apostando Tudo", L"Hier Staand, Alles Inzettend", L"Stoj?c tu, Stawiaj?c Wszystko na Szali", L"Burada Durarak Her ?eyi Bahse Girerek");
			break;
		case 8527:
			a = LL14(L"新しい仲間たち", L"New Comrades", L"Nouveaux Camarades", L"Nuovi Compagni", L"Nuevos Companeros", L"??? ???", L"新的?伴?", L"???? ???", L"Новые Товарищи", L"Neue Kameraden", L"Novos Camaradas", L"Nieuwe Kameraden", L"Nowi Towarzysze", L"Yeni Yolda?lar");
			break;
		case 8528:
			a = LL14(L"不透明な事態", L"Opaque Situation", L"Situation Opaque", L"Situazione Opaca", L"Situacion Opaca", L"???? ??", L"不透明的局?", L"??? ????", L"Непрозрачная Ситуация", L"Undurchsichtige Lage", L"Situacao Opaca", L"Ondoorzichtige Situatie", L"Niejasna Sytuacja", L"Belirsiz Durum");
			break;
		case 8529:
			a = LL14(L"鉄血へのレクイエム", L"Requiem for Iron and Blood", L"Requiem pour le Fer et le Sang", L"Requiem per il Ferro e il Sangue", L"Requiem por el Hierro y la Sangre", L"??? ?? ???", L"献??血的安魂曲", L"?????? ?????? ?????", L"Реквием по Железу и Крови", L"Requiem fur Eisen und Blut", L"Requiem pelo Ferro e pelo Sangue", L"Requiem voor IJzer en Bloed", L"Requiem dla ?elaza i Krwi", L"Demir ve Kan ?cin Requiem");
			break;
		case 8530:
			a = LL14(L"幻想の唄 -PHANTASMAGORIA-", L"Phantom Song -PHANTASMAGORIA-", L"Chant Fantome -PHANTASMAGORIA-", L"Canto Fantasma -PHANTASMAGORIA-", L"Cancion Fantasma -PHANTASMAGORIA-", L"??? ?? -PHANTASMAGORIA-", L"幻想之歌 -PHANTASMAGORIA-", L"????? ?????? -PHANTASMAGORIA-", L"Призрачная Песня -PHANTASMAGORIA-", L"Phantomgesang -PHANTASMAGORIA-", L"Cancao Fantasma -PHANTASMAGORIA-", L"Spooklied -PHANTASMAGORIA-", L"Pie?? Widmo -PHANTASMAGORIA-", L"Hayalet ?ark? -PHANTASMAGORIA-");
			break;
		case 8531:
			a = LL14(L"刻ハ至レリ", L"The Hour Has Come", L"L'Heure est Venue", L"L'Ora e Giunta", L"La Hora Ha Llegado", L"?? ?????", L"?刻已至", L"??? ???? ??????", L"Час Настал", L"Die Stunde ist Gekommen", L"A Hora Chegou", L"Het Uur is Gekomen", L"Godzina Nadesz?a", L"Saat Geldi");
			break;
		case 8532:
			a = LL14(L"目覚めし伝承", L"Awakening Legend", L"Legende Eveillee", L"Leggenda Risvegliata", L"Leyenda Despertada", L"?? ??", L"?醒的?承", L"???????? ?????????", L"Пробуждённая Легенда", L"Erwachende Legende", L"Lenda Despertada", L"Ontwakende Legende", L"Przebudzona Legenda", L"Uyanan Efsane");
			break;
		case 8533:
			a = LL14(L"唯一の希望", L"Only Hope", L"Seul Espoir", L"Unica Speranza", L"Unica Esperanza", L"??? ??", L"唯一的希望", L"????? ??????", L"Единственная Надежда", L"Einzige Hoffnung", L"Unica Esperanca", L"Enige Hoop", L"Jedyna Nadzieja", L"Tek Umut");
			break;
		case 8535:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8537:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8538:
			a = LL14(L"今はまだ...", L"Not Yet...", L"Pas Encore...", L"Non Ancora...", L"Todavia No...", L"??? ??...", L"?在?不行...", L"??? ???...", L"Ещё Нет...", L"Noch Nicht...", L"Ainda Nao...", L"Nog Niet...", L"Jeszcze Nie...", L"Henuz De?il...");
			break;
		case 8539:
			a = LL14(L"あの日に見た夜空", L"The Night Sky I Saw That Day", L"Le Ciel Nocturne que j'ai Vu Ce Jour-la", L"Il Cielo Notturno che Vidi Quel Giorno", L"El Cielo Nocturno que Vi Ese Dia", L"?? ? ???", L"那天看?的夜空", L"???? ????? ???? ?????? ??? ?????", L"Ночное Небо, Которое Я Видел В Тот День", L"Der Nachthimmel, den Ich Damals Sah", L"O Ceu Noturno que Vi Naquele Dia", L"De Nachtelijke Hemel die ik Die Dag Zag", L"Nocne Niebo, ktore Widzia?em Tamtego Dnia", L"O Gun Gordu?um Gece Gokyuzu");
			break;
		case 8540:
			a = LL14(L"偽りの時間", L"False Time", L"Temps Fictif", L"Tempo Falso", L"Tiempo Falso", L"??? ??", L"虚假的??", L"????? ??????", L"Ложное Время", L"Falsche Zeit", L"Tempo Falso", L"Valse Tijd", L"Fa?szywy Czas", L"Sahte Zaman");
			break;
		case 8541:
			a = LL14(L"紅き翼 -新たなる風-", L"Crimson Wings -New Wind-", L"Ailes Cramoisies -Nouveau Vent-", L"Ali Cremisi -Nuovo Vento-", L"Alas Carmesi -Nuevo Viento-", L"?? ?? -??? ??-", L"??之翼 -新?-", L"??????? ???????? -??? ?????-", L"Багровые Крылья -Новый Ветер-", L"Karmesinrote Flugel -Neuer Wind-", L"Asas Carmesim -Novo Vento-", L"Karmozijnrode Vleugels -Nieuwe Wind-", L"Karmazynowe Skrzyd?a -Nowy Wiatr-", L"K?rm?z? Kanatlar -Yeni Ruzgar-");
			break;
		case 8550:
			a = LL14(L"再会", L"Reunion", L"Retrouvailles", L"Riunione", L"Reencuentro", L"??", L"重逢", L"???? ????", L"Воссоединение", L"Wiedersehen", L"Reencontro", L"Hereniging", L"Ponowne Spotkanie", L"Yeniden Bulu?ma");
			break;
		case 8551:
			a = LL14(L"かけがえのない人へ", L"To Someone Irreplaceable", L"A Quelqu'un d'Irremplacable", L"A Qualcuno di Insostituibile", L"A Alguien Insustituible", L"??? ????", L"致无可替代之人", L"??? ??? ?? ???????", L"Незаменимому Человеку", L"An Jemanden Unersetzlichen", L"A Alguem Insubstituivel", L"Aan Iemand Onvervangbaar", L"Do Kogo? Niezast?pionego", L"Vazgecilmez Birine");
			break;
		case 8552:
			a = LL14(L"惜しむように、愛おしむように", L"Cherishing, Treasuring", L"Cherissant, Precieux", L"Custodendo, Tesaurizzando", L"Atesorando, Valorando", L"?????, ??? ???", L"如同珍惜，如同??", L"??????? ??????", L"Дорожа, Храня", L"Kosten, Schatzen", L"Prezando, Valorizando", L"Koesterend, Waarderend", L"Ceni?c, Piel?gnuj?c", L"De?er Vererek, Sevgiyle");
			break;
		case 8553:
			a = LL14(L"ライノの花が咲く頃", L"When the Rhino Flower Blooms", L"Quand la Fleur de Rhino s'Epanouit", L"Quando il Fiore di Rhino Sboccia", L"Cuando Florece la Flor de Rhino", L"??? ?? ? ??", L"莱?花盛?之?", L"??? ????? ???? ????", L"Когда Цветёт Цветок Райно", L"Wenn die Rhino-Blume Bluht", L"Quando a Flor de Rhino Desabrocha", L"Als de Rhino Bloem Bloeit", L"Gdy Kwitnie Kwiat Rhino", L"Rhino Cice?i Act???nda");
			break;
		case 8555:
			a = LL14(L"戦場の掟", L"Rules of Battlefield", L"Regles du Champ de Bataille", L"Regole del Campo di Battaglia", L"Reglas del Campo de Batalla", L"??? ??", L"??的??", L"????? ???? ???????", L"Правила Поля Боя", L"Regeln des Schlachtfeldes", L"Regras do Campo de Batalha", L"Regels van het Slagveld", L"Zasady Pola Bitwy", L"Sava? Alan?n?n Kurallar?");
			break;
		case 8556:
			a = L"Remaining Glow";
			break;
		case 8557:
			a = LL14(L"深淵の魔女", L"Witch of the Abyss", L"Sorciere de l'Abime", L"Strega dell'Abisso", L"Bruja del Abismo", L"??? ??", L"深渊的魔女", L"????? ???????", L"Ведьма Бездны", L"Hexe des Abgrunds", L"Bruxa do Abismo", L"Heks van de Afgrond", L"Wied?ma Otch?ani", L"Ucurumun Cad?s?");
			break;
		case 8558:
			a = L"ALTINA";
			break;
		case 8559:
			a = LL14(L"威風", L"Dignity", L"Dignite", L"Dignita", L"Dignidad", L"??", L"威?", L"????", L"Достоинство", L"Wurde", L"Dignidade", L"Waardigheid", L"Godno??", L"Haysiyet");
			break;
		case 8560:
			a = LL14(L"一撃に賭ける", L"Bet on One Strike", L"Miser sur un Seul Coup", L"Scommettere su un Solo Colpo", L"Apostar por un Solo Golpe", L"??? ??", L"?在一?", L"???????? ??? ???? ?????", L"Ставить на Один Удар", L"Auf einen Schlag Setzen", L"Apostar em um Unico Golpe", L"Alles op Een Slag Zetten", L"Postawi? na Jeden Cios", L"Tek Darbeye Bahse Girmek");
			break;
		case 8561:
			a = LL14(L"ユミル渓谷道", L"Ymir Valley Road", L"Route de la Vallee de Ymir", L"Strada della Valle di Ymir", L"Camino del Valle de Ymir", L"??? ???", L"尤弥?谷道", L"???? ???? ????", L"Дорога Долины Имир", L"Ymir-Talstrase", L"Estrada do Vale de Ymir", L"Ymir Valleiroute", L"Droga Doliny Ymir", L"Ymir Vadi Yolu");
			break;
		case 8562:
			a = L"Awakening";
			break;
		case 8563:
			a = L"Blitzkrieg";
			break;
		case 8564:
			a = LL14(L"魔王の凱歌", L"Demon Lord's Triumph", L"Triomphe du Seigneur Demon", L"Trionfo del Signore dei Demoni", L"Triunfo del Senor Demonio", L"??? ??", L"魔王的?歌", L"???? ?????? ??? ????????", L"Триумф Повелителя Демонов", L"Triumph des Damonenkonigs", L"Triunfo do Senhor dos Demonios", L"Triomf van de Demonenkoning", L"Triumf W?adcy Demonow", L"?eytan Lordu'nun Zaferi");
			break;
		case 8566:
			a = LL14(L"内なる黄昏", L"Inner Twilight", L"Crepuscule Interieur", L"Crepuscolo Interiore", L"Crepusculo Interior", L"??? ??", L"内心的黄昏", L"????? ???????", L"Внутренние Сумерки", L"Innere Dammerung", L"Crepusculo Interior", L"Innerlijke Schemering", L"Wewn?trzny Zmierzch", L"?c Alacakaranl?k");
			break;
		case 8567:
			a = LL14(L"蘇る記憶", L"Awakened Memories", L"Souvenirs Ressuscites", L"Ricordi Risvegliati", L"Recuerdos Resucitados", L"????? ??", L"?醒的??", L"?????? ??????", L"Пробуждённые Воспоминания", L"Erwachte Erinnerungen", L"Memorias Despертadas", L"Ontwakende Herinneringen", L"Przebudzone Wspomnienia", L"Uyanan An?lar");
			break;
		case 8570:
			a = LL14(L"静かな決意", L"Quiet Resolution", L"Resolution Silencieuse", L"Risoluzione Silenziosa", L"Resolucion Silenciosa", L"??? ??", L"静静的决意", L"??? ????", L"Тихая Решимость", L"Stille Entschlossenheit", L"Resolucao Silenciosa", L"Stille Vastberadenheid", L"Cicha Determinacja", L"Sessiz Kararl?l?k");
			break;
		case 8571:
			a = LL14(L"乾坤一擲", L"All or Nothing", L"Tout ou Rien", L"Tutto o Niente", L"Todo o Nada", L"????", L"孤注一?", L"?? ??? ?? ?? ???", L"Всё или Ничего", L"Alles oder Nichts", L"Tudo ou Nada", L"Alles of Niets", L"Wszystko albo Nic", L"Ya Hep Ya Hic");
			break;
		case 8572:
			a = LL14(L"交戦", L"Combat", L"Combat", L"Combattimento", L"Combate", L"??", L"交?", L"??????", L"Бой", L"Kampf", L"Combate", L"Gevecht", L"Walka", L"Muharebe");
			break;
		case 8573:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8600:
			a = LL14(L"大市の賑わい", L"Bustling Market", L"Marche Anime", L"Mercato Vivace", L"Mercado Animado", L"? ??? ??", L"大市?的??", L"??? ?????", L"Оживлённый Рынок", L"Belebter Markt", L"Mercado Movimentado", L"Drukke Markt", L"T?tni?cy ?yciem Rynek", L"Kalabal?k Pazar");
			break;
		case 8601:
			a = LL14(L"剣の遊戯", L"Sword Play", L"Jeu d'Epee", L"Gioco di Spada", L"Juego de Espada", L"?? ??", L"?的游?", L"???? ?????", L"Игра на Мечах", L"Schwertkampfspiel", L"Jogo de Espada", L"Zwaardspel", L"Gra na Miecze", L"K?l?c Oyunu");
			break;
		case 8602:
			a = LL14(L"紙一重の攻防", L"Close Fight", L"Combat Serre", L"Combattimento Serrato", L"Combate Renido", L"?? ? ? ??? ??", L"?之一?的攻防", L"???? ??????", L"Напряжённый Бой", L"Knappes Gefecht", L"Luta Apertada", L"Nipt Gevecht", L"Zaci?ta Walka", L"Ceki?meli Dovu?");
			break;
		case 8603:
			a = LL14(L"走れマッハ号!", L"Run Mach Train!", L"En Avant Mach Train!", L"Corri Treno Mach!", L"!Corre Tren Mach!", L"??? ???!", L"快??赫号！", L"???? ?????? ????????? ????!", L"Беги, Поезд Мах!", L"Lauf, Mach-Zug!", L"Corra Trem Mach!", L"Ren Mach Trein!", L"Biegnij Poci?gu Mach!", L"Ko? Mach Treni!");
			break;
		case 8605:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8606:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8607:
			a = LL14(L"星屑のカンタータ", L"Cantata of Stardust", L"Cantate de Poussiere d'Etoiles", L"Cantata di Polvere di Stelle", L"Cantata de Polvo de Estrellas", L"???? ???", L"星?康塔塔", L"??????? ???? ??????", L"Кантата Звёздной Пыли", L"Kantate des Sternenstaubs", L"Cantata de Poeira Estelar", L"Cantate van Sterrenstof", L"Kantata Gwiazdowego Py?u", L"Y?ld?z Tozu Kantas?");
			break;
		case 8608:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8609:
			a = L"Sonata No.45";
			break;
		case 8610:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8620:
			a = LL14(L"雪ウサギを追いかけて", L"Chasing the Snow Rabbit", L"Chasser le Lapin des Neiges", L"Inseguire il Coniglio della Neve", L"Persiguiendo al Conejo de Nieve", L"???? ???", L"追逐雪兔", L"?????? ???? ?????", L"Погоня за Снежным Кроликом", L"Das Schneekaninchen Jagen", L"Perseguindo o Coelho da Neve", L"Het Sneeuwkonijn Najagen", L"Goni?c ?nie?nego Krolika", L"Kar Tav?an?n? Kovalayarak");
			break;
		case 8621:
			a = L"Take The Windward!";
			break;
		case 8622:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8623:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8624:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8625:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8627:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音効", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8628:
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmiyor");
			break;
		case 8629:
			a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音効", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses efekti");
			break;
		case 8700:
			a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			break;
		case 8703:
			a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			break;
		case 8704:
			a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			break;
		case 8710:
			a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			break;
		case 8711:
			a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
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
		//Ys X
		if (a == L"y_act_e002.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_act_e002_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b100.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b100_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b200.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b200_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b210.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b210_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b300.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b300_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b400.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b400_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b500.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b500_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b510.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b510_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b520.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b520_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b610.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b610_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b620.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b620_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b700.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b700_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b710.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b710_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b720.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_b720_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_bgm_none.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d100.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d100_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d200.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d200_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d400.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d400_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d410.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d410_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d500.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d500_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d600.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d600_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d710.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d710_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d800.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d800_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d900.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d900_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d1010.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_d1010_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e001.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e002.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e003.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e004.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e005.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e006.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e007.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e007_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e008.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e009.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e010.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e011.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e011_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e012.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e013.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e014.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e015.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_e015_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f100.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f100_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f110.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f110_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f120.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f120_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f130.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f130_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f140.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f140_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f150.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f150_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f160.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f160_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f200.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f200_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f210.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f210_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f220.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f220_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f230.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f230_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f310.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_f310_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_gameover.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_op.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_op_lp.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t100.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t100_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t200.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t200_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t300.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t300_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t301.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t301_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t500.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t500_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t600.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_t600_s1.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}
		if (a == L"y_title.opus") {
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音?", L"??????", L"Музыка", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
			fff = 1;
		}

		CString ft = filen.Right(filen.GetLength() - filen.ReverseFind(L'\\') - 1);
		//Ys X
		if (ft == L"y_act_e002.opus") {
			a = L"Operation SANDRAS";
			fff = 1;
		}
		if (ft == L"y_act_e002_s1.opus") {
			a = LL14(L"Operation SANDRAS(重低音)", L"Operation SANDRAS (Bass Boost)", L"Operation SANDRAS (Renfort graves)", L"Operation SANDRAS (Rinforzo bassi)", L"Operation SANDRAS (Refuerzo graves)", L"Operation SANDRAS (?? ??)", L"Operation SANDRAS (重低音)", L"Operation SANDRAS (????? ??????)", L"Operation SANDRAS (Усиление низких)", L"Operation SANDRAS (Bassverstarkung)", L"Operation SANDRAS (Reforco graves)", L"Operation SANDRAS (Basversterking)", L"Operation SANDRAS (Wzmocnienie basow)", L"Operation SANDRAS (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b100.opus") {
			a = L"Overblaze";
			fff = 1;
		}
		if (ft == L"y_b100_s1.opus") {
			a = LL14(L"Overblaze(重低音)", L"Overblaze (Bass Boost)", L"Overblaze (Renfort graves)", L"Overblaze (Rinforzo bassi)", L"Overblaze (Refuerzo graves)", L"Overblaze (?? ??)", L"Overblaze (重低音)", L"Overblaze (????? ??????)", L"Overblaze (Усиление низких)", L"Overblaze (Bassverstarkung)", L"Overblaze (Reforco graves)", L"Overblaze (Basversterking)", L"Overblaze (Wzmocnienie basow)", L"Overblaze (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b200.opus") {
			a = L"Through the North Wind";
			fff = 1;
		}
		if (ft == L"y_b200_s1.opus") {
			a = LL14(L"Through the North Wind(重低音)", L"Through the North Wind (Bass Boost)", L"Through the North Wind (Renfort graves)", L"Through the North Wind (Rinforzo bassi)", L"Through the North Wind (Refuerzo graves)", L"Through the North Wind (?? ??)", L"Through the North Wind (重低音)", L"Through the North Wind (????? ??????)", L"Through the North Wind (Усиление низких)", L"Through the North Wind (Bassverstarkung)", L"Through the North Wind (Reforco graves)", L"Through the North Wind (Basversterking)", L"Through the North Wind (Wzmocnienie basow)", L"Through the North Wind (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b210.opus") {
			a = LL14(L"高鳴る鼓動", L"Pounding Heartbeat", L"Battement de c?ur saccade", L"Battito accelerato", L"Latido palpitante", L"???? ??", L"劇烈的心跳", L"????? ????? ?????????", L"Учащенное сердцебиение", L"Pochendes Herzklopfen", L"Batida forte do coracao", L"Bonzend hart", L"?omocz?ce serce", L"Kut Kut Atan Kalp");
			fff = 1;
		}
		if (ft == L"y_b210_s1.opus") {
			a = LL14(L"高鳴る鼓動(重低音)", L"Pounding Heartbeat (Bass Boost)", L"Pounding Heartbeat (Renfort graves)", L"Pounding Heartbeat (Rinforzo bassi)", L"Pounding Heartbeat (Refuerzo graves)", L"Pounding Heartbeat (?? ??)", L"Pounding Heartbeat (重低音)", L"Pounding Heartbeat (????? ??????)", L"Pounding Heartbeat (Усиление низких)", L"Pounding Heartbeat (Bassverstarkung)", L"Pounding Heartbeat (Reforco graves)", L"Pounding Heartbeat (Basversterking)", L"Pounding Heartbeat (Wzmocnienie basow)", L"Pounding Heartbeat (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b300.opus") {
			a = LL14(L"石火の如く", L"Like Flint", L"Comme le silex", L"Come la selce", L"Como el silex", L"?????", L"如同火石", L"??? ??????", L"Словно кремень", L"Wie Feuerstein", L"Como pederneira", L"Als vuursteen", L"Jak krzemie?", L"Cakmak Ta?? Gibi");
			fff = 1;
		}
		if (ft == L"y_b300_s1.opus") {
			a = LL14(L"石火の如く(重低音)", L"Like Flint (Bass Boost)", L"Like Flint (Renfort graves)", L"Like Flint (Rinforzo bassi)", L"Like Flint (Refuerzo graves)", L"Like Flint (?? ??)", L"Like Flint (重低音)", L"Like Flint (????? ??????)", L"Like Flint (Усиление низких)", L"Like Flint (Bassverstarkung)", L"Like Flint (Reforco graves)", L"Like Flint (Basversterking)", L"Like Flint (Wzmocnienie basow)", L"Like Flint (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b400.opus") {
			a = L"Can You Do It";
			fff = 1;
		}
		if (ft == L"y_b400_s1.opus") {
			a = LL14(L"Can You Do It(重低音)", L"Can You Do It (Bass Boost)", L"Can You Do It (Renfort graves)", L"Can You Do It (Rinforzo bassi)", L"Can You Do It (Refuerzo graves)", L"Can You Do It (?? ??)", L"Can You Do It (重低音)", L"Can You Do It (????? ??????)", L"Can You Do It (Усиление низких)", L"Can You Do It (Bassverstarkung)", L"Can You Do It (Reforco graves)", L"Can You Do It (Basversterking)", L"Can You Do It (Wzmocnienie basow)", L"Can You Do It (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b500.opus") {
			a = LL14(L"BERSERK -戦斧の咆哮-", L"BERSERK -Roar of the Battle Axe-", L"BERSERK -Rugissement de la hache de guerre-", L"BERSERK -Ruggito dell'ascia da battaglia-", L"BERSERK -Rugido del hacha de batalla-", L"BERSERK -??? ??-", L"BERSERK -戰斧的咆哮-", L"BERSERK - ???? ??? ???????", L"BERSERK -Рев боевого топора-", L"BERSERK -Brullen der Streitaxt-", L"BERSERK -Rugido do machado de batalha-", L"BERSERK -Geknal van de strijdbijl-", L"BERSERK -Ryk topora wojennego-", L"BERSERK -Sava? Baltas?n?n Kukreyi?i-");
			fff = 1;
		}
		if (ft == L"y_b500_s1.opus") {
			a = LL14(L"BERSERK -戦斧の咆哮-(重低音)", L"BERSERK -Roar of the Battle Axe- (Bass Boost)", L"BERSERK -Roar of the Battle Axe- (Renfort graves)", L"BERSERK -Roar of the Battle Axe- (Rinforzo bassi)", L"BERSERK -Roar of the Battle Axe- (Refuerzo graves)", L"BERSERK -Roar of the Battle Axe- (?? ??)", L"BERSERK -Roar of the Battle Axe- (重低音)", L"BERSERK -Roar of the Battle Axe- (????? ??????)", L"BERSERK -Roar of the Battle Axe- (Усиление низких)", L"BERSERK -Roar of the Battle Axe- (Bassverstarkung)", L"BERSERK -Roar of the Battle Axe- (Reforco graves)", L"BERSERK -Roar of the Battle Axe- (Basversterking)", L"BERSERK -Roar of the Battle Axe- (Wzmocnienie basow)", L"BERSERK -Roar of the Battle Axe- (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b510.opus") {
			a = LL14(L"悪意の洗礼", L"Baptism of Malice", L"Bapteme de malice", L"Battesimo di malizia", L"Bautismo de malicia", L"??? ??", L"惡意的洗禮", L"??????? ?????", L"Крещение злобой", L"Taufe der Bosheit", L"Batismo de malicia", L"Doop van kwaadaardigheid", L"Chrzest z?o?liwo?ci", L"Garez Vaftizi");
			fff = 1;
		}
		if (ft == L"y_b510_s1.opus") {
			a = LL14(L"悪意の洗礼(重低音)", L"Baptism of Malice (Bass Boost)", L"Baptism of Malice (Renfort graves)", L"Baptism of Malice (Rinforzo bassi)", L"Baptism of Malice (Refuerzo graves)", L"Baptism of Malice (?? ??)", L"Baptism of Malice (重低音)", L"Baptism of Malice (????? ??????)", L"Baptism of Malice (Усиление низких)", L"Baptism of Malice (Bassverstarkung)", L"Baptism of Malice (Reforco graves)", L"Baptism of Malice (Basversterking)", L"Baptism of Malice (Wzmocnienie basow)", L"Baptism of Malice (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b520.opus") {
			a = L"The Ultimate Pleasure in My Hands";
			fff = 1;
		}
		if (ft == L"y_b520_s1.opus") {
			a = LL14(L"The Ultimate Pleasure in My Hands(重低音)", L"The Ultimate Pleasure in My Hands (Bass Boost)", L"The Ultimate Pleasure in My Hands (Renfort graves)", L"The Ultimate Pleasure in My Hands (Rinforzo bassi)", L"The Ultimate Pleasure in My Hands (Refuerzo graves)", L"The Ultimate Pleasure in My Hands (?? ??)", L"The Ultimate Pleasure in My Hands (重低音)", L"The Ultimate Pleasure in My Hands (????? ??????)", L"The Ultimate Pleasure in My Hands (Усиление низких)", L"The Ultimate Pleasure in My Hands (Bassverstarkung)", L"The Ultimate Pleasure in My Hands (Reforco graves)", L"The Ultimate Pleasure in My Hands (Basversterking)", L"The Ultimate Pleasure in My Hands (Wzmocnienie basow)", L"The Ultimate Pleasure in My Hands (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b610.opus") {
			a = LL14(L"辿り着いた極光の下で", L"Under the Northern Lights", L"Sous les aurores boreales", L"Sotto l'aurora boreale", L"Bajo la aurora boreal", L"??? ?? ????", L"抵達極光之下", L"??? ????? ??????", L"Под северным сиянием", L"Unter dem Nordlicht", L"Sob a aurora boreal", L"Onder het noorderlicht", L"Pod zorz? polarn?", L"Kuzey I??klar? Alt?nda");
			fff = 1;
		}
		if (ft == L"y_b610_s1.opus") {
			a = LL14(L"辿り着いた極光の下で(重低音)", L"Under the Northern Lights (Bass Boost)", L"Under the Northern Lights (Renfort graves)", L"Under the Northern Lights (Rinforzo bassi)", L"Under the Northern Lights (Refuerzo graves)", L"Under the Northern Lights (?? ??)", L"Under the Northern Lights (重低音)", L"Under the Northern Lights (????? ??????)", L"Under the Northern Lights (Усиление низких)", L"Under the Northern Lights (Bassverstarkung)", L"Under the Northern Lights (Reforco graves)", L"Under the Northern Lights (Basversterking)", L"Under the Northern Lights (Wzmocnienie basow)", L"Under the Northern Lights (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b620.opus") {
			a = L"Nordics Saga -The Endless Bloody Sea-";
			fff = 1;
		}
		if (ft == L"y_b620_s1.opus") {
			a = LL14(L"Nordics Saga -The Endless Bloody Sea-(重低音)", L"Nordics Saga -The Endless Bloody Sea- (Bass Boost)", L"Nordics Saga -The Endless Bloody Sea- (Renfort graves)", L"Nordics Saga -The Endless Bloody Sea- (Rinforzo bassi)", L"Nordics Saga -The Endless Bloody Sea- (Refuerzo graves)", L"Nordics Saga -The Endless Bloody Sea- (?? ??)", L"Nordics Saga -The Endless Bloody Sea- (重低音)", L"Nordics Saga -The Endless Bloody Sea- (????? ??????)", L"Nordics Saga -The Endless Bloody Sea- (Усиление низких)", L"Nordics Saga -The Endless Bloody Sea- (Bassverstarkung)", L"Nordics Saga -The Endless Bloody Sea- (Reforco graves)", L"Nordics Saga -The Endless Bloody Sea- (Basversterking)", L"Nordics Saga -The Endless Bloody Sea- (Wzmocnienie basow)", L"Nordics Saga -The Endless Bloody Sea- (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b700.opus") {
			a = L"Ready to Fire!";
			fff = 1;
		}
		if (ft == L"y_b700_s1.opus") {
			a = LL14(L"Ready to Fire!(重低音)", L"Ready to Fire! (Bass Boost)", L"Ready to Fire! (Renfort graves)", L"Ready to Fire! (Rinforzo bassi)", L"Ready to Fire! (Refuerzo graves)", L"Ready to Fire! (?? ??)", L"Ready to Fire! (重低音)", L"Ready to Fire! (????? ??????)", L"Ready to Fire! (Усиление низких)", L"Ready to Fire! (Bassverstarkung)", L"Ready to Fire! (Reforco graves)", L"Ready to Fire! (Basversterking)", L"Ready to Fire! (Wzmocnienie basow)", L"Ready to Fire! (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b710.opus") {
			a = L"Hello, Those Who Can't Die";
			fff = 1;
		}
		if (ft == L"y_b710_s1.opus") {
			a = LL14(L"Hello, Those Who Can't Die(重低音)", L"Hello, Those Who Can't Die (Bass Boost)", L"Hello, Those Who Can't Die (Renfort graves)", L"Hello, Those Who Can't Die (Rinforzo bassi)", L"Hello, Those Who Can't Die (Refuerzo graves)", L"Hello, Those Who Can't Die (?? ??)", L"Hello, Those Who Can't Die (重低音)", L"Hello, Those Who Can't Die (????? ??????)", L"Hello, Those Who Can't Die (Усиление низких)", L"Hello, Those Who Can't Die (Bassverstarkung)", L"Hello, Those Who Can't Die (Reforco graves)", L"Hello, Those Who Can't Die (Basversterking)", L"Hello, Those Who Can't Die (Wzmocnienie basow)", L"Hello, Those Who Can't Die (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_b720.opus") {
			a = L"Landing Warfare";
			fff = 1;
		}
		if (ft == L"y_b720_s1.opus") {
			a = LL14(L"Landing Warfare(重低音)", L"Landing Warfare (Bass Boost)", L"Landing Warfare (Renfort graves)", L"Landing Warfare (Rinforzo bassi)", L"Landing Warfare (Refuerzo graves)", L"Landing Warfare (?? ??)", L"Landing Warfare (重低音)", L"Landing Warfare (????? ??????)", L"Landing Warfare (Усиление низких)", L"Landing Warfare (Bassverstarkung)", L"Landing Warfare (Reforco graves)", L"Landing Warfare (Basversterking)", L"Landing Warfare (Wzmocnienie basow)", L"Landing Warfare (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_bgm_none.opus") {
			a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"??", L"無音", L"???", L"Тишина", L"Stille", L"Silencio", L"Stilte", L"Cisza", L"Sessizlik");
			fff = 1;
		}
		if (ft == L"y_d100.opus") {
			a = LL14(L"光届かぬその奥に", L"In the Depths Where Light Doesn't Reach", L"Dans les profondeurs hors de portee de la lumiere", L"Nelle profondita dove non arriva la luce", L"En las profundidades donde no llega la luz", L"?? ?? ?? ? ?? ??", L"光線無法到達の深處", L"?? ??????? ??? ?? ??? ?????", L"В глубинах, куда не доходит свет", L"In den Tiefen, die kein Licht erreicht", L"Nas profundezas onde a luz nao chega", L"In de diepten waar geen licht komt", L"W g??binach, gdzie nie si?ga ?wiat?o", L"I????n Ula?amad??? Derinliklerde");
			fff = 1;
		}
		if (ft == L"y_d100_s1.opus") {
			a = LL14(L"光届かぬその奥に(重低音)", L"In the Depths Where Light Doesn't Reach (Bass Boost)", L"In the Depths Where Light Doesn't Reach (Renfort graves)", L"In the Depths Where Light Doesn't Reach (Rinforzo bassi)", L"In the Depths Where Light Doesn't Reach (Refuerzo graves)", L"In the Depths Where Light Doesn't Reach (?? ??)", L"In the Depths Where Light Doesn't Reach (重低音)", L"In the Depths Where Light Doesn't Reach (????? ??????)", L"In the Depths Where Light Doesn't Reach (Усиление низких)", L"In the Depths Where Light Doesn't Reach (Bassverstarkung)", L"In the Depths Where Light Doesn't Reach (Reforco graves)", L"In the Depths Where Light Doesn't Reach (Basversterking)", L"In the Depths Where Light Doesn't Reach (Wzmocnienie basow)", L"In the Depths Where Light Doesn't Reach (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_d200.opus") {
			a = L"Eerie Stillness";
			fff = 1;
		}
		if (ft == L"y_d200_s1.opus") {
			a = LL14(L"Eerie Stillness(重低音)", L"Eerie Stillness (Bass Boost)", L"Eerie Stillness (Renfort graves)", L"Eerie Stillness (Rinforzo bassi)", L"Eerie Stillness (Refuerzo graves)", L"Eerie Stillness (?? ??)", L"Eerie Stillness (重低音)", L"Eerie Stillness (????? ??????)", L"Eerie Stillness (Усиление низких)", L"Eerie Stillness (Bassverstarkung)", L"Eerie Stillness (Reforco graves)", L"Eerie Stillness (Basversterking)", L"Eerie Stillness (Wzmocnienie basow)", L"Eerie Stillness (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_d400.opus") {
			a = LL14(L"飽くなき渇望", L"Insatiable Thirst", L"Soif insatiable", L"Sete insaziabile", L"Sed insaciable", L"??? ??", L"永無止境的?望", L"??? ?? ?????", L"Ненасытная жажда", L"Unstillbares Verlangen", L"Sede insaciavel", L"Onverzadigbare dorst", L"Nienasycone pragnienie", L"Doymak Bilmez Susuzluk");
			fff = 1;
		}
		if (ft == L"y_d400_s1.opus") {
			a = LL14(L"飽くなき渇望(重低音)", L"Insatiable Thirst (Bass Boost)", L"Insatiable Thirst (Renfort graves)", L"Insatiable Thirst (Rinforzo bassi)", L"Insatiable Thirst (Refuerzo graves)", L"Insatiable Thirst (?? ??)", L"Insatiable Thirst (重低音)", L"Insatiable Thirst (????? ??????)", L"Insatiable Thirst (Усиление низких)", L"Insatiable Thirst (Bassverstarkung)", L"Insatiable Thirst (Reforco graves)", L"Insatiable Thirst (Basversterking)", L"Insatiable Thirst (Wzmocnienie basow)", L"Insatiable Thirst (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_d410.opus") {
			a = L"The Inner Darkness";
			fff = 1;
		}
		if (ft == L"y_d410_s1.opus") {
			a = LL14(L"The Inner Darkness(重低音)", L"The Inner Darkness (Bass Boost)", L"The Inner Darkness (Renfort graves)", L"The Inner Darkness (Rinforzo bassi)", L"The Inner Darkness (Refuerzo graves)", L"The Inner Darkness (?? ??)", L"The Inner Darkness (重低音)", L"The Inner Darkness (????? ??????)", L"The Inner Darkness (Усиление низких)", L"The Inner Darkness (Bassverstarkung)", L"The Inner Darkness (Reforco graves)", L"The Inner Darkness (Basversterking)", L"The Inner Darkness (Wzmocnienie basow)", L"The Inner Darkness (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_d500.opus") {
			a = L"Hardhearted Rock Line";
			fff = 1;
		}
		if (ft == L"y_d500_s1.opus") {
			a = LL14(L"Hardhearted Rock Line(重低音)", L"Hardhearted Rock Line (Bass Boost)", L"Hardhearted Rock Line (Renfort graves)", L"Hardhearted Rock Line (Rinforzo bassi)", L"Hardhearted Rock Line (Refuerzo graves)", L"Hardhearted Rock Line (?? ??)", L"Hardhearted Rock Line (重低音)", L"Hardhearted Rock Line (????? ??????)", L"Hardhearted Rock Line (Усиление низких)", L"Hardhearted Rock Line (Bassverstarkung)", L"Hardhearted Rock Line (Reforco graves)", L"Hardhearted Rock Line (Basversterking)", L"Hardhearted Rock Line (Wzmocnienie basow)", L"Hardhearted Rock Line (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_d600.opus") {
			a = LL14(L"夢の痕跡", L"Dream Traces", L"Traces de reves", L"Tracce di sogni", L"Rastros de suenos", L"?? ??", L"夢的痕跡", L"???? ???????", L"Следы снов", L"Traumspuren", L"Rastros de sonhos", L"Droomsporen", L"?lady snow", L"Ruya ?zleri");
			fff = 1;
		}
		if (ft == L"y_d600_s1.opus") {
			a = LL14(L"夢の痕跡(重低音)", L"Dream Traces (Bass Boost)", L"Dream Traces (Renfort graves)", L"Dream Traces (Rinforzo bassi)", L"Dream Traces (Refuerzo graves)", L"Dream Traces (?? ??)", L"Dream Traces (重低音)", L"Dream Traces (????? ??????)", L"Dream Traces (Усиление низких)", L"Dream Traces (Bassverstarkung)", L"Dream Traces (Reforco graves)", L"Dream Traces (Basversterking)", L"Dream Traces (Wzmocnienie basow)", L"Dream Traces (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_d710.opus") {
			a = LL14(L"甲鉄戦艦ナグルファ", L"Ironclad Battleship Naglfar", L"Cuirasse Naglfar", L"Corazzata Naglfar", L"Acorazado Naglfar", L"???? ???", L"甲鐵戰艦 Naglfar", L"??????? ??????? Naglfar", L"Броненосец Нагльфар", L"Panzerschiff Naglfar", L"Encouracado Naglfar", L"Slagschip Naglfar", L"Pancernik Naglfar", L"Z?rhl? Sava? Gemisi Naglfar");
			fff = 1;
		}
		if (ft == L"y_d710_s1.opus") {
			a = LL14(L"甲鉄戦艦ナグルファ(重低音)", L"Ironclad Battleship Naglfar (Bass Boost)", L"Ironclad Battleship Naglfar (Renfort graves)", L"Ironclad Battleship Naglfar (Rinforzo bassi)", L"Ironclad Battleship Naglfar (Refuerzo graves)", L"Ironclad Battleship Naglfar (?? ??)", L"Ironclad Battleship Naglfar (重低音)", L"Ironclad Battleship Naglfar (????? ??????)", L"Ironclad Battleship Naglfar (Усиление низких)", L"Ironclad Battleship Naglfar (Bassverstarkung)", L"Ironclad Battleship Naglfar (Reforco graves)", L"Ironclad Battleship Naglfar (Basversterking)", L"Ironclad Battleship Naglfar (Wzmocnienie basow)", L"Ironclad Battleship Naglfar (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_d800.opus") {
			a = L"LILA -Innocent Wish-";
			fff = 1;
		}
		if (ft == L"y_d800_s1.opus") {
			a = LL14(L"LILA -Innocent Wish-(重低音)", L"LILA -Innocent Wish- (Bass Boost)", L"LILA -Innocent Wish- (Renfort graves)", L"LILA -Innocent Wish- (Rinforzo bassi)", L"LILA -Innocent Wish- (Refuerzo graves)", L"LILA -Innocent Wish- (?? ??)", L"LILA -Innocent Wish- (重低音)", L"LILA -Innocent Wish- (????? ??????)", L"LILA -Innocent Wish- (Усиление низких)", L"LILA -Innocent Wish- (Bassverstarkung)", L"LILA -Innocent Wish- (Reforco graves)", L"LILA -Innocent Wish- (Basversterking)", L"LILA -Innocent Wish- (Wzmocnienie basow)", L"LILA -Innocent Wish- (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_d900.opus") {
			a = LL14(L"エギル海底神殿", L"Egil Undersea Temple", L"Temple sous-marin d'Egil", L"Tempio sottomarino di Egil", L"Templo submarino de Egil", L"?? ????", L"Egil 海底神殿", L"???? Egil ??? ?????", L"Подводный храм Эгиля", L"Egil-Unterseetempel", L"Templo submarino de Egil", L"Egil onderzeese tempel", L"Podmorska ?wi?tynia Egila", L"Egil Denizalt? Tap?na??");
			fff = 1;
		}
		if (ft == L"y_d900_s1.opus") {
			a = LL14(L"エギル海底神殿(重低音)", L"Egil Undersea Temple (Bass Boost)", L"Egil Undersea Temple (Renfort graves)", L"Egil Undersea Temple (Rinforzo bassi)", L"Egil Undersea Temple (Refuerzo graves)", L"Egil Undersea Temple (?? ??)", L"Egil Undersea Temple (重低音)", L"Egil Undersea Temple (????? ??????)", L"Egil Undersea Temple (Усиление низких)", L"Egil Undersea Temple (Bassverstarkung)", L"Egil Undersea Temple (Reforco graves)", L"Egil Undersea Temple (Basversterking)", L"Egil Undersea Temple (Wzmocnienie basow)", L"Egil Undersea Temple (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_d1010.opus") {
			a = L"The Paradise Lost of Norman";
			fff = 1;
		}
		if (ft == L"y_d1010_s1.opus") {
			a = LL14(L"The Paradise Lost of Norman(重低音)", L"The Paradise Lost of Norman (Bass Boost)", L"The Paradise Lost of Norman (Renfort graves)", L"The Paradise Lost of Norman (Rinforzo bassi)", L"The Paradise Lost of Norman (Refuerzo graves)", L"The Paradise Lost of Norman (?? ??)", L"The Paradise Lost of Norman (重低音)", L"The Paradise Lost of Norman (????? ??????)", L"The Paradise Lost of Norman (Усиление низких)", L"The Paradise Lost of Norman (Bassverstarkung)", L"The Paradise Lost of Norman (Reforco graves)", L"The Paradise Lost of Norman (Basversterking)", L"The Paradise Lost of Norman (Wzmocnienie basow)", L"The Paradise Lost of Norman (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_e001.opus") {
			a = L"Yesterday's Journey, Tomorrow's Dream";
			fff = 1;
		}
		if (ft == L"y_e002.opus") {
			a = L"Surging Pressure";
			fff = 1;
		}
		if (ft == L"y_e003.opus") {
			a = L"Turn of the Tide";
			fff = 1;
		}
		if (ft == L"y_e004.opus") {
			a = LL14(L"あの時からずっと…", L"Ever Since That Day...", L"Depuis ce jour-la...", L"Da quel giorno...", L"Desde aquel dia...", L"???? ??...", L"從那時起一直...", L"??? ??? ?????...", L"С того самого дня...", L"Seit jenem Tag...", L"Desde aquele dia...", L"Sinds die dag...", L"Od tamtego dnia...", L"O Gunden Beri...");
			fff = 1;
		}
		if (ft == L"y_e005.opus") {
			a = L"Waver as the Wave";
			fff = 1;
		}
		if (ft == L"y_e006.opus") {
			a = LL14(L"切っても切れない絆", L"Unbreakable Bonds", L"Liens indefectibles", L"Legami indissolubili", L"Vinculos inquebrantables", L"??? ? ? ?? ??", L"無法割捨的羈絆", L"????? ?? ?????", L"Неразрывные узы", L"Unzerbrechliche Bande", L"Lacos inquebraveis", L"Onbreekbare banden", L"Nierozerwalne wi?zi", L"Y?k?lmaz Ba?lar");
			fff = 1;
		}
		if (ft == L"y_e007.opus") {
			a = LL14(L"灰色の深層", L"Gray Depths", L"Profondeurs grises", L"Profondita grigie", L"Profundidades grises", L"??? ??", L"灰色的深層", L"????? ??????", L"Серые глубины", L"Graue Tiefen", L"Profundezas cinzentas", L"Grijze diepten", L"Szare g??biny", L"Gri Derinlikler");
			fff = 1;
		}
		if (ft == L"y_e007_s1.opus") {
			a = LL14(L"灰色の深層(重低音)", L"Gray Depths (Bass Boost)", L"Gray Depths (Renfort graves)", L"Gray Depths (Rinforzo bassi)", L"Gray Depths (Refuerzo graves)", L"Gray Depths (?? ??)", L"Gray Depths (重低音)", L"Gray Depths (????? ??????)", L"Gray Depths (Усиление низких)", L"Gray Depths (Bassverstarkung)", L"Gray Depths (Reforco graves)", L"Gray Depths (Basversterking)", L"Gray Depths (Wzmocnienie basow)", L"Gray Depths (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_e008.opus") {
			a = L"Premonition of Turmoil";
			fff = 1;
		}
		if (ft == L"y_e009.opus") {
			a = LL14(L"歪な願望", L"Twisted Desire", L"Desir tordu", L"Desiderio distorto", L"Deseo retorcido", L"???? ??", L"?曲的願望", L"???? ??????", L"Искаженное желание", L"Verdrehtes Verlangen", L"Desejo distorcido", L"Verdraaid verlangen", L"Skr?cone pragnienie", L"Carp?k Arzu");
			fff = 1;
		}
		if (ft == L"y_e010.opus") {
			a = L"The Road so Far, the Future Ahead";
			fff = 1;
		}
		if (ft == L"y_e011.opus") {
			a = L"Violent Warriors";
			fff = 1;
		}
		if (ft == L"y_e011_s1.opus") {
			a = LL14(L"Violent Warriors(重低音)", L"Violent Warriors (Bass Boost)", L"Violent Warriors (Renfort graves)", L"Violent Warriors (Rinforzo bassi)", L"Violent Warriors (Refuerzo graves)", L"Violent Warriors (?? ??)", L"Violent Warriors (重低音)", L"Violent Warriors (????? ??????)", L"Violent Warriors (Усиление низких)", L"Violent Warriors (Bassverstarkung)", L"Violent Warriors (Reforco graves)", L"Violent Warriors (Basversterking)", L"Violent Warriors (Wzmocnienie basow)", L"Violent Warriors (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_e012.opus") {
			a = LL14(L"手筈通りに", L"As Planned", L"Comme prevu", L"Come pianificato", L"Como se planeo", L"????", L"按照計畫", L"??? ?? ????", L"Как и планировалось", L"Wie geplant", L"Como planejado", L"Zoals gepland", L"Zgodnie z planem", L"Planland??? Gibi");
			fff = 1;
		}
		if (ft == L"y_e013.opus") {
			a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
			fff = 1;
		}
		if (ft == L"y_e014.opus") {
			a = L"ROLLO -Because of Its Purity-";
			fff = 1;
		}
		if (ft == L"y_e015.opus") {
			a = L"Deep Unconscious";
			fff = 1;
		}
		if (ft == L"y_e015_s1.opus") {
			a = LL14(L"Deep Unconscious(重低音)", L"Deep Unconscious (Bass Boost)", L"Deep Unconscious (Renfort graves)", L"Deep Unconscious (Rinforzo bassi)", L"Deep Unconscious (Refuerzo graves)", L"Deep Unconscious (?? ??)", L"Deep Unconscious (重低音)", L"Deep Unconscious (????? ??????)", L"Deep Unconscious (Усиление низких)", L"Deep Unconscious (Bassverstarkung)", L"Deep Unconscious (Reforco graves)", L"Deep Unconscious (Basversterking)", L"Deep Unconscious (Wzmocnienie basow)", L"Deep Unconscious (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f100.opus") {
			a = L"TO BE FREE";
			fff = 1;
		}
		if (ft == L"y_f100_s1.opus") {
			a = LL14(L"TO BE FREE(重低音)", L"TO BE FREE (Bass Boost)", L"TO BE FREE (Renfort graves)", L"TO BE FREE (Rinforzo bassi)", L"TO BE FREE (Refuerzo graves)", L"TO BE FREE (?? ??)", L"TO BE FREE (重低音)", L"TO BE FREE (????? ??????)", L"TO BE FREE (Усиление низких)", L"TO BE FREE (Bassverstarkung)", L"TO BE FREE (Reforco graves)", L"TO BE FREE (Basversterking)", L"TO BE FREE (Wzmocnienie basow)", L"TO BE FREE (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f110.opus") {
			a = L"Brother's Footsteps on the Island";
			fff = 1;
		}
		if (ft == L"y_f110_s1.opus") {
			a = LL14(L"Brother's Footsteps on the Island(重低音)", L"Brother's Footsteps on the Island (Bass Boost)", L"Brother's Footsteps on the Island (Renfort graves)", L"Brother's Footsteps on the Island (Rinforzo bassi)", L"Brother's Footsteps on the Island (Refuerzo graves)", L"Brother's Footsteps on the Island (?? ??)", L"Brother's Footsteps on the Island (重低音)", L"Brother's Footsteps on the Island (????? ??????)", L"Brother's Footsteps on the Island (Усиление низких)", L"Brother's Footsteps on the Island (Bassverstarkung)", L"Brother's Footsteps on the Island (Reforco graves)", L"Brother's Footsteps on the Island (Basversterking)", L"Brother's Footsteps on the Island (Wzmocnienie basow)", L"Brother's Footsteps on the Island (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f120.opus") {
			a = L"Burn with You";
			fff = 1;
		}
		if (ft == L"y_f120_s1.opus") {
			a = LL14(L"Burn with You(重低音)", L"Burn with You (Bass Boost)", L"Burn with You (Renfort graves)", L"Burn with You (Rinforzo bassi)", L"Burn with You (Refuerzo graves)", L"Burn with You (?? ??)", L"Burn with You (重低音)", L"Burn with You (????? ??????)", L"Burn with You (Усиление низких)", L"Burn with You (Bassverstarkung)", L"Burn with You (Reforco graves)", L"Burn with You (Basversterking)", L"Burn with You (Wzmocnienie basow)", L"Burn with You (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f130.opus") {
			a = L"Destined to Keep Running";
			fff = 1;
		}
		if (ft == L"y_f130_s1.opus") {
			a = LL14(L"Destined to Keep Running(重低音)", L"Destined to Keep Running (Bass Boost)", L"Destined to Keep Running (Renfort graves)", L"Destined to Keep Running (Rinforzo bassi)", L"Destined to Keep Running (Refuerzo graves)", L"Destined to Keep Running (?? ??)", L"Destined to Keep Running (重低音)", L"Destined to Keep Running (????? ??????)", L"Destined to Keep Running (Усиление низких)", L"Destined to Keep Running (Bassverstarkung)", L"Destined to Keep Running (Reforco graves)", L"Destined to Keep Running (Basversterking)", L"Destined to Keep Running (Wzmocnienie basow)", L"Destined to Keep Running (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f140.opus") {
			a = L"Ride on Mana!";
			fff = 1;
		}
		if (ft == L"y_f140_s1.opus") {
			a = LL14(L"Ride on Mana!(重低音)", L"Ride on Mana! (Bass Boost)", L"Ride on Mana! (Renfort graves)", L"Ride on Mana! (Rinforzo bassi)", L"Ride on Mana! (Refuerzo graves)", L"Ride on Mana! (?? ??)", L"Ride on Mana! (重低音)", L"Ride on Mana! (????? ??????)", L"Ride on Mana! (Усиление низких)", L"Ride on Mana! (Bassverstarkung)", L"Ride on Mana! (Reforco graves)", L"Ride on Mana! (Basversterking)", L"Ride on Mana! (Wzmocnienie basow)", L"Ride on Mana! (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f150.opus") {
			a = L"Heat Hazard";
			fff = 1;
		}
		if (ft == L"y_f150_s1.opus") {
			a = LL14(L"Heat Hazard(重低音)", L"Heat Hazard (Bass Boost)", L"Heat Hazard (Renfort graves)", L"Heat Hazard (Rinforzo bassi)", L"Heat Hazard (Refuerzo graves)", L"Heat Hazard (?? ??)", L"Heat Hazard (重低音)", L"Heat Hazard (????? ??????)", L"Heat Hazard (Усиление низких)", L"Heat Hazard (Bassverstarkung)", L"Heat Hazard (Reforco graves)", L"Heat Hazard (Basversterking)", L"Heat Hazard (Wzmocnienie basow)", L"Heat Hazard (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f160.opus") {
			a = LL14(L"瞳の中の少年剣士", L"Young Swordsman in My Eyes", L"Le jeune epeiste dans mes yeux", L"Il giovane spadaccino nei miei occhi", L"El joven espadachin en mis ojos", L"??? ?? ?? ??", L"瞳孔中的少年劍士", L"???? ??? ?? ????", L"Юный мечник в моих глазах", L"Junger Schwertkampfer in meinen Augen", L"Jovem espadachim nos meus olhos", L"Jonge zwaardvechter in mijn ogen", L"M?ody szermierz w moich oczach", L"Gozlerimdeki Genc K?l?c Ustas?");
			fff = 1;
		}
		if (ft == L"y_f160_s1.opus") {
			a = LL14(L"瞳の中の少年剣士(重低音)", L"Young Swordsman in My Eyes (Bass Boost)", L"Young Swordsman in My Eyes (Renfort graves)", L"Young Swordsman in My Eyes (Rinforzo bassi)", L"Young Swordsman in My Eyes (Refuerzo graves)", L"Young Swordsman in My Eyes (?? ??)", L"Young Swordsman in My Eyes (重低音)", L"Young Swordsman in My Eyes (????? ??????)", L"Young Swordsman in My Eyes (Усиление низких)", L"Young Swordsman in My Eyes (Bassverstarkung)", L"Young Swordsman in My Eyes (Reforco graves)", L"Young Swordsman in My Eyes (Basversterking)", L"Young Swordsman in My Eyes (Wzmocnienie basow)", L"Young Swordsman in My Eyes (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f200.opus") {
			a = LL14(L"錨を揚げろ！", L"Weigh Anchor!", L"Levez l'ancre !", L"Leva l'ancora!", L"!Leven anclas!", L"?? ???!", L"起錨！", L"???? ???????!", L"Поднять якорь!", L"Anker lichten!", L"Levantar ancora!", L"Licht het anker!", L"Podnie?? kotwic?!", L"Demir Al!");
			fff = 1;
		}
		if (ft == L"y_f200_s1.opus") {
			a = LL14(L"錨を揚げろ！(重低音)", L"Weigh Anchor! (Bass Boost)", L"Weigh Anchor! (Renfort graves)", L"Weigh Anchor! (Rinforzo bassi)", L"Weigh Anchor! (Refuerzo graves)", L"Weigh Anchor! (?? ??)", L"Weigh Anchor! (重低音)", L"Weigh Anchor! (????? ??????)", L"Weigh Anchor! (Усиление низких)", L"Weigh Anchor! (Bassverstarkung)", L"Weigh Anchor! (Reforco graves)", L"Weigh Anchor! (Basversterking)", L"Weigh Anchor! (Wzmocnienie basow)", L"Weigh Anchor! (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f210.opus") {
			a = LL14(L"悠き海に生きる者", L"Those Who Live in the Vast Sea", L"Ceux qui vivent dans la mer vaste", L"Coloro che vivono nel vasto mare", L"Aquellos que viven en el mar vasto", L"??? ??? ?? ?", L"生活在悠久大海的人", L"????? ????? ?????? ?? ????? ??????", L"Те, кто живет в бескрайнем море", L"Die im weiten Meer leben", L"Aqueles que vivem no mar vasto", L"Zij die in de onmetelijke zee leven", L"Ci, ktorzy ?yj? w rozleg?ym morzu", L"Engin Denizlerde Ya?ayanlar");
			fff = 1;
		}
		if (ft == L"y_f210_s1.opus") {
			a = LL14(L"悠き海に生きる者(重低音)", L"Those Who Live in the Vast Sea (Bass Boost)", L"Those Who Live in the Vast Sea (Renfort graves)", L"Those Who Live in the Vast Sea (Rinforzo bassi)", L"Those Who Live in the Vast Sea (Refuerzo graves)", L"Those Who Live in the Vast Sea (?? ??)", L"Those Who Live in the Vast Sea (重低音)", L"Those Who Live in the Vast Sea (????? ??????)", L"Those Who Live in the Vast Sea (Усиление низких)", L"Those Who Live in the Vast Sea (Bassverstarkung)", L"Those Who Live in the Vast Sea (Reforco graves)", L"Those Who Live in the Vast Sea (Basversterking)", L"Those Who Live in the Vast Sea (Wzmocnienie basow)", L"Those Who Live in the Vast Sea (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f220.opus") {
			a = LL14(L"コンパスは踊る", L"The Compass Dances", L"La boussole danse", L"La bussola danza", L"La brujula danza", L"???? ???", L"羅盤在跳舞", L"??????? ????", L"Компас танцует", L"Der Kompass tanzt", L"A bussola danca", L"Het kompas danst", L"Kompas ta?czy", L"Pusula Dans Ediyor");
			fff = 1;
		}
		if (ft == L"y_f220_s1.opus") {
			a = LL14(L"コンパスは踊る(重低音)", L"The Compass Dances (Bass Boost)", L"The Compass Dances (Renfort graves)", L"The Compass Dances (Rinforzo bassi)", L"The Compass Dances (Refuerzo graves)", L"The Compass Dances (?? ??)", L"The Compass Dances (重低音)", L"The Compass Dances (????? ??????)", L"The Compass Dances (Усиление низких)", L"The Compass Dances (Bassverstarkung)", L"The Compass Dances (Reforco graves)", L"The Compass Dances (Basversterking)", L"The Compass Dances (Wzmocnienie basow)", L"The Compass Dances (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f230.opus") {
			a = LL14(L"開闢の海", L"Sea of Genesis", L"Mer de la genese", L"Mare della genesi", L"Mar de la genesis", L"??? ??", L"開闢之海", L"??? ???????", L"Море сотворения", L"Meer der Schopfung", L"Mar da genese", L"Zee van de genesis", L"Morze genezy", L"Yarat?l?? Denizi");
			fff = 1;
		}
		if (ft == L"y_f230_s1.opus") {
			a = LL14(L"開闢の海(重低音)", L"Sea of Genesis (Bass Boost)", L"Sea of Genesis (Renfort graves)", L"Sea of Genesis (Rinforzo bassi)", L"Sea of Genesis (Refuerzo graves)", L"Sea of Genesis (?? ??)", L"Sea of Genesis (重低音)", L"Sea of Genesis (????? ??????)", L"Sea of Genesis (Усиление низких)", L"Sea of Genesis (Bassverstarkung)", L"Sea of Genesis (Reforco graves)", L"Sea of Genesis (Basversterking)", L"Sea of Genesis (Wzmocnienie basow)", L"Sea of Genesis (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_f310.opus") {
			a = L"If I Could Go Back to Those Days";
			fff = 1;
		}
		if (ft == L"y_f310_s1.opus") {
			a = LL14(L"If I Could Go Back to Those Days(重低音)", L"If I Could Go Back to Those Days (Bass Boost)", L"If I Could Go Back to Those Days (Renfort graves)", L"If I Could Go Back to Those Days (Rinforzo bassi)", L"If I Could Go Back to Those Days (Refuerzo graves)", L"If I Could Go Back to Those Days (?? ??)", L"If I Could Go Back to Those Days (重低音)", L"If I Could Go Back to Those Days (????? ??????)", L"If I Could Go Back to Those Days (Усиление низких)", L"If I Could Go Back to Those Days (Bassverstarkung)", L"If I Could Go Back to Those Days (Reforco graves)", L"If I Could Go Back to Those Days (Basversterking)", L"If I Could Go Back to Those Days (Wzmocnienie basow)", L"If I Could Go Back to Those Days (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_gameover.opus") {
			a = L"SO MUCH FOR TODAY (Ys X Ver.)";
			fff = 1;
		}
		if (ft == L"y_op.opus") {
			a = L"Facing the Distant Horizon";
			fff = 1;
		}
		if (ft == L"y_op_lp.opus") {
			a = L"Facing the Distant Horizon (lp)";
			fff = 1;
		}
		if (ft == L"y_t100.opus") {
			a = L"Our Hometown";
			fff = 1;
		}
		if (ft == L"y_t100_s1.opus") {
			a = LL14(L"Our Hometown(重低音)", L"Our Hometown (Bass Boost)", L"Our Hometown (Renfort graves)", L"Our Hometown (Rinforzo bassi)", L"Our Hometown (Refuerzo graves)", L"Our Hometown (?? ??)", L"Our Hometown (重低音)", L"Our Hometown (????? ??????)", L"Our Hometown (Усиление низких)", L"Our Hometown (Bassverstarkung)", L"Our Hometown (Reforco graves)", L"Our Hometown (Basversterking)", L"Our Hometown (Wzmocnienie basow)", L"Our Hometown (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_t200.opus") {
			a = LL14(L"根ざすべき場所", L"Where We Belong", L"La ou nous appartenons", L"Il posto a cui apparteniamo", L"El lugar al que pertenecemos", L"?? ??? ? ?", L"落地生根之處", L"??? ?????", L"Там, где наш дом", L"Wo wir hingehoren", L"Onde pertencemos", L"Waar we thuishoren", L"Miejsce, do ktorego nale?ymy", L"Ait Oldu?umuz Yer");
			fff = 1;
		}
		if (ft == L"y_t200_s1.opus") {
			a = LL14(L"根ざすべき場所(重低音)", L"Where We Belong (Bass Boost)", L"Where We Belong (Renfort graves)", L"Where We Belong (Rinforzo bassi)", L"Where We Belong (Refuerzo graves)", L"Where We Belong (?? ??)", L"Where We Belong (重低音)", L"Where We Belong (????? ??????)", L"Where We Belong (Усиление низких)", L"Where We Belong (Bassverstarkung)", L"Where We Belong (Reforco graves)", L"Where We Belong (Basversterking)", L"Where We Belong (Wzmocnienie basow)", L"Where We Belong (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_t300.opus") {
			a = L"Sometime Siesta";
			fff = 1;
		}
		if (ft == L"y_t300_s1.opus") {
			a = LL14(L"Sometime Siesta(重低音)", L"Sometime Siesta (Bass Boost)", L"Sometime Siesta (Renfort graves)", L"Sometime Siesta (Rinforzo bassi)", L"Sometime Siesta (Refuerzo graves)", L"Sometime Siesta (?? ??)", L"Sometime Siesta (重低音)", L"Sometime Siesta (????? ??????)", L"Sometime Siesta (Усиление низких)", L"Sometime Siesta (Bassverstarkung)", L"Sometime Siesta (Reforco graves)", L"Sometime Siesta (Basversterking)", L"Sometime Siesta (Wzmocnienie basow)", L"Sometime Siesta (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_t301.opus") {
			a = L"Innermost Feelings";
			fff = 1;
		}
		if (ft == L"y_t301_s1.opus") {
			a = LL14(L"Innermost Feelings(重低音)", L"Innermost Feelings (Bass Boost)", L"Innermost Feelings (Renfort graves)", L"Innermost Feelings (Rinforzo bassi)", L"Innermost Feelings (Refuerzo graves)", L"Innermost Feelings (?? ??)", L"Innermost Feelings (重低音)", L"Innermost Feelings (????? ??????)", L"Innermost Feelings (Усиление низких)", L"Innermost Feelings (Bassverstarkung)", L"Innermost Feelings (Reforco graves)", L"Innermost Feelings (Basversterking)", L"Innermost Feelings (Wzmocnienie basow)", L"Innermost Feelings (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_t500.opus") {
			a = LL14(L"情景に揺蕩う", L"Drifting in the Scene", L"Derivant dans la scene", L"Oscillando nella scena", L"Derivando en la escena", L"??? ????", L"?浸於情景中", L"???????? ?? ??????", L"Дрейфуя в пейзаже", L"In der Szenerie treiben", L"Derivando na cena", L"Drijvend in de scene", L"Dryfuj?c w scenerii", L"Manzarada Suzulmek");
			fff = 1;
		}
		if (ft == L"y_t500_s1.opus") {
			a = LL14(L"情景に揺蕩う(重低音)", L"Drifting in the Scene (Bass Boost)", L"Drifting in the Scene (Renfort graves)", L"Drifting in the Scene (Rinforzo bassi)", L"Drifting in the Scene (Refuerzo graves)", L"Drifting in the Scene (?? ??)", L"Drifting in the Scene (重低音)", L"Drifting in the Scene (????? ??????)", L"Drifting in the Scene (Усиление низких)", L"Drifting in the Scene (Bassverstarkung)", L"Drifting in the Scene (Reforco graves)", L"Drifting in the Scene (Basversterking)", L"Drifting in the Scene (Wzmocnienie basow)", L"Drifting in the Scene (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_t600.opus") {
			a = LL14(L"盾の兄弟", L"Shield Brothers", L"Freres de bouclier", L"Fratelli di scudo", L"Hermanos de escudo", L"??? ??", L"盾之兄弟", L"???? ?????", L"Братья по щиту", L"Schildbruder", L"Irmaos de escudo", L"Schildbroeders", L"Bracia tarczy", L"Kalkan Karde?li?i");
			fff = 1;
		}
		if (ft == L"y_t600_s1.opus") {
			a = LL14(L"盾の兄弟(重低音)", L"Shield Brothers (Bass Boost)", L"Shield Brothers (Renfort graves)", L"Shield Brothers (Rinforzo bassi)", L"Shield Brothers (Refuerzo graves)", L"Shield Brothers (?? ??)", L"Shield Brothers (重低音)", L"Shield Brothers (????? ??????)", L"Shield Brothers (Усиление низких)", L"Shield Brothers (Bassverstarkung)", L"Shield Brothers (Reforco graves)", L"Shield Brothers (Basversterking)", L"Shield Brothers (Wzmocnienie basow)", L"Shield Brothers (Bas guclendirme)");
			fff = 1;
		}
		if (ft == L"y_title.opus") {
			a = LL14(L"その優しさは誰のため", L"For Whom Is That Kindness", L"Pour qui est cette gentillesse", L"Per chi e quella gentilezza", L"Para quien es esa amabilidad", L"? ???? ??を ?? ???", L"那??柔是為了誰", L"??? ??? ?????", L"Для кого эта доброта", L"Wem gilt diese Gute", L"Para quem e essa bondade", L"Voor wie is die vriendelijkheid", L"Dla kogo ta dobro?", L"Bu Nezaket Kimin ?cin");
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
				if (a.Mid(4, 4) == "b010") { a = LL14(L"URBAN TERROR(イントロあり)", L"URBAN TERROR (With Intro)", L"URBAN TERROR (Avec Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (??? ??)", L"URBAN TERROR (含前奏)", L"URBAN TERROR (?? ?????)", L"URBAN TERROR (С интро)", L"URBAN TERROR (Mit Intro)", L"URBAN TERROR (Com Intro)", L"URBAN TERROR (Met Intro)", L"URBAN TERROR (Z intro)", L"URBAN TERROR (Giri?li)"); }
				if (a.Mid(4, 5) == "b011b") { a = "DREAMING IN THE GRIMWALD"; }
				if (a.Mid(4, 4) == "b011") { a = LL14(L"DREAMING IN THE GRIMWALD(イントロあり)", L"DREAMING IN THE GRIMWALD (With Intro)", L"DREAMING IN THE GRIMWALD (Avec Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (??? ??)", L"DREAMING IN THE GRIMWALD (含前奏)", L"DREAMING IN THE GRIMWALD (?? ?????)", L"DREAMING IN THE GRIMWALD (С интро)", L"DREAMING IN THE GRIMWALD (Mit Intro)", L"DREAMING IN THE GRIMWALD (Com Intro)", L"DREAMING IN THE GRIMWALD (Met Intro)", L"DREAMING IN THE GRIMWALD (Z intro)", L"DREAMING IN THE GRIMWALD (Giri?li)"); }
				if (a.Mid(4, 4) == "b012") { a = "WILD CARD"; }
				if (a.Mid(4, 5) == "b014b") { a = "FULL MOON CEREMONY"; }
				if (a.Mid(4, 4) == "b014") { a = LL14(L"FULL MOON CEREMONY(イントロあり)", L"FULL MOON CEREMONY (With Intro)", L"FULL MOON CEREMONY (Avec Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (??? ??)", L"FULL MOON CEREMONY (含前奏)", L"FULL MOON CEREMONY (?? ?????)", L"FULL MOON CEREMONY (С инトロ)", L"FULL MOON CEREMONY (Mit Intro)", L"FULL MOON CEREMONY (Com Intro)", L"FULL MOON CEREMONY (Met Intro)", L"FULL MOON CEREMONY (Z intro)", L"FULL MOON CEREMONY (Giri?li)"); }
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
				if (a.Mid(4, 4) == "e008") { a = L"IL ETAIT UNE FOIS"; }
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
				if (a.Mid(4, 4) == "muon") { a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"??", L"無音", L"???", L"Тишина", L"Stille", L"Silencio", L"Stilte", L"Cisza", L"Sessizlik"); }
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
					a = LL14(L"罪と罰と偽りと", L"Sin, Punishment and Falsehood", L"Peche, punition et mensonge", L"Peccato, punizione e falsita", L"Pecado, castigo y falsedad", L"?? ?? ???", L"罪、罰與?偽", L"??????? ??????? ??????", L"Грех, наказание и ложь", L"Sunde, Strafe und Falschheit", L"Pecado, castigo e falsidade", L"Zonde, straf en valsheid", L"Grzech, kara i fa?sz", L"Gunah, Ceza ve Sahtelik");
					break;
				case 81005:
					a = LL14(L"昏き鐘の残響", L"Resonance of the Dark Bell", L"Resonance de la cloche sombre", L"Risonanza della campana oscura", L"Resonancia de la campana oscura", L"??? ?? ??", L"昏暗之鐘的殘響", L"??? ????? ??????", L"Резонанс темного колокола", L"Resonanz der dunklen Glocke", L"Ressonancia do sino sombrio", L"Resonantie van de duistere klok", L"Rezonans mrocznego dzwonu", L"Karanl?k Can?n Yank?s?");
					break;
				case 81006:
					a = "Right on the Mark";
					break;
				case 81007:
					a = LL14(L"悪夢ふたたび", L"Nightmare Again", L"Le cauchemar recommence", L"Incubo di nuovo", L"Pesadilla de nuevo", L"?? ?? ??", L"?夢重現", L"??????? ??? ????", L"Кошмар снова", L"Albtraum erneut", L"Pesadelo novamente", L"Nachtmerrie opnieuw", L"Koszmar ponownie", L"Kabus Yeniden");
					break;
				case 81008:
					a = "Crossbell Nostalgia";
					break;
				case 81009:
					a = LL14(L"創まりの円庭", L"Garden of Beginnings", L"Jardin des commencements", L"Giardino degli inizi", L"Jardin de los inicios", L"??? ??", L"創始之圓庭", L"????? ????????", L"Сад начал", L"Garten der Anfange", L"Jardim dos comecos", L"Tuin van het begin", L"Ogrod pocz?tkow", L"Ba?lang?c Bahcesi");
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
					a = LL14(L"さざめきの途路", L"Path of Tumult", L"Chemin du tumulte", L"Sentiero del tumulto", L"Senda del tumulto", L"????? ?", L"?雜的途徑", L"???? ????????", L"Путь суматохи", L"Pfad des Tumults", L"Caminho do tumulto", L"Pad van rumoer", L"?cie?ka zgie?ku", L"Gurultulu Yol");
					break;
				case 81016:
					a = LL14(L"蒼の大地に生きる者", L"Those Who Live on the Azure Land", L"Ceux qui vivent sur la terre d'azur", L"Coloro che vivono sulla terra azzurra", L"Aquellos que viven en la tierra azul", L"?? ??? ?? ?", L"生活在蒼之大地的人", L"????? ????? ?????? ??? ????? ???????", L"Те, кто живет на лазурной земле", L"Die auf dem azurblauen Land leben", L"Aqueles que vivem na terra azul", L"Zij die op het azuurblauwe land leven", L"Ci, ktorzy ?yj? na b??kitnej ziemi", L"Mavi Topraklarda Ya?ayanlar");
					break;
				case 81017:
					a = LL14(L"黎明の鐘", L"Bell of Dawn", L"Cloche de l'aube", L"Campana dell'alba", L"Campana del alba", L"??? ?", L"黎明之鐘", L"??? ?????", L"Колокол рассвета", L"Glocke der Dammerung", L"Sino da aurora", L"Klok van de dageraad", L"Dzwon ?witu", L"?afak Can?");
					break;
				case 81018:
					a = LL14(L"レメディファンタジア -仲間とともに-", L"Remedi Fantasia -With Comrades-", L"Remedi Fantasia -Avec des camarades-", L"Remedi Fantasia -Con i compagni-", L"Remedi Fantasia -Con camaradas-", L"??? ???? ~??? ??~", L"Remedi Fantasia -與夥伴一起-", L"Remedi Fantasia - ?? ??????", L"Remedi Fantasia -С товарищами-", L"Remedi Fantasia -Mit Kameraden-", L"Remedi Fantasia -Com camaradas-", L"Remedi Fantasia -Met kameraden-", L"Remedi Fantasia -Z towarzyszami-", L"Remedi Fantasia -Yolda?larla-");
					break;
				case 81019:
					a = "Slight Suspicion";
					break;
				case 81020:
					a = "Maliciousness in the Mirror";
					break;
				case 81021:
					a = LL14(L"暗澹たる世界", L"Dark World", L"Monde sombre", L"Mondo oscuro", L"Mundo oscuro", L"??? ??", L"暗淡的世界", L"???? ????", L"Мрачный мир", L"Dunkle Welt", L"Mundo sombrio", L"Duistere wereld", L"Mroczny ?wiat", L"Karanl?k Dunya");
					break;
				case 81022:
					a = LL14(L"ひとときの温もり", L"Brief Warmth", L"Bref repit de chaleur", L"Breve calore", L"Breve calor", L"?? ??? ??", L"片刻的?暖", L"??? ????", L"Краткое тепло", L"Kurze Warme", L"Breve calor", L"Korte warmte", L"Krotkie ciep?o", L"K?sa Sureli S?cakl?k");
					break;
				case 81023:
					a = LL14(L"今、創まりのとき", L"Now, the Moment of Creation", L"Maintenant, le moment de la creation", L"Ora, il momento della creazione", L"Ahora, el momento de la creacion", L"??, ??? ??", L"現在，創始之時", L"????? ???? ???????", L"Теперь момент сотворения", L"Nun, der Moment der Schopfung", L"Agora, o momento da criacao", L"Nu, het moment van creatie", L"Teraz moment stworzenia", L"?imdi, Yarat?l?? An?");
					break;
				case 81024:
					a = "KERAUNOS -Fear and Hatred-";
					break;
				case 81025:
					a = LL14(L"亡失われた魂", L"Lost Souls", L"Ames perdues", L"Anime perse", L"Almas perdidas", L"???? ???", L"迷失的靈魂", L"????? ??????", L"Потерянные души", L"Verlorene Seelen", L"Almas perdidas", L"Verloren zielen", L"Zagubione dusze", L"Kay?p Ruhlar");
					break;
				case 81026:
					a = LL14(L"穏やかな時間", L"Peaceful Time", L"Temps paisible", L"Tempo pacifico", L"Tiempo pacifico", L"??? ??", L"平靜的時光", L"??? ????", L"Мирное время", L"Friedliche Zeit", L"Tempo pacifico", L"Vredige tijd", L"Spokojny czas", L"Huzurlu Vakit");
					break;
				case 81027:
					break;
				case 81028:
					a = LL14(L"運命という名の歯車", L"Gears of Fate", L"Engrenages du destin", L"Ingranaggi del destino", L"Engranajes del destino", L"????? ??? ????", L"名為命運的齒輪", L"???? ?????", L"Шестеренки судьбы", L"Zahnrader des Schicksals", L"Engrenagens do destino", L"Raderen van het lot", L"Ko?a z?bate losu", L"Kader Carklar?");
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
					a = LL14(L"鉱山町マインツ -創Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-");
					break;
				case 81316:
					a = LL14(L"木霊の道 -創Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-");
					break;
				case 81317:
					a = "Raindrops with the Wind";
					break;
				case 81319:
					a = LL14(L"陽溜まりにただいまを", L"Home in the Sunshine", L"Retour au soleil", L"A casa sotto il sole", L"Hogar bajo el sol", L"?? ?? ???????", L"在陽光下?我回來了", L"?????? ?????? ?? ??? ?????", L"Домой под лучами солнца", L"Zuhause im Sonnenschein", L"Lar sob o sol", L"Thuis in de zon", L"Dom w s?o?cu", L"Gune? I????nda Eve Donu?");
					break;
				case 81320:
					a = "Wind-Up Yesterday!";
					break;
				case 81321:
					a = LL14(L"零の邂逅", L"Zero Encounter", L"Rencontre de zero", L"Incontro zero", L"Encuentro cero", L"??? ??", L"零之邂逅", L"???? ?????", L"Встреча Зеро", L"Zero-Begegnung", L"Encontro zero", L"Zero ontmoeting", L"Spotkanie zero", L"S?f?r Kar??la?mas?");
					break;
				case 81322:
					a = LL14(L"影の見えざる手", L"Invisible Hand in the Shadows", L"Main invisible dans l'ombre", L"Mano invisibile nelle ombre", L"Mano invisible en las sombras", L"???? ??? ?? ?", L"影子那看不見的手", L"?? ???? ?? ??????", L"Невидимая рука в тени", L"Unsichtbare Hand im Schatten", L"Mao invisivel nas sombras", L"Onzichtbare hand in de schaduw", L"Niewidzialna r?ka w cieniu", L"Golgedeki Gorunmez El");
					break;
				case 81950: case 81951: case 81952: case 81953: case 81954:
				case 81955: case 81956: case 81957: case 81958: case 81961:
				case 81962: case 81963: case 81964: case 81965: case 81966:
				case 81967: case 81968: case 81969:
					break;
				case 82065:
					a = LL14(L"鋼鉄牙城", L"Iron Fortress", L"Forteresse d'acier", L"Fortezza d'acciaio", L"Fortaleza de acero", L"????", L"鋼鐵牙城", L"??? ??????", L"Железная крепость", L"Eiserne Festung", L"Fortaleza de aco", L"IJzeren vesting", L"Stalowa twierdza", L"Demir Kale");
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
					a = LL14(L"波間に弾む心", L"Heart Bouncing on the Waves", L"C?ur bondissant sur les vagues", L"Cuore che rimbalza sulle onde", L"Corazon saltando en las olas", L"?? ??? ??? ??", L"在波浪間雀躍的心", L"??? ???? ??? ???????", L"Сердце, прыгающее на волнах", L"Herz, das auf den Wellen hupft", L"Coracao saltitando nas ondas", L"Hart dat stuitert op de golven", L"Serce skacz?ce na falach", L"Dalgalarda Hoplayan Kalp");
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
					a = LL14(L"流麗闘冴", L"Elegant Battle", L"Combat elegant", L"Battaglia elegante", L"Batalla elegante", L"????", L"流麗鬥冴", L"????? ?????", L"Элегантная битва", L"Eleganter Kampf", L"Batalha elegante", L"Elegant gevecht", L"Elegancka bitwa", L"Zarif Sava?");
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
					a = LL14(L"ひとかけらの光明", L"Sliver of Light", L"Lueur d'espoir", L"Barlume di luce", L"Rayo de luz", L"? ??? ??", L"一絲光明", L"??? ?? ?????", L"Лучик света", L"Ein Schimmer Licht", L"Raio de luz", L"Lichtstraaltje", L"Promyk ?wiat?a", L"Bir I??k Huzmesi");
					break;
				case 82143:
					a = LL14(L"反攻の烽火", L"Beacon of Counterattack", L"Signal de contre-attaque", L"Segnale di contrattacco", L"Senal de contraataque", L"??? ??", L"反攻的烽火", L"????? ?????? ??????", L"Маяк контратаки", L"Leuchtfeuer des Gegenangriffs", L"Sinal de contra-ataque", L"Baken van de tegenaanval", L"Sygna? kontrataku", L"Kar?? Atak ??areti");
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
					a = LL14(L"今宵は宴と参りましょう", L"Tonight We Feast", L"Ce soir, nous festoyons", L"Stasera banchettiamo", L"Esta noche festejamos", L"?? ?? ??? ???", L"今?讓我們舉行宴會?", L"?????? ????? ?????", L"Сегодня мы пируем", L"Heute Abend wird gefeiert", L"Esta noite vamos festejar", L"Vanavond vieren we feest", L"Dzi? wieczorem ucztujemy", L"Bu Gece Ziyafet Cekelim");
					break;
				case 82159:
					a = "Flash Your Fighting Spirit";
					break;
				case 82161:
					a = LL14(L"鈍色に這う", L"Crawling in Gray", L"Ramper dans le gris", L"Strisciando nel grigio", L"Gateando en el gris", L"???? ????", L"在灰色中爬行", L"????? ?? ???????", L"Ползти в сером", L"Kriechen im Grau", L"Rastejando no cinza", L"Kruipen in het grijs", L"Pe?zanie w szaro?ci", L"Gri ?cinde Surunmek");
					break;
				case 82163:
					a = "Pyro Labyrinth";
					break;
				case 82164:
					a = LL14(L"優しさを未来に託して", L"Entrust Kindness to the Future", L"Confier la gentillesse au futur", L"Affidare la gentilezza al futuro", L"Confiar la amabilidad al futuro", L"???? ??? ???", L"將?柔託付給未來", L"??????? ????? ????????", L"Вверить доброту будущему", L"Gute der Zukunft anvertrauen", L"Confiar a bondade ao futuro", L"Vriendelijkheid aan de toekomst toevertrouwen", L"Powierzy? dobro? przysz?o?ci", L"Nezaketi Gelece?e Emanet Etmek");
					break;
				case 82166:
					a = LL14(L"高らかに、誇らしく", L"Loud and Proud", L"Fort et fier", L"Forte e fiero", L"Fuerte y orgulloso", L"???, ?????", L"高聲地，自豪地", L"???? ???? ?????", L"Громко и гордо", L"Laut und stolz", L"Alto e orgulhoso", L"Luid en trots", L"G?o?no i dumnie", L"Yuksek Sesle ve Gururla");
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
					a = LL14(L"このあと美味しくいただきました", L"Then We Ate Deliciously", L"Ensuite, nous avons mange delicieusement", L"Poi abbiamo mangiato deliziosamente", L"Luego comimos deliciosamente", L"?? ??? ?????", L"在那之後我們美味地享用了", L"?? ????? ????", L"Затем мы вкусно поели", L"Dann haben wir kostlich gegessen", L"Depois comemos deliciosamente", L"Daarna hebben we heerlijk gegeten", L"Potem zjedli?my wybornie", L"Sonra Afiyetle Yedik");
					break;
				case 82186:
					a = "Emergency Order";
					break;
				case 82188:
					a = LL14(L"激烈! 撃滅! ミシュナイダー!!", L"Fierce! Crush! Mishnayder!!", L"Feroce ! Ecraser ! Mishnayder !!", L"Feroce! Schiaccia! Mishnayder!!", L"!Feroz! !Aplasta! !Mishnayder!", L"??! ??! ?????!!", L"激烈！?滅！Mishnayder！！", L"???! ???! Mishnayder!!", L"Яростно! Разгромить! Mishnayder!!", L"Heftig! Zerschmettern! Mishnayder!!", L"Feroz! Esmagar! Mishnayder!!", L"Heftig! Verpletter! Mishnayder!!", L"Gwa?townie! Zmia?d?y?! Mishnayder!!", L"Sert! Ez Gec! Mishnayder!!");
					break;
				case 82189:
					a = "Life Goes On";
					break;
				default:
					if (a == L"ed8_inf_ex.opus") {
						a = LL14(L"夢幻の彼方へ", L"To the Realm of Dreams", L"Vers le royaume des reves", L"Verso il regno dei sogni", L"Hacia el reino de los suenos", L"??? ????", L"往夢幻的彼方", L"??? ???? ???????", L"В царство снов", L"In das Reich der Traume", L"Para o reino dos sonhos", L"Naar het rijk der dromen", L"Do krainy snow", L"Ruyalar Alemine");
					}
				}
				switch (_ttoi(a.Mid(2, 4))) {
				case 8001:
					a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"?? ??? 《VII組》", L"特科班《VII組》", L"????? ??????", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klas VII", L"Klasa VII", L"S?n?f VII");
					break;
				case 8002:
					a = LL14(L"スタートライン", L"Start Line", L"Ligne de depart", L"Linea di partenza", L"Linea de salida", L"??? ??", L"起?線", L"?? ???????", L"Стартовая линия", L"Startlinie", L"Linha de partida", L"Startlijn", L"Linia startu", L"Ba?lang?c Cizgisi");
					break;
				case 8003:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8004:
					a = "Youthful Victory";
					break;
				case 8006:
					a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours vers l'avant", L"Sempre avanti", L"Siempre adelante", L"??? ???", L"一心一意，向前邁進", L"??? ?????? ??????", L"Только вперед", L"Immer vorwarts", L"Sempre em frente", L"Altijd vooruit", L"Zawsze do przodu", L"Daima ?leri");
					break;
				case 8007:
					a = LL14(L"縁 -つなぐもの-", L"Fate -Connecting-", L"Destin -Connexion-", L"Destino -Connessione-", L"Destino -Conexion-", L"?? ~???? ?~", L"? -連?者-", L"????? - ???????", L"Судьба -Связующее звено-", L"Schicksal -Verbindend-", L"Destino -Conectando-", L"Lot -Verbindend-", L"Los -??cz?cy-", L"Kader -Ba?lay?c?-");
					break;
				case 8102:
					a = LL14(L"翡翠の公都バリアハート", L"Jade Capital Bareahard", L"Capitale de jade Bareahard", L"Capitale di giada Bareahard", L"Capital de jade Bareahard", L"??? ?? Bareahard", L"翡翠公都 Bareahard", L"????? ????? Bareahard", L"Нефритовая столица Bareahard", L"Jade-Hauptstadt Bareahard", L"Capital de jade Bareahard", L"Jade-hoofdstad Bareahard", L"Jadeitowa stolica Bareahard", L"Ye?im Ba?kenti Bareahard");
					break;
				case 8104:
					a = LL14(L"黒銀の鋼都ルーレ", L"Iron City Roer", L"Ville de fer Roer", L"Citta del ferro Roer", L"Ciudad del hierro Roer", L"??? ?? ?? Roer", L"黑銀鋼都 Roer", L"????? Roer ????????", L"Железный город Roer", L"Eisenstadt Roer", L"Cidade do ferro Roer", L"IJzerstad Roer", L"?elazne miasto Roer", L"Demir ?ehir Roer");
					break;
				case 8150:
					a = LL14(L"下校途中にパンケーキ", L"Pancakes on the Way Home", L"Des pancakes sur le chemin du retour", L"Pancake sulla via di casa", L"Tortitas de camino a casa", L"?? ?? ????", L"下學路上的煎餅", L"?????? ?? ???? ??????", L"Блинчики по дороге домой", L"Pfannkuchen auf dem Heimweg", L"Panquecas no caminho para casa", L"Pannenkoeken op weg naar huis", L"Nale?niki w drodze do domu", L"Eve Giderken Krep");
					break;
				case 8151:
					a = LL14(L"可能性は無限大", L"Infinite Possibilities", L"Possibilites infinies", L"Possibilita infinite", L"Posibilidades infinitas", L"???? ???", L"可能性是無限的", L"???????? ?? ??????", L"Бесконечные возможности", L"Unbegrenzte Moglichkeiten", L"Possibilidades infinitas", L"Oneindige mogelijkheden", L"Niesko?czone mo?liwo?ci", L"Sonsuz Olas?l?klar");
					break;
				case 8152:
					a = LL14(L"夜のしじまに", L"In the Night Silence", L"Dans le silence nocturne", L"Nel silenzio della notte", L"En el silencio de la noche", L"?? ?? ??", L"在深夜的靜謐中", L"?? ??? ?????", L"В ночной тишине", L"In der nachtlichen Stille", L"No silencio da noite", L"In de nachtelijke stilte", L"W nocnej ciszy", L"Gece Sessizli?inde");
					break;
				case 8153:
					a = LL14(L"夕景", L"Evening Scene", L"Scene de soiree", L"Scena serale", L"Escena vespertina", L"?? ??", L"夕陽美景", L"???? ??????", L"Вечерний пейзаж", L"Abendszene", L"Cena noturna", L"Avondtafereel", L"Wieczorna scena", L"Ak?am Manzaras?");
					break;
				case 8154:
					a = LL14(L"新しい朝", L"New Morning", L"Nouveau matin", L"Nuovo mattino", L"Nueva manana", L"??? ??", L"新的早晨", L"???? ????", L"Новое утро", L"Neuer Morgen", L"Nova manha", L"Nieuwe ochtend", L"Nowy poranek", L"Yeni Sabah");
					break;
				case 8155:
					a = LL14(L"束の間の里帰り", L"Brief Homecoming", L"Bref retour au pays", L"Breve ritorno a casa", L"Breve regreso al hogar", L"?? ??? ??", L"短暫的返郷", L"???? ????? ?????", L"Краткое возвращение домой", L"Kurze Heimkehr", L"Breve retorno ao lar", L"Korte thuiskomst", L"Krotki powrot do domu", L"K?sa Bir Memleket Donu?u");
					break;
				case 8156:
					a = LL14(L"白亜の旧都セントアーク", L"White City St. Ark", L"Vieille capitale blanche St. Ark", L"Antica capitale bianca St. Ark", L"Vieja capital blanca St. Ark", L"??? ??セントアーク", L"白亞舊都 St. Ark", L"??????? ??????? ??????? St. Ark", L"Белая старая столица Сент-Арк", L"Weise alte Hauptstadt St. Ark", L"Antiga capital branca St. Ark", L"Witte oude hoofdstad St. Ark", L"Bia?a stara stolica St. Ark", L"Beyaz Eski Ba?kent St. Ark");
					break;
				case 8157:
					a = LL14(L"紡績町パルム", L"Spinning Town Parm", L"Ville textile Parm", L"Citta tessile Parm", L"Pueblo textil Parm", L"?? ?? Parm", L"紡織鎮 Parm", L"???? ????? Parm", L"Ткацкий городок Парм", L"Spinnereistadt Parm", L"Vila textil Parm", L"Spinnerijstad Parm", L"Tkackie miasto Parm", L"Dokuma Kasabas? Parm");
					break;
				case 8158:
					a = LL14(L"籠の中のクロスベル", L"Crossbell in a Cage", L"Crossbell en cage", L"Crossbell in gabbia", L"Crossbell en una jaula", L"?? ?? Crossbell", L"籠中 Crossbell", L"Crossbell ?? ???", L"Кроссбелл в клетке", L"Crossbell im Kafig", L"Crossbell em uma gaiola", L"Crossbell in een kooi", L"Crossbell w klatce", L"Kafesteki Crossbell");
					break;
				case 8159:
					a = LL14(L"今、成すべきこと", L"What Must Be Done Now", L"Ce qui doit etre fait maintenant", L"Cio che deve essere fatto ora", L"Lo que debe hacerse ahora", L"??, ?? ? ?", L"現在，應做之事", L"?? ??? ???? ????", L"Что должно быть сделано сейчас", L"Was jetzt getan werden muss", L"O que deve ser feito agora", L"Wat nu moet worden gedaan", L"Co nale?y teraz zrobi?", L"?imdi Yap?lmas? Gereken");
					break;
				case 8160:
					a = LL14(L"歓楽都市ラクウェル", L"Pleasure City Raquel", L"Ville de plaisir Raquel", L"Citta del piacere Raquel", L"Ciudad del placer Raquel", L"?? ?? Raquel", L"歡樂都市 Raquel", L"????? ?????? Raquel", L"Город развлечений Ракель", L"Vergnugungsstadt Raquel", L"Cidade do prazer Raquel", L"Plezierstad Raquel", L"Miasto rozrywki Raquel", L"E?lence ?ehri Raquel");
					break;
				case 8161:
					a = LL14(L"静かなる駆け引き", L"Quiet Maneuvering", L"Manoeuvres silencieuses", L"Manovre silenziose", L"Maniobras silenciosas", L"??? ??", L"靜默的周旋", L"?????? ?????", L"Тихое маневрирование", L"Stilles Manovrieren", L"Manobras silenciosas", L"Stil manoeuvreren", L"Ciche manewry", L"Sessiz Manevralar");
					break;
				case 8162:
					a = LL14(L"赫奕たるヘイムダル", L"Splendid Heimdallr", L"Heimdallr splendide", L"Splendida Heimdallr", L"Esplendida Heimdallr", L"??? Heimdallr", L"赫赫有名的 Heimdallr", L"Heimdallr ???????", L"Великолепный Хеймдалль", L"Prachtiges Heimdallr", L"Esplendida Heimdallr", L"Prachtig Heimdallr", L"Wspania?y Heimdallr", L"Gorkemli Heimdallr");
					break;
				case 8163:
					a = LL14(L"紺碧の海都オルディス", L"Azure Port City Ordys", L"Ville portuaire d'azur Ordys", L"Citta portuale azzurra Ordys", L"Ciudad portuaria azul Ordys", L"??? ?? Ordys", L"紺碧海都 Ordys", L"????? Ordys ???????? ???????", L"Лазурный портовый город Ордис", L"Azurblaue Hafenstadt Ordys", L"Cidade portuaria azul Ordys", L"Azuurblauwe havenstad Ordys", L"B??kitne miasto portowe Ordys", L"Gok Mavisi Liman ?ehri Ordys");
					break;
				case 8164:
					a = LL14(L"最前線都市", L"Front-line City", L"Ville de premiere ligne", L"Citta di prima linea", L"Ciudad de primera linea", L"??? ??", L"最前線都市", L"????? ?????? ????????", L"Прифронтовой город", L"Frontstadt", L"Cidade de linha de frente", L"Frontstad", L"Miasto na linii frontu", L"Cephe ?ehri");
					break;
				case 8165:
					a = "Base Camp";
					break;
				case 8166:
					a = LL14(L"精強なる兵たち", L"Elite Soldiers", L"Soldats d'elite", L"Soldati d'elite", L"Soldados de elite", L"??? ???", L"精?的士兵們", L"???? ??????", L"Элитные солдаты", L"Elitesoldaten", L"Soldados de elite", L"Elitesoldaten", L"Elitarni ?o?nierze", L"Seckin Askerler");
					break;
				case 8168:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8170:
					a = LL14(L"隠れ里エリン", L"Hidden Village Erin", L"Village cache d'Erin", L"Villaggio nascosto di Erin", L"Aldea oculta de Erin", L"??? ?? ??", L"隠之里 Erin", L"???? Erin ???????", L"Скрытая деревня Эрин", L"Verborgenes Dorf Erin", L"Vila oculta de Erin", L"Verborgen dorp Erin", L"Ukryta wioska Erin", L"Gizli Koy Erin");
					break;
				case 8171:
					a = LL14(L"潜入調査", L"Infiltration", L"Infiltration", L"Infiltrazione", L"Infiltracion", L"?? ??", L"潛入調?", L"????", L"Инфильтрация", L"Infiltration", L"Infiltracao", L"Infiltratie", L"Infiltracja", L"S?zma Harekat?");
					break;
				case 8172:
					a = LL14(L"昏冥の中で", L"In the Darkness", L"Dans les tenebres", L"Nell'oscurita", L"En la oscuridad", L"?? ???", L"在昏暗之中", L"?? ??????", L"Во тьме", L"In der Dunkelheit", L"Na escuridao", L"In de duisternis", L"W ciemno?ci", L"Karanl?kta");
					break;
				case 8173:
					a = LL14(L"紅き閃影 -光まとう翼-", L"Crimson Flash -Wings of Light-", L"Eclat carmin -Ailes de lumiere-", L"Lampo cremisi -Ali di luce-", L"Destello carmesi -Alas de luz-", L"?? ?? ~?? ?? ??~", L"紅之閃影 -披光之翼-", L"???? ?????? - ????? ?????", L"Алая вспышка -Крылья света-", L"Purpurroter Blitz -Flugel des Lichts-", L"Lampejo carmesim -Asas de luz-", L"Karmozijnrode flits -Vleugels van licht-", L"Szkar?atny b?ysk -Skrzyd?a ?wiat?a-", L"K?z?l Par?lt? -I??k Kanatlar?-");
					break;
				case 8174:
					a = LL14(L"聖ウルスラ医科大学 -閃Ver.-", L"St. Ursula Medical College -CS Ver.-", L"St. Ursula Medical College -CS Ver.-", L"St. Ursula Medical College -CS Ver.-", L"St. Ursula Medical College -CS Ver.-", L"? ???? ???? -閃Ver.-", L"聖烏爾蘇拉醫科大學 -閃Ver.-", L"???? ???? ??????? ?????? -CS Ver.-", L"Медицинский колледж Св. Урсулы -CS Ver.-", L"Medizinische Hochschule St. Ursula -CS Ver.-", L"Faculdade de Medicina Sta. Ursula -CS Ver.-", L"Medisch College St. Ursula -CS Ver.-", L"Kolegium Medyczne ?w. Urszuli -CS Ver.-", L"Aziz Ursula T?p Koleji -CS Ver.-");
					break;
				case 8175:
					a = LL14(L"一抹の不安、一縷の望み", L"Hint of Unease, Ray of Hope", L"Une pointe d'inquietude, un rayon d'espoir", L"Un briciolo di ansia, un raggio di speranza", L"Un rastro de inquietud, un rayo de esperanza", L"??? ??, ? ?? ??", L"一抹不安，一縷希望", L"????? ?? ?????? ???? ?? ?????", L"Тень беспокойства, луч надежды", L"Ein Hauch von Unbehagen, ein Hoffnungsschimmer", L"Um toque de inquietacao, um raio de esperanca", L"Een spoortje van onrust, een straal van hoop", L"Cie? niepokoju, promie? nadziei", L"Bir Parca Huzursuzluk, Bir Umut I????");
					break;
				case 8176:
					a = "Lyrical Amber";
					break;
				case 8177:
					a = LL14(L"水面を渡る風", L"Wind Over the Water", L"Vent sur l'eau", L"Vento sull'acqua", L"Viento sobre el agua", L"??? ??? ??", L"拂過水面的風", L"???? ??? ?????", L"Ветер над водой", L"Wind uber dem Wasser", L"Vento sobre a agua", L"Wind over het water", L"Wiatr nad wod?", L"Su Ustundeki Ruzgar");
					break;
				case 8250:
					a = LL14(L"流れる雲の彼方に", L"Beyond the Drifting Clouds", L"Au-dela des nuages derivants", L"Oltre le nuvole erranti", L"Mas alla de las nubes errantes", L"??? ?? ????", L"流雲的彼方", L"?? ???? ????? ???????", L"За плывущими облаками", L"Jenseits der ziehenden Wolken", L"Alem das nuvens flutuantes", L"Voorbij de drijvende wolken", L"Poza p?yn?ce chmury", L"Suzulen Bulutlar?n Otesinde");
					break;
				case 8251:
					a = LL14(L"静寂の小路", L"Path of Silence", L"Chemin du silence", L"Sentiero del silenzio", L"Senda del silencio", L"??? ??", L"安靜的小徑", L"???? ?????", L"Путь тишины", L"Pfad der Stille", L"Caminho do silencio", L"Pad van stilte", L"?cie?ka ciszy", L"Sessizlik Yolu");
					break;
				case 8252:
					a = LL14(L"崖谷の狭間", L"Gap of the Cliff", L"Le fosse de la falaise", L"Divario della scogliera", L"Brecha del acantilado", L"?? ??? ?", L"崖谷狹間", L"???? ?????", L"Разрыв утеса", L"Spalt der Klippe", L"Fenda do penhasco", L"Kloof van de klif", L"Szczelina klifu", L"Ucurum Bo?lu?u");
					break;
				case 8253:
					a = "Weathering Road";
					break;
				case 8260:
					a = LL14(L"彼の地へ向かって", L"Toward That Land", L"Vers cette terre", L"Verso quella terra", L"Hacia esa tierra", L"? ?? ???", L"邁向那片土地", L"??? ??? ?????", L"К той земле", L"Jenem Land entgegen", L"Em direcao aquela terra", L"Naar dat land", L"Ku tamtej krainie", L"O Diyara Do?ru");
					break;
				case 8261:
					a = LL14(L"終焉の途へ", L"Toward the End", L"Vers la fin", L"Verso la fine", L"Hacia el final", L"??? ??", L"邁向終結", L"??? ???????", L"К концу", L"Dem Ende entgegen", L"Em direcao ao fim", L"Naar het einde", L"Ku ko?cowi", L"Sona Do?ru");
					break;
				case 8262:
					a = LL14(L"全てを識るもの -閃Ver.-", L"Omniscient -CS Ver.-", L"L'omniscient -CS Ver.-", L"L'onniscente -CS Ver.-", L"El omnisciente -CS Ver.-", L"?? ?? ?? ? -閃Ver.-", L"全知者 -閃Ver.-", L"?????? -CS Ver.-", L"Всеведущий -CS Ver.-", L"Der Allwissende -CS Ver.-", L"O onisciente -CS Ver.-", L"De alwetende -CS Ver.-", L"Wszechwiedz?cy -CS Ver.-", L"Her ?eyi Bilen -CS Ver.-");
					break;
				case 8263:
					a = LL14(L"たそがれ緑道", L"Twilight Green Path", L"Chemin vert du crepuscule", L"Sentiero verde del crepuscolo", L"Senda verde del crepusculo", L"??? ??", L"黄昏綠道", L"???? ????? ??????", L"Сумеречная зеленая тропа", L"Zwielichtiger gruner Pfad", L"Caminho verde do crepusculo", L"Groene schemerpad", L"Zielona ?cie?ka zmierzchu", L"Alacakaranl?k Ye?il Yolu");
					break;
				case 8311:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8350:
					a = LL14(L"アインヘル小要塞", L"Einhel Fortress", L"Forteresse d'Einhel", L"Fortezza di Einhel", L"Fortaleza de Einhel", L"Einhel ???", L"Einhel 小要塞", L"??? Einhel", L"Крепость Эйнхель", L"Einhel-Festung", L"Fortaleza de Einhel", L"Vesting Einhel", L"Twierdza Einhel", L"Einhel Kalesi");
					break;
				case 8351:
					a = LL14(L"伝承の裏で", L"Behind the Legend", L"Derriere la legende", L"Dietro la leggenda", L"Detras de la leyenda", L"??? ????", L"傳承的背後", L"??? ????????", L"За легендой", L"Hinter der Legende", L"Atras da lenda", L"Achter de legende", L"Za legend?", L"Efsanenin Arkas?nda");
					break;
				case 8352:
					a = "Unplanned Residue";
					break;
				case 8353:
					a = LL14(L"忘れられし幻夢の狭間 -閃Ver.-", L"Forgotten Phantasmal Gap -CS Ver.-", L"Ecart phantasmatique oublie -CS Ver.-", L"Divario fantasmatico dimenticato -CS Ver.-", L"Brecha fantasmal olvidada -CS Ver.-", L"??? ??? ?? -閃Ver.-", L"被遺忘的幻夢狹間 -閃Ver.-", L"?????? ???????? ??????? -CS Ver.-", L"Забытый призрачный разрыв -CS Ver.-", L"Vergessener phantasmagorischer Spalt -CS Ver.-", L"Fenda fantasmal esquecida -CS Ver.-", L"Vergeten fantoomkloof -CS Ver.-", L"Zapomniana fantastyczna szczelina -CS Ver.-", L"Unutulmu? Hayali Bo?luk -CS Ver.-");
					break;
				case 8354:
					a = LL14(L"幽世の気配", L"Atmosphere of the Netherworld", L"Atmosphere de l'au-dela", L"Atmosfera dell'oltretomba", L"Atmosfera del inframundo", L"??? ??", L"幽世之氣息", L"????? ?????? ??????", L"Атмосфера преисподней", L"Atmosphare der Unterwelt", L"Atmosfera do submundo", L"Sfeer van de onderwereld", L"Atmosfera za?wiatow", L"Obur Dunyan?n Havas?");
					break;
				case 8355:
					a = "solid as the Rock of JUNO";
					break;
				case 8356:
					a = LL14(L"地下に巣喰う", L"Nesting Underground", L"Nicher sous terre", L"Nidificare sottoterra", L"Anidando bajo tierra", L"??? ??? ??", L"盤據地下", L"??????? ??? ?????", L"Гнездование под землей", L"Unterirdisches Nisten", L"Aninhando-se no subsolo", L"Ondergronds nestelen", L"Gnie?d?enie si? pod ziemi?", L"Yeralt?ndaki Yuva");
					break;
				case 8359:
					a = "Spiral of Erebos";
					break;
				case 8360:
					a = LL14(L"鋼の障壁", L"Steel Barrier", L"Barriere d'acier", L"Barriera d'acciaio", L"Barrera de acero", L"??? ??", L"鋼鐵障壁", L"???? ??????", L"Стальной барьер", L"Stahlbarriere", L"Barreira de aco", L"Stalen barriere", L"Stalowa bariera", L"Celik Bariyer");
					break;
				case 8363:
					a = "Break In";
					break;
				case 8365:
					a = LL14(L"サングラール迷宮", L"Sanglar Maze", L"Labyrinthe de Sanglar", L"Labirinto di Sanglar", L"Laberinto de Sanglar", L"Sanglar ??", L"Sanglar 迷宮", L"????? Sanglar", L"Лабиринт Санглар", L"Sanglar-Labyrinth", L"Labirinto de Sanglar", L"Sanglar doolhof", L"Labirynt Sanglar", L"Sanglar Labirenti");
					break;
				case 8366:
					a = LL14(L"静けき森の魔女", L"Witch of the Silent Forest", L"Sorciere de la foret silencieuse", L"Strega della foresta silenziosa", L"Bruja del bosque silencioso", L"??? ?? ??", L"靜謐森林的魔女", L"????? ?????? ???????", L"Ведьма тихого леса", L"Hexe des stillen Waldes", L"Bruxa da floresta silenciosa", L"Heks van het stille woud", L"Wied?ma z cichego lasu", L"Sessiz Orman?n Cad?s?");
					break;
				case 8367:
					a = LL14(L"Mystic Core -閃Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -閃Ver.-", L"Mystic Core -閃Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-");
					break;
				case 8368:
					a = LL14(L"斉いし舞台", L"Unified Stage", L"Scene unifiee", L"Palcoscenico unificato", L"Escenario unificado", L"???? ??", L"齊整的舞台", L"?????? ??????", L"Единая сцена", L"Vereinte Buhne", L"Palco unificado", L"Verenigd podium", L"Zunifikowana scena", L"Birle?mi? Sahne");
					break;
				case 8369:
					a = LL14(L"シンクロニシティ #23", L"Synchronicity #23", L"Synchronicite #23", L"Sincronicita #23", L"Sincronicidad #23", L"?????? #23", L"共時性 #23", L"??????? #23", L"Синхронность #23", L"Synchronizitat #23", L"Sincronicidade #23", L"Synchroniciteit #23", L"Synchroniczno?? #23", L"E?zamanl?l?k #23");
					break;
				case 8371:
					a = LL14(L"世界の命運を賭けて", L"Betting on the World's Fate", L"Parier sur le destin du monde", L"Scommettendo sul destino del mondo", L"Apostando por el destino del mundo", L"??? ??? ??", L"賭上世界的命運", L"?????? ??? ???? ??????", L"Ставя на кон судьбу мира", L"Auf das Schicksal der Welt setzen", L"Apostando no destino do mundo", L"Inzetten op het lot van de wereld", L"Stawiaj?c na losy ?wiata", L"Dunyan?n Kaderi Uzerine Bahis");
					break;
				case 8372:
					a = "The End of -SAGA-";
					break;
				case 8429:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8450:
					a = "Brave Steel";
					break;
				case 8451:
					a = "Toughness!!";
					break;
				case 8452:
					a = LL14(L"剣戟怒涛", L"Sword and Lance Storm", L"Tempete d'epees et de lances", L"Tempesta di spade e lance", L"Tormenta de espadas y lanzas", L"????", L"劍戟怒濤", L"????? ????? ??????", L"Шторм мечей и копий", L"Schwert- und Lanzensturm", L"Tempestade de espadas e lancas", L"Zwaard- en lansstorm", L"Burza mieczy i w?oczni", L"K?l?c ve M?zrak F?rt?nas?");
					break;
				case 8453:
					a = "Proud Grudge";
					break;
				case 8454:
					a = LL14(L"チープ・トラップ", L"Cheap Trap", L"Piege bon marche", L"Trappola a buon mercato", L"Trampa barata", L"?? ??", L"便宜的陷?", L"?? ????", L"Дешевая ловушка", L"Billige Falle", L"Armadilha barata", L"Goedkope val", L"Tania pu?apka", L"Ucuz Tuzak");
					break;
				case 8455:
					a = "STEP AHEAD";
					break;
				case 8456:
					a = LL14(L"劣勢を挽回せよ！", L"Turn the Tide!", L"Inversez la tendance !", L"Inverti la rotta!", L"!Cambia la marea!", L"??? ????!", L"挽回劣勢！", L"???? ????????!", L"Переломи ход событий!", L"Das Blatt wenden!", L"Vire o jogo!", L"Keer het tij!", L"Odwro? losy!", L"Gidi?at? De?i?tir!");
					break;
				case 8457:
					a = "Abrupt Visitor";
					break;
				case 8458:
					a = LL14(L"行き着く先 -Opening Size-", L"Destination -Opening Size-", L"Destination -Opening Size-", L"Destinazione -Opening Size-", L"Destino -Opening Size-", L"???? ? -Opening Size-", L"抵達之處 -Opening Size-", L"?????? - Opening Size", L"Место назначения -Opening Size-", L"Zielort -Opening Size-", L"Destino -Opening Size-", L"Bestemming -Opening Size-", L"Miejsce docelowe -Opening Size-", L"Var?? Noktas? -Opening Size-");
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
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8466:
					a = "Erosion of Madness";
					break;
				case 8467:
					a = "DOOMSDAY TRANCE";
					break;
				case 8468:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
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
					a = LL14(L"古の盟約", L"Ancient Covenant", L"Ancienne alliance", L"Antico patto", L"Antiguo pacto", L"??? ??", L"古代盟約", L"????? ??????", L"Древний завет", L"Alter Bund", L"Antigo pacto", L"Oud verbond", L"Staro?ytne przymierze", L"Kadim Sozle?me");
					break;
				case 8476:
					a = LL14(L"七の相克 -EXCELLION KRIEG-", L"Seven Antagonisms -EXCELLION KRIEG-", L"Sept antagonismes -EXCELLION KRIEG-", L"Sette antagonismi -EXCELLION KRIEG-", L"Siete antagonismos -EXCELLION KRIEG-", L"?? ?? -EXCELLION KRIEG-", L"七之相克 -EXCELLION KRIEG-", L"???????? ?????? - EXCELLION KRIEG", L"Семь противостояний -EXCELLION KRIEG-", L"Sieben Antagonismen -EXCELLION KRIEG-", L"Sete antagonismos -EXCELLION KRIEG-", L"Zeven tegenstellingen -EXCELLION KRIEG-", L"Siedem antagonizmow -EXCELLION KRIEG-", L"Yedi Kar??tl?k -EXCELLION KRIEG-");
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
					a = LL14(L"授業は合同で", L"Joint Class", L"Cours commun", L"Classe congiunta", L"Clase conjunta", L"??? ????", L"聯合授課", L"??? ?????", L"Совместное занятие", L"Gemeinsamer Unterricht", L"Aula conjunta", L"Gezamenlijke les", L"Wspolna lekcja", L"Ortak Ders");
					break;
				case 8501:
					a = "Power or Technique";
					break;
				case 8502:
					a = "Briefing Time";
					break;
				case 8503:
					a = LL14(L"第II分校の日常", L"Daily Life at Branch II", L"Vie quotidienne a la Branche II", L"Vita quotidiana alla Branca II", L"Vida cotidiana en la Rama II", L"?II??? ??", L"第II分校的日常", L"?????? ??????? ?? ????? ??????", L"Будни во втором филиале", L"Alltag in Zweigstelle II", L"Vida cotidiana na Filial II", L"Dagelijks leven in Afdeling II", L"?ycie codzienne w Filii II", L"2. ?ubede Gunluk Ya?am");
					break;
				case 8504:
					a = LL14(L"充実したひととき", L"Satisfying Moment", L"Moment satisfaisant", L"Momento soddisfacente", L"Momento satisfactorio", L"??? ??", L"充實的時光", L"???? ?????", L"Насыщенный момент", L"Erfullter Moment", L"Momento gratificante", L"Bevredigend moment", L"Satysfakcjonuj?ca chwila", L"Tatmin Edici Bir An");
					break;
				case 8505:
					a = LL14(L"異端の研究者", L"Heretic Researcher", L"Chercheur heretique", L"Ricercatore eretico", L"Investigador heretico", L"??? ???", L"異端研究者", L"???? ??????", L"Исследователь-еретик", L"Haretischer Forscher", L"Pesquisador heretico", L"Ketters onderzoeker", L"Badacz heretycki", L"Sapk?n Ara?t?rmac?");
					break;
				case 8506:
					a = LL14(L"君に伝えたいこと", L"What I Want to Tell You", L"Ce que je veux te dire", L"Cio che voglio dirti", L"Lo que quiero decirte", L"??? ??? ?? ?", L"想傳達給?的事", L"?? ???? ?? ????? ??", L"То, что я хочу тебе сказать", L"Was ich dir sagen mochte", L"O que eu quero te dizer", L"Wat ik je wil vertellen", L"To, co chc? ci powiedzie?", L"Sana Soylemek ?stedi?im ?ey");
					break;
				case 8507: case 8508:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8509:
					a = LL14(L"張り詰めた思惑", L"Tense Speculation", L"Speculation tendue", L"Tesa speculazione", L"Especulacion tensa", L"??? ??", L"緊繃的意図", L"?????? ??????", L"Напряженное ожидание", L"Gespannte Spekulation", L"Especulacao tensa", L"Gespannen speculatie", L"Napi?te spekulacje", L"Gergin Bekleyi?");
					break;
				case 8510:
					a = LL14(L"混迷の対立", L"Chaotic Conflict", L"Conflit chaotique", L"Conflitto caotico", L"Conflicto caotico", L"??? ??", L"迷惘的對立", L"???? ?????", L"Хаотичный конфликт", L"Chaotischer Konflikt", L"Conflito caotico", L"Chaotisch conflict", L"Chaotyczny konflikt", L"Kaotik Cat??ma");
					break;
				case 8511:
					a = LL14(L"急転直下", L"Sudden Turn", L"Tournant soudain", L"Svolta improvvisa", L"Giro repentino", L"????", L"急轉直下", L"???? ?????", L"Внезапный поворот", L"Plotzliche Wendung", L"Reviravolta subita", L"Plotselinge wending", L"Nag?y zwrot", L"Ani Donu?");
					break;
				case 8512:
					a = LL14(L"蠢く陰謀", L"Writhing Conspiracy", L"Complot rampant", L"Cospirazione strisciante", L"Conspiracion reptante", L"???? ??", L"蠢動的陰謀", L"?????? ??????", L"Ползучий заговор", L"Sich windende Verschworung", L"Conspiracao rastejante", L"Kronkelende samenzwering", L"Wij?c si? spisek", L"Kaynayan Komplo");
					break;
				case 8513:
					a = LL14(L"託されたもの", L"Entrusted One", L"Celui a qui on a confie", L"Colui a cui e stato affidato", L"A quien se le confio", L"??? ?", L"被託付之物", L"????????", L"Вверенный", L"Der Anvertraute", L"O confiado", L"De toevertrouwde", L"Powierzony", L"Emanet Edilen");
					break;
				case 8514:
					a = LL14(L"羅刹の薫陶", L"Rasetsu's Guidance", L"L'influence de Rasetsu", L"La guida di Rasetsu", L"La guia de Rasetsu", L"???? ??", L"羅刹的教化", L"????? Rasetsu", L"Наставление Расецу", L"Rasetsus Fuhrung", L"Orientacao de Rasetsu", L"Rasetsu's begeleiding", L"Wskazowki Rasetsu", L"Rasetsu'nun Rehberli?i");
					break;
				case 8515:
					a = LL14(L"ハーメル -遺されたもの-", L"Hamel -What Was Left Behind-", L"Hamel -Ce qui a ete laisse-", L"Hamel -Cio che e rimasto-", L"Hamel -Lo que quedo atras-", L"?? ~??? ?~", L"哈梅爾 -遺留之物-", L"Hamel - ?? ????", L"Хамель -Что осталось позади-", L"Hamel -Was zuruckblieb-", L"Hamel -O que foi deixado para tras-", L"Hamel -Wat achterbleef-", L"Hamel -Co pozosta?o-", L"Hamel -Geride Kalanlar-");
					break;
				case 8516:
					a = LL14(L"Welcome Back! アーベントタイム(ラジオ)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (???)", L"Welcome Back! Evening Time (廣播)", L"Welcome Back! Evening Time (?????)", L"Welcome Back! Evening Time (радио)", L"Welcome Back! Evening Time (Radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (Radyo)");
					break;
				case 8517: case 8519:
					a = LL14(L"夏至祭", L"Summer Solstice Festival", L"Festival du solstice d'ete", L"Festival del solstizio d'estate", L"Festival del solsticio de verano", L"???", L"夏至祭", L"?????? ???????? ??????", L"Фестиваль летнего солнцестояния", L"Sommersonnenwendfest", L"Festival do solsticio de verao", L"Midzomerfestival", L"Festiwal przesilenia letniego", L"Yaz Gundonumu Festivali");
					break;
				case 8520:
					a = LL14(L"翡翠庭園", L"Jade Garden", L"Jardin de jade", L"Giardino di giada", L"Jardin de jade", L"?? ??", L"翡翠庭園", L"????? ?????", L"Нефритовый сад", L"Jade-Garten", L"Jardim de jade", L"Jade tuin", L"Jadeitowy ogrod", L"Ye?im Bahcesi");
					break;
				case 8521:
					a = LL14(L"初めての円舞曲", L"First Waltz", L"Premiere valse", L"Primo valzer", L"Primer vals", L"? ???", L"第一首圓舞曲", L"?????? ?????", L"Первый вальс", L"Erster Walzer", L"Primeira valsa", L"Eerste wals", L"Pierwszy walc", L"?lk Vals");
					break;
				case 8522:
					a = LL14(L"真打ち登場！", L"Headliner's Entrance!", L"Entree de la vedette !", L"Entrata del protagonista!", L"!Entrada del protagonista!", L"??? ??!", L"壓軸登場！", L"???? ?????!", L"Выход главной звезды!", L"Auftritt des Hauptactes!", L"Entrada da atracao principal!", L"Entree van de hoofdact!", L"Wej?cie gwiazdy wieczoru!", L"As?l Sanatc?n?n Giri?i!");
					break;
				case 8524:
					a = "Tragedy";
					break;
				case 8528:
					a = LL14(L"僅かな希望の先に", L"Beyond Slight Hope", L"Au-dela d'un mince espoir", L"Oltre una sottile speranza", L"Mas alla de una pequena esperanza", L"??? ?? ???", L"在微小的希望之後", L"?? ???? ??? ????", L"За хрупкой надеждой", L"Jenseits einer leisen Hoffnung", L"Alem de uma pequena esperanca", L"Voorbij een sprankje hoop", L"Poza nik?? nadziej?", L"Kucuk Bir Umudun Otesinde");
					break;
				case 8530:
					a = LL14(L"帰路へ", L"On the Road Home", L"Sur le chemin du retour", L"Sulla via di casa", L"En el camino a casa", L"???", L"歸途", L"?? ???? ??????", L"На пути домой", L"Auf dem Heimweg", L"No caminho para casa", L"Op weg naar huis", L"W drodze do domu", L"Eve Donu? Yolunda");
					break;
				case 8532:
					a = "Roots of Scar";
					break;
				case 8534:
					a = LL14(L"想い千里を走り", L"Feelings Run a Thousand Miles", L"Les sentiments parcourent mille lieues", L"I sentimenti corrono per mille miglia", L"Los sentimientos corren mil millas", L"??? ??? ??", L"思念奔馳千里", L"??????? ???? ??? ???", L"Чувства бегут за тысячи миль", L"Gefuhle eilen tausend Meilen", L"Sentimentos correm mil milhas", L"Gevoelens leggen duizend mijlen af", L"Uczucia biegn? tysi?c mil", L"Duygular Bin Mil Ko?ar");
					break;
				case 8536:
					a = LL14(L"光射す空の下で", L"Under the Shining Sky", L"Sous le ciel radieux", L"Sotto il cielo splendente", L"Bajo el cielo resplandeciente", L"? ??? ?? ????", L"在光芒照射的天空下", L"??? ?????? ???????", L"Под сияющим небом", L"Unter dem strahlenden Himmel", L"Sob o ceu brilhante", L"Onder de stralende hemel", L"Pod l?ni?cym niebem", L"I??ldayan Gokyuzu Alt?nda");
					break;
				case 8539:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8541:
					a = LL14(L"空を見上げて -Eliot Ver.-", L"Look Up at the Sky -Eliot Ver.-", L"Regarder le ciel -Eliot Ver.-", L"Guarda il cielo -Eliot Ver.-", L"Mira al cielo -Eliot Ver.-", L"??? ????? -Eliot Ver.-", L"仰望天空 -Eliot Ver.-", L"???? ??? ?????? -Eliot Ver.-", L"Посмотри на небо -Eliot Ver.-", L"Blick in den Himmel -Eliot Ver.-", L"Olhe para o ceu -Eliot Ver.-", L"Kijk naar de lucht -Eliot Ver.-", L"Spojrz w niebo -Eliot Ver.-", L"Gokyuzune Bak -Eliot Ver.-");
					break;
				case 8542: case 8543:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8544:
					a = "Little Rain";
					break;
				case 8545:
					a = LL14(L"暗雲", L"Dark Clouds", L"Nuages sombres", L"Nubi oscure", L"Nubes oscuras", L"???", L"暗雲", L"??? ?????", L"Темные тучи", L"Dunkle Wolken", L"Nuvens escuras", L"Donkere wolken", L"Ciemne chmury", L"Kara Bulutlar");
					break;
				case 8546:
					a = LL14(L"鐘、鳴り響く時", L"When the Bell Tolls", L"Quand la cloche sonne", L"Quando suona la campana", L"Cuando dobla la campana", L"?? ?? ?? ?", L"鐘聲響徹之時", L"????? ??? ?????", L"Когда бьет колокол", L"Wenn die Glocke lautet", L"Quando o sino toca", L"Wanneer de klok luidt", L"Kiedy bije dzwon", L"Canlar Cald???nda");
					break;
				case 8547:
					a = LL14(L"巨イナル黄昏", L"Giant Twilight", L"Crepuscule geant", L"Crepuscolo gigante", L"Crepusculo gigante", L"??? ??", L"巨大的黄昏", L"????? ???????", L"Великие сумерки", L"Riesige Dammerung", L"Crepusculo gigante", L"Gigantische schemering", L"Wielki zmierzch", L"Muazzam Alacakaranl?k");
					break;
				case 8548:
					a = LL14(L"あの日の約束", L"That Day's Promise", L"La promesse de ce jour-la", L"La promessa di quel giorno", L"La promesa de aquel dia", L"??? ??", L"那天的約定", L"??? ??? ?????", L"Обещание того дня", L"Das Versprechen von jenem Tag", L"A promessa daquele dia", L"De belofte van die dag", L"Obietnica tamtego dnia", L"O Gunku Soz");
					break;
				case 8551:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8553:
					a = "Sensitive Talk";
					break;
				case 8554:
					a = LL14(L"哀花", L"Mournful Flower", L"Fleur de deuil", L"Fiore di lutto", L"Flor de luto", L"??", L"哀花", L"???? ?????", L"Траурный цветок", L"Trauerblume", L"Flor de luto", L"Rouwbloem", L"?a?obny kwiat", L"Yas Cice?i");
					break;
				case 8555:
					a = "Feel at Home";
					break;
				case 8556:
					a = LL14(L"幾千万の夜を越えて", L"Beyond Countless Nights", L"Au-dela d'innombrables nuits", L"Oltre innumerevoli notti", L"Mas alla de incontables noches", L"??? ?を ???", L"跨越數千萬個夜?", L"??? ?????? ???????", L"Сквозь миллионы ночей", L"Jenseits von Millionen Nachten", L"Alem de milhoes de noites", L"Voorbij miljoenen nachten", L"Poza miliony nocy", L"Milyonlarca Gecenin Otesinde");
					break;
				case 8557: case 8558:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8559:
					a = LL14(L"優しき微睡み", L"Gentle Slumber", L"Sommeil paisible", L"Dolce sonno", L"Dulce sueno", L"??? ?", L"?柔的微睡", L"???? ????", L"Нежная дремота", L"Sanfter Schlummer", L"Sono suave", L"Zachte sluimer", L"?agodny sen", L"Nazik Uyku");
					break;
				case 8560:
					a = LL14(L"最悪の最善手", L"Best Move in the Worst Situation", L"Meilleur coup dans la pire situation", L"Mossa migliore nella peggiore situazione", L"Mejor jugada en la peor situacion", L"?? ?? ??", L"最壞情況中的最佳對策", L"???? ???? ?? ???? ???", L"Лучший ход в худшей ситуации", L"Bester Zug in der schlimmsten Lage", L"Melhor jogada na pior situacao", L"Beste zet in de slechtste situatie", L"Najlepszy ruch w najgorszej sytuacji", L"En Kotu Durumdaki En ?yi Hamle");
					break;
				case 8562:
					a = LL14(L"黒の真実", L"Black Truth", L"Verite noire", L"Verita nera", L"Verdad negra", L"?? ??", L"黑之真實", L"????? ?????", L"Черная правда", L"Schwarze Wahrheit", L"Verdade negra", L"Zwarte waarheid", L"Czarna prawda", L"Siyah Gercek");
					break;
				case 8563:
					a = LL14(L"いつでもそばに", L"Always by Your Side", L"Toujours a tes cotes", L"Sempre al tuo fianco", L"Siempre a tu lado", L"??? ??", L"永遠在身邊", L"?????? ??????", L"Всегда рядом", L"Immer an deiner Seite", L"Sempre ao seu lado", L"Altijd aan je zijde", L"Zawsze przy tobie", L"Daima Yan?nda");
					break;
				case 8564:
					a = LL14(L"その温もりは小さいけれど。", L"That warmth is small, but.", L"Cette chaleur est petite, mais.", L"Quel calore e piccolo, ma.", L"Ese calor es pequeno, pero.", L"? ??? ???.", L"那??暖雖小。", L"??? ????? ????? ???.", L"Это тепло мало, но.", L"Diese Warme ist klein, aber.", L"Aquele calor e pequeno, mas.", L"Die warmte is klein, maar.", L"To ciep?o jest ma?e, ale.", L"Bu s?cakl?k kucuk, ama.");
					break;
				case 8566:
					a = LL14(L"それでも前へ", L"Still Forward", L"Tout de meme vers l'avant", L"Ancora avanti", L"Aun asi, adelante", L"??? ???", L"即便如此依然向前", L"??? ???? ??? ??????", L"Все равно вперед", L"Trotzdem vorwarts", L"Ainda assim, em frente", L"Toch vooruit", L"Mimo to do przodu", L"Yine de ?leri");
					break;
				case 8570:
					a = LL14(L"想いひとつに", L"Hearts as One", L"C?urs unis", L"Cuori come uno", L"Corazones como uno", L"?? ???", L"心意合一", L"???? ?????", L"Сердца как одно", L"Herzen eins", L"Coracoes como um", L"Harten als een", L"Serca jako jedno", L"Kalpler Bir");
					break;
				case 8571:
					a = LL14(L"千年要塞", L"Millennium Fortress", L"Forteresse millenaire", L"Fortezza millenaria", L"Fortaleza milenaria", L"?? ??", L"千年要塞", L"??? ???????", L"Тысячелетняя крепость", L"Jahrtausendfestung", L"Fortaleza milenar", L"Millenniumvesting", L"Tysi?cletnia twierdza", L"Bin Y?ll?k Kale");
					break;
				case 8572:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8573:
					a = LL14(L"せめてこの夜に誓って", L"At Least Swear Tonight", L"Au moins, jure ce soir", L"Almeno giura stasera", L"Al menos jura esta noche", L"??? ? ?? ????", L"至少在今夜發誓", L"??? ????? ???? ??????", L"По крайней мере, поклянись сегодня", L"Schwore zumindest heute Nacht", L"Pelo menos jure esta noite", L"Zweer tenminste vanavond", L"Przynajmniej przysi?gnij dzi?", L"En Az?ndan Bu Gece Yemin Et");
					break;
				case 8574:
					a = "Constraint";
					break;
				case 8575:
					a = LL14(L"過ぎ去りし日々", L"Days Gone By", L"Jours passes", L"Giorni passati", L"Dias pasados", L"??? ??", L"逝去的日子", L"???? ???", L"Минувшие дни", L"Vergangene Tage", L"Dias passados", L"Voorbijgegane dagen", L"Minione dni", L"Gecip Giden Gunler");
					break;
				case 8576:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8577:
					a = LL14(L"それぞれの覚悟", L"Each One's Resolve", L"La resolution de chacun", L"La risoluzione di ognuno", L"La resolucion de cada uno", L"??? ??", L"各自的覺悟", L"????? ?? ????", L"Решимость каждого", L"Die Entschlossenheit jedes Einzelnen", L"A determinacao de cada um", L"Ieders eigen vastberadenheid", L"Determinacja ka?dego z nas", L"Her Birimizin Kararl?l???");
					break;
				case 8578:
					a = LL14(L"無明の闇の中で", L"In the Darkness", L"Dans les tenebres sans fin", L"Nell'oscurita eterna", L"En la oscuridad eterna", L"??? ?? ???", L"在無明之暗中", L"?? ?????? ??????", L"В вечной тьме", L"In ewiger Finsternis", L"Na escuridao eterna", L"In de eeuwige duisternis", L"W wiecznej ciemno?ci", L"Sonsuz Karanl?kta");
					break;
				case 8579:
					a = LL14(L"変わる世界 -闇の底から-", L"Changing World -From the Depths of Darkness-", L"Monde changeant -Du fond des tenebres-", L"Mondo che cambia -Dal profondo delle tenebre-", L"Mundo cambiante -Desde el fondo de la oscuridad-", L"??? ?? ~??? ????~", L"變化的世界 -從黑暗深處-", L"???? ????? - ?? ????? ??????", L"Меняющийся мир -Из глубин тьмы-", L"Sich wandelnde Welt -Aus den Tiefen der Finsternis-", L"Mundo em mudanca -Do fundo da escuridao-", L"Veranderende wereld -Uit de diepten van de duisternis-", L"Zmieniaj?cy si? ?wiat -Z g??bi ciemno?ci-", L"De?i?en Dunya -Karanl???n Derinliklerinden-");
					break;
				case 8600:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8601:
					a = LL14(L"ゲートイン", L"Gate In", L"Entree en piste", L"Ingresso ai cancelli", L"Entrada a gateras", L"??? ?", L"進入閘門", L"???? ???????", L"Вход в ворота", L"Einzug", L"Entrada no portao", L"Binnenkomst", L"Wjazd na bramk?", L"Giri?");
					break;
				case 8602:
					a = LL14(L"不明(空の軌跡)", L"Unknown(Sky)", L"Inconnu(Sky)", L"Sconosciuto(Sky)", L"Desconocido(Sky)", L"??(??)", L"不明(空之軌跡)", L"??? ?????(Sky)", L"Неизвестно(Sky)", L"Unbekannt(Sky)", L"Desconhecido(Sky)", L"Onbekend(Sky)", L"Nieznany(Sky)", L"Bilinmeyen(Sky)");
					break;
				case 8603:
					a = LL14(L"女神はいつも見ています", L"The Goddess is Always Watching", L"La deesse regarde toujours", L"La dea guarda sempre", L"La diosa siempre observa", L"??? ??? ?? ????", L"女神一直在注視著", L"?????? ????? ??????", L"Богиня всегда наблюдает", L"Die Gottin wacht immer", L"A deusa esta sempre olhando", L"De godin kijkt altijd toe", L"Bogini zawsze patrzy", L"Tanr?ca Daima ?zliyor");
					break;
				case 8604:
					a = LL14(L"不明(空の軌跡)", L"Unknown(Sky)", L"Inconnu(Sky)", L"Sconosciuto(Sky)", L"Desconocido(Sky)", L"??(??)", L"不明(空之軌跡)", L"??? ?????(Sky)", L"Неизвестно(Sky)", L"Unbekannt(Sky)", L"Desconhecido(Sky)", L"Onbekend(Sky)", L"Nieznany(Sky)", L"Bilinmeyen(Sky)");
					break;
				case 8605: case 8606: case 8608: case 8610: case 8611: case 8612:
				case 8613: case 8614: case 8616: case 8617: case 8618: case 8619:
				case 8620: case 8621:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
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
					a = LL14(L"巨竜目覚める", L"The Great Dragon Awakens", L"Le grand dragon s'eveille", L"Il grande drago si risveglia", L"El gran dragon despierta", L"?? ????", L"巨龍覺醒", L"?????? ?????? ??????", L"Великий дракон пробуждается", L"Der grose Drache erwacht", L"O grande dragao desperta", L"De grote draak ontwaakt", L"Wielki smok si? budzi", L"Buyuk Ejderha Uyan?yor");
					break;
				case 8715:
					a = LL14(L"未来へ。", L"To the Future.", L"Vers le futur.", L"Verso il futuro.", L"Hacia el futuro.", L"???.", L"往未來。", L"??? ????????.", L"В будущее.", L"In die Zukunft.", L"Para o futuro.", L"Naar de toekomst.", L"W przysz?o??.", L"Gelece?e.");
					break;
				case 8716:
					a = LL14(L"明日への軌跡 -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"???? ?? -Instrumental Ver.-", L"通向明天的軌跡 -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-");
					break;
				case 8717:
					a = "Deep Carnival";
					break;
				case 8718:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
					break;
				case 8719:
					a = "Chain Chain Chain!";
					break;
				case 8720:
					a = LL14(L"明日への軌跡", L"Trails to Tomorrow", L"Sillage vers demain", L"Tracce verso il domani", L"Estela hacia el manana", L"???? ??", L"通向明天的軌跡", L"?????? ??? ????", L"Пути в завтрашний день", L"Pfade nach morgen", L"Rastros para o amanha", L"Sporen naar morgen", L"?cie?ki do jutra", L"Yar?na Giden ?zler");
					break;
				case 8721:
					a = LL14(L"愛の詩(歌)", L"Poem of Love (vocal)", L"Poeme d'amour (vocal)", L"Poema d'amore (vocal)", L"Poema de amor (vocal)", L"??? ?(??)", L"愛之詩(歌)", L"????? ?? (?????)", L"Поэма о любви (вокал)", L"Liebesgedicht (Gesang)", L"Poema de amor (vocal)", L"Liefdesgedicht (vocaal)", L"Poemat mi?o?ci (wokal)", L"A?k ?iiri (vokal)");
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
					a = LL14(L"風よりも駿く", L"Swifter Than the Wind", L"Plus rapide que le vent", L"Piu veloce del vento", L"Mas rapido que el viento", L"???? ???", L"比風更迅捷", L"???? ?? ??????", L"Быстрее ветра", L"Schneller als der Wind", L"Mais rapido que o vento", L"Sneller dan de wind", L"Szybszy ni? wiatr", L"Ruzgardan Daha H?zl?");
					break;
				case 8803:
					a = "Brilliant Escape";
					break;
				case 8810: case 8811: case 8812: case 8910: case 8911: case 8912:
				case 8913: case 8916: case 8917: case 8918: case 8919: case 8920:
				case 8921:
					a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
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
		adpcmf.Open(fn, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);

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
		adpcmf.Open(filen, CFile::modeRead | CFile::typeBinary | CFile::shareDenyWrite, NULL);
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
		wavch=si1.dwChannels;
		wavbit=si1.dwSamplesPerSec;
		loop2=(int)(((float)loop2)/(44100.0f/((float)((wavch==2)?wavbit:(wavbit/2)))));
		data_size=oggsize=loop2*4;
		loop3=loop2;loop2=0;
		adbuf2=(char*)malloc(data_size+44100*10);
		if(adbuf2==0){wavwait=1;thend=1; fnn=LL14(L"メモリの確保に失敗しました。", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.", L"Memory allocation failed.");return;}
		og->m_time.SetRange(0,(data_size)/4,TRUE);
		lenl= 0;
		if(wav)free(wav);
		wav_start();
		Render();
		}else{wavwait=1;thend=1; fnn=LL14(L"ファイルが開けませんでした。", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.", L"Could not open file.");return;}
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
		wavbit = si.dwSamplesPerSec;
		loop2 = loop2 / (44100 / wavbit);
		data_size = oggsize = loop2 * 4;
		adbuf2 = (char*)calloc(data_size * 2 + 44100 * 30, 1);
		if (san2 == 1) loop2 = 225 * 44100;
		if (san2 == 2) loop2 = 247 * 44100;
		loop3 = loop2;
		if (san2)data_size = oggsize = loop2 * 4 + 44100 * 7;
		if (mode == -14 && filen == "42music.mp3")	loop2 = 0;
		if (adbuf2 == 0) {
			wavwait = 1; thend = 1; fnn = LL14(
				L"メモリの確保に失敗しました。",
				L"Memory allocation failed.",
				L"Echec de l'allocation memoire.",
				L"Allocazione della memoria fallita.",
				L"Error al asignar memoria.",
				L"??? ??? ??????.",
				L"内存分配失?。",
				L"??? ????? ???????.",
				L"Ошибка выделения памяти.",
				L"Speicherzuweisung fehlgeschlagen.",
				L"Falha na alocacao de memoria.",
				L"Geheugentoewijzing mislukt.",
				L"B??d przydzielania pami?ci.",
				L"Bellek tahsisi ba?ar?s?z."); return;
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
	uint16_t outBps = (uint16_t)((wavsam <= 0 || wavsam > 32) ? 16 : abs(wavsam));
	ConvertFloatToRawBytes(m_convertedPcmFloatData, outBps, wavch, outputRawBytesData);
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
		s1L = (int)((float)s1L * (float)savedata.kakuVal / 100.0f);
		s2L = (int)((float)s2L * (float)savedata.kakuVal / 100.0f);
		s1R = (int)((float)s1R * (float)savedata.kakuVal / 100.0f);
		s2R = (int)((float)s2R * (float)savedata.kakuVal / 100.0f);
		W16(dst, s2L); dst += 2;
		if (nCh >= 2) { W16(dst, s2R); dst += 2; }
		W16(dst, s1L); dst += 2;
		if (nCh >= 2) { W16(dst, s1R); dst += 2; }
		int dataBytes = blockAlign - preambleSize;
		for (int i = 0; i < dataBytes; i++) {
			unsigned char b = p[i];
			if (nCh >= 2) {
				process_nibble(b >> 4, &ideltaL, &s1L, &s2L, &coeffL);
				s1L = (int)((float)s1L * (float)savedata.kakuVal / 100.0f);
				W16(dst, s1L); dst += 2;
				process_nibble(b & 0x0F, &ideltaR, &s1R, &s2R, &coeffR);
				s1R = (int)((float)s1R * (float)savedata.kakuVal / 100.0f);
				W16(dst, s1R); dst += 2;
			}
			else {
				process_nibble(b >> 4, &ideltaL, &s1L, &s2L, &coeffL);
				s1L = (int)((float)s1L * (float)savedata.kakuVal / 100.0f);
				W16(dst, s1L); dst += 2;
				process_nibble(b & 0x0F, &ideltaL, &s1L, &s2L, &coeffL);
				s1L = (int)((float)s1L * (float)savedata.kakuVal / 100.0f);
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

int playwavwav(BYTE* bw, int old, int l1, int l2)
{
	playb += (l1 + l2) / (wavch * (wavsam / 8));
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
	int max_buffer_size = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3;
	int bytesPerSample = (wav_.m_info.wBitsPerSample + 7) / 8 * wav_.m_info.nChannels;
	int outBytesPerSample = (wavsam / 8) * wavch;
	if (poss4 <= cnt) {
		while (true) {
			int toRead = rr;
			if (bytesPerSample != outBytesPerSample) {
				toRead = (int)((__int64)rr * bytesPerSample / outBytesPerSample);
				toRead = (toRead / bytesPerSample) * bytesPerSample;
			}
			r = wav_.Render(bufkpi, toRead);
			if (r <= 0) {
				if (muon != 0) {
					if (savedata.saverenzoku == 0) { ZeroMemory(bufkpi, rr); muon--; }
					else endflg = 1;
				}
				break;
			}
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
					for (int ch = 0; ch < (int)wavch; ch++) {
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
						if (wavsam == 16) {
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
				if (poss4 > cnt) break;
			}
		}
	}
	cnt2 = (poss4 < cnt) ? poss4 : cnt;
	if (cnt2 > 0) {
		int to_read = cnt2;
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
	Int24* b24c = (Int24*)bw;
	short* b = (short*)bw;
	fade += fadeadd; if (fade < 0.0001) { fade = 0.0; fadeadd = 0; }
	if (wavsam == 24) {
		for (int i = 0; i < cnt2 / 3; i++) {
			int c4 = b24c[i];
			c4 = (int)((float)c4 * ((float)savedata.kakuVal / 100.0f));
			b24c[i] = c4;
			c4 = b24c[i];
			if (savedata.mp3 == 2) c4 = (int)((float)c4 * 2.0f);
			else if (savedata.mp3 == 4) c4 = (int)((float)c4 * 3.0f);
			else if (savedata.mp3 == 8) c4 = (int)((float)c4 * 4.0f);
			else if (savedata.mp3 == 16) c4 = (int)((float)c4 * 5.0f);
			if (c4 > 8388607) c4 = 8388607;
			if (c4 < -8388608) c4 = -8388608;
			b24c[i] = c4;
			{ float c4f; int c5; c5 = b24c[i]; c4f = (float)c5 * fade * fade; b24c[i] = (Int24)(int)c4f; }
		}
	}
	else {
		for (int i = 0; i < cnt2 / 2; i++) {
			int c = (int)b[i];
			c = (int)((float)c * ((float)savedata.kakuVal / 100.0f));
			b[i] = (short)c;
			c = (int)b[i];
			if (savedata.mp3 == 2) c = (int)((float)b[i] * 1.5f);
			else if (savedata.mp3 == 3) c = (int)((float)b[i] * 2.0f);
			else if (savedata.mp3 == 4) c = (int)((float)b[i] * 2.5f);
			else if (savedata.mp3 == 5) c = (int)((float)b[i] * 3.0f);
			if (c >= 32768) c = 32767;
			if (c <= -32767) c = -32766;
			b[i] = (short)c;
			c = b[i]; c = (short)(((float)c) * fade * fade); b[i] = c;
		}
	}
	if ((UINT)wl < (UINT)0x7fff0000) {
		if (cc1 == 1) cc.Write(bw, cnt2);
		wl += cnt2;
	}
	lenl += cnt2;
	return cnt2;
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
		if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
			f123.Close();
			if (IDYES == MessageBox(LL14(
				L"途中再生データが存在します。\n前回中断した部分から再生しますか？\nはい = 途中から再生\nいいえ = はじめから再生",
				L"Resume data exists.\nResume from where you left off?\nYes = Resume\nNo = Play from start",
				L"Des donnees de reprise existent.\nReprendre la ou vous vous etes arrete?\nOui = Reprendre\nNon = Jouer depuis le debut",
				L"Esistono dati di ripresa.\nRiprendere da dove ci si e fermati?\nSi = Riprendi\nNo = Riproduci dall'inizio",
				L"Existen datos de reanudacion.\n?Reanudar desde donde lo dejo?\nSi = Reanudar\nNo = Reproducir desde el inicio",
				L"?? ?? ???? ?????.\n??? ??? ???? ??????\n? = ???? ??\n??? = ???? ??",
				L"存在中途播放数据。\n是否从上次中断???播放？\n是 = 从中途播放\n否 = 从?播放",
				L"???? ?????? ???????.\n?? ???? ????????? ?? ??? ??????\n??? = ???????\n?? = ????? ?? ???????",
				L"Данные возобновления существуют.\nПродолжить с места остановки?\nДа = Продолжить\nНет = Играть с начала",
				L"Fortsetzungsdaten vorhanden.\nVon der Unterbrechungsstelle fortfahren?\nJa = Fortsetzen\nNein = Von Anfang abspielen",
				L"Dados de retomada existem.\nRetomar de onde parou?\nSim = Retomar\nNao = Reproduzir do inicio",
				L"Hervatgegevens aanwezig.\nHervatten waar u gebleven was?\nJa = Hervatten\nNee = Afspelen vanaf het begin",
				L"Istniej? dane wznowienia.\nWznowi? od miejsca przerwania?\nTak = Wznow\nNie = Odtworz od pocz?tku",
				L"Devam verisi mevcut.\nKald???n?z yerden devam edilsin mi?\nEvet = Devam et\nHay?r = Ba?tan oynat"),
				LL14(
					L"再生確認",
					L"Playback confirmation",
					L"Confirmation de lecture",
					L"Conferma riproduzione",
					L"Confirmacion de reproduccion",
					L"?? ??",
					L"播放??",
					L"????? ???????",
					L"Подтверждение воспроизведения",
					L"Wiedergabebestatigung",
					L"Confirmacao de reproducao",
					L"Afspeelbevestiging",
					L"Potwierdzenie odtwarzania",
					L"Oynatma onay?"),
				MB_YESNO)) {
				flggg = 1;
			}
			else {
				CFile::Remove(filen + _T(".save"));
			}
		}
		if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE && flggg == 1) {
			f123.Close();
			if (pGraphBuilder)pMainFrame1->plays2();
			if (pMediaControl) { for (int y = 0; y < 45; y++) { Sleep(10); DoEvent(); }pMediaControl->Run(); }
			if (mode == -10) {
				if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
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
				if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
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
		ps = 0; m_ps.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"?? ??", L"?停", L"????? ????", L"Пауза", L"Pause", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"));
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
		MessageBox(LL14(
			L"ファイルは1つだけドロップしてください。\nプレイリストが開いている時は複数でもokです。",
			L"Drop only one file.\nMultiple files ok when playlist is open.",
			L"Deposez un seul fichier.\nPlusieurs fichiers acceptes si la liste de lecture est ouverte.",
			L"Rilascia un solo file.\nPiu file accettati quando la playlist e aperta.",
			L"Suelte solo un archivo.\nSe aceptan varios archivos cuando la lista esta abierta.",
			L"??? ??? ?????.\n??????? ?? ?? ?? ?? ?? ok???.",
			L"?只?放一个文件。\n播放列表打??可以?放多个文件。",
			L"???? ????? ?????? ???.\n????? ?????? ?????? ??? ??? ????? ???????.",
			L"Перетащите только один файл.\nНесколько файлов допустимо, когда плейлист открыт.",
			L"Nur eine Datei ablegen.\nMehrere Dateien erlaubt, wenn die Wiedergabeliste geoffnet ist.",
			L"Solte apenas um arquivo.\nVarios arquivos aceitos quando a lista de reproducao esta aberta.",
			L"Zet slechts een bestand neer.\nMeerdere bestanden toegestaan als de afspeellijst open is.",
			L"Upu?? tylko jeden plik.\nWiele plikow dozwolone gdy lista odtwarzania jest otwarta.",
			L"Yaln?zca bir dosya b?rak?n.\nOynatma listesi ac?kken birden fazla dosya kabul edilir."),
			LL14(
				L"ogg/wav簡易プレイヤ",
				L"ogg/wav Simple Player",
				L"Lecteur Simple ogg/wav",
				L"Lettore Semplice ogg/wav",
				L"Reproductor Simple ogg/wav",
				L"ogg/wav ?? ????",
				L"ogg/wav?易播放器",
				L"???? ogg/wav ??????",
				L"Простой Плеер ogg/wav",
				L"ogg/wav Einfacher Player",
				L"Player Simples ogg/wav",
				L"Eenvoudige ogg/wav Speler",
				L"Prosty Odtwarzacz ogg/wav",
				L"ogg/wav Basit Oynat?c?"),
			MB_ICONEXCLAMATION);		CCustomDialog::OnDropFiles(hDropInfo);
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
			L"ほかのプログラムで開かれているためファイルが開けません",
			L"Cannot open file because another program has it open.",
			L"Impossible d'ouvrir le fichier car il est utilise par un autre programme.",
			L"Impossibile aprire il file perche e aperto da un altro programma.",
			L"No se puede abrir el archivo porque otro programa lo tiene abierto.",
			L"?? ?????? ?? ?? ??? ? ? ????.",
			L"文件无法打?，因?其他程序正在使用它。",
			L"?? ???? ??? ????? ??? ???????? ??? ???????.",
			L"Невозможно открыть файл, так как он используется другой программой.",
			L"Datei kann nicht geoffnet werden, da sie von einem anderen Programm geoffnet ist.",
			L"Nao e possivel abrir o arquivo pois outro programa o esta usando.",
			L"Kan bestand niet openen omdat een ander programma het in gebruik heeft.",
			L"Nie mo?na otworzy? pliku, poniewa? jest u?ywany przez inny program.",
			L"Dosya ba?ka bir program taraf?ndan ac?k oldu?undan ac?lam?yor."),
			LL14(
				L"ogg/wav簡易プレイヤ",
				L"ogg/wav Simple Player",
				L"Lecteur Simple ogg/wav",
				L"Lettore Semplice ogg/wav",
				L"Reproductor Simple ogg/wav",
				L"ogg/wav ?? ????",
				L"ogg/wav?易播放器",
				L"???? ogg/wav ??????",
				L"Простой Плеер ogg/wav",
				L"ogg/wav Einfacher Player",
				L"Player Simples ogg/wav",
				L"Eenvoudige ogg/wav Speler",
				L"Prosty Odtwarzacz ogg/wav",
				L"ogg/wav Basit Oynat?c?"),
			MB_ICONEXCLAMATION); 
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
	if ((ogg || adbuf2 || mod || wav || mode == 999) && mode != -2)
	{
		thn1 = TRUE;
		if (m_dsb)m_dsb->SetVolume(DSBVOLUME_MIN);
		if (ps == 1) {
			OnPause();
		}
		m_ps.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"?? ??", L"?停", L"????? ????", L"Пауза", L"Pause", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"));
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
		if (mode == 999) wav_.Close();
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
	m_lrc2.SetWindowText(LL14(L"歌詞(.lrc)が表示されます", L"Lyrics (.lrc) will be displayed here", L"Paroles (.lrc) affichees ici", L"Testi (.lrc) visualizzati qui", L"Letra (.lrc) mostrada aqui", L"??(.lrc)? ??? ?????", L"歌?(.lrc)将在此?示", L"????? (.lrc) ???? ???", L"Текст (.lrc) отображается здесь", L"Liedtext (.lrc) wird hier angezeigt", L"Letra (.lrc) exibida aqui", L"Songtekst (.lrc) wordt hier getoond", L"Teksty (.lrc) wy?wietlone tutaj", L"Soz (.lrc) burada goruntulenir"));
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
		if (mode == 999) wav_.Close();
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
	m_lrc2.SetWindowText(LL14(L"歌詞(.lrc)が表示されます", L"Lyrics (.lrc) will be displayed here", L"Paroles (.lrc) affichees ici", L"Testi (.lrc) visualizzati qui", L"Letra (.lrc) mostrada aqui", L"??(.lrc)? ??? ?????", L"歌?(.lrc)将在此?示", L"????? (.lrc) ???? ???", L"Текст (.lrc) отображается здесь", L"Liedtext (.lrc) wird hier angezeigt", L"Letra (.lrc) exibida aqui", L"Songtekst (.lrc) wordt hier getoond", L"Teksty (.lrc) wy?wietlone tutaj", L"Soz (.lrc) burada goruntulenir"));
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
	if (filen.Left(2) == L"★")		s.Format(LL14(
		L"file:動画",
		L"file:Video",
		L"file:Video",
		L"file:Video",
		L"file:Video",
		L"file:???",
		L"file:??",
		L"file:?????",
		L"file:Видео",
		L"file:Video",
		L"file:Video",
		L"file:Video",
		L"file:Wideo",
		L"file:Video"));
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
		s.Format(LL14(L"file:音声ファイル%s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s", L"file:Audio %s"), g);
	}
	if (mode == -2 || mode == -3) sss = filen.Right(filen.GetLength() - filen.ReverseFind('.') - 1);
	if (mode == -3) s.Format(LL14(
		L"file:kpiファイル(%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)",
		L"file:kpi (%s)"), sss);
	if (mode == -1) s.Format(LL14(
		L"file:oggファイル",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg",
		L"file:ogg"));
	if (mode == -2 && rate == 0.0) s.Format(LL14(
		L"file:音声ファイル(%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)",
		L"file:Audio (%s)"), sss);
	if (mode == -2 && rate != 0.0) s.Format(LL14(
		L"file:動画ファイル(%s)",
		L"file:Video (%s)",
		L"file:Video (%s)",
		L"file:Video (%s)",
		L"file:Video (%s)",
		L"file:??? (%s)",
		L"file:?? (%s)",
		L"file:????? (%s)",
		L"file:Видео (%s)",
		L"file:Video (%s)",
		L"file:Video (%s)",
		L"file:Video (%s)",
		L"file:Wideo (%s)",
		L"file:Video (%s)"), sss);
	if (mode == 30) s = LL14(
		L"file:空の軌跡 The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st",
		L"file:Sora no Kiseki The 1st");
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
		s.Format(LL14(L"rate:算出中……", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating...", L"rate:Calculating..."));
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
		s.Format(LL14(
			L"sample:不明",
			L"sample:Unknown",
			L"sample:Inconnu",
			L"sample:Sconosciuto",
			L"sample:Desconocido",
			L"sample:??",
			L"sample:不明",
			L"sample:??? ?????",
			L"sample:Неизвестно",
			L"sample:Unbekannt",
			L"sample:Desconhecido",
			L"sample:Onbekend",
			L"sample:Nieznany",
			L"sample:Bilinmiyor")); 
		moji(s, 1, 48, 0x7fffff);
		s.Format(LL14(
			L"sample:不明",
			L"sample:Unknown",
			L"sample:Inconnu",
			L"sample:Sconosciuto",
			L"sample:Desconocido",
			L"sample:??",
			L"sample:不明",
			L"sample:??? ?????",
			L"sample:Неизвестно",
			L"sample:Unbekannt",
			L"sample:Desconhecido",
			L"sample:Onbekend",
			L"sample:Nieznany",
			L"sample:Bilinmiyor")); 
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
	else if (mode == -8 || mode == -7 || mode == 999) {
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
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7 || mode == 999) {
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
			s.Format(LL14(L"Loop数:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d", L"Loop:%3d G:%3d:%02d.%02d"), loopcnt, tag, tbg, tcg);
		else
			s.Format(LL14(L"Loop数:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d", L"Loop:%3d G:%3d:%02d %02d"), loopcnt, tag, tbg, tcg);
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

	if (wavExportLoopCount > 0) {
		if (loopcnt >= wavExportLoopCount) OnButton5();
	}
	else if (pl && plw) {
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
				if (loopcnt >= wavExportLoopCount) OnButton5();
			}
			else {
				CString s; m_kaisuu.GetWindowText(s);
				if (loopcnt >= _tstoi(s)) OnButton5();
			}
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
			if (pl && plcnt >= pl->m_lc.GetItemCount()) plcnt = 0;
			endflg = 0;
			if (pl && plcnt < pl->m_lc.GetItemCount()) {
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
					if (plcnt >= pl->m_lc.GetItemCount()) plcnt = 0;
					endflg = 0;
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
				if (plcnt >= pl->m_lc.GetItemCount()) plcnt = 0;
				endflg = 0;
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
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return;
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
				L"ファイル又はフォルダがありません",
				L"No file or folder",
				L"Fichier ou dossier introuvable",
				L"File o cartella non trovati",
				L"Archivo o carpeta no encontrados",
				L"?? ?? ??? ????",
				L"找不到文件或文件?",
				L"?? ???? ??? ?? ????",
				L"Файл или папка не найдены",
				L"Datei oder Ordner nicht gefunden",
				L"Arquivo ou pasta nao encontrados",
				L"Bestand of map niet gevonden",
				L"Nie znaleziono pliku ani folderu",
				L"Dosya veya klasor bulunamad?");
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
	if (ret != 0) { fnn = LL14(
		L"ファイル又はフォルダがありません",
		L"No file or folder",
		L"Fichier ou dossier introuvable",
		L"File o cartella non trovati",
		L"Archivo o carpeta no encontrados",
		L"?? ?? ??? ????",
		L"找不到文件或文件?",
		L"?? ???? ??? ?? ????",
		L"Файл или папка не найдены",
		L"Datei oder Ordner nicht gefunden",
		L"Arquivo ou pasta nao encontrados",
		L"Bestand of map niet gevonden",
		L"Nie znaleziono pliku ani folderu",
		L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
				L"ファイル又はフォルダがありません",
				L"No file or folder",
				L"Fichier ou dossier introuvable",
				L"File o cartella non trovati",
				L"Archivo o carpeta no encontrados",
				L"?? ?? ??? ????",
				L"找不到文件或文件?",
				L"?? ???? ??? ?? ????",
				L"Файл или папка не найдены",
				L"Datei oder Ordner nicht gefunden",
				L"Arquivo ou pasta nao encontrados",
				L"Bestand of map niet gevonden",
				L"Nie znaleziono pliku ani folderu",
				L"Dosya veya klasor bulunamad?"); return; }
		ex = "_22";
	}
	else ex = "_44";
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
				L"ファイル又はフォルダがありません",
				L"No file or folder",
				L"Fichier ou dossier introuvable",
				L"File o cartella non trovati",
				L"Archivo o carpeta no encontrados",
				L"?? ?? ??? ????",
				L"找不到文件或文件?",
				L"?? ???? ??? ?? ????",
				L"Файл или папка не найдены",
				L"Datei oder Ordner nicht gefunden",
				L"Arquivo ou pasta nao encontrados",
				L"Bestand of map niet gevonden",
				L"Nie znaleziono pliku ani folderu",
				L"Dosya veya klasor bulunamad?"); return; }
		ex = "_22";
	}
	else ex = "_44";
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
				L"ファイル又はフォルダがありません",
				L"No file or folder",
				L"Fichier ou dossier introuvable",
				L"File o cartella non trovati",
				L"Archivo o carpeta no encontrados",
				L"?? ?? ??? ????",
				L"找不到文件或文件?",
				L"?? ???? ??? ?? ????",
				L"Файл или папка не найдены",
				L"Datei oder Ordner nicht gefunden",
				L"Arquivo ou pasta nao encontrados",
				L"Bestand of map niet gevonden",
				L"Nie znaleziono pliku ani folderu",
				L"Dosya veya klasor bulunamad?"); return; }
		ex = "_22";
	}
	else ex = "_44";
	if (ret != 0) {
		fnn = LL14(
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
						L"ファイル又はフォルダがありません",
						L"No file or folder",
						L"Fichier ou dossier introuvable",
						L"File o cartella non trovati",
						L"Archivo o carpeta no encontrados",
						L"?? ?? ??? ????",
						L"找不到文件或文件?",
						L"?? ???? ??? ?? ????",
						L"Файл или папка не найдены",
						L"Datei oder Ordner nicht gefunden",
						L"Arquivo ou pasta nao encontrados",
						L"Bestand of map niet gevonden",
						L"Nie znaleziono pliku ani folderu",
						L"Dosya veya klasor bulunamad?"); return; }
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
			L"ファイル又はフォルダがありません",
			L"No file or folder",
			L"Fichier ou dossier introuvable",
			L"File o cartella non trovati",
			L"Archivo o carpeta no encontrados",
			L"?? ?? ??? ????",
			L"找不到文件或文件?",
			L"?? ???? ??? ?? ????",
			L"Файл или папка не найдены",
			L"Datei oder Ordner nicht gefunden",
			L"Arquivo ou pasta nao encontrados",
			L"Bestand of map niet gevonden",
			L"Nie znaleziono pliku ani folderu",
			L"Dosya veya klasor bulunamad?"); return; }
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
		m_ps.SetWindowText(LL14(L"再開", L"Resume", L"Reprendre", L"Riprendi", L"Reanudar", L"??", L"恢?", L"???????", L"Продолжить", L"Fortsetzen", L"Retomar", L"Hervatten", L"Wznow", L"Surdur"));
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
		m_ps.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"?? ??", L"?停", L"????? ????", L"Пауза", L"Pause", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"));
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
		else if (mode == 999 && (filen.Right(4).MakeLower() == ".wav" || filen.Right(4) == ".WAV")) {
			modesub = 999;
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
			if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
				f123.Close();
				if (IDYES == MessageBox(LL14(
					L"途中再生データが存在します。\n前回中断した部分から再生しますか？\nはい = 途中から再生\nいいえ = はじめから再生",
					L"Resume data exists.\nResume from where you left off?\nYes = Resume\nNo = Play from start",
					L"Des donnees de reprise existent.\nReprendre la ou vous vous etes arrete?\nOui = Reprendre\nNon = Jouer depuis le debut",
					L"Esistono dati di ripresa.\nRiprendere da dove ci si e fermati?\nSi = Riprendi\nNo = Riproduci dall'inizio",
					L"Existen datos de reanudacion.\n?Reanudar desde donde lo dejo?\nSi = Reanudar\nNo = Reproducir desde el inicio",
					L"?? ?? ???? ?????.\n??? ??? ???? ??????\n? = ???? ??\n??? = ???? ??",
					L"存在中途播放数据。\n是否从上次中断???播放？\n是 = 从中途播放\n否 = 从?播放",
					L"???? ?????? ???????.\n?? ???? ????????? ?? ??? ??????\n??? = ???????\n?? = ????? ?? ???????",
					L"Данные возобновления существуют.\nПродолжить с места остановки?\nДа = Продолжить\nНет = Играть с начала",
					L"Fortsetzungsdaten vorhanden.\nVon der Unterbrechungsstelle fortfahren?\nJa = Fortsetzen\nNein = Von Anfang abspielen",
					L"Dados de retomada existem.\nRetomar de onde parou?\nSim = Retomar\nNao = Reproduzir do inicio",
					L"Hervatgegevens aanwezig.\nHervatten waar u gebleven was?\nJa = Hervatten\nNee = Afspelen vanaf het begin",
					L"Istniej? dane wznowienia.\nWznowi? od miejsca przerwania?\nTak = Wznow\nNie = Odtworz od pocz?tku",
					L"Devam verisi mevcut.\nKald???n?z yerden devam edilsin mi?\nEvet = Devam et\nHay?r = Ba?tan oynat"),
					LL14(
						L"再生確認",
						L"Playback confirmation",
						L"Confirmation de lecture",
						L"Conferma riproduzione",
						L"Confirmacion de reproduccion",
						L"?? ??",
						L"播放??",
						L"????? ???????",
						L"Подтверждение воспроизведения",
						L"Wiedergabebestatigung",
						L"Confirmacao de reproducao",
						L"Afspeelbevestiging",
						L"Potwierdzenie odtwarzania",
						L"Oynatma onay?"),
					MB_YESNO)) {
					flggg = 1;
				}
				else {
					CFile::Remove(filen + _T(".save"));
				}
			}
			if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE && flggg == 1) {
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
					if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
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
					if (f123.Open(filen + _T(".save"), CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
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
			ps = 0; m_ps.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"?? ??", L"?停", L"????? ????", L"Пауза", L"Pause", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"));
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
				if ((mode >= 10 && mode <= 21) || mode <= -10 || mode == 999) {
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
					else if (mode == 999) {
						hsc = 2;
						playb = curpos;
						if (playb < 0) playb = 0;
						if (ps == 0) {
							OnPause();
							ZeroMemory(bufwav3, sizeof(bufwav3));
							syukai = 1; syukai2 = 0;
							if (thn == FALSE) for (;;) { if (syukai2 == 1) break; DoEvent(); }
							if (wav_.Seek(playb) == FALSE) { fade1 = 1; if (thn == FALSE) { if (m_dsb) m_dsb->Stop(); } return; }
							poss = 0; sek = TRUE;
							cnt3 = 0;
							timer.SetEvent();
							syukai = 0;
							OnPause();
						}
						else {
							poss = 0; poss2 = 0; poss3 = 0; poss4 = 0; poss5 = 0; poss6 = 0;
							ZeroMemory(bufkpi, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
							ZeroMemory(bufkpi_, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
							ZeroMemory(bufkpi2, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
							ZeroMemory(bufkpi3, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
							ZeroMemory(bufkpi4, OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM * 3);
							if (wav_.Seek(playb) == FALSE) { return; }
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
				if ((mode >= 10 && mode <= 21) || mode <= -10 || mode == 999) {
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
				if ((mode >= 10 && mode <= 21) || mode <= -10 || mode == 999) {
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
			playb += wavbit * (wavch == 2 ? 10 : 5);
			if ((loop1 + loop2) < (int)playb && endf == 0) playb = (loop1 + loop2);
			if (mode != -10)m_time.SetPos((int)playb);
			if (pMainFrame1) {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit));
			}
			if ((mode >= 10 && mode <= 21) || mode <= -10 || mode == 999) {
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
			playb -= wavbit * (wavch == 2 ? 10 : 5);
			if ((loop1 + loop2) < (int)playb && endf == 0) playb = (loop1 + loop2);
			if (mode != -10)m_time.SetPos((int)playb);
			if (pMainFrame1) {
				pMainFrame1->seek((LONGLONG)(((float)((float)playb) * 10000000.0f) / (float)wavbit));
			}
			if ((mode >= 10 && mode <= 21) || mode <= -10 || mode == 999) {
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
	if ((mode >= 10 && mode <= 21) || mode <= -10 || mode == 999) {
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
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
#else
	if (ab.Open("oggYSEDbgm.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
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
			AfxMessageBox(LL14(
				L"Baseの起動に失敗しました",
				L"Failed to start Base",
				L"Echec du demarrage de Base",
				L"Avvio di Base fallito",
				L"Error al iniciar Base",
				L"Base ??? ??????",
				L"Base ??失?",
				L"??? ????? Base",
				L"Не удалось запустить Base",
				L"Base konnte nicht gestartet werden",
				L"Falha ao iniciar o Base",
				L"Kan Base niet starten",
				L"Nie uda?o si? uruchomi? Base",
				L"Base ba?lat?lamad?"));
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

	// アプリ非アクティブ時は必ずホットキーを解除する。
	// WM_ACTIVATEはフォーカスを失ったウィンドウにのみ送られるため、
	// mainiやpMainFrame1など他ウィンドウにフォーカスがある場合に
	// ホットキーが解除されないことがある。WM_ACTIVATEAPPは全トップレベル
	// ウィンドウに送られるため、アプリ全体の非アクティブを確実に検知できる。
	if (!bActive && ::IsWindow(GetSafeHwnd())) {
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

