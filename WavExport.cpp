// WavExport.cpp
//

#include "stdafx.h"
#include "ogg.h"
#include "oggDlg.h"
#include "PlayList.h"
#include "WavExport.h"
#include "DecodeProgress.h"
#include "ExportTagUi.h"
#include "CPromptEngine.h"
#include <ShlObj.h>

extern COggDlg* og;
extern void DoEvent();

namespace {

wchar_t WavExportMapInvalidFilenameChar(wchar_t c)
{
	switch (c) {
	case L'\\': return L'＼';
	case L'/':  return L'／';
	case L':':  return L'：';
	case L'*':  return L'＊';
	case L'?':  return L'？';
	case L'"':  return L'\xFF02';
	case L'<':  return L'＜';
	case L'>':  return L'＞';
	case L'|':  return L'｜';
	default:
		return (c < 32) ? L'_' : c;
	}
}

void WavExportTrimTrailingDotsAndSpaces(CString& s)
{
	while (s.GetLength() > 0) {
		const wchar_t c = s[s.GetLength() - 1];
		if (c == L'.' || c == L' ') s.Truncate(s.GetLength() - 1);
		else break;
	}
	if (s.IsEmpty()) s = L"_";
}

bool WavExportIsReservedDeviceName(const CString& upper)
{
	static const wchar_t* reserved[] = {
		L"CON", L"PRN", L"AUX", L"NUL",
		L"COM1", L"COM2", L"COM3", L"COM4", L"COM5", L"COM6", L"COM7", L"COM8", L"COM9",
		L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5", L"LPT6", L"LPT7", L"LPT8", L"LPT9",
		NULL
	};
	for (int i = 0; reserved[i]; ++i) {
		const int n = (int)wcslen(reserved[i]);
		if (upper.GetLength() == n && upper == reserved[i]) return true;
		if (upper.GetLength() > n && upper.Left(n) == reserved[i] && upper[n] == L'.') return true;
	}
	return false;
}

CString WavExportSanitizePathComponent(const CString& component)
{
	CString s = component;
	for (int i = 0; i < s.GetLength(); ++i)
		s.SetAt(i, WavExportMapInvalidFilenameChar(s[i]));
	WavExportTrimTrailingDotsAndSpaces(s);
	CString upper = s;
	upper.MakeUpper();
	if (WavExportIsReservedDeviceName(upper))
		s = L"_" + s;
	return s;
}

CString WavExportSanitizeFilePath(const CString& pathIn)
{
	if (pathIn.IsEmpty()) return pathIn;

	CString out;
	int i = 0;
	const int len = pathIn.GetLength();

	if (len >= 2 && pathIn[0] == L'\\' && pathIn[1] == L'\\') {
		out = L"\\\\";
		i = 2;
		int j = i;
		while (j < len && pathIn[j] != L'\\') ++j;
		if (j > i) out += WavExportSanitizePathComponent(pathIn.Mid(i, j - i));
		i = j;
	}
	else if (len >= 2 && pathIn[1] == L':') {
		out = pathIn.Left(2);
		i = 2;
	}

	if (i < len && pathIn[i] == L'\\') {
		out += L'\\';
		++i;
	}

	while (i < len) {
		int j = i;
		while (j < len && pathIn[j] != L'\\') ++j;
		CString part = pathIn.Mid(i, j - i);
		if (!part.IsEmpty())
			out += WavExportSanitizePathComponent(part);
		i = j;
		if (i < len && pathIn[i] == L'\\') {
			out += L'\\';
			++i;
		}
	}
	return out;
}

CString WavExportNormalizeOutputPath(const CString& pathIn)
{
	CString path = pathIn;
	if (path.Right(4).MakeLower() != L".wav") path += L".wav";
	return WavExportSanitizeFilePath(path);
}

CString WavExportBaseNameFromItem(const playlistdata0& item)
{
	CString name = item.name;
	if (name.IsEmpty()) {
		CString fol = item.fol;
		const int pos = fol.ReverseFind(L'\\');
		if (pos >= 0) name = fol.Mid(pos + 1);
		else name = fol;
	}
	const int dot = name.ReverseFind(L'.');
	if (dot >= 0) name = name.Left(dot);
	return name;
}

CString WavExportDefaultFolderFromPc(const playlistdata0& item)
{
	CString defPath = item.fol;
	const int pos = defPath.ReverseFind(L'\\');
	if (pos >= 0) defPath = defPath.Left(pos + 1);
	return defPath;
}

CString WavExportOutputPathForItem(const CString& folderIn, const playlistdata0& item)
{
	CString folder = folderIn;
	if (!folder.IsEmpty() && folder[folder.GetLength() - 1] != L'\\')
		folder += L'\\';
	return WavExportNormalizeOutputPath(folder + WavExportBaseNameFromItem(item) + L".wav");
}

CString WavExportDefaultOutputPath(const playlistdata0& item)
{
	return WavExportOutputPathForItem(WavExportDefaultFolderFromPc(item), item);
}

bool WavExportBrowseFolder(CWnd* owner, CString& outFolder)
{
	BROWSEINFO bi = {};
	bi.hwndOwner = owner ? owner->GetSafeHwnd() : NULL;
	bi.lpszTitle = LL14(L"出力フォルダを選択", L"Select output folder", L"Choisir le dossier de sortie", L"Scegli cartella di output",
		L"Seleccionar carpeta de salida", L"출력 폴더 선택", L"选择输出文件夹", L"اختر مجلد الإخراج",
		L"Выберите папку вывода", L"Ausgabeordner wählen", L"Selecionar pasta de saída", L"Selecteer uitvoermap",
		L"Wybierz folder wyjściowy", L"Çıktı klasörünü seç");
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
	LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
	if (!pidl) return false;
	wchar_t path[MAX_PATH] = {};
	const BOOL got = SHGetPathFromIDList(pidl, path);
	CoTaskMemFree(pidl);
	if (!got || path[0] == L'\0') return false;
	outFolder = path;
	return true;
}

class CWeHelpDlg : public CDialog
{
public:
	enum { IDD = IDD_WE_HELP };
	explicit CWeHelpDlg(CWnd* pParent = nullptr) : CDialog(IDD, pParent) {}
protected:
	virtual BOOL OnInitDialog();
	virtual void PostNcDestroy();
	virtual void OnOK();
	virtual void OnCancel();
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	DECLARE_MESSAGE_MAP()
};

static CWeHelpDlg* g_weHelpDlg = nullptr;

BEGIN_MESSAGE_MAP(CWeHelpDlg, CDialog)
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
END_MESSAGE_MAP()

BOOL CWeHelpDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	SetWindowText(LL14(
		L"WAV出力操作ガイド", L"WAV Export Guide", L"Guide d'export WAV", L"Guida esportazione WAV",
		L"Guía de exportación WAV", L"WAV 내보내기 가이드", L"WAV 导出指南", L"دليل تصدير WAV",
		L"Руководство экспорта WAV", L"WAV-Export-Anleitung", L"Guia de exportação WAV", L"WAV-exportgids",
		L"Przewodnik eksportu WAV", L"WAV dışa aktarma kılavuzu"));
	if (CWnd* pOk = GetDlgItem(IDOK))
		pOk->SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق",
			L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	return TRUE;
}

