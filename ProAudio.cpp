#include "stdafx.h"
#include "ProAudio.h"
#include "ogg.h"
#include "oggDlg.h"
#include "CEqualizer.h"
#include "SongParams.h"
#include <math.h>
#include <float.h>

extern save savedata;
extern COggDlg* og;
extern int tempo;
extern int pitch;
extern TCHAR karento2[1024];

#if _UNICODE
#define PRO_EXTRA_DAT_NAME L"oggYSEDbgmu_ProExtra.dat"
#else
#define PRO_EXTRA_DAT_NAME "oggYSEDbgm_ProExtra.dat"
#endif

static const int PRO_EXTRA_FILE_VER = 2;
static const int PRO_EXTRA_FILE_VER_V1 = 1;

// ---- 曲別テーブル(固定) ----
static ProSongExtra g_extra[PRO_SONG_EXTRA_MAX];
static int g_extraCount = 0;
static bool g_extraDirty = false;

static TCHAR g_curList[256];
static TCHAR g_curPath[1024];
static int g_curMode = 0;
static int g_curRet2 = 0;

// ---- クロスフェード尾(リングバッファ。満杯後の毎サンプル memmove はカクつきの原因になる) ----
static float g_tailL[PRO_XFADE_TAIL_FRAMES];
static float g_tailR[PRO_XFADE_TAIL_FRAMES];
static int   g_tailFrames = 0; // 蓄積済みフレーム数(<= g_tailCap)
static int   g_tailCap = 0;
static int   g_tailRate = 0;
static int   g_tailWrite = 0;  // 次に書く位置。満杯時は最古の上書き位置でもある
static int   g_xfadeInLeft = 0; // 残りクロスフェードイン・フレーム
static int   g_xfadeInTotal = 0;
static int   g_xfadeInRate = 0; // Commit 時のレート(次曲が違う SR でもリサンプルして混ぜる)
static int   g_xfadeHoldOnce = 0; // Commit 直後の Reset で消さないための1回ガード
static float g_xfadeInL[PRO_XFADE_TAIL_FRAMES];
static float g_xfadeInR[PRO_XFADE_TAIL_FRAMES];
static float g_xfadeTmpL[PRO_XFADE_TAIL_FRAMES];
static float g_xfadeTmpR[PRO_XFADE_TAIL_FRAMES];

// ---- ラウドネス(簡易 K 重み寄り) ----
static double g_loudSum = 0.0;
static double g_loudCount = 0.0;
static float  g_loudPeak = 0.0f;
static float  g_kwZ1L = 0, g_kwZ2L = 0, g_kwZ1R = 0, g_kwZ2R = 0;
static int    g_loudRate = 0;

// ---- A/B ----
static ProAbSlot g_ab[2];
static int g_abActive = -1;

// ---- 相関 ----
static double g_corrSumLR = 0, g_corrSumLL = 0, g_corrSumRR = 0;
static float  g_corrCached = 1.0f;
static float  g_balCached = 0.0f;

// ---- キュー(現在曲の作業コピー) ----
static ProCue g_cues[PRO_CUE_MAX];
static int g_cueCount = 0;

// ---- Export TP ----
static float g_tpEnv = 1.0f;

static void FoldPath(TCHAR* dst, int dstcch, LPCTSTR src)
{
	if (!dst || dstcch <= 0) return;
	dst[0] = 0;
	if (!src) return;
	_tcsncpy(dst, src, dstcch - 1);
	dst[dstcch - 1] = 0;
	for (TCHAR* p = dst; *p; ++p) {
		if (*p == _T('/')) *p = _T('\\');
	}
}

static bool PathEq(LPCTSTR a, LPCTSTR b)
{
	if (!a || !b) return false;
	return _tcsicmp(a, b) == 0;
}

static int FindExtraIndex(LPCTSTR list, LPCTSTR path, int mode, int ret2)
{
	TCHAR pbuf[1024];
	FoldPath(pbuf, 1024, path);
	for (int i = 0; i < g_extraCount; ++i) {
		if (g_extra[i].mode != mode || g_extra[i].ret2 != ret2) continue;
		if (_tcsicmp(g_extra[i].listName, list ? list : _T("")) != 0) continue;
		if (PathEq(g_extra[i].path, pbuf)) return i;
	}
	return -1;
}

void ProAudio_Init()
{
	ZeroMemory(g_extra, sizeof(g_extra));
	g_extraCount = 0;
	g_extraDirty = false;
	g_curList[0] = 0;
	g_curPath[0] = 0;
	ZeroMemory(g_ab, sizeof(g_ab));
	g_abActive = -1;
	ProAudio_LoudnessReset();
	ProAudio_CorrReset();
	ProAudio_ExportLimitReset();
	g_tailFrames = 0;
	g_tailWrite = 0;
	g_xfadeInLeft = 0;
	g_xfadeInTotal = 0;
	g_xfadeInRate = 0;
	g_xfadeHoldOnce = 0;
	g_cueCount = 0;
	ProAudio_LoadExtras();
}

