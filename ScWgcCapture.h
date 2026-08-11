#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Windows.Graphics.Capture で HWND を top-down BGRA に取得する。
// 他窓やキャプチャUIが前面でも対象ウィンドウ本体を取れる（OBS と同系統）。
// dstW/dstH は偶数推奨。失敗時は FALSE（呼び出し側で GDI フォールバック）。
BOOL ScWgcCaptureWindowBgra(HWND hwnd, BYTE* dstBgra, int dstW, int dstH, int dstStride);

// ウィンドウ内の矩形だけを dst へ（cropW/cropH<=0 なら全体）。ウィンドウ座標系。
BOOL ScWgcCaptureWindowBgraCrop(HWND hwnd, BYTE* dstBgra, int dstW, int dstH, int dstStride,
	int cropX, int cropY, int cropW, int cropH);

// モニタ全体を WGC + GPU 縮小で取得（プライマリ/サブ兼用。GDI StretchBlt より高速）
BOOL ScWgcCaptureMonitorBgra(HMONITOR mon, BYTE* dstBgra, int dstW, int dstH, int dstStride);

// キャンバス BGRA にポストエフェクト（in-place）。effect / chain は SC_FX_*。
enum {
	SC_FX_NONE = 0,
	SC_FX_BLUR_SOFT = 1,
	SC_FX_BLUR_STRONG = 2,
	SC_FX_GRAY = 3,
	SC_FX_SEPIA = 4,
	SC_FX_VIGNETTE = 5,
	SC_FX_SHARPEN = 6,
	SC_FX_MIRROR = 7,
	SC_FX_WAVE = 8,
	SC_FX_UNDERWATER = 9,
	SC_FX_DUSK = 10,
	SC_FX_COOL = 11,
	SC_FX_WARM = 12,
	SC_FX_POSTER = 13,
	SC_FX_SCANLINE = 14,
	SC_FX_EDGE = 15,
	SC_FX_INVERT = 16,
	SC_FX_SOLARIZE = 17,
	SC_FX_PIXELATE = 18,
	SC_FX_FLIP_V = 19,
	SC_FX_NOISE = 20,
	SC_FX_BLOOM = 21,
	SC_FX_NEON = 22,
	SC_FX_NIGHTVISION = 23,
	SC_FX_COMIC = 24,
	SC_FX_RETRO = 25,
	SC_FX_FISHEYE = 26,
	SC_FX_HUE_SHIFT = 27,
	SC_FX_CONTRAST = 28,
	SC_FX_BRIGHTNESS = 29,
	SC_FX_SATURATE = 30,
	SC_FX_DESAT = 31,
	SC_FX_THRESHOLD = 32,
	SC_FX_RED_CAST = 33,
	SC_FX_GREEN_CAST = 34,
	SC_FX_BLUE_CAST = 35,
	SC_FX_CYAN = 36,
	SC_FX_MAGENTA = 37,
	SC_FX_YELLOW = 38,
	SC_FX_TEAL_ORANGE = 39,
	SC_FX_FADE = 40,
	SC_FX_SPOTLIGHT = 41,
	SC_FX_BARS_H = 42,
	SC_FX_BARS_V = 43,
	SC_FX_CHROMA = 44,
	SC_FX_EMBOSS = 45,
	SC_FX_LIFT = 46,
	SC_FX_MONO_BLUE = 47,
	SC_FX_MONO_GREEN = 48,
	SC_FX_QUAD = 49,
	SC_FX_DUO_PURPLE = 50,
	SC_FX_BLUR_MEGA = 51,
	SC_FX_MOTION_BLUR = 52,
	SC_FX_RADIAL_BLUR = 53,
	SC_FX_ZOOM_BLUR = 54,
	SC_FX_SWIRL = 55,
	SC_FX_RIPPLE = 56,
	SC_FX_VORTEX = 57,
	SC_FX_HEAT_HAZE = 58,
	SC_FX_GLITCH = 59,
	SC_FX_CRT_CURVE = 60,
	SC_FX_KALEIDO = 61,
	SC_FX_OIL = 62,
	SC_FX_WATERCOLOR = 63,
	SC_FX_PENCIL = 64,
	SC_FX_DREAM = 65,
	SC_FX_GODRAYS = 66,
	SC_FX_DISPLACE = 67,
	SC_FX_INTERLACE = 68,
	SC_FX_CHROMA_HEAVY = 69,
	SC_FX_FOG = 70,
	SC_FX_SHARPEN_HEAVY = 71,
	SC_FX_COUNT = 72
};

enum { SC_FX_CHAIN_MAX = 8 };
enum { SC_FX_STR_N = 8 };     // S1..S8（0..8、既定4=等倍）
enum { SC_FX_STR_DEF = 4 };
enum { SC_FX_STR_MAX = 8 };

// 単発（chain n=1 の薄いラッパ）
BOOL ScGpuApplyEffect(BYTE* bgra, int w, int h, int stride, int effect);

// 線形チェーン（n<=SC_FX_CHAIN_MAX）。GPU: 1 upload → ping-pong → 1 download。
// timeSec は波/水中アニメ用。effects[i] は SC_FX_*（NONE は無視）。
// str[i][0..7] は S1..S8（0..8）。nullptr ならすべて SC_FX_STR_DEF。
BOOL ScGpuApplyEffectChain(BYTE* bgra, int w, int h, int stride,
	const int* effects, int n, float timeSec,
	const BYTE str[][SC_FX_STR_N]);

// 全セッション解放（ダイアログ終了時など）
void ScWgcShutdown(void);
// キャプチャセッションのみ破棄（D3D/エフェクトは維持）。録画開始時の WGC 固着対策。
void ScWgcReleaseSessions(void);
// 指定 HWND のウィンドウセッションだけ破棄（曲切替で動画窓を閉じる直前など）
void ScWgcReleaseWindow(HWND hwnd);

#ifdef __cplusplus
}
#endif
