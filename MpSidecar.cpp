#include "stdafx.h"
#include "MpSidecar.h"

extern save savedata;
extern TCHAR karento2[1024];

#if _UNICODE
#define MP_HIST_DAT   L"oggYSEDbgmu_MpHist.dat"
#define MP_SMART_DAT  L"oggYSEDbgmu_MpSmart.dat"
#else
#define MP_HIST_DAT   "oggYSEDbgm_MpHist.dat"
#define MP_SMART_DAT  "oggYSEDbgm_MpSmart.dat"
#endif

static const int MP_HIST_FILE_VER = 1;
static const int MP_SMART_FILE_VER = 1;

static MpHistEntry g_hist[MP_HIST_MAX];
static int g_histCnt = 0;
static bool g_histDirty = false;

static MpSmartRule g_smart[MP_SMART_MAX];
static int g_smartCnt = 0;
static bool g_smartDirty = false;

static CString MpSidePath(LPCTSTR leaf)
{
	CString ss = karento2;
	ss += leaf;
	return ss;
}

void MpHist_Init()
{
	ZeroMemory(g_hist, sizeof(g_hist));
	g_histCnt = 0;
	g_histDirty = false;
	MpHist_Load();
	if (g_histCnt <= 0 && savedata.mpHistCnt > 0) {
		const int n = (savedata.mpHistCnt < 8) ? savedata.mpHistCnt : 8;
		for (int i = 0; i < n; ++i) {
			if (!savedata.mpHistPath[i][0]) continue;
			MpHistEntry& e = g_hist[g_histCnt];
			ZeroMemory(&e, sizeof(e));
			_tcsncpy(e.path, savedata.mpHistPath[i], _countof(e.path) - 1);
			_tcsncpy(e.name, savedata.mpHistName[i], _countof(e.name) - 1);
			e.plIdx = -1;
			const int tod = savedata.mpHistTod[i];
			SYSTEMTIME s2; ::GetLocalTime(&s2);
			if (tod >= 0 && tod < 24 * 60) {
				s2.wHour = (WORD)(tod / 60);
				s2.wMinute = (WORD)(tod % 60);
				s2.wSecond = 0;
			}
			::SystemTimeToFileTime(&s2, &e.ft);
			g_histCnt++;
		}
		g_histDirty = true;
		MpHist_Save();
	}
}

