#pragma once

#include <stdint.h>

// ============================================================================
// KpiHost64 名前付きパイプ IPC
// ----------------------------------------------------------------------------
// 32bit の本体（oggYSEDbgm）は 64bit KPI / VST / 外部プラグインを直接 LoadLibrary
// できない。そのため別プロセス KpiHost64.exe を起こし、このプロトコルで
// 「開く・読む・シーク・閉じる」を依頼する。
//
// 通信は名前付きパイプ（バイトモード、リトルエンディアン）。文字列は UTF-16LE
// （Windows の wchar_t）。1 本のパイプを本体が直列化して使う（同時に複数の
// リクエストは出さない）。
//
// 例外: VST ライブ（鍵盤で鳴らす側）のノートと PCM はパイプを通さない。
// 共有メモリのリングバッファへ書き、レンダースレッドがそれを拾う。
// パイプはロード／アンロード／エディタ開閉などライフサイクルだけ。
// ============================================================================

// 本体とホストがつなぐパイプ名。Local 名前空間なので同一ユーザー内。
static const wchar_t* const KPIHOST64_PIPE_NAME = L"\\\\.\\pipe\\ogg_kpi64";

// 本体 → ホストのコマンド番号。ヘッダ KPIHOST64_MsgHeader.cmd に入る。
enum KPIHOST64_CMD : uint32_t
{
	KPIHOST64_CMD_PING = 1, // 生存確認。任意で u32 lang（savedata.lang と同じ 0=ja … 13=tr）
	KPIHOST64_CMD_LIST_EXTS = 2, // KPI DLL が扱う拡張子一覧。入力: kpiPath
	KPIHOST64_CMD_OPEN = 3,      // KPI でメディアを開く。入力: kpiPath, mediaPath, KPI_MEDIAINFO, songNo
	KPIHOST64_CMD_RENDER = 4,    // PCM を読む。入力: sessionId, bytesWanted
	KPIHOST64_CMD_SEEK = 5,      // サンプル位置へシーク。入力: sessionId, posSample, flag
	KPIHOST64_CMD_CLOSE = 6,     // セッション破棄。入力: sessionId
	KPIHOST64_CMD_FOREIGN_LIST_EXTS = 10, // Winamp/XMPlay/AIMP の拡張子。入力: pluginKind + path
	KPIHOST64_CMD_FOREIGN_OPEN = 11,      // 外部プラグインでメディアを開く
	KPIHOST64_CMD_FOREIGN_RENDER = 12,
	KPIHOST64_CMD_FOREIGN_SEEK = 13,
	KPIHOST64_CMD_FOREIGN_CLOSE = 14,
	// プレイリストの .mid を x64 VST で鳴らす（曲ファイル再生）。スロット 0/1 はクロスフェード用。
	KPIHOST64_CMD_VST_OPEN = 20,   // [u32 slot][u32 midChars][mid][u32 dllChars][dll][u32 extraChars][extra]
	KPIHOST64_CMD_VST_RENDER = 21, // RenderReq.sessionId = スロット 0 または 1
	KPIHOST64_CMD_VST_SEEK = 22,   // SeekReq.sessionId = スロット
	KPIHOST64_CMD_VST_CLOSE = 23,  // 任意の U32 スロット。空ペイロードなら両方閉じる
	// VST ホスト画面のパート（鍵盤リアルタイム）。
	// パイプはライフサイクルのみ。ノートと音声は共有メモリ。
	KPIHOST64_CMD_VST_LIVE_LOAD = 30,
	KPIHOST64_CMD_VST_LIVE_UNLOAD = 31,
	KPIHOST64_CMD_VST_LIVE_UNLOAD_ALL = 32,
	KPIHOST64_CMD_VST_LIVE_MIDI = 33,   // フォールバック（通常は MIDI 共有メモリ）
	KPIHOST64_CMD_VST_LIVE_SYSEX = 34,
	KPIHOST64_CMD_VST_LIVE_RENDER = 35, // フォールバック（通常は音声共有メモリ）
	KPIHOST64_CMD_VST_LIVE_AUDIO_START = 36,
	KPIHOST64_CMD_VST_LIVE_AUDIO_STOP = 37,
	KPIHOST64_CMD_VST_LIVE_EDITOR_OPEN = 38,
	KPIHOST64_CMD_VST_LIVE_EDITOR_CLOSE = 39,
	// パートグリッドのスロットメニュー用。MIDI チャンネル固定とプログラム一覧。
	KPIHOST64_CMD_VST_LIVE_SEND_CH = 40,
	KPIHOST64_CMD_VST_LIVE_PROGRAMS = 41,
	KPIHOST64_CMD_VST_LIVE_SET_PROGRAM = 42
};