void CWeHelpDlg::OnOK() { DestroyWindow(); }
void CWeHelpDlg::OnCancel() { DestroyWindow(); }
void CWeHelpDlg::OnClose() { DestroyWindow(); }

void CWeHelpDlg::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	if (g_weHelpDlg == this)
		g_weHelpDlg = nullptr;
	delete this;
}

BOOL CWeHelpDlg::OnEraseBkgnd(CDC* pDC)
{
	CRect rc; GetClientRect(&rc);
	pDC->FillSolidRect(rc, RGB(248, 248, 252));
	return TRUE;
}

void CWeHelpDlg::OnPaint()
{
	CPaintDC dc(this);
	CRect rc; GetClientRect(&rc);
	const int footerH = 26;
	rc.bottom -= footerH;
	dc.FillSolidRect(CRect(0, 0, rc.right, rc.bottom + footerH), RGB(248, 248, 252));
	dc.SetBkMode(TRANSPARENT);
	CFont* oldFont = dc.SelectObject(GetFont());

	TEXTMETRIC tm{};
	dc.GetTextMetrics(&tm);
	const int lh = max(14, tm.tmHeight + tm.tmExternalLeading + 1);
	const int titleLh = lh + 1;
	CBrush frameBrush(RGB(130, 130, 150));

	auto title = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(55, 45, 85));
		dc.TextOut(x, y, t);
	};
	auto body = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(65, 65, 80));
		dc.TextOut(x, y, t);
	};
	auto muted = [&](int x, int y, LPCTSTR t) {
		dc.SetTextColor(RGB(100, 100, 115));
		dc.TextOut(x, y, t);
	};

	int y = 6;
	const int L = 10;
	title(L, y, LL14(L"WAV出力操作ガイド", L"WAV Export — Guide", L"Guide export WAV", L"Guida esportazione WAV",
		L"Guía exportación WAV", L"WAV 내보내기 가이드", L"WAV 导出指南", L"دليل تصدير WAV",
		L"Руководство экспорта WAV", L"WAV-Export-Guide", L"Guia exportação WAV", L"WAV-exportgids",
		L"Przewodnik eksportu WAV", L"WAV dışa aktarma kılavuzu"));
	y += titleLh;
	muted(L, y, LL14(
		L"曲をデコードして WAV に書き出します。ループ・フェード・タグもここで指定します。",
		L"Decode the track and write WAV. Set loops, fade, and tags here.",
		L"Décodez la piste en WAV. Boucles, fondu et tags ici.",
		L"Decodifica la traccia in WAV. Loop, fade e tag qui.",
		L"Decodifica la pista a WAV. Bucles, fundido y etiquetas aquí.",
		L"곡을 디코드해 WAV로 내보냅니다. 루프·페이드·태그도 여기서 지정합니다.",
		L"解码曲目并写出 WAV。在此设置循环、淡出和标签。",
		L"فكّ الترميز واكتب WAV. الحلقات والتلاشي والوسوم هنا.",
		L"Декодируйте трек в WAV. Циклы, затухание и теги здесь.",
		L"Track dekodieren und als WAV schreiben. Schleifen, Fade und Tags hier.",
		L"Decode a faixa e grave WAV. Loops, fade e tags aqui.",
		L"Decodeer het nummer naar WAV. Loops, fade en tags hier.",
		L"Dekoduj utwór do WAV. Pętle, fade i tagi tutaj.",
		L"Parçayı decode edip WAV yazın. Döngü, solma ve etiketler burada."));
	y += lh + 4;

	title(L, y, LL14(L"基本操作", L"Basics", L"Bases", L"Basi", L"Básicos", L"기본", L"基本", L"أساسيات",
		L"Основы", L"Grundlagen", L"Básicos", L"Basis", L"Podstawy", L"Temeller"));
	y += titleLh;
	body(L, y, LL14(L"・繰返し回数 …… ループ再生して書き出す回数", L"· Loop count …… how many times to loop while exporting", L"· Boucles …… nombre de répétitions à l'export", L"· Loop …… quante volte ripetere in export",
		L"· Repeticiones …… veces a repetir al exportar", L"· 반복 횟수 …… 내보내기 시 루프 횟수", L"· 循环次数 …… 导出时循环播放的次数", L"· التكرار …… مرات الحلقة عند التصدير",
		L"· Повторы …… сколько раз зациклить при экспорте", L"· Schleifen …… wie oft beim Export wiederholen", L"· Repetições …… quantas vezes repetir na exportação", L"· Herhalingen …… hoe vaak loopen bij export",
		L"· Powtórzenia …… ile razy zapętlić przy eksporcie", L"· Döngü sayısı …… dışa aktarırken kaç kez döngü")); y += lh;
	body(L, y, LL14(L"・出力パス …… 単曲はファイル名、複数選択時はフォルダ", L"· Path …… file name for one track, folder for multi-select", L"· Chemin …… fichier (1) ou dossier (plusieurs)", L"· Percorso …… file (1) o cartella (più)",
		L"· Ruta …… archivo (1) o carpeta (varios)", L"· 출력 경로 …… 단곡=파일명, 다중=폴더", L"· 输出路径 …… 单曲为文件名，多选为文件夹", L"· المسار …… ملف لواحدة أو مجلد لعدة",
		L"· Путь …… файл (1) или папка (несколько)", L"· Pfad …… Datei (1) oder Ordner (mehrere)", L"· Caminho …… arquivo (1) ou pasta (vários)", L"· Pad …… bestand (1) of map (meer)",
		L"· Ścieżka …… plik (1) lub folder (wiele)", L"· Yol …… tek parça=dosya, çoklu=klasör")); y += lh;
	body(L, y, LL14(L"・フェードアウト …… 末尾を指定秒でフェード", L"· Fade out …… fade the end over N seconds", L"· Fondu …… fondre la fin sur N sec", L"· Dissolvenza …… fade finale in N sec",
		L"· Fundido …… fundir el final en N seg", L"· 페이드 아웃 …… 끝을 N초 페이드", L"· 淡出 …… 末尾用 N 秒淡出", L"· تلاشي …… تلاشي النهاية خلال N ث",
		L"· Затухание …… затухание конца за N сек", L"· Ausblenden …… Ende über N Sek. ausblenden", L"· Fade out …… esmaecer o fim em N seg", L"· Fade-out …… einde over N sec faden",
		L"· Wyciszanie …… wycisz koniec przez N sek", L"· Solma …… sonu N sn sol")); y += lh;
	body(L, y, LL14(L"・先頭無音を揃える …… 先頭の無音を指定秒だけ残して揃える", L"· Align leading silence …… keep N sec of lead-in silence", L"· Silence initial …… garder N sec au début", L"· Silenzio iniziale …… lascia N sec all'inizio",
		L"· Silencio inicial …… dejar N seg al inicio", L"· 앞 무음 맞추기 …… 앞 무음을 N초만 남김", L"· 对齐开头静音 …… 开头静音保留 N 秒", L"· صمت ابتدائي …… الإبقاء على N ث في البداية",
		L"· Нач. тишина …… оставить N сек в начале", L"· Anfangsstille …… N Sek. am Anfang behalten", L"· Silêncio inicial …… manter N seg no início", L"· Beginstilte …… N sec stilte vooraan houden",
		L"· Cisza na początku …… zostaw N sek na początku", L"· Baştaki sessizlik …… başta N sn bırak")); y += lh + 4;

	title(L, y, LL14(L"タグ / ジャケット", L"Tags / Cover", L"Tags / Pochette", L"Tag / Copertina", L"Etiquetas / Portada", L"태그 / 재킷", L"标签 / 封面", L"الوسوم / الغلاف",
		L"Теги / Обложка", L"Tags / Cover", L"Tags / Capa", L"Tags / Cover", L"Tagi / Okładka", L"Etiketler / Kapak"));
	y += titleLh;
	body(L, y, LL14(L"・タグとジャケットをコピー …… 元曲のタグ／カバーを引き継ぐ", L"· Copy tags and cover …… carry over source tags/cover", L"· Copier tags/pochette …… depuis la source", L"· Copia tag/copertina …… dalla sorgente",
		L"· Copiar etiquetas/portada …… desde el origen", L"· 태그·재킷 복사 …… 원곡에서 이어받음", L"· 复制标签和封面 …… 从源曲继承", L"· نسخ الوسوم/الغلاف …… من المصدر",
		L"· Копировать теги/обложку …… из источника", L"· Tags/Cover kopieren …… von der Quelle", L"· Copiar tags/capa …… da origem", L"· Tags/hoes kopiëren …… van de bron",
		L"· Kopiuj tagi/okładkę …… ze źródła", L"· Etiket/kapak kopyala …… kaynaktan")); y += lh;
	body(L, y, LL14(L"・プロンプト実行を適用 …… 出力時にプロンプト効果を適用", L"· Apply prompt …… apply prompt effects on export", L"· Appliquer le prompt …… effets à l'export", L"· Applica prompt …… effetti in export",
		L"· Aplicar prompt …… efectos al exportar", L"· 프롬프트 적용 …… 내보내기 시 프롬프트 효과", L"· 应用提示 …… 导出时应用提示效果", L"· تطبيق البرومبت …… تأثيرات عند التصدير",
		L"· Применить промпт …… эффекты при экспорте", L"· Prompt anwenden …… Effekte beim Export", L"· Aplicar prompt …… efeitos na exportação", L"· Prompt toepassen …… effecten bij export",
		L"· Zastosuj prompt …… efekty przy eksporcie", L"· Prompt uygula …… dışa aktarmada efekt")); y += lh;
	body(L, y, LL14(L"・ジャケット …… JPG/PNG をドロップ、または解除でクリア", L"· Cover …… drop JPG/PNG, or Clear to remove", L"· Pochette …… déposer JPG/PNG, ou Effacer", L"· Copertina …… trascina JPG/PNG, o Cancella",
		L"· Portada …… soltar JPG/PNG, o Borrar", L"· 재킷 …… JPG/PNG 드롭, 또는 해제로 지움", L"· 封面 …… 拖入 JPG/PNG，或点解除清除", L"· الغلاف …… أسقط JPG/PNG أو امسح",
		L"· Обложка …… перетащите JPG/PNG или очистите", L"· Cover …… JPG/PNG ablegen oder Löschen", L"· Capa …… solte JPG/PNG ou Limpar", L"· Cover …… JPG/PNG neerzetten of Wissen",
		L"· Okładka …… upuść JPG/PNG lub Wyczyść", L"· Kapak …… JPG/PNG bırak veya Temizle")); y += lh + 4;

	const int gx = L, gy = y, gw = min(320, rc.Width() - L * 2), gh = lh * 2 + 12;
	dc.FillSolidRect(gx, gy, gw, gh, RGB(245, 246, 250));
	dc.FillSolidRect(gx + 4, gy + 6, 44, gh - 12, RGB(70, 140, 90));
	dc.FillSolidRect(gx + 56, gy + 6, 44, gh - 12, RGB(180, 140, 60));
	dc.FillSolidRect(gx + 108, gy + 6, 44, gh - 12, RGB(70, 110, 160));
	dc.FillSolidRect(gx + 160, gy + 6, 50, gh - 12, RGB(150, 70, 70));
	dc.SetTextColor(RGB(255, 255, 255));
	dc.TextOut(gx + 10, gy + 8, L"Loop");
	dc.TextOut(gx + 62, gy + 8, L"Fade");
	dc.TextOut(gx + 114, gy + 8, L"Tags");
	dc.TextOut(gx + 168, gy + 8, L"WAV");
	dc.FrameRect(CRect(gx, gy, gx + gw, gy + gh), &frameBrush);
	y = gy + gh + 4;

	title(L, y, LL14(L"実行", L"Execute", L"Exécuter", L"Esegui", L"Ejecutar", L"실행", L"执行", L"تنفيذ",
		L"Выполнить", L"Ausführen", L"Executar", L"Uitvoeren", L"Wykonaj", L"Çalıştır"));
	y += titleLh;
	body(L, y, LL14(L"・実行 …… デコードと書き出しを開始。進捗バーで進行を確認", L"· Execute …… start decode/write. Watch the progress bar", L"· Exécuter …… démarre. Suivre la barre de progression", L"· Esegui …… avvia. Controlla la barra di avanzamento",
		L"· Ejecutar …… inicia. Mire la barra de progreso", L"· 실행 …… 디코드·쓰기 시작. 진행 바로 확인", L"· 执行 …… 开始解码与写出。用进度条查看", L"· تنفيذ …… يبدأ. راقب شريط التقدم",
		L"· Выполнить …… старт. Смотрите прогресс", L"· Ausführen …… startet. Fortschrittsbalken beobachten", L"· Executar …… inicia. Veja a barra de progresso", L"· Uitvoeren …… start. Volg de voortgangsbalk",
		L"· Wykonaj …… start. Pilnuj paska postępu", L"· Çalıştır …… decode/yazmayı başlat. İlerleme çubuğuna bak")); y += lh;
	muted(L, y, LL14(
		L"閉じるでキャンセルできます（書き出し中は完了を待ってください）。",
		L"Close cancels when idle (wait if export is running).",
		L"Fermer annule au repos (attendez si l'export tourne).",
		L"Chiudi annulla a riposo (attendi se l'export è in corso).",
		L"Cerrar cancela en reposo (espere si exporta).",
		L"닫기는 대기 중 취소(내보내기 중이면 완료를 기다리세요).",
		L"空闲时可关闭取消（导出进行中请等待完成）。",
		L"الإغلاق يلغي عند الخمول (انتظر إن كان التصدير جارياً).",
		L"Закрыть отменяет в простое (дождитесь при экспорте).",
		L"Schliessen bricht im Leerlauf ab (bei Export warten).",
		L"Fechar cancela em espera (aguarde se exportar).",
		L"Sluiten annuleert in rust (wacht bij export).",
		L"Zamknij anuluje w bezczynności (poczekaj przy eksporcie).",
		L"Kapat boştayken iptal eder (dışa aktarma sürüyorsa bekleyin)."));

	dc.SelectObject(oldFont);
}

} // namespace

