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
		L"・Lev / Vol Exp Rev Crs Var …… 出力と CC7/11/10/91/93/94。Vibrato・Filter・Envelope・EQ・NRPN は NRPN/CC。",
		L"· Lev / Vol Exp Rev Crs Var …… Output and CC7/11/10/91/93/94. Vibrato/Filter/Envelope/EQ/NRPN from NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… Sortie et CC7/11/10/91/93/94. Vibrato/filtre/enveloppe/EQ/NRPN via NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… Uscita e CC7/11/10/91/93/94. Vibrato/filtro/inviluppo/EQ/NRPN da NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… Salida y CC7/11/10/91/93/94. Vibrato/filtro/envolvente/EQ/NRPN por NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… 출력과 CC7/11/10/91/93/94. 비브라토/필터/엔벨로프/EQ/NRPN은 NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… 电平和 CC7/11/10/91/93/94。颤音/滤波/包络/EQ/NRPN 来自 NRPN/CC。",
		L"· Lev / Vol Exp Rev Crs Var …… خرج و CC7/11/10/91/93/94. اهتزاز/مرشح/غلاف/EQ/NRPN من NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… Выход и CC7/11/10/91/93/94. Вибрато/фильтр/огибающая/EQ/NRPN из NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… Pegel und CC7/11/10/91/93/94. Vibrato/Filter/Huellkurve/EQ/NRPN aus NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… Saida e CC7/11/10/91/93/94. Vibrato/filtro/envelope/EQ/NRPN via NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… Uitgang en CC7/11/10/91/93/94. Vibrato/filter/envelope/EQ/NRPN via NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… Wyjscie i CC7/11/10/91/93/94. Vibrato/filtr/obwiednia/EQ/NRPN z NRPN/CC.",
		L"· Lev / Vol Exp Rev Crs Var …… Cikis ve CC7/11/10/91/93/94. Vibrato/filtre/zarf/EQ/NRPN, NRPN/CC'den."));
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
	, m_viewMode(0), m_mapForce(0), m_frozen(false), m_alwaysOnTop(false), m_paintDisabled(false)
	, m_rotDragging(false), m_rotDragYaw0(0), m_rotDragPitch0(0), m_soft3dTourUntil(0)
	, m_hoverCol(-1), m_hoverPart(-1)
{
	m_loadedPath[0] = 0;
	m_titleBuf[0] = 0;
	m_hoverTip[0] = 0;
	memset(m_part, 0, sizeof(m_part));
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
	m_masterVol = 100;
	m_evPos = 0;
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
			p.lev = (float)d2 / 127.f;
			m_noteCount++;
		} else {
			p.noteOn[d1] = 0;
			if (p.held > 0) p.held--;
		}
	} else if (st == 0x80) {
		p.noteOn[d1] = 0;
		if (p.held > 0) p.held--;
	} else if (st == 0xc0) {
		p.pc = d1 & 127;
		RefreshPartName(p);
	} else if (st == 0xb0) {
		if (d1 == 0) { p.bankMsb = d2; RefreshPartName(p); }
		else if (d1 == 32) { p.bankLsb = d2; RefreshPartName(p); }
		else if (d1 == 7) p.vol = d2;
		else if (d1 == 11) p.exp = d2;
		else if (d1 == 10) p.pan = d2;
		else if (d1 == 91) p.rev = d2;
		else if (d1 == 93) p.crs = d2;
		else if (d1 == 94) p.var = d2;
		else if (d1 == 71) p.rsn = d2 - 64;
		else if (d1 == 74) p.lpf = d2 - 64;
		else if (d1 == 72) p.rls = d2 - 64;
		else if (d1 == 73) p.atk = d2 - 64;
		else if (d1 == 75) p.dcy = d2 - 64;
		else if (d1 == 76) p.vibRat = d2 - 64;
		else if (d1 == 77) p.vibDpt = d2 - 64;
		else if (d1 == 78) p.vibDly = d2 - 64;
		else if (d1 == 98) p.nrpnLsb = d2;
		else if (d1 == 99) p.nrpnMsb = d2;
		else if (d1 == 100) p.rpnLsb = d2;
		else if (d1 == 101) p.rpnMsb = d2;
		else if (d1 == 6) { p.dataMsb = d2; ApplyNrpn(p); }
		else if (d1 == 121) {
			p.exp = 127; p.pan = 64; p.rev = 40; p.crs = 0; p.var = 0;
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
	}
	if (n >= 11 && d[1] == 0x41 && d[3] == 0x42 && d[4] == 0x12 && d[5] == 0x40 && d[6] == 0x01) {
		if (d[7] == 0x30) m_revType = d[8];
		else if (d[7] == 0x38) m_choType = d[8];
	}
	if (n >= 9 && d[1] == 0x43 && d[3] == 0x4c && d[4] == 0x03 && d[5] == 0x00)
		m_ins1 = d[7];
	if (n >= 9 && d[1] == 0x43 && d[3] == 0x4c && d[4] == 0x03 && d[5] == 0x10)
		m_ins2 = d[7];
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
	if (pb < m_lastPlayb) {
		ResetParts();
		m_evPos = 0;
	}
	m_lastPlayb = pb;
	while (m_evPos < m_evCount && m_ev[m_evPos].sample <= pb) {
		ApplyEvent(m_ev[m_evPos]);
		m_evPos++;
	}
	for (int i = 0; i < PART_MAX; ++i) {
		if (m_part[i].held <= 0) {
			m_part[i].lev *= 0.82f;
			if (m_part[i].lev < 0.002f) m_part[i].lev = 0;
		}
	}
	int notes = 0;
	for (int i = 0; i < PART_MAX; ++i) notes += m_part[i].held;
	m_noteCount = notes;
}

