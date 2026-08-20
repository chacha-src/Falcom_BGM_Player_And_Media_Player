#include "stdafx.h"
#include "CMidiMonitorDlg.h"
#include "oggDlg.h"
#include "CMediaPlayerDlg.h"
#include "CEqualizer.h"
#include "resource.h"
#include "VstMidiEngine.h"
#include "PluginKinds.h"
#include <math.h>
#include <mmsystem.h>

class COggDlg;
extern COggDlg* og;
extern save savedata;
extern CString filen;
extern int mode;
extern int tempo;
extern int pitch;
extern __int64 playb;

namespace {

enum {
	IDM_MM_VIEW_2D = 42450,
	IDM_MM_VIEW_3D = 42451,
	IDM_MM_CAM_RESET = 42452,
	IDM_MM_FREEZE = 42453,
	IDM_MM_TOPMOST = 42454,
	IDM_MM_COPY = 42455,
	IDM_MM_RELOAD = 42456,
	IDM_MM_MAP_AUTO = 42457,
	IDM_MM_MAP_GS = 42458,
	IDM_MM_MAP_XG = 42459
};

static constexpr COLORREF MM_BG = RGB(8, 8, 12);
static constexpr COLORREF MM_HEAD_BG = RGB(196, 196, 200);
static constexpr COLORREF MM_CHROMA = RGB(8, 8, 12);
static constexpr COLORREF MM_GRID = RGB(36, 36, 44);
static constexpr COLORREF MM_FG = RGB(230, 230, 236);
static constexpr COLORREF MM_HEAD_TX = RGB(28, 28, 36);
static constexpr COLORREF MM_ROW_A = RGB(14, 16, 22);
static constexpr COLORREF MM_ROW_B = RGB(10, 12, 18);

enum {
	MM_HIT_NONE = 0,
	MM_HIT_APPVOL,
	MM_HIT_VOL,
	MM_HIT_PAN,
	MM_HIT_EXP,
	MM_HIT_REV,
	MM_HIT_CRS,
	MM_HIT_VAR,
	MM_HIT_KEYS,
	MM_HIT_PC
};

enum {
	MM_LATCH_VOL = 1,
	MM_LATCH_PAN = 2,
	MM_LATCH_EXP = 4,
	MM_LATCH_REV = 8,
	MM_LATCH_CRS = 16,
	MM_LATCH_VAR = 32,
	MM_LATCH_PC = 64
};

static int MmCcForHit(int kind)
{
	if (kind == MM_HIT_VOL) return 7;
	if (kind == MM_HIT_PAN) return 10;
	if (kind == MM_HIT_EXP) return 11;
	if (kind == MM_HIT_REV) return 91;
	if (kind == MM_HIT_CRS) return 93;
	if (kind == MM_HIT_VAR) return 94;
	return -1;
}

static int MmDefaultForHit(int kind)
{
	if (kind == MM_HIT_VOL) return 100;
	if (kind == MM_HIT_PAN) return 64;
	if (kind == MM_HIT_EXP) return 127;
	if (kind == MM_HIT_REV) return 40;
	return 0;
}

static int MmIsBlackKey(int n)
{
	const int m = n % 12;
	return (m == 1 || m == 3 || m == 6 || m == 8 || m == 10) ? 1 : 0;
}

static COLORREF MmMix(COLORREF a, COLORREF b, int t)
{
	if (t <= 0) return a;
	if (t >= 256) return b;
	const int ar = GetRValue(a), ag = GetGValue(a), ab = GetBValue(a);
	const int br = GetRValue(b), bg = GetGValue(b), bb = GetBValue(b);
	return RGB(ar + (br - ar) * t / 256, ag + (bg - ag) * t / 256, ab + (bb - ab) * t / 256);
}

static void MmGlowTick(BYTE& g)
{
	if (!g) return;
	g = (BYTE)((int)g * 7 / 8);
	if (g < 6) g = 0;
}

static const wchar_t* kGmName[128] = {
	L"Acoustic Grand", L"Bright Piano", L"Electric Grand", L"Honky-tonk",
	L"E.Piano 1", L"E.Piano 2", L"Harpsichord", L"Clavi",
	L"Celesta", L"Glockenspiel", L"Music Box", L"Vibraphone",
	L"Marimba", L"Xylophone", L"Tubular Bells", L"Dulcimer",
	L"Drawbar Organ", L"Percussive Organ", L"Rock Organ", L"Church Organ",
	L"Reed Organ", L"Accordion", L"Harmonica", L"Tango Accordion",
	L"Nylon Guitar", L"Steel Guitar", L"Jazz Guitar", L"Clean Guitar",
	L"Muted Guitar", L"Overdrive Gt", L"Distortion Gt", L"Gt Harmonics",
	L"Acoustic Bass", L"Finger Bass", L"Picked Bass", L"Fretless Bass",
	L"Slap Bass 1", L"Slap Bass 2", L"Synth Bass 1", L"Synth Bass 2",
	L"Violin", L"Viola", L"Cello", L"Contrabass",
	L"Tremolo Str", L"Pizzicato Str", L"Harp", L"Timpani",
	L"Strings", L"Slow Strings", L"Syn.Strings1", L"Syn.Strings2",
	L"Choir Aahs", L"Voice Oohs", L"SynVox", L"Orchestra Hit",
	L"Trumpet", L"Trombone", L"Tuba", L"Muted Trumpet",
	L"French Horns", L"Brass 1", L"Synth Brass1", L"Synth Brass2",
	L"Soprano Sax", L"Alto Sax", L"Tenor Sax", L"Baritone Sax",
	L"Oboe", L"EnglishHorn", L"Bassoon", L"Clarinet",
	L"Piccolo", L"Flute", L"Recorder", L"Pan Flute",
	L"Blown Bottle", L"Shakuhachi", L"Whistle", L"Ocarina",
	L"Square Lead", L"Saw Lead", L"Calliope", L"Chiff Lead",
	L"Charang", L"Voice Lead", L"Fifths Lead", L"Bass & Lead",
	L"New Age Pad", L"Warm Pad", L"Polysynth", L"Choir Pad",
	L"Bowed Glass", L"Metallic Pad", L"Halo Pad", L"Sweep Pad",
	L"Rain", L"Soundtrack", L"Crystal", L"Atmosphere",
	L"Brightness", L"Goblins", L"Echoes", L"Sci-Fi",
	L"Sitar", L"Banjo", L"Shamisen", L"Koto",
	L"Kalimba", L"Bagpipe", L"Fiddle", L"Shanai",
	L"Tinkle Bell", L"Agogo", L"Steel Drums", L"Woodblock",
	L"Taiko Drum", L"Melodic Tom", L"Synth Drum", L"Reverse Cymbal",
	L"Gt Fret Noise", L"Breath Noise", L"Seashore", L"Bird Tweet",
	L"Telephone", L"Helicopter", L"Applause", L"Gunshot"
};

static const wchar_t* kXgRev[] = {
	L"No Effect", L"Hall 1", L"Hall 2", L"Hall 3", L"Hall 4",
	L"Hall 5", L"Hall M", L"Hall L", L"Room 1", L"Room 2",
	L"Room 3", L"Room 4", L"Room 5", L"Room 6", L"Room 7",
	L"Stage 1", L"Stage 2", L"Plate 1", L"Plate 2", L"Delay",
	L"Panning Delay", L"White Room", L"Tunnel", L"Canyon", L"Basement"
};
static const wchar_t* kXgCho[] = {
	L"No Effect", L"Chorus 1", L"Chorus 2", L"Chorus 3", L"Chorus 4",
	L"FB Chorus", L"Flanger 1", L"Celeste 1", L"Celeste 2", L"Celeste 3",
	L"Flanger 2", L"Symphonic", L"Phaser"
};
static const wchar_t* kXgVar[] = {
	L"No Effect", L"Delay 1", L"Delay 2", L"Delay LCR", L"Echo",
	L"Cross Delay", L"ER 1", L"ER 2", L"Gate Reverb", L"Reverse Gate",
	L"Karaoke 1", L"Karaoke 2", L"Karaoke 3"
};
static const wchar_t* kGsRev[] = {
	L"Room 1", L"Room 2", L"Room 3", L"Hall 1", L"Hall 2",
	L"Plate", L"Delay", L"Panning Delay"
};
static const wchar_t* kGsCho[] = {
	L"Chorus 1", L"Chorus 2", L"Chorus 3", L"Chorus 4",
	L"Feedback Chorus", L"Flanger", L"Short Delay", L"Short Delay FB"
};

static BYTE* s_gsDat = NULL;
static int s_gsBytes = 0;
static BYTE* s_xgDat = NULL;
static int s_xgBytes = 0;
static int s_datTried = 0;

static void MmCopyW(wchar_t* dst, int n, const wchar_t* src)
{
	if (!dst || n <= 0) return;
	if (!src) src = L"";
	wcsncpy_s(dst, n, src, _TRUNCATE);
}

static UINT MmReadBE(const BYTE* p, int n)
{
	UINT v = 0;
	for (int i = 0; i < n; ++i) v = (v << 8) | p[i];
	return v;
}

static BOOL MmReadVar(const BYTE*& q, const BYTE* end, unsigned& out)
{
	out = 0;
	for (int i = 0; i < 4; ++i) {
		if (q >= end) return FALSE;
		BYTE b = *q++;
		out = (out << 7) | (b & 0x7f);
		if (!(b & 0x80)) return TRUE;
	}
	return FALSE;
}

static int MmCmpEv(const void* aa, const void* bb)
{
	const CMidiMonitorDlg::MmEv* a = (const CMidiMonitorDlg::MmEv*)aa;
	const CMidiMonitorDlg::MmEv* b = (const CMidiMonitorDlg::MmEv*)bb;
	if (a->tick < b->tick) return -1;
	if (a->tick > b->tick) return 1;
	const int ca = (int)(a->msg & 15), cb = (int)(b->msg & 15);
	if (ca < cb) return -1;
	if (ca > cb) return 1;
	return 0;
}

static BOOL MmLoadResDat(int id, BYTE** out, int* outN)
{
	*out = NULL;
	*outN = 0;
	HINSTANCE hi = AfxGetResourceHandle();
	HRSRC hr = FindResource(hi, MAKEINTRESOURCE(id), RT_RCDATA);
	if (!hr) return FALSE;
	HGLOBAL hg = LoadResource(hi, hr);
	if (!hg) return FALSE;
	DWORD sz = SizeofResource(hi, hr);
	const BYTE* p = (const BYTE*)LockResource(hg);
	if (!p || sz < 20) return FALSE;
	BYTE* buf = new BYTE[sz];
	memcpy(buf, p, sz);
	*out = buf;
	*outN = (int)sz;
	return TRUE;
}

static BOOL MmLoadFileDat(const wchar_t* path, BYTE** out, int* outN)
{
	*out = NULL;
	*outN = 0;
	HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (f == INVALID_HANDLE_VALUE) return FALSE;
	DWORD sz = GetFileSize(f, NULL), got = 0;
	if (sz < 20 || sz > 8 * 1024 * 1024) { CloseHandle(f); return FALSE; }
	BYTE* buf = new BYTE[sz];
	if (!ReadFile(f, buf, sz, &got, NULL) || got != sz) {
		CloseHandle(f); delete[] buf; return FALSE;
	}
	CloseHandle(f);
	*out = buf;
	*outN = (int)sz;
	return TRUE;
}

static void MmEnsureDat()
{
	if (s_datTried) return;
	s_datTried = 1;
	if (!MmLoadResDat(IDR_SASAMI_GS, &s_gsDat, &s_gsBytes))
		MmLoadFileDat(L"C:\\Windows\\SASAMI_GS.DAT", &s_gsDat, &s_gsBytes);
	if (!MmLoadResDat(IDR_SASAMI_XG, &s_xgDat, &s_xgBytes))
		MmLoadFileDat(L"C:\\Windows\\SASAMI_XG.DAT", &s_xgDat, &s_xgBytes);
}

static void MmSjisToW(const char* src, int srcN, wchar_t* dst, int dstN)
{
	if (!dst || dstN <= 0) return;
	dst[0] = 0;
	if (!src || srcN <= 0) return;
	char tmp[32];
	int n = srcN;
	if (n > 31) n = 31;
	memcpy(tmp, src, n);
	tmp[n] = 0;
	MultiByteToWideChar(932, 0, tmp, -1, dst, dstN);
	dst[dstN - 1] = 0;
}

static BOOL MmLookupGs(int mapId, int bank, int pc, wchar_t* out, int outN)
{
	if (!s_gsDat || s_gsBytes < 20) return FALSE;
	const int rec = s_gsBytes / 20;
	for (int pass = 0; pass < 3; ++pass) {
		int wantMap = mapId, wantBank = bank, wantPc = pc;
		if (pass == 1) { wantMap = 4; wantBank = bank; }
		if (pass == 2) { wantMap = 4; wantBank = 0; }
		for (int i = 0; i < rec; ++i) {
			const BYTE* r = s_gsDat + i * 20;
			if (r[0] == (BYTE)wantMap && r[1] == (BYTE)wantBank && r[2] == (BYTE)wantPc) {
				MmSjisToW((const char*)(r + 3), 17, out, outN);
				return out[0] != 0;
			}
		}
	}
	return FALSE;
}

static BOOL MmLookupXgOk(int msb, int lsb, int pc, wchar_t* out, int outN)
{
	if (!s_xgDat || s_xgBytes < 20) return FALSE;
	const int rec = s_xgBytes / 20;
	for (int pass = 0; pass < 2; ++pass) {
		const BYTE wantM = (BYTE)((pass == 0) ? msb : 0);
		const BYTE wantL = (BYTE)((pass == 0) ? lsb : 0);
		const BYTE wantP = (BYTE)pc;
		for (int i = 0; i < rec; ++i) {
			const BYTE* r = s_xgDat + i * 20;
			if (r[0] == wantM && r[1] == wantL && r[2] == wantP) {
				MmSjisToW((const char*)(r + 3), 17, out, outN);
				return out[0] != 0;
			}
		}
	}
	return FALSE;
}

static const wchar_t* MmEffName(int mode, int kind, int type)
{
	if (mode == 2) {
		if (kind == 0) {
			if (type >= 0 && type < (int)(sizeof(kXgRev) / sizeof(kXgRev[0]))) return kXgRev[type];
			return L"Reverb";
		}
		if (kind == 1) {
			if (type >= 0 && type < (int)(sizeof(kXgCho) / sizeof(kXgCho[0]))) return kXgCho[type];
			return L"Chorus";
		}
		if (type >= 0 && type < (int)(sizeof(kXgVar) / sizeof(kXgVar[0]))) return kXgVar[type];
		return L"Variation";
	}
	if (kind == 0) {
		if (type >= 0 && type < (int)(sizeof(kGsRev) / sizeof(kGsRev[0]))) return kGsRev[type];
		return L"Room 1";
	}
	if (kind == 1) {
		if (type >= 0 && type < (int)(sizeof(kGsCho) / sizeof(kGsCho[0]))) return kGsCho[type];
		return L"Chorus 1";
	}
	return L"OFF";
}

static const wchar_t* MmMapLabel(int sysMode, int mapId)
{
	if (sysMode == 2) return L"XGmap";
	if (mapId == 1) return L"55map";
	if (mapId == 3) return L"88Pmap";
	if (mapId == 0) return L"GSmap";
	return L"88map";
}

static void MmKeyName(int sf, int mi, wchar_t* out, int n)
{
	static const wchar_t* maj[] = { L"Cb", L"Gb", L"Db", L"Ab", L"Eb", L"Bb", L"F", L"C", L"G", L"D", L"A", L"E", L"B", L"F#", L"C#" };
	static const wchar_t* minn[] = { L"Ab", L"Eb", L"Bb", L"F", L"C", L"G", L"D", L"A", L"E", L"B", L"F#", L"C#", L"G#", L"D#", L"A#" };
	int i = sf + 7;
	if (i < 0) i = 0;
	if (i > 14) i = 14;
	if (mi)
		_snwprintf_s(out, n, _TRUNCATE, L"%s minor", minn[i]);
	else
		_snwprintf_s(out, n, _TRUNCATE, L"%s Major", maj[i]);
}

class CMmHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_MM_HELP };
	explicit CMmHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
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

