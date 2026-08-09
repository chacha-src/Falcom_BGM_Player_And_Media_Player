#pragma once
// ============================================================================
// ProAudio : ギャップレス、ReplayGain、A/B、相関、M/S、
//            書き出しリミッター、キュー、曲別拡張データの中核。
// 固定配列前提。呼び出し点は equaliser / 再生終端 / アナライザー / 書き出しに限定。
// ============================================================================

#ifndef WM_APP_PROAUDIO_CUESEEK
#define WM_APP_PROAUDIO_CUESEEK (WM_APP + 77)
#endif

enum {
	PRO_CUE_MAX = 8,
	PRO_XFADE_MAX_MS = 5000,             // UI/保存の上限
	PRO_XFADE_TAIL_FRAMES = 48000 * 5,   // 最大5秒分@48k (高SRでは時間は短くなる)
	PRO_SONG_EXTRA_MAX = 4096,
	PRO_WAVE_PEAKS = 2048
};

// savedata 末尾フィールドの既定値ヘルパ
inline int ProClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---- 曲別拡張(ReplayGain / キュー)。SongParam とは別ファイル ----
struct ProCue {
	int frame;          // PCM フレーム位置(0=未使用)
	TCHAR label[32];
};

struct ProSongExtra {
	TCHAR listName[256];
	TCHAR path[1024];
	int mode;
	int ret2;
	float trackGainDb;   // ReplayGain track (0=未計測は -1000 番兵ではなく valid フラグ)
	float trackPeak;     // 0..数
	float albumGainDb;
	int   rgValid;       // 1=trackGainDb 有効
	ProCue cues[PRO_CUE_MAX];
	int   cueCount;
	int   loopIn;        // 独自ループ上書き(サンプル)。-1=プレイリスト値を使う
	int   loopOut;
	int   loopFadeMs;
	int   albumRgValid;  // 1=albumGainDb 有効(0dB も正当)。末尾追記
	int   rating;        // 0..5。末尾追記(ファイル ver3)
	int   playCount;     // 再生回数。末尾追記(ファイル ver4)
	FILETIME lastPlay;   // 最終再生(ローカル FILETIME)。0=未
	int   setChapter;    // 0=なし 1=Warmup 2=Peak 3=Cooldown。末尾追記
};

// ---- A/B スナップショット(EQ/環境/FX/テンポ/ピッチ) ----
struct ProAbSlot {
	int valid;
	int eq[20];
	int eqsoundenv;
	int eqsoundeq;
	int eqsoundeffect;
	int eq_reverb;
	int eq_chorus;
	int eq_delay;
	int tempoPos;
	int pitchPos;
	int pro_ms_width;
	int pro_ms_mono;
};

// ---- 初期化 / 永続化 ----
void ProAudio_Init();
void ProAudio_LoadExtras();
void ProAudio_SaveExtras();

// ---- ギャップレス ----
// クロスフェード尺は撤去済み。常に 0（互換 API）
int  ProAudio_XfadeMs();
// PCM 出力直前に毎回呼ぶ: 末尾リングへ蓄積
void ProAudio_PushTailPcm(const void* interleaved, int byteLen, int sampleRate, int bits, int channels);
// 曲開始時: 前曲テールと先頭を合成（ms=0 なら実質 no-op）
int  ProAudio_ApplyXfadeIn(void* interleaved, int byteLen, int sampleRate, int bits, int channels);
// 連続再生で早期切替すべきか（クロス撤去後は常に false 寄り）
bool ProAudio_ShouldEarlyAdvance(__int64 heardBytes, __int64 endWrittenBytes, int outBytesPerFrame, int sampleRate);
// 次曲開始前にテールを確定(end 検出時)
void ProAudio_CommitTailForNext();
void ProAudio_ResetXfadeIn();
void ProAudio_ClearXfadeIn();
void ProAudio_OnSongBoundary(); // 曲切替時にメータ等リセット
// ProExtra のループ上書きをグローバル loop1/loop2 へ反映(-1は触らない)
void ProAudio_ApplyLoopOverrideToGlobals();