// ホスト → 本体の結果コード。KPIHOST64_ReplyHeader.status。
enum KPIHOST64_STATUS : uint32_t
{
	KPIHOST64_STATUS_OK = 0,
	KPIHOST64_STATUS_FAIL = 1,           // LoadLibrary / デコーダ失敗など
	KPIHOST64_STATUS_BAD_REQUEST = 2,    // ペイロード長や形がおかしい
	KPIHOST64_STATUS_NOT_FOUND = 3,      // セッション ID が無い、ファイルが無い
	KPIHOST64_STATUS_NOT_SUPPORTED = 4   // KPI v5 入口が無い、など
};

#pragma pack(push, 1)
// 本体が先に送る固定ヘッダ。続けて payloadBytes 分の本文。
struct KPIHOST64_MsgHeader
{
	uint32_t cmd;          // KPIHOST64_CMD_*
	uint32_t requestId;    // 応答と突き合わせる連番（本体側が振る）
	uint32_t payloadBytes; // このヘッダの直後に続くバイト数
};

// ホストが返す固定ヘッダ。続けて payloadBytes 分の本文。
struct KPIHOST64_ReplyHeader
{
	uint32_t cmd;          // エコー（どのコマンドへの返事か）
	uint32_t requestId;    // 要求と同じ ID
	uint32_t status;       // KPIHOST64_STATUS_*
	uint32_t payloadBytes;
};

// 1 個の u32 だけ載せるとき（CLOSE の sessionId など）
struct KPIHOST64_U32
{
	uint32_t v;
};

struct KPIHOST64_ListExtsReply
{
	uint32_t kpiVer; // 2=旧 KMP、5=KPI v5。分からなければ 0
	// 続き: [u32 chars][wchar_t[] supportExts]  （例 ".spc/.vgm"）
};

struct KPIHOST64_OpenReq
{
	uint32_t songNo; // 1 始まりの曲番号（マルチソング形式用）
	// 続き: [u32 kpiPathChars][wchar_t[]]
	//       [u32 mediaPathChars][wchar_t[]]
	//       [KPI_MEDIAINFO request]
};

struct KPIHOST64_OpenReply
{
	uint32_t sessionId;      // 以降の Render/Seek/Close で使う
	uint32_t openedSongCount;
	// 続き: KPI_MEDIAINFO selected（実際に開いたフォーマット）
};

struct KPIHOST64_RenderReq
{
	uint32_t sessionId;
	uint32_t bytesWanted; // ほしい PCM バイト数（フレーム境界に揃える）
};

// RenderReply.eof のビット。短い読み＝曲末、MIDI 未消化＝初期化 SysEx 中の無音を切らない。
enum {
	KPIHOST64_EOF_SHORT = 1u,          // 要求より短い PCM（曲末のことが多い）
	KPIHOST64_EOF_MIDI_PENDING = 2u,   // まだ MIDI イベントが残っている
	KPIHOST64_EOF_MIDI_KEEPALIVE = 4u  // この塊で SysEx か CC を送った
};

struct KPIHOST64_RenderReply
{
	uint32_t sessionId;
	uint32_t bytesReturned;
	uint32_t eof; // bit0=短い読み bit1=MIDI未消化 bit2=この塊でSysEx/CC
	// 続き: PCM バイト列
};

struct KPIHOST64_SeekReq
{
	uint32_t sessionId;
	uint64_t posSample; // サンプル単位の絶対位置
	uint32_t flag;      // KPI_MEDIAINFO の SEEK_FLAGS_*
};

struct KPIHOST64_SeekReply
{
	uint32_t sessionId;
	uint64_t newPosSample; // 実際に着いた位置
};

struct KPIHOST64_ForeignListReq
{
	uint32_t pluginKind; // 1=Winamp 2=XMPlay 3=AIMP（PluginKinds.h と同じ）
	// 続き: [u32 pathChars][wchar_t[]]
};

