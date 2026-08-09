#pragma once
// Wave B/C の横断機能（ステータス、確認、フォーカス、nowplaying、章、ライブ録画、等）

class CCustomPopupMenu;
class CWnd;

void MpFeatInitDefaults(); // savedata 末尾フィールドの起動時補正
void MpFeatWriteNowPlaying(); // nowplaying.txt
CString MpFeatStatusLine(); // 1行ステータス
BOOL MpFeatConfirmDanger(HWND owner, LPCTSTR whatJa);
void MpFeatApplyFocusMode(CWnd* mpDlg, BOOL on);
void MpFeatLiveSetRecordStart(CWnd* parent); // キャプチャ+デバイス録音
void MpFeatBumpBotUse(int idx); // 0..7
void MpFeatApplyBotUseOrder(); // flags 並び替えヒント
void MpFeatAppendKeyMenu(CCustomPopupMenu& menu); // キー確定・相性
BOOL MpFeatHandleKeyMenuCmd(UINT cmd);
void MpFeatSetChapter(int row, int chapter); // 0..3
int  MpFeatGetChapter(int row);
void MpFeatAppendChapterMenu(CCustomPopupMenu& menu, int row);
BOOL MpFeatHandleChapterMenuCmd(UINT cmd, int row);
void MpFeatOnSongStartedHooks(); // nowplaying + key capture attempt
void MpFeatEnsureRemoteOverlayHtml(CString& htmlOut); // /overlay 用断片
int  MpFeatAacBytesPerSec(); // profile → bytes/sec
float MpFeatMirrorGainLin();
float MpFeatRemoteGainLin();

enum {
	ID_MP_KEY_CAPTURE = 33250,
	ID_MP_KEY_COMPAT_BASE = 33251, // +0..31
	ID_MP_KEY_COMPAT_LAST = 33282,
	ID_MP_CH_NONE = 33290,
	ID_MP_CH_WARM = 33291,
	ID_MP_CH_PEAK = 33292,
	ID_MP_CH_COOL = 33293,
	ID_MP_FOCUS_MODE = 33294,
	ID_MP_CONFIRM_DANGER = 33295,
	ID_MP_LIVE_SET_REC = 33296,
	ID_MP_NOWPLAYING_FILE = 33297,
	ID_MP_TRANS_PRE_0 = 33300,
	ID_MP_TRANS_PRE_1 = 33301,
	ID_MP_TRANS_PRE_2 = 33302,
	ID_MP_MIDI_LEARN = 33303,
	ID_MP_MIRROR_CUE = 33304,
	ID_MP_PHRASE_SNAP = 33305,
	ID_MP_LAYOUT_SAVE0 = 33310,
	ID_MP_LAYOUT_LOAD0 = 33313,
	ID_MP_WEEKLY_SUMMARY = 33320,
	ID_MP_PRACTICE_LOG = 33321,
	ID_MP_PRACTICE_PACK = 33322,
	ID_MP_AAC_PROF0 = 33323,
	ID_MP_AAC_PROF1 = 33324,
	ID_MP_AAC_PROF2 = 33325,
};