static CMmHelpDlg* g_mmHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CMmHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CMmHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"MIDIモニタ操作ガイド", L"MIDI Monitor Guide", L"Guide moniteur MIDI", L"Guida monitor MIDI",
		L"Guia de monitor MIDI", L"MIDI 모니터 가이드", L"MIDI监视器指南", L"دليل مراقب MIDI",
		L"Руководство MIDI-монитора", L"MIDI-Monitor-Anleitung", L"Guia do monitor MIDI", L"MIDI-monitorhandleiding",
		L"Przewodnik monitora MIDI", L"MIDI izleyici kilavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}
void CMmHelpDlg::OnOK() { DestroyWindow(); }
void CMmHelpDlg::OnCancel() { DestroyWindow(); }
void CMmHelpDlg::OnClose() { DestroyWindow(); }
void CMmHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_mmHelpDlg == this) g_mmHelpDlg = nullptr;
	delete this;
}
BOOL CMmHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}
void CMmHelpDlg::OnPaint()
{
	CPaintDC pdc(this);
	CCC_GdiHelpPaint hp;
	if (!CCC_GdiHelpBeginPaint(this, pdc, hp))
		return;
	CDC& dc = hp.mem;
	CRect rc = hp.rc;
	dc.SetBkMode(TRANSPARENT);
	CFont* baseFont = GetFont();
	CFont boldFont;
	{
		LOGFONT lf = {};
		if (baseFont && baseFont->GetSafeHandle())
			baseFont->GetLogFont(&lf);
		else {
			NONCLIENTMETRICS ncm = {};
			ncm.cbSize = sizeof(ncm);
			::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
			lf = ncm.lfMessageFont;
		}
		lf.lfWeight = FW_BOLD;
		boldFont.CreateFontIndirect(&lf);
	}
	CFont* oldFont = dc.SelectObject(baseFont);
	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 2;
	auto title = [&](int x, int y, LPCTSTR t) {
		CFont* prev = dc.SelectObject(&boldFont);
		dc.SetTextColor(RGB(72, 48, 120));
		dc.TextOut(x, y, t);
		dc.SelectObject(prev);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(52, 52, 68));
		dc.TextOut(x, y, t);
	};
	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"MIDIモニタ操作ガイド", L"MIDI Monitor — Guide", L"Moniteur MIDI — Guide", L"Monitor MIDI — Guida",
		L"Monitor MIDI — Guía", L"MIDI 모니터 — 가이드", L"MIDI监视器 — 指南", L"مراقب MIDI — دليل",
		L"MIDI-монитор — руководство", L"MIDI-Monitor — Anleitung", L"Monitor MIDI — Guia", L"MIDI-monitor — Gids",
		L"Monitor MIDI — przewodnik", L"MIDI izleyici — kilavuz"));
	y += titleLh;
	body(L, y, LL14(
		L"再生中の SMF(.mid/.midi/.kar/.rmi) を32パート(A01-A16 / B01-B16)で監視します。音声推定ではなくファイルのMIDI事象そのものです。",
		L"Watches the playing SMF (.mid/.midi/.kar/.rmi) as 32 parts (A01-A16 / B01-B16). This is the file's MIDI events, not audio pitch detection.",
		L"Surveille le SMF en lecture (32 parties A01-A16 / B01-B16). Ce sont les evenements MIDI du fichier, pas une detection audio.",
		L"Controlla lo SMF in riproduzione (32 parti A01-A16 / B01-B16). Sono gli eventi MIDI del file, non il rilevamento audio.",
		L"Vigila el SMF en reproduccion (32 partes A01-A16 / B01-B16). Son los eventos MIDI del archivo, no deteccion de audio.",
		L"재생 중인 SMF를 32파트(A01-A16 / B01-B16)로 감시합니다. 오디오 추정가 아니라 파일의 MIDI 이벤트입니다.",
		L"监视正在播放的 SMF，共 32 声部（A01-A16 / B01-B16）。这是文件里的 MIDI 事件，不是音频音高检测。",
		L"يراقب ملف SMF أثناء التشغيل كـ 32 جزءاً. هذه أحداث MIDI من الملف وليست كشفاً صوتياً.",
		L"Следит за SMF во время воспроизведения (32 партии A01-A16 / B01-B16). Это MIDI-события файла, не оценка по аудио.",
		L"Ueberwacht das spielende SMF in 32 Parts (A01-A16 / B01-B16). Das sind MIDI-Ereignisse der Datei, keine Audioerkennung.",
		L"Monitora o SMF em reproducao em 32 partes (A01-A16 / B01-B16). Sao os eventos MIDI do arquivo, nao deteccao de audio.",
		L"Bewaakt het spelende SMF in 32 partijen (A01-A16 / B01-B16). Dit zijn MIDI-events uit het bestand, geen audiodetectie.",
		L"Nadzoruje odtwarzane SMF w 32 partiach (A01-A16 / B01-B16). To zdarzenia MIDI z pliku, nie detekcja audio.",
		L"Calan SMF'yi 32 part (A01-A16 / B01-B16) olarak izler. Ses tahmini degil, dosyanin MIDI olaylaridir."));
	y += lh + 4;
	title(L, y, LL14(L"表示", L"View", L"Affichage", L"Vista", L"Vista", L"표시", L"显示", L"العرض", L"Вид", L"Ansicht", L"Vista", L"Weergave", L"Widok", L"Gorunum"));
	y += titleLh;
	body(L, y, LL14(
		L"・通常(2D) …… ヘッダ(BPM/拍子/リバーブ等)と32行のチャンネル表。右端はミニ鍵盤。",
		L"· Normal (2D) …… Header (BPM/meter/reverb…) and a 32-row channel table. Mini keyboard on the right.",
		L"· Normal (2D) …… En-tete (BPM/mesure/reverb…) et tableau 32 canaux. Mini clavier a droite.",
		L"· Normale (2D) …… Intestazione (BPM/misura/reverb…) e tabella 32 canali. Mini tastiera a destra.",
		L"· Normal (2D) …… Cabecera (BPM/compas/reverb…) y tabla de 32 canales. Mini teclado a la derecha.",
		L"· 일반(2D) …… 헤더(BPM/박자/리버브 등)와 32행 채널 표. 오른쪽은 미니 건반.",
		L"· 普通(2D) …… 页眉（BPM/拍号/混响等）和 32 行通道表。右侧是迷你键盘。",
		L"· عادي (2D) …… رأس (BPM/ميزان/صدى…) وجدول 32 قناة. لوحة مفاتيح صغيرة يميناً.",
		L"· Обычный (2D) …… Заголовок (BPM/размер/реверб…) и таблица 32 каналов. Мини-клавиатура справа.",
		L"· Normal (2D) …… Kopf (BPM/Takt/Reverb…) und 32-Zeilen-Kanaltabelle. Mini-Tastatur rechts.",
		L"· Normal (2D) …… Cabecalho (BPM/compasso/reverb…) e tabela de 32 canais. Mini teclado a direita.",
		L"· Normaal (2D) …… Kop (BPM/maatsoort/reverb…) en 32-rijige kanaaltabel. Mini-toetsenbord rechts.",
		L"· Zwykly (2D) …… Naglowek (BPM/metrum/poglos…) i tabela 32 kanalow. Mini klawiatura po prawej.",
		L"· Normal (2D) …… Baslik (BPM/olcu/reverb…) ve 32 satirlik kanal tablosu. Sagda mini klavye."));
	y += lh;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KMIDIMON);
	title(L, y, LL14(L"簡易3D(Soft3D)", L"Soft 3D", L"3D simplifie", L"3D semplificato", L"3D simple", L"간이 3D", L"简易3D", L"Soft 3D",
		L"Простой 3D", L"Soft 3D", L"Soft 3D", L"Eenvoudig 3D", L"Soft 3D", L"Soft 3B"));
	y += titleLh;
	body(L, y, LL14(
		L"・右クリック「表示モード」→ 簡易3D。32パートのレベルを箱として CPU 描画(OpenGL/Direct3D 不使用)。",
		L"· Right-click View → Soft 3D. 32 part levels as boxes, CPU-only (no OpenGL/Direct3D).",
		L"· Clic droit Affichage → Soft 3D. 32 niveaux en boites, CPU seul (pas OpenGL/Direct3D).",
		L"· Destro Vista → Soft 3D. 32 livelli a scatole, solo CPU (niente OpenGL/Direct3D).",
		L"· Clic der. Vista → Soft 3D. 32 niveles en cajas, solo CPU (sin OpenGL/Direct3D).",
		L"· 우클릭 「표시 모드」→ 간이 3D. 32파트 레벨을 상자로 CPU만 그립니다.",
		L"· 右键「显示模式」→ 简易3D。用盒子表示 32 声部电平，仅 CPU（无 OpenGL/Direct3D）。",
		L"· يمين ← العرض ← Soft 3D. مستويات 32 جزءاً كصناديق، معالج فقط.",
		L"· ПКМ «Вид» → Soft 3D. 32 уровня партий ящиками, только CPU.",
		L"· Rechtsklick Ansicht → Soft 3D. 32 Part-Pegel als Boxen, nur CPU.",
		L"· Direito Exibir → Soft 3D. 32 niveis em caixas, so CPU.",
		L"· Rechtsklik Weergave → Soft 3D. 32 partijniveaus als dozen, alleen CPU.",
		L"· PPM Widok → Soft 3D. 32 poziomy partii jako pudelka, tylko CPU.",
		L"· Sag tik Gorunum → Soft 3B. 32 part seviyesi kutu olarak, yalnizca CPU."));
	y += lh;
	body(L, y, LL14(
		L"・ドラッグで回転、ホイールでズーム、0 で視点リセット。重いときは 2D に戻せます。",
		L"· Drag to orbit, wheel to zoom, 0 to reset view. Switch back to 2D if it feels heavy.",
		L"· Glisser = orbite, molette = zoom, 0 = reset. Revenez en 2D si c'est lourd.",
		L"· Trascina = orbita, rotella = zoom, 0 = reset. Torna al 2D se e pesante.",
		L"· Arrastrar = orbita, rueda = zoom, 0 = restablecer. Vuelva a 2D si va lento.",
		L"· 드래그=회전, 휠=줌, 0=리셋. 무거우면 2D로 되돌리세요.",
		L"· 拖动旋转、滚轮缩放、0 重置。觉得卡就切回 2D。",
		L"· سحب للدوران، عجلة للتكبير، 0 لإعادة الضبط. عد إلى 2D إذا ثقل.",
		L"· Перетаскивание — поворот, колесо — масштаб, 0 — сброс. Вернитесь в 2D, если тяжело.",
		L"· Ziehen = drehen, Rad = Zoom, 0 = Reset. Zurueck zu 2D, wenn es zäh ist.",
		L"· Arrastar = orbitar, roda = zoom, 0 = redefinir. Volte ao 2D se pesar.",
		L"· Sleepen = draaien, wiel = zoom, 0 = reset. Terug naar 2D als het zwaar is.",
		L"· Przeciagnij = obrot, kolo = zoom, 0 = reset. Wroc do 2D, gdy ciezko.",
		L"· Surukle = don, tekerlek = zoom, 0 = sifirla. Agirsa 2B'ye donun."));
	y += lh + 4;
	title(L, y, LL14(L"列の見方", L"Columns", L"Colonnes", L"Colonne", L"Columnas", L"열", L"列", L"الأعمدة", L"Столбцы", L"Spalten", L"Colunas", L"Kolommen", L"Kolumny", L"Sutunlar"));
	y += titleLh;
	body(L, y, LL14(
		L"・PC# BNK Map …… プログラム、バンク、GS/XG マップ。Instrument は DAT(SASAMI_GS/XG) から名前。",
		L"· PC# BNK Map …… Program, bank, GS/XG map. Instrument names come from SASAMI_GS/XG DAT.",
		L"· PC# BNK Map …… Programme, banque, carte GS/XG. Noms depuis SASAMI_GS/XG.DAT.",
		L"· PC# BNK Map …… Programma, bank, mappa GS/XG. Nomi da SASAMI_GS/XG.DAT.",
		L"· PC# BNK Map …… Programa, banco, mapa GS/XG. Nombres desde SASAMI_GS/XG.DAT.",
		L"· PC# BNK Map …… 프로그램, 뱅크, GS/XG 맵. 이름은 SASAMI_GS/XG.DAT에서.",
		L"· PC# BNK Map …… 音色号、库、GS/XG 映射。名称来自 SASAMI_GS/XG.DAT。",
		L"· PC# BNK Map …… برنامج، بنك، خريطة GS/XG. الأسماء من SASAMI_GS/XG.DAT.",
		L"· PC# BNK Map …… Программа, банк, карта GS/XG. Имена из SASAMI_GS/XG.DAT.",
		L"· PC# BNK Map …… Programm, Bank, GS/XG-Map. Namen aus SASAMI_GS/XG.DAT.",
		L"· PC# BNK Map …… Programa, banco, mapa GS/XG. Nomes de SASAMI_GS/XG.DAT.",
		L"· PC# BNK Map …… Programma, bank, GS/XG-map. Namen uit SASAMI_GS/XG.DAT.",
		L"· PC# BNK Map …… Program, bank, mapa GS/XG. Nazwy z SASAMI_GS/XG.DAT.",
		L"· PC# BNK Map …… Program, banka, GS/XG haritasi. Isimler SASAMI_GS/XG.DAT'tan."));
	y += lh;
	body(L, y, LL14(
		L"・Lev は発音。Vol/Pan/Exp 等は動いているとき明るく、初期値のままなら暗くなります。ヘッダ Vol は再生音量（ドラッグで本体/MP と同期）。Notes は実発音数、暗いバーが MAX（少しして減衰）。DRUM はドラムパートのヒット。",
		L"· Lev is level. Vol/Pan/Exp light up while moving, stay dim at defaults. Header Vol is playback volume (drag syncs with the main/MP slider). Notes is held polyphony; the dim bar is MAX (holds, then decays). DRUM lights on drum hits.",
		L"· Lev = niveau. Vol/Pan/Exp s'allument en mouvement. Vol d'en-tete = volume (glisser = sync MP). Notes = polyphonie reelle; barre sombre = MAX (puis baisse). DRUM = hits batterie.",
		L"· Lev e il livello. Vol/Pan/Exp si illuminano se si muovono. Vol in testa = volume (trascina = sync MP). Notes = polifonia reale; barra scura = MAX (poi scende). DRUM = colpi batteria.",
		L"· Lev es el nivel. Vol/Pan/Exp se iluminan al moverse. Vol de cabecera = volumen (arrastrar = sync MP). Notes = polifonia real; barra oscura = MAX (luego baja). DRUM = golpes de bateria.",
		L"· Lev는 레벨. Vol/Pan/Exp는 움직일 때 밝고, 기본값이면 어둡습니다. 헤더 Vol은 재생 음량(드래그하면 본체/MP와 동기). Notes는 실제 발음 수, 어두운 바는 MAX(잠시 후 감쇠). DRUM은 드럼 타격.",
		L"· Lev 是电平。Vol/Pan/Exp 在变化时发亮。页眉 Vol 是播放音量（拖动与主界面/MP 同步）。Notes 是实际发音数，暗条是 MAX（稍后衰减）。DRUM 表示鼓组敲击。",
		L"· Lev هو المستوى. Vol الرأس = حجم التشغيل. Notes = تعدد الأصوات الفعلي؛ الشريط الداكن = MAX ثم ينخفض. DRUM لضربات الطبل.",
		L"· Lev — уровень. Vol в шапке — громкость. Notes — реальная полифония; тёмная полоса — MAX (потом спадает). DRUM — удары барабанов.",
		L"· Lev ist Pegel. Kopf-Vol ist Wiedergabelautstaerke. Notes ist echte Polyphonie; die dunkle Leiste ist MAX (dann Abfall). DRUM = Drum-Hits.",
		L"· Lev e o nivel. Vol do cabecalho e o volume. Notes e a polifonia real; a barra escura e MAX (depois cai). DRUM = batidas de bateria.",
		L"· Lev is niveau. Kop-Vol is afspeelvolume. Notes is echte polyfonie; de donkere balk is MAX (daarna verval). DRUM = drumhits.",
		L"· Lev to poziom. Vol w naglowku to glosnosc. Notes to rzeczywista polifonia; ciemny pasek to MAX (potem opada). DRUM = uderzenia perkusji.",
		L"· Lev seviyedir. Baslik Vol oynatma sesidir. Notes gercek polifonidir; koyu cubuk MAX'tir (sonra duser). DRUM davul vurusudur."));
	y += lh;
	body(L, y, LL14(
		L"・Vol/Pan/Exp/Rev/Crs/Var はドラッグまたはホイールで送出（ダブルクリックで初期値）。PC# はホイールでプログラム変更。右端のミニ鍵盤はクリックで発音。曲の CC より約2.5秒優先します。",
		L"· Drag or wheel Vol/Pan/Exp/Rev/Crs/Var to send CC (double-click resets). Wheel PC# for program change. Click the mini keyboard to play. Your edits hold about 2.5s over the song CC.",
		L"· Glisser / molette Vol/Pan/Exp/Rev/Crs/Var envoie le CC (double-clic = defaut). Molette sur PC# = programme. Mini clavier cliquable. Vos reglages priment ~2,5 s sur le SMF.",
		L"· Trascina o rotella su Vol/Pan/Exp/Rev/Crs/Var per inviare CC (doppio clic = default). Rotella su PC# = programma. Mini tastiera cliccabile. Le modifiche restano ~2,5 s sul SMF.",
		L"· Arrastre o rueda Vol/Pan/Exp/Rev/Crs/Var para enviar CC (doble clic = valor por defecto). Rueda en PC# = programa. Mini teclado clicable. Sus ediciones pesan ~2,5 s sobre el SMF.",
		L"· Vol/Pan/Exp/Rev/Crs/Var는 드래그 또는 휠로 전송(더블클릭=초기값). PC#는 휠로 프로그램 변경. 미니 건반 클릭으로 연주. 곡 CC보다 약 2.5초 우선.",
		L"· 拖动或滚轮 Vol/Pan/Exp/Rev/Crs/Var 发送 CC（双击恢复默认）。滚轮 PC# 改音色。点击迷你键盘发音。约 2.5 秒内优先于乐曲 CC。",
		L"· اسحب أو عجلة Vol/Pan/Exp لإرسال CC. عجلة PC# لتغيير البرنامج. انقر على لوحة المفاتيح الصغيرة للعزف. تعديلاتك تسبق ملف SMF نحو 2.5 ث.",
		L"· Перетаскивание/колесо Vol/Pan/Exp/Rev/Crs/Var шлёт CC (двойной щелчок — сброс). Колесо на PC# — программа. Мини-клавиатура играет. Правки держатся ~2,5 с над CC файла.",
		L"· Ziehen oder Rad auf Vol/Pan/Exp/Rev/Crs/Var sendet CC (Doppelklick = Default). Rad auf PC# = Programm. Mini-Tastatur ist spielbar. Edits gelten ~2,5 s vor Song-CC.",
		L"· Arrastar ou roda em Vol/Pan/Exp/Rev/Crs/Var envia CC (duplo clique = padrao). Roda em PC# = programa. Mini teclado toca. Edicoes valem ~2,5 s sobre o CC do SMF.",
		L"· Slepen of wiel op Vol/Pan/Exp/Rev/Crs/Var stuurt CC (dubbelklik = default). Wiel op PC# = programma. Mini-toetsenbord speelt. Edits gaan ~2,5 s voor SMF-CC.",
		L"· Przeciagnij lub kolo na Vol/Pan/Exp/Rev/Crs/Var wysyla CC (dwuklik = domyslne). Kolo na PC# = program. Mini klawiatura gra. Edycje trzymaja ~2,5 s nad CC z SMF.",
		L"· Vol/Pan/Exp/Rev/Crs/Var surukleme veya tekerlekle CC gonderir (cift tik = varsayilan). PC# tekerlegi program degistirir. Mini klavye calinir. Duzenlemeler SMF CC'den ~2,5 sn once gelir."));
	y += lh + 2;
	title(L, y, LL14(L"右クリック", L"Right-click", L"Clic droit", L"Tasto destro", L"Clic derecho", L"우클릭", L"右键", L"زر أيمن", L"ПКМ", L"Rechtsklick", L"Botao direito", L"Rechtsklik", L"PPM", L"Sag tik"));
	y += titleLh;
	body(L, y, LL14(
		L"・表示モード / フリーズ / 常に手前 / 状態をコピー / マップ指定 / 他ウィンドウ / このガイド。",
		L"· View mode / Freeze / Always on top / Copy state / Map / Other windows / This guide.",
		L"· Mode d'affichage / Gel / Premier plan / Copier / Carte / Autres fenetres / Ce guide.",
		L"· Modalita / Congela / In primo piano / Copia / Mappa / Altre finestre / Questa guida.",
		L"· Modo / Congelar / Siempre visible / Copiar / Mapa / Otras ventanas / Esta guia.",
		L"· 표시 모드 / 프리즈 / 항상 위 / 상태 복사 / 맵 / 다른 창 / 이 가이드.",
		L"· 显示模式 / 冻结 / 置顶 / 复制状态 / 映射 / 其他窗口 / 本指南。",
		L"· وضع العرض / تجميد / دائماً أعلى / نسخ / خريطة / نوافذ أخرى / هذا الدليل.",
		L"· Режим / Заморозка / Поверх всех / Копия / Карта / Другие окна / Это руководство.",
		L"· Ansicht / Einfrieren / Immer oben / Kopieren / Map / Andere Fenster / Diese Anleitung.",
		L"· Modo / Congelar / Sempre no topo / Copiar / Mapa / Outras janelas / Este guia.",
		L"· Weergave / Bevriezen / Altijd boven / Kopieren / Map / Andere vensters / Deze gids.",
		L"· Widok / Zamroz / Zawsze na wierzchu / Kopiuj / Mapa / Inne okna / Ten przewodnik.",
		L"· Gorunum / Dondur / Her zaman ustte / Kopyala / Harita / Diger pencereler / Bu kilavuz."));
	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

