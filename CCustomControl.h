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

// CCustom* 一覧（実装は CCustomControl.cpp / CCustomPopupMenu.cpp）
//   子控件: Edit / Static / ListBox / ComboBox / ListCtrl / TreeCtrl / TabCtrl
//           StandardButton / Slider / RangeSlider / CheckBox / LevelMeter
//           Progress / SysPerf / GroupBox
//   ダイアログ: CCustomDialog(Ex) → CCustomBlurDialog(Ex)Base
//   ポップアップ: CCustomPopupMenu（不透明。ガラスは載せない）
//   ガラス子の α=255 化: CCustomOpaqueFixer（cpp 内クラス。ヘッダは前方宣言のみ）

#define CCC_MSG_INSTALL_CAPTION       (WM_APP + 314) // キャプション帯を後付け（Show 前に Post）

#if CCUSTOM_AERO_SUPPORT
#define CCC_MSG_REAPPLY_OPAQUE_FIXERS (WM_APP + 311) // 子 HWND 増減後に fixer を張り直し
#define CCC_WM_POST_OPAQUE_PAINT      (WM_APP + 312) // 1 フレーム遅延の不透明再描画（再入回避）
#define CCC_MSG_REFRESH_CHILDREN      (WM_APP + 313) // 子の Invalidate 一括
// 透過合成のクロマキー（黒文字 RGB(0,0,0) と区別するため 1,1,1 を使用）
#define CCC_AERO_CHROMA_KEY RGB(1, 1, 1)

struct CCC_ChromaBlitCache {
    HBITMAP hDib = NULL;      // 32bpp DIB。α 付き
    HDC     hdcDib = NULL;
    HGDIOBJ hOldBmp = NULL;
    void*   pBits = nullptr;  // トップダウン。ピッチは 4*dibW
    int     dibW = 0;
    int     dibH = 0;
    void Release(); // DIB/DC 解放。Ensure の作り直し前にも呼ぶ
    BOOL Ensure(HDC hdcRef, int w, int h); // 同じサイズなら再利用
    void ScrollRows(int y, int height, int scrollPx); // 縦スクロール差分
    void ScrollCols(int x, int y, int width, int height, int scrollPx);
    BOOL UpdateRect(HDC hdcSrc, int srcX, int srcY, int dx, int dy, int rw, int rh, COLORREF clrKey);
    // クロマ無し: BitBlt + α=255。キャプションガラス下の本文提示用（差分更新向き）
    BOOL UpdateOpaqueRect(HDC hdcSrc, int srcX, int srcY, int dx, int dy, int rw, int rh);
    BOOL FillOpaqueRect(int x, int y, int rw, int rh, COLORREF color, COLORREF chromaKey); // color==key なら α=0
    BOOL BlitRect(HDC hdcDest, int x, int y, int w, int h); // 差分矩形
    BOOL BlitFull(HDC hdcDest, int x, int y, int w, int h); // 全面
    // BlitFull 前にオーバーレイ等を焼き込んだ矩形のアルファを不透明にする
    void MakeRectOpaque(int x, int y, int rw, int rh);
};

// 子 HWND を除外して親の隙間だけ塗る（WS_CLIPCHILDREN と対）。dc は親。
void CCC_ClipNoChildren(CDC& dc, CWnd* pWnd);
// 不透明伸縮。ガラス上の本文パネル縮小に使う。α は触らない。
void CCC_BlitStretchOpaque(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH);
// クロマ伸縮。clrKey 画素を α=0 にしてガラスを透かす。
void CCC_BlitStretchChroma(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH, COLORREF clrKey);
// 非フリッカー版伸縮（オフスクリーン DIB）。ラベルの毎フレーム伸縮向け。
void CCC_BlitStretchNF(HDC hdcDest, int x, int y, int destW, int destH,
    HDC hdcSrc, int srcX, int srcY, int srcW, int srcH, COLORREF clrKey);
// 1:1 クロマ。ソースの clrKey を α=0。黒文字はキーにしない（RGB(1,1,1) を使う）。
void CCC_BlitChroma(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey);
void CCC_BlitChromaNF(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey);
BOOL CCC_BlitChromaCached(HDC hdcDest, int x, int y, int w, int h,
    HDC hdcSrc, int srcX, int srcY, COLORREF clrKey, CCC_ChromaBlitCache& cache);
// DWM 合成向け。BufferedPaint 面へ直接。OpaqueFixer 内から呼ぶ。
void CCC_BlitChromaDwm(HDC hdcDest, int x, int y, int w, int h, HDC hdcSrc, int srcX, int srcY, COLORREF clrKey);
// アクリル時のみ親を Invalidate（ラベル SetText の既定）。NoParentInvalidate 時は呼ばない。
void CCC_InvalidateParent(HWND hWnd, BOOL bAeroMode);
void CCC_RefreshDwmBlur(HWND hWnd); // 組成変更後にぼかしを張り直す（FRAMECHANGED は呼び出し側）
void CCC_PaintAeroGaps(CDC& dc, CWnd* pWnd, const RECT* pPreserveRect = nullptr); // 子の隙間だけ薄グレー
void CCC_ClearRectChroma(HDC hdcDest, const RECT& rect, COLORREF clrKey); // 矩形をキー色で潰して透かす
// アクリルホスト上に定数αの矩形を塗る（本文パネルの透け用）
void CCC_FillRectAlpha(HDC hdc, const RECT& rc, COLORREF clr, BYTE alpha);
#endif

// ============================================================================
// 隠し機能: 複合ファンクション連打で入る裏演出 (savedata.inwoman==1)。
// 出口は F12 を短時間に5回。入り口は F12×7 → F11×7 → F12 を2秒押しっぱなし。
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
// 不透明パネル等への淫女オーバーレイ描画（ポップアップメニュー等から利用）
void CCC_DrawInwoman(CDC* pDC, const CRect& rc, BOOL bAeroTrans);
// aero=0 の GDI キャンバス（ピアノロール／アナライザ等）へ裸体を重ねる
void CCC_DrawInwomanOnRect(CDC* pDC, const CRect& rc);
void CCC_DrawInwomanOnClient(CDC* pDC, HWND hWnd);
void CCC_CaptionPaintGdi(CDC& dc, HWND hDlg);
// ピクン時の控件全体シェイク量(非淫女/静止時は 0,0)
void CCC_InwomanGetShake(int& dx, int& dy);
// コンテキストメニューからアクリルON/OFFしたとき全UIへ再適用
void CCC_NotifyAeroSettingChanged();

