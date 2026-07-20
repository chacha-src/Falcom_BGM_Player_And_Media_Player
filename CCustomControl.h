#pragma once

#include "stdafx.h"
#include "afxdialogex.h"
#include "BtnST.h"
#include "ListCtrlA.h"
#include "OSVersion.h"
#include <map>
#include <vector>
#include <dwmapi.h>
#include "DwmBlurHelper.h"

#pragma comment(lib, "dwmapi.lib")

// ============================================================================
// VS2026 アクリルぼかし機能 (_MSC_VER >= 1950 のときのみ有効)
// savedata.aero == 1 かつ VS2026ビルド時のみ背景ぼかしが適用されます。
//
// 【Win11 ぼかしの正しい構成】
//   親: DWMWA_SYSTEMBACKDROP_TYPE + ExtendFrame(-1)（余白のアクリル源）
//   親 OnPaint: WS_CLIPCHILDREN で隙間のみ薄グレー（任意）
//   子: CCustomOpaqueFixer が BufferedPaint でアルファ255の不透明面を作り、
//       その上に WM_PRINTCLIENT → OnPrintClient で描画する。
//   ※CControlFixer（白を透過）は CCustom* には使わない。
//
// 【Win10のぼかし (SetWindowCompositionAttributeを使用)】
//   非公開APIである SetWindowCompositionAttribute を使用して
//   背景にアクリルぼかし効果（ACCENT_ENABLE_BLURBEHIND 等）を適用します。
// ============================================================================
#if _MSC_VER >= 1950
#define CCUSTOM_AERO_SUPPORT 1
#else
#define CCUSTOM_AERO_SUPPORT 0
#endif

#if CCUSTOM_AERO_SUPPORT
#define CCC_MSG_REAPPLY_OPAQUE_FIXERS (WM_APP + 311)
#define CCC_WM_POST_OPAQUE_PAINT      (WM_APP + 312)
#define CCC_MSG_REFRESH_CHILDREN      (WM_APP + 313)
// 透過合成のクロマキー（黒文字 RGB(0,0,0) と区別するため 1,1,1 を使用）
#define CCC_AERO_CHROMA_KEY RGB(1, 1, 1)

struct CCC_ChromaBlitCache {
    HBITMAP hDib = NULL;
    HDC     hdcDib = NULL;
    HGDIOBJ hOldBmp = NULL;
    void*   pBits = nullptr;
    int     dibW = 0;
    int     dibH = 0;
    void Release();
    BOOL Ensure(HDC hdcRef, int w, int h);
    void ScrollRows(int y, int height, int scrollPx);
    void ScrollCols(int x, int y, int width, int height, int scrollPx);
    BOOL UpdateRect(HDC hdcSrc, int srcX, int srcY, int dx, int dy, int rw, int rh, COLORREF clrKey);
    BOOL FillOpaqueRect(int x, int y, int rw, int rh, COLORREF color, COLORREF chromaKey);
    BOOL BlitRect(HDC hdcDest, int x, int y, int w, int h);
    BOOL BlitFull(HDC hdcDest, int x, int y, int w, int h);
    // BlitFull 前にオーバーレイ等を焼き込んだ矩形のアルファを不透明にする
    void MakeRectOpaque(int x, int y, int rw, int rh);
};

void CCC_ClipNoChildren(CDC& dc, CWnd* pWnd);
void CCC_BlitStretchOpaque(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH);
void CCC_BlitStretchChroma(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH, COLORREF clrKey);
void CCC_BlitStretchNF(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH, COLORREF clrKey);
void CCC_BlitChroma(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey);
void CCC_BlitChromaNF(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey);
BOOL CCC_BlitChromaCached(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey, CCC_ChromaBlitCache& cache);
void CCC_BlitChromaDwm(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey);
void CCC_InvalidateParent(HWND hWnd, BOOL bAeroMode);
void CCC_RefreshDwmBlur(HWND hWnd);
void CCC_PaintAeroGaps(CDC& dc, CWnd* pWnd, const RECT* pPreserveRect = nullptr);
void CCC_ClearRectChroma(HDC hdcDest, const RECT& rect, COLORREF clrKey);
#endif

// ============================================================================
// 隠し機能: 淫女モード (savedata.inwoman==1 のときだけ UI 演出を盛る)
// F12 を5回連打でトグル。音は出さず見た目だけで恥ずかしくなる遊び。
// ============================================================================
extern save savedata;
static inline BOOL CCC_IsInwoman()
{
    return savedata.inwoman == 1;
}
// F12連打を監視してトグル。各メインダイアログの PreTranslateMessage から呼ぶ。
BOOL CCC_InwomanHotkey(MSG* pMsg, CWnd* pWnd);
// 淫女モードのアニメ用に全ウィンドウを定期再描画するタイマーを用意(冪等)
void CCC_StartInwomanTimer();