IMPLEMENT_DYNAMIC(CWavExport, CCustomBlurDialogBase)

CWavExport::CWavExport(CWnd* pParent)
	: CCustomBlurDialogBase(CWavExport::IDD, pParent)
	, multiFile(false)
	, m_coverBmp(NULL)
{
}

CWavExport::~CWavExport()
{
	if (m_coverBmp) {
		::DeleteObject(m_coverBmp);
		m_coverBmp = NULL;
	}
}

void CWavExport::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_WE_HELP, m_help);
	DDX_Control(pDX, IDC_WAVEXPORT_LOOP, m_loop);
	DDX_Control(pDX, IDC_WAVEXPORT_PATH, m_path);
	DDX_Control(pDX, IDC_WAVEXPORT_STATUS, m_status);
	DDX_Control(pDX, IDC_WAVEXPORT_LOOP_LABEL, m_loopLabel);
	DDX_Control(pDX, IDC_WAVEXPORT_PATH_LABEL, m_pathLabel);
	DDX_Control(pDX, IDC_WAVEXPORT_BROWSE, m_browse);
	DDX_Control(pDX, IDC_WAVEXPORT_EXEC, m_exec);
	DDX_Control(pDX, IDC_WAVEXPORT_CLOSE, m_close);
	DDX_Control(pDX, IDC_WAVEXPORT_FADE, m_fadeCheck);
	DDX_Control(pDX, IDC_WAVEXPORT_FADE_SEC, m_fadeSec);
	DDX_Control(pDX, IDC_WAVEXPORT_FADE_LABEL, m_fadeLabel);
	DDX_Control(pDX, IDC_WAVEXPORT_TRIM, m_trimCheck);
	DDX_Control(pDX, IDC_WAVEXPORT_TRIM_SEC, m_trimSec);
	DDX_Control(pDX, IDC_WAVEXPORT_TRIM_LABEL, m_trimLabel);
	DDX_Control(pDX, IDC_WAVEXPORT_COPY_TAGS, m_copyTags);
	DDX_Control(pDX, IDC_WAVEXPORT_PROMPT, m_promptCheck);
	DDX_Control(pDX, IDC_WAVEXPORT_TITLE_L, m_titleL);
	DDX_Control(pDX, IDC_WAVEXPORT_TITLE, m_title);
	DDX_Control(pDX, IDC_WAVEXPORT_ARTIST_L, m_artistL);
	DDX_Control(pDX, IDC_WAVEXPORT_ARTIST, m_artist);
	DDX_Control(pDX, IDC_WAVEXPORT_ALBUM_L, m_albumL);
	DDX_Control(pDX, IDC_WAVEXPORT_ALBUM, m_album);
	DDX_Control(pDX, IDC_WAVEXPORT_COVER_L, m_coverL);
	DDX_Control(pDX, IDC_WAVEXPORT_COVER_PIC, m_coverPic);
	DDX_Control(pDX, IDC_WAVEXPORT_COVER, m_cover);
	DDX_Control(pDX, IDC_WAVEXPORT_COVER_CLEAR, m_coverClear);
}

