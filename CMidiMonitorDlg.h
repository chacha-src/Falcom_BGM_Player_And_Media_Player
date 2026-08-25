#pragma once
// CMidiMonitorDlg : MIDI 32パート・モニタ（XG/GS 風）
// SMF を再生位置に同期して CC/ノート/SysEx を表示。音色名は SASAMI_GS/XG/EX.DAT。
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "GdiSoft3D.h"

class CMidiMonitorDlg : public CCustomBlurDialogExBase
{
	DECLARE_DYNAMIC(CMidiMonitorDlg)

public:
	CMidiMonitorDlg(CWnd* pParent = nullptr);
	virtual ~CMidiMonitorDlg();

#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_MIDIMONITOR };
#endif

	static constexpr int PART_MAX = 32;
	static constexpr int NOTE_MAX = 128;
	static constexpr int NAME_CHARS = 24;
	static constexpr int EV_MAX = 500000;

	struct MmEv {
		unsigned __int64 tick;
		__int64 sample;
		DWORD msg;
		DWORD aux;
		int port;
		int sysexOff;
	};

	void PumpSyncNow();
	void ResetPlaybackState();
	void DetachForDestroy();
	void PaletteApplySoft3D();
	void PersistSoft3D();
	void LayoutHelpBtn();
	void ShowHelpSheet();
	GdiSoft3D::Cam m_cam;
	void ApplyMapForce(int force);
	const wchar_t* LoadedMidiPath() const { return m_loadedPath; }

protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	virtual BOOL OnInitDialog();
	DECLARE_MESSAGE_MAP()

	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
	afx_msg void OnClose();
	afx_msg void OnDestroy();
	afx_msg void OnBnClickedHelp();
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg BOOL OnTtnNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);
	virtual BOOL PreTranslateMessage(MSG* pMsg);

