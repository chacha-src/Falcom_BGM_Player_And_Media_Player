#include "stdafx.h"
// flacmode グローバルは使わない（スロットの flacMode を使う）
#include "GlobalCommand.h"
#include "mp3info.h"
#include "mp3.h"
#include "wav.h"
#include <vorbis/vorbisfile.h>
#include "kmp_pi.h"
#include "PlaySlot.h"
#include "ProXfadeDual.h"
#include "ProAudio.h"
#include "SongHeardClock.h"
#include "AudioUpscaler.h"
#include "resource.h"
#include "PlayList.h"
#include "XfDebugLog.h"
#include <mmreg.h>
#include <mutex>
#include <math.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "dsound.lib")

extern LPDIRECTSOUND8 m_ds;
extern LPDIRECTSOUNDBUFFER8 m_dsb;
extern LPDIRECTSOUNDBUFFER m_dsb1;
extern save savedata;
extern int wavbit_sample_Hz, wavchannel, wavsam_depth;
extern int oggsize, loop1, loop2, endf;
extern __int64 playb;
extern int g_openDecoderMode;
extern ULONG g_ds_buffer_bytes;
extern int g_ds_pcm_rate, g_ds_pcm_ch, g_ds_pcm_bits;
extern int g_pcm_upscale_active;
extern int endflg;
extern int fade1;
extern int playf, plf;
extern CPlayList* pl;
extern int plcnt;
extern BYTE bufwav3[];
extern float fade;
extern __int64 g_dsWrittenBytes;
extern __int64 g_heardBytes;
extern __int64 g_endWrittenBytes;
extern std::mutex cl2;
void Ogg_PostXfadePromote();
void Ogg_FeedPianoRoll(const void* p, int n);
void equaliser(void* data, int len, BOOL reset);
void equaliserBank(int bank, void* data, int len, BOOL reset);
void equaliserBank(int bank, void* data, int len, BOOL reset, int bitsOverride, int chOverride, int rateOverride);
void equaliserResetBank(int bank);
void RubberBand_DestroyBank(int bank);
void RubberBand_DestroyAll();
bool ProcessAudioWithRubberBandBank(int bank, float tempoRate, bool t,
	const uint8_t* inData, int inBytes, int bits, int ch, int rate,
	std::vector<float>& outFloat);
void ConvertFloatToRawBytes(const std::vector<float>& float_data,
	uint16_t target_bits_per_sample, uint16_t channels,
	std::vector<uint8_t>& out_raw_data);
extern int tempo;
void PlaybackCcWrite(const void* p, UINT n);
void PlaybackCcWriteForced(const void* p, UINT n);
void PlaybackCcWriteFromFormat(const void* p, UINT n, int srcRate, int srcCh, int srcBits, bool forced);
HKMP PlaySlot_FlacOpen(LPCTSTR path, SOUNDINFO* si);
void PlaySlot_FlacClose(HKMP h);
DWORD PlaySlot_FlacRender(HKMP h, BYTE* buf, DWORD n);
DWORD PlaySlot_FlacSetPosition(HKMP h, LONGLONG pos);
HKMP PlaySlot_M4aOpen(LPCTSTR path, SOUNDINFO* si);
void PlaySlot_M4aClose(HKMP h);
DWORD PlaySlot_M4aRender(HKMP h, BYTE* buf, DWORD n);
DWORD PlaySlot_M4aSetPosition(HKMP h, DWORD pos);

struct PlaySlotDecoders {
	mp3 mp3Dec;
	wav wavDec;
	OggVorbis_File vf;
	int vfOpened;
	HKMP kmp;
	SOUNDINFO si;
	AudioUpscaler up;
	std::vector<uint8_t> rbRing;
	size_t rbRingRead;
	bool rbEofFlushed;
	BYTE pcmScratch[64 * 1024];
	BYTE pcmScratch2[64 * 1024];
	PlaySlotDecoders() : vfOpened(0), kmp(NULL), rbRingRead(0), rbEofFlushed(false) {
		ZeroMemory(&vf, sizeof(vf));
		ZeroMemory(&si, sizeof(si));
	}
};

PlaySlot g_playSlots[PLAY_SLOT_COUNT];
volatile LONG g_activeSlot = 0;
volatile LONG g_stoppingSlot = -1;
volatile LONG g_slotDualEnabled = 0;
// handoff 直後に equaliser 状態をリセット（前曲のフィルタ残りを捨てる）
volatile LONG g_eqResetNext = 0;
/* Seek 直後は DS キュー差し引きでバーが旧位置/0 に戻るのを抑止 */
volatile LONG g_seekUiFreshTick = 0;

static PlaySlotDecoders* SlotDec(PlaySlot& s)
{
	if (!s.dec) s.dec = new PlaySlotDecoders();
	return s.dec;
}

void PlaySlot_InitAll()
{
	for (int i = 0; i < PLAY_SLOT_COUNT; ++i) {
		g_playSlots[i].idx = i;
		SlotDec(g_playSlots[i]);
		PlaySlot_Reset(i);
	}
	g_activeSlot = 0;
	g_stoppingSlot = -1;
	g_slotDualEnabled = 0;
}

void PlaySlot_Reset(int slot)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return;
	PlaySlot& s = g_playSlots[slot];
	s.stopReq = 0;
	s.running = 0;
	s.endflg = 0;
	s.seekParkReq = 0;
	s.seekParked = 0;
	s.seekDoReq = 0;
	s.seekDone = 0;
	s.seekToFrames = 0;
	s.oldw = 0;
	s.dsWritten = 0;
	s.heard = 0;
	s.playb = 0;
	s.openMode = INT_MIN;
	s.pcmExtraPos = 0;
	s.oggsize = 0;
	s.flacMode = 0;
	s.rate = 44100;
	s.ch = 2;
	s.bits = 16;
	s.outRate = 44100;
	s.outCh = 2;
	s.outBits = 16;
	s.ringBytes = 0;
}

bool PlaySlot_CloseDecoder(int slot)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return false;
	PlaySlot& s = g_playSlots[slot];
	PlaySlotDecoders* d = SlotDec(s);
	const int m = s.openMode;
	if (m == -10) {
		d->mp3Dec.Close();
	}
	else if (m == 999) {
		d->wavDec.Close();
	}
	else if (m == -1 && d->vfOpened) {
		ov_clear(&d->vf);
		d->vfOpened = 0;
	}
	else if (m == -8 && d->kmp) {
		PlaySlot_FlacClose(d->kmp);
		d->kmp = NULL;
	}
	else if (m == -9 && d->kmp) {
		PlaySlot_M4aClose(d->kmp);
		d->kmp = NULL;
	}
	if (s.pcmExtra) {
		free(s.pcmExtra);
		s.pcmExtra = NULL;
		s.pcmExtraBytes = 0;
		s.pcmExtraPos = 0;
	}
	s.openMode = INT_MIN;
	s.path[0] = 0;
	return true;
}

bool PlaySlot_ReleaseBuffer(int slot)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return false;
	PlaySlot& s = g_playSlots[slot];
	if (s.dsb) {
		s.dsb->Stop();
		s.dsb->Release();
		s.dsb = NULL;
	}
	if (s.dsb1) {
		s.dsb1->Release();
		s.dsb1 = NULL;
	}
	return true;
}

static size_t SlotOvRead(void* ptr, size_t size, size_t nmemb, void* datasource)
{
	FILE* f = (FILE*)datasource;
	return fread(ptr, size, nmemb, f);
}
static int SlotOvSeek(void* datasource, ogg_int64_t offset, int whence)
{
	FILE* f = (FILE*)datasource;
	return _fseeki64(f, offset, whence);
}
static int SlotOvClose(void* datasource)
{
	FILE* f = (FILE*)datasource;
	return fclose(f);
}
static long SlotOvTell(void* datasource)
{
	FILE* f = (FILE*)datasource;
	return (long)_ftelli64(f);
}

