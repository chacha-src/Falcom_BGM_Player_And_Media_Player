#include "stdafx.h"
#include "ogg.h"
#include "XfadePlayback.h"
#include "PlayList.h"
#include "oggDlg.h"
#include "AudioUpscaler.h"
#include "VstMidiEngine.h"
#include "PluginKinds.h"
#include <math.h>
#include <mutex>

/* クロスフェードで B(スロット1)が現行になった後も、正しいエンジンを見る（実体は oggDlg.cpp） */
void MmBindVstActiveSlot();

volatile LONG g_xfSlot = 0;
volatile LONG g_xfFillSlot = -1;
volatile LONG g_xfOpening = 0;
volatile LONG g_xfOpenSlot = 0;
volatile LONG g_xfOpenThreadId = 0;
volatile LONG g_xfInProgress = 0;
/* ループ回数の最後の一周。この間はループ点を曲終端にしてクロスする。 */
volatile LONG g_xfTreatLoopAsEnd = 0;
/* 1=ループ回数ボックスに 1 以上。0 は無制限（回数の途中周を回さない）。 */
volatile LONG g_xfLoopCountActive = 0;

int XfDeferCrossfadeForLoop()
{
	if (!XfEnabled())
		return 0;
	if (!InterlockedCompareExchange(&g_xfLoopCountActive, 0, 0))
		return 0;
	if (InterlockedCompareExchange(&g_xfTreatLoopAsEnd, 0, 0))
		return 0;
	return 1;
}
volatile LONG g_xfSecSlot = 1;
volatile LONG g_xfPromotePlIndex = -1;
volatile LONG g_xfSuppressRestart = 0;
/* クロスフェードでスロットを昇格した時刻。0=昇格していない */
volatile LONG g_xfPromoteTick = 0;
volatile LONG g_xfStartPosted = 0;
volatile LONG g_xfWantStart = 0;
volatile LONG g_xfPrepared = 0;
volatile LONG g_xfCancelMpFade = 0;

int g_openDecoderModeSlot[XF_SLOTS] = { INT_MIN, INT_MIN };
int g_xfSrcRate[XF_SLOTS] = { 0, 0 };
int g_xfSrcCh[XF_SLOTS] = { 2, 2 };
int g_xfSrcBits[XF_SLOTS] = { 16, 16 };

__int64 g_xfFadeTotalFrames = 0;
__int64 g_xfFadePos = 0;
DWORD g_xfMixStartTick = 0;

/* デコード進行・フェード: 実体を2つ */
int poss_arr[XF_SLOTS] = {};
int poss2_arr[XF_SLOTS] = {};
int poss3_arr[XF_SLOTS] = {};
int poss4_arr[XF_SLOTS] = {};
int poss5_arr[XF_SLOTS] = {};
int poss6_arr[XF_SLOTS] = {};
int rrr_arr[XF_SLOTS] = { 1, 1 };
int lenl_arr[XF_SLOTS] = {};
int readme_arr[XF_SLOTS] = {};
int loopcnt_arr[XF_SLOTS] = {};
int muon_arr[XF_SLOTS] = {};
int fade1_arr[XF_SLOTS] = {};
int endflg_arr[XF_SLOTS] = {};
float fade_arr[XF_SLOTS] = { 1.f, 1.f };
float fadeadd_arr[XF_SLOTS] = {};
BOOL reset_arr[XF_SLOTS] = { TRUE, TRUE };
__int64 playb_arr[XF_SLOTS] = {};
__int64 g_oggPcmDecodePos_arr[XF_SLOTS] = {};
int g_oggRbPrimingNeed_arr[XF_SLOTS] = {};

/* 作業用ミラーは oggDlg.cpp 側の実体を extern（二重定義禁止） */

/* 形式・kmp のみ */
struct XfBag {
	int mode;
	int rate;
	int ch;
	int bits;
	int mp3bps;
	HKMP kmp;
	HKMP kmp1;
	int valid;
};
static XfBag g_xfBag[XF_SLOTS];

extern int wavchannel, wavbit_sample_Hz, wavsam_depth;
extern int g_mp3_decoder_bps;
extern int g_openDecoderMode;
extern CPlayList* pl;
extern int plcnt;
extern COggDlg* og;
extern int loop1, loop2, loop3, loop1_2;
extern int oggsize;
extern long data_size;
extern int flacmode;
extern int endf;
extern std::mutex cl2;
extern __int64 g_expectedDsBytes;
extern __int64 g_heardBytes;
extern __int64 g_dsWrittenBytes;
extern __int64 g_dsSongBase;

