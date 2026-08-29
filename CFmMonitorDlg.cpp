#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CFmMonitorDlg.h"
#include "resource.h"
#include "DatArchive.h"
#include <algorithm>
#include <math.h>

extern save savedata;

namespace {

#pragma pack(push, 1)
struct FmMonGeomFile {
	char magic[4]; // "FMMG"
	int version;   // 1
	int open;      // 0/1
	int x, y, w, h;
};
#pragma pack(pop)

/* DatArc ステージ上の fmmon_geom.dat（アーカイブに同梱・起動時も残る） */
static void FmMonGeomPath(wchar_t* out, int n)
{
	LPCTSTR stage = DatArc_StageDir();
	if (stage && stage[0]) {
		_snwprintf_s(out, n, _TRUNCATE, L"%sfmmon_geom.dat", stage);
		return;
	}
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	_snwprintf_s(out, n, _TRUNCATE, L"%sogg_kbsasami\\fmmon_geom.dat", tmp);
}

static void FmMonGeomSave(int open, int x, int y, int w, int h)
{
	FmMonGeomFile g = {};
	g.magic[0] = 'F'; g.magic[1] = 'M'; g.magic[2] = 'M'; g.magic[3] = 'G';
	g.version = 1;
	g.open = open ? 1 : 0;
	g.x = x; g.y = y; g.w = w; g.h = h;
	wchar_t path[MAX_PATH];
	FmMonGeomPath(path, MAX_PATH);
	/* 親ディレクトリ保証（temp fallback 時） */
	{
		wchar_t dir[MAX_PATH];
		wcsncpy_s(dir, path, _TRUNCATE);
		wchar_t* sl = wcsrchr(dir, L'\\');
		if (sl) {
			*sl = 0;
			CreateDirectoryW(dir, NULL);
		}
	}
	HANDLE hf = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hf == INVALID_HANDLE_VALUE) return;
	DWORD wr = 0;
	WriteFile(hf, &g, sizeof(g), &wr, NULL);
	FlushFileBuffers(hf);
	CloseHandle(hf);
	DatArc_InvalidateLeaf(L"fmmon_geom.dat");
	DatArc_Commit(L"fmmon_geom.dat");
}

static int FmMonGeomLoad(FmMonGeomFile* out)
{
	/* アーカイブから展開済みのステージを優先 */
	CString staged = DatArc_Path(L"fmmon_geom.dat");
	wchar_t path[MAX_PATH];
	if (!staged.IsEmpty())
		wcsncpy_s(path, staged, _TRUNCATE);
	else
		FmMonGeomPath(path, MAX_PATH);
	HANDLE hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hf == INVALID_HANDLE_VALUE) {
		/* 旧 exe隣 .bin 互換 */
		wchar_t mod[MAX_PATH];
		GetModuleFileNameW(NULL, mod, MAX_PATH);
		wchar_t* slash = wcsrchr(mod, L'\\');
		if (slash) slash[1] = 0;
		else mod[0] = 0;
		_snwprintf_s(path, _TRUNCATE, L"%sfmmon_geom.bin", mod);
		hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (hf == INVALID_HANDLE_VALUE) return 0;
	}
	DWORD rd = 0;
	FmMonGeomFile g = {};
	BOOL ok = ReadFile(hf, &g, sizeof(g), &rd, NULL);
	CloseHandle(hf);
	if (!ok || rd != sizeof(g)) return 0;
	if (g.magic[0] != 'F' || g.magic[1] != 'M' || g.magic[2] != 'M' || g.magic[3] != 'G') return 0;
	if (g.version != 1) return 0;
	*out = g;
	return 1;
}

static UINT FmUiDpi(HWND hwnd)
{
	typedef UINT(WINAPI* PFN)(HWND);
	static PFN s_fn = nullptr;
	static BOOL s_got = FALSE;
	if (!s_got) {
		HMODULE u = ::GetModuleHandleW(L"user32.dll");
		if (u) s_fn = (PFN)::GetProcAddress(u, "GetDpiForWindow");
		s_got = TRUE;
	}
	if (s_fn && hwnd) {
		const UINT d = s_fn(hwnd);
		if (d) return d;
	}
	return 96;
}

static int FmScale(int v, UINT dpi) { return MulDiv(v, (int)dpi, 96); }

static void FmBump(BYTE& g) { g = 255; }

static void FmTickGlow(BYTE& g)
{
	if (g == 0) return;
	g = (BYTE)((g * 7) / 8);
	if (g < 8) g = 0;
}

static COLORREF FmMixFade(COLORREF base, COLORREF hi, BYTE fade)
{
	if (fade == 0) return base;
	const int a = fade;
	const int r = (GetRValue(base) * (255 - a) + GetRValue(hi) * a) / 255;
	const int g = (GetGValue(base) * (255 - a) + GetGValue(hi) * a) / 255;
	const int b = (GetBValue(base) * (255 - a) + GetBValue(hi) * a) / 255;
	return RGB(r, g, b);
}

static void FmFillFade(CDC& dc, int x, int y, int w, int h, COLORREF base, COLORREF hi, BYTE fade)
{
	if (w <= 0 || h <= 0) return;
	dc.FillSolidRect(x, y, w, h, FmMixFade(base, hi, fade));
}

static void FmMonLivePath(wchar_t* out, int n)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	_snwprintf_s(out, n, _TRUNCATE, L"%sogg_kbsasami\\fmmon_live.opna", tmp);
}

static void FmMonRingPath(wchar_t* out, int n)
{
	wchar_t tmp[MAX_PATH];
	GetTempPathW(MAX_PATH, tmp);
	_snwprintf_s(out, n, _TRUNCATE, L"%sogg_kbsasami\\fmmon_ring.opna", tmp);
}

static int FmReadDump(SasamiFmMonDump* out)
{
	wchar_t path[MAX_PATH];
	FmMonLivePath(path, MAX_PATH);
	for (int attempt = 0; attempt < 4; attempt++) {
		HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (h == INVALID_HANDLE_VALUE) {
			Sleep(1);
			continue;
		}
		SasamiFmMonDump tmp;
		DWORD rd = 0;
		BOOL ok = ReadFile(h, &tmp, sizeof(tmp), &rd, NULL);
		CloseHandle(h);
		if (ok && rd == sizeof(tmp) && SasamiFmMonMagicOk(tmp)) {
			*out = tmp;
			return 1;
		}
		Sleep(1);
	}
	return 0;
}

/* リング全体(~56KB)を毎回読まず、未消費スロットだけ読む */
static int FmDrainRingSlots(uint32_t* genLast,
	void (*onSlot)(const SasamiFmMonDump&, void*), void* ctx)
{
	if (!genLast || !onSlot) return 0;
	wchar_t path[MAX_PATH];
	FmMonRingPath(path, MAX_PATH);
	HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return 0;

	SasamiFmMonRing hdr;
	memset(&hdr, 0, sizeof(hdr));
	DWORD rd = 0;
	const DWORD hdrNeed = (DWORD)offsetof(SasamiFmMonRing, slot);
	if (!ReadFile(h, &hdr, hdrNeed, &rd, NULL) || rd != hdrNeed
		|| !SasamiFmMonRingMagicOk(hdr)) {
		CloseHandle(h);
		return 0;
	}
	uint32_t from = *genLast;
	const uint32_t to = hdr.gen;
	if (to < from)
		from = (to > SASAMI_FMMON_RING) ? (to - SASAMI_FMMON_RING) : 0;
	else if (to - from > SASAMI_FMMON_RING)
		from = to - SASAMI_FMMON_RING;
	int any = 0;
	for (uint32_t g = from; g < to; g++) {
		const uint32_t idx = g % SASAMI_FMMON_RING;
		LARGE_INTEGER off;
		off.QuadPart = (LONGLONG)offsetof(SasamiFmMonRing, slot)
			+ (LONGLONG)idx * (LONGLONG)sizeof(SasamiFmMonDump);
		if (!SetFilePointerEx(h, off, NULL, FILE_BEGIN))
			continue;
		SasamiFmMonDump d;
		rd = 0;
		if (!ReadFile(h, &d, sizeof(d), &rd, NULL) || rd != sizeof(d))
			continue;
		if (!SasamiFmMonMagicOk(d)) continue;
		onSlot(d, ctx);
		any = 1;
	}
	CloseHandle(h);
	*genLast = to;
	(void)any;
	return 1; /* ファイルあり（新規スロット無しでも OK） */
}

static const wchar_t* kRzmName[6] = { L"BD", L"SD", L"TOP", L"HH", L"TOM", L"RIM" };

static constexpr COLORREF FM_BG = RGB(28, 32, 40);
/* クロマキーは使わない（白抜け防止）。描画は常に不透明 Stretch/BitBlt */

} // namespace

static void FmReleaseFontCache();

IMPLEMENT_DYNAMIC(CFmMonitorDlg, CCustomBlurDialogExBase)

CFmMonitorDlg::CFmMonitorDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_FMMONITOR, pParent)
	, m_histN(0), m_histHead(0)
	, m_lastSeq(0), m_lastCurSample(0), m_lastHeardSamp(0), m_heardAnchor(0)
	, m_heardQpc(0), m_heardFreq(0), m_ringGenLast(0), m_haveDump(0)
	, m_dirtyHead(1), m_dirtyHex(1), m_dirtyPanels(1), m_dirtyKeys(1), m_fullDraw(1)
	, m_panelDirtyMask(0x3F)
	, m_readFail(0), m_persistAge(-1), m_userClosing(0), m_lastPollMs(0)
	, m_lastPlayy(-1)
	, m_layOk(0)
	, m_frameOld(nullptr), m_frameW(0), m_frameH(0)
#if CCUSTOM_AERO_SUPPORT
	, m_chromaW(0), m_chromaH(0), m_chromaReady(false)
#endif
{
	memset(&m_dump, 0, sizeof(m_dump));
	memset(&m_prev, 0, sizeof(m_prev));
	memset(m_hist, 0, sizeof(m_hist));
	memset(m_histSamp, 0, sizeof(m_histSamp));
	memset(m_fade, 0, sizeof(m_fade));
	LARGE_INTEGER f;
	if (QueryPerformanceFrequency(&f) && f.QuadPart > 0)
		m_heardFreq = f.QuadPart;
	else
		m_heardFreq = 1;
	memset(m_touched, 0, sizeof(m_touched));
	memset(m_fadeKey, 0, sizeof(m_fadeKey));
	memset(m_fadeSsg, 0, sizeof(m_fadeSsg));
	memset(m_fadePcm, 0, sizeof(m_fadePcm));
	memset(m_fadeRzmPad, 0, sizeof(m_fadeRzmPad));
	memset(&m_lay, 0, sizeof(m_lay));
	m_lastSong[0] = 0;
}

CFmMonitorDlg::~CFmMonitorDlg()
{
	ReleasePaintBuffers();
	FmReleaseFontCache();
}

void CFmMonitorDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FM_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CFmMonitorDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
	ON_WM_SYSCOMMAND()
	ON_BN_CLICKED(IDC_FM_HELP, &CFmMonitorDlg::OnBnClickedHelp)
END_MESSAGE_MAP()

BOOL CFmMonitorDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(L"FMモニタ", L"FM Monitor", L"Moniteur FM", L"Monitor FM",
		L"Monitor FM", L"FM 모니터", L"FM 监视器", L"مراقب FM",
		L"FM-монитор", L"FM-Monitor", L"Monitor FM", L"FM-monitor",
		L"Monitor FM", L"FM izleyici"));
	ModifyStyle(WS_MINIMIZEBOX, 0);
	RestoreGeom();
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	SetTimer(1, 16, NULL);
	m_fullDraw = 1;
	m_dirtyHead = m_dirtyHex = m_dirtyPanels = m_dirtyKeys = 1;
	return TRUE;
}

void CFmMonitorDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CFmMonitorDlg::ReleasePaintBuffers()
{
	if (m_frameDC.GetSafeHdc()) {
		if (m_frameOld) m_frameDC.SelectObject(m_frameOld);
		m_frameOld = nullptr;
		m_frameBmp.DeleteObject();
		m_frameDC.DeleteDC();
	}
	m_frameW = m_frameH = 0;
	m_layOk = 0;
	m_fullDraw = 1;
#if CCUSTOM_AERO_SUPPORT
	m_chromaCache.Release();
	m_chromaReady = false;
	m_chromaW = m_chromaH = 0;
#endif
}

bool CFmMonitorDlg::EnsureFrameBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
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

void CFmMonitorDlg::OnDestroy()
{
	PersistGeom();
	ReleasePaintBuffers();
	CCustomBlurDialogExBase::OnDestroy();
}

void CFmMonitorDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) return;
	if (CCC_IsAeroEnabled())
		CCC_RefreshDwmBlur(m_hWnd);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	/* 初期化中の誤保存を避け、ユーザー操作後だけ位置を書く（タイマーで間引き） */
	m_persistAge = 0;
	m_fullDraw = 1;
	m_dirtyHead = m_dirtyHex = m_dirtyPanels = m_dirtyKeys = 1;
	Invalidate(FALSE);
}

void CFmMonitorDlg::OnMove(int x, int y)
{
	CCustomBlurDialogExBase::OnMove(x, y);
	m_persistAge = 0;
}

void CFmMonitorDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	CCustomBlurDialogExBase::OnSysCommand(nID, lParam);
}

void CFmMonitorDlg::OnBnClickedHelp()
{
	AfxMessageBox(LL14(
		L"SASAMI .fpy の OPNA(YM2608) レジスタを表示します。\n"
		L"kbsasami.raira=1 時、プラグインが %TEMP%\\ogg_kbsasami\\ に dump を書き出し、本窓が同期します。\n"
		L"188h/18Ah=Bank0、18Ch/18Eh=Bank1。変化した hex と鍵盤以外の状態はフェードします。",
		L"Shows OPNA (YM2608) registers for SASAMI .fpy.\n"
		L"With kbsasami.raira=1 the plugin dumps to %TEMP%\\ogg_kbsasami\\ and this window syncs.\n"
		L"188h/18Ah=Bank0, 18Ch/18Eh=Bank1. Changed hex and non-key state fade.",
		L"Affiche les registres OPNA (YM2608) pour SASAMI .fpy.",
		L"Mostra i registri OPNA (YM2608) per SASAMI .fpy.",
		L"Muestra registros OPNA (YM2608) de SASAMI .fpy.",
		L"SASAMI .fpy의 OPNA(YM2608) 레지스터를 표시합니다.",
		L"显示 SASAMI .fpy 的 OPNA(YM2608) 寄存器。",
		L"يعرض سجلات OPNA (YM2608) لـ SASAMI .fpy.",
		L"Показывает регистры OPNA (YM2608) для SASAMI .fpy.",
		L"Zeigt OPNA-(YM2608)-Register fuer SASAMI .fpy.",
		L"Mostra registradores OPNA (YM2608) de SASAMI .fpy.",
		L"Toont OPNA-(YM2608)-registers voor SASAMI .fpy.",
		L"Pokazuje rejestry OPNA (YM2608) dla SASAMI .fpy.",
		L"SASAMI .fpy icin OPNA (YM2608) kayitlarini gosterir."));
}

BOOL CFmMonitorDlg::OnEraseBkgnd(CDC* pDC)
{
	(void)pDC;
	return TRUE;
}

void CFmMonitorDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) {
		/* 移動/リサイズ後だけ間引いて保存（常時 Commit は避ける） */
		if (m_persistAge >= 0 && ++m_persistAge >= 12) {
			if (!m_userClosing && IsWindowVisible())
				savedata.fmmonwindow = 1;
			PersistGeom();
			m_persistAge = -1; /* 次の OnSize/OnMove まで休止 */
		}
		IdlePulse();
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

double CFmMonitorDlg::ApproxHzFromFnum(uint8_t a4, uint8_t a0)
{
	/* YM2608/OPNA: fo = (clock/144) * Fnum * 2^(Block-1) / 2^20
	   （= Fnum * clock / (144 * 2^(21-Block))）。
	   2^(20-Block) だと1オクターブ高く、普通の曲でも O9C などになる。 */
	const int block = (a4 >> 3) & 7;
	const int fnum = ((a4 & 7) << 8) | a0;
	if (fnum <= 0) return 0.0;
	if (block <= 0)
		return (double)fnum * 7987200.0 / (144.0 * (double)(1u << 21));
	return (double)fnum * 7987200.0 / (144.0 * (double)(1u << (21 - block)));
}

int CFmMonitorDlg::ApproxMidiFromFnum(uint8_t a4, uint8_t a0)
{
	const double freq = ApproxHzFromFnum(a4, a0);
	if (freq < 8.0) return -1;
	int n = (int)(69.0 + 12.0 * log(freq / 440.0) / log(2.0) + 0.5);
	if (n < 0) n = 0;
	if (n > 127) n = 127;
	return n;
}

int CFmMonitorDlg::ApproxMidiFromSsg(uint16_t period)
{
	period &= 0x0FFF;
	if (period == 0) return -1;
	const double freq = 7987200.0 / (32.0 * (double)period);
	if (freq < 8.0) return -1;
	int n = (int)(69.0 + 12.0 * log(freq / 440.0) / log(2.0) + 0.5);
	if (n < 0) n = 0;
	if (n > 127) n = 127;
	return n;
}

/* 再生中のみ鍵盤・音名を出す。playy は停止後も 1 のままなので plf/ps/playf を見る */
static int FmMonIsLive()
{
	extern int plf;
	extern int playf;
	extern int ps;
	if (ps != 0) return 0;
	if (plf == 0 && playf == 0) return 0;
	return 1;
}

/* YM/SASAMI 流: MIDI60=O5C。音名は常に4文字（O5C / O5C#）で # 有無でも桁がずれない */
static void FmFormatNoteName(int midi, wchar_t* out, int outChars)
{
	if (!out || outChars < 5) return;
	if (midi < 0 || midi > 127) {
		wcsncpy_s(out, outChars, L"----", _TRUNCATE);
		return;
	}
	static const wchar_t* kName[12] = {
		L"C ", L"C#", L"D ", L"D#", L"E ", L"F ",
		L"F#", L"G ", L"G#", L"A ", L"A#", L"B "
	};
	_snwprintf_s(out, outChars, _TRUNCATE, L"O%d%s", midi / 12, kName[midi % 12]);
}

static int FmSsgNoisePeriod(const SasamiFmMonDump& d)
{
	return (int)(d.regs[6] & 0x1F);
}

static int FmSsgNoiseOn(const SasamiFmMonDump& d, int ch)
{
	if (ch < 0 || ch > 2) return 0;
	/* R7 bit3-5: 0=noise enable */
	return (((d.regs[7] >> (3 + ch)) & 1) == 0) ? 1 : 0;
}

int CFmMonitorDlg::PcmRows() const
{
	if (!m_haveDump) return 0;
	int n = m_dump.pcmCount;
	if (n < 0) n = 0;
	if (n > SASAMI_FMMON_PCM_MAX) n = SASAMI_FMMON_PCM_MAX;
	return n;
}

/* KPI(FPY) は MIDI モニタと同じ可聴補正を使う。
   OggGetHeardPcmFrames / GDI 時刻だけだとデコード側に近く、常に最新 dump を即表示 →
   スタンプやリングを直しても見た目が一切変わらない。mode==-3 は 700ms 引くのが本体の正解。 */
uint64_t CFmMonitorDlg::HeardSample(uint32_t sampleRate)
{
	extern int playy;
	extern int mode;
	extern int wavbit_sample_Hz;
	const uint32_t srDump = sampleRate > 0 ? sampleRate : 44100;

	__int64 frames = 0;
	if (mode == -3) {
		const double sec = OggGetGdiPlaybackTimeSec();
		frames = (__int64)(sec * (double)srDump + 0.5);
		frames -= (__int64)srDump * 700 / 1000;
	} else {
		const int srSrc = (wavbit_sample_Hz > 0) ? wavbit_sample_Hz : (int)srDump;
		frames = OggGetHeardPcmFrames();
		if (frames < 0) frames = 0;
		if (srSrc != (int)srDump)
			frames = frames * (__int64)srDump / (__int64)srSrc;
	}
	if (frames < 0) frames = 0;
	const uint64_t heard = (uint64_t)frames;

	if (playy == 0) {
		m_heardAnchor = heard;
		m_lastHeardSamp = heard;
		return heard;
	}
	/* シーク巻き戻し */
	if (m_lastHeardSamp > 0 && heard + (uint64_t)(srDump / 5) < m_lastHeardSamp) {
		m_heardAnchor = heard;
		m_lastHeardSamp = heard;
		return heard;
	}
	m_heardAnchor = heard;
	m_lastHeardSamp = heard;
	return heard;
}

int CFmMonitorDlg::ContentHeight(int dpi, int pcmRows) const
{
	const int pad = FmScale(4, dpi);
	const int head = pad + FmScale(14, dpi);
	const int cellH = FmScale(9, dpi);
	const int bankGap = FmScale(10, dpi);
	const int hexH = 2 * (FmScale(18, dpi) + 16 * cellH) + bankGap;
	const int fmPanelH = (std::max)(hexH, FmScale(320, dpi));
	const int rowH = FmScale(14, dpi);
	const int chRows = 6 + 3 + pcmRows + 1;
	const int keys = FmScale(4, dpi) + chRows * rowH;
	return head + fmPanelH + keys + pad;
}

int CFmMonitorDlg::PreferredWidth(int dpi) const
{
	const int pad = FmScale(4, dpi);
	const int cellW = FmScale(18, dpi); /* Consolas "00" + margin */
	const int gapExtra = FmScale(4, dpi);
	const int hexW = cellW + 16 * cellW + 4 * gapExtra + FmScale(8, dpi);
	const int fmW = FmScale(560, dpi);
	const int labelW = FmScale(58, dpi);
	const int pianoMin = FmScale(360, dpi);
	const int top = hexW + FmScale(6, dpi) + fmW;
	const int bot = labelW + pianoMin;
	return pad * 2 + (std::max)(top, bot);
}

void CFmMonitorDlg::RestoreGeom()
{
	const UINT dpi = FmUiDpi(m_hWnd ? m_hWnd : nullptr);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int clientW = PreferredWidth((int)dpi);
	const int clientH = ContentHeight((int)dpi, 0);
	/* クライアント→外枠 */
	CRect rc(0, 0, clientW, clientH + capH);
	AdjustWindowRectEx(&rc, GetStyle(), FALSE, GetExStyle());
	int prefW = rc.Width();
	int prefH = rc.Height();

	FmMonGeomFile side = {};
	const int hasSide = FmMonGeomLoad(&side);
	if (hasSide) {
		/* 位置・サイズのみ。開閉フラグは呼び出し側 / 起動時ロードが正 */
		if (side.w >= 280 && side.w <= 4000) savedata.fmmonw = side.w;
		if (side.h >= 180 && side.h <= 3000) savedata.fmmonh = side.h;
		if (side.x > -30000 && side.y > -30000) {
			savedata.fmmonx = side.x;
			savedata.fmmony = side.y;
		}
	}

	int x = savedata.fmmonx, y = savedata.fmmony;
	int w = savedata.fmmonw, h = savedata.fmmonh;
	if (w < 280 || w > 4000) w = prefW;
	if (h < 180 || h > 3000) h = prefH;
	if (x < -30000 || y < -30000) { x = 80; y = 80; }
	SetWindowPos(nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void CFmMonitorDlg::PersistGeom()
{
	if (!::IsWindow(GetSafeHwnd()) || IsIconic()) return;
	CRect rc;
	GetWindowRect(&rc);
	if (rc.Width() < 160 || rc.Height() < 120) return;
	savedata.fmmonx = rc.left;
	savedata.fmmony = rc.top;
	savedata.fmmonw = rc.Width();
	savedata.fmmonh = rc.Height();
	/* IsWindowVisible に頼らない（メニュー閉鎖時に開いたまま保存されるバグを防ぐ） */
	const int open = m_userClosing ? 0 : (savedata.fmmonwindow ? 1 : 0);
	savedata.fmmonwindow = open;
	FmMonGeomSave(open, savedata.fmmonx, savedata.fmmony, savedata.fmmonw, savedata.fmmonh);
}

void CFmMonitorDlg::DetachForDestroy()
{
	m_userClosing = 0;
	savedata.fmmonwindow = 1;
	PersistGeom();
	KillTimer(1);
	DatArc_InvalidateLeaf(L"oggYSEDbgmu.dat");
	OggPersistSaveDatNow();
}

void CFmMonitorDlg::OnClose()
{
	m_userClosing = 1;
	savedata.fmmonwindow = 0;
	PersistGeom();
	DatArc_InvalidateLeaf(L"oggYSEDbgmu.dat");
	OggPersistSaveDatNow();
	ShowWindow(SW_HIDE);
	DestroyWindow();
}

static int FmHexColX(int col, int cellW, int gapExtra)
{
	return col * cellW + (col / 4) * gapExtra;
}

static int FmHexRowIsFmOps(int row)
{
	/* 0x30..0x9F: 4-op パラメータ（3ch x 4op の並び） */
	return row >= 0x3 && row <= 0x9;
}

static int FmHexColInOpGroup(int col)
{
	return (col % 4) != 3;
}

static void FmFrameRect(CDC& dc, const CRect& rc, COLORREF penColor)
{
	/* Rectangle は現行ブラシで塗り潰す → 白抜けの元凶。枠のみ描く */
	CPen pen(PS_SOLID, 1, penColor);
	CPen* oldp = dc.SelectObject(&pen);
	HGDIOBJ oldb = ::SelectObject(dc.GetSafeHdc(), ::GetStockObject(NULL_BRUSH));
	dc.Rectangle(rc.left, rc.top, rc.right, rc.bottom);
	::SelectObject(dc.GetSafeHdc(), oldb);
	dc.SelectObject(oldp);
}

static HFONT s_fmFontCache[21]; /* px 8..20 */

static HFONT FmMakeFont(int px)
{
	if (px < 8) px = 8;
	if (px > 20) px = 20;
	if (!s_fmFontCache[px]) {
		s_fmFontCache[px] = ::CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
	}
	if (s_fmFontCache[px])
		return s_fmFontCache[px];
	return (HFONT)::GetStockObject(ANSI_FIXED_FONT);
}

static void FmReleaseFontCache()
{
	for (int i = 0; i < 21; i++) {
		if (s_fmFontCache[i]) {
			::DeleteObject(s_fmFontCache[i]);
			s_fmFontCache[i] = nullptr;
		}
	}
}

void CFmMonitorDlg::DrawHexBank(CDC& dc, int x, int y, int cellW, int cellH, int gapExtra, int bankBase, const wchar_t* title)
{
	const int fontPx = (std::max)(8, (std::min)(cellH - 2, cellW * 2 / 3));
	HFONT font = FmMakeFont(fontPx);
	HFONT oldf = (HFONT)dc.SelectObject(font);
	/* タイトルは col ヘッダの上・bankTitle 帯の中。cellH*2 上だと rcHead に食い込む */
	const int titleY = y - cellH - fontPx - 2;
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(200, 220, 210));
	dc.TextOut(x, titleY, title);

	wchar_t hdr[4];
	for (int col = 0; col < 16; col++) {
		_snwprintf_s(hdr, _TRUNCATE, L"%X", col);
		dc.SetTextColor(RGB(150, 175, 160));
		CSize hs = dc.GetTextExtent(hdr);
		dc.TextOut(x + FmHexColX(col, cellW, gapExtra) + (cellW - hs.cx) / 2, y - cellH, hdr);
	}

	const COLORREF baseDark = RGB(24, 28, 32);
	const COLORREF baseGroup = RGB(36, 52, 44);   /* 薄緑系グループ */
	const COLORREF baseTouched = RGB(48, 72, 58);
	const COLORREF hi = RGB(80, 200, 120);

	for (int row = 0; row < 16; row++) {
		_snwprintf_s(hdr, _TRUNCATE, L"%X", row);
		dc.SetTextColor(RGB(160, 180, 170));
		dc.SetBkMode(TRANSPARENT);
		dc.TextOut(x - cellW + 1, y + row * cellH + (cellH - fontPx) / 2, hdr);

		const int opsRow = FmHexRowIsFmOps(row);
		for (int col = 0; col < 16; col++) {
			const int idx = bankBase + row * 16 + col;
			const int px = x + FmHexColX(col, cellW, gapExtra);
			const int py = y + row * cellH;
			const int inGroup = FmHexColInOpGroup(col);
			COLORREF base = baseDark;
			if (inGroup && opsRow)
				base = m_touched[idx] ? baseTouched : baseGroup;
			else if (inGroup && (m_touched[idx] || (m_haveDump && m_dump.regs[idx] != 0)))
				base = m_touched[idx] ? baseTouched : baseGroup;
			else if (m_touched[idx])
				base = baseTouched;

			const COLORREF cellBg = FmMixFade(base, hi, m_fade[idx]);
			dc.FillSolidRect(px, py, cellW - 1, cellH - 1, cellBg);
			wchar_t t[4];
			if (m_haveDump)
				_snwprintf_s(t, _TRUNCATE, L"%02X", m_dump.regs[idx]);
			else
				wcscpy_s(t, L"--");
			/* OPAQUE+base だとフェード塗りを文字背景で潰し縁だけ緑に見える */
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(m_touched[idx] ? RGB(240, 250, 245) : RGB(130, 145, 138));
			CSize ts = dc.GetTextExtent(t);
			dc.TextOut(px + (cellW - ts.cx) / 2, py + (cellH - ts.cy) / 2, t);
		}
	}
	dc.SelectObject(oldf);
}

static void FmDeleteFont(HFONT)
{
	/* フォントはキャッシュ — 破棄しない */
}

/* セル内ノブ。背景は呼び出し側が塗る。ラベルは円の下。 */
static void FmDrawKnob(CDC& dc, int cx, int cy, int r, int val, int vmax, const wchar_t* name, COLORREF back)
{
	if (r < 5) r = 5;
	if (vmax < 1) vmax = 1;
	if (val < 0) val = 0;
	if (val > vmax) val = vmax;

	CBrush fill(RGB(36, 52, 44));
	CBrush* oldBr = dc.SelectObject(&fill);
	CPen pen(PS_SOLID, 1, RGB(140, 190, 160));
	CPen* oldp = dc.SelectObject(&pen);
	dc.Ellipse(cx - r, cy - r, cx + r + 1, cy + r + 1);
	dc.SelectObject(oldBr);

	const double a0 = 3.1415926535 * 0.75;
	const double a1 = 3.1415926535 * 2.25;
	const double t = (double)val / (double)vmax;
	const double ang = a0 + (a1 - a0) * t;
	const int x1 = cx + (int)(cos(ang) * (r - 2));
	const int y1 = cy - (int)(sin(ang) * (r - 2));
	CPen needle(PS_SOLID, (std::max)(1, r / 5), RGB(255, 255, 255));
	dc.SelectObject(&needle);
	dc.MoveTo(cx, cy);
	dc.LineTo(x1, y1);
	dc.SelectObject(oldp);

	const int fontPx = (std::max)(8, (std::min)(r - 1, 14));
	HFONT font = FmMakeFont(fontPx);
	HFONT oldf = (HFONT)dc.SelectObject(font);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(245, 250, 245));
	wchar_t vs[8];
	_snwprintf_s(vs, _TRUNCATE, L"%d", val);
	CSize vsZ = dc.GetTextExtent(vs);
	dc.TextOut(cx - vsZ.cx / 2, cy - vsZ.cy / 2, vs);

	const int namePx = (std::max)(8, (std::min)(fontPx, r * 2 / 3 + 2));
	HFONT nf = FmMakeFont(namePx);
	dc.SelectObject(nf);
	dc.SetTextColor(RGB(190, 220, 200));
	CSize ns = dc.GetTextExtent(name);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(back);
	dc.TextOut(cx - ns.cx / 2, cy + r + 1, name);
	dc.SelectObject(oldf);
	FmDeleteFont(font);
	FmDeleteFont(nf);
}

/* 狭いとき用: 上に名前、下に値の矩形セル（重ならないよう cell 内に収める） */
static void FmDrawParamCell(CDC& dc, const CRect& cell, int val, int /*vmax*/, const wchar_t* name, COLORREF back)
{
	if (cell.Width() < 8 || cell.Height() < 12) return;
	dc.FillSolidRect(cell, back);
	FmFrameRect(dc, cell, RGB(60, 90, 72));

	const int namePx = (std::max)(8, (std::min)(11, cell.Height() / 3));
	const int valPx = (std::max)(9, (std::min)(16, cell.Height() * 2 / 5));
	HFONT nf = FmMakeFont(namePx);
	HFONT vf = FmMakeFont(valPx);
	HFONT oldf = (HFONT)dc.SelectObject(nf);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(back);
	dc.SetTextColor(RGB(160, 200, 175));
	CSize ns = dc.GetTextExtent(name);
	dc.TextOut(cell.left + (cell.Width() - ns.cx) / 2, cell.top + 1, name);

	dc.SelectObject(vf);
	dc.SetTextColor(RGB(235, 250, 240));
	wchar_t vs[8];
	_snwprintf_s(vs, _TRUNCATE, L"%d", val);
	CSize vsZ = dc.GetTextExtent(vs);
	dc.TextOut(cell.left + (cell.Width() - vsZ.cx) / 2,
		cell.top + namePx + (cell.Height() - namePx - vsZ.cy) / 2, vs);
	dc.SelectObject(oldf);
	FmDeleteFont(nf);
	FmDeleteFont(vf);
}

/* セル幅に応じてノブ or 矩形。必ず cell 内に描く */
static void FmDrawParamInCell(CDC& dc, const CRect& cell, int val, int vmax, const wchar_t* name, COLORREF back)
{
	if (cell.Width() < 10 || cell.Height() < 14) return;
	const int labelReserve = (std::max)(9, cell.Height() / 4);
	const int r = (std::min)((cell.Width() - 2) / 2, (cell.Height() - labelReserve - 2) / 2);
	if (r >= 7) {
		dc.FillSolidRect(cell, back);
		const int cx = cell.left + cell.Width() / 2;
		const int cy = cell.top + r + 1;
		FmDrawKnob(dc, cx, cy, r, val, vmax, name, back);
	} else {
		FmDrawParamCell(dc, cell, val, vmax, name, back);
	}
}

/* YM2608 / OPN 系 ALGO 0..7 */
static void FmDrawAlgo(CDC& dc, const CRect& rc, int alg, int fontPx)
{
	const COLORREF bg = RGB(28, 44, 36);
	dc.FillSolidRect(rc, bg);
	FmFrameRect(dc, rc, RGB(90, 150, 120));
	HFONT font = FmMakeFont(fontPx);
	HFONT oldf = (HFONT)dc.SelectObject(font);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(bg);
	dc.SetTextColor(RGB(180, 230, 200));
	wchar_t at[16];
	_snwprintf_s(at, _TRUNCATE, L"ALGO %d", alg & 7);
	dc.TextOut(rc.left + 3, rc.top + 1, at);

	const int margin = 6;
	const int top = rc.top + fontPx + 3;
	const int bot = rc.bottom - 4;
	const int left = rc.left + margin;
	const int right = rc.right - margin;
	const int bw = (std::max)(14, (right - left - 20) / 4);
	const int bh = (std::max)(10, (bot - top - 10) / 3);
	CRect box[4];

	auto place = [&](int i, int col, int row, int cols, int rows) {
		const int cellW = (right - left) / (std::max)(1, cols);
		const int cellH = (bot - top) / (std::max)(1, rows);
		const int cx = left + col * cellW + cellW / 2;
		const int cy = top + row * cellH + cellH / 2;
		box[i].SetRect(cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2);
	};

	alg &= 7;
	switch (alg) {
	case 0: place(0, 0, 1, 4, 3); place(1, 1, 1, 4, 3); place(2, 2, 1, 4, 3); place(3, 3, 1, 4, 3); break;
	case 1: place(0, 0, 0, 3, 3); place(1, 0, 2, 3, 3); place(2, 1, 1, 3, 3); place(3, 2, 1, 3, 3); break;
	case 2: place(0, 0, 0, 3, 3); place(1, 0, 2, 3, 3); place(2, 1, 2, 3, 3); place(3, 2, 1, 3, 3); break;
	case 3: place(0, 0, 0, 3, 3); place(1, 1, 0, 3, 3); place(2, 1, 2, 3, 3); place(3, 2, 1, 3, 3); break;
	case 4: place(0, 0, 0, 2, 2); place(1, 1, 0, 2, 2); place(2, 0, 1, 2, 2); place(3, 1, 1, 2, 2); break;
	case 5: place(0, 0, 1, 2, 3); place(1, 1, 0, 2, 3); place(2, 1, 1, 2, 3); place(3, 1, 2, 2, 3); break;
	case 6: place(0, 0, 0, 2, 3); place(1, 1, 0, 2, 3); place(2, 1, 1, 2, 3); place(3, 1, 2, 2, 3); break;
	default: /* 7: 全部キャリア */ place(0, 0, 1, 4, 3); place(1, 1, 1, 4, 3); place(2, 2, 1, 4, 3); place(3, 3, 1, 4, 3); break;
	}

	CPen wire(PS_SOLID, 1, RGB(140, 210, 170));
	CPen* oldp = dc.SelectObject(&wire);
	auto wireTo = [&](int a, int b) {
		const POINT pa = { box[a].CenterPoint().x, box[a].CenterPoint().y };
		const POINT pb = { box[b].CenterPoint().x, box[b].CenterPoint().y };
		const int ax = (pa.x < pb.x) ? box[a].right : ((pa.x > pb.x) ? box[a].left : pa.x);
		const int ay = (pa.y < pb.y) ? box[a].bottom : ((pa.y > pb.y) ? box[a].top : pa.y);
		const int bx = (pb.x < pa.x) ? box[b].right : ((pb.x > pa.x) ? box[b].left : pb.x);
		const int by = (pb.y < pa.y) ? box[b].bottom : ((pb.y > pa.y) ? box[b].top : pb.y);
		dc.MoveTo(ax, ay);
		if (ax != bx && ay != by) { dc.LineTo(bx, ay); dc.LineTo(bx, by); }
		else dc.LineTo(bx, by);
	};
	switch (alg) {
	case 0: wireTo(0, 1); wireTo(1, 2); wireTo(2, 3); break;
	case 1: wireTo(0, 2); wireTo(1, 2); wireTo(2, 3); break;
	case 2: wireTo(1, 2); wireTo(2, 3); wireTo(0, 3); break;
	case 3: wireTo(0, 1); wireTo(1, 3); wireTo(2, 3); break;
	case 4: wireTo(0, 1); wireTo(2, 3); break;
	case 5: wireTo(0, 1); wireTo(0, 2); wireTo(0, 3); break;
	case 6: wireTo(0, 1); break;
	default: break;
	}
	dc.SelectObject(oldp);

	for (int i = 0; i < 4; i++) {
		dc.FillSolidRect(box[i], RGB(44, 68, 54));
		FmFrameRect(dc, box[i], RGB(120, 180, 140));
		wchar_t s[8];
		_snwprintf_s(s, _TRUNCATE, L"S%d", i + 1);
		dc.SetBkColor(RGB(44, 68, 54));
		dc.SetTextColor(RGB(240, 250, 245));
		CSize sz = dc.GetTextExtent(s);
		dc.TextOut(box[i].left + (box[i].Width() - sz.cx) / 2,
			box[i].top + (box[i].Height() - sz.cy) / 2, s);
	}
	{
		CPen fb(PS_SOLID, 1, RGB(255, 190, 100));
		CPen* op = dc.SelectObject(&fb);
		const int cx = box[0].left;
		dc.Arc(cx - 10, box[0].top - 7, cx + 8, box[0].bottom + 7,
			cx, box[0].top, cx, box[0].bottom);
		dc.MoveTo(cx - 1, box[0].top);
		dc.LineTo(cx - 4, box[0].top - 4);
		dc.SelectObject(op);
	}
	dc.SelectObject(oldf);
	FmDeleteFont(font);
}

static void FmDrawEnvelope(CDC& dc, const CRect& rc, int ar, int dr, int sr, int rr, int sl, int tl)
{
	dc.FillSolidRect(rc, RGB(18, 26, 22));
	FmFrameRect(dc, rc, RGB(70, 100, 80));
	const int x0 = rc.left + 2;
	const int x1 = rc.right - 2;
	const int y0 = rc.top + 2;
	const int y1 = rc.bottom - 2;
	const int w = (std::max)(8, x1 - x0);
	const int h = (std::max)(8, y1 - y0);
	/* TL=減衰。表示は上が大音量 */
	auto Y = [&](int atten) {
		if (atten < 0) atten = 0;
		if (atten > 127) atten = 127;
		return y0 + atten * h / 127;
	};
	const int peak = tl & 127;
	const int sus = peak + (127 - peak) * (sl & 15) / 15;
	int px = x0;
	POINT pts[5];
	pts[0] = { px, y1 };
	px += 2 + (31 - (ar & 31)) * w / 80; if (px > x1) px = x1;
	pts[1] = { px, Y(peak) };
	px += 2 + (31 - (dr & 31)) * w / 90; if (px > x1) px = x1;
	pts[2] = { px, Y(sus) };
	px += 2 + (31 - (sr & 31)) * w / 70; if (px > x1) px = x1;
	pts[3] = { px, Y(sus) };
	px += 2 + (15 - (rr & 15)) * w / 40; if (px > x1) px = x1;
	pts[4] = { px, y1 };
	CPen env(PS_SOLID, 1, RGB(120, 220, 170));
	CPen* oldp = dc.SelectObject(&env);
	dc.MoveTo(pts[0]);
	for (int i = 1; i < 5; i++) dc.LineTo(pts[i]);
	dc.SelectObject(oldp);
}

void CFmMonitorDlg::DrawFmChPanel(CDC& dc, const CRect& rc, int ch)
{
	if (rc.Width() < 100 || rc.Height() < 80) return;
	const int bank = (ch < 3) ? 0 : 0x100;
	const int slot = (ch < 3) ? ch : (ch - 3);
	auto reg = [&](int r) -> uint8_t {
		return m_haveDump ? m_dump.regs[bank + r] : 0;
	};

	const int savedDC = dc.SaveDC();
	dc.IntersectClipRect(rc);

	const COLORREF headBg = RGB(40, 64, 52);
	const COLORREF bodyBg = RGB(28, 36, 40);
	const COLORREF rowBg = RGB(32, 42, 38);
	const int pad = (std::max)(3, rc.Width() / 90);
	/* ヘッダは高さの 30%、最低 72 */
	const int headH = (std::max)(72, rc.Height() * 30 / 100);
	dc.FillSolidRect(rc.left, rc.top, rc.Width(), headH, headBg);
	dc.FillSolidRect(rc.left, rc.top + headH, rc.Width(), rc.Height() - headH, bodyBg);
	FmFrameRect(dc, rc, RGB(100, 160, 130));

	const uint8_t b0 = reg(0xB0 + slot);
	const uint8_t b4 = reg(0xB4 + slot);
	const uint8_t a4 = reg(0xA4 + slot);
	const uint8_t a0 = reg(0xA0 + slot);
	const int alg = b0 & 7;
	const int fb = (b0 >> 3) & 7;
	const int panL = (b4 >> 7) & 1;
	const int panR = (b4 >> 6) & 1;
	const int ams = (b4 >> 4) & 3;
	const int pms = b4 & 7;
	const int pan = (panL && panR) ? 3 : (panL ? 1 : (panR ? 2 : 0));
	const int midi = ApproxMidiFromFnum(a4, a0);
	const double hz = ApproxHzFromFnum(a4, a0);
	extern int playy;
	const int keyed = FmMonIsLive() && m_haveDump && m_dump.keyOnFm[ch];

	/* ---- ヘッダ: 左 ALGO / 中央ノブ / 右 NOTE ---- */
	const int titlePx = (std::max)(11, headH / 8);
	HFONT titleFont = FmMakeFont(titlePx);
	HFONT oldf = (HFONT)dc.SelectObject(titleFont);
	dc.SetBkMode(OPAQUE);
	dc.SetBkColor(headBg);
	dc.SetTextColor(RGB(220, 245, 230));
	wchar_t title[24];
	_snwprintf_s(title, _TRUNCATE, L"FM CH%d", ch + 1);
	dc.TextOut(rc.left + pad, rc.top + 2, title);

	const int headInnerTop = rc.top + titlePx + 4;
	const int headInnerBot = rc.top + headH - pad;
	const int headInnerH = (std::max)(40, headInnerBot - headInnerTop);
	const int innerW = rc.Width() - pad * 2;
	/* 比率: algo 36% / knobs 34% / info 30% */
	const int algoW = innerW * 36 / 100;
	const int infoW = innerW * 30 / 100;
	const int knobBandW = innerW - algoW - infoW - pad * 2;

	CRect algo(rc.left + pad, headInnerTop, rc.left + pad + algoW, headInnerBot);
	FmDrawAlgo(dc, algo, alg, (std::max)(9, titlePx - 1));

	CRect knobsRc(algo.right + pad, headInnerTop, algo.right + pad + knobBandW, headInnerBot);
	{
		const int cellW = knobsRc.Width() / 2;
		const int cellH = knobsRc.Height() / 2;
		const struct { int v; int vmax; const wchar_t* n; } k4[4] = {
			{ ams, 3, L"AMS" }, { pms, 7, L"PMS" }, { fb, 7, L"FB" }, { pan, 3, L"PAN" }
		};
		for (int i = 0; i < 4; i++) {
			CRect c(
				knobsRc.left + (i % 2) * cellW + 1,
				knobsRc.top + (i / 2) * cellH + 1,
				knobsRc.left + (i % 2) * cellW + cellW - 1,
				knobsRc.top + (i / 2) * cellH + cellH - 1);
			FmDrawParamInCell(dc, c, k4[i].v, k4[i].vmax, k4[i].n, headBg);
		}
	}

	CRect infoRc(rc.right - pad - infoW, headInnerTop, rc.right - pad, headInnerBot);
	{
		const int infoPx = (std::max)(10, (std::min)(14, infoRc.Height() / 5));
		HFONT infoFont = FmMakeFont(infoPx);
		dc.SelectObject(infoFont);
		dc.SetBkColor(headBg);
		dc.SetTextColor(RGB(230, 245, 235));
		wchar_t line[64];
		int iy = infoRc.top + 2;
		if (midi >= 0) _snwprintf_s(line, _TRUNCATE, L"NOTE: %d", midi);
		else wcscpy_s(line, L"NOTE: --");
		dc.TextOut(infoRc.left + 2, iy, line);
		iy += infoPx + 2;
		_snwprintf_s(line, _TRUNCATE, L"%.0f Hz", hz);
		dc.TextOut(infoRc.left + 2, iy, line);
		iy += infoPx + 4;
		dc.TextOut(infoRc.left + 2, iy, L"SLOT");
		iy += infoPx + 2;
		const int box = (std::max)(12, (std::min)(infoPx + 2, (infoRc.Width() - 8) / 4 - 2));
		for (int s = 0; s < 4; s++) {
			const int sx = infoRc.left + 2 + s * (box + 2);
			dc.FillSolidRect(sx, iy, box, box, keyed ? RGB(70, 200, 120) : RGB(40, 50, 44));
			FmFrameRect(dc, CRect(sx, iy, sx + box, iy + box), RGB(90, 140, 110));
			wchar_t sn[4];
			_snwprintf_s(sn, _TRUNCATE, L"%d", s + 1);
			dc.SetTextColor(RGB(245, 250, 245));
			CSize sz = dc.GetTextExtent(sn);
			dc.TextOut(sx + (box - sz.cx) / 2, iy + (box - sz.cy) / 2, sn);
		}
		FmDeleteFont(infoFont);
	}

	/* ---- 本体: 4オペレータ行 ---- */
	const int bodyTop = rc.top + headH + pad;
	const int bodyBot = rc.bottom - pad;
	const int slotH = (bodyBot - bodyTop) / 4;
	if (slotH < 24) {
		dc.SelectObject(oldf);
		FmDeleteFont(titleFont);
		dc.RestoreDC(savedDC);
		return;
	}

	for (int op = 0; op < 4; op++) {
		const int yy = bodyTop + op * slotH;
		CRect row(rc.left + pad, yy, rc.right - pad, yy + slotH - 2);
		dc.FillSolidRect(row, rowBg);

		const uint8_t dtMul = reg(0x30 + op * 4 + slot);
		const uint8_t tl = reg(0x40 + op * 4 + slot);
		const uint8_t ksAr = reg(0x50 + op * 4 + slot);
		const uint8_t amDr = reg(0x60 + op * 4 + slot);
		const uint8_t srV = reg(0x70 + op * 4 + slot);
		const uint8_t slRr = reg(0x80 + op * 4 + slot);
		const uint8_t ssg = reg(0x90 + op * 4 + slot);
		const int mul = dtMul & 0x0F;
		const int dt1 = (dtMul >> 4) & 0x07;
		const int ar = ksAr & 0x1F;
		const int dr = amDr & 0x1F;
		const int am = (amDr >> 7) & 1;
		const int srate = srV & 0x1F;
		const int sl = (slRr >> 4) & 0x0F;
		const int rr = slRr & 0x0F;
		const int tl7 = tl & 0x7F;

		/* 列: [S#][env][params 6列][AM/EG] — 幅から逆算 */
		const int labW = (std::max)(16, (std::min)(28, row.Width() / 18));
		const int flagW = (std::max)(28, (std::min)(44, row.Width() / 10));
		const int envW = (std::max)(36, (std::min)(72, row.Width() / 6));
		const int paramLeft = row.left + labW + 2;
		const int envRight = paramLeft + envW;
		const int flagLeft = row.right - flagW;
		const int paramRight = flagLeft - 2;
		const int paramW = (std::max)(48, paramRight - envRight - 2);

		const int labPx = (std::max)(10, (std::min)(14, row.Height() / 3));
		HFONT labFont = FmMakeFont(labPx);
		dc.SelectObject(labFont);
		dc.SetBkMode(OPAQUE);
		dc.SetBkColor(rowBg);
		dc.SetTextColor(RGB(170, 220, 190));
		wchar_t sn[8];
		_snwprintf_s(sn, _TRUNCATE, L"S%d", op + 1);
		CSize snZ = dc.GetTextExtent(sn);
		dc.TextOut(row.left + (labW - snZ.cx) / 2, row.top + (row.Height() - snZ.cy) / 2, sn);

		CRect env(paramLeft, row.top + 2, envRight, row.bottom - 2);
		FmDrawEnvelope(dc, env, ar, dr, srate, rr, sl, tl7);

		/* パラメータ格子: 上段6 (AR..TL)、下段2 (MUL DT)。セル幅=paramW/6 */
		const int cols = 6;
		const int cellW = paramW / cols;
		const int gapY = 1;
		const int topRowH = row.Height() * 58 / 100;
		const int botRowH = row.Height() - topRowH - gapY;
		CRect band(envRight + 2, row.top + 1, envRight + 2 + cellW * cols, row.bottom - 1);

		const struct { int v; int vmax; const wchar_t* n; } topP[6] = {
			{ ar, 31, L"AR" }, { dr, 31, L"DR" }, { srate, 31, L"SR" },
			{ rr, 15, L"RR" }, { sl, 15, L"SL" }, { tl7, 127, L"TL" }
		};
		for (int i = 0; i < 6; i++) {
			CRect c(band.left + i * cellW, band.top, band.left + (i + 1) * cellW - 1, band.top + topRowH);
			FmDrawParamInCell(dc, c, topP[i].v, topP[i].vmax, topP[i].n, rowBg);
		}
		if (botRowH >= 16) {
			const struct { int v; int vmax; const wchar_t* n; } botP[2] = {
				{ mul, 15, L"MUL" }, { dt1, 7, L"DT" }
			};
			for (int i = 0; i < 2; i++) {
				CRect c(band.left + i * cellW, band.top + topRowH + gapY,
					band.left + (i + 1) * cellW - 1, band.bottom);
				FmDrawParamInCell(dc, c, botP[i].v, botP[i].vmax, botP[i].n, rowBg);
			}
		}

		/* AM / EG ランプ（専用列・他と非重複） */
		{
			const int half = flagW / 2;
			const int lampPx = (std::max)(8, (std::min)(11, row.Height() / 4));
			HFONT lf = FmMakeFont(lampPx);
			dc.SelectObject(lf);
			dc.SetBkColor(rowBg);
			dc.SetTextColor(RGB(190, 220, 200));
			dc.TextOut(flagLeft + 1, row.top + 1, L"AM");
			const int lamp = (std::max)(8, (std::min)(half - 4, row.Height() / 3));
			dc.FillSolidRect(flagLeft + 2, row.top + lampPx + 2, lamp, lamp,
				am ? RGB(80, 200, 140) : RGB(40, 48, 44));
			FmFrameRect(dc, CRect(flagLeft + 2, row.top + lampPx + 2, flagLeft + 2 + lamp, row.top + lampPx + 2 + lamp),
				RGB(90, 130, 100));

			dc.TextOut(flagLeft + half, row.top + 1, L"EG");
			dc.FillSolidRect(flagLeft + half, row.top + lampPx + 2, lamp + 2, (std::max)(6, lamp / 2),
				(ssg & 0x08) ? RGB(200, 170, 80) : RGB(40, 48, 44));
			FmDeleteFont(lf);
		}

		FmDeleteFont(labFont);
	}

	dc.SelectObject(oldf);
	FmDeleteFont(titleFont);
	dc.RestoreDC(savedDC);
}
void CFmMonitorDlg::DrawPiano108(CDC& dc, const CRect& rc, int midiNote, int lit)
{
	if (rc.Width() < 40 || rc.Height() < 8) return;
	const int k0 = 21, k1 = 108;
	int note = midiNote;
	if (note < k0 || note > k1) note = -1;
	int whites = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) whites++;
	}
	if (whites < 1) return;
	const int ww = rc.Width();
	const int hh = rc.Height();
	const COLORREF keyW = RGB(228, 228, 232);
	const COLORREF keyB = RGB(22, 24, 28);
	const COLORREF litW = RGB(220, 40, 40);
	const COLORREF litB = RGB(255, 70, 70);
	const COLORREF gap = RGB(12, 14, 18);

	dc.FillSolidRect(rc, gap);

	int wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m == 1 || m == 3 || m == 6 || m == 8 || m == 10) continue;
		const int x0 = rc.left + wi * ww / whites;
		const int x1 = rc.left + (wi + 1) * ww / whites;
		const int kw = (std::max)(1, x1 - x0 - 1);
		COLORREF c = keyW;
		if (lit && note == n) c = litW;
		dc.FillSolidRect(x0, rc.top, kw, hh, c);
		wi++;
	}
	wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) { wi++; continue; }
		const int xw = rc.left + (wi * ww / whites);
		const int bw = (std::max)(2, ww / whites * 55 / 100);
		const int x0 = xw - bw / 2;
		COLORREF c = keyB;
		if (lit && note == n) c = litB;
		dc.FillSolidRect(x0, rc.top, bw, hh * 62 / 100, c);
	}
}

