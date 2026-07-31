#pragma once

// 選択曲を export 風に読込しながら解析し、プロンプトコマンド文字列を生成する。
// パターンは順次追加していく前提の骨組み。

enum MpPromptAnalyzeMode {
	MP_ANA_BALANCED = 0,   // バランス
	MP_ANA_COMEDY,         // お笑い系
	MP_ANA_SERIOUS,        // シリアス系
	MP_ANA_ROMANTIC,       // ロマンチック
	MP_ANA_INTENSE,        // 激しめ
	MP_ANA_CHILL,          // チル
	MP_ANA_ELECTRO,        // エレクトロ
	MP_ANA_ORCHESTRAL,     // オーケストラ風
	MP_ANA_RETRO,          // レトロ
	MP_ANA_CINEMATIC,      // シネマティック
	MP_ANA_ACOUSTIC,       // アコースティック
	MP_ANA_VOCAL,          // ボーカルフォーカス
	MP_ANA_CLUB,           // クラブ/ダンス
	MP_ANA_AMBIENT,        // アンビエント
	MP_ANA_LIVE,           // ライブステージ
	MP_ANA_SOFTPOP,        // ソフトポップ
	MP_ANA_HEALING,        // ヒーリング
	MP_ANA_RELAX,          // リラックス
	MP_ANA_SLEEP,          // スリープ
	MP_ANA_YASURAGI,       // やすらぎ
	MP_ANA_MODE_COUNT = 20
};

CString MpPromptAnalyzeModeName(int mode);
int MpPromptAnalyzeModeClamp(int mode);

BOOL MpPromptAnalyzeIsActive();
void MpPromptAnalyzeBegin();
void MpPromptAnalyzeFeed(const void* p, UINT n, int rate, int ch, int bits);
void MpPromptAnalyzeAbort();

typedef void (*MpPromptAnalyzeProgressCb)(int percent /*0..100*/, LPCTSTR status, void* user);
void MpPromptAnalyzeSetProgressCb(MpPromptAnalyzeProgressCb cb, void* user);
void MpPromptAnalyzeSetExpectedDurationSec(double sec);

// mode: MpPromptAnalyzeMode。進捗は SetProgressCb 経由。
BOOL MpPromptAnalyzeSelected(CString& outText, int mode, CString* errMsg = nullptr);

extern volatile LONG g_mpPromptAnalyzeOnly;
