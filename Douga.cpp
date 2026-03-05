// Douga.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "d3d9.h"
//#include "d3dtypes.h"
#include "ogg.h"
#include "oggDlg.h"
#include "Douga.h"
//#include "vfw.h"
//#include <digitalv.h>
//#include "resource.h"
//#include "objbase.h"
#include "dshow.h"
#include "evr9.h"
//#include "qedit.h"
#include "Dwmapi.h"
#include <Mtype.h>
#include <dvdmedia.h>
// 一般的な字幕メディアタイプ
static const GUID MEDIATYPE_Subtitle =
{ 0xe487eb20, 0x6aa4, 0x11d1, { 0xa1, 0x4d, 0x00, 0x20, 0xaf, 0xd7, 0x97, 0x67 } };

// LAV Splitterなどで使われるサブタイトルGUID
static const GUID MEDIASUBTYPE_UTF8 =
{ 0x87c0b230, 0x03a8, 0x4fdf, { 0x87, 0x07, 0xc4, 0x1a, 0xb6, 0x1e, 0x82, 0x25 } };

static const GUID MEDIASUBTYPE_SSA =
{ 0x3020560f, 0x255a, 0x4ddc, { 0x80, 0x6e, 0x6c, 0x5c, 0xc6, 0xdb, 0xd2, 0x17 } };

static const GUID MEDIASUBTYPE_ASS =
{ 0x326444f7, 0x686f, 0x47ff, { 0xa4, 0xb2, 0xc8, 0xc9, 0x63, 0x07, 0xb4, 0xc2 } };

static const GUID MEDIASUBTYPE_VOBSUB =
{ 0xc6b7f98c, 0xa555, 0x4ed4, { 0xa5, 0xa0, 0xa1, 0xf0, 0x53, 0x70, 0x86, 0x45 } };

typedef interface ISampleGrabberCB ISampleGrabberCB;
EXTERN_C const IID IID_ISampleGrabberCB;
MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85")
ISampleGrabberCB : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE SampleCB(
		double SampleTime,
		IMediaSample *pSample) = 0;

	virtual HRESULT STDMETHODCALLTYPE BufferCB(
		double SampleTime,
		BYTE *pBuffer,
		long BufferLen) = 0;

};

EXTERN_C const IID IID_ISampleGrabber;
MIDL_INTERFACE("6B652FFF-11FE-4fce-92AD-0266B5D7C78F")
ISampleGrabber : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE SetOneShot(
		BOOL OneShot) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetMediaType(
		const AM_MEDIA_TYPE *pType) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(
		AM_MEDIA_TYPE *pType) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(
		BOOL BufferThem) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(
		/* [out][in] */ long *pBufferSize,
		/* [out] */ long *pBuffer) = 0;

	virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(
		/* [retval][out] */ IMediaSample **ppSample) = 0;

	virtual HRESULT STDMETHODCALLTYPE SetCallback(
		ISampleGrabberCB *pCallback,
		long WhichMethodToCallback) = 0;

};
typedef interface IMediaDet IMediaDet;
EXTERN_C const IID IID_IMediaDet;
EXTERN_C const CLSID CLSID_MediaDet;
MIDL_INTERFACE("65BD0710-24D2-4ff7-9324-ED2E5D3ABAFA")
IMediaDet : public IUnknown
{
public:
	virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_Filter(
		/* [retval][out] */ IUnknown **pVal) = 0;

	virtual /* [helpstring][id][propput] */ HRESULT STDMETHODCALLTYPE put_Filter(
		/* [in] */ IUnknown *newVal) = 0;

	virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_OutputStreams(
		/* [retval][out] */ long *pVal) = 0;

	virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_CurrentStream(
		/* [retval][out] */ long *pVal) = 0;

	virtual /* [helpstring][id][propput] */ HRESULT STDMETHODCALLTYPE put_CurrentStream(
		/* [in] */ long newVal) = 0;

	virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_StreamType(
		/* [retval][out] */ GUID *pVal) = 0;

	virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_StreamTypeB(
		/* [retval][out] */ BSTR *pVal) = 0;

	virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_StreamLength(
		/* [retval][out] */ double *pVal) = 0;

	virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_Filename(
		/* [retval][out] */ BSTR *pVal) = 0;

	virtual /* [helpstring][id][propput] */ HRESULT STDMETHODCALLTYPE put_Filename(
		/* [in] */ BSTR newVal) = 0;

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetBitmapBits(
		double StreamTime,
		long *pBufferSize,
		char *pBuffer,
		long Width,
		long Height) = 0;

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE WriteBitmapBits(
		double StreamTime,
		long Width,
		long Height,
		BSTR Filename) = 0;

	virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_StreamMediaType(
		/* [retval][out] */ AM_MEDIA_TYPE *pVal) = 0;

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE GetSampleGrabber(
		/* [out] */ ISampleGrabber **ppVal) = 0;

	virtual /* [helpstring][id][propget] */ HRESULT STDMETHODCALLTYPE get_FrameRate(
		/* [retval][out] */ double *pVal) = 0;

	virtual /* [helpstring][id] */ HRESULT STDMETHODCALLTYPE EnterBitmapGrabMode(
		double SeekTime) = 0;

};





#define		RELEASE1(x)			{ if(x){ULONG r;for(r=1;;){r=x->Release();if(r==0)break;} x=NULL;} }
#define		RELEASE(x)			{ if (x != NULL) {x->Release(); x = NULL;} }
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif
extern save savedata;
/////////////////////////////////////////////////////////////////////////////
// CDouga

IMPLEMENT_DYNCREATE(CDouga, CFrameWnd)

CDouga::CDouga()
{
}

CDouga::~CDouga()
{
//	delete this;

}


BEGIN_MESSAGE_MAP(CDouga, CFrameWnd)
	ON_WM_TIMER()
	ON_WM_CONTEXTMENU()
	//{{AFX_MSG_MAP(CDouga)
	ON_WM_SIZING()
	ON_WM_SIZE()
	ON_WM_CLOSE()
	ON_WM_SHOWWINDOW()
	ON_COMMAND(ID_MENUITEM32771, OnMenuitem32771)
	ON_COMMAND(ID_MENUITEM32772, OnMenuitem32772)
	ON_COMMAND(ID_MENUITEM32773, OnMenuitem32773)
	ON_COMMAND(ID_ST1, OnST1)
	ON_COMMAND(ID_ST2, OnST2)
	ON_COMMAND(ID_ST3, OnST3)
	ON_COMMAND(ID_ST4, OnST4)
	ON_COMMAND(ID_ST5, OnST5)
	ON_COMMAND(ID_ST6, OnST6)
	ON_COMMAND(ID_ST7, OnST7)
	ON_COMMAND(ID_ST8, OnST8)
	ON_COMMAND(ID_ST9, OnST9)
	ON_COMMAND(ID_ST10, OnST10)
	ON_COMMAND(ID_ST11, OnST11)
	ON_COMMAND(ID_ST12, OnST12)
	ON_COMMAND(ID_ST13, OnST13)
	ON_COMMAND(ID_ST14, OnST14)
	ON_COMMAND(ID_ST15, OnST15)
	ON_COMMAND(ID_ST16, OnST16)
	ON_COMMAND(ID_ST17, OnST17)
	ON_COMMAND(ID_ST18, OnST18)
	ON_COMMAND(ID_ST19, OnST19)
	ON_COMMAND(ID_ST20, OnST20)
	ON_COMMAND(ID_ST21, OnST21)
	ON_COMMAND(ID_ST22, OnST22)
	ON_COMMAND(ID_ST23, OnST23)
	ON_COMMAND(ID_ST24, OnST24)
	ON_COMMAND(ID_ST25, OnST25)
	ON_COMMAND(ID_ST26, OnST26)
	ON_COMMAND(ID_ST27, OnST27)
	ON_COMMAND(ID_ST28, OnST28)
	ON_COMMAND(ID_ST29, OnST29)
	ON_COMMAND(ID_ST30, OnST30)
	ON_COMMAND(ID_ST31, OnST31)
	ON_COMMAND(ID_ST32, OnST32)
	ON_COMMAND(ID_ST33, OnST33)
	ON_COMMAND(ID_ST34, OnST34)
	ON_COMMAND(ID_ST35, OnST35)
	ON_COMMAND(ID_ST36, OnST36)
	ON_COMMAND(ID_ST37, OnST37)
	ON_COMMAND(ID_ST38, OnST38)
	ON_COMMAND(ID_ST39, OnST39)
	ON_COMMAND(ID_ST40, OnST40)
	ON_COMMAND(ID_MV1, OnMV1)
	ON_COMMAND(ID_MV2, OnMV2)
	ON_COMMAND(ID_MV3, OnMV3)
	ON_COMMAND(ID_MV4, OnMV4)
	ON_COMMAND(ID_MV5, OnMV5)
	ON_COMMAND(ID_MV6, OnMV6)
	ON_COMMAND(ID_MV7, OnMV7)
	ON_COMMAND(ID_MV8, OnMV8)
	ON_COMMAND(ID_MV9, OnMV9)
	ON_COMMAND(ID_MV10, OnMV10)
	ON_COMMAND(ID_ETC1, OnETC1)
	ON_COMMAND(ID_ETC2, OnETC2)
	ON_COMMAND(ID_ETC3, OnETC3)
	ON_COMMAND(ID_ETC4, OnETC4)
	ON_COMMAND(ID_ETC5, OnETC5)
	ON_COMMAND(ID_ETC6, OnETC6)
	ON_COMMAND(ID_ETC7, OnETC7)
	ON_COMMAND(ID_ETC8, OnETC8)
	ON_COMMAND(ID_ETC9, OnETC9)
	ON_COMMAND(ID_ETC10, OnETC10)
	ON_COMMAND(ID_ETC11, OnETC11)
	ON_COMMAND(ID_ETC12, OnETC12)
	ON_COMMAND(ID_ETC13, OnETC13)
	ON_COMMAND(ID_ETC14, OnETC14)
	ON_COMMAND(ID_ETC15, OnETC15)
	ON_COMMAND(ID_ETC16, OnETC16)
	ON_COMMAND(ID_ETC17, OnETC17)
	ON_COMMAND(ID_ETC18, OnETC18)
	ON_COMMAND(ID_ETC19, OnETC19)
	ON_COMMAND(ID_ETC20, OnETC20)
	ON_COMMAND(ID_ETC21, OnETC21)
	ON_COMMAND(ID_ETC22, OnETC22)
	ON_COMMAND(ID_ETC23, OnETC23)
	ON_COMMAND(ID_ETC24, OnETC24)
	ON_COMMAND(ID_ETC25, OnETC25)
	ON_COMMAND(ID_ETC26, OnETC26)
	ON_COMMAND(ID_ETC27, OnETC27)
	ON_COMMAND(ID_ETC28, OnETC28)
	ON_COMMAND(ID_ETC29, OnETC29)
	ON_COMMAND(ID_ETC30, OnETC30)
	ON_COMMAND(ID_ETC31, OnETC31)
	ON_COMMAND(ID_ETC32, OnETC32)
	ON_COMMAND(ID_ETC33, OnETC33)
	ON_COMMAND(ID_ETC34, OnETC34)
	ON_COMMAND(ID_ETC35, OnETC35)
	ON_COMMAND(ID_ETC36, OnETC36)
	ON_COMMAND(ID_ETC37, OnETC37)
	ON_COMMAND(ID_ETC38, OnETC38)
	ON_COMMAND(ID_ETC39, OnETC39)
	ON_COMMAND(ID_ETC40, OnETC40)
	ON_WM_PAINT()
	ON_WM_NCHITTEST()
	ON_WM_GETMINMAXINFO()
	ON_WM_MOUSEACTIVATE()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDOWN()
//	ON_WM_RBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_ERASEBKGND()
	//}}AFX_MSG_MAP
	ON_WM_WINDOWPOSCHANGING()
	ON_WM_WINDOWPOSCHANGED()
	ON_WM_KEYDOWN()
	ON_WM_RBUTTONDOWN()
//	ON_WM_NCLBUTTONDOWN()
	ON_WM_NCRBUTTONDOWN()
	ON_WM_MOUSEWHEEL()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_NCLBUTTONDBLCLK()
	ON_COMMAND(32775, &CDouga::On32775)
	ON_WM_DROPFILES()
	ON_WM_NCMOUSEMOVE()
	ON_WM_NCDESTROY()
	ON_WM_NCRBUTTONUP()
	ON_WM_RBUTTONUP()
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CDouga メッセージ ハンドラ
BOOL st12=FALSE;

HWND hMCIWnd;
RECT rc,rcm;
int si=0;
IGraphBuilder *pGraphBuilder = NULL;
IMediaControl *pMediaControl = NULL;
IMediaEvent* pMediaEvent = NULL;
IVideoWindow *pVideoWindow = NULL;
IBasicVideo *pBasicVideo = NULL;
IMediaSeeking *pMediaSeeking = NULL;
ICaptureGraphBuilder2 *pCaptureGraphBuilder2 = NULL;
IBaseFilter *pVmr9 = NULL;
IBaseFilter *pSource=NULL;
IBaseFilter *pSource1=NULL;
IBaseFilter *pSource2=NULL;

IBaseFilter   *pSourceFilter=NULL;
IBaseFilter   *pSplitter=NULL;
IBaseFilter   *pAviDecomp=NULL;
IBaseFilter   *pColour=NULL;
IBaseFilter   *pRenderer=NULL;
IBaseFilter   *pRenderer0=NULL;
IBaseFilter   *pRenderer0_=NULL;
IBaseFilter   *pRenderer1=NULL;
IBaseFilter   *pRenderer2=NULL;
IBaseFilter   *pACM=NULL;
IBaseFilter   *pDSRenderer=NULL;
IBaseFilter   *pDSRenderer2=NULL;
IBaseFilter   *pDSRenderer3=NULL;
IBaseFilter   *pDSRenderer4=NULL;
IBaseFilter   *pDSRenderer5=NULL;
IBaseFilter   *pDSRenderer6=NULL;
IBaseFilter   *pDSRenderer7=NULL;
IBaseFilter   *pDSRenderer8=NULL;
IBaseFilter   *pDSRenderer9=NULL;
IBaseFilter   *pDSRenderer10=NULL;
IBaseFilter   *prend=NULL;
IBaseFilter   *prenda=NULL;
IBaseFilter   *prenda2=NULL;
IBaseFilter   *prenda3=NULL;
IBaseFilter   *prenda4=NULL;
IBaseFilter   *prenda5=NULL;
IBaseFilter   *prenda6=NULL;
IBaseFilter   *prenda7=NULL;
IBaseFilter   *prenda8=NULL;
IBaseFilter   *prenda9=NULL;
IBaseFilter   *prenda10=NULL;
IFileSourceFilter *Haali=NULL;

IBasicAudio *pBasicAudio=NULL;
IMediaPosition *pMediaPosition=NULL;

IMFGetService *service=NULL;
IMFVideoDisplayControl *Vdc=NULL;
IQualProp *pop=NULL;
IMediaDet *vr=NULL;

IAMStreamSelect *iam = NULL;

BOOL ev=FALSE;

CString streamname[40];
CString streamname1[40];
CString streamname2[40];


extern WCHAR douga[2050];
extern save savedata;
extern int mode;

extern COggDlg *og;

BOOL CDouga::Create(HWND h)
{
	CString sClassName;
	sClassName = AfxRegisterWndClass(NULL ,
    LoadCursor(NULL, IDC_ARROW),
    (HBRUSH)::GetStockObject(BLACK_BRUSH),
    LoadIcon(AfxGetInstanceHandle(),
    MAKEINTRESOURCE(IDR_DOUGA)));

    int ret=CreateEx(WS_EX_OVERLAPPEDWINDOW|WS_EX_ACCEPTFILES,sClassName, LL14(L"mp3/m4a簡易プレイヤ(動画画面)", L"mp3/m4a Simple Player (video)", L"mp3/m4a Lecteur simple (vidéo)", L"mp3/m4a Player semplice (video)", L"mp3/m4a Reproductor simple (video)", L"mp3/m4a 간단 플레이어(동영상)", L"mp3/m4a简易播放器(视频)", L"mp3/m4a مشغل بسيط (فيديو)", L"mp3/m4a Простой плеер (видео)", L"mp3/m4a Einfacher Player (Video)", L"mp3/m4a Player simples (vídeo)", L"mp3/m4a Eenvoudige speler (video)", L"mp3/m4a Prosty odtwarzacz (wideo)", L"mp3/m4a Basit oynatıcı (video)"),
	  ((WS_OVERLAPPEDWINDOW)& ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_SYSMENU),
	0,0,640,360,NULL,NULL,NULL);
	if(ret==0) MessageBox(LL14(L"作成", L"Create", L"Créer", L"Crea", L"Crear", L"생성", L"创建", L"إنشاء", L"Создать", L"Erstellen", L"Criar", L"Maken", L"Utwórz", L"Oluştur"));
	ev=FALSE;
 
    ::GetWindowRect(this->GetSafeHwnd(), &rc);
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
//	savedata.gx=10;savedata.gy=10;savedata.p.top=0;savedata.p.left=0;
	if(savedata.gx==-10000){
			MoveWindow(10, 10,100,100,TRUE);
		}else{
			MoveWindow(savedata.gx,savedata.gy,100,100);
//			MoveWindow(10,10,100,100);
//			SetWindowPos(NULL, savedata.gx,savedata.gy,100, 100,   SWP_NOOWNERZORDER);
	}
	si=0;

	cdc0 = GetDC(); //new CClientDC(this);
	savedata.fs=0;
	dc.CreateCompatibleDC(NULL);
	bmp.CreateCompatibleBitmap(cdc0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN));
	dc.SelectObject(&bmp);
	dc.FillSolidRect(0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),RGB(0,0,0));
	ReleaseDC(cdc0);

	HMODULE hDLL;
	typedef DWORD (WINAPI *PFUNC)(UINT);
	PFUNC pFunc;
	hDLL=::LoadLibrary(_T("Dwmapi"));
	pFunc=(PFUNC)::GetProcAddress(hDLL,"DwmEnableComposition");

	if(pFunc){
		if(savedata.con){
			pFunc(DWM_EC_ENABLECOMPOSITION  );
		}else{
			pFunc(DWM_EC_DISABLECOMPOSITION );
		}
	}
	::FreeLibrary(hDLL);

	st12=0;
	return TRUE;
}
long DeviceID=-1;
int u1=0;
extern save savedata;
extern int spc;
int audionum;
HRESULT GetPin(IBaseFilter *pFilter,PIN_DIRECTION dir,IPin *&pPin,GUID majorType,WCHAR *name);
HRESULT CntPin(IBaseFilter *pFilter);
HRESULT ConnectFilter(IBaseFilter *pSrc,IBaseFilter *pDest,GUID majorType,WCHAR *name);

