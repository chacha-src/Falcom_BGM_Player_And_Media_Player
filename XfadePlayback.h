#pragma once
/* ライブ再生クロスフェード: 二重デコーダ・単一 HandleNotifications 用状態
 * ProAudio 残骸は使わない。 */

#ifndef XF_SLOTS
enum { XF_SLOTS = 2 };
#endif

/* 本流スロット 0/1（交互） */
extern volatile LONG g_xfSlot;
/* DispatchPlaywavFill / read* が今埋めているスロット。-1=未設定→g_xfSlot */
extern volatile LONG g_xfFillSlot;
/* 2系統目 play() 実行中（stop1/notify/DS再生成禁止） */
extern volatile LONG g_xfOpening;
/* オープン対象スロット（g_xfOpening 時） */
extern volatile LONG g_xfOpenSlot;
/* soft-open 中の UI スレッド ID（0=なし）。notify は本流のまま */
extern volatile LONG g_xfOpenThreadId;
/* クロスフェード進行中 */
extern volatile LONG g_xfInProgress;
/* 副スロット（xfade 中の B） */
extern volatile LONG g_xfSecSlot;
/* 昇格後に SIcon するプレイリスト index（-1=なし） */
extern volatile LONG g_xfPromotePlIndex;
/* xfade 完了直後、timer9000 の Restart を1回抑止 */
extern volatile LONG g_xfSuppressRestart;
/* XfTryStartCrossfade が UI へ投げ済み（二重 Post 抑止） */
extern volatile LONG g_xfStartPosted;
/* notify が立てる開始要求（UI timer9000 が処理） */
extern volatile LONG g_xfWantStart;
extern volatile LONG g_xfPrepared;

extern int g_ds_pcm_ch;
extern int g_ds_pcm_bits;
extern int g_ds_pcm_rate;
extern int wavbit_sample_Hz;
extern __int64 g_expectedDsBytes;

/* 現在の再生位置（DS 出力バイト）。シーク後も playb/poss5 から算出 */
__int64 XfPlayPosBytes();
/* 曲終端（DS 出力バイト）。end 確定値 → expected → loop3/oggsize */
__int64 XfTrackEndRefBytes(__int64 endWrittenBytes);
/* 曲長に含まれる「音の無い余白」（DS 出力バイト）。VST MIDI のみ */
__int64 XfTailPadBytes();
/* クロスフェードの基準終端（DS 出力バイト）= 終端 − 余白。窓もフェード長もこれで決める */
__int64 XfFadeEndRefBytes(__int64 endWrittenBytes);

/* cl2 を保持している呼び出し元用の混合開始（UI 呼び出しを含まない） */
void XfBeginMixLocked(int cur);
/* 混合開始で要求された MP 画面のフェード取消を UI スレッドで消化 */
void XfMpCancelFadeIfRequested();
/* 混合開始時に立つ。UI スレッドが XfMpCancelFadeIfRequested() で降ろす */
extern volatile LONG g_xfCancelMpFade;

double XfSecFromSave();

inline int XfDsOutBpf()
{
	const int ch = (g_ds_pcm_ch >= 1) ? g_ds_pcm_ch : 2;
	const int bits = (g_ds_pcm_bits >= 8) ? g_ds_pcm_bits : 16;
	const int bpf = ch * (bits / 8);
	return (bpf > 0) ? bpf : 4;
}

inline int XfDsOutRate()
{
	return (g_ds_pcm_rate > 0) ? g_ds_pcm_rate : ((wavbit_sample_Hz > 0) ? wavbit_sample_Hz : 44100);
}

inline __int64 XfCrossfadeWindowBytes()
{
	const int bpf = XfDsOutBpf();
	const int sr = XfDsOutRate();
	if (bpf <= 0 || sr <= 0)
		return 0;
	const double sec = XfSecFromSave();
	return (__int64)(sec * (double)sr + 0.5) * (__int64)bpf;
}

inline __int64 XfEndRefBytes(__int64 endWrittenBytes)
{
	return XfTrackEndRefBytes(endWrittenBytes);
}

/* スロット毎 Open 中の mode。未使用は INT_MIN */
extern int g_openDecoderModeSlot[XF_SLOTS];

/* スロット毎ソース形式（ミキシング前） */
extern int g_xfSrcRate[XF_SLOTS];
extern int g_xfSrcCh[XF_SLOTS];
extern int g_xfSrcBits[XF_SLOTS];