// ツール窓のキャプション／タスクバー用。iconId=0 は何もしない（MP/本窓は各自 SetIcon）。
void CCC_ApplyDlgResourceIcon(CWnd* w, UINT iconId);
UINT CCC_IconIdForDialogTemplate(UINT idd);
void CCC_ApplyWindowIconFromTemplate(CWnd* w, UINT idd);
HICON CCC_LoadSharedIcon(UINT iconId, int px = 24);
UINT CCC_CtlIconForCtrl(UINT id);

HCURSOR CCC_LoadUiCursor(UINT id);
BOOL CCC_SetUiCursor(UINT id);

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
#define COLOR_LIST_BG           RGB(252, 236, 244) // リスト偶数行（淡いローズ）
#define COLOR_LIST_ALT          RGB(230, 244, 238) // リスト奇数行（薄いミント）
#define COLOR_COMBO_BG          RGB(255, 234, 238) // コンボボックスの背景色
#define COLOR_BUTTON_BG         RGB(200, 232, 190) // ボタンの通常時背景色（やさしい緑：ファルコム特化型）
#define COLOR_BUTTON_PUSHED     RGB( 60, 160,  60) // ボタンの押下時背景色（濃い緑）
#define COLOR_BUTTON_HOVER      RGB(100, 200, 100) // ボタンのホバー時背景色（あかるい緑）
#define COLOR_SLIDER_THUMB      RGB(255,  92, 150) // スライダーのつまみの色（色っぽいローズ）
#define COLOR_RANGE_SLIDER_THUMB RGB(255, 255, 255) // 範囲スライダーのつまみ色(loop1/2)
#define COLOR_RANGE_SELECTION   RGB(255, 182, 213) // 範囲スライダーの選択範囲色（ローズ=ループ）
#define COLOR_AB_SLIDER_THUMB   RGB(64, 160, 255)  // A-B つまみ（A単独時からこの色）
#define COLOR_AB_RANGE          RGB(90, 210, 150)  // A-B 区間塗り（B確定後）
#define COLOR_SEEK_WAVE         RGB(120, 170, 220) // シーク波形オーバービュー
#define COLOR_SEEK_CUE          RGB(255, 190, 60)  // キュー／マーカー
#define COLOR_HANAMARU          RGB(255,   0,   0) // はなまるの色
#define COLOR_FLOWER_DECO       RGB(255, 240, 245) // お花の装飾色
#define COLOR_VINE_DECO         RGB(216, 132, 176) // 蔓（つる）の装飾色（ローズモーヴ）
#define COLOR_HEART             RGB(255,  86, 150) // ハートの装飾色（色っぽい濃ピンク）
// 選択: ローズ／ミント交互と被らない薄ラベンダー（コバルトは強すぎて見づらい）
#define COLOR_SEL_BG            RGB(176, 158, 228)
#define COLOR_LIST_SEL_TEXT     RGB( 40,  28,  88) // 選択行文字（濃い紫、白より目に優しい）
#define COLOR_EDIT_SEL_BG       RGB( 51, 120, 210) // Edit選択背景(青・文字と対比)
#define COLOR_EDIT_SEL_TEXT     RGB(255, 255, 255) // Edit選択文字
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
    int AccentState;    // ACCENT_ENABLE_BLURBEHIND 等。0=無効
    int AccentFlags;    // 未使用寄り。0 でよい
    int GradientColor;  // AABBGGRR。アクリル時のティント
    int AnimationId;    // 未使用。0
};

