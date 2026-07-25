// Kpilist.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Kpilist.h"
#include <algorithm>

// 先に使用するファイルスコープ関数の前方宣言
static void KpiPersistSavedata();


// CKpilist ダイアログ

IMPLEMENT_DYNAMIC(CKpilist, CCustomBlurDialogBase)

CKpilist::CKpilist(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CKpilist::IDD, pParent)
{

}

CKpilist::~CKpilist()
{
}

void CKpilist::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_lc);
	DDX_Control(pDX, IDOK, m_okdummy);
	DDX_Control(pDX, IDC_STATIC, m_desc);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CKpilist, CCustomBlurDialogBase)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, &CKpilist::OnLvnItemchangedList1)
	ON_BN_CLICKED(IDOK, &CKpilist::OnBnClickedOk)
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_WM_DESTROY()
cmn(CKpilist);

// CKpilist メッセージ ハンドラ
extern CString ext[150][300];
extern int kpicnt;
extern CString kpif[400];
extern TCHAR kpifs[200][64];
extern BOOL kpichk[200];
extern BYTE kpiarch[150];
extern BYTE kvar[150][300];
extern TCHAR karento2[1024];

IMPLEMENT_DYNAMIC(CKpiListCtrl, CCustomListCtrl)

BEGIN_MESSAGE_MAP(CKpiListCtrl, CCustomListCtrl)
END_MESSAGE_MAP()

void CKpiListCtrl::BuildToolTipText(int row, int col, CString& out)
{
	UNREFERENCED_PARAMETER(col);
	out.Empty();
	if (row < 0 || row >= GetItemCount() || row >= kpicnt)
		return;

	CString exts;
	for (int i = 0; ; ++i)
	{
		if (ext[row][i].IsEmpty())
			break;
		if (!exts.IsEmpty())
			exts += L'/';
		exts += ext[row][i];
	}

	CString ver;
	ver.Format(L"%u", (unsigned)kvar[row][0]);
	CString arch = L"?";
	if (kpiarch[row] == 32)
		arch = L"x86";
	else if (kpiarch[row] == 64)
		arch = L"x64";

	out.Format(LL14(
		L"パス：%s\nバージョン：%s\nCPU：%s\n拡張子：%s",
		L"Path: %s\nVersion: %s\nCPU: %s\nExtensions: %s",
		L"Chemin : %s\nVersion : %s\nCPU : %s\nExtensions : %s",
		L"Percorso: %s\nVersione: %s\nCPU: %s\nEstensioni: %s",
		L"Ruta: %s\nVersión: %s\nCPU: %s\nExtensiones: %s",
		L"경로: %s\n버전: %s\nCPU: %s\n확장자: %s",
		L"路径：%s\n版本：%s\nCPU：%s\n扩展名：%s",
		L"المسار: %s\nالإصدار: %s\nالمعالج: %s\nالامتدادات: %s",
		L"Путь: %s\nВерсия: %s\nЦП: %s\nРасширения: %s",
		L"Pfad: %s\nVersion: %s\nCPU: %s\nErweiterungen: %s",
		L"Caminho: %s\nVersão: %s\nCPU: %s\nExtensões: %s",
		L"Pad: %s\nVersie: %s\nCPU: %s\nExtensies: %s",
		L"Ścieżka: %s\nWersja: %s\nCPU: %s\nRozszerzenia: %s",
		L"Yol: %s\nSürüm: %s\nCPU: %s\nUzantılar: %s"),
		(LPCTSTR)kpif[row], (LPCTSTR)ver, (LPCTSTR)arch, (LPCTSTR)exts);
}

