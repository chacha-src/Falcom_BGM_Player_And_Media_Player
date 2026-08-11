#include "stdafx.h"
#include "ogg.h"
#include "PhotoFrameDlg.h"
#include "CMediaPlayerDlg.h"
#include "resource.h"
#include <ShlObj.h>

#pragma comment(lib,"Msimg32.lib")
extern void MpPersistSavedataQuick();
extern int playf;

namespace {
static BOOL PfImageExt(LPCTSTR n){LPCTSTR p=_tcsrchr(n,L'.');if(!p)return FALSE;return !_tcsicmp(p,L".jpg")||!_tcsicmp(p,L".jpeg")||!_tcsicmp(p,L".png")||!_tcsicmp(p,L".bmp")||!_tcsicmp(p,L".gif");}
class CPfHelp:public CDialog{
public:CPfHelp(CWnd*p):CDialog(IDD_PF_HELP,p){}
protected:virtual BOOL OnInitDialog();virtual void OnOK(){DestroyWindow();}virtual void OnCancel(){DestroyWindow();}virtual void PostNcDestroy();afx_msg void OnPaint();afx_msg BOOL OnEraseBkgnd(CDC*d){CRect r;GetClientRect(r);d->FillSolidRect(r,RGB(248,248,252));return TRUE;}afx_msg void OnClose(){DestroyWindow();}DECLARE_MESSAGE_MAP()
};
static CPfHelp* pfHelp=NULL;
BEGIN_MESSAGE_MAP(CPfHelp,CDialog) ON_WM_PAINT() ON_WM_ERASEBKGND() ON_WM_CLOSE() END_MESSAGE_MAP()
BOOL CPfHelp::OnInitDialog(){CDialog::OnInitDialog();SetWindowText(LL14(L"フォトフレームガイド",L"Photo frame guide",L"Guide cadre photo",L"Guida cornice foto",L"Guía marco de fotos",L"포토 프레임 가이드",L"照片框指南",L"دليل إطار الصور",L"Руководство фоторамки",L"Fotorahmen-Anleitung",L"Guia da moldura",L"Fotolijst-gids",L"Przewodnik ramki",L"Fotoğraf çerçevesi kılavuzu"));if(CWnd*w=GetDlgItem(IDOK))w->SetWindowText(LL14(L"閉じる",L"Close",L"Fermer",L"Chiudi",L"Cerrar",L"닫기",L"关闭",L"إغلاق",L"Закрыть",L"Schließen",L"Fechar",L"Sluiten",L"Zamknij",L"Kapat"));return TRUE;}
void CPfHelp::PostNcDestroy(){CDialog::PostNcDestroy();if(pfHelp==this)pfHelp=NULL;delete this;}
void CPfHelp::OnPaint(){CPaintDC p(this);CCC_GdiHelpPaint h;if(!CCC_GdiHelpBeginPaint(this,p,h))return;CDC&d=h.mem;d.SetBkMode(TRANSPARENT);d.SelectObject(GetFont());d.SetTextColor(RGB(55,45,85));int y=10;d.TextOut(10,y,LL14(L"フォトフレーム",L"Photo frame",L"Cadre photo",L"Cornice foto",L"Marco de fotos",L"포토 프레임",L"照片框",L"إطار الصور",L"Фоторамка",L"Fotorahmen",L"Moldura",L"Fotolijst",L"Ramka zdjęć",L"Fotoğraf çerçevesi"));y+=28;d.SetTextColor(RGB(65,65,80));LPCTSTR a[]={LL14(L"フォルダ内の JPG / JPEG / PNG / BMP / GIF を最大512枚読み込みます。",L"Loads up to 512 JPG / JPEG / PNG / BMP / GIF files in a folder.",L"Charge jusqu'à 512 fichiers JPG/JPEG/PNG/BMP/GIF.",L"Carica fino a 512 file JPG/JPEG/PNG/BMP/GIF.",L"Carga hasta 512 JPG/JPEG/PNG/BMP/GIF.",L"폴더의 JPG/JPEG/PNG/BMP/GIF를 최대 512장 읽습니다.",L"最多加载文件夹内 512 个 JPG/JPEG/PNG/BMP/GIF。",L"يحمّل حتى 512 ملف JPG/JPEG/PNG/BMP/GIF.",L"Загружает до 512 JPG/JPEG/PNG/BMP/GIF.",L"Lädt bis zu 512 JPG/JPEG/PNG/BMP/GIF.",L"Carrega até 512 JPG/JPEG/PNG/BMP/GIF.",L"Laadt maximaal 512 JPG/JPEG/PNG/BMP/GIF.",L"Wczytuje do 512 JPG/JPEG/PNG/BMP/GIF.",L"Klasörde en çok 512 JPG/JPEG/PNG/BMP/GIF yükler."),LL14(L"開始で一定間隔にクロスフェードします。再押下で停止します。",L"Start crossfades at the selected interval; press again to stop.",L"Démarrer lance les fondus; recliquez pour arrêter.",L"Avvia esegue dissolvenze; premi ancora per fermare.",L"Iniciar hace fundidos; pulse otra vez para detener.",L"시작하면 일정 간격으로 전환하며 다시 누르면 중지합니다.",L"开始后按所选间隔淡入淡出；再按停止。",L"يبدأ التلاشي المتبادل؛ اضغط مجددًا للإيقاف.",L"Старт запускает смену; повторное нажатие останавливает.",L"Start blendet im Intervall; erneut drücken zum Stoppen.",L"Iniciar faz transições; prima novamente para parar.",L"Start wisselt met overvloeien; opnieuw voor stoppen.",L"Start uruchamia przejścia; ponownie zatrzymuje.",L"Başlat seçili aralıkta geçiş yapar; tekrar basınca durur."),LL14(L"シャッフルは順序を混ぜ、最前面は他の窓より前に表示します。",L"Shuffle randomizes order; Topmost keeps the frame above other windows.",L"Aléatoire mélange l'ordre; Toujours devant garde la fenêtre au-dessus.",L"Casuale mescola; In primo piano mantiene la finestra sopra.",L"Aleatorio mezcla; Siempre visible mantiene la ventana encima.",L"셔플은 순서를 섞고 최상위는 다른 창 위에 표시합니다.",L"随机会打乱顺序；置顶让窗口保持在其他窗口上方。",L"العشوائي يخلط الترتيب؛ الأعلى يبقي النافذة فوق غيرها.",L"Случайно меняет порядок; Поверх всех держит окно сверху.",L"Zufall mischt; Immer oben hält das Fenster im Vordergrund.",L"Aleatório mistura; Sempre no topo mantém a janela acima.",L"Willekeurig mengt; Altijd boven houdt het venster vooraan.",L"Losowo miesza; Zawsze na wierzchu utrzymuje okno nad innymi.",L"Karıştır sırayı değiştirir; En üstte pencereyi önde tutar."),LL14(L"BGM は停止中のメディアプレイヤーの選択曲を再生します。",L"BGM starts the selected media-player track when playback is stopped.",L"BGM démarre la piste sélectionnée si le lecteur est arrêté.",L"BGM avvia la traccia selezionata se il lettore è fermo.",L"BGM inicia la pista seleccionada si está detenida.",L"BGM은 재생 중이 아닐 때 선택 곡을 시작합니다.",L"BGM 会在停止时播放媒体播放器所选曲目。",L"يشغل BGM المسار المحدد عند توقف التشغيل.",L"BGM запускает выбранный трек, если воспроизведение остановлено.",L"BGM startet den gewählten Titel, wenn Wiedergabe gestoppt ist.",L"BGM inicia a faixa selecionada se estiver parada.",L"BGM start het geselecteerde nummer als afspelen gestopt is.",L"BGM uruchamia wybrany utwór, gdy odtwarzanie jest zatrzymane.",L"BGM oynatma durmuşsa seçili parçayı başlatır.")};for(int i=0;i<4;i++){d.TextOut(10,y,a[i]);y+=22;}CCC_GdiHelpEndPaint(h);}
}

