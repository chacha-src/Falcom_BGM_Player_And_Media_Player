#pragma once
// ============================================================================
// SongParams : 曲ごと(プレイリスト名 + フルパス)にオーディオ/DSP パラメータを
//              savedata とは別の struct・別ファイル(oggYSEDbgmu_AudioData.dat)へ
//              保持し、再生/ WAV 出力時に復元する。
//
//   キー   : プレイリスト名(リスト削除でファイル番号が振り直されても安定)
//            + NormalizePlaylistPath(フルパス)
//   保存   : 再生中に各項目が変わったらその時点で反映(デバウンス書き込み)
//   復元   : 曲が始まったら適用(再生=メインスレッドへ post、WAV出力=直接)
//   有効化 : savedata.saveSongParams (チェックボックス)が 1 のときだけ動作
// ============================================================================

// 再生スレッド(HandleNotifications)からメインスレッドへ復元を依頼するメッセージ
// 注意: COggDlg では WM_APP+70〜74/+99〜101 が使用済み。+75 は未使用。
#ifndef WM_APP_SONGPARAM_RESTORE
#define WM_APP_SONGPARAM_RESTORE (WM_APP + 75)
#endif

// 1 曲分のパラメータ(固定長レコード。ファイルへそのまま書き出す)
struct SongParam {
	TCHAR listName[256];   // プレイリスト名(キー)
	TCHAR path[1024];      // 正規化済みフルパス(キー)
	int dsvol;             // DirectSound 音量スライダー値 -498..1 (1=100%)
	int kakuVol;           // 拡張音量 100..900 (100=100%)
	int pitchPos;          // ピッチ スライダー位置 0..400 (200=100%)
	int tempoPos;          // テンポ スライダー位置 0..400 (200=100%)
	int eq[20];            // EQ 15バンド + マスター/明瞭/バランス/密度/立体 (各 0..200, 100=中立)
	int eqsoundenv;        // 環境プリセット index (0=なし)
	int eqsoundeq;         // EQ プリセット index (0=デフォルト)
	int eqsoundeffect;     // 環境のかかり具合 0..100
	int eq_reverb;         // リバーブ 0..200 (0=off)
	int eq_chorus;         // コーラス 0..200 (0=off)
	int eq_delay;          // ディレイ 0..200 (0=off)
	int analyzerspecstyle; // アナライザー周波数表示モード 0..6
};

// 起動時に 1 度だけ読み込む
void SongParams_LoadFile();
// メモリ内テーブルをファイルへ書き出す
void SongParams_SaveFile();

// HandleNotifications / HandleNotifications_export の先頭で呼ぶ共通処理。
// exporting=true のとき WAV 出力(メインスレッド)。false のとき再生(別スレッド)。
void SongParams_Sync(bool exporting);

// 1 エントリをメイン画面(スライダー/コンボ/savedata/グローバル)へ適用する。
// 必ずメインスレッドから呼ぶこと。
void SongParams_ApplyEntryToMain(const SongParam& e);

// すべてのエントリを破棄し、ファイルも削除する(リセット用)。
void SongParams_ResetAll();

// リスト名の変更・削除に追従してキーを移行/削除する。
void SongParams_RenameList(LPCTSTR oldName, LPCTSTR newName);
void SongParams_DeleteList(LPCTSTR name);

// (listName, path) のエントリを探して out へコピー。見つかれば true。
bool SongParams_FindCopy(LPCTSTR listName, LPCTSTR path, SongParam& out);

// 現在表示中プレイリストの名前(空なら "#index" の安定名)。
CString SongParams_CurrentListName();

// ツールチップ用: そのエントリのうちデフォルトと異なる項目だけを整形した文字列。
// エントリが無い/変更なしのときは空文字列。
CString SongParams_BuildTipExtra(LPCTSTR listName, LPCTSTR path);