// ピンを取得する
HRESULT CntPin(IBaseFilter *pFilter){
	audionum=0;
	HRESULT retCode=E_FAIL;
	HRESULT hr=NOERROR;
	IEnumPins *e=NULL;
	IPin *pResult=NULL;
	if(pFilter)
		hr=pFilter->EnumPins(&e);
	if(FAILED(hr) || pFilter==NULL)
		return hr;
	FILTER_INFO filinfo;
	pFilter->QueryFilterInfo(&filinfo);
	while(e->Next(1, &pResult, NULL) == S_OK){
		PIN_DIRECTION PinDirThis;
		hr=pResult->QueryDirection(&PinDirThis);
		if (pResult!=NULL && SUCCEEDED(hr) && PinDirThis==PINDIR_OUTPUT){
			PIN_INFO info;
			pResult->QueryPinInfo(&info);
			{
				IEnumMediaTypes *em=NULL;
				AM_MEDIA_TYPE *amt;
				hr=pResult->EnumMediaTypes(&em);
				if(SUCCEEDED(hr)){
					while(em->Next(1,&amt,NULL)==S_OK){
						GUID mj=amt->majortype;
						// amt を解放
						if (amt->cbFormat != 0){
							CoTaskMemFree((PVOID)amt->pbFormat);
						}
						RELEASE(amt->pUnk);
						CoTaskMemFree(amt);
						if(_wcsnicmp(info.achName,L"Audio 1",14)==0){ audionum++;break;}
						if(_wcsnicmp(info.achName,L"Audio 2",14)==0){ audionum++;break;}
						if(_wcsnicmp(info.achName,L"Audio 3",14)==0){ audionum++;break;}
						if(_wcsnicmp(info.achName,L"Audio 4",14)==0){ audionum++;break;}
						if(_wcsnicmp(info.achName,L"Audio 5",14)==0){ audionum++;break;}
						if(_wcsnicmp(info.achName,L"Audio 6",14)==0){ audionum++;break;}
						if(_wcsnicmp(info.achName,L"Audio 7",14)==0){ audionum++;break;}
						if(_wcsnicmp(info.achName,L"Audio 8",14)==0){ audionum++;break;}
						if(_wcsnicmp(info.achName,L"Audio 9",14)==0){ audionum++;break;}
						if(_wcsnicmp(info.achName,L"Audio 10",16)==0){audionum++;break;}
						if (_wcsnicmp(info.achName, L"Audio 11", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 12", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 13", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 14", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 15", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 16", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 17", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 18", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 19", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 20", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 21", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 22", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 23", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 24", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 25", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 26", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 27", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 28", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 29", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 30", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 31", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 32", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 33", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 34", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 35", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 36", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 37", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 38", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 39", 16) == 0) { audionum++; break; }
						if (_wcsnicmp(info.achName, L"Audio 40", 16) == 0) { audionum++; break; }
					}
				}
				RELEASE(em);
			}
			if(retCode==S_OK)
				break;
		}
		RELEASE(pResult);
	}
	RELEASE(e);
	return retCode;
}

int au, etc;

DWORD CDouga::CntPin2(IAMStreamSelect* pFilter)
{
	if (!pFilter) return 0;

	DWORD i, totalCount;
	AM_MEDIA_TYPE* am;
	LPWSTR p;

	// ストリームマッピング情報を初期化
	streamMap.videoStart = -1;
	streamMap.videoCount = 0;
	streamMap.audioStart = -1;
	streamMap.audioCount = 0;
	streamMap.subtitleStart = -1;
	streamMap.subtitleCount = 0;

	// 配列をクリア
	for (int j = 0; j < 40; j++) {
		streamname[j] = "";
		streamname1[j] = "";
		streamname2[j] = "";
	}

	pFilter->Count(&totalCount);

	int videoIdx = 0, audioIdx = 0, subtitleIdx = 0;

	for (i = 0; i < totalCount; i++) {
		pFilter->Info(i, &am, NULL, NULL, NULL, &p, NULL, NULL);

		if (am->majortype == MEDIATYPE_Audio) {
			// 音声ストリーム
			if (streamMap.audioStart == -1) streamMap.audioStart = i;
			streamMap.audioCount++;
			if (audioIdx < 40) {
				streamname[audioIdx] = p;
				audioIdx++;
			}
		}
		else if (am->majortype == MEDIATYPE_Video) {
			// 映像ストリーム
			if (streamMap.videoStart == -1) streamMap.videoStart = i;
			streamMap.videoCount++;
			if (videoIdx < 40) {
				streamname1[videoIdx] = p;
				videoIdx++;
			}
		}
		else {
			// 字幕やその他のストリーム
			if (streamMap.subtitleStart == -1) streamMap.subtitleStart = i;
			streamMap.subtitleCount++;
			if (subtitleIdx < 40) {
				streamname2[subtitleIdx] = p;
				subtitleIdx++;
			}
		}

		CoTaskMemFree(p);
		DeleteMediaType(am);
		FreeMediaType(*am);
	}

	// 後方互換性のため、グローバル変数も設定
	au = streamMap.audioStart;
	etc = streamMap.subtitleStart;
	audionum = streamMap.audioCount;

	// デバッグ出力
	CString debug;
	debug.Format(L"Video: start=%d count=%d, Audio: start=%d count=%d, Subtitle: start=%d count=%d",
		streamMap.videoStart, streamMap.videoCount,
		streamMap.audioStart, streamMap.audioCount,
		streamMap.subtitleStart, streamMap.subtitleCount);
	OutputDebugString(debug);

	return streamMap.audioCount;
}

HRESULT GetPin(IBaseFilter *pFilter,PIN_DIRECTION dir,IPin *&pPin,GUID majorType,WCHAR *name){
	HRESULT retCode=E_FAIL;
	HRESULT hr=NOERROR;
	IEnumPins *e=NULL;
	IPin *pResult=NULL;
	if(pFilter)
		hr=pFilter->EnumPins(&e);
	if(FAILED(hr) || pFilter==NULL)
		return hr;
	FILTER_INFO filinfo;
	pFilter->QueryFilterInfo(&filinfo);
	while(e->Next(1, &pResult, NULL) == S_OK){
		PIN_DIRECTION PinDirThis;
		hr=pResult->QueryDirection(&PinDirThis);
		if (pResult!=NULL && SUCCEEDED(hr) && PinDirThis==dir){
			PIN_INFO info;
			pResult->QueryPinInfo(&info);
			if(dir==PINDIR_INPUT){
				pPin=pResult;
				retCode=S_OK;
			}else{
				IEnumMediaTypes *em=NULL;
				AM_MEDIA_TYPE *amt=NULL;
				hr=pResult->EnumMediaTypes(&em);
//				if(_wcsnicmp(info.achName,L"Video",5*2+2)>=0){
//					pPin=pResult;
//					retCode=S_OK;
//					break;
//				}
				if(SUCCEEDED(hr)){
					while(em->Next(1,&amt,NULL)==S_OK){
						GUID mj=amt->majortype;
						// amt を解放
						if (amt->cbFormat != 0){
							CoTaskMemFree((PVOID)amt->pbFormat);
						}
						RELEASE(amt->pUnk);
						CoTaskMemFree(amt);
						if(Haali==NULL || name==NULL){
							if(mj==majorType){
								pPin=pResult;
								retCode=S_OK;
								break;
							}
						}else{
							if(mj==majorType && _wcsnicmp(info.achName,name,14)==0){
								pPin=pResult;
								retCode=S_OK;
								break;
							}
						}
					}
				}
				RELEASE(em);
			}
			if(retCode==S_OK)
				break;
		}
		RELEASE(pResult);
	}
	RELEASE(e);
	return retCode;
}
// フィルタの同士を接続する
HRESULT ConnectFilter(IBaseFilter *pSrc,IBaseFilter *pDest,GUID majorType,WCHAR *name){
	HRESULT hr=-1;
	IPin *pPinOut=NULL;
	IPin *pPinIn=NULL;
	if(pSrc==NULL || pDest==NULL) return hr;
	hr=GetPin(pSrc,PINDIR_OUTPUT,pPinOut,majorType,name);
	if(FAILED(hr)){
		printf("Output pin is not found. : from %p to %p\n",pSrc,pDest);
		return hr;
	}
	hr=GetPin(pDest,PINDIR_INPUT,pPinIn,GUID_NULL,name);
	if(SUCCEEDED(hr)){
//		hr=pGraphBuilder->Connect(pPinIn,pPinOut);
		hr=pPinOut->Connect(pPinIn,NULL);
	}
	if(FAILED(hr)){
		printf("Failed Connecting. : from %p to %p\n",pSrc,pDest);
	}
	// 取得したピン インターフェイスを解放
	RELEASE(pPinIn);
	RELEASE(pPinOut);
	return hr;
}
	long p;
#include "AudioSelect.h"
int bit=0;
double rate;
int rateflg = 0;
extern DWORD videocnt3;
extern int wavch,wavbit;
CString s2;
#if WIN64
#else
//static const GUID MR_VIDEO_RENDER_SERVICE =     {0x1092a86c, 0xab1a, 0x459a, {0xa3, 0x36, 0x83, 0x1f, 0xbc, 0x4d, 0x11, 0xff} };
//static const IID IID_IMFVideoDisplayControl =   {0xa490b1e4, 0xab84, 0x4d31, {0xa1, 0xb2, 0x18, 0x1e, 0x03, 0xb1, 0x07, 0x7a} };
#endif

double CDouga::GetFrameRate(IGraphBuilder* pGraph)
{
	double frameRate = 0.0;

	// 方法1: ビデオデコーダーのメディアタイプから取得
	IEnumFilters* pEnum = NULL;
	IBaseFilter* pFilter = NULL;
	ULONG cFetched;

	if (SUCCEEDED(pGraph->EnumFilters(&pEnum)))
	{
		while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
		{
			IEnumPins* pEnumPins = NULL;
			if (SUCCEEDED(pFilter->EnumPins(&pEnumPins)))
			{
				IPin* pPin = NULL;
				while (pEnumPins->Next(1, &pPin, NULL) == S_OK)
				{
					PIN_DIRECTION dir;
					pPin->QueryDirection(&dir);

					if (dir == PINDIR_OUTPUT)
					{
						AM_MEDIA_TYPE* pmt = NULL;
						if (SUCCEEDED(pPin->ConnectionMediaType(pmt)))
						{
							if (pmt->majortype == MEDIATYPE_Video)
							{
								// VIDEOINFOHEADER から取得
								if (pmt->formattype == FORMAT_VideoInfo)
								{
									VIDEOINFOHEADER* pVih = (VIDEOINFOHEADER*)pmt->pbFormat;
									if (pVih->AvgTimePerFrame > 0)
									{
										frameRate = 10000000.0 / pVih->AvgTimePerFrame;
									}
								}
								// VIDEOINFOHEADER2 から取得（インターレース対応）
								else if (pmt->formattype == FORMAT_VideoInfo2)
								{
									VIDEOINFOHEADER2* pVih2 = (VIDEOINFOHEADER2*)pmt->pbFormat;
									if (pVih2->AvgTimePerFrame > 0)
									{
										frameRate = 10000000.0 / pVih2->AvgTimePerFrame;
									}
								}
							}
							DeleteMediaType(pmt);
						}
					}
					pPin->Release();

					if (frameRate > 0.0) break;
				}
				pEnumPins->Release();
			}
			pFilter->Release();

			if (frameRate > 0.0) break;
		}
		pEnum->Release();
	}

	// 方法2: フレームレートが取得できなかった場合はIMediaDetを使用
	if (frameRate == 0.0)
	{
		IMediaDet* pDet = NULL;
		if (SUCCEEDED(CoCreateInstance(CLSID_MediaDet, NULL, CLSCTX_INPROC,
			IID_IMediaDet, (LPVOID*)&pDet)))
		{
			if (SUCCEEDED(pDet->put_Filename(douga)))
			{
				long streams = 0;
				pDet->get_OutputStreams(&streams);

				for (long i = 0; i < streams; i++)
				{
					pDet->put_CurrentStream(i);
					double tempRate = 0.0;
					if (SUCCEEDED(pDet->get_FrameRate(&tempRate)) && tempRate > 0.0)
					{
						frameRate = tempRate;
						break;
					}
				}
			}
			pDet->Release();
		}
	}

	return frameRate;
}


// GetStreamInfo関数を簡略化して、既存のiamを使用
BOOL CDouga::GetStreamInfo(IGraphBuilder* pGraph, std::vector<StreamInfo>& audioStreams,
	std::vector<StreamInfo>& videoStreams, std::vector<StreamInfo>& subtitleStreams)
{
	audioStreams.clear();
	videoStreams.clear();
	subtitleStreams.clear();

	// 既にiamがあるなら、それを使う
	if (!iam) return FALSE;

	DWORD streamCount = 0;
	iam->Count(&streamCount);

	for (DWORD i = 0; i < streamCount; i++)
	{
		AM_MEDIA_TYPE* pmt = NULL;
		DWORD flags = 0;
		LCID lcid = 0;
		DWORD group = 0;
		LPWSTR pszName = NULL;
		IUnknown* pObject = NULL;
		IUnknown* pUnknown = NULL;

		if (SUCCEEDED(iam->Info(i, &pmt, &flags, &lcid, &group,
			&pszName, &pObject, &pUnknown)))
		{
			StreamInfo info;
			info.streamIndex = i;
			info.majorType = pmt->majortype;
			if (pszName) info.name = pszName;

			// 言語情報を取得
			if (lcid != 0)
			{
				WCHAR langName[256];
				if (GetLocaleInfo(lcid, LOCALE_SENGLANGUAGE, langName, 256) > 0)
				{
					info.language = langName;
				}
			}

			// ストリームの種類で分類
			if (pmt->majortype == MEDIATYPE_Audio)
			{
				audioStreams.push_back(info);
			}
			else if (pmt->majortype == MEDIATYPE_Video)
			{
				videoStreams.push_back(info);
			}
			else
			{
				// その他（字幕など）
				subtitleStreams.push_back(info);
			}

			if (pszName) CoTaskMemFree(pszName);
			if (pObject) pObject->Release();
			if (pUnknown) pUnknown->Release();
			DeleteMediaType(pmt);
		}
	}

	return (audioStreams.size() > 0 || videoStreams.size() > 0 || subtitleStreams.size() > 0);
}

// ピンを直接列挙してストリーム情報を取得
BOOL CDouga::EnumeratePinsForStreams(IGraphBuilder* pGraph,
	std::vector<StreamInfo>& audioStreams,
	std::vector<StreamInfo>& videoStreams,
	std::vector<StreamInfo>& subtitleStreams)
{
	IEnumFilters* pEnum = NULL;
	IBaseFilter* pFilter = NULL;
	ULONG cFetched;
	DWORD audioIndex = 0, videoIndex = 0, subtitleIndex = 0;

	if (SUCCEEDED(pGraph->EnumFilters(&pEnum)))
	{
		while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
		{
			FILTER_INFO filterInfo;
			pFilter->QueryFilterInfo(&filterInfo);
			CString filterName = filterInfo.achName;

			// デコーダーやレンダラーは除外
			if (filterName.Find(L"Decoder") == -1 &&
				filterName.Find(L"Renderer") == -1)
			{
				IEnumPins* pEnumPins = NULL;
				if (SUCCEEDED(pFilter->EnumPins(&pEnumPins)))
				{
					IPin* pPin = NULL;
					while (pEnumPins->Next(1, &pPin, NULL) == S_OK)
					{
						PIN_DIRECTION dir;
						pPin->QueryDirection(&dir);

						if (dir == PINDIR_OUTPUT)
						{
							PIN_INFO pinInfo;
							pPin->QueryPinInfo(&pinInfo);

							IEnumMediaTypes* pEnumMT = NULL;
							if (SUCCEEDED(pPin->EnumMediaTypes(&pEnumMT)))
							{
								AM_MEDIA_TYPE* pmt = NULL;
								if (pEnumMT->Next(1, &pmt, NULL) == S_OK)
								{
									StreamInfo info;
									info.majorType = pmt->majortype;
									info.name = pinInfo.achName;

									if (pmt->majortype == MEDIATYPE_Audio)
									{
										info.streamIndex = audioIndex++;
										audioStreams.push_back(info);
									}
									else if (pmt->majortype == MEDIATYPE_Video)
									{
										info.streamIndex = videoIndex++;
										videoStreams.push_back(info);
									}
									else if (pmt->majortype == MEDIATYPE_Subtitle ||
										pmt->majortype == MEDIATYPE_Text)
									{
										info.streamIndex = subtitleIndex++;
										subtitleStreams.push_back(info);
									}

									DeleteMediaType(pmt);
								}
								pEnumMT->Release();
							}

							if (pinInfo.pFilter) pinInfo.pFilter->Release();
						}
						pPin->Release();
					}
					pEnumPins->Release();
				}
			}

			if (filterInfo.pGraph) filterInfo.pGraph->Release();
			pFilter->Release();
		}
		pEnum->Release();
	}

	return (audioStreams.size() > 0 || videoStreams.size() > 0 || subtitleStreams.size() > 0);
}

static const GUID CLSID_VSFilter =
{ 0x9852A670, 0xF845, 0x491B, { 0x9B, 0xE6, 0xEB, 0xD8, 0x41, 0xB8, 0xA6, 0x13 } };

static const GUID CLSID_LAVSplitterSource =
{ 0xB98D13E7, 0x55DB, 0x4385, { 0xA3, 0x3D, 0x09, 0xFD, 0x1B, 0xA2, 0x63, 0x38 } };

void DumpMediaType(AM_MEDIA_TYPE* pmt)
{
	CString msg;
	msg.Format(L"  MajorType: %08X-%04X-%04X\n",
		pmt->majortype.Data1, pmt->majortype.Data2, pmt->majortype.Data3);
	OutputDebugString(msg);
	msg.Format(L"  SubType: %08X-%04X-%04X\n",
		pmt->subtype.Data1, pmt->subtype.Data2, pmt->subtype.Data3);
	OutputDebugString(msg);
}

