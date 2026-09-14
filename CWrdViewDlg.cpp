#include "stdafx.h"
#include "CWrdViewDlg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "resource.h"
#include "ComposerConvert.h"
#include "VstMidiEngine.h"
#include "CCustomControl.h"
#include "CCustomPopupMenu.h"

class COggDlg;
extern COggDlg* og;
extern save savedata;
extern CString filen;
extern int mode;
extern __int64 playb;
extern int playy;
extern int wavbit_sample_Hz;
void MmBindVstActiveSlot();

namespace {
enum {
	IDM_WRD_TOPMOST = 42610,
	IDM_WRD_RELOAD = 42611,
	IDM_WRD_MIDIMON = 42612,
	IDM_WRD_SCALE1 = 42613,
	IDM_WRD_SCALE2 = 42614,
	IDM_WRD_FIT = 42615
};

int WrdWantSampleRate()
{
	/* playb / heard はソース PCM。VST の内部レートと混ぜない */
	if (wavbit_sample_Hz >= 8000)
		return wavbit_sample_Hz;
	if (mode == MODE_VST_MIDI) {
		const int r = VstMidiGetRate();
		if (r > 0) return r;
	}
	if (savedata.samples >= 8000)
		return savedata.samples;
	return 44100;
}

__int64 WrdPlaybackSamples(int sr)
{
	/* バナー 0:56.52 と同じソース位置。heard は VST で 14秒相当に落ちて
	   @WAIT(8) の「答えはつかめるよ」で止まる */
	__int64 ui = OggGetUiSourcePcmFrames();
	if (ui > 0)
		return ui;
	const double sec = OggGetGdiPlaybackTimeSec();
	if (sec > 0.0) {
		int useSr = (sr >= 8000) ? sr : WrdWantSampleRate();
		if (useSr < 8000) useSr = 44100;
		ui = (__int64)(sec * (double)useSr + 0.5);
		if (ui > 0)
			return ui;
	}
	__int64 pb = playb;
	if (playy == 0 && pb < 0) pb = 0;
	if (pb < 0) pb = 0;
	return pb;
}

void WrdResolveMidiPath(wchar_t* mid, int midChars)
{
	if (!mid || midChars < 2) return;
	mid[0] = 0;
	if (filen.IsEmpty()) return;
	VstResolvePlayPath(filen, mid, midChars, NULL, 0, NULL);
	if (!mid[0])
		wcsncpy_s(mid, midChars, filen, _TRUNCATE);
}

int WrdLoadSmfClock(WrdEngine* e, int sr)
{
	if (!e) return 0;
	wchar_t mid[520];
	WrdResolveMidiPath(mid, 520);
	if (mid[0] && GetFileAttributesW(mid) != INVALID_FILE_ATTRIBUTES
		&& WrdEngineLoadSmfClock(e, mid, sr))
		return 1;
	if (!e->wrdPath[0]) return 0;
	wchar_t sib[520];
	wcsncpy_s(sib, e->wrdPath, _TRUNCATE);
	wchar_t* dot = wcsrchr(sib, L'.');
	if (!dot) return 0;
	const size_t rest = 520 - (size_t)(dot - sib);
	static const wchar_t* smfExt[] = { L".MID", L".mid", L".RMI", L".rmi" };
	for (int i = 0; i < 4; ++i) {
		wcscpy_s(dot, rest, smfExt[i]);
		if (GetFileAttributesW(sib) != INVALID_FILE_ATTRIBUTES
			&& WrdEngineLoadSmfClock(e, sib, sr))
			return 1;
	}
	static const wchar_t* seqExt[] = { L".RCP", L".rcp", L".R36", L".r36", L".G36", L".g36" };
	for (int i = 0; i < 6; ++i) {
		wcscpy_s(dot, rest, seqExt[i]);
		if (GetFileAttributesW(sib) == INVALID_FILE_ATTRIBUTES)
			continue;
		wchar_t conv[520];
		if (ComposerConvertToMidi(sib, conv, 520)
			&& WrdEngineLoadSmfClock(e, conv, sr))
			return 1;
	}
	return 0;
}
}

IMPLEMENT_DYNAMIC(CWrdViewDlg, CCustomBlurDialogExBase)

CWrdViewDlg::CWrdViewDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_WRDVIEW, pParent)
	, m_paintDisabled(false), m_alwaysOnTop(false)
	, m_frameOld(nullptr), m_frameW(0), m_frameH(0)
	, m_lastTickDrawn(-1)
{
	m_wrdPath[0] = 0;
	m_srcPath[0] = 0;
	WrdEngineInit(&m_eng);
}

