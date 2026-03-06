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
	SetWindowText(LL14(L"プレイリスト", L"Playlist", L"Liste de lecture", L"Playlist", L"Lista de reproduccion", L"?? ??", L"播放列表", L"????? ???????", L"Плейлист", L"Wiedergabeliste", L"Lista de reproducao", L"Afspeellijst", L"Lista odtwarzania", L"Calma listesi"));
	SetDlgItemText(IDC_CHECK1, LL14(L"連続再生", L"Continuous play", L"Lecture continue", L"Riproduzione continua", L"Reproduccion continua", L"?? ??", L"??播放", L"????? ?????", L"Непрерывное воспроизведение", L"Fortlaufende Wiedergabe", L"Reproducao continua", L"Doorlopend afspelen", L"Ci?g?e odtwarzanie", L"Surekli calma"));
	SetDlgItemText(IDC_CHECK4, LL14(L"ループ再生", L"Loop play", L"Lecture en boucle", L"Riproduzione in loop", L"Reproduccion en bucle", L"?? ??", L"循?播放", L"????? ?????", L"Зацикленное воспроизведение", L"Schleifenwiedergabe", L"Reproducao em loop", L"Herhalend afspelen", L"Odtwarzanie w p?tli", L"Dongude calma"));
	SetDlgItemText(IDC_CHECK28, LL14(L"ツールチップ表示", L"Show tooltips", L"Afficher les info-bulles", L"Mostra suggerimenti", L"Mostrar sugerencias", L"?? ??", L"?示工具提示", L"????? ?????????", L"Показывать подсказки", L"Tooltips anzeigen", L"Mostrar dicas", L"Tooltips tonen", L"Poka? etykiety", L"?puclar?n? goster"));
	SetDlgItemText(IDC_CHECK29, LL14(L"最小化、復帰", L"Minimize, restore", L"Reduire, restaurer", L"Riduci, ripristina", L"Minimizar, restaurar", L"???, ??", L"最小化、?原", L"?????? ???????", L"Свернуть, восстановить", L"Minimieren, wiederherstellen", L"Minimizar, restaurar", L"Minimaliseren, herstellen", L"Minimalizuj, przywro?", L"Kucult, geri yukle"));
	SetDlgItemText(IDC_CHECK5, LL14(L"再生位置\nを保存", L"Save\nplayback position", L"Enregistrer la\nposition de lecture", L"Salva posizione\ndi riproduzione", L"Guardar posicion\nde reproduccion", L"?? ??\n??", L"保存\n播放位置", L"??? ???? ???????", L"Сохранить позицию\nвоспроизведения", L"Wiedergabeposition\nspeichern", L"Salvar posicao\nde reproducao", L"Afspeelpositie\nopslaan", L"Zapisz pozycj?\nodtwarzania", L"Oynatma konumunu\nkaydet"));
	SetDlgItemText(IDC_STATICido, LL14(L"ファイル移動", L"File move", L"Deplacer fichier", L"Sposta file", L"Mover archivo", L"?? ??", L"文件移?", L"??? ?????", L"Переместить файл", L"Datei verschieben", L"Mover arquivo", L"Bestand verplaatsen", L"Przenie? plik", L"Dosya ta??"));
	SetDlgItemText(IDC_STATICken, LL14(L"あいまい検索", L"Fuzzy search", L"Recherche floue", L"Ricerca fuzzy", L"Busqueda difusa", L"?? ??", L"模糊搜索", L"??? ????", L"Нечеткий поиск", L"Fuzzy-Suche", L"Pesquisa fuzzy", L"Fuzzy zoeken", L"Wyszukiwanie rozmyte", L"Bulan?k arama"));
	SetDlgItemText(IDC_BUTTON3, LL14(L"名前変更", L"Rename", L"Renommer", L"Rinomina", L"Cambiar nombre", L"?? ???", L"重命名", L"????? ?????", L"Переименовать", L"Umbenennen", L"Renomear", L"Hernoemen", L"Zmie? nazw?", L"Yeniden adland?r"));
	SetDlgItemText(IDC_PLAYDELETE, LL14(L"リスト削除", L"Delete list", L"Supprimer la liste", L"Elimina lista", L"Eliminar lista", L"?? ??", L"?除列表", L"??? ???????", L"Удалить список", L"Liste loschen", L"Excluir lista", L"Lijst verwijderen", L"Usu? list?", L"Listeyi sil"));
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
	m_tooltip.AddTool(GetDlgItem(IDOK), LL14(L"プレイリストを閉じます。", L"Close the playlist.", L"Fermer la liste de lecture.", L"Chiudi la playlist.", L"Cerrar la lista de reproduccion.", L"?? ??? ????.", L"??播放列表。", L"????? ????? ???????.", L"Закрыть плейлист.", L"Wiedergabeliste schliesen.", L"Fechar lista de reproducao.", L"Afspeellijst sluiten.", L"Zamknij list? odtwarzania.", L"Calma listesini kapat."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON1), LL14(L"選択項目を一番上に持って行きます。", L"Move selected item to the top.", L"Deplacer l'element selectionne tout en haut.", L"Sposta l'elemento selezionato in cima.", L"Mover elemento seleccionado al inicio.", L"??? ??? ? ?? ?????.", L"将所??目移至?部。", L"??? ?????? ?????? ??? ??????.", L"Переместить выбранный элемент вверх.", L"Gewahltes Element nach oben verschieben.", L"Mover item selecionado para o topo.", L"Geselecteerd item naar boven verplaatsen.", L"Przenie? zaznaczony element na gor?.", L"Secili o?eyi en uste ta??."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON5), LL14(L"選択項目を上に持って行きます。", L"Move selected item up.", L"Deplacer l'element selectionne vers le haut.", L"Sposta l'elemento selezionato in alto.", L"Mover elemento seleccionado arriba.", L"??? ??? ?? ?????.", L"将所??目上移。", L"??? ?????? ?????? ?????.", L"Переместить выбранный элемент вверх.", L"Gewahltes Element nach oben verschieben.", L"Mover item selecionado para cima.", L"Geselecteerd item omhoog verplaatsen.", L"Przenie? zaznaczony element w gor?.", L"Secili o?eyi yukar? ta??."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON10), LL14(L"選択項目を一番下に持って行きます。", L"Move selected item to the bottom.", L"Deplacer l'element selectionne tout en bas.", L"Sposta l'elemento selezionato in fondo.", L"Mover elemento seleccionado al final.", L"??? ??? ? ??? ?????.", L"将所??目移至底部。", L"??? ?????? ?????? ??? ??????.", L"Переместить выбранный элемент вниз.", L"Gewahltes Element nach unten verschieben.", L"Mover item selecionado para o final.", L"Geselecteerd item naar beneden verplaatsen.", L"Przenie? zaznaczony element na do?.", L"Secili o?eyi en alta ta??."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON11), LL14(L"選択項目を下に持って行きます。", L"Move selected item down.", L"Deplacer l'element selectionne vers le bas.", L"Sposta l'elemento selezionato in basso.", L"Mover elemento seleccionado abajo.", L"??? ??? ??? ?????.", L"将所??目下移。", L"??? ?????? ?????? ?????.", L"Переместить выбранный элемент вниз.", L"Gewahltes Element nach unten verschieben.", L"Mover item selecionado para baixo.", L"Geselecteerd item omlaag verplaatsen.", L"Przenie? zaznaczony element w do?.", L"Secili o?eyi a?a?? ta??."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON16), LL14(L"現在の位置から下に検索します。", L"Search downward from current position.", L"Rechercher vers le bas a partir de la position actuelle.", L"Cerca verso il basso dalla posizione corrente.", L"Buscar hacia abajo desde la posicion actual.", L"?? ???? ??? ?????.", L"从当前位置向下搜索。", L"????? ?????? ?? ?????? ??????.", L"Искать вниз от текущей позиции.", L"Ab aktueller Position nach unten suchen.", L"Pesquisar para baixo a partir da posicao atual.", L"Zoek naar beneden vanaf de huidige positie.", L"Szukaj w do? od bie??cej pozycji.", L"Mevcut konumdan a?a?? do?ru ara."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON20), LL14(L"現在の位置から上に検索します。", L"Search upward from current position.", L"Rechercher vers le haut a partir de la position actuelle.", L"Cerca verso l'alto dalla posizione corrente.", L"Buscar hacia arriba desde la posicion actual.", L"?? ???? ?? ?????.", L"从当前位置向上搜索。", L"????? ?????? ?? ?????? ??????.", L"Искать вверх от текущей позиции.", L"Ab aktueller Position nach oben suchen.", L"Pesquisar para cima a partir da posicao atual.", L"Zoek naar boven vanaf de huidige positie.", L"Szukaj w gor? od bie??cej pozycji.", L"Mevcut konumdan yukar? do?ru ara."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK1), LL14(L"プレイリストの順番に再生を行います。\n再生中にファイルドロップして追加しても演奏中の曲はそのまま鳴り続けます。", L"Playback in playlist order.\nEven if files are added during playback, the currently playing track continues.", L"Lecture dans l'ordre de la liste.\nLa piste en cours continue meme si des fichiers sont ajoutes pendant la lecture.", L"Riproduzione nell'ordine della playlist.\nAnche se aggiungi file durante la riproduzione, la traccia corrente continua.", L"Reproduccion en orden de la lista.\nAunque se anadan archivos durante la reproduccion, la pista actual continua.", L"?? ?? ???? ?????.\n?? ? ??? ???? ?? ?? ?? ?? ?? ?????.", L"按播放列表?序播放。\n播放期?添加文件?，当前曲目仍??播放。", L"??????? ?????? ???????.\n??? ??? ????? ????? ????? ???????? ????? ?????? ??????.", L"Воспроизведение по порядку плейлиста.\nДаже при добавлении файлов текущий трек продолжает воспроизводиться.", L"Wiedergabe in Playlist-Reihenfolge.\nBei zusatzlichen Dateien wahrend der Wiedergabe lauft der aktuelle Titel weiter.", L"Reproducao na ordem da lista.\nMesmo ao adicionar arquivos durante a reproducao, a faixa atual continua.", L"Afspeel in playlistvolgorde.\nBij toevoegen van bestanden tijdens afspelen gaat het huidige nummer door.", L"Odtwarzaj w kolejno?ci listy.\nPrzy dodawaniu plikow podczas odtwarzania aktualny utwor kontynuuje.", L"Liste s?ras?na gore calma.\nCalma s?ras?nda dosya eklense bile cal?nan parca devam eder."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK4), LL14(L"選択した曲をループさせます。\n再生する前にチェックを入れる必要があります。\nそうでないとループはかかりません。\nループポイントが0のもの(mp3やループしない曲)が対象です。", L"Loop selected track.\nCheck before playback to enable looping.\nOtherwise, looping will not work.\nApplies to tracks with loop point 0 (mp3 or non-looping tracks).", L"Boucler la piste selectionnee.\nCochez avant la lecture pour activer la boucle.\nS'applique aux pistes avec point de boucle 0.", L"Ripeti la traccia selezionata.\nSpunta prima della riproduzione per attivare il loop.", L"Repetir pista seleccionada.\nMarque antes de reproducir para activar el bucle.", L"??? ?? ?????.\n?? ?? ???? ???.", L"循?所?曲目。\n播放前需勾?才能?用循?。", L"????? ?????? ??????.\n???? ??? ??????? ?????? ???????.", L"Зациклить выбранный трек.\nОтметьте перед воспроизведением.", L"Gewahlten Titel wiederholen.\nVor Wiedergabe aktivieren.", L"Repetir faixa selecionada.\nMarque antes de reproduzir para ativar o loop.", L"Herhaal geselecteerd nummer.\nVink aan voor afspelen.", L"Zap?tl zaznaczony utwor.\nZaznacz przed odtwarzaniem.", L"Secili parcay? donguye al.\nCalmadan once i?aretleyin."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK28), LL14(L"ツールチップを表示します。", L"Show tooltips.", L"Afficher les info-bulles.", L"Mostra suggerimenti.", L"Mostrar sugerencias.", L"??? ?????.", L"?示工具提示。", L"????? ?????????.", L"Показывать подсказки.", L"Tooltips anzeigen.", L"Mostrar dicas.", L"Tooltips tonen.", L"Poka? etykiety.", L"?puclar?n? goster."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK29), LL14(L"最小化、最小化からの復帰時、メイン画面とプレイリスト画面も同時に最小化、最小化からの復帰を行います。", L"When minimizing/restoring, main window and playlist window minimize/restore together.", L"Lors de la minimisation/restauration, les fenetres principale et playlist font de meme.", L"Alla minimizzazione/ripristino, finestra principale e playlist si minimizzano/ripristinano insieme.", L"Al minimizar/restaurar, ventana principal y lista se minimizan/restauran juntas.", L"???/?? ? ?? ?? ?? ?? ?? ?? ???/?????.", L"最小化/?原?，主窗口和播放列表窗口同?最小化/?原。", L"??? ???????/?????????? ??????? ??????? ????.", L"При сворачивании/восстановлении окна сворачиваются вместе.", L"Beim Minimieren/Wiederherstellen werden beide Fenster zusammen behandelt.", L"Ao minimizar/restaurar, as janelas fazem o mesmo juntas.", L"Bij minimaliseren/herstellen gaan beide vensters mee.", L"Przy minimalizowaniu/przywracaniu okna zmieniaj? si? razem.", L"Kucultme/geri yuklemede ana pencere ve liste birlikte de?i?ir."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK5), LL14(L"途中で演奏を停止した位置を自動保存します。\nmp3系と動画(avi,mp4など)のみ対応。\n停止ボタンもしくは終了したときのみ保存します。\n再生中に違う曲を選んだ時は位置は保存しません。", L"Auto-save playback position when stopped.\nSupports mp3 and video (avi, mp4, etc.) only.\nSaves only when stop button is pressed or when exiting.\nPosition is not saved when selecting a different track during playback.", L"Enregistrement auto de la position a l'arret.\nPrise en charge mp3 et video uniquement.", L"Salva automaticamente la posizione all'arresto.\nSupporta solo mp3 e video.", L"Guardar posicion automaticamente al detener.\nSolo mp3 y video.", L"?? ? ?? ??? ?? ?????.\nmp3 ? ???? ??.", L"停止?自?保存播放位置。\n?支持mp3和??。", L"??? ???? ??????? ???????? ??? ??????.", L"Автосохранение позиции при остановке.\nТолько mp3 и видео.", L"Position automatisch speichern.\nNur mp3 und Video.", L"Salva posicao ao parar.\nApenas mp3 e video.", L"Positie opslaan bij stoppen.\nAlleen mp3 en video.", L"Zapisz pozycj? przy zatrzymaniu.\nTylko mp3 i wideo.", L"Durduruldu?unda konumu kaydet.\nSadece mp3 ve video."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK6), LL14(L"mp3再生時に途中保存を有効にします。", L"Enable mid-playback save for mp3.", L"Activer l'enregistrement de position pour mp3.", L"Abilita salvataggio posizione per mp3.", L"Habilitar guardado de posicion para mp3.", L"mp3 ?? ? ?? ??? ??????.", L"mp3播放??用位置保存。", L"????? ??? ?????? ?? mp3.", L"Включить сохранение позиции для mp3.", L"Positionsspeicherung fur mp3 aktivieren.", L"Habilitar salvamento para mp3.", L"Positieopslag voor mp3 inschakelen.", L"W??cz zapisywanie pozycji dla mp3.", L"mp3 icin konum kayd?n? etkinle?tir."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK7), LL14(L"動画などのDirectShow使用時に途中保存を有効にします。", L"Enable mid-playback save for DirectShow (videos, etc.).", L"Activer l'enregistrement pour DirectShow (videos).", L"Abilita salvataggio per DirectShow (video).", L"Habilitar guardado para DirectShow (videos).", L"??? DirectShow ?? ? ?? ??? ??????.", L"DirectShow（??等）??用位置保存。", L"????? ??? ?????? ?? DirectShow (?????).", L"Включить сохранение для DirectShow (видео).", L"Fur DirectShow (Videos) aktivieren.", L"Habilitar para DirectShow (videos).", L"Voor DirectShow (video's) inschakelen.", L"W??cz dla DirectShow (wideo).", L"DirectShow (videolar) icin etkinle?tir."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO1), LL14(L"プレイリストを変更または追加します。", L"Change or add playlists.", L"Modifier ou ajouter des listes.", L"Cambia o aggiungi playlist.", L"Cambiar o anadir listas.", L"?? ??? ????? ?????.", L"更改或添加播放列表。", L"????? ?? ????? ?????.", L"Изменить или добавить плейлисты.", L"Playlists andern oder hinzufugen.", L"Alterar ou adicionar listas.", L"Playlists wijzigen of toevoegen.", L"Zmie? lub dodaj listy.", L"Listeleri de?i?tir veya ekle."));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON3), LL14(L"プレイリスト名を変更します。", L"Rename playlist.", L"Renommer la liste.", L"Rinomina playlist.", L"Cambiar nombre de lista.", L"?? ?? ??? ?????.", L"重命名播放列表。", L"????? ????? ???????.", L"Переименовать плейлист.", L"Playlist umbenennen.", L"Renomear lista.", L"Playlist hernoemen.", L"Zmie? nazw? listy.", L"Liste ad?n? de?i?tir."));
	m_tooltip.AddTool(GetDlgItem(IDC_PLAYDELETE), LL14(L"表示されているプレイリストを削除します。\n※削除したものは復活できないので注意ください。", L"Delete the displayed playlist.\n*Deleted playlists cannot be recovered.", L"Supprimer la liste affichee.\n*Les listes supprimees ne peuvent pas etre recuperees.", L"Elimina la playlist visualizzata.\n*Le playlist eliminate non possono essere recuperate.", L"Eliminar la lista mostrada.\n*Las listas eliminadas no se pueden recuperar.", L"??? ?? ??? ?????.\n*??? ??? ??? ? ????.", L"?除?示的播放列表。\n*?除后无法恢?。", L"??? ??????? ????????.\n*?? ???? ??????? ??????? ????????.", L"Удалить отображаемый плейлист.\n*Удалённые плейлисты восстановить нельзя.", L"Angezeigte Playlist loschen.\n*Geloschte Playlists konnen nicht wiederhergestellt werden.", L"Excluir lista exibida.\n*Listas excluidas nao podem ser recuperadas.", L"Getoonde playlist verwijderen.\n*Verwijderde playlists kunnen niet worden hersteld.", L"Usu? wy?wietlan? list?.\n*Usuni?tych list nie mo?na odzyska?.", L"Gosterilen listeyi sil.\n*Silinen listeler geri al?namaz."));
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
	m_lc.InsertColumn ( 0, LL14(L"名前", L"Name", L"Nom", L"Nome", L"Nombre", L"??", L"名称", L"?????", L"Имя", L"Name", L"Nome", L"Naam", L"Nazwa", L"Ad"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 1, LL14(L"ゲーム", L"Game", L"Jeu", L"Gioco", L"Juego", L"??", L"游?", L"??????", L"Игра", L"Spiel", L"Jogo", L"Spel", L"Gra", L"Oyun"), LVCFMT_LEFT, 50, 0 );
	m_lc.InsertColumn ( 2, LL14(L"時間", L"Time", L"Duree", L"Durata", L"Duracion", L"??", L"??", L"?????", L"Время", L"Zeit", L"Duracao", L"Tijd", L"Czas", L"Sure"), LVCFMT_RIGHT, 50, 0 );
	m_lc.InsertColumn ( 3, LL14(L"アーティスト", L"Artist", L"Artiste", L"Artista", L"Artista", L"????", L"??家", L"??????", L"Исполнитель", L"Kunstler", L"Artista", L"Artiest", L"Artysta", L"Sanatc?"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 4, LL14(L"アルバム/コメント", L"Album/Comment", L"Album/Commentaire", L"Album/Commento", L"Album/Comentario", L"??/???", L"??/注?", L"???????/???????", L"Альбом/Комментарий", L"Album/Kommentar", L"Album/Comentario", L"Album/Opmerking", L"Album/Komentarz", L"Album/Yorum"), LVCFMT_LEFT, 200, 0 );
	m_lc.InsertColumn ( 5, LL14(L"フォルダ", L"Folder", L"Dossier", L"Cartella", L"Carpeta", L"??", L"文件?", L"??????", L"Папка", L"Ordner", L"Pasta", L"Map", L"Folder", L"Klasor"), LVCFMT_LEFT, 50, 0 );
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
			s.Format(LL14(_T("%sファイル"), _T("%s File"), _T("%s fichier"), _T("%s file"), _T("%s archivo"), _T("%s ??"), _T("%s文件"), _T("%s ???"), _T("%s файл"), _T("%s Datei"), _T("%s arquivo"), _T("%s bestand"), _T("%s plik"), _T("%s dosya")),ss);break;
		case -2:
			ss=fol.Right(fol.GetLength()-fol.ReverseFind('.')-1);
			s.Format(LL14(_T("%sファイル"), _T("%s File"), _T("%s fichier"), _T("%s file"), _T("%s archivo"), _T("%s ??"), _T("%s文件"), _T("%s ???"), _T("%s файл"), _T("%s Datei"), _T("%s arquivo"), _T("%s bestand"), _T("%s plik"), _T("%s dosya")),ss);break;
		case -1:s=LL14(L"oggファイル", L"ogg File", L"ogg fichier", L"ogg file", L"ogg archivo", L"ogg ??", L"ogg文件", L"ogg ???", L"ogg файл", L"ogg Datei", L"ogg arquivo", L"ogg bestand", L"ogg plik", L"ogg dosya");break;
		case -7:
			s = fol; s.MakeLower();
			if (s.Right(3) == "dsf") { s = LL14(_T("dsfファイル(DSD)"), _T("dsf File(DSD)"), _T("dsf fichier(DSD)"), _T("dsf file(DSD)"), _T("dsf archivo(DSD)"), _T("dsf ??(DSD)"), _T("dsf文件(DSD)"), _T("dsf ???(DSD)"), _T("dsf файл(DSD)"), _T("dsf Datei(DSD)"), _T("dsf arquivo(DSD)"), _T("dsf bestand(DSD)"), _T("dsf plik(DSD)"), _T("dsf dosya(DSD)")); break; }
			if (s.Right(3) == "wsd") { s = LL14(_T("wsdファイル(DSD)"), _T("wsd File(DSD)"), _T("wsd fichier(DSD)"), _T("wsd file(DSD)"), _T("wsd archivo(DSD)"), _T("wsd ??(DSD)"), _T("wsd文件(DSD)"), _T("wsd ???(DSD)"), _T("wsd файл(DSD)"), _T("wsd Datei(DSD)"), _T("wsd arquivo(DSD)"), _T("wsd bestand(DSD)"), _T("wsd plik(DSD)"), _T("wsd dosya(DSD)")); break; }
			if (s.Right(3) == "dff") { s = LL14(_T("dffファイル(DSD)"), _T("dff File(DSD)"), _T("dff fichier(DSD)"), _T("dff file(DSD)"), _T("dff archivo(DSD)"), _T("dff ??(DSD)"), _T("dff文件(DSD)"), _T("dff ???(DSD)"), _T("dff файл(DSD)"), _T("dff Datei(DSD)"), _T("dff arquivo(DSD)"), _T("dff bestand(DSD)"), _T("dff plik(DSD)"), _T("dff dosya(DSD)")); break; }
		case -8:
			s = fol; s.MakeLower();
			if (s.Right(4) == "flac") { s = LL14(_T("flacファイル"), _T("flac File"), _T("flac fichier"), _T("flac file"), _T("flac archivo"), _T("flac ??"), _T("flac文件"), _T("flac ???"), _T("flac файл"), _T("flac Datei"), _T("flac arquivo"), _T("flac bestand"), _T("flac plik"), _T("flac dosya")); break; }
			if (s.Right(6).MakeLower() == "qull3h") { s = LL14(_T("Qull3Hファイル"), _T("Qull3H File"), _T("Qull3H fichier"), _T("Qull3H file"), _T("Qull3H archivo"), _T("Qull3H ??"), _T("Qull3H文件"), _T("Qull3H ???"), _T("Qull3H файл"), _T("Qull3H Datei"), _T("Qull3H arquivo"), _T("Qull3H bestand"), _T("Qull3H plik"), _T("Qull3H dosya")); break; }
		case -9:
			s = fol; s.MakeLower();
			if (s.Right(3) == "m4a") { s = LL14(_T("m4aファイル"), _T("m4a File"), _T("m4a fichier"), _T("m4a file"), _T("m4a archivo"), _T("m4a ??"), _T("m4a文件"), _T("m4a ???"), _T("m4a файл"), _T("m4a Datei"), _T("m4a arquivo"), _T("m4a bestand"), _T("m4a plik"), _T("m4a dosya")); break; }
			if (s.Right(3) == "aac") { s = LL14(_T("aacファイル"), _T("aac File"), _T("aac fichier"), _T("aac file"), _T("aac archivo"), _T("aac ??"), _T("aac文件"), _T("aac ???"), _T("aac файл"), _T("aac Datei"), _T("aac arquivo"), _T("aac bestand"), _T("aac plik"), _T("aac dosya")); break; }
		case -10:
			s=fol;s.MakeLower();
			if(s.Right(3)=="mp3"){ s=LL14(L"mp3ファイル", L"mp3 File", L"mp3 fichier", L"mp3 file", L"mp3 archivo", L"mp3 ??", L"mp3文件", L"mp3 ???", L"mp3 файл", L"mp3 Datei", L"mp3 arquivo", L"mp3 bestand", L"mp3 plik", L"mp3 dosya");break;}
			if(s.Right(3)=="mp2"){ s=LL14(L"mp2ファイル", L"mp2 File", L"mp2 fichier", L"mp2 file", L"mp2 archivo", L"mp2 ??", L"mp2文件", L"mp2 ???", L"mp2 файл", L"mp2 Datei", L"mp2 arquivo", L"mp2 bestand", L"mp2 plik", L"mp2 dosya");break;}
			if(s.Right(3)=="mp1"){ s=LL14(L"mp1ファイル", L"mp1 File", L"mp1 fichier", L"mp1 file", L"mp1 archivo", L"mp1 ??", L"mp1文件", L"mp1 ???", L"mp1 файл", L"mp1 Datei", L"mp1 arquivo", L"mp1 bestand", L"mp1 plik", L"mp1 dosya");break;}
			if(s.Right(3)=="rmp"){ s=LL14(L"rmpファイル", L"rmp File", L"rmp fichier", L"rmp file", L"rmp archivo", L"rmp ??", L"rmp文件", L"rmp ???", L"rmp файл", L"rmp Datei", L"rmp arquivo", L"rmp bestand", L"rmp plik", L"rmp dosya");break;}
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
				
				ff2.Open(s, CFile::modeRead | CFile::shareDenyWrite, NULL);
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
						ss = LL14(L"バトル#58", L"Battle #58", L"Combat #58", L"Battaglia #58", L"Batalla #58", L"?? #58", L"?斗 #58", L"????? #58", L"Сражение #58", L"Kampf #58", L"Batalha #58", L"Gevecht #58", L"Bitwa #58", L"Sava? #58");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b002.ogg") {
						ss = LL14(L"灼熱の炎の中で", L"Within the Blazing Flames", L"Dans les flammes ardentes", L"Tra le fiamme ardenti", L"Entre las llamas ardientes", L"???? ?? ???", L"在灼熱的火?中", L"?? ???? ?????", L"В раскаленном пламени", L"In den lodernden Flammen", L"Nas chamas ardentes", L"In de brandende vlammen", L"W p?on?cych p?omieniach", L"Yanan Alevlerin ?cinde");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b003.ogg") {
						ss = LL14(L"最終決戦", L"Final Battle", L"Bataille finale", L"Battaglia finale", L"Batalla final", L"?? ??", L"最終決戰", L"??????? ????????", L"Финальная битва", L"Letzter Kampf", L"Batalha final", L"Laatste gevecht", L"Ostateczna bitwa", L"Son Sava?");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b004.ogg") {
						ss = LL14(L"黒き翼", L"Black Wings", L"Ailes noires", L"Ali nere", L"Alas negras", L"?? ??", L"黑色翅膀", L"????? ?????", L"Черные крылья", L"Schwarze Flugel", L"Asas negras", L"Zwarte vleugels", L"Czarne skrzyd?a", L"Siyah Kanatlar");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_b005.ogg") {
						ss = "The False God of Causality";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d101.ogg") {
						ss = LL14(L"ダンジョン", L"Dungeon", L"Donjon", L"Sotterraneo", L"Mazmorra", L"??", L"迷宮", L"??????", L"Подземелье", L"Kerker", L"Masmorra", L"Kerker", L"Loch", L"Zindan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d201.ogg") {
						ss = LL14(L"道化師の誘い", L"Clown's Invitation", L"Invitation du bouffon", L"Invito del clown", L"Invitacion del payaso", L"??? ??", L"小丑的引誘", L"???? ??????", L"Приглашение клоуна", L"Einladung des Clowns", L"Convite do palhaco", L"Uitnodiging van de clown", L"Zaproszenie b?azna", L"Palyaconun Daveti");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d301.ogg") {
						ss = LL14(L"地下遺跡", L"Underground Ruins", L"Ruines souterraines", L"Rovine sotterranee", L"Ruinas subterraneas", L"?? ??", L"地下遺跡", L"????? ??? ?????", L"Подземные руины", L"Unterirdische Ruinen", L"Ruinas subterraneas", L"Ondergrondse ruines", L"Podziemne ruiny", L"Yeralt? Harabeleri");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d401.ogg") {
						ss = LL14(L"導きの塔〜エルディールにくちづけを", L"Tower of Guidance -Kiss for Eldeel-", L"Tour de guidance -Un baiser pour Eldeel-", L"Torre della guida -Un bacio per Eldeel-", L"Torre de guia -Un beso para Eldeel-", L"??? ? ~ Eldeel?? ????", L"引導之塔〜給 Eldeel 的吻", L"??? ??????? - ???? ?? Eldeel", L"Башня наставления -Поцелуй для Eldeel-", L"Turm der Fuhrung -Kuss fur Eldeel-", L"Torre de Orientacao -Beijo para Eldeel-", L"Toren van begeleiding -Kus voor Eldeel-", L"Wie?a przewodnictwa -Poca?unek dla Eldeel-", L"Rehberlik Kulesi -Eldeel icin Bir Opucuk-");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d501.ogg") {
						ss = LL14(L"失われし仮面を求めて", L"Seeking the Lost Mask", L"A la recherche du masque perdu", L"Alla ricerca della maschera perduta", L"Buscando la mascara perdida", L"???? ??? ???", L"尋找失落的面具", L"????? ?? ?????? ???????", L"В поисках утраченной маски", L"Auf der Suche nach der verlorenen Maske", L"Em busca da mascara perdida", L"Op zoek naar het verloren masker", L"W poszukiwaniu zagubionej maski", L"Kay?p Maskenin Pe?inde");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d701.ogg") {
						ss = LL14(L"イリス", L"Iris", L"Iris", L"Iris", L"Iris", L"???", L"伊莉絲", L"Iris", L"Iris", L"Iris", L"Iris", L"Iris", L"Iris", L"Iris");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d702.ogg") {
						ss = "yc_d702";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_d703.ogg") {
						ss = LL14(L"聖域", L"Sanctuary", L"Sanctuaire", L"Santuario", L"Santuario", L"??", L"聖域", L"????", L"Святилище", L"Heiligtum", L"Santuario", L"Heiligdom", L"Sanktuarium", L"Kutsal Alan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e001.ogg") {
						ss = LL14(L"賢者", L"Sage", L"Sage", L"Saggio", L"Sabio", L"??", L"賢者", L"????", L"Мудрец", L"Weiser", L"Sabio", L"Wijze", L"M?drzec", L"Bilge");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e002.ogg") {
						ss = LL14(L"復活の儀式", L"Resurrection Ceremony", L"Ceremonie de resurrection", L"Cerimonia di resurrezione", L"Ceremonia de resurreccion", L"??? ??", L"復活的儀式", L"????? ???????", L"Церемония воскрешения", L"Auferstehungszeremonie", L"Cerimonia de ressurreicao", L"Opstandingsceremonie", L"Ceremonia wskrzeszenia", L"Dirili? Toreni");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e003.ogg") {
						ss = LL14(L"レファンス", L"Refance", L"Refance", L"Refance", L"Refance", L"???", L"雷凡斯", L"Refance", L"Refance", L"Refance", L"Refance", L"Refance", L"Refance", L"Refance");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e004.ogg") {
						ss = LL14(L"涙の少年剣士", L"Young Swordsman in Tears", L"Jeune epeiste en larmes", L"Giovane spadaccino in lacrime", L"Joven espadachin en lagrimas", L"??? ?? ??", L"流?的少年劍士", L"???? ??? ????", L"Юный мечник в слезах", L"Junger Schwertkampfer in Tranen", L"Jovem espadachim em lagrimas", L"Jonge zwaardvechter in tranen", L"M?ody szermierz we ?zach", L"Gozu Ya?l? Genc K?l?c Ustas?");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e005.ogg") {
						ss = LL14(L"エルディール", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"??", L"艾爾迪爾", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel", L"Eldeel");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e006.ogg") {
						ss = LL14(L"ロムン帝国 -嗚呼レオ団長-", L"Romun Empire -Alas Captain Leo-", L"Empire de Romun -Helas Capitaine Leo-", L"Impero di Romun -Ahime Capitano Leo-", L"Imperio de Romun -Ay, Capitan Leo-", L"Romun ?? ~?? ?? ??~", L"Romun 帝國 -嗚呼里歐團長-", L"?????????? Romun -?? ????? ???? ?????? Leo-", L"Империя Romun -Увы, капитан Leo-", L"Romun Reich -Ach, Kapitan Leo-", L"Imperio de Romun -Ai, Capitao Leo-", L"Romun-rijk -Helaas Kapitein Leo-", L"Imperium Romun -Ach, Kapitanie Leo-", L"Romun ?mparatorlu?u -Vah Yuzba?? Leo-");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e008.ogg") {
						ss = "yc_e008";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_e010.ogg") {
						ss = LL14(L"冒険家、誕生", L"Birth of an Adventurer", L"Naissance d'un aventurier", L"Nascita di un avventuriero", L"Nacimiento de un aventurero", L"??? ??", L"冒險家誕生", L"????? ?????", L"Рождение искателя приключений", L"Geburt eines Abenteurers", L"Nascimento de um aventureiro", L"Geboorte van een avonturier", L"Narodziny poszukiwacza przygod", L"Bir Macerac?n?n Do?u?u");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f101.ogg") {
						ss = LL14(L"燃ゆる剣", L"Burning Sword", L"Epee brulante", L"Spada ardente", L"Espada ardiente", L"??? ?", L"燃燒之劍", L"????? ???????", L"Пылающий меч", L"Brennendes Schwert", L"Espada flamejante", L"Brandend zwaard", L"P?on?cy miecz", L"Yanan K?l?c");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f201.ogg") {
						ss = LL14(L"セルセタの樹海", L"Forest of Celceta", L"Foret de Celceta", L"Foresta di Celceta", L"Bosque de Celceta", L"Celceta? ??", L"Celceta 的樹海", L"???? Celceta", L"Лес Celceta", L"Wald von Celceta", L"Floresta de Celceta", L"Woud van Celceta", L"Las Celceta", L"Celceta Orman?");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f301.ogg") {
						ss = LL14(L"クレーター", L"Crater", L"Cratere", L"Cratere", L"Crater", L"????", L"火山口", L"???? ???????", L"Кратер", L"Krater", L"Cratera", L"Krater", L"Krater", L"Krater");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f401.ogg") {
						ss = "THE DAWN OF YS";
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f501.ogg") {
						ss = LL14(L"暁の森", L"Forest of Dawn", L"Foret de l'aube", L"Foresta dell'alba", L"Bosque del alba", L"??? ?", L"曉之森", L"???? ?????", L"Лес рассвета", L"Wald der Dammerung", L"Floresta da aurora", L"Woud van de dageraad", L"Las ?witu", L"?afak Orman?");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f601.ogg") {
						ss = LL14(L"一陣の風", L"Gust of Wind", L"Une rafale de vent", L"Raffica di vento", L"Rafaga de viento", L"? ?? ??", L"一陣風", L"????? ?? ??????", L"Порыв ветра", L"Windstos", L"Rajada de vento", L"Windvlaag", L"Podmuch wiatru", L"Bir Ruzgar Esintisi");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f701.ogg") {
						ss = LL14(L"神代の地", L"Land of the Gods", L"Terre des dieux", L"Terra degli dei", L"Tierra de los dioses", L"?? ??? ?", L"神代之地", L"??? ??????", L"Земля богов", L"Land der Gotter", L"Terra dos deuses", L"Land van de goden", L"Kraina bogow", L"Tanr?lar?n Diyar?");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f801.ogg") {
						ss = LL14(L"真実への序曲", L"Overture to Truth", L"Ouverture vers la verite", L"Ouverture alla verita", L"Obertura a la verdad", L"??? ?? ??", L"通往真実的序曲", L"????? ???????", L"Увертюра к истине", L"Ouverture zur Wahrheit", L"Preludio para a verdade", L"Ouverture naar de waarheid", L"Uwertura do prawdy", L"Gerce?e Uvertur");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_f901.ogg") {
						ss = LL14(L"雨上がりの朝に", L"Morning After the Rain", L"Le matin apres la pluie", L"Mattina dopo la pioggia", L"Manana despues de la lluvia", L"? ? ???", L"雨過天晴的早晨", L"?????? ??? ?????", L"Утро после дождя", L"Morgen nach dem Regen", L"Manha apos a chuva", L"Ochtend na de regen", L"Poranek po deszczu", L"Ya?mur Sonras? Sabah");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_over.ogg") {
						ss = LL14(L"ゲームオーバー", L"Game Over", L"Fin de partie", L"Fine del gioco", L"Juego terminado", L"?? ??", L"遊戲結束", L"????? ??????", L"Конец игры", L"Spiel vorbei", L"Fim de jogo", L"Game over", L"Koniec gry", L"Oyun Bitti");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t101.ogg") {
						ss = LL14(L"辺境都市《キャスナン》", L"Frontier City Casnan", L"Ville frontaliere Casnan", L"Citta di confine Casnan", L"Ciudad fronteriza Casnan", L"?? ?? Casnan", L"邊境都市 Casnan", L"????? Casnan ????????", L"Пограничный город Casnan", L"Grenzstadt Casnan", L"Cidade fronteirica Casnan", L"Grensstad Casnan", L"Graniczne miasto Casnan", L"S?n?r ?ehri Casnan");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t201.ogg") {
						ss = LL14(L"優しくなりたい", L"I Want to Be Kind", L"Je veux etre gentil", L"Voglio essere gentile", L"Quiero ser amable", L"????? ??", L"想要變得?柔", L"???? ?? ???? ?????", L"Я хочу быть добрым", L"Ich mochte gutig sein", L"Eu quero ser gentil", L"Ik wil vriendelijk zijn", L"Chc? by? mi?y", L"Nazik Olmak ?stiyorum");
						_tcscpy(p.name, ss);
					}
					if (ss == "yc_t301.ogg") {
						ss = LL14(L"古代の伝承", L"Ancient Legend", L"Legende ancienne", L"Antica leggenda", L"Leyenda antigua", L"??? ??", L"古代的傳承", L"???????? ???????", L"Древняя легенда", L"Alte Legende", L"Lenda antiga", L"Oude legende", L"Staro?ytna legenda", L"Kadim Efsane");
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
						ss = LL14(L"新たな時代のステージへ", L"To the Stage of a New Era", L"Vers l'etape d'une nouvelle ere", L"Verso il palcoscenico di una nuova era", L"Hacia el escenario de una nueva era", L"??? ??? ?????", L"邁向新時代的舞台", L"??? ????? ??? ????", L"На сцену новой эры", L"Auf die Buhne einer neuen Ara", L"Para o palco de uma nova era", L"Naar het podium van een nieuw tijdperk", L"Do etapu nowej ery", L"Yeni Bir Ca??n Sahnesine");
						_tcscpy(p.name, ss);
					}

					//zero 
					CString ss;
					ss = fname.Right(fname.GetLength() - fname.ReverseFind(L'\\') - 1);
					sss = fname.Left(fname.ReverseFind('\\'));
					int fg = 0;
					CFile ffff;
					if (ffff.Open(sss + L"\\..\\text\\t_bgm._dt", CFile::modeRead | CFile::shareDenyWrite)) { fg = 1; ffff.Close(); }
					CString zero = savedata.zero;
					if(zero != L"") if (ffff.Open(savedata.zero, CFile::modeRead | CFile::shareDenyWrite)) { fg = 1; ffff.Close(); }
					CString a;
					if (ss.Mid(0, 3) == L"ed7" && fg == 1) {
						switch (_ttoi(ss.Mid(2, 4))) {
						case 7001:
							a = LL14(L"零の軌跡", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"??? ??", L"零之軌跡", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero", L"Trails from Zero");
							break;
						case 7002:
							a = L"way of live -Opening Version-";
							break;
						case 7003:
							a = LL14(L"新しき日々〜予兆", L"New Days -Omen-", L"Jours nouveaux -Presage-", L"Nuovi giorni -Presagio-", L"Nuevos dias -Presagio-", L"??? ?? ~??", L"嶄新的日子〜預兆", L"???? ????? - ????", L"Новые дни -Предзнаменование-", L"Neue Tage -Vorbote-", L"Novos dias -Augurio-", L"Nieuwe dagen -Voorteken-", L"Nowe dni -Zwiastun-", L"Yeni Gunler -Kehanet-");
							break;
						case 7005:
							a = LL14(L"想い破れて・・・", L"Broken Heart...", L"C?ur brise...", L"Cuore infranto...", L"Corazon roto...", L"??? ??...", L"心碎・・・", L"??? ?????...", L"Разбитое сердце...", L"Gebrochenes Herz...", L"Coracao partido...", L"Gebroken hart...", L"Z?amane serce...", L"K?r?k Kalp...");
							break;
						case 7052:
							a = LL14(L"碧い軌跡 -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"?? ?? -Opening size-", L"碧之軌跡 -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-", L"Azure Arbitrator -Opening size-");
							break;
						case 7053:
							a = LL14(L"それでも僕らは。", L"Yet We're Still Here.", L"Pourtant, nous sommes la.", L"Eppure siamo ancora qui.", L"Sin embargo, seguimos aqui.", L"???? ????.", L"即便如此我們依舊。", L"??? ???? ?? ???? ???.", L"И все же мы здесь.", L"Doch wir sind noch hier.", L"Ainda assim, estamos aqui.", L"Toch zijn we er nog.", L"A jednak wci?? tu jeste?my.", L"Yine de Buraday?z.");
							break;
						case 7100:
							a = LL14(L"街角の風景", L"Street Corner Scenery", L"Paysage au coin de la rue", L"Paesaggio all'angolo della strada", L"Paisaje de esquina", L"???? ??", L"街角風景", L"???? ????? ??????", L"Пейзаж на углу улицы", L"Straseneckenszenerie", L"Cenario de esquina", L"Straathoektafereel", L"Krajobraz na rogu ulicy", L"Sokak Ko?esi Manzaras?");
							break;
						case 7101:
							a = LL14(L"明日は明日の風が吹く", L"Tomorrow the Wind Will Blow", L"Demain, le vent soufflera", L"Domani soffiera il vento", L"Manana soplara el viento", L"??? ??? ??? ??", L"明天吹明天的風", L"???? ???? ??????", L"Завтра подует ветер", L"Morgen weht der Wind", L"Amanha o vento soprara", L"Morgen waait de wind", L"Jutro zawieje wiatr", L"Yar?n Ruzgar Esecek");
							break;
						case 7102:
							a = LL14(L"クロスベルの午後", L"Afternoon in Crossbell", L"Apres-midi a Crossbell", L"Pomeriggio a Crossbell", L"Tarde en Crossbell", L"Crossbell? ??", L"Crossbell 的午後", L"??? ??? ????? ?? Crossbell", L"Полдень в Crossbell", L"Nachmittag in Crossbell", L"Tarde em Crossbell", L"Middag in Crossbell", L"Popo?udnie w Crossbell", L"Crossbell'da O?leden Sonra");
							break;
						case 7103:
							a = L"During Mission Accomplishment";
							break;
						case 7104:
							a = LL14(L"創立記念祭", L"Founding Festival", L"Festival de la fondation", L"Festival della fondazione", L"Festival de la fundacion", L"?? ???", L"創立紀念祭", L"?????? ???????", L"Фестиваль основания", L"Grundungsfest", L"Festival de fundacao", L"Oprichtingsfestival", L"Festiwal za?o?ycielski", L"Kurulu? Festivali");
							break;
						case 7105:
							a = LL14(L"降水確率10%", L"10% Chance of Rain", L"10% de chances de pluie", L"10% di probabilita di pioggia", L"10% de probabilidad de lluvia", L"???? 10%", L"降雨機率10%", L"?????? ???? ??????? 10%", L"10% вероятность дождя", L"10% Regenwahrscheinlichkeit", L"10% de chance de chuva", L"10% kans op regen", L"10% szans na deszcz", L"10% Ya?mur Olas?l???");
							break;
						case 7106:
							a = LL14(L"風船と紙吹雪", L"Balloons and Confetti", L"Ballons et confettis", L"Palloncini e coriandoli", L"Globos y confeti", L"??? ?????", L"氣球與五彩碎紙", L"??????? ??????? ???", L"Воздушные шары и конфетти", L"Luftballons und Konfetti", L"Baloes e confetes", L"Ballonnen en confetti", L"Balony i konfetti", L"Balonlar ve Konfetiler");
							break;
						case 7110:
							a = LL14(L"特務支援課", L"Special Support Section", L"Section de soutien special", L"Sezione di supporto speciale", L"Seccion de apoyo especial", L"?????", L"特務支援課", L"??? ????? ?????", L"Секция специальной поддержки", L"Spezielle Unterstutzungsabteilung", L"Secao de Apoio Especial", L"Speciale ondersteuningssectie", L"Specjalna Sekcja Wsparcia", L"Ozel Destek Bolumu");
							break;
						case 7111:
							a = LL14(L"C.S.P.D. -クロスベル警察", L"C.S.P.D. -Crossbell Police", L"C.S.P.D. -Police de Crossbell", L"C.S.P.D. -Polizia di Crossbell", L"C.S.P.D. -Policia de Crossbell", L"C.S.P.D. -Crossbell ??", L"C.S.P.D. -Crossbell 警察", L"C.S.P.D. -???? Crossbell", L"C.S.P.D. -Полиция Crossbell", L"C.S.P.D. -Polizei von Crossbell", L"C.S.P.D. -Policia de Crossbell", L"C.S.P.D. -Politie van Crossbell", L"C.S.P.D. -Policja Crossbell", L"C.S.P.D. -Crossbell Polisi");
							break;
						case 7113:
							a = L"Arc-en-ciel";
							break;
						case 7114:
							a = LL14(L"黒月貿易公司", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue ????", L"黑月貿易公司", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company", L"Heiyue Trading Company");
							break;
						case 7116:
							a = L"IGNIS";
							break;
						case 7117:
							a = L"TRINITY";
							break;
						case 7120:
							a = LL14(L"アルモリカ村", L"Armorica Village", L"Village d'Armorica", L"Villaggio di Armorica", L"Aldea Armorica", L"Armorica ??", L"阿爾摩利?村", L"???? Armorica", L"Деревня Armorica", L"Dorf Armorica", L"Vila Armorica", L"Dorp Armorica", L"Wioska Armorica", L"Armorica Koyu");
							break;
						case 7121:
							a = LL14(L"鉱山町マインツ", L"Mines Town Mainz", L"Ville miniere Mainz", L"Citta mineraria Mainz", L"Ciudad minera Mainz", L"?? ?? Mainz", L"礦山鎮 Mainz", L"???? ??????? Mainz", L"Шахтерский городок Mainz", L"Bergbaustadt Mainz", L"Cidade mineira Mainz", L"Mijnstad Mainz", L"Gornicze miasto Mainz", L"Maden Kasabas? Mainz");
							break;
						case 7122:
							a = L"Killing Bear";
							break;
						case 7123:
							a = LL14(L"聖ウルスラ医科大学", L"St. Ursula Medical College", L"College medical Ste Ursule", L"Collegio medico S. Orsola", L"Colegio Medico Sta. Ursula", L"? ???? ????", L"聖烏爾蘇拉醫科大學", L"???? ???? ??????? ??????", L"Медицинский колледж Св. Урсулы", L"Medizinische Hochschule St. Ursula", L"Faculdade de Medicina Sta. Ursula", L"Medisch College St. Ursula", L"Kolegium Medyczne ?w. Urszuli", L"Aziz Ursula T?p Koleji");
							break;
						case 7124:
							a = LL14(L"クロスベル大聖堂", L"Crossbell Cathedral", L"Cathedrale de Crossbell", L"Cattedrale di Crossbell", L"Catedral de Crossbell", L"Crossbell ???", L"Crossbell 大聖堂", L"????????? Crossbell", L"Собор Crossbell", L"Kathedrale von Crossbell", L"Catedral de Crossbell", L"Kathedraal van Crossbell", L"Katedra w Crossbell", L"Crossbell Katedrali");
							break;
						case 7125:
							a = LL14(L"黒の競売会", L"Black Auction", L"Encheres noires", L"Asta nera", L"Subasta negra", L"?? ???", L"黑色拍賣會", L"?????? ??????", L"Черный аукцион", L"Schwarze Auktion", L"Leilao negro", L"Zwarte veiling", L"Czarna aukcja", L"Siyah Muzayede");
							break;
						case 7126:
							a = LL14(L"大国にはさまれて", L"Caught Between Nations", L"Pris entre les nations", L"Incastrato tra le nazioni", L"Atrapado entre naciones", L"?? ??? ???", L"夾在大国之間", L"??? ??? ?????", L"Зажатый между странами", L"Gefangen zwischen den Nationen", L"Preso entre nacoes", L"Gevangen tussen de naties", L"Uwi?ziony mi?dzy narodami", L"Uluslar Aras?nda S?k??m??");
							break;
						case 7150:
							a = LL14(L"新たなる日常", L"New Daily Life", L"Nouvelle vie quotidienne", L"Nuova vita quotidiana", L"Nueva vida cotidiana", L"??? ??", L"嶄新的日常", L"???? ????? ?????", L"Новая повседневная жизнь", L"Neuer Alltag", L"Nova vida cotidiana", L"Nieuw dagelijks leven", L"Nowe ?ycie codzienne", L"Yeni Gunluk Ya?am");
							break;
						case 7151:
							a = LL14(L"動き始めた事態", L"Events in Motion", L"Evenements en mouvement", L"Eventi in movimento", L"Eventos en movimiento", L"???? ??? ??", L"開始動作的事態", L"??????? ?? ????", L"События в движении", L"Ereignisse in Bewegung", L"Eventos em movimento", L"Gebeurtenissen in beweging", L"Wydarzenia w toku", L"Harekete Gecen Olaylar");
							break;
						case 7160:
							a = LL14(L"ミシュラムワンダーランド", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland", L"Mishyram Wonderland");
							break;
						case 7161:
							a = LL14(L"束の間の休息", L"Brief Respite", L"Bref repit", L"Breve tregua", L"Breve respiro", L"?? ??? ??", L"短暫的休息", L"??????? ?????", L"Краткая передышка", L"Kurze Atempause", L"Breve descanso", L"Korte adempauze", L"Krotkie wytchnienie", L"K?sa Bir Mola");
							break;
						case 7162:
							a = LL14(L"ささやかな晩餐", L"Simple Dinner", L"Diner simple", L"Cena semplice", L"Cena sencilla", L"??? ??", L"簡單的?餐", L"???? ????", L"Простой ужин", L"Einfaches Abendessen", L"Jantar simples", L"Eenvoudig diner", L"Skromna kolacja", L"Sade Bir Ak?am Yeme?i");
							break;
						case 7200:
							a = LL14(L"水と草木と青い空", L"Water, Trees and Blue Sky", L"Eau, arbres et ciel bleu", L"Acqua, alberi e cielo blu", L"Agua, arboles y cielo azul", L"?? ??? ?? ??", L"水、草木與藍天", L"??? ?????? ????? ?????", L"Вода, деревья и синее небо", L"Wasser, Baume und blauer Himmel", L"Agua, arvores e ceu azul", L"Water, bomen en blauwe lucht", L"Woda, drzewa i b??kitne niebo", L"Su, A?aclar ve Mavi Gokyuzu");
							break;
						case 7201:
							a = LL14(L"片手にはレモネード", L"Lemonade in One Hand", L"Une limonade a la main", L"Una limonata in mano", L"Limonada en una mano", L"? ??? ?????", L"手裡拿著檸檬水", L"???????? ?? ?? ?????", L"С лимонадом в одной руке", L"Limonade in einer Hand", L"Limonada em uma mao", L"Limonade in een hand", L"Lemoniada w jednej r?ce", L"Bir Elde Limonata");
							break;
						case 7202:
							a = LL14(L"木霊の道", L"Path of Echoes", L"Chemin des echos", L"Sentiero degli echi", L"Senda de los ecos", L"???? ?", L"木靈之路", L"???? ???????", L"Путь эха", L"Pfad des Echos", L"Caminho dos ecos", L"Pad van echo's", L"?cie?ka ech", L"Yank?lar?n Yolu");
							break;
						case 7203:
							a = LL14(L"古の鼓動", L"Ancient Pulse", L"Pouls ancien", L"Battito antico", L"Pulso antiguo", L"??? ??", L"古之鼓動", L"??? ????", L"Древний пульс", L"Uralter Puls", L"Pulso antigo", L"Eeuwenoude hartslag", L"Staro?ytne t?tno", L"Kadim Nab?z");
							break;
						case 7204:
							a = L"On The Green Road";
							break;
						case 7205:
							a = LL14(L"鉄橋を越えて", L"Crossing the Iron Bridge", L"Traverser le pont de fer", L"Attraversando il ponte di ferro", L"Cruzando el puente de hierro", L"??? ???", L"越過鐵橋", L"???? ????? ???????", L"Пересекая железный мост", L"Uber die Eisenbrucke", L"Atravessando a ponte de ferro", L"De ijzeren brug oversteken", L"Przez ?elazny most", L"Demir Kopruyu Gecerken");
							break;
						case 7250:
							a = LL14(L"木洩れ日の中の静寂", L"Tranquility in the Dappled Light", L"Tranquillite dans la lumiere tamisee", L"Tranquillita nella luce filtrata", L"Tranquilidad en la luz moteada", L"??? ??? ??? ?? ?? ??", L"林間陽光中的寧靜", L"??????? ?? ????? ??????", L"Спокойствие в бликах света", L"Ruhe im gefleckten Licht", L"Tranquilidade na luz salpicada", L"Rust in het gespikkelde licht", L"Spokoj w rozproszonym ?wietle", L"Suzulen I??k Alt?ndaki Huzur");
							break;
						case 7251:
							a = LL14(L"偽りの楽土を越えて", L"Beyond the False Paradise", L"Au-dela du faux paradis", L"Oltre il falso paradiso", L"Mas alla del falso paraiso", L"??? ??? ???", L"越過?偽的樂土", L"?? ???? ????? ???????", L"За пределами ложного рая", L"Jenseits des falschen Paradieses", L"Alem do falso paraiso", L"Voorbij het valse paradijs", L"Poza fa?szywym rajem", L"Sahte Cennetin Otesinde");
							break;
						case 7300:
							a = LL14(L"ジオフロント", L"Geofront", L"Geofront", L"Geofront", L"Geofront", L"?????", L"地下空間", L"Geofront", L"Геофронт", L"Geofront", L"Geofront", L"Geofront", L"Geofront", L"Geofront");
							break;
						case 7301:
							a = LL14(L"七耀の煌き", L"Septium Radiance", L"Eclat de Septium", L"Splendore del Septium", L"Resplandor de Septium", L"??? ??", L"七耀之輝", L"???? Septium", L"Сияние Септиума", L"Septium-Glanz", L"Resplendor de Septium", L"Septium-glans", L"Blask Septium", L"Septium Par?lt?s?");
							break;
						case 7302:
							a = LL14(L"ルバーチェ商会", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache ??", L"魯巴徹商會", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company", L"Revache Trading Company");
							break;
						case 7303:
							a = LL14(L"鳴るはずのない鐘", L"The Bell That Shouldn't Ring", L"La cloche qui ne devrait pas sonner", L"La campana que non dovrebbe suonare", L"La campana que no deberia sonar", L"?? ? ?? ?", L"不該響起的鐘", L"????? ???? ?? ????? ?? ???", L"Колокол, который не должен звонить", L"Die Glocke, die nicht lauten sollte", L"O sino que nao deveria tocar", L"De klok die niet mag luiden", L"Dzwon, ktory nie powinien bi?", L"Calmamas? Gereken Can");
							break;
						case 7304:
							a = LL14(L"忘れられし幻夢の狭間", L"Forgotten Phantasmal Gap", L"L'ecart phantasmatique oublie", L"Divario fantasmatico dimenticato", L"Brecha fantasmal olvidada", L"??? ??? ??", L"被遺忘的幻夢狹間", L"?????? ???????? ???????", L"Забытый призрачный разрыв", L"Vergessener phantasmagorischer Spalt", L"Fenda fantasmal esquecida", L"Vergeten fantoomkloof", L"Zapomniana fantastyczna szczelina", L"Unutulmu? Hayali Bo?luk");
							break;
						case 7305:
							a = L"A Light Illuminating The Depths";
							break;
						case 7350:
							a = LL14(L"Dの残影", L"D's Shadow", L"L'ombre de D", L"L'ombra di D", L"La sombra de D", L"D? ??", L"D的殘影", L"?? D", L"Тень D", L"Ds Schatten", L"Sombra de D", L"D's schaduw", L"Cie? D", L"D'nin Golgesi");
							break;
						case 7351:
							a = LL14(L"異変の兆し", L"Omen of Change", L"Presage de changement", L"Presagio di cambiamento", L"Presagio de cambio", L"??? ??", L"異變的?兆", L"???? ???????", L"Предзнаменование перемен", L"Vorbote der Veranderung", L"Augurio de mudanca", L"Voorteken van verandering", L"Zwiastun zmian", L"De?i?im Kehaneti");
							break;
						case 7352:
							a = L"Mystic Core";
							break;
						case 7353:
							a = LL14(L"最果ての樹", L"Tree at World's End", L"L'arbre au bout du monde", L"L'albero alla fine del mondo", L"Arbol del fin del mundo", L"??? ??", L"最果て之樹", L"???? ?? ????? ??????", L"Древо на краю света", L"Baum am Ende der Welt", L"Arvore no fim do mundo", L"Boom aan het einde van de wereld", L"Drzewo na ko?cu ?wiata", L"Dunyan?n Ucundaki A?ac");
							break;
						case 7354:
							a = LL14(L"暴魔の呼び声", L"Call of the Beast", L"L'appel de la bete", L"Il richiamo della bestia", L"La llamada de la bestia", L"??? ??", L"暴魔的呼喚", L"???? ?????", L"Зов зверя", L"Ruf der Bestie", L"O chamado da besta", L"Roep van het beest", L"Zew bestii", L"Canavar?n Ca?r?s?");
							break;
						case 7356:
							a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
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
							a = LL14(L"これが俺たちの力だ!", L"This Is Our Power!", L"C'est notre pouvoir!", L"Questo e il nostro potere!", L"!Este es nuestro poder!", L"??? ???? ???!", L"這就是我們的力量！", L"??? ?? ?????!", L"Это наша сила!", L"Das ist unsere Macht!", L"Este e o nosso poder!", L"Dit is onze kracht!", L"To jest nasza moc!", L"Bu Bizim Gucumuz!");
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
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
							break;
						case 7500:
							a = LL14(L"金の太陽、銀の月　-陽の熱情", L"Golden Sun, Silver Moon -Solar Passion-", L"Soleil d'or, lune d'argent -Passion solaire-", L"Sole d'oro, luna d'argento -Passione solare-", L"Sol de oro, luna de plata -Pasion solar-", L"?? ??, ?? ? -??? ??", L"金之太陽、銀之月 -陽之熱情", L"??? ?????? ??? ??? - ??? ????", L"Золотое солнце, серебряная луна -Солнечная страсть-", L"Goldene Sonne, silberner Mond -Solare Leidenschaft-", L"Sol dourado, lua prateada -Paixao solar-", L"Gouden zon, zilveren maan -Zonnepassie-", L"Z?ote s?o?ce, srebrny ksi??yc -S?oneczna pasja-", L"Alt?n Gune?, Gumu? Ay -Gune? Tutkusu-");
							break;
						case 7501:
							a = LL14(L"金の太陽、銀の月　-月の慕情", L"Golden Sun, Silver Moon -Lunar Affection-", L"Soleil d'or, lune d'argent -Affection lunaire-", L"Sole d'oro, luna d'argento -Affetto lunare-", L"Sol de oro, luna de plata -Afecto lunar-", L"?? ??, ?? ? -?? ??", L"金之太陽、銀之月 -月之慕情", L"??? ?????? ??? ??? - ????? ?????", L"Золотое солнце, серебряная луна -Лунная привязанность-", L"Goldene Sonne, silberner Mond -Lunare Zuneigung-", L"Sol dourado, lua prateada -Afeicao lunar-", L"Gouden zon, zilveren maan -Maangenegenheid-", L"Z?ote s?o?ce, srebrny ksi??yc -Ksi??ycowe uczucie-", L"Alt?n Gune?, Gumu? Ay -Ay ?efkati-");
							break;
						case 7502:
							a = LL14(L"金の太陽、銀の月　-童心", L"Golden Sun, Silver Moon -Innocence-", L"Soleil d'or, lune d'argent -Innocence-", L"Sole d'oro, luna d'argento -Innocenza-", L"Sol de oro, luna de plata -Inocencia-", L"?? ??, ?? ? -??", L"金之太陽、銀之月 -童心", L"??? ?????? ??? ??? - ?????", L"Золотое солнце, серебряная луна -Невинность-", L"Goldene Sonne, silberner Mond -Unschuld-", L"Sol dourado, lua prateada -Inocencia-", L"Gouden zon, zilveren maan -Onschuld-", L"Z?ote s?o?ce, srebrny ksi??yc -Niewinno??-", L"Alt?n Gune?, Gumu? Ay -Masumiyet-");
							break;
						case 7503:
							a = LL14(L"金の太陽、銀の月　-運命の刻", L"Golden Sun, Silver Moon -Hour of Fate-", L"Soleil d'or, lune d'argent -L'heure du destin-", L"Sole d'oro, luna d'argento -L'ora del destino-", L"Sol de oro, luna de plata -La hora del destino-", L"?? ??, ?? ? -??? ??", L"金之太陽、銀之月 -命運之刻", L"??? ?????? ??? ??? - ???? ?????", L"Золотое солнце, серебряная луна -Час судьбы-", L"Goldene Sonne, silberner Mond -Stunde des Schicksals-", L"Sol dourado, lua prateada -Hora do destino-", L"Gouden zon, zilveren maan -Uur van het lot-", L"Z?ote s?o?ce, srebrny ksi??yc -Godzina losu-", L"Alt?n Gune?, Gumu? Ay -Kader Saati-");
							break;
						case 7504:
							a = LL14(L"金の太陽、銀の月　-譲れぬ想い", L"Golden Sun, Silver Moon -Unyielding Feelings-", L"Soleil d'or, lune d'argent -Sentiments inebranlables-", L"Sole d'oro, luna d'argento -Sentimenti incrollabili-", L"Sol de oro, luna de plata -Sentimientos inquebrantables-", L"?? ??, ?? ? -??? ? ?? ??", L"金之太陽、銀之月 -不容讓?的思念", L"??? ?????? ??? ??? - ????? ?? ??????", L"Золотое солнце, серебряная луна -Непоколебимые чувства-", L"Goldene Sonne, silberner Mond -Unbeugsame Gefuhle-", L"Sol dourado, lua prateada -Sentimentos inabalaveis-", L"Gouden zon, zilveren maan -Onwrikbare gevoelens-", L"Z?ote s?o?ce, srebrny ksi??yc -Nieust?pliwe uczucia-", L"Alt?n Gune?, Gumu? Ay -Sars?lmaz Duygular-");
							break;
						case 7505:
							a = LL14(L"金の太陽、銀の月　-幾千の夜を越えて", L"Golden Sun, Silver Moon -Beyond Countless Nights-", L"Soleil d'or, lune d'argent -Au-dela d'innombrables nuits-", L"Sole d'oro, luna d'argento -Oltre innumerevoli notti-", L"Sol de oro, luna de plata -Mas alla de incontables noches-", L"?? ??, ?? ? -??? ?? ???", L"金之太陽、銀之月 -跨越數千個夜?", L"??? ?????? ??? ??? - ??? ????? ?? ????", L"Золотое солнце, серебряная луна -Сквозь бесчисленные ночи-", L"Goldene Sonne, silberner Mond -Jenseits zahlloser Nachte-", L"Sol dourado, lua prateada -Alem de incontaveis noites-", L"Gouden zon, zilveren maan -Voorbij talloze nachten-", L"Z?ote s?o?ce, srebrny ksi??yc -Poza niezliczone noce-", L"Alt?n Gune?, Gumu? Ay -Say?s?z Gecenin Otesinde-");
							break;
						case 7506:
							a = LL14(L"金の太陽、銀の月　-夜明け〜大団円", L"Golden Sun, Silver Moon -Dawn to Grand Finale-", L"Soleil d'or, lune d'argent -De l'aube au grand final-", L"Sole d'oro, luna d'argento -Dall'alba al gran finale-", L"Sol de oro, luna de plata -Del amanecer al gran final-", L"?? ??, ?? ? -?? ~ ???", L"金之太陽、銀之月 -黎明〜大團圓", L"??? ?????? ??? ??? - ?? ????? ??? ??????? ??????", L"Золотое солнце, серебряная луна -От рассвета до финала-", L"Goldene Sonne, silberner Mond -Morgengrauen bis zum Finale-", L"Sol dourado, lua prateada -Do amanhecer ao grande final-", L"Gouden zon, zilveren maan -Dageraad tot grote finale-", L"Z?ote s?o?ce, srebrny ksi??yc -Od ?witu do wielkiego fina?u-", L"Alt?n Gune?, Gumu? Ay -?afaktan Buyuk Finale-");
							break;
						case 7507:
							a = L"Intense Chase";
							break;
						case 7509:
							a = LL14(L"守りぬく意志", L"Unyielding Will", L"Volonte inebranlable", L"Volonta incrollabile", L"Voluntad inquebrantable", L"????? ??", L"守護到底的意志", L"????? ?? ??????", L"Непоколебимая воля", L"Unbeugsamer Wille", L"Vontade inabalavel", L"Onwrikbare wil", L"Nieugi?ta wola", L"Sars?lmaz ?rade");
							break;
						case 7510:
							a = LL14(L"叡智への誘い", L"Invitation to Wisdom", L"Invitation a la sagesse", L"Invito alla saggezza", L"Invitacion a la sabiduria", L"???? ??", L"智之引誘", L"???? ??????", L"Приглашение к мудрости", L"Einladung zur Weisheit", L"Convite a sabedoria", L"Uitnodiging tot wijsheid", L"Zaproszenie do m?dro?ci", L"Bilgeli?e Davet");
							break;
						case 7511:
							a = LL14(L"危地", L"Perilous Ground", L"Terrain perilleux", L"Terreno pericoloso", L"Terreno peligroso", L"??", L"危地", L"??? ?????? ????????", L"Опасная земля", L"Gefahrlicher Boden", L"Terreno perigoso", L"Gevaarlijk terrein", L"Niebezpieczny teren", L"Tehlikeli Bolge");
							break;
						case 7512:
							a = LL14(L"揺るぎない強さ", L"Unshakable Strength", L"Force inebranlable", L"Forza incrollabile", L"Fuerza inquebrantable", L"??? ?? ??", L"動搖不得的強大", L"??? ?? ??????", L"Непоколебимая сила", L"Unerschutterliche Starke", L"Forca inabalavel", L"Onwankelbare kracht", L"Niezachwiana si?a", L"Sars?lmaz Guc");
							break;
						case 7513:
							a = LL14(L"夜景に霞む星空", L"Starry Sky in the Night", L"Ciel etoile dans la nuit", L"Cielo stellato nella notte", L"Cielo estrellado en la noche", L"??? ??? ???", L"夜景中朦朧的星空", L"???? ????? ??????? ?? ?????", L"Звездное небо в ночи", L"Sternenhimmel in der Nacht", L"Ceu estrelado na noite", L"Sterrenhemel in de nacht", L"Gwie?dziste niebo noc?", L"Geceleyin Y?ld?zl? Gokyuzu");
							break;
						case 7514:
							a = LL14(L"いつかきっと", L"Someday", L"Un jour", L"Un giorno", L"Algun dia", L"??? ???", L"總有一天必定", L"????? ??", L"Когда-нибудь", L"Irgendwann", L"Algum dia", L"Ooit", L"Pewnego dnia", L"Bir Gun Mutlaka");
							break;
						case 7515:
							a = LL14(L"柔らかな心", L"Tender Heart", L"C?ur tendre", L"Cuore tenero", L"Corazon tierno", L"???? ??", L"柔軟的心", L"??? ????", L"Нежное сердце", L"Zartes Herz", L"Coracao terno", L"Zachtmoedig hart", L"Czu?e serce", L"Yumu?ak Kalp");
							break;
						case 7516:
							a = LL14(L"点と線", L"Dots and Lines", L"Points et lignes", L"Punti e linee", L"Puntos y lineas", L"?? ?", L"點與線", L"???? ?????", L"Точки и линии", L"Punkte und Linien", L"Pontos e linhas", L"Punten en lijnen", L"Punkty i linie", L"Noktalar ve Cizgiler");
							break;
						case 7517:
							a = LL14(L"一触即発", L"Imminent Crisis", L"Crise imminente", L"Crisi imminente", L"Crisis inminente", L"????", L"一觸即發", L"???? ?????", L"Неизбежный кризис", L"Drohende Krise", L"Crise iminente", L"Dreigende crisis", L"Bliska kryzysu", L"An Meselesi");
							break;
						case 7518:
							a = L"Foolish Gig";
							break;
						case 7519:
							a = LL14(L"リベールからの風", L"Wind from Liberl", L"Vent de Liberl", L"Vento da Liberl", L"Viento de Liberl", L"Liberl???? ??", L"來自 Liberl 的風", L"???? ?? Liberl", L"Ветер из Liberl", L"Wind aus Liberl", L"Vento de Liberl", L"Wind uit Liberl", L"Wiatr z Liberl", L"Liberl'den Gelen Ruzgar");
							break;
						case 7520:
							a = LL14(L"とどいた想い", L"Feelings Delivered", L"Sentiments livres", L"Sentimenti consegnati", L"Sentimientos entregados", L"?? ??", L"傳達到的思念", L"????? ????", L"Доставленные чувства", L"Angekommene Gefuhle", L"Sentimentos entregues", L"Bereikte gevoelens", L"Dostarczone uczucia", L"Ula?an Duygular");
							break;
						case 7521:
							a = L"Underground Kids";
							break;
						case 7522:
							a = L"Terminal Room";
							break;
						case 7523:
							a = LL14(L"響きあう心", L"Resonating Hearts", L"C?urs resonnants", L"Cuori risonanti", L"Corazones resonantes", L"???? ??", L"共鳴之心", L"???? ????", L"Резонирующие сердца", L"Resonierende Herzen", L"Coracoes ressonantes", L"Resonerende harten", L"Rezonuj?ce serca", L"Yank?lanan Kalpler");
							break;
						case 7524:
							a = L"Limit Break";
							break;
						case 7525:
							a = LL14(L"パラダイスミ☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"Paradigm☆", L"????☆", L"典範☆", L"?????☆", L"Парадигма☆", L"Paradigma☆", L"Paradigma☆", L"Paradigma☆", L"Paradygmat☆", L"Paradigma☆");
							break;
						case 7526:
							a = L"Gnosis";
							break;
						case 7527:
							a = L"Get Over The Barrier! -Roaring Version-";
							break;
						case 7528:
							a = LL14(L"それぞれの明日", L"Our Tomorrows", L"Nos demains", L"I nostri domani", L"Nuestros mananas", L"??? ??", L"各自的明天", L"????", L"Наши завтра", L"Unsere Morgen", L"Nossos amanhas", L"Onze morgens", L"Nasze jutra", L"Her Birimizin Yar?n?");
							break;
						case 7529:
							a = LL14(L"効果音楽1", L"Sound Effect Music 1", L"Musique d'effet 1", L"Musica effetto 1", L"Musica de efecto 1", L"???? 1", L"效果音樂 1", L"?????? ????? 1", L"Музыка эффекта 1", L"Effektmusik 1", L"Musica de efeito 1", L"Effectmuziek 1", L"Muzyka efektowa 1", L"Efekt Muzi?i 1");
							break;
						case 7530:
							a = LL14(L"効果音楽2", L"Sound Effect Music 2", L"Musique d'effet 2", L"Musica effetto 2", L"Musica de efecto 2", L"???? 2", L"效果音樂 2", L"?????? ????? 2", L"Музыка эффекта 2", L"Effektmusik 2", L"Musica de efeito 2", L"Effectmuziek 2", L"Muzyka efektowa 2", L"Efekt Muzi?i 2");
							break;
						case 7531:
							a = LL14(L"効果音楽3", L"Sound Effect Music 3", L"Musique d'effet 3", L"Musica effetto 3", L"Musica de efecto 3", L"???? 3", L"效果音樂 3", L"?????? ????? 3", L"Музыка эффекта 3", L"Effektmusik 3", L"Musica de efeito 3", L"Effectmuziek 3", L"Muzyka efektowa 3", L"Efekt Muzi?i 3");
							break;
						case 7532:
							a = LL14(L"効果音楽4", L"Sound Effect Music 4", L"Musique d'effet 4", L"Musica effetto 4", L"Musica de efecto 4", L"???? 4", L"效果音樂 4", L"?????? ????? 4", L"Музыка эффекта 4", L"Effektmusik 4", L"Musica de efeito 4", L"Effectmuziek 4", L"Muzyka efektowa 4", L"Efekt Muzi?i 4");
							break;
						case 7533:
							a = LL14(L"踏み出す勇気", L"Courage to Step Forward", L"Courage d'avancer", L"Coraggio di farsi avanti", L"Coraje para dar un paso adelante", L"?? ???? ??", L"踏出一?的勇氣", L"??????? ?????? ??????", L"Смелость сделать шаг вперед", L"Mut zum Vorwartsschritt", L"Coragem para dar um passo a frente", L"Moed om vooruit te stappen", L"Odwaga, by i?? naprzod", L"?leri Ad?m Atma Cesareti");
							break;
						case 7534:
							a = LL14(L"その背中を見つめて", L"Watching Your Back", L"Regarder ton dos", L"Guardando le tue spalle", L"Mirando tu espalda", L"? ??? ????", L"凝視著那背影", L"?????? ????", L"Глядя тебе в спину", L"Deinen Rucken im Blick", L"Olhando para as suas costas", L"Je rug in de gaten houden", L"Patrz?c na twoje plecy", L"S?rt?n? ?zlerken");
							break;
						case 7540:
						case 7541:
						case 7542:
						case 7543:
						case 7544:
							a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
							break;
						case 7550:
							a = LL14(L"オルキスタワー", L"Orchis Tower", L"Tour Orchis", L"Torre Orchis", L"Torre Orchis", L"Orchis Tower", L"Orchis Tower", L"??? ????????", L"Башня Орхидея", L"Orchis-Turm", L"Torre Orchis", L"Orchis-toren", L"Wie?a Orchis", L"Orchis Kulesi");
							break;
						case 7551:
							a = L"Catastrophe";
							break;
						case 7552:
							a = LL14(L"碧き雫", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"?? ???", L"碧之雫", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator", L"Azure Arbitrator");
							break;
						case 7553:
							a = LL14(L"神機降臨", L"Divine Mechanoid Descent", L"Descente du mecanoide divin", L"Discesa del meccanoide divino", L"Descenso del mechon divino", L"????", L"神機降臨", L"???? ???????? ????", L"Схождение божественного механоида", L"Abstieg des gottlichen Mechanoids", L"Descida do mecanoide divino", L"Neerdaling van de goddelijke mechanoide", L"Zst?pienie boskiego mechanoida", L"?lahi Mekanoid ?ni?i");
							break;
						case 7554:
							a = LL14(L"ふるわれる奇蹟", L"Shaking Miracle", L"Miracle ebranle", L"Miracolo tremante", L"Milagro tembloroso", L"???? ??", L"被展現的奇蹟", L"????? ?????", L"Дрожащее чудо", L"Erschutterndes Wunder", L"Milagre tremendo", L"Schuddend wonder", L"Dr??cy cud", L"Sars?lan Mucize");
							break;
						case 7555:
							a = LL14(L"予定外の奇蹟", L"Unexpected Miracle", L"Miracle inattendu", L"Miracolo inaspettato", L"Milagro inesperado", L"?? ?? ??", L"意料之外的奇蹟", L"????? ??? ??????", L"Неожиданное чудо", L"Unerwartetes Wunder", L"Milagre inesperado", L"Onverwacht wonder", L"Nieoczekiwany cud", L"Beklenmedik Mucize");
							break;
						case 7556:
							a = LL14(L"鋼鉄の咆哮 -脅威-", L"Roar of Steel -Threat-", L"Rugissement de l'acier -Menace-", L"Ruggito d'acciaio -Minaccia-", L"Rugido de acero -Amenaza-", L"??? ?? ~??~", L"鋼鐵的咆哮 -威脅-", L"???? ??????? - ?????", L"Рев стали -Угроза-", L"Brullen aus Stahl -Bedrohung-", L"Rugido de aco -Ameaca-", L"Gebrul van staal -Dreiging-", L"Ryk stali -Zagro?enie-", L"Celi?in Kukreyi?i -Tehdit-");
							break;
						case 7560:
							a = LL14(L"雨の日の真実", L"Truth on a Rainy Day", L"Verite un jour de pluie", L"Verita in un giorno di pioggia", L"Verdad en un dia lluvioso", L"? ?? ?? ??", L"下雨天的真相", L"??????? ?? ??? ????", L"Правда в дождливый день", L"Wahrheit an einem Regentag", L"Verdade em um dia chuvoso", L"Waarheid op een regenachtige dag", L"Prawda w deszczowy dzie?", L"Ya?murlu Bir Gundeki Gercek");
							break;
						case 7561:
							a = LL14(L"不穏", L"Troubled", L"Trouble", L"Inquieto", L"Inquieto", L"??", L"不穩", L"?????", L"Тревожный", L"Unruhig", L"Perturbado", L"Onrustig", L"Niespokojny", L"Huzursuz");
							break;
						case 7562:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
							break;
						case 7563:
							a = LL14(L"犠牲の先の希望", L"Hope Beyond Sacrifice", L"Espoir au-dela du sacrifice", L"Speranza oltre il sacrificio", L"Esperanza mas alla del sacrificio", L"?? ?? ??", L"犧牲之後的希望", L"????? ???? ???????", L"Надежда после жертвы", L"Hoffnung uber das Opfer hinaus", L"Esperanca alem do sacrificio", L"Hoop voorbij opoffering", L"Nadzieja poza ofiar?", L"Fedakarl???n Otesindeki Umut");
							break;
						case 7564:
							a = L"Strange Feel";
							break;
						case 7565:
							a = L"Exhilarating Ride";
							break;
						case 7566:
							a = LL14(L"それぞれの正義", L"Each One's Justice", L"Chacun sa justice", L"Ognuno la sua giustizia", L"La justicia de cada uno", L"??? ??", L"各自的正義", L"????? ?? ????", L"Правосудие каждого", L"Die Gerechtigkeit jedes Einzelnen", L"A justica de cada um", L"Ieders eigen rechtvaardigheid", L"Sprawiedliwo?? ka?dego z nas", L"Her Birimizin Adaleti");
							break;
						case 7567:
							a = LL14(L"乗り越えるべき壁", L"Wall to Overcome", L"Mur a surmonter", L"Muro da superare", L"Muro que superar", L"??? ? ?", L"應當越過的障礙", L"???? ??? ?????? ????", L"Стена, которую нужно преодолеть", L"Mauer, die es zu uberwinden gilt", L"Muro a superar", L"Muur om te overwinnen", L"Mur do pokonania", L"A??lmas? Gereken Duvar");
							break;
						case 7568:
							a = LL14(L"月下の想い", L"Feelings Under the Moon", L"Sentiments sous la lune", L"Sentimenti sotto la luna", L"Sentimientos bajo la luna", L"??? ??", L"月下思念", L"????? ??? ?????", L"Чувства под луной", L"Gefuhle unter dem Mond", L"Sentimentos sob a lua", L"Gevoelens onder de maan", L"Uczucia pod ksi??ycem", L"Ay Alt?ndaki Duygular");
							break;
						case 7569:
							a = L"Miss You";
							break;
						case 7570:
							a = LL14(L"天の車", L"Chariot of Heaven", L"Char du ciel", L"Carro del cielo", L"Carro del cielo", L"??? ??", L"天之車", L"???? ??????", L"Небесная колесница", L"Himmelswagen", L"Carruagem do ceu", L"Hemelwagen", L"Rydwan niebios", L"Goklerin Arabas?");
							break;
						case 7571:
							a = LL14(L"突きつけられた現実", L"Reality Thrust Upon Us", L"La realite nous est imposee", L"Realta imposta su di noi", L"Realidad impuesta a nosotros", L"???? ??", L"擺在眼前的現實", L"?????? ??????? ?????", L"Реальность, навязанная нам", L"Uns aufgezwungene Realitat", L"Realidade imposta a nos", L"Realiteit ons opgedrongen", L"Rzeczywisto?? nam narzucona", L"Bize Dayat?lan Gerceklik");
							break;
						case 7572:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
							break;
						case 7573:
							a = LL14(L"全てを識るもの", L"The Omniscient", L"L'omniscient", L"L'onniscente", L"El omnisciente", L"?? ?? ?? ?", L"全知者", L"??????", L"Всеведущий", L"Der Allwissende", L"O onisciente", L"De alwetende", L"Wszechwiedz?cy", L"Her ?eyi Bilen");
							break;
						case 7574:
							a = LL14(L"想い、辿り着く場所", L"Where Feelings Lead", L"La ou les sentiments menent", L"Dove portano i sentimenti", L"Donde los sentimientos conducen", L"??? ??? ?", L"思念抵達之處", L"??? ???? ???????", L"Куда ведут чувства", L"Wohin Gefuhle fuhren", L"Onde os sentimentos levam", L"Waar gevoelens toe leiden", L"Gdzie prowadz? uczucia", L"Duygular?n Gitti?i Yer");
							break;
						case 7575:
							a = LL14(L"揺れ動く心", L"Wavering Heart", L"C?ur vacillant", L"Cuore incostante", L"Corazon vacilante", L"???? ??", L"動揺的心", L"??? ?????", L"Колеблющееся сердце", L"Wankendes Herz", L"Coracao vacilante", L"Wankelend hart", L"Chwiejne serce", L"Karars?z Kalp");
							break;
						case 7576:
							a = LL14(L"星降る夜に", L"On a Starry Night", L"Par une nuit etoilee", L"In una notte stellata", L"En una noche estrellada", L"?? ??? ??", L"在星辰降落之夜", L"?? ???? ????? ???????", L"Звездной ночью", L"In einer Sternennacht", L"Em uma noite estrelada", L"Op een sterrennacht", L"W gwie?dzist? noc", L"Y?ld?zl? Bir Gecede");
							break;
						case 7577:
						case 7578:
						case 7579:
						case 7580:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
							break;
						case 7581:
							a = LL14(L"本当の絆", L"True Bonds", L"Vrais liens", L"Veri legami", L"Verdaderos vinculos", L"??? ??", L"真正的羈絆", L"????? ??????", L"Истинные узы", L"Wahre Bande", L"Lacos verdadeiros", L"Echte banden", L"Prawdziwe wi?zi", L"Gercek Ba?lar");
							break;
						case 7582:
							a = LL14(L"猛き獣たち", L"Fierce Beasts", L"Betes feroces", L"Bestie feroci", L"Bestias feroces", L"??? ???", L"猛獸們", L"???? ?????", L"Свирепые звери", L"Wilde Bestien", L"Bestas ferozes", L"Woeste beesten", L"W?ciek?e bestie", L"Vah?i Canavarlar");
							break;
						case 7583:
							a = LL14(L"西ゼムリア通商会議", L"West Zemuria Trade Conference", L"Conference commerciale de Zemuria Ouest", L"Conferenza commerciale della Zemuria occidentale", L"Conferencia comercial de Zemuria Occidental", L"????? ????", L"西塞姆利亞通商會議", L"????? ??? Zemuria ???????", L"Западно-земурийская торговая конференция", L"West-Zemuria-Handelskonferenz", L"Conferencia Comercial de Zemuria Ocidental", L"Handelsconferentie West-Zemuria", L"Konferencja handlowa Zachodniej Zemurii", L"Bat? Zemurya Ticaret Konferans?");
							break;
						case 7584:
							a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Звуковой эффект", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
							break;
						case 7585:
							a = LL14(L"千年の妄執", L"Obsession of Millennia", L"Obsession des millenaires", L"Ossessione dei millenni", L"Obsesion de milenios", L"? ?? ??", L"千年的妄執", L"??? ???????", L"Одержимость тысячелетий", L"Obsession der Jahrtausende", L"Obsessao milenar", L"Obsessie van millennia", L"Obsesja tysi?cleci", L"Bin Y?ll?k Tak?nt?");
							break;
						case 7586:
							a = LL14(L"鋼鉄の咆哮 -死線-", L"Roar of Steel -Death Line-", L"Rugissement de l'acier -Ligne de mort-", L"Ruggito d'acciaio -Linea di morte-", L"Rugido de acero -Linea de muerte-", L"??? ?? ~??~", L"鋼鐵的咆哮 -死線-", L"???? ??????? - ?? ?????", L"Рев стали -Линия смерти-", L"Brullen aus Stahl -Todeslinie-", L"Rugido de aco -Linha de morte-", L"Gebrul van staal -Dodslijn-", L"Ryk stali -Linia ?mierci-", L"Celi?in Kukreyi?i -Olum Cizgisi-");
							break;
						case 7587:
							a = LL14(L"ポムっと! -お花見団子の逆襲-", L"Pom! -Cherry Blossom Dango Counterattack-", L"Pom! -Contre-attaque des dango fleurs de cerisier-", L"Pom! -Contrattacco del dango ai fiori di ciliegio-", L"!Pom! -Contraataque del dango de flor de cerezo-", L"Pom! ~??? ??? ??~", L"Pom! -花見?子的逆襲-", L"Pom! - ???? ????? ????? ????? ??????", L"Pom! -Контратака данго с вишневым цветом-", L"Pom! -Gegenangriff der Kirschbluten-Dango-", L"Pom! -Contra-ataque do dango de flor de cerejeira-", L"Pom! -Tegenstoot van de kersenbloesemdango-", L"Pom! -Kontratak dango z kwiatami wi?ni-", L"Pom! -Kiraz Cice?i Dango'nun Kar?? Ata??-");
							break;
						case 7588:
							a = LL14(L"Fateful Confrontation -ポムっと! Ver.-", L"Fateful Confrontation -Pom! Ver.-", L"Confrontation fatidique -Pom! Ver.-", L"Confronto fatidico -Pom! Ver.-", L"Confrontacion fatidica -!Pom! Ver.-", L"??? ?? -Pom! Ver.-", L"命運的對決 -Pom! Ver.-", L"???????? ???????? - Pom! Ver.", L"Судьбоносное противостояние -Pom! Ver.-", L"Schicksalhafte Konfrontation -Pom! Ver.-", L"Confronto fatidico -Pom! Ver.-", L"Noodlottige confrontatie -Pom! Ver.-", L"Fatalna konfrontacja -Pom! Ver.-", L"Kader An? -Pom! Ver.-");
							break;
						case 7589:
							a = LL14(L"ポムりますか", L"Shall We Pom?", L"Allons-nous Pom?", L"Vogliamo Pommare?", L"?Hacemos Pom?", L"Pom ??????", L"要來 Pom 一下?", L"?? ???? ?? Pom?", L"Сыграем в Pom?", L"Sollen wir Pom?", L"Vamos Pom?", L"Zullen we Pommen?", L"Zagramy w Pom?", L"Pom Yapal?m m??");
							break;
						case 7690:
							a = LL14(L"エリィ絶叫コースター", L"Elie Scream Coaster", L"Montagnes russes hurlantes d'Elie", L"Ottovolante urlante di Elie", L"Montana rusa de gritos de Elie", L"Elie? ?? ???", L"艾莉尖叫雲霄飛車", L"???????? ???? Elie", L"Американские горки крика Elie", L"Elies Schreiachterbahn", L"Montanha-russa de gritos da Elie", L"Elie's schreeuwachtbaan", L"Kolejka krzyku Elie", L"Elie'nin C??l?k Treni");
							break;
						case 7591:
							a = LL14(L"小さな英雄 -オルゴール-", L"Little Hero -Music Box-", L"Petit heros -Boite a musique-", L"Piccolo eroe -Carillon-", L"Pequeno heroe -Caja de musica-", L"?? ?? -???-", L"小小的英雄 -八音盒-", L"??? ???? - ????? ??????", L"Маленький герой -Музыкальная шкатулка-", L"Kleiner Held -Spieluhr-", L"Pequeno heroi -Caixa de musica-", L"Kleine held -Muziekdoos-", L"Ma?y bohater -Pozytywka-", L"Kucuk Kahraman -Muzik Kutusu-");
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
						a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"?? ??? 《VII組》", L"特科班《VII組》", L"????? ??????", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klas VII", L"Klasa VII", L"S?n?f VII");
						break;
					case 8002:
						a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours vers l'avant", L"Sempre avanti", L"Siempre adelante", L"??? ???", L"一心一意，向前邁進", L"??? ?????? ??????", L"Только вперед", L"Immer vorwarts", L"Sempre em frente", L"Altijd vooruit", L"Zawsze do przodu", L"Daima ?leri");
						break;
					case 8100:
						a = LL14(L"近郊都市トリスタ", L"Suburban City Trista", L"Ville suburbaine Trista", L"Citta suburbana Trista", L"Ciudad suburbana Trista", L"?? ?? Trista", L"近郊都市 Trista", L"????? Trista ???????", L"Prigorodnyj gorod Trista", L"Vorstadt Trista", L"Cidade suburbana Trista", L"Voorstad Trista", L"Podmiejskie miasto Trista", L"Banliyo ?ehri Trista");
						break;
					case 8101:
						a = LL14(L"交易町ケルディック", L"Trading Town Celdic", L"Ville marchande Celdic", L"Citta commerciale Celdic", L"Pueblo comercial Celdic", L"?? ?? Celdic", L"交易鎮 Celdic", L"???? Celdic ????????", L"Torgovyj gorod Celdic", L"Handelsstadt Celdic", L"Vila comercial Celdic", L"Handelsstad Celdic", L"Handlowe miasto Celdic", L"Ticaret Kasabas? Celdic");
						break;
					case 8102:
						a = LL14(L"翡翠の公都バリアハート", L"Jade Capital Bareahard", L"Capitale de jade Bareahard", L"Capitale di giada Bareahard", L"Capital de jade Bareahard", L"??? ?? Bareahard", L"翡翠公都 Bareahard", L"????? ????? Bareahard", L"Nefritovaya stolica Bareahard", L"Jade-Hauptstadt Bareahard", L"Capital de jade Bareahard", L"Jade-hoofdstad Bareahard", L"Jadeitowa stolica Bareahard", L"Ye?im Ba?kenti Bareahard");
						break;
					case 8103:
						a = LL14(L"湖畔の街レグラム", L"Lakeside Town Legram", L"Ville au bord du lac Legram", L"Citta lacustre Legram", L"Pueblo junto al lago Legram", L"?? ?? Legram", L"湖畔之街 Legram", L"???? Legram ????? ???????", L"Priozyornyj gorod Legram", L"Seeuferstadt Legram", L"Vila a beira-lago Legram", L"Meerstad Legram", L"Nadjeziorskie miasto Legram", L"Gol Kenar? Kasabas? Legram");
						break;
					case 8104:
						a = LL14(L"黒銀の鋼都ルーレ", L"Iron City Roer", L"Ville de fer Roer", L"Citta del ferro Roer", L"Ciudad del hierro Roer", L"??? ?? ?? Roer", L"黑銀鋼都 Roer", L"????? Roer ????????", L"Zheleznyj gorod Roer", L"Eisenstadt Roer", L"Cidade do ferro Roer", L"IJzerstad Roer", L"?elazne miasto Roer", L"Demir ?ehir Roer");
						break;
					case 8106:
						a = LL14(L"遊牧民の集落", L"Nomad Settlement", L"Campement nomade", L"Insediamento nomade", L"Asentamiento nomada", L"???? ??", L"遊牧民族定居點", L"??????? ?????", L"Poselenie kochevnikov", L"Nomadensiedlung", L"Assentamento nomade", L"Nomadennederzetting", L"Osada nomadow", L"Gocebe Yerle?imi");
						break;
					case 8107:
						a = LL14(L"緋の帝都ヘイムダル", L"Crimson Capital Heimdallr", L"Capitale pourpre Heimdallr", L"Capitale cremisi Heimdallr", L"Capital carmesi Heimdallr", L"?? ?? Heimdallr", L"緋紅帝都 Heimdallr", L"??????? ???????? Heimdallr", L"Alaya stolica Heimdallr", L"Purpurrote Hauptstadt Heimdallr", L"Capital carmesim Heimdallr", L"Karmozijnrode hoofdstad Heimdallr", L"Szkar?atna stolica Heimdallr", L"K?z?l Ba?kent Heimdallr");
						break;
					case 8108:
						a = LL14(L"癒しの我が家", L"Healing Home", L"Maison de guerison", L"Casa curativa", L"Hogar sanador", L"??? ?? ?", L"療癒的故郷", L"???? ??????", L"Isceleblyayushchij dom", L"Heilsames Zuhause", L"Lar curativo", L"Heilzaam thuis", L"Uzdrawiaj?cy dom", L"?ifal? Yuva");
						break;
					case 8109:
						a = LL14(L"ダイニングバー《F》", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"??? ? 《F》", L"餐飲酒?《F》", L"???? ?????? F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F", L"Dining Bar F");
						break;
					case 8110:
						a = LL14(L"常在戦場の気概", L"Ever-Present War Spirit", L"Esprit de guerre constant", L"Spirito bellico costante", L"Espiritu de guerra constante", L"????? ??", L"常在戰場的氣概", L"??? ????? ???????", L"Postoyannyj voennyj duh", L"Allgegenwartiger Kriegsgeist", L"Espirito de guerra constante", L"Altijd aanwezige oorlogsgeest", L"Nieustanny duch walki", L"Daima Mevcut Sava? Ruhu");
						break;
					case 8111:
						a = LL14(L"ガレリアの巨壁", L"Garelia Fortress", L"Forteresse de Garelia", L"Fortezza di Garelia", L"Fortaleza de Garelia", L"Garelia? ??", L"Garelia 巨壁", L"??? Garelia", L"Krepost' Gareliya", L"Festung Garelia", L"Fortaleza de Garelia", L"Vesting Garelia", L"Twierdza Garelia", L"Garelia Kalesi");
						break;
					case 8120:
						a = LL14(L"足湯の温もり", L"Foot Bath Warmth", L"Chaleur du bain de pieds", L"Calore del pediluvio", L"Calor del bano de pies", L"??? ???", L"足浴的?暖", L"??? ???? ?????", L"Teplo vannochki dlya nog", L"Warme des Fusbads", L"Calor do banho de pes", L"Warmte van het voetbad", L"Ciep?o k?pieli stop", L"Ayak Banyosu S?cakl???");
						break;
					case 8121:
						a = LL14(L"静寂の郷", L"Silent Village", L"Village silencieux", L"Villaggio silenzioso", L"Aldea silenciosa", L"??? ??", L"靜謐之郷", L"?????? ???????", L"Tihaya derevnya", L"Stilles Dorf", L"Vila silenciosa", L"Stil dorp", L"Cicha wioska", L"Sessiz Koy");
						break;
					case 8122:
						a = LL14(L"明日への休息", L"Rest for Tomorrow", L"Repos pour demain", L"Riposo per domani", L"Descanso para manana", L"???? ??", L"通往明天的休息", L"???? ????", L"Otdyh pered zavtrashnim dnyom", L"Ruhe fur morgen", L"Descanso para amanha", L"Rust voor morgen", L"Odpoczynek przed jutrem", L"Yar?n ?cin Dinlenme");
						break;
					case 8123:
						a = LL14(L"春の陽射し", L"Spring Sunshine", L"Soleil printanier", L"Sole primaverile", L"Sol de primavera", L"?? ??", L"春日陽光", L"??? ??????", L"Vesennee solnce", L"Fruhlingssonnenschein", L"Sol de primavera", L"Lentezon", L"Wiosenne s?o?ce", L"Bahar Gune?i");
						break;
					case 8125:
						a = LL14(L"カレイジャス発進！", L"Courageous Launch!", L"Lancement du Courageous!", L"Lancio del Courageous!", L"!Lanzamiento del Courageous!", L"Courageous ??!", L"Courageous 出?！", L"?????? Courageous!", L"Zapusk Courageous!", L"Start der Courageous!", L"Lancamento do Courageous!", L"Lancering van de Courageous!", L"Start Courageous!", L"Courageous Havalan?yor!");
						break;
					case 8126:
						a = LL14(L"目覚める意志", L"Awakening Will", L"Volonte s'eveillant", L"Volonta risvegliata", L"Voluntad que despierta", L"???? ??", L"覺醒的意志", L"????? ???????", L"Probuzhdayushchayasya volya", L"Erwachender Wille", L"Vontade despertando", L"Ontwakende wil", L"Budz?ca si? wola", L"Uyanan ?rade");
						break;
					case 8127:
						a = LL14(L"白銀の巨船", L"Silver Ship", L"Vaisseau d'argent", L"Nave d'argento", L"Nave de plata", L"??? ??", L"白銀巨船", L"??????? ??????", L"Serebryanyj korabl'", L"Silbernes Schiff", L"Navio de prata", L"Zilveren schip", L"Srebrny statek", L"Gumu? Gemi");
						break;
					case 8150:
						a = LL14(L"放課後の時間", L"After School", L"Apres l'ecole", L"Dopo la scuola", L"Despues de clase", L"?? ?? ??", L"放學後的時間", L"??? ???????", L"Posle urokov", L"Nach der Schule", L"Depois da escola", L"Naschoolse tijd", L"Po szkole", L"Okul C?k???");
						break;
					case 8152:
						a = LL14(L"さわやかな朝", L"Refreshing Morning", L"Matin rafraichissant", L"Mattina rinfrescante", L"Manana refrescante", L"??? ??", L"清爽的早晨", L"???? ????", L"Osvyazhayushchee utro", L"Erfrischender Morgen", L"Manha refrescante", L"Verfrissende ochtend", L"Orze?wiaj?cy poranek", L"Ferah Bir Sabah");
						break;
					case 8153:
						a = LL14(L"雨音の学院", L"Rain-sound Academy", L"Academie au son de la pluie", L"Accademia al suono della pioggia", L"Academia al sonido de la lluvia", L"???? ??", L"雨聲學院", L"???????? ??? ?????", L"Akademiya pod zvuk dozhdya", L"Akademie im Regenklang", L"Academia ao som da chuva", L"Academie met regengeluid", L"Akademia w d?wi?ku deszczu", L"Ya?mur Sesli Akademi");
						break;
					case 8154:
						a = LL14(L"爽やかな陽射し", L"Clear Sunshine", L"Soleil eclatant", L"Luce solare chiara", L"Sol claro", L"??? ??", L"爽朗的陽光", L"??? ?????", L"Yasnaya solnechnaya pogoda", L"Klarer Sonnenschein", L"Sol claro", L"Heldere zonneschijn", L"Jasne s?o?ce", L"Ac?k Gune? I????");
						break;
					case 8156:
						a = LL14(L"トールズ士官学院祭", L"Thors Academy Festival", L"Festival de l'Academie Thors", L"Festival dell'Accademia Thors", L"Festival de la Academia Thors", L"Thors ???? ??", L"托爾茲軍官學院祭", L"?????? ???????? Thors", L"Festival Akademii Thors", L"Thors-Akademie-Fest", L"Festival da Academia Thors", L"Thors Academiefestival", L"Festiwal Akademii Thors", L"Thors Akademi Festivali");
						break;
					case 8158:
						a = LL14(L"青空の開放感", L"Open Sky", L"Ciel ouvert", L"Cielo aperto", L"Cielo abierto", L"?? ??? ???", L"青空的開放感", L"???? ??????", L"Otkrytoe nebo", L"Offener Himmel", L"Ceu aberto", L"Open lucht", L"Otwarte niebo", L"Ac?k Gokyuzu");
						break;
					case 8159:
						a = LL14(L"自由行動日", L"Free Day", L"Journee libre", L"Giorno libero", L"Dia libre", L"?? ???", L"自由行動日", L"??? ??", L"Den' svobodnyh dejstvij", L"Freier Tag", L"Dia livre", L"Vrije dag", L"Dzie? wolny", L"Serbest Gun");
						break;
					case 8200:
						a = LL14(L"異郷の空", L"Foreign Sky", L"Ciel etranger", L"Cielo straniero", L"Cielo extranjero", L"??? ??", L"異郷之空", L"???? ?????", L"Chuzhoe nebo", L"Fremder Himmel", L"Ceu estrangeiro", L"Vreemde lucht", L"Obce niebo", L"Yabanc? Gokyuzu");
						break;
					case 8201:
						a = LL14(L"峡谷道を往く", L"Through the Canyon", L"A travers le canyon", L"Attraverso il canyon", L"A traves del canon", L"???? ??", L"穿梭峽谷道", L"??? ??????", L"Cherez kan'on", L"Durch den Canyon", L"Pelo canion", L"Door de kloof", L"Przez kanion", L"Kanyondan Gecerken");
						break;
					case 8202:
						a = LL14(L"精霊の小道", L"Spirit Path", L"Chemin des esprits", L"Sentiero degli spiriti", L"Senda de los espiritus", L"??? ???", L"精靈小徑", L"???? ???????", L"Tropa duhov", L"Geisterpfad", L"Caminho dos espiritos", L"Geesterpad", L"?cie?ka duchow", L"Ruh Yolu");
						break;
					case 8203:
						a = LL14(L"蒼穹の大地", L"Azure Skies Land", L"Terre aux cieux azurs", L"Terra dai cieli azzurri", L"Tierra de cielos azures", L"??? ??", L"蒼穹大地", L"??? ?????? ???????", L"Zemlya lazurnyh nebes", L"Land unter azurblauem Himmel", L"Terra de ceus azuis", L"Land van azuurblauwe luchten", L"Kraina b??kitnego nieba", L"Gok mavisi Topraklar");
						break;
					case 8210:
						a = LL14(L"戦火を越えて", L"Beyond the Flames of War", L"Au-dela des flammes de la guerre", L"Oltre le fiamme della guerra", L"Mas alla de las llamas de la guerra", L"??? ??", L"跨越戰火", L"?? ???? ???? ?????", L"Skvoz' plamya vojny", L"Jenseits der Flammen des Krieges", L"Alem das chamas da guerra", L"Voorbij de oorlogsvlammen", L"Poza p?omienie wojny", L"Sava? Alevlerinin Otesinde");
						break;
					case 8212:
						a = L"Trudge Along";
						break;
					case 8213:
						a = LL14(L"冬の訪れ", L"Arrival of Winter", L"L'arrivee de l'hiver", L"L'arrivo dell'inverno", L"Llegada del invierno", L"??? ??", L"冬日將至", L"???? ??????", L"Prihod zimy", L"Ankunft des Winters", L"Chegada do inverno", L"Komst van de winter", L"Przyj?cie zimy", L"K???n Geli?i");
						break;
					case 8300:
						a = LL14(L"旧校舎の謎", L"Old Schoolhouse Mystery", L"Mystere du vieux batiment", L"Mistero del vecchio edificio", L"Misterio del viejo edificio", L"???? ????", L"舊校舍之謎", L"??? ???? ??????? ??????", L"Tajna staroj shkoly", L"Geheimnis des alten Schulhauses", L"Misterio da velha escola", L"Mysterie van het oude schoolgebouw", L"Tajemnica starej szko?y", L"Eski Okul Binas?n?n Gizemi");
						break;
					case 8301:
						a = LL14(L"探索", L"Exploration", L"Exploration", L"Esplorazione", L"Exploracion", L"??", L"探索", L"???????", L"Issledovanie", L"Erkundung", L"Exploracao", L"Verkenning", L"Eksploracja", L"Ke?if");
						break;
					case 8302:
						a = LL14(L"深淵へ向かう", L"Toward the Abyss", L"Vers l'abime", L"Verso l'abisso", L"Hacia el abismo", L"???? ???", L"邁向深淵", L"??? ???????", L"K bezdne", L"Dem Abgrund entgegen", L"Em direcao ao abismo", L"Naar de afgrond", L"Ku otch?ani", L"Ucuruma Do?ru");
						break;
					case 8303:
						a = LL14(L"聖女の城", L"Saint's Castle", L"Chateau de la sainte", L"Castello della santa", L"Castillo de la santa", L"??? ?", L"聖女之城", L"???? ???????", L"Zamol svyatoj", L"Schloss der Heiligen", L"Castelo da santa", L"Kasteel van de heilige", L"Zamek ?wi?tej", L"Azizenin Kalesi");
						break;
					case 8304:
						a = LL14(L"明日を掴むために", L"To Seize Tomorrow", L"Pour saisir demain", L"Per afferrare il domani", L"Para atrapar el manana", L"??? ?? ??", L"為了抓住明天", L"?????? ????", L"Chtoby zahvatit' zavtrashnij den'", L"Um das Morgen zu ergreifen", L"Para alcancar o amanha", L"Om morgen te grijpen", L"Aby pochwyci? jutro", L"Yar?n? Yakalamak ?cin");
						break;
					case 8305:
						a = LL14(L"地下に眠る遺構", L"Ruins Beneath", L"Ruines souterraines", L"Rovine sotterranee", L"Ruinas subterraneas", L"??? ?? ??", L"?睡地下的遺構", L"????? ??? ?????", L"Podzemnye ruiny", L"Ruinen darunter", L"Ruinas subterraneas", L"Ondergrondse ruines", L"Podziemne ruiny", L"Yeralt? Harabeleri");
						break;
					case 8308:
						a = LL14(L"世の礎たるために", L"To Be the World's Foundation", L"Pour etre le fondement du monde", L"Per essere la fondazione del mondo", L"Para ser el cimiento del mundo", L"??? ???? ?? ??", L"為了成為世界的基石", L"????? ???? ??????", L"Chtoby stat' osnovoj mira", L"Um das Fundament der Welt zu sein", L"Para ser o fundamento do mundo", L"Om het fundament van de wereld te zijn", L"Aby by? fundamentem ?wiata", L"Dunyan?n Temeli Olmak ?cin");
						break;
					case 8310:
						a = LL14(L"精霊窟", L"Spirit Cave", L"Grotte des esprits", L"Grotta degli spiriti", L"Cueva de los espiritus", L"???", L"精靈窟", L"??? ???????", L"Peshchera duhov", L"Geisterhohle", L"Caverna dos espiritos", L"Grot van de geesten", L"Jaskinia duchow", L"Ruh Ma?aras?");
						break;
					case 8311:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8312:
						a = L"Phantasmal Blaze";
						break;
					case 8313:
						a = LL14(L"夢幻回廊", L"Phantasmagoria Corridor", L"Couloir de phantasmagorie", L"Corridoio di fantasmagoria", L"Corredor de fantasmagoria", L"????", L"夢幻迴廊", L"??? ??????", L"Koridor fantasmagorii", L"Phantasmagoria-Korridor", L"Corredor de fantasmagoria", L"Fantoomcorridor", L"Korytarz fantasmagorii", L"Hayalet Koridor");
						break;
					case 8315:
						a = LL14(L"幻煌", L"Phantom Radiance", L"Eclat fantome", L"Splendore fantasma", L"Resplandor fantasma", L"??", L"幻煌", L"???? ????", L"Prizrachnoe siyanie", L"Phantom-Glanz", L"Resplendor fantasma", L"Fantoomglans", L"Blask widma", L"Hayalet Par?lt?s?");
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
						a = LL14(L"巨イナルチカラ", L"Colossal Power", L"Pouvoir colossal", L"Potere colossale", L"Poder colosal", L"??? ?", L"巨大的力量", L"??? ?????", L"Kolossal'naya sila", L"Kolossale Macht", L"Poder colossal", L"Kolossale kracht", L"Kolosalna moc", L"Muazzam Guc");
						break;
					case 8409:
						a = L"The Decisive Collision";
						break;
					case 8410:
						a = LL14(L"この手で道を切り拓く!", L"Carve Our Path with These Hands!", L"Ouvrir la voie de nos mains!", L"Aprire la strada con queste mani!", L"!Abrir camino con estas manos!", L"? ??? ?? ????!", L"用這雙手開闢道路！", L"???? ?????? ???????!", L"Prolozhit' put' etimi rukami!", L"Den Weg mit diesen Handen ebnen!", L"Abrir o caminho com estas maos!", L"De weg banen met deze handen!", L"Przetrze? szlak tymi r?kami!", L"Yolumuzu Bu Ellerle Acaca??z!");
						break;
					case 8411:
						a = LL14(L"赤点です...", L"Failed...", L"Echec...", L"Fallito...", L"Fallido...", L"?????...", L"不及格...", L"???...", L"Neudovletvoritel'no...", L"Nicht bestanden...", L"Reprovado...", L"Gezakt...", L"Obla?...", L"Kald?n...");
						break;
					case 8412:
						a = L"Unknown Threat";
						break;
					case 8413:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8420:
						a = L"Heated Mind";
						break;
					case 8421:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
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
						a = LL14(L"輝ける明日へ", L"Toward a Shining Tomorrow", L"Vers un demain radieux", L"Verso un domani radioso", L"Hacia un manana radiante", L"??? ???", L"通往輝煌的明天", L"??? ?? ????", L"K siyayushchemu zavtra", L"Einem strahlenden Morgen entgegen", L"Para um amanha brilhante", L"Naar een stralende morgen", L"Ku ?wietlistemu jutru", L"Parlak Bir Yarana Do?ru");
						break;
					case 8435:
						a = LL14(L"迫る巨影", L"Approaching Giant Shadow", L"L'ombre geante approche", L"L'ombra gigante si avvicina", L"Sombra gigante acercandose", L"???? ??", L"逼近的巨影", L"?? ????? ?????", L"Priblizhayushchayasya gigantskaya ten'", L"Herannahender Riesenschatten", L"Sombra gigante se aproximando", L"Naderende gigantische schaduw", L"Zbli?aj?cy si? gigantyczny cie?", L"Yakla?an Dev Golge");
						break;
					case 8441:
						a = L"E.O.V";
						break;
					case 8442:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"刻いた", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8500:
						a = L"Strain";
						break;
					case 8501:
						a = LL14(L"夜のひととき", L"Nighttime", L"Moment de nuit", L"Momento notturno", L"Momento nocturno", L"?? ??", L"夜?時光", L"??? ?????", L"Nochnoe vremya", L"Nachtzeit", L"Momento da noite", L"Nachttijd", L"Nocny czas", L"Gece Vakti");
						break;
					case 8502:
						a = LL14(L"トラブル発生", L"Trouble", L"Probleme", L"Problema", L"Problema", L"??? ??", L"發生麻煩", L"?????", L"Problema", L"Arger", L"Problema", L"Problemen", L"Problem", L"Sorun");
						break;
					case 8503:
						a = LL14(L"鉄路遥々", L"Distant Iron Road", L"Lointain chemin de fer", L"Lontana ferrovia", L"Lejana via de hierro", L"??? ??", L"漫漫鐵路", L"??? ?????? ?????", L"Dal'nij zheleznyj put'", L"Ferne Eisenbahn", L"Caminho de ferro distante", L"Verre ijzeren weg", L"Daleka ?elazna droga", L"Uzak Demir Yolu");
						break;
					case 8504:
						a = LL14(L"旅愁", L"Travel Melancholy", L"Melancolie du voyage", L"Malinconia del viaggio", L"Melancolia del viaje", L"??", L"旅愁", L"??? ?????", L"Dorozhnaya toska", L"Reiseschwermut", L"Melancolia de viagem", L"Reismelancholie", L"Podro?na melancholia", L"Yolculuk Huzunu");
						break;
					case 8505:
						a = LL14(L"皇城にて", L"At the Imperial Castle", L"Au chateau imperial", L"Al castello imperiale", L"En el castillo imperial", L"????", L"在皇城", L"?? ?????? ????????????", L"V imperatorskom zamke", L"Im kaiserlichen Schloss", L"No castelo imperial", L"In het keizerlijk kasteel", L"W zamku cesarskim", L"?mparatorluk Kalesinde");
						break;
					case 8506:
						a = L"Let's Study";
						break;
					case 8507:
						a = LL14(L"知恵を絞って", L"Rack Your Brains", L"Se creuser la tete", L"Spremiti le meningi", L"Devanarse los sesos", L"??? ???", L"竭盡全力思考", L"???? ?????", L"Poraskinut' mozgami", L"Den Kopf zerbrechen", L"Quebrar a cabeca", L"Je hersens pijnigen", L"Wyt??a? mozg", L"Zihnini Cal??t?r");
						break;
					case 8508:
						a = LL14(L"実技教練", L"Combat Training", L"Entrainement au combat", L"Addestramento al combattimento", L"Entrenamiento de combate", L"?? ??", L"實技教練", L"????? ?????", L"Boevaya podgotovka", L"Kampftraining", L"Treinamento de combate", L"Gevechtstraining", L"Trening bojowy", L"Sava? E?itimi");
						break;
					case 8509:
						a = LL14(L"寮に帰ろう", L"Back to the Dorm", L"Retour au dortoir", L"Ritorno al dormitorio", L"Regreso al dormitorio", L"???? ????", L"回宿舍?", L"?????? ??? ?????", L"Nazad v obshchezhitie", L"Zuruck ins Wohnheim", L"De volta ao dormitorio", L"Terug naar de slaapzaal", L"Powrot do internatu", L"Yurda Donu?");
						break;
					case 8510:
						a = LL14(L"アーベントタイム", L"Evening Time", L"Soiree", L"Serata", L"Tarde noche", L"Abend time", L"傍?時分", L"??? ??????", L"Vechernee vremya", L"Abendzeit", L"Hora do entardecer", L"Avondtijd", L"Wieczorny czas", L"Ak?am Vakti");
						break;
					case 8512:
						a = LL14(L"鉄の統率", L"Iron Command", L"Commandement de fer", L"Comando di ferro", L"Mando de hierro", L"?? ??", L"鋼鐵統率", L"????? ??????", L"Zheleznoe komandovanie", L"Eisernes Kommando", L"Comando de ferro", L"IJzeren bevel", L"?elazne dowodztwo", L"Demir Komuta");
						break;
					case 8513:
						a = LL14(L"暗躍", L"Moving in the Shadows", L"Agir dans l'ombre", L"Muoversi nelle ombre", L"Moviendose en las sombras", L"??", L"暗中活動", L"?????? ?? ??????", L"Dejstviya v teni", L"Schattenarbeit", L"Movendo-se nas sombras", L"In de schaduw bewegen", L"Dzia?anie w cieniu", L"Golge Harekat?");
						break;
					case 8514:
						a = LL14(L"想いの行き先", L"Where Feelings Lead", L"La ou les sentiments menent", L"Dove portano i sentimenti", L"Donde los sentimientos conducen", L"??? ??", L"思念的去向", L"??? ???? ???????", L"Kuda vedut chuvstva", L"Wohin Gefuhle fuhren", L"Para onde os sentimentos levam", L"Waar gevoelens toe leiden", L"Gdzie prowadz? uczucia", L"Duygular?n Gitti?i Yer");
						break;
					case 8515:
						a = LL14(L"傷心", L"Heartbreak", L"C?ur brise", L"Cuore infranto", L"Corazon roto", L"??", L"傷心", L"??? ?????", L"Razbitoe serdce", L"Herzeleid", L"Coracao partido", L"Hartezeer", L"Z?amane serce", L"Kalp K?r?kl???");
						break;
					case 8516:
						a = LL14(L"揺らめく炎を見つめて", L"Watching the Flickering Flames", L"Regarder les flammes vacillantes", L"Guardando le fiamme tremolanti", L"Mirando las llamas vacilantes", L"???? ??? ????", L"凝視著搖曳的火?", L"?????? ??????? ????????", L"Glyadya na merkayushchee plamya", L"Die flackernden Flammen beobachten", L"Observando as chamas oscilantes", L"Kijken naar de flikkerende vlammen", L"Patrz?c na migocz?ce p?omienie", L"Titrek Alevleri ?zlerken");
						break;
					case 8517:
						a = LL14(L"一途な気持ち", L"Single-minded Feelings", L"Sentiments sinceres", L"Sentimenti sinceri", L"Sentimientos sinceros", L"???? ??", L"專一的心情", L"????? ?????", L"Iskrennie chuvstva", L"Aufrichtige Gefuhle", L"Sentimentos sinceros", L"Oprechte gevoelens", L"Szczere uczucia", L"Samimi Duygular");
						break;
					case 8520:
						a = LL14(L"臨戦態勢", L"Combat Ready", L"Pret au combat", L"Pronto al combattimento", L"Listo para el combate", L"????", L"進入戰鬥?態", L"????? ??????", L"Boevaya gotovnost'", L"Gefechtsbereit", L"Pronto para o combate", L"Gevechtsklaar", L"Gotowy do walki", L"Sava?a Haz?r");
						break;
					case 8521:
						a = L"Seriousness";
						break;
					case 8522:
						a = LL14(L"静かなる昂揚", L"Quiet Exhilaration", L"Exaltation tranquille", L"Silenziosa esaltazione", L"Silenciosa exaltacion", L"??? ??", L"安靜的昂揚", L"?????? ????", L"Tihoe voodushevlenie", L"Stille Begeisterung", L"Exaltacao silenciosa", L"Stille opwinding", L"Cicha ekscytacja", L"Sessiz Co?ku");
						break;
					case 8523:
						a = LL14(L"暖かな夕餉", L"Warm Dinner", L"Diner chaud", L"Cena calda", L"Cena caliente", L"??? ?? ??", L"?暖的?餐", L"???? ????", L"Teplyj uzhin", L"Warmes Abendessen", L"Jantar quente", L"Warm diner", L"Ciep?a kolacja", L"S?cak Ak?am Yeme?i");
						break;
					case 8524:
						a = L"Atrocious Raid";
						break;
					case 8525:
						a = LL14(L"全てを賭して今、ここに立つ", L"Standing Here, Betting Everything", L"Debout ici, pariant tout", L"In piedi qui, scommettendo tutto", L"Parado aqui, apostandolo todo", L"?? ?? ?? ??, ??? ??", L"賭上一切現在，立於此地", L"??? ???? ????? ??? ?? ???", L"Stoya zdes', stavy vsyo na kartu", L"Hier stehen, alles setzen", L"De pe aqui, apostando tudo", L"Hier staan, alles op het spel zetten", L"Stoj?c tu, stawiaj?c wszystko", L"Her ?eyi Goze Al?p Burada Duruyorum");
						break;
					case 8527:
						a = LL14(L"新しい仲間たち", L"New Comrades", L"Nouveaux camarades", L"Nuovi compagni", L"Nuevos camaradas", L"??? ???", L"新的夥伴們", L"???? ???", L"Novye tovarishchi", L"Neue Kameraden", L"Novos camaradas", L"Nieuwe kameraden", L"Nowi towarzysze", L"Yeni Yolda?lar");
						break;
					case 8528:
						a = LL14(L"不透明な事態", L"Opaque Situation", L"Situation opaque", L"Situazione opaca", L"Situacion opaca", L"???? ??", L"不明朗的事態", L"??? ????", L"Neprozrachnaya situaciya", L"Undurchsichtige Lage", L"Situacao opaca", L"Ondoorzichtige situatie", L"Niejasna sytuacja", L"Belirsiz Durum");
						break;
					case 8529:
						a = LL14(L"鉄血へのレクイエム", L"Requiem for Iron and Blood", L"Requiem pour le fer et le sang", L"Requiem per il ferro e il sangue", L"Requiem por el hierro y la sangre", L"??? ?クイエム", L"鐵血輓歌", L"???? ?????? ?????", L"Rekviem po zhelezu i krovi", L"Requiem fur Eisen und Blut", L"Requiem para ferro e sangue", L"Requiem voor ijzer en bloed", L"Requiem dla ?elaza i krwi", L"Demir ve Kan ?cin A??t");
						break;
					case 8530:
						a = LL14(L"幻想の唄 -PHANTASMAGORIA-", L"Phantom Song -PHANTASMAGORIA-", L"Chant fantome -PHANTASMAGORIA-", L"Canto fantasma -PHANTASMAGORIA-", L"Canto fantasma -PHANTASMAGORIA-", L"??? ?? -PHANTASMAGORIA-", L"幻想之歌 -PHANTASMAGORIA-", L"????? ?????? -PHANTASMAGORIA-", L"Prizrachnaya pesnya -PHANTASMAGORIA-", L"Phantommely -PHANTASMAGORIA-", L"Cancao fantasma -PHANTASMAGORIA-", L"Fantoomlied -PHANTASMAGORIA-", L"Pie?? widma -PHANTASMAGORIA-", L"Hayalet ?ark? -PHANTASMAGORIA-");
						break;
					case 8531:
						a = LL14(L"刻ハ至レリ", L"The Hour Has Come", L"L'heure est venue", L"L'ora e giunta", L"La hora ha llegado", L"??? ????", L"時機已到", L"??? ???? ??????", L"Chas nastal", L"Die Stunde ist gekommen", L"A hora chegou", L"Het uur is aangebroken", L"Nadesz?a godzina", L"Zaman Geldi");
						break;
					case 8532:
						a = LL14(L"目覚めし伝承", L"Awakening Legend", L"Legende s'eveillant", L"Leggenda risvegliata", L"Leyenda que despierta", L"??? ??", L"覺醒的傳承", L"?????? ???????", L"Probuzhdayushchayasya legenda", L"Erwachende Legende", L"Lenda despertando", L"Ontwakende legende", L"Budz?ca si? legenda", L"Uyanan Efsane");
						break;
					case 8533:
						a = LL14(L"唯一の希望", L"Only Hope", L"Seul espoir", L"Unica speranza", L"Unica esperanza", L"??? ??", L"唯一的希望", L"????? ??????", L"Edinstvennaya nadezhda", L"Einzige Hoffnung", L"Unica esperanca", L"Enige hoop", L"Jedyna nadzieja", L"Tek Umut");
						break;
					case 8535:
					case 8537:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8538:
						a = LL14(L"今はまだ...", L"Not Yet...", L"Pas encore...", L"Non ancora...", L"Aun no...", L"??? ??...", L"現在還...", L"??? ???...", L"Poka eshchyo net...", L"Noch nicht...", L"Ainda nao...", L"Nog niet...", L"Jeszcze nie...", L"Henuz De?il...");
						break;
					case 8539:
						a = LL14(L"あの日に見た夜空", L"The Night Sky I Saw That Day", L"Le ciel nocturne de ce jour-la", L"Il cielo stellato di quel giorno", L"El cielo nocturno de aquel dia", L"?? ? ???", L"那天所見的星空", L"???? ????? ???? ?????? ??? ?????", L"Nochnoe nebo, kotoroe ya videl v tot den'", L"Der Nachthimmel von jenem Tag", L"O ceu noturno que vi naquele dia", L"De nachthemel die ik die dag zag", L"Nocne niebo, ktore widzia?em tamtego dnia", L"O Gun Gordu?um Gece Gokyuzu");
						break;
					case 8540:
						a = LL14(L"偽りの時間", L"False Time", L"Temps mensonger", L"Tempo falso", L"Tiempo falso", L"??? ??", L"?偽的時間", L"??? ????", L"Lzhivoe vremya", L"Falsche Zeit", L"Tempo falso", L"Valse tijd", L"Fa?szywy czas", L"Sahte Zaman");
						break;
					case 8541:
						a = LL14(L"紅き翼 -新たなる風-", L"Crimson Wings -New Wind-", L"Ailes pourpres -Nouveau vent-", L"Ali cremisi -Nuovo vento-", L"Alas carmesies -Nuevo viento-", L"?? ?? ~??? ??~", L"紅之翼 -新之風-", L"????? ?????? - ???? ?????", L"Alye kryl'ya -Novyj veter-", L"Purpurrote Flugel -Neuer Wind-", L"Asas carmificadas -Novo vento-", L"Karmozijnrode vleugels -Nieuwe wind-", L"Szkar?atne skrzyd?a -Nowy wiatr-", L"K?z?l Kanatlar -Yeni Ruzgar-");
						break;
					case 8550:
						a = LL14(L"再会", L"Reunion", L"Retrouvailles", L"Riunione", L"Reencuentro", L"??", L"再會", L"?? ?????", L"Vstrecha", L"Wiedersehen", L"Reuniao", L"Reunie", L"Spotkanie", L"Yeniden Bulu?ma");
						break;
					case 8551:
						a = LL14(L"かけがえのない人へ", L"To Someone Irreplaceable", L"A une personne irremplacable", L"A qualcuno di insostituibile", L"A alguien insustituible", L"?? ?? ????", L"致無可取代的人", L"??? ??? ?? ???? ????????", L"Nezamenimomu cheloveku", L"Fur jemanden Unersetzlichen", L"Para alguem insubstituivel", L"Aan iemand die onvervangbaar is", L"Dla kogo? niezast?pionego", L"Yeri Doldurulamaz Birine");
						break;
					case 8552:
						a = LL14(L"惜しむように、愛おしむように", L"Cherishing, Treasuring", L"Cherir, proteger", L"Facendo tesoro, amando", L"Atesorando, amando", L"??? ??, ????? ??", L"依依不捨地，憐愛地", L"???? ??? ?????", L"Dorozha i lyubya", L"Hegen und pflegen", L"Estimando, amando", L"Koesterend, waarderend", L"Ceni?c, piel?gnuj?c", L"De?er Vererek, Severek");
						break;
					case 8553:
						a = LL14(L"ライノの花が咲く頃", L"When the Rhino Flower Blooms", L"Quand la fleur de rhino fleurit", L"Quando fiorisce il fiore di rino", L"Cuando florece la flor de rino", L"??? ?? ? ??", L"犀角花盛開之時", L"????? ???? ???? Rhino", L"Kogda cvetyot cvetok Rhino", L"Wenn die Rhino-Blute bluht", L"Quando a flor de rino floresce", L"Wanneer de rhino-bloem bloeit", L"Kiedy zakwita kwiat Rhino", L"Rhino Cice?i Act???nda");
						break;
					case 8555:
						a = LL14(L"戦場の掟", L"Rules of Battlefield", L"Regles du champ de bataille", L"Regole del campo di battaglia", L"Reglas del campo de batalla", L"??? ??", L"戰場規則", L"????? ???? ???????", L"Zakony polya boya", L"Regeln des Schlachtfelds", L"Regras do campo de batalha", L"Regels van het slagveld", L"Zasady pola walki", L"Sava? Alan? Kurallar?");
						break;
					case 8556:
						a = L"Remaining Glow";
						break;
					case 8557:
						a = LL14(L"深淵の魔女", L"Witch of the Abyss", L"Sorciere de l'abime", L"Strega dell'abisso", L"Bruja del abismo", L"??? ??", L"深淵魔女", L"????? ???????", L"Ved'ma bezdny", L"Hexe des Abgrunds", L"Bruxa do abismo", L"Heks van de afgrond", L"Wied?ma z otch?ani", L"Ucurum Cad?s?");
						break;
					case 8558:
						a = L"ALTINA";
						break;
					case 8559:
						a = LL14(L"威風", L"Dignity", L"Dignite", L"Dignita", L"Dignidad", L"??", L"威風", L"?????", L"Dostoinstvo", L"Wurde", L"Dignidade", L"Waardigheid", L"Godno??", L"Gorkem");
						break;
					case 8560:
						a = LL14(L"一撃に賭ける", L"Bet on One Strike", L"Parier sur un seul coup", L"Scommettere su un colpo solo", L"Apostar por un solo golpe", L"??? ??", L"賭在這一?上", L"???????? ??? ???? ?????", L"Stavit' na odin udar", L"Auf einen Schlag setzen", L"Apostar em um golpe", L"Gokken op een klap", L"Postawi? na jeden cios", L"Tek Vuru?a Guvenmek");
						break;
					case 8561:
						a = LL14(L"ユミル渓谷道", L"Ymir Valley Road", L"Route de la vallee d'Ymir", L"Strada della valle di Ymir", L"Camino del valle de Ymir", L"Ymir ???", L"Ymir 峽谷道", L"???? ???? Ymir", L"Doroga doliny Imir", L"Ymir-Talstrase", L"Caminho do vale de Ymir", L"Ymir-valleiweg", L"Droga przez dolin? Ymir", L"Ymir Vadi Yolu");
						break;
					case 8562:
						a = L"Awakening";
						break;
					case 8563:
						a = L"Blitzkrieg";
						break;
					case 8564:
						a = LL14(L"魔王の凱歌", L"Demon Lord's Triumph", L"Triomphe du seigneur demon", L"Trionfo del signore dei demoni", L"Triunfo del senor de los demonios", L"??? ??", L"魔王凱歌", L"?????? ??? ????????", L"Pobednyj marsh korolya demonov", L"Triumph des Damonenfursten", L"Triunfo do senhor demonio", L"Triomf van de demonenheer", L"Triumf w?adcy demonow", L"?blis Efendisinin Zaferi");
						break;
					case 8566:
						a = LL14(L"内なる黄昏", L"Inner Twilight", L"Crepuscule interieur", L"Crepuscolo interiore", L"Crepusculo interior", L"??? ??", L"?在的黄昏", L"????? ???????", L"Vnutrennie sumerki", L"Innere Dammerung", L"Crepusculo interior", L"Innerlijke schemering", L"Wewn?trzny zmierzch", L"?csel Alacakaranl?k");
						break;
					case 8567:
						a = LL14(L"蘇る記憶", L"Awakened Memories", L"Souvenirs eveilles", L"Memorie risvegliate", L"Memorias despertadas", L"????? ??", L"甦醒的記憶", L"?????? ???????", L"Probuzhdyonnye vospominaniya", L"Erwachte Erinnerungen", L"Memorias despertadas", L"Ontwaakte herinneringen", L"Obudzone wspomnienia", L"Uyanan An?lar");
						break;
					case 8570:
						a = LL14(L"静かな決意", L"Quiet Resolution", L"Resolution tranquille", L"Silenziosa risoluzione", L"Silenciosa resolucion", L"??? ??", L"平靜的決心", L"???? ????", L"Tihoe reshenie", L"Stille Entschlossenheit", L"Resolucao silenciosa", L"Stille vastberadenheid", L"Ciche postanowienie", L"Sessiz Kararl?l?k");
						break;
					case 8571:
						a = LL14(L"乾坤一擲", L"All or Nothing", L"Tout ou rien", L"Tutto o niente", L"Todo o nada", L"????", L"乾坤一擲", L"???? ?? ?? ???", L"Vsyyo ili nichego", L"Alles oder nichts", L"Tudo ou nada", L"Alles of niets", L"Wszystko albo nic", L"Ya Her ?ey Ya Hic");
						break;
					case 8572:
						a = LL14(L"交戦", L"Combat", L"Combat", L"Combattimento", L"Combate", L"??", L"交戰", L"??????", L"Srazhenie", L"Gefecht", L"Combate", L"Gevecht", L"Walka", L"Cat??ma");
						break;
					case 8573:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
						break;
					case 8600:
						a = LL14(L"大市の賑わい", L"Bustling Market", L"Marche anime", L"Mercato vivace", L"Mercado bullicioso", L"???? ??", L"大市集的熱鬧", L"??? ????", L"Ozhivlyonnyj rynok", L"Belebter Markt", L"Mercado movimentado", L"Bedrijvige markt", L"T?tni?cy ?yciem targ", L"Hareketli Pazar");
						break;
					case 8601:
						a = LL14(L"剣の遊戯", L"Sword Play", L"Jeu d'epee", L"Giuoco di spade", L"Juego de espadas", L"?? ??", L"劍之遊戲", L"??? ??????", L"Igra s mechami", L"Schwertspiel", L"Jogo de espadas", L"Zwaardspel", L"Szermierka", L"K?l?c Oyunu");
						break;
					case 8602:
						a = LL14(L"紙一重の攻防", L"Close Fight", L"Combat serre", L"Scontro serrato", L"Combate renido", L"??? ?? ??", L"千鈞一髮的攻防", L"???? ??????", L"Boy vplotnuyu", L"Knapper Kampf", L"Combate acirrado", L"Nipt gevecht", L"Zaci?ta walka", L"K?ran K?rana Mucadele");
						break;
					case 8603:
						a = LL14(L"走れマッハ号!", L"Run Mach Train!", L"Cours, train Mach!", L"Corri, treno Mach!", L"!Corre, tren Mach!", L"??? ?? ?!", L"奔?? Mach 號！", L"????? ?? ???? ???!", L"Begi, poezd Mah!", L"Lauf, Mach-Zug!", L"Corra, trem Mach!", L"Ren, Mach-trein!", L"P?d?, poci?gu Mach!", L"Ko? Mach Treni!");
						break;
					case 8605:
					case 8606:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
						break;
					case 8607:
						a = LL14(L"星屑のカンタータ", L"Cantata of Stardust", L"Cantate de poussiere d'etoiles", L"Cantata di polvere di stelle", L"Cantata de polvo de estrellas", L"??? ???", L"星塵大合唱", L"??????? ???? ??????", L"Kantata zvyozdnoj pyli", L"Kantate des Sternenstaubs", L"Cantata de poeira estelar", L"Cantate van sterrenstof", L"Kantata gwiezdnego py?u", L"Y?ld?z Tozu Kantat?");
						break;
					case 8608:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
						break;
					case 8609:
						a = L"Sonata No.45";
						break;
					case 8610:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
						break;
					case 8620:
						a = LL14(L"雪ウサギを追いかけて", L"Chasing the Snow Rabbit", L"Chasser le lapin des neiges", L"Inseguendo il coniglio di neve", L"Persiguiendo al conejo de nieve", L"???? ???", L"追逐雪兔", L"?????? ???? ?????", L"Presleduya snezhnogo krolika", L"Dem Schneehase hinterher", L"Perseguindo o coelho de neve", L"Het sneeuwkonijn achterna", L"Goni?c ?nie?nego krolika", L"Kar Tav?an?n?n Pe?inde");
						break;
					case 8621:
						a = L"Take The Windward!";
						break;
					case 8622:
					case 8623:
					case 8624:
					case 8625:
					case 8627:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
						break;
					case 8628:
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Neizvestno", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
						break;
					case 8629:
						a = LL14(L"効果音", L"Sound Effect", L"Effet sonore", L"Effetto sonoro", L"Efecto de sonido", L"???", L"音效", L"????? ????", L"Zvukovoj effekt", L"Soundeffekt", L"Efeito sonoro", L"Geluidseffect", L"Efekt d?wi?kowy", L"Ses Efekti");
						break;
					case 8700:
					case 8703:
					case 8704:
					case 8710:
					case 8711:
						a = LL14(L"音楽", L"Music", L"Musique", L"Musica", L"Musica", L"??", L"音樂", L"??????", L"Muzyka", L"Musik", L"Musica", L"Muziek", L"Muzyka", L"Muzik");
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
						L"001 Sentiments dansant avec le vent", L"100 Rolent - Ville provinciale", L"101 Bose - Ville commerciale", L"102 Ruan - Ville portuaire", L"103 Zeiss - Ville atelier", L"104 Grancel - Capitale royale", L"105 Chat au soleil", L"106 La patrouille frontiere n'est pas facile", L"107 Chateau royal", L"108 Grand Arena", L"108b Grand Arena (Sans intro)", L"200 Comment se deplacer a Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Errance dans les tenebres", L"301 Plancher d'acier bloquant le chemin", L"302 Paix des tenebres", L"303 Tours tetracycliques", L"304 Forteresse de Leiston", L"305 Terre vacante de lumiere", L"400 Sophisticated Fight -Combat rapide-", L"401 Sophisticated Fight -Combat commande-", L"402 To be Suggestive", L"403 Volonte d'argent", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Gardien du tresor", L"407 Ecrasement!!", L"408 Etoile defaillante", L"410 Pinch!!", L"500 Ou sont les etoiles Harmonica court", L"501 Amour d'ambre Hum Ver.(Japonais)", L"501e Amour d'ambre Hum Ver.", L"502 Amour d'ambre Piano Ver.", L"502b Amour d'ambre Piano Ver.1.5", L"503 Amour d'ambre Luth Ver.", L"504 Ou sont les etoiles Harmonica long", L"505 Allons gaiement", L"510 Decision de partir", L"511 Ceux qui agissent dans l'ombre", L"512 Ne le laissez pas s'echapper!", L"513 Dans mon c?ur", L"514 Sous le clair de lune", L"516 Crise rampante", L"517 Nous sommes la famille Capua!", L"518 Sentier du depart", L"519 Reprise", L"520 Liberation de la malediction, et...", L"521 Aveu", L"522 Orbement noir", L"523 Fierte de Liberl", L"530 Suite Madrigal de la Fleur Blanche - Souci de la princesse", L"531 Suite Madrigal - Lamentation des chevaliers", L"532 Suite Madrigal - Intentions de chacun", L"533 Suite Madrigal - Chateau", L"534 Suite Madrigal - Colisee", L"535 Suite Madrigal - Duel", L"536 Suite Madrigal - Mort de la princesse", L"537 Suite Madrigal - Grand final", L""
					};
					TCHAR ti1_de[][100] = {
						L"001 Gefuhle tanzend mit dem Wind", L"100 Rolent - Provinzstadt", L"101 Bose - Handelsstadt", L"102 Ruan - Hafenstadt", L"103 Zeiss - Werkstadt", L"104 Grancel - Konigshauptstadt", L"105 Katze in der Sonne", L"106 Grenzpatrouille ist nicht leicht", L"107 Konigsschloss", L"108 Grand Arena", L"108b Grand Arena (Ohne Intro)", L"200 Zu Fus durch Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Wandern in der Dunkelheit", L"301 Stahlboden versperrt den Weg", L"302 Frieden in der Dunkelheit", L"303 Tetrazyklische Turme", L"304 Leiston-Festung", L"305 Hohles Land des Lichts", L"400 Sophisticated Fight -Schneller Kampf-", L"401 Sophisticated Fight -Kommando-Kampf-", L"402 To be Suggestive", L"403 Silberner Wille", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Huter des Schatzes", L"407 Zerschmettern!!", L"408 Verblassender Stern", L"410 Pinch!!", L"500 Wo die Sterne sind Harmonica kurz", L"501 Bernstein-Liebe Hum Ver.(Japanisch)", L"501e Bernstein-Liebe Hum Ver.", L"502 Bernstein-Liebe Klavier Ver.", L"502b Bernstein-Liebe Klavier Ver.1.5", L"503 Bernstein-Liebe Laute Ver.", L"504 Wo die Sterne sind Harmonica lang", L"505 Gehen wir frohlich", L"510 Entschlossenheit zu gehen", L"511 Die im Schatten handeln", L"512 Lasst ihn nicht entkommen!", L"513 In meinem Herzen", L"514 Im Mondschein", L"516 Schleichende Krise", L"517 Wir sind die Capua-Familie!", L"518 Pfad des Aufbruchs", L"519 Ruckeroberung", L"520 Befreiung vom Fluch, und...", L"521 Gestandnis", L"522 Schwarzer Ouroboros", L"523 Stolz von Liberl", L"530 Suite Madrigal der Weisen Blume - Sorge der Prinzessin", L"531 Suite Madrigal - Klage der Ritter", L"532 Suite Madrigal - Jeder sein Plan", L"533 Suite Madrigal - Schloss", L"534 Suite Madrigal - Kolosseum", L"535 Suite Madrigal - Duell", L"536 Suite Madrigal - Tod der Prinzessin", L"537 Suite Madrigal - Groser Schluss", L""
					};
					TCHAR ti1_es[][100] = {
						L"001 Sentimientos bailando con el viento", L"100 Rolent - Ciudad provincial", L"101 Bose - Ciudad comercial", L"102 Ruan - Ciudad portuaria", L"103 Zeiss - Ciudad taller", L"104 Grancel - Capital real", L"105 Gato al sol", L"106 La patrulla fronteriza no es facil", L"107 Castillo real", L"108 Grand Arena", L"108b Grand Arena (Sin intro)", L"200 Como caminar por Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Vagando en la oscuridad", L"301 Piso de acero bloqueando el camino", L"302 Paz en la oscuridad", L"303 Torres tetracyclic", L"304 Fortaleza Leiston", L"305 Tierra vacia de luz", L"400 Sophisticated Fight -Batalla rapida-", L"401 Sophisticated Fight -Batalla comando-", L"402 To be Suggestive", L"403 Voluntad de plata", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Guardian del tesoro", L"407 !!Aplastar!!", L"408 Estrella desvaneciente", L"410 Pinch!!", L"500 Donde estan las estrellas Harmonica corto", L"501 Amor ambar Hum Ver.(Japones)", L"501e Amor ambar Hum Ver.", L"502 Amor ambar Piano Ver.", L"502b Amor ambar Piano Ver.1.5", L"503 Amor ambar Laud Ver.", L"504 Donde estan las estrellas Harmonica largo", L"505 Vamos alegres", L"510 Determinacion de partir", L"511 Los que actuan en la sombra", L"512 !No lo dejes escapar!", L"513 En mi corazon", L"514 Bajo la luna", L"516 Crisis creciente", L"517 !Somos la familia Capua!", L"518 Camino de partida", L"519 Recaptura", L"520 Liberacion de la maldicion, y...", L"521 Confesion", L"522 Orbement negro", L"523 Orgullo de Liberl", L"530 Suite Madrigal de la Flor Blanca - Preocupacion de la princesa", L"531 Suite Madrigal - Lamento de los caballeros", L"532 Suite Madrigal - Intenciones de cada uno", L"533 Suite Madrigal - Castillo", L"534 Suite Madrigal - Coliseo", L"535 Suite Madrigal - Duelo", L"536 Suite Madrigal - Muerte de la princesa", L"537 Suite Madrigal - Gran final", L""
					};
					TCHAR ti1_it[][100] = {
						L"001 Sentimenti danzanti con il vento", L"100 Rolent - Citta provinciale", L"101 Bose - Citta commerciale", L"102 Ruan - Citta portuale", L"103 Zeiss - Citta officina", L"104 Grancel - Capitale reale", L"105 Gatto al sole", L"106 La pattuglia di frontiera non e facile", L"107 Castello reale", L"108 Grand Arena", L"108b Grand Arena (Senza intro)", L"200 Come camminare a Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Vagando nelle tenebre", L"301 Pavimento d'acciaio che blocca il sentiero", L"302 Pace nelle tenebre", L"303 Torri tetracyclic", L"304 Fortezza Leiston", L"305 Terra vuota di luce", L"400 Sophisticated Fight -Battaglia rapida-", L"401 Sophisticated Fight -Battaglia comando-", L"402 To be Suggestive", L"403 Volonta d'argento", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Guardiano del tesoro", L"407 Schiacciare!!", L"408 Stella che svanisce", L"410 Pinch!!", L"500 Dove sono le stelle Fisarmonica corto", L"501 Amore ambra Hum Ver.(Giapponese)", L"501e Amore ambra Hum Ver.", L"502 Amore ambra Piano Ver.", L"502b Amore ambra Piano Ver.1.5", L"503 Amore ambra Liuto Ver.", L"504 Dove sono le stelle Fisarmonica lungo", L"505 Andiamo allegri", L"510 Determinazione a partire", L"511 Coloro che agiscono nell'ombra", L"512 Non lasciarlo scappare!", L"513 Nel mio cuore", L"514 Sotto la luna", L"516 Crisi strisciante", L"517 Siamo la famiglia Capua!", L"518 Sentiero di partenza", L"519 Riconquista", L"520 Liberazione dalla maledizione, e...", L"521 Confessione", L"522 Orbement nero", L"523 Orgoglio di Liberl", L"530 Suite Madrigal del Fiore Bianco - Preoccupazione della principessa", L"531 Suite Madrigal - Lamento dei cavalieri", L"532 Suite Madrigal - Intenzioni di ciascuno", L"533 Suite Madrigal - Castello", L"534 Suite Madrigal - Colosseo", L"535 Suite Madrigal - Duello", L"536 Suite Madrigal - Morte della principessa", L"537 Suite Madrigal - Gran finale", L""
					};
					TCHAR ti1_ko[][100] = {
						L"001 ??? ?? ??? ??", L"100 ???? ???", L"101 ???? ??", L"102 ???? ??", L"103 ???? ??", L"104 ?? ???", L"105 ???? ???? ???", L"106 ?? ??? ?? ??", L"107 ??", L"108 ?? ???", L"108b ?? ??? (??? ??)", L"200 ??? ?? ?", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 ??? ????", L"301 ???? ??? ??", L"302 ??? ? ??", L"303 ??? ?", L"304 ???? ??", L"305 ??? ?? ??", L"400 Sophisticated Fight -? ??-", L"401 Sophisticated Fight -??? ??-", L"402 To be Suggestive", L"403 ?? ??", L"404 Challenger Invited", L"405 Ancient Makes", L"406 ??? ???? ?", L"407 ??!!", L"408 ????? ?", L"410 Pinch!!", L"500 ?? ?? ? ???? short Ver.", L"501 ??? ?? Hum Ver.(???)", L"501e ??? ?? Hum Ver.", L"502 ??? ?? Piano Ver.", L"502b ??? ?? Piano Ver.1.5", L"503 ??? ?? ?? Ver.", L"504 ?? ?? ? ???? long Ver.", L"505 ??? ??", L"510 ???? ??", L"511 ????? ???? ??", L"512 ?? ??? ?!", L"513 ?? ??", L"514 ?? ????", L"516 ???? ??", L"517 ?? ??? ??!", L"518 ??? ???", L"519 ??", L"520 ?????? ??, ???...", L"521 ??", L"522 ?? ????", L"523 ??? ???", L"530 ?? ??? ???? - ??? ??", L"531 ?? ??? ???? - ???? ??", L"532 ?? ??? ???? - ??? ??", L"533 ?? ??? ???? - ?", L"534 ?? ??? ???? - ????", L"535 ?? ??? ???? - ??", L"536 ?? ??? ???? - ??? ??", L"537 ?? ??? ???? - ???", L""
					};
					TCHAR ti1_zh[][100] = {
						L"001 与?共舞的心", L"100 地方都市洛?特", L"101 商?都市柏斯", L"102 海港都市?安", L"103 工房都市蔡斯", L"104 王都格???", L"105 ?光下的猫", L"106 国境警?也不?松", L"107 王城", L"108 格??技?", L"108b 格??技?(无前奏)", L"200 利??的?道", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 彷徨于黑暗中", L"301 阻?去路的??之床", L"302 黑暗?来的安?", L"303 四?之塔", L"304 雷斯塔要塞", L"305 虚渺之光封土", L"400 Sophisticated Fight -快速?斗-", L"401 Sophisticated Fight -指令?斗-", L"402 To be Suggestive", L"403 ?之意志", L"404 Challenger Invited", L"405 Ancient Makes", L"406 至宝守?者", L"407 ?破!!", L"408 消逝之星", L"410 Pinch!!", L"500 星之所在 口琴short Ver.", L"501 琥珀之? Hum Ver.(日?)", L"501e 琥珀之? Hum Ver.", L"502 琥珀之? ?琴 Ver.", L"502b 琥珀之? ?琴 Ver.1.5", L"503 琥珀之? ?特琴 Ver.", L"504 星之所在 口琴long Ver.", L"505 ??地出?", L"510 ?去的决意", L"511 暗中行?者?", L"512 ??他逃了!", L"513 心中", L"514 月光下", L"516 悄悄逼近的危机", L"517 我?是?普?一家!", L"518 ?程小路", L"519 ??", L"520 从?咒中解放,然后...", L"521 告白", L"522 黑色?力器", L"523 利??的?傲", L"530 ?曲 白花之恋曲 - 公主的??", L"531 ?曲 白花之恋曲 - ?士?的?息", L"532 ?曲 白花之恋曲 - 各自的思?", L"533 ?曲 白花之恋曲 - 城堡", L"534 ?曲 白花之恋曲 - ?技?", L"535 ?曲 白花之恋曲 - 决斗", L"536 ?曲 白花之恋曲 - 公主之死", L"537 ?曲 白花之恋曲 - 大??", L""
					};
					TCHAR ti1_ar[][100] = {
						L"001 ????? ????? ?? ??????", L"100 ????? - ????? ???????", L"101 ??? - ????? ??????", L"102 ???? - ????? ?????", L"103 ???? - ????? ????", L"104 ?????? - ??????? ???????", L"105 ?? ?? ?????", L"106 ????? ?????? ???? ????", L"107 ????? ??????", L"108 Grand Arena", L"108b Grand Arena (???? ?????)", L"200 ????? ????? ?? ?????", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 ????? ?? ??????", L"301 ????? ??????? ????? ??????", L"302 ?????? ?? ??????", L"303 ??????? ????????", L"304 ??? ??????", L"305 ??? ????? ???????", L"400 Sophisticated Fight -????? ?????-", L"401 Sophisticated Fight -????? ?????-", L"402 To be Suggestive", L"403 ??????? ??????", L"404 Challenger Invited", L"405 Ancient Makes", L"406 ???? ?????", L"407 ???!!", L"408 ???? ?????", L"410 Pinch!!", L"500 ??? ?????? ????????? ????", L"501 ?? ???????? Hum Ver.(??????)", L"501e ?? ???????? Hum Ver.", L"502 ?? ???????? ????? Ver.", L"502b ?? ???????? ????? Ver.1.5", L"503 ?? ???????? ??? Ver.", L"504 ??? ?????? ????????? ????", L"505 ????? ????", L"510 ????? ??? ????????", L"511 ?? ??????? ?? ????", L"512 ?? ????? ????!", L"513 ?? ????", L"514 ??? ??? ?????", L"516 ???? ?????", L"517 ??? ????? ?????!", L"518 ???? ??????", L"519 ???????", L"520 ?????? ?? ??????? ?...", L"521 ??????", L"522 Orbement ????", L"523 ??? ?????", L"530 Suite Madrigal ?????? ??????? - ??? ???????", L"531 Suite Madrigal - ???? ???????", L"532 Suite Madrigal - ????? ?? ????", L"533 Suite Madrigal - ??????", L"534 Suite Madrigal - ??????????", L"535 Suite Madrigal - ??????", L"536 Suite Madrigal - ??? ???????", L"537 Suite Madrigal - ??????? ??????", L""
					};
					TCHAR ti1_ru[][100] = {
						L"001 Чувства, танцующие с ветром", L"100 Ролент - Провинциальный город", L"101 Бос - Торговый город", L"102 Руан - Портовый город", L"103 Цейсс - Город мастерских", L"104 Грансель - Королевская столица", L"105 Кот на солнце", L"106 Пограничный патруль нелёгок", L"107 Королевский замок", L"108 Grand Arena", L"108b Grand Arena (Без вступления)", L"200 Как ходить по Либерлу", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Блуждание во тьме", L"301 Стальной пол преграждает путь", L"302 Покой во тьме", L"303 Тетрациклические башни", L"304 Крепость Лейстон", L"305 Пустая земля света", L"400 Sophisticated Fight -Быстрый бой-", L"401 Sophisticated Fight -Командный бой-", L"402 To be Suggestive", L"403 Серебряная воля", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Страж сокровища", L"407 Разгром!!", L"408 Исчезающая звезда", L"410 Pinch!!", L"500 Где звёзды Гармоника короткая", L"501 Янтарная любовь Hum Ver.(Японский)", L"501e Янтарная любовь Hum Ver.", L"502 Янтарная любовь Фортепиано Ver.", L"502b Янтарная любовь Фортепиано Ver.1.5", L"503 Янтарная любовь Лютня Ver.", L"504 Где звёзды Гармоника длинная", L"505 Пойдём весело", L"510 Решимость уйти", L"511 Действующие в тени", L"512 Не дай ему сбежать!", L"513 В моём сердце", L"514 Под лунным светом", L"516 Надвигающийся кризис", L"517 Мы семья Капуа!", L"518 Тропа отбытия", L"519 Захват", L"520 Освобождение от проклятия, и...", L"521 Признание", L"522 Чёрный Orbment", L"523 Гордость Либерла", L"530 Сюита Мадригал Белого Цветка - Забота принцессы", L"531 Сюита Мадригал - Плач рыцарей", L"532 Сюита Мадригал - Замыслы каждого", L"533 Сюита Мадригал - Замок", L"534 Сюита Мадригал - Колизей", L"535 Сюита Мадригал - Поединок", L"536 Сюита Мадригал - Смерть принцессы", L"537 Сюита Мадригал - Большой финал", L""
					};
					TCHAR ti1_pt[][100] = {
						L"001 Sentimentos dancando com o vento", L"100 Rolent - Cidade provincial", L"101 Bose - Cidade comercial", L"102 Ruan - Cidade portuaria", L"103 Zeiss - Cidade oficina", L"104 Grancel - Capital real", L"105 Gato ao sol", L"106 A patrulha de fronteira nao e facil", L"107 Castelo real", L"108 Grand Arena", L"108b Grand Arena (Sem intro)", L"200 Como andar por Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Vagando na escuridao", L"301 Piso de aco bloqueando o caminho", L"302 Paz na escuridao", L"303 Torres tetraclic", L"304 Fortaleza Leiston", L"305 Terra vazia de luz", L"400 Sophisticated Fight -Batalha rapida-", L"401 Sophisticated Fight -Batalha comando-", L"402 To be Suggestive", L"403 Vontade de prata", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Guardiao do tesouro", L"407 Esmagar!!", L"408 Estrela desvanecente", L"410 Pinch!!", L"500 Onde estao as estrelas Harmonica curto", L"501 Amor ambar Hum Ver.(Japones)", L"501e Amor ambar Hum Ver.", L"502 Amor ambar Piano Ver.", L"502b Amor ambar Piano Ver.1.5", L"503 Amor ambar Alaude Ver.", L"504 Onde estao as estrelas Harmonica longo", L"505 Vamos animados", L"510 Determinacao de partir", L"511 Os que agem nas sombras", L"512 Nao o deixe escapar!", L"513 No meu coracao", L"514 Sob o luar", L"516 Crise rastejante", L"517 Somos a familia Capua!", L"518 Caminho da partida", L"519 Recaptura", L"520 Libertacao da maldicao, e...", L"521 Confissao", L"522 Orbement negro", L"523 Orgulho de Liberl", L"530 Suite Madrigal da Flor Branca - Preocupacao da princesa", L"531 Suite Madrigal - Lamento dos cavaleiros", L"532 Suite Madrigal - Intencoes de cada um", L"533 Suite Madrigal - Castelo", L"534 Suite Madrigal - Coliseu", L"535 Suite Madrigal - Duelo", L"536 Suite Madrigal - Morte da princesa", L"537 Suite Madrigal - Grande final", L""
					};
					TCHAR ti1_nl[][100] = {
						L"001 Gevoelens dansend met de wind", L"100 Rolent - Provinciestad", L"101 Bose - Handelsstad", L"102 Ruan - Havenstad", L"103 Zeiss - Werkplaatsstad", L"104 Grancel - Koninklijke hoofdstad", L"105 Kat in de zon", L"106 Grenspatrouille is niet gemakkelijk", L"107 Koninklijk kasteel", L"108 Grand Arena", L"108b Grand Arena (Zonder intro)", L"200 Hoe te lopen in Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Dwalen in de duisternis", L"301 Stalen vloer blokkeert het pad", L"302 Vrede in de duisternis", L"303 Tetracyclische torens", L"304 Leiston vesting", L"305 Leeg land van licht", L"400 Sophisticated Fight -Snelle gevecht-", L"401 Sophisticated Fight -Commando gevecht-", L"402 To be Suggestive", L"403 Zilveren wil", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Bewaker van de schat", L"407 Verpletteren!!", L"408 Vervagende ster", L"410 Pinch!!", L"500 Waar de sterren zijn Harmonica kort", L"501 Amberliefde Hum Ver.(Japans)", L"501e Amberliefde Hum Ver.", L"502 Amberliefde Piano Ver.", L"502b Amberliefde Piano Ver.1.5", L"503 Amberliefde Luit Ver.", L"504 Waar de sterren zijn Harmonica lang", L"505 Laten we vrolijk gaan", L"510 Vastberadenheid om te vertrekken", L"511 Degenen in de schaduw", L"512 Laat hem niet ontsnappen!", L"513 In mijn hart", L"514 Onder het maanlicht", L"516 Sluipende crisis", L"517 Wij zijn de Capua-familie!", L"518 Pad van vertrek", L"519 Herovering", L"520 Bevrijding van de vloek, en...", L"521 Biecht", L"522 Zwarte Orbment", L"523 Trots van Liberl", L"530 Suite Madrigal van de Witte Bloem - Zorg van de prinses", L"531 Suite Madrigal - Klaagzang van ridders", L"532 Suite Madrigal - Intenties van iedereen", L"533 Suite Madrigal - Kasteel", L"534 Suite Madrigal - Colosseum", L"535 Suite Madrigal - Duel", L"536 Suite Madrigal - Dood van prinses", L"537 Suite Madrigal - Grote finale", L""
					};
					TCHAR ti1_pl[][100] = {
						L"001 Uczucia ta?cz?ce z wiatrem", L"100 Rolent - Miasto prowincjonalne", L"101 Bose - Miasto handlowe", L"102 Ruan - Miasto portowe", L"103 Zeiss - Miasto warsztatow", L"104 Grancel - Stolica krolewska", L"105 Kot w s?o?cu", L"106 Patrol graniczny nie jest ?atwy", L"107 Zamek krolewski", L"108 Grand Arena", L"108b Grand Arena (Bez wst?pu)", L"200 Jak chodzi? po Liberl", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 B??dzenie w ciemno?ci", L"301 Stalowa pod?oga blokuj?ca drog?", L"302 Pokoj w ciemno?ci", L"303 Wie?e tetracyclic", L"304 Twierdza Leiston", L"305 Pusta ziemia ?wiat?a", L"400 Sophisticated Fight -Szybka bitwa-", L"401 Sophisticated Fight -Bitwa komendy-", L"402 To be Suggestive", L"403 Srebrna wola", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Stra?nik skarbu", L"407 Zniszczy?!!", L"408 Gasn?ca gwiazda", L"410 Pinch!!", L"500 Gdzie s? gwiazdy Harmonica krotka", L"501 Bursztynowa mi?o?? Hum Ver.(Japo?ski)", L"501e Bursztynowa mi?o?? Hum Ver.", L"502 Bursztynowa mi?o?? Fortepian Ver.", L"502b Bursztynowa mi?o?? Fortepian Ver.1.5", L"503 Bursztynowa mi?o?? Lutnia Ver.", L"504 Gdzie s? gwiazdy Harmonica d?uga", L"505 Id?my weso?o", L"510 Determinacja do odej?cia", L"511 Ci dzia?aj?cy w cieniu", L"512 Nie daj mu uciec!", L"513 W moim sercu", L"514 W ?wietle ksi??yca", L"516 Pe?zaj?cy kryzys", L"517 Jeste?my rodzin? Capua!", L"518 ?cie?ka odej?cia", L"519 Odzyskanie", L"520 Wyzwolenie od kl?twy, i...", L"521 Wyznanie", L"522 Czarny Orbment", L"523 Duma Liberl", L"530 Suita Madryga? Bia?ego Kwiatu - Troska ksi??niczki", L"531 Suita Madryga? - Lament rycerzy", L"532 Suita Madryga? - Zamiary ka?dego", L"533 Suita Madryga? - Zamek", L"534 Suita Madryga? - Koloseum", L"535 Suita Madryga? - Pojedynek", L"536 Suita Madryga? - ?mier? ksi??niczki", L"537 Suita Madryga? - Wielki fina?", L""
					};
					TCHAR ti1_tr[][100] = {
						L"001 Ruzgarla dans eden duygular", L"100 Rolent - ?l ?ehri", L"101 Bose - Ticaret ?ehri", L"102 Ruan - Liman ?ehri", L"103 Zeiss - Atolye ?ehri", L"104 Grancel - Kraliyet ba?kenti", L"105 Gune?te kedi", L"106 S?n?r devriyesi kolay de?il", L"107 Kraliyet kalesi", L"108 Grand Arena", L"108b Grand Arena (Intro yok)", L"200 Liberl'de nas?l yurunur", L"201 Secret Green Passage", L"202 Rock on the Road", L"300 Karanl?kta dola?ma", L"301 Yolu kapatan celik zemin", L"302 Karanl?ktaki huzur", L"303 Dortlu kuleler", L"304 Leiston kalesi", L"305 I??k bo? arazisi", L"400 Sophisticated Fight -H?zl? sava?-", L"401 Sophisticated Fight -Komut sava??-", L"402 To be Suggestive", L"403 Gumu? irade", L"404 Challenger Invited", L"405 Ancient Makes", L"406 Hazine bekcisi", L"407 Ez!!", L"408 Solan y?ld?z", L"410 Pinch!!", L"500 Y?ld?zlar?n oldu?u yer M?z?ka k?sa", L"501 Kehribar a?k? Hum Ver.(Japonca)", L"501e Kehribar a?k? Hum Ver.", L"502 Kehribar a?k? Piyano Ver.", L"502b Kehribar a?k? Piyano Ver.1.5", L"503 Kehribar a?k? Lut Ver.", L"504 Y?ld?zlar?n oldu?u yer M?z?ka uzun", L"505 Ne?eyle gidelim", L"510 Ayr?lma kararl?l???", L"511 Golgelerde hareket edenler", L"512 Kacmas?na izin verme!", L"513 Kalbimde", L"514 Ay ????? alt?nda", L"516 Sinsice yakla?an kriz", L"517 Biz Capua ailesiyiz!", L"518 Ayr?l?? yolu", L"519 Geri alma", L"520 Lanetten kurtulma, ve...", L"521 ?tiraf", L"522 Siyah Orbment", L"523 Liberl gururu", L"530 Beyaz Cicek Madrigal Suiti - Prenses endi?esi", L"531 Madrigal Suiti - ?ovalyelerin a??t?", L"532 Madrigal Suiti - Herkesin niyeti", L"533 Madrigal Suiti - Kale", L"534 Madrigal Suiti - Kolezyum", L"535 Madrigal Suiti - Duello", L"536 Madrigal Suiti - Prensesin olumu", L"537 Madrigal Suiti - Buyuk final", L""
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
					if (f.Open(fname, CFile::modeRead | CFile::shareDenyWrite)) {
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
									a += LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
									break;
								}
								if (s2.Left(4).Trim() == s1) {
									a = PL_FC_Track(j).Mid(4);
									aa1a = CString(ti1[j]).Left(4).Trim();
									if (aa1a == L"501e") {
										if (ft == L"bgm1.pac") a += L"(English)";
										if (ft == L"bgm2.pac") a += L"(English)";
										if (ft == L"bgm3.pac") a += LL14(L"(日本語)", L"(Japanese)", L"(Japonais)", L"(Giapponese)", L"(Japones)", L"(???)", L"(日本語)", L"(?????????)", L"(Японский)", L"(Japanisch)", L"(Japones)", L"(Japans)", L"(Japo?ski)", L"(Japonca)");
									}
									break;
								}
							}

							_tcscpy(p.name, a);
							_tcscpy(p.fol, fname + L"::" + aa1a + a);
							p.alb[0] = 0;
							p.art[0] = 0;

							if (ft == L"bgm1.pac") {
								wcscpy(p.art, LL14(L"steam版 空の軌跡 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Les Sentiers du Ciel 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam ??? ?? 1st bgm1.pac", L"Steam 空之?迹 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Тропы в Небе 1st bgm1.pac", L"Steam Himmelsleitern 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac", L"Steam Trails in the Sky 1st bgm1.pac"));
								wcscpy(p.alb, LL14(L"BGM:標準", L"BGM:Standard", L"BGM:Standard", L"BGM:Standard", L"BGM:Estandar", L"BGM:??", L"BGM:標準", L"BGM:?????", L"BGM:Стандарт", L"BGM:Standard", L"BGM:Padrao", L"BGM:Standaard", L"BGM:Standard", L"BGM:Standart"));
							}
							if (ft == L"bgm2.pac") {
								wcscpy(p.art, LL14(L"steam版 空の軌跡 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Les Sentiers du Ciel 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam ??? ?? 1st bgm2.pac", L"Steam 空之?迹 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Тропы в Небе 1st bgm2.pac", L"Steam Himmelsleitern 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac", L"Steam Trails in the Sky 1st bgm2.pac"));
								wcscpy(p.alb, LL14(L"BGM:アレンジ", L"BGM:Arrange", L"BGM:Arrangement", L"BGM:Arrangiamento", L"BGM:Arreglo", L"BGM:????", L"BGM:改編", L"BGM:?????", L"BGM:Аранжировка", L"BGM:Arrange", L"BGM:Arranjo", L"BGM:Arrange", L"BGM:Aran?acja", L"BGM:Aranjman"));
							}
							if (ft == L"bgm3.pac") {
								wcscpy(p.art, LL14(L"steam版 空の軌跡 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Les Sentiers du Ciel 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam ??? ?? 1st bgm3.pac", L"Steam 空之?迹 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Тропы в Небе 1st bgm3.pac", L"Steam Himmelsleitern 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac", L"Steam Trails in the Sky 1st bgm3.pac"));
								wcscpy(p.alb, LL14(L"BGM:オリジナル", L"BGM:Original", L"BGM:Original", L"BGM:Originale", L"BGM:Original", L"BGM:????", L"BGM:原創", L"BGM:????", L"BGM:Оригинал", L"BGM:Original", L"BGM:Original", L"BGM:Origineel", L"BGM:Orygina?", L"BGM:Orijinal"));
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
						a = LL14(L"Operation SANDRAS(重低音)", L"Operation SANDRAS (Bass Boost)", L"Operation SANDRAS (Renfort graves)", L"Operation SANDRAS (Rinforzo bassi)", L"Operation SANDRAS (Refuerzo graves)", L"Operation SANDRAS (?? ??)", L"Operation SANDRAS (重低音)", L"Operation SANDRAS (????? ??????)", L"Operation SANDRAS (Усиление низких)", L"Operation SANDRAS (Bassverstarkung)", L"Operation SANDRAS (Reforco graves)", L"Operation SANDRAS (Basversterking)", L"Operation SANDRAS (Wzmocnienie basow)", L"Operation SANDRAS (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b100.opus") {
						a = L"Overblaze";
						fff = 1;
					}
					if (ft == L"y_b100_s1.opus") {
						a = LL14(L"Overblaze(重低音)", L"Overblaze (Bass Boost)", L"Overblaze (Renfort graves)", L"Overblaze (Rinforzo bassi)", L"Overblaze (Refuerzo graves)", L"Overblaze (?? ??)", L"Overblaze (重低音)", L"Overblaze (????? ??????)", L"Overblaze (Усиление низких)", L"Overblaze (Bassverstarkung)", L"Overblaze (Reforco graves)", L"Overblaze (Basversterking)", L"Overblaze (Wzmocnienie basow)", L"Overblaze (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b200.opus") {
						a = L"Through the North Wind";
						fff = 1;
					}
					if (ft == L"y_b200_s1.opus") {
						a = LL14(L"Through the North Wind(重低音)", L"Through the North Wind (Bass Boost)", L"Through the North Wind (Renfort graves)", L"Through the North Wind (Rinforzo bassi)", L"Through the North Wind (Refuerzo graves)", L"Through the North Wind (?? ??)", L"Through the North Wind (重低音)", L"Through the North Wind (????? ??????)", L"Through the North Wind (Усиление низких)", L"Through the North Wind (Bassverstarkung)", L"Through the North Wind (Reforco graves)", L"Through the North Wind (Basversterking)", L"Through the North Wind (Wzmocnienie basow)", L"Through the North Wind (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b210.opus") {
						a = LL14(L"高鳴る鼓動", L"Pounding Heartbeat", L"Battement de c?ur saccade", L"Battito accelerato", L"Latido palpitante", L"???? ??", L"劇烈的心跳", L"????? ????? ?????????", L"Учащенное сердцебиение", L"Pochendes Herzklopfen", L"Batida forte do coracao", L"Bonzend hart", L"?omocz?ce serce", L"Kut Kut Atan Kalp");
						fff = 1;
					}
					if (ft == L"y_b210_s1.opus") {
						a = LL14(L"高鳴る鼓動(重低音)", L"Pounding Heartbeat (Bass Boost)", L"Pounding Heartbeat (Renfort graves)", L"Pounding Heartbeat (Rinforzo bassi)", L"Pounding Heartbeat (Refuerzo graves)", L"Pounding Heartbeat (?? ??)", L"Pounding Heartbeat (重低音)", L"Pounding Heartbeat (????? ??????)", L"Pounding Heartbeat (Усиление низких)", L"Pounding Heartbeat (Bassverstarkung)", L"Pounding Heartbeat (Reforco graves)", L"Pounding Heartbeat (Basversterking)", L"Pounding Heartbeat (Wzmocnienie basow)", L"Pounding Heartbeat (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b300.opus") {
						a = LL14(L"石火の如く", L"Like Flint", L"Comme le silex", L"Come la selce", L"Como el silex", L"?????", L"如同火石", L"??? ??????", L"Словно кремень", L"Wie Feuerstein", L"Como pederneira", L"Als vuursteen", L"Jak krzemie?", L"Cakmak Ta?? Gibi");
						fff = 1;
					}
					if (ft == L"y_b300_s1.opus") {
						a = LL14(L"石火の如く(重低音)", L"Like Flint (Bass Boost)", L"Like Flint (Renfort graves)", L"Like Flint (Rinforzo bassi)", L"Like Flint (Refuerzo graves)", L"Like Flint (?? ??)", L"Like Flint (重低音)", L"Like Flint (????? ??????)", L"Like Flint (Усиление низких)", L"Like Flint (Bassverstarkung)", L"Like Flint (Reforco graves)", L"Like Flint (Basversterking)", L"Like Flint (Wzmocnienie basow)", L"Like Flint (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b400.opus") {
						a = L"Can You Do It";
						fff = 1;
					}
					if (ft == L"y_b400_s1.opus") {
						a = LL14(L"Can You Do It(重低音)", L"Can You Do It (Bass Boost)", L"Can You Do It (Renfort graves)", L"Can You Do It (Rinforzo bassi)", L"Can You Do It (Refuerzo graves)", L"Can You Do It (?? ??)", L"Can You Do It (重低音)", L"Can You Do It (????? ??????)", L"Can You Do It (Усиление низких)", L"Can You Do It (Bassverstarkung)", L"Can You Do It (Reforco graves)", L"Can You Do It (Basversterking)", L"Can You Do It (Wzmocnienie basow)", L"Can You Do It (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b500.opus") {
						a = LL14(L"BERSERK -戦斧の咆哮-", L"BERSERK -Roar of the Battle Axe-", L"BERSERK -Rugissement de la hache de guerre-", L"BERSERK -Ruggito dell'ascia da battaglia-", L"BERSERK -Rugido del hacha de batalla-", L"BERSERK -??? ??-", L"BERSERK -戰斧的咆哮-", L"BERSERK - ???? ??? ???????", L"BERSERK -Рев боевого топора-", L"BERSERK -Brullen der Streitaxt-", L"BERSERK -Rugido do machado de batalha-", L"BERSERK -Geknal van de strijdbijl-", L"BERSERK -Ryk topora wojennego-", L"BERSERK -Sava? Baltas?n?n Kukreyi?i-");
						fff = 1;
					}
					if (ft == L"y_b500_s1.opus") {
						a = LL14(L"BERSERK -戦斧の咆哮-(重低音)", L"BERSERK -Roar of the Battle Axe- (Bass Boost)", L"BERSERK -Roar of the Battle Axe- (Renfort graves)", L"BERSERK -Roar of the Battle Axe- (Rinforzo bassi)", L"BERSERK -Roar of the Battle Axe- (Refuerzo graves)", L"BERSERK -Roar of the Battle Axe- (?? ??)", L"BERSERK -Roar of the Battle Axe- (重低音)", L"BERSERK -Roar of the Battle Axe- (????? ??????)", L"BERSERK -Roar of the Battle Axe- (Усиление низких)", L"BERSERK -Roar of the Battle Axe- (Bassverstarkung)", L"BERSERK -Roar of the Battle Axe- (Reforco graves)", L"BERSERK -Roar of the Battle Axe- (Basversterking)", L"BERSERK -Roar of the Battle Axe- (Wzmocnienie basow)", L"BERSERK -Roar of the Battle Axe- (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b510.opus") {
						a = LL14(L"悪意の洗礼", L"Baptism of Malice", L"Bapteme de malice", L"Battesimo di malizia", L"Bautismo de malicia", L"??? ??", L"惡意的洗禮", L"??????? ?????", L"Крещение злобой", L"Taufe der Bosheit", L"Batismo de malicia", L"Doop van kwaadaardigheid", L"Chrzest z?o?liwo?ci", L"Garez Vaftizi");
						fff = 1;
					}
					if (ft == L"y_b510_s1.opus") {
						a = LL14(L"悪意の洗礼(重低音)", L"Baptism of Malice (Bass Boost)", L"Baptism of Malice (Renfort graves)", L"Baptism of Malice (Rinforzo bassi)", L"Baptism of Malice (Refuerzo graves)", L"Baptism of Malice (?? ??)", L"Baptism of Malice (重低音)", L"Baptism of Malice (????? ??????)", L"Baptism of Malice (Усиление низких)", L"Baptism of Malice (Bassverstarkung)", L"Baptism of Malice (Reforco graves)", L"Baptism of Malice (Basversterking)", L"Baptism of Malice (Wzmocnienie basow)", L"Baptism of Malice (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b520.opus") {
						a = L"The Ultimate Pleasure in My Hands";
						fff = 1;
					}
					if (ft == L"y_b520_s1.opus") {
						a = LL14(L"The Ultimate Pleasure in My Hands(重低音)", L"The Ultimate Pleasure in My Hands (Bass Boost)", L"The Ultimate Pleasure in My Hands (Renfort graves)", L"The Ultimate Pleasure in My Hands (Rinforzo bassi)", L"The Ultimate Pleasure in My Hands (Refuerzo graves)", L"The Ultimate Pleasure in My Hands (?? ??)", L"The Ultimate Pleasure in My Hands (重低音)", L"The Ultimate Pleasure in My Hands (????? ??????)", L"The Ultimate Pleasure in My Hands (Усиление низких)", L"The Ultimate Pleasure in My Hands (Bassverstarkung)", L"The Ultimate Pleasure in My Hands (Reforco graves)", L"The Ultimate Pleasure in My Hands (Basversterking)", L"The Ultimate Pleasure in My Hands (Wzmocnienie basow)", L"The Ultimate Pleasure in My Hands (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b610.opus") {
						a = LL14(L"辿り着いた極光の下で", L"Under the Northern Lights", L"Sous les aurores boreales", L"Sotto l'aurora boreale", L"Bajo la aurora boreal", L"??? ?? ????", L"抵達極光之下", L"??? ????? ??????", L"Под северным сиянием", L"Unter dem Nordlicht", L"Sob a aurora boreal", L"Onder het noorderlicht", L"Pod zorz? polarn?", L"Kuzey I??klar? Alt?nda");
						fff = 1;
					}
					if (ft == L"y_b610_s1.opus") {
						a = LL14(L"辿り着いた極光の下で(重低音)", L"Under the Northern Lights (Bass Boost)", L"Under the Northern Lights (Renfort graves)", L"Under the Northern Lights (Rinforzo bassi)", L"Under the Northern Lights (Refuerzo graves)", L"Under the Northern Lights (?? ??)", L"Under the Northern Lights (重低音)", L"Under the Northern Lights (????? ??????)", L"Under the Northern Lights (Усиление низких)", L"Under the Northern Lights (Bassverstarkung)", L"Under the Northern Lights (Reforco graves)", L"Under the Northern Lights (Basversterking)", L"Under the Northern Lights (Wzmocnienie basow)", L"Under the Northern Lights (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b620.opus") {
						a = L"Nordics Saga -The Endless Bloody Sea-";
						fff = 1;
					}
					if (ft == L"y_b620_s1.opus") {
						a = LL14(L"Nordics Saga -The Endless Bloody Sea-(重低音)", L"Nordics Saga -The Endless Bloody Sea- (Bass Boost)", L"Nordics Saga -The Endless Bloody Sea- (Renfort graves)", L"Nordics Saga -The Endless Bloody Sea- (Rinforzo bassi)", L"Nordics Saga -The Endless Bloody Sea- (Refuerzo graves)", L"Nordics Saga -The Endless Bloody Sea- (?? ??)", L"Nordics Saga -The Endless Bloody Sea- (重低音)", L"Nordics Saga -The Endless Bloody Sea- (????? ??????)", L"Nordics Saga -The Endless Bloody Sea- (Усиление низких)", L"Nordics Saga -The Endless Bloody Sea- (Bassverstarkung)", L"Nordics Saga -The Endless Bloody Sea- (Reforco graves)", L"Nordics Saga -The Endless Bloody Sea- (Basversterking)", L"Nordics Saga -The Endless Bloody Sea- (Wzmocnienie basow)", L"Nordics Saga -The Endless Bloody Sea- (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b700.opus") {
						a = L"Ready to Fire!";
						fff = 1;
					}
					if (ft == L"y_b700_s1.opus") {
						a = LL14(L"Ready to Fire!(重低音)", L"Ready to Fire! (Bass Boost)", L"Ready to Fire! (Renfort graves)", L"Ready to Fire! (Rinforzo bassi)", L"Ready to Fire! (Refuerzo graves)", L"Ready to Fire! (?? ??)", L"Ready to Fire! (重低音)", L"Ready to Fire! (????? ??????)", L"Ready to Fire! (Усиление низких)", L"Ready to Fire! (Bassverstarkung)", L"Ready to Fire! (Reforco graves)", L"Ready to Fire! (Basversterking)", L"Ready to Fire! (Wzmocnienie basow)", L"Ready to Fire! (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b710.opus") {
						a = L"Hello, Those Who Can't Die";
						fff = 1;
					}
					if (ft == L"y_b710_s1.opus") {
						a = LL14(L"Hello, Those Who Can't Die(重低音)", L"Hello, Those Who Can't Die (Bass Boost)", L"Hello, Those Who Can't Die (Renfort graves)", L"Hello, Those Who Can't Die (Rinforzo bassi)", L"Hello, Those Who Can't Die (Refuerzo graves)", L"Hello, Those Who Can't Die (?? ??)", L"Hello, Those Who Can't Die (重低音)", L"Hello, Those Who Can't Die (????? ??????)", L"Hello, Those Who Can't Die (Усиление низких)", L"Hello, Those Who Can't Die (Bassverstarkung)", L"Hello, Those Who Can't Die (Reforco graves)", L"Hello, Those Who Can't Die (Basversterking)", L"Hello, Those Who Can't Die (Wzmocnienie basow)", L"Hello, Those Who Can't Die (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_b720.opus") {
						a = L"Landing Warfare";
						fff = 1;
					}
					if (ft == L"y_b720_s1.opus") {
						a = LL14(L"Landing Warfare(重低音)", L"Landing Warfare (Bass Boost)", L"Landing Warfare (Renfort graves)", L"Landing Warfare (Rinforzo bassi)", L"Landing Warfare (Refuerzo graves)", L"Landing Warfare (?? ??)", L"Landing Warfare (重低音)", L"Landing Warfare (????? ??????)", L"Landing Warfare (Усиление низких)", L"Landing Warfare (Bassverstarkung)", L"Landing Warfare (Reforco graves)", L"Landing Warfare (Basversterking)", L"Landing Warfare (Wzmocnienie basow)", L"Landing Warfare (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_bgm_none.opus") {
						a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"??", L"無音", L"???", L"Тишина", L"Stille", L"Silencio", L"Stilte", L"Cisza", L"Sessizlik");
						fff = 1;
					}
					if (ft == L"y_d100.opus") {
						a = LL14(L"光届かぬその奥に", L"In the Depths Where Light Doesn't Reach", L"Dans les profondeurs hors de portee de la lumiere", L"Nelle profondita dove non arriva la luce", L"En las profundidades donde no llega la luz", L"?? ?? ?? ? ?? ??", L"光線無法到達の深處", L"?? ??????? ??? ?? ??? ?????", L"В глубинах, куда не доходит свет", L"In den Tiefen, die kein Licht erreicht", L"Nas profundezas onde a luz nao chega", L"In de diepten waar geen licht komt", L"W g??binach, gdzie nie si?ga ?wiat?o", L"I????n Ula?amad??? Derinliklerde");
						fff = 1;
					}
					if (ft == L"y_d100_s1.opus") {
						a = LL14(L"光届かぬその奥に(重低音)", L"In the Depths Where Light Doesn't Reach (Bass Boost)", L"In the Depths Where Light Doesn't Reach (Renfort graves)", L"In the Depths Where Light Doesn't Reach (Rinforzo bassi)", L"In the Depths Where Light Doesn't Reach (Refuerzo graves)", L"In the Depths Where Light Doesn't Reach (?? ??)", L"In the Depths Where Light Doesn't Reach (重低音)", L"In the Depths Where Light Doesn't Reach (????? ??????)", L"In the Depths Where Light Doesn't Reach (Усиление низких)", L"In the Depths Where Light Doesn't Reach (Bassverstarkung)", L"In the Depths Where Light Doesn't Reach (Reforco graves)", L"In the Depths Where Light Doesn't Reach (Basversterking)", L"In the Depths Where Light Doesn't Reach (Wzmocnienie basow)", L"In the Depths Where Light Doesn't Reach (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_d200.opus") {
						a = L"Eerie Stillness";
						fff = 1;
					}
					if (ft == L"y_d200_s1.opus") {
						a = LL14(L"Eerie Stillness(重低音)", L"Eerie Stillness (Bass Boost)", L"Eerie Stillness (Renfort graves)", L"Eerie Stillness (Rinforzo bassi)", L"Eerie Stillness (Refuerzo graves)", L"Eerie Stillness (?? ??)", L"Eerie Stillness (重低音)", L"Eerie Stillness (????? ??????)", L"Eerie Stillness (Усиление низких)", L"Eerie Stillness (Bassverstarkung)", L"Eerie Stillness (Reforco graves)", L"Eerie Stillness (Basversterking)", L"Eerie Stillness (Wzmocnienie basow)", L"Eerie Stillness (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_d400.opus") {
						a = LL14(L"飽くなき渇望", L"Insatiable Thirst", L"Soif insatiable", L"Sete insaziabile", L"Sed insaciable", L"??? ??", L"永無止境的?望", L"??? ?? ?????", L"Ненасытная жажда", L"Unstillbares Verlangen", L"Sede insaciavel", L"Onverzadigbare dorst", L"Nienasycone pragnienie", L"Doymak Bilmez Susuzluk");
						fff = 1;
					}
					if (ft == L"y_d400_s1.opus") {
						a = LL14(L"飽くなき渇望(重低音)", L"Insatiable Thirst (Bass Boost)", L"Insatiable Thirst (Renfort graves)", L"Insatiable Thirst (Rinforzo bassi)", L"Insatiable Thirst (Refuerzo graves)", L"Insatiable Thirst (?? ??)", L"Insatiable Thirst (重低音)", L"Insatiable Thirst (????? ??????)", L"Insatiable Thirst (Усиление низких)", L"Insatiable Thirst (Bassverstarkung)", L"Insatiable Thirst (Reforco graves)", L"Insatiable Thirst (Basversterking)", L"Insatiable Thirst (Wzmocnienie basow)", L"Insatiable Thirst (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_d410.opus") {
						a = L"The Inner Darkness";
						fff = 1;
					}
					if (ft == L"y_d410_s1.opus") {
						a = LL14(L"The Inner Darkness(重低音)", L"The Inner Darkness (Bass Boost)", L"The Inner Darkness (Renfort graves)", L"The Inner Darkness (Rinforzo bassi)", L"The Inner Darkness (Refuerzo graves)", L"The Inner Darkness (?? ??)", L"The Inner Darkness (重低音)", L"The Inner Darkness (????? ??????)", L"The Inner Darkness (Усиление низких)", L"The Inner Darkness (Bassverstarkung)", L"The Inner Darkness (Reforco graves)", L"The Inner Darkness (Basversterking)", L"The Inner Darkness (Wzmocnienie basow)", L"The Inner Darkness (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_d500.opus") {
						a = L"Hardhearted Rock Line";
						fff = 1;
					}
					if (ft == L"y_d500_s1.opus") {
						a = LL14(L"Hardhearted Rock Line(重低音)", L"Hardhearted Rock Line (Bass Boost)", L"Hardhearted Rock Line (Renfort graves)", L"Hardhearted Rock Line (Rinforzo bassi)", L"Hardhearted Rock Line (Refuerzo graves)", L"Hardhearted Rock Line (?? ??)", L"Hardhearted Rock Line (重低音)", L"Hardhearted Rock Line (????? ??????)", L"Hardhearted Rock Line (Усиление низких)", L"Hardhearted Rock Line (Bassverstarkung)", L"Hardhearted Rock Line (Reforco graves)", L"Hardhearted Rock Line (Basversterking)", L"Hardhearted Rock Line (Wzmocnienie basow)", L"Hardhearted Rock Line (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_d600.opus") {
						a = LL14(L"夢の痕跡", L"Dream Traces", L"Traces de reves", L"Tracce di sogni", L"Rastros de suenos", L"?? ??", L"夢的痕跡", L"???? ???????", L"Следы снов", L"Traumspuren", L"Rastros de sonhos", L"Droomsporen", L"?lady snow", L"Ruya ?zleri");
						fff = 1;
					}
					if (ft == L"y_d600_s1.opus") {
						a = LL14(L"夢の痕跡(重低音)", L"Dream Traces (Bass Boost)", L"Dream Traces (Renfort graves)", L"Dream Traces (Rinforzo bassi)", L"Dream Traces (Refuerzo graves)", L"Dream Traces (?? ??)", L"Dream Traces (重低音)", L"Dream Traces (????? ??????)", L"Dream Traces (Усиление низких)", L"Dream Traces (Bassverstarkung)", L"Dream Traces (Reforco graves)", L"Dream Traces (Basversterking)", L"Dream Traces (Wzmocnienie basow)", L"Dream Traces (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_d710.opus") {
						a = LL14(L"甲鉄戦艦ナグルファ", L"Ironclad Battleship Naglfar", L"Cuirasse Naglfar", L"Corazzata Naglfar", L"Acorazado Naglfar", L"???? ???", L"甲鐵戰艦 Naglfar", L"??????? ??????? Naglfar", L"Броненосец Нагльфар", L"Panzerschiff Naglfar", L"Encouracado Naglfar", L"Slagschip Naglfar", L"Pancernik Naglfar", L"Z?rhl? Sava? Gemisi Naglfar");
						fff = 1;
					}
					if (ft == L"y_d710_s1.opus") {
						a = LL14(L"甲鉄戦艦ナグルファ(重低音)", L"Ironclad Battleship Naglfar (Bass Boost)", L"Ironclad Battleship Naglfar (Renfort graves)", L"Ironclad Battleship Naglfar (Rinforzo bassi)", L"Ironclad Battleship Naglfar (Refuerzo graves)", L"Ironclad Battleship Naglfar (?? ??)", L"Ironclad Battleship Naglfar (重低音)", L"Ironclad Battleship Naglfar (????? ??????)", L"Ironclad Battleship Naglfar (Усиление низких)", L"Ironclad Battleship Naglfar (Bassverstarkung)", L"Ironclad Battleship Naglfar (Reforco graves)", L"Ironclad Battleship Naglfar (Basversterking)", L"Ironclad Battleship Naglfar (Wzmocnienie basow)", L"Ironclad Battleship Naglfar (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_d800.opus") {
						a = L"LILA -Innocent Wish-";
						fff = 1;
					}
					if (ft == L"y_d800_s1.opus") {
						a = LL14(L"LILA -Innocent Wish-(重低音)", L"LILA -Innocent Wish- (Bass Boost)", L"LILA -Innocent Wish- (Renfort graves)", L"LILA -Innocent Wish- (Rinforzo bassi)", L"LILA -Innocent Wish- (Refuerzo graves)", L"LILA -Innocent Wish- (?? ??)", L"LILA -Innocent Wish- (重低音)", L"LILA -Innocent Wish- (????? ??????)", L"LILA -Innocent Wish- (Усиление низких)", L"LILA -Innocent Wish- (Bassverstarkung)", L"LILA -Innocent Wish- (Reforco graves)", L"LILA -Innocent Wish- (Basversterking)", L"LILA -Innocent Wish- (Wzmocnienie basow)", L"LILA -Innocent Wish- (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_d900.opus") {
						a = LL14(L"エギル海底神殿", L"Egil Undersea Temple", L"Temple sous-marin d'Egil", L"Tempio sottomarino di Egil", L"Templo submarino de Egil", L"?? ????", L"Egil 海底神殿", L"???? Egil ??? ?????", L"Подводный храм Эгиля", L"Egil-Unterseetempel", L"Templo submarino de Egil", L"Egil onderzeese tempel", L"Podmorska ?wi?tynia Egila", L"Egil Denizalt? Tap?na??");
						fff = 1;
					}
					if (ft == L"y_d900_s1.opus") {
						a = LL14(L"エギル海底神殿(重低音)", L"Egil Undersea Temple (Bass Boost)", L"Egil Undersea Temple (Renfort graves)", L"Egil Undersea Temple (Rinforzo bassi)", L"Egil Undersea Temple (Refuerzo graves)", L"Egil Undersea Temple (?? ??)", L"Egil Undersea Temple (重低音)", L"Egil Undersea Temple (????? ??????)", L"Egil Undersea Temple (Усиление низких)", L"Egil Undersea Temple (Bassverstarkung)", L"Egil Undersea Temple (Reforco graves)", L"Egil Undersea Temple (Basversterking)", L"Egil Undersea Temple (Wzmocnienie basow)", L"Egil Undersea Temple (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_d1010.opus") {
						a = L"The Paradise Lost of Norman";
						fff = 1;
					}
					if (ft == L"y_d1010_s1.opus") {
						a = LL14(L"The Paradise Lost of Norman(重低音)", L"The Paradise Lost of Norman (Bass Boost)", L"The Paradise Lost of Norman (Renfort graves)", L"The Paradise Lost of Norman (Rinforzo bassi)", L"The Paradise Lost of Norman (Refuerzo graves)", L"The Paradise Lost of Norman (?? ??)", L"The Paradise Lost of Norman (重低音)", L"The Paradise Lost of Norman (????? ??????)", L"The Paradise Lost of Norman (Усиление низких)", L"The Paradise Lost of Norman (Bassverstarkung)", L"The Paradise Lost of Norman (Reforco graves)", L"The Paradise Lost of Norman (Basversterking)", L"The Paradise Lost of Norman (Wzmocnienie basow)", L"The Paradise Lost of Norman (Bas guclendirme)");
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
						a = LL14(L"あの時からずっと…", L"Ever Since That Day...", L"Depuis ce jour-la...", L"Da quel giorno...", L"Desde aquel dia...", L"???? ??...", L"從那時起一直...", L"??? ??? ?????...", L"С того самого дня...", L"Seit jenem Tag...", L"Desde aquele dia...", L"Sinds die dag...", L"Od tamtego dnia...", L"O Gunden Beri...");
						fff = 1;
					}
					if (ft == L"y_e005.opus") {
						a = L"Waver as the Wave";
						fff = 1;
					}
					if (ft == L"y_e006.opus") {
						a = LL14(L"切っても切れない絆", L"Unbreakable Bonds", L"Liens indefectibles", L"Legami indissolubili", L"Vinculos inquebrantables", L"??? ? ? ?? ??", L"無法割捨的羈絆", L"????? ?? ?????", L"Неразрывные узы", L"Unzerbrechliche Bande", L"Lacos inquebraveis", L"Onbreekbare banden", L"Nierozerwalne wi?zi", L"Y?k?lmaz Ba?lar");
						fff = 1;
					}
					if (ft == L"y_e007.opus") {
						a = LL14(L"灰色の深層", L"Gray Depths", L"Profondeurs grises", L"Profondita grigie", L"Profundidades grises", L"??? ??", L"灰色的深層", L"????? ??????", L"Серые глубины", L"Graue Tiefen", L"Profundezas cinzentas", L"Grijze diepten", L"Szare g??biny", L"Gri Derinlikler");
						fff = 1;
					}
					if (ft == L"y_e007_s1.opus") {
						a = LL14(L"灰色の深層(重低音)", L"Gray Depths (Bass Boost)", L"Gray Depths (Renfort graves)", L"Gray Depths (Rinforzo bassi)", L"Gray Depths (Refuerzo graves)", L"Gray Depths (?? ??)", L"Gray Depths (重低音)", L"Gray Depths (????? ??????)", L"Gray Depths (Усиление низких)", L"Gray Depths (Bassverstarkung)", L"Gray Depths (Reforco graves)", L"Gray Depths (Basversterking)", L"Gray Depths (Wzmocnienie basow)", L"Gray Depths (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_e008.opus") {
						a = L"Premonition of Turmoil";
						fff = 1;
					}
					if (ft == L"y_e009.opus") {
						a = LL14(L"歪な願望", L"Twisted Desire", L"Desir tordu", L"Desiderio distorto", L"Deseo retorcido", L"???? ??", L"?曲的願望", L"???? ??????", L"Искаженное желание", L"Verdrehtes Verlangen", L"Desejo distorcido", L"Verdraaid verlangen", L"Skr?cone pragnienie", L"Carp?k Arzu");
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
						a = LL14(L"Violent Warriors(重低音)", L"Violent Warriors (Bass Boost)", L"Violent Warriors (Renfort graves)", L"Violent Warriors (Rinforzo bassi)", L"Violent Warriors (Refuerzo graves)", L"Violent Warriors (?? ??)", L"Violent Warriors (重低音)", L"Violent Warriors (????? ??????)", L"Violent Warriors (Усиление низких)", L"Violent Warriors (Bassverstarkung)", L"Violent Warriors (Reforco graves)", L"Violent Warriors (Basversterking)", L"Violent Warriors (Wzmocnienie basow)", L"Violent Warriors (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_e012.opus") {
						a = LL14(L"手筈通りに", L"As Planned", L"Comme prevu", L"Come pianificato", L"Como se planeo", L"????", L"按照計畫", L"??? ?? ????", L"Как и планировалось", L"Wie geplant", L"Como planejado", L"Zoals gepland", L"Zgodnie z planem", L"Planland??? Gibi");
						fff = 1;
					}
					if (ft == L"y_e013.opus") {
						a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
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
						a = LL14(L"Deep Unconscious(重低音)", L"Deep Unconscious (Bass Boost)", L"Deep Unconscious (Renfort graves)", L"Deep Unconscious (Rinforzo bassi)", L"Deep Unconscious (Refuerzo graves)", L"Deep Unconscious (?? ??)", L"Deep Unconscious (重低音)", L"Deep Unconscious (????? ??????)", L"Deep Unconscious (Усиление низких)", L"Deep Unconscious (Bassverstarkung)", L"Deep Unconscious (Reforco graves)", L"Deep Unconscious (Basversterking)", L"Deep Unconscious (Wzmocnienie basow)", L"Deep Unconscious (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f100.opus") {
						a = L"TO BE FREE";
						fff = 1;
					}
					if (ft == L"y_f100_s1.opus") {
						a = LL14(L"TO BE FREE(重低音)", L"TO BE FREE (Bass Boost)", L"TO BE FREE (Renfort graves)", L"TO BE FREE (Rinforzo bassi)", L"TO BE FREE (Refuerzo graves)", L"TO BE FREE (?? ??)", L"TO BE FREE (重低音)", L"TO BE FREE (????? ??????)", L"TO BE FREE (Усиление низких)", L"TO BE FREE (Bassverstarkung)", L"TO BE FREE (Reforco graves)", L"TO BE FREE (Basversterking)", L"TO BE FREE (Wzmocnienie basow)", L"TO BE FREE (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f110.opus") {
						a = L"Brother's Footsteps on the Island";
						fff = 1;
					}
					if (ft == L"y_f110_s1.opus") {
						a = LL14(L"Brother's Footsteps on the Island(重低音)", L"Brother's Footsteps on the Island (Bass Boost)", L"Brother's Footsteps on the Island (Renfort graves)", L"Brother's Footsteps on the Island (Rinforzo bassi)", L"Brother's Footsteps on the Island (Refuerzo graves)", L"Brother's Footsteps on the Island (?? ??)", L"Brother's Footsteps on the Island (重低音)", L"Brother's Footsteps on the Island (????? ??????)", L"Brother's Footsteps on the Island (Усиление низких)", L"Brother's Footsteps on the Island (Bassverstarkung)", L"Brother's Footsteps on the Island (Reforco graves)", L"Brother's Footsteps on the Island (Basversterking)", L"Brother's Footsteps on the Island (Wzmocnienie basow)", L"Brother's Footsteps on the Island (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f120.opus") {
						a = L"Burn with You";
						fff = 1;
					}
					if (ft == L"y_f120_s1.opus") {
						a = LL14(L"Burn with You(重低音)", L"Burn with You (Bass Boost)", L"Burn with You (Renfort graves)", L"Burn with You (Rinforzo bassi)", L"Burn with You (Refuerzo graves)", L"Burn with You (?? ??)", L"Burn with You (重低音)", L"Burn with You (????? ??????)", L"Burn with You (Усиление низких)", L"Burn with You (Bassverstarkung)", L"Burn with You (Reforco graves)", L"Burn with You (Basversterking)", L"Burn with You (Wzmocnienie basow)", L"Burn with You (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f130.opus") {
						a = L"Destined to Keep Running";
						fff = 1;
					}
					if (ft == L"y_f130_s1.opus") {
						a = LL14(L"Destined to Keep Running(重低音)", L"Destined to Keep Running (Bass Boost)", L"Destined to Keep Running (Renfort graves)", L"Destined to Keep Running (Rinforzo bassi)", L"Destined to Keep Running (Refuerzo graves)", L"Destined to Keep Running (?? ??)", L"Destined to Keep Running (重低音)", L"Destined to Keep Running (????? ??????)", L"Destined to Keep Running (Усиление низких)", L"Destined to Keep Running (Bassverstarkung)", L"Destined to Keep Running (Reforco graves)", L"Destined to Keep Running (Basversterking)", L"Destined to Keep Running (Wzmocnienie basow)", L"Destined to Keep Running (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f140.opus") {
						a = L"Ride on Mana!";
						fff = 1;
					}
					if (ft == L"y_f140_s1.opus") {
						a = LL14(L"Ride on Mana!(重低音)", L"Ride on Mana! (Bass Boost)", L"Ride on Mana! (Renfort graves)", L"Ride on Mana! (Rinforzo bassi)", L"Ride on Mana! (Refuerzo graves)", L"Ride on Mana! (?? ??)", L"Ride on Mana! (重低音)", L"Ride on Mana! (????? ??????)", L"Ride on Mana! (Усиление низких)", L"Ride on Mana! (Bassverstarkung)", L"Ride on Mana! (Reforco graves)", L"Ride on Mana! (Basversterking)", L"Ride on Mana! (Wzmocnienie basow)", L"Ride on Mana! (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f150.opus") {
						a = L"Heat Hazard";
						fff = 1;
					}
					if (ft == L"y_f150_s1.opus") {
						a = LL14(L"Heat Hazard(重低音)", L"Heat Hazard (Bass Boost)", L"Heat Hazard (Renfort graves)", L"Heat Hazard (Rinforzo bassi)", L"Heat Hazard (Refuerzo graves)", L"Heat Hazard (?? ??)", L"Heat Hazard (重低音)", L"Heat Hazard (????? ??????)", L"Heat Hazard (Усиление низких)", L"Heat Hazard (Bassverstarkung)", L"Heat Hazard (Reforco graves)", L"Heat Hazard (Basversterking)", L"Heat Hazard (Wzmocnienie basow)", L"Heat Hazard (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f160.opus") {
						a = LL14(L"瞳の中の少年剣士", L"Young Swordsman in My Eyes", L"Le jeune epeiste dans mes yeux", L"Il giovane spadaccino nei miei occhi", L"El joven espadachin en mis ojos", L"??? ?? ?? ??", L"瞳孔中的少年劍士", L"???? ??? ?? ????", L"Юный мечник в моих глазах", L"Junger Schwertkampfer in meinen Augen", L"Jovem espadachim nos meus olhos", L"Jonge zwaardvechter in mijn ogen", L"M?ody szermierz w moich oczach", L"Gozlerimdeki Genc K?l?c Ustas?");
						fff = 1;
					}
					if (ft == L"y_f160_s1.opus") {
						a = LL14(L"瞳の中の少年剣士(重低音)", L"Young Swordsman in My Eyes (Bass Boost)", L"Young Swordsman in My Eyes (Renfort graves)", L"Young Swordsman in My Eyes (Rinforzo bassi)", L"Young Swordsman in My Eyes (Refuerzo graves)", L"Young Swordsman in My Eyes (?? ??)", L"Young Swordsman in My Eyes (重低音)", L"Young Swordsman in My Eyes (????? ??????)", L"Young Swordsman in My Eyes (Усиление низких)", L"Young Swordsman in My Eyes (Bassverstarkung)", L"Young Swordsman in My Eyes (Reforco graves)", L"Young Swordsman in My Eyes (Basversterking)", L"Young Swordsman in My Eyes (Wzmocnienie basow)", L"Young Swordsman in My Eyes (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f200.opus") {
						a = LL14(L"錨を揚げろ！", L"Weigh Anchor!", L"Levez l'ancre !", L"Leva l'ancora!", L"!Leven anclas!", L"?? ???!", L"起錨！", L"???? ???????!", L"Поднять якорь!", L"Anker lichten!", L"Levantar ancora!", L"Licht het anker!", L"Podnie?? kotwic?!", L"Demir Al!");
						fff = 1;
					}
					if (ft == L"y_f200_s1.opus") {
						a = LL14(L"錨を揚げろ！(重低音)", L"Weigh Anchor! (Bass Boost)", L"Weigh Anchor! (Renfort graves)", L"Weigh Anchor! (Rinforzo bassi)", L"Weigh Anchor! (Refuerzo graves)", L"Weigh Anchor! (?? ??)", L"Weigh Anchor! (重低音)", L"Weigh Anchor! (????? ??????)", L"Weigh Anchor! (Усиление низких)", L"Weigh Anchor! (Bassverstarkung)", L"Weigh Anchor! (Reforco graves)", L"Weigh Anchor! (Basversterking)", L"Weigh Anchor! (Wzmocnienie basow)", L"Weigh Anchor! (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f210.opus") {
						a = LL14(L"悠き海に生きる者", L"Those Who Live in the Vast Sea", L"Ceux qui vivent dans la mer vaste", L"Coloro che vivono nel vasto mare", L"Aquellos que viven en el mar vasto", L"??? ??? ?? ?", L"生活在悠久大海的人", L"????? ????? ?????? ?? ????? ??????", L"Те, кто живет в бескрайнем море", L"Die im weiten Meer leben", L"Aqueles que vivem no mar vasto", L"Zij die in de onmetelijke zee leven", L"Ci, ktorzy ?yj? w rozleg?ym morzu", L"Engin Denizlerde Ya?ayanlar");
						fff = 1;
					}
					if (ft == L"y_f210_s1.opus") {
						a = LL14(L"悠き海に生きる者(重低音)", L"Those Who Live in the Vast Sea (Bass Boost)", L"Those Who Live in the Vast Sea (Renfort graves)", L"Those Who Live in the Vast Sea (Rinforzo bassi)", L"Those Who Live in the Vast Sea (Refuerzo graves)", L"Those Who Live in the Vast Sea (?? ??)", L"Those Who Live in the Vast Sea (重低音)", L"Those Who Live in the Vast Sea (????? ??????)", L"Those Who Live in the Vast Sea (Усиление низких)", L"Those Who Live in the Vast Sea (Bassverstarkung)", L"Those Who Live in the Vast Sea (Reforco graves)", L"Those Who Live in the Vast Sea (Basversterking)", L"Those Who Live in the Vast Sea (Wzmocnienie basow)", L"Those Who Live in the Vast Sea (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f220.opus") {
						a = LL14(L"コンパスは踊る", L"The Compass Dances", L"La boussole danse", L"La bussola danza", L"La brujula danza", L"???? ???", L"羅盤在跳舞", L"??????? ????", L"Компас танцует", L"Der Kompass tanzt", L"A bussola danca", L"Het kompas danst", L"Kompas ta?czy", L"Pusula Dans Ediyor");
						fff = 1;
					}
					if (ft == L"y_f220_s1.opus") {
						a = LL14(L"コンパスは踊る(重低音)", L"The Compass Dances (Bass Boost)", L"The Compass Dances (Renfort graves)", L"The Compass Dances (Rinforzo bassi)", L"The Compass Dances (Refuerzo graves)", L"The Compass Dances (?? ??)", L"The Compass Dances (重低音)", L"The Compass Dances (????? ??????)", L"The Compass Dances (Усиление низких)", L"The Compass Dances (Bassverstarkung)", L"The Compass Dances (Reforco graves)", L"The Compass Dances (Basversterking)", L"The Compass Dances (Wzmocnienie basow)", L"The Compass Dances (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f230.opus") {
						a = LL14(L"開闢の海", L"Sea of Genesis", L"Mer de la genese", L"Mare della genesi", L"Mar de la genesis", L"??? ??", L"開闢之海", L"??? ???????", L"Море сотворения", L"Meer der Schopfung", L"Mar da genese", L"Zee van de genesis", L"Morze genezy", L"Yarat?l?? Denizi");
						fff = 1;
					}
					if (ft == L"y_f230_s1.opus") {
						a = LL14(L"開闢の海(重低音)", L"Sea of Genesis (Bass Boost)", L"Sea of Genesis (Renfort graves)", L"Sea of Genesis (Rinforzo bassi)", L"Sea of Genesis (Refuerzo graves)", L"Sea of Genesis (?? ??)", L"Sea of Genesis (重低音)", L"Sea of Genesis (????? ??????)", L"Sea of Genesis (Усиление низких)", L"Sea of Genesis (Bassverstarkung)", L"Sea of Genesis (Reforco graves)", L"Sea of Genesis (Basversterking)", L"Sea of Genesis (Wzmocnienie basow)", L"Sea of Genesis (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_f310.opus") {
						a = L"If I Could Go Back to Those Days";
						fff = 1;
					}
					if (ft == L"y_f310_s1.opus") {
						a = LL14(L"If I Could Go Back to Those Days(重低音)", L"If I Could Go Back to Those Days (Bass Boost)", L"If I Could Go Back to Those Days (Renfort graves)", L"If I Could Go Back to Those Days (Rinforzo bassi)", L"If I Could Go Back to Those Days (Refuerzo graves)", L"If I Could Go Back to Those Days (?? ??)", L"If I Could Go Back to Those Days (重低音)", L"If I Could Go Back to Those Days (????? ??????)", L"If I Could Go Back to Those Days (Усиление низких)", L"If I Could Go Back to Those Days (Bassverstarkung)", L"If I Could Go Back to Those Days (Reforco graves)", L"If I Could Go Back to Those Days (Basversterking)", L"If I Could Go Back to Those Days (Wzmocnienie basow)", L"If I Could Go Back to Those Days (Bas guclendirme)");
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
						a = LL14(L"Our Hometown(重低音)", L"Our Hometown (Bass Boost)", L"Our Hometown (Renfort graves)", L"Our Hometown (Rinforzo bassi)", L"Our Hometown (Refuerzo graves)", L"Our Hometown (?? ??)", L"Our Hometown (重低音)", L"Our Hometown (????? ??????)", L"Our Hometown (Усиление низких)", L"Our Hometown (Bassverstarkung)", L"Our Hometown (Reforco graves)", L"Our Hometown (Basversterking)", L"Our Hometown (Wzmocnienie basow)", L"Our Hometown (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_t200.opus") {
						a = LL14(L"根ざすべき場所", L"Where We Belong", L"La ou nous appartenons", L"Il posto a cui apparteniamo", L"El lugar al que pertenecemos", L"?? ??? ? ?", L"落地生根之處", L"??? ?????", L"Там, где наш дом", L"Wo wir hingehoren", L"Onde pertencemos", L"Waar we thuishoren", L"Miejsce, do ktorego nale?ymy", L"Ait Oldu?umuz Yer");
						fff = 1;
					}
					if (ft == L"y_t200_s1.opus") {
						a = LL14(L"根ざすべき場所(重低音)", L"Where We Belong (Bass Boost)", L"Where We Belong (Renfort graves)", L"Where We Belong (Rinforzo bassi)", L"Where We Belong (Refuerzo graves)", L"Where We Belong (?? ??)", L"Where We Belong (重低音)", L"Where We Belong (????? ??????)", L"Where We Belong (Усиление низких)", L"Where We Belong (Bassverstarkung)", L"Where We Belong (Reforco graves)", L"Where We Belong (Basversterking)", L"Where We Belong (Wzmocnienie basow)", L"Where We Belong (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_t300.opus") {
						a = L"Sometime Siesta";
						fff = 1;
					}
					if (ft == L"y_t300_s1.opus") {
						a = LL14(L"Sometime Siesta(重低音)", L"Sometime Siesta (Bass Boost)", L"Sometime Siesta (Renfort graves)", L"Sometime Siesta (Rinforzo bassi)", L"Sometime Siesta (Refuerzo graves)", L"Sometime Siesta (?? ??)", L"Sometime Siesta (重低音)", L"Sometime Siesta (????? ??????)", L"Sometime Siesta (Усиление низких)", L"Sometime Siesta (Bassverstarkung)", L"Sometime Siesta (Reforco graves)", L"Sometime Siesta (Basversterking)", L"Sometime Siesta (Wzmocnienie basow)", L"Sometime Siesta (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_t301.opus") {
						a = L"Innermost Feelings";
						fff = 1;
					}
					if (ft == L"y_t301_s1.opus") {
						a = LL14(L"Innermost Feelings(重低音)", L"Innermost Feelings (Bass Boost)", L"Innermost Feelings (Renfort graves)", L"Innermost Feelings (Rinforzo bassi)", L"Innermost Feelings (Refuerzo graves)", L"Innermost Feelings (?? ??)", L"Innermost Feelings (重低音)", L"Innermost Feelings (????? ??????)", L"Innermost Feelings (Усиление низких)", L"Innermost Feelings (Bassverstarkung)", L"Innermost Feelings (Reforco graves)", L"Innermost Feelings (Basversterking)", L"Innermost Feelings (Wzmocnienie basow)", L"Innermost Feelings (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_t500.opus") {
						a = LL14(L"情景に揺蕩う", L"Drifting in the Scene", L"Derivant dans la scene", L"Oscillando nella scena", L"Derivando en la escena", L"??? ????", L"?浸於情景中", L"???????? ?? ??????", L"Дрейфуя в пейзаже", L"In der Szenerie treiben", L"Derivando na cena", L"Drijvend in de scene", L"Dryfuj?c w scenerii", L"Manzarada Suzulmek");
						fff = 1;
					}
					if (ft == L"y_t500_s1.opus") {
						a = LL14(L"情景に揺蕩う(重低音)", L"Drifting in the Scene (Bass Boost)", L"Drifting in the Scene (Renfort graves)", L"Drifting in the Scene (Rinforzo bassi)", L"Drifting in the Scene (Refuerzo graves)", L"Drifting in the Scene (?? ??)", L"Drifting in the Scene (重低音)", L"Drifting in the Scene (????? ??????)", L"Drifting in the Scene (Усиление низких)", L"Drifting in the Scene (Bassverstarkung)", L"Drifting in the Scene (Reforco graves)", L"Drifting in the Scene (Basversterking)", L"Drifting in the Scene (Wzmocnienie basow)", L"Drifting in the Scene (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_t600.opus") {
						a = LL14(L"盾の兄弟", L"Shield Brothers", L"Freres de bouclier", L"Fratelli di scudo", L"Hermanos de escudo", L"??? ??", L"盾之兄弟", L"???? ?????", L"Братья по щиту", L"Schildbruder", L"Irmaos de escudo", L"Schildbroeders", L"Bracia tarczy", L"Kalkan Karde?li?i");
						fff = 1;
					}
					if (ft == L"y_t600_s1.opus") {
						a = LL14(L"盾の兄弟(重低音)", L"Shield Brothers (Bass Boost)", L"Shield Brothers (Renfort graves)", L"Shield Brothers (Rinforzo bassi)", L"Shield Brothers (Refuerzo graves)", L"Shield Brothers (?? ??)", L"Shield Brothers (重低音)", L"Shield Brothers (????? ??????)", L"Shield Brothers (Усиление низких)", L"Shield Brothers (Bassverstarkung)", L"Shield Brothers (Reforco graves)", L"Shield Brothers (Basversterking)", L"Shield Brothers (Wzmocnienie basow)", L"Shield Brothers (Bas guclendirme)");
						fff = 1;
					}
					if (ft == L"y_title.opus") {
						a = LL14(L"その優しさは誰のため", L"For Whom Is That Kindness", L"Pour qui est cette gentillesse", L"Per chi e quella gentilezza", L"Para quien es esa amabilidad", L"? ???? ??を ?? ???", L"那??柔是為了誰", L"??? ??? ?????", L"Для кого эта доброта", L"Wem gilt diese Gute", L"Para quem e essa bondade", L"Voor wie is die vriendelijkheid", L"Dla kogo ta dobro?", L"Bu Nezaket Kimin ?cin");
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
							if (a.Mid(4, 4) == "b010") { a = LL14(L"URBAN TERROR(イントロあり)", L"URBAN TERROR (With Intro)", L"URBAN TERROR (Avec Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (Con Intro)", L"URBAN TERROR (??? ??)", L"URBAN TERROR (含前奏)", L"URBAN TERROR (?? ?????)", L"URBAN TERROR (С интро)", L"URBAN TERROR (Mit Intro)", L"URBAN TERROR (Com Intro)", L"URBAN TERROR (Met Intro)", L"URBAN TERROR (Z intro)", L"URBAN TERROR (Giri?li)"); }
							if (a.Mid(4, 5) == "b011b") { a = "DREAMING IN THE GRIMWALD"; }
							if (a.Mid(4, 4) == "b011") { a = LL14(L"DREAMING IN THE GRIMWALD(イントロあり)", L"DREAMING IN THE GRIMWALD (With Intro)", L"DREAMING IN THE GRIMWALD (Avec Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (Con Intro)", L"DREAMING IN THE GRIMWALD (??? ??)", L"DREAMING IN THE GRIMWALD (含前奏)", L"DREAMING IN THE GRIMWALD (?? ?????)", L"DREAMING IN THE GRIMWALD (С интро)", L"DREAMING IN THE GRIMWALD (Mit Intro)", L"DREAMING IN THE GRIMWALD (Com Intro)", L"DREAMING IN THE GRIMWALD (Met Intro)", L"DREAMING IN THE GRIMWALD (Z intro)", L"DREAMING IN THE GRIMWALD (Giri?li)"); }
							if (a.Mid(4, 4) == "b012") { a = "WILD CARD"; }
							if (a.Mid(4, 5) == "b014b") { a = "FULL MOON CEREMONY"; }
							if (a.Mid(4, 4) == "b014") { a = LL14(L"FULL MOON CEREMONY(イントロあり)", L"FULL MOON CEREMONY (With Intro)", L"FULL MOON CEREMONY (Avec Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (Con Intro)", L"FULL MOON CEREMONY (??? ??)", L"FULL MOON CEREMONY (含前奏)", L"FULL MOON CEREMONY (?? ?????)", L"FULL MOON CEREMONY (С инトロ)", L"FULL MOON CEREMONY (Mit Intro)", L"FULL MOON CEREMONY (Com Intro)", L"FULL MOON CEREMONY (Met Intro)", L"FULL MOON CEREMONY (Z intro)", L"FULL MOON CEREMONY (Giri?li)"); }
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
							if (a.Mid(4, 4) == "e008") { a = L"IL ETAIT UNE FOIS"; }
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
							if (a.Mid(4, 4) == "muon") { a = LL14(L"無音", L"Silence", L"Silence", L"Silenzio", L"Silencio", L"??", L"無音", L"???", L"Тишина", L"Stille", L"Silencio", L"Stilte", L"Cisza", L"Sessizlik"); }
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
								a = LL14(L"罪と罰と偽りと", L"Sin, Punishment and Falsehood", L"Peche, punition et mensonge", L"Peccato, punizione e falsita", L"Pecado, castigo y falsedad", L"?? ?? ???", L"罪、罰與?偽", L"??????? ??????? ??????", L"Грех, наказание и ложь", L"Sunde, Strafe und Falschheit", L"Pecado, castigo e falsidade", L"Zonde, straf en valsheid", L"Grzech, kara i fa?sz", L"Gunah, Ceza ve Sahtelik");
								break;
							case 81005:
								a = LL14(L"昏き鐘の残響", L"Resonance of the Dark Bell", L"Resonance de la cloche sombre", L"Risonanza della campana oscura", L"Resonancia de la campana oscura", L"??? ?? ??", L"昏暗之鐘的殘響", L"??? ????? ??????", L"Резонанс темного колокола", L"Resonanz der dunklen Glocke", L"Ressonancia do sino sombrio", L"Resonantie van de duistere klok", L"Rezonans mrocznego dzwonu", L"Karanl?k Can?n Yank?s?");
								break;
							case 81006:
								a = "Right on the Mark";
								break;
							case 81007:
								a = LL14(L"悪夢ふたたび", L"Nightmare Again", L"Le cauchemar recommence", L"Incubo di nuovo", L"Pesadilla de nuevo", L"?? ?? ??", L"?夢重現", L"??????? ??? ????", L"Кошмар снова", L"Albtraum erneut", L"Pesadelo novamente", L"Nachtmerrie opnieuw", L"Koszmar ponownie", L"Kabus Yeniden");
								break;
							case 81008:
								a = "Crossbell Nostalgia";
								break;
							case 81009:
								a = LL14(L"創まりの円庭", L"Garden of Beginnings", L"Jardin des commencements", L"Giardino degli inizi", L"Jardin de los inicios", L"??? ??", L"創始之圓庭", L"????? ????????", L"Сад начал", L"Garten der Anfange", L"Jardim dos comecos", L"Tuin van het begin", L"Ogrod pocz?tkow", L"Ba?lang?c Bahcesi");
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
								a = LL14(L"さざめきの途路", L"Path of Tumult", L"Chemin du tumulte", L"Sentiero del tumulto", L"Senda del tumulto", L"????? ?", L"?雜的途徑", L"???? ????????", L"Путь суматохи", L"Pfad des Tumults", L"Caminho do tumulto", L"Pad van rumoer", L"?cie?ka zgie?ku", L"Gurultulu Yol");
								break;
							case 81016:
								a = LL14(L"蒼の大地に生きる者", L"Those Who Live on the Azure Land", L"Ceux qui vivent sur la terre d'azur", L"Coloro che vivono sulla terra azzurra", L"Aquellos que viven en la tierra azul", L"?? ??? ?? ?", L"生活在蒼之大地的人", L"????? ????? ?????? ??? ????? ???????", L"Те, кто живет на лазурной земле", L"Die auf dem azurblauen Land leben", L"Aqueles que vivem na terra azul", L"Zij die op het azuurblauwe land leven", L"Ci, ktorzy ?yj? na b??kitnej ziemi", L"Mavi Topraklarda Ya?ayanlar");
								break;
							case 81017:
								a = LL14(L"黎明の鐘", L"Bell of Dawn", L"Cloche de l'aube", L"Campana dell'alba", L"Campana del alba", L"??? ?", L"黎明之鐘", L"??? ?????", L"Колокол рассвета", L"Glocke der Dammerung", L"Sino da aurora", L"Klok van de dageraad", L"Dzwon ?witu", L"?afak Can?");
								break;
							case 81018:
								a = LL14(L"レメディファンタジア -仲間とともに-", L"Remedi Fantasia -With Comrades-", L"Remedi Fantasia -Avec des camarades-", L"Remedi Fantasia -Con i compagni-", L"Remedi Fantasia -Con camaradas-", L"??? ???? ~??? ??~", L"Remedi Fantasia -與夥伴一起-", L"Remedi Fantasia - ?? ??????", L"Remedi Fantasia -С товарищами-", L"Remedi Fantasia -Mit Kameraden-", L"Remedi Fantasia -Com camaradas-", L"Remedi Fantasia -Met kameraden-", L"Remedi Fantasia -Z towarzyszami-", L"Remedi Fantasia -Yolda?larla-");
								break;
							case 81019:
								a = "Slight Suspicion";
								break;
							case 81020:
								a = "Maliciousness in the Mirror";
								break;
							case 81021:
								a = LL14(L"暗澹たる世界", L"Dark World", L"Monde sombre", L"Mondo oscuro", L"Mundo oscuro", L"??? ??", L"暗淡的世界", L"???? ????", L"Мрачный мир", L"Dunkle Welt", L"Mundo sombrio", L"Duistere wereld", L"Mroczny ?wiat", L"Karanl?k Dunya");
								break;
							case 81022:
								a = LL14(L"ひとときの温もり", L"Brief Warmth", L"Bref repit de chaleur", L"Breve calore", L"Breve calor", L"?? ??? ??", L"片刻的?暖", L"??? ????", L"Краткое тепло", L"Kurze Warme", L"Breve calor", L"Korte warmte", L"Krotkie ciep?o", L"K?sa Sureli S?cakl?k");
								break;
							case 81023:
								a = LL14(L"今、創まりのとき", L"Now, the Moment of Creation", L"Maintenant, le moment de la creation", L"Ora, il momento della creazione", L"Ahora, el momento de la creacion", L"??, ??? ??", L"現在，創始之時", L"????? ???? ???????", L"Теперь момент сотворения", L"Nun, der Moment der Schopfung", L"Agora, o momento da criacao", L"Nu, het moment van creatie", L"Teraz moment stworzenia", L"?imdi, Yarat?l?? An?");
								break;
							case 81024:
								a = "KERAUNOS -Fear and Hatred-";
								break;
							case 81025:
								a = LL14(L"亡失われた魂", L"Lost Souls", L"Ames perdues", L"Anime perse", L"Almas perdidas", L"???? ???", L"迷失的靈魂", L"????? ??????", L"Потерянные души", L"Verlorene Seelen", L"Almas perdidas", L"Verloren zielen", L"Zagubione dusze", L"Kay?p Ruhlar");
								break;
							case 81026:
								a = LL14(L"穏やかな時間", L"Peaceful Time", L"Temps paisible", L"Tempo pacifico", L"Tiempo pacifico", L"??? ??", L"平靜的時光", L"??? ????", L"Мирное время", L"Friedliche Zeit", L"Tempo pacifico", L"Vredige tijd", L"Spokojny czas", L"Huzurlu Vakit");
								break;
							case 81027:
								break;
							case 81028:
								a = LL14(L"運命という名の歯車", L"Gears of Fate", L"Engrenages du destin", L"Ingranaggi del destino", L"Engranajes del destino", L"????? ??? ????", L"名為命運的齒輪", L"???? ?????", L"Шестеренки судьбы", L"Zahnrader des Schicksals", L"Engrenagens do destino", L"Raderen van het lot", L"Ko?a z?bate losu", L"Kader Carklar?");
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
								a = LL14(L"陽溜まりにただいまを", L"Home in the Sunshine", L"Retour au soleil", L"A casa sotto il sole", L"Hogar bajo el sol", L"?? ?? ???????", L"在陽光下?我回來了", L"?????? ?????? ?? ??? ?????", L"Домой под лучами солнца", L"Zuhause im Sonnenschein", L"Lar sob o sol", L"Thuis in de zon", L"Dom w s?o?cu", L"Gune? I????nda Eve Donu?");
								break;
							case 81320:
								a = "Wind-Up Yesterday!";
								break;
							case 81321:
								a = LL14(L"零の邂逅", L"Zero Encounter", L"Rencontre de zero", L"Incontro zero", L"Encuentro cero", L"??? ??", L"零之邂逅", L"???? ?????", L"Встреча Зеро", L"Zero-Begegnung", L"Encontro zero", L"Zero ontmoeting", L"Spotkanie zero", L"S?f?r Kar??la?mas?");
								break;
							case 81322:
								a = LL14(L"影の見えざる手", L"Invisible Hand in the Shadows", L"Main invisible dans l'ombre", L"Mano invisibile nelle ombre", L"Mano invisible en las sombras", L"???? ??? ?? ?", L"影子那看不見的手", L"?? ???? ?? ??????", L"Невидимая рука в тени", L"Unsichtbare Hand im Schatten", L"Mao invisivel nas sombras", L"Onzichtbare hand in de schaduw", L"Niewidzialna r?ka w cieniu", L"Golgedeki Gorunmez El");
								break;
							case 81950: case 81951: case 81952: case 81953: case 81954:
							case 81955: case 81956: case 81957: case 81958: case 81961:
							case 81962: case 81963: case 81964: case 81965: case 81966:
							case 81967: case 81968: case 81969:
								break;
							case 82065:
								a = LL14(L"鋼鉄牙城", L"Iron Fortress", L"Forteresse d'acier", L"Fortezza d'acciaio", L"Fortaleza de acero", L"????", L"鋼鐵牙城", L"??? ??????", L"Железная крепость", L"Eiserne Festung", L"Fortaleza de aco", L"IJzeren vesting", L"Stalowa twierdza", L"Demir Kale");
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
								a = LL14(L"波間に弾む心", L"Heart Bouncing on the Waves", L"C?ur bondissant sur les vagues", L"Cuore che rimbalza sulle onde", L"Corazon saltando en las olas", L"?? ??? ??? ??", L"在波浪間雀躍的心", L"??? ???? ??? ???????", L"Сердце, прыгающее на волнах", L"Herz, das auf den Wellen hupft", L"Coracao saltitando nas ondas", L"Hart dat stuitert op de golven", L"Serce skacz?ce na falach", L"Dalgalarda Hoplayan Kalp");
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
								a = LL14(L"流麗闘冴", L"Elegant Battle", L"Combat elegant", L"Battaglia elegante", L"Batalla elegante", L"????", L"流麗鬥冴", L"????? ?????", L"Элегантная битва", L"Eleganter Kampf", L"Batalha elegante", L"Elegant gevecht", L"Elegancka bitwa", L"Zarif Sava?");
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
								a = LL14(L"ひとかけらの光明", L"Sliver of Light", L"Lueur d'espoir", L"Barlume di luce", L"Rayo de luz", L"? ??? ??", L"一絲光明", L"??? ?? ?????", L"Лучик света", L"Ein Schimmer Licht", L"Raio de luz", L"Lichtstraaltje", L"Promyk ?wiat?a", L"Bir I??k Huzmesi");
								break;
							case 82143:
								a = LL14(L"反攻の烽火", L"Beacon of Counterattack", L"Signal de contre-attaque", L"Segnale di contrattacco", L"Senal de contraataque", L"??? ??", L"反攻的烽火", L"????? ?????? ??????", L"Маяк контратаки", L"Leuchtfeuer des Gegenangriffs", L"Sinal de contra-ataque", L"Baken van de tegenaanval", L"Sygna? kontrataku", L"Kar?? Atak ??areti");
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
								a = LL14(L"今宵は宴と参りましょう", L"Tonight We Feast", L"Ce soir, nous festoyons", L"Stasera banchettiamo", L"Esta noche festejamos", L"?? ?? ??? ???", L"今?讓我們舉行宴會?", L"?????? ????? ?????", L"Сегодня мы пируем", L"Heute Abend wird gefeiert", L"Esta noite vamos festejar", L"Vanavond vieren we feest", L"Dzi? wieczorem ucztujemy", L"Bu Gece Ziyafet Cekelim");
								break;
							case 82159:
								a = "Flash Your Fighting Spirit";
								break;
							case 82161:
								a = LL14(L"鈍色に這う", L"Crawling in Gray", L"Ramper dans le gris", L"Strisciando nel grigio", L"Gateando en el gris", L"???? ????", L"在灰色中爬行", L"????? ?? ???????", L"Ползти в сером", L"Kriechen im Grau", L"Rastejando no cinza", L"Kruipen in het grijs", L"Pe?zanie w szaro?ci", L"Gri ?cinde Surunmek");
								break;
							case 82163:
								a = "Pyro Labyrinth";
								break;
							case 82164:
								a = LL14(L"優しさを未来に託して", L"Entrust Kindness to the Future", L"Confier la gentillesse au futur", L"Affidare la gentilezza al futuro", L"Confiar la amabilidad al futuro", L"???? ??? ???", L"將?柔託付給未來", L"??????? ????? ????????", L"Вверить доброту будущему", L"Gute der Zukunft anvertrauen", L"Confiar a bondade ao futuro", L"Vriendelijkheid aan de toekomst toevertrouwen", L"Powierzy? dobro? przysz?o?ci", L"Nezaketi Gelece?e Emanet Etmek");
								break;
							case 82166:
								a = LL14(L"高らかに、誇らしく", L"Loud and Proud", L"Fort et fier", L"Forte e fiero", L"Fuerte y orgulloso", L"???, ?????", L"高聲地，自豪地", L"???? ???? ?????", L"Громко и гордо", L"Laut und stolz", L"Alto e orgulhoso", L"Luid en trots", L"G?o?no i dumnie", L"Yuksek Sesle ve Gururla");
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
								a = LL14(L"このあと美味しくいただきました", L"Then We Ate Deliciously", L"Ensuite, nous avons mange delicieusement", L"Poi abbiamo mangiato deliziosamente", L"Luego comimos deliciosamente", L"?? ??? ?????", L"在那之後我們美味地享用了", L"?? ????? ????", L"Затем мы вкусно поели", L"Dann haben wir kostlich gegessen", L"Depois comemos deliciosamente", L"Daarna hebben we heerlijk gegeten", L"Potem zjedli?my wybornie", L"Sonra Afiyetle Yedik");
								break;
							case 82186:
								a = "Emergency Order";
								break;
							case 82188:
								a = LL14(L"激烈! 撃滅! ミシュナイダー!!", L"Fierce! Crush! Mishnayder!!", L"Feroce ! Ecraser ! Mishnayder !!", L"Feroce! Schiaccia! Mishnayder!!", L"!Feroz! !Aplasta! !Mishnayder!", L"??! ??! ?????!!", L"激烈！?滅！Mishnayder！！", L"???! ???! Mishnayder!!", L"Яростно! Разгромить! Mishnayder!!", L"Heftig! Zerschmettern! Mishnayder!!", L"Feroz! Esmagar! Mishnayder!!", L"Heftig! Verpletter! Mishnayder!!", L"Gwa?townie! Zmia?d?y?! Mishnayder!!", L"Sert! Ez Gec! Mishnayder!!");
								break;
							case 82189:
								a = "Life Goes On";
								break;
							default:
								if (a == L"ed8_inf_ex.opus") {
									a = LL14(L"夢幻の彼方へ", L"To the Realm of Dreams", L"Vers le royaume des reves", L"Verso il regno dei sogni", L"Hacia el reino de los suenos", L"??? ????", L"往夢幻的彼方", L"??? ???? ???????", L"В царство снов", L"In das Reich der Traume", L"Para o reino dos sonhos", L"Naar het rijk der dromen", L"Do krainy snow", L"Ruyalar Alemine");
								}
							}
							switch (_ttoi(a.Mid(2, 4))) {
							case 8001:
								a = LL14(L"特科クラス《VII組》", L"Class VII", L"Classe VII", L"Classe VII", L"Clase VII", L"?? ??? 《VII組》", L"特科班《VII組》", L"????? ??????", L"Класс VII", L"Klasse VII", L"Classe VII", L"Klas VII", L"Klasa VII", L"S?n?f VII");
								break;
							case 8002:
								a = LL14(L"スタートライン", L"Start Line", L"Ligne de depart", L"Linea di partenza", L"Linea de salida", L"??? ??", L"起?線", L"?? ???????", L"Стартовая линия", L"Startlinie", L"Linha de partida", L"Startlijn", L"Linia startu", L"Ba?lang?c Cizgisi");
								break;
							case 8003:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8004:
								a = "Youthful Victory";
								break;
							case 8006:
								a = LL14(L"ただひたすらに、前へ", L"Ever Forward", L"Toujours vers l'avant", L"Sempre avanti", L"Siempre adelante", L"??? ???", L"一心一意，向前邁進", L"??? ?????? ??????", L"Только вперед", L"Immer vorwarts", L"Sempre em frente", L"Altijd vooruit", L"Zawsze do przodu", L"Daima ?leri");
								break;
							case 8007:
								a = LL14(L"縁 -つなぐもの-", L"Fate -Connecting-", L"Destin -Connexion-", L"Destino -Connessione-", L"Destino -Conexion-", L"?? ~???? ?~", L"? -連?者-", L"????? - ???????", L"Судьба -Связующее звено-", L"Schicksal -Verbindend-", L"Destino -Conectando-", L"Lot -Verbindend-", L"Los -??cz?cy-", L"Kader -Ba?lay?c?-");
								break;
							case 8102:
								a = LL14(L"翡翠の公都バリアハート", L"Jade Capital Bareahard", L"Capitale de jade Bareahard", L"Capitale di giada Bareahard", L"Capital de jade Bareahard", L"??? ?? Bareahard", L"翡翠公都 Bareahard", L"????? ????? Bareahard", L"Нефритовая столица Bareahard", L"Jade-Hauptstadt Bareahard", L"Capital de jade Bareahard", L"Jade-hoofdstad Bareahard", L"Jadeitowa stolica Bareahard", L"Ye?im Ba?kenti Bareahard");
								break;
							case 8104:
								a = LL14(L"黒銀の鋼都ルーレ", L"Iron City Roer", L"Ville de fer Roer", L"Citta del ferro Roer", L"Ciudad del hierro Roer", L"??? ?? ?? Roer", L"黑銀鋼都 Roer", L"????? Roer ????????", L"Железный город Roer", L"Eisenstadt Roer", L"Cidade do ferro Roer", L"IJzerstad Roer", L"?elazne miasto Roer", L"Demir ?ehir Roer");
								break;
							case 8150:
								a = LL14(L"下校途中にパンケーキ", L"Pancakes on the Way Home", L"Des pancakes sur le chemin du retour", L"Pancake sulla via di casa", L"Tortitas de camino a casa", L"?? ?? ????", L"下學路上的煎餅", L"?????? ?? ???? ??????", L"Блинчики по дороге домой", L"Pfannkuchen auf dem Heimweg", L"Panquecas no caminho para casa", L"Pannenkoeken op weg naar huis", L"Nale?niki w drodze do domu", L"Eve Giderken Krep");
								break;
							case 8151:
								a = LL14(L"可能性は無限大", L"Infinite Possibilities", L"Possibilites infinies", L"Possibilita infinite", L"Posibilidades infinitas", L"???? ???", L"可能性是無限的", L"???????? ?? ??????", L"Бесконечные возможности", L"Unbegrenzte Moglichkeiten", L"Possibilidades infinitas", L"Oneindige mogelijkheden", L"Niesko?czone mo?liwo?ci", L"Sonsuz Olas?l?klar");
								break;
							case 8152:
								a = LL14(L"夜のしじまに", L"In the Night Silence", L"Dans le silence nocturne", L"Nel silenzio della notte", L"En el silencio de la noche", L"?? ?? ??", L"在深夜的靜謐中", L"?? ??? ?????", L"В ночной тишине", L"In der nachtlichen Stille", L"No silencio da noite", L"In de nachtelijke stilte", L"W nocnej ciszy", L"Gece Sessizli?inde");
								break;
							case 8153:
								a = LL14(L"夕景", L"Evening Scene", L"Scene de soiree", L"Scena serale", L"Escena vespertina", L"?? ??", L"夕陽美景", L"???? ??????", L"Вечерний пейзаж", L"Abendszene", L"Cena noturna", L"Avondtafereel", L"Wieczorna scena", L"Ak?am Manzaras?");
								break;
							case 8154:
								a = LL14(L"新しい朝", L"New Morning", L"Nouveau matin", L"Nuovo mattino", L"Nueva manana", L"??? ??", L"新的早晨", L"???? ????", L"Новое утро", L"Neuer Morgen", L"Nova manha", L"Nieuwe ochtend", L"Nowy poranek", L"Yeni Sabah");
								break;
							case 8155:
								a = LL14(L"束の間の里帰り", L"Brief Homecoming", L"Bref retour au pays", L"Breve ritorno a casa", L"Breve regreso al hogar", L"?? ??? ??", L"短暫的返郷", L"???? ????? ?????", L"Краткое возвращение домой", L"Kurze Heimkehr", L"Breve retorno ao lar", L"Korte thuiskomst", L"Krotki powrot do domu", L"K?sa Bir Memleket Donu?u");
								break;
							case 8156:
								a = LL14(L"白亜の旧都セントアーク", L"White City St. Ark", L"Vieille capitale blanche St. Ark", L"Antica capitale bianca St. Ark", L"Vieja capital blanca St. Ark", L"??? ??セントアーク", L"白亞舊都 St. Ark", L"??????? ??????? ??????? St. Ark", L"Белая старая столица Сент-Арк", L"Weise alte Hauptstadt St. Ark", L"Antiga capital branca St. Ark", L"Witte oude hoofdstad St. Ark", L"Bia?a stara stolica St. Ark", L"Beyaz Eski Ba?kent St. Ark");
								break;
							case 8157:
								a = LL14(L"紡績町パルム", L"Spinning Town Parm", L"Ville textile Parm", L"Citta tessile Parm", L"Pueblo textil Parm", L"?? ?? Parm", L"紡織鎮 Parm", L"???? ????? Parm", L"Ткацкий городок Парм", L"Spinnereistadt Parm", L"Vila textil Parm", L"Spinnerijstad Parm", L"Tkackie miasto Parm", L"Dokuma Kasabas? Parm");
								break;
							case 8158:
								a = LL14(L"籠の中のクロスベル", L"Crossbell in a Cage", L"Crossbell en cage", L"Crossbell in gabbia", L"Crossbell en una jaula", L"?? ?? Crossbell", L"籠中 Crossbell", L"Crossbell ?? ???", L"Кроссбелл в клетке", L"Crossbell im Kafig", L"Crossbell em uma gaiola", L"Crossbell in een kooi", L"Crossbell w klatce", L"Kafesteki Crossbell");
								break;
							case 8159:
								a = LL14(L"今、成すべきこと", L"What Must Be Done Now", L"Ce qui doit etre fait maintenant", L"Cio che deve essere fatto ora", L"Lo que debe hacerse ahora", L"??, ?? ? ?", L"現在，應做之事", L"?? ??? ???? ????", L"Что должно быть сделано сейчас", L"Was jetzt getan werden muss", L"O que deve ser feito agora", L"Wat nu moet worden gedaan", L"Co nale?y teraz zrobi?", L"?imdi Yap?lmas? Gereken");
								break;
							case 8160:
								a = LL14(L"歓楽都市ラクウェル", L"Pleasure City Raquel", L"Ville de plaisir Raquel", L"Citta del piacere Raquel", L"Ciudad del placer Raquel", L"?? ?? Raquel", L"歡樂都市 Raquel", L"????? ?????? Raquel", L"Город развлечений Ракель", L"Vergnugungsstadt Raquel", L"Cidade do prazer Raquel", L"Plezierstad Raquel", L"Miasto rozrywki Raquel", L"E?lence ?ehri Raquel");
								break;
							case 8161:
								a = LL14(L"静かなる駆け引き", L"Quiet Maneuvering", L"Manoeuvres silencieuses", L"Manovre silenziose", L"Maniobras silenciosas", L"??? ??", L"靜默的周旋", L"?????? ?????", L"Тихое маневрирование", L"Stilles Manovrieren", L"Manobras silenciosas", L"Stil manoeuvreren", L"Ciche manewry", L"Sessiz Manevralar");
								break;
							case 8162:
								a = LL14(L"赫奕たるヘイムダル", L"Splendid Heimdallr", L"Heimdallr splendide", L"Splendida Heimdallr", L"Esplendida Heimdallr", L"??? Heimdallr", L"赫赫有名的 Heimdallr", L"Heimdallr ???????", L"Великолепный Хеймдалль", L"Prachtiges Heimdallr", L"Esplendida Heimdallr", L"Prachtig Heimdallr", L"Wspania?y Heimdallr", L"Gorkemli Heimdallr");
								break;
							case 8163:
								a = LL14(L"紺碧の海都オルディス", L"Azure Port City Ordys", L"Ville portuaire d'azur Ordys", L"Citta portuale azzurra Ordys", L"Ciudad portuaria azul Ordys", L"??? ?? Ordys", L"紺碧海都 Ordys", L"????? Ordys ???????? ???????", L"Лазурный портовый город Ордис", L"Azurblaue Hafenstadt Ordys", L"Cidade portuaria azul Ordys", L"Azuurblauwe havenstad Ordys", L"B??kitne miasto portowe Ordys", L"Gok Mavisi Liman ?ehri Ordys");
								break;
							case 8164:
								a = LL14(L"最前線都市", L"Front-line City", L"Ville de premiere ligne", L"Citta di prima linea", L"Ciudad de primera linea", L"??? ??", L"最前線都市", L"????? ?????? ????????", L"Прифронтовой город", L"Frontstadt", L"Cidade de linha de frente", L"Frontstad", L"Miasto na linii frontu", L"Cephe ?ehri");
								break;
							case 8165:
								a = "Base Camp";
								break;
							case 8166:
								a = LL14(L"精強なる兵たち", L"Elite Soldiers", L"Soldats d'elite", L"Soldati d'elite", L"Soldados de elite", L"??? ???", L"精?的士兵們", L"???? ??????", L"Элитные солдаты", L"Elitesoldaten", L"Soldados de elite", L"Elitesoldaten", L"Elitarni ?o?nierze", L"Seckin Askerler");
								break;
							case 8168:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8170:
								a = LL14(L"隠れ里エリン", L"Hidden Village Erin", L"Village cache d'Erin", L"Villaggio nascosto di Erin", L"Aldea oculta de Erin", L"??? ?? ??", L"隠之里 Erin", L"???? Erin ???????", L"Скрытая деревня Эрин", L"Verborgenes Dorf Erin", L"Vila oculta de Erin", L"Verborgen dorp Erin", L"Ukryta wioska Erin", L"Gizli Koy Erin");
								break;
							case 8171:
								a = LL14(L"潜入調査", L"Infiltration", L"Infiltration", L"Infiltrazione", L"Infiltracion", L"?? ??", L"潛入調?", L"????", L"Инфильтрация", L"Infiltration", L"Infiltracao", L"Infiltratie", L"Infiltracja", L"S?zma Harekat?");
								break;
							case 8172:
								a = LL14(L"昏冥の中で", L"In the Darkness", L"Dans les tenebres", L"Nell'oscurita", L"En la oscuridad", L"?? ???", L"在昏暗之中", L"?? ??????", L"Во тьме", L"In der Dunkelheit", L"Na escuridao", L"In de duisternis", L"W ciemno?ci", L"Karanl?kta");
								break;
							case 8173:
								a = LL14(L"紅き閃影 -光まとう翼-", L"Crimson Flash -Wings of Light-", L"Eclat carmin -Ailes de lumiere-", L"Lampo cremisi -Ali di luce-", L"Destello carmesi -Alas de luz-", L"?? ?? ~?? ?? ??~", L"紅之閃影 -披光之翼-", L"???? ?????? - ????? ?????", L"Алая вспышка -Крылья света-", L"Purpurroter Blitz -Flugel des Lichts-", L"Lampejo carmesim -Asas de luz-", L"Karmozijnrode flits -Vleugels van licht-", L"Szkar?atny b?ysk -Skrzyd?a ?wiat?a-", L"K?z?l Par?lt? -I??k Kanatlar?-");
								break;
							case 8174:
								a = LL14(L"聖ウルスラ医科大学 -閃Ver.-", L"St. Ursula Medical College -CS Ver.-", L"St. Ursula Medical College -CS Ver.-", L"St. Ursula Medical College -CS Ver.-", L"St. Ursula Medical College -CS Ver.-", L"? ???? ???? -閃Ver.-", L"聖烏爾蘇拉醫科大學 -閃Ver.-", L"???? ???? ??????? ?????? -CS Ver.-", L"Медицинский колледж Св. Урсулы -CS Ver.-", L"Medizinische Hochschule St. Ursula -CS Ver.-", L"Faculdade de Medicina Sta. Ursula -CS Ver.-", L"Medisch College St. Ursula -CS Ver.-", L"Kolegium Medyczne ?w. Urszuli -CS Ver.-", L"Aziz Ursula T?p Koleji -CS Ver.-");
								break;
							case 8175:
								a = LL14(L"一抹の不安、一縷の望み", L"Hint of Unease, Ray of Hope", L"Une pointe d'inquietude, un rayon d'espoir", L"Un briciolo di ansia, un raggio di speranza", L"Un rastro de inquietud, un rayo de esperanza", L"??? ??, ? ?? ??", L"一抹不安，一縷希望", L"????? ?? ?????? ???? ?? ?????", L"Тень беспокойства, луч надежды", L"Ein Hauch von Unbehagen, ein Hoffnungsschimmer", L"Um toque de inquietacao, um raio de esperanca", L"Een spoortje van onrust, een straal van hoop", L"Cie? niepokoju, promie? nadziei", L"Bir Parca Huzursuzluk, Bir Umut I????");
								break;
							case 8176:
								a = "Lyrical Amber";
								break;
							case 8177:
								a = LL14(L"水面を渡る風", L"Wind Over the Water", L"Vent sur l'eau", L"Vento sull'acqua", L"Viento sobre el agua", L"??? ??? ??", L"拂過水面的風", L"???? ??? ?????", L"Ветер над водой", L"Wind uber dem Wasser", L"Vento sobre a agua", L"Wind over het water", L"Wiatr nad wod?", L"Su Ustundeki Ruzgar");
								break;
							case 8250:
								a = LL14(L"流れる雲の彼方に", L"Beyond the Drifting Clouds", L"Au-dela des nuages derivants", L"Oltre le nuvole erranti", L"Mas alla de las nubes errantes", L"??? ?? ????", L"流雲的彼方", L"?? ???? ????? ???????", L"За плывущими облаками", L"Jenseits der ziehenden Wolken", L"Alem das nuvens flutuantes", L"Voorbij de drijvende wolken", L"Poza p?yn?ce chmury", L"Suzulen Bulutlar?n Otesinde");
								break;
							case 8251:
								a = LL14(L"静寂の小路", L"Path of Silence", L"Chemin du silence", L"Sentiero del silenzio", L"Senda del silencio", L"??? ??", L"安靜的小徑", L"???? ?????", L"Путь тишины", L"Pfad der Stille", L"Caminho do silencio", L"Pad van stilte", L"?cie?ka ciszy", L"Sessizlik Yolu");
								break;
							case 8252:
								a = LL14(L"崖谷の狭間", L"Gap of the Cliff", L"Le fosse de la falaise", L"Divario della scogliera", L"Brecha del acantilado", L"?? ??? ?", L"崖谷狹間", L"???? ?????", L"Разрыв утеса", L"Spalt der Klippe", L"Fenda do penhasco", L"Kloof van de klif", L"Szczelina klifu", L"Ucurum Bo?lu?u");
								break;
							case 8253:
								a = "Weathering Road";
								break;
							case 8260:
								a = LL14(L"彼の地へ向かって", L"Toward That Land", L"Vers cette terre", L"Verso quella terra", L"Hacia esa tierra", L"? ?? ???", L"邁向那片土地", L"??? ??? ?????", L"К той земле", L"Jenem Land entgegen", L"Em direcao aquela terra", L"Naar dat land", L"Ku tamtej krainie", L"O Diyara Do?ru");
								break;
							case 8261:
								a = LL14(L"終焉の途へ", L"Toward the End", L"Vers la fin", L"Verso la fine", L"Hacia el final", L"??? ??", L"邁向終結", L"??? ???????", L"К концу", L"Dem Ende entgegen", L"Em direcao ao fim", L"Naar het einde", L"Ku ko?cowi", L"Sona Do?ru");
								break;
							case 8262:
								a = LL14(L"全てを識るもの -閃Ver.-", L"Omniscient -CS Ver.-", L"L'omniscient -CS Ver.-", L"L'onniscente -CS Ver.-", L"El omnisciente -CS Ver.-", L"?? ?? ?? ? -閃Ver.-", L"全知者 -閃Ver.-", L"?????? -CS Ver.-", L"Всеведущий -CS Ver.-", L"Der Allwissende -CS Ver.-", L"O onisciente -CS Ver.-", L"De alwetende -CS Ver.-", L"Wszechwiedz?cy -CS Ver.-", L"Her ?eyi Bilen -CS Ver.-");
								break;
							case 8263:
								a = LL14(L"たそがれ緑道", L"Twilight Green Path", L"Chemin vert du crepuscule", L"Sentiero verde del crepuscolo", L"Senda verde del crepusculo", L"??? ??", L"黄昏綠道", L"???? ????? ??????", L"Сумеречная зеленая тропа", L"Zwielichtiger gruner Pfad", L"Caminho verde do crepusculo", L"Groene schemerpad", L"Zielona ?cie?ka zmierzchu", L"Alacakaranl?k Ye?il Yolu");
								break;
							case 8311:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8350:
								a = LL14(L"アインヘル小要塞", L"Einhel Fortress", L"Forteresse d'Einhel", L"Fortezza di Einhel", L"Fortaleza de Einhel", L"Einhel ???", L"Einhel 小要塞", L"??? Einhel", L"Крепость Эйнхель", L"Einhel-Festung", L"Fortaleza de Einhel", L"Vesting Einhel", L"Twierdza Einhel", L"Einhel Kalesi");
								break;
							case 8351:
								a = LL14(L"伝承の裏で", L"Behind the Legend", L"Derriere la legende", L"Dietro la leggenda", L"Detras de la leyenda", L"??? ????", L"傳承的背後", L"??? ????????", L"За легендой", L"Hinter der Legende", L"Atras da lenda", L"Achter de legende", L"Za legend?", L"Efsanenin Arkas?nda");
								break;
							case 8352:
								a = "Unplanned Residue";
								break;
							case 8353:
								a = LL14(L"忘れられし幻夢の狭間 -閃Ver.-", L"Forgotten Phantasmal Gap -CS Ver.-", L"Ecart phantasmatique oublie -CS Ver.-", L"Divario fantasmatico dimenticato -CS Ver.-", L"Brecha fantasmal olvidada -CS Ver.-", L"??? ??? ?? -閃Ver.-", L"被遺忘的幻夢狹間 -閃Ver.-", L"?????? ???????? ??????? -CS Ver.-", L"Забытый призрачный разрыв -CS Ver.-", L"Vergessener phantasmagorischer Spalt -CS Ver.-", L"Fenda fantasmal esquecida -CS Ver.-", L"Vergeten fantoomkloof -CS Ver.-", L"Zapomniana fantastyczna szczelina -CS Ver.-", L"Unutulmu? Hayali Bo?luk -CS Ver.-");
								break;
							case 8354:
								a = LL14(L"幽世の気配", L"Atmosphere of the Netherworld", L"Atmosphere de l'au-dela", L"Atmosfera dell'oltretomba", L"Atmosfera del inframundo", L"??? ??", L"幽世之氣息", L"????? ?????? ??????", L"Атмосфера преисподней", L"Atmosphare der Unterwelt", L"Atmosfera do submundo", L"Sfeer van de onderwereld", L"Atmosfera za?wiatow", L"Obur Dunyan?n Havas?");
								break;
							case 8355:
								a = "solid as the Rock of JUNO";
								break;
							case 8356:
								a = LL14(L"地下に巣喰う", L"Nesting Underground", L"Nicher sous terre", L"Nidificare sottoterra", L"Anidando bajo tierra", L"??? ??? ??", L"盤據地下", L"??????? ??? ?????", L"Гнездование под землей", L"Unterirdisches Nisten", L"Aninhando-se no subsolo", L"Ondergronds nestelen", L"Gnie?d?enie si? pod ziemi?", L"Yeralt?ndaki Yuva");
								break;
							case 8359:
								a = "Spiral of Erebos";
								break;
							case 8360:
								a = LL14(L"鋼の障壁", L"Steel Barrier", L"Barriere d'acier", L"Barriera d'acciaio", L"Barrera de acero", L"??? ??", L"鋼鐵障壁", L"???? ??????", L"Стальной барьер", L"Stahlbarriere", L"Barreira de aco", L"Stalen barriere", L"Stalowa bariera", L"Celik Bariyer");
								break;
							case 8363:
								a = "Break In";
								break;
							case 8365:
								a = LL14(L"サングラール迷宮", L"Sanglar Maze", L"Labyrinthe de Sanglar", L"Labirinto di Sanglar", L"Laberinto de Sanglar", L"Sanglar ??", L"Sanglar 迷宮", L"????? Sanglar", L"Лабиринт Санглар", L"Sanglar-Labyrinth", L"Labirinto de Sanglar", L"Sanglar doolhof", L"Labirynt Sanglar", L"Sanglar Labirenti");
								break;
							case 8366:
								a = LL14(L"静けき森の魔女", L"Witch of the Silent Forest", L"Sorciere de la foret silencieuse", L"Strega della foresta silenziosa", L"Bruja del bosque silencioso", L"??? ?? ??", L"靜謐森林的魔女", L"????? ?????? ???????", L"Ведьма тихого леса", L"Hexe des stillen Waldes", L"Bruxa da floresta silenciosa", L"Heks van het stille woud", L"Wied?ma z cichego lasu", L"Sessiz Orman?n Cad?s?");
								break;
							case 8367:
								a = LL14(L"Mystic Core -閃Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -閃Ver.-", L"Mystic Core -閃Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-", L"Mystic Core -CS Ver.-");
								break;
							case 8368:
								a = LL14(L"斉いし舞台", L"Unified Stage", L"Scene unifiee", L"Palcoscenico unificato", L"Escenario unificado", L"???? ??", L"齊整的舞台", L"?????? ??????", L"Единая сцена", L"Vereinte Buhne", L"Palco unificado", L"Verenigd podium", L"Zunifikowana scena", L"Birle?mi? Sahne");
								break;
							case 8369:
								a = LL14(L"シンクロニシティ #23", L"Synchronicity #23", L"Synchronicite #23", L"Sincronicita #23", L"Sincronicidad #23", L"?????? #23", L"共時性 #23", L"??????? #23", L"Синхронность #23", L"Synchronizitat #23", L"Sincronicidade #23", L"Synchroniciteit #23", L"Synchroniczno?? #23", L"E?zamanl?l?k #23");
								break;
							case 8371:
								a = LL14(L"世界の命運を賭けて", L"Betting on the World's Fate", L"Parier sur le destin du monde", L"Scommettendo sul destino del mondo", L"Apostando por el destino del mundo", L"??? ??? ??", L"賭上世界的命運", L"?????? ??? ???? ??????", L"Ставя на кон судьбу мира", L"Auf das Schicksal der Welt setzen", L"Apostando no destino do mundo", L"Inzetten op het lot van de wereld", L"Stawiaj?c na losy ?wiata", L"Dunyan?n Kaderi Uzerine Bahis");
								break;
							case 8372:
								a = "The End of -SAGA-";
								break;
							case 8429:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8450:
								a = "Brave Steel";
								break;
							case 8451:
								a = "Toughness!!";
								break;
							case 8452:
								a = LL14(L"剣戟怒涛", L"Sword and Lance Storm", L"Tempete d'epees et de lances", L"Tempesta di spade e lance", L"Tormenta de espadas y lanzas", L"????", L"劍戟怒濤", L"????? ????? ??????", L"Шторм мечей и копий", L"Schwert- und Lanzensturm", L"Tempestade de espadas e lancas", L"Zwaard- en lansstorm", L"Burza mieczy i w?oczni", L"K?l?c ve M?zrak F?rt?nas?");
								break;
							case 8453:
								a = "Proud Grudge";
								break;
							case 8454:
								a = LL14(L"チープ・トラップ", L"Cheap Trap", L"Piege bon marche", L"Trappola a buon mercato", L"Trampa barata", L"?? ??", L"便宜的陷?", L"?? ????", L"Дешевая ловушка", L"Billige Falle", L"Armadilha barata", L"Goedkope val", L"Tania pu?apka", L"Ucuz Tuzak");
								break;
							case 8455:
								a = "STEP AHEAD";
								break;
							case 8456:
								a = LL14(L"劣勢を挽回せよ！", L"Turn the Tide!", L"Inversez la tendance !", L"Inverti la rotta!", L"!Cambia la marea!", L"??? ????!", L"挽回劣勢！", L"???? ????????!", L"Переломи ход событий!", L"Das Blatt wenden!", L"Vire o jogo!", L"Keer het tij!", L"Odwro? losy!", L"Gidi?at? De?i?tir!");
								break;
							case 8457:
								a = "Abrupt Visitor";
								break;
							case 8458:
								a = LL14(L"行き着く先 -Opening Size-", L"Destination -Opening Size-", L"Destination -Opening Size-", L"Destinazione -Opening Size-", L"Destino -Opening Size-", L"???? ? -Opening Size-", L"抵達之處 -Opening Size-", L"?????? - Opening Size", L"Место назначения -Opening Size-", L"Zielort -Opening Size-", L"Destino -Opening Size-", L"Bestemming -Opening Size-", L"Miejsce docelowe -Opening Size-", L"Var?? Noktas? -Opening Size-");
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
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8466:
								a = "Erosion of Madness";
								break;
							case 8467:
								a = "DOOMSDAY TRANCE";
								break;
							case 8468:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
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
								a = LL14(L"古の盟約", L"Ancient Covenant", L"Ancienne alliance", L"Antico patto", L"Antiguo pacto", L"??? ??", L"古代盟約", L"????? ??????", L"Древний завет", L"Alter Bund", L"Antigo pacto", L"Oud verbond", L"Staro?ytne przymierze", L"Kadim Sozle?me");
								break;
							case 8476:
								a = LL14(L"七の相克 -EXCELLION KRIEG-", L"Seven Antagonisms -EXCELLION KRIEG-", L"Sept antagonismes -EXCELLION KRIEG-", L"Sette antagonismi -EXCELLION KRIEG-", L"Siete antagonismos -EXCELLION KRIEG-", L"?? ?? -EXCELLION KRIEG-", L"七之相克 -EXCELLION KRIEG-", L"???????? ?????? - EXCELLION KRIEG", L"Семь противостояний -EXCELLION KRIEG-", L"Sieben Antagonismen -EXCELLION KRIEG-", L"Sete antagonismos -EXCELLION KRIEG-", L"Zeven tegenstellingen -EXCELLION KRIEG-", L"Siedem antagonizmow -EXCELLION KRIEG-", L"Yedi Kar??tl?k -EXCELLION KRIEG-");
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
								a = LL14(L"授業は合同で", L"Joint Class", L"Cours commun", L"Classe congiunta", L"Clase conjunta", L"??? ????", L"聯合授課", L"??? ?????", L"Совместное занятие", L"Gemeinsamer Unterricht", L"Aula conjunta", L"Gezamenlijke les", L"Wspolna lekcja", L"Ortak Ders");
								break;
							case 8501:
								a = "Power or Technique";
								break;
							case 8502:
								a = "Briefing Time";
								break;
							case 8503:
								a = LL14(L"第II分校の日常", L"Daily Life at Branch II", L"Vie quotidienne a la Branche II", L"Vita quotidiana alla Branca II", L"Vida cotidiana en la Rama II", L"?II??? ??", L"第II分校的日常", L"?????? ??????? ?? ????? ??????", L"Будни во втором филиале", L"Alltag in Zweigstelle II", L"Vida cotidiana na Filial II", L"Dagelijks leven in Afdeling II", L"?ycie codzienne w Filii II", L"2. ?ubede Gunluk Ya?am");
								break;
							case 8504:
								a = LL14(L"充実したひととき", L"Satisfying Moment", L"Moment satisfaisant", L"Momento soddisfacente", L"Momento satisfactorio", L"??? ??", L"充實的時光", L"???? ?????", L"Насыщенный момент", L"Erfullter Moment", L"Momento gratificante", L"Bevredigend moment", L"Satysfakcjonuj?ca chwila", L"Tatmin Edici Bir An");
								break;
							case 8505:
								a = LL14(L"異端の研究者", L"Heretic Researcher", L"Chercheur heretique", L"Ricercatore eretico", L"Investigador heretico", L"??? ???", L"異端研究者", L"???? ??????", L"Исследователь-еретик", L"Haretischer Forscher", L"Pesquisador heretico", L"Ketters onderzoeker", L"Badacz heretycki", L"Sapk?n Ara?t?rmac?");
								break;
							case 8506:
								a = LL14(L"君に伝えたいこと", L"What I Want to Tell You", L"Ce que je veux te dire", L"Cio che voglio dirti", L"Lo que quiero decirte", L"??? ??? ?? ?", L"想傳達給?的事", L"?? ???? ?? ????? ??", L"То, что я хочу тебе сказать", L"Was ich dir sagen mochte", L"O que eu quero te dizer", L"Wat ik je wil vertellen", L"To, co chc? ci powiedzie?", L"Sana Soylemek ?stedi?im ?ey");
								break;
							case 8507: case 8508:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8509:
								a = LL14(L"張り詰めた思惑", L"Tense Speculation", L"Speculation tendue", L"Tesa speculazione", L"Especulacion tensa", L"??? ??", L"緊繃的意図", L"?????? ??????", L"Напряженное ожидание", L"Gespannte Spekulation", L"Especulacao tensa", L"Gespannen speculatie", L"Napi?te spekulacje", L"Gergin Bekleyi?");
								break;
							case 8510:
								a = LL14(L"混迷の対立", L"Chaotic Conflict", L"Conflit chaotique", L"Conflitto caotico", L"Conflicto caotico", L"??? ??", L"迷惘的對立", L"???? ?????", L"Хаотичный конфликт", L"Chaotischer Konflikt", L"Conflito caotico", L"Chaotisch conflict", L"Chaotyczny konflikt", L"Kaotik Cat??ma");
								break;
							case 8511:
								a = LL14(L"急転直下", L"Sudden Turn", L"Tournant soudain", L"Svolta improvvisa", L"Giro repentino", L"????", L"急轉直下", L"???? ?????", L"Внезапный поворот", L"Plotzliche Wendung", L"Reviravolta subita", L"Plotselinge wending", L"Nag?y zwrot", L"Ani Donu?");
								break;
							case 8512:
								a = LL14(L"蠢く陰謀", L"Writhing Conspiracy", L"Complot rampant", L"Cospirazione strisciante", L"Conspiracion reptante", L"???? ??", L"蠢動的陰謀", L"?????? ??????", L"Ползучий заговор", L"Sich windende Verschworung", L"Conspiracao rastejante", L"Kronkelende samenzwering", L"Wij?c si? spisek", L"Kaynayan Komplo");
								break;
							case 8513:
								a = LL14(L"託されたもの", L"Entrusted One", L"Celui a qui on a confie", L"Colui a cui e stato affidato", L"A quien se le confio", L"??? ?", L"被託付之物", L"????????", L"Вверенный", L"Der Anvertraute", L"O confiado", L"De toevertrouwde", L"Powierzony", L"Emanet Edilen");
								break;
							case 8514:
								a = LL14(L"羅刹の薫陶", L"Rasetsu's Guidance", L"L'influence de Rasetsu", L"La guida di Rasetsu", L"La guia de Rasetsu", L"???? ??", L"羅刹的教化", L"????? Rasetsu", L"Наставление Расецу", L"Rasetsus Fuhrung", L"Orientacao de Rasetsu", L"Rasetsu's begeleiding", L"Wskazowki Rasetsu", L"Rasetsu'nun Rehberli?i");
								break;
							case 8515:
								a = LL14(L"ハーメル -遺されたもの-", L"Hamel -What Was Left Behind-", L"Hamel -Ce qui a ete laisse-", L"Hamel -Cio che e rimasto-", L"Hamel -Lo que quedo atras-", L"?? ~??? ?~", L"哈梅爾 -遺留之物-", L"Hamel - ?? ????", L"Хамель -Что осталось позади-", L"Hamel -Was zuruckblieb-", L"Hamel -O que foi deixado para tras-", L"Hamel -Wat achterbleef-", L"Hamel -Co pozosta?o-", L"Hamel -Geride Kalanlar-");
								break;
							case 8516:
								a = LL14(L"Welcome Back! アーベントタイム(ラジオ)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (???)", L"Welcome Back! Evening Time (廣播)", L"Welcome Back! Evening Time (?????)", L"Welcome Back! Evening Time (радио)", L"Welcome Back! Evening Time (Radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (radio)", L"Welcome Back! Evening Time (Radyo)");
								break;
							case 8517: case 8519:
								a = LL14(L"夏至祭", L"Summer Solstice Festival", L"Festival du solstice d'ete", L"Festival del solstizio d'estate", L"Festival del solsticio de verano", L"???", L"夏至祭", L"?????? ???????? ??????", L"Фестиваль летнего солнцестояния", L"Sommersonnenwendfest", L"Festival do solsticio de verao", L"Midzomerfestival", L"Festiwal przesilenia letniego", L"Yaz Gundonumu Festivali");
								break;
							case 8520:
								a = LL14(L"翡翠庭園", L"Jade Garden", L"Jardin de jade", L"Giardino di giada", L"Jardin de jade", L"?? ??", L"翡翠庭園", L"????? ?????", L"Нефритовый сад", L"Jade-Garten", L"Jardim de jade", L"Jade tuin", L"Jadeitowy ogrod", L"Ye?im Bahcesi");
								break;
							case 8521:
								a = LL14(L"初めての円舞曲", L"First Waltz", L"Premiere valse", L"Primo valzer", L"Primer vals", L"? ???", L"第一首圓舞曲", L"?????? ?????", L"Первый вальс", L"Erster Walzer", L"Primeira valsa", L"Eerste wals", L"Pierwszy walc", L"?lk Vals");
								break;
							case 8522:
								a = LL14(L"真打ち登場！", L"Headliner's Entrance!", L"Entree de la vedette !", L"Entrata del protagonista!", L"!Entrada del protagonista!", L"??? ??!", L"壓軸登場！", L"???? ?????!", L"Выход главной звезды!", L"Auftritt des Hauptactes!", L"Entrada da atracao principal!", L"Entree van de hoofdact!", L"Wej?cie gwiazdy wieczoru!", L"As?l Sanatc?n?n Giri?i!");
								break;
							case 8524:
								a = "Tragedy";
								break;
							case 8528:
								a = LL14(L"僅かな希望の先に", L"Beyond Slight Hope", L"Au-dela d'un mince espoir", L"Oltre una sottile speranza", L"Mas alla de una pequena esperanza", L"??? ?? ???", L"在微小的希望之後", L"?? ???? ??? ????", L"За хрупкой надеждой", L"Jenseits einer leisen Hoffnung", L"Alem de uma pequena esperanca", L"Voorbij een sprankje hoop", L"Poza nik?? nadziej?", L"Kucuk Bir Umudun Otesinde");
								break;
							case 8530:
								a = LL14(L"帰路へ", L"On the Road Home", L"Sur le chemin du retour", L"Sulla via di casa", L"En el camino a casa", L"???", L"歸途", L"?? ???? ??????", L"На пути домой", L"Auf dem Heimweg", L"No caminho para casa", L"Op weg naar huis", L"W drodze do domu", L"Eve Donu? Yolunda");
								break;
							case 8532:
								a = "Roots of Scar";
								break;
							case 8534:
								a = LL14(L"想い千里を走り", L"Feelings Run a Thousand Miles", L"Les sentiments parcourent mille lieues", L"I sentimenti corrono per mille miglia", L"Los sentimientos corren mil millas", L"??? ??? ??", L"思念奔馳千里", L"??????? ???? ??? ???", L"Чувства бегут за тысячи миль", L"Gefuhle eilen tausend Meilen", L"Sentimentos correm mil milhas", L"Gevoelens leggen duizend mijlen af", L"Uczucia biegn? tysi?c mil", L"Duygular Bin Mil Ko?ar");
								break;
							case 8536:
								a = LL14(L"光射す空の下で", L"Under the Shining Sky", L"Sous le ciel radieux", L"Sotto il cielo splendente", L"Bajo el cielo resplandeciente", L"? ??? ?? ????", L"在光芒照射的天空下", L"??? ?????? ???????", L"Под сияющим небом", L"Unter dem strahlenden Himmel", L"Sob o ceu brilhante", L"Onder de stralende hemel", L"Pod l?ni?cym niebem", L"I??ldayan Gokyuzu Alt?nda");
								break;
							case 8539:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8541:
								a = LL14(L"空を見上げて -Eliot Ver.-", L"Look Up at the Sky -Eliot Ver.-", L"Regarder le ciel -Eliot Ver.-", L"Guarda il cielo -Eliot Ver.-", L"Mira al cielo -Eliot Ver.-", L"??? ????? -Eliot Ver.-", L"仰望天空 -Eliot Ver.-", L"???? ??? ?????? -Eliot Ver.-", L"Посмотри на небо -Eliot Ver.-", L"Blick in den Himmel -Eliot Ver.-", L"Olhe para o ceu -Eliot Ver.-", L"Kijk naar de lucht -Eliot Ver.-", L"Spojrz w niebo -Eliot Ver.-", L"Gokyuzune Bak -Eliot Ver.-");
								break;
							case 8542: case 8543:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8544:
								a = "Little Rain";
								break;
							case 8545:
								a = LL14(L"暗雲", L"Dark Clouds", L"Nuages sombres", L"Nubi oscure", L"Nubes oscuras", L"???", L"暗雲", L"??? ?????", L"Темные тучи", L"Dunkle Wolken", L"Nuvens escuras", L"Donkere wolken", L"Ciemne chmury", L"Kara Bulutlar");
								break;
							case 8546:
								a = LL14(L"鐘、鳴り響く時", L"When the Bell Tolls", L"Quand la cloche sonne", L"Quando suona la campana", L"Cuando dobla la campana", L"?? ?? ?? ?", L"鐘聲響徹之時", L"????? ??? ?????", L"Когда бьет колокол", L"Wenn die Glocke lautet", L"Quando o sino toca", L"Wanneer de klok luidt", L"Kiedy bije dzwon", L"Canlar Cald???nda");
								break;
							case 8547:
								a = LL14(L"巨イナル黄昏", L"Giant Twilight", L"Crepuscule geant", L"Crepuscolo gigante", L"Crepusculo gigante", L"??? ??", L"巨大的黄昏", L"????? ???????", L"Великие сумерки", L"Riesige Dammerung", L"Crepusculo gigante", L"Gigantische schemering", L"Wielki zmierzch", L"Muazzam Alacakaranl?k");
								break;
							case 8548:
								a = LL14(L"あの日の約束", L"That Day's Promise", L"La promesse de ce jour-la", L"La promessa di quel giorno", L"La promesa de aquel dia", L"??? ??", L"那天的約定", L"??? ??? ?????", L"Обещание того дня", L"Das Versprechen von jenem Tag", L"A promessa daquele dia", L"De belofte van die dag", L"Obietnica tamtego dnia", L"O Gunku Soz");
								break;
							case 8551:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8553:
								a = "Sensitive Talk";
								break;
							case 8554:
								a = LL14(L"哀花", L"Mournful Flower", L"Fleur de deuil", L"Fiore di lutto", L"Flor de luto", L"??", L"哀花", L"???? ?????", L"Траурный цветок", L"Trauerblume", L"Flor de luto", L"Rouwbloem", L"?a?obny kwiat", L"Yas Cice?i");
								break;
							case 8555:
								a = "Feel at Home";
								break;
							case 8556:
								a = LL14(L"幾千万の夜を越えて", L"Beyond Countless Nights", L"Au-dela d'innombrables nuits", L"Oltre innumerevoli notti", L"Mas alla de incontables noches", L"??? ?を ???", L"跨越數千萬個夜?", L"??? ?????? ???????", L"Сквозь миллионы ночей", L"Jenseits von Millionen Nachten", L"Alem de milhoes de noites", L"Voorbij miljoenen nachten", L"Poza miliony nocy", L"Milyonlarca Gecenin Otesinde");
								break;
							case 8557: case 8558:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8559:
								a = LL14(L"優しき微睡み", L"Gentle Slumber", L"Sommeil paisible", L"Dolce sonno", L"Dulce sueno", L"??? ?", L"?柔的微睡", L"???? ????", L"Нежная дремота", L"Sanfter Schlummer", L"Sono suave", L"Zachte sluimer", L"?agodny sen", L"Nazik Uyku");
								break;
							case 8560:
								a = LL14(L"最悪の最善手", L"Best Move in the Worst Situation", L"Meilleur coup dans la pire situation", L"Mossa migliore nella peggiore situazione", L"Mejor jugada en la peor situacion", L"?? ?? ??", L"最壞情況中的最佳對策", L"???? ???? ?? ???? ???", L"Лучший ход в худшей ситуации", L"Bester Zug in der schlimmsten Lage", L"Melhor jogada na pior situacao", L"Beste zet in de slechtste situatie", L"Najlepszy ruch w najgorszej sytuacji", L"En Kotu Durumdaki En ?yi Hamle");
								break;
							case 8562:
								a = LL14(L"黒の真実", L"Black Truth", L"Verite noire", L"Verita nera", L"Verdad negra", L"?? ??", L"黑之真實", L"????? ?????", L"Черная правда", L"Schwarze Wahrheit", L"Verdade negra", L"Zwarte waarheid", L"Czarna prawda", L"Siyah Gercek");
								break;
							case 8563:
								a = LL14(L"いつでもそばに", L"Always by Your Side", L"Toujours a tes cotes", L"Sempre al tuo fianco", L"Siempre a tu lado", L"??? ??", L"永遠在身邊", L"?????? ??????", L"Всегда рядом", L"Immer an deiner Seite", L"Sempre ao seu lado", L"Altijd aan je zijde", L"Zawsze przy tobie", L"Daima Yan?nda");
								break;
							case 8564:
								a = LL14(L"その温もりは小さいけれど。", L"That warmth is small, but.", L"Cette chaleur est petite, mais.", L"Quel calore e piccolo, ma.", L"Ese calor es pequeno, pero.", L"? ??? ???.", L"那??暖雖小。", L"??? ????? ????? ???.", L"Это тепло мало, но.", L"Diese Warme ist klein, aber.", L"Aquele calor e pequeno, mas.", L"Die warmte is klein, maar.", L"To ciep?o jest ma?e, ale.", L"Bu s?cakl?k kucuk, ama.");
								break;
							case 8566:
								a = LL14(L"それでも前へ", L"Still Forward", L"Tout de meme vers l'avant", L"Ancora avanti", L"Aun asi, adelante", L"??? ???", L"即便如此依然向前", L"??? ???? ??? ??????", L"Все равно вперед", L"Trotzdem vorwarts", L"Ainda assim, em frente", L"Toch vooruit", L"Mimo to do przodu", L"Yine de ?leri");
								break;
							case 8570:
								a = LL14(L"想いひとつに", L"Hearts as One", L"C?urs unis", L"Cuori come uno", L"Corazones como uno", L"?? ???", L"心意合一", L"???? ?????", L"Сердца как одно", L"Herzen eins", L"Coracoes como um", L"Harten als een", L"Serca jako jedno", L"Kalpler Bir");
								break;
							case 8571:
								a = LL14(L"千年要塞", L"Millennium Fortress", L"Forteresse millenaire", L"Fortezza millenaria", L"Fortaleza milenaria", L"?? ??", L"千年要塞", L"??? ???????", L"Тысячелетняя крепость", L"Jahrtausendfestung", L"Fortaleza milenar", L"Millenniumvesting", L"Tysi?cletnia twierdza", L"Bin Y?ll?k Kale");
								break;
							case 8572:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8573:
								a = LL14(L"せめてこの夜に誓って", L"At Least Swear Tonight", L"Au moins, jure ce soir", L"Almeno giura stasera", L"Al menos jura esta noche", L"??? ? ?? ????", L"至少在今夜發誓", L"??? ????? ???? ??????", L"По крайней мере, поклянись сегодня", L"Schwore zumindest heute Nacht", L"Pelo menos jure esta noite", L"Zweer tenminste vanavond", L"Przynajmniej przysi?gnij dzi?", L"En Az?ndan Bu Gece Yemin Et");
								break;
							case 8574:
								a = "Constraint";
								break;
							case 8575:
								a = LL14(L"過ぎ去りし日々", L"Days Gone By", L"Jours passes", L"Giorni passati", L"Dias pasados", L"??? ??", L"逝去的日子", L"???? ???", L"Минувшие дни", L"Vergangene Tage", L"Dias passados", L"Voorbijgegane dagen", L"Minione dni", L"Gecip Giden Gunler");
								break;
							case 8576:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8577:
								a = LL14(L"それぞれの覚悟", L"Each One's Resolve", L"La resolution de chacun", L"La risoluzione di ognuno", L"La resolucion de cada uno", L"??? ??", L"各自的覺悟", L"????? ?? ????", L"Решимость каждого", L"Die Entschlossenheit jedes Einzelnen", L"A determinacao de cada um", L"Ieders eigen vastberadenheid", L"Determinacja ka?dego z nas", L"Her Birimizin Kararl?l???");
								break;
							case 8578:
								a = LL14(L"無明の闇の中で", L"In the Darkness", L"Dans les tenebres sans fin", L"Nell'oscurita eterna", L"En la oscuridad eterna", L"??? ?? ???", L"在無明之暗中", L"?? ?????? ??????", L"В вечной тьме", L"In ewiger Finsternis", L"Na escuridao eterna", L"In de eeuwige duisternis", L"W wiecznej ciemno?ci", L"Sonsuz Karanl?kta");
								break;
							case 8579:
								a = LL14(L"変わる世界 -闇の底から-", L"Changing World -From the Depths of Darkness-", L"Monde changeant -Du fond des tenebres-", L"Mondo che cambia -Dal profondo delle tenebre-", L"Mundo cambiante -Desde el fondo de la oscuridad-", L"??? ?? ~??? ????~", L"變化的世界 -從黑暗深處-", L"???? ????? - ?? ????? ??????", L"Меняющийся мир -Из глубин тьмы-", L"Sich wandelnde Welt -Aus den Tiefen der Finsternis-", L"Mundo em mudanca -Do fundo da escuridao-", L"Veranderende wereld -Uit de diepten van de duisternis-", L"Zmieniaj?cy si? ?wiat -Z g??bi ciemno?ci-", L"De?i?en Dunya -Karanl???n Derinliklerinden-");
								break;
							case 8600:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8601:
								a = LL14(L"ゲートイン", L"Gate In", L"Entree en piste", L"Ingresso ai cancelli", L"Entrada a gateras", L"??? ?", L"進入閘門", L"???? ???????", L"Вход в ворота", L"Einzug", L"Entrada no portao", L"Binnenkomst", L"Wjazd na bramk?", L"Giri?");
								break;
							case 8602:
								a = LL14(L"不明(空の軌跡)", L"Unknown(Sky)", L"Inconnu(Sky)", L"Sconosciuto(Sky)", L"Desconocido(Sky)", L"??(??)", L"不明(空之軌跡)", L"??? ?????(Sky)", L"Неизвестно(Sky)", L"Unbekannt(Sky)", L"Desconhecido(Sky)", L"Onbekend(Sky)", L"Nieznany(Sky)", L"Bilinmeyen(Sky)");
								break;
							case 8603:
								a = LL14(L"女神はいつも見ています", L"The Goddess is Always Watching", L"La deesse regarde toujours", L"La dea guarda sempre", L"La diosa siempre observa", L"??? ??? ?? ????", L"女神一直在注視著", L"?????? ????? ??????", L"Богиня всегда наблюдает", L"Die Gottin wacht immer", L"A deusa esta sempre olhando", L"De godin kijkt altijd toe", L"Bogini zawsze patrzy", L"Tanr?ca Daima ?zliyor");
								break;
							case 8604:
								a = LL14(L"不明(空の軌跡)", L"Unknown(Sky)", L"Inconnu(Sky)", L"Sconosciuto(Sky)", L"Desconocido(Sky)", L"??(??)", L"不明(空之軌跡)", L"??? ?????(Sky)", L"Неизвестно(Sky)", L"Unbekannt(Sky)", L"Desconhecido(Sky)", L"Onbekend(Sky)", L"Nieznany(Sky)", L"Bilinmeyen(Sky)");
								break;
							case 8605: case 8606: case 8608: case 8610: case 8611: case 8612:
							case 8613: case 8614: case 8616: case 8617: case 8618: case 8619:
							case 8620: case 8621:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
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
								a = LL14(L"巨竜目覚める", L"The Great Dragon Awakens", L"Le grand dragon s'eveille", L"Il grande drago si risveglia", L"El gran dragon despierta", L"?? ????", L"巨龍覺醒", L"?????? ?????? ??????", L"Великий дракон пробуждается", L"Der grose Drache erwacht", L"O grande dragao desperta", L"De grote draak ontwaakt", L"Wielki smok si? budzi", L"Buyuk Ejderha Uyan?yor");
								break;
							case 8715:
								a = LL14(L"未来へ。", L"To the Future.", L"Vers le futur.", L"Verso il futuro.", L"Hacia el futuro.", L"???.", L"往未來。", L"??? ????????.", L"В будущее.", L"In die Zukunft.", L"Para o futuro.", L"Naar de toekomst.", L"W przysz?o??.", L"Gelece?e.");
								break;
							case 8716:
								a = LL14(L"明日への軌跡 -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"???? ?? -Instrumental Ver.-", L"通向明天的軌跡 -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-", L"Trails to Tomorrow -Instrumental Ver.-");
								break;
							case 8717:
								a = "Deep Carnival";
								break;
							case 8718:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
								break;
							case 8719:
								a = "Chain Chain Chain!";
								break;
							case 8720:
								a = LL14(L"明日への軌跡", L"Trails to Tomorrow", L"Sillage vers demain", L"Tracce verso il domani", L"Estela hacia el manana", L"???? ??", L"通向明天的軌跡", L"?????? ??? ????", L"Пути в завтрашний день", L"Pfade nach morgen", L"Rastros para o amanha", L"Sporen naar morgen", L"?cie?ki do jutra", L"Yar?na Giden ?zler");
								break;
							case 8721:
								a = LL14(L"愛の詩(歌)", L"Poem of Love (vocal)", L"Poeme d'amour (vocal)", L"Poema d'amore (vocal)", L"Poema de amor (vocal)", L"??? ?(??)", L"愛之詩(歌)", L"????? ?? (?????)", L"Поэма о любви (вокал)", L"Liebesgedicht (Gesang)", L"Poema de amor (vocal)", L"Liefdesgedicht (vocaal)", L"Poemat mi?o?ci (wokal)", L"A?k ?iiri (vokal)");
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
								a = LL14(L"風よりも駿く", L"Swifter Than the Wind", L"Plus rapide que le vent", L"Piu veloce del vento", L"Mas rapido que el viento", L"???? ???", L"比風更迅捷", L"???? ?? ??????", L"Быстрее ветра", L"Schneller als der Wind", L"Mais rapido que o vento", L"Sneller dan de wind", L"Szybszy ni? wiatr", L"Ruzgardan Daha H?zl?");
								break;
							case 8803:
								a = "Brilliant Escape";
								break;
							case 8810: case 8811: case 8812: case 8910: case 8911: case 8912:
							case 8913: case 8916: case 8917: case 8918: case 8919: case 8920:
							case 8921:
								a = LL14(L"不明", L"Unknown", L"Inconnu", L"Sconosciuto", L"Desconocido", L"??", L"不明", L"??? ?????", L"Неизвестно", L"Unbekannt", L"Desconhecido", L"Onbekend", L"Nieznany", L"Bilinmeyen");
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
					ff.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
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
					ff.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
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
							CStdioFile f; if (f.Open(ft, CFile::modeRead | CFile::typeText | CFile::shareDenyWrite, NULL)) {
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
							CFile f; if (f.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL)) {
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
							f.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
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
							ff.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
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
							ff.Open(fname, CFile::modeRead | CFile::shareDenyWrite, NULL);
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
	CFile f;if(f.Open(s0,CFile::modeCreate|CFile::modeWrite | CFile::shareExclusive,NULL)==TRUE){
#else
	CFile f;if(f.Open(_T("playlist.dat"),CFile::modeCreate|CFile::modeWrite | CFile::shareExclusive,NULL)==TRUE){
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
	CFile f;if(f.Open(s,CFile::modeRead | CFile::shareDenyWrite,NULL)==TRUE){
#else
	CFile f;if(f.Open(_T("playlist.dat"),CFile::modeRead | CFile::shareDenyWrite,NULL)==TRUE){
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
	*pResult = 0;

	CPoint point;
	GetCursorPos(&point);

	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING | MF_ENABLED, ID_POP_32787,
		LL14(L"ファイル名変更", L"Rename File", L"Renommer le fichier", L"Rinomina file",
			L"Renombrar archivo", L"?? ?? ???", L"重命名文件", L"????? ????? ?????",
			L"Переименовать файл", L"Datei umbenennen", L"Renomear arquivo", L"Bestand hernoemen",
			L"Zmie? nazw? pliku", L"Dosyay? yeniden adland?r"));
	menu.AppendMenu(MF_STRING | MF_ENABLED, ID_POP_32776,
		LL14(L"ファイル詳細", L"File Details", L"Details du fichier", L"Dettagli file",
			L"Detalles del archivo", L"?? ?? ??", L"文件??信息", L"?????? ?????",
			L"Сведения о файле", L"Dateidetails", L"Detalhes do arquivo", L"Bestandsdetails",
			L"Szczego?y pliku", L"Dosya ayr?nt?lar?"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | MF_ENABLED, ID_POP_32777,
		LL14(L"削除", L"Delete", L"Supprimer", L"Elimina",
			L"Eliminar", L"??", L"?除", L"???",
			L"Удалить", L"Loschen", L"Excluir", L"Verwijderen",
			L"Usu?", L"Sil"));

	CWnd* pWndPopupOwner = this;
	while (pWndPopupOwner->GetStyle() & WS_CHILD)
		pWndPopupOwner = pWndPopupOwner->GetParent();

	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y,
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
				L"Lista de reproduccion: %d",
				L"??????: %d",
				L"播放列表：%d",
				L"????? ???????: %d",
				L"Плейлист: %d",
				L"Wiedergabeliste: %d",
				L"Lista de reproducao: %d",
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
		L"<Nueva lista de reproduccion>",
		L"<??? ??????>",
		L"<新建播放列表>",
		L"<????? ????? ?????>",
		L"<Новый плейлист>",
		L"<Neue Wiedergabeliste>",
		L"<Nova lista de reproducao>",
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
		L"?Eliminar la lista actual?",
		L"?? ??? ?????????",
		L"?定要?除当前列表?？",
		L"?? ???? ??? ??????? ????????",
		L"Удалить текущий список?",
		L"Aktuelle Liste loschen?",
		L"Excluir a lista atual?",
		L"Huidige lijst verwijderen?",
		L"Usun?? bie??c? list??",
		L"Mevcut liste silinsin mi?"),
		LL14(
			L"削除確認",
			L"Confirm Delete",
			L"Confirmer la suppression",
			L"Conferma eliminazione",
			L"Confirmar eliminacion",
			L"?? ??",
			L"???除",
			L"????? ?????",
			L"Подтверждение удаления",
			L"Loschung bestatigen",
			L"Confirmar exclusao",
			L"Verwijdering bevestigen",
			L"Potwierd? usuni?cie",
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