// 字幕ピンをDirectVobSubに接続する関数
void CDouga::ConnectSubtitleToVSFilter(IGraphBuilder* pGraph, IBaseFilter* pVSFilter)
{
	if (!pGraph || !pVSFilter) return;

	OutputDebugString(L"=== ConnectSubtitleToVSFilter Start ===\n");

	IBaseFilter* pSourceFilter = NULL;
	IBaseFilter* pVideoDecoder = NULL;
	IPin* pSubtitlePin = NULL;
	IPin* pTextInput = NULL;
	IPin* pVideoDecoderOut = NULL;
	IPin* pVSInput = NULL;
	IPin* pVSOutput = NULL;
	HRESULT hr;
	CString msg;

	// ソースフィルタとビデオデコーダを探す
	IEnumFilters* pEnum = NULL;
	if (SUCCEEDED(pGraph->EnumFilters(&pEnum)))
	{
		IBaseFilter* pFilter = NULL;
		ULONG cFetched;

		while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
		{
			FILTER_INFO filterInfo;
			pFilter->QueryFilterInfo(&filterInfo);
			CString filterName = filterInfo.achName;

			if (filterName.Find(L":\\") != -1)
			{
				pSourceFilter = pFilter;
				pSourceFilter->AddRef();
			}

			if (filterName.Find(L"Video Decoder") != -1)
			{
				pVideoDecoder = pFilter;
				pVideoDecoder->AddRef();
			}

			if (filterInfo.pGraph) filterInfo.pGraph->Release();
			pFilter->Release();
		}
		pEnum->Release();
	}

	if (!pSourceFilter || !pVideoDecoder)
	{
		OutputDebugString(L"Source or decoder not found\n");
		goto cleanup;
	}

	// 1. ソースから字幕ピンを探す
	IEnumPins* pEnumPins = NULL;
	if (SUCCEEDED(pSourceFilter->EnumPins(&pEnumPins)))
	{
		IPin* pPin = NULL;
		while (pEnumPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);

			if (dir == PINDIR_OUTPUT)
			{
				PIN_INFO pinInfo;
				pPin->QueryPinInfo(&pinInfo);
				CString pinName = pinInfo.achName;
				pinName.MakeLower();

				if (pinName.Find(L"subtitle") != -1)
				{
					IPin* pConnected = NULL;
					if (pPin->ConnectedTo(&pConnected) != S_OK)
					{
						pSubtitlePin = pPin;
						pSubtitlePin->AddRef();
						OutputDebugString(L"Found subtitle output pin\n");
					}
					else
					{
						pConnected->Release();
					}
				}

				if (pinInfo.pFilter) pinInfo.pFilter->Release();
			}
			pPin->Release();
		}
		pEnumPins->Release();
	}

	// 2. ビデオデコーダのIn TextピンとOutピンを探す
	pEnumPins = NULL;
	if (SUCCEEDED(pVideoDecoder->EnumPins(&pEnumPins)))
	{
		IPin* pPin = NULL;
		while (pEnumPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);
			PIN_INFO pinInfo;
			pPin->QueryPinInfo(&pinInfo);
			CString pinName = pinInfo.achName;
			pinName.MakeLower();

			if (dir == PINDIR_INPUT && pinName.Find(L"text") != -1)
			{
				IPin* pConnected = NULL;
				if (pPin->ConnectedTo(&pConnected) != S_OK)
				{
					pTextInput = pPin;
					pTextInput->AddRef();
					OutputDebugString(L"Found In Text pin\n");
				}
				else
				{
					pConnected->Release();
				}
			}
			else if (dir == PINDIR_OUTPUT)
			{
				pVideoDecoderOut = pPin;
				pVideoDecoderOut->AddRef();
				OutputDebugString(L"Found video decoder output\n");
			}

			if (pinInfo.pFilter) pinInfo.pFilter->Release();
			pPin->Release();
		}
		pEnumPins->Release();
	}

	// 3. DirectVobSubのピンを探す
	pEnumPins = NULL;
	if (SUCCEEDED(pVSFilter->EnumPins(&pEnumPins)))
	{
		IPin* pPin = NULL;
		while (pEnumPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);

			if (dir == PINDIR_INPUT)
			{
				pVSInput = pPin;
				pVSInput->AddRef();
			}
			else if (dir == PINDIR_OUTPUT)
			{
				pVSOutput = pPin;
				pVSOutput->AddRef();
			}

			pPin->Release();
		}
		pEnumPins->Release();
	}

	// 接続開始

	if (pSubtitlePin)
	{
		OutputDebugString(L"Subtitle pin media types:\n");
		IEnumMediaTypes* pEnum = NULL;
		if (SUCCEEDED(pSubtitlePin->EnumMediaTypes(&pEnum)))
		{
			AM_MEDIA_TYPE* pmt = NULL;
			while (pEnum->Next(1, &pmt, NULL) == S_OK)
			{
				DumpMediaType(pmt);
				DeleteMediaType(pmt);
			}
			pEnum->Release();
		}
	}

	// In Textピンが受け入れるメディアタイプを確認
	if (pTextInput)
	{
		OutputDebugString(L"In Text pin accepts:\n");
		IEnumMediaTypes* pEnum = NULL;
		if (SUCCEEDED(pTextInput->EnumMediaTypes(&pEnum)))
		{
			AM_MEDIA_TYPE* pmt = NULL;
			while (pEnum->Next(1, &pmt, NULL) == S_OK)
			{
				DumpMediaType(pmt);
				DeleteMediaType(pmt);
			}
			pEnum->Release();
		}
	}

	// ステップ1: Subtitle → ffdshow の In Text
	if (pSubtitlePin && pTextInput)
	{
		// まずIntelligent Connectを試す
		hr = pGraph->Connect(pSubtitlePin, pTextInput);
		msg.Format(L"Subtitle -> In Text (Connect): 0x%08X\n", hr);
		OutputDebugString(msg);

		if (FAILED(hr))
		{
			// 失敗したらConnectDirectも試す
			hr = pGraph->ConnectDirect(pSubtitlePin, pTextInput, NULL);
			msg.Format(L"Subtitle -> In Text (ConnectDirect): 0x%08X\n", hr);
			OutputDebugString(msg);
		}
	}

	// ステップ2: ffdshow Out と VMR の間に DirectVobSub を挿入
	if (pVideoDecoderOut && pVSInput && pVSOutput)
	{
		IPin* pRendererInput = NULL;

		// 現在の接続先を取得
		if (pVideoDecoderOut->ConnectedTo(&pRendererInput) == S_OK)
		{
			OutputDebugString(L"Inserting DirectVobSub into video path...\n");

			// 切断
			pVideoDecoderOut->Disconnect();
			pRendererInput->Disconnect();

			// ffdshow Out → DirectVobSub Input
			hr = pGraph->ConnectDirect(pVideoDecoderOut, pVSInput, NULL);
			if (FAILED(hr)) hr = pGraph->Connect(pVideoDecoderOut, pVSInput);
			msg.Format(L"Decoder Out -> VSFilter In: 0x%08X\n", hr);
			OutputDebugString(msg);

			// DirectVobSub Output → Renderer
			if (SUCCEEDED(hr))
			{
				hr = pGraph->ConnectDirect(pVSOutput, pRendererInput, NULL);
				if (FAILED(hr)) hr = pGraph->Connect(pVSOutput, pRendererInput);
				msg.Format(L"VSFilter Out -> Renderer: 0x%08X\n", hr);
				OutputDebugString(msg);
			}

			pRendererInput->Release();
		}
	}

cleanup:
	if (pSubtitlePin) pSubtitlePin->Release();
	if (pTextInput) pTextInput->Release();
	if (pVideoDecoderOut) pVideoDecoderOut->Release();
	if (pVSInput) pVSInput->Release();
	if (pVSOutput) pVSOutput->Release();
	if (pSourceFilter) pSourceFilter->Release();
	if (pVideoDecoder) pVideoDecoder->Release();

	OutputDebugString(L"=== ConnectSubtitleToVSFilter End ===\n");
}


void CDouga::ReplaceSourceWithLAV(IGraphBuilder* pGraph, LPCWSTR filename)
{
	OutputDebugString(L"=== ReplaceSourceWithLAV Start ===\n");

	IBaseFilter* pOldSource = NULL;

	// 現在のソースフィルタを探す
	IEnumFilters* pEnum = NULL;
	if (SUCCEEDED(pGraph->EnumFilters(&pEnum)))
	{
		IBaseFilter* pFilter = NULL;
		ULONG cFetched;

		while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
		{
			FILTER_INFO filterInfo;
			pFilter->QueryFilterInfo(&filterInfo);
			CString filterName = filterInfo.achName;

			// ファイル名を含むフィルタ = ソースフィルタ
			if (filterName.Find(L":\\") != -1 || filterName.Find(L".mp4") != -1)
			{
				OutputDebugString(L"Found old source filter: ");
				OutputDebugString(filterName);
				OutputDebugString(L"\n");

				pOldSource = pFilter;
				pOldSource->AddRef();

				if (filterInfo.pGraph) filterInfo.pGraph->Release();
				pFilter->Release();
				break;
			}

			if (filterInfo.pGraph) filterInfo.pGraph->Release();
			pFilter->Release();
		}
		pEnum->Release();
	}

	if (!pOldSource)
	{
		OutputDebugString(L"Old source not found\n");
		return;
	}

	// 古いソースの出力ピンとその接続先を記憶
	struct PinConnection {
		IPin* downstream;
		AM_MEDIA_TYPE mt;
	};

	std::vector<PinConnection> connections;

	IEnumPins* pEnumPins = NULL;
	if (SUCCEEDED(pOldSource->EnumPins(&pEnumPins)))
	{
		IPin* pPin = NULL;
		while (pEnumPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);

			if (dir == PINDIR_OUTPUT)
			{
				IPin* pConnected = NULL;
				if (pPin->ConnectedTo(&pConnected) == S_OK)
				{
					PinConnection conn;
					conn.downstream = pConnected;

					AM_MEDIA_TYPE* pmt = NULL;
					if (SUCCEEDED(pPin->ConnectionMediaType(pmt)))
					{
						CopyMediaType(&conn.mt, pmt);
						DeleteMediaType(pmt);
					}

					connections.push_back(conn);

					// 切断
					pPin->Disconnect();
					pConnected->Disconnect();
				}
			}
			pPin->Release();
		}
		pEnumPins->Release();
	}

	// 古いソースを削除
	pGraph->RemoveFilter(pOldSource);
	pOldSource->Release();

	// LAV Splitter Sourceを追加
	IBaseFilter* pLAVSource = NULL;
	HRESULT hr = CoCreateInstance(CLSID_LAVSplitterSource, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void**)&pLAVSource);

	if (FAILED(hr))
	{
		OutputDebugString(L"Failed to create LAV Splitter Source\n");
		return;
	}

	pGraph->AddFilter(pLAVSource, L"LAV Splitter Source");

	// ファイルをロード
	IFileSourceFilter* pFileSource = NULL;
	if (SUCCEEDED(pLAVSource->QueryInterface(IID_IFileSourceFilter, (void**)&pFileSource)))
	{
		pFileSource->Load(filename, NULL);
		pFileSource->Release();
	}

	// 再接続
	pEnumPins = NULL;
	if (SUCCEEDED(pLAVSource->EnumPins(&pEnumPins)))
	{
		IPin* pPin = NULL;
		int connIdx = 0;

		while (pEnumPins->Next(1, &pPin, NULL) == S_OK && connIdx < connections.size())
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);

			if (dir == PINDIR_OUTPUT)
			{
				pGraph->Connect(pPin, connections[connIdx].downstream);
				connections[connIdx].downstream->Release();
				FreeMediaType(connections[connIdx].mt);
				connIdx++;
			}
			pPin->Release();
		}
		pEnumPins->Release();
	}

	pLAVSource->Release();

	OutputDebugString(L"=== ReplaceSourceWithLAV End ===\n");
}

void CDouga::ConnectSubtitleWithDirectVobSub(IGraphBuilder* pGraph)
{
	OutputDebugString(L"=== ConnectSubtitleWithDirectVobSub Start ===\n");

	IBaseFilter* pSource = NULL;
	IBaseFilter* pVideoDecoder = NULL;
	IBaseFilter* pVSFilter = NULL;
	IBaseFilter* pRenderer = NULL;

	// フィルタを探す
	IEnumFilters* pEnum = NULL;
	if (SUCCEEDED(pGraph->EnumFilters(&pEnum)))
	{
		IBaseFilter* pFilter = NULL;
		ULONG cFetched;

		while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
		{
			FILTER_INFO fi;
			pFilter->QueryFilterInfo(&fi);
			CString name = fi.achName;

			if (name.Find(L":\\") != -1) {
				pSource = pFilter;
				pSource->AddRef();
			}
			else if (name.Find(L"ffdshow Video") != -1) {
				pVideoDecoder = pFilter;
				pVideoDecoder->AddRef();
			}
			else if (name.Find(L"DirectVobSub") != -1) {
				pVSFilter = pFilter;
				pVSFilter->AddRef();
			}
			else if (name.Find(L"Enhanced Video Renderer") != -1 || name.Find(L"Video Mixing Renderer") != -1) {
				pRenderer = pFilter;
				pRenderer->AddRef();
			}

			if (fi.pGraph) fi.pGraph->Release();
			pFilter->Release();
		}
		pEnum->Release();
	}

	if (!pSource || !pVideoDecoder || !pVSFilter || !pRenderer)
	{
		OutputDebugString(L"Required filters not found\n");
		goto cleanup;
	}

	// ステップ1: Subtitle → ffdshow In Text を接続
	IPin* pSubPin = NULL, * pTextPin = NULL;

	// Subtitleピンを探す
	OutputDebugString(L"Searching for Subtitle pin...\n");
	IEnumPins* pPins = NULL;
	if (SUCCEEDED(pSource->EnumPins(&pPins)))
	{
		IPin* pPin = NULL;
		while (pPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);

			if (dir == PINDIR_OUTPUT)
			{
				PIN_INFO pi;
				pPin->QueryPinInfo(&pi);
				CString pinName = pi.achName;

				CString msg;
				msg.Format(L"  Found output pin: %s\n", pinName);
				OutputDebugString(msg);

				pinName.MakeLower();

				if (pinName.Find(L"subtitle") != -1)
				{
					IPin* pConn = NULL;
					if (pPin->ConnectedTo(&pConn) != S_OK) {
						OutputDebugString(L"    -> This is unconnected Subtitle pin!\n");
						pSubPin = pPin;
						pSubPin->AddRef();
					}
					else {
						OutputDebugString(L"    -> Already connected\n");
						pConn->Release();
					}
				}

				if (pi.pFilter) pi.pFilter->Release();
			}
			pPin->Release();
			if (pSubPin) break;
		}
		pPins->Release();
	}

	if (!pSubPin)
	{
		OutputDebugString(L"ERROR: Subtitle pin not found!\n");
	}

	// In Textピンを探す
	OutputDebugString(L"Searching for In Text pin...\n");
	pPins = NULL;
	if (SUCCEEDED(pVideoDecoder->EnumPins(&pPins)))
	{
		IPin* pPin = NULL;
		while (pPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);

			if (dir == PINDIR_INPUT)
			{
				PIN_INFO pi;
				pPin->QueryPinInfo(&pi);
				CString pinName = pi.achName;

				CString msg;
				msg.Format(L"  Found input pin: %s\n", pinName);
				OutputDebugString(msg);

				pinName.MakeLower();

				if (pinName.Find(L"text") != -1)
				{
					OutputDebugString(L"    -> This is In Text pin!\n");
					pTextPin = pPin;
					pTextPin->AddRef();
				}

				if (pi.pFilter) pi.pFilter->Release();
			}
			pPin->Release();
			if (pTextPin) break;
		}
		pPins->Release();
	}

	if (!pTextPin)
	{
		OutputDebugString(L"ERROR: In Text pin not found!\n");
	}

	if (pSubPin && pTextPin)
	{
		OutputDebugString(L"Attempting to connect Subtitle -> In Text...\n");

		// まずIntelligent Connectを試す
		HRESULT hr = pGraph->Connect(pSubPin, pTextPin);
		CString msg;
		msg.Format(L"Subtitle -> In Text (Connect): 0x%08X\n", hr);
		OutputDebugString(msg);

		if (FAILED(hr))
		{
			// 失敗したらConnectDirectも試す
			hr = pGraph->ConnectDirect(pSubPin, pTextPin, NULL);
			msg.Format(L"Subtitle -> In Text (ConnectDirect): 0x%08X\n", hr);
			OutputDebugString(msg);
		}

		pSubPin->Release();
		pTextPin->Release();
	}
	else
	{
		OutputDebugString(L"Cannot connect: pins not found\n");
		if (pSubPin) pSubPin->Release();
		if (pTextPin) pTextPin->Release();
	}

	// ステップ2: ffdshow Out → DirectVobSub → Renderer に再接続
	IPin* pDecoderOut = NULL, * pVSIn = NULL, * pVSOut = NULL, * pRendererIn = NULL;

	// ffdshow Outピンを探す
	pPins = NULL;
	if (SUCCEEDED(pVideoDecoder->EnumPins(&pPins)))
	{
		IPin* pPin = NULL;
		while (pPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);
			if (dir == PINDIR_OUTPUT)
			{
				pDecoderOut = pPin;
				pDecoderOut->AddRef();
			}
			pPin->Release();
			if (pDecoderOut) break;
		}
		pPins->Release();
	}

	// DirectVobSubのピンを探す
	pPins = NULL;
	if (SUCCEEDED(pVSFilter->EnumPins(&pPins)))
	{
		IPin* pPin = NULL;
		while (pPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);

			if (dir == PINDIR_INPUT && !pVSIn)
			{
				pVSIn = pPin;
				pVSIn->AddRef();
			}
			else if (dir == PINDIR_OUTPUT && !pVSOut)
			{
				pVSOut = pPin;
				pVSOut->AddRef();
			}

			pPin->Release();
		}
		pPins->Release();
	}

	// Rendererの入力ピンを探す
	pPins = NULL;
	if (SUCCEEDED(pRenderer->EnumPins(&pPins)))
	{
		IPin* pPin = NULL;
		while (pPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);
			if (dir == PINDIR_INPUT)
			{
				pRendererIn = pPin;
				pRendererIn->AddRef();
			}
			pPin->Release();
			if (pRendererIn) break;
		}
		pPins->Release();
	}

	if (pDecoderOut && pVSIn && pVSOut && pRendererIn)
	{
		// 現在の接続を切断
		IPin* pOldConn = NULL;
		if (pDecoderOut->ConnectedTo(&pOldConn) == S_OK)
		{
			pDecoderOut->Disconnect();
			pOldConn->Disconnect();
			pOldConn->Release();
		}

		// ffdshow → DirectVobSub
		HRESULT hr = pGraph->Connect(pDecoderOut, pVSIn);
		CString msg;
		msg.Format(L"Decoder -> VSFilter: 0x%08X\n", hr);
		OutputDebugString(msg);

		// DirectVobSub → Renderer
		if (SUCCEEDED(hr))
		{
			hr = pGraph->Connect(pVSOut, pRendererIn);
			msg.Format(L"VSFilter -> Renderer: 0x%08X\n", hr);
			OutputDebugString(msg);
		}
	}

	if (pDecoderOut) pDecoderOut->Release();
	if (pVSIn) pVSIn->Release();
	if (pVSOut) pVSOut->Release();
	if (pRendererIn) pRendererIn->Release();

cleanup:
	if (pSource) pSource->Release();
	if (pVideoDecoder) pVideoDecoder->Release();
	if (pVSFilter) pVSFilter->Release();
	if (pRenderer) pRenderer->Release();

	OutputDebugString(L"=== ConnectSubtitleWithDirectVobSub End ===\n");
}