void XfClearSlotDecodeState(int slot)
{
	if (slot < 0 || slot >= XF_SLOTS)
		return;
	poss_arr[slot] = 0;
	poss2_arr[slot] = 0;
	poss3_arr[slot] = 0;
	poss4_arr[slot] = 0;
	poss5_arr[slot] = 0;
	poss6_arr[slot] = 0;
	rrr_arr[slot] = 1;
	lenl_arr[slot] = 0;
	readme_arr[slot] = 0;
	loopcnt_arr[slot] = 0;
	muon_arr[slot] = 0;
	fade1_arr[slot] = 0;
	endflg_arr[slot] = 0;
	fade_arr[slot] = 1.f;
	fadeadd_arr[slot] = 0.f;
	reset_arr[slot] = TRUE;
	playb_arr[slot] = 0;
	g_oggPcmDecodePos_arr[slot] = 0;
	g_oggRbPrimingNeed_arr[slot] = 0;
}

void XfSaveSlotDecodeState(int slot)
{
	if (slot < 0 || slot >= XF_SLOTS)
		return;
	poss_arr[slot] = poss;
	poss2_arr[slot] = poss2;
	poss3_arr[slot] = poss3;
	poss4_arr[slot] = poss4;
	poss5_arr[slot] = poss5;
	poss6_arr[slot] = poss6;
	rrr_arr[slot] = rrr;
	lenl_arr[slot] = lenl;
	readme_arr[slot] = readme;
	loopcnt_arr[slot] = loopcnt;
	muon_arr[slot] = muon;
	fade1_arr[slot] = fade1;
	endflg_arr[slot] = endflg;
	fade_arr[slot] = fade;
	fadeadd_arr[slot] = fadeadd;
	reset_arr[slot] = reset;
	playb_arr[slot] = playb;
	g_oggPcmDecodePos_arr[slot] = g_oggPcmDecodePos;
	g_oggRbPrimingNeed_arr[slot] = g_oggRbPrimingNeed;
	extern void XfSyncSlotDecodeRingSave(int slot);
	extern void XfSyncSlotSongMetaSave(int slot);
	XfSyncSlotDecodeRingSave(slot);
	XfSyncSlotSongMetaSave(slot);
}

void XfLoadSlotDecodeState(int slot)
{
	if (slot < 0 || slot >= XF_SLOTS)
		return;
	poss = poss_arr[slot];
	poss2 = poss2_arr[slot];
	poss3 = poss3_arr[slot];
	poss4 = poss4_arr[slot];
	poss5 = poss5_arr[slot];
	poss6 = poss6_arr[slot];
	rrr = rrr_arr[slot];
	lenl = lenl_arr[slot];
	readme = readme_arr[slot];
	loopcnt = loopcnt_arr[slot];
	muon = muon_arr[slot];
	fade1 = fade1_arr[slot];
	endflg = endflg_arr[slot];
	fade = fade_arr[slot];
	fadeadd = fadeadd_arr[slot];
	reset = reset_arr[slot];
	playb = playb_arr[slot];
	g_oggPcmDecodePos = g_oggPcmDecodePos_arr[slot];
	g_oggRbPrimingNeed = g_oggRbPrimingNeed_arr[slot];
	extern void XfSyncSlotDecodeRingLoad(int slot);
	extern void XfSyncSlotSongMetaLoad(int slot);
	XfSyncSlotDecodeRingLoad(slot);
	XfSyncSlotSongMetaLoad(slot);
}

__int64 XfPlayPosBytes()
{
	const int s = XfActiveSlot();
	/* 通常再生中は作業用 poss5/playb が本物。arr は Save した瞬間の古い値 */
	int posSamp;
	if (!InterlockedCompareExchange(&g_xfInProgress, 0, 0))
		posSamp = (poss5 > 0) ? poss5 : (int)playb;
	else
		posSamp = (poss5_arr[s] > 0) ? poss5_arr[s] : (int)playb_arr[s];
	if (posSamp <= 0) {
		/* 累積カーソルそのものではなく、この曲の開始からの差。
		 * まいご（約230秒）のまま Heart（約96秒）を測ると昇格直後に EOF になる。 */
		__int64 rel = g_heardBytes - g_dsSongBase;
		if (rel < 0)
			rel = 0;
		return rel;
	}
	const int outBpf = XfDsOutBpf();
	const int outRate = XfDsOutRate();
	int srcRate = (s >= 0 && s < XF_SLOTS && g_xfSrcRate[s] > 0) ? g_xfSrcRate[s] : 0;
	if (srcRate <= 0)
		srcRate = (wavbit_sample_Hz > 0) ? wavbit_sample_Hz : outRate;
	if (outBpf <= 0 || srcRate <= 0 || outRate <= 0)
		return 0;
	const __int64 outFrames = (srcRate == outRate)
		? (__int64)posSamp
		: ((__int64)posSamp * (__int64)outRate + srcRate / 2) / srcRate;
	return outFrames * (__int64)outBpf;
}