bool PlaySlot_OpenFile(int slot, LPCTSTR path, int mode)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT || !path || !path[0]) return false;
	if (!ProXfade_IsSupportedMode(mode)) return false;
	PlaySlot_StopFeed(slot, 3000);
	PlaySlot_CloseDecoder(slot);
	PlaySlot& s = g_playSlots[slot];
	PlaySlotDecoders* d = SlotDec(s);
	_tcsncpy(s.path, path, 1023);
	s.path[1023] = 0;
	ZeroMemory(&d->si, sizeof(d->si));
	d->si.dwSamplesPerSec = savedata.samples ? savedata.samples : 44100;
	d->si.dwChannels = 2;
	d->si.dwBitsPerSample = savedata.bit32 ? 32 : (savedata.bit24 ? 24 : 16);
	d->si.dwSeekable = 1;
	s.endflg = 0;
	s.playb = 0;

	if (mode == -10) {
		d->mp3Dec.mp3init();
		d->si.dwChannels = 2;
		if (!d->mp3Dec.Open(s.path, &d->si)) return false;
		s.openMode = -10;
		s.rate = (int)d->si.dwSamplesPerSec;
		s.ch = (int)d->si.dwChannels;
		s.bits = (int)d->si.dwBitsPerSample;
		if (!(s.bits==16||s.bits==24||s.bits==32)) s.bits = 16;
		if (d->mp3Dec.m_mp3info.freq > 0)
			s.rate = d->mp3Dec.m_mp3info.freq;
		s.oggsize = d->mp3Dec.m_mp3info.total_samples;
		if (s.oggsize <= 0 && s.rate > 0)
			s.oggsize = s.rate * 60; // 不明時の仮
		return true;
	}
	if (mode == 999) {
		wavinfo wi;
		ZeroMemory(&wi, sizeof(wi));
		if (!d->wavDec.Open(s.path, &wi)) return false;
		s.openMode = 999;
		s.rate = (int)wi.nSamplesPerSec;
		s.ch = (int)wi.nChannels;
		s.bits = (int)wi.wBitsPerSample;
		if (!(s.bits==16||s.bits==24||s.bits==32)) s.bits = (s.bits<=16)?16:24;
		s.oggsize = (int)wi.totalSamples;
		return true;
	}
	if (mode == -8) {
		d->si.dwChannels = 8;
		d->si.dwLength = (DWORD)-1;
		// レガシー Open と同様: 先頭 0xBF なら flacMode=1（Seek 単位が変わる）
		// ※グローバル flacmode は触らない（曲1再生中と衝突＝ギギギ／シーク壊れ）
		s.flacMode = 0;
		{
			FILE* hf = NULL;
#if UNICODE
			hf = _tfopen(s.path, _T("rb"));
#else
			hf = fopen(s.path, "rb");
#endif
			if (hf) {
				unsigned char a = 0;
				if (fread(&a, 1, 1, hf) == 1 && a == 0xBF)
					s.flacMode = 1;
				fclose(hf);
			}
		}
#if UNICODE
		d->kmp = PlaySlot_FlacOpen(s.path, &d->si);
#else
		d->kmp = PlaySlot_FlacOpen(s.path, &d->si);
#endif
		if (!d->kmp) return false;
		s.openMode = -8;
		s.rate = (int)d->si.dwSamplesPerSec;
		s.ch = (int)d->si.dwChannels;
		if (s.ch < 1) s.ch = 2;
		if (s.ch > 8) s.ch = 8;
		s.bits = (int)d->si.dwBitsPerSample;
		if (!(s.bits==16||s.bits==24||s.bits==32)) s.bits = 16;
		if (d->si.dwLength != (DWORD)-1 && s.rate > 0)
			s.oggsize = (int)((double)(DWORD)d->si.dwLength * (double)s.rate / 1000.0 + 0.5);
		else
			s.oggsize = s.rate * 60;
		PlaySlot_FlacSetPosition(d->kmp, 0);
		return true;
	}
	if (mode == -9) {
		d->si.dwChannels = 8;
		d->si.dwLength = (DWORD)-1;
		d->kmp = PlaySlot_M4aOpen(s.path, &d->si);
		if (!d->kmp) return false;
		s.openMode = -9;
		s.rate = (int)d->si.dwSamplesPerSec;
		s.ch = (int)d->si.dwChannels;
		if (s.ch < 1) s.ch = 2;
		if (s.ch > 8) s.ch = 8;
		s.bits = (int)d->si.dwBitsPerSample;
		if (!(s.bits==16||s.bits==24||s.bits==32)) s.bits = 16;
		if (d->si.dwLength != (DWORD)-1 && s.rate > 0)
			s.oggsize = (int)((double)(DWORD)d->si.dwLength * (double)s.rate / 1000.0 + 0.5);
		else
			s.oggsize = s.rate * 60;
		PlaySlot_M4aSetPosition(d->kmp, 0);
		return true;
	}
	if (mode == -1) {
		FILE* f = NULL;
#if UNICODE
		f = _tfopen(s.path, _T("rb"));
#else
		f = fopen(s.path, "rb");
#endif
		if (!f) return false;
		ov_callbacks cb;
		cb.read_func = SlotOvRead;
		cb.seek_func = SlotOvSeek;
		cb.close_func = SlotOvClose;
		cb.tell_func = SlotOvTell;
		ZeroMemory(&d->vf, sizeof(d->vf));
		if (ov_open_callbacks(f, &d->vf, NULL, 0, cb) < 0) {
			fclose(f);
			return false;
		}
		d->vfOpened = 1;
		vorbis_info* vi = ov_info(&d->vf, -1);
		if (!vi) {
			ov_clear(&d->vf);
			d->vfOpened = 0;
			return false;
		}
		s.openMode = -1;
		s.rate = (int)vi->rate;
		s.ch = (int)vi->channels;
		if (s.ch > 2) s.ch = 2;
		s.bits = 16;
		ogg_int64_t total = ov_pcm_total(&d->vf, -1);
		s.oggsize = (total > 0 && total < 0x7fffffff) ? (int)total : (s.rate * 60);
		return true;
	}
	if (mode == -6) {
		// Opus はスロット本デコード未配線。失敗→レガシー timer9000 経路へ。
		return false;
	}
	return false;
}

bool PlaySlot_CreateBuffer(int slot, LPDIRECTSOUND8 ds, ULONG bytes)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT || !ds) return false;
	PlaySlot& s = g_playSlots[slot];
	PlaySlot_ReleaseBuffer(slot);

	// クロス中は曲1の出力形式に合わせる（ネイティヴで別レートのバッファを作ると半速/無音ループになる）
	s.outRate = (g_ds_pcm_rate >= 8000) ? g_ds_pcm_rate : ((s.rate > 0) ? s.rate : 44100);
	s.outCh = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : ((s.ch > 0) ? s.ch : 2);
	s.outBits = (g_ds_pcm_bits == 16 || g_ds_pcm_bits == 24 || g_ds_pcm_bits == 32)
		? g_ds_pcm_bits : ((s.bits == 16 || s.bits == 24 || s.bits == 32) ? s.bits : 16);
	if (s.outCh > 8) s.outCh = 8;

	WAVEFORMATEX wfx;
	ZeroMemory(&wfx, sizeof(wfx));
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = (WORD)s.outCh;
	wfx.nSamplesPerSec = (DWORD)s.outRate;
	wfx.wBitsPerSample = (WORD)s.outBits;
	wfx.nBlockAlign = (WORD)(wfx.nChannels * wfx.wBitsPerSample / 8);
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	if (bytes < 4096)
		bytes = (g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : (ULONG)(wfx.nAvgBytesPerSec / 2);
	if (g_ds_buffer_bytes > bytes)
		bytes = g_ds_buffer_bytes;
	bytes = (bytes / wfx.nBlockAlign) * wfx.nBlockAlign;
	if (bytes < (ULONG)wfx.nBlockAlign * 256)
		bytes = (ULONG)wfx.nBlockAlign * 256;

	DSBUFFERDESC dsbd;
	ZeroMemory(&dsbd, sizeof(dsbd));
	dsbd.dwSize = sizeof(dsbd);
	dsbd.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS
		| DSBCAPS_LOCSOFTWARE | DSBCAPS_CTRLPOSITIONNOTIFY;
	dsbd.dwBufferBytes = bytes;
	dsbd.lpwfxFormat = &wfx;

	HRESULT hr = ds->CreateSoundBuffer(&dsbd, &s.dsb1, NULL);
	if (FAILED(hr) || !s.dsb1) return false;
	hr = s.dsb1->QueryInterface(IID_IDirectSoundBuffer8, (void**)&s.dsb);
	if (FAILED(hr) || !s.dsb) {
		s.dsb1->Release();
		s.dsb1 = NULL;
		return false;
	}
	LPVOID p1 = NULL, p2 = NULL;
	DWORD l1 = 0, l2 = 0;
	if (SUCCEEDED(s.dsb->Lock(0, bytes, &p1, &l1, &p2, &l2, 0))) {
		if (p1 && l1) ZeroMemory(p1, l1);
		if (p2 && l2) ZeroMemory(p2, l2);
		s.dsb->Unlock(p1, l1, p2, l2);
	}
	s.dsb->SetCurrentPosition(0);
	s.dsb->SetVolume(DSBVOLUME_MIN);
	s.oldw = 0;
	s.ringBytes = bytes;
	s.dsWritten = 0;
	s.heard = 0;

	PlaySlotDecoders* d = SlotDec(s);
	d->up.Configure(s.rate, s.ch, s.bits, s.outRate, s.outCh, s.outBits);
	d->up.Reset();
	return true;
}

static int SlotDecode(PlaySlot& s, BYTE* dst, int wantBytes)
{
	if (wantBytes <= 0 || !dst) return 0;
	PlaySlotDecoders* d = SlotDec(s);
	int got = 0;
	if (s.openMode == -10) {
		BYTE tmp[8192];
		while (got < wantBytes) {
			int chunk = wantBytes - got;
			if (chunk > (int)sizeof(tmp)) chunk = (int)sizeof(tmp);
			DWORD r = 0;
			if (savedata.mp3orig)
				r = (DWORD)d->mp3Dec.Render2(tmp, chunk, 0);
			else
				r = (DWORD)d->mp3Dec.Render(tmp, (DWORD)chunk);
			if (r == 0) break;
			memcpy(dst + got, tmp, r);
			got += (int)r;
		}
	}
	else if (s.openMode == 999) {
		got = d->wavDec.Render(dst, wantBytes);
	}
	else if (s.openMode == -8 && d->kmp) {
		got = (int)PlaySlot_FlacRender(d->kmp, dst, (DWORD)wantBytes);
	}
	else if (s.openMode == -9 && d->kmp) {
		got = (int)PlaySlot_M4aRender(d->kmp, dst, (DWORD)wantBytes);
	}
	else if (s.openMode == -1 && d->vfOpened) {
		int bitstream = 0;
		while (got < wantBytes) {
			long r = ov_read(&d->vf, (char*)dst + got, wantBytes - got, 0, 2, 1, &bitstream);
			if (r <= 0) break;
			got += (int)r;
		}
	}
	else if (s.openMode == -6 && s.pcmExtra && s.pcmExtraPos < s.pcmExtraBytes) {
		int n = s.pcmExtraBytes - s.pcmExtraPos;
		if (n > wantBytes) n = wantBytes;
		memcpy(dst, s.pcmExtra + s.pcmExtraPos, (size_t)n);
		s.pcmExtraPos += n;
		got = n;
	}
	// playb はソース PCM フレーム（出力バイトで進めるとアップスケール時にシーク/曲末が壊れる）
	if (got > 0) {
		const int srcBpf = (s.ch > 0 ? s.ch : 2)
			* (((s.bits == 24 || s.bits == 32) ? s.bits : 16) / 8);
		if (srcBpf > 0)
			s.playb += got / srcBpf;
	}
	return got;
}