void CFmMonitorDlg::DrawChannelKeys(CDC& dc, int x, int y, int w, int rowH, int keyH, int labelW)
{
	const int live = FmMonIsLive();
	const int fontPx = (std::max)(10, (std::min)(14, keyH - 1));
	HFONT labFont = FmMakeFont(fontPx);
	HFONT oldf = (HFONT)dc.SelectObject(labFont);
	dc.SetBkMode(TRANSPARENT);
	int row = 0;
	/* 等幅: "SSG1 O5C# N031" — # と N 桁で揺れないよう固定幅名 */
	const int lampProbe = (keyH > 4) ? (keyH * 3 / 4) : 8;
	const int needW = lampProbe + 4
		+ dc.GetTextExtent(L"SSG1 O5C# N031").cx
		+ FmScale(10, FmUiDpi(GetSafeHwnd()));
	if (labelW < needW) labelW = needW;
	int pianoW = w - labelW;
	if (pianoW < 80) pianoW = (w > labelW) ? (w - labelW) : 80;

	auto drawLabel = [&](int yy, const wchar_t* text, BYTE fade, COLORREF hi) {
		const int lamp = (keyH > 4) ? (keyH * 3 / 4) : 8;
		FmFillFade(dc, x, yy + (rowH - lamp) / 2, lamp, lamp,
			RGB(40, 44, 50), hi, fade);
		dc.SetTextColor(RGB(210, 215, 220));
		dc.TextOut(x + lamp + 4, yy + (rowH - keyH) / 2, text);
	};

	static const wchar_t* kFmName[6] = { L"FM1", L"FM2", L"FM3", L"FM4", L"FM5", L"FM6" };
	for (int ch = 0; ch < 6; ch++, row++) {
		const int yy = y + row * rowH;
		const int bank = (ch < 3) ? 0 : 0x100;
		const int slot = (ch < 3) ? ch : (ch - 3);
		const uint8_t a4 = m_haveDump ? m_dump.regs[bank + 0xA4 + slot] : 0;
		const uint8_t a0 = m_haveDump ? m_dump.regs[bank + 0xA0 + slot] : 0;
		const int keyLit = live && m_haveDump && m_dump.keyOnFm[ch];
		/* 休符中は fnum が次音のまま残るので、gate on のときだけ音名／赤鍵 */
		const int midi = keyLit ? ApproxMidiFromFnum(a4, a0) : -1;
		const BYTE fade = live ? m_fadeKey[ch] : (BYTE)0;

		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		wchar_t lab[40];
		_snwprintf_s(lab, _TRUNCATE, L"%s %s", kFmName[ch], note);
		drawLabel(yy, lab, fade, RGB(80, 220, 120));

		CRect krc(x + labelW, yy + (rowH - keyH) / 2, x + labelW + pianoW, yy + (rowH - keyH) / 2 + keyH);
		DrawPiano108(dc, krc, midi, keyLit);
	}

	static const wchar_t* kSsg[3] = { L"SSG1", L"SSG2", L"SSG3" };
	for (int i = 0; i < 3; i++, row++) {
		const int yy = y + row * rowH;
		const uint16_t period = m_haveDump
			? (uint16_t)(m_dump.regs[i * 2] | ((m_dump.regs[i * 2 + 1] & 0x0F) << 8))
			: 0;
		const int keyLit = live && m_haveDump && m_dump.ssgOn[i];
		const int midi = keyLit ? ApproxMidiFromSsg(period) : -1;
		const BYTE fade = live ? m_fadeSsg[i] : (BYTE)0;

		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		wchar_t noise[16];
		/* 常に4文字: N031 / N--- （# 有無で N が横ずれしない） */
		if (live && m_haveDump && FmSsgNoiseOn(m_dump, i))
			_snwprintf_s(noise, _TRUNCATE, L"N%03d", FmSsgNoisePeriod(m_dump));
		else
			wcscpy_s(noise, L"N---");
		wchar_t lab[48];
		_snwprintf_s(lab, _TRUNCATE, L"%s %s %s", kSsg[i], note, noise);
		drawLabel(yy, lab, fade, RGB(100, 180, 255));

		CRect krc(x + labelW, yy + (rowH - keyH) / 2, x + labelW + pianoW, yy + (rowH - keyH) / 2 + keyH);
		DrawPiano108(dc, krc, midi, keyLit);
	}

	const int pcmN = PcmRows();
	for (int i = 0; i < pcmN; i++, row++) {
		const int yy = y + row * rowH;
		const int keyLit = live && m_haveDump && m_dump.pcmOn[i];
		const int midi = keyLit ? (int)m_dump.pcmNote[i] : -1;
		const BYTE fade = live ? m_fadePcm[i] : (BYTE)0;

		wchar_t note[16];
		FmFormatNoteName(midi, note, 16);
		wchar_t lab[40];
		_snwprintf_s(lab, _TRUNCATE, L"PCM%d %s", i + 1, note);
		drawLabel(yy, lab, fade, RGB(220, 160, 80));

		CRect krc(x + labelW, yy + (rowH - keyH) / 2, x + labelW + pianoW, yy + (rowH - keyH) / 2 + keyH);
		DrawPiano108(dc, krc, midi, keyLit);
	}

	const int rzmY = y + row * rowH + (rowH / 5);
	dc.SetTextColor(RGB(200, 210, 220));
	dc.TextOut(x, rzmY + 2, L"RHY");
	const int padW = (keyH > 10) ? (keyH + 20) : 40;
	const int padH = (keyH > 10) ? (keyH + 2) : 16;
	for (int i = 0; i < 6; i++) {
		const BYTE fade = live ? m_fadeRzmPad[i] : (BYTE)0;
		const int px = x + labelW + i * (padW + 3);
		FmFillFade(dc, px, rzmY, padW, padH,
			RGB(40, 44, 50), RGB(255, 140, 80), fade);
		dc.SetTextColor(fade > 40 ? RGB(240, 240, 245) : RGB(140, 145, 155));
		dc.TextOut(px + 4, rzmY + 1, kRzmName[i]);
	}

	dc.SelectObject(oldf);
}

