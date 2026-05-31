#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include <vector>

class CPianoRoll : public CCustomDialogEx
{
    DECLARE_DYNAMIC(CPianoRoll)

public:
    CPianoRoll(CWnd* pParent = nullptr);
    virtual ~CPianoRoll();

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_PIANOROLL };
#endif

    // playbackDelaySamples: DS バッファ内の write-play 差（再生同期用）
    void FeedPCM(const void* pData, int frames, int sampleRate, int bits, int channels,
        int playbackDelaySamples = 0);
    void ResetPlaybackState();

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    DECLARE_MESSAGE_MAP()

    afx_msg void OnPaint();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnMove(int x, int y);
    afx_msg void OnClose();
    virtual BOOL PreTranslateMessage(MSG* pMsg);

private:
    struct NoteFrame {
        bool     active[88];
        float    strength[88];
        uint8_t  segment[88];  // 同音連打ごとに色分け
    };

    static constexpr int   KEY_COUNT    = 88;
    static constexpr int   WHITE_KEY_COUNT = 52;
    static constexpr int   MIDI_BASE      = 21;   // A0
    static constexpr size_t MAX_HISTORY   = 120;
    static constexpr int   RING_SIZE      = 32768;
    static constexpr int   WIN_LOW        = 8192; // mode0_Note 低域窓長
    static constexpr int   WIN_HIGH       = 4096; // mode0_Note 高域窓長
    static constexpr int   LOW_KEY_SPLIT  = 50;   // bin<50 → 8192点 (スペアナ mode0 同様)

    std::vector<NoteFrame> m_history;

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

    std::vector<double> m_ring;
    int                 m_ringWrite = 0;
    int                 m_ringCount = 0;
    int                 m_inputSampleRate = 44100;
    int                 m_samplesSinceAnalyze = 0;
    int                 m_playbackDelaySamples = 0;

    std::vector<double> m_goertzelCoeffs;
    std::vector<double> m_hannLow;
    std::vector<double> m_blackmanHigh;
    std::vector<double> m_analysisBuf;

    CRITICAL_SECTION m_cs;
    bool m_feedEnabled = true;

    void EnsureAnalysisTables(int sampleRate);
    void RunGoertzelAnalysis();
    void UpdateNoteStates();
    void PushFrame();

    static double ReadMonoSample(const uint8_t* sp, int bits);
    static double GoertzelMagnitude(const double* samples, int numSamples,
        double coefficient, const double* window);
    static float  ApplyDisplayScale(float rawAmp, int keyIndex);
    static float  MidiToFreq(int midi);
    static COLORREF SegmentNoteColor(int midi, int segment, float strength, bool blackKey);

    bool IsBlackKey(int midiNote) const;
    int  GetWhiteKeyIndex(int midiNote) const;
    void GetChromaticKeyRect(int keyIndex, int width, int& xL, int& xR) const;
    void GetWhiteKeyRect52(int midi, int width, int& xL, int& xR) const;
    void SyncMelodyBandLimit(bool* midPick, bool* treblePick) const;
};
