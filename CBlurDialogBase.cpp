#include "stdafx.h"
#include "CBlurDialogBase.h"

// ---------------------------------------------------------
// CControlFixer 実装 (ピクセル操作＆スクロール対策版)
// ---------------------------------------------------------

CControlFixer::CControlFixer()
	: m_hWnd(NULL)
	, m_bPrinting(FALSE)
{
}

CControlFixer::~CControlFixer()
{
	Uninstall();
}

BOOL CControlFixer::Install(HWND hWnd)
{
	if (m_hWnd != NULL) return FALSE;
	if (!::IsWindow(hWnd)) return FALSE;

	m_hWnd = hWnd;
	return ::SetWindowSubclass(hWnd, SubclassProc, (UINT_PTR)this, (DWORD_PTR)this);
}

void CControlFixer::Uninstall()
{
	if (m_hWnd && ::IsWindow(m_hWnd))
	{
		::RemoveWindowSubclass(m_hWnd, SubclassProc, (UINT_PTR)this);
	}
	m_hWnd = NULL;
}

LRESULT CALLBACK CControlFixer::SubclassProc(
	HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
	UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	CControlFixer* pThis = (CControlFixer*)dwRefData;

	switch (uMsg)
	{
	case WM_ERASEBKGND:
		// WM_PRINT中のみ背景消去を許可する（コントロールに描画させるため）
		if (pThis->m_bPrinting)
			return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
		// 通常描画時はチラつき防止＆透明維持のため無視
		return TRUE;

	case WM_VSCROLL:
	case WM_HSCROLL:
	case WM_MOUSEWHEEL:
	{
		// ★リストビューの黒ノイズ対策
		// デフォルトのスクロール処理を行う
		LRESULT lRes = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);

		// その後、ウィンドウ全体を強制的に無効化して全描画させる
		// これにより「古いピクセルの引きずり」による黒ノイズを消す
		::InvalidateRect(hWnd, NULL, TRUE);
		return lRes;
	}

	case WM_PAINT:
	case WM_PRINTCLIENT:
	{
		PAINTSTRUCT ps;
		HDC hDC = (uMsg == WM_PAINT) ? ::BeginPaint(hWnd, &ps) : (HDC)wParam;

		if (hDC)
		{
			pThis->OnPaint(hWnd, hDC);
		}

		if (uMsg == WM_PAINT)
		{
			::EndPaint(hWnd, &ps);
		}
		return 0;
	}

	case WM_DESTROY:
		::RemoveWindowSubclass(hWnd, SubclassProc, uIdSubclass);
		pThis->m_hWnd = NULL;
		break;
	}

	return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void CControlFixer::OnPaint(HWND hWnd, HDC hDestDC)
{
	RECT rect;
	::GetClientRect(hWnd, &rect);
	int width = rect.right - rect.left;
	int height = rect.bottom - rect.top;

	if (width <= 0 || height <= 0) return;

	BP_PAINTPARAMS params = { sizeof(BP_PAINTPARAMS) };
	params.dwFlags = BPPF_ERASE; // バッファをクリアして開始
	HDC hdcBuf = NULL;
	HPAINTBUFFER hBufferedPaint = ::BeginBufferedPaint(hDestDC, &rect, BPBF_TOPDOWNDIB, &params, &hdcBuf);

	if (hdcBuf && hBufferedPaint)
	{
		// 1. バッファを「真っ白」で塗りつぶす (キャンバスの用意)
		// アルファ値はGDI描画なので0になるが、RGBは(255,255,255)になる
		CBrush brushWhite(RGB(255, 255, 255));
		::FillRect(hdcBuf, &rect, (HBRUSH)brushWhite.GetSafeHandle());

		// 2. コントロールに描画させる
		// OnCtlColorで白ブラシを返しているので、コントロールは
		// 「白背景の上に黒文字」を描画するはず
		m_bPrinting = TRUE;
		::SendMessage(hWnd, WM_PRINT, (WPARAM)hdcBuf,
			PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND | PRF_NONCLIENT);
		m_bPrinting = FALSE;

		// 3. ピクセル走査：輝度判定による透過処理
		// 「白い部分は透明」「黒い部分は不透明」に書き換える
		RGBQUAD* pPixels = NULL;
		int rowLength = 0;

		if (SUCCEEDED(::GetBufferedPaintBits(hBufferedPaint, &pPixels, &rowLength)))
		{
			for (int y = 0; y < height; y++)
			{
				RGBQUAD* pRow = (RGBQUAD*)((BYTE*)pPixels + y * rowLength * sizeof(RGBQUAD));

				for (int x = 0; x < width; x++)
				{
					// ピクセルの色を取得
					BYTE r = pRow[x].rgbRed;
					BYTE g = pRow[x].rgbGreen;
					BYTE b = pRow[x].rgbBlue;

					// 判定ロジック：
					// 白(255,255,255)に近い色は「背景」とみなして透明にする。
					// しきい値を設けることで、完全な白じゃなくても(薄いグレー枠線など)消せる。
					// 文字(黒)は r,g,b が小さいので、ここは通らない。

					// 例: RGBすべてが250以上なら「白」とみなす
					if (r >= 250 && g >= 250 && b >= 250)
					{
						// 背景 -> 透明にする
						pRow[x].rgbReserved = 0;
					}
					else
					{
						// 文字や濃い色の枠線 -> 不透明にする
						pRow[x].rgbReserved = 255;
					}
				}
			}
		}
		else
		{
			// 万が一ビット取得に失敗した場合の保険
			// 少なくとも「白い箱に黒文字」として表示させる（見えないよりマシ）
			::BufferedPaintMakeOpaque(hBufferedPaint, &rect);
		}

		::EndBufferedPaint(hBufferedPaint, TRUE);
	}
	else
	{
		// バッファ確保失敗時のフォールバック
		::DefSubclassProc(hWnd, WM_PAINT, (WPARAM)hDestDC, 0);
	}
}

