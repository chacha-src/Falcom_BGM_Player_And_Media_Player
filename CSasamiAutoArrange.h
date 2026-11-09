#pragma once
#include "SasamiComposerDoc.h"
#include "CSasamiStaffCore.h"
#include "CSasamiScoreHist.h"

/* 既存の音符は消さない。イントロ／間奏は後ろへずらして空いた小節に足す。 */

enum ScAutoStyle : int {
	SC_AS_16BEAT = 0,
	SC_AS_8BEAT,
	SC_AS_REGGAE,
	SC_AS_TECHNO,
	SC_AS_HOUSE,
	SC_AS_TRANCE,
	SC_AS_DNB,
	SC_AS_HIPHOP,
	SC_AS_FUNK,
	SC_AS_DISCO,
	SC_AS_ROCK,
	SC_AS_METAL,
	SC_AS_BALLAD,
	SC_AS_WALTZ,
	SC_AS_BOSSA,
	SC_AS_SAMBA,
	SC_AS_SHUFFLE,
	SC_AS_MARCH,
	SC_AS_POLKA,
	SC_AS_CITYPOP,
	SC_AS_GAME,
	SC_AS_ORCH,
	SC_AS_AMBIENT,
	SC_AS_BREAKS,
	SC_AS_LATIN,
	SC_AS_ENKA,
	SC_AS_FUSION,
	SC_AS_ACOUSTIC,
	SC_AS_PIANO,
	SC_AS_FANFARE,
	SC_AS_SLOWROCK,
	/* --- v2 追加テーマ: コード運びのプロファイル付き（ジャズ/ブルース系ほか） --- */
	SC_AS_JAZZ,       /* スウィング 4ビート ii-V-I */
	SC_AS_SWINGJAZZ,  /* ビッグバンド寄りのスウィング */
	SC_AS_BEBOP,      /* 速いスウィング、細かいライド */
	SC_AS_BLUES,      /* 12 小節ブルース I7-IV7-V7 */
	SC_AS_SLOWBLUES,  /* 12/8 のスローブルース */
	SC_AS_ROCKNROLL,  /* 50s ロックンロール・シャッフル */
	SC_AS_GOSPEL,     /* ゴスペル/6-2-5-1 */
	SC_AS_SOUL,       /* モータウン・ソウル */
	SC_AS_RNB,        /* 現代 R&B */
	SC_AS_NEOSOUL,    /* ネオソウル、揺れたハット */
	SC_AS_COUNTRY,    /* カントリー・トレインビート */
	SC_AS_PUNK,       /* パンク、8 分全開 */
	SC_AS_GRUNGE,     /* グランジ、重いバックビート */
	SC_AS_SKA,        /* スカ、裏打ち */
	SC_AS_KAYOKYOKU,  /* 歌謡曲 */
	SC_AS_ANISON,     /* アニソン、疾走 16 ビート */
	SC_AS_CHIPTUNE,   /* チップチューン、アルペジオ */
	SC_AS_TRAP,       /* トラップ、ハットロール */
	SC_AS_DRILL,      /* ドリル、スライドベース */
	SC_AS_AFROBEAT,   /* アフロビート */
	SC_AS_SWING3,     /* ジャズワルツ 3/4 スウィング */
	SC_AS_TANGO,      /* タンゴ */
	SC_AS_FLAMENCO,   /* フラメンコ（ルンバ） */
	SC_AS_SURF,       /* サーフロック */
	SC_AS_LOFI,       /* ローファイ・ヒップホップ */
	SC_AS_DRUMSOLO,   /* ドラム主体、ブレイク連発 */
	SC_AS_EDM,        /* ビルドアップ付き EDM */
	SC_AS_DUBSTEP,    /* ダブステップ、ハーフタイム */
	SC_AS_SYNTHWAVE,  /* シンセウェイブ */
	SC_AS_COUNT
};

/* アレンジのかけ方。 */
enum ScArrangeMode : int {
	SC_AM_OVERLAY = 0, /* 既存を残し、空いている拍・小節だけ埋める（重ね） */
	SC_AM_REWRITE = 1  /* 対象パートの既存ノートを一旦消し、進行に沿って作り直す（書き換え） */
};

