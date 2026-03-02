#include "stdafx.h"
#include "ListCtrlA.h"

CListCtrlA::CListCtrlA(void)
{
}

CListCtrlA::~CListCtrlA(void)
{
}

BEGIN_MESSAGE_MAP(CListCtrlA, CListCtrl)
	//{{AFX_MSG_MAP(CListCtrls)
	ON_WM_CREATE()
	//}}AFX_MSG_MAP
    ON_NOTIFY_EX(TTN_NEEDTEXT, 0, OnToolTipText)
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnToolTipText)
    ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipText)
END_MESSAGE_MAP()


int CListCtrlA::OnCreate(LPCREATESTRUCT lpCreateStruct) 
{
	if (CListCtrl::OnCreate(lpCreateStruct) == -1)
		return -1;
	return 0;
}

BOOL CListCtrlA::OnToolTipText( UINT id, NMHDR * pNMHDR, LRESULT * pResult ){
	TOOLTIPTEXTA* pTTTA = (TOOLTIPTEXTA*)pNMHDR;
	TOOLTIPTEXTW* pTTTW = (TOOLTIPTEXTW*)pNMHDR;

	CString i,j,k,l,m;
	UINT nID = pNMHDR->idFrom;
	AFX_MODULE_THREAD_STATE* pThreadState = AfxGetModuleThreadState();
	CToolTipCtrl *pToolTip = pThreadState->m_pToolTip;
	if(pToolTip==NULL) return FALSE;
	pToolTip->SetMaxTipWidth(500);
	if( nID == 0 )	  	// Notification in NT from automatically
		return FALSE;   	// created tooltip

	int row = ((nID-1) >> 10) & 0x3fffff ;
	int col = (nID-1) & 0x3ff;
	i = GetItemText( row, 0 );
	j = GetItemText( row, 3 );
	k = GetItemText( row, 4 );
	l = GetItemText( row, 2 );
	m = GetItemText( row, 1 );
	strTipText.Format(LL14(L"名前：%s\nアーティスト：%s\nアルバム：%s\n時間：%s\n種類：%s", L"Name: %s\nArtist: %s\nAlbum: %s\nTime: %s\nType: %s", L"Nom : %s\nArtiste : %s\nAlbum : %s\nDurée : %s\nType : %s", L"Nome: %s\nArtista: %s\nAlbum: %s\nTempo: %s\nTipo: %s", L"Nombre: %s\nArtista: %s\nÁlbum: %s\nTiempo: %s\nTipo: %s", L"이름: %s\n아티스트: %s\n앨범: %s\n시간: %s\n유형: %s", L"名称：%s\n艺术家：%s\n专辑：%s\n时间：%s\n类型：%s", L"الاسم: %s\nالفنان: %s\nالألبوم: %s\nالوقت: %s\nالنوع: %s", L"Название: %s\nИсполнитель: %s\nАльбом: %s\nВремя: %s\nТип: %s", L"Name: %s\nInterpret: %s\nAlbum: %s\nZeit: %s\nTyp: %s", L"Nome: %s\nArtista: %s\nÁlbum: %s\nTempo: %s\nTipo: %s", L"Naam: %s\nArtiest: %s\nAlbum: %s\nTijd: %s\nType: %s", L"Nazwa: %s\nWykonawca: %s\nAlbum: %s\nCzas: %s\nTyp: %s", L"Ad: %s\nSanatçı: %s\nAlbüm: %s\nSüre: %s\nTür: %s"),i,j,k,l,m);

//#ifndef _UNICODE
//	if (pNMHDR->code == TTN_NEEDTEXTA)
//		lstrcpyn(pTTTA->szText, strTipText, 579);
//	else
//		_mbstowcsz(pTTTW->szText, strTipText, 579);
//#else
//	if (pNMHDR->code == TTN_NEEDTEXTA)
//		_wcstombsz(pTTTA->szText, strTipText, 579);
//	else
//		lstrcpyn(pTTTW->szText, strTipText, 579);
//#endif
#ifndef _UNICODE
	if (pNMHDR->code == TTN_NEEDTEXTA){
		lstrcpyn(ff1,strTipText,1024);
		pTTTA->lpszText= ff1;
		pTTTA->szText[0]=NULL;
	}
	else{
		int rr=MultiByteToWideChar(CP_ACP,0,strTipText,-1,ff2,1024);
		pTTTW->lpszText= ff2;
		pTTTW->szText[0]=NULL;
	}
#else
	if (pNMHDR->code == TTN_NEEDTEXTA)
		pTTTA->lpszText = (LPSTR)(LPWSTR)(LPCWSTR)strTipText;
	else
	    pTTTW->lpszText = (LPWSTR)(LPCWSTR)strTipText;
#endif
	*pResult = 0;

	return TRUE;    // message was handled
}

