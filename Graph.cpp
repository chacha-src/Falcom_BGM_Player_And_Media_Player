// Graph.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Graph.h"
#include "Dwmapi.h"
#include <Mtype.h>

extern IGraphBuilder *pGraphBuilder;
// CGraph ダイアログ

IMPLEMENT_DYNAMIC(CGraph, CCustomBlurDialogBase)

CGraph::CGraph(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CGraph::IDD, pParent)
{

}

CGraph::~CGraph()
{
}

void CGraph::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDC_LIST1, m_l);
}


BEGIN_MESSAGE_MAP(CGraph, CCustomBlurDialogBase)
END_MESSAGE_MAP()

extern IAMStreamSelect *iam;
// CGraph メッセージ ハンドラ

BOOL CGraph::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"グラフフィルタ一覧", L"Graph Filter List", L"Liste filtres graphiques", L"Elenco filtri grafici", L"Lista filtros gráficos", L"그래프 필터 목록", L"图形过滤器列表", L"قائمة مرشحات الرسم", L"Список фильтров графика", L"Grafikfilterliste", L"Lista filtros gráficos", L"Grafiekfilterlijst", L"Lista filtrów graficznych", L"Grafik filtre listesi"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	if(pGraphBuilder){
//		IFilterGraph *fg;
//		pGraphBuilder->QueryInterface(IID_IFilterGraph,(void**)&fg);

		EnumFilters(pGraphBuilder);

		if (iam) {
			m_l.AddString(L"");
			m_l.AddString(LL14(L"IAMStreamSelectの内容", L"IAMStreamSelect contents", L"Contenu IAMStreamSelect", L"Contenuto IAMStreamSelect", L"Contenido IAMStreamSelect", L"IAMStreamSelect 내용", L"IAMStreamSelect内容", L"محتوى IAMStreamSelect", L"Содержимое IAMStreamSelect", L"IAMStreamSelect-Inhalt", L"Conteúdo IAMStreamSelect", L"IAMStreamSelect-inhoud", L"Zawartość IAMStreamSelect", L"IAMStreamSelect içeriği"));
			m_l.AddString(L"");

			CntPin2(iam);
		}
		//		fg->Release();
	}else EndDialog(0);

	// 旧: r.top+=600; r.bottom+=600 → 高さは増えず下へ移動するだけ（縦が潰れて見えた主因）
	UINT dpi = 96;
	if (HDC hdc = ::GetDC(m_hWnd)) {
		dpi = (UINT)::GetDeviceCaps(hdc, LOGPIXELSX);
		::ReleaseDC(m_hWnd, hdc);
	}
	if (dpi == 0) dpi = 96;

	CRect wr, cr;
	GetWindowRect(&wr);
	GetClientRect(&cr);
	const int borderW = wr.Width() - cr.Width();
	const int borderH = wr.Height() - cr.Height();
	const int clientW = MulDiv(520, (int)dpi, 96);
	const int clientH = MulDiv(420, (int)dpi, 96);
	SetWindowPos(NULL, 0, 0, clientW + borderW, clientH + borderH,
		SWP_NOMOVE | SWP_NOZORDER);
	CenterWindow();

	GetClientRect(&cr);
	const int pad = MulDiv(8, (int)dpi, 96);
	const int gap = MulDiv(8, (int)dpi, 96);
	const int btnH = MulDiv(28, (int)dpi, 96);
	const int btnW = MulDiv(90, (int)dpi, 96);
	if (m_l.GetSafeHwnd()) {
		m_l.MoveWindow(pad, pad, cr.Width() - 2 * pad, cr.Height() - 2 * pad - btnH - gap);
		m_l.SetItemHeight(0, MulDiv(24, (int)dpi, 96));
	}
	if (m_ok.GetSafeHwnd())
		m_ok.MoveWindow((cr.Width() - btnW) / 2, cr.Height() - pad - btnH, btnW, btnH);

	CCC_BringDialogToForeground(this);
	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

DWORD CGraph::CntPin2(IAMStreamSelect *pFilter) {
	DWORD i, j, k, l;
	k = 0;
	l = 0;
	AM_MEDIA_TYPE *am;
	CString s;
	LPWSTR p;
	pFilter->Count(&i);
	l = i;
	for (j = 0; j < i; j++) {
		pFilter->Info(j, &am, NULL, NULL, NULL, &p, NULL, NULL);
			s= p;
			m_l.AddString(s);
			CoTaskMemFree(p);
			DeleteMediaType(am);
			FreeMediaType(*am);
	}
	return l;
}

HRESULT CGraph::EnumFilters (IGraphBuilder *pGraph) 
{
    IEnumFilters *pEnum = NULL;
    IBaseFilter *pFilter;
    ULONG cFetched;

    HRESULT hr = pGraph->EnumFilters(&pEnum);
    if (FAILED(hr)) return hr;
	int flg01=0;
    while(pEnum->Next(1, &pFilter, &cFetched) == S_OK)
    {
		IEnumPins *p;
		IPin *pPin;
		CString s,ss,sss;s="",ss="",sss="";
        FILTER_INFO FilterInfo,FilterInfo1;
        pFilter->QueryFilterInfo(&FilterInfo);
		pFilter->EnumPins(&p);
		s=" -> ";
        char szName[MAX_FILTER_NAME];
        char szName1[MAX_FILTER_NAME];
        int cch = WideCharToMultiByte(CP_ACP, 0, FilterInfo.achName,
            -1, szName, MAX_FILTER_NAME, 0, 0);
		if (cch > 0){
			ss=szName;
		}
		while(p->Next(1, &pPin, 0) == S_OK)
		{
			PIN_DIRECTION PinDirThis;
			pPin->QueryDirection(&PinDirThis);
			s=" -> ";
			if (PinDirThis == PINDIR_OUTPUT){
				PIN_INFO pp;
				IPin *pn;
				if(pPin->ConnectedTo(&pn)==S_OK){
					pn->QueryPinInfo(&pp);
					pp.pFilter->QueryFilterInfo(&FilterInfo1);
					WideCharToMultiByte(CP_ACP, 0, FilterInfo1.achName,
						-1, szName1, MAX_FILTER_NAME, 0, 0);
					s+=szName1;sss=ss+s;
					if(sss.Find(_T("Haali"))>=0) flg01=1;
					if(!(szName[1]==':'||sss.Find('\\')!=-1)){
						m_l.AddString(sss);
					}else{
						sss=_T("Source")+s;
						if(flg01==0)
							m_l.AddString(sss);
					}	
					s=" -> ";pn->Release();
				}else{
					//s+="UnKnown";sss=ss+s;
					//if(!(szName[1]==':'||sss.Find('.')!=-1)){
					//	m_l.AddString(sss);
				//	}
					s=" -> ";
				}
			}
		}
		p->Release();
        // FILTER_INFO 構造体はフィルタ グラフ マネージャへのポインタを保持する。
        // その参照カウントは解放しなければならない。
        if (FilterInfo.pGraph != NULL)
            FilterInfo.pGraph->Release();
        pFilter->Release();
    }

    pEnum->Release();
    return S_OK;
}