/* 対象パートに与える役割。AUTO は中身から自動判定。 */
enum ScPartRole : int {
	SC_PR_AUTO = 0,
	SC_PR_DRUM,
	SC_PR_BASS,
	SC_PR_CHORD,
	SC_PR_MELODY,
	SC_PR_HARMONY,
	SC_PR_ARPEGGIO,
	SC_PR_PAD,
	SC_PR_ROLE_COUNT
};

enum ScAutoJob : int {
	SC_AJ_DRUM = 1,
	SC_AJ_BASS = 2,
	SC_AJ_CHORD = 4,
	SC_AJ_MEL_VOL = 8,
	SC_AJ_MEL_PITCH = 16,
	SC_AJ_INTRO = 32,
	SC_AJ_BREAK = 64,
	SC_AJ_ENDING = 128
};

enum ScAutoErr : int {
	SC_AE_OK = 0,
	SC_AE_NONOTES = 1,
	SC_AE_EMPTY = 2,
	SC_AE_OVERFLOW = 3
};

struct ScAutoArrangeOpt {
	int style;
	int jobs;
	int introBars;
	int breakBars;
	int endBars;
	int pitchMode; /* 0 off, 1 harmony, 2 +scoop, 3 +octave */
	int isFm;
	int curPart;
	int defMeterN;
	int defMeterD;
	int defKey;
	ScStaffUi* ui;
	/* --- v2 --- すべて 0 のとき従来（自動）動作に一致する。 */
	int mode;             /* ScArrangeMode: 重ね / 書き換え */
	int useProg;          /* 1=テーマのコード進行を空き・書き換え小節へ適用 */
	int seed;             /* 変化の種。0=時刻から自動（毎回違う結果） */
	int variation;        /* 0..N 進行・パターンのバリエーション番号 */
	uint32_t targetMask;  /* アレンジ対象 ch ビットマスク。0=自動選択（従来） */
	uint32_t drumMask;    /* ドラム出力に使う ch ビットマスク。0=自動 */
	uint8_t drumSlot[32]; /* ドラム ch ごとの枠 0=ドラム1 1=ドラム2（VST2 用） */
	uint8_t partRole[32]; /* 対象 ch ごとの役割 ScPartRole（AUTO=自動判定） */
	int genBars;          /* 既存ノートが無いときに生成する小節数。0=既定(16) */
};

struct ScAutoArrangeResult {
	int err;
	int added;
	int removed;      /* 書き換えで消した既存ノート数 */
	int chDrum, chBass, chChord, chHarm;
	uint32_t usedMask; /* 実際に書き込んだ ch */
	wchar_t msg[256];
};

struct ScAutoArrangeHost {
	ScEvent* ev;
	int* evCount;
	int evMax;
	ScStaffUi* ui;
	ScScoreHist* hist;
	int isFm;
	int curPart;
	int defMeterN;
	int defMeterD;
	int defKey;
	void* ctx;
	void (*refresh)(void* ctx);
	int (*preview)(void* ctx);
	void (*stop)(void* ctx);
};

int ScAutoArrangeCountNotes(const ScEvent* ev, int n);
const wchar_t* ScAutoStyleName(int style);
/* ダイアログ用: テーマのジャンル見出し（「ジャズ／ブルース」など）。 */
const wchar_t* ScAutoStyleGroupName(int style);
/* テーマのコード進行を「Dm7 - G7 - Cmaj7」のように説明。buf は十分な長さ。 */
void ScAutoStyleProgDesc(int style, int variation, int keySig, wchar_t* buf, int cch);
/* そのテーマが持つバリエーション数（コード進行の数）。 */
int ScAutoStyleVariationCount(int style);
int ScAutoArrangeApply(ScEvent* ev, int* evCount, int evMax,
	const ScAutoArrangeOpt* opt, ScAutoArrangeResult* out);
/* モーダル。試し聴きは host->preview、決定でヒストリへ積む。 */
int ScAutoArrangeDlgRun(const ScAutoArrangeHost* host);
/* 音符の無いパートのソロは外す。アンドゥ後に空ソロが残らないようにする。 */
void ScAutoArrangeReleaseEmptySolo(ScStaffUi* ui, const ScEvent* ev, int n);
