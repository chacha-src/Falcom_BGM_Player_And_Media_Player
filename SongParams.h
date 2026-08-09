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
//            チェックON時: 現在曲に★(エントリ)があれば読んで UI/DSP へ反映
//            チェックOFF時: 現行のパラメータをその曲エントリへ保存してから無効化
//   移行   : savedata.audioDataVersion (末尾追記)
//            0→1 で旧キー(pathのみ)を playlistu*.dat 照合により mode+ret2 付きへ自動コンバート
//            AudioData.dat ファイル ver: 1=pathのみ / 2=mode+ret2 / 3=BPM(選択+候補3+グリッド)
//   リスト : 名前変更/削除/曲の他リスト移動・コピー時に listName キーを追従
//            コンテキストメニューから選択曲の記憶パラメータだけ削除も可
//   BPM    : detectedBpm / bpmCand[3] / beatGrid は「曲ごと保存」OFFでも曲単位で保存・再生時復元
//   AudioData.dat ファイル ver: 1=pathのみ / 2=mode+ret2 / 3=BPM / 4=key+Camelot+gridOffset
//   索引   : メモリ上は主キー(list+path+mode+ret2)ハッシュで検索。削除は末尾入替。
// ============================================================================

// 再生スレッド(HandleNotifications)からメインスレッドへ復元を依頼するメッセージ
// 注意: COggDlg では WM_APP+70〜74/+99〜101 が使用済み。+75/+76 は SongParams 用。
#ifndef WM_APP_SONGPARAM_RESTORE
#define WM_APP_SONGPARAM_RESTORE (WM_APP + 75)
#endif
#ifndef WM_APP_SONGPARAM_MARKS
#define WM_APP_SONGPARAM_MARKS (WM_APP + 76) // ★列の再描画依頼(再生スレッド→メイン可)
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
	// --- ver3 追記: 曲ごとの BPM（選択中 + 候補3） ---
	int detectedBpm;       // 選択中 BPM (0=未)
	int bpmCand[3];        // 候補3つ (0=空き)
	int beatGrid;          // 1=シーク拍グリッド表示
	// --- ver4 追記: キー / Camelot / グリッド位相 ---
	int keyRoot;           // 0..11 (C=0), -1=未
	int keyMinor;          // 0=major 1=minor
	int camelot;           // 1..24 (1A..12A=1..12, 1B..12B=13..24), 0=未
	int beatGridOffsetMs;  // 拍グリッド位相オフセット(ms)。正=右へ
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

// 曲エントリの listName 付け替え / 複製 / 削除(一括・1回の SaveFile)。
// newListName == NULL        → 削除
// newListName != NULL && !copy → listName を付け替え(他リストへ移動)
// newListName != NULL && copy  → newListName 向けに複製(他リストへコピー)
void SongParams_RebindEntries(LPCTSTR listName, LPCTSTR newListName, const playlistdata0* items, int n, bool copy);

// (listName, path, mode, ret2) のエントリを探して out へコピー。見つかれば true。
bool SongParams_FindCopy(LPCTSTR listName, LPCTSTR path, int mode, int ret2, SongParam& out);

// エントリの有無だけ(リスト★列用。パラメータ本体は要らない)。
bool SongParams_HasEntry(LPCTSTR listName, LPCTSTR path, int mode, int ret2);

// 現在表示中プレイリストの名前(空なら "#index" の安定名)。
CString SongParams_CurrentListName();

// ツールチップ用: そのエントリのうちデフォルトと異なる項目だけを整形した文字列。
CString SongParams_BuildTipExtra(LPCTSTR listName, LPCTSTR path, int mode, int ret2);

// プレイリスト行番号から直接解決(常に pl->pc を参照)
CString SongParams_BuildTipExtraForRow(int row);

// 行に記憶パラメータがあるか(★列用。BuildTipExtraForRow と同じキー解決)。
bool SongParams_HasEntryForRow(int row);

// PL/MP リストの★列を再描画。別スレッドからは PostMessage 経由。
void SongParams_NotifyListMarksChanged();

// 現在曲の BPM（選択中 + 候補3 + グリッド + キー/オフセット）を AudioData.dat へ即時反映。
// 「曲ごと保存」チェックOFFでも BPM/キーだけは曲単位で覚える。
void SongParams_SaveBpmForCurrentSong();
void SongParams_RestoreBpmForCurrentSong();
void SongParams_SaveKeyGridForCurrentSong(); // key + beatGridOffsetMs を現エントリへ
