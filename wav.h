// wav.h : WAV file playback (sub=999) - PCM, IEEE Float, ADPCM support
#pragma once

#include "stdafx.h"

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM         1
#endif
#ifndef WAVE_FORMAT_ADPCM
#define WAVE_FORMAT_ADPCM       2
#endif
#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT  3
#endif
#ifndef WAVE_FORMAT_ALAW
#define WAVE_FORMAT_ALAW        6
#endif
#ifndef WAVE_FORMAT_MULAW
#define WAVE_FORMAT_MULAW       7
#endif
#ifndef WAVE_FORMAT_IMA_ADPCM
#define WAVE_FORMAT_IMA_ADPCM   17
#endif
#ifndef WAVE_FORMAT_EXTENSIBLE
#define WAVE_FORMAT_EXTENSIBLE  0xFFFE
#endif

#pragma pack(push, 1)
typedef struct {
	DWORD dwChunkId;    // 'RIFF'
	DWORD dwChunkSize;
	DWORD dwFormat;     // 'WAVE'
} RIFFHEADER;

typedef struct {
	DWORD dwChunkId;    // 'fmt '
	DWORD dwChunkSize;
	WORD  wFormatTag;
	WORD  nChannels;
	DWORD nSamplesPerSec;
	DWORD nAvgBytesPerSec;
	WORD  nBlockAlign;
	WORD  wBitsPerSample;
	WORD  cbSize;       // extra format bytes
} FMTCHUNK;

typedef struct {
	DWORD dwChunkId;    // 'data'
	DWORD dwChunkSize;
} DATACHUNK;
#pragma pack(pop)

/* KSDATAFORMAT_SUBTYPE_* GUIDs as raw bytes for comparison (avoids WAVEFORMATEXTENSIBLE conflict) */
static const BYTE WAV_GUID_PCM[] =         { 0x01,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71 };
static const BYTE WAV_GUID_IEEE_FLOAT[] =  { 0x03,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71 };
static const BYTE WAV_GUID_ADPCM[] =       { 0x02,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71 };
static const BYTE WAV_GUID_IMA_ADPCM[] =   { 0x11,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71 };
static const BYTE WAV_GUID_ALAW[] =        { 0x06,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71 };
static const BYTE WAV_GUID_MULAW[] =       { 0x07,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71 };

struct wavinfo {
	DWORD nChannels;
	DWORD nSamplesPerSec;
	DWORD wBitsPerSample;
	DWORD wFormatTag;
	DWORD nBlockAlign;
	DWORD nSamplesPerBlock;
	__int64 dataOffset;
	__int64 dataSize;
	__int64 totalSamples;
};

class wav
{
public:
	CFile m_file;
	wavinfo m_info;
	BOOL m_bOpen;
	int m_formatTag;

	wav() : m_bOpen(FALSE), m_formatTag(0) { memset(&m_info, 0, sizeof(m_info)); }

