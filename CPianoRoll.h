// CPianoRoll.h : リアルタイム簡易ピアノロールビジュアライザ
//
// PCM ストリームを Goertzel アルゴリズムで 88 鍵分に変換し、ノートのオン/オフと
// 強度を推定して簡易ピアノロール形式で描画する。
//
// スレッドモデル:
//   - FeedPCM()          … 再生スレッドから呼ばれる。モノラル変換後リングバッファへ書込
//   - AnalyzePlayCursor() … 同スレッドから呼ばれる。ワーカーへジョブをポスト
//   - AnalysisWorkerLoop() … 専用ワーカースレッド。Goertzel 実行 → WM_PIANOROLL_ANALYSIS_DONE ポスト
//   - OnAnalysisDone()   … UI スレッド。UpdateNoteStates/DetectExpressions を実行しフレーム確定
//   - OnPaint() / OnTimer() … UI スレッド。確定済みフレームを GDI バッファへ描画
//
// 描画バッファ(メモリDC)は 3 種:
//   m_rollDC  … 簡易ピアノロール(時間軸スクロール: 最新行を下端に追記)
//   m_rollScratchDC … 一時合成用
//   m_keyDC   … 鍵盤(ノートアクティブ時に着色。変化時のみ再描画)
//
#pragma once
#include "afxdialogex.h"
#include "CCustomControl.h"
#include "NoteEnvelopeModel.h"

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

    // 再生スレッドから呼ぶ。各フォーマットをモノラル double に変換してリングバッファへ積む
    void FeedPCM(const void* pData, int frames, int sampleRate, int bits, int channels,
        int playbackDelaySamples = 0);
    // bufwav3 経路(再生バッファ直後)から呼ぶ。ワーカーに分析ジョブをキューイング
    void AnalyzePlayCursorMono(const double* mono, int frameCount, int sampleRate);
    // PC音(ループバック)用: 短いWASAPIパケットを蓄積してから解析に渡す
    void FeedLoopbackMono(const double* mono, int frameCount, int sampleRate);
    // Sync 側の重い PCM 変換を、解析受付可能時だけに限定するための判定
    bool ShouldCaptureAnalyzeJob();
    // チャンネル別 dB を受け取りメーターバーへ反映(レベルメーターは Goertzel 非依存)
    void SetChannelMeterDb(const float* dbPerChannel, int channelCount);
    // stop/曲切替の先頭で呼ぶ。DoEvent より前に解析を止める
    void PauseAnalysis();
    // 曲切替時などに分析状態を全クリアし、解析ワーカーを作り直す
    void ResetPlaybackState();
    // 再生スレッド稼働後に解析を再開する
    void ResumePlaybackFeed();
    // プレイヤー停止時: PC音譜面化または譜面録り中なら解析継続、それ以外は Pause/Reset
    void OnPlayerFeedStopping(bool fullReset);
    bool IsScoreCapturing() const { return m_scoreCapMidi || m_scoreCapXml; }
    // ウィンドウ破棄前に呼ぶ。ワーカースレッド停止と GDI バッファ解放を安全に行う
    void DetachForDestroy();

    static constexpr int PIANO_METER_CH_MAX = 8;       // レベルメーターの最大チャンネル数
    static constexpr int RING_SIZE = 131072;           // 解析キャプチャ上限サンプル数

    // 44100 Hz 基準の Goertzel 窓(サンプル数)。実際の窓長は ScaleWinSamples でレートに比例。
    // 低音だけ 16384 にすると窓中心が ~185ms 遅れ、中高(8192/4096)とタイミングがずれて
    // 「低音だけノートに乗らない」原因になる。低音も 8192 に揃え時間軸を一致させる。
    // 周波数分解能は F2 付近でなお半音未満を分離可能。
    static constexpr int REF_SAMPLE_RATE = 44100;
    // Goertzel は 48k 超でも分解能はほぼ伸びず計算量だけ増える。解析専用に上限を設ける。
    static constexpr int ANALYZE_RATE_MAX = 48000;
    static constexpr int WIN_LOW_REF = 8192;
    static constexpr int WIN_BASS_REF = 8192;
    static constexpr int WIN_HIGH_REF = 4096;
    static constexpr int WIN_ONSET_REF = 1024;
    static int ScaleWinSamples(int refSamples, int sampleRate, int capSamples = 0);
    static int CaptureFrameCount(int sampleRate, int capSamples = 0);
    static int MinAnalyzeFrameCount(int sampleRate, int capSamples = 0);
    // 再生 SR を解析用にキャップ（アップスケール/hi-res 時の窓肥大を防ぐ）
    static int CapAnalyzeSampleRate(int sampleRate);
    // ソース SR → 解析 SR 換算の必要ソースフレーム数
    static int SourceFramesForAnalyze(int analyzeFrames, int sourceRate, int analyzeRate);

    // 再アタック判定(ゲート連結中の同鍵連打分離)を有効にするかどうか。
    // [重要] 既定 false。短窓オンセット信号は測定ノイズの影響を受けやすく、
    // 実際の音源で2度にわたり持続音への誤発火(暴走)が確認されたため、
    // 実音源で聴きながら閾値(NoteEnvelopeModel.h の kPresets)を
    // 調整できるまでは無効のままにしておくことを強く推奨する。
    bool m_reattackDetectEnabled = false;
    // 音色分類に基づく打撃音(ドラム等)ゴースト抑制。既定は無効(opt-in)。
    // 誤って弱いピアノのスタッカートまで消してしまう可能性があるため、
    // 効果を確認しながら有効化することを推奨する。
    bool m_impulsiveGhostSuppressEnabled = false;

    // 倍音ゴースト抑制: 既に鳴っている音の倍音(オクターブ等)にあたる候補が、
    // 一瞬だけ基音より強くなって PassesFundamentalTest 等の閾値を超えた場合でも、
    // 単発フレームで即座に独立ノートとして通さず、連続 kHarmonicGhostConfirmFrames
    // フレーム分そのまま通過し続けた時だけ新規ノートと認める。
    // 実音源(悲しみ2)の実測で、通常は正しく棄却される倍音/基音比が
    // アタック直後などの一瞬だけ閾値(0.78)を超えて誤通過することを確認済み。
    // 既定は無効(opt-in)。和音で意図的に倍音関係の2音を同時に弾いた場合、
    // 後から鳴らした方の検出が数ms(既定設定で約12ms)遅れる副作用があるため、
    // 実音源で確認しながら有効化すること。
    bool m_harmonicGhostGuardEnabled = true;

    // 倍音比率プロファイル(HarmonicProfile.h)による音色分類を、既存のゴースト判定に
    // 追加の判断材料として組み込むかどうか。
    // [重要] 各プロファイルの数値は実測較正されたものではなく目安値のため、
    // 実音源で確認しながら HarmonicProfile.h の数値を調整することを前提とする。
    // LooksLikeNoiseProfile は「最良ノイズ ≥ 閾値 かつ 最良楽器を明確に上回る」ときだけ拒否。
    bool m_harmonicProfileGuardEnabled = true;
    // ノイズ系とみなす最低確信度(コサイン類似度)。楽器側とのマージン判定と併用。
    static constexpr float kHarmonicProfileNoiseMinConfidence = 0.92f;

    void LayoutHelpBtn();
    void ShowHelpSheet();

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
    afx_msg void OnDestroy();
    afx_msg void OnBnClickedHelp();
    afx_msg LRESULT OnSyncRequest(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnAnalysisDone(WPARAM wParam, LPARAM lParam);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    afx_msg void OnRollSpeedCmd(UINT nID);
    afx_msg void OnToggleFreeze();
    afx_msg void OnClearDisplay();
    afx_msg void OnToggleExprLegend();
    afx_msg void OnToggleExprMarks();
    afx_msg void OnToggleLevelMeter();
    afx_msg void OnToggleAlwaysOnTop();
    afx_msg void OnToggleReattackDetect();
    afx_msg void OnToggleImpulsiveGhost();
    afx_msg void OnToggleHarmonicGhost();
    afx_msg void OnToggleHarmonicProfile();
    afx_msg void OnOpenTuneDialog();
    afx_msg void OnViewModeCmd(UINT nID);
    afx_msg void OnKeyRangeCmd(UINT nID);
    afx_msg void OnToggleNoteNames();
    afx_msg void OnToggleCaptureMidi();
    afx_msg void OnToggleCaptureMusicXml();
    afx_msg void OnToggleChordPanel();
    afx_msg void OnToggleLoopbackScore();
    // 簡易3D 表示時のみ、クライアント領域のドラッグで視点(ヨー/ピッチ)を回す。
    // 2D 表示時は一切介入せず基底(ウィンドウドラッグ/追従チェック)へ素通しする。
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    virtual BOOL PreTranslateMessage(MSG* pMsg);

private:
    // 各ノートに付与する表現記号フラグ(DetectExpressions で設定、簡易ピアノロール描画で参照)
    struct NoteExpr {
        static constexpr uint8_t ACCENT = 0x01;  // 強調(アタック急峻)
        static constexpr uint8_t SCOOP = 0x02;  // スクープ(下から音程が上がる)
        static constexpr uint8_t VIBRATO = 0x04;  // ビブラート(強度が周期的に変動)
        static constexpr uint8_t SLIDE = 0x08;  // スライドアップ
        static constexpr uint8_t FALL = 0x10;  // フォール(音程が下降消音)
        static constexpr uint8_t SUSTAIN = 0x20;  // サスティン(長く保たれる持続音)
    };

    // ---- 鍵盤定数 ----
    static constexpr int   KEY_COUNT = 108;         // MIDI 0…107（A0=21, フルレンジ）
    static constexpr int   WHITE_KEY_COUNT = 63;       // MIDI 0..107 の白鍵数
    static constexpr int   MIDI_BASE = 0;

    // 1 分析フレーム分のノートスナップショット(履歴リングバッファの要素)
    struct NoteFrame {
        bool     active[KEY_COUNT];
        float    strength[KEY_COUNT];
        uint8_t  segment[KEY_COUNT];
        uint8_t  bandMask[KEY_COUNT];
        float    laneStrength[KEY_COUNT][3];
        uint8_t  expr[KEY_COUNT];
        float    dynLevel[KEY_COUNT];
        bool     reattack[KEY_COUNT];   // このフレームで再アタック(タイ分割)が起きたか
    };

    // ---- 分析バッファ / 履歴 ----
    static constexpr size_t MAX_HISTORY = 120;       // ロール上に表示するフレーム行数(上限)

    // ---- Goertzel 窓サイズ(サンプル数・実行時) ----
    // 44100 Hz 基準長を REF_SAMPLE_RATE に比例スケール(EnsureAnalysisTables で設定)
    int   m_winLow = WIN_LOW_REF;
    int   m_winBass = WIN_BASS_REF;
    int   m_winHigh = WIN_HIGH_REF;
    int   m_winOnset = WIN_ONSET_REF;
    // 分析: [0,60) 16384 / [60,84) 8192 / [84,108) 4096（108鍵・周波数基準）
    static constexpr int   BASS_ANALYSIS_END = 67; // PianoKey::BASS_BAND_END(ヘッダ依存回避)
    static constexpr int   DETECT_KEYS = KEY_COUNT;

    // 表示速度(アナライザーの波形速度と同系)。解析そのものは固定ホップで維持し、
    // 履歴行の投入だけを倍率で間引く/増やす。
    static constexpr int ROLL_SPEED_COUNT = 8;
    static constexpr int kRollSpeedPct[ROLL_SPEED_COUNT] = {
        25, 50, 75, 100, 125, 150, 175, 200
    };

    // ---- カスタムウィンドウメッセージ ----
    static constexpr UINT  WM_PIANOROLL_SYNC = WM_APP + 420;           // UI 同期要求(RequestSyncFromMainUi)
    static constexpr UINT  WM_PIANOROLL_ANALYSIS_DONE = WM_APP + 421;  // ワーカーからの分析完了通知
    static constexpr UINT  IDM_ROLL_SPEED_BASE = 42200;               // +0..ROLL_SPEED_COUNT-1
    static constexpr UINT  IDM_ROLL_FREEZE = 42210;
    static constexpr UINT  IDM_ROLL_CLEAR = 42211;
    static constexpr UINT  IDM_ROLL_LEGEND = 42212;
    static constexpr UINT  IDM_ROLL_EXPR = 42213;
    static constexpr UINT  IDM_ROLL_METER = 42214;
    static constexpr UINT  IDM_ROLL_TOPMOST = 42215;
    static constexpr UINT  IDM_ROLL_REATTACK = 42216;
    static constexpr UINT  IDM_ROLL_IMPULSE = 42217;
    static constexpr UINT  IDM_ROLL_HARM_GHOST = 42218;
    static constexpr UINT  IDM_ROLL_HARM_PROF = 42219;
    static constexpr UINT  IDM_ROLL_TUNE = 42220;
    static constexpr UINT  IDM_ROLL_VIEW_BASE = 42221;   // +0=2D +1=簡易3D
    static constexpr UINT  IDM_ROLL_VIEW_COUNT = 2;
    static constexpr UINT  IDM_ROLL_KEYS_BASE = 42224;   // +0=88鍵 +1=108鍵
    static constexpr UINT  IDM_ROLL_KEYS_COUNT = 2;
    static constexpr UINT  IDM_ROLL_NOTENAME = 42227;
    static constexpr UINT  IDM_ROLL_CAPTURE_MIDI = 42228;
    static constexpr UINT  IDM_ROLL_CAPTURE_MUSICXML = 42229;
    static constexpr UINT  IDM_ROLL_CHORD_PANEL = 42230;
    static constexpr UINT  IDM_ROLL_LOOPBACK_SCORE = 42231;

    // ---- フレーム履歴リングバッファ(UI スレッドのみ読み書き) ----
    NoteFrame m_historyRing[MAX_HISTORY];  // 確定済みフレームの環状配列
    int       m_historyCount = 0;          // 有効フレーム数(MAX_HISTORY 未満の間は増える)
    int       m_historyHead = 0;           // 次に書き込むインデックス(新→旧 = head-1, head-2, ...)

    // ---- ノート状態(UI スレッド / m_cs 保護なし。OnAnalysisDone からのみ更新) ----
    bool  m_activeKeys[KEY_COUNT];
    float m_noteStrength[KEY_COUNT];
    float m_rawStrengths[KEY_COUNT];          // 検出用（平坦スケール）
    float m_smoothedStrengths[KEY_COUNT];     // 検出用 IIR
    float m_displayStrengths[KEY_COUNT];      // 描画用（ApplyDisplayScale）
    float m_displaySmoothed[KEY_COUNT];       // 描画用 IIR
    int   m_consecActive[KEY_COUNT];
    int   m_consecSilent[KEY_COUNT];
    uint8_t m_segmentId[KEY_COUNT];
    float   m_envPeak[KEY_COUNT];
    int     m_unpickedFrames[KEY_COUNT];
    int     m_strengthDipFrames[KEY_COUNT];
    uint8_t m_transientHold[KEY_COUNT];
    uint8_t m_bandMask[KEY_COUNT];
    float   m_laneStrength[KEY_COUNT][3];
    uint8_t m_prevBandMask[KEY_COUNT];

    // ---- 音色エンベロープモデル(再アタック検出用。NoteEnvelopeModel.h) ----
    NoteEnvelope::NoteEnvelopeState m_envModel[KEY_COUNT];
    bool m_reattackMark[KEY_COUNT];   // このフレームで再アタックと判定された鍵(表示用)
    // このフレーム、短窓Goertzelのオンセット検出が「本物のアタックらしい」と
    // 判定したか(UpdateNoteStates 内で計算し、UpdateEnvelope へ引き渡す)。
    bool m_onsetBoostThisFrame[KEY_COUNT];
    // m_onsetBoostThisFrame が連続で true だったフレーム数。
    // 生のピック判定(picked[])は音の終わり際や、密なミックス中では他の楽器の
    // エネルギー漏れ込みにより閾値付近でチラつく(フリッカーする)ことが、
    // 実音源(ASTNEEZAL)での実測でも確認された(単発フレームでのオンセット
    // 誤発火率が低音域で12.9%〜18.2%)。そのため単発フレームのオンセット支持
    // だけで再アタックを確定させず、連続 kOnsetConfirmFrames フレーム以上
    // 続いた場合のみ「本物の攻撃」と扱う。
    uint8_t m_onsetBoostStreak[KEY_COUNT];
    static constexpr uint8_t kOnsetConfirmFrames = 3;

    // 倍音ゴースト抑制用: 既存活性音の倍音として疑わしい候補が、
    // 連続何フレーム閾値超えを維持しているか(m_harmonicGhostGuardEnabled 用)。
    uint8_t m_harmonicGhostStreak[KEY_COUNT];
    // 「疑わしい」と見なす基準。
    // [重要・修正] 以前は「他に鳴っている音と倍音関係にあるだけ」で疑う設計だったが、
    // 実際の音楽ではメロディ音がベースのオクターブ/5度上であることはごく普通で、
    // 本物のメロディ音の大半がこの条件に該当してしまい、通常のホールド機構による
    // 一瞬の再ピック(本来は継続音として自然に埋まるはず)まで足止めしてしまい、
    // 継続音が途切れて見える「漏れ」を引き起こしていた。
    // そこで、実際の基音判定式(PianoKeyTable.h の PassesFundamentalTest 系、
    // 閾値0.78)が「ギリギリで通過したか」だけを見る方式に変更した
    // (IsMarginalFund、CPianoRoll.cpp 側に実装)。
    // marginRatio: 0.78の閾値に対し、この比率以上に接近していたら「際どい通過」とみなす。
    // [調整] 0.85 → 0.75: 疑わしいと見なす範囲を少し広げ、悲しみ2L実測で
    // 残っていた微小なゴーストをできる範囲で追い込む。
    static constexpr float kHarmonicGhostMarginRatio = 0.75f;
    // [特性ベース判定へ変更] 際どい通過をした候補について、振幅の持続時間ではなく
    // 「それ自体の短窓オンセット(アタック transient)が連続何フレーム観測されたか」
    // (m_onsetBoostStreak)を合否基準にする。ゴースト(親音への追従に過ぎない漏れ込み)
    // は自分自身のアタックを持たないため、これを要求するだけで振幅に関係なく弾ける。
    static constexpr uint8_t kHarmonicGhostConfirmFrames = 3;

    // ---- PCM インプット（FeedPCM は現状 no-op。リングは廃止）----
    int                 m_ringWrite = 0;
    int                 m_ringCount = 0;
    int                 m_inputSampleRate = 44100;
    int                 m_samplesSinceAnalyze = 0;   // 前回分析からのサンプル数(ANALYZE_INTERVAL トリガー用)
    int                 m_playbackDelaySamples = 0;  // 再生バッファ遅延の補正値(IIR 平均)
    bool                m_analysisTablesReady = false;
    // PC音ループバック蓄積(短いWASAPIパケットを MinAnalyze まで溜める)
    static constexpr int LOOPBACK_ACCUM_MAX = 16384;
    double              m_loopbackAccum[LOOPBACK_ACCUM_MAX];
    int                 m_loopbackAccumN = 0;
    int                 m_loopbackAccumRate = 48000;

    // ---- Goertzel 係数 / 窓関数(サンプルレート変化時に再計算) ----
    // ScaleWinSamples の絶対上限 = RING_SIZE。std::vector 禁止（長時間断片化防止）
    static constexpr int WIN_SAMPLES_MAX = RING_SIZE;
    double m_goertzelCoeffs[KEY_COUNT];   // 2*cos(2π*f/sr) の事前計算値
    double m_hannLow[WIN_SAMPLES_MAX];
    double m_hannOnset[WIN_SAMPLES_MAX];
    double m_hannBass[WIN_SAMPLES_MAX];
    double m_blackmanHigh[WIN_SAMPLES_MAX];     // 高域はサイドローブ抑制のため Blackman 窓

    // ---- 前フレーム値 / 表現記号検出用 ----
    float m_prevRawStrengths[KEY_COUNT];
    float m_onsetStrengths[KEY_COUNT];       // オンセット検出用(短窓 Goertzel 値)
    float m_prevOnsetStrengths[KEY_COUNT];
    bool  m_prevActiveKeys[KEY_COUNT];
    float m_prevNoteStrength[KEY_COUNT];
    uint8_t m_noteAgeFrames[KEY_COUNT];      // ノートオン後の経過フレーム(スクープ/スライド判定)
    uint8_t m_scoopLatch[KEY_COUNT];
    uint8_t m_exprFlags[KEY_COUNT];
    float m_vibHist[KEY_COUNT][16];          // 強度変動履歴(ビブラート判定用)
    uint8_t m_vibHistCount[KEY_COUNT];
    static constexpr int VIB_HIST_LEN = 16;
    bool  m_analysisHasBass = false;
    double m_analysisBuf[WIN_SAMPLES_MAX];
    double m_bassAnalysisBuf[WIN_SAMPLES_MAX];
    double m_windowedLow[WIN_SAMPLES_MAX];
    double m_windowedBass[WIN_SAMPLES_MAX];
    double m_windowedHigh[WIN_SAMPLES_MAX];
    double m_windowedOnset[WIN_SAMPLES_MAX];

    // ---- 分析ワーカースレッド ----
    // 再生スレッドからのジョブを受け取り Goertzel 解析を行う専用スレッド。
    // 結果は WM_PIANOROLL_ANALYSIS_DONE で UI スレッドへ通知される。
    CRITICAL_SECTION m_cs;                // リングバッファ保護
    CRITICAL_SECTION m_jobCs;             // ジョブバッファ(m_jobMono)保護
    HANDLE           m_hAnalysisThread = NULL;
    HANDLE           m_hAnalysisWake = NULL;   // SetEvent でワーカーを起こすイベント
    volatile LONG    m_workerStop = 0;    // 1 にするとワーカーが自己終了
    volatile LONG    m_jobPending = 0;    // InterlockedExchange で管理するジョブ有無フラグ
    volatile LONG    m_analysisBusy = 0;  // ProcessAnalysisJob 実行中
    volatile LONG    m_analysisEpoch = 0; // Reset ごとに加算。古いジョブを破棄
    double           m_jobMono[WIN_SAMPLES_MAX];  // m_jobCs 保護下でコピーされる入力バッファ
    double           m_workerMonoScratch[WIN_SAMPLES_MAX];
    int              m_jobFrameCount = 0;
    int              m_jobSampleRate = 44100;
    double           m_goertzelRawScratch[KEY_COUNT];

    // ---- 描画制御フラグ ----
    bool m_feedEnabled = true;       // false にすると FeedPCM が即リターン(破棄前のシャットダウン用)
    bool m_paintDisabled = false;
    bool m_historyDirty = true;      // ロールバッファ再描画が必要か
    bool m_keyDirty = true;          // 鍵盤バッファ再描画が必要か
    bool m_meterDirty = false;
    int  m_framesPending = 0;        // スクロール済み未コミットフレーム数
    DWORD m_lastAnalyzeTick = 0;     // スロットリング用(ANALYZE_MIN_MS 未満は再分析しない)

    // ---- レベルメーター ----
    float m_bufwav3LevelDb = -60.0f;          // AGC後の動作レベル dB(ピック閾値スケーリング)
    float m_lastGainDb = 0.0f;                // 直近フレームの解析正規化ゲイン(dB, 正=ブースト/負=カット)
    // 大きなブースト時のソースSNR悪化補正などに使用。
    float m_chMeterDb[PIANO_METER_CH_MAX];
    float m_chMeterFill[PIANO_METER_CH_MAX];      // 表示用 IIR 平滑フィル値(0.0〜1.0)
    float m_chMeterAutoPeak[PIANO_METER_CH_MAX];  // 自動ピーク(棒グラフ上端の目印)
    int   m_chMeterCount = 0;
    static constexpr DWORD ANALYZE_MIN_MS = 16;      // 表示(ms2)と同程度。3ms は Goertzel が描画の5倍走り UI を圧迫する
    // RubberBand(テンポ/ピッチ)と CPU 競合するときさらに間隔を広げる
    static constexpr DWORD ANALYZE_MIN_MS_TEMPO = 32;
    static DWORD EffectiveAnalyzeMinMs();
    bool  m_detectSilent = false;                // Goertzel 直後の無音フラグ(publish 側で反映)

    void EnsureAnalysisTables(int sampleRate, int capCaptureFrames = 0);   // Goertzel 係数と窓関数を再計算
    void RunGoertzelFromBuffer(const double* winLow, const double* winBass, int bassWinLen);
    void PublishDetectResults(); // UpdateNoteStates/PushDisplayFrames または無音クリア（m_cs 下）
    void ResetScoreCaptureLocked();
    void AppendScoreCaptureLocked();
    void SaveCapturedMidi();
    void SaveCapturedMusicXml();
    void HoldPcAudioForScoreCapture();
    void ReleasePcAudioForScoreCaptureIfHeld();
    void UpdateNoteStates();    // ピック結果からノートのオン/オフ・強度・セグメントを更新
    void DetectExpressions();   // UpdateNoteStates 後に表現記号(アクセント/ビブラート等)を付与
    // 音色エンベロープモデルを更新し、再アタック(タイ分割)を判定する。
    // 「谷からのリバウンド量」と「短窓オンセット判定」の両方が揃った時のみ発火するため、
    // 持続音の自然な揺らぎだけでは連鎖的に誤発火しない(v1の既知不具合の修正版)。
    void UpdateEnvelope();
    void PushFrame(bool requestUiInvalidate);  // 確定フレームを履歴リングバッファへ追加
    void StartAnalysisWorker();
    void StopAnalysisWorker();
    // 死亡/停止済みワーカーを検出して再起動。Resume/解析投入のたびに呼ぶ
    bool EnsureAnalysisWorkerAlive();
    DWORD AnalysisWorkerLoop();
    bool ProcessAnalysisJob();  // ワーカースレッド内。ジョブバッファの Goertzel 解析を実行
    bool RunAnalysisJob(const double* mono, int frameCount, int sampleRate, LONG epochAtStart);
    static DWORD WINAPI AnalysisWorkerThreadEntry(LPVOID param);
    int  HistoryCountLocked() const;
    void CopyHistorySnapshot(NoteFrame* out, int maxOut, int& outCount) const;
    const NoteFrame& HistoryAt(int indexFromNewest) const;  // 0=最新フレーム

    static double ReadMonoSample(const uint8_t* sp, int bits);
    static double GoertzelMagnitude(const double* samples, int numSamples,
        double coefficient, const double* window);
    static float  ApplyDisplayScale(float rawAmp, int keyIndex, int winSamples, int refWinSamples);
    // 検出専用: 窓正規化 + 平坦圧縮のみ（鍵EQなし）。部分音が基音より強く見えるのを防ぐ。
    static float  ApplyDetectScale(float rawAmp, int winSamples, int refWinSamples);
    static float  MidiToFreq(int midi);
    static int      KeyBandIndex(int keyIndex);

    bool IsBlackKey(int midiNote) const;
    int  GetWhiteKeyIndex(int midiNote) const;
    void GetChromaticKeyRect(int keyIndex, int width, int& xL, int& xR) const;
    void GetWhiteKeyRect52(int midi, int width, int& xL, int& xR) const;
    void DrawChannelDbBars(CDC& dc, const CRect& rc, const float* chFill, int chCount) const;

    // ---- GDI オフスクリーンバッファ ----
    // 簡易ピアノロール(時間軸スクロール領域)。最新フレームを下端に BitBlt で追記し、
    // 残りを1行分上へシフトするためダブルバッファで回す。
    CDC     m_rollDC;
    CBitmap m_rollBmp;
    CBitmap* m_rollOldBmp = nullptr;
    CDC     m_rollScratchDC;    // 1行分のスクロール合成に使う作業バッファ
    CBitmap m_rollScratchBmp;
    CBitmap* m_rollScratchOldBmp = nullptr;

    // 表現記号の凡例キャッシュ（静的内容を毎フレーム再描画せず BitBlt するため）
    mutable CDC      m_legendDC;
    mutable CBitmap  m_legendBmp;
    mutable CBitmap* m_legendOldBmp = nullptr;
    mutable int      m_legendW = 0;
    mutable int      m_legendH = 0;
    mutable bool     m_legendReady = false;
    mutable int      m_legendCacheRollW = -1;
    mutable int      m_legendCacheRollH = -1;
    // 凡例焼き込み前の下地退避（毎フレーム CreateCompatibleBitmap しない）
    mutable CDC      m_legendBgDC;
    mutable CBitmap  m_legendBgBmp;
    mutable CBitmap* m_legendBgOldBmp = nullptr;
    mutable int      m_legendBgW = 0;
    mutable int      m_legendBgH = 0;
    int     m_rollW = 0;
    int     m_rollH = 0;
    bool    m_rollReady = false;
    bool    m_rollScrollValid = false;
    int     m_lastScrollPx = 0;
    int     m_lastScrollHealTop = 0;

    // 鍵盤描画バッファ。ノートがオン/オフするたびに再描画するが、毎フレーム再生成
    // しないためサイズ変化時にのみ再確保する。
    CDC     m_keyDC;
    CBitmap m_keyBmp;
    CBitmap* m_keyOldBmp = nullptr;
    int     m_keyW = 0;
    int     m_keyH = 0;
    bool    m_keyBufReady = false;

    // 最終提示用フレームバッファ（ロール+鍵盤+追従を合成 → 画面へ1回 BitBlt）
    CDC     m_frameDC;
    CBitmap m_frameBmp;
    CBitmap* m_frameOldBmp = nullptr;
    int     m_frameW = 0;
    int     m_frameH = 0;

    // ---- フォントキャッシュ(ウィンドウサイズ変化時のみ再生成) ----
    CFont   m_fontKeyNote;            // 鍵盤上のノート名(C3, A4 等)
    CFont   m_fontKeyOct;             // オクターブ表示
    CFont   m_fontMeterTag;           // メーターのチャンネルラベル
    CFont   m_fontExprSymbol;         // 表現記号アイコン
    CFont   m_fontExprSymbolCompact;  // 狭い行向けの小さい表現記号
    CFont   m_fontExprLegend;         // 凡例パネルのラベル
    int     m_fontCacheClientW = 0;
    int     m_fontCacheKeyH = 0;
    int     m_fontCacheRollH = 0;
    bool    m_paintFontsReady = false;
    volatile LONG m_syncPosted = 0;   // RequestSyncFromMainUi の多重ポスト防止フラグ
    volatile LONG m_analysisDonePosted = 0; // WM_PIANOROLL_ANALYSIS_DONE の多重ポスト防止
    volatile LONG m_analysisPresentDirty = 0; // 解析済だが提示キック未消化
    DWORD m_lastSyncPostTick = 0;
    DWORD m_lastAnalysisDonePostTick = 0;
    int   m_rollSpeedPct = 100;       // 表示スクロール速度(%) 25..200
    int   m_rollSpeedCredit = 0;      // PushFrame 用アキュムレータ(壁時計×速度%)
    DWORD m_lastRollPushTick = 0;     // PushDisplayFrames の前回時刻
    bool  m_frozen = false;           // 表示スクロール停止(解析は継続、ライブ行は更新)
    bool  m_showExprLegend = true;    // 記号凡例パネル
    bool  m_showExprMarks = true;     // 表現記号グリフ/音階移行/バー装飾
    bool  m_showLevelMeter = true;    // 鍵盤上のレベルメーター
    bool  m_alwaysOnTop = false;      // WS_EX_TOPMOST

    // ---- 表示モード / 鍵盤レンジ / ノート名 ----
    // m_viewMode==0(既定) のときは従来の 2D 経路をそのまま使う(描画・スクロールとも無改変)。
    // m_viewMode==1 のときだけロールバッファ全面へ簡易3D シーンを描く。
    int   m_viewMode = 0;             // 0=通常(2D) 1=簡易3D
    int   m_keyRange = 108;           // 表示鍵数(88/108)。解析は常に 108 鍵で不変
    bool  m_showNoteNames = true;     // 白鍵のノート名(C/D/E…)
    // 譜面録り: チェックONで解析フレームを蓄積し、OFF時にファイル保存
    static constexpr int SCORE_CAP_EV_MAX = 8192;
    static constexpr int SCORE_CAP_FRAME_MAX = 1024;
    static constexpr int SCORE_TPQ = 480;
    static constexpr int SCORE_TICKS_PER_FRAME = 120; // 16分相当(実験的)
    struct ScoreCapEv {
        int  deltaTicks;
        BYTE status; // 0x90 / 0x80
        BYTE note;
        BYTE vel;
    };
    ScoreCapEv m_scoreCapEv[SCORE_CAP_EV_MAX];
    int   m_scoreCapEvN = 0;
    int   m_scoreCapPendingDelta = 0;
    bool  m_scoreCapPrevActive[KEY_COUNT] = {};
    uint8_t m_scoreCapFrames[SCORE_CAP_FRAME_MAX][(KEY_COUNT + 7) / 8];
    int   m_scoreCapFrameN = 0;
    bool  m_scoreCapMidi = false;
    bool  m_scoreCapXml = false;
    bool  m_scoreCapHeldPcAudio = false; // MpPcAudioRetain を録り側で保持中
    // コード進行パネル(実験的・キー検出と履歴から簡易表示)
    static constexpr int CHORD_HIST_MAX = 48;
    WCHAR m_chordHist[CHORD_HIST_MAX][24] = {};
    int   m_chordHistCount = 0;
    int   m_chordHistHead = 0;
    WCHAR m_chordLast[24] = {};
    float m_view3dYawDeg = -22.0f;    // 簡易3D 水平回転角(-180..180、ドラッグで360度)
    float m_view3dPitchDeg = 26.0f;   // 簡易3D 仰角(負=下から / 正=上から)
    float m_view3dZoom = 1.0f;        // ホイール拡大縮小(1=自動フレーミング基準)
    bool  m_rotDragging = false;      // 視点ドラッグ中
    CPoint m_rotDragOrigin = CPoint(0, 0);
    float m_rotDragYaw0 = 0.0f;
    float m_rotDragPitch0 = 0.0f;
    // 上からも下からも見えるよう縦は広い。ヨーはラップで実質360度。
    static constexpr float kView3dPitchMin = -85.0f;
    static constexpr float kView3dPitchMax = 85.0f;
    static constexpr float kView3dZoomMin = 0.35f;
    static constexpr float kView3dZoomMax = 4.0f;

    // ---- 簡易3D 用の履歴サンプル(固定長。std::vector は使わない) ----
    static constexpr int VIEW3D_DEPTH = 48;     // 奥行き方向の行数
    static constexpr int VIEW3D_STRIDE = 2;     // 何フレームごとに1行サンプルするか
    struct Wall3DRow {
        uint8_t level[KEY_COUNT];   // 0=消音 / 1..255=表示強度
        uint8_t band[KEY_COUNT];    // 帯域マスク(色分け用)
    };
    Wall3DRow m_wall3D[VIEW3D_DEPTH];
    int       m_wall3DRows = 0;     // 実際に埋まっている行数

    // 表示範囲の白鍵番号キャッシュ(描画ループから毎回数え直さないため)
    mutable int m_dispWhiteIdx[KEY_COUNT];
    mutable int m_dispWhiteTotal = 0;
    mutable int m_dispCacheRange = 0;

    // 簡易3D の視点パラメータ(1フレーム分の事前計算値)
    struct View3D {
        float cosYaw, sinYaw;
        float cosPitch, sinPitch;
        float camD;       // 視点距離
        float scale;      // ワールド→ピクセル
        float originX;    // 画面上の投影原点
        float originY;
    };

#if CCUSTOM_AERO_SUPPORT
    CCC_ChromaBlitCache m_chromaCache;
    bool    m_chromaReady = false;
    int     m_chromaW = 0;
    int     m_chromaH = 0;
#endif
    bool    m_keySnapActive[KEY_COUNT];
    uint8_t m_keySnapBand[KEY_COUNT];
    uint8_t m_keySnapExpr[KEY_COUNT];

    void ReleasePaintBuffers();
    bool EnsureRollBuffer(CDC& refDC, int width, int rollH);
    bool EnsureFrameBuffer(CDC& refDC, int w, int h);
    void PresentClientFromBuffers(CPaintDC& dc, int w, int h, int rollH, int keySectionH);
    // ロール+鍵盤+追従UI をオフスクリーンへ合成し、画面へは1回だけ出す
    void PresentFinalFrame(CDC& dc, int w, int h, int rollH, int keySectionH, int chordH = 0);
    void UpdateChordHistoryFromKeyCodes();
    void DrawChordPanel(CDC& dc, int x, int y, int w, int h) const;
    static int ChordPanelHeightPx();
#if CCUSTOM_AERO_SUPPORT
    // 「メインに追従」をクロマへ焼付けてから1回 Blit（画面2段合成のちらつき防止）
    void BakeMainFollowOverlayIntoChroma(int w, int h, int rollH, int keySectionH);
#endif
    bool EnsureKeyBuffer(CDC& refDC, int width, int keySectionH);
    void MarkKeyVisualDirty();
    void ApplySyncInvalidate();
    void InvalidateRegions(bool roll, bool key);
    void EnsurePaintFonts(int clientW, int keyH, int rollH);
    void DrawExprLegend(CDC& dc, int rollW, int rollH, bool blitToDest = true) const;
    void DrawExprLegendContent(CDC& dc, int rollW, int rollH, const CRect& panel, bool fillPanelBg = true) const;
    bool EnsureExprLegendCache(CDC& refDC, int rollW, int rollH) const;
    void ReleaseExprLegendCache() const;
    void GetExprLegendPanelRect(int rollW, int rollH, CRect& panel) const;
    void DrawHistoryGrid(CDC& dc, int width, int yFrom, int yTo) const;
    int  HistoryRowPitch(int rollH) const;
    int  HistoryScrollPx(int rollH, int rowsToScroll) const;
    void DrawHistoryRowAt(CDC& dc, int width, int yTop, int yBot, const NoteFrame& frame) const;
    void DrawHistoryRow(CDC& dc, int width, int rollH, size_t rowIndex, const NoteFrame& frame) const;
    void DrawPitchTransitions(CDC& dc, int width, int rollH, int histCount, const NoteFrame* hist) const;
    void DrawHistoryArea(CDC& dc, int width, int rollH, int histCount, const NoteFrame* hist) const;
    void GetHistoryRowBounds(int rollH, int rowFromBottom, int& yTop, int& yBot) const;
    void ComposeRollBuffer(CDC& dc, int width, int rollH, int histCount, const NoteFrame* hist, const NoteFrame& live) const;
    bool TryAdvanceRollBuffer(int width, int rollH, int histCount, const NoteFrame* hist, int pendingCount, const NoteFrame& live);
    void BuildLiveNoteFrame(NoteFrame& frame) const;
    void DrawPlayheadRow(CDC& dc, int width, int rollH, const NoteFrame& live) const;
    void DrawKeyboardToBuffer(CDC& dc, int width, int keySectionH, int keyH,
        const bool* activesCopy, const uint8_t* bandMaskCopy, const float laneStrengthCopy[KEY_COUNT][3],
        const float* chFillCopy, int chCountCopy, const uint8_t* exprCopy) const;
    void UpdatePianoRollTimer();

    // ---- 表示モード / 鍵盤レンジ / ノート名 ----
    void SetViewMode(int mode);
    void SetKeyRange(int keys);
    void ToggleNoteNames();
    bool IsView3D() const { return m_viewMode == 1; }
    // 表示する鍵の範囲 [lo, hi)。解析は常に 0..KEY_COUNT で不変。
    void GetDisplayKeyRange(int& lo, int& hi) const;
    void EnsureDisplayKeyCache() const;
    int  DisplayWhiteKeyCount() const;
    int  DisplayWhiteKeyIndex(int midiNote) const;   // 表示範囲内での白鍵番号(範囲外は -1)
    void Save3DAngles();
    void ApplyViewChangeRedraw();

    // ---- 簡易3D ----
    void BuildView3D(int width, int height, View3D& v) const;
    static void ProjectView3D(const View3D& v, float x, float y, float z, POINT& out);
    static void DrawBox3D(CDC& dc, const View3D& v, float xL, float xR, float topY,
        float z0, float z1, COLORREF col, float frontShade, float baseY = 0.0f);
    void Capture3DWalls();   // 履歴リングから m_wall3D を作る(m_cs 下)
    void KeyXSpan3D(int keyIndex, float& xL, float& xR) const;   // 3D 空間での鍵の左右端
    void Draw3DSceneToBuffer(CDC& dc, int width, int height,
        const bool* activesCopy, const float* chFillCopy, int chCountCopy) const;
    void Draw3DKeyboard(CDC& dc, const View3D& v, const bool* activesCopy) const;
    void Draw3DWalls(CDC& dc, const View3D& v) const;
    void Draw3DMeters(CDC& dc, const View3D& v, const float* chFillCopy, int chCountCopy) const;

    void SetRollSpeedPct(int pct);
    int  RollSpeedIndex() const;
    static void RollSpeedSliderCb(void* ctx, int value);
    static void YawSliderCb(void* ctx, int value);
    static void PitchSliderCb(void* ctx, int value);
    static void ZoomSliderCb(void* ctx, int value);
    void PushDisplayFrames();
    void ClearRollHistory();
    void RequestFullRollRedraw();

    CCustomStandardButton m_help;
    CToolTipCtrl m_tooltip;
};