void CDouga::plays(TCHAR* s)
{
	WCHAR ss[2050];HRESULT hr;
	int cflg=0;
	LPWSTR ss1; ss1=ss;
	TCHAR *s3; s2=s; s3=s;
#if _UNICODE
	_tcscpy(ss,s);
#else
	MultiByteToWideChar(CP_ACP,0,s3,-1,ss1,2000);
#endif
	CoInitialize(NULL);
	
	int len = ::WideCharToMultiByte(CP_THREAD_ACP,0, ss, -1, NULL, 0, NULL, NULL);
	memcpy((TCHAR*)douga,(TCHAR*)ss,len*2+2);

	Haali=NULL;pSplitter=NULL;
	CoCreateInstance(CLSID_FilterGraph,NULL,CLSCTX_INPROC_SERVER,IID_IGraphBuilder,(LPVOID *)&pGraphBuilder);
	if(pGraphBuilder){
		pGraphBuilder->QueryInterface(IID_IMediaControl,(LPVOID *)&pMediaControl);
		pGraphBuilder->QueryInterface(IID_IVideoWindow,(LPVOID *)&pVideoWindow);
		pGraphBuilder->QueryInterface(IID_IMediaPosition,(LPVOID *)&pMediaPosition);
		pGraphBuilder->QueryInterface(IID_IBasicAudio, (LPVOID *)&pBasicAudio);
		pGraphBuilder->QueryInterface(IID_IMediaEvent,(LPVOID*)&pMediaEvent);
	}


	rate = GetFrameRate(pGraphBuilder);

	// ファイル拡張子による推測をフォールバックとして使用
	if (rate == 0.0)
	{
		s2.MakeLower();
		if (s2.Right(4) == ".vob" || s2.Right(4) == ".mpg" || s2.Right(3) == ".ts")
		{
			rate = 29.97;
		}
		else if (s2.Right(4) == ".mp4" || s2.Right(4) == ".mkv")
		{
			rate = 23.976; // 一般的なデフォルト
		}
	}

	rateflg = 0;
	if (rate == 0.0) {
		rateflg = 1;
	}
	else {
		rate = (float)((int)(rate * 1000.0f)) / 1000.0f;
	}




		IFilterMapper2 *pMapper = NULL;
		ICreateDevEnum *pDevEnum = NULL;
		IBaseFilter* pVSFilter = NULL;
		IEnumMoniker *pEnum = NULL;
		hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC,
			IID_ICreateDevEnum, (void**)&pDevEnum);
		hr = pDevEnum->CreateClassEnumerator(CLSID_LegacyAmFilterCategory, &pEnum, 0);
	    pDevEnum->Release();
		OSVERSIONINFO in;ZeroMemory(&in,sizeof(in));in.dwOSVersionInfoSize=sizeof(OSVERSIONINFO);GetVersionEx(&in);


		if(in.dwMajorVersion>=5){

			// モニカを列挙する。
			IMoniker *pMoniker;
			ULONG cFetched;
			while (pEnum->Next(1, &pMoniker, &cFetched) == S_OK){
				IPropertyBag *pPropBag = NULL;
				hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, 
				   (void **)&pPropBag);
				if (SUCCEEDED(hr)){
					// フィルタのフレンドリ名を取得するには、次の処理を行う。
					VARIANT varName;
					VariantInit(&varName);
					hr = pPropBag->Read(L"FriendlyName", &varName, 0);
					int len = ::WideCharToMultiByte(CP_THREAD_ACP,0, varName.bstrVal, -1, NULL, 0, NULL, NULL);

					// ★★★ DirectVobSubを探す ★★★
					if (_wcsnicmp(varName.bstrVal, L"DirectVobSub", 12 * 2) == 0 &&
						_wcsnicmp(varName.bstrVal, L"DirectVobSub (auto-loading version)", 36 * 2) != 0)
					{
						OutputDebugString(L"Found DirectVobSub (normal version)\n");
						hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&pVSFilter);
					}
					if(_wcsnicmp(varName.bstrVal,L"Enhanced Video Renderer",len*2-2)==0 && savedata.evr && savedata.render==0
						&& !(mode==11 || mode==12 || mode==16 || mode==19)){ev=TRUE;
//						CoCreateInstance(CLSID_EnhancedVideoRenderer, NULL, CLSCTX_INPROC_SERVER,IID_IBaseFilter, reinterpret_cast<void **>(&prend));
						hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&prend);
						prend->QueryInterface(IID_IMFGetService,(LPVOID *)&service);
						hr=service->GetService(MR_VIDEO_RENDER_SERVICE, IID_IMFVideoDisplayControl, (void**)&Vdc);
						hr=Vdc->SetVideoWindow(m_hWnd);
					}
					VariantClear(&varName);
					RELEASE(pPropBag);
				}
				RELEASE(pMoniker);
			}
		}
		RELEASE(pMapper);
		RELEASE(pEnum);

		
		BOOL renderr=0;
		CString ssss;
#if UNICODE
		ssss=ss;
#else
		char sss[1024];
		WideCharToMultiByte(CP_ACP,0, ss, 1024, sss, 1024, NULL, NULL);
		ssss=sss;
#endif
	ssss.MakeLower();
	int flg=0;



	if (pVSFilter)
	{
		OutputDebugString(L"Adding DirectVobSub to graph...\n");
		hr = pGraphBuilder->AddFilter(pVSFilter, L"DirectVobSub");
		CString msg;
		msg.Format(L"DirectVobSub AddFilter: 0x%08X\n", hr);
		OutputDebugString(msg);
		pVSFilter->Release();
	}
	else
	{
		OutputDebugString(L"DirectVobSub not found in system\n");
	}

	if(prend)
		pGraphBuilder->AddFilter(prend, L"Enhanced Video Renderer");

	DumpFilterGraph();

	HRESULT hr2 = pGraphBuilder->RenderFile(ss, NULL);
	ConnectSubtitleWithDirectVobSub(pGraphBuilder);
	DumpFilterGraph();

	//if(prend)
	//	pGraphBuilder->AddFilter(prend, L"Enhanced Video Renderer");

	if (prend)
		Filtervideooff2(pGraphBuilder);

	//Filtervideooff3(pGraphBuilder);
	if(pGraphBuilder)pGraphBuilder->QueryInterface(IID_IMediaSeeking,(LPVOID *)&pMediaSeeking);

	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);

	audionum = 0;
	if (iam) {
		audionum = CntPin2(iam);
	}

	if (GetStreamInfo(pGraphBuilder, audioStreams, videoStreams, subtitleStreams))
	{
		audionum = audioStreams.size();

		// デバッグ出力（必要に応じて）
		CString debug;
		debug.Format(L"Audio: %d, Video: %d, Subtitle: %d",
			audioStreams.size(), videoStreams.size(), subtitleStreams.size());
		// OutputDebugString(debug);
	}

	if(savedata.audiost==1)
		if (audionum > 1) {
			CAudioSelect as;
			as.no = audionum;
			int rett = as.DoModal();
			if (as.no < 0)as.no = 0;
			st12 = as.no + 1;
			if (pGraphBuilder && iam) {
				iam->Enable(as.no+1, AMSTREAMSELECTENABLE_ENABLE);
			}
		}
}

void CDouga::Filtersdown(IGraphBuilder *pGraph,WCHAR *filter) 
{
    IEnumFilters *pEnum = NULL;
    IBaseFilter *pFilter;
    ULONG cFetched;
	CString s;
    HRESULT hr = pGraph->EnumFilters(&pEnum);
    if (FAILED(hr)) return ;

    while(pEnum->Next(1, &pFilter, &cFetched) == S_OK)
    {
		IEnumPins *p;
		IPin *pPin;
		PIN_INFO pp, pp1;
		FILTER_INFO FilterInfo,FilterInfo1,FilterInfo2;
        pFilter->QueryFilterInfo(&FilterInfo);
		pFilter->EnumPins(&p);
		pFilter->QueryFilterInfo(&FilterInfo1);
		s = FilterInfo1.achName;
		if (s.Find(L"\\") != -1) {
			if (iam == NULL)pFilter->QueryInterface(IID_IAMStreamSelect, (void**)&iam);
			RELEASE(pFilter);
		}
		if (s.Find(L"Splitter") != -1) {
			if (iam == NULL)pFilter->QueryInterface(IID_IAMStreamSelect, (void**)&iam);
			RELEASE(pFilter);
		}
		while(p->Next(1, &pPin, 0) == S_OK)
		{
			PIN_DIRECTION PinDirThis;
			pPin->QueryDirection(&PinDirThis);
			//if (PinDirThis == PINDIR_OUTPUT){
				PIN_INFO pp,pp1;
				IPin *pn;
				if (pPin->ConnectedTo(&pn) == S_OK) {
					pn->QueryPinInfo(&pp);
					pp.pFilter->QueryFilterInfo(&FilterInfo1);
					pPin->QueryPinInfo(&pp1);
					pp1.pFilter->QueryFilterInfo(&FilterInfo2);
					s = FilterInfo1.achName;
					if (s.Find(L"Splitter") != -1) {
						if (iam == NULL)pp.pFilter->QueryInterface(IID_IAMStreamSelect, (void**)&iam);
						RELEASE(pp.pFilter);
						RELEASE(pp1.pFilter);
					}
					if (_wcsnicmp(FilterInfo1.achName, L"Default DirectSound Device", 27 * 2 + 4) == 0) {
						//pGraph->RemoveFilter(pp.pFilter);
						//pGraph->RemoveFilter(pp1.pFilter);
						RELEASE(pp.pFilter);
						RELEASE(pp1.pFilter);
					}
					RELEASE(pp.pFilter);
					RELEASE(pp1.pFilter);
				}
				//}
		}
		p->Release();
        // FILTER_INFO 構造体はフィルタ グラフ マネージャへのポインタを保持する。
        // その参照カウントは解放しなければならない。
        if (FilterInfo.pGraph != NULL)
            FilterInfo.pGraph->Release();
		if(pFilter)
	        pFilter->Release();
    }

    pEnum->Release();
    return ;
}

void CDouga::Filtervideooff(IGraphBuilder *pGraph,WCHAR *filter) 
{
    IEnumFilters *pEnum = NULL;
    IBaseFilter *pFilter;
    ULONG cFetched;
	CString s;

    HRESULT hr = pGraph->EnumFilters(&pEnum);
    if (FAILED(hr)) return ;

    while(pEnum->Next(1, &pFilter, &cFetched) == S_OK)
    {
		IEnumPins *p;
		IPin *pPin;
        FILTER_INFO FilterInfo,FilterInfo1;
        pFilter->QueryFilterInfo(&FilterInfo);
		pFilter->EnumPins(&p);
		PIN_INFO pp;
		pFilter->QueryFilterInfo(&FilterInfo1);
		s = FilterInfo1.achName;
		if (s.Find(L"Video Decoder") != -1) {
			pGraph->AddFilter(prend, L"Enhanced Video Renderer");
			ConnectFilter(pFilter, prend, MEDIATYPE_Video, NULL);
			RELEASE(pFilter);
		}
		RELEASE(pFilter);
		while(p->Next(1, &pPin, 0) == S_OK)
		{
			PIN_DIRECTION PinDirThis;
			pPin->QueryDirection(&PinDirThis);
			if (PinDirThis == PINDIR_OUTPUT){
				PIN_INFO pp;
				IPin *pn;
				if(pPin->ConnectedTo(&pn)==S_OK){
					pn->QueryPinInfo(&pp);
					pp.pFilter->QueryFilterInfo(&FilterInfo1);
					s = FilterInfo1.achName;
					if (s.Find(L"Video Decoder") != -1) {
						pGraph->AddFilter(prend, L"Enhanced Video Renderer");
						ConnectFilter(pp.pFilter, prend, MEDIATYPE_Video, NULL);
						RELEASE(pp.pFilter);
					}
				}
			}
		}
		p->Release();
        // FILTER_INFO 構造体はフィルタ グラフ マネージャへのポインタを保持する。
        // その参照カウントは解放しなければならない。
        if (FilterInfo.pGraph != NULL)
            FilterInfo.pGraph->Release();
		if(pFilter)
	        pFilter->Release();
    }

    pEnum->Release();
    return ;
}

void CDouga::Filtervideooff2(IGraphBuilder *pGraph, WCHAR *filter)
{
	IEnumFilters *pEnum = NULL;
	IBaseFilter *pFilter;
	ULONG cFetched;
	CString s;

	HRESULT hr = pGraph->EnumFilters(&pEnum);
	if (FAILED(hr)) return;

	while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
	{
		IEnumPins *p;
		IPin *pPin;
		FILTER_INFO FilterInfo, FilterInfo1;
		pFilter->QueryFilterInfo(&FilterInfo);
		pFilter->EnumPins(&p);
		PIN_INFO pp;
		pFilter->QueryFilterInfo(&FilterInfo1);
		s = FilterInfo1.achName;
		if (s.Find(L"Video Renderer") == 0) {
			pGraph->RemoveFilter(pFilter);
			RELEASE(pFilter);
		}
		RELEASE(pFilter);
		while (p->Next(1, &pPin, 0) == S_OK)
		{
			PIN_DIRECTION PinDirThis;
			pPin->QueryDirection(&PinDirThis);
			if (PinDirThis == PINDIR_OUTPUT) {
				PIN_INFO pp;
				IPin *pn;
				if (pPin->ConnectedTo(&pn) == S_OK) {
					pn->QueryPinInfo(&pp);
					pp.pFilter->QueryFilterInfo(&FilterInfo1);
					s = FilterInfo1.achName;
					if (s.Find(L"Video Renderer") == 0) {
						pGraph->RemoveFilter(pp.pFilter);
						RELEASE(pp.pFilter);
					}
				}
			}
		}
		p->Release();
		// FILTER_INFO 構造体はフィルタ グラフ マネージャへのポインタを保持する。
		// その参照カウントは解放しなければならない。
		if (FilterInfo.pGraph != NULL)
			FilterInfo.pGraph->Release();
		if (pFilter)
			pFilter->Release();
	}

	pEnum->Release();
	return;
}

void CDouga::Filtervideooff3(IGraphBuilder *pGraph, WCHAR *filter)
{
	IEnumFilters *pEnum = NULL;
	IBaseFilter *pFilter;
	ULONG cFetched;
	CString s;
	int cnt;
	cnt = 0;

	HRESULT hr = pGraph->EnumFilters(&pEnum);
	if (FAILED(hr)) return;

	while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
	{
		IEnumPins *p;
		IPin *pPin;
		FILTER_INFO FilterInfo, FilterInfo1;
		pFilter->QueryFilterInfo(&FilterInfo);
		pFilter->EnumPins(&p);
		pFilter->QueryFilterInfo(&FilterInfo1);
		s = FilterInfo1.achName;
		if (s.Find(L"ffdshow Video Decoder") == 0) {
			cnt++;
			RELEASE(pFilter);
		}
		if (s.Find(L"LAV Video Decoder") == 0) {
			cnt++;
			RELEASE(pFilter);
		}
		RELEASE(pFilter);
		while (p->Next(1, &pPin, 0) == S_OK)
		{
			PIN_DIRECTION PinDirThis;
			pPin->QueryDirection(&PinDirThis);
			if (PinDirThis == PINDIR_OUTPUT) {
				PIN_INFO pp;
				IPin *pn;
				if (pPin->ConnectedTo(&pn) == S_OK) {
					pn->QueryPinInfo(&pp);
				}
			}
		}
		p->Release();
		// FILTER_INFO 構造体はフィルタ グラフ マネージャへのポインタを保持する。
		// その参照カウントは解放しなければならない。
		if (FilterInfo.pGraph != NULL)
			FilterInfo.pGraph->Release();
		if (pFilter)
			pFilter->Release();
	}

	pEnum->Release();
	if(cnt==2)Filtervideooff4(pGraph);
	return;
}

void CDouga::Filtervideooff4(IGraphBuilder *pGraph)
{
	IEnumFilters *pEnum = NULL;
	IBaseFilter *pFilter;
	ULONG cFetched;
	CString s;
	int cnt;
	cnt = 0;

	HRESULT hr = pGraph->EnumFilters(&pEnum);
	if (FAILED(hr)) return;

	while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
	{
		IEnumPins *p;
		IPin *pPin;
		FILTER_INFO FilterInfo, FilterInfo1;
		pFilter->QueryFilterInfo(&FilterInfo);
		pFilter->EnumPins(&p);
		pFilter->QueryFilterInfo(&FilterInfo1);
		s = FilterInfo1.achName;
		if (s.Find(L"LAV Video Decoder") == 0) {
			pGraph->RemoveFilter(pFilter);
			RELEASE(pFilter);
		}
		RELEASE(pFilter);
		while (p->Next(1, &pPin, 0) == S_OK)
		{
			PIN_DIRECTION PinDirThis;
			pPin->QueryDirection(&PinDirThis);
			if (PinDirThis == PINDIR_OUTPUT) {
				PIN_INFO pp;
				IPin *pn;
				if (pPin->ConnectedTo(&pn) == S_OK) {
					pn->QueryPinInfo(&pp);
					pp.pFilter->QueryFilterInfo(&FilterInfo1);
					s = FilterInfo1.achName;
					if (s.Find(L"LAV Video Decoder") == 0) {
						pGraph->RemoveFilter(pp.pFilter);
						RELEASE(pp.pFilter);
					}
				}
			}
		}
		p->Release();
		// FILTER_INFO 構造体はフィルタ グラフ マネージャへのポインタを保持する。
		// その参照カウントは解放しなければならない。
		if (FilterInfo.pGraph != NULL)
			FilterInfo.pGraph->Release();
		if (pFilter)
			pFilter->Release();
	}

	pEnum->Release();
	return;
}

