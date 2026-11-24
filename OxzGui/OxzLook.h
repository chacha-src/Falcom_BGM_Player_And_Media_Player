#pragma once
#include <windows.h>

/* 本体の見た目テーマと同じ番号。保存先は HKCU\Software\OxzGui。savedata は使わない。 */
enum { OXZ_THEME_COUNT = 38, OXZ_THEME_GARDEN = 35, OXZ_FACE_MAX = 64 };

struct OxzPal {
	COLORREF bg0, bg1;
	COLORREF ribbon0, ribbon1, ribbonEdge;
	COLORREF accent, accent2, sep;
	COLORREF face, edge, track;
	COLORREF text, textDim;
	COLORREF hotTop, hotBot;
	COLORREF borderLite, borderDark;
};

enum {
	ID_LOOK_BOLD = 1401,
	ID_LOOK_ITALIC = 1402,
	ID_LOOK_SIZE0 = 1410,
	ID_LOOK_FACE0 = 1500,
	ID_LOOK_ACRYLIC = 1601,
	ID_LOOK_SOFT = 1602,
	ID_LOOK_THEME0 = 1700,
	ID_LOOK_ANIM0 = 1800
};

enum { OXZ_ANIM_COUNT = 10, OXZ_ANIM_AURORA = 9 };

struct OxzPopAdd {
	UINT id;
	const wchar_t* text;
	int sep;
};

UINT OxzOpenLookMenu(HWND owner, int x, int y, const OxzPopAdd* add, int count);

void OxzLookLoad();
const OxzPal& OxzLook();
int OxzTheme();
int OxzThemeCount();
const wchar_t* OxzThemeName(int id);
int OxzFontPt();
int OxzFontBold();
int OxzFontItalic();
const wchar_t* OxzFontFace();
int OxzAcrylic();
int OxzSoft();
int OxzCorner();
COLORREF OxzInk(COLORREF bg);
HFONT OxzCreateUiFont(HWND hwnd);
void OxzApplyChrome(HWND hwnd);
void OxzPaintButton(const DRAWITEMSTRUCT* di, HFONT font);
void OxzRowColors(int row, int sel, COLORREF* bg, COLORREF* fg);
int OxzLookCommand(UINT id);
int OxzMenuAnim();
int OxzPerPixelAcrylic();
void OxzSetLookRefresh(void (*fn)());
void OxzHoverTheme(int id);
void OxzFinishThemePreview(int commit);
void OxzTintCaption(HWND hwnd);
void OxzRefreshChrome(HWND hwnd);
void OxzBindAcrylic(HWND hwnd);
void OxzSetBarInk(HWND hwnd, COLORREF ink);
int OxzHostErase(HWND hwnd, HDC dc, HBRUSH br);
void OxzHostPaint(HWND hwnd);
HBRUSH OxzHostCtlColor(HDC dc, UINT msg, HBRUSH plate);
HDC OxzPlateBegin(void** hold, HDC ref, int w, int h, COLORREF fill);
void OxzPlateEnd(void* hold, HDC dst, int x, int y, int chroma);