// ---- ReplayGain / ラウドネス ----
void  ProAudio_LoudnessReset();
void  ProAudio_LoudnessFeed(const float* L, const float* R, int frames, int sampleRate);
void  ProAudio_LoudnessCommitCurrentSong(); // 計測結果を現在曲の extra へ
float ProAudio_ReplayGainLinear();          // 再生ゲイン(1.0=そのまま)
float ProAudio_LivePeak();                  // 短時定数ピーク 0..1（シーク波形用）
void  ProAudio_BumpLivePeak(float peak);    // ループバック等から短時定数ピークを更新
void  ProAudio_SetCurrentSongKey(LPCTSTR list, LPCTSTR path, int mode, int ret2);
bool  ProAudio_GetExtra(LPCTSTR list, LPCTSTR path, int mode, int ret2, ProSongExtra& out);
bool  ProAudio_UpsertExtra(const ProSongExtra& e);
void  ProAudio_ComputeAlbumGainsForList(LPCTSTR listName);

// ---- A/B ----
void ProAudio_AbCapture(int slot /*0 or 1*/);
void ProAudio_AbApply(int slot);
void ProAudio_AbToggle(); // A↔B。未キャプチャなら現在を反対側へ退避してから切替
int  ProAudio_AbActiveSlot(); // 0/1、-1=未使用

// ---- 位相 / 相関 ----
void  ProAudio_CorrReset();
void  ProAudio_CorrFeed(const float* L, const float* R, int frames);
float ProAudio_CorrValue();      // -1..+1
float ProAudio_CorrBalance();   // -1(L)..+1(R) 簡易エネルギー比
// EQ バイパス経路からも呼べる: インターリーブ PCM → ラウドネス/相関
void  ProAudio_FeedMetersFromInterleaved(const void* interleaved, int byteLen, int sampleRate, int bits, int channels);

// ---- Mid/Side (equaliser 最終寄りで適用) ----
// widthPct: 0..200 (100=中立)。monoCheck!=0 なら Side=0
void ProAudio_ApplyMS(float* L, float* R, int frames, int widthPct, int monoCheck);

// ---- 書き出し用 True Peak リミッター ----
// ceilingLin: 例 0.99。truePeak!=0 なら 4x オーバーサンプルで天井判定
void ProAudio_ExportLimitReset();
void ProAudio_ExportLimitProcess(float* L, float* R, int frames, int sampleRate, float ceilingLin, int truePeak);

// ---- キュー ----
int  ProAudio_CueAdd(int frame, LPCTSTR label);
bool ProAudio_CueGet(int index, ProCue& out);
int  ProAudio_CueCount();
void ProAudio_CueClearAll();
bool ProAudio_CueRemove(int index);
// 現在曲 extra のキューをメモリへロード / 保存
void ProAudio_CueLoadForCurrent();
void ProAudio_CueSaveForCurrent();

// ---- 評価(0..5) ----
int  ProAudio_GetRating(LPCTSTR list, LPCTSTR path, int mode, int ret2);
void ProAudio_SetRating(LPCTSTR list, LPCTSTR path, int mode, int ret2, int rating);

// ---- 再生回数(ver4) ----
int  ProAudio_GetPlayCount(LPCTSTR list, LPCTSTR path, int mode, int ret2);
void ProAudio_BumpPlayCount(LPCTSTR list, LPCTSTR path, int mode, int ret2);
bool ProAudio_GetLastPlay(LPCTSTR list, LPCTSTR path, int mode, int ret2, FILETIME& out);

// ---- ループ上書き(曲別) ----
void ProAudio_GetLoopOverride(int& inFrame, int& outFrame, int& fadeMs); // -1=無効
void ProAudio_SetLoopOverride(int inFrame, int outFrame, int fadeMs);

// ---- 波形ピーク概観(ループエディタ用)。成功でピーク数、失敗0 ----
// peaksL/peaksR は |peak| 0..1。固定長 PRO_WAVE_PEAKS まで。
int ProAudio_BuildWaveOverview(LPCTSTR path, float* peaksL, float* peaksR, int maxPeaks, int& outTotalFrames);