// ---------------------------------------------------------
// CBlurDialogBase 実装
// ---------------------------------------------------------

IMPLEMENT_DYNAMIC(CBlurDialogBase, CCustomBlurDialogBase)

BEGIN_MESSAGE_MAP(CBlurDialogBase, CDialog)
	ON_WM_CREATE()
	ON_WM_CTLCOLOR()
	ON_WM_ERASEBKGND()
	ON_WM_WINDOWPOSCHANGED()
	ON_WM_NCPAINT()
	ON_WM_PAINT()
	ON_WM_NCCALCSIZE()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CBlurDialogBase::CBlurDialogBase()
	: CDialog()
	, m_bBlurApplied(FALSE)
{
}

CBlurDialogBase::CBlurDialogBase(UINT nIDTemplate, CWnd* pParent)
	: CDialog(nIDTemplate, pParent)
	, m_bBlurApplied(FALSE)
{
}

CBlurDialogBase::~CBlurDialogBase()
{
	while (!m_fixerList.IsEmpty())
	{
		delete m_fixerList.RemoveHead();
	}
}

void CBlurDialogBase::DebugOutput(LPCTSTR format, ...)
{
	TCHAR buffer[1024];
	va_list args;
	va_start(args, format);
	_vstprintf_s(buffer, 1024, format, args);
	va_end(args);
	OutputDebugString(buffer);
}

void CBlurDialogBase::OnDestroy()
{
	POSITION pos = m_fixerList.GetHeadPosition();
	while (pos != NULL)
	{
		CControlFixer* pFixer = m_fixerList.GetNext(pos);
		if (pFixer)
		{
			pFixer->Uninstall();
			delete pFixer;
		}
	}
	m_fixerList.RemoveAll();

	CDialog::OnDestroy();
}

