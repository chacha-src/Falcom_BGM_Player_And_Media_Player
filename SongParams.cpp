// SongParams.cpp : 曲ごとのオーディオ/DSP パラメータ 保持・復元
#include "stdafx.h"
#include "SongParams.h"
#include "ProAudio.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "CMediaPlayerDlg.h"
#include "CEqualizer.h"
#include "CAnalyzerDlg.h"
#include "CPromptEngine.h"
#include <afxmt.h>
#include <vector>
#include <string>
#include <unordered_map>

// ---- 参照するグローバル ----
extern save savedata;
extern COggDlg* og;
extern CPlayList* pl;
extern CMediaPlayerDlg* mp;
extern CString filen;   // 現在再生中の曲パス(ゲームモードでは play() 中に書き換えられることあり)
extern int tempo;       // テンポ スライダー位置 0..400
extern int pitch;       // ピッチ スライダー位置 0..400
extern int modesub;     // 再生形式(= pc[].sub)
extern int ret2;        // 曲内番号(= pc[].ret2)
extern int plcnt;       // 再生中プレイリスト行
extern BOOL thn1;       // 再生通知スレッド停止要求
extern int stf;         // 再生停止要求
extern int playf;
extern int plf;

extern TCHAR karento2[1024]; // 実行ファイルのディレクトリ(末尾 \)

static bool SongParams_IsMainThread()
{
	if (!og || !::IsWindow(og->GetSafeHwnd()))
		return false;
	const DWORD mainTid = ::GetWindowThreadProcessId(og->GetSafeHwnd(), NULL);
	return mainTid != 0 && ::GetCurrentThreadId() == mainTid;
}

static bool SongParams_IsStopping()
{
	return thn1 || stf || playf == 0;
}
// ---- ファイル名 ----
#if _UNICODE
#define SONGPARAM_DAT_NAME L"oggYSEDbgmu_AudioData.dat"
#else
#define SONGPARAM_DAT_NAME "oggYSEDbgm_AudioData.dat"
#endif
static const int SONGPARAM_FILE_VERSION = 2;
// ver1 レコード末尾(mode/ret2 無し)のサイズ
static const size_t SONGPARAM_V1_SIZE = offsetof(SongParam, mode);

// ---- メモリ内テーブル(g_cs で保護) ----
// g_tbl: レコード本体(ファイルI/O用)。順序は問わない。
// g_prim: (listName+path+mode+ret2) → g_tbl index の主キー索引(大小無視)。
static CCriticalSection g_cs;
static std::vector<SongParam> g_tbl;

struct SpPrimKey {
	std::wstring list;
	std::wstring path;
	int mode;
	int ret2;
	bool operator==(const SpPrimKey& o) const
	{
		return mode == o.mode && ret2 == o.ret2 && list == o.list && path == o.path;
	}
};
struct SpPrimKeyHash {
	size_t operator()(const SpPrimKey& k) const
	{
		size_t h = std::hash<std::wstring>()(k.list);
		h ^= std::hash<std::wstring>()(k.path) + 0x9e3779b9u + (h << 6) + (h >> 2);
		h ^= std::hash<int>()(k.mode) + 0x9e3779b9u + (h << 6) + (h >> 2);
		h ^= std::hash<int>()(k.ret2) + 0x9e3779b9u + (h << 6) + (h >> 2);
		return h;
	}
};
static std::unordered_map<SpPrimKey, size_t, SpPrimKeyHash> g_prim;

static std::wstring SpFoldW(LPCTSTR s)
{
	if (!s) return std::wstring();
	CString t(s);
	t.MakeLower();
#if defined(UNICODE) || defined(_UNICODE)
	return std::wstring(t.GetString());
#else
	CStringW w(t);
	return std::wstring(w.GetString());
#endif
}

static SpPrimKey SpMakePrim(LPCTSTR list, LPCTSTR path, int mode, int ret2)
{
	SpPrimKey k;
	k.list = SpFoldW(list);
	CString p(path ? path : _T(""));
	p.Replace(_T('/'), _T('\\'));
	k.path = SpFoldW(p);
	k.mode = mode;
	k.ret2 = ret2;
	return k;
}

static void SpRebuildIndexLocked()
{
	g_prim.clear();
	g_prim.reserve(g_tbl.size() * 2 + 8);
	for (size_t i = 0; i < g_tbl.size(); i++) {
		g_prim[SpMakePrim(g_tbl[i].listName, g_tbl[i].path, g_tbl[i].mode, g_tbl[i].ret2)] = i;
	}
}

// 主キー完全一致。見つかれば index、なければ -1。
static int SpFindExactLocked(LPCTSTR list, LPCTSTR path, int mode, int ret2)
{
	if (!list || !*list || !path || !*path) return -1;
	auto it = g_prim.find(SpMakePrim(list, path, mode, ret2));
	if (it == g_prim.end()) return -1;
	if (it->second >= g_tbl.size()) return -1;
	return (int)it->second;
}

// 順序不問のため末尾と入れ替えて O(1) 削除。索引も更新。
static void SpEraseAtLocked(size_t i)
{
	if (i >= g_tbl.size()) return;
	g_prim.erase(SpMakePrim(g_tbl[i].listName, g_tbl[i].path, g_tbl[i].mode, g_tbl[i].ret2));
	const size_t last = g_tbl.size() - 1;
	if (i != last) {
		g_tbl[i] = g_tbl[last];
		g_prim[SpMakePrim(g_tbl[i].listName, g_tbl[i].path, g_tbl[i].mode, g_tbl[i].ret2)] = i;
	}
	g_tbl.pop_back();
}

static void SpReindexRowLocked(size_t i, const SpPrimKey& oldKey)
{
	g_prim.erase(oldKey);
	if (i < g_tbl.size())
		g_prim[SpMakePrim(g_tbl[i].listName, g_tbl[i].path, g_tbl[i].mode, g_tbl[i].ret2)] = i;
}

// ---- 保存デバウンス ----
static bool  g_dirty = false;
static DWORD g_lastWriteTick = 0;

// ---- 現在再生中の曲を追跡する状態 ----
static CString  s_curPath;       // 曲開始時に確定したパスキー
static CString  s_curList;       // 曲開始時に確定したリスト名
static int      s_curMode = 0;   // 曲開始時の mode(sub)
static int      s_curRet2 = 0;   // 曲開始時の ret2
static bool     s_baselineValid = false;
static bool     s_songReady = false;   // OnSongStarted 後のみ保存・復元ポーリングを有効化
static bool     s_restorePending = false; // PostMessage 復元待ち(その間は baseline を取らない)
static SongParam s_lastSaved;    // 直近に保存/復元したパラメータ(変更検知の基準)
static int      s_featLatch = -1; // savedata.saveSongParams の前回値(チェックON/OFF遷移検出用)