struct WINCOMPATTRDATA
{
    WINDOWCOMPOSITIONATTRIB Attrib; // WCA_ACCENT_POLICY
    PVOID pvData;                   // ACCENT_POLICY*
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

// Win10 非公開 SetWindowCompositionAttribute。失敗しても FALSE を返すだけ（必須ではない）。
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

#ifndef DWMWA_REDIRECTIONBITMAP_ALPHA
#define DWMWA_REDIRECTIONBITMAP_ALPHA 39
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

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
        // AcrylicCaption の再適用はここではしない（ApplyAero 内の FRAMECHANGED と
        // EnsureBackdrop の二重化が aero 切替フリーズの原因）。呼び出し側で EnsureBackdrop。
        if (build >= 26100)
        {
            BOOL useAlpha = FALSE;
            ::DwmSetWindowAttribute(hWnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
        }
        ::SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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
                if (build >= 26100) {
                    BOOL useAlpha = TRUE;
                    ::DwmSetWindowAttribute(hWnd, DWMWA_REDIRECTIONBITMAP_ALPHA, &useAlpha, sizeof(useAlpha));
                }
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
    // キャプション帯(AcrylicCaption)がある窓も背景ブラシを外す
    if (bAero || CCC_AcrylicCaption(hWnd))
    {
        ::SetClassLongPtr(hWnd, GCLP_HBRBACKGROUND, 0);
        // ホスト α 時は常に CLIPCHILDREN。外すと親の不透明塗りが子の NC(スクロールバー)を潰す
        pWnd->ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
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
//
// ガラス親では OpaqueFixer。システムキャレットは不透明化で消えるため自前点滅。
// IME 候補は SyncImePos。複数行は可視行だけ DrawMultilineVisibleText。
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
    void SetAeroMode(BOOL b) { m_bAeroMode = b; if (GetSafeHwnd()) Invalidate(FALSE); }
    BOOL m_bAeroMode;

    // 最小化復帰等: WM_PRINTCLIENT は Edit 本文を描かないため明示的に再描画
    void RepaintClient();
    void DrawClientText(CDC& dc, const CRect& r);
    void PaintOpaqueFrame(); // NC枠(フォーカス色含む)を α=255 で描く
    // Opaque 再描画でシステムキャレットが消えるため自前描画。IME 候補位置もここから合わせる
    BOOL GetCaretClientPos(CPoint& pt, int& lineH);
    void DrawCaretIfNeeded(CDC& dc);
    void SyncImePos();

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
    afx_msg void OnKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg LRESULT OnPostOpaquePaint(WPARAM, LPARAM);
    afx_msg LRESULT OnImeStartComposition(WPARAM, LPARAM);
    afx_msg LRESULT OnImeComposition(WPARAM, LPARAM);
    afx_msg LRESULT OnImeNotify(WPARAM, LPARAM);

    DECLARE_MESSAGE_MAP()

private:
    CBrush m_brBackground; // 背景塗りつぶし用ブラシ
    void DrawEditFrame(CDC& dc, const CRect& rWin); // NC 相当の枠。フォーカス色
    CFont m_fontBold;      // 内部キャッシュフォント(太字固定ではない)

    BOOL m_bHasFocus;      // 現在フォーカスを持っているかどうか
    BOOL m_bSelDrag;       // マウスで範囲選択中
    BOOL m_bCaretOn;       // 自前キャレット点滅
    int m_lastSel0;        // 選択始端。変化検知用スナップ
    int m_lastSel1;        // 選択終端
    void PaintOpaqueClient(CDC& dc);     // OpaqueFixer 面へ本文＋枠＋自前キャレット
    void ScheduleOpaqueRepaint();        // CCC_WM_POST_OPAQUE_PAINT を Post（再入回避）
    void RepaintIfSelChanged();          // 選択矩形だけ Invalidate
    void DrawMultilineVisibleText(CDC& dc, const CRect& rc); // ES_MULTILINE 可視行のみ
    void StartCaretBlink(); // 500ms。システムキャレットは隠したまま
    void StopCaretBlink();
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

    // 不透明塗りつぶし背景(LRC 現在行ハイライト等)。透過モード時は無視。
    void SetSolidFill(BOOL en, COLORREF c)
    {
        if (m_bSolidFill == en && m_clrSolidFill == c) return;
        m_bSolidFill = en;
        m_clrSolidFill = c;
        if (GetSafeHwnd()) Invalidate(FALSE);
    }

    // 頻繁更新ラベル用: 親ぼかし Invalidate を抑えて UI 詰まりを防ぐ
    void SetNoParentInvalidate(BOOL b) { m_bNoParentInvalidate = b; }

    // OpaqueFixer 経由の不透明バッファへ自前描画する（TRUE=描画済みで DrawClient を使わない）
    virtual BOOL PaintCustomOpaque(CDC& dc) { UNREFERENCED_PARAMETER(dc); return FALSE; }

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
    // 装飾タグを含むテキストを解析して m_segs に格納（std::vector 禁止・長時間断片化防止）
    void ParseFormattedText(const CString& str);

    // 分割されたテキストセグメントの描画サイズを計算します
    CSize MeasureSegmentedText(CDC* pDC, const LOGFONT& lf, int h, int w);

    // 分割されたテキストセグメントを実際に描画します
    void DrawSegmentedText(CDC* pDC, const CRect& rect, const LOGFONT& lf, int h, int w, UINT fmt);

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

    // 装飾テキストの解析結果キャッシュ（毎描画の vector/CString 積みを防ぐ）
    static constexpr int kMaxTextSegs = 64;
    TextSegment m_segs[kMaxTextSegs];
    int m_segCount;
    CString m_strSegSource;                // m_segs の元文字列（一致時は再解析しない）

    BOOL m_bAeroMode;                      // アクリルモードが有効かどうか
    BOOL m_bNoParentInvalidate;            // TRUE なら SetText 時に親 Invalidate しない
    BOOL m_bSolidFill;                     // カスタム不透明背景
    COLORREF m_clrSolidFill;
#if CCUSTOM_AERO_SUPPORT
    // 共有 s_nfCache のサイズ thrash を避ける（EQ コード行など固定サイズ静的ラベル向け）
    CCC_ChromaBlitCache m_chromaCache;
#endif
};

// ============================================================================
// カスタムリストボックスコントロール
// CCustomListBox
//
// オーナードロー。交互色＋選択ラベンダー。ガラス親では不透明バッファ。
// ドロップダウン内（コンボ子）でも同じ経路。空領域は最終行より下を塗る。
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
    virtual void DrawItem(LPDRAWITEMSTRUCT lp);       // 交互色＋選択ラベンダー
    virtual void MeasureItem(LPMEASUREITEMSTRUCT lp); // 行高。DPI 連動

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
//
// オーナードロー。無効行は論理インデックスから除外（グループ見出し）。
// GetCurSel は論理、GetCurSelPhysical は基底。閉じた欄は PaintClient。
// ガラス親では不透明。ドロップリストは CtlColor + DrawItem。
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
    int SetCurSelPhysical(int n)
    {
        const int r = CComboBox::SetCurSel(n);
        if (m_hWnd) ::PostMessage(m_hWnd, CCC_WM_POST_OPAQUE_PAINT, 0, 0);
        return r;
    }

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

    void PaintClient(CDC& dc); // 閉じた欄。ドロップリスト本体は DrawItem
};

// ============================================================================
// カスタムリストコントロール
// CCustomListCtrl
//
// CListCtrlA 派生。NM_CUSTOMDRAW で交互色・選択・ホバー♡。
// ガラス親では PaintOpaqueClient（最終行より下も交互色で α=255）。
// WS_EX_ACCEPTFILES 時は OnDropFiles が親へ転送（リストが親を覆うため）。
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

    virtual BOOL PreTranslateMessage(MSG* pMsg);

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
    // 回転♡: リスト全体ではなく♡の矩形だけ再描画してなめらかに回す
    CRect m_heartRcSel;    // 選択行の♡
    CRect m_heartRcHot;    // ホバー行の♡

    // ホバー状態のアイテムを更新し、必要に応じて再描画します
    void UpdateHotItem(int n);
    void UpdateHotItemFromCursor();

    // 表示されているアイテム領域を再描画します
    void RedrawVisibleItems();
    void PaintOpaqueClient(CDC& dc);
    void ScheduleOpaqueRepaint();
    // 可視最終行より下(とプレペイント時は行下地)を交互色・不透明で塗る
    void FillEmptyBelowVisible(HDC hdc, BOOL belowItemsOnly = TRUE);
};

// ============================================================================
// カスタムツリーコントロール (KotoriClient CCustomTreeCtrl 移植)
// CCustomTreeCtrl — リスト同様にアクリル下では不透明バッファ描画
//
// 標準 TreeView の NM_CUSTOMDRAW を横取りし、行全体を交互色＋選択色で塗る。
// ガラス親の上では OnEraseBkgnd=FALSE、PaintOpaqueClient で α=255 にする。
// ドラッグ開始は親へ TVN_BEGINDRAG 相当を自前通知（フル行ヒットと整合させる）。
// ============================================================================
#ifndef TVS_EX_FULLROWSELECT
#define TVS_EX_FULLROWSELECT 0x00000020
#endif

class CCustomTreeCtrl : public CTreeCtrl
{
	DECLARE_DYNAMIC(CCustomTreeCtrl)
public:
	CCustomTreeCtrl();
	virtual ~CCustomTreeCtrl();
	void EnableAutoDelete(BOOL bEnable = TRUE) { m_bAutoDelete = bEnable; }
	BOOL m_bAutoDelete;