BOOL CBlurDialogBase::PreCreateWindow(CREATESTRUCT& cs)
{
	if (!CDialog::PreCreateWindow(cs))
		return FALSE;

	WNDCLASS wc;
	HINSTANCE hInst = AfxGetInstanceHandle();

	if (!(::GetClassInfo(hInst, cs.lpszClass, &wc)))
	{
		if (!::GetClassInfo(NULL, cs.lpszClass, &wc))
		{
			return TRUE;
		}
	}

	wc.hbrBackground = NULL;
	wc.lpszClassName = _T("BlurDialogClass");
	wc.hInstance = hInst;

	if (!AfxRegisterClass(&wc))
	{
		DebugOutput(_T("ウィンドウクラスの登録に失敗しました\n"));
		return FALSE;
	}

	cs.lpszClass = _T("BlurDialogClass");
	return TRUE;
}

int CBlurDialogBase::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDialog::OnCreate(lpCreateStruct) == -1)
		return -1;

	::SetClassLongPtr(m_hWnd, GCLP_HBRBACKGROUND, (LONG_PTR)NULL);
	ApplyDwmBlur();

	return 0;
}

BOOL CBlurDialogBase::OnInitDialog()
{
	CDialog::OnInitDialog();

	HBRUSH hBrush = (HBRUSH)::GetClassLongPtr(m_hWnd, GCLP_HBRBACKGROUND);
	if (hBrush != NULL)
	{
		::SetClassLongPtr(m_hWnd, GCLP_HBRBACKGROUND, (LONG_PTR)NULL);
	}

	if (!m_bBlurApplied)
	{
		ApplyDwmBlur();
	}

	FixListControlsBackground();

	// 不透明化フックの適用（再帰的に全コントロールへ）
	ApplyOpacityFixToAllChildren();

	ModifyStyle(0, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);

	SetWindowPos(NULL, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_DRAWFRAME);

	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);

	return TRUE;
}

void CBlurDialogBase::ApplyOpacityFixToAllChildren()
{
	COSVersion os;
	os.GetVersionString();

	if (os.in.dwMajorVersion < 10 || os.in.dwBuildNumber < 22000)
		return;

	// 再帰処理を開始
	RecursiveApplyFix(m_hWnd);
}

// ★追加：再帰的に子ウィンドウを探索してフックを適用する
void CBlurDialogBase::RecursiveApplyFix(HWND hWndParent)
{
	HWND hChild = ::GetWindow(hWndParent, GW_CHILD);

	while (hChild != NULL)
	{
		TCHAR className[256] = { 0 };
		::GetClassName(hChild, className, 255);
		CString strClassName(className);
		strClassName.MakeUpper();

		// フック対象判定
		BOOL bTarget = FALSE;

		// 主要コントロール
		if (strClassName.Find(_T("STATIC")) >= 0) bTarget = TRUE;
		else if (strClassName.Find(_T("BUTTON")) >= 0)
		{
			// ★グループボックスは除外する（中身が見えなくなるため）
			LONG_PTR style = ::GetWindowLongPtr(hChild, GWL_STYLE);
			if ((style & BS_TYPEMASK) != BS_GROUPBOX)
			{
				bTarget = TRUE;
			}
		}
		else if (strClassName.Find(_T("LISTBOX")) >= 0) bTarget = TRUE;
		else if (strClassName.Find(_T("SYSLISTVIEW32")) >= 0) bTarget = TRUE;
		else if (strClassName.Find(_T("COMBOBOX")) >= 0) bTarget = TRUE;

		// ★追加：ヘッダーコントロール(SysHeader32)も対象にする
		// これでリストビューの列見出しが不透明になるはずです
		else if (strClassName.Find(_T("SYSHEADER32")) >= 0) bTarget = TRUE;

		if (bTarget)
		{
			// 二重登録防止のためにチェック（念のため）
			// ※SetWindowSubclassは同じIDでの二重登録を無視してくれるが、newを防ぐために
			// （厳密なチェックは省略していますが、OnInitDialog一回のみの呼び出しなら安全です）

			CControlFixer* pFixer = new CControlFixer();
			if (pFixer->Install(hChild))
			{
				m_fixerList.AddTail(pFixer);
				DebugOutput(_T("Fixed: HWND=0x%08X (%s)\n"), hChild, strClassName);
			}
			else
			{
				delete pFixer;
			}
		}

		// ★再帰呼び出し：この子ウィンドウの中にさらに孫がいるか探す
		// （例：リストビューの中のヘッダー、コンボボックスの中のエディットなど）
		RecursiveApplyFix(hChild);

		// 次の兄弟へ
		hChild = ::GetWindow(hChild, GW_HWNDNEXT);
	}
}