void ProAudio_LoadExtras()
{
	g_extraCount = 0;
	CString ss = karento2;
	ss += PRO_EXTRA_DAT_NAME;
	CFile f;
	if (f.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) != TRUE)
		return;
	try {
		int ver = 0, cnt = 0;
		if (f.Read(&ver, sizeof(int)) != sizeof(int)) { f.Close(); return; }
		if (ver != PRO_EXTRA_FILE_VER && ver != PRO_EXTRA_FILE_VER_V1) { f.Close(); return; }
		if (f.Read(&cnt, sizeof(int)) != sizeof(int)) { f.Close(); return; }
		if (cnt < 0) cnt = 0;
		if (cnt > PRO_SONG_EXTRA_MAX) cnt = PRO_SONG_EXTRA_MAX;
		const size_t v1Size = sizeof(ProSongExtra) - sizeof(int); // albumRgValid 無し
		for (int i = 0; i < cnt; ++i) {
			ProSongExtra e;
			ZeroMemory(&e, sizeof(e));
			if (ver == PRO_EXTRA_FILE_VER_V1) {
				if (f.Read(&e, (UINT)v1Size) != v1Size) break;
				// 旧: albumGainDb!=0 なら有効扱い。0dB album は移行で失われる
				e.albumRgValid = (e.albumGainDb != 0.0f) ? 1 : 0;
			}
			else {
				if (f.Read(&e, sizeof(e)) != sizeof(e)) break;
			}
			e.listName[255] = 0;
			e.path[1023] = 0;
			if (e.cueCount < 0) e.cueCount = 0;
			if (e.cueCount > PRO_CUE_MAX) e.cueCount = PRO_CUE_MAX;
			g_extra[g_extraCount++] = e;
		}
	}
	catch (...) {
	}
	f.Close();
	g_extraDirty = false;
}

void ProAudio_SaveExtras()
{
	if (!g_extraDirty) return;
	CString ss = karento2;
	ss += PRO_EXTRA_DAT_NAME;
	CFile f;
	if (f.Open(ss, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) != TRUE)
		return;
	try {
		int ver = PRO_EXTRA_FILE_VER;
		int cnt = g_extraCount;
		f.Write(&ver, sizeof(int));
		f.Write(&cnt, sizeof(int));
		for (int i = 0; i < cnt; ++i)
			f.Write(&g_extra[i], sizeof(ProSongExtra));
		g_extraDirty = false;
	}
	catch (...) {
	}
	f.Close();
}

bool ProAudio_GetExtra(LPCTSTR list, LPCTSTR path, int mode, int ret2, ProSongExtra& out)
{
	int idx = FindExtraIndex(list, path, mode, ret2);
	if (idx < 0) return false;
	out = g_extra[idx];
	return true;
}

bool ProAudio_UpsertExtra(const ProSongExtra& e)
{
	int idx = FindExtraIndex(e.listName, e.path, e.mode, e.ret2);
	if (idx >= 0) {
		g_extra[idx] = e;
		g_extraDirty = true;
		return true;
	}
	if (g_extraCount >= PRO_SONG_EXTRA_MAX)
		return false;
	g_extra[g_extraCount++] = e;
	g_extraDirty = true;
	return true;
}

void ProAudio_SetCurrentSongKey(LPCTSTR list, LPCTSTR path, int mode, int ret2)
{
	TCHAR pbuf[1024];
	FoldPath(pbuf, 1024, path);
	const bool same =
		(g_curMode == mode && g_curRet2 == ret2 &&
		 PathEq(g_curPath, pbuf) &&
		 _tcsicmp(g_curList, list ? list : _T("")) == 0);

	_tcsncpy(g_curList, list ? list : _T(""), 255);
	g_curList[255] = 0;
	_tcsncpy(g_curPath, pbuf, 1023);
	g_curPath[1023] = 0;
	g_curMode = mode;
	g_curRet2 = ret2;
	ProAudio_CueLoadForCurrent();
	// 同じ曲の再設定(詳細ダイアログ起動など)では計測・φを消さない
	if (!same) {
		ProAudio_LoudnessReset();
		ProAudio_CorrReset();
	}
}

// ============================================================================
// クロスフェード
// ============================================================================
static int XfadeMs()
{
	/* クロスフェード撤去。常に 0（ギャップレス用テール経路のみ） */
	return 0;
}

int ProAudio_XfadeMs()
{
	return 0;
}

static void PcmInterleavedToFloat(const unsigned char* p, int bits, float& L, float& R, int ch)
{
	L = R = 0;
	if (bits == 16) {
		const short* s = (const short*)p;
		L = s[0] / 32768.0f;
		R = (ch > 1) ? s[1] / 32768.0f : L;
	}
	else if (bits == 24) {
		int v0 = p[0] | (p[1] << 8) | ((signed char)p[2] << 16);
		L = v0 / 8388608.0f;
		if (ch > 1) {
			int v1 = p[3] | (p[4] << 8) | ((signed char)p[5] << 16);
			R = v1 / 8388608.0f;
		}
		else R = L;
	}
	else if (bits == 32) {
		const int* s = (const int*)p;
		L = s[0] / 2147483648.0f;
		R = (ch > 1) ? s[1] / 2147483648.0f : L;
	}
	else {
		L = (p[0] - 128) / 128.0f;
		R = (ch > 1) ? (p[1] - 128) / 128.0f : L;
	}
}