BOOL CKpilist::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"kpi一覧", L"kpi list", L"Liste kpi", L"Elenco kpi", L"Lista kpi", L"kpi 목록", L"kpi 列表", L"قائمة kpi", L"Список kpi", L"kpi-Liste", L"Lista kpi", L"kpi-lijst", L"Lista kpi", L"kpi listesi"));
	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	m_tooltip.AddTool(&m_okdummy, LL14(L"閉じます", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 512, 10000);

	Init();
	if (m_lc.GetSafeHwnd())
	{
		m_lc.EnableToolTips(TRUE);
		DWORD ex = m_lc.GetExtendedStyle();
		ex |= LVS_EX_INFOTIP;
		m_lc.SetExtendedStyle(ex);
	}

	// サイジング枠(WS_THICKFRAME)はダイアログテンプレート側で付与済み。

	// これ以上小さくできない最小サイズ = テンプレートの初期サイズ
	CRect wr;
	GetWindowRect(&wr);
	m_minW = wr.Width();
	m_minH = wr.Height();

	// 保存済みのサイズ・位置を復元(未保存 kpiWndW==0 のときは既定位置のまま)
	RestoreSavedPlacement();

	// 初期レイアウト(kpi/拡張子 列を自動フィット。kpi 初期幅もここで拡げる)
	LayoutControls();

	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

// リサイズ時に子コントロール(説明/リスト/OK)を再配置する。
// 位置・サイズはダイアログテンプレート(DLU)の比率を MapDialogRect で px 換算して再現。
void CKpilist::LayoutControls()
{
	if (!m_lc.GetSafeHwnd()) return;

	// 1 DLU あたりの px を求める(フォント依存・DPI 依存を吸収)
	CRect base(0, 0, 4, 8);
	MapDialogRect(&base);
	const double dx = base.Width() / 4.0;
	const double dy = base.Height() / 8.0;
	auto PX = [](double v) { return (int)(v + 0.5); };

	CRect rc;
	GetClientRect(&rc);
	const int cx = rc.Width();
	const int cy = rc.Height();

	const int mx = PX(7 * dx);              // 左右マージン
	const int bw = PX(50 * dx);             // OK ボタン幅
	const int bh = PX(14 * dy);             // OK ボタン高さ
	const int descTop = PX(11 * dy);
	const int descH = PX(9 * dy);
	const int listTop = PX(25 * dy);

	// OK ボタン: 下端中央
	const int by = cy - PX(7 * dy) - bh;
	if (m_okdummy.GetSafeHwnd())
		m_okdummy.MoveWindow((cx - bw) / 2, by, bw, bh);

	// 説明テキスト: 上部いっぱい
	if (m_desc.GetSafeHwnd())
		m_desc.MoveWindow(mx, descTop, (std::max)(0, cx - 2 * mx), descH);

	// リスト: 説明の下〜ボタンの上
	const int listBottom = by - PX(6 * dy);
	const int listH = (std::max)(0, listBottom - listTop);
	m_lc.MoveWindow(mx, listTop, (std::max)(0, cx - 2 * mx), listH);

	LayoutKpiColumns();
}

// kpi と 拡張子 の列幅を、Ver/Arch を固定したうえで残り幅へ自動フィットさせる。
// 拡張子 列は右端までフィット(kpi と 2 分割し、リサイズで両方が拡縮する)。
void CKpilist::LayoutKpiColumns()
{
	if (!m_lc.GetSafeHwnd()) return;
	if (m_lc.GetHeaderCtrl() == NULL || m_lc.GetHeaderCtrl()->GetItemCount() < 4) return;

	CRect rc;
	m_lc.GetClientRect(&rc);   // 縦スクロールバー分は除かれる
	const int total = rc.Width();

	const int verW = 50;
	const int archW = 60;
	int avail = total - verW - archW;
	if (avail < 160) avail = 160;   // 最低限の割り当て

	int kpiW = (int)(avail * 0.45);
	const int minKpi = 90;
	if (kpiW < minKpi) kpiW = minKpi;
	int extW = avail - kpiW;        // 残り全部 → 右端までフィット
	const int minExt = 90;
	if (extW < minExt) extW = minExt;

	m_lc.SetColumnWidth(0, kpiW);
	m_lc.SetColumnWidth(1, verW);
	m_lc.SetColumnWidth(2, archW);
	m_lc.SetColumnWidth(3, extW);
}

void CKpilist::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) return;
	LayoutControls();
}

void CKpilist::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (m_minW > 0 && m_minH > 0)
	{
		lpMMI->ptMinTrackSize.x = m_minW;
		lpMMI->ptMinTrackSize.y = m_minH;
	}
	CCustomBlurDialogBase::OnGetMinMaxInfo(lpMMI);
}

// savedata に記録したサイズ・位置を復元する。
// 未保存(kpiWndW==0)なら Init() の既定位置のまま。最小サイズ以上・画面内に補正する。
void CKpilist::RestoreSavedPlacement()
{
	if (savedata.kpiWndW <= 0 || savedata.kpiWndH <= 0) return;

	CRect want(savedata.kpiWndX, savedata.kpiWndY,
		savedata.kpiWndX + savedata.kpiWndW,
		savedata.kpiWndY + savedata.kpiWndH);

	// 最小サイズを下回らないよう補正
	if (want.Width()  < m_minW) want.right  = want.left + m_minW;
	if (want.Height() < m_minH) want.bottom = want.top  + m_minH;

	// 対象モニタの作業領域内に収まるよう補正(画面外復元を防ぐ)
	HMONITOR hMon = ::MonitorFromRect(&want, MONITOR_DEFAULTTONEAREST);
	MONITORINFO mi; mi.cbSize = sizeof(mi);
	if (hMon && ::GetMonitorInfo(hMon, &mi))
	{
		const CRect wa(mi.rcWork);
		int w = (std::min)(want.Width(), (int)wa.Width());
		int h = (std::min)(want.Height(), (int)wa.Height());
		int x = want.left, y = want.top;
		if (x < wa.left)            x = wa.left;
		if (y < wa.top)             y = wa.top;
		if (x + w > wa.right)       x = wa.right - w;
		if (y + h > wa.bottom)      y = wa.bottom - h;
		want.SetRect(x, y, x + w, y + h);
	}

	MoveWindow(&want);
}