// B → A Mix 用の消費型リング（上書きメールボックスだと A 周期で B が巻き戻りギギギになる）
static const int kMixRingCap = 512 * 1024;
static BYTE g_mixRing[kMixRingCap];
static int g_mixW = 0;
static int g_mixR = 0;
static int g_mixAvail = 0;
static std::mutex g_incomingMixMu;

void PlaySlot_StoreIncomingS16(const BYTE* p, int bytes)
{
	if (!p || bytes <= 0) return;
	std::lock_guard<std::mutex> lk(g_incomingMixMu);
	int left = bytes;
	int off = 0;
	while (left > 0) {
		if (g_mixAvail >= kMixRingCap) {
			/* 溢れ: 最古を捨てて進める */
			int drop = left;
			if (drop > g_mixAvail) drop = g_mixAvail;
			g_mixR = (g_mixR + drop) % kMixRingCap;
			g_mixAvail -= drop;
		}
		int space = kMixRingCap - g_mixAvail;
		int chunk = left;
		if (chunk > space) chunk = space;
		int first = kMixRingCap - g_mixW;
		if (first > chunk) first = chunk;
		memcpy(g_mixRing + g_mixW, p + off, (size_t)first);
		if (chunk > first)
			memcpy(g_mixRing, p + off + first, (size_t)(chunk - first));
		g_mixW = (g_mixW + chunk) % kMixRingCap;
		g_mixAvail += chunk;
		off += chunk;
		left -= chunk;
	}
}

void PlaySlot_ClearIncomingMix()
{
	std::lock_guard<std::mutex> lk(g_incomingMixMu);
	g_mixW = g_mixR = g_mixAvail = 0;
}

int PlaySlot_MixRingBytes()
{
	std::lock_guard<std::mutex> lk(g_incomingMixMu);
	return g_mixAvail;
}

bool PlaySlot_MixIncomingEx(BYTE* inout, int bytes, int bits, int ch, int* outMixed)
{
	/* クロスフェード撤去: Mix しない */
	(void)inout; (void)bytes; (void)bits; (void)ch;
	if (outMixed) *outMixed = 0;
	return false;
}

bool PlaySlot_MixIncomingS16(BYTE* inout, int bytes)
{
	return PlaySlot_MixIncomingEx(inout, bytes,
		(g_ds_pcm_bits == 24 || g_ds_pcm_bits == 32) ? g_ds_pcm_bits : 16,
		(g_ds_pcm_ch > 0) ? g_ds_pcm_ch : 2, nullptr);
}

static int SlotOutBpf(const PlaySlot& s)
{
	const int ch = (s.outCh > 0) ? s.outCh : 2;
	const int bits = (s.outBits == 24 || s.outBits == 32 || s.outBits == 16) ? s.outBits : 16;
	return ch * (bits / 8);
}

static void SlotWriteBufwav3(ULONG oldw, ULONG ring, const BYTE* linear, int nBytes)
{
	/* linear は連続 nBytes。リング折り返しはここで分割する（呼び出し側の古い len1 を信じない） */
	if (!linear || ring == 0 || nBytes <= 0) return;
	if ((ULONG)nBytes > ring) nBytes = (int)ring;
	const ULONG space = ring - (oldw % ring);
	int len1 = nBytes;
	int len2 = 0;
	if ((ULONG)nBytes > space) {
		len1 = (int)space;
		len2 = nBytes - len1;
	}
	if (len1 > 0)
		memcpy(bufwav3 + (oldw % ring), linear, (size_t)len1);
	if (len2 > 0)
		memcpy(bufwav3, linear + len1, (size_t)len2);
}

static void SlotApplyFadeOutFormat(BYTE* pcm, int bytes, int bits, float fadeGain)
{
	if (!pcm || bytes <= 0 || fadeGain >= 0.999f) return;
	const float g = fadeGain * fadeGain;
	if (bits == 16) {
		short* sh = (short*)pcm;
		const int n = bytes / 2;
		for (int i = 0; i < n; ++i) {
			float v = (float)sh[i] * g;
			if (v > 32767.f) v = 32767.f;
			if (v < -32768.f) v = -32768.f;
			sh[i] = (short)v;
		}
	}
	else if (bits == 24) {
		for (int i = 0; i + 3 <= bytes; i += 3) {
			int v = (int)(pcm[i] | (pcm[i+1] << 8) | (pcm[i+2] << 16));
			if (v & 0x800000) v |= ~0xffffff;
			float f = (float)v * g;
			int o = (int)f;
			if (o > 8388607) o = 8388607;
			if (o < -8388608) o = -8388608;
			pcm[i] = (BYTE)(o & 0xff);
			pcm[i+1] = (BYTE)((o >> 8) & 0xff);
			pcm[i+2] = (BYTE)((o >> 16) & 0xff);
		}
	}
	else if (bits == 32) {
		int* iv = (int*)pcm;
		const int n = bytes / 4;
		for (int i = 0; i < n; ++i) {
			double v = (double)iv[i] * (double)g;
			if (v > 2147483647.0) v = 2147483647.0;
			if (v < -2147483648.0) v = -2147483648.0;
			iv[i] = (int)v;
		}
	}
}

static float SlotTempoRatio()
{
	float te = (float)tempo;
	if (te >= 200.0f) te -= 100.0f;
	else te = te / 3.0f + 33.3f;
	return 100.0f / te;
}

static void SlotRbClear(PlaySlot& s)
{
	PlaySlotDecoders* d = SlotDec(s);
	d->rbRing.clear();
	d->rbRingRead = 0;
	d->rbEofFlushed = false;
	RubberBand_DestroyBank(s.idx);
}

static bool SlotRbPushDecoded(PlaySlot& s, const BYTE* pcm, int bytes, bool finalFlush)
{
	PlaySlotDecoders* d = SlotDec(s);
	const int bits = (s.outBits == 24 || s.outBits == 32) ? s.outBits : 16;
	const int ch = (s.outCh > 0) ? s.outCh : 2;
	const int rate = (s.outRate > 0) ? s.outRate : 44100;
	std::vector<float> outFloat;
	if (!ProcessAudioWithRubberBandBank(s.idx, SlotTempoRatio(), finalFlush,
		pcm, bytes, bits, ch, rate, outFloat)) {
		return false;
	}
	if (outFloat.empty()) return true;
	std::vector<uint8_t> raw;
	ConvertFloatToRawBytes(outFloat, (uint16_t)bits, (uint16_t)ch, raw);
	if (raw.empty()) return true;
	if (d->rbRingRead > 0) {
		d->rbRing.erase(d->rbRing.begin(), d->rbRing.begin() + (std::ptrdiff_t)d->rbRingRead);
		d->rbRingRead = 0;
	}
	d->rbRing.insert(d->rbRing.end(), raw.begin(), raw.end());
	return true;
}

static bool SlotFillDecodedNoRb(PlaySlot& s, BYTE* dst, int wantOutBytes)
{
	PlaySlotDecoders* d = SlotDec(s);
	ZeroMemory(dst, wantOutBytes);
	if (!d->up.IsActive()) {
		int got = SlotDecode(s, dst, wantOutBytes);
		if (got < wantOutBytes)
			ZeroMemory(dst + got, wantOutBytes - got);
		return got > 0;
	}
	int filled = 0;
	BYTE* src = d->pcmScratch2;
	const int srcCap = (int)sizeof(d->pcmScratch2);
	int guard = 0;
	while (filled < wantOutBytes && guard < 64) {
		++guard;
		int need = d->up.SuggestInputBytes(wantOutBytes - filled);
		if (need < 2048) need = 8192;
		if (need > srcCap) need = srcCap;
		const int srcBpf = (s.ch > 0 ? s.ch : 2) * (((s.bits == 24 || s.bits == 32) ? s.bits : 16) / 8);
		if (srcBpf > 0) need -= (need % srcBpf);
		if (need <= 0) break;
		ZeroMemory(src, need);
		int got = SlotDecode(s, src, need);
		if (got > 0)
			d->up.PushInterleaved(src, got);
		int pulled = d->up.PullInterleaved(dst + filled, wantOutBytes - filled);
		if (pulled > 0)
			filled += pulled;
		if (got <= 0 && pulled <= 0)
			break;
	}
	return filled > 0;
}

static bool SlotFillOutPcm(PlaySlot& s, BYTE* dst, int wantOutBytes)
{
	if (wantOutBytes <= 0 || !dst) return false;
	PlaySlotDecoders* d = SlotDec(s);
	const int bpf = SlotOutBpf(s);
	ZeroMemory(dst, wantOutBytes);

	BYTE* mid = d->pcmScratch;
	const int midCap = (int)sizeof(d->pcmScratch);
	int guard = 0;
	while ((int)(d->rbRing.size() - d->rbRingRead) < wantOutBytes && guard < 96) {
		++guard;
		int chunk = bpf > 0 ? (bpf * ((s.outRate > 0) ? s.outRate : 44100) * 20 / 1000) : 4096;
		if (chunk > midCap) chunk = midCap;
		if (bpf > 0) chunk -= (chunk % bpf);
		if (chunk < bpf) chunk = bpf;
		const bool got = SlotFillDecodedNoRb(s, mid, chunk);
		if (got) {
			if (!SlotRbPushDecoded(s, mid, chunk, false))
				break;
		}
		else {
			if (!d->rbEofFlushed) {
				SlotRbPushDecoded(s, NULL, 0, true);
				d->rbEofFlushed = true;
			}
			break;
		}
	}

	const int have = (int)(d->rbRing.size() - d->rbRingRead);
	const int take = (have < wantOutBytes) ? have : wantOutBytes;
	if (take <= 0) return false;
	memcpy(dst, d->rbRing.data() + d->rbRingRead, (size_t)take);
	d->rbRingRead += (size_t)take;
	if (take < wantOutBytes)
		ZeroMemory(dst + take, wantOutBytes - take);
	return true;
}

