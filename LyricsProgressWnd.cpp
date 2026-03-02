// LyricsProgressWnd.cpp
#include "StdAfx.h"
#include "LyricsProgressWnd.h"

BEGIN_MESSAGE_MAP(CLyricsProgressWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

CLyricsProgressWnd::CLyricsProgressWnd()
{
	m_strText = LL14(L"歌詞取得中...\n長い時10秒くらいかかります\n\n取得できない時は歌詞はでません。", L"Fetching lyrics...\nMay take up to 10 seconds.\n\nLyrics will not appear if unavailable.", L"Récupération des paroles...\nPeut prendre jusqu'à 10 s.\n\nLes paroles n'apparaîtront pas si indisponibles.", L"Recupero dei testi...\nPuò richiedere fino a 10 s.\n\nI testi non appariranno se indisponibili.", L"Obteniendo letras...\nPuede tardar hasta 10 s.\n\nLas letras no aparecerán si no hay.", L"가사 가져오는 중...\n최대 10초 걸릴 수 있습니다.\n\n가져올 수 없으면 가사가 표시되지 않습니다.", L"正在获取歌词...\n可能需要约10秒。\n\n无法获取时不会显示歌词。", L"جلب كلمات الأغنية...\nقد يستغرق 10 ثوانٍ.\n\nلن تظهر الكلمات إذا غير متوفرة.", L"Загрузка текста...\nМожет занять до 10 с.\n\nТекст не появится, если недоступен.", L"Texte werden geladen...\nKann bis zu 10 s dauern.\n\nText erscheint nicht, wenn nicht verfügbar.", L"Obtendo letras...\nPode levar até 10 s.\n\nLetras não aparecem se indisponíveis.", L"Teksten ophalen...\nKan tot 10 s duren.\n\nTekst verschijnt niet als niet beschikbaar.", L"Pobieranie tekstu...\nMoże potrwać do 10 s.\n\nTekst nie pojawi się, jeśli niedostępny.", L"Şarkı sözleri alınıyor...\n10 saniye sürebilir.\n\nMevcut değilse sözler görünmez.");
}

CLyricsProgressWnd::~CLyricsProgressWnd()
{
	if (m_hWnd != NULL) {
		DestroyWindow();
	}
}

int ScaleByDPI(int value, UINT dpi) { return (int)(((float)value) * ((float)dpi) / 96.0f); }

typedef UINT(WINAPI* LPGetDpiForWindow)(HWND);

UINT GetDpiForWindowCompat(HWND hWnd)
{
	HDC hdc = ::GetDC(hWnd);
	UINT dpi = GetDeviceCaps(hdc, LOGPIXELSX);
	::ReleaseDC(hWnd, hdc);
	return dpi;
}

BOOL CLyricsProgressWnd::Create(CWnd* pParent)
{
	// ウィンドウクラス登録
	CString strWndClass = AfxRegisterWndClass(
		CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS,
		::LoadCursor(NULL, IDC_WAIT), // 砂時計カーソル
		(HBRUSH)(COLOR_WINDOW + 1),
		NULL
	);

	UINT dpi = GetDpiForWindowCompat(m_hWnd);

	// 親ウィンドウの中央に配置
	CRect rect(0, 0, ScaleByDPI(300,dpi), ScaleByDPI(100,dpi));
	if (pParent != NULL && pParent->GetSafeHwnd() != NULL) {
		CRect parentRect;
		pParent->GetWindowRect(&parentRect);

		int x = parentRect.left + (parentRect.Width() - rect.Width()) / 2;
		int y = parentRect.top + (parentRect.Height() - rect.Height()) / 2;

		rect.OffsetRect(x, y);
	}
	else {
		// 親がない場合は画面中央
		int screenWidth = GetSystemMetrics(SM_CXSCREEN);
		int screenHeight = GetSystemMetrics(SM_CYSCREEN);

		int x = (screenWidth - rect.Width()) / 2;
		int y = (screenHeight - rect.Height()) / 2;

		rect.OffsetRect(x, y);
	}

	// ウィンドウ作成（ポップアップ、枠付き、常に最前面）
	BOOL result = CreateEx(
		WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
		strWndClass,
		_T(""),
		WS_POPUP | WS_BORDER,
		rect.left, rect.top, rect.Width(), rect.Height(),
		pParent ? pParent->GetSafeHwnd() : NULL,
		NULL
	);

	if (result) {
		// フォント作成
		m_font.CreatePointFont(120, _T("MS UI Gothic"));

		// ウィンドウを最前面に
		SetWindowPos(&wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}

	return result;
}



void CLyricsProgressWnd::SetText(const CString& text)
{
	m_strText = text;

	if (m_hWnd != NULL) {

		CClientDC dc(this);
		CFont* pOldFont = dc.SelectObject(&m_font);

		// テキストサイズ（論理座標）
		CRect calcRect(0, 0, 0, 0);
		dc.DrawText(m_strText, &calcRect, DT_CALCRECT | DT_WORDBREAK);

		dc.SelectObject(pOldFont);

		// 現在 DPI を取得
		UINT dpi = GetDpiForWindowCompat(m_hWnd);

		// DPI スケール
		int padding = ScaleByDPI(40, dpi);
		int width = ScaleByDPI(calcRect.Width(), dpi) + padding;
		int height = ScaleByDPI(calcRect.Height(), dpi) + padding;

		// ウィンドウ位置はそのまま、サイズだけ変更
		SetWindowPos(NULL, 0, 0, width, height,
			SWP_NOMOVE | SWP_NOZORDER);

		Invalidate();
		UpdateWindow();
	}
}



void CLyricsProgressWnd::Show()
{
	if (m_hWnd != NULL) {
		ShowWindow(SW_SHOW);
		UpdateWindow();

		// メッセージポンプを回して即座に表示
		MSG msg;
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
}

void CLyricsProgressWnd::Hide()
{
	if (m_hWnd != NULL) {
		ShowWindow(SW_HIDE);
	}
}

void CLyricsProgressWnd::OnPaint()
{
	CPaintDC dc(this);

	CRect clientRect;
	GetClientRect(&clientRect);

	// 背景塗りつぶし
	dc.FillSolidRect(&clientRect, RGB(230, 240, 255));

	// 枠線
	CPen pen(PS_SOLID, 2, RGB(100, 150, 200));
	CPen* pOldPen = dc.SelectObject(&pen);
	dc.Rectangle(&clientRect);
	dc.SelectObject(pOldPen);

	// フォント設定
	CFont* pOldFont = dc.SelectObject(&m_font);
	dc.SetBkMode(TRANSPARENT);
	dc.SetTextColor(RGB(50, 50, 150));

	// --- ① テキストサイズ計測 ---
	CRect textRect(0, 0, clientRect.Width(), 0);
	dc.DrawText(m_strText, &textRect, DT_WORDBREAK | DT_CALCRECT);

	// --- ② 中央に配置するためのオフセット計算 ---
	int x = (clientRect.Width() - textRect.Width()) / 2;
	int y = (clientRect.Height() - textRect.Height()) / 2;

	// 描画用の矩形を作成
	CRect drawRect = textRect;
	drawRect.OffsetRect(x, y);

	// --- ③ 描画 ---
	dc.DrawText(m_strText, &drawRect, DT_WORDBREAK);

	dc.SelectObject(pOldFont);
}


BOOL CLyricsProgressWnd::OnEraseBkgnd(CDC* pDC)
{
	// ちらつき防止のため、OnPaintで全て描画
	return TRUE;
}