BEGIN_MESSAGE_MAP(CPfPanel,CCustomStatic) ON_WM_PAINT() ON_WM_ERASEBKGND() END_MESSAGE_MAP()
BOOL CPfPanel::SetFirst(LPCTSTR path){m_old.Destroy();m_next.Destroy();m_alpha=255;HRESULT h=m_old.Load(path);Invalidate(FALSE);return SUCCEEDED(h);}
BOOL CPfPanel::BeginNext(LPCTSTR path){m_next.Destroy();m_alpha=0;HRESULT h=m_next.Load(path);Invalidate(FALSE);return SUCCEEDED(h);}
void CPfPanel::CommitNext(){if(!m_next.IsNull()){m_old.Destroy();m_old.Attach(m_next.Detach());}m_alpha=255;Invalidate(FALSE);}
void CPfPanel::DrawFit(CDC&dc,CImage&im,const CRect&r){if(im.IsNull())return;int iw=im.GetWidth(),ih=im.GetHeight();if(iw<=0||ih<=0)return;double s=min((double)r.Width()/iw,(double)r.Height()/ih);int w=max(1,(int)(iw*s)),h=max(1,(int)(ih*s));int x=(r.Width()-w)/2,y=(r.Height()-h)/2;im.Draw(dc.m_hDC,x,y,w,h,0,0,iw,ih);}
void CPfPanel::OnPaint(){CPaintDC dc(this);CRect r;GetClientRect(r);CDC mem;mem.CreateCompatibleDC(&dc);CBitmap bm;bm.CreateCompatibleBitmap(&dc,max(1,r.Width()),max(1,r.Height()));CBitmap*ob=mem.SelectObject(&bm);mem.FillSolidRect(r,RGB(18,18,22));DrawFit(mem,m_old,r);if(!m_next.IsNull()&&m_alpha>0){CDC over;over.CreateCompatibleDC(&dc);CBitmap bb;bb.CreateCompatibleBitmap(&dc,max(1,r.Width()),max(1,r.Height()));CBitmap*oo=over.SelectObject(&bb);over.FillSolidRect(r,RGB(18,18,22));DrawFit(over,m_next,r);BLENDFUNCTION f={AC_SRC_OVER,0,(BYTE)m_alpha,0};::AlphaBlend(mem.m_hDC,0,0,r.Width(),r.Height(),over.m_hDC,0,0,r.Width(),r.Height(),f);over.SelectObject(oo);}dc.BitBlt(0,0,r.Width(),r.Height(),&mem,0,0,SRCCOPY);mem.SelectObject(ob);}