// ============================================================================
// 小物
// ============================================================================
static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// パラメータ部(キー以外)だけを比較
static bool ParamsEqual(const SongParam& a, const SongParam& b)
{
	if (a.dsvol != b.dsvol || a.kakuVol != b.kakuVol) return false;
	if (a.pitchPos != b.pitchPos || a.tempoPos != b.tempoPos) return false;
	for (int i = 0; i < 20; i++) if (a.eq[i] != b.eq[i]) return false;
	if (a.eqsoundenv != b.eqsoundenv || a.eqsoundeq != b.eqsoundeq) return false;
	if (a.eqsoundeffect != b.eqsoundeffect) return false;
	if (a.eq_reverb != b.eq_reverb || a.eq_chorus != b.eq_chorus || a.eq_delay != b.eq_delay) return false;
	if (a.analyzerspecstyle != b.analyzerspecstyle) return false;
	return true;
}

// 現在のライブ値からパラメータ部を埋める。
// ※再生スレッドから UI(GetPos) を触ると、stop1 の Join 待ちとデッドロックする。
static void SnapshotCurrent(SongParam& p)
{
	if (SongParams_IsMainThread() && og && ::IsWindow(og->GetSafeHwnd())) {
		if (og->m_dsval.GetSafeHwnd())
			savedata.dsvol = og->m_dsval.GetPos();
		if (og->m_kakuVol.GetSafeHwnd())
			savedata.kakuVol = og->m_kakuVol.GetPos();
		if (og->m_tempo_sl.GetSafeHwnd())
			tempo = og->m_tempo_sl.GetPos();
		if (og->m_pitch_sl.GetSafeHwnd())
			pitch = og->m_pitch_sl.GetPos();
	}
	if (savedata.dsvol == 0) savedata.dsvol = 1;

	p.dsvol = savedata.dsvol;
	p.kakuVol = savedata.kakuVol;
	p.pitchPos = pitch;
	p.tempoPos = tempo;
	for (int i = 0; i < 20; i++) p.eq[i] = savedata.eq[i];
	p.eqsoundenv = savedata.eqsoundenv;
	p.eqsoundeq = savedata.eqsoundeq;
	p.eqsoundeffect = savedata.eqsoundeffect;
	p.eq_reverb = savedata.eq_reverb;
	p.eq_chorus = savedata.eq_chorus;
	p.eq_delay = savedata.eq_delay;
	p.analyzerspecstyle = savedata.analyzerspecstyle;
}

// プレイリストの fol / filen をキー用に整える。
// GetFullPathName は仮想パスで表記が揺れるため使わない。
static CString SongParams_KeyPath(LPCTSTR fol)
{
	if (!fol || !*fol) return CString();
	CString s(fol);
	s.Replace(_T('/'), _T('\\'));
	return s;
}

static bool PathKeysMatch(LPCTSTR a, LPCTSTR b)
{
	if (!a || !b) return false;
	if (_tcsicmp(a, b) == 0) return true;
	CString na = NormalizePlaylistPath(a);
	CString nb = NormalizePlaylistPath(b);
	return !na.IsEmpty() && na.CompareNoCase(nb) == 0;
}

// g_cs 保持中。listName+path+mode+ret2 で検索。
// 旧 ver1(mode/ret2==0) はパス一致で救済し、呼出側 Upsert がキーを昇格する。
// 通常系は主キー索引、ゆれ・救済だけ線形走査。
static int FindIndexFlexibleLocked(LPCTSTR listName, LPCTSTR path, int mode, int ret2Val)
{
	CString key = SongParams_KeyPath(path);
	if (key.IsEmpty()) return -1;

	auto tryExact = [&](LPCTSTR ln, LPCTSTR pth, int md, int r2) -> int {
		if (!ln || !*ln) return -1;
		return SpFindExactLocked(ln, pth, md, r2);
	};

	int idx = tryExact(listName, key, mode, ret2Val);
	if (idx >= 0) return idx;
	idx = tryExact(listName, key, 0, 0); // legacy
	if (idx >= 0) return idx;

	CString np = NormalizePlaylistPath(path);
	if (!np.IsEmpty() && np.CompareNoCase(key) != 0) {
		idx = tryExact(listName, np, mode, ret2Val);
		if (idx >= 0) return idx;
		idx = tryExact(listName, np, 0, 0);
		if (idx >= 0) return idx;
	}

	auto tryOne = [&](LPCTSTR ln, LPCTSTR pth, int md, int r2, bool allowLegacy, bool anyModeRet2) -> int {
		for (size_t i = 0; i < g_tbl.size(); i++) {
			if (ln && *ln && _tcsicmp(g_tbl[i].listName, ln) != 0) continue;
			if (!PathKeysMatch(g_tbl[i].path, pth)) continue;
			if (anyModeRet2) return (int)i;
			if (g_tbl[i].mode == md && g_tbl[i].ret2 == r2) return (int)i;
			if (allowLegacy && g_tbl[i].mode == 0 && g_tbl[i].ret2 == 0) return (int)i;
		}
		return -1;
	};

	// リスト名ゆれ: パス+mode+ret2 だけで再検索
	idx = tryOne(NULL, key, mode, ret2Val, false, false);
	if (idx >= 0) return idx;
	idx = tryOne(NULL, key, mode, ret2Val, true, false);
	if (idx >= 0) return idx;
	if (!np.IsEmpty() && np.CompareNoCase(key) != 0) {
		idx = tryOne(NULL, np, mode, ret2Val, true, false);
		if (idx >= 0) return idx;
	}
	// 最後の手段(ツールチップ救済): パス一致なら mode/ret2 を無視
	idx = tryOne(listName, key, mode, ret2Val, false, true);
	if (idx >= 0) return idx;
	return tryOne(NULL, key, mode, ret2Val, false, true);
}

CString SongParams_CurrentListName()
{
	int n = savedata.playlistnum;
	if (n < 0 || n >= 1000) n = 0;
	CString s = savedata.playlistname[n];
	if (s.IsEmpty()) s.Format(L"#%d", n); // 未命名でも安定するようにインデックス名で代替
	return s;
}

// 再生中キーをプレイリスト行から取る(filen は play() で basename 化されるため)。
static void ResolvePlayingKey(CString& outList, CString& outPath, int& outMode, int& outRet2)
{
	outList = SongParams_CurrentListName();
	outMode = modesub;
	outRet2 = ret2;
	outPath = SongParams_KeyPath(filen);
	if (!pl || !pl->pc || pl->playcnt <= 0)
		return;
	// plcnt を最優先(再生対象)。pnt は表示用で更新が遅れることがある。
	int idx = -1;
	if (plcnt >= 0 && plcnt < pl->playcnt)
		idx = plcnt;
	else if (pl->pnt >= 0 && pl->pnt < pl->playcnt)
		idx = pl->pnt;
	if (idx < 0)
		return;
	outPath = SongParams_KeyPath(pl->pc[idx].fol);
	outMode = pl->pc[idx].sub;
	outRet2 = pl->pc[idx].ret2;
}

