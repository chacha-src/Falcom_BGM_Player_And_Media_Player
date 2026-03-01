// Render.cpp : インプリメンテーション ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "Render.h"
#include "Graph.h"
#include "dsound.h"
#include "ZeroFol.h"
#include "oggDlg.h"
#include "CImageBase.h"

extern IGraphBuilder *pGraphBuilder;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern save savedata;
CImageBase* renderbase;
/////////////////////////////////////////////////////////////////////////////
// CRender ダイアログ

IMPLEMENT_DYNAMIC(CRender, CCustomBlurDialogExBase)
CRender::CRender(CWnd* pParent /*=NULL*/)
	: CCustomBlurDialogExBase(CRender::IDD, pParent)
{
	//{{AFX_DATA_INIT(CRender)
		// メモ - ClassWizard はこの位置にマッピング用のマクロを追加または削除します。
	//}}AFX_DATA_INIT
}


void CRender::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CRender)
	DDX_Control(pDX, IDC_COMBO1, m_1);
	//}}AFX_DATA_MAP
	DDX_Control(pDX, IDC_CHECK1, m_evr);
	DDX_Control(pDX, IDC_CHECK2, m_con);
	DDX_Control(pDX, IDC_CHECK3, m_a);
	DDX_Control(pDX, IDC_CHECK27, m_ffd);
	DDX_Control(pDX, IDCANCEL2, m_l);
	DDX_Control(pDX, IDC_CHECK30, m_vob);
	DDX_Control(pDX, IDC_CHECK31, m_haali);
	DDX_Control(pDX, IDC_CHECK32, m_spc2x);
	DDX_Control(pDX, IDC_CHECK33, m_spc4x);
	DDX_Control(pDX, IDC_CHECK34, m_spc8x);
	DDX_Control(pDX, IDC_CHECK35, m_spc1x);
	DDX_Control(pDX, IDC_CHECK36, m_spc16x);
	DDX_Control(pDX, IDC_CHECK40, m_mp31);
	DDX_Control(pDX, IDC_CHECK37, m_mp315);
	DDX_Control(pDX, IDC_CHECK38, m_mp32);
	DDX_Control(pDX, IDC_CHECK39, m_mp325);
	DDX_Control(pDX, IDC_CHECK41, m_mp33);
	DDX_Control(pDX, IDC_CHECK45, m_kpi10);
	DDX_Control(pDX, IDC_CHECK42, m_kpi15);
	DDX_Control(pDX, IDC_CHECK43, m_kpi20);
	DDX_Control(pDX, IDC_CHECK44, m_kpi25);
	DDX_Control(pDX, IDC_CHECK46, m_kpi30);
	DDX_Control(pDX, IDCANCEL3, m_kpi);
	DDX_Control(pDX, IDC_CHECK47, m_mp3orig);
	DDX_Control(pDX, IDC_CHECK48, m_audiost);
	DDX_Control(pDX, IDC_CHECK49, m_24);
	DDX_Control(pDX, IDC_CHECK50, m_m4a);
	DDX_Control(pDX, IDC_CHECK51, m_32bit);
	DDX_Control(pDX, IDC_SLIDER3, m_ms);
	DDX_Control(pDX, IDC_STATIC9, m_ms2);
	DDX_Control(pDX, IDC_SLIDER5, m_hyouji2);
	DDX_Control(pDX, IDC_STATIC10, m_hyouji3);
	DDX_Control(pDX, IDC_COMBO2, m_soundlist);
	DDX_Control(pDX, IDC_BUTTON1, m_ao);
	DDX_Control(pDX, IDC_COMBO3, m_Hz);
	DDX_Control(pDX, IDC_STATIC12, m_wup);
	DDX_Control(pDX, IDC_SLIDER6, w_wups);
	DDX_Control(pDX, IDC_CHECK52, m_speana);
	DDX_Control(pDX, IDC_COMBO4, m_speana_num);
	DDX_Control(pDX, IDC_CHECK_lrc, m_netlrc);
	DDX_Control(pDX, IDOK, m_okdummy);
	DDX_Control(pDX, IDCANCEL5, m_kanren);
	DDX_Control(pDX, IDCANCEL, m_canceldummy);
	DDX_Control(pDX, IDC_COMBO_LANG, m_comboLang);
}


BEGIN_MESSAGE_MAP(CRender, CCustomBlurDialogExBase)
	//{{AFX_MSG_MAP(CRender)
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDCANCEL2, &CRender::OnBnClickedCancel2)
	ON_BN_CLICKED(IDC_CHECK32, &CRender::Onspc2x)
	ON_BN_CLICKED(IDC_CHECK33, &CRender::Onspc4x)
	ON_BN_CLICKED(IDC_CHECK34, &CRender::Onspc8x)
	ON_BN_CLICKED(IDC_CHECK35, &CRender::Onspc1x)
	ON_BN_CLICKED(IDC_CHECK36, &CRender::Onspc16x)
	ON_BN_CLICKED(IDC_CHECK40, &CRender::Onmp31)
	ON_BN_CLICKED(IDC_CHECK37, &CRender::Onmp315)
	ON_BN_CLICKED(IDC_CHECK38, &CRender::Onmp32)
	ON_BN_CLICKED(IDC_CHECK39, &CRender::Onmp325)
	ON_BN_CLICKED(IDC_CHECK41, &CRender::Onmp33)
	ON_BN_CLICKED(IDC_CHECK45, &CRender::Onkpi10)
	ON_BN_CLICKED(IDC_CHECK42, &CRender::Onkpi15)
	ON_BN_CLICKED(IDC_CHECK43, &CRender::Onkpi20)
	ON_BN_CLICKED(IDC_CHECK44, &CRender::Onkpi25)
	ON_BN_CLICKED(IDC_CHECK46, &CRender::Onkpi30)
	ON_BN_CLICKED(IDCANCEL3, &CRender::Onkpi)
	ON_BN_CLICKED(IDC_FONT, &CRender::OnFontMain)
	ON_BN_CLICKED(IDC_FONT2, &CRender::OnFontList)
	ON_BN_CLICKED(IDOK, &CRender::OnBnClickedOk)
	ON_BN_CLICKED(IDC_CHECK49, &CRender::OnBnClicked24bit)
	ON_BN_CLICKED(IDC_CHECK50, &CRender::OnBnClickedCheck50)
	ON_BN_CLICKED(IDCANCEL4, &CRender::OnBnClickedCancel4)
	ON_WM_TIMER()
	ON_CBN_SELCHANGE(IDC_COMBO2, &CRender::OnCbnSelchangeCombo2)
	ON_BN_CLICKED(IDC_BUTTON1, &CRender::OnBnClickedButton1)
	ON_CBN_SELCHANGE(IDC_COMBO3, &CRender::OnCbnSelchangeCombo3)
	ON_WM_CTLCOLOR()
	ON_WM_CREATE()
	ON_WM_MOVING()
	ON_BN_CLICKED(IDCANCEL, &CRender::OnBnClickedCancel)
	ON_BN_CLICKED(IDC_CHECK52, &CRender::OnBnClickedCheck52)
	ON_CBN_EDITCHANGE(IDC_COMBO4, &CRender::OnCbnEditchangeCombo4)
	ON_CBN_SELCHANGE(IDC_COMBO4, &CRender::OnCbnSelchangeCombo4)
	ON_BN_CLICKED(IDCANCEL5, &CRender::OnBnClickedCancel5)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRender メッセージ ハンドラ