// 字幕ピンを接続する関数（改良版）
void CDouga::ConnectSubtitlePins(IGraphBuilder* pGraph)
{
	if (!pGraph) return;

	OutputDebugString(L"=== ConnectSubtitlePins Start ===\n");

	IEnumFilters* pEnum = NULL;
	IBaseFilter* pSourceFilter = NULL;
	IPin* pSubtitleOutputPin = NULL;
	ULONG cFetched;

	// ソースフィルタを探す
	if (SUCCEEDED(pGraph->EnumFilters(&pEnum)))
	{
		IBaseFilter* pFilter = NULL;
		while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
		{
			FILTER_INFO filterInfo;
			pFilter->QueryFilterInfo(&filterInfo);
			CString filterName = filterInfo.achName;

			// ソースフィルタ（ファイル名を含むもの）
			if (filterName.Find(L".mp4") != -1 ||
				filterName.Find(L".mkv") != -1 ||
				filterName.Find(L".avi") != -1 ||
				filterName.Find(L":\\") != -1)
			{
				OutputDebugString(L"Found Source Filter\n");
				pSourceFilter = pFilter;
				pSourceFilter->AddRef();
			}

			if (filterInfo.pGraph) filterInfo.pGraph->Release();
			pFilter->Release();
		}
		pEnum->Release();
	}

	if (!pSourceFilter)
	{
		OutputDebugString(L"ERROR: Source filter not found!\n");
		return;
	}

	// ソースフィルタの字幕ピンを探す
	IEnumPins* pEnumPins = NULL;
	if (SUCCEEDED(pSourceFilter->EnumPins(&pEnumPins)))
	{
		IPin* pPin = NULL;
		while (pEnumPins->Next(1, &pPin, NULL) == S_OK)
		{
			PIN_DIRECTION dir;
			pPin->QueryDirection(&dir);

			if (dir == PINDIR_OUTPUT)
			{
				PIN_INFO pinInfo;
				pPin->QueryPinInfo(&pinInfo);
				CString pinName = pinInfo.achName;
				pinName.MakeLower();

				// 字幕ピンかチェック
				if (pinName.Find(L"subtitle") != -1 ||
					pinName.Find(L"sub") != -1)
				{
					// 接続されていない場合のみ
					IPin* pConnected = NULL;
					if (pPin->ConnectedTo(&pConnected) != S_OK)
					{
						OutputDebugString(L"Found unconnected Subtitle pin\n");
						pSubtitleOutputPin = pPin;
						pSubtitleOutputPin->AddRef();

						if (pinInfo.pFilter) pinInfo.pFilter->Release();
						break;
					}
					else
					{
						pConnected->Release();
					}
				}

				if (pinInfo.pFilter) pinInfo.pFilter->Release();
			}
			pPin->Release();
		}
		pEnumPins->Release();
	}

	if (!pSubtitleOutputPin)
	{
		OutputDebugString(L"No unconnected subtitle pin found\n");
		pSourceFilter->Release();
		return;
	}

	// 方法1: ICaptureGraphBuilder2を使った自動レンダリング
	ICaptureGraphBuilder2* pBuilder = NULL;
	HRESULT hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC,
		IID_ICaptureGraphBuilder2, (void**)&pBuilder);

	if (SUCCEEDED(hr))
	{
		pBuilder->SetFiltergraph(pGraph);

		// RenderStreamで自動接続を試みる
		OutputDebugString(L"Trying RenderStream...\n");
		hr = pBuilder->RenderStream(NULL, NULL, pSubtitleOutputPin, NULL, NULL);

		CString msg;
		msg.Format(L"RenderStream result: 0x%08X\n", hr);
		OutputDebugString(msg);

		if (SUCCEEDED(hr))
		{
			OutputDebugString(L"SUCCESS: Subtitle rendered via RenderStream\n");
		}

		pBuilder->Release();
	}

	// 方法2: RenderStreamが失敗した場合、Intelligent Connectを試す
	if (FAILED(hr))
	{
		OutputDebugString(L"RenderStream failed, trying Render...\n");
		hr = pGraph->Render(pSubtitleOutputPin);

		CString msg;
		msg.Format(L"Render result: 0x%08X\n", hr);
		OutputDebugString(msg);

		if (SUCCEEDED(hr))
		{
			OutputDebugString(L"SUCCESS: Subtitle rendered via Render\n");
		}
		else
		{
			OutputDebugString(L"FAILED: Could not render subtitle\n");
		}
	}

	pSubtitleOutputPin->Release();
	pSourceFilter->Release();

	OutputDebugString(L"=== ConnectSubtitlePins End ===\n");
}
extern CString filen;
void CDouga::play(int u, CString str)
{
	u1=u;
	CString s0;
	if(mode==-2){
		TCHAR *s;
//		s = new TCHAR [filen.GetLength()+1];
		s=filen.GetBuffer();
		plays(s);
		filen.ReleaseBuffer();
//		delete [] s;
	}
	if (mode == -1) {//ogg ys8
		CString str2 = str;
		str += L"\\";
		switch (u)
		{
		case 1:
			{
				TCHAR *s;
				str += _T("movie\\logo.wmv");
				s = str.GetBuffer();
				plays(s);
				str.ReleaseBuffer();
				break;
			}
		case 2:
		{
			TCHAR *s;
			str += _T("movie\\op.wmv");
			s = str.GetBuffer();
			plays(s);
			str.ReleaseBuffer();
			break;
		}
		case 3:
		{
			TCHAR *s;
			str += _T("movie\\ed2.wmv");
			s = str.GetBuffer();
			plays(s);
			str.ReleaseBuffer();
			break;
		}
		case 4:
		{
			TCHAR *s;
			str += _T("movie\\logo.wmv");
			s = str.GetBuffer();
			plays(s);
			str.ReleaseBuffer();
			break;
		}
		}
}

	if(mode==1){//ED6SC
		switch(u)
		{
		case 98:
				{
				TCHAR s[]=_T("..\\ED6_2_LOGO.avi");
				;
				plays(s);
			break;
				}
		case 99:
				{
				TCHAR s[]=_T("..\\ED6_2_OP.avi");
				plays(s);
			break;
				}
		case 100:
				{
			TCHAR s[]=_T("..\\ED6_DT47.dat");
			plays(s);
			break;
				}
		case 101:
				{
			TCHAR s[]=_T("..\\ED6_DT40.dat");
			plays(s);
			break;
				}
		case 102:
				{
			TCHAR s[]=_T("..\\ED6_DT41.dat");
			plays(s);
			break;
				}
		case 103:
				{
			TCHAR s[]=_T("..\\ED6_DT42.dat");
			plays(s);
			break;
				}
		case 104:
				{
			TCHAR s[]=_T("..\\ED6_DT43.dat");
			plays(s);
			break;
				}
		case 105:
				{
			TCHAR s[]=_T("..\\ED6_DT44.dat");
			plays(s);
			break;
				}
		case 106:
				{
			TCHAR s[]=_T("..\\ED6_DT45.dat");
			plays(s);
			break;
				}
		case 107:
				{
			TCHAR s[]=_T("..\\ED6_DT46.dat");
			plays(s);
			break;
				}
		}
	}
	if(mode==2){//ED6FC
	switch(u)
	{
		case 55:
				{
			TCHAR s[]=_T("..\\ED6_LOGO.avi");
			plays(s);
			break;
				}
		case 56:
				{
			TCHAR s[]=_T("..\\ED6_OP.avi");
			plays(s);
			break;
				}
		case 57:
				{
			TCHAR s[]=_T("..\\ED6_DT17.dat");
			plays(s);
			break;
				}
		case 58:
				{
			TCHAR s[]=_T("..\\ED6_DT18.dat");
			plays(s);
			break;
				}
		}
	}
	if(mode==3){//YSF
		switch(u)
		{
		case 32:
				{
			TCHAR s[]=_T("..\\opening.avi");
			plays(s);
			break;
				}
		case 33:
				{
			TCHAR s[]=_T("..\\im01.dt");
			plays(s);
			break;
				}
		case 25:
				{
			TCHAR s[]=_T("..\\im03a.dt");
			plays(s);
			break;
				}
		}
	}
	if(mode==4){//YS6
		switch(u)
		{
		case 1:
				{
			TCHAR s[]=_T("..\\opening.avi");
			plays(s);
			break;
				}
		case 25:
				{
			TCHAR s[]=_T("..\\im01.dt");
			plays(s);
			break;
				}
		case 26:
				{
			TCHAR s[]=_T("..\\im02.dt");
			plays(s);
			break;
				}
		case 27:
				{
			TCHAR s[]=_T("..\\im03a.dt");
			plays(s);
			break;
				}
		case 28:
				{
			TCHAR s[]=_T("..\\im03b.dt");
			plays(s);
			break;
				}
		}
	}
	if(mode==5){//YSO
		switch(u+1)
		{
		case 41:
				{
			TCHAR s[]=_T("..\\yso_logo.avi");
			plays(s);
			break;
				}
		case 42:
				{
			TCHAR s[]=_T("..\\yso_pro.avi");
			plays(s);
			break;
				}
		case 43:
				{
			TCHAR s[]=_T("..\\yso_op.avi");
			plays(s);
			break;
				}
		case 44:
				{
			TCHAR s[]=_T("..\\yso_ins01.dat");
			plays(s);
			break;
				}
		case 45:
				{
			TCHAR s[]=_T("..\\yso_ins02.dat");
			plays(s);
			break;
				}
		case 46:
				{
			TCHAR s[]=_T("..\\yso_ins03.dat");
			plays(s);
			break;
				}
		case 47:
				{
			TCHAR s[]=_T("..\\yso_ed01.dat");
			plays(s);
			break;
				}
		case 48:
				{
			TCHAR s[]=_T("..\\yso_ed02.dat");
			plays(s);
			break;
				}
		}
	}
	if(mode==6){//YSO
		switch(u)
		{
		case 141:
				{
			TCHAR s[]=_T("..\\ED6_3_LOGO.avi");
			plays(s);
			break;
				}
		case 142:
				{
			TCHAR s[]=_T("..\\ED6_3_OP.avi");
			plays(s);
			break;
				}
		case 143:
				{
			TCHAR s[]=_T("..\\ED6_DT51.dat");
			plays(s);
			break;
				}
		case 144:
				{
			TCHAR s[]=_T("..\\ED6_DT48.dat");
			plays(s);
			break;
				}
		case 145:
				{
			TCHAR s[]=_T("..\\ED6_DT49.dat");
			plays(s);
			break;
				}
		case 146:
				{
			TCHAR s[]=_T("..\\ED6_DT50.dat");
			plays(s);
			break;
				}
		}
	}
	if(mode==7){//YSO
		switch(u)
		{
		case 65:
			{
			TCHAR s[]=_T("..\\data\\sys\\op.mpg");
			plays(s);
			break;
			}
		case 66:
			{
			TCHAR s[]=_T("..\\data\\sys\\ed.mpg");
			plays(s);
			break;
			}
		}
	}
	if(mode==8){//YSC1
		switch(u)
		{
		case 72:
			{
			TCHAR s[]=_T("..\\..\\data\\ys1_opwp.dat");
			plays(s);
			break;
			}
		case 73:
			{
			TCHAR s[]=_T("..\\..\\data\\ys1_opwo.dat");
			plays(s);
			break;
			}
		case 74:
			{
			TCHAR s[]=_T("..\\..\\data\\ys1_opwn.dat");
			plays(s);
			break;
			}
		}
	}
	if(mode==9){//YSC1
		switch(u)
		{
		case 93:
			{
			TCHAR s[]=_T("..\\..\\data2\\ys2op2op.dat");
			plays(s);
			break;
			}
		case 94:
			{
			TCHAR s[]=_T("..\\..\\data2\\ys2op2oo.dat");
			plays(s);
			break;
			}
		case 95:
			{
			TCHAR s[]=_T("..\\..\\data2\\ys2op2on.dat");
			plays(s);
			break;
			}
		case 96:
			{
			TCHAR s[]=_T("..\\..\\data2\\ys2ed1op.dat");
			plays(s);
			break;
			}
		case 97:
			{
			TCHAR s[]=_T("..\\..\\data2\\ys2ed1oo.dat");
			plays(s);
			break;
			}
		case 98:
			{
			TCHAR s[]=_T("..\\..\\data2\\ys2ed1on.dat");
			plays(s);
			break;
			}
		case 99:
			{
			TCHAR s[]=_T("..\\..\\data2\\ys2ed1np.dat");
			plays(s);
			break;
			}
		case 100:
			{
			TCHAR s[]=_T("..\\..\\data2\\ys2ed1no.dat");
			plays(s);
			break;
			}
		case 101:
			{
			TCHAR s[]=_T("..\\..\\data2\\ys2ed1nn.dat");
			plays(s);
			break;
			}
		}
	}
	if(mode==10){//XANADU
		switch(u)
		{
		case 24:
			{
			TCHAR s[]=_T("..\\MOVIE\\logo.avi");
			plays(s);
			break;
			}
		case 25:
			{
			TCHAR s[]=_T("..\\MOVIE\\opening.avi");
			plays(s);
			break;
			}
		}
	}
	if(mode==11){//ys1
		switch(u)
		{
		case 25:
			{
			TCHAR s[]=_T("..\\..\\data\\YS12OP.avi");
			plays(s);
			break;
			}
		}
	}
	if(mode==12){//ys2
		switch(u)
		{
		case 31:
			{
			TCHAR s[]=_T("..\\..\\data\\YS2OP_1.avi");
			plays(s);
			break;
			}
		case 32:
			{
			TCHAR s[]=_T("..\\..\\data\\YS2OP_2.avi");
			plays(s);
			break;
			}
		}
	}
	if(mode==15){//gurumin
		switch(u)
		{
		case 40:
			{
			TCHAR s[]=_T("..\\3ddata\\op.avi");
			plays(s);
			break;
			}
		}
	}
	if(mode==16){//dino
		switch(u)
		{
		case 33:
			{
			TCHAR s[]=_T("op_din.avi");
			plays(s);
			break;
			}
		}
	}
	if(mode==19){//ED4
		switch(u)
		{
		case 1:
			{
			TCHAR s[]=_T("..\\lib\\ED4OP1.AVI");
			plays(s);
			break;
			}
		case 2:
			{
			TCHAR s[]=_T("..\\lib\\ED4OP2.AVI");
			plays(s);
			break;
			}
		}
	}
	if(mode==-11){//ED4
		switch(u)
		{
		case 28:
			{
			TCHAR s[]=_T("..\\video\\logo.AVI");
			plays(s);
			break;
			}
		case 29:
			{
			TCHAR s[]=_T("..\\video\\open.AVI");
			plays(s);
			break;
			}
		case 30:
			{
			TCHAR s[]=_T("..\\video\\Team.AVI");
			plays(s);
			break;
			}
		case 31:
			{
			TCHAR s[]=_T("..\\video\\end1.AVI");
			plays(s);
			break;
			}
		case 32:
			{
			TCHAR s[]=_T("..\\video\\end2.AVI");
			plays(s);
			break;
			}
		case 33:
			{
			TCHAR s[]=_T("..\\video\\end3.AVI");
			plays(s);
			break;
			}
		case 34:
			{
			TCHAR s[]=_T("..\\video\\die.AVI");
			plays(s);
			break;
			}
		case 35:
			{
			TCHAR s[]=_T("..\\video\\NLZ-FALL.AVI");
			plays(s);
			break;
			}
		case 36:
			{
			TCHAR s[]=_T("..\\video\\SING.AVI");
			plays(s);
			break;
			}
		case 37:
			{
			TCHAR s[]=_T("..\\video\\YYF-FALL.AVI");
			plays(s);
			break;
			}
		case 38:
			{
			TCHAR s[]=_T("..\\video\\ZX-FIRST.AVI");
			plays(s);
			break;
			}
		}
	}
	if(mode==-13){//arcturus
		switch(u)
		{
		case 0:
			{
			TCHAR s[]=_T("movie\\arcturus.avi");
			plays(s);
			break;
			}
		}
	}
	if(mode==-14){//arcturus
		switch(u)
		{
		case 43:
			{
			TCHAR s[]=_T("FS43.bik");
			plays(s);
			break;
			}
		case 45:
			{
			TCHAR s[]=_T("FS45.bik");
			plays(s);
			break;
			}
		case 46:
			{
			TCHAR s[]=_T("FS46.bik");
			plays(s);
			break;
			}
		case 47:
			{
			TCHAR s[]=_T("FS47.bik");
			plays(s);
			break;
			}
		}
	}
	if(mode==-15){//arcturus
		switch(u)
		{
		case 49:
			{
			TCHAR s[]=_T("falcom.bik");
			plays(s);
			break;
			}
		case 50:
			{
			TCHAR s[]=_T("FS250.bik");
			plays(s);
			break;
			}
		case 51:
			{
			TCHAR s[]=_T("FS251.bik");
			plays(s);
			break;
			}
		}
	}
}
IAMStreamSelect *ia=NULL;