static void FloatToPcmInterleaved(unsigned char* p, int bits, float L, float R, int ch)
{
	if (L > 1.f) L = 1.f; if (L < -1.f) L = -1.f;
	if (R > 1.f) R = 1.f; if (R < -1.f) R = -1.f;
	if (bits == 16) {
		short* s = (short*)p;
		int v = (int)floorf(L * 32768.f + 0.5f);
		if (v > 32767) v = 32767; if (v < -32768) v = -32768;
		s[0] = (short)v;
		if (ch > 1) {
			v = (int)floorf(R * 32768.f + 0.5f);
			if (v > 32767) v = 32767; if (v < -32768) v = -32768;
			s[1] = (short)v;
		}
	}
	else if (bits == 24) {
		int v = (int)floorf(L * 8388608.f + 0.5f);
		if (v > 8388607) v = 8388607; if (v < -8388608) v = -8388608;
		p[0] = (unsigned char)(v & 0xFF);
		p[1] = (unsigned char)((v >> 8) & 0xFF);
		p[2] = (unsigned char)((v >> 16) & 0xFF);
		if (ch > 1) {
			v = (int)floorf(R * 8388608.f + 0.5f);
			if (v > 8388607) v = 8388607; if (v < -8388608) v = -8388608;
			p[3] = (unsigned char)(v & 0xFF);
			p[4] = (unsigned char)((v >> 8) & 0xFF);
			p[5] = (unsigned char)((v >> 16) & 0xFF);
		}
	}
	else if (bits == 32) {
		int* s = (int*)p;
		s[0] = (int)(L * 2147483647.0f);
		if (ch > 1) s[1] = (int)(R * 2147483647.0f);
	}
	else {
		p[0] = (unsigned char)(L * 127.f + 128.f);
		if (ch > 1) p[1] = (unsigned char)(R * 127.f + 128.f);
	}
}

void ProAudio_PushTailPcm(const void* interleaved, int byteLen, int sampleRate, int bits, int channels)
{
	if (!savedata.pro_gapless && XfadeMs() <= 0) return;
	if (!interleaved || byteLen <= 0 || sampleRate <= 0 || channels <= 0) return;
	int bps = bits / 8;
	if (bps <= 0) return;
	int bpf = bps * channels;
	if (bpf <= 0) return;
	byteLen -= byteLen % bpf;
	int frames = byteLen / bpf;
	if (frames <= 0) return;

	int need = XfadeMs() * sampleRate / 1000;
	if (need < 1) need = 1;
	if (need > PRO_XFADE_TAIL_FRAMES) need = PRO_XFADE_TAIL_FRAMES;
	if (sampleRate != g_tailRate || need != g_tailCap) {
		g_tailRate = sampleRate;
		g_tailCap = need;
		g_tailFrames = 0;
		g_tailWrite = 0;
	}
	if (g_tailCap <= 0) return;

	const unsigned char* p = (const unsigned char*)interleaved;
	for (int i = 0; i < frames; ++i) {
		float L, R;
		PcmInterleavedToFloat(p + i * bpf, bits, L, R, channels);
		g_tailL[g_tailWrite] = L;
		g_tailR[g_tailWrite] = R;
		g_tailWrite++;
		if (g_tailWrite >= g_tailCap)
			g_tailWrite = 0;
		if (g_tailFrames < g_tailCap)
			g_tailFrames++;
	}
}

void ProAudio_CommitTailForNext()
{
	if (g_tailFrames <= 0 || g_tailCap <= 0) {
		g_xfadeInLeft = 0;
		g_xfadeInTotal = 0;
		g_xfadeInRate = 0;
		return;
	}
	const int n = g_tailFrames;
	// リングを時系列(最古→最新)へ直線コピー
	int src = (g_tailFrames < g_tailCap) ? 0 : g_tailWrite;
	for (int i = 0; i < n; ++i) {
		g_xfadeInL[i] = g_tailL[src];
		g_xfadeInR[i] = g_tailR[src];
		src++;
		if (src >= g_tailCap)
			src = 0;
	}
	g_xfadeInTotal = n;
	g_xfadeInLeft = n;
	g_xfadeInRate = g_tailRate;
	g_xfadeHoldOnce = (n > 0) ? 1 : 0;
}

void ProAudio_ResetXfadeIn()
{
	// 連続再生: OnSongBoundary→Commit の直後に SongParams が Reset するため、1回だけ保持
	if (g_xfadeHoldOnce) {
		g_xfadeHoldOnce = 0;
		return;
	}
	g_xfadeInLeft = 0;
	g_xfadeInTotal = 0;
	g_xfadeInRate = 0;
}

void ProAudio_ClearXfadeIn()
{
	// 二重DS昇格時: 疑似 ApplyXfadeIn / テールを必ず破棄
	g_xfadeHoldOnce = 0;
	g_xfadeInLeft = 0;
	g_xfadeInTotal = 0;
	g_xfadeInRate = 0;
	g_tailFrames = 0;
	g_tailWrite = 0;
}

void ProAudio_OnSongBoundary()
{
	ProAudio_CommitTailForNext();
	g_tailFrames = 0;
	g_tailWrite = 0;
	ProAudio_LoudnessCommitCurrentSong();
	ProAudio_LoudnessReset();
	ProAudio_CorrReset();
}