void PlaySlot_ZeroRing(int slot)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return;
	PlaySlot& s = g_playSlots[slot];
	if (!s.dsb) return;
	const ULONG bytes = (s.ringBytes > 0) ? s.ringBytes
		: ((g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : 0);
	if (bytes < 4) return;
	LPVOID p1 = NULL, p2 = NULL;
	DWORD l1 = 0, l2 = 0;
	if (FAILED(s.dsb->Lock(0, bytes, &p1, &l1, &p2, &l2, 0)))
		return;
	if (p1 && l1) ZeroMemory(p1, l1);
	if (p2 && l2) ZeroMemory(p2, l2);
	s.dsb->Unlock(p1, l1, p2, l2);
}

// フィード停止中専用。再生スレッドと並行に呼ぶな（Lock/デコーダ競合で落ちる）。
static bool SlotDoSeekCore(PlaySlot& s, __int64 playbFrames)
{
	if (s.openMode == INT_MIN) return false;
	if (!s.dsb) return false;
	if (playbFrames < 0) playbFrames = 0;
	if (s.oggsize > 0 && playbFrames > (__int64)s.oggsize)
		playbFrames = (__int64)s.oggsize;

	s.dsb->Stop();

	PlaySlotDecoders* d = SlotDec(s);
	const int m = s.openMode;
	const int rate = (s.rate > 0) ? s.rate : 44100;
	const int ch = (s.ch > 0) ? s.ch : 2;
	if (m == -10) {
		DWORD dw = (ch == 2) ? (DWORD)playbFrames : (DWORD)(playbFrames * 4);
		if (savedata.mp3orig)
			d->mp3Dec.seek2(dw, s.ch);
		else
			d->mp3Dec.seek(dw, s.ch);
	}
	else if (m == 999) {
		d->wavDec.Seek(playbFrames);
	}
	else if (m == -8 && d->kmp) {
		/* SetPosition はデコーダインスタンスの mode（Open 時 0xBF）を使う。グローバル flacmode 不要 */
		if (s.flacMode == 1) {
			PlaySlot_FlacSetPosition(d->kmp, playbFrames);
		}
		else {
			LONGLONG pos = (LONGLONG)((double)playbFrames / (((double)rate * (double)ch) / 2000.0));
			if (pos < 0) pos = 0;
			PlaySlot_FlacSetPosition(d->kmp, pos);
		}
	}
	else if (m == -9 && d->kmp) {
		double wavv2[] = { 0, 2.0, 1.0, 1.0 / 2.0, 1.0 / 2.0, 1.0 / 2.0, 1.0 / 2.0 };
		int chIdx = ch;
		if (chIdx < 1) chIdx = 1;
		if (chIdx > 6) chIdx = 6;
		const double wb = (double)rate;
		DWORD pla = (DWORD)((double)playbFrames / (((double)(wb / wavv2[chIdx])) / ((ch > 2) ? (1069.1 * ch) : 1000.0)));
		pla = (pla / (DWORD)(ch * 2) * (DWORD)(ch * 2));
		PlaySlot_M4aSetPosition(d->kmp, pla);
	}
	else if (m == -1 && d->vfOpened) {
		ov_pcm_seek(&d->vf, playbFrames);
	}
	else {
		s.dsb->Play(0, 0, DSBPLAY_LOOPING);
		return false;
	}

	d->up.Reset();
	PlaySlot_ClearIncomingMix();
	SlotRbClear(s);
	s.playb = playbFrames;
	s.dsWritten = 0;
	s.heard = 0;
	s.oldw = 0;
	s.endflg = 0;
	s.pcmExtraPos = 0;
	InterlockedExchange(&s.seekDoReq, 0);
	InterlockedExchange(&s.seekDone, 0);

	const ULONG bytes = (s.ringBytes > 0) ? s.ringBytes
		: ((g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : 153600);
	LPVOID p1 = NULL, p2 = NULL;
	DWORD l1 = 0, l2 = 0;
	if (bytes > 0 && SUCCEEDED(s.dsb->Lock(0, bytes, &p1, &l1, &p2, &l2, 0))) {
		if (p1 && l1) ZeroMemory(p1, l1);
		if (p2 && l2) ZeroMemory(p2, l2);
		s.dsb->Unlock(p1, l1, p2, l2);
	}
	s.dsb->SetCurrentPosition(0);
	/* WriteCursor は SetCurrentPosition でも戻らない。oldw=wc だと play=0〜wc が無音のまま流れる */
	s.oldw = 0;
	s.dsWritten = 0;
	s.heard = 0;

	LONG v = (savedata.dsvol - 1) * 10;
	if (savedata.dsvol == -498) v = (savedata.dsvol - 1) * 7;
	if (v > 0) v = 0;
	if (v < DSBVOLUME_MIN) v = DSBVOLUME_MIN;
	s.dsb->SetVolume(v);
	if (FAILED(s.dsb->Play(0, 0, DSBPLAY_LOOPING)))
		return false;
	return true;
}

UINT PlaySlot_FeedThread(LPVOID p)
{
	const int slot = (int)(INT_PTR)p;
	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return 0;
	PlaySlot& s = g_playSlots[slot];
	InterlockedExchange(&s.running, 1);
	InterlockedExchange(&s.stopReq, 0);

	BYTE stage[64 * 1024];

	while (InterlockedCompareExchange(&s.stopReq, 0, 0) == 0) {
		if (!s.dsb) break;

		// 旧 in-thread シーク要求は破棄（PlaySlot_Seek は StopFeed 排他に統一）
		if (InterlockedCompareExchange(&s.seekDoReq, 0, 0) != 0) {
			InterlockedExchange(&s.seekDoReq, 0);
			InterlockedExchange(&s.seekDone, 1);
			Sleep(1);
			continue;
		}

		const ULONG ring = (s.ringBytes > 0) ? s.ringBytes
			: ((g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : 153600);
		const int bpf = SlotOutBpf(s);
		const bool isActiveEarly = (InterlockedCompareExchange(&g_activeSlot, 0, 0) == slot);
		const int phEarly = ProXfade_Phase();
		/* 昇格要求後は A への追記を止める */
		if (isActiveEarly && (phEarly == PRO_XF_PROMOTE
			|| InterlockedCompareExchange(&g_xfadeNoWrite, 0, 0) != 0)) {
			Sleep((DWORD)(savedata.ms > 0 ? savedata.ms : 10));
			continue;
		}
		/* 曲2クロス待ち: Mix リングへ供給 + DS を同じ PCM で温める（Play は昇格時） */
		if (!isActiveEarly && bpf > 0 && s.outRate > 0 && s.dsb) {
			const int bSlot = (int)InterlockedCompareExchange(&g_xfadeBSlot, 0, 0);
			const bool xfadeB = (bSlot == slot);
			const bool lockstep = (phEarly == PRO_XF_OVERLAP || phEarly == PRO_XF_PROMOTE)
				|| (phEarly == PRO_XF_READY && xfadeB);
			if (lockstep) {
			auto writeLockstep = [&](BYTE* pcm, int chunk) {
				if (chunk < bpf) return;
				equaliserBank(slot, pcm, chunk, FALSE, s.outBits, s.outCh, s.outRate > 0 ? s.outRate : 0);
				PlaySlot_StoreIncomingS16(pcm, chunk);
				LPVOID p1 = NULL, p2 = NULL;
				DWORD l1 = 0, l2 = 0;
				if (SUCCEEDED(s.dsb->Lock(s.oldw, (DWORD)chunk, &p1, &l1, &p2, &l2, 0))) {
					int off = 0;
					if (p1 && l1) { memcpy(p1, pcm + off, l1); off += (int)l1; }
					if (p2 && l2 && off < chunk) memcpy(p2, pcm + off, l2);
					s.dsb->Unlock(p1, l1, p2, l2);
				}
				s.oldw = (s.oldw + (ULONG)chunk) % ring;
				s.dsWritten += chunk;
			};
			const int wantMs = (phEarly == PRO_XF_OVERLAP || phEarly == PRO_XF_PROMOTE) ? 150 : 80;
			const int want = bpf * s.outRate * wantMs / 1000;
			if (want >= bpf && PlaySlot_MixRingBytes() >= want) {
				s.dsb->SetVolume(DSBVOLUME_MIN);
				DWORD st = 0;
				if (SUCCEEDED(s.dsb->GetStatus(&st)) && (st & DSBSTATUS_PLAYING))
					s.dsb->Stop();
				Sleep((DWORD)(savedata.ms > 0 ? savedata.ms : 10));
				continue;
			}
			int chunk = bpf * s.outRate * 20 / 1000;
			if (chunk > (int)sizeof(stage)) chunk = (int)sizeof(stage);
			if (bpf > 0) chunk -= (chunk % bpf);
			if (chunk >= bpf) {
				if (!SlotFillOutPcm(s, stage, chunk)) {
					InterlockedExchange(&s.endflg, 1);
					ZeroMemory(stage, (size_t)chunk);
				}
				writeLockstep(stage, chunk);
			}
			s.dsb->SetVolume(DSBVOLUME_MIN);
			{
				DWORD st = 0;
				if (SUCCEEDED(s.dsb->GetStatus(&st)) && (st & DSBSTATUS_PLAYING))
					s.dsb->Stop();
			}
			Sleep((DWORD)(savedata.ms > 0 ? savedata.ms : 5));
			continue;
			}
		}

		ULONG pc = 0, wc = 0;
		if (FAILED(s.dsb->GetCurrentPosition(&pc, &wc))) {
			Sleep(5);
			continue;
		}

		const bool isActive = (InterlockedCompareExchange(&g_activeSlot, 0, 0) == slot);
		const int phNow = ProXfade_Phase();
		const bool inXf = (phNow == PRO_XF_OVERLAP || phNow == PRO_XF_PROMOTE);

		/* OVERLAP も通常と同じ WriteCursor ギャップ（play+lead は周期欠乏の元） */
		int len1 = 0, len2 = 0;
		int total = 0;
		{
			len1 = (int)wc - (int)s.oldw;
			len2 = 0;
			if (len1 == 0) {
				Sleep((DWORD)(savedata.ms > 0 ? savedata.ms : 10));
				continue;
			}
			if (len1 < 0) {
				len1 = (int)ring - (int)s.oldw;
				len2 = (int)wc;
			}
			total = len1 + len2;
			if (total > (int)sizeof(stage)) total = (int)sizeof(stage);
			if (bpf > 0) total -= (total % bpf);
			if (total < bpf) {
				Sleep(5);
				continue;
			}
			if (total <= len1) { len1 = total; len2 = 0; }
			else { len2 = total - len1; }
		}
		if (total > (int)sizeof(stage)) {
			total = (int)sizeof(stage);
			if (bpf > 0) total -= (total % bpf);
			if (total <= len1) { len1 = total; len2 = 0; }
			else { len2 = total - len1; }
		}

		bool any = SlotFillOutPcm(s, stage, total);
		if (!any) {
			InterlockedExchange(&s.endflg, 1);
			ZeroMemory(stage, total);
		}

		bool dsHasMix = false;
		if (!isActive) {
			PlaySlot_StoreIncomingS16(stage, total);
		}
		if (isActive) {
			{
				std::lock_guard<std::mutex> guard(cl2);
				BOOL eqReset = FALSE;
				if (InterlockedCompareExchange(&g_eqResetNext, 0, 0) != 0) {
					if (!inXf) {
						InterlockedExchange(&g_eqResetNext, 0);
						eqReset = TRUE;
					}
				}
				equaliserBank(slot, stage, total, eqReset, s.outBits, s.outCh, s.outRate > 0 ? s.outRate : 0);
				if (!inXf && fade < 0.999f)
					SlotApplyFadeOutFormat(stage, total, s.outBits, fade);
			}
			if (inXf) {
				static BYTE mixBuf[64 * 1024];
				int mixN = total;
				if (mixN > (int)sizeof(mixBuf)) mixN = (int)sizeof(mixBuf);
				memcpy(mixBuf, stage, (size_t)mixN);
				int mixed = 0;
				if (PlaySlot_MixIncomingEx(mixBuf, mixN, s.outBits, s.outCh, &mixed) && mixed >= bpf) {
					memcpy(stage, mixBuf, (size_t)mixed);
					dsHasMix = true;
					total = mixed;
					if (total <= len1) { len1 = total; len2 = 0; }
					else { len2 = total - len1; }
					SlotWriteBufwav3(s.oldw, ring, mixBuf, mixed);
					Ogg_FeedPianoRoll(mixBuf, mixed);
					PlaybackCcWriteFromFormat(mixBuf, (UINT)mixed, s.outRate, s.outCh, s.outBits, true);
					g_dsWrittenBytes += mixed;
				}
				else {
					/* B 不足: A を現在ゲインで書く。done は進めない（途中昇格防止） */
					float g0 = 1.f, g1 = 1.f;
					if (ProXfade_PrepChunkGain(0, false, g0, g1))
						ProXfade_ApplyPcmGainRamp(stage, total, s.outBits, g0, g1);
					dsHasMix = true;
					SlotWriteBufwav3(s.oldw, ring, stage, total);
					Ogg_FeedPianoRoll(stage, total);
					PlaybackCcWriteFromFormat(stage, (UINT)total, s.outRate, s.outCh, s.outBits, true);
					g_dsWrittenBytes += total;
				}
			}
			else {
				SlotWriteBufwav3(s.oldw, ring, stage, total);
				Ogg_FeedPianoRoll(stage, total);
				PlaybackCcWriteFromFormat(stage, (UINT)total, s.outRate, s.outCh, s.outBits, false);
				g_dsWrittenBytes += total;
			}
		}

		LPVOID p1 = NULL, p2 = NULL;
		DWORD l1 = 0, l2 = 0;
		/* OVERLAP: Mix 済みなら追加ゲイン不要。B はフルPCMをリングに残し Mute（昇格用） */
		{
			const bool inXf = (ProXfade_Phase() == PRO_XF_OVERLAP || ProXfade_Phase() == PRO_XF_PROMOTE);
			if (inXf && (dsHasMix || !isActive)) {
				/* skip per-buffer PCM gain */
			}
			else {
				float g0 = 1.f, g1 = 1.f;
				if (ProXfade_PrepChunkGain(total, !isActive, g0, g1))
					ProXfade_ApplyPcmGainRamp(stage, total, s.outBits, g0, g1);
			}
		}
		if (SUCCEEDED(s.dsb->Lock(s.oldw, (DWORD)total, &p1, &l1, &p2, &l2, 0))) {
			int off = 0;
			if (p1 && l1) {
				memcpy(p1, stage + off, l1);
				off += (int)l1;
			}
			if (p2 && l2)
				memcpy(p2, stage + off, l2);
			s.dsb->Unlock(p1, l1, p2, l2);
		}
		s.oldw = (s.oldw + (ULONG)total) % ring;
		s.dsWritten += total;


		// heard 概算
		{
			ULONG pc2 = 0, wc2 = 0;
			if (SUCCEEDED(s.dsb->GetCurrentPosition(&pc2, &wc2))) {
				__int64 queued = (__int64)(((ULONG)s.oldw + ring - pc2) % ring);
				s.heard = s.dsWritten - queued;
				if (s.heard < 0) s.heard = 0;
			}
		}

		// active なら UI 用グローバルを同期 + 次曲スロット起動 / volume クロス
		if (InterlockedCompareExchange(&g_activeSlot, 0, 0) == slot
			&& InterlockedCompareExchange(&g_slotDualEnabled, 0, 0) != 0) {
			playb = s.playb;
			if (s.openMode != INT_MIN)
				g_openDecoderMode = s.openMode;

			const int xms = 0; /* クロスフェード撤去 */
			const bool dualWant = false;
			if (dualWant && s.rate > 0 && bpf > 0) {
				ULONG pcNow = 0, wcNow = 0;
				__int64 queuedBytes = 0;
				if (s.dsb && SUCCEEDED(s.dsb->GetCurrentPosition(&pcNow, &wcNow)))
					queuedBytes = (__int64)(((ULONG)s.oldw + ring - pcNow) % ring);
				double totalSec = 0.0, heardSec = 0.0;
				SongHeardSec_FromSlot(
					s.oggsize, s.playb, s.rate,
					queuedBytes, bpf, (s.outRate > 0) ? s.outRate : s.rate,
					totalSec, heardSec);
				__int64 remainMs = SongRemainMs_FromSecs(totalSec, heardSec);
				__int64 queuedMs = 0;
				if (s.outRate > 0 && bpf > 0)
					queuedMs = queuedBytes * 1000 / ((__int64)bpf * (__int64)s.outRate);
				const bool playedEnough = (heardSec >= 5.0)
					|| (totalSec > 1.0 && heardSec >= totalSec * 0.5);
				const bool nearEndPrefetch = (remainMs > 0 && remainMs <= xms + PRO_XF_PREPARE_MS)
					|| (s.endflg && playedEnough);
				if (remainMs <= 0 && s.endflg && playedEnough && queuedMs > 0)
					remainMs = queuedMs;
				/* 可聴の最後 xms をフェードにする → 書込み開始はキュー分だけ早く
				   remain<=xms だとキュー内の純Aの分だけフェードが短くなる（2000→~1000） */
				const __int64 overlapArmAt = (__int64)xms + queuedMs;
				const bool nearEndOverlap = (remainMs > 0 && remainMs <= (overlapArmAt > 0 ? overlapArmAt : 1));
				const bool timeToPrefetch = ProXfade_DualArmOk(xms) && nearEndPrefetch;
				const bool timeToOverlap = ProXfade_DualArmOk(xms) && nearEndOverlap;

				if (ProXfade_Phase() == PRO_XF_IDLE && timeToPrefetch) {
					int next = plcnt + 1;
					if (next >= pl->playcnt) next = 0;
					if (next >= 0 && next < pl->playcnt && next != plcnt) {
						const int nm = ProXfade_ModeFromPath(pl->pc[next].fol);
						const int bSlot = PlaySlot_IdleSlot();
						if (!ProXfade_IsSupportedMode(nm)) {
							ProXfade_MarkFailed();
						}
						else if (g_playSlots[bSlot].openMode == INT_MIN && m_ds
							&& InterlockedCompareExchange(&g_playSlots[bSlot].running, 0, 0) == 0) {
							if (PlaySlot_OpenFile(bSlot, pl->pc[next].fol, nm)
								&& PlaySlot_CreateBuffer(bSlot, m_ds, g_ds_buffer_bytes)) {
								ProXfade_ArmSlotReady(next, nm, pl->pc[next].fol, bSlot);
							}
							else {
								PlaySlot_StopFeedAndClose(bSlot, 2000);
								PlaySlot_ReleaseBuffer(bSlot);
								ProXfade_MarkFailed();
							}
						}
					}
				}
				const int bSlot = (int)InterlockedCompareExchange(&g_xfadeBSlot, 0, 0);
				LPDIRECTSOUNDBUFFER8 dsbB = (bSlot >= 0 && bSlot < PLAY_SLOT_COUNT)
					? g_playSlots[bSlot].dsb : NULL;
				if (ProXfade_Phase() == PRO_XF_READY && timeToOverlap && dsbB) {
					const int outR = (s.outRate > 0) ? s.outRate : ((s.rate > 0) ? s.rate : 44100);
					/* B 頭出しは1回。リング準備中も A フィードを止めない */
					if (!ProXfade_EnsureBFromHead(bSlot)) {
						ProXfade_MarkFailed();
					}
					else if (ProXfade_BRingReadyForOverlap(outR, bpf)) {
						__int64 qb2 = 0;
						ULONG pc2 = 0, wc2 = 0;
						if (s.dsb && SUCCEEDED(s.dsb->GetCurrentPosition(&pc2, &wc2)))
							qb2 = (__int64)(((ULONG)s.oldw + ring - pc2) % ring);
						__int64 qNow = 0;
						if (s.outRate > 0 && bpf > 0)
							qNow = qb2 * 1000 / ((__int64)bpf * (__int64)s.outRate);
						if (qNow < 0) qNow = 0;
						if (qNow > 5000) qNow = 5000;
						double totalSec2 = 0.0, heardSec2 = 0.0;
						SongHeardSec_FromSlot(
							s.oggsize, s.playb, s.rate,
							qb2, bpf, (s.outRate > 0) ? s.outRate : s.rate,
							totalSec2, heardSec2);
						__int64 remNow = SongRemainMs_FromSecs(totalSec2, heardSec2);
						int curveMs = (xms > 0) ? xms : 1;
						if (remNow > 0 && remNow < (__int64)curveMs + qNow) {
							int avail = (int)remNow - (int)qNow;
							if (avail < 1) avail = (int)remNow;
							if (avail > 0 && avail < curveMs) curveMs = avail;
						}
						if (curveMs < 1) curveMs = 1;
						ProXfade_BeginOverlapEx(curveMs, outR, bpf, (int)qNow);
						XfDbgF("slot arm(after B) rem=%lld qNow=%lld curve=%d ring=%d",
							(long long)remNow, (long long)qNow, curveMs, PlaySlot_MixRingBytes());
						ProXfade_TickOverlapVolumes(s.dsb, dsbB);
					}
				}
				if (ProXfade_Phase() == PRO_XF_OVERLAP && dsbB) {
					ProXfade_TickOverlapVolumes(s.dsb, dsbB);
					if (ProXfade_ReadyToPromote(queuedBytes)) {
						ProXfade_RequestPromote(0);
						Ogg_PostXfadePromote();
					}
				}
			}
		}
		else if (InterlockedCompareExchange(&g_activeSlot, 0, 0) == slot) {
			playb = s.playb;
			if (s.openMode != INT_MIN)
				g_openDecoderMode = s.openMode;
		}

		if (InterlockedCompareExchange(&s.endflg, 0, 0) != 0) {
			// EOF 後もしばらくドレイン。停止は上位が判断。
			Sleep(20);
		}
		else {
			Sleep((DWORD)(savedata.ms > 0 ? savedata.ms : 10));
		}
	}

	InterlockedExchange(&s.running, 0);
	return 0;
}

bool PlaySlot_BeginFeed(int slot)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return false;
	PlaySlot& s = g_playSlots[slot];
	if (!s.dsb || s.openMode == INT_MIN) return false;
	/* 停止済みの幽霊 thread ポインタが残っていると永久にフィードしない */
	if (s.thread != NULL) {
		if (InterlockedCompareExchange(&s.running, 0, 0) == 0) {
			delete s.thread;
			s.thread = NULL;
		}
		else if (InterlockedCompareExchange(&s.stopReq, 0, 0) != 0) {
			if (!PlaySlot_StopFeed(slot, 8000))
				return false;
		}
		else {
			return true; /* 健全に稼働中 */
		}
	}
	InterlockedExchange(&s.stopReq, 0);
	CWinThread* t = AfxBeginThread((AFX_THREADPROC)PlaySlot_FeedThread, (LPVOID)(INT_PTR)slot,
		THREAD_PRIORITY_TIME_CRITICAL, 0, CREATE_SUSPENDED);
	if (!t) return false;
	t->m_bAutoDelete = FALSE;
	s.thread = t;
	t->ResumeThread();
	return true;
}

bool PlaySlot_StartFromHead(int slot)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return false;
	PlaySlot& s = g_playSlots[slot];
	if (!s.dsb || s.openMode == INT_MIN) return false;

	PlaySlot_StopFeed(slot, 2000);
	PlaySlotDecoders* d = SlotDec(s);
	const int m = s.openMode;
	if (m == -10) {
		if (savedata.mp3orig) d->mp3Dec.seek2(0, s.ch);
		else d->mp3Dec.seek(0, s.ch);
	}
	else if (m == 999) {
		d->wavDec.Seek(0);
	}
	else if (m == -8 && d->kmp) {
		PlaySlot_FlacSetPosition(d->kmp, 0);
	}
	else if (m == -9 && d->kmp) {
		PlaySlot_M4aSetPosition(d->kmp, 0);
	}
	else if (m == -1 && d->vfOpened) {
		ov_pcm_seek(&d->vf, 0);
	}
	s.oldw = 0;
	s.dsWritten = 0;
	s.heard = 0;
	s.playb = 0;
	s.endflg = 0;
	s.pcmExtraPos = 0;
	PlaySlot_ClearIncomingMix();
	equaliserResetBank(slot);
	SlotRbClear(s);

	const ULONG bytes = (s.ringBytes > 0) ? s.ringBytes
		: ((g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : 153600);
	LPVOID p1 = NULL, p2 = NULL;
	DWORD l1 = 0, l2 = 0;
	if (SUCCEEDED(s.dsb->Lock(0, bytes, &p1, &l1, &p2, &l2, 0))) {
		if (p1 && l1) ZeroMemory(p1, l1);
		if (p2 && l2) ZeroMemory(p2, l2);
		s.dsb->Unlock(p1, l1, p2, l2);
	}
	s.dsb->SetCurrentPosition(0);
	/* 単純モデル: クロス中 B は鳴らさない。Mix リング+バッファ温めのみ。昇格で Play */
	s.dsb->Stop();
	s.dsb->SetVolume(DSBVOLUME_MIN);
	s.oldw = 0;
	s.dsWritten = 0;
	if (!PlaySlot_BeginFeed(slot))
		return false;
	return true;
}

bool PlaySlot_StopFeed(int slot, DWORD joinTimeoutMs)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return false;
	PlaySlot& s = g_playSlots[slot];
	InterlockedExchange(&s.stopReq, 1);
	if (!s.thread) {
		InterlockedExchange(&s.running, 0);
		return true;
	}
	HANDLE h = s.thread->m_hThread;
	DWORD elapsed = 0;
	const DWORD poll = 10;
	BOOL ok = FALSE;
	while (h) {
		DWORD w = WaitForSingleObject(h, poll);
		if (w == WAIT_OBJECT_0 || w == WAIT_FAILED) {
			ok = TRUE;
			break;
		}
		if (joinTimeoutMs != 0) {
			elapsed += poll;
			if (elapsed >= joinTimeoutMs) break;
		}
	}
	if (ok) {
		if (s.thread) {
			delete s.thread;
			s.thread = NULL;
		}
		InterlockedExchange(&s.running, 0);
		return true;
	}
	// join 失敗時は thread/running を残す（BeginFeed が二重起動しないように）
	return false;
}