BEGIN_MESSAGE_MAP(CWavExport, CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_WAVEXPORT_EXEC, &CWavExport::OnBnClickedWavExportExec)
	ON_BN_CLICKED(IDC_WAVEXPORT_BROWSE, &CWavExport::OnBnClickedWavExportBrowse)
	ON_BN_CLICKED(IDC_WAVEXPORT_CLOSE, &CWavExport::OnBnClickedWavExportClose)
	ON_BN_CLICKED(IDC_WAVEXPORT_COVER_CLEAR, &CWavExport::OnBnClickedCoverClear)
	ON_BN_CLICKED(IDC_WE_HELP, &CWavExport::OnBnClickedHelp)
	ON_WM_DROPFILES()
	ON_WM_SIZE()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

BOOL CWavExport::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	LayoutHelpBtn();
	SetWindowText(LL14(L"WAVへ出力", L"Export to WAV", L"Exporter en WAV", L"Esporta in WAV",
		L"Exportar a WAV", L"WAV로 내보내기", L"导出到WAV", L"تصدير إلى WAV",
		L"Экспорт в WAV", L"Als WAV exportieren", L"Exportar para WAV", L"Exporteren naar WAV",
		L"Eksportuj do WAV", L"WAV'e aktar"));
	m_loopLabel.SetWindowText(LL14(L"繰返し回数", L"Loop count", L"Nombre de boucles", L"Conteggio loop",
		L"Repeticiones", L"반복 횟수", L"循环次数", L"عدد التكرار",
		L"Количество повторов", L"Schleifenzahl", L"Repetições", L"Aantal herhalingen",
		L"Liczba powtórzeń", L"Döngü sayısı"));
	if (multiFile) {
		m_pathLabel.SetWindowText(LL14(L"出力フォルダ", L"Output folder", L"Dossier de sortie", L"Cartella di output",
			L"Carpeta de salida", L"출력 폴더", L"输出文件夹", L"مجلد الإخراج",
			L"Папка вывода", L"Ausgabeordner", L"Pasta de saída", L"Uitvoermap",
			L"Folder wyjściowy", L"Çıktı klasörü"));
	}
	else {
		m_pathLabel.SetWindowText(LL14(L"出力ファイル名", L"Output file", L"Fichier de sortie", L"File di output",
			L"Archivo de salida", L"출력 파일", L"输出文件名", L"اسم الملف",
			L"Выходной файл", L"Ausgabedatei", L"Arquivo de saída", L"Uitvoerbestand",
			L"Plik wyjściowy", L"Çıktı dosyası"));
	}
	m_fadeCheck.SetWindowText(LL14(L"フェードアウト", L"Fade out", L"Fondu", L"Dissolvenza",
		L"Fundido", L"페이드 아웃", L"淡出", L"تلاشي",
		L"Затухание", L"Ausblenden", L"Fade out", L"Fade-out",
		L"Wyciszanie", L"Solma"));
	m_fadeLabel.SetWindowText(LL14(L"秒", L"sec", L"sec", L"sec",
		L"seg", L"초", L"秒", L"ث",
		L"сек", L"Sek", L"seg", L"sec",
		L"sek", L"sn"));
	m_trimCheck.SetWindowText(LL14(L"先頭無音を揃える", L"Align leading silence", L"Aligner silence initial", L"Allinea silenzio iniziale",
		L"Alinear silencio inicial", L"앞 무음 맞추기", L"对齐开头静音", L"مواءمة الصمت الابتدائي",
		L"Выровнять нач. тишину", L"Anfangsstille angleichen", L"Alinhar silencio inicial", L"Beginstilte uitlijnen",
		L"Wyrównaj ciszę na początku", L"Bastaki sessizligi hizala"));
	m_trimLabel.SetWindowText(LL14(L"秒", L"sec", L"sec", L"sec",
		L"seg", L"초", L"秒", L"ث",
		L"сек", L"Sek", L"seg", L"sec",
		L"sek", L"sn"));
	m_copyTags.SetWindowText(LL14(L"タグとジャケットをコピー", L"Copy tags and cover art", L"Copier les tags et la pochette", L"Copia tag e copertina",
		L"Copiar etiquetas y portada", L"태그와 재킷 복사", L"复制标签和封面", L"نسخ الوسوم والغلاف",
		L"Копировать теги и обложку", L"Tags und Cover kopieren", L"Copiar tags e capa", L"Tags en hoes kopiëren",
		L"Kopiuj tagi i okładkę", L"Etiketleri ve kapağı kopyala"));
	m_promptCheck.SetWindowText(LL14(L"プロンプト実行を適用", L"Apply prompt execution", L"Appliquer le prompt", L"Applica esecuzione prompt",
		L"Aplicar ejecucion del prompt", L"프롬프트 실행 적용", L"应用提示执行", L"Apply prompt",
		L"Применить промпт", L"Prompt anwenden", L"Aplicar prompt", L"Prompt toepassen",
		L"Zastosuj prompt", L"Prompt uygula"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi",
		L"Cerrar", L"닫기", L"关闭", L"إغلاق",
		L"Закрыть", L"Schließen", L"Fechar", L"Sluiten",
		L"Zamknij", L"Kapat"));
	m_exec.SetWindowText(LL14(L"実行", L"Execute", L"Exécuter", L"Esegui",
		L"Ejecutar", L"실행", L"执行", L"تنفيذ",
		L"Выполнить", L"Ausführen", L"Executar", L"Uitvoeren",
		L"Wykonaj", L"Çalıştır"));
	m_loop.SetWindowText(L"1");
	int fadeSec = savedata.wav_export_fade_sec;
	if (fadeSec <= 0) fadeSec = 15;
	int trimKeep = savedata.wav_export_trim_keep_sec;
	if (trimKeep <= 0) trimKeep = 1;
	CString s;
	s.Format(L"%d", fadeSec);
	m_fadeSec.SetWindowText(s);
	s.Format(L"%d", trimKeep);
	m_trimSec.SetWindowText(s);
	m_fadeCheck.SetCheck(savedata.wav_export_fade ? BST_CHECKED : BST_UNCHECKED);
	m_trimCheck.SetCheck(savedata.wav_export_trim_lead ? BST_CHECKED : BST_UNCHECKED);
	m_copyTags.SetCheck(savedata.wav_export_copy_tags ? BST_CHECKED : BST_UNCHECKED);
	m_promptCheck.SetCheck((savedata.wav_export_apply_prompt || MpPromptIsActive()) ? BST_CHECKED : BST_UNCHECKED);
	if (multiFile) {
		m_path.SetWindowText(WavExportDefaultFolderFromPc(pc));
	}
	else {
		m_path.SetWindowText(WavExportDefaultOutputPath(pc));
	}
	m_status.SetWindowText(L"");
	if (CWnd* pPh = GetDlgItem(IDC_WAVEXPORT_PROGRESS)) {
		CRect rc; pPh->GetWindowRect(&rc); ScreenToClient(&rc);
		pPh->DestroyWindow();
		m_progress.Create(WS_CHILD | WS_VISIBLE, rc, this, IDC_WAVEXPORT_PROGRESS);
		m_progress.SetRange(0, 100);
		m_progress.SetPos(0);
		m_progress.SetShowPercent(TRUE);
		m_progress.SetColors(RGB(255, 236, 246), RGB(255, 170, 200), RGB(200, 120, 220));
		m_progress.SetAeroMode(CCC_IsAeroEnabled());
	}
	if (CWnd* pProgL = GetDlgItem(IDC_WAVEXPORT_PROG_L))
		pProgL->SetWindowText(LL14(L"進捗", L"Progress", L"Progression", L"Avanzamento", L"Progreso", L"진행", L"进度", L"Progress", L"Прогресс", L"Fortschritt", L"Progresso", L"Voortgang", L"Postep", L"Ilerleme"));
	ExportTagUi_InitFields(multiFile, pc, m_title, m_artist, m_album,
		m_titleL, m_artistL, m_albumL, m_coverL, m_coverPic, m_cover, m_coverClear, m_coverPath, m_coverBmp);
	DragAcceptFiles(TRUE);

	// チェック／スタティック幅＋秒欄縦中央。右列は左列実幅の右から
	{
		UINT dpi = 96;
		if (HDC hdcDpi = ::GetDC(GetSafeHwnd())) {
			dpi = (UINT)GetDeviceCaps(hdcDpi, LOGPIXELSX);
			::ReleaseDC(GetSafeHwnd(), hdcDpi);
			if (dpi == 0) dpi = 96;
		}
		auto scale = [dpi](int v96) -> int { return MulDiv(v96, (int)dpi, 96); };
		auto heightOf = [this](CWnd& w) -> int {
			if (!w.GetSafeHwnd()) return 18;
			CRect r; w.GetWindowRect(&r); return r.Height();
		};
		auto textW = [this](CWnd& w) -> int {
			if (!w.GetSafeHwnd()) return 0;
			CString t; w.GetWindowText(t);
			if (t.IsEmpty()) t = L"W";
			int cx = 0;
			if (CDC* pDc = GetDC()) {
				CFont* pFont = w.GetFont();
				if (!pFont) pFont = GetFont();
				CFont* pOld = pDc->SelectObject(pFont);
				cx = pDc->GetTextExtent(t).cx;
				if (pOld) pDc->SelectObject(pOld);
				ReleaseDC(pDc);
			}
			return cx;
		};
		auto fitCheck = [&](CWnd& chk) -> int {
			int w = textW(chk) + scale(18) + scale(8) + scale(16);
			if (w < scale(72)) w = scale(72);
			return w;
		};
		auto fitStatic = [&](CWnd& lab) -> int {
			int w = textW(lab) + scale(6);
			if (w < scale(24)) w = scale(24);
			return w;
		};
		auto placeSized = [this](CWnd& w, int x, int y, int width, int height) {
			if (!w.GetSafeHwnd()) return;
			w.SetWindowPos(NULL, x, y, width, height, SWP_NOZORDER);
		};
		auto placeCheckSecAt = [&](CWnd& chk, CWnd& sec, CWnd& lab, int x, int yTop, int* outH) -> int {
			const int chkW = fitCheck(chk);
			const int labW = fitStatic(lab);
			CRect rSec; sec.GetWindowRect(&rSec);
			const int secW = rSec.Width() > 0 ? rSec.Width() : scale(40);
			const int secH = rSec.Height() > 0 ? rSec.Height() : scale(22);
			const int chkH = (std::max)(heightOf(chk), scale(18));
			const int labH = (std::max)(heightOf(lab), scale(14));
			const int rowH = (std::max)(chkH, (std::max)(secH, labH));
			placeSized(chk, x, yTop + (rowH - chkH) / 2, chkW, chkH);
			const int secX = x + chkW + scale(6);
			placeSized(sec, secX, yTop + (rowH - secH) / 2, secW, secH);
			placeSized(lab, secX + secW + scale(4), yTop + (rowH - labH) / 2, labW, labH);
			if (outH) *outH = rowH;
			return secX + secW + scale(4) + labW;
		};

		CRect rcClient; GetClientRect(&rcClient);
		const int marginL = scale(7);
		const int clientRight = rcClient.right - scale(7);
		CRect rFade; m_fadeCheck.GetWindowRect(&rFade); ScreenToClient(&rFade);
		CRect rTrim; m_trimCheck.GetWindowRect(&rTrim); ScreenToClient(&rTrim);
		const int fadeY = rFade.top;
		const int trimY = rTrim.top;

		auto leftW = [&](CWnd& chk, CWnd& sec, CWnd& lab) {
			CRect rs; sec.GetWindowRect(&rs);
			const int sw = rs.Width() > 0 ? rs.Width() : scale(40);
			return fitCheck(chk) + scale(6) + sw + scale(4) + fitStatic(lab);
		};
		int rightColX = marginL + (std::max)(leftW(m_fadeCheck, m_fadeSec, m_fadeLabel), leftW(m_trimCheck, m_trimSec, m_trimLabel)) + scale(16);

		int fadeH = 0;
		placeCheckSecAt(m_fadeCheck, m_fadeSec, m_fadeLabel, marginL, fadeY, &fadeH);
		int trimH = 0;
		const int trimEnd = placeCheckSecAt(m_trimCheck, m_trimSec, m_trimLabel, marginL, trimY, &trimH);
		if (m_copyTags.GetSafeHwnd()) {
			const int copyW = fitCheck(m_copyTags);
			const int copyH = (std::max)(heightOf(m_copyTags), scale(18));
			int copyX = (std::max)(rightColX, trimEnd + scale(12));
			if (copyX + copyW > clientRight)
				copyX = (std::max)(trimEnd + scale(8), clientRight - copyW);
			placeSized(m_copyTags, copyX, trimY + (trimH - copyH) / 2, copyW, copyH);
		}
		if (m_promptCheck.GetSafeHwnd()) {
			CRect rP; m_promptCheck.GetWindowRect(&rP); ScreenToClient(&rP);
			placeSized(m_promptCheck, marginL, rP.top, fitCheck(m_promptCheck), (std::max)(rP.Height(), scale(18)));
		}
	}
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_help, LL14(L"操作ガイドを表示", L"Show operation guide", L"Afficher le guide", L"Mostra guida", L"Mostrar guía", L"조작 가이드 표시", L"显示操作指南", L"إظهار الدليل", L"Показать руководство", L"Bedienungsanleitung", L"Mostrar guia", L"Handleiding tonen", L"Pokaż przewodnik", L"İşlem kılavuzunu göster"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 10000);
	}
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	return TRUE;
}