// 次曲の出力 SR が前曲と違うとき(拡張子・デコーダ違い)、残りテールを線形リサンプル。
// 失敗時は破棄(クリックより歪みを避ける)。
static bool ResampleXfadeInToRate(int newRate)
{
	if (newRate <= 0 || g_xfadeInRate <= 0 || newRate == g_xfadeInRate)
		return true;
	if (g_xfadeInLeft <= 0 || g_xfadeInTotal <= 0)
		return false;
	const int srcStart = g_xfadeInTotal - g_xfadeInLeft;
	const int srcFrames = g_xfadeInLeft;
	if (srcStart < 0 || srcStart + srcFrames > g_xfadeInTotal)
		return false;
	const int dstFrames = (int)(((__int64)srcFrames * (__int64)newRate + g_xfadeInRate / 2) / g_xfadeInRate);
	if (dstFrames < 1 || dstFrames > PRO_XFADE_TAIL_FRAMES)
		return false;

	for (int i = 0; i < dstFrames; ++i) {
		const double srcPos = (double)i * (double)g_xfadeInRate / (double)newRate;
		int i0 = (int)srcPos;
		int i1 = i0 + 1;
		float t = (float)(srcPos - (double)i0);
		if (i0 >= srcFrames) i0 = srcFrames - 1;
		if (i1 >= srcFrames) i1 = srcFrames - 1;
		if (i0 < 0) i0 = 0;
		if (i1 < 0) i1 = 0;
		const float aL = g_xfadeInL[srcStart + i0];
		const float bL = g_xfadeInL[srcStart + i1];
		const float aR = g_xfadeInR[srcStart + i0];
		const float bR = g_xfadeInR[srcStart + i1];
		g_xfadeTmpL[i] = aL + (bL - aL) * t;
		g_xfadeTmpR[i] = aR + (bR - aR) * t;
	}
	memcpy(g_xfadeInL, g_xfadeTmpL, sizeof(float) * (size_t)dstFrames);
	memcpy(g_xfadeInR, g_xfadeTmpR, sizeof(float) * (size_t)dstFrames);
	g_xfadeInTotal = dstFrames;
	g_xfadeInLeft = dstFrames;
	g_xfadeInRate = newRate;
	return true;
}

int ProAudio_ApplyXfadeIn(void* interleaved, int byteLen, int sampleRate, int bits, int channels)
{
	if (g_xfadeInLeft <= 0 || !interleaved || byteLen <= 0) return 0;
	// 拡張子・デコーダが変わって SR が違っても、リサンプルしてから混ぜる
	if (g_xfadeInRate > 0 && sampleRate > 0 && g_xfadeInRate != sampleRate) {
		if (!ResampleXfadeInToRate(sampleRate)) {
			g_xfadeHoldOnce = 0;
			g_xfadeInLeft = 0;
			g_xfadeInTotal = 0;
			g_xfadeInRate = 0;
			return 0;
		}
	}
	int bps = bits / 8;
	if (bps <= 0 || channels <= 0) return 0;
	int bpf = bps * channels;
	byteLen -= byteLen % bpf;
	int frames = byteLen / bpf;
	if (frames <= 0) return 0;

	unsigned char* p = (unsigned char*)interleaved;
	int done = 0;
	const int total = (g_xfadeInTotal > 0) ? g_xfadeInTotal : 1;
	for (int i = 0; i < frames && g_xfadeInLeft > 0; ++i) {
		float L, R;
		PcmInterleavedToFloat(p + i * bpf, bits, L, R, channels);
		const int idx = total - g_xfadeInLeft;
		float t = (float)(idx + 1) / (float)(total + 1);
		float w = t * t * (3.0f - 2.0f * t); // smoothstep: 0=old tail, 1=new
		float oL = g_xfadeInL[idx];
		float oR = g_xfadeInR[idx];
		if (channels < 2) {
			// 次曲がモノ: 前曲ステレオテールをモノへ畳む
			float oM = 0.5f * (oL + oR);
			L = oM * (1.0f - w) + L * w;
			R = L;
		}
		else {
			L = oL * (1.0f - w) + L * w;
			R = oR * (1.0f - w) + R * w;
		}
		FloatToPcmInterleaved(p + i * bpf, bits, L, R, channels);
		g_xfadeInLeft--;
		done++;
	}
	if (g_xfadeInLeft <= 0)
		g_xfadeInRate = 0;
	return done;
}

bool ProAudio_ShouldEarlyAdvance(__int64 heardBytes, __int64 endWrittenBytes, int outBytesPerFrame, int sampleRate)
{
	if (!savedata.pro_gapless && XfadeMs() <= 0) return false;
	if (endWrittenBytes <= 0 || outBytesPerFrame <= 0 || sampleRate <= 0) return false;
	// クロスフェード時は DS 側で g_endWrittenBytes を既に xf ms 手前へ切り上げ済み。
	// ここは「その終端へ再生カーソルが到達したか」だけ見る。
	return heardBytes >= endWrittenBytes;
}

// ============================================================================
// ReplayGain / ラウドネス
// ============================================================================
void ProAudio_LoudnessReset()
{
	g_loudSum = 0;
	g_loudCount = 0;
	g_loudPeak = 0;
	g_kwZ1L = g_kwZ2L = g_kwZ1R = g_kwZ2R = 0;
	g_loudRate = 0;
}

