#pragma once

#include <stdint.h>

// Simple request/response protocol over Named Pipe (byte-oriented).
// All integers are little-endian. Strings are UTF-16LE (Windows wchar_t).

static const wchar_t* const KPIHOST64_PIPE_NAME = L"\\\\.\\pipe\\ogg_kpi64";

enum KPIHOST64_CMD : uint32_t
{
	KPIHOST64_CMD_PING = 1, // optional payload: u32 lang (0=ja … 13=tr, same as savedata.lang)
	KPIHOST64_CMD_LIST_EXTS = 2, // input: kpiPath
	KPIHOST64_CMD_OPEN = 3,      // input: kpiPath, mediaPath, request(KPI_MEDIAINFO), songNo
	KPIHOST64_CMD_RENDER = 4,    // input: sessionId, bytesWanted
	KPIHOST64_CMD_SEEK = 5,      // input: sessionId, posSample, flag
	KPIHOST64_CMD_CLOSE = 6,     // input: sessionId
	KPIHOST64_CMD_FOREIGN_LIST_EXTS = 10, // input: pluginKind + path
	KPIHOST64_CMD_FOREIGN_OPEN = 11,
	KPIHOST64_CMD_FOREIGN_RENDER = 12,
	KPIHOST64_CMD_FOREIGN_SEEK = 13,
	KPIHOST64_CMD_FOREIGN_CLOSE = 14,
	KPIHOST64_CMD_VST_OPEN = 20,   // [u32 slot][u32 midChars][mid][u32 dllChars][dll][u32 extraChars][extra]
	KPIHOST64_CMD_VST_RENDER = 21, // RenderReq.sessionId = slot 0/1
	KPIHOST64_CMD_VST_SEEK = 22,   // SeekReq.sessionId = slot
	KPIHOST64_CMD_VST_CLOSE = 23,  // optional U32 slot; empty = close all
	// VST Live parts (x86 app hosting x64 plug-ins such as SOUND Canvas VA).
	// Only lifecycle goes over the pipe; notes and audio use shared memory so
	// the keyboard is never blocked by a pending render request.
	KPIHOST64_CMD_VST_LIVE_LOAD = 30,
	KPIHOST64_CMD_VST_LIVE_UNLOAD = 31,
	KPIHOST64_CMD_VST_LIVE_UNLOAD_ALL = 32,
	KPIHOST64_CMD_VST_LIVE_MIDI = 33,
	KPIHOST64_CMD_VST_LIVE_SYSEX = 34,
	KPIHOST64_CMD_VST_LIVE_RENDER = 35,
	KPIHOST64_CMD_VST_LIVE_AUDIO_START = 36,
	KPIHOST64_CMD_VST_LIVE_AUDIO_STOP = 37,
	KPIHOST64_CMD_VST_LIVE_EDITOR_OPEN = 38,
	KPIHOST64_CMD_VST_LIVE_EDITOR_CLOSE = 39,
	// Per-part routing and program picking for the part grid's slot menu.
	KPIHOST64_CMD_VST_LIVE_SEND_CH = 40,
	KPIHOST64_CMD_VST_LIVE_PROGRAMS = 41,
	KPIHOST64_CMD_VST_LIVE_SET_PROGRAM = 42
};

enum KPIHOST64_STATUS : uint32_t
{
	KPIHOST64_STATUS_OK = 0,
	KPIHOST64_STATUS_FAIL = 1,
	KPIHOST64_STATUS_BAD_REQUEST = 2,
	KPIHOST64_STATUS_NOT_FOUND = 3,
	KPIHOST64_STATUS_NOT_SUPPORTED = 4
};

#pragma pack(push, 1)
struct KPIHOST64_MsgHeader
{
	uint32_t cmd;
	uint32_t requestId;
	uint32_t payloadBytes;
};

struct KPIHOST64_ReplyHeader
{
	uint32_t cmd;
	uint32_t requestId;
	uint32_t status;
	uint32_t payloadBytes;
};

// Payload helpers
struct KPIHOST64_U32
{
	uint32_t v;
};

struct KPIHOST64_ListExtsReply
{
	uint32_t kpiVer; // 2 or 5 (best-effort)
	// followed by: [u32 chars][wchar_t[] supportExts]
};

struct KPIHOST64_OpenReq
{
	uint32_t songNo; // 1-based
	// followed by: [u32 kpiPathChars][wchar_t[]]
	//             [u32 mediaPathChars][wchar_t[]]
	//             [KPI_MEDIAINFO request]
};

struct KPIHOST64_OpenReply
{
	uint32_t sessionId;
	uint32_t openedSongCount;
	// followed by KPI_MEDIAINFO selected
};

struct KPIHOST64_RenderReq
{
	uint32_t sessionId;
	uint32_t bytesWanted;
};