void CWavExport::LayoutHelpBtn()
{
	CCC_CaptionPlaceHelpBtn(m_hWnd, &m_help);
}

void CWavExport::ShowHelpSheet()
{
	if (g_weHelpDlg && ::IsWindow(g_weHelpDlg->GetSafeHwnd())) {
		CCC_PresentOwnedHelp(g_weHelpDlg, this);
		return;
	}
	if (g_weHelpDlg && !::IsWindow(g_weHelpDlg->GetSafeHwnd()))
		g_weHelpDlg = nullptr;
	CWeHelpDlg* dlg = new CWeHelpDlg(this);
	if (!dlg->Create(IDD_WE_HELP, this)) {
		delete dlg;
		return;
	}
	g_weHelpDlg = dlg;
	CCC_PresentOwnedHelp(dlg, this);
}

void CWavExport::OnBnClickedHelp()
{
	ShowHelpSheet();
}

void CWavExport::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogBase::OnSize(nType, cx, cy);
	if (nType != SIZE_MINIMIZED) {
		CCC_CaptionLayout(m_hWnd);
		LayoutHelpBtn();
	}
}

void CWavExport::OnDestroy()
{
	if (g_weHelpDlg && ::IsWindow(g_weHelpDlg->GetSafeHwnd()))
		g_weHelpDlg->DestroyWindow();
	CCustomBlurDialogBase::OnDestroy();
}