HRESULT CDouga::EnumFilters (IGraphBuilder *pGraph,int no) 
{
    IEnumFilters *pEnum = NULL;
    IBaseFilter *pFilter;
    ULONG cFetched;
	CString s,ss;
	int i=0;

    HRESULT hr = pGraph->EnumFilters(&pEnum);
    if (FAILED(hr)) return hr;

    while(pEnum->Next(1, &pFilter, &cFetched) == S_OK)
    {
		IEnumPins *p;
		IPin *pPin;
        FILTER_INFO FilterInfo,FilterInfo1;
        pFilter->QueryFilterInfo(&FilterInfo);
		pFilter->EnumPins(&p);
        char szName[MAX_FILTER_NAME];
        char szName1[MAX_FILTER_NAME];
        int cch = WideCharToMultiByte(CP_ACP, 0, FilterInfo.achName,
            -1, szName, MAX_FILTER_NAME, 0, 0);
		if (cch > 0){
			ss=szName;
		}
		while(p->Next(1, &pPin, 0) == S_OK)
		{
			PIN_DIRECTION PinDirThis;
			pPin->QueryDirection(&PinDirThis);
			if (PinDirThis == PINDIR_OUTPUT){
				PIN_INFO pp;
				IPin *pn;
				if(pPin->ConnectedTo(&pn)==S_OK){
					pn->QueryPinInfo(&pp);
					pp.pFilter->QueryFilterInfo(&FilterInfo1);
					WideCharToMultiByte(CP_ACP, 0, FilterInfo1.achName,
						-1, szName1, MAX_FILTER_NAME, 0, 0);
					s=szName1;
					if(s.Right(10)==_T("d Renderer")){
						if(st12==0 && pBasicAudio) pBasicAudio->Release();
						if(st12==0)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					if(s.Right(11)==_T("d Renderer2")){
						if(st12==1 && pBasicAudio) pBasicAudio->Release();
						if(st12==1)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					if(s.Right(11)==_T("d Renderer3")){
						if(st12==2 && pBasicAudio) pBasicAudio->Release();
						if(st12==2)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					if(s.Right(11)==_T("d Renderer4")){
						if(st12==3 && pBasicAudio) pBasicAudio->Release();
						if(st12==3)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					if(s.Right(11)==_T("d Renderer5")){
						if(st12==4 && pBasicAudio) pBasicAudio->Release();
						if(st12==4)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					if(s.Right(11)==_T("d Renderer6")){
						if(st12==5 && pBasicAudio) pBasicAudio->Release();
						if(st12==5)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					if(s.Right(11)==_T("d Renderer7")){
						if(st12==6 && pBasicAudio) pBasicAudio->Release();
						if(st12==6)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					if(s.Right(11)==_T("d Renderer8")){
						if(st12==7 && pBasicAudio) pBasicAudio->Release();
						if(st12==7)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					if(s.Right(11)==_T("d Renderer9")){
						if(st12==8 && pBasicAudio) pBasicAudio->Release();
						if(st12==8)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					if(s.Right(11)==_T("d Renderer10")){
						if(st12==9 && pBasicAudio) pBasicAudio->Release();
						if(st12==9)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1);
							pBasicAudio1->put_Volume(-10000);
							pBasicAudio1->Release();
						}
					}
					pn->Release();
				}
			}
		}
		p->Release();
        // FILTER_INFO 構造体はフィルタ グラフ マネージャへのポインタを保持する。
        // その参照カウントは解放しなければならない。
        if (FilterInfo.pGraph != NULL)
            FilterInfo.pGraph->Release();
        pFilter->Release();
    }

    pEnum->Release();
    return S_OK;
}

long height=0, width=0;

void CDouga::DumpFilterGraph()
{
	if (!pGraphBuilder) return;

	IEnumFilters* pEnum = NULL;
	IBaseFilter* pFilter = NULL;
	ULONG cFetched;
	CString output = L"=== Filter Graph ===\n";

	if (SUCCEEDED(pGraphBuilder->EnumFilters(&pEnum)))
	{
		while (pEnum->Next(1, &pFilter, &cFetched) == S_OK)
		{
			FILTER_INFO filterInfo;
			pFilter->QueryFilterInfo(&filterInfo);
			output += filterInfo.achName;
			output += L"\n";

			// ピンを列挙
			IEnumPins* pEnumPins = NULL;
			if (SUCCEEDED(pFilter->EnumPins(&pEnumPins)))
			{
				IPin* pPin = NULL;
				while (pEnumPins->Next(1, &pPin, NULL) == S_OK)
				{
					PIN_INFO pinInfo;
					pPin->QueryPinInfo(&pinInfo);

					output += L"  ";
					output += pinInfo.achName;

					IPin* pConnected = NULL;
					if (pPin->ConnectedTo(&pConnected) == S_OK)
					{
						output += L" -> Connected";
						pConnected->Release();
					}
					else
					{
						output += L" -> Not Connected";
					}
					output += L"\n";

					if (pinInfo.pFilter) pinInfo.pFilter->Release();
					pPin->Release();
				}
				pEnumPins->Release();
			}

			if (filterInfo.pGraph) filterInfo.pGraph->Release();
			pFilter->Release();
		}
		pEnum->Release();
	}

	OutputDebugString(output);
}

void CDouga::plays2()
{
	videocnt3 = 0;
	height = 0; width = 0;

	if (pGraphBuilder)EnumFilters(pGraphBuilder, 0);
	if (pVideoWindow)pVideoWindow->put_Owner((OAHWND)m_hWnd);
	if (pVideoWindow)pVideoWindow->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS);
	if (pVideoWindow)pVideoWindow->put_MessageDrain((OAHWND)m_hWnd);
	if (pGraphBuilder)pGraphBuilder->QueryInterface(IID_IBasicVideo, (LPVOID*)&pBasicVideo);

	// IBasicVideo2インターフェースを取得
	IBasicVideo2* pBasicVideo2 = NULL;
	if (pGraphBuilder)pGraphBuilder->QueryInterface(IID_IBasicVideo2, (LPVOID*)&pBasicVideo2);

	// 音量設定
	if (savedata.dsvol == -498) {
		if (pBasicAudio)pBasicAudio->put_Volume(-10000);
	}
	else {
		if (pBasicAudio)pBasicAudio->put_Volume((savedata.dsvol - 1) * 7);
	}

	width = 0;

	if (ev) {
		SIZE a = { 0 }, b = { 0 };
		Vdc->GetNativeVideoSize(&a, &b);
		width = a.cx;
		height = a.cy;
	}
	else {
		if (pBasicVideo)pBasicVideo->get_VideoHeight(&height);
		if (pBasicVideo)pBasicVideo->get_VideoWidth(&width);
	}

	// アスペクト比を考慮したサイズ計算
	long actualWidth = width;
	long actualHeight = height;

	if (pBasicVideo2)
	{
		long aspectX = 0, aspectY = 0;
		HRESULT hr = pBasicVideo2->GetPreferredAspectRatio(&aspectX, &aspectY);

		if (SUCCEEDED(hr) && aspectX > 0 && aspectY > 0)
		{
			// アスペクト比を使って実際の表示サイズを計算
			// 高さを基準にして幅を調整
			actualWidth = (long)((double)height * aspectX / aspectY);
		}
	}

	if (pVideoWindow)pVideoWindow->SetWindowPosition(0, 0, actualHeight, actualWidth);

	rc.top = 0; rc.left = 0; rc.right = actualWidth; rc.bottom = actualHeight;
	rcm.top = 0; rcm.left = 0; rcm.right = actualWidth; rcm.bottom = actualHeight;

	// 以前のハードコード補正は不要になりますわ
	/*
	if(rcm.right==352&&rcm.bottom==480){
		rcm.right=640;
	}
	if(rcm.right==1440&&rcm.bottom==1080){
		rcm.right=1920;
	}
	if(rcm.right==1440&&rcm.bottom==1088){
		rcm.right=1920;
	}
	*/

	if (rcm.right == 704 && rcm.bottom == 480) {
		rcm.bottom = 396;
	}

	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
	if (rc.left < 0) rc.left = 0;
	if (rc.top < 0) rc.top = 0;
	MoveWindow(rc.left + 10, rc.top + 10, rc.right - rc.left + 10, rc.bottom - rc.top + 10, TRUE);

	if (width == 0) {
		ShowWindow(SW_HIDE);
	}
	else {
		if (pVideoWindow)pVideoWindow->put_Visible(OATRUE);
		ShowWindow(SW_SHOWNORMAL);
		UpdateWindow();
		SetTimer(155, 200, NULL);
	}

	if (savedata.gx == -10000) {
	}
	else {
		MoveWindow(savedata.gx, savedata.gy, 100, 100);
	}

	switch (savedata.douga) {
	case 0:OnMenuitem32771(); break;
	case 1:OnMenuitem32772(); break;
	case 2:OnMenuitem32773(); break;
	case 3:OnMenuitem32774(); break;
	}

	SetTimer(1255, 200, NULL);

	// 解放をお忘れなく
	if (pBasicVideo2) pBasicVideo2->Release();

	DumpFilterGraph();
}

extern REFTIME aa2,aa;
void CDouga::seek(LONGLONG l)
{
	if(pMediaSeeking)pMediaSeeking->SetTimeFormat(&TIME_FORMAT_MEDIA_TIME);
	REFERENCE_TIME rtpos = l;
/*	if(aa2==0){}else{
		REFTIME t;
		pMediaPosition->get_CurrentPosition(&t);
		LONGLONG te=(LONGLONG)SeekPoint(filesize,(float)(t*100/aa));
//		pMediaSeeking->ConvertTimeFormat(&rtpos,NULL,te,&TIME_FORMAT_SAMPLE);
		rtpos=te*100;
	}*/
	if(pMediaSeeking)pMediaSeeking->SetPositions(&rtpos,AM_SEEKING_AbsolutePositioning,NULL,AM_SEEKING_NoPositioning);

}

extern int ps;
void CDouga::pause(int a)
{
	if(a==0)
	{
		pMediaControl->Pause();
		ps=1;og->m_ps.SetWindowText(LL14(L"再開", L"Resume", L"Reprendre", L"Riprendi", L"Reanudar", L"재개", L"恢复", L"استئناف", L"Продолжить", L"Fortsetzen", L"Retomar", L"Hervatten", L"Wznów", L"Sürdür"));
	}else{
		pMediaControl->Run();
		ps=0;og->m_ps.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시 정지", L"暂停", L"إيقاف مؤقت", L"Пауза", L"Pause", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"));
	}
}

void CDouga::stops()
{
	CRect r,rr;
	GetWindowRect(&r);
	savedata.gx=r.left;
	savedata.gy=r.top;
	if(savedata.douga==3){
		rr.top=r.top-savedata.p.top;
		rr.left=r.left-savedata.p.left;
		savedata.p.top+=rr.top;
		savedata.p.left+=rr.left;
		savedata.p.bottom+=rr.top;
		savedata.p.right+=rr.left;
	}
	if(pMediaControl)pMediaControl->Stop();
	REFERENCE_TIME rtpos = 0;
	if(pMediaSeeking)pMediaSeeking->SetPositions(&rtpos,AM_SEEKING_AbsolutePositioning,NULL,AM_SEEKING_NoPositioning);
//	pMediaControl->Pause();
	if(pVideoWindow)pVideoWindow->put_Visible(OAFALSE);
	if(pVideoWindow)pVideoWindow->put_Owner(NULL);
	
	RELEASE(pop);
	RELEASE(vr);
	RELEASE(pMediaPosition);
	RELEASE(pBasicAudio);
	RELEASE(service);
	RELEASE(Vdc);
	RELEASE(prenda);
	RELEASE(prenda2);
	RELEASE(prenda3);
	RELEASE(prenda4);
	RELEASE(prenda5);
	RELEASE(prenda6);
	RELEASE(prenda7);
	RELEASE(prenda8);
	RELEASE(prenda9);
	RELEASE(prenda10);
	RELEASE(prend);
	RELEASE(pDSRenderer);
	RELEASE(pDSRenderer2);
	RELEASE(pDSRenderer3);
	RELEASE(pDSRenderer4);
	RELEASE(pDSRenderer5);
	RELEASE(pDSRenderer6);
	RELEASE(pDSRenderer7);
	RELEASE(pDSRenderer8);
	RELEASE(pDSRenderer9);
	RELEASE(pDSRenderer10);
	RELEASE(pACM);
	RELEASE(pRenderer0);
	RELEASE(pRenderer0_);
	RELEASE(pRenderer1);
	RELEASE(pRenderer);
	RELEASE(pColour);
	RELEASE(pAviDecomp);
	RELEASE(pSourceFilter);
	RELEASE(Haali);
	RELEASE(pSplitter);
	RELEASE(pCaptureGraphBuilder2);
	RELEASE(pSource2);
	RELEASE(pSource1);
	RELEASE(pSource);
	RELEASE(pVmr9);
	RELEASE(pMediaControl);
	RELEASE(pBasicVideo);
	RELEASE(pVideoWindow);
	RELEASE(pMediaSeeking);
	RELEASE(iam);
	RELEASE1(pGraphBuilder);
	CoUninitialize();
	if(mode==-14) Sleep(500);
	ev=FALSE;
}

void CDouga::stop()
{
	if(mode==-2) stops();
	if(u1!=0)
	{
		if (mode == -1) {//ED6SC
				stops();
     	}
		if(mode==1){//ED6SC
			switch(u1)
			{
			case 98:
			case 99:
			case 100:
			case 101:
			case 102:
			case 103:
			case 104:
			case 105:
			case 106:
			case 107:
					stops();
					break;
			}
		}
	}
	if(mode==2){//ED6FC
		switch(u1)
		{
		case 55:
		case 56:
		case 57:
		case 58:
					stops();
					break;
		}
	}
	if(mode==3){//YSF
		switch(u1)
		{
		case 32:
		case 33:
		case 25:
		case 31:
					stops();
					break;
		}
	}
	if(mode==4){//YS6
		switch(u1)
		{
		case 1:
		case 25:
		case 26:
		case 27:
		case 28:
					stops();
					break;

		}
	}
	if(mode==5){//YSF
		switch(u1+1)
		{
		case 41:
		case 42:
		case 43:
		case 44:
		case 45:
		case 46:
		case 47:
		case 48:
				stops();
				break;
		}
	}
	if(mode==6){//YSF
		switch(u1)
		{
		case 141:
		case 142:
		case 143:
		case 144:
		case 145:
		case 146:
								stops();
								break;
		}
	}
	if(mode==7){//YSF
		switch(u1)
		{
		case 65:
		case 66:
			{
					stops();
				break;
			}
		}
	}
	if(mode==8){//YSC1
		switch(u1)
		{
		case 72:
		case 73:
		case 74:
			{
					stops();
				break;
			}
		}
	}
	if(mode==9){//YSC1
		switch(u1)
		{
		case 93:
		case 94:
		case 95:
		case 96:
		case 97:
		case 98:
		case 99:
		case 100:
		case 101:
			{
					stops();
				break;
			}
		}
	}
	if(mode==10){//XANADU
		switch(u1)
		{
		case 24:
		case 25:
			{
					stops();
				break;
			}
		}
	}
	if(mode==11){//ys1
		switch(u1)
		{
		case 25:
			{
					stops();
				break;
			}
		}
	}
	if(mode==12){//ys2
		switch(u1)
		{
		case 31:
		case 32:
			{
					stops();
				break;
			}
		}
	}
	if(mode==15){//gurumin
		switch(u1)
		{
		case 40:
			{
					stops();
				break;
			}
		}
	}
	if(mode==16){//dino
		switch(u1)
		{
		case 33:
			{
					stops();
				break;
			}
		}
	}
	if(mode==19){//ed4
		switch(u1)
		{
		case 1:
		case 2:
			{
					stops();
				break;
			}
		}
	}
	if(mode==-11){//ed4
		switch(u1)
		{
		case 28:
		case 29:
		case 30:
		case 31:
		case 32:
		case 33:
		case 34:
		case 35:
		case 36:
		case 37:
		case 38:
			{
					stops();
				break;
			}
		}
	}
	if(mode==-13){//arc
		switch(u1)
		{
		case 0:
			{
					stops();
				break;
			}
		}
	}
	if(mode==-14){//arc
		switch(u1)
		{
		case 43:
		case 47:
		case 46:
		case 45:
			{
					stops();
					
				break;
			}
		}
	}
	if(mode==-15){//arc
		switch(u1)
		{
		case 50:
		case 51:
		case 49:
			{
					stops();
					
				break;
			}
		}
	}
	u1=0;
	ShowWindow(SW_HIDE);
}

	int x,y,x1,y1_,lu=0;
	double xx,yy,xx1,yy1_,t;

	int mousecnt=0,mousecnt1=0;
	int poix,poiy;
#if WIN64
void CDouga::OnTimer(UINT_PTR nIDEvent) 
#else
void CDouga::OnTimer(UINT nIDEvent) 
#endif
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください

	if(nIDEvent==1255){
		if(savedata.fs==0){
			CRect r,rr;
			GetWindowRect(&r);
			savedata.gx=r.left;
			savedata.gy=r.top;
			if(savedata.douga==3){
				rr.top=r.top-savedata.p.top;
				rr.left=r.left-savedata.p.left;
				savedata.p.top+=rr.top;
				savedata.p.left+=rr.left;
				savedata.p.bottom+=rr.top;
				savedata.p.right+=rr.left;
			}
		}
	}
	if(nIDEvent==155){
		KillTimer(155);
		::SetWindowPos(m_hWnd,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
		SetFocus();
	}
	if(nIDEvent==3366){
		mousecnt++;if(mousecnt1==0 && mousecnt>3){
			mousecnt1=1;
			int j;
			for(;;){
				j=ShowCursor(FALSE);if(j<0) break;
			}
		}
	}
	if(nIDEvent==1597){
		KillTimer(1597);
		si=1;
		PostMessage(WM_SIZE,0,0);
	}
	if(nIDEvent==2987){
		pcnt++;if(pcnt==10)KillTimer(2987);
		int cx=GetSystemMetrics(SM_CXSCREEN);
		int cy=GetSystemMetrics(SM_CYSCREEN);
		RECT rect;
		rect.top   = 0;
		rect.left  = 0;
		rect.bottom= cy;
		rect.right = cx;
		InvalidateRect(&rect,TRUE);
	}
	CFrameWnd::OnTimer(nIDEvent);
}

void CDouga::OnSizing(UINT fwSide, LPRECT pRect) 
{
	CFrameWnd::OnSizing(fwSide, pRect);
//	if(lu==1){lu=0; return;}
	RECT r,rr;
	// TODO: この位置にメッセージ ハンドラ用のコードを追加してください
	 //左右比を保つ
	r.bottom=rcm.bottom;r.top=rcm.top;
	r.right=rcm.right;r.left=rcm.left;
    int     width,height;
	double _x1,_y1;
    width=r.right-r.left;
    height=r.bottom-r.top;
	x=r.bottom-r.top; y=r.right-r.left;xx=(double)y; yy=(double)x;//動画の画像の大きさを獲得
	r.bottom=pRect->bottom;	r.top=pRect->top;
	r.right=pRect->right;	r.left=pRect->left;
	x1=r.bottom - r.top; y1_=r.right - r.left;xx1=(double)y1_; yy1_=(double)x1;//現在のサイズ獲得
	_x1=xx1/xx;
	_y1=yy1_/yy;
	switch(fwSide){
		case WMSZ_TOP:
		case WMSZ_BOTTOM:
			pRect->right=pRect->left+(int)(width*_y1)-(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION));
			break;
		case WMSZ_LEFT:
        case WMSZ_RIGHT:
			pRect->bottom=pRect->top+(int)(height*_x1)+(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION));
			break;
		case WMSZ_BOTTOMRIGHT:
			if(((double)width<(double)height))
				pRect->right=pRect->left+(int)(width*_y1);
			else
				pRect->bottom=pRect->top+(int)(height*_x1);
			break;
		case    WMSZ_TOPLEFT:
			if(((double)width<(double)height))
                pRect->left=pRect->right-(int)(width*_y1);
            else
                pRect->top=pRect->bottom-(int)(height*_x1);
			break;
 		case    WMSZ_TOPRIGHT:
			if(((double)width<(double)height))
				pRect->right=pRect->left+(int)(width*_y1);
            else
                pRect->top=pRect->bottom-(int)(height*_x1);
			break;
		case    WMSZ_BOTTOMLEFT:
			if(((double)width<(double)height))
                pRect->left=pRect->right-(int)(width*_y1);
            else
				pRect->bottom=pRect->top+(int)(height*_x1);
			break;
	}
	savedata.p.top=pRect->top;
	savedata.p.left=pRect->left;
	savedata.p.bottom=pRect->bottom;
	savedata.p.right=pRect->right;
	savedata.douga=3;
//    SetWindowPos(NULL, 0,0,(int)yy1_, (int)xx1,   SWP_NOMOVE|SWP_NOOWNERZORDER);
//	RY=xx1; RX=yy1_;
//	CheckSize(fwSide,(LPARAM)pRect);
	GetClientRect(&rr);
	GetWindowRect(&r);
	MoveWindow(&r);
	if(ev){
		MFVideoNormalizedRect mvnr={0, 0, 1, 1};
		Vdc->SetVideoPosition(&mvnr, &rr);
	}else{
		pBasicVideo->put_DestinationWidth(rr.right);
		pBasicVideo->put_DestinationHeight(rr.bottom);
		pVideoWindow->put_Top(0);
		pVideoWindow->put_Left(0);
		pVideoWindow->put_Height(rr.bottom);
		pVideoWindow->put_Width(rr.right);
	}
	si=1;
//	SetTimer(1597,30,NULL);
	/**/
}



void CDouga::OnSize(UINT nType, int cx, int cy) 
{
	CFrameWnd::OnSize(nType, cx, cy);
	if(si==1){
		si=0;
		OnMenuitem32774();
	}
}

void CDouga::OnClose() 
{

	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	CFrameWnd::OnClose();
}


void CDouga::OnShowWindow(BOOL bShow, UINT nStatus) 
{
	CFrameWnd::OnShowWindow(bShow, nStatus);
	
	// TODO: この位置にメッセージ ハンドラ用のコードを追加してください
	
}
void CDouga::OnRButtonDown(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。

	CFrameWnd::OnRButtonDown(nFlags, point);
}
void CDouga::OnNcRButtonDown(UINT nHitTest, CPoint point)
{
	if (ev)
	{
		if (point.x == -1 && point.y == -1) {
			// キーストロークの発動
			CRect rect;
			GetClientRect(rect);
			ClientToScreen(rect);
			point = rect.TopLeft();
			point.Offset(5, 5);
		}

		CMenu menu, * sub1, * sub2;
		if (savedata.fs)
			VERIFY(menu.LoadMenu(CG_IDR_POPUP_DOUGA));
		else
			VERIFY(menu.LoadMenu(CG_IDR_POPUP_DOUGA1));

		sub1 = menu.GetSubMenu(0);

		// ========== 映像ストリーム (インデックス6) ==========
		sub2 = sub1->GetSubMenu(6);
		UpdateStreamMenu(sub2, streamname1, 10,
			LL14(L"映像", L"Video", L"Vidéo", L"Video",
				L"Vídeo", L"비디오", L"视频", L"فيديو",
				L"Видео", L"Video", L"Vídeo", L"Video",
				L"Wideo", L"Video"));
		DeleteEmptyMenuItems(menu, streamname1, 10, ID_MV1);

		// ========== 音声ストリーム (インデックス7) ==========
		sub2 = sub1->GetSubMenu(7);
		UpdateStreamMenu(sub2, streamname, 40,
			LL14(L"音声", L"Audio", L"Audio", L"Audio",
				L"Audio", L"오디오", L"音频", L"صوت",
				L"Аудио", L"Audio", L"Áudio", L"Audio",
				L"Audio", L"Ses"));
		DeleteAudioMenuItems(menu);

		// ========== 字幕/その他ストリーム (インデックス8) ==========
		sub2 = sub1->GetSubMenu(8);
		UpdateStreamMenu(sub2, streamname2, 40, L"");
		DeleteEmptyMenuItems(menu, streamname2, 40, ID_ETC1);

		// メニュー表示
		CMenu* pPopup = menu.GetSubMenu(0);
		ASSERT(pPopup != NULL);
		if (savedata.fs)
			LocalizeDougaMenu(pPopup);
		else
			LocalizeDougaMenu1(pPopup);
		CWnd* pWndPopupOwner = this;

		while (pWndPopupOwner->GetStyle() & WS_CHILD)
			pWndPopupOwner = pWndPopupOwner->GetParent();

		pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y,
			pWndPopupOwner);
	}
	CFrameWnd::OnNcRButtonDown(nHitTest, point);
}

void CDouga::UpdateStreamMenu(CMenu* pMenu, CString* streamNames, int maxCount, LPCWSTR prefix)
{
	MENUITEMINFO mii = { sizeof(mii) };
	mii.fMask = MIIM_STRING;

	for (int i = 0; i < maxCount; i++)
	{
		CString buf;
		if (wcslen(prefix) > 0)
		{
			// 映像・音声の場合はプレフィックス付き
			if (i < 9)
				buf.Format(L"%s %d:%s", prefix, i + 1, streamNames[i]);
			else
				buf.Format(L"%s %d:%s", prefix, i + 1, streamNames[i]);
		}
		else
		{
			// 字幕の場合はストリーム名のみ
			buf.Format(L"%s", streamNames[i]);
		}

		mii.dwTypeData = buf.GetBuffer();
		pMenu->SetMenuItemInfo(i, &mii, TRUE);
	}
}

void CDouga::DeleteEmptyMenuItems(CMenu& menu, CString* streamNames, int maxCount, UINT baseID)
{
	for (int i = maxCount - 1; i >= 0; i--)
	{
		if (streamNames[i] == "")
		{
			menu.DeleteMenu(baseID + i, MF_BYCOMMAND);
		}
	}
}

// 音声メニュー項目の削除（audionumに基づく）
void CDouga::DeleteAudioMenuItems(CMenu& menu)
{
	for (int i = 40; i > audionum; i--)
	{
		menu.DeleteMenu(ID_ST1 + (i - 1), MF_BYCOMMAND);
	}
}

// CG_IDR_POPUP_DOUGA メニューの固定テキスト項目を多言語化する
void CDouga::LocalizeDougaMenu(CMenu* pPopup)
{
	// 一時停止/再開
	pPopup->ModifyMenu(32775, MF_BYCOMMAND | MF_STRING, 32775,
		LL14(L"一時停止/再開 (&C)", L"Pause/Resume (&C)", L"Pause/Reprendre (&C)", L"Pausa/Riprendi (&C)",
			L"Pausa/Reanudar (&C)", L"일시정지/재개 (&C)", L"暂停/继续 (&C)", L"إيقاف مؤقت/استئناف (&C)",
			L"Пауза/Возобновить (&C)", L"Pause/Fortsetzen (&C)", L"Pausar/Retomar (&C)", L"Pauzeren/Hervatten (&C)",
			L"Pauza/Wznów (&C)", L"Duraklat/Devam Et (&C)"));

	// 映像ストリーム サブメニュータイトル (位置インデックス 6)
	if (CMenu* pSub = pPopup->GetSubMenu(6))
		pPopup->ModifyMenu(6, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)pSub->m_hMenu,
			LL14(L"映像ストリーム", L"Video Stream", L"Flux vidéo", L"Flusso video",
				L"Flujo de vídeo", L"비디오 스트림", L"视频流", L"تدفق الفيديو",
				L"Видеопоток", L"Videostream", L"Fluxo de vídeo", L"Videostream",
				L"Strumień wideo", L"Video Akışı"));

	// 音声ストリーム サブメニュータイトル (位置インデックス 7)
	if (CMenu* pSub = pPopup->GetSubMenu(7))
		pPopup->ModifyMenu(7, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)pSub->m_hMenu,
			LL14(L"音声ストリーム", L"Audio Stream", L"Flux audio", L"Flusso audio",
				L"Flujo de audio", L"오디오 스트림", L"音频流", L"تدفق الصوت",
				L"Аудиопоток", L"Audiostream", L"Fluxo de áudio", L"Audiostream",
				L"Strumień audio", L"Ses Akışı"));

	// 字幕ストリーム サブメニュータイトル (位置インデックス 8)
	if (CMenu* pSub = pPopup->GetSubMenu(8))
		pPopup->ModifyMenu(8, MF_BYPOSITION | MF_POPUP | MF_STRING, (UINT_PTR)pSub->m_hMenu,
			LL14(L"字幕ストリーム", L"Subtitle Stream", L"Flux de sous-titres", L"Flusso sottotitoli",
				L"Flujo de subtítulos", L"자막 스트림", L"字幕流", L"تدفق الترجمة",
				L"Поток субтитров", L"Untertitelstream", L"Fluxo de legendas", L"Ondertitelstream",
				L"Strumień napisów", L"Altyazı Akışı"));

	// 操作ヒント: 上下左右キー
	pPopup->ModifyMenu(ID__32783, MF_BYCOMMAND | MF_STRING, ID__32783,
		LL14(L"上下左右キー音量とシークが出来ます。", L"Arrow keys: volume & seek.",
			L"Touches fléchées : volume et défilement.", L"Frecce: volume e avanzamento.",
			L"Teclas de flecha: volumen y posición.", L"방향키: 음량 및 탐색.",
			L"方向键: 音量和搜索。", L"مفاتيح الأسهم: الصوت والتقديم.",
			L"Стрелки: громкость и перемотка.", L"Pfeiltasten: Lautstärke & Suche.",
			L"Teclas de seta: volume e busca.", L"Pijltoetsen: volume & zoeken.",
			L"Klawisze strzałek: głośność i wyszukiwanie.", L"Ok tuşları: ses ve arama."));

	// 操作ヒント: ダブルクリック
	pPopup->ModifyMenu(ID__32784, MF_BYCOMMAND | MF_STRING, ID__32784,
		LL14(L"ダブルクリックでフルスクリーンです。", L"Double-click for fullscreen.",
			L"Double-clic pour plein écran.", L"Doppio clic per schermo intero.",
			L"Doble clic para pantalla completa.", L"더블클릭: 전체화면.",
			L"双击进入全屏。", L"انقر نقراً مزدوجاً للشاشة الكاملة.",
			L"Двойной щелчок — полный экран.", L"Doppelklick für Vollbild.",
			L"Clique duplo para tela cheia.", L"Dubbelklik voor volledig scherm.",
			L"Dwuklik dla pełnego ekranu.", L"Tam ekran için çift tıklayın."));
}