/* eof 下位: 1=短い読み / 2=まだ MIDI イベントが残っている / 4=この塊で SysEx か CC を送った */
enum {
	KPIHOST64_EOF_SHORT = 1u,
	KPIHOST64_EOF_MIDI_PENDING = 2u,
	KPIHOST64_EOF_MIDI_KEEPALIVE = 4u
};

struct KPIHOST64_RenderReply
{
	uint32_t sessionId;
	uint32_t bytesReturned;
	uint32_t eof; // bit0=短い読み bit1=MIDI未消化 bit2=この塊でSysEx/CC
	// followed by PCM bytes
};

struct KPIHOST64_SeekReq
{
	uint32_t sessionId;
	uint64_t posSample;
	uint32_t flag;
};

struct KPIHOST64_SeekReply
{
	uint32_t sessionId;
	uint64_t newPosSample;
};

struct KPIHOST64_ForeignListReq
{
	uint32_t pluginKind; // 1=winamp 2=xmplay 3=aimp
	// followed by: [u32 pathChars][wchar_t[]]
};

struct KPIHOST64_ForeignOpenReq
{
	uint32_t pluginKind;
	// followed by: [u32 dllPathChars][wchar_t[]]
	//             [u32 mediaPathChars][wchar_t[]]
};

struct KPIHOST64_ForeignOpenReply
{
	uint32_t sessionId;
	uint32_t sampleRate;
	uint32_t channels;
	int32_t bitsPerSample;
	uint64_t lengthSamples;
	uint32_t latencySamples; // VST MIDI plugin delay; 0 for foreign/mapper
};

struct KPIHOST64_VstLiveLoadReq
{
	uint32_t part;   // 1..32
	uint32_t isVst3; // 0=VST2 DLL, 1=VST3 bundle
	// followed by: [u32 pathChars][wchar_t[]]
};

struct KPIHOST64_VstLiveMidiReq
{
	uint32_t port; // 0..2
	uint32_t msg;  // packed short message
	int32_t sampleOfs; // frames into this render block; 0 = block start
};

struct KPIHOST64_VstLiveSysexReq
{
	uint32_t port;
	uint32_t bytes;
	// followed by raw sysex bytes
};

struct KPIHOST64_VstLiveSendChReq
{
	uint32_t part;   // 1..32
	int32_t sendCh;  // -1 = keep the received channel, 0..15 = force
};

struct KPIHOST64_VstLiveProgramsReq
{
	uint32_t part;
	uint32_t first;
	uint32_t count; // names wanted, clamped by the host
};

struct KPIHOST64_VstLiveProgramsReply
{
	uint32_t total;   // programs the plug-in offers
	uint32_t current; // program in use, or 0xFFFFFFFF when unknown
	uint32_t got;     // names that follow
	// followed by got x [u32 chars][wchar_t[]]
};

struct KPIHOST64_VstLiveSetProgramReq
{
	uint32_t part;
	uint32_t index;
};

struct KPIHOST64_VstLiveRenderReq
{
	uint32_t frames;
};

struct KPIHOST64_VstLiveRenderReply
{
	uint32_t frames;
	// followed by frames * 2 interleaved floats (L,R)
};

// Audio ring written by KpiHost64, read by the app. Capacities are powers of
// two so producer and consumer can mask instead of taking a lock.
struct KPIHOST64_VstLiveAudioShm
{
	uint32_t capacity; // frames
	uint32_t sampleRate;
	volatile uint32_t writePos;
	volatile uint32_t readPos;
	// followed by capacity floats (L) then capacity floats (R)
};

struct KPIHOST64_VstLiveMidiEvent
{
	uint32_t port;
	uint32_t msg;
};

// MIDI ring written by the app, drained by KpiHost64's render thread.
struct KPIHOST64_VstLiveMidiShm
{
	uint32_t capacity; // events
	volatile uint32_t writePos;
	volatile uint32_t readPos;
	// followed by capacity KPIHOST64_VstLiveMidiEvent
};
#pragma pack(pop)

enum : uint32_t
{
	KPIHOST64_VST_LIVE_SHM_CAP = 32768, // frames, power of two
	KPIHOST64_VST_LIVE_MIDI_CAP = 8192  // events, power of two
};

static const wchar_t* const KPIHOST64_VST_LIVE_SHM_NAME =
	L"Local\\ogg_kpi64_vstlive_audio";
static const wchar_t* const KPIHOST64_VST_LIVE_MIDI_SHM_NAME =
	L"Local\\ogg_kpi64_vstlive_midi";
static const wchar_t* const KPIHOST64_VST_LIVE_EVENT_NAME =
	L"Local\\ogg_kpi64_vstlive_wake";