// モーダルダイアログを親の背後に隠さず前面へ出す(メディアプレイヤー等)
inline void CCC_BringDialogToForeground(CWnd* dlg)
{
    if (!dlg || !::IsWindow(dlg->GetSafeHwnd())) return;
    dlg->ShowWindow(SW_SHOW);
    ::SetWindowPos(dlg->GetSafeHwnd(), HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    dlg->BringWindowToTop();
    dlg->SetForegroundWindow();
}

// 最小化復帰・再表示時: オーナードロー子が親 Invalidate だけでは再描画されないため明示的に更新
void CCC_ForceRepaintHwnd(HWND hWnd);
void CCC_RefreshKids(HWND hWnd);
void CCC_GroupBoxesBack(HWND hDlg);

// ============================================================================
// 色定義
// ============================================================================
// エロ可愛い系: 色気のあるローズ／ブラッシュピンク基調へ（全コントロール共通）
#define COLOR_DIALOG_BG         RGB(255, 224, 236) // ダイアログの基本背景色（とろけるブラッシュピンク）
#define COLOR_EDIT_BG           RGB(255, 234, 238) // エディットボックスの背景色（ローズクリーム）
#define COLOR_EDIT_TEXT         RGB(0,   0,   0)   // エディットボックスの文字色
#define COLOR_LIST_BG           RGB(252, 228, 240) // リストボックスの背景色（淡いローズ）
#define COLOR_COMBO_BG          RGB(255, 234, 238) // コンボボックスの背景色
#define COLOR_BUTTON_BG         RGB(200, 232, 190) // ボタンの通常時背景色（やさしい緑：ファルコム特化型）
#define COLOR_BUTTON_PUSHED     RGB( 60, 160,  60) // ボタンの押下時背景色（濃い緑）
#define COLOR_BUTTON_HOVER      RGB(100, 200, 100) // ボタンのホバー時背景色（あかるい緑）
#define COLOR_SLIDER_THUMB      RGB(255,  92, 150) // スライダーのつまみの色（色っぽいローズ）
#define COLOR_RANGE_SLIDER_THUMB RGB(255, 255, 255) // 範囲スライダーのつまみ色
#define COLOR_RANGE_SELECTION   RGB(255, 182, 213) // 範囲スライダーの選択範囲色（ローズ）
#define COLOR_HANAMARU          RGB(255,   0,   0) // はなまるの色
#define COLOR_FLOWER_DECO       RGB(255, 240, 245) // お花の装飾色
#define COLOR_VINE_DECO         RGB(216, 132, 176) // 蔓（つる）の装飾色（ローズモーヴ）
#define COLOR_HEART             RGB(255,  86, 150) // ハートの装飾色（色っぽい濃ピンク）
#define COLOR_SEL_BG            RGB(255, 184, 212) // リストなどの選択時背景色（ローズ）
#define COLOR_GRAD_DARK_GREEN   RGB(  0, 100,   0) // グラデーション用の濃い緑
#define COLOR_GRAD_DARK_PURPLE  RGB( 75,   0, 130) // グラデーション用の濃い紫

// --- 追加: 可愛さ強化用の装飾カラー ---
#define COLOR_GLOSS             RGB(255, 255, 255) // ぷるんとした濡れツヤ(光沢)ハイライト色
#define COLOR_SPARKLE           RGB(255, 250, 205) // きらめき(キラキラ)の装飾色
#define COLOR_SPARKLE_CORE      RGB(255, 255, 255) // きらめきの白い芯
#define COLOR_BOW               RGB(255, 150, 190) // リボン(ちょうちょ結び)の色
#define COLOR_BUTTON_GLOSS_TOP  RGB(255, 240, 246) // ボタン上部のつや色
#define COLOR_BLUSH             RGB(255, 138, 176) // ほんのり頬染め(ブラッシュ)の色
#define COLOR_HEART_DEEP        RGB(229,  56, 120) // 深いローズハート(縁取り/陰影用)
#define COLOR_LACE              RGB(255, 196, 220) // レース(縁飾り)の色
#define COLOR_CHECK             RGB(226,  64, 124) // チェック(レ点)の色(深めローズ)

// アクリル半透明オーバーレイ用アルファ値
#define AERO_ALPHA_SEMI 160

// ============================================================================
// DWM / SetWindowCompositionAttribute 定数と構造体
// ============================================================================
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
// Windows 10/11 で背景ぼかしを行うための非公開API用構造体
enum WINDOWCOMPOSITIONATTRIB
{
    WCA_ACCENT_POLICY = 19
};

enum ACCENT_STATE
{
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,          // 標準的なぼかし
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,   // アクリルぼかし (Win10 1803以降)
    ACCENT_INVALID_STATE = 5
};

struct ACCENT_POLICY
{
    int AccentState;
    int AccentFlags;
    int GradientColor;
    int AnimationId;
};

struct WINCOMPATTRDATA
{
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
};

// ============================================================================
// アクリルぼかしユーティリティ (CCustomControl内完結)
// ============================================================================
#if CCUSTOM_AERO_SUPPORT
#include <afxtempl.h>

class CCustomOpaqueFixer;

BOOL CCC_IsBlurDialogChild(HWND hWnd);

// OSビルド番号取得 (ntdll RtlGetVersion経由)
static inline DWORD CCC_GetWindowsBuildNumber()
{
    typedef LONG(WINAPI* PFN_RtlGetVersion)(PRTL_OSVERSIONINFOW);
    static DWORD s_build = 0;
    static bool  s_done = false;

    // 既に取得済みならキャッシュを返す
    if (s_done) return s_build;

    HMODULE h = ::GetModuleHandleW(L"ntdll.dll");
    if (h)
    {
        PFN_RtlGetVersion fn = reinterpret_cast<PFN_RtlGetVersion>(::GetProcAddress(h, "RtlGetVersion"));
        if (fn)
        {
            RTL_OSVERSIONINFOW oi = { sizeof(oi) };
            if (fn(&oi) == 0)
            {
                s_build = oi.dwBuildNumber;
            }
        }
    }
    s_done = true;
    return s_build;
}

// Windows 11 かどうかの判定 (ビルド番号 22000 以上)
static inline BOOL CCC_IsWin11()
{
    return CCC_GetWindowsBuildNumber() >= 22000;
}

// savedata.aero==1 のときのみぼかしを有効化
static inline BOOL CCC_IsAeroEnabled()
{
    extern save savedata;
    return savedata.aero == 1;
}

static inline BOOL CCC_SetWindowCompositionAccent(HWND hWnd, const ACCENT_POLICY& policy)
{
    HMODULE hUser = ::GetModuleHandleW(L"user32.dll");
    if (!hUser) return FALSE;
    typedef BOOL(WINAPI* pSetWindowCompositionAttribute)(HWND, WINCOMPATTRDATA*);
    pSetWindowCompositionAttribute pfn =
        (pSetWindowCompositionAttribute)::GetProcAddress(hUser, "SetWindowCompositionAttribute");
    if (!pfn) return FALSE;
    WINCOMPATTRDATA data = { WCA_ACCENT_POLICY, const_cast<ACCENT_POLICY*>(&policy), sizeof(policy) };
    return pfn(hWnd, &data);
}

// 子ウィンドウの WS_EX_TRANSPARENT を設定または解除
static inline void CCC_SetChildTransparent(HWND hWnd, BOOL bOn)
{
    if (!hWnd || !::IsWindow(hWnd)) return;
    LONG ex = ::GetWindowLong(hWnd, GWL_EXSTYLE);
    if (bOn)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;
    ::SetWindowLong(hWnd, GWL_EXSTYLE, ex);
}

// 子 HWND のアクリル/Mica を無効化（親だけガラス、コントロールは不透明）
static inline void CCC_ClearChildDwmBackdrop(HWND hParent)
{
    if (!hParent || !::IsWindow(hParent)) return;
    for (HWND hChild = ::GetWindow(hParent, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        if (!::IsWindow(hChild)) continue;
        int backdropNone = 1;
        ::DwmSetWindowAttribute(hChild, DWMWA_SYSTEMBACKDROP_TYPE, &backdropNone, sizeof(backdropNone));
        CCC_ClearChildDwmBackdrop(hChild);
    }
}

// スライダー等に付いた WS_EX_TRANSPARENT を全て解除
static inline void CCC_ClearChildTrans(HWND hParent)
{
    if (!hParent || !::IsWindow(hParent)) return;
    for (HWND hChild = ::GetWindow(hParent, GW_CHILD); hChild; hChild = ::GetWindow(hChild, GW_HWNDNEXT))
    {
        CCC_SetChildTransparent(hChild, FALSE);
        CCC_ClearChildTrans(hChild);
    }
}

// CBlurDialogBase::ApplyDwmBlur と同等（Win11=SYSTEMBACKDROP、Win10=Layered+BlurBehind）
static inline BOOL CCC_ApplyAero(HWND hWnd, BOOL bAero)
{
    if (!hWnd || !::IsWindow(hWnd)) return FALSE;

    const DWORD build = CCC_GetWindowsBuildNumber();

    int backdropNone = 1;
    ::DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropNone, sizeof(backdropNone));
    MARGINS marginsZero = { 0, 0, 0, 0 };
    ::DwmExtendFrameIntoClientArea(hWnd, &marginsZero);
    ACCENT_POLICY policyOff = { ACCENT_DISABLED, 0, 0, 0 };
    CCC_SetWindowCompositionAccent(hWnd, policyOff);
    DWM_BLURBEHIND bbOff = {};
    bbOff.dwFlags = DWM_BB_ENABLE;
    bbOff.fEnable = FALSE;
    ::DwmEnableBlurBehindWindow(hWnd, &bbOff);

    LONG exStyle = ::GetWindowLong(hWnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED)
        ::SetWindowLong(hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);

    if (!bAero)
    {
        CCC_ClearChildDwmBackdrop(hWnd);
        CCC_ClearChildTrans(hWnd);
        ::SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        return FALSE;
    }

    BOOL bApplied = FALSE;

    if (build >= 22000)
    {
        BOOL compositionEnabled = FALSE;
        ::DwmIsCompositionEnabled(&compositionEnabled);
        if (compositionEnabled)
        {
            int backdropType = 3;
            if (SUCCEEDED(::DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType))))
            {
                ::EnableRoundedCorners(hWnd);
                MARGINS margins = { -1, -1, -1, -1 };
                ::DwmExtendFrameIntoClientArea(hWnd, &margins);
                bApplied = TRUE;
            }
        }
    }
    else if (build >= 10240)
    {
        exStyle = ::GetWindowLong(hWnd, GWL_EXSTYLE);
        ::SetWindowLong(hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
        ::SetLayeredWindowAttributes(hWnd, 0, 245, LWA_ALPHA);

        DWM_BLURBEHIND bb = {};
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = TRUE;
        ::DwmEnableBlurBehindWindow(hWnd, &bb);
        bApplied = TRUE;
    }

    CCC_ClearChildDwmBackdrop(hWnd);
    CCC_ClearChildTrans(hWnd);
    ::SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    return bApplied;
}