void CBlurDialogBase::FixListControlsBackground()
{
	COSVersion os;
	os.GetVersionString();

	if (os.in.dwMajorVersion < 10 || os.in.dwBuildNumber < 22000)
		return;

	HWND hChild = ::GetWindow(m_hWnd, GW_CHILD);

	while (hChild != NULL)
	{
		TCHAR className[256] = { 0 };
		::GetClassName(hChild, className, 255);
		CString strClassName(className);
		strClassName.MakeUpper();

		if (strClassName.Find(_T("SYSLISTVIEW32")) >= 0)
		{
			CListCtrl* pListCtrl = (CListCtrl*)CWnd::FromHandle(hChild);
			if (pListCtrl)
			{
				// 背景色の設定はせず、文字色を黒に確定させる
				pListCtrl->SetTextColor(RGB(0, 0, 0));
				pListCtrl->SetTextBkColor(CLR_NONE);
				pListCtrl->SetBkColor(CLR_NONE);
			}
		}

		hChild = ::GetWindow(hChild, GW_HWNDNEXT);
	}
}

void CBlurDialogBase::ApplyDwmBlur()
{
	if (!m_hWnd || !::IsWindow(m_hWnd)) return;

	COSVersion os;
	os.GetVersionString();

	if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000)
	{
		LONG exStyle = ::GetWindowLong(m_hWnd, GWL_EXSTYLE);
		if (exStyle & WS_EX_LAYERED)
		{
			::SetWindowLong(m_hWnd, GWL_EXSTYLE, exStyle & ~WS_EX_LAYERED);
		}

		BOOL compositionEnabled = FALSE;
		::DwmIsCompositionEnabled(&compositionEnabled);

		if (compositionEnabled)
		{
			int backdropType = 3;
			HRESULT hr = ::DwmSetWindowAttribute(m_hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));

			if (SUCCEEDED(hr))
			{
				EnableRoundedCorners(m_hWnd);
				MARGINS margins = { -1, -1, -1, -1 };
				::DwmExtendFrameIntoClientArea(m_hWnd, &margins);
				m_bBlurApplied = TRUE;
			}
		}
	}
	else if (os.in.dwMajorVersion == 10)
	{
		LONG exStyle = ::GetWindowLong(m_hWnd, GWL_EXSTYLE);
		::SetWindowLong(m_hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
		::SetLayeredWindowAttributes(m_hWnd, 0, 245, LWA_ALPHA);

		DWM_BLURBEHIND bb = { 0 };
		bb.dwFlags = DWM_BB_ENABLE;
		bb.fEnable = TRUE;
		::DwmEnableBlurBehindWindow(m_hWnd, &bb);
		m_bBlurApplied = TRUE;
	}
	else if (os.in.dwMajorVersion >= 6)
	{
		LONG exStyle = ::GetWindowLong(m_hWnd, GWL_EXSTYLE);
		::SetWindowLong(m_hWnd, GWL_EXSTYLE, exStyle | WS_EX_LAYERED);
		::SetLayeredWindowAttributes(m_hWnd, 0, 245, LWA_ALPHA);

		DWM_BLURBEHIND bb = { 0 };
		bb.dwFlags = DWM_BB_ENABLE;
		bb.fEnable = TRUE;
		::DwmEnableBlurBehindWindow(m_hWnd, &bb);
		m_bBlurApplied = TRUE;
	}
}

