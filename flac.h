#pragma once
#define FLAC__NO_DLL
#include "flac/all.h"

class KbFlacDecoder
{
private:
	HANDLE m_hFile;
	FLAC__StreamDecoder* m_decoder;
	FLAC__StreamMetadata_StreamInfo m_stream_info;
	int    m_block_align;//=(bitspersample/8) * channels
	BYTE* m_direct_buf;
	DWORD  m_direct_buf_size;
	DWORD  m_direct_buf_copied;
	BYTE   m_temp_buf[FLAC__MAX_BLOCK_SIZE * 8 * 4]; // 8ch × 32bit
	DWORD  m_temp_buf_size;
	DWORD  m_temp_buf_remain;
	int    m_flacPosMode; // 1=SetPosition がサンプル直指定（0xBF）。グローバル flacmode に依存しない
	//
	static FLAC__StreamDecoderReadStatus read_callback(const FLAC__StreamDecoder* decoder,
		FLAC__byte buffer[],
		unsigned* bytes,
		void* client_data);
	static FLAC__StreamDecoderSeekStatus seek_callback(const FLAC__StreamDecoder* decoder,
		FLAC__uint64 absolute_byte_offset,
		void* client_data);
	static FLAC__StreamDecoderTellStatus tell_callback(const FLAC__StreamDecoder* decoder,
		FLAC__uint64* absolute_byte_offset,
		void* client_data);
	static FLAC__StreamDecoderLengthStatus length_callback(const FLAC__StreamDecoder* decoder,
		FLAC__uint64* stream_length,
		void* client_data);
	static FLAC__bool eof_callback(const FLAC__StreamDecoder* decoder, void* client_data);
	//
	static void metadata_callback(const FLAC__StreamDecoder* decoder,
		const FLAC__StreamMetadata* metadata,
		void* client_data);
	static FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder* decoder,
		const FLAC__Frame* frame,
		const FLAC__int32* const buffer[],
		void* client_data);
	static void error_callback(const FLAC__StreamDecoder* decoder,
		FLAC__StreamDecoderErrorStatus status,
		void* client_data);
	void metadata_callback(const FLAC__StreamDecoder* decoder,
		const FLAC__StreamMetadata* metadata);
	FLAC__StreamDecoderWriteStatus write_callback(const FLAC__StreamDecoder* decoder,
		const FLAC__Frame* frame,
		const FLAC__int32* const buffer[]);
	void error_callback(const FLAC__StreamDecoder* decoder,
		FLAC__StreamDecoderErrorStatus status);

public:
	BOOL  __fastcall Open(const _TCHAR* cszFileName, SOUNDINFO* pInfo);
	void  __fastcall Close(void);
	DWORD __fastcall SetPosition(LONGLONG dwPos);
	DWORD __fastcall Render(BYTE* Buffer, DWORD dwSize);
	BOOL  __fastcall IsEndOfStream(void);
	KbFlacDecoder(void);
	~KbFlacDecoder(void);
};