void CFmMonitorDlg::ComputeLayout(int w, int h)
{
	memset(&m_lay, 0, sizeof(m_lay));
	m_lay.w = w;
	m_lay.h = h;
	m_lay.dpi = (int)FmUiDpi(GetSafeHwnd());
	const int pcmRows = PcmRows();
	m_lay.pad = FmScale(4, m_lay.dpi);
	m_lay.headH = FmScale(18, m_lay.dpi);
	m_lay.gapHexKeys = FmScale(4, m_lay.dpi);
	const int chRows = 6 + 3 + pcmRows;
	const int keyBlockRows = chRows + 1;
	m_lay.bankGap = FmScale(8, m_lay.dpi);

	int avail = h - m_lay.pad - m_lay.headH - m_lay.gapHexKeys - m_lay.pad;
	if (avail < 120) avail = 120;
	const int topShare = 18;
	const int botShare = keyBlockRows;
	m_lay.topH = avail * topShare / (topShare + botShare);
	m_lay.rowH = (avail - m_lay.topH) / keyBlockRows;
	if (m_lay.rowH < 11) m_lay.rowH = 11;
	if (m_lay.topH < 120) m_lay.topH = 120;
	{
		const int usedBot = keyBlockRows * m_lay.rowH;
		int rem = avail - m_lay.topH - usedBot;
		if (rem > 0) m_lay.topH += rem;
	}
	m_lay.keyH = (m_lay.rowH > 3) ? (m_lay.rowH - 2) : m_lay.rowH;
	m_lay.labelW = FmScale(120, m_lay.dpi);
	m_lay.topY = m_lay.pad + m_lay.headH;

	/* bankTitle = タイトル行 + col ヘッダ行。DrawHexBank と一致させる */
	const int titleLine = FmScale(14, m_lay.dpi);
	m_lay.cellH = (m_lay.topH - 2 * titleLine - m_lay.bankGap) / 34; /* 仮: 2*(title+cellH)+32*cellH */
	if (m_lay.cellH < 12) m_lay.cellH = 12;
	if (m_lay.cellH > 22) m_lay.cellH = 22;
	m_lay.bankTitle = m_lay.cellH + titleLine + 2;
	{
		const int need = 2 * (m_lay.bankTitle + 16 * m_lay.cellH) + m_lay.bankGap;
		if (need > m_lay.topH) {
			const int room = m_lay.topH - m_lay.bankGap - 2 * (titleLine + 2);
			m_lay.cellH = room / 34;
			if (m_lay.cellH < 10) m_lay.cellH = 10;
			if (m_lay.cellH > 22) m_lay.cellH = 22;
			m_lay.bankTitle = m_lay.cellH + titleLine + 2;
		}
	}
	m_lay.cellW = (std::max)(16, m_lay.cellH + 6);
	{
		HFONT measureFont = FmMakeFont((std::max)(8, m_lay.cellH - 2));
		HDC screen = ::GetDC(nullptr);
		if (screen) {
			HGDIOBJ old = ::SelectObject(screen, measureFont);
			SIZE sz = {};
			::GetTextExtentPoint32W(screen, L"00", 2, &sz);
			::SelectObject(screen, old);
			::ReleaseDC(nullptr, screen);
			m_lay.cellW = (std::max)(16, (int)sz.cx + 6);
		}
	}
	m_lay.gapExtra = (std::max)(FmScale(4, m_lay.dpi), 4);
	const int hexInnerW = m_lay.cellW + 16 * m_lay.cellW + 4 * m_lay.gapExtra;
	m_lay.hexColW = hexInnerW + FmScale(8, m_lay.dpi);
	const int minFm = FmScale(480, m_lay.dpi);
	if (m_lay.hexColW + minFm + m_lay.pad * 2 > w)
		m_lay.hexColW = (std::max)(hexInnerW, w - m_lay.pad * 2 - minFm);
	m_lay.hexX = m_lay.pad + m_lay.cellW;
	m_lay.gridY0 = m_lay.topY + m_lay.bankTitle;
	m_lay.gridY1 = m_lay.gridY0 + 16 * m_lay.cellH + m_lay.bankGap + m_lay.bankTitle;
	m_lay.fmX = m_lay.pad + m_lay.hexColW + FmScale(4, m_lay.dpi);
	m_lay.fmW = (std::max)(100, w - m_lay.pad - m_lay.fmX);
	m_lay.gap = FmScale(3, m_lay.dpi);
	m_lay.pw = (m_lay.fmW - m_lay.gap * 2) / 3;
	m_lay.ph = (m_lay.topH - m_lay.gap) / 2;
	m_lay.keysY = m_lay.topY + m_lay.topH + m_lay.gapHexKeys;
	m_lay.keysW = (std::max)(120, w - m_lay.pad * 2);

	m_lay.rcHead.SetRect(0, 0, w, m_lay.topY);
	m_lay.rcHex.SetRect(0, m_lay.topY, m_lay.fmX, m_lay.topY + m_lay.topH);
	m_lay.rcPanels.SetRect(m_lay.fmX, m_lay.topY, w, m_lay.topY + m_lay.topH);
	m_lay.rcKeys.SetRect(0, m_lay.keysY, w, h);
	m_layOk = 1;
}