// ダイアログをアクリル用に整える（WS_CLIPCHILDREN 等）
static inline void CCC_PrepareDialogSurface(HWND hWnd, BOOL bAero)
{
    if (!hWnd || !::IsWindow(hWnd)) return;
    CWnd* pWnd = CWnd::FromHandlePermanent(hWnd);
    if (!pWnd) return;
    if (bAero)
    {
        pWnd->ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        ::SetClassLongPtr(hWnd, GCLP_HBRBACKGROUND, 0);
    }
}

#endif // CCUSTOM_AERO_SUPPORT

// ============================================================================
// コントロールの色管理用ユーティリティクラス
// CCustomControlUtility
// ============================================================================
class CCustomControlUtility
{
public:
    // デバイスコンテキストに背景色と文字色を設定し、背景描画用のブラシを返します
    static HBRUSH SetControlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor, COLORREF clrBg, COLORREF clrText = RGB(0, 0, 0))
    {
        pDC->SetBkColor(clrBg);
        pDC->SetTextColor(clrText);

        auto& m = GetBrushMap();
        auto it = m.find(clrBg);

        // 既に同じ色のブラシが作成されていればそれを再利用します
        if (it != m.end() && it->second.GetSafeHandle())
        {
            return (HBRUSH)it->second.GetSafeHandle();
        }

        // 新しいブラシを作成してマップに保存します
        m[clrBg].CreateSolidBrush(clrBg);
        return (HBRUSH)m[clrBg].GetSafeHandle();
    }

    // 特定のウィンドウに対して背景色と文字色を割り当て、再描画を促します
    static void SetControlBackgroundColor(CWnd* p, COLORREF bg, COLORREF text = RGB(0, 0, 0))
    {
        if (!p || !p->GetSafeHwnd()) return;
        PruneCCMap();
        GetCCMap()[p->GetSafeHwnd()] = { bg, text };
        p->Invalidate();
        p->UpdateWindow();
    }

    // 特定のウィンドウの色指定を解除し、再描画を促します
    static void ClearControlBackgroundColor(CWnd* p)
    {
        if (!p || !p->GetSafeHwnd()) return;

        GetCCMap().erase(p->GetSafeHwnd());
        p->Invalidate();
        p->UpdateWindow();
    }

    // マップに登録されている色情報をもとに、デバイスコンテキストに色を適用します
    static HBRUSH ApplyControlColors(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
    {
        if (!pWnd || !pWnd->GetSafeHwnd()) return NULL;

        auto it = GetCCMap().find(pWnd->GetSafeHwnd());
        if (it == GetCCMap().end()) return NULL;

        return SetControlColor(pDC, pWnd, nCtlColor, it->second.bg, it->second.text);
    }

    // ダイアログ用 CToolTipCtrl の作成。TTS_ALWAYSTIP は使わない（初期化中の誤表示防止）。
    // AddTool 完了後に FinalizeDialogToolTip を呼ぶ。
    static BOOL BeginDialogToolTip(CToolTipCtrl& tip, CWnd* pParent, DWORD style = TTS_BALLOON | TTS_NOPREFIX)
    {
        if (!pParent || !pParent->GetSafeHwnd()) return FALSE;
        if (tip.GetSafeHwnd())
        {
            tip.Activate(FALSE);
            return TRUE;
        }
        if (!tip.Create(pParent, style)) return FALSE;
        tip.Activate(FALSE);
        tip.SetDelayTime(TTDT_INITIAL, 500);
        tip.SetDelayTime(TTDT_RESHOW, 100);
        tip.SetDelayTime(TTDT_AUTOPOP, 10000);
        tip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);
        return TRUE;
    }

    // 初期化完了後に有効化し、誤って出たツールチップを消す。
    static void FinalizeDialogToolTip(CToolTipCtrl& tip, int maxTipWidth = 512, int autoPopMs = 10000)
    {
        if (!tip.GetSafeHwnd()) return;
        if (maxTipWidth > 0)
            tip.SendMessage(TTM_SETMAXTIPWIDTH, 0, maxTipWidth);
        if (autoPopMs > 0)
            tip.SetDelayTime(TTDT_AUTOPOP, autoPopMs);
        tip.Activate(TRUE);
        tip.SendMessage(TTM_POP, 0, 0);
    }