INT_PTR CListCtrlA::OnToolHitTest( CPoint point, TOOLINFO* pTI) const{
	int row, col;
	RECT cellrect;
	row = CellRectFromPoint(point, &cellrect, &col );

	if ( row == -1 ) 
		return -1;

	pTI->hwnd = m_hWnd;
	pTI->uId = (UINT)((row<<10)+(col&0x3ff)+1);
	pTI->lpszText = LPSTR_TEXTCALLBACK;

	cellrect.bottom += 200;
	pTI->rect = cellrect;

	return pTI->uId;
}

int CListCtrlA::CellRectFromPoint(CPoint & point, RECT * cellrect, int * col) const{
	int colnum;

	// Make sure that the ListView is in LVS_REPORT
	if( (GetWindowLong(m_hWnd, GWL_STYLE) & LVS_TYPEMASK) != LVS_REPORT )
		return -1;

	// Get the top and bottom row visible
	int row = GetTopIndex();
	int bottom = row + GetCountPerPage();
	if( bottom > GetItemCount() )
		bottom = GetItemCount();
	
	// Get the number of columns
	CHeaderCtrl* pHeader = (CHeaderCtrl*)GetDlgItem(0);
	int nColumnCount = pHeader->GetItemCount();

	// Loop through the visible rows
	for( ;row <=bottom;row++)
	{
		// Get bounding rect of item and check whether point falls in it.
		CRect rect;
		GetItemRect( row, &rect, LVIR_BOUNDS );
		if( rect.PtInRect(point) )
		{
			// Now find the column
			for( colnum = 0; colnum < nColumnCount; colnum++ )
			{
				int colwidth = GetColumnWidth(colnum);
				if( point.x >= rect.left 
					&& point.x <= (rect.left + colwidth ) )
				{
					RECT rectClient;
					GetClientRect( &rectClient );
					if( col ) *col = colnum;
					rect.right = rect.left + colwidth;

					// Make sure that the right extent does not exceed
					// the client area
					if( rect.right > rectClient.right ) 
						rect.right = rectClient.right;
					*cellrect = rect;
					return row;
				}
				rect.left += colwidth;
			}
		}
	}
	return -1;
}

void CListCtrlA::PreSubclassWindow() 
{
	// TODO: この位置に固有の処理を追加するか、または基本クラスを呼び出してください
//    EnableToolTips(TRUE);

	CListCtrl::PreSubclassWindow();
}

BOOL CListCtrlA::PreTranslateMessage(MSG* pMsg) 
{
	// TODO: この位置に固有の処理を追加するか、または基本クラスを呼び出してください
//	::SendMessage(m_hWnd, TTM_RELAYEVENT, 0, (LPARAM)pMsg);
	return CListCtrl::PreTranslateMessage(pMsg);
}

BOOL CListCtrlA::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
	HD_NOTIFY * phdn = (HD_NOTIFY *)lParam;
	switch(phdn->hdr.code)
		{
		case HDN_BEGINTRACKA:
		case HDN_BEGINTRACKW:
		case HDN_DIVIDERDBLCLICKA:
		case HDN_DIVIDERDBLCLICKW:
//			if(phdn->iItem > 3)
//				*pResult = TRUE;
//				return TRUE;
			break;
		}

	return CListCtrl::OnNotify(wParam, lParam, pResult);
}
