#pragma once
// CFmMonitorDlg : SASAMI FPY / OPNA (YM2608) レジスタ・鍵盤モニタ
// KPI/SASAMI: %TEMP%\ogg_kbsasami\*.opna
// CEmu:       %TEMP%\ogg_cemu\*.opna （混ぜない）
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "kb_sasami/source/sasami_fmmon.h"
#include "gpu/GpuDx11.h"

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
	void PumpSyncNow(); /* timerp: 可聴位置へ dump 同期（UpdateWindow は呼び出し側） */
	void DetachForDestroy();
	void LayoutHelpBtn();
	void PersistGeom();
	void RestoreGeom();
	void SetHosted(int hosted);
	int IsHosted() const { return m_hosted; }
	int BodyTop() const; /* hosted は 0。親キャプションの下に載せる */
	void ApplyPcAudioKeys(const BYTE levels108[108]); /* 無演奏時のPC音鍵盤 */

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	DECLARE_MESSAGE_MAP()

	afx_msg void OnPaint();
	afx_msg LRESULT OnPrint(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnBnClickedHelp();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg LRESULT OnComposeDone(WPARAM wParam, LPARAM lParam);

private:
	enum { HIST_MAX = 512 }; /* リング容量（keys-only 高解像度用） */
	/* .fpy ~700ms / keys-only ~750ms。短すぎると可聴前 dump を捨てて無描画 */
	enum { HIST_SOFT = 448 };

	int PollDump(); /* live/ring を読み可聴位置の dump を Apply */
	void ResetDumpSync(); /* 曲切替で履歴とハンドルを捨てる */
	void PushHistDump(const SasamiFmMonDump& d);
	void TrimHistForHeard(uint64_t heard, uint32_t rate);
	void ApplyDump(const SasamiFmMonDump& d); /* フェード・dirty・行数変化 */
	void TickFades();
	void InvalidateDirtyRegions();
	uint64_t HeardSample(uint32_t sampleRate); /* 今聞こえているサンプル */
	uint64_t AdvanceHeard(__int64 frames, uint32_t srDump);
	int PcmRows() const; /* PPZ/ADPCM/OPL3 を含む鍵盤 PCM 行 */
	int ExRows() const;  /* FM3EX / OPM7-8 / OPL7-9 */
	int FmRows() const;
	int SsgRows() const;
	int KeysOnly() const; /* レジスタ無しで鍵盤だけ */
	int IsMsxDump() const;
	int IsOpmDump() const;
	int IsOplDump() const;
	int IsYm2610Dump() const;
	int IsArcadePcmDump() const;
	unsigned MsxDevMask() const;
	unsigned ChipProfile() const; /* pad6[1] のチップ種別 */
	unsigned ViewCaps() const;    /* KEYS/REGS/PANELS */
	int HideRhythm() const;
	int HasViewRegs() const;
	int HasViewPanels() const;
	int PrimaryPanelN() const;
	int CompanionPanelN() const;
	int PanelGridPcmCompact() const; /* FMアルゴ無し。PCM余白を詰めて全ch収める */
	int PrimarySilent() const; /* ハイブリッドで主FMが一度も発音していない */
	int IsOpnThreeShell() const; /* YM2203/OPN+: OPNA の FM1-3。下は空欄 */
	int PanelLayoutN() const; /* グリッド枠。OPN は 6 */
	int TryGpuFrame(); /* DX11。失敗時は GDI */
	int HexBankCount() const;
	int HistPeekFmKey() const;
	void TickFmViewReady();
	/* 起動直後／keys-only(MIDI等)でチップUIが無いとき OPNA 殻を出す */
	int PreferOpnaShell() const;
	bool EnsureFrameBuffer(CDC& refDC, int w, int h);
	void PaintClientToDC(HDC hdc);
	void BlitCachedFrameToPrintDC(HDC hdc);
	void ReleasePaintBuffers();
	void ComputeLayout(int w, int h);
	void DrawHead(CDC& dc);
	void DrawHexArea(CDC& dc);
	void DrawPanelsArea(CDC& dc);
	void DrawKeysArea(CDC& dc);
	void ComposeFrame(CDC& dc, int w, int h);
	void StartComposeThread();
	void StopComposeThread();
	void KickCompose(int w, int h);
	int EnsureWorkBuffers(int w, int h);
	void ReleaseWorkBuffers();
	HDC FrontWorkDc();
	void ComposeThreadLoop();
	static UINT ComposeThreadProc(LPVOID p);
	void DrawHexBank(CDC& dc, int x, int y, int cellW, int cellH, int gapExtra, int bankBase, const wchar_t* title, int rowCount = 16);
	void DrawFmChPanel(CDC& dc, const CRect& rc, int ch);
	void DrawOpmChPanel(CDC& dc, const CRect& rc, int ch);
	void DrawOplChPanel(CDC& dc, const CRect& rc, int ch, int packedCompanion = 0);
	void DrawOpllChPanel(CDC& dc, const CRect& rc, int ch, int packedCompanion = 0);
	void DrawArcadePcmChPanel(CDC& dc, const CRect& rc, int ch, unsigned profile, int useComp = 0);
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
	uint64_t m_histSamp[HIST_MAX]; /* dump.curSample（デコード書き込み位置） */
	int m_histN;
	int m_histHead;
	BYTE m_fade[0x300];
	BYTE m_touched[0x300];
	BYTE m_fadeKey[6];
	BYTE m_fadeEx[3];
	BYTE m_fadeSsg[3];
	BYTE m_fadePcm[SASAMI_FMMON_PCM_MAX];
	BYTE m_fadeRzmPad[6];
	wchar_t m_lastSong[260];
	wchar_t m_playIdent[260]; /* 再生中 stem。曲切替で ring 位置を捨てる */
	uint32_t m_lastSeq;
	uint64_t m_lastCurSample;
	uint64_t m_lastHeardSamp;
	uint64_t m_heardAnchor;
	LONGLONG m_heardQpc;
	LONGLONG m_heardFreq;
	uint32_t m_ringGenLast; /* fmmon_ring.opna の消費済み gen */
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
	int m_hosted; /* 1=CMidiMonitorDlg の子。独自キャプション/位置保存をしない */
	ULONGLONG m_lastPollMs;
	int m_inPrint; /* PrintWindow / スクショ中。CPaintDC と Poll を混ぜない */
	int m_inPump; /* PumpSyncNow 再入防止（timerp / OnIdle / タイマ） */
	int m_lastPlayy; /* FmMonIsLive() の前回値。停止遷移で鍵盤クリア */
	int m_fmEverOn; /* この曲で主FM/OPMが一度でもキーオンした */
	int m_fmViewReady; /* 0=先読み中。決まり次第 hex/panels/keys を出す */
	ULONGLONG m_fmHoldMs;

	struct Layout {
		int w, h, dpi;
		int pad, headH, topY, topH, gapHexKeys;
		int cellW, cellH, gapExtra, hexX, hexColW;
		int gridY0, gridY1, gridY2, bankTitle, bankGap;
		int hexBanks;
		int panN, panCols, panRows;
		int fmX, fmW, pw, ph, gap;
		int keysY, keysW, rowH, keyH, labelW;
		int pcmRows; /* 鍵盤ブロック行数に効く。変化時は ComputeLayout 必須 */
		int exRows;
		int fmRows;
		int ssgRows;
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
	GpuMonSurf m_gpu;

	/* hex/panel/keys の GDI 合成は UI から外し、提示だけ UI が BitBlt する */
	CWinThread* m_composeThread;
	HANDLE m_composeWake;
	CRITICAL_SECTION m_dataCs;
	CRITICAL_SECTION m_bufCs;
	CDC m_workDC[2];
	CBitmap m_workBmp[2];
	CBitmap* m_workOld[2];
	int m_workW;
	int m_workH;
	volatile LONG m_composeFront;
	volatile LONG m_composeNeed;
	volatile LONG m_composeStop;
	volatile LONG m_composeCsReady;
	int m_composeReqW;
	int m_composeReqH;
};