void CFmMonitorDlg::DrawHead(CDC& dc)
{
	if (!m_layOk) return;
	dc.FillSolidRect(m_lay.rcHead, FM_BG);
	CGdiObject* old = dc.SelectStockObject(DEFAULT_GUI_FONT);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(230, 235, 240));
	wchar_t head[512];
	if (m_haveDump) {
		const wchar_t* stem = m_dump.sourcePath;
		for (const wchar_t* p = m_dump.sourcePath; *p; p++)
			if (*p == L'\\' || *p == L'/') stem = p + 1;
		const wchar_t* chip =
			(m_dump.padHit == 1) ? L"OPN   FM×3+SSG×3" :
			(m_dump.padHit == 0) ? L"BEEP" :
			L"OPNA  FM×6";
		_snwprintf_s(head, _TRUNCATE,
			L"%s   seq=%u  %uHz  %s%s",
			chip, m_dump.seq, m_dump.sampleRate,
			stem, m_dump.fm10 ? L"  [10ch]" : L"");
	} else {
		_snwprintf_s(head, _TRUNCATE, L"OPN/OPNA dump 待機中");
	}
	dc.TextOut(m_lay.pad, m_lay.pad, head);
	dc.SelectObject(old);
}

void CFmMonitorDlg::DrawHexArea(CDC& dc)
{
	if (!m_layOk) return;
	dc.FillSolidRect(m_lay.rcHex, FM_BG);
	DrawHexBank(dc, m_lay.hexX, m_lay.gridY0, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x000, L"Bank0");
	DrawHexBank(dc, m_lay.hexX, m_lay.gridY1, m_lay.cellW, m_lay.cellH, m_lay.gapExtra, 0x100, L"Bank1");
}

