#pragma once
// ============================================================================
// SongParams : 曲ごと(プレイリスト名 + パス + mode + ret2)にオーディオ/DSP パラメータを
//              savedata とは別の struct・別ファイル(oggYSEDbgmu_AudioData.dat)へ
//              保持し、再生/ WAV 出力時に復元する。
//
//   キー   : プレイリスト名 + パス(fol) + mode(sub) + ret2
//            ※ファルコムゲームモードは同名 basename を ret2 で区別するため mode/ret2 必須
//            ※mode 自体の「再生形式切替」はプレイリスト行(pc[].sub)が既に持つ。
//              ここは DSP パラメータの衝突回避用キーであり、mode を書き戻すものではない。
//   保存   : 再生中に各項目が変わったらその時点で反映(デバウンス書き込み)
//            エントリが無い曲を再生開始したときも、その時点の設定を自動で新規保存する
//            ※プロンプト実行中(MpPromptIsActive)は保存しない(時系列改変の汚染防止)
//   復元   : エントリがある曲は開始時に DSP を適用(再生=メインスレッド、WAV出力=直接)
//   有効化 : savedata.saveSongParams (チェックボックス)が 1 のときだけ動作
//   移行   : savedata.audioDataVersion (末尾追記)
//            0→1 で旧キー(pathのみ)を playlistu*.dat 照合により mode+ret2 付きへ自動コンバート
// ============================================================================

// 再生スレッド(HandleNotifications)からメインスレッドへ復元を依頼するメッセージ
// 注意: COggDlg では WM_APP+70〜74/+99〜101 が使用済み。+75 は未使用。
#ifndef WM_APP_SONGPARAM_RESTORE
#define WM_APP_SONGPARAM_RESTORE (WM_APP + 75)
#endif

// 1 曲分のパラメータ(固定長レコード。ファイルへそのまま書き出す)
// ※末尾にフィールド追加。旧 ver1 読込時は mode/ret2 = 0 扱い。
struct SongParam {
	TCHAR listName[256];   // プレイリスト名(キー)
	TCHAR path[1024];      // プレイリスト fol(キー)。ゲームモードは basename のことも多い
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
	int mode;              // キー: pc[].sub / modesub (ゲームモード含む再生形式)
	int ret2;              // キー: pc[].ret2 (同一ファイル内の曲番号など)
};

// 起動時に 1 度だけ読み込む
void SongParams_LoadFile();
// メモリ内テーブルをファイルへ書き出す
void SongParams_SaveFile();

// 旧キー(pathのみ) → mode+ret2 付きキーへ自動コンバート。
// 全プレイリスト .dat を走査して突き合わせる。savedata.audioDataVersion で一度きり。
// プレイリスト Load 後に呼ぶこと。
void SongParams_ConvertKeysIfNeeded();

// HandleNotifications / HandleNotifications_export の先頭で呼ぶ共通処理。
// exporting=true のとき WAV 出力(メインスレッド)。false のとき再生(別スレッド可)。
void SongParams_Sync(bool exporting);

// play() 完了直後(plcnt/SIcon 確定後)にメインスレッドから呼ぶ。
// 曲ごとパラメータの復元をここで確実に行う(ポーリング任せにしない)。
void SongParams_OnSongStarted();

// 停止時: 未書き込みを flush し、保存ポーリングを止める。
void SongParams_OnSongStopped();

// PostMessage 復元完了時に baseline を確定する(メインスレッド)。
void SongParams_NoteRestored(const SongParam& e);

// 1 エントリをメイン画面(スライダー/コンボ/savedata/グローバル)へ適用する。
// 必ずメインスレッドから呼ぶこと。DSP のみ。mode は触らない(プレイリスト側が保持)。
void SongParams_ApplyEntryToMain(const SongParam& e);

// すべてのエントリを破棄し、ファイルも削除する(リセット用)。
void SongParams_ResetAll();

// リスト名の変更・削除に追従してキーを移行/削除する。
void SongParams_RenameList(LPCTSTR oldName, LPCTSTR newName);
void SongParams_DeleteList(LPCTSTR name);

// (listName, path, mode, ret2) のエントリを探して out へコピー。見つかれば true。
bool SongParams_FindCopy(LPCTSTR listName, LPCTSTR path, int mode, int ret2, SongParam& out);

// 現在表示中プレイリストの名前(空なら "#index" の安定名)。
CString SongParams_CurrentListName();

// ツールチップ用: そのエントリのうちデフォルトと異なる項目だけを整形した文字列。
CString SongParams_BuildTipExtra(LPCTSTR listName, LPCTSTR path, int mode, int ret2);

// プレイリスト行番号から直接解決(常に pl->pc を参照)
CString SongParams_BuildTipExtraForRow(int row);