struct KPIHOST64_ForeignOpenReq
{
	uint32_t pluginKind;
	// 続き: [u32 dllPathChars][wchar_t[]]
	//       [u32 mediaPathChars][wchar_t[]]
};

struct KPIHOST64_ForeignOpenReply
{
	uint32_t sessionId;
	uint32_t sampleRate;
	uint32_t channels;
	int32_t bitsPerSample;   // 負なら浮動小数（KPI と同じ慣習）
	uint64_t lengthSamples;
	uint32_t latencySamples; // VST MIDI のプラグイン遅延。外部/マッパーは 0
};

struct KPIHOST64_VstLiveLoadReq
{
	uint32_t part;   // パート番号 1..32（ホスト画面の行）
	uint32_t isVst3; // 0=VST2 DLL、1=VST3 バンドル
	// 続き: [u32 pathChars][wchar_t[]]
};

struct KPIHOST64_VstLiveMidiReq
{
	uint32_t port;     // 0..2（MIDI 入力口。ホストは最大 3 系統）
	uint32_t msg;      // パックしたショートメッセージ（ステータス|d1<<8|d2<<16）
	int32_t sampleOfs; // このレンダーブロック先頭からのフレーム。0=先頭
};

struct KPIHOST64_VstLiveSysexReq
{
	uint32_t port;
	uint32_t bytes;
	// 続き: SysEx 生バイト（F0 … F7）
};

struct KPIHOST64_VstLiveSendChReq
{
	uint32_t part;   // 1..32
	int32_t sendCh;  // -1=届いたチャンネルのまま、0..15=そのチャンネルへ強制
};

struct KPIHOST64_VstLiveProgramsReq
{
	uint32_t part;
	uint32_t first; // 取りたい先頭インデックス
	uint32_t count; // ほしい件数。ホスト側で上限を切る
};

struct KPIHOST64_VstLiveProgramsReply
{
	uint32_t total;   // プラグインが持つプログラム総数
	uint32_t current; // いま選ばれている番号。不明なら 0xFFFFFFFF
	uint32_t got;     // この応答に続く名前の数
	// 続き: got 個の [u32 chars][wchar_t[]]
};

struct KPIHOST64_VstLiveSetProgramReq
{
	uint32_t part;
	uint32_t index;
};

struct KPIHOST64_VstLiveRenderReq
{
	uint32_t frames; // ほしいステレオフレーム数（パイプ経由のときだけ）
};

struct KPIHOST64_VstLiveRenderReply
{
	uint32_t frames;
	// 続き: frames * 2 個の float（L,R 交互）
};

// ホストが書き、本体が読む音声リング。容量は 2 の冪なので、位置のラップはマスクで済む。
struct KPIHOST64_VstLiveAudioShm
{
	uint32_t capacity;          // フレーム数
	uint32_t sampleRate;
	volatile uint32_t writePos; // ホストが進める（累積。マスクしてインデックスに）
	volatile uint32_t readPos;  // 本体が進める
	// 続き: capacity 個の float（L）＋ capacity 個の float（R）
};

struct KPIHOST64_VstLiveMidiEvent
{
	uint32_t port;
	uint32_t msg;
};

// 本体が書き、ホストのレンダースレッドが読む MIDI リング。
struct KPIHOST64_VstLiveMidiShm
{
	uint32_t capacity; // イベント数
	volatile uint32_t writePos;
	volatile uint32_t readPos;
	// 続き: capacity 個の KPIHOST64_VstLiveMidiEvent
};
#pragma pack(pop)

enum : uint32_t
{
	KPIHOST64_VST_LIVE_SHM_CAP = 32768, // 音声リングのフレーム数（2 の冪）
	KPIHOST64_VST_LIVE_MIDI_CAP = 8192  // MIDI リングのイベント数（2 の冪）
};

static const wchar_t* const KPIHOST64_VST_LIVE_SHM_NAME =
	L"Local\\ogg_kpi64_vstlive_audio";
static const wchar_t* const KPIHOST64_VST_LIVE_MIDI_SHM_NAME =
	L"Local\\ogg_kpi64_vstlive_midi";
// 本体がノートを置いたあとホストの待ちを起こすイベント
static const wchar_t* const KPIHOST64_VST_LIVE_EVENT_NAME =
	L"Local\\ogg_kpi64_vstlive_wake";