CComboBox *sl;
GUID slg[200];
int slgc;
CString sls[200];
DWORD samp[] = { 11025, 12000, 22050, 24000, 44100, 48000, 96000, 192000, 384000, 768000, 1536000, 3072000 };
extern COggDlg* og;
#include "OSVersion.h"

BOOL CRender::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();

	SetWindowText(LL2(L"レンダリング選択", L"Rendering Options"));
	SetDlgItemText(IDOK, LL2(L"OK", L"OK"));
	SetDlgItemText(IDCANCEL, LL2(L"キャンセル", L"Cancel"));
	SetDlgItemText(IDCANCEL2, LL2(L"DirectShowフィルタ一覧", L"DirectShow Filter List"));
	SetDlgItemText(IDCANCEL3, LL2(L"kpi一覧", L"kpi List"));
	SetDlgItemText(IDCANCEL5, LL2(L"関連付け", L"File Association"));
	SetDlgItemText(IDC_CHECK1, LL2(L"デフォルトでEVR使用(Vista以降)", L"Default EVR use (Vista+)"));
	SetDlgItemText(IDC_CHECK2, LL2(L"デスクトップコンポジションを使用する", L"Use desktop composition"));
	SetDlgItemText(IDC_CHECK3, LL2(L"AeroやGrassを使用する", L"Use Aero and Grass"));
	SetDlgItemText(IDC_CHECK27, LL2(L"ffdshow使用", L"Use ffdshow"));
	SetDlgItemText(IDC_CHECK30, LL2(L"vobやdatはHaaliスキップ対応とする", L"vob/dat skip Haali by default"));
	SetDlgItemText(IDC_CHECK31, LL2(L"Haaliを使用しない", L"Do not use Haali"));
	SetDlgItemText(IDC_CHECK47, LL2(L"mp3 オリジナルデコーダを使う", L"Use original mp3 decoder"));
	SetDlgItemText(IDC_CHECK48, LL2(L"複数音声の動画の時は音声選択画面を出す", L"Show audio selection for multi-audio video"));
	SetDlgItemText(IDC_CHECK49, LL2(L"24bit使用", L"Use 24bit"));
	SetDlgItemText(IDC_CHECK50, LL2(L"m4aを内蔵エンジンで演奏する", L"Play m4a with built-in engine"));
	SetDlgItemText(IDC_CHECK51, LL2(L"32bit使用", L"Use 32bit"));
	SetDlgItemText(IDC_CHECK52, LL2(L"スペアナのモード切り替える", L"Switch spectrum analyzer mode"));
	SetDlgItemText(IDC_CHECK_lrc, LL2(L"lrcをネットから取得する(LRCLib/NetEase等による)", L"Fetch lrc from network (LRCLib/NetEase etc.)"));
	SetDlgItemText(IDC_BUTTON1, LL2(L"碧の軌跡用t_bgm._dt", L"t_bgm._dt for Ao no Kiseki"));
	SetDlgItemText(IDC_FONT, LL2(L"メイン用フォント", L"Main font"));
	SetDlgItemText(IDC_FONT2, LL2(L"リスト用フォント", L"List font"));
	SetDlgItemText(IDC_STATIC_LANG, LL2(L"言語", L"Language"));
	SetDlgItemText(IDC_STATIC_R_BUF, LL2(L"割込間隔", L"Buffer interval"));
	SetDlgItemText(IDC_STATIC_R_MP3, LL2(L"mp3音量", L"mp3 volume"));
	SetDlgItemText(IDC_STATIC_R_KPI, LL2(L"その他のkpi", L"Other kpi"));
	SetDlgItemText(IDC_STATIC_R_DISP, LL2(L"表示間隔", L"Display interval"));
	SetDlgItemText(IDC_STATIC_R_DEV, LL2(L"再生デバイス", L"Playback device"));
	SetDlgItemText(IDC_STATIC_R_SAMP, LL2(L"MAXサンプルレート：", L"MAX sample rate:"));
	SetDlgItemText(IDC_STATIC_R_SPEANA, LL2(L"スペアナ倍率", L"Spectrum scale"));
	SetDlgItemText(IDC_STATIC_R_SPC, LL2(L".SPC,.HES音量(kpi)", L".SPC,.HES volume(kpi)"));
	SetDlgItemText(IDC_STATIC_R_BIT, LL2(L"演奏bit深度：", L"Playback bits:"));
	SetDlgItemText(IDC_STATIC12, LL2(L"倍", L"x"));
	m_comboLang.AddString(LL2(L"日本語", L"Japanese"));
	m_comboLang.AddString(L"English");
	m_comboLang.SetCurSel(savedata.lang);
	OSVERSIONINFO in; ZeroMemory(&in, sizeof(in)); in.dwOSVersionInfoSize = sizeof(OSVERSIONINFO); GetVersionEx(&in);
	if (in.dwMajorVersion <= 5)
		m_1.AddString(LL2(L"デフォルト", L"Default"));
	else
		m_1.AddString(LL2(L"デフォルト(普通/EVR)", L"Default (normal/EVR)"));
	m_1.AddString(L"VMR7");
	m_1.AddString(L"VMR9");
	m_1.SetCurSel(savedata.render);
	switch (savedata.spc) {
	case 1:m_spc1x.SetCheck(TRUE); break;
	case 2:m_spc2x.SetCheck(TRUE); break;
	case 4:m_spc4x.SetCheck(TRUE); break;
	case 8:m_spc8x.SetCheck(TRUE); break;
	case 16:m_spc16x.SetCheck(TRUE); break;
	}
	switch (savedata.mp3) {
	case 1:m_mp31.SetCheck(TRUE); break;
	case 2:m_mp315.SetCheck(TRUE); break;
	case 3:m_mp32.SetCheck(TRUE); break;
	case 4:m_mp325.SetCheck(TRUE); break;
	case 5:m_mp33.SetCheck(TRUE); break;
	}
	switch (savedata.kpivol) {
	case 1:m_kpi10.SetCheck(TRUE); break;
	case 2:m_kpi15.SetCheck(TRUE); break;
	case 3:m_kpi20.SetCheck(TRUE); break;
	case 4:m_kpi25.SetCheck(TRUE); break;
	case 5:m_kpi30.SetCheck(TRUE); break;
	}
	if (in.dwMajorVersion <= 5) {
		m_evr.EnableWindow(FALSE);
		m_con.EnableWindow(FALSE);
		m_a.EnableWindow(FALSE);
	}
	m_mp3orig.SetCheck(savedata.mp3orig);
	m_audiost.SetCheck(savedata.audiost);
	m_24.SetCheck(savedata.bit24);
	m_32bit.SetCheck(savedata.bit32);
	m_m4a.SetCheck(savedata.m4a);

	m_tooltip.Create(this);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), LL2(L"保存して閉じます", L"Save and close"));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL), LL2(L"保存せずに閉じます", L"Close without saving"));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL2), LL2(L"再生中の使用DirectShowフィルタを表示します。", L"Show DirectShow filters in use during playback."));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL3), LL2(L"kpi一覧を表示します。", L"Show kpi list."));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL4), LL2(L"各種ファイルを簡易プレイヤに関連づけします。\nうまくいかない場合もあります。", L"Associate files with simple player.\nMay not always work."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK1), LL2(L"Windows Vista/7以降で有効です。\nIndeoを用いた動画の場合OFFにしてください。\nそれ以外はONでいいです。", L"Effective on Windows Vista/7+.\nTurn OFF for Indeo video.\nOtherwise ON."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK2), LL2(L"Windows Vista/7以降で有効です。\nデスクトップコンポジション(Aero)を使用するかどうかを選択します。\n使用しないにするとEVRじゃなくても動画画面はきれいになります。", L"Effective on Vista/7+.\nUse desktop composition (Aero).\nWithout it, video may still look good without EVR."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK3), LL2(L"Windows 10以降で有効です。\nAero Grassを使用するかどうか決めます。", L"Effective on Windows 10+.\nEnable Aero Grass."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK27), LL2(L"動画にffdshowを使うかどうか選択します。\nWindows7の場合、デフォルトでDivxなどを再生できるのでそちらを使いたい人はOFFにしてください。", L"Use ffdshow for video.\nOn Win7, DivX works by default; turn OFF if you prefer that."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK30), LL2(L"vobとdatファイルはHaaliを通さないように作られていますが、\nvobに複数音声があるときにはチェックを入れて下さい。", L"vob/dat skip Haali by default.\nCheck when vob has multiple audio tracks."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK31), LL2(L"動画にHaaliを使いません。\n動画が重いと思った時や複数音声が無い時はチェックを入れると軽くなります。", L"Do not use Haali for video.\nCheck if video is heavy or has no multiple audio."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK32), LL2(L"kpi SPC/NEZplug++等のSPCの音量を2倍にします。", L"2x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK33), LL2(L"kpi SPC/NEZplug++等のSPCの音量を3倍にします。", L"3x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK34), LL2(L"kpi SPC/NEZplug++等のSPCの音量を4倍にします。", L"4x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK35), LL2(L"kpi SPC/NEZplug++等のSPCの音量を等倍にします。", L"1x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK36), LL2(L"kpi SPC/NEZplug++等のSPCの音量を5倍にします。", L"5x volume for kpi SPC/NEZplug++ SPC."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK40), LL2(L"mp3の音量を等倍にします。", L"1x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK37), LL2(L"mp3の音量を1.5倍にします。", L"1.5x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK38), LL2(L"mp3の音量を2倍にします。", L"2x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK39), LL2(L"mp3の音量を2.5倍にします。", L"2.5x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK41), LL2(L"mp3の音量を3倍にします。", L"3x mp3 volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK45), LL2(L"kpiの音量を等倍にします。", L"1x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK42), LL2(L"kpiの音量を2倍にします。", L"2x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK43), LL2(L"kpiの音量を3倍にします。", L"3x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK44), LL2(L"kpiの音量を4倍にします。", L"4x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK46), LL2(L"kpiの音量を5倍にします。", L"5x kpi volume."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK47), LL2(L"mp3のデコーダをオリジナルのデコーダを使わずに、独自で使ったデコーダを使う。\nエラーなどで演奏できないときにチェック入れて下さい。\nまた独自で正常にならない時ははずして下さい。", L"Use custom mp3 decoder instead of original.\nCheck if playback fails.\nUncheck if custom causes issues."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK48), LL2(L"複数音声のある動画を再生する時に、再生前に\n音声ストリームの選択画面を表示します。\n通常ストリーム1がメインとして使われ、ストリーム2以降はコメンタリや英語音声などに使われています。", L"Show audio stream selection before playing multi-audio video.\nStream 1 is usually main; 2+ for commentary/English etc."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK49), LL2(L"対応しているkpiを24bit(ハイレゾ)で再生します。\n通常は16bitですが、まれに対応しているものがあります。\n音割れについては考慮されていないため、spcなど倍率を上げないといけないものは気をつけて下さい。", L"Play supported kpi at 24bit.\nUsually 16bit; some support 24bit.\nClipping not considered for spc etc."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK51), LL2(L"対応しているkpiを32bit(ハイレゾ)で再生します。\n通常は16bitですが、まれに対応しているものがあります。\n音割れについては考慮されていないため、spcなど倍率を上げないといけないものは気をつけて下さい。", L"Play supported kpi at 32bit.\nUsually 16bit; some support 32bit.\nClipping not considered for spc etc."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK50), LL2(L"m4aを内蔵エンジンで演奏します。", L"Play m4a with built-in engine."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK52), LL2(L"スペアナの表示モードを切り替えます", L"Switch spectrum analyzer display mode"));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO3), LL2(L"再生するサンプルレートを設定します。\nサウンドカードが対応していない場合自動的に再生時対応上限まで下げます。", L"Set playback sample rate.\nAuto-lowers if sound card unsupported."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO4), LL2(L"スペアナで表示する表示方法を選択します。\n使う時は横のチェックボックスにチェックを入れてください\n音階：88鍵盤として表示します\n周波数帯：周波数として表示します\n標準：既定の見やすい形のスペアナで表示します", L"Select spectrum display.\nCheck the box to use.\nScale: 88-key piano\nFreq band: frequency view\nStandard: default spectrum"));
	m_tooltip.AddTool(GetDlgItem(IDC_BUTTON1), LL2(L"碧の軌跡用のt_bgm._dtを設定します。", L"Set t_bgm._dt for Ao no Kiseki."));
	m_tooltip.AddTool(GetDlgItem(IDCANCEL5), LL2(L"win7くらいまで対応。関連付けに追加します。\nwin10以降でも追加はされるとは思いますがされないときもあります。", L"Up to Win7. Add file associations.\nMay work on Win10+ but not always."));
	m_tooltip.AddTool(GetDlgItem(IDC_CHECK_lrc), LL2(L"歌詞情報をネットから参照するようにします。\n数パターン試すため少し再生までに時間かかります。", L"Fetch lyrics from network.\nMay take longer to start playback."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER3), LL2(L"演奏のバッファ処理での割り込み時間を設定します。\n少なすぎると音飛びする可能性があります。", L"Set buffer interrupt time.\nToo low may cause audio glitches."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER5), LL2(L"描画の間隔時間を設定します。\nCPU使用が高いときに上げます。", L"Set render interval.\nIncrease when CPU usage is high."));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER6), LL2(L"スペアナの表示倍率を設定します。", L"Set spectrum display scale."));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO_LANG), LL2(L"UI表示言語を切り替えます。\n設定を保存して再起動後に反映されます。", L"Switch UI language.\nTakes effect after saving and restarting."));
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);

	m_ms.SetMode(1);
	m_hyouji2.SetMode(1);
	w_wups.SetMode(1);
	m_evr.SetCheck(savedata.evr);
	m_con.SetCheck(savedata.con);
	m_a.SetCheck(savedata.aero);
	COSVersion os;
	os.GetVersionString();
	if (os.in.dwMajorVersion < 6)
		m_a.ShowWindow(SW_HIDE);

	m_ffd.SetCheck(savedata.ffd);
	m_vob.SetCheck(savedata.vob);
	m_haali.SetCheck(savedata.haali);
	m_speana.SetCheck(savedata.speanamode);
	extern CPlayList* pl;
	extern COggDlg* og;
	extern int ip;
	ip = 0;
	og->KillTimer(4923);
	og->KillTimer(4924);
	if (pl) {
		pl->KillTimer(4923);
		pl->KillTimer(4924);
	}