// 現在のサイズ・位置を savedata に記録する(最大化/最小化中は記録しない)。
void CKpilist::SaveSavedPlacement()
{
	if (!GetSafeHwnd() || !::IsWindow(m_hWnd)) return;
	if (IsIconic() || IsZoomed()) return;   // 通常状態の矩形のみ保存

	CRect wr;
	GetWindowRect(&wr);
	if (wr.Width() <= 0 || wr.Height() <= 0) return;

	savedata.kpiWndX = wr.left;
	savedata.kpiWndY = wr.top;
	savedata.kpiWndW = wr.Width();
	savedata.kpiWndH = wr.Height();
}

void CKpilist::OnDestroy()
{
	// OK / × / Esc いずれの閉じ方でもサイズ・位置を保存して永続化する
	if (status == 0)
	{
		SaveSavedPlacement();
		KpiPersistSavedata();
	}
	CCustomBlurDialogBase::OnDestroy();
}

BOOL kpichks[300];

// kpif[i] のフルパスからファイル名(ベース名)だけを取り出す
static CString KpiBaseName(const CString& path)
{
	return path.Right(path.GetLength() - path.ReverseFind('\\') - 1);
}

// 旧 kpilist.dat は廃止。残っていれば savedata へ一度だけ移植し、そのうえで削除する。
// savedata 側に既にエントリがあれば移植済みとみなし、残骸ファイルがあれば掃除する。
static void KpiMigrateLegacyToSavedata()
{
	CString ss = karento2;
	ss += _T("kpilist.dat");

	// savedata 未保存(=未移植)かつ旧ファイルがある場合のみ内容を取り込む
	if (savedata.kpiChkCnt == 0) {
		CFile ff;
		if (ff.Open(ss, CFile::modeRead | CFile::shareDenyWrite, NULL) == TRUE) {
			try {
				int cnt = 0;
				if (ff.Read(&cnt, sizeof(cnt)) == sizeof(cnt)) {
					if (cnt < 0) cnt = 0;
					if (cnt > 200) cnt = 200;
					int stored = 0;
					for (int i = 0; i < cnt; i++) {
						BOOL chk = TRUE;
						TCHAR nm[64] = { 0 };
						if (ff.Read(&chk, sizeof(BOOL)) != sizeof(BOOL)) break;
						if (ff.Read(nm, 64 * sizeof(TCHAR)) != 64 * sizeof(TCHAR)) break;
						nm[63] = 0;
						savedata.kpiChkState[stored] = chk ? 1 : 0;
						_tcsncpy(savedata.kpiChkName[stored], nm, 63);
						savedata.kpiChkName[stored][63] = 0;
						stored++;
					}
					savedata.kpiChkCnt = stored;
				}
			}
			catch (...) {
			}
			ff.Close();
		}
	}

	// 廃止済みのため、残っている kpilist.dat は削除する(移植は savedata へ完了)
	::DeleteFile(ss);
}

// 現在の savedata(=最新)を .dat へ書き出し、チェック変更を即座に永続化する。
static void KpiPersistSavedata()
{
	CString ss = karento2;
#if _UNICODE
	ss += L"oggYSEDbgmu.dat";
#else
	ss += "oggYSEDbgm.dat";
#endif
	CFile ab;
	if (ab.Open(ss, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive, NULL) == TRUE) {
		ab.Write(&savedata, sizeof(save));
		ab.Close();
	}
}