CWrdViewDlg::~CWrdViewDlg()
{
	WrdEngineFree(&m_eng);
	ReleasePaintBuffers();
}

void CWrdViewDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_WRD_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CWrdViewDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_WM_SYSCOMMAND()
	ON_BN_CLICKED(IDC_WRD_HELP, &CWrdViewDlg::OnBnClickedHelp)
	ON_COMMAND(ID_HELP_SHOWSHEET, &CWrdViewDlg::OnBnClickedHelp)
END_MESSAGE_MAP()

UINT CWrdViewDlg::WindowDpi() const
{
	HWND h = GetSafeHwnd();
	if (!h) return 96;
	typedef UINT(WINAPI* PFN)(HWND);
	static PFN s_fn = NULL;
	static BOOL s_got = FALSE;
	if (!s_got) {
		HMODULE u = GetModuleHandleW(L"user32.dll");
		s_fn = u ? (PFN)GetProcAddress(u, "GetDpiForWindow") : NULL;
		s_got = TRUE;
	}
	if (s_fn) {
		UINT d = s_fn(h);
		if (d) return d;
	}
	return 96;
}

void CWrdViewDlg::ReleasePaintBuffers()
{
	if (m_frameOld && m_frameDC.GetSafeHdc()) {
		m_frameDC.SelectObject(m_frameOld);
		m_frameOld = nullptr;
	}
	if (m_frameBmp.GetSafeHandle())
		m_frameBmp.DeleteObject();
	if (m_frameDC.GetSafeHdc())
		m_frameDC.DeleteDC();
	m_frameW = m_frameH = 0;
}

bool CWrdViewDlg::EnsureFrameBuffer(CDC& refDC, int w, int h)
{
	if (w < 8 || h < 8) return false;
	if (m_frameDC.GetSafeHdc() && m_frameW == w && m_frameH == h)
		return true;
	ReleasePaintBuffers();
	if (!m_frameDC.CreateCompatibleDC(&refDC)) return false;
	if (!m_frameBmp.CreateCompatibleBitmap(&refDC, w, h)) {
		m_frameDC.DeleteDC();
		return false;
	}
	m_frameOld = m_frameDC.SelectObject(&m_frameBmp);
	m_frameW = w;
	m_frameH = h;
	return true;
}

void CWrdViewDlg::LayoutHelpBtn()
{
	if (!m_help.GetSafeHwnd()) return;
	CRect rc;
	GetClientRect(&rc);
	const UINT dpi = WindowDpi();
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int bw = Scale(24, dpi), bh = Scale(16, dpi);
	m_help.SetWindowPos(NULL, rc.right - Scale(32, dpi), capH + Scale(2, dpi), bw, bh,
		SWP_NOZORDER | SWP_NOACTIVATE);
}

void CWrdViewDlg::PersistPos()
{
	if (!::IsWindow(m_hWnd) || IsIconic()) return;
	CRect rc;
	GetWindowRect(&rc);
	savedata.wrdx = rc.left;
	savedata.wrdy = rc.top;
	savedata.wrdw = rc.Width();
	savedata.wrdh = rc.Height();
}

void CWrdViewDlg::DetachForDestroy()
{
	m_paintDisabled = true;
	KillTimer(1);
	PersistPos();
}

BOOL CWrdViewDlg::OnInitDialog()
{
	m_paintDisabled = false;
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(
		L"WRD画面", L"WRD screen", L"Ecran WRD", L"Schermo WRD", L"Pantalla WRD",
		L"WRD 화면", L"WRD画面", L"شاشة WRD", L"Экран WRD", L"WRD-Bildschirm",
		L"Tela WRD", L"WRD-scherm", L"Ekran WRD", L"WRD ekrani"));
	ModifyStyle(WS_MINIMIZEBOX, 0);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);

	m_alwaysOnTop = (savedata.wrdtopmost != 0);
	const UINT dpi = WindowDpi();
	int dw = Scale(680, dpi), dh = Scale(460, dpi);
	if (!ScRestoreWndGeom(this, savedata.wrdx, savedata.wrdy, savedata.wrdw, savedata.wrdh, 220, 180))
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndTop,
			120, 80, dw, dh,
			SWP_NOOWNERZORDER | (m_alwaysOnTop ? 0 : SWP_NOZORDER));
	else if (m_alwaysOnTop)
		SetWindowPos(&CWnd::wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER);

	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();
	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		if (m_help.GetSafeHwnd())
			m_tooltip.AddTool(&m_help, LL14(
				L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida",
				L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل",
				L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen",
				L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
		m_tooltip.Activate(TRUE);
	}
	EnableMainWindowLock(&savedata.wrdMainLock, TRUE);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	SetTimer(1, 16, nullptr);
	ReloadCurrent();
	return TRUE;
}

