// CMediaPlayerDlg.cpp : メディアプレイヤーモード画面(張りぼて)とモード選択ダイアログ
//
// 実体は COggDlg(og->) と CPlayList(pl->)。ここは表示と操作の取り次ぎだけを行う。
// メディアプレイヤーモード中は og / pl のウィンドウを非表示にして裏で生かしておく。
//
#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "CEqualizer.h"
#include "CPianoRoll.h"
#include "CAnalyzerDlg.h"
#include "CMediaPlayerDlg.h"
#include "AudioDevSync.h"
#include "CProToolsDlg.h"
#include "ProAudio.h"
#include "SongParams.h"
#include "CImageBase.h"
#include "Mp3Image.h"
#include "AudioUpscaler.h"
#include "CMpPlaylistIO.h"
#include "CMpM3uImportDlg.h"
#include "CMpDupesDlg.h"
#include "CMpFolderSyncDlg.h"
#include "CMpSmartPlaylistDlg.h"
#include "CMpQueueDlg.h"
#include "CMpCommandPaletteDlg.h"
#include "CMpHelpDlg.h"
#include "CMissingFilesDlg.h"
#include "MpSidecar.h"
#include "CPromptDlg.h"
#include "CCommandRollDlg.h"
#include "DeviceRecordDlg.h"
#include "ScreenCaptureDlg.h"
#include "SoundMeterDlg.h"
#include "DigitizeDlg.h"
#include "VoiceChangerDlg.h"
#include "TunerPracticeDlg.h"
#include "PhotoFrameDlg.h"
#include "Soft3DMazeDlg.h"
#include "CDesktopLyricsWnd.h"
#include "Douga.h"
#include "MpPlayerAddons.h"
#include "MpFeatureExtras.h"
#include "TagEditDlg.h"
#include "FileTagInfo.h"
#include "Render.h"
#include <direct.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <math.h>

extern void equaliser(void* data, int len, BOOL reset);
#ifdef _DEBUG
#define new DEBUG_NEW
#endif

/////////////////////////////////////////////////////////////////////////////
// 外部参照(既存のグローバル/メイン画面・プレイリストの状態を流用)
extern COggDlg* og;
extern CPlayList* pl;
extern BOOL plw;
extern int plcnt;
extern int gameon;
extern int killw1;
extern save savedata;
extern TCHAR karento2[1024];
extern CString filen, fnn, tagname, tagfile, tagalbum;
extern CString stitle;   // ゲーム内タイトル等(oggDlg.cpp)
extern CString tagtrack; // 曲番号(トラック番号。oggDlg.cpp)
extern int wavbit_sample_Hz; // サンプルレート Hz (oggDlg.cpp)
extern int wavchannel;       // チャンネル数 (oggDlg.cpp)
extern int wavsam_depth;     // ビット深度 (oggDlg.cpp)
extern int mode;         // 再生モード(タイトル解決に使用, oggDlg.cpp)
extern int tempo;        // テンポスライダー位置(oggDlg.cpp)
extern int pitch;        // ピッチスライダー位置(oggDlg.cpp)
extern int playy;   // 再生中フラグ(oggDlg.cpp)
extern int plf;          // 再生中(1=再生中。oggDlg.cpp)
extern int ps;           // 一時停止中(1=再開表示。oggDlg.cpp)
extern int videoonly;    // 動画のみ再生(oggDlg.cpp)
extern ITaskbarList3* ptl;   // タスクバー進捗(oggDlg.cpp で初期化)
extern void MpPushPlayHistory(LPCTSTR path, LPCTSTR displayName);
extern void MpTaskbarReplay();
extern void MpTaskbarNextTrack();
extern void MpTaskbarPrevTrack();
extern CDC dc;   // COggDlg のオフスクリーン合成面(スペアナ+ジャケ+時間)を流用
extern void ShowOggAboutDialog(CWnd* pParent);   // バージョン情報ダイアログ(oggDlg.cpp)
extern int spelv[400];

extern int g_mpExportStartFrame, g_mpExportEndFrame;

extern CString UrlEncode(const CString& str);
extern CStringA HttpGet(const CString& url, const CString& userAgent, const CString& headers, DWORD timeoutMs = 2000);
extern CStringA ExtractValueFromBlock(const CStringA& jsonObjectBlock, const CStringA& keyName, bool isString);
extern CStringA UnescapeJsonUnicode(const CStringA& src);
extern CStringA ExtractJsonStringSimple(const CStringA& json, const CStringA& key);

static const TCHAR* kMusicBrainzAgent = _T("oggPlayer-LyricsSearcher/1.0 ( ohimesama@example.com )");

// A-B 書き出し範囲(フレーム)。-1=未設定。ExportToWav が消費。
int g_mpExportStartFrame = -1;
int g_mpExportEndFrame = -1;

// og 側のオフスクリーン面のソース寸法(oggDlg.cpp の OnPaint と一致させる: srcW=MDCP+5)
static const int MP_SRCW = (88 * 2 + 175) * 4 + 5; // = 1409 (og の srcW と一致)
static const int MP_SRCH = (81 + 16) * 4;          // = 388

// 操作ガイド(モードレス)シングルトン。実装はファイル後半の CMpCheatSheetDlg。
static CDialog* g_mpHelpDlg = nullptr;

// ---- メディアプレイヤー拡張: 行↔pc 対応・ジャケット・ソート用(ファイル内 static) ----
static int MpDispToPc(CMediaPlayerDlg* self, int disp)
{
	if (!self || !pl) return disp;
	if (self->m_filtOn && self->m_fmap && disp >= 0 && disp < self->m_fcnt)
		return self->m_fmap[disp];
	return disp;
}

static int MpPcToDisp(CMediaPlayerDlg* self, int pcIdx)
{
	if (!self || !pl) return pcIdx;
	if (!self->m_filtOn)
		return (pcIdx >= 0 && pcIdx < pl->playcnt) ? pcIdx : -1;
	if (!self->m_fmap) return -1;
	for (int j = 0; j < self->m_fcnt; ++j)
		if (self->m_fmap[j] == pcIdx) return j;
	return -1;
}

static int MpRowMissGetCb(void* ctx, int disp)
{
	CMediaPlayerDlg* self = (CMediaPlayerDlg*)ctx;
	if (!self || !self->m_miss) return 0;
	const int pc = MpDispToPc(self, disp);
	if (pc < 0 || pc >= self->m_missCap) return 0;
	return self->m_miss[pc] == 1 ? 1 : 0;
}

static LPCTSTR MpCurListName()
{
	static CString s;
	s = SongParams_CurrentListName();
	return s;
}

static BOOL MpHourInRange(int hour, int from, int to)
{
	if (from < 0) from = 0;
	if (to < 0) to = 23;
	if (from <= to) return hour >= from && hour <= to;
	return hour >= from || hour <= to;
}

static BOOL MpTrackMatchesSmart(CMediaPlayerDlg* self, int pcIdx, const MpSmartRule& rule)
{
	if (!self || !pl || !pl->pc || pcIdx < 0 || pcIdx >= pl->playcnt) return FALSE;
	const playlistdata0& it = pl->pc[pcIdx];
	const int flags = rule.flags;
	if (flags & MP_SMART_UNPLAYED) {
		if (ProAudio_GetPlayCount(MpCurListName(), it.fol, it.sub, it.ret2) > 0)
			return FALSE;
	}
	if (flags & MP_SMART_MISSING) {
		if (!(self->m_miss && pcIdx < self->m_missCap && self->m_miss[pcIdx] == 1))
			return FALSE;
	}
	if (flags & MP_SMART_RATING_MIN) {
		const int r = ProAudio_GetRating(MpCurListName(), it.fol, it.sub, it.ret2);
		if (r < rule.ratingMin) return FALSE;
	}
	if (flags & MP_SMART_ARTIST) {
		CString a(it.art); a.MakeLower();
		CString needle(rule.artist); needle.MakeLower();
		if (needle.IsEmpty() || a.Find(needle) < 0) return FALSE;
	}
	if (flags & MP_SMART_PLAY_MAX) {
		const int pc = ProAudio_GetPlayCount(MpCurListName(), it.fol, it.sub, it.ret2);
		if (pc > rule.playCountMax) return FALSE;
	}
	if (flags & MP_SMART_BPM_RANGE) {
		SongParam e;
		if (!SongParams_FindCopy(MpCurListName(), it.fol, it.sub, it.ret2, e) || e.detectedBpm <= 0)
			return FALSE;
		if (e.detectedBpm < rule.bpmMin || e.detectedBpm > rule.bpmMax)
			return FALSE;
	}
	if (flags & MP_SMART_KEY) {
		SongParam e;
		if (!SongParams_FindCopy(MpCurListName(), it.fol, it.sub, it.ret2, e) || e.camelot < 1)
			return FALSE;
		if (rule.camelotWant > 0 && e.camelot != rule.camelotWant)
			return FALSE;
	}
	if (flags & MP_SMART_NO_JACKET) {
		if (it.fol[0] && PathFileExists(it.fol)) {
			// 簡易: cover.jpg 同梱無しを「欠損ジャケ」扱い
			CString dir(it.fol);
			const int slash = dir.ReverseFind(_T('\\'));
			if (slash >= 0) dir = dir.Left(slash + 1);
			if (PathFileExists(dir + L"cover.jpg") || PathFileExists(dir + L"folder.jpg"))
				return FALSE;
		}
	}
	if (flags & (MP_SMART_HOUR_RANGE | MP_SMART_LAST_HOUR)) {
		int hour = -1;
		if (flags & MP_SMART_LAST_HOUR) {
			FILETIME ft;
			if (ProAudio_GetLastPlay(MpCurListName(), it.fol, it.sub, it.ret2, ft)) {
				SYSTEMTIME st; ::FileTimeToSystemTime(&ft, &st);
				hour = (int)st.wHour;
			}
			else {
				// 未再生は LAST_HOUR 不一致（現在時刻へのフォールバック禁止）
				return FALSE;
			}
		}
		if (hour < 0) {
			SYSTEMTIME st; ::GetLocalTime(&st);
			hour = (int)st.wHour;
		}
		if (!MpHourInRange(hour, rule.hourFrom, rule.hourTo))
			return FALSE;
	}
	return TRUE;
}

// *.none 再抽出済み(起動セッション内)。GetCb / LoadVisible 共用。
// 固定長リング: 溢れで mark 失敗→同じ .none を 60ms 毎に叩き続けるのを防ぐ。
static const int kMpJakNoneCap = 512;
static TCHAR s_mpJakNoneDone[kMpJakNoneCap][1024];
static int s_mpJakNoneDoneN = 0;
static int s_mpJakNoneDoneW = 0;
static UINT s_mpJakTimerMs = 60;

static BOOL MpJakIsKnownNone(const TCHAR* path)
{
	if (!path || !path[0]) return FALSE;
	const int n = (s_mpJakNoneDoneN < kMpJakNoneCap) ? s_mpJakNoneDoneN : kMpJakNoneCap;
	for (int ni = 0; ni < n; ++ni) {
		if (_tcsicmp(s_mpJakNoneDone[ni], path) == 0) return TRUE;
	}
	return FALSE;
}
static void MpJakMarkNone(const TCHAR* path)
{
	if (!path || !path[0] || MpJakIsKnownNone(path)) return;
	_tcsncpy_s(s_mpJakNoneDone[s_mpJakNoneDoneW], path, _TRUNCATE);
	s_mpJakNoneDoneW = (s_mpJakNoneDoneW + 1) % kMpJakNoneCap;
	if (s_mpJakNoneDoneN < kMpJakNoneCap) ++s_mpJakNoneDoneN;
}

static HBITMAP MpJacketGetCb(void* ctx, int row)
{
	CMediaPlayerDlg* self = (CMediaPlayerDlg*)ctx;
	if (!self || !pl || !pl->pc) return NULL;
	const int pcIdx = MpDispToPc(self, row);
	if (pcIdx < 0 || pcIdx >= pl->playcnt) return NULL;
	const TCHAR* path = pl->pc[pcIdx].fol;
	if (!path || !path[0]) return NULL;

	// 描画(custom-draw)中はメモリキャッシュのみ。mtime/DeleteFile/CImage::Load は
	// MpJacketLoadVisible(Timer1/RefreshList)側で行う（起動直後の初回ペイント落ち防止）。
	DWORD now = GetTickCount();
	for (int i = 0; i < CMediaPlayerDlg::kMpJakN; ++i) {
		if (self->m_jakKey[i][0] && _tcsicmp(self->m_jakKey[i], path) == 0) {
			self->m_jakTick[i] = now;
			self->m_jakRow[i] = pcIdx;
			return self->m_jakBmp[i];
		}
	}
	return NULL;
}

static int MpNoteIconGetCb(void* ctx, int row)
{
	CMediaPlayerDlg* self = (CMediaPlayerDlg*)ctx;
	if (!self || !pl || !pl->pc) return 1;
	const int pcIdx = MpDispToPc(self, row);
	if (pcIdx < 0 || pcIdx >= pl->playcnt) return 1;
	return pl->pc[pcIdx].icon;
}

// 可視行のジャケット:
//  - ディスクキャッシュ(.b2 / 確定済み.n2)は一気にメモリへ
//  - 未キャッシュの LoadJacket はワーカスレッド(1件ずつ、UIを止めない)
static TCHAR s_mpJakNoDisk[96][1024];
static int s_mpJakNoDiskN = 0;

struct MpJakJob {
	HWND hwnd;
	LONG gen;
	TCHAR path[1024];
	int pcIdx;
	int disp;
	HBITMAP hb;
	char noneFlag;
	char bgOnly; // 1=非表示プリフェッチ。空きスロット無ならメモリへ載せない
};

static UINT AFX_CDECL MpJakLoadThread(LPVOID p)
{
	MpJakJob* job = (MpJakJob*)p;
	if (!job) return 0;
	job->hb = NULL;
	job->noneFlag = 1;
	::CoInitialize(NULL);
	extern COggDlg* og;
	const int px = CMediaPlayerDlg::kMpJakPx;
	CString diskBmp = PlJakDiskPath(job->path, FALSE);
	CString diskNone = PlJakDiskPath(job->path, TRUE);

	// 無し確定ファイルが先 → 抽出も Load もしない
	WIN32_FILE_ATTRIBUTE_DATA fadNone = {};
	if (!diskNone.IsEmpty() && ::GetFileAttributesEx(diskNone, GetFileExInfoStandard, &fadNone)) {
		job->noneFlag = 1;
		::CoUninitialize();
		if (job->hwnd && ::IsWindow(job->hwnd))
			::PostMessage(job->hwnd, WM_MP_JAK_DONE, (WPARAM)job->gen, (LPARAM)job);
		else
			free(job);
		return 0;
	}

	WIN32_FILE_ATTRIBUTE_DATA fadBmp = {};
	if (!diskBmp.IsEmpty() && ::GetFileAttributesEx(diskBmp, GetFileExInfoStandard, &fadBmp)) {
		// メディアより古いキャッシュは捨てて再抽出
		WIN32_FILE_ATTRIBUTE_DATA fadM = {};
		const CString media = PlPhysicalMediaPath(job->path);
		BOOL stale = FALSE;
		if (!media.IsEmpty() && ::GetFileAttributesEx(media, GetFileExInfoStandard, &fadM)) {
			ULARGE_INTEGER um = {}, uc = {};
			um.LowPart = fadM.ftLastWriteTime.dwLowDateTime; um.HighPart = fadM.ftLastWriteTime.dwHighDateTime;
			uc.LowPart = fadBmp.ftLastWriteTime.dwLowDateTime; uc.HighPart = fadBmp.ftLastWriteTime.dwHighDateTime;
			if (um.QuadPart > uc.QuadPart) {
				PlJakDiskForget(job->path);
				stale = TRUE;
			}
		}
		if (!stale) {
			CImage disk;
			if (disk.Load(diskBmp) == S_OK && !disk.IsNull() && disk.GetWidth() > 0) {
				HDC screen = ::GetDC(NULL);
				if (screen) {
					HDC mem = ::CreateCompatibleDC(screen);
					HBITMAP hbD = ::CreateCompatibleBitmap(screen, px, px);
					if (hbD && mem) {
						HGDIOBJ old = ::SelectObject(mem, hbD);
						RECT rc = { 0, 0, px, px };
						HBRUSH br = ::CreateSolidBrush(RGB(240, 240, 245));
						::FillRect(mem, &rc, br);
						::DeleteObject(br);
						::SetStretchBltMode(mem, COLORONCOLOR);
						disk.StretchBlt(mem, 0, 0, px, px, SRCCOPY);
						::SelectObject(mem, old);
						job->hb = hbD;
						job->noneFlag = 0;
						hbD = NULL;
					}
					if (hbD) ::DeleteObject(hbD);
					if (mem) ::DeleteDC(mem);
					::ReleaseDC(NULL, screen);
				}
				disk.Destroy();
			}
		}
	}
	if (!job->hb && og) {
		CImage tmp;
		og->LoadJacket(job->path, &tmp);
		if (!tmp.IsNull() && tmp.GetWidth() > 0 && tmp.GetHeight() > 0) {
			HDC screen = ::GetDC(NULL);
			if (screen) {
				HDC mem = ::CreateCompatibleDC(screen);
				HBITMAP hb = ::CreateCompatibleBitmap(screen, px, px);
				if (hb && mem) {
					HGDIOBJ old = ::SelectObject(mem, hb);
					RECT rc = { 0, 0, px, px };
					HBRUSH br = ::CreateSolidBrush(RGB(240, 240, 245));
					::FillRect(mem, &rc, br);
					::DeleteObject(br);
					::SetStretchBltMode(mem, COLORONCOLOR);
					tmp.StretchBlt(mem, 0, 0, px, px, SRCCOPY);
					::SelectObject(mem, old);
					job->hb = hb;
					job->noneFlag = 0;
					hb = NULL;
					if (!diskBmp.IsEmpty()) {
						try { tmp.Save(diskBmp, Gdiplus::ImageFormatBMP); } catch (...) {}
						if (!diskNone.IsEmpty()) ::DeleteFile(diskNone);
					}
				}
				if (hb) ::DeleteObject(hb);
				if (mem) ::DeleteDC(mem);
				::ReleaseDC(NULL, screen);
			}
		}
		tmp.Destroy();
	}
	if (job->noneFlag) {
		if (!diskNone.IsEmpty()) {
			HANDLE hf = ::CreateFile(diskNone, GENERIC_WRITE, FILE_SHARE_READ, NULL,
				CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
			if (hf != INVALID_HANDLE_VALUE) ::CloseHandle(hf);
		}
		if (!diskBmp.IsEmpty()) ::DeleteFile(diskBmp);
	}
	::CoUninitialize();
	if (job->hwnd && ::IsWindow(job->hwnd))
		::PostMessage(job->hwnd, WM_MP_JAK_DONE, (WPARAM)job->gen, (LPARAM)job);
	else {
		if (job->hb) ::DeleteObject(job->hb);
		free(job);
	}
	return 0;
}

// TRUE=未解決あり(高速タイマ維持) / FALSE=可視は温い(タイマを間引いてよい)
static BOOL MpJacketLoadVisible(CMediaPlayerDlg* self, BOOL allowExtract, BOOL bulkDisk = FALSE)
{
	if (!self || !::IsWindow(self->m_list.GetSafeHwnd()) || !pl || !pl->pc)
		return FALSE;
	const int nDisp = self->m_list.GetItemCount();
	if (nDisp <= 0) return FALSE;
	int top = self->m_list.GetTopIndex();
	if (top < 0) top = 0;
	int page = self->m_list.GetCountPerPage() + 2;
	if (page < 4) page = 4;
	const int px = CMediaPlayerDlg::kMpJakPx;
	const DWORD now = GetTickCount();

	auto findSlot = [&](const TCHAR* path, int& hit, int& freeSlot, int& lruSlot) {
		hit = -1; freeSlot = -1; lruSlot = 0;
		DWORD lruTick = 0xFFFFFFFF;
		for (int i = 0; i < CMediaPlayerDlg::kMpJakN; ++i) {
			if (self->m_jakKey[i][0] && path && _tcsicmp(self->m_jakKey[i], path) == 0) {
				hit = i;
				break;
			}
			if (!self->m_jakKey[i][0] && freeSlot < 0) freeSlot = i;
			if (self->m_jakTick[i] < lruTick) { lruTick = self->m_jakTick[i]; lruSlot = i; }
		}
	};
	auto isKnownNoDisk = [&](const TCHAR* path) -> BOOL {
		for (int ni = 0; ni < s_mpJakNoDiskN; ++ni) {
			if (_tcsicmp(s_mpJakNoDisk[ni], path) == 0) return TRUE;
		}
		return FALSE;
	};

	if (bulkDisk) {
		InterlockedIncrement(&self->m_jakGen);
		self->m_jakPend[0] = 0;
		InterlockedExchange(&self->m_jakBusy, 0);
		self->m_jakPrefetch = 0;
		s_mpJakNoDiskN = 0;
		for (int i = 0; i < CMediaPlayerDlg::kMpJakN; ++i) {
			if (self->m_jakBmp[i]) { ::DeleteObject(self->m_jakBmp[i]); self->m_jakBmp[i] = NULL; }
			self->m_jakKey[i][0] = 0;
			self->m_jakTick[i] = 0;
			self->m_jakRow[i] = -1;
		}
	}

	// UI はメモリ判定のみ。キャッシュファイルの再読込/属性チェックはしない。
	BOOL needJob = FALSE;
	int jobDisp = -1;
	BOOL jobBg = FALSE;
	for (int disp = top; disp < top + page && disp < nDisp; ++disp) {
		const int pcIdx = MpDispToPc(self, disp);
		if (pcIdx < 0 || pcIdx >= pl->playcnt) continue;
		const TCHAR* path = pl->pc[pcIdx].fol;
		if (!path || !path[0]) continue;
		int hit = -1, freeSlot = -1, lruSlot = 0;
		findSlot(path, hit, freeSlot, lruSlot);
		if (hit >= 0) {
			if (!self->m_jakBmp[hit]) {
				self->m_jakKey[hit][0] = 0;
				self->m_jakTick[hit] = 0;
				self->m_jakRow[hit] = -1;
				MpJakMarkNone(path);
				continue;
			}
			self->m_jakTick[hit] = now;
			self->m_jakRow[hit] = pcIdx;
			continue;
		}
		if (self->m_jakPend[0] && _tcsicmp(self->m_jakPend, path) == 0)
			return TRUE; // 抽出中
		if (MpJakIsKnownNone(path) || isKnownNoDisk(path))
			continue;
		needJob = TRUE;
		jobDisp = disp;
		jobBg = FALSE;
		break;
	}

	// 再生中プリフェッチは飢餓の元。停止時のみ画面外1件/tick。
	if (!needJob && allowExtract && plf == 0) {
		if (self->m_jakPrefetch < 0 || self->m_jakPrefetch >= nDisp)
			self->m_jakPrefetch = 0;
		const int disp = self->m_jakPrefetch;
		self->m_jakPrefetch = (disp + 1) % nDisp;
		if (disp < top || disp >= top + page) {
			const int pcIdx = MpDispToPc(self, disp);
			if (pcIdx >= 0 && pcIdx < pl->playcnt) {
				const TCHAR* path = pl->pc[pcIdx].fol;
				int hit = -1, freeSlot = -1, lruSlot = 0;
				if (path && path[0]) findSlot(path, hit, freeSlot, lruSlot);
				if (path && path[0] && hit < 0
					&& !MpJakIsKnownNone(path) && !isKnownNoDisk(path)
					&& !(self->m_jakPend[0] && _tcsicmp(self->m_jakPend, path) == 0)) {
					needJob = TRUE;
					jobDisp = disp;
					jobBg = TRUE;
				}
			}
		}
	}

	if (!needJob)
		return FALSE; // 可視解決済み・ディスク再監視なし
	if (!allowExtract || !og)
		return TRUE;
	if (InterlockedCompareExchange(&self->m_jakBusy, 1, 0) != 0)
		return TRUE;
	if (self->m_jakPend[0]) {
		InterlockedExchange(&self->m_jakBusy, 0);
		return TRUE;
	}

	const int pcIdx = MpDispToPc(self, jobDisp);
	if (pcIdx < 0 || pcIdx >= pl->playcnt) {
		InterlockedExchange(&self->m_jakBusy, 0);
		return FALSE;
	}
	const TCHAR* path = pl->pc[pcIdx].fol;
	if (!path || !path[0]) {
		InterlockedExchange(&self->m_jakBusy, 0);
		return FALSE;
	}

	// 再生中ジャケのメモリ流用のみ UI(ディスク無し)
	extern CString filen;
	if (!jobBg && !og->img.IsNull() && og->jx > 0 && filen.GetLength() > 0
		&& _tcsicmp(path, filen) == 0) {
		int hit = -1, freeSlot = -1, lruSlot = 0;
		findSlot(path, hit, freeSlot, lruSlot);
		int slot = (freeSlot >= 0) ? freeSlot : lruSlot;
		HDC screen = ::GetDC(NULL);
		if (screen && slot >= 0) {
			CDC mem; mem.CreateCompatibleDC(CDC::FromHandle(screen));
			HBITMAP hb0 = ::CreateCompatibleBitmap(screen, px, px);
			if (hb0) {
				if (self->m_jakBmp[slot]) { ::DeleteObject(self->m_jakBmp[slot]); self->m_jakBmp[slot] = NULL; }
				HGDIOBJ old0 = mem.SelectObject(hb0);
				mem.FillSolidRect(0, 0, px, px, RGB(240, 240, 245));
				mem.SetStretchBltMode(COLORONCOLOR);
				og->img.StretchBlt(mem.GetSafeHdc(), 0, 0, px, px, SRCCOPY);
				mem.SelectObject(old0);
				_tcsncpy_s(self->m_jakKey[slot], path, _TRUNCATE);
				self->m_jakBmp[slot] = hb0;
				self->m_jakTick[slot] = now;
				self->m_jakRow[slot] = pcIdx;
				CRect rIcon;
				if (self->m_list.GetItemRect(jobDisp, &rIcon, LVIR_BOUNDS)) {
					rIcon.right = rIcon.left + px + 28;
					self->m_list.RedrawWindow(&rIcon, NULL, RDW_INVALIDATE | RDW_NOERASE);
				}
				mem.DeleteDC();
				::ReleaseDC(NULL, screen);
				InterlockedExchange(&self->m_jakBusy, 0);
				return FALSE;
			}
			mem.DeleteDC();
		}
		if (screen) ::ReleaseDC(NULL, screen);
	}

	MpJakJob* job = (MpJakJob*)malloc(sizeof(MpJakJob));
	if (!job) {
		InterlockedExchange(&self->m_jakBusy, 0);
		return TRUE;
	}
	ZeroMemory(job, sizeof(*job));
	job->hwnd = self->m_hWnd;
	job->gen = InterlockedIncrement(&self->m_jakGen);
	job->pcIdx = pcIdx;
	job->disp = jobDisp;
	job->bgOnly = jobBg ? 1 : 0;
	_tcsncpy_s(job->path, path, _TRUNCATE);
	_tcsncpy_s(self->m_jakPend, path, _TRUNCATE);
	if (!AfxBeginThread(MpJakLoadThread, job, THREAD_PRIORITY_BELOW_NORMAL)) {
		self->m_jakPend[0] = 0;
		free(job);
		InterlockedExchange(&self->m_jakBusy, 0);
	}
	return TRUE;
}

LRESULT CMediaPlayerDlg::OnJakLoadDone(WPARAM wParam, LPARAM lParam)
{
	MpJakJob* job = (MpJakJob*)lParam;
	m_jakPend[0] = 0;
	InterlockedExchange(&m_jakBusy, 0);
	if (!job) return 0;
	const LONG gen = (LONG)wParam;
	const BOOL accept = (gen == m_jakGen) && job->path[0];
	if (accept) {
		int freeSlot = -1, lruSlot = 0;
		DWORD lruTick = 0xFFFFFFFF;
		int hit = -1;
		BOOL didImage = FALSE;
		for (int i = 0; i < kMpJakN; ++i) {
			if (m_jakKey[i][0] && _tcsicmp(m_jakKey[i], job->path) == 0) { hit = i; break; }
			if (!m_jakKey[i][0] && freeSlot < 0) freeSlot = i;
			if (m_jakTick[i] < lruTick) { lruTick = m_jakTick[i]; lruSlot = i; }
		}
		// 非表示プリフェッチは空きスロットのみ。可視を追い出さない(ディスクは書込済)
		// 無し確定はスロットに空ビットマップを載せない(無色サムネの原因)
		if (job->noneFlag || !job->hb) {
			MpJakMarkNone(job->path);
			if (hit >= 0) {
				if (m_jakBmp[hit]) { ::DeleteObject(m_jakBmp[hit]); m_jakBmp[hit] = NULL; }
				m_jakKey[hit][0] = 0;
				m_jakTick[hit] = 0;
				m_jakRow[hit] = -1;
			}
			if (job->hb) { ::DeleteObject(job->hb); job->hb = NULL; }
		}
		else {
			int slot = (hit >= 0) ? hit : freeSlot;
			if (slot < 0 && !job->bgOnly)
				slot = lruSlot;
			if (slot < 0) {
				if (job->hb) { ::DeleteObject(job->hb); job->hb = NULL; }
			}
			else {
				if (m_jakBmp[slot] && m_jakBmp[slot] != job->hb) {
					::DeleteObject(m_jakBmp[slot]);
					m_jakBmp[slot] = NULL;
				}
				_tcsncpy_s(m_jakKey[slot], job->path, _TRUNCATE);
				m_jakBmp[slot] = job->hb;
				job->hb = NULL;
				m_jakTick[slot] = GetTickCount();
				m_jakRow[slot] = job->pcIdx;
				didImage = TRUE;
			}
		}
		for (int ni = 0; ni < s_mpJakNoDiskN; ++ni) {
			if (_tcsicmp(s_mpJakNoDisk[ni], job->path) == 0) {
				s_mpJakNoDisk[ni][0] = 0;
				break;
			}
		}
		// 画像が載ったときだけ行更新(.none 完了の Redraw 連打で UI 飢餓しない)
		if (didImage && ::IsWindow(m_list.GetSafeHwnd()) && job->disp >= 0) {
			CRect rIcon;
			if (m_list.GetItemRect(job->disp, &rIcon, LVIR_BOUNDS)) {
				const int strip = m_list.m_mpJacketPx > 0
					? m_list.m_mpJacketPx + (int)(28 * hD2 + 0.5f)
					: (int)((kMpJakPx + 28) * hD2 + 0.5f);
				rIcon.right = rIcon.left + strip;
				m_list.RedrawWindow(&rIcon, NULL, RDW_INVALIDATE | RDW_NOERASE);
			}
		}
		// 本編ジャケの Adopt/LoadJacket はここではしない。
		// UI での CImage::Load がピアノ/アナライザ/EQ を飢餓させる。曲切替側に任せる。
	}
	else if (job->hb) {
		::DeleteObject(job->hb);
		job->hb = NULL;
	}
	free(job);
	// 次ジョブは Timer8(60ms)に任せる。完了直後の連鎖は .none 高速完了で
	// PostMessage 嵐になりピアノ/アナライザ/EQ が飢餓する。
	return 0;
}

static int MpCmpNameAsc(const void* a, const void* b)
{
	const playlistdata0* pa = (const playlistdata0*)a;
	const playlistdata0* pb = (const playlistdata0*)b;
	return _tcsicmp(pa->name, pb->name);
}
static int MpCmpNameDesc(const void* a, const void* b) { return -MpCmpNameAsc(a, b); }
static int MpCmpArtAsc(const void* a, const void* b)
{
	const playlistdata0* pa = (const playlistdata0*)a;
	const playlistdata0* pb = (const playlistdata0*)b;
	int c = _tcsicmp(pa->art, pb->art);
	return c ? c : _tcsicmp(pa->name, pb->name);
}
static int MpCmpArtDesc(const void* a, const void* b) { return -MpCmpArtAsc(a, b); }
static int MpCmpAlbAsc(const void* a, const void* b)
{
	const playlistdata0* pa = (const playlistdata0*)a;
	const playlistdata0* pb = (const playlistdata0*)b;
	int c = _tcsicmp(pa->alb, pb->alb);
	return c ? c : _tcsicmp(pa->name, pb->name);
}
static int MpCmpAlbDesc(const void* a, const void* b) { return -MpCmpAlbAsc(a, b); }
static int MpCmpTimeAsc(const void* a, const void* b)
{
	const playlistdata0* pa = (const playlistdata0*)a;
	const playlistdata0* pb = (const playlistdata0*)b;
	if (pa->time != pb->time) return (pa->time < pb->time) ? -1 : 1;
	return _tcsicmp(pa->name, pb->name);
}
static int MpCmpTimeDesc(const void* a, const void* b) { return -MpCmpTimeAsc(a, b); }

#pragma comment(lib, "version.lib")

// 実行ファイルのバージョンリソースからキャプション末尾「 Ver 0.9a Rel.xxxx.xx.xx」を作る。
// Ver は FILEVERSION(0,9,1,x)の第3要素を英字化(1='a')、Rel. は FileDescription から取得し、
// 見つからなければ LegalCopyright の "Copyright (C) 日付" から組み立てる。
static CString MpBuildVersionCaptionSuffix()
{
	TCHAR exePath[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, exePath, MAX_PATH);

	DWORD handle = 0;
	const DWORD size = GetFileVersionInfoSize(exePath, &handle);
	if (size == 0) return L"";

	BYTE* data = new BYTE[size];
	if (!GetFileVersionInfo(exePath, 0, size, data)) {
		delete[] data;
		return L"";
	}

	CString ver;
	VS_FIXEDFILEINFO* ffi = NULL;
	UINT len = 0;
	if (VerQueryValue(data, _T("\\"), (LPVOID*)&ffi, &len) && ffi && len >= sizeof(VS_FIXEDFILEINFO)) {
		const int major = HIWORD(ffi->dwFileVersionMS);
		const int minor = LOWORD(ffi->dwFileVersionMS);
		const int rev = HIWORD(ffi->dwFileVersionLS);
		if (rev >= 1 && rev <= 26)
			ver.Format(L" Ver %d.%d%c", major, minor, (wchar_t)(L'a' + rev - 1));
		else
			ver.Format(L" Ver %d.%d", major, minor);
	}

	CString rel;
	static const LPCTSTR strNames[] = { _T("FileDescription"), _T("LegalCopyright") };
	for (int i = 0; i < _countof(strNames) && rel.IsEmpty(); ++i) {
		CString query;
		query.Format(_T("\\StringFileInfo\\041104b0\\%s"), strNames[i]);
		LPVOID p = NULL;
		UINT l = 0;
		if (VerQueryValue(data, query, &p, &l) && p && l) {
			CString s((LPCTSTR)p);
			int pos = s.Find(L"Rel.");
			if (pos >= 0) {
				rel = L" " + s.Mid(pos);
				rel.TrimRight();
			}
			else if ((pos = s.Find(L"(C)")) >= 0) {
				CString d = s.Mid(pos + 3);
				d.Trim();
				if (!d.IsEmpty())
					rel = L" Rel." + d;
			}
		}
	}

	delete[] data;
	return ver + rel;
}

CMediaPlayerDlg* mp = NULL;
int g_mpBannerHover = 0;   // バナー(GDI)上にマウスがあるか(og の timerp がジャケットアニメに使用)
// 幅拡張時にジャケットを左余白へ分離表示しているか。1 の間は og の timerp が
// バナー内蔵ジャケット(半透明・タイトル背後)を描かない(二重表示・かぶり防止)。
int g_mpSideJacket = 0;

/////////////////////////////////////////////////////////////////////////////
// CModeSelectDlg
/////////////////////////////////////////////////////////////////////////////
IMPLEMENT_DYNAMIC(CModeSelectDlg, CCustomBlurDialogBase)

CModeSelectDlg::CModeSelectDlg(CWnd* pParent)
	: CCustomBlurDialogBase(CModeSelectDlg::IDD, pParent)
{
}

CModeSelectDlg::~CModeSelectDlg()
{
}

void CModeSelectDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MODE_FALCOM, m_btnFalcom);
	DDX_Control(pDX, IDC_MODE_MEDIA, m_btnMedia);
	DDX_Control(pDX, IDC_MODE_ASK, m_ask);
}

BEGIN_MESSAGE_MAP(CModeSelectDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MODE_FALCOM, &CModeSelectDlg::OnFalcom)
	ON_BN_CLICKED(IDC_MODE_MEDIA, &CModeSelectDlg::OnMedia)
	ON_WM_CTLCOLOR()
END_MESSAGE_MAP()

BOOL CModeSelectDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"起動モードの選択", L"Select startup mode", L"Selection du mode de demarrage", L"Selezione modalita di avvio", L"Seleccion del modo de inicio", L"시작 모드 선택", L"选择启动模式", L"اختيار وضع البدء", L"Выбор режима запуска", L"Startmodus auswahlen", L"Selecionar modo de inicializacao", L"Opstartmodus selecteren", L"Tryb uruchamiania", L"Başlangıç modunu seç"));
	SetDlgItemText(IDC_STATIC, LL14(L"どちらの画面で起動しますか？", L"Which screen to start with?", L"Quel ecran au demarrage ?", L"Quale schermata avviare?", L"¿Con qué pantalla iniciar?", L"어느 화면으로 시작할까요?", L"以哪个画面启动？", L"بأي شاشة تبدأ؟", L"С какого экрана начать?", L"Mit welchem Bildschirm starten?", L"Qual tela iniciar?", L"Met welk scherm starten?", L"Którym ekranem uruchomić?", L"Hangi ekranla başlasın?"));
	m_btnFalcom.SetWindowText(LL14(L"ファルコムbgm特化型画面", L"Falcom BGM dedicated screen", L"Ecran dedie BGM Falcom", L"Schermata BGM Falcom", L"Pantalla dedicada BGM Falcom", L"팔콤 BGM 전용 화면", L"Falcom BGM 专用画面", L"شاشة Falcom BGM المخصصة", L"Экран Falcom BGM", L"Falcom-BGM-Bildschirm", L"Tela dedicada Falcom BGM", L"Falcom BGM-scherm", L"Ekran Falcom BGM", L"Falcom BGM ekranı"));
	m_btnMedia.SetWindowText(LL14(L"メディアプレイヤー画面", L"Media player screen", L"Ecran lecteur multimedia", L"Schermata lettore multimediale", L"Pantalla reproductor multimedia", L"미디어 플레이어 화면", L"媒体播放器画面", L"شاشة مشغل الوسائط", L"Экран медиаплеера", L"Media-Player-Bildschirm", L"Tela do reprodutor de midia", L"Mediaspeler-scherm", L"Ekran odtwarzacza multimediow", L"Medya oynatıcı ekranı"));
	m_ask.SetWindowText(LL14(L"次回も起動時に確認する", L"Ask again next startup", L"Demander au prochain demarrage", L"Chiedi al prossimo avvio", L"Preguntar en el proximo inicio", L"다음에도 시작 시 확인", L"下次启动时也询问", L"اسأل في المرة القادمة", L"Спрашивать при следующем запуске", L"Beim nachsten Start fragen", L"Perguntar no proximo inicio", L"Volgende keer opnieuw vragen", L"Zapytaj przy następnym starcie", L"Sonraki açılışta tekrar sor"));
	// 既定はオフ（選んだら次回から出さない）。必要ならユーザーが付ける。
	m_ask.SetCheck(0);

	// 少し可愛い系: ボタンを大きめのフォントに
	m_btnFalcom.SetGradation(RGB(255, 210, 230), RGB(255, 170, 205), 0, TRUE);
	m_btnMedia.SetGradation(RGB(205, 230, 255), RGB(170, 205, 255), 0, TRUE);

	if (m_btnMedia.GetSafeHwnd())
		m_btnMedia.SetFocus();
	return FALSE; // TRUE だとフォーカスを既定コントロールへ戻す
}

void CModeSelectDlg::OnFalcom()
{
	savedata.playerMode = 0;
	savedata.startupAsk = m_ask.GetCheck() ? 1 : 0;
	EndDialog(IDOK);
}

void CModeSelectDlg::OnMedia()
{
	savedata.playerMode = 1;
	savedata.startupAsk = m_ask.GetCheck() ? 1 : 0;
	EndDialog(IDOK);
}

HBRUSH CModeSelectDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogBase::OnCtlColor(pDC, pWnd, nCtlColor);
	if (savedata.aero != 1) {
		if (m_brDlg.GetSafeHandle() == NULL)
			m_brDlg.CreateSolidBrush(COLOR_DIALOG_BG);
		if (nCtlColor == CTLCOLOR_DLG)
			return m_brDlg;
		if (nCtlColor == CTLCOLOR_STATIC) {
			pDC->SetBkMode(TRANSPARENT);
			return m_brDlg;
		}
	}
	return hbr;
}

/////////////////////////////////////////////////////////////////////////////
// CMediaPlayerDlg
/////////////////////////////////////////////////////////////////////////////
IMPLEMENT_DYNAMIC(CMediaPlayerDlg, CCustomBlurDialogExBase)

namespace {
const UINT_PTR kTimerListHdrDrag = 7;
const UINT_PTR kMpListHdrSubclassId = 4207;
}

LRESULT CALLBACK CMediaPlayerDlg::ListHeaderNotifySubclassProc(
	HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	CMediaPlayerDlg* pDlg = reinterpret_cast<CMediaPlayerDlg*>(dwRefData);
	if (pDlg && uMsg == WM_NOTIFY) {
		NMHDR* pN = reinterpret_cast<NMHDR*>(lParam);
		const HWND hHdr = ListView_GetHeader(hWnd);
		if (hHdr && pN && pN->hwndFrom == hHdr) {
			switch (pN->code) {
			case HDN_BEGINTRACKA:
			case HDN_BEGINTRACKW:
			case HDN_TRACKA:
			case HDN_TRACKW:
			case HDN_ENDTRACKA:
			case HDN_ENDTRACKW: {
				LRESULT lr = 0;
				pDlg->OnPlaylistHeaderNotify(pN, &lr);
				break;
			}
			}
		}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

CMediaPlayerDlg::CMediaPlayerDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CMediaPlayerDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
	m_lastCount = -1;
	m_lastPlcnt = -2;
	m_lastScroll = -2;
	m_lastFollowPnt = -2;
	m_lastComboCount = -1;
	m_plselDropExtent = 0;
	m_plselLayoutDpi = 0.f;
	m_lastMs2 = 0;
	m_seekDragging = 0;
	m_seekHoldPos = 0;
	m_seekHoldUntil = 0;
	m_lastPlayIcon = -999;
	m_savedEqVisible = 0;
	m_savedPianoVisible = 0;
	m_savedAnalyzerVisible = 0;
	m_inSizeMove = false;
	m_cascadePrevValid = false;
	m_uiReady = false;
	m_dragging = 0;
	m_dragSrc = -1;
	m_hDragImage = NULL;
	m_libDrag = 0;
	m_libDragFolder.Empty();
	hD2 = 1.0f;
	m_bannerRect.SetRectEmpty();
	m_jacketRect.SetRectEmpty();
	m_infoPanelRect.SetRectEmpty();
	m_plRailRect.SetRectEmpty();
	m_bannerCacheW = 0;
	m_bannerCacheH = 0;
	for (int i = 0; i < kInfoRows; i++) {
		m_isc[i] = 0; m_iscW[i] = 0;
		m_iscRowOldBmp[i] = nullptr;
		m_iscRowCacheW[i] = 0;
		m_iscRowCacheH[i] = 0;
		m_iscRowCacheClr[i] = 0;
		m_iscRowCacheBg[i] = 0;
	}
	m_iscActive    = false;
	m_iscScrollPosted = 0;
	m_lastInfoPanelW = 0;
	m_infoMemOldBmp = nullptr;
	m_infoMemW = m_infoMemH = 0;
	m_listHdrDragCol = -1;
	m_lastToggleSupe = -1;
	m_lastToggleSt = -1;
	m_lastToggleEq = -1;
	m_lastTogglePiano = -1;
	m_lastToggleAnalyzer = -1;
	m_lastTogglePrompt = -1;
	m_lastToggleCmdRoll = -1;
	m_dsvolSlW = 0;
	m_mpBtnShort = -1;
	m_mpBotShort = -1;
	m_mpPromptShort = -1;
	m_mpCmdRollShort = -1;
	for (int i = 0; i < 6; ++i)
		m_mpChkShort[i] = -1;
	m_abApos = -1;
	m_abBpos = -1;
	m_abWrapBusy = 0;
	m_abLoopCount = 0;
	m_wavePeakN = 0;
	m_wavePath[0] = 0;
	m_waveGen = 0;
	m_waveBusy = 0;
	m_jacketRemBucket = -1;
	ZeroMemory(m_wavePeaks, sizeof(m_wavePeaks));
	m_libPathN = 0;
	m_albumN = 0;
	m_libTreeBuilt = 0;
	m_libBuildPosted = 0;
	m_hLibDragImage = NULL;
	m_histBuilt = 0;
	m_fmap = NULL;
	m_fmapCap = 0;
	m_fcnt = 0;
	m_filtOn = 0;
	m_smartFilt = 0;
	m_activeSmartId = -1;
	m_queueN = 0;
	m_sleepEndTick = 0;
	ZeroMemory(m_queue, sizeof(m_queue));
	m_miss = NULL;
	m_missCap = 0;
	m_missScan = 0;
	m_missGen = 0;
	m_missBusy = 0;
	m_jakGen = 0;
	m_jakBusy = 0;
	m_jakPend[0] = 0;
	m_jakPrefetch = 0;
	ZeroMemory(m_jakBmp, sizeof(m_jakBmp));
	ZeroMemory(m_jakKey, sizeof(m_jakKey));
	ZeroMemory(m_jakTick, sizeof(m_jakTick));
	for (int i = 0; i < kMpJakN; ++i)
		m_jakRow[i] = -1;
}

CMediaPlayerDlg::~CMediaPlayerDlg()
{
	if (m_hLibDragImage) {
		ImageList_Destroy(m_hLibDragImage);
		m_hLibDragImage = NULL;
	}
	if (m_fmap) { free(m_fmap); m_fmap = NULL; }
	if (m_miss) { free(m_miss); m_miss = NULL; }
	for (int i = 0; i < kMpJakN; ++i) {
		if (m_jakBmp[i]) {
			::DeleteObject(m_jakBmp[i]);
			m_jakBmp[i] = NULL;
		}
	}
}

// DDX_Control は GetDlgItem 失敗時や二重 Subclass で CInvalidArgException になる。
// RC 未反映・IDずれ・自動 Subclass 済みでも Create を落とさない。
static void MpDdxControl(CDataExchange* pDX, int nIDC, CWnd& wnd)
{
	if (!pDX || !pDX->m_pDlgWnd) return;
	// 既に Subclass 済みなら二度目の DDX_Control は CInvalidArgException
	if (wnd.GetSafeHwnd()) return;
	HWND hDlg = pDX->m_pDlgWnd->GetSafeHwnd();
	if (!hDlg) return;
	HWND hCtrl = ::GetDlgItem(hDlg, nIDC);
	if (!hCtrl) return;
	// 別 CWnd が既に Subclass 済みなら DDX_Control は投げない
	if (CWnd::FromHandlePermanent(hCtrl)) return;
	try {
		DDX_Control(pDX, nIDC, wnd);
	}
	catch (CException* e) {
		e->Delete();
	}
}

void CMediaPlayerDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	MpDdxControl(pDX, IDC_MP_TITLE, m_title);
	MpDdxControl(pDX, IDC_MP_ARTIST, m_artist);
	MpDdxControl(pDX, IDC_MP_ALBUM, m_album);
	MpDdxControl(pDX, IDC_MP_LRC, m_lrc);
	MpDdxControl(pDX, IDC_MP_LRC2, m_lrc2);
	MpDdxControl(pDX, IDC_MP_LRC3, m_lrc3);
	MpDdxControl(pDX, IDC_MP_LRC4, m_lrc4);
	MpDdxControl(pDX, IDC_MP_LRC5, m_lrc5);
	MpDdxControl(pDX, IDC_MP_OS, m_os);
	MpDdxControl(pDX, IDC_MP_CPU, m_cpu);
	MpDdxControl(pDX, IDC_MP_OS3, m_os3);
	MpDdxControl(pDX, IDC_MP_TIME, m_time);
	MpDdxControl(pDX, IDC_MP_VOLVAL, m_volval);
	MpDdxControl(pDX, IDC_MP_VOL_L, m_vollabel);
	MpDdxControl(pDX, IDC_MP_SEEK, m_seek);
	MpDdxControl(pDX, IDC_MP_VOL, m_vol);
	MpDdxControl(pDX, IDC_MP_PREV, m_prev);
	MpDdxControl(pDX, IDC_MP_PLAY, m_play);
	MpDdxControl(pDX, IDC_MP_PAUSE, m_pause);
	MpDdxControl(pDX, IDC_MP_STOP, m_stop);
	MpDdxControl(pDX, IDC_MP_NEXT, m_next);
	MpDdxControl(pDX, IDC_MP_EQ, m_eq);
	MpDdxControl(pDX, IDC_MP_PIANO, m_piano);
	// IDC_MP_ANALYZER は RC に置かず動的生成(テンプレート差分と DDX 欠落例外を避ける)
	MpDdxControl(pDX, IDC_MP_SWITCHMODE, m_switch);
	MpDdxControl(pDX, IDC_MP_SETTINGS, m_settings);
	MpDdxControl(pDX, IDC_MP_EXIT, m_exit);
	MpDdxControl(pDX, IDC_MP_JACK, m_jacket);
	MpDdxControl(pDX, IDC_MP_FADEOUT, m_fadeout);
	MpDdxControl(pDX, IDC_MP_FOLDER, m_folder);
	MpDdxControl(pDX, IDC_MP_DSVOL, m_dsvol);
	MpDdxControl(pDX, IDC_MP_DSVOL_L, m_dsvolL);
	MpDdxControl(pDX, IDC_MP_KVOL, m_kvol);
	MpDdxControl(pDX, IDC_MP_KVOL_L, m_kvolL);
	MpDdxControl(pDX, IDC_MP_TEMPO, m_tempo);
	MpDdxControl(pDX, IDC_MP_TEMPO_L, m_tempoL);
	MpDdxControl(pDX, IDC_MP_PITCH, m_pitch);
	MpDdxControl(pDX, IDC_MP_PITCH_L, m_pitchL);
	MpDdxControl(pDX, IDC_MP_RENZOKU, m_renzoku);
	MpDdxControl(pDX, IDC_MP_LOOP, m_loop);
	MpDdxControl(pDX, IDC_MP_RANDOM, m_random);
	MpDdxControl(pDX, IDC_MP_XFADE, m_xfade);
	MpDdxControl(pDX, IDC_MP_XFADE_SEC, m_xfadeSec);
	MpDdxControl(pDX, IDC_MP_XFADE_L, m_xfadeL);
	MpDdxControl(pDX, IDC_MP_PLSEL, m_plsel);
	MpDdxControl(pDX, IDC_MP_PLRENAME, m_plrename);
	MpDdxControl(pDX, IDC_MP_PLDELETE, m_pldelete);
	MpDdxControl(pDX, IDC_MP_LSUP, m_lsup);
	MpDdxControl(pDX, IDC_MP_UP, m_up);
	MpDdxControl(pDX, IDC_MP_DOWN, m_down);
	MpDdxControl(pDX, IDC_MP_LSDOWN, m_lsdown);
	MpDdxControl(pDX, IDC_MP_ITEMDEL, m_itemdel);
	MpDdxControl(pDX, IDC_MP_M3U_EXPORT, m_m3uExport);
	MpDdxControl(pDX, IDC_MP_M3U_IMPORT, m_m3uImport);
	MpDdxControl(pDX, IDC_MP_FIND, m_find);
	MpDdxControl(pDX, IDC_MP_FINDUP, m_findup);
	MpDdxControl(pDX, IDC_MP_FINDDOWN, m_finddown);
	MpDdxControl(pDX, IDC_MP_SUPE, m_supe);
	MpDdxControl(pDX, IDC_MP_ST, m_st);
	MpDdxControl(pDX, IDC_MP_TIP, m_tip);
	MpDdxControl(pDX, IDC_MP_MINI, m_mini);
	MpDdxControl(pDX, IDC_MP_SAVEMP3, m_savemp3);
	MpDdxControl(pDX, IDC_MP_SAVEDS, m_saveds);
	MpDdxControl(pDX, IDC_MP_SAVEWAV, m_savewav);
	MpDdxControl(pDX, IDC_MP_MICMIX, m_micmix);
	MpDdxControl(pDX, IDC_MP_MICLEV, m_miclev);
	MpDdxControl(pDX, IDC_MP_MICLEV_L, m_miclevL);
	MpDdxControl(pDX, IDC_MP_MICMETER, m_micMeter);
	MpDdxControl(pDX, IDC_MP_MICDEV, m_micdev);
	MpDdxControl(pDX, IDC_MP_SAVEPARAM, m_saveparam);
	MpDdxControl(pDX, IDC_MP_RECORD, m_record);
	MpDdxControl(pDX, IDC_MP_CAPTURE, m_capture);
	MpDdxControl(pDX, IDC_MP_RESETDATA, m_resetdata);
	MpDdxControl(pDX, IDC_MP_KAISUU_L, m_kaisuuL);
	MpDdxControl(pDX, IDC_MP_KAISUU, m_kaisuu);
	MpDdxControl(pDX, IDC_MP_GRP_INFO, m_grpInfo);
	MpDdxControl(pDX, IDC_MP_GRP_SND, m_grpSnd);
	MpDdxControl(pDX, IDC_MP_GRP_PL, m_grpPl);
	MpDdxControl(pDX, IDC_MP_LIST, m_list);
}

BEGIN_MESSAGE_MAP(CMediaPlayerDlg, CCustomBlurDialogExBase)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_TIMER()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_WM_DROPFILES()
	ON_WM_DESTROY()
	ON_WM_CLOSE()
	ON_WM_HSCROLL()
	ON_BN_CLICKED(IDC_MP_PREV, &CMediaPlayerDlg::OnPrev)
	ON_BN_CLICKED(IDC_MP_PLAY, &CMediaPlayerDlg::OnPlay)
	ON_MESSAGE(WM_OGG_RESUME_PROMPT, &CMediaPlayerDlg::OnResumePrompt)
	ON_BN_CLICKED(IDC_MP_PAUSE, &CMediaPlayerDlg::OnPauseBtn)
	ON_BN_CLICKED(IDC_MP_STOP, &CMediaPlayerDlg::OnStopBtn)
	ON_BN_CLICKED(IDC_MP_NEXT, &CMediaPlayerDlg::OnNext)
	ON_BN_CLICKED(IDC_MP_EQ, &CMediaPlayerDlg::OnEq)
	ON_BN_CLICKED(IDC_MP_PIANO, &CMediaPlayerDlg::OnPiano)
	ON_BN_CLICKED(IDC_MP_ANALYZER, &CMediaPlayerDlg::OnAnalyzer)
	ON_BN_CLICKED(IDC_MP_PRO, &CMediaPlayerDlg::OnProTools)
	ON_BN_CLICKED(IDC_MP_FADEOUT, &CMediaPlayerDlg::OnFadeout)
	ON_BN_CLICKED(IDC_MP_FOLDER, &CMediaPlayerDlg::OnFolder)
	ON_BN_CLICKED(IDC_MP_EXIT, &CMediaPlayerDlg::OnExit)
	ON_BN_CLICKED(IDC_MP_JACK, &CMediaPlayerDlg::OnJacket)
	ON_BN_CLICKED(IDC_MP_SETTINGS, &CMediaPlayerDlg::OnSettings)
	ON_STN_CLICKED(IDC_MP_TEMPO_L, &CMediaPlayerDlg::OnTempoReset)
	ON_STN_CLICKED(IDC_MP_PITCH_L, &CMediaPlayerDlg::OnPitchReset)
	ON_BN_CLICKED(IDC_MP_SWITCHMODE, &CMediaPlayerDlg::OnSwitch)
	ON_BN_CLICKED(IDC_MP_RENZOKU, &CMediaPlayerDlg::OnRenzoku)
	ON_BN_CLICKED(IDC_MP_LOOP, &CMediaPlayerDlg::OnLoop)
	ON_BN_CLICKED(IDC_MP_RANDOM, &CMediaPlayerDlg::OnRandom)
	ON_BN_CLICKED(IDC_MP_XFADE, &CMediaPlayerDlg::OnPlayXfade)
	ON_EN_KILLFOCUS(IDC_MP_XFADE_SEC, &CMediaPlayerDlg::OnPlayXfadeSec)
	ON_CBN_SELCHANGE(IDC_MP_PLSEL, &CMediaPlayerDlg::OnPlSel)
	ON_CBN_DROPDOWN(IDC_MP_PLSEL, &CMediaPlayerDlg::OnPlselDropdown)
	ON_BN_CLICKED(IDC_MP_PLRENAME, &CMediaPlayerDlg::OnPlRename)
	ON_BN_CLICKED(IDC_MP_PLDELETE, &CMediaPlayerDlg::OnPlDelete)
	ON_BN_CLICKED(IDC_MP_LSUP, &CMediaPlayerDlg::OnMoveTop)
	ON_BN_CLICKED(IDC_MP_UP, &CMediaPlayerDlg::OnMoveUp)
	ON_BN_CLICKED(IDC_MP_DOWN, &CMediaPlayerDlg::OnMoveDown)
	ON_BN_CLICKED(IDC_MP_LSDOWN, &CMediaPlayerDlg::OnMoveBottom)
	ON_BN_CLICKED(IDC_MP_ITEMDEL, &CMediaPlayerDlg::OnItemDel)
	ON_BN_CLICKED(IDC_MP_M3U_EXPORT, &CMediaPlayerDlg::OnM3uExport)
	ON_BN_CLICKED(IDC_MP_M3U_IMPORT, &CMediaPlayerDlg::OnM3uImport)
	ON_BN_CLICKED(IDC_MP_SUPE, &CMediaPlayerDlg::OnSupe)
	ON_BN_CLICKED(IDC_MP_ST, &CMediaPlayerDlg::OnSt)
	ON_BN_CLICKED(IDC_MP_PROMPT, &CMediaPlayerDlg::OnPrompt)
	ON_BN_CLICKED(IDC_MP_CMDROLL, &CMediaPlayerDlg::OnCmdRoll)
	ON_BN_CLICKED(IDC_MP_TIP, &CMediaPlayerDlg::OnTip)
	ON_BN_CLICKED(IDC_MP_MINI, &CMediaPlayerDlg::OnMini)
	ON_BN_CLICKED(IDC_MP_SAVEMP3, &CMediaPlayerDlg::OnSaveMp3)
	ON_BN_CLICKED(IDC_MP_SAVEDS, &CMediaPlayerDlg::OnSaveDs)
	ON_BN_CLICKED(IDC_MP_SAVEWAV, &CMediaPlayerDlg::OnSaveWav)
	ON_BN_CLICKED(IDC_MP_MICMIX, &CMediaPlayerDlg::OnMicMix)
	ON_CBN_SELCHANGE(IDC_MP_MICDEV, &CMediaPlayerDlg::OnCbnSelchangeMicDev)
	ON_NOTIFY(NM_RELEASEDCAPTURE, IDC_MP_MICLEV, &CMediaPlayerDlg::OnMicLevRelease)
	ON_BN_CLICKED(IDC_MP_RECORD, &CMediaPlayerDlg::OnRecord)
	ON_BN_CLICKED(IDC_MP_CAPTURE, &CMediaPlayerDlg::OnCapture)
	ON_BN_CLICKED(IDC_MP_BOT_DJ, &CMediaPlayerDlg::OnMpDjPad)
	ON_BN_CLICKED(IDC_MP_BOT_TAG, &CMediaPlayerDlg::OnTagEdit)
	ON_BN_CLICKED(IDC_MP_BOT_BPM, &CMediaPlayerDlg::OnMpBpmDetect)
	ON_BN_CLICKED(IDC_MP_BOT_SLEEP, &CMediaPlayerDlg::OnBotSleep)
	ON_BN_CLICKED(IDC_MP_BOT_MIRROR, &CMediaPlayerDlg::OnMpMirror)
	ON_BN_CLICKED(IDC_MP_BOT_SSVIZ, &CMediaPlayerDlg::OnMpSsViz)
	ON_BN_CLICKED(IDC_MP_BOT_ALARM, &CMediaPlayerDlg::OnMpAlarm)
	ON_BN_CLICKED(IDC_MP_BOT_REMOTE, &CMediaPlayerDlg::OnMpRemote)
	ON_BN_CLICKED(IDC_MP_BOT_MAZE, &CMediaPlayerDlg::OnMpSoft3DMaze)
	ON_COMMAND(ID_MP_BOTVIS_DJ, &CMediaPlayerDlg::OnBotVisDj)
	ON_COMMAND(ID_MP_BOTVIS_TAG, &CMediaPlayerDlg::OnBotVisTag)
	ON_COMMAND(ID_MP_BOTVIS_BPM, &CMediaPlayerDlg::OnBotVisBpm)
	ON_COMMAND(ID_MP_BOTVIS_SLEEP, &CMediaPlayerDlg::OnBotVisSleep)
	ON_COMMAND(ID_MP_BOTVIS_MIRROR, &CMediaPlayerDlg::OnBotVisMirror)
	ON_COMMAND(ID_MP_BOTVIS_SSVIZ, &CMediaPlayerDlg::OnBotVisSsViz)
	ON_COMMAND(ID_MP_BOTVIS_ALARM, &CMediaPlayerDlg::OnBotVisAlarm)
	ON_COMMAND(ID_MP_BOTVIS_REMOTE, &CMediaPlayerDlg::OnBotVisRemote)
	ON_COMMAND(ID_MP_BOTVIS_MAZE, &CMediaPlayerDlg::OnBotVisMaze)
	ON_BN_CLICKED(IDC_MP_SAVEPARAM, &CMediaPlayerDlg::OnSaveParam)
	ON_BN_CLICKED(IDC_MP_RESETDATA, &CMediaPlayerDlg::OnResetData)
	ON_EN_KILLFOCUS(IDC_MP_KAISUU, &CMediaPlayerDlg::OnKaisuuKillFocus)
	ON_BN_CLICKED(IDC_MP_FINDUP, &CMediaPlayerDlg::OnFindUp)
	ON_BN_CLICKED(IDC_MP_FINDDOWN, &CMediaPlayerDlg::OnFindDown)
	ON_BN_CLICKED(IDC_MP_ABA, &CMediaPlayerDlg::OnAbSetA)
	ON_BN_CLICKED(IDC_MP_ABB, &CMediaPlayerDlg::OnAbSetB)
	ON_BN_CLICKED(IDC_MP_ABCLR, &CMediaPlayerDlg::OnAbClear)
	ON_BN_CLICKED(IDC_MP_SEEKLOCK, &CMediaPlayerDlg::OnSeekLock)
	ON_COMMAND(ID_MP_SEEK_LOCK, &CMediaPlayerDlg::OnSeekLock)
	ON_COMMAND(ID_MP_SEEK_ABCLR, &CMediaPlayerDlg::OnAbClear)
	ON_COMMAND(ID_MP_SEEK_SETA, &CMediaPlayerDlg::OnAbSetA)
	ON_COMMAND(ID_MP_SEEK_SETB, &CMediaPlayerDlg::OnAbSetB)
	ON_COMMAND(ID_MP_SEEK_WAVE, &CMediaPlayerDlg::OnSeekWaveToggle)
	ON_COMMAND(ID_MP_SEEK_CUEADD, &CMediaPlayerDlg::OnSeekCueAdd)
	ON_COMMAND(ID_MP_SEEK_CUECLR, &CMediaPlayerDlg::OnSeekCueClear)
	ON_COMMAND(ID_MP_SEEK_CUE1, &CMediaPlayerDlg::OnSeekCueJump1)
	ON_COMMAND(ID_MP_SEEK_CUE2, &CMediaPlayerDlg::OnSeekCueJump2)
	ON_COMMAND(ID_MP_SEEK_CUE3, &CMediaPlayerDlg::OnSeekCueJump3)
	ON_COMMAND(ID_MP_SEEK_CUE4, &CMediaPlayerDlg::OnSeekCueJump4)
	ON_COMMAND(ID_MP_SEEK_CUE5, &CMediaPlayerDlg::OnSeekCueJump5)
	ON_COMMAND(ID_MP_SEEK_CUE6, &CMediaPlayerDlg::OnSeekCueJump6)
	ON_COMMAND(ID_MP_SEEK_CUE7, &CMediaPlayerDlg::OnSeekCueJump7)
	ON_COMMAND(ID_MP_SEEK_CUE8, &CMediaPlayerDlg::OnSeekCueJump8)
	ON_COMMAND(ID_MP_PRACTICE_T50, &CMediaPlayerDlg::OnPracticeTempo50)
	ON_COMMAND(ID_MP_PRACTICE_T75, &CMediaPlayerDlg::OnPracticeTempo75)
	ON_COMMAND(ID_MP_PRACTICE_T100, &CMediaPlayerDlg::OnPracticeTempo100)
	ON_COMMAND(ID_MP_PHRASE_AB, &CMediaPlayerDlg::OnPhraseAbNow)
	ON_COMMAND(ID_MP_FILT_UNPLAYED, &CMediaPlayerDlg::OnFiltUnplayed)
	ON_COMMAND(ID_MP_FILT_MISSING, &CMediaPlayerDlg::OnFiltMissing)
	ON_COMMAND(ID_MP_FILT_CLEAR, &CMediaPlayerDlg::OnFiltClear)
	ON_COMMAND(ID_MP_MISS_MANAGE, &CMediaPlayerDlg::OnMissManage)
	ON_COMMAND(ID_MP_SMART_EDIT, &CMediaPlayerDlg::OnSmartEdit)
	ON_COMMAND(ID_MP_QUEUE_SHOW, &CMediaPlayerDlg::OnQueueShow)
	ON_COMMAND(ID_MP_EXPORT_AB_NOW, &CMediaPlayerDlg::OnExportAbNow)
	ON_COMMAND(ID_MP_NORM_SCAN, &CMediaPlayerDlg::OnNormScan)
	ON_COMMAND(ID_MP_NORM_LUFS14, &CMediaPlayerDlg::OnNormLufs14)
	ON_COMMAND(ID_MP_NORM_LUFS16, &CMediaPlayerDlg::OnNormLufs16)
	ON_COMMAND(ID_MP_NORM_LUFS18, &CMediaPlayerDlg::OnNormLufs18)
	ON_COMMAND(ID_MP_SLEEP_CUSTOM, &CMediaPlayerDlg::OnSleepCustom)
	ON_COMMAND(ID_MP_JACKET_SAVE_COVER, &CMediaPlayerDlg::OnJacketSaveCover)
	ON_COMMAND(ID_MP_TOOLS_PANEL, &CMediaPlayerDlg::OnToolsPanelToggle)
	ON_COMMAND_RANGE(ID_MP_SMART_BASE, ID_MP_SMART_BASE + MP_SMART_MAX - 1, &CMediaPlayerDlg::OnSmartApplyId)
	ON_COMMAND(ID_MP_QUEUE_ADD, &CMediaPlayerDlg::OnQueueAdd)
	ON_COMMAND(ID_MP_QUEUE_PLAYNEXT, &CMediaPlayerDlg::OnQueuePlayNext)
	ON_COMMAND(ID_MP_QUEUE_CLEAR, &CMediaPlayerDlg::OnQueueClear)
	ON_COMMAND(ID_MP_DUPES, &CMediaPlayerDlg::OnDupesScan)
	ON_COMMAND(ID_MP_FOLDER_SYNC, &CMediaPlayerDlg::OnFolderSyncDiff)
	ON_COMMAND(ID_MP_LRC_PLUS50, &CMediaPlayerDlg::OnLrcPlus50)
	ON_COMMAND(ID_MP_LRC_MINUS50, &CMediaPlayerDlg::OnLrcMinus50)
	ON_COMMAND(ID_MP_LRC_PLUS10, &CMediaPlayerDlg::OnLrcPlus10)
	ON_COMMAND(ID_MP_LRC_MINUS10, &CMediaPlayerDlg::OnLrcMinus10)
	ON_COMMAND(ID_MP_LRC_PLUS100, &CMediaPlayerDlg::OnLrcPlus100)
	ON_COMMAND(ID_MP_LRC_MINUS100, &CMediaPlayerDlg::OnLrcMinus100)
	ON_COMMAND(ID_MP_LRC_SAVE, &CMediaPlayerDlg::OnLrcSave)
	ON_COMMAND(ID_MP_DESK_LRC, &CMediaPlayerDlg::OnDeskLrcToggle)
	ON_COMMAND(ID_MP_TAG_EDIT, &CMediaPlayerDlg::OnTagEdit)
	ON_COMMAND(ID_MP_JACKET_RELOAD, &CMediaPlayerDlg::OnJacketReloadAlt)
	ON_COMMAND(ID_MP_JACKET_COVERJPG, &CMediaPlayerDlg::OnJacketPickCover)
	ON_COMMAND(ID_MP_EXPORT_AB, &CMediaPlayerDlg::OnExportAb)
	ON_COMMAND(ID_MP_AB_PACK, &CMediaPlayerDlg::OnAbPackExport)
	ON_COMMAND(ID_MP_NORM_BATCH, &CMediaPlayerDlg::OnNormBatch)
	ON_COMMAND(ID_MP_MB_AUTOTAG, &CMediaPlayerDlg::OnMbAutotag)
	ON_COMMAND(ID_MP_NORM_PREVIEW, &CMediaPlayerDlg::OnNormPreview)
	ON_COMMAND(ID_MP_AB_SNAP_A, &CMediaPlayerDlg::OnAbSnapA)
	ON_COMMAND(ID_MP_AB_SNAP_B, &CMediaPlayerDlg::OnAbSnapB)
	ON_COMMAND(ID_MP_AB_APPLY_A, &CMediaPlayerDlg::OnAbApplyA)
	ON_COMMAND(ID_MP_AB_APPLY_B, &CMediaPlayerDlg::OnAbApplyB)
	ON_COMMAND(ID_MP_AB_TOGGLE, &CMediaPlayerDlg::OnAbSnapToggle)
	ON_COMMAND(ID_MP_SLEEP_15, &CMediaPlayerDlg::OnSleep15)
	ON_COMMAND(ID_MP_SLEEP_30, &CMediaPlayerDlg::OnSleep30)
	ON_COMMAND(ID_MP_SLEEP_60, &CMediaPlayerDlg::OnSleep60)
	ON_COMMAND(ID_MP_SLEEP_OFF, &CMediaPlayerDlg::OnSleepOff)
	ON_COMMAND(ID_MP_XFADE_PREVIEW, &CMediaPlayerDlg::OnXfadePreviewToggle)
	ON_COMMAND(ID_MP_BEAT_GRID, &CMediaPlayerDlg::OnBeatGridToggle)
	ON_COMMAND(ID_MP_JACKET_REM_OVERLAY, &CMediaPlayerDlg::OnJacketRemOverlayToggle)
	ON_COMMAND(ID_MP_BPM_DETECT, &CMediaPlayerDlg::OnMpBpmDetect)
	ON_COMMAND(ID_MP_BPM_CAND1, &CMediaPlayerDlg::OnMpBpmCand1)
	ON_COMMAND(ID_MP_BPM_CAND2, &CMediaPlayerDlg::OnMpBpmCand2)
	ON_COMMAND(ID_MP_BPM_CAND3, &CMediaPlayerDlg::OnMpBpmCand3)
	ON_COMMAND(ID_MP_DJPAD, &CMediaPlayerDlg::OnMpDjPad)
	ON_COMMAND(ID_MP_ALARM, &CMediaPlayerDlg::OnMpAlarm)
	ON_COMMAND(ID_MP_MIRROR, &CMediaPlayerDlg::OnMpMirror)
	ON_COMMAND(ID_MP_REMOTE, &CMediaPlayerDlg::OnMpRemote)
	ON_COMMAND(ID_MP_REMOTE_DLG, &CMediaPlayerDlg::OnMpRemoteDlg)
	ON_COMMAND(ID_MP_REMOTE_BROWSER, &CMediaPlayerDlg::OnMpRemoteBrowser)
	ON_COMMAND(ID_MP_SSVIZ, &CMediaPlayerDlg::OnMpSsViz)
	ON_COMMAND(ID_MP_SOUNDMETER, &CMediaPlayerDlg::OnMpSoundMeter)
	ON_COMMAND(ID_MP_DIGITIZE, &CMediaPlayerDlg::OnMpDigitize)
	ON_COMMAND(ID_MP_VOICECHANGER, &CMediaPlayerDlg::OnMpVoiceChanger)
	ON_COMMAND(ID_MP_TUNERPRACTICE, &CMediaPlayerDlg::OnMpTunerPractice)
	ON_COMMAND(ID_MP_PHOTOFRAME, &CMediaPlayerDlg::OnMpPhotoFrame)
	ON_COMMAND(ID_MP_SOFT3DMAZE, &CMediaPlayerDlg::OnMpSoft3DMaze)
	ON_COMMAND(ID_MP_VIDEO_EXTRACT, &CMediaPlayerDlg::OnMpVideoExtract)
	ON_COMMAND(ID_MP_VIDEO_REPLACE, &CMediaPlayerDlg::OnMpVideoReplace)
	ON_COMMAND(ID_MP_GAME_PRESET, &CMediaPlayerDlg::OnMpGamePreset)
	ON_COMMAND_RANGE(ID_MP_GCP_720_60, ID_MP_GCP_PLUS_WAV, &CMediaPlayerDlg::OnMpGcpRange)
	ON_COMMAND(ID_MP_MIDI_IN, &CMediaPlayerDlg::OnMpMidiIn)
	ON_BN_CLICKED(IDC_MP_LRCEXPAND, &CMediaPlayerDlg::OnLrcExpand)
	ON_BN_CLICKED(IDC_MP_DESKLRC, &CMediaPlayerDlg::OnDeskLrcToggle)
	ON_BN_CLICKED(IDC_MP_TOOLSTOGGLE, &CMediaPlayerDlg::OnToolsToggle)
	ON_BN_CLICKED(IDC_MP_CHEATBTN, &CMediaPlayerDlg::OnCheatSheetBtn)
	ON_COMMAND(ID_HELP_SHOWSHEET, &CMediaPlayerDlg::OnCheatSheetBtn)
	ON_BN_CLICKED(IDC_MP_SORTNAME, &CMediaPlayerDlg::OnSortName)
	ON_BN_CLICKED(IDC_MP_SORTART, &CMediaPlayerDlg::OnSortArt)
	ON_BN_CLICKED(IDC_MP_SORTALB, &CMediaPlayerDlg::OnSortAlb)
	ON_BN_CLICKED(IDC_MP_SORTTIME, &CMediaPlayerDlg::OnSortTime)
	ON_BN_CLICKED(IDC_MP_ADDFOLDER, &CMediaPlayerDlg::OnAddFolder)
	ON_BN_CLICKED(IDC_MP_FINDFILTER, &CMediaPlayerDlg::OnFindFilter)
	ON_BN_CLICKED(IDC_MP_FINDREGEX, &CMediaPlayerDlg::OnFindRegex)
	ON_BN_CLICKED(IDC_MP_LIBTOGGLE, &CMediaPlayerDlg::OnLibToggle)
	ON_BN_CLICKED(IDC_MP_HISTTOGGLE, &CMediaPlayerDlg::OnHistToggle)
	ON_BN_CLICKED(IDC_MP_TEMPTOGGLE, &CMediaPlayerDlg::OnTempToggle)
	ON_BN_CLICKED(IDC_MP_TEMPCLEAR, &CMediaPlayerDlg::OnTempClear)
	ON_BN_CLICKED(IDC_MP_LIBADDROOT, &CMediaPlayerDlg::OnLibAddRoot)
	ON_BN_CLICKED(IDC_MP_LIBADDPL, &CMediaPlayerDlg::OnLibAddPl)
	ON_BN_CLICKED(IDC_MP_EMPTYFOLDER, &CMediaPlayerDlg::OnEmptyAddFolder)
	ON_BN_CLICKED(IDC_MP_EMPTYM3U, &CMediaPlayerDlg::OnEmptyM3u)
	ON_COMMAND(ID_MP_SPEANA_BAR, &CMediaPlayerDlg::OnSpeanaStyleBar)
	ON_COMMAND(ID_MP_SPEANA_MIRROR, &CMediaPlayerDlg::OnSpeanaStyleMirror)
	ON_COMMAND(ID_MP_SPEANA_WAVE, &CMediaPlayerDlg::OnSpeanaStyleWave)
	ON_COMMAND(ID_MP_CORR_METER, &CMediaPlayerDlg::OnCorrMeterToggle)
	ON_COMMAND(ID_MP_OPEN_ANALYZER, &CMediaPlayerDlg::OnAnalyzer)
	ON_COMMAND(ID_MP_OPEN_PIANOROLL, &CMediaPlayerDlg::OnPiano)
	ON_COMMAND(ID_MP_OPEN_EQ, &CMediaPlayerDlg::OnEq)
	ON_COMMAND(ID_MP_OPEN_PROTOOLS, &CMediaPlayerDlg::OnProTools)
	ON_COMMAND(ID_MP_LRC_EXPAND, &CMediaPlayerDlg::OnLrcExpand)
	ON_COMMAND(ID_MP_MICMIX_TOGGLE, &CMediaPlayerDlg::OnMicMixMenuToggle)
	ON_COMMAND_RANGE(ID_AUDIO_MIC_BASE, ID_AUDIO_LOOP_LAST, &CMediaPlayerDlg::OnAudioDevMenuRange)
	ON_COMMAND(ID_MP_REFRESH_JACKET, &CMediaPlayerDlg::OnRefreshJacket)
	ON_NOTIFY(TVN_SELCHANGED, IDC_MP_LIBTREE, &CMediaPlayerDlg::OnLibTreeSel)
	ON_NOTIFY(TVN_ITEMEXPANDING, IDC_MP_LIBTREE, &CMediaPlayerDlg::OnLibTreeExpanding)
	ON_NOTIFY(TVN_BEGINDRAG, IDC_MP_LIBTREE, &CMediaPlayerDlg::OnLibTreeBeginDrag)
	ON_NOTIFY(NM_DBLCLK, IDC_MP_LIBALBUMS, &CMediaPlayerDlg::OnLibAlbumDblClk)
	ON_NOTIFY(LVN_BEGINDRAG, IDC_MP_LIBALBUMS, &CMediaPlayerDlg::OnLibAlbumBeginDrag)
	ON_NOTIFY(NM_DBLCLK, IDC_MP_HISTLIST, &CMediaPlayerDlg::OnHistDblClk)
	ON_WM_RBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEWHEEL()
	ON_WM_ENTERSIZEMOVE()
	ON_WM_EXITSIZEMOVE()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_MP_LIST, &CMediaPlayerDlg::OnGetdispinfoList)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_MP_LIST, &CMediaPlayerDlg::OnListItemChanged)
	ON_NOTIFY(NM_DBLCLK, IDC_MP_LIST, &CMediaPlayerDlg::OnDblclkList)
	ON_NOTIFY(NM_CLICK, IDC_MP_LIST, &CMediaPlayerDlg::OnClickList)
	ON_NOTIFY(NM_RCLICK, IDC_MP_LIST, &CMediaPlayerDlg::OnRclickList)
	ON_NOTIFY(LVN_KEYDOWN, IDC_MP_LIST, &CMediaPlayerDlg::OnKeydownList)
	ON_NOTIFY(LVN_BEGINDRAG, IDC_MP_LIST, &CMediaPlayerDlg::OnBeginDragList)
	ON_NOTIFY(HDN_BEGINTRACKA, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_BEGINTRACKW, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_ENDTRACKA, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_ENDTRACKW, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_TRACKA, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_NOTIFY(HDN_TRACKW, IDC_MP_LIST, &CMediaPlayerDlg::OnListHeaderEndTrack)
	ON_MESSAGE(WM_MP_INFO_SCROLL, &CMediaPlayerDlg::OnInfoScrollTick)
	ON_MESSAGE(WM_MP_PLSEL_EXPAND, &CMediaPlayerDlg::OnPlselExpandPopup)
	ON_MESSAGE(WM_MP_MISS_DONE, &CMediaPlayerDlg::OnMissScanDone)
	ON_MESSAGE(WM_MP_JAK_DONE, &CMediaPlayerDlg::OnJakLoadDone)
	ON_MESSAGE(WM_MP_WAVE_DONE, &CMediaPlayerDlg::OnWaveOverviewDone)
	ON_MESSAGE(WM_MP_LIB_BUILD, &CMediaPlayerDlg::OnLibBuildLazy)
	ON_MESSAGE(WM_MP_TRANSPORT_CMD, &CMediaPlayerDlg::OnMpTransportCmd)
	ON_MESSAGE(WM_MP_HELP_HIGHLIGHT, &CMediaPlayerDlg::OnMpHelpHighlight)
	ON_WM_NCACTIVATE()
	ON_WM_SYSCOMMAND()
	ON_WM_MOVING()
END_MESSAGE_MAP()

static void MpMakePushToggle(CWnd* p)
{
	if (p && p->GetSafeHwnd())
		p->ModifyStyle(BS_TYPEMASK, BS_AUTOCHECKBOX | BS_PUSHLIKE | WS_TABSTOP);
}

static void MpSetPushToggle(CCustomStandardButton& btn, BOOL on,
	COLORREF onS, COLORREF onE, COLORREF offS, COLORREF offE)
{
	if (!btn.GetSafeHwnd()) return;
	btn.SetCheck(on ? BST_CHECKED : BST_UNCHECKED);
	btn.SetGradation(on ? onS : offS, on ? onE : offE, 0, TRUE);
	btn.EnsureAnimTimer();
	btn.RepaintClient();
}

int CMediaPlayerDlg::Create(CWnd* pParent)
{
	BOOL bret = CCustomBlurDialogExBase::Create(CMediaPlayerDlg::IDD, pParent);
	if (bret == TRUE) {
		ShowWindow(SW_SHOW);
		// OnInitDialog 時点では EnsureVisible が効かないことがあるため、
		// 表示確定後にプレイリスト側の選択位置へ復元する。
		InitListScrollPosition();
	}
	return bret;
}

BOOL CMediaPlayerDlg::OnInitDialog()
{
	// MFC の WindowProc 外側 CATCH に上げると ReportError で
	// 「引数が正しくありません」が出る。ここで必ず飲み込む。
	try {
	if (!CCustomBlurDialogExBase::OnInitDialog())
		return FALSE;

	SyncBannerSoft3DCamFromSave();

	// 子コントロールを親の再描画で塗り潰さない(スタティック消失・リスト欠け・ちらつき防止)
	ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

	// アナライザーボタンは RC 非依存で動的生成(DoLayout 前に HWND を確保)
	if (!m_analyzer.GetSafeHwnd() && m_piano.GetSafeHwnd()) {
		CRect rc;
		m_piano.GetWindowRect(&rc);
		ScreenToClient(&rc);
		if (rc.Width() < 1) rc.right = rc.left + 28;
		if (rc.Height() < 1) rc.bottom = rc.top + 12;
		const int gap = max(2, rc.Height() / 8);
		rc.OffsetRect(rc.Width() + gap, 0);
		if (!m_analyzer.Create(_T("アナ"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
			rc, this, IDC_MP_ANALYZER))
		{
			// 生成失敗時は以降のアナ操作をスキップ(Create 全体は落とさない)
		}
		else {
			// RC 由来の兄弟と違い Create はシステムフォントになるので、隣のボタンに揃える
			CFont* pFont = m_piano.GetFont();
			if (pFont)
				m_analyzer.SetFont(pFont);
		}
	}

	// 再生詳細(アナの右。EQ/ピアノ/アナと同列)
	if (!m_pro.GetSafeHwnd()) {
		CWnd* anchor = m_analyzer.GetSafeHwnd() ? (CWnd*)&m_analyzer
			: (m_piano.GetSafeHwnd() ? (CWnd*)&m_piano : NULL);
		if (anchor) {
			CRect rc;
			anchor->GetWindowRect(&rc);
			ScreenToClient(&rc);
			if (rc.Width() < 1) rc.right = rc.left + 28;
			if (rc.Height() < 1) rc.bottom = rc.top + 12;
			const int gap = max(2, rc.Height() / 8);
			rc.OffsetRect(rc.Width() + gap, 0);
			if (m_pro.Create(_T("Extra"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
				rc, this, IDC_MP_PRO))
			{
				CFont* pFont = anchor->GetFont();
				if (pFont)
					m_pro.SetFont(pFont);
			}
		}
	}

	// プロンプトボタン(スペアナの左)
	if (!m_prompt.GetSafeHwnd() && m_supe.GetSafeHwnd()) {
		CRect rc;
		m_supe.GetWindowRect(&rc);
		ScreenToClient(&rc);
		if (rc.Width() < 1) rc.right = rc.left + 28;
		if (rc.Height() < 1) rc.bottom = rc.top + 12;
		const int gap = max(2, rc.Height() / 8);
		rc.OffsetRect(-(rc.Width() + gap), 0);
		if (!m_prompt.Create(LL14(L"プロンプト", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"프롬프트", L"提示", L"موجه", L"Промпт", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"Istem"),
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_PROMPT))
		{
		}
		else {
			CFont* pFont = m_supe.GetFont();
			if (pFont)
				m_prompt.SetFont(pFont);
			m_prompt.SetGradation(RGB(255, 225, 245), RGB(255, 180, 210), 0, TRUE);
		}
	}

	// コマンドロール(プロンプトの左・沈むトグル)
	if (!m_cmdroll.GetSafeHwnd() && m_prompt.GetSafeHwnd()) {
		CRect rc;
		m_prompt.GetWindowRect(&rc);
		ScreenToClient(&rc);
		if (rc.Width() < 1) rc.right = rc.left + 28;
		if (rc.Height() < 1) rc.bottom = rc.top + 12;
		const int gap = max(2, rc.Height() / 8);
		rc.OffsetRect(-(rc.Width() + gap), 0);
		if (!m_cmdroll.Create(LL14(L"ロール", L"Roll", L"Rouleau", L"Roll", L"Roll", L"롤", L"卷轴", L"Roll", L"Roll", L"Roll", L"Roll", L"Roll", L"Roll", L"Rulo"),
			WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_CMDROLL))
		{
		}
		else {
			CFont* pFont = m_prompt.GetFont();
			if (pFont)
				m_cmdroll.SetFont(pFont);
			m_cmdroll.SetGradation(RGB(230, 220, 255), RGB(200, 185, 250), 0, TRUE);
		}
	}

	// A-B / 歌詞拡大 / 折りたたみツール帯 / フィルタ(RC非依存の動的生成)
	{
		CRect rc(0, 0, 28, 18);
		if (!m_abA.GetSafeHwnd())
			m_abA.Create(_T("A"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_ABA);
		if (!m_abB.GetSafeHwnd())
			m_abB.Create(_T("B"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_ABB);
		if (!m_abClr.GetSafeHwnd())
			m_abClr.Create(_T("A-B×"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_ABCLR);
		if (!m_seekLock.GetSafeHwnd())
			m_seekLock.Create(_T("Lock"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, rc, this, IDC_MP_SEEKLOCK);
		if (!m_lrcExpand.GetSafeHwnd())
			m_lrcExpand.Create(_T("▾"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_LRCEXPAND);
		if (!m_deskLrc.GetSafeHwnd())
			m_deskLrc.Create(_T("窓"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_DESKLRC);
		if (!m_toolsToggle.GetSafeHwnd())
			m_toolsToggle.Create(_T("ツール"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_TOOLSTOGGLE);
		if (!m_cheatBtn.GetSafeHwnd())
			m_cheatBtn.Create(_T("?"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_CHEATBTN);
		if (!m_sortName.GetSafeHwnd())
			m_sortName.Create(_T("Name"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_SORTNAME);
		if (!m_sortArt.GetSafeHwnd())
			m_sortArt.Create(_T("Art"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_SORTART);
		if (!m_sortAlb.GetSafeHwnd())
			m_sortAlb.Create(_T("Alb"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_SORTALB);
		if (!m_sortTime.GetSafeHwnd())
			m_sortTime.Create(_T("Time"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_SORTTIME);
		if (!m_addFolder.GetSafeHwnd())
			m_addFolder.Create(_T("Folder+"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_ADDFOLDER);
		if (!m_botDj.GetSafeHwnd())
			m_botDj.Create(_T("DJ"), WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_BOT_DJ);
		if (!m_botTag.GetSafeHwnd())
			m_botTag.Create(_T("Tag"), WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_BOT_TAG);
		if (!m_botBpm.GetSafeHwnd())
			m_botBpm.Create(_T("BPM"), WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_BOT_BPM);
		if (!m_botSleep.GetSafeHwnd())
			m_botSleep.Create(_T("Sleep"), WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_BOT_SLEEP);
		if (!m_botMirror.GetSafeHwnd())
			m_botMirror.Create(_T("Mirror"), WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_BOT_MIRROR);
		if (!m_botSsViz.GetSafeHwnd())
			m_botSsViz.Create(_T("SS"), WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_BOT_SSVIZ);
		if (!m_botAlarm.GetSafeHwnd())
			m_botAlarm.Create(_T("Alarm"), WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_BOT_ALARM);
		if (!m_botRemote.GetSafeHwnd())
			m_botRemote.Create(_T("Remote"), WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_BOT_REMOTE);
		if (!m_botMaze.GetSafeHwnd())
			m_botMaze.Create(_T("Maze"), WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_BOT_MAZE);
		{
			// ショートカットは色で役割が分かるようにする（水色一色は避ける）
			CCustomStandardButton* bots[9] = {
				&m_botDj, &m_botTag, &m_botBpm, &m_botSleep, &m_botMirror, &m_botSsViz, &m_botAlarm, &m_botRemote, &m_botMaze
			};
			const COLORREF botGrad[9][2] = {
				{ RGB(240, 225, 255), RGB(200, 170, 245) }, // DJ: 紫
				{ RGB(255, 235, 220), RGB(250, 195, 160) }, // Tag: 桃
				{ RGB(220, 250, 235), RGB(155, 220, 190) }, // BPM: ミント
				{ RGB(230, 225, 255), RGB(185, 180, 235) }, // Sleep: 薄藍紫
				{ RGB(220, 245, 255), RGB(160, 210, 240) }, // Mirror: 空
				{ RGB(255, 250, 220), RGB(240, 215, 150) }, // SS: 金
				{ RGB(255, 230, 240), RGB(245, 180, 205) }, // Alarm: 薔薇
				{ RGB(225, 250, 250), RGB(160, 215, 220) }, // Remote: 青緑
				{ RGB(235, 245, 255), RGB(150, 190, 230) }, // Maze: 青
			};
			for (int bi = 0; bi < 9; ++bi) {
				if (!bots[bi]->GetSafeHwnd()) continue;
				bots[bi]->SetGradation(botGrad[bi][0], botGrad[bi][1], 0, TRUE);
			}
		}
		if (!m_findFilter.GetSafeHwnd())
			m_findFilter.Create(_T("Filter"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, rc, this, IDC_MP_FINDFILTER);
		if (!m_findRegex.GetSafeHwnd())
			m_findRegex.Create(_T("Regex"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP, rc, this, IDC_MP_FINDREGEX);
		if (!m_lrcBadge.GetSafeHwnd())
			m_lrcBadge.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_ENDELLIPSIS, rc, this, IDC_MP_LRCBADGE);
		if (!m_lrcView.GetSafeHwnd())
			m_lrcView.Create(this, IDC_MP_LRCVIEW);
		EnsureLibControls();
		m_abA.SetGradation(RGB(220, 245, 255), RGB(160, 210, 240), 0, TRUE);
		m_abB.SetGradation(RGB(220, 245, 255), RGB(160, 210, 240), 0, TRUE);
		m_abClr.SetGradation(RGB(255, 230, 230), RGB(255, 180, 180), 0, TRUE);
		m_lrcExpand.SetGradation(RGB(240, 235, 255), RGB(210, 200, 245), 0, TRUE);
		UpdateDeskLrcBtnChrome();
		m_toolsToggle.SetGradation(RGB(220, 240, 255), RGB(180, 215, 250), 0, TRUE); // m3u と同系
		m_toolsToggle.SetFlat(TRUE); // 2px 白枠がメニュー後に白抜けに見えるのを防ぐ（Lib/Hist と同じ）
		if (m_cheatBtn.GetSafeHwnd()) {
			m_cheatBtn.SetFlat(TRUE);
			m_cheatBtn.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
			CCC_CaptionPlaceHelpBtn(m_hWnd, &m_cheatBtn);
		}
		m_sortName.SetGradation(RGB(230, 245, 255), RGB(190, 220, 245), 0, TRUE);
		m_sortArt.SetGradation(RGB(230, 245, 255), RGB(190, 220, 245), 0, TRUE);
		m_sortAlb.SetGradation(RGB(230, 245, 255), RGB(190, 220, 245), 0, TRUE);
		m_sortTime.SetGradation(RGB(230, 245, 255), RGB(190, 220, 245), 0, TRUE);
		m_addFolder.SetGradation(RGB(220, 240, 230), RGB(180, 220, 200), 0, TRUE);
		if (m_findFilter.GetSafeHwnd())
			m_findFilter.SetCheck(savedata.mpFindFilter ? BST_CHECKED : BST_UNCHECKED);
		if (m_findRegex.GetSafeHwnd())
			m_findRegex.SetCheck(savedata.mpFindRegex ? BST_CHECKED : BST_UNCHECKED);
		if (m_seekLock.GetSafeHwnd())
			m_seekLock.SetCheck(savedata.mpSeekLoopUnlock ? BST_UNCHECKED : BST_CHECKED);
	}

	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	// "バージョン情報..." メニュー項目をシステム メニュー(左上アイコンのメニュー)へ追加。
	// ファルコム特化型画面(COggDlg)と同じ挙動にそろえる。
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

	CDC* dc = GetDC();
	if (dc) {
		hD2 = (float)GetDeviceCaps(dc->m_hDC, LOGPIXELSX) / 96.0f;
		ReleaseDC(dc);
	}
	if (hD2 < 1.0f) hD2 = 1.0f;

	{
		CString cap = LL14(L"メディアプレイヤー「らいら」", L"Media Player \"Raira\"", L"Lecteur multimedia « Raira »", L"Lettore multimediale \"Raira\"", L"Reproductor multimedia \"Raira\"", L"미디어 플레이어 「라이라」", L"媒体播放器「莱拉」", L"مشغل الوسائط \"رايرا\"", L"Медиаплеер «Райра»", L"Media-Player \"Raira\"", L"Reprodutor de midia \"Raira\"", L"Mediaspeler \"Raira\"", L"Odtwarzacz multimediow \"Raira\"", L"Medya Oynatıcı \"Raira\"");
		cap += MpBuildVersionCaptionSuffix();
		SetWindowText(cap);
	}

	m_play.SetWindowText(LL14(L"▶ 再生", L"▶ Play", L"▶ Lire", L"▶ Play", L"▶ Play", L"▶ 재생", L"▶ 播放", L"▶ تشغيل", L"▶ Играть", L"▶ Play", L"▶ Play", L"▶ Play", L"▶ Odtwarzaj", L"▶ Oynat"));
	m_pause.SetWindowText(LL14(L"⏸ 一時停止", L"⏸ Pause", L"⏸ Pause", L"⏸ Pausa", L"⏸ Pausa", L"⏸ 일시정지", L"⏸ 暂停", L"⏸ إيقاف", L"⏸ Пауза", L"⏸ Pause", L"⏸ Pausar", L"⏸ Pauze", L"⏸ Pauza", L"⏸ Duraklat"));
	m_stop.SetWindowText(LL14(L"■ 停止", L"■ Stop", L"■ Stop", L"■ Stop", L"■ Stop", L"■ 정지", L"■ 停止", L"■ إيقاف", L"■ Стоп", L"■ Stopp", L"■ Parar", L"■ Stop", L"■ Stop", L"■ Durdur"));
	m_prev.SetWindowText(LL14(L"|◀", L"|◀", L"|◀", L"|◀", L"|◀", L"|◀", L"|◀", L"|◀", L"|◀", L"|◀", L"|◀", L"|◀", L"|◀", L"|◀"));
	m_next.SetWindowText(LL14(L"▶|", L"▶|", L"▶|", L"▶|", L"▶|", L"▶|", L"▶|", L"▶|", L"▶|", L"▶|", L"▶|", L"▶|", L"▶|", L"▶|"));
	if (m_abA.GetSafeHwnd())
		m_abA.SetWindowText(L"A");
	if (m_abB.GetSafeHwnd())
		m_abB.SetWindowText(L"B");
	if (m_abClr.GetSafeHwnd())
		m_abClr.SetWindowText(_T("A-B×")); // 56px: 長い訳語は Tip 側(addTip)
	if (m_seekLock.GetSafeHwnd())
		m_seekLock.SetWindowText(LL14(L"ロック", L"Lock", L"Verrou", L"Blocco", L"Bloqueo", L"잠금", L"锁定", L"قفل", L"Блок", L"Sperre", L"Trava", L"Slot", L"Blokada", L"Kilit"));
	if (m_lrcExpand.GetSafeHwnd())
		m_lrcExpand.SetWindowText(savedata.mpLrcExpand ? L"▴" : L"▾");
	UpdateQueueChrome();
	if (m_libToggle.GetSafeHwnd())
		m_libToggle.SetWindowText(savedata.mpLibOpen ? L"≪" : LL14(L"書庫", L"Lib", L"Lib", L"Lib", L"Lib", L"서고", L"库", L"مكتبة", L"Биб", L"Bib", L"Lib", L"Bib", L"Bib", L"Kit"));
	if (m_sortName.GetSafeHwnd())
		m_sortName.SetWindowText(LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"));
	if (m_sortArt.GetSafeHwnd())
		m_sortArt.SetWindowText(LL14(L"Art", L"Art", L"Art", L"Art", L"Art", L"Art", L"艺", L"فن", L"Исп", L"Art", L"Art", L"Art", L"Art", L"San"));
	if (m_sortAlb.GetSafeHwnd())
		m_sortAlb.SetWindowText(LL14(L"Alb", L"Alb", L"Alb", L"Alb", L"Alb", L"Alb", L"专", L"ألب", L"Альб", L"Alb", L"Alb", L"Alb", L"Alb", L"Alb"));
	if (m_sortTime.GetSafeHwnd())
		m_sortTime.SetWindowText(LL14(L"時間", L"Time", L"Duree", L"Durata", L"Tiempo", L"시간", L"时间", L"وقت", L"Время", L"Zeit", L"Tempo", L"Tijd", L"Czas", L"Sure"));
	if (m_addFolder.GetSafeHwnd())
		m_addFolder.SetWindowText(LL14(L"フォルダ追加", L"Add folder", L"Ajouter dossier", L"Aggiungi cartella", L"Anadir carpeta", L"폴더 추가", L"添加文件夹", L"إضافة مجلد", L"Добавить папку", L"Ordner hinzu", L"Add pasta", L"Map toevoegen", L"Dodaj folder", L"Klasor ekle"));
	if (m_findFilter.GetSafeHwnd())
		m_findFilter.SetWindowText(LL14(L"絞り込み", L"Filter", L"Filtrer", L"Filtra", L"Filtrar", L"필터", L"筛选", L"تصفية", L"Фильтр", L"Filter", L"Filtrar", L"Filteren", L"Filtruj", L"Filtrele"));
	if (m_findRegex.GetSafeHwnd())
		m_findRegex.SetWindowText(LL14(L"正規表現", L"Regex", L"Regex", L"Regex", L"Regex", L"정규식", L"正则", L"تعبير نمطي", L"Регулярка", L"Regex", L"Regex", L"Regex", L"Regex", L"Regex"));
	m_renzoku.SetWindowText(LL14(L"連続再生", L"Continuous", L"Lect. continue", L"Continua", L"Continua", L"연속 재생", L"连续播放", L"تشغيل متتابع", L"Подряд", L"Folge", L"Continuo", L"Doorlopend", L"Ciągłe", L"Sürekli çal"));
	m_loop.SetWindowText(LL14(L"ループ再生", L"Loop play", L"Lecture boucle", L"Riproduci loop", L"Repetir", L"루프 재생", L"循环播放", L"تشغيل متكرر", L"Цикл", L"Schleife", L"Repetir", L"Lus afspelen", L"Odtwarz. pętli", L"Donguye al"));
	m_random.SetWindowText(LL14(L"ランダム再生", L"Random play", L"Lect. aleatoire", L"Casuale", L"Aleatorio", L"랜덤 재생", L"随机播放", L"تشغيل عشوائي", L"Случайно", L"Zufall", L"Aleatorio", L"Willekeurig", L"Losowo", L"Rastgele cal"));
	m_xfade.SetWindowText(LL14(L"クロスフェード", L"Crossfade", L"Fondu croise", L"Crossfade", L"Fundido cruzado",
		L"크로스페이드", L"交叉淡化", L"تلاشي متقاطع", L"Кроссфейд", L"Crossfade",
		L"Crossfade", L"Crossfade", L"Przenikanie", L"Capraz gechis"));
	m_xfadeL.SetWindowText(LL14(L"秒", L"sec", L"s", L"s", L"s", L"초", L"秒", L"ث", L"с", L"s", L"s", L"s", L"s", L"sn"));
	SyncPlayXfadeUi(TRUE);
	m_eq.SetWindowText(LL14(L"イコライザー", L"Equalizer", L"Egaliseur", L"Equalizzatore", L"Ecualizador", L"이퀄라이저", L"均衡器", L"المعادل", L"Эквалайзер", L"Equalizer", L"Equalizador", L"Equalizer", L"Korektor", L"Ekolayzer"));
	m_piano.SetWindowText(LL14(L"簡易ピアノロール", L"Simple Piano Roll", L"Rouleau piano simple", L"Piano roll semplice", L"Rollo piano simple", L"간이 피아노 롤", L"简易钢琴卷帘", L"لوحة بيانو بسيطة", L"Простой пианоролл", L"Einfache Klavierrolle", L"Piano roll simples", L"Eenvoudige pianorol", L"Prosta rolka pianina", L"Basit piyano rulosu"));
	if (m_analyzer.GetSafeHwnd())
		m_analyzer.SetWindowText(LL14(L"アナライザー", L"Analyzer", L"Analyseur", L"Analizzatore", L"Analizador", L"분석기", L"分析器", L"المحلل", L"Анализатор", L"Analysator", L"Analisador", L"Analyser", L"Analizator", L"Analizor"));
	if (m_pro.GetSafeHwnd())
		m_pro.SetWindowText(LL14(L"詳細", L"Extra", L"Extra", L"Extra", L"Extra", L"상세", L"详情", L"تفاصيل", L"Доп.", L"Extra", L"Extra", L"Extra", L"Extra", L"Ek"));
	m_switch.SetWindowText(LL14(L"ファルコム特化型へ", L"To Falcom screen", L"Vers ecran Falcom", L"Alla schermata Falcom", L"A pantalla Falcom", L"팔콤 화면으로", L"切换到Falcom画面", L"إلى شاشة Falcom", L"К экрану Falcom", L"Zum Falcom-Bildschirm", L"Para tela Falcom", L"Naar Falcom-scherm", L"Do ekranu Falcom", L"Falcom ekranına"));
	m_settings.SetWindowText(LL14(L"設定", L"Settings", L"Reglages", L"Impostazioni", L"Ajustes", L"설정", L"设置", L"إعدادات", L"Настройки", L"Einstellungen", L"Config.", L"Instellingen", L"Ustawienia", L"Ayarlar"));
	m_jacket.SetWindowText(LL14(L"ジャケット", L"Jacket", L"Pochette", L"Copertina", L"Caratula", L"자켓", L"封面", L"الغلاف", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Okładka", L"Kapak"));
	m_exit.SetWindowText(LL14(L"終了", L"Exit", L"Quitter", L"Esci", L"Salir", L"종료", L"退出", L"خروج", L"Выход", L"Beenden", L"Sair", L"Afsluiten", L"Zakończ", L"Çıkış"));
	m_fadeout.SetWindowText(LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza", L"Desvanecer", L"페이드 아웃", L"淡出", L"تلاشي", L"Затухание", L"Ausblenden", L"Desvanecer", L"Uitfaden", L"Zanikanie", L"Soluklaştır"));
	m_folder.SetWindowText(LL14(L"フォルダ", L"Folder", L"Dossier", L"Cartella", L"Carpeta", L"폴더", L"文件夹", L"مجلد", L"Папка", L"Ordner", L"Pasta", L"Map", L"Folder", L"Klasor"));
	m_jacket.SetGradation(RGB(255, 235, 245), RGB(255, 200, 225), 0, TRUE);
	m_exit.SetGradation(RGB(255, 210, 210), RGB(255, 160, 160), 0, TRUE);
	m_fadeout.SetGradation(RGB(255, 235, 215), RGB(255, 200, 150), 0, TRUE);
	m_folder.SetGradation(RGB(220, 240, 230), RGB(180, 220, 200), 0, TRUE);
	m_vollabel.SetWindowText(LL14(L"!@C406848!@B主音量", L"!@C406848!@BVolume", L"!@C406848!@BVolume", L"!@C406848!@BVolume", L"!@C406848!@BVolumen", L"!@C406848!@B음량", L"!@C406848!@B音量", L"!@C406848!@Bالصوت", L"!@C406848!@BГромкость", L"!@C406848!@BLautstarke", L"!@C406848!@BVolume", L"!@C406848!@BVolume", L"!@C406848!@BGłośność", L"!@C406848!@BSes"));
	m_plrename.SetWindowText(LL14(L"名前変更", L"Rename", L"Renommer", L"Rinomina", L"Renombrar", L"이름변경", L"重命名", L"إعادة تسمية", L"Переименовать", L"Umbenennen", L"Renomear", L"Hernoemen", L"Zmień nazwę", L"Yeniden adlandır"));
	m_pldelete.SetWindowText(LL14(L"リスト削除", L"Delete list", L"Suppr. liste", L"Elimina lista", L"Eliminar lista", L"목록삭제", L"删除列表", L"حذف القائمة", L"Удалить список", L"Liste loschen", L"Excluir lista", L"Lijst wissen", L"Usuń listę", L"Listeyi sil"));
	m_itemdel.SetWindowText(LL14(L"曲削除", L"Remove", L"Retirer", L"Rimuovi", L"Quitar", L"곡삭제", L"删除曲目", L"حذف", L"Удалить", L"Entfernen", L"Remover", L"Verwijder", L"Usuń utwór", L"Parçayı sil"));
	m_m3uExport.SetWindowText(LL14(L"m3u出力", L"m3u export", L"Export m3u", L"Esporta m3u", L"Exportar m3u", L"m3u 내보내기", L"m3u导出", L"تصدير m3u", L"Экспорт m3u", L"m3u export", L"Exportar m3u", L"m3u export", L"Eksport m3u", L"m3u disa aktar"));
	m_m3uImport.SetWindowText(LL14(L"m3u入力", L"m3u import", L"Import m3u", L"Importa m3u", L"Importar m3u", L"m3u 가져오기", L"m3u导入", L"استيراد m3u", L"Импорт m3u", L"m3u import", L"Importar m3u", L"m3u import", L"Import m3u", L"m3u ice aktar"));
	m_supe.SetWindowText(LL14(L"スペアナ", L"Spectrum", L"Spectre", L"Spettro", L"Espectro", L"스펙트럼", L"频谱", L"الطيف", L"Спектр", L"Spektrum", L"Espectro", L"Spectrum", L"Widmo", L"Spektrum"));
	m_st.SetWindowText(LL14(L"ステレオ表示", L"Stereo view", L"Vue stereo", L"Vista stereo", L"Vista estereo", L"스테레오 표시", L"立体声显示", L"عرض ستيريو", L"Стерео", L"Stereo", L"Visao stereo", L"Stereo", L"Widok stereo", L"Stereo gosterim"));
	m_tip.SetWindowText(LL14(L"ツールチップ", L"Tooltips", L"Info-bulles", L"Suggerimenti", L"Sugerencias", L"툴팁", L"工具提示", L"تلميحات", L"Подсказки", L"Tooltips", L"Dicas", L"Tooltips", L"Etykiety", L"İpuçları"));
	m_mini.SetWindowText(LL14(L"最小化連動", L"Min. sync", L"Sync. min.", L"Sinc. min.", L"Sincr. min.", L"최소화 연동", L"最小化联动", L"تزامن التصغير", L"Синхр. сверт.", L"Min.-Sync", L"Sinc. min.", L"Min. koppelen", L"Synch. min.", L"Min. eşitle"));
	m_savemp3.SetWindowText(LL14(L"途中保存", L"Resume save", L"Reprise", L"Ripresa", L"Reanudar", L"위치저장", L"续播保存", L"حفظ الموضع", L"Позиция", L"Position", L"Retomar", L"Hervatten", L"Wznowienie", L"Konum kaydet"));
	m_saveds.SetWindowText(LL14(L"DShow途中保存", L"DShow resume", L"DShow reprise", L"DShow ripresa", L"DShow reanudar", L"DShow 위치저장", L"DShow续播", L"حفظ موضع DShow", L"DShow позиция", L"DShow Position", L"DShow retomar", L"DShow hervat", L"DShow wznow", L"DShow sürdür"));
	m_savewav.SetWindowText(LL14(L"WAVファイルへ保存", L"Save to WAV file", L"Enregistrer en WAV", L"Salva come WAV", L"Guardar como WAV", L"WAV 파일로 저장", L"保存到WAV文件", L"حفظ كـ WAV", L"Сохранить в WAV", L"Als WAV speichern", L"Salvar como WAV", L"Opslaan als WAV", L"Zapisz jako WAV", L"WAV olarak kaydet"));
	m_micmix.SetWindowText(LL14(L"マイクミックス", L"Mic mix", L"Mix micro", L"Mix microfono", L"Mezcla micro", L"마이크 믹스", L"麦克风混音", L"مزج الميكروفون", L"Микс микрофона", L"Mikrofon-Mix", L"Mix microfone", L"Mic-mix", L"Mix mikrofonu", L"Mikrofon karışımı"));
	m_miclevL.SetWindowText(LL14(L"マイク", L"Mic", L"Micro", L"Micro", L"Micro", L"마이크", L"麦克风", L"ميكروفون", L"Микрофон", L"Mikrofon", L"Microfone", L"Microfoon", L"Mikrofon", L"Mikrofon"));
	AudioMicDevFillCombo(m_micdev);
	m_record.SetWindowText(LL14(L"録音", L"Record", L"Enreg.", L"Registra", L"Grabar", L"녹음", L"录音", L"تسجيل", L"Запись", L"Aufnahme", L"Gravar", L"Opnemen", L"Nagraj", L"Kaydet"));
	m_capture.SetWindowText(LL14(L"キャプチャ", L"Capture", L"Capture", L"Cattura", L"Captura", L"캡처", L"捕获", L"التقاط", L"Захват", L"Aufnahme", L"Captura", L"Opname", L"Przechwyt", L"Yakala"));
	m_saveparam.SetWindowText(LL14(L"曲ごとに設定保存", L"Save per-song", L"Réglages/morceau", L"Impost. per brano", L"Ajustes por pista", L"곡별 설정 저장", L"逐曲保存设置", L"حفظ لكل أغنية", L"Настройки на трек", L"Pro Titel speichern", L"Config. por faixa", L"Per nummer opslaan", L"Ustaw. na utwor", L"Parça başına kaydet"));
	m_resetdata.SetWindowText(LL14(L"保存をリセット", L"Reset saved", L"Réinitialiser", L"Reimposta salvati", L"Restablecer", L"저장 초기화", L"重置已存", L"إعادة تعيين", L"Сброс сохран.", L"Zurücksetzen", L"Redefinir", L"Reset opgeslagen", L"Resetuj zapis", L"Kayıtı sıfırla"));
	m_kaisuuL.SetWindowText(LL14(L"ループ回数", L"Loop count", L"Nombre de boucles", L"Conteggio loop", L"Cuenta de bucle", L"루프 횟수", L"循环次数", L"عدد الحلقات", L"Количество повторов", L"Schleifenzahler", L"Contagem de loop", L"Loopaantal", L"Liczba petli", L"Dongu sayisi"));
	{
		CString ks; ks.Format(_T("%d"), savedata.kaisuu > 0 ? savedata.kaisuu : 2);
		m_kaisuu.SetWindowText(ks);
	}
	m_grpInfo.SetWindowText(LL14(L"情報", L"Info", L"Info", L"Info", L"Info", L"정보", L"信息", L"معلومات", L"Инфо", L"Info", L"Info", L"Info", L"Info", L"Bilgi"));
	m_grpSnd.SetWindowText(LL14(L"サウンド調整", L"Sound", L"Son", L"Audio", L"Sonido", L"사운드", L"声音", L"الصوت", L"Звук", L"Sound", L"Som", L"Geluid", L"Dźwięk", L"Ses"));
	m_grpPl.SetWindowText(LL14(L"プレイリスト", L"Playlist", L"Liste", L"Playlist", L"Lista", L"재생목록", L"播放列表", L"قائمة", L"Плейлист", L"Playlist", L"Lista", L"Playlist", L"Lista", L"Liste"));
	// グループ枠は最背面 + WS_CLIPSIBLINGS で、内側のコントロールを塗り潰さない
	m_grpInfo.ModifyStyle(0, WS_CLIPSIBLINGS);
	m_grpSnd.ModifyStyle(0, WS_CLIPSIBLINGS);
	m_grpPl.ModifyStyle(0, WS_CLIPSIBLINGS);
	// ButtonST(プレイリストと同じアイコン): 一番上/上/下/一番下 と あいまい検索 上/下
	m_lsup.SetIcon(IDR_SUP);    m_lsup.SetFlat(TRUE);
	m_up.SetIcon(IDR_UP);       m_up.SetFlat(TRUE);
	m_down.SetIcon(IDR_DOWN);   m_down.SetFlat(TRUE);
	m_lsdown.SetIcon(IDR_SDOWN); m_lsdown.SetFlat(TRUE);
	m_findup.SetIcon(IDR_DOWN); m_findup.SetFlat(TRUE);
	m_finddown.SetIcon(IDR_UP); m_finddown.SetFlat(TRUE);

	// サウンド調整スライダー(og の各スライダーと同じ範囲に合わせる)
	m_dsvol.SetRange(-498, 1); 
	m_kvol.SetRange(100, 900);
	m_tempo.SetRange(0, 400);   m_tempo.SetMode(1);
	m_pitch.SetRange(0, 400);   m_pitch.SetMode(1);

	// シークスライダーに選択範囲(緑)を有効化。リソースでは付いていないため
	// ここで付与しないと MirrorSeekVol の SetSelection(ループ範囲/緑追随)が描画されない。
	m_seek.ModifyStyle(0, TBS_ENABLESELRANGE);
	m_seek.SetSelectionLocked(savedata.mpSeekLoopUnlock ? FALSE : TRUE);
	m_seek.SetAB(m_abApos, m_abBpos);

	// 少し可愛い系の配色
	m_prev.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
	m_play.SetGradation(RGB(200, 240, 200), RGB(140, 210, 150), 0, TRUE);
	m_pause.SetGradation(RGB(255, 240, 200), RGB(255, 210, 140), 0, TRUE);
	m_stop.SetGradation(RGB(255, 215, 220), RGB(255, 170, 185), 0, TRUE);
	m_next.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
	m_eq.SetGradation(RGB(230, 220, 255), RGB(200, 185, 250), 0, TRUE);
	m_piano.SetGradation(RGB(230, 220, 255), RGB(200, 185, 250), 0, TRUE);
	if (m_analyzer.GetSafeHwnd())
		m_analyzer.SetGradation(RGB(230, 220, 255), RGB(200, 185, 250), 0, TRUE);
	if (m_pro.GetSafeHwnd())
		m_pro.SetGradation(RGB(230, 220, 255), RGB(200, 185, 250), 0, TRUE);
	m_switch.SetGradation(RGB(225, 210, 255), RGB(190, 170, 255), 0, TRUE);
	m_settings.SetGradation(RGB(255, 235, 205), RGB(255, 205, 150), 0, TRUE);
	m_plrename.SetGradation(RGB(220, 240, 255), RGB(180, 215, 250), 0, TRUE);
	m_pldelete.SetGradation(RGB(255, 220, 225), RGB(255, 180, 190), 0, TRUE);
	m_itemdel.SetGradation(RGB(255, 220, 225), RGB(255, 180, 190), 0, TRUE);
	m_m3uExport.SetGradation(RGB(220, 240, 255), RGB(180, 215, 250), 0, TRUE);
	m_m3uImport.SetGradation(RGB(220, 240, 255), RGB(180, 215, 250), 0, TRUE);
	MpMakePushToggle(&m_supe);
	MpMakePushToggle(&m_st);
	MpMakePushToggle(&m_eq);
	MpMakePushToggle(&m_piano);
	if (m_analyzer.GetSafeHwnd())
		MpMakePushToggle(&m_analyzer);
	if (m_prompt.GetSafeHwnd())
		MpMakePushToggle(&m_prompt);
	if (m_cmdroll.GetSafeHwnd())
		MpMakePushToggle(&m_cmdroll);
	MpSetPushToggle(m_supe, FALSE, RGB(140, 220, 160), RGB(80, 180, 110), RGB(215, 240, 220), RGB(175, 215, 190));
	MpSetPushToggle(m_st, FALSE, RGB(160, 200, 255), RGB(100, 150, 230), RGB(215, 230, 255), RGB(175, 200, 245));
	MpSetPushToggle(m_eq, FALSE, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
	MpSetPushToggle(m_piano, FALSE, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
	if (m_analyzer.GetSafeHwnd())
		MpSetPushToggle(m_analyzer, FALSE, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
	if (m_prompt.GetSafeHwnd())
		MpSetPushToggle(m_prompt, FALSE, RGB(255, 180, 210), RGB(255, 120, 170), RGB(255, 225, 245), RGB(255, 180, 210));
	if (m_cmdroll.GetSafeHwnd())
		MpSetPushToggle(m_cmdroll, FALSE, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
	CCustomControlUtility::SetControlBackgroundColor(&m_plsel, COLOR_COMBO_BG);
	// タイトルに淡いドロップシャドウで可愛く強調
	m_title.SetDropShadow(RGB(255, 220, 235), 0, 1, 0, TRUE);

	// リスト列(プレイリストと同じ並び)
	DWORD ex = m_list.GetExtendedStyle();
	ex |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP;
	m_list.SetExtendedStyle(ex);
	// Create 失敗後の Add は ENSURE→CInvalidArgException（「引数が正しくありません」）
	if (il.m_hImageList)
		il.DeleteImageList();
	// 行高は ♪ 相当(16px@96dpi)。ジャケ描画も同サイズに揃える。
	// 24*hD2 を ImageList に使うと高 DPI で行が間延びし、見える曲が半分近くになる。
	const int jakPx = max(16, (int)(16 * hD2 + 0.5f));
	if (il.Create(jakPx, jakPx, ILC_COLOR32 | ILC_MASK, 0, 1)) {
		HICON h1 = AfxGetApp()->LoadIcon(IDI_ICON1);
		HICON h2 = AfxGetApp()->LoadIcon(IDI_ICON2);
		HICON h3 = AfxGetApp()->LoadIcon(IDI_ICON3);
		if (h1) il.Add(h1);
		if (h2) il.Add(h2);
		if (h3) il.Add(h3);
		m_list.SetImageList(&il, LVSIL_SMALL);
	}
	m_list.m_mpJacketPx = jakPx;
	m_list.m_mpJacketGet = MpJacketGetCb;
	m_list.m_mpJacketCtx = this;
	m_list.m_mpNoteIconGet = MpNoteIconGetCb;
	m_list.m_mpRowMissGet = MpRowMissGetCb;
	m_list.m_bCol1IsRating = true;
	m_list.InsertColumn(0, LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"), LVCFMT_LEFT, (int)(220 * hD2));
	// 列1はレーティング(クリックで0〜5)。曲ごと保存[SAV] / 歌詞[LRC] / ch[MONO|LR|2.1…]は名前列先頭の印。
	m_list.InsertColumn(1, LL14(L"評価", L"Rate", L"Note", L"Voto", L"Nota", L"평점", L"评分", L"تقييم", L"Оценка", L"Bew.", L"Nota", L"Cijfer", L"Ocena", L"Puan"), LVCFMT_CENTER, (int)(42 * hD2));
	m_list.InsertColumn(2, LL14(L"ゲーム", L"Game", L"Jeu", L"Gioco", L"Juego", L"게임", L"游戏", L"لعبة", L"Игра", L"Spiel", L"Jogo", L"Spel", L"Gra", L"Oyun"), LVCFMT_LEFT, (int)(60 * hD2));
	m_list.InsertColumn(3, LL14(L"時間", L"Time", L"Duree", L"Durata", L"Duracion", L"시간", L"时间", L"الوقت", L"Время", L"Zeit", L"Duracao", L"Tijd", L"Czas", L"Sure"), LVCFMT_RIGHT, (int)(72 * hD2));
	m_list.InsertColumn(4, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Kunstler", L"Artista", L"Artiest", L"Artysta", L"Sanatçı"), LVCFMT_LEFT, (int)(160 * hD2));
	m_list.InsertColumn(5, LL14(L"アルバム/コメント", L"Album/Comment", L"Album/Comm.", L"Album/Comm.", L"Album/Com.", L"앨범/댓글", L"专辑/注释", L"الألبوم/تعليق", L"Альбом/Комм.", L"Album/Komm.", L"Album/Coment.", L"Album/Opm.", L"Album/Komentarz", L"Album/Yorum"), LVCFMT_LEFT, (int)(160 * hD2));

	// メディアプレイヤー側リストも pl->pc を参照してツールチップに保存パラメータを付記
	if (pl) m_list.pc = pl->pc;
	m_list.m_bSongParamTip = true;

	// 保存済みの列幅を復元(0=未設定なら上で設定した既定値のまま)
	// mpcol の意味スロットは ★挿入前と同じ: [0]=名前 [1]=ゲーム [2]=時間 [3]=アーティスト [4]=未使用
	// ★列は狭固定のため永続化しない。最終列(5=アルバム)は FitPlaylistLastColumn で右端フィット。
	savedata.mpcol[4] = 0;
	if (savedata.mpcol[0] > 0) {
		int w = savedata.mpcol[0];
		if (w > (int)(2000 * hD2)) w = (int)(2000 * hD2);
		m_list.SetColumnWidth(0, w); // 名前
	}
	if (savedata.mpcol[1] > 0) {
		int w = savedata.mpcol[1];
		if (w > (int)(2000 * hD2)) w = (int)(2000 * hD2);
		m_list.SetColumnWidth(2, w); // ゲーム
	}
	if (savedata.mpcol[2] > 0) {
		int w = savedata.mpcol[2];
		if (w > (int)(2000 * hD2)) w = (int)(2000 * hD2);
		if (w < (int)(72 * hD2))
			w = (int)(72 * hD2);   // 「取得不能」等が切れない最小幅
		m_list.SetColumnWidth(3, w); // 時間
	}
	if (savedata.mpcol[3] > 0) {
		int w = savedata.mpcol[3];
		if (w > (int)(2000 * hD2)) w = (int)(2000 * hD2);
		m_list.SetColumnWidth(4, w); // アーティスト
	}
	FitPlaylistLastColumn();
	// 列ドラッグ中も幅をライブ反映(HDN_TRACK + ヘッダー幅ポーリング)
	if (CHeaderCtrl* pHdr = m_list.GetHeaderCtrl()) {
		pHdr->ModifyStyle(0, HDS_FULLDRAG);
		pHdr->ModifyStyle(HDS_DRAGDROP, 0); // 列並べ替え中は幅追随不要
	}
	if (::IsWindow(m_list.GetSafeHwnd()))
		SetWindowSubclass(m_list.GetSafeHwnd(), ListHeaderNotifySubclassProc, kMpListHdrSubclassId, (DWORD_PTR)this);

	// フォント(.dat ずれで顔名が壊れていると CreateFont が失敗し得るため LF_FACESIZE に収める)
	TCHAR faceSafe[LF_FACESIZE];
	{
		LPCTSTR src = _tcslen(savedata.font2) ? savedata.font2
			: (_tcslen(savedata.font1) ? savedata.font1 : _T("メイリオ"));
		bool bad = false;
		for (LPCTSTR p = src; *p; ++p) {
			if ((unsigned short)*p < 0x20) { bad = true; break; }
		}
		if (bad || _tcslen(src) == 0 || _tcslen(src) >= LF_FACESIZE)
			src = _T("メイリオ");
		_tcsncpy_s(faceSafe, src, _TRUNCATE);
		auto makeFont = [&](CFont& f, int px, int weight) {
			if (f.GetSafeHandle()) f.DeleteObject();
			const int h = max(8, (int)(px * hD2 + 0.5f));
			if (!f.CreateFont(-h, 0, 0, 0, weight, 0, 0, 0, SHIFTJIS_CHARSET,
				OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, CLEARTYPE_QUALITY,
				DEFAULT_PITCH | FF_SWISS, faceSafe))
			{
				f.CreateFont(-h, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET,
					OUT_TT_PRECIS, CLIP_CHARACTER_PRECIS, CLEARTYPE_QUALITY,
					DEFAULT_PITCH | FF_SWISS, _T("Segoe UI"));
			}
		};
		makeFont(m_fontTitle, 20, FW_BOLD);
		makeFont(m_fontInfo, 13, FW_NORMAL);
		// 技術行(OS/ビット/RG/アップスケール)は長いので一段小さくして横スクロールを抑える
		makeFont(m_fontTech, 10, FW_NORMAL);
		// リスト・連続/ループ/回数/下部チェックは同一サイズ(拡大・縮小描画で差が出ないよう)
		makeFont(m_fontList, 12, FW_NORMAL);
		makeFont(m_fontChk, 12, FW_NORMAL);
	}
	m_tip.SetFont(&m_fontChk, TRUE);
	m_mini.SetFont(&m_fontChk, TRUE);
	m_savemp3.SetFont(&m_fontChk, TRUE);
	m_saveds.SetFont(&m_fontChk, TRUE);
	m_savewav.SetFont(&m_fontChk, TRUE);
	m_saveparam.SetFont(&m_fontChk, TRUE);
	m_renzoku.SetFont(&m_fontChk, TRUE);
	m_loop.SetFont(&m_fontChk, TRUE);
	m_random.SetFont(&m_fontChk, TRUE);
	if (m_seekLock.GetSafeHwnd())
		m_seekLock.SetFont(&m_fontChk, TRUE);
	m_kaisuuL.SetFont(&m_fontChk, TRUE);
	// PreferWideMode は縦に引き伸ばして「ループ回数」だけ巨大化するので使わない
	m_kaisuuL.SetPreferWideMode(FALSE);
	m_kaisuu.SetFont(&m_fontChk, TRUE);
	if (m_toolsToggle.GetSafeHwnd())
		m_toolsToggle.SetFont(&m_fontChk, TRUE);
	if (m_plrename.GetSafeHwnd())
		m_plrename.SetFont(&m_fontChk, TRUE);
	// Create() 動的ボタンはダイアログフォントを継がない → システム既定になり見た目がずれる
	if (m_sortName.GetSafeHwnd()) m_sortName.SetFont(&m_fontChk, TRUE);
	if (m_sortArt.GetSafeHwnd()) m_sortArt.SetFont(&m_fontChk, TRUE);
	if (m_sortAlb.GetSafeHwnd()) m_sortAlb.SetFont(&m_fontChk, TRUE);
	if (m_sortTime.GetSafeHwnd()) m_sortTime.SetFont(&m_fontChk, TRUE);
	if (m_addFolder.GetSafeHwnd()) m_addFolder.SetFont(&m_fontChk, TRUE);
	if (m_libToggle.GetSafeHwnd()) m_libToggle.SetFont(&m_fontChk, TRUE);
	if (m_histToggle.GetSafeHwnd()) m_histToggle.SetFont(&m_fontChk, TRUE);
	if (m_tempToggle.GetSafeHwnd()) m_tempToggle.SetFont(&m_fontChk, TRUE);
	if (m_botDj.GetSafeHwnd()) m_botDj.SetFont(&m_fontChk, TRUE);
	if (m_botTag.GetSafeHwnd()) m_botTag.SetFont(&m_fontChk, TRUE);
	if (m_botBpm.GetSafeHwnd()) m_botBpm.SetFont(&m_fontChk, TRUE);
	if (m_botSleep.GetSafeHwnd()) m_botSleep.SetFont(&m_fontChk, TRUE);
	if (m_botMirror.GetSafeHwnd()) m_botMirror.SetFont(&m_fontChk, TRUE);
	if (m_botSsViz.GetSafeHwnd()) m_botSsViz.SetFont(&m_fontChk, TRUE);
	if (m_botAlarm.GetSafeHwnd()) m_botAlarm.SetFont(&m_fontChk, TRUE);
	if (m_botRemote.GetSafeHwnd()) m_botRemote.SetFont(&m_fontChk, TRUE);
	if (m_botMaze.GetSafeHwnd()) m_botMaze.SetFont(&m_fontChk, TRUE);
	if (m_abA.GetSafeHwnd()) m_abA.SetFont(&m_fontChk, TRUE);
	if (m_abB.GetSafeHwnd()) m_abB.SetFont(&m_fontChk, TRUE);
	if (m_abClr.GetSafeHwnd()) m_abClr.SetFont(&m_fontChk, TRUE);
	if (m_lrcExpand.GetSafeHwnd()) m_lrcExpand.SetFont(&m_fontChk, TRUE);
	if (m_deskLrc.GetSafeHwnd()) m_deskLrc.SetFont(&m_fontChk, TRUE);
	if (m_cheatBtn.GetSafeHwnd()) m_cheatBtn.SetFont(&m_fontChk, TRUE);
	if (m_libAddRoot.GetSafeHwnd()) m_libAddRoot.SetFont(&m_fontChk, TRUE);
	if (m_libAddPl.GetSafeHwnd()) m_libAddPl.SetFont(&m_fontChk, TRUE);
	if (m_emptyFolder.GetSafeHwnd()) m_emptyFolder.SetFont(&m_fontChk, TRUE);
	if (m_emptyM3u.GetSafeHwnd()) m_emptyM3u.SetFont(&m_fontChk, TRUE);
	if (m_findFilter.GetSafeHwnd()) m_findFilter.SetFont(&m_fontChk, TRUE);
	if (m_findRegex.GetSafeHwnd()) m_findRegex.SetFont(&m_fontChk, TRUE);
	// タイトル/アーティスト/アルバムはバナーGDIに表示されるのでスタティックは隠す(縦幅節約)
	m_title.ShowWindow(SW_HIDE);
	m_artist.ShowWindow(SW_HIDE);
	m_album.ShowWindow(SW_HIDE);
	m_lrc.SetFont(&m_fontInfo, TRUE);
	m_lrc2.SetFont(&m_fontInfo, TRUE);
	m_lrc3.SetFont(&m_fontInfo, TRUE);
	m_lrc4.SetFont(&m_fontInfo, TRUE);
	m_lrc5.SetFont(&m_fontInfo, TRUE);
	m_dsvolL.SetFont(&m_fontInfo, TRUE);
	m_kvolL.SetFont(&m_fontInfo, TRUE);
	m_tempoL.SetFont(&m_fontInfo, TRUE);
	m_pitchL.SetFont(&m_fontInfo, TRUE);
	m_vollabel.SetFont(&m_fontInfo, TRUE);
	m_volval.SetFont(&m_fontInfo, TRUE);
	m_time.SetFont(&m_fontInfo, TRUE);
	m_list.SetFont(&m_fontList, TRUE);
	// ツールチップは SyncFromMain が m_tip を確定した後に ApplyListTooltipState で設定

	// タイトルは可愛くピンク強調
	m_title.SetGradation(RGB(255, 105, 180), RGB(150, 60, 160), 0, TRUE);

	m_vol.SetRange(0, 100);
	m_vol.SetPos(100);
	if (m_miclev.GetSafeHwnd()) {
		m_miclev.SetRange(0, 200);
		int lv = savedata.mic_mix_level;
		if (lv < 0) lv = 0;
		if (lv > 200) lv = 200;
		m_miclev.SetPos(lv);
	}
	if (m_micmix.GetSafeHwnd())
		m_micmix.SetCheck(savedata.mic_mix ? BST_CHECKED : BST_UNCHECKED);
	if (m_micmix.GetSafeHwnd()) m_micmix.SetFont(&m_fontChk, TRUE);
	if (m_miclevL.GetSafeHwnd()) m_miclevL.SetFont(&m_fontChk, TRUE);
	// 起動時からマイクミックスONでも、キャプチャは WAV 保存中のみ開始する。
	// (常時 eCapture はマイク付き PC で UI 全体を重くする)
	if (savedata.mic_mix && og && ::IsWindow(og->GetSafeHwnd())) {
		if (og->m_micmix.GetSafeHwnd())
			og->m_micmix.SetCheck(BST_CHECKED);
		og->OnMicMixCheck();
	}

	// 初期座標: 保存座標があればそれ、なければファルコム画面の位置・プレイリストの大きさ
	{
		int x = savedata.mpx, y = savedata.mpy, w = savedata.mpw, h = savedata.mph;
		if (!savedata.mpHasPos || w < 100 || h < 100 || w > 10000 || h > 10000) {
			RECT ro = { 0,0,0,0 };
			if (og && ::IsWindow(og->GetSafeHwnd()))
				og->GetWindowRect(&ro);
			x = (ro.left != 0 || ro.right != 0) ? ro.left : 100;
			y = (ro.top != 0 || ro.bottom != 0) ? ro.top : 100;
			w = (int)(580 * hD2);
			h = (int)(620 * hD2);
			if (pl && ::IsWindow(pl->GetSafeHwnd())) {
				RECT rp; pl->GetWindowRect(&rp);
				int pw = rp.right - rp.left, ph = rp.bottom - rp.top;
				if (pw > 200) w = pw;
				if (ph > 200) h = ph + (int)(360 * hD2); // プレイリスト分+情報/操作部
			}
		}
		// 最小サイズを下回らないようにクランプ(レイアウト崩れ防止)
		if (w < (int)(620 * hD2)) w = (int)(620 * hD2);
		if (h < (int)(560 * hD2)) h = (int)(560 * hD2);
		// どのモニタにも無ければ最近傍へ。サブモニタ上の x<0/y<0 は許可。
		CCC_ClampWindowPos(x, y, w, h);
		MoveWindow(x, y, w, h);
	}

	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this, TTS_BALLOON | TTS_NOPREFIX);
	auto addTip = [this](CWnd& w, LPCTSTR text) {
		if (!m_tooltip.GetSafeHwnd() || !text) return;
		HWND hw = w.GetSafeHwnd();
		if (!hw || !::IsWindow(hw)) return;
		m_tooltip.AddTool(&w, text);
	};
	addTip(m_switch, LL14(L"ファルコムbgm特化型画面へ戻します。", L"Return to the Falcom BGM dedicated screen.", L"Revenir a l'ecran Falcom.", L"Torna alla schermata Falcom.", L"Volver a la pantalla Falcom.", L"팔콤 전용 화면으로 돌아갑니다.", L"返回Falcom专用画面。", L"العودة إلى شاشة Falcom.", L"Вернуться к экрану Falcom.", L"Zum Falcom-Bildschirm zuruck.", L"Voltar para a tela Falcom.", L"Terug naar Falcom-scherm.", L"Powrot do ekranu Falcom.", L"Falcom ekranına dön."));
	addTip(m_prev, LL14(L"前の曲へ。再生中で曲頭から3秒以上なら今の曲の先頭へ、3秒未満なら前の曲へ。", L"Previous track. Restarts current if 3+ seconds in; otherwise goes to previous.", L"Piste precedente. Relance si >=3s, sinon precedente.", L"Traccia precedente. Riavvia se >=3s, altrimenti precedente.", L"Pista anterior. Reinicia si >=3s; si no, anterior.", L"이전 곡. 3초 이상이면 현재 곡 처음으로, 미만이면 이전 곡.", L"上一曲。开场3秒以上则从头，未满则上一曲。", L"المقطع السابق. إعادة إن >=3 ثوانٍ وإلا السابق.", L"Предыдущий. Сначала если >=3с, иначе предыдущий.", L"Vorheriger Titel. Neustart bei >=3s, sonst vorheriger.", L"Faixa anterior. Reinicia se >=3s; senao anterior.", L"Vorige track. Herstart bij >=3s, anders vorige.", L"Poprzedni. Od poczatku gdy >=3s, inaczej poprzedni.", L"Onceki parca. >=3 sn ise bastan, degilse onceki."));
	addTip(m_play, LL14(L"再生 / 選択曲を再生します。", L"Play / play the selected track.", L"Lire la piste selectionnee.", L"Riproduci la traccia selezionata.", L"Reproducir la pista seleccionada.", L"선택한 곡을 재생합니다.", L"播放所选曲目。", L"تشغيل المقطع المحدد.", L"Воспроизвести выбранный трек.", L"Ausgewahlten Titel abspielen.", L"Reproduzir a faixa selecionada.", L"Geselecteerde track afspelen.", L"Odtworz wybrany utwor.", L"Seçili parçayı çal."));
	addTip(m_pause, LL14(L"一時停止 / 再開します。", L"Pause / resume.", L"Pause / reprise.", L"Pausa / riprendi.", L"Pausar / reanudar.", L"일시정지 / 재개.", L"暂停/继续。", L"إيقاف مؤقت / استئناف.", L"Пауза / продолжить.", L"Pause / Fortsetzen.", L"Pausar / retomar.", L"Pauze / hervatten.", L"Pauza / wznow.", L"Duraklat / surdur."));
	addTip(m_stop, LL14(L"停止します。", L"Stop.", L"Arreter.", L"Ferma.", L"Detener.", L"정지합니다.", L"停止。", L"إيقاف.", L"Остановить.", L"Stoppen.", L"Parar.", L"Stoppen.", L"Zatrzymaj.", L"Durdur."));
	addTip(m_next, LL14(L"次の曲へ。", L"Next track.", L"Piste suivante.", L"Traccia successiva.", L"Pista siguiente.", L"다음 곡.", L"下一曲。", L"المقطع التالي.", L"Следующий трек.", L"Nachster Titel.", L"Proxima faixa.", L"Volgende track.", L"Następny utwór.", L"Sonraki parca."));
	addTip(m_renzoku, LL14(L"プレイリストを順番に連続再生します。", L"Play the playlist continuously in order.", L"Lecture continue dans l'ordre.", L"Riproduzione continua in ordine.", L"Reproduccion continua en orden.", L"순서대로 연속 재생.", L"按顺序连续播放。", L"تشغيل متواصل بالترتيب.", L"Непрерывное воспроизведение по порядку.", L"Fortlaufend in Reihenfolge abspielen.", L"Reproducao continua em ordem.", L"Doorlopend afspelen op volgorde.", L"Odtwarzaj po kolei.", L"Sırayla sürekli çal."));
	addTip(m_loop, LL14(L"選択した曲をループ再生します。", L"Loop the selected track.", L"Lire la piste en boucle.", L"Ripeti la traccia.", L"Repetir la pista.", L"선택한 곡을 반복 재생.", L"循环播放所选曲目。", L"تكرار المقطع المحدد.", L"Зациклить выбранный трек.", L"Ausgewahlten Titel wiederholen.", L"Repetir a faixa selecionada.", L"Geselecteerde track herhalen.", L"Zapętl wybrany utwór.", L"Seçili parçayı döngüye al."));
	addTip(m_random, LL14(L"ランダム再生 / 順次再生を切り替えます。", L"Toggle random / sequential play.", L"Lecture aleatoire / sequentielle.", L"Riproduzione casuale / sequenziale.", L"Reproduccion aleatoria / secuencial.", L"랜덤 / 순차 재생 전환.", L"切换随机/顺序播放。", L"تبديل التشغيل العشوائي/المتسلسل.", L"Случайное / последовательное.", L"Zufall / Reihenfolge umschalten.", L"Aleatorio / sequencial.", L"Willekeurig / opeenvolgend.", L"Losowo / po kolei.", L"Rastgele / sıralı."));
	addTip(m_xfade, LL14(L"連続再生時、曲のつなぎでフェードアウト／インします。", L"During continuous play, fade between tracks.", L"En lecture continue, fondu entre pistes.", L"In riproduzione continua, dissolve tra brani.", L"En reproduccion continua, fundido entre pistas.", L"연속 재생 시 곡 전환 페이드.", L"连续播放时曲间淡入淡出。", L"تلاشي بين المقاطع أثناء التشغيل المتتابع.", L"При непрерывном воспроизведении — кроссфейд.", L"Bei Dauerwiedergabe zwischen Titeln überblenden.", L"Na reproducao continua, crossfade entre faixas.", L"Bij doorlopend afspelen crossfaden.", L"Przy ciaglym odtwarzaniu przenikanie.", L"Surekli calmada parcilar arasi crossfade."));
	addTip(m_seek, LL14(L"再生位置。ピンク帯=ループ。青つまみ=A-B。⇔カーソルでつまみ移動、他はシーク。", L"Position. Pink=loop. Blue thumbs=A-B. Size cursor moves thumbs; else seek.", L"Position. Rose=boucle. Bleu=A-B. Curseur⇔=poignees.", L"Posizione. Rosa=loop. Blu=A-B. Cursore⇔=maniglie.", L"Posicion. Rosa=bucle. Azul=A-B. Cursor⇔=asas.", L"재생 위치. 분홍=루프. 파랑=A-B. ⇔커서=손잡이.", L"播放位置。粉=循环。蓝=A-B。⇔光标移动端点。", L"الموضع. وردي=حلقة. أزرق=A-B. مؤشر⇔=مقابض.", L"Позиция. Розовый=цикл. Синий=A-B. Курсор⇔=ручки.", L"Position. Rosa=Schleife. Blau=A-B. Cursor⇔=Griffe.", L"Posicao. Rosa=loop. Azul=A-B. Cursor⇔=alças.", L"Positie. Roze=lus. Blauw=A-B. Cursor⇔=grepen.", L"Pozycja. Rozowy=petla. Niebieski=A-B. Kursor⇔=uchwyty.", L"Konum. Pembe=dongu. Mavi=A-B. Imlec⇔=tutamac."));
	addTip(m_time, _T("%"));
	if (m_seekLock.GetSafeHwnd())
		addTip(m_seekLock, LL14(L"ONでループ(loop1/2)つまみを固定。OFFで可動。A-Bつまみは常に動かせます。", L"ON locks loop thumbs. OFF unlocks them. A-B thumbs always move.", L"ON verrouille les poignees de boucle. A-B toujours mobiles.", L"ON blocca le maniglie loop. A-B sempre mobili.", L"ON bloque asas de bucle. A-B siempre moviles.", L"ON이면 루프 손잡이 고정. A-B는 항상 이동 가능.", L"ON锁定循环端点。A-B端点始终可调。", L"ON يقفل مقابض الحلقة. A-B دائماً قابلة للتحريك.", L"ON блокирует ручки цикла. A-B всегда подвижны.", L"ON sperrt Schleifengriffe. A-B immer beweglich.", L"ON trava alças de loop. A-B sempre moveis.", L"ON vergrendelt lusgrepen. A-B altijd beweegbaar.", L"ON blokuje uchwyty petli. A-B zawsze ruchome.", L"ON dongu tutamaclarini kilitler. A-B her zaman hareket eder."));
	if (m_abA.GetSafeHwnd())
		addTip(m_abA, LL14(L"現在の再生位置をA点に設定します。", L"Set A point to the current position.", L"Definir le point A.", L"Imposta il punto A.", L"Fijar el punto A.", L"현재 위치를 A로.", L"将当前位置设为A点。", L"تعيين النقطة A.", L"Задать точку A.", L"Punkt A setzen.", L"Definir ponto A.", L"Punt A instellen.", L"Ustaw punkt A.", L"A noktasini ayarla."));
	if (m_abB.GetSafeHwnd())
		addTip(m_abB, LL14(L"現在の再生位置をB点に設定し、A-Bリピートを有効にします。", L"Set B point and enable A-B repeat.", L"Definir B et activer A-B.", L"Imposta B e attiva A-B.", L"Fijar B y activar A-B.", L"B로 설정 후 A-B 반복.", L"设置B点并启用A-B重复。", L"تعيين B وتفعيل A-B.", L"Задать B и включить A-B.", L"Punkt B setzen und A-B aktivieren.", L"Definir B e ativar A-B.", L"Punt B en A-B inschakelen.", L"Ustaw B i wlacz A-B.", L"B ayarla ve A-B ac."));
	if (m_abClr.GetSafeHwnd())
		addTip(m_abClr, LL14(L"A-Bリピートを解除します。", L"Clear A-B repeat.", L"Effacer A-B.", L"Cancella A-B.", L"Borrar A-B.", L"A-B 반복 해제.", L"清除A-B重复。", L"مسح تكرار A-B.", L"Сбросить A-B.", L"A-B aufheben.", L"Limpar A-B.", L"A-B wissen.", L"Wyczysc A-B.", L"A-B temizle."));
	addTip(m_vol, LL14(L"音量を調整します。", L"Adjust volume.", L"Regler le volume.", L"Regola il volume.", L"Ajustar el volumen.", L"음량을 조절합니다.", L"调整音量。", L"ضبط مستوى الصوت.", L"Регулировка громкости.", L"Lautstarke einstellen.", L"Ajustar o volume.", L"Volume aanpassen.", L"Reguluj głośność.", L"Sesi ayarla."));
	addTip(m_eq, LL14(L"イコライザーを開きます。右クリックでプリセット等。", L"Open the equalizer. Right-click for presets.", L"Ouvrir l'egaliseur. Clic droit: presets.", L"Apri l'equalizzatore. Tasto destro: preset.", L"Abrir el ecualizador. Clic derecho: presets.", L"이퀄라이저를 엽니다. 우클릭으로 프리셋.", L"打开均衡器。右键预设。", L"فتح المعادل. زر يمين: إعدادات.", L"Открыть эквалайзер. ПКМ — пресеты.", L"Equalizer offnen. RMB: Presets.", L"Abrir o equalizador. Clique direito: presets.", L"Equalizer openen. Rechtsklik: presets.", L"Otworz korektor. PPM: presety.", L"Ekolayzeri ac. Sag tik: onayarlar."));
	addTip(m_piano, LL14(L"簡易ピアノロールを開きます。", L"Open the simple piano roll.", L"Ouvrir le rouleau piano simple.", L"Apri il piano roll semplice.", L"Abrir el rollo de piano simple.", L"간이 피아노 롤을 엽니다.", L"打开简易钢琴卷帘。", L"فتح لوحة البيانو البسيطة.", L"Открыть простой пианоролл.", L"Einfache Klavierrolle offnen.", L"Abrir o piano roll simples.", L"Eenvoudige pianorol openen.", L"Otworz prosta rolke pianina.", L"Basit piyano rulosunu ac."));
	if (m_analyzer.GetSafeHwnd())
		addTip(m_analyzer, LL14(L"アナライザーを開きます。", L"Open the analyzer.", L"Ouvrir l'analyseur.", L"Apri l'analizzatore.", L"Abrir el analizador.", L"분석기를 엽니다.", L"打开分析器。", L"فتح المحلل.", L"Открыть анализатор.", L"Analysator offnen.", L"Abrir o analisador.", L"Analyser openen.", L"Otworz analizator.", L"Analizoru ac."));
	if (m_pro.GetSafeHwnd())
		addTip(m_pro, LL14(L"再生詳細(ギャップレス/RG/M/S/ループ/タグ/キュー)を開きます。", L"Open playback details (gapless/RG/M/S/loop/tags/cues).", L"Ouvrir les details de lecture.", L"Apri dettagli riproduzione.", L"Abrir detalles de reproduccion.", L"재생 상세를 엽니다.", L"打开播放详情。", L"فتح تفاصيل التشغيل.", L"Открыть параметры воспроизведения.", L"Wiedergabedetails oeffnen.", L"Abrir detalhes de reproducao.", L"Afspeeldetails openen.", L"Otworz szczegoly odtwarzania.", L"Oynatma ayrintilarini ac."));
	addTip(m_jacket, LL14(L"ジャケット画像を別窓で表示します。", L"Show cover art in a separate window.", L"Afficher la pochette.", L"Mostra la copertina.", L"Mostrar la caratula.", L"커버 이미지를 표시합니다.", L"在单独窗口显示封面。", L"عرض صورة الغلاف.", L"Показать обложку.", L"Cover anzeigen.", L"Mostrar a capa.", L"Toon hoes.", L"Pokaż okładkę.", L"Kapak resmini goster."));
	addTip(m_exit, LL14(L"アプリケーションを終了します。", L"Exit the application.", L"Quitter l'application.", L"Esci dall'applicazione.", L"Salir de la aplicacion.", L"앱을 종료합니다.", L"退出应用程序。", L"إنهاء التطبيق.", L"Выйти из приложения.", L"Anwendung beenden.", L"Sair do aplicativo.", L"Toepassing afsluiten.", L"Zamknij aplikację.", L"Uygulamadan çık."));
	// m_list のバルーンは「ツールチップ」OFF時のみ。ON時は行詳細ツールチップへ切替(ApplyListTooltipState)。
	addTip(m_settings, LL14(L"設定画面を開きます。右クリックでよく使う項目を即変更。", L"Open settings. Right-click for quick toggles.", L"Ouvrir les reglages. Clic droit pour raccourcis.", L"Apri le impostazioni. Tasto destro per scelte rapide.", L"Abrir ajustes. Clic derecho para atajos.", L"설정 화면을 엽니다. 우클릭으로 바로 변경.", L"打开设置。右键可快速切换常用项。", L"فتح الإعدادات. زر يمين للتبديل السريع.", L"Открыть настройки. ПКМ — быстрые переключатели.", L"Einstellungen offnen. RMB fur Schnelloptionen.", L"Abrir configuracoes. Clique direito para atalhos.", L"Instellingen openen. Rechtsklik voor snelle opties.", L"Otworz ustawienia. PPM = szybkie opcje.", L"Ayarları aç. Sağ tık ile hızlı degistir."));
	addTip(m_fadeout, LL14(L"再生中の曲をフェードアウトして停止します。右クリックで関連オプション。", L"Fade out and stop. Right-click for related options.", L"Fondu et arret. Clic droit pour options.", L"Dissolvenza e stop. Tasto destro per opzioni.", L"Desvanecer y detener. Clic derecho para opciones.", L"페이드 아웃하여 정지. 우클릭으로 관련 옵션.", L"淡出并停止。右键相关选项。", L"تلاشي وإيقاف. زر يمين للخيارات.", L"Затухание и остановка. ПКМ — опции.", L"Ausblenden und stoppen. RMB fur Optionen.", L"Desvanecer e parar. Clique direito para opcoes.", L"Uitfaden en stoppen. Rechtsklik voor opties.", L"Wycisz i zatrzymaj. PPM = opcje.", L"Soluklastirip durdur. Sag tik ile secenekler."));
	addTip(m_folder, LL14(L"フォルダ設定画面を開きます。右クリックで追加/同期ショートカット。", L"Open folder settings. Right-click for add/sync shortcuts.", L"Parametres dossier. Clic droit: ajouter/sync.", L"Impostazioni cartella. Tasto destro: aggiungi/sync.", L"Config. carpeta. Clic derecho: anadir/sync.", L"폴더 설정. 우클릭으로 추가/동기화.", L"打开文件夹设置。右键添加/同步。", L"إعدادات المجلد. زر يمين: إضافة/مزامنة.", L"Настройки папки. ПКМ — добавить/синхр.", L"Ordnereinstellungen. RMB: hinzufugen/sync.", L"Config. de pasta. Clique direito: adicionar/sync.", L"Mapinstellingen. Rechtsklik: toevoegen/sync.", L"Ustawienia folderu. PPM: dodaj/sync.", L"Klasor ayarlari. Sag tik: ekle/senkron."));
	addTip(m_dsvol, LL14(L"DirectSound音量を調整します。", L"Adjust DirectSound volume.", L"Reglez le volume DirectSound.", L"Regola il volume DirectSound.", L"Ajustar volumen DirectSound.", L"DirectSound 음량 조절.", L"调整DirectSound音量。", L"ضبط مستوى صوت DirectSound.", L"Громкость DirectSound.", L"DirectSound-Lautstarke.", L"Volume DirectSound.", L"DirectSound-volume.", L"Głośność DirectSound.", L"DirectSound sesi."));
	addTip(m_kvol, LL14(L"拡張音量(ブースト)を調整します。", L"Adjust extended (boost) volume.", L"Volume etendu (boost).", L"Volume esteso (boost).", L"Volumen extendido (boost).", L"확장(부스트) 음량 조절.", L"调整扩展(增益)音量。", L"ضبط الصوت الموسع (التعزيز).", L"Расширенная громкость (буст).", L"Erweiterte Lautstarke (Boost).", L"Volume estendido (boost).", L"Uitgebreid (boost) volume.", L"Rozszerzona głośność.", L"Genişletilmiş ses."));
	addTip(m_tempo, LL14(L"再生テンポを調整します(ラベルをクリックで100%に戻す)。", L"Adjust playback tempo (click label to reset to 100%).", L"Tempo de lecture (clic sur le label = 100%).", L"Tempo (clic sull'etichetta = 100%).", L"Tempo (clic en etiqueta = 100%).", L"재생 템포 조절(라벨 클릭 시 100%).", L"调整播放速度(点击标签恢复100%)。", L"ضبط الإيقاع (انقر التسمية لإعادة 100%).", L"Темп (клик по метке = 100%).", L"Tempo (Label klicken = 100%).", L"Tempo (clique no rotulo = 100%).", L"Tempo (klik label = 100%).", L"Tempo (etykieta = 100%).", L"Tempo (etikete tıkla = %100)."));
	addTip(m_pitch, LL14(L"再生ピッチ(音程)を調整します(ラベルをクリックで100%に戻す)。", L"Adjust playback pitch (click label to reset to 100%).", L"Hauteur (clic sur le label = 100%).", L"Altezza (clic sull'etichetta = 100%).", L"Tono (clic en etiqueta = 100%).", L"재생 피치 조절(라벨 클릭 시 100%).", L"调整音高(点击标签恢复100%)。", L"ضبط طبقة الصوت (انقر التسمية لإعادة 100%).", L"Высота (клик по метке = 100%).", L"Tonhohe (Label klicken = 100%).", L"Tom (clique no rotulo = 100%).", L"Toonhoogte (klik label = 100%).", L"Wysokosc (klik = 100%).", L"Perde (etikete tıkla = %100)."));
	addTip(m_plsel, LL14(L"プレイリストを切り替え/新規追加します。", L"Switch / add a playlist.", L"Changer / ajouter une liste.", L"Cambia / aggiungi playlist.", L"Cambiar / anadir lista.", L"재생목록 전환/추가.", L"切换/新建播放列表。", L"تبديل / إضافة قائمة.", L"Сменить / добавить плейлист.", L"Playlist wechseln / hinzufugen.", L"Trocar / adicionar lista.", L"Playlist wisselen/toevoegen.", L"Zmień/dodaj listę.", L"Liste değiştir/ekle."));
	addTip(m_plrename, LL14(L"現在のプレイリスト名を変更します。", L"Rename the current playlist.", L"Renommer la liste.", L"Rinomina la playlist.", L"Renombrar la lista.", L"현재 재생목록 이름 변경.", L"重命名当前播放列表。", L"إعادة تسمية القائمة.", L"Переименовать плейлист.", L"Playlist umbenennen.", L"Renomear a lista.", L"Lijst hernoemen.", L"Zmień nazwę listy.", L"Listeyi yeniden adlandır."));
	addTip(m_pldelete, LL14(L"現在のプレイリストを削除します。", L"Delete the current playlist.", L"Supprimer la liste.", L"Elimina la playlist.", L"Eliminar la lista.", L"현재 재생목록 삭제.", L"删除当前播放列表。", L"حذف القائمة.", L"Удалить плейлист.", L"Playlist loschen.", L"Excluir a lista.", L"Lijst verwijderen.", L"Usuń listę.", L"Listeyi sil."));
	addTip(m_m3uExport, LL14(L"現在のプレイリストをM3U形式で書き出します。", L"Export the current playlist as M3U.", L"Exporter la liste en M3U.", L"Esporta la playlist in M3U.", L"Exportar la lista como M3U.", L"현재 재생목록을 M3U로 내보냅니다.", L"将当前播放列表导出为M3U。", L"تصدير القائمة ك M3U.", L"Экспорт плейлиста в M3U.", L"Playlist als M3U exportieren.", L"Exportar lista como M3U.", L"Playlist exporteren als M3U.", L"Eksportuj liste do M3U.", L"Listeyi M3U olarak disa aktar."));
	addTip(m_m3uImport, LL14(L"プレイリストファイル(M3U/PLS等)を読み込みます。", L"Import a playlist file (M3U/PLS etc.).", L"Importer un fichier de liste.", L"Importa un file playlist.", L"Importar archivo de lista.", L"재생목록 파일을 가져옵니다.", L"导入播放列表文件。", L"استيراد ملف قائمة.", L"Импорт файла плейлиста.", L"Playlist-Datei importieren.", L"Importar arquivo de lista.", L"Playlistbestand importeren.", L"Importuj plik listy.", L"Oynatma listesi dosyasi ice aktar."));
	addTip(m_up, LL14(L"選択した曲を上へ移動します。", L"Move selected track up.", L"Monter la piste.", L"Sposta su.", L"Subir pista.", L"선택 곡을 위로.", L"上移所选曲目。", L"تحريك لأعلى.", L"Переместить вверх.", L"Nach oben.", L"Mover para cima.", L"Omhoog verplaatsen.", L"Przesuń w górę.", L"Yukarı taşı."));
	addTip(m_down, LL14(L"選択した曲を下へ移動します。", L"Move selected track down.", L"Descendre la piste.", L"Sposta giu.", L"Bajar pista.", L"선택 곡을 아래로.", L"下移所选曲目。", L"تحريك لأسفل.", L"Переместить вниз.", L"Nach unten.", L"Mover para baixo.", L"Omlaag verplaatsen.", L"Przesuń w dół.", L"Aşağı taşı."));
	addTip(m_itemdel, LL14(L"選択した曲をリストから削除します。", L"Remove selected track(s) from the list.", L"Retirer les pistes selectionnees.", L"Rimuovi le tracce selezionate.", L"Quitar pistas seleccionadas.", L"선택 곡을 목록에서 삭제.", L"从列表删除所选曲目。", L"حذف المقاطع المحددة.", L"Удалить выбранные треки.", L"Ausgewahlte Titel entfernen.", L"Remover faixas selecionadas.", L"Geselecteerde tracks verwijderen.", L"Usuń zaznaczone utwory.", L"Seçili parçaları sil."));
	addTip(m_supe, LL14(L"スペアナ表示を切り替えます。", L"Toggle spectrum display.", L"Afficher le spectre.", L"Mostra spettro.", L"Mostrar espectro.", L"스펙트럼 표시 전환.", L"切换频谱显示。", L"تبديل عرض الطيف.", L"Спектр вкл/выкл.", L"Spektrum umschalten.", L"Alternar espectro.", L"Spectrum wisselen.", L"Przełącz widmo.", L"Spektrumu değiştir."));
	if (m_prompt.GetSafeHwnd())
		addTip(m_prompt, LL14(L"演奏アレンジ用プロンプト(テキスト編集)を開きます。", L"Open the performance prompt (text editor).", L"Ouvrir le prompt (edition texte).", L"Apri prompt (editor testo).", L"Abrir prompt (editor de texto).", L"연주 프롬프트(텍스트 편집)를 엽니다.", L"打开演奏提示(文本编辑)。", L"فتح الموجه (تحرير النص).", L"Открыть промпт (текст).", L"Prompt (Texteditor) oeffnen.", L"Abrir prompt (editor de texto).", L"Prompt (teksteditor) openen.", L"Otworz prompt (edytor tekstu).", L"Istem (metin duzenleyici) ac."));
	if (m_cmdroll.GetSafeHwnd())
		addTip(m_cmdroll, LL14(L"プロンプトロールを開きます。時間軸でコマンドを配置・編集します。", L"Open the prompt roll. Place and edit timed commands on a timeline.", L"Ouvrir le rouleau de prompt.", L"Apri il prompt roll.", L"Abrir el prompt roll.", L"프롬프트 롤을 엽니다. 시간축에서 명령을 배치·편집합니다.", L"打开提示卷轴。在时间轴上放置/编辑命令。", L"فتح لفة الموجه.", L"Открыть prompt roll.", L"Prompt-Roll oeffnen.", L"Abrir prompt roll.", L"Prompt-roll openen.", L"Otworz prompt roll.", L"Prompt rulosunu ac."));
	addTip(m_st, LL14(L"スペアナのステレオ(L/R)表示を切り替えます。", L"Toggle stereo (L/R) spectrum view.", L"Afficher le spectre stereo L/R.", L"Mostra spettro stereo L/R.", L"Mostrar espectro estereo L/R.", L"스테레오(L/R) 스펙트럼 표시 전환.", L"切换立体声(L/R)频谱显示。", L"تبديل عرض الطيف الستيريو.", L"Переключить стерео-спектр.", L"Stereo-Spektrum umschalten.", L"Alternar espectro stereo.", L"Stereo spectrum wisselen.", L"Przelacz widmo stereo.", L"Stereo spektrumu degistir."));
	addTip(m_find, LL14(L"検索キーワード。絞り込みONで一致曲のみ表示、OFF時は▲▼で前後検索。正規表現ONでECMAScript正規表現(大小無視)。", L"Search keyword. Filter ON shows matches; OFF uses up/down jump. Regex ON = ECMAScript (case-insensitive).", L"Mot-cle. Filtre ON = liste. Regex ON = ECMAScript.", L"Parola. Filtro ON = elenco. Regex ON = ECMAScript.", L"Palabra. Filtro ON = lista. Regex ON = ECMAScript.", L"검색어. 필터 ON=일치만. 정규식 ON=ECMAScript.", L"关键字。筛选ON仅匹配。正则ON=ECMAScript。", L"كلمة. التصفية ON. Regex ON=ECMAScript.", L"Слово. Фильтр ON. Regex ON=ECMAScript.", L"Suchbegriff. Filter ON. Regex ON=ECMAScript.", L"Palavra. Filtro ON. Regex ON=ECMAScript.", L"Zoekterm. Filter ON. Regex ON=ECMAScript.", L"Slowo. Filtr ON. Regex ON=ECMAScript.", L"Kelime. Filtre ON. Regex ON=ECMAScript."));
	if (m_findFilter.GetSafeHwnd())
		addTip(m_findFilter, LL14(L"ONで検索語に一致する曲だけ表示します。OFFはジャンプ検索のみ。", L"ON: show only matching tracks. OFF: jump search only.", L"ON: filtrer. OFF: recherche seule.", L"ON: filtra. OFF: solo salto.", L"ON: filtrar. OFF: solo saltar.", L"ON: 일치 곡만. OFF: 점프만.", L"ON:仅显示匹配。OFF:仅跳转。", L"ON: تصفية. OFF: قفز فقط.", L"ON: фильтр. OFF: только переход.", L"ON: filtern. OFF: nur springen.", L"ON: filtrar. OFF: so saltar.", L"ON: filteren. OFF: alleen springen.", L"ON: filtruj. OFF: tylko skok.", L"ON: filtrele. OFF: sadece atla."));
	if (m_findRegex.GetSafeHwnd())
		addTip(m_findRegex, LL14(
			L"ONで正規表現検索(大小文字無視)。例: ^Ys|空の軌跡$  無効な式は一致なし。",
			L"ON: regex search (case-insensitive). e.g. ^Ys|Sky$  Invalid pattern matches nothing.",
			L"ON: recherche regex (insensible). Ex: ^Ys|Ciel$  Motif invalide = aucun match.",
			L"ON: ricerca regex (senza maiuscole). Es: ^Ys|Cielo$  Pattern non valido = nessun match.",
			L"ON: busqueda regex (sin mayusculas). Ej: ^Ys|Cielo$  Patron invalido = sin coincidencias.",
			L"ON: 정규식 검색(대소문자 무시). 예: ^Ys|궤적$  잘못된 식은 일치 없음.",
			L"ON:正则搜索(忽略大小写)。例: ^Ys|轨迹$  无效表达式不匹配。",
			L"ON: بحث regex (بدون حالة). مثال: ^Ys$  النمط غير صالح = لا تطابق.",
			L"ON: regex (без регистра). Пример: ^Ys$  Неверное выражение = нет совпадений.",
			L"ON: Regex-Suche (ohne Gross/Klein). z.B. ^Ys$  Ungueltig = kein Treffer.",
			L"ON: busca regex (sem maiusculas). Ex: ^Ys$  Padrao invalido = sem match.",
			L"ON: regex-zoeken (hoofdletterongevoelig). Bijv. ^Ys$  Ongeldig = geen treffer.",
			L"ON: wyszukiwanie regex (bez wielkości liter). Np. ^Ys$  Błędny wzorzec = brak.",
			L"ON: regex arama (büyük/küçük duyarsız). Örn: ^Ys$  Geçersiz ifade = eşleşme yok."));
	if (m_lrcExpand.GetSafeHwnd())
		addTip(m_lrcExpand, LL14(L"左クリック: 歌詞パネル拡大（カラオケ風）。右クリック: 歌詞メニュー。", L"Left-click: expand lyrics (karaoke). Right-click: lyrics menu.", L"Clic gauche: agrandir. Clic droit: menu paroles.", L"Clic sinistro: espandi. Clic destro: menu testi.", L"Clic izq.: ampliar. Clic der.: menu letra.", L"왼쪽 클릭: 가사 확대. 오른쪽 클릭: 가사 메뉴.", L"左键：扩大歌词。右键：歌词菜单。", L"نقر يسار: توسيع. نقر يمين: قائمة الكلمات.", L"ЛКМ: расширить. ПКМ: меню текста.", L"Linksklick: erweitern. Rechtsklick: Textmenue.", L"Clique esq.: ampliar. Clique dir.: menu de letra.", L"Linksklik: uitklappen. Rechtsklik: tekstmenu.", L"LPM: rozszerz. PPM: menu tekstu.", L"Sol tik: genislet. Sag tik: soz menusu."));
	if (m_deskLrc.GetSafeHwnd())
		addTip(m_deskLrc, LL14(L"歌詞ウィンドウ（常時最前面）の表示切替。右クリックで歌詞メニュー。", L"Toggle always-on-top lyrics window. Right-click for lyrics menu.", L"Afficher/masquer la fenetre paroles. Clic droit: menu.", L"Mostra/nascondi finestra testi. Tasto destro: menu.", L"Mostrar/ocultar ventana de letra. Clic der.: menu.", L"항상 위 가사 창 표시 전환. 우클릭: 가사 메뉴.", L"切换置顶歌词窗口。右键：歌词菜单。", L"إظهار/إخفاء نافذة الكلمات. زر يمين: القائمة.", L"Показать/скрыть окно текста. ПКМ: меню.", L"Textfenster ein/aus. RMB: Textmenue.", L"Mostrar/ocultar janela de letra. Clique dir.: menu.", L"Songtekstvenster aan/uit. Rechtsklik: menu.", L"Pokaz/ukryj okno tekstu. PPM: menu.", L"Soz penceresini ac/kapat. Sag tik: menu."));
	if (m_toolsToggle.GetSafeHwnd())
		addTip(m_toolsToggle, LL14(L"クリックでメニュー（並べ替え・欠損整理など）。", L"Click for menu (sort, missing manage, etc.).", L"Clic = menu.", L"Clic = menu.", L"Clic = menu.", L"클릭=메뉴.", L"点击打开菜单。", L"انقر للقائمة.", L"Клик = меню.", L"Klick = Menue.", L"Clique = menu.", L"Klik = menu.", L"Klik = menu.", L"Tikla = menu."));
	if (m_cheatBtn.GetSafeHwnd())
		addTip(m_cheatBtn, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
	if (m_lrcBadge.GetSafeHwnd())
		addTip(m_lrcBadge, LL14(L"歌詞の有無。LRC●=ローカル、net=取得、—=なし。", L"Lyrics status. LRC●=local, net=fetched, —=none.", L"Paroles: LRC●/net/—.", L"Testi: LRC●/net/—.", L"Letra: LRC●/net/—.", L"가사: LRC●/net/—.", L"歌词: LRC●/net/—.", L"كلمات: LRC●/net/—.", L"Текст: LRC●/net/—.", L"Text: LRC●/net/—.", L"Letra: LRC●/net/—.", L"Tekst: LRC●/net/—.", L"Tekst: LRC●/net/—.", L"Soz: LRC●/net/—."));
	if (m_addFolder.GetSafeHwnd())
		addTip(m_addFolder, LL14(L"フォルダを選んで配下の音源をプレイリストへ追加します。", L"Browse a folder and add audio files under it.", L"Ajouter les fichiers audio d'un dossier.", L"Aggiungi audio da cartella.", L"Anadir audio de una carpeta.", L"폴더의 음원을 목록에 추가.", L"选择文件夹并添加其下音频。", L"إضافة ملفات الصوت من مجلد.", L"Добавить аудио из папки.", L"Audio aus Ordner hinzufugen.", L"Adicionar audio de pasta.", L"Audio uit map toevoegen.", L"Dodaj audio z folderu.", L"Klasorden ses ekle."));
	if (m_libToggle.GetSafeHwnd())
		addTip(m_libToggle, LL14(L"ライブラリ(フォルダツリー＋アルバム)を開閉します。", L"Toggle library (folder tree + albums).", L"Afficher/masquer la bibliotheque.", L"Apri/chiudi libreria.", L"Abrir/cerrar biblioteca.", L"라이브러리 열기/닫기.", L"打开/关闭库。", L"فتح/إغلاق المكتبة.", L"Открыть/закрыть библиотеку.", L"Bibliothek ein-/ausblenden.", L"Abrir/fechar biblioteca.", L"Bibliotheek openen/sluiten.", L"Otwórz/zamknij bibliotekę.", L"Kitapligi ac/kapat."));
	if (m_histToggle.GetSafeHwnd())
		addTip(m_histToggle, LL14(L"再生履歴を開閉します。ダブルクリックで再生。", L"Toggle play history. Double-click to play.", L"Historique de lecture.", L"Cronologia di riproduzione.", L"Historial de reproduccion.", L"재생 기록 열기/닫기.", L"打开/关闭播放历史。", L"فتح/إغلاق سجل التشغيل.", L"История воспроизведения.", L"Wiedergabehistorie.", L"Historico de reproducao.", L"Afspeelgeschiedenis.", L"Historia odtwarzania.", L"Calma gecmisi."));
	if (m_libAddRoot.GetSafeHwnd())
		addTip(m_libAddRoot, LL14(L"ライブラリのルートフォルダを追加します。", L"Add a library root folder.", L"Ajouter une racine.", L"Aggiungi radice.", L"Anadir raiz.", L"라이브러리 루트 추가.", L"添加库根文件夹。", L"إضافة جذر المكتبة.", L"Добавить корень библиотеки.", L"Bibliothekswurzel hinzufugen.", L"Adicionar raiz.", L"Wortel toevoegen.", L"Dodaj korzen.", L"Kok klasor ekle."));
	if (m_libAddPl.GetSafeHwnd())
		addTip(m_libAddPl, LL14(L"選択中のアルバム/フォルダをプレイリストへ追加します。", L"Add selected album/folder to the playlist.", L"Ajouter l album/dossier a la liste.", L"Aggiungi album/cartella alla playlist.", L"Anadir album/carpeta a la lista.", L"선택 앨범/폴더를 목록에 추가.", L"将所选专辑/文件夹加入播放列表。", L"إضافة الألبوم/المجلد للقائمة.", L"Добавить альбом/папку в плейлист.", L"Album/Ordner zur Playlist.", L"Adicionar album/pasta a lista.", L"Album/map aan playlist.", L"Dodaj album/folder do listy.", L"Secili album/klasoru listeye ekle."));
	if (m_tempToggle.GetSafeHwnd())
		addTip(m_tempToggle, LL14(L"一時プレイリスト。ON(凹み)中の曲は保存されず、アプリ終了で破棄されます。", L"Temporary playlist. While pressed(ON), tracks are not saved and are discarded when the app closes.", L"Liste temporaire. Enfonce(ON): non enregistree, effacee a la fermeture.", L"Playlist temporanea. Premuto(ON): non salvata, scartata alla chiusura.", L"Lista temporal. Hundido(ON): no se guarda; se descarta al cerrar.", L"임시 재생목록. 눌림(ON) 중에는 저장되지 않으며 종료 시 버려집니다.", L"临时播放列表。按下(ON)时不保存，关闭应用后丢弃。", L"قائمة مؤقتة. مضغوط(ON): لا تُحفظ وتُهمل عند الإغلاق.", L"Временный плейлист. Нажат(ON): не сохраняется, сбрасывается при выходе.", L"Temporaere Playlist. Gedrueckt(ON): wird nicht gespeichert und beim Beenden verworfen.", L"Lista temporaria. Pressionado(ON): nao e salva; descartada ao fechar.", L"Tijdelijke playlist. Ingedrukt(ON): niet opgeslagen; weg bij afsluiten.", L"Lista tymczasowa. Wcisniety(ON): bez zapisu; znika po zamknieciu.", L"Gecici liste. Basili(ON): kaydedilmez; kapaninca silinir."));
	if (m_sortName.GetSafeHwnd())
		addTip(m_sortName, LL14(L"名前で並べ替え(再クリックで昇順/降順)。", L"Sort by name (click again to toggle asc/desc).", L"Trier par nom.", L"Ordina per nome.", L"Ordenar por nombre.", L"이름으로 정렬.", L"按名称排序。", L"ترتيب حسب الاسم.", L"Сортировка по имени.", L"Nach Name sortieren.", L"Ordenar por nome.", L"Sorteren op naam.", L"Sortuj wg nazwy.", L"Ada gore sirala."));
	if (m_sortArt.GetSafeHwnd())
		addTip(m_sortArt, LL14(L"アーティストで並べ替え(再クリックで昇順/降順)。", L"Sort by artist (click again to toggle asc/desc).", L"Trier par artiste.", L"Ordina per artista.", L"Ordenar por artista.", L"아티스트로 정렬.", L"按艺术家排序。", L"ترتيب حسب الفنان.", L"Сортировка по исполнителю.", L"Nach Interpret sortieren.", L"Ordenar por artista.", L"Sorteren op artiest.", L"Sortuj wg artysty.", L"Sanatciya gore sirala."));
	if (m_sortAlb.GetSafeHwnd())
		addTip(m_sortAlb, LL14(L"アルバムで並べ替え(再クリックで昇順/降順)。", L"Sort by album (click again to toggle asc/desc).", L"Trier par album.", L"Ordina per album.", L"Ordenar por album.", L"앨범으로 정렬.", L"按专辑排序。", L"ترتيب حسب الألبوم.", L"Сортировка по альбому.", L"Nach Album sortieren.", L"Ordenar por album.", L"Sorteren op album.", L"Sortuj wg albumu.", L"Albume gore sirala."));
	if (m_sortTime.GetSafeHwnd())
		addTip(m_sortTime, LL14(L"時間で並べ替え(再クリックで昇順/降順)。", L"Sort by duration (click again to toggle asc/desc).", L"Trier par duree.", L"Ordina per durata.", L"Ordenar por duracion.", L"시간으로 정렬.", L"按时长排序。", L"ترتيب حسب المدة.", L"Сортировка по длительности.", L"Nach Dauer sortieren.", L"Ordenar por duracao.", L"Sorteren op duur.", L"Sortuj wg czasu.", L"Sureye gore sirala."));
	addTip(m_findup, LL14(L"下方向(リスト後方)に検索します。", L"Search downward in the list.", L"Chercher vers le bas.", L"Cerca in basso.", L"Buscar abajo.", L"아래로 검색.", L"向下搜索。", L"بحث للأسفل.", L"Искать вниз.", L"Abwarts suchen.", L"Buscar abaixo.", L"Omlaag zoeken.", L"Szukaj w dol.", L"Asagi ara."));
	addTip(m_finddown, LL14(L"上方向(リスト前方)に検索します。", L"Search upward in the list.", L"Chercher vers le haut.", L"Cerca in alto.", L"Buscar arriba.", L"위로 검색.", L"向上搜索。", L"بحث للأعلى.", L"Искать вверх.", L"Aufwarts suchen.", L"Buscar acima.", L"Omhoog zoeken.", L"Szukaj w gore.", L"Yukari ara."));
	addTip(m_lsup, LL14(L"選択曲を一番上へ移動。", L"Move to top.", L"Tout en haut.", L"In cima.", L"Al principio.", L"맨 위로.", L"移到顶部。", L"إلى الأعلى.", L"В начало.", L"Ganz nach oben.", L"Para o topo.", L"Naar boven.", L"Na gore.", L"En uste."));
	addTip(m_up, LL14(L"選択曲を上へ移動。", L"Move up.", L"Monter.", L"Su.", L"Subir.", L"위로.", L"上移。", L"لأعلى.", L"Вверх.", L"Hoch.", L"Cima.", L"Omhoog.", L"W gore.", L"Yukarı."));
	addTip(m_down, LL14(L"選択曲を下へ移動。", L"Move down.", L"Descendre.", L"Giu.", L"Bajar.", L"아래로.", L"下移。", L"لأسفل.", L"Вниз.", L"Runter.", L"Baixo.", L"Omlaag.", L"W dół.", L"Aşağı."));
	addTip(m_lsdown, LL14(L"選択曲を一番下へ移動。", L"Move to bottom.", L"Tout en bas.", L"In fondo.", L"Al final.", L"맨 아래로.", L"移到底部。", L"إلى الأسفل.", L"В конец.", L"Ganz nach unten.", L"Para o final.", L"Naar beneden.", L"Na dol.", L"En alta."));
	addTip(m_tip, LL14(L"行ツールチップの表示を切り替えます。", L"Toggle row tooltips.", L"Info-bulles des lignes.", L"Suggerimenti righe.", L"Sugerencias de filas.", L"행 툴팁 표시 전환.", L"切换行工具提示。", L"تبديل تلميحات الصفوف.", L"Подсказки строк.", L"Zeilen-Tooltips.", L"Dicas de linha.", L"Rij-tooltips.", L"Etykiety wierszy.", L"Satır ipuçları."));
	addTip(m_mini, LL14(L"最小化/復帰をメイン画面と連動させます。", L"Sync minimize/restore with main window.", L"Synchroniser min./rest.", L"Sincronizza min./rip.", L"Sincronizar min./rest.", L"최소화/복원 연동.", L"最小化/还原联动。", L"تزامن التصغير/الاستعادة.", L"Синхр. сверт./восст.", L"Min./Wiederh. synchron.", L"Sincronizar min./rest.", L"Min./herstel synch.", L"Synch. min./przywr.", L"Min./geri yükleme eşitle."));
	addTip(m_savemp3, LL14(L"内蔵音源の再生時に途中保存を有効にします。", L"Enable resume save for built-in audio formats.", L"Reprise pour les formats audio integres.", L"Ripresa per i formati audio interni.", L"Reanudar para formatos de audio internos.", L"내장 음원 재생 시 위치 저장.", L"内置音源续播保存。", L"حفظ موضع للصيغ الصوتية المدمجة.", L"Сохранение позиции для встроенных форматов.", L"Position fur eingebaute Audioformate speichern.", L"Retomar formatos de audio internos.", L"Hervatten voor ingebouwde audio.", L"Wznawianie wbudowanych formatow.", L"Dahili ses formatlari icin konum kaydi."));
	addTip(m_saveds, LL14(L"DirectShow(動画等)で途中保存を有効にします。", L"Enable resume save for DirectShow.", L"Reprise pour DirectShow.", L"Ripresa per DirectShow.", L"Reanudar para DirectShow.", L"DirectShow 위치 저장.", L"DirectShow续播保存。", L"حفظ موضع DirectShow.", L"Сохранение позиции DirectShow.", L"DirectShow-Position.", L"Retomar DirectShow.", L"DirectShow hervatten.", L"Wznawianie DirectShow.", L"DirectShow surdurme."));
	addTip(m_savewav, LL14(L"再生中の音声をWAVファイルへ保存します。", L"Save playback audio to a WAV file.", L"Enregistrer l'audio en WAV.", L"Salva l'audio in WAV.", L"Guardar audio en WAV.", L"재생 음을 WAV로 저장.", L"将播放音频保存为WAV。", L"حفظ الصوت كـ WAV.", L"Сохранить звук в WAV.", L"Audio als WAV speichern.", L"Salvar audio em WAV.", L"Audio opslaan als WAV.", L"Zapis audio jako WAV.", L"Sesi WAV olarak kaydet."));
	addTip(m_micmix, LL14(L"WAV保存ONのとき、マイクを再生PCMにミックスして書き出します。", L"With Save WAV on, mix microphone into the saved PCM.", L"Si Sauver WAV est ON, mixer le micro dans le PCM.", L"Con Salva WAV, mixa il microfono nel PCM.", L"Con Guardar WAV, mezcla el micro en el PCM.", L"WAV 저장 ON이면 마이크를 PCM에 믹스.", L"WAV保存开启时将麦克风混入PCM。", L"مع حفظ WAV يُمزج الميكروفون في PCM.", L"При WAV — микс микрофона в PCM.", L"Bei WAV-Speichern Mikrofon in PCM mischen.", L"Com Salvar WAV, misturar microfone no PCM.", L"Bij WAV-opslaan microfoon in PCM mixen.", L"Przy zapisie WAV zmiksuj mikrofon do PCM.", L"WAV kaydı açıkken mikrofonu PCM'e karıştır."));
	addTip(m_miclev, LL14(L"マイクのミックスレベル(0〜200%)。端末は設定画面で選択。", L"Mic mix level (0-200%). Device is selected in Settings.", L"Niveau mix micro (0-200%). Périphérique dans Paramètres.", L"Livello mix micro (0-200%). Dispositivo in Impostazioni.", L"Nivel mix micro (0-200%). Dispositivo en Ajustes.", L"마이크 믹스 레벨(0~200%). 장치는 설정에서 선택.", L"麦克风混音电平(0–200%)。设备在设置中选择。", L"مستوى مزج الميكروفون (0-200%). الجهاز من الإعدادات.", L"Уровень микса (0–200%). Устройство — в настройках.", L"Mikrofon-Mixpegel (0–200%). Gerät in den Einstellungen.", L"Nível de mix (0-200%). Dispositivo em Configurações.", L"Microfoon-mixniveau (0-200%). Apparaat in Instellingen.", L"Poziom miksu (0–200%). Urządzenie w Ustawieniach.", L"Mikrofon karışım seviyesi (0-200%). Aygıt Ayarlar'da."));
	addTip(m_micMeter, LL14(L"マイク入力レベル。WAV保存＋マイクミックスONのとき反応します。", L"Mic level. Moves when Save-to-WAV and Mic mix are ON.", L"Niveau micro. Réagit si Enregistrer WAV + Mix micro ON.", L"Livello micro. Reagisce con Salva WAV + Mix micro ON.", L"Nivel de micro. Reacciona con Guardar WAV + Mezcla micro ON.", L"마이크 레벨. WAV 저장+마이크 믹스 ON일 때 반응.", L"麦克风电平。保存WAV且麦克风混音开启时会动。", L"مستوى الميكروفون. يتحرك عند حفظ WAV ومزج الميكروفون.", L"Уровень микрофона. Реагирует при WAV+микс ON.", L"Mikrofonpegel. Reagiert bei WAV-Speichern + Mix ON.", L"Nível do microfone. Reage com Salvar WAV + Mix ON.", L"Microfoonniveau. Reageert bij WAV opslaan + Mix ON.", L"Poziom mikrofonu. Reaguje przy WAV+Mix ON.", L"Mikrofon seviyesi. WAV kaydet + Karışım açıkken tepki verir."));
	addTip(m_record, LL14(L"他デバイスの音を録音して WAV/mp3/FLAC を作ります。", L"Record other device audio to WAV/mp3/FLAC.", L"Enregistrer l'audio d'un autre périphérique en WAV/mp3/FLAC.", L"Registra audio da altro dispositivo in WAV/mp3/FLAC.", L"Grabar audio de otro dispositivo a WAV/mp3/FLAC.", L"다른 장치 음을 WAV/mp3/FLAC로 녹음.", L"录制其他设备音频为 WAV/mp3/FLAC。", L"تسجيل صوت جهاز آخر إلى WAV/mp3/FLAC.", L"Запись звука другого устройства в WAV/mp3/FLAC.", L"Audio eines anderen Geräts als WAV/mp3/FLAC aufnehmen.", L"Gravar áudio de outro dispositivo em WAV/mp3/FLAC.", L"Audio van ander apparaat opnemen als WAV/mp3/FLAC.", L"Nagraj dźwięk innego urządzenia do WAV/mp3/FLAC.", L"Başka aygıt sesini WAV/mp3/FLAC olarak kaydet."));
	addTip(m_capture, LL14(L"画面と音声をキャプチャします（プレビュー付き）。", L"Capture screen and audio (with preview).", L"Capturer l'écran et l'audio (avec aperçu).", L"Cattura schermo e audio (con anteprima).", L"Capturar pantalla y audio (con vista previa).", L"화면과 음성을 캡처합니다(미리보기 포함).", L"捕获画面与音频（含预览）。", L"التقاط الشاشة والصوت (مع معاينة).", L"Захват экрана и звука (с предпросмотром).", L"Bildschirm und Audio aufnehmen (mit Vorschau).", L"Capturar tela e áudio (com prévia).", L"Scherm en audio opnemen (met voorbeeld).", L"Przechwyć ekran i dźwięk (z podglądem).", L"Ekran ve sesi yakala (önizlemeli)."));
	if (m_botDj.GetSafeHwnd())
		addTip(m_botDj, LL14(L"DJパッドを開きます。", L"Open DJ Pad.", L"Ouvrir le pad DJ.", L"Apri pad DJ.", L"Abrir pad DJ.", L"DJ 패드 열기.", L"打开 DJ 垫。", L"فتح لوحة DJ.", L"Открыть DJ-панель.", L"DJ-Pad öffnen.", L"Abrir pad DJ.", L"DJ-pad openen.", L"Otworz pad DJ.", L"DJ panelini ac."));
	if (m_botTag.GetSafeHwnd())
		addTip(m_botTag, LL14(L"選択曲のタグを編集します (F2)。", L"Edit tags of the selection (F2).", L"Editer les tags (F2).", L"Modifica tag (F2).", L"Editar etiquetas (F2).", L"선택 곡 태그 편집 (F2).", L"编辑所选标签 (F2)。", L"تحرير وسوم التحديد (F2).", L"Править теги выбранного (F2).", L"Tags der Auswahl bearbeiten (F2).", L"Editar tags da selecao (F2).", L"Tags van selectie bewerken (F2).", L"Edytuj tagi zaznaczenia (F2).", L"Secimin etiketlerini duzenle (F2)."));
	if (m_botBpm.GetSafeHwnd())
		addTip(m_botBpm, LL14(L"BPM を計測／確定します。", L"Measure / confirm BPM.", L"Mesurer / confirmer le BPM.", L"Misura / conferma BPM.", L"Medir / confirmar BPM.", L"BPM 측정/확정.", L"测量/确认 BPM。", L"قياس/تأكيد BPM.", L"Измерить/подтвердить BPM.", L"BPM messen/bestätigen.", L"Medir/confirmar BPM.", L"BPM meten/bevestigen.", L"Zmierz/potwierdz BPM.", L"BPM olc/onayla."));
	if (m_botSleep.GetSafeHwnd())
		addTip(m_botSleep, LL14(L"スリープタイマーを設定します。", L"Set sleep timer.", L"Regler la minuterie de veille.", L"Imposta timer sleep.", L"Configurar temporizador de sueño.", L"슬립 타이머 설정.", L"设置睡眠定时器。", L"ضبط مؤقت النوم.", L"Настроить таймер сна.", L"Schlaf-Timer setzen.", L"Definir temporizador de sono.", L"Slaaptimer instellen.", L"Ustaw timer snu.", L"Uyku zamanlayicisini ayarla."));
	if (m_botMirror.GetSafeHwnd())
		addTip(m_botMirror, LL14(L"ミラー出力設定を開きます。", L"Open mirror output settings.", L"Ouvrir la sortie miroir.", L"Apri uscita mirror.", L"Abrir salida espejo.", L"미러 출력 설정.", L"打开镜像输出。", L"فتح خرج المرآة.", L"Открыть зеркальный выход.", L"Spiegelausgabe öffnen.", L"Abrir saida espelho.", L"Spiegelaudio openen.", L"Otworz wyjscie lustrzane.", L"Ayna cikis ayarlarini ac."));
	if (m_botSsViz.GetSafeHwnd())
		addTip(m_botSsViz, LL14(L"SS ビジュアライザを開きます。", L"Open SS visualizer.", L"Ouvrir le visualiseur SS.", L"Apri visualizzatore SS.", L"Abrir visualizador SS.", L"SS 비주얼 열기.", L"打开 SS 可视化。", L"فتح عارض SS.", L"Открыть SS-визуализатор.", L"SS-Visualizer öffnen.", L"Abrir visual SS.", L"SS-visualizer openen.", L"Otworz wizual SS.", L"SS gorseli ac."));
	if (m_botAlarm.GetSafeHwnd())
		addTip(m_botAlarm, LL14(L"アラームのON/OFFを切り替えます。", L"Toggle alarm on/off.", L"Activer/desactiver l'alarme.", L"Attiva/disattiva sveglia.", L"Activar/desactivar alarma.", L"알람 ON/OFF.", L"开关闹钟。", L"تشغيل/إيقاف المنبه.", L"Вкл/выкл будильник.", L"Wecker ein/aus.", L"Ligar/desligar alarme.", L"Wekker aan/uit.", L"Wlacz/wylacz budzik.", L"Alarm ac/kapa."));
	if (m_botRemote.GetSafeHwnd())
		addTip(m_botRemote, LL14(L"ローカルリモート (HTTP) を切り替えます。", L"Toggle local remote (HTTP).", L"Basculer la telecommande locale (HTTP).", L"Attiva/disattiva remote locale (HTTP).", L"Alternar remoto local (HTTP).", L"로컬 리모트(HTTP) 전환.", L"切换本地遥控 (HTTP)。", L"تبديل التحكم المحلي (HTTP).", L"Переключить локальный пульт (HTTP).", L"Lokalfernbedienung (HTTP) umschalten.", L"Alternar remoto local (HTTP).", L"Lokale bediening (HTTP) wisselen.", L"Przelacz pilot lokalny (HTTP).", L"Yerel uzaktan (HTTP) ac/kapa."));
	if (m_botMaze.GetSafeHwnd())
		addTip(m_botMaze, LL14(L"Soft3D 迷路を開きます。", L"Open Soft3D maze.", L"Ouvrir le labyrinthe Soft3D.", L"Apri il labirinto Soft3D.", L"Abrir el laberinto Soft3D.", L"Soft3D 미로 열기.", L"打开 Soft3D 迷宫。", L"فتح متاهة Soft3D.", L"Открыть лабиринт Soft3D.", L"Soft3D-Labyrinth öffnen.", L"Abrir o labirinto Soft3D.", L"Soft3D-doolhof openen.", L"Otwórz labirynt Soft3D.", L"Soft3D labirenti aç."));
	addTip(m_saveparam, LL14(L"曲ごとに音量・EQ・テンポ等の全パラメータを記憶し、その曲を再生する度に自動で復元します。", L"Remember all parameters (volume, EQ, tempo, etc.) per song and auto-restore them each time the song plays.", L"Memoriser tous les parametres par morceau et les restaurer automatiquement.", L"Memorizza tutti i parametri per brano e li ripristina automaticamente.", L"Recuerda todos los parametros por pista y los restaura automaticamente.", L"곡별로 볼륨·EQ·템포 등 모든 파라미터를 기억하고 재생할 때마다 자동 복원합니다.", L"逐曲记忆音量、EQ、速度等所有参数，每次播放该曲时自动恢复。", L"تذكر كل المعلمات لكل أغنية واستعادتها تلقائيًا.", L"Запоминать все параметры для каждого трека и восстанавливать автоматически.", L"Alle Parameter pro Titel merken und automatisch wiederherstellen.", L"Memoriza todos os parametros por faixa e restaura automaticamente.", L"Onthoud alle parameters per nummer en herstel automatisch.", L"Zapamietaj wszystkie parametry na utwor i przywracaj automatycznie.", L"Her parça için tüm parametreleri hatırla ve otomatik geri yükle."));
	addTip(m_resetdata, LL14(L"曲ごとに保存した設定を全削除し、音量50%・拡張100%・EQ等を初期状態へ戻します。", L"Delete all per-song saved settings and reset volume to 50%, ext to 100%, EQ etc. to defaults.", L"Supprimer tous les reglages par morceau et reinitialiser les parametres.", L"Elimina tutte le impostazioni per brano e ripristina i parametri.", L"Elimina todos los ajustes por pista y restablece los parametros.", L"곡별 저장 설정을 모두 삭제하고 볼륨 50%·확장 100%·EQ 등을 초기화합니다.", L"删除所有逐曲保存的设置，并将音量重置为50%、扩展100%、EQ等为默认。", L"حذف كل الإعدادات المحفوظة لكل أغنية وإعادة الضبط.", L"Удалить все сохранённые настройки треков и сбросить параметры.", L"Alle pro-Titel-Einstellungen loeschen und Parameter zuruecksetzen.", L"Excluir todas as configuracoes por faixa e redefinir os parametros.", L"Verwijder alle per-nummer-instellingen en reset de parameters.", L"Usun wszystkie ustawienia na utwor i zresetuj parametry.", L"Tum parca ayarlarini sil ve parametreleri sifirla."));
	addTip(m_kaisuu, LL14(L"連続再生時、指定回数ループしたら次の曲へ進みます。", L"During continuous play, advance after this many loops.", L"En lecture continue, passer apres ce nombre de boucles.", L"In riproduzione continua, avanza dopo questo numero di loop.", L"En reproduccion continua, avanzar tras este numero de bucles.", L"연속 재생 시 지정 횟수만큼 반복 후 다음 곡.", L"连续播放时，循环指定次数后进入下一首。", L"في التشغيل المستمر، الانتقال بعد هذا العدد من الحلقات.", L"При непрерывном воспроизведении перейти после стольких повторов.", L"Bei Dauerwiedergabe nach so vielen Schleifen weiter.", L"Na reproducao continua, avancar apos este numero de loops.", L"Bij doorlopend afspelen na dit aantal loops verder.", L"Przy ciaglym odtwarzaniu przejdz po tylu petlach.", L"Surekli calmada bu dongu sayisindan sonra ilerle."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 10000);
	m_find.SetFont(&m_fontList, TRUE);

	DoLayout();
	CCC_GroupBoxesBack(GetSafeHwnd());   // 区分け枠を最背面へ(兄弟コントロールを覆わない)
	ReloadPlaylistCombo();
	// 起動時 Temp ON: 通常 Load 済みの pl を一時モードへ(空・非保存)
	if (savedata.mpTempOpen && pl && ::IsWindow(pl->GetSafeHwnd()) && !pl->m_tempMode) {
		pl->Save();
		if (pl->pc) { free(pl->pc); pl->pc = NULL; }
		pl->playcnt = 0;
		pl->pnt = -1;
		pl->pnt1 = -1;
		plcnt = -1;
		pl->pc = (playlistdata0*)malloc(sizeof(playlistdata0));
		if (pl->m_lc.GetSafeHwnd())
			pl->m_lc.SetItemCount(0);
		pl->m_tempMode = 1;
		m_queueN = 0;
	}
	if (savedata.mpTempOpen) {
		if (m_grpPl.GetSafeHwnd())
			m_grpPl.SetWindowText(LL14(L"一時プレイリスト", L"Temporary playlist", L"Liste temporaire", L"Playlist temporanea", L"Lista temporal", L"임시 재생목록", L"临时播放列表", L"قائمة مؤقتة", L"Временный плейлист", L"Temporaere Playlist", L"Lista temporaria", L"Tijdelijke playlist", L"Lista tymczasowa", L"Gecici liste"));
		if (m_plsel.GetSafeHwnd()) m_plsel.EnableWindow(FALSE);
		if (m_plrename.GetSafeHwnd()) m_plrename.EnableWindow(FALSE);
		if (m_pldelete.GetSafeHwnd()) m_pldelete.EnableWindow(FALSE);
	}
	RefreshList(TRUE);
	// SyncFromMain / Timer の GetCheck 前に立てる。これより前の WM_SIZE は抑止のまま。
	m_uiReady = true;
	SyncFromMain();
	ApplyListTooltipState(); // 行詳細 ON/OFF とリスト・バルーン切替を初期確定
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1)
		RefreshAeroMode();   // レイアウト確定後にアクリル/不透明化を再適用
#endif

	// 描画タイマーは og のスレッド基準(16ms=60fps)に合わせて固定する。
	// 実際のスペアナ等の更新頻度は og 側が savedata.ms2 で律速しており(ms2カウンタ)、
	// mp はそれを 60fps で Blit して pending を解除するだけ。ここで savedata.ms2 を
	// そのまま間隔に使うと描画全体がその間隔まで律速され遅くなる(=不具合の原因)。
	m_lastMs2 = 16;
	SetTimer(1, 250, NULL);          // 低速: テキスト/リスト/コンボ/チェックの同期
	SetTimer(2, 100, NULL);          // 安全網: 取りこぼし時のみバナー再描画(通常はtimerpが駆動)
	SetTimer(3, 33, NULL);           // 高速: シーク(playb追従)/時間/音量のミラー
	SetTimer(8, 60, NULL);           // ミニジャケ: 未解決時60ms、温いと500msに間引き
	if (savedata.mpSleepMin > 0) {
		SetTimer(9, (UINT)savedata.mpSleepMin * 60 * 1000, NULL);
		m_sleepEndTick = GetTickCount64() + (ULONGLONG)savedata.mpSleepMin * 60ULL * 1000ULL;
	}
	s_mpJakTimerMs = 60;
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1)
		SetTimer(4, 250, NULL);  // 遅延でアクリル再適用(ウィンドウ合成確定後)。一回で止める。
#endif
	// 起動直後はリスト項目を不可視時に設定したためスクロールバーが未実現。
	// 表示確定後にリストの非クライアント(枠/スクロールバー)を再描画して確実に表示
	// (アクリル時は OpaqueFixer の WM_NCPAINT で不透明化される)。一回限り。
	SetTimer(6, 120, NULL);
	MpRemoteEnsureRunning(m_hWnd);
	MpAlarmEnsureTimer(this);
	if (savedata.mpMirrorOut)
		MpMirrorOnFormatReady();
	if (savedata.deskLrcOn)
		OpenDesktopLyricsModeless(this);
	UpdateDeskLrcBtnChrome();
	return TRUE;
	}
	catch (CException* e)
	{
		// 診断: 実行フォルダに残す(次回から原因切り分け用)
		{
			TCHAR msg[512] = {};
			e->GetErrorMessage(msg, _countof(msg) - 1);
			CStdioFile f;
			if (f.Open(_T("mp_init_exception.log"),
				CFile::modeCreate | CFile::modeWrite | CFile::typeText))
			{
				CString line;
				line.Format(_T("CMediaPlayerDlg::OnInitDialog caught %hs: %s\n"),
					e->GetRuntimeClass()->m_lpszClassName, msg);
				f.WriteString(line);
				f.Close();
			}
		}
		e->Delete();
		return TRUE;
	}
}

// og 所有のまま(EQ/簡易ピアノロールと同じアクリルグループ)にして非アクティブでも
// アクリルを維持しつつ、WS_EX_APPWINDOW でタスクバーに単独ボタンを出す。
BOOL CMediaPlayerDlg::PreCreateWindow(CREATESTRUCT& cs)
{
	if (!CCustomBlurDialogExBase::PreCreateWindow(cs))
		return FALSE;
	cs.dwExStyle |= WS_EX_APPWINDOW;
	return TRUE;
}

BOOL CMediaPlayerDlg::RelayPreTranslateMessage(MSG* pMsg)
{
	// 途中再生確認中は Space/Enter を再生に流さない（確認ダイアログが前面ならここへ来ない）
	if (OggIsResumePromptActive() && pMsg
		&& pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST)
		return TRUE;
	// 子ボタン上の右クリックは親 OnRButtonUp に届かない → ここでクイックメニュー。
	auto relayBtn = [&](CWnd& btn, void (CMediaPlayerDlg::*fn)(CPoint)) -> BOOL {
		if (!btn.GetSafeHwnd()) return FALSE;
		if (pMsg->message != WM_RBUTTONUP && pMsg->message != WM_CONTEXTMENU) return FALSE;
		if (pMsg->hwnd != btn.GetSafeHwnd()) return FALSE;
		CPoint sp;
		if (pMsg->message == WM_CONTEXTMENU) {
			sp.x = (short)LOWORD(pMsg->lParam);
			sp.y = (short)HIWORD(pMsg->lParam);
			if (sp.x == -1 && sp.y == -1)
				::GetCursorPos(&sp);
		} else {
			sp.x = (short)LOWORD(pMsg->lParam);
			sp.y = (short)HIWORD(pMsg->lParam);
			btn.ClientToScreen(&sp);
		}
		(this->*fn)(sp);
		return TRUE;
	};
	if (relayBtn(m_toolsToggle, &CMediaPlayerDlg::ShowToolsExtrasMenu)) return TRUE;
	if (relayBtn(m_lrcExpand, &CMediaPlayerDlg::ShowLyricsExtrasMenu)) return TRUE;
	if (relayBtn(m_deskLrc, &CMediaPlayerDlg::ShowLyricsExtrasMenu)) return TRUE;
	if (relayBtn(m_settings, &CMediaPlayerDlg::ShowSettingsExtrasMenu)) return TRUE;
	if (relayBtn(m_folder, &CMediaPlayerDlg::ShowFolderExtrasMenu)) return TRUE;
	if (relayBtn(m_eq, &CMediaPlayerDlg::ShowEqButtonExtrasMenu)) return TRUE;
	if (relayBtn(m_fadeout, &CMediaPlayerDlg::ShowFadeExtrasMenu)) return TRUE;
	if (relayBtn(m_renzoku, &CMediaPlayerDlg::ShowPlayModeExtrasMenu)) return TRUE;
	if (relayBtn(m_loop, &CMediaPlayerDlg::ShowPlayModeExtrasMenu)) return TRUE;
	if (relayBtn(m_random, &CMediaPlayerDlg::ShowPlayModeExtrasMenu)) return TRUE;
	if (m_botMirror.GetSafeHwnd() && relayBtn(m_botMirror, &CMediaPlayerDlg::ShowMirrorExtrasMenu)) return TRUE;
	if (m_botSleep.GetSafeHwnd()
		&& (pMsg->message == WM_RBUTTONUP || pMsg->message == WM_CONTEXTMENU)
		&& pMsg->hwnd == m_botSleep.GetSafeHwnd()) {
		// LMB と同じスリープメニュー
		OnBotSleep();
		return TRUE;
	}
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_RETURN) {
		CWnd* pFocus = GetFocus();
		if (pFocus && pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()) {
			OnFindUp();  // Enter = 次の候補へ(og の IDOK/終了へ流さない)
			return TRUE;
		}
	}
	// ? / で操作ガイド(入力欄フォーカス時は文字入力を優先)
	if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == '?' || pMsg->wParam == VK_OEM_2)) {
		CWnd* pFocus = GetFocus();
		const BOOL inEdit = (pFocus && (pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()
			|| pFocus->IsKindOf(RUNTIME_CLASS(CEdit))));
		if (!inEdit) {
			const BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
			if (pMsg->wParam == '?' || (pMsg->wParam == VK_OEM_2 && shift)) {
				ShowCheatSheet();
				return TRUE;
			}
		}
	}
	// Ctrl+K = コマンドパレット（入力欄フォーカス時は文字入力を優先）
	if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == 'K' || pMsg->wParam == 'k')
		&& (GetKeyState(VK_CONTROL) & 0x8000) != 0
		&& (GetKeyState(VK_MENU) & 0x8000) == 0) {
		CWnd* pFocus = GetFocus();
		const BOOL inEdit = (pFocus && (pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()
			|| pFocus->IsKindOf(RUNTIME_CLASS(CEdit))));
		if (!inEdit) {
			OpenCommandPalette();
			return TRUE;
		}
	}
	// Soft3D 中 0 = 視点リセット
	if (pMsg->message == WM_KEYDOWN && IsBannerSoft3D()
		&& (pMsg->wParam == '0' || pMsg->wParam == VK_NUMPAD0)) {
		CWnd* pFocus = GetFocus();
		const BOOL inEdit = (pFocus && (pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()
			|| pFocus->IsKindOf(RUNTIME_CLASS(CEdit))));
		if (!inEdit) {
			savedata.mpBanner3dyaw = -220;
			savedata.mpBanner3dpitch = 260;
			savedata.mpBanner3dzoom = 100;
			SyncBannerSoft3DCamFromSave();
			Invalidate(FALSE);
			return TRUE;
		}
	}
	// Space = 再生/一時停止。og(非表示)の IsDialogMessage がフォーカスボタン(Ys6等)を押すのを防ぐ
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_SPACE) {
		CWnd* pFocus = GetFocus();
		const BOOL inEdit = (pFocus && (pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()
			|| pFocus->IsKindOf(RUNTIME_CLASS(CEdit))));
		if (!inEdit) {
			if (plf)
				OnPauseBtn();
			else
				OnPlay();
			return TRUE;
		}
	}
	// R = 現在±フレーズ秒を A-B に（練習用ワンキー）
	if (pMsg->message == WM_KEYDOWN && (pMsg->wParam == 'R' || pMsg->wParam == 'r')) {
		CWnd* pFocus = GetFocus();
		const BOOL inEdit = (pFocus && (pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()
			|| pFocus->IsKindOf(RUNTIME_CLASS(CEdit))));
		if (!inEdit) {
			SetPhraseAbAroundNow();
			return TRUE;
		}
	}
	// 1-8 = キュージャンプ（編集中以外）
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam >= '1' && pMsg->wParam <= '8') {
		CWnd* pFocus = GetFocus();
		const BOOL inEdit = (pFocus && (pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()
			|| pFocus->IsKindOf(RUNTIME_CLASS(CEdit))));
		if (!inEdit) {
			JumpToCueIndex((int)(pMsg->wParam - '1'));
			return TRUE;
		}
	}
	// F2 = タグ編集
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_F2) {
		CWnd* pFocus = GetFocus();
		const BOOL inEdit = (pFocus && (pFocus->GetSafeHwnd() == m_find.GetSafeHwnd()
			|| pFocus->IsKindOf(RUNTIME_CLASS(CEdit))));
		if (!inEdit) {
			OpenTagEditForSelection();
			return TRUE;
		}
	}
	/* メディアプレイヤー前面時は og の RegisterHotKey が Unregister されている。
	   ←→ を og のホットキーと同じ経路へ渡す（昇格後シークが死ぬのを防ぐ） */
	if (pMsg->message == WM_KEYDOWN
		&& (pMsg->wParam == VK_LEFT || pMsg->wParam == VK_RIGHT)
		&& og && ::IsWindow(og->GetSafeHwnd())) {
		const WPARAM hotId = (pMsg->wParam == VK_RIGHT) ? (WPARAM)8002 : (WPARAM)8003;
		og->SendMessage(WM_HOTKEY, hotId, 0);
		return TRUE;
	}
	return FALSE;
}

BOOL CMediaPlayerDlg::PreTranslateMessage(MSG* pMsg)
{
	if (RelayPreTranslateMessage(pMsg))
		return TRUE;
	if (CCC_InwomanHotkey(pMsg, this))
		return TRUE; // 隠し: F12を5回で淫女モード切替
	// リスト行ツールチップ (CListCtrlA 実装): ツールチップ表示ON時のみリレー
	if (m_list.GetSafeHwnd() && m_tip.GetCheck())
		if (m_list.PreTranslateMessage(pMsg))
			return TRUE;
	// シークホバー時刻ツールチップ
	if (m_seek.GetSafeHwnd())
		m_seek.PreTranslateMessage(pMsg);
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CMediaPlayerDlg::RequestAppShutdown()
{
	DesktopLyricsPrepareAppExit();
	MpDjPadPrepareAppExit();
	SavePos();
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
}

BOOL CMediaPlayerDlg::DestroyWindow()
{
	StopMissScan();
	InterlockedIncrement(&m_waveGen);
	SavePos();
	// バナー内蔵ジャケ(ファルコム特化型のミニジャケ)抑止フラグを必ず解除する。
	// これを残すと、ファルコム特化型へ戻した後もミニジャケが表示されなくなる。
	g_mpSideJacket = 0;
	KillTimer(1);
	KillTimer(2);
	KillTimer(3);
	KillTimer(4);
	KillTimer(6);
	KillTimer(7);
	KillTimer(8);
	KillTimer(9);
	if (::IsWindow(m_list.GetSafeHwnd()))
		RemoveWindowSubclass(m_list.GetSafeHwnd(), ListHeaderNotifySubclassProc, kMpListHdrSubclassId);
	if (m_bmpBanner.GetSafeHandle()) m_bmpBanner.DeleteObject();
	if (m_memBanner.GetSafeHdc()) m_memBanner.DeleteDC();
	for (int i = 0; i < kInfoRows; i++) {
		if (m_iscRowDC[i].GetSafeHdc()) {
			if (m_iscRowOldBmp[i]) m_iscRowDC[i].SelectObject(m_iscRowOldBmp[i]);
			m_iscRowDC[i].DeleteDC();
		}
		m_iscRowBmp[i].DeleteObject();
		m_iscRowOldBmp[i] = nullptr;
		m_iscRowCacheW[i] = m_iscRowCacheH[i] = 0;
		m_iscRowCacheText[i].Empty();
	}
	InterlockedExchange(&m_iscScrollPosted, 0);
	if (m_infoMemDC.GetSafeHdc()) {
		if (m_infoMemOldBmp) m_infoMemDC.SelectObject(m_infoMemOldBmp);
		m_infoMemDC.DeleteDC();
	}
	m_infoMemBmp.DeleteObject();
	m_infoMemOldBmp = nullptr;
	m_infoMemW = m_infoMemH = 0;
	return CCustomBlurDialogExBase::DestroyWindow();
}

// 1コントロールを移動するヘルパ。
// w/h が 0 以下だと MoveWindow/SetWindowPos が ERROR_INVALID_PARAMETER
// （「引数が正しくありません」）を立てるため、その場合は移動しない。
static void MoveCtl(CWnd* p, int x, int y, int w, int h)
{
	if (!p || !p->GetSafeHwnd()) return;
	if (w <= 0 || h <= 0) return;
	p->MoveWindow(x, y, w, h);
}

// m_plsel: CBS_DROPDOWNLIST の MoveWindow 高さはドロップダウン領域。毎回 tbH を渡すと潰れる。
// 初回だけ RC 相当の dropExtent を設定し、以降のリサイズは位置・幅のみ変更する。
#ifndef CB_SETMINVISIBLE
#define CB_SETMINVISIBLE 0x1702
#endif

static int MpPlselClosedH(float s)
{
	return max(1, (int)(19 * s + 0.5f));
}

static int MpPlselListRowH(float s)
{
	return max(1, (int)(28 * s + 0.5f));
}

static int MpPlselQueryRowH(HWND hCombo)
{
	int h = (int)(INT_PTR)::SendMessage(hCombo, CB_GETITEMHEIGHT, 0, 0);
	if (h <= 1)
		h = (int)(INT_PTR)::SendMessage(hCombo, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
	if (h <= 1) {
		UINT dpi = 96;
		if (hCombo) {
			if (HDC hdc = ::GetDC(hCombo)) {
				dpi = (UINT)::GetDeviceCaps(hdc, LOGPIXELSX);
				::ReleaseDC(hCombo, hdc);
			}
		}
		if (dpi == 0) dpi = 96;
		h = max(1, MulDiv(28, (int)dpi, 96));
	}
	return h;
}

// listRowH = ドロップダウン行の高さ(MeasureItem と同じ 28px@96dpi)
// closedH  = 選択欄の高さ(MoveCtl の tbH と同じ)。CBS_OWNERDRAWFIXED では
//            wParam=-1 が選択欄、-1 以外の 0 がリスト行。index 1 は無効。
static void FixPlselDropList(CCustomComboBox& cb, int listRowH, int closedH)
{
	if (!cb.GetSafeHwnd() || listRowH <= 0 || closedH <= 0) return;
	const HWND h = cb.GetSafeHwnd();
	const auto setH = [&](WPARAM idx, int ht) -> LRESULT {
		return ::SendMessage(h, CB_SETITEMHEIGHT, idx, (LPARAM)ht);
	};
	if (cb.GetStyle() & CBS_OWNERDRAWVARIABLE)
	{
		const int n = (int)::SendMessage(h, CB_GETCOUNT, 0, 0);
		for (int i = 0; i < n; ++i)
			setH((WPARAM)i, listRowH);
	}
	else
	{
		setH(0, listRowH);
	}
	// 選択欄は必ず -1。ここを listRowH のままにすると tbH より縦に伸びて下段ボタンに被る。
	setH((WPARAM)-1, closedH);
	const int cnt = (int)::SendMessage(h, CB_GETCOUNT, 0, 0);
	if (cnt > 0)
		::SendMessage(h, CB_SETMINVISIBLE, (WPARAM)min(cnt, 12), 0);
}

static void ExpandPlselDropListPopup(HWND hCombo)
{
	if (!hCombo) return;
	COMBOBOXINFO ci = { sizeof(ci) };
	if (!::GetComboBoxInfo(hCombo, &ci) || !ci.hwndList)
		return;
	const int cnt = (int)::SendMessage(hCombo, CB_GETCOUNT, 0, 0);
	if (cnt <= 0) return;
	const int vis = min(cnt, 12);
	// 行高はリストボックス自身から取得(コンボの CB_GETITEMHEIGHT とずれる環境がある)
	int rowH = (int)(INT_PTR)::SendMessage(ci.hwndList, LB_GETITEMHEIGHT, 0, 0);
	if (rowH <= 1)
		rowH = MpPlselQueryRowH(hCombo);
	// リストボックスの実際の枠(非クライアント)ぶんを実測して足す。SM_CYEDGE 固定だと
	// テーマ/DPI により誤差が出て下に空白が残る。
	CRect wr, cr;
	::GetWindowRect(ci.hwndList, &wr);
	::GetClientRect(ci.hwndList, &cr);
	const int ncH = max(0, wr.Height() - cr.Height());
	const int needH = rowH * vis + ncH;
	::SetWindowPos(ci.hwndList, NULL, 0, 0, wr.Width(), needH,
		SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CMediaPlayerDlg::LayoutPlselCombo(int x, int y, int w, int tbH, float s)
{
	if (!::IsWindow(m_plsel.GetSafeHwnd())) return;
	if (m_plselDropExtent > 0 && fabs(m_plselLayoutDpi - s) > 0.01f)
		m_plselDropExtent = 0;

	const int closedH = MpPlselClosedH(s);
	const int listRowH = MpPlselListRowH(s);
	const int dropExt = max((int)(182 * s + 0.5f), listRowH * 12);

	if (m_plselDropExtent <= 0)
	{
		m_plsel.MoveWindow(x, y, w, dropExt);
		m_plselDropExtent = dropExt;
		m_plselLayoutDpi = s;
		FixPlselDropList(m_plsel, listRowH, closedH);
	}
	else
	{
		m_plsel.SetWindowPos(NULL, x, y, w, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
		FixPlselDropList(m_plsel, listRowH, closedH);
	}

	CRect cr;
	m_plsel.GetWindowRect(&cr);
	ScreenToClient(&cr);
	const int dy = y + max(0, (tbH - cr.Height()) / 2);
	if (cr.left != x || cr.top != dy)
		m_plsel.SetWindowPos(NULL, x, dy, w, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

// DPI/リサイズ対応の手動レイアウト。RC で固定配置すると高 DPI で壊れるため
// OnInitDialog 後・OnSize ごとに呼ぶ。コントロール座標は hD2 スケールで計算する。
// バナー領域の計算はアスペクト維持(MP_SRCW:MP_SRCH)で行い、余白は DoLayout
// 内で m_jacketRect / m_infoPanelRect に割り当てる。
void CMediaPlayerDlg::DoLayout()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	CRect rc; GetClientRect(&rc);
	const int W = rc.Width(), H = rc.Height();
	if (W < 32 || H < 32) return;   // 初期化途中の極小クライアントでは触らない
	const float s = hD2;
	const int M = (int)(10 * s);             // マージン
	const int topM = M + CCC_GetCustomCaptionHeight(m_hWnd);

	// 上部: ビジュアライザ(スペアナ+ジャケ+時間)の帯。
	// アスペクト比(MP_SRCW:MP_SRCH)を保ったまま、高さを上限に抑えて幅を決める。
	// 横に伸ばしすぎると文字や隠れジャケットが見にくいため、最大幅を超えたら
	// それ以上ストレッチせず中央寄せにし、左右の余白は背景色/アクリルにする(OnPaintが処理)。
	int avail = W - M * 2;
	int bannerH = (int)(96 * s);                                  // 既定の帯高さ(上限)
	int bannerW = (int)((double)bannerH * (double)MP_SRCW / (double)MP_SRCH);  // 高さからアスペクト幅
	if (bannerW > avail) {                                        // 幅が足りない狭い窓では幅に合わせて縮小
		bannerW = avail;
		bannerH = (int)((double)bannerW * (double)MP_SRCH / (double)MP_SRCW);
		int bannerMin = (int)(54 * s);
		if (bannerH < bannerMin) bannerH = bannerMin;
	}

	// ===== 余白(左右)の有効活用 =====
	// バナーはアスペクト維持のため幅に上限があり、窓を広げると左右に余白ができる。
	// その余白へ: 左=ジャケット(ミニ・正方形), 右=曲情報パネル を順に展開する。
	// 余白が少ない狭い窓では従来どおりバナーを中央寄せするだけ(サイドパネル無し)。
	m_jacketRect.SetRectEmpty();
	m_infoPanelRect.SetRectEmpty();
	const int sideGap = (int)(10 * s);
	int freeSpace = avail - bannerW;                              // 左右に使える余白の合計
	int jacketSide = bannerH;                                     // ジャケットは帯と同じ高さの正方形
	bool showJacket = (freeSpace >= jacketSide + sideGap * 2);
	int leftZone = showJacket ? (jacketSide + sideGap) : 0;       // [ジャケ][gap] の占有幅
	int minInfo = (int)(130 * s);                                 // 情報パネルを出す最小幅
	int remainFree = freeSpace - leftZone;                        // ジャケ配置後に残る余白
	bool showInfo = showJacket && (remainFree >= minInfo + sideGap);

	int bannerX;
	if (!showJacket) {
		bannerX = M + freeSpace / 2;                              // 従来: 中央寄せのみ
	}
	else if (!showInfo) {
		// ジャケ+バナーのみ: 左右に余白を均等配分して塊を中央寄せ(空白を最小化)
		int pad = remainFree / 2;
		int jacketX = M + pad;
		m_jacketRect.SetRect(jacketX, topM, jacketX + jacketSide, topM + bannerH);
		bannerX = jacketX + leftZone;
	}
	else {
		// ジャケ(左端)+ バナー + 情報パネル(右端まで)で余白を埋め切る
		int jacketX = M;
		m_jacketRect.SetRect(jacketX, topM, jacketX + jacketSide, topM + bannerH);
		bannerX = jacketX + leftZone;
		int infoX = bannerX + bannerW + sideGap;
		m_infoPanelRect.SetRect(infoX, topM, M + avail, topM + bannerH);
	}
	m_bannerRect.SetRect(bannerX, topM, bannerX + bannerW, topM + bannerH);

	// og の timerp 側: ジャケットを左へ分離している間はバナー内蔵ジャケ描画を抑止
	// （ホバー前面化アルファも不要なので、分離中はバナーホバーも落とす）
	g_mpSideJacket = showJacket ? 1 : 0;
	if (g_mpSideJacket)
		g_mpBannerHover = 0;

	const int gTitle = (int)(14 * s);   // グループ枠のタイトル分の高さ
	const int gPad = (int)(5 * s);      // グループ内側の余白

	// ===== 情報グループ(歌詞5行 or 歌詞3行+OS/CPU) + 拡大時はカラオケ風ビュー =====
	// 歌詞有無で中身は切替えるが、枠の高さは固定(5行分)＋拡大分にしてプレイリスト位置が暴れないようにする。
	int infoTop = topM + bannerH + (int)(2 * s);
	int ix = M + gPad, iw = W - M * 2 - gPad * 2;
	if (iw < 1) iw = 1;
	int y = infoTop + gTitle;
	int lh = (int)(17 * s);   // 情報フォント13px が収まる行高
	const int osH = (int)(15 * s);
	const int lrcExtra = (savedata.mpLrcExpand ? 5 : 0); // 拡大時 +5行分の高さ
	const int infoInnerH = lh * (5 + lrcExtra) + (int)(1 * s);
	const bool hasLyrics = (og && og->lrcnum >= 2);
	// 拡大時は歌詞なしでも LRC ビュー(空GDI)を出す
	const bool lrcScroll = (savedata.mpLrcExpand && m_lrcView.GetSafeHwnd());
	// バッジ + 歌詞窓 + 拡大ボタンをグループ右上へ
	{
		const int badgeW = (int)(78 * s);
		const int deskW = (int)(52 * s); // 「歌詞窓」/「窓●」が切れない幅
		const int expW = (int)(22 * s);
		const int gap = (int)(2 * s);
		const int right = M + W - M * 2 - gap;
		const int topY = infoTop + (int)(1 * s);
		MoveCtl(&m_lrcExpand, right - expW, topY, expW, (int)(14 * s));
		MoveCtl(&m_deskLrc, right - expW - gap - deskW, topY, deskW, (int)(14 * s));
		MoveCtl(&m_lrcBadge, right - expW - gap - deskW - gPad - badgeW, topY, badgeW, (int)(12 * s));
	}
	if (lrcScroll) {
		const int viewTop = y + (int)(2 * s);
		const int viewH = infoInnerH - (int)(4 * s);
		MoveCtl(&m_lrcView, ix, viewTop, iw, viewH > 1 ? viewH : 1);
		m_lrcView.ShowWindow(SW_SHOW);
		m_lrcView.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		m_lrcView.EnsureFonts((int)(90 * s), _T("Segoe UI"));
		MoveCtl(&m_lrc, ix, infoTop, 0, 0);
		MoveCtl(&m_lrc2, ix, infoTop, 0, 0);
		MoveCtl(&m_lrc3, ix, infoTop, 0, 0);
		MoveCtl(&m_lrc4, ix, infoTop, 0, 0);
		MoveCtl(&m_lrc5, ix, infoTop, 0, 0);
		m_lrc.ShowWindow(SW_HIDE);
		m_lrc2.ShowWindow(SW_HIDE);
		m_lrc3.ShowWindow(SW_HIDE);
		m_lrc4.ShowWindow(SW_HIDE);
		m_lrc5.ShowWindow(SW_HIDE);
		MoveCtl(&m_os, ix, infoTop, 0, 0);
		MoveCtl(&m_cpu, ix, infoTop, 0, 0);
		MoveCtl(&m_os3, ix, infoTop, 0, 0);
		m_os.ShowWindow(SW_HIDE);
		m_cpu.ShowWindow(SW_HIDE);
		m_os3.ShowWindow(SW_HIDE);
		y = infoTop + gTitle + infoInnerH;
	}
	else if (hasLyrics) {
		if (m_lrcView.GetSafeHwnd()) {
			MoveCtl(&m_lrcView, ix, infoTop, 0, 0);
			m_lrcView.ShowWindow(SW_HIDE);
		}
		MoveCtl(&m_lrc, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc2, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc3, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc4, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc5, ix, y, iw, lh); y += lh;
		m_lrc.ShowWindow(SW_SHOW);
		m_lrc2.ShowWindow(SW_SHOW);
		m_lrc3.ShowWindow(SW_SHOW);
		m_lrc4.ShowWindow(SW_SHOW);
		m_lrc5.ShowWindow(SW_SHOW);
		MoveCtl(&m_os, ix, infoTop, 0, 0);
		MoveCtl(&m_cpu, ix, infoTop, 0, 0);
		MoveCtl(&m_os3, ix, infoTop, 0, 0);
		m_os.ShowWindow(SW_HIDE);
		m_cpu.ShowWindow(SW_HIDE);
		m_os3.ShowWindow(SW_HIDE);
	}
	else {
		if (m_lrcView.GetSafeHwnd()) {
			MoveCtl(&m_lrcView, ix, infoTop, 0, 0);
			m_lrcView.ShowWindow(SW_HIDE);
		}
		MoveCtl(&m_lrc, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc2, ix, y, iw, lh); y += lh;
		MoveCtl(&m_lrc3, ix, y, iw, lh); y += lh + (int)(1 * s);
		MoveCtl(&m_os, ix, y, iw * 3 / 5, osH);
		MoveCtl(&m_cpu, ix + iw * 3 / 5, y, iw * 2 / 5, osH); y += osH;
		MoveCtl(&m_os3, ix, y, iw, osH);
		MoveCtl(&m_lrc4, ix, infoTop, 0, 0);
		MoveCtl(&m_lrc5, ix, infoTop, 0, 0);
		m_lrc.ShowWindow(SW_SHOW);
		m_lrc2.ShowWindow(SW_SHOW);
		m_lrc3.ShowWindow(SW_SHOW);
		m_lrc4.ShowWindow(SW_HIDE);
		m_lrc5.ShowWindow(SW_HIDE);
		m_os.ShowWindow(SW_SHOW);
		m_cpu.ShowWindow(SW_SHOW);
		m_os3.ShowWindow(SW_SHOW);
	}
	const int infoBottom = infoTop + gTitle + infoInnerH + gPad;
	MoveCtl(&m_grpInfo, M, infoTop, W - M * 2, infoBottom - infoTop);

	// ===== シーク: ロック + 範囲スライダー + 時間% + A-B =====
	int seekY = infoBottom + (int)(5 * s);
	int timeW = (int)(42 * s);
	int lockW = (int)(58 * s);
	int seekH = savedata.mpSeekWave ? (int)(28 * s) : (int)(16 * s);
	if (seekH < 16) seekH = 16;
	int abW = (int)(28 * s), abClrW = (int)(56 * s), abGap = (int)(3 * s);
	int abBlock = abW + abGap + abW + abGap + abClrW + abGap;
	int seekW = W - M * 2 - lockW - (int)(4 * s) - timeW - (int)(4 * s) - abBlock;
	if (seekW < 1) seekW = 1;
	MoveCtl(&m_seekLock, M, seekY + (seekH - (int)(16 * s)) / 2, lockW, (int)(16 * s));
	MoveCtl(&m_seek, M + lockW + (int)(4 * s), seekY, seekW, seekH);
	MoveCtl(&m_time, M + lockW + (int)(4 * s) + seekW + (int)(2 * s), seekY + (seekH - (int)(14 * s)) / 2, timeW, (int)(14 * s));
	int abX = M + lockW + (int)(4 * s) + seekW + timeW + (int)(4 * s);
	const int abY = seekY + (seekH - (int)(16 * s)) / 2;
	MoveCtl(&m_abA, abX, abY, abW, (int)(16 * s)); abX += abW + abGap;
	MoveCtl(&m_abB, abX, abY, abW, (int)(16 * s)); abX += abW + abGap;
	MoveCtl(&m_abClr, abX, abY, abClrW, (int)(16 * s));

	// ===== 操作行: 前/再生/一時停止/停止/フェードアウト/次 + ジャケ/EQ/ロール/アナ + 主音量(右) =====
	int by = seekY + seekH + (int)(6 * s);
	int bh = (int)(24 * s), gap = (int)(3 * s);
	if (bh < 1) bh = 1;

	int volValW = (int)(44 * s), volLblW = (int)(38 * s);
	const int volSlW = max(1, (int)(100 * s));
	int volvalX = W - M - volValW;
	int volSlX = volvalX - (int)(4 * s) - volSlW;
	int volLblX = volSlX - volLblW;
	const int freeEnd = volLblX - gap;

	// 幅に応じて 0=フル / 1=EQ系短縮 / 2=フェード・JK等も短縮 / 3=最小幅用の超短縮
	// 各段階は「その段階の幅でもまだ主音量に食い込むか」で次へ進む(同条件で2と3が同時発火しないこと)。
	const int prevW = max(1, (int)(40 * s));
	const int playW = max(1, (int)(48 * s));
	const int stopW = max(1, (int)(44 * s));
	const int nextW = max(1, (int)(40 * s));
	const int pauseFull = max(1, (int)(68 * s)), pauseShort = max(1, (int)(40 * s)), pauseTiny = max(1, (int)(28 * s));
	const int fadeFull = max(1, (int)(92 * s)), fadeShort = max(1, (int)(52 * s)), fadeTiny = max(1, (int)(28 * s));
	const int jkFull = max(1, (int)(62 * s)), jkShort = max(1, (int)(32 * s)), jkTiny = max(1, (int)(28 * s));
	const int ebwFull = max(1, (int)(84 * s)), pbwFull = max(1, (int)(128 * s)), abwFull = max(1, (int)(88 * s)), prbwFull = max(1, (int)(56 * s));
	const int ebwShort = max(1, (int)(42 * s)), pbwShort = max(1, (int)(56 * s)), abwShort = max(1, (int)(48 * s)), prbwShort = max(1, (int)(40 * s));
	const int ebwTiny = max(1, (int)(30 * s)), pbwTiny = max(1, (int)(28 * s)), abwTiny = max(1, (int)(28 * s)), prbwTiny = max(1, (int)(28 * s));

	const int baseLeft = M + prevW + gap + playW + gap + stopW + gap + nextW + (int)(8 * s);
	// 各候補レイアウトの右端(Pro右端)。freeEnd を超えたら一段短い段階へ。
	const int endLv0 = baseLeft + pauseFull + gap + fadeFull + gap + jkFull + gap
		+ ebwFull + gap + pbwFull + gap + abwFull + gap + prbwFull;
	const int endLv1 = baseLeft + pauseFull + gap + fadeFull + gap + jkFull + gap
		+ ebwShort + gap + pbwShort + gap + abwShort + gap + prbwShort;
	const int endLv2 = baseLeft + pauseShort + gap + fadeShort + gap + jkShort + gap
		+ ebwShort + gap + pbwShort + gap + abwShort + gap + prbwShort;

	int shortLv = 0;
	if (endLv0 > freeEnd)
		shortLv = 1;
	if (endLv1 > freeEnd)
		shortLv = 2;
	if (endLv2 > freeEnd)
		shortLv = 3;

	const int pauseW = (shortLv >= 3) ? pauseTiny : (shortLv >= 2) ? pauseShort : pauseFull;
	const int fadeW = (shortLv >= 3) ? fadeTiny : (shortLv >= 2) ? fadeShort : fadeFull;
	const int jkw = (shortLv >= 3) ? jkTiny : (shortLv >= 2) ? jkShort : jkFull;
	const int ebw = (shortLv >= 3) ? ebwTiny : (shortLv >= 1) ? ebwShort : ebwFull;
	const int pbw = (shortLv >= 3) ? pbwTiny : (shortLv >= 1) ? pbwShort : pbwFull;
	const int abw = (shortLv >= 3) ? abwTiny : (shortLv >= 1) ? abwShort : abwFull;
	const int prbw = (shortLv >= 3) ? prbwTiny : (shortLv >= 1) ? prbwShort : prbwFull;

	if (shortLv != m_mpBtnShort) {
		m_mpBtnShort = shortLv;
		if (shortLv >= 3) {
			m_fadeout.SetWindowText(LL14(L"FO", L"FO", L"Fd", L"Fd", L"Fd", L"페", L"淡", L"تل", L"Зт", L"AO", L"Fd", L"Fo", L"Zan", L"So"));
			m_jacket.SetWindowText(LL14(L"JK", L"JK", L"Poc", L"Cop", L"Car", L"JK", L"封", L"غل", L"Обл", L"Cov", L"Cap", L"Oms", L"Okł", L"Kap"));
		}
		else if (shortLv >= 2) {
			m_fadeout.SetWindowText(LL14(L"フェード", L"Fade", L"Fondu", L"Fade", L"Fade", L"페이드", L"淡出", L"تلاشي", L"Затухание", L"Fade", L"Fade", L"Fade", L"Fade", L"Fade"));
			m_jacket.SetWindowText(LL14(L"JK", L"JK", L"Poc", L"Cop", L"Car", L"JK", L"封", L"غل", L"Обл", L"Cov", L"Cap", L"Oms", L"Okł", L"Kap"));
		}
		else {
			m_fadeout.SetWindowText(LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza", L"Desvanecer", L"페이드 아웃", L"淡出", L"تلاشي", L"Затухание", L"Ausblenden", L"Desvanecer", L"Uitfaden", L"Zanikanie", L"Soluklaştır"));
			m_jacket.SetWindowText(LL14(L"ジャケット", L"Jacket", L"Pochette", L"Copertina", L"Caratula", L"자켓", L"封面", L"الغلاف", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Okładka", L"Kapak"));
		}
		if (shortLv >= 3) {
			if (m_eq.GetSafeHwnd())
				m_eq.SetWindowText(LL14(L"EQ", L"EQ", L"Égal.", L"EQ", L"Ecual.", L"EQ", L"均衡", L"معادل", L"Экв.", L"EQ", L"Equal.", L"EQ", L"Kor.", L"Ekol."));
			if (m_piano.GetSafeHwnd())
				m_piano.SetWindowText(LL14(L"ロ", L"PR", L"PR", L"PR", L"PR", L"롤", L"卷", L"رول", L"Рл", L"PR", L"PR", L"PR", L"PR", L"PR"));
			if (m_analyzer.GetSafeHwnd())
				m_analyzer.SetWindowText(LL14(L"ア", L"A", L"A", L"A", L"A", L"아", L"析", L"أ", L"А", L"A", L"A", L"A", L"A", L"A"));
			if (m_pro.GetSafeHwnd())
				m_pro.SetWindowText(LL14(L"詳", L"E", L"E", L"E", L"E", L"상", L"详", L"ت", L"Д", L"E", L"E", L"E", L"E", L"E"));
		}
		else if (shortLv >= 1) {
			if (m_eq.GetSafeHwnd())
				m_eq.SetWindowText(LL14(L"EQ", L"EQ", L"Égal.", L"EQ", L"Ecual.", L"EQ", L"均衡", L"معادل", L"Экв.", L"EQ", L"Equal.", L"EQ", L"Kor.", L"Ekol."));
			if (m_piano.GetSafeHwnd())
				m_piano.SetWindowText(LL14(L"ロール", L"Roll", L"Rouleau", L"Roll", L"Rollo", L"롤", L"卷帘", L"رول", L"Ролл", L"Rolle", L"Rolo", L"Rol", L"Rolka", L"Rulo"));
			if (m_analyzer.GetSafeHwnd())
				m_analyzer.SetWindowText(LL14(L"アナ", L"Ana", L"Ana", L"Ana", L"Ana", L"아나", L"分析", L"محلل", L"Ана", L"Ana", L"Ana", L"Ana", L"Ana", L"Ana"));
			if (m_pro.GetSafeHwnd())
				m_pro.SetWindowText(LL14(L"詳細", L"Extra", L"Extra", L"Extra", L"Extra", L"상세", L"详情", L"تفاصيل", L"Доп.", L"Extra", L"Extra", L"Extra", L"Extra", L"Ek"));
		}
		else {
			if (m_eq.GetSafeHwnd())
				m_eq.SetWindowText(LL14(L"イコライザー", L"Equalizer", L"Egaliseur", L"Equalizzatore", L"Ecualizador", L"이퀄라이저", L"均衡器", L"المعادل", L"Эквалайзер", L"Equalizer", L"Equalizador", L"Equalizer", L"Korektor", L"Ekolayzer"));
			if (m_piano.GetSafeHwnd())
				m_piano.SetWindowText(LL14(L"簡易ピアノロール", L"Simple Piano Roll", L"Rouleau piano simple", L"Piano roll semplice", L"Rollo piano simple", L"간이 피아노 롤", L"简易钢琴卷帘", L"لوحة بيانو بسيطة", L"Простой пианоролл", L"Einfache Klavierrolle", L"Piano roll simples", L"Eenvoudige pianorol", L"Prosta rolka pianina", L"Basit piyano rulosu"));
			if (m_analyzer.GetSafeHwnd())
				m_analyzer.SetWindowText(LL14(L"アナライザー", L"Analyzer", L"Analyseur", L"Analizzatore", L"Analizador", L"분석기", L"分析器", L"المحلل", L"Анализатор", L"Analysator", L"Analisador", L"Analyser", L"Analizator", L"Analizor"));
			if (m_pro.GetSafeHwnd())
				m_pro.SetWindowText(LL14(L"詳細", L"Extra", L"Extra", L"Extra", L"Extra", L"상세", L"详情", L"تفاصيل", L"Доп.", L"Extra", L"Extra", L"Extra", L"Extra", L"Ek"));
		}
		ApplyPauseButtonLabel();
	}

	int bx = M;
	MoveCtl(&m_prev, bx, by, prevW, bh); bx += prevW + gap;
	MoveCtl(&m_play, bx, by, playW, bh); bx += playW + gap;
	MoveCtl(&m_pause, bx, by, pauseW, bh); bx += pauseW + gap;
	MoveCtl(&m_stop, bx, by, stopW, bh); bx += stopW + gap;
	MoveCtl(&m_fadeout, bx, by, fadeW, bh); bx += fadeW + gap;
	MoveCtl(&m_next, bx, by, nextW, bh); bx += nextW + (int)(8 * s);
	MoveCtl(&m_jacket, bx, by, jkw, bh); bx += jkw + gap;
	MoveCtl(&m_eq, bx, by, ebw, bh); bx += ebw + gap;
	MoveCtl(&m_piano, bx, by, pbw, bh); bx += pbw + gap;
	if (m_analyzer.GetSafeHwnd()) {
		MoveCtl(&m_analyzer, bx, by, abw, bh);
		bx += abw + gap;
	}
	if (m_pro.GetSafeHwnd()) {
		MoveCtl(&m_pro, bx, by, prbw, bh);
		bx += prbw + gap;
	}
	MoveCtl(&m_vollabel, volLblX, by + (int)(5 * s), volLblW, (int)(15 * s));
	MoveCtl(&m_vol, volSlX, by + (int)(4 * s), volSlW, (int)(16 * s));
	MoveCtl(&m_volval, volvalX, by + (int)(5 * s), volValW, (int)(16 * s));

	// ===== オプション行(1段): 連続/ループ/回数/ランダム + スペアナ/ST/フォルダ =====
	// chkRowH は 12px フォント(約 tmHeight≈16)が DrawSmartText2 で縮小されない高さにする。
	int by2 = by + bh + (int)(4 * s);
	int ch = (int)(24 * s);
	int chkRowH = (int)(20 * s);
	int optY = by2 + (ch - chkRowH) / 2;
	int cx = M;
	MoveCtl(&m_renzoku, cx, optY, (int)(86 * s), chkRowH); cx += (int)(90 * s);
	MoveCtl(&m_loop, cx, optY, (int)(92 * s), chkRowH); cx += (int)(96 * s);
	MoveCtl(&m_kaisuuL, cx, optY, (int)(80 * s), chkRowH); cx += (int)(82 * s);
	MoveCtl(&m_kaisuu, cx, optY, (int)(36 * s), chkRowH); cx += (int)(40 * s);
	MoveCtl(&m_random, cx, optY, (int)(88 * s), chkRowH); cx += (int)(92 * s);
	MoveCtl(&m_xfade, cx, optY, (int)(100 * s), chkRowH); cx += (int)(104 * s);
	MoveCtl(&m_xfadeSec, cx, optY, (int)(40 * s), chkRowH); cx += (int)(42 * s);
	MoveCtl(&m_xfadeL, cx, optY, (int)(24 * s), chkRowH); cx += (int)(28 * s);
	int folW = (int)(54 * s), stW = (int)(72 * s), supeW = (int)(62 * s);
	const int prWFull = max(1, (int)(76 * s));
	const int prWShort = max(1, (int)(36 * s));
	const int rollWFull = max(1, (int)(56 * s));
	const int rollWShort = max(1, (int)(36 * s));
	const int randomEndX = cx;
	int btnRowH = (int)(24 * s);
	int btnY1 = by2 + (ch - btnRowH) / 2;
	int rcx = W - M - folW;
	MoveCtl(&m_folder, rcx, by2, folW, ch); rcx -= (int)(4 * s) + stW;
	MoveCtl(&m_st, rcx, btnY1, stW, btnRowH); rcx -= (int)(4 * s) + supeW;
	MoveCtl(&m_supe, rcx, btnY1, supeW, btnRowH);
	const int prGap = (int)(8 * s);
	const bool prUseFull = (rcx - prGap - prWFull - (int)(4 * s) - rollWFull >= randomEndX);
	const int prW = prUseFull ? prWFull : prWShort;
	const int rollW = prUseFull ? rollWFull : rollWShort;
	if (m_prompt.GetSafeHwnd()) {
		const int prShortLv = prUseFull ? 0 : 1;
		if (prShortLv != m_mpPromptShort) {
			m_mpPromptShort = prShortLv;
			m_prompt.SetWindowText(prUseFull
				? LL14(L"プロンプト", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"프롬프트", L"提示", L"موجه", L"Промпт", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"Istem")
				: LL14(L"指示", L"Pmt", L"Pmt", L"Pmt", L"Pmt", L"지시", L"指示", L"توجيه", L"Прм", L"Pmt", L"Pmt", L"Pmt", L"Pmt", L"Pmt"));
		}
		rcx -= (int)(4 * s) + prW;
		MoveCtl(&m_prompt, rcx, btnY1, prW, btnRowH);
	}
	if (m_cmdroll.GetSafeHwnd()) {
		const int rollShortLv = prUseFull ? 0 : 1;
		if (rollShortLv != m_mpCmdRollShort) {
			m_mpCmdRollShort = rollShortLv;
			m_cmdroll.SetWindowText(prUseFull
				? LL14(L"ロール", L"Roll", L"Rouleau", L"Roll", L"Roll", L"롤", L"卷轴", L"Roll", L"Roll", L"Roll", L"Roll", L"Roll", L"Roll", L"Rulo")
				: LL14(L"巻", L"Rol", L"Rol", L"Rol", L"Rol", L"롤", L"卷", L"Rol", L"Rol", L"Rol", L"Rol", L"Rol", L"Rol", L"Rol"));
		}
		rcx -= (int)(4 * s) + rollW;
		MoveCtl(&m_cmdroll, rcx, btnY1, rollW, btnRowH);
	}

	// ===== サウンドグループ: 設定 + DS/拡張/テンポ/ピッチ(1段で省スペース) =====
	int sndTop = by2 + ch + (int)(5 * s);
	int sy = sndTop + gTitle;
	int slLabelH = (int)(15 * s), slH = (int)(16 * s);   // ラベル(13px)が収まる高さ
	MoveCtl(&m_settings, M + gPad, sy + (int)(4 * s), (int)(48 * s), (int)(24 * s));
	int slX = M + gPad + (int)(54 * s);
	int slGap = (int)(8 * s);
	int slW = (W - M - gPad - slX - slGap * 3) / 4;
	if (slW < (int)(56 * s)) slW = (int)(56 * s);
	struct { CCustomStatic* lbl; CCustomSliderCtrl* sl; } snd[4] = {
		{ &m_dsvolL, &m_dsvol }, { &m_kvolL, &m_kvol }, { &m_tempoL, &m_tempo }, { &m_pitchL, &m_pitch }
	};
	for (int i = 0; i < 4; i++) {
		int cxs = slX + i * (slW + slGap);
		MoveCtl(snd[i].lbl, cxs, sy, slW, slLabelH);
		MoveCtl(snd[i].sl, cxs, sy + slLabelH + (int)(1 * s), slW, slH);
		if (i == 0) m_dsvolSlW = slW;
	}
	int sndBottom = sy + slLabelH + slH + (int)(1 * s) + gPad;
	MoveCtl(&m_grpSnd, M, sndTop, W - M * 2, sndBottom - sndTop);

	// ===== プレイリストグループ: ツールバー + リスト + 下部チェック =====
	int plTop = sndBottom + (int)(5 * s);
	int by4 = plTop + gTitle;
	int tbH = (int)(19 * s);
	int comboW = (int)(120 * s);
	LayoutPlselCombo(M + gPad, by4, comboW, tbH, s);
	int tx = M + gPad + comboW + (int)(5 * s);
	int tbw = (int)(50 * s);
	MoveCtl(&m_plrename, tx, by4, tbw, tbH); tx += tbw + (int)(3 * s);
	MoveCtl(&m_pldelete, tx, by4, tbw, tbH); tx += tbw + (int)(4 * s);
	int m3uw = (int)(44 * s);
	MoveCtl(&m_m3uExport, tx, by4, m3uw, tbH); tx += m3uw + (int)(2 * s);
	MoveCtl(&m_m3uImport, tx, by4, m3uw, tbH); tx += m3uw + (int)(6 * s);
	// 「ツール」はツールバー上の明示ボタン（左クリックでメニュー）。▾単独は発見不能だった。
	const int toolsBtnW = (int)(88 * s);
	MoveCtl(&m_toolsToggle, tx, by4, toolsBtnW, tbH); tx += toolsBtnW + (int)(6 * s);
	int ibw = (int)(16 * s);
	int filtW = (int)(56 * s);
	int regexW = (int)(64 * s);
	int findW = (int)(64 * s);
	MoveCtl(&m_find, tx, by4 + (int)(1 * s), findW, tbH - (int)(2 * s)); tx += findW + (int)(2 * s);
	MoveCtl(&m_findFilter, tx, by4, filtW, tbH); tx += filtW + (int)(2 * s);
	MoveCtl(&m_findRegex, tx, by4, regexW, tbH); tx += regexW + (int)(2 * s);
	MoveCtl(&m_finddown, tx, by4, ibw, tbH); tx += ibw + (int)(1 * s);
	MoveCtl(&m_findup, tx, by4, ibw, tbH);
	int delGap = (int)(24 * s);
	int delW = (int)(50 * s);
	int delX = W - M - gPad - delW;
	int moveRight = delX - delGap;
	MoveCtl(&m_itemdel, delX, by4, delW, tbH);
	MoveCtl(&m_lsdown, moveRight - ibw, by4, ibw, tbH);
	MoveCtl(&m_down, moveRight - ibw * 2 - (int)(1 * s), by4, ibw, tbH);
	MoveCtl(&m_up, moveRight - ibw * 3 - (int)(2 * s), by4, ibw, tbH);
	MoveCtl(&m_lsup, moveRight - ibw * 4 - (int)(3 * s), by4, ibw, tbH);

	// 折りたたみ: 並べ替え / フォルダ追加（開閉はツールメニューから）
	int toolsH = 0;
	int byTools = by4 + tbH + (int)(2 * s);
	{
		if (m_cheatBtn.GetSafeHwnd())
			CCC_CaptionPlaceHelpBtn(m_hWnd, &m_cheatBtn);
		if (savedata.mpToolsOpen) {
			toolsH = tbH + (int)(2 * s);
			int sx = M + gPad;
			int sw = (int)(48 * s);
			MoveCtl(&m_sortName, sx, byTools, sw, tbH); sx += sw + (int)(2 * s);
			MoveCtl(&m_sortArt, sx, byTools, sw, tbH); sx += sw + (int)(2 * s);
			MoveCtl(&m_sortAlb, sx, byTools, sw, tbH); sx += sw + (int)(2 * s);
			MoveCtl(&m_sortTime, sx, byTools, sw, tbH); sx += sw + (int)(6 * s);
			MoveCtl(&m_addFolder, sx, byTools, (int)(88 * s), tbH);
			if (m_sortName.GetSafeHwnd()) m_sortName.ShowWindow(SW_SHOW);
			if (m_sortArt.GetSafeHwnd()) m_sortArt.ShowWindow(SW_SHOW);
			if (m_sortAlb.GetSafeHwnd()) m_sortAlb.ShowWindow(SW_SHOW);
			if (m_sortTime.GetSafeHwnd()) m_sortTime.ShowWindow(SW_SHOW);
			if (m_addFolder.GetSafeHwnd()) m_addFolder.ShowWindow(SW_SHOW);
		}
		else {
			toolsH = 0;
			if (m_sortName.GetSafeHwnd()) m_sortName.ShowWindow(SW_HIDE);
			if (m_sortArt.GetSafeHwnd()) m_sortArt.ShowWindow(SW_HIDE);
			if (m_sortAlb.GetSafeHwnd()) m_sortAlb.ShowWindow(SW_HIDE);
			if (m_sortTime.GetSafeHwnd()) m_sortTime.ShowWindow(SW_HIDE);
			if (m_addFolder.GetSafeHwnd()) m_addFolder.ShowWindow(SW_HIDE);
		}
	}

	int swH = (int)(22 * s);
	int listY = by4 + tbH + toolsH + (int)(4 * s);
	// UXステータス帯（操作ヒント）を画面最下端に確保。ボタンと重ねない
	const int uxBandH = max(16, (int)(18 * s));
	const int uxBandPad = max(2, (int)(3 * s));
	const int botY = H - swH - M - uxBandH - uxBandPad;
	const int micRowH = chkRowH + (int)(2 * s);
	const int ckY = botY - (int)(8 * s) - chkRowH;
	const int micY = ckY - micRowH;
	int listH = micY - (int)(3 * s) - listY;
	// 下限で押し広げるとマイク行に食い込み末尾が黒く潰れるので、空きが足りないときは縮めるだけ
	if (listH < 1) listH = 1;

	// ===== ライブラリ/履歴 左ドロワー + Tempトグル(レール) =====
	// Lib/Hist のみドロワー展開。Temp は押しボタン凹み=ON（パネル無し）。
	EnsureLibControls();
	const int libRail = (int)(26 * s);
	const int libPanel = (int)(280 * s);
	const int libGap = (int)(4 * s);
	const BOOL libOpen = (savedata.mpLibOpen != 0);
	const BOOL histOpen = (savedata.mpHistOpen != 0);
	const BOOL tempOn = (savedata.mpTempOpen != 0);
	const int libW = (libOpen || histOpen) ? libPanel : libRail;
	const int listX = M + gPad + libW + libGap;
	int listW = W - M - gPad - listX;
	if (listW < (int)(80 * s)) listW = (int)(80 * s);

	const int togH = (int)(22 * s);
	const int headerH = (libOpen || histOpen) ? (togH + (int)(4 * s)) : (togH * 3 + (int)(6 * s));
	if (libOpen || histOpen) {
		const int gapT = (int)(3 * s);
		const int third = (libPanel - gapT * 2) / 3;
		if (m_libToggle.GetSafeHwnd()) {
			MoveCtl(&m_libToggle, M + gPad, listY, third, togH);
			m_libToggle.ShowWindow(SW_SHOW);
			m_libToggle.SetWindowText(libOpen ? L"≪ Lib" : L"Lib");
		}
		if (m_histToggle.GetSafeHwnd()) {
			MoveCtl(&m_histToggle, M + gPad + third + gapT, listY, third, togH);
			m_histToggle.ShowWindow(SW_SHOW);
			m_histToggle.SetWindowText(histOpen ? L"≪ Hist" : L"Hist");
		}
		if (m_tempToggle.GetSafeHwnd()) {
			MoveCtl(&m_tempToggle, M + gPad + (third + gapT) * 2, listY, third, togH);
			m_tempToggle.ShowWindow(SW_SHOW);
			m_tempToggle.SetWindowText(L"Temp");
			if (m_tempToggle.GetCheck() != (tempOn ? BST_CHECKED : BST_UNCHECKED))
				m_tempToggle.SetCheck(tempOn ? BST_CHECKED : BST_UNCHECKED);
		}
	}
	else {
		if (m_libToggle.GetSafeHwnd()) {
			MoveCtl(&m_libToggle, M + gPad, listY, libRail, togH);
			m_libToggle.ShowWindow(SW_SHOW);
			m_libToggle.SetWindowText(L"Lib");
		}
		if (m_histToggle.GetSafeHwnd()) {
			MoveCtl(&m_histToggle, M + gPad, listY + togH + (int)(2 * s), libRail, togH);
			m_histToggle.ShowWindow(SW_SHOW);
			m_histToggle.SetWindowText(L"Hist");
		}
		if (m_tempToggle.GetSafeHwnd()) {
			MoveCtl(&m_tempToggle, M + gPad, listY + (togH + (int)(2 * s)) * 2, libRail, togH);
			m_tempToggle.ShowWindow(SW_SHOW);
			m_tempToggle.SetWindowText(L"Temp");
			if (m_tempToggle.GetCheck() != (tempOn ? BST_CHECKED : BST_UNCHECKED))
				m_tempToggle.SetCheck(tempOn ? BST_CHECKED : BST_UNCHECKED);
		}
	}

	const int bodyTop = listY + headerH;
	if (libOpen) {
		const int btnH = (int)(20 * s);
		const int bodyH = listH - headerH - btnH - (int)(4 * s);
		const int treeW = (int)(libPanel * 0.46);
		const int albX = M + gPad + treeW + (int)(3 * s);
		const int albW = libPanel - treeW - (int)(3 * s);
		if (bodyH > 10) {
			MoveCtl(&m_libTree, M + gPad, bodyTop, treeW, bodyH);
			MoveCtl(&m_libAlbums, albX, bodyTop, albW, bodyH);
			if (m_libAlbums.GetSafeHwnd() && m_libAlbums.GetHeaderCtrl()) {
				CRect arc; m_libAlbums.GetClientRect(&arc);
				m_libAlbums.SetColumnWidth(0, max(40, arc.Width() - 4));
			}
		}
		const int btnY = listY + listH - btnH;
		const int half = (libPanel - (int)(4 * s)) / 2;
		MoveCtl(&m_libAddRoot, M + gPad, btnY, half, btnH);
		MoveCtl(&m_libAddPl, M + gPad + half + (int)(4 * s), btnY, half, btnH);
		if (m_libTree.GetSafeHwnd()) m_libTree.ShowWindow(SW_SHOW);
		if (m_libAlbums.GetSafeHwnd()) m_libAlbums.ShowWindow(SW_SHOW);
		if (m_libAddRoot.GetSafeHwnd()) m_libAddRoot.ShowWindow(SW_SHOW);
		if (m_libAddPl.GetSafeHwnd()) m_libAddPl.ShowWindow(SW_SHOW);
		if (m_libAlbums.GetSafeHwnd())
			LibFitNoHScroll(&m_libAlbums);
		if (!m_libTreeBuilt && !m_libBuildPosted) {
			m_libBuildPosted = 1;
			if (m_libTree.GetSafeHwnd() && m_libTree.GetCount() == 0) {
				m_libTree.InsertItem(LL14(L"読み込み中…", L"Loading...", L"Chargement...", L"Caricamento...", L"Cargando...", L"로딩 중...", L"加载中…", L"Loading...", L"Загрузка...", L"Laden...", L"Carregando...", L"Laden...", L"Wczytywanie...", L"Yukleniyor..."), TVI_ROOT, TVI_LAST);
			}
			PostMessage(WM_MP_LIB_BUILD, 0, 0);
		}
		if (m_histList.GetSafeHwnd()) { MoveCtl(&m_histList, M + gPad, listY, 0, 0); m_histList.ShowWindow(SW_HIDE); }
	}
	else if (histOpen) {
		const int bodyH = listH - headerH;
		if (bodyH > 10)
			MoveCtl(&m_histList, M + gPad, bodyTop, libPanel, bodyH);
		if (m_histList.GetSafeHwnd()) {
			m_histList.ShowWindow(SW_SHOW);
			LibFitNoHScroll(&m_histList);
		}
		if (!m_histBuilt)
			HistRebuildList();
		if (m_libTree.GetSafeHwnd()) { MoveCtl(&m_libTree, M + gPad, listY, 0, 0); m_libTree.ShowWindow(SW_HIDE); }
		if (m_libAlbums.GetSafeHwnd()) { MoveCtl(&m_libAlbums, M + gPad, listY, 0, 0); m_libAlbums.ShowWindow(SW_HIDE); }
		if (m_libAddRoot.GetSafeHwnd()) { MoveCtl(&m_libAddRoot, M + gPad, listY, 0, 0); m_libAddRoot.ShowWindow(SW_HIDE); }
		if (m_libAddPl.GetSafeHwnd()) { MoveCtl(&m_libAddPl, M + gPad, listY, 0, 0); m_libAddPl.ShowWindow(SW_HIDE); }
	}
	else {
		if (m_libTree.GetSafeHwnd()) { MoveCtl(&m_libTree, M + gPad, listY, 0, 0); m_libTree.ShowWindow(SW_HIDE); }
		if (m_libAlbums.GetSafeHwnd()) { MoveCtl(&m_libAlbums, M + gPad, listY, 0, 0); m_libAlbums.ShowWindow(SW_HIDE); }
		if (m_libAddRoot.GetSafeHwnd()) { MoveCtl(&m_libAddRoot, M + gPad, listY, 0, 0); m_libAddRoot.ShowWindow(SW_HIDE); }
		if (m_libAddPl.GetSafeHwnd()) { MoveCtl(&m_libAddPl, M + gPad, listY, 0, 0); m_libAddPl.ShowWindow(SW_HIDE); }
		if (m_histList.GetSafeHwnd()) { MoveCtl(&m_histList, M + gPad, listY, 0, 0); m_histList.ShowWindow(SW_HIDE); }
	}
	if (m_tempHint.GetSafeHwnd()) { MoveCtl(&m_tempHint, M + gPad, listY, 0, 0); m_tempHint.ShowWindow(SW_HIDE); }
	if (m_tempClear.GetSafeHwnd()) { MoveCtl(&m_tempClear, M + gPad, listY, 0, 0); m_tempClear.ShowWindow(SW_HIDE); }

	MoveCtl(&m_list, listX, listY, listW, listH);

	// Lib/Hist 左レール矩形（アクリル時はグループのクロマ透過に任せる。不透明下地は敷かない）
	m_plRailRect.SetRect(M + gPad, listY, listX - (int)(1 * s), listY + listH);
	if (m_plRailRect.Width() < 1 || m_plRailRect.Height() < 1)
		m_plRailRect.SetRectEmpty();
	if (m_plRailBg.GetSafeHwnd()) {
		MoveCtl(&m_plRailBg, M + gPad, listY, 0, 0);
		m_plRailBg.ShowWindow(SW_HIDE);
	}

	// 空PL案内(本当に0曲のときだけ)
	{
		const BOOL emptyPl = (pl == NULL || pl->pc == NULL || pl->playcnt <= 0);
		const int btnW = (int)(200 * s);
		const int btnH = (int)(40 * s);
		const int gap = (int)(12 * s);
		const int totalH = btnH * 2 + gap;
		const int ex = listX + (listW - btnW) / 2;
		const int ey = listY + (listH - totalH) / 2;
		if (m_emptyFolder.GetSafeHwnd()) {
			MoveCtl(&m_emptyFolder, ex, ey, btnW, btnH);
			m_emptyFolder.ShowWindow(emptyPl ? SW_SHOW : SW_HIDE);
		}
		if (m_emptyM3u.GetSafeHwnd()) {
			MoveCtl(&m_emptyM3u, ex, ey + btnH + gap, btnW, btnH);
			m_emptyM3u.ShowWindow(emptyPl ? SW_SHOW : SW_HIDE);
		}
	}

	// アルバム/コメント列(最終列=4)をリスト右端へぴたりとフィットさせる
	FitPlaylistLastColumn();

	// 下部チェック(ツールチップ〜曲保存): 均等スロット幅に収まる最長ラベルを実測で選ぶ
	// CCustomCheckBox::OnDrawLayer と同じ箱サイズ/余白で必要幅 = 箱 + 8 + 文字幅 + 右余白
	int availCk = W - (M + gPad) * 2;
	int gapCk = (int)(5 * s);
	int ckW = (availCk - gapCk * 5) / 6;
	if (ckW < 1) ckW = 1;
	int boxS = chkRowH - 4;
	if (boxS > 18) boxS = 18;
	if (boxS < 14) boxS = 14;
	const int ckExtra = boxS + 8 + 4;

	CClientDC cdc(this);
	CFont* pOldChkF = nullptr;
	if (m_fontChk.GetSafeHandle())
		pOldChkF = cdc.SelectObject(&m_fontChk);
	auto ckNeed = [&](LPCTSTR t) -> int {
		if (!t || !*t) return ckExtra;
		return ckExtra + cdc.GetTextExtent(t).cx;
	};
	auto ckApply = [&](CCustomCheckBox& ctl, int idx, LPCTSTR full, LPCTSTR mid, LPCTSTR sh) {
		LPCTSTR use = sh;
		int lv = 2;
		if (ckNeed(full) <= ckW) { use = full; lv = 0; }
		else if (ckNeed(mid) <= ckW) { use = mid; lv = 1; }
		if (m_mpChkShort[idx] != lv) {
			m_mpChkShort[idx] = lv;
			ctl.SetWindowText(use);
		}
	};

	// tip: ツールチップ → チップ → チ
	ckApply(m_tip, 0,
		LL14(L"ツールチップ", L"Tooltips", L"Info-bulles", L"Suggerimenti", L"Sugerencias", L"툴팁", L"工具提示", L"تلميحات", L"Подсказки", L"Tooltips", L"Dicas", L"Tooltips", L"Etykiety", L"İpuçları"),
		LL14(L"チップ", L"Tips", L"Bulles", L"Sugger.", L"Tips", L"팁", L"提示", L"تلميح", L"Подск.", L"Tips", L"Dicas", L"Tips", L"Etyk.", L"İpucu"),
		LL14(L"チ", L"Tip", L"Tip", L"Tip", L"Tip", L"팁", L"提", L"تل", L"Пд", L"Tip", L"Dic", L"Tip", L"Et", L"İp"));
	// mini: 最小化連動 → 最小化 → 最小
	ckApply(m_mini, 1,
		LL14(L"最小化連動", L"Min. sync", L"Sync. min.", L"Sinc. min.", L"Sincr. min.", L"최소화 연동", L"最小化联动", L"تزامن التصغير", L"Синхр. сверт.", L"Min.-Sync", L"Sinc. min.", L"Min. koppelen", L"Synch. min.", L"Min. eşitle"),
		LL14(L"最小化", L"Minimize", L"Réduire", L"Riduci", L"Minimizar", L"최소화", L"最小化", L"تصغير", L"Свернуть", L"Minimieren", L"Minimizar", L"Minimaliseren", L"Minimalizuj", L"Küçült"),
		LL14(L"最小", L"Min", L"Min", L"Min", L"Min", L"최소", L"最小", L"تص", L"Свр", L"Min", L"Min", L"Min", L"Min", L"Min"));
	// 途中保存 → 途中 → 途
	ckApply(m_savemp3, 2,
		LL14(L"途中保存", L"Resume save", L"Reprise", L"Ripresa", L"Reanudar", L"위치저장", L"续播保存", L"حفظ الموضع", L"Позиция", L"Position", L"Retomar", L"Hervatten", L"Wznowienie", L"Konum kaydet"),
		LL14(L"途中", L"Resume", L"Reprise", L"Ripresa", L"Reanudar", L"위치", L"续播", L"موضع", L"Поз.", L"Pos.", L"Retomar", L"Hervat", L"Wznow", L"Konum"),
		LL14(L"途", L"Res", L"Rep", L"Rip", L"Rea", L"위", L"续", L"مو", L"Пз", L"Pos", L"Ret", L"Her", L"Wz", L"Kon"));
	// DShow途中保存 → DShow保存 → DS
	ckApply(m_saveds, 3,
		LL14(L"DShow途中保存", L"DShow resume", L"DShow reprise", L"DShow ripresa", L"DShow reanudar", L"DShow 위치저장", L"DShow续播", L"حفظ موضع DShow", L"DShow позиция", L"DShow Position", L"DShow retomar", L"DShow hervat", L"DShow wznow", L"DShow sürdür"),
		LL14(L"DShow保存", L"DShow save", L"DShow sauver", L"DShow salva", L"DShow guardar", L"DShow 저장", L"DShow保存", L"حفظ DS", L"DShow сохр.", L"DShow speichern", L"DShow salvar", L"DShow opslaan", L"DShow zapis", L"DShow kaydet"),
		LL14(L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS", L"DS"));
	// WAVファイルへ保存 → WAV保存 → WAV
	ckApply(m_savewav, 4,
		LL14(L"WAVファイルへ保存", L"Save to WAV file", L"Enregistrer en WAV", L"Salva come WAV", L"Guardar como WAV", L"WAV 파일로 저장", L"保存到WAV文件", L"حفظ كـ WAV", L"Сохранить в WAV", L"Als WAV speichern", L"Salvar como WAV", L"Opslaan als WAV", L"Zapisz jako WAV", L"WAV olarak kaydet"),
		LL14(L"WAV保存", L"Save WAV", L"Sauver WAV", L"Salva WAV", L"Guardar WAV", L"WAV 저장", L"WAV保存", L"حفظ WAV", L"WAV сохр.", L"WAV speichern", L"Salvar WAV", L"WAV opslaan", L"Zapis WAV", L"WAV kaydet"),
		LL14(L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV", L"WAV"));
	// 曲ごとに設定保存 → 曲ごと保存 → 曲保存
	ckApply(m_saveparam, 5,
		LL14(L"曲ごとに設定保存", L"Save per-song", L"Réglages/morceau", L"Impost. per brano", L"Ajustes por pista", L"곡별 설정 저장", L"逐曲保存设置", L"حفظ لكل أغنية", L"Настройки на трек", L"Pro Titel speichern", L"Config. por faixa", L"Per nummer opslaan", L"Ustaw. na utwor", L"Parça başına kaydet"),
		LL14(L"曲ごと保存", L"Per-song", L"Par morceau", L"Per brano", L"Por pista", L"곡별 저장", L"逐曲保存", L"لكل أغنية", L"На трек", L"Pro Titel", L"Por faixa", L"Per nummer", L"Na utwor", L"Parça kaydet"),
		LL14(L"曲保存", L"Song save", L"Mém. piste", L"Salva brano", L"Guarda pista", L"곡저장", L"曲保存", L"أغنية", L"Трек", L"Titel", L"Faixa", L"Nummer", L"Utwór", L"Parça"));

	if (pOldChkF)
		cdc.SelectObject(pOldChkF);

	int ckx = M + gPad;
	// マイクミックス行(チェック帯の直上)。端末コンボも同じ行に並べる
	{
		int mx = M + gPad;
		const int micChkW = (int)(100 * s);
		const int micLabW = (int)(36 * s);
		const int micSlW = (int)(100 * s);
		const int micMeterW = (int)(10 * s);
		const int gapMic = (int)(4 * s);
		MoveCtl(&m_micmix, mx, micY, micChkW, chkRowH); mx += micChkW + gapCk;
		MoveCtl(&m_miclevL, mx, micY, micLabW, chkRowH); mx += micLabW + (int)(2 * s);
		MoveCtl(&m_miclev, mx, micY, micSlW - micMeterW - (int)(4 * s), chkRowH);
		mx += micSlW - micMeterW - (int)(2 * s);
		MoveCtl(&m_micMeter, mx, micY, micMeterW, chkRowH);
		mx += micMeterW + gapMic;
		// 右端（終了ボタン手前）まで。下部ツール列には載せない
		const int micDevRight = W - M - gPad;
		int micDevW = micDevRight - mx;
		if (micDevW > (int)(280 * s)) micDevW = (int)(280 * s);
		if (micDevW < (int)(100 * s)) micDevW = (int)(100 * s);
		if (mx + micDevW > micDevRight)
			micDevW = micDevRight - mx;
		if (micDevW > (int)(60 * s) && m_micdev.GetSafeHwnd())
			MoveCtl(&m_micdev, mx, micY, micDevW, chkRowH);
	}
	MoveCtl(&m_tip, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_mini, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_savemp3, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_saveds, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_savewav, ckx, ckY, ckW, chkRowH); ckx += ckW + gapCk;
	MoveCtl(&m_saveparam, ckx, ckY, ckW, chkRowH);
	int plBottom = ckY + chkRowH + gPad;
	MoveCtl(&m_grpPl, M, plTop, W - M * 2, plBottom - plTop);

	// 最下部: 切替 / 保存リセット / 録音 / キャプチャ / ツール短カット… / 終了
	int swW = (int)(120 * s);
	MoveCtl(&m_switch, M, botY, swW, swH);
	int rsW = (int)(100 * s);
	bx = M + swW + (int)(6 * s);
	MoveCtl(&m_resetdata, bx, botY, rsW, swH); bx += rsW + (int)(6 * s);
	int recW = (int)(64 * s);
	MoveCtl(&m_record, bx, botY, recW, swH); bx += recW + (int)(6 * s);
	int capW = (int)(80 * s);
	MoveCtl(&m_capture, bx, botY, capW, swH); bx += capW + (int)(6 * s);
	int exW = (int)(80 * s);
	const int exitLeft = W - M - exW;
	const int gapBot = (int)(4 * s);
	if (!savedata.mpBotToolsInited) {
		savedata.mpBotToolsInited = 1;
		savedata.mpBotToolsFlags = 0x10F; // DJ|Tag|BPM|Sleep|Maze
	}
	const int botFl = savedata.mpBotToolsFlags;
	CCustomStandardButton* botBtn[9] = {
		&m_botDj, &m_botTag, &m_botBpm, &m_botSleep, &m_botMirror, &m_botSsViz, &m_botAlarm, &m_botRemote, &m_botMaze
	};
	const int botBit[9] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 };
	const int wFull[9] = {
		(int)(56 * s), (int)(52 * s), (int)(48 * s), (int)(56 * s),
		(int)(60 * s), (int)(40 * s), (int)(56 * s), (int)(64 * s), (int)(52 * s)
	};
	const int wMid[9] = {
		(int)(36 * s), (int)(36 * s), (int)(36 * s), (int)(40 * s),
		(int)(40 * s), (int)(32 * s), (int)(40 * s), (int)(44 * s), (int)(36 * s)
	};
	const int wShort[9] = {
		(int)(28 * s), (int)(28 * s), (int)(28 * s), (int)(28 * s),
		(int)(28 * s), (int)(28 * s), (int)(28 * s), (int)(28 * s), (int)(28 * s)
	};
	int needFull = 0, needMid = 0, needShort = 0;
	for (int i = 0; i < 9; ++i) {
		if (!(botFl & botBit[i])) continue;
		needFull += wFull[i] + gapBot;
		needMid += wMid[i] + gapBot;
		needShort += wShort[i] + gapBot;
	}
	const int freeBot = exitLeft - bx - gapBot;
	int botShortLv = 0;
	if (needFull > freeBot) botShortLv = 1;
	if (needMid > freeBot) botShortLv = 2;
	if (botShortLv != m_mpBotShort) {
		m_mpBotShort = botShortLv;
		if (m_botDj.GetSafeHwnd())
			m_botDj.SetWindowText(botShortLv >= 2 ? L"DJ" : LL14(L"DJパッド", L"DJ Pad", L"Pad DJ", L"Pad DJ", L"Pad DJ", L"DJ", L"DJ垫", L"DJ", L"DJ", L"DJ-Pad", L"Pad DJ", L"DJ-pad", L"Pad DJ", L"DJ"));
		if (m_botTag.GetSafeHwnd())
			m_botTag.SetWindowText(botShortLv >= 2 ? L"Tag" : LL14(L"タグ", L"Tags", L"Tags", L"Tag", L"Tags", L"태그", L"标签", L"وسوم", L"Теги", L"Tags", L"Tags", L"Tags", L"Tagi", L"Etiket"));
		if (m_botBpm.GetSafeHwnd())
			m_botBpm.SetWindowText(L"BPM");
		if (m_botSleep.GetSafeHwnd())
			m_botSleep.SetWindowText(botShortLv >= 2 ? L"Slp" : LL14(L"スリープ", L"Sleep", L"Veille", L"Sleep", L"Sueño", L"슬립", L"睡眠", L"نوم", L"Сон", L"Schlaf", L"Sono", L"Slaap", L"Sen", L"Uyku"));
		if (m_botMirror.GetSafeHwnd())
			m_botMirror.SetWindowText(botShortLv >= 2 ? L"Mir" : LL14(L"ミラー", L"Mirror", L"Miroir", L"Mirror", L"Espejo", L"미러", L"镜像", L"مرآة", L"Зеркало", L"Spiegel", L"Espelho", L"Spiegel", L"Lustro", L"Ayna"));
		if (m_botSsViz.GetSafeHwnd())
			m_botSsViz.SetWindowText(L"SS");
		if (m_botAlarm.GetSafeHwnd())
			m_botAlarm.SetWindowText(botShortLv >= 2 ? L"Alm" : LL14(L"アラーム", L"Alarm", L"Alarme", L"Sveglia", L"Alarma", L"알람", L"闹钟", L"منبه", L"Будильник", L"Wecker", L"Alarme", L"Wekker", L"Budzik", L"Alarm"));
		if (m_botRemote.GetSafeHwnd())
			m_botRemote.SetWindowText(botShortLv >= 2 ? L"Rem" : LL14(L"リモート", L"Remote", L"Remote", L"Remote", L"Remoto", L"리모트", L"遥控", L"تحكم", L"Пульт", L"Remote", L"Remoto", L"Remote", L"Pilot", L"Uzaktan"));
		if (m_botMaze.GetSafeHwnd())
			m_botMaze.SetWindowText(botShortLv >= 2 ? L"Mz" : LL14(L"迷路", L"Maze", L"Labyrinthe", L"Labirinto", L"Laberinto", L"미로", L"迷宫", L"متاهة", L"Лабиринт", L"Labyrinth", L"Labirinto", L"Doolhof", L"Labirynt", L"Labirent"));
	}
	for (int i = 0; i < 9; ++i) {
		CCustomStandardButton* b = botBtn[i];
		if (!b->GetSafeHwnd()) continue;
		if (!(botFl & botBit[i])) {
			if (b->IsWindowVisible()) b->ShowWindow(SW_HIDE);
			continue;
		}
		const int bw = (botShortLv >= 2) ? wShort[i] : (botShortLv >= 1) ? wMid[i] : wFull[i];
		if (bx + bw > exitLeft - 2) {
			if (b->IsWindowVisible()) b->ShowWindow(SW_HIDE);
			continue;
		}
		MoveCtl(b, bx, botY, bw, swH);
		if (!b->IsWindowVisible()) b->ShowWindow(SW_SHOW);
		bx += bw + gapBot;
	}
	MoveCtl(&m_exit, exitLeft, botY, exW, swH);

	CCC_GroupBoxesBack(GetSafeHwnd());   // 枠は最背面(子コントロールを覆わない)
	Invalidate();
}

void CMediaPlayerDlg::FitPlaylistLastColumn(int dragCol, int dragWidth)
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	CRect lcr;
	m_list.GetClientRect(&lcr);
	const int clientW = lcr.Width();
	if (clientW <= 0) return;

	// 最終列(5=アルバム)以外の幅合計。ドラッグ中の列は仮幅を使う。
	const BOOL bDragOther = (dragCol >= 0 && dragCol < 5 && dragWidth > 0);

	int used = 0;
	for (int ci = 0; ci < 5; ++ci) {
		if (bDragOther && ci == dragCol)
			used += dragWidth;
		else
			used += m_list.GetColumnWidth(ci);
	}

	const int minLast = (int)(80 * hD2);
	int last = clientW - used;
	if (last < minLast) last = minLast;

	if (bDragOther) {
		const int curDrag = m_list.GetColumnWidth(dragCol);
		if (curDrag != dragWidth)
			m_list.SetColumnWidth(dragCol, dragWidth);
	}

	if (m_list.GetColumnWidth(5) != last)
		m_list.SetColumnWidth(5, last);

	if (CHeaderCtrl* pHdr = m_list.GetHeaderCtrl())
		pHdr->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
	if (bDragOther)
		m_list.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_NOERASE);
}

void CMediaPlayerDlg::TickListHdrDragFit()
{
	if (m_listHdrDragCol < 0 || m_listHdrDragCol >= 5) return;
	CHeaderCtrl* pHdr = m_list.GetHeaderCtrl();
	if (!pHdr) return;
	HDITEM hi = {};
	hi.mask = HDI_WIDTH;
	if (pHdr->GetItem(m_listHdrDragCol, &hi) && hi.cxy > 0)
		FitPlaylistLastColumn(m_listHdrDragCol, hi.cxy);
}

void CMediaPlayerDlg::OnPlaylistHeaderNotify(NMHDR* pNMHDR, LRESULT* pResult)
{
	if (pResult) *pResult = 0;
	if (!pNMHDR) {
		FitPlaylistLastColumn();
		return;
	}

	HD_NOTIFY* phd = reinterpret_cast<HD_NOTIFY*>(pNMHDR);
	const UINT code = pNMHDR->code;
	int dragCol = -1;
	int dragCx = -1;
	if (phd && phd->iItem >= 0 && phd->iItem < 5) {
		dragCol = phd->iItem;
		if (phd->pitem)
			dragCx = phd->pitem->cxy;
	}

	switch (code) {
	case HDN_BEGINTRACKA:
	case HDN_BEGINTRACKW:
		if (phd) {
			m_listHdrDragCol = phd->iItem;
			if (phd->iItem >= 0 && phd->iItem < 5)
				SetTimer(kTimerListHdrDrag, 16, NULL);
		}
		return;
	case HDN_TRACKA:
	case HDN_TRACKW:
		if (phd && phd->iItem == 5)
			return;
		if (dragCol >= 0 && dragCx > 0)
			FitPlaylistLastColumn(dragCol, dragCx);
		return;
	case HDN_ENDTRACKA:
	case HDN_ENDTRACKW:
		KillTimer(kTimerListHdrDrag);
		m_listHdrDragCol = -1;
		FitPlaylistLastColumn();
		return;
	default:
		break;
	}
}

void CMediaPlayerDlg::OnListHeaderEndTrack(NMHDR* pNMHDR, LRESULT* pResult)
{
	OnPlaylistHeaderNotify(pNMHDR, pResult);
}

void CMediaPlayerDlg::SyncPushToggleButtons()
{
	// OnInitDialog 完了前は GetCheck/Repaint しない(Create 途中の再入・例外防止)
	if (!m_uiReady) return;
	extern int g_oggSubUiRestoring;
	if (g_oggSubUiRestoring) return;
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	const int supeOn = og->m_supe.GetCheck() ? 1 : 0;
	const int stOn = og->m_st.GetCheck() ? 1 : 0;
	// 破棄途中・未生成でもタイマーから呼ばれるためポインタを必ず確認する
	const int eqOpen = (og->m_EqualizerDlg
		&& ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd())
		&& ::IsWindowVisible(og->m_EqualizerDlg->m_hWnd)) ? 1 : 0;
	const int pianoOpen = (og->m_PianoRollDlg
		&& ::IsWindow(og->m_PianoRollDlg->GetSafeHwnd())
		&& ::IsWindowVisible(og->m_PianoRollDlg->m_hWnd)) ? 1 : 0;
	const int analyzerOpen = (og->m_AnalyzerDlg
		&& ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd())
		&& ::IsWindowVisible(og->m_AnalyzerDlg->m_hWnd)) ? 1 : 0;
	const int promptOpen = MpIsPromptOpen() ? 1 : 0;
	const int cmdRollOpen = MpIsCommandRollOpen() ? 1 : 0;
	if (supeOn != m_lastToggleSupe) {
		MpSetPushToggle(m_supe, supeOn, RGB(140, 220, 160), RGB(80, 180, 110), RGB(215, 240, 220), RGB(175, 215, 190));
		m_lastToggleSupe = supeOn;
	}
	if (stOn != m_lastToggleSt) {
		MpSetPushToggle(m_st, stOn, RGB(160, 200, 255), RGB(100, 150, 230), RGB(215, 230, 255), RGB(175, 200, 245));
		m_lastToggleSt = stOn;
	}
	if (eqOpen != m_lastToggleEq) {
		MpSetPushToggle(m_eq, eqOpen, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
		m_lastToggleEq = eqOpen;
	}
	if (pianoOpen != m_lastTogglePiano) {
		MpSetPushToggle(m_piano, pianoOpen, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
		m_lastTogglePiano = pianoOpen;
	}
	if (m_analyzer.GetSafeHwnd() && analyzerOpen != m_lastToggleAnalyzer) {
		MpSetPushToggle(m_analyzer, analyzerOpen, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
		m_lastToggleAnalyzer = analyzerOpen;
	}
	if (m_prompt.GetSafeHwnd() && promptOpen != m_lastTogglePrompt) {
		MpSetPushToggle(m_prompt, promptOpen, RGB(255, 180, 210), RGB(255, 120, 170), RGB(255, 225, 245), RGB(255, 180, 210));
		m_lastTogglePrompt = promptOpen;
	}
	if (m_cmdroll.GetSafeHwnd() && cmdRollOpen != m_lastToggleCmdRoll) {
		MpSetPushToggle(m_cmdroll, cmdRollOpen, RGB(200, 170, 255), RGB(160, 120, 240), RGB(230, 220, 255), RGB(200, 185, 250));
		m_lastToggleCmdRoll = cmdRollOpen;
	}
}

BOOL CMediaPlayerDlg::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	LPNMHDR pN = reinterpret_cast<LPNMHDR>(lParam);
	if (pN && ::IsWindow(m_list.GetSafeHwnd())) {
		HWND hHdr = m_list.GetHeaderCtrl() ? m_list.GetHeaderCtrl()->GetSafeHwnd() : NULL;
		if (hHdr && pN->hwndFrom == hHdr) {
			switch (pN->code) {
			case HDN_BEGINTRACKA:
			case HDN_BEGINTRACKW:
			case HDN_ENDTRACKA:
			case HDN_ENDTRACKW:
			case HDN_TRACKA:
			case HDN_TRACKW:
				OnPlaylistHeaderNotify(pN, pResult);
				return TRUE;
			}
		}
	}
	return CCustomBlurDialogExBase::OnNotify(wParam, lParam, pResult);
}

// 手動レイアウト後に仮想リストのスクロール範囲と Z 順を再確定する。
// OnSize では RedrawWindow するが、歌詞モード切替など DoLayout 単独呼び出し時は必要。
void CMediaPlayerDlg::RefreshListAfterLayout()
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	if (pl && pl->playcnt > 0) {
		int anchor = GetListScrollAnchor();
		int cnt = m_filtOn ? m_fcnt : pl->playcnt;
		m_list.SetItemCount(cnt);
		m_lastCount = cnt;
		if (!m_filtOn)
			RestoreListScrollAnchor(anchor);
	}
	m_list.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	m_list.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
}

// プレイリスト(pl)と同じく仮想リスト(LVS_OWNERDATA)としてミラーする。
// 仮想リストは SetItemCount でスクロール範囲(=スクロールバー)が直接確定するため、
// アクリル時に OpaqueFixer が WM_PAINT を横取りしてもスクロールバーが正しく表示される。
// (非仮想だと WM_PAINT 依存でスクロールバーが出ない/消える不具合になっていた。)
// 仮想リスト(LVS_OWNERDATA)の件数変化・再生中アイコン移動を反映する。
// Timer1(250ms)から呼ばれる。件数が同じなら SetItemCount は呼ばずコストを最小化する。
// bForce=TRUE は並べ替え/タグ更新時など表示内容が変わった場合に全行再取得を強制する。
void CMediaPlayerDlg::RefreshList(BOOL bForce)
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	if (!pl || pl->pc == NULL) {
		if (m_list.GetItemCount() > 0) { m_list.SetItemCount(0); m_lastCount = 0; }
		m_filtOn = 0; m_fcnt = 0;
		UpdateEmptyStateUi();
		return;
	}
	m_list.pc = pl->pc; // Load/realloc 後の実体ポインタを再同期

	// フィルタ再構築(キーワードが空 or OFFなら全件)。スマートフィルタも併用可。
	m_filtOn = 0;
	m_fcnt = 0;
	{
		CString kw;
		if (savedata.mpFindFilter && m_find.GetSafeHwnd()) {
			m_find.GetWindowText(kw);
			kw.Trim();
		}
		const BOOL wantKw = !kw.IsEmpty();
		PlaylistSearchCtx* searchCtx = NULL;
		if (wantKw)
			searchCtx = PlaylistSearchCtxCreate(kw, savedata.mpFindRegex != 0);
		MpSmartRule smartRule; ZeroMemory(&smartRule, sizeof(smartRule));
		BOOL wantSmartRule = FALSE;
		if (m_activeSmartId >= 0 && MpSmart_Get(m_activeSmartId, smartRule))
			wantSmartRule = TRUE;
		const BOOL wantQuick = (m_smartFilt == 1 || m_smartFilt == 2);
		if (wantKw || wantSmartRule || wantQuick) {
			if (m_fmapCap < pl->playcnt) {
				int* np = (int*)realloc(m_fmap, sizeof(int) * (size_t)pl->playcnt);
				if (np) { m_fmap = np; m_fmapCap = pl->playcnt; }
			}
			if (m_fmap && m_fmapCap >= pl->playcnt) {
				for (int i = 0; i < pl->playcnt; ++i) {
					const playlistdata0& it = pl->pc[i];
					bool hit = true;
					if (wantKw)
						hit = PlaylistSearchCtxMatch(searchCtx, it) ? true : false;
					if (hit && m_smartFilt == 2)
						hit = (m_miss && i < m_missCap && m_miss[i] == 1);
					if (hit && m_smartFilt == 1)
						hit = (ProAudio_GetPlayCount(MpCurListName(), it.fol, it.sub, it.ret2) <= 0);
					if (hit && wantSmartRule)
						hit = MpTrackMatchesSmart(this, i, smartRule) ? true : false;
					if (hit) m_fmap[m_fcnt++] = i;
				}
				m_filtOn = 1;
			}
		}
		PlaylistSearchCtxDestroy(searchCtx);
	}
	int cnt = m_filtOn ? m_fcnt : pl->playcnt;

	// 件数変化 or 強制(並べ替え/タグ更新/追加削除)時に範囲を再設定。
	const int prevCount = m_lastCount;
	if (bForce || cnt != m_lastCount) {
		if (bForce || (pl->playcnt != m_missCap)) {
			// 実体 PL が変わったら欠損フラグを再走査
			StopMissScan();
			m_missScan = 0;
		}
		int anchor = GetListScrollAnchor();
		m_list.SetItemCount(cnt);   // 仮想リスト: スクロール範囲を確定(pl と同じ仕組み)
		m_lastCount = cnt;
		if (cnt > 0 && !m_filtOn)
			RestoreListScrollAnchor(anchor);   // SetItemCount で先頭へ戻るのを防ぐ
		if (bForce) m_list.Invalidate(FALSE);   // 表示内容(順序/タグ)の変化を反映
	}

	// 再生中(♪)アイコンの点滅・移動を反映(該当行だけ再取得=GetDispInfo 再問合せ)。
	// Timer1(250ms)毎に無条件で RedrawItems すると、pl 側の点滅周期(1200ms)とずれて
	// 行が小刻みに再描画されちらつく(=ぎこちなく見える)。アイコン値が実際に
	// 変化したフレームだけ再描画して、pl 同様のなめらかな点滅にする。
	int pnt = pl->pnt;
	int pntDisp = pnt;
	if (m_filtOn && m_fmap) {
		pntDisp = -1;
		for (int j = 0; j < m_fcnt; ++j) if (m_fmap[j] == pnt) { pntDisp = j; break; }
	}
	// ♪ はジャケ右に描くため行左端ストリップを更新(NotifyPlayIconChanged と同範囲)
	auto redrawNoteStrip = [this](int d) {
		if (d < 0 || d >= m_list.GetItemCount()) return;
		CRect r;
		if (!m_list.GetItemRect(d, &r, LVIR_BOUNDS)) {
			m_list.RedrawItems(d, d);
			return;
		}
		r.right = r.left + (m_list.m_mpJacketPx > 0 ? m_list.m_mpJacketPx : kMpJakPx) + (int)(42 * hD2 + 0.5f);
		// UPDATENOW 禁止: スクロール中の Opaque 描画と競合し名前列だけ黒ちらつきする
		m_list.RedrawWindow(&r, NULL, RDW_INVALIDATE | RDW_NOERASE);
	};
	if (pntDisp != m_lastPlcnt) {
		if (m_lastPlcnt >= 0 && m_lastPlcnt < cnt) redrawNoteStrip(m_lastPlcnt);
		m_lastPlcnt = pntDisp;
		m_lastPlayIcon = -999;   // 行が変わったら次回必ず再描画
	}
	if (pntDisp >= 0 && pntDisp < cnt && pnt >= 0 && pnt < pl->playcnt) {
		int ic = pl->pc[pnt].icon;
		if (ic != m_lastPlayIcon) {
			redrawNoteStrip(pntDisp);
			m_lastPlayIcon = ic;
		}
	}

	FollowPlayingRow();   // ♪ 行へカーソル追従(曲変化時のみ)
	UpdateEmptyStateUi();
	// ディスク済ジャケは PL 変更/初回で一括メモリ化。未抽出のみ OnTimer で1件ずつ。
	if (bForce || cnt != prevCount)
		MpJacketLoadVisible(this, FALSE, TRUE);
	// 歌詞フラグ / チャンネル印はジャケTimerから外したので、ここ(低頻度)で可視分だけ Probe
	if (::IsWindow(m_list.GetSafeHwnd()) && pl && pl->pc) {
		const int nDisp = m_list.GetItemCount();
		int t = m_list.GetTopIndex();
		if (t < 0) t = 0;
		int pg = m_list.GetCountPerPage() + 2;
		if (pg < 4) pg = 4;
		BOOL markDirty = FALSE;
		int budget = 8;
		for (int disp = t; budget > 0 && disp < t + pg && disp < nDisp; ++disp) {
			const int pcIdx = MpDispToPc(this, disp);
			if (pcIdx < 0 || pcIdx >= pl->playcnt) continue;
			const TCHAR* path = pl->pc[pcIdx].fol;
			if (!path || !path[0]) continue;
			BOOL did = FALSE;
			if (PlLrcDiskGet(path) < 0) {
				PlLrcProbe(path);
				did = TRUE;
			}
			if (PlChDiskGet(path) < 0) {
				PlChProbe(path);
				did = TRUE;
			}
			if (did) {
				markDirty = TRUE;
				--budget;
			}
		}
		if (markDirty) {
			CRect r0, r1;
			if (m_list.GetItemRect(t, &r0, LVIR_BOUNDS)) {
				int last = t + pg - 1;
				if (last >= nDisp) last = nDisp - 1;
				if (m_list.GetItemRect(last, &r1, LVIR_BOUNDS))
					r0.bottom = r1.bottom;
				m_list.RedrawWindow(&r0, NULL, RDW_INVALIDATE | RDW_NOERASE);
			}
			if (pl && ::IsWindow(pl->m_lc.GetSafeHwnd()))
				pl->m_lc.Invalidate(FALSE);
		}
	}
}

void CMediaPlayerDlg::NotifyPlayIconChanged()
{
	// PlayList::SIconTimer から呼ばれる。Timer1(250ms)待ちだと点滅が間引かれて飛び飛びに見える。
	if (!::IsWindow(m_list.GetSafeHwnd()) || !pl || pl->pc == NULL) return;
	const int cnt = pl->playcnt;
	const int pnt = pl->pnt;
	if (pnt < 0 || pnt >= cnt) return;
	const int disp = MpPcToDisp(this, pnt);
	if (disp < 0) return; // フィルタ外の再生曲はリストに出ない
	const int ic = pl->pc[pnt].icon;
	const int nDisp = m_list.GetItemCount();
	// ♪ はジャケ右に描くため LVIR_ICON だけでは足りない。行左端〜ジャケ+音符を更新する。
	auto redrawNote = [this](int d) {
		if (d < 0 || d >= m_list.GetItemCount()) return;
		CRect r;
		if (!m_list.GetItemRect(d, &r, LVIR_BOUNDS)) {
			m_list.RedrawItems(d, d);
			return;
		}
		r.right = r.left + (m_list.m_mpJacketPx > 0 ? m_list.m_mpJacketPx : kMpJakPx) + (int)(42 * hD2 + 0.5f);
		// UPDATENOW 禁止(名前列の Opaque 描画と競合してちらつく)
		m_list.RedrawWindow(&r, NULL, RDW_INVALIDATE | RDW_NOERASE);
	};
	if (disp != m_lastPlcnt) {
		if (m_lastPlcnt >= 0 && m_lastPlcnt < nDisp)
			redrawNote(m_lastPlcnt);
		m_lastPlcnt = disp;
		m_lastPlayIcon = -999;
	}
	if (ic != m_lastPlayIcon) {
		redrawNote(disp);
		m_lastPlayIcon = ic;
	}
}

// 仮想リスト(LVS_OWNERDATA)の表示内容を pl->pc から供給する(pl の同名処理と同等)。
void CMediaPlayerDlg::OnGetdispinfoList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* di = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);
	*pResult = 0;
	if (di == NULL || !pl || pl->pc == NULL || pl->playcnt <= 0) return;
	int disp = di->item.iItem;
	int i = MpDispToPc(this, disp);
	if (i < 0 || i >= pl->playcnt) i = 0;
	const playlistdata0& d = pl->pc[i];
	if (di->item.mask & LVIF_TEXT) {
		switch (di->item.iSubItem) {
		case 1: {
			const int rt = ProAudio_GetRating(MpCurListName(), d.fol, d.sub, d.ret2);
			CString marks;
			if (rt > 0 && rt <= 5)
				marks.Format(_T("★%d"), rt);
			_tcsncpy_s(di->item.pszText, di->item.cchTextMax, marks, _TRUNCATE);
		} break;
		case 0: {
			TCHAR buf[1100];
			buf[0] = 0;
			if (m_miss && i < m_missCap && m_miss[i] == 1)
				_tcscpy_s(buf, _T("⚠ "));
			{
				CString marks;
				PlFormatRowMarks(i, d.fol, marks);
				if (!marks.IsEmpty()) {
					_tcscat_s(buf, marks);
					_tcscat_s(buf, _T(" "));
				}
			}
			_tcscat_s(buf, d.name);
			_tcsncpy_s(di->item.pszText, di->item.cchTextMax, buf, _TRUNCATE);
		} break;
		case 2: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.game); break;
		case 3: {
			CString s;
			if (d.time == 0) s = _T("");
			else if (d.time == -1) s = LL14(L"取得不能", L"N/A", L"N/D", L"N/D", L"N/D", L"해당 없음", L"不可用", L"غ/م", L"Н/Д", L"k. A.", L"N/D", L"N.v.t.", L"Brak", L"Yok");
			else if (d.time >= 3600) s.Format(_T("%d:%02d:%02d"), d.time / 3600, (d.time / 60) % 60, d.time % 60);
			else s.Format(_T("%d:%02d"), d.time / 60, d.time % 60);
			_tcsncpy_s(di->item.pszText, di->item.cchTextMax, s, _TRUNCATE);
		} break;
		case 4: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.art); break;
		case 5: _tcscpy_s(di->item.pszText, di->item.cchTextMax, d.alb); break;
		default: break;
		}
	}
	if (di->item.mask & LVIF_IMAGE)
		di->item.iImage = 1; // 空: 既定ILの♪先描きを抑止(自前CDで描く)
}

void CMediaPlayerDlg::OnListItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (plf) return;   // 再生中は表示を差し替えない
	const NMLISTVIEW* p = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if (!p || !(p->uChanged & LVIF_STATE)) return;
	if (!((p->uNewState ^ p->uOldState) & LVIS_SELECTED)) return;
	if (!(p->uNewState & LVIS_SELECTED)) return;
	const int disp = p->iItem;
	const int i = MpDispToPc(this, disp);
	if (!pl || i < 0 || i >= pl->playcnt) return;
	// pl->Get は SIcon で♪(pnt)を選択行へ移すため呼ばない。
	// 次に再生する候補(plcnt)と、追従抑止用の pnt1 だけ更新する。
	plcnt = i;
	pl->pnt1 = i;
	m_lastScroll = disp;
}

int CMediaPlayerDlg::GetListScrollAnchor() const
{
	if (!pl || pl->playcnt <= 0) return 0;
	if (pl->pnt1 >= 0 && pl->pnt1 < pl->playcnt) return pl->pnt1;
	if (plcnt >= 0 && plcnt < pl->playcnt) return plcnt;
	if (::IsWindow(m_list.GetSafeHwnd())) {
		int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
		if (sel >= 0 && sel < pl->playcnt) return sel;
	}
	if (::IsWindow(pl->m_lc.GetSafeHwnd())) {
		int sel = pl->m_lc.GetNextItem(-1, LVNI_SELECTED);
		if (sel >= 0 && sel < pl->playcnt) return sel;
		int top = pl->m_lc.GetTopIndex();
		if (top >= 0 && top < pl->playcnt) return top;
	}
	if (pl->pnt >= 0 && pl->pnt < pl->playcnt) return pl->pnt;
	return 0;
}

int CMediaPlayerDlg::GetSelectedPcIndex() const
{
	if (!pl || pl->playcnt <= 0) return -1;
	if (::IsWindow(m_list.GetSafeHwnd())) {
		const int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
		if (sel >= 0) {
			const int pc = MpDispToPc(const_cast<CMediaPlayerDlg*>(this), sel);
			if (pc >= 0 && pc < pl->playcnt) return pc;
		}
	}
	if (pl->pnt1 >= 0 && pl->pnt1 < pl->playcnt) return pl->pnt1;
	if (plcnt >= 0 && plcnt < pl->playcnt) return plcnt;
	if (pl->pnt >= 0 && pl->pnt < pl->playcnt) return pl->pnt;
	return 0;
}

void CMediaPlayerDlg::RestoreListScrollAnchor(int anchor)
{
	if (!::IsWindow(m_list.GetSafeHwnd()) || !pl || pl->playcnt <= 0) return;
	if (anchor < 0) anchor = 0;
	if (anchor >= pl->playcnt) anchor = pl->playcnt - 1;
	for (int k = -1; (k = m_list.GetNextItem(k, LVNI_SELECTED)) != -1; )
		m_list.SetItemState(k, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.SetItemState(anchor, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.EnsureVisible(anchor, FALSE);
	m_lastScroll = anchor;   // FollowPlayingRow が直後の選択を上書きしない
}

void CMediaPlayerDlg::InitListScrollPosition()
{
	if (!::IsWindow(m_list.GetSafeHwnd()) || !pl || pl->playcnt <= 0) return;
	int anchor = GetListScrollAnchor();
	RestoreListScrollAnchor(anchor);
	// FollowPlayingRow の初回強制追従を抑え、♪行が変わった時だけ追従する
	if (pl->pnt >= 0 && pl->pnt < pl->playcnt) {
		m_lastScroll = pl->pnt;
		m_lastFollowPnt = pl->pnt;
	}
	else
		m_lastScroll = anchor;
}

// 再生中(♪)の行へカーソル(選択)を移動して可視化する。
// ♪ の行は pl->pnt(SIcon が pc[pnt].icon を再生中アイコンへ切替えている)。
// 項目挿入後に呼ぶこと。pnt が変わった時のみ追従し、同一曲中のユーザー選択は邪魔しない。
// 再生中(♪)行へスクロールして選択する。pl->pnt が変化した場合のみ動作する。
// 起動時の位置復元は InitListScrollPosition() を使う。
void CMediaPlayerDlg::FollowPlayingRow()
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	if (!pl || pl->pc == NULL) return;
	int cnt = m_filtOn ? m_fcnt : pl->playcnt;
	int play = pl->pnt;
	if (play < 0 || play >= pl->playcnt) return;
	// 再生曲(pnt)が変わったときだけ追従。選択行変更では動かさない
	// (旧: m_lastScroll/pnt1 判定だと選択のたび♪行へ吸い付く／Get→SIconで♪が飛ぶ原因だった)
	if (play == m_lastFollowPnt) return;
	m_lastFollowPnt = play;
	pl->pnt1 = -1;
	int playDisp = play;
	if (m_filtOn && m_fmap) {
		playDisp = -1;
		for (int j = 0; j < m_fcnt; ++j) if (m_fmap[j] == play) { playDisp = j; break; }
		if (playDisp < 0) return; // フィルタ外の再生曲は追従しない
	}
	if (m_list.GetItemCount() < cnt) return;   // 項目未挿入なら何もしない
	int k = -1;
	while ((k = m_list.GetNextItem(k, LVNI_SELECTED)) != -1)
		m_list.SetItemState(k, 0, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.SetItemState(playDisp, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	m_list.EnsureVisible(playDisp, FALSE);
	m_lastScroll = playDisp;
}

// m_mpBtnShort と ps に応じて一時停止/再開ラベルを設定する。
// og->m_ps のフル文言をそのままミラーすると短縮段階が潰れるため、こちらで段階別文言を選ぶ。
void CMediaPlayerDlg::ApplyPauseButtonLabel()
{
	if (!::IsWindow(m_pause.GetSafeHwnd())) return;
	CString text;
	if (ps == 1) {
		if (m_mpBtnShort >= 3)
			text = LL14(L"▶", L"▶", L"▶", L"▶", L"▶", L"▶", L"▶", L"▶", L"▶", L"▶", L"▶", L"▶", L"▶", L"▶");
		else
			text = LL14(L"▶ 再開", L"▶ Resume", L"▶ Reprendre", L"▶ Riprendi", L"▶ Reanudar", L"▶ 재개", L"▶ 恢复", L"▶ استئناف", L"▶ Продолжить", L"▶ Fortsetzen", L"▶ Retomar", L"▶ Hervatten", L"▶ Wznów", L"▶ Sürdür");
	}
	else if (m_mpBtnShort >= 3) {
		text = LL14(L"⏸", L"⏸", L"⏸", L"⏸", L"⏸", L"⏸", L"⏸", L"⏸", L"⏸", L"⏸", L"⏸", L"⏸", L"⏸", L"⏸");
	}
	else if (m_mpBtnShort >= 2) {
		text = LL14(L"⏸ 一時停", L"⏸ Pause", L"⏸ Pause", L"⏸ Pausa", L"⏸ Pausa", L"⏸ 일시정", L"⏸ 暂停", L"⏸ إيقاف", L"⏸ Пауза", L"⏸ Pause", L"⏸ Pausa", L"⏸ Pauze", L"⏸ Pauza", L"⏸ Duraklat");
	}
	else {
		text = LL14(L"⏸ 一時停止", L"⏸ Pause", L"⏸ Pause", L"⏸ Pausa", L"⏸ Pausa", L"⏸ 일시정지", L"⏸ 暂停", L"⏸ إيقاف مؤقت", L"⏸ Пауза", L"⏸ Pause", L"⏸ Pausar", L"⏸ Pauze", L"⏸ Pauza", L"⏸ Duraklat");
	}
	CString cur;
	m_pause.GetWindowText(cur);
	if (cur != text) {
		m_pause.SetWindowText(text);
		m_pause.RepaintClient();
	}
}

// og/pl の UI 状態(歌詞・スライダー位置・チェック状態・コンボ選択)をこの画面へ反映する。
// 差分のみ SetWindowText / SetCheck するのはちらつき防止のため。
// Timer1(250ms)から定期呼び出しされるほか、コントロール操作直後にも都度呼ぶ。
void CMediaPlayerDlg::SyncFromMain()
{
	if (!::IsWindow(GetSafeHwnd())) return;

	// タイトル/アーティスト/アルバムはバナーGDIに表示されるためここでは更新しない。

	// 歌詞(5行) / 拡大時カラオケビュー / OS / CPU
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		CString s, s2;
		const bool hasLyrics = (og->lrcnum >= 2);
		const bool lrcScroll = (savedata.mpLrcExpand && m_lrcView.GetSafeHwnd());
		extern UINT ttt;
		if (lrcScroll) {
			if (hasLyrics) {
				const int n = (og->lrcnum > 1) ? (og->lrcnum - 1) : 0;
				m_lrcView.SetLines(og->lrc, n, og->lrctm, og->lrcnum);
				extern double OggGetGdiPlaybackTimeSec();
				extern int mode;
				extern int videoonly;
				DWORD centis = ttt;
				if (!(mode == -2 || videoonly)) {
					const double sec = OggGetGdiPlaybackTimeSec();
					if (sec >= 0.0)
						centis = (DWORD)(sec * 100.0 + 0.5);
				}
				m_lrcView.SetPlayCentis(centis);
			} else {
				m_lrcView.Clear();
			}
		}
		else {
			og->m_lrc.GetWindowText(s); m_lrc.GetWindowText(s2); if (s != s2) m_lrc.SetWindowText(s);
			og->m_lrc2.GetWindowText(s); m_lrc2.GetWindowText(s2); if (s != s2) m_lrc2.SetWindowText(s);
			// 現在行(3行目): 背景+文字色でハイライト
			if (hasLyrics) {
				CString cur = og->lrc[og->lrccur];
				CString hi;
				hi.Format(_T("!@C1E46AA%s"), (LPCTSTR)cur);
				m_lrc3.GetWindowText(s2);
				if (hi != s2) m_lrc3.SetWindowText(hi);
				m_lrc3.SetSolidFill(TRUE, RGB(220, 232, 255));
				og->m_lrc4.GetWindowText(s); m_lrc4.GetWindowText(s2); if (s != s2) m_lrc4.SetWindowText(s);
				og->m_lrc5.GetWindowText(s); m_lrc5.GetWindowText(s2); if (s != s2) m_lrc5.SetWindowText(s);
			} else {
				og->m_lrc3.GetWindowText(s); m_lrc3.GetWindowText(s2); if (s != s2) m_lrc3.SetWindowText(s);
				m_lrc3.SetSolidFill(FALSE, 0);
			}
		}
		if (!hasLyrics) {
			og->m_OS.GetWindowText(s); m_os.GetWindowText(s2); if (s != s2) m_os.SetWindowText(s);
			og->m_cpu.GetWindowText(s); m_cpu.GetWindowText(s2); if (s != s2) m_cpu.SetWindowText(s);
			og->m_os3.GetWindowText(s); m_os3.GetWindowText(s2); if (s != s2) m_os3.SetWindowText(s);
		}
		// 拡大歌詞の有無に関係なく歌詞ウィンドウへ同期（以前は mpLrcExpand 時のみだった）
		SyncDesktopLyricsIfOpen();
		static int s_lastLyricsMode = -1;
		const int lyricsMode = hasLyrics ? 1 : 0;
		static int s_lastLrcExpand = -1;
		if (s_lastLyricsMode != lyricsMode || s_lastLrcExpand != savedata.mpLrcExpand) {
			s_lastLyricsMode = lyricsMode;
			s_lastLrcExpand = savedata.mpLrcExpand;
			DoLayout();
			CCC_GroupBoxesBack(GetSafeHwnd());
			RefreshListAfterLayout();
			RedrawWindow(NULL, NULL,
				RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		}
		// LRC 状態バッジ — 幅が狭いので短い記号のみ（意味は Tip）
		if (m_lrcBadge.GetSafeHwnd()) {
			CString badge;
			if (hasLyrics)
				badge = _T("LRC●");
			else if (savedata.lrc_net)
				badge = _T("net");
			else
				badge = _T("—");
			CString curB; m_lrcBadge.GetWindowText(curB);
			if (curB != badge) m_lrcBadge.SetWindowText(badge);
		}

		// シーク/音量は timerp → MirrorSeekVol が駆動。ここは二重になるので呼ばない。

		// サウンド調整(DS音量/拡張/テンポ/ピッチ)を og からミラー。ドラッグ中のものは触らない。
		CWnd* pf2 = GetFocus();
		HWND hf = pf2 ? pf2->GetSafeHwnd() : NULL;
		if (hf != m_dsvol.GetSafeHwnd() && m_dsvol.GetPos() != og->m_dsval.GetPos())
			m_dsvol.SetPos(og->m_dsval.GetPos(), FALSE);
		if (hf != m_kvol.GetSafeHwnd() && m_kvol.GetPos() != og->m_kakuVol.GetPos())
			m_kvol.SetPos(og->m_kakuVol.GetPos(), FALSE);
		if (hf != m_tempo.GetSafeHwnd() && m_tempo.GetPos() != og->m_tempo_sl.GetPos())
			m_tempo.SetPos(og->m_tempo_sl.GetPos(), FALSE);
		if (hf != m_pitch.GetSafeHwnd() && m_pitch.GetPos() != og->m_pitch_sl.GetPos())
			m_pitch.SetPos(og->m_pitch_sl.GetPos(), FALSE);
		CString l;
		double dsp = (og->m_dsval.GetPos() + 499) * 2.0 / 10.0;
		CString dsLbl = (m_dsvolSlW >= (int)(92 * hD2))
			? LL14(L"DirectSound音量", L"DirectSound volume", L"Volume DirectSound", L"Volume DirectSound", L"Volumen DirectSound", L"DirectSound 음량", L"DirectSound音量", L"صوت DirectSound", L"DirectSound", L"DirectSound-Lautstarke", L"Volume DirectSound", L"DirectSound-volume", L"Głośność DirectSound", L"DirectSound sesi")
			: LL14(L"DS音量", L"DS volume", L"Volume DS", L"Volume DS", L"Volumen DS", L"DS 음량", L"DS音量", L"مستوى DS", L"Громкость DS", L"DS-Lautstarke", L"Volume DS", L"DS-volume", L"Głośność DS", L"DS sesi");
		l.Format(_T("!@C606868%s!@C206088 %.1f%%"), (LPCTSTR)dsLbl, dsp); m_dsvolL.GetWindowText(s2); if (l != s2) m_dsvolL.SetWindowText(l);
		{
			CString lbl = LL14(L"拡張音量", L"Extended volume", L"Volume etendu", L"Volume esteso", L"Volumen extendido", L"확장 음량", L"扩展音量", L"الصوت الموسع", L"Расшир. громкость", L"Erweiterte Lautstarke", L"Volume estendido", L"Uitgebreid volume", L"Rozszerzona głośność", L"Genisletilmis ses");
			l.Format(_T("!@C606868%s!@C904820 %.1f%%"), (LPCTSTR)lbl, (double)og->m_kakuVol.GetPos());
		}
		m_kvolL.GetWindowText(s2); if (l != s2) m_kvolL.SetWindowText(l);
		{
			CString lbl = LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"템포", L"速度", L"الإيقاع", L"Темп", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo");
			l.Format(_T("!@C606868%s!@C186878 %d%%"), (LPCTSTR)lbl, (int)TempoPercentFromPos(og->m_tempo_sl.GetPos()));
		}
		m_tempoL.GetWindowText(s2); if (l != s2) m_tempoL.SetWindowText(l);
		{
			CString lbl = LL14(L"ピッチ", L"Pitch", L"Hauteur", L"Altezza", L"Tono", L"피치", L"音高", L"طبقة الصوت", L"Высота", L"Tonhohe", L"Tom", L"Toonhoogte", L"Wysokość", L"Perde");
			l.Format(_T("!@C606868%s!@C704878 %d%%"), (LPCTSTR)lbl, (int)TempoPercentFromPos(og->m_pitch_sl.GetPos()));
		}
		m_pitchL.GetWindowText(s2); if (l != s2) m_pitchL.SetWindowText(l);

		// 乱数/順次・スペアナ/ステレオ/EQ/簡易ピアノロールの押下見た目
		int v1;
		v1 = og->m_random.GetCheck() ? 1 : 0; if (m_random.GetCheck() != v1) m_random.SetCheck(v1);
		SyncPushToggleButtons();

		// ジャケット(ボタン/ミニジャケクリック)はデータがあるときのみ有効
		BOOL hasJacket = (og->jx > 0 && !og->img.IsNull());
		if (m_jacket.IsWindowEnabled() != hasJacket)
			m_jacket.EnableWindow(hasJacket);

		// 一時停止/再開ボタン表記(短縮段階を維持)
		ApplyPauseButtonLabel();

		v1 = og->m_c2.GetCheck() ? 1 : 0;
		if (m_savewav.GetCheck() != v1) m_savewav.SetCheck(v1);

		v1 = savedata.mic_mix ? 1 : 0;
		if (m_micMeter.GetSafeHwnd()) {
			// OFF 時は 0。ON でもキャプチャ未稼働ならピークは 0 のまま。
			if (v1)
				m_micMeter.SetLevel(MpMicPeakLevel());
			else
				m_micMeter.SetLevel(0);
		}
		if (m_micmix.GetSafeHwnd() && m_micmix.GetCheck() != v1) m_micmix.SetCheck(v1);
		if (m_miclev.GetSafeHwnd() && GetFocus() != (CWnd*)&m_miclev) {
			int lv = savedata.mic_mix_level;
			if (lv < 0) lv = 0;
			if (lv > 200) lv = 200;
			// UpdateWindow/親Invalidate するとアクリル全面再描画が走り重い
			if (m_miclev.GetPos() != lv) m_miclev.SetPos(lv, FALSE);
		}

		v1 = savedata.saveSongParams ? 1 : 0;
		if (m_saveparam.GetSafeHwnd() && m_saveparam.GetCheck() != v1) m_saveparam.SetCheck(v1);

		if (GetFocus() != (CWnd*)&m_kaisuu) {
			og->m_kaisuu.GetWindowText(s);
			m_kaisuu.GetWindowText(s2);
			if (s != s2) m_kaisuu.SetWindowText(s);
		}
	}

	if (pl && ::IsWindow(pl->GetSafeHwnd())) {
		int v2;
		v2 = pl->m_renzoku.GetCheck() ? 1 : 0; if (m_renzoku.GetCheck() != v2) m_renzoku.SetCheck(v2);
		v2 = pl->m_loop.GetCheck() ? 1 : 0; if (m_loop.GetCheck() != v2) m_loop.SetCheck(v2);
		v2 = pl->m_tool.GetCheck() ? 1 : 0;
		if (m_tip.GetCheck() != v2) { m_tip.SetCheck(v2); ApplyListTooltipState(); }
		v2 = pl->m_saisyo.GetCheck() ? 1 : 0; if (m_mini.GetCheck() != v2) m_mini.SetCheck(v2);
		v2 = pl->m_save_mp3.GetCheck() ? 1 : 0; if (m_savemp3.GetCheck() != v2) m_savemp3.SetCheck(v2);
		v2 = pl->m_save_kpi.GetCheck() ? 1 : 0; if (m_saveds.GetCheck() != v2) m_saveds.SetCheck(v2);
		// プレイリスト一覧の増減や選択変更を反映。
		// ただしユーザーがコンボを操作中(ドロップダウン展開中/フォーカス中)は
		// SetCurSel で選択を奪わない。さもないと「2を選んでも1に戻る」不具合になる。
		if (::IsWindow(pl->m_listchange.GetSafeHwnd())) {
			BOOL busy = m_plsel.GetDroppedState() ||
				(GetFocus() == (CWnd*)&m_plsel);
			int n = pl->m_listchange.GetCount();
			if (n != m_lastComboCount) {
				if (!busy) ReloadPlaylistCombo();
			}
			else if (!busy && m_plsel.GetCurSel() != savedata.playlistnum)
				m_plsel.SetCurSel(savedata.playlistnum);
		}
	}

	// サイドパネル(左ジャケ/右曲情報)の表示内容が変わったら再描画。
	// 毎フレーム描画はせず、曲(タイトル/アーティスト/アルバム/ジャケ)変化時のみ更新。
	if (!m_jacketRect.IsRectEmpty() || !m_infoPanelRect.IsRectEmpty()) {
		CString fmt; if (::IsWindow(m_os.GetSafeHwnd())) m_os.GetWindowText(fmt);
		CString key;
		key.Format(_T("%s\x01%s\x01%s\x01%s\x01%s\x01%d\x01%d\x01%d\x01%d\x01%d\x01%d\x01%d"),
			(LPCTSTR)CurrentTrackTitle(), (LPCTSTR)tagname, (LPCTSTR)tagalbum,
			(LPCTSTR)tagtrack, (LPCTSTR)fmt, og ? og->jx : -1,
			g_pcm_upscale_active, wavbit_sample_Hz, wavchannel, wavsam_depth,
			g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits);
		if (key != m_lastBannerKey) {
			m_lastBannerKey = key;
			extern volatile LONG g_xfInProgress, g_xfOpening;
			extern ULONGLONG g_xfJacketStableUntil;
			/* xfade 開始(SoftOpenでHz等が一瞬変わる)／終了(昇格で jx/タグ更新)では
			 * 暗いトラックフェード＋ジャケ Invalidate を抑止（開始・終了の数回点滅の主因） */
			const bool xfBusy =
				InterlockedCompareExchange(&g_xfInProgress, 0, 0) != 0
				|| InterlockedCompareExchange(&g_xfOpening, 0, 0) != 0
				|| (g_xfJacketStableUntil != 0 && GetTickCount64() < g_xfJacketStableUntil);
			if (xfBusy) {
				ResetInfoScroll();
				if (!m_infoPanelRect.IsRectEmpty())
					InvalidateRect(&m_infoPanelRect, FALSE);
			}
			else {
				m_trackFadeStart = GetTickCount();
				ResetInfoScroll();
				InvalidateSidePanels();
			}
		}
	}
}

void CMediaPlayerDlg::EnforceFalcomHidden()
{
	if (savedata.playerMode != 1) return;
	extern CImageBase* maini;
	extern CImageBase* playbase;
	if (og && ::IsWindow(og->GetSafeHwnd()) && ::IsWindowVisible(og->m_hWnd))
		::ShowWindow(og->m_hWnd, SW_HIDE);
	if (pl && ::IsWindow(pl->GetSafeHwnd()) && ::IsWindowVisible(pl->m_hWnd))
		::ShowWindow(pl->m_hWnd, SW_HIDE);
	if (maini && ::IsWindow(maini->GetSafeHwnd()) && ::IsWindowVisible(maini->m_hWnd))
		::ShowWindow(maini->m_hWnd, SW_HIDE);
	if (playbase && ::IsWindow(playbase->GetSafeHwnd()) && ::IsWindowVisible(playbase->m_hWnd))
		::ShowWindow(playbase->m_hWnd, SW_HIDE);
}

// 再生位置・時間表示・音量を og からミラーする高速ミラー関数。
// Timer3(100ms)と SyncFromMain から呼ばれる。og の timerp が playb(再生位置)を
// SetPos するため、それに合わせて m_seek と m_time を追従させる。
// タスクバー進捗(ITaskbarList3)も og ではなく mp のウィンドウに対して設定する。
void CMediaPlayerDlg::MirrorSeekVol()
{
	if (!og || !::IsWindow(og->GetSafeHwnd()) || !::IsWindow(GetSafeHwnd())) return;
	CString s2;
	// シーク(og->m_time=範囲スライダー。timerp が playb を SetPos するのでそれに追従)
	if (!m_seekDragging) {
		int mn = og->m_time.GetMinValue();
		int mx = og->m_time.GetMaxValue();
		if (mx <= mn) mx = mn + 1;
		int psPos = og->m_time.GetPos();
		// シーク直後: decode/timerp が1〜数フレ旧位置を返す間は確定位置を維持（一瞬戻るのを防ぐ）
		if (m_seekHoldUntil != 0) {
			const ULONGLONG now = GetTickCount64();
			int eps = (mx - mn) / 200;
			if (eps < 1) eps = 1;
			if (abs(psPos - m_seekHoldPos) <= eps || now >= m_seekHoldUntil)
				m_seekHoldUntil = 0;
			else
				psPos = m_seekHoldPos;
		}
		int selMn, selMx; og->m_time.GetSelection(selMn, selMx);
		// A-B 有効時は巻き戻しのみ（緑帯はループのまま。A-Bは別つまみ/帯）
		if (m_abApos >= 0 && m_abBpos > m_abApos) {
			// ユーザーがシーク操作中は巻き戻さない（離した直後の Mirror で旧位置へ戻る温床）
			if (!m_seekDragging && !m_abWrapBusy && plf && ps != 1 && psPos >= m_abBpos) {
				m_abWrapBusy = 1;
				m_abLoopCount++;
				og->m_time.SetPos(m_abApos);
				og->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, m_abApos), (LPARAM)og->m_time.GetSafeHwnd());
				psPos = m_abApos;
				m_abWrapBusy = 0;
			}
		}
		// 一括更新+見た目変化時のみ Invalidate。ループ選択と A-B を分離して渡す。
		m_seek.SetPlaybackMirror(psPos, selMn, selMx, mn, mx, m_abApos, m_abBpos);
		// 開いているツールメニューのシーク／ループ／A-B も再生に追従
		if (CCustomPopupMenu* pop = CCustomPopupMenu::GetTrackingRoot())
			pop->LiveMirrorRange(0x00E01001u, psPos, selMn, selMx, mn, mx, m_abApos, m_abBpos);
		// MP3 等はスライダーが frames/100。動画系(aa*100)は centisec。それ以外は PCM フレーム。
		{
			int tb = wavbit_sample_Hz > 0 ? wavbit_sample_Hz : 44100;
			if (mode == -10)
				tb = max(1, tb / 100);
			else if (mode == -2 || videoonly)
				tb = 100;
			m_seek.SetTimeBaseHz(tb);
		}
		m_seek.SetBeatGrid(savedata.mpDetectedBpm > 0 ? (float)savedata.mpDetectedBpm : 120.f,
			savedata.mpBeatGrid ? TRUE : FALSE, savedata.mpBeatGridOffsetMs);
		{
			int xms = 0;
			if (savedata.mpXfadePreview)
				xms = (savedata.wav_export_xfade_sec > 0 ? savedata.wav_export_xfade_sec : 5) * 1000;
			m_seek.SetXfadePreviewMs(xms);
		}
		KickWaveOverview();
		RefreshSeekCues();
		{
			float bins[64];
			float peak = 1.f;
			for (int i = 0; i < 64; ++i) {
				float v = (float)spelv[i];
				if (v > peak) peak = v;
			}
			if (peak < 1.f) peak = 1.f;
			for (int i = 0; i < 64; ++i)
				bins[i] = (float)spelv[i] / peak;
			m_seek.SetMeterRibbon(bins, 64);
			// #1: PCM ライブピーク優先(スペアナOFFでも埋まる)。WAVは後からフル概観で置換
			if (savedata.mpSeekWave && plf) {
				float amp = ProAudio_LivePeak();
				if (amp < 0.02f && peak > 1.f) {
					float a = peak / 96.f;
					if (a > 1.f) a = 1.f;
					amp = a;
				}
				m_seek.AccumulateWaveAtPos(psPos, amp, 512);
			}
		}
		double pct = (double)(psPos - mn) * 100.0 / (double)(mx - mn);
		if (pct < 0.0) pct = 0.0; if (pct > 100.0) pct = 100.0;
		// m_time は ~42px。L%d を押し込むと DrawSmartText で潰れる → % のみ、周回は Tip へ。
		CString t;
		t.Format(_T("!@C206830%.1f%%"), pct);
		m_time.GetWindowText(s2); if (t != s2) m_time.SetWindowText(t);
		if (m_tooltip.GetSafeHwnd()) {
			CString tip;
			if (m_abApos >= 0 && m_abBpos > m_abApos && m_abLoopCount > 0)
				tip.Format(_T("%.1f%%  A-B L%d"), pct, m_abLoopCount);
			else
				tip.Format(_T("%.1f%%"), pct);
			m_tooltip.UpdateTipText(tip, &m_time);
		}

		// #6: ジャケット残時間リングを進捗に合わせて更新(1%刻みで Invalidate)
		if (savedata.mpJacketRemOverlay && !m_jacketRect.IsRectEmpty() && plf) {
			extern ULONGLONG g_xfJacketStableUntil;
			if (!(g_xfJacketStableUntil != 0 && GetTickCount64() < g_xfJacketStableUntil)) {
				const int bucket = (int)(pct + 0.5);
				if (bucket != m_jacketRemBucket) {
					m_jacketRemBucket = bucket;
					/* Invalidate せず直接提示（黒→描画の点滅を避ける） */
					CClientDC dc(this);
					PresentJacketCached(&dc);
				}
			}
		}

		// タスクバー進捗。og は非表示なのでこの MP 画面 HWND に設定。
		// 再生=緑 / 一時停止=黄 / 読み込み=不定 / 停止=消す
		if (ptl) {
			extern int g_oggKpiLoading;
			if (g_oggKpiLoading) {
				ptl->SetProgressState(m_hWnd, TBPF_INDETERMINATE);
			}
			else if (plf && mx > mn) {
				ptl->SetProgressState(m_hWnd, (ps == 1) ? TBPF_PAUSED : TBPF_NORMAL);
				ptl->SetProgressValue(m_hWnd, (ULONGLONG)(psPos - mn), (ULONGLONG)(mx - mn));
			}
			else {
				ptl->SetProgressState(m_hWnd, TBPF_NOPROGRESS);
			}
		}
	}
	// 音量(og->m_sl 0..100000 を 0..100 でミラー)
	int v = og->m_sl.GetPos() / 1000;
	if (v < 0) v = 0; if (v > 100) v = 100;
	CWnd* pf = GetFocus();
	if (!(pf && pf->GetSafeHwnd() == m_vol.GetSafeHwnd()) && m_vol.GetPos() != v)
		m_vol.SetPos(v, FALSE);
	double vpct = (double)og->m_sl.GetPos() / 1000.0;
	if (!og->deve) vpct *= 100.0;
	CString vs; vs.Format(_T("!@C206830%.1f%%"), vpct); m_volval.GetWindowText(s2); if (vs != s2) m_volval.SetWindowText(vs);
}

void CMediaPlayerDlg::SavePos()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	if (IsIconic()) return;
	RECT r; GetWindowRect(&r);
	int x = r.left, y = r.top;
	int w = r.right - r.left, h = r.bottom - r.top;
	CCC_ClampWindowPos(x, y, w, h);
	if (x != r.left || y != r.top)
		::SetWindowPos(m_hWnd, NULL, x, y, 0, 0,
			SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	savedata.mpx = x;
	savedata.mpy = y;
	savedata.mpw = w;
	savedata.mph = h;
	savedata.mpHasPos = 1;
	// リストの列幅も保存(最終列と★は起動時フィット/既定のため意味スロット 0..3 のみ)
	// mpcol: [0]=名前 [1]=ゲーム [2]=時間 [3]=アーティスト (列index 0,2,3,4)
	if (::IsWindow(m_list.GetSafeHwnd())) {
		int w = m_list.GetColumnWidth(0);
		if (w > 0) savedata.mpcol[0] = w;
		w = m_list.GetColumnWidth(2);
		if (w > 0) savedata.mpcol[1] = w;
		w = m_list.GetColumnWidth(3);
		if (w > 0) savedata.mpcol[2] = w;
		w = m_list.GetColumnWidth(4);
		if (w > 0) savedata.mpcol[3] = w;
	}
}

// 欠損走査スナップショット(UIスレッドで確保→ワーカが解放/結果ごと Post)
struct MpMissJob {
	HWND hwnd;
	LONG gen;
	int count;       // 走査対象件数(全曲ではなく要チェック分)
	int* indices;    // pc インデックス
	int* subs;
	TCHAR* fols;     // count * 1024
	char* result;
};

static UINT AFX_CDECL MpMissScanThread(LPVOID p)
{
	MpMissJob* job = (MpMissJob*)p;
	if (!job) return 0;
	for (int i = 0; i < job->count; ++i) {
		const TCHAR* fol = job->fols + (size_t)i * 1024;
		// ディスクキャッシュ読込もワーカ側(UIで全曲 CreateFile しない)
		const int cached = PlMissDiskGet(fol);
		if (cached == 0) {
			job->result[i] = 0;
			continue;
		}
		job->result[i] = PlTrackLooksMissing(job->subs[i], fol) ? 1 : 0;
		PlMissDiskSet(fol, job->result[i] ? 1 : 0);
	}
	if (job->hwnd && ::IsWindow(job->hwnd))
		::PostMessage(job->hwnd, WM_MP_MISS_DONE, (WPARAM)job->gen, (LPARAM)job);
	else {
		free(job->indices);
		free(job->subs);
		free(job->fols);
		free(job->result);
		free(job);
	}
	return 0;
}

void CMediaPlayerDlg::StopMissScan()
{
	InterlockedIncrement(&m_missGen);
	InterlockedIncrement(&m_jakGen);
	m_jakPend[0] = 0;
	// 稼働中スレッドは古い gen の結果を OnMissScanDone / OnJakLoadDone で破棄する
}

void CMediaPlayerDlg::KickMissScan()
{
	if (!::IsWindow(GetSafeHwnd()) || !pl || !pl->pc || pl->playcnt <= 0)
		return;

	const int n = pl->playcnt;
	if (m_missCap < n) {
		char* np = (char*)realloc(m_miss, (size_t)n);
		if (!np) return;
		if (m_missCap > 0)
			memset(np + m_missCap, 0, (size_t)(n - m_missCap));
		else
			memset(np, 0, (size_t)n);
		m_miss = np;
		m_missCap = n;
		m_missScan = 0; // 未完了
	}
	else if (m_missCap > n) {
		m_missCap = n;
		m_missScan = 0;
	}

	// 既に完了済み、または走査中なら何もしない
	if (m_missScan >= n) return;
	if (InterlockedCompareExchange(&m_missBusy, 1, 0) != 0) return;

	// UI では PlMissDiskGet しない。全曲スナップショットをワーカへ渡す。
	MpMissJob* job = (MpMissJob*)malloc(sizeof(MpMissJob));
	if (!job) {
		InterlockedExchange(&m_missBusy, 0);
		return;
	}
	ZeroMemory(job, sizeof(*job));
	job->hwnd = m_hWnd;
	job->gen = InterlockedIncrement(&m_missGen);
	job->count = n;
	job->indices = (int*)malloc(sizeof(int) * (size_t)n);
	job->subs = (int*)malloc(sizeof(int) * (size_t)n);
	job->fols = (TCHAR*)malloc(sizeof(TCHAR) * 1024 * (size_t)n);
	job->result = (char*)malloc((size_t)n);
	if (!job->indices || !job->subs || !job->fols || !job->result) {
		free(job->indices); free(job->subs); free(job->fols); free(job->result); free(job);
		InterlockedExchange(&m_missBusy, 0);
		return;
	}
	for (int i = 0; i < n; ++i) {
		job->indices[i] = i;
		job->subs[i] = pl->pc[i].sub;
		_tcsncpy_s(job->fols + (size_t)i * 1024, 1024, pl->pc[i].fol, _TRUNCATE);
	}
	if (!AfxBeginThread(MpMissScanThread, job, THREAD_PRIORITY_BELOW_NORMAL)) {
		free(job->indices); free(job->subs); free(job->fols); free(job->result); free(job);
		InterlockedExchange(&m_missBusy, 0);
	}
}

LRESULT CMediaPlayerDlg::OnMissScanDone(WPARAM wParam, LPARAM lParam)
{
	MpMissJob* job = (MpMissJob*)lParam;
	InterlockedExchange(&m_missBusy, 0);
	if (!job) return 0;
	const LONG gen = (LONG)wParam;
	const BOOL accept = (gen == m_missGen) && m_miss && pl && pl->pc
		&& job->count > 0 && job->indices && job->result;
	if (accept) {
		for (int k = 0; k < job->count; ++k) {
			const int i = job->indices[k];
			if (i >= 0 && i < m_missCap && i < pl->playcnt)
				m_miss[i] = job->result[k] ? 1 : 0;
		}
		m_missScan = pl->playcnt;
		if (::IsWindow(m_list.GetSafeHwnd()))
			m_list.Invalidate(FALSE);
		// プレイリスト窓の⚠も同じ結果を参照するため再描画
		if (pl && ::IsWindow(pl->m_lc.GetSafeHwnd()))
			pl->m_lc.Invalidate(FALSE);
		UpdateMissChrome();
	}
	else {
		// PL が変わっていたら次の Kick でやり直す
		m_missScan = 0;
	}
	free(job->indices);
	free(job->subs);
	free(job->fols);
	free(job->result);
	free(job);
	return 0;
}

#if WIN64
void CMediaPlayerDlg::OnTimer(UINT_PTR nIDEvent)
#else
void CMediaPlayerDlg::OnTimer(UINT nIDEvent)
#endif
{
	if (nIDEvent == IDT_OGG_RESUME_PROMPT) {
		KillTimer(IDT_OGG_RESUME_PROMPT);
		OggRunResumePrompt();
		return;
	}
	if (nIDEvent == IDT_OGG_RESUME_RESTART) {
		KillTimer(IDT_OGG_RESUME_RESTART);
		RequestPlaybackRestart(NULL);
		return;
	}
	if (nIDEvent == 1) {
		// 低速: テキスト/リスト/シーク/音量を同期(変化時のみ更新)
		SyncFromMain();
		RefreshList(FALSE);
		// 欠損判定はワーカスレッド(KickMissScan)。UI で PathFileExists しない。
		KickMissScan();
		EnforceFalcomHidden();
		if (m_sleepEndTick != 0)
			UpdateQueueChrome();
		// 描画タイマー(2)は 60fps 固定。更新頻度の律速は og 側(ms2カウンタ)が行うため、
		// ここで張り直す必要はない。
	}
	else if (nIDEvent == 8) {
		// ミニジャケ: 温いときは 500ms に間引き(毎60msの空監視をやめる)。
		// スクロール/未解決で 60ms に戻す。ディスク再読込はしない。
		extern LONG COgg_GetGdiPaintPending();
		static int s_jakLastTop = -1;
		const int topNow = ::IsWindow(m_list.GetSafeHwnd()) ? m_list.GetTopIndex() : -1;
		if (topNow != s_jakLastTop) {
			s_jakLastTop = topNow;
			if (s_mpJakTimerMs != 60) {
				s_mpJakTimerMs = 60;
				SetTimer(8, 60, NULL);
			}
		}
		BOOL work = FALSE;
		if (COgg_GetGdiPaintPending() == 0)
			work = MpJacketLoadVisible(this, TRUE, FALSE);
		else
			work = TRUE; // pending 中は高速側を維持し描画回復後すぐ再開
		const UINT want = work ? 60u : 500u;
		if (want != s_mpJakTimerMs) {
			s_mpJakTimerMs = want;
			SetTimer(8, want, NULL);
		}
	}
	else if (nIDEvent == 2) {
		// 安全網: pending 中に 100ms 毎 Invalidate すると重い OnPaint と重なって約2倍になる。
		// 取りこぼし(80ms以上残存)のときだけ、200ms に1回促す。
		extern LONG COgg_GetGdiPaintPending();
		extern DWORD COgg_GetGdiPaintPendingAgeMs();
		if (::IsWindowVisible(GetSafeHwnd()) && !IsIconic()
			&& COgg_GetGdiPaintPending() && COgg_GetGdiPaintPendingAgeMs() >= 80u) {
			static DWORD s_lastBannerNudge = 0;
			const DWORD now = GetTickCount();
			if (s_lastBannerNudge == 0 || (now - s_lastBannerNudge) >= 200u) {
				s_lastBannerNudge = now;
				InvalidateRect(&m_bannerRect, FALSE);
			}
		}
	}
	else if (nIDEvent == 3) {
		MpBpmOnTimerTick();
		if (savedata.mpRemoteOn)
			MpRemoteUiTick(this);
		// シーク/音量ミラーは timerp 側(同一UIターン)に一本化。ここでも呼ぶと
		// SetPlaybackMirror(UPDATENOW) が二重になり全体が約2倍重い。
		// LRC カラオケ塗りは 250ms 同期だと荒い → GDI時間表示と同じ実再生位置で追従
		if (savedata.mpLrcExpand && m_lrcView.GetSafeHwnd()
			&& ::IsWindowVisible(m_lrcView.GetSafeHwnd())) {
			extern double OggGetGdiPlaybackTimeSec();
			extern int mode;
			extern int videoonly;
			extern UINT ttt;
			DWORD centis = ttt;
			// 音声は DS 先読み補正済みの GDI 時刻。動画は MediaPosition(ttt) を使う。
			if (!(mode == -2 || videoonly)) {
				const double sec = OggGetGdiPlaybackTimeSec();
				if (sec >= 0.0)
					centis = (DWORD)(sec * 100.0 + 0.5);
			}
			m_lrcView.SetPlayCentis(centis);
		}
		SyncDesktopLyricsIfOpen();
		// EQ/ピアノ/スペアナ/ST を X ボタン等で閉じたときも押下見た目を追従
		SyncPushToggleButtons();
		// バナーのホバー状態を再計算(カーソルが帯の上にあれば前面化アニメ継続)。
		// 前面ウィンドウ条件は付けない(再生でフォーカスが移ってもアニメを止めない)。
		if (::IsWindowVisible(GetSafeHwnd()) && !IsIconic()) {
			CPoint pt; ::GetCursorPos(&pt); ScreenToClient(&pt);
			// ジャケットを左へ分離している間はバナー内蔵ジャケが無いので
			// ホバー演出(タイトル減光・ジャケ前面化)は無効化する。
			g_mpBannerHover = (!g_mpSideJacket && m_bannerRect.PtInRect(pt)) ? 1 : 0;

			// info パネルスクロールは TheadLoop から WM_MP_INFO_SCROLL で駆動（~30fps V-Sync同期）
			// Timer3 では行わない（精度不足のため TheadLoop ベースに移植済み）
		}
		else g_mpBannerHover = 0;
	}
	else if (nIDEvent == 4) {
		// 遅延アクリル再適用(合成確定後)。一度きり。
		KillTimer(4);
#if CCUSTOM_AERO_SUPPORT
		if (savedata.aero == 1) {
			RefreshAeroMode();
			if (CCC_IsWin11()) {
				BOOL comp = FALSE;
				if (SUCCEEDED(::DwmIsCompositionEnabled(&comp)) && comp) {
					int bt = 3;  // DWMSBT_TRANSIENTWINDOW(アクリル)
					::DwmSetWindowAttribute(m_hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &bt, sizeof(bt));
					MARGINS mg = { -1, -1, -1, -1 };
					::DwmExtendFrameIntoClientArea(m_hWnd, &mg);
				}
			}
			RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
			// ExtendFrame 直後は子の α が落ちる。ボタンは即不透明再描画。
			for (HWND h = ::GetWindow(m_hWnd, GW_CHILD); h; h = ::GetWindow(h, GW_HWNDNEXT))
				CCC_ForceRepaintHwnd(h);
		}
#endif
	}
	else if (nIDEvent == 6) {
		// 起動直後のスクロールバー未表示対策(両モード)。モード切替時と同じく
		// 子の非クライアント(枠/スクロールバー)を再描画させる。RDW_FRAME により
		// リストへ WM_NCPAINT が飛び、アクリル時は OpaqueFixer が不透明化する。
		KillTimer(6);
		if (::IsWindow(m_list.GetSafeHwnd())) {
			if (pl && pl->playcnt > 0) {
				int anchor = GetListScrollAnchor();
				m_list.SetItemCount(pl->playcnt);   // 可視状態で範囲を再確定
				RestoreListScrollAnchor(anchor);    // SetItemCount で先頭へ戻るのを防ぐ
			}
			m_list.RedrawWindow(NULL, NULL,
				RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW | RDW_ERASE);
		}
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
	}
	else if (nIDEvent == kTimerListHdrDrag) {
		TickListHdrDragFit();
	}
	else if (nIDEvent == 9) {
		KillTimer(9);
		savedata.mpSleepMin = 0;
		MpPersistSavedataQuick();
		OnFadeout();
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON1, BN_CLICKED), 0); // stop
	}
	else if (nIDEvent == 7) {
		MpAlarmTick(this);
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CMediaPlayerDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	// Create/MoveWindow 中の WM_SIZE は列作成前などに来る。
	// 未準備の GetCheck/DoLayout は ENSURE→「引数が正しくありません」。
	if (!m_uiReady)
		return;
	try {
	if (nType == SIZE_MINIMIZED) {
		if (m_mini.GetSafeHwnd() && m_mini.GetCheck() && og && ::IsWindow(og->GetSafeHwnd())) {
			if (og->m_EqualizerDlg && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd())) {
				m_savedEqVisible = ::IsWindowVisible(og->m_EqualizerDlg->m_hWnd) ? 1 : 0;
				if (m_savedEqVisible) ::ShowWindow(og->m_EqualizerDlg->m_hWnd, SW_HIDE);
			}
			if (og->m_PianoRollDlg && ::IsWindow(og->m_PianoRollDlg->GetSafeHwnd())) {
				m_savedPianoVisible = ::IsWindowVisible(og->m_PianoRollDlg->m_hWnd) ? 1 : 0;
				if (m_savedPianoVisible) ::ShowWindow(og->m_PianoRollDlg->m_hWnd, SW_HIDE);
			}
			if (og->m_AnalyzerDlg && ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd())) {
				m_savedAnalyzerVisible = ::IsWindowVisible(og->m_AnalyzerDlg->m_hWnd) ? 1 : 0;
				if (m_savedAnalyzerVisible) ::ShowWindow(og->m_AnalyzerDlg->m_hWnd, SW_HIDE);
			}
		}
		return;
	}
	if (::IsWindow(m_hWnd)) {
		if (nType == SIZE_RESTORED && m_mini.GetSafeHwnd() && m_mini.GetCheck() && og && ::IsWindow(og->GetSafeHwnd())) {
			if (m_savedEqVisible && og->m_EqualizerDlg && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd()))
				::ShowWindow(og->m_EqualizerDlg->m_hWnd, SW_SHOW);
			if (m_savedPianoVisible && og->m_PianoRollDlg && ::IsWindow(og->m_PianoRollDlg->GetSafeHwnd()))
				::ShowWindow(og->m_PianoRollDlg->m_hWnd, SW_SHOW);
			if (m_savedAnalyzerVisible && og->m_AnalyzerDlg && ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()))
				::ShowWindow(og->m_AnalyzerDlg->m_hWnd, SW_SHOW);
			m_savedEqVisible = 0;
			m_savedPianoVisible = 0;
			m_savedAnalyzerVisible = 0;
		}
		DoLayout();
		// リサイズ: 右/下辺連鎖は ENTERSIZEMOVE 無しでも pOld→pNew 増分で動く
		{
			CRect wr;
			GetWindowRect(&wr);
			if (m_cascadePrevValid) {
				const int dw = wr.Width() - m_cascadePrevRc.Width();
				const int dh = wr.Height() - m_cascadePrevRc.Height();
				if (dw != 0 || dh != 0)
					CCC_NeighborCascadeOnMainResize(&m_cascadePrevRc, &wr);
				else if (wr.left != m_cascadePrevRc.left || wr.top != m_cascadePrevRc.top)
					CCC_MainLockOnMainMoving(&wr);
			}
			m_cascadePrevRc = wr;
			m_cascadePrevValid = true;
		}
		if (m_inSizeMove) {
			RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
		}
		else {
			RedrawWindow(NULL, NULL,
				RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
			if (::IsWindow(m_list.GetSafeHwnd()))
				m_list.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
		}
	}
	}
	catch (CException* e)
	{
		OutputDebugString(_T("[CMediaPlayerDlg::OnSize] CException swallowed\n"));
		e->Delete();
	}
}

void CMediaPlayerDlg::OnEnterSizeMove()
{
	m_inSizeMove = true;
	GetWindowRect(&m_cascadePrevRc);
	m_cascadePrevValid = true;
	CCC_NeighborCascadeBegin(m_hWnd);
	Default();
}

void CMediaPlayerDlg::OnExitSizeMove()
{
	m_inSizeMove = false;
	if (::IsWindow(m_hWnd) && !IsIconic()) {
		DoLayout();
		{
			CRect wr;
			GetWindowRect(&wr);
			if (m_cascadePrevValid)
				CCC_NeighborCascadeOnMainResize(&m_cascadePrevRc, &wr);
			else
				CCC_MainLockOnMainMoving(&wr);
			m_cascadePrevRc = wr;
		}
		CCC_NeighborCascadeEnd();
		m_cascadePrevValid = false;
		// 確定時に一度だけ同期再描画して、ドラッグ中の簡易描画の崩れを整える。
		RedrawWindow(NULL, NULL,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		if (::IsWindow(m_list.GetSafeHwnd()))
			m_list.RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
	}
	Default();
}

void CMediaPlayerDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogExBase::OnMoving(fwSide, pRect);
	CCC_MainLockOnMainMoving(pRect);
	SavePos();
}

void CMediaPlayerDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	lpMMI->ptMinTrackSize.x = (int)(620 * hD2);
	lpMMI->ptMinTrackSize.y = (int)(560 * hD2);
	CCustomBlurDialogExBase::OnGetMinMaxInfo(lpMMI);
}

// og のオフスクリーン合成面(スペアナ+ジャケ+時間)を帯に描画(同一UIスレッドなので安全)
void CMediaPlayerDlg::BlitVisualizer(CDC* pDC)
{
	if (!pDC || m_bannerRect.IsRectEmpty()) return;
	if (dc.GetSafeHdc() == NULL) return;
	int dw = m_bannerRect.Width(), dh = m_bannerRect.Height();
	if (dw <= 0 || dh <= 0) return;

	// 常にメモリDCへ1枚合成してから画面へ1回だけ Blit。
	// メーターを後段で重ねるとアクリルでちらつく。
	if (m_memBanner.GetSafeHdc() == NULL)
		m_memBanner.CreateCompatibleDC(pDC);
	if (m_bannerCacheW != dw || m_bannerCacheH != dh || m_bmpBanner.GetSafeHandle() == NULL) {
		if (m_bmpBanner.GetSafeHandle()) m_bmpBanner.DeleteObject();
		m_bmpBanner.CreateCompatibleBitmap(pDC, dw, dh);
		m_bannerCacheW = dw; m_bannerCacheH = dh;
	}
	HGDIOBJ oldBmp = ::SelectObject(m_memBanner.GetSafeHdc(), m_bmpBanner.GetSafeHandle());
	int oldMode = ::SetStretchBltMode(m_memBanner.GetSafeHdc(), COLORONCOLOR);
	::SetBrushOrgEx(m_memBanner.GetSafeHdc(), 0, 0, NULL);
	::StretchBlt(m_memBanner.GetSafeHdc(), 0, 0, dw, dh, dc.GetSafeHdc(), 0, 0, MP_SRCW, MP_SRCH, SRCCOPY);
	::SetStretchBltMode(m_memBanner.GetSafeHdc(), oldMode);
	DrawBannerMeters(&m_memBanner, dw, dh);

#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1 && CCC_IsWin11()) {
		// 黒透過＋非黒(メーター含む)は不透明。1回の NF Blit で完結
		CCC_BlitStretchNF(pDC->m_hDC, m_bannerRect.left, m_bannerRect.top, dw, dh,
			m_memBanner.GetSafeHdc(), 0, 0, dw, dh, RGB(0, 0, 0));
	}
	else if (CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11() && !CCC_IsAeroEnabled()) {
		CCC_BlitStretchOpaque(pDC->m_hDC, m_bannerRect.left, m_bannerRect.top, dw, dh,
			m_memBanner.GetSafeHdc(), 0, 0, dw, dh);
	}
	else
#endif
	{
		::BitBlt(pDC->m_hDC, m_bannerRect.left, m_bannerRect.top, dw, dh,
			m_memBanner.GetSafeHdc(), 0, 0, SRCCOPY);
	}
	::SelectObject(m_memBanner.GetSafeHdc(), oldBmp);
}

void CMediaPlayerDlg::SyncBannerSoft3DCamFromSave()
{
	GdiSoft3D::CamFromSaved(m_bannerCam3d, savedata.mpBanner3dyaw, savedata.mpBanner3dpitch, savedata.mpBanner3dzoom);
}

void CMediaPlayerDlg::PersistBannerSoft3DCam()
{
	GdiSoft3D::CamToSaved(m_bannerCam3d, savedata.mpBanner3dyaw, savedata.mpBanner3dpitch, savedata.mpBanner3dzoom);
}

void CMediaPlayerDlg::BannerSoft3dYawCb(void* ctx, int value)
{
	auto* self = (CMediaPlayerDlg*)ctx;
	if (!self) return;
	self->m_bannerCam3d.yawDeg = (float)value / 10.f;
	GdiSoft3D::ClampCam(self->m_bannerCam3d);
	self->PersistBannerSoft3DCam();
	if (::IsWindow(self->m_hWnd)) self->Invalidate(FALSE);
}
void CMediaPlayerDlg::BannerSoft3dPitchCb(void* ctx, int value)
{
	auto* self = (CMediaPlayerDlg*)ctx;
	if (!self) return;
	self->m_bannerCam3d.pitchDeg = (float)value / 10.f;
	GdiSoft3D::ClampCam(self->m_bannerCam3d);
	self->PersistBannerSoft3DCam();
	if (::IsWindow(self->m_hWnd)) self->Invalidate(FALSE);
}
void CMediaPlayerDlg::BannerSoft3dZoomCb(void* ctx, int value)
{
	auto* self = (CMediaPlayerDlg*)ctx;
	if (!self) return;
	self->m_bannerCam3d.zoom = (float)value / 100.f;
	GdiSoft3D::ClampCam(self->m_bannerCam3d);
	self->PersistBannerSoft3DCam();
	if (::IsWindow(self->m_hWnd)) self->Invalidate(FALSE);
}

void CMediaPlayerDlg::PresentBannerSoft3D(CDC* pDC)
{
	if (!pDC || m_bannerRect.IsRectEmpty()) return;
	// コンテキストメニュー Track 中は重い Soft3D+Speana を止める（退場／再オープンで UI フリーズする）
	if (CCustomPopupMenu::GetTrackingRoot() != NULL) {
		pDC->FillSolidRect(&m_bannerRect, RGB(8, 10, 16));
		return;
	}
	// アナライザ/ピアノと同様: Soft3D はバナー矩形だけ。ジャケ/情報は 2D サイドパネル。
	// （旧: 3領域を1枚に載せてバーがジャケ・情報の下に食い込み、クリップで中心帯だけ見えて壊れて見えた）
	const int sw = m_bannerRect.Width(), sh = m_bannerRect.Height();
	if (sw < 40 || sh < 24) return;

	// 毎フレ CreateCompatibleBitmap しない（2D BlitVisualizer と同じ永続面）
	if (m_memBanner.GetSafeHdc() == NULL)
		m_memBanner.CreateCompatibleDC(pDC);
	if (m_bannerCacheW != sw || m_bannerCacheH != sh || m_bmpBanner.GetSafeHandle() == NULL) {
		if (m_bmpBanner.GetSafeHandle()) m_bmpBanner.DeleteObject();
		if (!m_bmpBanner.CreateCompatibleBitmap(pDC, sw, sh)) return;
		m_bannerCacheW = sw; m_bannerCacheH = sh;
	}
	HGDIOBJ oldBmp = ::SelectObject(m_memBanner.GetSafeHdc(), m_bmpBanner.GetSafeHandle());
	m_memBanner.FillSolidRect(0, 0, sw, sh, RGB(8, 10, 16));

	{
		static DWORD s_fftMs = 0;
		const DWORD now = GetTickCount();
		if (og && plf == 1 && (now - s_fftMs) >= 33u) {
			s_fftMs = now;
			og->Speana(FALSE, TRUE);
		}
	}

	extern int speanaInst[400];
	extern int speanaLiveStereo;
	extern int speanaFftL[88];
	extern int speanaFftR[88];
	extern int speanaFftStereo;
	extern int speanaFftValid;

	float levL[64] = {}, levR[64] = {};
	const int barN = 64;
	bool stereo = false;
	bool have = false;

	// 2Dバナーと同じ speanaInst を最優先（FFT 経路だけが低域偏り／枯死しやすい）
	{
		int nz = 0;
		if (speanaLiveStereo) {
			stereo = true;
			for (int i = 0; i < barN; ++i) {
				const int src = (i * 88) / barN;
				levL[i] = (float)speanaInst[100 + src] / 96.f;
				levR[i] = (float)speanaInst[200 + src] / 96.f;
				if (levL[i] > 0.01f || levR[i] > 0.01f) ++nz;
			}
		} else {
			for (int i = 0; i < barN; ++i) {
				const int src = (i * 88) / barN;
				levL[i] = (float)speanaInst[src] / 96.f;
				if (levL[i] > 0.01f) ++nz;
			}
		}
		have = (nz > 0);
	}

	// フォールバック: Soft3D 用 FFT
	if (!have && speanaFftValid) {
		stereo = (speanaFftStereo != 0);
		int nz = 0;
		for (int i = 0; i < barN; ++i) {
			const int i0 = (i * 88) / barN;
			const int i1 = ((i + 1) * 88) / barN;
			float aL = 0.f, aR = 0.f;
			for (int k = i0; k < i1 && k < 88; ++k) {
				float vL = (float)speanaFftL[k] / 96.f;
				float vR = (float)speanaFftR[k] / 96.f;
				if (vL > aL) aL = vL;
				if (vR > aR) aR = vR;
			}
			levL[i] = (aL > 1.f) ? 1.f : aL;
			levR[i] = (aR > 1.f) ? 1.f : aR;
			if (levL[i] > 0.01f || levR[i] > 0.01f) ++nz;
		}
		have = (nz > 0);
	}

	const float boxes[1][6] = { { -1.15f, 1.15f, -0.02f, 0.72f, 0.0f, 0.95f } };
	GdiSoft3D::View v;
	GdiSoft3D::BuildView(sw, sh, m_bannerCam3d, boxes, 1, v);

	const DWORD softT0 = GetTickCount();
	if (!m_bannerSoftCtx.Create(sw, sh)) {
		::SelectObject(m_memBanner.GetSafeHdc(), oldBmp);
		return;
	}
	GdiSoft3D::Context& ctx = m_bannerSoftCtx;
	ctx.view = v;
	ctx.depthTest = true;
	ctx.depthWrite = true;
	ctx.BeginFrame(RGB(8, 10, 16));
	ctx.DrawGrid(-1.05f, 1.05f, 0.05f, 0.90f, 0.0f, 6, RGB(40, 44, 58));

	if (have) {
		// 左半分=L全帯域 / 右半分=R全帯域（同じZ。bin内交互や手前奥は使わない）
		const float z0 = 0.22f, z1 = 0.55f, maxY = 0.58f;
		const float gapFrac = (savedata.mpSpeanaStyle == 1) ? 0.08f : 0.18f;
		const COLORREF cL = RGB(80, 210, 255);
		const COLORREF cR = RGB(255, 140, 90);
		if (stereo)
			ctx.DrawStereoBarsLR(-1.0f, 1.0f, barN, levL, levR, z0, z1, maxY, gapFrac, cL, cR);
		else
			ctx.DrawStereoBarsLR(-1.0f, 1.0f, barN, levL, nullptr, z0, z1, maxY, gapFrac, cL, cL);
	}

	if (savedata.pro_corr_meter) {
		const float corr = ProAudio_CorrValue();
		const float bal = ProAudio_CorrBalance();
		ctx.DrawBox(0.92f, 1.02f, 0.30f + corr * 0.25f, 0.15f, 0.35f, RGB(100, 230, 150), 0.f);
		const float bx = 1.08f + bal * 0.06f;
		ctx.DrawBox(bx - 0.025f, bx + 0.025f, 0.12f, 0.15f, 0.35f, RGB(255, 190, 90), 0.f);
	}

	ctx.EndFrame();
	ctx.Present(m_memBanner, 0, 0);

	// CPU 描画なので面積次第で遅くなる。連続で重かったら 2D に戻す案内を出す。
	{
		const DWORD spent = GetTickCount() - softT0;
		if (spent >= 24) {
			if (++m_soft3dSlowFrames >= 30) {
				m_soft3dSlowFrames = 0;
				m_soft3dPerfHintUntil = GetTickCount() + 4000;
			}
		} else if (m_soft3dSlowFrames > 0) {
			--m_soft3dSlowFrames;
		}
	}

	// ツアー中(操作ガイドの Soft3D 章から)はバナー左上にヒントを重ねる
	{
		const DWORD now = GetTickCount();
		if (now < m_soft3dTourUntil || now < m_soft3dPerfHintUntil) {
			const int oldMode = m_memBanner.SetBkMode(TRANSPARENT);
			CFont* oldFont = m_memBanner.SelectObject(GetFont());
			if (now < m_soft3dTourUntil) {
				m_memBanner.SetTextColor(RGB(255, 236, 180));
				m_memBanner.TextOut(8, 6, LL14(
					L"ドラッグで視点、ホイールでズーム、0 で戻る",
					L"Drag to orbit, wheel to zoom, 0 to reset",
					L"Glisser = orbite, molette = zoom, 0 = reset",
					L"Trascina = orbita, rotella = zoom, 0 = reset",
					L"Arrastrar = órbita, rueda = zoom, 0 = reiniciar",
					L"드래그로 시점, 휠로 줌, 0 으로 복귀",
					L"拖动旋转视角，滚轮缩放，0 复位",
					L"اسحب للدوران، العجلة للتكبير، 0 للتصفير",
					L"Тянуть — облёт, колесо — зум, 0 — сброс",
					L"Ziehen = Orbit, Rad = Zoom, 0 = Reset",
					L"Arraste = órbita, roda = zoom, 0 = redefinir",
					L"Slepen = orbit, wiel = zoom, 0 = reset",
					L"Przeciąganie = orbita, kółko = zoom, 0 = reset",
					L"Sürükle = yörünge, teker = zoom, 0 = sıfırla"));
			}
			if (now < m_soft3dPerfHintUntil) {
				m_memBanner.SetTextColor(RGB(255, 190, 160));
				m_memBanner.TextOut(8, 6 + 18, LL14(
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
			if (oldFont) m_memBanner.SelectObject(oldFont);
			m_memBanner.SetBkMode(oldMode);
		}
	}

#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1 && CCC_IsWin11())
		CCC_BlitStretchNF(pDC->m_hDC, m_bannerRect.left, m_bannerRect.top, sw, sh, m_memBanner.GetSafeHdc(), 0, 0, sw, sh, RGB(0, 0, 0));
	else if (CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11() && !CCC_IsAeroEnabled())
		CCC_BlitStretchOpaque(pDC->m_hDC, m_bannerRect.left, m_bannerRect.top, sw, sh, m_memBanner.GetSafeHdc(), 0, 0, sw, sh);
	else
#endif
		pDC->BitBlt(m_bannerRect.left, m_bannerRect.top, sw, sh, &m_memBanner, 0, 0, SRCCOPY);

	::SelectObject(m_memBanner.GetSafeHdc(), oldBmp);
}


CString CMediaPlayerDlg::CurrentTrackTitle() const
{
	CString t = fnn;
	if (mode == -10 || mode == -9 || mode == -8 || mode == -7) {
		if (!tagfile.IsEmpty()) t = tagfile;
	}
	if ((stitle != _T("") && mode == -1) || mode == 21 || mode == -6 || mode == 33 || mode == 34 || mode == 35) t = stitle;
	// wav 等もタグのタイトルがあれば優先(無ければファイル名のまま)
	if (mode == 999 && !stitle.IsEmpty()) t = stitle;
	return t;
}

void CMediaPlayerDlg::InvalidateSidePanels()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	extern ULONGLONG g_xfJacketStableUntil;
	const bool holdJak = (g_xfJacketStableUntil != 0 && GetTickCount64() < g_xfJacketStableUntil);
	if (!holdJak && !m_jacketRect.IsRectEmpty())
		InvalidateRect(&m_jacketRect, FALSE);
	if (!m_infoPanelRect.IsRectEmpty()) InvalidateRect(&m_infoPanelRect, FALSE);
}

// m_tip チェックボックスの状態を m_list のツールチップ設定に反映。
// ON : 行詳細ツールチップ(CListCtrlA)のみ。ダイアログ側バルーンは外す。
// OFF: 行詳細を止め、「ダブルクリックで再生…」バルーンを出す。
void CMediaPlayerDlg::ApplyListTooltipState()
{
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	const bool on = (m_tip.GetCheck() != 0);
	m_list.EnableToolTips(on ? TRUE : FALSE);
	DWORD exStyle = m_list.GetExtendedStyle();
	if (on)
		exStyle &= ~LVS_EX_INFOTIP;   // カスタムツールチップ使用中はシステム infotip を無効
	else
		exStyle |= LVS_EX_INFOTIP;
	m_list.SetExtendedStyle(exStyle);

	if (m_tooltip.GetSafeHwnd()) {
		const CString balloon = LL14(
			L"ダブルクリックで再生。ファイルをドロップして追加できます。",
			L"Double-click to play. Drop files to add.",
			L"Double-clic pour lire. Glissez des fichiers.",
			L"Doppio clic per riprodurre. Trascina file.",
			L"Doble clic para reproducir. Suelta archivos.",
			L"더블 클릭으로 재생. 파일을 드롭해 추가.",
			L"双击播放。拖入文件添加。",
			L"انقر مزدوجاً للتشغيل. أفلت الملفات.",
			L"Двойной клик — воспроизведение. Перетащите файлы.",
			L"Doppelklick zum Abspielen. Dateien ablegen.",
			L"Clique duplo para tocar. Solte arquivos.",
			L"Dubbelklik om af te spelen. Sleep bestanden.",
			L"Kliknij dwukrotnie. Upuść pliki.",
			L"Çift tıkla çal. Dosya bırak.");
		// いったん外してから、OFF のときだけ付け直す(ON 時の二重表示防止)
		m_tooltip.DelTool(&m_list);
		if (!on)
			m_tooltip.AddTool(&m_list, balloon);
	}
}

// WM_MP_INFO_SCROLL ハンドラ。TheadLoop から ~30fps で PostMessage される。
// タイマーよりも V-Sync に近いタイミングで呼ばれるため marquee が滑らかになる。
// m_iscActive が true なら右曲情報パネルを無効化 → DrawSidePanels がスクロールを1段進めて
// 再び true にセットする(→次 tick でまた無効化)。スクロール不要なら m_iscActive は
// false のままで再描画は発生しない。
LRESULT CMediaPlayerDlg::OnInfoScrollTick(WPARAM, LPARAM)
{
	// posted は描画完了まで保持。先に降ろすと TheadLoop が連投する。
	// ピアノ/アナライザが開いていると Invalidate だけの WM_PAINT は後回しになるため、
	// 情報パネルだけ UPDATENOW でこのターンに描画してスクロールを守る。
	if (m_iscActive && !m_infoPanelRect.IsRectEmpty()) {
		m_iscActive = false;
		RedrawWindow(&m_infoPanelRect, NULL,
			RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
	}
	else {
		InterlockedExchange(&m_iscScrollPosted, 0);
	}
	return 0;
}

// 非アクティブ化でアクリル背景が落ちる対策。
// mp はタスクバー表示のためトップレベル化(オーナー解除)されており、
// EQ/簡易ピアノロール等の og 所有ウィンドウと違い、非アクティブ時に DWM の
// アクリルバックドロップが維持されない。活性が変わるたびに backdrop 属性と
// フレーム拡張を再適用して、非アクティブでもアクリルを保つ。
BOOL CMediaPlayerDlg::OnNcActivate(BOOL bActive)
{
	BOOL r = CCustomBlurDialogExBase::OnNcActivate(bActive);
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1 && CCC_IsWin11())
		CCC_RefreshDwmBlur(m_hWnd);   // backdrop=acrylic + フレーム拡張を再適用
#endif
	return r;
}

void CMediaPlayerDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		ShowOggAboutDialog(this);
		return;
	}
	if ((nID & 0xFFF0) == SC_CLOSE)
	{
		OnClose();
		return;
	}
	CCustomBlurDialogExBase::OnSysCommand(nID, lParam);
}

void CMediaPlayerDlg::ResetInfoScroll()
{
	for (int i = 0; i < kInfoRows; i++) {
		m_isc[i] = 0; m_iscW[i] = 0;
		if (m_iscRowDC[i].GetSafeHdc()) {
			if (m_iscRowOldBmp[i]) m_iscRowDC[i].SelectObject(m_iscRowOldBmp[i]);
			m_iscRowDC[i].DeleteDC();
		}
		m_iscRowBmp[i].DeleteObject();
		m_iscRowOldBmp[i] = nullptr;
		m_iscRowCacheW[i] = m_iscRowCacheH[i] = 0;
		m_iscRowCacheText[i].Empty();
	}
	m_iscActive = false;
	InterlockedExchange(&m_iscScrollPosted, 0);
}

// 1行のテキストをスクロール対応で mem DC へ描画する。
//
// 収まる場合: DrawText で静止描画して false を返す(スクロール不要)。
//
// はみ出す場合: 「テキスト + セパレータ」2連続のワイド DC を行キャッシュし、
// m_isc[rowIdx] オフセットで可視幅(tw)分だけ BitBlt する。
// （旧実装は毎フレーム CreateCompatibleBitmap/CreatePen → 長時間で GDI が死ぬ）
//
// rowIdx: m_isc/m_iscW のインデックス(0=タイトル行, 1〜5=サブ行)
bool CMediaPlayerDlg::DrawInfoScrollRow(CDC& mem, int tx, int y, int tw, int lineH,
	const CString& text, COLORREF clr, int rowIdx, COLORREF kBg, CFont* font)
{
	if (text.IsEmpty() || tw <= 0 || lineH <= 0) return false;
	if (rowIdx < 0 || rowIdx >= kInfoRows) return false;

	CFont* oldFont = mem.SelectObject(font);
	CSize szText = mem.GetTextExtent(text);
	mem.SelectObject(oldFont);

	if (szText.cx <= tw) {
		m_isc[rowIdx]  = 0;
		m_iscW[rowIdx] = 0;
		mem.SelectObject(font);
		mem.SetTextColor(clr);
		CRect rr(tx, y, tx + tw, y + lineH);
		mem.DrawText(text, &rr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		mem.SelectObject(oldFont);
		return false;
	}

	const CString kSep = _T("　　 ");
	CString scrollText = text + kSep;
	mem.SelectObject(font);
	CSize szFull = mem.GetTextExtent(scrollText);
	mem.SelectObject(oldFont);

	if (szFull.cx <= 0) return false;
	m_iscW[rowIdx] = szFull.cx;

	const int wideW = szFull.cx * 2 + 4;
	const bool needRebuild =
		!m_iscRowDC[rowIdx].GetSafeHdc()
		|| m_iscRowCacheW[rowIdx] != wideW
		|| m_iscRowCacheH[rowIdx] != lineH
		|| m_iscRowCacheClr[rowIdx] != clr
		|| m_iscRowCacheBg[rowIdx] != kBg
		|| m_iscRowCacheText[rowIdx] != scrollText;

	if (needRebuild) {
		if (m_iscRowDC[rowIdx].GetSafeHdc()) {
			if (m_iscRowOldBmp[rowIdx]) m_iscRowDC[rowIdx].SelectObject(m_iscRowOldBmp[rowIdx]);
			m_iscRowDC[rowIdx].DeleteDC();
		}
		m_iscRowBmp[rowIdx].DeleteObject();
		m_iscRowOldBmp[rowIdx] = nullptr;
		m_iscRowCacheW[rowIdx] = m_iscRowCacheH[rowIdx] = 0;

		if (!m_iscRowDC[rowIdx].CreateCompatibleDC(&mem))
			return false;
		if (!m_iscRowBmp[rowIdx].CreateCompatibleBitmap(&mem, wideW, lineH)) {
			m_iscRowDC[rowIdx].DeleteDC();
			return false;
		}
		m_iscRowOldBmp[rowIdx] = m_iscRowDC[rowIdx].SelectObject(&m_iscRowBmp[rowIdx]);
		CDC& wdc = m_iscRowDC[rowIdx];
		wdc.FillSolidRect(0, 0, wideW, lineH, kBg);
		wdc.SetBkMode(TRANSPARENT);
		wdc.SetTextColor(clr);

		CFont* wf = wdc.SelectObject(font);
		CRect wr1(0, 0, szFull.cx + 4, lineH);
		wdc.DrawText(scrollText, &wr1, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
		CRect wr2(szFull.cx, 0, szFull.cx * 2 + 4, lineH);
		wdc.DrawText(scrollText, &wr2, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

		CSize szTextOnly = wdc.GetTextExtent(text);
		int sx = szTextOnly.cx;
		int sw = szFull.cx - szTextOnly.cx;
		if (sw > 8) {
			int cy = lineH / 2;
			int dr = max(2, lineH / 10);
			HDC hdc = wdc.GetSafeHdc();
			HGDIOBJ oldPen = ::SelectObject(hdc, ::GetStockObject(NULL_PEN));
			HGDIOBJ oldBrush = ::SelectObject(hdc, ::GetStockObject(DC_BRUSH));
			::SetDCBrushColor(hdc, clr);

			int lx = sx + sw / 3;
			wdc.Ellipse(lx - dr, cy - dr, lx + dr, cy + dr);
			int rx = sx + sw * 2 / 3;
			wdc.Ellipse(rx - dr, cy - dr, rx + dr, cy + dr);
			int mx = sx + sw / 2;
			int mr = max(2, lineH / 8);
			POINT diaPts[4] = { {mx, cy - mr}, {mx + mr, cy}, {mx, cy + mr}, {mx - mr, cy} };
			wdc.Polygon(diaPts, 4);

			::SelectObject(hdc, ::GetStockObject(DC_PEN));
			::SetDCPenColor(hdc, clr);
			::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
			wdc.MoveTo(sx + 2, cy); wdc.LineTo(lx - dr - 1, cy);
			wdc.MoveTo(rx + dr + 1, cy); wdc.LineTo(sx + sw - 2, cy);

			int sx2 = sx + szFull.cx;
			int lx2 = sx2 + sw / 3, rx2 = sx2 + sw * 2 / 3, mx2 = sx2 + sw / 2;
			::SelectObject(hdc, ::GetStockObject(NULL_PEN));
			::SelectObject(hdc, ::GetStockObject(DC_BRUSH));
			::SetDCBrushColor(hdc, clr);
			wdc.Ellipse(lx2 - dr, cy - dr, lx2 + dr, cy + dr);
			wdc.Ellipse(rx2 - dr, cy - dr, rx2 + dr, cy + dr);
			POINT diaPts2[4] = { {mx2, cy - mr}, {mx2 + mr, cy}, {mx2, cy + mr}, {mx2 - mr, cy} };
			wdc.Polygon(diaPts2, 4);
			::SelectObject(hdc, ::GetStockObject(DC_PEN));
			::SetDCPenColor(hdc, clr);
			::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
			wdc.MoveTo(sx2 + 2, cy); wdc.LineTo(lx2 - dr - 1, cy);
			wdc.MoveTo(rx2 + dr + 1, cy); wdc.LineTo(sx2 + sw - 2, cy);

			::SelectObject(hdc, oldBrush);
			::SelectObject(hdc, oldPen);
		}
		wdc.SelectObject(wf);

		m_iscRowCacheW[rowIdx] = wideW;
		m_iscRowCacheH[rowIdx] = lineH;
		m_iscRowCacheClr[rowIdx] = clr;
		m_iscRowCacheBg[rowIdx] = kBg;
		m_iscRowCacheText[rowIdx] = scrollText;
	}

	CDC& wdc = m_iscRowDC[rowIdx];
	if (!wdc.GetSafeHdc()) return false;

	int off = m_isc[rowIdx] % szFull.cx;
	if (off < 0) off = 0;

	int saved = mem.SaveDC();
	mem.IntersectClipRect(tx, y, tx + tw, y + lineH);
	mem.BitBlt(tx - off, y, szFull.cx, lineH, &wdc, 0, 0, SRCCOPY);
	mem.BitBlt(tx + szFull.cx - off, y, tw, lineH, &wdc, szFull.cx, 0, SRCCOPY);
	mem.RestoreDC(saved);

	m_isc[rowIdx] += 2;
	if (m_isc[rowIdx] >= szFull.cx) m_isc[rowIdx] -= szFull.cx;

	return true;
}

// ジャケット無しのとき、素っ気ないアイコンの代わりに「Media Player らいら」の
// タイトルと、ほんのり可愛いパステルの模様(縦グラデ + 水玉 + キラキラ/お花)を描く。
// dc は w×h のオフスクリーン。純黒(=アクリルのクロマキー)は使わない。
static void Mp_DrawNoJacketPlaceholder(CDC& dc, int w, int h)
{
	if (w <= 0 || h <= 0) return;

	// --- 背景: やわらかいピンク → ラベンダーの縦グラデ ---
	for (int y = 0; y < h; y++) {
		int t = (h > 1) ? (y * 100 / (h - 1)) : 0;
		int r = 255 + (234 - 255) * t / 100;
		int g = 226 + (223 - 226) * t / 100;
		int b = 240 + (250 - 240) * t / 100;
		dc.FillSolidRect(0, y, w, 1, RGB(r, g, b));
	}

	dc.SetBkMode(TRANSPARENT);
	CGdiObject* opnNull = dc.SelectStockObject(NULL_PEN);

	// --- 水玉模様(市松状にオフセット、ほんのり白でやさしく) ---
	int step = max(12, h / 5);
	int dot = max(2, step / 6);
	{
		CBrush brDot(RGB(255, 245, 250));
		CBrush* ob = dc.SelectObject(&brDot);
		for (int gy = 0, row = 0; gy <= h + step; gy += step, row++) {
			int offx = (row & 1) ? step / 2 : 0;
			for (int gx = -step; gx <= w + step; gx += step) {
				int cx = gx + offx, cy = gy;
				dc.Ellipse(cx - dot, cy - dot, cx + dot, cy + dot);
			}
		}
		dc.SelectObject(ob);
	}

	// --- ちいさなキラキラ(4尖)とお花(アクセント・ハートは使わない) ---
	auto sparkle = [&](int cx, int cy, int s, COLORREF c) {
		if (s < 2) return;
		CBrush br(c);
		CBrush* ob = dc.SelectObject(&br);
		// 縦横のひし形クロス
		POINT v[4] = { { cx, cy - s }, { cx + max(1, s / 4), cy }, { cx, cy + s }, { cx - max(1, s / 4), cy } };
		POINT hz[4] = { { cx - s, cy }, { cx, cy - max(1, s / 4) }, { cx + s, cy }, { cx, cy + max(1, s / 4) } };
		dc.Polygon(v, 4);
		dc.Polygon(hz, 4);
		dc.SelectObject(ob);
	};
	auto flower = [&](int cx, int cy, int s, COLORREF petal, COLORREF core) {
		if (s < 2) return;
		CBrush brP(petal);
		CBrush* ob = dc.SelectObject(&brP);
		int pr = max(2, s * 2 / 3);
		dc.Ellipse(cx - pr, cy - s - pr / 3, cx + pr, cy - s / 4);           // 上
		dc.Ellipse(cx - pr, cy + s / 4, cx + pr, cy + s + pr / 3);           // 下
		dc.Ellipse(cx - s - pr / 3, cy - pr, cx - s / 4, cy + pr);           // 左
		dc.Ellipse(cx + s / 4, cy - pr, cx + s + pr / 3, cy + pr);           // 右
		CBrush brC(core);
		dc.SelectObject(&brC);
		int cr = max(1, s / 3);
		dc.Ellipse(cx - cr, cy - cr, cx + cr, cy + cr);
		dc.SelectObject(ob);
	};
	int hs = max(3, h / 12);
	int fs = max(3, h / 14);
	sparkle(w * 18 / 100, h * 22 / 100, hs, RGB(255, 198, 220));
	flower(w * 80 / 100, h * 28 / 100, fs, RGB(255, 210, 228), RGB(255, 236, 180));
	flower(w * 22 / 100, h * 78 / 100, fs, RGB(255, 204, 222), RGB(255, 240, 190));
	sparkle(w * 78 / 100, h * 82 / 100, hs, RGB(255, 205, 224));
	// 中央寄りに小さなキラを1つ(タイトル周りをふんわり)
	sparkle(w * 88 / 100, h * 58 / 100, max(2, hs * 2 / 3), RGB(255, 220, 232));

	dc.SelectObject(opnNull);

	// --- タイトル: "Media Player" / "らいら" を中央に(下地にやわらかい白影) ---
	int hbig = max(11, h / 4);
	int hsml = max(9, h / 9);
	LOGFONT lf; ZeroMemory(&lf, sizeof(lf));
	lstrcpyn(lf.lfFaceName, _T("Yu Gothic UI"), LF_FACESIZE);
	lf.lfQuality = CLEARTYPE_QUALITY;
	lf.lfWeight = FW_SEMIBOLD;

	CFont fSml; lf.lfHeight = -hsml; fSml.CreateFontIndirect(&lf);
	CFont fBig; lf.lfHeight = -hbig; lf.lfWeight = FW_BOLD; fBig.CreateFontIndirect(&lf);

	int totalH = hsml + hbig + max(1, h / 40);
	int y0 = (h - totalH) / 2; if (y0 < 0) y0 = 0;

	auto shadowText = [&](CFont& f, int yy, int hh, LPCTSTR s, COLORREF fg) {
		CFont* of = dc.SelectObject(&f);
		CRect rt(0, yy, w, yy + hh);
		CRect rs = rt; rs.OffsetRect(1, 1);
		dc.SetTextColor(RGB(255, 255, 255));
		dc.DrawText(s, -1, &rs, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SetTextColor(fg);
		dc.DrawText(s, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		dc.SelectObject(of);
	};
	shadowText(fSml, y0, hsml, _T("Media Player"), RGB(214, 108, 150));
	// ブランド名はウィンドウタイトルと同じ LL14 表記(英語は Raira。らいら固定は翻訳漏れ)
	shadowText(fBig, y0 + hsml + max(1, h / 40), hbig,
		LL14(L"らいら", L"Raira", L"Raira", L"Raira", L"Raira", L"라이라", L"莱拉", L"رايرا", L"Райра", L"Raira", L"Raira", L"Raira", L"Raira", L"Raira"),
		RGB(200, 72, 128));
}

void CMediaPlayerDlg::PresentJacketCached(CDC* pDC)
{
	if (!pDC || m_jacketRect.IsRectEmpty()) return;
	const int w = m_jacketRect.Width(), h = m_jacketRect.Height();
	if (w <= 0 || h <= 0) return;
	const COLORREF kBg = RGB(0, 0, 0);

	CString key;
	if (og && og->jx >= 64 && !og->img.IsNull())
		key.Format(_T("%d:%d:%d"), og->jx, og->jy, (int)(og->jxy * 10000.0));
	else
		key = _T("none");

	const bool need = (m_jacketMemDC.GetSafeHdc() == NULL || m_jacketMemW != w || m_jacketMemH != h
		|| key != m_jacketCacheKey || m_jacketCacheJx != (og ? og->jx : -1));
	if (need) {
		if (m_jacketMemDC.GetSafeHdc()) {
			if (m_jacketMemOldBmp) m_jacketMemDC.SelectObject(m_jacketMemOldBmp);
			m_jacketMemDC.DeleteDC();
		}
		m_jacketMemBmp.DeleteObject();
		m_jacketMemOldBmp = nullptr;
		m_jacketMemW = m_jacketMemH = 0;
		if (!m_jacketMemDC.CreateCompatibleDC(pDC)
			|| !m_jacketMemBmp.CreateCompatibleBitmap(pDC, w, h)) {
			if (m_jacketMemDC.GetSafeHdc()) m_jacketMemDC.DeleteDC();
			m_jacketMemBmp.DeleteObject();
			return;
		}
		m_jacketMemOldBmp = m_jacketMemDC.SelectObject(&m_jacketMemBmp);
		m_jacketMemW = w;
		m_jacketMemH = h;
		m_jacketCacheKey = key;
		m_jacketCacheJx = og ? og->jx : -1;
	}

	m_jacketMemDC.FillSolidRect(0, 0, w, h, kBg);
	if (og && og->jx >= 64 && !og->img.IsNull()) {
		double jr = og->jxy; if (jr <= 0.0) jr = 1.0;
		int dw = w, dh = h;
		if (jr >= 1.0) { dw = w; dh = (int)((double)w / jr); }
		else { dh = h; dw = (int)((double)h * jr); }
		if (dw > w) { dw = w; dh = (int)((double)w / jr); }
		if (dh > h) { dh = h; dw = (int)((double)h * jr); }
		if (dw < 1) dw = 1; if (dh < 1) dh = 1;
		const int dx = (w - dw) / 2, dy = (h - dh) / 2;
		const int om = ::SetStretchBltMode(m_jacketMemDC.GetSafeHdc(), HALFTONE);
		::SetBrushOrgEx(m_jacketMemDC.GetSafeHdc(), 0, 0, NULL);
		og->img.Draw(m_jacketMemDC.GetSafeHdc(), dx, dy, dw, dh, 0, 0, og->jx, og->jy);
		::SetStretchBltMode(m_jacketMemDC.GetSafeHdc(), om);
	}
	else {
		Mp_DrawNoJacketPlaceholder(m_jacketMemDC, w, h);
	}
	DrawJacketHeroOverlay(m_jacketMemDC, w, h);

	bool aero = false;
#if CCUSTOM_AERO_SUPPORT
	aero = (savedata.aero == 1 && CCC_IsWin11());
#endif
	if (aero)
		CCC_BlitStretchNF(pDC->m_hDC, m_jacketRect.left, m_jacketRect.top, w, h,
			m_jacketMemDC.GetSafeHdc(), 0, 0, w, h, kBg);
#if CCUSTOM_AERO_SUPPORT
	else if (CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11() && !CCC_IsAeroEnabled())
		CCC_BlitStretchOpaque(pDC->m_hDC, m_jacketRect.left, m_jacketRect.top, w, h,
			m_jacketMemDC.GetSafeHdc(), 0, 0, w, h);
#endif
	else
		pDC->BitBlt(m_jacketRect.left, m_jacketRect.top, w, h, &m_jacketMemDC, 0, 0, SRCCOPY);
}

void CMediaPlayerDlg::InvalidateJacketImageOnly()
{
	if (!::IsWindow(GetSafeHwnd()) || m_jacketRect.IsRectEmpty()) return;
	m_jacketCacheKey.Empty();
	m_jacketCacheJx = -2;
	m_trackFadeStart = 0;
	CClientDC dc(this);
	PresentJacketCached(&dc);
}

void CMediaPlayerDlg::CancelTrackFade()
{
	m_trackFadeStart = 0;
}

// 左ジャケット / 右曲情報 パネルを描画。バナーと同じ黒地に統一し、上部の帯全体が
// ひとつのメディアバー(左:ジャケ / 中央:スペアナ / 右:曲情報)に見えるようにする。
// 内容は曲変更/リサイズ時のみ再描画されるため(毎フレームではない)ちらつかない。
void CMediaPlayerDlg::DrawSidePanels(CDC* pDC)
{
	if (!pDC) return;
	if (m_jacketRect.IsRectEmpty() && m_infoPanelRect.IsRectEmpty()) return;

	bool aero = false;
#if CCUSTOM_AERO_SUPPORT
	aero = (savedata.aero == 1 && CCC_IsWin11());
#endif
	// アクリル時は黒 = クロマキー(ガラス透過)。非アクリル時も黒背景でバナーと統一。
	const COLORREF kBg = RGB(0, 0, 0);

	// 更新領域(クリップ)に重なるパネルだけ再構築する。バナーは毎フレーム無効化
	// されるが、その際クリップはバナー矩形のみなので、ここでの重い描画(画像縮小/
	// 文字描画)は走らない(=サイドパネルは曲変更/リサイズ時のみ再描画)。
	CRect clip; pDC->GetClipBox(&clip);

	// ---- 左: ジャケット(ミニ・余白へ分離) — オフスクリーン完成後に1回 BitBlt ----
	if (!m_jacketRect.IsRectEmpty() && CRect().IntersectRect(&clip, &m_jacketRect))
		PresentJacketCached(pDC);

	// ---- 右: 曲情報パネル(タイトル/アーティスト/アルバム/形式, スクロール対応) ----
	if (!m_infoPanelRect.IsRectEmpty() && CRect().IntersectRect(&clip, &m_infoPanelRect)) {
		int w = m_infoPanelRect.Width(), h = m_infoPanelRect.Height();

		// リサイズ検出: パネル幅変化時はスクロールをリセット
		if (w != m_lastInfoPanelW) { ResetInfoScroll(); m_lastInfoPanelW = w; }

		if (w > 0 && h > 0) {
			bool memOk = (m_infoMemDC.GetSafeHdc() && m_infoMemW == w && m_infoMemH == h);
			if (!memOk) {
				if (m_infoMemDC.GetSafeHdc()) {
					if (m_infoMemOldBmp) m_infoMemDC.SelectObject(m_infoMemOldBmp);
					m_infoMemDC.DeleteDC();
				}
				m_infoMemBmp.DeleteObject();
				m_infoMemOldBmp = nullptr;
				m_infoMemW = m_infoMemH = 0;
				if (m_infoMemDC.CreateCompatibleDC(pDC)
					&& m_infoMemBmp.CreateCompatibleBitmap(pDC, w, h)) {
					m_infoMemOldBmp = m_infoMemDC.SelectObject(&m_infoMemBmp);
					m_infoMemW = w;
					m_infoMemH = h;
					memOk = true;
				}
				else {
					if (m_infoMemDC.GetSafeHdc()) m_infoMemDC.DeleteDC();
					m_infoMemBmp.DeleteObject();
				}
			}
			if (memOk) {
			CDC& mem = m_infoMemDC;
			mem.FillSolidRect(0, 0, w, h, kBg);
			mem.SetBkMode(TRANSPARENT);

			const int pad = (int)(8 * hD2);
			int tx = pad, tw = w - pad * 2;
			if (tw < 1) tw = 1;

			// ---- 情報収集 ----
			CString title = CurrentTrackTitle();
			CString artist = tagname, album = tagalbum;
			CString track = tagtrack;
			if (mode == -3)
				track.Empty();

			// 曲番号行
			CString trackLine;
			if (!track.IsEmpty())
				trackLine.Format(LL14(L"曲番号 %s", L"Track %s", L"Piste %s", L"Traccia %s", L"Pista %s", L"곡번호 %s", L"曲号 %s", L"المقطع %s", L"Трек %s", L"Titel %s", L"Faixa %s", L"Track %s", L"Utwór %s", L"Parça %s"), (LPCTSTR)track);

			// 形式・Hz・ch・ビット・RG を1行に統合(アップスケール時は ✦)
			CString techLine = MpTechFormatLine();

			// ---- 行高・縦中央寄せ ----
			int titleH  = (int)(24 * hD2);
			int lineH   = (int)(17 * hD2);
			if (lineH < 12) lineH = 12;
			// 技術行は文字が多いので行高も一段低く
			int techH   = (int)(13 * hD2);
			if (techH < 10) techH = 10;
			int ruleGap = (int)(6 * hD2);
			int totalH = titleH + ruleGap;
			if (!artist.IsEmpty())    totalH += lineH;
			if (!album.IsEmpty())     totalH += lineH;
			if (!trackLine.IsEmpty()) totalH += lineH;
			if (!techLine.IsEmpty())  totalH += techH;
			int y = (h - totalH) / 2; if (y < 0) y = 0;

			// ---- タイトル行(スクロール対応) ----
			CFont* of = mem.SelectObject(&m_fontTitle);
			mem.SetBkMode(TRANSPARENT);
			bool titleScrolled = DrawInfoScrollRow(mem, tx, y, tw, titleH,
				title.IsEmpty() ? CString(_T("‐")) : title,
				RGB(255, 255, 255), 0, kBg, &m_fontTitle);
			if (titleScrolled) m_iscActive = true;
			y += titleH;
			mem.SelectObject(of);

			// アクセント罫線（グリーン系でスペアナと調和）
			mem.FillSolidRect(tx, y + ruleGap / 2, tw, max(1, (int)(1 * hD2 + 0.5)), RGB(0, 160, 0));
			y += ruleGap;

			// ---- サブ行（アーティスト/アルバム/曲番号/技術情報、各スクロール対応） ----
			struct SubRow { CString s; COLORREF c; int idx; int h; CFont* font; };
			SubRow items[4]; int n = 0;
			if (!artist.IsEmpty())    { items[n] = { artist,    RGB(225, 225, 225), 1, lineH, &m_fontInfo }; n++; }
			if (!album.IsEmpty())     { items[n] = { album,     RGB(190, 190, 190), 2, lineH, &m_fontInfo }; n++; }
			if (!trackLine.IsEmpty()) { items[n] = { trackLine, RGB(180, 180, 210), 3, lineH, &m_fontInfo }; n++; }
			if (!techLine.IsEmpty())  { items[n] = { techLine,  RGB(130, 210, 230), 4, techH, &m_fontTech }; n++; }

			mem.SetBkMode(TRANSPARENT);
			for (int i = 0; i < n; i++) {
				bool scrolled = DrawInfoScrollRow(mem, tx, y, tw, items[i].h,
					items[i].s, items[i].c, items[i].idx, kBg, items[i].font);
				if (scrolled) m_iscActive = true;
				y += items[i].h;
			}

			if (aero)
				CCC_BlitStretchNF(pDC->m_hDC, m_infoPanelRect.left, m_infoPanelRect.top, w, h, mem.GetSafeHdc(), 0, 0, w, h, kBg);
#if CCUSTOM_AERO_SUPPORT
			else if (CCC_AcrylicCaption(m_hWnd) && CCC_IsWin11() && !CCC_IsAeroEnabled())
				CCC_BlitStretchOpaque(pDC->m_hDC, m_infoPanelRect.left, m_infoPanelRect.top, w, h, mem.GetSafeHdc(), 0, 0, w, h);
#endif
			else
				pDC->BitBlt(m_infoPanelRect.left, m_infoPanelRect.top, w, h, &mem, 0, 0, SRCCOPY);
			} // memOk
		}
		// スクロール tick の背圧を解放（描画完了後に次の Post を許可）
		InterlockedExchange(&m_iscScrollPosted, 0);
	}
}

BOOL CMediaPlayerDlg::OnEraseBkgnd(CDC* pDC)
{
	// 描画は OnPaint / Blit で完結させ、ここでは消去しない(チラつき防止)
	return TRUE;
}

void CMediaPlayerDlg::OnPaint()
{
	extern void COgg_ClearGdiPaintPending();
#if CCUSTOM_AERO_SUPPORT
	// アクリル(Win11) パス
	// CCC_PaintAeroGaps は SelectClipRgn を書き換えるため SaveDC/RestoreDC で挟む。
	// preserve=&m_bannerRect でバナーを保護(毎フレームのクリアを防いで点滅なし)。
	// グループボックスは CCC_ClipNoChildren で除外されて白くなるため、
	// RestoreDC 後に個別で CCC_ClearRectChroma を呼んで明示的にクロマクリアする。
	if (savedata.aero == 1 && CCC_IsWin11()) {
		CPaintDC dc(this);
		CRect clipBox; dc.GetClipBox(&clipBox);
		auto clipInside = [](const CRect& clip, const CRect& rect) -> bool {
			return !rect.IsRectEmpty()
				&& rect.PtInRect(clip.TopLeft()) && rect.PtInRect(clip.BottomRight());
		};
		// サイドパネルだけの再描画(曲情報スクロール等)では gap クリアをしない。
		// アクリル gap クリア→再描画の1フレーム空白が曲番号 GDI のちらつきになる。
		const bool sidePanelOnly =
			clipInside(clipBox, m_infoPanelRect) || clipInside(clipBox, m_jacketRect);
		bool hitBanner = !m_bannerRect.IsRectEmpty() && CRect().IntersectRect(&clipBox, &m_bannerRect);
		if (!sidePanelOnly) {
			int saved = dc.SaveDC();
			// ジャケット/曲情報パネルは直後の DrawSidePanels の Blit が全ピクセルを
			// 書き直すため gap クリア不要。バナー無効化(毎フレーム)とパネル無効化
			// (スクロール/曲変更)が同じ WM_PAINT に合流したとき、ここでクリアすると
			// クリア→再描画の1フレーム空白が曲番号 GDI のちらつきになる。
			if (!m_jacketRect.IsRectEmpty())    dc.ExcludeClipRect(&m_jacketRect);
			if (!m_infoPanelRect.IsRectEmpty()) dc.ExcludeClipRect(&m_infoPanelRect);
			CCC_PaintAeroGaps(dc, this, &m_bannerRect);
			dc.RestoreDC(saved);
		}
		if (IsBannerSoft3D()) {
			PresentBannerSoft3D(&dc);
			DrawSidePanels(&dc);
		} else {
			if (hitBanner) BlitVisualizer(&dc);
			DrawSidePanels(&dc);
		}
		DrawHighlightPulse(&dc);
		/* ジャケットにはトラックフェードを掛けない（黒半透明の点滅になる） */
		if (!m_infoPanelRect.IsRectEmpty())
			DrawTrackFadeOverlay(&dc, m_infoPanelRect);
		else if (!m_bannerRect.IsRectEmpty())
			DrawTrackFadeOverlay(&dc, m_bannerRect);
		UpdateUxStatusAndChips();
		DrawUxStatusBand(&dc);
		CCC_CaptionPaint(dc, m_hWnd);
		COgg_ClearGdiPaintPending();
		return;
	}
#endif
	CPaintDC pdc(this);
	CRect clipBox; pdc.GetClipBox(&clipBox);
	bool hitBanner = !m_bannerRect.IsRectEmpty() && CRect().IntersectRect(&clipBox, &m_bannerRect);
	// 非アクリル時は背景をベース色で塗る。バナー/サイドパネルは直後の Blit/GDI で
	// 完全に上書きするため除外する（先に塗ると一瞬フラッシュしてちらつく）。
	{
		CRect rcc; GetClientRect(&rcc);
		int saved = pdc.SaveDC();
		pdc.ExcludeClipRect(&m_bannerRect);
		if (!m_jacketRect.IsRectEmpty())   pdc.ExcludeClipRect(&m_jacketRect);
		if (!m_infoPanelRect.IsRectEmpty()) pdc.ExcludeClipRect(&m_infoPanelRect);
		const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
		if (capH > 0) {
			CRect cap(0, 0, rcc.right, capH);
			pdc.ExcludeClipRect(&cap);
		}
		CBrush bg(COLOR_DIALOG_BG);
#if CCUSTOM_AERO_SUPPORT
		if (capH > 0 && CCC_IsWin11()) {
			CRect body = rcc;
			body.top = capH;
			RECT rc = body;
			BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
			HDC hdcBuf = NULL;
			HPAINTBUFFER hBP = ::BeginBufferedPaint(pdc.GetSafeHdc(), &rc, BPBF_TOPDOWNDIB, &params, &hdcBuf);
			if (hdcBuf && hBP) {
				::FillRect(hdcBuf, &rc, (HBRUSH)bg.GetSafeHandle());
				::BufferedPaintMakeOpaque(hBP, &rc);
				::EndBufferedPaint(hBP, TRUE);
			}
			else {
				pdc.FillRect(&rcc, &bg);
			}
		}
		else
#endif
		{
			pdc.FillRect(&rcc, &bg);
		}
		pdc.RestoreDC(saved);
	}
	if (IsBannerSoft3D()) {
		PresentBannerSoft3D(&pdc);
		DrawSidePanels(&pdc);
	} else {
		if (hitBanner) BlitVisualizer(&pdc);
		DrawSidePanels(&pdc);
	}
	DrawHighlightPulse(&pdc);
	if (!m_infoPanelRect.IsRectEmpty())
		DrawTrackFadeOverlay(&pdc, m_infoPanelRect);
	else if (!m_bannerRect.IsRectEmpty())
		DrawTrackFadeOverlay(&pdc, m_bannerRect);
	UpdateUxStatusAndChips();
	DrawUxStatusBand(&pdc);
	CCC_CaptionPaint(pdc, m_hWnd);
	COgg_ClearGdiPaintPending();
}

HBRUSH CMediaPlayerDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);

#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1 && CCC_IsWin11()) {
		// アクリル/Win11: リスト・エディットを暗色で統一(ベースクラスの薄ピンクを上書き)。
		// CTLCOLOR_STATIC/BTN はベースクラスが NULL_BRUSH+黒テキストを返す(そのまま使う)。
		static CBrush s_brListAero(RGB(25, 25, 30));
		static CBrush s_brEditAero(RGB(22, 22, 28));
		if (nCtlColor == CTLCOLOR_LISTBOX) {
			pDC->SetBkColor(RGB(25, 25, 30));
			pDC->SetTextColor(RGB(200, 200, 210));
			return (HBRUSH)s_brListAero.GetSafeHandle();
		}
		if (nCtlColor == CTLCOLOR_EDIT) {
			pDC->SetBkColor(RGB(22, 22, 28));
			pDC->SetTextColor(RGB(200, 200, 210));
			return (HBRUSH)s_brEditAero.GetSafeHandle();
		}
		// CTLCOLOR_STATIC / CTLCOLOR_BTN (グループボックス含む):
		// ベースクラスは黒テキスト+NULL_BRUSH だが、アクリル越しでは見えない場合がある。
		// 白テキストに変更(ガラス上でどんな背景でも視認可)。
		if (nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_BTN) {
			pDC->SetBkMode(TRANSPARENT);
			pDC->SetTextColor(RGB(230, 230, 230));
			return (HBRUSH)GetStockObject(NULL_BRUSH);
		}
		return hbr;
	}
#endif

	// 非アクリル: ダイアログ背景色で統一
	if (savedata.aero != 1) {
		if (m_brDlg.GetSafeHandle() == NULL)
			m_brDlg.CreateSolidBrush(COLOR_DIALOG_BG);
		if (nCtlColor == CTLCOLOR_DLG)
			return m_brDlg;
		if (nCtlColor == CTLCOLOR_STATIC) {
			pDC->SetBkMode(TRANSPARENT);
			return m_brDlg;
		}
	}
	return hbr;
}

void CMediaPlayerDlg::OnDropFiles(HDROP hDropInfo)
{
	UINT n = ::DragQueryFile(hDropInfo, 0xFFFFFFFF, NULL, 0);
	for (UINT i = 0; i < n; i++) {
		TCHAR path[MAX_PATH];
		if (::DragQueryFile(hDropInfo, i, path, MAX_PATH) > 0) {
			if (MpIsPlaylistExtension(path)) {
				MpShowM3uImportDialog(this, path);
				::DragFinish(hDropInfo);
				return;
			}
		}
	}
	if (pl)
		pl->OnDropFiles(hDropInfo);
	RefreshList(TRUE);
	::DragFinish(hDropInfo);
}

void CMediaPlayerDlg::OnDestroy()
{
	SavePos();
	KillTimer(1);
	KillTimer(2);
	KillTimer(3);
	KillTimer(4);
	KillTimer(6);
	KillTimer(7);
	KillTimer(8);
	KillTimer(9);
	if (::IsWindow(m_list.GetSafeHwnd()))
		RemoveWindowSubclass(m_list.GetSafeHwnd(), ListHeaderNotifySubclassProc, kMpListHdrSubclassId);
	if (g_mpHelpDlg && ::IsWindow(g_mpHelpDlg->GetSafeHwnd()))
		g_mpHelpDlg->DestroyWindow();
	if (CMpHelpDlg* help = CMpHelpDlg::Instance()) {
		if (::IsWindow(help->GetSafeHwnd()))
			help->DestroyWindow();
	}
	MpAddonsShutdownAll();
	CloseDeviceRecordIfOpen();
	CCustomBlurDialogExBase::OnDestroy();
}

void CMediaPlayerDlg::OnClose()
{
	// メディアプレイヤー画面の×はアプリ終了(メイン画面を閉じる)
	RequestAppShutdown();
}

void CMediaPlayerDlg::OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
	CSliderCtrl* r = (CSliderCtrl*)pScrollBar;
	if (r && r->GetSafeHwnd() == m_seek.GetSafeHwnd()) {
		const int tgt = m_seek.GetDragTarget();
		extern int hsc;
		if (nSBCode == SB_THUMBTRACK) {
			m_seekDragging = 1;
			m_seekHoldUntil = 0; // 新しいドラッグでホールド解除
			// ドラッグ中に timerp が og->m_time を旧 playb で上書き→離した瞬間に棒が戻るのを防ぐ
			if (tgt == 3 && hsc == 0)
				hsc = 1;
			if (tgt == 4 || tgt == 5) {
				// A-B つまみドラッグ中 → 別変数へ即反映
				m_seek.GetAB(m_abApos, m_abBpos);
			}
			else if (tgt == 1 || tgt == 2) {
				// loop1/2 つまみドラッグ中 → グローバル/プレイリストへ即反映
				int a = 0, b = 0;
				m_seek.GetSelection(a, b);
				extern int loop1, loop2;
				loop1 = a;
				loop2 = (b > a) ? (b - a) : 0;
				if (og && ::IsWindow(og->GetSafeHwnd()))
					og->m_time.SetSelection(a, b);
				if (pl && plcnt >= 0 && plcnt < pl->playcnt) {
					pl->pc[plcnt].loop1 = loop1;
					pl->pc[plcnt].loop2 = loop2;
				}
			}
			CCustomBlurDialogExBase::OnHScroll(nSBCode, nPos, pScrollBar);
			return;
		}
		if (nSBCode == TB_ENDTRACK) {
			const int cueHit = m_seek.GetCueClick();
			if (cueHit >= 0) {
				m_seek.ClearCueClick();
				JumpToCueIndex(cueHit);
				m_seekDragging = 0;
				if (hsc == 1) hsc = 0;
				CCustomBlurDialogExBase::OnHScroll(nSBCode, nPos, pScrollBar);
				return;
			}
			const int lrcHit = m_seek.GetLrcClick();
			if (lrcHit >= 0) {
				m_seek.ClearLrcClick();
				const int fr = m_seek.GetLrcFrame(lrcHit);
				if (fr >= 0 && og && ::IsWindow(og->GetSafeHwnd())) {
					og->m_time.SetPos(fr);
					og->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, fr), (LPARAM)og->m_time.GetSafeHwnd());
					if (m_abApos < 0)
						m_abApos = fr;
					else if (m_abBpos <= m_abApos)
						m_abBpos = fr;
					m_seek.SetAB(m_abApos, m_abBpos);
				}
				m_seekDragging = 0;
				if (hsc == 1) hsc = 0;
				CCustomBlurDialogExBase::OnHScroll(nSBCode, nPos, pScrollBar);
				return;
			}
			if (tgt == 99) {
				savedata.mpBeatGridOffsetMs = m_seek.GetBeatGridOffsetMs();
				SongParams_SaveKeyGridForCurrentSong();
			}
			else if (tgt == 4 || tgt == 5) {
				m_seek.GetAB(m_abApos, m_abBpos);
			}
			else if (tgt == 1 || tgt == 2) {
				int a = 0, b = 0;
				m_seek.GetSelection(a, b);
				extern int loop1, loop2;
				loop1 = a;
				loop2 = (b > a) ? (b - a) : 0;
				if (og && ::IsWindow(og->GetSafeHwnd()))
					og->m_time.SetSelection(a, b);
				if (pl && plcnt >= 0 && plcnt < pl->playcnt) {
					pl->pc[plcnt].loop1 = loop1;
					pl->pc[plcnt].loop2 = loop2;
				}
			}
			m_seekDragging = 0;
			if (hsc == 1) hsc = 0;
			CCustomBlurDialogExBase::OnHScroll(nSBCode, nPos, pScrollBar);
			return;
		}
		if (nSBCode == SB_THUMBPOSITION || nSBCode == SB_ENDSCROLL ||
			nSBCode == SB_PAGELEFT || nSBCode == SB_PAGERIGHT ||
			nSBCode == SB_LINELEFT || nSBCode == SB_LINERIGHT) {
			// クリックのみ(THUMBTRACK無し)でもシーク中ミラーを止め、timerp 上書きを抑止
			m_seekDragging = 1;
			if (hsc == 0) hsc = 1;
			int p = m_seek.GetPos();
			if (og && ::IsWindow(og->GetSafeHwnd())) {
				// og 側の m_time を動かして og の既存シーク処理を流用
				og->m_time.SetPos(p);
				og->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, p), (LPARAM)og->m_time.GetSafeHwnd());
			}
			// decode が追いつくまで棒を確定位置に固定（離した直後の一瞬戻り対策）
			m_seekHoldPos = p;
			m_seekHoldUntil = GetTickCount64() + 800;
			m_seekDragging = 0;
			if (hsc == 1) hsc = 0;
		}
	}
	else if (r && r->GetSafeHwnd() == m_vol.GetSafeHwnd()) {
		int v = m_vol.GetPos();
		if (og && ::IsWindow(og->GetSafeHwnd()))
			og->m_sl.SetPos(v * 1000);
		CString vs; vs.Format(_T("!@C206830%.1f%%"), (double)v); m_volval.SetWindowText(vs);
	}
	else if (r && m_miclev.GetSafeHwnd() && r->GetSafeHwnd() == m_miclev.GetSafeHwnd()) {
		int lv = m_miclev.GetPos();
		if (lv < 0) lv = 0;
		if (lv > 200) lv = 200;
		savedata.mic_mix_level = lv;
		if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_miclev.GetSafeHwnd()) {
			og->m_miclev.SetPos(lv);
			og->ApplyMicMixLevelLabel();
		}
		if (nSBCode == SB_ENDSCROLL || nSBCode == SB_THUMBPOSITION)
			MpPersistSavedataQuick();
	}
	else if (og && ::IsWindow(og->GetSafeHwnd()) && r) {
		// サウンド調整スライダー → og の対応スライダーへ反映(timerp がライブ取得)
		HWND h = r->GetSafeHwnd();
		if (h == m_dsvol.GetSafeHwnd())      og->m_dsval.SetPos(m_dsvol.GetPos());
		else if (h == m_kvol.GetSafeHwnd())  og->m_kakuVol.SetPos(m_kvol.GetPos());
		else if (h == m_tempo.GetSafeHwnd()) {
			const int p = m_tempo.GetPos();
			og->m_tempo_sl.SetPos(p);
			tempo = p; // timerp 待ちにせず RB/予想時間と一致させる
			DougaApplyTempoToVideoRate();
		}
		else if (h == m_pitch.GetSafeHwnd()) {
			const int p = m_pitch.GetPos();
			og->m_pitch_sl.SetPos(p);
			pitch = p;
		}
	}
	CCustomBlurDialogExBase::OnHScroll(nSBCode, nPos, pScrollBar);
}

// ----- プレイリスト操作(og/pl 流用) -----
static void MP_PlayIndex(int idx)
{
	if (!pl || idx < 0 || idx >= pl->playcnt) return;
	pl->Get(idx);          // fnn/filen/modesub/loop1/loop2/ret2 をセット + 選択
	plcnt = idx;
	gameon = 0;
	// 前曲の A-B / 緑帯を持ち越さない
	if (mp) {
		mp->m_abApos = -1;
		mp->m_abBpos = -1;
		mp->m_abLoopCount = 0;
		mp->m_seekHoldUntil = 0;
		if (mp->m_seek.GetSafeHwnd())
			mp->m_seek.SetAB(-1, -1);
		mp->ClearWaveOverview();
	}
	MpPushPlayHistory(pl->pc[idx].fol, pl->pc[idx].name);
	if (!OggPrepareResumeBeforePlayback(pl->pc[idx].fol))
		return;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		RequestPlaybackRestart(og->GetSafeHwnd());  // 再生(再演奏)
}

void CMediaPlayerDlg::OnPrev()
{
	MpTaskbarPrevTrack();
}

void CMediaPlayerDlg::OnNext()
{
	MpTaskbarNextTrack();
}

LRESULT CMediaPlayerDlg::OnResumePrompt(WPARAM, LPARAM)
{
	OggRunResumePrompt();
	return 0;
}

void CMediaPlayerDlg::OnPlay()
{
	int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
	if (sel >= 0 && pl) {
		int pcIdx = MpDispToPc(this, sel);
		if (pcIdx >= 0 && pcIdx < pl->playcnt)
			MP_PlayIndex(pcIdx);
		else
			MpTaskbarReplay();
	}
	else
		MpTaskbarReplay();
}

void CMediaPlayerDlg::OnPauseBtn()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->OnPause();
	// OnPause は og->m_ps のみ更新するため、表示中の mp ボタンへ即反映
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SyncPauseButtonUi();
}

void CMediaPlayerDlg::OnStopBtn()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON1, BN_CLICKED), 0);
}

void CMediaPlayerDlg::OnEq()
{
	// Create/Destroy をボタンハンドラ＋Timer3 にネストさせない(稀なクラッシュ対策)
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON59, BN_CLICKED), 0);
}

void CMediaPlayerDlg::OnPiano()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_OGG_TOGGLE_SUBUI, 1, 0);  // 1=piano
}

void CMediaPlayerDlg::OnAnalyzer()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_OGG_TOGGLE_SUBUI, 2, 0);  // 2=analyzer
}

void CMediaPlayerDlg::OnProTools()
{
	extern CProToolsDlg* g_proToolsDlg;
	extern void CloseProToolsIfOpen();
	if (g_proToolsDlg && ::IsWindow(g_proToolsDlg->GetSafeHwnd()))
		CloseProToolsIfOpen();
	else
		OpenProToolsForSelection();
}

void CMediaPlayerDlg::OnFadeout()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON5, BN_CLICKED), 0);  // フェードアウト
}

void CMediaPlayerDlg::OnFolder()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON9, BN_CLICKED), 0);  // フォルダ設定
}

void CMediaPlayerDlg::OnSettings()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON21, BN_CLICKED), 0);  // 設定
}

void CMediaPlayerDlg::OnJacket()
{
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->OnBnmp3jake();   // ジャケット表示(別窓)
}

void CMediaPlayerDlg::OnExit()
{
	RequestAppShutdown();
}

void CMediaPlayerDlg::OnTempoReset()
{
	// ラベルクリックでテンポを 100%(200) に戻す(og の既存処理を流用)
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		og->OnTempoStatic();
		m_tempo.SetPos(og->m_tempo_sl.GetPos());
	}
}

void CMediaPlayerDlg::OnPitchReset()
{
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		og->OnPitchStatic();
		m_pitch.SetPos(og->m_pitch_sl.GetPos());
	}
}

void CMediaPlayerDlg::OnSwitch()
{
	// mp 自身を破棄する処理(EnterFalcomMode)を mp のハンドラ内で直接呼ばず、og へ委ねる
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_MP_ENTER_FALCOM, 0, 0);
}

void CMediaPlayerDlg::OnRenzoku()
{
	if (!pl) return;
	int st = m_renzoku.GetCheck() ? 1 : 0;
	pl->m_renzoku.SetCheck(st);
	pl->OnBnClickedCheck1();   // 既存処理(保存)
}

void CMediaPlayerDlg::OnLoop()
{
	if (!pl) return;
	int st = m_loop.GetCheck() ? 1 : 0;
	pl->m_loop.SetCheck(st);
	pl->OnBnClickedCheck4();   // 既存処理(保存)
}

void CMediaPlayerDlg::OnRandom()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	int st = m_random.GetCheck() ? 1 : 0;
	// 既存ハンドラ(OnCheck5=ランダム / OnCheck6=順次)を WM_COMMAND で流用
	if (st) og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK5, BN_CLICKED), 0);
	else    og->SendMessage(WM_COMMAND, MAKEWPARAM(IDC_CHECK6, BN_CLICKED), 0);
}

void CMediaPlayerDlg::SyncPlayXfadeUi(BOOL fromSavedata)
{
	if (fromSavedata) {
		if (m_xfade.GetSafeHwnd())
			m_xfade.SetCheck(savedata.play_xfade ? BST_CHECKED : BST_UNCHECKED);
		if (m_xfadeSec.GetSafeHwnd()) {
			CString s;
			s.Format(_T("%.2f"), (double)savedata.play_xfade_sec100 / 100.0);
			m_xfadeSec.SetWindowText(s);
		}
	}
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		if (og->m_xfade.GetSafeHwnd())
			og->m_xfade.SetCheck(savedata.play_xfade ? BST_CHECKED : BST_UNCHECKED);
		if (og->m_xfadeSec.GetSafeHwnd()) {
			CString s;
			s.Format(_T("%.2f"), (double)savedata.play_xfade_sec100 / 100.0);
			og->m_xfadeSec.SetWindowText(s);
		}
	}
}

void CMediaPlayerDlg::OnPlayXfade()
{
	savedata.play_xfade = m_xfade.GetCheck() ? 1 : 0;
	TCHAR tmp_savedir[1024];
	_tgetcwd(tmp_savedir, 1000);
	DatArc_Chdir();
	CFile ab;
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
		ab.Write(&savedata, sizeof(save));
		ab.Close();
		DatArc_Commit(L"oggYSEDbgmu.dat");
	}
	_tchdir(tmp_savedir);
	SyncPlayXfadeUi(TRUE);
}

void CMediaPlayerDlg::OnPlayXfadeSec()
{
	CString s;
	m_xfadeSec.GetWindowText(s);
	double sec = _tstof(s);
	if (sec < 0.1) sec = 0.1;
	if (sec > 120.0) sec = 120.0;
	savedata.play_xfade_sec100 = (int)(sec * 100.0 + 0.5);
	TCHAR tmp_savedir[1024];
	_tgetcwd(tmp_savedir, 1000);
	DatArc_Chdir();
	CFile ab;
	if (ab.Open(L"oggYSEDbgmu.dat", CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
		ab.Write(&savedata, sizeof(save));
		ab.Close();
		DatArc_Commit(L"oggYSEDbgmu.dat");
	}
	_tchdir(tmp_savedir);
	SyncPlayXfadeUi(TRUE);
}

void CMediaPlayerDlg::OnDblclkList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
	if (sel >= 0)
		MP_PlayIndex(MpDispToPc(this, sel));
}

// pl のプレイリスト一覧コンボをそのままミラー(全項目 enabled / 論理=物理)
void CMediaPlayerDlg::ReloadPlaylistCombo()
{
	if (!pl || !::IsWindow(pl->m_listchange.GetSafeHwnd()) || !::IsWindow(m_plsel.GetSafeHwnd()))
		return;
	int n = pl->m_listchange.GetCount();
	m_plsel.ResetContent();
	for (int i = 0; i < n; i++) {
		CString s;
		pl->m_listchange.GetLBText(i, s);
		m_plsel.AddString(s);
	}
	m_plsel.SetCurSel(savedata.playlistnum);
	FixPlselDropList(m_plsel, MpPlselListRowH(hD2), MpPlselClosedH(hD2));
	m_lastComboCount = n;
}

void CMediaPlayerDlg::OnPlselDropdown()
{
	FixPlselDropList(m_plsel, MpPlselListRowH(hD2), MpPlselClosedH(hD2));
	ExpandPlselDropListPopup(m_plsel.GetSafeHwnd());
	PostMessage(WM_MP_PLSEL_EXPAND, 0, 0);
}

LRESULT CMediaPlayerDlg::OnPlselExpandPopup(WPARAM, LPARAM)
{
	if (!::IsWindow(m_plsel.GetSafeHwnd())) return 0;
	ExpandPlselDropListPopup(m_plsel.GetSafeHwnd());
	return 0;
}

// mp リストの選択状態を pl リストへ反映(上下移動/削除を pl の既存処理へ委譲するため)
void CMediaPlayerDlg::SyncSelectionToPlaylist()
{
	if (!pl || !::IsWindow(pl->m_lc.GetSafeHwnd())) return;
	for (int i = 0; i < pl->playcnt; i++)
		pl->m_lc.SetItemState(i, 0, LVIS_SELECTED);
	int n = m_list.GetItemCount();
	for (int i = 0; i < n; i++) {
		UINT st = m_list.GetItemState(i, LVIS_SELECTED);
		if (!(st & LVIS_SELECTED)) continue;
		int pcIdx = MpDispToPc(this, i);
		if (pcIdx >= 0 && pcIdx < pl->playcnt)
			pl->m_lc.SetItemState(pcIdx, LVIS_SELECTED, LVIS_SELECTED);
	}
}

extern BOOL changeflg;

void CMediaPlayerDlg::OnPlSel()
{
	if (!pl || !::IsWindow(pl->m_listchange.GetSafeHwnd())) return;
	if (pl->m_tempMode) return;
	int sel = m_plsel.GetCurSel();
	if (sel < 0) return;
	if (sel == savedata.playlistnum && pl->pc != NULL && pl->playcnt > 0)
		return;
	changeflg = TRUE;
	pl->m_listchange.SetCurSel(sel);
	changeflg = FALSE;
	pl->OnCbnSelchangeCombo1();   // プレイリスト切替/新規作成(既存処理)
	ReloadPlaylistCombo();
	RefreshList(TRUE);
}

void CMediaPlayerDlg::OnPlRename()
{
	if (!pl) return;
	if (pl->m_tempMode) return;
	pl->OnBnClickedButton3();
	ReloadPlaylistCombo();
}

void CMediaPlayerDlg::OnPlDelete()
{
	if (!pl) return;
	if (pl->m_tempMode) return;
	pl->OnBnClickedPlaydelete();
	ReloadPlaylistCombo();
	RefreshList(TRUE);
}

// 選択行の並べ替え。mp リストの選択を基準に pl->pc を直接並べ替える。
// 旧実装は pl->m_lc(裏側の隠しリスト)の選択状態に依存していたため、
// 選択同期のズレや一番上/上ボタンの取り違え、OnSDOWN のヒープ範囲外書込みで
// 「位置がおかしい / 行が消える / 以降の曲が消える」不具合が発生していた。
// ここでは配列を直接操作し、再生インデックスと選択を確実に追従させる。
void CMediaPlayerDlg::MoveSelected(int mode)
{
	if (!pl || pl->pc == NULL || pl->playcnt <= 0) return;
	if (!::IsWindow(m_list.GetSafeHwnd())) return;
	const int n = pl->playcnt;

	// mp リストの選択フラグを取得(表示行→pc へマップ。フィルタ時も正しい)
	char* sel = (char*)calloc((size_t)n, 1);
	if (!sel) return;
	int selCount = 0;
	int k = -1;
	while ((k = m_list.GetNextItem(k, LVNI_SELECTED)) != -1) {
		int pcIdx = MpDispToPc(this, k);
		if (pcIdx >= 0 && pcIdx < n) { sel[pcIdx] = 1; selCount++; }
	}
	if (selCount == 0 || selCount == n) { free(sel); return; }  // 何もない/全選択は移動不要

	// arr[新しい位置] = 元のインデックス
	int* arr = (int*)malloc(sizeof(int) * (size_t)n);
	if (!arr) { free(sel); return; }
	int idx = 0;
	if (mode == 0) {                       // 一番上
		for (int i = 0; i < n; i++) if (sel[i]) arr[idx++] = i;
		for (int i = 0; i < n; i++) if (!sel[i]) arr[idx++] = i;
	} else if (mode == 3) {                // 一番下
		for (int i = 0; i < n; i++) if (!sel[i]) arr[idx++] = i;
		for (int i = 0; i < n; i++) if (sel[i]) arr[idx++] = i;
	} else {
		for (int i = 0; i < n; i++) arr[i] = i;
		if (mode == 1) {                   // 上へ1つ(選択ブロックは結合したまま)
			for (int i = 1; i < n; i++)
				if (sel[arr[i]] && !sel[arr[i - 1]]) { int t = arr[i]; arr[i] = arr[i - 1]; arr[i - 1] = t; }
		} else {                           // 下へ1つ
			for (int i = n - 2; i >= 0; i--)
				if (sel[arr[i]] && !sel[arr[i + 1]]) { int t = arr[i]; arr[i] = arr[i + 1]; arr[i + 1] = t; }
		}
	}

	BOOL changed = FALSE;
	for (int i = 0; i < n; i++) if (arr[i] != i) { changed = TRUE; break; }
	if (!changed) { free(arr); free(sel); return; }

	// 並べ替え後の pc を作成し、元→新の位置写像 pos を作る
	playlistdata0* np = (playlistdata0*)malloc(sizeof(playlistdata0) * (size_t)n);
	int* pos = (int*)malloc(sizeof(int) * (size_t)n);
	if (!np || !pos) { free(np); free(pos); free(arr); free(sel); return; }
	for (int i = 0; i < n; i++) { np[i] = pl->pc[arr[i]]; pos[arr[i]] = i; }   // 再生中アイコン等もそのまま追従
	memcpy(pl->pc, np, sizeof(playlistdata0) * (size_t)n);
	free(np);

	// 再生インデックスを追従
	if (plcnt >= 0 && plcnt < n) plcnt = pos[plcnt];
	if (pl->pnt >= 0 && pl->pnt < n) pl->pnt = pos[pl->pnt];
	if (pl->pnt1 >= 0 && pl->pnt1 < n) pl->pnt1 = pos[pl->pnt1];

	// 裏の pl->m_lc の選択も合わせておく(Falcom 画面との整合)
	if (::IsWindow(pl->m_lc.GetSafeHwnd())) {
		for (int i = 0; i < n; i++)
			pl->m_lc.SetItemState(i, sel[arr[i]] ? LVIS_SELECTED : 0, LVIS_SELECTED);
	}

	pl->Save();
	RefreshList(TRUE);

	// mp リストの選択を移動後の行へ追従させる
	int total = m_list.GetItemCount();
	for (int i = total - 1; i >= 0; i--)
		m_list.SetItemState(i, 0, LVIS_SELECTED | LVIS_FOCUSED);
	int firstSel = -1;
	for (int i = 0; i < n && i < total; i++) {
		if (sel[arr[i]]) {
			m_list.SetItemState(i, LVIS_SELECTED | (firstSel < 0 ? LVIS_FOCUSED : 0), LVIS_SELECTED | LVIS_FOCUSED);
			if (firstSel < 0) firstSel = i;
		}
	}
	if (firstSel >= 0) m_list.EnsureVisible(firstSel, FALSE);
	// FollowPlayingRow が選択を奪わないように、再生行の追従基準を更新しておく
	m_lastScroll = (pl->pnt >= 0 && pl->pnt < n) ? pl->pnt : firstSel;

	free(pos); free(arr); free(sel);
}

void CMediaPlayerDlg::OnMoveTop()
{
	MoveSelected(0);
}

void CMediaPlayerDlg::OnMoveUp()
{
	MoveSelected(1);
}

void CMediaPlayerDlg::OnMoveDown()
{
	MoveSelected(2);
}

void CMediaPlayerDlg::OnMoveBottom()
{
	MoveSelected(3);
}

void CMediaPlayerDlg::OnItemDel()
{
	if (!pl) return;
	SyncSelectionToPlaylist();
	pl->Del();
	RefreshList(TRUE);
}

void CMediaPlayerDlg::OnSupe()
{
	if (og && ::IsWindow(og->m_supe.GetSafeHwnd())) {
		int st = (m_supe.GetCheck() == BST_CHECKED) ? 1 : 0;
		og->m_supe.SetCheck(st);
	}
	SyncPushToggleButtons();
}

void CMediaPlayerDlg::OnSt()
{
	if (og && ::IsWindow(og->m_st.GetSafeHwnd())) {
		int st = (m_st.GetCheck() == BST_CHECKED) ? 1 : 0;
		og->m_st.SetCheck(st);
	}
	SyncPushToggleButtons();
}

void CMediaPlayerDlg::OnPrompt()
{
	MpTogglePromptDialog(this);
	SyncPushToggleButtons();
}

void CMediaPlayerDlg::OnCmdRoll()
{
	MpToggleCommandRollDialog(this);
	SyncPushToggleButtons();
}

void CMediaPlayerDlg::OnM3uExport()
{
	if (!pl || pl->playcnt <= 0) {
		AfxMessageBox(LL14(L"書き出す曲がありません。", L"No tracks to export.", L"Aucune piste.", L"Nessuna traccia.", L"Sin pistas.", L"내보낼 곡이 없습니다.", L"没有可导出的曲目。", L"لا مقاطع.", L"Нет треков.", L"Keine Titel.", L"Sem faixas.", L"Geen nummers.", L"Brak utworow.", L"Disa aktarilacak parca yok."));
		return;
	}
	CFileDialog fd(FALSE, _T("m3u"), _T("playlist.m3u"),
		OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		LL14(L"M3U プレイリスト (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|すべて (*.*)|*.*||",
			L"M3U playlist (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|All (*.*)|*.*||",
			L"Liste M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Tous (*.*)|*.*||",
			L"Playlist M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Tutti (*.*)|*.*||",
			L"Lista M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Todos (*.*)|*.*||",
			L"M3U 재생목록 (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|모든 (*.*)|*.*||",
			L"M3U 播放列表 (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|全部 (*.*)|*.*||",
			L"قوائم M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|الكل (*.*)|*.*||",
			L"Плейлист M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Все (*.*)|*.*||",
			L"M3U-Playlist (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Alle (*.*)|*.*||",
			L"Lista M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Todos (*.*)|*.*||",
			L"M3U-playlist (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Alle (*.*)|*.*||",
			L"Lista M3U (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Wszystkie (*.*)|*.*||",
			L"M3U listesi (*.m3u)|*.m3u|UTF-8 M3U (*.m3u8)|*.m3u8|Tumu (*.*)|*.*||"),
		this);
	if (fd.DoModal() != IDOK) return;
	CString ext = fd.GetFileExt(); ext.MakeLower();
	// .m3u も UTF-8 BOM で出力(日本語パス/曲名を正しく保持)。.m3u8 も同様。
	BOOL utf8 = TRUE;
	UNREFERENCED_PARAMETER(ext);
	if (!MpExportPlaylistM3U(fd.GetPathName(), utf8))
		AfxMessageBox(LL14(L"書き出しに失敗しました。", L"Export failed.", L"Echec export.", L"Esportazione fallita.", L"Error al exportar.", L"내보내기 실패.", L"导出失败。", L"فشل التصدير.", L"Ошибка экспорта.", L"Export fehlgeschlagen.", L"Falha na exportacao.", L"Exporteren mislukt.", L"Eksport nieudany.", L"Disa aktarma basarisiz."));
}

void CMediaPlayerDlg::OnM3uImport()
{
	MpShowM3uImportDialog(this);
}

BOOL CMediaPlayerDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (HIWORD(wParam) == THBN_CLICKED) {
		UINT id = LOWORD(wParam);
		if (id == 0) OnPlay();
		else if (id == 1) OnPauseBtn();
		else if (id == 2) OnStopBtn();
		else if (id == 3) OnNext();
		return TRUE;
	}
	const UINT cmd = LOWORD(wParam);
	if (MpFeatHandleKeyMenuCmd(cmd)) return TRUE;
	if (MpFeatHandleChapterMenuCmd(cmd, GetSelectedPcIndex())) return TRUE;
	if (cmd == ID_MP_FOCUS_MODE) {
		MpFeatApplyFocusMode(this, savedata.mpFocusMode ? FALSE : TRUE);
		MpPersistSavedataQuick();
		return TRUE;
	}
	if (cmd == ID_MP_CONFIRM_DANGER) {
		savedata.confirmDanger = savedata.confirmDanger ? 0 : 1;
		MpPersistSavedataQuick();
		return TRUE;
	}
	if (cmd == ID_MP_LIVE_SET_REC) {
		MpFeatLiveSetRecordStart(this);
		return TRUE;
	}
	if (cmd == ID_MP_NOWPLAYING_FILE) {
		savedata.mpNowPlayingFile = savedata.mpNowPlayingFile ? 0 : 1;
		if (savedata.mpNowPlayingFile) MpFeatWriteNowPlaying();
		MpPersistSavedataQuick();
		return TRUE;
	}
	if (cmd == ID_MP_MIDI_LEARN) {
		savedata.mpMidiLearn = savedata.mpMidiLearn ? 0 : 1;
		MpPersistSavedataQuick();
		return TRUE;
	}
	if (cmd == ID_MP_MIRROR_CUE) {
		savedata.mpMirrorCueMode = savedata.mpMirrorCueMode ? 0 : 1;
		MpPersistSavedataQuick();
		return TRUE;
	}
	if (cmd == ID_MP_PHRASE_SNAP) {
		savedata.mpPhraseSnapBeat = savedata.mpPhraseSnapBeat ? 0 : 1;
		MpPersistSavedataQuick();
		return TRUE;
	}
	if (cmd >= ID_MP_TRANS_PRE_0 && cmd <= ID_MP_TRANS_PRE_2) {
		savedata.mpTransPreset = (int)(cmd - ID_MP_TRANS_PRE_0);
		// プリセット: EQ ハイパス気味 / ローカット / クロスフェード秒
		if (savedata.mpTransPreset == 0) {
			savedata.wav_export_xfade_sec = 4;
			savedata.eq[0] = 70; savedata.eq[1] = 80; // 低域下げスイープ開始相当
		} else if (savedata.mpTransPreset == 1) {
			savedata.wav_export_xfade_sec = 8;
			savedata.eq[18] = 60; // フィルタ寄り
		} else {
			savedata.wav_export_xfade_sec = 2;
		}
		MpPersistSavedataQuick();
		MessageBox(LL14(L"次曲へトランジション設定を適用しました（クロスフェード秒・EQヒント）。",
			L"Transition preset applied for next track (xfade sec / EQ hint).",
			L"Preset transition applique.", L"Preset transizione applicato.", L"Preset de transicion aplicado.",
			L"다음 곡 전환 프리셋 적용.", L"已将过渡预设应用到下一曲。", L"تم تطبيق إعداد الانتقال.",
			L"Пресет перехода применён.", L"Übergangs-Preset angewendet.", L"Preset de transicao aplicado.",
			L"Overgangs-preset toegepast.", L"Preset przejscia zastosowany.", L"Gecis onayari uygulandi."),
			LL14(L"トランジション", L"Transition", L"Transition", L"Transizione", L"Transicion", L"전환", L"过渡", L"انتقال", L"Переход", L"Übergang", L"Transicao", L"Overgang", L"Przejscie", L"Gecis"),
			MB_OK | MB_ICONINFORMATION);
		return TRUE;
	}
	if (cmd >= ID_MP_LAYOUT_SAVE0 && cmd < ID_MP_LAYOUT_SAVE0 + 3) {
		const int slot = (int)(cmd - ID_MP_LAYOUT_SAVE0);
		CRect wr; GetWindowRect(&wr);
		savedata.mpLayoutX[slot] = wr.left;
		savedata.mpLayoutY[slot] = wr.top;
		savedata.mpLayoutW[slot] = wr.Width();
		savedata.mpLayoutH[slot] = wr.Height();
		savedata.mpLayoutFlags[slot] = savedata.mpBotToolsFlags;
		savedata.mpLayoutPreset = slot;
		MpPersistSavedataQuick();
		return TRUE;
	}
	if (cmd >= ID_MP_LAYOUT_LOAD0 && cmd < ID_MP_LAYOUT_LOAD0 + 3) {
		const int slot = (int)(cmd - ID_MP_LAYOUT_LOAD0);
		if (savedata.mpLayoutW[slot] > 100 && savedata.mpLayoutH[slot] > 100) {
			SetWindowPos(NULL, savedata.mpLayoutX[slot], savedata.mpLayoutY[slot],
				savedata.mpLayoutW[slot], savedata.mpLayoutH[slot], SWP_NOZORDER);
			savedata.mpBotToolsFlags = savedata.mpLayoutFlags[slot];
			savedata.mpLayoutPreset = slot;
			DoLayout();
			MpPersistSavedataQuick();
		}
		return TRUE;
	}
	if (cmd == ID_MP_WEEKLY_SUMMARY) {
		int plays = 0;
		FILETIME newest = {};
		for (int i = 0; pl && i < pl->playcnt; ++i) {
			plays += ProAudio_GetPlayCount(MpCurListName(), pl->pc[i].fol, pl->pc[i].sub, pl->pc[i].ret2);
			FILETIME ft;
			if (ProAudio_GetLastPlay(MpCurListName(), pl->pc[i].fol, pl->pc[i].sub, pl->pc[i].ret2, ft)) {
				if (CompareFileTime(&ft, &newest) > 0) newest = ft;
			}
		}
		CString msg;
		msg.Format(LL14(L"プレイリスト再生回数合計: %d\n（簡易週次サマリ）",
			L"Total playlist play count: %d\n(simple weekly summary)",
			L"Total lectures: %d", L"Totale riproduzioni: %d", L"Total reproducciones: %d",
			L"재생 횟수 합계: %d", L"播放次数合计: %d", L"إجمالي التشغيل: %d",
			L"Сумма проигрываний: %d", L"Summe Wiedergaben: %d", L"Total de plays: %d",
			L"Totaal plays: %d", L"Suma odtworzen: %d", L"Toplam calma: %d"), plays);
		MessageBox(msg, LL14(L"週次サマリ", L"Weekly summary", L"Resume hebdo", L"Riepilogo settimanale", L"Resumen semanal",
			L"주간 요약", L"周汇总", L"ملخص أسبوعي", L"Недельная сводка", L"Wochenübersicht", L"Resumo semanal", L"Weekoverzicht", L"Podsumowanie tygodnia", L"Haftalik ozet"), MB_OK);
		return TRUE;
	}
	if (cmd == ID_MP_AAC_PROF0 || cmd == ID_MP_AAC_PROF1 || cmd == ID_MP_AAC_PROF2) {
		savedata.mpAacProfile = (int)(cmd - ID_MP_AAC_PROF0);
		MpPersistSavedataQuick();
		return TRUE;
	}
	if (cmd == ID_MP_PRACTICE_LOG) {
		CString path;
		TCHAR dir[MAX_PATH] = {};
		if (::GetModuleFileName(NULL, dir, MAX_PATH)) {
			CString base(dir);
			const int slash = base.ReverseFind(_T('\\'));
			if (slash >= 0) base = base.Left(slash + 1);
			path = base + L"practice_log.txt";
			CString line;
			line.Format(L"%d%% tempo, loops=%d\r\n", (int)TempoPercentFromPos(tempo), m_abLoopCount);
			CFile f;
			if (f.Open(path, CFile::modeCreate | CFile::modeNoTruncate | CFile::modeWrite | CFile::shareDenyWrite, NULL) == TRUE) {
				f.SeekToEnd();
				const CStringA a = CW2A(line, CP_UTF8);
				f.Write((LPCSTR)a, a.GetLength());
				f.Close();
			}
		}
		return TRUE;
	}
	if (cmd == ID_MP_PRACTICE_PACK) {
		OnAbPackExport();
		return TRUE;
	}
	return CCustomBlurDialogExBase::OnCommand(wParam, lParam);
}

void CMediaPlayerDlg::OnTip()
{
	if (pl && ::IsWindow(pl->m_tool.GetSafeHwnd()))
		pl->m_tool.SetCheck(m_tip.GetCheck() ? 1 : 0);   // pl のタイマーが反映
	ApplyListTooltipState();   // m_list にも即時反映
}

void CMediaPlayerDlg::OnMini()
{
	if (pl && ::IsWindow(pl->m_saisyo.GetSafeHwnd()))
		pl->m_saisyo.SetCheck(m_mini.GetCheck() ? 1 : 0);
}

void CMediaPlayerDlg::OnSaveMp3()
{
	if (!pl) return;
	pl->m_save_mp3.SetCheck(m_savemp3.GetCheck() ? 1 : 0);
	pl->OnBnClickedCheck6mp3();   // savedata へ保存(既存処理)
}

void CMediaPlayerDlg::OnSaveDs()
{
	if (!pl) return;
	pl->m_save_kpi.SetCheck(m_saveds.GetCheck() ? 1 : 0);
	pl->OnBnClickedCheck7dshow();
}

void CMediaPlayerDlg::OnSaveWav()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	og->m_c2.SetCheck(m_savewav.GetCheck() ? 1 : 0);
}

void CMediaPlayerDlg::OnCbnSelchangeMicDev()
{
	AudioMicDevApplyFromCombo(m_micdev);
}

void CMediaPlayerDlg::OnAudioDevMenuRange(UINT nID)
{
	if (!AudioMicDevHandleMenuCmd(nID))
		AudioLoopDevHandleMenuCmd(nID);
}

void CMediaPlayerDlg::OnMicMix()
{
	savedata.mic_mix = (m_micmix.GetCheck() == BST_CHECKED) ? 1 : 0;
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		if (og->m_micmix.GetSafeHwnd())
			og->m_micmix.SetCheck(savedata.mic_mix ? BST_CHECKED : BST_UNCHECKED);
		og->OnMicMixCheck();
	} else {
		MpPersistSavedataQuick();
	}
}

void CMediaPlayerDlg::OnMicMixMenuToggle()
{
	savedata.mic_mix = savedata.mic_mix ? 0 : 1;
	if (m_micmix.GetSafeHwnd())
		m_micmix.SetCheck(savedata.mic_mix ? BST_CHECKED : BST_UNCHECKED);
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		if (og->m_micmix.GetSafeHwnd())
			og->m_micmix.SetCheck(savedata.mic_mix ? BST_CHECKED : BST_UNCHECKED);
		og->OnMicMixCheck();
	} else {
		MpPersistSavedataQuick();
	}
}

void CMediaPlayerDlg::OnMicLevRelease(NMHDR*, LRESULT* pResult)
{
	if (pResult) *pResult = 0;
	int lv = m_miclev.GetPos();
	if (lv < 0) lv = 0;
	if (lv > 200) lv = 200;
	savedata.mic_mix_level = lv;
	if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_miclev.GetSafeHwnd()) {
		og->m_miclev.SetPos(lv);
		og->ApplyMicMixLevelLabel();
	}
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::OnRecord()
{
	OpenDeviceRecordModeless(this);
}

void CMediaPlayerDlg::OnCapture()
{
	OpenScreenCaptureModeless(this);
}

void CMediaPlayerDlg::OnBotSleep()
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.SetSkipChrome(TRUE);
	menu.AddCommand(ID_MP_SLEEP_15,
		LL14(L"15 分", L"15 min", L"15 min", L"15 min", L"15 min", L"15분", L"15 分钟", L"15 د", L"15 мин", L"15 Min", L"15 min", L"15 min", L"15 min", L"15 dk"),
		LL14(L"15分後に再生を停止します", L"Stop playback after 15 minutes", L"Arrête la lecture après 15 min", L"Ferma la riproduzione dopo 15 min", L"Detiene la reproducción tras 15 min", L"15분 후 재생을 중지합니다", L"15 分钟后停止播放", L"يوقف التشغيل بعد 15 دقيقة", L"Остановить воспроизведение через 15 мин", L"Wiedergabe nach 15 Min stoppen", L"Para a reprodução após 15 min", L"Stopt afspelen na 15 min", L"Zatrzymaj odtwarzanie po 15 min", L"15 dk sonra calmayi durdurur"));
	menu.AddCommand(ID_MP_SLEEP_30,
		LL14(L"30 分", L"30 min", L"30 min", L"30 min", L"30 min", L"30분", L"30 分钟", L"30 د", L"30 мин", L"30 Min", L"30 min", L"30 min", L"30 min", L"30 dk"),
		LL14(L"30分後に再生を停止します", L"Stop playback after 30 minutes", L"Arrête la lecture après 30 min", L"Ferma la riproduzione dopo 30 min", L"Detiene la reproducción tras 30 min", L"30분 후 재생을 중지합니다", L"30 分钟后停止播放", L"يوقف التشغيل بعد 30 دقيقة", L"Остановить воспроизведение через 30 мин", L"Wiedergabe nach 30 Min stoppen", L"Para a reprodução após 30 min", L"Stopt afspelen na 30 min", L"Zatrzymaj odtwarzanie po 30 min", L"30 dk sonra calmayi durdurur"));
	menu.AddCommand(ID_MP_SLEEP_60,
		LL14(L"60 分", L"60 min", L"60 min", L"60 min", L"60 min", L"60분", L"60 分钟", L"60 د", L"60 мин", L"60 Min", L"60 min", L"60 min", L"60 min", L"60 dk"),
		LL14(L"60分後に再生を停止します", L"Stop playback after 60 minutes", L"Arrête la lecture après 60 min", L"Ferma la riproduzione dopo 60 min", L"Detiene la reproducción tras 60 min", L"60분 후 재생을 중지합니다", L"60 分钟后停止播放", L"يوقف التشغيل بعد 60 دقيقة", L"Остановить воспроизведение через 60 мин", L"Wiedergabe nach 60 Min stoppen", L"Para a reprodução após 60 min", L"Stopt afspelen na 60 min", L"Zatrzymaj odtwarzanie po 60 min", L"60 dk sonra calmayi durdurur"));
	menu.AddCommand(ID_MP_SLEEP_CUSTOM,
		LL14(L"カスタム…", L"Custom…", L"Perso…", L"Personalizzato…", L"Personalizado…",
			L"사용자…", L"自定义…", L"مخصص…", L"Своё…", L"Eigene…",
			L"Personalizado…", L"Aangepast…", L"Wlasne…", L"Ozel…"),
			LL14(L"分単位でスリープ時間を指定します（1〜240）", L"Set a custom sleep time in minutes (1–240)", L"Definir une duree personnalisee (1–240 min)", L"Imposta durata personalizzata (1–240 min)", L"Definir tiempo personalizado (1–240 min)", L"분 단위로 슬립 시간 지정(1–240)", L"自定义睡眠分钟数（1–240）", L"تعيين مدة نوم مخصصة (1–240)", L"Задать своё время сна (1–240 мин)", L"Eigene Schlafzeit in Minuten (1–240)", L"Definir sono personalizado (1–240 min)", L"Aangepaste slaaptijd in minuten (1–240)", L"Ustaw wlasny czas snu (1–240 min)", L"Ozel uyku suresi (1–240 dk)"));
	menu.AddSeparator();
	menu.AddCommand(ID_MP_SLEEP_OFF,
		LL14(L"スリープ解除", L"Sleep off", L"Veille off", L"Sleep off", L"Suspensión off",
			L"슬립 해제", L"关闭睡眠", L"إيقاف النوم", L"Сон выкл", L"Schlaf aus",
			L"Sono off", L"Slaap uit", L"Sen wyl", L"Uyku kapat"),
			LL14(L"スリープタイマーを解除して通常再生に戻します", L"Cancel the sleep timer and keep playing", L"Annuler la minuterie de veille", L"Annulla il timer sleep", L"Cancelar el temporizador de sueño", L"슬립 타이머를 해제합니다", L"取消睡眠定时器", L"إلغاء مؤقت النوم", L"Отменить таймер сна", L"Schlaf-Timer abschalten", L"Cancelar o temporizador de sono", L"Slaaptimer uitzetten", L"Wylacz timer snu", L"Uyku zamanlayicisini kapat"));
	CRect r;
	if (m_botSleep.GetSafeHwnd())
		m_botSleep.GetWindowRect(&r);
	else
		GetCursorPos(&r.TopLeft());
	const UINT cmd = menu.Track(CPoint(r.left, r.bottom), this);
	if (cmd)
		PostMessage(WM_COMMAND, cmd);
}

void CMediaPlayerDlg::ToggleBotVisFlag(int bit)
{
	if (!savedata.mpBotToolsInited) {
		savedata.mpBotToolsInited = 1;
		savedata.mpBotToolsFlags = 0x0F;
	}
	savedata.mpBotToolsFlags ^= bit;
	MpPersistSavedataQuick();
	m_mpBotShort = -1;
	DoLayout();
}

void CMediaPlayerDlg::OnBotVisDj() { ToggleBotVisFlag(1); }
void CMediaPlayerDlg::OnBotVisTag() { ToggleBotVisFlag(2); }
void CMediaPlayerDlg::OnBotVisBpm() { ToggleBotVisFlag(4); }
void CMediaPlayerDlg::OnBotVisSleep() { ToggleBotVisFlag(8); }
void CMediaPlayerDlg::OnBotVisMirror() { ToggleBotVisFlag(16); }
void CMediaPlayerDlg::OnBotVisSsViz() { ToggleBotVisFlag(32); }
void CMediaPlayerDlg::OnBotVisAlarm() { ToggleBotVisFlag(64); }
void CMediaPlayerDlg::OnBotVisRemote() { ToggleBotVisFlag(128); }
void CMediaPlayerDlg::OnBotVisMaze() { ToggleBotVisFlag(256); }

void CMediaPlayerDlg::OnSaveParam()
{
	savedata.saveSongParams = m_saveparam.GetCheck() ? 1 : 0;
	// チェック状態はすぐ .dat へ(再起動でフラグが消えるとツールチップも出ない)
	MpPersistSavedataQuick();
	// ON: ★付きなら保存パラメータを読んで反映 / OFF: 現行値をその曲へ保存反映
	SongParams_Sync(false);
}

void CMediaPlayerDlg::OnResetData()
{
	CString msg = LL14(
		L"曲ごとに保存した設定をすべて削除し、音量・EQ など各種パラメータを初期状態に戻します。よろしいですか？",
		L"Delete all per-song saved settings and reset volume, EQ and other parameters to defaults. Continue?",
		L"Supprimer tous les reglages par morceau et reinitialiser les parametres ?",
		L"Eliminare tutte le impostazioni per brano e ripristinare i parametri?",
		L"¿Eliminar todos los ajustes por pista y restablecer los parámetros?",
		L"곡별 저장 설정을 모두 삭제하고 볼륨·EQ 등 파라미터를 초기화합니다. 계속할까요?",
		L"删除所有逐曲保存的设置，并将音量、EQ等参数重置为默认。是否继续？",
		L"حذف كل الإعدادات المحفوظة لكل أغنية وإعادة الضبط؟",
		L"Удалить все сохранённые настройки для треков и сбросить параметры?",
		L"Alle pro-Titel-Einstellungen loeschen und Parameter zuruecksetzen?",
		L"Excluir todas as configuracoes por faixa e redefinir os parametros?",
		L"Alle per-nummer-instellingen verwijderen en parameters resetten?",
		L"Usunąć wszystkie ustawienia na utwór i zresetować parametry?",
		L"Tüm parça ayarlarını silip parametreleri sıfırlansın mı?");
	if (savedata.confirmDanger) {
		if (!MpFeatConfirmDanger(GetSafeHwnd(), msg))
			return;
	}

	// 保存ファイルとメモリ内テーブルを破棄
	SongParams_ResetAll();

	// ライブのパラメータを既定へ: DS音量50% / 拡張音量100% / その他デフォルト
	SongParam d;
	ZeroMemory(&d, sizeof(d));
	d.dsvol = -249;       // 50%
	d.kakuVol = 100;      // 100%
	d.pitchPos = 200;     // 100%
	d.tempoPos = 200;     // 100%
	for (int i = 0; i < 20; i++) d.eq[i] = 100; // フラット/中立
	d.eqsoundenv = 0;
	d.eqsoundeq = 0;
	d.eqsoundeffect = 50; // 環境のかかり具合 既定
	d.eq_reverb = 0;
	d.eq_chorus = 0;
	d.eq_delay = 0;
	d.analyzerspecstyle = 0;
	SongParams_ApplyEntryToMain(d);
}

void CMediaPlayerDlg::OnKaisuuKillFocus()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	CString s;
	m_kaisuu.GetWindowText(s);
	s.Trim();
	int n = _tstoi(s);
	if (n < 1) n = 1;
	s.Format(_T("%d"), n);
	m_kaisuu.SetWindowText(s);
	og->m_kaisuu.SetWindowText(s);
	savedata.kaisuu = n;
	MpPersistSavedataQuick();
}

// リスト右クリック: 詳細編集 / WAV保存 / 削除 / 記憶パラメータ削除 / 他リスト移動・コピー / 存在しないファイル削除
void CMediaPlayerDlg::OnRclickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	if (!pl) return;
	CPoint pt;
	::GetCursorPos(&pt);
	CPoint clientPt = pt;
	m_list.ScreenToClient(&clientPt);
	const int hit = m_list.HitTest(clientPt, NULL);
	if (hit >= 0 && !(m_list.GetItemState(hit, LVIS_SELECTED) & LVIS_SELECTED)) {
		const int n = m_list.GetItemCount();
		for (int i = 0; i < n; ++i)
			m_list.SetItemState(i, 0, LVIS_SELECTED);
		m_list.SetItemState(hit, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
	}
	SyncSelectionToPlaylist();
	const int cmd = pl->ShowTrackContextMenu(pt, this);
	if (cmd == 0) return;
	if (IsSeekExtrasCommand((UINT)cmd)) {
		SendMessage(WM_COMMAND, (WPARAM)cmd, 0);
		return;
	}
	if (cmd == PL_CTX_MICMIX) {
		savedata.mic_mix = savedata.mic_mix ? 0 : 1;
		if (m_micmix.GetSafeHwnd())
			m_micmix.SetCheck(savedata.mic_mix ? BST_CHECKED : BST_UNCHECKED);
		OnMicMix();
		return;
	}
	if (cmd == PL_CTX_AB_SET_A) { OnAbSetA(); return; }
	if (cmd == PL_CTX_AB_SET_B) { OnAbSetB(); return; }
	if (cmd == PL_CTX_AB_CLEAR) { OnAbClear(); return; }
	if (cmd == PL_CTX_SORT_NAME) { OnSortName(); return; }
	if (cmd == PL_CTX_SORT_ART) { OnSortArt(); return; }
	if (cmd == PL_CTX_SORT_ALB) { OnSortAlb(); return; }
	if (cmd == PL_CTX_SORT_TIME) { OnSortTime(); return; }
	if (cmd == PL_CTX_ADD_FOLDER) { OnAddFolder(); return; }
	if (cmd == PL_CTX_COPY_TITLEART) {
		int sel = m_list.GetNextItem(-1, LVNI_SELECTED);
		int i = MpDispToPc(this, sel);
		if (pl && i >= 0 && i < pl->playcnt) {
			CString s; s.Format(_T("%s - %s"), pl->pc[i].name, pl->pc[i].art);
			if (OpenClipboard()) {
				EmptyClipboard();
				size_t bytes = ((size_t)s.GetLength() + 1) * sizeof(TCHAR);
				HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
				if (h) {
					memcpy(GlobalLock(h), (LPCTSTR)s, bytes);
					GlobalUnlock(h);
#ifdef _UNICODE
					SetClipboardData(CF_UNICODETEXT, h);
#else
					SetClipboardData(CF_TEXT, h);
#endif
				}
				CloseClipboard();
			}
		}
		return;
	}
	if (cmd == PL_CTX_TAG_EDIT) {
		OpenTagEditForSelection();
		return;
	}
	if (cmd == PL_CTX_MB_AUTOTAG) { OnMbAutotag(); return; }
	if (cmd == PL_CTX_BPM) { OnMpBpmDetect(); return; }
	if (cmd == PL_CTX_BPM_CAND1) { OnMpBpmCand1(); return; }
	if (cmd == PL_CTX_BPM_CAND2) { OnMpBpmCand2(); return; }
	if (cmd == PL_CTX_BPM_CAND3) { OnMpBpmCand3(); return; }
	if (cmd == PL_CTX_NORM_SCAN) { OnNormScan(); return; }
	if (cmd == PL_CTX_EXPORT_AB) { OnExportAbNow(); return; }
	if (cmd == PL_CTX_SSVIZ) { OnMpSsViz(); return; }
	if (cmd == PL_CTX_DESK_LRC) { OnDeskLrcToggle(); return; }
	if (cmd == PL_CTX_DUPES) { OnDupesScan(); return; }
	if (cmd == PL_CTX_FOLDER_SYNC) { OnFolderSyncDiff(); return; }
	if (cmd == PL_CTX_QUEUE_ADD) { OnQueueAdd(); return; }
	if (cmd == PL_CTX_QUEUE_PLAYNEXT) { OnQueuePlayNext(); return; }
	if (cmd == PL_CTX_QUEUE_CLEAR) { OnQueueClear(); return; }
	if (MpFeatHandleChapterMenuCmd((UINT)cmd, GetSelectedPcIndex())) return;
	pl->HandleTrackContextCmd(cmd);
}

void CMediaPlayerDlg::OnClickList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMITEMACTIVATE p = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	if (!p || p->iItem < 0) return;
	if (p->iSubItem == 1)
		CycleRatingForDisp(p->iItem);
}

// リストでの DELETE キー押下: 選択曲を削除（プレイリスト本体の Del を流用）
void CMediaPlayerDlg::OnKeydownList(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMLVKEYDOWN pLVKeyDown = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);
	*pResult = 0;
	if (!pl) return;
	if (pLVKeyDown && pLVKeyDown->wVKey == VK_DELETE) {
		SyncSelectionToPlaylist();
		pl->Del();
		RefreshList(TRUE);
	}
}

// あいまい検索: pl のキーワード欄へ転記して pl の検索処理を流用し、結果を mp リストへ反映
void CMediaPlayerDlg::OnFindUp()
{
	if (!pl || !::IsWindow(pl->GetSafeHwnd())) return;
	CString s; m_find.GetWindowText(s);
	if (savedata.mpFindFilter) {
		RefreshList(TRUE);
		if (m_find.GetSafeHwnd()) m_find.SetFocus();
		return;
	}
	pl->m_find.SetWindowText(s);
	pl->OnFindUp();
	int i = pl->pnt1;
	if (i >= 0 && i < pl->playcnt && i < m_list.GetItemCount()) {
		for (int k = m_list.GetItemCount() - 1; k >= 0; k--)
			m_list.SetItemState(k, 0, LVIS_SELECTED);
		m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(i, FALSE);
		m_lastScroll = i;
	}
	if (m_find.GetSafeHwnd()) m_find.SetFocus();
}

void CMediaPlayerDlg::OnFindDown()
{
	if (!pl || !::IsWindow(pl->GetSafeHwnd())) return;
	CString s; m_find.GetWindowText(s);
	if (savedata.mpFindFilter) {
		RefreshList(TRUE);
		if (m_find.GetSafeHwnd()) m_find.SetFocus();
		return;
	}
	pl->m_find.SetWindowText(s);
	pl->OnFindDown();
	int i = pl->pnt1;
	if (i >= 0 && i < pl->playcnt && i < m_list.GetItemCount()) {
		for (int k = m_list.GetItemCount() - 1; k >= 0; k--)
			m_list.SetItemState(k, 0, LVIS_SELECTED);
		m_list.SetItemState(i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
		m_list.EnsureVisible(i, FALSE);
		m_lastScroll = i;
	}
	if (m_find.GetSafeHwnd()) m_find.SetFocus();
}

void CMediaPlayerDlg::OnAbSetA()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	m_abApos = og->m_time.GetPos();
	if (m_abBpos >= 0 && m_abBpos <= m_abApos) m_abBpos = -1;
	m_abLoopCount = 0;
	m_seek.SetAB(m_abApos, m_abBpos);
	MirrorSeekVol();
}

void CMediaPlayerDlg::OnAbSetB()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	m_abBpos = og->m_time.GetPos();
	if (m_abApos < 0) m_abApos = og->m_time.GetMinValue();
	if (m_abBpos <= m_abApos) {
		int t = m_abApos; m_abApos = m_abBpos; m_abBpos = t;
	}
	m_abLoopCount = 0;
	m_seek.SetAB(m_abApos, m_abBpos);
	MirrorSeekVol();
}

void CMediaPlayerDlg::OnAbClear()
{
	m_abApos = -1;
	m_abBpos = -1;
	m_abLoopCount = 0;
	m_seek.SetAB(-1, -1);
	MirrorSeekVol();
}

void CMediaPlayerDlg::OnSeekLock()
{
	if (!m_seekLock.GetSafeHwnd()) return;
	// メニューからの呼び出しはチェックをトグル。BN_CLICKED は既にトグル済み。
	const MSG* msg = GetCurrentMessage();
	if (msg && msg->message == WM_COMMAND && LOWORD(msg->wParam) == ID_MP_SEEK_LOCK)
		m_seekLock.SetCheck((m_seekLock.GetCheck() == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED);
	const BOOL locked = (m_seekLock.GetCheck() == BST_CHECKED);
	savedata.mpSeekLoopUnlock = locked ? 0 : 1;
	m_seek.SetSelectionLocked(locked);
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::OnSeekWaveToggle()
{
	savedata.mpSeekWave = savedata.mpSeekWave ? 0 : 1;
	if (!savedata.mpSeekWave) {
		ClearWaveOverview();
	} else {
		m_wavePath[0] = 0; // 強制再構築
		KickWaveOverview();
	}
	// seekH が変わるので DoLayout 単独では残像・位置ずれが残る（歌詞拡大切替と同方針）
	DoLayout();
	CCC_GroupBoxesBack(GetSafeHwnd());
	RefreshListAfterLayout();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::RefreshSeekCues()
{
	if (!m_seek.GetSafeHwnd()) return;
	extern int loop2;
	int frames[CCustomRangeSliderCtrl::kCueMax];
	int n = ProAudio_CueCount();
	if (n > CCustomRangeSliderCtrl::kCueMax) n = CCustomRangeSliderCtrl::kCueMax;
	const int mn = m_seek.GetMinValue();
	const int mx = m_seek.GetMaxValue();
	for (int i = 0; i < n; ++i) {
		ProCue c;
		if (!ProAudio_CueGet(i, c)) { n = i; break; }
		int pos = c.frame;
		if (mx > mn && pos > mx) {
			const int scaled100 = pos / 100;
			if (scaled100 >= mn && scaled100 <= mx)
				pos = scaled100;
			else if (loop2 > 0)
				pos = (int)((__int64)c.frame * (__int64)(mx - mn) / (__int64)loop2) + mn;
		}
		if (pos < mn) pos = mn;
		if (pos > mx) pos = mx;
		frames[i] = pos;
	}
	m_seek.SetCues(frames, n);

	int lrcFr[CCustomRangeSliderCtrl::kLrcMarkMax];
	int ln = 0;
	if (og && og->lrcnum > 0 && og->lrctm) {
		extern int wavbit_sample_Hz;
		const int hz = (wavbit_sample_Hz > 0) ? wavbit_sample_Hz : 44100;
		for (int i = 0; i < og->lrcnum && ln < CCustomRangeSliderCtrl::kLrcMarkMax; ++i) {
			const int cs = og->lrctm[i];
			int fr = (int)((__int64)cs * ( __int64)hz / 100);
			if (mx > mn) {
				if (fr < mn) fr = mn;
				if (fr > mx) fr = mx;
			}
			lrcFr[ln++] = fr;
		}
	}
	m_seek.SetLrcMarkers(lrcFr, ln);
}

void CMediaPlayerDlg::JumpToCueIndex(int idx)
{
	ProCue c;
	if (!ProAudio_CueGet(idx, c)) return;
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_APP_PROAUDIO_CUESEEK, 0, (LPARAM)c.frame);
}

void CMediaPlayerDlg::OnSeekCueAdd()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	const int pos = og->m_time.GetPos();
	TCHAR lab[32];
	_stprintf_s(lab, _T("C%d"), ProAudio_CueCount() + 1);
	if (ProAudio_CueAdd(pos, lab) < 0) return;
	RefreshSeekCues();
}

void CMediaPlayerDlg::OnSeekCueClear()
{
	ProAudio_CueClearAll();
	RefreshSeekCues();
}

void CMediaPlayerDlg::OnSeekCueJump1() { JumpToCueIndex(0); }
void CMediaPlayerDlg::OnSeekCueJump2() { JumpToCueIndex(1); }
void CMediaPlayerDlg::OnSeekCueJump3() { JumpToCueIndex(2); }
void CMediaPlayerDlg::OnSeekCueJump4() { JumpToCueIndex(3); }
void CMediaPlayerDlg::OnSeekCueJump5() { JumpToCueIndex(4); }
void CMediaPlayerDlg::OnSeekCueJump6() { JumpToCueIndex(5); }
void CMediaPlayerDlg::OnSeekCueJump7() { JumpToCueIndex(6); }
void CMediaPlayerDlg::OnSeekCueJump8() { JumpToCueIndex(7); }

static void MpPracticeTempoSliderCb(void* ctx, int value);
static void MpPhraseSecSliderCb(void* ctx, int value);
static void MpMicLevSliderCb(void* ctx, int value);
static void MpAbRangeCb(void* ctx, int pos, int selMin, int selMax, int abA, int abB, UINT nSBCode, int dragTarget);

BOOL CMediaPlayerDlg::IsSeekExtrasCommand(UINT cmd)
{
	if (cmd >= ID_MP_SEEK_LOCK && cmd <= ID_MP_PHRASE_AB) return TRUE;
	if (cmd >= ID_MP_AB_SNAP_A && cmd <= ID_MP_AB_TOGGLE) return TRUE;
	if (cmd == ID_MP_BEAT_GRID || cmd == ID_MP_XFADE_PREVIEW) return TRUE;
	if (cmd == ID_MP_VIDEO_REPLACE || cmd == ID_MP_MICMIX_TOGGLE) return TRUE;
	if (AudioMicDevHandleMenuCmd(cmd) || AudioLoopDevHandleMenuCmd(cmd)) return TRUE;
	if (cmd == ID_MP_EXPORT_AB_NOW || cmd == ID_MP_EXPORT_AB || cmd == ID_MP_AB_PACK) return TRUE;
	if (cmd == ID_MP_QUEUE_SHOW) return TRUE;
	if (cmd == ID_MP_LRC_PLUS10 || cmd == ID_MP_LRC_MINUS10
		|| cmd == ID_MP_LRC_PLUS50 || cmd == ID_MP_LRC_MINUS50
		|| cmd == ID_MP_LRC_PLUS100 || cmd == ID_MP_LRC_MINUS100
		|| cmd == ID_MP_LRC_SAVE)
		return TRUE;
	return FALSE;
}

void CMediaPlayerDlg::AppendSeekExtrasToMenu(CCustomPopupMenu& menu, UINT flags)
{
	BOOL needSep = FALSE;
	auto sep = [&]() {
		if (needSep) menu.AddSeparator();
		needSep = TRUE;
	};

	if (flags & MP_SEEK_MENU_VIDEO) {
		menu.AddCommand(ID_MP_VIDEO_REPLACE,
			LL14(L"動画の音声を差し替え…", L"Replace video audio…", L"Remplacer audio video…", L"Sostituisci audio video…", L"Reemplazar audio video…", L"동영상 오디오 교체…", L"替换视频音频…", L"استبدال صوت الفيديو…", L"Заменить звук видео…", L"Video-Audio ersetzen…", L"Substituir audio do video…", L"Video-audio vervangen…", L"Zastap audio wideo…", L"Video sesini degistir…"),
			LL14(L"動画ファイルの音声トラックを WAV で差し替えます", L"Replace the video file audio track with a WAV", L"Remplacer la piste audio video par un WAV", L"Sostituisci la traccia audio del video con WAV", L"Reemplazar la pista de audio del video con WAV", L"동영상 오디오 트랙을 WAV로 교체", L"用 WAV 替换视频音轨", L"استبدال مسار صوت الفيديو بـ WAV", L"Заменить звуковую дорожку видео на WAV", L"Video-Tonspur durch WAV ersetzen", L"Substituir a faixa de audio do video por WAV", L"Vervang de video-audiotrack door WAV", L"Zastap sciezke audio wideo plikiem WAV", L"Video ses izini WAV ile degistir"));
		needSep = TRUE;
	}
	if (flags & MP_SEEK_MENU_LOCK) {
		sep();
		const BOOL lockOn = (m_seekLock.GetSafeHwnd() && m_seekLock.GetCheck() == BST_CHECKED);
		menu.AddCheck(ID_MP_SEEK_LOCK,
			LL14(L"ループつまみをロック", L"Lock loop thumbs", L"Verrouiller poignees boucle", L"Blocca maniglie loop",
				L"Bloquear asas de bucle", L"루프 손잡이 잠금", L"锁定循环端点", L"قفل مقابض الحلقة",
				L"Блокировать ручки цикла", L"Schleifengriffe sperren", L"Travar alças de loop",
				L"Lusgrepen vergrendelen", L"Blokuj uchwyty petli", L"Dongu tutamaclarini kilitle"),
			lockOn,
			LL14(L"ループ端のつまみを動かないようにロックします", L"Lock the loop-end thumbs so they cannot be dragged", L"Verrouiller les poignees de boucle", L"Blocca le maniglie del loop", L"Bloquear las asas del bucle", L"루프 끝 손잡이를 잠급니다", L"锁定循环端点把手，防止误拖", L"قفل مقابض طرفي الحلقة", L"Заблокировать ручки границ цикла", L"Schleifengriffe sperren", L"Travar as alcas do loop", L"Lusgrepen vergrendelen", L"Zablokuj uchwyty petli", L"Dongu tutamaclarini kilitle"));
	}
	if (flags & MP_SEEK_MENU_AB_POINTS) {
		sep();
		menu.AddCommand(ID_MP_SEEK_SETA,
			LL14(L"A点を現在位置に", L"Set A to now", L"Definir A ici", L"Imposta A qui",
				L"Fijar A aqui", L"A를 현재 위치로", L"将A设为当前位置", L"تعيين A هنا",
				L"Задать A здесь", L"A hier setzen", L"Definir A aqui", L"A hier instellen",
				L"Ustaw A tutaj", L"A'yi buraya ayarla"),
				LL14(L"現在の再生位置を A 点（ループ／区間の始点）にします", L"Set point A to the current playback position", L"Definir le point A a la position actuelle", L"Imposta il punto A alla posizione attuale", L"Fijar el punto A en la posición actual", L"현재 재생 위치를 A점으로 설정", L"将当前位置设为 A 点", L"تعيين النقطة A إلى الموضع الحالي", L"Задать точку A в текущей позиции", L"Punkt A auf aktuelle Position setzen", L"Definir o ponto A na posicao atual", L"Punt A op huidige positie zetten", L"Ustaw punkt A na biezacej pozycji", L"A noktasini su anki konuma ayarla"));
		menu.AddCommand(ID_MP_SEEK_SETB,
			LL14(L"B点を現在位置に", L"Set B to now", L"Definir B ici", L"Imposta B qui",
				L"Fijar B aqui", L"B를 현재 위치로", L"将B设为当前位置", L"تعيين B هنا",
				L"Задать B здесь", L"B hier setzen", L"Definir B aqui", L"B hier instellen",
				L"Ustaw B tutaj", L"B'yi buraya ayarla"),
				LL14(L"現在の再生位置を B 点（ループ／区間の終点）にします", L"Set point B to the current playback position", L"Definir le point B a la position actuelle", L"Imposta il punto B alla posizione attuale", L"Fijar el punto B en la posición actual", L"현재 재생 위치를 B점으로 설정", L"将当前位置设为 B 点", L"تعيين النقطة B إلى الموضع الحالي", L"Задать точку B в текущей позиции", L"Punkt B auf aktuelle Position setzen", L"Definir o ponto B na posicao atual", L"Punt B op huidige positie zetten", L"Ustaw punkt B na biezacej pozycji", L"B noktasini su anki konuma ayarla"));
		menu.AddCommand(ID_MP_SEEK_ABCLR,
			LL14(L"A-B解除", L"Clear A-B", L"Effacer A-B", L"Cancella A-B", L"Borrar A-B", L"A-B 해제", L"清除A-B", L"مسح A-B", L"Сброс A-B", L"A-B aus", L"Limpar A-B", L"A-B uit", L"Wyczysc A-B", L"A-B sil"),
			LL14(L"A-B 区間とループ選択を解除します", L"Clear the A-B range and loop selection", L"Effacer la plage A-B et la boucle", L"Cancella l'intervallo A-B e il loop", L"Borrar el rango A-B y el bucle", L"A-B 구간과 루프 선택을 해제", L"清除 A-B 区间和循环选择", L"مسح نطاق A-B والحلقة", L"Сбросить диапазон A-B и цикл", L"A-B-Bereich und Loop loeschen", L"Limpar o intervalo A-B e o loop", L"A-B-bereik en lus wissen", L"Wyczysc zakres A-B i petle", L"A-B araligini ve donguyu temizle"));
	}
	if (flags & MP_SEEK_MENU_RANGE) {
		sep();
		if (og && ::IsWindow(og->GetSafeHwnd())) {
			MirrorSeekVol();
			const int mn = og->m_time.GetMinValue();
			int mx = og->m_time.GetMaxValue();
			if (mx <= mn) mx = mn + 1;
			if (mx > mn) {
				int selMn = 0, selMx = 0;
				og->m_time.GetSelection(selMn, selMx);
				menu.AddRangeSlider(
					LL14(L"シーク / ループ / A-B", L"Seek / loop / A-B", L"Seek / boucle / A-B", L"Seek / loop / A-B",
						L"Seek / bucle / A-B", L"시크 / 루프 / A-B", L"定位 / 循环 / A-B", L"تقديم / حلقة / A-B",
						L"Поиск / цикл / A-B", L"Suche / Loop / A-B", L"Seek / loop / A-B", L"Zoek / lus / A-B",
						L"Seek / petla / A-B", L"Seek / dongu / A-B"),
					mn, mx, og->m_time.GetPos(), selMn, selMx, m_abApos, m_abBpos,
					MpAbRangeCb, this,
					LL14(L"再生位置・ループ・A-B をメニュー上で調整", L"Adjust position, loop and A-B in the menu",
						L"Regler position, boucle et A-B dans le menu", L"Regola posizione, loop e A-B dal menu",
						L"Ajustar posicion, bucle y A-B en el menu", L"메뉴에서 위치/루프/A-B 조정", L"在菜单中调整位置、循环与 A-B",
						L"ضبط الموضع والحلقة و A-B من القائمة", L"Настройка позиции, цикла и A-B в меню",
						L"Position, Loop und A-B im Menü", L"Ajustar posicao, loop e A-B no menu",
						L"Positie, lus en A-B in het menu", L"Pozycja, petla i A-B w menu", L"Menude konum, dongu ve A-B"),
					0x00E01001u);
			}
		} else if (m_seek.GetSafeHwnd()) {
			const int mn = m_seek.GetMinValue();
			const int mx = m_seek.GetMaxValue();
			if (mx > mn) {
				int selMn = 0, selMx = 0;
				m_seek.GetSelection(selMn, selMx);
				menu.AddRangeSlider(
					LL14(L"シーク / ループ / A-B", L"Seek / loop / A-B", L"Seek / boucle / A-B", L"Seek / loop / A-B",
						L"Seek / bucle / A-B", L"시크 / 루프 / A-B", L"定位 / 循环 / A-B", L"تقديم / حلقة / A-B",
						L"Поиск / цикл / A-B", L"Suche / Loop / A-B", L"Seek / loop / A-B", L"Zoek / lus / A-B",
						L"Seek / petla / A-B", L"Seek / dongu / A-B"),
					mn, mx, m_seek.GetPos(), selMn, selMx, m_abApos, m_abBpos,
					MpAbRangeCb, this,
					LL14(L"再生位置・ループ・A-B をメニュー上で調整", L"Adjust position, loop and A-B in the menu",
						L"Regler position, boucle et A-B dans le menu", L"Regola posizione, loop e A-B dal menu",
						L"Ajustar posicion, bucle y A-B en el menu", L"메뉴에서 위치/루프/A-B 조정", L"在菜单中调整位置、循环与 A-B",
						L"ضبط الموضع والحلقة و A-B من القائمة", L"Настройка позиции, цикла и A-B в меню",
						L"Position, Loop und A-B im Menü", L"Ajustar posicao, loop e A-B no menu",
						L"Positie, lus en A-B in het menu", L"Pozycja, petla i A-B w menu", L"Menude konum, dongu ve A-B"),
					0x00E01001u);
			}
		}
	}
	if (flags & MP_SEEK_MENU_WAVE) {
		sep();
		menu.AddCheck(ID_MP_SEEK_WAVE,
			LL14(L"波形オーバービュー", L"Waveform overview", L"Apercu forme d'onde", L"Panoramica forma d'onda",
				L"Vista de forma de onda", L"파형 오버뷰", L"波形概览", L"نظرة الموجة",
				L"Обзор волны", L"Wellenform-Uberblick", L"Visao da forma de onda",
				L"Golfvorm-overzicht", L"Podglad fali", L"Dalga formu onizleme"),
			savedata.mpSeekWave != 0,
			LL14(L"シークバーに曲全体の波形オーバービューを表示します", L"Show a full-track waveform overview on the seek bar", L"Afficher un apercu d'onde sur la barre", L"Mostra panoramica forma d'onda sulla barra", L"Mostrar vista de forma de onda en la barra", L"시크바에 전체 파형 오버뷰를 표시", L"在进度条显示整曲波形概览", L"عرض نظرة موجة على شريط التقديم", L"Показать обзор волны на полосе", L"Wellenform-Uberblick auf der Suchleiste", L"Mostrar visao da forma de onda na barra", L"Golfvorm-overzicht op de zoekbalk", L"Pokaz podglad fali na pasku", L"Seek cubugunda dalga formu onizleme"));
	}
	if (flags & MP_SEEK_MENU_CUES) {
		sep();
		menu.AddCommand(ID_MP_SEEK_CUEADD,
			LL14(L"キューを現在位置に追加", L"Add cue at now", L"Ajouter cue ici", L"Aggiungi cue qui",
				L"Anadir cue aqui", L"현재 위치에 큐 추가", L"在当前位置添加标记", L"إضافة إشارة هنا",
				L"Добавить метку здесь", L"Cue hier hinzufugen", L"Adicionar cue aqui",
				L"Cue hier toevoegen", L"Dodaj cue tutaj", L"Buraya cue ekle"),
				LL14(L"現在位置にキュー（ジャンプ用マーカー）を追加します", L"Add a cue marker at the current position", L"Ajouter un marqueur cue a la position actuelle", L"Aggiungi un cue alla posizione attuale", L"Anadir un cue en la posición actual", L"현재 위치에 큐 마커 추가", L"在当前位置添加标记", L"إضافة إشارة عند الموضع الحالي", L"Добавить метку в текущей позиции", L"Cue an aktueller Position hinzufugen", L"Adicionar um cue na posicao atual", L"Cue op huidige positie toevoegen", L"Dodaj cue w biezacej pozycji", L"Su anki konuma cue ekle"));
		const int cn = ProAudio_CueCount();
		if (cn > 0) {
			CCustomPopupMenu* sub = menu.AddSubMenu(
				LL14(L"キューへジャンプ", L"Jump to cue", L"Aller au cue", L"Vai al cue",
					L"Ir al cue", L"큐로 이동", L"跳到标记", L"الانتقال إلى إشارة",
					L"К метке", L"Zum Cue", L"Ir ao cue", L"Naar cue", L"Do cue", L"Cue'ya git"),
					LL14(L"登録したキュー位置の一覧からジャンプします", L"Jump to a registered cue from the list", L"Aller a un cue enregistre dans la liste", L"Vai a un cue registrato dall'elenco", L"Ir a un cue registrado de la lista", L"등록된 큐 목록에서 이동합니다", L"从已注册标记列表跳转", L"الانتقال إلى إشارة مسجلة من القائمة", L"Перейти к метке из списка", L"Zu einem Cue aus der Liste springen", L"Ir a um cue registrado da lista", L"Naar een geregistreerde cue in de lijst springen", L"Skocz do cue z listy", L"Listeden kayitli cue'ya git"));
			if (sub) {
				for (int i = 0; i < cn && i < 8; ++i) {
					ProCue c; ProAudio_CueGet(i, c);
					CString s; s.Format(_T("%d: %s"), i + 1, c.label[0] ? c.label : _T("Cue"));
					sub->AddCommand(ID_MP_SEEK_CUE1 + i, s);
				}
			}
			menu.AddCommand(ID_MP_SEEK_CUECLR,
				LL14(L"キュー全削除", L"Clear all cues", L"Effacer tous les cues", L"Cancella tutti i cue",
					L"Borrar todos los cues", L"모든 큐 삭제", L"清除全部标记", L"مسح كل الإشارات",
					L"Очистить метки", L"Alle Cues loschen", L"Limpar todos os cues",
					L"Alle cues wissen", L"Wyczysc cue", L"Tum cue'lari sil"),
					LL14(L"登録したキューをすべて削除します", L"Delete all registered cue markers", L"Supprimer tous les marqueurs cue", L"Elimina tutti i cue", L"Eliminar todos los cues", L"등록된 큐를 모두 삭제", L"删除全部标记", L"حذف كل الإشارات", L"Удалить все метки", L"Alle Cues loschen", L"Excluir todos os cues", L"Alle cues verwijderen", L"Usun wszystkie cue", L"Tum cue isaretlerini sil"));
		}
	}
	if (flags & MP_SEEK_MENU_PRACTICE) {
		sep();
		CCustomPopupMenu* prac = menu.AddSubMenu(
			LL14(L"練習", L"Practice", L"Pratique", L"Pratica", L"Practica", L"연습", L"练习", L"تدريب",
				L"Практика", L"Ubung", L"Pratica", L"Oefenen", L"Cwiczenie", L"Alistirma"),
				LL14(L"テンポ変更やフレーズ A-B など練習用ツールです", L"Practice tools: tempo change and phrase A-B", L"Outils de pratique: tempo et phrase A-B", L"Strumenti pratica: tempo e frase A-B", L"Herramientas de practica: tempo y frase A-B", L"템포 변경·프레이즈 A-B 등 연습 도구", L"练习工具：变速与乐句 A-B", L"أدوات التدريب: الإيقاع وعبارة A-B", L"Инструменты практики: темп и фраза A-B", L"Ubungswerkzeuge: Tempo und Phrase A-B", L"Ferramentas de pratica: tempo e frase A-B", L"Oefenhulpmiddelen: tempo en frase A-B", L"Narzedzia cwiczen: tempo i fraza A-B", L"Alistirma araclari: tempo ve cumle A-B"));
		if (prac) {
			int pct = tempo / 2;
			if (pct < 50) pct = 50;
			if (pct > 200) pct = 200;
			prac->AddSlider(
				LL14(L"テンポ", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"템포", L"速度", L"الإيقاع",
					L"Темп", L"Tempo", L"Tempo", L"Tempo", L"Tempo", L"Tempo"),
				50, 200, pct, MpPracticeTempoSliderCb, this,
				LL14(L"練習テンポ 50%〜200%（ドラッグ中に反映）", L"Practice tempo 50%–200% (live)",
					L"Tempo pratique 50%–200% (direct)", L"Tempo pratica 50%–200% (live)",
					L"Tempo practica 50%–200% (en vivo)", L"연습 템포 50%–200% (즉시)", L"练习速度 50%–200%（即时）",
					L"إيقاع التدريب 50%–200% (مباشر)", L"Темп практики 50%–200% (сразу)",
					L"Ubungstempo 50%–200% (live)", L"Tempo pratica 50%–200% (ao vivo)",
					L"Oefentempo 50%–200% (live)", L"Tempo cwiczen 50%–200% (na zywo)", L"Alistirma temposu 50%–200% (anlik)"));
			prac->AddCommand(ID_MP_PRACTICE_T50,
				LL14(L"テンポ 50%", L"Tempo 50%", L"Tempo 50%", L"Tempo 50%", L"Tempo 50%",
					L"템포 50%", L"速度 50%", L"إيقاع 50%", L"Темп 50%", L"Tempo 50%",
					L"Tempo 50%", L"Tempo 50%", L"Tempo 50%", L"Tempo 50%"));
			prac->AddCommand(ID_MP_PRACTICE_T75,
				LL14(L"テンポ 75%", L"Tempo 75%", L"Tempo 75%", L"Tempo 75%", L"Tempo 75%",
					L"템포 75%", L"速度 75%", L"إيقاع 75%", L"Темп 75%", L"Tempo 75%",
					L"Tempo 75%", L"Tempo 75%", L"Tempo 75%", L"Tempo 75%"));
			prac->AddCommand(ID_MP_PRACTICE_T100,
				LL14(L"テンポ 100%", L"Tempo 100%", L"Tempo 100%", L"Tempo 100%", L"Tempo 100%",
					L"템포 100%", L"速度 100%", L"إيقاع 100%", L"Темп 100%", L"Tempo 100%",
					L"Tempo 100%", L"Tempo 100%", L"Tempo 100%", L"Tempo 100%"));
			prac->AddSeparator();
			int phSec = savedata.mpPhraseSec;
			if (phSec < 1) phSec = 4;
			if (phSec > 60) phSec = 60;
			prac->AddSlider(
				LL14(L"フレーズ幅 (秒)", L"Phrase width (sec)", L"Largeur phrase (s)", L"Larghezza frase (s)", L"Ancho frase (s)",
					L"프레이즈 폭 (초)", L"乐句宽度 (秒)", L"عرض العبارة (ث)", L"Ширина фразы (с)", L"Phrasenbreite (s)",
					L"Largura da frase (s)", L"Frasebreedte (s)", L"Szerokosc frazy (s)", L"Cumle genisligi (sn)"),
				1, 60, phSec, MpPhraseSecSliderCb, this,
				LL14(L"フレーズA-Bの±秒（ドラッグ中に反映）", L"Phrase A-B ±seconds (live)", L"±secondes de la phrase A-B (direct)", L"±secondi frase A-B (live)", L"±segundos frase A-B (en vivo)",
					L"프레이즈 A-B ±초(즉시)", L"乐句 A-B ±秒（即时）", L"±ثوانٍ لعبارة A-B (مباشر)", L"±секунды фразы A-B (сразу)", L"±Sekunden Phrase A-B (live)",
					L"±segundos da frase A-B (ao vivo)", L"±seconden frase A-B (live)", L"±sekundy frazy A-B (na zywo)", L"A-B cumle ±saniye (anlik)"));
			CString ph;
			ph.Format(LL14(L"フレーズA-B (±%d秒) [R]", L"Phrase A-B (±%d sec) [R]", L"Phrase A-B (±%d s) [R]", L"Frase A-B (±%d s) [R]",
				L"Frase A-B (±%d s) [R]", L"프레이즈 A-B (±%d초) [R]", L"乐句A-B (±%d秒) [R]", L"عبارة A-B (±%d ث) [R]",
				L"Фраза A-B (±%d с) [R]", L"Phrase A-B (±%d s) [R]", L"Frase A-B (±%d s) [R]", L"Frase A-B (±%d s) [R]",
				L"Fraza A-B (±%d s) [R]", L"Cumle A-B (±%d sn) [R]"),
				phSec);
			prac->AddCommand(ID_MP_PHRASE_AB, ph);
		}
	}
	if (flags & MP_SEEK_MENU_MIC) {
		sep();
		menu.AddCheck(ID_MP_MICMIX_TOGGLE,
			LL14(L"マイクミックス", L"Mic mix", L"Mix micro", L"Mix microfono", L"Mezcla micro",
				L"마이크 믹스", L"麦克风混音", L"مزج الميكروفون", L"Микс микрофона", L"Mikrofon-Mix",
				L"Mix microfone", L"Mic-mix", L"Mix mikrofonu", L"Mikrofon karisimi"),
			savedata.mic_mix != 0,
			LL14(L"再生／書き出し時にマイク入力をミックスします", L"Mix microphone input into playback/export", L"Mixer le micro dans la lecture/export", L"Mixa il microfono in riproduzione/export", L"Mezclar el microfono en reproduccion/exportacion", L"재생/내보내기에 마이크 입력을 믹스", L"在播放/导出时混合麦克风", L"مزج الميكروفون في التشغيل/التصدير", L"Микшировать микрофон при воспроизведении/экспорте", L"Mikrofon in Wiedergabe/Export mischen", L"Misturar microfone na reproducao/exportacao", L"Microfoon mixen bij afspelen/export", L"Miksuj mikrofon przy odtwarzaniu/eksporcie", L"Calma/disa aktarmada mikrofonu karistir"));
		int lv = savedata.mic_mix_level;
		if (lv < 0) lv = 0;
		if (lv > 200) lv = 200;
		menu.AddSlider(
			LL14(L"マイクレベル", L"Mic level", L"Niveau micro", L"Livello microfono", L"Nivel micro",
				L"마이크 레벨", L"麦克风电平", L"مستوى الميكروفون", L"Уровень микрофона", L"Mikrofonpegel",
				L"Nivel do microfone", L"Mic-niveau", L"Poziom mikrofonu", L"Mikrofon seviyesi"),
			0, 200, lv, MpMicLevSliderCb, this,
			LL14(L"マイクミックスの音量 0〜200（ドラッグ中に反映）", L"Mic mix level 0–200 (live)", L"Niveau mix micro 0–200 (direct)", L"Livello mix microfono 0–200 (live)", L"Nivel mezcla micro 0–200 (en vivo)",
				L"마이크 믹스 볼륨 0–200(즉시)", L"麦克风混音音量 0–200（即时）", L"مستوى مزج الميكروفون 0–200 (مباشر)", L"Громкость микса микрофона 0–200 (сразу)", L"Mikrofon-Mix-Pegel 0–200 (live)",
				L"Nivel do mix de microfone 0–200 (ao vivo)", L"Mic-mixniveau 0–200 (live)", L"Poziom mixu mikrofonu 0–200 (na zywo)", L"Mikrofon mix seviyesi 0–200 (anlik)"));
		AudioMicDevAppendMenu(menu);
		AudioLoopDevAppendMenu(menu);
	}
	if (flags & MP_SEEK_MENU_SNAPS) {
		sep();
		menu.AddCommand(ID_MP_AB_SNAP_A,
			LL14(L"スナップショット A", L"Snapshot A", L"Instantane A", L"Istantanea A", L"Instantanea A", L"스냅샷 A", L"快照 A", L"لقطة A", L"Снимок A", L"Schnappschuss A", L"Instantaneo A", L"Momentopname A", L"Migawka A", L"Anlik goruntu A"),
			LL14(L"現在の EQ／エフェクト設定をスロット A に保存します", L"Save current EQ/effect settings to slot A", L"Enregistrer les reglages EQ/FX dans le slot A", L"Salva EQ/FX attuali nello slot A", L"Guardar EQ/FX actuales en la ranura A", L"현재 EQ/효과 설정을 슬롯 A에 저장", L"将当前 EQ/效果保存到插槽 A", L"حفظ إعدادات EQ/FX الحالية في الفتحة A", L"Сохранить текущие EQ/FX в слот A", L"Aktuelle EQ/FX-Einstellungen in Slot A speichern", L"Salvar EQ/FX atuais no slot A", L"Huidige EQ/FX-instellingen in slot A opslaan", L"Zapisz biezace EQ/FX w slocie A", L"Gecerli EQ/FX ayarlarini A yuvasina kaydet"));
		menu.AddCommand(ID_MP_AB_SNAP_B,
			LL14(L"スナップショット B", L"Snapshot B", L"Instantane B", L"Istantanea B", L"Instantanea B", L"스냅샷 B", L"快照 B", L"لقطة B", L"Снимок B", L"Schnappschuss B", L"Instantaneo B", L"Momentopname B", L"Migawka B", L"Anlik goruntu B"),
			LL14(L"現在の EQ／エフェクト設定をスロット B に保存します", L"Save current EQ/effect settings to slot B", L"Enregistrer les reglages EQ/FX dans le slot B", L"Salva EQ/FX attuali nello slot B", L"Guardar EQ/FX actuales en la ranura B", L"현재 EQ/효과 설정을 슬롯 B에 저장", L"将当前 EQ/效果保存到插槽 B", L"حفظ إعدادات EQ/FX الحالية في الفتحة B", L"Сохранить текущие EQ/FX в слот B", L"Aktuelle EQ/FX-Einstellungen in Slot B speichern", L"Salvar EQ/FX atuais no slot B", L"Huidige EQ/FX-instellingen in slot B opslaan", L"Zapisz biezace EQ/FX w slocie B", L"Gecerli EQ/FX ayarlarini B yuvasina kaydet"));
		menu.AddCommand(ID_MP_AB_APPLY_A,
			LL14(L"A を適用", L"Apply A", L"Appliquer A", L"Applica A", L"Aplicar A", L"A 적용", L"应用 A", L"تطبيق A", L"Применить A", L"A anwenden", L"Aplicar A", L"A toepassen", L"Zastosuj A", L"A uygula"),
			LL14(L"スロット A の設定を再生に適用します", L"Apply slot A settings to playback", L"Appliquer les reglages du slot A", L"Applica le impostazioni dello slot A", L"Aplicar la configuracion de la ranura A", L"슬롯 A 설정을 재생에 적용", L"将插槽 A 设置应用到播放", L"تطبيق إعدادات الفتحة A", L"Применить настройки слота A", L"Einstellungen aus Slot A anwenden", L"Aplicar as configuracoes do slot A", L"Instellingen van slot A toepassen", L"Zastosuj ustawienia slotu A", L"A yuvasi ayarlarini uygula"));
		menu.AddCommand(ID_MP_AB_APPLY_B,
			LL14(L"B を適用", L"Apply B", L"Appliquer B", L"Applica B", L"Aplicar B", L"B 적용", L"应用 B", L"تطبيق B", L"Применить B", L"B anwenden", L"Aplicar B", L"B toepassen", L"Zastosuj B", L"B uygula"),
			LL14(L"スロット B の設定を再生に適用します", L"Apply slot B settings to playback", L"Appliquer les reglages du slot B", L"Applica le impostazioni dello slot B", L"Aplicar la configuracion de la ranura B", L"슬롯 B 설정을 재생에 적용", L"将插槽 B 设置应用到播放", L"تطبيق إعدادات الفتحة B", L"Применить настройки слота B", L"Einstellungen aus Slot B anwenden", L"Aplicar as configuracoes do slot B", L"Instellingen van slot B toepassen", L"Zastosuj ustawienia slotu B", L"B yuvasi ayarlarini uygula"));
		menu.AddCommand(ID_MP_AB_TOGGLE,
			LL14(L"A/B 切替", L"Toggle A/B", L"Basculer A/B", L"Commuta A/B", L"Alternar A/B", L"A/B 전환", L"切换 A/B", L"تبديل A/B", L"Переключить A/B", L"A/B umschalten", L"Alternar A/B", L"A/B wisselen", L"Przelacz A/B", L"A/B degistir"),
			LL14(L"スロット A と B の設定を交互に切り替えます", L"Toggle between slot A and B settings", L"Basculer entre les slots A et B", L"Commuta tra gli slot A e B", L"Alternar entre las ranuras A y B", L"슬롯 A/B 설정을 번갈아 전환", L"在插槽 A 与 B 之间切换", L"التبديل بين الفتحات A و B", L"Переключить между слотами A и B", L"Zwischen Slot A und B umschalten", L"Alternar entre os slots A e B", L"Wisselen tussen slot A en B", L"Przelacz miedzy slotami A i B", L"A ve B yuvalari arasinda gec"));
	}
	if (flags & MP_SEEK_MENU_EXPORT) {
		sep();
		menu.AddCommand(ID_MP_EXPORT_AB_NOW,
			LL14(L"A-Bを今すぐWAVへ…", L"Export A-B to WAV now…", L"Exporter A-B en WAV…", L"Esporta A-B in WAV…", L"Exportar A-B a WAV…", L"A-B를 지금 WAV로…", L"立即将 A-B 导出为 WAV…", L"تصدير A-B إلى WAV الآن…", L"Экспорт A-B в WAV сейчас…", L"A-B jetzt als WAV…", L"Exportar A-B para WAV agora…", L"A-B nu naar WAV…", L"Eksportuj A-B do WAV teraz…", L"A-B simdi WAV…"),
			LL14(L"設定した A-B 区間を今すぐ WAV ファイルへ書き出します", L"Export the A-B range to a WAV file right now", L"Exporter la plage A-B en WAV maintenant", L"Esporta subito l'intervallo A-B in WAV", L"Exportar ya el rango A-B a WAV", L"설정한 A-B 구간을 지금 WAV로 내보내기", L"立即将 A-B 区间导出为 WAV", L"تصدير نطاق A-B إلى WAV الآن", L"Сразу экспортировать диапазон A-B в WAV", L"A-B-Bereich jetzt als WAV exportieren", L"Exportar agora o intervalo A-B para WAV", L"A-B-bereik nu naar WAV exporteren", L"Eksportuj teraz zakres A-B do WAV", L"A-B araligini simdi WAV olarak disa aktar"));
		menu.AddCommand(ID_MP_EXPORT_AB,
			LL14(L"A-Bを書き出し範囲に", L"Export A-B range", L"Exporter plage A-B", L"Esporta intervallo A-B", L"Exportar rango A-B", L"A-B를 내보내기 범위로", L"将 A-B 设为导出范围", L"تصدير نطاق A-B", L"Экспорт диапазона A-B", L"A-B-Bereich exportieren", L"Exportar faixa A-B", L"A-B-bereik exporteren", L"Eksport zakresu A-B", L"A-B araligini disa aktar"),
			LL14(L"A-B 区間を以降の書き出し範囲として設定します", L"Use the A-B range for the next export", L"Utiliser la plage A-B pour le prochain export", L"Usa l'intervallo A-B per il prossimo export", L"Usar el rango A-B para la proxima exportacion", L"A-B 구간을 다음 내보내기 범위로 설정", L"将 A-B 设为下次导出范围", L"استخدام نطاق A-B للتصدير التالي", L"Использовать A-B для следующего экспорта", L"A-B als nachsten Exportbereich setzen", L"Usar o intervalo A-B na proxima exportacao", L"A-B gebruiken voor de volgende export", L"Uzyj zakresu A-B do nastepnego eksportu", L"Sonraki disa aktarma icin A-B kullan"));
		menu.AddCommand(ID_MP_AB_PACK,
			LL14(L"A-B/キューを一括書き出し…", L"Export A-B/cue pack…", L"Exporter pack A-B/cues…", L"Esporta pack A-B/cue…", L"Exportar pack A-B/cues…", L"A-B/큐 일괄 내보내기…", L"批量导出 A-B/标记…", L"تصدير حزمة A-B/cues…", L"Пакетный экспорт A-B/cue…", L"A-B/Cue-Paket export…", L"Exportar pacote A-B/cues…", L"A-B/cue-pakket…", L"Pakiet A-B/cue…", L"A-B/cue paketi…"),
			LL14(L"A-B 区間とキューをまとめて書き出します", L"Batch-export the A-B range and cue markers", L"Exporter en lot la plage A-B et les cues", L"Esporta in batch intervallo A-B e cue", L"Exportar por lotes el rango A-B y cues", L"A-B 구간과 큐를 일괄 내보내기", L"批量导出 A-B 区间与标记", L"تصدير نطاق A-B والإشارات دفعة واحدة", L"Пакетно экспортировать A-B и метки", L"A-B und Cues als Paket exportieren", L"Exportar em lote o intervalo A-B e cues", L"A-B en cues als pakket exporteren", L"Eksportuj pakietowo zakres A-B i cue", L"A-B ve cue'lari toplu disa aktar"));
	}
	if (flags & MP_SEEK_MENU_GRID) {
		sep();
		menu.AddCheck(ID_MP_BEAT_GRID,
			LL14(L"拍グリッド", L"Beat grid", L"Grille de battements", L"Griglia beat", L"Cuadricula de beats", L"비트 그리드", L"节拍网格", L"شبكة الإيقاع", L"Сетка битов", L"Beat-Raster", L"Grade de batidas", L"Beatraster", L"Siatka beatow", L"Vurus izgarasi"),
			savedata.mpBeatGrid != 0,
			LL14(L"シーク上に拍グリッドを重ねて表示します（BPM 計測後に有効）", L"Show a beat grid on the seek bar (after BPM measure)", L"Afficher une grille de temps sur la barre (apres BPM)", L"Mostra griglia beat sulla barra (dopo BPM)", L"Mostrar cuadricula de beats en la barra (tras BPM)", L"시크바에 비트 그리드 표시(BPM 측정 후)", L"在进度条显示节拍网格（测 BPM 后）", L"عرض شبكة الإيقاع على الشريط (بعد قياس BPM)", L"Показать сетку долей на полосе (после BPM)", L"Beat-Raster auf der Suchleiste (nach BPM)", L"Mostrar grade de batidas na barra (apos BPM)", L"Beatraster op de zoekbalk (na BPM)", L"Pokaz siatke beatow na pasku (po BPM)", L"Seekte vurus izgarasi goster (BPM sonrasi)"));
		menu.AddCheck(ID_MP_XFADE_PREVIEW,
			LL14(L"書き出しクロスフェード帯", L"Export xfade band", L"Bande xfade export", L"Banda xfade export", L"Banda xfade export", L"내보내기 크로스페이드 띠", L"导出交叉淡化带", L"شريط xfade للتصدير", L"Полоса xfade экспорта", L"Export-Xfade-Band", L"Faixa xfade export", L"Export-xfade-band", L"Pasmo xfade eksportu", L"Disa aktarma xfade seridi"),
			savedata.mpXfadePreview != 0,
			LL14(L"書き出しクロスフェードの重なり帯をシークに表示します", L"Show the export crossfade overlap band on the seek bar", L"Afficher la bande de chevauchement xfade sur la barre", L"Mostra la banda di sovrapposizione xfade sulla barra", L"Mostrar la banda de solape xfade en la barra", L"내보내기 크로스페이드 겹침을 시크바에 표시", L"在进度条显示导出交叉淡化重叠带", L"عرض شريط تداخل xfade على الشريط", L"Показать полосу перекрытия xfade на сике", L"Export-Xfade-Uberlappung auf der Suchleiste zeigen", L"Mostrar a faixa de sobreposicao xfade na barra", L"Export-xfade-overlap op de zoekbalk tonen", L"Pokaz pasmo nakladania xfade na pasku", L"Disa aktarma xfade bindirme bandini seekte goster"));
	}
}

static void MpPracticeTempoSliderCb(void* ctx, int value)
{
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (p) p->ApplyPracticeTempoPercent(value);
}

static void MpPhraseSecSliderCb(void* ctx, int value)
{
	if (value < 1) value = 1;
	if (value > 60) value = 60;
	if (value == savedata.mpPhraseSec) return;
	savedata.mpPhraseSec = value;
	MpPersistSavedataQuick();
	// ツールチップどおりドラッグ中に現在位置基準のフレーズ A-B を張り直す
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (p) p->SetPhraseAbAroundNow();
}

static void MpMicLevSliderCb(void* ctx, int value)
{
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (value < 0) value = 0;
	if (value > 200) value = 200;
	savedata.mic_mix_level = value;
	if (p && ::IsWindow(p->GetSafeHwnd()) && p->m_miclev.GetSafeHwnd())
		p->m_miclev.SetPos(value);
	extern COggDlg* og;
	if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_miclev.GetSafeHwnd()) {
		og->m_miclev.SetPos(value);
		og->ApplyMicMixLevelLabel();
	}
	MpPersistSavedataQuick();
}

struct MpLrcSliderCtx {
	CMediaPlayerDlg* dlg;
	int lastMs;
	int pendingMs; // lrctm は 10ms 単位。端数を捨てると小刻みドラッグが無反映になる
};

static void MpLrcOffsetSliderCb(void* ctx, int value)
{
	MpLrcSliderCtx* c = (MpLrcSliderCtx*)ctx;
	if (!c || !c->dlg) return;
	const int delta = value - c->lastMs;
	if (delta == 0) return;
	c->lastMs = value;
	c->pendingMs += delta;
	const int dCentis = c->pendingMs / 10;
	if (dCentis == 0) return;
	c->pendingMs -= dCentis * 10;
	c->dlg->ShiftLrcMs(dCentis * 10);
}

static void MpLufsSliderCb(void* /*ctx*/, int value)
{
	if (value > -14) value = -14;
	if (value < -18) value = -18;
	savedata.mpNormTargetLufs = value;
	savedata.pro_rg_target = value;
	MpPersistSavedataQuick();
}

static void MpSleepSliderCb(void* ctx, int value)
{
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (!p) return;
	if (value < 0) value = 0;
	if (value > 240) value = 240;
	if (value == savedata.mpSleepMin) return;
	p->ApplySleepTimer(value);
}

static void MpAbRangeCb(void* ctx, int pos, int selMin, int selMax, int abA, int abB, UINT nSBCode, int dragTarget)
{
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (!p) return;

	p->m_abApos = abA;
	p->m_abBpos = abB;
	extern int loop1, loop2;
	loop1 = selMin;
	loop2 = (selMax > selMin) ? (selMax - selMin) : 0;
	if (pl && plcnt >= 0 && plcnt < pl->playcnt) {
		pl->pc[plcnt].loop1 = loop1;
		pl->pc[plcnt].loop2 = loop2;
	}
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->m_time.SetSelection(selMin, selMax);
	if (p->m_seek.GetSafeHwnd()) {
		p->m_seek.SetSelection(selMin, selMax);
		p->m_seek.SetAB(abA, abB);
	}

	// 注意: TB_ENDTRACK == SB_ENDSCROLL。loop/A-B 確定も ENDTRACK なので SEEK 条件に含めない。
	const BOOL seekCommit =
		nSBCode == SB_THUMBPOSITION
		|| nSBCode == SB_PAGELEFT || nSBCode == SB_PAGERIGHT
		|| nSBCode == SB_LINELEFT || nSBCode == SB_LINERIGHT;

	if (nSBCode == TB_THUMBTRACK || nSBCode == SB_THUMBTRACK) {
		// 再生位置つまみ中のみミラーを止め、見た目を追従
		if (dragTarget == 3 || dragTarget == 0) {
			p->m_seekDragging = 1;
			extern int hsc;
			if (hsc == 0) hsc = 1;
			if (p->m_seek.GetSafeHwnd())
				p->m_seek.SetPos(pos);
		}
		return;
	}

	if (seekCommit) {
		// 位置つまみ／クリックシークのみ。loop(1,2) / A-B(4,5) は上で反映済み。
		if (dragTarget == 1 || dragTarget == 2 || dragTarget == 4 || dragTarget == 5) {
			p->m_seekDragging = 0;
			return;
		}
		if (p->m_seek.GetSafeHwnd())
			p->m_seek.SetPos(pos);
		if (og && ::IsWindow(og->GetSafeHwnd())) {
			extern int hsc;
			if (hsc == 0) hsc = 1;
			og->m_time.SetPos(pos);
			og->SendMessage(WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, pos), (LPARAM)og->m_time.GetSafeHwnd());
			if (hsc == 1) hsc = 0;
		}
		p->m_seekHoldPos = pos;
		p->m_seekHoldUntil = GetTickCount64() + 800;
		p->m_seekDragging = 0;
		return;
	}

	if (nSBCode == TB_ENDTRACK || nSBCode == SB_ENDSCROLL) {
		extern int hsc;
		if (hsc == 1) hsc = 0;
		p->m_seekDragging = 0;
	}
}

static void MpSleepEditCb(void* ctx, LPCTSTR text)
{
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (!p || !text || !text[0]) return;
	int mins = _ttoi(text);
	if (mins < 1) return;
	if (mins > 240) mins = 240;
	// 同じ値の再通知（初期 SetWindowText / KILLFOCUS）でタイマーを振り直さない
	if (mins == savedata.mpSleepMin) return;
	p->ApplySleepTimer(mins);
}

static void MpSleepChoiceCb(void* ctx, int /*index*/, LPCTSTR text)
{
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (!p || !text || !text[0]) return;
	// 「—」等のプレースホルダ（現在値が長めリスト外のとき）
	if (text[0] < _T('0') || text[0] > _T('9')) return;
	int mins = _ttoi(text);
	if (mins < 1) return;
	if (mins > 240) mins = 240;
	if (mins == savedata.mpSleepMin) return;
	p->ApplySleepTimer(mins);
}

// メニュー内アラーム用（無効中もコンボの選択を保持）
static int g_mpMenuAlarmH = 8;
static int g_mpMenuAlarmM = 0;
static BOOL g_mpMenuAlarmDraftValid = FALSE;

static void MpAlarmHourCb(void* ctx, int index, LPCTSTR /*text*/)
{
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (!p || index < 0 || index > 23) return;
	g_mpMenuAlarmH = index;
	g_mpMenuAlarmDraftValid = TRUE;
	// OFF 中も選択を保持。ON 中は即保存してタイマー更新。
	if (savedata.mpAlarmHour >= 0) {
		savedata.mpAlarmHour = index;
		MpPersistSavedataQuick();
		MpAlarmEnsureTimer(p);
	}
}

static void MpAlarmMinCb(void* ctx, int index, LPCTSTR /*text*/)
{
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (!p || index < 0 || index > 59) return;
	g_mpMenuAlarmM = index;
	g_mpMenuAlarmDraftValid = TRUE;
	if (savedata.mpAlarmHour >= 0) {
		savedata.mpAlarmMin = index;
		MpPersistSavedataQuick();
		MpAlarmEnsureTimer(p);
	}
}

static void MpRemotePortCb(void* ctx, LPCTSTR text)
{
	CMediaPlayerDlg* p = (CMediaPlayerDlg*)ctx;
	if (!p || !text || !text[0]) return;
	int port = _ttoi(text);
	if (port < 1024 || port > 65535) return;
	if (savedata.mpRemotePort == port) return;
	savedata.mpRemotePort = port;
	MpPersistSavedataQuick();
	if (savedata.mpRemoteOn)
		MpRemoteEnsureRunning(p->GetSafeHwnd());
}

void CMediaPlayerDlg::ApplyPracticeTempoPercent(int pct)
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	if (pct < 50) pct = 50;
	if (pct > 200) pct = 200;
	// スライダー 0..400、200=100%
	const int pos = pct * 2;
	og->m_tempo_sl.SetPos(pos);
	if (m_tempo.GetSafeHwnd())
		m_tempo.SetPos(pos);
	tempo = pos;
	DougaApplyTempoToVideoRate();
}

void CMediaPlayerDlg::OnPracticeTempo50() { ApplyPracticeTempoPercent(50); }
void CMediaPlayerDlg::OnPracticeTempo75() { ApplyPracticeTempoPercent(75); }
void CMediaPlayerDlg::OnPracticeTempo100() { ApplyPracticeTempoPercent(100); }

void CMediaPlayerDlg::SetPhraseAbAroundNow()
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	int sec = savedata.mpPhraseSec;
	if (sec < 1) sec = 4;
	if (sec > 60) sec = 60;
	const int mn = og->m_time.GetMinValue();
	const int mx = og->m_time.GetMaxValue();
	const int now = og->m_time.GetPos();
	int half = 0;
	if (wavbit_sample_Hz > 0) {
		half = wavbit_sample_Hz * sec;
		// mode==-10 は /100 縮尺のスライダー
		if (mode == -10) half /= 100;
	} else {
		half = (mx - mn) / 40; // フォールバック: 全長の約2.5%
		if (half < 1) half = 1;
	}
	int a = now - half;
	int b = now + half;
	if (a < mn) a = mn;
	if (b > mx) b = mx;
	if (savedata.mpPhraseSnapBeat && savedata.mpDetectedBpm > 0 && wavbit_sample_Hz > 0) {
		const double beatFrames = (60.0 * (double)wavbit_sample_Hz) / (double)savedata.mpDetectedBpm;
		auto snap = [&](int v) -> int {
			const double rel = (double)(v - mn) / beatFrames;
			const int n = (int)floor(rel + 0.5);
			int out = mn + (int)(n * beatFrames + 0.5);
			if (out < mn) out = mn;
			if (out > mx) out = mx;
			return out;
		};
		a = snap(a);
		b = snap(b);
		if (b <= a) b = min(mx, a + (int)(beatFrames + 0.5));
	}
	if (b <= a) b = (a < mx) ? (a + 1) : a;
	m_abApos = a;
	m_abBpos = b;
	m_abLoopCount = 0;
	m_seek.SetAB(m_abApos, m_abBpos);
	MirrorSeekVol();
}

void CMediaPlayerDlg::OnPhraseAbNow()
{
	SetPhraseAbAroundNow();
}

void CMediaPlayerDlg::ClearWaveOverview()
{
	InterlockedIncrement(&m_waveGen);
	m_wavePeakN = 0;
	m_wavePath[0] = 0;
	m_jacketRemBucket = -1;
	ZeroMemory(m_wavePeaks, sizeof(m_wavePeaks));
	if (m_seek.GetSafeHwnd())
		m_seek.ClearWavePeaks();
}

struct MpWaveJob {
	HWND hwnd;
	LONG gen;
	TCHAR path[1024];
	float peaksL[PRO_WAVE_PEAKS];
	float peaksR[PRO_WAVE_PEAKS];
	int peakN;
};

static UINT AFX_CDECL MpWaveOverviewThread(LPVOID p)
{
	MpWaveJob* job = (MpWaveJob*)p;
	if (!job) return 0;
	int total = 0;
	job->peakN = ProAudio_BuildWaveOverview(job->path, job->peaksL, job->peaksR, PRO_WAVE_PEAKS, total);
	(void)total;
	if (job->hwnd && ::IsWindow(job->hwnd))
		::PostMessage(job->hwnd, WM_MP_WAVE_DONE, (WPARAM)job->gen, (LPARAM)job);
	else
		free(job);
	return 0;
}

void CMediaPlayerDlg::KickWaveOverview()
{
	if (!savedata.mpSeekWave || !::IsWindow(GetSafeHwnd()) || !m_seek.GetSafeHwnd())
		return;
	if (filen.IsEmpty()) return;
	// 同じ曲は再走査しない（トグルや Clear で path を空にする）。失敗時もライブ波形は別途積む。
	if (m_wavePath[0] && _tcscmp(m_wavePath, filen) == 0)
		return;
	if (InterlockedCompareExchange(&m_waveBusy, 1, 0) != 0)
		return;

	_tcsncpy(m_wavePath, filen, 1023);
	m_wavePath[1023] = 0;
	// ライブ充填を消さない（WAV 成功時のみ SetWavePeaks で置換）
	m_wavePeakN = 0;

	MpWaveJob* job = (MpWaveJob*)malloc(sizeof(MpWaveJob));
	if (!job) {
		InterlockedExchange(&m_waveBusy, 0);
		return;
	}
	ZeroMemory(job, sizeof(*job));
	job->hwnd = m_hWnd;
	job->gen = InterlockedIncrement(&m_waveGen);
	_tcsncpy(job->path, m_wavePath, 1023);
	job->path[1023] = 0;
	if (!AfxBeginThread(MpWaveOverviewThread, job, THREAD_PRIORITY_BELOW_NORMAL)) {
		free(job);
		InterlockedExchange(&m_waveBusy, 0);
	}
}

LRESULT CMediaPlayerDlg::OnWaveOverviewDone(WPARAM wParam, LPARAM lParam)
{
	MpWaveJob* job = (MpWaveJob*)lParam;
	InterlockedExchange(&m_waveBusy, 0);
	if (!job) return 0;
	const LONG gen = (LONG)wParam;
	if (gen != m_waveGen || !savedata.mpSeekWave) {
		free(job);
		return 0;
	}
	// L/R の大きい方をモノピークに畳む → スライダー上限 1024 に間引き
	const int srcN = job->peakN;
	if (srcN <= 0) {
		// 非WAV等: ライブ波形を消さない
		free(job);
		return 0;
	}
	int dstN = srcN;
	if (dstN > 1024) dstN = 1024;
	if (dstN > CCustomRangeSliderCtrl::kWavePeaksMax)
		dstN = CCustomRangeSliderCtrl::kWavePeaksMax;
	ZeroMemory(m_wavePeaks, sizeof(m_wavePeaks));
	for (int i = 0; i < dstN; ++i) {
		int a = (srcN <= dstN) ? i : (i * srcN / dstN);
		int b = (srcN <= dstN) ? (i + 1) : ((i + 1) * srcN / dstN);
		if (b <= a) b = a + 1;
		if (b > srcN) b = srcN;
		float mx = 0.f;
		for (int j = a; j < b; ++j) {
			float v = job->peaksL[j];
			if (job->peaksR[j] > v) v = job->peaksR[j];
			if (v > mx) mx = v;
		}
		m_wavePeaks[i] = mx;
	}
	m_wavePeakN = dstN;
	if (m_seek.GetSafeHwnd())
		m_seek.SetWavePeaks(m_wavePeaks, m_wavePeakN);
	free(job);
	return 0;
}

void CMediaPlayerDlg::OnLrcExpand()
{
	savedata.mpLrcExpand = savedata.mpLrcExpand ? 0 : 1;
	if (m_lrcExpand.GetSafeHwnd())
		m_lrcExpand.SetWindowText(savedata.mpLrcExpand ? L"▴" : L"▾");
	DoLayout();
	CCC_GroupBoxesBack(GetSafeHwnd());
	if (savedata.mpLrcExpand && m_lrcView.GetSafeHwnd()) {
		// グループより前面へ(不透明GBがGDI歌詞を覆わないよう z を確保)
		m_lrcView.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0,
			SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		if (og && og->lrcnum >= 2) {
			const int n = og->lrcnum - 1;
			m_lrcView.SetLines(og->lrc, n > 0 ? n : 0, og->lrctm, og->lrcnum);
			extern UINT ttt;
			extern double OggGetGdiPlaybackTimeSec();
			extern int mode;
			extern int videoonly;
			DWORD centis = ttt;
			if (!(mode == -2 || videoonly)) {
				const double sec = OggGetGdiPlaybackTimeSec();
				if (sec >= 0.0)
					centis = (DWORD)(sec * 100.0 + 0.5);
			}
			m_lrcView.SetPlayCentis(centis);
			m_lrcView.BeginCatchFromTop(); // 途中拡大: 頭から該当行へ高速 chase
		} else {
			m_lrcView.Clear();
		}
		m_lrcView.Invalidate(FALSE);
		m_lrcView.UpdateWindow();
	}
	RefreshListAfterLayout();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	if (savedata.mpLrcExpand && m_lrcView.GetSafeHwnd()) {
		// 全面再描画後に歌詞GDIを最後に載せ直す(追従スクロールが消えるのを防ぐ)
		m_lrcView.Invalidate(FALSE);
		m_lrcView.UpdateWindow();
	}
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::OnToolsToggle()
{
	// 左クリックでメニュー（並べ替えパネル・欠損整理などを一覧）
	if (!m_toolsToggle.GetSafeHwnd()) return;
	CRect r; m_toolsToggle.GetWindowRect(&r);
	ShowToolsExtrasMenu(CPoint(r.left, r.bottom));
}

void CMediaPlayerDlg::OnToolsPanelToggle()
{
	savedata.mpToolsOpen = savedata.mpToolsOpen ? 0 : 1;
	UpdateQueueChrome();
	DoLayout();
	CCC_GroupBoxesBack(GetSafeHwnd());
	RefreshListAfterLayout();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::OnFindFilter()
{
	savedata.mpFindFilter = (m_findFilter.GetCheck() == BST_CHECKED) ? 1 : 0;
	RefreshList(TRUE);
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::OnFindRegex()
{
	savedata.mpFindRegex = (m_findRegex.GetCheck() == BST_CHECKED) ? 1 : 0;
	RefreshList(TRUE);
	MpPersistSavedataQuick();
}

void MpSortPlaylistByKey(int key)
{
	if (!pl || !pl->pc || pl->playcnt <= 1) return;
	// 再生中パスを保持して並べ替え後にインデックスを戻す
	TCHAR keep[1024]; keep[0] = 0;
	int keepPnt = pl->pnt;
	if (keepPnt >= 0 && keepPnt < pl->playcnt)
		_tcsncpy_s(keep, pl->pc[keepPnt].fol, _TRUNCATE);
	if (savedata.mpSortKey == key)
		savedata.mpSortAsc = savedata.mpSortAsc ? 0 : 1;
	else {
		savedata.mpSortKey = key;
		savedata.mpSortAsc = 1;
	}
	int (*cmp)(const void*, const void*) = NULL;
	if (key == 1) cmp = savedata.mpSortAsc ? MpCmpNameAsc : MpCmpNameDesc;
	else if (key == 2) cmp = savedata.mpSortAsc ? MpCmpArtAsc : MpCmpArtDesc;
	else if (key == 3) cmp = savedata.mpSortAsc ? MpCmpAlbAsc : MpCmpAlbDesc;
	else if (key == 4) cmp = savedata.mpSortAsc ? MpCmpTimeAsc : MpCmpTimeDesc;
	if (!cmp) return;
	qsort(pl->pc, (size_t)pl->playcnt, sizeof(playlistdata0), cmp);
	if (keep[0]) {
		for (int i = 0; i < pl->playcnt; ++i) {
			if (_tcsicmp(pl->pc[i].fol, keep) == 0) {
				pl->pnt = i; plcnt = i; break;
			}
		}
	}
	pl->Save();
}

void CMediaPlayerDlg::OnSortName() { MpSortPlaylistByKey(1); RefreshList(TRUE); MpPersistSavedataQuick(); }
void CMediaPlayerDlg::OnSortArt()  { MpSortPlaylistByKey(2); RefreshList(TRUE); MpPersistSavedataQuick(); }
void CMediaPlayerDlg::OnSortAlb()  { MpSortPlaylistByKey(3); RefreshList(TRUE); MpPersistSavedataQuick(); }
void CMediaPlayerDlg::OnSortTime() { MpSortPlaylistByKey(4); RefreshList(TRUE); MpPersistSavedataQuick(); }

void CMediaPlayerDlg::OnAddFolder()
{
	if (!pl) return;
	BROWSEINFO bi; ZeroMemory(&bi, sizeof(bi));
	TCHAR path[MAX_PATH] = { 0 };
	bi.hwndOwner = GetSafeHwnd();
	bi.pszDisplayName = path;
	bi.lpszTitle = LL14(L"追加するフォルダを選んでください", L"Select a folder to add", L"Choisir un dossier", L"Scegli una cartella", L"Elija una carpeta", L"추가할 폴더 선택", L"选择要添加的文件夹", L"اختر مجلدًا للإضافة", L"Выберите папку", L"Ordner zum Hinzufugen", L"Selecione uma pasta", L"Selecteer een map", L"Wybierz folder", L"Eklenecek klasoru secin");
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
	if (!pidl) return;
	if (SHGetPathFromIDList(pidl, path))
		pl->Fol(path);
	CoTaskMemFree(pidl);
	RefreshList(TRUE);
}

void CMediaPlayerDlg::EnsureLibControls()
{
	CRect rc(0, 0, 40, 20);
	BOOL createdLibChild = FALSE;
	if (!m_plRailBg.GetSafeHwnd()) {
		m_plRailBg.Create(_T(""), WS_CHILD | SS_NOTIFY, rc, this, IDC_MP_PLRAILBG);
		m_plRailBg.SetAeroMode(FALSE);
		m_plRailBg.SetSolidFill(TRUE, COLOR_DIALOG_BG);
		m_plRailBg.ShowWindow(SW_HIDE);
		createdLibChild = TRUE;
	}
	if (!m_libToggle.GetSafeHwnd()) {
		m_libToggle.Create(_T("Lib"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_LIBTOGGLE);
		m_libToggle.SetGradation(RGB(230, 240, 255), RGB(190, 210, 240), 0, FALSE);
		m_libToggle.SetFlat(TRUE); // 狭いレール: 2px白枠が白抜けに見えるのを防ぐ
		m_libToggle.SetAeroMode(TRUE); // アクリル下地を透かす（ラベル風）
		createdLibChild = TRUE;
	}
	if (!m_histToggle.GetSafeHwnd()) {
		m_histToggle.Create(_T("Hist"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_HISTTOGGLE);
		m_histToggle.SetGradation(RGB(255, 240, 230), RGB(240, 200, 170), 0, FALSE);
		m_histToggle.SetFlat(TRUE);
		m_histToggle.SetAeroMode(TRUE);
		createdLibChild = TRUE;
	}
	if (!m_tempToggle.GetSafeHwnd()) {
		m_tempToggle.Create(_T("Temp"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_TEMPTOGGLE);
		m_tempToggle.SetGradation(RGB(235, 255, 235), RGB(180, 230, 180), 0, FALSE);
		m_tempToggle.SetFlat(TRUE);
		m_tempToggle.SetAeroMode(TRUE);
		MpMakePushToggle(&m_tempToggle);
		m_tempToggle.SetCheck(savedata.mpTempOpen ? BST_CHECKED : BST_UNCHECKED);
		createdLibChild = TRUE;
	}
	if (!m_libAddRoot.GetSafeHwnd()) {
		m_libAddRoot.Create(LL14(L"ルート追加", L"Add root", L"Aj. racine", L"Agg. radice", L"Anadir raiz", L"루트 추가", L"添加根", L"إضافة جذر", L"Добавить корень", L"Wurzel +", L"Add raiz", L"Wortel +", L"Dodaj korzen", L"Kok +"),
			WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_LIBADDROOT);
		m_libAddRoot.SetGradation(RGB(220, 240, 230), RGB(180, 220, 200), 0, TRUE);
		createdLibChild = TRUE;
	}
	if (!m_libAddPl.GetSafeHwnd()) {
		m_libAddPl.Create(LL14(L"PLへ追加", L"Add to PL", L"Aj. a liste", L"Agg. a PL", L"Aadir a PL", L"목록에 추가", L"加入列表", L"إضافة للقائمة", L"В плейлист", L"Zur PL", L"Para PL", L"Naar PL", L"Do listy", L"Listeye"),
			WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_LIBADDPL);
		m_libAddPl.SetGradation(RGB(255, 240, 220), RGB(240, 200, 160), 0, TRUE);
		createdLibChild = TRUE;
	}
	if (!m_libTree.GetSafeHwnd()) {
#ifndef TVS_NOHSCROLL
#define TVS_NOHSCROLL 0x8000
#endif
		m_libTree.Create(WS_CHILD | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_INFOTIP | TVS_NOHSCROLL,
			rc, this, IDC_MP_LIBTREE);
		m_libTree.SetAeroMode(FALSE);
		createdLibChild = TRUE;
	}
	if (!m_libAlbums.GetSafeHwnd()) {
		m_libAlbums.Create(WS_CHILD | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER,
			rc, this, IDC_MP_LIBALBUMS);
		m_libAlbums.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
		m_libAlbums.InsertColumn(0, _T("Album"), LVCFMT_LEFT, 120);
		m_libAlbums.SetAeroMode(FALSE);
		m_libAlbums.DragAcceptFiles(TRUE);
		createdLibChild = TRUE;
	}
	if (!m_histList.GetSafeHwnd()) {
		m_histList.Create(WS_CHILD | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOCOLUMNHEADER,
			rc, this, IDC_MP_HISTLIST);
		m_histList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP);
		m_histList.InsertColumn(0, _T("History"), LVCFMT_LEFT, 200);
		m_histList.SetAeroMode(FALSE);
		createdLibChild = TRUE;
	}
	if (!m_emptyFolder.GetSafeHwnd()) {
		m_emptyFolder.Create(LL14(L"フォルダをドロップ / 開く", L"Drop or open a folder", L"Deposer/ouvrir un dossier", L"Trascina/apri cartella", L"Soltar/abrir carpeta", L"폴더 드롭 / 열기", L"拖放/打开文件夹", L"إسقاط/فتح مجلد", L"Перетащите/откройте папку", L"Ordner ablegen/offnen", L"Soltar/abrir pasta", L"Map neerzetten/openen", L"Upuść/otwórz folder", L"Klasor birak/ac"),
			WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_EMPTYFOLDER);
		m_emptyFolder.SetGradation(RGB(220, 245, 230), RGB(170, 220, 190), 0, TRUE);
		createdLibChild = TRUE;
	}
	if (!m_emptyM3u.GetSafeHwnd()) {
		m_emptyM3u.Create(LL14(L"m3u を開く", L"Open m3u", L"Ouvrir m3u", L"Apri m3u", L"Abrir m3u", L"m3u 열기", L"打开 m3u", L"فتح m3u", L"Открыть m3u", L"m3u offnen", L"Abrir m3u", L"m3u openen", L"Otwórz m3u", L"m3u ac"),
			WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP, rc, this, IDC_MP_EMPTYM3U);
		m_emptyM3u.SetGradation(RGB(230, 240, 255), RGB(180, 205, 240), 0, TRUE);
		createdLibChild = TRUE;
	}
	if (m_fontChk.GetSafeHandle()) {
		if (m_libToggle.GetSafeHwnd()) m_libToggle.SetFont(&m_fontChk);
		if (m_histToggle.GetSafeHwnd()) m_histToggle.SetFont(&m_fontChk);
		if (m_tempToggle.GetSafeHwnd()) m_tempToggle.SetFont(&m_fontChk);
		if (m_libAddRoot.GetSafeHwnd()) m_libAddRoot.SetFont(&m_fontChk);
		if (m_libAddPl.GetSafeHwnd()) m_libAddPl.SetFont(&m_fontChk);
		if (m_emptyFolder.GetSafeHwnd()) m_emptyFolder.SetFont(&m_fontChk);
		if (m_emptyM3u.GetSafeHwnd()) m_emptyM3u.SetFont(&m_fontChk);
	}
	if (m_fontList.GetSafeHandle()) {
		if (m_libTree.GetSafeHwnd()) m_libTree.SetFont(&m_fontList);
		if (m_libAlbums.GetSafeHwnd()) m_libAlbums.SetFont(&m_fontList);
		if (m_histList.GetSafeHwnd()) m_histList.SetFont(&m_fontList);
	}
	// FinishBlur/CaptionApply より後に作られる子は OpaqueFixer が乗らない。
	// 再適用しないと Win11 アクリル上でツリー/アルバム/履歴が透ける。
	if (createdLibChild)
		PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
}

CString CMediaPlayerDlg::LibRootsFilePath() const
{
	TCHAR base[MAX_PATH] = { 0 };
	if (FAILED(SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, base)))
		return CString();
	CString dir = base;
	dir += _T("\\oggYSED");
	CreateDirectory(dir, NULL);
	return dir + _T("\\libroots.txt");
}

void CMediaPlayerDlg::LibLoadUserRoots(CString* outs, int maxN, int& outN)
{
	outN = 0;
	if (!outs || maxN <= 0) return;
	const CString fp = LibRootsFilePath();
	if (fp.IsEmpty()) return;
	CStdioFile f;
	if (!f.Open(fp, CFile::modeRead | CFile::typeText | CFile::shareDenyWrite))
		return;
	CString line;
	while (f.ReadString(line) && outN < maxN) {
		line.Trim();
		if (line.IsEmpty()) continue;
		if (PathIsDirectory(line))
			outs[outN++] = line;
	}
	f.Close();
}

void CMediaPlayerDlg::LibSaveUserRoots(const CString* roots, int n)
{
	const CString fp = LibRootsFilePath();
	if (fp.IsEmpty() || !roots) return;
	CStdioFile f;
	if (!f.Open(fp, CFile::modeCreate | CFile::modeWrite | CFile::typeText | CFile::shareExclusive))
		return;
	for (int i = 0; i < n; i++) {
		if (!roots[i].IsEmpty())
			f.WriteString(roots[i] + _T("\n"));
	}
	f.Close();
}

int CMediaPlayerDlg::LibAllocPath(LPCTSTR path)
{
	if (!path || !path[0]) return -1;
	if (m_libPathN >= kLibPathMax) return -1;
	m_libPathBag[m_libPathN] = path;
	return m_libPathN++;
}

CString CMediaPlayerDlg::LibItemPath(HTREEITEM h) const
{
	if (!h || !m_libTree.GetSafeHwnd()) return CString();
	const LPARAM lp = m_libTree.GetItemData(h);
	const int idx = (int)lp - 1;
	if (idx < 0 || idx >= m_libPathN) return CString();
	return m_libPathBag[idx];
}

static BOOL LibDirHasSubdir(LPCTSTR folder)
{
	if (!folder || !folder[0]) return FALSE;
	CString pat = folder;
	if (pat.Right(1) != _T("\\")) pat += _T("\\");
	pat += _T("*.*");
	CFileFind ff;
	BOOL b = ff.FindFile(pat);
	while (b) {
		b = ff.FindNextFile();
		if (ff.IsDots()) continue;
		if (ff.IsDirectory()) { ff.Close(); return TRUE; }
	}
	ff.Close();
	return FALSE;
}

static int LibCountAudioFiles(LPCTSTR folder)
{
	if (!folder || !folder[0]) return 0;
	static const TCHAR* exts[] = {
		_T(".mp3"), _T(".ogg"), _T(".flac"), _T(".wav"), _T(".m4a"), _T(".aac"),
		_T(".opus"), _T(".wma"), _T(".aif"), _T(".aiff"), _T(".dsf"), _T(".dff"),
		_T(".tta"), NULL
	};
	CString pat = folder;
	if (pat.Right(1) != _T("\\")) pat += _T("\\");
	pat += _T("*.*");
	int n = 0;
	CFileFind ff;
	BOOL b = ff.FindFile(pat);
	while (b) {
		b = ff.FindNextFile();
		if (ff.IsDots() || ff.IsDirectory()) continue;
		CString name = ff.GetFileName();
		name.MakeLower();
		for (int i = 0; exts[i]; i++) {
			const int el = (int)_tcslen(exts[i]);
			if (name.GetLength() >= el && name.Right(el) == exts[i]) { n++; break; }
		}
	}
	ff.Close();
	return n;
}

void CMediaPlayerDlg::LibFitNoHScroll(CWnd* pList)
{
	if (!pList || !pList->GetSafeHwnd()) return;
	HWND h = pList->GetSafeHwnd();
	CRect rc; ::GetClientRect(h, &rc);
	int w = rc.Width() - 4;
	if (w < 40) w = 40;
	ListView_SetColumnWidth(h, 0, w);
	::ShowScrollBar(h, SB_HORZ, FALSE);
}

// 操作ガイドのミニマップクリック → 該当パーツを 0.8 秒だけ枠で光らせる
LRESULT CMediaPlayerDlg::OnMpHelpHighlight(WPARAM wParam, LPARAM lParam)
{
	if (!::IsWindow(GetSafeHwnd())) return 0;
	m_pulseHighlightId = (int)wParam;
	m_pulseUntil = GetTickCount() + 800;
	// lParam==1: ガイドの Soft3D 章を開いた合図。バナー上でツアーヒントを流す。
	if (lParam == 1 && IsBannerSoft3D())
		m_soft3dTourUntil = GetTickCount() + 6000;
	Invalidate(FALSE);
	return 0;
}

void CMediaPlayerDlg::DrawHighlightPulse(CDC* pDC)
{
	if (!pDC || m_pulseHighlightId < 0) return;
	const DWORD now = GetTickCount();
	if (now >= m_pulseUntil) {
		m_pulseHighlightId = -1;
		return;
	}
	CRect rc;
	switch (m_pulseHighlightId) {
	case 0: // Lib
		if (m_plRailRect.IsRectEmpty()) return;
		rc = m_plRailRect;
		break;
	case 1: // Banner
		if (m_bannerRect.IsRectEmpty()) return;
		rc = m_bannerRect;
		break;
	case 2: // Play
		if (m_play.GetSafeHwnd()) m_play.GetWindowRect(&rc);
		else return;
		ScreenToClient(&rc);
		break;
	case 3: // Sound
		if (m_vol.GetSafeHwnd()) m_vol.GetWindowRect(&rc);
		else return;
		ScreenToClient(&rc);
		break;
	case 4: // List
		if (m_list.GetSafeHwnd()) m_list.GetWindowRect(&rc);
		else return;
		ScreenToClient(&rc);
		break;
	case 5: // Lyrics
		if (m_lrc.GetSafeHwnd()) m_lrc.GetWindowRect(&rc);
		else if (!m_infoPanelRect.IsRectEmpty()) rc = m_infoPanelRect;
		else return;
		if (m_lrc.GetSafeHwnd()) ScreenToClient(&rc);
		break;
	case 6: // Tools
		if (m_toolsToggle.GetSafeHwnd()) m_toolsToggle.GetWindowRect(&rc);
		else return;
		ScreenToClient(&rc);
		break;
	default:
		return;
	}
	rc.InflateRect(2, 2);
	CPen pen(PS_SOLID, 2, RGB(255, 160, 60));
	CPen* old = pDC->SelectObject(&pen);
	CBrush* oldBr = (CBrush*)pDC->SelectStockObject(NULL_BRUSH);
	pDC->Rectangle(&rc);
	if (oldBr) pDC->SelectObject(oldBr);
	pDC->SelectObject(old);
	// パルス中は次フレームも再描画
	InvalidateRect(&rc, FALSE);
}

void CMediaPlayerDlg::DrawTrackFadeOverlay(CDC* pDC, const CRect& rc)
{
	if (!pDC || rc.IsRectEmpty() || m_trackFadeStart == 0) return;
	/* ジャケット矩形への適用は呼び出し側で除外済み。念のため拒否 */
	if (!m_jacketRect.IsRectEmpty() && rc.EqualRect(m_jacketRect))
		return;
	extern volatile LONG g_xfInProgress, g_xfOpening;
	extern ULONGLONG g_xfJacketStableUntil;
	if (InterlockedCompareExchange(&g_xfInProgress, 0, 0)
		|| InterlockedCompareExchange(&g_xfOpening, 0, 0)
		|| (g_xfJacketStableUntil != 0 && GetTickCount64() < g_xfJacketStableUntil)) {
		m_trackFadeStart = 0;
		return;
	}
	const DWORD el = GetTickCount() - m_trackFadeStart;
	if (el >= 260) {
		m_trackFadeStart = 0;
		return;
	}
	const int a = 220 - (int)(el * 220 / 260);
	if (a <= 0) return;
	const int w = rc.Width(), h = rc.Height();
	if (w < 2 || h < 2) return;
	BITMAPINFO bi = {};
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	void* bits = nullptr;
	HBITMAP dib = ::CreateDIBSection(pDC->GetSafeHdc(), &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!dib || !bits) return;
	CDC mem;
	mem.CreateCompatibleDC(pDC);
	HGDIOBJ old = ::SelectObject(mem.GetSafeHdc(), dib);
	mem.FillSolidRect(0, 0, w, h, RGB(12, 14, 22));
	BLENDFUNCTION bf = {};
	bf.BlendOp = AC_SRC_OVER;
	bf.SourceConstantAlpha = (BYTE)a;
	bf.AlphaFormat = 0;
	::AlphaBlend(pDC->GetSafeHdc(), rc.left, rc.top, w, h, mem.GetSafeHdc(), 0, 0, w, h, bf);
	::SelectObject(mem.GetSafeHdc(), old);
	::DeleteObject(dib);
	InvalidateRect(&rc, FALSE);
}

void CMediaPlayerDlg::UpdateUxStatusAndChips()
{
	CString status;
	CString chip;
	CWnd* pFocus = GetFocus();
	if (pFocus && ::IsWindow(pFocus->GetSafeHwnd())) {
		const int id = pFocus->GetDlgCtrlID();
		if (id == IDC_MP_FIND)
			status = LL14(L"検索欄: Enter で次候補", L"Find: Enter = next hit", L"Recherche: Entree = suivant", L"Cerca: Invio = successivo", L"Buscar: Enter = siguiente", L"검색: Enter=다음", L"搜索：Enter 下一个", L"بحث: Enter التالي", L"Поиск: Enter — далее", L"Suche: Enter = naechster", L"Busca: Enter = proximo", L"Zoeken: Enter = volgende", L"Szukaj: Enter = nastepny", L"Ara: Enter = sonraki");
		else if (id == IDC_MP_PLAY || id == IDC_MP_PAUSE)
			status = LL14(L"再生操作: Space でも切替", L"Playback: Space also toggles", L"Lecture: Espace bascule aussi", L"Play: Spazio alterna", L"Play: Espacio tambien", L"재생: Space로도 전환", L"播放：也可用 Space 切换", L"تشغيل: Space أيضاً", L"Воспроизведение: Space тоже", L"Wiedergabe: Leertaste wechselt", L"Play: Espaco tambem", L"Play: Spatie wisselt ook", L"Play: Spacja tez", L"Play: Space de degistirir");
	}
	if (status.IsEmpty()) {
		if (IsBannerSoft3D())
			status = LL14(L"簡易3D: ドラッグ=回転 / ホイール=ズーム / 0=リセット / Ctrl+K=パレット", L"Soft 3D: drag=orbit / wheel=zoom / 0=reset / Ctrl+K=palette", L"Soft 3D: glisser=orbite / molette=zoom / 0=reset / Ctrl+K", L"Soft 3D: trascina=orbita / rotella=zoom / 0=reset / Ctrl+K", L"Soft 3D: arrastrar=órbita / rueda=zoom / 0=reset / Ctrl+K", L"간이3D: 드래그=회전 / 휠=줌 / 0=리셋 / Ctrl+K", L"简易3D：拖=旋转 / 轮=缩放 / 0=复位 / Ctrl+K", L"Soft 3D: سحب=دوران / عجلة=تكبير / 0=تصفير / Ctrl+K", L"Soft 3D: тянуть=облёт / колесо=зум / 0=сброс / Ctrl+K", L"Soft 3D: Ziehen=Orbit / Rad=Zoom / 0=Reset / Ctrl+K", L"Soft 3D: arraste=orbita / roda=zoom / 0=reset / Ctrl+K", L"Soft 3D: slepen=orbit / wiel=zoom / 0=reset / Ctrl+K", L"Soft 3D: przeciągnij=orbita / kółko=zoom / 0=reset / Ctrl+K", L"Soft 3D: sürükle=yörünge / teker=zoom / 0=sıfırla / Ctrl+K");
		else
			status = LL14(L"?:操作ガイド  Ctrl+K:コマンドパレット  Space:再生/一時停止", L"?: guide  Ctrl+K: command palette  Space: play/pause", L"?: guide  Ctrl+K: palette  Espace: lecture/pause", L"?: guida  Ctrl+K: palette  Spazio: play/pausa", L"?: guía  Ctrl+K: paleta  Espacio: play/pausa", L"?:가이드  Ctrl+K:팔레트  Space:재생/일시정지", L"?:指南  Ctrl+K:命令面板  Space:播放/暂停", L"?:دليل  Ctrl+K:لوحة أوامر  Space:تشغيل/إيقاف", L"?:руководство  Ctrl+K:палитра  Space:play/пауза", L"?:Guide  Ctrl+K:Palette  Leertaste:Play/Pause", L"?:guia  Ctrl+K:paleta  Espaco:play/pausa", L"?:gids  Ctrl+K:palet  Spatie:play/pauze", L"?:przewodnik  Ctrl+K:paleta  Spacja:play/pauza", L"?:kılavuz  Ctrl+K:palet  Space:play/duraklat");
	}
	{
		const CString extra = MpFeatStatusLine();
		if (!extra.IsEmpty()) {
			status += L"  ·  ";
			status += extra;
		}
	}
	if (plf)
		chip = LL14(L"Space=一時停止", L"Space=Pause", L"Espace=Pause", L"Spazio=Pausa", L"Espacio=Pausa", L"Space=일시정지", L"Space=暂停", L"Space=إيقاف", L"Space=Пауза", L"Leertaste=Pause", L"Espaco=Pausa", L"Spatie=Pauze", L"Spacja=Pauza", L"Space=Duraklat");
	else
		chip = LL14(L"Space=再生", L"Space=Play", L"Espace=Lecture", L"Spazio=Play", L"Espacio=Play", L"Space=재생", L"Space=播放", L"Space=تشغيل", L"Space=Play", L"Leertaste=Play", L"Espaco=Play", L"Spatie=Play", L"Spacja=Play", L"Space=Play");
	if (IsBannerSoft3D()) {
		chip += L"  |  ";
		chip += LL14(L"0=視点リセット", L"0=Reset view", L"0=Reset vue", L"0=Reset vista", L"0=Restablecer", L"0=시점 리셋", L"0=重置视角", L"0=تصفير العرض", L"0=Сброс вида", L"0=Ansicht zurueck", L"0=Redefinir vista", L"0=Weergave reset", L"0=Reset widoku", L"0=Gorunum sifirla");
	}
	if (m_soft3dPerfHintUntil && GetTickCount() < m_soft3dPerfHintUntil
		&& (savedata.soft3dPerfHintDismiss & 1) == 0) {
		chip += L"  |  ";
		chip += LL14(L"クリックで2Dへ", L"Click→2D", L"Clic→2D", L"Clic→2D", L"Clic→2D", L"클릭→2D", L"点击→2D", L"نقر→2D", L"Клик→2D", L"Klick→2D", L"Clique→2D", L"Klik→2D", L"Klik→2D", L"Tik→2D");
	}
	const bool changed = (status != m_uxStatusText) || (chip != m_uxChipText);
	m_uxStatusText = status;
	m_uxChipText = chip;
	if (changed)
		Invalidate(FALSE);
}

void CMediaPlayerDlg::DrawUxStatusBand(CDC* pDC)
{
	if (!pDC) return;
	CRect rc;
	GetClientRect(&rc);
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const float s = (hD2 > 0.f) ? hD2 : 1.f;
	const int bandH = max(16, (int)(18 * s));
	const int bandPad = max(2, (int)(2 * s));
	CRect band(rc.left + 6, rc.bottom - bandH - bandPad, rc.right - 6, rc.bottom - bandPad);
	if (band.top <= capH) return;
	pDC->FillSolidRect(&band, RGB(248, 248, 252));
	pDC->SetBkMode(TRANSPARENT);
	CFont* old = pDC->SelectObject(GetFont());
	pDC->SetTextColor(RGB(70, 70, 90));
	CString left = m_uxStatusText;
	CString right = m_uxChipText;
	CRect leftRc = band;
	if (!right.IsEmpty()) {
		CSize sz = pDC->GetTextExtent(right);
		CRect chipRc(band.right - sz.cx - 10, band.top + 1, band.right - 2, band.bottom - 1);
		pDC->FillSolidRect(&chipRc, RGB(255, 236, 210));
		pDC->SetTextColor(RGB(120, 70, 20));
		pDC->DrawText(right, &chipRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
		leftRc.right = chipRc.left - 6;
		pDC->SetTextColor(RGB(70, 70, 90));
	}
	pDC->DrawText(left, &leftRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
	if (old) pDC->SelectObject(old);
}

LRESULT CMediaPlayerDlg::OnLibBuildLazy(WPARAM, LPARAM)
{
	if (!::IsWindow(GetSafeHwnd())) return 0;
	if (!savedata.mpLibOpen) { m_libBuildPosted = 0; return 0; }
	if (!m_libTreeBuilt)
		LibRebuildTree();
	else
		m_libBuildPosted = 0;
	return 0;
}

void CMediaPlayerDlg::LibRebuildTree()
{
	EnsureLibControls();
	if (!m_libTree.GetSafeHwnd()) return;
	m_libTree.SetRedraw(FALSE);
	m_libTree.DeleteAllItems();
	m_libPathN = 0;
	m_libSelFolder.Empty();
	m_albumN = 0;
	if (m_libAlbums.GetSafeHwnd())
		m_libAlbums.DeleteAllItems();

	auto insertNode = [&](HTREEITEM parent, LPCTSTR title, LPCTSTR path, BOOL lazy) -> HTREEITEM {
		const int idx = path ? LibAllocPath(path) : -1;
		HTREEITEM h = m_libTree.InsertItem(title, parent, TVI_LAST);
		m_libTree.SetItemData(h, (idx >= 0) ? (LPARAM)(idx + 1) : 0);
		if (lazy && path)
			m_libTree.InsertItem(_T("…"), h, TVI_LAST);
		return h;
	};

	HTREEITEM hLib = m_libTree.InsertItem(
		LL14(L"ライブラリ", L"Library", L"Bibliotheque", L"Libreria", L"Biblioteca", L"라이브러리", L"库", L"المكتبة", L"Библиотека", L"Bibliothek", L"Biblioteca", L"Bibliotheek", L"Biblioteka", L"Kitaplik"),
		TVI_ROOT, TVI_LAST);
	m_libTree.SetItemData(hLib, 0);
	CString roots[64];
	int rootN = 0;
	LibLoadUserRoots(roots, 64, rootN);
	if (rootN == 0) {
		HTREEITEM hHint = m_libTree.InsertItem(
			LL14(L"(ルート追加で登録)", L"(Add roots)", L"(Ajoutez des racines)", L"(Aggiungi radici)", L"(Anada raices)", L"(루트 추가)", L"(请添加根)", L"(أضف جذوراً)", L"(Добавьте корни)", L"(Wurzeln hinzufugen)", L"(Adicione raizes)", L"(Voeg wortels toe)", L"(Dodaj korzenie)", L"(Kok ekle)"),
			hLib, TVI_LAST);
		m_libTree.SetItemData(hHint, 0);
	}
	else {
		for (int i = 0; i < rootN; i++) {
			CString leaf = roots[i];
			const int slash = max(leaf.ReverseFind(_T('\\')), leaf.ReverseFind(_T('/')));
			if (slash >= 0 && slash + 1 < leaf.GetLength())
				leaf = leaf.Mid(slash + 1);
			if (leaf.IsEmpty()) leaf = roots[i];
			insertNode(hLib, leaf, roots[i], TRUE);
		}
	}
	m_libTree.Expand(hLib, TVE_EXPAND);

	HTREEITEM hPc = m_libTree.InsertItem(
		LL14(L"PC", L"This PC", L"Ce PC", L"Questo PC", L"Este PC", L"내 PC", L"此电脑", L"هذا الكمبيوتر", L"Этот ПК", L"Dieser PC", L"Este PC", L"Deze pc", L"Ten komputer", L"Bu PC"),
		TVI_ROOT, TVI_LAST);
	m_libTree.SetItemData(hPc, 0);
	TCHAR drives[256] = { 0 };
	GetLogicalDriveStrings(255, drives);
	for (TCHAR* p = drives; *p; p += _tcslen(p) + 1) {
		const UINT dtype = GetDriveType(p);
		if (dtype != DRIVE_FIXED && dtype != DRIVE_REMOVABLE && dtype != DRIVE_REMOTE && dtype != DRIVE_CDROM)
			continue;
		CString label = p;
		if (label.Right(1) == _T("\\")) label = label.Left(label.GetLength() - 1);
		insertNode(hPc, label, p, TRUE);
	}
	// ドライブ列挙は済んでいるので PC 展開は軽い。展開直後の再描画不足を防ぐ。
	m_libTree.Expand(hPc, TVE_EXPAND);

	HTREEITEM hSmart = m_libTree.InsertItem(
		LL14(L"スマート", L"Smart", L"Smart", L"Smart", L"Smart", L"스마트", L"智能", L"ذكي", L"Умные", L"Smart", L"Smart", L"Smart", L"Smart", L"Akilli"),
		TVI_ROOT, TVI_LAST);
	m_libTree.SetItemData(hSmart, (DWORD_PTR)0x7FFFFFFE);
	for (int si = 0; si < MpSmart_Count(); ++si) {
		MpSmartRule r;
		if (!MpSmart_Get(si, r)) continue;
		HTREEITEM hR = m_libTree.InsertItem(MpSmart_UiLabel(r), hSmart, TVI_LAST);
		m_libTree.SetItemData(hR, (DWORD_PTR)(0x80000000u | (unsigned)(si + 1)));
	}
	m_libTree.Expand(hSmart, TVE_EXPAND);

	m_libTree.SetRedraw(TRUE);
	m_libTree.Invalidate(FALSE);
	m_libTree.UpdateWindow();
	m_libTree.ScheduleOpaqueRepaint();
	m_libTreeBuilt = 1;
	m_libBuildPosted = 0;
}

void CMediaPlayerDlg::LibFillChildren(HTREEITEM hParent)
{
	if (!hParent || !m_libTree.GetSafeHwnd()) return;
	const CString folder = LibItemPath(hParent);
	if (folder.IsEmpty()) return;

	BOOL hasDummy = FALSE;
	BOOL hasReal = FALSE;
	HTREEITEM hChild = m_libTree.GetChildItem(hParent);
	while (hChild) {
		const CString t = m_libTree.GetItemText(hChild);
		if (t == _T("…")) hasDummy = TRUE;
		else if (!LibItemPath(hChild).IsEmpty()) hasReal = TRUE;
		hChild = m_libTree.GetNextSiblingItem(hChild);
	}
	if (hasReal && !hasDummy) return;

	m_libTree.SetRedraw(FALSE);
	// 子を全削除して再構築
	hChild = m_libTree.GetChildItem(hParent);
	while (hChild) {
		HTREEITEM hNext = m_libTree.GetNextSiblingItem(hChild);
		m_libTree.DeleteItem(hChild);
		hChild = hNext;
	}

	CString pat = folder;
	if (pat.Right(1) != _T("\\")) pat += _T("\\");
	pat += _T("*.*");
	CFileFind ff;
	BOOL b = ff.FindFile(pat);
	CString names[256];
	CString paths[256];
	int n = 0;
	while (b && n < 256) {
		b = ff.FindNextFile();
		if (ff.IsDots() || !ff.IsDirectory()) continue;
		names[n] = ff.GetFileName();
		paths[n] = ff.GetFilePath();
		n++;
	}
	ff.Close();
	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			if (names[j].CompareNoCase(names[i]) < 0) {
				CString tn = names[i]; names[i] = names[j]; names[j] = tn;
				CString tp = paths[i]; paths[i] = paths[j]; paths[j] = tp;
			}
		}
	}
	for (int i = 0; i < n; i++) {
		const int idx = LibAllocPath(paths[i]);
		HTREEITEM h = m_libTree.InsertItem(names[i], hParent, TVI_LAST);
		m_libTree.SetItemData(h, (idx >= 0) ? (LPARAM)(idx + 1) : 0);
		m_libTree.InsertItem(_T("…"), h, TVI_LAST);
	}
	m_libTree.SetRedraw(TRUE);
	m_libTree.Invalidate(FALSE);
	m_libTree.UpdateWindow();
	m_libTree.ScheduleOpaqueRepaint();
}

void CMediaPlayerDlg::LibFillAlbums(LPCTSTR folder)
{
	EnsureLibControls();
	if (!m_libAlbums.GetSafeHwnd()) return;
	m_libAlbums.SetRedraw(FALSE);
	m_libAlbums.DeleteAllItems();
	m_albumN = 0;
	ZeroMemory(m_albumIsFile, sizeof(m_albumIsFile));
	m_libSelFolder = folder ? folder : _T("");
	if (m_libSelFolder.IsEmpty() || !PathIsDirectory(m_libSelFolder)) {
		m_libAlbums.SetRedraw(TRUE);
		return;
	}

	static const TCHAR* exts[] = {
		_T(".mp3"), _T(".ogg"), _T(".flac"), _T(".wav"), _T(".m4a"), _T(".aac"),
		_T(".opus"), _T(".wma"), _T(".aif"), _T(".aiff"), _T(".dsf"), _T(".dff"),
		_T(".tta"), _T(".mp4"), _T(".mkv"), _T(".avi"), _T(".wmv"), NULL
	};
	auto isAudio = [&](CString name) -> BOOL {
		name.MakeLower();
		for (int i = 0; exts[i]; i++) {
			const int el = (int)_tcslen(exts[i]);
			if (name.GetLength() >= el && name.Right(el) == exts[i]) return TRUE;
		}
		return FALSE;
	};

	CString pat = m_libSelFolder;
	if (pat.Right(1) != _T("\\")) pat += _T("\\");
	pat += _T("*.*");
	CFileFind ff;
	BOOL b = ff.FindFile(pat);
	while (b && m_albumN < kLibAlbumMax) {
		b = ff.FindNextFile();
		if (ff.IsDots() || !ff.IsDirectory()) continue;
		m_albumPathBag[m_albumN] = ff.GetFilePath();
		m_albumIsFile[m_albumN] = 0;
		CString title = ff.GetFileName();
		const int row = m_libAlbums.InsertItem(m_albumN, title);
		m_libAlbums.SetItemData(row, (DWORD_PTR)m_albumN);
		m_albumN++;
	}
	ff.Close();

	b = ff.FindFile(pat);
	while (b && m_albumN < kLibAlbumMax) {
		b = ff.FindNextFile();
		if (ff.IsDots() || ff.IsDirectory()) continue;
		CString name = ff.GetFileName();
		if (!isAudio(name)) continue;
		m_albumPathBag[m_albumN] = ff.GetFilePath();
		m_albumIsFile[m_albumN] = 1;
		CString title = CString(_T("♪ ")) + name;
		const int row = m_libAlbums.InsertItem(m_albumN, title);
		m_libAlbums.SetItemData(row, (DWORD_PTR)m_albumN);
		m_albumN++;
	}
	ff.Close();

	if (m_albumN == 0) {
		m_albumPathBag[m_albumN] = m_libSelFolder;
		m_albumIsFile[m_albumN] = 0;
		CString title = LL14(L"このフォルダを追加", L"Add this folder", L"Ajouter ce dossier", L"Aggiungi cartella", L"Anadir carpeta", L"이 폴더 추가", L"添加此文件夹", L"إضافة هذا المجلد", L"Добавить папку", L"Diesen Ordner", L"Adicionar pasta", L"Deze map", L"Dodaj folder", L"Bu klasoru ekle");
		const int row = m_libAlbums.InsertItem(0, title);
		m_libAlbums.SetItemData(row, (DWORD_PTR)m_albumN);
		m_albumN++;
	}
	m_libAlbums.SetRedraw(TRUE);
	m_libAlbums.Invalidate(FALSE);
	LibFitNoHScroll(&m_libAlbums);
}

void CMediaPlayerDlg::LibAddToPlaylist(LPCTSTR folder)
{
	LibAddPath(folder, FALSE);
}

void CMediaPlayerDlg::LibAddPath(LPCTSTR path, BOOL playAfter)
{
	if (!pl || !path || !path[0]) return;
	CWaitCursor wc;
	const int before = pl->playcnt;
	const CString want = NormalizePlaylistPath(path);
	pl->Fol(path);
	RefreshList(TRUE);
	UpdateEmptyStateUi();
	if (!playAfter) return;
	int idx = pl->FindByPath(path);
	if (idx < 0 && !want.IsEmpty())
		idx = pl->FindByPath(want);
	if (idx < 0 && pl->playcnt > before)
		idx = before;
	if (idx >= 0)
		MP_PlayIndex(idx);
}

BOOL CMediaPlayerDlg::LibDropHitTestPlaylist(CPoint ptClient) const
{
	if (!m_list.GetSafeHwnd()) return FALSE;
	CPoint sp = ptClient;
	const_cast<CMediaPlayerDlg*>(this)->ClientToScreen(&sp);
	HWND h = ::WindowFromPoint(sp);
	while (h) {
		if (h == m_list.GetSafeHwnd()) return TRUE;
		h = ::GetParent(h);
	}
	return FALSE;
}

void CMediaPlayerDlg::OnLibToggle()
{
	if (savedata.mpLibOpen) {
		savedata.mpLibOpen = 0;
	}
	else {
		// Temp 中なら先に退出(保存済みPLへ戻す)
		if (savedata.mpTempOpen && pl) {
			savedata.mpTempOpen = 0;
			pl->m_tempMode = 0;
			pl->Load(TRUE);
			if (pl->m_lc.GetSafeHwnd())
				pl->m_lc.SetItemCount(pl->playcnt);
			m_queueN = 0;
			RefreshList(TRUE);
			if (m_grpPl.GetSafeHwnd())
				m_grpPl.SetWindowText(LL14(L"プレイリスト", L"Playlist", L"Liste", L"Playlist", L"Lista", L"재생목록", L"播放列表", L"قائمة", L"Плейлист", L"Playlist", L"Lista", L"Playlist", L"Lista", L"Liste"));
			if (m_plsel.GetSafeHwnd()) m_plsel.EnableWindow(TRUE);
			if (m_plrename.GetSafeHwnd()) m_plrename.EnableWindow(TRUE);
			if (m_pldelete.GetSafeHwnd()) m_pldelete.EnableWindow(TRUE);
		}
		savedata.mpLibOpen = 1;
		savedata.mpHistOpen = 0;
		savedata.mpTempOpen = 0;
		m_libTreeBuilt = 0;
		m_libBuildPosted = 0;
	}
	if (m_libToggle.GetSafeHwnd())
		m_libToggle.SetWindowText(savedata.mpLibOpen ? L"≪" : L"Lib");
	if (m_histToggle.GetSafeHwnd())
		m_histToggle.SetWindowText(savedata.mpHistOpen ? L"≪" : L"Hist");
	if (m_tempToggle.GetSafeHwnd()) {
		m_tempToggle.SetWindowText(L"Temp");
		m_tempToggle.SetCheck(savedata.mpTempOpen ? BST_CHECKED : BST_UNCHECKED);
		m_tempToggle.EnsureAnimTimer();
		m_tempToggle.RepaintClient();
	}
	DoLayout();
	CCC_GroupBoxesBack(GetSafeHwnd());
	RefreshListAfterLayout();
	// 開いた直後にツリー/リストがアクリル穴になるのを防ぐ（遅延生成子向け）
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	if (m_libTree.GetSafeHwnd() && savedata.mpLibOpen)
		m_libTree.ScheduleOpaqueRepaint();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::OnLibAddRoot()
{
	BROWSEINFO bi; ZeroMemory(&bi, sizeof(bi));
	TCHAR path[MAX_PATH] = { 0 };
	bi.hwndOwner = GetSafeHwnd();
	bi.pszDisplayName = path;
	bi.lpszTitle = LL14(L"ライブラリのルートフォルダを選んでください", L"Select a library root folder", L"Choisir une racine", L"Scegli una radice", L"Elija una raiz", L"라이브러리 루트 선택", L"选择库根文件夹", L"اختر جذر المكتبة", L"Выберите корень библиотеки", L"Bibliothekswurzel wahlen", L"Selecione a raiz", L"Selecteer een wortel", L"Wybierz korzen", L"Kok klasoru secin");
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
	if (!pidl) return;
	if (SHGetPathFromIDList(pidl, path)) {
		CString roots[64];
		int n = 0;
		LibLoadUserRoots(roots, 64, n);
		BOOL dup = FALSE;
		for (int i = 0; i < n; i++) {
			if (roots[i].CompareNoCase(path) == 0) { dup = TRUE; break; }
		}
		if (!dup && n < 64) {
			roots[n++] = path;
			LibSaveUserRoots(roots, n);
		}
		m_libTreeBuilt = 0;
		LibRebuildTree();
	}
	CoTaskMemFree(pidl);
}

void CMediaPlayerDlg::OnLibAddPl()
{
	CString folder;
	if (m_libAlbums.GetSafeHwnd()) {
		POSITION pos = m_libAlbums.GetFirstSelectedItemPosition();
		if (pos) {
			const int row = m_libAlbums.GetNextSelectedItem(pos);
			const int idx = (int)m_libAlbums.GetItemData(row);
			if (idx >= 0 && idx < m_albumN)
				folder = m_albumPathBag[idx];
		}
	}
	if (folder.IsEmpty())
		folder = m_libSelFolder;
	LibAddToPlaylist(folder);
}

void CMediaPlayerDlg::OnLibTreeSel(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMTREEVIEW p = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	if (!p) return;
	const DWORD_PTR data = m_libTree.GetItemData(p->itemNew.hItem);
	if (data & 0x80000000u) {
		const int sid = (int)(data & 0x7FFFFFFFu) - 1;
		m_activeSmartId = sid;
		m_smartFilt = 0;
		RefreshList(TRUE);
		return;
	}
	if (data == 0x7FFFFFFE) {
		// smart root: open editor
		OnSmartEdit();
		return;
	}
	const CString path = LibItemPath(p->itemNew.hItem);
	LibFillAlbums(path);
}

void CMediaPlayerDlg::OnLibTreeExpanding(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMTREEVIEW p = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	if (!p) return;
	if ((p->action & TVE_EXPAND) == 0) return;
	LibFillChildren(p->itemNew.hItem);
	if (m_libTree.GetSafeHwnd()) {
		m_libTree.Invalidate(FALSE);
		m_libTree.ScheduleOpaqueRepaint();
	}
}

void CMediaPlayerDlg::OnLibAlbumDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMITEMACTIVATE p = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	if (!p || p->iItem < 0) return;
	const int idx = (int)m_libAlbums.GetItemData(p->iItem);
	if (idx < 0 || idx >= m_albumN) return;
	LibAddPath(m_albumPathBag[idx], TRUE);
}

void CMediaPlayerDlg::LibStartFolderDrag(LPCTSTR path, CPoint ptClient)
{
	if (!path || !path[0]) return;
	if (!PathFileExists(path) && !PathIsDirectory(path)) return;
	m_libDragFolder = path;
	m_libDrag = 1;
	if (m_hLibDragImage) {
		ImageList_Destroy(m_hLibDragImage);
		m_hLibDragImage = NULL;
	}
	CString leaf = path;
	const int slash = max(leaf.ReverseFind(_T('\\')), leaf.ReverseFind(_T('/')));
	if (slash >= 0 && slash + 1 < leaf.GetLength())
		leaf = leaf.Mid(slash + 1);
	if (leaf.GetLength() > 28)
		leaf = leaf.Left(25) + _T("...");
	CClientDC dc(this);
	CFont* oldF = NULL;
	if (m_fontList.GetSafeHandle())
		oldF = dc.SelectObject(&m_fontList);
	CSize sz = dc.GetTextExtent(leaf);
	if (oldF) dc.SelectObject(oldF);
	int bw = sz.cx + 24;
	int bh = max(22, sz.cy + 8);
	if (bw > 220) bw = 220;
	CBitmap bmp;
	bmp.CreateCompatibleBitmap(&dc, bw, bh);
	CDC mem; mem.CreateCompatibleDC(&dc);
	CBitmap* ob = mem.SelectObject(&bmp);
	mem.FillSolidRect(0, 0, bw, bh, RGB(255, 245, 230));
	mem.Draw3dRect(0, 0, bw, bh, RGB(200, 140, 80), RGB(160, 100, 40));
	if (m_fontList.GetSafeHandle())
		mem.SelectObject(&m_fontList);
	mem.SetBkMode(TRANSPARENT);
	mem.SetTextColor(RGB(40, 40, 40));
	mem.TextOut(8, (bh - sz.cy) / 2, leaf);
	mem.SelectObject(ob);
	mem.DeleteDC();
	m_hLibDragImage = ImageList_Create(bw, bh, ILC_COLOR24, 1, 1);
	if (m_hLibDragImage) {
		ImageList_Add(m_hLibDragImage, (HBITMAP)bmp.GetSafeHandle(), NULL);
		CPoint sp = ptClient;
		ClientToScreen(&sp);
		ImageList_BeginDrag(m_hLibDragImage, 0, 8, 8);
		ImageList_DragEnter(::GetDesktopWindow(), sp.x, sp.y);
	}
	SetCapture();
	SetCursor(::LoadCursor(NULL, IDC_HAND));
}

void CMediaPlayerDlg::OnLibTreeBeginDrag(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMTREEVIEW p = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	if (!p || !p->itemNew.hItem) return;
	const CString path = LibItemPath(p->itemNew.hItem);
	if (path.IsEmpty()) return;
	LibStartFolderDrag(path, p->ptDrag);
}

void CMediaPlayerDlg::OnLibAlbumBeginDrag(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMLISTVIEW p = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	if (!p || p->iItem < 0) return;
	const int idx = (int)m_libAlbums.GetItemData(p->iItem);
	if (idx < 0 || idx >= m_albumN) return;
	LibStartFolderDrag(m_albumPathBag[idx], p->ptAction);
}

void CMediaPlayerDlg::OnHistToggle()
{
	if (savedata.mpHistOpen) {
		savedata.mpHistOpen = 0;
	}
	else {
		if (savedata.mpTempOpen && pl) {
			savedata.mpTempOpen = 0;
			pl->m_tempMode = 0;
			pl->Load(TRUE);
			if (pl->m_lc.GetSafeHwnd())
				pl->m_lc.SetItemCount(pl->playcnt);
			m_queueN = 0;
			RefreshList(TRUE);
			if (m_grpPl.GetSafeHwnd())
				m_grpPl.SetWindowText(LL14(L"プレイリスト", L"Playlist", L"Liste", L"Playlist", L"Lista", L"재생목록", L"播放列表", L"قائمة", L"Плейлист", L"Playlist", L"Lista", L"Playlist", L"Lista", L"Liste"));
			if (m_plsel.GetSafeHwnd()) m_plsel.EnableWindow(TRUE);
			if (m_plrename.GetSafeHwnd()) m_plrename.EnableWindow(TRUE);
			if (m_pldelete.GetSafeHwnd()) m_pldelete.EnableWindow(TRUE);
		}
		savedata.mpHistOpen = 1;
		savedata.mpLibOpen = 0;
		savedata.mpTempOpen = 0;
		m_histBuilt = 0;
	}
	if (m_libToggle.GetSafeHwnd())
		m_libToggle.SetWindowText(savedata.mpLibOpen ? L"≪" : L"Lib");
	if (m_histToggle.GetSafeHwnd())
		m_histToggle.SetWindowText(savedata.mpHistOpen ? L"≪" : L"Hist");
	if (m_tempToggle.GetSafeHwnd()) {
		m_tempToggle.SetWindowText(L"Temp");
		m_tempToggle.SetCheck(savedata.mpTempOpen ? BST_CHECKED : BST_UNCHECKED);
		m_tempToggle.EnsureAnimTimer();
		m_tempToggle.RepaintClient();
	}
	DoLayout();
	CCC_GroupBoxesBack(GetSafeHwnd());
	RefreshListAfterLayout();
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::OnTempToggle()
{
	if (savedata.mpTempOpen) {
		// OFF: 一時破棄 → ディスクのPLへ戻す
		savedata.mpTempOpen = 0;
		if (pl) {
			pl->m_tempMode = 0;
			pl->Load(TRUE);
			if (pl->m_lc.GetSafeHwnd())
				pl->m_lc.SetItemCount(pl->playcnt);
		}
		m_queueN = 0;
		if (m_grpPl.GetSafeHwnd())
			m_grpPl.SetWindowText(LL14(L"プレイリスト", L"Playlist", L"Liste", L"Playlist", L"Lista", L"재생목록", L"播放列表", L"قائمة", L"Плейлист", L"Playlist", L"Lista", L"Playlist", L"Lista", L"Liste"));
		if (m_plsel.GetSafeHwnd()) m_plsel.EnableWindow(TRUE);
		if (m_plrename.GetSafeHwnd()) m_plrename.EnableWindow(TRUE);
		if (m_pldelete.GetSafeHwnd()) m_pldelete.EnableWindow(TRUE);
	}
	else {
		// ON: 現行PLを保存してから空の一時へ
		savedata.mpTempOpen = 1;
		savedata.mpLibOpen = 0;
		savedata.mpHistOpen = 0;
		if (pl && !pl->m_tempMode) {
			pl->Save();
			if (pl->pc) { free(pl->pc); pl->pc = NULL; }
			pl->playcnt = 0;
			pl->pnt = -1;
			pl->pnt1 = -1;
			pl->pc = (playlistdata0*)malloc(sizeof(playlistdata0));
			if (pl->m_lc.GetSafeHwnd())
				pl->m_lc.SetItemCount(0);
			pl->m_tempMode = 1;
		}
		m_queueN = 0;
		if (m_grpPl.GetSafeHwnd())
			m_grpPl.SetWindowText(LL14(L"一時プレイリスト", L"Temporary playlist", L"Liste temporaire", L"Playlist temporanea", L"Lista temporal", L"임시 재생목록", L"临时播放列表", L"قائمة مؤقتة", L"Временный плейлист", L"Temporaere Playlist", L"Lista temporaria", L"Tijdelijke playlist", L"Lista tymczasowa", L"Gecici liste"));
		if (m_plsel.GetSafeHwnd()) m_plsel.EnableWindow(FALSE);
		if (m_plrename.GetSafeHwnd()) m_plrename.EnableWindow(FALSE);
		if (m_pldelete.GetSafeHwnd()) m_pldelete.EnableWindow(FALSE);
	}
	if (m_libToggle.GetSafeHwnd())
		m_libToggle.SetWindowText(savedata.mpLibOpen ? L"≪" : L"Lib");
	if (m_histToggle.GetSafeHwnd())
		m_histToggle.SetWindowText(savedata.mpHistOpen ? L"≪" : L"Hist");
	if (m_tempToggle.GetSafeHwnd()) {
		m_tempToggle.SetWindowText(L"Temp");
		m_tempToggle.SetCheck(savedata.mpTempOpen ? BST_CHECKED : BST_UNCHECKED);
		m_tempToggle.EnsureAnimTimer();
		m_tempToggle.RepaintClient();
	}
	RefreshList(TRUE);
	DoLayout();
	CCC_GroupBoxesBack(GetSafeHwnd());
	RefreshListAfterLayout();
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
	MpPersistSavedataQuick();
}

void CMediaPlayerDlg::OnTempClear()
{
	if (!pl || !pl->m_tempMode) return;
	if (pl->playcnt <= 0) return;
	if (MessageBox(LL14(
		L"一時リストをすべてクリアしますか？", L"Clear the entire temporary list?", L"Vider toute la liste temporaire ?", L"Svuotare tutta la lista temporanea?", L"¿Vaciar toda la lista temporal?",
		L"임시 목록을 모두 비울까요?", L"清空整个临时列表？", L"مسح القائمة المؤقتة بالكامل؟", L"Очистить весь временный список?", L"Gesamte Templiste leeren?",
		L"Limpar toda a lista temporaria?", L"Hele tijdelijke lijst wissen?", L"Wyczyscic cala liste tymczasowa?", L"Tum gecici liste temizlensin mi?"),
		LL14(L"確認", L"Confirm", L"Confirmer", L"Conferma", L"Confirmar", L"확인", L"确认", L"تأكيد",
			L"Подтверждение", L"Bestätigen", L"Confirmar", L"Bevestigen", L"Potwierdzenie", L"Onay"),
		MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;
	if (pl->pc) { free(pl->pc); pl->pc = NULL; }
	pl->playcnt = 0;
	pl->pnt = -1;
	pl->pnt1 = -1;
	pl->pc = (playlistdata0*)malloc(sizeof(playlistdata0));
	if (pl->m_lc.GetSafeHwnd())
		pl->m_lc.SetItemCount(0);
	m_queueN = 0;
	RefreshList(TRUE);
	UpdateEmptyStateUi();
}

void CMediaPlayerDlg::HistRebuildList()
{
	EnsureLibControls();
	if (!m_histList.GetSafeHwnd()) return;
	m_histList.DeleteAllItems();

	SYSTEMTIME now; ::GetLocalTime(&now);
	auto dayKeyOf = [](const SYSTEMTIME& st) -> CString {
		CString k; k.Format(_T("%04u-%02u-%02u"), st.wYear, st.wMonth, st.wDay);
		return k;
	};
	const CString todayKey = dayKeyOf(now);
	SYSTEMTIME yest = now;
	{
		FILETIME ft; ::SystemTimeToFileTime(&now, &ft);
		ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
		u.QuadPart -= 24ULL * 60ULL * 60ULL * 10000000ULL;
		ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
		::FileTimeToSystemTime(&ft, &yest);
	}
	const CString yestKey = dayKeyOf(yest);

	CString lastKey;
	const int n = MpHist_Count();
	for (int i = 0; i < n; ++i) {
		MpHistEntry e;
		if (!MpHist_Get(i, e)) continue;
		SYSTEMTIME st; ::FileTimeToSystemTime(&e.ft, &st);
		const CString key = dayKeyOf(st);
		if (key != lastKey) {
			lastKey = key;
			CString hdr;
			if (key == todayKey)
				hdr = LL14(L"— 今日 —", L"— Today —", L"— Aujourd'hui —", L"— Oggi —", L"— Hoy —", L"— 오늘 —", L"— 今天 —", L"— اليوم —", L"— Сегодня —", L"— Heute —", L"— Hoje —", L"— Vandaag —", L"— Dzisiaj —", L"— Bugun —");
			else if (key == yestKey)
				hdr = LL14(L"— 昨日 —", L"— Yesterday —", L"— Hier —", L"— Ieri —", L"— Ayer —", L"— 어제 —", L"— 昨天 —", L"— أمس —", L"— Вчера —", L"— Gestern —", L"— Ontem —", L"— Gisteren —", L"— Wczoraj —", L"— Dun —");
			else
				hdr.Format(_T("— %02u/%02u —"), st.wMonth, st.wDay);
			const int row = m_histList.InsertItem(m_histList.GetItemCount(), hdr);
			m_histList.SetItemData(row, (DWORD_PTR)-2);
		}
		CString title = e.name;
		if (title.IsEmpty()) title = e.path;
		CString line;
		line.Format(_T("%02d:%02d  %s"), st.wHour, st.wMinute, (LPCTSTR)title);
		const int row = m_histList.InsertItem(m_histList.GetItemCount(), line);
		m_histList.SetItemData(row, (DWORD_PTR)i);
	}
	LibFitNoHScroll(&m_histList);
	m_histList.Invalidate(FALSE);
	m_histBuilt = 1;
}

static int MpFindTrackInPlaylistDat(int plIdx, LPCTSTR wantPath)
{
	if (!wantPath || !wantPath[0]) return -1;
	const CString want = NormalizePlaylistPath(wantPath);
	if (want.IsEmpty()) return -1;
	TCHAR tmp[1024] = { 0 };
	_tgetcwd(tmp, 1000);
	DatArc_Chdir();
	CString fname;
	if (plIdx == 0) fname = _T("playlistu.dat");
	else fname.Format(_T("playlistu%d.dat"), plIdx);
	CFile f;
	if (!f.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
		_tchdir(tmp);
		return -1;
	}
	int cnt = 0;
	if (f.Read(&cnt, 4) != 4 || cnt < 0 || cnt > 100000) {
		f.Close(); _tchdir(tmp); return -1;
	}
	int skip = 0;
	for (int i = 0; i < 9; ++i) {
		if (f.Read(&skip, 4) != 4) { f.Close(); _tchdir(tmp); return -1; }
	}
	playlistdata pld;
	int found = -1;
	for (int i = 0; i < cnt; ++i) {
		if (f.Read(&pld, sizeof(pld)) != sizeof(pld)) break;
		if (NormalizePlaylistPath(pld.fol).CompareNoCase(want) == 0) {
			found = i;
			break;
		}
	}
	f.Close();
	_tchdir(tmp);
	return found;
}

void CMediaPlayerDlg::HistPlayIndex(int histIdx)
{
	if (histIdx < 0 || histIdx >= MpHist_Count()) return;
	MpHistEntry he;
	if (!MpHist_Get(histIdx, he)) return;
	const CString path = he.path;
	if (path.IsEmpty() || !pl) return;

	// 1) current playlist
	int idx = pl->FindByPath(path);
	if (idx >= 0) {
		MP_PlayIndex(idx);
		return;
	}

	// 2) scan all playlist files; switch + play when found
	const int plCnt = pl->GetPlaylistFileCount();
	for (int pi = 0; pi < plCnt; ++pi) {
		if (pi == savedata.playlistnum) continue;
		const int ti = MpFindTrackInPlaylistDat(pi, path);
		if (ti < 0) continue;
		if (::IsWindow(m_plsel.GetSafeHwnd()))
			m_plsel.SetCurSel(pi);
		OnPlSel();
		idx = pl->FindByPath(path);
		if (idx >= 0) {
			MP_PlayIndex(idx);
			return;
		}
		break;
	}

	// 3) not in any PL -> append to current PL end and play
	CString leaf = path;
	const int slash = max(leaf.ReverseFind(_T('\\')), leaf.ReverseFind(_T('/')));
	if (slash >= 0 && slash + 1 < leaf.GetLength())
		leaf = leaf.Mid(slash + 1);
	CString disp = he.name;
	if (disp.IsEmpty()) disp = leaf;
	pl->Fol(path);
	idx = pl->FindByPath(path);
	if (idx < 0) {
		pl->Add(disp, 0, 0, 0, _T(""), _T(""), path, 0, 0);
		idx = pl->FindByPath(path);
		if (idx < 0 && pl->playcnt > 0)
			idx = pl->playcnt - 1;
	}
	RefreshList(TRUE);
	UpdateEmptyStateUi();
	if (idx >= 0)
		MP_PlayIndex(idx);
}

void CMediaPlayerDlg::OnHistDblClk(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMITEMACTIVATE p = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	if (!p || p->iItem < 0) return;
	const int histIdx = (int)m_histList.GetItemData(p->iItem);
	if (histIdx < 0) return; // day header
	HistPlayIndex(histIdx);
}

void CMediaPlayerDlg::OnEmptyAddFolder()
{
	OnAddFolder();
	UpdateEmptyStateUi();
}

void CMediaPlayerDlg::OnEmptyM3u()
{
	OnM3uImport();
	UpdateEmptyStateUi();
}

void CMediaPlayerDlg::UpdateEmptyStateUi()
{
	const BOOL emptyPl = (pl == NULL || pl->pc == NULL || pl->playcnt <= 0);
	if (m_emptyFolder.GetSafeHwnd())
		m_emptyFolder.ShowWindow(emptyPl ? SW_SHOW : SW_HIDE);
	if (m_emptyM3u.GetSafeHwnd())
		m_emptyM3u.ShowWindow(emptyPl ? SW_SHOW : SW_HIDE);
}

void CMediaPlayerDlg::OnRButtonUp(UINT nFlags, CPoint point)
{
	// ツールボタン上は PreTranslate でも処理。ここはフォールバック。
	auto hitBtn = [&](CWnd& btn, void (CMediaPlayerDlg::*fn)(CPoint)) -> BOOL {
		if (!btn.GetSafeHwnd()) return FALSE;
		CRect r; btn.GetWindowRect(&r); ScreenToClient(&r);
		if (!r.PtInRect(point)) return FALSE;
		CPoint sp = point; ClientToScreen(&sp);
		(this->*fn)(sp);
		return TRUE;
	};
	if (hitBtn(m_toolsToggle, &CMediaPlayerDlg::ShowToolsExtrasMenu)) return;
	if (hitBtn(m_lrcExpand, &CMediaPlayerDlg::ShowLyricsExtrasMenu)) return;
	if (hitBtn(m_deskLrc, &CMediaPlayerDlg::ShowLyricsExtrasMenu)) return;
	if (hitBtn(m_settings, &CMediaPlayerDlg::ShowSettingsExtrasMenu)) return;
	if (hitBtn(m_folder, &CMediaPlayerDlg::ShowFolderExtrasMenu)) return;
	if (hitBtn(m_eq, &CMediaPlayerDlg::ShowEqButtonExtrasMenu)) return;
	if (hitBtn(m_fadeout, &CMediaPlayerDlg::ShowFadeExtrasMenu)) return;
	if (hitBtn(m_renzoku, &CMediaPlayerDlg::ShowPlayModeExtrasMenu)) return;
	if (hitBtn(m_loop, &CMediaPlayerDlg::ShowPlayModeExtrasMenu)) return;
	if (hitBtn(m_random, &CMediaPlayerDlg::ShowPlayModeExtrasMenu)) return;
	if (hitBtn(m_botMirror, &CMediaPlayerDlg::ShowMirrorExtrasMenu)) return;
	if (m_botSleep.GetSafeHwnd()) {
		CRect sr; m_botSleep.GetWindowRect(&sr); ScreenToClient(&sr);
		if (sr.PtInRect(point)) { OnBotSleep(); return; }
	}
	const BOOL onBanner = m_bannerRect.PtInRect(point);
	const BOOL onJacket = (g_mpSideJacket && !m_jacketRect.IsRectEmpty() && m_jacketRect.PtInRect(point));
	BOOL onSeek = FALSE;
	if (m_seek.GetSafeHwnd()) {
		CRect sr; m_seek.GetWindowRect(&sr); ScreenToClient(&sr);
		onSeek = sr.PtInRect(point);
	}
	if (onSeek) {
		CCustomPopupMenu menu;
		menu.SetAeroMode(FALSE);
		AppendSeekExtrasToMenu(menu, MP_SEEK_MENU_BAR);
		CPoint sp = point;
		ClientToScreen(&sp);
		const UINT cmd = menu.Track(sp, this);
		if (cmd)
			PostMessage(WM_COMMAND, cmd);
		return;
	}
	if (onBanner || onJacket) {
		CCustomPopupMenu menu;
		menu.SetAeroMode(FALSE);
		if (onBanner) {
			enum { IDM_MP_BANNER_2D = 42340, IDM_MP_BANNER_3D = 42341 };
			CCustomPopupMenu* subView = menu.AddSubMenu(
				LL14(L"表示モード", L"View mode", L"Mode d'affichage", L"Modalita di visualizzazione", L"Modo de visualizacion", L"표시 모드", L"显示模式", L"وضع العرض", L"Режим отображения", L"Anzeigemodus", L"Modo de exibicao", L"Weergavemodus", L"Tryb wyswietlania", L"Goruntuleme modu"),
				LL14(L"バナー／ジャケット／曲情報の表示モード（通常2D／簡易3D）を選びます。", L"Choose banner/jacket/info view mode (normal 2D / soft 3D).", L"Choisir le mode banniere/pochette/infos (2D / 3D).", L"Scegli modalita banner/copertina/info (2D / 3D).", L"Elegir modo banner/caratula/info (2D / 3D).", L"배너/재킷/정보 표시 모드(일반 2D/간이 3D).", L"选择横幅/封面/信息显示模式（普通2D/简易3D）。", L"اختر وضع الشريط/الغلاف/المعلومات (2D / 3D).", L"Режим баннера/обложки/инфо (2D / 3D).", L"Banner-/Cover-/Info-Modus (2D / 3D).", L"Modo banner/capa/info (2D / 3D).", L"Banner-/hoes-/infomodus (2D / 3D).", L"Tryb banera/okladki/info (2D / 3D).", L"Banner/kapak/bilgi modu (2D / 3D)."));
			if (subView) {
				subView->AddCheck(IDM_MP_BANNER_2D,
					LL14(L"通常 (2D)", L"Normal (2D)", L"Normal (2D)", L"Normale (2D)", L"Normal (2D)", L"일반 (2D)", L"普通 (2D)", L"عادي (2D)", L"Обычный (2D)", L"Normal (2D)", L"Normal (2D)", L"Normaal (2D)", L"Zwykly (2D)", L"Normal (2D)"),
					!IsBannerSoft3D());
				subView->AddCheck(IDM_MP_BANNER_3D,
					LL14(L"簡易3D", L"Soft 3D", L"3D simplifie", L"3D semplificato", L"3D simple", L"간이 3D", L"简易3D", L"ثلاثي الأبعاد مبسط", L"Простой 3D", L"Einfaches 3D", L"3D simples", L"Eenvoudig 3D", L"Uproszczone 3D", L"Basit 3B"),
					IsBannerSoft3D());
				if (IsBannerSoft3D()) {
					int yaw10 = (int)(m_bannerCam3d.yawDeg * 10.f);
					int pit10 = (int)(m_bannerCam3d.pitchDeg * 10.f);
					int zoomPct = (int)(m_bannerCam3d.zoom * 100.f + 0.5f);
					if (yaw10 < -1800) yaw10 = -1800; if (yaw10 > 1800) yaw10 = 1800;
					if (pit10 < -850) pit10 = -850; if (pit10 > 850) pit10 = 850;
					if (zoomPct < 35) zoomPct = 35; if (zoomPct > 400) zoomPct = 400;
					subView->AddSeparator();
					subView->AddSlider(LL14(L"Yaw (0.1°)", L"Yaw (0.1°)", L"Lacet (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"偏航 (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)", L"Yaw (0.1°)"),
						-1800, 1800, yaw10, &CMediaPlayerDlg::BannerSoft3dYawCb, this,
						LL14(L"水平回転（ドラッグ中に反映）", L"Horizontal rotation (live)", L"Rotation horizontale (direct)", L"Rotazione orizzontale (live)", L"Rotacion horizontal (en vivo)", L"수평 회전(즉시)", L"水平旋转（即时）", L"دوران أفقي (مباشر)", L"Горизонтальный поворот (сразу)", L"Horizontale Drehung (live)", L"Rotacao horizontal (ao vivo)", L"Horizontale rotatie (live)", L"Obrot poziomy (na zywo)", L"Yatay donus (anlik)"));
					subView->AddSlider(LL14(L"Pitch (0.1°)", L"Pitch (0.1°)", L"Tangage (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"俯仰 (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)", L"Pitch (0.1°)"),
						-850, 850, pit10, &CMediaPlayerDlg::BannerSoft3dPitchCb, this,
						LL14(L"仰角（ドラッグ中に反映）", L"Elevation angle (live)", L"Angle d'elevation (direct)", L"Angolo di elevazione (live)", L"Angulo de elevacion (en vivo)", L"앙각(즉시)", L"仰角（即时）", L"زاوية الارتفاع (مباشر)", L"Угол наклона (сразу)", L"Neigungswinkel (live)", L"Angulo de elevacao (ao vivo)", L"Elevatiehoek (live)", L"Kat nachylenia (na zywo)", L"Yukselis acisi (anlik)"));
					subView->AddSlider(LL14(L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"缩放 (%)", L"تكبير (%)", L"Масштаб (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)", L"Zoom (%)"),
						35, 400, zoomPct, &CMediaPlayerDlg::BannerSoft3dZoomCb, this,
						LL14(L"拡大縮小（ドラッグ中に反映）", L"Zoom (live)", L"Zoom (direct)", L"Zoom (live)", L"Zoom (en vivo)", L"확대/축소(즉시)", L"缩放（即时）", L"تكبير (مباشر)", L"Масштаб (сразу)", L"Zoom (live)", L"Zoom (ao vivo)", L"Zoom (live)", L"Powiększenie (na zywo)", L"Yakinlastirma (anlik)"));
					subView->AddSeparator();
					subView->AddCommand(42342,
						LL14(L"視点をリセット", L"Reset view", L"Reinitialiser la vue", L"Reimposta vista", L"Restablecer vista",
							L"시점 재설정", L"重置视角", L"إعادة ضبط العرض", L"Сбросить вид", L"Ansicht zuruecksetzen",
							L"Redefinir vista", L"Weergave resetten", L"Resetuj widok", L"Gorunumu sifirla"),
						LL14(L"簡易3Dのカメラを既定（Yaw/Pitch/Zoom）に戻します。ショートカット 0。", L"Restore Soft 3D camera to defaults (Yaw/Pitch/Zoom). Shortcut 0.", L"Remettre la camera 3D par defaut. Raccourci 0.", L"Ripristina la camera Soft 3D ai valori predefiniti. Scorciatoia 0.", L"Restaurar la camara Soft 3D a valores por defecto. Atajo 0.", L"간이 3D 카메라를 기본값으로. 단축키 0.", L"将简易3D相机恢复为默认。快捷键 0。", L"إعادة كاميرا Soft 3D للافتراضي. اختصار 0.", L"Вернуть Soft 3D камеру к умолчанию. Клавиша 0.", L"Soft-3D-Kamera auf Standard. Taste 0.", L"Restaurar camera Soft 3D ao padrao. Atalho 0.", L"Soft 3D-camera terugzetten. Sneltoets 0.", L"Przywroc kamere Soft 3D do domyslnych. Skrot 0.", L"Soft 3D kamerayi varsayilana al. Kisayol 0."));
				}
			}
			menu.AddSeparator();
			menu.AddCheck(ID_MP_SPEANA_BAR,
				LL14(L"スペアナ: バー", L"Spectrum: Bars", L"Spectre: Barres", L"Spettro: Barre", L"Espectro: Barras", L"스펙트럼: 막대", L"频谱: 柱状", L"الطيف: أشرطة", L"Спектр: Столбцы", L"Spektrum: Balken", L"Espectro: Barras", L"Spectrum: Balken", L"Widmo: Slupki", L"Spektrum: Cubuk"),
				savedata.mpSpeanaStyle == 0,
				LL14(L"バナースペアナを棒グラフ表示にします", L"Show the banner spectrum as bars", L"Afficher le spectre en barres", L"Mostra lo spettro a barre", L"Mostrar el espectro en barras", L"배너 스펙트럼을 막대 표시로", L"将横幅频谱显示为柱状", L"عرض طيف الشريط كأعمدة", L"Показать спектр баннера столбцами", L"Banner-Spektrum als Balken anzeigen", L"Mostrar o espectro do banner em barras", L"Banner-spectrum als staven tonen", L"Pokaz widmo banera jako slupki", L"Banner spektrumunu cubuk olarak goster"));
			menu.AddCheck(ID_MP_SPEANA_MIRROR,
				LL14(L"スペアナ: ミラー", L"Spectrum: Mirror", L"Spectre: Miroir", L"Spettro: Specchio", L"Espectro: Espejo", L"스펙트럼: 미러", L"频谱: 镜像", L"الطيف: مرآة", L"Спектр: Зеркало", L"Spektrum: Spiegel", L"Espectro: Espelho", L"Spectrum: Spiegel", L"Widmo: Lustro", L"Spektrum: Ayna"),
				savedata.mpSpeanaStyle == 1,
				LL14(L"バナースペアナを左右ミラー表示にします", L"Show the banner spectrum as a mirror view", L"Afficher le spectre en miroir", L"Mostra lo spettro a specchio", L"Mostrar el espectro en espejo", L"배너 스펙트럼을 미러 표시로", L"将横幅频谱显示为镜像", L"عرض طيف الشريط كمرآة", L"Показать спектр баннера зеркально", L"Banner-Spektrum als Spiegel anzeigen", L"Mostrar o espectro do banner em espelho", L"Banner-spectrum als spiegel tonen", L"Pokaz widmo banera jako lustro", L"Banner spektrumunu ayna olarak goster"));
			menu.AddCheck(ID_MP_SPEANA_WAVE,
				LL14(L"スペアナ: 波形", L"Spectrum: Waveform", L"Spectre: Forme d'onde", L"Spettro: Forma d'onda", L"Espectro: Forma de onda", L"스펙트럼: 파형", L"频谱: 波形", L"الطيف: موجة", L"Спектр: Волна", L"Spektrum: Wellenform", L"Espectro: Forma de onda", L"Spectrum: Golfvorm", L"Widmo: Fala", L"Spektrum: Dalga"),
				savedata.mpSpeanaStyle == 2,
				LL14(L"バナースペアナを波形表示にします", L"Show the banner spectrum as a waveform", L"Afficher le spectre en forme d'onde", L"Mostra lo spettro come forma d'onda", L"Mostrar el espectro como forma de onda", L"배너 스펙트럼을 파형 표시로", L"将横幅频谱显示为波形", L"عرض طيف الشريط كموجة", L"Показать спектр баннера волной", L"Banner-Spektrum als Wellenform anzeigen", L"Mostrar o espectro do banner como forma de onda", L"Banner-spectrum als golfvorm tonen", L"Pokaz widmo banera jako fale", L"Banner spektrumunu dalga olarak goster"));
			menu.AddCheck(ID_MP_CORR_METER,
				LL14(L"位相相関メーター (φ/LR)", L"Correlation meter (φ/LR)", L"Corrélomètre (φ/LR)", L"Misuratore correlazione (φ/LR)", L"Medidor de correlación (φ/LR)", L"위상 상관 미터 (φ/LR)", L"相位相关表 (φ/LR)", L"مقياس الترابط (φ/LR)", L"Корреляция (φ/LR)", L"Korrelationsmesser (φ/LR)", L"Medidor de correlação (φ/LR)", L"Correlatiemeter (φ/LR)", L"Miernik korelacji (φ/LR)", L"Faz korelasyon (φ/LR)"),
				savedata.pro_corr_meter != 0,
				LL14(L"左右位相の相関メーターをバナーに表示します", L"Show a L/R phase correlation meter on the banner", L"Afficher le correlometre L/R sur la banniere", L"Mostra il correlometro L/R sul banner", L"Mostrar el medidor de correlacion L/R en el banner", L"배너에 L/R 위상 상관 미터 표시", L"在横幅显示左右相位相关表", L"عرض مقياس ترابط الطور L/R على الشريط", L"Показать коррелометр L/R на баннере", L"L/R-Korrelationsmesser auf dem Banner zeigen", L"Mostrar o medidor de correlacao L/R no banner", L"L/R-correlatiemeter op de banner tonen", L"Pokaz miernik korelacji L/R na banerze", L"Bannerda L/R faz korelasyon olcerini goster"));
			menu.AddSeparator();
			menu.AddCheck(ID_MP_OPEN_ANALYZER,
				LL14(L"アナライザー...", L"Analyzer...", L"Analyseur...", L"Analizzatore...", L"Analizador...", L"애널라이저...", L"分析器...", L"المحلل...", L"Анализатор...", L"Analyzer...", L"Analisador...", L"Analyser...", L"Analizator...", L"Analizor..."),
				savedata.analyzerwindow != 0,
				LL14(L"周波数アナライザーウィンドウを開閉します", L"Open or close the frequency analyzer window", L"Ouvrir/fermer la fenetre analyseur", L"Apri/chiudi la finestra analizzatore", L"Abrir/cerrar la ventana del analizador", L"주파수 애널라이저 창을 여닫기", L"打开或关闭频率分析器窗口", L"فتح/إغلاق نافذة المحلل", L"Открыть/закрыть окно анализатора", L"Frequenz-Analyzer-Fenster oeffnen/schliessen", L"Abrir/fechar a janela do analisador", L"Frequentie-analyservenster openen/sluiten", L"Otworz/zamknij okno analizatora", L"Frekans analizor penceresini ac/kapat"));
			menu.AddCheck(ID_MP_OPEN_PIANOROLL,
				LL14(L"ピアノロール...", L"Piano roll...", L"Piano roll...", L"Piano roll...", L"Piano roll...", L"피아노 롤...", L"钢琴卷帘...", L"لفة البيانو...", L"Пианоролл...", L"Piano Roll...", L"Piano roll...", L"Piano roll...", L"Piano roll...", L"Piano roll..."),
				savedata.pianorollwindow != 0,
				LL14(L"ピアノロールウィンドウを開閉します", L"Open or close the piano-roll window", L"Ouvrir/fermer la fenetre piano roll", L"Apri/chiudi la finestra piano roll", L"Abrir/cerrar la ventana piano roll", L"피아노 롤 창을 여닫기", L"打开或关闭钢琴卷帘窗口", L"فتح/إغلاق نافذة لفة البيانو", L"Открыть/закрыть окно пианоролла", L"Piano-Roll-Fenster oeffnen/schliessen", L"Abrir/fechar a janela do piano roll", L"Piano-rollvenster openen/sluiten", L"Otworz/zamknij okno piano roll", L"Piano roll penceresini ac/kapat"));
			menu.AddCheck(ID_MP_OPEN_EQ,
				LL14(L"イコライザー...", L"Equalizer...", L"Egaliseur...", L"Equalizzatore...", L"Ecualizador...",
					L"이퀄라이저...", L"均衡器...", L"المعادل...", L"Эквалайзер...", L"Equalizer...",
					L"Equalizador...", L"Equalizer...", L"Equalizer...", L"Equalizer..."),
				savedata.eqwindow != 0,
				LL14(L"イコライザーウィンドウを開閉します", L"Open or close the equalizer window", L"Ouvrir/fermer la fenetre egaliseur", L"Apri/chiudi la finestra equalizzatore", L"Abrir/cerrar la ventana del ecualizador", L"이퀄라이저 창을 여닫기", L"打开或关闭均衡器窗口", L"فتح/إغلاق نافذة المعادل", L"Открыть/закрыть окно эквалайзера", L"Equalizer-Fenster oeffnen/schliessen", L"Abrir/fechar a janela do equalizador", L"Equalizervenster openen/sluiten", L"Otworz/zamknij okno equalizera", L"Equalizer penceresini ac/kapat"));
			{
				extern CProToolsDlg* g_proToolsDlg;
				const BOOL ptOpen = (g_proToolsDlg && ::IsWindow(g_proToolsDlg->GetSafeHwnd())) ? TRUE : FALSE;
				menu.AddCheck(ID_MP_OPEN_PROTOOLS,
					LL14(L"再生詳細...", L"Playback details...", L"Details lecture...", L"Dettagli riproduzione...", L"Detalles de reproduccion...",
						L"재생 상세...", L"播放详情...", L"تفاصيل التشغيل...", L"Подробности воспроизведения...", L"Wiedergabedetails...",
						L"Detalhes de reproducao...", L"Afspeeldetails...", L"Szczegoly odtwarzania...", L"Calma ayrintilari..."),
					ptOpen,
					LL14(L"再生詳細（Pro Tools 風）パネルを開閉します", L"Open or close the playback-details panel", L"Ouvrir/fermer le panneau details de lecture", L"Apri/chiudi il pannello dettagli riproduzione", L"Abrir/cerrar el panel de detalles de reproduccion", L"재생 상세 패널을 여닫기", L"打开或关闭播放详情面板", L"فتح/إغلاق لوحة تفاصيل التشغيل", L"Открыть/закрыть панель подробностей", L"Wiedergabedetails-Panel oeffnen/schliessen", L"Abrir/fechar o painel de detalhes", L"Afspeeldetailsvenster openen/sluiten", L"Otworz/zamknij panel szczegolow", L"Calma ayrintilari panelini ac/kapat"));
			}
			menu.AddSeparator();
			menu.AddCheck(ID_MP_LRC_EXPAND,
				LL14(L"歌詞パネル拡大", L"Expand lyrics panel", L"Agrandir paroles", L"Espandi testi", L"Expandir letra",
					L"가사 패널 확대", L"扩大歌词面板", L"توسيع لوحة الكلمات", L"Развернуть панель текста", L"Textpanel vergroessern",
					L"Expandir painel de letra", L"Songtekstpaneel vergroten", L"Rozszerz panel tekstu", L"Soz panelini genislet"),
				savedata.mpLrcExpand != 0,
				LL14(L"プレイヤー内の歌詞をカラオケ風に拡大表示します", L"Expand in-player lyrics to karaoke-style view", L"Agrandir les paroles en style karaoke", L"Espandi i testi in stile karaoke", L"Expandir la letra al estilo karaoke", L"플레이어 내 가사를 가라오케풍으로 확대", L"将播放器内歌词扩大为卡拉OK样式", L"توسيع الكلمات داخل المشغّل بأسلوب كاريوكي", L"Развернуть текст в плеере в стиле караоке", L"Liedtext im Player karaokeartig vergroessern", L"Expandir a letra no player em estilo karaoke", L"Songtekst in speler karaoke-achtig vergroten", L"Rozszerz tekst w odtwarzaczu jak karaoke", L"Oynaticida sozleri karaoke gibi genislet"));
			menu.AddCheck(ID_MP_DESK_LRC,
				LL14(L"歌詞ウィンドウを表示", L"Show lyrics window", L"Afficher fenetre paroles", L"Mostra finestra testi", L"Mostrar ventana de letra",
					L"가사 창 표시", L"显示歌词窗口", L"عرض نافذة الكلمات", L"Показать окно текста", L"Textfenster anzeigen",
					L"Mostrar janela de letra", L"Songtekstvenster tonen", L"Pokaz okno tekstu", L"Soz penceresini goster"),
				IsDesktopLyricsOpen(),
				LL14(L"常時最前面の歌詞ウィンドウを開閉します（不透明度などは右クリック）", L"Toggle the always-on-top lyrics window (RMB for opacity etc.)", L"Basculer la fenetre de paroles au premier plan (clic droit pour opacite)", L"Attiva/disattiva la finestra testi in primo piano (tasto destro per opacita)", L"Alternar la ventana de letra siempre visible (clic der. para opacidad)", L"항상 위 가사 창을 여닫기(불투명도 등은 우클릭)", L"打开/关闭置顶歌词窗口（右键调不透明度等）", L"فتح/إغلاق نافذة كلمات أمامية (زر يمين للعتامة)", L"Открыть/закрыть окно текста поверх всех (ПКМ — непрозрачность)", L"Textfenster im Vordergrund ein/aus (RMB fur Deckkraft)", L"Abrir/fechar janela de letra no topo (botao dir. para opacidade)", L"Songtekstvenster bovenop aan/uit (RMB voor dekking)", L"Otworz/zamknij okno tekstu na wierzchu (PPM: nieprzezroczystosc)", L"Her zaman ustte soz penceresini ac/kapat (opaklik icin sag tik)"));
			menu.AddSeparator();
		}
		menu.AddCommand(ID_MP_REFRESH_JACKET,
			LL14(L"再生中のジャケを再取得", L"Refresh playing jacket", L"Rafraichir la pochette en lecture", L"Aggiorna copertina in riproduzione",
				L"Actualizar caratula en reproduccion", L"재생 중 재킷 다시 가져오기", L"重新获取正在播放的封面", L"تحديث غلاف التشغيل",
				L"Обновить обложку текущего", L"Aktuelles Cover neu laden", L"Atualizar capa em reproducao",
				L"Huidige hoes vernieuwen", L"Odswiez okladke odtwarzanego", L"Oynatilan kapagi yenile"),
				LL14(L"再生中の曲のジャケット画像を再取得します", L"Re-fetch the jacket art for the playing track", L"Retrouver la pochette du titre en lecture", L"Ricarica la copertina del brano in riproduzione", L"Volver a obtener la caratula de la pista actual", L"재생 중 곡의 재킷을 다시 가져오기", L"重新获取正在播放曲目的封面", L"إعادة جلب غلاف المقطع الحالي", L"Заново получить обложку текущего трека", L"Cover des aktuellen Titels neu laden", L"Buscar de novo a capa da faixa atual", L"Hoes van het huidige nummer opnieuw ophalen", L"Pobierz ponownie okladke biezacego utworu", L"Calan parcanin kapagini yeniden al"));
		if (onJacket) {
			menu.AddCheck(ID_MP_JACKET_REM_OVERLAY,
				LL14(L"残時間リングとタイム", L"Remaining ring and time", L"Anneau restant et temps", L"Anello rest. e tempo",
					L"Anillo restante y tiempo", L"남은 시간 링과 타임", L"剩余时间环与时间", L"حلقة الوقت المتبقي والزمن",
					L"Кольцо остатка и время", L"Restzeit-Ring und Zeit", L"Anel restante e tempo",
					L"Resttijd-ring en tijd", L"Piermien pozostaly i czas", L"Kalan sure halkasi ve zaman"),
				savedata.mpJacketRemOverlay != 0,
				LL14(L"ジャケット上に残時間リングとタイムを重ねます", L"Overlay remaining-time ring and clock on the jacket", L"Superposer anneau de temps restant et horloge sur la pochette", L"Sovrapponi anello tempo restante e orologio sulla copertina", L"Superponer anillo de tiempo restante y reloj en la caratula", L"재킷 위에 남은 시간 링과 타임 표시", L"在封面上叠加剩余时间环与时间", L"عرض حلقة الوقت المتبقي والساعة على الغلاف", L"Наложить кольцо остатка и время на обложку", L"Restzeit-Ring und Uhr auf dem Cover anzeigen", L"Sobrepor anel de tempo restante e relogio na capa", L"Resttijd-ring en tijd op de hoes tonen", L"Nałóż pierścień pozostałego czasu i zegar na okladke", L"Kapak uzerine kalan sure halkasi ve zamani goster"));
			menu.AddCommand(ID_MP_JACKET_RELOAD,
				LL14(L"代替ジャケを再読込", L"Reload jacket alternatives", L"Recharger alternatives", L"Ricarica alternative",
					L"Recargar alternativas", L"대체 재킷 다시 읽기", L"重新加载备选封面", L"إعادة تحميل البدائل",
					L"Перечитать альтернативы", L"Alternativen neu laden", L"Recarregar alternativas",
					L"Alternatieven herladen", L"Przeladuj alternatywy", L"Alternatif kapaklari yenile"),
					LL14(L"代替ジャケット候補をフォルダから再読込します", L"Reload alternate jacket candidates from the folder", L"Recharger les pochettes alternatives du dossier", L"Ricarica le copertine alternative dalla cartella", L"Recargar caratulas alternativas de la carpeta", L"폴더에서 대체 재킷 후보를 다시 읽기", L"从文件夹重新加载备选封面", L"إعادة تحميل أغلفة بديلة من المجلد", L"Перечитать альтернативные обложки из папки", L"Alternative Cover aus dem Ordner neu laden", L"Recarregar capas alternativas da pasta", L"Alternatieve hoezen uit de map herladen", L"Przeladuj alternatywne okladki z folderu", L"Klasorden alternatif kapaklari yeniden yukle"));
			menu.AddCommand(ID_MP_JACKET_COVERJPG,
				LL14(L"フォルダの cover.jpg を選ぶ", L"Pick cover.jpg in folder", L"Choisir cover.jpg", L"Scegli cover.jpg",
					L"Elegir cover.jpg", L"폴더의 cover.jpg 선택", L"选择文件夹 cover.jpg", L"اختيار cover.jpg",
					L"Выбрать cover.jpg", L"cover.jpg waehlen", L"Escolher cover.jpg",
					L"Kies cover.jpg", L"Wybierz cover.jpg", L"cover.jpg sec"),
					LL14(L"曲フォルダ内の cover.jpg をジャケットに選びます", L"Use cover.jpg from the track folder as jacket", L"Utiliser cover.jpg du dossier comme pochette", L"Usa cover.jpg della cartella come copertina", L"Usar cover.jpg de la carpeta como caratula", L"곡 폴더의 cover.jpg를 재킷으로 선택", L"使用曲目文件夹中的 cover.jpg 作为封面", L"استخدام cover.jpg من مجلد المقطع كغلاف", L"Взять cover.jpg из папки трека как обложку", L"cover.jpg aus dem Ordner als Cover wahlen", L"Usar cover.jpg da pasta como capa", L"cover.jpg uit de map als hoes gebruiken", L"Uzyj cover.jpg z folderu jako okladki", L"Klasordeki cover.jpg'yi kapak olarak sec"));
			menu.AddCommand(ID_MP_JACKET_SAVE_COVER,
				LL14(L"画像を cover.jpg として保存…", L"Save image as cover.jpg…", L"Enregistrer en cover.jpg…", L"Salva come cover.jpg…",
					L"Guardar como cover.jpg…", L"이미지를 cover.jpg로 저장…", L"另存为 cover.jpg…", L"حفظ كـ cover.jpg…",
					L"Сохранить как cover.jpg…", L"Als cover.jpg speichern…", L"Salvar como cover.jpg…",
					L"Opslaan als cover.jpg…", L"Zapisz jako cover.jpg…", L"cover.jpg olarak kaydet…"),
					LL14(L"表示中の画像を cover.jpg としてフォルダに保存します", L"Save the displayed image as cover.jpg in the folder", L"Enregistrer l'image affichee comme cover.jpg", L"Salva l'immagine visualizzata come cover.jpg", L"Guardar la imagen mostrada como cover.jpg", L"표시 중인 이미지를 cover.jpg로 저장", L"将当前显示的图像另存为 cover.jpg", L"حفظ الصورة المعروضة كـ cover.jpg", L"Сохранить отображаемое изображение как cover.jpg", L"Angezeigtes Bild als cover.jpg speichern", L"Salvar a imagem exibida como cover.jpg", L"Getoonde afbeelding opslaan als cover.jpg", L"Zapisz wyswietlany obraz jako cover.jpg", L"Gorunen gorseli cover.jpg olarak kaydet"));
		}
		menu.AddSeparator();
		menu.AddCheck(ID_MP_SEEK_WAVE,
			LL14(L"波形オーバービュー", L"Waveform overview", L"Apercu forme d'onde", L"Panoramica forma d'onda",
				L"Vista de forma de onda", L"파형 오버뷰", L"波形概览", L"نظرة الموجة",
				L"Обзор волны", L"Wellenform-Uberblick", L"Visao da forma de onda",
				L"Golfvorm-overzicht", L"Podglad fali", L"Dalga formu onizleme"),
			savedata.mpSeekWave != 0,
			LL14(L"シークバーに曲全体の波形オーバービューを表示します", L"Show a full-track waveform overview on the seek bar", L"Afficher un apercu d'onde sur la barre", L"Mostra panoramica forma d'onda sulla barra", L"Mostrar vista de forma de onda en la barra", L"시크바에 전체 파형 오버뷰를 표시", L"在进度条显示整曲波形概览", L"عرض نظرة موجة على شريط التقديم", L"Показать обзор волны на полосе", L"Wellenform-Uberblick auf der Suchleiste", L"Mostrar visao da forma de onda na barra", L"Golfvorm-overzicht op de zoekbalk", L"Pokaz podglad fali na pasku", L"Seek cubugunda dalga formu onizleme"));
		menu.AddCommand(ID_MP_SEEK_CUEADD,
			LL14(L"キューを現在位置に追加", L"Add cue at now", L"Ajouter cue ici", L"Aggiungi cue qui",
				L"Anadir cue aqui", L"현재 위치에 큐 추가", L"在当前位置添加标记", L"إضافة إشارة هنا",
				L"Добавить метку здесь", L"Cue hier hinzufugen", L"Adicionar cue aqui",
				L"Cue hier toevoegen", L"Dodaj cue tutaj", L"Buraya cue ekle"),
				LL14(L"現在位置にキュー（ジャンプ用マーカー）を追加します", L"Add a cue marker at the current position", L"Ajouter un marqueur cue a la position actuelle", L"Aggiungi un cue alla posizione attuale", L"Anadir un cue en la posición actual", L"현재 위치에 큐 마커 추가", L"在当前位置添加标记", L"إضافة إشارة عند الموضع الحالي", L"Добавить метку в текущей позиции", L"Cue an aktueller Position hinzufugen", L"Adicionar um cue na posicao atual", L"Cue op huidige positie toevoegen", L"Dodaj cue w biezacej pozycji", L"Su anki konuma cue ekle"));
		menu.AddCommand(ID_MP_PHRASE_AB,
			LL14(L"フレーズA-B [R]", L"Phrase A-B [R]", L"Phrase A-B [R]", L"Frase A-B [R]", L"Frase A-B [R]",
				L"프레이즈 A-B [R]", L"乐句A-B [R]", L"عبارة A-B [R]", L"Фраза A-B [R]", L"Phrase A-B [R]",
				L"Frase A-B [R]", L"Frase A-B [R]", L"Fraza A-B [R]", L"Cumle A-B [R]"),
				LL14(L"現在位置を中心にフレーズ幅で A-B を設定します（ショートカット R）", L"Set A-B around now by phrase width (shortcut R)", L"Definir A-B autour de maintenant selon la phrase (R)", L"Imposta A-B attorno ad ora per larghezza frase (R)", L"Fijar A-B alrededor de ahora segun frase (R)", L"현재 위치 기준으로 프레이즈 폭 A-B 설정(단축키 R)", L"以当前位置为中心按乐句宽度设 A-B（快捷键 R）", L"تعيين A-B حول الموضع بعرض العبارة (اختصار R)", L"Задать A-B вокруг текущей позиции по ширине фразы (R)", L"A-B um Jetzt mit Phrasenbreite setzen (Kurzbefehl R)", L"Definir A-B em torno de agora pela largura da frase (R)", L"A-B rond nu zetten op frasebreedte (sneltoets R)", L"Ustaw A-B wokol teraz wg szerokosci frazy (R)", L"Su anin etrafinda cumle genisligiyle A-B ayarla (R)"));
		menu.AddSeparator();
		menu.AddCommand(ID_HELP_SHOWSHEET,
			LL14(L"操作ガイド", L"Operation guide", L"Guide d'utilisation", L"Guida operativa",
				L"Guía de operación", L"조작 가이드", L"操作指南", L"دليل التشغيل",
				L"Руководство", L"Bedienungsanleitung", L"Guia de operação", L"Handleiding",
				L"Przewodnik", L"İşlem kılavuzu"),
				LL14(L"この画面の操作ガイド（ヘルプシート）を表示します", L"Show the operation guide (help sheet) for this view", L"Afficher le guide d'utilisation de cette vue", L"Mostra la guida operativa di questa vista", L"Mostrar la guía de operación de esta vista", L"이 화면의 조작 가이드를 표시", L"显示此界面的操作指南", L"عرض دليل التشغيل لهذه الشاشة", L"Показать руководство по этой панели", L"Bedienungsanleitung fur diese Ansicht zeigen", L"Mostrar o guia de operacao desta tela", L"Handleiding voor dit scherm tonen", L"Pokaz przewodnik po tym widoku", L"Bu ekranin islem kilavuzunu goster"));
		menu.AddCommand(42343,
			LL14(L"コマンドパレット… Ctrl+K", L"Command palette… Ctrl+K", L"Palette de commandes… Ctrl+K", L"Palette comandi… Ctrl+K",
				L"Paleta de comandos… Ctrl+K", L"명령 팔레트… Ctrl+K", L"命令面板… Ctrl+K", L"لوحة الأوامر… Ctrl+K",
				L"Палитра команд… Ctrl+K", L"Befehlspalette… Ctrl+K", L"Paleta de comandos… Ctrl+K", L"Opdrachtpalet… Ctrl+K",
				L"Paleta polecen… Ctrl+K", L"Komut paleti… Ctrl+K"),
				LL14(L"入力して絞り込み、Enter で実行できるコマンド一覧を開きます（Ctrl+K）", L"Open a searchable command list; Enter runs the selection (Ctrl+K)", L"Ouvrir la liste de commandes filtrable ; Entree execute (Ctrl+K)", L"Apri l'elenco comandi filtrabile; Invio esegue (Ctrl+K)", L"Abrir la lista de comandos filtrable; Enter ejecuta (Ctrl+K)", L"입력으로 좁히고 Enter 로 실행하는 명령 목록을 엽니다(Ctrl+K)", L"打开可筛选的命令列表，Enter 执行（Ctrl+K）", L"فتح قائمة أوامر قابلة للتصفية وينفذ Enter التحديد (Ctrl+K)", L"Открыть список команд с фильтром; Enter выполняет (Ctrl+K)", L"Durchsuchbare Befehlsliste oeffnen; Enter fuehrt aus (Ctrl+K)", L"Abrir a lista de comandos filtravel; Enter executa (Ctrl+K)", L"Zoekbare opdrachtenlijst openen; Enter voert uit (Ctrl+K)", L"Otworz filtrowana liste polecen; Enter uruchamia (Ctrl+K)", L"Filtrelenebilir komut listesini ac; Enter calistirir (Ctrl+K)"));
		CPoint sp = point;
		ClientToScreen(&sp);
		const UINT cmd = menu.Track(sp, this);
		if (cmd == 42340) {
			savedata.mpBannerviewmode = 0;
			Invalidate(FALSE);
		}
		else if (cmd == 42341) {
			savedata.mpBannerviewmode = 1;
			SyncBannerSoft3DCamFromSave();
			if ((savedata.soft3dTourSeen & 1) == 0) {
				savedata.soft3dTourSeen |= 1;
				m_soft3dTourUntil = ::GetTickCount() + 3000;
			}
			Invalidate(FALSE);
		}
		else if (cmd == 42342) {
			savedata.mpBanner3dyaw = -220;
			savedata.mpBanner3dpitch = 260;
			savedata.mpBanner3dzoom = 100;
			SyncBannerSoft3DCamFromSave();
			Invalidate(FALSE);
		}
		else if (cmd == 42343) {
			OpenCommandPalette();
		}
		else if (cmd)
			PostMessage(WM_COMMAND, cmd);
		return;
	}
	// キャプション帯(アイコン含む)のシステムメニューは Base 側
	CCustomBlurDialogExBase::OnRButtonUp(nFlags, point);
}

void CMediaPlayerDlg::OnSpeanaStyleBar()
{
	savedata.mpSpeanaStyle = 0;
	MpPersistSavedataQuick();
	InvalidateRect(&m_bannerRect, FALSE);
}

void CMediaPlayerDlg::OnSpeanaStyleMirror()
{
	savedata.mpSpeanaStyle = 1;
	MpPersistSavedataQuick();
	InvalidateRect(&m_bannerRect, FALSE);
}

void CMediaPlayerDlg::OnSpeanaStyleWave()
{
	savedata.mpSpeanaStyle = 2;
	MpPersistSavedataQuick();
	InvalidateRect(&m_bannerRect, FALSE);
}

void CMediaPlayerDlg::OnCorrMeterToggle()
{
	savedata.pro_corr_meter = savedata.pro_corr_meter ? 0 : 1;
	MpPersistSavedataQuick();
	InvalidateRect(&m_bannerRect, FALSE);
	if (og && og->m_AnalyzerDlg && ::IsWindow(og->m_AnalyzerDlg->GetSafeHwnd()))
		og->m_AnalyzerDlg->Invalidate(FALSE);
}

void CMediaPlayerDlg::OnRefreshJacket()
{
	extern CString filen;
	if (!og || filen.IsEmpty()) return;
	PlJakDiskForget(filen);
	for (int j = 0; j < kMpJakN; ++j) {
		if (m_jakKey[j][0] && _tcsicmp(m_jakKey[j], filen) == 0) {
			if (m_jakBmp[j]) { ::DeleteObject(m_jakBmp[j]); m_jakBmp[j] = NULL; }
			m_jakKey[j][0] = 0;
			m_jakTick[j] = 0;
			m_jakRow[j] = -1;
		}
	}
	og->LoadJacket(filen);
	if (!m_jacketRect.IsRectEmpty())
		InvalidateRect(&m_jacketRect, FALSE);
	InvalidateRect(&m_bannerRect, FALSE);
	m_list.Invalidate(FALSE);
}

void CMediaPlayerDlg::DrawJacketHeroOverlay(CDC& mem, int w, int h)
{
	if (!savedata.mpJacketRemOverlay) return;
	if (!plf || !og) return;
	const int mn = m_seek.GetMinValue();
	const int mx = m_seek.GetMaxValue();
	if (mx <= mn) return;
	const int pos = m_seek.GetPos();
	const double rem = (double)(mx - pos) / (double)(mx - mn);
	int hz = (wavbit_sample_Hz > 0) ? wavbit_sample_Hz : 44100;
	if (mode == -10)
		hz = max(1, hz / 100);
	else if (mode == -2 || videoonly)
		hz = 100;
	int remSec = (mx - pos) / hz;
	if (remSec < 0) remSec = 0;

	const int pad = max(4, w / 24);
	const int rw = max(18, min(w, h) / 4);
	const int cx = w - pad - rw / 2;
	const int cy = pad + rw / 2;
	CRect rr(cx - rw / 2, cy - rw / 2, cx + rw / 2, cy + rw / 2);
	CPen penBg(PS_SOLID, max(2, rw / 10), RGB(60, 60, 70));
	CPen penFg(PS_SOLID, max(2, rw / 10), RGB(80, 220, 140));
	CPen* op = mem.SelectObject(&penBg);
	mem.SelectStockObject(NULL_BRUSH);
	mem.Ellipse(&rr);
	mem.SelectObject(&penFg);
	const double start = -90.0;
	const double sweep = 360.0 * rem;
	if (sweep > 0.5) {
		POINT cpt = { cx, cy };
		const double r = rw / 2.0 - 1.0;
		const double a0 = start * 3.141592653589793 / 180.0;
		const double a1 = (start + sweep) * 3.141592653589793 / 180.0;
		POINT p0 = { (LONG)(cx + r * cos(a0)), (LONG)(cy + r * sin(a0)) };
		POINT p1 = { (LONG)(cx + r * cos(a1)), (LONG)(cy + r * sin(a1)) };
		mem.Arc(&rr, p0, p1);
	}
	mem.SelectObject(op);

	CString ts;
	ts.Format(_T("%d:%02d"), remSec / 60, remSec % 60);
	mem.SetBkMode(TRANSPARENT);
	mem.SetTextColor(RGB(255, 255, 255));
	CFont* of = mem.SelectObject(&m_fontTech);
	CRect tr(pad, h - pad - 16, w - pad, h - pad);
	mem.DrawText(ts, &tr, DT_RIGHT | DT_SINGLELINE | DT_BOTTOM);
	mem.SelectObject(of);
}

// バナー右上: アナライザーと同じ意味の φ(相関針) / LR(バランス位置)。
// pDC はバナー局部座標(0,0)=左上。BlitVisualizer のメモリ面に描き込み、画面へは1回だけ出す。
void CMediaPlayerDlg::DrawBannerMeters(CDC* pDC, int bannerW, int bannerH)
{
	if (!pDC || bannerW < 40 || bannerH < 24) return;
	if (!savedata.pro_corr_meter) return;
	const float corr = ProAudio_CorrValue(); // -1..+1 上=+1 下=-1
	const float bal = ProAudio_CorrBalance(); // -1..+1 左L..右R
	const int mw = max(10, (int)(12 * hD2));
	const int lblH = max(11, (int)(12 * hD2));
	const int gap = 2;
	const int mh = max(22, bannerH / 3);
	const int pad = max(4, (int)(6 * hD2));
	const int x1 = bannerW - mw * 2 - pad;
	const int y0 = pad;
	const int x2 = x1 + mw + 2;
	if (x1 < 0 || y0 + mh + gap + lblH > bannerH) return;

	const COLORREF bg = RGB(28, 32, 44);
	const COLORREF edgeHi = RGB(70, 80, 100);
	const COLORREF edgeLo = RGB(40, 45, 60);
	pDC->FillSolidRect(x1, y0, mw, mh, bg);
	pDC->FillSolidRect(x2, y0, mw, mh, bg);
	pDC->Draw3dRect(x1, y0, mw, mh, edgeHi, edgeLo);
	pDC->Draw3dRect(x2, y0, mw, mh, edgeHi, edgeLo);

	// φ: 中央線 + 緑の針（アナライザーと同式）
	const int midY = y0 + mh / 2;
	pDC->FillSolidRect(x1 + 2, midY, mw - 4, 1, RGB(90, 100, 120));
	int cy = midY - (int)(corr * ((mh / 2) - 5));
	if (cy < y0 + 3) cy = y0 + 3;
	if (cy > y0 + mh - 4) cy = y0 + mh - 4;
	pDC->FillSolidRect(x1 + 2, cy - 2, mw - 4, 5, RGB(100, 230, 150));

	// LR: 下端の横位置がバランス（アナライザーの橙マーカと同じ）
	pDC->FillSolidRect(x2 + 2, midY, mw - 4, 1, RGB(90, 100, 120));
	int bx = x2 + mw / 2 + (int)(bal * (mw / 2 - 3));
	if (bx < x2 + 2) bx = x2 + 2;
	if (bx > x2 + mw - 3) bx = x2 + mw - 3;
	pDC->FillSolidRect(bx - 1, y0 + mh - 6, 3, 5, RGB(255, 180, 80));

	pDC->SetBkMode(TRANSPARENT);
	pDC->SetTextColor(RGB(200, 220, 210));
	CFont* oldF = nullptr;
	if (m_fontTech.GetSafeHandle())
		oldF = pDC->SelectObject(&m_fontTech);
	CRect r1(x1, y0 + mh + gap, x1 + mw, y0 + mh + gap + lblH);
	CRect r2(x2, y0 + mh + gap, x2 + mw, y0 + mh + gap + lblH);
	pDC->DrawText(_T("φ"), &r1, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
	pDC->DrawText(_T("LR"), &r2, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
	if (oldF)
		pDC->SelectObject(oldF);
}

BOOL CMediaPlayerDlg::TryPlayFromQueue()
{
	if (m_queueN <= 0 || !pl || !pl->pc) return FALSE;
	int idx = m_queue[0];
	for (int i = 1; i < m_queueN; ++i) m_queue[i - 1] = m_queue[i];
	m_queueN--;
	UpdateQueueChrome();
	if (idx < 0 || idx >= pl->playcnt) return FALSE;
	MP_PlayIndex(idx);
	return TRUE;
}

void CMediaPlayerDlg::UpdateQueueChrome()
{
	if (!m_toolsToggle.GetSafeHwnd()) return;
	// ツールバー上の明示ラベル。欠損件数があれば併記。
	CString base = LL14(L"ツール", L"Tools", L"Outils", L"Strumenti", L"Herramientas", L"도구", L"工具", L"أدوات", L"Инструменты", L"Extras", L"Ferramentas", L"Extra", L"Narzedzia", L"Araclar");
	const int missN = CountMissing();
	CString s = base;
	if (InterlockedCompareExchange(&m_missBusy, 0, 0) != 0)
		s = base + _T("…");
	else if (missN > 0) {
		CString n; n.Format(_T("%s・%d"), (LPCTSTR)base, missN);
		s = n;
	}
	else if (m_queueN > 0) {
		CString n; n.Format(_T("%s ▾"), (LPCTSTR)base);
		s = n;
	}
	else
		s = base + _T(" ▾");
	CString cur;
	m_toolsToggle.GetWindowText(cur);
	if (cur != s) {
		m_toolsToggle.SetWindowText(s);
		m_toolsToggle.RepaintClient();
	}

	CString tip = LL14(L"クリックでメニュー（並べ替えパネル・欠損整理・Up Next など）。", L"Click for menu (sort panel, missing, Up Next, etc.).", L"Clic = menu (tri, manquants, Up Next…).", L"Clic = menu (ordina, mancanti, Up Next…).", L"Clic = menu (ordenar, faltantes, Up Next…).", L"클릭=메뉴(정렬/결손/Up Next 등).", L"点击打开菜单（排序/缺失/Up Next等）。", L"انقر للقائمة.", L"Клик = меню.", L"Klick = Menue.", L"Clique = menu.", L"Klik = menu.", L"Klik = menu.", L"Tikla = menu.");
	if (m_queueN > 0) {
		CString q; q.Format(_T("\nUp Next: %d"), m_queueN);
		tip += q;
	}
	if (InterlockedCompareExchange(&m_missBusy, 0, 0) != 0)
		tip += LL14(L"\n欠損スキャン中…", L"\nScanning missing…", L"\nScan manquants…", L"\nScansione mancanti…", L"\nEscaneando faltantes…", L"\n결손 스캔 중…", L"\n正在扫描缺失…", L"\nجارٍ فحص المفقود…", L"\nСканирование отсутствующих…", L"\nFehlende werden gescannt…", L"\nVarrendo ausentes…", L"\nOntbrekende scannen…", L"\nSkanowanie brakujacych…", L"\nEksikler taraniyor…");
	else if (missN > 0) {
		CString m; m.Format(LL14(L"\n欠損: %d → メニューの「欠損を整理…」", L"\nMissing: %d → Manage missing… in menu", L"\nManquants: %d → Gerer…", L"\nMancanti: %d → Gestisci…", L"\nFaltantes: %d → Gestionar…", L"\n결손: %d → 메뉴에서 정리", L"\n缺失: %d → 菜单中整理", L"\nمفقود: %d", L"\nОтсутствует: %d", L"\nFehlend: %d", L"\nAusentes: %d", L"\nOntbrekend: %d", L"\nBrakujace: %d", L"\nEksik: %d"), missN);
		tip += m;
	}
	if (m_sleepEndTick != 0) {
		const ULONGLONG now = GetTickCount64();
		if (now < m_sleepEndTick) {
			const int sec = (int)((m_sleepEndTick - now) / 1000ULL);
			CString t; t.Format(_T("\nSleep %d:%02d"), sec / 60, sec % 60);
			tip += t;
		}
	}
	const int ab = ProAudio_AbActiveSlot();
	if (ab == 0) tip += _T("\nSnap: A");
	else if (ab == 1) tip += _T("\nSnap: B");

	if (m_tooltip.GetSafeHwnd())
		m_tooltip.UpdateTipText(tip, &m_toolsToggle);
}

int CMediaPlayerDlg::CountMissing() const
{
	if (!m_miss || m_missCap <= 0 || !pl) return 0;
	const int n = (m_missCap < pl->playcnt) ? m_missCap : pl->playcnt;
	int c = 0;
	for (int i = 0; i < n; ++i)
		if (m_miss[i] == 1) ++c;
	return c;
}

void CMediaPlayerDlg::UpdateMissChrome()
{
	UpdateQueueChrome();
}

int CMediaPlayerDlg::QueueCount() const
{
	return m_queueN;
}

int CMediaPlayerDlg::QueueAt(int i) const
{
	if (i < 0 || i >= m_queueN) return -1;
	return m_queue[i];
}

void CMediaPlayerDlg::QueueMove(int from, int to)
{
	if (from < 0 || to < 0 || from >= m_queueN || to >= m_queueN || from == to) return;
	const int v = m_queue[from];
	if (from < to) {
		for (int i = from; i < to; ++i) m_queue[i] = m_queue[i + 1];
	} else {
		for (int i = from; i > to; --i) m_queue[i] = m_queue[i - 1];
	}
	m_queue[to] = v;
	UpdateQueueChrome();
}

void CMediaPlayerDlg::QueueRemoveAt(int i)
{
	if (i < 0 || i >= m_queueN) return;
	for (int j = i + 1; j < m_queueN; ++j) m_queue[j - 1] = m_queue[j];
	m_queueN--;
	UpdateQueueChrome();
}

void CMediaPlayerDlg::QueueClear()
{
	m_queueN = 0;
	UpdateQueueChrome();
}

void CMediaPlayerDlg::QueueAdd(int pcIdx, BOOL playNext)
{
	if (!pl || pcIdx < 0 || pcIdx >= pl->playcnt) return;
	if (m_queueN >= 64) return;
	if (playNext) {
		for (int i = m_queueN; i > 0; --i) m_queue[i] = m_queue[i - 1];
		m_queue[0] = pcIdx;
		m_queueN++;
	}
	else {
		m_queue[m_queueN++] = pcIdx;
	}
	UpdateQueueChrome();
}

void CMediaPlayerDlg::ShowLyricsExtrasMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);

	menu.AddCheck(ID_MP_LRC_EXPAND,
		LL14(L"歌詞パネル拡大（カラオケ風）", L"Expand lyrics panel (karaoke)", L"Agrandir paroles (karaoke)", L"Espandi testi (karaoke)", L"Expandir letra (karaoke)",
			L"가사 패널 확대(가라오케)", L"扩大歌词面板（卡拉OK）", L"توسيع لوحة الكلمات (كاريوكي)", L"Развернуть панель текста (караоке)", L"Textpanel vergroessern (Karaoke)",
			L"Expandir painel de letra (karaoke)", L"Songtekstpaneel vergroten (karaoke)", L"Rozszerz panel tekstu (karaoke)", L"Soz panelini genislet (karaoke)"),
		savedata.mpLrcExpand != 0,
		LL14(L"プレイヤー内の歌詞をカラオケ風スクロール表示にします。", L"Show in-player lyrics as karaoke-style scrolling text.", L"Afficher les paroles en defilement karaoke.", L"Mostra i testi a scorrimento karaoke.", L"Mostrar letra con desplazamiento karaoke.",
			L"플레이어 내 가사를 가라오케풍 스크롤로 표시.", L"将播放器内歌词显示为卡拉OK滚动。", L"عرض الكلمات داخل المشغّل بأسلوب كاريوكي.", L"Показывать текст в плеере в стиле караоке.", L"Liedtext im Player karaokeartig scrollen.",
			L"Mostrar letra no player em estilo karaoke.", L"Songtekst in speler karaoke-achtig tonen.", L"Pokaz tekst w odtwarzaczu jak karaoke.", L"Oynaticida sozleri karaoke kaydirmali goster."));

	menu.AddCheck(ID_MP_DESK_LRC,
		LL14(L"歌詞ウィンドウを表示", L"Show lyrics window", L"Afficher fenetre paroles", L"Mostra finestra testi", L"Mostrar ventana de letra",
			L"가사 창 표시", L"显示歌词窗口", L"عرض نافذة الكلمات", L"Показать окно текста", L"Textfenster anzeigen",
			L"Mostrar janela de letra", L"Songtekstvenster tonen", L"Pokaz okno tekstu", L"Soz penceresini goster"),
		IsDesktopLyricsOpen(),
		LL14(L"常時最前面の歌詞ウィンドウを開きます（不透明度・行数・フォントはウィンドウ右クリック）。", L"Open an always-on-top lyrics window (opacity/lines/font via window RMB).", L"Ouvre une fenetre de paroles au premier plan (opacite/lignes/police au clic droit).", L"Apre una finestra testi in primo piano (opacita/righe/font col destro).", L"Abre una ventana de letra siempre visible (opacidad/lineas/fuente con clic der.).",
			L"항상 위 가사 창을 엽니다(불투명도·행수·글꼴은 창 우클릭).", L"打开置顶歌词窗口（不透明度/行数/字体在窗口右键）。", L"يفتح نافذة كلمات في المقدمة (العتامة/الأسطر/الخط بزر يمين النافذة).", L"Открывает окно текста поверх всех (непрозрачность/строки/шрифт — ПКМ по окну).", L"Oeffnet ein Textfenster im Vordergrund (Deckkraft/Zeilen/Schrift per RMB).",
			L"Abre janela de letra no topo (opacidade/linhas/fonte no botao dir. da janela).", L"Opent songtekstvenster bovenop (dekking/regels/lettertype via RMB).", L"Otwiera okno tekstu na wierzchu (nieprzezroczystosc/wiersze/czcionka przez PPM).", L"Her zaman ustte soz penceresi acar (opaklik/satir/yazi pencere sag tik)."));

	menu.AddSeparator();
	{
		CCustomPopupMenu* lrcSub = menu.AddSubMenu(
			LL14(L"LRC 微調整", L"LRC fine adjust", L"Reglage fin LRC", L"Regolazione fine LRC",
				L"Ajuste fino LRC", L"LRC 미세 조정", L"LRC 微调", L"ضبط دقيق LRC",
				L"Тонкая настройка LRC", L"LRC Feineinstellung", L"Ajuste fino LRC", L"LRC fijnafstellen",
				L"Dostrojenie LRC", L"LRC ince ayar"),
				LL14(L"歌詞タイミングを ±10/50/100 ms 単位でずらします", L"Nudge lyric timing by ±10/50/100 ms", L"Decaler le timing par ±10/50/100 ms", L"Sposta il timing di ±10/50/100 ms", L"Desplazar el timing ±10/50/100 ms", L"가사 타이밍을 ±10/50/100 ms 단위로 이동", L"按 ±10/50/100 ms 微调歌词时间", L"إزاحة توقيت الكلمات بمقدار ±10/50/100 مللي ثانية", L"Сдвинуть тайминг текста на ±10/50/100 мс", L"Text-Timing um ±10/50/100 ms verschieben", L"Deslocar o timing da letra em ±10/50/100 ms", L"Teksttiming met ±10/50/100 ms verschuiven", L"Przesun timing tekstu o ±10/50/100 ms", L"Soz zamanlamasini ±10/50/100 ms kaydir"));
		if (lrcSub) {
			lrcSub->AddCommand(ID_MP_LRC_MINUS100, L"-100 ms");
			lrcSub->AddCommand(ID_MP_LRC_MINUS50, L"-50 ms");
			lrcSub->AddCommand(ID_MP_LRC_MINUS10, L"-10 ms");
			lrcSub->AddCommand(ID_MP_LRC_PLUS10, L"+10 ms");
			lrcSub->AddCommand(ID_MP_LRC_PLUS50, L"+50 ms");
			lrcSub->AddCommand(ID_MP_LRC_PLUS100, L"+100 ms");
		}
	}
	menu.AddCommand(ID_MP_LRC_SAVE,
		LL14(L"LRC を保存…", L"Save LRC…", L"Enregistrer LRC…", L"Salva LRC…", L"Guardar LRC…", L"LRC 저장…", L"保存 LRC…", L"حفظ LRC…", L"Сохранить LRC…", L"LRC speichern…", L"Salvar LRC…", L"LRC opslaan…", L"Zapisz LRC…", L"LRC kaydet…"),
		LL14(L"調整した歌詞タイミングを LRC ファイルに保存します", L"Save adjusted lyric timing to an LRC file", L"Enregistrer le timing ajuste dans un LRC", L"Salva il timing regolato in un file LRC", L"Guardar el timing ajustado en un LRC", L"조정한 가사 타이밍을 LRC로 저장", L"将调整后的歌词时间保存为 LRC", L"حفظ توقيت الكلمات المضبوط في ملف LRC", L"Сохранить скорректированный тайминг в LRC", L"Angepasstes Text-Timing als LRC speichern", L"Salvar o timing ajustado em um LRC", L"Aangepaste teksttiming als LRC opslaan", L"Zapisz skorygowany timing do LRC", L"Ayarlanan soz zamanlamasini LRC olarak kaydet"));
	menu.AddSeparator();
	menu.AddCommand(ID_MP_PHRASE_AB,
		LL14(L"フレーズA-B [R]", L"Phrase A-B [R]", L"Phrase A-B [R]", L"Frase A-B [R]", L"Frase A-B [R]",
			L"프레이즈 A-B [R]", L"乐句A-B [R]", L"عبارة A-B [R]", L"Фраза A-B [R]", L"Phrase A-B [R]",
			L"Frase A-B [R]", L"Frase A-B [R]", L"Fraza A-B [R]", L"Cumle A-B [R]"),
			LL14(L"現在位置を中心にフレーズ幅で A-B を設定します（ショートカット R）", L"Set A-B around now by phrase width (shortcut R)", L"Definir A-B autour de maintenant selon la phrase (R)", L"Imposta A-B attorno ad ora per larghezza frase (R)", L"Fijar A-B alrededor de ahora segun frase (R)", L"현재 위치 기준으로 프레이즈 폭 A-B 설정(단축키 R)", L"以当前位置为中心按乐句宽度设 A-B（快捷键 R）", L"تعيين A-B حول الموضع بعرض العبارة (اختصار R)", L"Задать A-B вокруг текущей позиции по ширине фразы (R)", L"A-B um Jetzt mit Phrasenbreite setzen (Kurzbefehl R)", L"Definir A-B em torno de agora pela largura da frase (R)", L"A-B rond nu zetten op frasebreedte (sneltoets R)", L"Ustaw A-B wokol teraz wg szerokosci frazy (R)", L"Su anin etrafinda cumle genisligiyle A-B ayarla (R)"));
	menu.AddCommand(ID_MP_SEEK_ABCLR,
		LL14(L"A-B解除", L"Clear A-B", L"Effacer A-B", L"Cancella A-B", L"Borrar A-B", L"A-B 해제", L"清除A-B", L"مسح A-B", L"Сброс A-B", L"A-B aus", L"Limpar A-B", L"A-B uit", L"Wyczysc A-B", L"A-B sil"),
		LL14(L"A-B 区間とループ選択を解除します", L"Clear the A-B range and loop selection", L"Effacer la plage A-B et la boucle", L"Cancella l'intervallo A-B e il loop", L"Borrar el rango A-B y el bucle", L"A-B 구간과 루프 선택을 해제", L"清除 A-B 区间和循环选择", L"مسح نطاق A-B والحلقة", L"Сбросить диапазон A-B и цикл", L"A-B-Bereich und Loop loeschen", L"Limpar o intervalo A-B e o loop", L"A-B-bereik en lus wissen", L"Wyczysc zakres A-B i petle", L"A-B araligini ve donguyu temizle"));
	menu.AddCommand(ID_MP_SEEK_CUEADD,
		LL14(L"キューを現在位置に追加", L"Add cue at now", L"Ajouter cue ici", L"Aggiungi cue qui",
			L"Anadir cue aqui", L"현재 위치에 큐 추가", L"在当前位置添加标记", L"إضافة إشارة هنا",
			L"Добавить метку здесь", L"Cue hier hinzufugen", L"Adicionar cue aqui",
			L"Cue hier toevoegen", L"Dodaj cue tutaj", L"Buraya cue ekle"),
			LL14(L"現在位置にキュー（ジャンプ用マーカー）を追加します", L"Add a cue marker at the current position", L"Ajouter un marqueur cue a la position actuelle", L"Aggiungi un cue alla posizione attuale", L"Anadir un cue en la posición actual", L"현재 위치에 큐 마커 추가", L"在当前位置添加标记", L"إضافة إشارة عند الموضع الحالي", L"Добавить метку в текущей позиции", L"Cue an aktueller Position hinzufugen", L"Adicionar um cue na posicao atual", L"Cue op huidige positie toevoegen", L"Dodaj cue w biezacej pozycji", L"Su anki konuma cue ekle"));

	const UINT cmd = menu.Track(screenPt, this);
	if (cmd)
		PostMessage(WM_COMMAND, cmd);
}

void CMediaPlayerDlg::ShowSettingsExtrasMenu(CPoint screenPt)
{
	MpShowSettingsExtrasMenu(this, screenPt);
}

void MpShowSettingsExtrasMenu(CWnd* owner, CPoint screenPt)
{
	if (!owner) return;
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);

	menu.AddCheck(ID_MP_SET_UPSCALE,
		LL14(L"アップスケール出力", L"Upscale output", L"Sortie upscale", L"Uscita upscale", L"Salida upscale",
			L"업스케일 출력", L"升频输出", L"خرج التكبير", L"Апскейл выход", L"Upscale-Ausgabe",
			L"Saida upscale", L"Upscale-uitvoer", L"Wyjscie upscale", L"Upscale cikis"),
		savedata.upscale_enable != 0,
		LL14(L"出力を高サンプルレートへアップスケールします（再初期化あり）", L"Upscale output to a higher sample rate (re-inits audio)", L"Upscaler la sortie (reinit audio)", L"Upscale dell'uscita (reiniz. audio)", L"Escalar la salida (reinicia audio)", L"출력을 고샘플레이트로 업스케일(오디오 재초기화)", L"将输出升频到更高采样率（会重新初始化）", L"رفع معدل العينات للخرج (إعادة تهيئة)", L"Апскейл выхода к более высокой ЧД (реинит.)", L"Ausgabe auf hoehere Samplerate (Audio-Reinit)", L"Fazer upscale da saida (reinicia audio)", L"Uitvoer upscalen (audio-herinit)", L"Upscale wyjscia (reinicjalizacja audio)", L"Cikisi yuksek ornekleme hizina cikar (ses yeniden)"));

	{
		CCustomPopupMenu* bitSub = menu.AddSubMenu(
			LL14(L"ビット深度", L"Bit depth", L"Profondeur bits", L"Profondita bit", L"Profundidad de bits",
				L"비트 심도", L"位深", L"عمق البت", L"Разрядность", L"Bittiefe",
				L"Profundidade de bits", L"Bitdiepte", L"Glebia bitowa", L"Bit derinligi"),
				LL14(L"出力のビット深度を選びます（変更時は再初期化）", L"Choose output bit depth (re-inits on change)", L"Choisir la profondeur de bits (reinit)", L"Scegli la profondita bit (reiniz.)", L"Elegir profundidad de bits (reinicia)", L"출력 비트 심도를 선택(변경 시 재초기화)", L"选择输出位深（更改会重新初始化）", L"اختيار عمق بت الخرج (إعادة تهيئة عند التغيير)", L"Выбрать разрядность выхода (реинит.)", L"Ausgangsbittiefe wahlen (Reinit bei Anderung)", L"Escolher profundidade de bits (reinicia)", L"Uitvoerbitdiepte kiezen (herinit bij wijziging)", L"Wybierz glebie bitowa (reinicj. przy zmianie)", L"Cikis bit derinligini sec (degisince yeniden)"));
		if (bitSub) {
			const int bits = savedata.bit32 ? 32 : (savedata.bit24 ? 24 : 16);
			bitSub->AddCheck(ID_MP_SET_BIT16, L"16 bit", bits == 16);
			bitSub->AddCheck(ID_MP_SET_BIT24, L"24 bit", bits == 24);
			bitSub->AddCheck(ID_MP_SET_BIT32, L"32 bit", bits == 32);
		}
	}
	{
		CCustomPopupMenu* spkSub = menu.AddSubMenu(
			LL14(L"出力チャンネル", L"Output channels", L"Canaux de sortie", L"Canali di uscita", L"Canales de salida",
				L"출력 채널", L"输出声道", L"قنوات الخرج", L"Каналы выхода", L"Ausgangskanäle",
				L"Canais de saida", L"Uitgangskanalen", L"Kanaly wyjsciowe", L"Cikis kanallari"),
				LL14(L"出力スピーカー配置を選びます（変更時は再初期化）", L"Choose output speaker layout (re-inits on change)", L"Choisir la disposition des enceintes (reinit)", L"Scegli il layout degli altoparlanti (reiniz.)", L"Elegir disposicion de altavoces (reinicia)", L"출력 스피커 배치를 선택(변경 시 재초기화)", L"选择输出扬声器布局（更改会重新初始化）", L"اختيار تخطيط مكبرات الخرج (إعادة تهيئة)", L"Выбрать раскладку колонок (реинит.)", L"Lautsprecherlayout wahlen (Reinit bei Anderung)", L"Escolher layout de alto-falantes (reinicia)", L"Luidsprekerlayout kiezen (herinit bij wijziging)", L"Wybierz uklad glosnikow (reinicj. przy zmianie)", L"Cikis hoparlor yerlesimini sec (degisince yeniden)"));
		if (spkSub) {
			const int sp = (savedata.speaker_layout >= 0 && savedata.speaker_layout <= 5) ? savedata.speaker_layout : 0;
			spkSub->AddCheck(ID_MP_SET_SPK0, LL14(L"ステレオ (2ch)", L"Stereo (2ch)", L"Stereo (2ch)", L"Stereo (2ch)", L"Estereo (2ch)", L"스테레오 (2ch)", L"立体声 (2ch)", L"ستيريو (2ch)", L"Стерео (2ch)", L"Stereo (2ch)", L"Estereo (2ch)", L"Stereo (2ch)", L"Stereo (2ch)", L"Stereo (2ch)"), sp == 0);
			spkSub->AddCheck(ID_MP_SET_SPK1, L"2.1ch", sp == 1);
			spkSub->AddCheck(ID_MP_SET_SPK2, L"4ch", sp == 2);
			spkSub->AddCheck(ID_MP_SET_SPK3, L"5.1ch", sp == 3);
			spkSub->AddCheck(ID_MP_SET_SPK4, L"7.1ch", sp == 4);
			spkSub->AddCheck(ID_MP_SET_SPK5, LL14(L"オリジナル（マッピングなし）", L"Original (no mapping)", L"Original (sans mappage)", L"Originale (no mapping)", L"Original (sin mapeo)", L"원본(매핑 없음)", L"原始（不映射）", L"أصلي", L"Исходный", L"Original", L"Original", L"Origineel", L"Oryginal", L"Orijinal"), sp == 5);
		}
	}
	{
		CCustomPopupMenu* rateSub = menu.AddSubMenu(
			LL14(L"サンプルレート", L"Sample rate", L"Frequence d'echantillonnage", L"Frequenza di campionamento", L"Frecuencia de muestreo",
				L"샘플레이트", L"采样率", L"معدل العينات", L"Частота дискретизации", L"Samplerate",
				L"Taxa de amostragem", L"Samplefrequentie", L"Czestotliwosc", L"Ornekleme hizi"),
				LL14(L"出力サンプルレートを選びます（変更時は再初期化）", L"Choose output sample rate (re-inits on change)", L"Choisir la frequence d'echantillonnage (reinit)", L"Scegli la frequenza di campionamento (reiniz.)", L"Elegir frecuencia de muestreo (reinicia)", L"출력 샘플레이트를 선택(변경 시 재초기화)", L"选择输出采样率（更改会重新初始化）", L"اختيار معدل عينات الخرج (إعادة تهيئة)", L"Выбрать частоту дискретизации (реинит.)", L"Samplerate wahlen (Reinit bei Anderung)", L"Escolher taxa de amostragem (reinicia)", L"Samplefrequentie kiezen (herinit bij wijziging)", L"Wybierz czestotliwosc (reinicj. przy zmianie)", L"Cikis ornekleme hizini sec (degisince yeniden)"));
		if (rateSub) {
			rateSub->AddCheck(ID_MP_SET_RATE_44100, L"44100 Hz", savedata.samples == 44100);
			rateSub->AddCheck(ID_MP_SET_RATE_48000, L"48000 Hz", savedata.samples == 48000);
			rateSub->AddCheck(ID_MP_SET_RATE_96000, L"96000 Hz", savedata.samples == 96000);
			rateSub->AddCheck(ID_MP_SET_RATE_192000, L"192000 Hz", savedata.samples == 192000);
		}
	}

	menu.AddSeparator();
	menu.AddCheck(ID_MP_SET_AERO,
		LL14(L"アクリルモード", L"Acrylic mode", L"Mode acrylique", L"Modalita acrilico", L"Modo acrilico",
			L"아크릴 모드", L"亚克力模式", L"وضع الأكريليك", L"Акриловый режим", L"Acryl-Modus",
			L"Modo acrilico", L"Acrylmodus", L"Tryb akrylowy", L"Akrilik mod"),
		savedata.aero != 0,
		LL14(L"ウィンドウ背景をアクリル／半透明スタイルにします", L"Use acrylic/translucent window backgrounds", L"Utiliser un fond acrylique/translucide", L"Usa sfondo acrilico/traslucido", L"Usar fondo acrilico/translucido", L"창 배경을 아크릴/반투명으로", L"使用亚克力/半透明窗口背景", L"استخدام خلفية أكريليك/شفافة", L"Акриловый/полупрозрачный фон окна", L"Acryl-/Halbtransparenten Fensterhintergrund nutzen", L"Usar fundo acrilico/translucido", L"Acryl-/doorschijnende vensterachtergrond", L"Uzyj akrylowego/polprzezroczystego tla", L"Pencere arka planini akrilik/yarisaydam yap"));
	menu.AddCheck(ID_MP_SET_LRC_NET,
		LL14(L"ネットから LRC 取得", L"Fetch LRC from network", L"Telecharger LRC reseau", L"Scarica LRC rete", L"Obtener LRC de red",
			L"네트워크에서 LRC 가져오기", L"从网络获取 LRC", L"جلب LRC من الشبكة", L"Загрузить LRC из сети", L"LRC aus dem Netz laden",
			L"Buscar LRC na rede", L"LRC van netwerk ophalen", L"Pobierz LRC z sieci", L"Agdan LRC al"),
		savedata.lrc_net != 0,
		LL14(L"ネットから歌詞（LRC）を自動取得します", L"Automatically fetch lyrics (LRC) from the network", L"Telecharger automatiquement les paroles (LRC)", L"Scarica automaticamente i testi (LRC)", L"Obtener automaticamente la letra (LRC) de la red", L"네트워크에서 가사(LRC) 자동 가져오기", L"自动从网络获取歌词（LRC）", L"جلب الكلمات (LRC) تلقائياً من الشبكة", L"Автоматически загружать текст (LRC) из сети", L"Liedtexte (LRC) automatisch aus dem Netz laden", L"Buscar letra (LRC) automaticamente na rede", L"Songtekst (LRC) automatisch van het netwerk ophalen", L"Automatycznie pobieraj tekst (LRC) z sieci", L"Agdan sozleri (LRC) otomatik al"));
	menu.AddCheck(ID_MP_SET_SPEANA,
		LL14(L"スペアナモード", L"Spectrum analyzer mode", L"Mode speana", L"Modalita speana", L"Modo speana",
			L"스펙애너 모드", L"频谱模式", L"وضع speana", L"Режим speana", L"Speana-Modus",
			L"Modo speana", L"Speana-modus", L"Tryb speana", L"Speana modu"),
		savedata.speanamode != 0,
		LL14(L"バナー／ジャケット周辺のスペクトラム表示を有効にします", L"Enable spectrum display around the banner/jacket", L"Activer l'affichage spectre autour de la banniere", L"Abilita lo spettro intorno al banner/copertina", L"Activar el espectro alrededor del banner/caratula", L"배너/재킷 주변 스펙트럼 표시를 켭니다", L"启用横幅/封面附近的频谱显示", L"تفعيل عرض الطيف حول الشريط/الغلاف", L"Включить спектр у баннера/обложки", L"Spektrum um Banner/Cover aktivieren", L"Ativar espectro ao redor do banner/capa", L"Spectrum rond banner/hoes inschakelen", L"Wlacz widmo wokol banera/okladki", L"Banner/kapak cevresinde spektrum gosterimini ac"));

	menu.AddSeparator();
	{
		CCustomPopupMenu* mp3Sub = menu.AddSubMenu(LL14(L"mp3 音量", L"mp3 volume", L"Volume mp3", L"Volume mp3", L"Volumen mp3", L"mp3 볼륨", L"mp3 音量", L"مستوى mp3", L"Громкость mp3", L"mp3-Lautstarke", L"Volume mp3", L"mp3-volume", L"Glosnosc mp3", L"mp3 ses"), LL14(L"mp3 再生時の内部ゲイン倍率を選びます", L"Choose internal gain multiplier for mp3 playback", L"Choisir le gain interne pour la lecture mp3", L"Scegli il gain interno per la riproduzione mp3", L"Elegir la ganancia interna para mp3", L"mp3 재생 내부 게인 배율을 선택", L"选择 mp3 播放的内部增益倍率", L"اختيار مضاعف الكسب الداخلي لتشغيل mp3", L"Выбрать внутренний коэффициент для mp3", L"Internen Gain-Faktor fur mp3 wahlen", L"Escolher o ganho interno para mp3", L"Interne gain-factor voor mp3 kiezen", L"Wybierz wewnetrzny wspolczynnik gain dla mp3", L"mp3 calma dahili kazanc carpanini sec"));
		if (mp3Sub) {
			mp3Sub->AddCheck(ID_MP_SET_MP3_1, L"1.0x", savedata.mp3 == 1);
			mp3Sub->AddCheck(ID_MP_SET_MP3_2, L"1.5x", savedata.mp3 == 2);
			mp3Sub->AddCheck(ID_MP_SET_MP3_3, L"2.0x", savedata.mp3 == 3);
			mp3Sub->AddCheck(ID_MP_SET_MP3_4, L"2.5x", savedata.mp3 == 4);
			mp3Sub->AddCheck(ID_MP_SET_MP3_5, L"3.0x", savedata.mp3 == 5);
		}
		CCustomPopupMenu* spcSub = menu.AddSubMenu(LL14(L"SPC/HES 音量", L"SPC/HES volume", L"Volume SPC/HES", L"Volume SPC/HES", L"Volumen SPC/HES", L"SPC/HES 볼륨", L"SPC/HES 音量", L"مستوى SPC/HES", L"Громкость SPC/HES", L"SPC/HES-Lautstarke", L"Volume SPC/HES", L"SPC/HES-volume", L"Glosnosc SPC/HES", L"SPC/HES ses"), LL14(L"SPC/HES 再生時の内部ゲイン倍率を選びます", L"Choose internal gain multiplier for SPC/HES", L"Choisir le gain interne pour SPC/HES", L"Scegli il gain interno per SPC/HES", L"Elegir la ganancia interna para SPC/HES", L"SPC/HES 재생 내부 게인 배율을 선택", L"选择 SPC/HES 播放的内部增益倍率", L"اختيار مضاعف الكسب الداخلي لـ SPC/HES", L"Выбрать внутренний коэффициент для SPC/HES", L"Internen Gain-Faktor fur SPC/HES wahlen", L"Escolher o ganho interno para SPC/HES", L"Interne gain-factor voor SPC/HES kiezen", L"Wybierz wewnetrzny wspolczynnik gain dla SPC/HES", L"SPC/HES calma dahili kazanc carpanini sec"));
		if (spcSub) {
			spcSub->AddCheck(ID_MP_SET_SPC_1, L"1x", savedata.spc == 1);
			spcSub->AddCheck(ID_MP_SET_SPC_2, L"2x", savedata.spc == 2);
			spcSub->AddCheck(ID_MP_SET_SPC_4, L"4x", savedata.spc == 4);
			spcSub->AddCheck(ID_MP_SET_SPC_8, L"8x", savedata.spc == 8);
			spcSub->AddCheck(ID_MP_SET_SPC_16, L"16x", savedata.spc == 16);
		}
		CCustomPopupMenu* kpiSub = menu.AddSubMenu(LL14(L"その他 kpi 音量", L"Other kpi volume", L"Volume kpi autres", L"Volume kpi altri", L"Volumen kpi otros", L"기타 kpi 볼륨", L"其他 kpi 音量", L"مستوى kpi أخرى", L"Громкость прочих kpi", L"Sonstige kpi-Lautstarke", L"Volume kpi outros", L"Overig kpi-volume", L"Glosnosc innych kpi", L"Diger kpi ses"), LL14(L"その他 kpi 再生時の内部ゲイン倍率を選びます", L"Choose internal gain multiplier for other kpi formats", L"Choisir le gain interne pour les autres kpi", L"Scegli il gain interno per altri kpi", L"Elegir la ganancia interna para otros kpi", L"기타 kpi 재생 내부 게인 배율을 선택", L"选择其他 kpi 播放的内部增益倍率", L"اختيار مضاعف الكسب الداخلي لـ kpi الأخرى", L"Выбрать внутренний коэффициент для прочих kpi", L"Internen Gain-Faktor fur sonstige kpi wahlen", L"Escolher o ganho interno para outros kpi", L"Interne gain-factor voor overige kpi kiezen", L"Wybierz wewnetrzny wspolczynnik gain dla innych kpi", L"Diger kpi calma dahili kazanc carpanini sec"));
		if (kpiSub) {
			kpiSub->AddCheck(ID_MP_SET_KPI_1, L"1.0x", savedata.kpivol == 1);
			kpiSub->AddCheck(ID_MP_SET_KPI_2, L"1.5x", savedata.kpivol == 2);
			kpiSub->AddCheck(ID_MP_SET_KPI_3, L"2.0x", savedata.kpivol == 3);
			kpiSub->AddCheck(ID_MP_SET_KPI_4, L"2.5x", savedata.kpivol == 4);
			kpiSub->AddCheck(ID_MP_SET_KPI_5, L"3.0x", savedata.kpivol == 5);
		}
	}
	menu.AddCheck(ID_MP_SET_M4A,
		LL14(L"m4a 内蔵エンジン", L"Built-in m4a engine", L"Moteur m4a integre", L"Motore m4a interno", L"Motor m4a integrado",
			L"m4a 내장 엔진", L"内置 m4a 引擎", L"محرك m4a مدمج", L"Встроенный m4a", L"Integrierte m4a-Engine",
			L"Motor m4a interno", L"Ingebouwde m4a-engine", L"Wbudowany silnik m4a", L"Dahili m4a motoru"),
		savedata.m4a != 0,
		LL14(L"m4a/AAC を内蔵エンジンでデコードします", L"Decode m4a/AAC with the built-in engine", L"Decoder m4a/AAC avec le moteur integre", L"Decodifica m4a/AAC con il motore interno", L"Decodificar m4a/AAC con el motor integrado", L"m4a/AAC를 내장 엔진으로 디코드", L"用内置引擎解码 m4a/AAC", L"فك m4a/AAC بالمحرك المدمج", L"Декодировать m4a/AAC встроенным движком", L"m4a/AAC mit integrierter Engine dekodieren", L"Decodificar m4a/AAC com o motor interno", L"m4a/AAC decoderen met de ingebouwde engine", L"Dekoduj m4a/AAC wbudowanym silnikiem", L"m4a/AAC'yi dahili motorla coz"));
	menu.AddCheck(ID_MP_SET_MP3ORIG,
		LL14(L"mp3 オリジナルデコーダ", L"Original mp3 decoder", L"Decodeur mp3 original", L"Decoder mp3 originale", L"Decodificador mp3 original",
			L"mp3 원본 디코더", L"原始 mp3 解码器", L"مفكك mp3 أصلي", L"Оригинальный mp3-декодер", L"Originaler mp3-Decoder",
			L"Decoder mp3 original", L"Originele mp3-decoder", L"Oryginalny dekoder mp3", L"Orijinal mp3 cozucu"),
		savedata.mp3orig != 0,
		LL14(L"mp3 をオリジナル互換デコーダで再生します", L"Play mp3 with the original-compatible decoder", L"Lire le mp3 avec le decodeur original", L"Riproduci mp3 con il decoder originale", L"Reproducir mp3 con el decodificador original", L"mp3를 원본 호환 디코더로 재생", L"用原始兼容解码器播放 mp3", L"تشغيل mp3 بمفكك متوافق أصلي", L"Воспроизводить mp3 оригинальным декодером", L"mp3 mit originalem Decoder wiedergeben", L"Reproduzir mp3 com o decoder original", L"mp3 afspelen met de originele decoder", L"Odtwarzaj mp3 oryginalnym dekoderem", L"mp3'u orijinal uyumlu cozucuyle cal"));

	menu.AddSeparator();
	menu.AddCommand(ID_MP_SET_OPEN,
		LL14(L"設定画面を開く…", L"Open settings…", L"Ouvrir les reglages…", L"Apri impostazioni…", L"Abrir ajustes…",
			L"설정 화면 열기…", L"打开设置…", L"فتح الإعدادات…", L"Открыть настройки…", L"Einstellungen öffnen…",
			L"Abrir configuracoes…", L"Instellingen openen…", L"Otworz ustawienia…", L"Ayarlari ac…"),
			LL14(L"詳細な設定ダイアログを開きます", L"Open the full settings dialog", L"Ouvrir la boite de dialogue des reglages", L"Apri la finestra delle impostazioni", L"Abrir el dialogo de ajustes", L"상세 설정 대화상자를 엽니다", L"打开详细设置对话框", L"فتح مربع حوار الإعدادات", L"Открыть диалог настроек", L"Einstellungsdialog oeffnen", L"Abrir o dialogo de configuracoes", L"Instellingendialoog openen", L"Otworz okno ustawien", L"Ayarlar penceresini ac"));

	const UINT cmd = menu.Track(screenPt, owner);
	if (!cmd) return;

	BOOL needSound = FALSE;
	BOOL needPersist = TRUE;
	if (cmd == ID_MP_SET_OPEN) {
		if (mp && ::IsWindow(mp->GetSafeHwnd()))
			mp->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_MP_SETTINGS, BN_CLICKED), 0);
		else if (og && ::IsWindow(og->GetSafeHwnd()))
			og->PostMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON21, BN_CLICKED), 0);
		return;
	} else if (cmd == ID_MP_SET_UPSCALE) {
		savedata.upscale_enable = savedata.upscale_enable ? 0 : 1;
		needSound = TRUE;
	} else if (cmd == ID_MP_SET_BIT16) {
		savedata.bit24 = 0; savedata.bit32 = 0; needSound = TRUE;
	} else if (cmd == ID_MP_SET_BIT24) {
		savedata.bit24 = 1; savedata.bit32 = 0; needSound = TRUE;
	} else if (cmd == ID_MP_SET_BIT32) {
		savedata.bit32 = 1; needSound = TRUE;
	} else if (cmd >= ID_MP_SET_SPK0 && cmd <= ID_MP_SET_SPK5) {
		savedata.speaker_layout = (int)(cmd - ID_MP_SET_SPK0);
		needSound = TRUE;
	} else if (cmd == ID_MP_SET_RATE_44100) { savedata.samples = 44100; needSound = TRUE; }
	else if (cmd == ID_MP_SET_RATE_48000) { savedata.samples = 48000; needSound = TRUE; }
	else if (cmd == ID_MP_SET_RATE_96000) { savedata.samples = 96000; needSound = TRUE; }
	else if (cmd == ID_MP_SET_RATE_192000) { savedata.samples = 192000; needSound = TRUE; }
	else if (cmd == ID_MP_SET_AERO) {
		savedata.aero = savedata.aero ? 0 : 1;
		MpPersistSavedataQuick();
		CCC_NotifyAeroSettingChanged();
		return;
	} else if (cmd == ID_MP_SET_LRC_NET) {
		savedata.lrc_net = savedata.lrc_net ? 0 : 1;
	} else if (cmd == ID_MP_SET_SPEANA) {
		savedata.speanamode = savedata.speanamode ? 0 : 1;
	} else if (cmd == ID_MP_SET_M4A) {
		savedata.m4a = savedata.m4a ? 0 : 1;
	} else if (cmd == ID_MP_SET_MP3ORIG) {
		savedata.mp3orig = savedata.mp3orig ? 0 : 1;
	} else if (cmd >= ID_MP_SET_MP3_1 && cmd <= ID_MP_SET_MP3_5) {
		savedata.mp3 = (int)(cmd - ID_MP_SET_MP3_1) + 1;
	} else if (cmd == ID_MP_SET_SPC_1) savedata.spc = 1;
	else if (cmd == ID_MP_SET_SPC_2) savedata.spc = 2;
	else if (cmd == ID_MP_SET_SPC_4) savedata.spc = 4;
	else if (cmd == ID_MP_SET_SPC_8) savedata.spc = 8;
	else if (cmd == ID_MP_SET_SPC_16) savedata.spc = 16;
	else if (cmd >= ID_MP_SET_KPI_1 && cmd <= ID_MP_SET_KPI_5) {
		savedata.kpivol = (int)(cmd - ID_MP_SET_KPI_1) + 1;
	} else {
		needPersist = FALSE;
	}

	if (needPersist)
		MpPersistSavedataQuick();
	if (needSound)
		MpRecreatePlaybackOutput();
}

void CMediaPlayerDlg::ShowFolderExtrasMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.AddCommand(ID_MP_FOLDER_OPEN,
		LL14(L"フォルダ設定を開く…", L"Open folder settings…", L"Ouvrir parametres dossier…", L"Apri impostazioni cartella…", L"Abrir config. carpeta…",
			L"폴더 설정 열기…", L"打开文件夹设置…", L"فتح إعدادات المجلد…", L"Открыть настройки папки…", L"Ordnereinstellungen öffnen…",
			L"Abrir config. pasta…", L"Mapinstellingen openen…", L"Otworz ustawienia folderu…", L"Klasor ayarlarini ac…"),
			LL14(L"監視フォルダなどのフォルダ設定を開きます", L"Open folder settings (watched folders, etc.)", L"Ouvrir les parametres de dossiers", L"Apri le impostazioni cartelle", L"Abrir la configuracion de carpetas", L"감시 폴더 등 폴더 설정을 엽니다", L"打开文件夹设置（监视文件夹等）", L"فتح إعدادات المجلدات", L"Открыть настройки папок", L"Ordnereinstellungen oeffnen", L"Abrir as configuracoes de pastas", L"Mapinstellingen openen", L"Otworz ustawienia folderow", L"Klasor ayarlarini ac"));
	menu.AddCommand(ID_MP_FOLDER_ADD,
		LL14(L"フォルダを追加…", L"Add folder…", L"Ajouter un dossier…", L"Aggiungi cartella…", L"Anadir carpeta…",
			L"폴더 추가…", L"添加文件夹…", L"إضافة مجلد…", L"Добавить папку…", L"Ordner hinzufügen…",
			L"Adicionar pasta…", L"Map toevoegen…", L"Dodaj folder…", L"Klasor ekle…"),
			LL14(L"ライブラリへ新しいフォルダを追加します", L"Add a new folder to the library", L"Ajouter un nouveau dossier a la bibliotheque", L"Aggiungi una nuova cartella alla libreria", L"Anadir una carpeta nueva a la biblioteca", L"라이브러리에 새 폴더를 추가", L"向媒体库添加新文件夹", L"إضافة مجلد جديد إلى المكتبة", L"Добавить новую папку в библиотеку", L"Neuen Ordner zur Bibliothek hinzufugen", L"Adicionar uma nova pasta a biblioteca", L"Een nieuwe map aan de bibliotheek toevoegen", L"Dodaj nowy folder do biblioteki", L"Kitapliga yeni klasor ekle"));
	menu.AddCommand(ID_MP_FOLDER_SYNC,
		LL14(L"フォルダ同期差分…", L"Folder sync diff…", L"Diff sync dossier…", L"Diff sync cartella…", L"Diff sync carpeta…",
			L"폴더 동기화 차이…", L"文件夹同步差异…", L"فرق مزامنة المجلد…", L"Разница синхр. папки…", L"Ordner-Sync-Diff…",
			L"Diff sync pasta…", L"Map-sync-diff…", L"Roznica sync folderu…", L"Klasor senkron fark…"),
			LL14(L"監視フォルダとリストの差分を確認・同期します", L"Review and sync differences vs watched folders", L"Verifier/synchroniser les differences des dossiers", L"Controlla/sincronizza le differenze delle cartelle", L"Revisar/sincronizar diferencias de carpetas", L"감시 폴더와 목록 차이를 확인하고 동기화", L"查看并同步监视文件夹与列表的差异", L"مراجعة ومزامنة فروق المجلدات", L"Проверить и синхронизировать различия папок", L"Differenzen zu Uberwachungsordnern prufen/synchronisieren", L"Revisar e sincronizar diferencas das pastas", L"Mapverschillen controleren/synchroniseren", L"Sprawdz i zsynchronizuj roznice folderow", L"Klasor farklarini incele ve senkronize et"));
	const UINT cmd = menu.Track(screenPt, this);
	if (cmd == ID_MP_FOLDER_OPEN) OnFolder();
	else if (cmd == ID_MP_FOLDER_ADD) OnAddFolder();
	else if (cmd == ID_MP_FOLDER_SYNC) OnFolderSyncDiff();
}

void CMediaPlayerDlg::ShowEqButtonExtrasMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.AddCommand(ID_MP_OPEN_EQ,
		LL14(L"イコライザを開く", L"Open equalizer", L"Ouvrir l'egaliseur", L"Apri equalizzatore", L"Abrir ecualizador",
			L"이퀄라이저 열기", L"打开均衡器", L"فتح المعادل", L"Открыть эквалайзер", L"Equalizer öffnen",
			L"Abrir equalizador", L"Equalizer openen", L"Otworz equalizer", L"Equalizeri ac"),
			LL14(L"イコライザーウィンドウを開閉します", L"Open or close the equalizer window", L"Ouvrir/fermer la fenetre egaliseur", L"Apri/chiudi la finestra equalizzatore", L"Abrir/cerrar la ventana del ecualizador", L"이퀄라이저 창을 여닫기", L"打开或关闭均衡器窗口", L"فتح/إغلاق نافذة المعادل", L"Открыть/закрыть окно эквалайзера", L"Equalizer-Fenster oeffnen/schliessen", L"Abrir/fechar a janela do equalizador", L"Equalizervenster openen/sluiten", L"Otworz/zamknij okno equalizera", L"Equalizer penceresini ac/kapat"));
	menu.AddCommand(ID_MP_EQBTN_FLAT,
		LL14(L"デフォルト（フラット）", L"Default (flat)", L"Par defaut (plat)", L"Predefinito (piatto)", L"Predeterminado (plano)",
			L"기본(플랫)", L"默认（平直）", L"افتراضي (مسطح)", L"По умолчанию (ровный)", L"Standard (flach)",
			L"Padrao (plano)", L"Standaard (vlak)", L"Domyslny (plaski)", L"Varsayilan (duz)"),
			LL14(L"EQ をフラット（効果なし）に戻します", L"Reset EQ to flat (no boost/cut)", L"Remettre l'EQ a plat", L"Ripristina EQ piatto", L"Restablecer el EQ a plano", L"EQ를 플랫(효과 없음)으로 되돌림", L"将 EQ 重置为平直", L"إعادة EQ إلى المستوى المسطح", L"Сбросить EQ в ровный", L"EQ auf flach zurucksetzen", L"Redefinir o EQ para plano", L"EQ terugzetten naar vlak", L"Przywroc EQ do plaskiego", L"EQ'yu duz (etkisiz) yap"));
	menu.AddCommand(ID_MP_EQBTN_SUGGEST,
		LL14(L"キーからEQを提案", L"Suggest EQ from key", L"Suggérer EQ depuis la tonalité", L"Suggerisci EQ dalla tonalità", L"Sugerir EQ desde tonalidad",
			L"키에서 EQ 제안", L"根据调性建议 EQ", L"اقتراح EQ من المفتاح", L"Предложить EQ по тональности", L"EQ aus Tonart vorschlagen",
			L"Sugerir EQ pela tonalidade", L"EQ voorstellen uit toonsoort", L"Zaproponuj EQ z tonacji", L"Anahtardan EQ oner"),
			LL14(L"検出キーに基づく EQ カーブを提案します", L"Suggest an EQ curve from the detected key", L"Suggérer une courbe EQ selon la tonalite", L"Suggerisci una curva EQ dalla tonalita", L"Sugerir una curva EQ segun la tonalidad", L"감지된 키 기반 EQ 커브 제안", L"根据检测到的调性建议 EQ 曲线", L"اقتراح منحنى EQ حسب المفتاح", L"Предложить кривую EQ по тональности", L"EQ-Kurve aus erkannter Tonart vorschlagen", L"Sugerir uma curva EQ pela tonalidade", L"EQ-curve voorstellen uit gedetecteerde toonsoort", L"Zaproponuj krzywa EQ z tonacji", L"Algilanan anahtara gore EQ egrisi oner"));
	menu.AddCheck(ID_MP_EQBTN_AUTO,
		LL14(L"キー検出時に自動提案", L"Auto-suggest on key detect", L"Suggestion auto sur détection", L"Suggerimento auto su rilevamento", L"Sugerencia auto al detectar",
			L"키 검출 시 자동 제안", L"检测到调性时自动建议", L"اقتراح تلقائي عند الكشف", L"Авто-предложение по ключу", L"Auto-Vorschlag bei Erkennung",
			L"Sugestao auto na deteccao", L"Auto-voorstel bij detectie", L"Auto-propozycja przy wykryciu", L"Algilamada otomatik oneri"),
		savedata.mpKeyEqSuggest != 0,
		LL14(L"キー検出時に EQ 提案を自動適用します", L"Auto-apply EQ suggestions when a key is detected", L"Appliquer auto les suggestions EQ a la detection", L"Applica auto i suggerimenti EQ al rilevamento", L"Aplicar auto sugerencias EQ al detectar", L"키 검출 시 EQ 제안을 자동 적용", L"检测到调性时自动应用 EQ 建议", L"تطبيق اقتراحات EQ تلقائياً عند الكشف", L"Автоприменять предложения EQ при детекции", L"EQ-Vorschlage bei Tonarterkennung automatisch anwenden", L"Aplicar auto sugestoes de EQ na deteccao", L"EQ-voorstellen automatisch toepassen bij detectie", L"Auto-stosuj propozycje EQ przy wykryciu", L"Anahtar algilaninca EQ onerisini otomatik uygula"));
	const UINT cmd = menu.Track(screenPt, this);
	if (cmd == ID_MP_OPEN_EQ) {
		OnEq();
	} else if (cmd == ID_MP_EQBTN_FLAT) {
		savedata.eqsoundeq = 0;
		equaliser(0, 0, 2);
		MpPersistSavedataQuick();
	} else if (cmd == ID_MP_EQBTN_SUGGEST) {
		OnEq();
	} else if (cmd == ID_MP_EQBTN_AUTO) {
		savedata.mpKeyEqSuggest = savedata.mpKeyEqSuggest ? 0 : 1;
		MpPersistSavedataQuick();
	}
}

void CMediaPlayerDlg::ShowFadeExtrasMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.AddCommand(ID_MP_FADE_NOW,
		LL14(L"今すぐフェードアウト", L"Fade out now", L"Fondu maintenant", L"Dissolvenza ora", L"Desvanecer ahora",
			L"지금 페이드 아웃", L"立即淡出", L"تلاشي الآن", L"Затухание сейчас", L"Jetzt ausblenden",
			L"Desvanecer agora", L"Nu uitfaden", L"Wycisz teraz", L"Simdi soluklastir"),
			LL14(L"いま再生中の曲をすぐにフェードアウトします", L"Fade out the current track immediately", L"Faire un fondu immediat de la piste", L"Esegui dissolvenza immediata del brano", L"Desvanecer inmediatamente la pista actual", L"현재 곡을 바로 페이드 아웃", L"立即淡出当前曲目", L"تلاشي المقطع الحالي فوراً", L"Сразу затушить текущий трек", L"Aktuellen Titel sofort ausblenden", L"Desvanecer a faixa atual imediatamente", L"Huidige nummer meteen uitfaden", L"Wycisz biezacy utwor natychmiast", L"Calan parcayi hemen soluklastir"));
	menu.AddCheck(ID_MP_XFADE_PREVIEW,
		LL14(L"クロスフェード帯をシークに表示", L"Show crossfade band on seek", L"Afficher bande xfade sur seek", L"Mostra banda xfade sul seek", L"Mostrar banda xfade en seek",
			L"시크에 크로스페이드 대역 표시", L"在进度条显示交叉淡入淡出带", L"إظهار نطاق xfade على الشريط", L"Показывать полосу xfade на сике", L"Xfade-Band auf Seek zeigen",
			L"Mostrar faixa xfade no seek", L"Xfade-band op seek tonen", L"Pokaz pasmo xfade na seek", L"Seekte xfade bandini goster"),
		savedata.mpXfadePreview != 0,
		LL14(L"書き出しクロスフェードの重なり帯をシークに表示します", L"Show the export crossfade overlap band on the seek bar", L"Afficher la bande de chevauchement xfade sur la barre", L"Mostra la banda di sovrapposizione xfade sulla barra", L"Mostrar la banda de solape xfade en la barra", L"내보내기 크로스페이드 겹침을 시크바에 표시", L"在进度条显示导出交叉淡化重叠带", L"عرض شريط تداخل xfade على الشريط", L"Показать полосу перекрытия xfade на сике", L"Export-Xfade-Uberlappung auf der Suchleiste zeigen", L"Mostrar a faixa de sobreposicao xfade na barra", L"Export-xfade-overlap op de zoekbalk tonen", L"Pokaz pasmo nakladania xfade na pasku", L"Disa aktarma xfade bindirme bandini seekte goster"));
	const UINT cmd = menu.Track(screenPt, this);
	if (cmd == ID_MP_FADE_NOW) OnFadeout();
	else if (cmd == ID_MP_XFADE_PREVIEW) OnXfadePreviewToggle();
}

void CMediaPlayerDlg::ShowPlayModeExtrasMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	const BOOL cont = m_renzoku.GetSafeHwnd() ? (m_renzoku.GetCheck() != 0) : FALSE;
	const BOOL loop = m_loop.GetSafeHwnd() ? (m_loop.GetCheck() != 0) : FALSE;
	const BOOL rnd = m_random.GetSafeHwnd() ? (m_random.GetCheck() != 0) : FALSE;
	menu.AddCheck(ID_MP_MODE_CONT,
		LL14(L"連続再生", L"Continuous", L"Lecture continue", L"Continua", L"Continua",
			L"연속 재생", L"连续播放", L"تشغيل متتابع", L"Подряд", L"Folge",
			L"Continuo", L"Doorlopend", L"Ciagle", L"Surekli"),
		cont,
		LL14(L"リストを最後まで連続再生します", L"Play through the list continuously", L"Lire la liste en continu", L"Riproduci l'elenco in continuo", L"Reproducir la lista de forma continua", L"목록을 끝까지 연속 재생", L"连续播放列表直到结束", L"تشغيل القائمة بشكل متتابع", L"Воспроизводить список подряд", L"Liste durchgehend abspielen", L"Reproduzir a lista continuamente", L"Lijst doorlopend afspelen", L"Odtwarzaj liste ciagle", L"Listeyi surekli cal"));
	menu.AddCheck(ID_MP_MODE_LOOP,
		LL14(L"ループ再生", L"Loop", L"Boucle", L"Loop", L"Repetir",
			L"루프", L"循环", L"تكرار", L"Цикл", L"Schleife",
			L"Repetir", L"Lus", L"Petla", L"Dongu"),
		loop,
		LL14(L"現在の曲または選択区間をループ再生します", L"Loop the current track or selected range", L"Boucler la piste ou la plage selectionnee", L"Ripeti il brano o l'intervallo selezionato", L"Repetir la pista o el rango seleccionado", L"현재 곡 또는 선택 구간을 루프", L"循环当前曲目或所选区间", L"تكرار المقطع أو النطاق المحدد", L"Зациклить текущий трек или диапазон", L"Aktuellen Titel oder Bereich in Schleife", L"Repetir a faixa ou o intervalo selecionado", L"Huidige nummer of bereik herhalen", L"Zapetl biezacy utwor lub zakres", L"Gecerli parcayi veya araligi donguye al"));
	menu.AddCheck(ID_MP_MODE_RAND,
		LL14(L"ランダム再生", L"Random", L"Aleatoire", L"Casuale", L"Aleatorio",
			L"랜덤", L"随机", L"عشوائي", L"Случайно", L"Zufall",
			L"Aleatorio", L"Willekeurig", L"Losowo", L"Rastgele"),
		rnd,
		LL14(L"リストをランダム順で再生します", L"Play the list in random order", L"Lire la liste dans un ordre aleatoire", L"Riproduci l'elenco in ordine casuale", L"Reproducir la lista en orden aleatorio", L"목록을 무작위 순서로 재생", L"按随机顺序播放列表", L"تشغيل القائمة بترتيب عشوائي", L"Воспроизводить список в случайном порядке", L"Liste in Zufallsreihenfolge abspielen", L"Reproduzir a lista em ordem aleatoria", L"Lijst in willekeurige volgorde afspelen", L"Odtwarzaj liste losowo", L"Listeyi rastgele sirada cal"));
	const BOOL xf = savedata.play_xfade != 0;
	menu.AddCheck(ID_MP_MODE_XFADE,
		LL14(L"クロスフェード", L"Crossfade", L"Fondu croise", L"Crossfade", L"Fundido cruzado",
			L"크로스페이드", L"交叉淡化", L"تلاشي متقاطع", L"Кроссфейд", L"Crossfade",
			L"Crossfade", L"Crossfade", L"Przenikanie", L"Capraz gechis"),
		xf,
		LL14(L"連続再生時、曲のつなぎでフェードアウト／インします", L"During continuous play, fade out/in between tracks", L"En lecture continue, enchainement en fondu", L"In riproduzione continua, dissolve tra brani", L"En reproduccion continua, fundido entre pistas", L"연속 재생 시 곡 전환에 페이드", L"连续播放时曲间淡入淡出", L"تلاشي عند الانتقال أثناء التشغيل المتتابع", L"При непрерывном воспроизведении — кроссфейд", L"Bei Dauerwiedergabe zwischen Titeln überblenden", L"Na reproducao continua, crossfade entre faixas", L"Bij doorlopend afspelen crossfaden tussen nummers", L"Przy ciaglym odtwarzaniu przenikanie miedzy utworami", L"Surekli calmada parcilar arasi crossfade"));
	const UINT cmd = menu.Track(screenPt, this);
	if (cmd == ID_MP_MODE_CONT) {
		if (m_renzoku.GetSafeHwnd()) { m_renzoku.SetCheck(cont ? BST_UNCHECKED : BST_CHECKED); OnRenzoku(); }
	} else if (cmd == ID_MP_MODE_LOOP) {
		if (m_loop.GetSafeHwnd()) { m_loop.SetCheck(loop ? BST_UNCHECKED : BST_CHECKED); OnLoop(); }
	} else if (cmd == ID_MP_MODE_RAND) {
		if (m_random.GetSafeHwnd()) { m_random.SetCheck(rnd ? BST_UNCHECKED : BST_CHECKED); OnRandom(); }
	} else if (cmd == ID_MP_MODE_XFADE) {
		if (m_xfade.GetSafeHwnd()) { m_xfade.SetCheck(xf ? BST_UNCHECKED : BST_CHECKED); OnPlayXfade(); }
	}
}

void CMediaPlayerDlg::ShowMirrorExtrasMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	menu.AddCheck(ID_MP_MIRROR_TOGGLE,
		LL14(L"ミラー出力を有効", L"Enable mirror output", L"Activer sortie miroir", L"Abilita uscita mirror", L"Activar salida espejo",
			L"미러 출력 사용", L"启用镜像输出", L"تفعيل خرج المرآة", L"Включить зеркальный выход", L"Spiegelausgabe aktivieren",
			L"Ativar saida espelho", L"Spiegelaudio inschakelen", L"Wlacz wyjscie lustrzane", L"Ayna cikisi etkin"),
		savedata.mpMirrorOut != 0,
		LL14(L"別デバイスへのミラー音声出力を有効／無効にします", L"Enable/disable mirrored audio to another device", L"Activer/desactiver la sortie miroir", L"Abilita/disabilita l'uscita mirror", L"Activar/desactivar la salida espejo", L"다른 장치로의 미러 출력을 켜거나 끔", L"启用/禁用镜像到另一设备的音频输出", L"تفعيل/تعطيل خرج المرآة لجهاز آخر", L"Вкл/выкл зеркальный вывод на другое устройство", L"Spiegelausgabe auf anderes Gerat ein/aus", L"Ativar/desativar saida espelho para outro dispositivo", L"Spiegelaudio naar ander apparaat aan/uit", L"Wlacz/wylacz wyjscie lustrzane na inne urzadzenie", L"Baska cihaza ayna cikisini ac/kapat"));
	menu.AddCommand(ID_MP_MIRROR_OPEN,
		LL14(L"ミラー設定を開く…", L"Open mirror settings…", L"Ouvrir reglages miroir…", L"Apri impostazioni mirror…", L"Abrir ajustes espejo…",
			L"미러 설정 열기…", L"打开镜像设置…", L"فتح إعدادات المرآة…", L"Открыть настройки зеркала…", L"Spiegeleinstellungen öffnen…",
			L"Abrir config. espelho…", L"Spiegelinstellingen openen…", L"Otworz ustawienia lustra…", L"Ayna ayarlarini ac…"),
			LL14(L"ミラー出力デバイスや遅延などの設定を開きます", L"Open mirror output device and latency settings", L"Ouvrir les reglages du miroir (peripherique/latence)", L"Apri impostazioni mirror (dispositivo/latenza)", L"Abrir ajustes de espejo (dispositivo/latencia)", L"미러 출력 장치·지연 설정을 엽니다", L"打开镜像输出设备与延迟设置", L"فتح إعدادات جهاز المرآة والتأخير", L"Открыть настройки зеркала (устройство/задержка)", L"Spiegeleinstellungen (Gerat/Latenz) oeffnen", L"Abrir configuracoes do espelho (dispositivo/latencia)", L"Spiegelinstellingen (apparaat/latentie) openen", L"Otworz ustawienia lustra (urzadzenie/opoznienie)", L"Ayna cikis cihaz/gecikme ayarlarini ac"));
	const UINT cmd = menu.Track(screenPt, this);
	if (cmd == ID_MP_MIRROR_TOGGLE) {
		savedata.mpMirrorOut = savedata.mpMirrorOut ? 0 : 1;
		MpPersistSavedataQuick();
		MpMirrorOnFormatReady();
	} else if (cmd == ID_MP_MIRROR_OPEN) {
		OnMpMirror();
	}
}

void CMediaPlayerDlg::ShowToolsExtrasMenu(CPoint screenPt)
{
	CCustomPopupMenu menu;
	menu.SetAeroMode(FALSE);
	MpLrcSliderCtx lrcCtx;
	lrcCtx.dlg = this;
	lrcCtx.lastMs = 0;
	lrcCtx.pendingMs = 0;

	menu.AddCheck(ID_MP_TOOLS_PANEL,
		LL14(L"並べ替え・フォルダ追加パネル", L"Sort / add-folder panel", L"Panneau tri / dossier", L"Pannello ordina / cartella", L"Panel ordenar / carpeta", L"정렬/폴더 추가 패널", L"排序/添加文件夹面板", L"لوحة الترتيب/المجلد", L"Панель сортировки/папки", L"Sortier-/Ordnerpanel", L"Painel ordenar/pasta", L"Sorteer-/mappaneel", L"Panel sortowania/folderu", L"Sirala/klasor paneli"),
		savedata.mpToolsOpen != 0,
		LL14(L"並べ替えやフォルダ追加などのツールパネルを開閉します", L"Show/hide the sort and add-folder tools panel", L"Afficher/masquer le panneau tri/dossier", L"Mostra/nascondi il pannello ordina/cartella", L"Mostrar/ocultar el panel ordenar/carpeta", L"정렬·폴더 추가 도구 패널을 여닫기", L"打开/关闭排序与添加文件夹工具面板", L"إظهار/إخفاء لوحة الترتيب وإضافة المجلد", L"Показать/скрыть панель сортировки/папки", L"Sortier-/Ordnerpanel ein-/ausblenden", L"Mostrar/ocultar o painel ordenar/pasta", L"Sorteer-/mappaneel tonen/verbergen", L"Pokaz/ukryj panel sortowania/folderu", L"Sirala/klasor arac panelini ac/kapat"));
	{
		if (!savedata.mpBotToolsInited) {
			savedata.mpBotToolsInited = 1;
			savedata.mpBotToolsFlags = 0x0F;
		}
		const int bf = savedata.mpBotToolsFlags;
		CCustomPopupMenu* botSub = menu.AddSubMenu(
			LL14(L"底バーのツールボタン", L"Bottom bar tool buttons", L"Boutons outils bas", L"Pulsanti strumenti in basso", L"Botones de herramientas abajo",
				L"하단 도구 버튼", L"底栏工具按钮", L"أزرار أدوات الشريط السفلي", L"Кнопки панели снизу", L"Werkzeugtasten unten",
				L"Botoes de ferramentas embaixo", L"Werkknoppen onderaan", L"Przyciski narzedzi na dole", L"Alt cubuk arac dugmeleri"),
			LL14(L"キャプチャ右の空きに出すショートカットの表示切替", L"Show/hide shortcuts to the right of Capture", L"Afficher/masquer les raccourcis a droite de Capture", L"Mostra/nascondi scorciatoie a destra di Cattura", L"Mostrar/ocultar atajos a la derecha de Captura",
				L"캡처 오른쪽 단축 버튼 표시", L"捕获右侧快捷按钮显示", L"إظهار/إخفاء اختصارات يمين الالتقاط", L"Показ ярлыков справа от Захвата", L"Shortcuts rechts von Capture ein/aus",
				L"Mostrar/ocultar atalhos a direita de Captura", L"Snelkoppelingen rechts van Capture", L"Skroty na prawo od Przechwytu", L"Yakala sagindaki kisayollar"));
		if (botSub) {
			botSub->AddCheck(ID_MP_BOTVIS_DJ, LL14(L"DJパッド", L"DJ Pad", L"Pad DJ", L"Pad DJ", L"Pad DJ", L"DJ 패드", L"DJ 垫", L"لوحة DJ", L"DJ-панель", L"DJ-Pad", L"Pad DJ", L"DJ-pad", L"Pad DJ", L"DJ paneli"), (bf & 1) != 0);
			botSub->AddCheck(ID_MP_BOTVIS_TAG, LL14(L"タグ編集", L"Edit tags", L"Editer tags", L"Modifica tag", L"Editar etiquetas", L"태그 편집", L"编辑标签", L"تحرير الوسوم", L"Правка тегов", L"Tags bearbeiten", L"Editar tags", L"Tags bewerken", L"Edytuj tagi", L"Etiket duzenle"), (bf & 2) != 0);
			botSub->AddCheck(ID_MP_BOTVIS_BPM, L"BPM", (bf & 4) != 0);
			botSub->AddCheck(ID_MP_BOTVIS_SLEEP, LL14(L"スリープ", L"Sleep", L"Veille", L"Sleep", L"Sueño", L"슬립", L"睡眠", L"نوم", L"Сон", L"Schlaf", L"Sono", L"Slaap", L"Sen", L"Uyku"), (bf & 8) != 0);
			botSub->AddCheck(ID_MP_BOTVIS_MIRROR, LL14(L"ミラー", L"Mirror", L"Miroir", L"Mirror", L"Espejo", L"미러", L"镜像", L"مرآة", L"Зеркало", L"Spiegel", L"Espelho", L"Spiegel", L"Lustro", L"Ayna"), (bf & 16) != 0);
			botSub->AddCheck(ID_MP_BOTVIS_SSVIZ, LL14(L"SS ビジュアライザ", L"SS visualizer", L"Visualiseur SS", L"Visualizzatore SS", L"Visualizador SS", L"SS 비주얼", L"SS 可视化", L"عارض SS", L"SS-визуализатор", L"SS-Visualizer", L"Visual SS", L"SS-visualizer", L"Wizual SS", L"SS gorsel"), (bf & 32) != 0);
			botSub->AddCheck(ID_MP_BOTVIS_ALARM, LL14(L"アラーム", L"Alarm", L"Alarme", L"Sveglia", L"Alarma", L"알람", L"闹钟", L"منبه", L"Будильник", L"Wecker", L"Alarme", L"Wekker", L"Budzik", L"Alarm"), (bf & 64) != 0);
			botSub->AddCheck(ID_MP_BOTVIS_REMOTE, LL14(L"リモート", L"Remote", L"Remote", L"Remote", L"Remoto", L"리모트", L"遥控", L"تحكم", L"Пульт", L"Remote", L"Remoto", L"Remote", L"Pilot", L"Uzaktan"), (bf & 128) != 0);
			botSub->AddCheck(ID_MP_BOTVIS_MAZE, LL14(L"迷路", L"Maze", L"Labyrinthe", L"Labirinto", L"Laberinto", L"미로", L"迷宫", L"متاهة", L"Лабиринт", L"Labyrinth", L"Labirinto", L"Doolhof", L"Labirynt", L"Labirent"), (bf & 256) != 0);
		}
	}
	menu.AddCommand(ID_MP_MISS_MANAGE,
		LL14(L"欠損を整理…", L"Manage missing…", L"Gerer manquants…", L"Gestisci mancanti…", L"Gestionar faltantes…", L"결손 정리…", L"整理缺失…", L"إدارة المفقود…", L"Управление отсутствующими…", L"Fehlende verwalten…", L"Gerir ausentes…", L"Ontbrekende beheren…", L"Zarzadzaj brakujacymi…", L"Eksikleri yonnet…"),
		LL14(L"見つからないファイルを一覧し整理します", L"List and clean up missing files", L"Lister et nettoyer les fichiers manquants", L"Elenca e sistema i file mancanti", L"Listar y limpiar archivos faltantes", L"없는 파일을 목록으로 정리", L"列出并整理缺失文件", L"سرد وتنظيف الملفات المفقودة", L"Показать и упорядочить отсутствующие файлы", L"Fehlende Dateien auflisten und bereinigen", L"Listar e limpar arquivos ausentes", L"Ontbrekende bestanden tonen en opruimen", L"Wyswietl i uporzadkuj brakujace pliki", L"Eksik dosyalari listele ve duzenle"));
	menu.AddSeparator();
	menu.AddCommand(ID_MP_SMART_EDIT,
		LL14(L"スマートプレイリスト…", L"Smart playlists…", L"Playlists intelligentes…", L"Playlist smart…", L"Listas inteligentes…", L"스마트 재생목록…", L"智能播放列表…", L"قوائم ذكية…", L"Умные списки…", L"Smart-Playlists…", L"Playlists inteligentes…", L"Slimme afspeellijsten…", L"Inteligentne listy…", L"Akilli listeler…"),
		LL14(L"条件付きスマートプレイリストを作成・編集します", L"Create or edit conditional smart playlists", L"Creer/editer des playlists intelligentes", L"Crea/modifica playlist smart", L"Crear/editar listas inteligentes", L"조건 기반 스마트 재생목록 만들기/편집", L"创建或编辑条件智能播放列表", L"إنشاء/تحرير قوائم ذكية", L"Создать/править умные списки", L"Smart-Playlists erstellen/bearbeiten", L"Criar/editar playlists inteligentes", L"Slimme afspeellijsten maken/bewerken", L"Tworz/edytuj inteligentne listy", L"Kosullu akilli listeleri olustur/duzenle"));
	for (int si = 0; si < MpSmart_Count() && si < MP_SMART_MAX; ++si) {
		MpSmartRule r;
		if (!MpSmart_Get(si, r)) continue;
		menu.AddCheck(ID_MP_SMART_BASE + si, MpSmart_UiLabel(r), m_activeSmartId == si, LL14(L"このスマートプレイリストを適用／解除します", L"Apply or clear this smart playlist", L"Appliquer/effacer cette playlist intelligente", L"Applica/cancella questa playlist smart", L"Aplicar/quitar esta lista inteligente", L"이 스마트 재생목록을 적용/해제", L"应用或清除此智能播放列表", L"تطبيق/مسح هذه القائمة الذكية", L"Применить/сбросить этот умный список", L"Diese Smart-Playlist anwenden/entfernen", L"Aplicar/limpar esta playlist inteligente", L"Deze slimme afspeellijst toepassen/wissen", L"Zastosuj/wyczysc te inteligentna liste", L"Bu akilli listeyi uygula/temizle"));
	}
	menu.AddSeparator();
	// 旧「クイック: 未再生/欠損」は既定スマートPLと同義のため出さない
	menu.AddCommand(ID_MP_FILT_CLEAR,
		LL14(L"フィルタ解除", L"Clear filter", L"Effacer filtre", L"Cancella filtro", L"Borrar filtro", L"필터 해제", L"清除筛选", L"مسح التصفية", L"Сбросить фильтр", L"Filter aus", L"Limpar filtro", L"Filter wissen", L"Wyczysc filtr", L"Filtreyi temizle"),
		LL14(L"リストにかかっているフィルタをすべて解除します", L"Clear all active list filters", L"Effacer tous les filtres actifs", L"Cancella tutti i filtri attivi", L"Borrar todos los filtros activos", L"적용 중인 필터를 모두 해제", L"清除所有活动列表筛选", L"مسح كل عوامل التصفية النشطة", L"Сбросить все активные фильтры", L"Alle aktiven Filter entfernen", L"Limpar todos os filtros ativos", L"Alle actieve filters wissen", L"Wyczysc wszystkie aktywne filtry", L"Tum etkin filtreleri temizle"));
	menu.AddSeparator();
	menu.AddCommand(ID_MP_QUEUE_SHOW,
		LL14(L"Up Next を表示…", L"Show Up Next…", L"Afficher Up Next…", L"Mostra Up Next…", L"Mostrar Up Next…", L"Up Next 표시…", L"显示 Up Next…", L"عرض Up Next…", L"Показать Up Next…", L"Up Next anzeigen…", L"Mostrar Up Next…", L"Up Next tonen…", L"Pokaz Up Next…", L"Up Next goster…"),
		LL14(L"Up Next（次に再生するキュー）を表示します", L"Show the Up Next playback queue", L"Afficher la file Up Next", L"Mostra la coda Up Next", L"Mostrar la cola Up Next", L"Up Next 재생 큐를 표시", L"显示 Up Next 播放队列", L"عرض طابور Up Next", L"Показать очередь Up Next", L"Up-Next-Warteschlange anzeigen", L"Mostrar a fila Up Next", L"Up Next-wachtrij tonen", L"Pokaz kolejke Up Next", L"Up Next kuyrugunu goster"));
	menu.AddCommand(ID_MP_QUEUE_ADD,
		LL14(L"Up Next に追加", L"Add to Up Next", L"Ajouter a Up Next", L"Aggiungi a Up Next", L"Anadir a Up Next", L"Up Next에 추가", L"加入 Up Next", L"إضافة إلى Up Next", L"В Up Next", L"Zu Up Next", L"Adicionar a Up Next", L"Toevoegen aan Up Next", L"Dodaj do Up Next", L"Up Next'e ekle"),
		LL14(L"選択曲を Up Next の末尾に追加します", L"Append the selection to the end of Up Next", L"Ajouter la selection a la fin de Up Next", L"Aggiungi la selezione in coda a Up Next", L"Anadir la seleccion al final de Up Next", L"선택 곡을 Up Next 끝에 추가", L"将所选追加到 Up Next 末尾", L"إضافة التحديد إلى نهاية Up Next", L"Добавить выбор в конец Up Next", L"Auswahl ans Ende von Up Next anhangen", L"Acrescentar a selecao ao fim de Up Next", L"Selectie aan het einde van Up Next toevoegen", L"Dodaj wybor na koniec Up Next", L"Secimi Up Next sonuna ekle"));
	menu.AddCommand(ID_MP_QUEUE_PLAYNEXT,
		LL14(L"次に再生", L"Play Next", L"Lire ensuite", L"Riproduci dopo", L"Reproducir despues", L"다음에 재생", L"下一首播放", L"تشغيل التالي", L"Играть следующим", L"Als Nachstes", L"Tocar a seguir", L"Speel hierna", L"Odtworz nastepnie", L"Sonraki oynat"),
		LL14(L"選択曲を次に再生する位置へ挿入します", L"Insert the selection to play next", L"Inserer la selection pour lire ensuite", L"Inserisci la selezione da riprodurre dopo", L"Insertar la seleccion para reproducir despues", L"선택 곡을 다음에 재생되도록 삽입", L"将所选插入为下一首播放", L"إدراج التحديد للتشغيل التالي", L"Вставить выбор следующим", L"Auswahl als Nachstes einfugen", L"Inserir a selecao para tocar a seguir", L"Selectie als volgende invoegen", L"Wstaw wybor jako nastepny", L"Secimi sonraki olarak ekle"));
	menu.AddCommand(ID_MP_QUEUE_CLEAR,
		LL14(L"キューをクリア", L"Clear Queue", L"Vider la file", L"Svuota coda", L"Vaciar cola", L"큐 비우기", L"清空队列", L"مسح الطابور", L"Очистить очередь", L"Warteschlange leeren", L"Limpar fila", L"Wachtrij wissen", L"Wyczysc kolejke", L"Kuyrugu temizle"),
		LL14(L"Up Next キューを空にします", L"Clear the entire Up Next queue", L"Vider toute la file Up Next", L"Svuota tutta la coda Up Next", L"Vaciar toda la cola Up Next", L"Up Next 큐를 비웁니다", L"清空整个 Up Next 队列", L"مسح طابور Up Next بالكامل", L"Очистить всю очередь Up Next", L"Gesamte Up-Next-Warteschlange leeren", L"Limpar toda a fila Up Next", L"Hele Up Next-wachtrij wissen", L"Wyczysc cala kolejke Up Next", L"Tum Up Next kuyrugunu temizle"));
	menu.AddSeparator();
	menu.AddCommand(ID_MP_DUPES,
		LL14(L"重複を検出…", L"Find duplicates…", L"Trouver doublons…", L"Trova duplicati…", L"Buscar duplicados…", L"중복 찾기…", L"查找重复…", L"البحث عن مكررات…", L"Найти дубликаты…", L"Duplikate finden…", L"Localizar duplicatas…", L"Duplicaten zoeken…", L"Znajdz duplikaty…", L"Yinelenenleri bul…"),
		LL14(L"ライブラリ内の重複曲を検出します", L"Find duplicate tracks in the library", L"Trouver les doublons dans la bibliotheque", L"Trova i duplicati nella libreria", L"Buscar duplicados en la biblioteca", L"라이브러리에서 중복 곡을 찾습니다", L"在媒体库中查找重复曲目", L"البحث عن مكررات في المكتبة", L"Найти дубликаты в библиотеке", L"Duplikate in der Bibliothek finden", L"Localizar duplicatas na biblioteca", L"Duplicaten in de bibliotheek zoeken", L"Znajdz duplikaty w bibliotece", L"Kitaplikta yinelenenleri bul"));
	menu.AddCommand(ID_MP_FOLDER_SYNC,
		LL14(L"フォルダ同期差分…", L"Folder sync diff…", L"Diff sync dossier…", L"Diff sync cartella…", L"Diff sync carpeta…", L"폴더 동기화 차이…", L"文件夹同步差异…", L"فرق مزامنة المجلد…", L"Разница синхр. папки…", L"Ordner-Sync-Diff…", L"Diff sync pasta…", L"Map-sync-diff…", L"Roznica sync folderu…", L"Klasor senkron fark…"),
		LL14(L"監視フォルダとリストの差分を確認・同期します", L"Review and sync differences vs watched folders", L"Verifier/synchroniser les differences des dossiers", L"Controlla/sincronizza le differenze delle cartelle", L"Revisar/sincronizar diferencias de carpetas", L"감시 폴더와 목록 차이를 확인하고 동기화", L"查看并同步监视文件夹与列表的差异", L"مراجعة ومزامنة فروق المجلدات", L"Проверить и синхронизировать различия папок", L"Differenzen zu Uberwachungsordnern prufen/synchronisieren", L"Revisar e sincronizar diferencas das pastas", L"Mapverschillen controleren/synchroniseren", L"Sprawdz i zsynchronizuj roznice folderow", L"Klasor farklarini incele ve senkronize et"));
	menu.AddSeparator();
	menu.AddSlider(
		LL14(L"LRC オフセット (ms)", L"LRC offset (ms)", L"Decalage LRC (ms)", L"Offset LRC (ms)",
			L"Desfase LRC (ms)", L"LRC 오프셋 (ms)", L"LRC 偏移 (ms)", L"إزاحة LRC (ms)",
			L"Смещение LRC (ms)", L"LRC-Versatz (ms)", L"Offset LRC (ms)", L"LRC-offset (ms)",
			L"Przesuniecie LRC (ms)", L"LRC ofset (ms)"),
		-500, 500, 0, MpLrcOffsetSliderCb, &lrcCtx,
		LL14(L"歌詞タイミングを ms 単位でずらす（ドラッグ中に反映）", L"Shift lyric timing in ms (live)",
			L"Decaler le timing (ms, direct)", L"Sposta timing (ms, live)",
			L"Desplazar timing (ms, en vivo)", L"가사 타이밍 ms 이동 (즉시)", L"歌词时间偏移 ms（即时）",
			L"إزاحة توقيت الكلمات بالميلي ثانية (مباشر)", L"Сдвиг текста в мс (сразу)",
			L"Text-Timing in ms (live)", L"Deslocar timing em ms (ao vivo)",
			L"Teksttiming in ms (live)", L"Przesun timing w ms (na zywo)", L"Soz zamanini ms kaydir (anlik)"));
	{
		CCustomPopupMenu* lrcSub = menu.AddSubMenu(
			LL14(L"LRC 微調整", L"LRC fine adjust", L"Reglage fin LRC", L"Regolazione fine LRC",
				L"Ajuste fino LRC", L"LRC 미세 조정", L"LRC 微调", L"ضبط دقيق LRC",
				L"Тонкая настройка LRC", L"LRC Feineinstellung", L"Ajuste fino LRC", L"LRC fijnafstellen",
				L"Dostrojenie LRC", L"LRC ince ayar"),
				LL14(L"歌詞タイミングを ±10/50/100 ms 単位でずらします", L"Nudge lyric timing by ±10/50/100 ms", L"Decaler le timing par ±10/50/100 ms", L"Sposta il timing di ±10/50/100 ms", L"Desplazar el timing ±10/50/100 ms", L"가사 타이밍을 ±10/50/100 ms 단위로 이동", L"按 ±10/50/100 ms 微调歌词时间", L"إزاحة توقيت الكلمات بمقدار ±10/50/100 مللي ثانية", L"Сдвинуть тайминг текста на ±10/50/100 мс", L"Text-Timing um ±10/50/100 ms verschieben", L"Deslocar o timing da letra em ±10/50/100 ms", L"Teksttiming met ±10/50/100 ms verschuiven", L"Przesun timing tekstu o ±10/50/100 ms", L"Soz zamanlamasini ±10/50/100 ms kaydir"));
		if (lrcSub) {
			lrcSub->AddCommand(ID_MP_LRC_MINUS100, L"-100 ms");
			lrcSub->AddCommand(ID_MP_LRC_MINUS50, L"-50 ms");
			lrcSub->AddCommand(ID_MP_LRC_MINUS10, L"-10 ms");
			lrcSub->AddCommand(ID_MP_LRC_PLUS10, L"+10 ms");
			lrcSub->AddCommand(ID_MP_LRC_PLUS50, L"+50 ms");
			lrcSub->AddCommand(ID_MP_LRC_PLUS100, L"+100 ms");
		}
	}
	menu.AddCommand(ID_MP_LRC_SAVE,
		LL14(L"LRC を保存…", L"Save LRC…", L"Enregistrer LRC…", L"Salva LRC…", L"Guardar LRC…", L"LRC 저장…", L"保存 LRC…", L"حفظ LRC…", L"Сохранить LRC…", L"LRC speichern…", L"Salvar LRC…", L"LRC opslaan…", L"Zapisz LRC…", L"LRC kaydet…"),
		LL14(L"調整した歌詞タイミングを LRC ファイルに保存します", L"Save adjusted lyric timing to an LRC file", L"Enregistrer le timing ajuste dans un LRC", L"Salva il timing regolato in un file LRC", L"Guardar el timing ajustado en un LRC", L"조정한 가사 타이밍을 LRC로 저장", L"将调整后的歌词时间保存为 LRC", L"حفظ توقيت الكلمات المضبوط في ملف LRC", L"Сохранить скорректированный тайминг в LRC", L"Angepasstes Text-Timing als LRC speichern", L"Salvar o timing ajustado em um LRC", L"Aangepaste teksttiming als LRC opslaan", L"Zapisz skorygowany timing do LRC", L"Ayarlanan soz zamanlamasini LRC olarak kaydet"));
	menu.AddCheck(ID_MP_DESK_LRC,
		LL14(L"歌詞ウィンドウを表示", L"Show lyrics window", L"Afficher fenetre paroles", L"Mostra finestra testi", L"Mostrar ventana de letra",
			L"가사 창 표시", L"显示歌词窗口", L"عرض نافذة الكلمات", L"Показать окно текста", L"Textfenster anzeigen",
			L"Mostrar janela de letra", L"Songtekstvenster tonen", L"Pokaz okno tekstu", L"Soz penceresini goster"),
		IsDesktopLyricsOpen(),
		LL14(L"常時最前面の歌詞ウィンドウを開閉します（不透明度などは右クリック）", L"Toggle the always-on-top lyrics window (RMB for opacity etc.)", L"Basculer la fenetre de paroles au premier plan (clic droit pour opacite)", L"Attiva/disattiva la finestra testi in primo piano (tasto destro per opacita)", L"Alternar la ventana de letra siempre visible (clic der. para opacidad)", L"항상 위 가사 창을 여닫기(불투명도 등은 우클릭)", L"打开/关闭置顶歌词窗口（右键调不透明度等）", L"فتح/إغلاق نافذة كلمات أمامية (زر يمين للعتامة)", L"Открыть/закрыть окно текста поверх всех (ПКМ — непрозрачность)", L"Textfenster im Vordergrund ein/aus (RMB fur Deckkraft)", L"Abrir/fechar janela de letra no topo (botao dir. para opacidade)", L"Songtekstvenster bovenop aan/uit (RMB voor dekking)", L"Otworz/zamknij okno tekstu na wierzchu (PPM: nieprzezroczystosc)", L"Her zaman ustte soz penceresini ac/kapat (opaklik icin sag tik)"));
	menu.AddCommand(ID_MP_TAG_EDIT,
		LL14(L"タグ編集 (F2)", L"Edit tags (F2)", L"Editer tags (F2)", L"Modifica tag (F2)", L"Editar etiquetas (F2)", L"태그 편집 (F2)", L"编辑标签 (F2)", L"تحرير الوسوم (F2)", L"Правка тегов (F2)", L"Tags bearbeiten (F2)", L"Editar tags (F2)", L"Tags bewerken (F2)", L"Edytuj tagi (F2)", L"Etiket duzenle (F2)"),
		LL14(L"選択曲のタグを編集します（F2）", L"Edit tags for the selection (F2)", L"Editer les tags de la selection (F2)", L"Modifica i tag della selezione (F2)", L"Editar etiquetas de la seleccion (F2)", L"선택 곡의 태그를 편집(F2)", L"编辑所选曲目的标签（F2）", L"تحرير وسوم التحديد (F2)", L"Редактировать теги выбора (F2)", L"Tags der Auswahl bearbeiten (F2)", L"Editar tags da selecao (F2)", L"Tags van de selectie bewerken (F2)", L"Edytuj tagi wyboru (F2)", L"Secimin etiketlerini duzenle (F2)"));
	menu.AddCommand(ID_MP_NORM_PREVIEW,
		LL14(L"正規化プレビュー…", L"Normalize preview…", L"Apercu normalisation…", L"Anteprima normalizza…", L"Vista previa normalizar…", L"정규화 미리보기…", L"标准化预览…", L"معاينة التطبيع…", L"Превью нормализации…", L"Normalisierungsvorschau…", L"Previa normalizacao…", L"Normalisatie-voorbeeld…", L"Podglad normalizacji…", L"Normalizasyon onizleme…"),
		LL14(L"正規化後の音量感をプレビューします", L"Preview how normalization will sound", L"Apercu du rendu apres normalisation", L"Anteprima del suono dopo normalizzazione", L"Vista previa del sonido tras normalizar", L"정규화 후 음량을 미리 듣기", L"预览标准化后的音量效果", L"معاينة الصوت بعد التطبيع", L"Превью звучания после нормализации", L"Vorschau des normalisierten Klangs", L"Previa do som apos normalizacao", L"Voorbeeld van genormaliseerd geluid", L"Podglad brzmienia po normalizacji", L"Normalizasyon sonrasi sesi onizle"));
	menu.AddCommand(ID_MP_NORM_SCAN,
		LL14(L"計測して表示…", L"Measure & show…", L"Mesurer et afficher…", L"Misura e mostra…", L"Medir y mostrar…", L"측정하여 표시…", L"测量并显示…", L"قياس وعرض…", L"Измерить и показать…", L"Messen und anzeigen…", L"Medir e mostrar…", L"Meten en tonen…", L"Zmierz i pokaz…", L"Olç ve goster…"),
		LL14(L"選択曲のラウドネスを計測して表示します", L"Measure and display loudness for the selection", L"Mesurer et afficher le loudness de la selection", L"Misura e mostra il loudness della selezione", L"Medir y mostrar el loudness de la seleccion", L"선택 곡의 라우드니스를 측정·표시", L"测量并显示所选的响度", L"قياس وعرض جهارة التحديد", L"Измерить и показать громкость выбора", L"Loudness der Auswahl messen und anzeigen", L"Medir e mostrar o loudness da selecao", L"Loudness van de selectie meten en tonen", L"Zmierz i pokaz glosnosc wyboru", L"Secimin seslilik olcumunu yap ve goster"));
	{
		int lufs = savedata.mpNormTargetLufs;
		if (lufs > -14) lufs = -14;
		if (lufs < -18) lufs = -18;
		menu.AddSlider(
			LL14(L"LUFS 目標", L"LUFS target", L"Cible LUFS", L"Target LUFS", L"Objetivo LUFS",
				L"LUFS 목표", L"LUFS 目标", L"هدف LUFS", L"Цель LUFS", L"LUFS-Ziel",
				L"Alvo LUFS", L"LUFS-doel", L"Cel LUFS", L"LUFS hedef"),
			-18, -14, lufs, MpLufsSliderCb, this,
			LL14(L"正規化ターゲット LUFS -18…-14（ドラッグ中に反映）", L"Normalize target LUFS -18…-14 (live)",
				L"Cible normalisation LUFS -18…-14 (direct)", L"Target normalizza LUFS -18…-14 (live)",
				L"Objetivo normalizar LUFS -18…-14 (en vivo)", L"정규화 목표 LUFS -18…-14 (즉시)", L"标准化目标 LUFS -18…-14（即时）",
				L"هدف التطبيع LUFS -18…-14 (مباشر)", L"Цель нормализации LUFS -18…-14 (сразу)",
				L"Normalisierungsziel LUFS -18…-14 (live)", L"Alvo de normalizacao LUFS -18…-14 (ao vivo)",
				L"Normalisatiedoel LUFS -18…-14 (live)", L"Cel normalizacji LUFS -18…-14 (na zywo)", L"Normalizasyon hedefi LUFS -18…-14 (anlik)"));
		menu.AddCommand(ID_MP_NORM_LUFS14, L"-14 LUFS", LL14(L"正規化ターゲットを -14 LUFS（やや大きめ）にします", L"Set normalize target to -14 LUFS (louder)", L"Cible de normalisation -14 LUFS (plus fort)", L"Target normalizzazione -14 LUFS (piu forte)", L"Objetivo de normalizacion -14 LUFS (mas alto)", L"정규화 목표를 -14 LUFS(조금 크게)", L"将标准化目标设为 -14 LUFS（偏响）", L"تعيين هدف التطبيع إلى -14 LUFS (أعلى)", L"Цель нормализации -14 LUFS (громче)", L"Normalisierungsziel -14 LUFS (lauter)", L"Definir alvo de normalizacao -14 LUFS (mais alto)", L"Normalisatiedoel -14 LUFS (luider)", L"Cel normalizacji -14 LUFS (glosniej)", L"Normalizasyon hedefi -14 LUFS (daha yuksek)"));
		menu.AddCommand(ID_MP_NORM_LUFS16, L"-16 LUFS", LL14(L"正規化ターゲットを -16 LUFS（標準）にします", L"Set normalize target to -16 LUFS (standard)", L"Cible de normalisation -16 LUFS (standard)", L"Target normalizzazione -16 LUFS (standard)", L"Objetivo de normalizacion -16 LUFS (estandar)", L"정규화 목표를 -16 LUFS(표준)", L"将标准化目标设为 -16 LUFS（标准）", L"تعيين هدف التطبيع إلى -16 LUFS (قياسي)", L"Цель нормализации -16 LUFS (стандарт)", L"Normalisierungsziel -16 LUFS (Standard)", L"Definir alvo de normalizacao -16 LUFS (padrao)", L"Normalisatiedoel -16 LUFS (standaard)", L"Cel normalizacji -16 LUFS (standard)", L"Normalizasyon hedefi -16 LUFS (standart)"));
		menu.AddCommand(ID_MP_NORM_LUFS18, L"-18 LUFS", LL14(L"正規化ターゲットを -18 LUFS（控えめ）にします", L"Set normalize target to -18 LUFS (quieter)", L"Cible de normalisation -18 LUFS (plus bas)", L"Target normalizzazione -18 LUFS (piu basso)", L"Objetivo de normalizacion -18 LUFS (mas bajo)", L"정규화 목표를 -18 LUFS(낮게)", L"将标准化目标设为 -18 LUFS（偏轻）", L"تعيين هدف التطبيع إلى -18 LUFS (أخفض)", L"Цель нормализации -18 LUFS (тише)", L"Normalisierungsziel -18 LUFS (leiser)", L"Definir alvo de normalizacao -18 LUFS (mais baixo)", L"Normalisatiedoel -18 LUFS (zachter)", L"Cel normalizacji -18 LUFS (ciszej)", L"Normalizasyon hedefi -18 LUFS (daha dusuk)"));
	}
	menu.AddCommand(ID_MP_EXPORT_AB_NOW,
		LL14(L"A-Bを今すぐWAVへ…", L"Export A-B to WAV now…", L"Exporter A-B en WAV…", L"Esporta A-B in WAV…", L"Exportar A-B a WAV…", L"A-B를 지금 WAV로…", L"立即将 A-B 导出为 WAV…", L"تصدير A-B إلى WAV الآن…", L"Экспорт A-B в WAV сейчас…", L"A-B jetzt als WAV…", L"Exportar A-B para WAV agora…", L"A-B nu naar WAV…", L"Eksportuj A-B do WAV teraz…", L"A-B simdi WAV…"),
		LL14(L"設定した A-B 区間を今すぐ WAV ファイルへ書き出します", L"Export the A-B range to a WAV file right now", L"Exporter la plage A-B en WAV maintenant", L"Esporta subito l'intervallo A-B in WAV", L"Exportar ya el rango A-B a WAV", L"설정한 A-B 구간을 지금 WAV로 내보내기", L"立即将 A-B 区间导出为 WAV", L"تصدير نطاق A-B إلى WAV الآن", L"Сразу экспортировать диапазон A-B в WAV", L"A-B-Bereich jetzt als WAV exportieren", L"Exportar agora o intervalo A-B para WAV", L"A-B-bereik nu naar WAV exporteren", L"Eksportuj teraz zakres A-B do WAV", L"A-B araligini simdi WAV olarak disa aktar"));
	menu.AddCommand(ID_MP_EXPORT_AB,
		LL14(L"A-Bを書き出し範囲に", L"Export A-B range", L"Exporter plage A-B", L"Esporta intervallo A-B", L"Exportar rango A-B", L"A-B를 내보내기 범위로", L"将 A-B 设为导出范围", L"تصدير نطاق A-B", L"Экспорт диапазона A-B", L"A-B-Bereich exportieren", L"Exportar faixa A-B", L"A-B-bereik exporteren", L"Eksport zakresu A-B", L"A-B araligini disa aktar"),
		LL14(L"A-B 区間を以降の書き出し範囲として設定します", L"Use the A-B range for the next export", L"Utiliser la plage A-B pour le prochain export", L"Usa l'intervallo A-B per il prossimo export", L"Usar el rango A-B para la proxima exportacion", L"A-B 구간을 다음 내보내기 범위로 설정", L"将 A-B 设为下次导出范围", L"استخدام نطاق A-B للتصدير التالي", L"Использовать A-B для следующего экспорта", L"A-B als nachsten Exportbereich setzen", L"Usar o intervalo A-B na proxima exportacao", L"A-B gebruiken voor de volgende export", L"Uzyj zakresu A-B do nastepnego eksportu", L"Sonraki disa aktarma icin A-B kullan"));
	menu.AddCommand(ID_MP_AB_PACK,
		LL14(L"A-B/キューを一括書き出し…", L"Export A-B/cue pack…", L"Exporter pack A-B/cues…", L"Esporta pack A-B/cue…", L"Exportar pack A-B/cues…", L"A-B/큐 일괄 내보내기…", L"批量导出 A-B/标记…", L"تصدير حزمة A-B/cues…", L"Пакетный экспорт A-B/cue…", L"A-B/Cue-Paket export…", L"Exportar pacote A-B/cues…", L"A-B/cue-pakket…", L"Pakiet A-B/cue…", L"A-B/cue paketi…"),
		LL14(L"A-B 区間とキューをまとめて書き出します", L"Batch-export the A-B range and cue markers", L"Exporter en lot la plage A-B et les cues", L"Esporta in batch intervallo A-B e cue", L"Exportar por lotes el rango A-B y cues", L"A-B 구간과 큐를 일괄 내보내기", L"批量导出 A-B 区间与标记", L"تصدير نطاق A-B والإشارات دفعة واحدة", L"Пакетно экспортировать A-B и метки", L"A-B und Cues als Paket exportieren", L"Exportar em lote o intervalo A-B e cues", L"A-B en cues als pakket exporteren", L"Eksportuj pakietowo zakres A-B i cue", L"A-B ve cue'lari toplu disa aktar"));
	menu.AddCommand(ID_MP_NORM_BATCH,
		LL14(L"選択曲をLUFS正規化書き出し…", L"Batch normalize export (LUFS)…", L"Export normalise LUFS…", L"Esporta normalizzazione LUFS…", L"Exportar normalizar LUFS…", L"선택곡 LUFS 정규화…", L"批量 LUFS 标准化导出…", L"تصدير تطبيع LUFS…", L"Пакетная нормализация LUFS…", L"Batch-Normalisierung LUFS…", L"Exportar normalizacao LUFS…", L"Batch LUFS normaliseren…", L"Normalizacja LUFS…", L"LUFS toplu normalizasyon…"),
		LL14(L"選択曲を LUFS 正規化して一括書き出します", L"Batch-export the selection with LUFS normalization", L"Exporter la selection normalisee en LUFS", L"Esporta la selezione normalizzata LUFS", L"Exportar la seleccion normalizada LUFS", L"선택 곡을 LUFS 정규화하여 일괄 내보내기", L"将所选按 LUFS 标准化后批量导出", L"تصدير التحديد بعد تطبيع LUFS", L"Пакетно экспортировать выбор с нормализацией LUFS", L"Auswahl mit LUFS-Normalisierung batch-exportieren", L"Exportar a selecao com normalizacao LUFS", L"Selectie batch-exporteren met LUFS-normalisatie", L"Eksportuj wybor z normalizacja LUFS", L"Secimi LUFS normalizasyonuyla toplu disa aktar"));
	menu.AddCommand(ID_MP_MB_AUTOTAG,
		LL14(L"MusicBrainz 自動タグ…", L"MusicBrainz auto-tag…", L"Auto-tag MusicBrainz…", L"Auto-tag MusicBrainz…", L"Auto-etiqueta MusicBrainz…", L"MusicBrainz 자동 태그…", L"MusicBrainz 自动标签…", L"وسوم MusicBrainz…", L"Авто-тег MusicBrainz…", L"MusicBrainz Auto-Tag…", L"Auto-tag MusicBrainz…", L"MusicBrainz auto-tag…", L"Auto-tag MusicBrainz…", L"MusicBrainz otomatik etiket…"),
		LL14(L"MusicBrainz からタグ情報を自動取得します", L"Auto-fill tags from MusicBrainz", L"Remplir auto les tags via MusicBrainz", L"Compila auto i tag da MusicBrainz", L"Rellenar auto etiquetas desde MusicBrainz", L"MusicBrainz에서 태그 자동 가져오기", L"从 MusicBrainz 自动填充标签", L"ملء الوسوم تلقائياً من MusicBrainz", L"Автозаполнить теги из MusicBrainz", L"Tags automatisch von MusicBrainz holen", L"Preencher tags automaticamente pelo MusicBrainz", L"Tags automatisch van MusicBrainz invullen", L"Uzupelnij tagi automatycznie z MusicBrainz", L"MusicBrainz'den etiketleri otomatik doldur"));
	menu.AddSeparator();
	// 本体シークと同系統の値を og->m_time から取る（メニュー開時の (0) ズレ防止）
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		MirrorSeekVol();
		const int mn = og->m_time.GetMinValue();
		int mx = og->m_time.GetMaxValue();
		if (mx <= mn) mx = mn + 1;
		if (mx > mn) {
			int selMn = 0, selMx = 0;
			og->m_time.GetSelection(selMn, selMx);
			const int pos = og->m_time.GetPos();
			menu.AddRangeSlider(
				LL14(L"シーク / ループ / A-B", L"Seek / loop / A-B", L"Seek / boucle / A-B", L"Seek / loop / A-B",
					L"Seek / bucle / A-B", L"시크 / 루프 / A-B", L"定位 / 循环 / A-B", L"تقديم / حلقة / A-B",
					L"Поиск / цикл / A-B", L"Suche / Loop / A-B", L"Seek / loop / A-B", L"Zoek / lus / A-B",
					L"Seek / petla / A-B", L"Seek / dongu / A-B"),
				mn, mx, pos, selMn, selMx, m_abApos, m_abBpos,
				MpAbRangeCb, this,
				LL14(L"再生位置・ループ・A-B をメニュー上で調整", L"Adjust position, loop and A-B in the menu",
					L"Regler position, boucle et A-B dans le menu", L"Regola posizione, loop e A-B dal menu",
					L"Ajustar posicion, bucle y A-B en el menu", L"메뉴에서 위치/루프/A-B 조정", L"在菜单中调整位置、循环与 A-B",
					L"ضبط الموضع والحلقة و A-B من القائمة", L"Настройка позиции, цикла и A-B в меню",
					L"Position, Loop und A-B im Menü", L"Ajustar posicao, loop e A-B no menu",
					L"Positie, lus en A-B in het menu", L"Pozycja, petla i A-B w menu", L"Menude konum, dongu ve A-B"),
				0x00E01001u);
			menu.AddSeparator();
		}
	} else if (m_seek.GetSafeHwnd()) {
		const int mn = m_seek.GetMinValue();
		const int mx = m_seek.GetMaxValue();
		if (mx > mn) {
			int selMn = 0, selMx = 0;
			m_seek.GetSelection(selMn, selMx);
			menu.AddRangeSlider(
				LL14(L"シーク / ループ / A-B", L"Seek / loop / A-B", L"Seek / boucle / A-B", L"Seek / loop / A-B",
					L"Seek / bucle / A-B", L"시크 / 루프 / A-B", L"定位 / 循环 / A-B", L"تقديم / حلقة / A-B",
					L"Поиск / цикл / A-B", L"Suche / Loop / A-B", L"Seek / loop / A-B", L"Zoek / lus / A-B",
					L"Seek / petla / A-B", L"Seek / dongu / A-B"),
				mn, mx, m_seek.GetPos(), selMn, selMx, m_abApos, m_abBpos,
				MpAbRangeCb, this,
				LL14(L"再生位置・ループ・A-B をメニュー上で調整", L"Adjust position, loop and A-B in the menu",
					L"Regler position, boucle et A-B dans le menu", L"Regola posizione, loop e A-B dal menu",
					L"Ajustar posicion, bucle y A-B en el menu", L"메뉴에서 위치/루프/A-B 조정", L"在菜单中调整位置、循环与 A-B",
					L"ضبط الموضع والحلقة و A-B من القائمة", L"Настройка позиции, цикла и A-B в меню",
					L"Position, Loop und A-B im Menü", L"Ajustar posicao, loop e A-B no menu",
					L"Positie, lus en A-B in het menu", L"Pozycja, petla i A-B w menu", L"Menude konum, dongu ve A-B"),
				0x00E01001u);
			menu.AddSeparator();
		}
	}
	menu.AddCommand(ID_MP_AB_SNAP_A,
		LL14(L"スナップショット A", L"Snapshot A", L"Instantane A", L"Istantanea A", L"Instantanea A", L"스냅샷 A", L"快照 A", L"لقطة A", L"Снимок A", L"Schnappschuss A", L"Instantaneo A", L"Momentopname A", L"Migawka A", L"Anlik goruntu A"),
		LL14(L"現在の EQ／エフェクト設定をスロット A に保存します", L"Save current EQ/effect settings to slot A", L"Enregistrer les reglages EQ/FX dans le slot A", L"Salva EQ/FX attuali nello slot A", L"Guardar EQ/FX actuales en la ranura A", L"현재 EQ/효과 설정을 슬롯 A에 저장", L"将当前 EQ/效果保存到插槽 A", L"حفظ إعدادات EQ/FX الحالية في الفتحة A", L"Сохранить текущие EQ/FX в слот A", L"Aktuelle EQ/FX-Einstellungen in Slot A speichern", L"Salvar EQ/FX atuais no slot A", L"Huidige EQ/FX-instellingen in slot A opslaan", L"Zapisz biezace EQ/FX w slocie A", L"Gecerli EQ/FX ayarlarini A yuvasina kaydet"));
	menu.AddCommand(ID_MP_AB_SNAP_B,
		LL14(L"スナップショット B", L"Snapshot B", L"Instantane B", L"Istantanea B", L"Instantanea B", L"스냅샷 B", L"快照 B", L"لقطة B", L"Снимок B", L"Schnappschuss B", L"Instantaneo B", L"Momentopname B", L"Migawka B", L"Anlik goruntu B"),
		LL14(L"現在の EQ／エフェクト設定をスロット B に保存します", L"Save current EQ/effect settings to slot B", L"Enregistrer les reglages EQ/FX dans le slot B", L"Salva EQ/FX attuali nello slot B", L"Guardar EQ/FX actuales en la ranura B", L"현재 EQ/효과 설정을 슬롯 B에 저장", L"将当前 EQ/效果保存到插槽 B", L"حفظ إعدادات EQ/FX الحالية في الفتحة B", L"Сохранить текущие EQ/FX в слот B", L"Aktuelle EQ/FX-Einstellungen in Slot B speichern", L"Salvar EQ/FX atuais no slot B", L"Huidige EQ/FX-instellingen in slot B opslaan", L"Zapisz biezace EQ/FX w slocie B", L"Gecerli EQ/FX ayarlarini B yuvasina kaydet"));
	menu.AddCommand(ID_MP_AB_APPLY_A,
		LL14(L"A を適用", L"Apply A", L"Appliquer A", L"Applica A", L"Aplicar A", L"A 적용", L"应用 A", L"تطبيق A", L"Применить A", L"A anwenden", L"Aplicar A", L"A toepassen", L"Zastosuj A", L"A uygula"),
		LL14(L"スロット A の設定を再生に適用します", L"Apply slot A settings to playback", L"Appliquer les reglages du slot A", L"Applica le impostazioni dello slot A", L"Aplicar la configuracion de la ranura A", L"슬롯 A 설정을 재생에 적용", L"将插槽 A 设置应用到播放", L"تطبيق إعدادات الفتحة A", L"Применить настройки слота A", L"Einstellungen aus Slot A anwenden", L"Aplicar as configuracoes do slot A", L"Instellingen van slot A toepassen", L"Zastosuj ustawienia slotu A", L"A yuvasi ayarlarini uygula"));
	menu.AddCommand(ID_MP_AB_APPLY_B,
		LL14(L"B を適用", L"Apply B", L"Appliquer B", L"Applica B", L"Aplicar B", L"B 적용", L"应用 B", L"تطبيق B", L"Применить B", L"B anwenden", L"Aplicar B", L"B toepassen", L"Zastosuj B", L"B uygula"),
		LL14(L"スロット B の設定を再生に適用します", L"Apply slot B settings to playback", L"Appliquer les reglages du slot B", L"Applica le impostazioni dello slot B", L"Aplicar la configuracion de la ranura B", L"슬롯 B 설정을 재생에 적용", L"将插槽 B 设置应用到播放", L"تطبيق إعدادات الفتحة B", L"Применить настройки слота B", L"Einstellungen aus Slot B anwenden", L"Aplicar as configuracoes do slot B", L"Instellingen van slot B toepassen", L"Zastosuj ustawienia slotu B", L"B yuvasi ayarlarini uygula"));
	menu.AddCommand(ID_MP_AB_TOGGLE,
		LL14(L"A/B 切替", L"Toggle A/B", L"Basculer A/B", L"Commuta A/B", L"Alternar A/B", L"A/B 전환", L"切换 A/B", L"تبديل A/B", L"Переключить A/B", L"A/B umschalten", L"Alternar A/B", L"A/B wisselen", L"Przelacz A/B", L"A/B degistir"),
		LL14(L"スロット A と B の設定を交互に切り替えます", L"Toggle between slot A and B settings", L"Basculer entre les slots A et B", L"Commuta tra gli slot A e B", L"Alternar entre las ranuras A y B", L"슬롯 A/B 설정을 번갈아 전환", L"在插槽 A 与 B 之间切换", L"التبديل بين الفتحات A و B", L"Переключить между слотами A и B", L"Zwischen Slot A und B umschalten", L"Alternar entre os slots A e B", L"Wisselen tussen slot A en B", L"Przelacz miedzy slotami A i B", L"A ve B yuvalari arasinda gec"));
	{
		CCustomPopupMenu* seekMore = menu.AddSubMenu(
			LL14(L"シーク拡張", L"Seek extras", L"Seek avance", L"Seek extra", L"Seek extra",
				L"시크 확장", L"定位扩展", L"تقديم إضافي", L"Доп. поиск", L"Suche extra",
				L"Seek extra", L"Zoek extra", L"Seek ekstra", L"Seek ekstra"),
			LL14(L"シークバー右クリックのロック／波形／キュー／練習など", L"Seek-bar RMB: lock / wave / cues / practice", L"Clic droit barre: verrou / onde / cues / pratique", L"RMB barra: blocco / onda / cue / pratica",
				L"Clic der. barra: bloqueo / onda / cues / practica", L"시크바 우클릭: 잠금/파형/큐/연습", L"定位条右键：锁定/波形/标记/练习", L"زر يمين الشريط: قفل/موجة/إشارات/تدريب",
				L"ПКМ полосы: блокировка / волна / метки / практика", L"RMB Suchleiste: Sperre / Welle / Cues / Ubung",
				L"Botao dir. barra: trava / onda / cues / pratica", L"RMB zoekbalk: slot / golf / cues / oefenen",
				L"PPM paska: blokada / fala / cue / cwiczenie", L"Seek sag tik: kilit / dalga / cue / alistirma"));
		if (seekMore)
			AppendSeekExtrasToMenu(*seekMore, MP_SEEK_MENU_TOOLS_MORE);
	}
	menu.AddSeparator();
	{
		int sleepMin = savedata.mpSleepMin;
		if (sleepMin < 0) sleepMin = 0;
		if (sleepMin > 240) sleepMin = 240;
		menu.AddSlider(
			LL14(L"スリープ (分)", L"Sleep (min)", L"Veille (min)", L"Sleep (min)", L"Suspensión (min)",
				L"슬립 (분)", L"睡眠 (分)", L"نوم (د)", L"Сон (мин)", L"Schlaf (Min)",
				L"Sono (min)", L"Slaap (min)", L"Sen (min)", L"Uyku (dk)"),
			0, 240, sleepMin, MpSleepSliderCb, this,
			LL14(L"0=解除 … 240分。ドラッグ中にタイマー反映", L"0=off … 240 min. Live timer while dragging",
				L"0=off … 240 min. Timer en direct", L"0=off … 240 min. Timer live",
				L"0=off … 240 min. Temporizador en vivo", L"0=해제 … 240분. 드래그 중 타이머 반영", L"0=关闭 … 240 分钟。拖动即时计时",
				L"0=إيقاف … 240 د. مؤقت مباشر", L"0=выкл … 240 мин. Таймер сразу",
				L"0=aus … 240 Min. Timer live", L"0=off … 240 min. Timer ao vivo",
				L"0=uit … 240 min. Timer live", L"0=wyl … 240 min. Timer na zywo", L"0=kapali … 240 dk. Suruklerken anlik"));
		{
			wchar_t init[16];
			// 解除中は空欄（旧: 45 を仮表示 → SetWindowText/EN_CHANGE で 45 分スリープが武装する表記バグ）
			if (savedata.mpSleepMin > 0)
				_snwprintf_s(init, _TRUNCATE, L"%d", savedata.mpSleepMin);
			else
				init[0] = 0;
			menu.AddEdit(
				LL14(L"カスタム分 (1–240)", L"Custom min (1–240)", L"Min perso (1–240)", L"Min personalizzati (1–240)",
					L"Min personalizados (1–240)", L"사용자 분 (1–240)", L"自定义分钟 (1–240)", L"دقائق مخصصة (1–240)",
					L"Свои мин (1–240)", L"Eigene Min (1–240)", L"Min personalizados (1–240)", L"Aangepaste min (1–240)",
					L"Wlasne min (1–240)", L"Ozel dk (1–240)"),
				init, MpSleepEditCb, this,
				LL14(L"1–240分。入力中に反映（空は無視）", L"1–240 min. Applies while typing (empty ignored)",
					L"1–240 min. Applique en saisissant", L"1–240 min. Applica mentre digiti",
					L"1–240 min. Se aplica al escribir", L"1–240분. 입력 중 반영", L"1–240 分钟。输入即反映",
					L"1–240 د. يطبق أثناء الكتابة", L"1–240 мин. При вводе", L"1–240 Min. Beim Tippen",
					L"1–240 min. Ao digitar", L"1–240 min. Tijdens typen", L"1–240 min. Podczas wpisywania",
					L"1–240 dk. Yazarken uygula"));
			static const LPCTSTR kSleepLongNums[] = { L"75", L"90", L"120", L"180", L"240" };
			// 現在値がリスト外だと先頭が誤選択され、同じ値を選んでも選択イベントが飛ばず無反映になる
			static wchar_t s_sleepDash[] = L"—";
			LPCTSTR sleepLongItems[6];
			sleepLongItems[0] = s_sleepDash;
			for (int i = 0; i < 5; ++i)
				sleepLongItems[i + 1] = kSleepLongNums[i];
			int longSel = 0;
			for (int i = 0; i < 5; ++i) {
				if (_ttoi(kSleepLongNums[i]) == savedata.mpSleepMin) { longSel = i + 1; break; }
			}
			menu.AddCombo(
				LL14(L"長めプリセット", L"Long presets", L"Presets longs", L"Preset lunghi", L"Presets largos",
					L"긴 프리셋", L"较长预设", L"إعدادات طويلة", L"Длинные пресеты", L"Lange Vorgaben",
					L"Presets longos", L"Lange presets", L"Dlugie preset", L"Uzun onayar"),
				sleepLongItems, 6, longSel, MpSleepChoiceCb, this,
				LL14(L"61分超はここから。選択で即タイマー", L"Over 60 min from here. Applies on select",
					L"Au-dela de 60 min. Applique a la selection", L"Oltre 60 min. Applica alla selezione",
					L"Mas de 60 min. Aplica al elegir", L"60분 초과는 여기. 선택 즉시", L"超过 60 分钟从这里。选择即应用",
					L"أكثر من 60 د. يطبق عند الاختيار", L"Больше 60 мин. При выборе", L"Ueber 60 Min. Bei Auswahl",
					L"Acima de 60 min. Ao selecionar", L"Boven 60 min. Bij selectie", L"Powyzej 60 min. Po wyborze",
					L"60 dk ustu. Secince uygula"));
		}
		menu.AddCommand(ID_MP_SLEEP_OFF,
			LL14(L"スリープ解除", L"Sleep off", L"Veille off", L"Sleep off", L"Suspensión off",
				L"슬립 해제", L"关闭睡眠", L"إيقاف النوم", L"Сон выкл", L"Schlaf aus",
				L"Sono off", L"Slaap uit", L"Sen wyl", L"Uyku kapat"),
				LL14(L"スリープタイマーを解除して通常再生に戻します", L"Cancel the sleep timer and keep playing", L"Annuler la minuterie de veille", L"Annulla il timer sleep", L"Cancelar el temporizador de sueño", L"슬립 타이머를 해제합니다", L"取消睡眠定时器", L"إلغاء مؤقت النوم", L"Отменить таймер сна", L"Schlaf-Timer abschalten", L"Cancelar o temporizador de sono", L"Slaaptimer uitzetten", L"Wylacz timer snu", L"Uyku zamanlayicisini kapat"));
		menu.AddCommand(ID_MP_SLEEP_15,
			LL14(L"15 分", L"15 min", L"15 min", L"15 min", L"15 min", L"15분", L"15 分钟", L"15 د", L"15 мин", L"15 Min", L"15 min", L"15 min", L"15 min", L"15 dk"),
			LL14(L"15分後に再生を停止します", L"Stop playback after 15 minutes", L"Arrête la lecture après 15 min", L"Ferma la riproduzione dopo 15 min", L"Detiene la reproducción tras 15 min", L"15분 후 재생을 중지합니다", L"15 分钟后停止播放", L"يوقف التشغيل بعد 15 دقيقة", L"Остановить воспроизведение через 15 мин", L"Wiedergabe nach 15 Min stoppen", L"Para a reprodução após 15 min", L"Stopt afspelen na 15 min", L"Zatrzymaj odtwarzanie po 15 min", L"15 dk sonra calmayi durdurur"));
		menu.AddCommand(ID_MP_SLEEP_30,
			LL14(L"30 分", L"30 min", L"30 min", L"30 min", L"30 min", L"30분", L"30 分钟", L"30 د", L"30 мин", L"30 Min", L"30 min", L"30 min", L"30 min", L"30 dk"),
			LL14(L"30分後に再生を停止します", L"Stop playback after 30 minutes", L"Arrête la lecture après 30 min", L"Ferma la riproduzione dopo 30 min", L"Detiene la reproducción tras 30 min", L"30분 후 재생을 중지합니다", L"30 分钟后停止播放", L"يوقف التشغيل بعد 30 دقيقة", L"Остановить воспроизведение через 30 мин", L"Wiedergabe nach 30 Min stoppen", L"Para a reprodução após 30 min", L"Stopt afspelen na 30 min", L"Zatrzymaj odtwarzanie po 30 min", L"30 dk sonra calmayi durdurur"));
		menu.AddCommand(ID_MP_SLEEP_60,
			LL14(L"60 分", L"60 min", L"60 min", L"60 min", L"60 min", L"60분", L"60 分钟", L"60 د", L"60 мин", L"60 Min", L"60 min", L"60 min", L"60 min", L"60 dk"),
			LL14(L"60分後に再生を停止します", L"Stop playback after 60 minutes", L"Arrête la lecture après 60 min", L"Ferma la riproduzione dopo 60 min", L"Detiene la reproducción tras 60 min", L"60분 후 재생을 중지합니다", L"60 分钟后停止播放", L"يوقف التشغيل بعد 60 دقيقة", L"Остановить воспроизведение через 60 мин", L"Wiedergabe nach 60 Min stoppen", L"Para a reprodução após 60 min", L"Stopt afspelen na 60 min", L"Zatrzymaj odtwarzanie po 60 min", L"60 dk sonra calmayi durdurur"));
	}
	menu.AddSeparator();
	{
		CString bpmItem;
		if (MpBpmIsMeasuring()) {
			bpmItem = LL14(L"BPM 計測中…（再クリックで確定）", L"Measuring BPM… (click again to finish)", L"Mesure BPM… (recliquer pour finir)", L"Misura BPM… (clic di nuovo)", L"Midiendo BPM… (clic otra vez)", L"BPM 측정 중… (다시 클릭으로 확정)", L"正在测 BPM…（再点确定）", L"قياس BPM… (انقر مجدداً)", L"Измерение BPM… (клик снова)", L"BPM messen… (nochmals klicken)", L"Medindo BPM… (clique de novo)", L"BPM meten… (opnieuw klikken)", L"Pomiar BPM… (klik ponownie)", L"BPM olculuyor… (bitirmek icin tekrar)");
		}
		else if (savedata.mpDetectedBpm > 0) {
			bpmItem.Format(LL14(L"BPM 再計測（現在 %d）", L"Remeasure BPM (now %d)", L"Remesurer BPM (actuel %d)", L"Rimisura BPM (ora %d)", L"Volver a medir BPM (ahora %d)", L"BPM 재측정 (현재 %d)", L"重新测 BPM（当前 %d）", L"إعادة قياس BPM (الآن %d)", L"Перемерить BPM (сейчас %d)", L"BPM neu messen (jetzt %d)", L"Remedir BPM (agora %d)", L"BPM opnieuw (nu %d)", L"Ponownie BPM (teraz %d)", L"BPM yeniden olc (simdi %d)"),
				savedata.mpDetectedBpm);
		}
		else {
			bpmItem = LL14(L"BPM 計測開始（再生/PC音で数秒→再クリック）", L"Start BPM measure (play/PC audio a few sec → click again)", L"Demarrer BPM (lecture/PC quelques sec → recliquer)", L"Avvia BPM (riproduci/PC pochi sec → clic)", L"Iniciar BPM (reproduccion/PC unos seg → clic)", L"BPM 측정 시작 (재생/PC 소리 수초→다시 클릭)", L"开始测 BPM（播放/PC声数秒→再点）", L"بدء قياس BPM (تشغيل/صوت الجهاز ثم انقر)", L"Начать BPM (воспроизведение/ПК сек → клик)", L"BPM starten (Wiedergabe/PC einige Sek → Klick)", L"Iniciar BPM (reproducao/PC alguns seg → clique)", L"Start BPM (afspelen/pc enkele sec → klik)", L"Start BPM (odtwarzanie/PC kilka sek → klik)", L"BPM baslat (cal/PC birkac sn → tekrar)");
		}
		menu.AddCheck(ID_MP_BPM_DETECT, bpmItem, MpBpmIsMeasuring(), LL14(L"再生中のテンポを計測します（再クリックで確定）", L"Measure tempo while playing (click again to confirm)", L"Mesurer le tempo en lecture (recliquer pour confirmer)", L"Misura il tempo in riproduzione (clic di nuovo)", L"Medir el tempo al reproducir (clic otra vez)", L"재생 중 템포를 측정(다시 클릭으로 확정)", L"在播放中测量速度（再点确认）", L"قياس الإيقاع أثناء التشغيل (انقر مجدداً)", L"Измерить темп во время воспроизведения (клик снова)", L"Tempo wahrend Wiedergabe messen (nochmals klicken)", L"Medir o tempo durante a reproducao (clique de novo)", L"Tempo tijdens afspelen meten (opnieuw klikken)", L"Zmierz tempo podczas odtwarzania (klik ponownie)", L"Calarken temposu olc (onay icin tekrar tikla)"));
		if (!MpBpmIsMeasuring() && (savedata.mpDetectedBpm > 0 || savedata.mpBpmCand[0] > 0)) {
			MpBpmEnsureCandList();
			CCustomPopupMenu* candSub = NULL;
			for (int ci = 0; ci < 3; ++ci) {
				const int cb = savedata.mpBpmCand[ci];
				if (cb <= 0) continue;
				if (!candSub) {
					candSub = menu.AddSubMenu(
						LL14(L"BPM 候補", L"BPM candidates", L"Candidats BPM", L"Candidati BPM",
							L"Candidatos BPM", L"BPM 후보", L"BPM 候选", L"مرشحو BPM",
							L"Кандидаты BPM", L"BPM-Kandidaten", L"Candidatos BPM", L"BPM-kandidaten",
							L"Kandydaci BPM", L"BPM adaylari"),
							LL14(L"計測で得た BPM 候補から採用値を選びます", L"Pick the adopted BPM from measured candidates", L"Choisir le BPM retenu parmi les candidats", L"Scegli il BPM adottato tra i candidati", L"Elegir el BPM adoptado entre candidatos", L"측정된 BPM 후보에서 채택값을 선택", L"从测得的 BPM 候选中选择采用值", L"اختيار BPM المعتمد من المرشحين", L"Выбрать принятый BPM из кандидатов", L"Erkanntes BPM aus Kandidaten wahlen", L"Escolher o BPM adotado entre candidatos", L"Gekozen BPM uit kandidaten kiezen", L"Wybierz przyjete BPM sposrod kandydatow", L"Olcum adaylarindan BPM degerini sec"));
					if (!candSub) break;
				}
				CString candItem;
				candItem.Format(L"%d", cb);
				const UINT id = (ci == 0) ? ID_MP_BPM_CAND1 : (ci == 1) ? ID_MP_BPM_CAND2 : ID_MP_BPM_CAND3;
				candSub->AddCheck(id, candItem, savedata.mpDetectedBpm == cb);
			}
		}
	}
	menu.AddCommand(ID_MP_DJPAD,
		LL14(L"DJ パッド", L"DJ Pad", L"Pad DJ", L"Pad DJ", L"Pad DJ", L"DJ 패드", L"DJ 垫", L"لوحة DJ", L"DJ-панель", L"DJ-Pad", L"Pad DJ", L"DJ-pad", L"Pad DJ", L"DJ paneli"),
		LL14(L"DJ パッド（ホットキュー／エフェクト）を開きます", L"Open the DJ pad (hotcues/effects)", L"Ouvrir le pad DJ (hotcues/effets)", L"Apri il pad DJ (hotcue/effetti)", L"Abrir el pad DJ (hotcues/efectos)", L"DJ 패드(핫큐/이펙트)를 엽니다", L"打开 DJ 垫（热键标记/效果）", L"فتح لوحة DJ (إشارات/تأثيرات)", L"Открыть DJ-панель (hotcue/эффекты)", L"DJ-Pad (Hotcues/Effekte) oeffnen", L"Abrir o pad DJ (hotcues/efeitos)", L"DJ-pad (hotcues/effecten) openen", L"Otworz pad DJ (hotcue/efekty)", L"DJ panelini ac (hotcue/efekt)"));
	menu.AddCommand(ID_MP_VIDEO_EXTRACT,
		LL14(L"動画→音声抽出…", L"Extract audio from video…", L"Extraire audio de la video…", L"Estrai audio dal video…", L"Extraer audio del video…", L"동영상→오디오 추출…", L"从视频提取音频…", L"استخراج صوت من الفيديو…", L"Извлечь аудио из видео…", L"Audio aus Video…", L"Extrair audio do video…", L"Audio uit video…", L"Wyodrebnij audio z wideo…", L"Videodan ses cikar…"),
		LL14(L"動画ファイルから音声だけを抽出します", L"Extract audio only from a video file", L"Extraire uniquement l'audio d'une video", L"Estrai solo l'audio da un video", L"Extraer solo el audio de un video", L"동영상에서 오디오만 추출", L"仅从视频文件提取音频", L"استخراج الصوت فقط من فيديو", L"Извлечь только аудио из видео", L"Nur Audio aus einer Videodatei extrahieren", L"Extrair apenas o audio de um video", L"Alleen audio uit een videobestand halen", L"Wyodrebnij tylko audio z wideo", L"Videodan yalnizca sesi cikar"));
	menu.AddCommand(ID_MP_VIDEO_REPLACE,
		LL14(L"動画の音声を差し替え…", L"Replace video audio…", L"Remplacer audio video…", L"Sostituisci audio video…", L"Reemplazar audio video…", L"동영상 오디오 교체…", L"替换视频音频…", L"استبدال صوت الفيديو…", L"Заменить звук видео…", L"Video-Audio ersetzen…", L"Substituir audio do video…", L"Video-audio vervangen…", L"Zastap audio wideo…", L"Video sesini degistir…"),
		LL14(L"動画ファイルの音声トラックを WAV で差し替えます", L"Replace the video file audio track with a WAV", L"Remplacer la piste audio video par un WAV", L"Sostituisci la traccia audio del video con WAV", L"Reemplazar la pista de audio del video con WAV",
			L"동영상 오디오 트랙을 WAV로 교체", L"用 WAV 替换视频音轨", L"استبدال مسار صوت الفيديو بـ WAV", L"Заменить звуковую дорожку видео на WAV", L"Video-Tonspur durch WAV ersetzen",
			L"Substituir a faixa de audio do video por WAV", L"Vervang de video-audiotrack door WAV", L"Zastap sciezke audio wideo plikiem WAV", L"Video ses izini WAV ile degistir"));
	{
		CCustomPopupMenu* gcp = menu.AddSubMenu(
			LL14(L"ゲーム録画プリセット", L"Game capture preset", L"Preset capture jeu", L"Preset cattura gioco", L"Preset captura juego",
				L"게임 캡처 프리셋", L"游戏录制预设", L"إعداد التقاط اللعبة", L"Пресет записи игры", L"Game-Capture-Preset",
				L"Preset captura jogo", L"Game-capturepreset", L"Preset nagrywania gry", L"Oyun kayit on ayari"),
			LL14(L"画質を選んで画面キャプチャを開きます（1080p60推奨）", L"Pick quality and open screen capture (1080p60 recommended)", L"Choisir la qualite et ouvrir la capture (1080p60 recommande)", L"Scegli qualita e apri cattura (1080p60 consigliato)", L"Elija calidad y abra captura (1080p60 recomendado)",
				L"화질을 고르고 화면 캡처를 엽니다(1080p60 권장)", L"选择画质并打开画面捕获（推荐1080p60）", L"اختر الجودة وافتح الالتقاط (يُفضل 1080p60)", L"Выберите качество и откройте захват (рекомендуется 1080p60)", L"Qualität wählen und Capture öffnen (1080p60 empfohlen)",
				L"Escolha qualidade e abra a captura (1080p60 recomendado)", L"Kies kwaliteit en open capture (1080p60 aanbevolen)", L"Wybierz jakosc i otworz przechwytywanie (zalecane 1080p60)", L"Kalite secip ekran yakalamayi ac (onerilen 1080p60)"));
		if (gcp) {
			gcp->AddCommand(ID_MP_GCP_720_60,
				LL14(L"720p / 60fps（軽量）", L"720p / 60fps (light)", L"720p / 60fps (leger)", L"720p / 60fps (leggero)", L"720p / 60fps (ligero)",
					L"720p / 60fps (가벼움)", L"720p / 60fps（轻量）", L"720p / 60fps (خفيف)", L"720p / 60fps (лёгкий)", L"720p / 60fps (leicht)",
					L"720p / 60fps (leve)", L"720p / 60fps (licht)", L"720p / 60fps (lekki)", L"720p / 60fps (hafif)"),
				LL14(L"負荷を抑えたゲーム録画。まずはこれで試せます。", L"Lower-load game capture. Good starting point.", L"Capture jeu legere. Bon point de depart.", L"Cattura gioco leggera. Buon inizio.", L"Captura ligera. Buen punto de partida.",
					L"부하를 낮춘 게임 녹화. 먼저 이것부터.", L"低负载游戏录制。可先试这个。", L"تسجيل لعبة خفيف. نقطة بداية جيدة.", L"Лёгкая запись игры. Хороший старт.", L"Leichte Game-Aufnahme. Guter Start.",
					L"Captura leve. Bom ponto de partida.", L"Lichte game-opname. Goed startpunt.", L"Lekkie nagrywanie gry. Dobry start.", L"Hafif oyun kaydi. Iyi baslangic."));
			gcp->AddCommand(ID_MP_GCP_1080_60,
				LL14(L"1080p / 60fps（推奨・高品位）", L"1080p / 60fps (recommended)", L"1080p / 60fps (recommande)", L"1080p / 60fps (consigliato)", L"1080p / 60fps (recomendado)",
					L"1080p / 60fps (권장·고화질)", L"1080p / 60fps（推荐·高画质）", L"1080p / 60fps (موصى به)", L"1080p / 60fps (рекомендуется)", L"1080p / 60fps (empfohlen)",
					L"1080p / 60fps (recomendado)", L"1080p / 60fps (aanbevolen)", L"1080p / 60fps (zalecane)", L"1080p / 60fps (onerilen)"),
				LL14(L"フルHD・60fps・高ビットレート。多くのゲーム向けの標準高品位。", L"Full HD 60fps high bitrate. Standard high quality for most games.", L"Plein HD 60fps debit eleve. Qualite standard pour la plupart des jeux.", L"Full HD 60fps bitrate alto. Qualita standard per molti giochi.", L"Full HD 60fps alto bitrate. Calidad estandar para la mayoria.",
					L"풀HD 60fps 고비트레이트. 대부분 게임에 맞는 표준 고화질.", L"全高清60fps高码率。多数游戏的标准高画质。", L"Full HD 60 إطار بمعدل بت عالٍ. جودة قياسية لمعظم الألعاب.", L"Full HD 60fps высокий битрейт. Стандарт для большинства игр.", L"Full HD 60fps hohe Bitrate. Standard-Qualität für die meisten Spiele.",
					L"Full HD 60fps alto bitrate. Qualidade padrao para a maioria.", L"Full HD 60fps hoge bitrate. Standaardkwaliteit voor de meeste games.", L"Full HD 60fps wysoki bitrate. Standard dla wiekszosci gier.", L"Full HD 60fps yuksek bitrate. Cogu oyun icin standart kalite."));
			gcp->AddCommand(ID_MP_GCP_1080_120,
				LL14(L"1080p / 120fps（滑らか・高負荷）", L"1080p / 120fps (smooth·heavy)", L"1080p / 120fps (fluide·lourd)", L"1080p / 120fps (fluido·pesante)", L"1080p / 120fps (suave·pesado)",
					L"1080p / 120fps (부드러움·고부하)", L"1080p / 120fps（流畅·高负载）", L"1080p / 120fps (سلس·ثقيل)", L"1080p / 120fps (плавно·тяжело)", L"1080p / 120fps (flüssig·schwer)",
					L"1080p / 120fps (suave·pesado)", L"1080p / 120fps (soepel·zwaar)", L"1080p / 120fps (plynnie·ciezkie)", L"1080p / 120fps (akici·agir)"),
				LL14(L"高リフレッシュ向け。PC性能が必要です。", L"For high-refresh games. Needs a strong PC.", L"Pour ecrans haute frequence. PC puissant requis.", L"Per alti Hz. Serve un PC potente.", L"Para alto refresco. Requiere PC potente.",
					L"고주사율용. 강한 PC 필요.", L"适合高刷新。需要较强电脑。", L"لشاشات عالية التردد. يحتاج جهاز قوي.", L"Для высокого Гц. Нужен мощный ПК.", L"Für hohe Hz. Starker PC nötig.",
					L"Para alto refresh. Precisa de PC forte.", L"Voor hoge Hz. Sterke PC nodig.", L"Dla wysokiego Hz. Potrzebny mocny PC.", L"Yuksek Hz icin. Guclu PC gerekir."));
			gcp->AddCommand(ID_MP_GCP_4K_60,
				LL14(L"4K / 60fps（最高画質）", L"4K / 60fps (max quality)", L"4K / 60fps (qualite max)", L"4K / 60fps (qualita max)", L"4K / 60fps (max calidad)",
					L"4K / 60fps (최고화질)", L"4K / 60fps（最高画质）", L"4K / 60fps (أقصى جودة)", L"4K / 60fps (макс. качество)", L"4K / 60fps (max. Qualität)",
					L"4K / 60fps (qualidade max)", L"4K / 60fps (max kwaliteit)", L"4K / 60fps (max jakosc)", L"4K / 60fps (en yuksek kalite)"),
				LL14(L"3840×2160。容量と負荷が最大。強力なGPU向け。", L"3840×2160. Largest size/load. For strong GPUs.", L"3840×2160. Taille/charge max. GPU puissant.", L"3840×2160. Dimensione/carico max. GPU potente.", L"3840×2160. Tamano/carga max. GPU potente.",
					L"3840×2160. 용량·부하 최대. 강한 GPU용.", L"3840×2160。体积与负载最大。需强GPU。", L"3840×2160. أكبر حجم/حمل. لوحدة GPU قوية.", L"3840×2160. Макс. размер/нагрузка. Для мощных GPU.", L"3840×2160. Max. Größe/Last. Für starke GPUs.",
					L"3840×2160. Tamanho/carga max. Para GPU forte.", L"3840×2160. Max. formaat/belasting. Voor sterke GPU.", L"3840×2160. Max. rozmiar/obciazenie. Dla mocnego GPU.", L"3840×2160. En buyuk boyut/yuk. Guclu GPU icin."));
			gcp->AddSeparator();
			gcp->AddCommand(ID_MP_GCP_PLUS_WAV,
				LL14(L"1080p60 + 高音質WAV録音も開く", L"1080p60 + open HQ WAV recorder", L"1080p60 + ouvrir enregistreur WAV HQ", L"1080p60 + apri registratore WAV HQ", L"1080p60 + abrir grabador WAV HQ",
					L"1080p60 + 고음질 WAV 녹음도 열기", L"1080p60 + 同时打开高音质WAV录音", L"1080p60 + فتح مسجل WAV عالي الجودة", L"1080p60 + открыть WAV-запись HQ", L"1080p60 + WAV-Rekorder HQ öffnen",
					L"1080p60 + abrir gravador WAV HQ", L"1080p60 + WAV-recorder HQ openen", L"1080p60 + otworz rejestrator WAV HQ", L"1080p60 + yuksek kaliteli WAV ac"),
				LL14(L"画面キャプチャに加え、システム音を別途WAVで残します。", L"Screen capture plus a separate WAV of system audio.", L"Capture ecran plus WAV separe du son systeme.", L"Cattura piu WAV separato dell'audio di sistema.", L"Captura mas WAV aparte del audio del sistema.",
					L"화면 캡처와 함께 시스템 음을 별도 WAV로 남김.", L"画面捕获并另存系统声为WAV。", L"التقاط الشاشة مع WAV منفصل لصوت النظام.", L"Захват экрана плюс отдельный WAV системного звука.", L"Bildschirmaufnahme plus separates System-WAV.",
					L"Captura mais WAV separado do audio do sistema.", L"Schermopname plus apart systeemaudio-WAV.", L"Nagranie ekranu plus osobny WAV dzwieku systemu.", L"Ekran yakalama artı ayri sistem sesi WAV."));
		}
	}
	menu.AddCommand(ID_MP_MIRROR,
		LL14(L"ミラー出力…", L"Mirror output…", L"Sortie miroir…", L"Uscita mirror…", L"Salida espejo…", L"미러 출력…", L"镜像输出…", L"خرج مرآة…", L"Зеркальный выход…", L"Spiegelausgabe…", L"Saida espelho…", L"Spiegelaudio…", L"Wyjscie lustrzane…", L"Ayna cikis…"),
		LL14(L"ミラー出力の設定ダイアログを開きます", L"Open the mirror-output settings dialog", L"Ouvrir la boite de sortie miroir", L"Apri la finestra di uscita mirror", L"Abrir el dialogo de salida espejo", L"미러 출력 설정 대화상자를 엽니다", L"打开镜像输出设置对话框", L"فتح مربع حوار خرج المرآة", L"Открыть диалог зеркального выхода", L"Dialog der Spiegelausgabe oeffnen", L"Abrir o dialogo de saida espelho", L"Dialoog van spiegelaudio openen", L"Otworz okno wyjscia lustrzanego", L"Ayna cikis ayar penceresini ac"));
	menu.AddSeparator();
	menu.AddCommand(ID_MP_SOUNDMETER,
		LL14(L"騒音計…", L"Sound meter…", L"Sonomètre…", L"Fonometro…", L"Medidor de sonido…",
			L"소음계…", L"声级计…", L"مقياس الصوت…", L"Шумомер…", L"Schallpegelmesser…",
			L"Medidor de som…", L"Geluidsmeter…", L"Miernik dźwięku…", L"Ses ölçer…"),
		LL14(L"マイクの相対 dBFS を表示します（校正 SPL ではありません）", L"Shows relative mic dBFS (not calibrated SPL)", L"Affiche le dBFS micro relatif (pas SPL calibré)", L"Mostra dBFS micro relativo (non SPL calibrato)", L"Muestra dBFS de mic relativo (no SPL calibrado)",
			L"마이크 상대 dBFS 표시(교정 SPL 아님)", L"显示麦克风相对 dBFS（非校准 SPL）", L"يعرض dBFS نسبي للميكروفون (ليس SPL معايرًا)", L"Показывает относительный dBFS микрофона (не калибр. SPL)", L"Zeigt relativen Mikrofon-dBFS (kein kalibrierter SPL)",
			L"Mostra dBFS relativo do micro (não SPL calibrado)", L"Toont relatief micro-dBFS (geen gekalibreerde SPL)", L"Pokazuje względne dBFS mikrofonu (nie skalibrowany SPL)", L"Mikrofon göreli dBFS gösterir (kalibre SPL değil)"));
	menu.AddCommand(ID_MP_DIGITIZE,
		LL14(L"アナログ起こし台…", L"Analog digitizer…", L"Numériseur analogique…", L"Digitalizzatore analogico…", L"Digitalizador analógico…",
			L"아날로그 디지타이저…", L"模拟数字化…", L"محول تماثلي…", L"Оцифровка аналога…", L"Analog-Digitalisierer…",
			L"Digitalizador analógico…", L"Analoge digitizer…", L"Digitalizacja analogowa…", L"Analog dijitalleştirici…"),
		LL14(L"ライン／マイク入力を録り、モニタしながら WAV/mp3/FLAC に保存します", L"Record line/mic input to WAV/mp3/FLAC with monitoring", L"Enregistre entrée ligne/micro vers WAV/mp3/FLAC avec monitoring", L"Registra ingresso linea/micro in WAV/mp3/FLAC con monitor", L"Graba entrada de línea/mic a WAV/mp3/FLAC con monitor",
			L"라인/마이크 입력을 모니터하며 WAV/mp3/FLAC로 녹음", L"录制线路/麦克风输入为 WAV/mp3/FLAC（可监听）", L"يسجّل دخل الخط/الميك إلى WAV/mp3/FLAC مع مراقبة", L"Запись линейного/микрофонного входа в WAV/mp3/FLAC с монитором", L"Line-/Mikrofon-Eingang als WAV/mp3/FLAC mit Monitor aufnehmen",
			L"Grava entrada de linha/micro em WAV/mp3/FLAC com monitor", L"Neemt lijn/micro-invoer op naar WAV/mp3/FLAC met monitor", L"Nagrywa wejście liniowe/mikrofon do WAV/mp3/FLAC z monitorem", L"Hat/mikrofon girişini monitörleyerek WAV/mp3/FLAC kaydeder"));
	menu.AddCommand(ID_MP_VOICECHANGER,
		LL14(L"ボイスチェンジャー…", L"Voice changer…", L"Changeur de voix…", L"Cambia voce…", L"Cambiador de voz…",
			L"보이스 체인저…", L"变声器…", L"مغير الصوت…", L"Изменение голоса…", L"Stimmenwandler…",
			L"Modificador de voz…", L"Stemvervormer…", L"Zmiana głosu…", L"Ses değiştirici…"),
		LL14(L"マイクを加工して仮想ケーブル等の出力へ送ります", L"Process the mic and send it to a virtual-cable output", L"Traite le micro vers une sortie câble virtuel", L"Elabora il microverso un'uscita cavo virtuale", L"Procesa el micrófono hacia una salida de cable virtual",
			L"마이크를 가공해 가상 케이블 등으로 출력", L"处理麦克风并送到虚拟线缆等输出", L"يعالج الميكروفون ويرسله لخرج كابل افتراضي", L"Обрабатывает микрофон и выводит на виртуальный кабель", L"Verarbeitet Mikrofon und sendet an virtuelles Kabel",
			L"Processa o microfone e envia a um cabo virtual", L"Verwerkt microfoon en stuurt naar virtuele kabel", L"Przetwarza mikrofon i wysyła na kabel wirtualny", L"Mikrofonu işleyip sanal kablo çıkışına yollar"));
	menu.AddCommand(ID_MP_TUNERPRACTICE,
		LL14(L"チューナー道場…", L"Tuner practice…", L"Entraînement accordeur…", L"Pratica accordatore…", L"Práctica de afinación…",
			L"튜너 연습…", L"调音练习…", L"تدريب الموالف…", L"Практика с тюнером…", L"Stimmtraining…",
			L"Prática de afinação…", L"Stemoefening…", L"Ćwiczenie strojenia…", L"Akort alıştırması…"),
		LL14(L"単音チューナーとメトロノーム（趣味用途）", L"Monophonic tuner and metronome (hobby use)", L"Accordeur monophonique et métronome (loisir)", L"Accordatore monofonico e metronomo (hobbistico)", L"Afinador monofónico y metrónomo (aficionado)",
			L"단음 튜너와 메트로놈(취미용)", L"单音调音器与节拍器（爱好用途）", L"موالف أحادي ومترونوم (للهواية)", L"Монофонический тюнер и метроном (хобби)", L"Monophones Stimmgerät und Metronom (Hobby)",
			L"Afinador monofónico e metrónomo (amador)", L"Monofoon stemapparaat en metronoom (hobby)", L"Stroik monofoniczny i metronom (hobby)", L"Tek ses akort aleti ve metronom (hobi)"));
	menu.AddCommand(ID_MP_PHOTOFRAME,
		LL14(L"フォトフレーム…", L"Photo frame…", L"Cadre photo…", L"Cornice foto…", L"Marco de fotos…",
			L"포토 프레임…", L"照片框…", L"إطار الصور…", L"Фоторамка…", L"Fotorahmen…",
			L"Moldura…", L"Fotolijst…", L"Ramka zdjęć…", L"Fotoğraf çerçevesi…"),
		LL14(L"画像スライドショー。プレイリストを BGM にできます", L"Image slideshow; can use the playlist as BGM", L"Diaporama d'images; playlist en BGM possible", L"Presentazione immagini; playlist come BGM", L"Diapositivas de imágenes; playlist como BGM",
			L"이미지 슬라이드쇼. 플레이리스트를 BGM으로 사용 가능", L"图片幻灯片；可用播放列表作 BGM", L"عرض شرائح للصور؛ يمكن استخدام قائمة التشغيل كـ BGM", L"Слайд-шоу изображений; плейлист как BGM", L"Bild-Diashow; Playlist als BGM möglich",
			L"Apresentação de imagens; playlist como BGM", L"Afbeeldingsdiavoorstelling; afspeellijst als BGM", L"Pokaz obrazów; playlista jako BGM", L"Görüntü slayt gösterisi; çalma listesi BGM olabilir"));
	menu.AddCommand(ID_MP_SOFT3DMAZE,
		LL14(L"Soft3D 迷路…", L"Soft3D maze…", L"Labyrinthe Soft3D…", L"Labirinto Soft3D…", L"Laberinto Soft3D…",
			L"Soft3D 미로…", L"Soft3D 迷宫…", L"متاهة Soft3D…", L"Лабиринт Soft3D…", L"Soft3D-Labyrinth…",
			L"Labirinto Soft3D…", L"Soft3D-doolhof…", L"Labirynt Soft3D…", L"Soft3D labirent…"),
		LL14(L"大きさで迷路を生成。アイテムでテンポ・ピッチ・次曲・EQ に干渉します", L"Generate a maze by size; items tweak tempo, pitch, next track, EQ", L"Labyrinthe par taille ; objets affectent tempo, hauteur, piste, EQ", L"Labirinto per dimensione; oggetti influenzano tempo, pitch, brano, EQ", L"Laberinto por tamaño; objetos afectan tempo, tono, pista, EQ",
			L"크기로 미로 생성. 아이템이 템포·피치·다음 곡·EQ에 개입", L"按大小生成迷宫；道具干预速度、音高、下一曲、EQ", L"متاهة حسب الحجم؛ العناصر تؤثر على الإيقاع والطبقة والمسار وEQ", L"Лабиринт по размеру; предметы влияют на темп, высоту, трек, EQ", L"Labyrinth nach Größe; Items greifen in Tempo, Tonhöhe, Titel, EQ ein",
			L"Labirinto por tamanho; itens afetam tempo, tom, faixa, EQ", L"Doolhof op grootte; items grijpen in op tempo, toon, nummer, EQ", L"Labirynt wg rozmiaru; przedmioty wpływają na tempo, wysokość, utwór, EQ", L"Boyuta göre labirent; öğeler tempo, perde, parça, EQ’ye müdahale eder"));
	menu.AddCommand(ID_MP_SSVIZ,
		LL14(L"SS ビジュアライザ", L"SS visualizer", L"Visualiseur SS", L"Visualizzatore SS", L"Visualizador SS", L"SS 비주얼", L"SS 可视化", L"عارض SS", L"SS-визуализатор", L"SS-Visualizer", L"Visual SS", L"SS-visualizer", L"Wizual SS", L"SS gorsel"),
		LL14(L"スクリーンセーバー風ビジュアライザを開きます", L"Open the screensaver-style visualizer", L"Ouvrir le visualiseur type ecran de veille", L"Apri il visualizzatore stile screensaver", L"Abrir el visualizador tipo protector", L"화면보호기풍 비주얼라이저를 엽니다", L"打开屏保风格可视化器", L"فتح العارض بأسلوب شاشة التوقف", L"Открыть визуализатор в стиле заставки", L"Screensaver-Visualizer oeffnen", L"Abrir o visualizador estilo protetor", L"Screensaver-achtige visualizer openen", L"Otworz wizualizer w stylu wygaszacza", L"Ekran koruyucu tarzi gorseli ac"));
	{
		static wchar_t s_hh[24][4];
		static wchar_t s_mm[60][4];
		static LPCTSTR s_hhItems[24];
		static LPCTSTR s_mmItems[60];
		static BOOL s_hmInit = FALSE;
		if (!s_hmInit) {
			for (int h = 0; h < 24; ++h) {
				_snwprintf_s(s_hh[h], _TRUNCATE, L"%02d", h);
				s_hhItems[h] = s_hh[h];
			}
			for (int m = 0; m < 60; ++m) {
				_snwprintf_s(s_mm[m], _TRUNCATE, L"%02d", m);
				s_mmItems[m] = s_mm[m];
			}
			s_hmInit = TRUE;
		}
		if (savedata.mpAlarmHour >= 0 && savedata.mpAlarmHour <= 23) {
			g_mpMenuAlarmH = savedata.mpAlarmHour;
			g_mpMenuAlarmM = savedata.mpAlarmMin;
			if (g_mpMenuAlarmM < 0) g_mpMenuAlarmM = 0;
			if (g_mpMenuAlarmM > 59) g_mpMenuAlarmM = 59;
			g_mpMenuAlarmDraftValid = TRUE;
		} else if (!g_mpMenuAlarmDraftValid) {
			SYSTEMTIME st = {};
			::GetLocalTime(&st);
			g_mpMenuAlarmH = (int)st.wHour;
			g_mpMenuAlarmM = (int)st.wMinute;
			g_mpMenuAlarmDraftValid = TRUE;
		}
		menu.AddCombo(
			LL14(L"アラーム時", L"Alarm hour", L"Heure alarme", L"Ora sveglia", L"Hora alarma",
				L"알람 시", L"闹钟时", L"ساعة المنبه", L"Час будильника", L"Wecker-Stunde",
				L"Hora alarme", L"Wekker-uur", L"Godzina budzika", L"Alarm saati"),
			s_hhItems, 24, g_mpMenuAlarmH, MpAlarmHourCb, this,
			LL14(L"0–23。有効中は選択ですぐ反映", L"0–23. Applies immediately while alarm is on",
				L"0–23. Applique tout de suite si active", L"0–23. Subito se attiva",
				L"0–23. Al instante si activa", L"0–23. 켜져 있으면 즉시", L"0–23。开启时立即生效",
				L"0–23. فوري إن كان مفعلاً", L"0–23. Сразу если включён", L"0–23. Sofort wenn aktiv",
				L"0–23. Imediato se ligado", L"0–23. Meteen als aan", L"0–23. Od razu gdy wl",
				L"0–23. Aciksa hemen"));
		menu.AddCombo(
			LL14(L"アラーム分", L"Alarm minute", L"Minute alarme", L"Minuto sveglia", L"Minuto alarma",
				L"알람 분", L"闹钟分", L"دقيقة المنبه", L"Минута будильника", L"Wecker-Minute",
				L"Minuto alarme", L"Wekker-minuut", L"Minuta budzika", L"Alarm dakikasi"),
			s_mmItems, 60, g_mpMenuAlarmM, MpAlarmMinCb, this,
			LL14(L"0–59。有効中は選択ですぐ反映", L"0–59. Applies immediately while alarm is on",
				L"0–59. Applique tout de suite si active", L"0–59. Subito se attiva",
				L"0–59. Al instante si activa", L"0–59. 켜져 있으면 즉시", L"0–59。开启时立即生效",
				L"0–59. فوري إن كان مفعلاً", L"0–59. Сразу если включён", L"0–59. Sofort wenn aktiv",
				L"0–59. Imediato se ligado", L"0–59. Meteen als aan", L"0–59. Od razu gdy wl",
				L"0–59. Aciksa hemen"));
		menu.AddCheck(ID_MP_ALARM,
			LL14(L"アラーム有効", L"Alarm on", L"Alarme active", L"Sveglia attiva", L"Alarma activa",
				L"알람 켜기", L"闹钟开启", L"المنبه مفعل", L"Будильник вкл", L"Wecker an",
				L"Alarme ligado", L"Wekker aan", L"Budzik wl", L"Alarm acik"),
			savedata.mpAlarmHour >= 0 ? TRUE : FALSE,
			LL14(L"指定時刻に再生開始。先に時分を選んでからON", L"Start playback at set time. Set hour/min first, then ON",
				L"Lecture a l'heure. Regler H/M puis ON", L"Avvia all'ora. Imposta H/M poi ON",
				L"Reproduce a la hora. Elija H/M luego ON", L"지정 시각에 재생. 시/분 후 ON",
				L"到点播放。先选时/分再开启", L"تشغيل في الوقت. عيّن الساعة/الدقيقة ثم تشغيل",
				L"Старт в заданное время. Сначала Ч/М, потом Вкл", L"Wiedergabe zur Zeit. Zuerst H/M, dann AN",
				L"Toca no horario. Defina H/M depois ON", L"Afspelen op tijd. Eerst U/M, dan AAN",
				L"Odtwarzanie o godzinie. Najpierw G/M, potem WL", L"Saatte cal. Once saat/dk, sonra AC"));
	}
	{
		wchar_t portInit[16];
		// 無効値は空欄（旧: 8765 仮表示 → KILLFOCUS で保存される表記寄りバグ）
		if (savedata.mpRemotePort >= 1024 && savedata.mpRemotePort <= 65535)
			_snwprintf_s(portInit, _TRUNCATE, L"%d", savedata.mpRemotePort);
		else
			portInit[0] = 0;
		menu.AddEdit(
			LL14(L"リモートポート", L"Remote port", L"Port remote", L"Porta remote", L"Puerto remoto",
				L"리모트 포트", L"遥控端口", L"منفذ التحكم", L"Порт пульта", L"Remote-Port",
				L"Porta remota", L"Remote-poort", L"Port pilota", L"Uzaktan port"),
			portInit, MpRemotePortCb, this,
			LL14(L"1024–65535。有効中は入力で再起動（空は無視）", L"1024–65535. Restarts while remote is on (empty ignored)",
				L"1024–65535. Redemarre si actif (vide ignore)", L"1024–65535. Riavvia se attivo (vuoto ignorato)",
				L"1024–65535. Reinicia si activo (vacio ignorado)", L"1024–65535. 켜져 있으면 재시작(빈칸 무시)", L"1024–65535。开启时会重启（空忽略）",
				L"1024–65535. يعاد التشغيل إن كان مفعلاً (فارغ يُتجاهل)", L"1024–65535. Перезапуск если включён (пусто игнор)", L"1024–65535. Neustart wenn aktiv (leer ignoriert)",
				L"1024–65535. Reinicia se ligado (vazio ignorado)", L"1024–65535. Herstart als aan (leeg genegeerd)", L"1024–65535. Restart gdy wl (puste ignoruj)",
				L"1024–65535. Aciksa yeniden baslar (bos yok sayilir)"));
		menu.AddCheck(ID_MP_REMOTE,
			LL14(L"ローカルリモート (HTTP)", L"Local remote (HTTP)", L"Telecommande locale (HTTP)", L"Remote locale (HTTP)", L"Remoto local (HTTP)",
				L"로컬 리모트 (HTTP)", L"本地遥控 (HTTP)", L"تحكم محلي (HTTP)", L"Локальный пульт (HTTP)", L"Lokalfernbedienung (HTTP)",
				L"Remoto local (HTTP)", L"Lokale bediening (HTTP)", L"Pilot lokalny (HTTP)", L"Yerel uzaktan (HTTP)"),
			savedata.mpRemoteOn ? TRUE : FALSE,
			LL14(L"同じ Wi-Fi のスマホ／PC から操作（同時3台まで）。先にポートを確認", L"Control from phones/PCs on same Wi-Fi (max 3). Confirm port first",
				L"Controle depuis telephones/PC sur le meme Wi-Fi (max 3). Verifier le port", L"Controllo da telefoni/PC sulla stessa Wi-Fi (max 3). Controlla porta",
				L"Control desde moviles/PC en la misma Wi-Fi (máx. 3). Confirme puerto", L"같은 Wi-Fi의 폰/PC에서 조작(최대 3). 포트 먼저 확인",
				L"同一 Wi-Fi 下手机/PC 控制（最多3）。先确认端口", L"تحكم من الهواتف/أجهزة الكمبيوتر على نفس Wi-Fi (حد 3). أكد المنفذ",
				L"Управление с телефонов/ПК в той же Wi-Fi (до 3). Сначала порт", L"Steuerung von Telefonen/PCs im gleichen WLAN (max. 3). Zuerst Port",
				L"Controlo de telemoveis/PCs na mesma Wi-Fi (máx. 3). Confirme a porta", L"Bediening vanaf telefoons/pc's op hetzelfde Wi-Fi (max 3). Eerst poort",
				L"Sterowanie z telefonow/PC w tej samej Wi-Fi (max 3). Najpierw port", L"Ayni Wi-Fi'deki telefon/PC'den kontrol (en fazla 3). Once port"));
		menu.AddCommand(ID_MP_REMOTE_DLG,
			LL14(L"リモート設定…", L"Remote settings…", L"Reglages remote…", L"Impostazioni remote…", L"Ajustes remoto…",
				L"리모트 설정…", L"遥控设置…", L"إعدادات التحكم…", L"Настройки пульта…", L"Remote-Einstellungen…",
				L"Definicoes remotas…", L"Remote-instellingen…", L"Ustawienia pilota…", L"Uzaktan ayarlar…"),
			LL14(L"URL表示・ブラウザ起動などの設定画面", L"Settings: URL, open in browser", L"Reglages: URL, navigateur", L"Impostazioni: URL, browser", L"Ajustes: URL, navegador",
				L"URL/브라우저 설정", L"URL、浏览器设置", L"الإعدادات: الرابط والمتصفح", L"Настройки: URL, браузер", L"Einstellungen: URL, Browser",
				L"Definicoes: URL, navegador", L"Instellingen: URL, browser", L"Ustawienia: URL, przegladarka", L"Ayarlar: URL, tarayici"));
		menu.AddCommand(ID_MP_REMOTE_BROWSER,
			LL14(L"ブラウザで開く", L"Open in browser", L"Ouvrir dans le navigateur", L"Apri nel browser", L"Abrir en el navegador",
				L"브라우저에서 열기", L"在浏览器打开", L"فتح في المتصفح", L"Открыть в браузере", L"Im Browser öffnen",
				L"Abrir no navegador", L"Openen in browser", L"Otworz w przegladarce", L"Tarayicida ac"),
			LL14(L"既定ブラウザでリモコンページを開く（必要なら有効化）", L"Open remote page in default browser (enables if needed)", L"Ouvrir la page dans le navigateur (active si besoin)", L"Apri la pagina nel browser (attiva se serve)", L"Abrir la pagina en el navegador (activa si hace falta)",
				L"기본 브라우저로 리모트 페이지(필요시 켜기)", L"用默认浏览器打开遥控页（必要时启用）", L"فتح صفحة التحكم (تفعيل إن لزم)", L"Открыть страницу пульта (включить при необходимости)", L"Remote-Seite im Browser (bei Bedarf aktivieren)",
				L"Abrir pagina remota (ativa se preciso)", L"Remote-pagina openen (indien nodig aan)", L"Otworz strone pilota (wlacz gdy trzeba)", L"Uzaktan sayfasini ac (gerekirse ac)"));
	}
	menu.AddCheck(ID_MP_MIDI_IN,
		LL14(L"MIDI In", L"MIDI In", L"MIDI In", L"MIDI In", L"MIDI In", L"MIDI In", L"MIDI 输入", L"MIDI In", L"MIDI In", L"MIDI In", L"MIDI In", L"MIDI In", L"MIDI In", L"MIDI In"),
		MpMidiInIsActive(),
		LL14(L"MIDI 入力デバイスからの操作を受け付けます", L"Accept control from a MIDI input device", L"Accepter les commandes d'un peripherique MIDI", L"Accetta comandi da un dispositivo MIDI In", L"Aceptar control desde un dispositivo MIDI", L"MIDI 입력 장치의 조작을 받습니다", L"接受来自 MIDI 输入设备的控制", L"قبول التحكم من جهاز إدخال MIDI", L"Принимать управление с MIDI-входа", L"Steuerung uber MIDI-Eingabegerat annehmen", L"Aceitar controle de um dispositivo MIDI", L"Bediening vanaf een MIDI-invoerapparaat", L"Przyjmuj sterowanie z urzadzenia MIDI", L"MIDI giris cihazindan kontrol kabul et"));
	menu.AddSeparator();
	MpFeatAppendKeyMenu(menu);
	menu.AddCheck(ID_MP_FOCUS_MODE,
		LL14(L"フォーカスモード", L"Focus mode", L"Mode focus", L"Modalita focus", L"Modo foco",
			L"포커스 모드", L"专注模式", L"وضع التركيز", L"Режим фокуса", L"Fokusmodus",
			L"Modo foco", L"Focusmodus", L"Tryb focus", L"Odak modu"),
		savedata.mpFocusMode != 0,
		LL14(L"作業に集中できるよう UI の装飾を控えめにします", L"Simplify UI chrome for a calmer focus mode", L"Simplifier l'interface pour se concentrer", L"Semplifica l'interfaccia in modalita focus", L"Simplificar la interfaz en modo foco", L"집중을 위해 UI 장식을 줄입니다", L"简化界面装饰以便专注", L"تبسيط واجهة المستخدم لوضع التركيز", L"Упростить интерфейс для режима фокуса", L"UI beruhigen fur Fokusmodus", L"Simplificar a interface no modo foco", L"UI vereenvoudigen voor focusmodus", L"Uprosc interfejs w trybie focus", L"Odak icin arayuzu sadeleştir"));
	menu.AddCheck(ID_MP_CONFIRM_DANGER,
		LL14(L"危険操作の確認", L"Confirm dangerous ops", L"Confirmer ops dangereuses", L"Conferma ops rischiose", L"Confirmar ops peligrosas",
			L"위험 작업 확인", L"危险操作确认", L"تأكيد العمليات الخطرة", L"Подтверждать опасные", L"Gefährliche bestätigen",
			L"Confirmar ops perigosas", L"Bevestig gevaarlijke", L"Potwierdzaj niebezpieczne", L"Tehlikeli islem onayi"),
		savedata.confirmDanger != 0,
		LL14(L"削除など危険な操作の前に確認ダイアログを出します", L"Ask for confirmation before dangerous actions", L"Demander confirmation avant les actions dangereuses", L"Chiedi conferma prima delle azioni rischiose", L"Pedir confirmacion antes de acciones peligrosas", L"삭제 등 위험 작업 전에 확인", L"危险操作前弹出确认", L"طلب تأكيد قبل العمليات الخطرة", L"Спрашивать подтверждение перед опасными действиями", L"Vor gefahrlichen Aktionen nachfragen", L"Pedir confirmacao antes de acoes perigosas", L"Om bevestiging vragen bij gevaarlijke acties", L"Pytaj o potwierdzenie przed niebezpiecznymi", L"Tehlikeli islemlerden once onay iste"));
	menu.AddCommand(ID_MP_LIVE_SET_REC,
		LL14(L"ライブセット録画（画面+録音）", L"Live-set record (capture+audio)", L"Enreg. set live", L"Registra set live", L"Grabar set en vivo",
			L"라이브 세트 녹화", L"现场套录制", L"تسجيل المجموعة", L"Запись сета", L"Live-Set aufnehmen",
			L"Gravar set ao vivo", L"Live-set opnemen", L"Nagraj set live", L"Canli set kaydi"),
			LL14(L"画面キャプチャと録音を同時に開始します", L"Start screen capture and audio recording together", L"Demarrer capture ecran et enregistrement ensemble", L"Avvia cattura schermo e registrazione insieme", L"Iniciar captura de pantalla y grabacion juntas", L"화면 캡처와 녹음을 함께 시작", L"同时开始画面捕获与录音", L"بدء التقاط الشاشة والتسجيل معاً", L"Запустить захват экрана и запись вместе", L"Bildschirm- und Audioaufnahme zusammen starten", L"Iniciar captura de tela e gravacao juntas", L"Schermopname en audio-opname samen starten", L"Uruchom przechwytywanie i nagrywanie razem", L"Ekran yakalama ve kaydi birlikte baslat"));
	menu.AddCheck(ID_MP_NOWPLAYING_FILE, L"nowplaying.txt", savedata.mpNowPlayingFile != 0, LL14(L"再生中の曲情報を nowplaying.txt に書き出します（配信連携）", L"Write now-playing info to nowplaying.txt (for streaming)", L"Ecrire les infos en cours dans nowplaying.txt", L"Scrivi le info in riproduzione su nowplaying.txt", L"Escribir la info en reproducción en nowplaying.txt", L"재생 정보를 nowplaying.txt에 기록(방송 연동)", L"将正在播放信息写入 nowplaying.txt（直播联动）", L"كتابة معلومات التشغيل إلى nowplaying.txt", L"Писать текущий трек в nowplaying.txt", L"Aktuelle Titelinfo in nowplaying.txt schreiben", L"Gravar info atual em nowplaying.txt", L"Huidige info naar nowplaying.txt schrijven", L"Zapisz biezace info do nowplaying.txt", L"Calan bilgiyi nowplaying.txt'ye yaz"));
	menu.AddCheck(ID_MP_MIDI_LEARN,
		LL14(L"MIDI 学習", L"MIDI learn", L"Apprentissage MIDI", L"Apprendimento MIDI", L"Aprendizaje MIDI",
			L"MIDI 학습", L"MIDI 学习", L"تعلم MIDI", L"Обучение MIDI", L"MIDI lernen",
			L"Aprendizado MIDI", L"MIDI leren", L"Nauka MIDI", L"MIDI ogren"),
		savedata.mpMidiLearn != 0,
		LL14(L"次の操作を MIDI コントローラに割り当てる学習モードです", L"Learn mode: map the next action to a MIDI control", L"Mode apprentissage: associer l'action suivante au MIDI", L"Modalita learn: associa l'azione successiva al MIDI", L"Modo aprendizaje: asignar la siguiente accion a MIDI", L"다음 동작을 MIDI에 할당하는 학습 모드", L"学习模式：将下一操作映射到 MIDI", L"وضع التعلم: ربط الإجراء التالي بـ MIDI", L"Режим обучения: назначить следующее действие MIDI", L"Lernmodus: nächste Aktion einem MIDI-Regler zuweisen", L"Modo aprendizado: mapear a proxima acao ao MIDI", L"Leermodus: volgende actie aan MIDI koppelen", L"Tryb nauki: mapuj nastepna akcje na MIDI", L"Ogrenme modu: sonraki islemi MIDI'ye ata"));
	menu.AddCheck(ID_MP_MIRROR_CUE,
		LL14(L"Mirror CUE プレビュー", L"Mirror CUE preview", L"Apercu CUE miroir", L"Anteprima CUE mirror", L"Vista previa CUE espejo",
			L"미러 CUE 미리보기", L"镜像 CUE 预览", L"معاينة CUE المرآة", L"Превью CUE зеркала", L"Mirror-CUE-Vorschau",
			L"Previa CUE espelho", L"Mirror-CUE-voorbeeld", L"Podglad CUE lustra", L"Ayna CUE onizleme"),
		savedata.mpMirrorCueMode != 0,
		LL14(L"ミラー出力側で CUE プレビューを使えるようにします", L"Enable CUE preview on the mirror output", L"Activer l'apercu CUE sur la sortie miroir", L"Abilita anteprima CUE sull'uscita mirror", L"Activar vista previa CUE en la salida espejo", L"미러 출력에서 CUE 미리보기를 사용", L"在镜像输出上启用 CUE 预览", L"تفعيل معاينة CUE على خرج المرآة", L"Включить превью CUE на зеркальном выходе", L"CUE-Vorschau auf der Spiegelausgabe aktivieren", L"Ativar previa CUE na saida espelho", L"CUE-voorbeeld op spiegelaudio inschakelen", L"Wlacz podglad CUE na wyjsciu lustrzanym", L"Ayna cikisinda CUE onizlemeyi ac"));
	menu.AddCheck(ID_MP_PHRASE_SNAP,
		LL14(L"フレーズを拍スナップ", L"Snap phrases to beats", L"Accrocher phrases aux temps", L"Aggancia frasi ai beat", L"Ajustar frases al beat",
			L"프레이즈 비트 스냅", L"乐句对齐拍", L"محاذاة العبارات للنبض", L"Привязка фраз к долям", L"Phrasen an Beats",
			L"Ajustar frases aos beats", L"Frases aan beats", L"Frazy do beatow", L"Cumleleri vuruslara"),
		savedata.mpPhraseSnapBeat != 0,
		LL14(L"フレーズ A-B を拍グリッドにスナップします", L"Snap phrase A-B points to the beat grid", L"Accrocher les points A-B de phrase a la grille", L"Aggancia i punti A-B della frase alla griglia", L"Ajustar los puntos A-B de frase a la cuadricula", L"프레이즈 A-B를 비트 그리드에 스냅", L"将乐句 A-B 对齐到节拍网格", L"محاذاة نقاط عبارة A-B لشبكة الإيقاع", L"Привязать точки фразы A-B к сетке долей", L"Phrasen-A-B an das Beat-Raster snappen", L"Ajustar pontos A-B da frase a grade", L"Frase-A-B aan het beatraster snappen", L"Przypnij punkty frazy A-B do siatki", L"Cumle A-B noktalarini vurus izgarasina yapistir"));
	{
		CCustomPopupMenu* tr = menu.AddSubMenu(LL14(L"トランジション・プリセット", L"Transition presets", L"Presets transition", L"Preset transizione", L"Presets de transicion",
			L"전환 프리셋", L"过渡预设", L"إعدادات الانتقال", L"Пресеты перехода", L"Übergangs-Presets",
			L"Presets de transicao", L"Overgangs-presets", L"Preset przejsc", L"Gecis onayarlari"),
			LL14(L"曲間トランジションのプリセットを適用します", L"Apply a between-track transition preset", L"Appliquer un preset de transition", L"Applica un preset di transizione", L"Aplicar un preset de transicion", L"곡 사이 전환 프리셋을 적용", L"应用曲间过渡预设", L"تطبيق إعداد انتقال بين المقاطع", L"Применить пресет перехода между треками", L"Ubergangs-Preset zwischen Titeln anwenden", L"Aplicar um preset de transicao entre faixas", L"Overgangs-preset tussen nummers toepassen", L"Zastosuj preset przejscia miedzy utworami", L"Parcalar arasi gecis onayarisini uygula"));
		if (tr) {
			tr->AddCommand(ID_MP_TRANS_PRE_0, L"EQ sweep / 4s");
			tr->AddCommand(ID_MP_TRANS_PRE_1, L"Filter / 8s");
			tr->AddCommand(ID_MP_TRANS_PRE_2, L"Quick xfade / 2s");
		}
		CCustomPopupMenu* lay = menu.AddSubMenu(LL14(L"レイアウト", L"Layout", L"Disposition", L"Layout", L"Diseno",
			L"레이아웃", L"布局", L"تخطيط", L"Макет", L"Layout", L"Layout", L"Layout", L"Uklad", L"Duzen"),
			LL14(L"ウィンドウ配置の保存／読込スロットです", L"Save/load window layout slots", L"Slots pour enregistrer/charger la disposition", L"Slot per salvare/caricare il layout", L"Ranuras para guardar/cargar el diseno", L"창 배치 저장/불러오기 슬롯", L"窗口布局的保存/加载插槽", L"فتحات حفظ/تحميل تخطيط النوافذ", L"Слоты сохранения/загрузки макета", L"Slots zum Speichern/Laden des Layouts", L"Slots para salvar/carregar o layout", L"Slots om de indeling op te slaan/laden", L"Sloty zapisu/wczytania ukladu", L"Pencere duzeni kaydet/yukle yuvalari"));
		if (lay) {
			lay->AddCommand(ID_MP_LAYOUT_SAVE0, L"Save slot 1");
			lay->AddCommand(ID_MP_LAYOUT_SAVE0 + 1, L"Save slot 2");
			lay->AddCommand(ID_MP_LAYOUT_SAVE0 + 2, L"Save slot 3");
			lay->AddCommand(ID_MP_LAYOUT_LOAD0, L"Load slot 1");
			lay->AddCommand(ID_MP_LAYOUT_LOAD0 + 1, L"Load slot 2");
			lay->AddCommand(ID_MP_LAYOUT_LOAD0 + 2, L"Load slot 3");
		}
		CCustomPopupMenu* aac = menu.AddSubMenu(L"AAC profile", LL14(L"AAC 書き出しのビットレート／プロファイルを選びます", L"Choose AAC export bitrate/profile", L"Choisir le debit/profil AAC", L"Scegli bitrate/profilo AAC", L"Elegir bitrate/perfil AAC", L"AAC 내보내기 비트레이트/프로필 선택", L"选择 AAC 导出码率/配置", L"اختيار معدل بت/ملف تعريف AAC", L"Выбрать битрейт/профиль AAC", L"AAC-Bitrate/Profil wahlen", L"Escolher bitrate/perfil AAC", L"AAC-bitrate/profiel kiezen", L"Wybierz bitrate/profil AAC", L"AAC bitrate/profili sec"));
		if (aac) {
			aac->AddCheck(ID_MP_AAC_PROF0, L"128 kbps", savedata.mpAacProfile == 0);
			aac->AddCheck(ID_MP_AAC_PROF1, L"192 kbps", savedata.mpAacProfile == 1);
			aac->AddCheck(ID_MP_AAC_PROF2, L"96 kbps low-latency", savedata.mpAacProfile == 2);
		}
	}
	menu.AddCommand(ID_MP_WEEKLY_SUMMARY,
		LL14(L"週次サマリ…", L"Weekly summary…", L"Resume hebdo…", L"Riepilogo…", L"Resumen…",
			L"주간 요약…", L"周汇总…", L"ملخص…", L"Сводка…", L"Wochenübersicht…", L"Resumo…", L"Weekoverzicht…", L"Podsumowanie…", L"Haftalik ozet…"),
			LL14(L"今週の再生・練習状況のサマリを表示します", L"Show this week's playback/practice summary", L"Afficher le resume de lecture/pratique de la semaine", L"Mostra il riepilogo di riproduzione/pratica della settimana", L"Mostrar el resumen de reproduccion/practica de la semana", L"이번 주 재생·연습 요약을 표시", L"显示本周播放/练习汇总", L"عرض ملخص التشغيل/التدريب لهذا الأسبوع", L"Показать сводку воспроизведения/практики за неделю", L"Wochenubersicht von Wiedergabe/Ubung anzeigen", L"Mostrar o resumo de reproducao/pratica da semana", L"Weekoverzicht van afspelen/oefenen tonen", L"Pokaz podsumowanie odtwarzania/cwiczen tygodnia", L"Bu haftanin calma/alisirma ozetini goster"));
	menu.AddCommand(ID_MP_PRACTICE_LOG, LL14(L"練習ログ追記", L"Append practice log", L"Journal pratique", L"Log pratica", L"Registro practica",
		L"연습 로그", L"练习日志", L"سجل التمرين", L"Журнал практики", L"Übungsprotokoll", L"Log de pratica", L"Oefenlog", L"Dziennik cwiczen", L"Alisirma gunlugu"),
		LL14(L"現在の練習内容をログに追記します", L"Append the current practice session to the log", L"Ajouter la session de pratique au journal", L"Aggiungi la sessione di pratica al log", L"Anadir la sesion de practica al registro", L"현재 연습 내용을 로그에 추가", L"将当前练习内容追加到日志", L"إلحاق جلسة التدريب بالسجل", L"Добавить текущую практику в журнал", L"Aktuelle Ubungseinheit ins Protokoll schreiben", L"Acrescentar a sessao de pratica ao log", L"Huidige oefensessie aan het log toevoegen", L"Dopisz biezaca sesje cwiczen do dziennika", L"Gecerli alistirmayi gunluge ekle"));
	menu.AddCommand(ID_MP_PRACTICE_PACK, LL14(L"練習パック書き出し…", L"Export practice pack…", L"Exporter pack pratique…", L"Esporta pack pratica…", L"Exportar pack practica…",
		L"연습 팩 내보내기…", L"导出练习包…", L"تصدير حزمة…", L"Экспорт пакета…", L"Übungspaket…", L"Exportar pacote…", L"Oefenpakket…", L"Pakiet cwiczen…", L"Alisirma paketi…"),
		LL14(L"練習用の区間・設定をパックとして書き出します", L"Export practice ranges and settings as a pack", L"Exporter plages/reglages de pratique en pack", L"Esporta intervalli/impostazioni pratica come pack", L"Exportar rangos/ajustes de practica como pack", L"연습 구간·설정을 팩으로 내보내기", L"将练习区间与设置导出为包", L"تصدير نطاقات/إعدادات التدريب كحزمة", L"Экспортировать диапазоны/настройки практики пакетом", L"Ubungsbereiche/-einstellungen als Paket exportieren", L"Exportar intervalos/ajustes de pratica como pacote", L"Oefenbereiken/-instellingen als pakket exporteren", L"Eksportuj zakresy/ustawienia cwiczen jako pakiet", L"Alisirma aralik/ayarlarini paket olarak disa aktar"));
	const UINT cmd = menu.Track(screenPt, this);
	// メニュー閉鎖時に LRC オフセットの端数(1–9ms)を捨てない
	if (lrcCtx.pendingMs != 0) {
		const int rem = lrcCtx.pendingMs;
		lrcCtx.pendingMs = 0;
		// 四捨五入相当: ±5ms 以上なら 10ms 単位へ繰り上げ
		int apply = 0;
		if (rem >= 5) apply = 10;
		else if (rem <= -5) apply = -10;
		if (apply) ShiftLrcMs(apply);
	}
	if (cmd)
		PostMessage(WM_COMMAND, cmd);
	// メニュー後はカスタム再描画のみ（BM_SETSTATE はテーマ無効時に空塗り→完全透過の原因）
	if (m_toolsToggle.GetSafeHwnd()) {
		m_toolsToggle.Invalidate(FALSE);
		m_toolsToggle.UpdateWindow();
	}
}

void CMediaPlayerDlg::ApplySleepTimer(int minutes)
{
	if (minutes < 0) minutes = 0;
	if (minutes > 240) minutes = 240;
	// 同値の再適用で残り時間をフルに振り直さない（武装中の同分数）
	if (minutes == savedata.mpSleepMin && minutes > 0 && m_sleepEndTick != 0)
		return;
	if (minutes == 0 && savedata.mpSleepMin == 0 && m_sleepEndTick == 0)
		return;
	savedata.mpSleepMin = minutes;
	KillTimer(9);
	m_sleepEndTick = 0;
	if (minutes > 0) {
		SetTimer(9, (UINT)minutes * 60 * 1000, NULL);
		m_sleepEndTick = GetTickCount64() + (ULONGLONG)minutes * 60ULL * 1000ULL;
	}
	MpPersistSavedataQuick();
	UpdateQueueChrome();
}

void CMediaPlayerDlg::OpenTagEditForSelection()
{
	const int pc = GetSelectedPcIndex();
	if (pc < 0 || !pl || !pl->pc) return;
	CTagEditDlg dlg(this);
	dlg.pc = pl->pc[pc];
	dlg.multiFile = false;
	dlg.DoModal();
}

void CMediaPlayerDlg::CycleRatingForDisp(int disp)
{
	const int pc = MpDispToPc(this, disp);
	if (pc < 0 || !pl || pc >= pl->playcnt) return;
	const playlistdata0& d = pl->pc[pc];
	int r = ProAudio_GetRating(MpCurListName(), d.fol, d.sub, d.ret2);
	r = (r + 1) % 6;
	ProAudio_SetRating(MpCurListName(), d.fol, d.sub, d.ret2, r);
	m_list.RedrawItems(disp, disp);
}

void CMediaPlayerDlg::ShiftLrcMs(int deltaMs)
{
	if (!og || og->lrcnum <= 0) return;
	const int dCentis = deltaMs / 10; // lrctm は 1/100 秒
	for (int i = 0; i < og->lrcnum; ++i) {
		int t = (int)og->lrctm[i] + dCentis;
		if (t < 0) t = 0;
		og->lrctm[i] = (DWORD)t;
	}
	if (m_lrcView.GetSafeHwnd()) {
		const int n = og->lrcnum;
		m_lrcView.SetLines(og->lrc, n, og->lrctm, og->lrcnum);
		m_lrcView.Invalidate(FALSE);
	}
	SyncDesktopLyricsIfOpen();
}

void CMediaPlayerDlg::OnFiltUnplayed() { m_activeSmartId = -1; m_smartFilt = (m_smartFilt == 1) ? 0 : 1; RefreshList(TRUE); }
void CMediaPlayerDlg::OnFiltMissing() { m_activeSmartId = -1; m_smartFilt = (m_smartFilt == 2) ? 0 : 2; RefreshList(TRUE); }
void CMediaPlayerDlg::OnFiltClear() { m_smartFilt = 0; m_activeSmartId = -1; RefreshList(TRUE); }

void CMediaPlayerDlg::OnMissManage()
{
	if (!pl || !pl->pc || pl->playcnt <= 0) return;
	KickMissScan();
	std::vector<int> missing;
	const int n = (m_missCap < pl->playcnt) ? m_missCap : pl->playcnt;
	for (int i = 0; i < n; ++i)
		if (m_miss && m_miss[i] == 1) missing.push_back(i);
	if (missing.empty()) {
		MessageBox(LL14(L"欠損ファイルはありません。", L"No missing files.", L"Aucun fichier manquant.", L"Nessun file mancante.", L"Sin archivos faltantes.", L"결손 파일 없음.", L"没有缺失文件。", L"لا ملفات مفقودة.", L"Нет отсутствующих.", L"Keine fehlenden Dateien.", L"Sem arquivos ausentes.", L"Geen ontbrekende bestanden.", L"Brak brakujacych.", L"Eksik dosya yok."), LL14(L"欠損", L"Missing", L"Manquants", L"Mancanti", L"Faltantes", L"결손", L"缺失", L"مفقود", L"Отсутствующие", L"Fehlend", L"Ausentes", L"Ontbrekend", L"Brakujace", L"Eksik"), MB_OK);
		return;
	}
	CMissingFilesDlg dlg(pl, missing, this);
	if (dlg.DoModal() == IDOK && !dlg.m_toDelete.empty()) {
		pl->DelByIndices(dlg.m_toDelete);
		RefreshList(TRUE);
	}
}

void CMediaPlayerDlg::OnSmartEdit()
{
	CMpSmartPlaylistDlg dlg(this);
	if (dlg.DoModal() == IDOK && dlg.m_appliedIndex >= 0) {
		m_activeSmartId = dlg.m_appliedIndex;
		m_smartFilt = 0;
		RefreshList(TRUE);
	}
	if (m_libTreeBuilt) LibRebuildTree();
}

void CMediaPlayerDlg::OnSmartApplyId(UINT nID)
{
	const int sid = (int)(nID - ID_MP_SMART_BASE);
	if (sid < 0 || sid >= MpSmart_Count()) return;
	if (m_activeSmartId == sid) m_activeSmartId = -1;
	else m_activeSmartId = sid;
	m_smartFilt = 0;
	RefreshList(TRUE);
}

void CMediaPlayerDlg::OnQueueShow()
{
	CMpQueueDlg dlg(this);
	dlg.DoModal();
	UpdateQueueChrome();
}

void CMediaPlayerDlg::OnExportAbNow()
{
	if (m_abApos < 0 || m_abBpos <= m_abApos) {
		MessageBox(LL14(L"先に A-B を設定してください。", L"Set A-B first.", L"Definissez A-B d'abord.", L"Imposta prima A-B.", L"Configure A-B primero.", L"먼저 A-B를 설정하세요.", L"请先设置 A-B。", L"عيّن A-B أولاً.", L"Сначала задайте A-B.", L"Zuerst A-B setzen.", L"Defina A-B primeiro.", L"Stel eerst A-B in.", L"Najpierw ustaw A-B.", L"Once A-B ayarlayin."), LL14(L"A-B書き出し", L"Export A-B", L"Export A-B", L"Esporta A-B", L"Exportar A-B", L"A-B보내기", L"导出A-B", L"تصدير A-B", L"Экспорт A-B", L"A-B exportieren", L"Exportar A-B", L"A-B exporteren", L"Eksport A-B", L"A-B disa aktar"), MB_OK);
		return;
	}
	if (!og || !pl || !pl->pc) return;
	// A-B フレームは演奏中曲のもの。選択行が違うと誤書き出しになるので演奏中行を使う。
	extern int plcnt;
	int pc = -1;
	if (plcnt >= 0 && plcnt < pl->playcnt)
		pc = plcnt;
	else if (pl->pnt >= 0 && pl->pnt < pl->playcnt)
		pc = pl->pnt;
	const int sel = GetSelectedPcIndex();
	if (sel >= 0 && sel < pl->playcnt && pc >= 0 && sel != pc) {
		MessageBox(LL14(
			L"A-B は演奏中の曲に対するものです。\n選択行が違うため書き出しを中止しました。",
			L"A-B belongs to the playing track.\nSelection differs; export cancelled.",
			L"A-B concerne la piste en lecture.\nSelection differente; export annule.",
			L"A-B e della traccia in riproduzione.\nSelezione diversa; esportazione annullata.",
			L"A-B es de la pista en reproduccion.\nSeleccion distinta; exportacion cancelada.",
			L"A-B는 재생 중인 곡 기준입니다.\n선택 행이 달라 내보내기를 취소했습니다.",
			L"A-B 属于正在播放的曲目。\n选择行不同，已取消导出。",
			L"A-B للمسار قيد التشغيل.\nالتحديد مختلف؛ أُلغي التصدير.",
			L"A-B относится к играющему треку.\nВыбор другой; экспорт отменён.",
			L"A-B gehoert zum spielenden Titel.\nAuswahl weicht ab; Export abgebrochen.",
			L"A-B e da faixa em reproducao.\nSelecao diferente; exportacao cancelada.",
			L"A-B hoort bij het spelende nummer.\nSelectie wijkt af; export geannuleerd.",
			L"A-B nalezy do odtwarzanej sciezki.\nInny wybor; anulowano eksport.",
			L"A-B calinan parcaya aittir.\nSecim farkli; disa aktarma iptal."),
			LL14(L"A-B書き出し", L"Export A-B", L"Export A-B", L"Esporta A-B", L"Exportar A-B", L"A-B보내기", L"导出A-B", L"تصدير A-B", L"Экспорт A-B", L"A-B exportieren", L"Exportar A-B", L"A-B exporteren", L"Eksport A-B", L"A-B disa aktar"),
			MB_OK | MB_ICONWARNING);
		return;
	}
	if (pc < 0 || pc >= pl->playcnt) {
		if (sel >= 0 && sel < pl->playcnt) pc = sel;
		else return;
	}
	playlistdata0& item = pl->pc[pc];
	CString path = item.fol;
	const int dot = path.ReverseFind(_T('.'));
	CString out = (dot > 0) ? (path.Left(dot) + _T("_ab.wav")) : (path + _T("_ab.wav"));
	if (PathFileExists(out)) {
		CString ask;
		ask.Format(LL14(L"%s は既に存在します。上書きしますか？", L"%s already exists. Overwrite?", L"%s existe deja. Ecraser ?", L"%s esiste gia. Sovrascrivere?", L"%s ya existe. ¿Sobrescribir?", L"%s 이(가) 이미 있습니다. 덮어쓸까요?", L"%s 已存在。要覆盖吗？", L"%s موجود بالفعل. هل تريد الاستبدال؟", L"%s уже существует. Перезаписать?", L"%s existiert bereits. Ueberschreiben?", L"%s ja existe. Sobrescrever?", L"%s bestaat al. Overschrijven?", L"%s juz istnieje. Nadpisac?", L"%s zaten var. Uzerine yazilsin mi?"), (LPCTSTR)out);
		if (MessageBox(ask, LL14(L"A-B書き出し", L"Export A-B", L"Export A-B", L"Esporta A-B", L"Exportar A-B", L"A-B보내기", L"导出A-B", L"تصدير A-B", L"Экспорт A-B", L"A-B exportieren", L"Exportar A-B", L"A-B exporteren", L"Eksport A-B", L"A-B disa aktar"), MB_YESNO | MB_ICONQUESTION) != IDYES)
			return;
	}
	WavExportOptions opts = {};
	opts.startFrame = m_abApos;
	opts.endFrame = m_abBpos;
	const BOOL ok = og->ExportToWav(&item, out, 1, &opts, TRUE);
	MessageBox(ok
		? LL14(L"A-B を WAV に書き出しました。", L"Exported A-B to WAV.", L"A-B exporte en WAV.", L"A-B esportato in WAV.", L"A-B exportado a WAV.", L"A-B를 WAV로 내보냄.", L"已将 A-B 导出为 WAV。", L"تم تصدير A-B إلى WAV.", L"A-B экспортирован в WAV.", L"A-B als WAV exportiert.", L"A-B exportado para WAV.", L"A-B geexporteerd naar WAV.", L"Wyeksportowano A-B do WAV.", L"A-B WAV olarak disa aktarildi.")
		: LL14(L"書き出しに失敗しました。", L"Export failed.", L"Echec export.", L"Esportazione non riuscita.", L"Error al exportar.", L"내보내기 실패.", L"导出失败。", L"فشل التصدير.", L"Ошибка экспорта.", L"Export fehlgeschlagen.", L"Falha na exportacao.", L"Export mislukt.", L"Eksport nie powiodl sie.", L"Disa aktarma basarisiz."),
		LL14(L"A-B書き出し", L"Export A-B", L"Export A-B", L"Esporta A-B", L"Exportar A-B", L"A-B보내기", L"导出A-B", L"تصدير A-B", L"Экспорт A-B", L"A-B exportieren", L"Exportar A-B", L"A-B exporteren", L"Eksport A-B", L"A-B disa aktar"), ok ? MB_OK : MB_OK | MB_ICONWARNING);
}

void CMediaPlayerDlg::OnNormScan()
{
	if (!og || !pl || !pl->pc) return;
	const int pc = GetSelectedPcIndex();
	if (pc < 0 || pc >= pl->playcnt) return;
	playlistdata0& item = pl->pc[pc];
	if (item.sub == -2) {
		MessageBox(LL14(
			L"動画の正規化計測には対応していません。\n先に音声抽出してください。",
			L"Normalize measure is not supported for video.\nExtract audio first.",
			L"Mesure non prise en charge pour la video.\nExtrayez l'audio d'abord.",
			L"Misura non supportata per video.\nEstrarre prima l'audio.",
			L"Medicion no compatible con video.\nExtraiga el audio primero.",
			L"동영상 정규화 측정은 지원되지 않습니다.\n먼저 오디오를 추출하세요.",
			L"不支持对视频做标准化测量。\n请先提取音频。",
			L"القياس غير مدعوم للفيديو.\nاستخرج الصوت أولاً.",
			L"Измерение для видео не поддерживается.\nСначала извлеките аудио.",
			L"Messung fuer Video nicht unterstuetzt.\nZuerst Audio extrahieren.",
			L"Medicao nao suportada para video.\nExtraia o audio primeiro.",
			L"Meting niet ondersteund voor video.\nExtraheer eerst audio.",
			L"Pomiar nieobslugiwany dla wideo.\nNajpierw wyodrebnij audio.",
			L"Video icin olcum desteklenmiyor.\nOnce sesi cikarin."),
			LL14(L"ノーマライズ", L"Normalize", L"Normaliser", L"Normalizza", L"Normalizar", L"정규화", L"标准化", L"تطبيع", L"Нормализация", L"Normalisieren", L"Normalizar", L"Normaliseren", L"Normalizuj", L"Normalize"),
			MB_OK | MB_ICONINFORMATION);
		return;
	}
	const int rgTargetBak = savedata.pro_rg_target;
	savedata.pro_rg_target = savedata.mpNormTargetLufs;
	ProAudio_SetCurrentSongKey(MpCurListName(), item.fol, item.sub, item.ret2);
	ProAudio_LoudnessReset();
	CString path = item.fol;
	const int dot = path.ReverseFind(_T('.'));
	const CString tmp = (dot > 0) ? (path.Left(dot) + _T("_normscan.tmp.wav")) : (path + _T("_normscan.tmp.wav"));
	const BOOL ok = og->ExportToWav(&item, tmp, 1, NULL, FALSE);
	ProAudio_LoudnessCommitCurrentSong();
	::DeleteFile(tmp);
	savedata.pro_rg_target = rgTargetBak;
	ProSongExtra e;
	CString msg;
	if (ok && ProAudio_GetExtra(MpCurListName(), item.fol, item.sub, item.ret2, e) && e.rgValid)
		msg.Format(_T("peak=%.3f  gain=%.2f dB  target=%d LUFS"), e.trackPeak, e.trackGainDb, savedata.mpNormTargetLufs);
	else
		msg = LL14(L"計測に失敗しました。", L"Measure failed.", L"Echec mesure.", L"Misura non riuscita.", L"Medicion fallida.", L"측정 실패.", L"测量失败。", L"فشل القياس.", L"Измерение не удалось.", L"Messung fehlgeschlagen.", L"Medicao falhou.", L"Meting mislukt.", L"Pomiar nieudany.", L"Olcum basarisiz.");
	MessageBox(msg, LL14(L"ノーマライズ", L"Normalize", L"Normaliser", L"Normalizza", L"Normalizar", L"정규화", L"标准化", L"تطبيع", L"Нормализация", L"Normalisieren", L"Normalizar", L"Normaliseren", L"Normalizuj", L"Normalize"), MB_OK | MB_ICONINFORMATION);
}

void CMediaPlayerDlg::OnNormLufs14() { savedata.mpNormTargetLufs = -14; savedata.pro_rg_target = -14; MpPersistSavedataQuick(); }
void CMediaPlayerDlg::OnNormLufs16() { savedata.mpNormTargetLufs = -16; savedata.pro_rg_target = -16; MpPersistSavedataQuick(); }
void CMediaPlayerDlg::OnNormLufs18() { savedata.mpNormTargetLufs = -18; savedata.pro_rg_target = -18; MpPersistSavedataQuick(); }

void CMediaPlayerDlg::OnSleepCustom()
{
	class CSleepMinDlg : public CDialog {
	public:
		int m_mins;
		CSleepMinDlg(CWnd* p) : CDialog(IDD_FILENAME, p), m_mins(30) {}
		virtual BOOL OnInitDialog() {
			CDialog::OnInitDialog();
			SetWindowText(LL14(L"スリープ", L"Sleep", L"Veille", L"Sleep", L"Suspensión", L"슬립", L"睡眠", L"نوم", L"Сон", L"Schlaf", L"Sono", L"Slaap", L"Sen", L"Uyku"));
			CString s; s.Format(_T("%d"), m_mins);
			SetDlgItemText(IDC_EDIT1, s);
			if (CWnd* w = GetDlgItem(IDC_EDIT2)) w->ShowWindow(SW_HIDE);
			if (CWnd* w = GetDlgItem(IDC_EDIT3)) w->ShowWindow(SW_HIDE);
			if (CWnd* w = GetDlgItem(IDC_EDIT4)) w->ShowWindow(SW_HIDE);
			if (CWnd* w = GetDlgItem(IDC_FILENAME_LBL_ART)) w->ShowWindow(SW_HIDE);
			if (CWnd* w = GetDlgItem(IDC_FILENAME_LBL_ALB)) w->ShowWindow(SW_HIDE);
			if (CWnd* w = GetDlgItem(IDC_FILENAME_LBL_FOL)) w->ShowWindow(SW_HIDE);
			SetDlgItemText(IDC_FILENAME_LBL_NAME, LL14(L"分 (1–240)", L"min (1–240)", L"min (1–240)", L"min (1–240)", L"min (1–240)", L"분 (1–240)", L"分钟 (1–240)", L"دقيقة (1–240)", L"мин (1–240)", L"Min (1–240)", L"min (1–240)", L"min (1–240)", L"min (1–240)", L"dk (1–240)"));
			return TRUE;
		}
		virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) {
			if (LOWORD(wParam) == ID_OK) {
				CString s; GetDlgItemText(IDC_EDIT1, s);
				m_mins = _ttoi(s);
				EndDialog(IDOK);
				return TRUE;
			}
			return CDialog::OnCommand(wParam, lParam);
		}
	};
	CSleepMinDlg dlg(this);
	dlg.m_mins = (savedata.mpSleepMin > 0) ? savedata.mpSleepMin : 30;
	if (dlg.DoModal() != IDOK) return;
	int mins = dlg.m_mins;
	if (mins < 1) mins = 1;
	if (mins > 240) mins = 240;
	ApplySleepTimer(mins);
}

void CMediaPlayerDlg::OnJacketSaveCover()
{
	if (!pl) return;
	const int pc = GetSelectedPcIndex();
	if (pc < 0 || pc >= pl->playcnt) return;
	CString fol = pl->pc[pc].fol;
	const int slash = max(fol.ReverseFind(_T('\\')), fol.ReverseFind(_T('/')));
	if (slash < 0) return;
	CString dir = fol.Left(slash + 1);
	CFileDialog fd(TRUE, _T("jpg"), NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		_T("Images (*.jpg;*.jpeg;*.png;*.bmp)|*.jpg;*.jpeg;*.png;*.bmp|All (*.*)|*.*||"), this);
	if (fd.DoModal() != IDOK) return;
	CString dest = dir + _T("cover.jpg");
	if (!::CopyFile(fd.GetPathName(), dest, FALSE))
		MessageBox(LL14(L"cover.jpg の保存に失敗しました。", L"Failed to save cover.jpg.", L"Echec enregistrement cover.jpg.", L"Salvataggio cover.jpg non riuscito.", L"Error al guardar cover.jpg.", L"cover.jpg 저장 실패.", L"保存 cover.jpg 失败。", L"فشل حفظ cover.jpg.", L"Не удалось сохранить cover.jpg.", L"cover.jpg speichern fehlgeschlagen.", L"Falha ao salvar cover.jpg.", L"cover.jpg opslaan mislukt.", L"Nie udalo sie zapisac cover.jpg.", L"cover.jpg kaydedilemedi."), LL14(L"カバー", L"Cover", L"Pochette", L"Copertina", L"Caratula", L"커버", L"封面", L"غلاف", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Okladka", L"Kapak"), MB_OK | MB_ICONWARNING);
	else {
		OnJacketReloadAlt();
		MessageBox(LL14(L"cover.jpg を保存しました。", L"Saved cover.jpg.", L"cover.jpg enregistre.", L"cover.jpg salvato.", L"cover.jpg guardado.", L"cover.jpg 저장됨.", L"已保存 cover.jpg。", L"تم حفظ cover.jpg.", L"cover.jpg сохранён.", L"cover.jpg gespeichert.", L"cover.jpg salvo.", L"cover.jpg opgeslagen.", L"Zapisano cover.jpg.", L"cover.jpg kaydedildi."), LL14(L"カバー", L"Cover", L"Pochette", L"Copertina", L"Caratula", L"커버", L"封面", L"غلاف", L"Обложка", L"Cover", L"Capa", L"Omslag", L"Okladka", L"Kapak"), MB_OK);
	}
}

void CMediaPlayerDlg::OnQueueAdd() { QueueAdd(GetSelectedPcIndex(), FALSE); }
void CMediaPlayerDlg::OnQueuePlayNext() { QueueAdd(GetSelectedPcIndex(), TRUE); }
void CMediaPlayerDlg::OnQueueClear() { QueueClear(); }

void CMediaPlayerDlg::OnDupesScan()
{
	CMpDupesDlg dlg(this);
	dlg.DoModal();
	RefreshList(TRUE);
}

void CMediaPlayerDlg::OnFolderSyncDiff()
{
	CString folder = m_libSelFolder;
	if (folder.IsEmpty()) {
		MessageBox(LL14(L"ライブラリでフォルダを選択してください。", L"Select a library folder first.", L"Selectionnez un dossier.", L"Seleziona una cartella.", L"Seleccione una carpeta.", L"라이브러리에서 폴더를 선택하세요.", L"请先选择媒体库文件夹。", L"حدد مجلدًا أولاً.", L"Сначала выберите папку.", L"Zuerst Ordner waehlen.", L"Selecione uma pasta.", L"Selecteer eerst een map.", L"Najpierw wybierz folder.", L"Once bir klasor secin."), LL14(L"同期", L"Sync", L"Sync", L"Sinc", L"Sincr.", L"동기화", L"同步", L"مزامنة", L"Синхр.", L"Sync", L"Sinc.", L"Sync", L"Sync", L"Senkron"), MB_OK);
		return;
	}
	CMpFolderSyncDlg dlg(this, folder);
	dlg.DoModal();
	RefreshList(TRUE);
}

void CMediaPlayerDlg::OnLrcPlus50() { ShiftLrcMs(50); }
void CMediaPlayerDlg::OnLrcMinus50() { ShiftLrcMs(-50); }
void CMediaPlayerDlg::OnLrcPlus10() { ShiftLrcMs(10); }
void CMediaPlayerDlg::OnLrcMinus10() { ShiftLrcMs(-10); }
void CMediaPlayerDlg::OnLrcPlus100() { ShiftLrcMs(100); }
void CMediaPlayerDlg::OnLrcMinus100() { ShiftLrcMs(-100); }

void CMediaPlayerDlg::OnLrcSave()
{
	if (!og || og->lrcnum < 2) {
		MessageBox(LL14(L"保存する歌詞がありません。", L"No lyrics to save.", L"Aucune parole a enregistrer.", L"Nessun testo da salvare.", L"No hay letra que guardar.", L"저장할 가사가 없습니다.", L"没有可保存的歌词。", L"لا كلمات للحفظ.", L"Нет текста для сохранения.", L"Kein Text zum Speichern.", L"Sem letra para salvar.", L"Geen tekst om op te slaan.", L"Brak tekstu do zapisu.", L"Kaydedilecek soz yok."), LL14(L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC"), MB_OK | MB_ICONINFORMATION);
		return;
	}
	extern CString filen;
	CString path;
	if (!filen.IsEmpty()) {
		const int dot = filen.ReverseFind(_T('.'));
		path = (dot > 0) ? (filen.Left(dot) + _T(".lrc")) : (filen + _T(".lrc"));
	}
	CFileDialog fd(FALSE, _T("lrc"), path,
		OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_ENABLESIZING,
		_T("Lyrics (*.lrc)|*.lrc|All (*.*)|*.*||"), this);
	if (fd.DoModal() != IDOK) return;

	CString body;
	const int lastLine = og->lrcnum - 1;
	for (int i = 0; i < lastLine; ++i) {
		if (og->lrc[i] == _T("...")) continue;
		const DWORD t = og->lrctm[i];
		const int mm = (int)(t / 6000);
		const int rem = (int)(t % 6000);
		const int ss = rem / 100;
		const int xx = rem % 100;
		CString line;
		line.Format(_T("[%02d:%02d.%02d]%s\r\n"), mm, ss, xx, (LPCTSTR)og->lrc[i]);
		body += line;
	}
	try {
		CFile out;
		if (!out.Open(fd.GetPathName(), CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive))
			throw 0;
		const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
		out.Write(bom, 3);
		CStringA utf8 = CW2A(body, CP_UTF8);
		if (utf8.GetLength() > 0)
			out.Write(utf8, utf8.GetLength());
		out.Close();
	}
	catch (...) {
		MessageBox(LL14(L"LRC の書き込みに失敗しました。", L"Failed to write LRC file.", L"Echec ecriture LRC.", L"Scrittura LRC non riuscita.", L"Error al escribir LRC.", L"LRC 저장 실패.", L"写入 LRC 失败。", L"فشل كتابة LRC.", L"Не удалось записать LRC.", L"LRC schreiben fehlgeschlagen.", L"Falha ao gravar LRC.", L"LRC schrijven mislukt.", L"Zapis LRC nie powiódł się.", L"LRC yazilamadi."), LL14(L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC"), MB_OK | MB_ICONWARNING);
		return;
	}
	MessageBox(LL14(L"LRC を保存しました。", L"LRC saved.", L"LRC enregistre.", L"LRC salvato.", L"LRC guardado.", L"LRC 저장됨.", L"LRC 已保存。", L"تم حفظ LRC.", L"LRC сохранён.", L"LRC gespeichert.", L"LRC salvo.", L"LRC opgeslagen.", L"Zapisano LRC.", L"LRC kaydedildi."), LL14(L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC", L"LRC"), MB_OK | MB_ICONINFORMATION);
}

void CMediaPlayerDlg::OnDeskLrcToggle()
{
	if (IsDesktopLyricsOpen()) {
		CloseDesktopLyricsIfOpen();
		savedata.deskLrcOn = 0;
	}
	else {
		OpenDesktopLyricsModeless(this);
		savedata.deskLrcOn = 1;
	}
	MpPersistSavedataQuick();
	UpdateDeskLrcBtnChrome();
}

void CMediaPlayerDlg::UpdateDeskLrcBtnChrome()
{
	if (!m_deskLrc.GetSafeHwnd()) return;
	// 実ウィンドウの有無だけを見る（閉じ処理中は deskLrcOn=0 でも HWND が残っているため）
	const BOOL on = IsDesktopLyricsOpen();
	m_deskLrc.SetWindowText(LL14(
		on ? L"窓●" : L"歌詞窓",
		on ? L"Win●" : L"Lyrics",
		on ? L"Par●" : L"Paroles",
		on ? L"Tes●" : L"Testi",
		on ? L"Let●" : L"Letra",
		on ? L"창●" : L"가사창",
		on ? L"窗●" : L"歌词窗",
		on ? L"ناف●" : L"كلمات",
		on ? L"Ок●" : L"Текст",
		on ? L"Fn●" : L"Text",
		on ? L"Jan●" : L"Letra",
		on ? L"Ven●" : L"Tekst",
		on ? L"Ok●" : L"Tekst",
		on ? L"Pen●" : L"Soz"));
	if (on)
		m_deskLrc.SetGradation(RGB(200, 255, 230), RGB(120, 220, 180), 0, TRUE);
	else
		m_deskLrc.SetGradation(RGB(235, 245, 255), RGB(190, 215, 245), 0, TRUE);
	m_deskLrc.Invalidate(FALSE);
}

void CMediaPlayerDlg::OnTagEdit() { OpenTagEditForSelection(); }

void CMediaPlayerDlg::OnJacketReloadAlt()
{
	extern CString filen;
	if (!og || filen.IsEmpty()) return;
	PlJakDiskForget(filen);
	og->LoadJacket(filen);
	InvalidateSidePanels();
	InvalidateRect(&m_bannerRect, FALSE);
}

void CMediaPlayerDlg::OnJacketPickCover()
{
	extern CString filen;
	if (!og || filen.IsEmpty()) return;
	CString dir = filen;
	const int slash = max(dir.ReverseFind(_T('\\')), dir.ReverseFind(_T('/')));
	if (slash > 0) dir = dir.Left(slash + 1);
	else dir.Empty();
	CFileDialog fd(TRUE, _T("jpg"), dir + _T("cover.jpg"),
		OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
		_T("Images (*.jpg;*.jpeg;*.png;*.bmp)|*.jpg;*.jpeg;*.png;*.bmp|All|*.*||"), this);
	if (fd.DoModal() != IDOK) return;
	if (!og->img.IsNull()) og->img.Destroy();
	if (SUCCEEDED(og->img.Load(fd.GetPathName())) && !og->img.IsNull()) {
		og->jx = og->img.GetWidth();
		og->jy = og->img.GetHeight();
		og->jxy = (og->jy > 0) ? ((double)og->jx / (double)og->jy) : 1.0;
	}
	InvalidateSidePanels();
	InvalidateRect(&m_bannerRect, FALSE);
}

void CMediaPlayerDlg::OnExportAb()
{
	// 旧実装はグローバルに範囲だけ置いて未消費だった。即 A-B WAV 書き出しへ。
	OnExportAbNow();
}

void CMediaPlayerDlg::OnAbPackExport()
{
	if (!og || !pl || !pl->pc) return;
	const int pc = GetSelectedPcIndex();
	if (pc < 0 || pc >= pl->playcnt) return;
	playlistdata0& item = pl->pc[pc];

	struct MpSeg { int start; int end; } segs[16];
	int nSeg = 0;
	if (m_abApos >= 0 && m_abBpos > m_abApos && nSeg < 16) {
		segs[nSeg].start = m_abApos;
		segs[nSeg].end = m_abBpos;
		nSeg++;
	}

	ProAudio_SetCurrentSongKey(MpCurListName(), item.fol, item.sub, item.ret2);
	ProAudio_CueLoadForCurrent();
	int cueFrames[PRO_CUE_MAX];
	int cueN = 0;
	for (int i = 0; i < ProAudio_CueCount() && cueN < PRO_CUE_MAX; ++i) {
		ProCue c;
		if (ProAudio_CueGet(i, c) && c.frame >= 0)
			cueFrames[cueN++] = c.frame;
	}
	for (int i = 0; i < cueN - 1; ++i) {
		for (int j = i + 1; j < cueN; ++j) {
			if (cueFrames[j] < cueFrames[i]) {
				const int t = cueFrames[i]; cueFrames[i] = cueFrames[j]; cueFrames[j] = t;
			}
		}
	}
	int endHz = wavbit_sample_Hz > 0 ? wavbit_sample_Hz : 44100;
	int endDefault = item.loop2 > 0 ? (item.loop1 + item.loop2) : (item.time > 0 ? item.time * endHz : 0);
	if (endDefault <= 0) endDefault = endHz * 300;
	for (int i = 0; i < cueN && nSeg < 16; ++i) {
		segs[nSeg].start = cueFrames[i];
		segs[nSeg].end = (i + 1 < cueN) ? cueFrames[i + 1] : endDefault;
		if (segs[nSeg].end > segs[nSeg].start) nSeg++;
	}
	if (nSeg <= 0) {
		MessageBox(LL14(L"A-B またはキューがありません。", L"No A-B or cues to export.", L"Pas de plage A-B ou cues.", L"Nessun A-B o cue.", L"Sin A-B o cues.", L"A-B 또는 큐 없음.", L"无 A-B 或标记。", L"لا A-B أو cues.", L"Нет A-B или cue.", L"Kein A-B oder Cues.", L"Sem A-B ou cues.", L"Geen A-B of cues.", L"Brak A-B lub cue.", L"A-B veya cue yok."), LL14(L"書き出しパック", L"Export pack", L"Pack export", L"Pacchetto export", L"Paquete export", L"내보내기 팩", L"导出包", L"حزمة تصدير", L"Пакет экспорта", L"Export-Paket", L"Pacote export", L"Exportpakket", L"Pakiet eksportu", L"Disa aktarma paketi"), MB_OK);
		return;
	}

	CString path = item.fol;
	int dot = path.ReverseFind(_T('.'));
	CString stem = (dot > 0) ? path.Left(dot) : path;
	BOOL ok = TRUE;
	BOOL overwriteAsked = FALSE;
	BOOL overwriteAll = TRUE;
	for (int i = 0; i < nSeg; ++i) {
		WavExportOptions opts = {};
		opts.startFrame = segs[i].start;
		opts.endFrame = segs[i].end;
		CString out;
		out.Format(_T("%s_seg%02d.wav"), (LPCTSTR)stem, i + 1);
		if (PathFileExists(out)) {
			if (!overwriteAsked) {
				overwriteAsked = TRUE;
				CString ask;
				ask.Format(LL14(L"既存の区間 WAV があります。上書きしますか？\n例: %s", L"Segment WAV files already exist. Overwrite?\nE.g. %s", L"Des WAV de segments existent. Ecraser ?\nEx. %s", L"WAV segmenti gia presenti. Sovrascrivere?\nEs. %s", L"Ya hay WAV de segmentos. ¿Sobrescribir?\nEj. %s", L"구간 WAV가 이미 있습니다. 덮어쓸까요?\n예: %s", L"已有区间 WAV。要覆盖吗？\n例: %s", L"ملفات WAV للمقاطع موجودة. هل تريد الاستبدال؟\nمثال: %s", L"Файлы сегментов уже есть. Перезаписать?\nНапр. %s", L"Segment-WAVs existieren bereits. Ueberschreiben?\nZ.B. %s", L"WAVs de segmentos ja existem. Sobrescrever?\nEx. %s", L"Segment-WAVs bestaan al. Overschrijven?\nBv. %s", L"Pliki segmentow juz istnieja. Nadpisac?\nNp. %s", L"Bolum WAV dosyalari var. Uzerine yazilsin mi?\nOrn. %s"), (LPCTSTR)out);
				overwriteAll = (MessageBox(ask, LL14(L"書き出しパック", L"Export pack", L"Pack export", L"Pacchetto export", L"Paquete export", L"내보내기 팩", L"导出包", L"حزمة تصدير", L"Пакет экспорта", L"Export-Paket", L"Pacote export", L"Exportpakket", L"Pakiet eksportu", L"Disa aktarma paketi"), MB_YESNO | MB_ICONQUESTION) == IDYES);
			}
			if (!overwriteAll) continue;
		}
		ok = og->ExportToWav(&item, out, 1, &opts, TRUE) && ok;
	}
	CString msg;
	msg.Format(LL14(L"%d 区間を書き出しました。", L"Exported %d segments.", L"%d segments exportes.", L"%d segmenti esportati.", L"%d segmentos exportados.", L"%d 구간 내보냄.", L"已导出 %d 段。", L"%d مقاطع.", L"Экспорт %d сегм.", L"%d Segmente exportiert.", L"%d segmentos.", L"%d segmenten.", L"Wyeksportowano %d.", L"%d bolum disa aktarildi."), nSeg);
	MessageBox(msg, ok ? LL14(L"書き出しパック", L"Export pack", L"Pack export", L"Pacchetto export", L"Paquete export", L"내보내기 팩", L"导出包", L"حزمة تصدير", L"Пакет экспорта", L"Export-Paket", L"Pacote export", L"Exportpakket", L"Pakiet eksportu", L"Disa aktarma paketi") : LL14(L"書き出しパック", L"Export pack", L"Pack export", L"Pacchetto export", L"Paquete export", L"내보내기 팩", L"导出包", L"حزمة تصدير", L"Пакет экспорта", L"Export-Paket", L"Pacote export", L"Exportpakket", L"Pakiet eksportu", L"Disa aktarma paketi"), ok ? MB_OK : MB_OK | MB_ICONWARNING);
}

void CMediaPlayerDlg::OnNormBatch()
{
	if (!og || !pl || !pl->pc) return;
	int pcs[256];
	int nPc = 0;
	for (int d = -1; (d = m_list.GetNextItem(d, LVNI_SELECTED)) != -1 && nPc < 256; ) {
		const int pc = MpDispToPc(this, d);
		if (pc >= 0 && pc < pl->playcnt) pcs[nPc++] = pc;
	}
	if (nPc == 0) {
		const int one = GetSelectedPcIndex();
		if (one >= 0) pcs[nPc++] = one;
	}
	if (nPc <= 0) return;

	const int rgModeBak = savedata.pro_rg_mode;
	const int rgTargetBak = savedata.pro_rg_target;
	const int limitBak = savedata.pro_export_limit;
	savedata.pro_rg_mode = 1;
	savedata.pro_rg_target = savedata.mpNormTargetLufs;
	savedata.pro_export_limit = 1;

	BOOL ok = TRUE;
	BOOL overwriteAsked = FALSE;
	BOOL overwriteAll = TRUE;
	for (int i = 0; i < nPc; ++i) {
		playlistdata0& item = pl->pc[pcs[i]];
		ProAudio_SetCurrentSongKey(MpCurListName(), item.fol, item.sub, item.ret2);
		ProSongExtra ex;
		const BOOL hadRg = ProAudio_GetExtra(MpCurListName(), item.fol, item.sub, item.ret2, ex) && ex.rgValid;
		CString path = item.fol;
		const int dot = path.ReverseFind(_T('.'));
		const CString out = (dot > 0) ? (path.Left(dot) + _T("_norm.wav")) : (path + _T("_norm.wav"));
		if (PathFileExists(out)) {
			if (!overwriteAsked) {
				overwriteAsked = TRUE;
				CString ask;
				ask.Format(LL14(L"%s など既存の正規化 WAV を上書きしますか？", L"Overwrite existing normalized WAVs such as %s?", L"Ecraser les WAV normalises existants (ex. %s) ?", L"Sovrascrivere i WAV normalizzati esistenti (es. %s)?", L"¿Sobrescribir WAV normalizados existentes (ej. %s)?", L"기존 정규화 WAV를 덮어쓸까요? (예: %s)", L"是否覆盖已有的标准化 WAV（如 %s）？", L"هل تريد استبدال ملفات WAV المطبّعة الموجودة (مثل %s)؟", L"Перезаписать существующие нормализованные WAV (напр. %s)?", L"Vorhandene normalisierte WAVs ueberschreiben (z.B. %s)?", L"Sobrescrever WAVs normalizados existentes (ex. %s)?", L"Bestaande genormaliseerde WAVs overschrijven (bv. %s)?", L"Nadpisac istniejace znormalizowane WAV (np. %s)?", L"Mevcut normallestirilmis WAV uzerine yazilsin mi (orn. %s)?"), (LPCTSTR)out);
				overwriteAll = (MessageBox(ask, LL14(L"ノーマライズ", L"Normalize", L"Normaliser", L"Normalizza", L"Normalizar", L"정규화", L"标准化", L"تطبيع", L"Нормализация", L"Normalisieren", L"Normalizar", L"Normaliseren", L"Normalizuj", L"Normalize"), MB_YESNO | MB_ICONQUESTION) == IDYES);
			}
			if (!overwriteAll) continue;
		}
		if (!hadRg) {
			// 計測パスは最終名に書かない（失敗時に未正規化ファイルが残るのを防ぐ）
			const CString tmp = (dot > 0) ? (path.Left(dot) + _T("_normscan.tmp.wav")) : (path + _T("_normscan.tmp.wav"));
			ProAudio_LoudnessReset();
			ok = og->ExportToWav(&item, tmp, 1, NULL, FALSE) && ok;
			ProAudio_LoudnessCommitCurrentSong();
			::DeleteFile(tmp);
		}
		ok = og->ExportToWav(&item, out, 1, NULL, TRUE) && ok;
	}
	savedata.pro_rg_mode = rgModeBak;
	savedata.pro_rg_target = rgTargetBak;
	savedata.pro_export_limit = limitBak;

	MessageBox(ok
		? LL14(L"LUFS 正規化書き出しが完了しました。", L"Batch LUFS export finished.", L"Export LUFS termine.", L"Export LUFS completato.", L"Exportacion LUFS lista.", L"LUFS 정규화 완료.", L"LUFS 批量导出完成。", L"اكتمل تصدير LUFS.", L"Пакетный экспорт LUFS готов.", L"Batch-LUFS-Export fertig.", L"Exportacao LUFS concluida.", L"Batch LUFS klaar.", L"Normalizacja LUFS zakonczona.", L"LUFS toplu aktarim bitti.")
		: LL14(L"一部の書き出しに失敗しました。", L"Some exports failed.", L"Certains exports ont echoue.", L"Alcune esportazioni fallite.", L"Algunas exportaciones fallaron.", L"일부 내보내기 실패.", L"部分导出失败。", L"فشل بعض التصدير.", L"Часть экспорта не удалась.", L"Einige Exporte fehlgeschlagen.", L"Algumas exportacoes falharam.", L"Sommige exports mislukt.", L"Czesc eksportow nieudana.", L"Bazi disa aktarimlar basarisiz."),
		_T("Normalize batch"), ok ? MB_OK : MB_OK | MB_ICONWARNING);
}

static CString MpMbUtf8TopField(const CStringA& obj, LPCSTR key)
{
	// ネスト内の同名キーを拾わない（releases[].title / track.title 対策）
	CStringA raw = ExtractValueFromBlock(obj, CStringA(key), true);
	if (raw.IsEmpty()) return _T("");
	raw = UnescapeJsonUnicode(raw);
	return CString(CA2T(raw, CP_UTF8));
}

static int MpMbJsonObjectEnd(const CStringA& json, int objStart)
{
	const char* p = json.GetString();
	const int len = json.GetLength();
	if (objStart < 0 || objStart >= len || p[objStart] != '{') return -1;
	int depth = 0;
	bool inQuote = false;
	for (int k = objStart; k < len; ++k) {
		const char c = p[k];
		if (c == '"') {
			int bs = 0;
			for (int j = k - 1; j >= 0 && p[j] == '\\'; --j) ++bs;
			if ((bs % 2) == 0) inQuote = !inQuote;
			continue;
		}
		if (inQuote) continue;
		if (c == '{') depth++;
		else if (c == '}') {
			depth--;
			if (depth == 0) return k;
		}
	}
	return -1;
}

static CString MpMbLuceneQuote(CString s)
{
	s.Trim();
	s.Replace(_T("\\"), _T("\\\\"));
	s.Replace(_T("\""), _T("\\\""));
	return s;
}

static CString MpMbStripAnimeSuffix(CString title)
{
	title.Trim();
	if (title.IsEmpty()) return title;
	// ファイル名に多い「作品名OP/ED」は MB にそのまま無いことが多い
	static const TCHAR* kSuf[] = {
		_T("オープニング"), _T("エンディング"), _T("オープニングテーマ"), _T("エンディングテーマ"),
		_T("主題歌"), _T("挿入歌"), _T("挿入曲"), _T("イメージソング"),
		_T("opening"), _T("ending"), _T("opening theme"), _T("ending theme"),
		_T("OPテーマ"), _T("EDテーマ"), _T("OP曲"), _T("ED曲"),
		_T("OP"), _T("ED"), _T("IN"), _T("inst"), _T("Instrumental"),
	};
	CString low = title;
	low.MakeLower();
	for (int i = 0; i < (int)(sizeof(kSuf) / sizeof(kSuf[0])); ++i) {
		CString s = kSuf[i];
		CString sl = s; sl.MakeLower();
		if (low.GetLength() > sl.GetLength() && low.Right(sl.GetLength()) == sl) {
			CString t = title.Left(title.GetLength() - s.GetLength());
			t.TrimRight(_T(" 　-_~〜・:：()（）[]【】"));
			t.Trim();
			if (t.GetLength() >= 2)
				return t;
		}
	}
	return title;
}

static CString MpMbCleanSearchTitle(CString title)
{
	title.Trim();
	// パス葉だけ
	const int slashL = title.ReverseFind(_T('\\'));
	const int slashR = title.ReverseFind(_T('/'));
	const int slash = (slashL > slashR) ? slashL : slashR;
	if (slash >= 0 && slash + 1 < title.GetLength())
		title = title.Mid(slash + 1);
	// 拡張子除去
	const int dot = title.ReverseFind(_T('.'));
	if (dot > 0) {
		CString ext = title.Mid(dot + 1);
		ext.MakeLower();
		if (ext == _T("mp3") || ext == _T("flac") || ext == _T("ogg") || ext == _T("wav")
			|| ext == _T("m4a") || ext == _T("aac") || ext == _T("wma") || ext == _T("opus")
			|| ext == _T("aiff") || ext == _T("aif") || ext == _T("wv"))
			title = title.Left(dot);
	}
	// 先頭の曲番 "01 - " / "01." / "1 "
	int i = 0;
	while (i < title.GetLength() && title[i] >= _T('0') && title[i] <= _T('9')) ++i;
	if (i > 0 && i < title.GetLength()) {
		TCHAR c = title[i];
		if (c == _T('.') || c == _T('-') || c == _T('_') || c == _T(' ') || c == _T(')')) {
			while (i < title.GetLength() && (title[i] == _T('.') || title[i] == _T('-')
				|| title[i] == _T('_') || title[i] == _T(' ') || title[i] == _T(')')))
				++i;
			if (i < title.GetLength())
				title = title.Mid(i);
		}
	}
	title.Trim();
	return title;
}

static BOOL MpMbIsUselessArtist(const CString& artist)
{
	if (artist.IsEmpty()) return TRUE;
	CString a = artist;
	a.Trim();
	a.MakeLower();
	return a == _T("various artists") || a == _T("various") || a == _T("va")
		|| a == _T("unknown") || a == _T("unknown artist") || a == _T("アーティスト未設定");
}

static CStringA MpMbHttpGet(const CString& query)
{
	CString url;
	url.Format(_T("https://musicbrainz.org/ws/2/recording?query=%s&fmt=json&limit=10"),
		(LPCTSTR)UrlEncode(query));
	return HttpGet(url, kMusicBrainzAgent, _T("Accept: application/json\r\n"), 20000);
}

struct MpMbHit {
	CString title;
	CString artist;
	CString album;
};
enum { kMpMbHitMax = 12 };

static BOOL MpMbHitDup(const MpMbHit* hits, int nHits, const CString& t, const CString& art)
{
	for (int i = 0; i < nHits; ++i) {
		if (hits[i].title.CompareNoCase(t) == 0 && hits[i].artist.CompareNoCase(art) == 0)
			return TRUE;
	}
	return FALSE;
}

// nHits は呼び出し側が初期化。既存候補へ追記（重複スキップ）
static BOOL MpMbParseHitsAll(const CStringA& resp, MpMbHit* hits, int& nHits)
{
	if (!hits) return FALSE;
	int recPos = resp.Find("\"recordings\"");
	if (recPos < 0) return FALSE;
	int arrayStart = resp.Find('[', recPos);
	if (arrayStart < 0) return FALSE;
	int i = arrayStart + 1;
	while (i < resp.GetLength() && (resp[i] == ' ' || resp[i] == '\n' || resp[i] == '\r' || resp[i] == '\t'))
		++i;
	if (i < resp.GetLength() && resp[i] == ']')
		return TRUE;

	int scan = arrayStart + 1;
	while (nHits < kMpMbHitMax) {
		const int objStart = resp.Find('{', scan);
		if (objStart < 0) break;
		const int objEnd = MpMbJsonObjectEnd(resp, objStart);
		if (objEnd < 0) break;
		scan = objEnd + 1;
		CStringA block = resp.Mid(objStart, objEnd - objStart + 1);
		CString t = MpMbUtf8TopField(block, "title");
		if (t.IsEmpty()) continue;

		CString art;
		int ap = block.Find("\"artist-credit\"");
		if (ap >= 0) {
			int arr = block.Find('[', ap);
			if (arr >= 0) {
				int creditObj = block.Find('{', arr);
				if (creditObj >= 0) {
					int creditEnd = MpMbJsonObjectEnd(block, creditObj);
					if (creditEnd > creditObj) {
						CStringA credit = block.Mid(creditObj, creditEnd - creditObj + 1);
						art = MpMbUtf8TopField(credit, "name");
						if (art.IsEmpty()) {
							int artistKey = credit.Find("\"artist\"");
							if (artistKey >= 0) {
								int aObj = credit.Find('{', artistKey);
								if (aObj >= 0) {
									int aEnd = MpMbJsonObjectEnd(credit, aObj);
									if (aEnd > aObj)
										art = MpMbUtf8TopField(credit.Mid(aObj, aEnd - aObj + 1), "name");
								}
							}
						}
					}
				}
			}
		}
		if (MpMbHitDup(hits, nHits, t, art)) continue;
		CString alb;
		int rp = block.Find("\"releases\"");
		if (rp >= 0) {
			int relArr = block.Find('[', rp);
			int relObj = (relArr >= 0) ? block.Find('{', relArr) : -1;
			if (relObj >= 0) {
				int relEnd = MpMbJsonObjectEnd(block, relObj);
				if (relEnd > relObj)
					alb = MpMbUtf8TopField(block.Mid(relObj, relEnd - relObj + 1), "title");
			}
		}
		hits[nHits].title = t;
		hits[nHits].artist = art;
		hits[nHits].album = alb;
		nHits++;
	}
	return TRUE;
}

// 「ゆるゆりOP」等: Wikipedia 主題歌欄から正式曲名を拾う
static void MpMbAddThemeTitle(CString* out, int& n, int maxOut, CString t)
{
	t.Trim();
	t.Replace(_T("[["), _T(""));
	t.Replace(_T("]]"), _T(""));
	const int pipe = t.Find(_T('|'));
	if (pipe >= 0) t = t.Mid(pipe + 1);
	t.Trim();
	if (t.GetLength() < 2 || t.GetLength() > 80) return;
	if (t.Find(_T("{{")) >= 0 || t.Find(_T("http")) >= 0) return;
	for (int i = 0; i < n; ++i) {
		if (out[i].CompareNoCase(t) == 0) return;
	}
	if (n < maxOut) out[n++] = t;
}

static int MpMbWikiThemeSongs(const CString& workTitle, CString* out, int maxOut)
{
	if (!out || maxOut <= 0 || workTitle.IsEmpty()) return 0;
	CString url;
	url.Format(_T("https://ja.wikipedia.org/w/api.php?action=parse&page=%s&prop=wikitext&format=json&formatversion=2&redirects=1"),
		(LPCTSTR)UrlEncode(workTitle));
	CStringA resp = HttpGet(url, kMusicBrainzAgent, _T("Accept: application/json\r\n"), 15000);
	if (resp.IsEmpty()) return 0;
	CStringA wtUtf8 = ExtractJsonStringSimple(resp, "wikitext");
	if (wtUtf8.IsEmpty()) return 0;
	CString wt = CString(CA2T(wtUtf8, CP_UTF8));
	int n = 0;
	static const TCHAR* kKeys[] = {
		_T("オープニングテーマ"), _T("エンディングテーマ"), _T("オープニング"), _T("エンディング"),
		_T("主題歌"), _T("OPテーマ"), _T("EDテーマ"), _T("OP"), _T("ED"),
	};
	for (int k = 0; k < (int)(sizeof(kKeys) / sizeof(kKeys[0])); ++k) {
		const CString key = kKeys[k];
		int pos = 0;
		while (n < maxOut) {
			pos = wt.Find(key, pos);
			if (pos < 0) break;
			const int from = pos;
			const int to = min(wt.GetLength(), from + 220);
			CString win = wt.Mid(from, to - from);
			pos = from + (int)_tcslen(kKeys[k]);
			for (int qi = 0; qi < win.GetLength() && n < maxOut; ++qi) {
				const TCHAR oq = win[qi];
				TCHAR cq = 0;
				if (oq == 0x300C) cq = 0x300D; // 「」
				else if (oq == 0x300E) cq = 0x300F; // 『』
				else continue;
				const int qe = win.Find(cq, qi + 1);
				if (qe <= qi + 1) continue;
				MpMbAddThemeTitle(out, n, maxOut, win.Mid(qi + 1, qe - qi - 1));
				qi = qe;
			}
		}
	}
	return n;
}

class CMpMbPickDlg : public CCustomBlurDialogBase
{
public:
	MpMbHit* m_hits;
	int m_nHits;
	int m_sel;
	CCustomListCtrl m_lc;
	CCustomStandardButton m_apply, m_cancel;
	CMpMbPickDlg(CWnd* p, MpMbHit* hits, int n)
		: CCustomBlurDialogBase(IDD_MP_MBPICK, p), m_hits(hits), m_nHits(n), m_sel(-1) {}
	virtual void DoDataExchange(CDataExchange* pDX)
	{
		CCustomBlurDialogBase::DoDataExchange(pDX);
		DDX_Control(pDX, IDC_MMP_LIST, m_lc);
		DDX_Control(pDX, IDC_MMP_APPLY, m_apply);
		DDX_Control(pDX, IDC_MMP_CANCEL, m_cancel);
	}
	virtual BOOL OnInitDialog()
	{
		CCustomBlurDialogBase::OnInitDialog();
		CCC_BringDialogToForeground(this);
		SetWindowText(LL14(L"MusicBrainz 候補", L"MusicBrainz candidates", L"Candidats MusicBrainz", L"Candidati MusicBrainz", L"Candidatos MusicBrainz", L"MusicBrainz 후보", L"MusicBrainz 候选", L"مرشحات MusicBrainz", L"Кандидаты MusicBrainz", L"MusicBrainz-Treffer", L"Candidatos MusicBrainz", L"MusicBrainz-kandidaten", L"Kandydaci MusicBrainz", L"MusicBrainz adaylari"));
		SetDlgItemText(IDC_MMP_APPLY, LL14(L"適用", L"Apply", L"Appliquer", L"Applica", L"Aplicar", L"적용", L"应用", L"تطبيق", L"Применить", L"Anwenden", L"Aplicar", L"Toepassen", L"Zastosuj", L"Uygula"));
		SetDlgItemText(IDC_MMP_CANCEL, LL14(L"キャンセル", L"Cancel", L"Annuler", L"Annulla", L"Cancelar", L"취소", L"取消", L"إلغاء", L"Отмена", L"Abbrechen", L"Cancelar", L"Annuleren", L"Anuluj", L"Iptal"));
		m_apply.SetGradation(RGB(220, 245, 230), RGB(160, 220, 180), 0, TRUE);
		m_cancel.SetGradation(RGB(235, 235, 240), RGB(200, 200, 210), 0, TRUE);
		m_lc.SetExtendedStyle(m_lc.GetExtendedStyle() | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
		m_lc.InsertColumn(0, _T("#"), LVCFMT_RIGHT, 28);
		m_lc.InsertColumn(1, LL14(L"タイトル", L"Title", L"Titre", L"Titolo", L"Titulo", L"제목", L"标题", L"العنوان", L"Название", L"Titel", L"Titulo", L"Titel", L"Tytul", L"Baslik"), LVCFMT_LEFT, 180);
		m_lc.InsertColumn(2, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Interpret", L"Artista", L"Artiest", L"Artysta", L"Sanatci"), LVCFMT_LEFT, 140);
		m_lc.InsertColumn(3, LL14(L"アルバム", L"Album", L"Album", L"Album", L"Album", L"앨범", L"专辑", L"الألبوم", L"Альбом", L"Album", L"Album", L"Album", L"Album", L"Album"), LVCFMT_LEFT, 140);
		for (int i = 0; i < m_nHits; ++i) {
			CString num; num.Format(_T("%d"), i + 1);
			const int row = m_lc.InsertItem(i, num);
			m_lc.SetItemText(row, 1, m_hits[i].title);
			m_lc.SetItemText(row, 2, m_hits[i].artist);
			m_lc.SetItemText(row, 3, m_hits[i].album);
			m_lc.SetItemData(row, (DWORD_PTR)i);
		}
		if (m_nHits > 0) {
			m_lc.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
			m_sel = 0;
		}
		return TRUE;
	}
	afx_msg void OnApply()
	{
		POSITION pos = m_lc.GetFirstSelectedItemPosition();
		if (!pos) { EndDialog(IDCANCEL); return; }
		m_sel = (int)m_lc.GetItemData(m_lc.GetNextSelectedItem(pos));
		EndDialog(IDOK);
	}
	afx_msg void OnCancelBtn() { EndDialog(IDCANCEL); }
	afx_msg void OnDblClk(NMHDR*, LRESULT* p) { *p = 0; OnApply(); }
	DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMpMbPickDlg, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_MMP_APPLY, &CMpMbPickDlg::OnApply)
	ON_BN_CLICKED(IDC_MMP_CANCEL, &CMpMbPickDlg::OnCancelBtn)
	ON_NOTIFY(NM_DBLCLK, IDC_MMP_LIST, &CMpMbPickDlg::OnDblClk)
END_MESSAGE_MAP()

static BOOL MpMbParseHits(const CStringA& resp, CString& pickTitle, CString& pickArtist, CString& pickAlbum, int& hits)
{
	hits = 0;
	pickTitle.Empty(); pickArtist.Empty(); pickAlbum.Empty();
	int recPos = resp.Find("\"recordings\"");
	if (recPos < 0) return FALSE;
	int arrayStart = resp.Find('[', recPos);
	if (arrayStart < 0) return FALSE;
	int i = arrayStart + 1;
	while (i < resp.GetLength() && (resp[i] == ' ' || resp[i] == '\n' || resp[i] == '\r' || resp[i] == '\t'))
		++i;
	if (i < resp.GetLength() && resp[i] == ']')
		return TRUE; // 空配列=一致なし（解析失敗ではない）

	int scan = arrayStart + 1;
	while (hits < 5) {
		const int objStart = resp.Find('{', scan);
		if (objStart < 0) break;
		const int objEnd = MpMbJsonObjectEnd(resp, objStart);
		if (objEnd < 0) break;
		scan = objEnd + 1;
		CStringA block = resp.Mid(objStart, objEnd - objStart + 1);
		CString t = MpMbUtf8TopField(block, "title");
		if (t.IsEmpty()) continue;

		CString art;
		int ap = block.Find("\"artist-credit\"");
		if (ap >= 0) {
			int arr = block.Find('[', ap);
			if (arr >= 0) {
				int creditObj = block.Find('{', arr);
				if (creditObj >= 0) {
					int creditEnd = MpMbJsonObjectEnd(block, creditObj);
					if (creditEnd > creditObj) {
						CStringA credit = block.Mid(creditObj, creditEnd - creditObj + 1);
						art = MpMbUtf8TopField(credit, "name");
						if (art.IsEmpty()) {
							int artistKey = credit.Find("\"artist\"");
							if (artistKey >= 0) {
								int aObj = credit.Find('{', artistKey);
								if (aObj >= 0) {
									int aEnd = MpMbJsonObjectEnd(credit, aObj);
									if (aEnd > aObj)
										art = MpMbUtf8TopField(credit.Mid(aObj, aEnd - aObj + 1), "name");
								}
							}
						}
					}
				}
			}
		}
		CString alb;
		int rp = block.Find("\"releases\"");
		if (rp >= 0) {
			int relArr = block.Find('[', rp);
			int relObj = (relArr >= 0) ? block.Find('{', relArr) : -1;
			if (relObj >= 0) {
				int relEnd = MpMbJsonObjectEnd(block, relObj);
				if (relEnd > relObj)
					alb = MpMbUtf8TopField(block.Mid(relObj, relEnd - relObj + 1), "title");
			}
		}
		if (hits == 0) {
			pickTitle = t; pickArtist = art; pickAlbum = alb;
		}
		hits++;
	}
	return TRUE;
}

void CMediaPlayerDlg::OnMbAutotag()
{
	if (!pl || !pl->pc) return;
	const int pc = GetSelectedPcIndex();
	if (pc < 0 || pc >= pl->playcnt) return;
	playlistdata0& item = pl->pc[pc];

	FileTagFields tags;
	ReadFileTagFields(item.fol, tags);
	CString title = tags.title.IsEmpty() ? item.name : tags.title;
	CString artist = tags.artist.IsEmpty() ? item.art : tags.artist;
	title = MpMbCleanSearchTitle(title);
	artist.Trim();
	if (title.IsEmpty()) {
		MessageBox(LL14(L"タイトルがありません。", L"No title to search.", L"Pas de titre.", L"Nessun titolo.", L"Sin titulo.", L"제목 없음.", L"无标题。", L"لا عنوان.", L"Нет названия.", L"Kein Titel.", L"Sem titulo.", L"Geen titel.", L"Brak tytulu.", L"Baslik yok."), LL14(L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz"), MB_OK);
		return;
	}

	// MusicBrainz レート制限（1 req/sec）
	static DWORD s_mbLast = 0;
	{
		const DWORD now = GetTickCount();
		if (s_mbLast && now - s_mbLast < 1100)
			Sleep(1100 - (now - s_mbLast));
		s_mbLast = GetTickCount();
	}

	CString q1;
	q1.Format(_T("recording:\"%s\""), (LPCTSTR)MpMbLuceneQuote(title));
	const BOOL useArtist = !MpMbIsUselessArtist(artist);
	if (useArtist) {
		CString a;
		a.Format(_T(" AND artist:\"%s\""), (LPCTSTR)MpMbLuceneQuote(artist));
		q1 += a;
	}

	MpMbHit hitBuf[kMpMbHitMax];
	int hits = 0;
	CStringA resp = MpMbHttpGet(q1);
	BOOL parsed = !resp.IsEmpty() && MpMbParseHitsAll(resp, hitBuf, hits);
	BOOL anyHttp = !resp.IsEmpty();

	auto mbWait = [&]() {
		const DWORD now = GetTickCount();
		if (s_mbLast && now - s_mbLast < 1100)
			Sleep(1100 - (now - s_mbLast));
		s_mbLast = GetTickCount();
	};

	auto mbQueryMerge = [&](const CString& q) {
		if (hits >= kMpMbHitMax || q.IsEmpty()) return;
		mbWait();
		resp = MpMbHttpGet(q);
		if (resp.IsEmpty()) return;
		anyHttp = TRUE;
		if (MpMbParseHitsAll(resp, hitBuf, hits))
			parsed = TRUE;
	};

	// アーティスト付きで空ならタイトルのみ
	if (hits == 0 && useArtist) {
		CString q2;
		q2.Format(_T("recording:\"%s\""), (LPCTSTR)MpMbLuceneQuote(title));
		mbQueryMerge(q2);
	}

	const CString stripped = MpMbStripAnimeSuffix(title);
	const BOOL animeSuffix = (stripped != title && !stripped.IsEmpty());

	// 「ゆるゆりOP」→ Wikipedia 主題歌から正式曲名（ゆりゆららららゆるゆり大事件 等）を先頭候補に
	if (animeSuffix && hits < kMpMbHitMax) {
		CString themes[8];
		const int nTheme = MpMbWikiThemeSongs(stripped, themes, 8);
		for (int ti = 0; ti < nTheme && hits < kMpMbHitMax; ++ti) {
			if (themes[ti].CompareNoCase(stripped) == 0) continue;
			CString qt;
			qt.Format(_T("recording:\"%s\""), (LPCTSTR)MpMbLuceneQuote(themes[ti]));
			mbQueryMerge(qt);
		}
	}

	if (animeSuffix)
		mbQueryMerge(CString(_T("recording:\"")) + MpMbLuceneQuote(stripped) + _T("\""));
	mbQueryMerge(stripped.IsEmpty() ? title : stripped);
	{
		CString q4;
		q4.Format(_T("release:\"%s\""), (LPCTSTR)MpMbLuceneQuote(stripped.IsEmpty() ? title : stripped));
		mbQueryMerge(q4);
	}

	if (!anyHttp) {
		MessageBox(LL14(L"MusicBrainz に接続できませんでした。", L"Could not reach MusicBrainz.", L"MusicBrainz injoignable.", L"MusicBrainz non raggiungibile.", L"No se pudo contactar MusicBrainz.", L"MusicBrainz 연결 실패.", L"无法连接 MusicBrainz。", L"تعذر الوصول.", L"MusicBrainz недоступен.", L"MusicBrainz nicht erreichbar.", L"MusicBrainz inacessivel.", L"MusicBrainz onbereikbaar.", L"Brak polaczenia z MusicBrainz.", L"MusicBrainz ulasilamadi."), LL14(L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz"), MB_OK | MB_ICONWARNING);
		return;
	}
	if (!parsed && hits <= 0) {
		MessageBox(LL14(L"タグ候補を解析できませんでした。", L"Could not parse tag candidates.", L"Analyse impossible.", L"Impossibile analizzare.", L"No se pudo analizar.", L"후보 파싱 실패.", L"无法解析候选。", L"تعذر التحليل.", L"Не удалось разобрать.", L"Analyse fehlgeschlagen.", L"Falha ao analisar.", L"Parseren mislukt.", L"Nie udalo sie odczytac.", L"Adaylar cozulemedi."), LL14(L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz"), MB_OK);
		return;
	}
	if (hits <= 0) {
		CString msg;
		msg.Format(LL14(L"一致する録音がありません。\n検索: %s", L"No matching recordings.\nQuery: %s", L"Aucun enregistrement.\nRequete: %s", L"Nessuna registrazione.\nQuery: %s", L"Sin coincidencias.\nConsulta: %s", L"일치 녹음 없음.\n검색: %s", L"无匹配录音。\n搜索: %s", L"لا تطابق.\nاستعلام: %s", L"Нет совпадений.\nЗапрос: %s", L"Keine Treffer.\nSuche: %s", L"Sem resultados.\nConsulta: %s", L"Geen matches.\nZoek: %s", L"Brak dopasowan.\nSzukaj: %s", L"Eslesme yok.\nArama: %s"),
			(LPCTSTR)title);
		MessageBox(msg, LL14(L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz"), MB_OK);
		return;
	}

	CMpMbPickDlg pick(this, hitBuf, hits);
	if (pick.DoModal() != IDOK || pick.m_sel < 0 || pick.m_sel >= hits)
		return;
	const CString pickTitle = hitBuf[pick.m_sel].title;
	const CString pickArtist = hitBuf[pick.m_sel].artist;
	const CString pickAlbum = hitBuf[pick.m_sel].album;

	FileTagFields out = tags;
	if (!pickTitle.IsEmpty()) out.title = pickTitle;
	if (!pickArtist.IsEmpty()) out.artist = pickArtist;
	if (!pickAlbum.IsEmpty()) out.album = pickAlbum;
	if (!WriteFileTagFields(item.fol, out)) {
		MessageBox(LL14(L"タグの書き込みに失敗しました。", L"Failed to write tags.", L"Ecriture tags echouee.", L"Scrittura tag fallita.", L"Error al escribir tags.", L"태그 저장 실패.", L"写入标签失败。", L"فشل الكتابة.", L"Не удалось записать теги.", L"Tags schreiben fehlgeschlagen.", L"Falha ao gravar tags.", L"Tags schrijven mislukt.", L"Zapis tagow nieudany.", L"Etiket yazilamadi."), LL14(L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz"), MB_OK | MB_ICONWARNING);
		return;
	}
	if (!out.title.IsEmpty()) _tcsncpy(item.name, out.title, 1023);
	if (!out.artist.IsEmpty()) _tcsncpy(item.art, out.artist, 1023);
	if (!out.album.IsEmpty()) _tcsncpy(item.alb, out.album, 1023);
	item.name[1023] = item.art[1023] = item.alb[1023] = 0;
	RefreshList(TRUE);
	MessageBox(LL14(L"MusicBrainz タグを適用しました。", L"Applied MusicBrainz tags.", L"Tags MusicBrainz appliques.", L"Tag MusicBrainz applicati.", L"Etiquetas MusicBrainz aplicadas.", L"MusicBrainz 태그 적용됨.", L"已应用 MusicBrainz 标签。", L"تم تطبيق وسوم MusicBrainz.", L"Теги MusicBrainz применены.", L"MusicBrainz-Tags angewendet.", L"Tags MusicBrainz aplicadas.", L"MusicBrainz-tags toegepast.", L"Zastosowano tagi MusicBrainz.", L"MusicBrainz etiketleri uygulandi."), LL14(L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz", L"MusicBrainz"), MB_OK);
}

void CMediaPlayerDlg::OnNormPreview()
{
	const int pc = GetSelectedPcIndex();
	if (pc < 0 || !pl) return;
	ProSongExtra e;
	CString msg;
	if (ProAudio_GetExtra(MpCurListName(), pl->pc[pc].fol, pl->pc[pc].sub, pl->pc[pc].ret2, e) && e.rgValid)
		msg.Format(_T("trackPeak=%.4f\ntrackGainDb=%.2f dB\nalbumGainDb=%.2f dB"), e.trackPeak, e.trackGainDb, e.albumGainDb);
	else
		msg = LL14(L"この曲の正規化データはありません。", L"No normalize data for this track.", L"Pas de donnees de normalisation.", L"Nessun dato di normalizzazione.", L"Sin datos de normalizacion.", L"이 곡의 정규화 데이터 없음.", L"此曲无标准化数据。", L"لا بيانات تطبيع.", L"Нет данных нормализации.", L"Keine Normalisierungsdaten.", L"Sem dados de normalizacao.", L"Geen normalisatiegegevens.", L"Brak danych normalizacji.", L"Normalizasyon verisi yok.");
	MessageBox(msg, LL14(L"ノーマライズ", L"Normalize", L"Normaliser", L"Normalizza", L"Normalizar", L"정규화", L"标准化", L"تطبيع", L"Нормализация", L"Normalisieren", L"Normalizar", L"Normaliseren", L"Normalizuj", L"Normalize"), MB_OK | MB_ICONINFORMATION);
}

void CMediaPlayerDlg::OnAbSnapA() { ProAudio_AbCapture(0); }
void CMediaPlayerDlg::OnAbSnapB() { ProAudio_AbCapture(1); }
void CMediaPlayerDlg::OnAbApplyA()
{
	ProAudio_AbApply(0);
	if (m_tempo.GetSafeHwnd()) m_tempo.SetPos(tempo);
	if (m_pitch.GetSafeHwnd()) m_pitch.SetPos(pitch);
}
void CMediaPlayerDlg::OnAbApplyB()
{
	ProAudio_AbApply(1);
	if (m_tempo.GetSafeHwnd()) m_tempo.SetPos(tempo);
	if (m_pitch.GetSafeHwnd()) m_pitch.SetPos(pitch);
}
void CMediaPlayerDlg::OnAbSnapToggle()
{
	ProAudio_AbToggle();
	if (m_tempo.GetSafeHwnd()) m_tempo.SetPos(tempo);
	if (m_pitch.GetSafeHwnd()) m_pitch.SetPos(pitch);
}
void CMediaPlayerDlg::OnSleep15() { ApplySleepTimer(15); }
void CMediaPlayerDlg::OnSleep30() { ApplySleepTimer(30); }
void CMediaPlayerDlg::OnSleep60() { ApplySleepTimer(60); }
void CMediaPlayerDlg::OnSleepOff() { ApplySleepTimer(0); }
void CMediaPlayerDlg::OnXfadePreviewToggle()
{
	savedata.mpXfadePreview = savedata.mpXfadePreview ? 0 : 1;
	MpPersistSavedataQuick();
	if (m_seek.GetSafeHwnd()) {
		int xms = 0;
		if (savedata.mpXfadePreview)
			xms = (savedata.wav_export_xfade_sec > 0 ? savedata.wav_export_xfade_sec : 5) * 1000;
		m_seek.SetXfadePreviewMs(xms);
		m_seek.Invalidate(FALSE);
	}
}
void CMediaPlayerDlg::OnBeatGridToggle()
{
	savedata.mpBeatGrid = savedata.mpBeatGrid ? 0 : 1;
	MpPersistSavedataQuick();
	if (m_seek.GetSafeHwnd()) {
		const float bpm = savedata.mpDetectedBpm > 0 ? (float)savedata.mpDetectedBpm : 120.f;
		m_seek.SetBeatGrid(bpm, savedata.mpBeatGrid ? TRUE : FALSE, savedata.mpBeatGridOffsetMs);
		m_seek.Invalidate(FALSE);
	}
	if (savedata.mpDetectedBpm > 0)
		SongParams_SaveBpmForCurrentSong();
}

void CMediaPlayerDlg::OnJacketRemOverlayToggle()
{
	savedata.mpJacketRemOverlay = savedata.mpJacketRemOverlay ? 0 : 1;
	MpPersistSavedataQuick();
	m_jacketRemBucket = -1;
	if (!m_jacketRect.IsRectEmpty())
		InvalidateRect(&m_jacketRect, FALSE);
}

void CMediaPlayerDlg::OnMpBpmDetect() { MpOnBpmDetect(this); }
void CMediaPlayerDlg::OnMpBpmCand1() { MpOnBpmCandPick(0); }
void CMediaPlayerDlg::OnMpBpmCand2() { MpOnBpmCandPick(1); }
void CMediaPlayerDlg::OnMpBpmCand3() { MpOnBpmCandPick(2); }
void CMediaPlayerDlg::OnMpDjPad() { OpenMpDjPadModeless(this); }
void CMediaPlayerDlg::OnMpAlarm()
{
	CloseMpAlarmDlgIfOpen();
	if (savedata.mpAlarmHour >= 0) {
		// OFF にしても時分下書きは残す（メニュー再オープンで現在時刻に戻さない）
		g_mpMenuAlarmH = savedata.mpAlarmHour;
		g_mpMenuAlarmM = savedata.mpAlarmMin;
		if (g_mpMenuAlarmM < 0) g_mpMenuAlarmM = 0;
		if (g_mpMenuAlarmM > 59) g_mpMenuAlarmM = 59;
		g_mpMenuAlarmDraftValid = TRUE;
		savedata.mpAlarmHour = -1;
	} else {
		if (g_mpMenuAlarmH < 0 || g_mpMenuAlarmH > 23) g_mpMenuAlarmH = 8;
		if (g_mpMenuAlarmM < 0 || g_mpMenuAlarmM > 59) g_mpMenuAlarmM = 0;
		savedata.mpAlarmHour = g_mpMenuAlarmH;
		savedata.mpAlarmMin = g_mpMenuAlarmM;
	}
	MpPersistSavedataQuick();
	MpAlarmEnsureTimer(this);
}
void CMediaPlayerDlg::OnMpMirror() { OpenMpMirrorDlgModeless(this); }

void CMediaPlayerDlg::OnMpSoundMeter() { OpenSoundMeterModeless(this); }
void CMediaPlayerDlg::OnMpDigitize() { OpenDigitizeModeless(this); }
void CMediaPlayerDlg::OnMpVoiceChanger() { OpenVoiceChangerModeless(this); }
void CMediaPlayerDlg::OnMpTunerPractice() { OpenTunerPracticeModeless(this); }
void CMediaPlayerDlg::OnMpPhotoFrame() { OpenPhotoFrameModeless(this); }
void CMediaPlayerDlg::OnMpSoft3DMaze() { OpenSoft3DMazeModeless(this); }
void CMediaPlayerDlg::OnMpRemote()
{
	CloseMpRemoteDlgIfOpen();
	savedata.mpRemoteOn = savedata.mpRemoteOn ? 0 : 1;
	if (savedata.mpRemotePort < 1024 || savedata.mpRemotePort > 65535)
		savedata.mpRemotePort = 8765;
	MpPersistSavedataQuick();
	MpRemoteEnsureRunning(GetSafeHwnd());
}
void CMediaPlayerDlg::OnMpRemoteDlg() { OpenMpRemoteDlgModeless(this); }
void CMediaPlayerDlg::OnMpRemoteBrowser() { MpRemoteOpenInBrowser(); }
void CMediaPlayerDlg::OnMpSsViz() { OpenMpSsVizModeless(this); }
void CMediaPlayerDlg::OnMpVideoExtract() { MpOnVideoExtract(this); }
void CMediaPlayerDlg::OnMpVideoReplace() { MpOnVideoReplaceAudio(this); }
void CMediaPlayerDlg::OnMpGamePreset() { MpOnGameCapturePreset(this, ID_MP_GCP_1080_60); }
void CMediaPlayerDlg::OnMpGcpRange(UINT nID) { MpOnGameCapturePreset(this, nID); }
void CMediaPlayerDlg::OnMpMidiIn() { MpOnMidiInToggle(this); }
LRESULT CMediaPlayerDlg::OnMpTransportCmd(WPARAM wParam, LPARAM lParam)
{
	return MpAddonsOnTransportCmd(this, wParam, lParam);
}

static CString MpCompactRate(int hz)
{
	CString s;
	if (hz <= 0) return s;
	if (hz % 1000 == 0)
		s.Format(L"%dkHz", hz / 1000);
	else if (hz % 100 == 0)
		s.Format(L"%.1fkHz", hz / 1000.0);
	else
		s.Format(L"%dHz", hz);
	return s;
}

static CString MpCompactAudioPiece(int rate, int ch, int bits)
{
	CString out;
	const int b = abs(bits);
	const CString rateS = MpCompactRate(rate);
	const CString chS = ChannelLayoutLabel(ch);
	if (b > 0 && !rateS.IsEmpty())
		out.Format(L"%dbit · %s · %s", b, (LPCTSTR)rateS, (LPCTSTR)chS);
	else if (!rateS.IsEmpty())
		out.Format(L"%s · %s", (LPCTSTR)rateS, (LPCTSTR)chS);
	return out;
}

CString CMediaPlayerDlg::MpTechFormatLine() const
{
	CString fmt;
	if (::IsWindow(m_os.GetSafeHwnd()))
		m_os.GetWindowText(fmt);
	fmt.Trim();

	CString audio = MpCompactAudioPiece(wavbit_sample_Hz, wavchannel, wavsam_depth);
	if (g_pcm_upscale_active) {
		const CString dst = MpCompactAudioPiece(g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits);
		if (!dst.IsEmpty() && dst.CompareNoCase(audio) != 0) {
			CString both;
			both.Format(L"%s  %s  %s", (LPCTSTR)audio, AudioUpscaleFlowSymbol(), (LPCTSTR)dst);
			audio = both;
		}
	}

	CString rg;
	if (savedata.pro_rg_mode == 1)
		rg = LL14(L"音量補正:曲", L"ReplayGain:Track", L"ReplayGain:Piste", L"ReplayGain:Traccia", L"ReplayGain:Pista", L"재생볼륨:곡", L"音量补偿:曲", L"ReplayGain:Track", L"ReplayGain:Трек", L"ReplayGain:Titel", L"ReplayGain:Faixa", L"ReplayGain:Track", L"ReplayGain:Utwor", L"ReplayGain:Parca");
	else if (savedata.pro_rg_mode == 2)
		rg = LL14(L"音量補正:アルバム", L"ReplayGain:Album", L"ReplayGain:Album", L"ReplayGain:Album", L"ReplayGain:Album", L"재생볼륨:앨범", L"音量补偿:专辑", L"ReplayGain:Album", L"ReplayGain:Альбом", L"ReplayGain:Album", L"ReplayGain:Album", L"ReplayGain:Album", L"ReplayGain:Album", L"ReplayGain:Album");
	else
		rg = LL14(L"音量補正:オフ", L"ReplayGain:Off", L"ReplayGain:Off", L"ReplayGain:Off", L"ReplayGain:Off", L"재생볼륨:끔", L"音量补偿:关", L"ReplayGain:Off", L"ReplayGain:Выкл", L"ReplayGain:Aus", L"ReplayGain:Off", L"ReplayGain:Uit", L"ReplayGain:Wyl", L"ReplayGain:Kapali");

	CString line;
	if (!fmt.IsEmpty() && !audio.IsEmpty())
		line.Format(L"%s · %s · %s", (LPCTSTR)fmt, (LPCTSTR)audio, (LPCTSTR)rg);
	else if (!fmt.IsEmpty())
		line.Format(L"%s · %s", (LPCTSTR)fmt, (LPCTSTR)rg);
	else if (!audio.IsEmpty())
		line.Format(L"%s · %s", (LPCTSTR)audio, (LPCTSTR)rg);
	else
		line = rg;

	extern int kbps;
	if (kbps > 0) {
		CString bp;
		bp.Format(L"%dkbps", kbps);
		line += L" · ";
		line += bp;
	}
	{
		const CString extra = MpFeatStatusLine();
		if (!extra.IsEmpty()) {
			line += L" · ";
			line += extra;
		}
	}
	return line;
}

namespace {

class CMpCheatSheetDlg : public CDialog
{
public:
	enum { IDD = IDD_MP_CHEATSHEET };
	explicit CMpCheatSheetDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
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

BEGIN_MESSAGE_MAP(CMpCheatSheetDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CMpCheatSheetDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"メディアプレイヤー操作ガイド", L"Media Player Guide", L"Guide lecteur", L"Guida Media Player",
		L"Guía Media Player", L"미디어 플레이어 가이드", L"媒体播放器指南", L"دليل المشغّل",
		L"Руководство плеера", L"Media-Player-Anleitung", L"Guia Media Player", L"Mediaspeler-gids",
		L"Przewodnik Media Player", L"Medya Oynatıcı kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CMpCheatSheetDlg::OnOK() { DestroyWindow(); }
void CMpCheatSheetDlg::OnCancel() { DestroyWindow(); }
void CMpCheatSheetDlg::OnClose() { DestroyWindow(); }

void CMpCheatSheetDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_mpHelpDlg == this)
		g_mpHelpDlg = nullptr;
	delete this;
}

BOOL CMpCheatSheetDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CMpCheatSheetDlg::OnPaint()
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
	const int lh = max(13, tm.tmHeight + tm.tmExternalLeading);
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

	int y = 4;
	const int L = 10;
	const int R = rc.Width() / 2 + 4;
	title(L, y, LL14(L"メディアプレイヤー操作ガイド（詳細）", L"Media Player — Detailed Guide", L"Guide détaillé Media Player", L"Guida dettagliata Media Player",
		L"Guía detallada Media Player", L"미디어 플레이어 상세 가이드", L"媒体播放器详细指南", L"دليل المشغّل التفصيلي",
		L"Подробное руководство плеера", L"Media-Player — ausführliche Anleitung", L"Guia detalhado Media Player", L"Gedetailleerde mediaspeler-gids",
		L"Szczegółowy przewodnik Media Player", L"Medya Oynatıcı ayrıntılı kılavuz"));
	y += titleLh;
	muted(L, y, LL14(
		L"らいら向け再生画面。バナー・リスト・歌詞・連携ツールの全体像です。キャプション右の ? からも開けます。",
		L"Laira playback screen. Overview of banner, list, lyrics, linked tools. Also from caption ?.",
		L"Écran Laira. Aperçu bannière, liste, paroles, outils. Aussi via ? de la légende.",
		L"Schermata Laira. Panoramica banner, lista, testi, strumenti. Anche da ? didascalia.",
		L"Pantalla Laira. Resumen banner, lista, letras, herramientas. También desde ? del título.",
		L"라이라 재생 화면. 배너·목록·가사·연동 도구 개요. 캡션 ? 로도 열립니다.",
		L"面向らいら的播放界面。横幅、列表、歌词与联动工具总览。也可从标题栏 ? 打开。",
		L"شاشة Laira. نظرة على البانر والقائمة والكلمات والأدوات. أيضاً من ? الشريط.",
		L"Экран Laira. Обзор баннера, списка, текста и окон. Также с ? в заголовке.",
		L"Laira-Fenster. Ueberblick Banner, Liste, Text, Tools. Auch per ? in der Titelleiste.",
		L"Tela Laira. Visão de banner, lista, letras e ferramentas. Também pelo ? da legenda.",
		L"Laira-scherm. Overzicht banner, lijst, tekst, tools. Ook via ? in de titel.",
		L"Ekran Laira. Przegląd banera, listy, tekstu i narzędzi. Też z ? na belce.",
		L"Laira ekranı. Banner, liste, söz ve araçların özeti. Başlıktaki ? ile de açılır."));
	y += lh + 3;
	y = CCC_GdiHelpDrawSoftDemoPair(dc, L, y, rc.Width() - L * 2, min(140, max(112, rc.Height() / 5)),
		CCC_HELPDEMO_KSPECTRUM);

	// mini map
	{
		const int gx = L, gy = y, gw = min(520, rc.Width() - L * 2), gh = lh * 2 + 10;
		dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
		dc.FillSolidRect(gx + 4, gy + 5, 40, gh - 10, RGB(160, 195, 240));
		dc.FillSolidRect(gx + 50, gy + 5, 70, gh - 10, RGB(255, 210, 160));
		dc.FillSolidRect(gx + 126, gy + 5, 48, gh - 10, RGB(130, 205, 140));
		dc.FillSolidRect(gx + 180, gy + 5, 52, gh - 10, RGB(240, 210, 160));
		dc.FillSolidRect(gx + 238, gy + 5, 48, gh - 10, RGB(220, 190, 245));
		dc.FillSolidRect(gx + 292, gy + 5, 56, gh - 10, RGB(255, 180, 120));
		dc.FillSolidRect(gx + 354, gy + 5, 44, gh - 10, RGB(180, 220, 200));
		dc.SetTextColor(RGB(35, 35, 50));
		dc.TextOut(gx + 10, gy + 7, L"Lib");
		dc.TextOut(gx + 58, gy + 7, L"Banner");
		dc.TextOut(gx + 134, gy + 7, L"Play");
		dc.TextOut(gx + 188, gy + 7, L"Sound");
		dc.TextOut(gx + 246, gy + 7, L"List");
		dc.TextOut(gx + 300, gy + 7, L"Lyrics");
		dc.TextOut(gx + 360, gy + 7, L"Tools");
		dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
		y = gy + gh + 4;
	}

	int yL = y, yR = y;

	title(L, yL, LL14(L"バナー / ジャケット", L"Banner / Jacket", L"Bannière / Pochette", L"Banner / Copertina",
		L"Banner / Carátula", L"배너 / 자켓", L"横幅 / 封面", L"البانر / الغلاف",
		L"Баннер / Обложка", L"Banner / Cover", L"Banner / Capa", L"Banner / Omslag",
		L"Baner / Okładka", L"Banner / Kapak")); yL += titleLh;
	body(L, yL, LL14(L"・中央バナー …… スペアナ＋曲情報。幅を広げると左にジャケット分離", L"· Center banner …… spectrum + info. Wider window splits jacket left", L"· Bannière …… spectre + infos. Largeur → pochette à gauche", L"· Banner …… spettro + info. Largo → copertina a sinistra",
		L"· Banner …… espectro + info. Ancho → carátula a la izq.", L"· 중앙 배너 …… 스펙트럼+정보. 넓히면 왼쪽 자켓 분리", L"· 中央横幅 …… 频谱+信息。加宽后左侧分离封面", L"· البانر …… طيف+معلومات. التوسيع يفصل الغلاف يساراً",
		L"· Баннер …… спектр + инфо. Шире — обложка слева", L"· Banner …… Spektrum+Info. Breiter → Cover links", L"· Banner …… espectro+info. Mais largo → capa à esquerda", L"· Banner …… spectrum+info. Breder → omslag links",
		L"· Baner …… widmo+info. Szersze → okładka z lewej", L"· Banner …… spektrum+bilgi. Genişleyince kapak sola")); yL += lh;
	body(L, yL, LL14(L"・右クリック …… スペアナ様式(バー/ミラー/波形)・連携起動・本ガイド", L"· Right-click …… spectrum style, linked tools, this guide", L"· Clic droit …… style spectre, outils, ce guide", L"· Destro …… stile spettro, strumenti, guida",
		L"· Clic der. …… estilo espectro, herramientas, guía", L"· 우클릭 …… 스펙트럼 양식·연동·이 가이드", L"· 右键 …… 频谱样式、联动、本指南", L"· يمين …… نمط الطيف والأدوات وهذا الدليل",
		L"· ПКМ …… стиль спектра, окна, это руководство", L"· Rechtsklick …… Spektrumstil, Tools, Guide", L"· Direito …… estilo espectro, ferramentas, guia", L"· Rechtsklik …… spectrumstijl, tools, gids",
		L"· PPM …… styl widma, narzędzia, przewodnik", L"· Sağ tık …… spektrum stili, araçlar, kılavuz")); yL += lh;
	body(L, yL, LL14(L"・ジャケットボタン …… 埋め込み/外部画像の表示切替", L"· Jacket button …… toggle embedded/external cover art", L"· Pochette …… bascule image intégrée/externe", L"· Copertina …… alterna arte incorporata/esterna",
		L"· Carátula …… alternar arte embebida/externa", L"· 자켓 버튼 …… 내장/외부 커버 전환", L"· 封面按钮 …… 切换内嵌/外部封面", L"· زر الغلاف …… تبديل غلاف مضمّن/خارجي",
		L"· Обложка …… встроенное/внешнее изображение", L"· Cover-Taste …… eingebettet/extern umschalten", L"· Capa …… alternar arte embutida/externa", L"· Omslag …… wissel ingebed/extern",
		L"· Okładka …… przełącz osadzoną/zewnętrzną", L"· Kapak …… gömülü/dış kapak geçişi")); yL += lh;
	body(L, yL, LL14(L"・残時間リング …… ジャケット上に残り時間と弧", L"· Remaining ring …… time+arc on jacket", L"· Anneau restant …… temps+arc sur pochette", L"· Anello rest …… tempo+arco su copertina",
		L"· Anillo resto …… tiempo+arco en carátula", L"· 남은시간 링 …… 자켓에 시간+호", L"· 剩余环 …… 封面上时间+弧", L"· حلقة المتبقي …… وقت+قوس على الغلاف",
		L"· Кольцо остатка …… время+дуга на обложке", L"· Restzeit-Ring …… Zeit+Bogen auf Cover", L"· Anel restante …… tempo+arco na capa", L"· Restring …… tijd+boog op omslag",
		L"· Pierścień …… czas+łuk na okładce", L"· Kalan halka …… kapakta süre+yay")); yL += lh;
	body(L, yL, LL14(L"・バナー右上 …… φ/LR相関。右クリック「位相相関メーター」でON/OFF（アナライザーと共通）", L"· Banner top-right …… φ/LR corr. RMB toggle (shared with Analyzer)", L"· Bannière …… φ/LR. Clic droit ON/OFF (partagé Analyseur)", L"· Banner …… φ/LR. Destro ON/OFF (condiviso Analizzatore)",
		L"· Banner …… φ/LR. Clic der. ON/OFF (compartido Analizador)", L"· 배너 우측 …… φ/LR. 우클릭 ON/OFF(애널라이저 공통)", L"· 横幅右上 …… φ/LR。右键开/关（与分析器共用）", L"· البانر …… φ/LR. يمين ON/OFF (مشترك مع المحلل)",
		L"· Баннер …… φ/LR. ПКМ ON/OFF (общее с анализатором)", L"· Banner …… φ/LR. RMB ON/OFF (geteilt mit Analyzer)", L"· Banner …… φ/LR. Direito ON/OFF (compartilhado)", L"· Banner …… φ/LR. Rechtsklik ON/OFF (gedeeld)",
		L"· Baner …… φ/LR. PPM ON/OFF (wspólne z Analizatorem)", L"· Banner …… φ/LR. Sağ tık ON/OFF (Analyzer ile ortak)")); yL += lh + 2;

	title(R, yR, LL14(L"再生 / シーク / A-B", L"Play / Seek / A-B", L"Lecture / Seek / A-B", L"Play / Seek / A-B",
		L"Play / Seek / A-B", L"재생 / 시크 / A-B", L"播放 / 定位 / A-B", L"تشغيل / Seek / A-B",
		L"Play / Seek / A-B", L"Play / Seek / A-B", L"Play / Seek / A-B", L"Play / Seek / A-B",
		L"Play / Seek / A-B", L"Play / Seek / A-B")); yR += titleLh;
	body(R, yR, LL14(L"・▶ / ⏸ / ■ / |◀ ▶| …… 再生・一時停止・停止・前後曲", L"· ▶ / ⏸ / ■ / |◀ ▶| …… play, pause, stop, prev/next", L"· ▶ / ⏸ / ■ …… lire, pause, stop, préc/suiv", L"· ▶ / ⏸ / ■ …… play, pausa, stop, prec/succ",
		L"· ▶ / ⏸ / ■ …… play, pausa, stop, ant/sig", L"· ▶ / ⏸ / ■ …… 재생·일시정지·정지·이전/다음", L"· ▶ / ⏸ / ■ …… 播放、暂停、停止、上一/下一", L"· ▶ / ⏸ / ■ …… تشغيل، إيقاف مؤقت، إيقاف، سابق/تالٍ",
		L"· ▶ / ⏸ / ■ …… play, пауза, стоп, пред/след", L"· ▶ / ⏸ / ■ …… Play, Pause, Stop, zurueck/weiter", L"· ▶ / ⏸ / ■ …… play, pausa, parar, ant/prox", L"· ▶ / ⏸ / ■ …… play, pauze, stop, vorige/volgende",
		L"· ▶ / ⏸ / ■ …… play, pauza, stop, poprz/nast", L"· ▶ / ⏸ / ■ …… play, duraklat, dur, önceki/sonraki")); yR += lh;
	body(R, yR, LL14(L"・シークバー …… ピンク帯=ループ。青つまみ=A-B。⇔でつまみ、他はシーク。左のロックでloop固定", L"· Seek …… pink=loop; blue=A-B; size-cursor moves thumbs. Lock pins loop", L"· Seek …… rose=boucle; bleu=A-B; verrou fixe la boucle", L"· Seek …… rosa=loop; blu=A-B; blocco fissa il loop",
		L"· Seek …… rosa=bucle; azul=A-B; bloqueo fija el bucle", L"· 시크 …… 분홍=루프; 파랑=A-B; 잠금으로 루프 고정", L"· 进度条 …… 粉=循环；蓝=A-B；锁定固定循环", L"· Seek …… وردي=حلقة؛ أزرق=A-B؛ القفل يثبت الحلقة",
		L"· Seek …… розовый=цикл; синий=A-B; блок фиксирует цикл", L"· Seek …… rosa=Schleife; blau=A-B; Sperre fixiert Schleife", L"· Seek …… rosa=loop; azul=A-B; trava fixa o loop", L"· Seek …… roze=lus; blauw=A-B; slot fixeert lus",
		L"· Seek …… rozowy=petla; niebieski=A-B; blokada pinuje petle", L"· Seek …… pembe=dongu; mavi=A-B; kilit donguyu sabitler")); yR += lh;
	body(R, yR, LL14(L"・Alt+ドラッグ …… 拍グリッドの位相(オフセット)をずらす。曲ごとに記憶", L"· Alt+drag …… shift beat-grid phase (offset). Saved per song", L"· Alt+glisser …… décaler la phase de grille. Par morceau", L"· Alt+trascina …… sfasa griglia. Per brano",
		L"· Alt+arrastrar …… desplaza fase de rejilla. Por pista", L"· Alt+드래그 …… 비트 그리드 위상 이동. 곡별 저장", L"· Alt+拖 …… 移动拍网格相位。按曲记忆", L"· Alt+سحب …… إزاحة طور الشبكة. لكل أغنية",
		L"· Alt+перетаскивание …… сдвиг фазы сетки. На трек", L"· Alt+Ziehen …… Beat-Raster-Phase. Pro Titel", L"· Alt+arrastar …… desloca fase da grade. Por faixa", L"· Alt+slepen …… verschuif beatrasterfase. Per nummer",
		L"· Alt+przeciągnij …… przesuń fazę siatki. Na utwór", L"· Alt+sürükle …… vuruş ızgarası fazını kaydır. Parça başına")); yR += lh;
	body(R, yR, LL14(L"・水色の下線 …… LRC時刻マーカー。クリックでシーク／A-B候補", L"· Cyan ticks …… LRC time marks. Click to seek / A-B candidate", L"· Traits cyan …… marques LRC. Clic = seek / A-B", L"· Tratti ciano …… marche LRC. Clic = seek / A-B",
		L"· Marcas cian …… LRC. Clic = seek / A-B", L"· 청록 눈금 …… LRC 시각. 클릭으로 시크/A-B", L"· 青色刻度 …… LRC 时刻。点击定位/A-B", L"· علامات سماوية …… LRC. نقر=seek/A-B",
		L"· Голубые метки …… LRC. Клик = seek / A-B", L"· Cyan-Striche …… LRC. Klick = Seek / A-B", L"· Marcas ciano …… LRC. Clique = seek / A-B", L"· Cyaan tikken …… LRC. Klik = seek / A-B",
		L"· Cyan znaczniki …… LRC. Klik = seek / A-B", L"· Camgöbeği işaret …… LRC. Tık = seek / A-B")); yR += lh;
	body(R, yR, LL14(L"・ホバー …… 波形があるとき拡大ルーペ（局所拡大）", L"· Hover …… local waveform magnifier when peaks exist", L"· Survol …… loupe d'onde locale", L"· Passaggio …… lente sull'onda",
		L"· Pasar …… lupa de onda local", L"· 호버 …… 파형 확대 루페", L"· 悬停 …… 局部波形放大镜", L"· مرور …… عدسة موجة محلية",
		L"· Наведение …… лупа волны", L"· Hover …… Wellen-Lupe", L"· Hover …… lupa de onda local", L"· Hover …… lokale golflens",
		L"· Najechanie …… lupa fali", L"· Üzerine gel …… yerel dalga büyüteci")); yR += lh;
	body(R, yR, LL14(L"・波形 …… 最奥に表示。再生で埋まり、WAVは全体概観に置換。右クリックでON/OFF", L"· Wave …… backmost layer; fills live, WAV gets full overview. RMB toggle", L"· Onde …… couche arrière; se remplit en live. Clic droit ON/OFF", L"· Onda …… strato posteriore; si riempie in live. Destro ON/OFF",
		L"· Onda …… capa trasera; se llena en vivo. Clic der. ON/OFF", L"· 파형 …… 최후층. 재생으로 채워지고 WAV는 전체 개요. 우클릭 ON/OFF", L"· 波形 …… 最底层；播放中填充，WAV换全曲概览。右键开/关", L"· الموجة …… أبعد طبقة؛ تُملأ أثناء التشغيل. يمين ON/OFF",
		L"· Волна …… самый задний слой; заполняется live. ПКМ ON/OFF", L"· Welle …… hinterste Ebene; fuellt sich live. RMB ON/OFF", L"· Onda …… camada de fundo; preenche ao vivo. Direito ON/OFF", L"· Golf …… achterste laag; vult live. Rechtsklik ON/OFF",
		L"· Fala …… najdalsza warstwa; wypełnia się live. PPM ON/OFF", L"· Dalga …… en arkadaki katman; canlı dolar. Sağ tık ON/OFF")); yR += lh;
	body(R, yR, LL14(L"・A / B / A-B解除 …… 区間ループ。フェードアウトも下部にあり", L"· A / B / Clear …… section loop. Fade-out is at the bottom", L"· A / B / Effacer …… boucle de section. Fondu en bas", L"· A / B / Cancella …… loop di sezione. Fade in basso",
		L"· A / B / Borrar …… bucle de sección. Fade abajo", L"· A / B / 해제 …… 구간 루프. 페이드는 하단", L"· A / B / 清除 …… 区间循环。淡出在底部", L"· A / B / مسح …… حلقة مقطع. التلاشي في الأسفل",
		L"· A / B / Сброс …… петля участка. Затухание внизу", L"· A / B / Aus …… Abschnittsloop. Fade unten", L"· A / B / Limpar …… loop de seção. Fade embaixo", L"· A / B / Uit …… sectielus. Fade onderaan",
		L"· A / B / Wyczyść …… pętla odcinka. Fade na dole", L"· A / B / Sil …… bölüm döngüsü. Fade altta")); yR += lh;
	body(R, yR, LL14(L"・R …… 現在±フレーズ秒をA-B。1-8 …… キュージャンプ。F2 …… タグ編集", L"· R …… phrase A-B. 1-8 …… cue jump. F2 …… edit tags", L"· R …… phrase A-B. 1-8 …… cues. F2 …… tags", L"· R …… frase A-B. 1-8 …… cue. F2 …… tag",
		L"· R …… frase A-B. 1-8 …… cues. F2 …… etiquetas", L"· R …… 프레이즈 A-B. 1-8 …… 큐. F2 …… 태그", L"· R …… 乐句A-B。1-8 …… 标记。F2 …… 标签", L"· R …… عبارة A-B. 1-8 …… إشارات. F2 …… وسوم",
		L"· R …… фраза A-B. 1-8 …… метки. F2 …… теги", L"· R …… Phrase A-B. 1-8 …… Cues. F2 …… Tags", L"· R …… frase A-B. 1-8 …… cues. F2 …… tags", L"· R …… frase A-B. 1-8 …… cues. F2 …… tags",
		L"· R …… fraza A-B. 1-8 …… cue. F2 …… tagi", L"· R …… cumle A-B. 1-8 …… cue. F2 …… etiket")); yR += lh;
	body(R, yR, LL14(L"・Sleep(ツール右クリック) …… 15/30/60分後にフェード停止", L"· Sleep (tools RMB) …… fade-stop after 15/30/60 min", L"· Sleep …… arret apres 15/30/60 min", L"· Sleep …… stop dopo 15/30/60 min",
		L"· Sleep …… parar tras 15/30/60 min", L"· Sleep …… 15/30/60분 후 페이드 정지", L"· Sleep …… 15/30/60分钟后淡出停止", L"· Sleep …… توقف بعد 15/30/60 د",
		L"· Sleep …… стоп через 15/30/60 мин", L"· Sleep …… Stop nach 15/30/60 Min", L"· Sleep …… parar apos 15/30/60 min", L"· Sleep …… stop na 15/30/60 min",
		L"· Sleep …… stop po 15/30/60 min", L"· Sleep …… 15/30/60 dk sonra dur")); yR += lh + 2;

	y = max(yL, yR) + 2;
	yL = y; yR = y;

	title(L, yL, LL14(L"サウンド調整", L"Sound controls", L"Réglages son", L"Controlli audio",
		L"Controles de sonido", L"사운드 조절", L"声音调节", L"ضبط الصوت",
		L"Звук", L"Soundregler", L"Controles de som", L"Geluidsregelaars",
		L"Regulacja dźwięku", L"Ses ayarları")); yL += titleLh;
	body(L, yL, LL14(L"・主音量 / DS音量 / KPI音量 …… 系統別レベル", L"· Master / DS / KPI volume …… per-path levels", L"· Volume maître / DS / KPI", L"· Volume master / DS / KPI",
		L"· Volumen máster / DS / KPI", L"· 주음량 / DS / KPI …… 경로별 레벨", L"· 主音量 / DS / KPI …… 各通路电平", L"· مستوى رئيسي / DS / KPI",
		L"· Громкость master / DS / KPI", L"· Master / DS / KPI-Lautstaerke", L"· Volume master / DS / KPI", L"· Master / DS / KPI-volume",
		L"· Głośność master / DS / KPI", L"· Ana / DS / KPI ses")); yL += lh;
	body(L, yL, LL14(L"・テンポ / ピッチ …… 再生速度と音程（曲ごと保存可）", L"· Tempo / Pitch …… speed and pitch (can save per song)", L"· Tempo / Hauteur …… vitesse et hauteur (par morceau)", L"· Tempo / Pitch …… velocità e altezza (per brano)",
		L"· Tempo / Tono …… velocidad y tono (por pista)", L"· 템포 / 피치 …… 속도·음정(곡별 저장 가능)", L"· 速度 / 音高 …… 可按曲保存", L"· الإيقاع / الطبقة …… السرعة والطبقة (لكل أغنية)",
		L"· Темп / Высота …… скорость и тон (на трек)", L"· Tempo / Pitch …… Tempo und Tonhoehe (pro Titel)", L"· Tempo / Tom …… velocidade e tom (por faixa)", L"· Tempo / Pitch …… snelheid en toonhoogte (per nummer)",
		L"· Tempo / Pitch …… prędkość i wysokość (na utwór)", L"· Tempo / Pitch …… hız ve perde (parça başına)")); yL += lh;
	body(L, yL, LL14(L"・マイクミックス / レベル …… WAV保存時などにマイクを混ぜる", L"· Mic mix / level …… blend mic (e.g. while saving WAV)", L"· Mix micro …… mélanger le micro (ex. WAV)", L"· Mix micro …… mescola microfono (es. WAV)",
		L"· Mezcla micro …… mezclar micro (p. ej. WAV)", L"· 마이크 믹스 …… WAV 저장 등에 마이크 혼합", L"· 麦克风混音 …… 如保存 WAV 时混入麦克风", L"· مزج الميك …… خلط الميك (مثل حفظ WAV)",
		L"· Микс микрофона …… подмешать (напр. при WAV)", L"· Mikrofon-Mix …… Mikro beim WAV-Speichern mischen", L"· Mix micro …… misturar micro (ex. WAV)", L"· Mic-mix …… microfoon mengen (bijv. WAV)",
		L"· Mix mikrofonu …… zmieszaj mik (np. przy WAV)", L"· Mikrofon karışımı …… WAV kaydında mik karıştır")); yL += lh + 2;

	title(R, yR, LL14(L"プレイリスト操作", L"Playlist ops", L"Opérations liste", L"Operazioni playlist",
		L"Operaciones de lista", L"재생목록 조작", L"播放列表操作", L"عمليات القائمة",
		L"Операции плейлиста", L"Playlist-Aktionen", L"Operações de lista", L"Afspeellijst-acties",
		L"Operacje listy", L"Liste işlemleri")); yR += titleLh;
	body(R, yR, LL14(L"・連続 / ループ / ランダム …… 再生モード。ループ回数は下部", L"· Continuous / Loop / Random …… modes. Loop count at bottom", L"· Continue / Boucle / Aléatoire …… modes. Nb boucles en bas", L"· Continua / Loop / Casuale …… modalità. Conteggio in basso",
		L"· Continua / Bucle / Aleatorio …… modos. Cuenta abajo", L"· 연속 / 루프 / 랜덤 …… 모드. 루프 횟수는 하단", L"· 连续 / 循环 / 随机 …… 模式。循环次数在底部", L"· متتابع / حلقة / عشوائي …… أوضاع. العدد أسفل",
		L"· Подряд / Цикл / Случайно …… режимы. Счётчик внизу", L"· Folge / Schleife / Zufall …… Modi. Zaehler unten", L"· Contínuo / Loop / Aleatório …… modos. Contagem embaixo", L"· Doorlopend / Lus / Willekeurig …… modi. Aantal onder",
		L"· Ciągłe / Pętla / Losowo …… tryby. Licznik na dole", L"· Sürekli / Döngü / Rastgele …… modlar. Sayı altta")); yR += lh;
	body(R, yR, LL14(L"・名前変更 / リスト削除 / 曲削除 …… PL管理。D&Dで並べ替え", L"· Rename / Delete list / Remove …… manage PL. D&D to reorder", L"· Renommer / Suppr. liste / Retirer …… D&D pour trier", L"· Rinomina / Elimina lista / Rimuovi …… D&D per ordine",
		L"· Renombrar / Eliminar lista / Quitar …… D&D para ordenar", L"· 이름변경 / 목록삭제 / 곡삭제 …… D&D로 정렬", L"· 重命名 / 删列表 / 删曲 …… 拖放排序", L"· إعادة تسمية / حذف قائمة / حذف …… سحب للترتيب",
		L"· Переименовать / Удалить список / Удалить …… D&D порядок", L"· Umbenennen / Liste loeschen / Entfernen …… D&D sortieren", L"· Renomear / Excluir lista / Remover …… D&D ordenar", L"· Hernoemen / Lijst wissen / Verwijder …… D&D ordenen",
		L"· Zmień nazwę / Usuń listę / Usuń …… D&D kolejność", L"· Yeniden adlandır / Liste sil / Parça sil …… D&D sırala")); yR += lh;
	body(R, yR, LL14(L"・m3u入出力 / 検索 / 絞り込み / 正規表現 / ▾ツール …… 並べ替え・Folder+", L"· m3u I/O / Find / Filter / Regex / ▾ tools …… sort & Folder+", L"· m3u / Recherche / Filtre / Regex / ▾ …… tri et Folder+", L"· m3u / Cerca / Filtro / Regex / ▾ …… ordina e Folder+",
		L"· m3u / Buscar / Filtro / Regex / ▾ …… orden y Folder+", L"· m3u / 검색 / 필터 / 정규식 / ▾ …… 정렬·Folder+", L"· m3u / 搜索 / 筛选 / 正则 / ▾ …… 排序与 Folder+", L"· m3u / بحث / تصفية / Regex / ▾ …… فرز و Folder+",
		L"· m3u / Поиск / Фильтр / Regex / ▾ …… сорт и Folder+", L"· m3u / Suche / Filter / Regex / ▾ …… Sort und Folder+", L"· m3u / Busca / Filtro / Regex / ▾ …… ordem e Folder+", L"· m3u / Zoeken / Filter / Regex / ▾ …… sorteren en Folder+",
		L"· m3u / Szukaj / Filtr / Regex / ▾ …… sort i Folder+", L"· m3u / Ara / Filtre / Regex / ▾ …… sırala ve Folder+")); yR += lh;
	body(R, yR, LL14(L"・名前の印 …… 橙SAV=曲ごと保存 / 青LRC=歌詞 / 緑MONO·LR·2.1…=ch。色タグ（PLの印列も同じ）", L"· Name marks …… amber SAV=per-song / blue LRC=lyrics / green MONO·LR·2.1…=ch (same in PL Mark col)", L"· Marques …… orange SAV / bleu LRC / vert MONO·LR·2.1…=ch", L"· Segni …… arancio SAV / blu LRC / verde MONO·LR·2.1…=ch",
		L"· Marcas …… naranja SAV / azul LRC / verde MONO·LR·2.1…=ch", L"· 이름 표시 …… 주황 SAV / 파랑 LRC / 초록 MONO·LR·2.1…=ch", L"· 名称标记 …… 橙SAV / 蓝LRC / 绿MONO·LR·2.1…=ch", L"· علامات …… برتقالي SAV / أزرق LRC / أخضر MONO·LR·2.1…=ch",
		L"· Метки …… оранж. SAV / син. LRC / зел. MONO·LR·2.1…=ch", L"· Zeichen …… orange SAV / blau LRC / gruen MONO·LR·2.1…=ch", L"· Marcas …… laranja SAV / azul LRC / verde MONO·LR·2.1…=ch", L"· Tekens …… oranje SAV / blauw LRC / groen MONO·LR·2.1…=ch",
		L"· Znaki …… pomarańcz. SAV / nieb. LRC / ziel. MONO·LR·2.1…=ch", L"· İşaret …… turuncu SAV / mavi LRC / yeşil MONO·LR·2.1…=ch")); yR += lh + 2;

	y = max(yL, yR) + 2;
	yL = y; yR = y;

	title(L, yL, LL14(L"ライブラリ / 履歴", L"Library / History", L"Bibliothèque / Historique", L"Libreria / Cronologia",
		L"Biblioteca / Historial", L"라이브러리 / 기록", L"媒体库 / 历史", L"المكتبة / السجل",
		L"Библиотека / История", L"Bibliothek / Verlauf", L"Biblioteca / Histórico", L"Bibliotheek / Geschiedenis",
		L"Biblioteka / Historia", L"Kitaplık / Geçmiş")); yL += titleLh;
	body(L, yL, LL14(L"・Lib …… 左ドロワー。ルート追加でフォルダを登録し曲を収集", L"· Lib …… left drawer. Add roots to register folders and collect tracks", L"· Lib …… tiroir. Ajouter des racines pour collecter", L"· Lib …… cassetto. Aggiungi root per raccogliere",
		L"· Lib …… cajón. Añadir raíces para recopilar", L"· Lib …… 왼쪽 드로어. 루트 추가로 폴더 등록", L"· Lib …… 左侧抽屉。添加根目录收集曲目", L"· Lib …… درج أيسر. أضف جذوراً لجمع المقاطع",
		L"· Lib …… левый ящик. Корни для сбора треков", L"· Lib …… linke Schublade. Roots zum Sammeln", L"· Lib …… gaveta. Adicionar raízes para coletar", L"· Lib …… lade. Roots toevoegen om te verzamelen",
		L"· Lib …… lewa szuflada. Dodaj korzenie by zbierać", L"· Lib …… sol çekmece. Kök ekleyip parça topla")); yL += lh;
	body(L, yL, LL14(L"・Hist …… 日付つき再生履歴(最大64)。ダブルクリックで再生（該当PLへ切替）", L"· Hist …… dated play history (max 64). Double-click to play (switch PL when found)", L"· Hist …… historique date (max 64). Double-clic pour lire", L"· Hist …… cronologia datata (max 64). Doppio clic",
		L"· Hist …… historial con fecha (max 64). Doble clic", L"· Hist …… 날짜 재생기록(최대64). 더블클릭으로 재생", L"· Hist …… 带日期历史(最多64)。双击播放", L"· Hist …… سجل بتاريخ (حتى 64). نقر مزدوج للتشغيل",
		L"· Hist …… история с датой (до 64). Двойной щелчок", L"· Hist …… datierte Historie (max 64). Doppelklick", L"· Hist …… histórico datado (max 64). Duplo clique", L"· Hist …… geschiedenis met datum (max 64)",
		L"· Hist …… historia z data (max 64). Dwuklik", L"· Hist …… tarihli geçmiş (max 64). Çift tıkla çal")); yL += lh + 2;

	title(R, yR, LL14(L"歌詞パネル", L"Lyrics panel", L"Panneau paroles", L"Pannello testi",
		L"Panel de letras", L"가사 패널", L"歌词面板", L"لوحة الكلمات",
		L"Панель текста", L"Textfenster", L"Painel de letras", L"Tekstpaneel",
		L"Panel tekstu", L"Söz paneli")); yR += titleLh;
	body(R, yR, LL14(L"・▾/▴ …… 左クリックで拡大。右クリックで歌詞メニュー（ウィンドウ表示・LRC微調整）", L"· ▾/▴ …… left-click expands. Right-click lyrics menu (window, LRC nudge)", L"· ▾/▴ …… clic gauche agrandit. Clic droit = menu paroles", L"· ▾/▴ …… clic sinistro espande. Clic destro = menu testi",
		L"· ▾/▴ …… clic izq. amplía. Clic der. = menú letra", L"· ▾/▴ …… 왼쪽 클릭 확대. 오른쪽 클릭=가사 메뉴", L"· ▾/▴ …… 左键扩大。右键=歌词菜单", L"· ▾/▴ …… نقر يسار للتوسيع. يمين=قائمة الكلمات",
		L"· ▾/▴ …… ЛКМ расширяет. ПКМ = меню текста", L"· ▾/▴ …… Linksklick erweitert. Rechtsklick = Textmenue", L"· ▾/▴ …… clique esq. amplia. Dir. = menu de letra", L"· ▾/▴ …… linksklik vergroot. Rechtsklik = tekstmenu",
		L"· ▾/▴ …… LPM rozszerza. PPM = menu tekstu", L"· ▾/▴ …… sol tik genisletir. Sag tik = soz menusu")); yR += lh;
	body(R, yR, LL14(L"・拡大時はカラオケ風ビュー。同期LRCがあれば追従表示", L"· Expanded = karaoke-style view; follows synced LRC when present", L"· Agrandi = vue karaoké; suit LRC synchronisé", L"· Espanso = vista karaoke; segue LRC sincronizzato",
		L"· Ampliado = vista karaoke; sigue LRC sincronizado", L"· 확대 시 노래방 뷰. 동기 LRC 추종", L"· 扩大为卡拉OK风；有同步 LRC 则跟随", L"· موسّع = عرض كاريوكي؛ يتبع LRC المتزامن",
		L"· Расширение = караоке; следует за синхронным LRC", L"· Erweitert = Karaoke-Ansicht; folgt sync-LRC", L"· Ampliado = vista karaokê; segue LRC sincronizado", L"· Uitgeklapt = karaokeweergave; volgt sync-LRC",
		L"· Rozszerzony = widok karaoke; śledzi zsynchronizowane LRC", L"· Geniş = karaoke görünümü; senkron LRC izler")); yR += lh;
	body(R, yR, LL14(L"・歌詞ウィンドウ …… 常時最前面。不透明度・表示行数・フォントはウィンドウ右クリック", L"· Lyrics window …… always on top. Opacity/lines/font via window RMB", L"· Fenetre paroles …… premier plan. Opacite/lignes/police au clic droit", L"· Finestra testi …… in primo piano. Opacita/righe/font col destro",
		L"· Ventana letra …… siempre visible. Opacidad/lineas/fuente con clic der.", L"· 가사 창 …… 항상 위. 불투명도·행수·글꼴은 창 우클릭", L"· 歌词窗口 …… 置顶。不透明度/行数/字体在窗口右键", L"· نافذة الكلمات …… في المقدمة. العتامة/الأسطر/الخط بزر يمين",
		L"· Окно текста …… поверх всех. Непрозрачность/строки/шрифт — ПКМ", L"· Textfenster …… Vordergrund. Deckkraft/Zeilen/Schrift per RMB", L"· Janela de letra …… no topo. Opacidade/linhas/fonte no botao dir.", L"· Songtekstvenster …… bovenop. Dekking/regels/lettertype via RMB",
		L"· Okno tekstu …… na wierzchu. Nieprzezroczystosc/wiersze/czcionka przez PPM", L"· Soz penceresi …… her zaman ustte. Opaklik/satir/yazi pencere sag tik")); yR += lh + 2;

	y = max(yL, yR) + 2;
	yL = y; yR = y;

	title(L, yL, LL14(L"連携ツール", L"Linked tools", L"Outils liés", L"Strumenti collegati",
		L"Herramientas vinculadas", L"연동 도구", L"联动工具", L"أدوات مرتبطة",
		L"Связанные окна", L"Verknuepfte Tools", L"Ferramentas ligadas", L"Gekoppelde tools",
		L"Powiązane narzędzia", L"Bağlı araçlar")); yL += titleLh;
	body(L, yL, LL14(L"・EQ / ピアノロール / アナライザー / 詳細 …… 各窓に ? ガイドあり", L"· EQ / Piano roll / Analyzer / Extra …… each has its own ? guide", L"· EQ / Piano / Analyseur / Extra …… chacun a un guide ?", L"· EQ / Piano / Analizzatore / Extra …… ciascuno ha guida ?",
		L"· EQ / Piano / Analizador / Extra …… cada uno tiene guía ?", L"· EQ / 피아노 / 분석기 / 상세 …… 각 창에 ? 가이드", L"· EQ / 钢琴卷 / 分析器 / 详情 …… 各窗有 ? 指南", L"· EQ / بيانو / محلل / تفاصيل …… لكل منها دليل ?",
		L"· EQ / Пианоролл / Анализатор / Доп. …… у каждого свой ?", L"· EQ / Piano / Analyzer / Extra …… jeweils eigener ?-Guide", L"· EQ / Piano / Analisador / Extra …… cada um tem guia ?", L"· EQ / Piano / Analyser / Extra …… elk heeft ?-gids",
		L"· EQ / Piano / Analizator / Extra …… każde ma przewodnik ?", L"· EQ / Piano / Analizör / Extra …… her birinin ? kılavuzu var")); yL += lh;
	body(L, yL, LL14(L"・プロンプト / ロール …… 時刻付きコマンドで再生中パラメータ変更", L"· Prompt / Roll …… timed commands change params during play", L"· Prompt / Rouleau …… commandes horodatées", L"· Prompt / Roll …… comandi a tempo",
		L"· Prompt / Roll …… comandos temporizados", L"· 프롬프트 / 롤 …… 시간 명령으로 재생 중 변경", L"· 提示 / 卷轴 …… 定时命令改播放参数", L"· الموجه / الرول …… أوامر موقوتة",
		L"· Промпт / Ролл …… команды по времени", L"· Prompt / Roll …… Zeitbefehle aendern Parameter", L"· Prompt / Roll …… comandos com tempo", L"· Prompt / Roll …… getimede opdrachten",
		L"· Prompt / Roll …… komendy czasowe", L"· Prompt / Rulo …… zamanlı komutlar")); yL += lh;
	body(L, yL, LL14(L"・録音 / キャプチャ / WAV保存 …… デバイス録音・画面録画・書き出し", L"· Record / Capture / WAV …… device record, screen capture, export", L"· Enreg. / Capture / WAV …… enregistrement et export", L"· Registra / Cattura / WAV …… registrazione ed export",
		L"· Grabar / Captura / WAV …… grabación y exportación", L"· 녹음 / 캡처 / WAV …… 장치 녹음·화면 녹화·내보내기", L"· 录音 / 捕获 / WAV …… 设备录音、屏录、导出", L"· تسجيل / التقاط / WAV …… تسجيل وتصدير",
		L"· Запись / Захват / WAV …… запись и экспорт", L"· Aufnahme / Capture / WAV …… Aufnahme und Export", L"· Gravar / Captura / WAV …… gravação e exportação", L"· Opnemen / Capture / WAV …… opname en export",
		L"· Nagraj / Capture / WAV …… nagrywanie i eksport", L"· Kaydet / Yakalama / WAV …… kayıt ve dışa aktarma")); yL += lh;
	body(L, yL, LL14(L"・騒音計 / 起こし台 / ボイスチェンジャー / チューナー道場 / フォトフレーム / Soft3D迷路", L"· Sound meter / Digitizer / Voice changer / Tuner practice / Photo frame / Soft3D maze", L"· Sonomètre / Numériseur / Changeur de voix / Accordeur / Cadre photo / Labyrinthe Soft3D", L"· Fonometro / Digitalizzatore / Cambia voce / Accordatore / Cornice / Labirinto Soft3D",
		L"· Medidor / Digitalizador / Cambiador / Afinador / Marco / Laberinto Soft3D", L"· 소음계 / 디지타이저 / 보이스체인저 / 튜너 / 포토프레임 / Soft3D 미로", L"· 声级计 / 数字化 / 变声器 / 调音练习 / 照片框 / Soft3D 迷宫", L"· مقياس صوت / محول / مغير صوت / موالف / إطار صور / متاهة Soft3D",
		L"· Шумомер / Оцифровка / Голос / Тюнер / Фоторамка / Лабиринт Soft3D", L"· Schallpegel / Digitalisierer / Stimmenwandler / Stimmtraining / Fotorahmen / Soft3D-Labyrinth", L"· Medidor / Digitalizador / Modificador / Afinador / Moldura / Labirinto Soft3D", L"· Geluidsmeter / Digitizer / Stemvervormer / Stemtrainer / Fotolijst / Soft3D-doolhof",
		L"· Miernik / Digitalizacja / Zmiana głosu / Stroik / Ramka / Labirynt Soft3D", L"· Ses ölçer / Dijitalleştirici / Ses değiştirici / Akort / Çerçeve / Soft3D labirent")); yL += lh + 2;

	title(R, yR, LL14(L"保存 / 設定 / 切替", L"Save / Settings / Switch", L"Sauver / Réglages / Basculer", L"Salva / Impostazioni / Passa",
		L"Guardar / Ajustes / Cambiar", L"저장 / 설정 / 전환", L"保存 / 设置 / 切换", L"حفظ / إعدادات / تبديل",
		L"Сохранить / Настройки / Переключить", L"Speichern / Einstellungen / Wechsel", L"Salvar / Config. / Alternar", L"Opslaan / Instellingen / Wisselen",
		L"Zapisz / Ustawienia / Przełącz", L"Kaydet / Ayarlar / Geçiş")); yR += titleLh;
	body(R, yR, LL14(L"・途中保存 / DShow途中保存 / 曲ごとに設定保存 …… 位置・音量等", L"· Resume / DShow resume / per-song save …… position, volume, etc.", L"· Reprise / DShow / par morceau …… position, volume…", L"· Ripresa / DShow / per brano …… posizione, volume…",
		L"· Reanudar / DShow / por pista …… posición, volumen…", L"· 위치저장 / DShow / 곡별 …… 위치·음량 등", L"· 续播 / DShow / 逐曲 …… 位置、音量等", L"· استئناف / DShow / لكل أغنية …… موضع ومستوى…",
		L"· Позиция / DShow / на трек …… позиция, громкость…", L"· Position / DShow / pro Titel …… Position, Lautstaerke…", L"· Retomar / DShow / por faixa …… posição, volume…", L"· Hervatten / DShow / per nummer …… positie, volume…",
		L"· Wznowienie / DShow / na utwór …… pozycja, głośność…", L"· Konum / DShow / parça …… konum, ses…")); yR += lh;
	body(R, yR, LL14(L"・設定 / フォルダ / ファルコム特化型へ …… 出力設定・パス・本窓切替", L"· Settings / Folder / To Falcom …… output, paths, switch main UI", L"· Réglages / Dossier / Falcom …… sortie, chemins, bascule", L"· Impostazioni / Cartella / Falcom …… uscita, percorsi, passa",
		L"· Ajustes / Carpeta / Falcom …… salida, rutas, cambiar", L"· 설정 / 폴더 / 팔콤 …… 출력·경로·화면 전환", L"· 设置 / 文件夹 / Falcom …… 输出、路径、切主界面", L"· إعدادات / مجلد / Falcom …… إخراج ومسارات وتبديل",
		L"· Настройки / Папка / Falcom …… вывод, пути, переключение", L"· Einstellungen / Ordner / Falcom …… Ausgabe, Pfade, Wechsel", L"· Config. / Pasta / Falcom …… saída, caminhos, alternar", L"· Instellingen / Map / Falcom …… uitvoer, paden, wisselen",
		L"· Ustawienia / Folder / Falcom …… wyjście, ścieżki, przełącz", L"· Ayarlar / Klasör / Falcom …… çıkış, yollar, geçiş")); yR += lh;
	body(R, yR, LL14(L"・ツールチップ / ステレオ / 最小化連動 / スペアナ …… 表示オプション", L"· Tooltips / Stereo / Min.sync / Spectrum …… display options", L"· Infobulles / Stéréo / Sync.min / Spectre …… affichage", L"· Suggerimenti / Stereo / Sinc.min / Spettro …… visualizzazione",
		L"· Sugerencias / Estéreo / Sincr.min / Espectro …… visualización", L"· 툴팁 / 스테레오 / 최소화연동 / 스펙트럼 …… 표시 옵션", L"· 工具提示 / 立体声 / 最小化联动 / 频谱 …… 显示选项", L"· تلميحات / ستيريو / تزامن تصغير / طيف …… خيارات العرض",
		L"· Подсказки / Стерео / Синхр.сверт. / Спектр …… отображение", L"· Tooltips / Stereo / Min.-Sync / Spektrum …… Anzeige", L"· Dicas / Stereo / Sinc.min / Espectro …… exibição", L"· Tooltips / Stereo / Min.koppel / Spectrum …… weergave",
		L"· Etykiety / Stereo / Synch.min / Widmo …… wyświetlanie", L"· İpuçları / Stereo / Min.eşit / Spektrum …… görünüm")); yR += lh + 2;

	y = max(yL, yR) + 2;
	
	title(L, y, LL14(L"附属ツール（続き）", L"Add-on tools (more)", L"Outils annexes (suite)", L"Strumenti extra",
		L"Herramientas extra", L"부가 도구(계속)", L"附属工具（续）", L"أدوات إضافية",
		L"Доп. инструменты", L"Zusatztools", L"Ferramentas extras", L"Extra tools",
		L"Dodatkowe narzędzia", L"Ek araçlar"));
	y += titleLh;
	body(L, y, LL14(L"・A-B素材パック / 正規化バッチ / MusicBrainz自動タグ …… リスト右クリック", L"· A-B pack / normalize batch / MusicBrainz auto-tag …… list RMB", L"· Pack A-B / normalisation / MusicBrainz …… clic droit liste", L"· Pack A-B / normalizza / MusicBrainz …… destro lista",
		L"· Pack A-B / normalizar / MusicBrainz …… clic der. lista", L"· A-B 팩 / 정규화 배치 / MusicBrainz …… 목록 우클릭", L"· A-B素材包 / 标准化批处理 / MusicBrainz …… 列表右键", L"· حزمة A-B / تطبيع / MusicBrainz …… يمين القائمة",
		L"· Пакет A-B / нормализация / MusicBrainz …… ПКМ списка", L"· A-B-Pack / Norm-Batch / MusicBrainz …… Listen-RMB", L"· Pacote A-B / normalizar / MusicBrainz …… direito lista", L"· A-B-pak / normalisatie / MusicBrainz …… lijst-RMB",
		L"· Pakiet A-B / normalizacja / MusicBrainz …… PPM listy", L"· A-B paketi / normalizasyon / MusicBrainz …… liste sag tik")); y += lh;
	body(L, y, LL14(L"・DJパッド / アラーム / ミラー出力 / localhost操作 / SSビジュアライザ", L"· DJ pad / alarm / mirror out / localhost remote / SS visualizer", L"· Pad DJ / alarme / miroir / remote local / visualiseur SS", L"· Pad DJ / sveglia / mirror / remote locale / visualizzatore SS",
		L"· Pad DJ / alarma / espejo / remoto local / visualizador SS", L"· DJ 패드 / 알람 / 미러 / localhost / SS 비주얼", L"· DJ垫 / 闹钟 / 镜像输出 / 本机遥控 / SS可视化", L"· لوحة DJ / منبّه / مرآة / تحكم محلي / عارض SS",
		L"· DJ-пад / будильник / зеркало / localhost / SS-виз", L"· DJ-Pad / Wecker / Spiegel / localhost / SS-Viz", L"· Pad DJ / alarme / espelho / remoto local / visual SS", L"· DJ-pad / wekker / spiegel / localhost / SS-viz",
		L"· Pad DJ / budzik / lustro / localhost / wizual SS", L"· DJ pad / alarm / ayna / localhost / SS gorsel")); y += lh;
	body(L, y, LL14(L"・動画→WAV抽出 / 動画の音声差し替え(WAV→MP4) / ゲーム録画プリセット(画質選択→画面キャプチャ)", L"· Video→WAV extract / replace video audio (WAV→MP4) / game-capture preset (pick quality → screen capture)", L"· Video→WAV / remplacer audio (WAV→MP4) / preset jeu (qualite → capture)", L"· Video→WAV / sostituisci audio (WAV→MP4) / preset gioco (qualita → cattura)",
		L"· Video→WAV / reemplazar audio (WAV→MP4) / preset captura (calidad → captura)", L"· 동영상→WAV / 오디오 교체(WAV→MP4) / 게임 녹화 프리셋(화질 선택→화면 캡처)", L"· 视频→WAV / 替换音频(WAV→MP4) / 游戏录制预设(选画质→画面捕获)", L"· فيديو→WAV / استبدال الصوت (WAV→MP4) / إعداد لعبة (جودة → التقاط)",
		L"· Видео→WAV / замена звука (WAV→MP4) / пресет игры (качество → захват)", L"· Video→WAV / Audio ersetzen (WAV→MP4) / Game-Preset (Qualität → Capture)", L"· Video→WAV / substituir audio (WAV→MP4) / preset jogo (qualidade → captura)", L"· Video→WAV / audio vervangen (WAV→MP4) / game-preset (kwaliteit → capture)",
		L"· Wideo→WAV / zamiana audio (WAV→MP4) / preset gry (jakosc → przechwytywanie)", L"· Video→WAV / ses degistir (WAV→MP4) / oyun on ayari (kalite → yakalama)")); y += lh + 2;


	title(L, y, LL14(L"キーボードショートカット", L"Keyboard shortcuts", L"Raccourcis clavier", L"Scorciatoie",
		L"Atajos de teclado", L"키보드 단축키", L"键盘快捷键", L"اختصارات لوحة المفاتيح",
		L"Горячие клавиши", L"Tastenkuerzel", L"Atalhos de teclado", L"Sneltoetsen",
		L"Skróty klawiszowe", L"Klavye kısayolları"));
	y += titleLh;
	body(L, y, LL14(L"・Space …… 再生/一時停止　　←/→ …… シーク　　Home/End …… 曲頭/曲末付近", L"· Space …… Play/Pause　　←/→ …… Seek　　Home/End …… start/near end", L"· Espace …… Lecture/Pause　　←/→ …… Seek　　Home/End …… début/fin", L"· Spazio …… Play/Pausa　　←/→ …… Seek　　Home/End …… inizio/fine",
		L"· Espacio …… Play/Pausa　　←/→ …… Seek　　Home/End …… inicio/fin", L"· Space …… 재생/일시정지　　←/→ …… 시크　　Home/End …… 곡처음/끝", L"· Space …… 播放/暂停　　←/→ …… 定位　　Home/End …… 曲头/近曲末", L"· Space …… تشغيل/إيقاف　　←/→ …… Seek　　Home/End …… بداية/نهاية",
		L"· Space …… Play/Пауза　　←/→ …… Seek　　Home/End …… начало/конец", L"· Leertaste …… Play/Pause　　←/→ …… Seek　　Home/End …… Anfang/Ende", L"· Espaço …… Play/Pausa　　←/→ …… Seek　　Home/End …… início/fim", L"· Spatie …… Play/Pauze　　←/→ …… Seek　　Home/End …… begin/einde",
		L"· Spacja …… Play/Pauza　　←/→ …… Seek　　Home/End …… początek/koniec", L"· Space …… Play/Duraklat　　←/→ …… Seek　　Home/End …… baş/son")); y += lh;
	body(L, y, LL14(L"・PgUp/PgDn …… 前/次曲　　Enter …… 検索次候補　　? …… 本ガイド（入力欄優先）", L"· PgUp/PgDn …… Prev/Next　　Enter …… next find　　? …… this guide (typing wins in edits)", L"· PgUp/PgDn …… Préc/Suiv　　Entrée …… recherche　　? …… ce guide", L"· PgUp/PgDn …… Prec/Succ　　Invio …… cerca　　? …… questa guida",
		L"· PgUp/PgDn …… Ant/Sig　　Enter …… buscar　　? …… esta guía", L"· PgUp/PgDn …… 이전/다음　　Enter …… 검색　　? …… 이 가이드", L"· PgUp/PgDn …… 上一/下一　　Enter …… 搜索　　? …… 本指南", L"· PgUp/PgDn …… سابق/تالٍ　　Enter …… بحث　　? …… هذا الدليل",
		L"· PgUp/PgDn …… Пред/След　　Enter …… поиск　　? …… это руководство", L"· PgUp/PgDn …… Zurueck/Weiter　　Enter …… Suche　　? …… dieser Guide", L"· PgUp/PgDn …… Ant/Prox　　Enter …… busca　　? …… este guia", L"· PgUp/PgDn …… Vorige/Volgende　　Enter …… zoeken　　? …… deze gids",
		L"· PgUp/PgDn …… Poprz/Nast　　Enter …… szukaj　　? …… ten przewodnik", L"· PgUp/PgDn …… Önceki/Sonraki　　Enter …… ara　　? …… bu kılavuz")); y += lh + 2;

	yL = y; yR = y;
	title(L, yL, LL14(L"シーク拡張（右クリック）", L"Seek extras (RMB)", L"Extras Seek (clic droit)", L"Extra Seek (destro)",
		L"Extras Seek (clic der.)", L"시크 확장(우클릭)", L"进度条扩展（右键）", L"إضافات Seek (يمين)",
		L"Доп. Seek (ПКМ)", L"Seek-Extras (RMB)", L"Extras Seek (direito)", L"Seek-extra's (rechtsklik)",
		L"Dodatki Seek (PPM)", L"Seek ekleri (sağ tık)")); yL += titleLh;
	body(L, yL, LL14(L"・細いスペアナリボン / 拍グリッド / 書き出しxfade帯プレビュー", L"· Thin spectrum ribbon / beat grid / export xfade-band preview", L"· Ruban spectre / grille / aperçu bande xfade", L"· Nastro spettro / griglia / anteprima xfade",
		L"· Cinta espectro / rejilla / vista previa xfade", L"· 얇은 스펙트럼 리본 / 박자 그리드 / xfade 미리보기", L"· 细频谱带 / 拍网格 / 导出交叉淡入预览", L"· شريط طيف / شبكة نبض / معاينة xfade",
		L"· Лента спектра / сетка / превью xfade", L"· Spektrum-Band / Beat-Raster / Xfade-Vorschau", L"· Fita espectro / grade / prévia xfade", L"· Spectrumlint / beatraster / xfade-voorbeeld",
		L"· Wstęga widma / siatka / podgląd xfade", L"· İnce spektrum şeridi / vuruş ızgarası / xfade önizleme")); yL += lh;
	body(L, yL, LL14(L"・ホバーで時刻チップ。キュー追加／1–8でジャンプ", L"· Hover time tip. Add cues / jump with 1–8", L"· Infobulle temps au survol. Cues / saut 1–8", L"· Suggerimento tempo al passaggio. Cue / salto 1–8",
		L"· Tip de tiempo al pasar. Cues / salto 1–8", L"· 호버 시각 팁. 큐 추가 / 1–8 점프", L"· 悬停显示时刻。添加标记 / 1–8 跳转", L"· تلميح الوقت عند المرور. إشارات / قفز 1–8",
		L"· Подсказка времени при наведении. Метки / прыжок 1–8", L"· Hover-Zeit-Tipp. Cues / Sprung 1–8", L"· Dica de tempo no hover. Cues / salto 1–8", L"· Hover-tijdtip. Cues / spring 1–8",
		L"· Podpowiedź czasu przy najechaniu. Cue / skok 1–8", L"· Üzerine gelince zaman ipucu. Cue / 1–8 atla")); yL += lh + 2;

	title(R, yR, LL14(L"ツール拡張（▾ / リスト右クリック）", L"Tools extras (▾ / list RMB)", L"Extras outils (▾ / liste)", L"Extra strumenti (▾ / lista)",
		L"Extras herramientas (▾ / lista)", L"도구 확장(▾ / 목록 우클릭)", L"工具扩展（▾ / 列表右键）", L"إضافات الأدوات (▾ / قائمة)",
		L"Доп. инструменты (▾ / список)", L"Tool-Extras (▾ / Liste RMB)", L"Extras ferramentas (▾ / lista)", L"Tool-extra's (▾ / lijst RMB)",
		L"Dodatki narzędzi (▾ / lista)", L"Araç ekleri (▾ / liste sağ tık)")); yR += titleLh;
	body(R, yR, LL14(L"・スマートPL / Up Next一覧 / 欠損整理 / 重複ダイアログ / フォルダ同期リスト", L"· Smart PL / Up Next panel / missing manage / dupes dlg / folder sync lists", L"· Smart PL / Up Next / manquants / doublons / sync dossier", L"· Smart PL / Up Next / mancanti / duplicati / sync",
		L"· Smart PL / Up Next / faltantes / duplicados / sync", L"· 스마트PL / Up Next / 결손정리 / 중복 / 폴더동기", L"· 智能PL / Up Next / 缺失整理 / 重复 / 文件夹同步", L"· قوائم ذكية / Up Next / مفقود / مكررات / مزامنة",
		L"· Умные списки / Up Next / отсутствующие / дубли / sync", L"· Smart-PL / Up Next / Fehlende / Duplikate / Sync", L"· Smart PL / Up Next / ausentes / duplicatas / sync", L"· Slimme PL / Up Next / ontbrekend / duplicaten / sync",
		L"· Smart PL / Up Next / brakujace / duplikaty / sync", L"· Akilli PL / Up Next / eksik / yinelenen / sync")); yR += lh;
	body(R, yR, LL14(L"・★レーティング / 練習テンポ50·75·100% / A-Bスナップ / 正規化プレビュー", L"· ★ rating / practice tempo 50·75·100% / A-B snap / normalize preview", L"· ★ note / tempo 50·75·100% / snap A-B / aperçu normalisation", L"· ★ voto / tempo 50·75·100% / snap A-B / anteprima normalizza",
		L"· ★ nota / tempo 50·75·100% / snap A-B / vista normalizar", L"· ★ 평점 / 연습 템포 50·75·100% / A-B 스냅 / 정규화 미리보기", L"· ★评分 / 练习速度50·75·100% / A-B快照 / 标准化预览", L"· ★ تقييم / إيقاع 50·75·100% / لقطة A-B / معاينة تطبيع",
		L"· ★ рейтинг / темп 50·75·100% / снимок A-B / превью нормализации", L"· ★ Bewertung / Tempo 50·75·100% / A-B-Snap / Normalisierungsvorschau", L"· ★ nota / tempo 50·75·100% / snap A-B / prévia normalizar", L"· ★ beoordeling / tempo 50·75·100% / A-B-snap / normalisatie-voorbeeld",
		L"· ★ ocena / tempo 50·75·100% / snap A-B / podgląd normalizacji", L"· ★ puan / tempo 50·75·100% / A-B anlık / normalizasyon önizleme")); yR += lh;
	body(R, yR, LL14(L"・LRC ±10/50/100ms・保存 / 歌詞ウィンドウ(不透明度・表示行数・フォント・右クリック) / A-Bを書き出し範囲に", L"· LRC ±10/50/100ms·save / lyrics window (opacity·visible lines·font·RMB) / export A-B range", L"· LRC ±ms·sauver / fenetre paroles (opacite·lignes·police·clic droit) / exporter plage A-B", L"· LRC ±ms·salva / finestra testi (opacita·righe·font·tasto destro) / esporta intervallo A-B",
		L"· LRC ±ms·guardar / ventana letra (opacidad·lineas·fuente·clic der.) / exportar rango A-B", L"· LRC ±ms·저장 / 가사 창(불투명도·표시행수·글꼴·우클릭) / A-B 내보내기 범위", L"· LRC ±ms·保存 / 歌词窗口(不透明度·显示行数·字号·右键) / 将A-B设为导出范围", L"· LRC ±ms·حفظ / نافذة الكلمات (عتامة·أسطر·خط·يمين) / تصدير نطاق A-B",
		L"· LRC ±мс·сохранить / окно текста (непрозрачность·строки·шрифт·ПКМ) / экспорт A-B", L"· LRC ±ms·speichern / Textfenster (Deckkraft·Zeilen·Schrift·RMB) / A-B exportieren", L"· LRC ±ms·salvar / janela de letra (opacidade·linhas·fonte·botao dir.) / exportar faixa A-B", L"· LRC ±ms·opslaan / songtekstvenster (dekking·regels·lettertype·RMB) / A-B-bereik exporteren",
		L"· LRC ±ms·zapisz / okno tekstu (nieprzezroczystosc·wiersze·czcionka·PPM) / eksport zakresu A-B", L"· LRC ±ms·kaydet / soz penceresi (opaklik·satir·yazi·sag tik) / A-B aralığını dışa aktar")); yR += lh;
	body(R, yR, LL14(L"・BPM計測 …… 開始→再生数秒→再クリックで確定。BPMはダイアログとシーク拍グリッドへ", L"· BPM …… start → play a few sec → click again. Shows dialog + seek beat grid", L"· BPM …… demarrer → lire → recliquer. Dialogue + grille", L"· BPM …… avvia → riproduci → clic. Dialogo + griglia",
		L"· BPM …… iniciar → reproducir → clic. Dialogo + rejilla", L"· BPM 측정 …… 시작→재생 수초→다시 클릭 확정. 대화상자+비트 그리드", L"· BPM测量 …… 开始→播放数秒→再点确定。对话框+拍网格", L"· قياس BPM …… ابدأ→شغّل→انقر. حوار+شبكة",
		L"· BPM …… старт→воспроизведение→клик. Диалог+сетка", L"· BPM …… Start→Wiedergabe→Klick. Dialog+Raster", L"· BPM …… iniciar→reproduzir→clique. Dialogo+grade", L"· BPM …… start→afspelen→klik. Dialoog+raster",
		L"· BPM …… start→odtwarzanie→klik. Okno+siatka", L"· BPM …… baslat→cal→tekrar. Diyalog+izgara")); yR += lh;
	body(R, yR, LL14(L"・MIDI In …… 鍵盤/CCで再生操作。譜面のMIDI録りはピアノロール右クリック", L"· MIDI In …… keys/CC control playback. Score MIDI capture is piano-roll RMB", L"· MIDI In …… touches/CC. Enreg. partition = clic droit piano roll", L"· MIDI In …… tasti/CC. Registrazione partitura = destro piano roll",
		L"· MIDI In …… teclas/CC. Captura de partitura = clic der. piano", L"· MIDI In …… 건반/CC로 재생 조작. 악보 MIDI 녹음은 피아노롤 우클릭", L"· MIDI In …… 琴键/CC 控制播放。谱面 MIDI 录制在钢琴卷右键", L"· MIDI In …… مفاتيح/CC. تسجيل النوتة = يمين لفة البيانو",
		L"· MIDI In …… клавиши/CC. Запись партитуры = ПКМ пианоролла", L"· MIDI In …… Tasten/CC. Partitur-Aufnahme = RMB Klavierrolle", L"· MIDI In …… teclas/CC. Captura de partitura = direito no piano", L"· MIDI In …… toetsen/CC. Partituuropname = RMB pianorol",
		L"· MIDI In …… klawisze/CC. Zapis partytury = PPM rolki", L"· MIDI In …… tuş/CC. Parti kaydı = piyano rulosu sağ tık")); yR += lh + 2;

	y = max(yL, yR) + 2;

	// ---- 図解: シーク階層 ----
	title(L, y, LL14(L"シークバーの層（図）", L"Seek bar layers (diagram)", L"Couches Seek (schéma)", L"Livelli Seek (schema)",
		L"Capas Seek (diagrama)", L"시크 바 계층(그림)", L"进度条分层（图）", L"طبقات Seek (رسم)",
		L"Слои Seek (схема)", L"Seek-Ebenen (Diagramm)", L"Camadas Seek (diagrama)", L"Seek-lagen (diagram)",
		L"Warstwy Seek (schemat)", L"Seek katmanları (şekil)"));
	y += titleLh;
	{
		const int gx = L, gy = y, gw = min(560, rc.Width() - L * 2), gh = lh * 5 + 18;
		dc.FillSolidRect(gx, gy, gw, gh, RGB(248, 249, 252));
		dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
		// 波形帯
		dc.FillSolidRect(gx + 8, gy + 6, gw - 16, lh + 2, RGB(210, 225, 240));
		dc.SetTextColor(RGB(40, 50, 70));
		dc.TextOut(gx + 14, gy + 7, LL14(L"波形オーバービュー（最奥）", L"Wave overview (backmost)", L"Onde (arrière)", L"Onda (fondo)",
			L"Onda (fondo)", L"파형 개요(최후)", L"波形概览（最底）", L"موجة (خلف)", L"Волна (сзади)", L"Welle (hinten)", L"Onda (fundo)", L"Golf (achter)", L"Fala (tył)", L"Dalga (arka)"));
		// 拍グリッド
		dc.FillSolidRect(gx + 8, gy + lh + 10, gw - 16, lh + 2, RGB(230, 235, 250));
		for (int i = 0; i < 8; ++i) {
			const int x = gx + 20 + i * ((gw - 40) / 8);
			dc.FillSolidRect(x, gy + lh + 12, 2, lh - 2, RGB(120, 140, 200));
		}
		dc.TextOut(gx + 14, gy + lh + 11, LL14(L"拍グリッド（BPM）  ←Alt+ドラッグで位相→", L"Beat grid (BPM)  ←Alt+drag phase→", L"Grille BPM  ←Alt+glisser→", L"Griglia BPM  ←Alt+trascina→",
			L"Rejilla BPM  ←Alt+arrastrar→", L"비트 그리드  ←Alt+드래그 위상→", L"拍网格  ←Alt+拖相位→", L"شبكة BPM  ←Alt+سحب→",
			L"Сетка BPM  ←Alt+сдвиг→", L"Beat-Raster  ←Alt+Phase→", L"Grade BPM  ←Alt+arrastar→", L"Beatraster  ←Alt+fase→",
			L"Siatka BPM  ←Alt+faza→", L"Vuruş ızgarası  ←Alt+faz→"));
		// LRC
		dc.FillSolidRect(gx + 8, gy + 2 * lh + 14, gw - 16, lh + 2, RGB(220, 240, 255));
		for (int i = 0; i < 5; ++i) {
			const int x = gx + 40 + i * ((gw - 80) / 5);
			dc.FillSolidRect(x, gy + 2 * lh + 16, 2, lh - 2, RGB(80, 180, 255));
		}
		dc.TextOut(gx + 14, gy + 2 * lh + 15, LL14(L"水色マーカー = LRC 時刻（クリックでシーク）", L"Cyan marks = LRC times (click to seek)", L"Traits cyan = LRC", L"Marche ciano = LRC",
			L"Marcas cian = LRC", L"청록 = LRC 시각(클릭)", L"青色 = LRC 时刻（点击）", L"سماوي = LRC", L"Голубые = LRC", L"Cyan = LRC", L"Ciano = LRC", L"Cyaan = LRC", L"Cyan = LRC", L"Camgöbeği = LRC"));
		// ループ / A-B / 再生
		dc.FillSolidRect(gx + 30, gy + 3 * lh + 18, gw / 3, lh, RGB(255, 180, 200));
		dc.FillSolidRect(gx + 30 + gw / 4, gy + 3 * lh + 18, 6, lh, RGB(80, 120, 255));
		dc.FillSolidRect(gx + 30 + gw / 3, gy + 3 * lh + 18, 6, lh, RGB(80, 120, 255));
		dc.FillSolidRect(gx + gw / 2, gy + 3 * lh + 18, 4, lh, RGB(255, 80, 120));
		dc.SetTextColor(RGB(50, 40, 60));
		dc.TextOut(gx + 14, gy + 4 * lh + 8, LL14(L"ピンク=ループ　青つまみ=A-B　♥=再生位置　三角=キュー", L"Pink=loop  Blue=A-B  ♥=playhead  Tri=cues", L"Rose=boucle  Bleu=A-B  ♥=tête  Tri=cues", L"Rosa=loop  Blu=A-B  ♥=testina  Tri=cue",
			L"Rosa=bucle  Azul=A-B  ♥=cabeza  Tri=cues", L"분홍=루프  파랑=A-B  ♥=재생  삼각=큐", L"粉=循环  蓝=A-B  ♥=播放头  三角=标记", L"وردي=حلقة  أزرق=A-B  ♥=رأس  مثلث=إشارات",
			L"Розовый=цикл  Синий=A-B  ♥=голова  ▲=метки", L"Rosa=Schleife  Blau=A-B  ♥=Kopf  Dreieck=Cues", L"Rosa=loop  Azul=A-B  ♥=cabeça  Tri=cues", L"Roze=lus  Blauw=A-B  ♥=kop  Driehoek=cues",
			L"Różowy=pętla  Niebieski=A-B  ♥=głowica  Trój=cue", L"Pembe=döngü  Mavi=A-B  ♥=kafa  Üçgen=cue"));
		y = gy + gh + 4;
	}

	// ---- 図解: Camelot ----
	title(L, y, LL14(L"キー / Camelot 相性（図）", L"Key / Camelot compatibility (diagram)", L"Clé / Camelot (schéma)", L"Tonalità / Camelot (schema)",
		L"Tonalidad / Camelot (diagrama)", L"키 / Camelot 호환(그림)", L"调性 / Camelot 相容（图）", L"مفتاح / Camelot (رسم)",
		L"Тональность / Camelot (схема)", L"Tonart / Camelot (Diagramm)", L"Tom / Camelot (diagrama)", L"Toonsoort / Camelot (diagram)",
		L"Tonacja / Camelot (schemat)", L"Ton / Camelot (şekil)"));
	y += titleLh;
	{
		const int gx = L, gy = y, gw = min(560, rc.Width() - L * 2), gh = lh * 4 + 14;
		dc.FillSolidRect(gx, gy, gw, gh, RGB(252, 248, 255));
		dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
		// 中心
		dc.FillSolidRect(gx + gw / 2 - 28, gy + lh + 4, 56, lh + 4, RGB(200, 170, 230));
		dc.SetTextColor(RGB(40, 30, 60));
		dc.TextOut(gx + gw / 2 - 18, gy + lh + 6, L"8A");
		// 隣接
		auto box = [&](int x, int by, LPCTSTR t, COLORREF c) {
			dc.FillSolidRect(x, by, 44, lh, c);
			dc.SetTextColor(RGB(30, 30, 40));
			dc.TextOut(x + 10, by + 1, t);
		};
		box(gx + gw / 2 - 28 - 56, gy + lh + 4, L"7A", RGB(180, 210, 255));
		box(gx + gw / 2 - 28 + 62, gy + lh + 4, L"9A", RGB(180, 210, 255));
		box(gx + gw / 2 - 28, gy + 4, L"8B", RGB(255, 210, 170));
		box(gx + gw / 2 - 28 - 56, gy + 4, L"7B", RGB(255, 230, 200));
		box(gx + gw / 2 - 28 + 62, gy + 4, L"9B", RGB(255, 230, 200));
		dc.SetTextColor(RGB(60, 50, 80));
		dc.TextOut(gx + 10, gy + 2 * lh + 10, LL14(
			L"ツール「キー確定」→曲に記憶。相性候補=隣接±1＋相対長/短。未検出曲はスキップ。",
			L"Tools “Capture key” → save per song. Compatible = ±1 neighbors + relative major/minor. Skip unknown.",
			L"« Capturer clé » → par morceau. Compatibles = ±1 + relatif. Inconnus ignorés.",
			L"« Cattura chiave » → per brano. Compatibili = ±1 + relativo. Sconosciuti saltati.",
			L"« Capturar tonalidad » → por pista. Compatibles = ±1 + relativo. Desconocidos omitidos.",
			L"「키 확정」→곡별 저장. 호환=±1+상대. 미검출 건너뜀.",
			L"「确定调性」→按曲保存。相容=±1+关系大小调。未检测跳过。",
			L"«تأكيد المفتاح» → لكل أغنية. متوافق=±1+نسبي. مجهول يُتخطى.",
			L"«Зафиксировать тональность» → на трек. Совместимые = ±1 + относительные. Неизвестные пропускаются.",
			L"„Tonart speichern“ → pro Titel. Passend = ±1 + relativ. Unbekannte übersprungen.",
			L"«Capturar tom» → por faixa. Compatíveis = ±1 + relativo. Desconhecidos ignorados.",
			L"„Toonsoort vastleggen“ → per nummer. Compatibel = ±1 + relatief. Onbekend overgeslagen.",
			L"«Zapisz tonację» → na utwór. Kompatybilne = ±1 + względne. Nieznane pomijane.",
			L"«Ton kaydet» → parça. Uyumlu = ±1 + göreli. Bilinmeyen atlanır."));
		y = gy + gh + 4;
	}

	yL = y; yR = y;
	title(L, yL, LL14(L"セット章 / フィルタ（図）", L"Set chapters / filter (diagram)", L"Chapitres set (schéma)", L"Capitoli set (schema)",
		L"Capítulos set (diagrama)", L"세트 구간/필터(그림)", L"套章节/筛选（图）", L"فصول المجموعة (رسم)",
		L"Главы сета (схема)", L"Set-Kapitel (Diagramm)", L"Capítulos do set (diagrama)", L"Set-hoofdstukken (diagram)",
		L"Rozdziały setu (schemat)", L"Set bölümleri (şekil)")); yL += titleLh;
	{
		const int bx = L, by = yL, bw = min(250, (rc.Width() / 2) - 20);
		dc.FillSolidRect(bx, by, bw, lh * 3 + 8, RGB(250, 250, 245));
		dc.FrameRect(CRect(bx, by, bx + bw, by + lh * 3 + 8), &frameBrush);
		dc.FillSolidRect(bx + 6, by + 4, bw - 12, lh, RGB(200, 230, 200));
		dc.FillSolidRect(bx + 6, by + lh + 6, bw - 12, lh, RGB(255, 210, 160));
		dc.FillSolidRect(bx + 6, by + 2 * lh + 8, bw - 12, lh, RGB(180, 200, 240));
		dc.SetTextColor(RGB(40, 40, 50));
		dc.TextOut(bx + 12, by + 5, L"Warmup");
		dc.TextOut(bx + 12, by + lh + 7, L"Peak");
		dc.TextOut(bx + 12, by + 2 * lh + 9, L"Cooldown");
		yL = by + lh * 3 + 14;
	}
	body(L, yL, LL14(L"・リスト右クリック「セット章」で曲にタグ。色分けの目安に。", L"· List RMB “Set chapter” tags a track for set planning.", L"· Clic droit liste « Chapitre ».", L"· Destro lista « Capitolo ».",
		L"· Clic der. lista « Capítulo ».", L"· 목록 우클릭 「세트 구간」.", L"· 列表右键「套章节」。", L"· يمين القائمة «فصل».",
		L"· ПКМ списка «Глава сета».", L"· Listen-RMB „Set-Kapitel“.", L"· Direito lista «Capítulo».", L"· Lijst-RMB „Set-hoofdstuk“.",
		L"· PPM listy «Rozdział setu».", L"· Liste sağ tık «Set bölümü».")); yL += lh + 2;

	title(R, yR, LL14(L"ツール追加メニュー（図）", L"Tools menu additions (diagram)", L"Extras menu (schéma)", L"Menu extra (schema)",
		L"Menú extra (diagrama)", L"도구 추가 메뉴(그림)", L"工具新增菜单（图）", L"قائمة إضافية (رسم)",
		L"Доп. меню (схема)", L"Zusatzmenü (Diagramm)", L"Menu extra (diagrama)", L"Extra-menu (diagram)",
		L"Menu ekstra (schemat)", L"Ek menü (şekil)")); yR += titleLh;
	body(R, yR, LL14(L"▾ツール下段あたり:", L"Near bottom of ▾ Tools:", L"Bas du menu ▾:", L"Fondo menu ▾:",
		L"Final del menú ▾:", L"▾도구 하단:", L"▾工具靠下:", L"أسفل ▾:", L"Низ ▾:", L"Unten in ▾:", L"Fim do ▾:", L"Onder in ▾:", L"Dół ▾:", L"▾ altı:")); yR += lh;
	body(R, yR, LL14(L"・キー確定／相性候補　・フォーカス　・危険操作確認", L"· Capture key / Compatible　· Focus　· Confirm danger", L"· Clé / Compatibles　· Focus　· Confirmer", L"· Chiave / Compatibili　· Focus　· Conferma",
		L"· Tonalidad / Compatibles　· Foco　· Confirmar", L"· 키 확정/호환　· 포커스　· 위험 확인", L"· 确定调性/相容　· 专注　· 危险确认", L"· مفتاح/متوافق　· تركيز　· تأكيد",
		L"· Тональность/совмест.　· Фокус　· Подтверждение", L"· Tonart/Passend　· Fokus　· Bestätigen", L"· Tom/Compatíveis　· Foco　· Confirmar", L"· Toonsoort/Compatibel　· Focus　· Bevestigen",
		L"· Tonacja/Kompat.　· Focus　· Potwierdź", L"· Ton/Uyumlu　· Odak　· Onay")); yR += lh;
	body(R, yR, LL14(L"・ライブセット録画(画面+録音)　・nowplaying.txt　・MIDI学習", L"· Live-set record (cap+audio)　· nowplaying.txt　· MIDI learn", L"· Enreg. set　· nowplaying　· MIDI learn", L"· Registra set　· nowplaying　· MIDI learn",
		L"· Grabar set　· nowplaying　· MIDI learn", L"· 라이브 세트 녹화　· nowplaying　· MIDI 학습", L"· 现场套录制　· nowplaying　· MIDI学习", L"· تسجيل المجموعة　· nowplaying　· تعلم MIDI",
		L"· Запись сета　· nowplaying　· MIDI learn", L"· Live-Set　· nowplaying　· MIDI lernen", L"· Gravar set　· nowplaying　· MIDI learn", L"· Live-set　· nowplaying　· MIDI leren",
		L"· Nagraj set　· nowplaying　· Nauka MIDI", L"· Canlı set　· nowplaying　· MIDI öğren")); yR += lh;
	body(R, yR, LL14(L"・トランジション・プリセット　・レイアウト1–3　・週次サマリ", L"· Transition presets　· Layout slots 1–3　· Weekly summary", L"· Presets transition　· Layout 1–3　· Résumé", L"· Preset transizione　· Layout 1–3　· Riepilogo",
		L"· Presets transición　· Layout 1–3　· Resumen", L"· 전환 프리셋　· 레이아웃1–3　· 주간 요약", L"· 过渡预设　· 布局1–3　· 周汇总", L"· إعدادات انتقال　· تخطيط 1–3　· ملخص",
		L"· Пресеты перехода　· Макет 1–3　· Сводка", L"· Übergangs-Presets　· Layout 1–3　· Wochenübersicht", L"· Presets transição　· Layout 1–3　· Resumo", L"· Overgangs-presets　· Layout 1–3　· Weekoverzicht",
		L"· Preset przejść　· Układ 1–3　· Podsumowanie", L"· Geçiş önayarları　· Düzen 1–3　· Haftalık özet")); yR += lh;
	body(R, yR, LL14(L"・AACプロファイル　・Mirror CUE　・フレーズ拍スナップ　・練習ログ", L"· AAC profile　· Mirror CUE　· Phrase beat-snap　· Practice log", L"· Profil AAC　· CUE miroir　· Accrochage　· Journal", L"· Profilo AAC　· CUE mirror　· Snap　· Log",
		L"· Perfil AAC　· CUE espejo　· Snap　· Registro", L"· AAC 프로필　· 미러 CUE　· 비트 스냅　· 연습 로그", L"· AAC配置　· 镜像CUE　· 拍对齐　· 练习日志", L"· ملف AAC　· CUE مرآة　· محاذاة　· سجل",
		L"· Профиль AAC　· CUE зеркала　· Привязка　· Журнал", L"· AAC-Profil　· Mirror-CUE　· Beat-Snap　· Übungsprotokoll", L"· Perfil AAC　· CUE espelho　· Snap　· Log", L"· AAC-profiel　· Mirror-CUE　· Snap　· Oefenlog",
		L"· Profil AAC　· CUE lustra　· Snap　· Dziennik", L"· AAC profil　· Ayna CUE　· Snap　· Günlük")); yR += lh + 2;

	y = max(yL, yR) + 2;
	title(L, y, LL14(L"Remote / OBS（図）", L"Remote / OBS (diagram)", L"Remote / OBS (schéma)", L"Remote / OBS (schema)",
		L"Remote / OBS (diagrama)", L"Remote / OBS(그림)", L"Remote / OBS（图）", L"Remote / OBS (رسم)",
		L"Remote / OBS (схема)", L"Remote / OBS (Diagramm)", L"Remote / OBS (diagrama)", L"Remote / OBS (diagram)",
		L"Remote / OBS (schemat)", L"Remote / OBS (şekil)"));
	y += titleLh;
	{
		const int gx = L, gy = y, gw = min(560, rc.Width() - L * 2), gh = lh * 3 + 12;
		dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 250, 255));
		dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
		dc.FillSolidRect(gx + 8, gy + 6, 90, lh * 2, RGB(160, 200, 240));
		dc.FillSolidRect(gx + 110, gy + 6, 100, lh * 2, RGB(180, 220, 180));
		dc.FillSolidRect(gx + 222, gy + 6, 110, lh * 2, RGB(240, 210, 160));
		dc.FillSolidRect(gx + 344, gy + 6, 120, lh * 2, RGB(220, 190, 245));
		dc.SetTextColor(RGB(30, 40, 50));
		dc.TextOut(gx + 16, gy + 10, L"/");
		dc.TextOut(gx + 16, gy + lh + 4, L"remote UI");
		dc.TextOut(gx + 118, gy + 10, L"/overlay");
		dc.TextOut(gx + 118, gy + lh + 4, L"OBS HTML");
		dc.TextOut(gx + 230, gy + 10, L"/api/queue-add");
		dc.TextOut(gx + 230, gy + lh + 4, L"?i=row");
		dc.TextOut(gx + 352, gy + 10, L"nowplaying.txt");
		dc.TextOut(gx + 352, gy + lh + 4, L"exe folder");
		dc.TextOut(gx + 10, gy + 2 * lh + 6, LL14(
			L"同一LANのみ。認証なし（既存Remoteと同水準）。オーバーレイは透過タイトル用。",
			L"Same LAN only. No auth (same as existing Remote). Overlay is transparent title for OBS.",
			L"LAN uniquement. Sans auth. Overlay = titre transparent OBS.",
			L"Solo LAN. Senza auth. Overlay = titolo trasparente OBS.",
			L"Solo LAN. Sin auth. Overlay = título transparente OBS.",
			L"동일 LAN만. 인증 없음. 오버레이=OBS 투명 타이틀.",
			L"仅同一局域网。无认证。overlay=OBS透明标题。",
			L"LAN فقط. بلا مصادقة. Overlay لعنوان شفاف.",
			L"Только LAN. Без auth. Overlay — прозрачный заголовок OBS.",
			L"Nur LAN. Keine Auth. Overlay = transparenter OBS-Titel.",
			L"Só LAN. Sem auth. Overlay = título transparente OBS.",
			L"Alleen LAN. Geen auth. Overlay = transparante OBS-titel.",
			L"Tylko LAN. Bez auth. Overlay = przezroczysty tytuł OBS.",
			L"Yalnızca LAN. Kimlik yok. Overlay = OBS şeffaf başlık."));
		y = gy + gh + 4;
	}

	body(L, y, LL14(L"・リスト削除の「削除を元に戻す」 …… 直近1回分のみ（フルUndoではない）", L"· List “Undo delete” …… last delete only (not full undo stack)", L"· « Annuler suppression » …… dernier seul", L"· « Annulla elimina » …… solo ultimo",
		L"· « Deshacer eliminar » …… solo el último", L"· 「삭제 실행 취소」 …… 직전 1회만", L"·「撤销删除」……仅最近一次", L"· «تراجع عن الحذف» …… الأخير فقط",
		L"· «Отменить удаление» …… только последний", L"· „Löschen rückgängig“ …… nur letzter", L"· «Desfazer exclusão» …… só o último", L"· „Verwijderen ongedaan“ …… alleen laatste",
		L"· «Cofnij usuwanie» …… tylko ostatnie", L"· «Silmeyi geri al» …… yalnızca son")); y += lh + 2;

	muted(L, y, LL14(
		L"各サブ窓の ? も同様に操作ガイドを開きます。キャプションの「メインに追随」はサブ窓の位置追従です。",
		L"Each sub-window ? opens its own guide. Caption “Follow main” keeps sub-windows attached.",
		L"Chaque ? de sous-fenêtre ouvre son guide. « Suivre principal » attache les fenêtres.",
		L"Ogni ? delle sottofinestre apre la guida. « Segui principale » le tiene agganciate.",
		L"Cada ? de subventana abre su guía. « Seguir principal » las mantiene unidas.",
		L"각 하위 창 ? 도 가이드를 엽니다. 「메인 추종」은 하위 창 위치 추종입니다.",
		L"各子窗口的 ? 也会打开指南。标题栏「跟随主窗口」用于子窗位置跟随。",
		L"كل ? في النوافذ الفرعية يفتح دليله. «اتبع الرئيسي» يلصق النوافذ.",
		L"Каждый ? дочернего окна открывает своё руководство. «Следовать за главным» держит окна.",
		L"Jedes Unterfenster-? oeffnet seinen Guide. «Hauptfenster folgen» haelt Fenster angeheftet.",
		L"Cada ? de subjanela abre seu guia. «Seguir principal» mantém janelas anexadas.",
		L"Elke subvenster-? opent zijn gids. «Volg hoofd» houdt vensters vast.",
		L"Każde ? okna podrzędnego otwiera przewodnik. «Podążaj za głównym» przykleja okna.",
		L"Her alt pencere ? kendi kılavuzunu açar. «Ana pencereyi takip» konumları yapıştırır."));

	dc.SelectObject(oldFont);
	CCC_GdiHelpEndPaint(hp);
}

} // namespace

void CMediaPlayerDlg::OnCheatSheetBtn()
{
	ShowCheatSheet();
}

void CMediaPlayerDlg::ShowCheatSheet()
{
	// アクリルキャプション＋Soft3D 実演つきの新ガイド(CMpHelpDlg)を開く。
	CMpHelpDlg* dlg = CMpHelpDlg::Instance();
	if (dlg && ::IsWindow(dlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(dlg, this);
		return;
	}
	// オーナー付きモードレス。ヘルプはオーナー上、他UI前面時は下へ（TOPMOSTしない）
	dlg = new CMpHelpDlg(this);
	if (!dlg->Create(IDD_MP_CHEATSHEET, this)) {
		delete dlg;
		return;
	}
	CCC_PresentOwnedHelp(dlg, this);
}

void CMediaPlayerDlg::OpenCommandPalette()
{
	// オーナー付きモードレス。シングルトン管理と自己破棄はパレット側で行う。
	CMpCommandPaletteDlg::OpenPalette(this);
}

// pc[] を src→dst へ移動(リスト内ドラッグ移動)。再生インデックスも追従。
static void MP_MovePlaylistItem(int src, int dst)
{
	if (!pl || !pl->pc) return;
	int n = pl->playcnt;
	if (src < 0 || src >= n || dst < 0 || dst >= n || src == dst) return;
	playlistdata0 tmp = pl->pc[src];
	if (src < dst) for (int i = src; i < dst; i++) pl->pc[i] = pl->pc[i + 1];
	else           for (int i = src; i > dst; i--) pl->pc[i] = pl->pc[i - 1];
	pl->pc[dst] = tmp;
	auto adj = [&](int idx)->int {
		if (idx == src) return dst;
		if (src < dst && idx > src && idx <= dst) return idx - 1;
		if (src > dst && idx >= dst && idx < src) return idx + 1;
		return idx;
	};
	plcnt = adj(plcnt);
	pl->pnt = adj(pl->pnt);
	pl->pnt1 = adj(pl->pnt1);
	if (::IsWindow(pl->m_lc.GetSafeHwnd())) pl->m_lc.RedrawWindow();
	pl->Save();
}

void CMediaPlayerDlg::OnBeginDragList(NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;
	LPNMLISTVIEW nm = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	m_dragSrc = nm->iItem;
	if (m_dragSrc < 0) return;
	m_dragging = 1;
	// ドラッグ画像(プレイリストと同様の見た目)
	POINT ptHot = { 0,0 };
	m_hDragImage = ListView_CreateDragImage(m_list.m_hWnd, m_dragSrc, &ptHot);
	if (m_hDragImage) {
		ImageList_BeginDrag(m_hDragImage, 0, 0, 0);
		POINT pc = nm->ptAction;          // リストクライアント座標
		m_list.ClientToScreen(&pc);
		ScreenToClient(&pc);
		ImageList_DragEnter(GetSafeHwnd(), pc.x, pc.y);
	}
	SetCapture();
}

// ミニジャケット(幅拡張時に左へ分離表示する正方形ジャケ)クリックで、
// ジャケボタンと同じくジャケット拡大表示を開く。座標は DoLayout が m_jacketRect を
// 毎リサイズ更新するので、リサイズで位置が変わっても追従する。
// ジャケ分離していない狭い窓ではバナー内蔵ジャケなので、バナー領域クリックでも開く。
void CMediaPlayerDlg::OnLButtonDown(UINT nFlags, CPoint point)
{
	// Soft3D 負荷ヒント表示中にバナーをクリック → 2D に戻す（案内どおり）
	if (IsBannerSoft3D()
		&& m_soft3dPerfHintUntil != 0
		&& GetTickCount() < m_soft3dPerfHintUntil
		&& (savedata.soft3dPerfHintDismiss & 1) == 0
		&& !m_bannerRect.IsRectEmpty()
		&& m_bannerRect.PtInRect(point)
		&& point.y < m_bannerRect.top + 44) {
		savedata.mpBannerviewmode = 0;
		savedata.soft3dPerfHintDismiss |= 1;
		m_soft3dPerfHintUntil = 0;
		Invalidate(FALSE);
		return;
	}
	if (IsBannerSoft3D()) {
		const bool hit =
			(!m_bannerRect.IsRectEmpty() && m_bannerRect.PtInRect(point)) ||
			(!m_jacketRect.IsRectEmpty() && m_jacketRect.PtInRect(point)) ||
			(!m_infoPanelRect.IsRectEmpty() && m_infoPanelRect.PtInRect(point));
		if (hit) {
			m_bannerRotDragging = true;
			m_bannerRotOrigin = point;
			m_bannerRotYaw0 = m_bannerCam3d.yawDeg;
			m_bannerRotPitch0 = m_bannerCam3d.pitchDeg;
			SetCapture();
			return;
		}
	}
	BOOL hasJacket = (og && og->jx > 0 && !og->img.IsNull());
	if (g_mpSideJacket && !m_jacketRect.IsRectEmpty() && m_jacketRect.PtInRect(point)) {
		if (hasJacket) OnJacket();
		return;
	}
	if (!g_mpSideJacket && !m_bannerRect.IsRectEmpty() && m_bannerRect.PtInRect(point)) {
		if (hasJacket) OnJacket();
		return;
	}
	CCustomBlurDialogExBase::OnLButtonDown(nFlags, point);
}

void CMediaPlayerDlg::OnMouseMove(UINT nFlags, CPoint point)
{
	if (m_bannerRotDragging) {
		if (!(nFlags & MK_LBUTTON)) {
			m_bannerRotDragging = false;
			if (::GetCapture() == m_hWnd) ::ReleaseCapture();
			PersistBannerSoft3DCam();
			return;
		}
		GdiSoft3D::OrbitDrag(m_bannerCam3d, m_bannerRotYaw0, m_bannerRotPitch0, m_bannerRotOrigin, point);
		PersistBannerSoft3DCam();
		Invalidate(FALSE);
		return;
	}
	// バナー上ホバーで og と同じジャケットアニメを発火(ジャケ分離中は無効)
	g_mpBannerHover = (!g_mpSideJacket && m_bannerRect.PtInRect(point)) ? 1 : 0;
	// ジャケ拡大できる領域(ミニジャケ or バナー内蔵ジャケ)では手のひらカーソル
	if (!m_dragging && !m_libDrag) {
		BOOL hasJacket = (og && og->jx > 0 && !og->img.IsNull());
		const bool overJacket = hasJacket && (
			(g_mpSideJacket && !m_jacketRect.IsRectEmpty() && m_jacketRect.PtInRect(point)) ||
			(!g_mpSideJacket && !m_bannerRect.IsRectEmpty() && m_bannerRect.PtInRect(point)));
		if (overJacket)
			::SetCursor(::LoadCursor(NULL, IDC_HAND));
	}
	if (m_dragging) {
		::SetCursor(::LoadCursor(NULL, IDC_HAND));
		if (m_hDragImage) {
			ImageList_DragMove(point.x, point.y);
		}
	}
	if (m_libDrag) {
		CPoint sp = point; ClientToScreen(&sp);
		if (m_hLibDragImage)
			ImageList_DragMove(sp.x, sp.y);
		const BOOL overPl = LibDropHitTestPlaylist(point);
		::SetCursor(::LoadCursor(NULL, overPl ? IDC_HAND : IDC_NO));
	}
	CCustomBlurDialogExBase::OnMouseMove(nFlags, point);
}

void CMediaPlayerDlg::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_bannerRotDragging) {
		m_bannerRotDragging = false;
		if (::GetCapture() == m_hWnd) ::ReleaseCapture();
		PersistBannerSoft3DCam();
		return;
	}
	if (m_libDrag) {
		m_libDrag = 0;
		ReleaseCapture();
		if (m_hLibDragImage) {
			ImageList_DragLeave(::GetDesktopWindow());
			ImageList_EndDrag();
			ImageList_Destroy(m_hLibDragImage);
			m_hLibDragImage = NULL;
		}
		const CString path = m_libDragFolder;
		m_libDragFolder.Empty();
		if (!path.IsEmpty() && LibDropHitTestPlaylist(point))
			LibAddPath(path, TRUE);
		CDialog::OnLButtonUp(nFlags, point);
		return;
	}
	if (m_dragging) {
		m_dragging = 0;
		ReleaseCapture();
		if (m_hDragImage) {
			ImageList_DragLeave(GetSafeHwnd());
			ImageList_EndDrag();
			ImageList_Destroy(m_hDragImage);
			m_hDragImage = NULL;
		}
		// ドロップ先の行を mp リスト座標で判定
		CPoint sp = point; ClientToScreen(&sp);
		CPoint lp = sp; m_list.ScreenToClient(&lp);
		UINT fl = 0;
		int dst = m_list.HitTest(lp, &fl);
		if (dst < 0) {
			CRect rc; m_list.GetClientRect(&rc);
			if (lp.y >= rc.bottom) dst = m_list.GetItemCount() - 1; // 末尾へ
		}
		if (pl && m_dragSrc >= 0 && dst >= 0 && dst != m_dragSrc) {
			int srcPc = MpDispToPc(this, m_dragSrc);
			int dstPc = MpDispToPc(this, dst);
			if (srcPc >= 0 && dstPc >= 0 && srcPc < pl->playcnt && dstPc < pl->playcnt && srcPc != dstPc) {
				MP_MovePlaylistItem(srcPc, dstPc);
				RefreshList(TRUE);
				if (dst < m_list.GetItemCount()) {
					m_list.SetItemState(dst, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
					m_list.EnsureVisible(dst, FALSE);
				}
			}
		}
		m_dragSrc = -1;
	}
	CCustomBlurDialogExBase::OnLButtonUp(nFlags, point);
}

BOOL CMediaPlayerDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (IsBannerSoft3D()) {
		CPoint client = pt;
		ScreenToClient(&client);
		const bool hit =
			(!m_bannerRect.IsRectEmpty() && m_bannerRect.PtInRect(client)) ||
			(!m_jacketRect.IsRectEmpty() && m_jacketRect.PtInRect(client)) ||
			(!m_infoPanelRect.IsRectEmpty() && m_infoPanelRect.PtInRect(client));
		if (hit) {
			GdiSoft3D::WheelZoom(m_bannerCam3d, zDelta);
			PersistBannerSoft3DCam();
			Invalidate(FALSE);
			return TRUE;
		}
	}
	return CCustomBlurDialogExBase::OnMouseWheel(nFlags, zDelta, pt);
}

/////////////////////////////////////////////////////////////////////////////
// モード切替
/////////////////////////////////////////////////////////////////////////////
// ファルコム特化型 → メディアプレイヤーモードへ切替。
// mp を新規生成し og/pl を非表示にする。og は再生エンジンとして裏で動き続ける。
// DWM アクリル問題対策として mp をトップレベル化(オーナー解除)してから RefreshAeroMode する。
void EnterMediaPlayerMode(BOOL bConvertCoords)
{
	if (!og || !::IsWindow(og->GetSafeHwnd())) return;
	if (mp && ::IsWindow(mp->GetSafeHwnd())) {  // 既に MP モード
		mp->ShowWindow(SW_SHOW);
		return;
	}

	// 中途切替時のみ旧メイン矩形を取る。起動時は絶対に変換しない
	// (起動のたびに (mp-og) を足すと斜め上へドリフトする)。
	CRect oldMainRc;
	if (bConvertCoords)
		og->GetWindowRect(&oldMainRc);

	// プレイリストを必ず生成(裏で生かす)。再生はプレイリスト方式にする。
	if (!pl) {
		killw1 = 0;
		pl = new CPlayList;
		if (!pl->Create(og)) {
			delete pl;
			pl = NULL;
		}
	}
	if (!pl || !::IsWindow(pl->GetSafeHwnd()))
		return;
	plw = 1;
	savedata.playerMode = 1;

	// メディアプレイヤー画面を生成・表示。
	// オーナーは og のまま(EQ/簡易ピアノロールと同じアクリルグループ)にして、
	// 非アクティブ時もアクリルが維持されるようにする。トップレベル化(オーナー解除)は
	// 孤立窓となり非アクティブでアクリルが落ちるため行わない。タスクバー単独表示は
	// PreCreateWindow の WS_EX_APPWINDOW で確保する。
	// TheadLoop が mp メンバを触るため、Create 完了後に初めてグローバルへ載せる。
	CMediaPlayerDlg* creating = new CMediaPlayerDlg;
	if (!creating->Create(og) || !::IsWindow(creating->GetSafeHwnd())) {
		delete creating;
		return;
	}
	mp = creating;
#if CCUSTOM_AERO_SUPPORT
	if (savedata.aero == 1)
		mp->RefreshAeroMode();
#endif

	// 重複防止: プレイリスト/メイン画面/aeroオーバーレイの単独ウィンドウを隠す
	extern CImageBase* maini;
	extern CImageBase* playbase;
	if (pl && ::IsWindow(pl->GetSafeHwnd()))
		::ShowWindow(pl->m_hWnd, SW_HIDE);
	if (maini && ::IsWindow(maini->GetSafeHwnd()))
		::ShowWindow(maini->m_hWnd, SW_HIDE);
	if (playbase && ::IsWindow(playbase->GetSafeHwnd()))
		::ShowWindow(playbase->m_hWnd, SW_HIDE);
	// イコライザー/簡易ピアノロールはオプション窓なので閉じない(そのまま維持)
	::ShowWindow(og->m_hWnd, SW_HIDE);

	if (mp && ::IsWindow(mp->GetSafeHwnd())) {
		::SetForegroundWindow(mp->m_hWnd);
		mp->SetFocus();
		SetupTaskbarThumbButtons(mp->m_hWnd, TRUE);
		RefreshTaskbarJumpList(TRUE);
#if CCUSTOM_AERO_SUPPORT
		if (savedata.aero == 1)
			mp->RefreshAeroMode();   // 前面化後に再適用
#endif
		if (bConvertCoords) {
			// 中途切替: og基準の相対位置を保ったまま mp 基準へ座標変換
			CCC_MainLockRefreshOffsetsFor(mp, &oldMainRc);
		}
		else {
			// 起動時: 現位置のまま mp 基準でオフセットだけ取り直す(窓は動かさない)
			CCC_MainLockRefreshOffsetsFor(mp, NULL);
		}
		// PlaceChild は WM_MOVING を飛ばないため、aero==2 グラス背面を親に合わせる
		if (pl && playbase && ::IsWindow(pl->GetSafeHwnd()) && ::IsWindow(playbase->GetSafeHwnd())) {
			CRect pr;
			pl->GetWindowRect(&pr);
			playbase->MoveWindow(&pr);
		}
	}
}

// メディアプレイヤー → ファルコム特化型モードへ切替。
// mp を破棄して og を再表示する。EnterFalcomMode 自体は og->m_hWnd の OnReceive
// (WM_MP_ENTER_FALCOM)から遅延呼び出しされるため、mp のハンドラ内で mp を破棄
// してしまう問題を避けられる。
void EnterFalcomMode()
{
	savedata.playerMode = 0;

	// ファルコム特化型では内蔵(ミニ)ジャケを必ず表示するため抑止フラグを解除。
	g_mpSideJacket = 0;

	// 切替前メイン(mp)矩形。破棄前に取得する。
	CRect oldMainRc;
	BOOL haveOldMain = FALSE;
	CMediaPlayerDlg* dying = mp;
	mp = NULL;
	if (dying) {
		if (::IsWindow(dying->GetSafeHwnd())) {
			dying->GetWindowRect(&oldMainRc);
			haveOldMain = TRUE;
			dying->SavePos();
			dying->DestroyWindow();
		}
		delete dying;
	}

	// ファルコム特化型: タスクバーを og 用に戻す
	if (og && ::IsWindow(og->m_hWnd)) {
		SetupTaskbarThumbButtons(og->m_hWnd, FALSE);
		RefreshTaskbarJumpList(FALSE);
	}

	// メイン画面を表示し、aero/全コントロールを確実に再反映(▲▼の開閉状態は保持=Resizeは呼ばない)
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		::ShowWindow(og->m_hWnd, SW_SHOW);
		::SetForegroundWindow(og->m_hWnd);
		CCC_GroupBoxesBack(og->m_hWnd);   // 枠を最背面へ(チェックボックスを覆わない)
#if CCUSTOM_AERO_SUPPORT
		og->RefreshAeroMode();                   // アクリル/非アクリルを再適用
#endif
		CCC_RefreshKids(og->m_hWnd);   // 再表示時の子コントロール再描画
		og->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
		og->PostRefreshAllAeroWindows();         // EQ/ピアノ/プレイリスト等も再反映
		// mp基準の相対位置を保ったまま og 基準へ座標変換
		CCC_MainLockRefreshOffsetsFor(og, haveOldMain ? &oldMainRc : NULL);
	}

	// プレイリストは savedata.pl に従って表示/非表示
	if (pl && ::IsWindow(pl->GetSafeHwnd())) {
		if (savedata.pl) {
			::ShowWindow(pl->m_hWnd, SW_SHOW);
			pl->EnsureOnScreen();
			// EnsureOnScreen が退避位置から戻した場合、追随オフセットを実位置で取り直す
			if (savedata.playlistMainLock) {
				CRect pr;
				pl->GetWindowRect(&pr);
				CCC_MainLockOnChildMoving(pl, &pr);
				savedata.p.left = pr.left;
				savedata.p.top = pr.top;
				savedata.p.right = pr.right;
				savedata.p.bottom = pr.bottom;
			}
			plw = 1;
		}
		else {
			::ShowWindow(pl->m_hWnd, SW_HIDE);
			plw = 0;
		}
	}

	// aero オーバーレイを復帰(aero==2 のときのみ存在)
	extern CImageBase* maini;
	extern CImageBase* playbase;
	if (maini && ::IsWindow(maini->GetSafeHwnd()))
		::ShowWindow(maini->m_hWnd, SW_SHOW);
	if (playbase && ::IsWindow(playbase->GetSafeHwnd()) && savedata.pl) {
		// PlaceChild/EnsureOnScreen 後の pl にグラスを合わせる(WM_MOVING 非発火対策)
		if (pl && ::IsWindow(pl->GetSafeHwnd())) {
			CRect pr;
			pl->GetWindowRect(&pr);
			playbase->MoveWindow(&pr);
		}
		::ShowWindow(playbase->m_hWnd, SW_SHOW);
	}
}