private:
	struct Part {
		int pc;
		int bankMsb;
		int bankLsb;
		int mapId;
		int vol;
		int exp;
		int pan;
		int rev;
		int crs;
		int var;
		int dt;
		int vibRat;
		int vibDpt;
		int vibDly;
		int lpf;
		int rsn;
		int hpf;
		int atk;
		int dcy;
		int rls;
		int eqLow;
		int eqHigh;
		int nrpnMsb;
		int nrpnLsb;
		int rpnMsb;
		int rpnLsb;
		int dataMsb;
		BYTE noteOn[NOTE_MAX];
		BYTE noteFlash[NOTE_MAX];
		int lastNote;
		int lastVel;
		int isDrum;
		int held;
		int rxCh;   // 0-15, 16=off (GS Rx Channel)
		int rxPort; // 0=A 1=B 2=both
		float lev;
		BYTE glowVol;
		BYTE glowExp;
		BYTE glowPan;
		BYTE glowRev;
		BYTE glowCrs;
		BYTE glowVar;
		BYTE fadeCh;
		BYTE fadeInst;
		BYTE fadeVib;
		BYTE fadeFilt;
		BYTE fadeEnv;
		BYTE fadeEq;
		BYTE fadeNrpn;
		BYTE heard;
		BYTE efxOn;
		wchar_t name[NAME_CHARS];
	};

	Part m_show[PART_MAX];
	int m_showBpm, m_showTpc, m_showNotes, m_showPeak, m_showVol, m_showSys;
	int m_showRev, m_showCho, m_showVar, m_showVarPacked, m_showVarConn, m_showIns1, m_showIns2, m_showDrum;
	int m_showRevPacked, m_showChoPacked;
	int m_showDiv, m_showTsN, m_showTsD, m_showTransp, m_showKeySf, m_showKeyMin, m_showFrozen;
	int m_showBar, m_showBars, m_showBeat, m_showTick, m_showTpm, m_showNum;
	wchar_t m_showTitle[280];

	void ReleasePaintBuffers();
	bool EnsureFrameBuffer(CDC& refDC, int w, int h);
	void DrawMonitor2D(CDC& dc, int w, int h, UINT dpi);
	void DrawMonitor3D(CDC& dc, int w, int h);
	void DrawMiniKeys(CDC& dc, const CRect& rc, const Part& p, COLORREF keyW, COLORREF keyB);
	void DrawVBar(CDC& dc, int x, int y, int bw, int bh, int v0, int vmax, COLORREF col, int glow, int idle);
	void DrawPanBar(CDC& dc, int x, int y, int bw, int bh, int pan, int glow, int idle);
	void DrawHeader(CDC& dc, int w, int headH, UINT dpi);
	void DrawInsFoot(CDC& dc, int y, int w, int footH, UINT dpi);
	void DrawPartRow(CDC& dc, int i, int y, int rowH, int w, UINT dpi, int forceKeys);
	void BuildInsLine(int slot, wchar_t* out, int outN);
	void TickVisuals();
	void TickNotePeak();
	void UpdateNoteMeter();
	void PollAppVolume();
	void InvalidateDirty();
	int AppVolPercent() const;
	void SetAppVolPercent(int pct);
	bool HitVolBar(CPoint clientPt) const;
	int HitMonitor(CPoint clientPt, int& part, CRect& cell) const;
	int KeyAt(const CRect& rc, CPoint pt) const;
	void InjectShort(int part, DWORD msg);
	void DrainLiveTap();
	void SnapshotLiveNotes();
	void MarkHostOccupiedParts();
	void LatchPart(int part, BYTE bit);
	bool IsLatched(int part, BYTE bit) const;
	void ApplyDragValue(CPoint clientPt);
	void ReleasePlayNote();
	void SyncFromPlayback();
	void UpdatePlayPos();
	void ApplyDueEvents(int lastDue);
	void LoadCurrentMidi();
	void UnloadMidi();
	void ResetParts();
	void InitPartDefaults(int i, BYTE heard);
	void ResetPartsBank(int port);
	void ApplyEvent(const MmEv& e);
	void ApplyShort(int port, DWORD msg, BOOL fromUser = FALSE, BOOL liveExact = FALSE);
	void ApplySysex(const BYTE* d, int n, int livePort = -1);
	void ApplyNrpn(Part& p);
	void RefreshPartName(Part& p);
	void LookupToneName(int isXg, int mapId, int bankMsb, int bankLsb, int pc, int isDrum, wchar_t* out, int outN);
	void PersistPos();
	void SyncSoft3DFromSave();
	bool IsView3D() const { return m_viewMode == 1; }
	int Scale(int v96, UINT dpi) const { return MulDiv(v96, (int)dpi, 96); }
	UINT WindowDpi() const;

	CCustomStandardButton m_help;
	CToolTipCtrl m_tooltip;
	TCHAR m_hoverTip[256];

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

	CFont m_fontHead;
	CFont m_fontCell;
	CFont m_fontTiny;
	int m_fontDpi;
	int m_fontH;

	Part m_part[PART_MAX];
	MmEv* m_ev;
	int m_evCount;
	int m_evPos;
	int m_hadNote;
	__int64 m_hearPlayb;
	BYTE* m_sx;
	int m_sxBytes;
	int m_division;
	int m_sampleRate;
	__int64 m_lastPlayb;
	wchar_t m_loadedPath[520];
	wchar_t m_titleBuf[280];
	int m_gsMapKind; // 0=なし 1=55 2=88 3=88Pro 4=8820 5=GM 6=SD 8=LA 9..=ETC
	int m_fileHasXg;
	int m_fileHasGm;
	int m_fileHasSd;
	int m_gs32;      // GS Port B / XG 17-32 SysEx
	int m_mirrorToB; // no FF 21: channel MIDI arrives on both cables
	struct MmTsEv {
		unsigned __int64 tick;
		int num;
		int den;
	};
	MmTsEv m_tsEv[64];
	int m_tsEvN;
	unsigned __int64 m_maxTick;
	int m_posBar, m_posBars, m_posBeat, m_posTick, m_posTpm, m_posNum;

	int m_usecQn;
	int m_tsNum;
	int m_tsDen;
	int m_keySf;
	int m_keyMin;
	int m_transpose;
	int m_sysMode; // 0=GM 1=GS 2=XG
	int m_revType;
	int m_choType;
	int m_varType;
	int m_revPacked;
	int m_choPacked;
	int m_varPacked; // XG variation TYPE (MSB<<8)|LSB; 0 until SysEx
	int m_varConn;   // XG 02 01 5A: 0=INSERTION, 1=SYSTEM
	int m_ins1;      // XG TYPE (MSB<<8)|LSB, or GS EFX (MSB<<8)|LSB
	int m_ins2;
	BYTE m_gsEfx[32];
	BYTE m_gsEfxHasLsb;
	DWORD m_gsEfxMask;
	BYTE m_insBlk[2][24];
	BYTE m_varBlk[32];
	int m_noteCount;
	int m_masterVol;
	float m_notesPeak;
	int m_notesPeakHold;
	int m_layW;
	int m_dragKind;
	int m_dragPart;
	int m_playPart;
	int m_playNote;
	DWORD m_latchUntil[PART_MAX];
	BYTE m_latchMask[PART_MAX];

	int m_viewMode;
	int m_mapForce;
	bool m_frozen;
	bool m_alwaysOnTop;
	bool m_paintDisabled;
	bool m_rotDragging;
	CPoint m_rotDragOrigin;
	float m_rotDragYaw0;
	float m_rotDragPitch0;
	DWORD m_soft3dTourUntil;
	int m_hoverCol;
	int m_hoverPart;
	int m_layHeadH;
	int m_layRowH;
	int m_layFootH;
	int m_persistAge;
	int m_drumGlow;
	int m_dispBpm;
	DWORD m_dirtyRows;
	DWORD m_rowLive;
	DWORD m_nameNeed;
	int m_burstApply;
	bool m_dirtyHead;
	bool m_fullDraw;
	bool m_volDragging;
	CRect m_volBarRc;
	CRect m_notesBarRc;
	wchar_t m_plugShown[PART_MAX][40];
	wchar_t m_insLine[2][220];
	wchar_t m_showInsLine[2][220];
};