int CWrdViewDlg::LoadWrdPath(const wchar_t* wrdPath)
{
	if (!wrdPath || !wrdPath[0]) return 0;
	wcsncpy_s(m_wrdPath, wrdPath, _TRUNCATE);
	if (!WrdEngineLoad(&m_eng, wrdPath))
		return 0;
	const int sr = WrdWantSampleRate();
	WrdLoadSmfClock(&m_eng, sr);
	m_eng.sr = sr;
	m_lastTickDrawn = -1;
	if (::IsWindow(m_hWnd))
		Invalidate(FALSE);
	return 1;
}

void CWrdViewDlg::ReloadCurrent()
{
	wchar_t wrd[MAX_PATH];
	wrd[0] = 0;
	const wchar_t* src = (LPCWSTR)filen;
	if (src && src[0])
		ComposerFindSidecarWrd(src, wrd, MAX_PATH);
	if (!wrd[0] && m_wrdPath[0])
		wcsncpy_s(wrd, m_wrdPath, _TRUNCATE);
	if (wrd[0])
		LoadWrdPath(wrd);
}

void CWrdViewDlg::SyncFromPlayback()
{
	if (!m_eng.loaded) {
		ReloadCurrent();
		if (!m_eng.loaded) return;
	}
	if (!filen.IsEmpty() && m_eng.wrdPath[0]) {
		wchar_t wrd[MAX_PATH];
		if (ComposerFindSidecarWrd(filen, wrd, MAX_PATH)
			&& _wcsicmp(wrd, m_eng.wrdPath) != 0)
			LoadWrdPath(wrd);
	}
	int sr = WrdWantSampleRate();
	if (!m_eng.smfOk || m_eng.sr != sr)
		WrdLoadSmfClock(&m_eng, sr);
	m_eng.sr = sr;
	const __int64 pb = WrdPlaybackSamples(sr);
	/* WRD の tick は SMF テンポマップ（4分=48）だけを使う。
	   VST の PPQN やバッファ補正を混ぜると KIDA のように数秒ずれる。 */
	const int tick = WrdEngineTickFromSample(&m_eng, pb);
	WrdEngineSeek(&m_eng, tick);
}

void CWrdViewDlg::PumpSyncNow()
{
	if (m_paintDisabled || !::IsWindow(m_hWnd)) return;
	SyncFromPlayback();
	if (!IsWindowVisible() || IsIconic()) return;
	Invalidate(FALSE);
}

void CWrdViewDlg::IdlePulse()
{
	PumpSyncNow();
}

BOOL CWrdViewDlg::OnEraseBkgnd(CDC*)
{
	return TRUE;
}

void CWrdViewDlg::OnPaint()
{
	CPaintDC dc(this);
	if (m_paintDisabled) return;
	CRect rect;
	GetClientRect(&rect);
	const int w = rect.Width();
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int h = rect.Height() - capH;
	if (w <= 0 || h <= 0) {
		CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}
	if (!EnsureFrameBuffer(dc, w, rect.Height())) {
		CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}
	m_frameDC.FillSolidRect(0, capH, w, h, RGB(0, 0, 16));
	RECT inner = { 0, 0, w, rect.Height() };
	WrdEnginePaint(&m_eng, m_frameDC.GetSafeHdc(), &inner, capH);
#if CCUSTOM_AERO_SUPPORT
	/* GDI FillSolidRect / StretchDIBits は α=0 のまま残る。Win11 アクリルでは
	   DWMWA_REDIRECTIONBITMAP_ALPHA がそれを完全透過として扱う。 */
	if (CCC_IsWin11() && (CCC_IsAeroEnabled() || CCC_AcrylicCaption(m_hWnd))) {
		CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, capH, w, h,
			m_frameDC.GetSafeHdc(), 0, capH, w, h);
	} else
#endif
	{
		dc.BitBlt(0, capH, w, h, &m_frameDC, 0, capH, SRCCOPY);
	}
	CCC_CaptionPaintGdi(dc, m_hWnd);
}

void CWrdViewDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) {
		PersistPos();
		SyncFromPlayback();
		if (IsWindowVisible() && !IsIconic())
			Invalidate(FALSE);
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CWrdViewDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
		PersistPos();
		Invalidate(FALSE);
	}
}

void CWrdViewDlg::OnMove(int x, int y)
{
	CCustomBlurDialogExBase::OnMove(x, y);
	PersistPos();
}

