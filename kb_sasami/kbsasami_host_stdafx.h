#pragma once
/* kbsasami_host は VstMidiEngine と同じプロセスで、本体の FM/MIDI モニタ dlg を出す。
   エンジン側の save は kpihost_stdafx。その前に MFC を入れる。 */
#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#include <afxwin.h>
#include <afxext.h>
#include <afxcmn.h>
#include <afxdialogex.h>
#include "KpiHost32/kpihost_stdafx.h"

struct playlistdata0 {
	TCHAR name[1024];
	TCHAR art[1024];
	TCHAR alb[1024];
	TCHAR fol[1024];
	int sub;
	TCHAR game[256];
	int loop1;
	int loop2;
	int ret2;
	int icon;
	int time;
};

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

CWnd* CCC_GetActiveMainWindow();
void CCC_MainLockSetup(CWnd* pDlg, int* pSavedLockFlag, BOOL bOverlayPaint = FALSE);
void CCC_MainLockUnregister(HWND hWnd);
void CCC_ClampWindowPos(int& x, int& y, int w, int h);
void CCC_MainLockOnChildMoving(CWnd* pDlg, LPRECT pRect);
BOOL CCC_MainLockOverlayHitTest(HWND hDlg, CPoint ptClient);
int CCC_GetCustomCaptionHeight(HWND hDlg);
BOOL CCC_AcrylicCaption(HWND hWnd);
void CCC_CaptionEnsureHostAcrylic(HWND hWnd);
void CCC_CaptionPaint(CDC& dc, HWND hDlg);
void CCC_CaptionPaintGdi(CDC& dc, HWND hDlg);
void CCC_MainLockPaintClient(CDC& dc, HWND hDlg);
int CCC_MainLockGetReserveWidth(HWND hDlg);
void CCC_MainLockGetOverlayRect(HWND hDlg, CRect& rc);
void CCC_MainLockBringToFront(HWND hDlg);
void CCC_CaptionLayout(HWND hDlg);
void CCC_CaptionUnregister(HWND hDlg);
void CCC_CaptionPlaceHelpBtn(HWND hDlg, CWnd* pHelp);
void CCC_PresentOwnedHelp(CWnd* help, CWnd* owner);
void CCC_InvalidateRectMinusOverlay(HWND hDlg, const CRect& area);
struct CCC_GdiHelpPaint {
	CWnd* wnd;
	CDC* pPaintDc;
	CDC mem;
	CBitmap bmp;
	CBitmap* oldBmp;
	void* bits;
	int bw;
	int bh;
	int footerH;
	CRect rc;
	BOOL ok;
	CCC_GdiHelpPaint()
		: wnd(NULL), pPaintDc(NULL), oldBmp(NULL), bits(NULL)
		, bw(0), bh(0), footerH(26), ok(FALSE) {}
};
BOOL CCC_GdiHelpBeginPaint(CWnd* wnd, CDC& paintDc, CCC_GdiHelpPaint& hp);
void CCC_GdiHelpEndPaint(CCC_GdiHelpPaint& hp);
enum {
	CCC_HELPDEMO_KGENERIC = 0,
	CCC_HELPDEMO_KSPECTRUM,
	CCC_HELPDEMO_KWAVE,
	CCC_HELPDEMO_KPIANO,
	CCC_HELPDEMO_KEQ,
	CCC_HELPDEMO_KCAPTURE,
	CCC_HELPDEMO_KCMDROLL,
	CCC_HELPDEMO_KLIST,
	CCC_HELPDEMO_KTRANSPORT,
	CCC_HELPDEMO_KMAZE,
	CCC_HELPDEMO_KRACE,
	CCC_HELPDEMO_KMIDIMON
};
int CCC_GdiHelpDrawSoft3DDemo(CDC& dc, int x, int y, int maxW, int maxH, int kind);
int CCC_GdiHelpDrawSoftDemoPair(CDC& dc, int x, int y, int totalW, int demoH, int kind);