__int64 XfKpiSessionEndBytes()
{
	int ms = 0;
	const int srcRate = (wavbit_sample_Hz > 0) ? wavbit_sample_Hz : XfDsOutRate();
	if (og) {
		const DWORD L = og->sikpi.dwLength;
		if (L != (DWORD)-1 && L >= 2000 && L < 7200000u)
			ms = (int)L;
	}
	if (ms < 2000 && srcRate >= 8000) {
		int samp = 0;
		if (loop3 > 0)
			samp = loop3;
		else if (loop2 > 0)
			samp = loop1 + loop2;
		if (samp > 0) {
			const int fromSamp = (int)((double)samp * 1000.0 / (double)srcRate + 0.5);
			if (fromSamp >= 2000 && fromSamp < 7200000)
				ms = fromSamp;
		}
	}
	if (ms < 2000) {
		if (!XfEnabled())
			return 0;
		ms = XF_KPI_DEFAULT_LEN_MS;
	}
	const int outBpf = XfDsOutBpf();
	const int outRate = XfDsOutRate();
	if (outBpf <= 0 || outRate <= 0)
		return 0;
	return (__int64)ms * (__int64)outRate / 1000 * (__int64)outBpf;
}

__int64 XfTrackEndRefBytes(__int64 endWrittenBytes)
{
	/* シーク後の g_dsWrittenBytes 絶対値は曲位置と一致しないことがある。
	 * クロスフェード判定はソース総長（loop3/loop2）を優先する。
	 * KPI メタは短いことがあるので、取れるときだけ使い、取れなければ 5 分を曲長にする。 */
	const int dm = g_openDecoderMode;
	const bool kpiLenHintOnly = (dm == -3 || dm == -20 || dm == -21 || dm == -22);
	if (kpiLenHintOnly) {
		if (endWrittenBytes > 0)
			return endWrittenBytes;
		if (XfEnabled()) {
			const __int64 reserved = XfKpiSessionEndBytes();
			if (reserved > 0)
				return reserved;
		}
		if (g_expectedDsBytes > 0)
			return g_expectedDsBytes;
		return 0;
	}
	if (!kpiLenHintOnly) {
		int totalSamp = 0;
		/* endf==0 の loop1/loop2 は通常ループ点。最終周だけ終端にしてクロスする。 */
		if (loop2 > 0 && (endf != 0 || InterlockedCompareExchange(&g_xfTreatLoopAsEnd, 0, 0)))
			totalSamp = loop1 + loop2;
		else if (loop3 > 0)
			totalSamp = loop3;
		else if (oggsize > 0 && wavchannel > 0 && wavsam_depth >= 8) {
			const int bps = abs(wavsam_depth) / 8;
			if (bps > 0) {
				/* MP3: oggsize は PCM フレーム数。FLAC 等: バイト数 */
				if (dm == -10)
					totalSamp = oggsize;
				else
					totalSamp = oggsize / (wavchannel * bps);
			}
		}
		if (totalSamp > 0) {
			const int outBpf = XfDsOutBpf();
			const int outRate = XfDsOutRate();
			const int s = XfActiveSlot();
			int srcRate = (s >= 0 && s < XF_SLOTS && g_xfSrcRate[s] > 0) ? g_xfSrcRate[s] : 0;
			if (srcRate <= 0)
				srcRate = (wavbit_sample_Hz > 0) ? wavbit_sample_Hz : outRate;
			if (outBpf > 0 && srcRate > 0 && outRate > 0) {
				const __int64 outFrames = (srcRate == outRate)
					? (__int64)totalSamp
					: ((__int64)totalSamp * (__int64)outRate + srcRate / 2) / srcRate;
				return outFrames * (__int64)outBpf;
			}
		}
	}
	if (endWrittenBytes > 0)
		return endWrittenBytes;
	if (!kpiLenHintOnly && g_expectedDsBytes > 0)
		return g_expectedDsBytes;
	return 0;
}

/* VST MIDI の曲長は「最終イベント + 2 秒」。この 2 秒は残響を鳴らし切るための
 * 余白で、音楽としてはもう終わっている。ここを含めた終端で窓を取ると
 * フェード後半は A が居ない状態で B だけが上がり、クロスの末尾と B の頭がずれる。 */
