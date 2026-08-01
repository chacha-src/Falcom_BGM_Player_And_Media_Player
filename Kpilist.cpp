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
	DDX_Control(pDX, IDC_KPI_EXTFILTER, m_extFilter);
	DDX_Control(pDX, IDC_KPI_EXTFILTER_L, m_extFilterLbl);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CKpilist, CCustomBlurDialogBase)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST1, &CKpilist::OnLvnItemchangedList1)
	ON_EN_CHANGE(IDC_KPI_EXTFILTER, &CKpilist::OnEnChangeExtFilter)
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
	if (row < 0 || row >= GetItemCount())
		return;
	// 表示行 ≠ KPI 配列 index(拡張子フィルタ時)。ItemData が実 index。
	const int kpiIdx = (int)GetItemData(row);
	if (kpiIdx < 0 || kpiIdx >= kpicnt)
		return;

	CString exts;
	for (int i = 0; ; ++i)
	{
		if (ext[kpiIdx][i].IsEmpty())
			break;
		if (!exts.IsEmpty())
			exts += L'/';
		exts += ext[kpiIdx][i];
	}

	CString ver;
	ver.Format(L"%u", (unsigned)kvar[kpiIdx][0]);
	CString arch = L"?";
	if (kpiarch[kpiIdx] == 32)
		arch = L"x86";
	else if (kpiarch[kpiIdx] == 64)
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
		(LPCTSTR)kpif[kpiIdx], (LPCTSTR)ver, (LPCTSTR)arch, (LPCTSTR)exts);
}