#if WIN64
	m_kpi.EnableWindow(FALSE);
#else
#endif
	m_ms.SetRange(30, 80);
	if (savedata.ms < 30) savedata.ms = 30;
	m_ms.SetPos(savedata.ms);
	if (savedata.ms > 80) savedata.ms = 80;
	m_hyouji2.SetRange(1, 60);
	m_hyouji2.SetPos(savedata.ms2);
	CString s; s.Format(L"%dms", savedata.ms);
	m_ms2.SetWindowText(s);
	SetTimer(11, 100, NULL);
	w_wups.SetRange(100, 1000);
	w_wups.SetPos((int)savedata.wup);

	//sl = &m_soundlist;
	slgc = 0;
	m_soundlist.Clear();
	DirectSoundEnumerate(DSEnumCallback, NULL);
	for (int k = 0; k < slgc; k++) {
		m_soundlist.AddString(sls[k]);
	}
	m_soundlist.SetCurSel(savedata.soundcur);
	if (!pGraphBuilder)
		m_l.EnableWindow(FALSE);
	CString abc = savedata.zero;
	if (abc == L"") {
		m_ao.ShowWindow(FALSE);
	}
	// { 11025, 12000, 22050, 24000, 44100, 48000, 96000, 192000, 384000, 768000, 1536000, 3072000 };
	m_Hz.AddString(LL2(L"--低周波数帯- イコライザーでアップスケール対応し処理される", L"--Low freq- EQ upscale processed"),TRUE);
	m_Hz.AddString(L"11025");
	m_Hz.AddString(L"12000");
	m_Hz.AddString(L"22050");
	m_Hz.AddString(L"24000");
	m_Hz.AddString(LL2(L"--通常波数帯- イコライザー通常処理される", L"--Normal freq- EQ normal processed"), TRUE);
	m_Hz.AddString(L"44100");
	m_Hz.AddString(L"48000");
	m_Hz.AddString(L"96000");
	m_Hz.AddString(L"192000");
	m_Hz.AddString(LL2(L"--高周波数帯- イコライザー処理されない場合がある", L"--High freq- EQ may not process"), TRUE);
	m_Hz.AddString(L"384000");
	m_Hz.AddString(L"768000");
	m_Hz.AddString(L"1536000");
	m_Hz.AddString(L"3072000 ");
	for (int l = 0; l < 12; l++) {
		if (savedata.samples == samp[l]) {
			m_Hz.SetCurSel(l);
			break;
		}
	}

	m_speana_num.AddString(LL2(L"音階", L"Scale"));
	m_speana_num.AddString(LL2(L"低周波帯特化", L"Low freq focus"));
	m_speana_num.AddString(LL2(L"標準", L"Standard"));
	m_speana_num.AddString(LL2(L"高周波帯", L"High freq"));
	m_speana_num.AddString(LL2(L"音声特化", L"Voice focus"));
	m_speana_num.SetCurSel(savedata.speananum);


	m_netlrc.SetCheck(savedata.lrc_net);

	if (savedata.aero){
		renderbase = new CImageBase;
	renderbase->Create(NULL);
	renderbase->oya = this;
	}
	else {
		renderbase = NULL;
	}
	CRect r;
	GetWindowRect(&r);
	if (savedata.aero)
	renderbase->MoveWindow(&r);
	if(renderbase)
		::SetWindowPos(renderbase->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	return TRUE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}