__int64 XfTailPadBytes()
{
	if (!InterlockedCompareExchange(&g_xfInProgress, 0, 0)) {
		extern int g_openDecoderMode;
		if (IsVstMidiPlayMode(g_openDecoderMode)) {
			MmBindVstActiveSlot();
			const double sec = VstMidiTailPadSec();
			if (sec <= 0.0)
				return 0;
			const int bpf = XfDsOutBpf();
			const int rate = XfDsOutRate();
			if (bpf > 0 && rate > 0)
				return (__int64)(sec * (double)rate + 0.5) * (__int64)bpf;
		}
		return 0;
	}
	
	const int s = XfActiveSlot();
	if (IsVstMidiPlayMode(g_openDecoderModeSlot[s])) {
		MmBindVstActiveSlot();
		const double sec = VstMidiTailPadSec();
		if (sec <= 0.0)
			return 0;
		const int bpf = XfDsOutBpf();
		const int rate = XfDsOutRate();
		if (bpf > 0 && rate > 0)
			return (__int64)(sec * (double)rate + 0.5) * (__int64)bpf;
	}
	return 0;
}

__int64 XfFadeEndRefBytes(__int64 endWrittenBytes)
{
	const __int64 endRef = XfTrackEndRefBytes(endWrittenBytes);
	if (endRef <= 0)
		return endRef;
	const __int64 pad = XfTailPadBytes();
	/* 余白しか無いような短い曲では引かない */
	if (pad > 0 && endRef > pad * 2)
		return endRef - pad;
	return endRef;
}

double XfSecFromSave()
{
	int c = savedata.play_xfade_sec100;
	if (c < 10)
		c = 500;
	if (c > 12000)
		c = 12000;
	return (double)c / 100.0;
}

int XfEnabled()
{
	if (savedata.saverenzoku != 1)
		return 0;
	if (savedata.play_xfade != 0)
		return 1;
	if (og && ::IsWindow(og->GetSafeHwnd()) && og->m_xfade.GetSafeHwnd()
		&& og->m_xfade.GetCheck())
		return 1;
	return 0;
}

static int XfDisplayBusy()
{
	return InterlockedCompareExchange(&g_xfInProgress, 0, 0)
		|| InterlockedCompareExchange(&g_xfOpening, 0, 0)
		|| InterlockedCompareExchange(&g_xfPrepared, 0, 0);
}

int XfDisplaySrcRate()
{
	const int s = XfActiveSlot();
	if (XfDisplayBusy() && s >= 0 && s < XF_SLOTS && g_xfSrcRate[s] > 0)
		return g_xfSrcRate[s];
	return wavbit_sample_Hz;
}

int XfDisplaySrcCh()
{
	const int s = XfActiveSlot();
	if (XfDisplayBusy() && s >= 0 && s < XF_SLOTS && g_xfSrcCh[s] > 0)
		return g_xfSrcCh[s];
	return wavchannel;
}

int XfDisplaySrcBits()
{
	const int s = XfActiveSlot();
	if (XfDisplayBusy() && s >= 0 && s < XF_SLOTS && g_xfSrcBits[s] > 0)
		return g_xfSrcBits[s];
	return wavsam_depth;
}

void XfCaptureGlobalsToSlot(int slot)
{
	if (slot < 0 || slot >= XF_SLOTS)
		return;
	XfBag& b = g_xfBag[slot];
	/* 0 は実在しないデコーダモード（未設定の bag が 0 で埋まっているだけ）。
	 * これを取り込むとデコード先が消えるので、既存の値を残す。 */
	if (g_openDecoderMode != 0) {
		b.mode = g_openDecoderMode;
		g_openDecoderModeSlot[slot] = g_openDecoderMode;
	}
	b.rate = wavbit_sample_Hz;
	b.ch = wavchannel;
	b.bits = wavsam_depth;
	b.mp3bps = g_mp3_decoder_bps;
	b.kmp = og ? og->kmp : NULL;
	b.kmp1 = og ? og->kmp1 : NULL;
	b.valid = 1;
	g_xfSrcRate[slot] = wavbit_sample_Hz;
	g_xfSrcCh[slot] = wavchannel;
	{
		int sb = abs(wavsam_depth);
		if (!(sb == 8 || sb == 16 || sb == 24 || sb == 32))
			sb = 16;
		if (wavsam_depth < 0)
			sb = 16;
		g_xfSrcBits[slot] = sb;
	}
	extern void XfSyncSlotMediaPtrsCapture(int slot);
	XfSyncSlotMediaPtrsCapture(slot);
}