void CMidiMonitorDlg::DrawVBar(CDC& dc, int x, int y, int bw, int bh, int v0, int vmax, COLORREF col)
{
	if (bw < 2 || bh < 2) return;
	dc.FillSolidRect(x, y, bw, bh, RGB(22, 22, 28));
	int v = v0;
	if (v < 0) v = 0;
	if (vmax < 1) vmax = 1;
	if (v > vmax) v = vmax;
	int h = (bh * v) / vmax;
	if (h > 0)
		dc.FillSolidRect(x, y + bh - h, bw, h, col);
}

void CMidiMonitorDlg::DrawPanBar(CDC& dc, int x, int y, int bw, int bh, int pan)
{
	if (bw < 3 || bh < 2) return;
	dc.FillSolidRect(x, y, bw, bh, RGB(22, 22, 28));
	int mid = x + bw / 2;
	dc.FillSolidRect(mid, y, 1, bh, RGB(80, 80, 40));
	int p = pan;
	if (p < 0) p = 0;
	if (p > 127) p = 127;
	int px = x + (bw - 2) * p / 127;
	dc.FillSolidRect(px, y + 1, 2, bh - 2, RGB(230, 210, 40));
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

void CMidiMonitorDlg::DrawMonitor2D(CDC& dc, int w, int h, UINT dpi)
{
	const int headH = Scale(78, dpi);
	dc.FillSolidRect(0, 0, w, headH, MM_HEAD_BG);
	dc.FillSolidRect(0, headH, w, h - headH, MM_BG);
	dc.SetBkMode(TRANSPARENT);
	CFont* oldF = dc.SelectObject(&m_fontHead);
	dc.SetTextColor(MM_HEAD_TX);

	int bpm = 0;
	if (m_usecQn > 0)
		bpm = (int)((60000000.0 / (double)m_usecQn) + 0.5);
	if (bpm < 1) bpm = savedata.mpDetectedBpm;
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

	int volBarW = max(40, w / 3);
	int notesW = Scale(90, dpi);
	dc.FillSolidRect(Scale(8, dpi), Scale(22, dpi), volBarW, Scale(10, dpi), RGB(40, 140, 50));
	wchar_t volT[32];
	_snwprintf_s(volT, _TRUNCATE, L"Vol %d%%", m_masterVol);
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(Scale(12, dpi), Scale(21, dpi), volT);
	dc.FillSolidRect(Scale(8, dpi) + volBarW + Scale(6, dpi), Scale(22, dpi), notesW, Scale(10, dpi), RGB(90, 170, 220));
	wchar_t nt[24];
	_snwprintf_s(nt, _TRUNCATE, L"Notes %03d", m_noteCount);
	dc.TextOut(Scale(12, dpi) + volBarW + Scale(6, dpi), Scale(21, dpi), nt);
	dc.FillSolidRect(Scale(8, dpi) + volBarW + notesW + Scale(14, dpi), Scale(20, dpi), Scale(12, dpi), Scale(12, dpi), RGB(180, 30, 30));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(Scale(10, dpi) + volBarW + notesW + Scale(16, dpi), Scale(20, dpi), L"D");

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
	const wchar_t* hdr = L"CH#   PC# BNK Map     Instrument        Lev  Vol Pan Exp Rev Crs Var   Vibrato        Filter         Envelope        EQ         NRPN   Keyboard";
	dc.TextOut(Scale(4, dpi), Scale(54, dpi), hdr);
	dc.SelectObject(&m_fontTiny);
	dc.SetTextColor(RGB(70, 70, 80));
	dc.TextOut(Scale(520, dpi), Scale(66, dpi), L"Rat Dpt Dly     LPF Rsn HPF     Atk Dcy Rls     Low High");

	const int rows = PART_MAX;
	int bodyH = h - headH;
	if (bodyH < rows) bodyH = rows;
	int rowH = bodyH / rows;
	if (rowH < Scale(10, dpi)) rowH = Scale(10, dpi);
	dc.SelectObject(&m_fontTiny);
	for (int i = 0; i < rows; ++i) {
		const Part& p = m_part[i];
		int y = headH + i * rowH;
		dc.FillSolidRect(0, y, w, rowH - 1, (i < 16) ? MM_ROW_A : MM_ROW_B);
		dc.FillSolidRect(0, y + rowH - 1, w, 1, MM_GRID);
		dc.SetTextColor(MM_FG);
		wchar_t chs[8];
		_snwprintf_s(chs, _TRUNCATE, L"%c%02d", (i < 16) ? L'A' : L'B', (i % 16) + 1);
		dc.TextOut(Scale(4, dpi), y + 1, chs);
		wchar_t pcb[48];
		_snwprintf_s(pcb, _TRUNCATE, L"%03d %03d %s", p.pc + 1, p.bankMsb, MmMapLabel(m_sysMode, p.mapId));
		dc.TextOut(Scale(40, dpi), y + 1, pcb);
		dc.TextOut(Scale(148, dpi), y + 1, p.name);
		int meterX = Scale(280, dpi);
		int bh = rowH - Scale(4, dpi);
		int by = y + Scale(2, dpi);
		int bw = Scale(6, dpi);
		DrawVBar(dc, meterX, by, bw, bh, (int)(p.lev * 127.f), 127, RGB(40, 210, 70));
		DrawVBar(dc, meterX + Scale(10, dpi), by, bw, bh, p.vol, 127, RGB(50, 200, 70));
		DrawPanBar(dc, meterX + Scale(18, dpi), by, Scale(8, dpi), bh, p.pan);
		DrawVBar(dc, meterX + Scale(28, dpi), by, bw, bh, p.exp, 127, RGB(50, 200, 70));
		DrawVBar(dc, meterX + Scale(36, dpi), by, bw, bh, p.rev, 127, RGB(210, 50, 50));
		DrawVBar(dc, meterX + Scale(44, dpi), by, bw, bh, p.crs, 127, RGB(80, 200, 230));
		DrawVBar(dc, meterX + Scale(52, dpi), by, bw, bh, p.var, 127, RGB(50, 80, 200));
		wchar_t num[96];
		_snwprintf_s(num, _TRUNCATE, L"%+03d %+03d %+03d   %+03d %+03d %+03d   %+03d %+03d %+03d   %d %s",
			p.vibRat, p.vibDpt, p.vibDly, p.lpf, p.rsn, p.hpf, p.atk, p.dcy, p.rls,
			p.eqLow, (p.eqHigh >= 1000) ? L"10k" : L"Hz");
		dc.TextOut(Scale(360, dpi), y + 1, num);
		wchar_t nr[16];
		_snwprintf_s(nr, _TRUNCATE, L"%02X\n%02X", p.nrpnMsb & 127, p.nrpnLsb & 127);
		CRect nrRc(Scale(640, dpi), y, Scale(668, dpi), y + rowH);
		dc.DrawText(nr, &nrRc, DT_CENTER | DT_WORDBREAK);
		CRect krc(Scale(672, dpi), y + 1, w - Scale(4, dpi), y + rowH - 2);
		DrawMiniKeys(dc, krc, p);
	}
	if (m_frozen) {
		dc.SelectObject(&m_fontHead);
		dc.SetTextColor(RGB(255, 180, 80));
		dc.TextOut(Scale(8, dpi), Scale(4, dpi) + Scale(14, dpi),
			LL14(L"フリーズ中", L"Frozen", L"Gele", L"Congelato", L"Congelado", L"정지됨", L"已冻结", L"مجمد",
				L"Заморожено", L"Eingefroren", L"Congelado", L"Bevroren", L"Zamrozone", L"Donduruldu"));
	}
	dc.SelectObject(oldF);
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
	SetTimer(1, 33, nullptr);
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
	if (w <= 0 || h <= 0) return;
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
	}

	if (!EnsureFrameBuffer(dc, w, h) || !m_frameDC.GetSafeHdc()) {
		dc.FillSolidRect(0, capH, w, h, MM_BG);
		return;
	}
	if (IsView3D())
		DrawMonitor3D(m_frameDC, w, h);
	else
		DrawMonitor2D(m_frameDC, w, h, dpi);

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
			return;
		}
	}
	if (!CCC_IsAeroEnabled() && CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11()) {
		CCC_BlitStretchOpaque(dc.GetSafeHdc(), 0, capH, w, h,
			m_frameDC.GetSafeHdc(), 0, 0, w, h);
		return;
	}