	COLORREF SetBkColor(COLORREF clr); // 内部 m_clrBk。TreeView 本体色も合わせる
	COLORREF GetBkColor() const { return m_clrBk; }
	HTREEITEM HitTest(CPoint pt, UINT* pFlags = NULL); // アイコン以外の行クリックも項目扱い

	void SetAeroMode(BOOL b)
	{
		m_bAeroMode = b;
		if (GetSafeHwnd()) Invalidate();
	}
	void PaintOpaqueIntoBuffer(HDC hdcBuf); // OpaqueFixer の BufferedPaint 面へ直描き
	void ScheduleOpaqueRepaint();           // スクロール直後のチラつき回避（遅延 WM）

protected:
	BOOL m_bAeroMode;
	virtual void PreSubclassWindow();
	virtual void PostNcDestroy();
	afx_msg void  OnPaint();
	afx_msg BOOL  OnEraseBkgnd(CDC* pDC);
	afx_msg void  OnCustomDraw(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void  OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void  OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void  OnMouseLeave();
	afx_msg void  OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	afx_msg BOOL  OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void  OnWindowPosChanged(WINDOWPOS* lpwndpos);
	afx_msg void  OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
	afx_msg LRESULT OnPostOpaquePaint(WPARAM, LPARAM);

	DECLARE_MESSAGE_MAP()

private:
	int  GetItemLevel(HTREEITEM hItem) const;      // ルート=0。インデント描画用
	void InvalidateItemRow(HTREEITEM hItem);       // フル行矩形だけ Invalidate
	HTREEITEM HitTestRowAtPoint(CPoint pt, UINT* pFlags); // アイコン外の行ヒット
	void NotifySelChangedByMouse(HTREEITEM hNew, HTREEITEM hOld); // 親へ TVN_SELCHANGED
	void NotifyBeginDrag(HTREEITEM hItem, CPoint pt);             // 親へ TVN_BEGINDRAG 相当
	void PaintOpaqueClient(CDC& dc);               // α=255 でクライアント全面

	HTREEITEM m_hHotItem;    // ホバー行。ハート／ハイライト用。NULL=なし
	int       m_nItemDrawIndex; // CustomDraw の交互色インデックス
	COLORREF  m_clrBk;       // SetBkColor。TreeView 本体色と同期
	CBrush    m_brBackground;
};

// ============================================================================
// カスタムタブコントロール
// CCustomTabCtrl
//
// 標準 Tab の中身を自前描画（等幅スロット、選択パネル、ホバー）。
// TCS_VERTICAL / TCS_RIGHT も IsVertical / IsRightSide で分岐する。
// 選択タブは Soft 立体の軽い揺れタイマー (kTabSoftTimerId)。
// listing4 準拠オーナードロー。ガラス親では不透明バッファ。
// ============================================================================
class CCustomTabCtrl : public CTabCtrl
{
	DECLARE_DYNAMIC(CCustomTabCtrl)
public:
	CCustomTabCtrl();
	virtual ~CCustomTabCtrl();
	void EnableAutoDelete(BOOL bEnable = TRUE) { m_bAutoDelete = bEnable; }
	BOOL m_bAutoDelete;

	BOOL IsVertical() const;   // TCS_VERTICAL
	BOOL IsRightSide() const;  // TCS_RIGHT（縦タブの右側）
	void RebuildFonts();       // DPI 変更後。選択タブは少し大きい
	void LayoutEqualTabs(int nSlots = 3); // 等幅スロット。既定 3 は EQ 等

	void SetAeroMode(BOOL b)
	{
		m_bAeroMode = b;
		if (GetSafeHwnd()) Invalidate(FALSE);
	}
	void PaintOpaqueIntoBuffer(HDC hdcBuf); // OpaqueFixer 面へ直描き
	void ScheduleOpaqueRepaint();           // 遅延 WM（スクロール直後のチラつき回避）

protected:
	BOOL m_bAeroMode;
	virtual void PreSubclassWindow();
	virtual void PostNcDestroy();
	afx_msg void  OnPaint();
	afx_msg BOOL  OnEraseBkgnd(CDC* pDC);
	afx_msg void  OnSize(UINT nType, int cx, int cy);
	afx_msg void  OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void  OnMouseLeave();
	afx_msg void  OnWindowPosChanged(WINDOWPOS* lpwndpos);
	afx_msg void  OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
	afx_msg LRESULT OnPostOpaquePaint(WPARAM, LPARAM);
	afx_msg BOOL  OnSelChange(NMHDR* pNMHDR, LRESULT* pResult);

	DECLARE_MESSAGE_MAP()

private:
	void PaintOpaqueClient(CDC& dc); // α=255 クライアント全面
	void DrawToDC(CDC* pDC, const CRect& rcClient, BOOL bAeroChroma); // タブ＋ページパネル
	void DrawTabItem(CDC* pDC, int nItem, CRect rc, BOOL bSelected, BOOL bHot);
	void DrawPagePanel(CDC* pDC, const CRect& rcClient); // 本文側の薄いパネル
	void InvalidateTabItem(int nItem); // 1 タブ分だけ

	CBrush m_brBackground;
	CFont  m_fontTab;
	CFont  m_fontTabSel;  // 選択タブ用（やや大きい）
	int    m_nHotItem;    // ホバー。-1=なし
	BOOL   m_bTracking;   // TrackMouseEvent 済み
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

    // CButtonST 互換: アイコンボタン(プレイリスト上下移動など)
    DWORD SetIcon(int nIconIn, int nIconOut = 0);
    DWORD SetIcon(HICON hIconIn, HICON hIconOut = NULL);
    void SetFlat(BOOL bFlat);
    // TRUE=アクリル下地をクロマ透過（Lib/Hist レール等のラベル風ボタン向け）
    void SetAeroMode(BOOL b);

    void PaintClient(CDC& dc, const CRect& r);
    void PaintOpaqueClient(CDC& dc); // アクリル下の OpaqueFixer 用（α=255）
    void RepaintClient();
    void EnsureAnimTimer();   // 押下状態変化後に流れるアニメを再開

protected:
    virtual void PreSubclassWindow();
    virtual void PostNcDestroy();

    // メッセージハンドラ
    afx_msg HBRUSH CtlColor(CDC*, UINT);
    afx_msg void OnPaint();
    afx_msg LRESULT OnPrintClient(WPARAM, LPARAM);
    afx_msg LRESULT OnBmSetState(WPARAM, LPARAM);
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
    void UpdateAnimTimer(); // ホバー/フォーカス/残点に応じてタイマーを開始/停止
    void SparkleTick(BOOL bSpawn); // 点を進め、必要なら発生。全滅で FALSE 相当は N==0