void XfSetSlotBag(int slot, int mode, int rate, int ch, int bits, int mp3bps, void* kmp)
{
	if (slot < 0 || slot >= XF_SLOTS)
		return;
	XfBag& b = g_xfBag[slot];
	b.mode = mode;
	b.rate = rate;
	b.ch = (ch > 0) ? ch : 2;
	b.bits = bits;
	b.mp3bps = mp3bps;
	b.kmp = (HKMP)kmp;
	b.kmp1 = NULL;
	b.valid = 1;
	g_openDecoderModeSlot[slot] = mode;
	g_xfSrcRate[slot] = rate;
	g_xfSrcCh[slot] = b.ch;
	{
		int sb = abs(bits);
		if (!(sb == 8 || sb == 16 || sb == 24 || sb == 32))
			sb = 16;
		if (bits < 0)
			sb = 16;
		g_xfSrcBits[slot] = sb;
	}
}

void XfUpdateSlotSrcFormat(int slot, int rate, int ch, int bits)
{
	if (slot < 0 || slot >= XF_SLOTS)
		return;
	XfBag& b = g_xfBag[slot];
	if (rate >= 8000 && rate <= 384000) {
		b.rate = rate;
		g_xfSrcRate[slot] = rate;
	}
	if (ch >= 1 && ch <= 32) {
		b.ch = ch;
		g_xfSrcCh[slot] = ch;
	}
	if (bits != 0) {
		int sb = abs(bits);
		if (!(sb == 8 || sb == 16 || sb == 24 || sb == 32))
			sb = 16;
		if (bits < 0)
			sb = 16;
		b.bits = bits;
		g_xfSrcBits[slot] = sb;
	}
	/* この関数は mode を書かない。bag がまだ 0（未設定）のまま valid にすると
	 * XfApplySlotFormatToGlobals がモード 0 を globals へ流し、デコード先が消える。 */
	if (b.mode != 0 && b.mode != INT_MIN)
		b.valid = 1;
}

void XfApplySlotFormatToGlobals(int slot)
{
	if (slot < 0 || slot >= XF_SLOTS)
		return;
	XfBag& b = g_xfBag[slot];
	if (!b.valid)
		return;
	/* モード 0 は実在しない。流すと混合中ずっと無音になり、毎サイクル
	 * endflg=1 が立って昇格直後の曲が丸ごと飛ばされる。 */
	if (b.mode != 0 && b.mode != INT_MIN) {
		g_openDecoderMode = b.mode;
		g_openDecoderModeSlot[slot] = b.mode;
	}
	wavbit_sample_Hz = b.rate;
	wavchannel = b.ch;
	wavsam_depth = b.bits;
	g_mp3_decoder_bps = b.mp3bps;
	if (og) {
		og->kmp = b.kmp;
		og->kmp1 = b.kmp1;
	}
	extern void XfSyncSlotMediaPtrsApply(int slot);
	XfSyncSlotMediaPtrsApply(slot);
}

void XfResetAll()
{
	InterlockedExchange(&g_xfSlot, 0);
	InterlockedExchange(&g_xfFillSlot, -1);
	InterlockedExchange(&g_xfOpening, 0);
	InterlockedExchange(&g_xfOpenSlot, 0);
	InterlockedExchange(&g_xfOpenThreadId, 0);
	InterlockedExchange(&g_xfInProgress, 0);
	InterlockedExchange(&g_xfSecSlot, 1);
	InterlockedExchange(&g_xfPromotePlIndex, -1);
	InterlockedExchange(&g_xfSuppressRestart, 0);
	InterlockedExchange(&g_xfStartPosted, 0);
	InterlockedExchange(&g_xfWantStart, 0);
	InterlockedExchange(&g_xfPrepared, 0);
	g_xfFadeTotalFrames = 0;
	g_xfFadePos = 0;
	g_xfMixStartTick = 0;
	for (int i = 0; i < XF_SLOTS; ++i) {
		g_openDecoderModeSlot[i] = INT_MIN;
		g_xfSrcRate[i] = 0;
		g_xfSrcCh[i] = 2;
		g_xfSrcBits[i] = 16;
		ZeroMemory(&g_xfBag[i], sizeof(g_xfBag[i]));
		XfClearSlotDecodeState(i);
	}
}

void XfClearLiveMixFlags()
{
	InterlockedExchange(&g_xfInProgress, 0);
	InterlockedExchange(&g_xfPrepared, 0);
	InterlockedExchange(&g_xfWantStart, 0);
	InterlockedExchange(&g_xfStartPosted, 0);
	InterlockedExchange(&g_xfOpening, 0);
	InterlockedExchange(&g_xfOpenThreadId, 0);
	InterlockedExchange(&g_xfFillSlot, -1);
	g_xfFadePos = 0;
	g_xfFadeTotalFrames = 0;
	g_xfMixStartTick = 0;
}