private:
    // 色情報を保持する構造体
    struct CC { COLORREF bg, text; };

    // ウィンドウハンドルと色情報の対応マップ（シングルトン）
    static std::map<HWND, CC>& GetCCMap()
    {
        static std::map<HWND, CC> s;
        return s;
    }

    // 破棄済み HWND を掃除（ダイアログ再生成でマップが肥大化しないように）
    static void PruneCCMap()
    {
        auto& m = GetCCMap();
        for (auto it = m.begin(); it != m.end(); )
        {
            if (!::IsWindow(it->first))
                it = m.erase(it);
            else
                ++it;
        }
    }

    // 背景色とブラシの対応マップ（シングルトン）
    static std::map<COLORREF, CBrush>& GetBrushMap()
    {
        static std::map<COLORREF, CBrush> s;
        return s;
    }
};

// ============================================================================
// カスタムエディットコントロール
// CCustomEdit
// ============================================================================
class CCustomEdit : public CEdit
{
    DECLARE_DYNAMIC(CCustomEdit)
public:
    CCustomEdit();
    virtual ~CCustomEdit();

    // コントロール破棄時に自動的に delete するかどうかを設定します
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
    BOOL m_bAutoDelete;

    // 最小化復帰等: WM_PRINTCLIENT は Edit 本文を描かないため明示的に再描画
    void RepaintClient();
    void DrawClientText(CDC& dc, const CRect& r);

protected:
    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // メッセージハンドラ群
    afx_msg HBRUSH CtlColor(CDC* pDC, UINT);
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg void OnNcPaint();
    afx_msg void OnSetFocus(CWnd*);
    afx_msg void OnKillFocus(CWnd*);
    afx_msg void OnEnUpdate();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg LRESULT OnPostOpaquePaint(WPARAM, LPARAM);

    DECLARE_MESSAGE_MAP()

private:
    CBrush m_brBackground; // 背景塗りつぶし用ブラシ
    CFont m_fontBold;      // 内部キャッシュフォント(太字固定ではない)

    BOOL m_bHasFocus;      // 現在フォーカスを持っているかどうか
    void PaintOpaqueClient(CDC& dc);
    void ScheduleOpaqueRepaint();
};

// ============================================================================
// テキストセグメント構造体 / カスタムスタティックコントロール
// CCustomStatic
// ============================================================================

// 装飾付きテキスト（色、太字、斜体など）を分割して保持するための構造体
struct TextSegment
{
    CString text;            // 表示する文字列
    BOOL bBold;              // 太字かどうか
    BOOL bItalic;            // 斜体かどうか
    BOOL bHasColor;          // 固有の色指定があるかどうか
    COLORREF clrText;        // 文字色
    int nFontSizeOffset;     // 基本フォントサイズからの相対サイズ（オフセット）

    // コンストラクタで初期化
    TextSegment()
        : bBold(FALSE), bItalic(FALSE), bHasColor(FALSE),
        clrText(RGB(0, 0, 0)), nFontSizeOffset(0)
    {}
};

class CCustomStatic : public CStatic
{
    DECLARE_DYNAMIC(CCustomStatic)
public:
    CCustomStatic();
    virtual ~CCustomStatic();

    // グラデーションの設定
    void SetGradation(COLORREF s, COLORREF e, int d, BOOL en);
    void GetGradation(COLORREF* ps, COLORREF* pe, int* pd, BOOL* pbe) const;

    // ドロップシャドウ（影）の設定
    void SetDropShadow(COLORREF c, int d, int dist, int blur, BOOL en);
    void GetDropShadow(COLORREF* pc, int* pd, int* pdist, int* pblur, BOOL* pbe) const;

    // ワイドモード（文字間隔を広げるなど）の設定と取得
    void SetPreferWideMode(BOOL b);
    BOOL GetPreferWideMode() const;

    // 表示フォントの設定
    void SetFont(CFont* pFont, BOOL bRedraw = TRUE);

    // コントロール破棄時に自動的に delete を行うかどうかのフラグ
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
    BOOL m_bAutoDelete;

    // アクリルモードの設定
    // TRUE のとき、背景を塗りつぶさずに文字のみを描画します。
    // これにより、ダイアログのアクリルぼかし背景が文字の背景として透けて見えます。
    void SetAeroMode(BOOL b)
    {
        m_bAeroMode = b;
        if (GetSafeHwnd()) Invalidate();
    }

    // 頻繁更新ラベル用: 親ぼかし Invalidate を抑えて UI 詰まりを防ぐ
    void SetNoParentInvalidate(BOOL b) { m_bNoParentInvalidate = b; }

protected:
    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // メッセージハンドラ
    afx_msg void OnPaint();
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg LRESULT OnSetText(WPARAM, LPARAM);
    afx_msg LRESULT OnGetText(WPARAM, LPARAM);
    afx_msg LRESULT OnGetTextLength(WPARAM, LPARAM);

    DECLARE_MESSAGE_MAP()

    void DrawClient(CDC& dc);

private:
    // 装飾タグを含むテキストを解析してセグメントに分割します
    std::vector<TextSegment> ParseFormattedText(const CString& str);

    // 分割されたテキストセグメントの描画サイズを計算します
    CSize MeasureSegmentedText(CDC* pDC, const std::vector<TextSegment>& segs, const LOGFONT& lf, int h, int w);

    // 分割されたテキストセグメントを実際に描画します
    void DrawSegmentedText(CDC* pDC, const CRect& rect, const std::vector<TextSegment>& segs, const LOGFONT& lf, int h, int w, UINT fmt);

    // プロパティ保持用メンバ変数
    COLORREF m_clrGradStart, m_clrGradEnd; // グラデーションの開始色と終了色
    int m_nGradDirection;                  // グラデーションの方向（角度）
    BOOL m_bGradEnable;                    // グラデーションが有効かどうか

    COLORREF m_clrShadow;                  // シャドウの色
    int m_nShadowDirection;                // シャドウの方向
    int m_nShadowDistance;                 // シャドウの距離
    int m_nShadowBlur;                     // シャドウのぼかし度合い
    BOOL m_bShadowEnable;                  // シャドウが有効かどうか