BOOL CALLBACK CRender::DSEnumCallback(LPGUID pGUID, LPCWSTR strDesc,LPCWSTR strDrvName, LPVOID pContext)
{
	if (pGUID)
	{
		sls[slgc] = strDesc;
		CopyMemory(&slg[slgc], pGUID, sizeof(GUID));
		slgc++;
	}

	return TRUE;
}

void CRender::OnOK() 
{
	// TODO: この位置にその他の検証用のコードを追加してください
	savedata.render=m_1.GetCurSel();
	savedata.evr=m_evr.GetCheck();
	savedata.con=m_con.GetCheck();
	savedata.aero=m_a.GetCheck();
	savedata.ffd=m_ffd.GetCheck();
	savedata.vob=m_vob.GetCheck();
	savedata.haali=m_haali.GetCheck();
	savedata.audiost=m_audiost.GetCheck();
	savedata.bit24 = m_24.GetCheck();
	savedata.bit32 = m_32bit.GetCheck();
	savedata.m4a = m_m4a.GetCheck();
	savedata.ms = m_ms.GetPos();
	savedata.samples = samp[m_Hz.GetCurSel()];
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
	savedata.lang = m_comboLang.GetCurSel();

	//	savedata.mp3orig=m_mp3orig.GetCheck();
	if (savedata.aero)
	delete renderbase;
	CCustomBlurDialogExBase::OnOK();
}