void XfCloseSlotDecodersImpl(int slot);
void XfCloseSlotDecoders(int slot)
{
	XfCloseSlotDecodersImpl(slot);
	if (slot >= 0 && slot < XF_SLOTS) {
		g_openDecoderModeSlot[slot] = INT_MIN;
		g_xfBag[slot].valid = 0;
		g_xfBag[slot].mode = INT_MIN;
		XfClearSlotDecodeState(slot);
	}
}

int XfMixEqualPower(BYTE* dst, const BYTE* a, const BYTE* b, int outBytes, int bits, int ch)
{
	if (!dst || outBytes <= 0 || ch < 1)
		return 0;
	const int bps = bits / 8;
	if (bps <= 0)
		return 0;
	const int bpf = bps * ch;
	if (bpf <= 0)
		return 0;
	outBytes -= outBytes % bpf;
	const int frames = outBytes / bpf;
	if (frames <= 0)
		return 0;

	const __int64 total = (g_xfFadeTotalFrames > 0) ? g_xfFadeTotalFrames : 1;
	for (int i = 0; i < frames; ++i) {
		double t = (double)(g_xfFadePos + i) / (double)total;
		if (t < 0.0) t = 0.0;
		if (t > 1.0) t = 1.0;
		const float ga = (float)cos(t * 1.5707963267948966);
		const float gb = (float)sin(t * 1.5707963267948966);
		const BYTE* pa = a ? (a + i * bpf) : NULL;
		const BYTE* pb = b ? (b + i * bpf) : NULL;
		BYTE* pd = dst + i * bpf;
		for (int c = 0; c < ch; ++c) {
			float va = 0.f, vb = 0.f;
			if (bits == 16) {
				if (pa) va = ((const short*)pa)[c] / 32768.f;
				if (pb) vb = ((const short*)pb)[c] / 32768.f;
			}
			else if (bits == 24) {
				if (pa) {
					const BYTE* p = pa + c * 3;
					int v = p[0] | (p[1] << 8) | ((signed char)p[2] << 16);
					va = v / 8388608.f;
				}
				if (pb) {
					const BYTE* p = pb + c * 3;
					int v = p[0] | (p[1] << 8) | ((signed char)p[2] << 16);
					vb = v / 8388608.f;
				}
			}
			else if (bits == 32) {
				if (pa) va = ((const int*)pa)[c] / 2147483648.f;
				if (pb) vb = ((const int*)pb)[c] / 2147483648.f;
			}
			else {
				if (pa) va = (pa[c] - 128) / 128.f;
				if (pb) vb = (pb[c] - 128) / 128.f;
			}
			float o = va * ga + vb * gb;
			if (o > 1.f) o = 1.f;
			if (o < -1.f) o = -1.f;
			if (bits == 16) {
				int v = (int)floorf(o * 32768.f + 0.5f);
				if (v > 32767) v = 32767;
				if (v < -32768) v = -32768;
				((short*)pd)[c] = (short)v;
			}
			else if (bits == 24) {
				int v = (int)floorf(o * 8388608.f + 0.5f);
				if (v > 8388607) v = 8388607;
				if (v < -8388608) v = -8388608;
				BYTE* p = pd + c * 3;
				p[0] = (BYTE)(v & 0xFF);
				p[1] = (BYTE)((v >> 8) & 0xFF);
				p[2] = (BYTE)((v >> 16) & 0xFF);
			}
			else if (bits == 32) {
				((int*)pd)[c] = (int)(o * 2147483647.0f);
			}
			else {
				pd[c] = (BYTE)(o * 127.f + 128.f);
			}
		}
	}
	g_xfFadePos += frames;
	return outBytes;
}