IMPLEMENT_DYNAMIC(CMidiMonitorDlg, CCustomBlurDialogExBase)

CMidiMonitorDlg::CMidiMonitorDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(IDD_MIDIMONITOR, pParent)
	, m_frameOld(nullptr), m_frameW(0), m_frameH(0)
#if CCUSTOM_AERO_SUPPORT
	, m_chromaW(0), m_chromaH(0), m_chromaReady(false)
#endif
	, m_fontDpi(0), m_fontH(0)
	, m_ev(NULL), m_evCount(0), m_evPos(0), m_sx(NULL), m_sxBytes(0)
	, m_division(480), m_sampleRate(44100), m_lastPlayb(-1)
	, m_usecQn(500000), m_tsNum(4), m_tsDen(4), m_keySf(0), m_keyMin(0), m_transpose(0)
	, m_sysMode(0), m_revType(1), m_choType(2), m_varType(1), m_ins1(0), m_ins2(0)
	, m_noteCount(0), m_masterVol(100)
	, m_notesPeak(0), m_notesPeakHold(0), m_layW(0)
	, m_dragKind(0), m_dragPart(-1), m_playPart(-1), m_playNote(-1)
	, m_viewMode(0), m_mapForce(0), m_frozen(false), m_alwaysOnTop(false), m_paintDisabled(false)
	, m_rotDragging(false), m_rotDragYaw0(0), m_rotDragPitch0(0), m_soft3dTourUntil(0)
	, m_hoverCol(-1), m_hoverPart(-1)
	, m_layHeadH(0), m_layRowH(0), m_persistAge(0), m_drumGlow(0), m_dispBpm(-1)
	, m_dirtyRows(0xFFFFFFFFu), m_rowLive(0)
	, m_dirtyHead(true), m_fullDraw(true), m_volDragging(false)
{
	m_loadedPath[0] = 0;
	m_titleBuf[0] = 0;
	m_hoverTip[0] = 0;
	m_volBarRc.SetRectEmpty();
	m_notesBarRc.SetRectEmpty();
	memset(m_part, 0, sizeof(m_part));
	memset(m_latchUntil, 0, sizeof(m_latchUntil));
	memset(m_latchMask, 0, sizeof(m_latchMask));
}

CMidiMonitorDlg::~CMidiMonitorDlg()
{
	UnloadMidi();
	ReleasePaintBuffers();
}

void CMidiMonitorDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MM_HELP, m_help);
}

BEGIN_MESSAGE_MAP(CMidiMonitorDlg, CCustomBlurDialogExBase)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_MOVE()
	ON_WM_SHOWWINDOW()
	ON_WM_CLOSE()
	ON_WM_DESTROY()
	ON_WM_CONTEXTMENU()
	ON_BN_CLICKED(IDC_MM_HELP, &CMidiMonitorDlg::OnBnClickedHelp)
	ON_COMMAND(ID_HELP_SHOWSHEET, &CMidiMonitorDlg::OnBnClickedHelp)
	ON_WM_LBUTTONDOWN()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEWHEEL()
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, &CMidiMonitorDlg::OnTtnNeedText)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, &CMidiMonitorDlg::OnTtnNeedText)
END_MESSAGE_MAP()

UINT CMidiMonitorDlg::WindowDpi() const
{
	HWND h = GetSafeHwnd();
	if (!h) return 96;
	typedef UINT(WINAPI* PFN)(HWND);
	static PFN s_fn = NULL;
	static BOOL s_got = FALSE;
	if (!s_got) {
		HMODULE u = ::GetModuleHandleW(L"user32.dll");
		if (u) s_fn = (PFN)::GetProcAddress(u, "GetDpiForWindow");
		s_got = TRUE;
	}
	if (s_fn) {
		UINT d = s_fn(h);
		if (d > 0) return d;
	}
	HDC hdc = ::GetDC(h);
	UINT d = hdc ? (UINT)GetDeviceCaps(hdc, LOGPIXELSX) : 96;
	if (hdc) ::ReleaseDC(h, hdc);
	return d ? d : 96;
}

void CMidiMonitorDlg::ReleasePaintBuffers()
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
	m_fullDraw = true;
#if CCUSTOM_AERO_SUPPORT
	m_chromaCache.Release();
	m_chromaReady = false;
	m_chromaW = m_chromaH = 0;
#endif
}

bool CMidiMonitorDlg::EnsureFrameBuffer(CDC& refDC, int w, int h)
{
	if (w <= 0 || h <= 0) return false;
	if (m_frameDC.GetSafeHdc() && m_frameW == w && m_frameH == h && m_frameBmp.GetSafeHandle())
		return true;
	ReleasePaintBuffers();
	if (!m_frameDC.CreateCompatibleDC(&refDC))
		return false;
	if (!m_frameBmp.CreateCompatibleBitmap(&refDC, w, h)) {
		m_frameDC.DeleteDC();
		return false;
	}
	m_frameOld = m_frameDC.SelectObject(&m_frameBmp);
	m_frameW = w;
	m_frameH = h;
	m_fullDraw = true;
	return true;
}

void CMidiMonitorDlg::ResetParts()
{
	memset(m_part, 0, sizeof(m_part));
	for (int i = 0; i < PART_MAX; ++i) {
		Part& p = m_part[i];
		p.pc = 0;
		p.vol = 100;
		p.exp = 127;
		p.pan = 64;
		p.rev = 40;
		p.eqLow = 80;
		p.eqHigh = 10000;
		p.mapId = 4;
		p.isDrum = ((i % 16) == 9) ? 1 : 0;
		p.lastNote = -1;
		RefreshPartName(p);
	}
	m_usecQn = 500000;
	m_tsNum = 4;
	m_tsDen = 4;
	m_keySf = 0;
	m_keyMin = 0;
	m_transpose = 0;
	m_sysMode = 1;
	m_revType = 1;
	m_choType = 2;
	m_varType = 1;
	m_ins1 = 0;
	m_ins2 = 0;
	m_noteCount = 0;
	m_notesPeak = 0;
	m_notesPeakHold = 0;
	m_masterVol = 100;
	m_evPos = 0;
	m_dirtyRows = 0xFFFFFFFFu;
	m_dirtyHead = true;
	m_fullDraw = true;
	m_rowLive = 0;
	m_drumGlow = 0;
	memset(m_latchUntil, 0, sizeof(m_latchUntil));
	memset(m_latchMask, 0, sizeof(m_latchMask));
}

void CMidiMonitorDlg::UnloadMidi()
{
	if (m_ev) { delete[] m_ev; m_ev = NULL; }
	if (m_sx) { delete[] m_sx; m_sx = NULL; }
	m_evCount = m_evPos = 0;
	m_sxBytes = 0;
	m_loadedPath[0] = 0;
}

void CMidiMonitorDlg::LookupToneName(int isXg, int mapId, int bankMsb, int bankLsb, int pc, int isDrum, wchar_t* out, int outN)
{
	out[0] = 0;
	MmEnsureDat();
	if (isDrum) {
		if (isXg) {
			if (MmLookupXgOk(127, 0, pc, out, outN) && out[0]) return;
		} else {
			if (MmLookupGs(0, 0, pc, out, outN) && out[0]) return;
		}
		MmCopyW(out, outN, L"Standard Kit");
		return;
	}
	if (isXg) {
		if (MmLookupXgOk(bankMsb, bankLsb, pc, out, outN) && out[0]) return;
	} else {
		if (MmLookupGs(mapId, bankMsb, pc, out, outN) && out[0]) return;
	}
	if (pc >= 0 && pc < 128)
		MmCopyW(out, outN, kGmName[pc]);
	else
		MmCopyW(out, outN, L"—");
}

void CMidiMonitorDlg::RefreshPartName(Part& p)
{
	const int isXg = (m_mapForce == 2) ? 1 : (m_mapForce == 1) ? 0 : ((m_sysMode == 2) ? 1 : 0);
	LookupToneName(isXg, p.mapId, p.bankMsb, p.bankLsb, p.pc, p.isDrum, p.name, NAME_CHARS);
}

void CMidiMonitorDlg::ApplyNrpn(Part& p)
{
	const int d = p.dataMsb;
	if (p.nrpnMsb == 1) {
		if (p.nrpnLsb == 8) p.vibRat = d - 64;
		else if (p.nrpnLsb == 9) p.vibDpt = d - 64;
		else if (p.nrpnLsb == 10) p.vibDly = d - 64;
		else if (p.nrpnLsb == 0x20) p.lpf = d - 64;
		else if (p.nrpnLsb == 0x21) p.rsn = d - 64;
		else if (p.nrpnLsb == 0x24) p.hpf = d - 64;
		else if (p.nrpnLsb == 0x63) p.atk = d - 64;
		else if (p.nrpnLsb == 0x64) p.dcy = d - 64;
		else if (p.nrpnLsb == 0x66) p.rls = d - 64;
	}
}