// 簡易ハイシェルフ寄り前段 + 高域強調(ITU-R BS.1770 の粗い近似)
static float KwStep(float x, float& z1, float& z2, int rate)
{
	if (rate <= 0) return x;
	// 1次 HPF ~60Hz + 軽い高域ブースト
	const float a = expf(-2.0f * 3.14159265f * 60.0f / (float)rate);
	float y = x - z1;
	z1 = x;
	float y2 = y + 0.5f * (y - z2);
	z2 = y;
	return y2;
}

void ProAudio_LoudnessFeed(const float* L, const float* R, int frames, int sampleRate)
{
	if (!L || !R || frames <= 0 || sampleRate <= 0) return;
	if (g_loudRate != sampleRate) {
		g_loudRate = sampleRate;
		g_kwZ1L = g_kwZ2L = g_kwZ1R = g_kwZ2R = 0;
	}
	for (int i = 0; i < frames; ++i) {
		float l = KwStep(L[i], g_kwZ1L, g_kwZ2L, sampleRate);
		float r = KwStep(R[i], g_kwZ1R, g_kwZ2R, sampleRate);
		float m = 0.5f * (l * l + r * r);
		g_loudSum += (double)m;
		g_loudCount += 1.0;
		float p = fabsf(L[i]);
		if (fabsf(R[i]) > p) p = fabsf(R[i]);
		if (p > g_loudPeak) g_loudPeak = p;
	}
}

void ProAudio_LoudnessCommitCurrentSong()
{
	if (g_loudCount < (double)(g_loudRate > 0 ? g_loudRate : 44100)) // 最低約1秒
		return;
	if (g_curPath[0] == 0) return;
	double mean = g_loudSum / g_loudCount;
	if (mean < 1e-12) mean = 1e-12;
	float lufsApprox = 10.0f * (float)log10(mean) - 0.691f; // 粗い LUFS 近似
	float target = (float)savedata.pro_rg_target; // 既定 -18
	if (target > -1.0f || target < -30.0f) target = -18.0f;
	float gainDb = target - lufsApprox;

	ProSongExtra e;
	ZeroMemory(&e, sizeof(e));
	int idx = FindExtraIndex(g_curList, g_curPath, g_curMode, g_curRet2);
	if (idx >= 0) e = g_extra[idx];
	else {
		_tcsncpy(e.listName, g_curList, 255);
		_tcsncpy(e.path, g_curPath, 1023);
		e.mode = g_curMode;
		e.ret2 = g_curRet2;
		e.loopIn = -1;
		e.loopOut = -1;
	}
	e.trackGainDb = gainDb;
	e.trackPeak = g_loudPeak;
	e.rgValid = 1;
	ProAudio_UpsertExtra(e);
	ProAudio_SaveExtras();
}

float ProAudio_ReplayGainLinear()
{
	if (savedata.pro_rg_mode <= 0) return 1.0f;
	ProSongExtra e;
	if (!ProAudio_GetExtra(g_curList, g_curPath, g_curMode, g_curRet2, e) || !e.rgValid)
		return 1.0f;
	float db = e.trackGainDb;
	if (savedata.pro_rg_mode == 2 && e.albumRgValid)
		db = e.albumGainDb;
	// プリクリップ防止: ピーク×gain が 1 を超えないよう制限
	float g = powf(10.0f, db / 20.0f);
	if (e.trackPeak > 1e-6f && g * e.trackPeak > 1.0f)
		g = 1.0f / e.trackPeak;
	if (g < 0.05f) g = 0.05f;
	if (g > 8.0f) g = 8.0f;
	return g;
}

void ProAudio_ComputeAlbumGainsForList(LPCTSTR listName)
{
	if (!listName || !*listName) return;
	double sum = 0;
	int n = 0;
	for (int i = 0; i < g_extraCount; ++i) {
		if (_tcsicmp(g_extra[i].listName, listName) != 0) continue;
		if (!g_extra[i].rgValid) continue;
		sum += g_extra[i].trackGainDb;
		n++;
	}
	if (n <= 0) return;
	float album = (float)(sum / (double)n);
	for (int i = 0; i < g_extraCount; ++i) {
		if (_tcsicmp(g_extra[i].listName, listName) != 0) continue;
		if (!g_extra[i].rgValid) continue;
		g_extra[i].albumGainDb = album;
		g_extra[i].albumRgValid = 1;
	}
	g_extraDirty = true;
	ProAudio_SaveExtras();
}

// ============================================================================
// A/B
// ============================================================================
void ProAudio_AbCapture(int slot)
{
	if (slot < 0 || slot > 1) return;
	ProAbSlot& s = g_ab[slot];
	s.valid = 1;
	for (int i = 0; i < 20; ++i) s.eq[i] = savedata.eq[i];
	s.eqsoundenv = savedata.eqsoundenv;
	s.eqsoundeq = savedata.eqsoundeq;
	s.eqsoundeffect = savedata.eqsoundeffect;
	s.eq_reverb = savedata.eq_reverb;
	s.eq_chorus = savedata.eq_chorus;
	s.eq_delay = savedata.eq_delay;
	s.tempoPos = tempo;
	s.pitchPos = pitch;
	s.pro_ms_width = savedata.pro_ms_width;
	s.pro_ms_mono = savedata.pro_ms_mono;
}