INT_PTR CRender::OnToolHitTest(CPoint point, TOOLINFO* pTI) const
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。

	return CCustomBlurDialogExBase::OnToolHitTest(point, pTI);
}

BOOL CRender::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基本クラスを呼び出してください。
		m_tooltip.RelayEvent(pMsg);

	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CRender::OnBnClickedCancel2()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	CGraph *a = new CGraph(CWnd::FromHandle(GetSafeHwnd()));
	::SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	if (renderbase)::SetWindowPos(renderbase->m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	if(pGraphBuilder)
		a->DoModal();
	delete a;
}

void CRender::Onspc2x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(TRUE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=2;
}

void CRender::Onspc4x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(TRUE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=4;
}

void CRender::Onspc8x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(TRUE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=8;
}

void CRender::Onspc1x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(FALSE);
	m_spc1x.SetCheck(TRUE);
	savedata.spc=1;
}

void CRender::Onspc16x()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_spc2x.SetCheck(FALSE);
	m_spc4x.SetCheck(FALSE);
	m_spc8x.SetCheck(FALSE);
	m_spc16x.SetCheck(TRUE);
	m_spc1x.SetCheck(FALSE);
	savedata.spc=16;
}

void CRender::Onmp31()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(TRUE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=1;
}

void CRender::Onmp315()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(TRUE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=2;
}

void CRender::Onmp32()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(TRUE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=3;
}

void CRender::Onmp325()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(TRUE);
	m_mp33.SetCheck(FALSE);
	savedata.mp3=4;
}

void CRender::Onmp33()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_mp31.SetCheck(FALSE);
	m_mp315.SetCheck(FALSE);
	m_mp32.SetCheck(FALSE);
	m_mp325.SetCheck(FALSE);
	m_mp33.SetCheck(TRUE);
	savedata.mp3=5;
}

void CRender::Onkpi10()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(TRUE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=1;
}

void CRender::Onkpi15()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(TRUE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=2;
}

void CRender::Onkpi20()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(TRUE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=3;
}

void CRender::Onkpi25()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(TRUE);
	m_kpi30.SetCheck(FALSE);
	savedata.kpivol=4;
}

void CRender::Onkpi30()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	m_kpi10.SetCheck(FALSE);
	m_kpi15.SetCheck(FALSE);
	m_kpi20.SetCheck(FALSE);
	m_kpi25.SetCheck(FALSE);
	m_kpi30.SetCheck(TRUE);
	savedata.kpivol=5;
}

void CRender::OnBnClicked24bit()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.bit24 = m_24.GetCheck();
}

void CRender::OnBnClickedCheck50()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.m4a = m_m4a.GetCheck();
}



#include "Kpilist.h"
void CRender::Onkpi()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	SetTimer(7000, 300, NULL);
}

extern HFONT	hFont;
#include "afxdlgs.h"
void CRender::OnFontMain()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	LOGFONT      logFont;
	CFont* f=CFont::FromHandle(hFont);
	f->GetLogFont(&logFont);
	CFontDialog fontDlg(&logFont);
	if (fontDlg.DoModal() == IDOK){
		DeleteObject(hFont);
		hFont=CreateFontIndirect(fontDlg.m_cf.lpLogFont);
	}

}
#include "PlayList.h"
extern CPlayList *pl;
void CRender::OnFontList()
{
	// TODO: ここにコントロール通知ハンドラ コードを追加します。
	LOGFONT      logFont;
	CFont* f=pl->m_lc.GetFont();
	f->GetLogFont(&logFont);
	CFontDialog fontDlg(&logFont,CF_SCREENFONTS);
	if (fontDlg.DoModal() == IDOK && pl){
		pl->m_lc.SetFont(fontDlg.GetFont());
	}
}


void CRender::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.render = m_1.GetCurSel();
	savedata.evr = m_evr.GetCheck();
	savedata.con = m_con.GetCheck();
	savedata.aero = m_a.GetCheck();
	savedata.ffd = m_ffd.GetCheck();
	savedata.vob = m_vob.GetCheck();
	savedata.haali = m_haali.GetCheck();
	savedata.audiost = m_audiost.GetCheck();
	savedata.bit24 = m_24.GetCheck();
	savedata.bit32 = m_32bit.GetCheck();
	savedata.m4a = m_m4a.GetCheck();
	savedata.lrc_net = m_netlrc.GetCheck();
	savedata.ms = m_ms.GetPos();
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
	savedata.lang = m_comboLang.GetCurSel();
	extern int gameon;
	if(savedata.aero)
	delete renderbase;
	CCustomBlurDialogExBase::OnOK();
}




