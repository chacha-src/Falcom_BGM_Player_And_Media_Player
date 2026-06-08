// AudioSelect.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "AudioSelect.h"

extern CString streamname[40];
// CAudioSelect ダイアログ
extern IAMStreamSelect* iam;
extern int audionum;
extern int au;

IMPLEMENT_DYNAMIC(CAudioSelect, CCustomBlurDialogBase)

CAudioSelect::CAudioSelect(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogBase(CAudioSelect::IDD, pParent)
{

}

CAudioSelect::~CAudioSelect()
{
}

void CAudioSelect::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_lb);
	DDX_Control(pDX, IDOK, m_okdummy);
	DDX_Control(pDX, IDC_STATIC, m_desc);
}


BEGIN_MESSAGE_MAP(CAudioSelect, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, &CAudioSelect::OnLbnDblclkList1)
	ON_BN_CLICKED(IDOK, &CAudioSelect::OnBnClickedOk)
END_MESSAGE_MAP()


// CAudioSelect メッセージ ハンドラ

void CAudioSelect::OnLbnDblclkList1()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	select();
}

void CAudioSelect::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	select();
	OnOK();
}

void CAudioSelect::select()
{
	int cnt=m_lb.GetCurSel();
	no=cnt;
	EndDialog(cnt);
}

BOOL CAudioSelect::OnInitDialog()
{
	CCustomBlurDialogBase::OnInitDialog();

	SetWindowText(LL14(L"再生ストリーム選択", L"Select Playback Stream", L"Sélectionner le flux de lecture", L"Seleziona flusso di riproduzione", L"Seleccionar flujo de reproducción", L"재생 스트림 선택", L"选择播放流", L"حدد دفق التشغيل", L"Выбрать поток воспроизведения", L"Wiedergabestream auswählen", L"Selecionar fluxo de reprodução", L"Selecteer afspeelstroom", L"Wybierz strumień odtwarzania", L"Çalma akışı seç"));
	m_desc.SetWindowText(LL14(L"複数の音声チャンネルがある時に\nこの画面が表示されます。\n再生したい音声チャンネルを\n選択して下さい。\n\n再生ウィンドウでの右クリック\nメニューからも選択できます。", L"When there are multiple audio channels,\nthis screen will be displayed.\nPlease select the audio channel\nyou want to play.\n\nYou can also select from the\nright-click menu on the playback window.", L"Lorsqu'il y a plusieurs canaux audio,\ncet écran s'affichera.\nVeuillez sélectionner le canal audio\nque vous souhaitez lire.\n\nVous pouvez également sélectionner depuis\nle menu contextuel de la fenêtre de lecture.", L"Quando ci sono più canali audio,\nquesta schermata verrà visualizzata.\nSeleziona il canale audio\nche desideri riprodurre.\n\nPuoi anche selezionare dal menu\ncontesto della finestra di riproduzione.", L"Cuando hay varios canales de audio,\nse mostrará esta pantalla.\nSeleccione el canal de audio\nque desea reproducir.\n\nTambién puede seleccionar desde el\nmenú contextual de la ventana de reproducción.", L"여러 오디오 채널이 있을 때\n이 화면이 표시됩니다.\n재생하려는 오디오 채널을\n선택하세요.\n\n재생 창의 마우스 오른쪽 버튼\n메뉴에서도 선택할 수 있습니다.", L"当有多个音频声道时\n将显示此屏幕。\n请选择要播放的\n音频声道。\n\n您也可以从播放窗口的\n右键菜单中进行选择。", L"عند وجود قنوات صوتية متعددة،\nستظهر هذه الشاشة.\nالرجاء تحديد القناة الصوتية\nالتي تريد تشغيلها.\n\nيمكنك أيضًا الاختيار من\nقائمة النقر بزر الماوس الأيمن في نافذة التشغيل.", L"При наличии нескольких звуковых каналов\nбудет отображаться этот экран.\nВыберите звуковой канал,\nкоторый хотите воспроизвести.\n\nВы также можете выбрать из\nконтекстного меню окна воспроизведения.", L"Wenn mehrere Audiokanäle vorhanden sind,\nwird dieser Bildschirm angezeigt.\nBitte wählen Sie den Audiokanal,\nden Sie abspielen möchten.\n\nSie können auch aus dem Kontextmenü\nder Wiedergabefenster wählen.", L"Quando há vários canais de áudio,\nesta tela será exibida.\nSelecione o canal de áudio\nque deseja reproduzir.\n\nVocê também pode selecionar no\nmenu de contexto da janela de reprodução.", L"Wanneer er meerdere audiokanalen zijn,\nwordt dit scherm weergegeven.\nSelecteer het audiokanaal\ndat u wilt afspelen.\n\nU kunt ook selecteren via het\nrechtermuismenu van het afspeelvenster.", L"Gdy dostępnych jest kilka kanałów audio,\nzostanie wyświetlony ten ekran.\nWybierz kanał audio,\nktóry chcesz odtworzyć.\n\nMożesz także wybrać z menu\nkontekstowego okna odtwarzania.", L"Birden fazla ses kanalı olduğunda\nbu ekran görüntülenir.\nÇalmak istediğiniz ses kanalını\nseçin.\n\nAyrıca oynatma penceresindeki\nsağ tık menüsünden de seçebilirsiniz."));
	m_tooltip.Create(this);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(&m_okdummy, LL14(L"音声ストリームを決定します", L"Determine audio stream", L"Définir le flux audio", L"Determina flusso audio", L"Determinar flujo de audio", L"오디오 스트림 결정", L"确定音频流", L"تحديد دفق الصوت", L"Определить аудиопоток", L"Audiostream festlegen", L"Determinar fluxo de áudio", L"Audiostroom bepalen", L"Określ strumień audio", L"Ses akışını belirle"));
	m_tooltip.SetDelayTime( TTDT_AUTOPOP, 10000 );
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);

	for(int i=0;i<no;i++){
		CString str;
		str.Format(LL14(L"音声%d:%s", L"Audio %d:%s", L"Audio %d:%s", L"Audio %d:%s", L"Audio %d:%s", L"오디오 %d:%s", L"音频%d：%s", L"صوت %d:%s", L"Аудио %d:%s", L"Audio %d:%s", L"Áudio %d:%s", L"Audio %d:%s", L"Dźwięk %d:%s", L"Ses %d:%s"), i+1, streamname[i]);
		m_lb.AddString(str);
	}

	AM_MEDIA_TYPE* ppmt = NULL;
	DWORD* pdwFlags = NULL;
	for (int l = 0; l < audionum; l++) {
		int num = l + au;
		
		if (iam->Info(num, NULL, pdwFlags, NULL, NULL, NULL, NULL, NULL) == S_OK) {
			if (pdwFlags != nullptr && *pdwFlags == AMSTREAMSELECTINFO_ENABLED) {
				m_lb.SetCurSel(l);
			}
		}
	}


	return TRUE;  // return TRUE unless you set the focus to a control
	// 例外 : OCX プロパティ ページは必ず FALSE を返します。
}

BOOL CAudioSelect::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
		m_tooltip.RelayEvent(pMsg);

	return CCustomBlurDialogBase::PreTranslateMessage(pMsg);
}