void ProAudio_AbApply(int slot)
{
	if (slot < 0 || slot > 1) return;
	ProAbSlot& s = g_ab[slot];
	if (!s.valid) return;
	for (int i = 0; i < 20; ++i) savedata.eq[i] = s.eq[i];
	savedata.eqsoundenv = s.eqsoundenv;
	savedata.eqsoundeq = s.eqsoundeq;
	savedata.eqsoundeffect = s.eqsoundeffect;
	savedata.eq_reverb = s.eq_reverb;
	savedata.eq_chorus = s.eq_chorus;
	savedata.eq_delay = s.eq_delay;
	tempo = s.tempoPos;
	pitch = s.pitchPos;
	savedata.pro_ms_width = ProClampI(s.pro_ms_width, 0, 200);
	savedata.pro_ms_mono = s.pro_ms_mono ? 1 : 0;
	g_abActive = slot;
	if (og && ::IsWindow(og->GetSafeHwnd())) {
		if (og->m_tempo_sl.GetSafeHwnd()) og->m_tempo_sl.SetPos(tempo);
		if (og->m_pitch_sl.GetSafeHwnd()) og->m_pitch_sl.SetPos(pitch);
		if (og->m_EqualizerDlg && ::IsWindow(og->m_EqualizerDlg->GetSafeHwnd()))
			og->m_EqualizerDlg->SyncSlidersFromSavedata();
	}
}

void ProAudio_AbToggle()
{
	if (g_abActive < 0) {
		ProAudio_AbCapture(0);
		g_abActive = 0;
		return;
	}
	int other = 1 - g_abActive;
	if (!g_ab[other].valid) {
		// 反対側が空: 現在を反対側へ保存し、そちらをアクティブに(A は維持)
		ProAudio_AbCapture(other);
		g_abActive = other;
		return;
	}
	ProAudio_AbCapture(g_abActive);
	ProAudio_AbApply(other);
}

int ProAudio_AbActiveSlot()
{
	return g_abActive;
}

// ============================================================================
// 相関
// ============================================================================
void ProAudio_CorrReset()
{
	g_corrSumLR = g_corrSumLL = g_corrSumRR = 0;
	g_corrCached = 1.0f;
	g_balCached = 0.0f;
}

void ProAudio_CorrFeed(const float* L, const float* R, int frames)
{
	if (!L || !R || frames <= 0) return;
	const double decay = 0.9995; // 指数移動
	for (int i = 0; i < frames; ++i) {
		double l = L[i], r = R[i];
		g_corrSumLR = g_corrSumLR * decay + l * r;
		g_corrSumLL = g_corrSumLL * decay + l * l;
		g_corrSumRR = g_corrSumRR * decay + r * r;
	}
	double d = sqrt(g_corrSumLL * g_corrSumRR);
	if (d >= 1e-12) {
		// 無音時は前回値を保持(0へ落とさない)
		double c = g_corrSumLR / d;
		if (c > 1.0) c = 1.0;
		if (c < -1.0) c = -1.0;
		g_corrCached = (float)c;
	}
	double el = g_corrSumLL + 1e-12;
	double er = g_corrSumRR + 1e-12;
	g_balCached = (float)((er - el) / (er + el));
}

float ProAudio_CorrValue() { return g_corrCached; }
float ProAudio_CorrBalance() { return g_balCached; }

void ProAudio_FeedMetersFromInterleaved(const void* interleaved, int byteLen, int sampleRate, int bits, int channels)
{
	if (!interleaved || byteLen <= 0 || sampleRate <= 0 || channels < 1) return;
	const int bps = bits / 8;
	if (bps <= 0) return;
	const int bpf = bps * channels;
	if (bpf <= 0) return;
	byteLen -= byteLen % bpf;
	int frames = byteLen / bpf;
	if (frames <= 0) return;
	// ブロック上限(スタック固定配列)
	const int CAP = 2048;
	float L[2048], R[2048];
	const unsigned char* p = (const unsigned char*)interleaved;
	int done = 0;
	while (done < frames) {
		int n = frames - done;
		if (n > CAP) n = CAP;
		for (int i = 0; i < n; ++i) {
			float l, r;
			PcmInterleavedToFloat(p + (done + i) * bpf, bits, l, r, channels);
			L[i] = l;
			R[i] = r;
		}
		ProAudio_LoudnessFeed(L, R, n, sampleRate);
		if (savedata.pro_corr_meter && channels >= 2)
			ProAudio_CorrFeed(L, R, n);
		done += n;
	}
}

// ============================================================================
// Mid/Side
// ============================================================================
void ProAudio_ApplyMS(float* L, float* R, int frames, int widthPct, int monoCheck)
{
	if (!L || !R || frames <= 0) return;
	if (monoCheck) {
		for (int i = 0; i < frames; ++i) {
			float m = 0.5f * (L[i] + R[i]);
			L[i] = R[i] = m;
		}
		return;
	}
	if (widthPct == 100) return;
	float w = (float)widthPct / 100.0f;
	if (w < 0.0f) w = 0.0f;
	if (w > 2.0f) w = 2.0f;
	for (int i = 0; i < frames; ++i) {
		float mid = 0.5f * (L[i] + R[i]);
		float side = 0.5f * (L[i] - R[i]) * w;
		L[i] = mid + side;
		R[i] = mid - side;
	}
}