void XfOnCrossfadeFinished()
{
	const int oldSlot = XfActiveSlot();
	const int newSlot = (int)InterlockedCompareExchange(&g_xfSecSlot, 0, 0);
	const LONG pi = InterlockedExchange(&g_xfPromotePlIndex, -1);
	XfCloseSlotDecoders(oldSlot);
	InterlockedExchange(&g_xfSlot, newSlot);
	InterlockedExchange(&g_xfSecSlot, XfOtherSlot(newSlot));
	/* InProgress を下ろす前に終端フラグを消す。まいご（約230秒）の endflg が残ったまま
	 * Heart（約96秒）が見えると、タイマーが EOF と判定して次の次の曲へ進む。 */
	endflg = 0;
	fade1 = 0;
	InterlockedExchange(&g_xfSuppressRestart, 1);
	/* 昇格時刻を残す。混合解除直後は g_dsWrittenBytes / g_heardBytes が前曲の
	 * 絶対値を引きずっているので、この直後に終端を確定させてはいけない。 */
	InterlockedExchange(&g_xfPromoteTick, (LONG)GetTickCount());
	/* g_xfInProgress を下ろす前に新スロット側の終端ラッチも消す。
	 * XfLoadSlotDecodeState は endflg/fade1 をスロット配列から読み直すので、
	 * ここで消しておかないと混合解除直後に endflg=1 が復活し、
	 * DS スレッドが前曲の絶対書込み位置を新曲の終端として確定してしまう。 */
	if (newSlot >= 0 && newSlot < XF_SLOTS) {
		endflg_arr[newSlot] = 0;
		fade1_arr[newSlot] = 0;
		fade_arr[newSlot] = 1.0f;
	}
	/* 混合を必ず下ろす。残ると 2 曲目再演奏がノイズ、3 曲目クロスが始まらない */
	XfClearLiveMixFlags();
	InterlockedExchange(&g_xfFillSlot, -1);
	XfApplySlotFormatToGlobals(newSlot);
	XfLoadSlotDecodeState(newSlot);
	XfBindSlotUpscalerToSession(newSlot);
	extern __int64 g_endWrittenBytes, g_expectedDsBytes;
	g_endWrittenBytes = 0;
	{
		/* デコーダ位置が次曲の長さを超えていたら前曲の値が漏れている。
		 * そのままだと Heart は最初のタイマーで EOF になり、お名前が頭から始まる。 */
		int posSamp = (poss5 > 0) ? poss5 : (int)playb;
		int totalSamp = 0;
		if (loop3 > 0)
			totalSamp = loop3;
		else if (oggsize > 0)
			totalSamp = oggsize;
		else if (loop2 > 0)
			totalSamp = loop1 + loop2;
		if (totalSamp > 44100 && posSamp >= totalSamp) {
			poss5 = 0;
			playb = 0;
			poss5_arr[newSlot] = 0;
			playb_arr[newSlot] = 0;
			posSamp = 0;
		}
		/* 累積 g_dsWrittenBytes は 0 に戻さない。DS スレッドの加算が古い値を書き戻す。
		 * 基準だけ進め、曲位置は「累積 − 基準」にする。 */
		{
			std::lock_guard<std::mutex> guard(cl2);
			endflg = 0;
			fade1 = 0;
			g_dsSongBase = g_dsWrittenBytes;
			g_endWrittenBytes = 0;
		}
	}
	{
		const int srcRate = (g_xfSrcRate[newSlot] > 0) ? g_xfSrcRate[newSlot] : wavbit_sample_Hz;
		int totalSamp = 0;
		if (loop3 > 0)
			totalSamp = loop3;
		else if (oggsize > 0)
			totalSamp = oggsize;
		else if (loop2 > 0)
			totalSamp = loop1 + loop2;
		const int outRate = XfDsOutRate();
		const int outBpf = XfDsOutBpf();
		if (totalSamp > 0 && srcRate >= 8000 && outRate >= 8000 && outBpf > 0) {
			const __int64 outFrames = (srcRate == outRate)
				? (__int64)totalSamp
				: ((__int64)totalSamp * (__int64)outRate + srcRate / 2) / srcRate;
			g_expectedDsBytes = outFrames * (__int64)outBpf;
		}
		else
			g_expectedDsBytes = 0;
	}
	endflg_arr[newSlot] = 0;
	fade1_arr[newSlot] = 0;
	fade_arr[newSlot] = 1.0f;
	endflg = 0;
	fade1 = 0;
	fade = 1.0f;
	fadeadd = 0.0f;
	readme = 0;
	loopcnt = 0;
	rrr = 1;
	rrr_arr[newSlot] = 1;
	if (g_openDecoderModeSlot[newSlot] == -10) {
		extern int endf;
		endf = 1;
	}
	extern int g_pcm_upscale_active;
	g_pcm_upscale_active = g_audioUpscalerArr[newSlot].IsActive() ? 1 : 0;
	/* 次曲解決は UI 待ちにしない（遅れると 2 曲目の末で同じ曲を先読みして 3 曲目が来ない） */
	if (pi >= 0 && pl && pi < pl->playcnt)
		plcnt = (int)pi;
	InterlockedExchange(&g_xfSuppressRestart, 1);
	if (og && ::IsWindow(og->GetSafeHwnd()))
		og->PostMessage(WM_APP + 92, (WPARAM)newSlot, (LPARAM)pi);
}