    enum { kBtnSparkleMax = 48 };
    int m_nSparkleN;                 // 生存中の流れる点の数
    int m_sparklePos[kBtnSparkleMax]; // 各点の X 位置（左端からの px）
    int m_nSparkleSpawnAcc;          // 等間隔発生用アキュムレータ

    // プロパティ保持用メンバ変数
    COLORREF m_clrGradStart, m_clrGradEnd;
    int m_nGradDirection;
    BOOL m_bGradEnable;

    COLORREF m_clrShadow;
    int m_nShadowDirection, m_nShadowDistance, m_nShadowBlur;
    BOOL m_bShadowEnable;

    HICON m_hIconIn;       // 通常アイコン(所有)
    HICON m_hIconOut;      // ホバー用(所有・無くても可)
    BOOL m_bFlat;          // TRUE=薄い枠・装飾控えめ(アイコンボタン向け)
    BOOL m_bAeroMode;      // TRUE=クロマ透過（既定ボタンは不透明のまま）
    BOOL m_bIconOwnedIn;   // DestroyIcon が必要か
    BOOL m_bIconOwnedOut;
    BOOL m_bAutoGlyphDone; // コントロールIDから一度だけグリフを載せた
    void EnsureAutoGlyph();
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

    // 描画モードの設定・取得（0=楔＋音符、1=紫ダイヤ、2=緑ダイヤ）
    void SetMode(int m);
    int GetMode() const { return m_nMode; }

    // スライダーの位置設定
    void SetPos(int nPos, BOOL bRedraw = TRUE);

    // アクリルモードの設定
    void SetAeroMode(BOOL b);
    void PaintOpaqueIntoBuffer(HDC hdcBuf);

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

    void PaintClient(CDC& dc);       // トラック＋つまみ＋スパークル
    void PaintOpaqueClient(CDC& dc); // ガラス下 α=255。クロマは PaintClient 側

private:
    UINT m_nShimmer; // 流れるシマー用カウンタ（ホバーキラキラ／DrawSoftJkThumb）
    BOOL m_bHover;   // マウスがスライダー上にあるか
    enum { kSliderSparkleMax = 48 };
    int m_nSparkleN;                    // 生存中の流れる点の数
    int m_sparklePos[kSliderSparkleMax]; // 各点の軌跡上位置（px）
    int m_nSparkleSpawnAcc;             // 等間隔発生用アキュムレータ
    void SparkleTick(BOOL bSpawn);      // 点を進め、ホバー中なら発生
    int SparkleSpan(BOOL* pbVert);      // 現在の軌跡長（つまみまで）
    CBitmap m_memBackstore; // 毎描画 CreateCompatibleBitmap を避ける
    int m_backstoreW;
    int m_backstoreH;
#if CCUSTOM_AERO_SUPPORT
    CCC_ChromaBlitCache m_chromaCache; // 共有 s_nfCache のサイズ thrash 回避
#endif

    // 描画モードごとの実際の描画処理
    void DrawSlider(CDC* pDC); // TBS_VERT は各 Mode 内で分岐
    void DrawMode0(CDC* pDC, const CRect& r, int mn, int mx, int pos); // 楔形バー＋音符つまみ
    void DrawMode1(CDC* pDC, const CRect& r, int mn, int mx, int pos); // 紫グラデ線＋ダイヤ
    void DrawMode2(CDC* pDC, const CRect& r, int mn, int mx, int pos); // 緑グラデ線＋ダイヤ（mode1 と同型）
};

// ============================================================================
// カスタム範囲スライダーコントロール
// CCustomRangeSliderCtrl
// 再生位置 + ループ選択(loop1/2) + 任意の A-B つまみ。
// （アクリルモードの挙動は CCustomSliderCtrl と同様です）
// HitTest: 0=なし 1=loop最小 2=loop最大 3=再生位置/シーク 4=A 5=B
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

    // 選択された範囲（loop1/2 つまみの位置）の設定・取得
    void SetSelection(int mn, int mx);
    void GetSelection(int& mn, int& mx) const;

    // A-B 区間（-1=未設定）。loop 選択とは別変数。Aのみでつまみ表示、Bで区間塗り。
    void SetAB(int a, int b);
    void GetAB(int& a, int& b) const;

    // TRUE=loop1/2 つまみをドラッグ不可（既定）。A-B は常に可。
    void SetSelectionLocked(BOOL bLocked);
    BOOL IsSelectionLocked() const { return m_bSelLocked; }

    // 現在の位置（ドラッグ中の仮想位置ではなく確定位置）の設定・取得
    void SetPos(int nPos);
    int GetPos() const;

    // 再生追従用: 範囲・選択・位置・A-B を一括更新し、見た目(px)が変わったときだけ Invalidate
    // abA/abB に 0x80000000 を渡すとその項目は触らない（Douga 等の既存呼び出し互換）
    void SetPlaybackMirror(int nPos, int selMin, int selMax, int rangeMin, int rangeMax,
        int abA = (int)0x80000000, int abB = (int)0x80000000);

    // 範囲(最小/最大)の取得(メディアプレイヤー画面のシーク表示用)
    int GetMinValue() const { return m_nMin; }
    int GetMaxValue() const { return m_nMax; }

    // ドラッグ対象(HitTest 値)。ドラッグ中/直後の親通知判別に使う。
    int GetDragTarget() const { return m_nDragTarget; }
    BOOL IsDragging() const { return m_bDragging; }

    // 波形オーバービュー(0..1ピーク)。count<=0 で消去。内部にコピー(最大 kWavePeaksMax)。
    enum { kWavePeaksMax = 1024, kCueMax = 8 };
    void SetWavePeaks(const float* peaks, int count);
    void ClearWavePeaks();
    int GetWavePeakCount() const { return m_wavePeakCount; }
    // 再生位置の振幅をビンに最大合成（リアルタイム波形。full概観が来たら SetWavePeaks で置換）
    void AccumulateWaveAtPos(int pos, float amp, int bins = 512);

    // キューマーカー(フレーム位置)。count<=0 で消去。クリックで GetCueClick>=0。
    void SetCues(const int* frames, int count);
    void ClearCues();
    int GetCueClick() const { return m_nCueClick; }
    void ClearCueClick() { m_nCueClick = -1; }

    // LRC 時刻マーカー(フレーム)。クリックで GetLrcClick>=0。
    enum { kLrcMarkMax = 64 };
    void SetLrcMarkers(const int* frames, int count);
    void ClearLrcMarkers();
    int GetLrcClick() const { return m_nLrcClick; }
    void ClearLrcClick() { m_nLrcClick = -1; }
    int GetLrcFrame(int idx) const;