    CFont m_font;                          // 描画用フォント
    BOOL m_bPreferWideMode;                // ワイドモードが有効かどうか

    CString m_strText;                     // コントロールが保持しているテキスト
    CString m_strCachedText;               // キャッシュされたテキスト（再計算防止用）
    int m_nCachedHeight, m_nCachedWidth;   // キャッシュされたサイズ情報
    float m_fCachedScaleX;               // 幅オーバー時の X 軸ワールド変換倍率（1.0=なし）
    CRect m_rectCached;                    // キャッシュされた描画領域
    UINT  m_nCachedDpi;                    // キャッシュ時の DPI（Per-Monitor 対応）

    CBitmap m_memBackstore;                // ちらつき防止のダブルバッファリング用バックバッファ
    int m_backstoreW, m_backstoreH;        // バックバッファの寸法

    BOOL m_bAeroMode;                      // アクリルモードが有効かどうか
    BOOL m_bNoParentInvalidate;            // TRUE なら SetText 時に親 Invalidate しない
#if CCUSTOM_AERO_SUPPORT
    // 共有 s_nfCache のサイズ thrash を避ける（EQ コード行など固定サイズ静的ラベル向け）
    CCC_ChromaBlitCache m_chromaCache;
#endif
};

// ============================================================================
// カスタムリストボックスコントロール
// CCustomListBox
// ============================================================================
class CCustomListBox : public CListBox
{
    DECLARE_DYNAMIC(CCustomListBox)
public:
    CCustomListBox();
    virtual ~CCustomListBox();

    // コントロール破棄時に自動的に delete を行うかどうかのフラグ
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
    BOOL m_bAutoDelete;

    // アクリルモードの設定（半透明描画などを有効にします）
    void SetAeroMode(BOOL b)
    {
        m_bAeroMode = b;
        if (GetSafeHwnd()) Invalidate();
    }

protected:
    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // オーナードロー関連
    virtual void DrawItem(LPDRAWITEMSTRUCT lp);
    virtual void MeasureItem(LPMEASUREITEMSTRUCT lp);

    // メッセージハンドラ
    afx_msg HBRUSH CtlColor(CDC*, UINT);
    afx_msg void OnPaint();
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg BOOL OnEraseBkgnd(CDC*);

    DECLARE_MESSAGE_MAP()

private:
    CBrush m_brBackground; // 背景塗りつぶし用ブラシ
    BOOL m_bAeroMode;      // アクリルモードが有効かどうか
};

// ============================================================================
// カスタムコンボボックスコントロール
// CCustomComboBox
// ============================================================================
class CCustomComboBox : public CComboBox
{
    DECLARE_DYNAMIC(CCustomComboBox)
public:
    CCustomComboBox();
    virtual ~CCustomComboBox();

    // コントロール破棄時に自動的に delete を行うかどうかのフラグ
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
    BOOL m_bAutoDelete;

    // 文字列を追加します。bDisabled = TRUE の場合は無効化されたアイテムとして扱われます
    int AddString(LPCTSTR lp, BOOL bDisabled = FALSE);

    // 選択項目の取得と設定（無効化されたアイテムをスキップした「論理的な」インデックスを使用）
    int GetCurSel() const;
    int SetCurSel(int nLogical);

    // 物理インデックス（ComboBox本来の全アイテムに対するインデックス）の取得と設定
    int GetCurSelPhysical() const { return CComboBox::GetCurSel(); }
    int SetCurSelPhysical(int n) { return CComboBox::SetCurSel(n); }

    // 無効化アイテム用のラベル色設定と取得
    void SetLabelColor(COLORREF ct, COLORREF cb);
    void GetLabelColor(COLORREF* pct, COLORREF* pcb) const;

    // アクリルモードの設定
    void SetAeroMode(BOOL b)
    {
        m_bAeroMode = b;
        if (GetSafeHwnd()) Invalidate();
    }

protected:
    CBrush m_brBackground;                  // 背景塗りつぶし用ブラシ
    std::vector<BOOL> m_vDisabledItems;     // 各アイテムが無効化されているかどうかのフラグリスト
    std::vector<int> m_vSelectableIndices;  // 選択可能なアイテムの物理インデックスリスト

    COLORREF m_clrLabelText, m_clrLabelBg;  // ラベル（無効化アイテム）の文字色と背景色
    BOOL m_bAeroMode;                       // アクリルモードが有効かどうか

    // 論理インデックスと物理インデックスの相互変換
    int LogicalToPhysical(int n) const;
    int PhysicalToLogical(int n) const;

    // ドロップダウンリストの幅を、保持している文字列の長さに合わせて自動調整します
    void UpdateDropDownWidth();

    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // オーナードロー関連
    virtual void DrawItem(LPDRAWITEMSTRUCT lp);
    virtual void MeasureItem(LPMEASUREITEMSTRUCT lp);
    virtual BOOL OnCommand(WPARAM, LPARAM);

    // メッセージハンドラ
    afx_msg HBRUSH CtlColor(CDC*, UINT);
    afx_msg void OnPaint();
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg void OnDropdown();

    DECLARE_MESSAGE_MAP()

    void PaintClient(CDC& dc);
};

// ============================================================================
// カスタムリストコントロール
// CCustomListCtrl
// ============================================================================
class CCustomListCtrl : public CListCtrlA
{
    DECLARE_DYNAMIC(CCustomListCtrl)
public:
    CCustomListCtrl();
    virtual ~CCustomListCtrl();

    // コントロール破棄時に自動的に delete を行うかどうかのフラグ
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }

    // アクリルモードの設定
    void SetAeroMode(BOOL b)
    {
        m_bAeroMode = b;
        if (GetSafeHwnd()) Invalidate();
    }

    // CCustomOpaqueFixer 用: 外側の BufferedPaint バッファへ直接描画
    void PaintOpaqueIntoBuffer(HDC hdcBuf);

protected:
    BOOL m_bAutoDelete;
    BOOL m_bAeroMode;

    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // メッセージハンドラ群
    afx_msg HBRUSH CtlColor(CDC*, UINT);
    afx_msg void OnCustomDraw(NMHDR*, LRESULT*);
    afx_msg void OnMouseMove(UINT, CPoint);
    afx_msg void OnMouseLeave();
    afx_msg void OnVScroll(UINT, UINT, CScrollBar*);
    afx_msg void OnHScroll(UINT, UINT, CScrollBar*);
    afx_msg BOOL OnMouseWheel(UINT, short, CPoint);
    afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg void OnPaint();
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg LRESULT OnPostOpaquePaint(WPARAM, LPARAM);
    // リストが WS_EX_ACCEPTFILES を持つ場合に、リスト上へのファイルドロップを
    // 親ダイアログへ転送する(リストが親を覆い、親がドロップを受け取れない問題への対処)。
    afx_msg void OnDropFiles(HDROP hDropInfo);

    DECLARE_MESSAGE_MAP()