// ============================================================================
// ファイル入出力
// ============================================================================
void SongParams_LoadFile()
{
	CSingleLock lk(&g_cs, TRUE);
	g_tbl.clear();
	g_prim.clear();
	CString ss = karento2; ss += SONGPARAM_DAT_NAME;
	CFile f;
	if (f.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE) {
		s_featLatch = savedata.saveSongParams ? 1 : 0;
		return;
	}
	try {
		int ver = 0, cnt = 0;
		if (f.Read(&ver, sizeof(int)) != sizeof(int)) { f.Close(); s_featLatch = savedata.saveSongParams ? 1 : 0; return; }
		if (f.Read(&cnt, sizeof(int)) != sizeof(int)) { f.Close(); s_featLatch = savedata.saveSongParams ? 1 : 0; return; }
		if (cnt < 0) cnt = 0;
		if (cnt > 100000) cnt = 100000;
		g_tbl.reserve((size_t)cnt);
		for (int i = 0; i < cnt; i++) {
			SongParam e;
			ZeroMemory(&e, sizeof(e));
			if (ver >= 2) {
				UINT got = f.Read(&e, sizeof(SongParam));
				if (got != sizeof(SongParam)) break;
			}
			else {
				// ver1: mode/ret2 無し
				UINT got = f.Read(&e, (UINT)SONGPARAM_V1_SIZE);
				if (got != (UINT)SONGPARAM_V1_SIZE) break;
				e.mode = 0;
				e.ret2 = 0;
			}
			e.listName[255] = 0;
			e.path[1023] = 0;
			g_tbl.push_back(e);
		}
	}
	catch (...) {
	}
	f.Close();
	SpRebuildIndexLocked();
	g_dirty = false;
	s_featLatch = savedata.saveSongParams ? 1 : 0;
}

void SongParams_SaveFile()
{
	// ロック中はメモリコピーのみ。ファイル I/O はロック外(Join 待ちとの噛み合い防止)
	int ver = SONGPARAM_FILE_VERSION;
	std::vector<SongParam> copy;
	{
		CSingleLock lk(&g_cs, TRUE);
		copy = g_tbl;
		g_dirty = false;
		g_lastWriteTick = GetTickCount();
	}
	CString ss = karento2; ss += SONGPARAM_DAT_NAME;
	CFile f;
	if (f.Open(ss, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) != TRUE)
		return;
	try {
		int cnt = (int)copy.size();
		f.Write(&ver, sizeof(int));
		f.Write(&cnt, sizeof(int));
		for (int i = 0; i < cnt; i++)
			f.Write(&copy[i], sizeof(SongParam));
	}
	catch (...) {
	}
	f.Close();
}

// savedata だけ書き戻す(audioDataVersion 更新用)
static void SongParams_PersistSaveData()
{
	TCHAR tmp[1024];
	_tgetcwd(tmp, 1000);
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
	_tchdir(tmp);
}

struct SpPlTrack {
	CString listName;
	CString path;
	int mode;
	int ret2;
};

static CString SpPlaylistFileName(int idx)
{
	if (idx == 0) return _T("playlistu.dat");
	CString s; s.Format(_T("playlistu%d.dat"), idx);
	return s;
}

static CString SpListNameForIndex(int idx)
{
	if (idx < 0 || idx >= 1000) idx = 0;
	CString s = savedata.playlistname[idx];
	if (s.IsEmpty()) s.Format(_T("#%d"), idx);
	return s;
}

// playlistu*.dat から (listName, path, mode, ret2) を収集
static void SpCollectAllPlaylistTracks(std::vector<SpPlTrack>& out)
{
	out.clear();
	TCHAR tmp[1024];
	_tgetcwd(tmp, 1000);
	_tchdir(karento2);
	for (int plIdx = 0; plIdx < 1000; plIdx++) {
		CString fname = SpPlaylistFileName(plIdx);
		CFile f;
		if (!f.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL))
			continue;
		int cnt = 0;
		if (f.Read(&cnt, 4) != 4 || cnt < 0 || cnt > 100000) {
			f.Close();
			continue;
		}
		int skip = 0;
		bool ok = true;
		for (int i = 0; i < 9; ++i) {
			if (f.Read(&skip, 4) != 4) { ok = false; break; }
		}
		if (!ok) { f.Close(); continue; }

		CString listName = SpListNameForIndex(plIdx);
		playlistdata pld;
		for (int i = 0; i < cnt; ++i) {
			if (f.Read(&pld, sizeof(pld)) != sizeof(pld)) break;
			pld.fol[1023] = 0;
			CString path = SongParams_KeyPath(pld.fol);
			if (path.IsEmpty()) continue;
			SpPlTrack t;
			t.listName = listName;
			t.path = path;
			t.mode = pld.sub;
			t.ret2 = pld.ret2;
			out.push_back(t);
		}
		f.Close();
	}
	_tchdir(tmp);
}

void SongParams_ConvertKeysIfNeeded()
{
	// 1=mode+ret2 キーへ移行済み。新規も起動後に 1 にする。
	if (savedata.audioDataVersion >= 1)
		return;

	std::vector<SpPlTrack> tracks;
	SpCollectAllPlaylistTracks(tracks);

	bool changed = false;
	{
		CSingleLock lk(&g_cs, TRUE);
		if (g_tbl.empty()) {
			// 移行対象なしでもフラグは立てる(毎回スキャンしない)
		}
		else {
			std::vector<SongParam> neu;
			neu.reserve(g_tbl.size());

			for (size_t i = 0; i < g_tbl.size(); i++) {
				SongParam e = g_tbl[i];
				// 既に mode/ret2 付きならそのまま
				if (e.mode != 0 || e.ret2 != 0) {
					neu.push_back(e);
					continue;
				}

				// listName+path で一致するプレイリスト行を集める
				std::vector<std::pair<int, int> > keys; // (mode, ret2)
				auto addUnique = [&](int md, int r2) {
					for (size_t k = 0; k < keys.size(); k++)
						if (keys[k].first == md && keys[k].second == r2) return;
					keys.push_back(std::make_pair(md, r2));
				};

				for (size_t t = 0; t < tracks.size(); t++) {
					if (_tcsicmp(tracks[t].listName, e.listName) != 0) continue;
					if (!PathKeysMatch(tracks[t].path, e.path)) continue;
					addUnique(tracks[t].mode, tracks[t].ret2);
				}
				// リスト名ゆれ: パスだけでも探す
				if (keys.empty()) {
					for (size_t t = 0; t < tracks.size(); t++) {
						if (!PathKeysMatch(tracks[t].path, e.path)) continue;
						addUnique(tracks[t].mode, tracks[t].ret2);
					}
				}

				if (keys.empty()) {
					// 孤児エントリ: 旧キーのまま残す(ソフトマッチ救済用)
					neu.push_back(e);
				}
				else if (keys.size() == 1) {
					if (e.mode != keys[0].first || e.ret2 != keys[0].second)
						changed = true;
					e.mode = keys[0].first;
					e.ret2 = keys[0].second;
					neu.push_back(e);
				}
				else {
					// 同一 path に複数 mode/ret2 → パラメータを複製
					changed = true;
					for (size_t k = 0; k < keys.size(); k++) {
						SongParam c = e;
						c.mode = keys[k].first;
						c.ret2 = keys[k].second;
						neu.push_back(c);
					}
				}
			}
			g_tbl.swap(neu);
			SpRebuildIndexLocked();
		}
	}

	if (changed)
		SongParams_SaveFile();

	savedata.audioDataVersion = 1;
	SongParams_PersistSaveData();
}