	BOOL Open(const TCHAR* path, wavinfo* info)
	{
		Close();
		if (!m_file.Open(path, CFile::modeRead | CFile::shareDenyWrite, NULL))
			return FALSE;

		RIFFHEADER riff;
		if (m_file.Read(&riff, sizeof(riff)) != sizeof(riff))
			return FALSE;
		if (riff.dwChunkId != 0x46464952 || riff.dwFormat != 0x45564157) // 'RIFF','WAVE'
			return FALSE;

		FMTCHUNK fmt;
		memset(&fmt, 0, sizeof(fmt));
		BYTE fmtBuf[128];
		UINT toRead = 0;
		for (;;) {
			DWORD chunkId, chunkSize;
			if (m_file.Read(&chunkId, 4) != 4) return FALSE;
			if (m_file.Read(&chunkSize, 4) != 4) return FALSE;
			if (chunkId == 0x20746d66) { // 'fmt '
				toRead = (chunkSize < 128) ? chunkSize : 128;
				if (m_file.Read(fmtBuf, toRead) < 16) return FALSE;
				fmt.wFormatTag = *(WORD*)&fmtBuf[0];
				fmt.nChannels = *(WORD*)&fmtBuf[2];
				fmt.nSamplesPerSec = *(DWORD*)&fmtBuf[4];
				fmt.nAvgBytesPerSec = *(DWORD*)&fmtBuf[8];
				fmt.nBlockAlign = *(WORD*)&fmtBuf[12];
				fmt.wBitsPerSample = *(WORD*)&fmtBuf[14];
				fmt.cbSize = (toRead >= 18) ? *(WORD*)&fmtBuf[16] : 0;
				if (chunkSize > toRead)
					m_file.Seek(chunkSize - toRead, CFile::current);
				break;
			}
			m_file.Seek(chunkSize, CFile::current);
		}

		m_formatTag = fmt.wFormatTag;
		m_info.nChannels = fmt.nChannels;
		m_info.nSamplesPerSec = fmt.nSamplesPerSec;
		m_info.wBitsPerSample = fmt.wBitsPerSample;
		m_info.nBlockAlign = fmt.nBlockAlign;
		m_info.nSamplesPerBlock = 0;
		m_info.wFormatTag = m_formatTag;

		if (m_formatTag == WAVE_FORMAT_EXTENSIBLE && fmt.cbSize >= 22 && toRead >= 40) {
			BYTE* pSubFormat = &fmtBuf[24];
			if (memcmp(pSubFormat, WAV_GUID_IEEE_FLOAT, 16) == 0)
				m_formatTag = WAVE_FORMAT_IEEE_FLOAT;
			else if (memcmp(pSubFormat, WAV_GUID_PCM, 16) == 0)
				m_formatTag = WAVE_FORMAT_PCM;
			else if (memcmp(pSubFormat, WAV_GUID_ADPCM, 16) == 0)
				m_formatTag = WAVE_FORMAT_ADPCM;
			else if (memcmp(pSubFormat, WAV_GUID_IMA_ADPCM, 16) == 0)
				m_formatTag = WAVE_FORMAT_IMA_ADPCM;
			else if (memcmp(pSubFormat, WAV_GUID_ALAW, 16) == 0)
				m_formatTag = WAVE_FORMAT_ALAW;
			else if (memcmp(pSubFormat, WAV_GUID_MULAW, 16) == 0)
				m_formatTag = WAVE_FORMAT_MULAW;
			m_info.wFormatTag = m_formatTag;
		}
		if ((m_formatTag == WAVE_FORMAT_ADPCM || m_formatTag == WAVE_FORMAT_IMA_ADPCM) && fmt.cbSize >= 2 && toRead >= 18) {
			m_info.nSamplesPerBlock = *(WORD*)&fmtBuf[18];
			if (m_info.nSamplesPerBlock == 0) m_info.nSamplesPerBlock = 256;
		}

		for (;;) {
			DWORD chunkId, chunkSize;
			if (m_file.Read(&chunkId, 4) != 4) return FALSE;
			if (m_file.Read(&chunkSize, 4) != 4) return FALSE;
			if (chunkId == 0x61746164) { // 'data'
				m_info.dataOffset = m_file.GetPosition();
				m_info.dataSize = chunkSize;
				if (m_formatTag == WAVE_FORMAT_ADPCM || m_formatTag == WAVE_FORMAT_IMA_ADPCM) {
					if (m_info.nBlockAlign > 0 && m_info.nSamplesPerBlock > 0)
						m_info.totalSamples = (chunkSize / m_info.nBlockAlign) * m_info.nSamplesPerBlock;
					else
						m_info.totalSamples = chunkSize * 4;
				}
				else {
					DWORD bytesPerSample = (m_info.wBitsPerSample + 7) / 8 * m_info.nChannels;
					if (bytesPerSample > 0)
						m_info.totalSamples = chunkSize / bytesPerSample;
					else
						m_info.totalSamples = 0;
				}
				break;
			}
			m_file.Seek(chunkSize, CFile::current);
		}

		if (info) *info = m_info;
		m_bOpen = TRUE;
		return TRUE;
	}

	void Close()
	{
		if (m_bOpen) {
			m_file.Close();
			m_bOpen = FALSE;
		}
	}

	int Render(BYTE* buf, int len)
	{
		if (!m_bOpen || len <= 0) return 0;
		UINT nRead = m_file.Read(buf, len);
		return nRead;
	}

	BOOL Seek(__int64 samplePos)
	{
		if (!m_bOpen) return FALSE;
		DWORD bytesPerSample = (m_info.wBitsPerSample + 7) / 8 * m_info.nChannels;
		__int64 pos = m_info.dataOffset + samplePos * bytesPerSample;
		if (pos < m_info.dataOffset || pos >= m_info.dataOffset + m_info.dataSize)
			return FALSE;
		m_file.Seek((LONG)pos, CFile::begin);
		return TRUE;
	}

	BOOL IsPCM() const { return m_formatTag == WAVE_FORMAT_PCM; }
	BOOL IsFloat() const { return m_formatTag == WAVE_FORMAT_IEEE_FLOAT; }
	BOOL IsADPCM() const { return m_formatTag == WAVE_FORMAT_ADPCM || m_formatTag == WAVE_FORMAT_IMA_ADPCM; }
	BOOL IsMSADPCM() const { return m_formatTag == WAVE_FORMAT_ADPCM; }
	BOOL IsIMAADPCM() const { return m_formatTag == WAVE_FORMAT_IMA_ADPCM; }
	BOOL IsALaw() const { return m_formatTag == WAVE_FORMAT_ALAW; }
	BOOL IsMuLaw() const { return m_formatTag == WAVE_FORMAT_MULAW; }
	BOOL IsFakeAAC()
	{
		if (!m_bOpen || m_info.dataSize < 2) return FALSE;
		ULONGLONG savePos = m_file.GetPosition();
		m_file.Seek((LONG)m_info.dataOffset, CFile::begin);
		BYTE hdr[2];
		BOOL ok = (m_file.Read(hdr, 2) == 2 && hdr[0] == 0xFF && (hdr[1] & 0xF0) == 0xF0);
		m_file.Seek((LONG)savePos, CFile::begin);
		return ok;
	}
};
