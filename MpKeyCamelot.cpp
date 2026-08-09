#include "stdafx.h"
#include "MpKeyCamelot.h"
#include "SongParams.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "CMediaPlayerDlg.h"

extern save savedata;
extern CString KeyCodeAll;
extern CPlayList* pl;
extern CMediaPlayerDlg* mp;

static const int kMajToCam[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
static const int kMinToCam[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };

int MpKeyToCamelot(int root, int minor)
{
	if (root < 0 || root > 11) return 0;
	if (minor) return kMinToCam[root];
	return 12 + kMajToCam[root];
}

void MpCamelotToKey(int camelot, int* root, int* minor)
{
	if (!root || !minor) return;
	*root = 0; *minor = 0;
	if (camelot < 1 || camelot > 24) return;
	const int isA = (camelot <= 12);
	const int n = isA ? camelot : (camelot - 12);
	*minor = isA ? 1 : 0;
	for (int r = 0; r < 12; ++r) {
		if (isA) { if (kMinToCam[r] == n) { *root = r; return; } }
		else { if (kMajToCam[r] == n) { *root = r; return; } }
	}
}

int MpCamelotNeighbors(int camelot, int* out, int outMax)
{
	if (!out || outMax < 1 || camelot < 1 || camelot > 24) return 0;
	int n = 0;
	auto add = [&](int c) {
		if (c < 1 || c > 24) return;
		for (int i = 0; i < n; ++i) if (out[i] == c) return;
		if (n < outMax) out[n++] = c;
	};
	add(camelot);
	const int isA = (camelot <= 12);
	const int num = isA ? camelot : (camelot - 12);
	const int prev = (num == 1) ? 12 : (num - 1);
	const int next = (num == 12) ? 1 : (num + 1);
	if (isA) {
		add(prev); add(next); add(12 + num); add(12 + prev); add(12 + next);
	} else {
		add(12 + prev); add(12 + next); add(num); add(prev); add(next);
	}
	return n;
}

BOOL MpKeyParseFromDisplay(LPCTSTR keyCode, int* root, int* minor)
{
	if (!root || !minor) return FALSE;
	*root = -1; *minor = 0;
	if (!keyCode || !*keyCode) return FALSE;
	const WCHAR* p = keyCode;
	while (*p && !((*p >= L'A' && *p <= L'G') || (*p >= L'a' && *p <= L'g')))
		++p;
	if (!*p) return FALSE;
	WCHAR r0 = (WCHAR)towupper(*p);
	WCHAR r1 = p[1];
	static const WCHAR* names[12] = {
		L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
	};
	static const WCHAR* flats[12] = {
		L"C", L"Db", L"D", L"Eb", L"E", L"F", L"Gb", L"G", L"Ab", L"A", L"Bb", L"B"
	};
	WCHAR tok[4] = { r0, 0, 0, 0 };
	if (r1 == L'#' || r1 == L'b') { tok[1] = r1; tok[2] = 0; }
	int found = -1;
	for (int i = 0; i < 12; ++i) {
		if (_wcsicmp(tok, names[i]) == 0 || _wcsicmp(tok, flats[i]) == 0) { found = i; break; }
	}
	if (found < 0) return FALSE;
	*root = found;
	const WCHAR* q = p + (tok[1] ? 2 : 1);
	while (*q == L' ' || *q == L',') ++q;
	if (*q == L'm' || *q == L'M') {
		if (q[1] == 0 || q[1] == L' ' || q[1] == L',' || q[1] == L'i' || q[1] == L'I')
			*minor = 1;
	}
	if (wcsstr(keyCode, L"min") || wcsstr(keyCode, L"Min") || wcsstr(keyCode, L"MIN"))
		*minor = 1;
	return TRUE;
}

CString MpCamelotLabel(int camelot)
{
	CString s;
	if (camelot < 1 || camelot > 24) return s;
	if (camelot <= 12) s.Format(L"%dA", camelot);
	else s.Format(L"%dB", camelot - 12);
	return s;
}

CString MpKeyDisplayName(int root, int minor)
{
	static const WCHAR* names[12] = {
		L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B"
	};
	CString s;
	if (root < 0 || root > 11) return s;
	s = names[root];
	if (minor) s += L"m";
	return s;
}

void MpKeyCaptureFromLiveAnalysis()
{
	int root = -1, minor = 0;
	if (!MpKeyParseFromDisplay(KeyCodeAll, &root, &minor) || root < 0)
		return;
	savedata.mpKeyRoot = root;
	savedata.mpKeyMinor = minor ? 1 : 0;
	savedata.mpCamelot = MpKeyToCamelot(root, savedata.mpKeyMinor);
	SongParams_SaveKeyGridForCurrentSong();
}

int MpKeyFindCompatibleInPlaylist(int maxOut, CString* outNames, int* outRows)
{
	if (maxOut < 1 || !outNames || !outRows || !pl || !pl->pc) return 0;
	const int curCam = savedata.mpCamelot;
	if (curCam < 1) return 0;
	int neigh[8];
	const int nn = MpCamelotNeighbors(curCam, neigh, 8);
	int n = 0;
	CString list = SongParams_CurrentListName();
	const int lim = pl->playcnt;
	for (int i = 0; i < lim && n < maxOut; ++i) {
		SongParam e;
		if (!SongParams_FindCopy(list, pl->pc[i].fol, pl->pc[i].sub, pl->pc[i].ret2, e))
			continue;
		if (e.camelot < 1) continue;
		BOOL ok = FALSE;
		for (int k = 0; k < nn; ++k) if (neigh[k] == e.camelot) { ok = TRUE; break; }
		if (!ok) continue;
		CString name = pl->pc[i].name;
		if (name.IsEmpty()) name = pl->pc[i].fol;
		CString lab;
		lab.Format(L"%s [%s]", (LPCTSTR)name, (LPCTSTR)MpCamelotLabel(e.camelot));
		outNames[n] = lab;
		outRows[n] = i;
		++n;
	}
	return n;
}