IMPLEMENT_DYNAMIC(CPhotoFrameDlg,CCustomBlurDialogBase)
static CPhotoFrameDlg*g_pf=NULL;
CPhotoFrameDlg::CPhotoFrameDlg(CWnd*p):CCustomBlurDialogBase(IDD,p),m_count(0),m_index(0),m_running(0),m_fading(0),m_fade(0),m_rng(GetTickCount()){memset(m_paths,0,sizeof(m_paths));}
CPhotoFrameDlg::~CPhotoFrameDlg(){StopSlides();}
void CPhotoFrameDlg::DoDataExchange(CDataExchange*p){CCustomBlurDialogBase::DoDataExchange(p);DDX_Control(p,IDC_PF_HELP,m_help);DDX_Control(p,IDC_PF_FOLDER_L,m_folderL);DDX_Control(p,IDC_PF_FOLDER,m_folder);DDX_Control(p,IDC_PF_BROWSE,m_browse);DDX_Control(p,IDC_PF_INTERVAL_L,m_intervalL);DDX_Control(p,IDC_PF_INTERVAL,m_interval);DDX_Control(p,IDC_PF_SHUFFLE,m_shuffle);DDX_Control(p,IDC_PF_TOPMOST,m_topmost);DDX_Control(p,IDC_PF_BGM,m_bgm);DDX_Control(p,IDC_PF_PANEL,m_panel);DDX_Control(p,IDC_PF_START,m_start);DDX_Control(p,IDC_PF_CLOSE,m_close);DDX_Control(p,IDC_PF_STATUS,m_status);}
BEGIN_MESSAGE_MAP(CPhotoFrameDlg,CCustomBlurDialogBase)
	ON_BN_CLICKED(IDC_PF_BROWSE,OnBrowse)
	ON_BN_CLICKED(IDC_PF_START,OnStart)
	ON_BN_CLICKED(IDC_PF_CLOSE,OnCloseBtn)
	ON_BN_CLICKED(IDC_PF_HELP,OnHelp)
	ON_BN_CLICKED(IDC_PF_SHUFFLE,OnChanged)
	ON_BN_CLICKED(IDC_PF_TOPMOST,OnChanged)
	ON_BN_CLICKED(IDC_PF_BGM,OnChanged)
	ON_CBN_SELCHANGE(IDC_PF_INTERVAL,OnChanged)
	ON_EN_KILLFOCUS(IDC_PF_FOLDER,OnChanged)
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
END_MESSAGE_MAP()
BOOL CPhotoFrameDlg::PreTranslateMessage(MSG*p){if(m_tooltip.GetSafeHwnd())m_tooltip.RelayEvent(p);return CCustomBlurDialogBase::PreTranslateMessage(p);}
void CPhotoFrameDlg::PostNcDestroy(){CCustomBlurDialogBase::PostNcDestroy();if(g_pf==this)g_pf=NULL;delete this;}
void CPhotoFrameDlg::LayoutHelpBtn(){CCC_CaptionPlaceHelpBtn(m_hWnd,&m_help);}