void XfAbortCrossfade()
{
	const int cur = XfActiveSlot();
	const int sec = (int)InterlockedCompareExchange(&g_xfSecSlot, 0, 0);
	const int had = InterlockedCompareExchange(&g_xfInProgress, 0, 0)
		|| InterlockedCompareExchange(&g_xfPrepared, 0, 0);
	if (!had)
		return;
	/* 先読みスレッドがまだ B を開いている間は閉じない（cl2 を持つ呼び出し元がいるので待てない。
	 * 開き終わった側が中止フラグを見て自分で破棄する） */
	if (XfPreloadCancel(0))
		XfCloseSlotDecoders(sec);
	XfClearLiveMixFlags();
	InterlockedExchange(&g_xfPromotePlIndex, -1);
	g_xfFadePos = 0;
	g_xfFadeTotalFrames = 0;
	XfApplySlotFormatToGlobals(cur);
	extern int g_pcm_upscale_active;
	g_pcm_upscale_active = g_audioUpscalerArr[cur].IsActive() ? 1 : 0;
	extern __int64 g_endWrittenBytes;
	if (g_endWrittenBytes == 0)
		endflg_arr[cur] = 1;
}

int XfFindNextAudioPlIndex(int fromInclusive)
{
	if (!pl)
		return -1;
	const int n = pl->playcnt;
	if (n <= 0)
		return -1;
	int start = fromInclusive;
	if (start < 0)
		start = 0;
	if (start >= n)
		start = 0;
	int i = start;
	for (int guard = 0; guard < n; ++guard) {
		if (!pl->pc)
			return -1;
		/* 未再生の .mid は DirectShow(-2) のまま残る。動画と誤判定して飛ばすと
		 * MIDI が丸ごと候補から外れ、ずっと後ろの曲がクロスフェードで上がってくる。
		 * 再生時と同じ振り直しを先に行って本来のモードにする。
		 * 振り直しても -2 のまま（VST も KPI も無い）なら毎回引き直さない。 */
		static int s_midiFixFailIdx = -1;
		if (pl->pc[i].sub == -2 && i != s_midiFixFailIdx &&
			(VstIsMidiExt(pl->pc[i].fol) || VstIsProjectExt(pl->pc[i].fol))) {
			pl->FixMidiMode(pl->pc[i]);
			if (pl->pc[i].sub == -2)
				s_midiFixFailIdx = i;
		}
		const int sub = pl->pc[i].sub;
		CString fol = pl->pc[i].fol;
		fol.MakeLower();
		const BOOL looksVideo =
			(fol.Find(_T(".mp4")) >= 0 || fol.Find(_T(".avi")) >= 0 || fol.Find(_T(".mkv")) >= 0 ||
			 fol.Find(_T(".wmv")) >= 0 || fol.Find(_T(".mov")) >= 0 || fol.Find(_T(".webm")) >= 0 ||
			 fol.Find(_T(".mpg")) >= 0 || fol.Find(_T(".mpeg")) >= 0);
		if (sub != -2 && !looksVideo)
			return i;
		i++;
		if (i >= n)
			i = 0;
		if (i == start)
			break;
	}
	return -1;
}

int XfShouldStartEarly(__int64 /*heardBytes*/, __int64 endWrittenBytes)
{
	if (!XfEnabled())
		return 0;
	if (InterlockedCompareExchange(&g_xfInProgress, 0, 0))
		return 0;
	if (InterlockedCompareExchange(&g_xfOpening, 0, 0))
		return 0;
	/* シーク直後はデコード復帰前。窓に入っていても開始しない */
	extern ULONGLONG g_seekUiHoldUntil;
	if (g_seekUiHoldUntil != 0 && GetTickCount64() < g_seekUiHoldUntil)
		return 0;
	extern BOOL sek;
	extern int sek4;
	if (sek || sek4)
		return 0;
	const __int64 endRef = XfFadeEndRefBytes(endWrittenBytes);
	if (endRef <= 0)
		return 0;
	const __int64 xfBytes = XfCrossfadeWindowBytes();
	if (xfBytes <= 0)
		return 0;
	const __int64 pos = XfPlayPosBytes();
	const __int64 startAt = endRef - xfBytes;
	if (startAt <= 0)
		return (pos > 0) ? 1 : 0;
	/* 終端を越えた側も窓幅分だけ窓とみなす。mp3 の曲長はヘッダからの概算で
	 * 実際に鳴る長さとずれるため、pos < endRef だけを窓にすると、
	 * 先に pos が endRef を越える曲では開始せず EOF 任せの遅いクロスになる。 */
	return (pos >= startAt && pos < endRef + xfBytes) ? 1 : 0;
}

void XfTryStartCrossfade()
{
	if (!XfEnabled())
		return;
	if (InterlockedCompareExchange(&g_xfInProgress, 0, 0))
		return;
	if (InterlockedCompareExchange(&g_xfOpening, 0, 0))
		return;
	InterlockedExchange(&g_xfWantStart, 1);
}
