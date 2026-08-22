// Douga.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "XfadePlayback.h"
#include "d3d9.h"
//#include "d3dtypes.h"
#include "ogg.h"
#include "oggDlg.h"
#include "Douga.h"
#include "CMediaPlayerDlg.h"
#include "Graph.h"
#include "dsound.h"
#include "rubberband/RubberBandStretcher.h"
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>
#include <mmreg.h>
#if _MSC_VER >= 1950
#pragma comment(lib, "rubberband-library_2026")
#else
#pragma comment(lib, "rubberband-library")
#endif
#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "dxguid.lib")

#include "MpPlayerAddons.h"
#include "AudioUpscaler.h"
#include "CEqualizer.h"
#include "CCustomPopupMenu.h"
#include "MpRemoteEqEnvLabels.inc"
#include "MpEqPresetLabels.inc"

void equaliserBank(int bank, void* data, int len, BOOL reset,
	int bitsOverride, int chOverride, int rateOverride);
void EqualiserSetFormatVolContext(int mode, BOOL spcApplicable);
void equaliser(void* data, int len, BOOL reset);
extern int g_outBytesPerFrame;

extern IMediaSeeking* pMediaSeeking;
extern CDouga* pMainFrame1;
extern int mode;

// 関連付け起動(タイマー9998)のみ: plays2 表示時に一時 TOPMOST → timer155 で解除
volatile LONG g_dougaAssocTopMost = 0;

BOOL DougaPitchCorrect_Install(IGraphBuilder* graph, HWND hwndOwner);
void DougaPitchCorrect_Shutdown();
BOOL DougaPitchCorrect_IsActive();
void DougaPitchCorrect_EnableCallback();
int DougaPitchCorrect_GetLatencyMs();
void DougaPitchCorrect_SetVolumeDsPos(int dsPos);
void DougaPitchCorrect_SetPlaybackRate(double rate);
void DougaPitchCorrect_SetPaused(BOOL paused);
void DougaPitchCorrect_Poll();
void DougaPitchCorrect_PauseForGraphSeek();
void DougaPitchCorrect_OnSeek(double mediaSec = -1.0);

// PitchCorrect: Grabber→外部 DS（mode=-2）。0=無効（切り分け用）
#ifndef DOUGA_PITCHCORRECT_ENABLE
#define DOUGA_PITCHCORRECT_ENABLE 1
#endif
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
#ifndef AMSTREAMSELECTENABLE_ENABLEONLY
#define AMSTREAMSELECTENABLE_ENABLEONLY 0x2
#endif
// 一般的な字幕メディアタイプ（古い定義 / MPC-HC・LAV 系）
static const GUID MEDIATYPE_Subtitle =
{ 0xe487eb20, 0x6aa4, 0x11d1, { 0xa1, 0x4d, 0x00, 0x20, 0xaf, 0xd7, 0x97, 0x67 } };
static const GUID MEDIATYPE_Subtitle_MPC =
{ 0xe487eb08, 0x6aa4, 0x11cf, { 0x8f, 0x52, 0x00, 0x40, 0x05, 0x48, 0x59, 0x64 } };

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

// qedit Sample Grabber / Null Renderer（この環境の CLSID は登録済み）
static const GUID CLSID_SampleGrabber =
{ 0xC1F400A0, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
static const GUID CLSID_NullRenderer =
{ 0xC1F400A4, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };

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





#define		RELEASE1(x)			{ if(x){ ULONG r=1; int _n=0; for(; _n<32; ++_n){ r=x->Release(); if(r==0) break; } x=NULL; } }
#define		RELEASE(x)			{ if (x != NULL) {x->Release(); x = NULL;} }
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif
extern save savedata;

namespace {

class CDougaHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_DOUGA_HELP };
	explicit CDougaHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CDougaHelpDlg* g_dougaHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CDougaHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CDougaHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	CCC_ApplyWindowIconFromTemplate(this, IDD);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"動画操作ガイド", L"Video Guide", L"Guide vidéo", L"Guida video",
		L"Guía de vídeo", L"동영상 가이드", L"视频操作指南", L"دليل الفيديو",
		L"Руководство видео", L"Video-Anleitung", L"Guia de vídeo", L"Videogids",
		L"Przewodnik wideo", L"Video kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CDougaHelpDlg::OnOK() { DestroyWindow(); }
void CDougaHelpDlg::OnCancel() { DestroyWindow(); }
void CDougaHelpDlg::OnClose() { DestroyWindow(); }

void CDougaHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_dougaHelpDlg == this)
		g_dougaHelpDlg = nullptr;
	delete this;
}

BOOL CDougaHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CDougaHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	const int footerH = hp.footerH;
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 1;
	CBrush frameBrush(RGB(130, 130, 150));

	auto title = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(x, y, t);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(65, 65, 80));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 115));
		dc.TextOut(x, y, t);
	};

	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"動画操作ガイド", L"Video — Guide", L"Guide vidéo", L"Guida video",
		L"Guía de vídeo", L"동영상 가이드", L"视频指南", L"دليل الفيديو",
		L"Руководство видео", L"Video-Guide", L"Guia de vídeo", L"Videogids",
		L"Przewodnik wideo", L"Video kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"下部バーと右クリックメニューで再生・表示・ストリームを操作します。ダブルクリックでも全画面切替。",
		L"Use the bottom bar and right-click menu for playback, display, and streams. Double-click toggles fullscreen.",
		L"Barre bas et clic droit : lecture, affichage, flux. Double-clic = plein écran.",
		L"Barra in basso e destro: riproduzione, visualizzazione, stream. Doppio clic = schermo intero.",
		L"Barra inferior y clic derecho: reproducción, pantalla y streams. Doble clic = pantalla completa.",
		L"하단 바와 우클릭으로 재생·표시·스트림을 조작합니다. 더블클릭으로 전체화면.",
		L"用底部栏和右键菜单操作播放、显示与流。双击也可切换全屏。",
		L"الشريط السفلي والنقر الأيمن للتشغيل والعرض والمسارات. النقر المزدوج يبدّل ملء الشاشة.",
		L"Нижняя панель и ПКМ: воспроизведение, вид и потоки. Двойной щелчок — полный экран.",
		L"Untere Leiste und Rechtsklick: Wiedergabe, Anzeige, Streams. Doppelklick = Vollbild.",
		L"Barra inferior e botão direito: reprodução, ecrã e streams. Duplo clique = ecrã inteiro.",
		L"Onderbalk en rechtsklik: afspelen, weergave, streams. Dubbelklik = volledig scherm.",
		L"Dolny pasek i PPM: odtwarzanie, widok i strumienie. Dwuklik = pełny ekran.",
		L"Alt çubuk ve sağ tık: oynatma, görünüm, akışlar. Çift tık = tam ekran."));
	y += lh + 4;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KGENERIC);


	title(L, y, LL14(L"再生 / シーク", L"Play / Seek", L"Lecture / Position", L"Play / Seek",
		L"Reproducir / Buscar", L"재생 / 탐색", L"播放 / 定位", L"تشغيل / تقديم",
		L"Воспроизведение / Перемотка", L"Wiedergabe / Suche", L"Reproduzir / Busca", L"Afspelen / Zoeken",
		L"Odtwarzanie / Przewijanie", L"Oynat / Sar"));
	y += titleLh;
	body(L, y, LL14(L"・再生／一時停止／停止 …… バーまたは右クリック。前へ／次へで曲切替", L"· Play / Pause / Stop …… bar or right-click. Prev / Next changes track", L"· Lect. / Pause / Stop …… barre ou clic droit. Préc./Suiv. change de piste", L"· Play / Pausa / Stop …… barra o destro. Prec./Succ. cambia brano",
		L"· Play / Pausa / Stop …… barra o clic der. Ant./Sig. cambia pista", L"· 재생/일시정지/중지 …… 바 또는 우클릭. 이전/다음으로 곡 전환", L"· 播放/暂停/停止 …… 栏或右键。上一首/下一首切换曲目", L"· تشغيل/إيقاف/إيقاف تام …… الشريط أو يمين. السابق/التالي يغيّر المقطع",
		L"· Играть / Пауза / Стоп …… панель или ПКМ. Пред./След. — трек", L"· Play / Pause / Stop …… Leiste oder Rechtsklick. Zurück/Weiter wechselt Titel", L"· Play / Pausa / Parar …… barra ou direito. Ant./Prox. muda faixa", L"· Play / Pauze / Stop …… balk of rechtsklik. Vorige/Volgende wisselt track",
		L"· Odtwórz / Pauza / Stop …… pasek lub PPM. Poprz./Nast. zmienia utwór", L"· Çal / Duraklat / Dur …… çubuk veya sağ tık. Önceki/Sonraki parça")); y += lh;
	body(L, y, LL14(L"・シーク …… 上段スライダー。←→ キーでも前後。戻す／進めるで少し移動", L"· Seek …… top slider. ←→ keys also step. Rew / FF nudge a bit", L"· Position …… curseur haut. ←→ aussi. Recul / Avance = petit saut", L"· Seek …… cursore in alto. ←→ pure. Indietro / Avanti = piccolo salto",
		L"· Buscar …… control superior. ←→ también. Retr./Avanz. = un poco", L"· 탐색 …… 상단 슬라이더. ←→ 키도 가능. 되감기/빨리감기로 조금 이동", L"· 定位 …… 上方滑块。←→ 键也可。快退/快进微调", L"· تقديم …… شريط علوي. ←→ أيضاً. ترجيع/تقديم = خطوة صغيرة",
		L"· Перемотка …… верхний ползунок. ←→ тоже. Назад/Вперёд — чуть", L"· Suche …… oberer Schieber. ←→ ebenfalls. Zurück/Vor = etwas", L"· Busca …… controlo superior. ←→ também. Voltar/Avançar = um pouco", L"· Zoeken …… bovenste schuif. ←→ ook. Terug/Vooruit = beetje",
		L"· Przewijanie …… górny suwak. ←→ też. Wstecz/Naprzód = trochę", L"· Sar …… üst kaydırıcı. ←→ da. Geri/İleri = biraz")); y += lh + 4;

	title(L, y, LL14(L"表示 / 全画面", L"Display / Fullscreen", L"Affichage / Plein écran", L"Visualizzazione / Schermo intero",
		L"Pantalla / Completa", L"표시 / 전체화면", L"显示 / 全屏", L"عرض / ملء الشاشة",
		L"Вид / Полный экран", L"Anzeige / Vollbild", L"Ecrã / Completo", L"Weergave / Volledig",
		L"Widok / Pełny ekran", L"Görünüm / Tam ekran"));
	y += titleLh;
	body(L, y, LL14(L"・全画面 …… バー／メニュー／ダブルクリック。ウィンドウ時は 1x・1.5x・2x（タスクバーを除く画面内に自動収める）", L"· Fullscreen …… bar / menu / double-click. In window: 1x · 1.5x · 2x (auto-fit inside work area, excluding taskbar)", L"· Plein écran …… barre / menu / double-clic. Fenêtre: 1x · 1,5x · 2x (ajusté à la zone utile, hors barre des tâches)", L"· Schermo intero …… barra / menu / doppio clic. Finestra: 1x · 1.5x · 2x (adattato all'area utile, esclusa la barra)",
		L"· Completa …… barra / menú / doble clic. Ventana: 1x · 1.5x · 2x (ajuste al área de trabajo, sin barra de tareas)", L"· 전체화면 …… 바/메뉴/더블클릭. 창 모드: 1x·1.5x·2x (작업 표시줄 제외 화면 안에 자동 맞춤)", L"· 全屏 …… 栏/菜单/双击。窗口模式: 1x·1.5x·2x（自动收进不含任务栏的工作区）", L"· ملء الشاشة …… شريط/قائمة/نقر مزدوج. النافذة: 1x·1.5x·2x (ملاءمة تلقائية داخل منطقة العمل دون شريط المهام)",
		L"· Полный экран …… панель / меню / двойной щелчок. Окно: 1x · 1.5x · 2x (автоподгон в рабочую область без панели задач)", L"· Vollbild …… Leiste / Menü / Doppelklick. Fenster: 1x · 1,5x · 2x (automatisch in Arbeitsbereich ohne Taskleiste)", L"· Ecrã inteiro …… barra / menu / duplo clique. Janela: 1x · 1.5x · 2x (ajuste automático à área de trabalho, sem barra de tarefas)", L"· Volledig …… balk / menu / dubbelklik. Venster: 1x · 1.5x · 2x (automatisch in werkgebied zonder taakbalk)",
		L"· Pełny ekran …… pasek / menu / dwuklik. Okno: 1x · 1.5x · 2x (auto dopasowanie do obszaru roboczego bez paska zadań)", L"· Tam ekran …… çubuk / menü / çift tık. Pencere: 1x · 1.5x · 2x (görev çubuğu hariç çalışma alanına otomatik sığdırma)")); y += lh;
	body(L, y, LL14(L"・アスペクト比を維持 …… 右クリック。黒帯で比率を保つ／伸ばして埋める", L"· Keep aspect …… right-click. Letterbox vs stretch-to-fill", L"· Proportions …… clic droit. Bandes noires ou étirement", L"· Proporzioni …… destro. Bande nere o stiramento",
		L"· Proporción …… clic der. Bandas negras o estirar", L"· 화면비 유지 …… 우클릭. 레터박스 / 늘려 채우기", L"· 保持宽高比 …… 右键。黑边或拉伸填满", L"· نسبة العرض …… يمين. أشرطة سوداء أو تمديد",
		L"· Пропорции …… ПКМ. Поля или растяжение", L"· Seitenverhältnis …… Rechtsklick. Balken oder strecken", L"· Proporção …… direito. Barras pretas ou esticar", L"· Beeldverhouding …… rechtsklik. Zwarte balken of rekken",
		L"· Proporcje …… PPM. Pasma lub rozciągnięcie", L"· En-boy …… sağ tık. Siyah şerit veya esnetme")); y += lh;
	body(L, y, LL14(L"・常に手前に表示 …… 右クリック。他ウィンドウの上に固定", L"· Always on top …… right-click. Keep above other windows", L"· Toujours devant …… clic droit. Au-dessus des autres", L"· Sempre in primo piano …… destro. Sopra le altre finestre",
		L"· Siempre visible …… clic der. Sobre otras ventanas", L"· 항상 위에 …… 우클릭. 다른 창 위에 고정", L"· 总在最前 …… 右键。固定在其他窗口之上", L"· دائماً في المقدمة …… يمين. فوق النوافذ الأخرى",
		L"· Поверх всех …… ПКМ. Над другими окнами", L"· Immer im Vordergrund …… Rechtsklick. Über anderen Fenstern", L"· Sempre visível …… direito. Acima das outras janelas", L"· Altijd op voorgrond …… rechtsklik. Boven andere vensters",
		L"· Zawsze na wierzchu …… PPM. Nad innymi oknami", L"· Her zaman üstte …… sağ tık. Diğer pencerelerin üstünde")); y += lh;
	body(L, y, LL14(L"・動画画面を閉じる …… タイトルバーの×、または右クリック。動画専用なら再生も停止", L"· Close video window …… title-bar × or right-click. Also stops playback for video-only", L"· Fermer l'ecran …… × barre titre ou clic droit. Arrete aussi en lecture video seule", L"· Chiudi finestra …… × barra titolo o destro. Ferma anche in solo video",
		L"· Cerrar pantalla …… × de titulo o clic der. Tambien detiene en solo video", L"· 동영상 화면 닫기 …… 제목 표시줄 × 또는 우클릭. 동영상 전용이면 재생도 중지", L"· 关闭视频窗口 …… 标题栏×或右键。仅视频时也会停止播放", L"· إغلاق الشاشة …… × شريط العنوان أو يمين. يوقف أيضاً عند الفيديو فقط",
		L"· Закрыть окно …… × заголовка или ПКМ. Для только-видео также стоп", L"· Videofenster …… × Titelleiste oder Rechtsklick. Bei Nur-Video auch Stop", L"· Fechar janela …… × da barra ou direito. Em so-video tambem para", L"· Videovenster …… × titelbalk of rechtsklik. Bij alleen-video ook stop",
		L"· Zamknij okno …… × paska lub PPM. Przy samym wideo tez stop", L"· Video penceresi …… baslik × veya sag tik. Sadece videoda oynatmayi da durdurur")); y += lh;
	body(L, y, LL14(L"・再生速度 …… バー右のスライダー／右クリック。0.1x〜4.0x（テンポ連動）", L"· Playback speed …… bar slider / right-click. 0.1x–4.0x (tempo sync)", L"· Vitesse …… curseur / clic droit. 0,1x–4,0x (tempo)", L"· Velocità …… cursore / destro. 0.1x–4.0x (tempo)",
		L"· Velocidad …… control / clic der. 0.1x–4.0x (tempo)", L"· 재생 속도 …… 바 슬라이더/우클릭. 0.1x–4.0x (템포 연동)", L"· 播放速度 …… 栏滑块/右键。0.1x–4.0x（速度联动）", L"· السرعة …… شريط / يمين. 0.1x–4.0x",
		L"· Скорость …… слайдер / ПКМ. 0.1x–4.0x (темп)", L"· Geschwindigkeit …… Leiste / RMB. 0,1x–4,0x", L"· Velocidade …… barra / direito. 0,1x–4,0x", L"· Snelheid …… balk / rechtsklik. 0,1x–4,0x",
		L"· Predkosc …… pasek / PPM. 0,1x–4,0x", L"· Hiz …… cubuk / sag tik. 0.1x–4.0x")); y += lh;

	title(L, y, LL14(L"音量 / 消音 / フェード", L"Volume / Mute / Fade", L"Volume / Muet / Fondu", L"Volume / Mute / Fade",
		L"Volumen / Silencio / Fade", L"음량 / 음소거 / 페이드", L"音量 / 静音 / 淡出", L"الصوت / كتم / تلاشي",
		L"Громкость / Без звука / Затухание", L"Lautstärke / Stumm / Fade", L"Volume / Mudo / Fade", L"Volume / Dempen / Fade",
		L"Głośność / Wycisz / Fade", L"Ses / Sessiz / Fade"));
	y += titleLh;
	body(L, y, LL14(L"・音量 …… 右端スライダー／↑↓ キー。消音で一時的に 0。フェードで徐々に停止", L"· Volume …… right slider / ↑↓. Mute zeros temporarily. Fade stops gradually", L"· Volume …… curseur droit / ↑↓. Muet = 0. Fondu = arrêt progressif", L"· Volume …… cursore destro / ↑↓. Mute = 0. Fade = stop graduale",
		L"· Volumen …… control der. / ↑↓. Silencio = 0. Fade = parada gradual", L"· 음량 …… 우측 슬라이더/↑↓. 음소거는 일시 0. 페이드는 서서히 정지", L"· 音量 …… 右侧滑块/↑↓。静音暂置 0。淡出逐渐停止", L"· الصوت …… شريط يمين / ↑↓. الكتم = 0. التلاشي يوقف تدريجياً",
		L"· Громкость …… правый ползунок / ↑↓. Без звука = 0. Затухание — плавный стоп", L"· Lautstärke …… rechter Schieber / ↑↓. Stumm = 0. Fade = sanftes Stoppen", L"· Volume …… controlo direito / ↑↓. Mudo = 0. Fade = paragem gradual", L"· Volume …… rechter schuif / ↑↓. Dempen = 0. Fade = geleidelijk stoppen",
		L"· Głośność …… prawy suwak / ↑↓. Wycisz = 0. Fade = stopniowy stop", L"· Ses …… sağ kaydırıcı / ↑↓. Sessiz = 0. Fade = yavaşça durur")); y += lh + 4;

	title(L, y, LL14(L"ストリーム / フィルタ", L"Streams / Filters", L"Flux / Filtres", L"Stream / Filtri",
		L"Streams / Filtros", L"스트림 / 필터", L"流 / 滤镜", L"المسارات / المرشحات",
		L"Потоки / Фильтры", L"Streams / Filter", L"Streams / Filtros", L"Streams / Filters",
		L"Strumienie / Filtry", L"Akışlar / Filtreler"));
	y += titleLh;
	body(L, y, LL14(L"・右クリック …… 映像／音声／字幕ストリームを選択（ある場合）", L"· Right-click …… choose video / audio / subtitle streams (when present)", L"· Clic droit …… choisir flux vidéo / audio / sous-titres (si présents)", L"· Destro …… scegli stream video / audio / sottotitoli (se presenti)",
		L"· Clic der. …… elegir streams de vídeo / audio / subtítulos (si hay)", L"· 우클릭 …… 영상/음성/자막 스트림 선택(있을 때)", L"· 右键 …… 选择视频/音频/字幕流（若有）", L"· يمين …… اختيار مسارات فيديو/صوت/ترجمة (إن وُجدت)",
		L"· ПКМ …… выбрать видео / аудио / субтитры (если есть)", L"· Rechtsklick …… Video-/Audio-/Untertitelstreams (falls vorhanden)", L"· Direito …… escolher streams de vídeo / áudio / legendas (se houver)", L"· Rechtsklik …… video-/audio-/ondertitelstreams (indien aanwezig)",
		L"· PPM …… wybierz strumienie wideo / audio / napisów (jeśli są)", L"· Sağ tık …… video / ses / altyazı akışları (varsa)")); y += lh;
	body(L, y, LL14(L"・フィルタ …… 再生設定のデコーダ／レンダラ等。グラフ構築時に適用", L"· Filters …… decoders / renderers from playback settings; applied when building the graph", L"· Filtres …… décodeurs / rendus des réglages; appliqués à la construction du graphe", L"· Filtri …… decoder / renderer dalle impostazioni; applicati nella costruzione del grafo",
		L"· Filtros …… decodificadores / renderizadores de ajustes; al construir el grafo", L"· 필터 …… 재생 설정의 디코더/렌더러 등. 그래프 구축 시 적용", L"· 滤镜 …… 播放设置中的解码器/渲染器等，建图时应用", L"· المرشحات …… وحدات فك/عرض من الإعدادات؛ تُطبَّق عند بناء الرسم",
		L"· Фильтры …… декодеры / рендереры из настроек; при построении графа", L"· Filter …… Decoder / Renderer aus Einstellungen; beim Graphaufbau", L"· Filtros …… descodificadores / renderers das definições; ao construir o grafo", L"· Filters …… decoders / renderers uit instellingen; bij opbouw van de graph",
		L"· Filtry …… dekodery / renderery z ustawień; przy budowie grafu", L"· Filtreler …… ayarlardaki kod çözücü / renderer; grafik kurulurken")); y += lh + 4;

	const int gx = L, gy = y, gw = min(360, rc.Width() - L * 2), gh = lh * 2 + 12;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	dc.FillSolidRect(gx + 4, gy + 6, 44, gh - 12, RGB(255, 180, 120));
	dc.FillSolidRect(gx + 56, gy + 6, 44, gh - 12, RGB(130, 205, 140));
	dc.FillSolidRect(gx + 108, gy + 6, 48, gh - 12, RGB(160, 195, 240));
	dc.FillSolidRect(gx + 164, gy + 6, 50, gh - 12, RGB(240, 210, 160));
	dc.SetTextColor(RGB(40, 40, 55));
	dc.TextOut(gx + 10, gy + 8, L"Play");
	dc.TextOut(gx + 64, gy + 8, L"FS");
	dc.TextOut(gx + 116, gy + 8, L"Mute");
	dc.TextOut(gx + 172, gy + 8, L"RMB");
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

/////////////////////////////////////////////////////////////////////////////
// CDouga

IMPLEMENT_DYNCREATE(CDouga, CFrameWnd)

CDouga::CDouga()
	: m_applyBusy(0)
	, m_inSizeMove(0)
	, m_closingByMain(0)
{
}

CDouga::~CDouga()
{
//	delete this;

}

/////////////////////////////////////////////////////////////////////////////
// CDougaVideoSite — マウスを親へ転送(座標は親クライアントへ変換)

LRESULT CDougaVideoSite::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_CONTEXTMENU:
	{
		CWnd* p = GetParent();
		if (p && p->GetSafeHwnd())
			return p->SendMessage(message, wParam, lParam);
		break;
	}
	case WM_MOUSEWHEEL:
	case WM_MOUSEHWHEEL:
	{
		CWnd* p = GetParent();
		if (p && p->GetSafeHwnd())
			return p->SendMessage(message, wParam, lParam);
		break;
	}
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_LBUTTONDBLCLK:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
	{
		CWnd* p = GetParent();
		if (p && p->GetSafeHwnd()) {
			CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
			MapWindowPoints(p, &pt, 1);
			return p->SendMessage(message, wParam, MAKELPARAM(pt.x, pt.y));
		}
		break;
	}
	default:
		break;
	}
	return CWnd::WindowProc(message, wParam, lParam);
}

/////////////////////////////////////////////////////////////////////////////
// CDougaBarHost

static int DougaDpiScale(HWND hwnd, int px)
{
	int dpi = 96;
	if (hwnd) {
		HDC hdc = ::GetDC(hwnd);
		if (hdc) {
			dpi = ::GetDeviceCaps(hdc, LOGPIXELSX);
			::ReleaseDC(hwnd, hdc);
		}
	}
	if (dpi < 96) dpi = 96;
	return MulDiv(px, dpi, 96);
}

CDougaBarHost::CDougaBarHost()
	: m_owner(NULL)
	, m_dpi(1.f)
	, m_barH(56)
	, m_ready(0)
	, m_short(0)
	, m_laidShort(-1)
	, m_seekDrag(0)
	, m_rateDrag(0)
	, m_muted(0)
	, m_mutePos(0)
{
}

BEGIN_MESSAGE_MAP(CDougaBarHost, CWnd)
	ON_WM_ERASEBKGND()
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_DOUGA_PREV, OnBnPrev)
	ON_BN_CLICKED(IDC_DOUGA_REW, OnBnRew)
	ON_BN_CLICKED(IDC_DOUGA_PLAY, OnBnPlay)
	ON_BN_CLICKED(IDC_DOUGA_PAUSE, OnBnPause)
	ON_BN_CLICKED(IDC_DOUGA_STOP, OnBnStop)
	ON_BN_CLICKED(IDC_DOUGA_FF, OnBnFf)
	ON_BN_CLICKED(IDC_DOUGA_NEXT, OnBnNext)
	ON_BN_CLICKED(IDC_DOUGA_FADE, OnBnFade)
	ON_BN_CLICKED(IDC_DOUGA_MUTE, OnBnMute)
	ON_BN_CLICKED(IDC_DOUGA_FS, OnBnFs)
	ON_BN_CLICKED(IDC_DOUGA_SZ1, OnBnSz1)
	ON_BN_CLICKED(IDC_DOUGA_SZ15, OnBnSz15)
	ON_BN_CLICKED(IDC_DOUGA_SZ2, OnBnSz2)
	ON_BN_CLICKED(IDC_DOUGA_HELP, OnBnHelp)
	ON_STN_CLICKED(IDC_DOUGA_RATE_L, OnStnRateReset)
	ON_STN_CLICKED(IDC_DOUGA_RATEVAL, OnStnRateReset)
END_MESSAGE_MAP()

// ファイル後方で定義されるグローバルへの前方参照
extern COggDlg* og;
extern BOOL ev;
extern IMFVideoDisplayControl* Vdc;
extern IVideoWindow* pVideoWindow;
extern IBasicVideo* pBasicVideo;
extern RECT rcm; // 再生中動画の元サイズ(アスペクト比維持で使用)
extern IGraphBuilder* pGraphBuilder;
extern IAMStreamSelect* iam;
extern CString streamname[40];
extern int audionum;
extern long width, height;
extern int tempo;
extern void MpTaskbarReplay();
extern void MpTaskbarNextTrack();
extern void MpTaskbarPrevTrack();

BOOL CDougaBarHost::OnEraseBkgnd(CDC* pDC)
{
	if (!pDC) return TRUE;
	CRect rc;
	GetClientRect(&rc);
	pDC->FillSolidRect(&rc, RGB(32, 32, 36));
	return TRUE;
}

BOOL CDougaBarHost::PtInBarClient(CPoint ptClientOfDouga) const
{
	if (!m_ready || !GetSafeHwnd()) return FALSE;
	CRect r;
	GetWindowRect(&r);
	if (m_owner && m_owner->GetSafeHwnd())
		m_owner->ScreenToClient(&r);
	return r.PtInRect(ptClientOfDouga);
}

void CDougaBarHost::ShowBar(BOOL show)
{
	if (!GetSafeHwnd()) return;
	ShowWindow(show ? SW_SHOW : SW_HIDE);
}

BOOL CDougaBarHost::PreTranslateMessage(MSG* pMsg)
{
	if (m_tip.GetSafeHwnd())
		m_tip.RelayEvent(pMsg);
	if (pMsg && pMsg->message == WM_KEYDOWN
		&& (pMsg->wParam == VK_OEM_4 || pMsg->wParam == VK_OEM_6
			|| pMsg->wParam == '[' || pMsg->wParam == ']')) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->HandleAbBracketKey(pMsg->wParam)) {
			SyncSeekVol();
			return TRUE;
		}
	}
	// 速度つまみ: HSCROLL より先にドラッグ中フラグを立て、タイマー同期の上書きを防ぐ
	if (pMsg && m_rate.GetSafeHwnd() && pMsg->hwnd == m_rate.m_hWnd) {
		if (pMsg->message == WM_LBUTTONDOWN || pMsg->message == WM_LBUTTONDBLCLK)
			m_rateDrag = 1;
		else if (pMsg->message == WM_LBUTTONUP)
			m_rateDrag = 0;
	}
	return CWnd::PreTranslateMessage(pMsg);
}

static void DougaAddTip(CToolTipCtrl& tip, CWnd& w, LPCWSTR text)
{
	if (!tip.GetSafeHwnd() || !w.GetSafeHwnd() || !text) return;
	tip.AddTool(&w, text);
}

BOOL CDougaBarHost::CreateBar(CDouga* owner)
{
	if (!owner || !owner->GetSafeHwnd()) return FALSE;
	m_owner = owner;
	m_dpi = (float)DougaDpiScale(owner->m_hWnd, 100) / 100.f;
	m_barH = DougaDpiScale(owner->m_hWnd, 56);

	CString cls = AfxRegisterWndClass(CS_DBLCLKS,
		::LoadCursor(NULL, IDC_ARROW),
		(HBRUSH)::GetStockObject(NULL_BRUSH),
		NULL);
	CRect rc(0, 0, 10, m_barH);
	if (!Create(cls, _T(""), WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		rc, owner, IDC_DOUGA_BARHOST))
		return FALSE;

	CRect z(0, 0, 1, 1);
	const DWORD btnStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
	const DWORD stStyle = WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS;
	const DWORD stClickStyle = stStyle | SS_NOTIFY;
	const DWORD slStyle = WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_BOTH;

	m_seek.Create(slStyle | TBS_ENABLESELRANGE, z, this, IDC_DOUGA_SEEK);
	m_time.Create(_T(""), stStyle, z, this, IDC_DOUGA_TIME);
	m_prev.Create(_T(""), btnStyle, z, this, IDC_DOUGA_PREV);
	m_rew.Create(_T(""), btnStyle, z, this, IDC_DOUGA_REW);
	m_play.Create(_T(""), btnStyle, z, this, IDC_DOUGA_PLAY);
	m_pause.Create(_T(""), btnStyle, z, this, IDC_DOUGA_PAUSE);
	m_stop.Create(_T(""), btnStyle, z, this, IDC_DOUGA_STOP);
	m_ff.Create(_T(""), btnStyle, z, this, IDC_DOUGA_FF);
	m_next.Create(_T(""), btnStyle, z, this, IDC_DOUGA_NEXT);
	m_fade.Create(_T(""), btnStyle, z, this, IDC_DOUGA_FADE);
	m_mute.Create(_T(""), btnStyle, z, this, IDC_DOUGA_MUTE);
	m_fs.Create(_T(""), btnStyle, z, this, IDC_DOUGA_FS);
	m_sz1.Create(_T(""), btnStyle, z, this, IDC_DOUGA_SZ1);
	m_sz15.Create(_T(""), btnStyle, z, this, IDC_DOUGA_SZ15);
	m_sz2.Create(_T(""), btnStyle, z, this, IDC_DOUGA_SZ2);
	m_help.Create(_T("?"), btnStyle, z, this, IDC_DOUGA_HELP);
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_volL.Create(_T(""), stStyle, z, this, IDC_DOUGA_VOL_L);
	m_vol.Create(slStyle, z, this, IDC_DOUGA_VOL);
	m_volVal.Create(_T(""), stStyle, z, this, IDC_DOUGA_VOLVAL);
	m_rateL.Create(_T(""), stClickStyle, z, this, IDC_DOUGA_RATE_L);
	m_rate.Create(slStyle, z, this, IDC_DOUGA_RATE);
	m_rateVal.Create(_T(""), stClickStyle, z, this, IDC_DOUGA_RATEVAL);
	m_info.Create(_T(""), stStyle | SS_ENDELLIPSIS, z, this, IDC_DOUGA_INFO);

	m_vol.SetRange(-498, 1);
	m_rate.SetRange(10, 400);
	m_rate.SetPos(100);
	m_seek.ModifyStyle(0, TBS_ENABLESELRANGE);
	m_time.SetNoParentInvalidate(TRUE);
	m_volVal.SetNoParentInvalidate(TRUE);
	m_rateVal.SetNoParentInvalidate(TRUE);
	m_info.SetNoParentInvalidate(TRUE);
	// バーは親がピンク以外。無効化時に白抜けしないようラベルは不透明ピンク固定
	m_rateL.SetSolidFill(TRUE, COLOR_DIALOG_BG);
	m_volL.SetSolidFill(TRUE, COLOR_DIALOG_BG);
	m_rateVal.SetSolidFill(TRUE, COLOR_DIALOG_BG);
	m_volVal.SetSolidFill(TRUE, COLOR_DIALOG_BG);
	m_time.SetSolidFill(TRUE, COLOR_DIALOG_BG);
	m_info.SetSolidFill(TRUE, COLOR_DIALOG_BG);

	m_prev.SetWindowText(LL14(L"前へ", L"Prev", L"Prec", L"Prec", L"Ant", L"이전", L"上一首", L"السابق", L"Пред", L"Zurück", L"Ant", L"Vorige", L"Poprz", L"Onceki"));
	m_rew.SetWindowText(LL14(L"戻す", L"Rew", L"Recul", L"Ind", L"Retr", L"되감기", L"快退", L"ترجيع", L"Назад", L"Zurück", L"Voltar", L"Terug", L"Wstecz", L"Geri"));
	m_play.SetWindowText(LL14(L"再生", L"Play", L"Lect", L"Play", L"Play", L"재생", L"播放", L"تشغيل", L"Играть", L"Play", L"Play", L"Play", L"Odtw", L"Cal"));
	m_pause.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف", L"Пауза", L"Pause", L"Pausa", L"Pauze", L"Pauza", L"Duraklat"));
	m_stop.SetWindowText(LL14(L"停止", L"Stop", L"Stop", L"Stop", L"Stop", L"중지", L"停止", L"إيقاف", L"Стоп", L"Stop", L"Parar", L"Stop", L"Stop", L"Durdur"));
	m_ff.SetWindowText(LL14(L"進める", L"FF", L"Avance", L"Avanti", L"Avanz", L"빨리감기", L"快进", L"تقديم", L"Вперёд", L"Vor", L"Avançar", L"Vooruit", L"Naprz", L"Ileri"));
	m_next.SetWindowText(LL14(L"次へ", L"Next", L"Suiv", L"Succ", L"Sig", L"다음", L"下一首", L"التالي", L"След", L"Weiter", L"Prox", L"Volgende", L"Nast", L"Sonraki"));
	m_fade.SetWindowText(LL14(L"フェード", L"Fade", L"Fondu", L"Fade", L"Fade", L"페이드", L"淡出", L"تلاشي", L"Затух", L"Fade", L"Fade", L"Fade", L"Fade", L"Fade"));
	m_mute.SetWindowText(LL14(L"消音", L"Mute", L"Muet", L"Mute", L"Silenc", L"음소거", L"静音", L"كتم", L"Без зв.", L"Stumm", L"Mudo", L"Dempen", L"Wycisz", L"Sessiz"));
	m_fs.SetWindowText(LL14(L"全画面", L"Full", L"Plein", L"Intero", L"Completa", L"전체", L"全屏", L"ملء", L"Полн.", L"Voll", L"Cheia", L"Volledig", L"Pełny", L"Tam"));
	m_sz1.SetWindowText(L"1x");
	m_sz15.SetWindowText(L"1.5x");
	m_sz2.SetWindowText(L"2x");
	m_volL.SetWindowText(LL14(L"音量", L"Vol", L"Vol", L"Vol", L"Vol", L"음량", L"音量", L"صوت", L"Громк.", L"Laut", L"Vol", L"Vol", L"Głośn.", L"Ses"));
	m_rateL.SetWindowText(LL14(L"速度", L"Spd", L"Vit", L"Vel", L"Vel", L"속도", L"速度", L"سرعة", L"Скор.", L"Geschw", L"Vel", L"Snel", L"Pręd", L"Hiz"));
	m_time.SetWindowText(L"00:00 / 00:00");
	m_volVal.SetWindowText(L"0%");
	m_rateVal.SetWindowText(L"1.00x");

	if (m_tip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		m_tip.Activate(TRUE);
		DougaAddTip(m_tip, m_prev, LL14(L"前の曲へ", L"Previous track", L"Piste précédente", L"Brano precedente", L"Pista anterior", L"이전 곡", L"上一曲", L"المقطع السابق", L"Предыдущий трек", L"Vorheriger Titel", L"Faixa anterior", L"Vorige track", L"Poprzedni utwór", L"Önceki parça"));
		DougaAddTip(m_tip, m_rew, LL14(L"少し戻す", L"Rewind a bit", L"Reculer un peu", L"Indietro un po'", L"Retroceder un poco", L"조금 되감기", L"快退一点", L"ترجيع قليلاً", L"Немного назад", L"Etwas zurück", L"Voltar um pouco", L"Iets terug", L"Cofnij trochę", L"Biraz geri"));
		DougaAddTip(m_tip, m_play, LL14(L"再生/最初から", L"Play / restart", L"Lecture / recommencer", L"Riproduci / riavvia", L"Reproducir / reiniciar", L"재생/처음부터", L"播放/重头", L"تشغيل/إعادة", L"Играть/сначала", L"Abspielen/neu", L"Reproduzir/reiniciar", L"Afspelen/herstart", L"Odtwórz/od nowa", L"Çal/baştan"));
		DougaAddTip(m_tip, m_pause, LL14(L"一時停止/再開", L"Pause / resume", L"Pause / reprendre", L"Pausa / riprendi", L"Pausa / reanudar", L"일시정지/재개", L"暂停/继续", L"إيقاف/استئناف", L"Пауза/продолжить", L"Pause/fortsetzen", L"Pausar/retomar", L"Pauzeren/hervatten", L"Pauza/wznów", L"Duraklat/devam"));
		DougaAddTip(m_tip, m_stop, LL14(L"停止", L"Stop", L"Arrêt", L"Stop", L"Detener", L"중지", L"停止", L"إيقاف", L"Стоп", L"Stop", L"Parar", L"Stoppen", L"Stop", L"Durdur"));
		DougaAddTip(m_tip, m_ff, LL14(L"少し進める", L"Fast-forward a bit", L"Avancer un peu", L"Avanti un po'", L"Avanzar un poco", L"조금 빨리감기", L"快进一点", L"تقديم قليلاً", L"Немного вперёд", L"Etwas vor", L"Avançar um pouco", L"Iets vooruit", L"Przewiń trochę", L"Biraz ileri"));
		DougaAddTip(m_tip, m_next, LL14(L"次の曲へ", L"Next track", L"Piste suivante", L"Brano successivo", L"Pista siguiente", L"다음 곡", L"下一曲", L"المقطع التالي", L"Следующий трек", L"Nächster Titel", L"Próxima faixa", L"Volgende track", L"Następny utwór", L"Sonraki parça"));
		DougaAddTip(m_tip, m_fade, LL14(L"フェードアウトして停止", L"Fade out and stop", L"Fondu puis arrêt", L"Dissolvenza e stop", L"Desvanecer y detener", L"페이드 아웃 후 정지", L"淡出并停止", L"تلاشي ثم إيقاف", L"Затухание и стоп", L"Ausblenden und stoppen", L"Desvanecer e parar", L"Uitfaden en stoppen", L"Wycisz i zatrzymaj", L"Soluklaştırıp durdur"));
		DougaAddTip(m_tip, m_mute, LL14(L"消音の切替", L"Toggle mute", L"Couper/rétablir le son", L"Attiva/disattiva mute", L"Silenciar/activar", L"음소거 전환", L"切换静音", L"تبديل الكتم", L"Переключить звук", L"Stummschaltung", L"Alternar mudo", L"Dempen wisselen", L"Przełącz wyciszenie", L"Sessize al/aç"));
		DougaAddTip(m_tip, m_fs, LL14(L"フルスクリーン切替", L"Toggle fullscreen", L"Plein écran", L"Schermo intero", L"Pantalla completa", L"전체화면 전환", L"切换全屏", L"تبديل ملء الشاشة", L"Полный экран", L"Vollbild umschalten", L"Tela cheia", L"Volledig scherm", L"Pełny ekran", L"Tam ekran"));
		DougaAddTip(m_tip, m_sz1, LL14(L"通常サイズ (1x)", L"Normal size (1x)", L"Taille normale (1x)", L"Dimensione normale (1x)", L"Tamaño normal (1x)", L"표준 크기 (1x)", L"标准尺寸 (1x)", L"الحجم العادي (1x)", L"Обычный размер (1x)", L"Normalgröße (1x)", L"Tamanho normal (1x)", L"Normale grootte (1x)", L"Normalny rozmiar (1x)", L"Normal boyut (1x)"));
		DougaAddTip(m_tip, m_sz15, LL14(L"中間サイズ (1.5x)", L"Medium size (1.5x)", L"Taille moyenne (1,5x)", L"Dimensione media (1.5x)", L"Tamaño medio (1.5x)", L"중간 크기 (1.5x)", L"中等尺寸 (1.5x)", L"الحجم المتوسط (1.5x)", L"Средний размер (1.5x)", L"Mittlere Größe (1,5x)", L"Tamanho médio (1.5x)", L"Middelgroot (1.5x)", L"Średni rozmiar (1.5x)", L"Orta boyut (1.5x)"));
		DougaAddTip(m_tip, m_sz2, LL14(L"倍サイズ (2x)", L"Large size (2x)", L"Grande taille (2x)", L"Dimensione grande (2x)", L"Tamaño grande (2x)", L"2배 크기 (2x)", L"双倍尺寸 (2x)", L"الحجم الكبير (2x)", L"Двойной размер (2x)", L"Doppelte Größe (2x)", L"Tamanho grande (2x)", L"Grote maat (2x)", L"Duży rozmiar (2x)", L"Büyük boyut (2x)"));
		DougaAddTip(m_tip, m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
		DougaAddTip(m_tip, m_vol, LL14(L"DirectShow 音量", L"DirectShow volume", L"Volume DirectShow", L"Volume DirectShow", L"Volumen DirectShow", L"DirectShow 음량", L"DirectShow 音量", L"صوت DirectShow", L"Громкость DirectShow", L"DirectShow-Lautstärke", L"Volume DirectShow", L"DirectShow-volume", L"Głośność DirectShow", L"DirectShow sesi"));
		DougaAddTip(m_tip, m_rate, LL14(L"再生速度 0.1x〜4.0x（テンポと連動）", L"Playback speed 0.1x–4.0x (syncs with tempo)", L"Vitesse 0,1x–4,0x (liee au tempo)", L"Velocita 0.1x–4.0x (collegata al tempo)", L"Velocidad 0.1x–4.0x (sincronizada con tempo)", L"재생 속도 0.1x–4.0x (템포 연동)", L"播放速度 0.1x–4.0x（与速度联动）", L"سرعة 0.1x–4.0x (متزامنة مع الإيقاع)", L"Скорость 0.1x–4.0x (связана с темпом)", L"Geschwindigkeit 0,1x–4,0x (mit Tempo)", L"Velocidade 0,1x–4,0x (ligada ao tempo)", L"Snelheid 0,1x–4,0x (gekoppeld aan tempo)", L"Predkosc 0,1x–4,0x (zsynchronizowana z tempo)", L"Hiz 0.1x–4.0x (tempo ile bagli)"));
		DougaAddTip(m_tip, m_rateL, LL14(L"クリックで 1.00x（100%）に戻す", L"Click to reset to 1.00x (100%)", L"Clic pour revenir a 1,00x (100%)", L"Clic per tornare a 1.00x (100%)", L"Clic para volver a 1.00x (100%)", L"클릭 시 1.00x(100%)로 복귀", L"点击恢复为 1.00x（100%）", L"انقر لإعادة 1.00x (100%)", L"Щелчок: сброс на 1.00x (100%)", L"Klick setzt auf 1,00x (100%)", L"Clique para voltar a 1.00x (100%)", L"Klik voor reset naar 1.00x (100%)", L"Kliknij, aby wrócić do 1.00x (100%)", L"Tikla: 1.00x (100%)"));
		DougaAddTip(m_tip, m_rateVal, LL14(L"クリックで 1.00x（100%）に戻す", L"Click to reset to 1.00x (100%)", L"Clic pour revenir a 1,00x (100%)", L"Clic per tornare a 1.00x (100%)", L"Clic para volver a 1.00x (100%)", L"클릭 시 1.00x(100%)로 복귀", L"点击恢复为 1.00x（100%）", L"انقر لإعادة 1.00x (100%)", L"Щелчок: сброс на 1.00x (100%)", L"Klick setzt auf 1,00x (100%)", L"Clique para voltar a 1.00x (100%)", L"Klik voor reset naar 1.00x (100%)", L"Kliknij, aby wrócić do 1.00x (100%)", L"Tikla: 1.00x (100%)"));
		DougaAddTip(m_tip, m_seek, LL14(L"シーク", L"Seek", L"Position", L"Posizione", L"Posición", L"탐색", L"定位", L"تقديم", L"Перемотка", L"Suche", L"Busca", L"Zoeken", L"Przewijanie", L"Sar"));
	}

	m_ready = 1;
	LayoutBar();
	SyncSeekVol();
	return TRUE;
}

void CDougaBarHost::LayoutBar()
{
	if (!m_ready || !GetSafeHwnd()) return;
	CRect rc;
	GetClientRect(&rc);
	const int W = rc.Width();
	const int H = rc.Height();
	if (W < 8 || H < 8) return;

	const int pad = DougaDpiScale(m_hWnd, 4);
	const int gap = DougaDpiScale(m_hWnd, 3);
	const int row1 = DougaDpiScale(m_hWnd, 18);
	const int row2 = DougaDpiScale(m_hWnd, 26);
	const int y1 = pad;
	const int y2 = y1 + row1 + pad;
	m_short = (W < DougaDpiScale(m_hWnd, 520)) ? 1 : 0;

	auto move = [](CWnd& w, int x, int y, int ww, int hh) {
		if (w.GetSafeHwnd())
			w.SetWindowPos(NULL, x, y, ww, hh,
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
	};

	int timeW = DougaDpiScale(m_hWnd, m_short ? 90 : 110);
	int seekW = W - pad * 2 - timeW - gap;
	if (seekW < 10) seekW = 10;
	move(m_seek, pad, y1, seekW, row1);
	move(m_time, W - pad - timeW, y1, timeW, row1);

	int x = pad;
	int bh = H - y2 - pad;
	if (bh > row2) bh = row2;
	if (bh < 8) bh = 8;
	auto btnW = [&](int wide, int mid, int nar) {
		return DougaDpiScale(m_hWnd, m_short ? nar : (W < DougaDpiScale(m_hWnd, 700) ? mid : wide));
	};

	struct { CWnd* w; int ww; } btns[] = {
		{ &m_prev, btnW(48, 40, 28) },
		{ &m_rew,  btnW(48, 40, 28) },
		{ &m_play, btnW(52, 44, 32) },
		{ &m_pause,btnW(64, 52, 36) },
		{ &m_stop, btnW(48, 40, 28) },
		{ &m_ff,   btnW(52, 44, 32) },
		{ &m_next, btnW(48, 40, 28) },
		{ &m_fade, btnW(56, 44, 28) },
		{ &m_mute, btnW(48, 40, 28) },
		{ &m_fs,   btnW(56, 44, 28) },
		{ &m_sz1,  btnW(36, 32, 26) },
		{ &m_sz15, btnW(44, 36, 30) },
		{ &m_sz2,  btnW(36, 32, 26) },
		{ &m_help, DougaDpiScale(m_hWnd, m_short ? 22 : 26) },
	};
	for (size_t i = 0; i < sizeof(btns) / sizeof(btns[0]); ++i) {
		move(*btns[i].w, x, y2, btns[i].ww, bh);
		x += btns[i].ww + gap;
	}

	// 速度＋音量は右端固定。ボタンとの間の空きにメディア情報
	int rateLW = DougaDpiScale(m_hWnd, m_short ? 28 : 36);
	int rateVW = DougaDpiScale(m_hWnd, m_short ? 36 : 44);
	int rateW = DougaDpiScale(m_hWnd, m_short ? 56 : 80);
	int volLW = DougaDpiScale(m_hWnd, m_short ? 28 : 36);
	int volVW = DougaDpiScale(m_hWnd, m_short ? 32 : 40);
	int volW = DougaDpiScale(m_hWnd, m_short ? 60 : 90);
	int rateArea = rateLW + gap + rateW + gap + rateVW;
	int volArea = volLW + gap + volW + gap + volVW;
	int rightArea = rateArea + gap + volArea;
	int volX = W - pad - volArea;
	int rateX = volX - gap - rateArea;
	if (rateX < x + DougaDpiScale(m_hWnd, 8)) {
		rateW = DougaDpiScale(m_hWnd, 48); volW = DougaDpiScale(m_hWnd, 48);
		rateArea = rateLW + gap + rateW + gap + rateVW;
		volArea = volLW + gap + volW + gap + volVW;
		rightArea = rateArea + gap + volArea;
		volX = W - pad - volArea;
		rateX = volX - gap - rateArea;
	}
	int infoX = x + gap;
	int infoW = rateX - gap - infoX;
	if (infoW >= DougaDpiScale(m_hWnd, 40) && m_info.GetSafeHwnd()) {
		move(m_info, infoX, y2, infoW, bh);
		m_info.ShowWindow(SW_SHOWNA);
	} else if (m_info.GetSafeHwnd()) {
		m_info.ShowWindow(SW_HIDE);
	}
	move(m_rateL, rateX, y2, rateLW, bh); rateX += rateLW + gap;
	move(m_rate, rateX, y2, rateW, bh); rateX += rateW + gap;
	move(m_rateVal, rateX, y2, rateVW, bh);
	move(m_volL, volX, y2, volLW, bh); volX += volLW + gap;
	move(m_vol, volX, y2, volW, bh); volX += volW + gap;
	move(m_volVal, volX, y2, volVW, bh);

	if (m_laidShort != m_short) {
		m_laidShort = m_short;
		if (m_short) {
			m_prev.SetWindowText(L"|<");
			m_rew.SetWindowText(L"<<");
			m_play.SetWindowText(L">");
			m_pause.SetWindowText(L"||");
			m_stop.SetWindowText(L"[]");
			m_ff.SetWindowText(L">>");
			m_next.SetWindowText(L">|");
			m_fade.SetWindowText(L"FO");
			m_mute.SetWindowText(L"M");
			m_fs.SetWindowText(L"FS");
		} else {
			m_prev.SetWindowText(LL14(L"前へ", L"Prev", L"Prec", L"Prec", L"Ant", L"이전", L"上一首", L"السابق", L"Пред", L"Zurück", L"Ant", L"Vorige", L"Poprz", L"Onceki"));
			m_rew.SetWindowText(LL14(L"戻す", L"Rew", L"Recul", L"Ind", L"Retr", L"되감기", L"快退", L"ترجيع", L"Назад", L"Zurück", L"Voltar", L"Terug", L"Wstecz", L"Geri"));
			m_play.SetWindowText(LL14(L"再生", L"Play", L"Lect", L"Play", L"Play", L"재생", L"播放", L"تشغيل", L"Играть", L"Play", L"Play", L"Play", L"Odtw", L"Cal"));
			m_pause.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시정지", L"暂停", L"إيقاف", L"Пауза", L"Pause", L"Pausa", L"Pauze", L"Pauza", L"Duraklat"));
			m_stop.SetWindowText(LL14(L"停止", L"Stop", L"Stop", L"Stop", L"Stop", L"중지", L"停止", L"إيقاف", L"Стоп", L"Stop", L"Parar", L"Stop", L"Stop", L"Durdur"));
			m_ff.SetWindowText(LL14(L"進める", L"FF", L"Avance", L"Avanti", L"Avanz", L"빨리감기", L"快进", L"تقديم", L"Вперёд", L"Vor", L"Avançar", L"Vooruit", L"Naprz", L"Ileri"));
			m_next.SetWindowText(LL14(L"次へ", L"Next", L"Suiv", L"Succ", L"Sig", L"다음", L"下一首", L"التالي", L"След", L"Weiter", L"Prox", L"Volgende", L"Nast", L"Sonraki"));
			m_fade.SetWindowText(LL14(L"フェード", L"Fade", L"Fondu", L"Fade", L"Fade", L"페이드", L"淡出", L"تلاشي", L"Затух", L"Fade", L"Fade", L"Fade", L"Fade", L"Fade"));
			m_mute.SetWindowText(LL14(L"消音", L"Mute", L"Muet", L"Mute", L"Silenc", L"음소거", L"静音", L"كتم", L"Без зв.", L"Stumm", L"Mudo", L"Dempen", L"Wycisz", L"Sessiz"));
			m_fs.SetWindowText(LL14(L"全画面", L"Full", L"Plein", L"Intero", L"Completa", L"전체", L"全屏", L"ملء", L"Полн.", L"Voll", L"Cheia", L"Volledig", L"Pełny", L"Tam"));
		}
	}
	// 移動後の残像を消す
	Invalidate(TRUE);
}

static void DougaFormatDsVolPct(int dsPos, WCHAR* buf, size_t n)
{
	// MP と同じ換算: (-498..1) → 約 0%..100%
	double pct = (dsPos + 499) * 2.0 / 10.0;
	if (pct < 0.0) pct = 0.0;
	if (pct > 100.0) pct = 100.0;
	swprintf_s(buf, n, L"%.0f%%", pct);
}

void CDougaBarHost::SyncSeekVol()
{
	if (!m_ready || !og || !::IsWindow(og->GetSafeHwnd())) return;

	if (!m_seekDrag && m_seek.GetSafeHwnd()) {
		int mn = 0, mx = 1;
		og->m_time.GetRange(mn, mx);
		if (mx <= mn) mx = mn + 1;
		int selMn = 0, selMx = 0;
		og->m_time.GetSelection(selMn, selMx);
		int psPos = og->m_time.GetPos();
		int abA = -1, abB = -1;
		extern CMediaPlayerDlg* mp;
		if (mp) {
			abA = mp->m_abApos;
			abB = mp->m_abBpos;
		}
		m_seek.SetPlaybackMirror(psPos, selMn, selMx, mn, mx, abA, abB);
		if (::IsWindowVisible(m_seek.GetSafeHwnd()))
			m_seek.Invalidate(FALSE);

		int cur = psPos, tot = mx;
		if (tot < 1) tot = 1;
		auto fmt = [](int cs, WCHAR* buf, size_t n) {
			if (cs < 0) cs = 0;
			int s = cs / 100;
			int m = s / 60; s %= 60;
			int h = m / 60; m %= 60;
			if (h > 0) swprintf_s(buf, n, L"%d:%02d:%02d", h, m, s);
			else swprintf_s(buf, n, L"%02d:%02d", m, s);
		};
		WCHAR a[32], b[32], t[80];
		fmt(cur, a, 32); fmt(tot, b, 32);
		swprintf_s(t, L"%s / %s", a, b);
		m_time.SetWindowText(t);
	}

	if (m_vol.GetSafeHwnd()) {
		HWND hf = ::GetFocus();
		if (hf != m_vol.GetSafeHwnd()) {
			int v = og->m_dsval.GetPos();
			m_vol.SetPos(v);
			WCHAR vs[32];
			DougaFormatDsVolPct(v, vs, 32);
			m_volVal.SetWindowText(vs);
		}
	}
	if (m_rate.GetSafeHwnd() && DougaVideoRateActive()) {
		m_rate.EnableWindow(TRUE);
		// ラベルは無効化しない（WS_DISABLED だと Win11 で白背景に戻る）
		m_rateL.EnableWindow(TRUE);
		m_rateVal.EnableWindow(TRUE);
		// ドラッグ中は GetRate/テンポ同期でつまみが戻るのを防ぐ（音量は m_dsval 即時反映なので問題になりにくい）
		HWND hf = ::GetFocus();
		HWND cap = ::GetCapture();
		const BOOL busy = m_rateDrag
			|| (hf && hf == m_rate.GetSafeHwnd())
			|| (cap && cap == m_rate.GetSafeHwnd());
		if (!busy && pMediaSeeking) {
			double cur = 1.0;
			if (FAILED(pMediaSeeking->GetRate(&cur)) || cur <= 0.0)
				cur = TempoPlaybackRateFromPos(tempo);
			if (cur < 0.1) cur = 0.1;
			if (cur > 4.0) cur = 4.0;
			m_rate.SetPos((int)(cur * 100.0 + 0.5));
			WCHAR vs[32];
			swprintf_s(vs, L"%.2fx", cur);
			m_rateVal.SetWindowText(vs);
		}
	} else if (m_rate.GetSafeHwnd()) {
		m_rate.EnableWindow(FALSE);
		// ラベルは常に有効のまま（見た目の白抜け防止）。文字色はスライダー無効で十分伝わる
		m_rateL.EnableWindow(TRUE);
		m_rateVal.EnableWindow(TRUE);
	}
}

void CDougaBarHost::SetMediaInfoText(LPCWSTR text)
{
	if (!m_info.GetSafeHwnd()) return;
	m_info.SetWindowText(text ? text : L"");
}

void CDougaBarHost::RefreshAero()
{
	if (!GetSafeHwnd()) return;
	// 動画窓のバーは不透明固定(透過とEVRの競合を避ける)
	PROPAGATE_AERO_TO_CHILDREN(m_hWnd, FALSE);
}

void CDougaBarHost::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	if (!og || !pScrollBar) {
		CWnd::OnHScroll(nSBCode, nPos, pScrollBar);
		return;
	}
	HWND h = pScrollBar->GetSafeHwnd();
	if (h == m_seek.GetSafeHwnd()) {
		int p = m_seek.GetPos();
		if (nSBCode == TB_THUMBTRACK || nSBCode == TB_THUMBPOSITION)
			m_seekDrag = 1;
		og->m_time.SetPos(p);
		// 本体 OnHScroll へ SendMessage しない（cl2 / RubberBand_DestroyBank で UI が永久待ちになる）
		if (nSBCode == TB_THUMBPOSITION || nSBCode == TB_ENDTRACK
			|| nSBCode == TB_PAGEUP || nSBCode == TB_PAGEDOWN
			|| nSBCode == TB_LINEUP || nSBCode == TB_LINEDOWN
			|| nSBCode == TB_TOP || nSBCode == TB_BOTTOM) {
			extern IMediaPosition* pMediaPosition;
			extern BOOL videoonly;
												if (pMediaPosition && (mode == -2 || (mode > 0 && videoonly == TRUE))) {
				int mn = 0, mx = 1;
				og->m_time.GetRange(mn, mx);
				if (p < mn) p = mn;
				if (p > mx) p = mx;
				og->m_time.SetPos(p);
				playb = (__int64)p;
				poss = 0;
				poss5 = p;
				if (m_owner)
					m_owner->seek((LONGLONG)((float)p * 100000.0f));
			}
			m_seekDrag = 0;
		}
	} else if (h == m_vol.GetSafeHwnd()) {
		int v = m_vol.GetPos();
		og->m_dsval.SetPos(v);
		m_muted = 0;
		if (DougaPitchCorrect_IsActive())
			DougaPitchCorrect_SetVolumeDsPos(v);
		WCHAR vs[32];
		DougaFormatDsVolPct(v, vs, 32);
		m_volVal.SetWindowText(vs);
	} else if (h == m_rate.GetSafeHwnd()) {
		if (nSBCode == TB_THUMBTRACK || nSBCode == TB_THUMBPOSITION || nSBCode == TB_ENDTRACK)
			m_rateDrag = 1;
		int v = m_rate.GetPos();
		if (nSBCode == TB_THUMBTRACK && nPos != 0)
			v = (int)nPos; // track 中はメッセージ側の値が新しいことがある
		if (v < 10) v = 10;
		if (v > 400) v = 400;
		DougaSetPlaybackRate((double)v / 100.0, TRUE);
		WCHAR vs[32];
		swprintf_s(vs, L"%.2fx", (double)v / 100.0);
		m_rateVal.SetWindowText(vs);
		// 解除は PreTranslate の LBUTTONUP（THUMBPOSITION で落とすとドラッグ中に同期が割り込む）
		if (nSBCode == TB_ENDTRACK && ::GetCapture() != m_rate.GetSafeHwnd())
			m_rateDrag = 0;
	}
	CWnd::OnHScroll(nSBCode, nPos, pScrollBar);
}

void CDougaBarHost::OnBnPrev() { if (og) MpTaskbarPrevTrack(); }
void CDougaBarHost::OnBnNext() { if (og) MpTaskbarNextTrack(); }
void CDougaBarHost::OnBnPlay() { if (og) MpTaskbarReplay(); }
void CDougaBarHost::OnBnPause() { if (m_owner) m_owner->On32775(); }
void CDougaBarHost::OnBnStop()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON1, BN_CLICKED), 0);
}
void CDougaBarHost::OnBnRew()
{
	if (og) og->SendMessage(WM_HOTKEY, (WPARAM)8003, 0);
}
void CDougaBarHost::OnBnFf()
{
	if (og) og->SendMessage(WM_HOTKEY, (WPARAM)8002, 0);
}
void CDougaBarHost::OnBnFade()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON5, BN_CLICKED), 0);
}
void CDougaBarHost::OnBnMute()
{
	if (!og) return;
	if (!m_muted) {
		m_mutePos = og->m_dsval.GetPos();
		og->m_dsval.SetPos(-498);
		m_muted = 1;
	} else {
		og->m_dsval.SetPos(m_mutePos);
		m_muted = 0;
	}
	SyncSeekVol();
}
void CDougaBarHost::OnBnFs()
{
	if (m_owner)
		m_owner->ToggleFullScreen();
}
void CDougaBarHost::OnBnSz1() { if (m_owner) m_owner->OnMenuitem32771(); }
void CDougaBarHost::OnBnSz15() { if (m_owner) m_owner->OnMenuitem32773(); }
void CDougaBarHost::OnBnSz2() { if (m_owner) m_owner->OnMenuitem32772(); }
void CDougaBarHost::OnBnHelp() { if (m_owner) m_owner->ShowHelpSheet(); }
void CDougaBarHost::OnStnRateReset()
{
	// テンポ／ピッチラベルと同様: クリックで 100%（1.00x）へ
	DougaSetPlaybackRate(1.0, TRUE);
	if (m_rate.GetSafeHwnd())
		m_rate.SetPos(100);
	if (m_rateVal.GetSafeHwnd())
		m_rateVal.SetWindowText(L"1.00x");
}

/////////////////////////////////////////////////////////////////////////////
// CDouga — 動画配置(バーと競合しないよう videoSite に限定)

int CDouga::GetBarHeight() const
{
	if (!m_bar.IsBarReady() || savedata.fs) return 0;
	return m_bar.BarHeight();
}

static CString DougaFourCCName(DWORD fcc)
{
	WCHAR s[8] = {};
	char c[4] = {
		(char)(fcc & 0xff),
		(char)((fcc >> 8) & 0xff),
		(char)((fcc >> 16) & 0xff),
		(char)((fcc >> 24) & 0xff)
	};
	for (int i = 0; i < 4; i++) {
		if (c[i] < 32 || c[i] > 126) return L"";
		s[i] = (WCHAR)c[i];
	}
	// よく使う FourCC を見やすい表記に
	if (_wcsicmp(s, L"H264") == 0 || _wcsicmp(s, L"AVC1") == 0 || _wcsicmp(s, L"X264") == 0) return L"H264";
	if (_wcsicmp(s, L"HEVC") == 0 || _wcsicmp(s, L"HVC1") == 0 || _wcsicmp(s, L"HEV1") == 0) return L"HEVC";
	if (_wcsicmp(s, L"VP90") == 0 || _wcsicmp(s, L"VP09") == 0) return L"VP9";
	if (_wcsicmp(s, L"VP80") == 0 || _wcsicmp(s, L"VP08") == 0) return L"VP8";
	if (_wcsicmp(s, L"AV01") == 0) return L"AV1";
	if (_wcsicmp(s, L"WMV3") == 0) return L"WMV3";
	if (_wcsicmp(s, L"WVC1") == 0) return L"VC1";
	if (_wcsicmp(s, L"MP4V") == 0 || _wcsicmp(s, L"XVID") == 0 || _wcsicmp(s, L"DIVX") == 0) return s;
	if (_wcsicmp(s, L"MP4A") == 0 || _wcsicmp(s, L"AAC ") == 0 || _wcsicmp(s, L"AAC\0") == 0 ||
		_wcsicmp(s, L"ADTS") == 0 || _wcsicmp(s, L"LATM") == 0 || _wcsicmp(s, L"AACL") == 0) return L"AAC";
	if (_wcsicmp(s, L"AC-3") == 0 || _wcsicmp(s, L"AC3 ") == 0 || _wcsicmp(s, L"DAC3") == 0) return L"AC3";
	if (_wcsicmp(s, L"EAC3") == 0 || _wcsicmp(s, L"EC-3") == 0 || _wcsicmp(s, L"DEC3") == 0) return L"EAC3";
	if (_wcsicmp(s, L"DTS ") == 0 || _wcsicmp(s, L"DTS\0") == 0 || _wcsicmp(s, L"DTSB") == 0) return L"DTS";
	if (_wcsicmp(s, L"DTSH") == 0 || _wcsicmp(s, L"DTSL") == 0) return L"DTS-HD";
	if (_wcsicmp(s, L"TRUE") == 0 || _wcsicmp(s, L"MLPA") == 0) return L"TrueHD";
	if (_wcsicmp(s, L"OPUS") == 0) return L"Opus";
	if (_wcsicmp(s, L"FLAC") == 0 || _wcsicmp(s, L"fLaC") == 0) return L"FLAC";
	if (_wcsicmp(s, L"VORB") == 0 || _wcsicmp(s, L"VOR1") == 0) return L"Vorbis";
	if (_wcsicmp(s, L"ALAC") == 0) return L"ALAC";
	if (_wcsicmp(s, L"APE ") == 0) return L"APE";
	if (_wcsicmp(s, L"TTA1") == 0) return L"TTA";
	if (_wcsicmp(s, L"WAVP") == 0 || _wcsicmp(s, L"WVPK") == 0) return L"WavPack";
	if (_wcsicmp(s, L"SPEX") == 0) return L"Speex";
	if (_wcsicmp(s, L"SAMR") == 0 || _wcsicmp(s, L"SAWB") == 0) return L"AMR";
	if (_wcsicmp(s, L"QDM2") == 0 || _wcsicmp(s, L"QDMC") == 0) return L"QDesign";
	_wcsupr_s(s, 8);
	return s;
}

static CString DougaGuessCodecFromText(LPCWSTR text)
{
	if (!text || !text[0]) return L"";
	CString t(text);
	t.MakeUpper();
	// 長い／具体的な名前を先に
	if (t.Find(L"TRUEHD") >= 0 || t.Find(L"TRUE-HD") >= 0 || t.Find(L"MLP") >= 0) return L"TrueHD";
	if (t.Find(L"DTS-HD") >= 0 || t.Find(L"DTSHD") >= 0 || t.Find(L"DTS MA") >= 0) return L"DTS-HD";
	if (t.Find(L"E-AC-3") >= 0 || t.Find(L"EAC3") >= 0 || t.Find(L"E-AC3") >= 0 || t.Find(L"DD+") >= 0 || t.Find(L"DOLBY DIGITAL PLUS") >= 0) return L"EAC3";
	if (t.Find(L"AC-3") >= 0 || t.Find(L"AC3") >= 0 || t.Find(L"DOLBY DIGITAL") >= 0 || t.Find(L"A52") >= 0) return L"AC3";
	if (t.Find(L"DTS") >= 0) return L"DTS";
	if (t.Find(L"HE-AAC") >= 0 || t.Find(L"HEAAC") >= 0) return L"HE-AAC";
	if (t.Find(L"AAC") >= 0 || t.Find(L"MP4A") >= 0 || t.Find(L"ADTS") >= 0 || t.Find(L"LATM") >= 0) return L"AAC";
	if (t.Find(L"MP3") >= 0 || t.Find(L"MPEG LAYER-3") >= 0 || t.Find(L"MPEG-1 LAYER 3") >= 0) return L"MP3";
	if (t.Find(L"FLAC") >= 0) return L"FLAC";
	if (t.Find(L"OPUS") >= 0) return L"Opus";
	if (t.Find(L"VORBIS") >= 0 || t.Find(L"OGG") >= 0) return L"Vorbis";
	if (t.Find(L"ALAC") >= 0 || t.Find(L"APPLE LOSSLESS") >= 0) return L"ALAC";
	if (t.Find(L"WMA") >= 0) return L"WMA";
	if (t.Find(L"PCM") >= 0 || t.Find(L"LPCM") >= 0) return L"PCM";
	if (t.Find(L"FLOAT") >= 0) return L"Float";
	if (t.Find(L"AMR") >= 0) return L"AMR";
	if (t.Find(L"SPEEX") >= 0) return L"Speex";
	if (t.Find(L"APE") >= 0 || t.Find(L"MONKEY") >= 0) return L"APE";
	if (t.Find(L"WAVPACK") >= 0) return L"WavPack";
	if (t.Find(L"TTA") >= 0) return L"TTA";
	if (t.Find(L"HEVC") >= 0 || t.Find(L"H.265") >= 0 || t.Find(L"H265") >= 0) return L"HEVC";
	if (t.Find(L"AVC") >= 0 || t.Find(L"H.264") >= 0 || t.Find(L"H264") >= 0) return L"H264";
	if (t.Find(L"AV1") >= 0) return L"AV1";
	if (t.Find(L"VP9") >= 0) return L"VP9";
	if (t.Find(L"VP8") >= 0) return L"VP8";
	if (t.Find(L"MPEG-2") >= 0 || t.Find(L"MPEG2") >= 0) return L"MPEG2";
	if (t.Find(L"MPEG-4") >= 0 || t.Find(L"MPEG4") >= 0 || t.Find(L"XVID") >= 0 || t.Find(L"DIVX") >= 0) return L"MPEG4";
	if (t.Find(L"VC-1") >= 0 || t.Find(L"VC1") >= 0 || t.Find(L"WMV3") >= 0) return L"VC1";
	return L"";
}

static CString DougaWaveFormatTagLabel(WORD tag)
{
	switch (tag) {
	case WAVE_FORMAT_PCM: return L"PCM";
	case WAVE_FORMAT_IEEE_FLOAT: return L"Float";
	case WAVE_FORMAT_ALAW: return L"A-law";
	case WAVE_FORMAT_MULAW: return L"μ-law";
	case WAVE_FORMAT_ADPCM: return L"ADPCM";
	case WAVE_FORMAT_IMA_ADPCM: return L"IMA-ADPCM";
	case WAVE_FORMAT_MPEGLAYER3: return L"MP3";
	case WAVE_FORMAT_MPEG: return L"MPEG";
	case 0x0160: case 0x0161: case 0x0162: case 0x0163: return L"WMA";
	case 0x00FF: // WAVE_FORMAT_RAW_AAC1
	case 0x1600: // AAC ADTS-ish
	case 0x1610: // WAVE_FORMAT_MPEG_HEAAC
	case 0x706D: // 'mp'
		return L"AAC";
	case 0x2000: return L"AC3";      // WAVE_FORMAT_DOLBY_AC3
	case 0x0092: return L"AC3";      // WAVE_FORMAT_DOLBY_AC3_SPDIF
	case 0x2001: return L"DTS";
	case 0x2002: return L"AAC";      // sometimes MPEG4 AAC
	case 0xF1AC: return L"FLAC";
	case 0x704F: return L"Opus";     // 'Op'
	case 0x674F: return L"Opus";
	case 0x566F: return L"Vorbis";   // 'oV'
	default: return L"";
	}
}

static CString DougaSubtypeLabel(const GUID& sub, BOOL isAudio)
{
	if (sub == MEDIASUBTYPE_MPEG2_VIDEO) return L"MPEG2";
	if (sub == MEDIASUBTYPE_MPEG1Payload) return L"MPEG1";
	if (isAudio) {
		if (sub == MEDIASUBTYPE_PCM) return L"PCM";
		if (sub == MEDIASUBTYPE_IEEE_FLOAT) return L"Float";
		if (sub == MEDIASUBTYPE_MPEG1AudioPayload) return L"MP1";
		if (sub == MEDIASUBTYPE_DOLBY_AC3) return L"AC3";
		if (sub == MEDIASUBTYPE_DTS) return L"DTS";
	}
	CString fcc = DougaFourCCName(sub.Data1);
	if (!fcc.IsEmpty()) {
		// 非圧縮YUV FourCCはコーデック表示から除外
		if (fcc == L"YV12" || fcc == L"NV12" || fcc == L"YUY2" || fcc == L"UYVY" || fcc == L"P010")
			return L"";
		return fcc;
	}
	return L"";
}

static BOOL DougaIsCompressedCodecName(const CString& codec)
{
	if (codec.IsEmpty()) return FALSE;
	if (codec == L"PCM" || codec == L"Float" || codec == L"A-law" || codec == L"μ-law")
		return FALSE;
	return TRUE;
}

static CString DougaCodecFromMediaType(const AM_MEDIA_TYPE& mt, BOOL isAudio)
{
	CString codec = DougaSubtypeLabel(mt.subtype, isAudio);
	if (DougaIsCompressedCodecName(codec))
		return codec;

	if (mt.formattype == FORMAT_WaveFormatEx && mt.pbFormat && mt.cbFormat >= sizeof(WAVEFORMATEX)) {
		const WAVEFORMATEX* wfx = (const WAVEFORMATEX*)mt.pbFormat;
		WORD tag = wfx->wFormatTag;
		if (tag == WAVE_FORMAT_EXTENSIBLE && mt.cbFormat >= sizeof(WAVEFORMATEXTENSIBLE)) {
			const WAVEFORMATEXTENSIBLE* wfex = (const WAVEFORMATEXTENSIBLE*)mt.pbFormat;
			CString sub = DougaSubtypeLabel(wfex->SubFormat, TRUE);
			if (DougaIsCompressedCodecName(sub))
				return sub;
			// extensible の SubFormat.Data1 が FourCC のこともある
			sub = DougaFourCCName(wfex->SubFormat.Data1);
			if (DougaIsCompressedCodecName(sub))
				return sub;
		}
		CString fromTag = DougaWaveFormatTagLabel(tag);
		if (DougaIsCompressedCodecName(fromTag))
			return fromTag;
		if (codec.IsEmpty())
			codec = fromTag;
	}
	return codec;
}

static int DougaChannelsFromMediaType(const AM_MEDIA_TYPE& mt)
{
	if (mt.formattype == FORMAT_WaveFormatEx && mt.pbFormat && mt.cbFormat >= sizeof(WAVEFORMATEX)) {
		const WAVEFORMATEX* wfx = (const WAVEFORMATEX*)mt.pbFormat;
		return wfx->nChannels;
	}
	return 0;
}

static CString DougaChannelLabel(int ch)
{
	switch (ch) {
	case 1: return L"1.0";
	case 2: return L"2.0";
	case 3: return L"2.1";
	case 4: return L"4.0";
	case 5: return L"4.1";
	case 6: return L"5.1";
	case 7: return L"6.1";
	case 8: return L"7.1";
	default: {
		CString s;
		s.Format(L"%d.0", ch);
		return s;
	}
	}
}

static BOOL DougaIsRawVideoSubtype(const GUID& sub)
{
	CString fcc = DougaFourCCName(sub.Data1);
	return (fcc == L"YV12" || fcc == L"NV12" || fcc == L"YUY2" || fcc == L"UYVY" || fcc == L"P010");
}

static void DougaConsiderMediaType(const AM_MEDIA_TYPE& mt,
	CString& bestVideoCodec, int& bestVideoScore, long& bestW, long& bestH,
	CString& bestAudioCodec, int& bestAudioScore, int& bestCh)
{
	if (mt.majortype == MEDIATYPE_Video) {
		CString codec = DougaCodecFromMediaType(mt, FALSE);
		int score = 0;
		if (DougaIsCompressedCodecName(codec)) score = 3;
		else if (!DougaIsRawVideoSubtype(mt.subtype) && !codec.IsEmpty()) score = 1;
		if (score > bestVideoScore) {
			bestVideoScore = score;
			bestVideoCodec = codec;
		}
		long w = 0, h = 0;
		if (mt.formattype == FORMAT_VideoInfo && mt.pbFormat) {
			VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)mt.pbFormat;
			w = vih->bmiHeader.biWidth;
			h = abs(vih->bmiHeader.biHeight);
		} else if (mt.formattype == FORMAT_VideoInfo2 && mt.pbFormat) {
			VIDEOINFOHEADER2* vih = (VIDEOINFOHEADER2*)mt.pbFormat;
			w = vih->bmiHeader.biWidth;
			h = abs(vih->bmiHeader.biHeight);
		}
		if (w > 0 && h > 0) {
			bestW = w;
			bestH = h;
		}
	}
	else if (mt.majortype == MEDIATYPE_Audio) {
		CString codec = DougaCodecFromMediaType(mt, TRUE);
		int score = 0;
		if (DougaIsCompressedCodecName(codec)) score = 3;
		else if (!codec.IsEmpty()) score = 1;
		if (score > bestAudioScore) {
			bestAudioScore = score;
			bestAudioCodec = codec;
		}
		int ch = DougaChannelsFromMediaType(mt);
		if (ch > bestCh)
			bestCh = ch;
	}
}

static void DougaScanGraphMediaInfo(IGraphBuilder* pGraph,
	CString& bestVideoCodec, long& bestW, long& bestH,
	CString& bestAudioCodec, int& bestCh)
{
	bestVideoCodec.Empty();
	bestAudioCodec.Empty();
	bestW = bestH = 0;
	bestCh = 0;
	if (!pGraph) return;

	int bestVideoScore = -1;
	int bestAudioScore = -1;

	IEnumFilters* pEnum = NULL;
	if (FAILED(pGraph->EnumFilters(&pEnum)) || !pEnum) return;
	IBaseFilter* pFilter = NULL;
	ULONG fetched = 0;
	while (pEnum->Next(1, &pFilter, &fetched) == S_OK) {
		FILTER_INFO fi = {};
		if (SUCCEEDED(pFilter->QueryFilterInfo(&fi))) {
			CString guess = DougaGuessCodecFromText(fi.achName);
			// デコーダ名から推測(弱い手がかり)
			if (DougaIsCompressedCodecName(guess) && bestAudioScore < 2) {
				CString name(fi.achName);
				name.MakeUpper();
				if (name.Find(L"AUDIO") >= 0 || name.Find(L"AAC") >= 0 || name.Find(L"AC3") >= 0 ||
					name.Find(L"DTS") >= 0 || name.Find(L"FLAC") >= 0 || name.Find(L"OPUS") >= 0) {
					bestAudioCodec = guess;
					bestAudioScore = 2;
				}
			}
			if (fi.pGraph) fi.pGraph->Release();
		}

		IEnumPins* pPins = NULL;
		if (SUCCEEDED(pFilter->EnumPins(&pPins)) && pPins) {
			IPin* pPin = NULL;
			while (pPins->Next(1, &pPin, NULL) == S_OK) {
				// 接続済みタイプ(デコーダ入力=圧縮、出力=PCM の両方を見る)
				AM_MEDIA_TYPE mt = {};
				if (SUCCEEDED(pPin->ConnectionMediaType(&mt))) {
					DougaConsiderMediaType(mt, bestVideoCodec, bestVideoScore, bestW, bestH,
						bestAudioCodec, bestAudioScore, bestCh);
					FreeMediaType(mt);
				}
				// 未接続／候補タイプも列挙(スプリッタ側の圧縮型)
				IEnumMediaTypes* pEnumMt = NULL;
				if (SUCCEEDED(pPin->EnumMediaTypes(&pEnumMt)) && pEnumMt) {
					AM_MEDIA_TYPE* pmt = NULL;
					while (pEnumMt->Next(1, &pmt, NULL) == S_OK) {
						if (pmt) {
							DougaConsiderMediaType(*pmt, bestVideoCodec, bestVideoScore, bestW, bestH,
								bestAudioCodec, bestAudioScore, bestCh);
							DeleteMediaType(pmt);
						}
					}
					pEnumMt->Release();
				}
				PIN_INFO pi = {};
				if (SUCCEEDED(pPin->QueryPinInfo(&pi))) {
					CString guess = DougaGuessCodecFromText(pi.achName);
					if (DougaIsCompressedCodecName(guess) && bestAudioScore < 2)
						{ bestAudioCodec = guess; bestAudioScore = 2; }
					if (pi.pFilter) pi.pFilter->Release();
				}
				pPin->Release();
			}
			pPins->Release();
		}
		pFilter->Release();
	}
	pEnum->Release();
}

void CDouga::RefreshBarMediaInfo()
{
	if (!m_bar.IsBarReady()) return;

	CString videoCodec, audioCodec;
	long w = width, h = height;
	int ch = 0;
	DougaScanGraphMediaInfo(pGraphBuilder, videoCodec, w, h, audioCodec, ch);

	// IAMStreamSelect の元ストリーム情報(圧縮AAC/AC3等が残っていることが多い)
	if (iam) {
		DWORD total = 0;
		if (SUCCEEDED(iam->Count(&total))) {
			for (DWORD i = 0; i < total; ++i) {
				AM_MEDIA_TYPE* am = NULL;
				LPWSTR pname = NULL;
				DWORD flags = 0;
				if (FAILED(iam->Info(i, &am, &flags, NULL, NULL, &pname, NULL, NULL)))
					continue;
				if (am) {
					if (am->majortype == MEDIATYPE_Audio) {
						CString c = DougaCodecFromMediaType(*am, TRUE);
						if (!DougaIsCompressedCodecName(c) && pname)
							c = DougaGuessCodecFromText(pname);
						if (DougaIsCompressedCodecName(c))
							audioCodec = c;
						int cch = DougaChannelsFromMediaType(*am);
						if (cch > ch) ch = cch;
					}
					else if (am->majortype == MEDIATYPE_Video) {
						CString c = DougaCodecFromMediaType(*am, FALSE);
						if (DougaIsCompressedCodecName(c))
							videoCodec = c;
						if (am->formattype == FORMAT_VideoInfo && am->pbFormat) {
							VIDEOINFOHEADER* vih = (VIDEOINFOHEADER*)am->pbFormat;
							if (vih->bmiHeader.biWidth > 0) {
								w = vih->bmiHeader.biWidth;
								h = abs(vih->bmiHeader.biHeight);
							}
						}
					}
					DeleteMediaType(am);
				}
				if (pname) {
					if (!DougaIsCompressedCodecName(audioCodec)) {
						CString g = DougaGuessCodecFromText(pname);
						if (DougaIsCompressedCodecName(g))
							audioCodec = g;
					}
					CoTaskMemFree(pname);
				}
			}
		}
	}

	// streamname[] フォールバック
	if (!DougaIsCompressedCodecName(audioCodec)) {
		for (int i = 0; i < audionum && i < 40; ++i) {
			CString g = DougaGuessCodecFromText(streamname[i]);
			if (DougaIsCompressedCodecName(g)) {
				audioCodec = g;
				break;
			}
		}
	}

	if (w <= 0 || h <= 0) {
		w = width;
		h = height;
	}

	CString videoPart, audioPart;
	if (w > 0 && h > 0) {
		if (DougaIsCompressedCodecName(videoCodec) || !videoCodec.IsEmpty())
			videoPart.Format(L"[%s %ldx%ld]", (LPCWSTR)videoCodec, w, h);
		else
			videoPart.Format(L"[%ldx%ld]", w, h);
	} else if (!videoCodec.IsEmpty()) {
		videoPart.Format(L"[%s]", (LPCWSTR)videoCodec);
	}

	if (DougaIsCompressedCodecName(audioCodec) || (!audioCodec.IsEmpty() && audioCodec != L"PCM" && audioCodec != L"Float")) {
		if (ch > 0)
			audioPart.Format(L"[%s %s]", (LPCWSTR)audioCodec, (LPCWSTR)DougaChannelLabel(ch));
		else
			audioPart.Format(L"[%s]", (LPCWSTR)audioCodec);
	} else if (ch > 0) {
		// 圧縮名が取れないときだけ PCM/Float を出す。チャンネルだけは避ける
		if (!audioCodec.IsEmpty())
			audioPart.Format(L"[%s %s]", (LPCWSTR)audioCodec, (LPCWSTR)DougaChannelLabel(ch));
		else
			audioPart.Format(L"[%s]", (LPCWSTR)DougaChannelLabel(ch));
	}

	CString all;
	if (!videoPart.IsEmpty() && !audioPart.IsEmpty())
		all = videoPart + L" " + audioPart;
	else if (!videoPart.IsEmpty())
		all = videoPart;
	else
		all = audioPart;
	m_bar.SetMediaInfoText(all);
}

void CDouga::RefreshBarAero()
{
	m_bar.RefreshAero();
}

BOOL CDouga::PreTranslateMessage(MSG* pMsg)
{
	if (m_bar.IsBarReady() && m_bar.m_tip.GetSafeHwnd())
		m_bar.m_tip.RelayEvent(pMsg);
	// [ / ] A-B（音楽と同じ。動画前面でも効く）
	if (pMsg && pMsg->message == WM_KEYDOWN
		&& (pMsg->wParam == VK_OEM_4 || pMsg->wParam == VK_OEM_6
			|| pMsg->wParam == '[' || pMsg->wParam == ']')) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->HandleAbBracketKey(pMsg->wParam)) {
			m_bar.SyncSeekVol();
			return TRUE;
		}
	}
	return CFrameWnd::PreTranslateMessage(pMsg);
}

// 外枠をタスクバー除外のワークエリア内に収める。
// はみ出すときだけ動画部を等比縮小し、位置もワーク内へ補正する(収まるなら触らない)。
static void DougaFitOuterToWorkArea(HWND hwnd, int& x, int& y, int& outerW, int& outerH, int chromeH)
{
	if (!hwnd || !::IsWindow(hwnd) || outerW < 1 || outerH < 1)
		return;
	if (chromeH < 0)
		chromeH = 0;

	RECT propose;
	propose.left = x;
	propose.top = y;
	propose.right = x + outerW;
	propose.bottom = y + outerH;
	HMONITOR mon = ::MonitorFromRect(&propose, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	RECT rcWork;
	if (mon && ::GetMonitorInfo(mon, &mi))
		rcWork = mi.rcWork;
	else if (!::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0))
		return;

	const int workW = rcWork.right - rcWork.left;
	const int workH = rcWork.bottom - rcWork.top;
	if (workW < 32 || workH < 32)
		return;

	int videoW = outerW;
	int videoH = outerH - chromeH;
	if (videoH < 1)
		videoH = 1;

	int maxVideoW = workW;
	int maxVideoH = workH - chromeH;
	if (maxVideoH < 1)
		maxVideoH = 1;

	if (videoW > maxVideoW || videoH > maxVideoH) {
		const double sx = (double)maxVideoW / (double)videoW;
		const double sy = (double)maxVideoH / (double)videoH;
		double s = (sx < sy) ? sx : sy;
		if (s < 1.0) {
			videoW = (int)(videoW * s);
			videoH = (int)(videoH * s);
			if (videoW < 1) videoW = 1;
			if (videoH < 1) videoH = 1;
		}
	}

	outerW = videoW;
	outerH = videoH + chromeH;
	if (outerW > workW) outerW = workW;
	if (outerH > workH) outerH = workH;
	if (outerW < 1) outerW = 1;
	if (outerH < 1) outerH = 1;

	if (x + outerW > rcWork.right) x = rcWork.right - outerW;
	if (y + outerH > rcWork.bottom) y = rcWork.bottom - outerH;
	if (x < rcWork.left) x = rcWork.left;
	if (y < rcWork.top) y = rcWork.top;
}

// アスペクト比維持がONなら、元サイズ(rcm)比で dest 矩形をレターボックス化する。
// OFF や元サイズ不明なら矩形は触らず従来どおり引き伸ばす。調整したときだけ TRUE。
static BOOL DougaLetterbox(CRect& dest)
{
	if (!savedata.dougaaspect || rcm.right <= 0 || rcm.bottom <= 0) return FALSE;
	const int dw = dest.Width(), dh = dest.Height();
	if (dw < 1 || dh < 1) return FALSE;
	const double sx = (double)dw / (double)rcm.right;
	const double sy = (double)dh / (double)rcm.bottom;
	const double s = (sx < sy) ? sx : sy;
	int w = (int)(rcm.right * s + 0.5);
	int h = (int)(rcm.bottom * s + 0.5);
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	const int x = dest.left + (dw - w) / 2;
	const int y = dest.top + (dh - h) / 2;
	dest.SetRect(x, y, x + w, y + h);
	return TRUE;
}

void CDouga::ApplyVideoDest()
{
	if (!GetSafeHwnd() || m_applyBusy) return;
	m_applyBusy = 1;

	CRect client;
	GetClientRect(&client);
	const int barH = GetBarHeight();
	const int vw = client.Width();
	int vh = client.Height() - barH;
	if (vh < 1) vh = 1;
	if (vw < 1) {
		m_applyBusy = 0;
		return;
	}

	// 動画は NOREDRAW 可。バーは NOCOPYBITS で旧ピクセルを引きずらない
	HDWP hdwp = ::BeginDeferWindowPos(2);
	if (hdwp && m_videoSite.GetSafeHwnd()) {
		hdwp = ::DeferWindowPos(hdwp, m_videoSite.m_hWnd, HWND_BOTTOM,
			0, 0, vw, vh, SWP_NOACTIVATE | SWP_NOREDRAW);
	}
	if (hdwp && m_bar.IsBarReady() && m_bar.GetSafeHwnd()) {
		if (savedata.fs) {
			::ShowWindow(m_bar.m_hWnd, SW_HIDE);
		} else {
			hdwp = ::DeferWindowPos(hdwp, m_bar.m_hWnd, HWND_TOP,
				0, vh, vw, barH,
				SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOCOPYBITS);
		}
	}
	if (hdwp)
		::EndDeferWindowPos(hdwp);
	// 動画HWND/レンダラ再配置後もバーを最前面に（クリックが動画側へ吸われるのを防ぐ）
	if (!savedata.fs && m_bar.IsBarReady() && m_bar.GetSafeHwnd())
		::SetWindowPos(m_bar.m_hWnd, HWND_TOP, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	// ドラッグ中は子コントロール再配置しない(残像の主因)。枠だけ追従。
	if (!savedata.fs && m_bar.IsBarReady() && !m_inSizeMove)
		m_bar.LayoutBar();

	CRect vr(0, 0, vw, vh);
	if (DougaLetterbox(vr) && m_videoSite.GetSafeHwnd())
		m_videoSite.Invalidate(TRUE); // 帯に旧フレームを残さない
	if (ev && Vdc) {
		MFVideoNormalizedRect mvnr = { 0, 0, 1, 1 };
		Vdc->SetVideoPosition(&mvnr, &vr);
	} else if (!savedata.fs) {
		if (pBasicVideo) {
			pBasicVideo->put_DestinationWidth(vr.Width());
			pBasicVideo->put_DestinationHeight(vr.Height());
		}
		if (pVideoWindow) {
			pVideoWindow->put_Top(vr.top);
			pVideoWindow->put_Left(vr.left);
			pVideoWindow->put_Height(vr.Height());
			pVideoWindow->put_Width(vr.Width());
		}
	}

	m_applyBusy = 0;
}


BEGIN_MESSAGE_MAP(CDouga, CFrameWnd)
	ON_WM_TIMER()
	ON_WM_CONTEXTMENU()
	//{{AFX_MSG_MAP(CDouga)
	ON_WM_SIZING()
	ON_WM_SIZE()
	ON_WM_ENTERSIZEMOVE()
	ON_WM_EXITSIZEMOVE()
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
	ON_COMMAND(ID_DOUGA_PLAY, OnDougaMenuPlay)
	ON_COMMAND(ID_DOUGA_STOP, OnDougaMenuStop)
	ON_COMMAND(ID_DOUGA_PREV, OnDougaMenuPrev)
	ON_COMMAND(ID_DOUGA_NEXT, OnDougaMenuNext)
	ON_COMMAND(ID_DOUGA_REW, OnDougaMenuRew)
	ON_COMMAND(ID_DOUGA_FF, OnDougaMenuFf)
	ON_COMMAND(ID_DOUGA_MUTE, OnDougaMenuMute)
	ON_COMMAND(ID_DOUGA_FS, OnDougaMenuFs)
	ON_COMMAND(ID_DOUGA_FADE, OnDougaMenuFade)
	ON_COMMAND(ID_DOUGA_DSFILTERS, OnDougaMenuDsFilters)
	ON_COMMAND(ID_DOUGA_TOPMOST, OnDougaMenuTopmost)
	ON_COMMAND(ID_DOUGA_ASPECT, OnDougaMenuAspect)
	ON_COMMAND(ID_DOUGA_CLOSE, OnDougaMenuClose)
	ON_COMMAND(ID_DOUGA_SUBOFF, OnDougaMenuSubOff)
	ON_COMMAND_RANGE(ID_DOUGA_SPEED_FIRST, ID_DOUGA_SPEED_LAST, OnDougaMenuSpeed)
	ON_COMMAND(ID_HELP_SHOWSHEET, OnHelpShowSheet)
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
// IAMStreamSelect の絶対インデックス（Start+相対は非連続で壊れる）
int streamidx[40];
int streamidx1[40];
int streamidx2[40];
// LAV の「S: No subtitles」等。メニューには出さずオフ切替用に保持
int streamidxSubOff = -1;

static BOOL DougaIsOffSubtitleName(const CString& nm)
{
	if (nm.IsEmpty()) return FALSE;
	CString low = nm;
	low.MakeLower();
	if (low.Find(L"no subtitle") >= 0) return TRUE;
	if (low.Find(L"字幕なし") >= 0) return TRUE;
	if (low.Find(L"ohne untertitel") >= 0) return TRUE;
	if (low.Find(L"sans sous-titre") >= 0) return TRUE;
	if (low.Find(L"sin subt") >= 0) return TRUE;
	if (low.Find(L"без субтитр") >= 0) return TRUE;
	// "S: Off" / 単独 Off（LAV 系）
	if (low == L"off" || low == L"s: off" || low.Find(L"s: off") == 0) return TRUE;
	return FALSE;
}

extern WCHAR douga[2050];
extern save savedata;
extern int mode;

extern COggDlg *og;
extern void MpTaskbarReplay();
extern void MpTaskbarNextTrack();
extern void MpTaskbarPrevTrack();

BOOL CDouga::Create(HWND h)
{
	CString sClassName;
	sClassName = AfxRegisterWndClass(NULL ,
    LoadCursor(NULL, IDC_ARROW),
    (HBRUSH)::GetStockObject(BLACK_BRUSH),
    LoadIcon(AfxGetInstanceHandle(),
    MAKEINTRESOURCE(IDR_DOUGA)));

    int ret=CreateEx(WS_EX_OVERLAPPEDWINDOW|WS_EX_ACCEPTFILES,sClassName, LL14(L"メディアプレイヤーらいら✡動画画面", L"Media Player Raira ✡ Video Screen", L"Lecteur multimédia Raira ✡ Écran vidéo", L"Lettore multimediale Raira ✡ Schermata video", L"Reproductor multimedia Raira ✡ Pantalla de vídeo", L"미디어 플레이어 라이라 ✡ 동영상 화면", L"媒体播放器莱拉 ✡ 视频画面", L"مشغل الوسائط رايرا ✡ شاشة الفيديو", L"Медиаплеер Райра ✡ Экран видео", L"Mediaplayer Raira ✡ Videobildschirm", L"Reprodutor multimídia Raira ✡ Tela de vídeo", L"Mediaspeler Raira ✡ Videoscherm", L"Odtwarzacz multimedialny Raira ✡ Ekran wideo", L"Medya Oynatıcı Raira ✡ Video Ekranı"),
	  ((WS_OVERLAPPEDWINDOW)& ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX),
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

	// 動画サイト(EVR/VideoWindow用)と下部バー — 兄弟HWNDで重ね順競合を避ける
	{
		CString vcls = AfxRegisterWndClass(CS_DBLCLKS,
			::LoadCursor(NULL, IDC_ARROW),
			(HBRUSH)::GetStockObject(BLACK_BRUSH),
			NULL);
		m_videoSite.Create(vcls, _T(""),
			WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
			CRect(0, 0, 1, 1), this, IDC_DOUGA_VIDEOSITE);
		m_bar.CreateBar(this);
		ApplyVideoDest();
	}
	ApplyDougaTopmost();
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
	FILTER_INFO filinfo = {};
	pFilter->QueryFilterInfo(&filinfo);
	if (filinfo.pGraph) { filinfo.pGraph->Release(); filinfo.pGraph = NULL; }
	while(e->Next(1, &pResult, NULL) == S_OK){
		PIN_DIRECTION PinDirThis;
		hr=pResult->QueryDirection(&PinDirThis);
		if (pResult!=NULL && SUCCEEDED(hr) && PinDirThis==PINDIR_OUTPUT){
			PIN_INFO info = {};
			pResult->QueryPinInfo(&info);
			if (info.pFilter) { info.pFilter->Release(); info.pFilter = NULL; }
			{
				IEnumMediaTypes *em=NULL;
				AM_MEDIA_TYPE *amt;
				hr=pResult->EnumMediaTypes(&em);
				if(SUCCEEDED(hr)){
					while(em->Next(1,&amt,NULL)==S_OK){
						GUID mj=amt->majortype;
						(void)mj;
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

	streamMap.videoStart = -1;
	streamMap.videoCount = 0;
	streamMap.audioStart = -1;
	streamMap.audioCount = 0;
	streamMap.subtitleStart = -1;
	streamMap.subtitleCount = 0;
	streamidxSubOff = -1;

	for (int j = 0; j < 40; j++) {
		streamname[j] = L"";
		streamname1[j] = L"";
		streamname2[j] = L"";
		streamidx[j] = -1;
		streamidx1[j] = -1;
		streamidx2[j] = -1;
	}

	if (FAILED(pFilter->Count(&totalCount)))
		return 0;

	int videoIdx = 0, audioIdx = 0, subtitleIdx = 0;

	for (i = 0; i < totalCount; i++) {
		am = NULL;
		p = NULL;
		DWORD flags = 0, group = 0;
		LCID lcid = 0;
		const HRESULT ihr = pFilter->Info(i, &am, &flags, &lcid, &group, &p, NULL, NULL);
		if (FAILED(ihr)) {
			if (p) CoTaskMemFree(p);
			if (am) DeleteMediaType(am);
			continue;
		}

		CString nm;
		if (p && p[0])
			nm = p;

		BOOL isAudio = (am && am->majortype == MEDIATYPE_Audio);
		BOOL isVideo = (am && am->majortype == MEDIATYPE_Video);
		BOOL isSub = FALSE;
		if (am && (am->majortype == MEDIATYPE_Subtitle || am->majortype == MEDIATYPE_Subtitle_MPC
			|| am->majortype == MEDIATYPE_Text))
			isSub = TRUE;
		// LAV: GetStreamType が group に入る（0=video 1=audio 2=subpic）
		if (group == 2)
			isSub = TRUE;
		if (!nm.IsEmpty()) {
			CString low = nm;
			low.MakeLower();
			if (low.Find(L"subtitle") >= 0 || low.Find(L"subpic") >= 0 || low.Find(L"字幕") >= 0
				|| low.Find(L"pgs") >= 0 || low.Find(L"vobsub") >= 0 || low.Find(L"sami") >= 0
				|| low.Find(L"(ass)") >= 0 || low.Find(L"[ass]") >= 0
				|| low.Find(L"(ssa)") >= 0 || low.Find(L"[ssa]") >= 0
				|| low.Find(L".sup") >= 0 || low.Find(L"hdmv") >= 0
				|| low.Find(L"softsub") >= 0 || low.Find(L"xsub") >= 0)
				isSub = TRUE;
		}
		if (isSub) {
			isAudio = FALSE;
			isVideo = FALSE;
		} else if (!am) {
			// majortype 欠落時: group0 を安易に映像にしない（字幕が消える原因だった）
			if (group == 1)
				isAudio = TRUE;
			else if (group == 2)
				isSub = TRUE;
			else {
				CString low = nm;
				low.MakeLower();
				if (low.Find(L"video") >= 0 || low.Find(L"映像") >= 0)
					isVideo = TRUE;
				else if (low.Find(L"audio") >= 0 || low.Find(L"音声") >= 0 || low.Find(L"音軌") >= 0)
					isAudio = TRUE;
				else
					isSub = TRUE;
			}
		} else if (!isAudio && !isVideo) {
			isSub = TRUE;
		}

		if (isAudio) {
			if (streamMap.audioStart == -1) streamMap.audioStart = (int)i;
			streamMap.audioCount++;
			if (audioIdx < 40) {
				if (!nm.IsEmpty())
					streamname[audioIdx] = nm;
				else
					streamname[audioIdx].Format(L"%s %d",
						LL14(L"音声", L"Audio", L"Audio", L"Audio", L"Audio", L"오디오", L"音频", L"صوت",
							L"Аудио", L"Audio", L"Áudio", L"Audio", L"Audio", L"Ses"),
						audioIdx + 1);
				streamidx[audioIdx] = (int)i;
				audioIdx++;
			}
		} else if (isVideo) {
			if (streamMap.videoStart == -1) streamMap.videoStart = (int)i;
			streamMap.videoCount++;
			if (videoIdx < 40) {
				if (!nm.IsEmpty())
					streamname1[videoIdx] = nm;
				else
					streamname1[videoIdx].Format(L"%s %d",
						LL14(L"映像", L"Video", L"Vidéo", L"Video", L"Vídeo", L"비디오", L"视频", L"فيديو",
							L"Видео", L"Video", L"Vídeo", L"Video", L"Wideo", L"Video"),
						videoIdx + 1);
				streamidx1[videoIdx] = (int)i;
				videoIdx++;
			}
		} else if (isSub) {
			// LAV の「No subtitles」はオフ専用（一覧に出さない＝メニュー二重化防止）
			if (DougaIsOffSubtitleName(nm)) {
				streamidxSubOff = (int)i;
			} else {
				if (streamMap.subtitleStart == -1) streamMap.subtitleStart = (int)i;
				streamMap.subtitleCount++;
				if (subtitleIdx < 40) {
					if (!nm.IsEmpty())
						streamname2[subtitleIdx] = nm;
					else
						streamname2[subtitleIdx].Format(L"%s %d",
							LL14(L"字幕", L"Subtitle", L"Sous-titres", L"Sottotitoli", L"Subtítulos", L"자막", L"字幕", L"ترجمة",
								L"Субтитры", L"Untertitel", L"Legendas", L"Ondertitel", L"Napisy", L"Altyazı"),
							subtitleIdx + 1);
					streamidx2[subtitleIdx] = (int)i;
					subtitleIdx++;
				}
			}
		}

		if (p) {
			CoTaskMemFree(p);
			p = NULL;
		}
		if (am) {
			DeleteMediaType(am);
			am = NULL;
		}
	}

	au = streamMap.audioStart;
	etc = streamMap.subtitleStart;
	audionum = streamMap.audioCount;

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
	pPin = NULL;
	if(pFilter)
		hr=pFilter->EnumPins(&e);
	if(FAILED(hr) || pFilter==NULL)
		return hr;
	FILTER_INFO filinfo = {};
	pFilter->QueryFilterInfo(&filinfo);
	if (filinfo.pGraph) { filinfo.pGraph->Release(); filinfo.pGraph = NULL; }
	while(e->Next(1, &pResult, NULL) == S_OK){
		PIN_DIRECTION PinDirThis;
		hr=pResult->QueryDirection(&PinDirThis);
		if (pResult!=NULL && SUCCEEDED(hr) && PinDirThis==dir){
			PIN_INFO info = {};
			pResult->QueryPinInfo(&info);
			if (info.pFilter) { info.pFilter->Release(); info.pFilter = NULL; }
			if(dir==PINDIR_INPUT){
				pPin=pResult;
				pResult = NULL; // ownership to caller
				retCode=S_OK;
			}else{
				IEnumMediaTypes *em=NULL;
				AM_MEDIA_TYPE *amt=NULL;
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
						if(Haali==NULL || name==NULL){
							if(mj==majorType){
								pPin=pResult;
								pResult = NULL;
								retCode=S_OK;
								break;
							}
						}else{
							if(mj==majorType && _wcsnicmp(info.achName,name,14)==0){
								pPin=pResult;
								pResult = NULL;
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
extern int wavchannel,wavbit_sample_Hz;
CString s2;
#if WIN64
#else
//static const GUID MR_VIDEO_RENDER_SERVICE =     {0x1092a86c, 0xab1a, 0x459a, {0xa3, 0x36, 0x83, 0x1f, 0xbc, 0x4d, 0x11, 0xff} };
//static const IID IID_IMFVideoDisplayControl =   {0xa490b1e4, 0xab84, 0x4d31, {0xa1, 0xb2, 0x18, 0x1e, 0x03, 0xb1, 0x07, 0x7a} };
#endif

double CDouga::GetFrameRate(IGraphBuilder* pGraph)
{
	double frameRate = 0.0;
	if (!pGraph) return 0.0;

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
						AM_MEDIA_TYPE mt;
						ZeroMemory(&mt, sizeof(mt));
						if (SUCCEEDED(pPin->ConnectionMediaType(&mt)))
						{
							if (mt.majortype == MEDIATYPE_Video && mt.pbFormat)
							{
								if (mt.formattype == FORMAT_VideoInfo)
								{
									VIDEOINFOHEADER* pVih = (VIDEOINFOHEADER*)mt.pbFormat;
									if (pVih->AvgTimePerFrame > 0)
										frameRate = 10000000.0 / pVih->AvgTimePerFrame;
								}
								else if (mt.formattype == FORMAT_VideoInfo2)
								{
									VIDEOINFOHEADER2* pVih2 = (VIDEOINFOHEADER2*)mt.pbFormat;
									if (pVih2->AvgTimePerFrame > 0)
										frameRate = 10000000.0 / pVih2->AvgTimePerFrame;
								}
							}
							FreeMediaType(mt);
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

	// MediaDet の put_Filename は前グラフがファイルロック中だと UI フリーズする。使わない。
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
			info.majorType = GUID_NULL;
			if (pmt) info.majorType = pmt->majortype;
			if (pszName) info.name = pszName;

			if (lcid != 0)
			{
				WCHAR langName[256];
				if (GetLocaleInfo(lcid, LOCALE_SENGLANGUAGE, langName, 256) > 0)
					info.language = langName;
			}

			BOOL isAudio = (pmt && pmt->majortype == MEDIATYPE_Audio);
			BOOL isVideo = (pmt && pmt->majortype == MEDIATYPE_Video);
			BOOL isSub = FALSE;
			if (pmt && (pmt->majortype == MEDIATYPE_Subtitle || pmt->majortype == MEDIATYPE_Subtitle_MPC
				|| pmt->majortype == MEDIATYPE_Text))
				isSub = TRUE;
			if (group == 2) isSub = TRUE;
			if (!info.name.IsEmpty()) {
				CString low = info.name;
				low.MakeLower();
				if (low.Find(L"subtitle") >= 0 || low.Find(L"subpic") >= 0 || low.Find(L"字幕") >= 0
					|| low.Find(L"pgs") >= 0 || low.Find(L"vobsub") >= 0
					|| low.Find(L"(ass)") >= 0 || low.Find(L"[ass]") >= 0
					|| low.Find(L"(ssa)") >= 0 || low.Find(L"[ssa]") >= 0)
					isSub = TRUE;
			}
			if (isSub) { isAudio = FALSE; isVideo = FALSE; }
			else if (!pmt) {
				if (group == 1) isAudio = TRUE;
				else if (group == 2) isSub = TRUE;
				else isSub = TRUE;
			} else if (!isAudio && !isVideo) {
				isSub = TRUE;
			}

			if (isAudio) audioStreams.push_back(info);
			else if (isVideo) videoStreams.push_back(info);
			else if (isSub) {
				if (DougaIsOffSubtitleName(info.name))
					streamidxSubOff = (int)i;
				else
					subtitleStreams.push_back(info);
			}

			if (pszName) CoTaskMemFree(pszName);
			if (pObject) pObject->Release();
			if (pUnknown) pUnknown->Release();
			if (pmt) DeleteMediaType(pmt);
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
{ 0x9852A670, 0xF845, 0x491B, { 0x9B, 0xE6, 0xEB, 0xD8, 0x41, 0xB8, 0xA6, 0x13 } }; // DirectVobSub (auto-loading)
static const GUID CLSID_DirectVobSubNormal =
{ 0x93A22E7A, 0x5091, 0x45EF, { 0xBA, 0x61, 0x6D, 0xA2, 0x61, 0x56, 0xA5, 0xD0 } };

// ffdshow remote API（字幕フィルタ ON）
static const GUID IID_IffdshowBaseW =
{ 0xFC5BCCF4, 0xFD62, 0x45EE, { 0xB0, 0x22, 0x38, 0x40, 0xEA, 0xEA, 0x77, 0xB2 } };
enum {
	kIdff_isSubtitles = 801,
	kIdff_showSubtitles = 828,
	kIdff_subTextpin = 845,
	kIdff_subText = 3547,
	kIdff_subSSA = 861,
	kIdff_subPGS = 3545,
	kIdff_subFiles = 3546,
	kIdff_subDelay = 812 // ms（正で字幕を遅らせる＝遅延音声に合わせる）
};
MIDL_INTERFACE("FC5BCCF4-FD62-45ee-B022-3840EAEA77B2")
IffdshowBaseWMin : public IUnknown
{
public:
	STDMETHOD_(int, getVersion2)(void) PURE;
	STDMETHOD(getParam)(unsigned int paramID, int* value) PURE;
	STDMETHOD_(int, getParam2)(unsigned int paramID) PURE;
	STDMETHOD(putParam)(unsigned int paramID, int value) PURE;
};

static const GUID CLSID_ffdshowRawVideo =
{ 0x0B390488, 0xD80F, 0x4A68, { 0x84, 0x08, 0x48, 0xDC, 0x19, 0x9F, 0x0E, 0x97 } };
static const GUID CLSID_ffdshowVideoDecoder =
{ 0x04FE9017, 0xF873, 0x410E, { 0x87, 0x1E, 0xAB, 0x91, 0x66, 0x1A, 0x4E, 0xF7 } };
// 汚れの主因: IC が ffdshow Audio を拾うと有↔無を跨いで残る
static const GUID CLSID_ffdshowAudioDecoder =
{ 0x0F40E1E5, 0x4F79, 0x4988, { 0xB1, 0xA9, 0xCC, 0x98, 0x79, 0x4E, 0x6B, 0x55 } };
static const GUID CLSID_ffdshowAudioRaw =
{ 0xB86F6BEE, 0xE7C0, 0x4D03, { 0x8D, 0x52, 0x5B, 0x44, 0x30, 0xCF, 0x6C, 0x88 } };

static void DougaPrefFfdshowSubsReg(BOOL enable)
{
	HKEY hk = NULL;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\GNU\\ffdshow_raw\\default",
		0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
		return;
	DWORD v = enable ? 1u : 0u;
	RegSetValueExW(hk, L"isSubtitles", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
	RegSetValueExW(hk, L"showSubtitles", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
	RegSetValueExW(hk, L"subTextpin", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
	RegSetValueExW(hk, L"subText", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
	RegSetValueExW(hk, L"subSSA", 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
	RegCloseKey(hk);
}

// Intelligent Connect だけ遮断。CoCreateInstance（Ensure）は通る。
static void DougaSetClsidMeritHKCU(REFCLSID clsid, DWORD merit)
{
	WCHAR guid[64] = {};
	if (!StringFromGUID2(clsid, guid, 64))
		return;
	WCHAR key[160] = {};
	wsprintfW(key, L"Software\\Classes\\CLSID\\%s", guid);
	HKEY hk = NULL;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
		return;
	RegSetValueExW(hk, L"Merit", 0, REG_DWORD, (const BYTE*)&merit, sizeof(merit));
	RegCloseKey(hk);
}

static void DougaBlockFfdshowIntelligentConnect(BOOL block)
{
	const DWORD m = block ? (DWORD)MERIT_DO_NOT_USE : (DWORD)MERIT_NORMAL;
	DougaSetClsidMeritHKCU(CLSID_ffdshowRawVideo, m);
	DougaSetClsidMeritHKCU(CLSID_ffdshowVideoDecoder, m);
	DougaSetClsidMeritHKCU(CLSID_ffdshowAudioDecoder, m);
	DougaSetClsidMeritHKCU(CLSID_ffdshowAudioRaw, m);
}

// RenderFile の IC が ffdshow raw/Video に入って固まるのを防ぐ。
// BindToStorage は使わない（汚れた ffd でそこが固まる）。Ensure の CoCreate は別経路。
class CDougaRejectFfdCb : public IAMGraphBuilderCallback
{
	LONG m_cRef;
public:
	CDougaRejectFfdCb() : m_cRef(1) {}
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		*ppv = NULL;
		if (riid == IID_IUnknown || riid == IID_IAMGraphBuilderCallback) {
			*ppv = static_cast<IAMGraphBuilderCallback*>(this);
			AddRef();
			return S_OK;
		}
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&m_cRef); }
	STDMETHODIMP_(ULONG) Release()
	{
		LONG c = InterlockedDecrement(&m_cRef);
		if (c < 1) InterlockedExchange(&m_cRef, 1);
		return (ULONG)(c < 1 ? 1 : c);
	}
	STDMETHODIMP SelectedFilter(IMoniker* pMon)
	{
		if (!pMon) return S_OK;
		IBindCtx* bc = NULL;
		if (FAILED(CreateBindCtx(0, &bc)) || !bc) return S_OK;
		LPOLESTR dn = NULL;
		const HRESULT hr = pMon->GetDisplayName(bc, NULL, &dn);
		bc->Release();
		if (FAILED(hr) || !dn) return S_OK;
		CString s(dn);
		CoTaskMemFree(dn);
		s.MakeUpper();
		// raw / Video / Audio / audio raw — いずれも IC 拒否（Audio 残留が有→無→有フリーズの汚れ）
		if (s.Find(L"0B390488") >= 0 || s.Find(L"04FE9017") >= 0
			|| s.Find(L"0F40E1E5") >= 0 || s.Find(L"B86F6BEE") >= 0)
			return E_FAIL;
		return S_OK;
	}
	STDMETHODIMP CreatedFilter(IBaseFilter* pFil)
	{
		if (!pFil) return S_OK;
		CLSID clsid = CLSID_NULL;
		if (SUCCEEDED(pFil->GetClassID(&clsid))) {
			if (clsid == CLSID_ffdshowRawVideo || clsid == CLSID_ffdshowVideoDecoder
				|| clsid == CLSID_ffdshowAudioDecoder || clsid == CLSID_ffdshowAudioRaw)
				return E_FAIL;
		}
		FILTER_INFO fi = {};
		if (SUCCEEDED(pFil->QueryFilterInfo(&fi))) {
			CString name = fi.achName;
			if (fi.pGraph) fi.pGraph->Release();
			if (name.Find(L"ffdshow raw") >= 0 || name.Find(L"ffdshow Video") >= 0
				|| name.Find(L"ffdshow Audio") >= 0 || name.Find(L"ffdshow audio") >= 0)
				return E_FAIL;
		}
		return S_OK;
	}
};

static CDougaRejectFfdCb s_dougaRejectFfdCb;

static void DougaInstallRejectFfdCallback(IGraphBuilder* g)
{
	if (!g) return;
	IObjectWithSite* ows = NULL;
	if (FAILED(g->QueryInterface(IID_IObjectWithSite, (void**)&ows)) || !ows)
		return;
	ows->SetSite(static_cast<IAMGraphBuilderCallback*>(&s_dougaRejectFfdCb));
	ows->Release();
}

static void DougaClearRejectFfdCallback(IGraphBuilder* g)
{
	if (!g) return;
	IObjectWithSite* ows = NULL;
	if (FAILED(g->QueryInterface(IID_IObjectWithSite, (void**)&ows)) || !ows)
		return;
	ows->SetSite(NULL);
	ows->Release();
}

static void DougaEnableFfdshowSubs(IBaseFilter* pFfd)
{
	if (!pFfd) return;
	IffdshowBaseWMin* ff = NULL;
	if (FAILED(pFfd->QueryInterface(IID_IffdshowBaseW, (void**)&ff)) || !ff) {
		OutputDebugString(L"ffdshow: IffdshowBaseW QI failed\n");
		return;
	}
	ff->putParam(kIdff_isSubtitles, 1);
	ff->putParam(kIdff_showSubtitles, 1);
	ff->putParam(kIdff_subTextpin, 1);
	ff->putParam(kIdff_subText, 1);
	ff->putParam(kIdff_subSSA, 1);
	ff->putParam(kIdff_subPGS, 1);
	ff->putParam(kIdff_subFiles, 1);
	const int subDelayMs = DougaPitchCorrect_IsActive()
		? DougaPitchCorrect_GetLatencyMs() : 0;
	ff->putParam(kIdff_subDelay, subDelayMs);
	ff->Release();
	// Pref は触らない。TRUE を残すと次の字幕なし RenderFile に ffdshow が巻き込まれて落ちる。
}

static BOOL DougaNameIsVsFilter(const CString& name)
{
	return name.Find(L"DirectVobSub") >= 0 || name.Find(L"xy-VSFilter") >= 0
		|| name.Find(L"XySubFilter") >= 0 || name.Find(L"VSFilter") >= 0;
}

static BOOL DougaNameIsSplitterish(const CString& name)
{
	if (name.Find(L"Splitter") >= 0) return TRUE;
	if (name.Find(L":\\") >= 0 || name.Find(L"\\\\") >= 0) return TRUE;
	CString low = name; low.MakeLower();
	return low.Find(L".mp4") >= 0 || low.Find(L".mkv") >= 0 || low.Find(L".avi") >= 0
		|| low.Find(L".m2ts") >= 0 || low.Find(L".ts") >= 0 || low.Find(L".mov") >= 0
		|| low.Find(L".wmv") >= 0 || low.Find(L".webm") >= 0;
}

// IDirectVobSub（字幕表示の強制オン）
MIDL_INTERFACE("EBE1FB08-3957-47ca-9B81-00E15EC75872")
IDirectVobSub : public IUnknown
{
public:
	STDMETHOD(get_FileName)(THIS_ BSTR* fn) PURE;
	STDMETHOD(put_FileName)(THIS_ BSTR fn) PURE;
	STDMETHOD(get_LanguageCount)(THIS_ int* nLangs) PURE;
	STDMETHOD(get_LanguageName)(THIS_ int iLanguage, BSTR* ppName) PURE;
	STDMETHOD(get_SelectedLanguage)(THIS_ int* iSelected) PURE;
	STDMETHOD(put_SelectedLanguage)(THIS_ int iSelected) PURE;
	STDMETHOD(get_HideSubtitles)(THIS_ bool* fHideSubtitles) PURE;
	STDMETHOD(put_HideSubtitles)(THIS_ bool fHideSubtitles) PURE;
};

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

					ZeroMemory(&conn.mt, sizeof(conn.mt));
					if (SUCCEEDED(pPin->ConnectionMediaType(&conn.mt)))
					{
						// conn.mt にコピー済み。Free は connections 破棄時
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

static IPin* DougaFirstPin(IBaseFilter* f, PIN_DIRECTION want)
{
	if (!f) return NULL;
	IEnumPins* ep = NULL;
	if (FAILED(f->EnumPins(&ep)) || !ep) return NULL;
	IPin* pin = NULL;
	IPin* found = NULL;
	while (ep->Next(1, &pin, NULL) == S_OK) {
		PIN_DIRECTION d = PINDIR_INPUT;
		pin->QueryDirection(&d);
		if (d == want) { found = pin; break; }
		pin->Release();
	}
	ep->Release();
	return found;
}

static IPin* DougaFfdVideoIn(IBaseFilter* f)
{
	if (!f) return NULL;
	IEnumPins* ep = NULL;
	if (FAILED(f->EnumPins(&ep)) || !ep) return NULL;
	IPin* pin = NULL;
	IPin* found = NULL;
	while (ep->Next(1, &pin, NULL) == S_OK) {
		PIN_DIRECTION d = PINDIR_OUTPUT;
		pin->QueryDirection(&d);
		PIN_INFO pi; pi.pFilter = NULL;
		pin->QueryPinInfo(&pi);
		CString low = pi.achName; low.MakeLower();
		if (pi.pFilter) pi.pFilter->Release();
		if (d == PINDIR_INPUT && low.Find(L"text") < 0) { found = pin; break; }
		pin->Release();
	}
	ep->Release();
	return found;
}

static BOOL DougaPinPeerHas(IPin* p, LPCWSTR needle)
{
	if (!p || !needle) return FALSE;
	IPin* peer = NULL;
	if (p->ConnectedTo(&peer) != S_OK || !peer) return FALSE;
	PIN_INFO pi; pi.pFilter = NULL;
	peer->QueryPinInfo(&pi);
	peer->Release();
	BOOL hit = FALSE;
	if (pi.pFilter) {
		FILTER_INFO fi;
		pi.pFilter->QueryFilterInfo(&fi);
		CString n = fi.achName;
		if (fi.pGraph) fi.pGraph->Release();
		hit = (n.Find(needle) >= 0);
		pi.pFilter->Release();
	}
	return hit;
}

static void DougaPinDisconnectPair(IPin* p)
{
	if (!p) return;
	IPin* o = NULL;
	if (p->ConnectedTo(&o) == S_OK) {
		p->Disconnect();
		o->Disconnect();
		o->Release();
	}
}

static void DougaDisconnectAllPins(IBaseFilter* f)
{
	if (!f) return;
	IEnumPins* ep = NULL;
	if (FAILED(f->EnumPins(&ep)) || !ep) return;
	IPin* pin = NULL;
	while (ep->Next(1, &pin, NULL) == S_OK) {
		DougaPinDisconnectPair(pin);
		pin->Release();
	}
	ep->Release();
}

static void DougaHangTrace(LPCWSTR stage);

// RenderFile 直後のクリーンな Dec→Ren 接続を保存し、Ensure / Peel で再利用する
struct DougaVideoPinMap {
	BOOL valid;
	AM_MEDIA_TYPE mt;
	WCHAR decName[128];
	WCHAR renName[128];
};
static DougaVideoPinMap s_dougaVidMap = {};

static void DougaClearVideoPinMap()
{
	if (s_dougaVidMap.valid)
		FreeMediaType(s_dougaVidMap.mt);
	ZeroMemory(&s_dougaVidMap, sizeof(s_dougaVidMap));
}

static void DougaCaptureVideoPinMap(IGraphBuilder* g)
{
	DougaClearVideoPinMap();
	if (!g) return;

	IBaseFilter* pRen = NULL;
	IEnumFilters* pEnum = NULL;
	if (FAILED(g->EnumFilters(&pEnum)) || !pEnum) return;
	IBaseFilter* pF = NULL;
	ULONG n = 0;
	while (pEnum->Next(1, &pF, &n) == S_OK) {
		FILTER_INFO fi;
		pF->QueryFilterInfo(&fi);
		CString name = fi.achName;
		if (fi.pGraph) fi.pGraph->Release();
		if (name.Find(L"Enhanced Video Renderer") >= 0
			|| name.Find(L"Video Mixing Renderer") >= 0
			|| name.Find(L"Video Renderer") == 0) {
			if (pRen) pRen->Release();
			pRen = pF; pRen->AddRef();
		}
		// 全フィルタの接続を短くログ（クリーン化検証用）
		{
			IEnumPins* ep = NULL;
			if (SUCCEEDED(pF->EnumPins(&ep)) && ep) {
				IPin* pin = NULL;
				while (ep->Next(1, &pin, NULL) == S_OK) {
					PIN_DIRECTION d = PINDIR_INPUT;
					pin->QueryDirection(&d);
					if (d == PINDIR_OUTPUT) {
						IPin* peer = NULL;
						if (pin->ConnectedTo(&peer) == S_OK && peer) {
							PIN_INFO pi; pi.pFilter = NULL;
							peer->QueryPinInfo(&pi);
							CString peerName = L"?";
							if (pi.pFilter) {
								FILTER_INFO pfi;
								pi.pFilter->QueryFilterInfo(&pfi);
								peerName = pfi.achName;
								if (pfi.pGraph) pfi.pGraph->Release();
								pi.pFilter->Release();
							}
							CString line;
							line.Format(L"PinMap:%s -> %s", (LPCWSTR)name, (LPCWSTR)peerName);
							DougaHangTrace((LPCWSTR)line);
							peer->Release();
						}
					}
					pin->Release();
				}
				ep->Release();
			}
		}
		pF->Release();
	}
	pEnum->Release();

	if (!pRen) {
		DougaHangTrace(L"PinMap:no renderer");
		return;
	}

	IPin* rIn = DougaFirstPin(pRen, PINDIR_INPUT);
	IPin* up = NULL;
	if (rIn && rIn->ConnectedTo(&up) == S_OK && up) {
		PIN_INFO pi; pi.pFilter = NULL;
		up->QueryPinInfo(&pi);
		if (pi.pFilter) {
			FILTER_INFO fi;
			pi.pFilter->QueryFilterInfo(&fi);
			wcsncpy_s(s_dougaVidMap.decName, _countof(s_dougaVidMap.decName), fi.achName, _TRUNCATE);
			if (fi.pGraph) fi.pGraph->Release();
			pi.pFilter->Release();
		}
		{
			FILTER_INFO rfi;
			pRen->QueryFilterInfo(&rfi);
			wcsncpy_s(s_dougaVidMap.renName, _countof(s_dougaVidMap.renName), rfi.achName, _TRUNCATE);
			if (rfi.pGraph) rfi.pGraph->Release();
		}
		ZeroMemory(&s_dougaVidMap.mt, sizeof(s_dougaVidMap.mt));
		if (SUCCEEDED(up->ConnectionMediaType(&s_dougaVidMap.mt))) {
			s_dougaVidMap.valid = TRUE;
			CString msg;
			msg.Format(L"PinMap:captured %s -> %s (mt ok)",
				s_dougaVidMap.decName, s_dougaVidMap.renName);
			DougaHangTrace((LPCWSTR)msg);
		} else {
			DougaHangTrace(L"PinMap:ConnectionMediaType FAILED");
		}
		up->Release();
	} else {
		DougaHangTrace(L"PinMap:renderer input not connected");
	}
	if (rIn) rIn->Release();
	pRen->Release();
}

// フリーズ調査用ログ。exe の隣に dougatrace.txt を置いた時だけ有効になり、
// 同じ場所の dougatrace.log へ 1 行ごとに追記＋即クローズする。
// 固まっても直前までの行が残るので、停止位置をそのまま特定できる。
static BOOL DougaHangTraceEnabled(WCHAR* outLogPath)
{
	static int cached = -1;
	static WCHAR s_log[MAX_PATH] = {};
	if (cached < 0) {
		cached = 0;
		WCHAR path[MAX_PATH] = {};
		if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
			WCHAR* sep = wcsrchr(path, L'\\');
			if (sep) {
				const size_t room = MAX_PATH - (size_t)(sep + 1 - path);
				wcscpy_s(sep + 1, room, L"dougatrace.txt");
				if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
					wcscpy_s(sep + 1, room, L"dougatrace.log");
					wcscpy_s(s_log, MAX_PATH, path);
					cached = 1;
				}
			}
		}
	}
	if (cached && outLogPath)
		wcscpy_s(outLogPath, MAX_PATH, s_log);
	return cached ? TRUE : FALSE;
}

static void DougaHangTrace(LPCWSTR stage)
{
	WCHAR log[MAX_PATH];
	if (!stage || !DougaHangTraceEnabled(log))
		return;

	SYSTEMTIME st;
	GetLocalTime(&st);
	char line[1024];
	char utf8[512];
	if (WideCharToMultiByte(CP_UTF8, 0, stage, -1, utf8, sizeof(utf8), NULL, NULL) <= 0)
		return;
	const int n = _snprintf_s(line, sizeof(line), _TRUNCATE,
		"%02u:%02u:%02u.%03u t=%lu tid=%lu %s\r\n",
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		(unsigned long)GetTickCount(), (unsigned long)GetCurrentThreadId(), utf8);
	if (n <= 0)
		return;

	HANDLE h = CreateFileW(log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return;
	DWORD wrote = 0;
	WriteFile(h, line, (DWORD)n, &wrote, NULL);
	FlushFileBuffers(h);
	CloseHandle(h);
}

static void DougaPumpSleep(DWORD ms)
{
	const DWORD t0 = GetTickCount();
	for (;;) {
		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		const DWORD elapsed = GetTickCount() - t0;
		if (elapsed >= ms) break;
		MsgWaitForMultipleObjects(0, NULL, FALSE, ms - elapsed, QS_ALLINPUT);
	}
}

static LONG s_dougaSessionUsedFfd = 0;
static LONG s_dougaAvoidFlush = 0; // ffd 直後の Flush を避ける（次の字幕つき Run が固まる）
static LONG s_dougaPlayRoute = 0; // 0=none / 1=NO-SUB / 2=WITH-SUB
static int s_dougaDeferSubStreamIdx = -1;
static BOOL s_dougaDeferSubConnect = FALSE; // InText も Run 後（Run 前接続が無→有で固まる）
static LONG s_dougaNeedComRecycle = 0;
static LONG s_dougaOurCoInit = 0;

static void DougaFlushLeftoverFilterModules()
{
	if (InterlockedCompareExchange(&s_dougaAvoidFlush, 0, 0) > 0) {
		InterlockedDecrement(&s_dougaAvoidFlush);
		DougaHangTrace(L"FlushModules:skipped (post-ffd)");
		return;
	}
	DougaHangTrace(L"FlushModules:begin");
	for (int i = 0; i < 6; ++i) {
		CoFreeUnusedLibraries();
		DougaPumpSleep(30);
	}
	DougaHangTrace(L"FlushModules:done");
}

// 字幕ありセッション後に COM を起動直後相当へ戻す（別スレッド RenderFile は STA 違反でデッドロック）
static void DougaRecycleComApartmentIfNeeded()
{
	if (InterlockedExchange(&s_dougaNeedComRecycle, 0) == 0)
		return;
	// 無効化: CoFreeUnusedLibraries 後の RenderFile が固まる
	DougaHangTrace(L"ComRecycle:skipped");
}

static void DougaEnsureComInitialized()
{
	HRESULT hr = CoInitialize(NULL);
	if (hr == S_OK || hr == S_FALSE) {
		InterlockedIncrement(&s_dougaOurCoInit);
		DougaHangTrace(hr == S_OK ? L"CoInitialize:S_OK" : L"CoInitialize:S_FALSE");
	}
}

static void DougaNukeGraphFilters(IGraphBuilder* g)
{
	if (!g) return;
	// Stop 後は DisconnectAllPins しない（ffd を切ると次の字幕再生で固まる）
	for (int pass = 0; pass < 16; ++pass) {
		IBaseFilter* list[64];
		int n = 0;
		IEnumFilters* e = NULL;
		if (FAILED(g->EnumFilters(&e)) || !e) break;
		IBaseFilter* f = NULL;
		ULONG c = 0;
		while (n < 64 && e->Next(1, &f, &c) == S_OK)
			list[n++] = f;
		e->Release();
		if (n == 0) break;
		for (int i = 0; i < n; ++i) {
			g->RemoveFilter(list[i]);
			list[i]->Release();
		}
	}
}

// Stop 前に ffdshow を外す。再接続成功で TRUE。失敗時は呼び出し側で Stop せず Nuke する。
static BOOL DougaPeelFfdBeforeStop(IGraphBuilder* pGraph)
{
	if (!pGraph) return TRUE;
	DougaHangTrace(L"PeelFfd:begin");

	IBaseFilter* pFfd = NULL;
	IBaseFilter* pRen = NULL;
	IEnumFilters* pEnum = NULL;
	if (SUCCEEDED(pGraph->EnumFilters(&pEnum)) && pEnum) {
		IBaseFilter* pF = NULL;
		ULONG n = 0;
		while (pEnum->Next(1, &pF, &n) == S_OK) {
			FILTER_INFO fi;
			pF->QueryFilterInfo(&fi);
			CString name = fi.achName;
			if (fi.pGraph) fi.pGraph->Release();
			if (name.Find(L"ffdshow raw") >= 0 || name.Find(L"ffdshow Video") >= 0) {
				if (pFfd) pFfd->Release();
				pFfd = pF; pFfd->AddRef();
			}
			if (name.Find(L"Enhanced Video Renderer") >= 0
				|| name.Find(L"Video Mixing Renderer") >= 0
				|| name.Find(L"Video Renderer") == 0) {
				if (pRen) pRen->Release();
				pRen = pF; pRen->AddRef();
			}
			pF->Release();
		}
		pEnum->Release();
	}
	if (!pFfd) {
		DougaHangTrace(L"PeelFfd:no ffd");
		if (pRen) pRen->Release();
		return TRUE;
	}

	// InText 切断
	{
		IEnumPins* ep = NULL;
		if (SUCCEEDED(pFfd->EnumPins(&ep)) && ep) {
			IPin* pin = NULL;
			while (ep->Next(1, &pin, NULL) == S_OK) {
				PIN_DIRECTION d = PINDIR_OUTPUT;
				pin->QueryDirection(&d);
				if (d == PINDIR_INPUT) {
					PIN_INFO pi; pi.pFilter = NULL;
					pin->QueryPinInfo(&pi);
					CString low = pi.achName; low.MakeLower();
					if (pi.pFilter) pi.pFilter->Release();
					if (low.Find(L"text") >= 0)
						DougaPinDisconnectPair(pin);
				}
				pin->Release();
			}
			ep->Release();
		}
		DougaHangTrace(L"PeelFfd:InText disconnected");
	}

	// ffd 単体 Stop（グラフ Stop の前にワーカーを落とす）
	{
		IMediaFilter* mf = NULL;
		if (SUCCEEDED(pFfd->QueryInterface(IID_IMediaFilter, (void**)&mf)) && mf) {
			mf->Stop();
			mf->Release();
			DougaHangTrace(L"PeelFfd:ffd IMediaFilter::Stop");
		}
	}

	IPin* fIn = DougaFfdVideoIn(pFfd);
	IPin* fOut = DougaFirstPin(pFfd, PINDIR_OUTPUT);
	IPin* rIn = pRen ? DougaFirstPin(pRen, PINDIR_INPUT) : NULL;
	IPin* upOut = NULL;
	BOOL reconnected = FALSE;
	if (fIn && fIn->ConnectedTo(&upOut) == S_OK && upOut) {
		// RenderFile 直後に保存した Dec→Ren のタイプで戻す（ffd 経由タイプだと CANNOT_CONNECT）
		AM_MEDIA_TYPE mtLive = {};
		AM_MEDIA_TYPE* pMt = NULL;
		BOOL freeLive = FALSE;
		if (s_dougaVidMap.valid) {
			pMt = &s_dougaVidMap.mt;
			DougaHangTrace(L"PeelFfd:using PinMap mt");
		} else {
			freeLive = SUCCEEDED(fIn->ConnectionMediaType(&mtLive));
			if (freeLive) pMt = &mtLive;
			DougaHangTrace(L"PeelFfd:using live fIn mt");
		}
		DougaHangTrace(L"PeelFfd:bypass Dec->Ren");
		DougaPinDisconnectPair(fIn);
		if (fOut) DougaPinDisconnectPair(fOut);
		HRESULT hr = E_FAIL;
		if (rIn) {
			if (pMt)
				hr = pGraph->ConnectDirect(upOut, rIn, pMt);
			if (FAILED(hr))
				hr = pGraph->ConnectDirect(upOut, rIn, NULL);
			if (FAILED(hr))
				hr = pGraph->Connect(upOut, rIn);
			reconnected = SUCCEEDED(hr);
		}
		{
			CString msg;
			msg.Format(L"PeelFfd:reconnect hr=0x%08X", (unsigned)hr);
			DougaHangTrace((LPCWSTR)msg);
		}
		if (freeLive) FreeMediaType(mtLive);
		upOut->Release();
	}
	if (fIn) fIn->Release();
	if (fOut) fOut->Release();
	if (rIn) rIn->Release();

	DougaDisconnectAllPins(pFfd);
	pGraph->RemoveFilter(pFfd);
	pFfd->Release();
	if (pRen) pRen->Release();
	DougaHangTrace(reconnected ? L"PeelFfd:done OK" : L"PeelFfd:done BROKEN");
	return reconnected;
}

// 遷移が終わらないとき、どのフィルタが止まっているかを individually に出す。
// GetState はタイムアウト 0 なので詰まったグラフでも戻る。
static void DougaTraceFilterStates(IGraphBuilder* g, LPCWSTR tag)
{
	if (!g) return;
	IEnumFilters* e = NULL;
	if (FAILED(g->EnumFilters(&e)) || !e) return;
	IBaseFilter* f = NULL;
	ULONG n = 0;
	while (e->Next(1, &f, &n) == S_OK) {
		if (!f) continue;
		FILTER_INFO fi = {};
		if (FAILED(f->QueryFilterInfo(&fi)))
			fi.achName[0] = 0;
		if (fi.pGraph) fi.pGraph->Release();
		FILTER_STATE fs = State_Stopped;
		const HRESULT hr = f->GetState(0, &fs);
		WCHAR m[256];
		swprintf_s(m, L"%s:%s st=%d hr=0x%08X", tag,
			fi.achName[0] ? fi.achName : L"(noname)", (int)fs, (unsigned)hr);
		DougaHangTrace(m);
		f->Release();
		f = NULL;
	}
	e->Release();
}

static void DougaStopFiltersIndividually(IGraphBuilder* g)
{
	if (!g) return;
	DougaHangTrace(L"StopFilters:begin");
	IEnumFilters* e = NULL;
	if (FAILED(g->EnumFilters(&e)) || !e) return;
	IBaseFilter* f = NULL;
	ULONG n = 0;
	while (e->Next(1, &f, &n) == S_OK) {
		IMediaFilter* mf = NULL;
		if (SUCCEEDED(f->QueryInterface(IID_IMediaFilter, (void**)&mf)) && mf) {
			mf->Stop();
			mf->Release();
		}
		f->Release();
	}
	e->Release();
	DougaHangTrace(L"StopFilters:done");
}

static void DougaColdResetPlaybackGlobals()
{
	// 起動直後と同じ状態へ。すべて UI (STA) スレッド上で行う。
	DougaHangTrace(L"ColdReset:enter");
	const BOOL hadFfd = (InterlockedCompareExchange(&s_dougaSessionUsedFfd, 0, 0) != 0);
	// グラフが既に無い呼び出し(停止時に数回連続で来る)では、落とすべき
	// フィルタ DLL も無いので CoFreeUnusedLibraries の周回は丸ごと無駄になる。
	// 登録フィルタの多い環境ではこれが毎回 0.3〜1.2 秒の停止として出る。
	const BOOL hadGraph = (pGraphBuilder != NULL || pMediaControl != NULL);

	DougaHangTrace(L"ColdReset:PitchCorrect_Shutdown");
	DougaPitchCorrect_Shutdown();

	if (Vdc)
		Vdc->SetVideoWindow(NULL);
	if (pVideoWindow) {
		pVideoWindow->put_Visible(OAFALSE);
		pVideoWindow->put_Owner(NULL);
	}

	// PitchCorrect 無効時はグラフ内 DS が音源。Peel 失敗で Stop を飛ばすと
	// Nuke しても DS が鳴り続ける（画面だけ消えて音が残る）。
	// Stop は非同期。Stopped になる前に RemoveFilter すると EVR/LAV で返ってこない。
	// 待ち中はキー/マウス/タイマーを回さない（切替の再入防止）。
	BOOL graphStopped = (pMediaControl == NULL && pGraphBuilder == NULL);
	if (pMediaControl) {
		DougaHangTrace(L"ColdReset:Stop");
		pMediaControl->Stop();
		graphStopped = DougaPumpWaitState(State_Stopped, 2000);
		if (!graphStopped)
			DougaHangTrace(L"ColdReset:Stop not settled, skip Nuke");
		else
			DougaHangTrace(L"ColdReset:Stop done");
	} else if (pGraphBuilder) {
		DougaHangTrace(L"ColdReset:StopFilters (no MediaControl)");
		DougaStopFiltersIndividually(pGraphBuilder);
		graphStopped = TRUE;
	}

	// Peel は Nuke 前には不要。Stop 後の再配線が ffd モジュールを汚し、
	// 無→有の次 Run を固める（ログ上 PinMap はクリーンなのに plays2:return で停止）。
	if (pGraphBuilder && graphStopped) {
		DougaHangTrace(L"ColdReset:Nuke");
		DougaNukeGraphFilters(pGraphBuilder);
		DougaHangTrace(L"ColdReset:Nuke done");
	}
	DougaClearVideoPinMap();

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
	RELEASE(pBasicVideo);
	RELEASE(pVideoWindow);
	RELEASE(pMediaSeeking);
	RELEASE(iam);
	RELEASE(pMediaEvent);
	if (!graphStopped && (pMediaControl || pGraphBuilder)) {
		// Stop が着地していないグラフに Release を投げると同じ所で固まる。
		// 参照は残してポインタだけ捨て、次の plays は新しい FilterGraph を作る。
		DougaHangTrace(L"ColdReset:leave graph (not stopped)");
		pMediaControl = NULL;
		pGraphBuilder = NULL;
	} else {
		RELEASE(pMediaControl);
		RELEASE1(pGraphBuilder);
	}

	if (pGraphBuilder || pMediaControl || pMediaSeeking || prend || Vdc)
		DougaHangTrace(L"ColdReset:WARNING leftover COM");

	ev = FALSE;
	streamidxSubOff = -1;
	s_dougaDeferSubStreamIdx = -1;
	s_dougaDeferSubConnect = FALSE;
	st12 = 0; // 字幕/音声選択の残り。次の字幕なしで EnumFilters が落ちる原因になる
	for (int j = 0; j < 40; j++) {
		streamname[j] = L"";
		streamname1[j] = L"";
		streamname2[j] = L"";
		streamidx[j] = -1;
		streamidx1[j] = -1;
		streamidx2[j] = -1;
	}
	DougaPrefFfdshowSubsReg(FALSE);
	DougaBlockFfdshowIntelligentConnect(TRUE);
	InterlockedExchange(&s_dougaSessionUsedFfd, 0);
	// 「クリーン＝インスタンス無し」: Flush スキップしない（残留 ffd.ax が次の Run を壊す）
	InterlockedExchange(&s_dougaAvoidFlush, 0);
	if (hadFfd) {
		DougaHangTrace(L"ColdReset:Flush (ffd unload)");
		DougaFlushLeftoverFilterModules();
	} else {
		DougaHangTrace(hadGraph ? L"FlushModules:skipped (no ffd)" : L"FlushModules:skipped (no graph)");
	}
	DougaHangTrace(L"ColdReset:leave");
}
static BOOL DougaGraphHasFfdshow(IGraphBuilder* g)
{
	if (!g) return FALSE;
	IEnumFilters* e = NULL;
	if (FAILED(g->EnumFilters(&e)) || !e) return FALSE;
	IBaseFilter* f = NULL;
	ULONG n = 0;
	BOOL hit = FALSE;
	while (e->Next(1, &f, &n) == S_OK) {
		FILTER_INFO fi;
		f->QueryFilterInfo(&fi);
		CString name = fi.achName;
		if (fi.pGraph) fi.pGraph->Release();
		// Audio Decoder を映像 ffd と誤認しない（Ensure フリーズの主因）
		if (name.Find(L"ffdshow raw") >= 0 || name.Find(L"ffdshow Video") >= 0)
			hit = TRUE;
		f->Release();
		if (hit) break;
	}
	e->Release();
	return hit;
}

static BOOL DougaGraphHasFfdAudio(IGraphBuilder* g)
{
	if (!g) return FALSE;
	IEnumFilters* e = NULL;
	if (FAILED(g->EnumFilters(&e)) || !e) return FALSE;
	IBaseFilter* f = NULL;
	ULONG n = 0;
	BOOL hit = FALSE;
	while (e->Next(1, &f, &n) == S_OK) {
		FILTER_INFO fi;
		f->QueryFilterInfo(&fi);
		CString name = fi.achName;
		if (fi.pGraph) fi.pGraph->Release();
		if (name.Find(L"ffdshow Audio") >= 0 || name.Find(L"ffdshow audio") >= 0)
			hit = TRUE;
		f->Release();
		if (hit) break;
	}
	e->Release();
	return hit;
}

static BOOL DougaGraphHasAnyFfdDirt(IGraphBuilder* g)
{
	return DougaGraphHasFfdshow(g) || DougaGraphHasFfdAudio(g);
}

static void DougaEnableAllFfdshowInGraph(IGraphBuilder* g);
static void DougaEnsureFfdOnVideoPath(IGraphBuilder* pGraph);

// =============================================================================
// DougaPins — 字幕あり/なし分岐の唯一入口（Peel / Graph 作り直しはしない）
// 契約:
//  1) RenderFile は常に LAV→EVR / LAV→DS（ffd は IC 拒否）
//  2) NO-SUB: 映像ピンを触らない
//  3) WITH-SUB: PinMap の mt で Dec→ffd raw→EVR を挿入するだけ
//  4) InText は呼び出し側が Stopped 中に ConnectSubtitleWithDirectVobSub
//  5) 破棄は ColdReset の Stop→Nuke（Peel なし）
// =============================================================================
static BOOL DougaPins_ApplyRoute(IGraphBuilder* g, BOOL wantSubtitle)
{
	if (!g) return FALSE;
	if (!wantSubtitle) {
		DougaHangTrace(L"Pins:route NO-SUB (leave Dec->Ren alone)");
		if (DougaGraphHasAnyFfdDirt(g))
			DougaHangTrace(L"Pins:NO-SUB warning: unexpected ffd still in graph");
		InterlockedExchange(&s_dougaSessionUsedFfd, 0);
		return TRUE;
	}
	DougaHangTrace(L"Pins:route WITH-SUB (insert ffd raw on video)");
	DougaEnsureFfdOnVideoPath(g);
	DougaEnableAllFfdshowInGraph(g);
	if (DougaGraphHasFfdshow(g)) {
		InterlockedExchange(&s_dougaSessionUsedFfd, 1);
		DougaHangTrace(L"Pins:WITH-SUB ready");
		return TRUE;
	}
	DougaHangTrace(L"Pins:WITH-SUB Ensure failed (no video ffd)");
	InterlockedExchange(&s_dougaSessionUsedFfd, 0);
	return FALSE;
}

// 旧: 同一 Graph の Nuke+RenderFile。EVR 再利用事故の温床なので入口から外す
static HRESULT DougaRerenderFileClean(IGraphBuilder* g, LPCWSTR file, IBaseFilter* evr)
{
	UNREFERENCED_PARAMETER(g);
	UNREFERENCED_PARAMETER(file);
	UNREFERENCED_PARAMETER(evr);
	DougaHangTrace(L"rerender:DISABLED (use Pins_ApplyRoute)");
	return E_NOTIMPL;
}

static void DougaEnableAllFfdshowInGraph(IGraphBuilder* g)
{
	if (!g) return;
	IEnumFilters* e = NULL;
	if (FAILED(g->EnumFilters(&e)) || !e) return;
	IBaseFilter* f = NULL;
	ULONG n = 0;
	while (e->Next(1, &f, &n) == S_OK) {
		FILTER_INFO fi;
		f->QueryFilterInfo(&fi);
		CString name = fi.achName;
		if (fi.pGraph) fi.pGraph->Release();
		if (name.Find(L"ffdshow raw") >= 0 || name.Find(L"ffdshow Video") >= 0)
			DougaEnableFfdshowSubs(f);
		f->Release();
	}
	e->Release();
}

// 字幕表示には映像経路上の ffdshow が必要。plays のグラフ構築時だけ呼ぶ。
static void DougaEnsureFfdOnVideoPath(IGraphBuilder* pGraph)
{
	if (!pGraph) return;
	/* CLSID_ffdshowRawVideo / CLSID_ffdshowVideoDecoder are file-scope */

	IBaseFilter* pDec = NULL;
	IBaseFilter* pFfd = NULL;
	IBaseFilter* pRen = NULL;
	BOOL addedFfd = FALSE;

	IEnumFilters* pEnum = NULL;
	if (SUCCEEDED(pGraph->EnumFilters(&pEnum)) && pEnum) {
		IBaseFilter* pF = NULL;
		ULONG n = 0;
		while (pEnum->Next(1, &pF, &n) == S_OK) {
			FILTER_INFO fi;
			pF->QueryFilterInfo(&fi);
			CString name = fi.achName;
			if (fi.pGraph) fi.pGraph->Release();
			if (name.Find(L"ffdshow raw") >= 0 || name.Find(L"ffdshow Video") >= 0) {
				if (pFfd) pFfd->Release();
				pFfd = pF; pFfd->AddRef();
			}
			if (name.Find(L"LAV Video") >= 0) {
				if (pDec) pDec->Release();
				pDec = pF; pDec->AddRef();
			}
			if (name.Find(L"Enhanced Video Renderer") >= 0
				|| name.Find(L"Video Mixing Renderer") >= 0
				|| name.Find(L"Video Renderer") == 0) {
				if (pRen) pRen->Release();
				pRen = pF; pRen->AddRef();
			}
			pF->Release();
		}
		pEnum->Release();
	}

	if (pRen) {
		IPin* rIn = DougaFirstPin(pRen, PINDIR_INPUT);
		if (rIn) {
			IPin* up = NULL;
			if (rIn->ConnectedTo(&up) == S_OK && up) {
				PIN_INFO pi; pi.pFilter = NULL;
				up->QueryPinInfo(&pi);
				up->Release();
				if (pi.pFilter) {
					FILTER_INFO fi;
					pi.pFilter->QueryFilterInfo(&fi);
					CString nm = fi.achName;
					if (fi.pGraph) fi.pGraph->Release();
					// Audio 等は除外。raw / Video のみ「映像経路上」とみなす
					if (nm.Find(L"ffdshow raw") >= 0 || nm.Find(L"ffdshow Video") >= 0) {
						DougaHangTrace(L"Ensure:already on video path");
						InterlockedExchange(&s_dougaSessionUsedFfd, 1);
						pi.pFilter->Release();
						rIn->Release();
						if (pDec) pDec->Release();
						if (pFfd) pFfd->Release();
						if (pRen) pRen->Release();
						return;
					} else if (!pDec) {
						pDec = pi.pFilter; pDec->AddRef();
					}
					pi.pFilter->Release();
				}
			}
			rIn->Release();
		}
	}

	// グラフ内の stray ffd は使わない（誤接続で固まる）。字幕用は raw を新規挿入。
	if (pFfd) { pFfd->Release(); pFfd = NULL; }
	{
		HRESULT hr = CoCreateInstance(CLSID_ffdshowRawVideo, NULL, CLSCTX_INPROC_SERVER,
			IID_IBaseFilter, (void**)&pFfd);
		if (FAILED(hr) || !pFfd) {
			pFfd = NULL;
			DougaHangTrace(L"Ensure:CoCreate raw FAILED");
		} else {
			hr = pGraph->AddFilter(pFfd, L"ffdshow raw video filter");
			if (FAILED(hr)) {
				DougaHangTrace(L"Ensure:AddFilter raw FAILED");
				pFfd->Release();
				pFfd = NULL;
			} else {
				addedFfd = TRUE;
				DougaHangTrace(L"Ensure:AddFilter raw OK");
			}
		}
	}

	if (pFfd && pDec && pRen) {
		IPin* decOut = DougaFirstPin(pDec, PINDIR_OUTPUT);
		IPin* fIn = DougaFfdVideoIn(pFfd);
		IPin* fOut = DougaFirstPin(pFfd, PINDIR_OUTPUT);
		IPin* rIn = DougaFirstPin(pRen, PINDIR_INPUT);
		BOOL directToRen = FALSE;
		IPin* origDown = NULL;
		if (decOut && decOut->ConnectedTo(&origDown) == S_OK && origDown) {
			PIN_INFO pi; pi.pFilter = NULL;
			origDown->QueryPinInfo(&pi);
			if (pi.pFilter) {
				FILTER_INFO fi;
				pi.pFilter->QueryFilterInfo(&fi);
				CString n = fi.achName;
				if (fi.pGraph) fi.pGraph->Release();
				if (n.Find(L"Enhanced Video Renderer") >= 0
					|| n.Find(L"Video Mixing Renderer") >= 0
					|| n.Find(L"Video Renderer") == 0)
					directToRen = TRUE;
				pi.pFilter->Release();
			}
		}
		if (directToRen && decOut && fIn && fOut && rIn && origDown) {
			DougaHangTrace(L"Ensure:Connect Dec->raw->Ren begin");
			AM_MEDIA_TYPE mtLive = {};
			AM_MEDIA_TYPE* pMt = NULL;
			BOOL freeLive = FALSE;
			if (s_dougaVidMap.valid) {
				pMt = &s_dougaVidMap.mt;
				DougaHangTrace(L"Ensure:using PinMap mt");
			} else {
				freeLive = SUCCEEDED(decOut->ConnectionMediaType(&mtLive));
				if (freeLive) pMt = &mtLive;
			}
			DougaPinDisconnectPair(decOut);
			HRESULT h1 = E_FAIL;
			if (pMt)
				h1 = pGraph->ConnectDirect(decOut, fIn, pMt);
			if (FAILED(h1))
				h1 = pGraph->ConnectDirect(decOut, fIn, NULL);
			if (FAILED(h1))
				h1 = pGraph->Connect(decOut, fIn);
			HRESULT h2 = E_FAIL;
			if (SUCCEEDED(h1)) {
				// EVR 側は Intelligent Connect 優先（色変換が挟まる場合がある）
				h2 = pGraph->Connect(fOut, rIn);
				if (FAILED(h2)) h2 = pGraph->ConnectDirect(fOut, rIn, NULL);
			}
			{
				CString msg;
				msg.Format(L"Ensure:Connect hr=0x%08X / 0x%08X", (unsigned)h1, (unsigned)h2);
				DougaHangTrace((LPCWSTR)msg);
			}
			if (FAILED(h1) || FAILED(h2)) {
				DougaPinDisconnectPair(decOut);
				DougaPinDisconnectPair(fOut);
				HRESULT hrRest = E_FAIL;
				if (pMt)
					hrRest = pGraph->ConnectDirect(decOut, origDown, pMt);
				if (FAILED(hrRest))
					hrRest = pGraph->ConnectDirect(decOut, origDown, NULL);
				if (FAILED(hrRest))
					pGraph->Connect(decOut, origDown);
				if (addedFfd) {
					pGraph->RemoveFilter(pFfd);
					pFfd->Release();
					pFfd = NULL;
				}
			}
			if (freeLive) FreeMediaType(mtLive);
		} else {
			DougaHangTrace(L"Ensure:skip (not Dec->Ren direct)");
			if (addedFfd && pFfd) {
				pGraph->RemoveFilter(pFfd);
				pFfd->Release();
				pFfd = NULL;
			}
		}
		if (origDown) origDown->Release();
		if (decOut) decOut->Release();
		if (fIn) fIn->Release();
		if (fOut) fOut->Release();
		if (rIn) rIn->Release();
	} else {
		DougaHangTrace(L"Ensure:missing Dec/ffd/Ren");
		if (addedFfd && pFfd) {
			pGraph->RemoveFilter(pFfd);
			pFfd->Release();
			pFfd = NULL;
		}
	}

	if (pDec) pDec->Release();
	if (pFfd) {
		InterlockedExchange(&s_dougaSessionUsedFfd, 1);
		pFfd->Release();
	}
	if (pRen) pRen->Release();
}

// 字幕選択時のみ。InText 配線。映像経路の ffdshow raw は plays の Ensure で載せる。
void CDouga::ConnectSubtitleWithDirectVobSub(IGraphBuilder* pGraph)
{
	if (!pGraph) return;
	OutputDebugString(L"=== ConnectSubtitleWithDirectVobSub (text-only) ===\n");

	IBaseFilter* pSplit = NULL;
	IBaseFilter* pFfd = NULL;
	IEnumFilters* pEnum = NULL;
	if (SUCCEEDED(pGraph->EnumFilters(&pEnum)) && pEnum) {
		IBaseFilter* pF = NULL;
		ULONG n = 0;
		while (pEnum->Next(1, &pF, &n) == S_OK) {
			FILTER_INFO fi;
			pF->QueryFilterInfo(&fi);
			CString name = fi.achName;
			if (fi.pGraph) fi.pGraph->Release();
			if (!pSplit && DougaNameIsSplitterish(name)) {
				pSplit = pF; pSplit->AddRef();
			}
			if (name.Find(L"ffdshow raw") >= 0 || name.Find(L"ffdshow Video") >= 0) {
				if (pFfd) pFfd->Release();
				pFfd = pF; pFfd->AddRef();
			}
			pF->Release();
		}
		pEnum->Release();
	}

	if (!pFfd) {
		OutputDebugString(L"subtitle: no ffdshow in graph\n");
		if (pSplit) pSplit->Release();
		return;
	}
	DougaEnableFfdshowSubs(pFfd);
	if (!pSplit) {
		pFfd->Release();
		return;
	}

	IPin* pSub = NULL;
	IEnumPins* ep = NULL;
	if (SUCCEEDED(pSplit->EnumPins(&ep)) && ep) {
		IPin* pin = NULL;
		while (ep->Next(1, &pin, NULL) == S_OK) {
			PIN_DIRECTION dir = PINDIR_INPUT;
			pin->QueryDirection(&dir);
			if (dir == PINDIR_OUTPUT) {
				PIN_INFO pi; pi.pFilter = NULL;
				pin->QueryPinInfo(&pi);
				CString low = pi.achName; low.MakeLower();
				if (pi.pFilter) pi.pFilter->Release();
				BOOL isSub = (low.Find(L"subtitle") >= 0 || low.Find(L"subpic") >= 0
					|| low.Find(L"字幕") >= 0 || low.Find(L"softsub") >= 0);
				if (!isSub) {
					IEnumMediaTypes* em = NULL;
					if (SUCCEEDED(pin->EnumMediaTypes(&em)) && em) {
						AM_MEDIA_TYPE* mt = NULL;
						while (em->Next(1, &mt, NULL) == S_OK) {
							if (mt && (mt->majortype == MEDIATYPE_Subtitle
								|| mt->majortype == MEDIATYPE_Subtitle_MPC
								|| mt->majortype == MEDIATYPE_Text
								|| mt->majortype.Data1 == 0xe487eb08))
								isSub = TRUE;
							if (mt) DeleteMediaType(mt);
							if (isSub) break;
						}
						em->Release();
					}
				}
				if (isSub && !DougaPinPeerHas(pin, L"ffdshow")) {
					IPin* peer = NULL;
					if (pin->ConnectedTo(&peer) != S_OK) {
						pSub = pin; pSub->AddRef();
					} else {
						peer->Release();
					}
				}
			}
			pin->Release();
			if (pSub) break;
		}
		ep->Release();
	}

	IPin* pText = NULL;
	ep = NULL;
	if (SUCCEEDED(pFfd->EnumPins(&ep)) && ep) {
		IPin* pin = NULL;
		while (ep->Next(1, &pin, NULL) == S_OK) {
			PIN_DIRECTION dir = PINDIR_OUTPUT;
			pin->QueryDirection(&dir);
			if (dir == PINDIR_INPUT) {
				PIN_INFO pi; pi.pFilter = NULL;
				pin->QueryPinInfo(&pi);
				CString low = pi.achName; low.MakeLower();
				if (pi.pFilter) pi.pFilter->Release();
				if (low.Find(L"text") >= 0) {
					IPin* busy = NULL;
					if (pin->ConnectedTo(&busy) != S_OK) {
						pText = pin; pText->AddRef();
					} else {
						busy->Release();
					}
				}
			}
			pin->Release();
			if (pText) break;
		}
		ep->Release();
	}

	if (pSub && pText) {
		HRESULT hr = pGraph->ConnectDirect(pSub, pText, NULL);
		if (FAILED(hr)) hr = pGraph->Connect(pSub, pText);
		CString msg; msg.Format(L"subtitle: InText hr=0x%08X", (unsigned)hr);
		DougaHangTrace((LPCWSTR)msg);
		OutputDebugString(msg + L"\n");
	} else {
		DougaHangTrace(L"subtitle: free sub/InText not found");
		OutputDebugString(L"subtitle: free sub/InText not found (may already be wired)\n");
	}
	if (pSub) pSub->Release();
	if (pText) pText->Release();
	pSplit->Release();
	pFfd->Release();
	OutputDebugString(L"=== ConnectSubtitleWithDirectVobSub End ===\n");
}

// ---------------------------------------------------------------------------
// LAV 優先レンダリング
// Windows 内蔵の AVI Splitter + WMVideo Decoder DMO 経路は環境によって
// まともに再生できない。K-Lite 等で LAV が入っていれば AVI はそちらを使う。
// CLSID 直書きは版によってずれるのでフレンドリ名で引く。
// ---------------------------------------------------------------------------
static IBaseFilter* DougaCreateFilterByFriendlyName(LPCWSTR wantName)
{
	if (!wantName || !wantName[0]) return NULL;
	ICreateDevEnum* devEnum = NULL;
	if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC,
		IID_ICreateDevEnum, (void**)&devEnum)) || !devEnum)
		return NULL;
	IEnumMoniker* en = NULL;
	const HRESULT hrEnum = devEnum->CreateClassEnumerator(CLSID_LegacyAmFilterCategory, &en, 0);
	devEnum->Release();
	if (hrEnum != S_OK || !en)
		return NULL;

	IBaseFilter* found = NULL;
	IMoniker* mon = NULL;
	while (!found && en->Next(1, &mon, NULL) == S_OK) {
		IPropertyBag* bag = NULL;
		if (SUCCEEDED(mon->BindToStorage(0, 0, IID_IPropertyBag, (void**)&bag)) && bag) {
			VARIANT v;
			VariantInit(&v);
			if (SUCCEEDED(bag->Read(L"FriendlyName", &v, 0)) && v.vt == VT_BSTR && v.bstrVal) {
				if (_wcsicmp(v.bstrVal, wantName) == 0) {
					IBaseFilter* f = NULL;
					if (SUCCEEDED(mon->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&f)) && f)
						found = f;
				}
			}
			VariantClear(&v);
			bag->Release();
		}
		mon->Release();
		mon = NULL;
	}
	en->Release();
	return found;
}

static IPin* DougaGetFreePin(IBaseFilter* f, PIN_DIRECTION want)
{
	if (!f) return NULL;
	IEnumPins* ep = NULL;
	if (FAILED(f->EnumPins(&ep)) || !ep) return NULL;
	IPin* found = NULL;
	IPin* pin = NULL;
	while (!found && ep->Next(1, &pin, NULL) == S_OK) {
		PIN_DIRECTION dir;
		IPin* peer = NULL;
		if (SUCCEEDED(pin->QueryDirection(&dir)) && dir == want
			&& FAILED(pin->ConnectedTo(&peer))) {
			found = pin;
			pin = NULL;
		}
		if (peer) peer->Release();
		if (pin) pin->Release();
		pin = NULL;
	}
	ep->Release();
	return found;
}

static BOOL DougaPinMajorType(IPin* pin, GUID* out)
{
	if (!pin || !out) return FALSE;
	IEnumMediaTypes* em = NULL;
	if (FAILED(pin->EnumMediaTypes(&em)) || !em) return FALSE;
	BOOL ok = FALSE;
	AM_MEDIA_TYPE* mt = NULL;
	if (em->Next(1, &mt, NULL) == S_OK && mt) {
		*out = mt->majortype;
		ok = TRUE;
		DeleteMediaType(mt);
	}
	em->Release();
	return ok;
}

// スプリッタ出力を指定デコーダへ直結してから下流を Render する。
// Intelligent Connect に任せるとメリット順で内蔵 DMO が勝ってしまうため。
static BOOL DougaConnectThroughDecoder(IGraphBuilder* g, IPin* srcOut, IBaseFilter* dec)
{
	if (!g || !srcOut || !dec) return FALSE;
	IPin* decIn = DougaGetFreePin(dec, PINDIR_INPUT);
	if (!decIn) {
		DougaHangTrace(L"LAV:decoder has no free input pin");
		return FALSE;
	}
	HRESULT hr = g->ConnectDirect(srcOut, decIn, NULL);
	if (FAILED(hr)) {
		WCHAR m[96];
		swprintf_s(m, L"LAV:ConnectDirect hr=0x%08X", (unsigned)hr);
		DougaHangTrace(m);
		decIn->Release();
		return FALSE;
	}
	IPin* decOut = DougaGetFreePin(dec, PINDIR_OUTPUT);
	if (decOut) {
		hr = g->Render(decOut);
		decOut->Release();
	} else {
		hr = E_FAIL;
	}
	if (FAILED(hr)) {
		// 中途半端に繋がった状態を残さない
		g->Disconnect(srcOut);
		g->Disconnect(decIn);
		decIn->Release();
		return FALSE;
	}
	decIn->Release();
	return TRUE;
}

static BOOL DougaFilterHasConnectedPin(IBaseFilter* f)
{
	if (!f) return FALSE;
	IEnumPins* ep = NULL;
	if (FAILED(f->EnumPins(&ep)) || !ep) return FALSE;
	BOOL any = FALSE;
	IPin* pin = NULL;
	while (!any && ep->Next(1, &pin, NULL) == S_OK) {
		IPin* peer = NULL;
		if (SUCCEEDED(pin->ConnectedTo(&peer)) && peer) {
			any = TRUE;
			peer->Release();
		}
		pin->Release();
		pin = NULL;
	}
	ep->Release();
	return any;
}

// LAV の設定インターフェース。SDK ヘッダは取り込まず先頭 3 メソッドだけ宣言する。
// LAVVideoSettings.h の並びは SetRuntimeConfig / Get / Set の順で、
// この 3 つは vtable 先頭に固定されている。順序を間違えると別の関数を呼ぶので注意。
struct DECLSPEC_UUID("FA40D6E9-4D38-4761-ADD2-71A9EC5FD32F") IDougaLavVideoSettings : public IUnknown
{
	STDMETHOD(SetRuntimeConfig)(BOOL bRuntimeConfig) PURE;
	STDMETHOD_(BOOL, GetFormatConfiguration)(int vCodec) PURE;
	STDMETHOD(SetFormatConfiguration)(int vCodec, BOOL bEnabled) PURE;
};

// LAV Splitter の設定インターフェース。
struct DECLSPEC_UUID("774A919D-EA95-4A87-8A1E-F48ABE8499C7") IDougaLavSplitterSettings : public IUnknown
{
	STDMETHOD(SetRuntimeConfig)(BOOL bRuntimeConfig) PURE;
	// ILAVFSettings の 2 番目以降のメソッドは文字列を返すものなどがあり、
	// 単純な int ではないため宣言を省略する。SetFormatEnabled は 15 番目付近。
	// ここでは SetRuntimeConfig(TRUE) による既定値（全有効）へのリセットのみ使う。
};

// K-Lite 等は LAV Video Decoder の担当フォーマットを絞って登録することがあり、
// 古い MS-MPEG4 系 AVI は拒否されて内蔵 DMO に流れる。
// SetRuntimeConfig(TRUE) はこのインスタンスだけを既定値に戻すもので、
// 利用者のレジストリ設定は書き換えない。接続前に呼ぶ必要がある。
static void DougaLavEnableAllVideoFormats(IBaseFilter* vdec)
{
	if (!vdec) return;
	IDougaLavVideoSettings* cfg = NULL;
	if (FAILED(vdec->QueryInterface(__uuidof(IDougaLavVideoSettings), (void**)&cfg)) || !cfg) {
		DougaHangTrace(L"LAV:vdec no ILAVVideoSettings");
		return;
	}
	const HRESULT hr = cfg->SetRuntimeConfig(TRUE);
	// 列挙値はバージョンで増減するので範囲で総当たりする
	int on = 0;
	for (int codec = 0; codec < 64; ++codec) {
		if (SUCCEEDED(cfg->SetFormatConfiguration(codec, TRUE)))
			on++;
	}
	cfg->Release();
	WCHAR m[112];
	swprintf_s(m, L"LAV:vdec runtime cfg hr=0x%08X formats=%d", (unsigned)hr, on);
	DougaHangTrace(m);
}

static void DougaLavEnableAllSplitterFormats(IBaseFilter* src)
{
	if (!src) return;
	IDougaLavSplitterSettings* cfg = NULL;
	if (FAILED(src->QueryInterface(__uuidof(IDougaLavSplitterSettings), (void**)&cfg)) || !cfg) {
		DougaHangTrace(L"LAV:src no ILAVFSettings");
		return;
	}
	const HRESULT hr = cfg->SetRuntimeConfig(TRUE);
	cfg->Release();
	WCHAR m[112];
	swprintf_s(m, L"LAV:src runtime cfg hr=0x%08X", (unsigned)hr);
	DougaHangTrace(m);
}

// 成功したら S_OK。LAV が無い/失敗した場合は元の RenderFile へ落とす。
static HRESULT DougaRenderWithLav(IGraphBuilder* g, LPCWSTR path)
{
	if (!g || !path || !path[0]) return E_INVALIDARG;

	IBaseFilter* src = DougaCreateFilterByFriendlyName(L"LAV Splitter Source");
	if (!src) {
		DougaHangTrace(L"LAV:not installed");
		return E_FAIL;
	}
	IFileSourceFilter* fsrc = NULL;
	if (FAILED(src->QueryInterface(IID_IFileSourceFilter, (void**)&fsrc)) || !fsrc) {
		DougaHangTrace(L"LAV:no IFileSourceFilter");
		src->Release();
		return E_FAIL;
	}
	DougaLavEnableAllSplitterFormats(src);
	HRESULT hr = fsrc->Load(path, NULL);
	fsrc->Release();
	if (FAILED(hr)) {
		WCHAR m[96];
		swprintf_s(m, L"LAV:Load FAILED hr=0x%08X", (unsigned)hr);
		DougaHangTrace(m);
		src->Release();
		return hr;
	}
	hr = g->AddFilter(src, L"LAV Splitter Source");
	if (FAILED(hr)) {
		DougaHangTrace(L"LAV:AddFilter source FAILED");
		src->Release();
		return hr;
	}

	IBaseFilter* vdec = DougaCreateFilterByFriendlyName(L"LAV Video Decoder");
	if (!vdec)
		DougaHangTrace(L"LAV:vdec not found");
	else if (FAILED(g->AddFilter(vdec, L"LAV Video Decoder"))) {
		DougaHangTrace(L"LAV:vdec AddFilter FAILED");
		vdec->Release();
		vdec = NULL;
	}
	else {
		DougaLavEnableAllVideoFormats(vdec);
	}
	IBaseFilter* adec = DougaCreateFilterByFriendlyName(L"LAV Audio Decoder");
	if (!adec)
		DougaHangTrace(L"LAV:adec not found");
	else if (FAILED(g->AddFilter(adec, L"LAV Audio Decoder"))) {
		DougaHangTrace(L"LAV:adec AddFilter FAILED");
		adec->Release();
		adec = NULL;
	}

	int rendered = 0;
	IEnumPins* ep = NULL;
	if (SUCCEEDED(src->EnumPins(&ep)) && ep) {
		IPin* pin = NULL;
		while (ep->Next(1, &pin, NULL) == S_OK) {
			PIN_DIRECTION dir;
			IPin* peer = NULL;
			if (SUCCEEDED(pin->QueryDirection(&dir)) && dir == PINDIR_OUTPUT
				&& FAILED(pin->ConnectedTo(&peer))) {
				GUID major = GUID_NULL;
				DougaPinMajorType(pin, &major);
				const BOOL isVideo = (major == MEDIATYPE_Video);
				IBaseFilter* want = isVideo ? vdec : ((major == MEDIATYPE_Audio) ? adec : NULL);
				BOOL done = FALSE;
				if (want)
					done = DougaConnectThroughDecoder(g, pin, want);
				if (done) {
					DougaHangTrace(isVideo ? L"LAV:video via LAV Video Decoder"
						: L"LAV:audio via LAV Audio Decoder");
				} else {
					if (want)
						DougaHangTrace(isVideo ? L"LAV:vdec connect FAILED, fallback IC"
							: L"LAV:adec connect FAILED, fallback IC");
					done = SUCCEEDED(g->Render(pin));
				}
				if (done) rendered++;
			}
			if (peer) peer->Release();
			pin->Release();
			pin = NULL;
		}
		ep->Release();
	}

	// 使われなかったデコーダはグラフに残さない
	if (vdec) {
		if (!DougaFilterHasConnectedPin(vdec)) g->RemoveFilter(vdec);
		vdec->Release();
	}
	if (adec) {
		if (!DougaFilterHasConnectedPin(adec)) g->RemoveFilter(adec);
		adec->Release();
	}

	if (rendered == 0) {
		DougaHangTrace(L"LAV:Render produced nothing, fallback");
		g->RemoveFilter(src);
		src->Release();
		return E_FAIL;
	}
	{
		WCHAR m[96];
		swprintf_s(m, L"LAV:rendered %d pin(s)", rendered);
		DougaHangTrace(m);
	}
	src->Release();
	return S_OK;
}

static BOOL DougaPathShouldUseLav(LPCWSTR path)
{
	if (!path) return FALSE;
	const WCHAR* dot = wcsrchr(path, L'.');
	if (!dot) return FALSE;
	return _wcsicmp(dot, L".avi") == 0;
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
	// RenderFile 前にジャケット取得(再生中はファイル占有で Shell/MF が失敗しやすい)
	if (og && s && s[0])
		og->LoadJacket(CString(s));

	DougaHangTrace(L"plays:enter");
	// 前回字幕ありなら COM を起動直後相当に戻してから始める
	DougaRecycleComApartmentIfNeeded();
	DougaEnsureComInitialized();

	int len = ::WideCharToMultiByte(CP_THREAD_ACP,0, ss, -1, NULL, 0, NULL, NULL);
	memcpy((TCHAR*)douga,(TCHAR*)ss,len*2+2);

	// 途中停止漏れでも起動直後と同じグローバル状態から始める
	DougaColdResetPlaybackGlobals();
	DougaHangTrace(L"plays:after ColdReset");
	DougaEnsureComInitialized();
	audioStreams.clear();
	videoStreams.clear();
	subtitleStreams.clear();
	streamMap.videoStart = streamMap.audioStart = streamMap.subtitleStart = -1;
	streamMap.videoCount = streamMap.audioCount = streamMap.subtitleCount = 0;
	{
		const HRESULT hrGraph = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
			IID_IGraphBuilder, (LPVOID*)&pGraphBuilder);
		if (FAILED(hrGraph) || !pGraphBuilder) {
			WCHAR m[96];
			swprintf_s(m, L"plays:CoCreateInstance FilterGraph FAILED hr=0x%08X", (unsigned)hrGraph);
			DougaHangTrace(m);
			pGraphBuilder = NULL;
			DougaHangTrace(L"plays:return (no graph)");
			return;
		}
	}
	if(pGraphBuilder){
		pGraphBuilder->QueryInterface(IID_IMediaControl,(LPVOID *)&pMediaControl);
		pGraphBuilder->QueryInterface(IID_IVideoWindow,(LPVOID *)&pVideoWindow);
		pGraphBuilder->QueryInterface(IID_IMediaPosition,(LPVOID *)&pMediaPosition);
		pGraphBuilder->QueryInterface(IID_IBasicAudio, (LPVOID *)&pBasicAudio);
		pGraphBuilder->QueryInterface(IID_IMediaEvent,(LPVOID*)&pMediaEvent);
	}


	rate = 0.0;
	// 空グラフでの MediaDet 走査はファイルを二重オープンして重い。
	// 拡張子で仮置きし、RenderFile 後にグラフから取り直す。
	{
		CString ext = s2;
		ext.MakeLower();
		if (ext.Right(4) == ".vob" || ext.Right(4) == ".mpg" || ext.Right(3) == ".ts")
			rate = 29.97;
		else if (ext.Right(4) == ".mp4" || ext.Right(4) == ".mkv" || ext.Right(4) == ".m4v"
			|| ext.Right(4) == ".mov" || ext.Right(5) == ".webm")
			rate = 23.976;
		else if (ext.Right(4) == ".avi" || ext.Right(4) == ".wmv")
			rate = 29.97;
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
		IEnumMoniker *pEnum = NULL;

		// VSFilter を RenderFile 前に入れない。2回目の字幕ピン Intelligent Connect で固まる。
		hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC,
			IID_ICreateDevEnum, (void**)&pDevEnum);
		if (SUCCEEDED(hr) && pDevEnum) {
			hr = pDevEnum->CreateClassEnumerator(CLSID_LegacyAmFilterCategory, &pEnum, 0);
			pDevEnum->Release();
			pDevEnum = NULL;
		}
		OSVERSIONINFO in;ZeroMemory(&in,sizeof(in));in.dwOSVersionInfoSize=sizeof(OSVERSIONINFO);GetVersionEx(&in);


		if(in.dwMajorVersion>=5 && pEnum){

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
					if (FAILED(hr) || varName.vt != VT_BSTR || !varName.bstrVal) {
						VariantClear(&varName);
						RELEASE(pPropBag);
						RELEASE(pMoniker);
						continue;
					}
					int len = ::WideCharToMultiByte(CP_THREAD_ACP,0, varName.bstrVal, -1, NULL, 0, NULL, NULL);

					if(_wcsnicmp(varName.bstrVal,L"Enhanced Video Renderer",len*2-2)==0 && savedata.evr && savedata.render==0
						&& !(mode==11 || mode==12 || mode==16 || mode==19)){
						IBaseFilter* evrF = NULL;
						hr = pMoniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&evrF);
						if (SUCCEEDED(hr) && evrF) {
							RELEASE(Vdc);
							RELEASE(service);
							RELEASE(prend);
							prend = evrF;
							ev = TRUE;
							if (SUCCEEDED(prend->QueryInterface(IID_IMFGetService, (LPVOID*)&service)) && service) {
								hr = service->GetService(MR_VIDEO_RENDER_SERVICE, IID_IMFVideoDisplayControl, (void**)&Vdc);
								if (FAILED(hr) || !Vdc)
									Vdc = NULL;
							} else {
								service = NULL;
							}
							HWND hwndVideo = m_videoSite.GetSafeHwnd() ? m_videoSite.m_hWnd : m_hWnd;
							if (Vdc && hwndVideo)
								Vdc->SetVideoWindow(hwndVideo);
						}
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



	if(prend)
		pGraphBuilder->AddFilter(prend, L"Enhanced Video Renderer");
	// AddFilter 後なら GetService が通ることがある（UI 前の NULL クラッシュ回避）
	if (ev && prend && !Vdc) {
		if (!service)
			prend->QueryInterface(IID_IMFGetService, (LPVOID*)&service);
		if (service)
			service->GetService(MR_VIDEO_RENDER_SERVICE, IID_IMFVideoDisplayControl, (void**)&Vdc);
		HWND hwndVideo = m_videoSite.GetSafeHwnd() ? m_videoSite.m_hWnd : m_hWnd;
		if (Vdc && hwndVideo)
			Vdc->SetVideoWindow(hwndVideo);
	}

	DumpFilterGraph();

	// IC に ffdshow を拾わせない（字幕は Ensure の CoCreate で載せる）
	DougaPrefFfdshowSubsReg(FALSE);
	DougaBlockFfdshowIntelligentConnect(TRUE);
	DougaInstallRejectFfdCallback(pGraphBuilder);
	HRESULT hr2 = E_FAIL;
	// AVI は内蔵 AVI Splitter 経路が環境依存で不安定なので LAV を優先する。
	if (DougaPathShouldUseLav(ss)) {
		DougaHangTrace(L"plays:LAV:begin (avi)");
		hr2 = DougaRenderWithLav(pGraphBuilder, ss);
	}
	if (FAILED(hr2)) {
		DougaHangTrace(L"plays:RenderFile:begin (UI/STA sync)");
		hr2 = pGraphBuilder->RenderFile(ss, NULL);
	}
	DougaClearRejectFfdCallback(pGraphBuilder);
	{
		CString m; m.Format(L"plays:RenderFile:end hr=0x%08X", (unsigned)hr2);
		DougaHangTrace(m);
	}
	if (ev && prend && !Vdc) {
		if (!service)
			prend->QueryInterface(IID_IMFGetService, (LPVOID*)&service);
		if (service)
			service->GetService(MR_VIDEO_RENDER_SERVICE, IID_IMFVideoDisplayControl, (void**)&Vdc);
		HWND hwndVideo = m_videoSite.GetSafeHwnd() ? m_videoSite.m_hWnd : m_hWnd;
		if (Vdc && hwndVideo)
			Vdc->SetVideoWindow(hwndVideo);
	}
	DumpFilterGraph();

	// Filtervideooff2 / GetFrameRate は字幕・ffdshow 判定の後へ（汚染グラフで先に走ると落ちる）
	if(pGraphBuilder)pGraphBuilder->QueryInterface(IID_IMediaSeeking,(LPVOID *)&pMediaSeeking);

	DougaHangTrace(L"plays:Filtersdown:begin");
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	Filtersdown(pGraphBuilder, NULL);
	DougaHangTrace(L"plays:Filtersdown:end");

	// 何も足す前のクリーンなピン地図を取得 → 以降の再配線／Peel の基準
	DougaHangTrace(L"plays:PinMap capture");
	DougaCaptureVideoPinMap(pGraphBuilder);

	audionum = 0;
	if (iam) {
		audionum = CntPin2(iam);
	}

	if (GetStreamInfo(pGraphBuilder, audioStreams, videoStreams, subtitleStreams))
	{
		audionum = audioStreams.size();
	}
	// CntPin2 で字幕 0 でも GetStreamInfo 側に拾えていれば一覧へ反映
	if (streamMap.subtitleCount == 0 && !subtitleStreams.empty()) {
		int n = 0;
		for (size_t si = 0; si < subtitleStreams.size() && n < 40; ++si) {
			if (DougaIsOffSubtitleName(subtitleStreams[si].name)) {
				streamidxSubOff = (int)subtitleStreams[si].streamIndex;
				continue;
			}
			streamidx2[n] = (int)subtitleStreams[si].streamIndex;
			if (!subtitleStreams[si].name.IsEmpty())
				streamname2[n] = subtitleStreams[si].name;
			else
				streamname2[n].Format(L"%s %d",
					LL14(L"字幕", L"Subtitle", L"Sous-titres", L"Sottotitoli", L"Subtítulos", L"자막", L"字幕", L"ترجمة",
						L"Субтитры", L"Untertitel", L"Legendas", L"Ondertitel", L"Napisy", L"Altyazı"),
					n + 1);
			++n;
		}
		streamMap.subtitleCount = n;
		if (n > 0)
			streamMap.subtitleStart = streamidx2[0];
	}

	const BOOL hasSub = (streamMap.subtitleCount > 0);
	const LONG newRoute = hasSub ? 2 : 1;
	const LONG oldRoute = InterlockedExchange(&s_dougaPlayRoute, newRoute);
	{
		CString rm;
		rm.Format(L"plays:route %s (prev=%ld)", hasSub ? L"WITH-SUB" : L"NO-SUB", oldRoute);
		DougaHangTrace((LPCWSTR)rm);
	}

	// ピン分岐は DougaPins_ApplyRoute のみ（Rerender / Peel / Graph 再生成はしない）
	DougaPins_ApplyRoute(pGraphBuilder, hasSub);
	if (hasSub) {
		HWND hwndVideo = m_videoSite.GetSafeHwnd() ? m_videoSite.m_hWnd : m_hWnd;
		if (Vdc && hwndVideo)
			Vdc->SetVideoWindow(hwndVideo);
	}

	DougaHangTrace(L"plays:GetFrameRate:begin");
	{
		const double r2 = GetFrameRate(pGraphBuilder);
		if (r2 > 0.0) {
			rate = (float)((int)(r2 * 1000.0f)) / 1000.0f;
			rateflg = 0;
		}
	}

	DougaHangTrace(L"plays:GetFrameRate:end");
	if (prend)
		Filtervideooff2(pGraphBuilder);
	DougaHangTrace(L"plays:before AudioSelect");

	BOOL wiredSub = FALSE;
	if (savedata.audiost == 1 && (audionum > 1 || streamMap.subtitleCount > 0)) {
		CAudioSelect as;
		as.audioCount = (audionum > 0) ? audionum : 1;
		as.subCount = streamMap.subtitleCount;
		as.no = 0;
		as.subNo = -1;
		const INT_PTR rett = as.DoModal();
		if (rett != IDOK) {
			// ×／キャンセル: 再生開始せず動画を止めて閉じる
			stops();
			if (og && ::IsWindow(og->GetSafeHwnd()))
				og->PostMessage(WM_OGG_CLOSE_DOUGA, 0, 0);
			return;
		}
		if (as.no < 0) as.no = 0;
		if (as.no >= audionum) as.no = 0;
		st12 = as.no + 1;
		// 相対番号ではなく IAMStreamSelect 絶対 index（streamidx）で切替
		if (pGraphBuilder && iam && as.no < 40 && streamidx[as.no] >= 0) {
			HRESULT ehr = iam->Enable(streamidx[as.no], AMSTREAMSELECTENABLE_ENABLEONLY);
			if (FAILED(ehr))
				ehr = iam->Enable(streamidx[as.no], AMSTREAMSELECTENABLE_ENABLE);
		}
		// 字幕: subNo>=0 で選択、それ以外（ダブルクリック音声含む）はオフ
		if (iam) {
			if (as.subNo >= 0 && as.subNo < streamMap.subtitleCount && as.subNo < 40
				&& streamidx2[as.subNo] >= 0) {
				// InText + Enable は Stopped 中に完了（Run 後 Enable は途中再生で数秒無音）
				if (pGraphBuilder) {
					ConnectSubtitleWithDirectVobSub(pGraphBuilder);
					wiredSub = TRUE;
				}
				s_dougaDeferSubConnect = FALSE;
				s_dougaDeferSubStreamIdx = -1;
				{
					const int sidx = streamidx2[as.subNo];
					for (int i = 0; i < 40; ++i) {
						if (streamidx2[i] >= 0 && streamidx2[i] != sidx)
							iam->Enable(streamidx2[i], 0);
					}
					HRESULT shr = iam->Enable(sidx, AMSTREAMSELECTENABLE_ENABLE);
					if (FAILED(shr))
						shr = iam->Enable(sidx, AMSTREAMSELECTENABLE_ENABLEONLY);
					// ENABLEONLY で音声が落ちた場合に戻す
					int aidx = -1;
					if (as.no >= 0 && as.no < 40 && streamidx[as.no] >= 0)
						aidx = streamidx[as.no];
					else if (streamidx[0] >= 0)
						aidx = streamidx[0];
					if (aidx >= 0) {
						HRESULT ahr = iam->Enable(aidx, AMSTREAMSELECTENABLE_ENABLE);
						if (FAILED(ahr))
							iam->Enable(aidx, AMSTREAMSELECTENABLE_ENABLEONLY);
					}
				}
			} else {
				s_dougaDeferSubConnect = FALSE;
				s_dougaDeferSubStreamIdx = -1;
				BOOL offOk = FALSE;
				if (streamidxSubOff >= 0) {
					HRESULT ohr = iam->Enable(streamidxSubOff, AMSTREAMSELECTENABLE_ENABLEONLY);
					if (FAILED(ohr))
						ohr = iam->Enable(streamidxSubOff, AMSTREAMSELECTENABLE_ENABLE);
					offOk = SUCCEEDED(ohr);
				}
				if (!offOk) {
					for (int i = 0; i < streamMap.subtitleCount && i < 40; ++i) {
						if (streamidx2[i] >= 0)
							iam->Enable(streamidx2[i], 0);
					}
				}
			}
		}
	}
	(void)wiredSub;
	DougaHangTrace(L"plays:return");
}

void CDouga::Filtersdown(IGraphBuilder *pGraph,WCHAR *filter) 
{
	UNREFERENCED_PARAMETER(filter);
	if (!pGraph) return;
	IEnumFilters *pEnum = NULL;
	if (FAILED(pGraph->EnumFilters(&pEnum)) || !pEnum) return;

	IBaseFilter *pFilter = NULL;
	ULONG cFetched = 0;
	while (pEnum->Next(1, &pFilter, &cFetched) == S_OK) {
		FILTER_INFO FilterInfo = {};
		pFilter->QueryFilterInfo(&FilterInfo);
		CString s = FilterInfo.achName;
		if (FilterInfo.pGraph) {
			FilterInfo.pGraph->Release();
			FilterInfo.pGraph = NULL;
		}

		// Splitter / ファイルソースから IAMStreamSelect を1回だけ取る
		if (iam == NULL && (s.Find(L"\\") != -1 || s.Find(L"Splitter") != -1))
			pFilter->QueryInterface(IID_IAMStreamSelect, (void**)&iam);

		IEnumPins *pPins = NULL;
		if (SUCCEEDED(pFilter->EnumPins(&pPins)) && pPins) {
			IPin *pPin = NULL;
			while (pPins->Next(1, &pPin, 0) == S_OK) {
				IPin *pn = NULL;
				if (pPin->ConnectedTo(&pn) == S_OK && pn) {
					PIN_INFO pp = {};
					pn->QueryPinInfo(&pp);
					if (pp.pFilter) {
						FILTER_INFO fiPeer = {};
						pp.pFilter->QueryFilterInfo(&fiPeer);
						CString peer = fiPeer.achName;
						if (fiPeer.pGraph) {
							fiPeer.pGraph->Release();
							fiPeer.pGraph = NULL;
						}
						if (iam == NULL && peer.Find(L"Splitter") != -1)
							pp.pFilter->QueryInterface(IID_IAMStreamSelect, (void**)&iam);
						pp.pFilter->Release();
						pp.pFilter = NULL;
					}
					pn->Release();
				}
				pPin->Release();
			}
			pPins->Release();
		}
		pFilter->Release();
	}
	pEnum->Release();
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
	(void)filter;
	if (!pGraph) return;
	// 旧実装は Release 後にピン列挙しており、字幕あり→なしで落ちることがある。
	IBaseFilter* kill[64];
	int kn = 0;
	IEnumFilters* pEnum = NULL;
	if (FAILED(pGraph->EnumFilters(&pEnum)) || !pEnum) return;
	IBaseFilter* pFilter = NULL;
	ULONG cFetched = 0;
	while (kn < 64 && pEnum->Next(1, &pFilter, &cFetched) == S_OK) {
		FILTER_INFO fi;
		fi.pGraph = NULL;
		pFilter->QueryFilterInfo(&fi);
		CString s = fi.achName;
		if (fi.pGraph) fi.pGraph->Release();
		// 先頭一致のみ（Enhanced Video Renderer は除外）
		if (s.Find(L"Video Renderer") == 0 && s.Find(L"Enhanced") < 0) {
			kill[kn++] = pFilter;
		} else {
			pFilter->Release();
		}
	}
	pEnum->Release();
	for (int i = 0; i < kn; ++i) {
		pGraph->RemoveFilter(kill[i]);
		kill[i]->Release();
	}
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
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
						}
					}
					if(s.Right(11)==_T("d Renderer2")){
						if(st12==1 && pBasicAudio) pBasicAudio->Release();
						if(st12==1)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
						}
					}
					if(s.Right(11)==_T("d Renderer3")){
						if(st12==2 && pBasicAudio) pBasicAudio->Release();
						if(st12==2)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
						}
					}
					if(s.Right(11)==_T("d Renderer4")){
						if(st12==3 && pBasicAudio) pBasicAudio->Release();
						if(st12==3)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
						}
					}
					if(s.Right(11)==_T("d Renderer5")){
						if(st12==4 && pBasicAudio) pBasicAudio->Release();
						if(st12==4)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
						}
					}
					if(s.Right(11)==_T("d Renderer6")){
						if(st12==5 && pBasicAudio) pBasicAudio->Release();
						if(st12==5)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
						}
					}
					if(s.Right(11)==_T("d Renderer7")){
						if(st12==6 && pBasicAudio) pBasicAudio->Release();
						if(st12==6)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
						}
					}
					if(s.Right(11)==_T("d Renderer8")){
						if(st12==7 && pBasicAudio) pBasicAudio->Release();
						if(st12==7)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
						}
					}
					if(s.Right(11)==_T("d Renderer9")){
						if(st12==8 && pBasicAudio) pBasicAudio->Release();
						if(st12==8)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
						}
					}
					if(s.Right(11)==_T("d Renderer10")){
						if(st12==9 && pBasicAudio) pBasicAudio->Release();
						if(st12==9)
							pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio);
						else{
							IBasicAudio *pBasicAudio1=NULL;
							if (SUCCEEDED(pp.pFilter->QueryInterface(IID_IBasicAudio,(LPVOID *)&pBasicAudio1)) && pBasicAudio1) {
								pBasicAudio1->put_Volume(-10000);
								pBasicAudio1->Release();
							}
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
						PIN_INFO cpi;
						cpi.pFilter = NULL;
						pConnected->QueryPinInfo(&cpi);
						output += L" -> ";
						if (cpi.pFilter) {
							FILTER_INFO cfi;
							cpi.pFilter->QueryFilterInfo(&cfi);
							output += cfi.achName;
							output += L".";
							output += cpi.achName;
							if (cfi.pGraph) cfi.pGraph->Release();
							cpi.pFilter->Release();
						} else {
							output += L"Connected";
						}
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

	HWND hwndVideo = m_videoSite.GetSafeHwnd() ? m_videoSite.m_hWnd : m_hWnd;
	if (Vdc && hwndVideo)
		Vdc->SetVideoWindow(hwndVideo);
	if (pVideoWindow)pVideoWindow->put_Owner((OAHWND)hwndVideo);
	if (pVideoWindow)pVideoWindow->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS);
	if (pVideoWindow)pVideoWindow->put_MessageDrain((OAHWND)m_hWnd);
	if (pBasicVideo) { pBasicVideo->Release(); pBasicVideo = NULL; }
	if (pGraphBuilder)pGraphBuilder->QueryInterface(IID_IBasicVideo, (LPVOID*)&pBasicVideo);

	IBasicVideo2* pBasicVideo2 = NULL;
	if (pGraphBuilder)pGraphBuilder->QueryInterface(IID_IBasicVideo2, (LPVOID*)&pBasicVideo2);

	width = 0;
	// EVR GetNativeVideoSize は条件により返りにくい。IBasicVideo を優先。
	if (pBasicVideo) {
		pBasicVideo->get_VideoHeight(&height);
		pBasicVideo->get_VideoWidth(&width);
	}
	if (width == 0 && ev && Vdc) {
		SIZE a = { 0 }, b = { 0 };
		if (SUCCEEDED(Vdc->GetNativeVideoSize(&a, &b)) && a.cx > 0) {
			width = a.cx;
			height = a.cy;
		}
	} else if (width == 0 && ev && !Vdc) {
		ev = FALSE;
	}

	long actualWidth = width;
	long actualHeight = height;
	if (pBasicVideo2) {
		long aspectX = 0, aspectY = 0;
		HRESULT hr = pBasicVideo2->GetPreferredAspectRatio(&aspectX, &aspectY);
		if (SUCCEEDED(hr) && aspectX > 0 && aspectY > 0 && height > 0)
			actualWidth = (long)((double)height * aspectX / aspectY);
	}

	if (pVideoWindow)pVideoWindow->SetWindowPosition(0, 0, actualWidth, actualHeight);
	rc.top = 0; rc.left = 0; rc.right = actualWidth; rc.bottom = actualHeight;
	rcm.top = 0; rcm.left = 0; rcm.right = actualWidth; rcm.bottom = actualHeight;
	if (rcm.right == 704 && rcm.bottom == 480)
		rcm.bottom = 396;

	// サイズ未確定でも窓は出す（Stopped では get_VideoWidth=0 が多く、隠すと一生出ない）
	if (width <= 0 || height <= 0) {
		width = 640;
		height = 360;
		actualWidth = width;
		actualHeight = height;
		rc.right = rcm.right = width;
		rc.bottom = rcm.bottom = height;
	}
	if (pVideoWindow)pVideoWindow->put_Visible(OATRUE);
	if (savedata.gx != -10000) {
		SetWindowPos(NULL, savedata.gx, savedata.gy, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
	}
	switch (savedata.douga) {
	case 0:OnMenuitem32771(); break;
	case 1:OnMenuitem32772(); break;
	case 2:OnMenuitem32773(); break;
	case 3:OnMenuitem32774(); break;
	}
	if (InterlockedCompareExchange(&g_dougaAssocTopMost, 0, 0) != 0)
		::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	else if (savedata.dougatopmost)
		::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	ShowWindow(SW_SHOWNORMAL);
	::SetWindowPos(m_hWnd, savedata.dougatopmost ? HWND_TOPMOST : HWND_TOP,
		0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
	::BringWindowToTop(m_hWnd);
	RefreshBarMediaInfo();
	SetTimer(155, 400, NULL);
	SetTimer(1255, 200, NULL);

	DougaHangTrace(L"plays2:after ShowWindow");
	// mode=-2: UI 表示後に Grabber→Stretcher 張り替え
#if DOUGA_PITCHCORRECT_ENABLE
	if (mode == -2 && pGraphBuilder) {
		DougaHangTrace(L"plays2:PitchCorrect_Install:begin");
		HWND hwndOwner = GetSafeHwnd();
		if (DougaPitchCorrect_Install(pGraphBuilder, hwndOwner)) {
			DougaHangTrace(L"plays2:PitchCorrect_Install OK");
			SetTimer(1255, 40, NULL);
			if (pBasicAudio) { pBasicAudio->Release(); pBasicAudio = NULL; }
			DougaPitchCorrect_SetVolumeDsPos(savedata.dsvol);
			if (pMediaSeeking) {
				double r = 1.0;
				if (SUCCEEDED(pMediaSeeking->GetRate(&r)))
					DougaPitchCorrect_SetPlaybackRate(r);
			}
			// SetDefaultSyncSource は Install 内で済み。二重呼び出しは例外の原因になる
		} else {
			DougaHangTrace(L"plays2:PitchCorrect_Install FAILED");
			OutputDebugStringW(L"[DougaPitch] Install failed at plays2\n");
		}
		DougaHangTrace(L"plays2:PitchCorrect_Install:end");
	}
#else
	if (mode == -2 && pGraphBuilder)
		DougaHangTrace(L"plays2:PitchCorrect DISABLED (verify)");
#endif


	if (s_dougaDeferSubStreamIdx >= 0) {
		SetTimer(1260, 400, NULL);
		DougaHangTrace(L"plays2:arm subtitle Enable timer");
	}

	DougaHangTrace(L"plays2:return");
	if (!DougaPitchCorrect_IsActive()) {
		if (savedata.dsvol == -498) {
			if (pBasicAudio)pBasicAudio->put_Volume(-10000);
		} else {
			if (pBasicAudio)pBasicAudio->put_Volume((savedata.dsvol - 1) * 7);
		}
	}

	if (pBasicVideo2) pBasicVideo2->Release();
}

void CDouga::RefreshVideoSizeAfterRun()
{
	if (!GetSafeHwnd()) return;
	if (pBasicVideo) { pBasicVideo->Release(); pBasicVideo = NULL; }
	if (pGraphBuilder)pGraphBuilder->QueryInterface(IID_IBasicVideo, (LPVOID*)&pBasicVideo);
	long nw = 0, nh = 0;
	if (pBasicVideo) {
		pBasicVideo->get_VideoHeight(&nh);
		pBasicVideo->get_VideoWidth(&nw);
	}
	if (nw == 0 && ev && Vdc) {
		SIZE a = { 0 }, b = { 0 };
		if (SUCCEEDED(Vdc->GetNativeVideoSize(&a, &b)) && a.cx > 0) {
			nw = a.cx;
			nh = a.cy;
		}
	}
	if (nw <= 0 || nh <= 0) {
		ApplyVideoDest();
		return;
	}
	if (nw == width && nh == height) {
		ApplyVideoDest();
		return;
	}
	width = nw;
	height = nh;
	long actualWidth = nw;
	long actualHeight = nh;
	IBasicVideo2* pBasicVideo2 = NULL;
	if (pGraphBuilder)pGraphBuilder->QueryInterface(IID_IBasicVideo2, (LPVOID*)&pBasicVideo2);
	if (pBasicVideo2) {
		long aspectX = 0, aspectY = 0;
		HRESULT hrAr = pBasicVideo2->GetPreferredAspectRatio(&aspectX, &aspectY);
		if (SUCCEEDED(hrAr) && aspectX > 0 && aspectY > 0 && height > 0)
			actualWidth = (long)((double)height * aspectX / aspectY);
		pBasicVideo2->Release();
	}
	rc.right = rcm.right = actualWidth;
	rc.bottom = rcm.bottom = actualHeight;
	if (pVideoWindow)pVideoWindow->SetWindowPosition(0, 0, actualWidth, actualHeight);
	switch (savedata.douga) {
	case 0:OnMenuitem32771(); break;
	case 1:OnMenuitem32772(); break;
	case 2:OnMenuitem32773(); break;
	case 3:OnMenuitem32774(); break;
	}
	ApplyVideoDest();
}

extern REFTIME aa2,aa;
extern int ps;

// Pause/Run は非同期。EVR や字幕フィルタは完了に呼び出し側のメッセージ処理を
// 必要とするため、待つ間もメッセージを回す。入力とタイマーは回さない
// （再入で再生開始をやり直してしまう）。
BOOL DougaPumpWaitState(OAFilterState want, DWORD timeoutMs)
{
	static LONG s_inWait = 0;
	if (InterlockedCompareExchange(&s_inWait, 1, 0) != 0) {
		DougaHangTrace(L"WaitState:REENTRY rejected");
		return FALSE;
	}
	const DWORD t0 = GetTickCount();
	BOOL ok = FALSE;
	OAFilterState lastSt = -1;
	HRESULT lastHr = E_FAIL;
	for (;;) {
		IMediaControl* mc = pMediaControl;
		if (!mc) break;
		OAFilterState st = State_Stopped;
		const HRESULT hr = mc->GetState(10, &st);
		lastSt = st;
		lastHr = hr;
		if (SUCCEEDED(hr) && hr != VFW_S_STATE_INTERMEDIATE && st == want) {
			ok = TRUE;
			break;
		}
		// 検証されない WM_PAINT は PeekMessage が返し続けるので必ず上限を切る
		MSG m;
		for (int n = 0; n < 32; ++n) {
			if (!::PeekMessage(&m, NULL, 0, WM_KEYFIRST - 1, PM_REMOVE) &&
				!::PeekMessage(&m, NULL, WM_MOUSELAST + 1, 0xFFFFFFFF, PM_REMOVE))
				break;
			if (m.message == WM_QUIT) {
				::PostQuitMessage((int)m.wParam);
				InterlockedExchange(&s_inWait, 0);
				return FALSE;
			}
			::TranslateMessage(&m);
			::DispatchMessage(&m);
		}
		// WM_TIMER は回さない（再入する）ので、PCM のドレインだけ直接叩く。
		// これを止めると待っている間ぶん音が出ず、そのまま無音の穴になる。
		DougaPitchCorrect_Poll();
		if (GetTickCount() - t0 >= timeoutMs)
			break;
	}
	if (!ok) {
		WCHAR msg[160];
		swprintf_s(msg, L"WaitState:TIMEOUT want=%d last=%d hr=0x%08X after=%lums",
			(int)want, (int)lastSt, (unsigned)lastHr, (unsigned long)(GetTickCount() - t0));
		DougaHangTrace(msg);
	}
	InterlockedExchange(&s_inWait, 0);
	return ok;
}

// Run 完了まで待つ。待たずに次へ進むと、環境によっては Paused のまま
// 1 枚目だけ表示され「窓は出るが再生されない」状態になる。
BOOL DougaRunGraphAndWait()
{
	if (!pMediaControl) {
		DougaHangTrace(L"RunGraph:no MediaControl");
		return FALSE;
	}
	DougaHangTrace(L"RunGraph:Run:begin");
	HRESULT hr = pMediaControl->Run();
	{
		WCHAR m[96];
		swprintf_s(m, L"RunGraph:Run:end hr=0x%08X", (unsigned)hr);
		DougaHangTrace(m);
	}
	// 待っている間は入力メッセージを配送しないので、粘るほどボタンが死ぬ。
	// まず短く確定を狙う。
	if (DougaPumpWaitState(State_Running, 300)) {
		DougaHangTrace(L"RunGraph:Running OK");
		return TRUE;
	}
	// Run が成功していて遷移中なら、そのまま Running へ着地する。AVI Splitter の
	// ように充填が長い環境ではここで数秒 INTERMEDIATE のまま留まるため、
	// 確定を待つと再生直後の操作を丸ごと食ってしまう。作り直しが要るのは
	// Run 自体が失敗した時か、遷移もせず Stopped/Paused で固まっている時だけ。
	if (SUCCEEDED(hr)) {
		OAFilterState st = State_Stopped;
		const HRESULT hs = pMediaControl->GetState(0, &st);
		if (st == State_Running) {
			DougaHangTrace(L"RunGraph:Running OK");
			return TRUE;
		}
		if (hs == VFW_S_STATE_INTERMEDIATE) {
			WCHAR m[128];
			swprintf_s(m, L"RunGraph:accept transitioning st=%d hs=0x%08X", (int)st, (unsigned)hs);
			DougaHangTrace(m);
			DougaTraceFilterStates(pGraphBuilder, L"RunState");
			return TRUE;
		}
	}
	DougaHangTrace(L"RunGraph:retry Run");
	if (pMediaControl)
		pMediaControl->Run();
	if (DougaPumpWaitState(State_Running, 800)) {
		DougaHangTrace(L"RunGraph:Running OK (retry)");
		return TRUE;
	}
	WCHAR msg[128];
	swprintf_s(msg, L"RunGraph:FAILED not Running hr=0x%08X", (unsigned)hr);
	DougaHangTrace(msg);
	OutputDebugStringW(msg);
	return FALSE;
}

void CDouga::seek(LONGLONG l)
{
	// SetPositions より先に Grabber CB を止める（再生中シーク／途中再生のデッドロック防止）
	DougaPitchCorrect_PauseForGraphSeek();
	{
		WCHAR m[128];
		swprintf_s(m, L"seek:enter pos=%lldms", (long long)(l / 10000));
		DougaHangTrace(m);
	}
	HRESULT hrSeek = E_FAIL;
	if (pMediaSeeking) {
		pMediaSeeking->SetTimeFormat(&TIME_FORMAT_MEDIA_TIME);
		REFERENCE_TIME rtpos = l;
		hrSeek = pMediaSeeking->SetPositions(&rtpos, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
	}
	if (FAILED(hrSeek) && pMediaPosition)
		pMediaPosition->put_CurrentPosition((REFTIME)l / 10000000.0);
	DougaPitchCorrect_OnSeek((double)l / 10000000.0);
	{
		WCHAR m[96];
		swprintf_s(m, L"seek:SetPositions hr=0x%08X", (unsigned)hrSeek);
		DougaHangTrace(m);
	}
	DougaHangTrace(L"seek:return");
}

void CDouga::pause(int a)
{
	if(a==0)
	{
		pMediaControl->Pause();
		DougaPitchCorrect_SetPaused(TRUE);
		ps=1;og->m_ps.SetWindowText(LL14(L"再開", L"Resume", L"Reprendre", L"Riprendi", L"Reanudar", L"재개", L"恢复", L"استئناف", L"Продолжить", L"Fortsetzen", L"Retomar", L"Hervatten", L"Wznów", L"Sürdür"));
		og->SyncPauseButtonUi();
	}else{
		pMediaControl->Run();
		DougaPitchCorrect_SetPaused(FALSE);
		ps=0;og->m_ps.SetWindowText(LL14(L"一時停止", L"Pause", L"Pause", L"Pausa", L"Pausa", L"일시 정지", L"暂停", L"إيقاف مؤقت", L"Пауза", L"Pause", L"Pausar", L"Pauzeren", L"Wstrzymaj", L"Duraklat"));
		og->SyncPauseButtonUi();
	}
}

void CDouga::stops()
{
	DougaHangTrace(L"stops:enter");
	if (!pGraphBuilder && !pMediaControl) {
		ev = FALSE;
		DougaColdResetPlaybackGlobals();
		DougaHangTrace(L"stops:return (empty graph)");
		return;
	}
	CRect r,rr;
	if (GetSafeHwnd()) {
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
	audioStreams.clear();
	videoStreams.clear();
	subtitleStreams.clear();
	streamMap.videoStart = streamMap.audioStart = streamMap.subtitleStart = -1;
	streamMap.videoCount = streamMap.audioCount = streamMap.subtitleCount = 0;
	DougaColdResetPlaybackGlobals();
	// 字幕あり後の COM リサイクルは次の plays 冒頭 DougaRecycleComApartmentIfNeeded で行う。
	if(mode==-14) Sleep(500);
	DougaHangTrace(L"stops:return");
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
		if (m_bar.IsBarReady())
			m_bar.SyncSeekVol();
		DougaPitchCorrect_Poll();
	}
	if(nIDEvent==155){
		KillTimer(155);
		if (InterlockedCompareExchange(&g_dougaAssocTopMost, 0, 0) != 0) {
			InterlockedExchange(&g_dougaAssocTopMost, 0);
			if (savedata.dougatopmost)
				ApplyDougaTopmost();
			else
				::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		} else if (!savedata.dougatopmost) {
			::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}
		SetFocus();
		extern int ps;
		if (pMediaControl && ps != 1) {
			OAFilterState st = State_Stopped;
			pMediaControl->GetState(0, &st);
			if (st != State_Running) {
				pMediaControl->Run();
				ApplyVideoDest();
			}
		}
		DougaPitchCorrect_EnableCallback();
	}
	if (nIDEvent == 1260) {
		KillTimer(1260);
		s_dougaDeferSubConnect = FALSE;
		const int idx = s_dougaDeferSubStreamIdx;
		s_dougaDeferSubStreamIdx = -1;
		if (iam && idx >= 0) {
			for (int i = 0; i < 40; ++i) {
				if (streamidx2[i] >= 0 && streamidx2[i] != idx)
					iam->Enable(streamidx2[i], 0);
			}
			// ENABLEONLY は他ストリーム（音声）を落とすので最後の手段
			HRESULT shr = iam->Enable(idx, AMSTREAMSELECTENABLE_ENABLE);
			if (FAILED(shr))
				shr = iam->Enable(idx, AMSTREAMSELECTENABLE_ENABLEONLY);
			CString msg;
			msg.Format(L"plays2:deferred subtitle Enable idx=%d hr=0x%08X", idx, (unsigned)shr);
			DougaHangTrace((LPCWSTR)msg);
			// 字幕切替後は必ず音声を戻す（st12 未設定でも先頭音声）
			int aidx = -1;
			if (st12 > 0 && st12 <= 40 && streamidx[st12 - 1] >= 0)
				aidx = streamidx[st12 - 1];
			else if (streamidx[0] >= 0)
				aidx = streamidx[0];
			else if (streamMap.audioStart >= 0)
				aidx = streamMap.audioStart;
			if (aidx >= 0) {
				HRESULT ahr = iam->Enable(aidx, AMSTREAMSELECTENABLE_ENABLE);
				if (FAILED(ahr))
					ahr = iam->Enable(aidx, AMSTREAMSELECTENABLE_ENABLEONLY);
				CString amsg;
				amsg.Format(L"plays2:re-enable audio idx=%d hr=0x%08X", aidx, (unsigned)ahr);
				DougaHangTrace((LPCWSTR)amsg);
			}
		}
	}
	if(nIDEvent==3366){
		if (!savedata.fs) {
			RestoreDougaCursor();
		} else {
			mousecnt++;if(mousecnt1==0 && mousecnt>3){
				mousecnt1=1;
				int j;
				for(;;){
					j=ShowCursor(FALSE);if(j<0) break;
				}
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
	RECT r;
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
	const int barChrome = GetBarHeight();
	switch(fwSide){
		case WMSZ_TOP:
		case WMSZ_BOTTOM:
			pRect->right=pRect->left+(int)(width*_y1)-(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION));
			break;
		case WMSZ_LEFT:
        case WMSZ_RIGHT:
			pRect->bottom=pRect->top+(int)(height*_x1)+(GetSystemMetrics(SM_CYSIZEFRAME)+::GetSystemMetrics(SM_CYCAPTION))+barChrome;
			break;
		case WMSZ_BOTTOMRIGHT:
			if(((double)width<(double)height))
				pRect->right=pRect->left+(int)(width*_y1);
			else
				pRect->bottom=pRect->top+(int)(height*_x1)+barChrome;
			break;
		case    WMSZ_TOPLEFT:
			if(((double)width<(double)height))
                pRect->left=pRect->right-(int)(width*_y1);
            else
                pRect->top=pRect->bottom-(int)(height*_x1)-barChrome;
			break;
 		case    WMSZ_TOPRIGHT:
			if(((double)width<(double)height))
				pRect->right=pRect->left+(int)(width*_y1);
            else
                pRect->top=pRect->bottom-(int)(height*_x1)-barChrome;
			break;
		case    WMSZ_BOTTOMLEFT:
			if(((double)width<(double)height))
                pRect->left=pRect->right-(int)(width*_y1);
            else
				pRect->bottom=pRect->top+(int)(height*_x1)+barChrome;
			break;
	}
	savedata.p.top=pRect->top;
	savedata.p.left=pRect->left;
	savedata.p.bottom=pRect->bottom;
	savedata.p.right=pRect->right;
	savedata.douga=3;
	// 実サイズ反映は WM_SIZE → ApplyVideoDest。ここではアスペクト調整のみ。
}



void CDouga::OnSize(UINT nType, int cx, int cy) 
{
	CFrameWnd::OnSize(nType, cx, cy);
	if(si==1){
		si=0;
		ApplyVideoDest();
	} else if (!m_applyBusy) {
		ApplyVideoDest();
	}
}

void CDouga::OnEnterSizeMove()
{
	m_inSizeMove = 1;
	CFrameWnd::OnEnterSizeMove();
}

void CDouga::OnExitSizeMove()
{
	m_inSizeMove = 0;
	if (!savedata.fs && GetSafeHwnd()) {
		RECT r;
		GetWindowRect(&r);
		int wx = r.left, wy = r.top, ww = r.right - r.left, wh = r.bottom - r.top;
		const int chromeH = GetSystemMetrics(SM_CYSIZEFRAME) + ::GetSystemMetrics(SM_CYCAPTION) + GetBarHeight();
		const int ox = wx, oy = wy, ow = ww, oh = wh;
		DougaFitOuterToWorkArea(m_hWnd, wx, wy, ww, wh, chromeH);
		if (wx != ox || wy != oy || ww != ow || wh != oh) {
			SetWindowPos(NULL, wx, wy, ww, wh, SWP_NOOWNERZORDER | SWP_NOACTIVATE);
			savedata.p.top = wy;
			savedata.p.left = wx;
			savedata.p.bottom = wy + wh;
			savedata.p.right = wx + ww;
			savedata.douga = 3;
		}
	}
	ApplyVideoDest(); // ここで LayoutBar を一回だけ
	if (m_bar.IsBarReady())
		m_bar.Invalidate(TRUE);
	CFrameWnd::OnExitSizeMove();
}

void CDouga::OnClose() 
{
	DestroyHelpSheet();
	// gamenkill からの閉じは通常 Destroy。ユーザーの×は本体に委譲（ポインタ整合のため）
	if (!m_closingByMain && og && ::IsWindow(og->GetSafeHwnd()) && pMainFrame1 == this) {
		og->PostMessage(WM_OGG_CLOSE_DOUGA, 0, 0);
		return;
	}
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
		ShowDougaContextMenu(point);
		return;
	}
	CFrameWnd::OnNcRButtonDown(nHitTest, point);
}

// ============================================================================
// mode=-2 音程維持: SetRate(R) + Stretcher(timeRatio=1/R, pitchScale=1) + 専用 DirectSound
//
// SampleGrabber は SetRate(R) 後、壁時計あたり約 R 倍の原音程 PCM を渡す。
// timeRatio=1 のまま DS(Fs) に書くと書込速度≠再生速度になり、繰り返し／ノイズになる。
// timeRatio=1/R で壁時計あたり Fs サンプルに直し、pitchScale=1 で音程を維持する。
//
// 出力 DS は 16bit 固定（EAC→デコーダが float/24 でもここで変換）。
// リアルタイム PitchCorrect 用。映画再生の実用品質としては十分。
// ============================================================================
namespace {

static const double kDougaPitchEps = 0.02;
// Play 開始前に貯める量。グラフ起動直後のデコーダはバースト後に一度息継ぎするので、
// ここが小さいと鳴り出した直後に必ず一度枯れる（＝一瞬鳴って無音）。
static const DWORD kDougaPcPrebufferMs = 200;
static const DWORD kDougaPcPrebufferMsMulti = 320;
// 2ch: 短めで低遅延。多ch: デコードバーストで先行が膨らむ。
// ここで捨てると「届いたサンプルをロスト→音声だけ先へ」となりセリフ位置が狂う。
static const DWORD kDougaPcMaxAheadMs = 800;
static const DWORD kDougaPcMaxAheadMsMulti = 2500;
static const DWORD kDougaPcGapFlushMs = 350;   // PCM 途切れ → flush＋ドレイン開始
static const DWORD kDougaPcStartGraceMs = 4000; // 字幕/ffd 起動直後のギャップで DS を止めない
static const size_t kDougaPcQMax = 96;

struct DougaPcQItem {
	std::vector<BYTE> data;
};

struct DougaPcState {
	LONG refCb;
	IGraphBuilder* graph; // weak（Install 中のグラフ。Shutdown で RemoveFilter 用）
	IBaseFilter* grabberF;
	IBaseFilter* nullF;
	IBaseFilter* oldRenderer;
	ISampleGrabber* grabber;
	IDirectSound8* ds;
	IDirectSoundBuffer* dsb;
	RubberBand::RubberBandStretcher* shifter;
	CRITICAL_SECTION cs;   // 状態（rate/shifter/playing）のみ。DS 操作では握らない
	CRITICAL_SECTION dsCs; // DS バッファ書き込み専用。UI は inCallback==0 のときだけ触る
	BOOL csInit;
	BOOL dsCsInit;
	BOOL installed;
	BOOL playing;
	BOOL dsRunning;
	BOOL endFlushed;
	BOOL draining;       // EOS 後、残量だけ再生して Stop
	BOOL downmix;        // Grabber 多ch → DS/RB は stereo
	DWORD lastPcmTick;
	DWORD drainTick;
	DWORD drainAheadBytes; // flush 時点の残りバイト（ループ防止の上限）
	DWORD startTick;     // Install 時刻（起動グレース）
	double rate;
	int sampleRate;
	int channels;        // DS / RubberBand / EQ 側
	int srcChannels;     // SampleGrabber 入力
	int bits;            // DS 出力ビット（常に 16）
	int srcBits;         // Grabber 実ビット（float/24 可）
	BOOL srcFloat;       // IEEE float 入力
	int blockAlign;      // 出力（DS）側
	int srcBlockAlign;   // 入力側（実フレームバイト）
	DWORD channelMask;
	DWORD bufBytes;
	DWORD writePos;
	BOOL writePosValid;
	DWORD prebufferBytes;
	volatile LONG inCallback; // BufferCB 滞在中（UI は Stop 前に待つ）
	volatile LONG shuttingDown; // Shutdown 中は CB を即抜ける（SetCallback 待ち回避）
	BOOL cbArmed;        // SetCallback 済み
	DWORD lastWriteTick;
	std::deque<DougaPcQItem> pcmQ;
	std::vector<float> inFlat;
	std::vector<float> outFlat;
	std::vector<float> interleavedTmp;
	std::vector<float*> inPtrs;
	std::vector<float*> outPtrs;
	std::vector<BYTE> pcmOut;
	std::vector<BYTE> downmixBuf;
	volatile LONG eqNeedReset; // シーク後の次PCM1回だけ equaliser reset
};

static DougaPcState g_pc = {};

// プロセス寿命で 1 回だけ作る。Install/Shutdown ごとに作り直すと、
// 取り残された Grabber スレッドが待っている最中に破棄してしまう。
static void DougaPcEnsureCs()
{
	if (g_pc.csInit) return;
	InitializeCriticalSection(&g_pc.cs);
	InitializeCriticalSection(&g_pc.dsCs);
	g_pc.csInit = TRUE;
	g_pc.dsCsInit = TRUE;
}

static void DougaPcEqAndRemote(BYTE* data, DWORD bytes)
{
	if (!data || bytes == 0) return;
	EqualiserSetFormatVolContext(0, FALSE);
	const BOOL reset = (InterlockedExchange(&g_pc.eqNeedReset, 0) != 0) ? TRUE : FALSE;
	equaliserBank(0, data, (int)bytes, reset, g_pc.bits, g_pc.channels, g_pc.sampleRate);
	g_ds_pcm_ch = g_pc.channels;
	g_ds_pcm_bits = g_pc.bits;
	g_ds_pcm_rate = g_pc.sampleRate;
	::g_outBytesPerFrame = g_pc.blockAlign;
	MpRemoteWritePcm(data, (int)bytes);
}

static void DougaPcFreeMediaType(AM_MEDIA_TYPE& mt)
{
	if (mt.cbFormat) {
		CoTaskMemFree(mt.pbFormat);
		mt.cbFormat = 0;
		mt.pbFormat = NULL;
	}
	if (mt.pUnk) {
		mt.pUnk->Release();
		mt.pUnk = NULL;
	}
}

static LONG DougaPcDsVolFromPos(int dsPos)
{
	if (dsPos <= -498) return -10000;
	LONG v = (dsPos - 1) * 7;
	if (v < -10000) v = -10000;
	if (v > 0) v = 0;
	return v;
}

static void DougaPcApplyVolume_NoLock(int dsPos)
{
	if (!g_pc.dsb) return;
	g_pc.dsb->SetVolume(DougaPcDsVolFromPos(dsPos));
}

static DWORD DougaPcBytesAhead(DWORD writePos, DWORD playPos, DWORD bufBytes)
{
	if (bufBytes == 0) return 0;
	if (writePos >= playPos)
		return writePos - playPos;
	return bufBytes - playPos + writePos;
}

static DWORD DougaPcMsToBytes(DWORD ms)
{
	if (g_pc.sampleRate <= 0 || g_pc.blockAlign <= 0) return 0;
	DWORD b = (DWORD)(((__int64)g_pc.sampleRate * g_pc.blockAlign * ms) / 1000);
	// blockAlign に揃える
	if (g_pc.blockAlign > 0)
		b -= b % (DWORD)g_pc.blockAlign;
	return b;
}

static DWORD DougaPcMaxAheadBytes()
{
	const DWORD ms = (g_pc.channels > 2) ? kDougaPcMaxAheadMsMulti : kDougaPcMaxAheadMs;
	return DougaPcMsToBytes(ms);
}

static void DougaPcSilenceAll_NoLock()
{
	if (!g_pc.dsb || g_pc.bufBytes == 0) return;
	void* p1 = NULL; void* p2 = NULL;
	DWORD b1 = 0, b2 = 0;
	HRESULT hr = g_pc.dsb->Lock(0, g_pc.bufBytes, &p1, &b1, &p2, &b2, 0);
	if (hr == DSERR_BUFFERLOST) {
		g_pc.dsb->Restore();
		hr = g_pc.dsb->Lock(0, g_pc.bufBytes, &p1, &b1, &p2, &b2, 0);
	}
	if (FAILED(hr) || !p1) return;
	memset(p1, 0, b1);
	if (p2 && b2) memset(p2, 0, b2);
	g_pc.dsb->Unlock(p1, b1, p2, b2);
	g_pc.writePos = 0;
	g_pc.writePosValid = TRUE;
	g_pc.prebufferBytes = 0;
}

static void DougaPcWaitCallbackGone()
{
	const int lim = (InterlockedCompareExchange(&g_pc.shuttingDown, 0, 0) != 0) ? 150 : 400;
	for (int i = 0; i < lim; ++i) {
		if (InterlockedCompareExchange(&g_pc.inCallback, 0, 0) == 0)
			break;
		Sleep(1);
	}
}

static void DougaPcStopDsOutsideCallback()
{
	DougaPcWaitCallbackGone();
	if (g_pc.dsCsInit)
		EnterCriticalSection(&g_pc.dsCs);
	if (g_pc.dsb)
		g_pc.dsb->Stop();
	DougaPcSilenceAll_NoLock();
	g_pc.dsRunning = FALSE;
	g_pc.draining = FALSE;
	g_pc.drainAheadBytes = 0;
	if (g_pc.dsCsInit)
		LeaveCriticalSection(&g_pc.dsCs);
	if (g_pc.csInit) {
		EnterCriticalSection(&g_pc.cs);
		g_pc.pcmQ.clear();
		LeaveCriticalSection(&g_pc.cs);
	}
}

// [writePos → play) を無音化（有効区間は残す）— 呼び出し元が dsCs 保持
static void DougaPcSilenceStaleKeepValid_NoLock()
{
	if (!g_pc.dsb || !g_pc.writePosValid || g_pc.bufBytes == 0) return;
	DWORD play = 0, dsWrite = 0;
	if (FAILED(g_pc.dsb->GetCurrentPosition(&play, &dsWrite)))
		return;
	const DWORD ahead = DougaPcBytesAhead(g_pc.writePos, play, g_pc.bufBytes);
	if (ahead >= g_pc.bufBytes) return;
	DWORD stale = g_pc.bufBytes - ahead;
	if (stale <= (DWORD)g_pc.blockAlign * 8) return;
	stale -= (DWORD)g_pc.blockAlign * 4;
	void* p1 = NULL; void* p2 = NULL;
	DWORD b1 = 0, b2 = 0;
	HRESULT hr = g_pc.dsb->Lock(g_pc.writePos, stale, &p1, &b1, &p2, &b2, 0);
	if (hr == DSERR_BUFFERLOST) {
		g_pc.dsb->Restore();
		hr = g_pc.dsb->Lock(g_pc.writePos, stale, &p1, &b1, &p2, &b2, 0);
	}
	if (FAILED(hr) || !p1) return;
	memset(p1, 0, b1);
	if (p2 && b2) memset(p2, 0, b2);
	g_pc.dsb->Unlock(p1, b1, p2, b2);
}

// 供給が間に合わず再生位置が書込位置を追い越した状態から復帰する。
// 貯め直しから始めるので穴はプリバッファ分だけで済む。
static void DougaPcResyncUnderrun_NoLock()
{
	if (!g_pc.dsb) return;
	g_pc.dsb->Stop();
	DougaPcSilenceAll_NoLock(); // writePos=0 / writePosValid=TRUE / prebufferBytes=0
	g_pc.dsRunning = FALSE;
	g_pc.lastWriteTick = GetTickCount();
	DougaHangTrace(L"PitchDs:underrun resync");
}

// DS が再生位置よりどれだけ先行しているかで書き込み可否を返す — 呼び出し元が dsCs 保持
static BOOL DougaPcHasRoom_NoLock(DWORD bytes)
{
	if (!g_pc.dsb || !g_pc.dsRunning || g_pc.bufBytes == 0) return TRUE;
	DWORD play = 0, dsWrite = 0;
	if (FAILED(g_pc.dsb->GetCurrentPosition(&play, &dsWrite)))
		return FALSE;
	const DWORD ahead = DougaPcBytesAhead(g_pc.writePos, play, g_pc.bufBytes);
	// play..dsWrite は DS が今まさに読み出している区間。
	const DWORD lead = DougaPcBytesAhead(dsWrite, play, g_pc.bufBytes);
	DWORD maxAhead = DougaPcMaxAheadBytes();
	if (maxAhead == 0 || maxAhead > g_pc.bufBytes)
		maxAhead = g_pc.bufBytes;
	// 追い越されると先行量がバッファ一周ぶんに化ける。これを「先行しすぎ」と
	// 誤認して書き込みを止めると、再生位置が一周して戻るまで無音が続く
	// (バッファは 3 秒なので実測 1〜2 秒の穴になる)。
	if (ahead <= lead || ahead > maxAhead + DougaPcMsToBytes(120)) {
		DougaPcResyncUnderrun_NoLock();
		return TRUE;
	}
	if (ahead + bytes > maxAhead)
		return FALSE;
	if (ahead + bytes >= g_pc.bufBytes - (DWORD)g_pc.blockAlign * 8)
		return FALSE;
	return TRUE;
}

// 呼び出し元が dsCs を保持すること
static BOOL DougaPcWritePcm_NoLock(const BYTE* data, DWORD bytes)
{
	if (!g_pc.dsb || !data || bytes == 0) return FALSE;
	if (g_pc.blockAlign > 0)
		bytes -= bytes % (DWORD)g_pc.blockAlign;
	if (bytes == 0) return FALSE;

	if (!g_pc.writePosValid)
		DougaPcSilenceAll_NoLock();

	if (!DougaPcHasRoom_NoLock(bytes))
		return FALSE;

	void* p1 = NULL; void* p2 = NULL;
	DWORD b1 = 0, b2 = 0;
	HRESULT hr = g_pc.dsb->Lock(g_pc.writePos, bytes, &p1, &b1, &p2, &b2, 0);
	if (hr == DSERR_BUFFERLOST) {
		g_pc.dsb->Restore();
		hr = g_pc.dsb->Lock(g_pc.writePos, bytes, &p1, &b1, &p2, &b2, 0);
	}
	if (FAILED(hr) || !p1) return FALSE;
	memcpy(p1, data, b1);
	if (p2 && b2) memcpy(p2, data + b1, b2);
	g_pc.dsb->Unlock(p1, b1, p2, b2);
	g_pc.writePos = (g_pc.writePos + bytes) % g_pc.bufBytes;

	if (!g_pc.dsRunning && g_pc.playing) {
		g_pc.lastWriteTick = GetTickCount();
		g_pc.prebufferBytes += bytes;
		const DWORD needMs = (g_pc.channels > 2) ? kDougaPcPrebufferMsMulti : kDougaPcPrebufferMs;
		if (g_pc.prebufferBytes >= DougaPcMsToBytes(needMs)) {
			g_pc.dsb->SetCurrentPosition(0);
			if (SUCCEEDED(g_pc.dsb->Play(0, 0, DSBPLAY_LOOPING)))
				g_pc.dsRunning = TRUE;
		}
	}
	return TRUE;
}

static void DougaPcApplyRateToShifter_NoLock()
{
	if (!g_pc.shifter) return;
	const double r = (g_pc.rate > 0.05) ? g_pc.rate : 1.0;
	g_pc.shifter->setTimeRatio(1.0 / r);
	g_pc.shifter->setPitchScale(1.0);
}

static void DougaPcEnsureShifter_NoLock()
{
	if (g_pc.shifter) return;
	if (g_pc.sampleRate < 8000 || g_pc.channels < 1) return;
	try {
		const double r = (g_pc.rate > 0.05) ? g_pc.rate : 1.0;
		// 実時間向け Faster を優先（Finer はコールバックが長く UI を圧迫しやすい）
		g_pc.shifter = new RubberBand::RubberBandStretcher(
			(size_t)g_pc.sampleRate, (size_t)g_pc.channels,
			RubberBand::RubberBandStretcher::OptionProcessRealTime |
			RubberBand::RubberBandStretcher::OptionEngineFaster |
			RubberBand::RubberBandStretcher::OptionThreadingNever |
			RubberBand::RubberBandStretcher::OptionTransientsMixed |
			RubberBand::RubberBandStretcher::OptionPhaseLaminar |
			RubberBand::RubberBandStretcher::OptionFormantPreserved |
			RubberBand::RubberBandStretcher::OptionChannelsTogether,
			1.0 / r,
			1.0);
		g_pc.shifter->setDebugLevel(0);
		g_pc.shifter->setMaxProcessSize(8192);
		g_pc.inPtrs.resize((size_t)g_pc.channels);
		g_pc.outPtrs.resize((size_t)g_pc.channels);
	}
	catch (...) {
		g_pc.shifter = NULL;
		OutputDebugStringW(L"[DougaPitch] Stretcher create failed\n");
	}
}

static void DougaPcFloatToPcm16(const float* interleaved, size_t frames, int ch, BYTE* dst)
{
	const size_t n = frames * (size_t)ch;
	INT16* out = (INT16*)dst;
	for (size_t i = 0; i < n; ++i) {
		float s = interleaved[i];
		if (s > 1.f) s = 1.f;
		if (s < -1.f) s = -1.f;
		const int v = (int)floorf(s * 32767.f + 0.5f);
		out[i] = (INT16)((v > 32767) ? 32767 : (v < -32768 ? -32768 : v));
	}
}

// WAVEFORMATEXTENSIBLE 慣習順: FL FR FC LFE BL/SL BR/SR ...
static void DougaPcDownmixToStereo16(const INT16* src, long frames, int chIn, INT16* dst)
{
	const float cC = 0.70710678f;
	const float cS = 0.70710678f;
	const float cLfe = 0.5f;
	for (long i = 0; i < frames; ++i) {
		const INT16* s = src + i * chIn;
		float L = (float)s[0];
		float R = (chIn > 1) ? (float)s[1] : L;
		if (chIn >= 3) {
			const float c = cC * (float)s[2];
			L += c; R += c;
		}
		if (chIn >= 4) {
			const float lfe = cLfe * (float)s[3];
			L += lfe; R += lfe;
		}
		if (chIn >= 5) L += cS * (float)s[4];
		if (chIn >= 6) R += cS * (float)s[5];
		if (chIn >= 7) L += cS * (float)s[6];
		if (chIn >= 8) R += cS * (float)s[7];
		if (L > 32767.f) L = 32767.f;
		if (L < -32768.f) L = -32768.f;
		if (R > 32767.f) R = 32767.f;
		if (R < -32768.f) R = -32768.f;
		dst[i * 2] = (INT16)L;
		dst[i * 2 + 1] = (INT16)R;
	}
}

// Grabber は多chで float/24bit になりやすい。DS/RB は 16bit PCM 固定。
static BOOL DougaPcSrcToPcm16(const BYTE* src, long frames, int ch, int srcBits, BOOL srcFloat, INT16* dst)
{
	if (!src || !dst || frames <= 0 || ch < 1) return FALSE;
	const long n = frames * (long)ch;
	if (srcFloat) {
		const float* f = (const float*)src;
		for (long i = 0; i < n; ++i) {
			float s = f[i];
			if (s > 1.f) s = 1.f;
			if (s < -1.f) s = -1.f;
			const int v = (int)floorf(s * 32767.f + 0.5f);
			dst[i] = (INT16)((v > 32767) ? 32767 : (v < -32768 ? -32768 : v));
		}
		return TRUE;
	}
	if (srcBits == 16) {
		memcpy(dst, src, (size_t)n * sizeof(INT16));
		return TRUE;
	}
	if (srcBits == 32) {
		const INT32* s32 = (const INT32*)src;
		for (long i = 0; i < n; ++i)
			dst[i] = (INT16)(s32[i] >> 16);
		return TRUE;
	}
	if (srcBits == 24) {
		const int bytesPerSamp = (g_pc.srcBlockAlign > 0 && ch > 0)
			? (g_pc.srcBlockAlign / ch) : 3;
		if (bytesPerSamp >= 4) {
			const INT32* s32 = (const INT32*)src;
			for (long i = 0; i < n; ++i)
				dst[i] = (INT16)(s32[i] >> 16);
			return TRUE;
		}
		for (long i = 0; i < n; ++i) {
			const BYTE* b = src + i * 3;
			const int v = (int)((INT8)b[2] << 16) | (b[1] << 8) | b[0];
			dst[i] = (INT16)(v >> 8);
		}
		return TRUE;
	}
	return FALSE;
}

static BOOL DougaPcIsIeeeFloatWfx(const WAVEFORMATEX* wfx, const WAVEFORMATEXTENSIBLE* we)
{
	if (!wfx) return FALSE;
	if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return TRUE;
	if (we && wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		static const GUID kFloat = { 0x00000003, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
		return IsEqualGUID(we->SubFormat, kFloat) ? TRUE : FALSE;
	}
	return FALSE;
}

static DWORD DougaPcDefaultChannelMask(int ch)
{
	// AudioUpscaler の speaker_layout に依存せず、標準マスクを使う
	switch (ch) {
	case 1: return 0x4; // SPEAKER_FRONT_CENTER
	case 2: return 0x3; // FL|FR
	case 3: return 0x7; // FL|FR|FC
	case 4: return 0x33; // FL|FR|BL|BR
	case 5: return 0x37; // FL|FR|FC|BL|BR
	case 6: return 0x3F; // 5.1
	case 7: return 0x13F;
	case 8: return 0x63F; // 7.1
	default:
		if (ch <= 0) return 0x3;
		return DirectSoundChannelMaskForOutput(ch, 0);
	}
}

static void DougaPcAttachSystemClock(IGraphBuilder* graph)
{
	// DS レンダラを外すと SetDefaultSyncSource が EVR を選び、
	// 字幕あり(ffd) だと映像プリロール待ちで数秒無音になる。
	if (!graph) return;
	IMediaFilter* mf = NULL;
	if (FAILED(graph->QueryInterface(IID_IMediaFilter, (void**)&mf)) || !mf) {
		graph->SetDefaultSyncSource();
		return;
	}
	IReferenceClock* clock = NULL;
	HRESULT hr = CoCreateInstance(CLSID_SystemClock, NULL, CLSCTX_INPROC_SERVER,
		IID_IReferenceClock, (void**)&clock);
	if (SUCCEEDED(hr) && clock) {
		mf->SetSyncSource(clock);
		clock->Release();
		DougaHangTrace(L"PitchInstall:SystemClock");
	} else {
		graph->SetDefaultSyncSource();
		DougaHangTrace(L"PitchInstall:SetDefaultSyncSource fallback");
	}
	mf->Release();
}

static void DougaPcRetrieveAndWrite_NoLock()
{
	// 注意: 呼び出し元は cs を握らないこと。dsCs のみ使用。
	if (!g_pc.shifter) return;
	const size_t maxFramesTotal = (size_t)(std::max)(1, g_pc.sampleRate / 5);
	size_t wroteFrames = 0;
	for (;;) {
		int avail = g_pc.shifter->available();
		if (avail <= 0) break;

		if (g_pc.dsCsInit)
			EnterCriticalSection(&g_pc.dsCs);
		BOOL canWrite = TRUE;
		if (g_pc.dsRunning && g_pc.dsb && g_pc.writePosValid) {
			DWORD play = 0, dsWrite = 0;
			if (SUCCEEDED(g_pc.dsb->GetCurrentPosition(&play, &dsWrite))) {
				const DWORD ahead = DougaPcBytesAhead(g_pc.writePos, play, g_pc.bufBytes);
				const DWORD maxAhead = DougaPcMaxAheadBytes();
				if (maxAhead > 0 && ahead + DougaPcMsToBytes(40) > maxAhead)
					canWrite = FALSE;
			}
		}
		if (g_pc.dsCsInit)
			LeaveCriticalSection(&g_pc.dsCs);
		if (!canWrite) break;

		size_t frames = (size_t)(std::min)(avail, g_pc.sampleRate / 25);
		if (frames == 0) break;
		if (wroteFrames + frames > maxFramesTotal)
			frames = maxFramesTotal - wroteFrames;
		if (frames == 0) break;

		g_pc.outFlat.resize(frames * (size_t)g_pc.channels);
		for (int c = 0; c < g_pc.channels; ++c)
			g_pc.outPtrs[c] = g_pc.outFlat.data() + (size_t)c * frames;
		const size_t got = g_pc.shifter->retrieve(g_pc.outPtrs.data(), frames);
		if (got == 0) break;
		g_pc.interleavedTmp.resize(got * (size_t)g_pc.channels);
		for (size_t i = 0; i < got; ++i) {
			for (int c = 0; c < g_pc.channels; ++c)
				g_pc.interleavedTmp[i * (size_t)g_pc.channels + (size_t)c] = g_pc.outPtrs[c][i];
		}
		if (g_pc.bits == 16) {
			g_pc.pcmOut.resize(got * (size_t)g_pc.blockAlign);
			DougaPcFloatToPcm16(g_pc.interleavedTmp.data(), got, g_pc.channels, g_pc.pcmOut.data());
			DougaPcEqAndRemote(g_pc.pcmOut.data(), (DWORD)(got * (size_t)g_pc.blockAlign));
			if (g_pc.dsCsInit)
				EnterCriticalSection(&g_pc.dsCs);
			const BOOL ok = DougaPcWritePcm_NoLock(g_pc.pcmOut.data(), (DWORD)(got * (size_t)g_pc.blockAlign));
			if (g_pc.dsCsInit)
				LeaveCriticalSection(&g_pc.dsCs);
			if (!ok) break;
		}
		wroteFrames += got;
		if (wroteFrames >= maxFramesTotal) break;
	}
}

// 受け取った PCM を順に DS へ流す。時刻での取捨選択はしない。
//
// NullRenderer は参照クロックに合わせてサンプルをスケジュールするので、
// PCM はもともと実時間で届く。またシークはグラフをフラッシュするため、
// 以降に届くものは必ずシーク後のデータになる。
// BufferCB の SampleTime を IMediaSeeking の位置と比較する方式は、
// 「シーク後に 0 から振り直すスプリッタ」と「絶対値のままのスプリッタ」で
// 意味が逆になり、片方では全サンプルを捨てて無音になっていた。
static void DougaPcDrainQueue()
{
	if (!g_pc.installed) return;
	for (;;) {
		DWORD bytes = 0;
		if (g_pc.csInit)
			EnterCriticalSection(&g_pc.cs);
		if (g_pc.playing && !g_pc.pcmQ.empty())
			bytes = (DWORD)g_pc.pcmQ.front().data.size();
		if (g_pc.csInit)
			LeaveCriticalSection(&g_pc.cs);
		if (bytes == 0) break;

		// DS が先行しすぎなら次のタイマーまで残す（捨てると位置がずれる）
		BOOL room = TRUE;
		if (g_pc.dsCsInit)
			EnterCriticalSection(&g_pc.dsCs);
		room = DougaPcHasRoom_NoLock(bytes);
		if (g_pc.dsCsInit)
			LeaveCriticalSection(&g_pc.dsCs);
		if (!room) break;

		DougaPcQItem item;
		if (g_pc.csInit)
			EnterCriticalSection(&g_pc.cs);
		if (!g_pc.playing || g_pc.pcmQ.empty()) {
			if (g_pc.csInit)
				LeaveCriticalSection(&g_pc.cs);
			break;
		}
		item = std::move(g_pc.pcmQ.front());
		g_pc.pcmQ.pop_front();
		if (g_pc.csInit)
			LeaveCriticalSection(&g_pc.cs);

		DougaPcEqAndRemote(item.data.data(), (DWORD)item.data.size());
		if (g_pc.dsCsInit)
			EnterCriticalSection(&g_pc.dsCs);
		if (g_pc.playing)
			DougaPcWritePcm_NoLock(item.data.data(), (DWORD)item.data.size());
		if (g_pc.dsCsInit)
			LeaveCriticalSection(&g_pc.dsCs);
	}
}

static void DougaPcQueuePcm(const BYTE* data, DWORD bytes)
{
	if (!data || bytes == 0) return;
	if (g_pc.csInit)
		EnterCriticalSection(&g_pc.cs);
	if (!g_pc.playing) {
		if (g_pc.csInit)
			LeaveCriticalSection(&g_pc.cs);
		return;
	}
	// 溢れは捨てるだけ。ここから DS 書き／GetCurrentPosition すると
	// SampleGrabber スレッドとグラフがデッドロックし、映像も止まる。
	while (g_pc.pcmQ.size() >= kDougaPcQMax)
		g_pc.pcmQ.pop_front();
	DougaPcQItem it;
	it.data.assign(data, data + bytes);
	g_pc.pcmQ.push_back(std::move(it));
	if (g_pc.csInit)
		LeaveCriticalSection(&g_pc.cs);
}

static void DougaPcOnPcm(const BYTE* p, long len)
{
	if (!p || len <= 0 || !g_pc.installed) return;
	InterlockedExchange(&g_pc.inCallback, 1);

	BOOL playing = FALSE;
	int channels = 0, srcCh = 0, srcBpf = 0, srcBits = 16;
	BOOL downmix = FALSE, srcFloat = FALSE;
	double rate = 1.0;
	RubberBand::RubberBandStretcher* shifter = NULL;

	if (g_pc.csInit)
		EnterCriticalSection(&g_pc.cs);
	playing = g_pc.playing;
	channels = g_pc.channels;
	srcCh = g_pc.srcChannels > 0 ? g_pc.srcChannels : g_pc.channels;
	srcBpf = g_pc.srcBlockAlign > 0 ? g_pc.srcBlockAlign : g_pc.blockAlign;
	srcBits = g_pc.srcBits > 0 ? g_pc.srcBits : 16;
	srcFloat = g_pc.srcFloat;
	downmix = g_pc.downmix;
	rate = g_pc.rate;
	if (playing && channels >= 1 && srcCh >= 1 && srcBpf >= 1 && len >= srcBpf) {
		len -= (len % srcBpf);
		g_pc.lastPcmTick = GetTickCount();
		g_pc.endFlushed = FALSE;
		g_pc.draining = FALSE;
		g_pc.drainAheadBytes = 0;
	} else {
		playing = FALSE;
	}
	if (g_pc.csInit)
		LeaveCriticalSection(&g_pc.cs);

	if (!playing || len < srcBpf) {
		InterlockedExchange(&g_pc.inCallback, 0);
		return;
	}

	const long frames = len / srcBpf;
	g_pc.pcmOut.resize((size_t)frames * (size_t)srcCh * 2);
	BOOL converted = DougaPcSrcToPcm16(p, frames, srcCh, srcBits, srcFloat, (INT16*)g_pc.pcmOut.data());
	if (!converted && srcBits != 16) {
		converted = DougaPcSrcToPcm16(p, frames, srcCh, 16, FALSE, (INT16*)g_pc.pcmOut.data());
	}
	if (!converted) {
		InterlockedExchange(&g_pc.inCallback, 0);
		return;
	}
	const INT16* pcm16 = (const INT16*)g_pc.pcmOut.data();
	long pcmBytes = frames * srcCh * 2;

	if (downmix && srcCh > 2 && channels == 2) {
		g_pc.downmixBuf.resize((size_t)frames * 4);
		DougaPcDownmixToStereo16(pcm16, frames, srcCh, (INT16*)g_pc.downmixBuf.data());
		pcm16 = (const INT16*)g_pc.downmixBuf.data();
		pcmBytes = frames * 4;
	}

	// rate≈1: DS 書きは Grabber スレッドでやらない（グラフとデッドロックする）
	if (fabs(rate - 1.0) < kDougaPitchEps) {
		DougaPcQueuePcm((const BYTE*)pcm16, (DWORD)pcmBytes);
		InterlockedExchange(&g_pc.inCallback, 0);
		return;
	}

	if (g_pc.csInit)
		EnterCriticalSection(&g_pc.cs);
	if (!g_pc.playing) {
		if (g_pc.csInit)
			LeaveCriticalSection(&g_pc.cs);
		InterlockedExchange(&g_pc.inCallback, 0);
		return;
	}
	DougaPcEnsureShifter_NoLock();
	shifter = g_pc.shifter;
	if (!shifter) {
		if (g_pc.csInit)
			LeaveCriticalSection(&g_pc.cs);
		InterlockedExchange(&g_pc.inCallback, 0);
		return;
	}
	if (g_pc.csInit)
		LeaveCriticalSection(&g_pc.cs);

	const size_t samplesIn = (size_t)frames;
	g_pc.inFlat.resize(samplesIn * (size_t)channels);
	g_pc.inPtrs.resize((size_t)channels);
	for (int c = 0; c < channels; ++c)
		g_pc.inPtrs[c] = g_pc.inFlat.data() + (size_t)c * samplesIn;
	for (long i = 0; i < frames; ++i) {
		for (int c = 0; c < channels; ++c)
			g_pc.inPtrs[c][i] = (float)pcm16[i * channels + c] / 32768.f;
	}

	const size_t maxChunk = 8192;
	size_t done = 0;
	while (done < samplesIn) {
		if (!g_pc.playing || !g_pc.installed)
			break;
		const size_t n = (std::min)(maxChunk, samplesIn - done);
		float* ptrs[32];
		const int chUse = (std::min)(channels, 32);

		if (g_pc.csInit)
			EnterCriticalSection(&g_pc.cs);
		if (!g_pc.shifter || g_pc.shifter != shifter || !g_pc.playing) {
			if (g_pc.csInit)
				LeaveCriticalSection(&g_pc.cs);
			break;
		}
		for (int c = 0; c < chUse; ++c)
			ptrs[c] = g_pc.inPtrs[c] + done;
		RubberBand::RubberBandStretcher* sh = g_pc.shifter;
		if (g_pc.csInit)
			LeaveCriticalSection(&g_pc.cs);

		try {
			sh->process(ptrs, n, false);
		} catch (...) {
			break;
		}

		if (g_pc.playing && g_pc.shifter == sh)
			DougaPcRetrieveAndWrite_NoLock();
		done += n;
	}
	InterlockedExchange(&g_pc.inCallback, 0);
}

class DougaPcGrabberCB : public ISampleGrabberCB
{
public:
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_ISampleGrabberCB) {
			*ppv = static_cast<ISampleGrabberCB*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = NULL;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() { return (ULONG)InterlockedIncrement(&g_pc.refCb); }
	STDMETHODIMP_(ULONG) Release()
	{
		LONG n = InterlockedDecrement(&g_pc.refCb);
		return (ULONG)n;
	}
	STDMETHODIMP SampleCB(double, IMediaSample*) { return S_OK; }
	STDMETHODIMP BufferCB(double, BYTE* pBuffer, long BufferLen)
	{
		if (InterlockedCompareExchange(&g_pc.shuttingDown, 0, 0) != 0)
			return S_OK;
		DougaPcOnPcm(pBuffer, BufferLen);
		return S_OK;
	}
};

static DougaPcGrabberCB g_pcCb;

static BOOL DougaPcIsAudioRendererName(const WCHAR* name)
{
	if (!name) return FALSE;
	CString s(name);
	s.MakeLower();
	if (s.Find(L"directsound") >= 0) return TRUE;
	if (s.Find(L"default wave") >= 0) return TRUE;
	if (s.Find(L"audio renderer") >= 0) return TRUE;
	if (s.Find(L"wasapi") >= 0) return TRUE;
	return FALSE;
}

static HRESULT DougaPcGetPin(IBaseFilter* f, PIN_DIRECTION dir, IPin** pp)
{
	*pp = NULL;
	if (!f) return E_POINTER;
	IEnumPins* e = NULL;
	if (FAILED(f->EnumPins(&e)) || !e) return E_FAIL;
	IPin* pin = NULL;
	while (e->Next(1, &pin, NULL) == S_OK) {
		PIN_DIRECTION d;
		if (SUCCEEDED(pin->QueryDirection(&d)) && d == dir) {
			*pp = pin;
			e->Release();
			return S_OK;
		}
		pin->Release();
	}
	e->Release();
	return E_FAIL;
}

static BOOL DougaPcFindAudioRenderer(IGraphBuilder* graph, IBaseFilter** ppRenderer, IPin** ppIn, IPin** ppUp)
{
	*ppRenderer = NULL; *ppIn = NULL; *ppUp = NULL;
	if (!graph) return FALSE;
	IEnumFilters* en = NULL;
	if (FAILED(graph->EnumFilters(&en)) || !en) return FALSE;
	IBaseFilter* f = NULL;
	while (en->Next(1, &f, NULL) == S_OK) {
		FILTER_INFO fi = {};
		f->QueryFilterInfo(&fi);
		if (fi.pGraph) fi.pGraph->Release();
		if (DougaPcIsAudioRendererName(fi.achName)) {
			IPin* inPin = NULL;
			if (SUCCEEDED(DougaPcGetPin(f, PINDIR_INPUT, &inPin)) && inPin) {
				IPin* up = NULL;
				if (SUCCEEDED(inPin->ConnectedTo(&up)) && up) {
					*ppRenderer = f;
					*ppIn = inPin;
					*ppUp = up;
					en->Release();
					return TRUE;
				}
				inPin->Release();
			}
		}
		f->Release();
	}
	en->Release();
	return FALSE;
}

static BOOL DougaPcCreateDs(HWND hwnd, const WAVEFORMATEX* wfx, DWORD channelMask, BOOL allowDownmix)
{
	if (!wfx || wfx->nChannels < 1 || wfx->nSamplesPerSec < 8000) return FALSE;
	HRESULT hr = E_FAIL;
	for (int attempt = 0; attempt < 10; ++attempt) {
		if (g_pc.ds) { g_pc.ds->Release(); g_pc.ds = NULL; }
		hr = DirectSoundCreate8(NULL, &g_pc.ds, NULL);
		if (SUCCEEDED(hr) && g_pc.ds) break;
		Sleep(30 + attempt * 20);
	}
	if (FAILED(hr) || !g_pc.ds) {
		WCHAR m[96];
		swprintf_s(m, L"CreateDs:DirectSoundCreate8 FAILED hr=0x%08X", (unsigned)hr);
		DougaHangTrace(m);
		return FALSE;
	}
	HWND owner = hwnd ? hwnd : GetDesktopWindow();
	if (FAILED(g_pc.ds->SetCooperativeLevel(owner, DSSCL_PRIORITY))) {
		if (FAILED(g_pc.ds->SetCooperativeLevel(owner, DSSCL_NORMAL))) {
			DougaHangTrace(L"CreateDs:SetCooperativeLevel FAILED");
			g_pc.ds->Release(); g_pc.ds = NULL;
			return FALSE;
		}
	}

	static const GUID kSubtypePcm = { 0x00000001, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

	auto tryCreate = [&](WAVEFORMATEX* fmt, BOOL setPrimary) -> BOOL {
		// 多ch でプライマリを無理に 5.1 にすると、環境によって無音になることがある
		if (setPrimary) {
			DSBUFFERDESC primDesc = {};
			primDesc.dwSize = sizeof(primDesc);
			primDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
			IDirectSoundBuffer* prim = NULL;
			if (SUCCEEDED(g_pc.ds->CreateSoundBuffer(&primDesc, &prim, NULL)) && prim) {
				prim->SetFormat(fmt);
				prim->Release();
			}
		}
		DSBUFFERDESC desc = {};
		desc.dwSize = sizeof(desc);
		desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
		DWORD bytes = fmt->nAvgBytesPerSec * 3;
		if (bytes < 32768) bytes = 32768;
		desc.dwBufferBytes = bytes;
		desc.lpwfxFormat = fmt;
		IDirectSoundBuffer* pPrim = NULL;
		HRESULT chr = E_FAIL;
		for (int attempt = 0; attempt < 8; ++attempt) {
			chr = g_pc.ds->CreateSoundBuffer(&desc, &pPrim, NULL);
			if (SUCCEEDED(chr) && pPrim) break;
			pPrim = NULL;
			Sleep(30 + attempt * 15);
		}
		if (FAILED(chr) || !pPrim) {
			WCHAR m[96];
			swprintf_s(m, L"CreateDs:CreateSoundBuffer FAILED hr=0x%08X", (unsigned)chr);
			DougaHangTrace(m);
			return FALSE;
		}
		if (g_pc.dsb) { g_pc.dsb->Release(); g_pc.dsb = NULL; }
		g_pc.dsb = pPrim;
		g_pc.sampleRate = (int)fmt->nSamplesPerSec;
		g_pc.channels = (int)fmt->nChannels;
		g_pc.bits = 16;
		g_pc.blockAlign = (int)fmt->nBlockAlign;
		g_pc.bufBytes = bytes;
		g_pc.writePos = 0;
		g_pc.writePosValid = FALSE;
		{
			void* p1 = NULL; void* p2 = NULL;
			DWORD b1 = 0, b2 = 0;
			if (SUCCEEDED(g_pc.dsb->Lock(0, g_pc.bufBytes, &p1, &b1, &p2, &b2, 0))) {
				if (p1 && b1) memset(p1, 0, b1);
				if (p2 && b2) memset(p2, 0, b2);
				g_pc.dsb->Unlock(p1, b1, p2, b2);
			}
		}
		return TRUE;
	};

	g_pc.srcChannels = (int)wfx->nChannels;
	g_pc.srcBits = (int)wfx->wBitsPerSample;
	if (g_pc.srcBits <= 0) g_pc.srcBits = 16;
	// 実フレームバイト必須。16bit 前提の nCh*2 だと float 多ch が約2倍速になる
	if (wfx->nBlockAlign > 0)
		g_pc.srcBlockAlign = (int)wfx->nBlockAlign;
	else
		g_pc.srcBlockAlign = (int)wfx->nChannels * ((g_pc.srcBits + 7) / 8);
	g_pc.channelMask = channelMask
		? channelMask
		: DirectSoundChannelMaskForOutput((int)wfx->nChannels, 0); // speaker_layout で並べ替えない（PCM 順＝標準マスク）
	g_pc.downmix = FALSE;

	// 2ch 以下: PCM
	if (wfx->nChannels <= 2) {
		WAVEFORMATEX fmt = *wfx;
		fmt.wFormatTag = WAVE_FORMAT_PCM;
		fmt.wBitsPerSample = 16;
		fmt.nBlockAlign = (WORD)(fmt.nChannels * fmt.wBitsPerSample / 8);
		fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
		fmt.cbSize = 0;
		if (tryCreate(&fmt, FALSE) || tryCreate(&fmt, TRUE))
			return TRUE;
		DougaHangTrace(L"CreateDs:tryCreate(2ch) FAILED");
		g_pc.ds->Release(); g_pc.ds = NULL;
		return FALSE;
	}

	// 多ch: WAVEFORMATEXTENSIBLE で 5.1/7.1 をそのまま再生（ダウンミックスしない）
	WAVEFORMATEXTENSIBLE wfex = {};
	wfex.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	wfex.Format.nChannels = wfx->nChannels;
	wfex.Format.nSamplesPerSec = wfx->nSamplesPerSec;
	wfex.Format.wBitsPerSample = 16;
	wfex.Format.nBlockAlign = (WORD)(wfex.Format.nChannels * 2);
	wfex.Format.nAvgBytesPerSec = wfex.Format.nSamplesPerSec * wfex.Format.nBlockAlign;
	wfex.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	wfex.Samples.wValidBitsPerSample = 16;
	wfex.dwChannelMask = g_pc.channelMask;
	wfex.SubFormat = kSubtypePcm;
	// 先にプライマリ非変更で作成。ダメならプライマリ合わせ→最後に stereo
	if (tryCreate(&wfex.Format, FALSE) || tryCreate(&wfex.Format, TRUE))
		return TRUE;

	DougaHangTrace(L"CreateDs:tryCreate(multi) FAILED");
	if (!allowDownmix) {
		g_pc.ds->Release(); g_pc.ds = NULL;
		return FALSE;
	}
	WAVEFORMATEX stereo = {};
	stereo.wFormatTag = WAVE_FORMAT_PCM;
	stereo.nChannels = 2;
	stereo.nSamplesPerSec = wfx->nSamplesPerSec;
	stereo.wBitsPerSample = 16;
	stereo.nBlockAlign = 4;
	stereo.nAvgBytesPerSec = stereo.nSamplesPerSec * stereo.nBlockAlign;
	stereo.cbSize = 0;
	if (!tryCreate(&stereo, FALSE) && !tryCreate(&stereo, TRUE)) {
		DougaHangTrace(L"CreateDs:tryCreate(downmix) FAILED");
		g_pc.ds->Release(); g_pc.ds = NULL;
		return FALSE;
	}
	g_pc.downmix = TRUE;
	return TRUE;
}

} // namespace

BOOL DougaPitchCorrect_IsActive()
{
	return g_pc.installed ? TRUE : FALSE;
}

void DougaPitchCorrect_EnableCallback()
{
	if (!g_pc.installed || !g_pc.grabber) return;
	if (g_pc.cbArmed) return;
	g_pc.cbArmed = TRUE;
	g_pc.playing = TRUE;
	if (g_pc.dsCsInit)
		EnterCriticalSection(&g_pc.dsCs);
	DougaPcSilenceAll_NoLock();
	g_pc.dsRunning = FALSE;
	g_pc.prebufferBytes = 0;
	g_pc.lastWriteTick = 0;
	if (g_pc.dsCsInit)
		LeaveCriticalSection(&g_pc.dsCs);
	g_pc.grabber->SetCallback(&g_pcCb, 1);
}

int DougaPitchCorrect_GetLatencyMs()
{
	if (!g_pc.installed || g_pc.sampleRate <= 0 || g_pc.blockAlign <= 0)
		return 0;
	// 未再生時はプリバッファ想定。再生中は DS の未再生バイトから実測。
	DWORD aheadBytes = DougaPcMsToBytes(kDougaPcPrebufferMs);
	if (g_pc.dsCsInit)
		EnterCriticalSection(&g_pc.dsCs);
	if (g_pc.dsb && g_pc.dsRunning && g_pc.writePosValid && g_pc.bufBytes > 0) {
		DWORD play = 0, dsWrite = 0;
		if (SUCCEEDED(g_pc.dsb->GetCurrentPosition(&play, &dsWrite)))
			aheadBytes = DougaPcBytesAhead(g_pc.writePos, play, g_pc.bufBytes);
	} else if (!g_pc.dsRunning && g_pc.prebufferBytes > 0) {
		aheadBytes = g_pc.prebufferBytes;
	}
	if (g_pc.dsCsInit)
		LeaveCriticalSection(&g_pc.dsCs);
	const DWORD bps = (DWORD)g_pc.sampleRate * (DWORD)g_pc.blockAlign;
	if (bps == 0) return (int)kDougaPcPrebufferMs;
	int ms = (int)((aheadBytes * 1000ull) / bps);
	if (ms < (int)kDougaPcPrebufferMs) ms = (int)kDougaPcPrebufferMs;
	const int maxMs = (g_pc.channels > 2)
		? (int)kDougaPcMaxAheadMsMulti + 40
		: (int)kDougaPcMaxAheadMs + 40;
	if (ms > maxMs) ms = maxMs;
	return ms;
}

void DougaPitchCorrect_SetVolumeDsPos(int dsPos)
{
	if (!g_pc.installed || !g_pc.dsb) return;
	// SetVolume は CS 不要（DS 自身がスレッドセーフ寄り）。待たない。
	g_pc.dsb->SetVolume(DougaPcDsVolFromPos(dsPos));
}

void DougaPitchCorrect_SetPaused(BOOL paused)
{
	if (!g_pc.installed || !g_pc.csInit) return;
	EnterCriticalSection(&g_pc.cs);
	g_pc.playing = paused ? FALSE : TRUE;
	if (paused) {
		LeaveCriticalSection(&g_pc.cs);
		DougaPcStopDsOutsideCallback();
		return;
	}
	g_pc.endFlushed = FALSE;
	g_pc.writePosValid = FALSE;
	g_pc.dsRunning = FALSE;
	g_pc.prebufferBytes = 0;
	g_pc.lastPcmTick = GetTickCount();
	LeaveCriticalSection(&g_pc.cs);
}

void DougaPitchCorrect_PauseForGraphSeek()
{
	if (!g_pc.installed || !g_pc.csInit) return;
	EnterCriticalSection(&g_pc.cs);
	g_pc.playing = FALSE;
	LeaveCriticalSection(&g_pc.cs);
	DougaPcWaitCallbackGone();
}

void DougaPitchCorrect_OnSeek(double)
{
	if (!g_pc.installed || !g_pc.csInit) return;
	EnterCriticalSection(&g_pc.cs);
	g_pc.playing = FALSE;
	LeaveCriticalSection(&g_pc.cs);

	DougaPcStopDsOutsideCallback();

	EnterCriticalSection(&g_pc.cs);
	if (g_pc.shifter)
		g_pc.shifter->reset();
	g_pc.pcmQ.clear();
	g_pc.endFlushed = FALSE;
	g_pc.draining = FALSE;
	g_pc.drainAheadBytes = 0;
	g_pc.writePosValid = FALSE;
	g_pc.dsRunning = FALSE;
	g_pc.prebufferBytes = 0;
	g_pc.startTick = GetTickCount();
	g_pc.lastPcmTick = 0;
	g_pc.lastWriteTick = 0;
	InterlockedExchange(&g_pc.eqNeedReset, 1);
	g_pc.playing = TRUE;
	LeaveCriticalSection(&g_pc.cs);
}

void DougaPitchCorrect_Poll()
{
	if (!g_pc.installed || !g_pc.csInit) return;
	DougaPcDrainQueue();
	// UI スレッドをブロックしない
	if (!TryEnterCriticalSection(&g_pc.cs))
		return;
	if (!g_pc.playing || !g_pc.dsb || g_pc.lastPcmTick == 0) {
		LeaveCriticalSection(&g_pc.cs);
		return;
	}
	const DWORD now = GetTickCount();
	const DWORD gap = now - g_pc.lastPcmTick;
	BOOL doStop = FALSE;

	if (gap >= kDougaPcGapFlushMs && g_pc.dsRunning) {
		if (g_pc.startTick != 0 && (now - g_pc.startTick) < kDougaPcStartGraceMs) {
			LeaveCriticalSection(&g_pc.cs);
			return;
		}
		if (!g_pc.draining) {
			g_pc.draining = TRUE;
			g_pc.drainTick = now;
		}
		const DWORD since = now - g_pc.drainTick;
		if (since >= 200)
			doStop = TRUE;
	}
	LeaveCriticalSection(&g_pc.cs);

	if (doStop)
		DougaPcStopDsOutsideCallback();
}

void DougaPitchCorrect_SetPlaybackRate(double rate)
{
	if (rate < 0.1) rate = 0.1;
	if (rate > 4.0) rate = 4.0;
	if (!g_pc.csInit) return;
	EnterCriticalSection(&g_pc.cs);
	const double old = g_pc.rate;
	const BOOL rateChanged = (fabs(old - rate) > 0.005);
	const BOOL needReset = (g_pc.shifter && fabs(old - rate) > 0.05);
	g_pc.rate = rate;
	if (g_pc.shifter && !needReset)
		DougaPcApplyRateToShifter_NoLock();
	BOOL wasPlaying = g_pc.playing;
	if (needReset || rateChanged)
		g_pc.playing = FALSE; // コールバックを抜けさせる
	LeaveCriticalSection(&g_pc.cs);

	if (needReset || rateChanged)
		DougaPcStopDsOutsideCallback();

	EnterCriticalSection(&g_pc.cs);
	if (g_pc.shifter) {
		DougaPcApplyRateToShifter_NoLock();
		if (needReset)
			g_pc.shifter->reset();
	}
	if (rateChanged) {
		g_pc.writePosValid = FALSE;
		g_pc.dsRunning = FALSE;
		g_pc.prebufferBytes = 0;
	}
	g_pc.playing = wasPlaying;
	LeaveCriticalSection(&g_pc.cs);
}

void DougaPitchCorrect_Shutdown()
{
	if (!g_pc.installed && !g_pc.dsb && !g_pc.grabber) {
		DougaHangTrace(L"PitchShutdown:idle");
		return;
	}

	InterlockedExchange(&g_pc.shuttingDown, 1);

	ISampleGrabber* grab = NULL;
	IDirectSoundBuffer* dsb = NULL;
	IDirectSound8* ds = NULL;
	RubberBand::RubberBandStretcher* shifter = NULL;
	IBaseFilter* grabberF = NULL;
	IBaseFilter* nullF = NULL;
	IBaseFilter* oldRenderer = NULL;

	if (g_pc.grabber) {
		g_pc.grabber->SetCallback(NULL, 0);
	}

	if (g_pc.csInit)
		EnterCriticalSection(&g_pc.cs);
	g_pc.playing = FALSE;
	g_pc.installed = FALSE;
	g_pc.cbArmed = FALSE;
	g_pc.dsRunning = FALSE;
	g_pc.endFlushed = FALSE;
	g_pc.lastPcmTick = 0;
	grab = g_pc.grabber; g_pc.grabber = NULL;
	dsb = g_pc.dsb; g_pc.dsb = NULL;
	ds = g_pc.ds; g_pc.ds = NULL;
	shifter = g_pc.shifter; g_pc.shifter = NULL;
	grabberF = g_pc.grabberF; g_pc.grabberF = NULL;
	nullF = g_pc.nullF; g_pc.nullF = NULL;
	oldRenderer = g_pc.oldRenderer; g_pc.oldRenderer = NULL;
	g_pc.graph = NULL;
	g_pc.inFlat.clear();
	g_pc.outFlat.clear();
	g_pc.pcmQ.clear();
	g_pc.rate = 1.0;
	g_pc.writePosValid = FALSE;
	g_pc.downmix = FALSE;
	g_pc.srcChannels = 0;
	g_pc.srcBlockAlign = 0;
	g_pc.srcBits = 0;
	g_pc.srcFloat = FALSE;
	g_pc.channelMask = 0;
	g_pc.startTick = 0;
	g_pc.lastWriteTick = 0;
	g_pc.downmixBuf.clear();
	if (g_pc.csInit)
		LeaveCriticalSection(&g_pc.cs);

	DougaHangTrace(L"PitchShutdown:enter");
	DougaPcWaitCallbackGone();
	if (grab) {
		grab->SetCallback(NULL, 0);
		grab->Release();
	}
	if (dsb) {
		DougaHangTrace(L"PitchShutdown:dsb Stop");
		dsb->SetVolume(DSBVOLUME_MIN);
		dsb->Stop();
		dsb->SetCurrentPosition(0);
		dsb->Release();
	}
	if (ds) ds->Release();
	if (shifter) delete shifter;
	// Grabber/Null はグラフから外さない。外すと音声ピン欠落のまま ffd+EVR の
	// MediaControl::Stop が返らなくなる（片田舎停止で ColdReset:Stop 固まり）。
	// 破棄は ColdReset の Nuke に任せる。
	if (grabberF) grabberF->Release();
	if (nullF) nullF->Release();
	if (oldRenderer) oldRenderer->Release();

	// クリティカルセクションは破棄しない。DougaPcWaitCallbackGone は上限付きで
	// 諦めるため、Grabber のストリーミングスレッドがまだ EnterCriticalSection で
	// 待っている可能性がある。そこで削除すると そのスレッドが永久に戻らず、
	// 旧グラフの Stop() も返らなくなる（曲移動の 2 回目以降で「応答なし」）。
	InterlockedExchange(&g_pc.shuttingDown, 0);
	DougaHangTrace(L"PitchShutdown:leave");
}

// exe と同じ場所に nopitch.txt を置くと Grabber 張り替えを行わない。
// 環境切り分け用（その PC では速度変更時の音程補正のみ無効になる）。
static BOOL DougaPitchDisabledByFile()
{
	BOOL disabled = FALSE;
	WCHAR path[MAX_PATH] = {};
	if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
		WCHAR* sep = wcsrchr(path, L'\\');
		if (sep) {
			const size_t room = MAX_PATH - (size_t)(sep + 1 - path);
			wcscpy_s(sep + 1, room, L"nopitch.txt");
			if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
				disabled = TRUE;
		}
	}
	return disabled;
}

BOOL DougaPitchCorrect_Install(IGraphBuilder* graph, HWND hwndOwner)
{
#if !DOUGA_PITCHCORRECT_ENABLE
	UNREFERENCED_PARAMETER(graph);
	UNREFERENCED_PARAMETER(hwndOwner);
	DougaHangTrace(L"PitchInstall:DISABLED for verify");
	return FALSE;
#else
	DougaPitchCorrect_Shutdown();
	if (!graph || mode != -2) return FALSE;
	if (DougaPitchDisabledByFile()) {
		OutputDebugStringW(L"[DougaPitch] disabled by nopitch.txt\n");
		DougaHangTrace(L"PitchInstall:DISABLED by nopitch.txt");
		return FALSE;
	}

	IBaseFilter* renderer = NULL;
	IPin* renIn = NULL;
	IPin* upOut = NULL;
	if (!DougaPcFindAudioRenderer(graph, &renderer, &renIn, &upOut)) {
		OutputDebugStringW(L"[DougaPitch] audio renderer not found\n");
		return FALSE;
	}

	IBaseFilter* grabF = NULL;
	IBaseFilter* nullF = NULL;
	ISampleGrabber* grab = NULL;
	HRESULT hr = CoCreateInstance(CLSID_SampleGrabber, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void**)&grabF);
	if (FAILED(hr) || !grabF) {
		OutputDebugStringW(L"[DougaPitch] CLSID_SampleGrabber CoCreate failed\n");
		DougaHangTrace(L"PitchInstall:CLSID_SampleGrabber CoCreate failed");
		renIn->Release(); upOut->Release(); renderer->Release();
		return FALSE;
	}
	hr = CoCreateInstance(CLSID_NullRenderer, NULL, CLSCTX_INPROC_SERVER,
		IID_IBaseFilter, (void**)&nullF);
	if (FAILED(hr) || !nullF) {
		OutputDebugStringW(L"[DougaPitch] CLSID_NullRenderer CoCreate failed\n");
		DougaHangTrace(L"PitchInstall:CLSID_NullRenderer CoCreate failed");
		grabF->Release();
		renIn->Release(); upOut->Release(); renderer->Release();
		return FALSE;
	}
	hr = grabF->QueryInterface(IID_ISampleGrabber, (void**)&grab);
	if (FAILED(hr) || !grab) {
		OutputDebugStringW(L"[DougaPitch] IID_ISampleGrabber QI failed\n");
		DougaHangTrace(L"PitchInstall:IID_ISampleGrabber QI failed");
		nullF->Release(); grabF->Release();
		renIn->Release(); upOut->Release(); renderer->Release();
		return FALSE;
	}

	AM_MEDIA_TYPE mtWant = {};
	mtWant.majortype = MEDIATYPE_Audio;
	mtWant.subtype = MEDIASUBTYPE_PCM;
	mtWant.formattype = FORMAT_WaveFormatEx;
	grab->SetMediaType(&mtWant);
	grab->SetOneShot(FALSE);
	grab->SetBufferSamples(TRUE); // BufferCB 必須

	// 何も壊していない段階で降りる（DS レンダラのまま普通に再生される）
	auto abortKeepRenderer = [&](LPCWSTR why, HRESULT h) {
		WCHAR m[192];
		swprintf_s(m, L"[DougaPitch] %s hr=0x%08X; keep DS renderer\n", why, (unsigned)h);
		OutputDebugStringW(m);
		DougaHangTrace(m);
		if (upOut && renIn) graph->ConnectDirect(upOut, renIn, NULL);
		if (grab) { grab->Release(); grab = NULL; }
		if (nullF) { nullF->Release(); nullF = NULL; }
		if (grabF) { grabF->Release(); grabF = NULL; }
		if (renIn) { renIn->Release(); renIn = NULL; }
		if (upOut) { upOut->Release(); upOut = NULL; }
		if (renderer) { renderer->Release(); renderer = NULL; }
	};

	// 張り替えは停止中でしか行えない。停止していなければ Disconnect も
	// RemoveFilter も VFW_E_NOT_STOPPED で失敗し、一部だけ成功した壊れたグラフが
	// 残る（窓は出るが何も描画されず Stop も返らない）。
	// ここで Stop() を呼びに行くと、詰まったグラフでは戻ってこないので呼ばない。
	// 張り替えを見送るだけにする＝その回はピッチ補正なしで普通に再生される。
	if (pMediaControl) {
		OAFilterState st0 = State_Stopped;
		pMediaControl->GetState(0, &st0);
		if (st0 != State_Stopped) {
			abortKeepRenderer(L"graph not stopped", (HRESULT)st0);
			return FALSE;
		}
	}

	BOOL rewired = FALSE;
	DougaHangTrace(L"PitchInstall:Disconnect:begin");
	hr = graph->Disconnect(renIn);
	if (SUCCEEDED(hr))
		hr = graph->Disconnect(upOut);
	if (FAILED(hr)) {
		abortKeepRenderer(L"Disconnect failed", hr);
		return FALSE;
	}
	DougaHangTrace(L"PitchInstall:RemoveFilter renderer");
	hr = graph->RemoveFilter(renderer);
	if (FAILED(hr)) {
		abortKeepRenderer(L"RemoveFilter failed", hr);
		return FALSE;
	}
	hr = graph->AddFilter(grabF, L"Douga Pitch Grabber");
	if (SUCCEEDED(hr))
		hr = graph->AddFilter(nullF, L"Douga Pitch Null");
	if (FAILED(hr)) {
		graph->RemoveFilter(grabF);
		graph->RemoveFilter(nullF);
		graph->AddFilter(renderer, L"Default DirectSound Device");
		abortKeepRenderer(L"AddFilter failed", hr);
		return FALSE;
	}
	rewired = TRUE;

	IPin* grabIn = NULL; IPin* grabOut = NULL; IPin* nullIn = NULL;
	DougaPcGetPin(grabF, PINDIR_INPUT, &grabIn);
	DougaPcGetPin(grabF, PINDIR_OUTPUT, &grabOut);
	DougaPcGetPin(nullF, PINDIR_INPUT, &nullIn);

	auto failCleanup = [&](LPCWSTR why) {
		OutputDebugStringW(why);
		DougaHangTrace(why);
		if (grabIn) { grabIn->Release(); grabIn = NULL; }
		if (grabOut) { grabOut->Release(); grabOut = NULL; }
		if (nullIn) { nullIn->Release(); nullIn = NULL; }
		if (grab) { grab->SetCallback(NULL, 0); grab->Release(); grab = NULL; }
		if (rewired) {
			graph->RemoveFilter(grabF);
			graph->RemoveFilter(nullF);
			graph->AddFilter(renderer, L"Default DirectSound Device");
			IPin* rin = NULL;
			DougaPcGetPin(renderer, PINDIR_INPUT, &rin);
			if (rin && upOut)
				graph->ConnectDirect(upOut, rin, NULL);
			if (rin) rin->Release();
		}
		if (nullF) { nullF->Release(); nullF = NULL; }
		if (grabF) { grabF->Release(); grabF = NULL; }
		if (renIn) { renIn->Release(); renIn = NULL; }
		if (upOut) { upOut->Release(); upOut = NULL; }
		if (renderer) { renderer->Release(); renderer = NULL; }
	};

	if (!grabIn || !grabOut || !nullIn) {
		failCleanup(L"[DougaPitch] grabber/null pins missing\n");
		return FALSE;
	}
	hr = graph->ConnectDirect(upOut, grabIn, NULL);
	if (FAILED(hr)) {
		CString msg; msg.Format(L"PitchInstall:upstream->grabber fail 0x%08X", (unsigned)hr);
		DougaHangTrace((LPCWSTR)msg);
		failCleanup(msg);
		return FALSE;
	}
	hr = graph->ConnectDirect(grabOut, nullIn, NULL);
	if (FAILED(hr)) {
		CString msg; msg.Format(L"PitchInstall:grabber->null fail 0x%08X", (unsigned)hr);
		DougaHangTrace((LPCWSTR)msg);
		failCleanup(msg);
		return FALSE;
	}
	grabIn->Release(); grabIn = NULL;
	grabOut->Release(); grabOut = NULL;
	nullIn->Release(); nullIn = NULL;
	renIn->Release(); renIn = NULL;
	upOut->Release(); upOut = NULL;

	AM_MEDIA_TYPE mt = {};
	WAVEFORMATEX wfx = {};
	DWORD chMask = 0;
	BOOL srcFloat = FALSE;
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = 44100;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = 4;
	wfx.nAvgBytesPerSec = 44100 * 4;
	if (SUCCEEDED(grab->GetConnectedMediaType(&mt))) {
		if (mt.formattype == FORMAT_WaveFormatEx && mt.pbFormat && mt.cbFormat >= sizeof(WAVEFORMATEX)) {
			WAVEFORMATEX* pwf = (WAVEFORMATEX*)mt.pbFormat;
			wfx = *pwf;
			const WAVEFORMATEXTENSIBLE* we = NULL;
			if (wfx.wFormatTag == WAVE_FORMAT_EXTENSIBLE && mt.cbFormat >= sizeof(WAVEFORMATEXTENSIBLE)) {
				we = (const WAVEFORMATEXTENSIBLE*)mt.pbFormat;
				wfx = we->Format;
				chMask = we->dwChannelMask;
			}
			srcFloat = DougaPcIsIeeeFloatWfx(&wfx, we);
		}
		DougaPcFreeMediaType(mt);
	}
	if (chMask == 0)
		chMask = DirectSoundChannelMaskForOutput((int)wfx.nChannels, 0);

	DougaPcEnsureCs();
	g_pc.rate = 1.0;
	g_pc.refCb = 1;
	if (!DougaPcCreateDs(hwndOwner, &wfx, chMask, TRUE)) {
		DougaHangTrace(L"PitchInstall:CreateDs FAILED");
		// re-acquire pins for rollback path: upOut already released — reconnect via failCleanup needs upOut
		// Reconstruct: grab still connected; tear grab and restore renderer from current grab input
		IPin* gIn = NULL; IPin* upstream = NULL;
		DougaPcGetPin(grabF, PINDIR_INPUT, &gIn);
		if (gIn) {
			gIn->ConnectedTo(&upstream);
			gIn->Disconnect();
			if (upstream) upstream->Disconnect();
		}
		if (gIn) gIn->Release();
		grab->SetCallback(NULL, 0);
		grab->Release(); grab = NULL;
		graph->RemoveFilter(grabF);
		graph->RemoveFilter(nullF);
		nullF->Release(); nullF = NULL;
		grabF->Release(); grabF = NULL;
		graph->AddFilter(renderer, L"Default DirectSound Device");
		IPin* rin = NULL;
		DougaPcGetPin(renderer, PINDIR_INPUT, &rin);
		if (rin && upstream)
			graph->ConnectDirect(upstream, rin, NULL);
		if (rin) rin->Release();
		if (upstream) upstream->Release();
		renderer->Release();
		OutputDebugStringW(L"[DougaPitch] CreateDs failed; restored DS renderer\n");
		DougaHangTrace(L"PitchInstall:CreateDs FAILED (restored DS renderer)");
		return FALSE;
	}

	g_pc.graph = graph;
	g_pc.grabberF = grabF;
	g_pc.nullF = nullF;
	g_pc.oldRenderer = renderer; // 所有して stops で Release
	g_pc.grabber = grab;
	g_pc.srcFloat = srcFloat;
	if (g_pc.srcBits <= 0)
		g_pc.srcBits = (int)wfx.wBitsPerSample;
	DougaPcAttachSystemClock(graph);
	InterlockedExchange(&g_pc.refCb, 1);
	// BufferCB は Run 後に付ける。Pause/Run 中の GetCurrentPosition デッドロックを避ける
	g_pc.installed = TRUE;
	g_pc.playing = TRUE;
	g_pc.dsRunning = FALSE;
	g_pc.endFlushed = FALSE;
	g_pc.draining = FALSE;
	g_pc.drainAheadBytes = 0;
	g_pc.pcmQ.clear();
	g_pc.startTick = GetTickCount();
	g_pc.lastPcmTick = 0;
	g_pc.lastWriteTick = 0;
	g_pc.cbArmed = FALSE;
	DougaPcApplyVolume_NoLock(savedata.dsvol);
	DougaPcSilenceAll_NoLock();
	OutputDebugStringW(L"[DougaPitch] installed OK (callback deferred)\n");
	return TRUE;
#endif
}

static LONG s_dougaRateTempoGate = 0;
extern BOOL videoonly;
extern int tempo;
extern CMediaPlayerDlg* mp;

BOOL DougaVideoRateActive()
{
	// コンテキストメニューの速度スライダーと同じ: GetRate できるとき有効。
	if (!pMediaSeeking)
		return FALSE;
	double cur = 0.0;
	return SUCCEEDED(pMediaSeeking->GetRate(&cur)) ? TRUE : FALSE;
}

void DougaSetPlaybackRate(double rate, BOOL pushTempo)
{
	if (rate < 0.1) rate = 0.1;
	if (rate > 4.0) rate = 4.0;
	if (pMediaSeeking)
		pMediaSeeking->SetRate(rate);
	// mode=-2: SetRate の音程変化を Stretcher(pitch=1/R) で打ち消す
	if (mode == -2 && DougaPitchCorrect_IsActive())
		DougaPitchCorrect_SetPlaybackRate(rate);

	if (pMainFrame1 && pMainFrame1->m_bar.IsBarReady()
		&& pMainFrame1->m_bar.m_rate.GetSafeHwnd()) {
		const int pos = (int)(rate * 100.0 + 0.5);
		HWND hf = ::GetFocus();
		HWND cap = ::GetCapture();
		const BOOL busy = pMainFrame1->m_bar.m_rateDrag
			|| (hf && hf == pMainFrame1->m_bar.m_rate.GetSafeHwnd())
			|| (cap && cap == pMainFrame1->m_bar.m_rate.GetSafeHwnd());
		if (!busy)
			pMainFrame1->m_bar.m_rate.SetPos(pos);
		WCHAR vs[32];
		swprintf_s(vs, L"%.2fx", rate);
		pMainFrame1->m_bar.m_rateVal.SetWindowText(vs);
	}

	if (!pushTempo) return;
	if (InterlockedCompareExchange(&s_dougaRateTempoGate, 1, 0) != 0)
		return;
	const int tpos = TempoPosFromPercent((float)(rate * 100.0));
	tempo = tpos;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->m_tempo_sl.SetPos(tpos);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && mp->m_tempo.GetSafeHwnd())
		mp->m_tempo.SetPos(tpos, FALSE);
	InterlockedExchange(&s_dougaRateTempoGate, 0);
}

void DougaApplyTempoToVideoRate()
{
	if (!DougaVideoRateActive() || !pMediaSeeking)
		return;
	// バー速度ドラッグ中はテンポ→速度の上書きをしない
	if (pMainFrame1 && pMainFrame1->m_bar.IsBarReady()) {
		HWND cap = ::GetCapture();
		if (pMainFrame1->m_bar.m_rateDrag
			|| (cap && cap == pMainFrame1->m_bar.m_rate.GetSafeHwnd()))
			return;
	}
	if (InterlockedCompareExchange(&s_dougaRateTempoGate, 1, 0) != 0)
		return;
	double rate = TempoPlaybackRateFromPos(tempo);
	if (rate < 0.1) rate = 0.1;
	if (rate > 4.0) rate = 4.0;
	double cur = 1.0;
	if (FAILED(pMediaSeeking->GetRate(&cur)) || cur <= 0.0)
		cur = 1.0;
	if (fabs(cur - rate) > 0.005) {
		pMediaSeeking->SetRate(rate);
		if (mode == -2 && DougaPitchCorrect_IsActive())
			DougaPitchCorrect_SetPlaybackRate(rate);
	}
	if (pMainFrame1 && pMainFrame1->m_bar.IsBarReady()
		&& pMainFrame1->m_bar.m_rate.GetSafeHwnd()) {
		HWND hf = ::GetFocus();
		HWND cap = ::GetCapture();
		const BOOL busy = pMainFrame1->m_bar.m_rateDrag
			|| (hf && hf == pMainFrame1->m_bar.m_rate.GetSafeHwnd())
			|| (cap && cap == pMainFrame1->m_bar.m_rate.GetSafeHwnd());
		const int pos = (int)(rate * 100.0 + 0.5);
		if (!busy)
			pMainFrame1->m_bar.m_rate.SetPos(pos);
		WCHAR vs[32];
		swprintf_s(vs, L"%.2fx", rate);
		pMainFrame1->m_bar.m_rateVal.SetWindowText(vs);
	}
	InterlockedExchange(&s_dougaRateTempoGate, 0);
}

static void DougaRateSliderCb(void* /*ctx*/, int value)
{
	if (value < 10) value = 10;
	if (value > 400) value = 400;
	DougaSetPlaybackRate((double)value / 100.0, TRUE);
}

static void DougaEqSyncUi()
{
	extern COggDlg* og;
	if (og && og->m_EqualizerDlg && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd()))
		og->m_EqualizerDlg->SyncSlidersFromSavedata();
}

static void DougaEqBandSliderCb(void* ctx, int value)
{
	const int band = (int)(INT_PTR)ctx;
	if (band < 0 || band >= 15) return;
	if (value < 0) value = 0;
	if (value > 200) value = 200;
	savedata.eq[band] = value;
	savedata.eqsoundeq = 9; // Custom
	DougaEqSyncUi();
}

static void DougaEqGlobalSliderCb(void* ctx, int value)
{
	const int which = (int)(INT_PTR)ctx;
	if (value < 0) value = 0;
	if (value > 200) value = 200;
	if (which >= 15 && which <= 19)
		savedata.eq[which] = value;
	else if (which == 20)
		savedata.eq_reverb = value;
	else if (which == 21)
		savedata.eq_chorus = value;
	else if (which == 22)
		savedata.eq_delay = value;
	else if (which == 23)
		savedata.eqsoundeffect = value / 2;
	DougaEqSyncUi();
}

static void DougaEqPresetComboCb(void* /*ctx*/, int index, LPCTSTR /*text*/)
{
	if (index < 0) index = 0;
	if (index > MP_EQ_PRESET_COUNT - 1) index = MP_EQ_PRESET_COUNT - 1;
	savedata.eqsoundeq = index;
	equaliser(NULL, 0, 2);
	DougaEqSyncUi();
}

static void DougaEqEnvComboCb(void* /*ctx*/, int index, LPCTSTR /*text*/)
{
	if (index < 0) index = 0;
	if (index >= MP_REMOTE_EQ_ENV_COUNT) index = MP_REMOTE_EQ_ENV_COUNT - 1;
	savedata.eqsoundenv = index;
	DougaEqSyncUi();
}

static void DougaEqResetBandsCb(void* /*ctx*/, UINT /*id*/)
{
	for (int i = 0; i < 15; ++i)
		savedata.eq[i] = 100;
	DougaEqSyncUi();
}

static void DougaEqResetGlobalCb(void* /*ctx*/, UINT /*id*/)
{
	savedata.eq[15] = 100;
	savedata.eq[16] = 100;
	savedata.eq[17] = 100;
	savedata.eq[18] = 100;
	savedata.eq[19] = 100;
	savedata.eq_reverb = 0;
	savedata.eq_chorus = 0;
	savedata.eq_delay = 0;
	DougaEqSyncUi();
}

void CDouga::ShowDougaContextMenu(CPoint point)
{
	if (point.x == -1 && point.y == -1) {
		CRect rect;
		GetClientRect(rect);
		ClientToScreen(rect);
		point = rect.TopLeft();
		point.Offset(5, 5);
	}

	// メニュー表示直前にストリーム名を再取得（字幕などが空になるのを防ぐ）
	if (iam) {
		CntPin2(iam);
		if (pGraphBuilder)
			GetStreamInfo(pGraphBuilder, audioStreams, videoStreams, subtitleStreams);
		if (streamMap.subtitleCount == 0 && !subtitleStreams.empty()) {
			int n = 0;
			for (size_t si = 0; si < subtitleStreams.size() && n < 40; ++si) {
				if (DougaIsOffSubtitleName(subtitleStreams[si].name)) {
					streamidxSubOff = (int)subtitleStreams[si].streamIndex;
					continue;
				}
				streamidx2[n] = (int)subtitleStreams[si].streamIndex;
				if (!subtitleStreams[si].name.IsEmpty())
					streamname2[n] = subtitleStreams[si].name;
				else
					streamname2[n].Format(L"%s %d",
						LL14(L"字幕", L"Subtitle", L"Sous-titres", L"Sottotitoli", L"Subtítulos", L"자막", L"字幕", L"ترجمة",
							L"Субтитры", L"Untertitel", L"Legendas", L"Ondertitel", L"Napisy", L"Altyazı"),
						n + 1);
				++n;
			}
			streamMap.subtitleCount = n;
			if (n > 0)
				streamMap.subtitleStart = streamidx2[0];
		}
	}

	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);

	// ウィンドウ時は倍率サブメニュー(現在の倍率にチェック)
	if (!savedata.fs) {
		CCustomPopupMenu* scaleSub = menu.AddSubMenu(
			LL14(L"ウィンドウ倍率", L"Window scale", L"Echelle fenetre", L"Scala finestra",
				L"Escala de ventana", L"창 배율", L"窗口倍率", L"مقياس النافذة",
				L"Масштаб окна", L"Fensterskalierung", L"Escala da janela", L"Vensterschaling",
				L"Skala okna", L"Pencere olcegi"),
			LL14(L"動画ウィンドウの表示倍率を切り替える", L"Switch the video window display scale",
				L"Changer l'echelle d'affichage de la fenetre video", L"Cambia la scala di visualizzazione della finestra video",
				L"Cambiar la escala de visualizacion de la ventana de video", L"동영상 창 표시 배율 전환",
				L"切换视频窗口显示倍率", L"تبديل مقياس عرض نافذة الفيديو",
				L"Переключить масштаб окна видео", L"Anzeigeskalierung des Videofensters umschalten",
				L"Mudar a escala de exibicao da janela de video", L"Weergaveschaal van het videovenster wisselen",
				L"Przelacz skale wyswietlania okna wideo", L"Video penceresi goruntu olcegini degistir"));
		if (scaleSub) {
			scaleSub->AddCheck(ID_MENUITEM32771,
				LL14(L"通常(1x1)", L"Normal (1x1)", L"Normal (1x1)", L"Normale (1x1)",
					L"Normal (1x1)", L"표준 (1x1)", L"标准 (1x1)", L"عادي (1×1)",
					L"Обычный (1x1)", L"Normal (1x1)", L"Normal (1x1)", L"Normaal (1x1)",
					L"Normalny (1x1)", L"Normal (1x1)"),
				savedata.douga == 0,
				LL14(L"動画ウィンドウを等倍(1x1)に切り替える", L"Switch the video window to 1x1 scale",
					L"Passer la fenetre video a l'echelle 1x1", L"Imposta la finestra video a scala 1x1",
					L"Cambiar la ventana de video a escala 1x1", L"동영상 창을 1x1 배율로 전환",
					L"将视频窗口切换为 1x1 倍率", L"تبديل نافذة الفيديو إلى مقياس 1×1",
					L"Переключить окно видео на масштаб 1x1", L"Videofenster auf 1x1-Skalierung umschalten",
					L"Mudar a janela de video para escala 1x1", L"Videovenster naar 1x1-schaal schakelen",
					L"Przelacz okno wideo na skale 1x1", L"Video penceresini 1x1 olcege al"));
			scaleSub->AddCheck(ID_MENUITEM32773,
				LL14(L"中間(1.5x1.5)", L"Medium (1.5x1.5)", L"Moyen (1,5x1,5)", L"Medio (1.5x1.5)",
					L"Mediano (1.5x1.5)", L"중간 (1.5x1.5)", L"中等 (1.5x1.5)", L"متوسط (1.5×1.5)",
					L"Средний (1.5x1.5)", L"Mittel (1,5x1,5)", L"Médio (1.5x1.5)", L"Middel (1.5x1.5)",
					L"Średni (1.5x1.5)", L"Orta (1.5x1.5)"),
				savedata.douga == 2,
				LL14(L"動画ウィンドウを1.5倍に切り替える", L"Switch the video window to 1.5x scale",
					L"Passer la fenetre video a l'echelle 1,5x", L"Imposta la finestra video a scala 1.5x",
					L"Cambiar la ventana de video a escala 1.5x", L"동영상 창을 1.5배 배율로 전환",
					L"将视频窗口切换为 1.5x 倍率", L"تبديل نافذة الفيديو إلى مقياس 1.5×",
					L"Переключить окно видео на масштаб 1.5x", L"Videofenster auf 1,5x-Skalierung umschalten",
					L"Mudar a janela de video para escala 1.5x", L"Videovenster naar 1.5x-schaal schakelen",
					L"Przelacz okno wideo na skale 1.5x", L"Video penceresini 1.5x olcege al"));
			scaleSub->AddCheck(ID_MENUITEM32772,
				LL14(L"倍(2x2)", L"Large (2x2)", L"Grand (2x2)", L"Grande (2x2)",
					L"Grande (2x2)", L"2배 (2x2)", L"双倍 (2x2)", L"كبير (2×2)",
					L"Двойной (2x2)", L"Groß (2x2)", L"Grande (2x2)", L"Groot (2x2)",
					L"Duży (2x2)", L"Büyük (2x2)"),
				savedata.douga == 1,
				LL14(L"動画ウィンドウを2倍に切り替える", L"Switch the video window to 2x scale",
					L"Passer la fenetre video a l'echelle 2x", L"Imposta la finestra video a scala 2x",
					L"Cambiar la ventana de video a escala 2x", L"동영상 창을 2배 배율로 전환",
					L"将视频窗口切换为 2x 倍率", L"تبديل نافذة الفيديو إلى مقياس 2×",
					L"Переключить окно видео на масштаб 2x", L"Videofenster auf 2x-Skalierung umschalten",
					L"Mudar a janela de video para escala 2x", L"Videovenster naar 2x-schaal schakelen",
					L"Przelacz okno wideo na skale 2x", L"Video penceresini 2x olcege al"));
		}
		menu.AddSeparator();
	}

	{
		LPCWSTR vpref = LL14(L"映像", L"Video", L"Vidéo", L"Video",
			L"Vídeo", L"비디오", L"视频", L"فيديو",
			L"Видео", L"Video", L"Vídeo", L"Video",
			L"Wideo", L"Video");
		CCustomPopupMenu* subV = NULL;
		for (int i = 0; i < 10; ++i) {
			if (streamname1[i].IsEmpty()) continue;
			if (!subV) {
				subV = menu.AddSubMenu(
					LL14(L"映像ストリーム", L"Video Stream", L"Flux vidéo", L"Flusso video",
						L"Flujo de vídeo", L"비디오 스트림", L"视频流", L"تدفق الفيديو",
						L"Видеопоток", L"Videostream", L"Fluxo de vídeo", L"Videostream",
						L"Strumień wideo", L"Video Akışı"),
					LL14(L"再生する映像ストリームを選ぶ", L"Choose which video stream to play",
						L"Choisir le flux video a lire", L"Scegli il flusso video da riprodurre",
						L"Elegir que flujo de video reproducir", L"재생할 비디오 스트림 선택",
						L"选择要播放的视频流", L"اختر تدفق الفيديو للتشغيل",
						L"Выбрать видеопоток для воспроизведения", L"Videostream zum Abspielen wählen",
						L"Escolher qual fluxo de video reproduzir", L"Kies welke videostream af te spelen",
						L"Wybierz strumien wideo do odtwarzania", L"Oynatilacak video akisini sec"));
				if (!subV) break;
			}
			CString buf;
			buf.Format(L"%s %d:%s", vpref, i + 1, (LPCWSTR)streamname1[i]);
			subV->AddCommand(ID_MV1 + i, buf);
		}
	}
	{
		LPCWSTR apref = LL14(L"音声", L"Audio", L"Audio", L"Audio",
			L"Audio", L"오디오", L"音频", L"صوت",
			L"Аудио", L"Audio", L"Áudio", L"Audio",
			L"Audio", L"Ses");
		CCustomPopupMenu* subA = NULL;
		const int nAud = (audionum > 40) ? 40 : audionum;
		for (int i = 0; i < nAud; ++i) {
			if (!subA) {
				subA = menu.AddSubMenu(
					LL14(L"音声ストリーム", L"Audio Stream", L"Flux audio", L"Flusso audio",
						L"Flujo de audio", L"오디오 스트림", L"音频流", L"تدفق الصوت",
						L"Аудиопоток", L"Audiostream", L"Fluxo de áudio", L"Audiostream",
						L"Strumień audio", L"Ses Akışı"),
					LL14(L"再生する音声ストリームを選ぶ", L"Choose which audio stream to play",
						L"Choisir le flux audio a lire", L"Scegli il flusso audio da riprodurre",
						L"Elegir que flujo de audio reproducir", L"재생할 오디오 스트림 선택",
						L"选择要播放的音频流", L"اختر تدفق الصوت للتشغيل",
						L"Выбрать аудиопоток для воспроизведения", L"Audiostream zum Abspielen wählen",
						L"Escolher qual fluxo de audio reproduzir", L"Kies welke audiostream af te spelen",
						L"Wybierz strumien audio do odtwarzania", L"Oynatilacak ses akisini sec"));
				if (!subA) break;
			}
			CString buf;
			buf.Format(L"%s %d:%s", apref, i + 1, (LPCWSTR)streamname[i]);
			subA->AddCommand(ID_ST1 + i, buf);
		}
	}
	{
		LPCWSTR spref = LL14(L"字幕", L"Subtitle", L"Sous-titres", L"Sottotitoli",
			L"Subtítulos", L"자막", L"字幕", L"ترجمة",
			L"Субтитры", L"Untertitel", L"Legendas", L"Ondertitel",
			L"Napisy", L"Altyazı");
		// 音声の下・EQの上に常時表示（0件でも「オフ」を出す。iam 無し時は切替は no-op）
		CCustomPopupMenu* subS = menu.AddSubMenu(
			LL14(L"字幕ストリーム", L"Subtitle Stream", L"Flux de sous-titres", L"Flusso sottotitoli",
				L"Flujo de subtítulos", L"자막 스트림", L"字幕流", L"تدفق الترجمة",
				L"Поток субтитров", L"Untertitelstream", L"Fluxo de legendas", L"Ondertitelstream",
				L"Strumień napisów", L"Altyazı Akışı"),
			LL14(L"表示する字幕ストリームを選ぶ", L"Choose which subtitle stream to show",
				L"Choisir le flux de sous-titres a afficher", L"Scegli il flusso sottotitoli da mostrare",
				L"Elegir que flujo de subtitulos mostrar", L"표시할 자막 스트림 선택",
				L"选择要显示的字幕流", L"اختر تدفق الترجمة للعرض",
				L"Выбрать поток субтитров для показа", L"Untertitelstream zum Anzeigen wählen",
				L"Escolher qual fluxo de legendas mostrar", L"Kies welke ondertitelstream te tonen",
				L"Wybierz strumien napisow do wyswietlenia", L"Gosterilecek altyazi akisini sec"));
		if (subS) {
			subS->AddCommand(ID_DOUGA_SUBOFF,
				LL14(L"オフ（字幕なし）", L"Off (no subtitles)", L"Desactive (sans sous-titres)", L"Off (nessun sottotitolo)",
					L"Desactivado (sin subtitulos)", L"끄기(자막 없음)", L"关闭（无字幕）", L"إيقاف (بدون ترجمة)",
					L"Выкл (без субтитров)", L"Aus (keine Untertitel)", L"Desligado (sem legendas)", L"Uit (geen ondertitels)",
					L"Wylacz (bez napisow)", L"Kapali (altyazi yok)"),
				LL14(L"字幕ストリームを無効にする", L"Disable subtitle stream", L"Desactiver les sous-titres", L"Disattiva sottotitoli",
					L"Desactivar subtitulos", L"자막 스트림 끄기", L"禁用字幕流", L"تعطيل الترجمة",
					L"Отключить субтитры", L"Untertitel deaktivieren", L"Desativar legendas", L"Ondertitels uitzetten",
					L"Wylacz napisy", L"Altyaziyi kapat"));
			const int nSub = (streamMap.subtitleCount > 40) ? 40 : streamMap.subtitleCount;
			for (int i = 0; i < nSub; ++i) {
				CString buf;
				if (!streamname2[i].IsEmpty())
					buf.Format(L"%s %d:%s", spref, i + 1, (LPCWSTR)streamname2[i]);
				else
					buf.Format(L"%s %d", spref, i + 1);
				subS->AddCommand(ID_ETC1 + i, buf);
			}
		}
	}

	// イコライザー（字幕の下）
	{
		CCustomPopupMenu* eqRoot = menu.AddSubMenu(
			LL14(L"イコライザー", L"Equalizer", L"Egaliseur", L"Equalizzatore", L"Ecualizador",
				L"이퀄라이저", L"均衡器", L"معادل", L"Эквалайзер", L"Equalizer",
				L"Equalizador", L"Equalizer", L"Equalizer", L"Ekualizer"),
			LL14(L"動画音声のEQ帯域・グローバル・環境を調整", L"Adjust EQ bands, global, and environment for video audio",
				L"Regler bandes/global/env pour l'audio video", L"Regola bande/global/env audio video",
				L"Ajustar bandas/global/entorno del audio de video", L"동영상 오디오 EQ/글로벌/환경 조정",
				L"调整视频音频的频段/全局/环境", L"ضبط نطاقات/عام/بيئة صوت الفيديو",
				L"Настроить полосы/глобал/среду видеоаудио", L"EQ-Baender/Global/Umgebung fuer Videoton",
				L"Ajustar bandas/global/ambiente do audio de video", L"EQ-banden/globaal/omgeving voor video-audio",
				L"Dostosuj pasma/global/srodowisko audio wideo", L"Video sesi EQ/global/ortam ayarla"));
		if (eqRoot) {
			CCustomPopupMenu* bands = eqRoot->AddSubMenu(
				LL14(L"周波数帯", L"Frequency bands", L"Bandes", L"Bande", L"Bandas",
					L"주파수 대역", L"频段", L"نطاقات", L"Полосы", L"Baender",
					L"Bandas", L"Banden", L"Pasma", L"Bantlar"),
				LL14(L"15帯域ゲインとプリセット", L"15-band gain and presets",
					L"Gain 15 bandes et presets", L"Guadagno 15 bande e preset",
					L"Ganancia 15 bandas y presets", L"15대역 게인 및 프리셋",
					L"15 频段增益与预设", L"ربح 15 نطاقاً والإعدادات",
					L"Усиление 15 полос и пресеты", L"15-Band-Gain und Presets",
					L"Ganho 15 bandas e presets", L"15-bands gain en presets",
					L"Wzmocnienie 15 pasm i presety", L"15 bant kazanc ve onayarlar"));
			if (bands) {
				bands->AddButton(0,
					LL14(L"イコライザーリセット", L"Reset equalizer", L"Reinit egaliseur", L"Reset equalizzatore", L"Restablecer ecualizador",
						L"이퀄라이저 재설정", L"重置均衡器", L"إعادة المعادل", L"Сброс эквалайзера", L"Equalizer zuruecksetzen",
						L"Redefinir equalizador", L"Equalizer resetten", L"Reset equalizera", L"Ekualizeri sifirla"),
					DougaEqResetBandsCb, NULL,
					LL14(L"帯域をすべて100に戻す", L"Reset all bands to 100",
						L"Remettre toutes les bandes a 100", L"Ripristina tutte le bande a 100",
						L"Restablecer todas las bandas a 100", L"모든 대역을 100으로",
						L"将所有频段恢复为 100", L"إعادة كل النطاقات إلى 100",
						L"Сбросить все полосы на 100", L"Alle Baender auf 100",
						L"Redefinir todas as bandas para 100", L"Alle banden naar 100",
						L"Ustaw wszystkie pasma na 100", L"Tum bantlari 100 yap"),
					FALSE);
				static const wchar_t* kPreNames[MP_EQ_PRESET_COUNT];
				for (int pi = 0; pi < MP_EQ_PRESET_COUNT; ++pi)
					kPreNames[pi] = MpEqPresetLabel(pi);
				int preCur = savedata.eqsoundeq;
				if (preCur < 0) preCur = 0;
				if (preCur > MP_EQ_PRESET_COUNT - 1) preCur = MP_EQ_PRESET_COUNT - 1;
				bands->AddCombo(
					LL14(L"プリセット", L"Preset", L"Preset", L"Preset", L"Preset", L"프리셋", L"预设", L"مسبق", L"Пресет", L"Preset", L"Preset", L"Preset", L"Preset", L"Onayar"),
					kPreNames, MP_EQ_PRESET_COUNT, preCur, DougaEqPresetComboCb, NULL,
					LL14(L"EQカーブプリセット", L"EQ curve preset", L"Preset courbe EQ", L"Preset curva EQ", L"Preset curva EQ",
						L"EQ 커브 프리셋", L"EQ 曲线预设", L"إعداد منحنى EQ", L"Пресет кривой EQ", L"EQ-Kurven-Preset",
						L"Preset de curva EQ", L"EQ-curvepreset", L"Preset krzywej EQ", L"EQ egri onayari"));
				static const wchar_t* kBandLab[15] = {
					L"25Hz", L"40Hz", L"63Hz", L"100Hz", L"160Hz", L"250Hz", L"400Hz", L"630Hz",
					L"1kHz", L"1.6kHz", L"2.5kHz", L"4kHz", L"6.3kHz", L"10kHz", L"16kHz"
				};
				for (int bi = 0; bi < 15; ++bi) {
					int v = savedata.eq[bi];
					if (v < 0) v = 0;
					if (v > 200) v = 200;
					bands->AddSlider(kBandLab[bi], 0, 200, v, DougaEqBandSliderCb, (void*)(INT_PTR)bi,
						LL14(L"帯域ゲイン（ドラッグ中反映）", L"Band gain (live)", L"Gain bande (direct)", L"Guadagno banda (live)", L"Ganancia banda (en vivo)",
							L"대역 게인(즉시)", L"频段增益（即时）", L"ربح النطاق (مباشر)", L"Усиление полосы (сразу)", L"Bandgain (live)",
							L"Ganho da banda (ao vivo)", L"Bandgain (live)", L"Wzmocnienie pasma (na zywo)", L"Bant kazanci (anlik)"));
				}
			}
			CCustomPopupMenu* glob = eqRoot->AddSubMenu(
				LL14(L"グローバル", L"Global", L"Global", L"Globale", L"Global",
					L"글로벌", L"全局", L"عام", L"Глобально", L"Global",
					L"Global", L"Globaal", L"Globalnie", L"Kuresel"),
				LL14(L"マスター・明瞭・バランス・密度・立体・FX", L"Master, clarity, balance, density, spatial, FX",
					L"Master, clarte, balance, densite, spatial, FX", L"Master, chiarezza, bilanciamento, densita, spaziale, FX",
					L"Master, claridad, balance, densidad, espacial, FX", L"마스터·명료·밸런스·밀도·입체·FX",
					L"主音量、清晰、平衡、密度、立体、FX", L"الماستر والوضوح والتوازن والكثافة والمكاني وFX",
					L"Мастер, ясность, баланс, плотность, пространство, FX", L"Master, Klarheit, Balance, Dichte, Raum, FX",
					L"Master, clareza, balanco, densidade, espacial, FX", L"Master, helderheid, balans, dichtheid, ruimtelijk, FX",
					L"Master, jasnosc, balans, gestosc, przestrzen, FX", L"Master, netlik, denge, yogunluk, uzamsal, FX"));
			if (glob) {
				glob->AddButton(0,
					LL14(L"グローバルリセット", L"Reset global", L"Reinit global", L"Reset globale", L"Restablecer global",
						L"글로벌 재설정", L"重置全局", L"إعادة العام", L"Сброс глобальных", L"Global zuruecksetzen",
						L"Redefinir global", L"Globaal resetten", L"Reset globalny", L"Kureseli sifirla"),
					DougaEqResetGlobalCb, NULL,
					LL14(L"グローバルとFXを初期値へ", L"Reset global and FX to defaults",
						L"Remettre global et FX par defaut", L"Ripristina globale e FX",
						L"Restablecer global y FX", L"글로벌과 FX를 기본값으로",
						L"将全局和 FX 恢复默认", L"إعادة العام وFX للافتراضي",
						L"Сбросить глобальные и FX", L"Global und FX zuruecksetzen",
						L"Redefinir global e FX", L"Globaal en FX resetten",
						L"Reset globalny i FX", L"Kuresel ve FX sifirla"),
					FALSE);
				static const struct { int key; const wchar_t* lab; } kG[] = {
					{ 15, L"Master" }, { 16, L"Clarity" }, { 17, L"Balance" }, { 18, L"Density" }, { 19, L"Spatial" },
					{ 20, L"Reverb" }, { 21, L"Chorus" }, { 22, L"Delay" }
				};
				for (int gi = 0; gi < 8; ++gi) {
					int v = (kG[gi].key <= 19) ? savedata.eq[kG[gi].key]
						: (kG[gi].key == 20) ? savedata.eq_reverb
						: (kG[gi].key == 21) ? savedata.eq_chorus : savedata.eq_delay;
					if (v < 0) v = 0;
					if (v > 200) v = 200;
					glob->AddSlider(kG[gi].lab, 0, 200, v, DougaEqGlobalSliderCb, (void*)(INT_PTR)kG[gi].key, NULL);
				}
			}
			CCustomPopupMenu* env = eqRoot->AddSubMenu(
				LL14(L"環境", L"Environment", L"Environnement", L"Ambiente", L"Entorno",
					L"환경", L"环境", L"بيئة", L"Среда", L"Umgebung",
					L"Ambiente", L"Omgeving", L"Srodowisko", L"Ortam"),
				LL14(L"部屋の響きプリセットとかかり具合", L"Room ambience preset and wet amount",
					L"Preset d'ambiance et quantite wet", L"Preset ambiente e quantita wet",
					L"Preset de entorno y cantidad wet", L"공간 프리셋과 적용량",
					L"房间环境预设与湿量", L"إعداد البيئة وكمية التأثير",
					L"Пресет среды и сила эффекта", L"Raum-Preset und Wet-Anteil",
					L"Preset de ambiente e quantidade wet", L"Omgevingspreset en wet-hoeveelheid",
					L"Preset srodowiska i ilosc wet", L"Ortam onayari ve wet miktari"));
			if (env) {
				static const wchar_t* kEnvNames[MP_REMOTE_EQ_ENV_COUNT];
				static BOOL kEnvInit = FALSE;
				if (!kEnvInit) {
					for (int ei = 0; ei < MP_REMOTE_EQ_ENV_COUNT; ++ei)
						kEnvNames[ei] = MpRemoteEqEnvLabel(ei);
					kEnvInit = TRUE;
				}
				int envCur = savedata.eqsoundenv;
				if (envCur < 0) envCur = 0;
				if (envCur >= MP_REMOTE_EQ_ENV_COUNT) envCur = MP_REMOTE_EQ_ENV_COUNT - 1;
				env->AddCombo(
					LL14(L"環境プリセット", L"Env preset", L"Preset env", L"Preset amb", L"Preset entorno",
						L"환경 프리셋", L"环境预设", L"إعداد بيئة", L"Пресет среды", L"Umgebungs-Preset",
						L"Preset ambiente", L"Omgevingspreset", L"Preset srodowiska", L"Ortam onayari"),
					kEnvNames, MP_REMOTE_EQ_ENV_COUNT, envCur, DougaEqEnvComboCb, NULL, NULL);
				int eff = savedata.eqsoundeffect * 2;
				if (eff < 0) eff = 0;
				if (eff > 200) eff = 200;
				env->AddSlider(
					LL14(L"かかり具合", L"Effect amount", L"Quantite effet", L"Quantita effetto", L"Cantidad efecto",
						L"적용량", L"效果强度", L"قوة التأثير", L"Сила эффекта", L"Effektstaerke",
						L"Quantidade efeito", L"Effecthoeveelheid", L"Sila efektu", L"Efekt miktari"),
					0, 200, eff, DougaEqGlobalSliderCb, (void*)(INT_PTR)23,
					LL14(L"環境エフェクトのウェット量", L"Environment effect wet amount",
						L"Quantite wet de l'effet d'ambiance", L"Quantita wet effetto ambiente",
						L"Cantidad wet del efecto de entorno", L"환경 효과 Wet 양",
						L"环境效果 Wet 量", L"مقدار Wet لتأثير البيئة",
						L"Wet эффекта среды", L"Wet des Umgebungseffekts",
						L"Quantidade wet do efeito de ambiente", L"Wet van omgevingseffect",
						L"Wet efektu srodowiska", L"Ortam efekti wet miktari"));
			}
		}
	}

	// 再生速度: IMediaSeeking がレートを返せるときだけスライダー(0.1x..4.0x、ドラッグ中ライブ)
	{
		double cur = 1.0;
		if (pMediaSeeking && SUCCEEDED(pMediaSeeking->GetRate(&cur))) {
			if (cur <= 0.0) cur = 1.0;
			int pos = (int)(cur * 100.0 + 0.5);
			if (pos < 10) pos = 10;
			if (pos > 400) pos = 400;
			menu.AddSlider(
				LL14(L"再生速度", L"Playback speed", L"Vitesse de lecture", L"Velocita di riproduzione",
					L"Velocidad de reproduccion", L"재생 속도", L"播放速度", L"سرعة التشغيل",
					L"Скорость воспроизведения", L"Wiedergabegeschwindigkeit", L"Velocidade de reproducao",
					L"Afspeelsnelheid", L"Predkosc odtwarzania", L"Oynatma hizi"),
				10, 400, pos, DougaRateSliderCb, NULL,
				LL14(L"0.1x〜4.0x（ドラッグ中に即反映）。ゲーム合成時はテンポと連動", L"0.1x–4.0x (live while dragging). Syncs with tempo in game+video",
					L"0,1x–4,0x (en direct). Lie au tempo en jeu+video", L"0,1x–4,0x (in tempo reale). Collegato al tempo",
					L"0,1x–4,0x (en vivo). Sincroniza con tempo", L"0.1x–4.0x (드래그 중 즉시). 게임+영상 시 템포 연동", L"0.1x–4.0x（拖动即时）。游戏合成时与速度联动",
					L"0.1x–4.0x (مباشر). يتزامن مع الإيقاع", L"0.1x–4.0x (сразу). Связано с темпом",
					L"0,1x–4,0x (live). Mit Tempo gekoppelt", L"0,1x–4,0x (ao arrastar). Liga ao tempo",
					L"0,1x–4,0x (live). Gekoppeld aan tempo", L"0,1x–4,0x (na zywo). Zsynchronizowane z tempo", L"0.1x–4.0x (suruklerken). Tempo ile bagli"));
			{
				static const double kRates[8] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0 };
				static const wchar_t* const kRateLabels[8] = {
					L"0.25x", L"0.5x", L"0.75x", L"1.0x", L"1.25x", L"1.5x", L"2.0x", L"4.0x"
				};
				CCustomPopupMenu* spdSub = menu.AddSubMenu(
					LL14(L"再生速度プリセット", L"Speed presets", L"Presets de vitesse", L"Preset velocita",
						L"Presets de velocidad", L"재생 속도 프리셋", L"播放速度预设", L"إعدادات السرعة",
						L"Пресеты скорости", L"Geschwindigkeits-Presets", L"Presets de velocidade",
						L"Snelheidpresets", L"Presety predkosci", L"Hiz onayarlari"),
					LL14(L"よく使う再生速度へすぐ切り替える", L"Jump to a common playback rate",
						L"Passer vite a une vitesse de lecture courante", L"Passa subito a una velocita comune",
						L"Saltar a una velocidad de reproduccion comun", L"자주 쓰는 재생 속도로 바로 전환",
						L"快速切换到常用播放速度", L"الانتقال سريعاً إلى سرعة تشغيل شائعة",
						L"Быстро перейти к обычной скорости", L"Schnell zu einer üblichen Rate wechseln",
						L"Ir logo para uma velocidade comum", L"Snel naar een gangbare afspeelsnelheid",
						L"Szybko przelacz na typowa predkosc", L"Sik kullanilan oynatma hizina atla"));
				if (spdSub) {
					for (int i = 0; i < 8; ++i) {
						const BOOL on = (cur > kRates[i] - 0.01 && cur < kRates[i] + 0.01);
						spdSub->AddCheck(ID_DOUGA_SPEED_FIRST + i, kRateLabels[i], on);
					}
				}
			}
		}
	}

	menu.AddSeparator();
	{
		CCustomPopupMenu* playSub = menu.AddSubMenu(
			LL14(L"再生", L"Playback", L"Lecture", L"Riproduzione", L"Reproduccion", L"재생", L"播放", L"تشغيل", L"Воспроизведение", L"Wiedergabe", L"Reproducao", L"Afspelen", L"Odtwarzanie", L"Calma"),
			LL14(L"一時停止・再生・停止・前後・シーク・消音・全画面・フェード・閉じる", L"Pause, play, stop, prev/next, seek, mute, fullscreen, fade, close",
				L"Pause, lecture, arret, prev/suiv, seek, mute, plein ecran, fondu, fermer", L"Pausa, play, stop, prec/succ, seek, mute, schermo intero, fade, chiudi",
				L"Pausa, play, stop, ant/sig, seek, silencio, pantalla completa, fade, cerrar", L"일시정지·재생·중지·이전/다음·시크·음소거·전체화면·페이드·닫기",
				L"暂停、播放、停止、前后、定位、静音、全屏、淡出、关闭", L"إيقاف مؤقت وتشغيل وإيقاف وسابق/تالٍ وتقديم وكتم وملء الشاشة وتلاشي وإغلاق",
				L"Пауза, играть, стоп, назад/вперёд, перемотка, mute, полный экран, fade, закрыть", L"Pause, Play, Stop, Zurück/Weiter, Seek, Mute, Vollbild, Fade, Schließen",
				L"Pausar, play, stop, ant/prox, seek, mudo, tela cheia, fade, fechar", L"Pauze, play, stop, vorige/volgende, seek, mute, volledig scherm, fade, sluiten",
				L"Pauza, odtworz, stop, poprz/nast, seek, wycisz, pelny ekran, fade, zamknij", L"Duraklat, cal, durdur, onceki/sonraki, seek, sessiz, tam ekran, fade, kapat"));
		if (playSub) {
			playSub->AddCommand(32775,
				LL14(L"一時停止/再開 (&C)", L"Pause/Resume (&C)", L"Pause/Reprendre (&C)", L"Pausa/Riprendi (&C)",
					L"Pausa/Reanudar (&C)", L"일시정지/재개 (&C)", L"暂停/继续 (&C)", L"إيقاف مؤقت/استئناف (&C)",
					L"Пауза/Возобновить (&C)", L"Pause/Fortsetzen (&C)", L"Pausar/Retomar (&C)", L"Pauzeren/Hervatten (&C)",
					L"Pauza/Wznów (&C)", L"Duraklat/Devam Et (&C)"),
				LL14(L"再生の一時停止と再開を切り替える（ショートカット C）", L"Toggle pause/resume playback (shortcut C)",
					L"Basculer pause/reprise (raccourci C)", L"Alterna pausa/ripresa (scorciatoia C)",
					L"Alternar pausa/reanudacion (atajo C)", L"일시정지/재개를 전환 (단축키 C)",
					L"切换暂停/继续播放（快捷键 C）", L"تبديل الإيقاف المؤقت/الاستئناف (اختصار C)",
					L"Переключить паузу/возобновление (клавиша C)", L"Pause/Fortsetzen umschalten (Shortcut C)",
					L"Alternar pausar/retomar (atalho C)", L"Pauze/hervatten wisselen (sneltoets C)",
					L"Przelacz pauze/wznowienie (skrot C)", L"Duraklat/devam et (kisayol C)"));
			playSub->AddCommand(ID_DOUGA_PLAY,
				LL14(L"再生", L"Play", L"Lecture", L"Riproduci", L"Reproducir", L"재생", L"播放", L"تشغيل", L"Воспроизведение", L"Abspielen", L"Reproduzir", L"Afspelen", L"Odtwórz", L"Çal"),
				LL14(L"動画の再生を開始する", L"Start video playback",
					L"Demarrer la lecture video", L"Avvia la riproduzione video",
					L"Iniciar la reproduccion de video", L"동영상 재생 시작",
					L"开始视频播放", L"بدء تشغيل الفيديو",
					L"Начать воспроизведение видео", L"Videowiedergabe starten",
					L"Iniciar reproducao de video", L"Videoweergave starten",
					L"Rozpocznij odtwarzanie wideo", L"Video oynatmayi baslat"));
			playSub->AddCommand(ID_DOUGA_STOP,
				LL14(L"停止", L"Stop", L"Arrêt", L"Stop", L"Detener", L"중지", L"停止", L"إيقاف", L"Стоп", L"Stop", L"Parar", L"Stoppen", L"Stop", L"Durdur"),
				LL14(L"再生を停止する", L"Stop playback",
					L"Arreter la lecture", L"Ferma la riproduzione",
					L"Detener la reproduccion", L"재생 중지",
					L"停止播放", L"إيقاف التشغيل",
					L"Остановить воспроизведение", L"Wiedergabe stoppen",
					L"Parar a reproducao", L"Afspelen stoppen",
					L"Zatrzymaj odtwarzanie", L"Oynatmayı durdur"));
			playSub->AddCommand(ID_DOUGA_PREV,
				LL14(L"前へ", L"Previous", L"Précédent", L"Precedente", L"Anterior", L"이전", L"上一首", L"السابق", L"Предыдущий", L"Zurück", L"Anterior", L"Vorige", L"Poprzedni", L"Önceki"),
				LL14(L"前のトラックへ移る", L"Go to the previous track",
					L"Aller a la piste precedente", L"Vai al brano precedente",
					L"Ir a la pista anterior", L"이전 트랙으로 이동",
					L"转到上一曲", L"الانتقال إلى المقطع السابق",
					L"Перейти к предыдущему треку", L"Zum vorherigen Titel wechseln",
					L"Ir para a faixa anterior", L"Ga naar het vorige nummer",
					L"Przejdz do poprzedniego utworu", L"Onceki parcaya git"));
			playSub->AddCommand(ID_DOUGA_NEXT,
				LL14(L"次へ", L"Next", L"Suivant", L"Successivo", L"Siguiente", L"다음", L"下一首", L"التالي", L"Следующий", L"Weiter", L"Próximo", L"Volgende", L"Następny", L"Sonraki"),
				LL14(L"次のトラックへ移る", L"Go to the next track",
					L"Aller a la piste suivante", L"Vai al brano successivo",
					L"Ir a la pista siguiente", L"다음 트랙으로 이동",
					L"转到下一曲", L"الانتقال إلى المقطع التالي",
					L"Перейти к следующему треку", L"Zum nächsten Titel wechseln",
					L"Ir para a proxima faixa", L"Ga naar het volgende nummer",
					L"Przejdz do nastepnego utworu", L"Sonraki parcaya git"));
			playSub->AddCommand(ID_DOUGA_REW,
				LL14(L"戻す", L"Rewind", L"Reculer", L"Indietro", L"Retroceder", L"되감기", L"快退", L"ترجيع", L"Назад", L"Zurückspulen", L"Voltar", L"Terugspoelen", L"Przewiń wstecz", L"Geri sar"),
				LL14(L"再生位置を少し戻す", L"Seek backward a short step",
					L"Reculer un peu dans la lecture", L"Indietreggia di un piccolo passo",
					L"Retroceder un poco en la reproduccion", L"재생 위치를 조금 되돌림",
					L"将播放位置稍微回退", L"الرجوع قليلاً في موضع التشغيل",
					L"Немного перемотать назад", L"Wiedergabeposition etwas zurückspulen",
					L"Voltar um pouco na reproducao", L"Kort terugspoelen in de weergave",
					L"Cofnij pozycje odtwarzania o krok", L"Oynatma konumunu biraz geri sar"));
			playSub->AddCommand(ID_DOUGA_FF,
				LL14(L"進める", L"Fast forward", L"Avancer", L"Avanti", L"Avanzar", L"빨리감기", L"快进", L"تقديم", L"Вперёд", L"Vorspulen", L"Avançar", L"Vooruitspoelen", L"Przewiń naprzód", L"İleri sar"),
				LL14(L"再生位置を少し進める", L"Seek forward a short step",
					L"Avancer un peu dans la lecture", L"Avanza di un piccolo passo",
					L"Avanzar un poco en la reproduccion", L"재생 위치를 조금 앞으로",
					L"将播放位置稍微快进", L"التقديم قليلاً في موضع التشغيل",
					L"Немного перемотать вперёд", L"Wiedergabeposition etwas vorspulen",
					L"Avancar um pouco na reproducao", L"Kort vooruitspoelen in de weergave",
					L"Przewin pozycje odtwarzania o krok", L"Oynatma konumunu biraz ileri sar"));
			playSub->AddCommand(ID_DOUGA_MUTE,
				LL14(L"消音", L"Mute", L"Muet", L"Mute", L"Silencio", L"음소거", L"静音", L"كتم", L"Без звука", L"Stumm", L"Mudo", L"Dempen", L"Wycisz", L"Sessiz"),
				LL14(L"音声のミュートを切り替える", L"Toggle audio mute",
					L"Basculer le mute audio", L"Attiva/disattiva mute audio",
					L"Alternar silencio de audio", L"오디오 음소거 전환",
					L"切换音频静音", L"تبديل كتم الصوت",
					L"Переключить отключение звука", L"Stummschaltung umschalten",
					L"Alternar mudo de audio", L"Audiomute wisselen",
					L"Przelacz wyciszenie audio", L"Ses susturmayı ac/kapat"));
			playSub->AddCommand(ID_DOUGA_FS,
				LL14(L"フルスクリーン", L"Fullscreen", L"Plein écran", L"Schermo intero", L"Pantalla completa", L"전체화면", L"全屏", L"ملء الشاشة", L"Полный экран", L"Vollbild", L"Tela cheia", L"Volledig scherm", L"Pełny ekran", L"Tam ekran"),
				LL14(L"フルスクリーン表示を切り替える", L"Toggle fullscreen display",
					L"Basculer le plein ecran", L"Attiva/disattiva schermo intero",
					L"Alternar pantalla completa", L"전체화면 표시 전환",
					L"切换全屏显示", L"تبديل العرض بملء الشاشة",
					L"Переключить полноэкранный режим", L"Vollbildanzeige umschalten",
					L"Alternar tela cheia", L"Volledig scherm wisselen",
					L"Przelacz pelny ekran", L"Tam ekran gorunumunu ac/kapat"));
			playSub->AddCommand(ID_DOUGA_FADE,
				LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza", L"Desvanecer", L"페이드 아웃", L"淡出", L"تلاشي", L"Затухание", L"Ausblenden", L"Desvanecer", L"Uitfaden", L"Zanikanie", L"Soluklaştır"),
				LL14(L"音量をフェードアウトして停止する", L"Fade out volume and stop",
					L"Faire un fondu du volume puis arreter", L"Dissolvi il volume e ferma",
					L"Desvanecer el volumen y detener", L"볼륨을 페이드 아웃 후 정지",
					L"淡出音量并停止", L"تلاشي مستوى الصوت ثم الإيقاف",
					L"Затухание громкости и остановка", L"Lautstarke ausblenden und stoppen",
					L"Desvanecer o volume e parar", L"Volume uitfaden en stoppen",
					L"Zanikaj glosnosc i zatrzymaj", L"Sesi soluklastirip durdur"));
			playSub->AddCommand(ID_DOUGA_CLOSE,
				LL14(L"動画画面を閉じる", L"Close video window", L"Fermer l'ecran video", L"Chiudi finestra video",
					L"Cerrar pantalla de video", L"동영상 화면 닫기", L"关闭视频窗口", L"إغلاق شاشة الفيديو",
					L"Закрыть окно видео", L"Videofenster schliessen", L"Fechar janela de video", L"Videovenster sluiten",
					L"Zamknij okno wideo", L"Video penceresini kapat"),
				LL14(L"動画ウィンドウを閉じる（動画専用なら再生も停止）", L"Close the video window (also stops video-only playback)",
					L"Fermer la fenetre video (arrete aussi en lecture video seule)", L"Chiudi la finestra video (ferma anche in solo video)",
					L"Cerrar la ventana de video (tambien detiene solo video)", L"동영상 창 닫기(동영상 전용이면 재생도 중지)",
					L"关闭视频窗口（纯视频时也会停止播放）", L"إغلاق نافذة الفيديو (يوقف أيضاً التشغيل للفيديو فقط)",
					L"Закрыть окно видео (также останавливает только видео)", L"Videofenster schliessen (stoppt auch bei Nur-Video)",
					L"Fechar a janela de video (tambem para so video)", L"Videovenster sluiten (stopt ook bij alleen-video)",
					L"Zamknij okno wideo (zatrzymuje tez samo wideo)", L"Video penceresini kapat (yalniz videoda oynatmayi da durdurur)"));
		}
	}

	menu.AddSeparator();
	{
		CCustomPopupMenu* viewSub = menu.AddSubMenu(
			LL14(L"表示", L"Display", L"Affichage", L"Visualizzazione", L"Visualizacion", L"표시", L"显示", L"عرض", L"Отображение", L"Anzeige", L"Exibicao", L"Weergave", L"Wyswietlanie", L"Gorunum"),
			LL14(L"常に手前・アスペクト比など表示オプション", L"Always-on-top, aspect ratio, and other display options",
				L"Toujours au premier plan, proportions et autres options d'affichage", L"Sempre in primo piano, proporzioni e altre opzioni di visualizzazione",
				L"Siempre visible, proporcion y otras opciones de visualizacion", L"항상 위·화면 비율 등 표시 옵션",
				L"置顶、宽高比等显示选项", L"خيارات العرض مثل دائماً في المقدمة ونسبة العرض",
				L"Поверх всех, пропорции и другие параметры отображения", L"Immer im Vordergrund, Seitenverhaltnis und weitere Anzeigeoptionen",
				L"Sempre no topo, proporcao e outras opcoes de exibicao", L"Altijd bovenop, beeldverhouding en andere weergaveopties",
				L"Zawsze na wierzchu, proporcje i inne opcje wyswietlania", L"Her zaman ustte, en-boy orani ve diger gorunum secenekleri"));
		if (viewSub) {
			viewSub->AddCheck(ID_DOUGA_TOPMOST,
				LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano",
					L"Siempre visible", L"항상 위에 표시", L"总在最前面", L"دائمًا في المقدمة",
					L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre visivel", L"Altijd op voorgrond",
					L"Zawsze na wierzchu", L"Her zaman ustte"),
				savedata.dougatopmost != 0,
				LL14(L"他のウィンドウより常に手前に表示する", L"Keep this window always above others",
					L"Garder cette fenetre toujours au-dessus des autres", L"Mantieni questa finestra sopra le altre",
					L"Mantener esta ventana siempre encima de las demas", L"다른 창보다 항상 위에 표시",
					L"始终将此窗口置于其他窗口之上", L"إبقاء هذه النافذة دائماً فوق الأخرى",
					L"Держать это окно поверх остальных", L"Dieses Fenster immer über anderen halten",
					L"Manter esta janela sempre acima das outras", L"Dit venster altijd boven andere houden",
					L"Trzymaj to okno zawsze nad innymi", L"Bu pencereyi her zaman digerlerinin ustunde tut"));
			viewSub->AddCheck(ID_DOUGA_ASPECT,
				LL14(L"アスペクト比を維持", L"Keep aspect ratio", L"Conserver les proportions", L"Mantieni proporzioni",
					L"Mantener proporcion", L"화면 비율 유지", L"保持宽高比", L"الحفاظ على نسبة العرض",
					L"Сохранять пропорции", L"Seitenverhaltnis beibehalten", L"Manter proporcao", L"Beeldverhouding behouden",
					L"Zachowaj proporcje", L"En-boy oranini koru"),
				savedata.dougaaspect != 0,
				LL14(L"黒帯で比率を保つ／オフで伸ばして埋める", L"Letterbox to keep ratio; off stretches to fill",
					L"Bandes noires pour garder le ratio; off etire pour remplir", L"Bande nere per il ratio; off allunga per riempire",
					L"Bandas negras para ratio; off estira para llenar", L"레터박스로 비율 유지 / 끄면 늘려 채움",
					L"用黑边保持比例；关闭则拉伸填满", L"أشرطة سوداء للحفاظ على النسبة؛ إيقاف = تمديد للملء",
					L"Поля для пропорций; выкл. — растянуть", L"Balken fürs Verhältnis; aus = strecken zum Füllen",
					L"Barras pretas para proporcao; off estica para preencher", L"Zwarte balken voor verhouding; uit = rekken tot vullen",
					L"Pasma dla proporcji; wyl. rozciaga do wypelnienia", L"Oran icin siyah serit; kapali = doldurmak icin esnet"));
		}
	}

	menu.AddSeparator();
	menu.AddCommand(ID_DOUGA_DSFILTERS,
		LL14(L"DirectShowフィルタ一覧", L"DirectShow Filter List", L"Liste des filtres DirectShow", L"Elenco filtri DirectShow",
			L"Lista de filtros DirectShow", L"DirectShow 필터 목록", L"DirectShow 过滤器列表", L"قائمة مرشحات DirectShow",
			L"Список фильтров DirectShow", L"DirectShow-Filterliste", L"Lista de filtros DirectShow", L"DirectShow-filterlijst",
			L"Lista filtrów DirectShow", L"DirectShow Filtre Listesi"),
		LL14(L"再生中の DirectShow フィルタグラフ一覧を表示", L"Show the DirectShow filter graph list in use",
			L"Afficher la liste du graphe de filtres DirectShow", L"Mostra l'elenco del grafo filtri DirectShow",
			L"Mostrar la lista del grafo de filtros DirectShow", L"사용 중인 DirectShow 필터 그래프 목록 표시",
			L"显示正在使用的 DirectShow 滤镜图列表", L"عرض قائمة مخطط مرشحات DirectShow المستخدمة",
			L"Показать список графа фильтров DirectShow", L"DirectShow-Filtergraphenliste anzeigen",
			L"Mostrar a lista do grafo de filtros DirectShow", L"DirectShow-filtergraaflijst tonen",
			L"Pokaz liste grafu filtrow DirectShow", L"Kullanilan DirectShow filtre grafi listesini goster"),
		pGraphBuilder != NULL);

	menu.AddSeparator();
	menu.AddCommand(ID__32783,
		LL14(L"上下左右キー音量とシークが出来ます。", L"Arrow keys: volume & seek.",
			L"Touches fléchées : volume et défilement.", L"Frecce: volume e avanzamento.",
			L"Teclas de flecha: volumen y posición.", L"방향키: 음량 및 탐색.",
			L"方向键: 音量和搜索。", L"مفاتيح الأسهم: الصوت والتقديم.",
			L"Стрелки: громкость и перемотка.", L"Pfeiltasten: Lautstärke & Suche.",
			L"Teclas de seta: volume e busca.", L"Pijltoetsen: volume & zoeken.",
			L"Klawisze strzałek: głośność i wyszukiwanie.", L"Ok tuşları: ses ve arama."),
		LL14(L"↑↓で音量、←→でシーク（情報メモ）", L"Up/Down = volume, Left/Right = seek (info note)",
			L"Haut/Bas = volume, Gauche/Droite = seek (note)", L"Su/Giu = volume, Sx/Dx = seek (nota)",
			L"Arriba/Abajo = volumen, Izq/Der = seek (nota)", L"위/아래=음량, 좌/우=시크 (안내)",
			L"上下=音量，左右=定位（提示）", L"أعلى/أسفل=الصوت، يسار/يمين=Seek (ملاحظة)",
			L"↑↓ = громкость, ←→ = перемотка (справка)", L"Hoch/Runter = Lautstärke, Links/Rechts = Seek (Hinweis)",
			L"Cima/Baixo = volume, Esq/Dir = seek (nota)", L"Omhoog/Omlaag = volume, Links/Rechts = seek (tip)",
			L"Gora/Dol = glosnosc, Lewo/Prawo = seek (wskazowka)", L"Yukari/Asagi = ses, Sol/Sag = seek (bilgi)"),
		FALSE);
	menu.AddCommand(ID__32784,
		LL14(L"ダブルクリックでフルスクリーンです。", L"Double-click for fullscreen.",
			L"Double-clic pour plein écran.", L"Doppio clic per schermo intero.",
			L"Doble clic para pantalla completa.", L"더블클릭: 전체화면.",
			L"双击进入全屏。", L"انقر نقراً مزدوجاً للشاشة الكاملة.",
			L"Двойной щелчок — полный экран.", L"Doppelklick für Vollbild.",
			L"Clique duplo para tela cheia.", L"Dubbelklik voor volledig scherm.",
			L"Dwuklik dla pełnego ekranu.", L"Tam ekran için çift tıklayın."),
		LL14(L"映像上をダブルクリックでフルスクリーン切替（情報メモ）", L"Double-click the picture to toggle fullscreen (info note)",
			L"Double-clic sur l'image pour le plein ecran (note)", L"Doppio clic sull'immagine per schermo intero (nota)",
			L"Doble clic en la imagen para pantalla completa (nota)", L"화면 더블클릭으로 전체화면 전환 (안내)",
			L"双击画面切换全屏（提示）", L"انقر نقراً مزدوجاً على الصورة لملء الشاشة (ملاحظة)",
			L"Двойной щелчок по кадру — полный экран (справка)", L"Doppelklick aufs Bild für Vollbild (Hinweis)",
			L"Clique duplo na imagem para tela cheia (nota)", L"Dubbelklik op beeld voor volledig scherm (tip)",
			L"Dwuklik na obrazie dla pelnego ekranu (wskazowka)", L"Goruntuye cift tikla: tam ekran (bilgi)"),
		FALSE);
	menu.AddSeparator();
	menu.AddCommand(ID_HELP_SHOWSHEET,
		LL14(L"操作ガイド", L"Operation guide", L"Guide d'utilisation", L"Guida operativa",
			L"Guía de operación", L"조작 가이드", L"操作指南", L"دليل التشغيل",
			L"Руководство", L"Bedienungsanleitung", L"Guia de operação", L"Bedieningsgids",
			L"Przewodnik", L"İşlem kılavuzu"),
		LL14(L"このウィンドウの操作ガイドを表示する", L"Show the operation guide for this window",
			L"Afficher le guide d'utilisation de cette fenetre", L"Mostra la guida operativa di questa finestra",
			L"Mostrar la guia de operacion de esta ventana", L"이 창의 조작 가이드 표시",
			L"显示此窗口的操作指南", L"إظهار دليل تشغيل هذه النافذة",
			L"Показать руководство по этому окну", L"Bedienungsanleitung für dieses Fenster anzeigen",
			L"Mostrar o guia de operacao desta janela", L"Toon de handleiding voor dit venster",
			L"Pokaz przewodnik po tym oknie", L"Bu pencerenin islem kilavuzunu goster"));

	CWnd* pWndPopupOwner = this;
	while (pWndPopupOwner->GetStyle() & WS_CHILD)
		pWndPopupOwner = pWndPopupOwner->GetParent();

	const UINT cmd = menu.Track(point, pWndPopupOwner);
	if (cmd)
		pWndPopupOwner->PostMessage(WM_COMMAND, cmd);
}

//void CDouga::OnNcLButtonDown(UINT nHitTest, CPoint point)
//{
//	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
//	int a=0;
//	a=a;
//}

void CDouga::OnContextMenu(CWnd*, CPoint point)
{
	// 動画サイト経由の右クリックもここに来る。ストリーム絞り込み付きの共通処理へ。
	ShowDougaContextMenu(point);
}

// 汎用的なストリーム切替関数
BOOL CDouga::SwitchStream(int streamType, int index)
{
	if (!pGraphBuilder || !iam) return FALSE;

	int actualIndex = -1;

	switch (streamType) {
	case 0: // 映像
		if (index >= 0 && index < streamMap.videoCount && streamidx1[index] >= 0)
			actualIndex = streamidx1[index];
		break;

	case 1: // 音声
		if (index >= 0 && index < streamMap.audioCount && streamidx[index] >= 0)
			actualIndex = streamidx[index];
		break;

	case 2: // 字幕
		if (index >= 0 && index < streamMap.subtitleCount && streamidx2[index] >= 0)
			actualIndex = streamidx2[index];
		break;
	}

	// デバッグ出力
	CString debug;
	debug.Format(L"SwitchStream: type=%d, index=%d, actualIndex=%d", streamType, index, actualIndex);
	OutputDebugString(debug);

	if (actualIndex >= 0) {
		OAFilterState prevState = State_Stopped;
		if (pMediaControl)
			pMediaControl->GetState(200, &prevState);

		// 再生中の字幕切替は Stop してから（ピン再接続が失敗しやすい）
		const BOOL needStop = (streamType == 2 && pMediaControl
			&& (prevState == State_Running || prevState == State_Paused));
		if (needStop) {
			// ピッチ DS が古い PCM を鳴らし続けて字幕と大ずれしないよう先に止める
			DougaPitchCorrect_PauseForGraphSeek();
			pMediaControl->Stop();
		}

		// 同一グループ内で排他選択（LAV 字幕切替で ENABLEONLY が効く）
		HRESULT hr = iam->Enable(actualIndex, AMSTREAMSELECTENABLE_ENABLEONLY);
		if (FAILED(hr))
			hr = iam->Enable(actualIndex, AMSTREAMSELECTENABLE_ENABLE);

		debug.Format(L"IAMStreamSelect::Enable(%d) = 0x%08X", actualIndex, hr);
		OutputDebugString(debug);

		// 字幕: ffdshow In Text へ配線し直す
		if (streamType == 2 && pGraphBuilder) {
			ConnectSubtitleWithDirectVobSub(pGraphBuilder);
			if (Vdc) {
				HWND hw = m_videoSite.GetSafeHwnd() ? m_videoSite.m_hWnd : m_hWnd;
				Vdc->SetVideoWindow(hw);
			}
			ApplyVideoDest();
			// 現在位置へシークして字幕サンプルを流し直す（ピッチ側も seek() 同様にリセット）
			if (pMediaSeeking) {
				REFERENCE_TIME pos = 0;
				if (SUCCEEDED(pMediaSeeking->GetCurrentPosition(&pos)))
					pMediaSeeking->SetPositions(&pos, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
			}
			DougaPitchCorrect_OnSeek();
		}

		if (needStop && pMediaControl) {
			if (prevState == State_Running)
				pMediaControl->Run();
			else if (prevState == State_Paused) {
				pMediaControl->Pause();
				DougaPitchCorrect_SetPaused(TRUE);
			}
		}

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
	RECT r;
	r.bottom=rcm.bottom;
	r.top=rcm.top;
	r.right=rcm.right;
	r.left=rcm.left;
	y=r.bottom-r.top; x=r.right-r.left;
	y1_=rcm.bottom-rcm.top; x1=rcm.right-rcm.left;
	si=0;
	const int chromeH = GetSystemMetrics(SM_CYSIZEFRAME) + ::GetSystemMetrics(SM_CYCAPTION) + GetBarHeight();
	GetWindowRect(&r);
	int wx = r.left, wy = r.top, ww = x, wh = y + chromeH;
	DougaFitOuterToWorkArea(m_hWnd, wx, wy, ww, wh, chromeH);
	{
		UINT flags = SWP_NOOWNERZORDER | SWP_NOACTIVATE;
		if (!::IsWindowVisible(m_hWnd))
			flags |= SWP_NOREDRAW;
		SetWindowPos(NULL, wx, wy, ww, wh, flags);
	}
	GetWindowRect(&r);
	ApplyVideoDest();
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
	RECT r;
	r.bottom=rcm.bottom;
	r.top=rcm.top;
	r.right=rcm.right;
	r.left=rcm.left;
	y=r.bottom-r.top; x=r.right-r.left;
	si=0;
	const int chromeH = GetSystemMetrics(SM_CYSIZEFRAME) + ::GetSystemMetrics(SM_CYCAPTION) + GetBarHeight();
	GetWindowRect(&r);
	int wx = r.left, wy = r.top, ww = x * 2, wh = y * 2 + chromeH;
	DougaFitOuterToWorkArea(m_hWnd, wx, wy, ww, wh, chromeH);
	{
		UINT flags = SWP_NOOWNERZORDER | SWP_NOACTIVATE;
		if (!::IsWindowVisible(m_hWnd))
			flags |= SWP_NOREDRAW;
		SetWindowPos(NULL, wx, wy, ww, wh, flags);
	}
	GetWindowRect(&r);
	ApplyVideoDest();
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
	RECT r;
	r.bottom=rcm.bottom;
	r.top=rcm.top;
	r.right=rcm.right;
	r.left=rcm.left;
	y=r.bottom-r.top; x=r.right-r.left;
	si=0;
	const int chromeH = GetSystemMetrics(SM_CYSIZEFRAME) + ::GetSystemMetrics(SM_CYCAPTION) + GetBarHeight();
	GetWindowRect(&r);
	int wx = r.left, wy = r.top;
	int ww = (int)((double)x * 1.5);
	int wh = (int)((double)y * 1.5) + chromeH;
	DougaFitOuterToWorkArea(m_hWnd, wx, wy, ww, wh, chromeH);
	{
		UINT flags = SWP_NOOWNERZORDER | SWP_NOACTIVATE;
		if (!::IsWindowVisible(m_hWnd))
			flags |= SWP_NOREDRAW;
		SetWindowPos(NULL, wx, wy, ww, wh, flags);
	}
	GetWindowRect(&r);
	ApplyVideoDest();
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
	if (pBasicVideo == NULL && !(ev && Vdc)) return;
	RECT r;
	double i;
	r.bottom=rcm.bottom;
	r.top=rcm.top;
	r.right=rcm.right;
	r.left=rcm.left;
	if (rcm.right <= 0) return;
	i=(double)(savedata.p.right-savedata.p.left)/(double)rcm.right;
	const int chromeH = GetSystemMetrics(SM_CYSIZEFRAME) + ::GetSystemMetrics(SM_CYCAPTION) + GetBarHeight();
	int wx = savedata.p.left;
	int wy = savedata.p.top;
	int ww = (int)(i * (double)(rcm.right - rcm.left));
	int wh = (int)(i * (double)(rcm.bottom - rcm.top) + chromeH);
	if (ww < 1) ww = 1;
	if (wh < 1) wh = 1;
	DougaFitOuterToWorkArea(m_hWnd, wx, wy, ww, wh, chromeH);
	{
		UINT flags = SWP_NOOWNERZORDER | SWP_NOACTIVATE;
		if (!::IsWindowVisible(m_hWnd))
			flags |= SWP_NOREDRAW;
		SetWindowPos(NULL, wx, wy, ww, wh, flags);
	}
	GetWindowRect(&r);
	savedata.p.top = r.top;
	savedata.p.left = r.left;
	savedata.p.bottom = r.bottom;
	savedata.p.right = r.right;
	ApplyVideoDest();
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
	if (ev && Vdc)
		Vdc->RepaintVideo();

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
		CPoint pt = point;
		ScreenToClient(&pt);
		if (m_bar.IsBarReady() && m_bar.PtInBarClient(pt))
			return HTCLIENT; // バーは操作対象(ドラッグにしない)
		UINT nHit = CFrameWnd::OnNcHitTest(point);
		return (nHit == HTCLIENT)? HTCAPTION : nHit;
	}else{
		// ヒットテストで毎回 SetWindowPos しない(ちらつき・カーソル不調の元)
		return HTBORDER ;
	}
//	return HTCAPTION;
}

void CDouga::OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI) 
{
	CFrameWnd::OnGetMinMaxInfo(lpMMI);
	// フルスクリーン以外は手動リサイズもタスクバー除外ワークエリアを上限にする
	if (savedata.fs || !GetSafeHwnd())
		return;
	HMONITOR mon = ::MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi = {};
	mi.cbSize = sizeof(mi);
	RECT rcWork;
	if (mon && ::GetMonitorInfo(mon, &mi))
		rcWork = mi.rcWork;
	else if (!::SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0))
		return;
	const int workW = rcWork.right - rcWork.left;
	const int workH = rcWork.bottom - rcWork.top;
	if (workW < 32 || workH < 32)
		return;
	lpMMI->ptMaxTrackSize.x = workW;
	lpMMI->ptMaxTrackSize.y = workH;
	lpMMI->ptMaxSize.x = workW;
	lpMMI->ptMaxSize.y = workH;
}

int CDouga::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message) 
{
	// OnNcHitTest が動画領域を HTCAPTION、バーを HTCLIENT に振り分け済み
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
	if (m_bar.IsBarReady() && m_bar.PtInBarClient(point)) {
		CFrameWnd::OnLButtonDown(nFlags, point);
		return;
	}
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
	DestroyHelpSheet();
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
	const BOOL videoMode = (mode == -2 || (mode > 0 && videoonly == TRUE));
	// 左右シークは常にメインの OnHotKey（二重スロット対応）。rl() は昇格後に音が動かない。
	if (og && (nChar == VK_RIGHT || nChar == VK_LEFT)) {
		const WPARAM hotId = (nChar == VK_RIGHT) ? (WPARAM)8002 : (WPARAM)8003;
		og->SendMessage(WM_HOTKEY, hotId, 0);
		CFrameWnd::OnKeyDown(nChar, nRepCnt, nFlags);
		return;
	}
	if (pMediaSeeking && videoMode) {
		if (nChar == VK_UP) {
			og->m_dsval.SetPos(og->m_dsval.GetPos() + 5);
		}
		if (nChar == VK_DOWN) {
			og->m_dsval.SetPos(og->m_dsval.GetPos() - 5);
		}
		if (nChar == 'C') {
			On32775();
		}
	}
	else {
		if (nChar == VK_UP) {
			og->m_dsval.SetPos(og->m_dsval.GetPos() + 5);
		}
		if (nChar == VK_DOWN) {
			og->m_dsval.SetPos(og->m_dsval.GetPos() - 5);
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

void CDouga::RestoreDougaCursor()
{
	KillTimer(3366);
	mousecnt = mousecnt1 = 0;
	for (;;) {
		int j = ShowCursor(TRUE);
		if (j >= 0) break;
	}
}

void CDouga::ToggleFullScreen()
{
	// クライアント/NC の二重配信で即トグルバックするのを防ぐ
	static DWORD s_lastToggle = 0;
	DWORD now = GetTickCount();
	if (now - s_lastToggle < 250)
		return;
	s_lastToggle = now;

	RECT rr = {};
	if (savedata.fs) {
		savedata.fs = 0;
		SetWindowLong(m_hWnd, GWL_EXSTYLE, WS_EX_OVERLAPPEDWINDOW | WS_EX_ACCEPTFILES);
		int i = GetWindowLong(m_hWnd, GWL_STYLE);
		SetWindowLong(m_hWnd, GWL_STYLE, ((i | WS_OVERLAPPEDWINDOW) & ~WS_MAXIMIZEBOX & ~WS_MINIMIZEBOX));
		OnMenuitem32774();
		RestoreDougaCursor();
		ApplyDougaTopmost(); // 拡張スタイル入れ替えで TOPMOST が落ちるため戻す
	} else {
		savedata.fs = 1;
		int cx = GetSystemMetrics(SM_CXSCREEN);
		int cy = GetSystemMetrics(SM_CYSCREEN);
		int i = GetWindowLong(m_hWnd, GWL_STYLE);
		SetWindowLong(m_hWnd, GWL_EXSTYLE, 0);
		SetWindowLong(m_hWnd, GWL_STYLE, i & ~WS_CAPTION & ~WS_BORDER & ~WS_THICKFRAME);
		SetWindowPos(NULL, 0, 0, cx, cy, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		ApplyDougaTopmost(); // 拡張スタイル入れ替えで TOPMOST が落ちるため戻す
		ApplyVideoDest();
		if (ev && Vdc) {
			MFVideoNormalizedRect mvnr = { 0, 0, 1, 1 };
			CRect fsr;
			if (m_videoSite.GetSafeHwnd()) m_videoSite.GetClientRect(&fsr);
			else GetClientRect(&fsr);
			DougaLetterbox(fsr);
			rr = fsr;
			Vdc->SetVideoPosition(&mvnr, &rr);
		} else if (pBasicVideo && pVideoWindow) {
			double ii; int cyy, cxx;
			if (rcm.bottom < rcm.right) {
				ii = (double)(cx) / (double)rcm.right;
				cyy = cy / 2 - (int)(((double)rcm.bottom * ii) / 2);
				cxx = cx;
				rr.top = cyy; rr.bottom = cyy + (int)((double)rcm.bottom * ii); rr.left = 0; rr.right = cxx;
			} else {
				ii = (double)(cy) / (double)rcm.bottom;
				cxx = cx / 2 - (int)(((double)rcm.right * ii) / 2);
				cyy = cy;
				rr.top = 0; rr.bottom = cyy; rr.left = cxx; rr.right = cxx + (int)((double)rcm.right * ii);
			}
			if (savedata.render == 0) {
				pBasicVideo->put_DestinationWidth(rr.right - rr.left);
				pBasicVideo->put_DestinationHeight(rr.bottom - rr.top);
				pVideoWindow->put_Top(rr.top);
				pVideoWindow->put_Left(rr.left);
				pVideoWindow->put_Height(rr.bottom);
				pVideoWindow->put_Width(rr.right);
			} else {
				CRect vs;
				if (m_videoSite.GetSafeHwnd()) m_videoSite.GetClientRect(&vs);
				else GetClientRect(&vs);
				rr = vs;
				pBasicVideo->put_DestinationWidth(rr.right - rr.left);
				pBasicVideo->put_DestinationHeight(rr.bottom - rr.top);
				pVideoWindow->put_Top(rr.top);
				pVideoWindow->put_Left(rr.left);
				pVideoWindow->put_Height(rr.bottom);
				pVideoWindow->put_Width(rr.right);
			}
		}
		mousecnt = mousecnt1 = 0;
		SetTimer(3366, 500, NULL);
	}
	GetClientRect(&rr);
	InvalidateRect(&rr, FALSE);
	dd2 = 1;
}

void CDouga::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	// 動画サイトから転送されるクライアントDblClkでも FS を切替(EVR含む)
	ToggleFullScreen();
	CFrameWnd::OnLButtonDblClk(nFlags, point);
}

void CDouga::OnNcLButtonDblClk(UINT nHitTest, CPoint point)
{
	ToggleFullScreen();
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

void CDouga::OnDougaMenuPlay() { m_bar.OnBnPlay(); }
void CDouga::OnDougaMenuStop() { m_bar.OnBnStop(); }
void CDouga::OnDougaMenuPrev() { m_bar.OnBnPrev(); }
void CDouga::OnDougaMenuNext() { m_bar.OnBnNext(); }
void CDouga::OnDougaMenuRew() { m_bar.OnBnRew(); }
void CDouga::OnDougaMenuFf() { m_bar.OnBnFf(); }
void CDouga::OnDougaMenuMute() { m_bar.OnBnMute(); }
void CDouga::OnDougaMenuFs() { m_bar.OnBnFs(); }
void CDouga::OnDougaMenuFade() { m_bar.OnBnFade(); }
void CDouga::OnDougaMenuClose()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_OGG_CLOSE_DOUGA, 0, 0);
}

void CDouga::OnDougaMenuSubOff()
{
	if (!iam) return;

	OAFilterState prevState = State_Stopped;
	if (pMediaControl)
		pMediaControl->GetState(200, &prevState);
	const BOOL needStop = (pMediaControl
		&& (prevState == State_Running || prevState == State_Paused));
	if (needStop) {
		DougaPitchCorrect_PauseForGraphSeek();
		pMediaControl->Stop();
	}

	// LAV の「No subtitles」ストリームがあればそれを有効化（本来のオフ）
	BOOL ok = FALSE;
	if (streamidxSubOff >= 0) {
		HRESULT hr = iam->Enable(streamidxSubOff, AMSTREAMSELECTENABLE_ENABLEONLY);
		if (FAILED(hr))
			hr = iam->Enable(streamidxSubOff, AMSTREAMSELECTENABLE_ENABLE);
		ok = SUCCEEDED(hr);
	}
	if (!ok) {
		for (int i = 0; i < streamMap.subtitleCount && i < 40; ++i) {
			const int idx = streamidx2[i];
			if (idx >= 0)
				iam->Enable(idx, 0);
		}
	}

	if (pMediaSeeking) {
		REFERENCE_TIME pos = 0;
		if (SUCCEEDED(pMediaSeeking->GetCurrentPosition(&pos)))
			pMediaSeeking->SetPositions(&pos, AM_SEEKING_AbsolutePositioning, NULL, AM_SEEKING_NoPositioning);
	}
	DougaPitchCorrect_OnSeek();

	if (needStop && pMediaControl) {
		if (prevState == State_Running)
			pMediaControl->Run();
		else if (prevState == State_Paused) {
			pMediaControl->Pause();
			DougaPitchCorrect_SetPaused(TRUE);
		}
	}
}

void CDouga::OnDougaMenuDsFilters()
{
	// CRender::OnBnClickedCancel2 と同じ CGraph（再生中グラフのフィルタ一覧）
	if (!pGraphBuilder) return;
	CGraph* dlg = new CGraph(this);
	::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	dlg->DoModal();
	delete dlg;
}

void CDouga::ShowHelpSheet()
{
	if (g_dougaHelpDlg && ::IsWindow(g_dougaHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_dougaHelpDlg, this);
		return;
	}
	if (g_dougaHelpDlg && !::IsWindow(g_dougaHelpDlg->GetSafeHwnd()))
		g_dougaHelpDlg = nullptr;
	CDougaHelpDlg* dlg = new CDougaHelpDlg(this);
	if (!dlg->Create(IDD_DOUGA_HELP, this)) {
		delete dlg;
		return;
	}
	g_dougaHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CDouga::DestroyHelpSheet()
{
	if (g_dougaHelpDlg && ::IsWindow(g_dougaHelpDlg->GetSafeHwnd()))
		g_dougaHelpDlg->DestroyWindow();
}

void CDouga::OnHelpShowSheet()
{
	ShowHelpSheet();
}

void CDouga::OnDougaMenuTopmost()
{
	savedata.dougatopmost = savedata.dougatopmost ? 0 : 1;
	ApplyDougaTopmost();
}

void CDouga::OnDougaMenuAspect()
{
	savedata.dougaaspect = savedata.dougaaspect ? 0 : 1;
	ApplyVideoDest();
}

void CDouga::OnDougaMenuSpeed(UINT nID)
{
	static const double kRates[8] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0 };
	const int i = (int)nID - ID_DOUGA_SPEED_FIRST;
	if (i < 0 || i >= 8) return;
	DougaSetPlaybackRate(kRates[i], TRUE);
}

// 常に手前の適用。ウィンドウ生成時とメニュー切替の両方から呼ぶ。
void CDouga::ApplyDougaTopmost()
{
	if (!GetSafeHwnd()) return;
	SetWindowPos(savedata.dougatopmost ? &wndTopMost : &wndNoTopMost, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}
