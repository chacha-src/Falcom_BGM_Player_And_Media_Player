#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include <vector>

class CPianoRoll : public CCustomBlurDialogExBase
{
    DECLARE_DYNAMIC(CPianoRoll)

public:
    CPianoRoll(CWnd* pParent = nullptr);
    virtual ~CPianoRoll();

    void RequestSyncFromMainUi();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_PIANOROLL };
#endif

    void FeedPCM(const void* pData, int frames, int sampleRate, int bits, int channels,
        int playbackDelaySamples = 0);
    void AnalyzePlayCursorMono(const double* mono, int frameCount, int sampleRate);
    void SetChannelMeterDb(const float* dbPerChannel, int channelCount);
    void ResetPlaybackState();
    void DetachForDestroy();

    static constexpr int PIANO_METER_CH_MAX = 8;
    static constexpr int PIANO_BASS_FRAMES = 16384;
    static constexpr int PIANO_LOW_FRAMES  = 8192;

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    DECLARE_MESSAGE_MAP()

    afx_msg void OnPaint();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnMove(int x, int y);
    afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
    afx_msg void OnClose();
    afx_msg LRESULT OnSyncRequest(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnAnalysisDone(WPARAM wParam, LPARAM lParam);
    virtual BOOL PreTranslateMessage(MSG* pMsg);

private:
    struct NoteExpr {
        static constexpr uint8_t ACCENT  = 0x01;
        static constexpr uint8_t SCOOP   = 0x02;
        static constexpr uint8_t VIBRATO = 0x04;
        static constexpr uint8_t SLIDE   = 0x08;
        static constexpr uint8_t FALL    = 0x10;
        static constexpr uint8_t SUSTAIN = 0x20;
    };

    struct NoteFrame {
        bool     active[88];
        float    strength[88];
        uint8_t  segment[88];
        uint8_t  bandMask[88];
        float    laneStrength[88][3];
        uint8_t  expr[88];
        float    dynLevel[88];
    };

    static constexpr int   KEY_COUNT    = 88;
    static constexpr int   WHITE_KEY_COUNT = 52;
    static constexpr int   MIDI_BASE      = 21;
    static constexpr size_t MAX_HISTORY   = 120;
    static constexpr int   RING_SIZE      = 131072;
    static constexpr int   WIN_LOW        = 8192;
    static constexpr int   WIN_BASS       = 16384;
    static constexpr int   WIN_HIGH       = 4096;
    static constexpr int   WIN_ONSET      = 1024;
    static constexpr int   LOW_KEY_SPLIT  = 51; // C5: これ未満は低音/中低音窓
    static constexpr int   DETECT_KEYS    = 108;
    static constexpr int   KEY_OFFSET       = 9;
    static constexpr UINT  WM_PIANOROLL_SYNC = WM_APP + 420;
    static constexpr UINT  WM_PIANOROLL_ANALYSIS_DONE = WM_APP + 421;

    NoteFrame m_historyRing[MAX_HISTORY];
    int       m_historyCount = 0;
    int       m_historyHead = 0;

    bool  m_activeKeys[88];
    float m_noteStrength[88];
    float m_rawStrengths[88];
    float m_smoothedStrengths[88];
    int   m_consecActive[88];
    int   m_consecSilent[88];
    uint8_t m_segmentId[88];
    float   m_envPeak[88];
    int     m_unpickedFrames[88];
    int     m_strengthDipFrames[88];
    uint8_t m_bandMask[88];
    float   m_laneStrength[88][3];
    uint8_t m_prevBandMask[KEY_COUNT];

    std::vector<double> m_ring;
    int                 m_ringWrite = 0;
    int                 m_ringCount = 0;
    int                 m_inputSampleRate = 44100;
    int                 m_samplesSinceAnalyze = 0;
    int                 m_playbackDelaySamples = 0;

    std::vector<double> m_goertzelCoeffs;
    std::vector<double> m_hannLow;
    std::vector<double> m_hannOnset;
    std::vector<double> m_hannBass;
    std::vector<double> m_blackmanHigh;
    float m_prevRawStrengths[KEY_COUNT];
    float m_onsetStrengths[KEY_COUNT];
    float m_prevOnsetStrengths[KEY_COUNT];
    bool  m_prevActiveKeys[KEY_COUNT];
    float m_prevNoteStrength[KEY_COUNT];
    uint8_t m_noteAgeFrames[KEY_COUNT];
    uint8_t m_scoopLatch[KEY_COUNT];
    uint8_t m_exprFlags[KEY_COUNT];
    float m_vibHist[KEY_COUNT][10];
    uint8_t m_vibHistCount[KEY_COUNT];
    static constexpr int VIB_HIST_LEN = 10;
    bool  m_analysisHasBass = false;
    std::vector<double> m_analysisBuf;
    std::vector<double> m_bassAnalysisBuf;
    std::vector<double> m_windowedLow;
    std::vector<double> m_windowedBass;
    std::vector<double> m_windowedHigh;
    std::vector<double> m_windowedOnset;

    CRITICAL_SECTION m_cs;
    CRITICAL_SECTION m_jobCs;
    HANDLE           m_hAnalysisThread = NULL;
    HANDLE           m_hAnalysisWake = NULL;
    volatile LONG    m_workerStop = 0;
    volatile LONG    m_jobPending = 0;
    double           m_jobMono[PIANO_BASS_FRAMES];
    std::vector<double> m_workerMonoScratch;
    int              m_jobFrameCount = 0;
    int              m_jobSampleRate = 44100;
    double           m_goertzelRawScratch[KEY_COUNT];

    bool m_feedEnabled = true;
    bool m_paintDisabled = false;
    bool m_historyDirty = true;
    bool m_keyDirty = true;
    bool m_meterDirty = false;
    int  m_framesPending = 0;
    DWORD m_lastAnalyzeTick = 0;
    float m_bufwav3LevelDb = -60.0f;
    float m_chMeterDb[PIANO_METER_CH_MAX];
    float m_chMeterFill[PIANO_METER_CH_MAX];
    float m_chMeterAutoPeak[PIANO_METER_CH_MAX];
    int   m_chMeterCount = 0;
    static constexpr DWORD ANALYZE_MIN_MS = 4;

    void EnsureAnalysisTables(int sampleRate);
    void RunGoertzelFromBuffer(const double* winLow8192, const double* winBass, int bassWinLen);
    void UpdateNoteStates();
    void DetectExpressions();
    void PushFrame(bool requestUiInvalidate);
    void StartAnalysisWorker();
    void StopAnalysisWorker();
    DWORD AnalysisWorkerLoop();
    bool ProcessAnalysisJob();
    static DWORD WINAPI AnalysisWorkerThreadEntry(LPVOID param);
    int  HistoryCountLocked() const;
    void CopyHistorySnapshot(NoteFrame* out, int maxOut, int& outCount) const;
    const NoteFrame& HistoryAt(int indexFromNewest) const;

    static double ReadMonoSample(const uint8_t* sp, int bits);
    static double GoertzelMagnitude(const double* samples, int numSamples,
        double coefficient, const double* window);
    static float  ApplyDisplayScale(float rawAmp, int keyIndex);
    static float  MidiToFreq(int midi);
    static int      KeyBandIndex(int keyIndex);

    bool IsBlackKey(int midiNote) const;
    int  GetWhiteKeyIndex(int midiNote) const;
    void GetChromaticKeyRect(int keyIndex, int width, int& xL, int& xR) const;
    void GetWhiteKeyRect52(int midi, int width, int& xL, int& xR) const;
    void DrawChannelDbBars(CDC& dc, const CRect& rc, const float* chFill, int chCount) const;

    CDC     m_rollDC;
    CBitmap m_rollBmp;
    CBitmap* m_rollOldBmp = nullptr;
    CDC     m_rollScratchDC;
    CBitmap m_rollScratchBmp;
    CBitmap* m_rollScratchOldBmp = nullptr;
    int     m_rollW = 0;
    int     m_rollH = 0;
    bool    m_rollReady = false;
    bool    m_rollScrollValid = false;
    int     m_lastScrollPx = 0;
    int     m_lastScrollHealTop = 0;

    CDC     m_keyDC;
    CBitmap m_keyBmp;
    CBitmap* m_keyOldBmp = nullptr;
    int     m_keyW = 0;
    int     m_keyH = 0;
    bool    m_keyBufReady = false;

    CFont   m_fontKeyNote;
    CFont   m_fontKeyOct;
    CFont   m_fontMeterTag;
    CFont   m_fontExprSymbol;
    CFont   m_fontExprSymbolCompact;
    CFont   m_fontExprLegend;
    int     m_fontCacheClientW = 0;
    int     m_fontCacheKeyH = 0;
    int     m_fontCacheRollH = 0;
    bool    m_paintFontsReady = false;
    volatile LONG m_syncPosted = 0;

#if CCUSTOM_AERO_SUPPORT
    CCC_ChromaBlitCache m_chromaCache;
    bool    m_chromaReady = false;
    int     m_chromaW = 0;
    int     m_chromaH = 0;
#endif
    bool    m_keySnapActive[KEY_COUNT];
    uint8_t m_keySnapBand[KEY_COUNT];

    void ReleasePaintBuffers();
    bool EnsureRollBuffer(CDC& refDC, int width, int rollH);
    bool EnsureKeyBuffer(CDC& refDC, int width, int keySectionH);
    void MarkKeyVisualDirty();
    void ApplySyncInvalidate();
    void InvalidatePianoRollRegions(bool roll, bool key);
    void EnsurePaintFonts(int clientW, int keyH, int rollH);
    void DrawExprLegend(CDC& dc, int rollW, int rollH) const;
    void GetExprLegendPanelRect(int rollW, int rollH, CRect& panel) const;
    void DrawHistoryGrid(CDC& dc, int width, int yFrom, int yTo) const;
    int  HistoryRowPitch(int rollH) const;
    int  HistoryScrollPx(int rollH, int rowsToScroll) const;
    void DrawHistoryRowAt(CDC& dc, int width, int yTop, int yBot, const NoteFrame& frame) const;
    void DrawHistoryRow(CDC& dc, int width, int rollH, size_t rowIndex, const NoteFrame& frame) const;
    void DrawHistoryArea(CDC& dc, int width, int rollH, int histCount, const NoteFrame* hist) const;
    void GetHistoryRowBounds(int rollH, int rowFromBottom, int& yTop, int& yBot) const;
    void ComposeRollBuffer(CDC& dc, int width, int rollH, int histCount, const NoteFrame* hist, const NoteFrame& live) const;
    bool TryAdvanceRollBuffer(int width, int rollH, int histCount, const NoteFrame* hist, int pendingCount, const NoteFrame& live);
    void BuildLiveNoteFrame(NoteFrame& frame) const;
    void DrawPlayheadRow(CDC& dc, int width, int rollH, const NoteFrame& live) const;
    void DrawKeyboardToBuffer(CDC& dc, int width, int keySectionH, int keyH,
        const bool* activesCopy, const uint8_t* bandMaskCopy, const float laneStrengthCopy[KEY_COUNT][3],
        const float* chFillCopy, int chCountCopy) const;
    void UpdatePianoRollTimer();
};