void CFmMonitorDlg::DrawPanelsArea(CDC& dc)
{
	if (!m_layOk) return;
	BYTE mask = m_panelDirtyMask;
	if (mask == 0) mask = 0x3F;
	/* 全チャンネル汚れていなければ背景全体は塗らない（ちらつき・負荷軽減） */
	if (mask == 0x3F)
		dc.FillSolidRect(m_lay.rcPanels, FM_BG);
	for (int i = 0; i < 6; i++) {
		if (!(mask & (1 << i))) continue;
		const int c = i % 3;
		const int r = i / 3;
		CRect pr(m_lay.fmX + c * (m_lay.pw + m_lay.gap), m_lay.topY + r * (m_lay.ph + m_lay.gap),
			m_lay.fmX + c * (m_lay.pw + m_lay.gap) + m_lay.pw,
			m_lay.topY + r * (m_lay.ph + m_lay.gap) + m_lay.ph);
		if (mask != 0x3F)
			dc.FillSolidRect(pr, FM_BG);
		DrawFmChPanel(dc, pr, i);
	}
	m_panelDirtyMask = 0;
}

void CFmMonitorDlg::DrawKeysArea(CDC& dc)
{
	if (!m_layOk) return;
	dc.FillSolidRect(m_lay.rcKeys, FM_BG);
	DrawChannelKeys(dc, m_lay.pad, m_lay.keysY, m_lay.keysW, m_lay.rowH, m_lay.keyH, m_lay.labelW);
}

void CFmMonitorDlg::ComposeFrame(CDC& dc, int w, int h)
{
	if (w < 80 || h < 80) {
		dc.FillSolidRect(0, 0, w, h, FM_BG);
		return;
	}
	const int needLay = !m_layOk || m_lay.w != w || m_lay.h != h || m_fullDraw;
	if (needLay)
		ComputeLayout(w, h);

	if (m_fullDraw || needLay) {
		dc.FillSolidRect(0, 0, w, h, FM_BG);
		m_panelDirtyMask = 0x3F;
		DrawHead(dc);
		DrawHexArea(dc);
		DrawPanelsArea(dc);
		DrawKeysArea(dc);
		m_fullDraw = 0;
		m_dirtyHead = m_dirtyHex = m_dirtyPanels = m_dirtyKeys = 0;
		return;
	}
	if (m_dirtyHead) { DrawHead(dc); m_dirtyHead = 0; }
	if (m_dirtyHex) { DrawHexArea(dc); m_dirtyHex = 0; }
	if (m_dirtyPanels) { DrawPanelsArea(dc); m_dirtyPanels = 0; }
	if (m_dirtyKeys) { DrawKeysArea(dc); m_dirtyKeys = 0; }
}

void CFmMonitorDlg::ApplyDump(const SasamiFmMonDump& d)
{
	const int songChanged = (m_lastSong[0] == 0)
		|| (wcscmp(m_lastSong, d.sourcePath) != 0)
		|| (m_haveDump && d.curSample + 10000 < m_lastCurSample);
	if (songChanged) {
		memset(m_touched, 0, sizeof(m_touched));
		memset(m_fadeKey, 0, sizeof(m_fadeKey));
		memset(m_fadeSsg, 0, sizeof(m_fadeSsg));
		memset(m_fadePcm, 0, sizeof(m_fadePcm));
		memset(m_fadeRzmPad, 0, sizeof(m_fadeRzmPad));
		wcsncpy_s(m_lastSong, d.sourcePath, _TRUNCATE);
		m_dirtyHead = 1;
		m_dirtyKeys = 1;
		m_panelDirtyMask = 0x3F;
		m_dirtyPanels = 1;
	}

	int chgHex = 0, chgKeys = 0;
	BYTE panelMask = 0;
	if (m_haveDump) {
		for (int i = 0; i < 0x200; i++) {
			if (d.regs[i] != m_dump.regs[i]) {
				FmBump(m_fade[i]);
				m_touched[i] = 1;
				chgHex = 1;
			}
			if (d.regs[i] != 0)
				m_touched[i] = 1;
		}
		/* チャンネル単位: ALG(B0) / AMS·PMS·PAN(B4) / Fnum / オペレータ / keyOn */
		for (int ch = 0; ch < 6; ch++) {
			const int bank = (ch < 3) ? 0 : 0x100;
			const int slot = (ch < 3) ? ch : (ch - 3);
			int dirty = 0;
			if (d.keyOnFm[ch] != m_dump.keyOnFm[ch]) {
				dirty = 1;
				chgKeys = 1;
			}
			if (d.keyOnFm[ch] && !m_dump.keyOnFm[ch])
				FmBump(m_fadeKey[ch]);
			/* v4: key-on 書き込み累積（同一状態の再トリガも拾う） */
			if (d.version >= 4 && d.keyOnHitCnt[ch] != m_dump.keyOnHitCnt[ch]) {
				FmBump(m_fadeKey[ch]);
				dirty = 1;
				chgKeys = 1;
			}
			const uint8_t b0 = d.regs[bank + 0xB0 + slot];
			const uint8_t pb0 = m_dump.regs[bank + 0xB0 + slot];
			const uint8_t b4 = d.regs[bank + 0xB4 + slot];
			const uint8_t pb4 = m_dump.regs[bank + 0xB4 + slot];
			if (b0 != pb0 || b4 != pb4)
				dirty = 1; /* ALG 図・FB・AMS/PMS/PAN */
			if (d.regs[bank + 0xA4 + slot] != m_dump.regs[bank + 0xA4 + slot]
				|| d.regs[bank + 0xA0 + slot] != m_dump.regs[bank + 0xA0 + slot]) {
				FmBump(m_fadeKey[ch]);
				chgKeys = 1;
				dirty = 1;
			}
			for (int op = 0; op < 4 && !dirty; op++) {
				const int o = op * 4 + slot;
				if (d.regs[bank + 0x30 + o] != m_dump.regs[bank + 0x30 + o]
					|| d.regs[bank + 0x40 + o] != m_dump.regs[bank + 0x40 + o]
					|| d.regs[bank + 0x50 + o] != m_dump.regs[bank + 0x50 + o]
					|| d.regs[bank + 0x60 + o] != m_dump.regs[bank + 0x60 + o]
					|| d.regs[bank + 0x70 + o] != m_dump.regs[bank + 0x70 + o]
					|| d.regs[bank + 0x80 + o] != m_dump.regs[bank + 0x80 + o]
					|| d.regs[bank + 0x90 + o] != m_dump.regs[bank + 0x90 + o])
					dirty = 1;
			}
			if (dirty)
				panelMask = (BYTE)(panelMask | (1 << ch));
		}
		/* 部分パネル描画の取り残しで CH ごとに「合ってる／ずれてる」に見えるのを防ぐ */
		if (panelMask != 0)
			panelMask = 0x3F;
		for (int i = 0; i < 3; i++) {
			if (d.ssgOn[i] != m_dump.ssgOn[i])
				chgKeys = 1;
			if (d.ssgOn[i] && !m_dump.ssgOn[i])
				FmBump(m_fadeSsg[i]);
			if (d.version >= 4 && d.ssgHitCnt[i] != m_dump.ssgHitCnt[i]) {
				FmBump(m_fadeSsg[i]);
				chgKeys = 1;
			}
			if (d.regs[i * 2] != m_dump.regs[i * 2]
				|| d.regs[i * 2 + 1] != m_dump.regs[i * 2 + 1]) {
				FmBump(m_fadeSsg[i]);
				chgKeys = 1;
			}
		}
		const int pcmN = (d.pcmCount < SASAMI_FMMON_PCM_MAX) ? d.pcmCount : SASAMI_FMMON_PCM_MAX;
		if (d.pcmCount != m_dump.pcmCount) {
			chgKeys = 1;
			m_fullDraw = 1;
		}
		for (int i = 0; i < pcmN; i++) {
			if (d.pcmOn[i] && !m_dump.pcmOn[i])
				FmBump(m_fadePcm[i]);
			else if (d.pcmOn[i] && d.pcmNote[i] != m_dump.pcmNote[i])
				FmBump(m_fadePcm[i]);
			if (d.pcmOn[i] != m_dump.pcmOn[i] || d.pcmNote[i] != m_dump.pcmNote[i])
				chgKeys = 1;
		}
		for (int i = 0; i < 6; i++) {
			const int was = (m_dump.rhythmKey >> i) & 1;
			const int nowR = (d.rhythmKey >> i) & 1;
			if (nowR != was) chgKeys = 1;
			if (nowR && !was)
				FmBump(m_fadeRzmPad[i]);
		}
		/* v3: 累積ヒット差分 — ファイル上書きで pulse を見失っても拾える */
		int hitBump = 0;
		if (d.version >= 3) {
			for (int i = 0; i < 6; i++) {
				if (d.rhythmHitCnt[i] != m_dump.rhythmHitCnt[i]) {
					FmBump(m_fadeRzmPad[i]);
					hitBump = 1;
				}
			}
		}
		/* 同一 0x10 値の連打 / 区間 pulse */
		if (hitBump || (d.rhythmPulse & 0x3F)) {
			FmBump(m_fade[0x10]);
			m_touched[0x10] = 1;
			chgHex = 1;
			chgKeys = 1;
			if (!hitBump) {
				for (int i = 0; i < 6; i++) {
					if (d.rhythmPulse & (1 << i))
						FmBump(m_fadeRzmPad[i]);
				}
			}
		} else if (d.regs[0x10] != m_dump.regs[0x10] && !(d.regs[0x10] & 0x80)) {
			for (int i = 0; i < 6; i++) {
				if (d.regs[0x10] & (1 << i))
					FmBump(m_fadeRzmPad[i]);
			}
			chgKeys = 1;
			chgHex = 1;
		}
		if (d.seq != m_dump.seq || d.sampleRate != m_dump.sampleRate)
			m_dirtyHead = 1;
	} else {
		/* 初回 dump: hex は触るが、停止中／開始直後のキー点灯は出さない */
		memset(m_fade, 0, sizeof(m_fade));
		memset(m_fadeKey, 0, sizeof(m_fadeKey));
		memset(m_fadeSsg, 0, sizeof(m_fadeSsg));
		memset(m_fadePcm, 0, sizeof(m_fadePcm));
		memset(m_fadeRzmPad, 0, sizeof(m_fadeRzmPad));
		for (int i = 0; i < 0x200; i++) {
			if (d.regs[i] != 0)
				m_touched[i] = 1;
		}
		if (FmMonIsLive()) {
			for (int i = 0; i < 6; i++)
				if (d.keyOnFm[i]) FmBump(m_fadeKey[i]);
			for (int i = 0; i < 3; i++)
				if (d.ssgOn[i]) FmBump(m_fadeSsg[i]);
			for (int i = 0; i < 6; i++)
				if ((d.rhythmKey >> i) & 1) FmBump(m_fadeRzmPad[i]);
			if (d.rhythmPulse & 0x3F) {
				FmBump(m_fade[0x10]);
				for (int i = 0; i < 6; i++) {
					if (d.rhythmPulse & (1 << i))
						FmBump(m_fadeRzmPad[i]);
				}
			}
			const int pcmN = (d.pcmCount < SASAMI_FMMON_PCM_MAX) ? d.pcmCount : SASAMI_FMMON_PCM_MAX;
			for (int i = 0; i < pcmN; i++)
				if (d.pcmOn[i]) FmBump(m_fadePcm[i]);
		}
		chgHex = chgKeys = 1;
		panelMask = 0x3F;
		m_dirtyHead = 1;
	}

	m_prev = m_dump;
	m_dump = d;
	m_lastSeq = d.seq;
	m_lastCurSample = d.curSample;
	m_haveDump = 1;
	if (chgHex) m_dirtyHex = 1;
	if (panelMask) {
		m_panelDirtyMask = (BYTE)(m_panelDirtyMask | panelMask);
		m_dirtyPanels = 1;
	}
	if (chgKeys) m_dirtyKeys = 1;
}

