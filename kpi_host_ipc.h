#pragma once

#include <stdint.h>

// Simple request/response protocol over Named Pipe (byte-oriented).
// All integers are little-endian. Strings are UTF-16LE (Windows wchar_t).

static const wchar_t* const KPIHOST64_PIPE_NAME = L"\\\\.\\pipe\\ogg_kpi64";

enum KPIHOST64_CMD : uint32_t
{
	KPIHOST64_CMD_PING = 1,
	KPIHOST64_CMD_LIST_EXTS = 2, // input: kpiPath
	KPIHOST64_CMD_OPEN = 3,      // input: kpiPath, mediaPath, request(KPI_MEDIAINFO), songNo
	KPIHOST64_CMD_RENDER = 4,    // input: sessionId, bytesWanted
	KPIHOST64_CMD_SEEK = 5,      // input: sessionId, posSample, flag
	KPIHOST64_CMD_CLOSE = 6      // input: sessionId
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

struct KPIHOST64_RenderReply
{
	uint32_t sessionId;
	uint32_t bytesReturned;
	uint32_t eof; // 1 if decoder returned < requested
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
#pragma pack(pop)

