#pragma once

// メディアプレイヤーモード専用: 演奏時間ベースのプロンプト実行エンジン
// 形式: @<cmd><time>[-<endTime>][<val>[-<endVal>]]
// 例: @p50-1:20[100-120]  @p1:50[100]  @sb1:30

struct MpPromptBackup {
	int pitchSl;   // 0..400 (200=100%)
	int tempoSl;
	int dsvol;
	int eq[20];
	int eqReverb;
	int eqChorus;
	int eqDelay;
};

void MpPromptBackupCapture(MpPromptBackup& out);
void MpPromptBackupRestore(const MpPromptBackup& in);
void MpPromptBackupToSavedata(const MpPromptBackup& b);
void MpPromptBackupFromSavedata(MpPromptBackup& b);

BOOL MpPromptParse(const CString& text, CString* errMsg = nullptr);
void MpPromptClearEvents();

BOOL MpPromptIsActive();
void MpPromptSetActive(BOOL active);
BOOL MpPromptHasBackup();

// 実行: バックアップ保存→解析→ON
BOOL MpPromptExecute(const CString& text, CString* errMsg = nullptr);
// 停止: OFF(値は維持)
void MpPromptStop();
// リセット: バックアップ復元→OFF
void MpPromptReset();
// クリア: イベント消去→リセット
void MpPromptClearAll();

// 曲切替・再演奏時( play() 開始時)。実行中は次の再生開始まで待機し、値はバックアップへ戻す。
void MpPromptOnTrackChange();
// 演奏停止( stop() )時。値をバックアップへ戻し、実行中なら次の再生開始まで待機。
void MpPromptOnPlaybackStop();
// アプリ終了時。バックアップ復元して保存データを正常化。
void MpPromptOnAppShutdown();
// timerp から plf と GDI 時刻(ttt/100)を渡す(再生開始エッジ検出込み)
void MpPromptNotifyPlayback(int plf, double tSec);
// timerp 等から毎フレーム呼ぶ(演奏中のみ)。tSec は GDI バナー時間表示(t3)を渡す。
void MpPromptTickAtTime(double tSec);
void MpPromptTick();

// 演奏時間(秒): GDI バナー表示と同じ(曲頭からの実再生位置、DS 先読み補正済み)
double MpGetPerformanceTimeSec();