void CFmMonitorDlg::PushHistDump(const SasamiFmMonDump& d)
{
	if (m_histN > 0) {
		const int lastI = (m_histHead + m_histN - 1) % HIST_MAX;
		if (d.seq == m_hist[lastI].seq && d.curSample == m_hist[lastI].curSample
			&& d.rhythmPulse == m_hist[lastI].rhythmPulse
			&& (d.version < 3 || memcmp(d.rhythmHitCnt, m_hist[lastI].rhythmHitCnt, 6) == 0)
			&& (d.version < 4 || (memcmp(d.keyOnHitCnt, m_hist[lastI].keyOnHitCnt, 6) == 0
				&& memcmp(d.ssgHitCnt, m_hist[lastI].ssgHitCnt, 3) == 0)))
			return;
	}
	if (m_histN < HIST_MAX) {
		const int i = (m_histHead + m_histN) % HIST_MAX;
		m_hist[i] = d;
		m_histSamp[i] = d.curSample;
		m_histN++;
	} else {
		m_hist[m_histHead] = d;
		m_histSamp[m_histHead] = d.curSample;
		m_histHead = (m_histHead + 1) % HIST_MAX;
	}
}

int CFmMonitorDlg::PollDump()
{
	struct Cb { CFmMonitorDlg* self; int got; } cb = { this, 0 };
	auto thunk = [](const SasamiFmMonDump& d, void* p) {
		Cb* c = (Cb*)p;
		c->self->PushHistDump(d);
		c->got = 1;
	};

	int got = 0;
	if (FmDrainRingSlots(&m_ringGenLast, thunk, &cb)) {
		m_readFail = 0;
		got = (cb.got || m_histN > 0) ? 1 : 0;
	} else {
		SasamiFmMonDump d;
		if (!FmReadDump(&d)) {
			if (m_haveDump) {
				if (++m_readFail > 45) {
					m_haveDump = 0;
					m_histN = 0;
					m_histHead = 0;
					m_ringGenLast = 0;
					m_readFail = 0;
					m_fullDraw = 1;
					m_panelDirtyMask = 0x3F;
					m_dirtyHead = m_dirtyHex = m_dirtyPanels = m_dirtyKeys = 1;
				}
			}
			return 0;
		}
		m_readFail = 0;
		got = 1;
		PushHistDump(d);
	}
	if (!got || m_histN <= 0) return 0;

	uint32_t rate = 44100;
	{
		const int li = (m_histHead + m_histN - 1) % HIST_MAX;
		if (m_hist[li].sampleRate > 0)
			rate = m_hist[li].sampleRate;
	}
	const uint64_t heard = HeardSample(rate);

	/* dump.curSample = その tick の PCM 開始位置 / heard = 可聴位置 */
	int bestN = -1;
	for (int n = 0; n < m_histN; n++) {
		const int i = (m_histHead + n) % HIST_MAX;
		if (m_histSamp[i] <= heard)
			bestN = n;
	}
	if (bestN < 0)
		return 1; /* 全部がまだ先 → 音に合わせて待つ */

	int curN = -1;
	for (int n = 0; n < m_histN; n++) {
		const int i = (m_histHead + n) % HIST_MAX;
		if (m_haveDump
			&& m_hist[i].seq == m_lastSeq
			&& m_hist[i].curSample == m_lastCurSample) {
			curN = n;
			break;
		}
	}

	int nextN = bestN;
	int fromN;
	if (curN >= 0 && bestN < curN) {
		fromN = bestN;
		nextN = bestN;
	} else {
		fromN = (curN < 0) ? nextN : (curN + 1);
	}
	if (fromN > nextN) return 1;

	int applied = 0;
	for (int n = fromN; n <= nextN; n++) {
		const SasamiFmMonDump& show = m_hist[(m_histHead + n) % HIST_MAX];
		if (m_haveDump && show.seq == m_lastSeq
			&& show.rhythmKey == m_dump.rhythmKey
			&& show.rhythmPulse == m_dump.rhythmPulse
			&& show.pcmCount == m_dump.pcmCount
			&& (show.version < 3 || memcmp(show.rhythmHitCnt, m_dump.rhythmHitCnt, 6) == 0)
			&& (show.version < 4 || (memcmp(show.keyOnHitCnt, m_dump.keyOnHitCnt, 6) == 0
				&& memcmp(show.ssgHitCnt, m_dump.ssgHitCnt, 3) == 0))
			&& memcmp(show.keyOnFm, m_dump.keyOnFm, sizeof(show.keyOnFm)) == 0
			&& memcmp(show.ssgOn, m_dump.ssgOn, sizeof(show.ssgOn)) == 0
			&& memcmp(show.pcmOn, m_dump.pcmOn, sizeof(show.pcmOn)) == 0
			&& memcmp(show.pcmNote, m_dump.pcmNote, sizeof(show.pcmNote)) == 0
			&& memcmp(show.regs, m_dump.regs, sizeof(show.regs)) == 0) {
			m_lastCurSample = show.curSample;
			continue;
		}
		ApplyDump(show);
		applied = 1;
	}

	if (bestN > 0 && bestN < m_histN) {
		m_histHead = (m_histHead + bestN) % HIST_MAX;
		m_histN -= bestN;
	}
	return applied || m_haveDump;
}

void CFmMonitorDlg::TickFades()
{
	/* フェードは hex/鍵盤のみ。パネルはフェードを使わないので触らない */
	auto tickQ = [](BYTE& g) -> int {
		if (!g) return 0;
		const BYTE before = (BYTE)(g >> 4);
		FmTickGlow(g);
		const BYTE after = (BYTE)(g >> 4);
		return before != after || g == 0;
	};
	int hex = 0, keys = 0;
	for (int i = 0; i < 0x200; i++)
		if (tickQ(m_fade[i])) hex = 1;
	for (int i = 0; i < 6; i++)
		if (tickQ(m_fadeKey[i])) keys = 1;
	for (int i = 0; i < 3; i++)
		if (tickQ(m_fadeSsg[i])) keys = 1;
	for (int i = 0; i < SASAMI_FMMON_PCM_MAX; i++)
		if (tickQ(m_fadePcm[i])) keys = 1;
	for (int i = 0; i < 6; i++)
		if (tickQ(m_fadeRzmPad[i])) keys = 1;
	if (hex) m_dirtyHex = 1;
	if (keys) m_dirtyKeys = 1;
}

void CFmMonitorDlg::InvalidateDirtyRegions()
{
	if (!(m_fullDraw || m_dirtyHead || m_dirtyHex || m_dirtyPanels || m_dirtyKeys))
		return;
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (m_fullDraw || !m_layOk) {
		Invalidate(FALSE);
		return;
	}
	CRect acc(0, 0, 0, 0);
	auto add = [&](const CRect& r) {
		CRect c = r;
		c.OffsetRect(0, capH);
		if (acc.IsRectEmpty()) acc = c;
		else acc.UnionRect(&acc, &c);
	};
	if (m_dirtyHead) add(m_lay.rcHead);
	if (m_dirtyHex) add(m_lay.rcHex);
	if (m_dirtyPanels) add(m_lay.rcPanels);
	if (m_dirtyKeys) add(m_lay.rcKeys);
	if (!acc.IsRectEmpty())
		InvalidateRect(&acc, FALSE);
	else
		Invalidate(FALSE);
}

void CFmMonitorDlg::PumpSyncNow()
{
	if (!::IsWindow(GetSafeHwnd()) || !IsWindowVisible() || IsIconic())
		return;
	const int live = FmMonIsLive();
	if (m_lastPlayy != 0 && live == 0) {
		memset(m_fadeKey, 0, sizeof(m_fadeKey));
		memset(m_fadeSsg, 0, sizeof(m_fadeSsg));
		memset(m_fadePcm, 0, sizeof(m_fadePcm));
		memset(m_fadeRzmPad, 0, sizeof(m_fadeRzmPad));
		/* 表示上の gate も落とす（描画側でも live 判定するが残骸を残さない） */
		memset(m_dump.keyOnFm, 0, sizeof(m_dump.keyOnFm));
		memset(m_dump.ssgOn, 0, sizeof(m_dump.ssgOn));
		memset(m_dump.pcmOn, 0, sizeof(m_dump.pcmOn));
		m_dump.rhythmKey = 0;
		m_dump.rhythmPulse = 0;
		m_dirtyKeys = 1;
		m_panelDirtyMask = 0x3F;
		m_dirtyPanels = 1;
		m_fullDraw = 1;
	}
	m_lastPlayy = live;
	if (live)
		PollDump();
	TickFades();
	InvalidateDirtyRegions();
}

void CFmMonitorDlg::IdlePulse()
{
	if (!::IsWindow(GetSafeHwnd()) || !IsWindowVisible() || IsIconic())
		return;
	const ULONGLONG now = GetTickCount64();
	if (now - m_lastPollMs < 8)
		return;
	m_lastPollMs = now;
	PumpSyncNow();
	CRect ur;
	if (GetUpdateRect(&ur, FALSE))
		UpdateWindow();
}

void CFmMonitorDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rect;
	GetClientRect(&rect);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int w = rect.Width();
	const int h = rect.Height() - capH;
	if (w <= 0 || h <= 0) {
		CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}

	if (!EnsureFrameBuffer(dc, w, h) || !m_frameDC.GetSafeHdc()) {
		dc.FillSolidRect(0, capH, w, h, FM_BG);
		CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}

	ComposeFrame(m_frameDC, w, h);

	CRect pr = dc.m_ps.rcPaint;
	if (pr.IsRectEmpty())
		pr.SetRect(0, capH, w, capH + h);
	const int paintCap = (pr.top < capH) ? 1 : 0;

#if CCUSTOM_AERO_SUPPORT
	const bool needOpaque = CCC_IsWin11()
		&& (savedata.aero == 1 || CCC_AcrylicCaption(m_hWnd));
	if (needOpaque) {
		if (m_chromaW != w || m_chromaH != h) {
			m_chromaCache.Release();
			m_chromaReady = false;
			m_chromaW = w;
			m_chromaH = h;
		}
		if (m_chromaCache.Ensure(dc.GetSafeHdc(), w, h)) {
			m_chromaCache.UpdateOpaqueRect(m_frameDC.GetSafeHdc(), 0, 0, 0, 0, w, h);
			m_chromaReady = true;
			m_chromaCache.BlitFull(dc.GetSafeHdc(), 0, capH, w, h);
			if (paintCap)
				CCC_CaptionPaintGdi(dc, m_hWnd);
			return;
		}
		CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, capH, w, h,
			m_frameDC.GetSafeHdc(), 0, 0, w, h);
		if (paintCap)
			CCC_CaptionPaintGdi(dc, m_hWnd);
		return;
	}
#endif
	int sx = pr.left;
	int sy = pr.top - capH;
	int sw = pr.Width();
	int sh = pr.Height();
	if (sy < 0) { sh += sy; sy = 0; }
	if (sx < 0) { sw += sx; sx = 0; }
	if (sx + sw > w) sw = w - sx;
	if (sy + sh > h) sh = h - sy;
	if (sw > 0 && sh > 0)
		dc.BitBlt(sx, capH + sy, sw, sh, &m_frameDC, sx, sy, SRCCOPY);
	if (paintCap)
		CCC_CaptionPaintGdi(dc, m_hWnd);
}