void CMidiMonitorDlg::ApplyShort(int port, DWORD msg)
{
	const int st = msg & 0xf0;
	const int ch = msg & 0x0f;
	int part = (port * 16) + ch;
	if (part < 0) part = 0;
	if (part >= PART_MAX) part = PART_MAX - 1;
	Part& p = m_part[part];
	const int d1 = (int)((msg >> 8) & 0xff);
	const int d2 = (int)((msg >> 16) & 0xff);
	if (st == 0x90) {
		if (d2 > 0) {
			p.noteOn[d1] = (BYTE)d2;
			p.lastNote = d1;
			p.lastVel = d2;
			p.held++;
			if (p.held < 1) p.held = 1;
			const float lv = (float)d2 / 127.f;
			if (lv > p.lev) p.lev = lv;
			m_dirtyRows |= (1u << part);
			if (p.isDrum) m_drumGlow = 255;
		} else {
			p.noteOn[d1] = 0;
			if (p.held > 0) p.held--;
			m_dirtyRows |= (1u << part);
		}
	} else if (st == 0x80) {
		p.noteOn[d1] = 0;
		if (p.held > 0) p.held--;
		m_dirtyRows |= (1u << part);
	} else if (st == 0xc0) {
		if (!IsLatched(part, MM_LATCH_PC)) {
			p.pc = d1 & 127;
			RefreshPartName(p);
			m_dirtyRows |= (1u << part);
		}
	} else if (st == 0xb0) {
		if (d1 == 0) { p.bankMsb = d2; RefreshPartName(p); m_dirtyRows |= (1u << part); }
		else if (d1 == 32) { p.bankLsb = d2; RefreshPartName(p); m_dirtyRows |= (1u << part); }
		else if (d1 == 7) { if (!IsLatched(part, MM_LATCH_VOL)) { if (p.vol != d2) p.glowVol = 255; p.vol = d2; m_dirtyRows |= (1u << part); } }
		else if (d1 == 11) { if (!IsLatched(part, MM_LATCH_EXP)) { if (p.exp != d2) p.glowExp = 255; p.exp = d2; m_dirtyRows |= (1u << part); } }
		else if (d1 == 10) { if (!IsLatched(part, MM_LATCH_PAN)) { if (p.pan != d2) p.glowPan = 255; p.pan = d2; m_dirtyRows |= (1u << part); } }
		else if (d1 == 91) { if (!IsLatched(part, MM_LATCH_REV)) { if (p.rev != d2) p.glowRev = 255; p.rev = d2; m_dirtyRows |= (1u << part); } }
		else if (d1 == 93) { if (!IsLatched(part, MM_LATCH_CRS)) { if (p.crs != d2) p.glowCrs = 255; p.crs = d2; m_dirtyRows |= (1u << part); } }
		else if (d1 == 94) { if (!IsLatched(part, MM_LATCH_VAR)) { if (p.var != d2) p.glowVar = 255; p.var = d2; m_dirtyRows |= (1u << part); } }
		else if (d1 == 71) { p.rsn = d2 - 64; m_dirtyRows |= (1u << part); }
		else if (d1 == 74) { p.lpf = d2 - 64; m_dirtyRows |= (1u << part); }
		else if (d1 == 72) { p.rls = d2 - 64; m_dirtyRows |= (1u << part); }
		else if (d1 == 73) { p.atk = d2 - 64; m_dirtyRows |= (1u << part); }
		else if (d1 == 75) { p.dcy = d2 - 64; m_dirtyRows |= (1u << part); }
		else if (d1 == 76) { p.vibRat = d2 - 64; m_dirtyRows |= (1u << part); }
		else if (d1 == 77) { p.vibDpt = d2 - 64; m_dirtyRows |= (1u << part); }
		else if (d1 == 78) { p.vibDly = d2 - 64; m_dirtyRows |= (1u << part); }
		else if (d1 == 98) p.nrpnLsb = d2;
		else if (d1 == 99) p.nrpnMsb = d2;
		else if (d1 == 100) p.rpnLsb = d2;
		else if (d1 == 101) p.rpnMsb = d2;
		else if (d1 == 6) { p.dataMsb = d2; ApplyNrpn(p); m_dirtyRows |= (1u << part); }
		else if (d1 == 121) {
			p.exp = 127; p.pan = 64; p.rev = 40; p.crs = 0; p.var = 0;
			p.glowExp = p.glowPan = p.glowRev = p.glowCrs = p.glowVar = 180;
			m_dirtyRows |= (1u << part);
		}
		else if (d1 == 120 || d1 == 123) {
			memset(p.noteOn, 0, sizeof(p.noteOn));
			p.held = 0;
		}
	} else if (st == 0xe0) {
		const int bend = ((d2 << 7) | d1) - 8192;
		p.dt = bend / 64;
	}
}

void CMidiMonitorDlg::ApplySysex(const BYTE* d, int n)
{
	if (!d || n < 6) return;
	if (d[0] != 0xf0) return;
	if (n >= 6 && d[1] == 0x7e && d[3] == 0x09 && d[4] == 0x01) {
		m_sysMode = 0;
		ResetParts();
		m_sysMode = 0;
		return;
	}
	if (n >= 11 && d[1] == 0x41 && d[3] == 0x42 && d[4] == 0x12 && d[5] == 0x40 && d[6] == 0x00 && d[7] == 0x7f) {
		m_sysMode = 1;
		ResetParts();
		m_sysMode = 1;
		return;
	}
	if (n >= 9 && d[1] == 0x43 && d[3] == 0x4c && d[4] == 0x00 && d[5] == 0x00 && d[6] == 0x7e) {
		m_sysMode = 2;
		ResetParts();
		m_sysMode = 2;
		return;
	}
	if (n >= 9 && d[1] == 0x43 && d[3] == 0x4c && d[4] == 0x02 && d[5] == 0x01) {
		if (d[6] == 0x00) m_revType = d[7];
		else if (d[6] == 0x20) m_choType = d[7];
		else if (d[6] == 0x40) m_varType = d[7];
		m_dirtyHead = true;
	}
	if (n >= 11 && d[1] == 0x41 && d[3] == 0x42 && d[4] == 0x12 && d[5] == 0x40 && d[6] == 0x01) {
		if (d[7] == 0x30) m_revType = d[8];
		else if (d[7] == 0x38) m_choType = d[8];
		m_dirtyHead = true;
	}
	if (n >= 9 && d[1] == 0x43 && d[3] == 0x4c && d[4] == 0x03 && d[5] == 0x00) {
		m_ins1 = d[7]; m_dirtyHead = true;
	}
	if (n >= 9 && d[1] == 0x43 && d[3] == 0x4c && d[4] == 0x03 && d[5] == 0x10) {
		m_ins2 = d[7]; m_dirtyHead = true;
	}
}

void CMidiMonitorDlg::ApplyEvent(const MmEv& e)
{
	if (e.msg == 0xff) {
		if (e.aux >= 10000) m_usecQn = (int)e.aux;
		return;
	}
	if (e.msg == 0xfe) {
		m_tsNum = (int)(e.aux & 0xff);
		m_tsDen = (int)((e.aux >> 8) & 0xff);
		if (m_tsNum < 1) m_tsNum = 4;
		if (m_tsDen < 1) m_tsDen = 4;
		return;
	}
	if (e.msg == 0xfd) {
		m_keySf = (int)(signed char)(e.aux & 0xff);
		m_keyMin = (int)((e.aux >> 8) & 1);
		return;
	}
	if (e.msg == 0xf0) {
		if (e.sysexOff >= 0 && e.sysexOff + (int)e.aux <= m_sxBytes)
			ApplySysex(m_sx + e.sysexOff, (int)e.aux);
		return;
	}
	ApplyShort(e.port, e.msg);
}

void CMidiMonitorDlg::LoadCurrentMidi()
{
	wchar_t mid[520];
	mid[0] = 0;
	wchar_t hints[8][128];
	int hc = 0;
	const wchar_t* src = filen;
	if (src && src[0])
		VstResolvePlayPath(src, mid, 520, hints, 8, &hc);
	if (!mid[0] && src)
		MmCopyW(mid, 520, src);
	if (!mid[0]) {
		UnloadMidi();
		ResetParts();
		return;
	}
	if (_wcsicmp(m_loadedPath, mid) == 0)
		return;

	UnloadMidi();
	ResetParts();
	HANDLE f = CreateFileW(mid, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
	if (f == INVALID_HANDLE_VALUE) return;
	DWORD size = GetFileSize(f, NULL), got = 0;
	if (size < 14 || size > 64 * 1024 * 1024) { CloseHandle(f); return; }
	BYTE* data = new BYTE[size];
	if (!ReadFile(f, data, size, &got, NULL) || got != size) {
		CloseHandle(f); delete[] data; return;
	}
	CloseHandle(f);
	const BYTE* smf = data;
	DWORD smfSize = size;
	if (size >= 20 && memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "RMID", 4) == 0) {
		DWORD off = 12;
		while (off + 8 <= size) {
			const DWORD cksz = (DWORD)data[off + 4] | ((DWORD)data[off + 5] << 8)
				| ((DWORD)data[off + 6] << 16) | ((DWORD)data[off + 7] << 24);
			if (memcmp(data + off, "data", 4) == 0) {
				if (off + 8 > size) break;
				smf = data + off + 8;
				smfSize = cksz;
				if (smf + smfSize > data + size)
					smfSize = (DWORD)(data + size - smf);
				break;
			}
			const DWORD step = 8 + ((cksz + 1) & ~1u);
			if (step < 8 || off + step < off) break;
			off += step;
		}
	}
	if (smfSize < 14 || memcmp(smf, "MThd", 4) || MmReadBE(smf + 4, 4) < 6) {
		delete[] data; return;
	}
	const int tracks = (int)MmReadBE(smf + 10, 2);
	const int division = (int)MmReadBE(smf + 12, 2);
	if (division <= 0 || (division & 0x8000)) { delete[] data; return; }
	m_division = division;
	MmEv* ev = new MmEv[EV_MAX];
	BYTE* sxData = new BYTE[size + 8];
	int sxUsed = 0;
	int count = 0;
	const BYTE* p = smf + 8 + MmReadBE(smf + 4, 4);
	const BYTE* fileEnd = smf + smfSize;
	for (int tr = 0; tr < tracks && p + 8 <= fileEnd; ++tr) {
		if (memcmp(p, "MTrk", 4)) break;
		DWORD len = MmReadBE(p + 4, 4);
		const BYTE* q = p + 8;
		const BYTE* end = (q + len <= fileEnd) ? q + len : fileEnd;
		unsigned __int64 tick = 0;
		BYTE running = 0;
		int curPort = 0;
		while (q < end && count < EV_MAX) {
			unsigned delta = 0;
			if (!MmReadVar(q, end, delta)) break;
			tick += delta;
			if (q >= end) break;
			BYTE st = *q;
			if (st & 0x80) { ++q; if (st < 0xf0) running = st; }
			else if (running) st = running;
			else break;
			if (st == 0xff) {
				if (q >= end) break;
				BYTE type = *q++;
				unsigned ml = 0;
				if (!MmReadVar(q, end, ml) || q + ml > end) break;
				if (type == 0x51 && ml == 3) {
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = 0xff; ev[count].aux = MmReadBE(q, 3);
					ev[count].port = curPort; ev[count].sysexOff = -1;
					++count;
				} else if (type == 0x58 && ml >= 2) {
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = 0xfe;
					int den = 1 << (q[1] & 7);
					ev[count].aux = (DWORD)q[0] | ((DWORD)den << 8);
					ev[count].port = curPort; ev[count].sysexOff = -1;
					++count;
				} else if (type == 0x59 && ml >= 2) {
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = 0xfd;
					ev[count].aux = (DWORD)(BYTE)q[0] | ((DWORD)(q[1] & 1) << 8);
					ev[count].port = curPort; ev[count].sysexOff = -1;
					++count;
				} else if (type == 0x21 && ml >= 1) {
					curPort = (int)q[0];
					if (curPort < 0) curPort = 0;
					if (curPort > 1) curPort = 1;
				}
				q += ml;
			} else if (st == 0xf0 || st == 0xf7) {
				unsigned sl = 0;
				if (!MmReadVar(q, end, sl) || q + sl > end) break;
				const int need = (st == 0xf0) ? (1 + (int)sl) : (int)sl;
				if (need > 0 && sxUsed + need <= (int)size + 8) {
					const int off = sxUsed;
					if (st == 0xf0) sxData[sxUsed++] = 0xf0;
					memcpy(sxData + sxUsed, q, sl);
					sxUsed += (int)sl;
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = 0xf0; ev[count].aux = (DWORD)need;
					ev[count].port = curPort; ev[count].sysexOff = off;
					++count;
				}
				q += sl;
			} else {
				const int kind = st & 0xf0;
				const int need = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
				if (q + need > end) break;
				BYTE d1 = q[0], d2 = (need == 2) ? q[1] : 0;
				q += need;
				if (kind >= 0x80 && kind <= 0xe0) {
					ev[count].tick = tick; ev[count].sample = 0;
					ev[count].msg = st | ((DWORD)d1 << 8) | ((DWORD)d2 << 16);
					ev[count].aux = 0;
					ev[count].port = curPort; ev[count].sysexOff = -1;
					++count;
				}
			}
		}
		p = end;
	}
	if (!count) {
		delete[] ev; delete[] data; delete[] sxData; return;
	}
	qsort(ev, count, sizeof(MmEv), MmCmpEv);
	int sr = 44100;
	if (mode == MODE_VST_MIDI) {
		int r = VstMidiGetRate();
		if (r > 0) sr = r;
	} else if (savedata.samples >= 8000) {
		sr = savedata.samples;
	}
	m_sampleRate = sr;
	unsigned __int64 lastTick = 0;
	unsigned tempoU = 500000;
	__int64 sample = 0;
	for (int i = 0; i < count; ++i) {
		const unsigned __int64 dt = ev[i].tick - lastTick;
		sample += (__int64)((dt * (unsigned __int64)tempoU * (unsigned __int64)sr) /
			((unsigned __int64)division * 1000000ULL));
		ev[i].sample = sample;
		lastTick = ev[i].tick;
		if (ev[i].msg == 0xff && ev[i].aux >= 10000)
			tempoU = (unsigned)ev[i].aux;
	}
	m_ev = ev;
	m_evCount = count;
	m_sx = sxData;
	m_sxBytes = sxUsed;
	MmCopyW(m_loadedPath, 520, mid);
	delete[] data;
	m_lastPlayb = -1;
	m_evPos = 0;
}

void CMidiMonitorDlg::SyncFromPlayback()
{
	if (m_frozen) return;
	LoadCurrentMidi();
	if (!m_ev || m_evCount <= 0) return;
	__int64 pb = playb;
	if (pb < 0) pb = 0;
	if (mode == MODE_VST_MIDI) {
		const int vstPcm = (savedata.vstMultiDll[0] || savedata.vstExtraPath[0]);
		if (vstPcm) {
			// 時間表示と同じ「再生カーソル」位置。さらにプラグイン遅延と、
			// DS play cursor がアナログ出力より先に進む分（簡易ピアノロール extra と同じ 700ms）。
			const int sr = (m_sampleRate > 0) ? m_sampleRate : 44100;
			const double sec = OggGetGdiPlaybackTimeSec();
			pb = (__int64)(sec * (double)sr + 0.5);
			pb -= VstMidiGetLatencySamples();
			pb -= (__int64)sr * 700 / 1000;
			if (pb < 0) pb = 0;
		}
	}
	if (pb < m_lastPlayb) {
		ResetParts();
		m_evPos = 0;
	}
	m_lastPlayb = pb;
	while (m_evPos < m_evCount && m_ev[m_evPos].sample <= pb) {
		ApplyEvent(m_ev[m_evPos]);
		m_evPos++;
	}
	UpdateNoteMeter();
}

void CMidiMonitorDlg::DrawVBar(CDC& dc, int x, int y, int bw, int bh, int v0, int vmax, COLORREF col, int glow, int idle)
{
	if (bw < 2 || bh < 2) return;
	dc.FillSolidRect(x, y, bw, bh, RGB(16, 16, 20));
	int v = v0;
	if (v < 0) v = 0;
	if (vmax < 1) vmax = 1;
	if (v > vmax) v = vmax;
	int h = (bh * v) / vmax;
	if (h <= 0) return;
	COLORREF c = col;
	if (idle)
		c = MmMix(RGB(38, 40, 46), col, 40);
	else if (glow > 0)
		c = MmMix(col, RGB(255, 255, 220), glow);
	else
		c = MmMix(RGB(48, 50, 56), col, 150);
	dc.FillSolidRect(x, y + bh - h, bw, h, c);
	if (glow > 40 && h > 2)
		dc.FillSolidRect(x, y + bh - h, bw, 1, MmMix(c, RGB(255, 255, 255), 120));
}