// ファイル書き込みはメインスレッドだけ。再生通知スレッドで SaveFile すると
// stop1 の無限 Join と噛み合い、曲切替で UI が永久に止まる。
static void FlushIfDirty()
{
	bool dirty;
	{ CSingleLock lk(&g_cs, TRUE); dirty = g_dirty; }
	if (!dirty) return;
	if (!SongParams_IsMainThread())
		return;
	SongParams_SaveFile();
}

static void MarkDirtyAndMaybeWrite()
{
	DWORD now = GetTickCount();
	{ CSingleLock lk(&g_cs, TRUE); g_dirty = true; }
	// 再生スレッド側は dirty にするだけ。実書き込みは timerp / OnSongStopped。
	if (!SongParams_IsMainThread())
		return;
	// 連続変更(スライダードラッグ等)をまとめる。500ms ごとに書き出し。
	if (now - g_lastWriteTick >= 500)
		SongParams_SaveFile();
}

// ============================================================================
// 検索・更新・移行
// ============================================================================
bool SongParams_FindCopy(LPCTSTR listName, LPCTSTR path, int mode, int ret2Val, SongParam& out)
{
	CSingleLock lk(&g_cs, TRUE);
	int idx = FindIndexFlexibleLocked(listName, path, mode, ret2Val);
	if (idx < 0) return false;
	out = g_tbl[idx];
	return true;
}

bool SongParams_HasEntry(LPCTSTR listName, LPCTSTR path, int mode, int ret2Val)
{
	CSingleLock lk(&g_cs, TRUE);
	return FindIndexFlexibleLocked(listName, path, mode, ret2Val) >= 0;
}

// true=新規追加(★が付く変化)。false=更新のみ/何もしない。
static bool Upsert(LPCTSTR listName, LPCTSTR path, int mode, int ret2Val, const SongParam& params)
{
	CString key = SongParams_KeyPath(path);
	if (key.IsEmpty() || !listName) return false;
	CSingleLock lk(&g_cs, TRUE);
	int idx = FindIndexFlexibleLocked(listName, key, mode, ret2Val);
	if (idx < 0) {
		SongParam e = params;
		_tcsncpy(e.listName, listName, 255); e.listName[255] = 0;
		_tcsncpy(e.path, key, 1023); e.path[1023] = 0;
		e.mode = mode;
		e.ret2 = ret2Val;
		g_prim[SpMakePrim(e.listName, e.path, e.mode, e.ret2)] = g_tbl.size();
		g_tbl.push_back(e);
		return true;
	}
	else {
		SongParam& e = g_tbl[idx];
		SpPrimKey oldKey = SpMakePrim(e.listName, e.path, e.mode, e.ret2);
		// キーは最新表記へ揃えつつパラメータだけ更新(旧 ver1 もここで mode/ret2 昇格)
		_tcsncpy(e.listName, listName, 255); e.listName[255] = 0;
		_tcsncpy(e.path, key, 1023); e.path[1023] = 0;
		e.mode = mode;
		e.ret2 = ret2Val;
		e.dsvol = params.dsvol; e.kakuVol = params.kakuVol;
		e.pitchPos = params.pitchPos; e.tempoPos = params.tempoPos;
		for (int i = 0; i < 20; i++) e.eq[i] = params.eq[i];
		e.eqsoundenv = params.eqsoundenv; e.eqsoundeq = params.eqsoundeq;
		e.eqsoundeffect = params.eqsoundeffect;
		e.eq_reverb = params.eq_reverb; e.eq_chorus = params.eq_chorus; e.eq_delay = params.eq_delay;
		e.analyzerspecstyle = params.analyzerspecstyle;
		SpReindexRowLocked((size_t)idx, oldKey);
		return false;
	}
}

void SongParams_NotifyListMarksChanged()
{
	if (!SongParams_IsMainThread()) {
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->PostMessage(WM_APP_SONGPARAM_MARKS, 0, 0);
		return;
	}
	if (pl && ::IsWindow(pl->m_lc.GetSafeHwnd()))
		pl->m_lc.Invalidate(FALSE);
	if (mp && ::IsWindow(mp->GetSafeHwnd()) && ::IsWindow(mp->m_list.GetSafeHwnd()))
		mp->m_list.Invalidate(FALSE);
}

void SongParams_ResetAll()
{
	{
		CSingleLock lk(&g_cs, TRUE);
		g_tbl.clear();
		g_prim.clear();
		g_dirty = false;
	}
	CString ss = karento2; ss += SONGPARAM_DAT_NAME;
	::DeleteFile(ss);
	// 追跡状態もクリア(次の同一曲でも復元しない)
	s_curPath.Empty();
	s_curMode = 0;
	s_curRet2 = 0;
	s_baselineValid = false;
	s_songReady = false;
	s_restorePending = false;
	SongParams_NotifyListMarksChanged();
}

// 未命名キー "#N" のとき、表示名で保存された旧エントリも同一リストとみなす。
// 他リストの実名と衝突する表示名はエイリアスに入れない。
static void SpCollectListNameAliases(LPCTSTR name, std::vector<CString>& out)
{
	out.clear();
	if (!name || !*name) return;
	out.push_back(name);
	int idx = -1;
	if (_stscanf(name, _T("#%d"), &idx) != 1 || idx < 0)
		return;
	const int disp = idx + 1;
	CString cands[13];
	cands[0].Format(L"プレイリスト：%d", disp);
	cands[1].Format(L"Playlist: %d", disp);
	cands[2].Format(L"Liste de lecture : %d", disp);
	cands[3].Format(L"Lista de reproducción: %d", disp);
	cands[4].Format(L"플레이리스트: %d", disp);
	cands[5].Format(L"播放列表：%d", disp);
	cands[6].Format(L"قائمة التشغيل: %d", disp);
	cands[7].Format(L"Плейлист: %d", disp);
	cands[8].Format(L"Wiedergabeliste: %d", disp);
	cands[9].Format(L"Lista de reprodução: %d", disp);
	cands[10].Format(L"Afspeellijst: %d", disp);
	cands[11].Format(L"Lista odtwarzania: %d", disp);
	cands[12].Format(L"Oynatma Listesi: %d", disp);
	for (int a = 0; a < 13; a++) {
		bool clash = false;
		for (int k = 0; k < 1000; k++) {
			if (k == idx) continue;
			if (savedata.playlistname[k][0] == 0) continue;
			if (_tcsicmp(savedata.playlistname[k], cands[a]) == 0) {
				clash = true;
				break;
			}
		}
		if (!clash)
			out.push_back(cands[a]);
	}
}