BOOL CKpilist::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"kpi一覧", L"kpi list", L"Liste kpi", L"Elenco kpi", L"Lista kpi", L"kpi 목록", L"kpi 列表", L"قائمة kpi", L"Список kpi", L"kpi-Liste", L"Lista kpi", L"kpi-lijst", L"Lista kpi", L"kpi listesi"));
	if (m_extFilterLbl.GetSafeHwnd())
		m_extFilterLbl.SetWindowText(LL14(L"拡張子", L"Ext", L"Ext", L"Est", L"Ext", L"확장자", L"扩展名", L"امتداد", L"Расш", L"Erw", L"Ext", L"Ext", L"Rozsz", L"Uzantı"));
	CCustomControlUtility::BeginDialogToolTip(m_tooltip, this);
	m_tooltip.AddTool(&m_okdummy, LL14(L"閉じます", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	if (m_extFilter.GetSafeHwnd()) {
		m_tooltip.AddTool(&m_extFilter, LL14(
			L"拡張子で一覧を絞り込みます。入力すると即座に反映。空欄で全表示。. の有無は問いません。複数は空白や , / で区切れます。",
			L"Filter the list by extension. Updates as you type. Empty shows all. Dot optional. Separate multiple with space, comma or /.",
			L"Filtrer par extension. Mise a jour pendant la saisie.",
			L"Filtra per estensione. Aggiorna mentre digiti.",
			L"Filtrar por extension. Se actualiza al escribir.",
			L"확장자로 목록을 필터. 입력 즉시 반영. 비우면 전체.",
			L"按扩展名筛选列表。输入即更新。空则显示全部。",
			L"تصفية حسب الامتداد. يتحدث أثناء الكتابة.",
			L"Фильтр по расширению. Обновляется при вводе.",
			L"Nach Erweiterung filtern. Sofort bei Eingabe.",
			L"Filtrar por extensao. Atualiza ao digitar.",
			L"Filter op extensie. Direct bij typen.",
			L"Filtruj po rozszerzeniu. Od razu przy wpisywaniu.",
			L"Uzantiya gore filtrele. Yazarken aninda guncellenir."));
	}
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

// リサイズ時に子コントロール(説明/フィルタ/リスト/OK)を再配置する。
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
	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);

	const int mx = PX(7 * dx);              // 左右マージン
	const int bw = PX(50 * dx);             // OK ボタン幅
	const int bh = PX(14 * dy);             // OK ボタン高さ
	const int descTop = PX(11 * dy) + capH;
	const int descH = PX(9 * dy);
	const int filtTop = PX(24 * dy) + capH;
	const int filtH = PX(14 * dy);
	const int filtLblW = PX(36 * dx);
	const int listTop = PX(43 * dy) + capH;

	// OK ボタン: 下端中央
	const int by = cy - PX(7 * dy) - bh;
	if (m_okdummy.GetSafeHwnd())
		m_okdummy.MoveWindow((cx - bw) / 2, by, bw, bh);

	// 説明テキスト: 上部いっぱい
	if (m_desc.GetSafeHwnd())
		m_desc.MoveWindow(mx, descTop, (std::max)(0, cx - 2 * mx), descH);

	// 拡張子フィルタ: 説明の下
	const int filtEditX = mx + filtLblW + PX(3 * dx);
	const int filtEditW = (std::max)(0, cx - filtEditX - mx);
	if (m_extFilterLbl.GetSafeHwnd())
		m_extFilterLbl.MoveWindow(mx, filtTop + PX(2 * dy), filtLblW, PX(10 * dy));
	if (m_extFilter.GetSafeHwnd())
		m_extFilter.MoveWindow(filtEditX, filtTop, filtEditW, filtH);

	// リスト: フィルタの下〜ボタンの上
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

// 拡張子トークンの先頭 '.' を除いた比較用キー
static CString KpiExtKey(CString e)
{
	e.Trim();
	e.MakeLower();
	if (!e.IsEmpty() && e[0] == _T('.'))
		e = e.Mid(1);
	return e;
}

// フィルタ文字列に合う KPI か。空欄=全表示。空白/,/; 区切りは OR。
static bool KpiExtMatchesFilter(int kpiIdx, const CString& filterRaw)
{
	CString raw = filterRaw;
	raw.Trim();
	if (raw.IsEmpty())
		return true;

	raw.MakeLower();
	raw.Replace(_T(','), _T(' '));
	raw.Replace(_T(';'), _T(' '));
	raw.Replace(_T('/'), _T(' '));

	CStringArray tokens;
	for (int p = 0; p < raw.GetLength(); ) {
		while (p < raw.GetLength() && raw[p] == _T(' ')) ++p;
		if (p >= raw.GetLength()) break;
		const int start = p;
		while (p < raw.GetLength() && raw[p] != _T(' ')) ++p;
		CString t = KpiExtKey(raw.Mid(start, p - start));
		if (!t.IsEmpty())
			tokens.Add(t);
	}
	if (tokens.GetCount() == 0)
		return true;

	for (INT_PTR t = 0; t < tokens.GetCount(); ++t) {
		const CString& want = tokens[t];
		for (int i = 0; ; ++i) {
			if (ext[kpiIdx][i].IsEmpty())
				break;
			const CString have = KpiExtKey(ext[kpiIdx][i]);
			if (have.IsEmpty())
				continue;
			// 部分一致(例: "sp" → spc)。先頭一致も許容。
			if (have.Find(want) >= 0 || want.Find(have) >= 0)
				return true;
		}
	}
	return false;
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

// 表示中行のチェック状態を kpichk[実KPI index] へ吸い上げる。
// フィルタ再構築で行が消える前に呼ぶこと(非表示行のチェックを壊さない)。
void CKpilist::SyncChecksFromList()
{
	if (!m_lc.GetSafeHwnd()) return;
	const int n = m_lc.GetItemCount();
	for (int row = 0; row < n; ++row) {
		const int idx = (int)m_lc.GetItemData(row);
		if (idx < 0 || idx >= kpicnt || idx >= 200) continue;
		kpichk[idx] = m_lc.GetCheck(row) ? TRUE : FALSE;
	}
}

void CKpilist::FillKpiList()
{
	if (!m_lc.GetSafeHwnd()) return;

	// 再構築前に表示中のチェックを本体配列へ退避
	SyncChecksFromList();

	CString filter;
	if (m_extFilter.GetSafeHwnd())
		m_extFilter.GetWindowText(filter);

	m_bFillingList = TRUE;
	m_lc.SetRedraw(FALSE);
	m_lc.DeleteAllItems();

	TCHAR* buf = (TCHAR*)calloc(10000, sizeof(TCHAR));
	if (!buf) {
		m_lc.SetRedraw(TRUE);
		m_bFillingList = FALSE;
		return;
	}

	LV_ITEM LvItem;
	ZeroMemory(&LvItem, sizeof(LvItem));

	for (int j = 0; j < kpicnt; j++) {
		if (!KpiExtMatchesFilter(j, filter))
			continue;
		try {
			CString s;
			for (int i = 0; ; i++) {
				if (ext[j][i] == "") break;
				s += ext[j][i]; s += "/";
			}
			if (!s.IsEmpty())
				s = s.Left(s.GetLength() - 1);

			const int a2 = kpif[j].ReverseFind(L'\\');
			_tcscpy(buf, kpif[j].Right(kpif[j].GetLength() - a2 - 1));
			LvItem.pszText = buf;
			LvItem.iItem = m_lc.GetItemCount();
			LvItem.mask = LVIF_TEXT | LVIF_PARAM;
			LvItem.iSubItem = 0;
			LvItem.lParam = j; // 実 KPI index(チェック保存の正)
			LvItem.cchTextMax = (int)_tcslen(LvItem.pszText);
			const int idItem = m_lc.InsertItem(&LvItem);
			if (idItem < 0) continue;
			m_lc.SetItemData(idItem, (DWORD_PTR)j);

			LvItem.mask = LVIF_TEXT;
			LvItem.iItem = idItem;
			CString sss; sss.Format(L"%d", kvar[j][0]);
			_tcscpy(buf, sss);
			LvItem.iSubItem = 1;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);

			CString arch = L"?";
			if (kpiarch[j] == 32) arch = L"x86";
			else if (kpiarch[j] == 64) arch = L"x64";
			_tcscpy(buf, arch);
			LvItem.iSubItem = 2;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);

			_tcscpy(buf, s);
			LvItem.iSubItem = 3;
			LvItem.pszText = buf;
			m_lc.SetItem(&LvItem);

			// チェックは常に kpichk[実index] から(行番号ではない)
			m_lc.SetCheck(idItem, kpichk[j] ? TRUE : FALSE);
		}
		catch (...) {
			break;
		}
	}
	free(buf);
	m_lc.SetRedraw(TRUE);
	m_lc.Invalidate(FALSE);
	m_bFillingList = FALSE;
	LayoutKpiColumns();
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

	FillKpiList();

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

	// ダイアログ表示中は、まず表示中チェックを kpichk へ反映。
	// フィルタで隠れた行は既に Sync/ITEMCHANGED で kpichk に残っている。
	if (status == 0)
		SyncChecksFromList();

	for (int i = 0; i < n; i++) {
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
	*pResult = 0;
	if (m_bFillingList || !pNMLV) return;
	// チェックボックス(状態イメージ)の変化だけを kpichk[実index] へ即反映
	if ((pNMLV->uChanged & LVIF_STATE) == 0) return;
	const UINT oldImg = (pNMLV->uOldState & LVIS_STATEIMAGEMASK);
	const UINT newImg = (pNMLV->uNewState & LVIS_STATEIMAGEMASK);
	if (oldImg == newImg) return;
	const int row = pNMLV->iItem;
	if (row < 0) return;
	const int idx = (int)m_lc.GetItemData(row);
	if (idx < 0 || idx >= kpicnt || idx >= 200) return;
	kpichk[idx] = m_lc.GetCheck(row) ? TRUE : FALSE;
}

void CKpilist::OnEnChangeExtFilter()
{
	if (m_bFillingList) return;
	FillKpiList();
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