/* 等パワー進捗（出力フレーム単位） */
extern __int64 g_xfFadeTotalFrames;
extern __int64 g_xfFadePos;

inline int XfDecSlot()
{
	LONG f = InterlockedCompareExchange(&g_xfFillSlot, -1, -1);
	if (f >= 0 && f < XF_SLOTS)
		return (int)f;
	/* soft-open 中でも notify は本流。Open 側マクロだけ OpenSlot を見る */
	if (InterlockedCompareExchange(&g_xfOpening, 0, 0)) {
		const LONG tid = InterlockedCompareExchange(&g_xfOpenThreadId, 0, 0);
		if (tid != 0 && (DWORD)tid == GetCurrentThreadId()) {
			LONG o = InterlockedCompareExchange(&g_xfOpenSlot, 0, 0);
			if (o >= 0 && o < XF_SLOTS)
				return (int)o;
		}
	}
	LONG s = InterlockedCompareExchange(&g_xfSlot, 0, 0);
	if (s < 0 || s >= XF_SLOTS)
		s = 0;
	return (int)s;
}

inline int XfActiveSlot()
{
	LONG s = InterlockedCompareExchange(&g_xfSlot, 0, 0);
	if (s < 0 || s >= XF_SLOTS)
		s = 0;
	return (int)s;
}

inline int XfOtherSlot(int slot)
{
	return (slot == 0) ? 1 : 0;
}

/* ========== スロット二重化（実体は *_arr[2]。作業用グローバルへ Load/Save） ========== */
extern int poss_arr[XF_SLOTS];
extern int poss2_arr[XF_SLOTS];
extern int poss3_arr[XF_SLOTS];
extern int poss4_arr[XF_SLOTS];
extern int poss5_arr[XF_SLOTS];
extern int poss6_arr[XF_SLOTS];
extern int rrr_arr[XF_SLOTS];
extern int lenl_arr[XF_SLOTS];
extern int readme_arr[XF_SLOTS];
extern int loopcnt_arr[XF_SLOTS];
extern int muon_arr[XF_SLOTS];
extern int fade1_arr[XF_SLOTS];
extern int endflg_arr[XF_SLOTS];
extern float fade_arr[XF_SLOTS];
extern float fadeadd_arr[XF_SLOTS];
extern BOOL reset_arr[XF_SLOTS];
extern __int64 playb_arr[XF_SLOTS];
extern __int64 g_oggPcmDecodePos_arr[XF_SLOTS];
extern int g_oggRbPrimingNeed_arr[XF_SLOTS];

/* 作業用（現行バインド中スロットのミラー）。デコードコードは従来どおりこれらを触る */
extern int poss, poss2, poss3, poss4, poss5, poss6;
extern int rrr, lenl, readme, loopcnt, muon, fade1, endflg;
extern float fade, fadeadd;
extern BOOL reset;
extern __int64 playb;
extern __int64 g_oggPcmDecodePos;
extern int g_oggRbPrimingNeed;

void XfClearSlotDecodeState(int slot);
void XfSaveSlotDecodeState(int slot);
void XfLoadSlotDecodeState(int slot);

double XfSecFromSave();
int XfEnabled();
void XfResetAll();
void XfCloseSlotDecoders(int slot);
void XfApplySlotFormatToGlobals(int slot);
void XfCaptureGlobalsToSlot(int slot);
void XfSetSlotBag(int slot, int mode, int rate, int ch, int bits, int mp3bps, void* kmp);
int XfMixEqualPower(BYTE* dst, const BYTE* a, const BYTE* b, int outBytes, int bits, int ch);
void XfOnCrossfadeFinished();
void XfAbortCrossfade();
int XfFindNextAudioPlIndex(int fromInclusive);
/* 早期開始: heard が (end - xfade) に達したら 1 */
int XfShouldStartEarly(__int64 heardBytes, __int64 endWrittenBytes);
/* 成功で1。失敗時は呼び出し側がレガシー次曲へ */
int XfStartCrossfadeFromNotify();
int XfPreloadNextFromNotify();
int XfShouldPreloadNext();
/* 先読み(B)を中止。waitMs 待って 1=停止済み（スロットを閉じても安全） 0=まだ開いている */
int XfPreloadCancel(int waitMs);
void XfTryStartCrossfade();