bool PlaySlot_StopFeedAndClose(int slot, DWORD joinTimeoutMs)
{
	PlaySlot_StopFeed(slot, joinTimeoutMs);
	PlaySlot_CloseDecoder(slot);
	// バッファは handoff 後に相手が使う場合があるので、呼び側が Release する
	return true;
}

// timerp は MP3以外で oggsize=バイト長を想定。スロット内部は常に PCM フレーム。
static void PlaySlot_PublishSongTiming(const PlaySlot& neu)
{
	playb = neu.playb;
	g_openDecoderMode = neu.openMode;
	wavbit_sample_Hz = neu.rate;
	wavchannel = neu.ch;
	wavsam_depth = neu.bits;
	const int frames = (neu.oggsize > 0) ? neu.oggsize : 0;
	if (neu.openMode == -10) {
		oggsize = frames;
		loop1 = 0;
		loop2 = 0;
	}
	else {
		int bps = (neu.bits >= 8) ? (neu.bits / 8) : 2;
		int ch = (neu.ch > 0) ? neu.ch : 2;
		__int64 bytes = (__int64)frames * (__int64)ch * (__int64)bps;
		if (bytes < 0) bytes = 0;
		if (bytes > (__int64)0x7fffffff) bytes = (__int64)0x7fffffff;
		oggsize = (int)bytes;
		loop1 = 0;
		loop2 = frames;
	}
}