void CMidiMonitorDlg::DrawPanBar(CDC& dc, int x, int y, int bw, int bh, int pan, int glow, int idle)
{
	if (bw < 3 || bh < 2) return;
	dc.FillSolidRect(x, y, bw, bh, RGB(16, 16, 20));
	int mid = x + bw / 2;
	dc.FillSolidRect(mid, y, 1, bh, RGB(70, 70, 36));
	int p = pan;
	if (p < 0) p = 0;
	if (p > 127) p = 127;
	int px = x + (bw - 2) * p / 127;
	COLORREF c = RGB(230, 210, 40);
	if (idle)
		c = RGB(90, 88, 50);
	else if (glow > 0)
		c = MmMix(c, RGB(255, 255, 200), glow);
	dc.FillSolidRect(px, y + 1, 2, bh - 2, c);
}

void CMidiMonitorDlg::DrawMiniKeys(CDC& dc, const CRect& rc, const Part& p)
{
	if (rc.Width() < 20 || rc.Height() < 6) return;
	const int k0 = 21, k1 = 108;
	int whites = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) whites++;
	}
	if (whites < 1) return;
	const int ww = rc.Width();
	const int hh = rc.Height();
	int wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m == 1 || m == 3 || m == 6 || m == 8 || m == 10) continue;
		int x0 = rc.left + wi * ww / whites;
		int x1 = rc.left + (wi + 1) * ww / whites;
		COLORREF c = RGB(228, 228, 232);
		if (p.noteOn[n]) c = RGB(220, 40, 40);
		dc.FillSolidRect(x0, rc.top, max(1, x1 - x0 - 1), hh, c);
		wi++;
	}
	wi = 0;
	for (int n = k0; n <= k1; ++n) {
		const int m = n % 12;
		if (m != 1 && m != 3 && m != 6 && m != 8 && m != 10) { wi++; continue; }
		int xw = rc.left + (wi * ww / whites);
		int bw = max(2, ww / whites * 6 / 10);
		int x0 = xw - bw / 2;
		COLORREF c = RGB(20, 20, 24);
		if (p.noteOn[n]) c = RGB(255, 70, 70);
		dc.FillSolidRect(x0, rc.top, bw, hh * 6 / 10, c);
	}
}

void CMidiMonitorDlg::DrawHeader(CDC& dc, int w, int headH, UINT dpi)
{
	dc.FillSolidRect(0, 0, w, headH, MM_HEAD_BG);
	dc.SetBkMode(TRANSPARENT);
	CFont* oldF = dc.SelectObject(&m_fontHead);
	dc.SetTextColor(MM_HEAD_TX);

	int bpm = 0;
	if (m_usecQn > 0)
		bpm = (int)((60000000.0 / (double)m_usecQn) + 0.5);
	if (bpm < 1) bpm = savedata.mpDetectedBpm;
	m_dispBpm = bpm;
	int tpc = tempo;
	if (tpc < 1) tpc = 100;
	m_transpose = (pitch != 0) ? (int)((double)pitch / 100.0 + (pitch > 0 ? 0.5 : -0.5)) : 0;

	wchar_t keyBuf[40];
	MmKeyName(m_keySf, m_keyMin, keyBuf, 40);
	const wchar_t* fn = m_loadedPath[0] ? m_loadedPath : (LPCWSTR)filen;
	const wchar_t* slash = fn ? wcsrchr(fn, L'\\') : NULL;
	const wchar_t* base = slash ? slash + 1 : (fn ? fn : L"");
	wchar_t line1[400];
	_snwprintf_s(line1, _TRUNCATE, L"BPM %3d    %3d%%    %s    TB %d    %d/%d    Transpose %d    %s",
		bpm, tpc, keyBuf, m_division, m_tsNum, m_tsDen, m_transpose, base);
	dc.TextOut(Scale(8, dpi), Scale(4, dpi), line1);

	const int volBarW = max(40, w / 3);
	const int vx = Scale(8, dpi);
	const int vy = Scale(22, dpi);
	const int vh = Scale(10, dpi);
	m_volBarRc.SetRect(vx, vy, vx + volBarW, vy + vh);
	dc.FillSolidRect(vx, vy, volBarW, vh, RGB(22, 28, 22));
	int pct = m_masterVol;
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	const int fillW = volBarW * pct / 100;
	if (fillW > 0)
		dc.FillSolidRect(vx, vy, fillW, vh, RGB(48, 170, 70));
	dc.FillSolidRect(vx, vy, volBarW, 1, RGB(70, 100, 70));
	dc.FillSolidRect(vx, vy + vh - 1, volBarW, 1, RGB(20, 40, 20));
	for (int k = 1; k < 4; ++k) {
		const int tx = vx + volBarW * k / 4;
		dc.FillSolidRect(tx, vy + 1, 1, vh - 2, RGB(36, 52, 36));
	}
	wchar_t volT[32];
	_snwprintf_s(volT, _TRUNCATE, L"Vol %d%%", pct);
	dc.SetTextColor(RGB(245, 255, 245));
	dc.TextOut(vx + Scale(4, dpi), Scale(21, dpi), volT);

	const int nx = vx + volBarW + Scale(6, dpi);
	const int notesW = Scale(140, dpi);
	m_notesBarRc.SetRect(nx, vy, nx + notesW, vy + vh);
	dc.FillSolidRect(nx, vy, notesW, vh, RGB(14, 22, 32));
	int nScale = 32;
	const int pk = (int)(m_notesPeak + 0.5f);
	if (pk > nScale) nScale = pk;
	if (m_noteCount > nScale) nScale = m_noteCount;
	if (nScale < 1) nScale = 1;
	const int maxW = notesW * pk / nScale;
	const int nFill = notesW * m_noteCount / nScale;
	if (maxW > 0)
		dc.FillSolidRect(nx, vy, maxW, vh, RGB(36, 72, 104));
	if (nFill > 0)
		dc.FillSolidRect(nx, vy, nFill, vh, RGB(90, 170, 220));
	if (pk > 0 && maxW > 1)
		dc.FillSolidRect(nx + maxW - 2, vy, 2, vh, RGB(220, 240, 255));
	dc.FillSolidRect(nx, vy, notesW, 1, RGB(50, 80, 110));
	dc.FillSolidRect(nx, vy + vh - 1, notesW, 1, RGB(16, 24, 36));
	wchar_t nt[40];
	_snwprintf_s(nt, _TRUNCATE, L"Notes %03d  MAX %03d", m_noteCount, pk);
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(nx + Scale(4, dpi), Scale(21, dpi), nt);

	const int dx = nx + notesW + Scale(14, dpi);
	const int dy = Scale(20, dpi);
	const int ds = Scale(12, dpi);
	const int dg = m_drumGlow;
	COLORREF dcol = (dg > 0)
		? MmMix(RGB(70, 16, 16), RGB(255, 50, 40), dg)
		: RGB(52, 20, 20);
	dc.FillSolidRect(dx, dy, ds, ds, dcol);
	dc.FillSolidRect(dx + 1, dy + 1, ds - 2, 1, MmMix(dcol, RGB(255, 180, 160), dg > 0 ? 80 : 20));
	dc.SetTextColor(dg > 40 ? RGB(255, 230, 230) : RGB(140, 90, 90));
	dc.SelectObject(&m_fontTiny);
	dc.TextOut(dx + ds + Scale(3, dpi), dy + 1, L"DRUM");

	dc.SelectObject(&m_fontHead);
	dc.SetTextColor(MM_HEAD_TX);
	const wchar_t* sysN = (m_sysMode == 2) ? L"XG" : (m_sysMode == 1) ? L"GS" : L"GM";
	wchar_t line3[320];
	_snwprintf_s(line3, _TRUNCATE, L"Reverb  %s     Chorus  %s     Variation  %s     SYS  %s     INSERTION 1/2  %s / %s",
		MmEffName(m_sysMode, 0, m_revType),
		MmEffName(m_sysMode, 1, m_choType),
		MmEffName(m_sysMode, 2, m_varType),
		sysN,
		m_ins1 ? L"ON" : L"OFF",
		m_ins2 ? L"ON" : L"OFF");
	dc.TextOut(Scale(8, dpi), Scale(36, dpi), line3);

	dc.SelectObject(&m_fontCell);
	dc.TextOut(Scale(4, dpi), Scale(54, dpi),
		L"CH#   PC# BNK Map     Instrument        Lev  Vol Pan Exp Rev Crs Var   Vibrato        Filter         Envelope        EQ         NRPN   Keyboard");
	dc.SelectObject(&m_fontTiny);
	dc.SetTextColor(RGB(70, 70, 80));
	dc.TextOut(Scale(520, dpi), Scale(66, dpi), L"Rat Dpt Dly     LPF Rsn HPF     Atk Dcy Rls     Low High");
	if (m_frozen) {
		dc.SelectObject(&m_fontHead);
		dc.SetTextColor(RGB(255, 180, 80));
		dc.TextOut(Scale(8, dpi), Scale(4, dpi) + Scale(14, dpi),
			LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد",
				L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu"));
	}
	dc.SelectObject(oldF);
}

void CMidiMonitorDlg::DrawPartRow(CDC& dc, int i, int y, int rowH, int w, UINT dpi)
{
	const Part& p = m_part[i];
	dc.SetBkMode(TRANSPARENT);
	CFont* oldF = dc.SelectObject(&m_fontTiny);
	dc.FillSolidRect(0, y, w, rowH - 1, (i < 16) ? MM_ROW_A : MM_ROW_B);
	dc.FillSolidRect(0, y + rowH - 1, w, 1, MM_GRID);
	const int live = (p.held > 0);
	dc.SetTextColor(live ? RGB(255, 245, 245) : MM_FG);
	wchar_t chs[8];
	_snwprintf_s(chs, _TRUNCATE, L"%c%02d", (i < 16) ? L'A' : L'B', (i % 16) + 1);
	dc.TextOut(Scale(4, dpi), y + 1, chs);
	wchar_t pcb[48];
	_snwprintf_s(pcb, _TRUNCATE, L"%03d %03d %s", p.pc + 1, p.bankMsb, MmMapLabel(m_sysMode, p.mapId));
	dc.TextOut(Scale(40, dpi), y + 1, pcb);
	dc.TextOut(Scale(148, dpi), y + 1, p.name);
	const int meterX = Scale(280, dpi);
	const int bh = rowH - Scale(4, dpi);
	const int by = y + Scale(2, dpi);
	const int bw = Scale(6, dpi);
	DrawVBar(dc, meterX, by, bw, bh, (int)(p.lev * 127.f), 127, RGB(70, 255, 90), 255, 0);
	DrawVBar(dc, meterX + Scale(10, dpi), by, bw, bh, p.vol, 127, RGB(50, 200, 70), p.glowVol, (p.vol == 100 && !p.glowVol) ? 1 : 0);
	DrawPanBar(dc, meterX + Scale(18, dpi), by, Scale(8, dpi), bh, p.pan, p.glowPan, (p.pan == 64 && !p.glowPan) ? 1 : 0);
	DrawVBar(dc, meterX + Scale(28, dpi), by, bw, bh, p.exp, 127, RGB(50, 200, 70), p.glowExp, (p.exp == 127 && !p.glowExp) ? 1 : 0);
	DrawVBar(dc, meterX + Scale(36, dpi), by, bw, bh, p.rev, 127, RGB(210, 50, 50), p.glowRev, (p.rev == 40 && !p.glowRev) ? 1 : 0);
	DrawVBar(dc, meterX + Scale(44, dpi), by, bw, bh, p.crs, 127, RGB(80, 200, 230), p.glowCrs, (p.crs == 0 && !p.glowCrs) ? 1 : 0);
	DrawVBar(dc, meterX + Scale(52, dpi), by, bw, bh, p.var, 127, RGB(50, 80, 200), p.glowVar, (p.var == 0 && !p.glowVar) ? 1 : 0);
	const int numsLive = (p.vibRat | p.vibDpt | p.vibDly | p.lpf | p.rsn | p.hpf | p.atk | p.dcy | p.rls);
	dc.SetTextColor(numsLive ? RGB(220, 220, 230) : RGB(88, 90, 98));
	wchar_t num[96];
	_snwprintf_s(num, _TRUNCATE, L"%+03d %+03d %+03d   %+03d %+03d %+03d   %+03d %+03d %+03d   %d %s",
		p.vibRat, p.vibDpt, p.vibDly, p.lpf, p.rsn, p.hpf, p.atk, p.dcy, p.rls,
		p.eqLow, (p.eqHigh >= 1000) ? L"10k" : L"Hz");
	dc.TextOut(Scale(360, dpi), y + 1, num);
	dc.SetTextColor((p.nrpnMsb | p.nrpnLsb) ? MM_FG : RGB(70, 72, 80));
	wchar_t nr[16];
	_snwprintf_s(nr, _TRUNCATE, L"%02X\n%02X", p.nrpnMsb & 127, p.nrpnLsb & 127);
	CRect nrRc(Scale(640, dpi), y, Scale(668, dpi), y + rowH);
	dc.DrawText(nr, &nrRc, DT_CENTER | DT_WORDBREAK);
	CRect krc(Scale(672, dpi), y + 1, w - Scale(4, dpi), y + rowH - 2);
	DrawMiniKeys(dc, krc, p);
	dc.SelectObject(oldF);
}

void CMidiMonitorDlg::DrawMonitor2D(CDC& dc, int w, int h, UINT dpi)
{
	const int headH = Scale(78, dpi);
	dc.FillSolidRect(0, headH, w, h - headH, MM_BG);
	DrawHeader(dc, w, headH, dpi);
	int bodyH = h - headH;
	if (bodyH < PART_MAX) bodyH = PART_MAX;
	int rowH = bodyH / PART_MAX;
	if (rowH < Scale(10, dpi)) rowH = Scale(10, dpi);
	m_layHeadH = headH;
	m_layRowH = rowH;
	m_layW = w;
	for (int i = 0; i < PART_MAX; ++i)
		DrawPartRow(dc, i, headH + i * rowH, rowH, w, dpi);
}

void CMidiMonitorDlg::TickVisuals()
{
	DWORD live = 0;
	int drumHit = 0;
	for (int i = 0; i < PART_MAX; ++i) {
		Part& p = m_part[i];
		if (p.held <= 0) {
			p.lev *= 0.90f;
			if (p.lev < 0.002f) p.lev = 0;
		}
		MmGlowTick(p.glowVol);
		MmGlowTick(p.glowExp);
		MmGlowTick(p.glowPan);
		MmGlowTick(p.glowRev);
		MmGlowTick(p.glowCrs);
		MmGlowTick(p.glowVar);
		const int busy = (p.held > 0 || p.lev > 0.002f
			|| p.glowVol || p.glowExp || p.glowPan || p.glowRev || p.glowCrs || p.glowVar);
		if (busy) live |= (1u << i);
		if (p.isDrum && p.held > 0)
			drumHit = 1;
	}
	TickNotePeak();
	if (drumHit) m_drumGlow = 255;
	else if (m_drumGlow) {
		m_drumGlow = m_drumGlow * 5 / 6;
		if (m_drumGlow < 8) m_drumGlow = 0;
		m_dirtyHead = true;
	}
	m_dirtyRows |= live | m_rowLive;
	m_rowLive = live;
}

int CMidiMonitorDlg::AppVolPercent() const
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return m_masterVol;
	int mn = 0, mx = 100000;
	og->m_sl.GetRange(mn, mx);
	if (mx <= mn) return 100;
	int pct = (og->m_sl.GetPos() - mn) * 100 / (mx - mn);
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	return pct;
}

void CMidiMonitorDlg::SetAppVolPercent(int pct)
{
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	int mn = 0, mx = 100000;
	og->m_sl.GetRange(mn, mx);
	if (mx <= mn) return;
	og->m_sl.SetPos(mn + pct * (mx - mn) / 100);
	m_masterVol = pct;
	m_dirtyHead = true;
}

void CMidiMonitorDlg::PollAppVolume()
{
	const int pct = AppVolPercent();
	if (pct != m_masterVol) {
		m_masterVol = pct;
		m_dirtyHead = true;
	}
}