/////////////////////////////////////////////////////////////////////////////
KbFlacDecoder::KbFlacDecoder(void)
{
	m_decoder = NULL;
	m_hFile = INVALID_HANDLE_VALUE;
	ZeroMemory(&m_stream_info, sizeof(m_stream_info));
	m_block_align = 0;
	m_direct_buf = NULL;
	m_direct_buf_size = 0;
	m_direct_buf_copied = 0;
	m_temp_buf_size = 0;
	m_temp_buf_remain = 0;
	m_flacPosMode = 0;
}
/////////////////////////////////////////////////////////////////////////////
KbFlacDecoder::~KbFlacDecoder(void)
{
	Close();
}
/////////////////////////////////////////////////////////////////////////////
BOOL __fastcall KbFlacDecoder::Open(const _TCHAR* cszFileName, SOUNDINFO* pInfo)
{
	ZeroMemory(pInfo, sizeof(SOUNDINFO));
	HANDLE hFile;
#if UNICODE	
	hFile = CreateFileW(cszFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#else
	hFile = CreateFileA(cszFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
#endif
	if (hFile == INVALID_HANDLE_VALUE) {
		return FALSE;
	}
	/* 先頭 0xBF はインスタンスに記録（スロット/曲1でグローバル flacmode を共有しない） */
	m_flacPosMode = 0;
	{
		BYTE a = 0;
		DWORD nr = 0;
		if (ReadFile(hFile, &a, 1, &nr, NULL) && nr == 1 && a == 0xBF)
			m_flacPosMode = 1;
		SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
	}
	FLAC__StreamDecoder* decoder = FLAC__stream_decoder_new();

	m_hFile = hFile;
	m_decoder = decoder;

	if (FLAC__stream_decoder_init_stream(decoder,
		read_callback,
		seek_callback,
		tell_callback,
		length_callback,
		eof_callback,
		write_callback,
		metadata_callback,
		error_callback,
		this) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
		Close();
		return FALSE;
	}
	if (!FLAC__stream_decoder_process_until_end_of_metadata(decoder)) {
		//OGG Flac?
		FLAC__stream_decoder_finish(decoder);
		FLAC__stream_decoder_delete(decoder);
		m_decoder = decoder = FLAC__stream_decoder_new();
		SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
		if (FLAC__stream_decoder_init_ogg_stream(decoder,
			read_callback,
			seek_callback,
			tell_callback,
			length_callback,
			eof_callback,
			write_callback,
			metadata_callback,
			error_callback,
			this) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
			Close();
			return FALSE;
		}
		if (!FLAC__stream_decoder_process_until_end_of_metadata(decoder)) {
			Close();
			return FALSE;
		}
	}
	if (m_stream_info.bits_per_sample != 24 &&
		m_stream_info.bits_per_sample != 16 &&
		m_stream_info.bits_per_sample != 8) {
		Close();
		return FALSE;
	}
	m_block_align = (m_stream_info.bits_per_sample / 8) * m_stream_info.channels;
	pInfo->dwSamplesPerSec = m_stream_info.sample_rate;
	pInfo->dwChannels = m_stream_info.channels;
	pInfo->dwBitsPerSample = m_stream_info.bits_per_sample;
	pInfo->dwSeekable = 1;
	pInfo->dwUnitRender = 0;
	pInfo->dwLength = MulDiv(m_stream_info.total_samples, 1000, m_stream_info.sample_rate);
	CString s;
	s.Format(L"%d", (m_stream_info.total_samples * (LONGLONG)m_stream_info.sample_rate) / (LONGLONG)1000);
	//AfxMessageBox(s);
	pInfo->dwReserved1 = 0;
	pInfo->dwReserved2 = 0;
	return TRUE;
}
void __fastcall KbFlacDecoder::Close(void)
{
	if (m_decoder) {
		FLAC__stream_decoder_finish(m_decoder);
		FLAC__stream_decoder_delete(m_decoder);
		m_decoder = NULL;
	}
	if (m_hFile != INVALID_HANDLE_VALUE) {
		CloseHandle(m_hFile);
		m_hFile = INVALID_HANDLE_VALUE;
	}
	ZeroMemory(&m_stream_info, sizeof(m_stream_info));
}
/////////////////////////////////////////////////////////////////////////////
DWORD __fastcall KbFlacDecoder::SetPosition(LONGLONG dwPos)
{
	m_direct_buf = NULL;
	m_direct_buf_size = m_direct_buf_copied = 0;//write_callback が呼ばれるので必ず必要
	m_temp_buf_size = m_temp_buf_remain = 0;    //write_callback が呼ばれるので必ず必要
	try {
		/* インスタンス mode 優先。未設定時のみレガシー互換でグローバル flacmode */
		const int posMode = (m_flacPosMode != 0) ? m_flacPosMode : flacmode;
		LONGLONG dwPosSample = (dwPos * (LONGLONG)m_stream_info.sample_rate) / (LONGLONG)1000;
		if (posMode == 1) {
			if (FLAC__stream_decoder_seek_absolute(m_decoder, dwPos)) {
				return dwPos;
			}
		}
		else
		{
			if (FLAC__stream_decoder_seek_absolute(m_decoder, dwPosSample)) {
				return dwPos;
			}

		}
	}
	catch (...) {}
	return 0;
}
/////////////////////////////////////////////////////////////////////////////
DWORD __fastcall KbFlacDecoder::Render(BYTE* Buffer, DWORD dwSize)
{
	DWORD dwRet = 0;
	while (1) {
		if (m_temp_buf_remain) {
			DWORD copy_bytes = m_temp_buf_remain;
			if (copy_bytes + dwRet > dwSize) {
				copy_bytes = dwSize - dwRet;
			}
			memcpy(Buffer + dwRet, m_temp_buf + m_temp_buf_size - m_temp_buf_remain, copy_bytes);
			m_temp_buf_remain -= copy_bytes;
			dwRet += copy_bytes;
			if (dwRet == dwSize) {
				break;
			}
		}
		if (FLAC__stream_decoder_get_state(m_decoder) == FLAC__STREAM_DECODER_END_OF_STREAM) {
			break;
		}
		m_direct_buf = Buffer + dwRet;
		m_direct_buf_size = dwSize - dwRet;
		m_direct_buf_copied = 0;
		if (!FLAC__stream_decoder_process_single(m_decoder)) {
			break;
		}
		dwRet += m_direct_buf_copied;
		if (dwRet == dwSize) {
			break;
		}
	}
	return dwRet;
}
/////////////////////////////////////////////////////////////////////////////
BOOL __fastcall KbFlacDecoder::IsEndOfStream(void)
{
	if (!m_decoder) return FALSE;
	return FLAC__stream_decoder_get_state(m_decoder) == FLAC__STREAM_DECODER_END_OF_STREAM;
}
/////////////////////////////////////////////////////////////////////////////
FLAC__StreamDecoderWriteStatus KbFlacDecoder::write_callback(const FLAC__StreamDecoder* decoder, const FLAC__Frame* frame, const FLAC__int32* const buffer[], void* client_data)
{
	KbFlacDecoder* flacDecoder = (KbFlacDecoder*)client_data;
	return flacDecoder->write_callback(decoder, frame, buffer);
}
/////////////////////////////////////////////////////////////////////////////
void KbFlacDecoder::metadata_callback(const FLAC__StreamDecoder* decoder, const FLAC__StreamMetadata* metadata, void* client_data)
{
	KbFlacDecoder* flacDecoder = (KbFlacDecoder*)client_data;
	flacDecoder->metadata_callback(decoder, metadata);
}
/////////////////////////////////////////////////////////////////////////////
void KbFlacDecoder::error_callback(const FLAC__StreamDecoder* decoder, FLAC__StreamDecoderErrorStatus status, void* client_data)
{}
/////////////////////////////////////////////////////////////////////////////
void KbFlacDecoder::metadata_callback(const FLAC__StreamDecoder* decoder,
	const FLAC__StreamMetadata* metadata)
{
	if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
		memcpy(&m_stream_info, &metadata->data.stream_info, sizeof(m_stream_info));
	}
}
/////////////////////////////////////////////////////////////////////////////
FLAC__StreamDecoderWriteStatus KbFlacDecoder::write_callback(const FLAC__StreamDecoder* decoder,
	const FLAC__Frame* frame,
	const FLAC__int32* const buffer[])
{
	int wide_sample, sample = 0, channel;
	int channels = m_stream_info.channels;
	int wide_samples = frame->header.blocksize;
	int direct_copy_samples = 0;
	if (m_direct_buf && m_block_align > 0)
		direct_copy_samples = (int)(m_direct_buf_size / (DWORD)m_block_align);
	if (direct_copy_samples > wide_samples) {
		direct_copy_samples = wide_samples;
	}
	if (!m_direct_buf)
		direct_copy_samples = 0;
	if (m_stream_info.bits_per_sample == 16) {
		for (sample = 0, wide_sample = 0; wide_sample < direct_copy_samples; wide_sample++) {
			for (channel = 0; channel < channels; channel++, sample++) {
				((FLAC__int16*)m_direct_buf)[sample] = (FLAC__int16)buffer[channel][wide_sample];
			}
		}
		for (sample = 0; wide_sample < wide_samples; wide_sample++) {
			for (channel = 0; channel < channels; channel++, sample++) {
				((FLAC__int16*)m_temp_buf)[sample] = (FLAC__int16)buffer[channel][wide_sample];
			}
		}
	}
	else if (m_stream_info.bits_per_sample == 32) {
		for (sample = 0, wide_sample = 0; wide_sample < direct_copy_samples; wide_sample++) {
			for (channel = 0; channel < channels; channel++, sample++) {
				((FLAC__int32*)m_direct_buf)[sample] = (FLAC__int32)buffer[channel][wide_sample];
			}
		}
		for (sample = 0; wide_sample < wide_samples; wide_sample++) {
			for (channel = 0; channel < channels; channel++, sample++) {
				((FLAC__int32*)m_temp_buf)[sample] = (FLAC__int32)buffer[channel][wide_sample];
			}
		}

	}
	else if (m_stream_info.bits_per_sample == 24) {
		for (sample = 0, wide_sample = 0; wide_sample < direct_copy_samples; wide_sample++) {
			for (channel = 0; channel < channels; channel++, sample++) {
				BYTE* dst = &m_direct_buf[sample * 3];
				const BYTE* src = (const BYTE*)&buffer[channel][wide_sample];
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
			}
		}
		for (sample = 0; wide_sample < wide_samples; wide_sample++) {
			for (channel = 0; channel < channels; channel++, sample++) {
				BYTE* dst = &m_temp_buf[sample * 3];
				const BYTE* src = (const BYTE*)&buffer[channel][wide_sample];
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
			}
		}
	}
	else if (m_stream_info.bits_per_sample == 8) {
		for (sample = 0, wide_sample = 0; wide_sample < direct_copy_samples; wide_sample++) {
			for (channel = 0; channel < channels; channel++, sample++) {
				m_direct_buf[sample] = (FLAC__int8)buffer[channel][wide_sample] + 128;
			}
		}
		for (sample = 0; wide_sample < wide_samples; wide_sample++) {
			for (channel = 0; channel < channels; channel++, sample++) {
				m_temp_buf[sample] = (FLAC__int8)buffer[channel][wide_sample] + 128;
			}
		}
	}
	m_direct_buf_copied = direct_copy_samples * m_block_align;
	int temp_bytes = sample * m_block_align / (channels > 0 ? channels : 1);
	if (temp_bytes < 0)
		temp_bytes = 0;
	if ((DWORD)temp_bytes > sizeof(m_temp_buf))
		temp_bytes = (int)sizeof(m_temp_buf);
	m_temp_buf_remain = m_temp_buf_size = (DWORD)temp_bytes;
	return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}
/////////////////////////////////////////////////////////////////////////////
void KbFlacDecoder::error_callback(const FLAC__StreamDecoder* decoder,
	FLAC__StreamDecoderErrorStatus status)
{

}
int loopflac = 0;
int loopflac1 = 0;
/////////////////////////////////////////////////////////////////////////////
FLAC__StreamDecoderReadStatus KbFlacDecoder::read_callback(const FLAC__StreamDecoder* decoder,
	FLAC__byte buffer[],
	unsigned* bytes,
	void* client_data)
{
	HANDLE hFile = ((KbFlacDecoder*)client_data)->m_hFile;
	BYTE offenc[7] = { 0xd9,0x3F,0x86,0x7B,0xC7,0x61,0xaa };
	if (loop1 == 0) {
		loopflac = 0; loopflac1 = 0;
	}
	int offcont = 0;
	if (*bytes > 0) {
		DWORD dwRead = 0;
		loopflac1 = loopflac;
		int j = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
		if (!(::ReadFile(hFile, buffer, *bytes, &dwRead, NULL))) {
			return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		}
		else {
			offcont = j;
			offcont %= 7;
			*bytes = dwRead;
			if (((KbFlacDecoder*)client_data)->m_flacPosMode == 1 || flacmode == 1) {
				BYTE* b = (BYTE*)buffer;
				for (unsigned i = 0; i < *bytes; i++) {
					b[i] ^= offenc[offcont];
					offcont++; offcont %= 7;
				}
			}
			return dwRead ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
		}
	}
	else {
		return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
	}
}
/////////////////////////////////////////////////////////////////////////////
FLAC__StreamDecoderSeekStatus KbFlacDecoder::seek_callback(const FLAC__StreamDecoder* decoder,
	FLAC__uint64 absolute_byte_offset,
	void* client_data)
{
	HANDLE hFile = ((KbFlacDecoder*)client_data)->m_hFile;
	CFile file(hFile);
	LONGLONG offset = (LONGLONG)absolute_byte_offset;
	file.SeekToBegin();
	file.Seek(offset, CFile::begin);
	return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
}
/////////////////////////////////////////////////////////////////////////////
FLAC__StreamDecoderTellStatus KbFlacDecoder::tell_callback(const FLAC__StreamDecoder* decoder,
	FLAC__uint64* absolute_byte_offset,
	void* client_data)
{
	HANDLE hFile = ((KbFlacDecoder*)client_data)->m_hFile;
	LARGE_INTEGER* offset = (LARGE_INTEGER*)absolute_byte_offset;
	offset->HighPart = 0;
	offset->LowPart = SetFilePointer(hFile, 0, &offset->HighPart, FILE_CURRENT);
	if (offset->LowPart == 0xFFFFFFFF) {
		if (GetLastError() != NO_ERROR) {
			offset->QuadPart = 0;
			return FLAC__STREAM_DECODER_TELL_STATUS_ERROR;
		}
	}
	return FLAC__STREAM_DECODER_TELL_STATUS_OK;
}
/////////////////////////////////////////////////////////////////////////////
FLAC__StreamDecoderLengthStatus KbFlacDecoder::length_callback(const FLAC__StreamDecoder* decoder,
	FLAC__uint64* stream_length,
	void* client_data)
{
	HANDLE hFile = ((KbFlacDecoder*)client_data)->m_hFile;
	ULARGE_INTEGER* length = (ULARGE_INTEGER*)stream_length;
	length->LowPart = GetFileSize(hFile, &length->HighPart);
	if (length->LowPart == 0xFFFFFFFF) {
		if (GetLastError() != NO_ERROR) {
			length->QuadPart = 0;
			return FLAC__STREAM_DECODER_LENGTH_STATUS_ERROR;
		}
	}
	return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
}
/////////////////////////////////////////////////////////////////////////////
FLAC__bool KbFlacDecoder::eof_callback(const FLAC__StreamDecoder* decoder, void* client_data)
{
	HANDLE hFile = ((KbFlacDecoder*)client_data)->m_hFile;
	DWORD dwSizeHigh = 0;
	LONG  lPosHigh = 0;
	return GetFileSize(hFile, &dwSizeHigh) == SetFilePointer(hFile, 0, &lPosHigh, FILE_CURRENT) &&
		dwSizeHigh == lPosHigh;
}
/////////////////////////////////////////////////////////////////////////////

class flac
{
public:
	HKMP WINAPI Open(const _TCHAR* cszFileName, SOUNDINFO* pInfo)
	{
		KbFlacDecoder* flac = new KbFlacDecoder;
		if (flac->Open(cszFileName, pInfo)) {
			return flac;
		}
		delete flac;
		return NULL;
	}
	/////////////////////////////////////////////////////////////////////////////
	void WINAPI Close(HKMP hKMP)
	{
		if (hKMP) {
			KbFlacDecoder* flac = (KbFlacDecoder*)hKMP;
			delete flac;
		}
	}
	/////////////////////////////////////////////////////////////////////////////
	DWORD WINAPI Render(HKMP hKMP, BYTE* Buffer, DWORD dwSize)
	{
		if (!hKMP)return 0;
		KbFlacDecoder* flac = (KbFlacDecoder*)hKMP;
		return flac->Render(Buffer, dwSize);
	}
	/////////////////////////////////////////////////////////////////////////////
	DWORD WINAPI SetPosition(HKMP hKMP, LONGLONG dwPos)
	{
		if (!hKMP)return 0;
		KbFlacDecoder* flac = (KbFlacDecoder*)hKMP;
		return flac->SetPosition(dwPos);
	}
	/////////////////////////////////////////////////////////////////////////////
	BOOL WINAPI IsEndOfStream(HKMP hKMP)
	{
		if (!hKMP)return FALSE;
		KbFlacDecoder* flac = (KbFlacDecoder*)hKMP;
		return flac->IsEndOfStream();
	}
};