/* 書込み済み Mix バイトから、まだスピーカーに出ていないキュー分を引く。
   書込み位置のまま seek すると可聴位置より数〜数十ms 先の2曲目になる。 */
static __int64 SlotDsQueuedBytes(LPDIRECTSOUNDBUFFER8 dsb, ULONG writePos, ULONG ring)
{
	if (!dsb || ring == 0) return 0;
	ULONG pc = 0, wc = 0;
	if (FAILED(dsb->GetCurrentPosition(&pc, &wc))) return 0;
	return (__int64)(((ULONG)writePos + ring - pc) % ring);
}

/* 昇格: クロス中に温めた B ストリームをそのまま継続する。
   seek + RB 破棄 + prefill は波形が別物になり「繋ぎ飛び」の本体だった。
   バッファ内に可聴点が残っていないときだけ旧 seek 経路へフォールバック（互換）。 */
static bool SlotPromoteContinue(PlaySlot& neu, __int64 queuedHint = -1)
{
	if (!neu.dsb || neu.openMode == INT_MIN) return false;

	const int outBpf = SlotOutBpf(neu);
	/* 可聴位置は「書込み済み − 未再生」。target で先にクランプすると
	   done>target のとき mixedAud が過小→数ms巻き戻り / lead 過大で FALLBACK になる */
	__int64 mixedDone = ProXfade_OverlapBytesDone();
	if (mixedDone < 0) mixedDone = 0;

	__int64 queued = 0;
	if (queuedHint >= 0) {
		queued = queuedHint;
	}
	else if (m_dsb) {
		const int a = (int)InterlockedCompareExchange(&g_activeSlot, 0, 0);
		if (a >= 0 && a < PLAY_SLOT_COUNT && g_playSlots[a].dsb == m_dsb) {
			const ULONG ringA = (g_playSlots[a].ringBytes > 0) ? g_playSlots[a].ringBytes
				: ((g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : 153600);
			queued = SlotDsQueuedBytes(m_dsb, g_playSlots[a].oldw, ringA);
		}
		else if (m_dsb) {
			/* レガシー A: oldw グローバル */
			extern ULONG oldw;
			const ULONG ringA = (g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : 153600;
			queued = SlotDsQueuedBytes(m_dsb, oldw, ringA);
		}
	}
	if (queued < 0) queued = 0;
	__int64 mixedAud = mixedDone - queued;
	if (mixedAud < 0) mixedAud = 0;

	const ULONG ring = (neu.ringBytes > 0) ? neu.ringBytes
		: ((g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : 153600);
	if (ring == 0) return false;

	const __int64 lead = neu.dsWritten - mixedAud;
	const bool inBuffer = (lead >= 0 && lead < (__int64)ring && neu.dsWritten > 0);

	XfDbgF("SlotPromoteContinue seamless=%d mixedAud=%lld mixedDone=%lld queued=%lld lead=%lld dsW=%lld oldw=%lu playb=%lld",
		inBuffer ? 1 : 0,
		(long long)mixedAud, (long long)mixedDone, (long long)queued,
		(long long)lead, (long long)neu.dsWritten, (unsigned long)neu.oldw,
		(long long)neu.playb);

	if (inBuffer) {
		ULONG playPos = (ULONG)(mixedAud % (__int64)ring);
		if (outBpf > 0)
			playPos -= (playPos % (ULONG)outBpf);

		neu.dsb->Stop();
		neu.dsb->SetCurrentPosition(playPos);
		neu.heard = mixedAud;
		if (neu.heard < 0) neu.heard = 0;

		LONG v = (savedata.dsvol - 1) * 10;
		if (savedata.dsvol == -498) v = (savedata.dsvol - 1) * 7;
		if (v > 0) v = 0;
		if (v < DSBVOLUME_MIN) v = DSBVOLUME_MIN;
		neu.dsb->SetVolume(v);
		if (FAILED(neu.dsb->Play(0, 0, DSBPLAY_LOOPING)))
			return false;
		/* oldw / playb / RB / EQ はそのまま。デコーダも seek しない */
		return true;
	}

	/* ---- フォールバック: 旧 seek+prefill（リングから可聴点が消えた場合のみ） ---- */
	const __int64 target = ProXfade_OverlapBytesTarget();
	__int64 audForSeek = mixedAud;
	if (target > 0 && audForSeek > target)
		audForSeek = target;
	__int64 srcFrames = 0;
	if (outBpf > 0 && neu.outRate > 0 && neu.rate > 0) {
		const __int64 outFrames = audForSeek / outBpf;
		srcFrames = outFrames * (__int64)neu.rate / (__int64)neu.outRate;
	}
	else {
		int ms = 0;
		const ProXfadeInfo* xi = ProXfade_Get();
		if (xi && xi->overlapMs > 0) ms = xi->overlapMs;
		else ms = ProAudio_XfadeMs();
		if (ms < 1) ms = 1;
		const int rate = (neu.rate > 0) ? neu.rate : 44100;
		srcFrames = (__int64)rate * (__int64)ms / 1000;
	}
	if (srcFrames < 0) srcFrames = 0;
	if (neu.oggsize > 0 && srcFrames > (__int64)neu.oggsize)
		srcFrames = (__int64)neu.oggsize;

	XfDbgF("SlotPromoteContinue FALLBACK seekFrames=%lld", (long long)srcFrames);
	if (!SlotDoSeekCore(neu, srcFrames))
		return false;

	int prefill = (outBpf > 0 && neu.outRate > 0) ? (outBpf * neu.outRate * 60 / 1000) : 0;
	BYTE stage[64 * 1024];
	if (prefill > (int)sizeof(stage)) prefill = (int)sizeof(stage);
	if (prefill > (int)ring / 4) prefill = (int)ring / 4;
	if (outBpf > 0) prefill -= (prefill % outBpf);
	if (prefill >= outBpf) {
		if (SlotFillOutPcm(neu, stage, prefill)) {
			equaliserBank(neu.idx, stage, prefill, FALSE, neu.outBits, neu.outCh, neu.outRate > 0 ? neu.outRate : 0);
			LPVOID p1 = NULL, p2 = NULL;
			DWORD l1 = 0, l2 = 0;
			if (SUCCEEDED(neu.dsb->Lock(0, (DWORD)prefill, &p1, &l1, &p2, &l2, 0))) {
				int off = 0;
				if (p1 && l1) { memcpy(p1, stage + off, l1); off += (int)l1; }
				if (p2 && l2 && off < prefill) memcpy(p2, stage + off, l2);
				neu.dsb->Unlock(p1, l1, p2, l2);
				neu.oldw = (ULONG)prefill % ring;
				neu.dsWritten = prefill;
				neu.heard = 0;
			}
		}
	}
	LONG v = (savedata.dsvol - 1) * 10;
	if (savedata.dsvol == -498) v = (savedata.dsvol - 1) * 7;
	if (v > 0) v = 0;
	if (v < DSBVOLUME_MIN) v = DSBVOLUME_MIN;
	neu.dsb->SetVolume(v);
	neu.dsb->Play(0, 0, DSBPLAY_LOOPING);
	return true;
}

void PlaySlot_ApplyPromotePosition(int slot)
{
	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return;
	PlaySlot& neu = g_playSlots[slot];
	if (!neu.dsb) return;
	PlaySlot_StopFeed(slot, 8000);
	if (!SlotPromoteContinue(neu)) {
		PlaySlot_BeginFeed(slot);
		return;
	}
	InterlockedExchange(&g_activeSlot, slot);
	InterlockedExchange(&g_xfadeBSlot, -1);
	PlaySlot_BeginFeed(slot);
}

bool PlaySlot_Handoff(int oldSlot, int newSlot)
{
	if (oldSlot < 0 || oldSlot >= PLAY_SLOT_COUNT) return false;
	if (newSlot < 0 || newSlot >= PLAY_SLOT_COUNT) return false;
	if (oldSlot == newSlot) return false;
	PlaySlot& neu = g_playSlots[newSlot];
	if (!neu.dsb) return false;

	/* 先に A を黙らせてから B を Play（同時再生の被り防止） */
	PlaySlot& old = g_playSlots[oldSlot];
	if (old.dsb && old.dsb != neu.dsb) {
		old.dsb->SetVolume(DSBVOLUME_MIN);
		old.dsb->Stop();
	}
	InterlockedExchange(&g_xfadeNoWrite, 1);

	/* 温めた B を継続再生してから active にしてフィード再開（seek 作り直ししない） */
	PlaySlot_StopFeed(newSlot, 8000);
	if (!SlotPromoteContinue(neu)) {
		PlaySlot_BeginFeed(newSlot);
		return false;
	}
	InterlockedExchange(&g_activeSlot, newSlot);
	InterlockedExchange(&g_xfadeBSlot, -1);
	m_dsb = neu.dsb;
	m_dsb1 = neu.dsb1;
	InterlockedExchange(&g_slotDualEnabled, 1);
	PlaySlot_BeginFeed(newSlot);
	PlaySlot_PublishSongTiming(neu);
	g_ds_pcm_rate = neu.outRate;
	g_ds_pcm_ch = neu.outCh;
	g_ds_pcm_bits = neu.outBits;
	if (neu.ringBytes > 0)
		g_ds_buffer_bytes = neu.ringBytes;
	g_pcm_upscale_active = (neu.rate != neu.outRate || neu.ch != neu.outCh || neu.bits != neu.outBits) ? 1 : 0;
	endflg = 0;
	fade1 = 0;
	playb = neu.playb;

	InterlockedExchange(&g_stoppingSlot, oldSlot);
	PlaySlot_StopFeed(oldSlot, 8000);
	PlaySlot_CloseDecoder(oldSlot);
	RubberBand_DestroyBank(oldSlot);
	if (old.dsb && old.dsb != neu.dsb) {
		old.dsb->Stop();
		old.dsb->Release();
	}
	if (old.dsb1 && old.dsb1 != neu.dsb1)
		old.dsb1->Release();
	old.dsb = NULL;
	old.dsb1 = NULL;
	PlaySlot_Reset(oldSlot);
	InterlockedExchange(&g_stoppingSlot, -1);
	InterlockedExchange(&g_xfadeBSlot, -1);
	InterlockedExchange(&g_xfadeKeepDsb, 0);
	InterlockedExchange(&g_xfadeNoWrite, 0);
	InterlockedExchange(&g_xfadeDeferCcWrite, 0);
	PlaySlot_ClearIncomingMix();
	ProXfade_OnSongPlaybackStarted();
	return true;
}

// 従来の単一 HandleNotifications / グローバルデコーダからスロットへ渡す
bool PlaySlot_HandoffFromLegacy(int newSlot, LPDIRECTSOUNDBUFFER8 oldDsb, LPDIRECTSOUNDBUFFER oldDsb1)
{
	if (newSlot < 0 || newSlot >= PLAY_SLOT_COUNT) return false;
	PlaySlot& neu = g_playSlots[newSlot];
	if (!neu.dsb) return false;

	/* m_dsb は既に外れているので、旧 A のキューをここで測る */
	__int64 queuedHint = -1;
	if (oldDsb) {
		extern ULONG oldw;
		const ULONG ring = (g_ds_buffer_bytes > 0) ? g_ds_buffer_bytes : 153600;
		queuedHint = SlotDsQueuedBytes(oldDsb, oldw, ring);
		/* B Play 前に A を止める（被り防止）。キュー計測後に行う */
		oldDsb->SetVolume(DSBVOLUME_MIN);
		oldDsb->Stop();
	}
	InterlockedExchange(&g_xfadeNoWrite, 1);

	PlaySlot_StopFeed(newSlot, 8000);
	if (!SlotPromoteContinue(neu, queuedHint)) {
		PlaySlot_BeginFeed(newSlot);
		return false;
	}
	InterlockedExchange(&g_activeSlot, newSlot);
	InterlockedExchange(&g_xfadeBSlot, -1);
	InterlockedExchange(&g_slotDualEnabled, 1);
	m_dsb = neu.dsb;
	m_dsb1 = neu.dsb1;
	PlaySlot_BeginFeed(newSlot);
	PlaySlot_PublishSongTiming(neu);
	g_ds_pcm_rate = neu.outRate;
	g_ds_pcm_ch = neu.outCh;
	g_ds_pcm_bits = neu.outBits;
	if (neu.ringBytes > 0)
		g_ds_buffer_bytes = neu.ringBytes;
	g_pcm_upscale_active = (neu.rate != neu.outRate || neu.ch != neu.outCh || neu.bits != neu.outBits) ? 1 : 0;
	endflg = 0;
	fade1 = 0;
	playb = neu.playb;

	if (oldDsb && oldDsb != neu.dsb) {
		oldDsb->Stop();
		oldDsb->Release();
	}
	if (oldDsb1 && oldDsb1 != neu.dsb1)
		oldDsb1->Release();
	InterlockedExchange(&g_xfadeBSlot, -1);
	InterlockedExchange(&g_xfadeKeepDsb, 0);
	InterlockedExchange(&g_xfadeNoWrite, 0);
	InterlockedExchange(&g_xfadeSkipFrames, 0);
	InterlockedExchange(&g_xfadeDeferCcWrite, 0);
	PlaySlot_ClearIncomingMix();
	RubberBand_DestroyBank(0); /* レガシー A のみ。B(newSlot) の RB は継続 */
	ProXfade_OnSongPlaybackStarted();
	return true;
}

void PlaySlot_StopAll()
{
	for (int i = 0; i < PLAY_SLOT_COUNT; ++i) {
		PlaySlot& s = g_playSlots[i];
		if (s.dsb && s.dsb == m_dsb) {
			m_dsb = NULL;
			m_dsb1 = NULL;
		}
		PlaySlot_StopFeed(i, 8000);
		PlaySlot_CloseDecoder(i);
		PlaySlot_ReleaseBuffer(i);
		PlaySlot_Reset(i);
	}
	RubberBand_DestroyAll();
	InterlockedExchange(&g_slotDualEnabled, 0);
	InterlockedExchange(&g_activeSlot, 0);
	InterlockedExchange(&g_stoppingSlot, -1);
	InterlockedExchange(&g_xfadeBSlot, -1);
	InterlockedExchange(&g_xfadeHoldCcFile, 0);
	InterlockedExchange(&g_xfadeDeferCcWrite, 0);
}

bool PlaySlot_IsDualSeekTarget(int* outSlot)
{
	/* 昇格後はグローバルデコーダ閉鎖済み。m_dsb 不一致でも dual/active を信じる。
	   ※稼働中スロット全検索は曲1中の B 先読みを誤るのでやらない */
	int found = -1;
	if (m_dsb) {
		for (int i = 0; i < PLAY_SLOT_COUNT; ++i) {
			if (g_playSlots[i].dsb == m_dsb && g_playSlots[i].openMode != INT_MIN) {
				found = i;
				break;
			}
		}
	}
	if (found < 0 && InterlockedCompareExchange(&g_slotDualEnabled, 0, 0) != 0) {
		const int a = (int)InterlockedCompareExchange(&g_activeSlot, 0, 0);
		if (a >= 0 && a < PLAY_SLOT_COUNT
			&& g_playSlots[a].dsb && g_playSlots[a].openMode != INT_MIN)
			found = a;
	}
	if (found < 0) return false;
	/* m_dsb を癒す（昇格後のずれでレガシー経路に落ちるのを防ぐ） */
	if (g_playSlots[found].dsb)
		m_dsb = g_playSlots[found].dsb;
	if (outSlot) *outSlot = found;
	return true;
}

bool PlaySlot_Seek(int slot, __int64 playbFrames)
{
	/* スライダとホットキーの再入、フィード Lock との競合を避けるため
	   必ず StopFeed → SeekCore → BeginFeed の排他にする */
	static std::mutex s_seekMu;
	std::lock_guard<std::mutex> seekLk(s_seekMu);

	if (slot < 0 || slot >= PLAY_SLOT_COUNT) return false;
	PlaySlot& s = g_playSlots[slot];
	if (s.openMode == INT_MIN || !s.dsb) return false;
	if (playbFrames < 0) playbFrames = 0;
	if (s.oggsize > 0 && playbFrames > (__int64)s.oggsize)
		playbFrames = (__int64)s.oggsize;

	// 進行中クロスを破棄 + 相手スロット停止
	{
		const int bKill = ProXfade_CancelPendingCrossfade();
		if (bKill >= 0 && bKill < PLAY_SLOT_COUNT && bKill != slot) {
			PlaySlot_StopFeedAndClose(bKill, 4000);
			PlaySlot_ReleaseBuffer(bKill);
		}
		for (int i = 0; i < PLAY_SLOT_COUNT; ++i) {
			if (i == slot) continue;
			if (g_playSlots[i].thread
				|| InterlockedCompareExchange(&g_playSlots[i].running, 0, 0) != 0)
				PlaySlot_StopFeed(i, 4000);
		}
	}

	InterlockedExchange(&s.seekDoReq, 0);
	InterlockedExchange(&s.seekDone, 0);
	InterlockedExchange(&g_activeSlot, slot);
	InterlockedExchange(&g_slotDualEnabled, 1);
	m_dsb = s.dsb;
	m_dsb1 = s.dsb1;

	/* フィード生存中に SlotDoSeekCore すると Lock/デコーダで落ちる → 必ず join。
	   cl2 待ちで一回目がタイムアウトすることがあるので短く再試行する */
	bool stopped = PlaySlot_StopFeed(slot, 8000);
	if (!stopped) {
		Sleep(20);
		stopped = PlaySlot_StopFeed(slot, 8000);
	}
	if (!stopped) {
		PlaySlot_BeginFeed(slot);
		XfDbgF("PlaySlot_Seek FAIL stopFeed slot=%d frames=%lld", slot, (long long)playbFrames);
		return false;
	}

	if (!SlotDoSeekCore(s, playbFrames)) {
		PlaySlot_BeginFeed(slot);
		XfDbgF("PlaySlot_Seek FAIL seekCore slot=%d frames=%lld", slot, (long long)playbFrames);
		return false;
	}

	playb = playbFrames;
	g_dsWrittenBytes = 0;
	g_heardBytes = 0;
	g_endWrittenBytes = 0;
	endflg = 0;
	fade1 = 0;
	PlaySlot_PublishSongTiming(s);
	InterlockedExchange(&g_seekUiFreshTick, (LONG)GetTickCount());
	if (!PlaySlot_BeginFeed(slot)) {
		XfDbgF("PlaySlot_Seek FAIL beginFeed slot=%d frames=%lld", slot, (long long)playbFrames);
		return false;
	}
	playb = playbFrames; /* フィード起動直後の上書きレースを潰す */
	s.playb = playbFrames;
	InterlockedExchange(&g_seekUiFreshTick, (LONG)GetTickCount());
	XfDbgF("PlaySlot_Seek OK slot=%d frames=%lld playb=%lld", slot, (long long)playbFrames, (long long)s.playb);
	return true;
}

int PlaySlot_IdleSlot()
{
	const int a = (int)InterlockedCompareExchange(&g_activeSlot, 0, 0);
	return (a == 0) ? 1 : 0;
}

PlaySlot& PlaySlot_Active()
{
	int a = (int)InterlockedCompareExchange(&g_activeSlot, 0, 0);
	if (a < 0 || a >= PLAY_SLOT_COUNT) a = 0;
	return g_playSlots[a];
}

LPDIRECTSOUNDBUFFER8 PlaySlot_ActiveDsb()
{
	return PlaySlot_Active().dsb;
}