void CMidiMonitorDlg::UpdateNoteMeter()
{
	int notes = 0;
	for (int i = 0; i < PART_MAX; ++i)
		notes += m_part[i].held;
	if (notes < 0) notes = 0;
	if (notes != m_noteCount) {
		m_noteCount = notes;
		m_dirtyHead = true;
	}
	if ((float)m_noteCount > m_notesPeak) {
		m_notesPeak = (float)m_noteCount;
		m_notesPeakHold = 45;
		m_dirtyHead = true;
	}
}

void CMidiMonitorDlg::TickNotePeak()
{
	UpdateNoteMeter();
	if ((float)m_noteCount > m_notesPeak) {
		m_notesPeak = (float)m_noteCount;
		m_notesPeakHold = 45;
		m_dirtyHead = true;
		return;
	}
	if (m_notesPeakHold > 0) {
		m_notesPeakHold--;
		return;
	}
	if (m_notesPeak > (float)m_noteCount + 0.02f) {
		m_notesPeak -= 0.16f;
		if (m_notesPeak < (float)m_noteCount)
			m_notesPeak = (float)m_noteCount;
		m_dirtyHead = true;
	} else {
		m_notesPeak = (float)m_noteCount;
	}
}

void CMidiMonitorDlg::LatchPart(int part, BYTE bit)
{
	if (part < 0 || part >= PART_MAX || !bit) return;
	m_latchMask[part] |= bit;
	m_latchUntil[part] = GetTickCount() + 2500;
}

bool CMidiMonitorDlg::IsLatched(int part, BYTE bit) const
{
	if (part < 0 || part >= PART_MAX || !bit) return false;
	if ((m_latchMask[part] & bit) == 0) return false;
	if ((int)(GetTickCount() - m_latchUntil[part]) >= 0) return false;
	return true;
}

void CMidiMonitorDlg::InjectShort(int part, DWORD msg)
{
	if (part < 0 || part >= PART_MAX) return;
	const int port = part / 16;
	const int ch = part % 16;
	msg = (msg & ~(DWORD)0x0f) | (DWORD)ch;
	VstMidiInjectShort(port, msg);
	ApplyShort(port, msg);
	const int st = (int)(msg & 0xf0);
	if (st == 0xb0) {
		const int cc = (int)((msg >> 8) & 0x7f);
		BYTE bit = 0;
		if (cc == 7) bit = MM_LATCH_VOL;
		else if (cc == 10) bit = MM_LATCH_PAN;
		else if (cc == 11) bit = MM_LATCH_EXP;
		else if (cc == 91) bit = MM_LATCH_REV;
		else if (cc == 93) bit = MM_LATCH_CRS;
		else if (cc == 94) bit = MM_LATCH_VAR;
		LatchPart(part, bit);
	} else if (st == 0xc0) {
		LatchPart(part, MM_LATCH_PC);
	}
	UpdateNoteMeter();
}

void CMidiMonitorDlg::ReleasePlayNote()
{
	if (m_playPart < 0 || m_playNote < 0) return;
	const int ch = m_playPart % 16;
	const DWORD msg = (DWORD)(0x80 | ch) | ((DWORD)(m_playNote & 127) << 8);
	InjectShort(m_playPart, msg);
	m_playPart = -1;
	m_playNote = -1;
}

int CMidiMonitorDlg::KeyAt(const CRect& rc, CPoint pt) const
{
	if (!rc.PtInRect(pt) || rc.Width() < 20 || rc.Height() < 6) return -1;
	const int k0 = 21, k1 = 108;
	int whites = 0;
	for (int n = k0; n <= k1; ++n) {
		if (!MmIsBlackKey(n)) whites++;
	}
	if (whites < 1) return -1;
	const int ww = rc.Width();
	const int hh = rc.Height();
	if (pt.y < rc.top + hh * 6 / 10) {
		int wi = 0;
		for (int n = k0; n <= k1; ++n) {
			if (!MmIsBlackKey(n)) { wi++; continue; }
			int xw = rc.left + (wi * ww / whites);
			int bw = max(2, ww / whites * 6 / 10);
			int x0 = xw - bw / 2;
			if (pt.x >= x0 && pt.x < x0 + bw) return n;
		}
	}
	int wi = 0;
	for (int n = k0; n <= k1; ++n) {
		if (MmIsBlackKey(n)) continue;
		int x0 = rc.left + wi * ww / whites;
		int x1 = rc.left + (wi + 1) * ww / whites;
		if (pt.x >= x0 && pt.x < x1) return n;
		wi++;
	}
	return -1;
}

int CMidiMonitorDlg::HitMonitor(CPoint clientPt, int& part, CRect& cell) const
{
	part = -1;
	cell.SetRectEmpty();
	if (IsView3D() || m_layHeadH <= 0 || m_layRowH <= 0) return MM_HIT_NONE;
	CPoint f = clientPt;
	f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
	if (!m_volBarRc.IsRectEmpty() && m_volBarRc.PtInRect(f))
		return MM_HIT_APPVOL;
	if (f.y < m_layHeadH) return MM_HIT_NONE;
	const int row = (f.y - m_layHeadH) / m_layRowH;
	if (row < 0 || row >= PART_MAX) return MM_HIT_NONE;
	part = row;
	const UINT dpi = WindowDpi();
	const int y = m_layHeadH + row * m_layRowH;
	const int w = m_layW;
	const int meterX = Scale(280, dpi);
	const int bh = m_layRowH - Scale(4, dpi);
	const int by = y + Scale(2, dpi);
	const int bw = Scale(6, dpi);
	auto hitBar = [&](int x, int bwBar, CRect& out) -> bool {
		out.SetRect(x, by, x + bwBar, by + bh);
		return f.x >= x && f.x < x + bwBar && f.y >= by && f.y < by + bh;
	};
	if (hitBar(meterX + Scale(10, dpi), bw, cell)) return MM_HIT_VOL;
	if (hitBar(meterX + Scale(18, dpi), Scale(8, dpi), cell)) return MM_HIT_PAN;
	if (hitBar(meterX + Scale(28, dpi), bw, cell)) return MM_HIT_EXP;
	if (hitBar(meterX + Scale(36, dpi), bw, cell)) return MM_HIT_REV;
	if (hitBar(meterX + Scale(44, dpi), bw, cell)) return MM_HIT_CRS;
	if (hitBar(meterX + Scale(52, dpi), bw, cell)) return MM_HIT_VAR;
	CRect krc(Scale(672, dpi), y + 1, w - Scale(4, dpi), y + m_layRowH - 2);
	if (krc.PtInRect(f)) { cell = krc; return MM_HIT_KEYS; }
	CRect pcRc(Scale(40, dpi), y, Scale(140, dpi), y + m_layRowH);
	if (pcRc.PtInRect(f)) { cell = pcRc; return MM_HIT_PC; }
	return MM_HIT_NONE;
}

void CMidiMonitorDlg::ApplyDragValue(CPoint clientPt)
{
	if (m_dragPart < 0 || m_dragPart >= PART_MAX) return;
	const int cc = MmCcForHit(m_dragKind);
	if (cc < 0) return;
	CPoint f = clientPt;
	f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
	const UINT dpi = WindowDpi();
	CRect cell;
	int part = m_dragPart;
	HitMonitor(clientPt, part, cell);
	part = m_dragPart;
	const int y = m_layHeadH + part * m_layRowH;
	const int bh = m_layRowH - Scale(4, dpi);
	const int by = y + Scale(2, dpi);
	int v = 0;
	if (m_dragKind == MM_HIT_PAN) {
		const int meterX = Scale(280, dpi);
		const int x0 = meterX + Scale(18, dpi);
		const int bw = Scale(8, dpi);
		if (bw > 0) v = (f.x - x0) * 127 / bw;
	} else {
		if (bh > 0) v = (by + bh - f.y) * 127 / bh;
	}
	if (v < 0) v = 0;
	if (v > 127) v = 127;
	const int ch = part % 16;
	const DWORD msg = (DWORD)(0xb0 | ch) | ((DWORD)cc << 8) | ((DWORD)v << 16);
	InjectShort(part, msg);
	InvalidateDirty();
}

bool CMidiMonitorDlg::HitVolBar(CPoint clientPt) const
{
	if (IsView3D() || m_volBarRc.IsRectEmpty()) return false;
	CPoint f = clientPt;
	f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
	return m_volBarRc.PtInRect(f) ? true : false;
}

void CMidiMonitorDlg::InvalidateDirty()
{
	if (!::IsWindow(m_hWnd)) return;
	if (m_fullDraw || IsView3D()) {
		Invalidate(FALSE);
		return;
	}
	if (!m_dirtyHead && m_dirtyRows == 0)
		return;
	CRect rc;
	GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int w = rc.Width();
	if (m_dirtyHead && m_layHeadH > 0) {
		CRect r(0, capH, w, capH + m_layHeadH);
		InvalidateRect(&r, FALSE);
	}
	if (m_layRowH > 0 && m_layHeadH > 0) {
		for (int i = 0; i < PART_MAX; ++i) {
			if ((m_dirtyRows & (1u << i)) == 0) continue;
			const int y = capH + m_layHeadH + i * m_layRowH;
			CRect r(0, y, w, y + m_layRowH);
			InvalidateRect(&r, FALSE);
		}
	} else {
		Invalidate(FALSE);
	}
}

void CMidiMonitorDlg::DrawMonitor3D(CDC& dc, int w, int h)
{
	dc.FillSolidRect(0, 0, w, h, MM_BG);
	GdiSoft3D::View v;
	const float boxes[1][6] = { { -1.25f, 1.25f, -0.05f, 0.85f, -0.05f, 1.15f } };
	GdiSoft3D::BuildView(w, h, m_cam, boxes, 1, v);
	GdiSoft3D::Context ctx;
	if (!ctx.Create(w, h)) return;
	ctx.view = v;
	ctx.SetFog(GdiSoft3D::FogLinear, MM_BG, 0.05f, 1.2f, 0.7f);
	ctx.BeginFrame(MM_BG);
	ctx.DrawGrid(-1.1f, 1.1f, 0.0f, 1.05f, 0.0f, 6, RGB(40, 44, 58));
	for (int i = 0; i < PART_MAX; ++i) {
		const int bank = i / 16;
		const int col = i % 16;
		const float x0 = -1.00f + col * (2.00f / 16.f);
		const float x1 = x0 + (2.00f / 16.f) * 0.72f;
		const float z0 = (bank == 0) ? 0.18f : 0.58f;
		const float z1 = z0 + 0.28f;
		float lv = m_part[i].lev;
		if (lv < 0.04f) lv = 0.04f;
		const float y = 0.08f + lv * 0.70f;
		COLORREF c = (bank == 0) ? RGB(80, 210, 255) : RGB(255, 150, 90);
		if (m_part[i].held > 0) c = RGB(255, 80, 80);
		ctx.DrawBox(x0, x1, y, z0, z1, c, 0.f);
	}
	ctx.EndFrame();
	ctx.Present(dc, 0, 0);
	if (m_soft3dTourUntil != 0 && GetTickCount() < m_soft3dTourUntil) {
		dc.SetBkMode(TRANSPARENT);
		dc.SetTextColor(RGB(240, 245, 255));
		dc.TextOut(8, 8, LL14(
			L"ドラッグ=回転 ホイール=ズーム 0=リセット",
			L"Drag=rotate  Wheel=zoom  0=reset",
			L"Glisser=rotation  Molette=zoom  0=reinit.",
			L"Trascina=ruota  Rotella=zoom  0=reset",
			L"Arrastrar=rotar  Rueda=zoom  0=restablecer",
			L"드래그=회전  휠=줌  0=리셋",
			L"拖动=旋转  滚轮=缩放  0=重置",
			L"سحب=دوران  عجلة=تكبير  0=إعادة",
			L"Перетащ.=поворот  Колесо=масштаб  0=сброс",
			L"Ziehen=drehen  Rad=Zoom  0=Reset",
			L"Arrastar=girar  Roda=zoom  0=redefinir",
			L"Sleep=draaien  Wiel=zoom  0=reset",
			L"Przeciagnij=obrot  Kolo=zoom  0=reset",
			L"Surukle=don  Tekerlek=zoom  0=sifirla"));
	}
	if ((savedata.soft3dPerfHintDismiss & 16) == 0) {
		static DWORD s_t0 = 0;
		static int s_slow = 0;
		static DWORD s_hint = 0;
		const DWORD now = GetTickCount();
		if (s_t0 != 0) {
			if (now - s_t0 >= 32) { if (++s_slow >= 40) { s_slow = 0; s_hint = now + 4000; } }
			else if (s_slow > 0) --s_slow;
		}
		s_t0 = now;
		if (now < s_hint) {
			dc.SetBkMode(TRANSPARENT);
			dc.SetTextColor(RGB(255, 190, 160));
			dc.TextOut(8, 26, LL14(
				L"重いときは右クリックから 2D に戻せます",
				L"Feeling heavy? Switch back to 2D from the context menu",
				L"Trop lourd ? Revenez en 2D via le menu contextuel",
				L"Troppo pesante? Torna al 2D dal menu contestuale",
				L"¿Va lento? Vuelva a 2D desde el menú contextual",
				L"무거우면 우클릭에서 2D로 돌릴 수 있습니다",
				L"若觉得卡，可从右键菜单切回 2D",
				L"ثقيل؟ عد إلى 2D من قائمة السياق",
				L"Тяжело? Вернитесь в 2D через контекстное меню",
				L"Zu zäh? Über das Kontextmenü zurück zu 2D",
				L"Pesado? Volte ao 2D pelo menu de contexto",
				L"Te zwaar? Ga terug naar 2D via het contextmenu",
				L"Za ciężko? Wróć do 2D z menu kontekstowego",
				L"Ağır mı? Bağlam menüsünden 2B'ye dönün"));
		}
	}
}

BOOL CMidiMonitorDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	SetWindowText(LL14(
		L"MIDIモニタ", L"MIDI Monitor", L"Moniteur MIDI", L"Monitor MIDI", L"Monitor MIDI",
		L"MIDI 모니터", L"MIDI监视器", L"مراقب MIDI", L"MIDI-монитор", L"MIDI-Monitor",
		L"Monitor MIDI", L"MIDI-monitor", L"Monitor MIDI", L"MIDI izleyici"));
	ModifyStyle(WS_MINIMIZEBOX, 0);
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);

	m_viewMode = (savedata.midimonviewmode == 1) ? 1 : 0;
	m_alwaysOnTop = (savedata.midimontopmost != 0);
	SyncSoft3DFromSave();
	ResetParts();
	MmEnsureDat();

	const UINT dpi = WindowDpi();
	int dw = Scale(1180, dpi), dh = Scale(720, dpi);
	if (savedata.midimonx != -1 && savedata.midimonw > 200 && savedata.midimonh > 160)
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndTop,
			savedata.midimonx, savedata.midimony, savedata.midimonw, savedata.midimonh,
			SWP_NOOWNERZORDER | (m_alwaysOnTop ? 0 : SWP_NOZORDER));
	else
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndTop,
			80, 80, dw, dh,
			SWP_NOOWNERZORDER | (m_alwaysOnTop ? 0 : SWP_NOZORDER));

	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();
	if (m_tooltip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX)) {
		if (m_help.GetSafeHwnd())
			m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
		m_tooltip.SetDelayTime(TTDT_INITIAL, 400);
		m_tooltip.SetDelayTime(TTDT_RESHOW, 120);
		m_tooltip.SetDelayTime(TTDT_AUTOPOP, 12000);
		m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 460);
		m_tooltip.Activate(TRUE);
	}
	EnableToolTips(TRUE);
	EnableMainWindowLock(&savedata.midimonMainLock, TRUE);
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	SetTimer(1, 16, nullptr);
	LoadCurrentMidi();
	return TRUE;
}