    // ホバー拡大波形（peaks があるとき）
    void SetHoverZoom(BOOL on);

    // スペアナ・リボン(最大64本)。シーク中央上に細いバー。
    enum { kRibbonMax = 64 };
    void SetMeterRibbon(const float* bins, int n);

    // 書き出しクロスフェード帯プレビュー(ms)。0=非表示。timeBaseHz でフレーム換算。
    void SetXfadePreviewMs(int ms);
    void SetTimeBaseHz(int hz);

    // 拍グリッド。bpm<=0 は 120 扱い。beatsPerBar は小節頭アクセント（2..16、既定4）。
    void SetBeatGrid(float bpm, BOOL enabled);
    void SetBeatGrid(float bpm, BOOL enabled, int offsetMs);
    void SetBeatGrid(float bpm, BOOL enabled, int offsetMs, int beatsPerBar);
    int GetBeatGridOffsetMs() const { return m_beatOffsetMs; }

    // アクリルモードの設定
    void SetAeroMode(BOOL b);
    void PaintOpaqueIntoBuffer(HDC hdcBuf);

    // ホバー時刻チップ用(親 PreTranslate から呼ぶ)
    virtual BOOL PreTranslateMessage(MSG* pMsg);

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
    afx_msg void OnMouseLeave();
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
    afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
    afx_msg BOOL OnTtnNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);

    DECLARE_MESSAGE_MAP()

    void PaintClient(CDC& dc);       // 波形・loop・A-B・シークを重ねる
    void PaintOpaqueClient(CDC& dc);

private:
    void DrawRangeSlider(CDC* pDC);

    // 値とピクセル座標の相互変換、マウスクリック時のヒットテスト
    int ValueToPixel(int v) const;
    int PixelToValue(int x) const;
    int HitTest(CPoint p) const;
    void EnsureHoverTip();
    void UpdateHoverTip(CPoint p);

    // 状態保持用メンバ変数
    int m_nMin, m_nMax;         // 全体の最小値・最大値
    int m_nSelMin, m_nSelMax;   // ループ選択(loop1 / loop1+loop2)
    int m_nAbA, m_nAbB;         // A-B（-1=未設定）
    BOOL m_bSelLocked;          // loop つまみロック
    int m_nDragTarget;          // 現在ドラッグしている対象（HitTestの戻り値に対応）
    int m_nLogicalPos;          // 確定された論理位置
    BOOL m_bDragging;           // ドラッグ中かどうか
    int m_nVisualPos;           // ドラッグ中の見た目上の位置
    float m_wavePeaks[kWavePeaksMax];
    int m_wavePeakCount;
    int m_cueFrames[kCueMax];
    int m_cueCount;
    int m_nCueClick;            // クリックされたキュー index。-1=なし
    int m_lrcFrames[kLrcMarkMax];
    int m_lrcCount;
    int m_nLrcClick;            // クリックされた LRC index。-1=なし
    BOOL m_bHoverZoom;
    int m_hoverZoomX;           // クライアント X（ホバー拡大中心）
    float m_ribbon[kRibbonMax];
    int m_ribbonN;
    int m_xfadePreviewMs;
    int m_timeBaseHz;
    float m_beatBpm;
    BOOL m_bBeatGrid;
    int m_beatOffsetMs;
    int m_beatMeter; // 小節内拍数（アクセント用）
    BOOL m_bHoverTracking;
    CToolTipCtrl m_hoverTip;
    CString m_hoverTipText;
    CBitmap m_memBackstore;     // 毎描画 CreateCompatibleBitmap を避ける
    int m_backstoreW;
    int m_backstoreH;
#if CCUSTOM_AERO_SUPPORT
    CCC_ChromaBlitCache m_chromaCache;
#endif
};

// ============================================================================
// カスタムチェックボックスコントロール
// CCustomCheckBox
//
// 自前レ点。ON 時 8 フレームのぷるんバウンス (m_nBounce)。
// アクリル時は箱以外をクロマ。ポップアップのレ点バウンスと同じカウンタ規約。
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
// 縦レベルメータ (録音/キャプチャ/マイク検出)
// CCustomLevelMeter
//
// 0..1000。緑→黄→赤の縦バー。ピークホールドは持たない（親が SetLevel する）。
// バー寸法は旧 WS_BORDER+Deflate(2,2) と同じ。余白と枠だけ不透明に塗る。
// 変化なしなら Invalidate しない。
// ============================================================================
class CCustomLevelMeter : public CStatic
{
	DECLARE_DYNAMIC(CCustomLevelMeter)
public:
	CCustomLevelMeter();
	virtual ~CCustomLevelMeter();
	void SetLevel(int n); // 0..1000
	int GetLevel() const { return m_level; }
	void SetAeroMode(BOOL b) { m_bAeroMode = b; if (GetSafeHwnd()) Invalidate(FALSE); }
	void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
	void PaintOpaqueIntoBuffer(HDC hdcBuf);
	BOOL m_bAutoDelete;

protected:
	virtual void PreSubclassWindow();
	virtual void PostNcDestroy();
	afx_msg void OnPaint();
	afx_msg void OnNcPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

	void PaintClient(CDC& dc); // 余白はダイアログ色、内側がレベルバー

	int m_level;       // 0..1000。SetLevel がクランプ
	BOOL m_bAeroMode;
};

// ============================================================================
// カスタムプログレスバー (オーナー描画・アクリル透過 / 淫女モード対応)
// CCustomProgressCtrl
// ============================================================================
// ぼかしダイアログ上では背景を透過(トラック/塗り/％は不透明)、通常時は
// COLOR_DIALOG_BG で塗りつぶす。淫女モード時は CCC_DrawInwoman を重ねる。
class CCustomProgressCtrl : public CWnd
{
	DECLARE_DYNAMIC(CCustomProgressCtrl)
public:
	CCustomProgressCtrl();
	virtual ~CCustomProgressCtrl();
	void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
	BOOL m_bAutoDelete;

	BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
	void SetRange(int nLower, int nUpper);
	void GetRange(int& nLower, int& nUpper) const;
	int  SetPos(int nPos);
	int  GetPos() const { return m_nPos; }
	void SetShowPercent(BOOL b) { m_bShowPercent = b; if (GetSafeHwnd()) Invalidate(FALSE); }
	void SetColors(COLORREF track, COLORREF fillStart, COLORREF fillEnd);
	void SetAeroMode(BOOL b);
	void PaintOpaqueIntoBuffer(HDC hdcBuf);

protected:
	virtual void PostNcDestroy();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	void PaintClient(CDC& dc);                 // クライアント全面
	void PaintClient(CDC& dc, const CRect& r); // 指定矩形（OpaqueFixer 用）
	void DrawProgressLayer(CDC& dc, const CRect& r, BOOL bAeroTrans); // トラック＋塗り＋％
	void PaintOpaqueClient(CDC& dc);           // α=255

	int m_nMin, m_nMax, m_nPos; // 範囲と現在値。SetPos はクランプして返す
	BOOL m_bShowPercent;        // 中央に % 文字
	BOOL m_bAeroMode;
	COLORREF m_clrTrack, m_clrFill0, m_clrFill1; // 空トラック、塗り始端/終端
	CBrush m_brBackground;
	CFont m_fontPct;
#if CCUSTOM_AERO_SUPPORT
	CCC_ChromaBlitCache m_chromaCache;
#endif
};

// ============================================================================
// システム性能パネル (メモリ数値 + CPU 全体/コア別グラフ・アクリル/淫女対応)
// CCustomSysPerfCtrl
//
// タイマ ~1Hz で GetSystemTimes。初回差分は捨てる (m_bHaveTimes)。
// SMBIOS は起動時一度。右クリックで表示切替・コピー・一時停止。
// ============================================================================
class CCustomSysPerfCtrl : public CWnd
{
	DECLARE_DYNAMIC(CCustomSysPerfCtrl)
public:
	enum {
		VIEW_ALL = 0,        // メモリ＋全体CPU＋コアグリッド
		VIEW_MEM,            // メモリ数値のみ
		VIEW_CPU_OVERALL,    // 全体 CPU スパークのみ
		VIEW_CPU_GRID,       // コア別グリッドのみ
		VIEW_CPU_BOTH        // 全体＋コア
	};
	static const int kMaxCores = 64; // NtQuerySystemInformation 上限に合わせる
	static const int kHistLen = 60;  // スパーク履歴（約 1 分 @1Hz）

	CCustomSysPerfCtrl();
	virtual ~CCustomSysPerfCtrl();
	void EnableAutoDelete(BOOL b = TRUE) { m_bAutoDelete = b; }
	BOOL m_bAutoDelete;

	BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);
	void SetAeroMode(BOOL b);
	void PaintOpaqueIntoBuffer(HDC hdcBuf);
	void SetViewMode(int mode);
	int  GetViewMode() const { return m_viewMode; }

protected:
	virtual void PostNcDestroy();
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnDestroy();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg LRESULT OnPrintClient(WPARAM wParam, LPARAM lParam);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg BOOL OnToolTipNotify(UINT id, NMHDR* pNMHDR, LRESULT* pResult);
	DECLARE_MESSAGE_MAP()

private:
	void PaintClient(CDC& dc);                 // クライアント全面
	void PaintClient(CDC& dc, const CRect& r); // OpaqueFixer 用の指定矩形
	void DrawPerfLayer(CDC& dc, const CRect& r, BOOL bAeroTrans); // 3 区画の配置＋描画
	void PaintOpaqueClient(CDC& dc);
	void SampleOnce();           // GetSystemTimes + メモリ。タイマから ~1Hz
	void SampleSmbiosOnce();     // メモリスロット/速度。起動時一度だけ
	void LayoutRects(const CRect& r, CRect& rcMem, CRect& rcOverall, CRect& rcGrid);
	void DrawMemory(CDC& dc, const CRect& rc, BOOL bAeroTrans);
	void DrawOverallCpu(CDC& dc, const CRect& rc, BOOL bAeroTrans);
	void DrawCoreGrid(CDC& dc, const CRect& rc, BOOL bAeroTrans);
	void DrawSpark(CDC& dc, const CRect& rc, const BYTE* hist, int histCount, BOOL bAeroTrans);
	void FormatBytesGB(ULONGLONG bytes, CString& out);
	void CopyStatsToClipboard(); // 右クリック「コピー」
	void ShowCtxMenu(CPoint screenPt);
	UINT Dpi() const;
	int  S(int v) const; // 96dpi 基準 px → 実 DPI

	BOOL m_bAeroMode;
	BOOL m_bPaused;      // 右クリック一時停止。SampleOnce を止める
	BOOL m_bSmbiosDone;  // SampleSmbiosOnce 済み
	int  m_viewMode;
	int  m_gridCols; // 0=auto（コア数から列を決める）
	int  m_coreCount;
	int  m_histCount;    // 埋まった履歴本数（立ち上がり）
	int  m_histPos;      // リング書き込み位置
	BYTE m_overallHist[kHistLen];
	BYTE m_coreHist[kMaxCores][kHistLen];
	BYTE m_overallNow;   // 直近サンプル 0..100
	BYTE m_coreNow[kMaxCores];

	FILETIME m_ftIdlePrev; // GetSystemTimes 差分用
	FILETIME m_ftKerPrev;
	FILETIME m_ftUsrPrev;
	BOOL m_bHaveTimes;     // 初回は差分が取れない
	ULONGLONG m_coreIdlePrev[kMaxCores];
	ULONGLONG m_coreKerPrev[kMaxCores];
	ULONGLONG m_coreUsrPrev[kMaxCores];
	BOOL m_bHaveCore;

	ULONGLONG m_memInUse;
	ULONGLONG m_memCompressed;
	ULONGLONG m_memAvail;
	ULONGLONG m_memCommit;
	ULONGLONG m_memCommitLimit;
	ULONGLONG m_memCached;
	ULONGLONG m_memPaged;
	ULONGLONG m_memNonPaged;
	ULONGLONG m_memHwReserved;
	UINT m_memSpeedMTs;
	UINT m_memSlotsUsed;
	UINT m_memSlotsTotal;
	UINT m_memFormFactor; // SMBIOS form factor（DIMM 等）
	BOOL m_bHaveCompressed;

	CString m_tipText;
	CToolTipCtrl m_tip;
	CFont m_fontLabel;
	CFont m_fontValue;
#if CCUSTOM_AERO_SUPPORT
	CCC_ChromaBlitCache m_chromaCache;
#endif
};