static bool SpListNameMatchesAny(LPCTSTR listName, const std::vector<CString>& aliases)
{
	if (!listName) return false;
	for (size_t i = 0; i < aliases.size(); i++) {
		if (_tcsicmp(listName, aliases[i]) == 0)
			return true;
	}
	return false;
}

static int SpFindEntryLocked(LPCTSTR listName, LPCTSTR path, int mode, int ret2Val, const std::vector<CString>* aliases)
{
	CString key = SongParams_KeyPath(path);
	if (key.IsEmpty()) return -1;

	auto tryNames = [&](int md, int r2) -> int {
		if (aliases && !aliases->empty()) {
			for (size_t a = 0; a < aliases->size(); a++) {
				int idx = SpFindExactLocked((*aliases)[a], key, md, r2);
				if (idx >= 0) return idx;
			}
			return -1;
		}
		return SpFindExactLocked(listName, key, md, r2);
	};

	int idx = tryNames(mode, ret2Val);
	if (idx >= 0) return idx;
	idx = tryNames(0, 0); // legacy
	if (idx >= 0) return idx;

	// パス正規化ゆれ・索引ミス時の線形フォールバック
	for (size_t i = 0; i < g_tbl.size(); i++) {
		bool listOk = false;
		if (aliases && !aliases->empty())
			listOk = SpListNameMatchesAny(g_tbl[i].listName, *aliases);
		else if (listName && *listName)
			listOk = (_tcsicmp(g_tbl[i].listName, listName) == 0);
		if (!listOk) continue;
		if (!PathKeysMatch(g_tbl[i].path, key)) continue;
		if (g_tbl[i].mode == mode && g_tbl[i].ret2 == ret2Val) return (int)i;
		if (g_tbl[i].mode == 0 && g_tbl[i].ret2 == 0) return (int)i;
	}
	return -1;
}

void SongParams_RenameList(LPCTSTR oldName, LPCTSTR newName)
{
	if (!oldName || !newName || !*newName) return;
	if (_tcsicmp(oldName, newName) == 0) return;
	std::vector<CString> aliases;
	SpCollectListNameAliases(oldName, aliases);
	bool changed = false;
	{
		CSingleLock lk(&g_cs, TRUE);
		for (size_t i = 0; i < g_tbl.size(); i++) {
			if (!SpListNameMatchesAny(g_tbl[i].listName, aliases)) continue;
			_tcsncpy(g_tbl[i].listName, newName, 255);
			g_tbl[i].listName[255] = 0;
			changed = true;
		}
		if (changed)
			SpRebuildIndexLocked();
	}
	if (changed) SongParams_SaveFile();
	// 追跡中リスト名も追従(エイリアス含む)
	if (SpListNameMatchesAny(s_curList, aliases))
		s_curList = newName;
}

void SongParams_DeleteList(LPCTSTR name)
{
	if (!name) return;
	std::vector<CString> aliases;
	SpCollectListNameAliases(name, aliases);
	bool changed = false;
	{
		CSingleLock lk(&g_cs, TRUE);
		std::vector<SongParam> neu;
		neu.reserve(g_tbl.size());
		for (size_t i = 0; i < g_tbl.size(); i++) {
			if (SpListNameMatchesAny(g_tbl[i].listName, aliases)) {
				if (SpListNameMatchesAny(s_curList, aliases) && PathKeysMatch(s_curPath, g_tbl[i].path)
					&& s_curMode == g_tbl[i].mode && s_curRet2 == g_tbl[i].ret2) {
					s_curPath.Empty();
					s_curList.Empty();
					s_curMode = 0;
					s_curRet2 = 0;
					s_baselineValid = false;
					s_songReady = false;
					s_restorePending = false;
				}
				changed = true;
				continue;
			}
			neu.push_back(g_tbl[i]);
		}
		if (changed) {
			g_tbl.swap(neu);
			SpRebuildIndexLocked();
		}
	}
	if (changed) {
		SongParams_SaveFile();
		SongParams_NotifyListMarksChanged();
	}
}

void SongParams_RebindEntries(LPCTSTR listName, LPCTSTR newListName, const playlistdata0* items, int n, bool copy)
{
	if (!listName || !*listName || !items || n <= 0) return;
	if (copy && (!newListName || !*newListName)) return;
	std::vector<CString> aliases;
	SpCollectListNameAliases(listName, aliases);
	bool changed = false;
	{
		CSingleLock lk(&g_cs, TRUE);
		for (int k = 0; k < n; k++) {
			const playlistdata0& it = items[k];
			CString key = SongParams_KeyPath(it.fol);
			if (key.IsEmpty()) continue;
			const int mode = it.sub;
			const int r2 = it.ret2;
			int idx = SpFindEntryLocked(listName, key, mode, r2, &aliases);
			if (idx < 0) continue;

			if (!newListName) {
				// 削除
				if (SpListNameMatchesAny(s_curList, aliases) && PathKeysMatch(s_curPath, key)
					&& ((s_curMode == mode && s_curRet2 == r2) || (s_curMode == 0 && s_curRet2 == 0))) {
					s_curPath.Empty();
					s_curList.Empty();
					s_curMode = 0;
					s_curRet2 = 0;
					s_baselineValid = false;
					s_songReady = false;
					s_restorePending = false;
				}
				SpEraseAtLocked((size_t)idx);
				changed = true;
				continue;
			}

			if (copy) {
				// 複製: 先に同一キーがあれば上書き
				int dest = SpFindEntryLocked(newListName, key, mode, r2, NULL);
				SongParam e = g_tbl[idx];
				_tcsncpy(e.listName, newListName, 255);
				e.listName[255] = 0;
				_tcsncpy(e.path, key, 1023);
				e.path[1023] = 0;
				e.mode = mode;
				e.ret2 = r2;
				if (dest >= 0) {
					SpPrimKey oldDest = SpMakePrim(g_tbl[dest].listName, g_tbl[dest].path, g_tbl[dest].mode, g_tbl[dest].ret2);
					g_tbl[dest] = e;
					SpReindexRowLocked((size_t)dest, oldDest);
				}
				else {
					g_prim[SpMakePrim(e.listName, e.path, e.mode, e.ret2)] = g_tbl.size();
					g_tbl.push_back(e);
				}
				changed = true;
				continue;
			}

			// 付け替え(移動): 先に移動先の衝突を消す
			int dest = SpFindEntryLocked(newListName, key, mode, r2, NULL);
			if (dest >= 0 && dest != idx) {
				SpEraseAtLocked((size_t)dest);
				// erase で idx が動く可能性(末尾入れ替え)
				idx = SpFindEntryLocked(listName, key, mode, r2, &aliases);
				if (idx < 0) continue;
			}
			SpPrimKey oldKey = SpMakePrim(g_tbl[idx].listName, g_tbl[idx].path, g_tbl[idx].mode, g_tbl[idx].ret2);
			_tcsncpy(g_tbl[idx].listName, newListName, 255);
			g_tbl[idx].listName[255] = 0;
			_tcsncpy(g_tbl[idx].path, key, 1023);
			g_tbl[idx].path[1023] = 0;
			g_tbl[idx].mode = mode;
			g_tbl[idx].ret2 = r2;
			SpReindexRowLocked((size_t)idx, oldKey);
			if (SpListNameMatchesAny(s_curList, aliases) && PathKeysMatch(s_curPath, key)
				&& s_curMode == mode && s_curRet2 == r2)
				s_curList = newListName;
			changed = true;
		}
	}
	if (changed) {
		SongParams_SaveFile();
		SongParams_NotifyListMarksChanged();
	}
}