BOOL CRender::MySetFileType(LPCTSTR lpExt, LPCTSTR lpDocName, LPCTSTR lpDocType, LPCTSTR lpPath, LPCTSTR lpPath1)
{
	CRegKey reg;

	// lpExtをlpDocNameに関連付ける
	if (reg.SetValue(HKEY_CLASSES_ROOT, lpExt, lpDocName) != ERROR_SUCCESS)
		return FALSE;
	// lpDocName作成
	CString strDocNameTmp = lpDocName;
	CString strIcon = lpPath1; strIcon += ",0";
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp, lpDocType) != ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp + _T("\\shell"), _T("open"))
		!= ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp + _T("\\shell\\open\\command"),
		lpPath) != ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_CLASSES_ROOT, strDocNameTmp + _T("\\DefaultIcon"),
		strIcon) != ERROR_SUCCESS)
		return FALSE;

	if (reg.SetValue(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\RegisteredApplications"),
		_T("SOFTWARE\\PrePrayerPowerSoft\\oggYSEDbgm_uni\\Capabilities"),_T("oggYSEDbgm_uni")) != ERROR_SUCCESS)
		return FALSE;
	if (reg.Create(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\PrePrayerPowerSoft\\oggYSEDbgm_uni\\Capabilities")) != ERROR_SUCCESS)
		return FALSE;
	if (reg.SetValue(HKEY_LOCAL_MACHINE, _T("SOFTWARE\\PrePrayerPowerSoft\\oggYSEDbgm_uni\\Capabilities"),
		strDocNameTmp, lpExt) != ERROR_SUCCESS)
		return FALSE;
	strIcon = _T("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\");
	strIcon += lpExt;
	strIcon += _T("\\UserChoice");
	reg.Open(HKEY_CURRENT_USER, _T(""));
	if(reg.DeleteSubKey(strIcon) != ERROR_SUCCESS)
		return FALSE;
	reg.Close();
	if (reg.SetValue(HKEY_CURRENT_USER, strIcon,
		lpDocName, _T("Progid")) != ERROR_SUCCESS)
		return FALSE;

	// 関連付けが変更された事をシステムに通知
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	return TRUE;
}

extern TCHAR karento2[1024];

void CRender::OnBnClickedCancel4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CString s,ss;
	s = "\"";
	s += karento2;
	s += "oggYSEDbgm_uni.exe\" \"%1\"";
	ss = karento2;
	ss += "oggYSEDbgm_uni.exe";
	MySetFileType(_T(".mp3"), _T("oggYSEDbgm_uni.exe.mp3"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mp2"), _T("oggYSEDbgm_uni.exe.mp2"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mp1"), _T("oggYSEDbgm_uni.exe.mp1"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".rmp"), _T("oggYSEDbgm_uni.exe.rmp"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".flac"), _T("oggYSEDbgm_uni.exe.flac"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".m4a"), _T("oggYSEDbgm_uni.exe.m4a"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".aac"), _T("oggYSEDbgm_uni.exe.aac"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".avi"), _T("oggYSEDbgm_uni.exe.avi"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mp4"), _T("oggYSEDbgm_uni.exe.mp4"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mkv"), _T("oggYSEDbgm_uni.exe.mkv"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".wmv"), _T("oggYSEDbgm_uni.exe.wmv"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	MySetFileType(_T(".mpg"), _T("oggYSEDbgm_uni.exe.mpg"), LL2(L"簡易プレイヤで開く", L"Open with Simple Player"), s, ss);
	// 関連付けが変更された事をシステムに通知
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	MessageBox(LL2(L"一応関連づけを走らせてみました。\nmp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpgに関連をつけました。", L"File association attempted.\nAssociated mp1,2,3,rmp,flac,m4a,aac,avi,mp4,mkv,wmv,mpg."));
	// 関連付けが変更された事をシステムに通知
	::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}


void CRender::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	if (nIDEvent == 7000) {
		KillTimer(7000);
		CKpilist k;
		k.status = 0;
		k.DoModal();
		return;
	}
	savedata.ms = m_ms.GetPos();
	CString s; s.Format(L"%dms", savedata.ms);
	m_ms2.SetWindowText(s);
	savedata.ms2 = m_hyouji2.GetPos();
	s.Format(L"%dms", savedata.ms2*16);
	m_hyouji3.SetWindowText(s);
	savedata.wup = w_wups.GetPos()/ 100.0;
	s.Format(savedata.lang ? L"%1.2lfx" : L"%1.2lf倍", savedata.wup);
	m_wup.SetWindowText(s);
	if (nIDEvent == 90) {
		KillTimer(90);
//		::SetWindowPos(renderbase->m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		::SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	}
	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

extern int wavbit, wavch, wavsam, wavbit2, fade1;
#include <MMSystem.h>
#include "dsound.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include "libmad\decoder.h"
#include "mp3info.h"
#include "mp3.h"
extern void DoEvent();
extern int sek;
extern int			logtbl[100 + 1];
extern LPDIRECTSOUND8 m_ds;
extern LPDIRECTSOUNDBUFFER m_dsb1;
extern LPDIRECTSOUNDBUFFER8 m_dsb;
extern CString tagfile,fnn;
void CRender::OnCbnSelchangeCombo2()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	memcpy(&savedata.soundguid, &slg[m_soundlist.GetCurSel()], sizeof(GUID));
	if (m_soundlist.GetCurSel() == 0) {
		savedata.soundguid = { 0,0,0,0 };
	}
	savedata.soundcur= m_soundlist.GetCurSel();
	og->ReleaseDXSound();
	og->init(og->m_hWnd, wavbit);
	sek = 1;
	WAVEFORMATEX wfx1;
	if (wavsam<0)
		wfx1.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	else
		wfx1.wFormatTag = WAVE_FORMAT_PCM;
	wfx1.nChannels = wavch;
	wfx1.nSamplesPerSec = wavbit;
	wfx1.wBitsPerSample = abs(wavsam);
	wfx1.nBlockAlign = wfx1.nChannels * wfx1.wBitsPerSample / 8;
	wfx1.nAvgBytesPerSec = wfx1.nSamplesPerSec * wfx1.nBlockAlign;
	wfx1.cbSize = 0;

	static const GUID GUID_SUBTYPE_PCM = { 0x00000001, 0x0000, 0x0010,{ 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

	DWORD targetSpeakers = 0;
	switch (wavch) {
	case 1:
		targetSpeakers |= SPEAKER_FRONT_CENTER;
		break;
	case 2:
		targetSpeakers |=
			SPEAKER_FRONT_LEFT
			| SPEAKER_FRONT_RIGHT;
		break;
	case 3:
		targetSpeakers |=
			SPEAKER_FRONT_LEFT
			| SPEAKER_FRONT_RIGHT
			| SPEAKER_FRONT_CENTER
			;
	case 4:
		targetSpeakers |=
			SPEAKER_FRONT_LEFT
			| SPEAKER_FRONT_RIGHT
			| SPEAKER_FRONT_CENTER
			| SPEAKER_BACK_CENTER
			;
	case 5:
		targetSpeakers |=
			SPEAKER_FRONT_LEFT
			| SPEAKER_FRONT_RIGHT
			| SPEAKER_FRONT_CENTER
			| SPEAKER_BACK_LEFT
			| SPEAKER_BACK_RIGHT
			;
	case 6:
		targetSpeakers |=
			SPEAKER_FRONT_LEFT
			| SPEAKER_FRONT_RIGHT
			| SPEAKER_FRONT_CENTER
			| SPEAKER_LOW_FREQUENCY
			| SPEAKER_BACK_LEFT
			| SPEAKER_BACK_RIGHT
			;
	}
	int nChannels = __popcnt(targetSpeakers);
	WAVEFORMATEXTENSIBLE wfx = {};
	wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	wfx.Format.nChannels = nChannels;
	wfx.Format.nSamplesPerSec = wavbit;
	wfx.Format.wBitsPerSample = abs(wavsam);
	wfx.Format.nBlockAlign = (WORD)(wfx.Format.wBitsPerSample / 8 * wfx.Format.nChannels);
	wfx.Format.nAvgBytesPerSec = (DWORD)(wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign);
	wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	wfx.dwChannelMask = targetSpeakers;
	if (wavsam < 0)
		wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
	else
		wfx.SubFormat = GUID_SUBTYPE_PCM;
	wavsam = abs(wavsam);
	wavbit2 = wavbit;
	int i, iii = 0;
	double ik = 32.0;
	double il = 45.71712838;
	for (i = 0; i <= 88; i++, iii++) { // 低音域用
		logtbl[i] = (int)(il * pow(2.0, (double)(iii) / ik));// *(double)BUFSZH1 / (double)192000 / 4.0 + 1.0);
		if (i < 20) {
			ik -= 0.12 / ((double)wavbit / 44100.0);
		}
		else {
			ik -= 0.14 / ((double)wavbit / 44100.0);
		}
		if (i != 0) {
			if (iii>240) {
				break;
			}
			if (logtbl[i] <= logtbl[i - 1]) {
				i--; continue;
			}
		}
		//if( logtbl[i] > BUFSZH1 -1 ) logtbl[i] = BUFSZH1 -1;
	}


	//    mmRes = waveOutOpen(&hwo,WAVE_MAPPER,&wfx1,(DWORD)(LPVOID)0,(DWORD)NULL,CALLBACK_NULL);

	fade1 = 0;
	//-------------------------------------------------------------------
	//if (pAudioClient == NULL) {
	DSBUFFERDESC dsbd;
	ZeroMemory(&dsbd, sizeof(DSBUFFERDESC));
	dsbd.dwSize = sizeof(DSBUFFERDESC);
	dsbd.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLVOLUME;// | DSBCAPS_CTRL3D;
	dsbd.dwBufferBytes = OUTPUT_BUFFER_SIZE * OUTPUT_BUFFER_NUM;
	if (wavch > 2)
		dsbd.lpwfxFormat = (LPWAVEFORMATEX)&wfx;
	else
		dsbd.lpwfxFormat = &wfx1;
	//dsbd.guid3DAlgorithm = DS3DALG_HRTF_LIGHT;
	HRESULT r;
	r = m_ds->CreateSoundBuffer(&dsbd, &m_dsb1, NULL);
	if (m_dsb1 == NULL) {
		CString s; s.Format(L"%d", savedata.samples);
		MessageBox(s + LL2(L"Hzのサンプリングレートにサウンドカードが対応していません", L"Hz sampling rate not supported by sound card"), LL2(L"ogg/wav簡易プレイヤ", L"ogg/wav Simple Player"));
		return;
	}
	for (i = 0; i < 10; i++) {
		r = m_dsb1->QueryInterface(IID_IDirectSoundBuffer8, (void**)&m_dsb);

		if (m_dsb == NULL) { DoEvent(); Sleep(100); continue; }
		else break;
	}
	if (m_dsb == NULL) {
		AfxMessageBox(LL2(L"DirectSoundが開けませんでした。", L"Could not open DirectSound."));
		if (r == DSERR_ALLOCATED) {
			AfxMessageBox(LL2(L"優先レベルなどのリソースが他の呼び出しによって既に使用中であるため、要求は失敗した。", L"Request failed: resources in use."));
		}
		else if (r == DSERR_CONTROLUNAVAIL) {
			AfxMessageBox(LL2(L"呼び出し元が要求するバッファ コントロール (ボリューム、パンなど) は利用できない。", L"Buffer control requested is not available."));
		}
		else if (r == DSERR_BADFORMAT) {
			AfxMessageBox(LL2(L"指定したウェーブ フォーマットはサポートされていない。", L"Specified wave format not supported."));
		}
		else if (r == DSERR_INVALIDPARAM) {
			AfxMessageBox(LL2(L"無効なパラメータが関数に渡された。", L"Invalid parameter passed."));
		}
		else if (r == DSERR_NOAGGREGATION) {
			AfxMessageBox(LL2(L"このオブジェクトは COM 集合化をサポートしない。", L"Object does not support COM aggregation."));
		}
		else if (r == DSERR_OUTOFMEMORY) {
			AfxMessageBox(LL2(L"DirectSound サブシステムは、呼び出し元の要求を完了するための十分なメモリを割り当てられなかった。", L"DirectSound could not allocate enough memory."));
		}
		else if (r == DSERR_UNINITIALIZED) {
			AfxMessageBox(LL2(L"他のメソッドを呼び出す前に IDirectSound::Initialize メソッドを呼び出さなかったか、呼び出しが成功しなかった。", L"IDirectSound::Initialize not called or failed."));
		}
		else if (r == DSERR_UNSUPPORTED) {
			AfxMessageBox(LL2(L"呼び出した関数はこの時点ではサポートされていない。", L"Function not supported at this point."));
		}
		else {}

		tagfile = fnn;
		og->m_saisai.EnableWindow(TRUE);
		return;
	}
	m_dsb->Play(0, 0, DSBPLAY_LOOPING);
}


void CRender::OnBnClickedButton1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	CZeroFol z;
	z.DoModal();
}


void CRender::OnCbnSelchangeCombo3()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.samples = samp[m_Hz.GetCurSel()];
}


HBRUSH CRender::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);

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


int CRender::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CCustomBlurDialogExBase::OnCreate(lpCreateStruct) == -1)
		return -1;

	// TODO: ここに特定な作成コードを追加してください。
	ModifyStyleEx(0, WS_EX_LAYERED);

	// レイヤードウィンドウの不透明度と透明のカラーキー
	SetLayeredWindowAttributes(RGB(255, 0, 0), 0, LWA_COLORKEY);

	// 赤色のブラシを作成する．
	m_brDlg.CreateSolidBrush(RGB(255, 0, 0));
	return 0;
}