BOOL CWavExport::PreTranslateMessage(MSG* pMsg)
{
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}

void CWavExport::OnBnClickedCoverClear()
{
	ExportTagUi_ClearCover(m_coverPic, m_cover, m_coverPath, m_coverBmp);
}

void CWavExport::OnDropFiles(HDROP hDropInfo)
{
	ExportTagUi_OnDropFiles(hDropInfo, m_coverPic, m_cover, m_coverPath, m_coverBmp);
}

void CWavExport::ExportProgressThunk(int percent, LPCTSTR status, void* user)
{
	CWavExport* self = reinterpret_cast<CWavExport*>(user);
	if (!self || !::IsWindow(self->GetSafeHwnd())) return;
	if (self->m_progress.GetSafeHwnd()) {
		self->m_progress.SetPos(percent);
		self->m_progress.Invalidate(FALSE);
		self->m_progress.UpdateWindow();
	}
	if (status && status[0])
		self->m_status.SetWindowText(status);
	else if (CWnd* p = self->GetDlgItem(IDC_WAVEXPORT_PROG_L)) {
		CString s;
		s.Format(L"%d%%", percent);
		p->SetWindowText(s);
	}
	// 全メッセージを汲み出すと timer/Restart 再入で export が壊れる。描画だけ通す。
	MSG msg;
	while (::PeekMessage(&msg, self->GetSafeHwnd(), WM_PAINT, WM_PAINT, PM_REMOVE))
		::DispatchMessage(&msg);
}