void MpHist_Load()
{
	g_histCnt = 0;
	CFile f;
	if (f.Open(MpSidePath(MP_HIST_DAT), CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE)
		return;
	try {
		int ver = 0, cnt = 0;
		if (f.Read(&ver, sizeof(int)) != sizeof(int)) { f.Close(); return; }
		if (ver != MP_HIST_FILE_VER) { f.Close(); return; }
		if (f.Read(&cnt, sizeof(int)) != sizeof(int)) { f.Close(); return; }
		if (cnt < 0) cnt = 0;
		if (cnt > MP_HIST_MAX) cnt = MP_HIST_MAX;
		for (int i = 0; i < cnt; ++i) {
			MpHistEntry e;
			ZeroMemory(&e, sizeof(e));
			if (f.Read(&e, sizeof(e)) != sizeof(e)) break;
			e.path[_countof(e.path) - 1] = 0;
			e.name[_countof(e.name) - 1] = 0;
			g_hist[g_histCnt++] = e;
		}
	}
	catch (...) {
	}
	f.Close();
	g_histDirty = false;
}

void MpHist_Save()
{
	if (!g_histDirty) return;
	CFile f;
	if (f.Open(MpSidePath(MP_HIST_DAT), CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) != TRUE)
		return;
	try {
		int ver = MP_HIST_FILE_VER;
		int cnt = g_histCnt;
		f.Write(&ver, sizeof(int));
		f.Write(&cnt, sizeof(int));
		for (int i = 0; i < cnt; ++i)
			f.Write(&g_hist[i], sizeof(MpHistEntry));
		g_histDirty = false;
	}
	catch (...) {
	}
	f.Close();
}

int MpHist_Count() { return g_histCnt; }

bool MpHist_Get(int i, MpHistEntry& out)
{
	if (i < 0 || i >= g_histCnt) return false;
	out = g_hist[i];
	return true;
}

static CString NormPathLocal(LPCTSTR path)
{
	CString p(path ? path : _T(""));
	p.Trim();
	p.Replace(_T('/'), _T('\\'));
	return p;
}

void MpHist_Push(LPCTSTR path, LPCTSTR displayName, int plIdx)
{
	CString p = NormPathLocal(path);
	if (p.IsEmpty()) return;
	CString n(displayName ? displayName : _T(""));
	n.Trim();
	if (n.IsEmpty()) {
		int slash = p.ReverseFind(_T('\\'));
		n = (slash >= 0) ? p.Mid(slash + 1) : p;
	}
	SYSTEMTIME st; ::GetLocalTime(&st);
	FILETIME ft; ::SystemTimeToFileTime(&st, &ft);

	for (int i = 0; i < g_histCnt; ++i) {
		if (NormPathLocal(g_hist[i].path).CompareNoCase(p) == 0) {
			MpHistEntry e = g_hist[i];
			_tcsncpy(e.name, n, _countof(e.name) - 1);
			e.name[_countof(e.name) - 1] = 0;
			e.ft = ft;
			if (plIdx >= 0) e.plIdx = plIdx;
			for (int j = i; j > 0; --j)
				g_hist[j] = g_hist[j - 1];
			g_hist[0] = e;
			g_histDirty = true;
			MpHist_Save();
			MpHist_SyncJumpList8();
			return;
		}
	}
	const int nMove = (g_histCnt < MP_HIST_MAX) ? g_histCnt : (MP_HIST_MAX - 1);
	for (int j = nMove; j > 0; --j)
		g_hist[j] = g_hist[j - 1];
	ZeroMemory(&g_hist[0], sizeof(g_hist[0]));
	_tcsncpy(g_hist[0].path, p, _countof(g_hist[0].path) - 1);
	_tcsncpy(g_hist[0].name, n, _countof(g_hist[0].name) - 1);
	g_hist[0].ft = ft;
	g_hist[0].plIdx = plIdx;
	if (g_histCnt < MP_HIST_MAX)
		g_histCnt++;
	g_histDirty = true;
	MpHist_Save();
	MpHist_SyncJumpList8();
}

void MpHist_SyncJumpList8()
{
	const int n = (g_histCnt < 8) ? g_histCnt : 8;
	savedata.mpHistCnt = n;
	for (int i = 0; i < 8; ++i) {
		savedata.mpHistPath[i][0] = 0;
		savedata.mpHistName[i][0] = 0;
		savedata.mpHistTod[i] = -1;
	}
	for (int i = 0; i < n; ++i) {
		_tcsncpy(savedata.mpHistPath[i], g_hist[i].path, _countof(savedata.mpHistPath[i]) - 1);
		_tcsncpy(savedata.mpHistName[i], g_hist[i].name, _countof(savedata.mpHistName[i]) - 1);
		SYSTEMTIME st;
		FILETIME local = g_hist[i].ft;
		::FileTimeToSystemTime(&local, &st);
		savedata.mpHistTod[i] = (int)st.wHour * 60 + (int)st.wMinute;
	}
}

void MpSmart_Init()
{
	ZeroMemory(g_smart, sizeof(g_smart));
	g_smartCnt = 0;
	g_smartDirty = false;
	MpSmart_Load();
	MpSmart_EnsureDefaults();
}

void MpSmart_Load()
{
	g_smartCnt = 0;
	CFile f;
	if (f.Open(MpSidePath(MP_SMART_DAT), CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE)
		return;
	try {
		int ver = 0, cnt = 0;
		if (f.Read(&ver, sizeof(int)) != sizeof(int)) { f.Close(); return; }
		if (ver != MP_SMART_FILE_VER) { f.Close(); return; }
		if (f.Read(&cnt, sizeof(int)) != sizeof(int)) { f.Close(); return; }
		if (cnt < 0) cnt = 0;
		if (cnt > MP_SMART_MAX) cnt = MP_SMART_MAX;
		for (int i = 0; i < cnt; ++i) {
			MpSmartRule r;
			ZeroMemory(&r, sizeof(r));
			if (f.Read(&r, sizeof(r)) != sizeof(r)) break;
			r.name[_countof(r.name) - 1] = 0;
			r.artist[_countof(r.artist) - 1] = 0;
			g_smart[g_smartCnt++] = r;
		}
	}
	catch (...) {
	}
	f.Close();
	g_smartDirty = false;
}

void MpSmart_Save()
{
	if (!g_smartDirty) return;
	CFile f;
	if (f.Open(MpSidePath(MP_SMART_DAT), CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) != TRUE)
		return;
	try {
		int ver = MP_SMART_FILE_VER;
		int cnt = g_smartCnt;
		f.Write(&ver, sizeof(int));
		f.Write(&cnt, sizeof(int));
		for (int i = 0; i < cnt; ++i)
			f.Write(&g_smart[i], sizeof(MpSmartRule));
		g_smartDirty = false;
	}
	catch (...) {
	}
	f.Close();
}

int MpSmart_Count() { return g_smartCnt; }

bool MpSmart_Get(int i, MpSmartRule& out)
{
	if (i < 0 || i >= g_smartCnt) return false;
	out = g_smart[i];
	return true;
}

bool MpSmart_Set(int i, const MpSmartRule& r)
{
	if (i < 0 || i >= g_smartCnt) return false;
	g_smart[i] = r;
	g_smart[i].name[_countof(g_smart[i].name) - 1] = 0;
	g_smart[i].artist[_countof(g_smart[i].artist) - 1] = 0;
	g_smartDirty = true;
	MpSmart_Save();
	return true;
}

int MpSmart_Add(const MpSmartRule& r)
{
	if (g_smartCnt >= MP_SMART_MAX) return -1;
	g_smart[g_smartCnt] = r;
	g_smart[g_smartCnt].name[_countof(g_smart[0].name) - 1] = 0;
	g_smart[g_smartCnt].artist[_countof(g_smart[0].artist) - 1] = 0;
	const int idx = g_smartCnt++;
	g_smartDirty = true;
	MpSmart_Save();
	return idx;
}

bool MpSmart_Remove(int i)
{
	if (i < 0 || i >= g_smartCnt) return false;
	for (int j = i; j < g_smartCnt - 1; ++j)
		g_smart[j] = g_smart[j + 1];
	g_smartCnt--;
	g_smartDirty = true;
	MpSmart_Save();
	return true;
}

void MpSmart_EnsureDefaults()
{
	if (g_smartCnt > 0) return;
	MpSmartRule a; ZeroMemory(&a, sizeof(a));
	{
		const CString n = LL14(L"未再生", L"Unplayed", L"Non joues", L"Non riprodotti", L"No reproducidos", L"미재생", L"未播放", L"غير مشغّل", L"Неигранные", L"Ungespielt", L"Nao tocados", L"Ongespeeld", L"Nieodtworzone", L"Oynatilmamis");
		_tcsncpy(a.name, n, _countof(a.name) - 1);
		a.name[_countof(a.name) - 1] = 0;
	}
	a.flags = MP_SMART_UNPLAYED;
	a.enabled = 1;
	a.ratingMin = 1;
	a.hourFrom = 0; a.hourTo = 23;
	a.playCountMax = 0;
	MpSmart_Add(a);
	MpSmartRule b; ZeroMemory(&b, sizeof(b));
	{
		const CString n = LL14(L"欠損", L"Missing", L"Manquants", L"Mancanti", L"Faltantes", L"결손", L"缺失", L"مفقود", L"Отсутствующие", L"Fehlend", L"Ausentes", L"Ontbrekend", L"Brakujace", L"Eksik");
		_tcsncpy(b.name, n, _countof(b.name) - 1);
		b.name[_countof(b.name) - 1] = 0;
	}
	b.flags = MP_SMART_MISSING;
	b.enabled = 1;
	b.ratingMin = 1;
	b.hourFrom = 0; b.hourTo = 23;
	MpSmart_Add(b);
}