// ============================================================================
// Export True Peak limiter
// ============================================================================
void ProAudio_ExportLimitReset()
{
	g_tpEnv = 1.0f;
}

static float PeakBetweenSamples(float a, float b)
{
	float m = fabsf(a);
	float bb = fabsf(b);
	if (bb > m) m = bb;
	// 4x 相当の線形補間ピーク(True Peak 近似)
	for (int k = 1; k <= 3; ++k) {
		const float t = (float)k * 0.25f;
		const float v = fabsf(a + (b - a) * t);
		if (v > m) m = v;
	}
	return m;
}

static float Poly4Peak(float a, float b, float c, float d)
{
	float m = PeakBetweenSamples(a, b);
	float m2 = PeakBetweenSamples(c, d);
	return (m2 > m) ? m2 : m;
}

void ProAudio_ExportLimitProcess(float* L, float* R, int frames, int sampleRate, float ceilingLin, int truePeak)
{
	if (!L || !R || frames <= 0 || sampleRate <= 0) return;
	if (ceilingLin < 0.1f) ceilingLin = 0.1f;
	if (ceilingLin > 1.0f) ceilingLin = 1.0f;

	const float atk = expf(-1.0f / (0.0005f * (float)sampleRate));
	const float rel = expf(-1.0f / (0.100f * (float)sampleRate));

	for (int i = 0; i < frames; ++i) {
		float peak = fabsf(L[i]);
		if (fabsf(R[i]) > peak) peak = fabsf(R[i]);
		if (truePeak) {
			if (i + 1 < frames) {
				float tp = Poly4Peak(L[i], L[i + 1], R[i], R[i + 1]);
				if (tp > peak) peak = tp;
			}
			else {
				// 最終サンプルもサンプルピークは既に反映済み
			}
		}
		float need = (peak > ceilingLin) ? (ceilingLin / peak) : 1.0f;
		float coeff = (need < g_tpEnv) ? atk : rel;
		g_tpEnv = need + coeff * (g_tpEnv - need);
		if (g_tpEnv > 1.0f) g_tpEnv = 1.0f;
		L[i] *= g_tpEnv;
		R[i] *= g_tpEnv;
	}
}

// ============================================================================
// Cues / Loop
// ============================================================================
void ProAudio_CueLoadForCurrent()
{
	g_cueCount = 0;
	ZeroMemory(g_cues, sizeof(g_cues));
	ProSongExtra e;
	if (!ProAudio_GetExtra(g_curList, g_curPath, g_curMode, g_curRet2, e))
		return;
	g_cueCount = e.cueCount;
	if (g_cueCount > PRO_CUE_MAX) g_cueCount = PRO_CUE_MAX;
	for (int i = 0; i < g_cueCount; ++i)
		g_cues[i] = e.cues[i];
}

void ProAudio_CueSaveForCurrent()
{
	if (g_curPath[0] == 0) return;
	ProSongExtra e;
	ZeroMemory(&e, sizeof(e));
	int idx = FindExtraIndex(g_curList, g_curPath, g_curMode, g_curRet2);
	if (idx >= 0) e = g_extra[idx];
	else {
		_tcsncpy(e.listName, g_curList, 255);
		_tcsncpy(e.path, g_curPath, 1023);
		e.mode = g_curMode;
		e.ret2 = g_curRet2;
		e.loopIn = -1;
		e.loopOut = -1;
	}
	e.cueCount = g_cueCount;
	for (int i = 0; i < PRO_CUE_MAX; ++i)
		e.cues[i] = g_cues[i];
	ProAudio_UpsertExtra(e);
	ProAudio_SaveExtras();
}

int ProAudio_CueAdd(int frame, LPCTSTR label)
{
	if (frame < 0) return -1;
	if (g_cueCount >= PRO_CUE_MAX) return -1;
	g_cues[g_cueCount].frame = frame;
	g_cues[g_cueCount].label[0] = 0;
	if (label) {
		_tcsncpy(g_cues[g_cueCount].label, label, 31);
		g_cues[g_cueCount].label[31] = 0;
	}
	g_cueCount++;
	ProAudio_CueSaveForCurrent();
	return g_cueCount - 1;
}

bool ProAudio_CueGet(int index, ProCue& out)
{
	if (index < 0 || index >= g_cueCount) return false;
	out = g_cues[index];
	return true;
}

int ProAudio_CueCount() { return g_cueCount; }

void ProAudio_CueClearAll()
{
	g_cueCount = 0;
	ZeroMemory(g_cues, sizeof(g_cues));
	ProAudio_CueSaveForCurrent();
}

bool ProAudio_CueRemove(int index)
{
	if (index < 0 || index >= g_cueCount) return false;
	for (int i = index; i < g_cueCount - 1; ++i)
		g_cues[i] = g_cues[i + 1];
	g_cueCount--;
	ProAudio_CueSaveForCurrent();
	return true;
}