private:
    CBrush m_brBackground; // 背景塗りつぶし用ブラシ
    int m_nHotItem;        // 現在マウスカーソルが乗っているアイテムのインデックス（ホバー処理用）

    // ホバー状態のアイテムを更新し、必要に応じて再描画します
    void UpdateHotItem(int n);
    void UpdateHotItemFromCursor();

    // 表示されているアイテム領域を再描画します
    void RedrawVisibleItems();
    void PaintOpaqueClient(CDC& dc);
    void ScheduleOpaqueRepaint();
};

// ============================================================================
// カスタム標準ボタンコントロール
// CCustomStandardButton (常に不透明で描画されます)
// ============================================================================
class CCustomStandardButton : public CButton
{
    DECLARE_DYNAMIC(CCustomStandardButton)
public:
    CCustomStandardButton();
    virtual ~CCustomStandardButton();

    // コントロール破棄時に自動的に delete を行うかどうかのフラグ
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
    BOOL m_bAutoDelete;

    // 背景のグラデーション設定
    void SetGradation(COLORREF s, COLORREF e, int d, BOOL en);
    void GetGradation(COLORREF* ps, COLORREF* pe, int* pd, BOOL* pbe) const;

    // テキストのドロップシャドウ（影）設定
    void SetDropShadow(COLORREF c, int d, int dist, int blur, BOOL en);
    void GetDropShadow(COLORREF* pc, int* pd, int* pdist, int* pblur, BOOL* pbe) const;

    void PaintClient(CDC& dc, const CRect& r);
    void RepaintClient();
    void EnsureAnimTimer();   // 押下状態変化後に流れるアニメを再開

protected:
    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // メッセージハンドラ
    afx_msg HBRUSH CtlColor(CDC*, UINT);
    afx_msg void OnPaint();
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg void OnMouseMove(UINT, CPoint);
    afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
    afx_msg void OnSetFocus(CWnd*);
    afx_msg void OnKillFocus(CWnd*);
    afx_msg void OnEnable(BOOL);
    afx_msg void OnTimer(UINT_PTR nIDEvent);

    DECLARE_MESSAGE_MAP()

private:
    CBrush m_brBackground; // 背景塗りつぶし用ブラシ
    BOOL m_bMouseOver;     // マウスカーソルがボタンに乗っているかどうか
    UINT m_nAnimTick;      // アニメーション用カウンタ(流れるツヤ・鼓動パルス)
    BOOL m_bAnimRunning;   // アニメーションタイマー動作中か
    void UpdateAnimTimer(); // ホバー/フォーカス状態に応じてタイマーを開始/停止
    void PaintOpaqueClient(CDC& dc);

    // プロパティ保持用メンバ変数
    COLORREF m_clrGradStart, m_clrGradEnd;
    int m_nGradDirection;
    BOOL m_bGradEnable;

    COLORREF m_clrShadow;
    int m_nShadowDirection, m_nShadowDistance, m_nShadowBlur;
    BOOL m_bShadowEnable;
};

// ============================================================================
// カスタムスライダーコントロール
// CCustomSliderCtrl
// aeroMode=TRUE : WS_EX_TRANSPARENT を適用し、ダイアログのアクリル背景が透けて見えます
//                 （描画物のみ上乗せされます）
// aeroMode=FALSE: 通常通り COLOR_DIALOG_BG で背景を塗りつぶします
// ============================================================================
class CCustomSliderCtrl : public CSliderCtrl
{
    DECLARE_DYNAMIC(CCustomSliderCtrl)
public:
    CCustomSliderCtrl();
    virtual ~CCustomSliderCtrl();

    // コントロール破棄時に自動的に delete を行うかどうかのフラグ
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
    BOOL m_bAutoDelete;

    // 描画モードの設定・取得（0, 1, 2 などでデザインを切り替えます）
    void SetMode(int m);
    int GetMode() const { return m_nMode; }

    // スライダーの位置設定
    void SetPos(int nPos, BOOL bRedraw = TRUE);

    // アクリルモードの設定
    void SetAeroMode(BOOL b);

protected:
    int m_nMode;      // 現在の描画モード
    BOOL m_bAeroMode; // アクリルモードが有効かどうか

    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // メッセージハンドラ
    afx_msg void OnPaint();
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg LRESULT OnMouseMoveMsg(WPARAM, LPARAM);
    afx_msg LRESULT OnLButtonDownMsg(WPARAM, LPARAM);
    afx_msg LRESULT OnLButtonUpMsg(WPARAM, LPARAM);
    afx_msg LRESULT OnMouseLeaveMsg(WPARAM, LPARAM);
    afx_msg void OnTimer(UINT_PTR nIDEvent);

    DECLARE_MESSAGE_MAP()

    void PaintClient(CDC& dc);

private:
    UINT m_nShimmer; // 流れるシマー用カウンタ
    BOOL m_bHover;   // マウスがスライダー上にあるか

    // 描画モードごとの実際の描画処理
    void DrawSlider(CDC* pDC);
    void DrawMode0(CDC* pDC, const CRect& r, int mn, int mx, int pos);
    void DrawMode1(CDC* pDC, const CRect& r, int mn, int mx, int pos);
    void DrawMode2(CDC* pDC, const CRect& r, int mn, int mx, int pos);
};

// ============================================================================
// カスタム範囲スライダーコントロール
// CCustomRangeSliderCtrl
// 2つのつまみを持ち、最小値・最大値の範囲を選択できるスライダーです。
// （アクリルモードの挙動は CCustomSliderCtrl と同様です）
// ============================================================================
class CCustomRangeSliderCtrl : public CSliderCtrl
{
    DECLARE_DYNAMIC(CCustomRangeSliderCtrl)
public:
    CCustomRangeSliderCtrl();
    virtual ~CCustomRangeSliderCtrl();

    // コントロール破棄時に自動的に delete を行うかどうかのフラグ
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
    BOOL m_bAutoDelete;

    // 全体の範囲設定
    void SetRange(int mn, int mx, BOOL bRedraw = TRUE);

    // 選択された範囲（つまみの位置）の設定・取得
    void SetSelection(int mn, int mx);
    void GetSelection(int& mn, int& mx) const;