void CRender::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogExBase::OnMoving(fwSide, pRect);
	CRect r;
	GetWindowRect(&r);
	if (savedata.aero)

	renderbase->MoveWindow(&r);
	// TODO: ここにメッセージ ハンドラー コードを追加します。
}

int CRender::Create(CWnd* pWnd)
{
	m_pParent = NULL;
	BOOL bret = CCustomBlurDialogExBase::Create(CRender::IDD, this);
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

void CRender::OnBnClickedCancel()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	if (savedata.aero)

	delete renderbase;
	CCustomBlurDialogExBase::OnCancel();
}

void CRender::OnBnClickedCheck52()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
}

void CRender::OnCbnEditchangeCombo4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_speana.SetCheck(TRUE);
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
}

void CRender::OnCbnSelchangeCombo4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	m_speana.SetCheck(TRUE);
	savedata.speanamode = m_speana.GetCheck();
	savedata.speananum = m_speana_num.GetCurSel();
}

void CRender::OnBnClickedCancel5()
{
	// プログラムの実行ファイルパスを取得
	TCHAR szExePath[MAX_PATH];
	GetModuleFileName(NULL, szExePath, MAX_PATH);

	CString strProgID = _T("falcombgm.mediaplayer");
	CString strAppName = _T("Falcom BGM&メディアプレイヤー");

	// 対応拡張子一覧
	const TCHAR* extensions[] = {
		_T(".mp3"), _T(".mp2"), _T(".mp1"), _T(".rmp"),
		_T(".ogg"), _T(".flac"), _T(".m4a"), _T(".aac"),
		_T(".dsf"), _T(".dff"), _T(".mp4"), _T(".mkv"), _T(".avi")
	};

	HKEY hKey;
	LONG result;

	// 1. ProgIDの登録
	CString strProgIDKey = _T("Software\\Classes\\") + strProgID;
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strProgIDKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		RegSetValueEx(hKey, NULL, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strAppName,
			(strAppName.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 2. DefaultIconの設定
	CString strIconKey = strProgIDKey + _T("\\DefaultIcon");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strIconKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strIcon;
		strIcon.Format(_T("%s,0"), szExePath);
		RegSetValueEx(hKey, NULL, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strIcon,
			(strIcon.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 3. shell\open\commandの設定
	CString strCommandKey = strProgIDKey + _T("\\shell\\open\\command");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strCommandKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strCommand;
		strCommand.Format(_T("\"%s\" \"%%1\""), szExePath);
		RegSetValueEx(hKey, NULL, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strCommand,
			(strCommand.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 4. 各拡張子にOpenWithProgidsを設定
	for (int i = 0; i < _countof(extensions); i++)
	{
		CString strExtKey;
		strExtKey.Format(_T("Software\\Classes\\%s\\OpenWithProgids"), extensions[i]);

		result = RegCreateKeyEx(HKEY_CURRENT_USER, strExtKey, 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
		if (result == ERROR_SUCCESS)
		{
			// 値は空でOK(値の存在自体が意味を持つ)
			RegSetValueEx(hKey, strProgID, 0, REG_NONE, NULL, 0);
			RegCloseKey(hKey);
		}
	}

	// 5. アプリケーションの登録
	CString strAppKey = _T("Software\\") + strAppName;
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strAppKey + _T("\\Capabilities"),
		0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strDesc = _T("ファルコムゲームBGMのループ再生対応メディアプレイヤー");
		RegSetValueEx(hKey, _T("ApplicationDescription"), 0, REG_SZ,
			(BYTE*)(LPCTSTR)strDesc,
			(strDesc.GetLength() + 1) * sizeof(TCHAR));
		RegSetValueEx(hKey, _T("ApplicationName"), 0, REG_SZ,
			(BYTE*)(LPCTSTR)strAppName,
			(strAppName.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 6. ファイル関連付けの登録
	CString strFileAssocKey = strAppKey + _T("\\Capabilities\\FileAssociations");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strFileAssocKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		for (int i = 0; i < _countof(extensions); i++)
		{
			RegSetValueEx(hKey, extensions[i], 0, REG_SZ,
				(BYTE*)(LPCTSTR)strProgID,
				(strProgID.GetLength() + 1) * sizeof(TCHAR));
		}
		RegCloseKey(hKey);
	}

	// 7. Registered Applicationsに登録
	CString strRegAppKey = _T("Software\\RegisteredApplications");
	result = RegCreateKeyEx(HKEY_CURRENT_USER, strRegAppKey, 0, NULL,
		REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (result == ERROR_SUCCESS)
	{
		CString strCapPath = _T("Software\\") + strAppName + _T("\\Capabilities");
		RegSetValueEx(hKey, strAppName, 0, REG_SZ,
			(BYTE*)(LPCTSTR)strCapPath,
			(strCapPath.GetLength() + 1) * sizeof(TCHAR));
		RegCloseKey(hKey);
	}

	// 8. 変更をシステムに通知
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

	AfxMessageBox(LL2(L"ファイルの関連付け登録が完了しました。", L"File association registration completed."), MB_ICONINFORMATION);
}