void ProAudio_GetLoopOverride(int& inFrame, int& outFrame, int& fadeMs)
{
	inFrame = outFrame = -1;
	fadeMs = 0;
	ProSongExtra e;
	if (!ProAudio_GetExtra(g_curList, g_curPath, g_curMode, g_curRet2, e))
		return;
	inFrame = e.loopIn;
	outFrame = e.loopOut;
	fadeMs = e.loopFadeMs;
}

void ProAudio_ApplyLoopOverrideToGlobals()
{
	extern int loop1, loop2;
	int oIn = -1, oOut = -1, oFade = 0;
	ProAudio_GetLoopOverride(oIn, oOut, oFade);
	if (oIn >= 0) loop1 = oIn;
	if (oOut >= 0) loop2 = oOut;
	(void)oFade; // ループ内フェードは再生エンジン未接続(上書き区間のみ反映)
}

void ProAudio_SetLoopOverride(int inFrame, int outFrame, int fadeMs)
{
	if (g_curPath[0] == 0) return;
	ProSongExtra e;
	ZeroMemory(&e, sizeof(e));
	int idx = FindExtraIndex(g_curList, g_curPath, g_curMode, g_curRet2);
	if (idx >= 0) e = g_extra[idx];
	else {
		_tcsncpy(e.listName, g_curList, 255);
		_tcsncpy(e.path, g_curPath, 1023);
		e.mode = g_curMode;
		e.ret2 = g_curRet2;
	}
	e.loopIn = inFrame;
	e.loopOut = outFrame;
	e.loopFadeMs = fadeMs;
	ProAudio_UpsertExtra(e);
	ProAudio_SaveExtras();
}

// WAV/PCM 風の単純ピーク概観。非対応形式は 0。
int ProAudio_BuildWaveOverview(LPCTSTR path, float* peaksL, float* peaksR, int maxPeaks, int& outTotalFrames)
{
	outTotalFrames = 0;
	if (!path || !peaksL || !peaksR || maxPeaks <= 0) return 0;
	if (maxPeaks > PRO_WAVE_PEAKS) maxPeaks = PRO_WAVE_PEAKS;

	CFile f;
	if (f.Open(path, CFile::modeRead | CFile::shareDenyNone, NULL) != TRUE)
		return 0;

	BYTE hdr[64];
	if (f.Read(hdr, 12) != 12) { f.Close(); return 0; }
	if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
		f.Close();
		return 0; // WAV のみ概観(他形式はループ数値編集で対応)
	}

	int channels = 2, bits = 16, rate = 44100;
	DWORD dataSize = 0;
	ULONGLONG dataPos = 0;
	for (;;) {
		BYTE ch[8];
		if (f.Read(ch, 8) != 8) break;
		DWORD id = *(DWORD*)ch;
		DWORD sz = *(DWORD*)(ch + 4);
		ULONGLONG next = f.GetPosition() + sz + (sz & 1);
		if (id == 0x20746d66) { // 'fmt '
			BYTE fmt[32];
			UINT n = (UINT)(sz > 32 ? 32 : sz);
			if (f.Read(fmt, n) != n) break;
			channels = *(WORD*)(fmt + 2);
			rate = *(DWORD*)(fmt + 4);
			bits = *(WORD*)(fmt + 14);
			(void)rate;
		}
		else if (id == 0x61746164) { // 'data'
			dataPos = f.GetPosition();
			dataSize = sz;
			break;
		}
		f.Seek(next, CFile::begin);
	}
	if (dataSize == 0 || channels <= 0 || bits <= 0) { f.Close(); return 0; }

	int bpf = (bits / 8) * channels;
	if (bpf <= 0) { f.Close(); return 0; }
	int totalFrames = (int)(dataSize / (DWORD)bpf);
	outTotalFrames = totalFrames;
	if (totalFrames <= 0) { f.Close(); return 0; }

	int peaks = maxPeaks;
	if (peaks > totalFrames) peaks = totalFrames;
	ZeroMemory(peaksL, sizeof(float) * peaks);
	ZeroMemory(peaksR, sizeof(float) * peaks);

	const int block = 8192;
	BYTE buf[8192 * 8];
	f.Seek(dataPos, CFile::begin);
	int frame = 0;
	while (frame < totalFrames) {
		int framesLeft = totalFrames - frame;
		int n = framesLeft;
		if (n * bpf > (int)sizeof(buf)) n = (int)sizeof(buf) / bpf;
		UINT got = f.Read(buf, n * bpf);
		int gotFrames = (int)(got / bpf);
		if (gotFrames <= 0) break;
		for (int i = 0; i < gotFrames; ++i) {
			float L, R;
			PcmInterleavedToFloat(buf + i * bpf, bits, L, R, channels);
			int bin = (int)((__int64)(frame + i) * peaks / totalFrames);
			if (bin < 0) bin = 0;
			if (bin >= peaks) bin = peaks - 1;
			float aL = fabsf(L), aR = fabsf(R);
			if (aL > peaksL[bin]) peaksL[bin] = aL;
			if (aR > peaksR[bin]) peaksR[bin] = aR;
		}
		frame += gotFrames;
	}
	f.Close();
	return peaks;
}