    // 現在の位置（ドラッグ中の仮想位置ではなく確定位置）の設定・取得
    void SetPos(int nPos);
    int GetPos() const;

    // 再生追従用: 範囲・選択・位置を一括更新し、見た目(px)が変わったときだけ1回 UPDATENOW
    // (旧: SetSelection→SetPos の二重 UPDATENOW / 新: Invalidate 合流で再生が進むほど遅延)
    void SetPlaybackMirror(int nPos, int selMin, int selMax, int rangeMin, int rangeMax);

    // 範囲(最小/最大)の取得(メディアプレイヤー画面のシーク表示用)
    int GetMinValue() const { return m_nMin; }
    int GetMaxValue() const { return m_nMax; }

    // アクリルモードの設定
    void SetAeroMode(BOOL b);

protected:
    BOOL m_bAeroMode; // アクリルモードが有効かどうか

    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // メッセージハンドラ
    afx_msg void OnPaint();
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg void OnLButtonDown(UINT, CPoint);
    afx_msg void OnLButtonUp(UINT, CPoint);
    afx_msg void OnMouseMove(UINT, CPoint);

    DECLARE_MESSAGE_MAP()

    void PaintClient(CDC& dc);

private:
    void DrawRangeSlider(CDC* pDC);

    // 値とピクセル座標の相互変換、マウスクリック時のヒットテスト
    int ValueToPixel(int v) const;
    int PixelToValue(int x) const;
    int HitTest(CPoint p) const; // 戻り値: 1=最小つまみ, 2=最大つまみ, 3=全体, 0=なし

    // 状態保持用メンバ変数
    int m_nMin, m_nMax;         // 全体の最小値・最大値
    int m_nSelMin, m_nSelMax;   // 選択されている最小値・最大値
    int m_nDragTarget;          // 現在ドラッグしている対象（HitTestの戻り値に対応）
    int m_nLogicalPos;          // 確定された論理位置
    BOOL m_bDragging;           // ドラッグ中かどうか
    int m_nVisualPos;           // ドラッグ中の見た目上の位置
};

// ============================================================================
// カスタムチェックボックスコントロール
// CCustomCheckBox
// ============================================================================
class CCustomCheckBox : public CButton
{
    DECLARE_DYNAMIC(CCustomCheckBox)
public:
    CCustomCheckBox();
    virtual ~CCustomCheckBox();

    // コントロール破棄時に自動的に delete を行うかどうかのフラグ
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
    BOOL m_bAutoDelete;

    // フォントとチェック状態の設定・取得
    void SetFont(CFont* pFont, BOOL bRedraw = TRUE);
    int GetCheck();
    void SetCheck(int n);

    // アクリルモードの設定
    void SetAeroMode(BOOL b)
    {
        m_bAeroMode = b;
        if (GetSafeHwnd()) Invalidate();
    }

protected:
    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // メッセージハンドラ
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC*);
    afx_msg void OnLButtonDown(UINT, CPoint);
    afx_msg void OnLButtonUp(UINT, CPoint);
    afx_msg void OnMouseMove(UINT, CPoint);
    afx_msg void OnMouseLeave();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);

    DECLARE_MESSAGE_MAP()

private:
    void OnDrawLayer(CDC* pDC, CRect rect); // 実際の描画処理
    void StartCheckBounce();                // チェックON時のぷるんバウンス開始

    // 状態保持用メンバ変数
    BOOL m_bIsFlatStyle; // フラットスタイルかどうか
    BOOL m_bIsPressed;   // 押下状態かどうか
    BOOL m_bIsHot;       // ホバー状態かどうか
    BOOL m_bTracking;    // マウストラッキング中かどうか
    int m_nCheck;        // チェック状態 (BST_CHECKED / BST_UNCHECKED)
    BOOL m_bAeroMode;    // アクリルモードが有効かどうか
    int m_nBounce;       // チェックON時のバウンス残りフレーム
};

// ============================================================================
// カスタムグループボックスコントロール
// CCustomGroupBox
// ============================================================================
class CCustomGroupBox : public CButton
{
    DECLARE_DYNAMIC(CCustomGroupBox)
public:
    CCustomGroupBox();
    virtual ~CCustomGroupBox();

    // コントロール破棄時に自動的に delete を行うかどうかのフラグ
    void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
    BOOL m_bAutoDelete;

    // アクリルモードの設定
    void SetAeroMode(BOOL b)
    {
        m_bAeroMode = b;
        if (GetSafeHwnd()) Invalidate();
    }

protected:
    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // メッセージハンドラ
    afx_msg void OnPaint();
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg BOOL OnEraseBkgnd(CDC*);

    DECLARE_MESSAGE_MAP()

private:
    void DrawGroupBox(CDC* pDC, CRect& rect); // グループボックスの枠線とテキスト描画
    BOOL m_bAeroMode;                         // アクリルモードが有効かどうか
};

// ============================================================================
// アクリル設定を子コントロールへ伝播させるマクロ
// PROPAGATE_AERO_TO_CHILDREN
// ============================================================================
// ラベル・スライダー・グループ枠は透過、リスト・ボタン類は不透明
#define PROPAGATE_AERO_TO_CHILDREN(hWndParent, bAero)                                  \
do {                                                                                   \
    const BOOL _bA = (bAero);                                                          \
    HWND _hc = ::GetWindow((hWndParent), GW_CHILD);                                    \
    while (_hc) {                                                                      \
        CWnd* _pw = CWnd::FromHandlePermanent(_hc);                                    \
        if (_pw) {                                                                     \
            if      (auto* p = dynamic_cast<CCustomStatic*>(_pw))          p->SetAeroMode(_bA); \
            else if (auto* p = dynamic_cast<CCustomGroupBox*>(_pw))        p->SetAeroMode(_bA); \
            else if (auto* p = dynamic_cast<CCustomSliderCtrl*>(_pw))      p->SetAeroMode(_bA); \
            else if (auto* p = dynamic_cast<CCustomRangeSliderCtrl*>(_pw)) p->SetAeroMode(_bA); \
            else if (auto* p = dynamic_cast<CCustomListBox*>(_pw))         p->SetAeroMode(FALSE); \
            else if (auto* p = dynamic_cast<CCustomComboBox*>(_pw))        p->SetAeroMode(FALSE); \
            else if (auto* p = dynamic_cast<CCustomListCtrl*>(_pw))        p->SetAeroMode(FALSE); \
            else if (auto* p = dynamic_cast<CCustomCheckBox*>(_pw))        p->SetAeroMode(_bA); \
            CCC_SetChildTransparent(_hc, FALSE);                                       \
            _pw->Invalidate();                                                         \
        }                                                                              \
        _hc = ::GetWindow(_hc, GW_HWNDNEXT);                                           \
    }                                                                                  \
} while(0)