// ============================================================================
// カスタムグループボックスコントロール
// CCustomGroupBox
//
// 枠＋キャプションのみ。兄弟の下に回り（WS_CLIPSIBLINGS）、兄弟領域へ描かない。
// Soft3D 常時タイマーはピアノ等と競合するため Kill する。アクリル時は枠をクロマ。
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
    afx_msg void OnTimer(UINT_PTR nIDEvent);

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
            else if (auto* p = dynamic_cast<CCustomTreeCtrl*>(_pw))        p->SetAeroMode(FALSE); \
            else if (auto* p = dynamic_cast<CCustomTabCtrl*>(_pw))         p->SetAeroMode(FALSE); \
            else if (auto* p = dynamic_cast<CCustomCheckBox*>(_pw))        p->SetAeroMode(_bA); \
            else if (auto* p = dynamic_cast<CCustomProgressCtrl*>(_pw))    p->SetAeroMode(_bA); \
            else if (auto* p = dynamic_cast<CCustomLevelMeter*>(_pw))      p->SetAeroMode(_bA); \
            else if (auto* p = dynamic_cast<CCustomSysPerfCtrl*>(_pw))     p->SetAeroMode(_bA); \
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

    CBrush m_brDialog;   // ダイアログの背景塗りつぶし用ブラシ（aero=0）
    BOOL m_bAeroEnabled; // EnableAero 後。子伝播と OnPaint 隙間塗りの判定

    // Windows 11 のアクリル背景時に使用する透明ブラシ (NULL_BRUSH 相当)
    CBrush m_brNull;

private:
    // ダイアログ上の標準コントロールをカスタムコントロールに自動置換(サブクラス化)します
    void SubclassChildControls();
};

// ============================================================================
// ぼかし適用済みカスタムダイアログの基底クラス (CDialog派生)
// CCustomBlurDialogBase
//
// EnableAero + OpaqueFixer 一覧 + カスタムキャプション（min/max/close/pin/help）。
// ApplyDwmBlurCore を二重に走らせない（FRAMECHANGED でフリーズする）。
// EnableMainWindowLock はメイン位置へ追従するチェック。
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
    // (カスタムキャプション有効時はキャプション帯へ配置。オーバーレイも子チェックへ切替)
    void EnableMainWindowLock(int* pSavedLockFlag, BOOL bOverlayPaint = FALSE);
    int GetCustomCaptionHeight() const { return CCC_GetCustomCaptionHeight(m_hWnd); }
    virtual BOOL PreTranslateMessage(MSG* pMsg);

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
    afx_msg LRESULT OnInstallCustomCaption(WPARAM wParam, LPARAM lParam);
    afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
    afx_msg BOOL OnNcActivate(BOOL bActive);
    afx_msg LRESULT OnNcThemeCaptionPaint(WPARAM wParam, LPARAM lParam);
    afx_msg void OnMainLockClicked();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
    afx_msg void OnCapClose();
    afx_msg void OnCapMin();
    afx_msg void OnCapMax();
    afx_msg void OnCapSettings();
    afx_msg void OnCapPin();
    afx_msg void OnCapOfflineHelp();
    afx_msg BOOL OnTtnNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);

    DECLARE_MESSAGE_MAP()

private:
    void ApplyDwmBlurCore(BOOL bForce); // bForce でも二重 FRAMECHANGED は避ける
    BOOL m_bBlurApplied;                // 既適用。RefreshAeroMode がクリアする
    BOOL m_bInApplyBlur = FALSE;        // 再入防止（組成変更からの再入）
    CTypedPtrList<CPtrList, CCustomOpaqueFixer*> m_opaqueFixers; // 子 HWND ごと。OnDestroy で解除
    int* m_pMainLockSave = nullptr;     // savedata の追従フラグ。EnableMainWindowLock が渡す
    CToolTipCtrl m_capTip;              // キャプションボタン（min/max/close/pin/help）
};

// ============================================================================
// カスタムダイアログクラス (CDialogEx 派生・非 Blur)
// CCustomDialogEx
//
// CCustomDialog と同型だが、もともと CDialogEx だった窓向け。
// ぼかし＋キャプションは CCustomBlurDialogExBase。
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

    CBrush m_brDialog;   // ダイアログの背景塗りつぶし用ブラシ（aero=0）
    BOOL m_bAeroEnabled; // EnableAero 後。子伝播と OnPaint 隙間塗りの判定

    // Windows 11 のアクリル背景時に使用する透明ブラシ (NULL_BRUSH 相当)
    CBrush m_brNull;

private:
    // ダイアログ上の標準コントロールをカスタムコントロールに自動置換(サブクラス化)します
    void SubclassChildControls();
};

// ============================================================================
// ぼかし適用済みカスタムダイアログの基底クラス (CDialogEx派生)
// CCustomBlurDialogExBase
//
// CCustomBlurDialogBase と同型。もともと CDialogEx だった窓向け。
// ApplyDwmBlurCore を二重に走らせない（FRAMECHANGED でフリーズする）。
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
    // (カスタムキャプション有効時はキャプション帯へ配置。オーバーレイも子チェックへ切替)
    void EnableMainWindowLock(int* pSavedLockFlag, BOOL bOverlayPaint = FALSE);
    int GetCustomCaptionHeight() const { return CCC_GetCustomCaptionHeight(m_hWnd); }
    virtual BOOL PreTranslateMessage(MSG* pMsg);

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
    afx_msg LRESULT OnInstallCustomCaption(WPARAM wParam, LPARAM lParam);
    afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
    afx_msg BOOL OnNcActivate(BOOL bActive);
    afx_msg LRESULT OnNcThemeCaptionPaint(WPARAM wParam, LPARAM lParam);
    afx_msg void OnMainLockClicked();
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMoving(UINT fwSide, LPRECT pRect);
    afx_msg void OnCapClose();
    afx_msg void OnCapMin();
    afx_msg void OnCapMax();
    afx_msg void OnCapSettings();
    afx_msg void OnCapPin();
    afx_msg void OnCapOfflineHelp();
    afx_msg BOOL OnTtnNeedText(UINT id, NMHDR* pNMHDR, LRESULT* pResult);

    DECLARE_MESSAGE_MAP()

private:
    void ApplyDwmBlurCore(BOOL bForce); // bForce でも二重 FRAMECHANGED は避ける
    BOOL m_bBlurApplied;                // 既適用。RefreshAeroMode がクリアする
    BOOL m_bInApplyBlur = FALSE;        // 再入防止（組成変更からの再入）
    CTypedPtrList<CPtrList, CCustomOpaqueFixer*> m_opaqueFixers; // 子 HWND ごと。OnDestroy で解除
    int* m_pMainLockSave = nullptr;     // savedata の追従フラグ。EnableMainWindowLock が渡す
    CToolTipCtrl m_capTip;              // キャプションボタン（min/max/close/pin/help）
};

#include "CCustomPopupMenu.h"