// ============================================================================
// 適用(メインスレッド)
// ============================================================================
void SongParams_ApplyEntryToMain(const SongParam& e)
{
	// savedata / グローバルへ反映(DSP はここを直接読むため即効く)
	savedata.dsvol = ClampI(e.dsvol, -498, 1);
	savedata.kakuVol = ClampI(e.kakuVol, 100, 900);
	savedata.kakuVal = savedata.kakuVol;
	tempo = ClampI(e.tempoPos, 0, 400);
	pitch = ClampI(e.pitchPos, 0, 400);
	for (int i = 0; i < 20; i++) savedata.eq[i] = ClampI(e.eq[i], 0, 200);
	savedata.eqsoundenv = e.eqsoundenv;
	savedata.eqsoundeq = e.eqsoundeq;
	savedata.eqsoundeffect = ClampI(e.eqsoundeffect, 0, 100);
	savedata.eq_reverb = ClampI(e.eq_reverb, 0, 200);
	savedata.eq_chorus = ClampI(e.eq_chorus, 0, 200);
	savedata.eq_delay = ClampI(e.eq_delay, 0, 200);
	savedata.analyzerspecstyle = ClampI(e.analyzerspecstyle, 0, 6);

	if (!og || !::IsWindow(og->GetSafeHwnd()))
		return;

	// メイン画面のスライダー(OnTimer が毎tick スライダー→savedata へ写すため必須)
	if (og->m_dsval.GetSafeHwnd())    og->m_dsval.SetPos(savedata.dsvol);
	if (og->m_kakuVol.GetSafeHwnd())  og->m_kakuVol.SetPos(savedata.kakuVol);
	if (og->m_tempo_sl.GetSafeHwnd()) og->m_tempo_sl.SetPos(tempo);
	if (og->m_pitch_sl.GetSafeHwnd()) og->m_pitch_sl.SetPos(pitch);

	// イコライザー画面が開いていれば、そのスライダー/コンボも更新
	// (開いている間は EQ の OnTimer がスライダー→savedata へ写すため必須)
	if (og->m_EqualizerDlg && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd())) {
		if (og->m_EqualizerDlg->m_pre.GetSafeHwnd())
			og->m_EqualizerDlg->m_pre.SetCurSel(savedata.eqsoundeq);
		if (og->m_EqualizerDlg->m_env.GetSafeHwnd())
			og->m_EqualizerDlg->m_env.SetCurSel(savedata.eqsoundenv);
		og->m_EqualizerDlg->SyncSlidersFromSavedata();
	}

	// アナライザーが開いていれば周波数表示モードを反映
	if (og->m_AnalyzerDlg && ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()))
		og->m_AnalyzerDlg->ApplySpecStyleExternal(savedata.analyzerspecstyle);
}

// play() 完了後に呼ぶ。復元をメインスレッドで同期実行する。
void SongParams_OnSongStarted()
{
	CString list, path;
	int md = 0, r2 = 0;
	ResolvePlayingKey(list, path, md, r2);
	if (path.IsEmpty()) {
		s_songReady = false;
		return;
	}

	ProAudio_SetCurrentSongKey(list, path, md, r2);
	ProAudio_ResetXfadeIn();
	ProAudio_ApplyLoopOverrideToGlobals();

	const bool promptOn = (MpPromptIsActive() != FALSE);
	if (promptOn)
		g_dirty = false;
	else
		FlushIfDirty();

	s_curPath = path;
	s_curList = list;
	s_curMode = md;
	s_curRet2 = r2;
	s_restorePending = false;
	s_songReady = true;

	if (!savedata.saveSongParams) {
		s_baselineValid = false;
		return;
	}

	SongParam e;
	if (SongParams_FindCopy(s_curList, s_curPath, s_curMode, s_curRet2, e)) {
		SongParams_ApplyEntryToMain(e);
		s_lastSaved = e;
		s_baselineValid = true;
	}
	else if (!promptOn) {
		// エントリ無し → 現在の設定をその曲の初期値として自動保存
		SnapshotCurrent(s_lastSaved);
		if (Upsert(s_curList, s_curPath, s_curMode, s_curRet2, s_lastSaved))
			SongParams_NotifyListMarksChanged();
		MarkDirtyAndMaybeWrite();
		s_baselineValid = true;
	}
	else {
		s_baselineValid = false;
	}
}

void SongParams_NoteRestored(const SongParam& e)
{
	s_lastSaved = e;
	s_baselineValid = true;
	s_restorePending = false;
}

void SongParams_OnSongStopped()
{
	if (!MpPromptIsActive())
		FlushIfDirty();
	s_songReady = false;
	s_restorePending = false;
	s_baselineValid = false;
}