// ============================================================================
// カスタムダイアログクラス (CDialog派生)
// CCustomDialog
// ============================================================================
// 基底は CDialogEx。CDialog 派生だと非アクティブ時に DWM アクリル背景が落ちる
// (EQ/簡易ピアノロール等 CDialogEx 派生窓だけが非アクティブでもアクリルを維持する)。
// この基底を CDialogEx にすることで、派生する全ぼかし窓を一括で維持側に揃える。
class CCustomDialog : public CDialogEx
{
    DECLARE_DYNAMIC(CCustomDialog)
public:
    CCustomDialog();
    CCustomDialog(UINT nIDTemplate, CWnd* pParentWnd = NULL);
    virtual ~CCustomDialog();

    // アクリルぼかし機能の有効・無効を切り替えます
    void EnableAero(BOOL bEnable);
    BOOL IsAeroEnabled() const { return m_bAeroEnabled; }

protected:
    virtual BOOL OnInitDialog();

    // メッセージハンドラ
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnPaint();
    afx_msg LRESULT OnSubclassControls(WPARAM, LPARAM);

    DECLARE_MESSAGE_MAP()

    CBrush m_brDialog;   // ダイアログの背景塗りつぶし用ブラシ
    BOOL m_bAeroEnabled; // アクリルぼかしが有効かどうか

    // Windows 11 のアクリル背景時に使用する透明ブラシ (NULL_BRUSH 相当)
    CBrush m_brNull;

private:
    // ダイアログ上の標準コントロールをカスタムコントロールに自動置換(サブクラス化)します
    void SubclassChildControls();
};

// ============================================================================
// ぼかし適用済みカスタムダイアログの基底クラス (CDialog派生)
// CCustomBlurDialogBase
// ============================================================================
class CCustomBlurDialogBase : public CCustomDialog
{
    DECLARE_DYNAMIC(CCustomBlurDialogBase)
public:
    CCustomBlurDialogBase();
    CCustomBlurDialogBase(UINT nIDTemplate, CWnd* pParent = NULL);
    virtual ~CCustomBlurDialogBase();

    // モード切替・子再配置後など、ぼかしを強制再適用する
    void RefreshAeroMode();

    // ウィンドウ右上に「メインに追従」チェック。bOverlayPaint=TRUE で GDI 全画面描画向け
    void EnableMainWindowLock(int* pSavedLockFlag, BOOL bOverlayPaint = FALSE);

protected:
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
    virtual BOOL OnInitDialog();

    // savedata.aero==1 のとき DWM ぼかしを適用（既適用なら no-op）
    virtual void ApplyDwmBlur();
    afx_msg void OnPaint();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
    afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
    afx_msg void OnCompositionChanged();
    afx_msg void OnDestroy();
    afx_msg LRESULT OnReapplyOpaqueFixers(WPARAM wParam, LPARAM lParam);
    afx_msg void OnMainLockClicked();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMoving(UINT fwSide, LPRECT pRect);

    DECLARE_MESSAGE_MAP()

private:
    void ApplyDwmBlurCore(BOOL bForce);
    BOOL m_bBlurApplied;
    CTypedPtrList<CPtrList, CCustomOpaqueFixer*> m_opaqueFixers;
    int* m_pMainLockSave = nullptr;
};

// ============================================================================
// カスタムダイアログクラス (CDialogEx派生)
// CCustomDialogEx
// ============================================================================
class CCustomDialogEx : public CDialogEx
{
    DECLARE_DYNAMIC(CCustomDialogEx)
public:
    CCustomDialogEx();
    CCustomDialogEx(UINT nIDTemplate, CWnd* pParentWnd = NULL);
    virtual ~CCustomDialogEx();

    // アクリルぼかし機能の有効・無効を切り替えます
    void EnableAero(BOOL bEnable);
    BOOL IsAeroEnabled() const { return m_bAeroEnabled; }

protected:
    virtual BOOL OnInitDialog();

    // メッセージハンドラ
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg BOOL OnEraseBkgnd(CDC* pDC);
    afx_msg void OnPaint();
    afx_msg LRESULT OnSubclassControls(WPARAM, LPARAM);

    DECLARE_MESSAGE_MAP()

    CBrush m_brDialog;   // ダイアログの背景塗りつぶし用ブラシ
    BOOL m_bAeroEnabled; // アクリルぼかしが有効かどうか

    // Windows 11 のアクリル背景時に使用する透明ブラシ (NULL_BRUSH 相当)
    CBrush m_brNull;

private:
    // ダイアログ上の標準コントロールをカスタムコントロールに自動置換(サブクラス化)します
    void SubclassChildControls();
};

// ============================================================================
// ぼかし適用済みカスタムダイアログの基底クラス (CDialogEx派生)
// CCustomBlurDialogExBase
// ============================================================================
class CCustomBlurDialogExBase : public CCustomDialogEx
{
    DECLARE_DYNAMIC(CCustomBlurDialogExBase)
public:
    CCustomBlurDialogExBase();
    CCustomBlurDialogExBase(UINT nIDTemplate, CWnd* pParent = nullptr);
    virtual ~CCustomBlurDialogExBase();

    // モード切替・子再配置後など、ぼかしを強制再適用する
    void RefreshAeroMode();

    // ウィンドウ右上に「メインに追従」チェック。bOverlayPaint=TRUE で GDI 全画面描画向け
    void EnableMainWindowLock(int* pSavedLockFlag, BOOL bOverlayPaint = FALSE);

protected:
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
    virtual BOOL OnInitDialog();

    // savedata.aero==1 のとき DWM ぼかしを適用（既適用なら no-op）
    virtual void ApplyDwmBlur();
    afx_msg void OnPaint();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnShowWindow(BOOL bShow, UINT nStatus);
    afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
    afx_msg void OnCompositionChanged();
    afx_msg void OnDestroy();
    afx_msg LRESULT OnReapplyOpaqueFixers(WPARAM wParam, LPARAM lParam);
    afx_msg void OnMainLockClicked();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMoving(UINT fwSide, LPRECT pRect);

    DECLARE_MESSAGE_MAP()

private:
    void ApplyDwmBlurCore(BOOL bForce);
    BOOL m_bBlurApplied;
    CTypedPtrList<CPtrList, CCustomOpaqueFixer*> m_opaqueFixers;
    int* m_pMainLockSave = nullptr;
};