void CMidiMonitorDlg::OnPaint()
{
	CPaintDC dc(this);
	if (m_paintDisabled) return;
	CRect rect;
	GetClientRect(&rect);
	const int w = rect.Width();
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int h = rect.Height() - capH;
	if (w <= 0 || h <= 0) {
		CCC_CaptionPaint(dc, m_hWnd);
		return;
	}
	if (!m_frozen)
		SyncFromPlayback();

	const UINT dpi = WindowDpi();
	if (m_fontDpi != (int)dpi || !m_fontTiny.GetSafeHandle()) {
		if (m_fontHead.GetSafeHandle()) m_fontHead.DeleteObject();
		if (m_fontCell.GetSafeHandle()) m_fontCell.DeleteObject();
		if (m_fontTiny.GetSafeHandle()) m_fontTiny.DeleteObject();
		LOGFONT lf = {};
		lf.lfHeight = -Scale(12, dpi);
		lf.lfWeight = FW_BOLD;
		wcscpy_s(lf.lfFaceName, L"MS Gothic");
		m_fontHead.CreateFontIndirect(&lf);
		lf.lfHeight = -Scale(11, dpi);
		lf.lfWeight = FW_NORMAL;
		m_fontCell.CreateFontIndirect(&lf);
		lf.lfHeight = -Scale(10, dpi);
		m_fontTiny.CreateFontIndirect(&lf);
		m_fontDpi = (int)dpi;
		m_fullDraw = true;
	}

	if (!EnsureFrameBuffer(dc, w, h) || !m_frameDC.GetSafeHdc()) {
		dc.FillSolidRect(0, capH, w, h, MM_BG);
		CCC_CaptionPaint(dc, m_hWnd);
		return;
	}

	const bool full = m_fullDraw || IsView3D();
	if (full) {
		if (IsView3D())
			DrawMonitor3D(m_frameDC, w, h);
		else
			DrawMonitor2D(m_frameDC, w, h, dpi);
		m_fullDraw = false;
		m_dirtyRows = 0;
		m_dirtyHead = false;
	} else {
		if (m_layHeadH <= 0 || m_layRowH <= 0)
			DrawMonitor2D(m_frameDC, w, h, dpi);
		else {
			if (m_dirtyHead)
				DrawHeader(m_frameDC, w, m_layHeadH, dpi);
			for (int i = 0; i < PART_MAX; ++i) {
				if (m_dirtyRows & (1u << i))
					DrawPartRow(m_frameDC, i, m_layHeadH + i * m_layRowH, m_layRowH, w, dpi);
			}
		}
		m_dirtyRows = 0;
		m_dirtyHead = false;
	}

	CRect pr = dc.m_ps.rcPaint;
	if (pr.IsRectEmpty()) {
		pr.SetRect(0, capH, w, capH + h);
	}
	int sx = pr.left;
	int sy = pr.top - capH;
	int sw = pr.Width();
	int sh = pr.Height();
	if (sy < 0) { sh += sy; sy = 0; }
	if (sx < 0) { sw += sx; sx = 0; }
	if (sx + sw > w) sw = w - sx;
	if (sy + sh > h) sh = h - sy;
	if (sw <= 0 || sh <= 0) {
		CCC_CaptionPaint(dc, m_hWnd);
		return;
	}

#if CCUSTOM_AERO_SUPPORT
	const bool bodyAero = (savedata.aero == 1 && CCC_IsWin11());
	const bool capGlassBody = (!bodyAero && CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11());
	if (bodyAero || capGlassBody) {
		if (m_chromaW != w || m_chromaH != h) {
			m_chromaCache.Release();
			m_chromaReady = false;
			m_chromaW = w;
			m_chromaH = h;
		}
		if (m_chromaCache.Ensure(dc.GetSafeHdc(), w, h)) {
			if (bodyAero)
				m_chromaCache.UpdateRect(m_frameDC.GetSafeHdc(), 0, 0, 0, 0, w, h, MM_CHROMA);
			else
				m_chromaCache.UpdateOpaqueRect(m_frameDC.GetSafeHdc(), 0, 0, 0, 0, w, h);
			m_chromaReady = true;
			m_chromaCache.BlitFull(dc.GetSafeHdc(), 0, capH, w, h);
			CCC_CaptionPaint(dc, m_hWnd);
			return;
		}
	}
	if (!CCC_IsAeroEnabled() && CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11()) {
		CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, capH, w, h,
			m_frameDC.GetSafeHdc(), 0, 0, w, h);
		CCC_CaptionPaint(dc, m_hWnd);
		return;
	}
#endif
	dc.BitBlt(sx, capH + sy, sw, sh, &m_frameDC, sx, sy, SRCCOPY);
	CCC_CaptionPaint(dc, m_hWnd);
}

BOOL CMidiMonitorDlg::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

void CMidiMonitorDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) {
		if (++m_persistAge >= 32) {
			PersistPos();
			m_persistAge = 0;
		}
		if (!IsIconic() && IsWindowVisible() && !m_paintDisabled) {
			if (m_playNote >= 0 && ::GetCapture() != m_hWnd)
				ReleasePlayNote();
			if (!m_frozen) {
				SyncFromPlayback();
				TickVisuals();
				PollAppVolume();
			}
			if (m_volDragging)
				PollAppVolume();
			InvalidateDirty();
		}
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CMidiMonitorDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	ReleasePaintBuffers();
#if CCUSTOM_AERO_SUPPORT
	if (nType != SIZE_MINIMIZED && CCC_IsAeroEnabled())
		CCC_RefreshDwmBlur(m_hWnd);
#endif
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
	Invalidate(FALSE);
}

void CMidiMonitorDlg::OnMove(int x, int y)
{
	CCustomBlurDialogExBase::OnMove(x, y);
}

void CMidiMonitorDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogExBase::OnShowWindow(bShow, nStatus);
	if (bShow) {
		LoadCurrentMidi();
		Invalidate(FALSE);
	}
}

void CMidiMonitorDlg::OnClose()
{
	DetachForDestroy();
	savedata.midimonwindow = 0;
	DestroyWindow();
}

void CMidiMonitorDlg::OnDestroy()
{
	KillTimer(1);
	ReleasePlayNote();
	PersistPos();
	CCC_CaptionUnregister(m_hWnd);
	CCustomBlurDialogExBase::OnDestroy();
}

void CMidiMonitorDlg::DetachForDestroy()
{
	m_paintDisabled = true;
	KillTimer(1);
	PersistPos();
	ReleasePaintBuffers();
}

void CMidiMonitorDlg::ResetPlaybackState()
{
	ResetParts();
	m_lastPlayb = -1;
	m_evPos = 0;
	m_loadedPath[0] = 0;
	if (::IsWindow(m_hWnd))
		Invalidate(FALSE);
}

void CMidiMonitorDlg::PumpSyncNow()
{
	if (!::IsWindow(m_hWnd) || m_paintDisabled) return;
	if (!m_frozen) {
		SyncFromPlayback();
		PollAppVolume();
	}
	InvalidateDirty();
}

void CMidiMonitorDlg::PersistPos()
{
	if (!::IsWindow(m_hWnd) || IsIconic()) return;
	CRect rc; GetWindowRect(&rc);
	savedata.midimonx = rc.left;
	savedata.midimony = rc.top;
	savedata.midimonw = rc.Width();
	savedata.midimonh = rc.Height();
}

void CMidiMonitorDlg::SyncSoft3DFromSave()
{
	GdiSoft3D::CamFromSaved(m_cam, savedata.midimon3dyaw, savedata.midimon3dpitch, savedata.midimon3dzoom);
}

void CMidiMonitorDlg::PersistSoft3D()
{
	GdiSoft3D::CamToSaved(m_cam, savedata.midimon3dyaw, savedata.midimon3dpitch, savedata.midimon3dzoom);
}

void CMidiMonitorDlg::PaletteApplySoft3D()
{
	m_viewMode = (savedata.midimonviewmode == 1) ? 1 : 0;
	SyncSoft3DFromSave();
	if (::IsWindow(m_hWnd))
		Invalidate(FALSE);
}