void CPhotoFrameDlg::LayoutAll()
{
	if (!GetSafeHwnd() || !m_panel.GetSafeHwnd())
		return;
	CRect rc;
	GetClientRect(&rc);
	const int cx = rc.Width();
	const int cy = rc.Height();
	if (cx < 160 || cy < 160)
		return;

	int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	if (capH < 0)
		capH = 0;

	const int m = 8;
	const int rowH = 22;
	const int btnH = 22;
	const int bottomPad = 8;
	int y = capH + 6;

	// フォルダ行
	if (m_folderL.GetSafeHwnd())
		m_folderL.SetWindowPos(NULL, m, y + 2, 50, 14, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_browse.GetSafeHwnd())
		m_browse.SetWindowPos(NULL, cx - m - 36, y, 36, 18, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_folder.GetSafeHwnd())
		m_folder.SetWindowPos(NULL, m + 54, y, max(40, cx - m - 36 - 8 - (m + 54)), 18, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH;

	// オプション行
	if (m_intervalL.GetSafeHwnd())
		m_intervalL.SetWindowPos(NULL, m, y + 2, 36, 14, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_interval.GetSafeHwnd())
		m_interval.SetWindowPos(NULL, m + 40, y, 80, 120, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_shuffle.GetSafeHwnd())
		m_shuffle.SetWindowPos(NULL, m + 130, y + 2, 78, 16, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_topmost.GetSafeHwnd())
		m_topmost.SetWindowPos(NULL, m + 214, y + 2, 70, 16, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_bgm.GetSafeHwnd())
		m_bgm.SetWindowPos(NULL, m + 290, y + 2, 50, 16, SWP_NOZORDER | SWP_NOACTIVATE);
	y += rowH + 4;

	const int btnY = cy - bottomPad - btnH;
	int panelBottom = btnY - 8;
	if (panelBottom < y + 40)
		panelBottom = y + 40;
	const int panelH = max(40, panelBottom - y);
	m_panel.SetWindowPos(NULL, m, y, max(40, cx - 2 * m), panelH, SWP_NOZORDER | SWP_NOACTIVATE);

	if (m_start.GetSafeHwnd())
		m_start.SetWindowPos(NULL, m + 52, btnY, 90, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_close.GetSafeHwnd())
		m_close.SetWindowPos(NULL, m + 152, btnY, 90, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
	if (m_status.GetSafeHwnd())
		m_status.SetWindowPos(NULL, m + 252, btnY + 3, max(40, cx - (m + 252) - m), 16, SWP_NOZORDER | SWP_NOACTIVATE);

	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
}

BOOL CPhotoFrameDlg::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();
	CCC_BringDialogToForeground(this);
	m_help.SetWindowText(L"?");
	m_help.SetFlat(TRUE);
	m_help.SetGradation(RGB(255, 245, 220), RGB(240, 210, 160), 0, TRUE);
	m_interval.SetAeroMode(FALSE);
	m_shuffle.SetAeroMode(FALSE);
	m_topmost.SetAeroMode(FALSE);
	m_bgm.SetAeroMode(FALSE);
	m_panel.SetAeroMode(FALSE);
	m_browse.SetAeroMode(FALSE);
	m_start.SetAeroMode(FALSE);
	m_close.SetAeroMode(FALSE);
	SetWindowText(LL14(L"フォトフレーム", L"Photo frame", L"Cadre photo", L"Cornice foto", L"Marco de fotos", L"포토 프레임", L"照片框", L"إطار الصور", L"Фоторамка", L"Fotorahmen", L"Moldura", L"Fotolijst", L"Ramka zdjęć", L"Fotoğraf çerçevesi"));
	m_folderL.SetWindowText(LL14(L"フォルダ", L"Folder", L"Dossier", L"Cartella", L"Carpeta", L"폴더", L"文件夹", L"المجلد", L"Папка", L"Ordner", L"Pasta", L"Map", L"Folder", L"Klasör"));
	m_intervalL.SetWindowText(LL14(L"間隔", L"Interval", L"Intervalle", L"Intervallo", L"Intervalo", L"간격", L"间隔", L"الفاصل", L"Интервал", L"Intervall", L"Intervalo", L"Interval", L"Interwał", L"Aralık"));
	m_shuffle.SetWindowText(LL14(L"シャッフル", L"Shuffle", L"Aléatoire", L"Casuale", L"Aleatorio", L"셔플", L"随机", L"عشوائي", L"Случайно", L"Zufall", L"Aleatório", L"Willekeurig", L"Losowo", L"Karıştır"));
	m_topmost.SetWindowText(LL14(L"最前面", L"Topmost", L"Toujours devant", L"In primo piano", L"Siempre visible", L"항상 위", L"置顶", L"دائمًا في الأعلى", L"Поверх всех", L"Immer oben", L"Sempre no topo", L"Altijd boven", L"Zawsze na wierzchu", L"En üstte"));
	m_bgm.SetWindowText(LL14(L"BGM", L"BGM", L"BGM", L"BGM", L"BGM", L"BGM", L"BGM", L"BGM", L"BGM", L"BGM", L"BGM", L"BGM", L"BGM", L"BGM"));
	m_start.SetWindowText(LL14(L"開始", L"Start", L"Démarrer", L"Avvia", L"Iniciar", L"시작", L"开始", L"بدء", L"Старт", L"Start", L"Iniciar", L"Start", L"Start", L"Başlat"));
	m_close.SetWindowText(LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_folder.SetWindowText(savedata.pf_folder);
	static const int ms[] = { 1000, 2000, 3000, 5000, 10000, 15000, 30000, 60000 };
	for (int i = 0; i < 8; i++) {
		CString x;
		x.Format(L"%.0f s", ms[i] / 1000.0);
		m_interval.AddString(x);
		if (ms[i] == savedata.pf_interval_ms)
			m_interval.SetCurSel(i);
	}
	if (m_interval.GetCurSel() < 0)
		m_interval.SetCurSel(3);
	m_shuffle.SetCheck(savedata.pf_shuffle ? BST_CHECKED : BST_UNCHECKED);
	m_topmost.SetCheck(savedata.pf_topmost ? BST_CHECKED : BST_UNCHECKED);
	m_bgm.SetCheck(savedata.pf_bgm ? BST_CHECKED : BST_UNCHECKED);
	if (CCustomControlUtility::BeginDialogToolTip(m_tooltip, this)) {
		m_tooltip.AddTool(&m_panel, LL14(L"画像は縦横比を保って表示し、滑らかに切り替わります。", L"Images keep aspect ratio and crossfade smoothly.", L"Les images gardent leur ratio et se fondent.", L"Le immagini mantengono il rapporto e sfumano.", L"Las imágenes conservan proporción y se funden.", L"이미지는 비율을 유지하고 부드럽게 전환됩니다.", L"图像保持宽高比并平滑淡入淡出。", L"تحافظ الصور على النسبة وتتلاشى بسلاسة.", L"Изображения сохраняют пропорции и плавно сменяются.", L"Bilder behalten Seitenverhältnis und blenden weich.", L"As imagens mantêm proporção e transitam suavemente.", L"Afbeeldingen behouden verhouding en vloeien over.", L"Obrazy zachowują proporcje i płynnie przechodzą.", L"Görüntüler oranı korur ve yumuşak geçer."));
		m_tooltip.AddTool(&m_start, LL14(L"スライドショーを開始または停止", L"Start or stop slideshow", L"Démarrer ou arrêter le diaporama", L"Avvia o ferma la presentazione", L"Iniciar o detener diapositivas", L"슬라이드쇼 시작/중지", L"开始或停止幻灯片", L"بدء عرض الشرائح أو إيقافه", L"Запустить или остановить слайд-шоу", L"Diashow starten/stoppen", L"Iniciar ou parar apresentação", L"Diavoorstelling starten/stoppen", L"Uruchom lub zatrzymaj pokaz", L"Slayt gösterisini başlat/durdur"));
		CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 340, 9000);
	}
	ApplyTopMost();
	CCC_CaptionLayout(m_hWnd);
	LayoutHelpBtn();
	LayoutAll();
	PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS);
	return TRUE;
}
void CPhotoFrameDlg::PersistUi(){CString f;m_folder.GetWindowText(f);_tcsncpy(savedata.pf_folder,f,1023);savedata.pf_folder[1023]=0;static const int ms[]={1000,2000,3000,5000,10000,15000,30000,60000};savedata.pf_interval_ms=ms[max(0,min(7,m_interval.GetCurSel()))];savedata.pf_shuffle=m_shuffle.GetCheck()==BST_CHECKED;savedata.pf_topmost=m_topmost.GetCheck()==BST_CHECKED;savedata.pf_bgm=m_bgm.GetCheck()==BST_CHECKED;MpPersistSavedataQuick();}
void CPhotoFrameDlg::EnumerateImages(){m_count=0;CString folder;m_folder.GetWindowText(folder);folder.TrimRight(L"\\/");CString pat=folder+L"\\*";WIN32_FIND_DATA fd={};HANDLE h=FindFirstFile(pat,&fd);if(h!=INVALID_HANDLE_VALUE){do{if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&PfImageExt(fd.cFileName)&&m_count<PF_IMAGE_MAX){CString p=folder+L"\\"+fd.cFileName;_tcsncpy(m_paths[m_count],p,1023);m_paths[m_count][1023]=0;m_count++;}}while(FindNextFile(h,&fd));FindClose(h);}if(savedata.pf_shuffle&&m_count>1)for(int i=m_count-1;i>0;i--){m_rng=m_rng*1664525+1013904223;int j=m_rng%(i+1);TCHAR tmp[1024];_tcscpy(tmp,m_paths[i]);_tcscpy(m_paths[i],m_paths[j]);_tcscpy(m_paths[j],tmp);}}
void CPhotoFrameDlg::StartSlides(){PersistUi();EnumerateImages();if(!m_count){m_status.SetWindowText(LL14(L"画像が見つかりません。",L"No images found.",L"Aucune image.",L"Nessuna immagine.",L"No se encontraron imágenes.",L"이미지를 찾을 수 없습니다.",L"未找到图像。",L"لم يتم العثور على صور.",L"Изображения не найдены.",L"Keine Bilder gefunden.",L"Não foram encontradas imagens.",L"Geen afbeeldingen gevonden.",L"Nie znaleziono obrazów.",L"Görüntü bulunamadı."));return;}m_index=0;if(!m_panel.SetFirst(m_paths[0])){m_status.SetWindowText(LL14(L"最初の画像を開けません。",L"Cannot open the first image.",L"Impossible d'ouvrir la première image.",L"Impossibile aprire la prima immagine.",L"No se puede abrir la primera imagen.",L"첫 이미지를 열 수 없습니다.",L"无法打开第一张图像。",L"تعذر فتح الصورة الأولى.",L"Не удалось открыть первое изображение.",L"Erstes Bild kann nicht geöffnet werden.",L"Não foi possível abrir a primeira imagem.",L"Kan eerste afbeelding niet openen.",L"Nie można otworzyć pierwszego obrazu.",L"İlk görüntü açılamıyor."));return;}m_running=1;SetTimer(PF_SLIDE_TIMER,savedata.pf_interval_ms,NULL);m_start.SetWindowText(LL14(L"停止",L"Stop",L"Arrêter",L"Ferma",L"Detener",L"중지",L"停止",L"إيقاف",L"Стоп",L"Stop",L"Parar",L"Stop",L"Stop",L"Durdur"));CString x;x.Format(LL14(L"%d 枚を読み込みました。",L"Loaded %d images.",L"%d images chargées.",L"Caricate %d immagini.",L"Cargadas %d imágenes.",L"%d장을 읽었습니다.",L"已加载 %d 张图像。",L"تم تحميل %d صورة.",L"Загружено изображений: %d.",L"%d Bilder geladen.",L"%d imagens carregadas.",L"%d afbeeldingen geladen.",L"Wczytano obrazów: %d.",L"%d görüntü yüklendi."),m_count);m_status.SetWindowText(x);ApplyBgm();}
void CPhotoFrameDlg::StopSlides(){if(GetSafeHwnd()){KillTimer(PF_SLIDE_TIMER);KillTimer(PF_FADE_TIMER);}m_running=0;m_fading=0;if(m_start.GetSafeHwnd())m_start.SetWindowText(LL14(L"開始",L"Start",L"Démarrer",L"Avvia",L"Iniciar",L"시작",L"开始",L"بدء",L"Старт",L"Start",L"Iniciar",L"Start",L"Start",L"Başlat"));}
void CPhotoFrameDlg::AdvanceSlide(){if(m_count<2||m_fading)return;m_index=(m_index+1)%m_count;if(!m_panel.BeginNext(m_paths[m_index]))return;m_fading=1;m_fade=0;SetTimer(PF_FADE_TIMER,35,NULL);}
void CPhotoFrameDlg::ApplyTopMost(){if(GetSafeHwnd())SetWindowPos(savedata.pf_topmost?&wndTopMost:&wndNoTopMost,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);}
void CPhotoFrameDlg::ApplyBgm(){if(savedata.pf_bgm&&mp&&mp->GetSafeHwnd()&&!playf)mp->PostMessage(WM_COMMAND,MAKEWPARAM(IDC_MP_PLAY,BN_CLICKED),0);}
void CPhotoFrameDlg::OnTimer(UINT_PTR id){if(id==PF_SLIDE_TIMER)AdvanceSlide();else if(id==PF_FADE_TIMER){m_fade+=24;if(m_fade>=255){m_panel.SetBlend(255);m_panel.CommitNext();m_fading=0;KillTimer(PF_FADE_TIMER);}else m_panel.SetBlend(m_fade);}CCustomBlurDialogBase::OnTimer(id);}
void CPhotoFrameDlg::OnBrowse(){BROWSEINFO bi={};bi.hwndOwner=m_hWnd;bi.lpszTitle=LL14(L"画像フォルダを選択",L"Select image folder",L"Sélectionner le dossier d'images",L"Seleziona cartella immagini",L"Seleccione carpeta de imágenes",L"이미지 폴더 선택",L"选择图像文件夹",L"اختر مجلد الصور",L"Выберите папку изображений",L"Bildordner auswählen",L"Selecionar pasta de imagens",L"Afbeeldingsmap kiezen",L"Wybierz folder obrazów",L"Görüntü klasörünü seçin");bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;PIDLIST_ABSOLUTE id=SHBrowseForFolder(&bi);if(id){TCHAR p[MAX_PATH]={};if(SHGetPathFromIDList(id,p))m_folder.SetWindowText(p);CoTaskMemFree(id);OnChanged();}}
void CPhotoFrameDlg::OnStart(){if(m_running)StopSlides();else StartSlides();}
void CPhotoFrameDlg::OnChanged(){PersistUi();ApplyTopMost();ApplyBgm();}
void CPhotoFrameDlg::ShowHelpSheet(){if(pfHelp&&pfHelp->GetSafeHwnd()){pfHelp->SetForegroundWindow();return;}pfHelp=new CPfHelp(this);if(!pfHelp->Create(IDD_PF_HELP,this)){delete pfHelp;pfHelp=NULL;return;}CCC_PresentOwnedHelp(this,pfHelp);}
void CPhotoFrameDlg::OnHelp(){ShowHelpSheet();}
void CPhotoFrameDlg::OnCloseBtn(){DestroyWindow();}
void CPhotoFrameDlg::OnOK(){}
void CPhotoFrameDlg::OnCancel(){DestroyWindow();}
void CPhotoFrameDlg::OnSize(UINT t,int x,int y)
{
	CCustomBlurDialogBase::OnSize(t,x,y);
	if (GetSafeHwnd() && t != SIZE_MINIMIZED)
		LayoutAll();
}
void CPhotoFrameDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomBlurDialogBase::OnShowWindow(bShow, nStatus);
	if (bShow && GetSafeHwnd())
		LayoutAll();
}
void CPhotoFrameDlg::OnDestroy(){PersistUi();StopSlides();CCustomBlurDialogBase::OnDestroy();}
void OpenPhotoFrameModeless(CWnd*p){if(g_pf&&g_pf->GetSafeHwnd()){g_pf->SetForegroundWindow();return;}g_pf=new CPhotoFrameDlg(p);if(!g_pf->Create(IDD_PHOTOFRAME,p)){delete g_pf;g_pf=NULL;return;}g_pf->ShowWindow(SW_SHOW);}
void ClosePhotoFrameIfOpen(){if(g_pf&&g_pf->GetSafeHwnd())g_pf->DestroyWindow();}