void CBlurDialogBase::OnPaint()
{
	COSVersion os;
	os.GetVersionString();

	if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000)
	{
		CPaintDC dc(this);
		CRect rect;
		GetClientRect(&rect);
		dc.FillSolidRect(&rect, RGB(250, 250, 250));
	}
	else
	{
		Default();
	}
}

HBRUSH CBlurDialogBase::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	COSVersion os;
	os.GetVersionString();

	// Windows 11の場合
	if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000)
	{
		// 背景処理用に「白ブラシ」を用意
		static CBrush brushWhite(RGB(255, 255, 255));

		// ダイアログ背景（ここは透明のまま）
		if (nCtlColor == CTLCOLOR_DLG)
		{
			pDC->SetBkMode(TRANSPARENT);
			return (HBRUSH)GetStockObject(NULL_BRUSH);
		}

		// コントロール類
		// ★重要変更：無理に透明ブラシやマゼンタを返さず、
		// 「白背景・黒文字」として正しく描画させる。
		// 透過処理は後でピクセル操作で行うため、ここでは「白」で塗らせてOK。
		if (nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_BTN ||
			nCtlColor == CTLCOLOR_LISTBOX || nCtlColor == CTLCOLOR_EDIT)
		{
			pDC->SetBkMode(OPAQUE);
			pDC->SetBkColor(RGB(255, 255, 255));
			pDC->SetTextColor(RGB(0, 0, 0));
			return (HBRUSH)brushWhite.GetSafeHandle();
		}
	}
	else if (os.in.dwMajorVersion >= 6)
	{
		// Windows 10以前（変更なし）
		static CBrush brushBg(RGB(248, 248, 248));
		if (nCtlColor == CTLCOLOR_DLG || nCtlColor == CTLCOLOR_STATIC || nCtlColor == CTLCOLOR_BTN)
		{
			pDC->SetBkMode(TRANSPARENT);
			pDC->SetTextColor(RGB(0, 0, 0));
			return (HBRUSH)brushBg.GetSafeHandle();
		}
		if (nCtlColor == CTLCOLOR_LISTBOX || nCtlColor == CTLCOLOR_EDIT)
		{
			pDC->SetBkMode(OPAQUE);
			pDC->SetTextColor(RGB(0, 0, 0));
			pDC->SetBkColor(RGB(255, 255, 255));
			return (HBRUSH)::GetStockObject(WHITE_BRUSH);
		}
	}

	return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
}

BOOL CBlurDialogBase::OnEraseBkgnd(CDC* pDC)
{
	COSVersion os;
	os.GetVersionString();

	if (os.in.dwMajorVersion == 10 && os.in.dwBuildNumber >= 22000)
	{
		return TRUE;
	}
	else if (os.in.dwMajorVersion >= 6)
	{
		CRect rect;
		GetClientRect(&rect);
		pDC->FillSolidRect(&rect, RGB(248, 248, 248));
		return TRUE;
	}

	return CDialog::OnEraseBkgnd(pDC);
}

void CBlurDialogBase::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CDialog::OnWindowPosChanged(lpwndpos);

	if ((lpwndpos->flags & SWP_SHOWWINDOW) && !m_bBlurApplied)
	{
		if (m_hWnd && ::IsWindow(m_hWnd))
		{
			ApplyDwmBlur();
		}
	}
}

void CBlurDialogBase::OnNcPaint()
{
	Default();
}

void CBlurDialogBase::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp)
{
	CDialog::OnNcCalcSize(bCalcValidRects, lpncsp);
}