void CMidiMonitorDlg::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CMidiMonitorDlg::ShowHelpSheet()
{
	if (g_mmHelpDlg && ::IsWindow(g_mmHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_mmHelpDlg, this);
		return;
	}
	if (g_mmHelpDlg && !::IsWindow(g_mmHelpDlg->GetSafeHwnd()))
		g_mmHelpDlg = nullptr;
	CMmHelpDlg* dlg = new CMmHelpDlg(this);
	if (!dlg->Create(IDD_MM_HELP, this)) {
		delete dlg;
		return;
	}
	g_mmHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CMidiMonitorDlg::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CMidiMonitorDlg::OnContextMenu(CWnd* /*pWnd*/, CPoint point)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	CCustomPopupMenu* view = menu.AddSubMenu(
		LL14(L"表示モード", L"View mode", L"Mode d'affichage", L"Modalita", L"Modo", L"표시 모드", L"显示模式", L"وضع العرض", L"Режим", L"Ansicht", L"Modo", L"Weergave", L"Tryb", L"Gorunum"),
		LL14(L"通常2Dと簡易3Dを切り替えます", L"Switch between normal 2D and Soft 3D", L"Basculer 2D / Soft 3D", L"Passa 2D / Soft 3D", L"Cambiar 2D / Soft 3D", L"일반 2D와 간이 3D 전환", L"在普通2D与简易3D间切换", L"التبديل بين 2D و Soft 3D", L"Переключение 2D / Soft 3D", L"Zwischen 2D und Soft 3D wechseln", L"Alternar 2D / Soft 3D", L"Wisselen 2D / Soft 3D", L"Przelacz 2D / Soft 3D", L"2D / Soft 3B degistir"));
	if (view) {
		view->AddCheck(IDM_MM_VIEW_2D, LL14(L"通常 (2D)", L"Normal (2D)", L"Normal (2D)", L"Normale (2D)", L"Normal (2D)", L"일반 (2D)", L"普通 (2D)", L"عادي (2D)", L"Обычный (2D)", L"Normal (2D)", L"Normal (2D)", L"Normaal (2D)", L"Zwykly (2D)", L"Normal (2D)"), !IsView3D());
		view->AddCheck(IDM_MM_VIEW_3D, LL14(L"簡易3D", L"Soft 3D", L"3D simplifie", L"3D semplificato", L"3D simple", L"간이 3D", L"简易3D", L"Soft 3D", L"Простой 3D", L"Einfaches 3D", L"3D simples", L"Eenvoudig 3D", L"Uproszczone 3D", L"Basit 3B"), IsView3D());
		if (IsView3D()) {
			int yaw10 = (int)(m_cam.yawDeg * 10.f);
			int pit10 = (int)(m_cam.pitchDeg * 10.f);
			int zoomPct = (int)(m_cam.zoom * 100.f + 0.5f);
			view->AddSeparator();
			view->AddSlider(LL14(L"Yaw (0.1°)", L"Yaw (0.1°)", L"Lacet (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"偏航 (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)"),
				-1800, 1800, yaw10, [](void* ctx, int v) {
					auto* self = (CMidiMonitorDlg*)ctx;
					self->m_cam.yawDeg = (float)v / 10.f;
					GdiSoft3D::ClampCam(self->m_cam);
					self->PersistSoft3D();
					self->Invalidate(FALSE);
				}, this);
			view->AddSlider(LL14(L"Pitch (0.1°)", L"Pitch (0.1°)", L"Tangage (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"俯仰 (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)"),
				-850, 850, pit10, [](void* ctx, int v) {
					auto* self = (CMidiMonitorDlg*)ctx;
					self->m_cam.pitchDeg = (float)v / 10.f;
					GdiSoft3D::ClampCam(self->m_cam);
					self->PersistSoft3D();
					self->Invalidate(FALSE);
				}, this);
			view->AddSlider(LL14(L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"缩放 (%)", L"تكبير (%)", L"Масштаб (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)"),
				35, 400, zoomPct, [](void* ctx, int v) {
					auto* self = (CMidiMonitorDlg*)ctx;
					self->m_cam.zoom = (float)v / 100.f;
					GdiSoft3D::ClampCam(self->m_cam);
					self->PersistSoft3D();
					self->Invalidate(FALSE);
				}, this);
			view->AddCommand(IDM_MM_CAM_RESET,
				LL14(L"視点をリセット", L"Reset view", L"Reinitialiser la vue", L"Reimposta vista", L"Restablecer vista", L"시점 재설정", L"重置视角", L"إعادة ضبط العرض", L"Сбросить вид", L"Ansicht zuruecksetzen", L"Redefinir vista", L"Weergave resetten", L"Resetuj widok", L"Gorunumu sifirla"));
		}
	}
	CCustomPopupMenu* ops = menu.AddSubMenu(
		LL14(L"操作", L"Actions", L"Actions", L"Azioni", L"Acciones", L"조작", L"操作", L"إجراءات", L"Действия", L"Aktionen", L"Acoes", L"Acties", L"Akcje", L"Islemler"),
		LL14(L"フリーズ、最前面、状態コピー、再読込", L"Freeze, always on top, copy state, reload", L"Gel, premier plan, copier, recharger", L"Congela, primo piano, copia, ricarica", L"Congelar, siempre visible, copiar, recargar", L"프리즈, 항상 위, 상태 복사, 다시 읽기", L"冻结、置顶、复制状态、重新读取", L"تجميد، أعلى، نسخ، إعادة تحميل", L"Заморозка, поверх, копия, перечитать", L"Einfrieren, immer oben, kopieren, neu laden", L"Congelar, topo, copiar, recarregar", L"Bevriezen, bovenop, kopieren, herladen", L"Zamroz, na wierzchu, kopiuj, wczytaj ponownie", L"Dondur, ustte, kopyala, yeniden yukle"));
	if (ops) {
		ops->AddCheck(IDM_MM_FREEZE, LL14(L"フリーズ", L"Freeze", L"Gel", L"Congela", L"Congelar", L"정지", L"冻结", L"تجميد", L"Заморозка", L"Einfrieren", L"Congelar", L"Bevriezen", L"Zamroz", L"Dondur"), m_frozen);
		ops->AddCheck(IDM_MM_TOPMOST, LL14(L"常に手前に表示", L"Always on top", L"Toujours au premier plan", L"Sempre in primo piano", L"Siempre visible", L"항상 위", L"始终置顶", L"دائماً أعلى", L"Поверх всех окон", L"Immer im Vordergrund", L"Sempre no topo", L"Altijd boven", L"Zawsze na wierzchu", L"Her zaman ustte"), m_alwaysOnTop);
		ops->AddCommand(IDM_MM_COPY, LL14(L"状態をコピー", L"Copy state", L"Copier l'etat", L"Copia stato", L"Copiar estado", L"상태 복사", L"复制状态", L"نسخ الحالة", L"Копировать состояние", L"Zustand kopieren", L"Copiar estado", L"Status kopieren", L"Kopiuj stan", L"Durumu kopyala"));
		ops->AddCommand(IDM_MM_RELOAD, LL14(L"MIDIを再読込", L"Reload MIDI", L"Recharger MIDI", L"Ricarica MIDI", L"Recargar MIDI", L"MIDI 다시 읽기", L"重新读取 MIDI", L"إعادة تحميل MIDI", L"Перечитать MIDI", L"MIDI neu laden", L"Recarregar MIDI", L"MIDI herladen", L"Wczytaj MIDI ponownie", L"MIDI'yi yeniden yukle"));
	}
	CCustomPopupMenu* map = menu.AddSubMenu(
		LL14(L"音色マップ", L"Tone map", L"Carte de timbres", L"Mappa timbri", L"Mapa de timbres", L"음색 맵", L"音色映射", L"خريطة الأصوات", L"Карта тембров", L"Klangkarte", L"Mapa de timbres", L"Klankkaart", L"Mapa barw", L"Timbir haritasi"),
		LL14(L"名前引きに使う GS/XG マップ", L"GS/XG map used for instrument names", L"Carte GS/XG pour les noms", L"Mappa GS/XG per i nomi", L"Mapa GS/XG para nombres", L"이름에 쓸 GS/XG 맵", L"用于查名的 GS/XG 映射", L"خريطة GS/XG للأسماء", L"Карта GS/XG для имён", L"GS/XG-Map fuer Namen", L"Mapa GS/XG para nomes", L"GS/XG-map voor namen", L"Mapa GS/XG do nazw", L"Isimler icin GS/XG haritasi"));
	if (map) {
		map->AddCheck(IDM_MM_MAP_AUTO, LL14(L"自動 (SysEx)", L"Auto (SysEx)", L"Auto (SysEx)", L"Auto (SysEx)", L"Auto (SysEx)", L"자동 (SysEx)", L"自动 (SysEx)", L"تلقائي (SysEx)", L"Авто (SysEx)", L"Auto (SysEx)", L"Auto (SysEx)", L"Auto (SysEx)", L"Auto (SysEx)", L"Otomatik (SysEx)"), m_mapForce == 0);
		map->AddCheck(IDM_MM_MAP_GS, L"GS", m_mapForce == 1);
		map->AddCheck(IDM_MM_MAP_XG, L"XG", m_mapForce == 2);
	}
	CCustomPopupMenu* openSub = menu.AddSubMenu(
		LL14(L"開く", L"Open", L"Ouvrir", L"Apri", L"Abrir", L"열기", L"打开", L"فتح", L"Открыть", L"Offnen", L"Abrir", L"Openen", L"Otworz", L"Ac"),
		LL14(L"イコライザ／ピアノロール／アナライザ／操作ガイド", L"Equalizer / piano roll / analyzer / guide", L"Egaliseur / piano roll / analyseur / guide", L"Equalizzatore / piano roll / analizzatore / guida", L"Ecualizador / piano roll / analizador / guia", L"이퀄라이저/피아노 롤/분석기/가이드", L"均衡器/钢琴卷帘/分析器/指南", L"المعادل / البيانو / المحلل / الدليل", L"Эквалайзер / пианоролл / анализатор / руководство", L"Equalizer / Piano-Roll / Analyzer / Anleitung", L"Equalizador / piano roll / analisador / guia", L"Equalizer / piano-roll / analyser / gids", L"Equalizer / piano roll / analizator / przewodnik", L"Equalizer / piano roll / analizor / kilavuz"));
	if (openSub) {
		openSub->AddCommand(ID_MP_OPEN_EQ, LL14(L"イコライザを開く", L"Open equalizer", L"Ouvrir l'egaliseur", L"Apri equalizzatore", L"Abrir ecualizador", L"이퀄라이저 열기", L"打开均衡器", L"فتح المعادل", L"Открыть эквалайзер", L"Equalizer öffnen", L"Abrir equalizador", L"Equalizer openen", L"Otworz equalizer", L"Equalizeri ac"));
		openSub->AddCommand(ID_MP_OPEN_PIANOROLL, LL14(L"ピアノロールを開く", L"Open piano roll", L"Ouvrir le piano roll", L"Apri piano roll", L"Abrir piano roll", L"피아노 롤 열기", L"打开钢琴卷帘", L"فتح لفافة البيانو", L"Открыть пианоролл", L"Piano-Roll öffnen", L"Abrir piano roll", L"Piano-roll openen", L"Otworz piano roll", L"Piyano rolunu ac"));
		openSub->AddCommand(ID_MP_OPEN_ANALYZER, LL14(L"アナライザを開く", L"Open analyzer", L"Ouvrir l'analyseur", L"Apri analizzatore", L"Abrir analizador", L"분석기 열기", L"打开分析器", L"فتح المحلل", L"Открыть анализатор", L"Analyzer öffnen", L"Abrir analisador", L"Analyzer openen", L"Otworz analizator", L"Analizoru ac"));
		openSub->AddCommand(ID_HELP_SHOWSHEET, LL14(L"操作ガイド", L"Operation guide", L"Guide d'utilisation", L"Guida operativa", L"Guía de operación", L"조작 가이드", L"操作指南", L"دليل التشغيل", L"Руководство", L"Bedienungsanleitung", L"Guia de operação", L"Handleiding", L"Przewodnik", L"İşlem kılavuzu"));
	}

	if (point.x == -1 && point.y == -1) {
		CRect rc; GetClientRect(&rc); ClientToScreen(&rc);
		point = CPoint(rc.left + 8, rc.top + 8);
	}
	const UINT cmd = menu.Track(point, this);
	if (cmd == IDM_MM_VIEW_2D) {
		m_viewMode = 0; savedata.midimonviewmode = 0; m_fullDraw = true; Invalidate(FALSE);
	} else if (cmd == IDM_MM_VIEW_3D) {
		m_viewMode = 1; savedata.midimonviewmode = 1;
		m_fullDraw = true;
		if (!(savedata.soft3dTourSeen & 16)) {
			savedata.soft3dTourSeen |= 16;
			m_soft3dTourUntil = GetTickCount() + 3000;
		}
		Invalidate(FALSE);
	} else if (cmd == IDM_MM_CAM_RESET) {
		savedata.midimon3dyaw = -220; savedata.midimon3dpitch = 260; savedata.midimon3dzoom = 100;
		SyncSoft3DFromSave(); Invalidate(FALSE);
	} else if (cmd == IDM_MM_FREEZE) {
		m_frozen = !m_frozen;
	} else if (cmd == IDM_MM_TOPMOST) {
		m_alwaysOnTop = !m_alwaysOnTop;
		savedata.midimontopmost = m_alwaysOnTop ? 1 : 0;
		SetWindowPos(m_alwaysOnTop ? &CWnd::wndTopMost : &CWnd::wndNoTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	} else if (cmd == IDM_MM_COPY) {
		wchar_t buf[4096];
		buf[0] = 0;
		int n = 0;
		n += _snwprintf_s(buf + n, _countof(buf) - n, _TRUNCATE, L"CH\tPC\tBNK\tName\tVol\tExp\tPan\tRev\tCrs\r\n");
		for (int i = 0; i < PART_MAX && n < 4000; ++i) {
			n += _snwprintf_s(buf + n, _countof(buf) - n, _TRUNCATE, L"%c%02d\t%03d\t%03d\t%s\t%d\t%d\t%d\t%d\t%d\r\n",
				(i < 16) ? L'A' : L'B', (i % 16) + 1, m_part[i].pc + 1, m_part[i].bankMsb, m_part[i].name,
				m_part[i].vol, m_part[i].exp, m_part[i].pan, m_part[i].rev, m_part[i].crs);
		}
		if (OpenClipboard()) {
			EmptyClipboard();
			const size_t bytes = (wcslen(buf) + 1) * sizeof(wchar_t);
			HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
			if (h) {
				void* p = GlobalLock(h);
				if (p) { memcpy(p, buf, bytes); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
				else GlobalFree(h);
			}
			CloseClipboard();
		}
	} else if (cmd == IDM_MM_RELOAD) {
		m_loadedPath[0] = 0;
		LoadCurrentMidi();
		Invalidate(FALSE);
	} else if (cmd == IDM_MM_MAP_AUTO) {
		m_mapForce = 0;
		for (int i = 0; i < PART_MAX; ++i) RefreshPartName(m_part[i]);
		Invalidate(FALSE);
	} else if (cmd == IDM_MM_MAP_GS) {
		m_mapForce = 1;
		for (int i = 0; i < PART_MAX; ++i) RefreshPartName(m_part[i]);
		Invalidate(FALSE);
	} else if (cmd == IDM_MM_MAP_XG) {
		m_mapForce = 2;
		for (int i = 0; i < PART_MAX; ++i) RefreshPartName(m_part[i]);
		Invalidate(FALSE);
	} else if (cmd == ID_MP_OPEN_EQ || cmd == ID_MP_OPEN_PIANOROLL || cmd == ID_MP_OPEN_ANALYZER) {
		extern CMediaPlayerDlg* mp;
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->PostMessage(WM_COMMAND, cmd);
		else if (og && ::IsWindow(og->GetSafeHwnd())) {
			if (cmd == ID_MP_OPEN_EQ)
				og->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON59, BN_CLICKED), 0);
			else if (cmd == ID_MP_OPEN_PIANOROLL)
				og->PostMessage(WM_OGG_TOGGLE_SUBUI, 1, 0);
			else
				og->PostMessage(WM_OGG_TOGGLE_SUBUI, 2, 0);
		}
	} else if (cmd == ID_HELP_SHOWSHEET) {
		ShowHelpSheet();
	}
}

void CMidiMonitorDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (CCC_MainLockOverlayHitTest(m_hWnd, point)) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH > 0 && point.y >= 0 && point.y < capH) {
		CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
		return;
	}
	if (IsView3D()) {
		m_rotDragging = true;
		m_rotDragOrigin = point;
		m_rotDragYaw0 = m_cam.yawDeg;
		m_rotDragPitch0 = m_cam.pitchDeg;
		SetCapture();
		return;
	}
	int part = -1;
	CRect cell;
	const int hit = HitMonitor(point, part, cell);
	if (hit == MM_HIT_APPVOL) {
		m_volDragging = true;
		CPoint f = point;
		f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
		int pct = 0;
		if (m_volBarRc.Width() > 0)
			pct = (f.x - m_volBarRc.left) * 100 / m_volBarRc.Width();
		SetAppVolPercent(pct);
		SetCapture();
		InvalidateDirty();
		return;
	}
	if (hit == MM_HIT_VOL || hit == MM_HIT_PAN || hit == MM_HIT_EXP
		|| hit == MM_HIT_REV || hit == MM_HIT_CRS || hit == MM_HIT_VAR) {
		m_dragKind = hit;
		m_dragPart = part;
		SetCapture();
		ApplyDragValue(point);
		return;
	}
	if (hit == MM_HIT_KEYS && part >= 0) {
		CPoint f = point;
		f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
		const int note = KeyAt(cell, f);
		if (note >= 0) {
			ReleasePlayNote();
			m_playPart = part;
			m_playNote = note;
			const int ch = part % 16;
			const DWORD msg = (DWORD)(0x90 | ch) | ((DWORD)note << 8) | (100u << 16);
			InjectShort(part, msg);
			InvalidateDirty();
			SetCapture();
		}
		return;
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CMidiMonitorDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_volDragging) {
		if (!(nFlags & MK_LBUTTON)) {
			m_volDragging = false;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			return;
		}
		CPoint f = point;
		f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
		int pct = 0;
		if (m_volBarRc.Width() > 0)
			pct = (f.x - m_volBarRc.left) * 100 / m_volBarRc.Width();
		SetAppVolPercent(pct);
		InvalidateDirty();
		return;
	}
	if (m_dragKind == MM_HIT_VOL || m_dragKind == MM_HIT_PAN || m_dragKind == MM_HIT_EXP
		|| m_dragKind == MM_HIT_REV || m_dragKind == MM_HIT_CRS || m_dragKind == MM_HIT_VAR) {
		if (!(nFlags & MK_LBUTTON)) {
			m_dragKind = MM_HIT_NONE;
			m_dragPart = -1;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			return;
		}
		ApplyDragValue(point);
		return;
	}
	if (m_playNote >= 0) {
		if (!(nFlags & MK_LBUTTON)) {
			ReleasePlayNote();
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			InvalidateDirty();
			return;
		}
		int part = -1;
		CRect cell;
		if (HitMonitor(point, part, cell) == MM_HIT_KEYS && part == m_playPart) {
			CPoint f = point;
			f.y -= CCC_GetCustomCaptionHeight(m_hWnd);
			const int note = KeyAt(cell, f);
			if (note >= 0 && note != m_playNote) {
				ReleasePlayNote();
				m_playPart = part;
				m_playNote = note;
				const int ch = part % 16;
				const DWORD msg = (DWORD)(0x90 | ch) | ((DWORD)note << 8) | (100u << 16);
				InjectShort(part, msg);
				InvalidateDirty();
			}
		}
		return;
	}
	if (!IsView3D()) {
		int part = -1;
		CRect cell;
		const int hit = HitMonitor(point, part, cell);
		if (hit == MM_HIT_APPVOL || hit == MM_HIT_PAN)
			::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
		else if (hit == MM_HIT_VOL || hit == MM_HIT_EXP || hit == MM_HIT_REV
			|| hit == MM_HIT_CRS || hit == MM_HIT_VAR)
			::SetCursor(::LoadCursor(NULL, IDC_SIZENS));
		else if (hit == MM_HIT_KEYS)
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
	}
	if (m_rotDragging) {
		if (!(nFlags & MK_LBUTTON)) {
			m_rotDragging = false;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			PersistSoft3D();
			return;
		}
		GdiSoft3D::OrbitDrag(m_cam, m_rotDragYaw0, m_rotDragPitch0, m_rotDragOrigin, point);
		Invalidate(FALSE);
		return;
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CMidiMonitorDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_volDragging) {
		m_volDragging = false;
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		return;
	}
	if (m_dragKind != MM_HIT_NONE) {
		m_dragKind = MM_HIT_NONE;
		m_dragPart = -1;
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		return;
	}
	if (m_playNote >= 0) {
		ReleasePlayNote();
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		InvalidateDirty();
		return;
	}
	if (m_rotDragging) {
		m_rotDragging = false;
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		PersistSoft3D();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

void CMidiMonitorDlg::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	if (IsView3D() || CCC_MainLockOverlayHitTest(m_hWnd, point)) {
		CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
		return;
	}
	int part = -1;
	CRect cell;
	const int hit = HitMonitor(point, part, cell);
	const int cc = MmCcForHit(hit);
	if (cc >= 0 && part >= 0) {
		const int v = MmDefaultForHit(hit);
		const int ch = part % 16;
		const DWORD msg = (DWORD)(0xb0 | ch) | ((DWORD)cc << 8) | ((DWORD)v << 16);
		InjectShort(part, msg);
		InvalidateDirty();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonDblClk(nFlags, point);
}

BOOL CMidiMonitorDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	UNREFERENCED_PARAMETER(nFlags);
	if (IsView3D()) {
		GdiSoft3D::WheelZoom(m_cam, zDelta);
		PersistSoft3D();
		Invalidate(FALSE);
		return TRUE;
	}
	CPoint client = pt;
	ScreenToClient(&client);
	int part = -1;
	CRect cell;
	const int hit = HitMonitor(client, part, cell);
	if (part >= 0 && (hit == MM_HIT_VOL || hit == MM_HIT_PAN || hit == MM_HIT_EXP
		|| hit == MM_HIT_REV || hit == MM_HIT_CRS || hit == MM_HIT_VAR)) {
		const int cc = MmCcForHit(hit);
		int* pv = nullptr;
		Part& p = m_part[part];
		if (hit == MM_HIT_VOL) pv = &p.vol;
		else if (hit == MM_HIT_PAN) pv = &p.pan;
		else if (hit == MM_HIT_EXP) pv = &p.exp;
		else if (hit == MM_HIT_REV) pv = &p.rev;
		else if (hit == MM_HIT_CRS) pv = &p.crs;
		else pv = &p.var;
		int v = *pv + ((zDelta > 0) ? 2 : -2);
		if (v < 0) v = 0;
		if (v > 127) v = 127;
		const int ch = part % 16;
		const DWORD msg = (DWORD)(0xb0 | ch) | ((DWORD)cc << 8) | ((DWORD)v << 16);
		InjectShort(part, msg);
		InvalidateDirty();
		return TRUE;
	}
	if (part >= 0 && hit == MM_HIT_PC) {
		int pc = m_part[part].pc + ((zDelta > 0) ? 1 : -1);
		if (pc < 0) pc = 0;
		if (pc > 127) pc = 127;
		const int ch = part % 16;
		const DWORD msg = (DWORD)(0xc0 | ch) | ((DWORD)pc << 8);
		InjectShort(part, msg);
		InvalidateDirty();
		return TRUE;
	}
	return CCustomBlurDialogExBase::OnMouseWheel(nFlags, zDelta, pt);
}

BOOL CMidiMonitorDlg::OnTtnNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult)
{
	UNREFERENCED_PARAMETER(id);
	*pResult = 0;
	NMTTDISPINFO* pdi = (NMTTDISPINFO*)pNMHDR;
	pdi->lpszText = m_hoverTip;
	m_hoverTip[0] = 0;
	return FALSE;
}

BOOL CMidiMonitorDlg::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == '0' && IsView3D()) {
		savedata.midimon3dyaw = -220;
		savedata.midimon3dpitch = 260;
		savedata.midimon3dzoom = 100;
		SyncSoft3DFromSave();
		Invalidate(FALSE);
		return TRUE;
	}
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}