void CWavExport::OnBnClickedWavExportBrowse()
{
	CString path;
	m_path.GetWindowText(path);
	if (multiFile) {
		if (WavExportBrowseFolder(this, path))
			m_path.SetWindowText(path);
		return;
	}
	CFileDialog fd(FALSE, L"wav", path, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
		LL14(L"WAVファイル (*.wav)|*.wav|すべてのファイル (*.*)|*.*||",
		L"WAV files (*.wav)|*.wav|All files (*.*)|*.*||",
		L"Fichiers WAV (*.wav)|*.wav|Tous les fichiers (*.*)|*.*||",
		L"File WAV (*.wav)|*.wav|Tutti i file (*.*)|*.*||",
		L"Archivos WAV (*.wav)|*.wav|Todos los archivos (*.*)|*.*||",
		L"WAV 파일 (*.wav)|*.wav|모든 파일 (*.*)|*.*||",
		L"WAV文件 (*.wav)|*.wav|所有文件 (*.*)|*.*||",
		L"ملفات WAV (*.wav)|*.wav|جميع الملفات (*.*)|*.*||",
		L"Файлы WAV (*.wav)|*.wav|Все файлы (*.*)|*.*||",
		L"WAV-Dateien (*.wav)|*.wav|Alle Dateien (*.*)|*.*||",
		L"Arquivos WAV (*.wav)|*.wav|Todos os arquivos (*.*)|*.*||",
		L"WAV-bestanden (*.wav)|*.wav|Alle bestanden (*.*)|*.*||",
		L"Pliki WAV (*.wav)|*.wav|Wszystkie pliki (*.*)|*.*||",
		L"WAV dosyalari (*.wav)|*.wav|Tum dosyalar (*.*)|*.*||"));
	if (fd.DoModal() == IDOK) {
		m_path.SetWindowText(fd.GetPathName());
	}
}