// CG_IDR_POPUP_DOUGA1 メニュー（ウィンドウモード専用項目＋共通項目）を多言語化する
void CDouga::LocalizeDougaMenu1(CMenu* pPopup)
{
	// 通常(1x1)
	pPopup->ModifyMenu(ID_MENUITEM32771, MF_BYCOMMAND | MF_STRING, ID_MENUITEM32771,
		LL14(L"通常(1x1)", L"Normal (1x1)", L"Normal (1x1)", L"Normale (1x1)",
			L"Normal (1x1)", L"표준 (1x1)", L"标准 (1x1)", L"عادي (1×1)",
			L"Обычный (1x1)", L"Normal (1x1)", L"Normal (1x1)", L"Normaal (1x1)",
			L"Normalny (1x1)", L"Normal (1x1)"));

	// 中間(1.5x1.5)
	pPopup->ModifyMenu(ID_MENUITEM32773, MF_BYCOMMAND | MF_STRING, ID_MENUITEM32773,
		LL14(L"中間(1.5x1.5)", L"Medium (1.5x1.5)", L"Moyen (1,5x1,5)", L"Medio (1.5x1.5)",
			L"Mediano (1.5x1.5)", L"중간 (1.5x1.5)", L"中等 (1.5x1.5)", L"متوسط (1.5×1.5)",
			L"Средний (1.5x1.5)", L"Mittel (1,5x1,5)", L"Médio (1.5x1.5)", L"Middel (1.5x1.5)",
			L"Średni (1.5x1.5)", L"Orta (1.5x1.5)"));

	// 倍(2x2)
	pPopup->ModifyMenu(ID_MENUITEM32772, MF_BYCOMMAND | MF_STRING, ID_MENUITEM32772,
		LL14(L"倍(2x2)", L"Large (2x2)", L"Grand (2x2)", L"Grande (2x2)",
			L"Grande (2x2)", L"2배 (2x2)", L"双倍 (2x2)", L"كبير (2×2)",
			L"Двойной (2x2)", L"Groß (2x2)", L"Grande (2x2)", L"Groot (2x2)",
			L"Duży (2x2)", L"Büyük (2x2)"));

	// DOUGA と共通の項目を翻訳
	LocalizeDougaMenu(pPopup);
}

//void CDouga::OnNcLButtonDown(UINT nHitTest, CPoint point)
//{
//	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
//	int a=0;
//	a=a;
//}

void CDouga::OnContextMenu(CWnd*, CPoint point)
{
	if(savedata.fs)return;
	// CG: このブロックはポップアップ メニュー コンポーネントによって追加されました
	{
		if (point.x == -1 && point.y == -1){
			//キーストロークの発動
			CRect rect;
			GetClientRect(rect);
			ClientToScreen(rect);

			point = rect.TopLeft();
			point.Offset(5, 5);
		}

		CMenu menu;
		VERIFY(menu.LoadMenu(CG_IDR_POPUP_DOUGA));
		if(audionum<=9)
			menu.DeleteMenu(ID_ST10,MF_BYCOMMAND);
		if(audionum<=8)
			menu.DeleteMenu(ID_ST9,MF_BYCOMMAND);
		if(audionum<=7)
			menu.DeleteMenu(ID_ST8,MF_BYCOMMAND);
		if(audionum<=6)
			menu.DeleteMenu(ID_ST7,MF_BYCOMMAND);
		if(audionum<=5)
			menu.DeleteMenu(ID_ST6,MF_BYCOMMAND);
		if(audionum<=4)
			menu.DeleteMenu(ID_ST5,MF_BYCOMMAND);
		if(audionum<=3)
			menu.DeleteMenu(ID_ST4,MF_BYCOMMAND);
		if(audionum<=2)
			menu.DeleteMenu(ID_ST3,MF_BYCOMMAND);
		if(audionum<=1)
			menu.DeleteMenu(ID_ST2,MF_BYCOMMAND);
		CMenu* pPopup = menu.GetSubMenu(0);
		ASSERT(pPopup != NULL);
		LocalizeDougaMenu(pPopup);
		CWnd* pWndPopupOwner = this;

		while (pWndPopupOwner->GetStyle() & WS_CHILD)
			pWndPopupOwner = pWndPopupOwner->GetParent();

		pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y,
			pWndPopupOwner);
	}
}

// 汎用的なストリーム切替関数
BOOL CDouga::SwitchStream(int streamType, int index)
{
	if (!pGraphBuilder || !iam) return FALSE;

	int actualIndex = -1;

	switch (streamType) {
	case 0: // 映像
		if (index >= 0 && index < streamMap.videoCount) {
			actualIndex = streamMap.videoStart + index;
		}
		break;

	case 1: // 音声
		if (index >= 0 && index < streamMap.audioCount) {
			actualIndex = streamMap.audioStart + index;
		}
		break;

	case 2: // 字幕
		if (index >= 0 && index < streamMap.subtitleCount) {
			actualIndex = streamMap.subtitleStart + index;
		}
		break;
	}

	// デバッグ出力
	CString debug;
	debug.Format(L"SwitchStream: type=%d, index=%d, actualIndex=%d", streamType, index, actualIndex);
	OutputDebugString(debug);

	if (actualIndex >= 0) {
		HRESULT hr = iam->Enable(actualIndex, AMSTREAMSELECTENABLE_ENABLE);

		debug.Format(L"IAMStreamSelect::Enable(%d) = 0x%08X", actualIndex, hr);
		OutputDebugString(debug);

		return SUCCEEDED(hr);
	}

	return FALSE;
}

// マクロ定義
#define DEFINE_VIDEO_SWITCH(idx) \
    void CDouga::OnMV##idx() { \
        SwitchStream(0, idx - 1); \
    }

#define DEFINE_AUDIO_SWITCH(idx) \
    void CDouga::OnST##idx() { \
        st12 = idx; \
        SwitchStream(1, idx - 1); \
    }

#define DEFINE_SUBTITLE_SWITCH(idx) \
    void CDouga::OnETC##idx() { \
        SwitchStream(2, idx - 1); \
    }

// ========== 映像切替 (MV1～MV10) 10個 ==========
DEFINE_VIDEO_SWITCH(1)
DEFINE_VIDEO_SWITCH(2)
DEFINE_VIDEO_SWITCH(3)
DEFINE_VIDEO_SWITCH(4)
DEFINE_VIDEO_SWITCH(5)
DEFINE_VIDEO_SWITCH(6)
DEFINE_VIDEO_SWITCH(7)
DEFINE_VIDEO_SWITCH(8)
DEFINE_VIDEO_SWITCH(9)
DEFINE_VIDEO_SWITCH(10)

// ========== 音声切替 (ST1～ST40) 40個 ==========
DEFINE_AUDIO_SWITCH(1)
DEFINE_AUDIO_SWITCH(2)
DEFINE_AUDIO_SWITCH(3)
DEFINE_AUDIO_SWITCH(4)
DEFINE_AUDIO_SWITCH(5)
DEFINE_AUDIO_SWITCH(6)
DEFINE_AUDIO_SWITCH(7)
DEFINE_AUDIO_SWITCH(8)
DEFINE_AUDIO_SWITCH(9)
DEFINE_AUDIO_SWITCH(10)
DEFINE_AUDIO_SWITCH(11)
DEFINE_AUDIO_SWITCH(12)
DEFINE_AUDIO_SWITCH(13)
DEFINE_AUDIO_SWITCH(14)
DEFINE_AUDIO_SWITCH(15)
DEFINE_AUDIO_SWITCH(16)
DEFINE_AUDIO_SWITCH(17)
DEFINE_AUDIO_SWITCH(18)
DEFINE_AUDIO_SWITCH(19)
DEFINE_AUDIO_SWITCH(20)
DEFINE_AUDIO_SWITCH(21)
DEFINE_AUDIO_SWITCH(22)
DEFINE_AUDIO_SWITCH(23)
DEFINE_AUDIO_SWITCH(24)
DEFINE_AUDIO_SWITCH(25)
DEFINE_AUDIO_SWITCH(26)
DEFINE_AUDIO_SWITCH(27)
DEFINE_AUDIO_SWITCH(28)
DEFINE_AUDIO_SWITCH(29)
DEFINE_AUDIO_SWITCH(30)
DEFINE_AUDIO_SWITCH(31)
DEFINE_AUDIO_SWITCH(32)
DEFINE_AUDIO_SWITCH(33)
DEFINE_AUDIO_SWITCH(34)
DEFINE_AUDIO_SWITCH(35)
DEFINE_AUDIO_SWITCH(36)
DEFINE_AUDIO_SWITCH(37)
DEFINE_AUDIO_SWITCH(38)
DEFINE_AUDIO_SWITCH(39)
DEFINE_AUDIO_SWITCH(40)

// ========== 字幕切替 (ETC1～ETC40) 40個 ==========
DEFINE_SUBTITLE_SWITCH(1)
DEFINE_SUBTITLE_SWITCH(2)
DEFINE_SUBTITLE_SWITCH(3)
DEFINE_SUBTITLE_SWITCH(4)
DEFINE_SUBTITLE_SWITCH(5)
DEFINE_SUBTITLE_SWITCH(6)
DEFINE_SUBTITLE_SWITCH(7)
DEFINE_SUBTITLE_SWITCH(8)
DEFINE_SUBTITLE_SWITCH(9)
DEFINE_SUBTITLE_SWITCH(10)
DEFINE_SUBTITLE_SWITCH(11)
DEFINE_SUBTITLE_SWITCH(12)
DEFINE_SUBTITLE_SWITCH(13)
DEFINE_SUBTITLE_SWITCH(14)
DEFINE_SUBTITLE_SWITCH(15)
DEFINE_SUBTITLE_SWITCH(16)
DEFINE_SUBTITLE_SWITCH(17)
DEFINE_SUBTITLE_SWITCH(18)
DEFINE_SUBTITLE_SWITCH(19)
DEFINE_SUBTITLE_SWITCH(20)
DEFINE_SUBTITLE_SWITCH(21)
DEFINE_SUBTITLE_SWITCH(22)
DEFINE_SUBTITLE_SWITCH(23)
DEFINE_SUBTITLE_SWITCH(24)
DEFINE_SUBTITLE_SWITCH(25)
DEFINE_SUBTITLE_SWITCH(26)
DEFINE_SUBTITLE_SWITCH(27)
DEFINE_SUBTITLE_SWITCH(28)
DEFINE_SUBTITLE_SWITCH(29)
DEFINE_SUBTITLE_SWITCH(30)
DEFINE_SUBTITLE_SWITCH(31)
DEFINE_SUBTITLE_SWITCH(32)
DEFINE_SUBTITLE_SWITCH(33)
DEFINE_SUBTITLE_SWITCH(34)
DEFINE_SUBTITLE_SWITCH(35)
DEFINE_SUBTITLE_SWITCH(36)
DEFINE_SUBTITLE_SWITCH(37)
DEFINE_SUBTITLE_SWITCH(38)
DEFINE_SUBTITLE_SWITCH(39)
DEFINE_SUBTITLE_SWITCH(40)

void CDouga::OnMenuitem32771()
{
	RECT r,rr;
	// TODO: この位置にコマンド ハンドラ用のコードを追加してください
	r.bottom=rcm.bottom;
	r.top=rcm.top;
	r.right=rcm.right;
	r.left=rcm.left;
//	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	y=r.bottom-r.top; x=r.right-r.left;
	y1_=rcm.bottom-rcm.top; x1=rcm.right-rcm.left;
//	x+=GetSystemMetrics(SM_CXSIZEFRAME)*2;
//	y=y+(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION));
	si=0;
	SetWindowPos(NULL,
				0,0,x, y+(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION)),   SWP_NOMOVE|SWP_NOOWNERZORDER);
	GetClientRect(&rr);
	GetWindowRect(&r);
	MoveWindow(&r);
	if(ev){
		MFVideoNormalizedRect mvnr={0, 0, 1, 1};
		Vdc->SetVideoPosition(&mvnr, &rr);
	}else{
		if(pBasicVideo){
			pBasicVideo->put_DestinationWidth(rr.right);
			pBasicVideo->put_DestinationHeight(rr.bottom);
		}
		if(pVideoWindow){
			pVideoWindow->put_Top(0);
			pVideoWindow->put_Left(0);
			pVideoWindow->put_Height(rr.bottom);
			pVideoWindow->put_Width(rr.right);
		}
	}
	savedata.douga=0;

	savedata.p.top=r.top;
	savedata.p.left=r.left;
	savedata.p.bottom=r.bottom;
	savedata.p.right=r.right;
	si=1;
	SetTimer(1597,30,NULL);
}