void CWrdViewDlg::OnClose()
{
	savedata.wrdwindow = 0;
	DetachForDestroy();
	DestroyWindow();
}

void CWrdViewDlg::OnDestroy()
{
	PersistPos();
	CCustomBlurDialogExBase::OnDestroy();
}

void CWrdViewDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == SC_CLOSE) {
		OnClose();
		return;
	}
	CCustomBlurDialogExBase::OnSysCommand(nID, lParam);
}

void CWrdViewDlg::OnBnClickedHelp()
{
	AfxMessageBox(LL14(
		L"WRDはPC-98 MIMPI/TMIDIの歌詞・画面スクリプトです。MIDI演奏位置（小節/@WAIT）に同期して文字とMAG画像を出します。右クリックでMIDIモニタを開けます。",
		L"WRD is a PC-98 MIMPI/TMIDI lyric/screen script. Lyrics and MAG images follow MIDI playback (@WAIT/measures). Right-click to open the MIDI monitor.",
		L"WRD est un script paroles MIMPI/TMIDI PC-98. Les paroles suivent la lecture MIDI.",
		L"WRD e uno script testi MIMPI/TMIDI PC-98. I testi seguono la riproduzione MIDI.",
		L"WRD es un script de letra MIMPI/TMIDI de PC-98. La letra sigue la reproduccion MIDI.",
		L"WRD는 PC-98 MIMPI/TMIDI 가사 스크립트입니다. MIDI 재생에 맞춰 표시됩니다.",
		L"WRD 是 PC-98 MIMPI/TMIDI 歌词脚本，随 MIDI 播放同步。",
		L"WRD هو نص كلمات MIMPI/TMIDI لجهاز PC-98 ويتزامن مع MIDI.",
		L"WRD — скрипт текстов MIMPI/TMIDI для PC-98, синхронизирован с MIDI.",
		L"WRD ist ein PC-98 MIMPI/TMIDI-Textskript und folgt der MIDI-Wiedergabe.",
		L"WRD e um script de letra MIMPI/TMIDI do PC-98, sincronizado com MIDI.",
		L"WRD is een PC-98 MIMPI/TMIDI-lyricsscript, gesynchroniseerd met MIDI.",
		L"WRD to skrypt tekstow MIMPI/TMIDI PC-98, zsynchronizowany z MIDI.",
		L"WRD, MIDI calma ile eszamanli PC-98 MIMPI/TMIDI soz betigidir."),
		MB_OK | MB_ICONINFORMATION);
}

void CWrdViewDlg::OnContextMenu(CWnd*, CPoint point)
{
	CCustomPopupMenu menu;
	menu.AddCheck(IDM_WRD_TOPMOST,
		LL14(L"常に手前", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano",
			L"Siempre visible", L"항상 위", L"置顶", L"دائماً أعلى", L"Поверх всех", L"Immer im Vordergrund",
			L"Sempre no topo", L"Altijd boven", L"Zawsze na wierzchu", L"Her zaman ustte"),
		m_alwaysOnTop ? TRUE : FALSE);
	menu.AddCommand(IDM_WRD_RELOAD,
		LL14(L"再読み込み", L"Reload", L"Recharger", L"Ricarica", L"Recargar", L"다시 읽기", L"重新加载",
			L"إعادة التحميل", L"Перезагрузить", L"Neu laden", L"Recarregar", L"Herladen", L"Wczytaj ponownie", L"Yeniden yukle"));
	menu.AddCommand(IDM_WRD_MIDIMON,
		LL14(L"MIDIモニタを開く", L"Open MIDI monitor", L"Ouvrir le moniteur MIDI", L"Apri monitor MIDI",
			L"Abrir monitor MIDI", L"MIDI 모니터 열기", L"打开MIDI监视器", L"فتح مراقب MIDI",
			L"Открыть MIDI-монитор", L"MIDI-Monitor oeffnen", L"Abrir monitor MIDI", L"MIDI-monitor openen",
			L"Otworz monitor MIDI", L"MIDI izleyiciyi ac"));
	const int cmd = (int)menu.Track(point, this);
	if (cmd == IDM_WRD_TOPMOST) {
		m_alwaysOnTop = !m_alwaysOnTop;
		savedata.wrdtopmost = m_alwaysOnTop ? 1 : 0;
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndNoTopMost,
			0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	} else if (cmd == IDM_WRD_RELOAD) {
		ReloadCurrent();
	} else if (cmd == IDM_WRD_MIDIMON) {
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->EnsureMidiMonitor();
	}
}

BOOL CWrdViewDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
