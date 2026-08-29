#pragma once
// CFmMonitorDlg : SASAMI FPY / OPNA (YM2608) レジスタ・鍵盤モニタ
// kbsasami (raira=1) が %TEMP%\ogg_kbsasami\*.opna に出す dump を同期表示。
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "kb_sasami/source/sasami_fmmon.h"

class CFmMonitorDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CFmMonitorDlg)

public:
	CFmMonitorDlg(CWnd* pParent = nullptr);
	virtual ~CFmMonitorDlg();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_FMMONITOR };
#endif

	void IdlePulse();
	void DetachForDestroy();
	void LayoutHelpBtn();
	void PersistGeom();
	void RestoreGeom();

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	DECLARE_MESSAGE_MAP()

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnBnClickedHelp();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);

private:
	enum { HIST_MAX = 128 };

	int PollDump();
	void ApplyDump(const SasamiFmMonDump& d);
	void TickFades();
	int DelayMs() const;
	int PcmRows() const;
	bool EnsureFrameBuffer(CDC& refDC, int w, int h);
	void ReleasePaintBuffers();
	void ComputeLayout(int w, int h);
	void DrawHead(CDC& dc);
	void DrawHexArea(CDC& dc);
	void DrawPanelsArea(CDC& dc);
	void DrawKeysArea(CDC& dc);
	void ComposeFrame(CDC& dc, int w, int h);
	void DrawHexBank(CDC& dc, int x, int y, int cellW, int cellH, int gapExtra, int bankBase, const wchar_t* title);
	void DrawFmChPanel(CDC& dc, const CRect& rc, int ch);
	void DrawPiano108(CDC& dc, const CRect& rc, int midiNote, int lit);
	void DrawChannelKeys(CDC& dc, int x, int y, int w, int rowH, int keyH, int labelW);
	static int ApproxMidiFromFnum(uint8_t a4, uint8_t a0);
	static double ApproxHzFromFnum(uint8_t a4, uint8_t a0);
	static int ApproxMidiFromSsg(uint16_t period);
	int ContentHeight(int dpi, int pcmRows) const;
	int PreferredWidth(int dpi) const;

	CCustomStandardButton m_help;
	SasamiFmMonDump m_dump;
	SasamiFmMonDump m_prev;
	SasamiFmMonDump m_hist[HIST_MAX];
	ULONGLONG m_histMs[HIST_MAX];
	int m_histN;
	int m_histHead;
	BYTE m_fade[0x200];
	BYTE m_touched[0x200];
	BYTE m_fadeKey[6];
	BYTE m_fadeSsg[3];
	BYTE m_fadePcm[SASAMI_FMMON_PCM_MAX];
	BYTE m_fadeRzmPad[6];
	wchar_t m_lastSong[260];
	uint32_t m_lastSeq;
	uint64_t m_lastCurSample;
	int m_haveDump;
	int m_dirtyHead;
	int m_dirtyHex;
	int m_dirtyPanels;
	int m_dirtyKeys;
	int m_fullDraw;
	BYTE m_panelDirtyMask; /* bit0..5 = FM CH1..6。ALG 含むパネル差分 */
	int m_readFail;
	int m_persistAge;
	int m_userClosing; /* 1=ユーザーが×で閉じた → fmmonwindow=0 */
	ULONGLONG m_lastPollMs;

	struct Layout {
		int w, h, dpi;
		int pad, headH, topY, topH, gapHexKeys;
		int cellW, cellH, gapExtra, hexX, hexColW;
		int gridY0, gridY1, bankTitle, bankGap;
		int fmX, fmW, pw, ph, gap;
		int keysY, keysW, rowH, keyH, labelW;
		CRect rcHead, rcHex, rcPanels, rcKeys;
	} m_lay;
	int m_layOk;

	CDC m_frameDC;
	CBitmap m_frameBmp;
	CBitmap* m_frameOld;
	int m_frameW;
	int m_frameH;
#if CCUSTOM_AERO_SUPPORT
	CCC_ChromaBlitCache m_chromaCache;
	int m_chromaW;
	int m_chromaH;
	bool m_chromaReady;
#endif
};