// ============================================================================
// 同期本体(HandleNotifications / timerp / _export)
// ============================================================================
void SongParams_Sync(bool exporting)
{
	const bool feature = (savedata.saveSongParams != 0);
	const int featNow = feature ? 1 : 0;
	if (s_featLatch < 0)
		s_featLatch = featNow;

	// チェックON/OFFは停止中でも即反映(IsStopping より先)。新規関数は作らずここで処理。
	if (!exporting && s_featLatch != featNow) {
		CString list, path;
		int md = 0, r2 = 0;
		if (s_songReady && !s_curPath.IsEmpty()) {
			list = s_curList;
			path = s_curPath;
			md = s_curMode;
			r2 = s_curRet2;
		}
		else {
			ResolvePlayingKey(list, path, md, r2);
		}
		const bool promptOn = (MpPromptIsActive() != FALSE);

		if (featNow && !path.IsEmpty()) {
			// OFF→ON: ★付きなら保存値を読んで反映。無ければ現行値でシード(OnSongStarted と同趣旨)
			s_curList = list;
			s_curPath = path;
			s_curMode = md;
			s_curRet2 = r2;
			SongParam e;
			if (SongParams_FindCopy(list, path, md, r2, e)) {
				if (SongParams_IsMainThread()) {
					SongParams_ApplyEntryToMain(e);
					SongParams_NoteRestored(e);
				}
				else if (og && ::IsWindow(og->GetSafeHwnd())) {
					s_restorePending = true;
					og->PostMessage(WM_APP_SONGPARAM_RESTORE, 0, (LPARAM)new SongParam(e));
				}
			}
			else if (!promptOn) {
				SnapshotCurrent(s_lastSaved);
				if (Upsert(list, path, md, r2, s_lastSaved))
					SongParams_NotifyListMarksChanged();
				MarkDirtyAndMaybeWrite();
				FlushIfDirty();
				s_baselineValid = true;
			}
			else {
				s_baselineValid = false;
			}
		}
		else if (!featNow && s_featLatch) {
			// ON→OFF: 現行の音量/EQ等をその曲のエントリへ保存してから無効化
			if (!path.IsEmpty() && !promptOn) {
				SnapshotCurrent(s_lastSaved);
				if (Upsert(list, path, md, r2, s_lastSaved))
					SongParams_NotifyListMarksChanged();
				MarkDirtyAndMaybeWrite();
				FlushIfDirty();
			}
			s_baselineValid = false;
			s_restorePending = false;
		}
		s_featLatch = featNow;
	}

	// 停止中/停止要求中は何もしない(Join 待ちメイン ↔ Sync のデッドロック防止)
	if (!exporting && SongParams_IsStopping())
		return;

	// 再生スレッドが付けた dirty をメイン側で回収
	if (!exporting && SongParams_IsMainThread())
		FlushIfDirty();

	CString list, path;
	int md = 0, r2 = 0;
	ResolvePlayingKey(list, path, md, r2);
	if (path.IsEmpty()) return;

	const bool promptOn = (MpPromptIsActive() != FALSE);

	if (exporting) {
		if (!feature) return;
		SongParam e;
		if (SongParams_FindCopy(list, path, md, r2, e))
			SongParams_ApplyEntryToMain(e);
		return;
	}

	// play() 完了前はキーが未確定。OnSongStarted 待ち。
	if (!s_songReady)
		return;

	const bool songChanged =
		path.CompareNoCase(s_curPath) != 0 ||
		md != s_curMode ||
		r2 != s_curRet2 ||
		list.CompareNoCase(s_curList) != 0;

	if (songChanged) {
		if (promptOn)
			g_dirty = false;
		else
			FlushIfDirty();
		s_curPath = path;
		s_curList = list;
		s_curMode = md;
		s_curRet2 = r2;
		s_baselineValid = false;
		s_restorePending = false;

		if (feature) {
			SongParam e;
			if (SongParams_FindCopy(s_curList, s_curPath, s_curMode, s_curRet2, e)) {
				DWORD mainTid = 0;
				if (og && ::IsWindow(og->GetSafeHwnd()))
					mainTid = ::GetWindowThreadProcessId(og->GetSafeHwnd(), NULL);
				if (mainTid != 0 && ::GetCurrentThreadId() == mainTid) {
					SongParams_ApplyEntryToMain(e);
					s_lastSaved = e;
					s_baselineValid = true;
				}
				else if (og && ::IsWindow(og->GetSafeHwnd())) {
					s_restorePending = true;
					og->PostMessage(WM_APP_SONGPARAM_RESTORE, 0, (LPARAM)new SongParam(e));
				}
			}
			else if (!promptOn) {
				// エントリ無し → 現在の設定を自動保存
				SnapshotCurrent(s_lastSaved);
				if (Upsert(s_curList, s_curPath, s_curMode, s_curRet2, s_lastSaved))
					SongParams_NotifyListMarksChanged();
				MarkDirtyAndMaybeWrite();
				s_baselineValid = true;
			}
		}
		return;
	}

	if (!feature) { s_baselineValid = false; return; }
	if (promptOn) return;
	if (s_restorePending) return;

	if (!s_baselineValid) {
		// 有効化直後などで baseline 未確定: エントリがあれば復元、無ければ現行値を基準に
		SongParam e;
		if (SongParams_FindCopy(s_curList, s_curPath, s_curMode, s_curRet2, e)) {
			if (SongParams_IsMainThread()) {
				SongParams_ApplyEntryToMain(e);
				s_lastSaved = e;
				s_baselineValid = true;
			}
			else if (og && ::IsWindow(og->GetSafeHwnd())) {
				s_restorePending = true;
				og->PostMessage(WM_APP_SONGPARAM_RESTORE, 0, (LPARAM)new SongParam(e));
			}
		}
		else {
			SnapshotCurrent(s_lastSaved);
			s_baselineValid = true;
		}
		return;
	}

	SongParam cur;
	SnapshotCurrent(cur);
	if (!ParamsEqual(cur, s_lastSaved)) {
		Upsert(s_curList, s_curPath, s_curMode, s_curRet2, cur);
		s_lastSaved = cur;
		MarkDirtyAndMaybeWrite();
	}
}

// ============================================================================
// ツールチップ用: デフォルトと異なる項目だけを整形
// ============================================================================
static void TipAppend(CString& out, const CString& item)
{
	if (item.IsEmpty()) return;
	if (!out.IsEmpty()) out += L", ";
	out += item;
}

// テンポ/ピッチ スライダー位置(0..400)→ %(200=100%)
static double PosToPercent(int pos)
{
	if (pos >= 200) return (double)(pos - 100);
	return pos / 3.0 + 33.3;
}