void CWavExport::OnBnClickedWavExportExec()
{
	CString loopStr, pathStr, fadeStr, trimStr;
	m_loop.GetWindowText(loopStr);
	m_path.GetWindowText(pathStr);
	m_fadeSec.GetWindowText(fadeStr);
	m_trimSec.GetWindowText(trimStr);
	int loopCount = _tstoi(loopStr);
	if (loopCount < 1) loopCount = 1;
	int fadeSec = _tstoi(fadeStr);
	if (fadeSec < 1) fadeSec = 15;
	int trimKeepSec = _tstoi(trimStr);
	if (trimKeepSec < 0) trimKeepSec = 1;

	WavExportOptions opts = {};
	opts.fadeEnable = m_fadeCheck.GetCheck() ? 1 : 0;
	opts.fadeSec = fadeSec;
	opts.trimLeadEnable = m_trimCheck.GetCheck() ? 1 : 0;
	opts.trimKeepSec = trimKeepSec;
	opts.applyPrompt = m_promptCheck.GetCheck() ? 1 : 0;
	savedata.wav_export_fade = opts.fadeEnable;
	savedata.wav_export_fade_sec = opts.fadeSec;
	savedata.wav_export_trim_lead = opts.trimLeadEnable;
	savedata.wav_export_trim_keep_sec = opts.trimKeepSec;
	savedata.wav_export_copy_tags = m_copyTags.GetCheck() ? 1 : 0;
	savedata.wav_export_apply_prompt = opts.applyPrompt;
	ExportTagUi_Collect(multiFile, savedata.wav_export_copy_tags, m_title, m_artist, m_album, m_coverPath, opts);

	if (pathStr.IsEmpty()) {
		m_status.SetWindowText(multiFile
			? LL14(L"フォルダを指定してください", L"Please specify folder", L"Veuillez specifier le dossier", L"Specificare la cartella",
				L"Especifique la carpeta", L"폴더를 지정하세요", L"请指定文件夹", L"يرجى تحديد المجلد",
				L"Укажите папку", L"Bitte Ordner angeben", L"Especifique a pasta", L"Geef map op",
				L"Podaj folder", L"Klasor belirtin")
			: LL14(L"ファイル名を指定してください", L"Please specify file name", L"Veuillez specifier le nom du fichier",
				L"Specificare il nome del file", L"Especifique el nombre del archivo", L"파일 이름을 지정하세요", L"请指定文件名",
				L"يرجى تحديد اسم الملف", L"Укажите имя файла", L"Bitte Dateinamen angeben", L"Especifique o nome do arquivo",
				L"Geef bestandsnaam op", L"Podaj nazwę pliku", L"Dosya adini belirtin"));
		return;
	}
	// 再生中の状態が書き出しに混ざらないよう、先に停止する
	if (og) og->stop1();
	m_status.SetWindowText(LL14(L"出力中...", L"Exporting...", L"Export en cours...", L"Esportazione in corso...",
		L"Exportando...", L"내보내는 중...", L"导出中...", L"جاري التصدير...",
		L"Экспорт...", L"Exportiere...", L"Exportando...", L"Exporteren...",
		L"Eksportowanie...", L"Dışa aktarılıyor..."));
	m_exec.EnableWindow(FALSE);
	if (m_progress.GetSafeHwnd()) {
		m_progress.SetPos(0);
		m_progress.ShowWindow(SW_SHOW);
	}
	MpDecodeProgressReset();
	MpDecodeProgressSetPcmCap(95);
	MpDecodeProgressSetCb(&CWavExport::ExportProgressThunk, this);
	UpdateWindow();

	BOOL ok = TRUE;
	if (multiFile) {
		CString folder = pathStr;
		if (folder.Right(4).MakeLower() == L".wav") {
			int pos = folder.ReverseFind(L'\\');
			if (pos >= 0) folder = folder.Left(pos + 1);
		}
		if (!folder.IsEmpty() && folder[folder.GetLength() - 1] != L'\\')
			folder += L'\\';
		const size_t total = pcs.size();
		for (size_t i = 0; i < total; ++i) {
			CString outPath = WavExportOutputPathForItem(folder, pcs[i]);
			const int base = (int)((i * 100) / total);
			const int span = (int)(((i + 1) * 100) / total) - base;
			MpDecodeProgressSetSegment(base, span > 0 ? span : 1);
			MpDecodeProgressSetPcmCap(95);
			CString st;
			st.Format(LL14(L"出力中... (%d/%d)", L"Exporting... (%d/%d)", L"Export en cours... (%d/%d)", L"Esportazione... (%d/%d)",
				L"Exportando... (%d/%d)", L"내보내는 중... (%d/%d)", L"导出中... (%d/%d)", L"جاري التصدير... (%d/%d)",
				L"Экспорт... (%d/%d)", L"Exportiere... (%d/%d)", L"Exportando... (%d/%d)", L"Exporteren... (%d/%d)",
				L"Eksportowanie... (%d/%d)", L"Dışa aktarılıyor... (%d/%d)"),
				(int)(i + 1), (int)total);
			m_status.SetWindowText(st);
			UpdateWindow();
			DoEvent();
			ok = og->ExportToWav(&pcs[i], outPath, loopCount, &opts) && ok;
		}
	}
	else {
		CString path = WavExportNormalizeOutputPath(pathStr);
		CString pathForCompare = pathStr;
		if (pathForCompare.Right(4).MakeLower() != L".wav")
			pathForCompare += L".wav";
		if (path != pathForCompare)
			m_path.SetWindowText(path);
		MpDecodeProgressSetSegment(0, 100);
		MpDecodeProgressSetPcmCap(95);
		ok = og->ExportToWav(&pc, path, loopCount, &opts);
	}

	if (ok && m_progress.GetSafeHwnd())
		m_progress.SetPos(100);
	MpDecodeProgressClearCb();
	m_exec.EnableWindow(TRUE);
	if (ok) {
		CString msg = LL14(L"完了", L"Complete", L"Termine", L"Completato",
			L"Completado", L"완료", L"完成", L"اكتمل",
			L"Завершено", L"Abgeschlossen", L"Concluido", L"Voltooid",
			L"Zakończono", L"Tamamlandı");
		m_status.SetWindowText(msg);
		MessageBox(msg, LL14(L"WAVへ出力", L"Export to WAV", L"Exporter en WAV", L"Esporta in WAV",
			L"Exportar a WAV", L"WAV로 내보내기", L"导出到WAV", L"تصدير إلى WAV",
			L"Экспорт в WAV", L"Als WAV exportieren", L"Exportar para WAV", L"Exporteren naar WAV",
			L"Eksportuj do WAV", L"WAV'e aktar"), MB_OK | MB_ICONINFORMATION);
	}
	else {
		CString msg = LL14(L"エラー", L"Error", L"Erreur", L"Errore",
			L"Error", L"오류", L"错误", L"خطأ",
			L"Ошибка", L"Fehler", L"Erro", L"Fout",
			L"Błąd", L"Hata");
		m_status.SetWindowText(msg);
		MessageBox(msg, LL14(L"WAVへ出力", L"Export to WAV", L"Exporter en WAV", L"Esporta in WAV",
			L"Exportar a WAV", L"WAV로 내보내기", L"导出到WAV", L"تصدير إلى WAV",
			L"Экспорт в WAV", L"Als WAV exportieren", L"Exportar para WAV", L"Exporteren naar WAV",
			L"Eksportuj do WAV", L"WAV'e aktar"), MB_OK | MB_ICONERROR);
	}
}

void CWavExport::OnBnClickedWavExportClose()
{
	EndDialog(IDCANCEL);
}