#endif
	dc.BitBlt(0, capH, w, h, &m_frameDC, 0, 0, SRCCOPY);
}

BOOL CMidiMonitorDlg::OnEraseBkgnd(CDC* pDC)
{
	UNREFERENCED_PARAMETER(pDC);
	return TRUE;
}

void CMidiMonitorDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1) {
		PersistPos();
		if (!IsIconic() && IsWindowVisible() && !m_paintDisabled)
			Invalidate(FALSE);
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
	if (!m_frozen)
		SyncFromPlayback();
	Invalidate(FALSE);
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
		m_viewMode = 0; savedata.midimonviewmode = 0; Invalidate(FALSE);
	} else if (cmd == IDM_MM_VIEW_3D) {
		m_viewMode = 1; savedata.midimonviewmode = 1;
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
	if (IsView3D() && !CCC_MainLockOverlayHitTest(m_hWnd, point)) {
		m_rotDragging = true;
		m_rotDragOrigin = point;
		m_rotDragYaw0 = m_cam.yawDeg;
		m_rotDragPitch0 = m_cam.pitchDeg;
		SetCapture();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CMidiMonitorDlg::OnMouseMove(UINT nFlags, CPoint point)
{
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
	if (m_rotDragging) {
		m_rotDragging = false;
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		PersistSoft3D();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

BOOL CMidiMonitorDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	UNREFERENCED_PARAMETER(nFlags);
	UNREFERENCED_PARAMETER(pt);
	if (IsView3D()) {
		GdiSoft3D::WheelZoom(m_cam, zDelta);
		PersistSoft3D();
		Invalidate(FALSE);
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