void CDouga::OnMenuitem32772() 
{
	RECT r,rr;
	// TODO: この位置にコマンド ハンドラ用のコードを追加してください
	r.bottom=rcm.bottom;
	r.top=rcm.top;
	r.right=rcm.right;
	r.left=rcm.left;
//	AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
	y=r.bottom-r.top; x=r.right-r.left;
//	x+=GetSystemMetrics(SM_CXSIZEFRAME)*2;
//	y=y+(GetSystemMetrics(SM_CYSIZEFRAME)*2+::GetSystemMetrics(SM_CYCAPTION));
	si=0;
	SetWindowPos(NULL,
				0,0,x*2, y*2+(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION)),   SWP_NOMOVE|SWP_NOOWNERZORDER);
	GetClientRect(&rr);
	GetWindowRect(&r);
	MoveWindow(&r);
	if(ev){
		MFVideoNormalizedRect mvnr={0, 0, 1, 1};
		Vdc->SetVideoPosition(&mvnr, &rr);
	}else{
		if(pBasicVideo){
			pBasicVideo->put_DestinationWidth(rr.right);
			pBasicVideo->put_DestinationHeight(rr.bottom);
		}
		if(pVideoWindow){
			pVideoWindow->put_Top(0);
			pVideoWindow->put_Left(0);
			pVideoWindow->put_Height(rr.bottom);
			pVideoWindow->put_Width(rr.right);
		}
	}
	savedata.douga=1;	
	savedata.p.top=r.top;
	savedata.p.left=r.left;
	savedata.p.bottom=r.bottom;
	savedata.p.right=r.right;
	si=1;
	SetTimer(1597,30,NULL);
	
}

void CDouga::OnMenuitem32773() 
{
	RECT r,rr;
	// TODO: この位置にコマンド ハンドラ用のコードを追加してください
	r.bottom=rcm.bottom;
	r.top=rcm.top;
	r.right=rcm.right;
	r.left=rcm.left;
//	AdjustWindowRect(&r, (WS_OVERLAPPEDWINDOW)& ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_SYSMENU, FALSE);
	y=r.bottom-r.top; x=r.right-r.left;
//	x-=GetSystemMetrics(SM_CXSIZEFRAME)*2;
//	y=y-(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION));
	si=0;
	SetWindowPos(NULL,
				0,0,(int)((double)x*1.5), (int)((double)y*1.5)+(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION)),   SWP_NOMOVE|SWP_NOOWNERZORDER);
	GetClientRect(&rr);
	GetWindowRect(&r);
	MoveWindow(&r);
	if(ev){
		MFVideoNormalizedRect mvnr={0, 0, 1, 1};
		Vdc->SetVideoPosition(&mvnr, &rr);
	}else{
		if(pBasicVideo){
			pBasicVideo->put_DestinationWidth(rr.right);
			pBasicVideo->put_DestinationHeight(rr.bottom);
		}
		if(pVideoWindow){
			pVideoWindow->put_Top(0);
			pVideoWindow->put_Left(0);
			pVideoWindow->put_Height(rr.bottom);
			pVideoWindow->put_Width(rr.right);
		}
	}
	savedata.douga=2;	
	savedata.p.top=r.top;
	savedata.p.left=r.left;
	savedata.p.bottom=r.bottom;
	savedata.p.right=r.right;
	si=1;
	SetTimer(1597,30,NULL);
}

void CDouga::OnMenuitem32774() 
{
	if(pBasicVideo==NULL) return;
	RECT r,rr;
	double i;
	// TODO: この位置にコマンド ハンドラ用のコードを追加してください
	r.bottom=rcm.bottom;
	r.top=rcm.top;
	r.right=rcm.right;
	r.left=rcm.left;
	i=(double)(savedata.p.right-savedata.p.left)/(double)rcm.right;
	SetWindowPos(NULL,
				savedata.p.left,savedata.p.top,(int)(i*(double)(rcm.right-rcm.left)),
				(int)(i*(double)(rcm.bottom-rcm.top)+(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION))),  SWP_NOOWNERZORDER);
	GetClientRect(&rr);
	GetWindowRect(&r);
	MoveWindow(&r);
	if(ev){
		MFVideoNormalizedRect mvnr={0, 0, 1, 1};
		Vdc->SetVideoPosition(&mvnr, &rr);
	}else{
		if(pBasicVideo){
			pBasicVideo->put_DestinationWidth(rr.right);
			pBasicVideo->put_DestinationHeight(rr.bottom);
		}
		if(pVideoWindow){
			pVideoWindow->put_Top(0);
			pVideoWindow->put_Left(0);
			pVideoWindow->put_Height(rr.bottom);
			pVideoWindow->put_Width(rr.right);
		}
	}
//	savedata.douga=3;	
}

int dd2=0;
void CDouga::OnPaint() 
{
	CPaintDC dcc(this); // 描画用のデバイス コンテキスト
	
	// TODO: この位置にメッセージ ハンドラ用のコードを追加してください
/*	DWORD dwStyle = GetWindowLong(m_hWnd, GWL_STYLE);
	dwStyle &= ~WS_CAPTION;
	dwStyle |= WS_SIZEBOX;
	SetWindowLong(m_hWnd, GWL_STYLE, dwStyle);
	SetWindowPos(NULL, 0, 0, 0, 0,
			SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
*/	RECT r;
	GetWindowRect(&r);
	MoveWindow(&r);
	if(dd2==1){
		dcc.BitBlt(0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),&dc,0,0,SRCCOPY);
		dd2=0;
	}
	if(ev){
		Vdc->RepaintVideo();
	}

	// 描画用メッセージとして CFrameWnd::OnPaint() を呼び出してはいけません
}
extern int killw;
void CDouga::PostNcDestroy() 
{
	// TODO: この位置に固有の処理を追加するか、または基本クラスを呼び出してください
//	killw=1;
//	delete this;
	CFrameWnd::PostNcDestroy();
}


LRESULT CDouga::OnNcHitTest(CPoint point) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	
//	return CFrameWnd::OnNcHitTest(point);
	if(savedata.fs==0){
		UINT nHit = CFrameWnd::OnNcHitTest(point);
		return (nHit == HTCLIENT)? HTCAPTION : nHit;
	}else{
		int cx=GetSystemMetrics(SM_CXSCREEN);
		int cy=GetSystemMetrics(SM_CYSCREEN);
		SetWindowPos(NULL,0,0,cx,cy,SWP_NOOWNERZORDER);
		return HTBORDER ;
	}
//	return HTCAPTION;
}

void CDouga::OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
//	if(si==1){
//		lpMMI->ptMinTrackSize.y=(int)xx1-20;
//		lpMMI->ptMinTrackSize.x=(int)yy1_-20;
//		lpMMI->ptMaxTrackSize.y=(int)xx1+20;
//		lpMMI->ptMaxTrackSize.x=(int)yy1_+20;
//	}
	CFrameWnd::OnGetMinMaxInfo(lpMMI);
}

int CDouga::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	if(savedata.fs==0)
		nHitTest =(nHitTest == HTCLIENT)? HTCAPTION : nHitTest;
	return CFrameWnd::OnMouseActivate(pDesktopWnd, nHitTest, message);
}

LRESULT CDouga::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam) 
{
	// TODO: この位置に固有の処理を追加するか、または基本クラスを呼び出してください
	
	return CFrameWnd::DefWindowProc(message, wParam, lParam);
}

CPoint m_pointOld;
BOOL m_bMoving=FALSE;

void CDouga::OnLButtonUp(UINT nFlags, CPoint point) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	lu=1;

	if( m_bMoving == TRUE ) {
		// ドラッグ中だった場合
		m_bMoving = FALSE;
		::ReleaseCapture();	
	}
	CFrameWnd::OnLButtonUp(nFlags, point);
}

void CDouga::OnLButtonDown(UINT nFlags, CPoint point) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	if(savedata.fs) return;
   m_bMoving = TRUE;
	SetCapture();
	m_pointOld = point;
	
	CFrameWnd::OnLButtonDown(nFlags, point);
}

void CDouga::OnNcMouseMove(UINT nHitTest, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	if(savedata.fs && (poix!=point.x || poiy!=point.y)){
		int j;
		for(;;){
			j=ShowCursor(TRUE);if(j>=0) break;
		}
		mousecnt=mousecnt1=0;
	}
	if(savedata.fs){
		poix=point.x;
		poiy=point.y;
	}
	CFrameWnd::OnNcMouseMove(nHitTest, point);
}

void CDouga::OnMouseMove(UINT nFlags, CPoint point) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	if( m_bMoving == TRUE ) {
		CRect rect;
		GetWindowRect(&rect);
		rect.left += (point.x - m_pointOld.x);
		rect.right += (point.x - m_pointOld.x);
		rect.top += (point.y - m_pointOld.y);
		rect.bottom += (point.y - m_pointOld.y);
		SetWindowPos(NULL, rect.left, rect.top,
		rect.right - rect.left, rect.bottom - rect.top,
		SWP_SHOWWINDOW|SWP_NOOWNERZORDER);
	}
	if(savedata.fs && (poix!=point.x || poiy!=point.y)){
		int j;
		for(;;){
			j=ShowCursor(TRUE);if(j>=0) break;
		}
		mousecnt=mousecnt1=0;
	}
	if(savedata.fs){
		poix=point.x;
		poiy=point.y;
	}
	
	CFrameWnd::OnMouseMove(nFlags, point);
}

BOOL CDouga::OnEraseBkgnd(CDC* pDC) 
{
	// TODO: この位置にメッセージ ハンドラ用のコードを追加するかまたはデフォルトの処理を呼び出してください
	RECT r;
	GetWindowRect(&r);
	MoveWindow(&r);
	
	return CFrameWnd::OnEraseBkgnd(pDC);
}

extern CDouga *pMainFrame1;
BOOL CDouga::DestroyWindow() 
{
	// TODO: この位置に固有の処理を追加するか、または基本クラスを呼び出してください
	KillTimer(1255);
	stop();	
	bmp.DeleteObject();
	dc.DeleteDC();
	HMODULE hDLL;
	typedef DWORD (WINAPI *PFUNC)(UINT);
	PFUNC pFunc;
	hDLL=::LoadLibrary(_T("Dwmapi"));
	pFunc=(PFUNC)::GetProcAddress(hDLL,"DwmEnableComposition");

	if(pFunc){
		pFunc(DWM_EC_ENABLECOMPOSITION  );
	}
	::FreeLibrary(hDLL);
	KillTimer(155);
	KillTimer(3366);
	KillTimer(1597);
	KillTimer(2987);
	BOOL rr=CFrameWnd::DestroyWindow();
//	delete this;

	return rr;
}

BOOL CDouga::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
//	cs.style&=~WS_CAPTION;
	return CFrameWnd::PreCreateWindow(cs);
}

void CDouga::OnWindowPosChanging(WINDOWPOS* lpwndpos)
{
	CFrameWnd::OnWindowPosChanging(lpwndpos);
}

void CDouga::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CFrameWnd::OnWindowPosChanged(lpwndpos);

	// TODO: ここにメッセージ ハンドラ コードを追加します。
}

int CDouga::SeekPoint(int file_bytes, float percent)
{
// interpolate in TOC to get file seek point in bytes
int a, seekpoint;
float fa, fb, fx;
if( percent < 0.0f )   percent = 0.0f;
if( percent > 100.0f ) percent = 100.0f;
a = (int)percent;
if( a > 99 ) a = 99;
fa = toc[a];
if( a < 99 ) {
    fb = toc[a+1];
}else {
    fb = 256.0f;
}
fx = fa + (fb-fa)*(percent-a);
seekpoint = (int)((1.0f/256.0f)*fx*file_bytes); 
return seekpoint;
}
extern BOOL videoonly;

void CDouga::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	if(pMediaSeeking && (mode==-2 || (mode>0 && videoonly==TRUE))){
		LONGLONG a;
		pMediaSeeking->GetCurrentPosition(&a);
		if(nChar==VK_RIGHT){
			a+=10*10000000;
			pMediaSeeking->SetPositions(&a,AM_SEEKING_AbsolutePositioning,NULL,AM_SEEKING_NoPositioning);
		}
		if(nChar==VK_LEFT){
			a-=10*10000000;
			pMediaSeeking->SetPositions(&a,AM_SEEKING_AbsolutePositioning,NULL,AM_SEEKING_NoPositioning);
		}
		if(nChar==VK_UP){
			og->m_dsval.SetPos(og->m_dsval.GetPos()+5);
		}
		if(nChar==VK_DOWN){
			og->m_dsval.SetPos(og->m_dsval.GetPos()-5);
		}
		if(nChar=='C'){
			On32775();
		}
	}else{
		if(nChar==VK_RIGHT){
			og->rl(1);
		}
		if(nChar==VK_LEFT){
			og->rl(-1);
		}
		if(nChar==VK_UP){
			og->m_dsval.SetPos(og->m_dsval.GetPos()+5);
		}
		if(nChar==VK_DOWN){
			og->m_dsval.SetPos(og->m_dsval.GetPos()-5);
		}
	}
	CFrameWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

BOOL CDouga::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	if(savedata.fs)return CFrameWnd::OnMouseWheel(nFlags, zDelta, pt);
	if(zDelta>0){
		savedata.p.right+=20;
	}else{
		savedata.p.right-=20;
		if(savedata.p.right<savedata.p.left)savedata.p.right=savedata.p.left;
	}
	savedata.douga=3;
	OnMenuitem32774();
	return CFrameWnd::OnMouseWheel(nFlags, zDelta, pt);
}

void CDouga::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	RECT rr;
	if(ev){	CFrameWnd::OnLButtonDblClk(nFlags, point);return;}
	if(savedata.fs){
		savedata.fs=0;
		SetWindowLong(m_hWnd,GWL_EXSTYLE,WS_EX_OVERLAPPEDWINDOW|WS_EX_ACCEPTFILES);
		int i=GetWindowLong(m_hWnd,GWL_STYLE);
		SetWindowLong(m_hWnd,GWL_STYLE,((i | WS_OVERLAPPEDWINDOW)& ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_SYSMENU));
		OnMenuitem32774();
		KillTimer(3366);
		int j;
		for(;;){
			j=ShowCursor(TRUE);if(j>=0) break;
		}
	}else{
		savedata.fs=1;
		int cx=GetSystemMetrics(SM_CXSCREEN);
		int cy=GetSystemMetrics(SM_CYSCREEN);
		int i=GetWindowLong(m_hWnd,GWL_STYLE);
		SetWindowLong(m_hWnd,GWL_EXSTYLE,0);
		SetWindowLong(m_hWnd,GWL_STYLE,i & ~WS_CAPTION & ~WS_BORDER & ~WS_THICKFRAME);
		SetWindowPos(NULL,0,0,cx,cy,SWP_NOOWNERZORDER);
		double ii;int cyy,cxx;
		if(rcm.bottom<rcm.right){
			ii=(double)(cx)/(double)rcm.right;
			cyy=cy/2-(int)(((double)rcm.bottom*ii)/2);
			cxx=cx;
			rr.top=cyy;rr.bottom=cyy+(int)((double)rcm.bottom*ii); rr.left=0;rr.right=cxx;
		}else{
			ii=(double)(cy)/(double)rcm.bottom;
			cxx=cx/2-(int)(((double)rcm.right*ii)/2);
			cyy=cy;
			rr.top=0;rr.bottom=cyy; rr.left=cxx;rr.right=cxx+(int)((double)rcm.right*ii);
		}
		if(savedata.render==0){
			pBasicVideo->put_DestinationWidth(rr.right-rr.left);
			pBasicVideo->put_DestinationHeight(rr.bottom-rr.top);
			pVideoWindow->put_Top(rr.top);
			pVideoWindow->put_Left(rr.left);
			pVideoWindow->put_Height(rr.bottom);
			pVideoWindow->put_Width(rr.right);
		}else{
			GetClientRect(&rr);
			pBasicVideo->put_DestinationWidth(rr.right-rr.left);
			pBasicVideo->put_DestinationHeight(rr.bottom-rr.top);
			pVideoWindow->put_Top(rr.top);
			pVideoWindow->put_Left(rr.left);
			pVideoWindow->put_Height(rr.bottom);
			pVideoWindow->put_Width(rr.right);
		}
		mousecnt=mousecnt1=0;
		SetTimer(3366,500,NULL);
	}
	GetClientRect(&rr);
	InvalidateRect(&rr,TRUE);
	dd2=1;
	CFrameWnd::OnLButtonDblClk(nFlags, point);
}
extern void DoEvent();
void CDouga::OnNcLButtonDblClk(UINT nHitTest, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	//if(ev){CFrameWnd::OnNcLButtonDblClk(nHitTest, point);return;}
	if(savedata.fs){
		savedata.fs=0;
		SetWindowLong(m_hWnd,GWL_EXSTYLE,WS_EX_OVERLAPPEDWINDOW|WS_EX_ACCEPTFILES);
		int i=GetWindowLong(m_hWnd,GWL_STYLE);
		SetWindowLong(m_hWnd,GWL_STYLE,((i | WS_OVERLAPPEDWINDOW)& ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX & ~WS_SYSMENU));
		OnMenuitem32774();
		KillTimer(3366);
		int j;
		for(;;){
			j=ShowCursor(TRUE);if(j>=0) break;
		}
	}else{
		savedata.fs=1;
		int cx=GetSystemMetrics(SM_CXSCREEN);
		int cy=GetSystemMetrics(SM_CYSCREEN);
		int i=GetWindowLong(m_hWnd,GWL_STYLE);
		SetWindowLong(m_hWnd,GWL_EXSTYLE,0);
		SetWindowLong(m_hWnd,GWL_STYLE,i & ~WS_CAPTION & ~WS_BORDER & ~WS_THICKFRAME);
		SetWindowPos(NULL,0,0,cx,cy,SWP_NOOWNERZORDER);
		pcnt=0;
		double ii=(double)(cx)/(double)rcm.right;
		int cyy=cy/2-(int)(((double)rcm.bottom*ii)/2);
		int cxx=cx;
		RECT rr;
		MFVideoNormalizedRect mvnr={0, 0, 1, 1};
		GetClientRect(&rr);
		Vdc->SetVideoPosition(&mvnr, &rr);
		mousecnt=mousecnt1=0;
		SetTimer(3366,500,NULL);
	}
	
	CFrameWnd::OnNcLButtonDblClk(nHitTest, point);
}

void CDouga::On32775()
{
	// TODO: ここにコマンド ハンドラ コードを追加します。
	static BOOL pp=0;
	if(!(videoonly==TRUE || mode==-2))return;
	pause(pp);
	pp++;if(pp>1) pp=0;
}


void CDouga::OnDropFiles(HDROP hDropInfo)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	::PostMessage(og->m_hWnd,WM_DROPFILES,(WPARAM)hDropInfo,NULL);
//	CFrameWnd::OnDropFiles(hDropInfo);
}


void CDouga::OnNcDestroy()
{
	CFrameWnd::OnNcDestroy();

	// TODO: ここにメッセージ ハンドラ コードを追加します。
	killw=1;
}

void CDouga::OnNcRButtonUp(UINT nHitTest, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。

	CFrameWnd::OnNcRButtonUp(nHitTest, point);
}

void CDouga::OnRButtonUp(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。

	CFrameWnd::OnRButtonUp(nFlags, point);
}
