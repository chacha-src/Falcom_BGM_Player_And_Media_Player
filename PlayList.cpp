// PlayList.cpp : 実装ファイル
//

#include "stdafx.h"
#include "direct.h"
#include "dshow.h"
#include "ogg.h"
#include "oggDlg.h"
#include "ListCtrlA.h"
#include "PlayList.h"
#include "ListSyosai.h"
#include "Filename.h"
#include "Douga.h"
#include "mp3image.h"
#include "CImageBase.h"
#include "CPlayListNew.h"

// CPlayList ダイアログ

IMPLEMENT_DYNAMIC(CPlayList, CCustomDialog)

extern 	CString ext[150][300];
extern 	CString kpif[400];
extern  BOOL kpichk[200];
extern 	int kpicnt;
extern COggDlg *og;
extern BOOL plw;

extern BYTE kvar[150][300];
extern BYTE kvver;

CPlayList::CPlayList(CWnd* pParent /*=NULL*/)
	: CCustomDialog(CPlayList::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDI_PL);
	pc=NULL;
	plw=0;
//	pc = new playlistdata0[60000];
}

CPlayList::~CPlayList()
{
	if (pc) {
		free(pc);
		pc = NULL;
	}
	m_tooltip.DestroyWindow();
}

void CPlayList::DoDataExchange(CDataExchange* pDX)
{
	CCustomDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_BUTTON1, m_lsup);
	DDX_Control(pDX, IDC_BUTTON5, m_lup);
	DDX_Control(pDX, IDC_BUTTON10, m_lsdown);
	DDX_Control(pDX, IDC_BUTTON11, m_ldown);
	DDX_Control(pDX, IDC_LIST1, m_lc);
	DDX_Control(pDX, IDC_EDIT1, m_e);
	DDX_Control(pDX, IDC_CHECK1, m_renzoku);
	DDX_Control(pDX, IDC_CHECK4, m_loop);
	DDX_Control(pDX, IDC_CHECK28, m_tool);
	DDX_Control(pDX, IDC_CHECK29, m_saisyo);
	DDX_Control(pDX, IDC_EDIT2, m_find);
	DDX_Control(pDX, IDC_BUTTON16, m_findup);
	DDX_Control(pDX, IDC_BUTTON20, m_finddown);
	DDX_Control(pDX, IDC_CHECK5, m_savecheck);
	DDX_Control(pDX, IDC_CHECK6, m_save_mp3);
	DDX_Control(pDX, IDC_CHECK7, m_save_kpi);
	DDX_Control(pDX, IDC_COMBO1, m_listchange);
	DDX_Control(pDX, IDC_BUTTON3, m_namechage);
	DDX_Control(pDX, IDC_PLAYDELETE, m_listdelete);
}


BEGIN_MESSAGE_MAP(CPlayList, CCustomDialog)
	ON_WM_NCDESTROY()
	ON_WM_CREATE()
	ON_WM_CLOSE()
	ON_BN_CLICKED(IDOK, &CPlayList::OnBnClickedOk)
	ON_BN_CLICKED(IDC_BUTTON1, &CPlayList::OnUP)
	ON_BN_CLICKED(IDC_BUTTON5, &CPlayList::OnSUP)
	ON_BN_CLICKED(IDC_BUTTON10, &CPlayList::OnSDOWN)
	ON_BN_CLICKED(IDC_BUTTON11, &CPlayList::OnDOWN)
	ON_NOTIFY(LVN_KEYDOWN, IDC_LIST1, &CPlayList::OnLvnKeydownList1)
	ON_WM_DROPFILES()
	ON_NOTIFY(NM_DBLCLK, IDC_LIST1, &CPlayList::OnNMDblclkList1)
	ON_WM_SIZE()
	ON_WM_TIMER()
	ON_WM_KEYDOWN()
	ON_BN_CLICKED(IDC_CHECK4, &CPlayList::OnBnClickedCheck4)
	ON_BN_CLICKED(IDC_CHECK1, &CPlayList::OnBnClickedCheck1)
	ON_NOTIFY(LVN_BEGINDRAG, IDC_LIST1, &CPlayList::OnLvnBegindragList1)
	ON_WM_MOUSEMOVE()
	ON_WM_LBUTTONUP()
	ON_NOTIFY(LVN_GETDISPINFO, IDC_LIST1, &CPlayList::OnLvnGetdispinfoList1)
	ON_NOTIFY(NM_RCLICK, IDC_LIST1, &CPlayList::OnNMRclickList1)
	ON_COMMAND(ID_POP_32776, OnList)
	ON_COMMAND(ID_POP_32777,Del)
	ON_WM_ACTIVATE()
	ON_COMMAND(ID_POP_32787, &CPlayList::OnPop32787)
	ON_BN_CLICKED(IDC_BUTTON16, &CPlayList::OnFindUp)
	ON_BN_CLICKED(IDC_BUTTON20, &CPlayList::OnFindDown)
	ON_BN_CLICKED(IDC_CHECK6, &CPlayList::OnBnClickedCheck6mp3)
	ON_BN_CLICKED(IDC_CHECK7, &CPlayList::OnBnClickedCheck7dshow)
	ON_WM_CTLCOLOR()
	ON_WM_SHOWWINDOW()
	ON_WM_MOVING()
	ON_WM_SIZING()
	ON_WM_SETFOCUS()
	ON_WM_NCACTIVATE()
	ON_CBN_SELCHANGE(IDC_COMBO1, &CPlayList::OnCbnSelchangeCombo1)
	ON_BN_CLICKED(IDC_BUTTON3, &CPlayList::OnBnClickedButton3)
	ON_BN_CLICKED(IDC_PLAYDELETE, &CPlayList::OnBnClickedPlaydelete)
END_MESSAGE_MAP()

#include <eh.h>
class SE_Exception1
{
private:
    unsigned int nSE;
public:
    SE_Exception1() {}
    SE_Exception1( unsigned int n ) : nSE( n ) {}
    ~SE_Exception1() {}
    unsigned int getSeNumber() { return nSE; }
};
void trans_func1( unsigned int, EXCEPTION_POINTERS* );
void trans_func1( unsigned int u, EXCEPTION_POINTERS* pExp )
{
    throw SE_Exception1();
}


int playcnt=0;
float hD2;
int syo;
int syomode;
CString syos;
extern TCHAR karento2[1024];
extern int fade1;
extern IMediaPosition *pMediaPosition;
extern int mode,videoonly,playf;
extern int plcnt;
extern save savedata;
extern CPlayList* pl;
CImageBase* playbase;
int ogpl = 0;

BOOL CPlayList::OnInitDialog()
{
	CCustomDialog::OnInitDialog();

	CDC *desktopDc = GetDC();
	// Get native resolution
	int horizontalDPI = GetDeviceCaps(desktopDc->m_hDC, LOGPIXELSX);
	hD2 = (float)(horizontalDPI) / (96.0f);
	ReleaseDC(desktopDc);

	playcnt=0;
	w_flg=TRUE;
	pnt=0;
	SetIcon(m_hIcon, TRUE);			// 大きいアイコンを設定
	SetIcon(m_hIcon, FALSE);		// 小さいアイコンを設定
	SetWindowText(LL14(L"プレイリスト", L"Playlist", L"Liste de lecture", L"Playlist", L"Lista de reproducción", L"재생 목록", L"播放列表", L"قائمة التشغيل", L"Плейлист", L"Wiedergabeliste", L"Lista de reprodução", L"Afspeellijst", L"Lista odtwarzania", L"Çalma listesi"));
	SetDlgItemText(IDC_CHECK1, LL14(L"連続再生", L"Continuous play", L"Lecture continue", L"Riproduzione continua", L"Reproducción continua", L"연속 재생", L"连续播放", L"تشغيل مستمر", L"Непрерывное воспроизведение", L"Fortlaufende Wiedergabe", L"Reprodução contínua", L"Doorlopend afspelen", L"Ciągłe odtwarzanie", L"Sürekli çalma"));
	SetDlgItemText(IDC_CHECK4, LL14(L"ループ再生", L"Loop play", L"Lecture en boucle", L"Riproduzione in loop", L"Reproducción en bucle", L"반복 재생", L"循环播放", L"تشغيل متكرر", L"Зацикленное воспроизведение", L"Schleifenwiedergabe", L"Reprodução em loop", L"Herhalend afspelen", L"Odtwarzanie w pętli", L"Döngüde çalma"));
	SetDlgItemText(IDC_CHECK28, LL14(L"ツールチップ表示", L"Show tooltips", L"Afficher les info-bulles", L"Mostra suggerimenti", L"Mostrar sugerencias", L"툴팁 표시", L"显示工具提示", L"إظهار التلميحات", L"Показывать подсказки", L"Tooltips anzeigen", L"Mostrar dicas", L"Tooltips tonen", L"Pokaż etykiety", L"İpuçlarını göster"));
	SetDlgItemText(IDC_CHECK29, LL14(L"最小化、復帰", L"Minimize, restore", L"Réduire, restaurer", L"Riduci, ripristina", L"Minimizar, restaurar", L"최소화, 복원", L"最小化、还原", L"تصغير، استعادة", L"Свернуть, восстановить", L"Minimieren, wiederherstellen", L"Minimizar, restaurar", L"Minimaliseren, herstellen", L"Minimalizuj, przywróć", L"Küçült, geri yükle"));
	SetDlgItemText(IDC_CHECK5, LL14(L"再生位置\nを保存", L"Save\nplayback position", L"Enregistrer la\nposition de lecture", L"Salva posizione\ndi riproduzione", L"Guardar posición\nde reproducción", L"재생 위치\n저장", L"保存\n播放位置", L"حفظ موضع التشغيل", L"Сохранить позицию\nвоспроизведения", L"Wiedergabeposition\nspeichern", L"Salvar posição\nde reprodução", L"Afspeelpositie\nopslaan", L"Zapisz pozycję\nodtwarzania", L"Oynatma konumunu\nkaydet"));
	SetDlgItemText(IDC_STATICido, LL14(L"ファイル移動", L"File move", L"Déplacer fichier", L"Sposta file", L"Mover archivo", L"파일 이동", L"文件移动", L"نقل الملف", L"Переместить файл", L"Datei verschieben", L"Mover arquivo", L"Bestand verplaatsen", L"Przenieś plik", L"Dosya taşı"));
	SetDlgItemText(IDC_STATICken, LL14(L"あいまい検索", L"Fuzzy search", L"Recherche floue", L"Ricerca fuzzy", L"Búsqueda difusa", L"퍼지 검색", L"模糊搜索", L"بحث غامض", L"Нечеткий поиск", L"Fuzzy-Suche", L"Pesquisa fuzzy", L"Fuzzy zoeken", L"Wyszukiwanie rozmyte", L"Bulanık arama"));
	SetDlgItemText(IDC_BUTTON3, LL14(L"名前変更", L"Rename", L"Renommer", L"Rinomina", L"Cambiar nombre", L"이름 바꾸기", L"重命名", L"إعادة تسمية", L"Переименовать", L"Umbenennen", L"Renomear", L"Hernoemen", L"Zmień nazwę", L"Yeniden adlandır"));
	SetDlgItemText(IDC_PLAYDELETE, LL14(L"リスト削除", L"Delete list", L"Supprimer la liste", L"Elimina lista", L"Eliminar lista", L"목록 삭제", L"删除列表", L"حذف القائمة", L"Удалить список", L"Liste löschen", L"Excluir lista", L"Lijst verwijderen", L"Usuń listę", L"Listeyi sil"));
	m_lsup.SetIcon(IDR_SUP);
	m_lsup.SetFlat(TRUE);
	m_lup.SetIcon(IDR_UP);
	m_lup.SetFlat(TRUE);
	m_lsdown.SetIcon(IDR_SDOWN);
	m_lsdown.SetFlat(TRUE);
	m_ldown.SetIcon(IDR_DOWN);
	m_ldown.SetFlat(TRUE);

	m_findup.SetIcon(IDR_DOWN);
	m_findup.SetFlat(TRUE);
	m_finddown.SetIcon(IDR_UP);
	m_finddown.SetFlat(TRUE);

	m_tooltip.Create(this,TTS_ALWAYSTIP | TTS_BALLOON);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), LL14(L"プレイリストを閉じます。", L"Close the playlist.", L"Fermer la liste de lecture.", L"Chiudi la playlist.", L"Cerrar la lista de reproducción.", L"재생 목록을 닫습니다.", L"关闭播放列表。", L"إغلاق قائمة التشغيل.", L"Закрыть плейлист.", L"Wiedergabeliste schließen.", L"Fechar lista de reprodução.", L"Afspeellijst sluiten.", L"Zamknij listę odtwarzania.", L"Çalma listesini kapat."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON1), LL14(L"選択項目を一番上に持って行きます。", L"Move selected item to the top.", L"Déplacer l'élément sélectionné tout en haut.", L"Sposta l'elemento selezionato in cima.", L"Mover elemento seleccionado al inicio.", L"선택한 항목을 맨 위로 이동합니다.", L"将所选项目移至顶部。", L"نقل العنصر المحدد إلى الأعلى.", L"Переместить выбранный элемент вверх.", L"Gewähltes Element nach oben verschieben.", L"Mover item selecionado para o topo.", L"Geselecteerd item naar boven verplaatsen.", L"Przenieś zaznaczony element na górę.", L"Seçili öğeyi en üste taşı."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON5), LL14(L"選択項目を上に持って行きます。", L"Move selected item up.", L"Déplacer l'élément sélectionné vers le haut.", L"Sposta l'elemento selezionato in alto.", L"Mover elemento seleccionado arriba.", L"선택한 항목을 위로 이동합니다.", L"将所选项目上移。", L"نقل العنصر المحدد لأعلى.", L"Переместить выбранный элемент вверх.", L"Gewähltes Element nach oben verschieben.", L"Mover item selecionado para cima.", L"Geselecteerd item omhoog verplaatsen.", L"Przenieś zaznaczony element w górę.", L"Seçili öğeyi yukarı taşı."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON10), LL14(L"選択項目を一番下に持って行きます。", L"Move selected item to the bottom.", L"Déplacer l'élément sélectionné tout en bas.", L"Sposta l'elemento selezionato in fondo.", L"Mover elemento seleccionado al final.", L"선택한 항목을 맨 아래로 이동합니다.", L"将所选项目移至底部。", L"نقل العنصر المحدد إلى الأسفل.", L"Переместить выбранный элемент вниз.", L"Gewähltes Element nach unten verschieben.", L"Mover item selecionado para o final.", L"Geselecteerd item naar beneden verplaatsen.", L"Przenieś zaznaczony element na dół.", L"Seçili öğeyi en alta taşı."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON11), LL14(L"選択項目を下に持って行きます。", L"Move selected item down.", L"Déplacer l'élément sélectionné vers le bas.", L"Sposta l'elemento selezionato in basso.", L"Mover elemento seleccionado abajo.", L"선택한 항목을 아래로 이동합니다.", L"将所选项目下移。", L"نقل العنصر المحدد لأسفل.", L"Переместить выбранный элемент вниз.", L"Gewähltes Element nach unten verschieben.", L"Mover item selecionado para baixo.", L"Geselecteerd item omlaag verplaatsen.", L"Przenieś zaznaczony element w dół.", L"Seçili öğeyi aşağı taşı."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON16), LL14(L"現在の位置から下に検索します。", L"Search downward from current position.", L"Rechercher vers le bas à partir de la position actuelle.", L"Cerca verso il basso dalla posizione corrente.", L"Buscar hacia abajo desde la posición actual.", L"현재 위치부터 아래로 검색합니다.", L"从当前位置向下搜索。", L"البحث للأسفل من الموضع الحالي.", L"Искать вниз от текущей позиции.", L"Ab aktueller Position nach unten suchen.", L"Pesquisar para baixo a partir da posição atual.", L"Zoek naar beneden vanaf de huidige positie.", L"Szukaj w dół od bieżącej pozycji.", L"Mevcut konumdan aşağı doğru ara."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON20), LL14(L"現在の位置から上に検索します。", L"Search upward from current position.", L"Rechercher vers le haut à partir de la position actuelle.", L"Cerca verso l'alto dalla posizione corrente.", L"Buscar hacia arriba desde la posición actual.", L"현재 위치부터 위로 검색합니다.", L"从当前位置向上搜索。", L"البحث للأعلى من الموضع الحالي.", L"Искать вверх от текущей позиции.", L"Ab aktueller Position nach oben suchen.", L"Pesquisar para cima a partir da posição atual.", L"Zoek naar boven vanaf de huidige positie.", L"Szukaj w górę od bieżącej pozycji.", L"Mevcut konumdan yukarı doğru ara."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK1), LL14(L"プレイリストの順番に再生を行います。\n再生中にファイルドロップして追加しても演奏中の曲はそのまま鳴り続けます。", L"Playback in playlist order.\nEven if files are added during playback, the currently playing track continues.", L"Lecture dans l'ordre de la liste.\nLa piste en cours continue même si des fichiers sont ajoutés pendant la lecture.", L"Riproduzione nell'ordine della playlist.\nAnche se aggiungi file durante la riproduzione, la traccia corrente continua.", L"Reproducción en orden de la lista.\nAunque se añadan archivos durante la reproducción, la pista actual continúa.", L"재생 목록 순서대로 재생합니다.\n재생 중 파일을 추가해도 현재 재생 중인 곡은 계속 재생됩니다.", L"按播放列表顺序播放。\n播放期间添加文件时，当前曲目仍继续播放。", L"التشغيل بترتيب القائمة.\nحتى عند إضافة ملفات أثناء التشغيل، تستمر المسار الحالي.", L"Воспроизведение по порядку плейлиста.\nДаже при добавлении файлов текущий трек продолжает воспроизводиться.", L"Wiedergabe in Playlist-Reihenfolge.\nBei zusätzlichen Dateien während der Wiedergabe läuft der aktuelle Titel weiter.", L"Reprodução na ordem da lista.\nMesmo ao adicionar arquivos durante a reprodução, a faixa atual continua.", L"Afspeel in playlistvolgorde.\nBij toevoegen van bestanden tijdens afspelen gaat het huidige nummer door.", L"Odtwarzaj w kolejności listy.\nPrzy dodawaniu plików podczas odtwarzania aktualny utwór kontynuuje.", L"Liste sırasına göre çalma.\nÇalma sırasında dosya eklense bile çalınan parça devam eder."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK4), LL14(L"選択した曲をループさせます。\n再生する前にチェックを入れる必要があります。\nそうでないとループはかかりません。\nループポイントが0のもの(mp3やループしない曲)が対象です。", L"Loop selected track.\nCheck before playback to enable looping.\nOtherwise, looping will not work.\nApplies to tracks with loop point 0 (mp3 or non-looping tracks).", L"Boucler la piste sélectionnée.\nCochez avant la lecture pour activer la boucle.\nS'applique aux pistes avec point de boucle 0.", L"Ripeti la traccia selezionata.\nSpunta prima della riproduzione per attivare il loop.", L"Repetir pista seleccionada.\nMarque antes de reproducir para activar el bucle.", L"선택한 곡을 반복합니다.\n재생 전에 체크해야 합니다.", L"循环所选曲目。\n播放前需勾选才能启用循环。", L"تكرار المسار المحدد.\nتحقق قبل التشغيل لتفعيل التكرار.", L"Зациклить выбранный трек.\nОтметьте перед воспроизведением.", L"Gewählten Titel wiederholen.\nVor Wiedergabe aktivieren.", L"Repetir faixa selecionada.\nMarque antes de reproduzir para ativar o loop.", L"Herhaal geselecteerd nummer.\nVink aan vóór afspelen.", L"Zapętl zaznaczony utwór.\nZaznacz przed odtwarzaniem.", L"Seçili parçayı döngüye al.\nÇalmadan önce işaretleyin."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK28), LL14(L"ツールチップを表示します。", L"Show tooltips.", L"Afficher les info-bulles.", L"Mostra suggerimenti.", L"Mostrar sugerencias.", L"툴팁을 표시합니다.", L"显示工具提示。", L"إظهار التلميحات.", L"Показывать подсказки.", L"Tooltips anzeigen.", L"Mostrar dicas.", L"Tooltips tonen.", L"Pokaż etykiety.", L"İpuçlarını göster."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK29), LL14(L"最小化、最小化からの復帰時、メイン画面とプレイリスト画面も同時に最小化、最小化からの復帰を行います。", L"When minimizing/restoring, main window and playlist window minimize/restore together.", L"Lors de la minimisation/restauration, les fenêtres principale et playlist font de même.", L"Alla minimizzazione/ripristino, finestra principale e playlist si minimizzano/ripristinano insieme.", L"Al minimizar/restaurar, ventana principal y lista se minimizan/restauran juntas.", L"최소화/복원 시 메인 창과 재생 목록 창도 함께 최소화/복원됩니다.", L"最小化/还原时，主窗口和播放列表窗口同步最小化/还原。", L"عند التصغير/الاستعادة، تُصغَّر النوافذ معاً.", L"При сворачивании/восстановлении окна сворачиваются вместе.", L"Beim Minimieren/Wiederherstellen werden beide Fenster zusammen behandelt.", L"Ao minimizar/restaurar, as janelas fazem o mesmo juntas.", L"Bij minimaliseren/herstellen gaan beide vensters mee.", L"Przy minimalizowaniu/przywracaniu okna zmieniają się razem.", L"Küçültme/geri yüklemede ana pencere ve liste birlikte değişir."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK5), LL14(L"途中で演奏を停止した位置を自動保存します。\nmp3系と動画(avi,mp4など)のみ対応。\n停止ボタンもしくは終了したときのみ保存します。\n再生中に違う曲を選んだ時は位置は保存しません。", L"Auto-save playback position when stopped.\nSupports mp3 and video (avi, mp4, etc.) only.\nSaves only when stop button is pressed or when exiting.\nPosition is not saved when selecting a different track during playback.", L"Enregistrement auto de la position à l'arrêt.\nPrise en charge mp3 et vidéo uniquement.", L"Salva automaticamente la posizione all'arresto.\nSupporta solo mp3 e video.", L"Guardar posición automáticamente al detener.\nSolo mp3 y video.", L"중지 시 재생 위치를 자동 저장합니다.\nmp3 및 동영상만 지원.", L"停止时自动保存播放位置。\n仅支持mp3和视频。", L"حفظ موضع التشغيل تلقائياً عند التوقف.", L"Автосохранение позиции при остановке.\nТолько mp3 и видео.", L"Position automatisch speichern.\nNur mp3 und Video.", L"Salva posição ao parar.\nApenas mp3 e vídeo.", L"Positie opslaan bij stoppen.\nAlleen mp3 en video.", L"Zapisz pozycję przy zatrzymaniu.\nTylko mp3 i wideo.", L"Durdurulduğunda konumu kaydet.\nSadece mp3 ve video."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK6), LL14(L"mp3再生時に途中保存を有効にします。", L"Enable mid-playback save for mp3.", L"Activer l'enregistrement de position pour mp3.", L"Abilita salvataggio posizione per mp3.", L"Habilitar guardado de posición para mp3.", L"mp3 재생 시 위치 저장을 활성화합니다.", L"mp3播放时启用位置保存。", L"تفعيل حفظ الموضع لـ mp3.", L"Включить сохранение позиции для mp3.", L"Positionsspeicherung für mp3 aktivieren.", L"Habilitar salvamento para mp3.", L"Positieopslag voor mp3 inschakelen.", L"Włącz zapisywanie pozycji dla mp3.", L"mp3 için konum kaydını etkinleştir."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK7), LL14(L"動画などのDirectShow使用時に途中保存を有効にします。", L"Enable mid-playback save for DirectShow (videos, etc.).", L"Activer l'enregistrement pour DirectShow (vidéos).", L"Abilita salvataggio per DirectShow (video).", L"Habilitar guardado para DirectShow (videos).", L"동영상 DirectShow 사용 시 위치 저장을 활성화합니다.", L"DirectShow（视频等）时启用位置保存。", L"تفعيل حفظ الموضع لـ DirectShow (فيديو).", L"Включить сохранение для DirectShow (видео).", L"Für DirectShow (Videos) aktivieren.", L"Habilitar para DirectShow (vídeos).", L"Voor DirectShow (video's) inschakelen.", L"Włącz dla DirectShow (wideo).", L"DirectShow (videolar) için etkinleştir."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO1), LL14(L"プレイリストを変更または追加します。", L"Change or add playlists.", L"Modifier ou ajouter des listes.", L"Cambia o aggiungi playlist.", L"Cambiar o añadir listas.", L"재생 목록을 변경하거나 추가합니다.", L"更改或添加播放列表。", L"تغيير أو إضافة قوائم.", L"Изменить или добавить плейлисты.", L"Playlists ändern oder hinzufügen.", L"Alterar ou adicionar listas.", L"Playlists wijzigen of toevoegen.", L"Zmień lub dodaj listy.", L"Listeleri değiştir veya ekle."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON3), LL14(L"プレイリスト名を変更します。", L"Rename playlist.", L"Renommer la liste.", L"Rinomina playlist.", L"Cambiar nombre de lista.", L"재생 목록 이름을 변경합니다.", L"重命名播放列表。", L"إعادة تسمية القائمة.", L"Переименовать плейлист.", L"Playlist umbenennen.", L"Renomear lista.", L"Playlist hernoemen.", L"Zmień nazwę listy.", L"Liste adını değiştir."));
	m_tooltip.AddTool(GetDlgItem(IDC_PLAYDELETE), LL14(L"表示されているプレイリストを削除します。\n※削除したものは復活できないので注意ください。", L"Delete the displayed playlist.\n*Deleted playlists cannot be recovered.", L"Supprimer la liste affichée.\n*Les listes supprimées ne peuvent pas être récupérées.", L"Elimina la playlist visualizzata.\n*Le playlist eliminate non possono essere recuperate.", L"Eliminar la lista mostrada.\n*Las listas eliminadas no se pueden recuperar.", L"표시된 재생 목록을 삭제합니다.\n*삭제한 목록은 복구할 수 없습니다.", L"删除显示的播放列表。\n*删除后无法恢复。", L"حذف القائمة المعروضة.\n*لا يمكن استرداد القوائم المحذوفة.", L"Удалить отображаемый плейлист.\n*Удалённые плейлисты восстановить нельзя.", L"Angezeigte Playlist löschen.\n*Gelöschte Playlists können nicht wiederhergestellt werden.", L"Excluir lista exibida.\n*Listas excluídas não podem ser recuperadas.", L"Getoonde playlist verwijderen.\n*Verwijderde playlists kunnen niet worden hersteld.", L"Usuń wyświetlaną listę.\n*Usuniętych list nie można odzyskać.", L"Gösterilen listeyi sil.\n*Silinen listeler geri alınamaz."));
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);
//	m_lc.SetMaxTipWidth(500)
	DWORD dwExStyle = m_lc.GetExtendedStyle();
	dwExStyle |= LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES;//|LVS_EX_INFOTIP;
	m_lc.SetExtendedStyle(dwExStyle);
	il.Create(16, 16, ILC_COLOR, 0, 1);
	il.Add(::AfxGetApp()->LoadIcon(IDI_ICON1)); 
	il.Add(::AfxGetApp()->LoadIcon(IDI_ICON2)); 
	il.Add(::AfxGetApp()->LoadIcon(IDI_ICON3)); 
	m_lc.SetImageList(&il,LVSIL_SMALL);
	m_lc.ModifyStyle ( 0, LVS_REPORT );
	m_lc.InsertColumn ( 0, LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"이름", L"名称", L"الاسم", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 1, LL14(L"ゲーム", L"Game", L"Jeu", L"Gioco", L"Juego", L"게임", L"游戏", L"اللعبة", L"Игра", L"Spiel", L"Jogo", L"Spel", L"Gra", L"Oyun"), LVCFMT_LEFT, 50, 0 );
	m_lc.InsertColumn ( 2, LL14(L"時間", L"Time", L"Durée", L"Durata", L"Duración", L"시간", L"时间", L"المدة", L"Время", L"Zeit", L"Duração", L"Tijd", L"Czas", L"Süre"), LVCFMT_RIGHT, 50, 0 );
	m_lc.InsertColumn ( 3, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"아티스트", L"艺术家", L"الفنان", L"Исполнитель", L"Künstler", L"Artista", L"Artiest", L"Artysta", L"Sanatçı"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 4, LL14(L"アルバム/コメント", L"Album/Comment", L"Album/Commentaire", L"Album/Commento", L"Álbum/Comentario", L"앨범/코멘트", L"专辑/注释", L"الألبوم/التعليق", L"Альбом/Комментарий", L"Album/Kommentar", L"Álbum/Comentário", L"Album/Opmerking", L"Album/Komentarz", L"Albüm/Yorum"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 5, LL14(L"フォルダ", L"Folder", L"Dossier", L"Cartella", L"Carpeta", L"폴더", L"文件夹", L"المجلد", L"Папка", L"Ordner", L"Pasta", L"Map", L"Folder", L"Klasör"), LVCFMT_LEFT, 50, 0 );
	m_lc.pc = pc;
//	pc=NULL;
//	pc = (playlistdata0*)malloc(sizeof(playlistdata0)*50000);
//	if(pc==NULL)
//		EndDialog(0);
	m_lc.SetFocus();
	pnt=pnt1=-1;
	nnn=1;
	pc=NULL;

	m_savecheck.SetCheck(savedata.savecheck);
	m_save_mp3.SetCheck(savedata.savecheck_mp3);
	m_save_kpi.SetCheck(savedata.savecheck_dshow);

	loadplaylistname();

	Load();
	if(pc==NULL){
		pc = (playlistdata0*)malloc(sizeof(playlistdata0));
	}
	SetTimer(20,20,NULL);
	SetTimer(3000,1200,NULL);
	SetTimer(40,500,NULL);
	SetTimer(5000,100,NULL);
	SIcon(pnt1);

	CCustomControlUtility::SetControlBackgroundColor(&m_listchange, COLOR_COMBO_BG);

//	CFont pFont;
//	BOOL retfont=pFont.CreateFont(-15,0,0,0,400,0,0,0,128,3,2,1,50,savedata.font2);
//	if(retfont){
//		m_lc.SetFont(&pFont,TRUE);
//		m_find.SetFont(&pFont,TRUE);
//	}
//	pFont.DeleteObject();
//	if(retfont==0)
//		retfont=pFont.CreateFont(0,0,0,0,FW_NORMAL,FALSE,FALSE,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH | FF_SWISS,_T("MS UI Gothic"));
//	if(retfont==0)
//		retfont=pFont.CreateFont(0,0,0,0,FW_NORMAL,FALSE,FALSE,0,ANSI_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH | FF_SWISS,_T("ＭＳ Ｐゴシック"));
//	if(retfont){
//		m_lc.SetFont(&pFont,TRUE);
//		m_find.SetFont(&pFont,TRUE);
//	}
	Invalidate();
	playbase = NULL;
	if (savedata.aero) {
		playbase = new CImageBase;
		playbase->Create(pl);
		playbase->oya = pl;
	}
	CRect r;
	GetWindowRect(&r);
	if(playbase)
		playbase->MoveWindow(&r);

	plw = 1;
	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}
extern int killw1;

void CPlayList::OnNcDestroy()
{
	CCustomDialog::OnNcDestroy();

	// TODO: ここにメッセージ ハンドラ コードを追加します。
	killw1=1;
}

BOOL CPlayList::DestroyWindow()
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
	Save();
//	free(pc);
//	pc=NULL;
//	KillTimer(20);
//	KillTimer(30);
	BOOL rr=CCustomDialog::DestroyWindow();
	pl=NULL;
//	if(nnn)
//		delete this;
	plw=0;
	if(playbase) delete playbase;
	playbase = NULL;
	return rr;
}

int CPlayList::Create(CWnd *pWnd)
{
	 m_pParent = NULL;
	BOOL bret = CCustomDialog::Create( CPlayList::IDD, this);
	if (savedata.aero == 1) {
		ModifyStyleEx(0, WS_EX_LAYERED);

		// レイヤードウィンドウの不透明度と透明のカラーキー
		SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);

		// 赤色のブラシを作成する．
		m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	}
	if (bret == TRUE)
		ShowWindow(SW_SHOW);
	return bret;
}

void CPlayList::OnClose()
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	nnn=0;
	DestroyWindow();

	CCustomDialog::OnClose();
}

void CPlayList::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
//	DestroyWindow();
}

BOOL CPlayList::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
		m_tooltip.RelayEvent(pMsg);

	return CCustomDialog::PreTranslateMessage(pMsg);
}

int pnt1=-1;

int CPlayList::chk(CString name,int sub,CString art,CString fol,int ret)
{
	int i=m_lc.GetItemCount(),c=0;
	pnt1=-1;
	CString s,s1;
	for(int j=0;j<i;j++){
		c=0;
		if ((pc[j].sub == -10) || (pc[j].sub == -2) || (pc[j].sub == -3 || pc[j].sub == 30)) {
			if (_tcscmp(pc[j].fol, fol) == 0 && pc[j].sub == sub && _tcscmp(pc[j].name, name) == 0)
				return j;
		}else{
			if(_tcscmp(pc[j].fol,fol)==0 && pc[j].sub==sub && (pc[j].ret2==ret))
				return j;
		}
	}
	return -1;
}

CString CPlayList::UTF8toSJIS(const char* a)
{
	WCHAR f[1024];
	char ff[1024];
	int rr=MultiByteToWideChar(CP_UTF8,0,a,-1,f,1024);
	int rr2=WideCharToMultiByte(CP_ACP,0,f,rr,ff,0,NULL,NULL);
	WideCharToMultiByte(CP_ACP,0,f,rr,ff,rr2,NULL,NULL);
	CString s; s=f;
	return s;
//	return _T("");
}

CString CPlayList::UTF8toUNI(const TCHAR* a)
{
//	WCHAR f[1024];
//	char ff[1024];
//	int rr2=WideCharToMultiByte(CP_ACP, 0, a,1024,ff,1024,NULL,NULL);
//	int rr= MultiByteToWideChar(CP_UTF8,0,ff,-1,f ,1024);
//	WideCharToMultiByte(CP_ACP,0,f,rr,ff,rr2,NULL,NULL);
	CString s; s=a;
	return s;
//	return _T("");
}

int CPlayList::Add(CString name,int sub,int loop1,int loop2,CString art,CString alb,CString fol,int ret,int time,BOOL f,BOOL ff)
{
	int cnt1;
	CString s,ss;
	switch(sub){
		case 1:s=LL14(L"空の軌跡SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC", L"Sora no Kiseki SC");break;
		case 2:s=LL14(L"空の軌跡FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC", L"Sora no Kiseki FC");break;
		case 3:s=LL14(L"イース フェルガナの誓い", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai", L"Ys Felghana no Chikai");break;
		case 4:s=LL14(L"Ys6 ナピシュテムの匣", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako", L"Ys6 Napishtim no Hako");break;
		case 5:s=LL14(L"イース オリジン", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin", L"Ys Origin");break;
		case 6:s=LL14(L"空の軌跡 The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd", L"Sora no Kiseki The3rd");break;
		case 7:s="ZWEI II";break;
		case 8:s="Ys I&II Chronicles 1";break;
		case 9:s="Ys I&II Chronicles 2";break;
		case 10:s="XANADU NEXT";break;
		case 11:s=LL14(L"Ys I&II 完全版 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1", L"Ys I&II Complete 1");break;
		case 12:s=LL14(L"Ys I&II 完全版 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2", L"Ys I&II Complete 2");break;
		case 13:s="Sorcerian Original";break;
		case 14:s="Zwei!!";break;
		case 15:s=LL14(L"ぐるみん -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-", L"Gurumin -GURUMIN-");break;
		case 16:s=LL14(L"ダイナソア リザレクション", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection", L"Dinosaur Resurrection");break;
		case 17:s=LL14(L"Brandish4 眠れる神の塔", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God", L"Brandish4 Tower of the Sleeping God");break;
		case 18:s=LL14(L"白き魔女", L"White Witch", L"White Witch", L"White Witch", L"White Witch", L"White Witch", L"White Witch", L"White Witch", L"White Witch", L"White Witch", L"White Witch", L"White Witch", L"White Witch", L"White Witch");break;
		case 19:s=LL14(L"朱紅い雫", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears", L"Crimson Tears");break;
		case 20:s=LL14(L"海の檻歌", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean", L"Cagesong of the Ocean");break;
		case 21:s = LL14(_T("閃の軌跡Ⅰ,Ⅱ,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8"), _T("Trails of Cold Steel I,II,Ys8")); break;
		case 30:s = LL14(_T("空の軌跡 The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st"), _T("Trails in the Sky The 1st")); break;
		case -6:s = LL14(_T("閃Ⅲ,Ⅳ,創,零改,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX"), _T("CS III,IV,Reverie,Zero Kai,Ys9,YsX")); break;
		case -11:s=LL14(L"月影のラプソディー", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon", L"Lunacy of the Moon");break;
		case -12:s=LL14(L"西風の狂詩曲", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind", L"Rhapsody of the West Wind");break;
		case -13:s=LL14(L"アークトゥルス", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus", L"Arcturus");break;
		case -14:s=LL14(L"幻想三国志1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1", L"Fantasia Sango 1");break;
		case -15:s=LL14(L"幻想三国志2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2", L"Fantasia Sango 2");break;
		case -3:
			ss=fol.Right(fol.GetLength()-fol.ReverseFind('.')-1);
			s.Format(LL14(_T("%sファイル"), _T("%s File"), _T("%s fichier"), _T("%s file"), _T("%s archivo"), _T("%s 파일"), _T("%s文件"), _T("%s ملف"), _T("%s файл"), _T("%s Datei"), _T("%s arquivo"), _T("%s bestand"), _T("%s plik"), _T("%s dosya")),ss);break;
		case -2:
			ss=fol.Right(fol.GetLength()-fol.ReverseFind('.')-1);
			s.Format(LL14(_T("%sファイル"), _T("%s File"), _T("%s fichier"), _T("%s file"), _T("%s archivo"), _T("%s 파일"), _T("%s文件"), _T("%s ملف"), _T("%s файл"), _T("%s Datei"), _T("%s arquivo"), _T("%s bestand"), _T("%s plik"), _T("%s dosya")),ss);break;
		case -1:s=LL14(L"oggファイル", L"ogg File", L"ogg fichier", L"ogg file", L"ogg archivo", L"ogg 파일", L"ogg文件", L"ogg ملف", L"ogg файл", L"ogg Datei", L"ogg arquivo", L"ogg bestand", L"ogg plik", L"ogg dosya");break;
		case -7:
			s = fol; s.MakeLower();
			if (s.Right(3) == "dsf") { s = LL14(_T("dsfファイル(DSD)"), _T("dsf File(DSD)"), _T("dsf fichier(DSD)"), _T("dsf file(DSD)"), _T("dsf archivo(DSD)"), _T("dsf 파일(DSD)"), _T("dsf文件(DSD)"), _T("dsf ملف(DSD)"), _T("dsf файл(DSD)"), _T("dsf Datei(DSD)"), _T("dsf arquivo(DSD)"), _T("dsf bestand(DSD)"), _T("dsf plik(DSD)"), _T("dsf dosya(DSD)")); break; }
			if (s.Right(3) == "wsd") { s = LL14(_T("wsdファイル(DSD)"), _T("wsd File(DSD)"), _T("wsd fichier(DSD)"), _T("wsd file(DSD)"), _T("wsd archivo(DSD)"), _T("wsd 파일(DSD)"), _T("wsd文件(DSD)"), _T("wsd ملف(DSD)"), _T("wsd файл(DSD)"), _T("wsd Datei(DSD)"), _T("wsd arquivo(DSD)"), _T("wsd bestand(DSD)"), _T("wsd plik(DSD)"), _T("wsd dosya(DSD)")); break; }
			if (s.Right(3) == "dff") { s = LL14(_T("dffファイル(DSD)"), _T("dff File(DSD)"), _T("dff fichier(DSD)"), _T("dff file(DSD)"), _T("dff archivo(DSD)"), _T("dff 파일(DSD)"), _T("dff文件(DSD)"), _T("dff ملف(DSD)"), _T("dff файл(DSD)"), _T("dff Datei(DSD)"), _T("dff arquivo(DSD)"), _T("dff bestand(DSD)"), _T("dff plik(DSD)"), _T("dff dosya(DSD)")); break; }
		case -8:
			s = fol; s.MakeLower();
			if (s.Right(4) == "flac") { s = LL14(_T("flacファイル"), _T("flac File"), _T("flac fichier"), _T("flac file"), _T("flac archivo"), _T("flac 파일"), _T("flac文件"), _T("flac ملف"), _T("flac файл"), _T("flac Datei"), _T("flac arquivo"), _T("flac bestand"), _T("flac plik"), _T("flac dosya")); break; }
			if (s.Right(6).MakeLower() == "qull3h") { s = LL14(_T("Qull3Hファイル"), _T("Qull3H File"), _T("Qull3H fichier"), _T("Qull3H file"), _T("Qull3H archivo"), _T("Qull3H 파일"), _T("Qull3H文件"), _T("Qull3H ملف"), _T("Qull3H файл"), _T("Qull3H Datei"), _T("Qull3H arquivo"), _T("Qull3H bestand"), _T("Qull3H plik"), _T("Qull3H dosya")); break; }
		case -9:
			s = fol; s.MakeLower();
			if (s.Right(3) == "m4a") { s = LL14(_T("m4aファイル"), _T("m4a File"), _T("m4a fichier"), _T("m4a file"), _T("m4a archivo"), _T("m4a 파일"), _T("m4a文件"), _T("m4a ملف"), _T("m4a файл"), _T("m4a Datei"), _T("m4a arquivo"), _T("m4a bestand"), _T("m4a plik"), _T("m4a dosya")); break; }
			if (s.Right(3) == "aac") { s = LL14(_T("aacファイル"), _T("aac File"), _T("aac fichier"), _T("aac file"), _T("aac archivo"), _T("aac 파일"), _T("aac文件"), _T("aac ملف"), _T("aac файл"), _T("aac Datei"), _T("aac arquivo"), _T("aac bestand"), _T("aac plik"), _T("aac dosya")); break; }
		case -10:
			s=fol;s.MakeLower();
			if(s.Right(3)=="mp3"){ s=LL14(L"mp3ファイル", L"mp3 File", L"mp3 fichier", L"mp3 file", L"mp3 archivo", L"mp3 파일", L"mp3文件", L"mp3 ملف", L"mp3 файл", L"mp3 Datei", L"mp3 arquivo", L"mp3 bestand", L"mp3 plik", L"mp3 dosya");break;}
			if(s.Right(3)=="mp2"){ s=LL14(L"mp2ファイル", L"mp2 File", L"mp2 fichier", L"mp2 file", L"mp2 archivo", L"mp2 파일", L"mp2文件", L"mp2 ملف", L"mp2 файл", L"mp2 Datei", L"mp2 arquivo", L"mp2 bestand", L"mp2 plik", L"mp2 dosya");break;}
			if(s.Right(3)=="mp1"){ s=LL14(L"mp1ファイル", L"mp1 File", L"mp1 fichier", L"mp1 file", L"mp1 archivo", L"mp1 파일", L"mp1文件", L"mp1 ملف", L"mp1 файл", L"mp1 Datei", L"mp1 arquivo", L"mp1 bestand", L"mp1 plik", L"mp1 dosya");break;}
			if(s.Right(3)=="rmp"){ s=LL14(L"rmpファイル", L"rmp File", L"rmp fichier", L"rmp file", L"rmp archivo", L"rmp 파일", L"rmp文件", L"rmp ملف", L"rmp файл", L"rmp Datei", L"rmp arquivo", L"rmp bestand", L"rmp plik", L"rmp dosya");break;}
	}

	if(f)
		if((cnt1=chk(name,sub,art,fol,ret))!=-1){
			pc[cnt1].time=time;
			RECT r;
			m_lc.GetItemRect(cnt1,&r,LVIR_BOUNDS);
			m_lc.RedrawWindow(&r);	
			return cnt1;
		}
//	if(playcnt<60000){
		if(ff){
			playlistdata0 *tmp;	tmp=pc;
		size_t size=_msize(pc);
		playlistdata0 *newPc = (playlistdata0*)realloc(tmp, size + sizeof(playlistdata0));
		if (newPc == NULL) {
			pc = tmp;
			return -1;
		}
		pc = newPc;
			m_lc.SetItemCount(playcnt+1);
		}
		_tcscpy(pc[playcnt].name,name);
		_tcscpy(pc[playcnt].art,art);
		_tcscpy(pc[playcnt].alb,alb);
		_tcscpy(pc[playcnt].fol,fol);
		_tcscpy(pc[playcnt].game,s);
		pc[playcnt].loop1=loop1;
		pc[playcnt].loop2=loop2;
		pc[playcnt].sub=sub;
		pc[playcnt].ret2=ret;
		pc[playcnt].icon=1;
		pc[playcnt].time=time;
//		RECT r;
//		m_lc.GetItemRect(playcnt,&r,LVIR_BOUNDS);
//		m_lc.RedrawWindow(&r);	
		playcnt++;
//	}		
		
	return -1;
}

void CPlayList::Del()
{
	int Lindex=-1,j=0;
	for(;;){//選択されているものをピックアップ
		Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
		if(Lindex==-1) break;
		m_lc.SetItemState(Lindex,m_lc.GetItemState(Lindex,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		for(int i=Lindex+1+j;i<playcnt;i++){
			memcpy(&pc[i-1],&pc[i],sizeof(playlistdata0));
		}
		playcnt--;j--;
	}
	playlistdata0 *tmp;	tmp=pc;
	playlistdata0 *newPc = (playlistdata0*)realloc(tmp, (size_t)sizeof(playlistdata0) * (playcnt + 2));
	if (newPc) {
		pc = newPc;
	} else {
		pc = tmp;
	}//余裕を持って解放
	m_lc.SetItemCount(playcnt);
	for(j=0;j<playcnt;j++) pc[j].icon=1;
	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnSUP()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount();
	CString s,s1;
	for(int j=0;j<i-1;j++){
		if((m_lc.GetItemState(j+1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j+1].icon=1;
			memcpy(&ppp,&pc[j+1],sizeof(playlistdata0));
			memcpy(&pc[j+1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j,j+1);
			m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
			m_lc.SetItemState(j+1,m_lc.GetItemState(j+1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
	}
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnUP()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount(),i2=0;
	CString s,s1;
	for(;;){i2=0;
		for(int j=0;j<i-1;j++){
			if((m_lc.GetItemState(j+1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j+1].icon=1;
			memcpy(&ppp,&pc[j+1],sizeof(playlistdata0));
			memcpy(&pc[j+1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j,j+1);
				m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
				m_lc.SetItemState(j+1,m_lc.GetItemState(j+1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
				i2=1;
			}
		}
		if(i2==0) break;
	}	
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnSDOWN()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount(),i2=0;
	CString s,s1;
	for(;;){i2=0;
		for(int j=i-1;j>0;j--){
			if((m_lc.GetItemState(j-1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j+1].icon=1;
			memcpy(&ppp,&pc[j-1],sizeof(playlistdata0));
			memcpy(&pc[j-1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j-1,j);
				m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
				m_lc.SetItemState(j-1,m_lc.GetItemState(j-1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
				i2=1;
			}
		}
		if(i2==0) break;
	}	
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnDOWN()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	int i=m_lc.GetItemCount();
	CString s,s1;
	for(int j=i-1;j>0;j--){
		if((m_lc.GetItemState(j-1,LVIS_SELECTED)&LVIS_SELECTED)&&!(m_lc.GetItemState(j,LVIS_SELECTED)&LVIS_SELECTED)){
			playlistdata0 ppp;
			pc[j].icon=pc[j-1].icon=1;
			memcpy(&ppp,&pc[j-1],sizeof(playlistdata0));
			memcpy(&pc[j-1],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
			m_lc.RedrawItems(j-1,j);

			m_lc.SetItemState(j  ,m_lc.GetItemState(j  ,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
			m_lc.SetItemState(j-1,m_lc.GetItemState(j-1,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
	}
//	m_lc.RedrawWindow();
	Save();
}

void CPlayList::OnXCHG(int i,int j)
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
			playlistdata0 ppp;
			pc[j].icon=pc[j-1].icon=1;
			memcpy(&ppp,&pc[i],sizeof(playlistdata0));
			memcpy(&pc[i],&pc[j],sizeof(playlistdata0));
			memcpy(&pc[j],&ppp,sizeof(playlistdata0));
}

extern COggDlg *og;
extern CString filen,fnn;

extern int modesub,ret2;
extern int loop1, loop2;

void CPlayList::Get(int i)
{
		fnn=pc[i].name; filen=pc[i].fol; modesub=pc[i].sub; loop1=pc[i].loop1; loop2=pc[i].loop2; ret2=pc[i].ret2;
		for(int k=0;k<playcnt;k++){
			m_lc.SetItemState(k,m_lc.GetItemState(k,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
		m_lc.SetItemState(i,LVIS_SELECTED,LVIS_SELECTED);
		SIcon(i);
}

void CPlayList::OnLvnKeydownList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLVKEYDOWN pLVKeyDow = reinterpret_cast<LPNMLVKEYDOWN>(pNMHDR);
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	if(pLVKeyDow->wVKey == VK_DELETE){
		Del();
	}
	*pResult = 0;
}


void CPlayList::OnDropFiles(HDROP hDropInfo)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	TCHAR filen_c[1024];
	syo = 0; syos = ""; syomode = 0;
	int ii=m_lc.GetItemCount();
	UINT cnt = DragQueryFile(hDropInfo,(UINT)-1,filen_c,sizeof(filen_c));
	TCHAR tmp[1024];
	_tgetcwd(tmp,1000);
		for(UINT i=0;i<cnt;i++){
			DragQueryFile(hDropInfo,(UINT)i,filen_c,sizeof(filen_c));
			Fol(filen_c);
		}
	_tchdir(tmp);
	if(syo==1 && (fade1==1 || playf==0) && !pMediaPosition){
		plcnt=ii;
		SIcon(ii);
	}
	if(syo==1 && m_renzoku.GetCheck()==FALSE){
		plcnt=ii;
		SIcon(ii);
		if(PathIsDirectory(syos)==FALSE)
			filen = syos;
		else
			filen = syos + L"\\" + fnn;
		if (syomode == 30) {
			filen = syos;
		}
		og->dp(filen);
	}
	if(syo==1 && pMediaPosition){
		if(mode==-2 || videoonly==TRUE){
			REFTIME aa,bb;
			pMediaPosition->get_CurrentPosition(&aa);
			pMediaPosition->get_Duration(&bb);
			if(aa>=bb){
				if (PathIsDirectory(syos) == FALSE)
					filen = syos;
				else
					filen = syos + L"\\" + fnn;
				og->dp(filen);
			}
		}
	}
	if(syo==1 && (fade1==1 || playf==0) && !pMediaPosition){
		if (PathIsDirectory(syos) == FALSE)
			filen = syos;
		else
			filen = syos + L"\\" + fnn;
		og->dp(filen);
	}
	Save();
	CCustomDialog::OnDropFiles(hDropInfo);
}

#include "Id3tagv1.h"
#include "Id3tagv2.h"

#include "vorbis/codec.h"
#include "vorbis/vorbisfile.h"
OggVorbis_File vf1;
extern BYTE bufimage[0x30000f];

// OggVorbisコールバック関数
extern size_t Callback_Read(
	void* ptr,
	size_t size,
	size_t nmemb,
	void* datasource
);

extern int Callback_Seek(
	void *datasource,
	ogg_int64_t offset,
	int whence
);

extern int Callback_Close(void *datasource);

extern long Callback_Tell(void *datasource);

extern ov_callbacks callbacks;

void CPlayList::Fol(CString fname)
{
	CString fname_full = fname;
	CString fname1 = fname;
	CString ft; 
	ft = "*.*";
	CString ft2;
	if (PathIsDirectory(fname) == FALSE) {
		CString ft1;
		ft1 = fname;
		ft = ft1.Right(ft1.GetLength()-ft1.ReverseFind(L'\\')-1);
	}
	CString s, ss;
	playlistdata p; ZeroMemory(&p, sizeof(p));
	CFileFind f;
	if (PathIsDirectory(fname) == FALSE) {
		CString ff = fname.Left(fname.ReverseFind('\\'));
		_tchdir(ff);
	}
	else {
		_tchdir(fname);
	}
	if (f.FindFile(ft)) {
		int b = 1;
		for (; b;) {
			b = f.FindNextFile();
			s = f.GetFileName();
			if (f.IsDirectory() == 0) {
				fname = fname1;
				BOOL a1 = PathIsDirectory(fname);
				if (a1) {
					fname = fname1 + L"\\" + s;
				}
				else {
					
				}
				//CString ff = fname.Left(fname.ReverseFind('\\'));
				//_tchdir(ff);
				ft = s;
				ft2 = s;
				ft.MakeLower();
				CFile ff2;
				
				ff2.Open(s, CFile::modeRead | CFile::shareDenyNone, NULL);
				ff2.Read(bufimage, 2);
				ff2.Close();
				if (ft.Right(4).MakeLower() == ".ogg" || ft.Right(4) == ".OGG" || ft.Right(6).MakeLower() == ".qull3") {
					p.sub = -1;
					mode = -1;
					FILE *fp;
					fp = _tfopen(fname, _T("rb"));
					if (fp == NULL) {
						return;
					}

					if (ov_open_callbacks(fp, &vf1, NULL, 0, callbacks) < 0) {
						fclose(fp);
						return;
					}
					CString cc;
					_tcscpy(p.name, ft2);
					p.alb[0] = p.art[0] = NULL;
					for (int iii = 0; iii < vf1.vc->comments; iii++) {
#if _UNICODE
						WCHAR f[1024];
						MultiByteToWideChar(CP_UTF8, 0, vf1.vc->user_comments[iii], -1, f, 1024);
						cc = f;
#else
						cc = vf1.vc->user_comments[iii];
#endif
						if (cc.Left(6).MakeUpper() == "TITLE=")
						{
#if _UNICODE
							ss = UTF8toUNI(cc.Mid(6));
#else
							ss = UTF8toSJIS(cc.Mid(6));
#endif
							_tcscpy(p.name, ss);
						}
						if (cc.Left(7).MakeUpper() == "ARTIST=")
						{
#if _UNICODE
							ss = UTF8toUNI(cc.Mid(7));
#else
							ss = UTF8toSJIS(cc.Mid(7));
#endif
							_tcscpy(p.art, ss);
						}
						if (cc.Left(6).MakeUpper() == "ALBUM=")
						{
#if _UNICODE
							ss = UTF8toUNI(cc.Mid(6));
#else
							ss = UTF8toSJIS(cc.Mid(6));
#endif
							_tcscpy(p.alb, ss);
						}
					}
					ov_clear(&vf1);
					fclose(fp);

					//YS8 steam版用　bgmテーブル変換
					CString sss = fname.Left(fname.ReverseFind('\\')); ss = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					_tchdir(sss);
					//ys8用
					CStdioFile f;
					char *buff;
					int looping = 0;
					int igg;

					ss = ss.Left(ss.ReverseFind('.'));
					char file[256];
					WCHAR outcm[1024];
					WideCharToMultiByte(CP_ACP, 0, ss, 1024, file, 256, NULL, NULL);
					FILE *fp2;
					fp2 = _wfopen(L"..\\text\\bgmtbl.tbl", L"r");
					if (fp2) {
						buff = (char*)calloc(256, 1);
						for (;;) {
							if (fgets(buff, 256, fp2) == NULL) {
								free(buff); break;
							}
							char *p1 = strstr(buff, file);
							if (p1 == NULL) continue;
							if (buff[0] == '/') continue;
							p1 += strlen(file) + 1;
							for (; *p1 == 0x09; p1++);
							if (*p1 == '1') looping = 1;
							p1++;
							for (; *p1 == 0x09; p1++);
							typedef struct {
								char st[8];
								char a[1];
								char ed[8];
							} aa;
							aa *aa1;
							aa1 = (aa*)p1;
							int i, j;
							j = 0;
							for (i = 0; i < 8; i++) {
								switch (aa1->st[i])
								{
								case '0':
									j *= 10; j += 0;
									break;
								case '1':
									j *= 10; j += 1;
									break;
								case '2':
									j *= 10; j += 2;
									break;
								case '3':
									j *= 10; j += 3;
									break;
								case '4':
									j *= 10; j += 4;
									break;
								case '5':
									j *= 10; j += 5;
									break;
								case '6':
									j *= 10; j += 6;
									break;
								case '7':
									j *= 10; j += 7;
									break;
								case '8':
									j *= 10; j += 8;
									break;
								case '9':
									j *= 10; j += 9;
									break;
								}
							}
							loop1 = j;
							j = 0;
							for (i = 0; i < 8; i++) {
								switch (aa1->ed[i])
								{
								case '0':
									j *= 10; j += 0;
									break;
								case '1':
									j *= 10; j += 1;
									break;
								case '2':
									j *= 10; j += 2;
									break;
								case '3':
									j *= 10; j += 3;
									break;
								case '4':
									j *= 10; j += 4;
									break;
								case '5':
									j *= 10; j += 5;
									break;
								case '6':
									j *= 10; j += 6;
									break;
								case '7':
									j *= 10; j += 7;
									break;
								case '8':
									j *= 10; j += 8;
									break;
								case '9':
									j *= 10; j += 9;
									break;
								}
							}
							loop2 = j - loop1;
							p1 += sizeof(aa) + 1;
							for (; *p1 == 0x09; p1++);
							p1 += 3;
							char* pp = p1;
							for (; *p1 != 0xd; p1++) {
								if (*p1 == 0x9) {
									*p1 = 0x20;
								}
							}
							p1 = pp;
							MultiByteToWideChar(CP_ACP, 0, p1, -1, outcm, 1024);
							ss = outcm;
							_tcscpy(p.name, ss.Trim());
							if (looping == 0) {
								loop1 = loop2 = 0;
							}
							free(buff); break;
						}
						fclose(fp2);
					}

					//YSC
					sss = fname.Left(fname.ReverseFind('\\'));
					_tchdir(sss);
					ss = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					if (ss == "yc_b001.ogg") {
						ss = LL14(L"バトル#58", L"Battle #58", L"Combat #58", L"Battaglia #58", L"Batalla #58", L"전투 #58", L"战斗 #58", L"معركة #58", L"Сражение #58", L"Kampf #58", L"Batalha #58", L"Gevecht #58", L"Bitwa #58", L"Savaş #58");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b002.ogg") {
						ss = LL14(L"灼熱の炎の中で", L"Within the Blazing Flames", L"Dans les flammes ardentes", L"Tra le fiamme ardenti", L"Entre las llamas ardientes", L"타오르는 불길 속에서", L"在灼熱的火焰中", L"في لهيب مشتعل", L"В раскаленном пламени", L"In den lodernden Flammen", L"Nas chamas ardentes", L"In de brandende vlammen", L"W płonących płomieniach", L"Yanan Alevlerin İçinde");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b003.ogg") {
						ss = LL14(L"最終決戦", L"Final Battle", L"Bataille finale", L"Battaglia finale", L"Batalla final", L"최종 결전", L"最終決戰", L"المعركة النهائية", L"Финальная битва", L"Letzter Kampf", L"Batalha final", L"Laatste gevecht", L"Ostateczna bitwa", L"Son Savaş");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b004.ogg") {
						ss = LL14(L"黒き翼", L"Black Wings", L"Ailes noires", L"Ali nere", L"Alas negras", L"검은 날개", L"黑色翅膀", L"أجنحة سوداء", L"Черные крылья", L"Schwarze Flügel", L"Asas negras", L"Zwarte vleugels", L"Czarne skrzydła", L"Siyah Kanatlar");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b005.ogg") {
						ss = "The False God of Causality";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d101.ogg") {
						ss = LL14(L"ダンジョン", L"Dungeon", L"Donjon", L"Sotterraneo", L"Mazmorra", L"던전", L"迷宮", L"زنزانة", L"Подземелье", L"Kerker", L"Masmorra", L"Kerker", L"Loch", L"Zindan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d201.ogg") {
						ss = LL14(L"道化師の誘い", L"Clown's Invitation", L"Invitation du bouffon", L"Invito del clown", L"Invitación del payaso", L"광대의 유혹", L"小丑的引誘", L"دعوة المهرج", L"Приглашение клоуна", L"Einladung des Clowns", L"Convite do palhaço", L"Uitnodiging van de clown", L"Zaproszenie błazna", L"Palyaçonun Daveti");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d301.ogg") {
						ss = LL14(L"地下遺跡", L"Underground Ruins", L"Ruines souterraines", L"Rovine sotterranee", L"Ruinas subterráneas", L"지하 유적", L"地下遺跡", L"أطلال تحت الأرض", L"Подземные руины", L"Unterirdische Ruinen", L"Ruínas subterrâneas", L"Ondergrondse ruïnes", L"Podziemne ruiny", L"Yeraltı Harabeleri");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d401.ogg") {
						ss = LL14(L"導きの塔～エルディールにくちづけを", L"Tower of Guidance -Kiss for Eldeel-", L"Tour de guidance -Un baiser pour Eldeel-", L"Torre della guida -Un bacio per Eldeel-", L"Torre de guía -Un beso para Eldeel-", L"인도의 탑 ~ Eldeel에게 입맞춤을", L"引導之塔～給 Eldeel 的吻", L"برج التوجيه - قبلة لـ Eldeel", L"Башня наставления -Поцелуй для Eldeel-", L"Turm der Führung -Kuss für Eldeel-", L"Torre de Orientação -Beijo para Eldeel-", L"Toren van begeleiding -Kus voor Eldeel-", L"Wieża przewodnictwa -Pocałunek dla Eldeel-", L"Rehberlik Kulesi -Eldeel için Bir Öpücük-");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d501.ogg") {
						ss = LL14(L"失われし仮面を求めて", L"Seeking the Lost Mask", L"À la recherche du masque perdu", L"Alla ricerca della maschera perduta", L"Buscando la máscara perdida", L"잃어버린 가면을 찾아서", L"尋找失落的面具", L"البحث عن القناع المفقود", L"В поисках утраченной маски", L"Auf der Suche nach der verlorenen Maske", L"Em busca da máscara perdida", L"Op zoek naar het verloren masker", L"W poszukiwaniu zagubionej maski", L"Kayıp Maskenin Peşinde");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d701.ogg") {
						ss = LL14(L"イリス", L"Iris", L"Iris", L"Iris", L"Iris", L"이리스", L"伊莉絲", L"Iris", L"Iris", L"Iris", L"Iris", L"Iris", L"Iris", L"Iris");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d702.ogg") {
						ss = "yc_d702";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d703.ogg") {
						ss = LL14(L"聖域", L"Sanctuary", L"Sanctuaire", L"Santuario", L"Santuario", L"성역", L"聖域", L"ملاذ", L"Святилище", L"Heiligtum", L"Santuário", L"Heiligdom", L"Sanktuarium", L"Kutsal Alan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e001.ogg") {
						ss = LL14(L"賢者", L"Sage", L"Sage", L"Saggio", L"Sabio", L"현자", L"賢者", L"حكيم", L"Мудрец", L"Weiser", L"Sábio", L"Wijze", L"Mędrzec", L"Bilge");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e002.ogg") {
						ss = LL14(L"復活の儀式", L"Resurrection Ceremony", L"Cérémonie de résurrection", L"Cerimonia di resurrezione", L"Ceremonia de resurrección", L"부활의 의식", L"復活的儀式", L"مراسم القيامة", L"Церемония воскрешения", L"Auferstehungszeremonie", L"Cerimônia de ressurreição", L"Opstandingsceremonie", L"Ceremonia wskrzeszenia", L"Diriliş Töreni");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e003.ogg") {
						ss = LL14(L"レファンス", L"Refance", L"Refance", L"Refance", L"Refance", L"레판스", L"雷凡斯", L"Refance", L"Refance", L"Refance", L"Refance", L"Refance", L"Refance", L"Refance");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e004.ogg") {
						ss = LL14(L"涙の少年剣士", L"Young Swordsman in Tears", L"Jeune épéiste en larmes", L"Giovane spadaccino in lacrime", L"Joven espadachín en lágrimas", L"눈물의 소년 검사", L"流淚的少年劍士", L"سياف شاب باكٍ", L"Юный мечник в слезах", L"Junger Schwertkämpfer in Tränen", L"Jovem espadachim em lágrimas", L"Jonge zwaardvechter in tranen", L"Młody szermierz we łzach", L"Gözü Yaşlı Genç Kılıç Ustası");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e005.ogg") {
						ss = LL14(L"エルディール", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"엘딜", L"艾爾迪爾", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e006.ogg") {
						ss = LL14(L"ロムン帝国 -嗚呼レオ団長-", L"Romun Empire -Alas Captain Leo-", L"Empire de Romun -Hélas Capitaine Leo-", L"Impero di Romun -Ahimè Capitano Leo-", L"Imperio de Romun -Ay, Capitán Leo-", L"Romun 제국 ~아아 레오 단장~", L"Romun 帝國 -嗚呼里歐團長-", L"إمبراطورية Romun -يا للأسف أيها القائد Leo-", L"Империя Romun -Увы, капитан Leo-", L"Romun Reich -Ach, Kapitän Leo-", L"Império de Romun -Ai, Capitão Leo-", L"Romun-rijk -Helaas Kapitein Leo-", L"Imperium Romun -Ach, Kapitanie Leo-", L"Romun İmparatorluğu -Vah Yüzbaşı Leo-");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e008.ogg") {
						ss = "yc_e008";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e010.ogg") {
						ss = LL14(L"冒険家、誕生", L"Birth of an Adventurer", L"Naissance d'un aventurier", L"Nascita di un avventuriero", L"Nacimiento de un aventurero", L"모험가 탄생", L"冒險家誕生", L"ولادة مغامر", L"Рождение искателя приключений", L"Geburt eines Abenteurers", L"Nascimento de um aventureiro", L"Geboorte van een avonturier", L"Narodziny poszukiwacza przygód", L"Bir Maceracının Doğuşu");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f101.ogg") {
						ss = LL14(L"燃ゆる剣", L"Burning Sword", L"Épée brûlante", L"Spada ardente", L"Espada ardiente", L"불타는 검", L"燃燒之劍", L"السيف المشتعل", L"Пылающий меч", L"Brennendes Schwert", L"Espada flamejante", L"Brandend zwaard", L"Płonący miecz", L"Yanan Kılıç");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f201.ogg") {
						ss = LL14(L"セルセタの樹海", L"Forest of Celceta", L"Forêt de Celceta", L"Foresta di Celceta", L"Bosque de Celceta", L"Celceta의 수해", L"Celceta 的樹海", L"غابة Celceta", L"Лес Celceta", L"Wald von Celceta", L"Floresta de Celceta", L"Woud van Celceta", L"Las Celceta", L"Celceta Ormanı");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f301.ogg") {
						ss = LL14(L"クレーター", L"Crater", L"Cratère", L"Cratere", L"Cráter", L"크레이터", L"火山口", L"فوهة البركان", L"Кратер", L"Krater", L"Cratera", L"Krater", L"Krater", L"Krater");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f401.ogg") {
						ss = "THE DAWN OF YS";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f501.ogg") {
						ss = LL14(L"暁の森", L"Forest of Dawn", L"Forêt de l'aube", L"Foresta dell'alba", L"Bosque del alba", L"새벽의 숲", L"曉之森", L"غابة الفجر", L"Лес рассвета", L"Wald der Dämmerung", L"Floresta da aurora", L"Woud van de dageraad", L"Las świtu", L"Şafak Ormanı");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f601.ogg") {
						ss = LL14(L"一陣の風", L"Gust of Wind", L"Une rafale de vent", L"Raffica di vento", L"Ráfaga de viento", L"한 줄기 바람", L"一陣風", L"عاصفة من الرياح", L"Порыв ветра", L"Windstoß", L"Rajada de vento", L"Windvlaag", L"Podmuch wiatru", L"Bir Rüzgar Esintisi");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f701.ogg") {
						ss = LL14(L"神代の地", L"Land of the Gods", L"Terre des dieux", L"Terra degli dei", L"Tierra de los dioses", L"신의 시대의 땅", L"神代之地", L"ارض الالهة", L"Земля богов", L"Land der Götter", L"Terra dos deuses", L"Land van de goden", L"Kraina bogów", L"Tanrıların Diyarı");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f801.ogg") {
						ss = LL14(L"真実への序曲", L"Overture to Truth", L"Ouverture vers la vérité", L"Ouverture alla verità", L"Obertura a la verdad", L"진실을 향한 서곡", L"通往真実的序曲", L"مقدمة الحقيقة", L"Увертюра к истине", L"Ouvertüre zur Wahrheit", L"Prelúdio para a verdade", L"Ouverture naar de waarheid", L"Uwertura do prawdy", L"Gerçeğe Uvertür");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f901.ogg") {
						ss = LL14(L"雨上がりの朝に", L"Morning After the Rain", L"Le matin après la pluie", L"Mattina dopo la pioggia", L"Mañana después de la lluvia", L"비 갠 아침에", L"雨過天晴的早晨", L"الصباح بعد المطر", L"Утро после дождя", L"Morgen nach dem Regen", L"Manhã após a chuva", L"Ochtend na de regen", L"Poranek po deszczu", L"Yağmur Sonrası Sabah");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_over.ogg") {
						ss = LL14(L"ゲームオーバー", L"Game Over", L"Fin de partie", L"Fine del gioco", L"Juego terminado", L"게임 오버", L"遊戲結束", L"انتهت اللعبة", L"Конец игры", L"Spiel vorbei", L"Fim de jogo", L"Game over", L"Koniec gry", L"Oyun Bitti");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t101.ogg") {
						ss = LL14(L"辺境都市《キャスナン》", L"Frontier City Casnan", L"Ville frontalière Casnan", L"Città di confine Casnan", L"Ciudad fronteriza Casnan", L"변방 도시 Casnan", L"邊境都市 Casnan", L"مدينة Casnan الحدودية", L"Пограничный город Casnan", L"Grenzstadt Casnan", L"Cidade fronteiriça Casnan", L"Grensstad Casnan", L"Graniczne miasto Casnan", L"Sınır Şehri Casnan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t201.ogg") {
						ss = LL14(L"優しくなりたい", L"I Want to Be Kind", L"Je veux être gentil", L"Voglio essere gentile", L"Quiero ser amable", L"상냥해지고 싶어", L"想要變得溫柔", L"أريد أن أكون طيباً", L"Я хочу быть добрым", L"Ich möchte gütig sein", L"Eu quero ser gentil", L"Ik wil vriendelijk zijn", L"Chcę być miły", L"Nazik Olmak İstiyorum");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t301.ogg") {
						ss = LL14(L"古代の伝承", L"Ancient Legend", L"Légende ancienne", L"Antica leggenda", L"Leyenda antigua", L"고대의 전승", L"古代的傳承", L"الأسطورة القديمة", L"Древняя легенда", L"Alte Legende", L"Lenda antiga", L"Oude legende", L"Starożytna legenda", L"Kadim Efsane");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t501.ogg") {
						ss = "RODA";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_title.ogg") {
						ss = "THEME OF ADOL 2012";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_op.ogg") {
						ss = "The Foliage Ocean in CELCETA -Opening size-";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_end.ogg") {
						ss = LL14(L"新たな時代のステージへ", L"To the Stage of a New Era", L"Vers l'étape d'une nouvelle ère", L"Verso il palcoscenico di una nuova era", L"Hacia el escenario de una nueva era", L"새로운 시대의 스테이지로", L"邁向新時代的舞台", L"إلى مرحلة عصر جديد", L"На сцену новой эры", L"Auf die Bühne einer neuen Ära", L"Para o palco de uma nova era", L"Naar het podium van een nieuw tijdperk", L"Do etapu nowej ery", L"Yeni Bir Çağın Sahnesine");
						_tcscpy(p.name, ss);
					}

					//zero 
					CString ss;
					ss = fname.Right(fname.GetLength() - fname.ReverseFind(L'\\') - 1);
					sss = fname.Left(fname.ReverseFind('\\'));
					int fg = 0;
					CFile ffff;
					if (ffff.Open(sss + L"\\..\\text\\t_bgm._dt", CFile::modeRead)) { fg = 1; ffff.Close(); }
					CString zero = savedata.zero;
					if(zero != L"") if (ffff.Open(savedata.zero, CFile::modeRead)) { fg = 1; ffff.Close(); }
					CString a;
					if (ss.Mid(0, 3) == L"ed7" && fg == 1) {
						switch (_ttoi(ss.Mid(2, 4))) {
						case 7001:
							a = LL14(L"零の軌跡", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"제로의 궤적", L"零之軌跡", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero");
							break;
						case 7002:
							a = L"way of live -Opening Version-";
							break;
						case 7003:
							a = LL14(L"新しき日々～予兆", L"New Days -Omen-", L"Jours nouveaux -Présage-", L"Nuovi giorni -Presagio-", L"Nuevos días -Presagio-", L"새로운 나날 ~전조", L"嶄新的日子～預兆", L"أيام جديدة - نذير", L"Новые дни -Предзнаменование-", L"Neue Tage -Vorbote-", L"Novos dias -Augúrio-", L"Nieuwe dagen -Voorteken-", L"Nowe dni -Zwiastun-", L"Yeni Günler -Kehanet-");
							break;
						case 7005:
							a = LL14(L"想い破れて・・・", L"Broken Heart...", L"Cœur brisé...", L"Cuore infranto...", L"Corazón roto...", L"깨어진 마음...", L"心碎・・・", L"قلب مكسور...", L"Разбитое сердце...", L"Gebrochenes Herz...", L"Coração partido...", L"Gebroken hart...", L"Złamane serce...", L"Kırık Kalp...");
							break;
						case 7052:
							a = LL14(L"碧い軌跡 -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"푸른 궤적 -Opening size-", L"碧之軌跡 -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-");
							break;
						case 7053:
							a = LL14(L"それでも僕らは。", L"Yet We're Still Here.", L"Pourtant, nous sommes là.", L"Eppure siamo ancora qui.", L"Sin embargo, seguimos aquí.", L"그럼에도 우리들은.", L"即便如此我們依舊。", L"ومع ذلك، ما زلنا هنا.", L"И все же мы здесь.", L"Doch wir sind noch hier.", L"Ainda assim, estamos aqui.", L"Toch zijn we er nog.", L"A jednak wciąż tu jesteśmy.", L"Yine de Buradayız.");
							break;
						case 7100:
							a = LL14(L"街角の風景", L"Street Corner Scenery", L"Paysage au coin de la rue", L"Paesaggio all'angolo della strada", L"Paisaje de esquina", L"길거리의 풍경", L"街角風景", L"منظر زاوية الشارع", L"Пейзаж на углу улицы", L"Straßeneckenszenerie", L"Cenário de esquina", L"Straathoektafereel", L"Krajobraz na rogu ulicy", L"Sokak Köşesi Manzarası");
							break;
						case 7101:
							a = LL14(L"明日は明日の風が吹く", L"Tomorrow the Wind Will Blow", L"Demain, le vent soufflera", L"Domani soffierà il vento", L"Mañana soplará el viento", L"내일은 내일의 바람이 분다", L"明天吹明天的風", L"غداً ستهب الرياح", L"Завтра подует ветер", L"Morgen weht der Wind", L"Amanhã o vento soprará", L"Morgen waait de wind", L"Jutro zawieje wiatr", L"Yarın Rüzgar Esecek");
							break;
						case 7102:
							a = LL14(L"クロスベルの午後", L"Afternoon in Crossbell", L"Après-midi à Crossbell", L"Pomeriggio a Crossbell", L"Tarde en Crossbell", L"Crossbell의 오후", L"Crossbell 的午後", L"بعد ظهر اليوم في Crossbell", L"Полдень в Crossbell", L"Nachmittag in Crossbell", L"Tarde em Crossbell", L"Middag in Crossbell", L"Popołudnie w Crossbell", L"Crossbell'da Öğleden Sonra");
							break;
						case 7103:
							a = L"During Mission Accomplishment";
							break;
						case 7104:
							a = LL14(L"創立記念祭", L"Founding Festival", L"Festival de la fondation", L"Festival della fondazione", L"Festival de la fundación", L"창립 기념제", L"創立紀念祭", L"مهرجان التأسيس", L"Фестиваль основания", L"Gründungsfest", L"Festival de fundação", L"Oprichtingsfestival", L"Festiwal założycielski", L"Kuruluş Festivali");
							break;
						case 7105:
							a = LL14(L"降水確率10%", L"10% Chance of Rain", L"10% de chances de pluie", L"10% di probabilità di pioggia", L"10% de probabilidad de lluvia", L"강수확률 10%", L"降雨機率10%", L"احتمال هطول الأمطار 10%", L"10% вероятность дождя", L"10% Regenwahrscheinlichkeit", L"10% de chance de chuva", L"10% kans op regen", L"10% szans na deszcz", L"10% Yağmur Olasılığı");
							break;
						case 7106:
							a = LL14(L"風船と紙吹雪", L"Balloons and Confetti", L"Ballons et confettis", L"Palloncini e coriandoli", L"Globos y confeti", L"풍선과 종이꽃가루", L"氣球與五彩碎紙", L"بالونات وقصاصات ورق", L"Воздушные шары и конфетти", L"Luftballons und Konfetti", L"Balões e confetes", L"Ballonnen en confetti", L"Balony i konfetti", L"Balonlar ve Konfetiler");
							break;
						case 7110:
							a = LL14(L"特務支援課", L"Special Support Section", L"Section de soutien spécial", L"Sezione di supporto speciale", L"Sección de apoyo especial", L"특무지원과", L"特務支援課", L"قسم الدعم الخاص", L"Секция специальной поддержки", L"Spezielle Unterstützungsabteilung", L"Seção de Apoio Especial", L"Speciale ondersteuningssectie", L"Specjalna Sekcja Wsparcia", L"Özel Destek Bölümü");
							break;
						case 7111:
							a = LL14(L"C.S.P.D. -クロスベル警察", L"C.S.P.D. -Crossbell Police", L"C.S.P.D. -Police de Crossbell", L"C.S.P.D. -Polizia di Crossbell", L"C.S.P.D. -Policía de Crossbell", L"C.S.P.D. -Crossbell 경찰", L"C.S.P.D. -Crossbell 警察", L"C.S.P.D. -شرطة Crossbell", L"C.S.P.D. -Полиция Crossbell", L"C.S.P.D. -Polizei von Crossbell", L"C.S.P.D. -Polícia de Crossbell", L"C.S.P.D. -Politie van Crossbell", L"C.S.P.D. -Policja Crossbell", L"C.S.P.D. -Crossbell Polisi");
							break;
						case 7113:
							a = L"Arc-en-ciel";
							break;
						case 7114:
							a = LL14(L"黒月貿易公司", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue 무역공사", L"黑月貿易公司", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company");
							break;
						case 7116:
							a = L"IGNIS";
							break;
						case 7117:
							a = L"TRINITY";
							break;
						case 7120:
							a = LL14(L"アルモリカ村", L"Armorica Village", L"Village d'Armorica", L"Villaggio di Armorica", L"Aldea Armorica", L"Armorica 마을", L"阿爾摩利卡村", L"قرية Armorica", L"Деревня Armorica", L"Dorf Armorica", L"Vila Armorica", L"Dorp Armorica", L"Wioska Armorica", L"Armorica Köyü");
							break;
						case 7121:
							a = LL14(L"鉱山町マインツ", L"Mines Town Mainz", L"Ville minière Mainz", L"Città mineraria Mainz", L"Ciudad minera Mainz", L"광산 마을 Mainz", L"礦山鎮 Mainz", L"بلدة المناجم Mainz", L"Шахтерский городок Mainz", L"Bergbaustadt Mainz", L"Cidade mineira Mainz", L"Mijnstad Mainz", L"Górnicze miasto Mainz", L"Maden Kasabası Mainz");
							break;
						case 7122:
							a = L"Killing Bear";
							break;
						case 7123:
							a = LL14(L"聖ウルスラ医科大学", L"St. Ursula Medical College", L"Collège médical Ste Ursule", L"Collegio medico S. Orsola", L"Colegio Médico Sta. Úrsula", L"성 우르술라 의과대학", L"聖烏爾蘇拉醫科大學", L"كلية سانت أورسولا الطبية", L"Медицинский колледж Св. Урсулы", L"Medizinische Hochschule St. Ursula", L"Faculdade de Medicina Sta. Úrsula", L"Medisch College St. Ursula", L"Kolegium Medyczne św. Urszuli", L"Aziz Ursula Tıp Koleji");
							break;
						case 7124:
							a = LL14(L"クロスベル大聖堂", L"Crossbell Cathedral", L"Cathédrale de Crossbell", L"Cattedrale di Crossbell", L"Catedral de Crossbell", L"Crossbell 대성당", L"Crossbell 大聖堂", L"كاتدرائية Crossbell", L"Собор Crossbell", L"Kathedrale von Crossbell", L"Catedral de Crossbell", L"Kathedraal van Crossbell", L"Katedra w Crossbell", L"Crossbell Katedrali");
							break;
						case 7125:
							a = LL14(L"黒の競売会", L"Black Auction", L"Enchères noires", L"Asta nera", L"Subasta negra", L"검은 경매회", L"黑色拍賣會", L"المزاد الأسود", L"Черный аукцион", L"Schwarze Auktion", L"Leilão negro", L"Zwarte veiling", L"Czarna aukcja", L"Siyah Müzayede");
							break;
						case 7126:
							a = LL14(L"大国にはさまれて", L"Caught Between Nations", L"Pris entre les nations", L"Incastrato tra le nazioni", L"Atrapado entre naciones", L"대국 사이에 끼어서", L"夾在大国之間", L"علق بين الدول", L"Зажатый между странами", L"Gefangen zwischen den Nationen", L"Preso entre nações", L"Gevangen tussen de naties", L"Uwięziony między narodami", L"Uluslar Arasında Sıkışmış");
							break;
						case 7150:
							a = LL14(L"新たなる日常", L"New Daily Life", L"Nouvelle vie quotidienne", L"Nuova vita quotidiana", L"Nueva vida cotidiana", L"새로운 일상", L"嶄新的日常", L"حياة يومية جديدة", L"Новая повседневная жизнь", L"Neuer Alltag", L"Nova vida cotidiana", L"Nieuw dagelijks leven", L"Nowe życie codzienne", L"Yeni Günlük Yaşam");
							break;
						case 7151:
							a = LL14(L"動き始めた事態", L"Events in Motion", L"Événements en mouvement", L"Eventi in movimento", L"Eventos en movimiento", L"움직이기 시작한 사태", L"開始動作的事態", L"الأحداث في حركة", L"События в движении", L"Ereignisse in Bewegung", L"Eventos em movimento", L"Gebeurtenissen in beweging", L"Wydarzenia w toku", L"Harekete Geçen Olaylar");
							break;
						case 7160:
							a = LL14(L"ミシュラムワンダーランド", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland");
							break;
						case 7161:
							a = LL14(L"束の間の休息", L"Brief Respite", L"Bref répit", L"Breve tregua", L"Breve respiro", L"잠시 동안의 휴식", L"短暫的休息", L"استراحة قصيرة", L"Краткая передышка", L"Kurze Atempause", L"Breve descanso", L"Korte adempauze", L"Krótkie wytchnienie", L"Kısa Bir Mola");
							break;
						case 7162:
							a = LL14(L"ささやかな晩餐", L"Simple Dinner", L"Dîner simple", L"Cena semplice", L"Cena sencilla", L"소박한 만찬", L"簡單的晚餐", L"عشاء بسيط", L"Простой ужин", L"Einfaches Abendessen", L"Jantar simples", L"Eenvoudig diner", L"Skromna kolacja", L"Sade Bir Akşam Yemeği");
							break;
						case 7200:
							a = LL14(L"水と草木と青い空", L"Water, Trees and Blue Sky", L"Eau, arbres et ciel bleu", L"Acqua, alberi e cielo blu", L"Agua, árboles y cielo azul", L"물과 초목과 푸른 하늘", L"水、草木與藍天", L"ماء وأشجار وسماء زرقاء", L"Вода, деревья и синее небо", L"Wasser, Bäume und blauer Himmel", L"Água, árvores e céu azul", L"Water, bomen en blauwe lucht", L"Woda, drzewa i błękitne niebo", L"Su, Ağaçlar ve Mavi Gökyüzü");
							break;
						case 7201:
							a = LL14(L"片手にはレモネード", L"Lemonade in One Hand", L"Une limonade à la main", L"Una limonata in mano", L"Limonada en una mano", L"한 손에는 레모네이드", L"手裡拿著檸檬水", L"ليمونادة في يد واحدة", L"С лимонадом в одной руке", L"Limonade in einer Hand", L"Limonada em uma mão", L"Limonade in één hand", L"Lemoniada w jednej ręce", L"Bir Elde Limonata");
							break;
						case 7202:
							a = LL14(L"木霊の道", L"Path of Echoes", L"Chemin des échos", L"Sentiero degli echi", L"Senda de los ecos", L"메아리의 길", L"木靈之路", L"مسار الأصداء", L"Путь эха", L"Pfad des Echos", L"Caminho dos ecos", L"Pad van echo's", L"Ścieżka ech", L"Yankıların Yolu");
							break;
						case 7203:
							a = LL14(L"古の鼓動", L"Ancient Pulse", L"Pouls ancien", L"Battito antico", L"Pulso antiguo", L"고대의 고동", L"古之鼓動", L"نبض قديم", L"Древний пульс", L"Uralter Puls", L"Pulso antigo", L"Eeuwenoude hartslag", L"Starożytne tętno", L"Kadim Nabız");
							break;
						case 7204:
							a = L"On The Green Road";
							break;
						case 7205:
							a = LL14(L"鉄橋を越えて", L"Crossing the Iron Bridge", L"Traverser le pont de fer", L"Attraversando il ponte di ferro", L"Cruzando el puente de hierro", L"철교를 넘어서", L"越過鐵橋", L"عبور الجسر الحديدي", L"Пересекая железный мост", L"Über die Eisenbrücke", L"Atravessando a ponte de ferro", L"De ijzeren brug oversteken", L"Przez żelazny most", L"Demir Köprüyü Geçerken");
							break;
						case 7250:
							a = LL14(L"木洩れ日の中の静寂", L"Tranquility in the Dappled Light", L"Tranquillité dans la lumière tamisée", L"Tranquillità nella luce filtrata", L"Tranquilidad en la luz moteada", L"나뭇잎 사이로 비치는 햇살 속의 정적", L"林間陽光中的寧靜", L"السكينة في الضوء المرقش", L"Спокойствие в бликах света", L"Ruhe im gefleckten Licht", L"Tranquilidade na luz salpicada", L"Rust in het gespikkelde licht", L"Spokój w rozproszonym świetle", L"Süzülen Işık Altındaki Huzur");
							break;
						case 7251:
							a = LL14(L"偽りの楽土を越えて", L"Beyond the False Paradise", L"Au-delà du faux paradis", L"Oltre il falso paradiso", L"Más allá del falso paraíso", L"거짓된 낙토를 넘어서", L"越過虛偽的樂土", L"ما وراء الجنة المزيفة", L"За пределами ложного рая", L"Jenseits des falschen Paradieses", L"Além do falso paraíso", L"Voorbij het valse paradijs", L"Poza fałszywym rajem", L"Sahte Cennetin Ötesinde");
							break;
						case 7300:
							a = LL14(L"ジオフロント", L"Geofront", L"Geofront", L"Geofront", L"Geofront", L"지오프런트", L"地下空間", L"Geofront", L"Геофронт", L"Geofront", L"Geofront", L"Geofront", L"Geofront", L"Geofront");
							break;
						case 7301:
							a = LL14(L"七耀の煌き", L"Septium Radiance", L"Éclat de Septium", L"Splendore del Septium", L"Resplandor de Septium", L"칠요의 광채", L"七耀之輝", L"تألق Septium", L"Сияние Септиума", L"Septium-Glanz", L"Resplendor de Septium", L"Septium-glans", L"Blask Septium", L"Septium Parıltısı");
							break;
						case 7302:
							a = LL14(L"ルバーチェ商会", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache 상회", L"魯巴徹商會", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company");
							break;
						case 7303:
							a = LL14(L"鳴るはずのない鐘", L"The Bell That Shouldn't Ring", L"La cloche qui ne devrait pas sonner", L"La campana que non dovrebbe suonare", L"La campana que no debería sonar", L"울릴 리 없는 종", L"不該響起的鐘", L"الجرس الذي لا ينبغي أن يرن", L"Колокол, который не должен звонить", L"Die Glocke, die nicht läuten sollte", L"O sino que não deveria tocar", L"De klok die niet mag luiden", L"Dzwon, który nie powinien bić", L"Çalmaması Gereken Çan");
							break;
						case 7304:
							a = LL14(L"忘れられし幻夢の狭間", L"Forgotten Phantasmal Gap", L"L'écart phantasmatique oublié", L"Divario fantasmatico dimenticato", L"Brecha fantasmal olvidada", L"잊혀진 환몽의 틈새", L"被遺忘的幻夢狹間", L"الفجوة الخيالية المنسية", L"Забытый призрачный разрыв", L"Vergessener phantasmagorischer Spalt", L"Fenda fantasmal esquecida", L"Vergeten fantoomkloof", L"Zapomniana fantastyczna szczelina", L"Unutulmuş Hayali Boşluk");
							break;
						case 7305:
							a = L"A Light Illuminating The Depths";
							break;
						case 7350:
							a = LL14(L"Dの残影", L"D's Shadow", L"L'ombre de D", L"L'ombra di D", L"La sombra de D", L"D의 잔영", L"D的殘影", L"ظل D", L"Тень D", L"Ds Schatten", L"Sombra de D", L"D's schaduw", L"Cień D", L"D'nin Gölgesi");
							break;
						case 7351:
							a = LL14(L"異変の兆し", L"Omen of Change", L"Présage de changement", L"Presagio di cambiamento", L"Presagio de cambio", L"이변의 조짐", L"異變的徵兆", L"نذير التغيير", L"Предзнаменование перемен", L"Vorbote der Veränderung", L"Augúrio de mudança", L"Voorteken van verandering", L"Zwiastun zmian", L"Değişim Kehaneti");
							break;
						case 7352:
							a = L"Mystic Core";
							break;
						case 7353:
							a = LL14(L"最果ての樹", L"Tree at World's End", L"L'arbre au bout du monde", L"L'albero alla fine del mondo", L"Árbol del fin del mundo", L"최후의 나무", L"最果て之樹", L"شجرة في نهاية العالم", L"Древо на краю света", L"Baum am Ende der Welt", L"Árvore no fim do mundo", L"Boom aan het einde van de wereld", L"Drzewo na końcu świata", L"Dünyanın Ucundaki Ağaç");
							break;
						case 7354:
							a = LL14(L"暴魔の呼び声", L"Call of the Beast", L"L'appel de la bête", L"Il richiamo della bestia", L"La llamada de la bestia", L"폭마의 부름", L"暴魔的呼喚", L"نداء الوحش", L"Зов зверя", L"Ruf der Bestie", L"O chamado da besta", L"Roep van het beest", L"Zew bestii", L"Canavarın Çağrısı");
							break;
						case 7356:
							a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
							break;
						case 7400:
							a = L"Get Over The Barrier!";
							break;
						case 7401:
							a = L"Arrest The Criminal";
							break;
						case 7402:
							a = L"Formidable Enemy";
							break;
						case 7403:
							a = L"Stand Up Battle Formation Again!";
							break;
						case 7404:
							a = L"Inevitable Struggle";
							break;
						case 7405:
							a = L"Demonic Drive";
							break;
						case 7406:
							a = L"Arrival Existence";
							break;
						case 7408:
							a = LL14(L"これが俺たちの力だ!", L"This Is Our Power!", L"C'est notre pouvoir!", L"Questo è il nostro potere!", L"¡Este es nuestro poder!", L"이것이 우리들의 힘이다!", L"這就是我們的力量！", L"هذه هي قوتنا!", L"Это наша сила!", L"Das ist unsere Macht!", L"Este é o nosso poder!", L"Dit is onze kracht!", L"To jest nasza moc!", L"Bu Bizim Gücümüz!");
							break;
						case 7450:
							a = L"Seize The Truth!";
							break;
						case 7451:
							a = L"Concentrate All Firepower!!";
							break;
						case 7452:
							a = L"Conflicting Passions";
							break;
						case 7453:
							a = L"Unexpected Emergency";
							break;
						case 7454:
							a = L"Mythtic Roar";
							break;
						case 7455:
							a = L"Destruction Impulse";
							break;
						case 7458:
							a = L"Unfathomed Force";
							break;
						case 7459:
							a = L"The Azure Arbitrator";
							break;
						case 7460:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
							break;
						case 7500:
							a = LL14(L"金の太陽、銀の月　-陽の熱情", L"Golden Sun, Silver Moon -Solar Passion-", L"Soleil d'or, lune d'argent -Passion solaire-", L"Sole d'oro, luna d'argento -Passione solare-", L"Sol de oro, luna de plata -Pasión solar-", L"금의 태양, 은의 달 -태양의 열정", L"金之太陽、銀之月 -陽之熱情", L"شمس ذهبية، قمر فضي - شغف شمسي", L"Золотое солнце, серебряная луна -Солнечная страсть-", L"Goldene Sonne, silberner Mond -Solare Leidenschaft-", L"Sol dourado, lua prateada -Paixão solar-", L"Gouden zon, zilveren maan -Zonnepassie-", L"Złote słońce, srebrny księżyc -Słoneczna pasja-", L"Altın Güneş, Gümüş Ay -Güneş Tutkusu-");
							break;
						case 7501:
							a = LL14(L"金の太陽、銀の月　-月の慕情", L"Golden Sun, Silver Moon -Lunar Affection-", L"Soleil d'or, lune d'argent -Affection lunaire-", L"Sole d'oro, luna d'argento -Affetto lunare-", L"Sol de oro, luna de plata -Afecto lunar-", L"금의 태양, 은의 달 -달의 모정", L"金之太陽、銀之月 -月之慕情", L"شمس ذهبية، قمر فضي - عاطفة قمرية", L"Золотое солнце, серебряная луна -Лунная привязанность-", L"Goldene Sonne, silberner Mond -Lunare Zuneigung-", L"Sol dourado, lua prateada -Afeição lunar-", L"Gouden zon, zilveren maan -Maangenegenheid-", L"Złote słońce, srebrny księżyc -Księżycowe uczucie-", L"Altın Güneş, Gümüş Ay -Ay Şefkati-");
							break;
						case 7502:
							a = LL14(L"金の太陽、銀の月　-童心", L"Golden Sun, Silver Moon -Innocence-", L"Soleil d'or, lune d'argent -Innocence-", L"Sole d'oro, luna d'argento -Innocenza-", L"Sol de oro, luna de plata -Inocencia-", L"금의 태양, 은의 달 -동심", L"金之太陽、銀之月 -童心", L"شمس ذهبية، قمر فضي - براءة", L"Золотое солнце, серебряная луна -Невинность-", L"Goldene Sonne, silberner Mond -Unschuld-", L"Sol dourado, lua prateada -Inocência-", L"Gouden zon, zilveren maan -Onschuld-", L"Złote słońce, srebrny księżyc -Niewinność-", L"Altın Güneş, Gümüş Ay -Masumiyet-");
							break;
						case 7503:
							a = LL14(L"金の太陽、銀の月　-運命の刻", L"Golden Sun, Silver Moon -Hour of Fate-", L"Soleil d'or, lune d'argent -L'heure du destin-", L"Sole d'oro, luna d'argento -L'ora del destino-", L"Sol de oro, luna de plata -La hora del destino-", L"금의 태양, 은의 달 -운명의 시간", L"金之太陽、銀之月 -命運之刻", L"شمس ذهبية، قمر فضي - ساعة القدر", L"Золотое солнце, серебряная луна -Час судьбы-", L"Goldene Sonne, silberner Mond -Stunde des Schicksals-", L"Sol dourado, lua prateada -Hora do destino-", L"Gouden zon, zilveren maan -Uur van het lot-", L"Złote słońce, srebrny księżyc -Godzina losu-", L"Altın Güneş, Gümüş Ay -Kader Saati-");
							break;
						case 7504:
							a = LL14(L"金の太陽、銀の月　-譲れぬ想い", L"Golden Sun, Silver Moon -Unyielding Feelings-", L"Soleil d'or, lune d'argent -Sentiments inébranlables-", L"Sole d'oro, luna d'argento -Sentimenti incrollabili-", L"Sol de oro, luna de plata -Sentimientos inquebrantables-", L"금의 태양, 은의 달 -양보할 수 없는 마음", L"金之太陽、銀之月 -不容讓步的思念", L"شمس ذهبية، قمر فضي - مشاعر لا تتزعزع", L"Золотое солнце, серебряная луна -Непоколебимые чувства-", L"Goldene Sonne, silberner Mond -Unbeugsame Gefühle-", L"Sol dourado, lua prateada -Sentimentos inabaláveis-", L"Gouden zon, zilveren maan -Onwrikbare gevoelens-", L"Złote słońce, srebrny księżyc -Nieustępliwe uczucia-", L"Altın Güneş, Gümüş Ay -Sarsılmaz Duygular-");
							break;
						case 7505:
							a = LL14(L"金の太陽、銀の月　-幾千の夜を越えて", L"Golden Sun, Silver Moon -Beyond Countless Nights-", L"Soleil d'or, lune d'argent -Au-delà d'innombrables nuits-", L"Sole d'oro, luna d'argento -Oltre innumerevoli notti-", L"Sol de oro, luna de plata -Más allá de incontables noches-", L"금의 태양, 은의 달 -수많은 밤을 넘어서", L"金之太陽、銀之月 -跨越數千個夜晚", L"شمس ذهبية، قمر فضي - عبر ليالٍ لا تحصى", L"Золотое солнце, серебряная луна -Сквозь бесчисленные ночи-", L"Goldene Sonne, silberner Mond -Jenseits zahlloser Nächte-", L"Sol dourado, lua prateada -Além de incontáveis noites-", L"Gouden zon, zilveren maan -Voorbij talloze nachten-", L"Złote słońce, srebrny księżyc -Poza niezliczone noce-", L"Altın Güneş, Gümüş Ay -Sayısız Gecenin Ötesinde-");
							break;
						case 7506:
							a = LL14(L"金の太陽、銀の月　-夜明け～大団円", L"Golden Sun, Silver Moon -Dawn to Grand Finale-", L"Soleil d'or, lune d'argent -De l'aube au grand final-", L"Sole d'oro, luna d'argento -Dall'alba al gran finale-", L"Sol de oro, luna de plata -Del amanecer al gran final-", L"금의 태양, 은의 달 -새벽 ~ 대단원", L"金之太陽、銀之月 -黎明～大團圓", L"شمس ذهبية، قمر فضي - من الفجر إلى النهاية الكبرى", L"Золотое солнце, серебряная луна -От рассвета до финала-", L"Goldene Sonne, silberner Mond -Morgengrauen bis zum Finale-", L"Sol dourado, lua prateada -Do amanhecer ao grande final-", L"Gouden zon, zilveren maan -Dageraad tot grote finale-", L"Złote słońce, srebrny księżyc -Od świtu do wielkiego finału-", L"Altın Güneş, Gümüş Ay -Şafaktan Büyük Finale-");
							break;
						case 7507:
							a = L"Intense Chase";
							break;
						case 7509:
							a = LL14(L"守りぬく意志", L"Unyielding Will", L"Volonté inébranlable", L"Volontà incrollabile", L"Voluntad inquebrantable", L"지켜내려는 의지", L"守護到底的意志", L"إرادة لا تتزعزع", L"Непоколебимая воля", L"Unbeugsamer Wille", L"Vontade inabalável", L"Onwrikbare wil", L"Nieugięta wola", L"Sarsılmaz İrade");
							break;
						case 7510:
							a = LL14(L"叡智への誘い", L"Invitation to Wisdom", L"Invitation à la sagesse", L"Invito alla saggezza", L"Invitación a la sabiduría", L"예지로의 유혹", L"智之引誘", L"دعوة للحكمة", L"Приглашение к мудрости", L"Einladung zur Weisheit", L"Convite à sabedoria", L"Uitnodiging tot wijsheid", L"Zaproszenie do mądrości", L"Bilgeliğe Davet");
							break;
						case 7511:
							a = LL14(L"危地", L"Perilous Ground", L"Terrain périlleux", L"Terreno pericoloso", L"Terreno peligroso", L"위지", L"危地", L"أرض محفوفة بالمخاطر", L"Опасная земля", L"Gefährlicher Boden", L"Terreno perigoso", L"Gevaarlijk terrein", L"Niebezpieczny teren", L"Tehlikeli Bölge");
							break;
						case 7512:
							a = LL14(L"揺るぎない強さ", L"Unshakable Strength", L"Force inébranlable", L"Forza incrollabile", L"Fuerza inquebrantable", L"흔들림 없는 강함", L"動搖不得的強大", L"قوة لا تتزعزع", L"Непоколебимая сила", L"Unerschütterliche Stärke", L"Força inabalável", L"Onwankelbare kracht", L"Niezachwiana siła", L"Sarsılmaz Güç");
							break;
						case 7513:
							a = LL14(L"夜景に霞む星空", L"Starry Sky in the Night", L"Ciel étoilé dans la nuit", L"Cielo stellato nella notte", L"Cielo estrellado en la noche", L"야경에 가려진 별하늘", L"夜景中朦朧的星空", L"سماء مرصعة بالنجوم في الليل", L"Звездное небо в ночи", L"Sternenhimmel in der Nacht", L"Céu estrelado na noite", L"Sterrenhemel in de nacht", L"Gwieździste niebo nocą", L"Geceleyin Yıldızlı Gökyüzü");
							break;
						case 7514:
							a = LL14(L"いつかきっと", L"Someday", L"Un jour", L"Un giorno", L"Algún día", L"언젠가 반드시", L"總有一天必定", L"يوماً ما", L"Когда-нибудь", L"Irgendwann", L"Algum dia", L"Ooit", L"Pewnego dnia", L"Bir Gün Mutlaka");
							break;
						case 7515:
							a = LL14(L"柔らかな心", L"Tender Heart", L"Cœur tendre", L"Cuore tenero", L"Corazón tierno", L"부드러운 마음", L"柔軟的心", L"قلب حنون", L"Нежное сердце", L"Zartes Herz", L"Coração terno", L"Zachtmoedig hart", L"Czułe serce", L"Yumuşak Kalp");
							break;
						case 7516:
							a = LL14(L"点と線", L"Dots and Lines", L"Points et lignes", L"Punti e linee", L"Puntos y líneas", L"점과 선", L"點與線", L"نقاط وخطوط", L"Точки и линии", L"Punkte und Linien", L"Pontos e linhas", L"Punten en lijnen", L"Punkty i linie", L"Noktalar ve Çizgiler");
							break;
						case 7517:
							a = LL14(L"一触即発", L"Imminent Crisis", L"Crise imminente", L"Crisi imminente", L"Crisis inminente", L"일촉즉발", L"一觸即發", L"أزمة وشيكة", L"Неизбежный кризис", L"Drohende Krise", L"Crise iminente", L"Dreigende crisis", L"Bliska kryzysu", L"An Meselesi");
							break;
						case 7518:
							a = L"Foolish Gig";
							break;
						case 7519:
							a = LL14(L"リベールからの風", L"Wind from Liberl", L"Vent de Liberl", L"Vento da Liberl", L"Viento de Liberl", L"Liberl로부터의 바람", L"來自 Liberl 的風", L"رياح من Liberl", L"Ветер из Liberl", L"Wind aus Liberl", L"Vento de Liberl", L"Wind uit Liberl", L"Wiatr z Liberl", L"Liberl'den Gelen Rüzgar");
							break;
						case 7520:
							a = LL14(L"とどいた想い", L"Feelings Delivered", L"Sentiments livrés", L"Sentimenti consegnati", L"Sentimientos entregados", L"닿은 마음", L"傳達到的思念", L"مشاعر وصلت", L"Доставленные чувства", L"Angekommene Gefühle", L"Sentimentos entregues", L"Bereikte gevoelens", L"Dostarczone uczucia", L"Ulaşan Duygular");
							break;
						case 7521:
							a = L"Underground Kids";
							break;
						case 7522:
							a = L"Terminal Room";
							break;
						case 7523:
							a = LL14(L"響きあう心", L"Resonating Hearts", L"Cœurs résonnants", L"Cuori risonanti", L"Corazones resonantes", L"공명하는 마음", L"共鳴之心", L"قلوب مرنة", L"Резонирующие сердца", L"Resonierende Herzen", L"Corações ressonantes", L"Resonerende harten", L"Rezonujące serca", L"Yankılanan Kalpler");
							break;
						case 7524:
							a = L"Limit Break";
							break;
						case 7525:
							a = LL14(L"パラダイスミ☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"파라다임☆", L"典範☆", L"نموذج☆", L"Парадигма☆", L"Paradigma☆", L"Paradigma☆", L"Paradigma☆", L"Paradygmat☆", L"Paradigma☆");
							break;
						case 7526:
							a = L"Gnosis";
							break;
						case 7527:
							a = L"Get Over The Barrier! -Roaring Version-";
							break;
						case 7528:
							a = LL14(L"それぞれの明日", L"Our Tomorrows", L"Nos demains", L"I nostri domani", L"Nuestros mañanas", L"각자의 내일", L"各自的明天", L"غدنا", L"Наши завтра", L"Unsere Morgen", L"Nossos amanhãs", L"Onze morgens", L"Nasze jutra", L"Her Birimizin Yarını");
							break;
						case 7529:
							a = LL14(L"効果音楽1", L"Sound Effect Music 1", L"Musique d'effet 1", L"Musica effetto 1", L"Música de efecto 1", L"효과음악 1", L"效果音樂 1", L"موسيقى تأثير 1", L"Музыка эффекта 1", L"Effektmusik 1", L"Música de efeito 1", L"Effectmuziek 1", L"Muzyka efektowa 1", L"Efekt Müziği 1");
							break;
						case 7530:
							a = LL14(L"効果音楽2", L"Sound Effect Music 2", L"Musique d'effet 2", L"Musica effetto 2", L"Música de efecto 2", L"효과음악 2", L"效果音樂 2", L"موسيقى تأثير 2", L"Музыка эффекта 2", L"Effektmusik 2", L"Música de efeito 2", L"Effectmuziek 2", L"Muzyka efektowa 2", L"Efekt Müziği 2");
							break;
						case 7531:
							a = LL14(L"効果音楽3", L"Sound Effect Music 3", L"Musique d'effet 3", L"Musica effetto 3", L"Música de efecto 3", L"효과음악 3", L"效果音樂 3", L"موسيقى تأثير 3", L"Музыка эффекта 3", L"Effektmusik 3", L"Música de efeito 3", L"Effectmuziek 3", L"Muzyka efektowa 3", L"Efekt Müziği 3");
							break;
						case 7532:
							a = LL14(L"効果音楽4", L"Sound Effect Music 4", L"Musique d'effet 4", L"Musica effetto 4", L"Música de efecto 4", L"효과음악 4", L"效果音樂 4", L"موسيقى تأثير 4", L"Музыка эффекта 4", L"Effektmusik 4", L"Música de efeito 4", L"Effectmuziek 4", L"Muzyka efektowa 4", L"Efekt Müziği 4");
							break;
						case 7533:
							a = LL14(L"踏み出す勇気", L"Courage to Step Forward", L"Courage d'avancer", L"Coraggio di farsi avanti", L"Coraje para dar un paso adelante", L"딛고 나아가는 용기", L"踏出一步的勇氣", L"الشجاعة للتقدم للأمام", L"Смелость сделать шаг вперед", L"Mut zum Vorwärtsschritt", L"Coragem para dar um passo à frente", L"Moed om vooruit te stappen", L"Odwaga, by iść naprzód", L"İleri Adım Atma Cesareti");
							break;
						case 7534:
							a = LL14(L"その背中を見つめて", L"Watching Your Back", L"Regarder ton dos", L"Guardando le tue spalle", L"Mirando tu espalda", L"그 등뒤를 바라보며", L"凝視著那背影", L"مشاهدة ظهرك", L"Глядя тебе в спину", L"Deinen Rücken im Blick", L"Olhando para as suas costas", L"Je rug in de gaten houden", L"Patrząc na twoje plecy", L"Sırtını İzlerken");
							break;
						case 7540:
						case 7541:
						case 7542:
						case 7543:
						case 7544:
							a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
							break;
						case 7550:
							a = LL14(L"オルキスタワー", L"Orchis Tower", L"Tour Orchis", L"Torre Orchis", L"Torre Orchis", L"Orchis Tower", L"Orchis Tower", L"برج الأوركيد", L"Башня Орхидея", L"Orchis-Turm", L"Torre Orchis", L"Orchis-toren", L"Wieża Orchis", L"Orchis Kulesi");
							break;
						case 7551:
							a = L"Catastrophe";
							break;
						case 7552:
							a = LL14(L"碧き雫", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"푸른 물방울", L"碧之雫", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator");
							break;
						case 7553:
							a = LL14(L"神機降臨", L"Divine Mechanoid Descent", L"Descente du mécanoïde divin", L"Discesa del meccanoide divino", L"Descenso del mechón divino", L"신기강림", L"神機降臨", L"هبوط ميكانويد إلهي", L"Схождение божественного механоида", L"Abstieg des göttlichen Mechanoids", L"Descida do mecanoide divino", L"Neerdaling van de goddelijke mechanoïde", L"Zstąpienie boskiego mechanoida", L"İlahi Mekanoid İnişi");
							break;
						case 7554:
							a = LL14(L"ふるわれる奇蹟", L"Shaking Miracle", L"Miracle ébranlé", L"Miracolo tremante", L"Milagro tembloroso", L"휘둘리는 기적", L"被展現的奇蹟", L"معجزة مهتزة", L"Дрожащее чудо", L"Erschütterndes Wunder", L"Milagre tremendo", L"Schuddend wonder", L"Drżący cud", L"Sarsılan Mucize");
							break;
						case 7555:
							a = LL14(L"予定外の奇蹟", L"Unexpected Miracle", L"Miracle inattendu", L"Miracolo inaspettato", L"Milagro inesperado", L"예정 밖의 기적", L"意料之外的奇蹟", L"معجزة غير متوقعة", L"Неожиданное чудо", L"Unerwartetes Wunder", L"Milagre inesperado", L"Onverwacht wonder", L"Nieoczekiwany cud", L"Beklenmedik Mucize");
							break;
						case 7556:
							a = LL14(L"鋼鉄の咆哮 -脅威-", L"Roar of Steel -Threat-", L"Rugissement de l'acier -Menace-", L"Ruggito d'acciaio -Minaccia-", L"Rugido de acero -Amenaza-", L"강철의 포효 ~위협~", L"鋼鐵的咆哮 -威脅-", L"زئير الفولاذ - تهديد", L"Рев стали -Угроза-", L"Brüllen aus Stahl -Bedrohung-", L"Rugido de aço -Ameaça-", L"Gebrul van staal -Dreiging-", L"Ryk stali -Zagrożenie-", L"Çeliğin Kükreyişi -Tehdit-");
							break;
						case 7560:
							a = LL14(L"雨の日の真実", L"Truth on a Rainy Day", L"Vérité un jour de pluie", L"Verità in un giorno di pioggia", L"Verdad en un día lluvioso", L"비 오는 날의 진실", L"下雨天的真相", L"الحقيقة في يوم ممطر", L"Правда в дождливый день", L"Wahrheit an einem Regentag", L"Verdade em um dia chuvoso", L"Waarheid op een regenachtige dag", L"Prawda w deszczowy dzień", L"Yağmurlu Bir Gündeki Gerçek");
							break;
						case 7561:
							a = LL14(L"不穏", L"Troubled", L"Troublé", L"Inquieto", L"Inquieto", L"불온", L"不穩", L"مضطرب", L"Тревожный", L"Unruhig", L"Perturbado", L"Onrustig", L"Niespokojny", L"Huzursuz");
							break;
						case 7562:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
							break;
						case 7563:
							a = LL14(L"犠牲の先の希望", L"Hope Beyond Sacrifice", L"Espoir au-delà du sacrifice", L"Speranza oltre il sacrificio", L"Esperanza más allá del sacrificio", L"희생 끝의 희망", L"犧牲之後的希望", L"الأمل وراء التضحية", L"Надежда после жертвы", L"Hoffnung über das Opfer hinaus", L"Esperança além do sacrifício", L"Hoop voorbij opoffering", L"Nadzieja poza ofiarą", L"Fedakarlığın Ötesindeki Umut");
							break;
						case 7564:
							a = L"Strange Feel";
							break;
						case 7565:
							a = L"Exhilarating Ride";
							break;
						case 7566:
							a = LL14(L"それぞれの正義", L"Each One's Justice", L"Chacun sa justice", L"Ognuno la sua giustizia", L"La justicia de cada uno", L"각자의 정의", L"各自的正義", L"عدالة كل واحد", L"Правосудие каждого", L"Die Gerechtigkeit jedes Einzelnen", L"A justiça de cada um", L"Ieders eigen rechtvaardigheid", L"Sprawiedliwość każdego z nas", L"Her Birimizin Adaleti");
							break;
						case 7567:
							a = LL14(L"乗り越えるべき壁", L"Wall to Overcome", L"Mur à surmonter", L"Muro da superare", L"Muro que superar", L"넘어야 할 벽", L"應當越過的障礙", L"جدار يجب التغلب عليه", L"Стена, которую нужно преодолеть", L"Mauer, die es zu überwinden gilt", L"Muro a superar", L"Muur om te overwinnen", L"Mur do pokonania", L"Aşılması Gereken Duvar");
							break;
						case 7568:
							a = LL14(L"月下の想い", L"Feelings Under the Moon", L"Sentiments sous la lune", L"Sentimenti sotto la luna", L"Sentimientos bajo la luna", L"달밤의 상념", L"月下思念", L"مشاعر تحت القمر", L"Чувства под луной", L"Gefühle unter dem Mond", L"Sentimentos sob a lua", L"Gevoelens onder de maan", L"Uczucia pod księżycem", L"Ay Altındaki Duygular");
							break;
						case 7569:
							a = L"Miss You";
							break;
						case 7570:
							a = LL14(L"天の車", L"Chariot of Heaven", L"Char du ciel", L"Carro del cielo", L"Carro del cielo", L"하늘의 수레", L"天之車", L"عربة السماء", L"Небесная колесница", L"Himmelswagen", L"Carruagem do céu", L"Hemelwagen", L"Rydwan niebios", L"Göklerin Arabası");
							break;
						case 7571:
							a = LL14(L"突きつけられた現実", L"Reality Thrust Upon Us", L"La réalité nous est imposée", L"Realtà imposta su di noi", L"Realidad impuesta a nosotros", L"들이닥친 현실", L"擺在眼前的現實", L"الواقع المفروض علينا", L"Реальность, навязанная нам", L"Uns aufgezwungene Realität", L"Realidade imposta a nós", L"Realiteit ons opgedrongen", L"Rzeczywistość nam narzucona", L"Bize Dayatılan Gerçeklik");
							break;
						case 7572:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
							break;
						case 7573:
							a = LL14(L"全てを識るもの", L"The Omniscient", L"L'omniscient", L"L'onniscente", L"El omnisciente", L"모든 것을 아는 자", L"全知者", L"العليم", L"Всеведущий", L"Der Allwissende", L"O onisciente", L"De alwetende", L"Wszechwiedzący", L"Her Şeyi Bilen");
							break;
						case 7574:
							a = LL14(L"想い、辿り着く場所", L"Where Feelings Lead", L"Là où les sentiments mènent", L"Dove portano i sentimenti", L"Donde los sentimientos conducen", L"마음이 가닿는 곳", L"思念抵達之處", L"حيث تقود المشاعر", L"Куда ведут чувства", L"Wohin Gefühle führen", L"Onde os sentimentos levam", L"Waar gevoelens toe leiden", L"Gdzie prowadzą uczucia", L"Duyguların Gittiği Yer");
							break;
						case 7575:
							a = LL14(L"揺れ動く心", L"Wavering Heart", L"Cœur vacillant", L"Cuore incostante", L"Corazón vacilante", L"동요하는 마음", L"動揺的心", L"قلب متردد", L"Колеблющееся сердце", L"Wankendes Herz", L"Coração vacilante", L"Wankelend hart", L"Chwiejne serce", L"Kararsız Kalp");
							break;
						case 7576:
							a = LL14(L"星降る夜に", L"On a Starry Night", L"Par une nuit étoilée", L"In una notte stellata", L"En una noche estrellada", L"별이 내리는 밤에", L"在星辰降落之夜", L"في ليلة مرصعة بالنجوم", L"Звездной ночью", L"In einer Sternennacht", L"Em uma noite estrelada", L"Op een sterrennacht", L"W gwieździstą noc", L"Yıldızlı Bir Gecede");
							break;
						case 7577:
						case 7578:
						case 7579:
						case 7580:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
							break;
						case 7581:
							a = LL14(L"本当の絆", L"True Bonds", L"Vrais liens", L"Veri legami", L"Verdaderos vínculos", L"진정한 인연", L"真正的羈絆", L"روابط حقيقية", L"Истинные узы", L"Wahre Bande", L"Laços verdadeiros", L"Echte banden", L"Prawdziwe więzi", L"Gerçek Bağlar");
							break;
						case 7582:
							a = LL14(L"猛き獣たち", L"Fierce Beasts", L"Bêtes féroces", L"Bestie feroci", L"Bestias feroces", L"사나운 짐승들", L"猛獸們", L"وحوش ضارية", L"Свирепые звери", L"Wilde Bestien", L"Bestas ferozes", L"Woeste beesten", L"Wściekłe bestie", L"Vahşi Canavarlar");
							break;
						case 7583:
							a = LL14(L"西ゼムリア通商会議", L"West Zemuria Trade Conference", L"Conférence commerciale de Zemuria Ouest", L"Conferenza commerciale della Zemuria occidentale", L"Conferencia comercial de Zemuria Occidental", L"서제무리아 통상회의", L"西塞姆利亞通商會議", L"مؤتمر غرب Zemuria التجاري", L"Западно-земурийская торговая конференция", L"West-Zemuria-Handelskonferenz", L"Conferência Comercial de Zemuria Ocidental", L"Handelsconferentie West-Zemuria", L"Konferencja handlowa Zachodniej Zemurii", L"Batı Zemurya Ticaret Konferansı");
							break;
						case 7584:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
							break;
						case 7585:
							a = LL14(L"千年の妄執", L"Obsession of Millennia", L"Obsession des millénaires", L"Ossessione dei millenni", L"Obsesión de milenios", L"천 년의 망집", L"千年的妄執", L"هوس الألفية", L"Одержимость тысячелетий", L"Obsession der Jahrtausende", L"Obsessão milenar", L"Obsessie van millennia", L"Obsesja tysiącleci", L"Bin Yıllık Takıntı");
							break;
						case 7586:
							a = LL14(L"鋼鉄の咆哮 -死線-", L"Roar of Steel -Death Line-", L"Rugissement de l'acier -Ligne de mort-", L"Ruggito d'acciaio -Linea di morte-", L"Rugido de acero -Línea de muerte-", L"강철의 포효 ~사선~", L"鋼鐵的咆哮 -死線-", L"زئير الفولاذ - خط الموت", L"Рев стали -Линия смерти-", L"Brüllen aus Stahl -Todeslinie-", L"Rugido de aço -Linha de morte-", L"Gebrul van staal -Dodslijn-", L"Ryk stali -Linia śmierci-", L"Çeliğin Kükreyişi -Ölüm Çizgisi-");
							break;
						case 7587:
							a = LL14(L"ポムっと! -お花見団子の逆襲-", L"Pom! -Cherry Blossom Dango Counterattack-", L"Pom! -Contre-attaque des dango fleurs de cerisier-", L"Pom! -Contrattacco del dango ai fiori di ciliegio-", L"¡Pom! -Contraataque del dango de flor de cerezo-", L"Pom! ~꽃구경 경단의 역습~", L"Pom! -花見糰子的逆襲-", L"Pom! - هجوم دانغو أزهار الكرز المضاد", L"Pom! -Контратака данго с вишневым цветом-", L"Pom! -Gegenangriff der Kirschblüten-Dango-", L"Pom! -Contra-ataque do dango de flor de cerejeira-", L"Pom! -Tegenstoot van de kersenbloesemdango-", L"Pom! -Kontratak dango z kwiatami wiśni-", L"Pom! -Kiraz Çiçeği Dango'nun Karşı Atağı-");
							break;
						case 7588:
							a = LL14(L"Fateful Confrontation -ポムっと! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Confrontation fatidique -Pom! Ver.-", L"Confronto fatidico -Pom! Ver.-", L"Confrontación fatídica -¡Pom! Ver.-", L"운명의 대결 -Pom! Ver.-", L"命運的對決 -Pom! Ver.-", L"المواجهة المصيرية - Pom! Ver.", L"Судьбоносное противостояние -Pom! Ver.-", L"Schicksalhafte Konfrontation -Pom! Ver.-", L"Confronto fatídico -Pom! Ver.-", L"Noodlottige confrontatie -Pom! Ver.-", L"Fatalna konfrontacja -Pom! Ver.-", L"Kader Anı -Pom! Ver.-");
							break;
						case 7589:
							a = LL14(L"ポムりますか", L"Shall We Pom?", L"Allons-nous Pom?", L"Vogliamo Pommare?", L"¿Hacemos Pom?", L"Pom 하시겠습니까", L"要來 Pom 一下嗎", L"هل نقوم بـ Pom؟", L"Сыграем в Pom?", L"Sollen wir Pom?", L"Vamos Pom?", L"Zullen we Pommen?", L"Zagramy w Pom?", L"Pom Yapalım mı?");
							break;
						case 7690:
							a = LL14(L"エリィ絶叫コースター", L"Elie Scream Coaster", L"Montagnes russes hurlantes d'Elie", L"Ottovolante urlante di Elie", L"Montaña rusa de gritos de Elie", L"Elie의 비명 코스터", L"艾莉尖叫雲霄飛車", L"أفعوانية صراخ Elie", L"Американские горки крика Elie", L"Elies Schreiachterbahn", L"Montanha-russa de gritos da Elie", L"Elie's schreeuwachtbaan", L"Kolejka krzyku Elie", L"Elie'nin Çığlık Treni");
							break;
						case 7591:
							a = LL14(L"小さな英雄 -オルゴール-", L"Little Hero -Music Box-", L"Petit héros -Boîte à musique-", L"Piccolo eroe -Carillon-", L"Pequeño héroe -Caja de música-", L"작은 영웅 -오르골-", L"小小的英雄 -八音盒-", L"بطل صغير - صندوق موسيقى", L"Маленький герой -Музыкальная шкатулка-", L"Kleiner Held -Spieluhr-", L"Pequeno herói -Caixa de música-", L"Kleine held -Muziekdoos-", L"Mały bohater -Pozytywka-", L"Küçük Kahraman -Müzik Kutusu-");
							break;
						case 7592:
							a = L"TOWER OF THE SHADOW OF DEATH -Jukebox-";
							break;
						}
						_tcscpy(p.name, a);
					}
					_tcscpy(p.fol, fname1);
					p.loop1 = p.loop2 = 0;
						}
				else if (fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1).Mid(0,3) == L"ed8" && (ft.Right(4) == ".wav")) {
					p.sub = 21; p.loop1 = p.loop2 = 0;
					CString a = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					switch (_ttoi(a.Mid(2, 4))) {
					case 8001:
						a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"특과 클래스 《VII組》", L"特科班《VII組》", L"الفصل السابع", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klas VII", L"Klasa VII", L"Sınıf VII");
						break;
					case 8002:
						a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours vers l'avant", L"Sempre avanti", L"Siempre adelante", L"오로지 앞으로", L"一心一意，向前邁進", L"إلى الأمام دائماً", L"Только вперед", L"Immer vorwärts", L"Sempre em frente", L"Altijd vooruit", L"Zawsze do przodu", L"Daima İleri");
						break;
					case 8100:
						a = LL14(L"近郊都市トリスタ", L"Suburban City Trista", L"Ville suburbaine Trista", L"Città suburbana Trista", L"Ciudad suburbana Trista", L"근교 도시 Trista", L"近郊都市 Trista", L"مدينة Trista الضواحي", L"Prigorodnyj gorod Trista", L"Vorstadt Trista", L"Cidade suburbana Trista", L"Voorstad Trista", L"Podmiejskie miasto Trista", L"Banliyö Şehri Trista");
						break;
					case 8101:
						a = LL14(L"交易町ケルディック", L"Trading Town Celdic", L"Ville marchande Celdic", L"Città commerciale Celdic", L"Pueblo comercial Celdic", L"교역 마을 Celdic", L"交易鎮 Celdic", L"بلدة Celdic التجارية", L"Torgovyj gorod Celdic", L"Handelsstadt Celdic", L"Vila comercial Celdic", L"Handelsstad Celdic", L"Handlowe miasto Celdic", L"Ticaret Kasabası Celdic");
						break;
					case 8102:
						a = LL14(L"翡翠の公都バリアハート", L"Jade Capital Bareahard", L"Capitale de jade Bareahard", L"Capitale di giada Bareahard", L"Capital de jade Bareahard", L"비취의 공도 Bareahard", L"翡翠公都 Bareahard", L"عاصمة اليشم Bareahard", L"Nefritovaya stolica Bareahard", L"Jade-Hauptstadt Bareahard", L"Capital de jade Bareahard", L"Jade-hoofdstad Bareahard", L"Jadeitowa stolica Bareahard", L"Yeşim Başkenti Bareahard");
						break;
					case 8103:
						a = LL14(L"湖畔の街レグラム", L"Lakeside Town Legram", L"Ville au bord du lac Legram", L"Città lacustre Legram", L"Pueblo junto al lago Legram", L"호수 마을 Legram", L"湖畔之街 Legram", L"بلدة Legram بجانب البحيرة", L"Priozyornyj gorod Legram", L"Seeuferstadt Legram", L"Vila à beira-lago Legram", L"Meerstad Legram", L"Nadjeziorskie miasto Legram", L"Göl Kenarı Kasabası Legram");
						break;
					case 8104:
						a = LL14(L"黒銀の鋼都ルーレ", L"Iron City Roer", L"Ville de fer Roer", L"Città del ferro Roer", L"Ciudad del hierro Roer", L"흑은의 강철 도시 Roer", L"黑銀鋼都 Roer", L"مدينة Roer الحديدية", L"Zheleznyj gorod Roer", L"Eisenstadt Roer", L"Cidade do ferro Roer", L"IJzerstad Roer", L"Żelazne miasto Roer", L"Demir Şehir Roer");
						break;
					case 8106:
						a = LL14(L"遊牧民の集落", L"Nomad Settlement", L"Campement nomade", L"Insediamento nomade", L"Asentamiento nómada", L"유목민의 부락", L"遊牧民族定居點", L"مستوطنة بدوية", L"Poselenie kochevnikov", L"Nomadensiedlung", L"Assentamento nômade", L"Nomadennederzetting", L"Osada nomadów", L"Göçebe Yerleşimi");
						break;
					case 8107:
						a = LL14(L"緋の帝都ヘイムダル", L"Crimson Capital Heimdallr", L"Capitale pourpre Heimdallr", L"Capitale cremisi Heimdallr", L"Capital carmesí Heimdallr", L"붉은 제도 Heimdallr", L"緋紅帝都 Heimdallr", L"العاصمة القرمزية Heimdallr", L"Alaya stolica Heimdallr", L"Purpurrote Hauptstadt Heimdallr", L"Capital carmesim Heimdallr", L"Karmozijnrode hoofdstad Heimdallr", L"Szkarłatna stolica Heimdallr", L"Kızıl Başkent Heimdallr");
						break;
					case 8108:
						a = LL14(L"癒しの我が家", L"Healing Home", L"Maison de guérison", L"Casa curativa", L"Hogar sanador", L"치유의 우리 집", L"療癒的故郷", L"منزل الشفاء", L"Isceleblyayushchij dom", L"Heilsames Zuhause", L"Lar curativo", L"Heilzaam thuis", L"Uzdrawiający dom", L"Şifalı Yuva");
						break;
					case 8109:
						a = LL14(L"ダイニングバー《F》", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"다이닝 바 《F》", L"餐飲酒吧《F》", L"حانة الطعام F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F");
						break;
					case 8110:
						a = LL14(L"常在戦場の気概", L"Ever-Present War Spirit", L"Esprit de guerre constant", L"Spirito bellico costante", L"Espíritu de guerra constante", L"상재전장의 기개", L"常在戰場的氣概", L"روح الحرب الدائمة", L"Postoyannyj voennyj duh", L"Allgegenwärtiger Kriegsgeist", L"Espírito de guerra constante", L"Altijd aanwezige oorlogsgeest", L"Nieustanny duch walki", L"Daima Mevcut Savaş Ruhu");
						break;
					case 8111:
						a = LL14(L"ガレリアの巨壁", L"Garelia Fortress", L"Forteresse de Garelia", L"Fortezza di Garelia", L"Fortaleza de Garelia", L"Garelia의 거벽", L"Garelia 巨壁", L"حصن Garelia", L"Krepost' Gareliya", L"Festung Garelia", L"Fortaleza de Garelia", L"Vesting Garelia", L"Twierdza Garelia", L"Garelia Kalesi");
						break;
					case 8120:
						a = LL14(L"足湯の温もり", L"Foot Bath Warmth", L"Chaleur du bain de pieds", L"Calore del pediluvio", L"Calor del baño de pies", L"족욕의 따스함", L"足浴的溫暖", L"دفء حمام القدم", L"Teplo vannochki dlya nog", L"Wärme des Fußbads", L"Calor do banho de pés", L"Warmte van het voetbad", L"Ciepło kąpieli stóp", L"Ayak Banyosu Sıcaklığı");
						break;
					case 8121:
						a = LL14(L"静寂の郷", L"Silent Village", L"Village silencieux", L"Villaggio silenzioso", L"Aldea silenciosa", L"정적의 마을", L"靜謐之郷", L"القرية الصامتة", L"Tihaya derevnya", L"Stilles Dorf", L"Vila silenciosa", L"Stil dorp", L"Cicha wioska", L"Sessiz Köy");
						break;
					case 8122:
						a = LL14(L"明日への休息", L"Rest for Tomorrow", L"Repos pour demain", L"Riposo per domani", L"Descanso para mañana", L"내일로의 휴식", L"通往明天的休息", L"راحة للغد", L"Otdyh pered zavtrashnim dnyom", L"Ruhe für morgen", L"Descanso para amanhã", L"Rust voor morgen", L"Odpoczynek przed jutrem", L"Yarın İçin Dinlenme");
						break;
					case 8123:
						a = LL14(L"春の陽射し", L"Spring Sunshine", L"Soleil printanier", L"Sole primaverile", L"Sol de primavera", L"봄의 햇살", L"春日陽光", L"شمس الربيع", L"Vesennee solnce", L"Frühlingssonnenschein", L"Sol de primavera", L"Lentezon", L"Wiosenne słońce", L"Bahar Güneşi");
						break;
					case 8125:
						a = LL14(L"カレイジャス発進！", L"Courageous Launch!", L"Lancement du Courageous!", L"Lancio del Courageous!", L"¡Lanzamiento del Courageous!", L"Courageous 발진!", L"Courageous 出擊！", L"انطلاق Courageous!", L"Zapusk Courageous!", L"Start der Courageous!", L"Lançamento do Courageous!", L"Lancering van de Courageous!", L"Start Courageous!", L"Courageous Havalanıyor!");
						break;
					case 8126:
						a = LL14(L"目覚める意志", L"Awakening Will", L"Volonté s'éveillant", L"Volontà risvegliata", L"Voluntad que despierta", L"깨어나는 의지", L"覺醒的意志", L"إرادة مستيقظة", L"Probuzhdayushchayasya volya", L"Erwachender Wille", L"Vontade despertando", L"Ontwakende wil", L"Budząca się wola", L"Uyanan İrade");
						break;
					case 8127:
						a = LL14(L"白銀の巨船", L"Silver Ship", L"Vaisseau d'argent", L"Nave d'argento", L"Nave de plata", L"백은의 거선", L"白銀巨船", L"السفينة الفضية", L"Serebryanyj korabl'", L"Silbernes Schiff", L"Navio de prata", L"Zilveren schip", L"Srebrny statek", L"Gümüş Gemi");
						break;
					case 8150:
						a = LL14(L"放課後の時間", L"After School", L"Après l'école", L"Dopo la scuola", L"Después de clase", L"방과 후의 시간", L"放學後的時間", L"بعد المدرسة", L"Posle urokov", L"Nach der Schule", L"Depois da escola", L"Naschoolse tijd", L"Po szkole", L"Okul Çıkışı");
						break;
					case 8152:
						a = LL14(L"さわやかな朝", L"Refreshing Morning", L"Matin rafraîchissant", L"Mattina rinfrescante", L"Mañana refrescante", L"상쾌한 아침", L"清爽的早晨", L"صباح منعش", L"Osvyazhayushchee utro", L"Erfrischender Morgen", L"Manhã refrescante", L"Verfrissende ochtend", L"Orzeźwiający poranek", L"Ferah Bir Sabah");
						break;
					case 8153:
						a = LL14(L"雨音の学院", L"Rain-sound Academy", L"Académie au son de la pluie", L"Accademia al suono della pioggia", L"Academia al sonido de la lluvia", L"빗소리의 학원", L"雨聲學院", L"أكاديمية صوت المطر", L"Akademiya pod zvuk dozhdya", L"Akademie im Regenklang", L"Academia ao som da chuva", L"Academie met regengeluid", L"Akademia w dźwięku deszczu", L"Yağmur Sesli Akademi");
						break;
					case 8154:
						a = LL14(L"爽やかな陽射し", L"Clear Sunshine", L"Soleil éclatant", L"Luce solare chiara", L"Sol claro", L"상쾌한 햇살", L"爽朗的陽光", L"شمس مشرقة", L"Yasnaya solnechnaya pogoda", L"Klarer Sonnenschein", L"Sol claro", L"Heldere zonneschijn", L"Jasne słońce", L"Açık Güneş Işığı");
						break;
					case 8156:
						a = LL14(L"トールズ士官学院祭", L"Thors Academy Festival", L"Festival de l'Académie Thors", L"Festival dell'Accademia Thors", L"Festival de la Academia Thors", L"Thors 사관학교 축제", L"托爾茲軍官學院祭", L"مهرجان أكاديمية Thors", L"Festival Akademii Thors", L"Thors-Akademie-Fest", L"Festival da Academia Thors", L"Thors Academiefestival", L"Festiwal Akademii Thors", L"Thors Akademi Festivali");
						break;
					case 8158:
						a = LL14(L"青空の開放感", L"Open Sky", L"Ciel ouvert", L"Cielo aperto", L"Cielo abierto", L"푸른 하늘의 해방감", L"青空的開放感", L"سماء مفتوحة", L"Otkrytoe nebo", L"Offener Himmel", L"Céu aberto", L"Open lucht", L"Otwarte niebo", L"Açık Gökyüzü");
						break;
					case 8159:
						a = LL14(L"自由行動日", L"Free Day", L"Journée libre", L"Giorno libero", L"Día libre", L"자유 행동일", L"自由行動日", L"يوم حر", L"Den' svobodnyh dejstvij", L"Freier Tag", L"Dia livre", L"Vrije dag", L"Dzień wolny", L"Serbest Gün");
						break;
					case 8200:
						a = LL14(L"異郷の空", L"Foreign Sky", L"Ciel étranger", L"Cielo straniero", L"Cielo extranjero", L"이향의 하늘", L"異郷之空", L"سماء غريبة", L"Chuzhoe nebo", L"Fremder Himmel", L"Céu estrangeiro", L"Vreemde lucht", L"Obce niebo", L"Yabancı Gökyüzü");
						break;
					case 8201:
						a = LL14(L"峡谷道を往く", L"Through the Canyon", L"À travers le canyon", L"Attraverso il canyon", L"A través del cañón", L"협곡길을 가다", L"穿梭峽谷道", L"عبر الوادي", L"Cherez kan'on", L"Durch den Canyon", L"Pelo cânion", L"Door de kloof", L"Przez kanion", L"Kanyondan Geçerken");
						break;
					case 8202:
						a = LL14(L"精霊の小道", L"Spirit Path", L"Chemin des esprits", L"Sentiero degli spiriti", L"Senda de los espíritus", L"정령의 오솔길", L"精靈小徑", L"مسار الأرواح", L"Tropa duhov", L"Geisterpfad", L"Caminho dos espíritos", L"Geesterpad", L"Ścieżka duchów", L"Ruh Yolu");
						break;
					case 8203:
						a = LL14(L"蒼穹の大地", L"Azure Skies Land", L"Terre aux cieux azurs", L"Terra dai cieli azzurri", L"Tierra de cielos azures", L"창궁의 대지", L"蒼穹大地", L"أرض السماء الزرقاء", L"Zemlya lazurnyh nebes", L"Land unter azurblauem Himmel", L"Terra de céus azuis", L"Land van azuurblauwe luchten", L"Kraina błękitnego nieba", L"Gök mavisi Topraklar");
						break;
					case 8210:
						a = LL14(L"戦火を越えて", L"Beyond the Flames of War", L"Au-delà des flammes de la guerre", L"Oltre le fiamme della guerra", L"Más allá de las llamas de la guerra", L"전화를 넘어", L"跨越戰火", L"ما وراء لهيب الحرب", L"Skvoz' plamya vojny", L"Jenseits der Flammen des Krieges", L"Além das chamas da guerra", L"Voorbij de oorlogsvlammen", L"Poza płomienie wojny", L"Savaş Alevlerinin Ötesinde");
						break;
					case 8212:
						a = L"Trudge Along";
						break;
					case 8213:
						a = LL14(L"冬の訪れ", L"Arrival of Winter", L"L'arrivée de l'hiver", L"L'arrivo dell'inverno", L"Llegada del invierno", L"겨울의 방문", L"冬日將至", L"قدوم الشتاء", L"Prihod zimy", L"Ankunft des Winters", L"Chegada do inverno", L"Komst van de winter", L"Przyjście zimy", L"Kışın Gelişi");
						break;
					case 8300:
						a = LL14(L"旧校舎の謎", L"Old Schoolhouse Mystery", L"Mystère du vieux bâtiment", L"Mistero del vecchio edificio", L"Misterio del viejo edificio", L"구교사의 수수께끼", L"舊校舍之謎", L"لغز مبنى المدرسة القديم", L"Tajna staroj shkoly", L"Geheimnis des alten Schulhauses", L"Mistério da velha escola", L"Mysterie van het oude schoolgebouw", L"Tajemnica starej szkoły", L"Eski Okul Binasının Gizemi");
						break;
					case 8301:
						a = LL14(L"探索", L"Exploration", L"Exploration", L"Esplorazione", L"Exploración", L"탐색", L"探索", L"استكشاف", L"Issledovanie", L"Erkundung", L"Exploração", L"Verkenning", L"Eksploracja", L"Keşif");
						break;
					case 8302:
						a = LL14(L"深淵へ向かう", L"Toward the Abyss", L"Vers l'abîme", L"Verso l'abisso", L"Hacia el abismo", L"심연으로 향하다", L"邁向深淵", L"نحو الهاوية", L"K bezdne", L"Dem Abgrund entgegen", L"Em direção ao abismo", L"Naar de afgrond", L"Ku otchłani", L"Uçuruma Doğru");
						break;
					case 8303:
						a = LL14(L"聖女の城", L"Saint's Castle", L"Château de la sainte", L"Castello della santa", L"Castillo de la santa", L"성녀의 성", L"聖女之城", L"قلعة القديسة", L"Zamol svyatoj", L"Schloss der Heiligen", L"Castelo da santa", L"Kasteel van de heilige", L"Zamek świętej", L"Azizenin Kalesi");
						break;
					case 8304:
						a = LL14(L"明日を掴むために", L"To Seize Tomorrow", L"Pour saisir demain", L"Per afferrare il domani", L"Para atrapar el mañana", L"내일을 잡기 위해", L"為了抓住明天", L"لإمساك الغد", L"Chtoby zahvatit' zavtrashnij den'", L"Um das Morgen zu ergreifen", L"Para alcançar o amanhã", L"Om morgen te grijpen", L"Aby pochwycić jutro", L"Yarını Yakalamak İçin");
						break;
					case 8305:
						a = LL14(L"地下に眠る遺構", L"Ruins Beneath", L"Ruines souterraines", L"Rovine sotterranee", L"Ruinas subterráneas", L"지하에 잠든 유구", L"沉睡地下的遺構", L"أطلال تحت الأرض", L"Podzemnye ruiny", L"Ruinen darunter", L"Ruínas subterrâneas", L"Ondergrondse ruïnes", L"Podziemne ruiny", L"Yeraltı Harabeleri");
						break;
					case 8308:
						a = LL14(L"世の礎たるために", L"To Be the World's Foundation", L"Pour être le fondement du monde", L"Per essere la fondazione del mondo", L"Para ser el cimiento del mundo", L"세상의 주춧돌이 되기 위해", L"為了成為世界的基石", L"لنكون أساس العالم", L"Chtoby stat' osnovoj mira", L"Um das Fundament der Welt zu sein", L"Para ser o fundamento do mundo", L"Om het fundament van de wereld te zijn", L"Aby być fundamentem świata", L"Dünyanın Temeli Olmak İçin");
						break;
					case 8310:
						a = LL14(L"精霊窟", L"Spirit Cave", L"Grotte des esprits", L"Grotta degli spiriti", L"Cueva de los espíritus", L"정령굴", L"精靈窟", L"كهف الأرواح", L"Peshchera duhov", L"Geisterhöhle", L"Caverna dos espíritos", L"Grot van de geesten", L"Jaskinia duchów", L"Ruh Mağarası");
						break;
					case 8311:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8312:
						a = L"Phantasmal Blaze";
						break;
					case 8313:
						a = LL14(L"夢幻回廊", L"Phantasmagoria Corridor", L"Couloir de phantasmagorie", L"Corridoio di fantasmagoria", L"Corredor de fantasmagoría", L"몽환회랑", L"夢幻迴廊", L"ممر الخيال", L"Koridor fantasmagorii", L"Phantasmagoria-Korridor", L"Corredor de fantasmagoria", L"Fantoomcorridor", L"Korytarz fantasmagorii", L"Hayalet Koridor");
						break;
					case 8315:
						a = LL14(L"幻煌", L"Phantom Radiance", L"Éclat fantôme", L"Splendore fantasma", L"Resplandor fantasma", L"환황", L"幻煌", L"تألق وهمي", L"Prizrachnoe siyanie", L"Phantom-Glanz", L"Resplendor fantasma", L"Fantoomglans", L"Blask widma", L"Hayalet Parıltısı");
						break;
					case 8400:
						a = L"The Glint of Cold Steel";
						break;
					case 8401:
						a = L"Tie a Link of ARCUS!";
						break;
					case 8402:
						a = L"Belief";
						break;
					case 8403:
						a = L"Even if Driven to the Wall";
						break;
					case 8404:
						a = L"Eliminate Crisis!";
						break;
					case 8405:
						a = L"Exceed!";
						break;
					case 8406:
						a = L"Don't be Defeated by a Friend!";
						break;
					case 8407:
						a = L"Machinery Attack";
						break;
					case 8408:
						a = LL14(L"巨イナルチカラ", L"Colossal Power", L"Pouvoir colossal", L"Potere colossale", L"Poder colosal", L"거대한 힘", L"巨大的力量", L"قوة هائلة", L"Kolossal'naya sila", L"Kolossale Macht", L"Poder colossal", L"Kolossale kracht", L"Kolosalna moc", L"Muazzam Güç");
						break;
					case 8409:
						a = L"The Decisive Collision";
						break;
					case 8410:
						a = LL14(L"この手で道を切り拓く!", L"Carve Our Path with These Hands!", L"Ouvrir la voie de nos mains!", L"Aprire la strada con queste mani!", L"¡Abrir camino con estas manos!", L"이 손으로 길을 개척한다!", L"用這雙手開闢道路！", L"نمهد الطريق بأيدينا!", L"Prolozhit' put' etimi rukami!", L"Den Weg mit diesen Händen ebnen!", L"Abrir o caminho com estas mãos!", L"De weg banen met deze handen!", L"Przetrzeć szlak tymi rękami!", L"Yolumuzu Bu Ellerle Açacağız!");
						break;
					case 8411:
						a = LL14(L"赤点です...", L"Failed...", L"Échec...", L"Fallito...", L"Fallido...", L"과락입니다...", L"不及格...", L"فشل...", L"Neudovletvoritel'no...", L"Nicht bestanden...", L"Reprovado...", L"Gezakt...", L"Oblał...", L"Kaldın...");
						break;
					case 8412:
						a = L"Unknown Threat";
						break;
					case 8413:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8420:
						a = L"Heated Mind";
						break;
					case 8421:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8423:
						a = L"Impatient";
						break;
					case 8424:
						a = L"Severe Blow";
						break;
					case 8426:
						a = L"Transcend Beat";
						break;
					case 8429:
						a = L"Blue Destination";
						break;
					case 8430:
						a = L"Heteromorphy";
						break;
					case 8431:
						a = LL14(L"輝ける明日へ", L"Toward a Shining Tomorrow", L"Vers un demain radieux", L"Verso un domani radioso", L"Hacia un mañana radiante", L"빛나는 내일로", L"通往輝煌的明天", L"نحو غد مشرق", L"K siyayushchemu zavtra", L"Einem strahlenden Morgen entgegen", L"Para um amanhã brilhante", L"Naar een stralende morgen", L"Ku świetlistemu jutru", L"Parlak Bir Yarana Doğru");
						break;
					case 8435:
						a = LL14(L"迫る巨影", L"Approaching Giant Shadow", L"L'ombre géante approche", L"L'ombra gigante si avvicina", L"Sombra gigante acercándose", L"다가오는 거영", L"逼近的巨影", L"ظل عملاق يقترب", L"Priblizhayushchayasya gigantskaya ten'", L"Herannahender Riesenschatten", L"Sombra gigante se aproximando", L"Naderende gigantische schaduw", L"Zbliżający się gigantyczny cień", L"Yaklaşan Dev Gölge");
						break;
					case 8441:
						a = L"E.O.V";
						break;
					case 8442:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"刻いた", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8500:
						a = L"Strain";
						break;
					case 8501:
						a = LL14(L"夜のひととき", L"Nighttime", L"Moment de nuit", L"Momento notturno", L"Momento nocturno", L"밤의 한때", L"夜晚時光", L"وقت الليل", L"Nochnoe vremya", L"Nachtzeit", L"Momento da noite", L"Nachttijd", L"Nocny czas", L"Gece Vakti");
						break;
					case 8502:
						a = LL14(L"トラブル発生", L"Trouble", L"Problème", L"Problema", L"Problema", L"트러블 발생", L"發生麻煩", L"مشكلة", L"Problema", L"Ärger", L"Problema", L"Problemen", L"Problem", L"Sorun");
						break;
					case 8503:
						a = LL14(L"鉄路遥々", L"Distant Iron Road", L"Lointain chemin de fer", L"Lontana ferrovia", L"Lejana vía de hierro", L"머나먼 철길", L"漫漫鐵路", L"سكة حديدية بعيدة", L"Dal'nij zheleznyj put'", L"Ferne Eisenbahn", L"Caminho de ferro distante", L"Verre ijzeren weg", L"Daleka żelazna droga", L"Uzak Demir Yolu");
						break;
					case 8504:
						a = LL14(L"旅愁", L"Travel Melancholy", L"Mélancolie du voyage", L"Malinconia del viaggio", L"Melancolía del viaje", L"여수", L"旅愁", L"حزن السفر", L"Dorozhnaya toska", L"Reiseschwermut", L"Melancolia de viagem", L"Reismelancholie", L"Podróżna melancholia", L"Yolculuk Hüzünü");
						break;
					case 8505:
						a = LL14(L"皇城にて", L"At the Imperial Castle", L"Au château impérial", L"Al castello imperiale", L"En el castillo imperial", L"황성에서", L"在皇城", L"في القلعة الإمبراطورية", L"V imperatorskom zamke", L"Im kaiserlichen Schloss", L"No castelo imperial", L"In het keizerlijk kasteel", L"W zamku cesarskim", L"İmparatorluk Kalesinde");
						break;
					case 8506:
						a = L"Let's Study";
						break;
					case 8507:
						a = LL14(L"知恵を絞って", L"Rack Your Brains", L"Se creuser la tête", L"Spremiti le meningi", L"Devanarse los sesos", L"지혜를 짜내어", L"竭盡全力思考", L"اعصر دماغك", L"Poraskinut' mozgami", L"Den Kopf zerbrechen", L"Quebrar a cabeça", L"Je hersens pijnigen", L"Wytężać mózg", L"Zihnini Çalıştır");
						break;
					case 8508:
						a = LL14(L"実技教練", L"Combat Training", L"Entraînement au combat", L"Addestramento al combattimento", L"Entrenamiento de combate", L"실기 교련", L"實技教練", L"تدريب قتالي", L"Boevaya podgotovka", L"Kampftraining", L"Treinamento de combate", L"Gevechtstraining", L"Trening bojowy", L"Savaş Eğitimi");
						break;
					case 8509:
						a = LL14(L"寮に帰ろう", L"Back to the Dorm", L"Retour au dortoir", L"Ritorno al dormitorio", L"Regreso al dormitorio", L"기숙사로 돌아가자", L"回宿舍吧", L"العودة إلى السكن", L"Nazad v obshchezhitie", L"Zurück ins Wohnheim", L"De volta ao dormitório", L"Terug naar de slaapzaal", L"Powrót do internatu", L"Yurda Dönüş");
						break;
					case 8510:
						a = LL14(L"アーベントタイム", L"Evening Time", L"Soirée", L"Serata", L"Tarde noche", L"Abend time", L"傍晚時分", L"وقت المساء", L"Vechernee vremya", L"Abendzeit", L"Hora do entardecer", L"Avondtijd", L"Wieczorny czas", L"Akşam Vakti");
						break;
					case 8512:
						a = LL14(L"鉄の統率", L"Iron Command", L"Commandement de fer", L"Comando di ferro", L"Mando de hierro", L"철의 통솔", L"鋼鐵統率", L"قيادة حديدية", L"Zheleznoe komandovanie", L"Eisernes Kommando", L"Comando de ferro", L"IJzeren bevel", L"Żelazne dowództwo", L"Demir Komuta");
						break;
					case 8513:
						a = LL14(L"暗躍", L"Moving in the Shadows", L"Agir dans l'ombre", L"Muoversi nelle ombre", L"Moviéndose en las sombras", L"암약", L"暗中活動", L"التحرك في الظلال", L"Dejstviya v teni", L"Schattenarbeit", L"Movendo-se nas sombras", L"In de schaduw bewegen", L"Działanie w cieniu", L"Gölge Harekatı");
						break;
					case 8514:
						a = LL14(L"想いの行き先", L"Where Feelings Lead", L"Là où les sentiments mènent", L"Dove portano i sentimenti", L"Donde los sentimientos conducen", L"마음의 행방", L"思念的去向", L"حيث تقود المشاعر", L"Kuda vedut chuvstva", L"Wohin Gefühle führen", L"Para onde os sentimentos levam", L"Waar gevoelens toe leiden", L"Gdzie prowadzą uczucia", L"Duyguların Gittiği Yer");
						break;
					case 8515:
						a = LL14(L"傷心", L"Heartbreak", L"Cœur brisé", L"Cuore infranto", L"Corazón roto", L"상심", L"傷心", L"قلب مكسور", L"Razbitoe serdce", L"Herzeleid", L"Coração partido", L"Hartezeer", L"Złamane serce", L"Kalp Kırıklığı");
						break;
					case 8516:
						a = LL14(L"揺らめく炎を見つめて", L"Watching the Flickering Flames", L"Regarder les flammes vacillantes", L"Guardando le fiamme tremolanti", L"Mirando las llamas vacilantes", L"흔들리는 불꽃을 바라보며", L"凝視著搖曳的火焰", L"مشاهدة النيران المرتعشة", L"Glyadya na merkayushchee plamya", L"Die flackernden Flammen beobachten", L"Observando as chamas oscilantes", L"Kijken naar de flikkerende vlammen", L"Patrząc na migoczące płomienie", L"Titrek Alevleri İzlerken");
						break;
					case 8517:
						a = LL14(L"一途な気持ち", L"Single-minded Feelings", L"Sentiments sincères", L"Sentimenti sinceri", L"Sentimientos sinceros", L"한결같은 마음", L"專一的心情", L"مشاعر مخلصة", L"Iskrennie chuvstva", L"Aufrichtige Gefühle", L"Sentimentos sinceros", L"Oprechte gevoelens", L"Szczere uczucia", L"Samimi Duygular");
						break;
					case 8520:
						a = LL14(L"臨戦態勢", L"Combat Ready", L"Prêt au combat", L"Pronto al combattimento", L"Listo para el combate", L"임전태세", L"進入戰鬥狀態", L"مستعد للقتال", L"Boevaya gotovnost'", L"Gefechtsbereit", L"Pronto para o combate", L"Gevechtsklaar", L"Gotowy do walki", L"Savaşa Hazır");
						break;
					case 8521:
						a = L"Seriousness";
						break;
					case 8522:
						a = LL14(L"静かなる昂揚", L"Quiet Exhilaration", L"Exaltation tranquille", L"Silenziosa esaltazione", L"Silenciosa exaltación", L"고요한 고양", L"安靜的昂揚", L"ابتهاج هادئ", L"Tihoe voodushevlenie", L"Stille Begeisterung", L"Exaltação silenciosa", L"Stille opwinding", L"Cicha ekscytacja", L"Sessiz Coşku");
						break;
					case 8523:
						a = LL14(L"暖かな夕餉", L"Warm Dinner", L"Dîner chaud", L"Cena calda", L"Cena caliente", L"따뜻한 저녁 식사", L"溫暖的晚餐", L"عشاء دافئ", L"Teplyj uzhin", L"Warmes Abendessen", L"Jantar quente", L"Warm diner", L"Ciepła kolacja", L"Sıcak Akşam Yemeği");
						break;
					case 8524:
						a = L"Atrocious Raid";
						break;
					case 8525:
						a = LL14(L"全てを賭して今、ここに立つ", L"Standing Here, Betting Everything", L"Debout ici, pariant tout", L"In piedi qui, scommettendo tutto", L"Parado aquí, apostándolo todo", L"모든 것을 걸고 지금, 여기에 서다", L"賭上一切現在，立於此地", L"أقف هنا، أراهن على كل شيء", L"Stoya zdes', stavy vsyo na kartu", L"Hier stehen, alles setzen", L"De pé aqui, apostando tudo", L"Hier staan, alles op het spel zetten", L"Stojąc tu, stawiając wszystko", L"Her Şeyi Göze Alıp Burada Duruyorum");
						break;
					case 8527:
						a = LL14(L"新しい仲間たち", L"New Comrades", L"Nouveaux camarades", L"Nuovi compagni", L"Nuevos camaradas", L"새로운 동료들", L"新的夥伴們", L"رفاق جدد", L"Novye tovarishchi", L"Neue Kameraden", L"Novos camaradas", L"Nieuwe kameraden", L"Nowi towarzysze", L"Yeni Yoldaşlar");
						break;
					case 8528:
						a = LL14(L"不透明な事態", L"Opaque Situation", L"Situation opaque", L"Situazione opaca", L"Situación opaca", L"불투명한 사태", L"不明朗的事態", L"وضع غامض", L"Neprozrachnaya situaciya", L"Undurchsichtige Lage", L"Situação opaca", L"Ondoorzichtige situatie", L"Niejasna sytuacja", L"Belirsiz Durum");
						break;
					case 8529:
						a = LL14(L"鉄血へのレクイエム", L"Requiem for Iron and Blood", L"Requiem pour le fer et le sang", L"Requiem per il ferro e il sangue", L"Réquiem por el hierro y la sangre", L"철혈의 레クイエム", L"鐵血輓歌", L"قداس للحديد والدم", L"Rekviem po zhelezu i krovi", L"Requiem für Eisen und Blut", L"Réquiem para ferro e sangue", L"Requiem voor ijzer en bloed", L"Requiem dla żelaza i krwi", L"Demir ve Kan İçin Ağıt");
						break;
					case 8530:
						a = LL14(L"幻想の唄 -PHANTASMAGORIA-", L"Phantom Song -PHANTASMAGORIA-", L"Chant fantôme -PHANTASMAGORIA-", L"Canto fantasma -PHANTASMAGORIA-", L"Canto fantasma -PHANTASMAGORIA-", L"환상의 노래 -PHANTASMAGORIA-", L"幻想之歌 -PHANTASMAGORIA-", L"أغنية خيالية -PHANTASMAGORIA-", L"Prizrachnaya pesnya -PHANTASMAGORIA-", L"Phantommely -PHANTASMAGORIA-", L"Canção fantasma -PHANTASMAGORIA-", L"Fantoomlied -PHANTASMAGORIA-", L"Pieśń widma -PHANTASMAGORIA-", L"Hayalet Şarkı -PHANTASMAGORIA-");
						break;
					case 8531:
						a = LL14(L"刻ハ至レリ", L"The Hour Has Come", L"L'heure est venue", L"L'ora è giunta", L"La hora ha llegado", L"시간은 다가왔다", L"時機已到", L"لقد حانت الساعة", L"Chas nastal", L"Die Stunde ist gekommen", L"A hora chegou", L"Het uur is aangebroken", L"Nadeszła godzina", L"Zaman Geldi");
						break;
					case 8532:
						a = LL14(L"目覚めし伝承", L"Awakening Legend", L"Légende s'éveillant", L"Leggenda risvegliata", L"Leyenda que despierta", L"깨어난 전승", L"覺醒的傳承", L"أسطورة مستيقظة", L"Probuzhdayushchayasya legenda", L"Erwachende Legende", L"Lenda despertando", L"Ontwakende legende", L"Budząca się legenda", L"Uyanan Efsane");
						break;
					case 8533:
						a = LL14(L"唯一の希望", L"Only Hope", L"Seul espoir", L"Unica speranza", L"Única esperanza", L"유일한 희망", L"唯一的希望", L"الأمل الوحيد", L"Edinstvennaya nadezhda", L"Einzige Hoffnung", L"Única esperança", L"Enige hoop", L"Jedyna nadzieja", L"Tek Umut");
						break;
					case 8535:
					case 8537:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8538:
						a = LL14(L"今はまだ...", L"Not Yet...", L"Pas encore...", L"Non ancora...", L"Aún no...", L"지금은 아직...", L"現在還...", L"ليس بعد...", L"Poka eshchyo net...", L"Noch nicht...", L"Ainda não...", L"Nog niet...", L"Jeszcze nie...", L"Henüz Değil...");
						break;
					case 8539:
						a = LL14(L"あの日に見た夜空", L"The Night Sky I Saw That Day", L"Le ciel nocturne de ce jour-là", L"Il cielo stellato di quel giorno", L"El cielo nocturno de aquel día", L"그날 본 밤하늘", L"那天所見的星空", L"سماء الليل التي رأيتها ذلك اليوم", L"Nochnoe nebo, kotoroe ya videl v tot den'", L"Der Nachthimmel von jenem Tag", L"O céu noturno que vi naquele dia", L"De nachthemel die ik die dag zag", L"Nocne niebo, które widziałem tamtego dnia", L"O Gün Gördüğüm Gece Gökyüzü");
						break;
					case 8540:
						a = LL14(L"偽りの時間", L"False Time", L"Temps mensonger", L"Tempo falso", L"Tiempo falso", L"거짓된 시간", L"虛偽的時間", L"وقت مزيف", L"Lzhivoe vremya", L"Falsche Zeit", L"Tempo falso", L"Valse tijd", L"Fałszywy czas", L"Sahte Zaman");
						break;
					case 8541:
						a = LL14(L"紅き翼 -新たなる風-", L"Crimson Wings -New Wind-", L"Ailes pourpres -Nouveau vent-", L"Ali cremisi -Nuovo vento-", L"Alas carmesíes -Nuevo viento-", L"붉은 날개 ~새로운 바람~", L"紅之翼 -新之風-", L"أجنحة قرمزية - رياح جديدة", L"Alye kryl'ya -Novyj veter-", L"Purpurrote Flügel -Neuer Wind-", L"Asas carmificadas -Novo vento-", L"Karmozijnrode vleugels -Nieuwe wind-", L"Szkarłatne skrzydła -Nowy wiatr-", L"Kızıl Kanatlar -Yeni Rüzgar-");
						break;
					case 8550:
						a = LL14(L"再会", L"Reunion", L"Retrouvailles", L"Riunione", L"Reencuentro", L"재회", L"再會", L"لم الشمل", L"Vstrecha", L"Wiedersehen", L"Reunião", L"Reünie", L"Spotkanie", L"Yeniden Buluşma");
						break;
					case 8551:
						a = LL14(L"かけがえのない人へ", L"To Someone Irreplaceable", L"À une personne irremplaçable", L"A qualcuno di insostituibile", L"A alguien insustituible", L"둘도 없는 사람에게", L"致無可取代的人", L"إلى شخص لا يمكن استبداله", L"Nezamenimomu cheloveku", L"Für jemanden Unersetzlichen", L"Para alguém insubstituível", L"Aan iemand die onvervangbaar is", L"Dla kogoś niezastąpionego", L"Yeri Doldurulamaz Birine");
						break;
					case 8552:
						a = LL14(L"惜しむように、愛おしむように", L"Cherishing, Treasuring", L"Chérir, protéger", L"Facendo tesoro, amando", L"Atesorando, amando", L"아쉬운 듯이, 사랑스러운 듯이", L"依依不捨地，憐愛地", L"نعتز به، نقدسه", L"Dorozha i lyubya", L"Hegen und pflegen", L"Estimando, amando", L"Koesterend, waarderend", L"Ceniąc, pielęgnując", L"Değer Vererek, Severek");
						break;
					case 8553:
						a = LL14(L"ライノの花が咲く頃", L"When the Rhino Flower Blooms", L"Quand la fleur de rhino fleurit", L"Quando fiorisce il fiore di rino", L"Cuando florece la flor de rino", L"라이노 꽃이 필 무렵", L"犀角花盛開之時", L"عندما تزهر زهرة Rhino", L"Kogda cvetyot cvetok Rhino", L"Wenn die Rhino-Blüte blüht", L"Quando a flor de rino floresce", L"Wanneer de rhino-bloem bloeit", L"Kiedy zakwita kwiat Rhino", L"Rhino Çiçeği Açtığında");
						break;
					case 8555:
						a = LL14(L"戦場の掟", L"Rules of Battlefield", L"Règles du champ de bataille", L"Regole del campo di battaglia", L"Reglas del campo de batalla", L"전장의 규칙", L"戰場規則", L"قواعد ساحة المعركة", L"Zakony polya boya", L"Regeln des Schlachtfelds", L"Regras do campo de batalha", L"Regels van het slagveld", L"Zasady pola walki", L"Savaş Alanı Kuralları");
						break;
					case 8556:
						a = L"Remaining Glow";
						break;
					case 8557:
						a = LL14(L"深淵の魔女", L"Witch of the Abyss", L"Sorcière de l'abîme", L"Strega dell'abisso", L"Bruja del abismo", L"심연의 마녀", L"深淵魔女", L"ساحرة الهاوية", L"Ved'ma bezdny", L"Hexe des Abgrunds", L"Bruxa do abismo", L"Heks van de afgrond", L"Wiedźma z otchłani", L"Uçurum Cadısı");
						break;
					case 8558:
						a = L"ALTINA";
						break;
					case 8559:
						a = LL14(L"威風", L"Dignity", L"Dignité", L"Dignità", L"Dignidad", L"위풍", L"威風", L"كرامة", L"Dostoinstvo", L"Würde", L"Dignidade", L"Waardigheid", L"Godność", L"Görkem");
						break;
					case 8560:
						a = LL14(L"一撃に賭ける", L"Bet on One Strike", L"Parier sur un seul coup", L"Scommettere su un colpo solo", L"Apostar por un solo golpe", L"일격에 걸다", L"賭在這一擊上", L"المراهنة على ضربة واحدة", L"Stavit' na odin udar", L"Auf einen Schlag setzen", L"Apostar em um golpe", L"Gokken op één klap", L"Postawić na jeden cios", L"Tek Vuruşa Güvenmek");
						break;
					case 8561:
						a = LL14(L"ユミル渓谷道", L"Ymir Valley Road", L"Route de la vallée d'Ymir", L"Strada della valle di Ymir", L"Camino del valle de Ymir", L"Ymir 협곡길", L"Ymir 峽谷道", L"طريق وادي Ymir", L"Doroga doliny Imir", L"Ymir-Talstraße", L"Caminho do vale de Ymir", L"Ymir-valleiweg", L"Droga przez dolinę Ymir", L"Ymir Vadi Yolu");
						break;
					case 8562:
						a = L"Awakening";
						break;
					case 8563:
						a = L"Blitzkrieg";
						break;
					case 8564:
						a = LL14(L"魔王の凱歌", L"Demon Lord's Triumph", L"Triomphe du seigneur démon", L"Trionfo del signore dei demoni", L"Triunfo del señor de los demonios", L"마왕의 개가", L"魔王凱歌", L"انتصار ملك الشياطين", L"Pobednyj marsh korolya demonov", L"Triumph des Dämonenfürsten", L"Triunfo do senhor demônio", L"Triomf van de demonenheer", L"Triumf władcy demonów", L"İblis Efendisinin Zaferi");
						break;
					case 8566:
						a = LL14(L"内なる黄昏", L"Inner Twilight", L"Crépuscule intérieur", L"Crepuscolo interiore", L"Crepúsculo interior", L"내면의 황혼", L"內在的黄昏", L"الغسق الداخلي", L"Vnutrennie sumerki", L"Innere Dämmerung", L"Crepúsculo interior", L"Innerlijke schemering", L"Wewnętrzny zmierzch", L"İçsel Alacakaranlık");
						break;
					case 8567:
						a = LL14(L"蘇る記憶", L"Awakened Memories", L"Souvenirs éveillés", L"Memorie risvegliate", L"Memorias despertadas", L"되살아나는 기억", L"甦醒的記憶", L"ذكريات مستيقظة", L"Probuzhdyonnye vospominaniya", L"Erwachte Erinnerungen", L"Memórias despertadas", L"Ontwaakte herinneringen", L"Obudzone wspomnienia", L"Uyanan Anılar");
						break;
					case 8570:
						a = LL14(L"静かな決意", L"Quiet Resolution", L"Résolution tranquille", L"Silenziosa risoluzione", L"Silenciosa resolución", L"고요한 결의", L"平靜的決心", L"قرار هادئ", L"Tihoe reshenie", L"Stille Entschlossenheit", L"Resolução silenciosa", L"Stille vastberadenheid", L"Ciche postanowienie", L"Sessiz Kararlılık");
						break;
					case 8571:
						a = LL14(L"乾坤一擲", L"All or Nothing", L"Tout ou rien", L"Tutto o niente", L"Todo o nada", L"건곤일척", L"乾坤一擲", L"الكل أو لا شيء", L"Vsyyo ili nichego", L"Alles oder nichts", L"Tudo ou nada", L"Alles of niets", L"Wszystko albo nic", L"Ya Her Şey Ya Hiç");
						break;
					case 8572:
						a = LL14(L"交戦", L"Combat", L"Combat", L"Combattimento", L"Combate", L"교전", L"交戰", L"اشتباك", L"Srazhenie", L"Gefecht", L"Combate", L"Gevecht", L"Walka", L"Çatışma");
						break;
					case 8573:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
						break;
					case 8600:
						a = LL14(L"大市の賑わい", L"Bustling Market", L"Marché animé", L"Mercato vivace", L"Mercado bullicioso", L"대시장의 활기", L"大市集的熱鬧", L"سوق صاخب", L"Ozhivlyonnyj rynok", L"Belebter Markt", L"Mercado movimentado", L"Bedrijvige markt", L"Tętniący życiem targ", L"Hareketli Pazar");
						break;
					case 8601:
						a = LL14(L"剣の遊戯", L"Sword Play", L"Jeu d'épée", L"Giuoco di spade", L"Juego de espadas", L"검의 유희", L"劍之遊戲", L"لعب بالسيف", L"Igra s mechami", L"Schwertspiel", L"Jogo de espadas", L"Zwaardspel", L"Szermierka", L"Kılıç Oyunu");
						break;
					case 8602:
						a = LL14(L"紙一重の攻防", L"Close Fight", L"Combat serré", L"Scontro serrato", L"Combate reñido", L"간발의 차의 공방", L"千鈞一髮的攻防", L"قتال متلاحم", L"Boy vplotnuyu", L"Knapper Kampf", L"Combate acirrado", L"Nipt gevecht", L"Zacięta walka", L"Kıran Kırana Mücadele");
						break;
					case 8603:
						a = LL14(L"走れマッハ号!", L"Run Mach Train!", L"Cours, train Mach!", L"Corri, treno Mach!", L"¡Corre, tren Mach!", L"달려라 마하 호!", L"奔跑吧 Mach 號！", L"انطلق يا قطار ماخ!", L"Begi, poezd Mah!", L"Lauf, Mach-Zug!", L"Corra, trem Mach!", L"Ren, Mach-trein!", L"Pędź, pociągu Mach!", L"Koş Mach Treni!");
						break;
					case 8605:
					case 8606:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
						break;
					case 8607:
						a = LL14(L"星屑のカンタータ", L"Cantata of Stardust", L"Cantate de poussière d'étoiles", L"Cantata di polvere di stelle", L"Cantata de polvo de estrellas", L"별무리 칸타타", L"星塵大合唱", L"كانتاتا غبار النجوم", L"Kantata zvyozdnoj pyli", L"Kantate des Sternenstaubs", L"Cantata de poeira estelar", L"Cantate van sterrenstof", L"Kantata gwiezdnego pyłu", L"Yıldız Tozu Kantatı");
						break;
					case 8608:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
						break;
					case 8609:
						a = L"Sonata No.45";
						break;
					case 8610:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
						break;
					case 8620:
						a = LL14(L"雪ウサギを追いかけて", L"Chasing the Snow Rabbit", L"Chasser le lapin des neiges", L"Inseguendo il coniglio di neve", L"Persiguiendo al conejo de nieve", L"눈토끼를 쫓아서", L"追逐雪兔", L"مطاردة أرنب الثلج", L"Presleduya snezhnogo krolika", L"Dem Schneehase hinterher", L"Perseguindo o coelho de neve", L"Het sneeuwkonijn achterna", L"Goniąc śnieżnego królika", L"Kar Tavşanının Peşinde");
						break;
					case 8621:
						a = L"Take The Windward!";
						break;
					case 8622:
					case 8623:
					case 8624:
					case 8625:
					case 8627:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
						break;
					case 8628:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8629:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"효과음", L"音效", L"تأثير صوتي", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt dźwiękowy", L"Ses Efekti");
						break;
					case 8700:
					case 8703:
					case 8704:
					case 8710:
					case 8711:
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Música", L"음악", L"音樂", L"موسيقى", L"Muzyka", L"Musik", L"Música", L"Muziek", L"Muzyka", L"Müzik");
						break;
					}
					_tcscpy(p.name, a);
					_tcscpy(p.fol, fname1);
				}
				else if ((ft == L"bgm1.pac" || ft == L"bgm2.pac" || ft == L"bgm3.pac") && fname.Find(L"Trails in the Sky 1st Chapter") > 0) {
					p.sub = 30; p.loop1 = p.loop2 = 0;
					TCHAR ti1[][100] = {
						L"001 風を共に舞う気持ち",
						L"100 地方都市ロレント",
						L"101 商業都市ボース",
						L"102 海港都市ルーアン",
						L"103 工房都市ツァイス",
						L"104 王都グランセル",
						L"105 陽だまりにて和む猫",
						L"106 国境警備も楽じゃない",
						L"107 王城",
						L"108 グランアリーナ",
						L"108bグランアリーナ(ノーイントロ)",
						L"200 リベールの歩き方",
						L"201 Secret Green Passage",
						L"202 Rock on the Road",
						L"300 闇を彷徨う",
						L"301 行く手をはばむ鋼の床",
						L"302 暗がりがくれた安らぎ",
						L"303 四輪の塔",
						L"304 レイストン要塞",
						L"305 虚ろなる光の封土",
						L"400 Sophisticated Fight -Quick Battle-",
						L"401 Sophisticated Fight -Command Battle-",
						L"402 To be Suggestive",
						L"403 銀の意志",
						L"404 Challenger Invited",
						L"405 Ancient Makes",
						L"406 至宝を守護せしモノ",
						L"407 撃破！！",
						L"408 消え行く星",
						L"410 ピンチ！！",
						L"500 星の在り処 Harmonica short Ver.",
						L"501 琥珀の愛 Hum Ver.(日本語)",
						L"501e琥珀の愛 Hum Ver.",
						L"502 琥珀の愛 Piano Ver.",
						L"502b琥珀の愛 Piano Ver.1.5",
						L"503 琥珀の愛 Lute Ver.",
						L"504 星の在り処 Harmonica long Ver.",
						L"505 賑やかに行こう",
						L"510 去り行く決意",
						L"511 暗躍する者たち",
						L"512 奴を逃がすな！",
						L"513 胸の中に",
						L"514 月明りの下で",
						L"516 忍び寄る危機",
						L"517 俺達カプア一家！",
						L"518 旅立ちの小径",
						L"519 奪還",
						L"520 呪縛からの解放、そして・・・",
						L"521 告白",
						L"522 黒のオーブメント",
						L"523 リベールの誇り",
						L"530 組曲 白き花のマドリガル - 姫の悩み",
						L"531 組曲 白き花のマドリガル - 騎士達の嘆き",
						L"532 組曲 白き花のマドリガル - それぞれの思惑",
						L"533 組曲 白き花のマドリガル - 城",
						L"534 組曲 白き花のマドリガル - コロシアム",
						L"535 組曲 白き花のマドリガル - 決闘",
						L"536 組曲 白き花のマドリガル - 姫の死",
						L"537 組曲 白き花のマドリガル - 大団円",
						L""
					};
					TCHAR ti1_en[][100] = {
						L"001 Dancing with the Wind",
						L"100 Provincial City Rolent",
						L"101 Commercial City Bose",
						L"102 Port City Ruan",
						L"103 Workshop City Zeiss",
						L"104 Royal Capital Grancel",
						L"105 Cat Relaxing in the Sun",
						L"106 Border Patrol Isn't Easy",
						L"107 Royal Castle",
						L"108 Grand Arena",
						L"108b Grand Arena (No Intro)",
						L"200 Walking in Liberl",
						L"201 Secret Green Passage",
						L"202 Rock on the Road",
						L"300 Wandering in the Darkness",
						L"301 Steel Floor Blocking the Path",
						L"302 Peace in the Darkness",
						L"303 Tetracyclic Towers",
						L"304 Leiston Fortress",
						L"305 Hollow Land of Light",
						L"400 Sophisticated Fight -Quick Battle-",
						L"401 Sophisticated Fight -Command Battle-",
						L"402 To be Suggestive",
						L"403 Silver Will",
						L"404 Challenger Invited",
						L"405 Ancient Makes",
						L"406 Guardian of the Treasure",
						L"407 Crush!!",
						L"408 Disappearing Star",
						L"410 Pinch!!",
						L"500 Where the Stars Are Harmonica short Ver.",
						L"501 Amber Love Hum Ver.(Japanese)",
						L"501e Amber Love Hum Ver.",
						L"502 Amber Love Piano Ver.",
						L"502b Amber Love Piano Ver.1.5",
						L"503 Amber Love Lute Ver.",
						L"504 Where the Stars Are Harmonica long Ver.",
						L"505 Let's Go Lively",
						L"510 Determination to Leave",
						L"511 Those Who Move in the Shadows",
						L"512 Don't Let Him Escape!",
						L"513 In My Heart",
						L"514 Under the Moonlight",
						L"516 Creeping Crisis",
						L"517 We're the Capua Family!",
						L"518 Path of Departure",
						L"519 Recapture",
						L"520 Liberation from the Curse, and...",
						L"521 Confession",
						L"522 Black Ouroboros",
						L"523 Pride of Liberl",
						L"530 Suite Madrigal of the White Flower - Princess's Worry",
						L"531 Suite Madrigal of the White Flower - Knights' Lament",
						L"532 Suite Madrigal of the White Flower - Each One's Scheme",
						L"533 Suite Madrigal of the White Flower - Castle",
						L"534 Suite Madrigal of the White Flower - Colosseum",
						L"535 Suite Madrigal of the White Flower - Duel",
						L"536 Suite Madrigal of the White Flower - Princess's Death",
						L"537 Suite Madrigal of the White Flower - Grand Finale",
						L""
					};
					TCHAR ti1_fr[][100] = {
						L"001 Sentiments dansant avec le vent", L"100 Rolent - Ville provinciale", L"101 Bose - Ville commerciale", L"102 Ruan - Ville portuaire", L"103 Zeiss - Ville atelier", L"104 Grancel - Capitale royale", L"105 Chat au soleil", L"106 La patrouille frontière n'est pas facile", L"107 Château royal", L"108 Grand Arena", L"108b Grand Arena (Sans intro)", L"200 Comment se déplacer à Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Errance dans les ténèbres", L"301 Plancher d'acier bloquant le chemin", L"302 Paix des ténèbres", L"303 Tours tétracycliques", L"304 Forteresse de Leiston", L"305 Terre vacante de lumière", L"400 Sophisticated Fight -Combat rapide-", L"401 Sophisticated Fight -Combat commande-", L"402 To be Suggestive", L"403 Volonté d'argent", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Gardien du trésor", L"407 Écrasement!!", L"408 Étoile défaillante", L"410 Pinch!!", L"500 Où sont les étoiles Harmonica court", L"501 Amour d'ambre Hum Ver.(Japonais)", L"501e Amour d'ambre Hum Ver.", L"502 Amour d'ambre Piano Ver.", L"502b Amour d'ambre Piano Ver.1.5", L"503 Amour d'ambre Luth Ver.", L"504 Où sont les étoiles Harmonica long", L"505 Allons gaiement", L"510 Décision de partir", L"511 Ceux qui agissent dans l'ombre", L"512 Ne le laissez pas s'échapper!", L"513 Dans mon cœur", L"514 Sous le clair de lune", L"516 Crise rampante", L"517 Nous sommes la famille Capua!", L"518 Sentier du départ", L"519 Reprise", L"520 Libération de la malédiction, et...", L"521 Aveu", L"522 Orbement noir", L"523 Fierté de Liberl", L"530 Suite Madrigal de la Fleur Blanche - Souci de la princesse", L"531 Suite Madrigal - Lamentation des chevaliers", L"532 Suite Madrigal - Intentions de chacun", L"533 Suite Madrigal - Château", L"534 Suite Madrigal - Colisée", L"535 Suite Madrigal - Duel", L"536 Suite Madrigal - Mort de la princesse", L"537 Suite Madrigal - Grand final", L""
					};
					TCHAR ti1_de[][100] = {
						L"001 Gefühle tanzend mit dem Wind", L"100 Rolent - Provinzstadt", L"101 Bose - Handelsstadt", L"102 Ruan - Hafenstadt", L"103 Zeiss - Werkstadt", L"104 Grancel - Königshauptstadt", L"105 Katze in der Sonne", L"106 Grenzpatrouille ist nicht leicht", L"107 Königsschloss", L"108 Grand Arena", L"108b Grand Arena (Ohne Intro)", L"200 Zu Fuß durch Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Wandern in der Dunkelheit", L"301 Stahlboden versperrt den Weg", L"302 Frieden in der Dunkelheit", L"303 Tetrazyklische Türme", L"304 Leiston-Festung", L"305 Hohles Land des Lichts", L"400 Sophisticated Fight -Schneller Kampf-", L"401 Sophisticated Fight -Kommando-Kampf-", L"402 To be Suggestive", L"403 Silberner Wille", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Hüter des Schatzes", L"407 Zerschmettern!!", L"408 Verblassender Stern", L"410 Pinch!!", L"500 Wo die Sterne sind Harmonica kurz", L"501 Bernstein-Liebe Hum Ver.(Japanisch)", L"501e Bernstein-Liebe Hum Ver.", L"502 Bernstein-Liebe Klavier Ver.", L"502b Bernstein-Liebe Klavier Ver.1.5", L"503 Bernstein-Liebe Laute Ver.", L"504 Wo die Sterne sind Harmonica lang", L"505 Gehen wir fröhlich", L"510 Entschlossenheit zu gehen", L"511 Die im Schatten handeln", L"512 Lasst ihn nicht entkommen!", L"513 In meinem Herzen", L"514 Im Mondschein", L"516 Schleichende Krise", L"517 Wir sind die Capua-Familie!", L"518 Pfad des Aufbruchs", L"519 Rückeroberung", L"520 Befreiung vom Fluch, und...", L"521 Geständnis", L"522 Schwarzer Ouroboros", L"523 Stolz von Liberl", L"530 Suite Madrigal der Weißen Blume - Sorge der Prinzessin", L"531 Suite Madrigal - Klage der Ritter", L"532 Suite Madrigal - Jeder sein Plan", L"533 Suite Madrigal - Schloss", L"534 Suite Madrigal - Kolosseum", L"535 Suite Madrigal - Duell", L"536 Suite Madrigal - Tod der Prinzessin", L"537 Suite Madrigal - Großer Schluss", L""
					};
					TCHAR ti1_es[][100] = {
						L"001 Sentimientos bailando con el viento", L"100 Rolent - Ciudad provincial", L"101 Bose - Ciudad comercial", L"102 Ruan - Ciudad portuaria", L"103 Zeiss - Ciudad taller", L"104 Grancel - Capital real", L"105 Gato al sol", L"106 La patrulla fronteriza no es fácil", L"107 Castillo real", L"108 Grand Arena", L"108b Grand Arena (Sin intro)", L"200 Cómo caminar por Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Vagando en la oscuridad", L"301 Piso de acero bloqueando el camino", L"302 Paz en la oscuridad", L"303 Torres tetracyclic", L"304 Fortaleza Leiston", L"305 Tierra vacía de luz", L"400 Sophisticated Fight -Batalla rápida-", L"401 Sophisticated Fight -Batalla comando-", L"402 To be Suggestive", L"403 Voluntad de plata", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Guardián del tesoro", L"407 ¡¡Aplastar!!", L"408 Estrella desvaneciente", L"410 Pinch!!", L"500 Dónde están las estrellas Harmónica corto", L"501 Amor ámbar Hum Ver.(Japonés)", L"501e Amor ámbar Hum Ver.", L"502 Amor ámbar Piano Ver.", L"502b Amor ámbar Piano Ver.1.5", L"503 Amor ámbar Laúd Ver.", L"504 Dónde están las estrellas Harmónica largo", L"505 Vamos alegres", L"510 Determinación de partir", L"511 Los que actúan en la sombra", L"512 ¡No lo dejes escapar!", L"513 En mi corazón", L"514 Bajo la luna", L"516 Crisis creciente", L"517 ¡Somos la familia Capua!", L"518 Camino de partida", L"519 Recaptura", L"520 Liberación de la maldición, y...", L"521 Confesión", L"522 Orbement negro", L"523 Orgullo de Liberl", L"530 Suite Madrigal de la Flor Blanca - Preocupación de la princesa", L"531 Suite Madrigal - Lamento de los caballeros", L"532 Suite Madrigal - Intenciones de cada uno", L"533 Suite Madrigal - Castillo", L"534 Suite Madrigal - Coliseo", L"535 Suite Madrigal - Duelo", L"536 Suite Madrigal - Muerte de la princesa", L"537 Suite Madrigal - Gran final", L""
					};
					TCHAR ti1_it[][100] = {
						L"001 Sentimenti danzanti con il vento", L"100 Rolent - Città provinciale", L"101 Bose - Città commerciale", L"102 Ruan - Città portuale", L"103 Zeiss - Città officina", L"104 Grancel - Capitale reale", L"105 Gatto al sole", L"106 La pattuglia di frontiera non è facile", L"107 Castello reale", L"108 Grand Arena", L"108b Grand Arena (Senza intro)", L"200 Come camminare a Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Vagando nelle tenebre", L"301 Pavimento d'acciaio che blocca il sentiero", L"302 Pace nelle tenebre", L"303 Torri tetracyclic", L"304 Fortezza Leiston", L"305 Terra vuota di luce", L"400 Sophisticated Fight -Battaglia rapida-", L"401 Sophisticated Fight -Battaglia comando-", L"402 To be Suggestive", L"403 Volontà d'argento", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Guardiano del tesoro", L"407 Schiacciare!!", L"408 Stella che svanisce", L"410 Pinch!!", L"500 Dove sono le stelle Fisarmonica corto", L"501 Amore ambra Hum Ver.(Giapponese)", L"501e Amore ambra Hum Ver.", L"502 Amore ambra Piano Ver.", L"502b Amore ambra Piano Ver.1.5", L"503 Amore ambra Liuto Ver.", L"504 Dove sono le stelle Fisarmonica lungo", L"505 Andiamo allegri", L"510 Determinazione a partire", L"511 Coloro che agiscono nell'ombra", L"512 Non lasciarlo scappare!", L"513 Nel mio cuore", L"514 Sotto la luna", L"516 Crisi strisciante", L"517 Siamo la famiglia Capua!", L"518 Sentiero di partenza", L"519 Riconquista", L"520 Liberazione dalla maledizione, e...", L"521 Confessione", L"522 Orbement nero", L"523 Orgoglio di Liberl", L"530 Suite Madrigal del Fiore Bianco - Preoccupazione della principessa", L"531 Suite Madrigal - Lamento dei cavalieri", L"532 Suite Madrigal - Intenzioni di ciascuno", L"533 Suite Madrigal - Castello", L"534 Suite Madrigal - Colosseo", L"535 Suite Madrigal - Duello", L"536 Suite Madrigal - Morte della principessa", L"537 Suite Madrigal - Gran finale", L""
					};
					TCHAR ti1_ko[][100] = {
						L"001 바람과 함께 춤추는 마음", L"100 지방도시 롤렌트", L"101 상업도시 보스", L"102 항구도시 루안", L"103 공방도시 체스", L"104 왕도 그랑셀", L"105 양지에서 늘어지는 고양이", L"106 국경 경비도 쉽지 않아", L"107 왕성", L"108 그랑 아레나", L"108b 그랑 아레나 (인트로 없음)", L"200 리벨의 걷는 법", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 어둠을 방황하며", L"301 가로막는 강철의 바닥", L"302 어둠이 준 안식", L"303 사륜의 탑", L"304 레이스톤 요새", L"305 허무한 빛의 봉토", L"400 Sophisticated Fight -퀵 배틀-", L"401 Sophisticated Fight -커맨드 배틀-", L"402 To be Suggestive", L"403 은의 의지", L"404 Challenger Invited", L"405 Ancient Makes", L"406 지보를 수호하는 자", L"407 격파!!", L"408 사라져가는 별", L"410 Pinch!!", L"500 별이 있는 곳 하모니카 short Ver.", L"501 호박의 사랑 Hum Ver.(일본어)", L"501e 호박의 사랑 Hum Ver.", L"502 호박의 사랑 Piano Ver.", L"502b 호박의 사랑 Piano Ver.1.5", L"503 호박의 사랑 류트 Ver.", L"504 별이 있는 곳 하모니카 long Ver.", L"505 활발히 가자", L"510 떠나가는 결의", L"511 그림자에서 움직이는 자들", L"512 놈을 놓치지 마!", L"513 가슴 속에", L"514 달빛 아래에서", L"516 다가오는 위기", L"517 우리 카푸아 가족!", L"518 여정의 오솔길", L"519 탈환", L"520 저주로부터의 해방, 그리고...", L"521 고백", L"522 검은 오브먼트", L"523 리벨의 자긍심", L"530 조곡 백화의 마드리갈 - 공주의 고민", L"531 조곡 백화의 마드리갈 - 기사들의 한탄", L"532 조곡 백화의 마드리갈 - 각자의 의도", L"533 조곡 백화의 마드리갈 - 성", L"534 조곡 백화의 마드리갈 - 콜로세움", L"535 조곡 백화의 마드리갈 - 결투", L"536 조곡 백화의 마드리갈 - 공주의 죽음", L"537 조곡 백화의 마드리갈 - 대단원", L""
					};
					TCHAR ti1_zh[][100] = {
						L"001 与风共舞的心", L"100 地方都市洛连特", L"101 商业都市柏斯", L"102 海港都市卢安", L"103 工房都市蔡斯", L"104 王都格兰赛尔", L"105 阳光下的猫", L"106 国境警备也不轻松", L"107 王城", L"108 格兰竞技场", L"108b 格兰竞技场(无前奏)", L"200 利贝尔的步道", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 彷徨于黑暗中", L"301 阻挡去路的钢铁之床", L"302 黑暗带来的安宁", L"303 四轮之塔", L"304 雷斯塔要塞", L"305 虚渺之光封土", L"400 Sophisticated Fight -快速战斗-", L"401 Sophisticated Fight -指令战斗-", L"402 To be Suggestive", L"403 银之意志", L"404 Challenger Invited", L"405 Ancient Makes", L"406 至宝守护者", L"407 击破!!", L"408 消逝之星", L"410 Pinch!!", L"500 星之所在 口琴short Ver.", L"501 琥珀之爱 Hum Ver.(日语)", L"501e 琥珀之爱 Hum Ver.", L"502 琥珀之爱 钢琴 Ver.", L"502b 琥珀之爱 钢琴 Ver.1.5", L"503 琥珀之爱 鲁特琴 Ver.", L"504 星之所在 口琴long Ver.", L"505 热闹地出发", L"510 离去的决意", L"511 暗中行动者们", L"512 别让他逃了!", L"513 心中", L"514 月光下", L"516 悄悄逼近的危机", L"517 我们是卡普亚一家!", L"518 启程小路", L"519 夺还", L"520 从诅咒中解放,然后...", L"521 告白", L"522 黑色导力器", L"523 利贝尔的骄傲", L"530 组曲 白花之恋曲 - 公主的烦恼", L"531 组曲 白花之恋曲 - 骑士们的叹息", L"532 组曲 白花之恋曲 - 各自的思虑", L"533 组曲 白花之恋曲 - 城堡", L"534 组曲 白花之恋曲 - 竞技场", L"535 组曲 白花之恋曲 - 决斗", L"536 组曲 白花之恋曲 - 公主之死", L"537 组曲 白花之恋曲 - 大团圆", L""
					};
					TCHAR ti1_ar[][100] = {
						L"001 مشاعر راقصة مع الرياح", L"100 رولنت - مدينة إقليمية", L"101 بوس - مدينة تجارية", L"102 روان - مدينة ميناء", L"103 زايس - مدينة ورشة", L"104 غرانسل - العاصمة الملكية", L"105 قط في الشمس", L"106 دورية الحدود ليست سهلة", L"107 القصر الملكي", L"108 Grand Arena", L"108b Grand Arena (بدون مقدمة)", L"200 كيفية المشي في ليبرل", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 التيه في الظلام", L"301 أرضية فولاذية تعترض الطريق", L"302 السلام في الظلام", L"303 الأبراج الرباعية", L"304 حصن ليستون", L"305 أرض الضوء الفارغة", L"400 Sophisticated Fight -معركة سريعة-", L"401 Sophisticated Fight -معركة أوامر-", L"402 To be Suggestive", L"403 الإرادة الفضية", L"404 Challenger Invited", L"405 Ancient Makes", L"406 حارس الكنز", L"407 سحق!!", L"408 نجمة باهتة", L"410 Pinch!!", L"500 أين النجوم هارمونيكا قصير", L"501 حب الكهرمان Hum Ver.(ياباني)", L"501e حب الكهرمان Hum Ver.", L"502 حب الكهرمان بيانو Ver.", L"502b حب الكهرمان بيانو Ver.1.5", L"503 حب الكهرمان عود Ver.", L"504 أين النجوم هارمونيكا طويل", L"505 لنذهب بمرح", L"510 العزم على المغادرة", L"511 من يتحركون في الظل", L"512 لا تدعوه يهرب!", L"513 في قلبي", L"514 تحت ضوء القمر", L"516 أزمة زاحفة", L"517 نحن عائلة كابوا!", L"518 طريق الرحيل", L"519 استعادة", L"520 التحرر من اللعنة، و...", L"521 اعتراف", L"522 Orbement أسود", L"523 فخر ليبرل", L"530 Suite Madrigal الزهرة البيضاء - قلق الأميرة", L"531 Suite Madrigal - رثاء الفرسان", L"532 Suite Madrigal - نوايا كل واحد", L"533 Suite Madrigal - القلعة", L"534 Suite Madrigal - الكولوسيوم", L"535 Suite Madrigal - مبارزة", L"536 Suite Madrigal - موت الأميرة", L"537 Suite Madrigal - الخاتمة الكبرى", L""
					};
					TCHAR ti1_ru[][100] = {
						L"001 Чувства, танцующие с ветром", L"100 Ролент - Провинциальный город", L"101 Бос - Торговый город", L"102 Руан - Портовый город", L"103 Цейсс - Город мастерских", L"104 Грансель - Королевская столица", L"105 Кот на солнце", L"106 Пограничный патруль нелёгок", L"107 Королевский замок", L"108 Grand Arena", L"108b Grand Arena (Без вступления)", L"200 Как ходить по Либерлу", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Блуждание во тьме", L"301 Стальной пол преграждает путь", L"302 Покой во тьме", L"303 Тетрациклические башни", L"304 Крепость Лейстон", L"305 Пустая земля света", L"400 Sophisticated Fight -Быстрый бой-", L"401 Sophisticated Fight -Командный бой-", L"402 To be Suggestive", L"403 Серебряная воля", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Страж сокровища", L"407 Разгром!!", L"408 Исчезающая звезда", L"410 Pinch!!", L"500 Где звёзды Гармоника короткая", L"501 Янтарная любовь Hum Ver.(Японский)", L"501e Янтарная любовь Hum Ver.", L"502 Янтарная любовь Фортепиано Ver.", L"502b Янтарная любовь Фортепиано Ver.1.5", L"503 Янтарная любовь Лютня Ver.", L"504 Где звёзды Гармоника длинная", L"505 Пойдём весело", L"510 Решимость уйти", L"511 Действующие в тени", L"512 Не дай ему сбежать!", L"513 В моём сердце", L"514 Под лунным светом", L"516 Надвигающийся кризис", L"517 Мы семья Капуа!", L"518 Тропа отбытия", L"519 Захват", L"520 Освобождение от проклятия, и...", L"521 Признание", L"522 Чёрный Orbment", L"523 Гордость Либерла", L"530 Сюита Мадригал Белого Цветка - Забота принцессы", L"531 Сюита Мадригал - Плач рыцарей", L"532 Сюита Мадригал - Замыслы каждого", L"533 Сюита Мадригал - Замок", L"534 Сюита Мадригал - Колизей", L"535 Сюита Мадригал - Поединок", L"536 Сюита Мадригал - Смерть принцессы", L"537 Сюита Мадригал - Большой финал", L""
					};
					TCHAR ti1_pt[][100] = {
						L"001 Sentimentos dançando com o vento", L"100 Rolent - Cidade provincial", L"101 Bose - Cidade comercial", L"102 Ruan - Cidade portuária", L"103 Zeiss - Cidade oficina", L"104 Grancel - Capital real", L"105 Gato ao sol", L"106 A patrulha de fronteira não é fácil", L"107 Castelo real", L"108 Grand Arena", L"108b Grand Arena (Sem intro)", L"200 Como andar por Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Vagando na escuridão", L"301 Piso de aço bloqueando o caminho", L"302 Paz na escuridão", L"303 Torres tetraclic", L"304 Fortaleza Leiston", L"305 Terra vazia de luz", L"400 Sophisticated Fight -Batalha rápida-", L"401 Sophisticated Fight -Batalha comando-", L"402 To be Suggestive", L"403 Vontade de prata", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Guardião do tesouro", L"407 Esmagar!!", L"408 Estrela desvanecente", L"410 Pinch!!", L"500 Onde estão as estrelas Harmônica curto", L"501 Amor âmbar Hum Ver.(Japonês)", L"501e Amor âmbar Hum Ver.", L"502 Amor âmbar Piano Ver.", L"502b Amor âmbar Piano Ver.1.5", L"503 Amor âmbar Alaúde Ver.", L"504 Onde estão as estrelas Harmônica longo", L"505 Vamos animados", L"510 Determinação de partir", L"511 Os que agem nas sombras", L"512 Não o deixe escapar!", L"513 No meu coração", L"514 Sob o luar", L"516 Crise rastejante", L"517 Somos a família Capua!", L"518 Caminho da partida", L"519 Recaptura", L"520 Libertação da maldição, e...", L"521 Confissão", L"522 Orbement negro", L"523 Orgulho de Liberl", L"530 Suite Madrigal da Flor Branca - Preocupação da princesa", L"531 Suite Madrigal - Lamento dos cavaleiros", L"532 Suite Madrigal - Intenções de cada um", L"533 Suite Madrigal - Castelo", L"534 Suite Madrigal - Coliseu", L"535 Suite Madrigal - Duelo", L"536 Suite Madrigal - Morte da princesa", L"537 Suite Madrigal - Grande final", L""
					};
					TCHAR ti1_nl[][100] = {
						L"001 Gevoelens dansend met de wind", L"100 Rolent - Provinciestad", L"101 Bose - Handelsstad", L"102 Ruan - Havenstad", L"103 Zeiss - Werkplaatsstad", L"104 Grancel - Koninklijke hoofdstad", L"105 Kat in de zon", L"106 Grenspatrouille is niet gemakkelijk", L"107 Koninklijk kasteel", L"108 Grand Arena", L"108b Grand Arena (Zonder intro)", L"200 Hoe te lopen in Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Dwalen in de duisternis", L"301 Stalen vloer blokkeert het pad", L"302 Vrede in de duisternis", L"303 Tetracyclische torens", L"304 Leiston vesting", L"305 Leeg land van licht", L"400 Sophisticated Fight -Snelle gevecht-", L"401 Sophisticated Fight -Commando gevecht-", L"402 To be Suggestive", L"403 Zilveren wil", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Bewaker van de schat", L"407 Verpletteren!!", L"408 Vervagende ster", L"410 Pinch!!", L"500 Waar de sterren zijn Harmonica kort", L"501 Amberliefde Hum Ver.(Japans)", L"501e Amberliefde Hum Ver.", L"502 Amberliefde Piano Ver.", L"502b Amberliefde Piano Ver.1.5", L"503 Amberliefde Luit Ver.", L"504 Waar de sterren zijn Harmonica lang", L"505 Laten we vrolijk gaan", L"510 Vastberadenheid om te vertrekken", L"511 Degenen in de schaduw", L"512 Laat hem niet ontsnappen!", L"513 In mijn hart", L"514 Onder het maanlicht", L"516 Sluipende crisis", L"517 Wij zijn de Capua-familie!", L"518 Pad van vertrek", L"519 Herovering", L"520 Bevrijding van de vloek, en...", L"521 Biecht", L"522 Zwarte Orbment", L"523 Trots van Liberl", L"530 Suite Madrigal van de Witte Bloem - Zorg van de prinses", L"531 Suite Madrigal - Klaagzang van ridders", L"532 Suite Madrigal - Intenties van iedereen", L"533 Suite Madrigal - Kasteel", L"534 Suite Madrigal - Colosseum", L"535 Suite Madrigal - Duel", L"536 Suite Madrigal - Dood van prinses", L"537 Suite Madrigal - Grote finale", L""
					};
					TCHAR ti1_pl[][100] = {
						L"001 Uczucia tańczące z wiatrem", L"100 Rolent - Miasto prowincjonalne", L"101 Bose - Miasto handlowe", L"102 Ruan - Miasto portowe", L"103 Zeiss - Miasto warsztatów", L"104 Grancel - Stolica królewska", L"105 Kot w słońcu", L"106 Patrol graniczny nie jest łatwy", L"107 Zamek królewski", L"108 Grand Arena", L"108b Grand Arena (Bez wstępu)", L"200 Jak chodzić po Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Błądzenie w ciemności", L"301 Stalowa podłoga blokująca drogę", L"302 Pokój w ciemności", L"303 Wieże tetracyclic", L"304 Twierdza Leiston", L"305 Pusta ziemia światła", L"400 Sophisticated Fight -Szybka bitwa-", L"401 Sophisticated Fight -Bitwa komendy-", L"402 To be Suggestive", L"403 Srebrna wola", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Strażnik skarbu", L"407 Zniszczyć!!", L"408 Gasnąca gwiazda", L"410 Pinch!!", L"500 Gdzie są gwiazdy Harmonica krótka", L"501 Bursztynowa miłość Hum Ver.(Japoński)", L"501e Bursztynowa miłość Hum Ver.", L"502 Bursztynowa miłość Fortepian Ver.", L"502b Bursztynowa miłość Fortepian Ver.1.5", L"503 Bursztynowa miłość Lutnia Ver.", L"504 Gdzie są gwiazdy Harmonica długa", L"505 Idźmy wesoło", L"510 Determinacja do odejścia", L"511 Ci działający w cieniu", L"512 Nie daj mu uciec!", L"513 W moim sercu", L"514 W świetle księżyca", L"516 Pełzający kryzys", L"517 Jesteśmy rodziną Capua!", L"518 Ścieżka odejścia", L"519 Odzyskanie", L"520 Wyzwolenie od klątwy, i...", L"521 Wyznanie", L"522 Czarny Orbment", L"523 Duma Liberl", L"530 Suita Madrygał Białego Kwiatu - Troska księżniczki", L"531 Suita Madrygał - Lament rycerzy", L"532 Suita Madrygał - Zamiary każdego", L"533 Suita Madrygał - Zamek", L"534 Suita Madrygał - Koloseum", L"535 Suita Madrygał - Pojedynek", L"536 Suita Madrygał - Śmierć księżniczki", L"537 Suita Madrygał - Wielki finał", L""
					};
					TCHAR ti1_tr[][100] = {
						L"001 Rüzgarla dans eden duygular", L"100 Rolent - İl şehri", L"101 Bose - Ticaret şehri", L"102 Ruan - Liman şehri", L"103 Zeiss - Atölye şehri", L"104 Grancel - Kraliyet başkenti", L"105 Güneşte kedi", L"106 Sınır devriyesi kolay değil", L"107 Kraliyet kalesi", L"108 Grand Arena", L"108b Grand Arena (Intro yok)", L"200 Liberl'de nasıl yürünür", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Karanlıkta dolaşma", L"301 Yolu kapatan çelik zemin", L"302 Karanlıktaki huzur", L"303 Dörtlü kuleler", L"304 Leiston kalesi", L"305 Işık boş arazisi", L"400 Sophisticated Fight -Hızlı savaş-", L"401 Sophisticated Fight -Komut savaşı-", L"402 To be Suggestive", L"403 Gümüş irade", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Hazine bekçisi", L"407 Ez!!", L"408 Solan yıldız", L"410 Pinch!!", L"500 Yıldızların olduğu yer Mızıka kısa", L"501 Kehribar aşkı Hum Ver.(Japonca)", L"501e Kehribar aşkı Hum Ver.", L"502 Kehribar aşkı Piyano Ver.", L"502b Kehribar aşkı Piyano Ver.1.5", L"503 Kehribar aşkı Lüt Ver.", L"504 Yıldızların olduğu yer Mızıka uzun", L"505 Neşeyle gidelim", L"510 Ayrılma kararlılığı", L"511 Gölgelerde hareket edenler", L"512 Kaçmasına izin verme!", L"513 Kalbimde", L"514 Ay ışığı altında", L"516 Sinsice yaklaşan kriz", L"517 Biz Capua ailesiyiz!", L"518 Ayrılış yolu", L"519 Geri alma", L"520 Lanetten kurtulma, ve...", L"521 İtiraf", L"522 Siyah Orbment", L"523 Liberl gururu", L"530 Beyaz Çiçek Madrigal Süiti - Prenses endişesi", L"531 Madrigal Süiti - Şövalyelerin ağıtı", L"532 Madrigal Süiti - Herkesin niyeti", L"533 Madrigal Süiti - Kale", L"534 Madrigal Süiti - Kolezyum", L"535 Madrigal Süiti - Düello", L"536 Madrigal Süiti - Prensesin ölümü", L"537 Madrigal Süiti - Büyük final", L""
					};
					auto PL_FC_Track = [&](int j) -> CString {
						switch (savedata.lang) {
						case 0: return CString(ti1[j]);
						case 1: return CString(ti1_en[j]);
						case 2: return CString(ti1_fr[j]);
						case 3: return CString(ti1_it[j]);
						case 4: return CString(ti1_es[j]);
						case 5: return CString(ti1_ko[j]);
						case 6: return CString(ti1_zh[j]);
						case 7: return CString(ti1_ar[j]);
						case 8: return CString(ti1_ru[j]);
						case 9: return CString(ti1_de[j]);
						case 10: return CString(ti1_pt[j]);
						case 11: return CString(ti1_nl[j]);
						case 12: return CString(ti1_pl[j]);
						case 13: return CString(ti1_tr[j]);
						default: return CString(ti1_en[j]);
						}
					};
					struct a {
						int start;
						int d1;
						int size;
						int d2;
						int loop1;
						int d3;
						int loop2;
						int d4;
					};
					//データ数は分からないので多く取っておく
					struct dd {
						int loop1;
						int loop2;
						char wav[6];
					};

					dd ddata[1000];

					a ldata = {};
					char data[33] = { 0 };
					char data0;
					int id = 0;

					CFile f;
					if (f.Open(fname, CFile::modeRead | CFile::shareDenyNone)) {
						//空の軌跡 The 1st
						//最初の8バイトは飛ばす
						f.Read(data, 8);
						for (;;) {
							//データ取得
							f.Read(data, 32);
							//ループデータにも入れる
							memcpy(&ldata, data, 32);
							//bgmで始まるまで。
							CStringA s;
							s = data;
							if (s.Left(3) == "bgm") break;
							//bgmで無い場合は、データとして保持
							ddata[id].loop1 = ldata.loop2;
							ddata[id].loop2 = ldata.loop1;
							id++;
							//16バイト飛ばす
							f.Read(data, 16);
						}
						f.SeekToBegin();
						f.Seek(0x770, 1);//現在位置から16バイト戻る
						ZeroMemory(data, 21);
						//bgmのファイル名は20文字
						//bgmファイル名取得
						CString a = L"";
						for (int i = 0; i < 59; i++) {
							f.Read(data, 20);
							CString s = CString(data);
							if (s.Find(L"fmt") > 0) break;
							if (s.Find(L"F") > 0) break;
							if (s.Find(L"b.") > 0 || s.Find(L"e.") > 0) {
								f.Read(&data0, 1); }

							//10文字目から、ed6001.wav と入っているので、001だけ抜き出す
							CString s1 = s.Mid(12, 4); s1.Replace(L".", L""); 
							a = CString(s1) + L" ";
							CString aa1a = L"";
							p.loop1 = ddata[i].loop1;
							p.loop2 = ddata[i].loop2;
							for (int j = 0;; j++) {
								CString s2 = ti1[j];
								if (s2 == "") {
									a += LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
									break;
								}
								if (s2.Left(4).Trim() == s1) {
									a = PL_FC_Track(j).Mid(4);
									aa1a = CString(ti1[j]).Left(4).Trim();
									if (aa1a == L"501e") {
										if (ft == L"bgm1.pac") a += L"(English)";
										if (ft == L"bgm2.pac") a += L"(English)";
										if (ft == L"bgm3.pac") a += LL14(L"(日本語)", L"(Japanese)", L"(Japonais)", L"(Giapponese)", L"(Japonés)", L"(일본어)", L"(日本語)", L"(اليابانية)", L"(Японский)", L"(Japanisch)", L"(Japonês)", L"(Japans)", L"(Japoński)", L"(Japonca)");
									}
									break;
								}
							}

							_tcscpy(p.name, a);
							_tcscpy(p.fol, fname + L"::" + aa1a + a);
							p.alb[0] = 0;
							p.art[0] = 0;

							if (ft == L"bgm1.pac") {
								wcscpy(p.art, LL14(L"steam版 空の軌跡 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Les Sentiers du Ciel 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam 하늘의 궤적 1st bgm1.pac", L"Steam 空之轨迹 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Тропы в Небе 1st bgm1.pac", L"Steam Himmelsleitern 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac"));
								wcscpy(p.alb, LL14(L"BGM:標準", L"BGM:Standard", L"BGM:Standard", L"BGM:Standard", L"BGM:Estándar", L"BGM:표준", L"BGM:標準", L"BGM:قياسي", L"BGM:Стандарт", L"BGM:Standard", L"BGM:Padrão", L"BGM:Standaard", L"BGM:Standard", L"BGM:Standart"));
							}
							if (ft == L"bgm2.pac") {
								wcscpy(p.art, LL14(L"steam版 空の軌跡 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Les Sentiers du Ciel 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam 하늘의 궤적 1st bgm2.pac", L"Steam 空之轨迹 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Тропы в Небе 1st bgm2.pac", L"Steam Himmelsleitern 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac"));
								wcscpy(p.alb, LL14(L"BGM:アレンジ", L"BGM:Arrange", L"BGM:Arrangement", L"BGM:Arrangiamento", L"BGM:Arreglo", L"BGM:어레인지", L"BGM:改編", L"BGM:توزيع", L"BGM:Аранжировка", L"BGM:Arrange", L"BGM:Arranjo", L"BGM:Arrange", L"BGM:Aranżacja", L"BGM:Aranjman"));
							}
							if (ft == L"bgm3.pac") {
								wcscpy(p.art, LL14(L"steam版 空の軌跡 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Les Sentiers du Ciel 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam 하늘의 궤적 1st bgm3.pac", L"Steam 空之轨迹 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Тропы в Небе 1st bgm3.pac", L"Steam Himmelsleitern 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac"));
								wcscpy(p.alb, LL14(L"BGM:オリジナル", L"BGM:Original", L"BGM:Original", L"BGM:Originale", L"BGM:Original", L"BGM:오리지널", L"BGM:原創", L"BGM:أصلي", L"BGM:Оригинал", L"BGM:Original", L"BGM:Original", L"BGM:Origineel", L"BGM:Oryginał", L"BGM:Orijinal"));
							}
							if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; syomode = 30; }
							Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
						}

						f.Close();
					}
				}
				else if (ft.Right(5) == ".opus") {
					p.sub = -6; p.loop1 = p.loop2 = 0;
					CString a = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					CString b = a.Mid(6, 1);
					int err;
					int fff = 0;
					//Ys X
					if (ft == L"y_act_e002.opus") {
						a = L"Operation SANDRAS";
						fff = 1;
					}
					if (ft == L"y_act_e002_s1.opus") {
						a = LL14(L"Operation SANDRAS(重低音)", L"Operation SANDRAS (Bass Boost)", L"Operation SANDRAS (Renfort graves)", L"Operation SANDRAS (Rinforzo bassi)", L"Operation SANDRAS (Refuerzo graves)", L"Operation SANDRAS (저음 강화)", L"Operation SANDRAS (重低音)", L"Operation SANDRAS (تعزيز الجهير)", L"Operation SANDRAS (Усиление низких)", L"Operation SANDRAS (Bassverstärkung)", L"Operation SANDRAS (Reforço graves)", L"Operation SANDRAS (Basversterking)", L"Operation SANDRAS (Wzmocnienie basów)", L"Operation SANDRAS (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b100.opus") {
						a = L"Overblaze";
						fff = 1;
					}
					if (ft == L"y_b100_s1.opus") {
						a = LL14(L"Overblaze(重低音)", L"Overblaze (Bass Boost)", L"Overblaze (Renfort graves)", L"Overblaze (Rinforzo bassi)", L"Overblaze (Refuerzo graves)", L"Overblaze (저음 강화)", L"Overblaze (重低音)", L"Overblaze (تعزيز الجهير)", L"Overblaze (Усиление низких)", L"Overblaze (Bassverstärkung)", L"Overblaze (Reforço graves)", L"Overblaze (Basversterking)", L"Overblaze (Wzmocnienie basów)", L"Overblaze (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b200.opus") {
						a = L"Through the North Wind";
						fff = 1;
					}
					if (ft == L"y_b200_s1.opus") {
						a = LL14(L"Through the North Wind(重低音)", L"Through the North Wind (Bass Boost)", L"Through the North Wind (Renfort graves)", L"Through the North Wind (Rinforzo bassi)", L"Through the North Wind (Refuerzo graves)", L"Through the North Wind (저음 강화)", L"Through the North Wind (重低音)", L"Through the North Wind (تعزيز الجهير)", L"Through the North Wind (Усиление низких)", L"Through the North Wind (Bassverstärkung)", L"Through the North Wind (Reforço graves)", L"Through the North Wind (Basversterking)", L"Through the North Wind (Wzmocnienie basów)", L"Through the North Wind (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b210.opus") {
						a = LL14(L"高鳴る鼓動", L"Pounding Heartbeat", L"Battement de cœur saccadé", L"Battito accelerato", L"Latido palpitante", L"고동치는 심장", L"劇烈的心跳", L"نبضات القلب المتسارعة", L"Учащенное сердцебиение", L"Pochendes Herzklopfen", L"Batida forte do coração", L"Bonzend hart", L"Łomoczące serce", L"Küt Küt Atan Kalp");
						fff = 1;
					}
					if (ft == L"y_b210_s1.opus") {
						a = LL14(L"高鳴る鼓動(重低音)", L"Pounding Heartbeat (Bass Boost)", L"Pounding Heartbeat (Renfort graves)", L"Pounding Heartbeat (Rinforzo bassi)", L"Pounding Heartbeat (Refuerzo graves)", L"Pounding Heartbeat (저음 강화)", L"Pounding Heartbeat (重低音)", L"Pounding Heartbeat (تعزيز الجهير)", L"Pounding Heartbeat (Усиление низких)", L"Pounding Heartbeat (Bassverstärkung)", L"Pounding Heartbeat (Reforço graves)", L"Pounding Heartbeat (Basversterking)", L"Pounding Heartbeat (Wzmocnienie basów)", L"Pounding Heartbeat (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b300.opus") {
						a = LL14(L"石火の如く", L"Like Flint", L"Comme le silex", L"Come la selce", L"Como el sílex", L"부싯돌처럼", L"如同火石", L"مثل الصوان", L"Словно кремень", L"Wie Feuerstein", L"Como pederneira", L"Als vuursteen", L"Jak krzemień", L"Çakmak Taşı Gibi");
						fff = 1;
					}
					if (ft == L"y_b300_s1.opus") {
						a = LL14(L"石火の如く(重低音)", L"Like Flint (Bass Boost)", L"Like Flint (Renfort graves)", L"Like Flint (Rinforzo bassi)", L"Like Flint (Refuerzo graves)", L"Like Flint (저음 강화)", L"Like Flint (重低音)", L"Like Flint (تعزيز الجهير)", L"Like Flint (Усиление низких)", L"Like Flint (Bassverstärkung)", L"Like Flint (Reforço graves)", L"Like Flint (Basversterking)", L"Like Flint (Wzmocnienie basów)", L"Like Flint (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b400.opus") {
						a = L"Can You Do It";
						fff = 1;
					}
					if (ft == L"y_b400_s1.opus") {
						a = LL14(L"Can You Do It(重低音)", L"Can You Do It (Bass Boost)", L"Can You Do It (Renfort graves)", L"Can You Do It (Rinforzo bassi)", L"Can You Do It (Refuerzo graves)", L"Can You Do It (저음 강화)", L"Can You Do It (重低音)", L"Can You Do It (تعزيز الجهير)", L"Can You Do It (Усиление низких)", L"Can You Do It (Bassverstärkung)", L"Can You Do It (Reforço graves)", L"Can You Do It (Basversterking)", L"Can You Do It (Wzmocnienie basów)", L"Can You Do It (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b500.opus") {
						a = LL14(L"BERSERK -戦斧の咆哮-", L"BERSERK -Roar of the Battle Axe-", L"BERSERK -Rugissement de la hache de guerre-", L"BERSERK -Ruggito dell'ascia da battaglia-", L"BERSERK -Rugido del hacha de batalla-", L"BERSERK -전부의 포효-", L"BERSERK -戰斧的咆哮-", L"BERSERK - زئير فأس المعركة", L"BERSERK -Рев боевого топора-", L"BERSERK -Brüllen der Streitaxt-", L"BERSERK -Rugido do machado de batalha-", L"BERSERK -Geknal van de strijdbijl-", L"BERSERK -Ryk topora wojennego-", L"BERSERK -Savaş Baltasının Kükreyişi-");
						fff = 1;
					}
					if (ft == L"y_b500_s1.opus") {
						a = LL14(L"BERSERK -戦斧の咆哮-(重低音)", L"BERSERK -Roar of the Battle Axe- (Bass Boost)", L"BERSERK -Roar of the Battle Axe- (Renfort graves)", L"BERSERK -Roar of the Battle Axe- (Rinforzo bassi)", L"BERSERK -Roar of the Battle Axe- (Refuerzo graves)", L"BERSERK -Roar of the Battle Axe- (저음 강화)", L"BERSERK -Roar of the Battle Axe- (重低音)", L"BERSERK -Roar of the Battle Axe- (تعزيز الجهير)", L"BERSERK -Roar of the Battle Axe- (Усиление низких)", L"BERSERK -Roar of the Battle Axe- (Bassverstärkung)", L"BERSERK -Roar of the Battle Axe- (Reforço graves)", L"BERSERK -Roar of the Battle Axe- (Basversterking)", L"BERSERK -Roar of the Battle Axe- (Wzmocnienie basów)", L"BERSERK -Roar of the Battle Axe- (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b510.opus") {
						a = LL14(L"悪意の洗礼", L"Baptism of Malice", L"Baptême de malice", L"Battesimo di malizia", L"Bautismo de malicia", L"악의의 세례", L"惡意的洗禮", L"معمودية الخبث", L"Крещение злобой", L"Taufe der Bosheit", L"Batismo de malícia", L"Doop van kwaadaardigheid", L"Chrzest złośliwości", L"Garez Vaftizi");
						fff = 1;
					}
					if (ft == L"y_b510_s1.opus") {
						a = LL14(L"悪意の洗礼(重低音)", L"Baptism of Malice (Bass Boost)", L"Baptism of Malice (Renfort graves)", L"Baptism of Malice (Rinforzo bassi)", L"Baptism of Malice (Refuerzo graves)", L"Baptism of Malice (저음 강화)", L"Baptism of Malice (重低音)", L"Baptism of Malice (تعزيز الجهير)", L"Baptism of Malice (Усиление низких)", L"Baptism of Malice (Bassverstärkung)", L"Baptism of Malice (Reforço graves)", L"Baptism of Malice (Basversterking)", L"Baptism of Malice (Wzmocnienie basów)", L"Baptism of Malice (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b520.opus") {
						a = L"The Ultimate Pleasure in My Hands";
						fff = 1;
					}
					if (ft == L"y_b520_s1.opus") {
						a = LL14(L"The Ultimate Pleasure in My Hands(重低音)", L"The Ultimate Pleasure in My Hands (Bass Boost)", L"The Ultimate Pleasure in My Hands (Renfort graves)", L"The Ultimate Pleasure in My Hands (Rinforzo bassi)", L"The Ultimate Pleasure in My Hands (Refuerzo graves)", L"The Ultimate Pleasure in My Hands (저음 강화)", L"The Ultimate Pleasure in My Hands (重低音)", L"The Ultimate Pleasure in My Hands (تعزيز الجهير)", L"The Ultimate Pleasure in My Hands (Усиление низких)", L"The Ultimate Pleasure in My Hands (Bassverstärkung)", L"The Ultimate Pleasure in My Hands (Reforço graves)", L"The Ultimate Pleasure in My Hands (Basversterking)", L"The Ultimate Pleasure in My Hands (Wzmocnienie basów)", L"The Ultimate Pleasure in My Hands (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b610.opus") {
						a = LL14(L"辿り着いた極光の下で", L"Under the Northern Lights", L"Sous les aurores boréales", L"Sotto l'aurora boreale", L"Bajo la aurora boreal", L"다다른 극광 아래에서", L"抵達極光之下", L"تحت أضواء الشمال", L"Под северным сиянием", L"Unter dem Nordlicht", L"Sob a aurora boreal", L"Onder het noorderlicht", L"Pod zorzą polarną", L"Kuzey Işıkları Altında");
						fff = 1;
					}
					if (ft == L"y_b610_s1.opus") {
						a = LL14(L"辿り着いた極光の下で(重低音)", L"Under the Northern Lights (Bass Boost)", L"Under the Northern Lights (Renfort graves)", L"Under the Northern Lights (Rinforzo bassi)", L"Under the Northern Lights (Refuerzo graves)", L"Under the Northern Lights (저음 강화)", L"Under the Northern Lights (重低音)", L"Under the Northern Lights (تعزيز الجهير)", L"Under the Northern Lights (Усиление низких)", L"Under the Northern Lights (Bassverstärkung)", L"Under the Northern Lights (Reforço graves)", L"Under the Northern Lights (Basversterking)", L"Under the Northern Lights (Wzmocnienie basów)", L"Under the Northern Lights (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b620.opus") {
						a = L"Nordics Saga -The Endless Bloody Sea-";
						fff = 1;
					}
					if (ft == L"y_b620_s1.opus") {
						a = LL14(L"Nordics Saga -The Endless Bloody Sea-(重低音)", L"Nordics Saga -The Endless Bloody Sea- (Bass Boost)", L"Nordics Saga -The Endless Bloody Sea- (Renfort graves)", L"Nordics Saga -The Endless Bloody Sea- (Rinforzo bassi)", L"Nordics Saga -The Endless Bloody Sea- (Refuerzo graves)", L"Nordics Saga -The Endless Bloody Sea- (저음 강화)", L"Nordics Saga -The Endless Bloody Sea- (重低音)", L"Nordics Saga -The Endless Bloody Sea- (تعزيز الجهير)", L"Nordics Saga -The Endless Bloody Sea- (Усиление низких)", L"Nordics Saga -The Endless Bloody Sea- (Bassverstärkung)", L"Nordics Saga -The Endless Bloody Sea- (Reforço graves)", L"Nordics Saga -The Endless Bloody Sea- (Basversterking)", L"Nordics Saga -The Endless Bloody Sea- (Wzmocnienie basów)", L"Nordics Saga -The Endless Bloody Sea- (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b700.opus") {
						a = L"Ready to Fire!";
						fff = 1;
					}
					if (ft == L"y_b700_s1.opus") {
						a = LL14(L"Ready to Fire!(重低音)", L"Ready to Fire! (Bass Boost)", L"Ready to Fire! (Renfort graves)", L"Ready to Fire! (Rinforzo bassi)", L"Ready to Fire! (Refuerzo graves)", L"Ready to Fire! (저음 강화)", L"Ready to Fire! (重低音)", L"Ready to Fire! (تعزيز الجهير)", L"Ready to Fire! (Усиление низких)", L"Ready to Fire! (Bassverstärkung)", L"Ready to Fire! (Reforço graves)", L"Ready to Fire! (Basversterking)", L"Ready to Fire! (Wzmocnienie basów)", L"Ready to Fire! (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b710.opus") {
						a = L"Hello, Those Who Can't Die";
						fff = 1;
					}
					if (ft == L"y_b710_s1.opus") {
						a = LL14(L"Hello, Those Who Can't Die(重低音)", L"Hello, Those Who Can't Die (Bass Boost)", L"Hello, Those Who Can't Die (Renfort graves)", L"Hello, Those Who Can't Die (Rinforzo bassi)", L"Hello, Those Who Can't Die (Refuerzo graves)", L"Hello, Those Who Can't Die (저음 강화)", L"Hello, Those Who Can't Die (重低音)", L"Hello, Those Who Can't Die (تعزيز الجهير)", L"Hello, Those Who Can't Die (Усиление низких)", L"Hello, Those Who Can't Die (Bassverstärkung)", L"Hello, Those Who Can't Die (Reforço graves)", L"Hello, Those Who Can't Die (Basversterking)", L"Hello, Those Who Can't Die (Wzmocnienie basów)", L"Hello, Those Who Can't Die (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_b720.opus") {
						a = L"Landing Warfare";
						fff = 1;
					}
					if (ft == L"y_b720_s1.opus") {
						a = LL14(L"Landing Warfare(重低音)", L"Landing Warfare (Bass Boost)", L"Landing Warfare (Renfort graves)", L"Landing Warfare (Rinforzo bassi)", L"Landing Warfare (Refuerzo graves)", L"Landing Warfare (저음 강화)", L"Landing Warfare (重低音)", L"Landing Warfare (تعزيز الجهير)", L"Landing Warfare (Усиление низких)", L"Landing Warfare (Bassverstärkung)", L"Landing Warfare (Reforço graves)", L"Landing Warfare (Basversterking)", L"Landing Warfare (Wzmocnienie basów)", L"Landing Warfare (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_bgm_none.opus") {
						a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"무음", L"無音", L"صمت", L"Тишина", L"Stille", L"Silêncio", L"Stilte", L"Cisza", L"Sessizlik");
						fff = 1;
					}
					if (ft == L"y_d100.opus") {
						a = LL14(L"光届かぬその奥に", L"In the Depths Where Light Doesn't Reach", L"Dans les profondeurs hors de portée de la lumière", L"Nelle profondità dove non arriva la luce", L"En las profundidades donde no llega la luz", L"빛이 닿지 않는 그 깊은 곳에", L"光線無法到達の深處", L"في الأعماق حيث لا يصل الضوء", L"В глубинах, куда не доходит свет", L"In den Tiefen, die kein Licht erreicht", L"Nas profundezas onde a luz não chega", L"In de diepten waar geen licht komt", L"W głębinach, gdzie nie sięga światło", L"Işığın Ulaşamadığı Derinliklerde");
						fff = 1;
					}
					if (ft == L"y_d100_s1.opus") {
						a = LL14(L"光届かぬその奥に(重低音)", L"In the Depths Where Light Doesn't Reach (Bass Boost)", L"In the Depths Where Light Doesn't Reach (Renfort graves)", L"In the Depths Where Light Doesn't Reach (Rinforzo bassi)", L"In the Depths Where Light Doesn't Reach (Refuerzo graves)", L"In the Depths Where Light Doesn't Reach (저음 강화)", L"In the Depths Where Light Doesn't Reach (重低音)", L"In the Depths Where Light Doesn't Reach (تعزيز الجهير)", L"In the Depths Where Light Doesn't Reach (Усиление низких)", L"In the Depths Where Light Doesn't Reach (Bassverstärkung)", L"In the Depths Where Light Doesn't Reach (Reforço graves)", L"In the Depths Where Light Doesn't Reach (Basversterking)", L"In the Depths Where Light Doesn't Reach (Wzmocnienie basów)", L"In the Depths Where Light Doesn't Reach (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_d200.opus") {
						a = L"Eerie Stillness";
						fff = 1;
					}
					if (ft == L"y_d200_s1.opus") {
						a = LL14(L"Eerie Stillness(重低音)", L"Eerie Stillness (Bass Boost)", L"Eerie Stillness (Renfort graves)", L"Eerie Stillness (Rinforzo bassi)", L"Eerie Stillness (Refuerzo graves)", L"Eerie Stillness (저음 강화)", L"Eerie Stillness (重低音)", L"Eerie Stillness (تعزيز الجهير)", L"Eerie Stillness (Усиление низких)", L"Eerie Stillness (Bassverstärkung)", L"Eerie Stillness (Reforço graves)", L"Eerie Stillness (Basversterking)", L"Eerie Stillness (Wzmocnienie basów)", L"Eerie Stillness (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_d400.opus") {
						a = LL14(L"飽くなき渇望", L"Insatiable Thirst", L"Soif insatiable", L"Sete insaziabile", L"Sed insaciable", L"끝없는 갈망", L"永無止境的渴望", L"عطش لا يرتوي", L"Ненасытная жажда", L"Unstillbares Verlangen", L"Sede insaciável", L"Onverzadigbare dorst", L"Nienasycone pragnienie", L"Doymak Bilmez Susuzluk");
						fff = 1;
					}
					if (ft == L"y_d400_s1.opus") {
						a = LL14(L"飽くなき渇望(重低音)", L"Insatiable Thirst (Bass Boost)", L"Insatiable Thirst (Renfort graves)", L"Insatiable Thirst (Rinforzo bassi)", L"Insatiable Thirst (Refuerzo graves)", L"Insatiable Thirst (저음 강화)", L"Insatiable Thirst (重低音)", L"Insatiable Thirst (تعزيز الجهير)", L"Insatiable Thirst (Усиление низких)", L"Insatiable Thirst (Bassverstärkung)", L"Insatiable Thirst (Reforço graves)", L"Insatiable Thirst (Basversterking)", L"Insatiable Thirst (Wzmocnienie basów)", L"Insatiable Thirst (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_d410.opus") {
						a = L"The Inner Darkness";
						fff = 1;
					}
					if (ft == L"y_d410_s1.opus") {
						a = LL14(L"The Inner Darkness(重低音)", L"The Inner Darkness (Bass Boost)", L"The Inner Darkness (Renfort graves)", L"The Inner Darkness (Rinforzo bassi)", L"The Inner Darkness (Refuerzo graves)", L"The Inner Darkness (저음 강화)", L"The Inner Darkness (重低音)", L"The Inner Darkness (تعزيز الجهير)", L"The Inner Darkness (Усиление низких)", L"The Inner Darkness (Bassverstärkung)", L"The Inner Darkness (Reforço graves)", L"The Inner Darkness (Basversterking)", L"The Inner Darkness (Wzmocnienie basów)", L"The Inner Darkness (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_d500.opus") {
						a = L"Hardhearted Rock Line";
						fff = 1;
					}
					if (ft == L"y_d500_s1.opus") {
						a = LL14(L"Hardhearted Rock Line(重低音)", L"Hardhearted Rock Line (Bass Boost)", L"Hardhearted Rock Line (Renfort graves)", L"Hardhearted Rock Line (Rinforzo bassi)", L"Hardhearted Rock Line (Refuerzo graves)", L"Hardhearted Rock Line (저음 강화)", L"Hardhearted Rock Line (重低音)", L"Hardhearted Rock Line (تعزيز الجهير)", L"Hardhearted Rock Line (Усиление низких)", L"Hardhearted Rock Line (Bassverstärkung)", L"Hardhearted Rock Line (Reforço graves)", L"Hardhearted Rock Line (Basversterking)", L"Hardhearted Rock Line (Wzmocnienie basów)", L"Hardhearted Rock Line (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_d600.opus") {
						a = LL14(L"夢の痕跡", L"Dream Traces", L"Traces de rêves", L"Tracce di sogni", L"Rastros de sueños", L"꿈의 흔적", L"夢的痕跡", L"آثار الأحلام", L"Следы снов", L"Traumspuren", L"Rastros de sonhos", L"Droomsporen", L"Ślady snów", L"Rüya İzleri");
						fff = 1;
					}
					if (ft == L"y_d600_s1.opus") {
						a = LL14(L"夢の痕跡(重低音)", L"Dream Traces (Bass Boost)", L"Dream Traces (Renfort graves)", L"Dream Traces (Rinforzo bassi)", L"Dream Traces (Refuerzo graves)", L"Dream Traces (저음 강화)", L"Dream Traces (重低音)", L"Dream Traces (تعزيز الجهير)", L"Dream Traces (Усиление низких)", L"Dream Traces (Bassverstärkung)", L"Dream Traces (Reforço graves)", L"Dream Traces (Basversterking)", L"Dream Traces (Wzmocnienie basów)", L"Dream Traces (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_d710.opus") {
						a = LL14(L"甲鉄戦艦ナグルファ", L"Ironclad Battleship Naglfar", L"Cuirassé Naglfar", L"Corazzata Naglfar", L"Acorazado Naglfar", L"갑철전함 나글파", L"甲鐵戰艦 Naglfar", L"البارعة المدرعة Naglfar", L"Броненосец Нагльфар", L"Panzerschiff Naglfar", L"Encouraçado Naglfar", L"Slagschip Naglfar", L"Pancernik Naglfar", L"Zırhlı Savaş Gemisi Naglfar");
						fff = 1;
					}
					if (ft == L"y_d710_s1.opus") {
						a = LL14(L"甲鉄戦艦ナグルファ(重低音)", L"Ironclad Battleship Naglfar (Bass Boost)", L"Ironclad Battleship Naglfar (Renfort graves)", L"Ironclad Battleship Naglfar (Rinforzo bassi)", L"Ironclad Battleship Naglfar (Refuerzo graves)", L"Ironclad Battleship Naglfar (저음 강화)", L"Ironclad Battleship Naglfar (重低音)", L"Ironclad Battleship Naglfar (تعزيز الجهير)", L"Ironclad Battleship Naglfar (Усиление низких)", L"Ironclad Battleship Naglfar (Bassverstärkung)", L"Ironclad Battleship Naglfar (Reforço graves)", L"Ironclad Battleship Naglfar (Basversterking)", L"Ironclad Battleship Naglfar (Wzmocnienie basów)", L"Ironclad Battleship Naglfar (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_d800.opus") {
						a = L"LILA -Innocent Wish-";
						fff = 1;
					}
					if (ft == L"y_d800_s1.opus") {
						a = LL14(L"LILA -Innocent Wish-(重低音)", L"LILA -Innocent Wish- (Bass Boost)", L"LILA -Innocent Wish- (Renfort graves)", L"LILA -Innocent Wish- (Rinforzo bassi)", L"LILA -Innocent Wish- (Refuerzo graves)", L"LILA -Innocent Wish- (저음 강화)", L"LILA -Innocent Wish- (重低音)", L"LILA -Innocent Wish- (تعزيز الجهير)", L"LILA -Innocent Wish- (Усиление низких)", L"LILA -Innocent Wish- (Bassverstärkung)", L"LILA -Innocent Wish- (Reforço graves)", L"LILA -Innocent Wish- (Basversterking)", L"LILA -Innocent Wish- (Wzmocnienie basów)", L"LILA -Innocent Wish- (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_d900.opus") {
						a = LL14(L"エギル海底神殿", L"Egil Undersea Temple", L"Temple sous-marin d'Egil", L"Tempio sottomarino di Egil", L"Templo submarino de Egil", L"에길 해저신전", L"Egil 海底神殿", L"معبد Egil تحت البحر", L"Подводный храм Эгиля", L"Egil-Unterseetempel", L"Templo submarino de Egil", L"Egil onderzeese tempel", L"Podmorska świątynia Egila", L"Egil Denizaltı Tapınağı");
						fff = 1;
					}
					if (ft == L"y_d900_s1.opus") {
						a = LL14(L"エギル海底神殿(重低音)", L"Egil Undersea Temple (Bass Boost)", L"Egil Undersea Temple (Renfort graves)", L"Egil Undersea Temple (Rinforzo bassi)", L"Egil Undersea Temple (Refuerzo graves)", L"Egil Undersea Temple (저음 강화)", L"Egil Undersea Temple (重低音)", L"Egil Undersea Temple (تعزيز الجهير)", L"Egil Undersea Temple (Усиление низких)", L"Egil Undersea Temple (Bassverstärkung)", L"Egil Undersea Temple (Reforço graves)", L"Egil Undersea Temple (Basversterking)", L"Egil Undersea Temple (Wzmocnienie basów)", L"Egil Undersea Temple (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_d1010.opus") {
						a = L"The Paradise Lost of Norman";
						fff = 1;
					}
					if (ft == L"y_d1010_s1.opus") {
						a = LL14(L"The Paradise Lost of Norman(重低音)", L"The Paradise Lost of Norman (Bass Boost)", L"The Paradise Lost of Norman (Renfort graves)", L"The Paradise Lost of Norman (Rinforzo bassi)", L"The Paradise Lost of Norman (Refuerzo graves)", L"The Paradise Lost of Norman (저음 강화)", L"The Paradise Lost of Norman (重低音)", L"The Paradise Lost of Norman (تعزيز الجهير)", L"The Paradise Lost of Norman (Усиление низких)", L"The Paradise Lost of Norman (Bassverstärkung)", L"The Paradise Lost of Norman (Reforço graves)", L"The Paradise Lost of Norman (Basversterking)", L"The Paradise Lost of Norman (Wzmocnienie basów)", L"The Paradise Lost of Norman (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_e001.opus") {
						a = L"Yesterday's Journey, Tomorrow's Dream";
						fff = 1;
					}
					if (ft == L"y_e002.opus") {
						a = L"Surging Pressure";
						fff = 1;
					}
					if (ft == L"y_e003.opus") {
						a = L"Turn of the Tide";
						fff = 1;
					}
					if (ft == L"y_e004.opus") {
						a = LL14(L"あの時からずっと…", L"Ever Since That Day...", L"Depuis ce jour-là...", L"Da quel giorno...", L"Desde aquel día...", L"그때부터 줄곧...", L"從那時起一直...", L"منذ ذلك اليوم...", L"С того самого дня...", L"Seit jenem Tag...", L"Desde aquele dia...", L"Sinds die dag...", L"Od tamtego dnia...", L"O Günden Beri...");
						fff = 1;
					}
					if (ft == L"y_e005.opus") {
						a = L"Waver as the Wave";
						fff = 1;
					}
					if (ft == L"y_e006.opus") {
						a = LL14(L"切っても切れない絆", L"Unbreakable Bonds", L"Liens indéfectibles", L"Legami indissolubili", L"Vínculos inquebrantables", L"뗄래야 뗄 수 없는 인연", L"無法割捨的羈絆", L"روابط لا تنفصم", L"Неразрывные узы", L"Unzerbrechliche Bande", L"Laços inquebráveis", L"Onbreekbare banden", L"Nierozerwalne więzi", L"Yıkılmaz Bağlar");
						fff = 1;
					}
					if (ft == L"y_e007.opus") {
						a = LL14(L"灰色の深層", L"Gray Depths", L"Profondeurs grises", L"Profondità grigie", L"Profundidades grises", L"회색의 심층", L"灰色的深層", L"أعماق رمادية", L"Серые глубины", L"Graue Tiefen", L"Profundezas cinzentas", L"Grijze diepten", L"Szare głębiny", L"Gri Derinlikler");
						fff = 1;
					}
					if (ft == L"y_e007_s1.opus") {
						a = LL14(L"灰色の深層(重低音)", L"Gray Depths (Bass Boost)", L"Gray Depths (Renfort graves)", L"Gray Depths (Rinforzo bassi)", L"Gray Depths (Refuerzo graves)", L"Gray Depths (저음 강화)", L"Gray Depths (重低音)", L"Gray Depths (تعزيز الجهير)", L"Gray Depths (Усиление низких)", L"Gray Depths (Bassverstärkung)", L"Gray Depths (Reforço graves)", L"Gray Depths (Basversterking)", L"Gray Depths (Wzmocnienie basów)", L"Gray Depths (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_e008.opus") {
						a = L"Premonition of Turmoil";
						fff = 1;
					}
					if (ft == L"y_e009.opus") {
						a = LL14(L"歪な願望", L"Twisted Desire", L"Désir tordu", L"Desiderio distorto", L"Deseo retorcido", L"비뚤어진 열망", L"扭曲的願望", L"رغبة ملتوية", L"Искаженное желание", L"Verdrehtes Verlangen", L"Desejo distorcido", L"Verdraaid verlangen", L"Skręcone pragnienie", L"Çarpık Arzu");
						fff = 1;
					}
					if (ft == L"y_e010.opus") {
						a = L"The Road so Far, the Future Ahead";
						fff = 1;
					}
					if (ft == L"y_e011.opus") {
						a = L"Violent Warriors";
						fff = 1;
					}
					if (ft == L"y_e011_s1.opus") {
						a = LL14(L"Violent Warriors(重低音)", L"Violent Warriors (Bass Boost)", L"Violent Warriors (Renfort graves)", L"Violent Warriors (Rinforzo bassi)", L"Violent Warriors (Refuerzo graves)", L"Violent Warriors (저음 강화)", L"Violent Warriors (重低音)", L"Violent Warriors (تعزيز الجهير)", L"Violent Warriors (Усиление низких)", L"Violent Warriors (Bassverstärkung)", L"Violent Warriors (Reforço graves)", L"Violent Warriors (Basversterking)", L"Violent Warriors (Wzmocnienie basów)", L"Violent Warriors (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_e012.opus") {
						a = LL14(L"手筈通りに", L"As Planned", L"Comme prévu", L"Come pianificato", L"Como se planeó", L"계획대로", L"按照計畫", L"كما هو مخطط", L"Как и планировалось", L"Wie geplant", L"Como planejado", L"Zoals gepland", L"Zgodnie z planem", L"Planlandığı Gibi");
						fff = 1;
					}
					if (ft == L"y_e013.opus") {
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						fff = 1;
					}
					if (ft == L"y_e014.opus") {
						a = L"ROLLO -Because of Its Purity-";
						fff = 1;
					}
					if (ft == L"y_e015.opus") {
						a = L"Deep Unconscious";
						fff = 1;
					}
					if (ft == L"y_e015_s1.opus") {
						a = LL14(L"Deep Unconscious(重低音)", L"Deep Unconscious (Bass Boost)", L"Deep Unconscious (Renfort graves)", L"Deep Unconscious (Rinforzo bassi)", L"Deep Unconscious (Refuerzo graves)", L"Deep Unconscious (저음 강화)", L"Deep Unconscious (重低音)", L"Deep Unconscious (تعزيز الجهير)", L"Deep Unconscious (Усиление низких)", L"Deep Unconscious (Bassverstärkung)", L"Deep Unconscious (Reforço graves)", L"Deep Unconscious (Basversterking)", L"Deep Unconscious (Wzmocnienie basów)", L"Deep Unconscious (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f100.opus") {
						a = L"TO BE FREE";
						fff = 1;
					}
					if (ft == L"y_f100_s1.opus") {
						a = LL14(L"TO BE FREE(重低音)", L"TO BE FREE (Bass Boost)", L"TO BE FREE (Renfort graves)", L"TO BE FREE (Rinforzo bassi)", L"TO BE FREE (Refuerzo graves)", L"TO BE FREE (저음 강화)", L"TO BE FREE (重低音)", L"TO BE FREE (تعزيز الجهير)", L"TO BE FREE (Усиление низких)", L"TO BE FREE (Bassverstärkung)", L"TO BE FREE (Reforço graves)", L"TO BE FREE (Basversterking)", L"TO BE FREE (Wzmocnienie basów)", L"TO BE FREE (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f110.opus") {
						a = L"Brother's Footsteps on the Island";
						fff = 1;
					}
					if (ft == L"y_f110_s1.opus") {
						a = LL14(L"Brother's Footsteps on the Island(重低音)", L"Brother's Footsteps on the Island (Bass Boost)", L"Brother's Footsteps on the Island (Renfort graves)", L"Brother's Footsteps on the Island (Rinforzo bassi)", L"Brother's Footsteps on the Island (Refuerzo graves)", L"Brother's Footsteps on the Island (저음 강화)", L"Brother's Footsteps on the Island (重低音)", L"Brother's Footsteps on the Island (تعزيز الجهير)", L"Brother's Footsteps on the Island (Усиление низких)", L"Brother's Footsteps on the Island (Bassverstärkung)", L"Brother's Footsteps on the Island (Reforço graves)", L"Brother's Footsteps on the Island (Basversterking)", L"Brother's Footsteps on the Island (Wzmocnienie basów)", L"Brother's Footsteps on the Island (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f120.opus") {
						a = L"Burn with You";
						fff = 1;
					}
					if (ft == L"y_f120_s1.opus") {
						a = LL14(L"Burn with You(重低音)", L"Burn with You (Bass Boost)", L"Burn with You (Renfort graves)", L"Burn with You (Rinforzo bassi)", L"Burn with You (Refuerzo graves)", L"Burn with You (저음 강화)", L"Burn with You (重低音)", L"Burn with You (تعزيز الجهير)", L"Burn with You (Усиление низких)", L"Burn with You (Bassverstärkung)", L"Burn with You (Reforço graves)", L"Burn with You (Basversterking)", L"Burn with You (Wzmocnienie basów)", L"Burn with You (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f130.opus") {
						a = L"Destined to Keep Running";
						fff = 1;
					}
					if (ft == L"y_f130_s1.opus") {
						a = LL14(L"Destined to Keep Running(重低音)", L"Destined to Keep Running (Bass Boost)", L"Destined to Keep Running (Renfort graves)", L"Destined to Keep Running (Rinforzo bassi)", L"Destined to Keep Running (Refuerzo graves)", L"Destined to Keep Running (저음 강화)", L"Destined to Keep Running (重低音)", L"Destined to Keep Running (تعزيز الجهير)", L"Destined to Keep Running (Усиление низких)", L"Destined to Keep Running (Bassverstärkung)", L"Destined to Keep Running (Reforço graves)", L"Destined to Keep Running (Basversterking)", L"Destined to Keep Running (Wzmocnienie basów)", L"Destined to Keep Running (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f140.opus") {
						a = L"Ride on Mana!";
						fff = 1;
					}
					if (ft == L"y_f140_s1.opus") {
						a = LL14(L"Ride on Mana!(重低音)", L"Ride on Mana! (Bass Boost)", L"Ride on Mana! (Renfort graves)", L"Ride on Mana! (Rinforzo bassi)", L"Ride on Mana! (Refuerzo graves)", L"Ride on Mana! (저음 강화)", L"Ride on Mana! (重低音)", L"Ride on Mana! (تعزيز الجهير)", L"Ride on Mana! (Усиление низких)", L"Ride on Mana! (Bassverstärkung)", L"Ride on Mana! (Reforço graves)", L"Ride on Mana! (Basversterking)", L"Ride on Mana! (Wzmocnienie basów)", L"Ride on Mana! (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f150.opus") {
						a = L"Heat Hazard";
						fff = 1;
					}
					if (ft == L"y_f150_s1.opus") {
						a = LL14(L"Heat Hazard(重低音)", L"Heat Hazard (Bass Boost)", L"Heat Hazard (Renfort graves)", L"Heat Hazard (Rinforzo bassi)", L"Heat Hazard (Refuerzo graves)", L"Heat Hazard (저음 강화)", L"Heat Hazard (重低音)", L"Heat Hazard (تعزيز الجهير)", L"Heat Hazard (Усиление низких)", L"Heat Hazard (Bassverstärkung)", L"Heat Hazard (Reforço graves)", L"Heat Hazard (Basversterking)", L"Heat Hazard (Wzmocnienie basów)", L"Heat Hazard (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f160.opus") {
						a = LL14(L"瞳の中の少年剣士", L"Young Swordsman in My Eyes", L"Le jeune épéiste dans mes yeux", L"Il giovane spadaccino nei miei occhi", L"El joven espadachín en mis ojos", L"눈동자 속의 소년 검사", L"瞳孔中的少年劍士", L"سياف شاب في عيني", L"Юный мечник в моих глазах", L"Junger Schwertkämpfer in meinen Augen", L"Jovem espadachim nos meus olhos", L"Jonge zwaardvechter in mijn ogen", L"Młody szermierz w moich oczach", L"Gözlerimdeki Genç Kılıç Ustası");
						fff = 1;
					}
					if (ft == L"y_f160_s1.opus") {
						a = LL14(L"瞳の中の少年剣士(重低音)", L"Young Swordsman in My Eyes (Bass Boost)", L"Young Swordsman in My Eyes (Renfort graves)", L"Young Swordsman in My Eyes (Rinforzo bassi)", L"Young Swordsman in My Eyes (Refuerzo graves)", L"Young Swordsman in My Eyes (저음 강화)", L"Young Swordsman in My Eyes (重低音)", L"Young Swordsman in My Eyes (تعزيز الجهير)", L"Young Swordsman in My Eyes (Усиление низких)", L"Young Swordsman in My Eyes (Bassverstärkung)", L"Young Swordsman in My Eyes (Reforço graves)", L"Young Swordsman in My Eyes (Basversterking)", L"Young Swordsman in My Eyes (Wzmocnienie basów)", L"Young Swordsman in My Eyes (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f200.opus") {
						a = LL14(L"錨を揚げろ！", L"Weigh Anchor!", L"Levez l'ancre !", L"Leva l'ancora!", L"¡Leven anclas!", L"닻을 올려라!", L"起錨！", L"ارفع المرساة!", L"Поднять якорь!", L"Anker lichten!", L"Levantar âncora!", L"Licht het anker!", L"Podnieść kotwicę!", L"Demir Al!");
						fff = 1;
					}
					if (ft == L"y_f200_s1.opus") {
						a = LL14(L"錨を揚げろ！(重低音)", L"Weigh Anchor! (Bass Boost)", L"Weigh Anchor! (Renfort graves)", L"Weigh Anchor! (Rinforzo bassi)", L"Weigh Anchor! (Refuerzo graves)", L"Weigh Anchor! (저음 강화)", L"Weigh Anchor! (重低音)", L"Weigh Anchor! (تعزيز الجهير)", L"Weigh Anchor! (Усиление низких)", L"Weigh Anchor! (Bassverstärkung)", L"Weigh Anchor! (Reforço graves)", L"Weigh Anchor! (Basversterking)", L"Weigh Anchor! (Wzmocnienie basów)", L"Weigh Anchor! (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f210.opus") {
						a = LL14(L"悠き海に生きる者", L"Those Who Live in the Vast Sea", L"Ceux qui vivent dans la mer vaste", L"Coloro che vivono nel vasto mare", L"Aquellos que viven en el mar vasto", L"유구한 바다에 사는 자", L"生活在悠久大海的人", L"أولئك الذين يعيشون في البحر الشاسع", L"Те, кто живет в бескрайнем море", L"Die im weiten Meer leben", L"Aqueles que vivem no mar vasto", L"Zij die in de onmetelijke zee leven", L"Ci, którzy żyją w rozległym morzu", L"Engin Denizlerde Yaşayanlar");
						fff = 1;
					}
					if (ft == L"y_f210_s1.opus") {
						a = LL14(L"悠き海に生きる者(重低音)", L"Those Who Live in the Vast Sea (Bass Boost)", L"Those Who Live in the Vast Sea (Renfort graves)", L"Those Who Live in the Vast Sea (Rinforzo bassi)", L"Those Who Live in the Vast Sea (Refuerzo graves)", L"Those Who Live in the Vast Sea (저음 강화)", L"Those Who Live in the Vast Sea (重低音)", L"Those Who Live in the Vast Sea (تعزيز الجهير)", L"Those Who Live in the Vast Sea (Усиление низких)", L"Those Who Live in the Vast Sea (Bassverstärkung)", L"Those Who Live in the Vast Sea (Reforço graves)", L"Those Who Live in the Vast Sea (Basversterking)", L"Those Who Live in the Vast Sea (Wzmocnienie basów)", L"Those Who Live in the Vast Sea (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f220.opus") {
						a = LL14(L"コンパスは踊る", L"The Compass Dances", L"La boussole danse", L"La bussola danza", L"La brújula danza", L"나침반은 춤춘다", L"羅盤在跳舞", L"البوصلة ترقص", L"Компас танцует", L"Der Kompass tanzt", L"A bússola dança", L"Het kompas danst", L"Kompas tańczy", L"Pusula Dans Ediyor");
						fff = 1;
					}
					if (ft == L"y_f220_s1.opus") {
						a = LL14(L"コンパスは踊る(重低音)", L"The Compass Dances (Bass Boost)", L"The Compass Dances (Renfort graves)", L"The Compass Dances (Rinforzo bassi)", L"The Compass Dances (Refuerzo graves)", L"The Compass Dances (저음 강화)", L"The Compass Dances (重低音)", L"The Compass Dances (تعزيز الجهير)", L"The Compass Dances (Усиление низких)", L"The Compass Dances (Bassverstärkung)", L"The Compass Dances (Reforço graves)", L"The Compass Dances (Basversterking)", L"The Compass Dances (Wzmocnienie basów)", L"The Compass Dances (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f230.opus") {
						a = LL14(L"開闢の海", L"Sea of Genesis", L"Mer de la genèse", L"Mare della genesi", L"Mar de la génesis", L"개벽의 바다", L"開闢之海", L"بحر التكوين", L"Море сотворения", L"Meer der Schöpfung", L"Mar da gênese", L"Zee van de genesis", L"Morze genezy", L"Yaratılış Denizi");
						fff = 1;
					}
					if (ft == L"y_f230_s1.opus") {
						a = LL14(L"開闢の海(重低音)", L"Sea of Genesis (Bass Boost)", L"Sea of Genesis (Renfort graves)", L"Sea of Genesis (Rinforzo bassi)", L"Sea of Genesis (Refuerzo graves)", L"Sea of Genesis (저음 강화)", L"Sea of Genesis (重低音)", L"Sea of Genesis (تعزيز الجهير)", L"Sea of Genesis (Усиление низких)", L"Sea of Genesis (Bassverstärkung)", L"Sea of Genesis (Reforço graves)", L"Sea of Genesis (Basversterking)", L"Sea of Genesis (Wzmocnienie basów)", L"Sea of Genesis (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_f310.opus") {
						a = L"If I Could Go Back to Those Days";
						fff = 1;
					}
					if (ft == L"y_f310_s1.opus") {
						a = LL14(L"If I Could Go Back to Those Days(重低音)", L"If I Could Go Back to Those Days (Bass Boost)", L"If I Could Go Back to Those Days (Renfort graves)", L"If I Could Go Back to Those Days (Rinforzo bassi)", L"If I Could Go Back to Those Days (Refuerzo graves)", L"If I Could Go Back to Those Days (저음 강화)", L"If I Could Go Back to Those Days (重低音)", L"If I Could Go Back to Those Days (تعزيز الجهير)", L"If I Could Go Back to Those Days (Усиление низких)", L"If I Could Go Back to Those Days (Bassverstärkung)", L"If I Could Go Back to Those Days (Reforço graves)", L"If I Could Go Back to Those Days (Basversterking)", L"If I Could Go Back to Those Days (Wzmocnienie basów)", L"If I Could Go Back to Those Days (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_gameover.opus") {
						a = L"SO MUCH FOR TODAY (Ys X Ver.)";
						fff = 1;
					}
					if (ft == L"y_op.opus") {
						a = L"Facing the Distant Horizon";
						fff = 1;
					}
					if (ft == L"y_op_lp.opus") {
						a = L"Facing the Distant Horizon (lp)";
						fff = 1;
					}
					if (ft == L"y_t100.opus") {
						a = L"Our Hometown";
						fff = 1;
					}
					if (ft == L"y_t100_s1.opus") {
						a = LL14(L"Our Hometown(重低音)", L"Our Hometown (Bass Boost)", L"Our Hometown (Renfort graves)", L"Our Hometown (Rinforzo bassi)", L"Our Hometown (Refuerzo graves)", L"Our Hometown (저음 강화)", L"Our Hometown (重低音)", L"Our Hometown (تعزيز الجهير)", L"Our Hometown (Усиление низких)", L"Our Hometown (Bassverstärkung)", L"Our Hometown (Reforço graves)", L"Our Hometown (Basversterking)", L"Our Hometown (Wzmocnienie basów)", L"Our Hometown (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_t200.opus") {
						a = LL14(L"根ざすべき場所", L"Where We Belong", L"Là où nous appartenons", L"Il posto a cui apparteniamo", L"El lugar al que pertenecemos", L"뿌리 내려야 할 곳", L"落地生根之處", L"حيث ننتمي", L"Там, где наш дом", L"Wo wir hingehören", L"Onde pertencemos", L"Waar we thuishoren", L"Miejsce, do którego należymy", L"Ait Olduğumuz Yer");
						fff = 1;
					}
					if (ft == L"y_t200_s1.opus") {
						a = LL14(L"根ざすべき場所(重低音)", L"Where We Belong (Bass Boost)", L"Where We Belong (Renfort graves)", L"Where We Belong (Rinforzo bassi)", L"Where We Belong (Refuerzo graves)", L"Where We Belong (저음 강화)", L"Where We Belong (重低音)", L"Where We Belong (تعزيز الجهير)", L"Where We Belong (Усиление низких)", L"Where We Belong (Bassverstärkung)", L"Where We Belong (Reforço graves)", L"Where We Belong (Basversterking)", L"Where We Belong (Wzmocnienie basów)", L"Where We Belong (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_t300.opus") {
						a = L"Sometime Siesta";
						fff = 1;
					}
					if (ft == L"y_t300_s1.opus") {
						a = LL14(L"Sometime Siesta(重低音)", L"Sometime Siesta (Bass Boost)", L"Sometime Siesta (Renfort graves)", L"Sometime Siesta (Rinforzo bassi)", L"Sometime Siesta (Refuerzo graves)", L"Sometime Siesta (저음 강화)", L"Sometime Siesta (重低音)", L"Sometime Siesta (تعزيز الجهير)", L"Sometime Siesta (Усиление низких)", L"Sometime Siesta (Bassverstärkung)", L"Sometime Siesta (Reforço graves)", L"Sometime Siesta (Basversterking)", L"Sometime Siesta (Wzmocnienie basów)", L"Sometime Siesta (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_t301.opus") {
						a = L"Innermost Feelings";
						fff = 1;
					}
					if (ft == L"y_t301_s1.opus") {
						a = LL14(L"Innermost Feelings(重低音)", L"Innermost Feelings (Bass Boost)", L"Innermost Feelings (Renfort graves)", L"Innermost Feelings (Rinforzo bassi)", L"Innermost Feelings (Refuerzo graves)", L"Innermost Feelings (저음 강화)", L"Innermost Feelings (重低音)", L"Innermost Feelings (تعزيز الجهير)", L"Innermost Feelings (Усиление низких)", L"Innermost Feelings (Bassverstärkung)", L"Innermost Feelings (Reforço graves)", L"Innermost Feelings (Basversterking)", L"Innermost Feelings (Wzmocnienie basów)", L"Innermost Feelings (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_t500.opus") {
						a = LL14(L"情景に揺蕩う", L"Drifting in the Scene", L"Dérivant dans la scène", L"Oscillando nella scena", L"Derivando en la escena", L"정경에 요동치다", L"沉浸於情景中", L"الانجراف في المشهد", L"Дрейфуя в пейзаже", L"In der Szenerie treiben", L"Derivando na cena", L"Drijvend in de scène", L"Dryfując w scenerii", L"Manzarada Süzülmek");
						fff = 1;
					}
					if (ft == L"y_t500_s1.opus") {
						a = LL14(L"情景に揺蕩う(重低音)", L"Drifting in the Scene (Bass Boost)", L"Drifting in the Scene (Renfort graves)", L"Drifting in the Scene (Rinforzo bassi)", L"Drifting in the Scene (Refuerzo graves)", L"Drifting in the Scene (저음 강화)", L"Drifting in the Scene (重低音)", L"Drifting in the Scene (تعزيز الجهير)", L"Drifting in the Scene (Усиление низких)", L"Drifting in the Scene (Bassverstärkung)", L"Drifting in the Scene (Reforço graves)", L"Drifting in the Scene (Basversterking)", L"Drifting in the Scene (Wzmocnienie basów)", L"Drifting in the Scene (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_t600.opus") {
						a = LL14(L"盾の兄弟", L"Shield Brothers", L"Frères de bouclier", L"Fratelli di scudo", L"Hermanos de escudo", L"방패의 형제", L"盾之兄弟", L"إخوة الدرع", L"Братья по щиту", L"Schildbrüder", L"Irmãos de escudo", L"Schildbroeders", L"Bracia tarczy", L"Kalkan Kardeşliği");
						fff = 1;
					}
					if (ft == L"y_t600_s1.opus") {
						a = LL14(L"盾の兄弟(重低音)", L"Shield Brothers (Bass Boost)", L"Shield Brothers (Renfort graves)", L"Shield Brothers (Rinforzo bassi)", L"Shield Brothers (Refuerzo graves)", L"Shield Brothers (저음 강화)", L"Shield Brothers (重低音)", L"Shield Brothers (تعزيز الجهير)", L"Shield Brothers (Усиление низких)", L"Shield Brothers (Bassverstärkung)", L"Shield Brothers (Reforço graves)", L"Shield Brothers (Basversterking)", L"Shield Brothers (Wzmocnienie basów)", L"Shield Brothers (Bas güçlendirme)");
						fff = 1;
					}
					if (ft == L"y_title.opus") {
						a = LL14(L"その優しさは誰のため", L"For Whom Is That Kindness", L"Pour qui est cette gentillesse", L"Per chi è quella gentilezza", L"Para quién es esa amabilidad", L"그 다정함은 누구を 위한 것인가", L"那份溫柔是為了誰", L"لمن هذا اللطف", L"Для кого эта доброта", L"Wem gilt diese Güte", L"Para quem é essa bondade", L"Voor wie is die vriendelijkheid", L"Dla kogo ta dobroć", L"Bu Nezaket Kimin İçin");
						fff = 1;
					}

					if (fff == 0)
						if (a.Left(2) == "y9") {
							if (a.Mid(4, 4) == "b001") { a = "FEEL FORCE"; }
							if (a.Mid(4, 4) == "b002") { a = "TROUBLEMAKER"; }
							if (a.Mid(4, 4) == "b003") { a = "MONSTRUM SPECTRUM"; }
							if (a.Mid(4, 4) == "b004") { a = "LACRIMA CRISIS"; }
							if (a.Mid(4, 4) == "b005") { a = "WELCOME TO CHAOS"; }
							if (a.Mid(4, 4) == "b006") { a = "JUDGEMENT TIME"; }
							if (a.Mid(4, 4) == "b007") { a = "KNOCK ON NOX"; }
							if (a.Mid(4, 4) == "b008") { a = "ANIMA ERGASTULUM"; }
							if (a.Mid(4, 5) == "b010b") { a = "URBAN TERROR"; }
							if (a.Mid(4, 4) == "b010") { a = LL14(L"URBAN TERROR(イントロあり)", L"URBAN TERROR (With Intro)", L"URBAN TERROR (Avec Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (인트로 있음)", L"URBAN TERROR (含前奏)", L"URBAN TERROR (مع مقدمة)", L"URBAN TERROR (С интро)", L"URBAN TERROR (Mit Intro)", L"URBAN TERROR (Com Intro)", L"URBAN TERROR (Met Intro)", L"URBAN TERROR (Z intro)", L"URBAN TERROR (Girişli)"); }
							if (a.Mid(4, 5) == "b011b") { a = "DREAMING IN THE GRIMWALD"; }
							if (a.Mid(4, 4) == "b011") { a = LL14(L"DREAMING IN THE GRIMWALD(イントロあり)", L"DREAMING IN THE GRIMWALD (With Intro)", L"DREAMING IN THE GRIMWALD (Avec Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (인트로 있음)", L"DREAMING IN THE GRIMWALD (含前奏)", L"DREAMING IN THE GRIMWALD (مع مقدمة)", L"DREAMING IN THE GRIMWALD (С интро)", L"DREAMING IN THE GRIMWALD (Mit Intro)", L"DREAMING IN THE GRIMWALD (Com Intro)", L"DREAMING IN THE GRIMWALD (Met Intro)", L"DREAMING IN THE GRIMWALD (Z intro)", L"DREAMING IN THE GRIMWALD (Girişli)"); }
							if (a.Mid(4, 4) == "b012") { a = "WILD CARD"; }
							if (a.Mid(4, 5) == "b014b") { a = "FULL MOON CEREMONY"; }
							if (a.Mid(4, 4) == "b014") { a = LL14(L"FULL MOON CEREMONY(イントロあり)", L"FULL MOON CEREMONY (With Intro)", L"FULL MOON CEREMONY (Avec Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (인트로 있음)", L"FULL MOON CEREMONY (含前奏)", L"FULL MOON CEREMONY (مع مقدمة)", L"FULL MOON CEREMONY (С инトロ)", L"FULL MOON CEREMONY (Mit Intro)", L"FULL MOON CEREMONY (Com Intro)", L"FULL MOON CEREMONY (Met Intro)", L"FULL MOON CEREMONY (Z intro)", L"FULL MOON CEREMONY (Girişli)"); }
							if (a.Mid(4, 4) == "d101") { a = "HEART BEAT SHAKER"; }
							if (a.Mid(4, 4) == "d201") { a = "CLOACA MAXIMA"; }
							if (a.Mid(4, 4) == "d301") { a = "RUIN OF DRY MOAT"; }
							if (a.Mid(4, 4) == "d401") { a = "MARIONETTE, MARIONETTE"; }
							if (a.Mid(4, 4) == "d501") { a = "THE CAVE OF GROAN"; }
							if (a.Mid(4, 4) == "d601") { a = "EVAN MACHA"; }
							if (a.Mid(4, 4) == "d701") { a = "A QUARRY RUIN"; }
							if (a.Mid(4, 4) == "d702") { a = "CROSSING A/A"; }
							if (a.Mid(4, 4) == "d801") { a = "CATCH ME IF YOU CAN"; }
							if (a.Mid(4, 4) == "d901") { a = "ALCHEMY LAB"; }
							if (a.Mid(4, 4) == "d911") { a = "STRATEGIC ZONE"; }
							if (a.Mid(4, 5) == "d1001") { a = "FORTRESS UNDERGROUND"; }
							if (a.Mid(4, 5) == "d2001") { a = "DANCE WITH TRAPS"; }
							if (a.Mid(4, 4) == "e001") { a = "APRILIS"; }
							if (a.Mid(4, 4) == "e002") { a = "TAKE IT EASY!"; }
							if (a.Mid(4, 4) == "e003") { a = "PETITE FLEUR"; }
							if (a.Mid(4, 4) == "e004") { a = "EYES ON..."; }
							if (a.Mid(4, 4) == "e005") { a = "FORGOTTEN DAYS"; }
							if (a.Mid(4, 4) == "e006") { a = "PRISON OF BALDUQ -LIVE THE FUTURE-"; }
							if (a.Mid(4, 4) == "e007") { a = "PRISON OF BALDUQ -YEARNING-"; }
							if (a.Mid(4, 4) == "e008") { a = L"IL ÉTAIT UNE FOIS"; }
							if (a.Mid(4, 4) == "e009") { a = "WHO KNOWS THE TRUTH?"; }
							if (a.Mid(4, 4) == "e010") { a = "DECISION"; }
							if (a.Mid(4, 4) == "e011") { a = "STAGNANT POOL"; }
							if (a.Mid(4, 4) == "e013") { a = "INQUISITION"; }
							if (a.Mid(4, 4) == "e014") { a = "SILLY MEETING"; }
							if (a.Mid(4, 4) == "e016") { a = "MONSTRUM NOX"; }
							if (a.Mid(4, 4) == "e017") { a = "CHALLENGER'S ROAD"; }
							if (a.Mid(4, 4) == "e018") { a = "RED MULETA"; }
							if (a.Mid(4, 4) == "e019") { a = "NAB THE TAIL"; }
							if (a.Mid(4, 4) == "e020") { a = "THUS SPOKE AN ALCHEMIST"; }
							if (a.Mid(4, 4) == "e023") { a = "DENOUEMENT"; }
							if (a.Mid(4, 4) == "e024") { a = "INVITATION TO THE CRIMSON NIGHT"; }
							if (a.Mid(4, 4) == "f101") { a = "NORSE WIND"; }
							if (a.Mid(4, 4) == "f201") { a = "TRANQUIL SILENCE"; }
							if (a.Mid(4, 4) == "f301") { a = "GLESSING WAY!"; }
							if (a.Mid(4, 4) == "f501") { a = "DESERT AFTER TEARS"; }
							if (a.Mid(4, 4) == "muon") { a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"무음", L"無音", L"صمت", L"Тишина", L"Stille", L"Silêncio", L"Stilte", L"Cisza", L"Sessizlik"); }
							if (a.Mid(4, 4) == "t101") { a = "PRISONCITY"; }
							if (a.Mid(4, 4) == "t102") { a = "IN PROFILE, ON BELFRY"; }
							if (a.Mid(4, 4) == "t103") { a = "NEW LIFE"; }
							if (a.Mid(4, 4) == "t104") { a = "GRIA RECOLLECTION"; }
							if (a.Mid(4, 4) == "t201") { a = "BAR \"DANDELION\""; }
							if (a.Mid(4, 4) == "t301") { a = "AMBIGUOUS TERRITORY"; }
							if (a.Mid(4, 4) == "t402") { a = "WALTZ FOR GRACE"; }
							if (a.Mid(4, 4) == "t501") { a = "HEAT AND SPLENDOR"; }
							if (a.Mid(4, 4) == "t901") { a = "ONLY THE CORPSE GOES OUT"; }
							if (a.Mid(4, 4) == "t902") { a = "A GOLDEN KEY CAN OPEN ANY DOOR"; }
							if (a.Mid(4, 4) == "tbox") { a = "TREASURE BOX -Ys IX-"; }
						}
						else {
							switch (_ttoi(a.Mid(2, 5))) {
							case 81004:
								a = LL14(L"罪と罰と偽りと", L"Sin, Punishment and Falsehood", L"Péché, punition et mensonge", L"Peccato, punizione e falsità", L"Pecado, castigo y falsedad", L"죄와 벌과 거짓과", L"罪、罰與虛偽", L"الخطيئة والعقاب والزيف", L"Грех, наказание и ложь", L"Sünde, Strafe und Falschheit", L"Pecado, castigo e falsidade", L"Zonde, straf en valsheid", L"Grzech, kara i fałsz", L"Günah, Ceza ve Sahtelik");
								break;
							case 81005:
								a = LL14(L"昏き鐘の残響", L"Resonance of the Dark Bell", L"Résonance de la cloche sombre", L"Risonanza della campana oscura", L"Resonancia de la campana oscura", L"어두운 종의 잔향", L"昏暗之鐘的殘響", L"صدى الجرس المظلم", L"Резонанс темного колокола", L"Resonanz der dunklen Glocke", L"Ressonância do sino sombrio", L"Resonantie van de duistere klok", L"Rezonans mrocznego dzwonu", L"Karanlık Çanın Yankısı");
								break;
							case 81006:
								a = "Right on the Mark";
								break;
							case 81007:
								a = LL14(L"悪夢ふたたび", L"Nightmare Again", L"Le cauchemar recommence", L"Incubo di nuovo", L"Pesadilla de nuevo", L"악몽 다시 한번", L"噩夢重現", L"الكابوس مرة أخرى", L"Кошмар снова", L"Albtraum erneut", L"Pesadelo novamente", L"Nachtmerrie opnieuw", L"Koszmar ponownie", L"Kabus Yeniden");
								break;
							case 81008:
								a = "Crossbell Nostalgia";
								break;
							case 81009:
								a = LL14(L"創まりの円庭", L"Garden of Beginnings", L"Jardin des commencements", L"Giardino degli inizi", L"Jardín de los inicios", L"시작의 원정", L"創始之圓庭", L"حديقة البدايات", L"Сад начал", L"Garten der Anfänge", L"Jardim dos começos", L"Tuin van het begin", L"Ogród początków", L"Başlangıç Bahçesi");
								break;
							case 81010:
								a = "Mysterious Element";
								break;
							case 81012:
								a = "Stand Up Again and Again!";
								break;
							case 81014:
								a = "Purgatory Scream";
								break;
							case 81015:
								a = LL14(L"さざめきの途路", L"Path of Tumult", L"Chemin du tumulte", L"Sentiero del tumulto", L"Senda del tumulto", L"웅성거리는 길", L"嘈雜的途徑", L"مسار الاضطراب", L"Путь суматохи", L"Pfad des Tumults", L"Caminho do tumulto", L"Pad van rumoer", L"Ścieżka zgiełku", L"Gürültülü Yol");
								break;
							case 81016:
								a = LL14(L"蒼の大地に生きる者", L"Those Who Live on the Azure Land", L"Ceux qui vivent sur la terre d'azur", L"Coloro che vivono sulla terra azzurra", L"Aquellos que viven en la tierra azul", L"푸른 대지에 사는 자", L"生活在蒼之大地的人", L"أولئك الذين يعيشون على الأرض الزرقاء", L"Те, кто живет на лазурной земле", L"Die auf dem azurblauen Land leben", L"Aqueles que vivem na terra azul", L"Zij die op het azuurblauwe land leven", L"Ci, którzy żyją na błękitnej ziemi", L"Mavi Topraklarda Yaşayanlar");
								break;
							case 81017:
								a = LL14(L"黎明の鐘", L"Bell of Dawn", L"Cloche de l'aube", L"Campana dell'alba", L"Campana del alba", L"여명의 종", L"黎明之鐘", L"جرس الفجر", L"Колокол рассвета", L"Glocke der Dämmerung", L"Sino da aurora", L"Klok van de dageraad", L"Dzwon świtu", L"Şafak Çanı");
								break;
							case 81018:
								a = LL14(L"レメディファンタジア -仲間とともに-", L"Remedi Fantasia -With Comrades-", L"Remedi Fantasia -Avec des camarades-", L"Remedi Fantasia -Con i compagni-", L"Remedi Fantasia -Con camaradas-", L"레메디 판타지아 ~동료와 함께~", L"Remedi Fantasia -與夥伴一起-", L"Remedi Fantasia - مع الرفاق", L"Remedi Fantasia -С товарищами-", L"Remedi Fantasia -Mit Kameraden-", L"Remedi Fantasia -Com camaradas-", L"Remedi Fantasia -Met kameraden-", L"Remedi Fantasia -Z towarzyszami-", L"Remedi Fantasia -Yoldaşlarla-");
								break;
							case 81019:
								a = "Slight Suspicion";
								break;
							case 81020:
								a = "Maliciousness in the Mirror";
								break;
							case 81021:
								a = LL14(L"暗澹たる世界", L"Dark World", L"Monde sombre", L"Mondo oscuro", L"Mundo oscuro", L"암담한 세계", L"暗淡的世界", L"عالم مظلم", L"Мрачный мир", L"Dunkle Welt", L"Mundo sombrio", L"Duistere wereld", L"Mroczny świat", L"Karanlık Dünya");
								break;
							case 81022:
								a = LL14(L"ひとときの温もり", L"Brief Warmth", L"Bref répit de chaleur", L"Breve calore", L"Breve calor", L"잠시 동안의 온기", L"片刻的溫暖", L"دفء عابر", L"Краткое тепло", L"Kurze Wärme", L"Breve calor", L"Korte warmte", L"Krótkie ciepło", L"Kısa Süreli Sıcaklık");
								break;
							case 81023:
								a = LL14(L"今、創まりのとき", L"Now, the Moment of Creation", L"Maintenant, le moment de la création", L"Ora, il momento della creazione", L"Ahora, el momento de la creación", L"지금, 시작의 시간", L"現在，創始之時", L"الآن، لحظة التكوين", L"Теперь момент сотворения", L"Nun, der Moment der Schöpfung", L"Agora, o momento da criação", L"Nu, het moment van creatie", L"Teraz moment stworzenia", L"Şimdi, Yaratılış Anı");
								break;
							case 81024:
								a = "KERAUNOS -Fear and Hatred-";
								break;
							case 81025:
								a = LL14(L"亡失われた魂", L"Lost Souls", L"Âmes perdues", L"Anime perse", L"Almas perdidas", L"잃어버린 영혼들", L"迷失的靈魂", L"أرواح مفقودة", L"Потерянные души", L"Verlorene Seelen", L"Almas perdidas", L"Verloren zielen", L"Zagubione dusze", L"Kayıp Ruhlar");
								break;
							case 81026:
								a = LL14(L"穏やかな時間", L"Peaceful Time", L"Temps paisible", L"Tempo pacifico", L"Tiempo pacífico", L"평온한 시간", L"平靜的時光", L"وقت هادئ", L"Мирное время", L"Friedliche Zeit", L"Tempo pacífico", L"Vredige tijd", L"Spokojny czas", L"Huzurlu Vakit");
								break;
							case 81027:
								break;
							case 81028:
								a = LL14(L"運命という名の歯車", L"Gears of Fate", L"Engrenages du destin", L"Ingranaggi del destino", L"Engranajes del destino", L"운명이라는 이름의 톱니바퀴", L"名為命運的齒輪", L"تروس القدر", L"Шестеренки судьбы", L"Zahnräder des Schicksals", L"Engrenagens do destino", L"Raderen van het lot", L"Koła zębate losu", L"Kader Çarkları");
								break;
							case 81200:
								a = "Crossing Causal Lines";
								break;
							case 81201:
								a = "Glittering Mirage";
								break;
							case 81202:
								a = "Like a Whirlwind";
								break;
							case 81203:
								a = "Hide and Seek by Myself";
								break;
							case 81315:
								a = LL14(L"鉱山町マインツ -創Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-", L"Mines Town Mainz -Reverie Ver.-");
								break;
							case 81316:
								a = LL14(L"木霊の道 -創Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-", L"Path of Echoes -Reverie Ver.-");
								break;
							case 81317:
								a = "Raindrops with the Wind";
								break;
							case 81319:
								a = LL14(L"陽溜まりにただいまを", L"Home in the Sunshine", L"Retour au soleil", L"A casa sotto il sole", L"Hogar bajo el sol", L"햇살 속에 다녀왔습니다를", L"在陽光下說我回來了", L"العودة للمنزل في ضوء الشمس", L"Домой под лучами солнца", L"Zuhause im Sonnenschein", L"Lar sob o sol", L"Thuis in de zon", L"Dom w słońcu", L"Güneş Işığında Eve Dönüş");
								break;
							case 81320:
								a = "Wind-Up Yesterday!";
								break;
							case 81321:
								a = LL14(L"零の邂逅", L"Zero Encounter", L"Rencontre de zéro", L"Incontro zero", L"Encuentro cero", L"제로의 해후", L"零之邂逅", L"لقاء الصفر", L"Встреча Зеро", L"Zero-Begegnung", L"Encontro zero", L"Zero ontmoeting", L"Spotkanie zero", L"Sıfır Karşılaşması");
								break;
							case 81322:
								a = LL14(L"影の見えざる手", L"Invisible Hand in the Shadows", L"Main invisible dans l'ombre", L"Mano invisibile nelle ombre", L"Mano invisible en las sombras", L"그림자의 보이지 않는 손", L"影子那看不見的手", L"يد خفية في الظلال", L"Невидимая рука в тени", L"Unsichtbare Hand im Schatten", L"Mão invisível nas sombras", L"Onzichtbare hand in de schaduw", L"Niewidzialna ręka w cieniu", L"Gölgedeki Görünmez El");
								break;
							case 81950: case 81951: case 81952: case 81953: case 81954:
							case 81955: case 81956: case 81957: case 81958: case 81961:
							case 81962: case 81963: case 81964: case 81965: case 81966:
							case 81967: case 81968: case 81969:
								break;
							case 82065:
								a = LL14(L"鋼鉄牙城", L"Iron Fortress", L"Forteresse d'acier", L"Fortezza d'acciaio", L"Fortaleza de acero", L"강철아성", L"鋼鐵牙城", L"حصن فولاذي", L"Железная крепость", L"Eiserne Festung", L"Fortaleza de aço", L"IJzeren vesting", L"Stalowa twierdza", L"Demir Kale");
								break;
							case 82113:
								a = "Zero Break Battle";
								break;
							case 82114:
								a = "Stake Everything Strategy";
								break;
							case 82123:
								break;
							case 82124:
								a = "POM's Paradise";
								break;
							case 82125:
								a = LL14(L"波間に弾む心", L"Heart Bouncing on the Waves", L"Cœur bondissant sur les vagues", L"Cuore che rimbalza sulle onde", L"Corazón saltando en las olas", L"물결 사이에 들뜨는 마음", L"在波浪間雀躍的心", L"قلب يقفز على الأمواج", L"Сердце, прыгающее на волнах", L"Herz, das auf den Wellen hüpft", L"Coração saltitando nas ondas", L"Hart dat stuitert op de golven", L"Serce skaczące na falach", L"Dalgalarda Hoplayan Kalp");
								break;
							case 82129:
								a = "Reverse Babel";
								break;
							case 82131:
								a = "Aim a Gun at the Bullet";
								break;
							case 82133:
								a = "Section G.F.S. II";
								break;
							case 82135:
								a = "Magical Revolt";
								break;
							case 82136:
								a = LL14(L"流麗闘冴", L"Elegant Battle", L"Combat élégant", L"Battaglia elegante", L"Batalla elegante", L"유려투채", L"流麗鬥冴", L"معركة أنيقة", L"Элегантная битва", L"Eleganter Kampf", L"Batalha elegante", L"Elegant gevecht", L"Elegancka bitwa", L"Zarif Savaş");
								break;
							case 82137:
								a = "The Road to All-Out War";
								break;
							case 82138:
								a = "LAPIS";
								break;
							case 82140:
								a = "Invisible Hilly Country";
								break;
							case 82141:
								a = LL14(L"ひとかけらの光明", L"Sliver of Light", L"Lueur d'espoir", L"Barlume di luce", L"Rayo de luz", L"한 조각의 광명", L"一絲光明", L"خيط من الضوء", L"Лучик света", L"Ein Schimmer Licht", L"Raio de luz", L"Lichtstraaltje", L"Promyk światła", L"Bir Işık Hüzmesi");
								break;
							case 82143:
								a = LL14(L"反攻の烽火", L"Beacon of Counterattack", L"Signal de contre-attaque", L"Segnale di contrattacco", L"Señal de contraataque", L"반격의 봉화", L"反攻的烽火", L"منارة الهجوم المضاد", L"Маяк контратаки", L"Leuchtfeuer des Gegenangriffs", L"Sinal de contra-ataque", L"Baken van de tegenaanval", L"Sygnał kontrataku", L"Karşı Atak İşareti");
								break;
							case 82147:
								a = "Rapid Wind";
								break;
							case 82148:
								a = "NO END NO WORLD -Instrumental Ver.-";
								break;
							case 82150:
								a = "Be Caught Up!";
								break;
							case 82151:
								a = "Breeding Innumerable Arms";
								break;
							case 82152:
								a = "The Destination of FATE";
								break;
							case 82154:
								a = "Twinkle Attack";
								break;
							case 82157:
								a = "Sword of Swords";
								break;
							case 82158:
								a = LL14(L"今宵は宴と参りましょう", L"Tonight We Feast", L"Ce soir, nous festoyons", L"Stasera banchettiamo", L"Esta noche festejamos", L"오늘 밤은 연회를 엽시다", L"今晚讓我們舉行宴會吧", L"الليلة سنقيم وليمة", L"Сегодня мы пируем", L"Heute Abend wird gefeiert", L"Esta noite vamos festejar", L"Vanavond vieren we feest", L"Dziś wieczorem ucztujemy", L"Bu Gece Ziyafet Çekelim");
								break;
							case 82159:
								a = "Flash Your Fighting Spirit";
								break;
							case 82161:
								a = LL14(L"鈍色に這う", L"Crawling in Gray", L"Ramper dans le gris", L"Strisciando nel grigio", L"Gateando en el gris", L"잿빛으로 기어가다", L"在灰色中爬行", L"الزحف في الرمادي", L"Ползти в сером", L"Kriechen im Grau", L"Rastejando no cinza", L"Kruipen in het grijs", L"Pełzanie w szarości", L"Gri İçinde Sürünmek");
								break;
							case 82163:
								a = "Pyro Labyrinth";
								break;
							case 82164:
								a = LL14(L"優しさを未来に託して", L"Entrust Kindness to the Future", L"Confier la gentillesse au futur", L"Affidare la gentilezza al futuro", L"Confiar la amabilidad al futuro", L"다정함을 미래에 맡기고", L"將溫柔託付給未來", L"استئمان اللطف للمستقبل", L"Вверить доброту будущему", L"Güte der Zukunft anvertrauen", L"Confiar a bondade ao futuro", L"Vriendelijkheid aan de toekomst toevertrouwen", L"Powierzyć dobroć przyszłości", L"Nezaketi Geleceğe Emanet Etmek");
								break;
							case 82166:
								a = LL14(L"高らかに、誇らしく", L"Loud and Proud", L"Fort et fier", L"Forte e fiero", L"Fuerte y orgulloso", L"드높게, 자랑스럽게", L"高聲地，自豪地", L"بصوت عالٍ وبفخر", L"Громко и гордо", L"Laut und stolz", L"Alto e orgulhoso", L"Luid en trots", L"Głośno i dumnie", L"Yüksek Sesle ve Gururla");
								break;
							case 82170:
								a = "Infinity Rage";
								break;
							case 82171:
								a = "Heavy Violent Match";
								break;
							case 82173:
								a = "Roar of Evil Spirits";
								break;
							case 82174:
								a = "Bad Dream Invasion";
								break;
							case 82175:
								a = "Golden Fever";
								break;
							case 82177:
								a = "The Perfect Steel of ZERO";
								break;
							case 82178:
								a = "Twilight Hermitage";
								break;
							case 82179:
								a = "Something Luxury...?";
								break;
							case 82183:
								a = "Challenger Invigorated";
								break;
							case 82184:
								a = LL14(L"このあと美味しくいただきました", L"Then We Ate Deliciously", L"Ensuite, nous avons mangé délicieusement", L"Poi abbiamo mangiato deliziosamente", L"Luego comimos deliciosamente", L"이후 맛있게 먹었습니다", L"在那之後我們美味地享用了", L"ثم أكلنا بلذة", L"Затем мы вкусно поели", L"Dann haben wir köstlich gegessen", L"Depois comemos deliciosamente", L"Daarna hebben we heerlijk gegeten", L"Potem zjedliśmy wybornie", L"Sonra Afiyetle Yedik");
								break;
							case 82186:
								a = "Emergency Order";
								break;
							case 82188:
								a = LL14(L"激烈! 撃滅! ミシュナイダー!!", L"Fierce! Crush! Mishnayder!!", L"Féroce ! Écraser ! Mishnayder !!", L"Feroce! Schiaccia! Mishnayder!!", L"¡Feroz! ¡Aplasta! ¡Mishnayder!", L"격렬! 격멸! 미슈나이더!!", L"激烈！擊滅！Mishnayder！！", L"شرس! سحق! Mishnayder!!", L"Яростно! Разгромить! Mishnayder!!", L"Heftig! Zerschmettern! Mishnayder!!", L"Feroz! Esmagar! Mishnayder!!", L"Heftig! Verpletter! Mishnayder!!", L"Gwałtownie! Zmiażdżyć! Mishnayder!!", L"Sert! Ez Geç! Mishnayder!!");
								break;
							case 82189:
								a = "Life Goes On";
								break;
							default:
								if (a == L"ed8_inf_ex.opus") {
									a = LL14(L"夢幻の彼方へ", L"To the Realm of Dreams", L"Vers le royaume des rêves", L"Verso il regno dei sogni", L"Hacia el reino de los sueños", L"몽환의 저편으로", L"往夢幻的彼方", L"إلى عالم الأحلام", L"В царство снов", L"In das Reich der Träume", L"Para o reino dos sonhos", L"Naar het rijk der dromen", L"Do krainy snów", L"Rüyalar Alemine");
								}
							}
							switch (_ttoi(a.Mid(2, 4))) {
							case 8001:
								a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"특과 클래스 《VII組》", L"特科班《VII組》", L"الفصل السابع", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klas VII", L"Klasa VII", L"Sınıf VII");
								break;
							case 8002:
								a = LL14(L"スタートライン", L"Start Line", L"Ligne de départ", L"Linea di partenza", L"Línea de salida", L"스타트 라인", L"起跑線", L"خط البداية", L"Стартовая линия", L"Startlinie", L"Linha de partida", L"Startlijn", L"Linia startu", L"Başlangıç Çizgisi");
								break;
							case 8003:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8004:
								a = "Youthful Victory";
								break;
							case 8006:
								a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours vers l'avant", L"Sempre avanti", L"Siempre adelante", L"오로지 앞으로", L"一心一意，向前邁進", L"إلى الأمام دائماً", L"Только вперед", L"Immer vorwärts", L"Sempre em frente", L"Altijd vooruit", L"Zawsze do przodu", L"Daima İleri");
								break;
							case 8007:
								a = LL14(L"縁 -つなぐもの-", L"Fate -Connecting-", L"Destin -Connexion-", L"Destino -Connessione-", L"Destino -Conexión-", L"인연 ~이어주는 것~", L"緣 -連繫者-", L"القدر - الترابط", L"Судьба -Связующее звено-", L"Schicksal -Verbindend-", L"Destino -Conectando-", L"Lot -Verbindend-", L"Los -Łączący-", L"Kader -Bağlayıcı-");
								break;
							case 8102:
								a = LL14(L"翡翠の公都バリアハート", L"Jade Capital Bareahard", L"Capitale de jade Bareahard", L"Capitale di giada Bareahard", L"Capital de jade Bareahard", L"비취의 공도 Bareahard", L"翡翠公都 Bareahard", L"عاصمة اليشم Bareahard", L"Нефритовая столица Bareahard", L"Jade-Hauptstadt Bareahard", L"Capital de jade Bareahard", L"Jade-hoofdstad Bareahard", L"Jadeitowa stolica Bareahard", L"Yeşim Başkenti Bareahard");
								break;
							case 8104:
								a = LL14(L"黒銀の鋼都ルーレ", L"Iron City Roer", L"Ville de fer Roer", L"Città del ferro Roer", L"Ciudad del hierro Roer", L"흑은의 강철 도시 Roer", L"黑銀鋼都 Roer", L"مدينة Roer الحديدية", L"Железный город Roer", L"Eisenstadt Roer", L"Cidade do ferro Roer", L"IJzerstad Roer", L"Żelazne miasto Roer", L"Demir Şehir Roer");
								break;
							case 8150:
								a = LL14(L"下校途中にパンケーキ", L"Pancakes on the Way Home", L"Des pancakes sur le chemin du retour", L"Pancake sulla via di casa", L"Tortitas de camino a casa", L"하교 길에 팬케이크", L"下學路上的煎餅", L"بانكيك في طريق العودة", L"Блинчики по дороге домой", L"Pfannkuchen auf dem Heimweg", L"Panquecas no caminho para casa", L"Pannenkoeken op weg naar huis", L"Naleśniki w drodze do domu", L"Eve Giderken Krep");
								break;
							case 8151:
								a = LL14(L"可能性は無限大", L"Infinite Possibilities", L"Possibilités infinies", L"Possibilità infinite", L"Posibilidades infinitas", L"가능성은 무한대", L"可能性是無限的", L"احتمالات لا نهائية", L"Бесконечные возможности", L"Unbegrenzte Möglichkeiten", L"Possibilidades infinitas", L"Oneindige mogelijkheden", L"Nieskończone możliwości", L"Sonsuz Olasılıklar");
								break;
							case 8152:
								a = LL14(L"夜のしじまに", L"In the Night Silence", L"Dans le silence nocturne", L"Nel silenzio della notte", L"En el silencio de la noche", L"밤의 정적 속에", L"在深夜的靜謐中", L"في صمت الليل", L"В ночной тишине", L"In der nächtlichen Stille", L"No silêncio da noite", L"In de nachtelijke stilte", L"W nocnej ciszy", L"Gece Sessizliğinde");
								break;
							case 8153:
								a = LL14(L"夕景", L"Evening Scene", L"Scène de soirée", L"Scena serale", L"Escena vespertina", L"저녁 풍경", L"夕陽美景", L"مشهد المساء", L"Вечерний пейзаж", L"Abendszene", L"Cena noturna", L"Avondtafereel", L"Wieczorna scena", L"Akşam Manzarası");
								break;
							case 8154:
								a = LL14(L"新しい朝", L"New Morning", L"Nouveau matin", L"Nuovo mattino", L"Nueva mañana", L"새로운 아침", L"新的早晨", L"صباح جديد", L"Новое утро", L"Neuer Morgen", L"Nova manhã", L"Nieuwe ochtend", L"Nowy poranek", L"Yeni Sabah");
								break;
							case 8155:
								a = LL14(L"束の間の里帰り", L"Brief Homecoming", L"Bref retour au pays", L"Breve ritorno a casa", L"Breve regreso al hogar", L"잠시 동안의 귀향", L"短暫的返郷", L"عودة قصيرة للوطن", L"Краткое возвращение домой", L"Kurze Heimkehr", L"Breve retorno ao lar", L"Korte thuiskomst", L"Krótki powrót do domu", L"Kısa Bir Memleket Dönüşü");
								break;
							case 8156:
								a = LL14(L"白亜の旧都セントアーク", L"White City St. Ark", L"Vieille capitale blanche St. Ark", L"Antica capitale bianca St. Ark", L"Vieja capital blanca St. Ark", L"백아의 구도セントアーク", L"白亞舊都 St. Ark", L"العاصمة القديمة البيضاء St. Ark", L"Белая старая столица Сент-Арк", L"Weiße alte Hauptstadt St. Ark", L"Antiga capital branca St. Ark", L"Witte oude hoofdstad St. Ark", L"Biała stara stolica St. Ark", L"Beyaz Eski Başkent St. Ark");
								break;
							case 8157:
								a = LL14(L"紡績町パルム", L"Spinning Town Parm", L"Ville textile Parm", L"Città tessile Parm", L"Pueblo textil Parm", L"방적 마을 Parm", L"紡織鎮 Parm", L"بلدة الغزل Parm", L"Ткацкий городок Парм", L"Spinnereistadt Parm", L"Vila têxtil Parm", L"Spinnerijstad Parm", L"Tkackie miasto Parm", L"Dokuma Kasabası Parm");
								break;
							case 8158:
								a = LL14(L"籠の中のクロスベル", L"Crossbell in a Cage", L"Crossbell en cage", L"Crossbell in gabbia", L"Crossbell en una jaula", L"장벽 속의 Crossbell", L"籠中 Crossbell", L"Crossbell في قفص", L"Кроссбелл в клетке", L"Crossbell im Käfig", L"Crossbell em uma gaiola", L"Crossbell in een kooi", L"Crossbell w klatce", L"Kafesteki Crossbell");
								break;
							case 8159:
								a = LL14(L"今、成すべきこと", L"What Must Be Done Now", L"Ce qui doit être fait maintenant", L"Ciò che deve essere fatto ora", L"Lo que debe hacerse ahora", L"지금, 해야 할 일", L"現在，應做之事", L"ما يجب فعله الآن", L"Что должно быть сделано сейчас", L"Was jetzt getan werden muss", L"O que deve ser feito agora", L"Wat nu moet worden gedaan", L"Co należy teraz zrobić", L"Şimdi Yapılması Gereken");
								break;
							case 8160:
								a = LL14(L"歓楽都市ラクウェル", L"Pleasure City Raquel", L"Ville de plaisir Raquel", L"Città del piacere Raquel", L"Ciudad del placer Raquel", L"환락 도시 Raquel", L"歡樂都市 Raquel", L"مدينة المتعة Raquel", L"Город развлечений Ракель", L"Vergnügungsstadt Raquel", L"Cidade do prazer Raquel", L"Plezierstad Raquel", L"Miasto rozrywki Raquel", L"Eğlence Şehri Raquel");
								break;
							case 8161:
								a = LL14(L"静かなる駆け引き", L"Quiet Maneuvering", L"Manoeuvres silencieuses", L"Manovre silenziose", L"Maniobras silenciosas", L"고요한 밀당", L"靜默的周旋", L"مناورة هادئة", L"Тихое маневрирование", L"Stilles Manövrieren", L"Manobras silenciosas", L"Stil manoeuvreren", L"Ciche manewry", L"Sessiz Manevralar");
								break;
							case 8162:
								a = LL14(L"赫奕たるヘイムダル", L"Splendid Heimdallr", L"Heimdallr splendide", L"Splendida Heimdallr", L"Espléndida Heimdallr", L"혁혁한 Heimdallr", L"赫赫有名的 Heimdallr", L"Heimdallr الرائعة", L"Великолепный Хеймдалль", L"Prächtiges Heimdallr", L"Esplêndida Heimdallr", L"Prachtig Heimdallr", L"Wspaniały Heimdallr", L"Görkemli Heimdallr");
								break;
							case 8163:
								a = LL14(L"紺碧の海都オルディス", L"Azure Port City Ordys", L"Ville portuaire d'azur Ordys", L"Città portuale azzurra Ordys", L"Ciudad portuaria azul Ordys", L"쪽빛의 해도 Ordys", L"紺碧海都 Ordys", L"مدينة Ordys المرفئية الزرقاء", L"Лазурный портовый город Ордис", L"Azurblaue Hafenstadt Ordys", L"Cidade portuária azul Ordys", L"Azuurblauwe havenstad Ordys", L"Błękitne miasto portowe Ordys", L"Gök Mavisi Liman Şehri Ordys");
								break;
							case 8164:
								a = LL14(L"最前線都市", L"Front-line City", L"Ville de première ligne", L"Città di prima linea", L"Ciudad de primera línea", L"최전선 도시", L"最前線都市", L"مدينة الخطوط الأمامية", L"Прифронтовой город", L"Frontstadt", L"Cidade de linha de frente", L"Frontstad", L"Miasto na linii frontu", L"Cephe Şehri");
								break;
							case 8165:
								a = "Base Camp";
								break;
							case 8166:
								a = LL14(L"精強なる兵たち", L"Elite Soldiers", L"Soldats d'élite", L"Soldati d'élite", L"Soldados de élite", L"정강한 병사들", L"精銳的士兵們", L"جنود النخبة", L"Элитные солдаты", L"Elitesoldaten", L"Soldados de elite", L"Elitesoldaten", L"Elitarni żołnierze", L"Seçkin Askerler");
								break;
							case 8168:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8170:
								a = LL14(L"隠れ里エリン", L"Hidden Village Erin", L"Village caché d'Erin", L"Villaggio nascosto di Erin", L"Aldea oculta de Erin", L"숨겨진 마을 에린", L"隠之里 Erin", L"قرية Erin المخفية", L"Скрытая деревня Эрин", L"Verborgenes Dorf Erin", L"Vila oculta de Erin", L"Verborgen dorp Erin", L"Ukryta wioska Erin", L"Gizli Köy Erin");
								break;
							case 8171:
								a = LL14(L"潜入調査", L"Infiltration", L"Infiltration", L"Infiltrazione", L"Infiltración", L"잠입 조사", L"潛入調查", L"تسلل", L"Инфильтрация", L"Infiltration", L"Infiltração", L"Infiltratie", L"Infiltracja", L"Sızma Harekatı");
								break;
							case 8172:
								a = LL14(L"昏冥の中で", L"In the Darkness", L"Dans les ténèbres", L"Nell'oscurità", L"En la oscuridad", L"혼명 속에서", L"在昏暗之中", L"في الظلام", L"Во тьме", L"In der Dunkelheit", L"Na escuridão", L"In de duisternis", L"W ciemności", L"Karanlıkta");
								break;
							case 8173:
								a = LL14(L"紅き閃影 -光まとう翼-", L"Crimson Flash -Wings of Light-", L"Éclat carmin -Ailes de lumière-", L"Lampo cremisi -Ali di luce-", L"Destello carmesí -Alas de luz-", L"붉은 섬영 ~빛을 두른 날개~", L"紅之閃影 -披光之翼-", L"وميض قرمزية - أجنحة الضوء", L"Алая вспышка -Крылья света-", L"Purpurroter Blitz -Flügel des Lichts-", L"Lampejo carmesim -Asas de luz-", L"Karmozijnrode flits -Vleugels van licht-", L"Szkarłatny błysk -Skrzydła światła-", L"Kızıl Parıltı -Işık Kanatları-");
								break;
							case 8174:
								a = LL14(L"聖ウルスラ医科大学 -閃Ver.-", L"St. Ursula Medical College -CS Ver.-", L"St. Ursula Medical College -CS Ver.-", L"St. Ursula Medical College -CS Ver.-", L"St. Ursula Medical College -CS Ver.-", L"성 우르술라 의과대학 -閃Ver.-", L"聖烏爾蘇拉醫科大學 -閃Ver.-", L"كلية سانت أورسولا الطبية -CS Ver.-", L"Медицинский колледж Св. Урсулы -CS Ver.-", L"Medizinische Hochschule St. Ursula -CS Ver.-", L"Faculdade de Medicina Sta. Úrsula -CS Ver.-", L"Medisch College St. Ursula -CS Ver.-", L"Kolegium Medyczne św. Urszuli -CS Ver.-", L"Aziz Ursula Tıp Koleji -CS Ver.-");
								break;
							case 8175:
								a = LL14(L"一抹の不安、一縷の望み", L"Hint of Unease, Ray of Hope", L"Une pointe d'inquiétude, un rayon d'espoir", L"Un briciolo di ansia, un raggio di speranza", L"Un rastro de inquietud, un rayo de esperanza", L"일말의 불안, 한 줄기 희망", L"一抹不安，一縷希望", L"تلميح من القلق، شعاع من الأمل", L"Тень беспокойства, луч надежды", L"Ein Hauch von Unbehagen, ein Hoffnungsschimmer", L"Um toque de inquietação, um raio de esperança", L"Een spoortje van onrust, een straal van hoop", L"Cień niepokoju, promień nadziei", L"Bir Parça Huzursuzluk, Bir Umut Işığı");
								break;
							case 8176:
								a = "Lyrical Amber";
								break;
							case 8177:
								a = LL14(L"水面を渡る風", L"Wind Over the Water", L"Vent sur l'eau", L"Vento sull'acqua", L"Viento sobre el agua", L"수면을 가르는 바람", L"拂過水面的風", L"رياح فوق الماء", L"Ветер над водой", L"Wind über dem Wasser", L"Vento sobre a água", L"Wind over het water", L"Wiatr nad wodą", L"Su Üstündeki Rüzgar");
								break;
							case 8250:
								a = LL14(L"流れる雲の彼方に", L"Beyond the Drifting Clouds", L"Au-delà des nuages dérivants", L"Oltre le nuvole erranti", L"Más allá de las nubes errantes", L"흐르는 구름 저편으로", L"流雲的彼方", L"ما وراء السحب العابرة", L"За плывущими облаками", L"Jenseits der ziehenden Wolken", L"Além das nuvens flutuantes", L"Voorbij de drijvende wolken", L"Poza płynące chmury", L"Süzülen Bulutların Ötesinde");
								break;
							case 8251:
								a = LL14(L"静寂の小路", L"Path of Silence", L"Chemin du silence", L"Sentiero del silenzio", L"Senda del silencio", L"정적의 소로", L"安靜的小徑", L"مسار الصمت", L"Путь тишины", L"Pfad der Stille", L"Caminho do silêncio", L"Pad van stilte", L"Ścieżka ciszy", L"Sessizlik Yolu");
								break;
							case 8252:
								a = LL14(L"崖谷の狭間", L"Gap of the Cliff", L"Le fossé de la falaise", L"Divario della scogliera", L"Brecha del acantilado", L"절벽 사이의 틈", L"崖谷狹間", L"فجوة الجرف", L"Разрыв утеса", L"Spalt der Klippe", L"Fenda do penhasco", L"Kloof van de klif", L"Szczelina klifu", L"Uçurum Boşluğu");
								break;
							case 8253:
								a = "Weathering Road";
								break;
							case 8260:
								a = LL14(L"彼の地へ向かって", L"Toward That Land", L"Vers cette terre", L"Verso quella terra", L"Hacia esa tierra", L"그 땅을 향하여", L"邁向那片土地", L"نحو تلك الأرض", L"К той земле", L"Jenem Land entgegen", L"Em direção àquela terra", L"Naar dat land", L"Ku tamtej krainie", L"O Diyara Doğru");
								break;
							case 8261:
								a = LL14(L"終焉の途へ", L"Toward the End", L"Vers la fin", L"Verso la fine", L"Hacia el final", L"종언의 길로", L"邁向終結", L"نحو النهاية", L"К концу", L"Dem Ende entgegen", L"Em direção ao fim", L"Naar het einde", L"Ku końcowi", L"Sona Doğru");
								break;
							case 8262:
								a = LL14(L"全てを識るもの -閃Ver.-", L"Omniscient -CS Ver.-", L"L'omniscient -CS Ver.-", L"L'onniscente -CS Ver.-", L"El omnisciente -CS Ver.-", L"모든 것을 아는 자 -閃Ver.-", L"全知者 -閃Ver.-", L"العليم -CS Ver.-", L"Всеведущий -CS Ver.-", L"Der Allwissende -CS Ver.-", L"O onisciente -CS Ver.-", L"De alwetende -CS Ver.-", L"Wszechwiedzący -CS Ver.-", L"Her Şeyi Bilen -CS Ver.-");
								break;
							case 8263:
								a = LL14(L"たそがれ緑道", L"Twilight Green Path", L"Chemin vert du crépuscule", L"Sentiero verde del crepuscolo", L"Senda verde del crepúsculo", L"황혼의 녹도", L"黄昏綠道", L"مسار الغسق الأخضر", L"Сумеречная зеленая тропа", L"Zwielichtiger grüner Pfad", L"Caminho verde do crepúsculo", L"Groene schemerpad", L"Zielona ścieżka zmierzchu", L"Alacakaranlık Yeşil Yolu");
								break;
							case 8311:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8350:
								a = LL14(L"アインヘル小要塞", L"Einhel Fortress", L"Forteresse d'Einhel", L"Fortezza di Einhel", L"Fortaleza de Einhel", L"Einhel 소요새", L"Einhel 小要塞", L"حصن Einhel", L"Крепость Эйнхель", L"Einhel-Festung", L"Fortaleza de Einhel", L"Vesting Einhel", L"Twierdza Einhel", L"Einhel Kalesi");
								break;
							case 8351:
								a = LL14(L"伝承の裏で", L"Behind the Legend", L"Derrière la légende", L"Dietro la leggenda", L"Detrás de la leyenda", L"전승의 이면에서", L"傳承的背後", L"خلف الأسطورة", L"За легендой", L"Hinter der Legende", L"Atrás da lenda", L"Achter de legende", L"Za legendą", L"Efsanenin Arkasında");
								break;
							case 8352:
								a = "Unplanned Residue";
								break;
							case 8353:
								a = LL14(L"忘れられし幻夢の狭間 -閃Ver.-", L"Forgotten Phantasmal Gap -CS Ver.-", L"Écart phantasmatique oublié -CS Ver.-", L"Divario fantasmatico dimenticato -CS Ver.-", L"Brecha fantasmal olvidada -CS Ver.-", L"잊혀진 환몽의 틈새 -閃Ver.-", L"被遺忘的幻夢狹間 -閃Ver.-", L"الفجوة الخيالية المنسية -CS Ver.-", L"Забытый призрачный разрыв -CS Ver.-", L"Vergessener phantasmagorischer Spalt -CS Ver.-", L"Fenda fantasmal esquecida -CS Ver.-", L"Vergeten fantoomkloof -CS Ver.-", L"Zapomniana fantastyczna szczelina -CS Ver.-", L"Unutulmuş Hayali Boşluk -CS Ver.-");
								break;
							case 8354:
								a = LL14(L"幽世の気配", L"Atmosphere of the Netherworld", L"Atmosphère de l'au-delà", L"Atmosfera dell'oltretomba", L"Atmósfera del inframundo", L"저승의 기운", L"幽世之氣息", L"أجواء العالم السفلي", L"Атмосфера преисподней", L"Atmosphäre der Unterwelt", L"Atmosfera do submundo", L"Sfeer van de onderwereld", L"Atmosfera zaświatów", L"Öbür Dünyanın Havası");
								break;
							case 8355:
								a = "solid as the Rock of JUNO";
								break;
							case 8356:
								a = LL14(L"地下に巣喰う", L"Nesting Underground", L"Nicher sous terre", L"Nidificare sottoterra", L"Anidando bajo tierra", L"지하에 둥지를 틀다", L"盤據地下", L"التعشيش تحت الأرض", L"Гнездование под землей", L"Unterirdisches Nisten", L"Aninhando-se no subsolo", L"Ondergronds nestelen", L"Gnieżdżenie się pod ziemią", L"Yeraltındaki Yuva");
								break;
							case 8359:
								a = "Spiral of Erebos";
								break;
							case 8360:
								a = LL14(L"鋼の障壁", L"Steel Barrier", L"Barrière d'acier", L"Barriera d'acciaio", L"Barrera de acero", L"강철의 장벽", L"鋼鐵障壁", L"حاجز فولاذي", L"Стальной барьер", L"Stahlbarriere", L"Barreira de aço", L"Stalen barrière", L"Stalowa bariera", L"Çelik Bariyer");
								break;
							case 8363:
								a = "Break In";
								break;
							case 8365:
								a = LL14(L"サングラール迷宮", L"Sanglar Maze", L"Labyrinthe de Sanglar", L"Labirinto di Sanglar", L"Laberinto de Sanglar", L"Sanglar 미궁", L"Sanglar 迷宮", L"متاهة Sanglar", L"Лабиринт Санглар", L"Sanglar-Labyrinth", L"Labirinto de Sanglar", L"Sanglar doolhof", L"Labirynt Sanglar", L"Sanglar Labirenti");
								break;
							case 8366:
								a = LL14(L"静けき森の魔女", L"Witch of the Silent Forest", L"Sorcière de la forêt silencieuse", L"Strega della foresta silenziosa", L"Bruja del bosque silencioso", L"고요한 숲의 마녀", L"靜謐森林的魔女", L"ساحرة الغابة الصامتة", L"Ведьма тихого леса", L"Hexe des stillen Waldes", L"Bruxa da floresta silenciosa", L"Heks van het stille woud", L"Wiedźma z cichego lasu", L"Sessiz Ormanın Cadısı");
								break;
							case 8367:
								a = LL14(L"Mystic Core -閃Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -閃Ver.-", L"Mystic Core -閃Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-");
								break;
							case 8368:
								a = LL14(L"斉いし舞台", L"Unified Stage", L"Scène unifiée", L"Palcoscenico unificato", L"Escenario unificado", L"가지런한 무대", L"齊整的舞台", L"المسرح الموحد", L"Единая сцена", L"Vereinte Bühne", L"Palco unificado", L"Verenigd podium", L"Zunifikowana scena", L"Birleşmiş Sahne");
								break;
							case 8369:
								a = LL14(L"シンクロニシティ #23", L"Synchronicity #23", L"Synchronicité #23", L"Sincronicità #23", L"Sincronicidad #23", L"싱크로니시티 #23", L"共時性 #23", L"التزامن #23", L"Синхронность #23", L"Synchronizität #23", L"Sincronicidade #23", L"Synchroniciteit #23", L"Synchroniczność #23", L"Eşzamanlılık #23");
								break;
							case 8371:
								a = LL14(L"世界の命運を賭けて", L"Betting on the World's Fate", L"Parier sur le destin du monde", L"Scommettendo sul destino del mondo", L"Apostando por el destino del mundo", L"세상의 운명을 걸고", L"賭上世界的命運", L"الرهان على مصير العالم", L"Ставя на кон судьбу мира", L"Auf das Schicksal der Welt setzen", L"Apostando no destino do mundo", L"Inzetten op het lot van de wereld", L"Stawiając na losy świata", L"Dünyanın Kaderi Üzerine Bahis");
								break;
							case 8372:
								a = "The End of -SAGA-";
								break;
							case 8429:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8450:
								a = "Brave Steel";
								break;
							case 8451:
								a = "Toughness!!";
								break;
							case 8452:
								a = LL14(L"剣戟怒涛", L"Sword and Lance Storm", L"Tempête d'épées et de lances", L"Tempesta di spade e lance", L"Tormenta de espadas y lanzas", L"검격노도", L"劍戟怒濤", L"عاصفة السيف والرمح", L"Шторм мечей и копий", L"Schwert- und Lanzensturm", L"Tempestade de espadas e lanças", L"Zwaard- en lansstorm", L"Burza mieczy i włóczni", L"Kılıç ve Mızrak Fırtınası");
								break;
							case 8453:
								a = "Proud Grudge";
								break;
							case 8454:
								a = LL14(L"チープ・トラップ", L"Cheap Trap", L"Piège bon marché", L"Trappola a buon mercato", L"Trampa barata", L"치프 트랩", L"便宜的陷阱", L"فخ رخيص", L"Дешевая ловушка", L"Billige Falle", L"Armadilha barata", L"Goedkope val", L"Tania pułapka", L"Ucuz Tuzak");
								break;
							case 8455:
								a = "STEP AHEAD";
								break;
							case 8456:
								a = LL14(L"劣勢を挽回せよ！", L"Turn the Tide!", L"Inversez la tendance !", L"Inverti la rotta!", L"¡Cambia la marea!", L"열세를 만회하라!", L"挽回劣勢！", L"اقلب الموازين!", L"Переломи ход событий!", L"Das Blatt wenden!", L"Vire o jogo!", L"Keer het tij!", L"Odwróć losy!", L"Gidişatı Değiştir!");
								break;
							case 8457:
								a = "Abrupt Visitor";
								break;
							case 8458:
								a = LL14(L"行き着く先 -Opening Size-", L"Destination -Opening Size-", L"Destination -Opening Size-", L"Destinazione -Opening Size-", L"Destino -Opening Size-", L"다다르는 곳 -Opening Size-", L"抵達之處 -Opening Size-", L"الوجهة - Opening Size", L"Место назначения -Opening Size-", L"Zielort -Opening Size-", L"Destino -Opening Size-", L"Bestemming -Opening Size-", L"Miejsce docelowe -Opening Size-", L"Varış Noktası -Opening Size-");
								break;
							case 8460:
								a = "Lift-off!";
								break;
							case 8461:
								a = "Accursed Tycoon";
								break;
							case 8464:
								a = "One-Way to the Netherworld";
								break;
							case 8465:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8466:
								a = "Erosion of Madness";
								break;
							case 8467:
								a = "DOOMSDAY TRANCE";
								break;
							case 8468:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8472:
								a = "Malicious Fiend";
								break;
							case 8473:
								a = "Unlikely Combination";
								break;
							case 8474:
								a = "Robust One";
								break;
							case 8475:
								a = LL14(L"古の盟約", L"Ancient Covenant", L"Ancienne alliance", L"Antico patto", L"Antiguo pacto", L"고대의 맹약", L"古代盟約", L"العهد القديم", L"Древний завет", L"Alter Bund", L"Antigo pacto", L"Oud verbond", L"Starożytne przymierze", L"Kadim Sözleşme");
								break;
							case 8476:
								a = LL14(L"七の相克 -EXCELLION KRIEG-", L"Seven Antagonisms -EXCELLION KRIEG-", L"Sept antagonismes -EXCELLION KRIEG-", L"Sette antagonismi -EXCELLION KRIEG-", L"Siete antagonismos -EXCELLION KRIEG-", L"칠의 상극 -EXCELLION KRIEG-", L"七之相克 -EXCELLION KRIEG-", L"الخصومات السبعة - EXCELLION KRIEG", L"Семь противостояний -EXCELLION KRIEG-", L"Sieben Antagonismen -EXCELLION KRIEG-", L"Sete antagonismos -EXCELLION KRIEG-", L"Zeven tegenstellingen -EXCELLION KRIEG-", L"Siedem antagonizmów -EXCELLION KRIEG-", L"Yedi Karşıtlık -EXCELLION KRIEG-");
								break;
							case 8477:
								a = "Burning Throb";
								break;
							case 8478:
								a = "Neck or Nothing";
								break;
							case 8479:
								a = "Majestic Roar";
								break;
							case 8480:
								a = "With Our Own Hands!!";
								break;
							case 8500:
								a = LL14(L"授業は合同で", L"Joint Class", L"Cours commun", L"Classe congiunta", L"Clase conjunta", L"수업은 합동으로", L"聯合授課", L"فصل مشترك", L"Совместное занятие", L"Gemeinsamer Unterricht", L"Aula conjunta", L"Gezamenlijke les", L"Wspólna lekcja", L"Ortak Ders");
								break;
							case 8501:
								a = "Power or Technique";
								break;
							case 8502:
								a = "Briefing Time";
								break;
							case 8503:
								a = LL14(L"第II分校の日常", L"Daily Life at Branch II", L"Vie quotidienne à la Branche II", L"Vita quotidiana alla Branca II", L"Vida cotidiana en la Rama II", L"제II분교의 일상", L"第II分校的日常", L"الحياة اليومية في الفرع الثاني", L"Будни во втором филиале", L"Alltag in Zweigstelle II", L"Vida cotidiana na Filial II", L"Dagelijks leven in Afdeling II", L"Życie codzienne w Filii II", L"2. Şubede Günlük Yaşam");
								break;
							case 8504:
								a = LL14(L"充実したひととき", L"Satisfying Moment", L"Moment satisfaisant", L"Momento soddisfacente", L"Momento satisfactorio", L"충실한 한때", L"充實的時光", L"لحظة مرضية", L"Насыщенный момент", L"Erfüllter Moment", L"Momento gratificante", L"Bevredigend moment", L"Satysfakcjonująca chwila", L"Tatmin Edici Bir An");
								break;
							case 8505:
								a = LL14(L"異端の研究者", L"Heretic Researcher", L"Chercheur hérétique", L"Ricercatore eretico", L"Investigador herético", L"이단의 연구자", L"異端研究者", L"باحث هرطوقي", L"Исследователь-еретик", L"Häretischer Forscher", L"Pesquisador herético", L"Ketters onderzoeker", L"Badacz heretycki", L"Sapkın Araştırmacı");
								break;
							case 8506:
								a = LL14(L"君に伝えたいこと", L"What I Want to Tell You", L"Ce que je veux te dire", L"Ciò che voglio dirti", L"Lo que quiero decirte", L"너에게 전하고 싶은 것", L"想傳達給你的事", L"ما أريد أن أقوله لك", L"То, что я хочу тебе сказать", L"Was ich dir sagen möchte", L"O que eu quero te dizer", L"Wat ik je wil vertellen", L"To, co chcę ci powiedzieć", L"Sana Söylemek İstediğim Şey");
								break;
							case 8507: case 8508:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8509:
								a = LL14(L"張り詰めた思惑", L"Tense Speculation", L"Spéculation tendue", L"Tesa speculazione", L"Especulación tensa", L"긴박한 의도", L"緊繃的意図", L"تكهنات متوترة", L"Напряженное ожидание", L"Gespannte Spekulation", L"Especulação tensa", L"Gespannen speculatie", L"Napięte spekulacje", L"Gergin Bekleyiş");
								break;
							case 8510:
								a = LL14(L"混迷の対立", L"Chaotic Conflict", L"Conflit chaotique", L"Conflitto caotico", L"Conflicto caótico", L"혼미한 대립", L"迷惘的對立", L"صراع فوضوي", L"Хаотичный конфликт", L"Chaotischer Konflikt", L"Conflito caótico", L"Chaotisch conflict", L"Chaotyczny konflikt", L"Kaotik Çatışma");
								break;
							case 8511:
								a = LL14(L"急転直下", L"Sudden Turn", L"Tournant soudain", L"Svolta improvvisa", L"Giro repentino", L"급전직하", L"急轉直下", L"تحول مفاجئ", L"Внезапный поворот", L"Plötzliche Wendung", L"Reviravolta súbita", L"Plotselinge wending", L"Nagły zwrot", L"Ani Dönüş");
								break;
							case 8512:
								a = LL14(L"蠢く陰謀", L"Writhing Conspiracy", L"Complot rampant", L"Cospirazione strisciante", L"Conspiración reptante", L"꿈틀대는 음모", L"蠢動的陰謀", L"مؤامرة ملتوية", L"Ползучий заговор", L"Sich windende Verschwörung", L"Conspiração rastejante", L"Kronkelende samenzwering", L"Wijąc się spisek", L"Kaynayan Komplo");
								break;
							case 8513:
								a = LL14(L"託されたもの", L"Entrusted One", L"Celui à qui on a confié", L"Colui a cui è stato affidato", L"A quien se le confió", L"수탁된 것", L"被託付之物", L"المستأمن", L"Вверенный", L"Der Anvertraute", L"O confiado", L"De toevertrouwde", L"Powierzony", L"Emanet Edilen");
								break;
							case 8514:
								a = LL14(L"羅刹の薫陶", L"Rasetsu's Guidance", L"L'influence de Rasetsu", L"La guida di Rasetsu", L"La guía de Rasetsu", L"라세츠의 훈도", L"羅刹的教化", L"توجيه Rasetsu", L"Наставление Расецу", L"Rasetsus Führung", L"Orientação de Rasetsu", L"Rasetsu's begeleiding", L"Wskazówki Rasetsu", L"Rasetsu'nun Rehberliği");
								break;
							case 8515:
								a = LL14(L"ハーメル -遺されたもの-", L"Hamel -What Was Left Behind-", L"Hamel -Ce qui a été laissé-", L"Hamel -Ciò che è rimasto-", L"Hamel -Lo que quedó atrás-", L"하멜 ~남겨진 것~", L"哈梅爾 -遺留之物-", L"Hamel - ما تبقى", L"Хамель -Что осталось позади-", L"Hamel -Was zurückblieb-", L"Hamel -O que foi deixado para trás-", L"Hamel -Wat achterbleef-", L"Hamel -Co pozostało-", L"Hamel -Geride Kalanlar-");
								break;
							case 8516:
								a = LL14(L"Welcome Back! アーベントタイム(ラジオ)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (라디오)", L"Welcome Back! Evening Time (廣播)", L"Welcome Back! Evening Time (راديو)", L"Welcome Back! Evening Time (радио)", L"Welcome Back! Evening Time (Radio)", L"Welcome Back! Evening Time (rádio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (Radyo)");
								break;
							case 8517: case 8519:
								a = LL14(L"夏至祭", L"Summer Solstice Festival", L"Festival du solstice d'été", L"Festival del solstizio d'estate", L"Festival del solsticio de verano", L"하지제", L"夏至祭", L"مهرجان الانقلاب الصيفي", L"Фестиваль летнего солнцестояния", L"Sommersonnenwendfest", L"Festival do solstício de verão", L"Midzomerfestival", L"Festiwal przesilenia letniego", L"Yaz Gündönümü Festivali");
								break;
							case 8520:
								a = LL14(L"翡翠庭園", L"Jade Garden", L"Jardin de jade", L"Giardino di giada", L"Jardín de jade", L"비취 정원", L"翡翠庭園", L"حديقة اليشم", L"Нефритовый сад", L"Jade-Garten", L"Jardim de jade", L"Jade tuin", L"Jadeitowy ogród", L"Yeşim Bahçesi");
								break;
							case 8521:
								a = LL14(L"初めての円舞曲", L"First Waltz", L"Première valse", L"Primo valzer", L"Primer vals", L"첫 원무곡", L"第一首圓舞曲", L"الفالس الأول", L"Первый вальс", L"Erster Walzer", L"Primeira valsa", L"Eerste wals", L"Pierwszy walc", L"İlk Vals");
								break;
							case 8522:
								a = LL14(L"真打ち登場！", L"Headliner's Entrance!", L"Entrée de la vedette !", L"Entrata del protagonista!", L"¡Entrada del protagonista!", L"신우치 등장!", L"壓軸登場！", L"دخول النجم!", L"Выход главной звезды!", L"Auftritt des Hauptactes!", L"Entrada da atração principal!", L"Entree van de hoofdact!", L"Wejście gwiazdy wieczoru!", L"Asıl Sanatçının Girişi!");
								break;
							case 8524:
								a = "Tragedy";
								break;
							case 8528:
								a = LL14(L"僅かな希望の先に", L"Beyond Slight Hope", L"Au-delà d'un mince espoir", L"Oltre una sottile speranza", L"Más allá de una pequeña esperanza", L"희미한 희망 너머에", L"在微小的希望之後", L"ما وراء أمل ضئيل", L"За хрупкой надеждой", L"Jenseits einer leisen Hoffnung", L"Além de uma pequena esperança", L"Voorbij een sprankje hoop", L"Poza nikłą nadzieję", L"Küçük Bir Umudun Ötesinde");
								break;
							case 8530:
								a = LL14(L"帰路へ", L"On the Road Home", L"Sur le chemin du retour", L"Sulla via di casa", L"En el camino a casa", L"귀로에", L"歸途", L"في طريق العودة", L"На пути домой", L"Auf dem Heimweg", L"No caminho para casa", L"Op weg naar huis", L"W drodze do domu", L"Eve Dönüş Yolunda");
								break;
							case 8532:
								a = "Roots of Scar";
								break;
							case 8534:
								a = LL14(L"想い千里を走り", L"Feelings Run a Thousand Miles", L"Les sentiments parcourent mille lieues", L"I sentimenti corrono per mille miglia", L"Los sentimientos corren mil millas", L"그리움 천리를 달려", L"思念奔馳千里", L"المشاعر تجري ألف ميل", L"Чувства бегут за тысячи миль", L"Gefühle eilen tausend Meilen", L"Sentimentos correm mil milhas", L"Gevoelens leggen duizend mijlen af", L"Uczucia biegną tysiąc mil", L"Duygular Bin Mil Koşar");
								break;
							case 8536:
								a = LL14(L"光射す空の下で", L"Under the Shining Sky", L"Sous le ciel radieux", L"Sotto il cielo splendente", L"Bajo el cielo resplandeciente", L"빛 비치는 하늘 아래에서", L"在光芒照射的天空下", L"تحت السماء المشرقة", L"Под сияющим небом", L"Unter dem strahlenden Himmel", L"Sob o céu brilhante", L"Onder de stralende hemel", L"Pod lśniącym niebem", L"Işıldayan Gökyüzü Altında");
								break;
							case 8539:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8541:
								a = LL14(L"空を見上げて -Eliot Ver.-", L"Look Up at the Sky -Eliot Ver.-", L"Regarder le ciel -Eliot Ver.-", L"Guarda il cielo -Eliot Ver.-", L"Mira al cielo -Eliot Ver.-", L"하늘을 올려다보며 -Eliot Ver.-", L"仰望天空 -Eliot Ver.-", L"انظر إلى السماء -Eliot Ver.-", L"Посмотри на небо -Eliot Ver.-", L"Blick in den Himmel -Eliot Ver.-", L"Olhe para o céu -Eliot Ver.-", L"Kijk naar de lucht -Eliot Ver.-", L"Spójrz w niebo -Eliot Ver.-", L"Gökyüzüne Bak -Eliot Ver.-");
								break;
							case 8542: case 8543:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8544:
								a = "Little Rain";
								break;
							case 8545:
								a = LL14(L"暗雲", L"Dark Clouds", L"Nuages sombres", L"Nubi oscure", L"Nubes oscuras", L"먹구름", L"暗雲", L"سحب مظلمة", L"Темные тучи", L"Dunkle Wolken", L"Nuvens escuras", L"Donkere wolken", L"Ciemne chmury", L"Kara Bulutlar");
								break;
							case 8546:
								a = LL14(L"鐘、鳴り響く時", L"When the Bell Tolls", L"Quand la cloche sonne", L"Quando suona la campana", L"Cuando dobla la campana", L"종이 울려 퍼질 때", L"鐘聲響徹之時", L"عندما يدق الجرس", L"Когда бьет колокол", L"Wenn die Glocke läutet", L"Quando o sino toca", L"Wanneer de klok luidt", L"Kiedy bije dzwon", L"Çanlar Çaldığında");
								break;
							case 8547:
								a = LL14(L"巨イナル黄昏", L"Giant Twilight", L"Crépuscule géant", L"Crepuscolo gigante", L"Crepúsculo gigante", L"거대한 황혼", L"巨大的黄昏", L"الغسق العملاق", L"Великие сумерки", L"Riesige Dämmerung", L"Crepúsculo gigante", L"Gigantische schemering", L"Wielki zmierzch", L"Muazzam Alacakaranlık");
								break;
							case 8548:
								a = LL14(L"あの日の約束", L"That Day's Promise", L"La promesse de ce jour-là", L"La promessa di quel giorno", L"La promesa de aquel día", L"그날의 약속", L"那天的約定", L"وعد ذلك اليوم", L"Обещание того дня", L"Das Versprechen von jenem Tag", L"A promessa daquele dia", L"De belofte van die dag", L"Obietnica tamtego dnia", L"O Günkü Söz");
								break;
							case 8551:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8553:
								a = "Sensitive Talk";
								break;
							case 8554:
								a = LL14(L"哀花", L"Mournful Flower", L"Fleur de deuil", L"Fiore di lutto", L"Flor de luto", L"애화", L"哀花", L"زهرة حزينة", L"Траурный цветок", L"Trauerblume", L"Flor de luto", L"Rouwbloem", L"Żałobny kwiat", L"Yas Çiçeği");
								break;
							case 8555:
								a = "Feel at Home";
								break;
							case 8556:
								a = LL14(L"幾千万の夜を越えて", L"Beyond Countless Nights", L"Au-delà d'innombrables nuits", L"Oltre innumerevoli notti", L"Más allá de incontables noches", L"수천만 밤を 넘어서", L"跨越數千萬個夜晚", L"عبر ملايين الليالي", L"Сквозь миллионы ночей", L"Jenseits von Millionen Nächten", L"Além de milhões de noites", L"Voorbij miljoenen nachten", L"Poza miliony nocy", L"Milyonlarca Gecenin Ötesinde");
								break;
							case 8557: case 8558:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8559:
								a = LL14(L"優しき微睡み", L"Gentle Slumber", L"Sommeil paisible", L"Dolce sonno", L"Dulce sueño", L"포근한 잠", L"溫柔的微睡", L"سبات لطيف", L"Нежная дремота", L"Sanfter Schlummer", L"Sono suave", L"Zachte sluimer", L"Łagodny sen", L"Nazik Uyku");
								break;
							case 8560:
								a = LL14(L"最悪の最善手", L"Best Move in the Worst Situation", L"Meilleur coup dans la pire situation", L"Mossa migliore nella peggiore situazione", L"Mejor jugada en la peor situación", L"최악 중의 최선", L"最壞情況中的最佳對策", L"أفضل خطوة في أسوأ وضع", L"Лучший ход в худшей ситуации", L"Bester Zug in der schlimmsten Lage", L"Melhor jogada na pior situação", L"Beste zet in de slechtste situatie", L"Najlepszy ruch w najgorszej sytuacji", L"En Kötü Durumdaki En İyi Hamle");
								break;
							case 8562:
								a = LL14(L"黒の真実", L"Black Truth", L"Vérité noire", L"Verità nera", L"Verdad negra", L"검은 진실", L"黑之真實", L"حقيقة سوداء", L"Черная правда", L"Schwarze Wahrheit", L"Verdade negra", L"Zwarte waarheid", L"Czarna prawda", L"Siyah Gerçek");
								break;
							case 8563:
								a = LL14(L"いつでもそばに", L"Always by Your Side", L"Toujours à tes côtés", L"Sempre al tuo fianco", L"Siempre a tu lado", L"언제나 곁에", L"永遠在身邊", L"دائماً بجانبك", L"Всегда рядом", L"Immer an deiner Seite", L"Sempre ao seu lado", L"Altijd aan je zijde", L"Zawsze przy tobie", L"Daima Yanında");
								break;
							case 8564:
								a = LL14(L"その温もりは小さいけれど。", L"That warmth is small, but.", L"Cette chaleur est petite, mais.", L"Quel calore è piccolo, ma.", L"Ese calor es pequeño, pero.", L"그 온기는 작지만.", L"那份溫暖雖小。", L"ذلك الدفء صغير، لكن.", L"Это тепло мало, но.", L"Diese Wärme ist klein, aber.", L"Aquele calor é pequeno, mas.", L"Die warmte is klein, maar.", L"To ciepło jest małe, ale.", L"Bu sıcaklık küçük, ama.");
								break;
							case 8566:
								a = LL14(L"それでも前へ", L"Still Forward", L"Tout de même vers l'avant", L"Ancora avanti", L"Aun así, adelante", L"그래도 앞으로", L"即便如此依然向前", L"ومع ذلك، إلى الأمام", L"Все равно вперед", L"Trotzdem vorwärts", L"Ainda assim, em frente", L"Toch vooruit", L"Mimo to do przodu", L"Yine de İleri");
								break;
							case 8570:
								a = LL14(L"想いひとつに", L"Hearts as One", L"Cœurs unis", L"Cuori come uno", L"Corazones como uno", L"마음 하나로", L"心意合一", L"قلوب متحدة", L"Сердца как одно", L"Herzen eins", L"Corações como um", L"Harten als één", L"Serca jako jedno", L"Kalpler Bir");
								break;
							case 8571:
								a = LL14(L"千年要塞", L"Millennium Fortress", L"Forteresse millénaire", L"Fortezza millenaria", L"Fortaleza milenaria", L"천년 요새", L"千年要塞", L"حصن الألفية", L"Тысячелетняя крепость", L"Jahrtausendfestung", L"Fortaleza milenar", L"Millenniumvesting", L"Tysiącletnia twierdza", L"Bin Yıllık Kale");
								break;
							case 8572:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8573:
								a = LL14(L"せめてこの夜に誓って", L"At Least Swear Tonight", L"Au moins, jure ce soir", L"Almeno giura stasera", L"Al menos jura esta noche", L"적어도 이 밤에 맹세하며", L"至少在今夜發誓", L"على الأقل أقسم الليلة", L"По крайней мере, поклянись сегодня", L"Schwöre zumindest heute Nacht", L"Pelo menos jure esta noite", L"Zweer tenminste vanavond", L"Przynajmniej przysięgnij dziś", L"En Azından Bu Gece Yemin Et");
								break;
							case 8574:
								a = "Constraint";
								break;
							case 8575:
								a = LL14(L"過ぎ去りし日々", L"Days Gone By", L"Jours passés", L"Giorni passati", L"Días pasados", L"지나간 나날", L"逝去的日子", L"أيام مضت", L"Минувшие дни", L"Vergangene Tage", L"Dias passados", L"Voorbijgegane dagen", L"Minione dni", L"Geçip Giden Günler");
								break;
							case 8576:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8577:
								a = LL14(L"それぞれの覚悟", L"Each One's Resolve", L"La résolution de chacun", L"La risoluzione di ognuno", L"La resolución de cada uno", L"각자의 각오", L"各自的覺悟", L"عزيمة كل واحد", L"Решимость каждого", L"Die Entschlossenheit jedes Einzelnen", L"A determinação de cada um", L"Ieders eigen vastberadenheid", L"Determinacja każdego z nas", L"Her Birimizin Kararlılığı");
								break;
							case 8578:
								a = LL14(L"無明の闇の中で", L"In the Darkness", L"Dans les ténèbres sans fin", L"Nell'oscurità eterna", L"En la oscuridad eterna", L"무명의 어둠 속에서", L"在無明之暗中", L"في الظلام الدامس", L"В вечной тьме", L"In ewiger Finsternis", L"Na escuridão eterna", L"In de eeuwige duisternis", L"W wiecznej ciemności", L"Sonsuz Karanlıkta");
								break;
							case 8579:
								a = LL14(L"変わる世界 -闇の底から-", L"Changing World -From the Depths of Darkness-", L"Monde changeant -Du fond des ténèbres-", L"Mondo che cambia -Dal profondo delle tenebre-", L"Mundo cambiante -Desde el fondo de la oscuridad-", L"변하는 세계 ~어둠의 바닥에서~", L"變化的世界 -從黑暗深處-", L"عالم متغير - من أعماق الظلام", L"Меняющийся мир -Из глубин тьмы-", L"Sich wandelnde Welt -Aus den Tiefen der Finsternis-", L"Mundo em mudança -Do fundo da escuridão-", L"Veranderende wereld -Uit de diepten van de duisternis-", L"Zmieniający się świat -Z głębi ciemności-", L"Değişen Dünya -Karanlığın Derinliklerinden-");
								break;
							case 8600:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8601:
								a = LL14(L"ゲートイン", L"Gate In", L"Entrée en piste", L"Ingresso ai cancelli", L"Entrada a gateras", L"게이트 인", L"進入閘門", L"دخول البوابة", L"Вход в ворота", L"Einzug", L"Entrada no portão", L"Binnenkomst", L"Wjazd na bramkę", L"Giriş");
								break;
							case 8602:
								a = LL14(L"不明(空の軌跡)", L"Unknown(Sky)", L"Inconnu(Sky)", L"Sconosciuto(Sky)", L"Desconocido(Sky)", L"불명(하늘)", L"不明(空之軌跡)", L"غير معروف(Sky)", L"Неизвестно(Sky)", L"Unbekannt(Sky)", L"Desconhecido(Sky)", L"Onbekend(Sky)", L"Nieznany(Sky)", L"Bilinmeyen(Sky)");
								break;
							case 8603:
								a = LL14(L"女神はいつも見ています", L"The Goddess is Always Watching", L"La déesse regarde toujours", L"La dea guarda sempre", L"La diosa siempre observa", L"여신은 언제나 보고 있습니다", L"女神一直在注視著", L"الآلهة تراقب دائماً", L"Богиня всегда наблюдает", L"Die Göttin wacht immer", L"A deusa está sempre olhando", L"De godin kijkt altijd toe", L"Bogini zawsze patrzy", L"Tanrıça Daima İzliyor");
								break;
							case 8604:
								a = LL14(L"不明(空の軌跡)", L"Unknown(Sky)", L"Inconnu(Sky)", L"Sconosciuto(Sky)", L"Desconocido(Sky)", L"불명(하늘)", L"不明(空之軌跡)", L"غير معروف(Sky)", L"Неизвестно(Sky)", L"Unbekannt(Sky)", L"Desconhecido(Sky)", L"Onbekend(Sky)", L"Nieznany(Sky)", L"Bilinmeyen(Sky)");
								break;
							case 8605: case 8606: case 8608: case 8610: case 8611: case 8612:
							case 8613: case 8614: case 8616: case 8617: case 8618: case 8619:
							case 8620: case 8621:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8702:
								a = "Master's Vertex";
								break;
							case 8706:
								a = "Endure Grief";
								break;
							case 8707:
								a = "Intuition and Insight";
								break;
							case 8708:
								a = "Bold Assailants";
								break;
							case 8709:
								a = "Seductive Shudder";
								break;
							case 8711:
								a = "Blue Stardust";
								break;
							case 8713:
								a = "Pleasure Smile";
								break;
							case 8714:
								a = LL14(L"巨竜目覚める", L"The Great Dragon Awakens", L"Le grand dragon s'éveille", L"Il grande drago si risveglia", L"El gran dragón despierta", L"거룡 깨어나다", L"巨龍覺醒", L"التنين العظيم يستيقظ", L"Великий дракон пробуждается", L"Der große Drache erwacht", L"O grande dragão desperta", L"De grote draak ontwaakt", L"Wielki smok się budzi", L"Büyük Ejderha Uyanıyor");
								break;
							case 8715:
								a = LL14(L"未来へ。", L"To the Future.", L"Vers le futur.", L"Verso il futuro.", L"Hacia el futuro.", L"미래로.", L"往未來。", L"إلى المستقبل.", L"В будущее.", L"In die Zukunft.", L"Para o futuro.", L"Naar de toekomst.", L"W przyszłość.", L"Geleceğe.");
								break;
							case 8716:
								a = LL14(L"明日への軌跡 -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"내일로의 궤적 -Instrumental Ver.-", L"通向明天的軌跡 -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-");
								break;
							case 8717:
								a = "Deep Carnival";
								break;
							case 8718:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8719:
								a = "Chain Chain Chain!";
								break;
							case 8720:
								a = LL14(L"明日への軌跡", L"Trails to Tomorrow", L"Sillage vers demain", L"Tracce verso il domani", L"Estela hacia el mañana", L"내일로의 궤적", L"通向明天的軌跡", L"مسارات نحو الغد", L"Пути в завтрашний день", L"Pfade nach morgen", L"Rastros para o amanhã", L"Sporen naar morgen", L"Ścieżki do jutra", L"Yarına Giden İzler");
								break;
							case 8721:
								a = LL14(L"愛の詩(歌)", L"Poem of Love (vocal)", L"Poème d'amour (vocal)", L"Poema d'amore (vocal)", L"Poema de amor (vocal)", L"사랑의 시(노래)", L"愛之詩(歌)", L"قصيدة حب (صوتية)", L"Поэма о любви (вокал)", L"Liebesgedicht (Gesang)", L"Poema de amor (vocal)", L"Liefdesgedicht (vocaal)", L"Poemat miłości (wokal)", L"Aşk Şiiri (vokal)");
								break;
							case 8722:
								a = "Celestial Coalescence";
								break;
							case 8800:
								a = "Vantage Masters";
								break;
							case 8801:
								a = "Concept H.M.I.";
								break;
							case 8802:
								a = LL14(L"風よりも駿く", L"Swifter Than the Wind", L"Plus rapide que le vent", L"Più veloce del vento", L"Más rápido que el viento", L"바람보다 빠르게", L"比風更迅捷", L"أسرع من الرياح", L"Быстрее ветра", L"Schneller als der Wind", L"Mais rápido que o vento", L"Sneller dan de wind", L"Szybszy niż wiatr", L"Rüzgardan Daha Hızlı");
								break;
							case 8803:
								a = "Brilliant Escape";
								break;
							case 8810: case 8811: case 8812: case 8910: case 8911: case 8912:
							case 8913: case 8916: case 8917: case 8918: case 8919: case 8920:
							case 8921:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"불명", L"不明", L"غير معروف", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							}
						}

					//a = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
					if (a.Left(3) == L"ed7") {
						int b = _ttoi(a.Mid(3, 3));
						CString fil = fname.Left(fname.ReverseFind(L'\\')) + L"\\..\\..\\data\\bgm\\info.yaml";
						FILE* fp;
						errno_t ferr;
						ferr = _tfopen_s(&fp, fil, _T("r, ccs=UTF-8"));
						if (ferr == 0) {
							CStdioFile fzero(fp);
							fzero.SeekToBegin();
							CString stf, stl, stn;
							BOOL ck = FALSE;
							for (;;) {
								if (fzero.ReadString(stf) == FALSE) break;
								stl.Format(L"'%d'", b);
								if (stf.Find(stl) != -1) {
									ck = TRUE;
								}
								if (stf.Find(L"jp:") != -1 && ck == TRUE) {
									int k = stf.Find(L"jp:") + 4;
									stn = stf.Mid(k);
									break;
								}
							}
							if (stn != L"") {
								a = stn;
							}
							fzero.Close();
							fclose(fp);
						}
					}
					_tcscpy(p.name, a);
					_tcscpy(p.fol, fname1);
				}
				else if (ft.Right(4) == ".mp3" || ft.Right(4) == ".MP3" || ft.Right(4) == ".mp2" || ft.Right(4) == ".MP2" ||
					ft.Right(4) == ".mp1" || ft.Right(4) == ".MP1" || ft.Right(4) == ".rmp" || ft.Right(4) == ".RMP") {
					p.sub = -10; p.loop1 = p.loop2 = 0;
					ft = ft2;
					_tcscpy(p.fol, fname1);
					CId3tagv1 ta1p;
					CId3tagv2 ta2p;
					int b = ta2p.Load(fname);
					ss = ta2p.GetArtist(); if (b == -1) { ta1p.Load(fname); ss = ta1p.GetArtist(); } _tcscpy(p.art, ss);
					ss = ta2p.GetTitle(); if (b == -1) ss = ta1p.GetTitle(); if (ss == "")ss = ft; _tcscpy(p.name, ss);
					ss = ta2p.GetAlbum(); if (b == -1) ss = ta1p.GetAlbum(); _tcscpy(p.alb, ss);
				}
				else if ((bufimage[0] == 0xff && (bufimage[1] & 0xf0 == 0xf0)) && (ft.Right(4) == ".aac" || ft.Right(4) == ".AAC")) {
					p.sub = -9;
					ft = ft2;
					_tcscpy(p.name, ft2);
					_tcscpy(p.fol, fname1);
				}
				else if ((ft.Right(4) == ".dsf" || ft.Right(4) == ".DSF" || ft.Right(4) == ".dff" || ft.Right(4) == ".DFF" || ft.Right(4) == ".wsd" || ft.Right(4) == ".WSD")) {
					CString tagfile, tagname, tagalbum;
					ULONGLONG po;
					ft = ft2;
					og->dsdload(fname,tagfile, tagname, tagalbum,po, 1);
					og->dsdclose();
					_tcscpy(p.name, tagfile);
					_tcscpy(p.alb, tagalbum);
					_tcscpy(p.art, tagname);
					_tcscpy(p.fol, fname1);
					p.sub = -7; p.loop1 = p.loop2 = 0;
				}
				else if ((ft.Right(4) == ".m4a" || ft.Right(4) == ".M4A" || ft.Right(4) == ".aac" || ft.Right(4) == ".AAC")) {
					ft = ft2;
					CFile ff;
					char buf[1024];
					TCHAR kpi[512];
					ff.Open(fname, CFile::modeRead | CFile::shareDenyNone, NULL);
					int flg, read = ff.Read(bufimage, sizeof(bufimage));
					ff.Close();
					kpi[0] = 0;
					plugs(s, &p, kpi, kvver);
					if (kpi[0] == 0)
						p.sub = -3;
					else
						p.sub = -2;
					if (savedata.m4a == 1)
						p.sub = -9;
					_tcscpy(p.name, ft);
					_tcscpy(p.fol, fname1);
					flg = 0;
					int i;
					for (i = 0; i < read - 4; i++) {
						if (bufimage[i] == 'u' && bufimage[i + 1] == 'd' && bufimage[i + 2] == 't' && bufimage[i + 3] == 'a') {
							int j;
							for (j = i + 4; j < read - 4; j++) {
								if (bufimage[j] == 'a' && bufimage[j + 1] == 'l' && bufimage[j + 2] == 'b' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
									j += 19;
									for (int k = j; k < read - 4; k++) {
										if (bufimage[k] == 0) {
											flg = 1;
											buf[k - j] = 0;
											buf[k - j + 1] = 0;
											buf[k - j + 2] = 0;
											break;
										}
										buf[k - j] = bufimage[k];
									}
								}
								if (flg == 1) {
									const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
									TCHAR* buff = new TCHAR[wlen + 1];
									if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
									{
										buff[wlen] = _T('\0');
									}
									wcscpy(p.alb, buff);
									delete[] buff;
									flg = 0;
									break;
								}
							}
							for (j = i + 4; j < read - 4; j++) {
								if (bufimage[j] == 'A' && bufimage[j + 1] == 'R' && bufimage[j + 2] == 'T' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
									j += 19;
									for (int k = j; k < read - 4; k++) {
										if (bufimage[k] == 0) {
											flg = 1;
											buf[k - j] = 0;
											buf[k - j + 1] = 0;
											buf[k - j + 2] = 0;
											break;
										}
										buf[k - j] = bufimage[k];
									}
								}
								if (flg == 1) {
									const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
									TCHAR* buff = new TCHAR[wlen + 1];
									if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
									{
										buff[wlen] = _T('\0');
									}
									wcscpy(p.art, buff);
									delete[] buff;
									flg = 0;
									break;
								}
							}
							for (j = i + 4; j < read - 4; j++) {
								if (bufimage[j] == 'n' && bufimage[j + 1] == 'a' && bufimage[j + 2] == 'm' && bufimage[j + 7] == 'd' && bufimage[j + 8] == 'a' && bufimage[j + 9] == 't' && bufimage[j + 10] == 'a') {
									j += 19;
									for (int k = j; k < read - 4; k++) {
										if (bufimage[k] == 0) {
											flg = 1;
											buf[k - j] = 0;
											buf[k - j + 1] = 0;
											buf[k - j + 2] = 0;
											break;
									}
										buf[k - j] = bufimage[k];
									}
								}
								if (flg == 1) {
									const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0);
									TCHAR* buff = new TCHAR[wlen + 1];
									if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), buff, wlen))
									{
										buff[wlen] = _T('\0');
								}
									wcscpy(p.name, buff);
									delete[] buff;
									flg = 0;
									break;
							}
						}
					}
				}
						}
				else if ((ft.Right(5).MakeLower() == ".flac" || ft.Right(5) == ".FLAC" || ft.Right(7).MakeLower() == L".qull3h")) {
					ft = ft2;
					CFile ff;
					char buf[2024];
					TCHAR kpi[512];
					ff.Open(fname, CFile::modeRead | CFile::shareDenyNone, NULL);
					int flg, read = ff.Read(bufimage, sizeof(bufimage));
					ff.Close();
						if (bufimage[0] == 0xBF) {
							BYTE offenc[7] = { 0xd9,0x3F,0x86,0x7B,0xC7,0x61,0xaa };
							int off = 0;
							for (int ll = 0; ll < sizeof(bufimage); ll++) {
								bufimage[ll] ^= offenc[off];
								off++; off %= 7;
							}
						}
					kpi[0] = 0;
					plugs(s, &p, kpi, kvver);
					if (kpi[0] == 0)
						p.sub = -3;
					else
						p.sub = -2;
					//			if (savedata.m4a == 1)
					p.sub = -8;
					_tcscpy(p.name, ft);
					_tcscpy(p.fol, fname1);
					flg = 0;
					int i = 0, j;
					for (j = i; j < read - 6; j++) {
						if (bufimage[j] == 'A' && bufimage[j + 1] == 'L' && bufimage[j + 2] == 'B' && bufimage[j + 3] == 'U' && bufimage[j + 4] == 'M' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = 0;
							}
							wcscpy(p.alb, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 6; j++) {
						if ((bufimage[j] == 'A' || bufimage[j] == 'a') && bufimage[j + 1] == 'l' && bufimage[j + 2] == 'b' && bufimage[j + 3] == 'u' && bufimage[j + 4] == 'm' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = 0;
							}
							wcscpy(p.alb, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 6; j++) {
						if (bufimage[j] == 'A' && bufimage[j + 1] == 'R' && bufimage[j + 2] == 'T' && bufimage[j + 3] == 'I' && bufimage[j + 4] == 'S' && bufimage[j + 5] == 'T' && bufimage[j + 6] == '=') {
							j += 7;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.art, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 6; j++) {
						if ((bufimage[j] == 'A' || bufimage[j] == 'a') && bufimage[j + 1] == 'r' && bufimage[j + 2] == 't' && bufimage[j + 3] == 'i' && bufimage[j + 4] == 's' && bufimage[j + 5] == 't' && bufimage[j + 6] == '=') {
							j += 7;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
							}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.art, buff);
							delete[] buff;
							flg = 0;
							break;
							}
						}
					for (j = i; j < read - 4; j++) {
						if (bufimage[j] == 'T' && bufimage[j + 1] == 'I' && bufimage[j + 2] == 'T' && bufimage[j + 3] == 'L' && bufimage[j + 4] == 'E' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.name, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
					for (j = i; j < read - 4; j++) {
						if ((bufimage[j] == 'T' || bufimage[j] == 't') && bufimage[j + 1] == 'i' && bufimage[j + 2] == 't' && bufimage[j + 3] == 'l' && bufimage[j + 4] == 'e' && bufimage[j + 5] == '=') {
							j += 6;
							for (int k = j; k < read - 4; k++) {
								if (bufimage[k] == 0) {
									flg = 1;
									buf[k - j] = 0;
									buf[k - j + 1] = 0;
									buf[k - j + 2] = 0;
									break;
								}
								buf[k - j] = bufimage[k];
							}
						}
						if (flg == 1) {
							const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf), NULL, 0) - 1;
							TCHAR* buff = new TCHAR[wlen + 1];
							if (::MultiByteToWideChar(CP_UTF8, 0, buf, strlen(buf) - 1, buff, wlen))
							{
								buff[wlen] = _T('\0');
							}
							wcscpy(p.name, buff);
							delete[] buff;
							flg = 0;
							break;
						}
					}
				}
				else {
					p.sub = -2;
					_tcscpy(p.name, s);
					_tcscpy(p.fol, fname1);
					p.alb[0] = p.art[0] = NULL; p.loop1 = p.loop2 = 0;
					TCHAR kpi[512]; kpi[0] = 0;
					plugs(fname, &p, kpi, kvver);
					if (kpi[0]) {
						ft = fname.Left(fname.ReverseFind('.')); ft += ".m3u";
						char ftt[1024];
						WideCharToMultiByte(CP_ACP, 0, ft, -1, ftt, 2000, " ", FALSE);
						ss = fname.Right(4); ss.MakeLower();
						if (ss == ".kss") {
							FILE *f; if (f = fopen(ftt, "r")) {
								char buf[256];  int st, ed, tmp;
								for (;;) {
									if (fgets(buf, sizeof(buf), f) == NULL) break;
									//							if (f.Read(buf1, 250) == FALSE) break;
									if (buf[0] == '#' || buf[0] == '\r' || buf[0] == '\n') continue;
									ss = buf;
									st = ss.Find(',', 0); ed = ss.Find(',', st + 1); s = ss.Mid(st + 1, (ed - 1) - st);
									if (s.Left(1) == _T("$")) {
										int num = 0;
										CString s3 = s.Mid(1, 1);
										if (_T("0") <= s3 && _T("9") >= s3) num = s3.GetAt(0) - _T('0');
										if (_T("a") <= s3 && _T("f") >= s3) num = s3.GetAt(0) - _T('a') + 10;
										if (_T("A") <= s3 && _T("F") >= s3) num = s3.GetAt(0) - _T('A') + 10;
										s3 = s.Mid(2, 1); num *= 16;
										if (_T("0") <= s3 && _T("9") >= s3) num += s3.GetAt(0) - _T('0');
										if (_T("a") <= s3 && _T("f") >= s3) num += s3.GetAt(0) - _T('a') + 10;
										if (_T("A") <= s3 && _T("F") >= s3) num += s3.GetAt(0) - _T('A') + 10;
										ft.Format(_T("%s::%04d"), fname, num + 1);
									}
									else
										ft.Format(_T("%s::%04d"), fname, _tstoi(s) + 1);
									_tcscpy(p.fol, ft);
									//TCHAR ss1[2001];
									//MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
									//ss = ss1;
									st = ss.Find(L',', ed); ed = ss.Find(L',', st + 1); s = ss.Mid(st + 1, (ed - 1) - st);
									_tcscpy(p.name, s);
									if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
									Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								}
								fclose(f);
								return;
							}
						}
						ft = fname.Left(fname.ReverseFind('.')); ft += ".frm";
						if (ss == ".nsf") {
							CStdioFile f; if (f.Open(ft, CFile::modeRead | CFile::typeText, NULL)) {
								TCHAR buf[256]; int st, ed, tmp;
								f.ReadString(buf, 256);
								f.ReadString(buf, 256);
								_tcscpy(p.alb, buf);
								f.ReadString(buf, 256);
								s = buf; int j = s.Find(_T("songs")); if (j >= 0) {
									int k = s.Find(_T("S.E."));
									int l = s.ReverseFind('('); ss = s.Mid(l + 1, 3); j = _tstoi(ss);
									if (k >= 0) { l = s.ReverseFind('&'); ss = s.Mid(l + 1, 3); j += _tstoi(ss); }
									for (l = 0; l < j; l++) {
										s = fname.Right(fname.GetLength() - fname.ReverseFind('\\') - 1);
										ss.Format(_T("%s::%04d"), s, l + 1);
										_tcscpy(p.name, ss);
										ss.Format(_T("%s::%04d"), fname, l + 1);
										_tcscpy(p.fol, ss);
										if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
										Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
									}
								}
								f.Close();
								return;
							}
						}
						if (ss == ".gbs") {
							CFile f; if (f.Open(fname, CFile::modeRead, NULL)) {
								char buf[32];
								f.Read(buf, 16); int i = buf[4];
								f.Read(buf, 32);
#if UNICODE
								TCHAR ss1[512];
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
								_tcscpy(p.name, ss1);
#else
								_tcscpy(p.name, buf);
#endif
								f.Read(buf, 32);
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
								_tcscpy(p.alb, ss1);
#else
								_tcscpy(p.alb, buf);
#endif
								f.Read(buf, 32);
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf, -1, ss1, 2000);
								_tcscpy(p.art, ss1);
#else
								_tcscpy(p.art, buf);
#endif
								f.Close();
								for (int j = 0; j < i; j++) {
									ss.Format(_T("%s::%04d"), fname, j + 1); _tcscpy(p.fol, ss);
									if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
									Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								}
								return;
							}
						}
						if (ss == ".hes") {
							ft = fname1.Right(fname1.GetLength() - fname1.ReverseFind(L'\\') - 1);
							_tcscpy(p.name, ft);
							_tcscpy(p.fol, fname1);
							_tchdir(fname);
							CString ftt0 = ft;
							p.alb[0] = p.art[0] = NULL; p.loop1 = p.loop2 = 0;
							TCHAR kpi[512]; kpi[0] = 0;
							plugs(fname, &p, kpi,kvver);
							if (kpi[0]) {
								ft = fname.Left(fname.ReverseFind('.')); ft += ".m3u";
								char ftt[1024];
								WideCharToMultiByte(CP_ACP, 0, ft, -1, ftt, 2000, " ", FALSE);
								ft = fname1.Right(fname1.GetLength() - fname1.ReverseFind(L'\\') - 1);
								ss = fname.Right(4); ss.MakeLower();
								if (ss == L".hes") {
									FILE *f; if (f = fopen(ftt, "r")) {
										char buf[256];  int st, ed;
										for (;;) {
											if (fgets(buf, sizeof(buf), f) == NULL) break;
											if (buf[0] == _T('#') || buf[0] == _T('\r') || buf[0] == _T('\n')) continue;
											ss = buf;
											int z = 0;
											ss.Replace(L"\n", L"");
											ss.Replace(L"\r", L"");

											if ((z = ss.Find(',', 0)) != -1) {
												s = ss.Mid(z + 1);
												if (s.Left(1) == _T("$")) {
													int num = 0;
													CString s3 = s.Mid(1, 1);
													if (_T("0") <= s3 && _T("9") >= s3) num = s3.GetAt(0) - _T('0');
													if (_T("a") <= s3 && _T("f") >= s3) num = s3.GetAt(0) - _T('a') + 10;
													if (_T("A") <= s3 && _T("F") >= s3) num = s3.GetAt(0) - _T('A') + 10;
													s3 = s.Mid(2, 1); num *= 16;
													if (_T("0") <= s3 && _T("9") >= s3) num += s3.GetAt(0) - _T('0');
													if (_T("a") <= s3 && _T("f") >= s3) num += s3.GetAt(0) - _T('a') + 10;
													if (_T("A") <= s3 && _T("F") >= s3) num += s3.GetAt(0) - _T('A') + 10;
													ftt0.Format(_T("%s::%04d"), fname, num + 1);
												}
												else
													ftt0.Format(_T("%s::%04d"), fname, _tstoi(s) + 1);
												_tcscpy(p.fol, ftt0);
												st = ss.Find(L',', z + 1); ed = ss.Find(L',', st + 1); s = ss.Mid(st + 1, (ed - 1) - st);
												_tcscpy(p.name, s);
												p.sub = -3;
												if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
												Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
											}
											else {
												s = fname1.Left(fname1.ReverseFind(L'\\')+1);
												ftt0.Format(_T("%s%s"), s,ss);
												_tcscpy(p.fol, ftt0);
												ftt0.Format(_T("%s"), ss);
												_tcscpy(p.name, ftt0);
												p.sub = -10;
												if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
												Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
											}
										}
										fclose(f);
									}
									else {
										ft = ftt0;
										for (int i = 1; i < 255; i++) {
											ftt0.Format(_T("%s::%04d"), fname, i + 1);
											_tcscpy(p.fol, ftt0);
											ftt0.Format(_T("%s::%04d"), ft, i + 1);
											_tcscpy(p.name, ftt0);
											if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub;	fnn = p.name; }
											Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
										}
									}
									return;
								}
								return;
							}
							return;
						}
						if (ss == ".ovi" || ss == ".opi" || ss == ".ozi") {
							CFile f; char buf[512], *buf2;
							f.Open(fname, CFile::modeRead | CFile::shareDenyRead, NULL);
							if (f.GetLength() > 512)
								f.Seek(-512, CFile::end);
							else
								f.SeekToBegin();
							f.Read(buf, 512);
							int i = 0;
							f.Close();
							for (; i < 500; i++) {
								if (buf[i] == 'F'&&buf[i + 1] == 'M'&&buf[i + 2] == 'C') break;
							}
							if (i != 500) {
								buf2 = buf + i + 4; ss = buf2;
								int st = ss.Find(0x0d, 0);
								ft = ss.Left(st); _tcscpy(p.name, ft);
								int ed = ss.Find(0x0d, st + 2);
								ft = ss.Mid(st + 1, ed - st - 1); _tcscpy(p.art, ft);
								st = ss.Find(0x0d, ed + 2);
								ft = ss.Mid(ed + 1, st - ed - 1); _tcscpy(p.alb, ft);
								if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub; fnn = p.name; }
								Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
								return;
							}
						}
						ss = fname.Right(2); ss.MakeLower();
						ft = fname.Right(3); ft.MakeLower();
						if (ss == ".m" || ft == ".mz") {
							CFile ff; char buf[512], *buf2;
							ff.Open(fname, CFile::modeRead | CFile::shareDenyNone, NULL);
							if (ff.GetLength() > 512)
								ff.Seek(-512, CFile::end);
							else
								ff.SeekToBegin();
							ff.Read(buf, 512);
							int jj = ff.GetLength(); if (jj > 510) jj = 510;
							jj -= 3;
							int i;
							for (i = jj; i > 0; i--) {
								if (buf[i] == 0 && (buf[i + 1] == 0 || (BYTE)buf[i + 1] == 255) && buf[i + 2] == 0)break;
							}
							ff.Close();
							if (i != 0) {
								buf2 = buf + i + 3;
								int j = 0;
								for (;; j++)if (buf2[j] == 0)break;
#if UNICODE
								TCHAR ss1[512];
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								_tcscpy(p.name, ss1); buf2 += j;
#else
								_tcscpy(p.name, buf2); buf2 += j;
#endif
								for (j = 0;; j++)if (buf2[j] != 0)break;
								buf2 += j;
								for (j = 0;; j++)if (buf2[j] == 0)break;
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								_tcscpy(p.art, ss1); buf2 += j;
#else
								_tcscpy(p.art, buf2); buf2 += j;
#endif
								for (j = 0;; j++)if (buf2[j] != 0)break;
								buf2 += j;
								for (j = 0;; j++)if (buf2[j] == 0)break;
#if UNICODE
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								_tcscpy(p.alb, ss1); buf2 += j;
#else
								_tcscpy(p.alb, buf2); buf2 += j;
#endif
								if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub; fnn = p.name; }
								Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
							}
							return;
						}
						ft = fname.Right(4); ft.MakeLower();
						if (ft == ".tta") {
							CFile ff; char buf[512], *buf2;
							ff.Open(fname, CFile::modeRead | CFile::shareDenyNone, NULL);
							if (ff.GetLength() > 0x80)
								ff.Seek(-0x80, CFile::end);
							else
								ff.SeekToBegin();
							ff.Read(buf, 0x80);
							int i = 0;
							for (; i < 0x80; i++) {
								if (buf[i + 0] == 'T'&&buf[i + 1] == 'A'&&buf[i + 2] == 'G')break;
							}
							ff.Close();
							if (i != 0x80) {
								buf2 = buf + i + 3;
#if UNICODE
								TCHAR ss1[512];
								TCHAR buf3 = buf2[30]; buf2[30] = 0;
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								buf2[30] = buf3;
								_tcscpy(p.name, ss1); buf2 += 30;
#else
								_tcscpy(p.name, buf2); buf2 += 30;
#endif
#if UNICODE
								buf3 = buf2[30]; buf2[30] = 0;
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								buf2[30] = buf3;
								_tcscpy(p.art, ss1); buf2 += 30;
#else
								_tcscpy(p.art, buf2); buf2 += 30;
#endif
#if UNICODE
								buf3 = buf2[30]; buf2[30] = 0;
								MultiByteToWideChar(CP_ACP, 0, (LPCSTR)buf2, -1, ss1, 2000);
								buf2[30] = buf3;
								_tcscpy(p.alb, ss1); buf2 += 30;
#else
								_tcscpy(p.alb, buf2); buf2 += 30;
#endif
								if (syo == 0) { syo = 1; syos = p.fol; modesub = p.sub; fnn = s; }
								Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, p.fol, 0, 0);
							}
							return;
						}
					}

				}
				CString sL = s;
				s.MakeLower();
				if (s.Right(4) == ".png" || s.Right(4) == ".url" || s.Right(4) == ".jpg" || s.Right(4) == ".bmp" || s.Right(4) == ".cue" || s.Right(4) == ".iso" || s.Right(4) == ".bin" || s.Right(4) == ".img" || s.Right(4) == ".mds" || s.Right(4) == ".mdf" || s.Right(4) == ".ccd" || s.Right(4) == ".sub" || s.Right(4) == ".pdf" || s.Right(4) == ".com" || s.Right(4) == ".exe" || s.Right(4) == ".dll" || s.Right(4) == ".bat" || s.Right(4) == ".reg" || s.Right(4) == ".msi" || s.Right(4) == ".nfo" || s.Right(4) == ".diz" || s.Right(4) == ".gif" || s.Right(4) == ".ico" ||
					s.Right(4) == ".lrc" || s.Right(4) == ".zip" || s.Right(4) == ".lzh" || s.Right(4) == ".cab" || s.Right(4) == ".rar" || s.Right(4) == ".txt" || s.Right(4) == ".doc" || s.Right(4) == "html" || s.Right(4) == ".htm" || s.Right(4) == ".ini" || s.Right(4) == ".xml" || s.Right(4) == ".kar" || s.Right(4) == ".hed" || s.Right(4) == ".mzi" || s.Right(4) == ".mag" || s.Right(4) == ".mvi" || s.Right(4) == ".lvi" || s.Right(4) == ".mpi" || s.Right(4) == ".pvi" || s.Right(4) == ".pzi" || s.Right(4) == ".p86" || s.Right(4) == ".mml" || s.Right(4) == ".m3u" || s.Right(4) == ".frm" || s.Right(7) == ".psflib" || s.Right(8) == ".psf2lib" || s.Right(7) == ".usflib" || s.Right(7) == ".2sflib" || s.Right(3) == ".gb" || s.Right(7) == ".gsflib" || s.Right(4) == ".pdx") {
				}
				else {
					if (syo == 0) {
						syo = 1; syos = p.fol; modesub = p.sub; fnn = sL;
					}
					CString fol = p.fol;
					if (PathIsDirectory(fol)) {
						fol += L"\\" + sL;
					}
					

					Add(p.name, p.sub, p.loop1, p.loop2, p.art, p.alb, fol, 0, 0);
					fol = p.fol;
					if (PathIsDirectory(fname_full) == FALSE) {
						return;
					}
				}
			}
		}
	}
	f.Close();
	fname = fname1;
	int cdd=0;
	if(PathIsDirectory(fname)){
		CFileFind cf1;
		if (cf1.FindFile(_T("*.*")) != 0) {
			int r = 1;
			for (; r;) {
				r = cf1.FindNextFile();
				CString ss, sss;
				ss = cf1.GetFileName();
				sss = cf1.GetFilePath();
				if (!(ss == L"." || ss == L"..")) {
					if ((cf1.IsHidden() == 0)) {
						if (cf1.IsDirectory() != 0) { //フォルダ？
							Fol(cf1.GetFilePath());//*/fname+cf1.GetFileName();
						}
					}
				}
			}
		}
		cf1.Close();
	}
	else {

	}
}



void CPlayList::plugs(CString fff, playlistdata *p,TCHAR* kpi, BYTE& kv)
{
	CString ss,ft;
	int flg=0;
	for(int i=0;i<kpicnt;i++){
		for(int j=0;;j++){
			if(ext[i][j]=="") break;
			ss=fff.Right(fff.GetLength()-fff.ReverseFind('.'));ss.MakeLower();
			if(ext[i][j]==ss){
				ss=kpif[i];
				if (kpichk[i] == 1) {
					flg = 1;
					kv = kvar[i][j];
					break;
				}
			}
		}
		if(flg==1)break;
	}
	if(flg==1){
		_tcscpy(p->fol,fff);
		p->sub=-3;
		ft=fff.Right(fff.GetLength()-fff.ReverseFind('\\')-1);
		_tcscpy(p->name,ft);
		p->alb[0]=NULL;p->art[0]=NULL;p->loop1=p->loop2=p->ret2=0;
		_tcscpy(kpi,ss);
	}
}

void CPlayList::Save()
{
	TCHAR tmp[1024];int cnt,j;CString s;
	int cx,cy,x,y;RECT r;
	int c;
	_tgetcwd(tmp,1000);
	_tchdir(karento2);
	if(IsIconic()){
		ShowWindow(SW_RESTORE);
		GetWindowRect(&r);
		ShowWindow(SW_MINIMIZE);
	}else
		GetWindowRect(&r);
	x=r.left;y=r.top;cx=r.right-x;cy=r.bottom-y;
#if _UNICODE
	int lcnt = savedata.playlistnum;
	CString s0;
	if (lcnt == 0)
		s0.Format(L"playlistu.dat");
	else
		s0.Format(L"playlistu%d.dat", lcnt);
	CFile f;if(f.Open(s0,CFile::modeCreate|CFile::modeWrite,NULL)==TRUE){
#else
	CFile f;if(f.Open(_T("playlist.dat"),CFile::modeCreate|CFile::modeWrite,NULL)==TRUE){
#endif
		cnt=playcnt;
		f.Write(&cnt,4);
		f.Write(&x,4);
		f.Write(&y,4);
		f.Write(&cx,4);
		f.Write(&cy,4);
		c=m_lc.GetColumnWidth(0);f.Write(&c,4);
		c=m_lc.GetColumnWidth(1);f.Write(&c,4);
		c=m_lc.GetColumnWidth(3);f.Write(&c,4);
		c=m_lc.GetColumnWidth(4);f.Write(&c,4);
		c=m_lc.GetColumnWidth(7);f.Write(&c,4);
		playlistdata pld;
		for(int i=0;i<cnt;i++){ZeroMemory(&pld,sizeof(pld));
			_tcscpy(pld.alb,pc[i].alb);
			_tcscpy(pld.art,pc[i].art);
			_tcscpy(pld.fol,pc[i].fol);
			_tcscpy(pld.name,pc[i].name);
			pld.loop1=pc[i].loop1;
			pld.loop2=pc[i].loop2;
			pld.sub=pc[i].sub;
			pld.ret2=pc[i].ret2;
			pld.time=pc[i].time;
			f.Write(&pld,sizeof(pld));
		}
		c=m_loop.GetCheck();f.Write(&c,4);
		c=m_renzoku.GetCheck();f.Write(&c,4);
		c=m_tool.GetCheck();f.Write(&c,4);
		c=m_saisyo.GetCheck();f.Write(&c,4);
		c=m_lc.GetColumnWidth(2);f.Write(&c,4);
		c=m_lc.GetColumnWidth(5);f.Write(&c,4);
		f.Write(&pnt,4);
		f.Close();

	}
	_tchdir(tmp);
}


void CPlayList::Load()
{
	TCHAR tmp[1024];int cnt;
	int cx,cy,x=-10000,y,c;
	_tgetcwd(tmp,1000);
	_tchdir(karento2);
#if _UNICODE
	int lcnt = savedata.playlistnum;
	CString s;
	if (lcnt == 0)
		s.Format(L"playlistu.dat");
	else
		s.Format(L"playlistu%d.dat", lcnt);
	CFile f;if(f.Open(s,CFile::modeRead,NULL)==TRUE){
#else
	CFile f;if(f.Open(_T("playlist.dat"),CFile::modeRead,NULL)==TRUE){
#endif
		f.Read(&cnt,4);
		pc = (playlistdata0*)malloc(sizeof(playlistdata0) * (cnt + 1));
		f.Read(&x,4);
		f.Read(&y,4);
		f.Read(&cx,4);
		f.Read(&cy,4);
		f.Read(&c,4);m_lc.SetColumnWidth(0,c);
		f.Read(&c,4);m_lc.SetColumnWidth(1,c);
		f.Read(&c,4);m_lc.SetColumnWidth(3,c);
		f.Read(&c,4);m_lc.SetColumnWidth(4,c);
		f.Read(&c,4);
		playlistdata pld;
		m_lc.SetItemCount(cnt);
		for(int i=0;i<cnt;i++){
			f.Read(&pld,sizeof(pld));
			Add(pld.name,pld.sub,pld.loop1,pld.loop2,pld.art,pld.alb,pld.fol,pld.ret2,pld.time,FALSE,FALSE);			
		}
		c=0;f.Read(&c,4);m_loop.SetCheck(c);
		c=0;f.Read(&c,4);m_renzoku.SetCheck(c);
		c=1;f.Read(&c,4);m_tool.SetCheck(c);
		c=1;f.Read(&c,4);m_saisyo.SetCheck(c);
		c=-1;f.Read(&c,4);if(c!=-1)m_lc.SetColumnWidth(2,c);
		c=-1;f.Read(&c,4);if(c!=-1)m_lc.SetColumnWidth(5,c);
		pnt1=-1;f.Read(&pnt1,4);//if(c!=-1)SIcon(pnt);
		f.Close();
	}
	_tchdir(tmp);
	if(GetAsyncKeyState(VK_LCONTROL)&0x8000){
		x=-10000;
	}
	if(x!=-10000){
		MoveWindow(x,y,cx,cy,TRUE);
		RECT r;
		GetClientRect(&r);
	m_lc.SetWindowPos(&wndNoTopMost,0,0,(int)(r.right-20*(hD2)),(int)(r.bottom-80 * (hD2 )),SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOZORDER);
	}
}
int SC=0;
void CPlayList::SIcon(int i){
	if(i<0) return;
	if(i>=playcnt) return;
	if(i==pnt) return;
	RECT r;
	pc[i].icon=0; if(pnt>=0&&pnt<playcnt){ pc[pnt].icon=1;
			m_lc.GetItemRect(pnt,&r,LVIR_ICON);
			m_lc.RedrawWindow(&r);
	}
	pnt=i;
	m_lc.GetItemRect(pnt,&r,LVIR_ICON);
	m_lc.RedrawWindow(&r);
	m_lc.EnsureVisible(i,FALSE);
	SC=0;
}

void CPlayList::SIconTimer(int i){
	CString s; s.Format(L"%d", i);
	if(pnt<0) return;
	if(pnt>=playcnt) return;
	if(IsBadReadPtr(&pc[pnt],sizeof(playlistdata0))) return;
	//_try{
	if(i==0)
		pc[pnt].icon=2;
	else
		pc[pnt].icon=0;
	//}__except(EXCEPTION_EXECUTE_HANDLER){}
	RECT r;
	m_lc.GetItemRect(pnt,&r,LVIR_ICON);
	m_lc.RedrawWindow(&r);
}
int pln=0;
extern int ps;
extern void DoEvent();
extern int gameon;
void CPlayList::OnNMDblclkList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	*pResult = 0;
	CString s;int i,j;
	int Lindex=-1;
	Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);i=Lindex;
	if(Lindex>=playcnt) return;
	if(Lindex==-1) return;
	//SIcon(i);
	fnn=pc[Lindex].name;
	filen=pc[Lindex].fol;
	modesub=pc[Lindex].sub;
	loop1=pc[Lindex].loop1;
	loop2=pc[Lindex].loop2;
	ret2=pc[Lindex].ret2;
	plcnt=i;
	gameon = 0;
	if(pln==0){
		pln=1;
		og->OnRestart();
//		for(;ps==1;){
//			DoEvent();
//			og->OnRestart();
//		}
		pln=0;
	}
}
extern CDouga *pMainFrame1;
extern long height, width;
int ip1 = 0;
void CPlayList::OnSize(UINT nType, int cx, int cy)
{
	CCustomDialog::OnSize(nType, cx, cy);

	// TODO: ここにメッセージ ハンドラ コードを追加します。
	RECT r;
	GetClientRect(&r);
	if( ::IsWindow( this->GetSafeHwnd()) == TRUE &&  this->IsWindowVisible() == TRUE)
		m_lc.SetWindowPos(&wndNoTopMost, 0, 0, (int)(r.right - 20 * (hD2 )), (int)(r.bottom - 80 * (hD2 )), SWP_NOMOVE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	if(pl){
		if (nType == SIZE_MINIMIZED){
			if(m_saisyo.GetCheck())
				og->ShowWindow(SW_MINIMIZE);
			if(pMainFrame1){
				pMainFrame1->ShowWindow(SW_HIDE);
			}
			if (playbase)
				playbase->ShowWindow(SW_MINIMIZE);
		}
		if(nType== SIZE_RESTORED){
			if (ogpl == 1) {
				ogpl = 0;
//				return;
			}
			if(m_saisyo.GetCheck())
				og->ShowWindow(SW_RESTORE);
			if(pMainFrame1 && height!=0){
				pMainFrame1->ShowWindow(SW_SHOWNORMAL);
			}
			if (playbase)
				playbase->ShowWindow(SW_RESTORE);
	//		ip1 = 0;
//			SetTimer(4923, 100, NULL);
		}
	}
}
int kk=0;
extern int lenl;
int tlg=0;

extern int aaaa,aaaa1;
extern CPlayList*pl;
void timerpl(UINT nIDEvent,CPlayList* pl);
void timerpl1(UINT nIDEvent,CPlayList* pl);
void timerpl1(UINT nIDEvent,CPlayList* pl)
{
	if (nIDEvent == 4927) {
		pl->KillTimer(4927);
		if (ip1 > 0) {
			 return;
		}
		if (playbase)
				::SetWindowPos(playbase->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			::SetWindowPos(pl->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			pl->SetTimer(4930, 10, NULL);
			ip1 = 3;
	}
	if (nIDEvent == 4924) {
		pl->KillTimer(4924);
		if (ip1 <= 0) return;
		if (playbase)
			::SetWindowPos(playbase->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(pl->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		pl->SetTimer(4930, 10, NULL);
		ip1 = 3;
	}
	if (nIDEvent == 4930) {
		ip1--;
		if (ip1 <= 0) {
			ip1 = 0;
			aaaa = 0;
			pl->KillTimer(4930);
		}
	}
	if(nIDEvent==5000){
		pl->KillTimer(5000);
		pl->SIcon(pl->pnt1);
	}
	if(nIDEvent==40){
		pl->KillTimer(40);
		plw=1;
	}

	if(nIDEvent==3000){
		pl->SIconTimer(SC);
		SC++; SC = SC % 2;
	}
	if(nIDEvent==20){
		
		if(pl->w_flg==FALSE) return;
		if(pl->GetFocus()==NULL){return;}
		if(pl->m_find.GetFocus()->m_hWnd==pl->m_find.m_hWnd){return;}
		{
			HWND rtn;
			TCHAR Name[1024];
			long Leng = sizeof(Name);
			rtn = GetActiveWindow();
			GetWindowTextW(rtn, Name, Leng);
			CString sss;
			sss = Name;
			if (sss != _T("プレイリスト")) {
				return;
			}
			if((GetKeyState(VK_RETURN)&0x8000)==0 && kk==1)
				kk=0;
			if(GetKeyState(VK_RETURN)&0x8000 && kk==0){
				kk=1;
				CString s;int i,j;
				int Lindex=-1;
				Lindex=pl->m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				i=Lindex;
				if(Lindex>=playcnt) return;
				if(Lindex==-1) return;
//				pl->SIcon(i);
				fnn=pl->pc[Lindex].name;
				filen=pl->pc[Lindex].fol;
				modesub=pl->pc[Lindex].sub;
				loop1=pl->pc[Lindex].loop1;
				loop2=pl->pc[Lindex].loop2;
				ret2=pl->pc[Lindex].ret2;
				plcnt=i;
				og->OnRestart();
			}
			if((GetKeyState(VK_CONTROL)&0x8000) && (GetKeyState('A')&0x8000)){
				int i=pl->m_lc.GetItemCount();
				for(int j=0;j<i;j++){
					pl->m_lc.SetItemState(j,LVIS_SELECTED,LVIS_SELECTED);
				}
			}
		}
		int tl=pl->m_tool.GetCheck();
		if(tl!=tlg){
			tlg=tl;
			if(tlg){
				pl->m_lc.EnableToolTips(TRUE);
				tl=pl->m_lc.GetExtendedStyle();
				tl = tl & ~LVS_EX_INFOTIP;
				pl->m_lc.SetExtendedStyle(tl);
			}else{
				pl->m_lc.EnableToolTips(FALSE);
				tl=pl->m_lc.GetExtendedStyle();
				tl |=LVS_EX_INFOTIP;
				pl->m_lc.SetExtendedStyle(tl);
			}
		}
	}
}
void timerpl(UINT nIDEvent,CPlayList* pl)
{
	try{
		_set_se_translator( trans_func1 );
		timerpl1(nIDEvent,pl);
//	}__except(EXCEPTION_EXECUTE_HANDLER){}
	}catch(SE_Exception1 e){
	}
	catch(_EXCEPTION_POINTERS *ep){
	}
	catch(...){}
}

extern int stflg;

#if WIN64
void CPlayList::OnTimer(UINT_PTR nIDEvent) 
#else
void CPlayList::OnTimer(UINT nIDEvent) 
#endif
{
	savedata.saveloop = m_loop.GetCheck();
	savedata.saverenzoku = m_renzoku.GetCheck();
	savedata.savecheck=m_savecheck.GetCheck();
	savedata.savecheck_mp3 = m_save_mp3.GetCheck();
	savedata.savecheck_dshow = m_save_kpi.GetCheck();
	CPlayList* pl = (CPlayList*)this;
	if(stflg == FALSE)
		timerpl(nIDEvent,pl);
	CCustomDialog::OnTimer(nIDEvent);
}

void CPlayList::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	CCustomDialog::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CPlayList::OnBnClickedCheck4()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	Save();
}

void CPlayList::OnBnClickedCheck1()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	Save();
}

void CPlayList::OnLvnBegindragList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNM = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	POINT ptPos,ptPos2;
    HIMAGELIST hOneImageList;
    HIMAGELIST hTempImageList;
	IMAGEINFO imf;
	long iHeight;
	m_hDragImage = ListView_CreateDragImage(m_lc.m_hWnd,pNM->iItem,&ptPos);
	ImageList_GetImageInfo(m_hDragImage, 0, &imf);
	iHeight = imf.rcImage.bottom;
	for(int Lindex=-1;;){
		Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);//pNM->iItem
		if(Lindex==-1) break;
		if(pNM->iItem==Lindex){
		}else{
            hOneImageList= ListView_CreateDragImage(m_lc.m_hWnd,Lindex,&ptPos2);
            hTempImageList = ImageList_Merge(m_hDragImage, 
                             0, hOneImageList, 0, 0, iHeight);
            ImageList_Destroy(m_hDragImage);
            ImageList_Destroy(hOneImageList);
            m_hDragImage = hTempImageList;
            ImageList_GetImageInfo(m_hDragImage, 0, &imf);
            iHeight = imf.rcImage.bottom;		}
	}
 	// ドラッグ開始
	POINT ptCursor;
	GetCursorPos(&ptCursor);
	m_lc.ScreenToClient(&ptCursor);

	long lX = ptCursor.x- ptPos.x;
	long lY = ptCursor.y- ptPos.y;

	ImageList_BeginDrag(m_hDragImage,0,lX,lY);
	ImageList_DragEnter(m_hWnd,0,0);
	SetCapture();


	*pResult = 0;
}

void CPlayList::OnDrag(int x,int y)
{
	POINT Point={x,y};
	ClientToScreen(&Point);
	RECT Rect;
	GetWindowRect(&Rect);
	ImageList_DragMove(Point.x-Rect.left,Point.y-Rect.top);
	{
		CPoint  point,point2;CRect rect;
		GetCursorPos(&point);
		ScreenToClient(&point);
		m_lc.GetWindowRect(&rect);
		point2.y=rect.top; point2.x=rect.left;
		ScreenToClient(&point2);
		point-=point2;
		int hItem = m_lc.HitTest(point ,NULL);
	}
}

void CPlayList::OnEndDrag()
{
	// ドラッグ終了
	ImageList_DragLeave(m_hWnd);
	ImageList_EndDrag();
	ImageList_Destroy(m_hDragImage);
	m_hDragImage = NULL;

	// カーソル表示
	ShowCursor(TRUE);
}
void CPlayList::OnMouseMove(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	if(GetCapture()==this){
		OnDrag(point.x,point.y);
	}
	CCustomDialog::OnMouseMove(nFlags, point);
}

void CPlayList::OnLButtonUp(UINT nFlags, CPoint point)
{
	// TODO: ここにメッセージ ハンドラ コードを追加するか、既定の処理を呼び出します。
	if(GetCapture()==this){
		OnEndDrag();
		// キャプチャ解除
		ReleaseCapture();
		//実際の移動のための座標割りだし
		CPoint  point,point2;CRect rect;
		GetCursorPos(&point);
		ScreenToClient(&point);
		m_lc.GetWindowRect(&rect);
		point2.y=rect.top; point2.x=rect.left;
		ScreenToClient(&point2);
		point-=point2;
		int hItem = m_lc.HitTest(point,NULL);
		if( hItem != -1){
			playlistdata *p;int cnt=0,j,cnt2=0,*cn;
			int Lindex = -1, Lindexx;
			for(;;){
				Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				if(Lindex==-1) break;
				cnt++;
			}
			//転送データをあらかじめ作っておく
			p = (playlistdata*)malloc(sizeof(playlistdata)*cnt);
			for(Lindexx=-1;;cnt2++){
				Lindexx=m_lc.GetNextItem(Lindexx,LVNI_ALL |LVNI_SELECTED);
				if(Lindexx==-1) break;
			}
			//転送するインテックス番号を獲得する
			int cn1;
			cn =(int*)malloc(sizeof(int)*cnt2);
			for(cn1=0,Lindexx=-1;;cn1++){
				Lindexx=m_lc.GetNextItem(Lindexx,LVNI_ALL |LVNI_SELECTED);
				if(Lindexx==-1) break;
				cn[cn1]=Lindexx;
			}
			CString s;
			for(cnt=0,Lindex=-1;;cnt++){
				Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				if(Lindex==-1) break;
				_tcscpy(p[cnt].name,pc[Lindex].name);
				_tcscpy(p[cnt].fol,pc[Lindex].fol);
				p[cnt].sub=pc[Lindex].sub;
				p[cnt].ret2=pc[Lindex].ret2;
			}
			for(Lindex=-1;;){
				playlistdata pp;
				Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
				if(Lindex==-1) break;
				_tcscpy(pp.name,pc[hItem].name);
				_tcscpy(pp.fol,pc[hItem].fol);
				pp.sub=pc[hItem].sub;
				pp.ret2=pc[hItem].ret2;
				int cnt1 = 0;
				for(;cnt1<cnt;cnt1++){
					if(_tcscmp(p[cnt1].name,pp.name)==0 && _tcscmp(p[cnt1].fol,pp.fol)==0 && p[cnt1].sub==pp.sub && p[cnt1].ret2==pp.ret2){
						break;
					}
				}
				if(cnt1!=cnt) break;
				if(hItem<Lindex){//選択項目が下　ドロップ位置が上
					int k=Lindex-hItem;
					m_lc.SetItemState(Lindex,m_lc.GetItemState(Lindex,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
					for(int kk=0;kk<k;kk++){
						OnXCHG(Lindex-kk,Lindex-kk-1);
					}
					m_lc.SetItemState(hItem  ,m_lc.GetItemState(hItem,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
				}else{//選択項目が上　ドロップ位置が下
					int Lindexx = -1;
					for(;;){
						Lindexx=m_lc.GetNextItem(Lindexx,LVNI_ALL |LVNI_SELECTED);
						if(Lindexx==-1) break;
						m_lc.SetItemState(Lindexx,m_lc.GetItemState(Lindexx,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
					}
					int i;
					for(i=0;i<cn1;i++){
						int k=hItem-cn[i];
						for(int kk=0;kk<k;kk++){
								OnXCHG(cn[i]+kk+1,cn[i]+kk);
						}
						for(int j=0;j<cn1;j++) cn[j]--;
					}
					hItem-=cn1;
					for(i=0;i<cn1;i++){
						hItem++;
						m_lc.SetItemState(hItem  ,m_lc.GetItemState(hItem,LVIS_SELECTED)|LVIS_SELECTED,LVIS_SELECTED);
					}
					break;
				}
				hItem++;
			}
			free(cn);
			free(p);
			m_lc.RedrawWindow();
			m_lDragTopItem=0;m_lDragTopItemt=0;
		 }
	}

	CCustomDialog::OnLButtonUp(nFlags, point);
}

void CPlayList::OnLvnGetdispinfoList1(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVDISPINFO* lpDInfo = reinterpret_cast<NMLVDISPINFO*>(pNMHDR);

	// 基本的なNULLチェックを行います
	if (lpDInfo == NULL) return;

	*pResult = 0; // 初期化

	try {
		_set_se_translator(trans_func1);

		// pcがまだ確保されていない、あるいは要素数が0の場合は何もせず安全に終了します
		// ※ m_nPcCount は pc配列の要素数を管理している変数に置き換えてください
		if (pc == NULL || playcnt <= 0) {
			return;
		}

		// 要求されたインデックスを取得します
		int nTargetIndex = lpDInfo->item.iItem;

		// インデックスが範囲外（個数オーバー）の場合の処理
		if (nTargetIndex < 0 || nTargetIndex >= playcnt) {
			// お嬢様のご指示通り、範囲外なら0番目を参照するようにします
			nTargetIndex = 0;
		}

		// テキスト情報の要求に対する処理
		if (lpDInfo->item.mask & LVIF_TEXT) {
			// 安全確保済みの nTargetIndex を使用して pc にアクセスします
			switch (lpDInfo->item.iSubItem) {
			case 0:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].name);
				break;
			case 1:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].game);
				break;
			case 2: {
				CString s;
				if (pc[nTargetIndex].time >= 3600)
					s.Format(_T("%d:%02d:%02d"), pc[nTargetIndex].time / 3600, (pc[nTargetIndex].time / 60) % 60, pc[nTargetIndex].time % 60);
				else
					s.Format(_T("%d:%02d"), pc[nTargetIndex].time / 60, pc[nTargetIndex].time % 60);

				if (pc[nTargetIndex].time == 0) s = "";
				if (pc[nTargetIndex].time == -1) s = LL14(L"取得不能", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch", L"Unable to fetch");
				_tcscpy(lpDInfo->item.pszText, s);
			} break;
			case 3:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].art);
				break;
			case 4:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].alb);
				break;
			case 5:
				_tcscpy(lpDInfo->item.pszText, pc[nTargetIndex].fol);
				break;
			default:
				break;
			}
		}

		// 画像情報の要求に対する処理
		if (lpDInfo->item.mask & LVIF_IMAGE) {
			// ここでも安全な nTargetIndex を使用します
			lpDInfo->item.iImage = pc[nTargetIndex].icon;
		}
	}
	catch (SE_Exception1 e) {
		// 例外発生時の処理
	}
	catch (_EXCEPTION_POINTERS* ep) {
		// 例外発生時の処理
	}
	catch (...) {
		// その他の例外
	}
}
void CPlayList::OnNMRclickList1(NMHDR *pNMHDR, LRESULT *pResult)
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	*pResult = 0;

	CPoint point;
	CRect rect;
	GetCursorPos(&point);

	CMenu menu;
	VERIFY(menu.LoadMenu(CG_IDR_POPUP_LIST));

	CMenu* pPopup = menu.GetSubMenu(0);
	ASSERT(pPopup != NULL);
	CWnd* pWndPopupOwner = this;

	while (pWndPopupOwner->GetStyle() & WS_CHILD)
		pWndPopupOwner = pWndPopupOwner->GetParent();

	pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y,
		pWndPopupOwner);

}

void CPlayList::OnList()
{
	int Lindex=-1;
	Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
	CListSyosai *a = new CListSyosai(CWnd::FromHandle(GetSafeHwnd()));
	w_flg=FALSE;
	CWnd::PostMessage(0x118);
	memcpy(&a->pc,&pc[Lindex],sizeof(playlistdata0));
	a->DoModal();
	w_flg=TRUE;
	delete a;
}
#define ID_HOTKEY0 8000
#define ID_HOTKEY1 8001
#define ID_HOTKEY2 8002
#define ID_HOTKEY3 8003
void CPlayList::OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
	CCustomDialog::OnActivate(nState, pWndOther, bMinimized);
	int l = 5;
	if(plw){
		if ((nState == WA_ACTIVE || nState == WA_CLICKACTIVE) && bMinimized == 0 && pl->m_saisyo.GetCheck()) {
			l = 20;
			ogpl = 1;
			og->ShowWindow(SW_RESTORE);
		}
	}
	if (nState == WA_ACTIVE || nState == WA_CLICKACTIVE) {
		SetTimer(4927, 10, NULL);

	}
	else {
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY0);
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY1);
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY2);
		UnregisterHotKey(og->m_hWnd, ID_HOTKEY3);
	}
//	else {
//		if (nState == WA_INACTIVE) {
//			SetTimer(4924, l, NULL);
//		}
//	}
	// TODO: ここにメッセージ ハンドラ コードを追加します。
}

void CPlayList::OnPop32787()//ファイル名変更
{
	// TODO: ここにコマンド ハンドラ コードを追加します。
	int Lindex=-1;
	Lindex=m_lc.GetNextItem(Lindex,LVNI_ALL |LVNI_SELECTED);
	CFilename *a = new CFilename(CWnd::FromHandle(GetSafeHwnd()));
	w_flg=FALSE;
	memcpy(&a->pc,&pc[Lindex],sizeof(playlistdata0));
	CWnd::PostMessage(0x118);
	int ret=a->DoModal();
	if(ret==IDOK){
		_tcscpy(pc[Lindex].name,a->pc.name);
		_tcscpy(pc[Lindex].art,a->pc.art);
		_tcscpy(pc[Lindex].alb,a->pc.alb);
		_tcscpy(pc[Lindex].fol,a->pc.fol);
		RECT r;
		m_lc.GetItemRect(Lindex,&r,LVIR_BOUNDS);
		m_lc.RedrawWindow(&r);
	}
	w_flg=TRUE;
	delete a;
}

void CPlayList::OnFindUp()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CString s;
	m_find.GetWindowText(s);
	s.MakeLower();
	if(s==_T("")) return;
	int pnt2;

	if(pnt<0) pnt=-1;
	if(pnt>=playcnt) pnt=playcnt;

	pnt2=pnt;
	if(pnt1!=-1) pnt2=pnt1;


	int flg=0;
	int i;
	for(i=pnt2;i<playcnt;i++){
		CString ss,ssl;
		ss=pc[i].name;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
		ss=pc[i].alb;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
		ss=pc[i].art;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
	}

	if(flg){
		for(int k=0;k<playcnt;k++){
			m_lc.SetItemState(k,m_lc.GetItemState(k,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}

		pnt1=i;

		m_lc.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
		m_lc.EnsureVisible(i,FALSE);
	}
	m_lc.SetFocus();
}

void CPlayList::OnFindDown()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CString s;
	m_find.GetWindowText(s);
	s.MakeLower();
	if(s==_T("")) return;
	int pnt2;

	if(pnt<0) pnt=-1;
	if(pnt>=playcnt) pnt=playcnt;

	pnt2=pnt;
	if(pnt1!=-1) pnt2=pnt1;


	int flg=0;
	int i;
	for(i=pnt2;i>=0;i--){
		CString ss,ssl;
		ss=pc[i].name;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
		ss=pc[i].alb;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
		ss=pc[i].art;
		ssl=ss;ssl.MakeLower();
		if(ssl.Find(s)!=-1 && pnt2!=i) {flg=1;break;}
	}

	if(flg){
		for(int k=0;k<playcnt;k++){
			m_lc.SetItemState(k,m_lc.GetItemState(k,LVIS_SELECTED)&~LVIS_SELECTED,LVIS_SELECTED);
		}
		pnt1=i;

		m_lc.SetItemState(i, LVIS_SELECTED, LVIS_SELECTED);
		m_lc.EnsureVisible(i,FALSE);
	}
	m_lc.SetFocus();
}


void CPlayList::OnBnClickedCheck6mp3()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
}


void CPlayList::OnBnClickedCheck7dshow()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
}


HBRUSH CPlayList::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{

	HBRUSH hbr = CCustomControlUtility::ApplyControlColors(pDC, pWnd, nCtlColor);
	if (hbr)
		return hbr;

	hbr = CCustomDialog::OnCtlColor(pDC, pWnd, nCtlColor);

	// TODO: ここで DC の属性を変更してください。
	if (savedata.aero == 1) {
		if (nCtlColor == CTLCOLOR_DLG)
		{
			return m_brDlg;
		}
		if (nCtlColor == CTLCOLOR_STATIC)
		{
			SetBkMode(pDC->m_hDC, TRANSPARENT);
			return m_brDlg;
		}
	}
	// TODO: 既定値を使用したくない場合は別のブラシを返します。
	return hbr;
}


void CPlayList::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CCustomDialog::OnShowWindow(bShow, nStatus);
	Invalidate();

	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


void CPlayList::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomDialog::OnMoving(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if (playbase)
	playbase->MoveWindow(&r);
//	if (playbase)
//		::SetWindowPos(playbase->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
//	::SetWindowPos(og->m_hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


void CPlayList::OnSizing(UINT fwSide, LPRECT pRect)
{
	CCustomDialog::OnSizing(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if(playbase)
		playbase->MoveWindow(&r);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}


void CPlayList::OnSetFocus(CWnd* pOldWnd)
{
	CCustomDialog::OnSetFocus(pOldWnd);

	// TODO: ここにメッセージ ハンドラー コードを追加します。

}


BOOL CPlayList::OnNcActivate(BOOL bActive)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
		// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	UINT_PTR aa = 0;
	DWORD aaa = 0;
	if (plw) {
		if (bActive && pl->m_saisyo.GetCheck()) {
		//	og->ShowWindow(SW_RESTORE);
		}
	}
	if (bActive) {
		//aaaa = 1;
//		if (playbase) playbase->ShowWindow(SW_SHOW);
//		KillTimer(4930);
		aa = SetTimer(4927, 10, NULL);
		aaa = GetLastError();
		aaa = aaa;
	}
	else {
		//if(!bActive)
		//	SetTimer(4924, 10, NULL);
	}
	return CCustomDialog::OnNcActivate(bActive);
}

BOOL changeflg = FALSE;
void CPlayList::OnCbnSelchangeCombo1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	if (changeflg == TRUE) return;
	Save();
	int num = m_listchange.GetCurSel();
	savedata.playlistnum = num;
	playcnt = 0;
	playlistdata0* tmp; tmp = pc;
	free(pc);
	pc = NULL;
	Load();
	if (pc == NULL) {
		pc = (playlistdata0*)malloc(sizeof(playlistdata0));
	}
	m_lc.SetItemCount(playcnt);
	for (int j = 0; j < playcnt; j++) pc[j].icon = 1;
	m_lc.RedrawWindow();
	Save();

	loadplaylistname();
}

void CPlayList::loadplaylistname()
{
	m_listchange.ResetContent();
	int lcnt = 0;
	for (lcnt = 0;; lcnt++) {
		CString s;
		if (lcnt == 0)
			s.Format(L"playlistu.dat");
		else
			s.Format(L"playlistu%d.dat", lcnt);
		if (!PathFileExists(GetModulePath() + s))
			break;
	}
	if (lcnt >= 999) lcnt = 999;
	for (int ii = 0; ii < lcnt; ii++) {
		CString s, ss;
		ss = savedata.playlistname[ii];
		if (ss == "") {
			ss.Format(LL14(
				L"プレイリスト：%d",
				L"Playlist: %d",
				L"Liste de lecture : %d",
				L"Playlist: %d",
				L"Lista de reproducción: %d",
				L"플레이리스트: %d",
				L"播放列表：%d",
				L"قائمة التشغيل: %d",
				L"Плейлист: %d",
				L"Wiedergabeliste: %d",
				L"Lista de reprodução: %d",
				L"Afspeellijst: %d",
				L"Lista odtwarzania: %d",
				L"Oynatma Listesi: %d"),
				ii + 1);
		}
		s.Format(L"%s",ss);
		m_listchange.AddString(s);
	}
	m_listchange.AddString(LL14(
		L"<新しいプレイリスト>",
		L"<New playlist>",
		L"<Nouvelle liste de lecture>",
		L"<Nuova playlist>",
		L"<Nueva lista de reproducción>",
		L"<새로운 플레이리스트>",
		L"<新建播放列表>",
		L"<قائمة تشغيل جديدة>",
		L"<Новый плейлист>",
		L"<Neue Wiedergabeliste>",
		L"<Nova lista de reprodução>",
		L"<Nieuwe afspeellijst>",
		L"<Nowa lista odtwarzania>",
		L"<Yeni oynatma listesi>")); m_listchange.SetCurSel(savedata.playlistnum);
	int num = m_listchange.GetCurSel();
	if (num != savedata.playlistnum) {
		savedata.playlistnum = 0;
		m_listchange.SetCurSel(savedata.playlistnum);
	}
}

CString CPlayList::GetModulePath()
{
	// 実行ファイルのパス
	CString modulePath = _T("");
	// ドライブ名、ディレクトリ名、ファイル名、拡張子
	wchar_t path[_MAX_PATH], drive[_MAX_PATH], dir[_MAX_PATH], fname[_MAX_PATH], ext[_MAX_PATH];

	// 実行ファイルのファイルパスを取得
	if (::GetModuleFileName(NULL, path, _MAX_PATH) != 0)
	{
		// ファイルパスを分割
		::_wsplitpath_s(path, drive, dir, fname, ext);
		// ドライブとディレクトリ名を結合して実行ファイルパスとする
		modulePath = CString(drive) + CString(dir);
	}

	return modulePath;
}

void CPlayList::OnBnClickedButton3()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CPlayListNew pn;
	pn.name = savedata.playlistname[savedata.playlistnum];
	if (pn.name == L"") pn.name.Format(L"プレイリスト：%d", savedata.playlistnum + 1);
	if (pn.DoModal() == IDOK) {
		wcscpy(savedata.playlistname[savedata.playlistnum], pn.name);
		loadplaylistname();
	}

}


void CPlayList::OnBnClickedPlaydelete()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	if (MessageBox(LL14(
		L"現在のリストを削除しますがよろしいですか？",
		L"Delete the current list?",
		L"Supprimer la liste actuelle ?",
		L"Eliminare la lista corrente?",
		L"¿Eliminar la lista actual?",
		L"현재 목록을 삭제하시겠습니까?",
		L"确定要删除当前列表吗？",
		L"هل تريد حذف القائمة الحالية؟",
		L"Удалить текущий список?",
		L"Aktuelle Liste löschen?",
		L"Excluir a lista atual?",
		L"Huidige lijst verwijderen?",
		L"Usunąć bieżącą listę?",
		L"Mevcut liste silinsin mi?"),
		LL14(
			L"削除確認",
			L"Confirm Delete",
			L"Confirmer la suppression",
			L"Conferma eliminazione",
			L"Confirmar eliminación",
			L"삭제 확인",
			L"确认删除",
			L"تأكيد الحذف",
			L"Подтверждение удаления",
			L"Löschung bestätigen",
			L"Confirmar exclusão",
			L"Verwijdering bevestigen",
			L"Potwierdź usunięcie",
			L"Silmeyi Onayla"),
		MB_YESNO) == IDNO) {
		return;
	}
	changeflg = TRUE;
	Save();
	CString s;
	int num = m_listchange.GetCurSel();

	int lcnt = 0;
	for (lcnt = 0;; lcnt++) {
		CString s;
		if (lcnt == 0)
			s.Format(L"playlistu.dat");
		else
			s.Format(L"playlistu%d.dat", lcnt);
		if (!PathFileExists(GetModulePath() + s))
			break;
	}
	if (lcnt >= 999) lcnt = 999;

	if (lcnt == 0 && num == 0) {//まだ追加してなくて、一番最初のを削除されたとき、
		m_listchange.ResetContent();
		s.Format(L"playlistu.dat");
		savedata.playlistname[0][0] = 0;
		CFile::Remove(GetModulePath() + s);
		savedata.playlistnum = 0;
		Load();
		savedata.playlistnum = 0;
		loadplaylistname();
		changeflg = FALSE;
		return;
	}

	if (lcnt != 0) {//削除されたとき
		CString s1, s2;
		if (num == 0)
			s1.Format(L"playlistu.dat");
		else
			s1.Format(L"playlistu%d.dat", num);
		if (PathFileExists(GetModulePath() + s1))
			CFile::Remove(GetModulePath() + s1);
		for (int j = num; j < lcnt - 1; j++) {
			if (j == 0)
				s1.Format(L"playlistu.dat");
			else
				s1.Format(L"playlistu%d.dat", j);
			const int jj = j + 1;
			if (jj == 0)
				s2.Format(L"playlistu.dat");
			else
				s2.Format(L"playlistu%d.dat", jj);

			CFile::Rename(GetModulePath() + s2, GetModulePath() + s1);
			wcscpy(savedata.playlistname[j], savedata.playlistname[jj]);
		}
		lcnt = 0;
		for (lcnt = 0;; lcnt++) {
			CString s;
			if (lcnt == 0)
				s.Format(L"playlistu.dat");
			else
				s.Format(L"playlistu%d.dat", lcnt);
			if (!PathFileExists(GetModulePath() + s))
				break;
		}
		if (lcnt >= 999) lcnt = 999;

		savedata.playlistname[lcnt][0] = 0;
		if (num == lcnt) {
			savedata.playlistnum = 0;
			num = 0;
		}
		loadplaylistname();
		int num = m_listchange.GetCurSel();
		savedata.playlistnum = num;
		playcnt = 0;
		playlistdata0* tmp; tmp = pc;
		free(pc);
		pc = NULL;
		Load();
		if (pc == NULL) {
			pc = (playlistdata0*)malloc(sizeof(playlistdata0));
		}
		m_lc.SetItemCount(playcnt);
		for (int j = 0; j < playcnt; j++) pc[j].icon = 1;
		m_lc.RedrawWindow();
		Save();
		changeflg = FALSE;
		return;
	}
}