void CKpilist::Init()
{
	// 保存済みチェック状態を savedata から取得(初回は旧 kpilist.dat から移行)
	KpiMigrateLegacyToSavedata();
	int cnt = savedata.kpiChkCnt;
	if (cnt > 200) cnt = 200;

	// 現在のプラグイン名 → 保存済み状態を突き合わせて runtime の kpichk[] を確定。
	// 未保存の新規プラグインは既定で使用(チェックON)。並び順に依らずファイル名で復元。
	for (int j = 0; j < kpicnt; j++) {
		BOOL chk = TRUE;
		CString name = KpiBaseName(kpif[j]);
		for (int i = 0; i < cnt; i++) {
			if (name.CompareNoCase(savedata.kpiChkName[i]) == 0) {
				chk = savedata.kpiChkState[i] ? TRUE : FALSE;
				break;
			}
		}
		kpichk[j] = chk;
	}

	// status==1(起動時)はウィンドウ未生成のため UI は組まない
	if (status != 0) return;

	DWORD dwExStyle = m_lc.GetExtendedStyle();
	dwExStyle |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_INFOTIP | LVS_EX_CHECKBOXES;
	m_lc.SetExtendedStyle(dwExStyle);
	m_lc.ModifyStyle(0, LVS_REPORT);
	m_lc.InsertColumn(0, LL14(L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi", L"kpi"), LVCFMT_LEFT, 100, 0);
	m_lc.InsertColumn(1, L"Ver", LVCFMT_CENTER, 50, 0);
	m_lc.InsertColumn(2, L"Arch", LVCFMT_CENTER, 60, 0);
	m_lc.InsertColumn(3, LL14(L"拡張子", L"Extensions", L"Extensions", L"Estensioni", L"Extensiones", L"확장자", L"扩展名", L"الامتدادات", L"Расширения", L"Erweiterungen", L"Extensões", L"Extensies", L"Rozszerzenia", L"Uzantılar"), LVCFMT_LEFT, 340, 0);

	TCHAR *buf;
	buf = (TCHAR*)calloc(10000, 2);
	LV_ITEM LvItem;
	int      idItem;
	m_lc.DeleteAllItems();
	for (int j = 0; j<kpicnt; j++) {
		CString s; s = "";
		try {
			for (int i = 0;; i++) {
				if (ext[j][i] == "") break;
				s += ext[j][i]; s += "/";
			}
			s = s.Left(s.GetLength() - 1);
			int a1, a2, a3;
			a1 = kpif[j].GetLength() * 2;
			a2 = kpif[j].ReverseFind(L'\\');
			a3 = a1 - a2;
			_tcscpy(buf, kpif[j].Right(a3));	LvItem.pszText = buf;

			LvItem.iItem = m_lc.GetItemCount();
			LvItem.mask = LVIF_TEXT | LVIF_STATE;
			LvItem.stateMask = LVIS_FOCUSED | LVIS_SELECTED;
			LvItem.state = 0;
			LvItem.iSubItem = 0;
			LvItem.cchTextMax = _tcslen(LvItem.pszText);
			idItem = m_lc.InsertItem(&LvItem);
			// InsertItem() によって item ID (行番号) が返される
			LvItem.iItem = idItem;
			CString sss; sss.Format(L"%d", kvar[j][0]);	LvItem.iSubItem = 1;
			_tcscpy(buf, sss);
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);
			CString arch = L"?";
			if (kpiarch[j] == 32) arch = L"x86";
			else if (kpiarch[j] == 64) arch = L"x64";
			_tcscpy(buf, arch); LvItem.iSubItem = 2;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);
			_tcscpy(buf, s);	LvItem.iSubItem = 3;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);
			// 各行のチェックは runtime で確定済みの kpichk[] を反映(独立トグル)
			m_lc.SetCheck(j, kpichk[j] ? TRUE : FALSE);
		}
		catch (...) {
			break;
		}
	}
	free(buf);
	RECT r;
	GetWindowRect(&r);
	r.top += 600;
	r.bottom += 600;
	MoveWindow(&r);
}

void CKpilist::Save()
{
	int n = kpicnt;
	if (n > 200) n = 200;

	for (int i = 0; i < n; i++) {
		// status==0(ダイアログ)はリストのチェックを、status==1(起動時)は
		// Init() で確定済みの kpichk[] をそのまま採用する。
		if (status == 0)
			kpichk[i] = m_lc.GetCheck(i);

		savedata.kpiChkState[i] = kpichk[i] ? 1 : 0;
		CString bn = KpiBaseName(kpif[i]);
		_tcsncpy(savedata.kpiChkName[i], bn, 63);
		savedata.kpiChkName[i][63] = 0;
	}
	savedata.kpiChkCnt = n;

	// チェック変更を即座に .dat へ反映(アプリ終了を待たずに永続化)
	KpiPersistSavedata();
}

void CKpilist::OnLvnItemchangedList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	*pResult = 0;
}

BOOL CKpilist::PreTranslateMessage(MSG* pMsg)
{
	if (m_lc.GetSafeHwnd() && m_lc.PreTranslateMessage(pMsg))
		return TRUE;
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CKpilist::OnBnClickedOk()
{
	// ダイアログのチェック変更を savedata へ保存してから閉じる
	Save();
	CCustomBlurDialogBase::OnOK();
}