CString SongParams_BuildTipExtra(LPCTSTR listName, LPCTSTR path, int mode, int ret2Val)
{
	SongParam e;
	if (!SongParams_FindCopy(listName, path, mode, ret2Val, e))
		return CString();

	CString body;
	CString tmp;

	if (e.dsvol != 1) {
		double pct = (e.dsvol + 499) * 2.0 / 10.0;
		tmp.Format(L"%s=%.1f%%", LL14(L"音量", L"Volume", L"Volume", L"Volume", L"Volumen", L"볼륨", L"音量", L"مستوى الصوت", L"Громкость", L"Lautstärke", L"Volume", L"Volume", L"Głośność", L"Ses"), pct);
		TipAppend(body, tmp);
	}
	if (e.kakuVol != 100) {
		tmp.Format(L"%s=%d%%", LL14(L"拡張音量", L"Ext.vol", L"Vol.ext", L"Vol.est", L"Vol.ext", L"확장 볼륨", L"扩展音量", L"صوت موسع", L"Расш.громк", L"Ext.Lautst", L"Vol.ext", L"Ext.vol", L"Głośn.rozsz", L"Geniş ses"), e.kakuVol);
		TipAppend(body, tmp);
	}
	if (e.tempoPos != 200) {
		tmp.Format(L"%s=%d%%", LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"템포", L"速度", L"الإيقاع", L"Темп", L"Tempo", L"Andamento", L"Tempo", L"Tempo", L"Tempo"), (int)(PosToPercent(e.tempoPos) + 0.5));
		TipAppend(body, tmp);
	}
	if (e.pitchPos != 200) {
		tmp.Format(L"%s=%d%%", LL14(L"ピッチ", L"Pitch", L"Hauteur", L"Intonazione", L"Tono", L"피치", L"音高", L"طبقة الصوت", L"Высота", L"Tonhöhe", L"Tom", L"Toonhoogte", L"Wysokość", L"Perde"), (int)(PosToPercent(e.pitchPos) + 0.5));
		TipAppend(body, tmp);
	}

	// EQ 15バンド: いずれかが中立(100)以外なら 15 個表示
	bool eqChanged = false;
	for (int i = 0; i < 15; i++) if (e.eq[i] != 100) { eqChanged = true; break; }
	if (eqChanged) {
		CString bands;
		for (int i = 0; i < 15; i++) {
			if (i) bands += L",";
			CString v; v.Format(L"%d", e.eq[i]); bands += v;
		}
		tmp.Format(L"%s=[%s]", LL14(L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ", L"EQ"), bands);
		TipAppend(body, tmp);
	}
	if (e.eq[15] != 100) { tmp.Format(L"%s=%d", LL14(L"マスター", L"Master", L"Maître", L"Master", L"Máster", L"마스터", L"主控", L"رئيسي", L"Мастер", L"Master", L"Mestre", L"Master", L"Master", L"Ana"), e.eq[15]); TipAppend(body, tmp); }
	if (e.eq[16] != 100) { tmp.Format(L"%s=%d", LL14(L"明瞭", L"Clarity", L"Clarté", L"Chiarezza", L"Claridad", L"명료", L"清晰", L"وضوح", L"Чёткость", L"Klarheit", L"Clareza", L"Helderheid", L"Klarość", L"Netlik"), e.eq[16]); TipAppend(body, tmp); }
	if (e.eq[17] != 100) { tmp.Format(L"%s=%d", LL14(L"バランス", L"Balance", L"Balance", L"Bilancio", L"Balance", L"밸런스", L"平衡", L"توازن", L"Баланс", L"Balance", L"Balanço", L"Balans", L"Balans", L"Denge"), e.eq[17]); TipAppend(body, tmp); }
	if (e.eq[18] != 100) { tmp.Format(L"%s=%d", LL14(L"密度", L"Density", L"Densité", L"Densità", L"Densidad", L"밀도", L"密度", L"كثافة", L"Плотность", L"Dichte", L"Densidade", L"Dichtheid", L"Gęstość", L"Yoğunluk"), e.eq[18]); TipAppend(body, tmp); }
	if (e.eq[19] != 100) { tmp.Format(L"%s=%d", LL14(L"立体", L"3D", L"3D", L"3D", L"3D", L"입체", L"立体", L"مجسم", L"3D", L"3D", L"3D", L"3D", L"3D", L"3D"), e.eq[19]); TipAppend(body, tmp); }

	if (e.eqsoundenv != 0) {
		tmp.Format(L"%s=#%d(%d%%)", LL14(L"環境", L"Env", L"Env", L"Amb", L"Entorno", L"환경", L"环境", L"بيئة", L"Среда", L"Umgeb", L"Amb", L"Omg", L"Środ", L"Ortam"), e.eqsoundenv, e.eqsoundeffect);
		TipAppend(body, tmp);
	}
	if (e.eqsoundeq != 0) {
		tmp.Format(L"%s=#%d", LL14(L"EQﾌﾟﾘｾｯﾄ", L"EQ preset", L"EQ preset", L"EQ preset", L"EQ preset", L"EQ 프리셋", L"EQ预设", L"إعداد EQ", L"EQ пресет", L"EQ Preset", L"EQ preset", L"EQ preset", L"EQ preset", L"EQ ön ayar"), e.eqsoundeq);
		TipAppend(body, tmp);
	}
	if (e.eq_reverb != 0) { tmp.Format(L"%s=%d", LL14(L"リバーブ", L"Reverb", L"Réverb", L"Riverbero", L"Reverb", L"리버브", L"混响", L"صدى", L"Реверб", L"Hall", L"Reverb", L"Reverb", L"Pogłos", L"Yankı"), e.eq_reverb); TipAppend(body, tmp); }
	if (e.eq_chorus != 0) { tmp.Format(L"%s=%d", LL14(L"コーラス", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"코러스", L"合唱", L"كورس", L"Хорус", L"Chorus", L"Chorus", L"Chorus", L"Chór", L"Koro"), e.eq_chorus); TipAppend(body, tmp); }
	if (e.eq_delay != 0) { tmp.Format(L"%s=%d", LL14(L"ディレイ", L"Delay", L"Delay", L"Delay", L"Delay", L"딜레이", L"延迟", L"تأخير", L"Задержка", L"Delay", L"Delay", L"Delay", L"Delay", L"Gecikme"), e.eq_delay); TipAppend(body, tmp); }

	if (e.analyzerspecstyle != 0) {
		tmp.Format(L"%s=%d", LL14(L"表示", L"Disp", L"Aff", L"Disp", L"Vista", L"표시", L"显示", L"عرض", L"Вид", L"Anz", L"Exib", L"Weerg", L"Wyśw", L"Görün"), e.analyzerspecstyle);
		TipAppend(body, tmp);
	}

	// エントリはあるが全部デフォルト扱い → それでも「保存あり」を示して空戻りにしない
	if (body.IsEmpty()) {
		body = LL14(L"(変更なし)", L"(no changes)", L"(aucun)", L"(nessuna)", L"(sin cambios)", L"(변경 없음)", L"(无变更)", L"(لا تغيير)", L"(без изменений)", L"(keine)", L"(sem alteracoes)", L"(geen)", L"(brak)", L"(degisiklik yok)");
	}

	CString head = LL14(L"保存パラメータ", L"Saved params", L"Params enreg", L"Param salv", L"Params guard", L"저장 파라미터", L"已存参数", L"معلمات محفوظة", L"Сохр.параметры", L"Gespeichert", L"Params salvos", L"Opgeslagen", L"Zapisane param", L"Kayıtlı param");
	CString out;
	out.Format(L"\n%s: %s", head, body);
	return out;
}

CString SongParams_BuildTipExtraForRow(int row)
{
	// 有効フラグ判定はここで行う(ListCtrlA 側の PCH/オフセットずれ対策)
	if (!savedata.saveSongParams)
		return CString();
	if (!pl || !pl->pc || row < 0 || row >= pl->playcnt)
		return CString();

	const playlistdata0& d = pl->pc[row];
	CString tip = SongParams_BuildTipExtra(
		SongParams_CurrentListName(),
		d.fol,
		d.sub,
		d.ret2);
	if (!tip.IsEmpty())
		return tip;

	// 再生中キー(ResolvePlayingKey)で保存された場合の救済
	// filen 書き換えや plcnt ずれで tip 用キーと保存キーが食い違うことがある
	if (row == plcnt && !s_curPath.IsEmpty()) {
		tip = SongParams_BuildTipExtra(s_curList, s_curPath, s_curMode, s_curRet2);
		if (!tip.IsEmpty())
			return tip;
	}
	return CString();
}

bool SongParams_HasEntryForRow(int row)
{
	if (!pl || !pl->pc || row < 0 || row >= pl->playcnt)
		return false;

	const playlistdata0& d = pl->pc[row];
	if (SongParams_HasEntry(SongParams_CurrentListName(), d.fol, d.sub, d.ret2))
		return true;

	// BuildTipExtraForRow と同じ再生中キー救済
	if (row == plcnt && !s_curPath.IsEmpty())
		return SongParams_HasEntry(s_curList, s_curPath, s_curMode, s_curRet2);
	return false;
}
