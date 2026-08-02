#include "stdafx.h"
#include "CPromptDlg.h"
#include "CPromptEngine.h"
#include "CPromptAnalyze.h"
#include "CCommandRollDlg.h"

extern save savedata;
extern void MpPersistSavedataQuick();
static CPromptDlg* g_promptDlg = nullptr;
static BOOL g_histSelChanging = FALSE;

IMPLEMENT_DYNAMIC(CPromptDlg, CCustomBlurDialogExBase)

CPromptDlg::CPromptDlg(CWnd* pParent)
	: CCustomBlurDialogExBase(CPromptDlg::IDD, pParent)
{
}

CPromptDlg::~CPromptDlg()
{
	if (g_promptDlg == this)
		g_promptDlg = nullptr;
}

void CPromptDlg::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_MPP_TEXT, m_edit);
	DDX_Control(pDX, IDC_MPP_LEGEND, m_legend);
	DDX_Control(pDX, IDC_MPP_RUN, m_run);
	DDX_Control(pDX, IDC_MPP_ANALYZE, m_analyze);
	DDX_Control(pDX, IDC_MPP_ROLL, m_roll);
	DDX_Control(pDX, IDC_MPP_STOP, m_stop);
	DDX_Control(pDX, IDC_MPP_RESET, m_reset);
	DDX_Control(pDX, IDC_MPP_CLEAR, m_clear);
	DDX_Control(pDX, IDC_MPP_CLOSE, m_close);
	DDX_Control(pDX, IDC_MPP_HIST, m_hist);
	DDX_Control(pDX, IDC_MPP_MODE, m_mode);
	DDX_Control(pDX, IDC_MPP_SAVEHIST, m_saveHist);
}

BEGIN_MESSAGE_MAP(CPromptDlg, CCustomBlurDialogExBase)
	ON_BN_CLICKED(IDC_MPP_RUN, &CPromptDlg::OnRun)
	ON_BN_CLICKED(IDC_MPP_ANALYZE, &CPromptDlg::OnAnalyze)
	ON_BN_CLICKED(IDC_MPP_ROLL, &CPromptDlg::OnRoll)
	ON_BN_CLICKED(IDC_MPP_STOP, &CPromptDlg::OnStop)
	ON_BN_CLICKED(IDC_MPP_RESET, &CPromptDlg::OnReset)
	ON_BN_CLICKED(IDC_MPP_CLEAR, &CPromptDlg::OnClear)
	ON_BN_CLICKED(IDC_MPP_CLOSE, &CPromptDlg::OnCloseBtn)
	ON_BN_CLICKED(IDC_MPP_SAVEHIST, &CPromptDlg::OnSaveHist)
	ON_CBN_SELCHANGE(IDC_MPP_HIST, &CPromptDlg::OnHistSel)
	ON_CBN_SELCHANGE(IDC_MPP_MODE, &CPromptDlg::OnModeSel)
	ON_EN_CHANGE(IDC_MPP_TEXT, &CPromptDlg::OnTextChanged)
	ON_WM_CONTEXTMENU()
	ON_WM_SIZE()
	ON_WM_ENTERSIZEMOVE()
	ON_WM_EXITSIZEMOVE()
	ON_WM_MOVING()
	ON_WM_GETMINMAXINFO()
	ON_WM_MOUSEWHEEL()
	ON_WM_CTLCOLOR()
	ON_WM_CLOSE()
#if CCUSTOM_AERO_SUPPORT
	ON_MESSAGE(CCC_MSG_REAPPLY_OPAQUE_FIXERS, OnReapplyOpaqueFixers)
#endif
END_MESSAGE_MAP()

static CString MpPromptLegendText()
{
	return LL14(
		L"━━ 使い方 ━━\r\n"
		L"1. 上の入力欄にコマンドを書く (複数行可、1行に複数 @/% も可)\r\n"
		L"2. [実行] で解析・有効化 → 演奏中、時刻になると自動適用\r\n"
		L"3. [停止]=適用停止(値維持)  [リセット]=実行前に戻す  [クリア]=本文消去\r\n"
		L"※ [解析]=選択曲を読込しながら解析し、時間/効果コマンドを自動生成(パターンベース)\r\n"
		L"※ 時刻はメディアプレイヤー・バナー(GDI)の時計と同じ基準です\r\n"
		L"※ 適用はDS先読み分を先取りするため、記載した秒で聴感上も切り替わります\r\n"
		L"\r\n"
		L"【形式】 @<cmd><時刻>[-<終了時刻>][<値>[-<終了値>]]\r\n"
		L"【周期】 %<cmd><周期><開始[-終了]>[<値>[-<終了値>]]  … 一定間隔で繰り返す(@の拡張)\r\n"
		L"  例: %N1:00<20-40>[100-120]  … 1分周期の20〜40秒で鮮明100→120%(窓外は非適用)\r\n"
		L"【時刻】 秒(50) または 分:秒(1:20)。例: 1:20 = 80秒\r\n"
		L"【値】 基本は0〜200 (100=原曲)。[]内2値 = 開始〜終了を線形補間\r\n"
		L"【基本】 p=ピッチ  t=テンポ  d=DirectSound音量\r\n"
		L"【EQ周波数帯】(小文字 a-o = イコライザー15帯)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"【EQ項目】(大文字 M/N/K/I/S/E/F)\r\n"
		L"  M=マスター N=鮮明 K=高低(バランス) I=密度 S=立体\r\n"
		L"  E=環境番号(0〜100, 0=なし)  F=環境のかかり具合(0〜200, EQスライダーと同じ)\r\n"
		L"  (互換: 小文字 s も立体。sb/sl 演出は2文字。小文字 e/f は周波数帯)\r\n"
		L"【効果】 r=リバーブ  c=コーラス  y=ディレイ\r\n"
		L"  ※0=オフ / 1〜100=通常(強さ) / 101〜200=別モード(強さ=値-100)\r\n"
		L"    r:通常リバーブ / パンリバーブ\r\n"
		L"    c:通常コーラス / コーラスディストーション\r\n"
		L"    y:通常ディレイ / マルチディレイ(ピンポン)\r\n"
		L"【演出】 sb=しょんぼり  br=明るめ  sl=スロー  fa=ファスト\r\n"
		L"         wm=温か  cd=冷たく  dp=深め  wi=広がり  nr=寄り  gn=穏やか  pw=パワー  dr=夢うつつ (値不要)\r\n"
		L"【例1】 @p50-1:20[100-120]  … 50秒〜1:20でピッチ100→120%\r\n"
		L"【例2】 @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"【例3】 @t0-30[100-80]  … 曲頭から30秒かけてテンポ100→80%\r\n"
		L"【例4】 @E30[12] @F30-1:00[0-160]  … 30秒で環境#12、かかり具合を30秒〜1:00で0→160",
		L"━━ How to use ━━\r\n"
		L"1. Type commands in the box above (multiple lines OK, several @ per line OK)\r\n"
		L"2. [Run] to parse & enable → auto-applied at the set time during playback\r\n"
		L"3. [Stop]=stop applying (keep values)  [Reset]=revert to pre-run  [Clear]=erase text\r\n"
		L"※ Times use the same clock as the media player / banner (GDI)\r\n"
		L"※ Applied ahead via the DS look-ahead, so it switches audibly at the written time\r\n"
		L"\r\n"
		L"[Format] @<cmd><time>[-<endTime>][<val>[-<endVal>]]\r\n"
		L"[Period] %<cmd><period><start[-end]>[<val>[-<endVal>]]  … repeats each period (@ extension)\r\n"
		L"  e.g. %N1:00<20-40>[100-120]  … clarity 100→120% during sec 20-40 of each 1-min span (inactive outside)\r\n"
		L"[Time] sec(50) or min:sec(1:20). e.g. 1:20 = 80 sec\r\n"
		L"[Value] usually 0-200 (100=original). Two values in [] = linear ramp from start to end\r\n"
		L"[Basic] p=pitch  t=tempo  d=DirectSound volume\r\n"
		L"[EQ bands] (lowercase a-o = the 15 equalizer bands)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[EQ controls] (uppercase M/N/K/I/S/E/F)\r\n"
		L"  M=Master N=Clarity K=Balance(hi/lo) I=Density S=Stereo\r\n"
		L"  E=Environment no.(0-100, 0=none)  F=Environment amount(0-200, same as EQ slider)\r\n"
		L"  (compat: lowercase s is also Stereo. sb/sl presets are 2 letters. lowercase e/f are bands)\r\n"
		L"[FX] r=Reverb  c=Chorus  y=Delay\r\n"
		L"  *0=off / 1-100=normal(amount) / 101-200=alt mode(amount=value-100)\r\n"
		L"    r:reverb / pan-reverb\r\n"
		L"    c:chorus / chorus-distortion\r\n"
		L"    y:delay / multi-delay(ping-pong)\r\n"
		L"[Presets] sb=melancholy  br=bright  sl=slow  fa=fast\r\n"
		L"  wm=warm  cd=cold  dp=deep  wi=wide  nr=near  gn=gentle  pw=power  dr=dreamy (no value)\r\n"
		L"[Ex1] @p50-1:20[100-120]  … pitch 100→120% from 50s to 1:20\r\n"
		L"[Ex2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Ex3] @t0-30[100-80]  … tempo 100→80% over 30s from the start\r\n"
		L"[Ex4] @E30[12] @F30-1:00[0-160]  … env #12 at 30s, amount 0→160 from 30s to 1:00",
		L"━━ Utilisation ━━\r\n"
		L"1. Saisissez des commandes dans le champ ci-dessus (plusieurs lignes possibles, plusieurs @ par ligne)\r\n"
		L"2. [Executer] pour analyser et activer → applique automatiquement a l'heure pendant la lecture\r\n"
		L"3. [Arreter]=arrete l'application (garde les valeurs)  [Reinitialiser]=revient a l'etat initial  [Effacer]=efface le texte\r\n"
		L"※ Les heures utilisent la meme horloge que le lecteur / la banniere (GDI)\r\n"
		L"※ Applique via l'anticipation DS, donc le changement s'entend a l'heure indiquee\r\n"
		L"\r\n"
		L"[Format] @<cmd><heure>[-<fin>][<val>[-<valFin>]]\r\n"
		L"[Periode] %<cmd><periode><debut[-fin]>[val]  … repetition (@ etendu). Ex: %N1:00<20-40>[100-120]\r\n"
		L"[Heure] sec(50) ou min:sec(1:20). Ex : 1:20 = 80 sec\r\n"
		L"[Valeur] normalement 0-200 (100=original). Deux valeurs dans [] = rampe lineaire du debut a la fin\r\n"
		L"[Base] p=hauteur  t=tempo  d=volume DirectSound\r\n"
		L"[Bandes EQ] (minuscules a-o = les 15 bandes de l'egaliseur)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[Controles EQ] (majuscules M/N/K/I/S/E/F)\r\n"
		L"  M=Master N=Clarte K=Balance(aigu/grave) I=Densite S=Stereo\r\n"
		L"  E=No d'environnement(0-100, 0=aucun)  F=Intensite d'environnement(0-200, meme curseur EQ)\r\n"
		L"  (compat : la minuscule s = aussi Stereo. Les presets sb/sl ont 2 lettres. Les minuscules e/f sont des bandes)\r\n"
		L"[Effets] r=Reverb  c=Chorus  y=Delay\r\n"
		L"[Presets] sb=melancolique  br=lumineux  sl=lent  fa=rapide (sans valeur)\r\n"
		L"[Ex1] @p50-1:20[100-120]  … hauteur 100→120% de 50s a 1:20\r\n"
		L"[Ex2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Ex3] @t0-30[100-80]  … tempo 100→80% sur 30s depuis le debut\r\n"
		L"[Ex4] @E30[12] @F30-1:00[0-160]  … env #12 a 30s, intensite 0→160 de 30s a 1:00",
		L"━━ Uso ━━\r\n"
		L"1. Scrivi i comandi nel campo sopra (piu righe consentite, piu @ per riga)\r\n"
		L"2. [Esegui] per analizzare e attivare → applicato automaticamente all'orario durante la riproduzione\r\n"
		L"3. [Ferma]=ferma l'applicazione (mantiene i valori)  [Reimposta]=torna allo stato iniziale  [Cancella]=cancella il testo\r\n"
		L"※ Gli orari usano lo stesso riferimento dell'orologio del lettore / banner (GDI)\r\n"
		L"※ Applicato tramite l'anticipo DS, quindi il cambio si sente all'orario indicato\r\n"
		L"\r\n"
		L"[Formato] @<cmd><tempo>[-<fine>][<val>[-<valFine>]]\r\n"
		L"[Periodo] %<cmd><periodo><inizio[-fine]>[val]  … ripetizione (estensione @). Es: %N1:00<20-40>[100-120]\r\n"
		L"[Tempo] sec(50) o min:sec(1:20). Es: 1:20 = 80 sec\r\n"
		L"[Valore] di norma 0-200 (100=originale). Due valori in [] = rampa lineare da inizio a fine\r\n"
		L"[Base] p=intonazione  t=tempo  d=volume DirectSound\r\n"
		L"[Bande EQ] (minuscole a-o = le 15 bande dell'equalizzatore)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[Controlli EQ] (maiuscole M/N/K/I/S/E/F)\r\n"
		L"  M=Master N=Nitidezza K=Bilanciamento(alti/bassi) I=Densita S=Stereo\r\n"
		L"  E=N. ambiente(0-100, 0=nessuno)  F=Intensita ambiente(0-200, stesso slider EQ)\r\n"
		L"  (compat: la minuscola s = anche Stereo. I preset sb/sl hanno 2 lettere. Le minuscole e/f sono bande)\r\n"
		L"[Effetti] r=Riverbero  c=Chorus  y=Delay\r\n"
		L"[Preset] sb=malinconico  br=luminoso  sl=lento  fa=veloce (senza valore)\r\n"
		L"[Es1] @p50-1:20[100-120]  … intonazione 100→120% da 50s a 1:20\r\n"
		L"[Es2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Es3] @t0-30[100-80]  … tempo 100→80% in 30s dall'inizio\r\n"
		L"[Es4] @E30[12] @F30-1:00[0-160]  … ambiente #12 a 30s, intensita 0→160 da 30s a 1:00",
		L"━━ Uso ━━\r\n"
		L"1. Escriba comandos en el campo de arriba (varias lineas posibles, varios @ por linea)\r\n"
		L"2. [Ejecutar] para analizar y activar → se aplica automaticamente a la hora durante la reproduccion\r\n"
		L"3. [Detener]=detiene la aplicacion (mantiene valores)  [Restablecer]=vuelve al estado inicial  [Borrar]=borra el texto\r\n"
		L"※ Las horas usan el mismo reloj que el reproductor / banner (GDI)\r\n"
		L"※ Se aplica mediante la anticipacion DS, asi que el cambio se oye a la hora indicada\r\n"
		L"\r\n"
		L"[Formato] @<cmd><tiempo>[-<fin>][<val>[-<finVal>]]\r\n"
		L"[Periodo] %<cmd><periodo><inicio[-fin]>[val]  … repeticion (extension @). Ej: %N1:00<20-40>[100-120]\r\n"
		L"[Tiempo] seg(50) o min:seg(1:20). Ej: 1:20 = 80 seg\r\n"
		L"[Valor] normalmente 0-200 (100=original). Dos valores en [] = rampa lineal de inicio a fin\r\n"
		L"[Basico] p=tono  t=tempo  d=volumen DirectSound\r\n"
		L"[Bandas EQ] (minusculas a-o = las 15 bandas del ecualizador)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[Controles EQ] (mayusculas M/N/K/I/S/E/F)\r\n"
		L"  M=Master N=Claridad K=Balance(agudos/graves) I=Densidad S=Estereo\r\n"
		L"  E=N. de entorno(0-100, 0=ninguno)  F=Intensidad de entorno(0-200, mismo slider EQ)\r\n"
		L"  (compat: la minuscula s = tambien Estereo. Los preajustes sb/sl tienen 2 letras. Las minusculas e/f son bandas)\r\n"
		L"[Efectos] r=Reverb  c=Chorus  y=Delay\r\n"
		L"[Preajustes] sb=melancolico  br=brillante  sl=lento  fa=rapido (sin valor)\r\n"
		L"[Ej1] @p50-1:20[100-120]  … tono 100→120% de 50s a 1:20\r\n"
		L"[Ej2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Ej3] @t0-30[100-80]  … tempo 100→80% en 30s desde el inicio\r\n"
		L"[Ej4] @E30[12] @F30-1:00[0-160]  … entorno #12 a 30s, intensidad 0→160 de 30s a 1:00",
		L"━━ 사용법 ━━\r\n"
		L"1. 위 입력란에 명령을 씁니다 (여러 줄 가능, 한 줄에 여러 @ 가능)\r\n"
		L"2. [실행]으로 해석·활성화 → 재생 중 시각이 되면 자동 적용\r\n"
		L"3. [정지]=적용 정지(값 유지)  [리셋]=실행 전으로 복귀  [지우기]=본문 삭제\r\n"
		L"※ 시각은 미디어 플레이어·배너(GDI) 시계와 같은 기준입니다\r\n"
		L"※ 적용은 DS 선행 읽기분을 앞당기므로 기재한 초에 청각상으로도 전환됩니다\r\n"
		L"\r\n"
		L"[형식] @<cmd><시각>[-<종료시각>][<값>[-<종료값>]]\r\n"
		L"[주기] %<cmd><주기><시작[-끝]>[값]  … 일정 간격 반복(@ 확장). 예: %N1:00<20-40>[100-120]\r\n"
		L"[시각] 초(50) 또는 분:초(1:20). 예: 1:20 = 80초\r\n"
		L"[값] 기본은 0~200 (100=원곡). [] 안 2값 = 시작~종료 선형 보간\r\n"
		L"[기본] p=피치  t=템포  d=DirectSound 음량\r\n"
		L"[EQ 주파수대] (소문자 a-o = 이퀄라이저 15밴드)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[EQ 항목] (대문자 M/N/K/I/S/E/F)\r\n"
		L"  M=마스터 N=선명 K=고저(밸런스) I=밀도 S=입체\r\n"
		L"  E=환경 번호(0~100, 0=없음)  F=환경 적용량(0~200, EQ 슬라이더와 동일)\r\n"
		L"  (호환: 소문자 s도 입체. sb/sl 연출은 2글자. 소문자 e/f는 주파수대)\r\n"
		L"[효과] r=리버브  c=코러스  y=딜레이\r\n"
		L"[연출] sb=시무룩  br=밝게  sl=슬로우  fa=패스트 (값 불필요)\r\n"
		L"[예1] @p50-1:20[100-120]  … 50초~1:20에서 피치 100→120%\r\n"
		L"[예2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[예3] @t0-30[100-80]  … 곡 시작부터 30초에 걸쳐 템포 100→80%\r\n"
		L"[예4] @E30[12] @F30-1:00[0-160]  … 30초에 환경#12, 적용량을 30초~1:00에서 0→160",
		L"━━ 使用方法 ━━\r\n"
		L"1. 在上方输入框中写入命令（可多行，一行可多个 @）\r\n"
		L"2. [执行] 解析并启用 → 播放中到达时刻时自动应用\r\n"
		L"3. [停止]=停止应用（保留值）  [重置]=恢复到执行前  [清除]=清空正文\r\n"
		L"※ 时刻与媒体播放器·横幅(GDI)的时钟基准相同\r\n"
		L"※ 应用会提前 DS 预读部分，因此在标注的秒数听感上也会切换\r\n"
		L"\r\n"
		L"【格式】 @<cmd><时刻>[-<结束时刻>][<值>[-<结束值>]]\r\n"
		L"【周期】 %<cmd><周期><开始[-结束]>[值]  … 按间隔重复(@扩展)。例: %N1:00<20-40>[100-120]\r\n"
		L"【时刻】 秒(50) 或 分:秒(1:20)。例: 1:20 = 80秒\r\n"
		L"【值】 基本为0~200 (100=原曲)。[]内两值 = 起点~终点线性插值\r\n"
		L"【基本】 p=音高  t=速度  d=DirectSound音量\r\n"
		L"【EQ频率带】(小写 a-o = 均衡器15段)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"【EQ项目】(大写 M/N/K/I/S/E/F)\r\n"
		L"  M=主控 N=清晰 K=高低(平衡) I=密度 S=立体\r\n"
		L"  E=环境编号(0~100, 0=无)  F=环境强度(0~200, 与EQ滑块相同)\r\n"
		L"  (兼容: 小写 s 也是立体。sb/sl 演出为2字母。小写 e/f 为频率带)\r\n"
		L"【效果】 r=混响  c=合唱  y=延迟\r\n"
		L"【演出】 sb=消沉  br=明亮  sl=慢速  fa=快速 (无需值)\r\n"
		L"【例1】 @p50-1:20[100-120]  … 50秒~1:20内音高100→120%\r\n"
		L"【例2】 @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"【例3】 @t0-30[100-80]  … 从曲首起30秒内速度100→80%\r\n"
		L"【例4】 @E30[12] @F30-1:00[0-160]  … 30秒时环境#12，强度在30秒~1:00内0→160",
		L"━━ طريقة الاستخدام ━━\r\n"
		L"1. اكتب الأوامر في الحقل أعلاه (يُسمح بعدة أسطر، وعدة @ في السطر)\r\n"
		L"2. [تشغيل] للتحليل والتفعيل → يُطبَّق تلقائياً عند الوقت أثناء التشغيل\r\n"
		L"3. [إيقاف]=يوقف التطبيق (يحتفظ بالقيم)  [إعادة تعيين]=العودة لما قبل التنفيذ  [مسح]=مسح النص\r\n"
		L"※ الأوقات تستخدم نفس ساعة مشغل الوسائط · اللافتة (GDI)\r\n"
		L"※ يُطبَّق مع قراءة DS المسبقة، لذا يتبدّل سمعياً عند الثانية المذكورة\r\n"
		L"\r\n"
		L"[الصيغة] @<cmd><وقت>[-<وقت النهاية>][<قيمة>[-<قيمة النهاية>]]\r\n"
		L"[دورة] %<cmd><دورة><بداية[-نهاية]>[قيمة]  … تكرار (@). مثال: %N1:00<20-40>[100-120]\r\n"
		L"[الوقت] ثانية(50) أو دقيقة:ثانية(1:20). مثال: 1:20 = 80 ثانية\r\n"
		L"[القيمة] عادةً 0-200 (100=الأصل). قيمتان داخل [] = تدرّج خطي من البداية إلى النهاية\r\n"
		L"[أساسي] p=طبقة الصوت  t=إيقاع  d=مستوى DirectSound\r\n"
		L"[نطاقات EQ] (الأحرف الصغيرة a-o = نطاقات المعادل الـ15)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[عناصر EQ] (الأحرف الكبيرة M/N/K/I/S/E/F)\r\n"
		L"  M=رئيسي N=وضوح K=توازن(حاد/غليظ) I=كثافة S=مجسم\r\n"
		L"  E=رقم البيئة(0-100، 0=بلا)  F=شدة البيئة(0-200)\r\n"
		L"  (توافق: الحرف الصغير s أيضاً مجسم. إعدادات sb/sl حرفان. الأحرف الصغيرة e/f نطاقات)\r\n"
		L"[التأثيرات] r=صدى  c=كورس  y=تأخير\r\n"
		L"[الإعدادات] sb=حزين  br=مشرق  sl=بطيء  fa=سريع (بلا قيمة)\r\n"
		L"[مثال1] @p50-1:20[100-120]  … الطبقة من 100 إلى 120% بين 50ث و1:20\r\n"
		L"[مثال2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[مثال3] @t0-30[100-80]  … الإيقاع من 100 إلى 80% خلال 30ث من البداية\r\n"
		L"[مثال4] @E30[12] @F30-1:00[0-160]  … البيئة #12 عند 30ث، والشدة من 0 إلى 160 بين 30ث و1:00",
		L"━━ Использование ━━\r\n"
		L"1. Введите команды в поле выше (можно несколько строк, несколько @ в строке)\r\n"
		L"2. [Выполнить] — разбор и активация → применяется автоматически по времени во время воспроизведения\r\n"
		L"3. [Стоп]=прекратить применение (значения сохраняются)  [Сброс]=вернуть к исходному  [Очистить]=стереть текст\r\n"
		L"※ Время использует те же часы, что плеер · баннер (GDI)\r\n"
		L"※ Применяется с упреждением буфера DS, поэтому переключение слышно на указанной секунде\r\n"
		L"\r\n"
		L"[Формат] @<cmd><время>[-<конец>][<знач>[-<конЗнач>]]\r\n"
		L"[Период] %<cmd><период><нач[-кон]>[знач]  … повтор (@). Напр.: %N1:00<20-40>[100-120]\r\n"
		L"[Время] сек(50) или мин:сек(1:20). Напр.: 1:20 = 80 сек\r\n"
		L"[Значение] обычно 0-200 (100=оригинал). Два значения в [] = линейная рампа от начала к концу\r\n"
		L"[Основные] p=высота  t=темп  d=громкость DirectSound\r\n"
		L"[Полосы EQ] (строчные a-o = 15 полос эквалайзера)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[Параметры EQ] (заглавные M/N/K/I/S/E/F)\r\n"
		L"  M=Мастер N=Чёткость K=Баланс(верх/низ) I=Плотность S=Стерео\r\n"
		L"  E=Номер среды(0-100, 0=нет)  F=Интенсивность среды(0-200, как слайдер EQ)\r\n"
		L"  (совмест.: строчная s тоже Стерео. Пресеты sb/sl из 2 букв. Строчные e/f — полосы)\r\n"
		L"[Эффекты] r=Реверб  c=Хорус  y=Дилей\r\n"
		L"[Пресеты] sb=грустно  br=ярко  sl=медленно  fa=быстро (без значения)\r\n"
		L"[Пример1] @p50-1:20[100-120]  … высота 100→120% с 50с до 1:20\r\n"
		L"[Пример2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Пример3] @t0-30[100-80]  … темп 100→80% за 30с от начала\r\n"
		L"[Пример4] @E30[12] @F30-1:00[0-160]  … среда #12 на 30с, интенсивность 0→160 с 30с до 1:00",
		L"━━ Bedienung ━━\r\n"
		L"1. Befehle in das Feld oben eingeben (mehrere Zeilen moglich, mehrere @ pro Zeile)\r\n"
		L"2. [Ausfuhren] zum Parsen & Aktivieren → wird wahrend der Wiedergabe zur Zeit automatisch angewendet\r\n"
		L"3. [Stopp]=Anwendung stoppen (Werte behalten)  [Zurucksetzen]=vor Ausfuhrung zuruck  [Loschen]=Text leeren\r\n"
		L"※ Zeiten nutzen dieselbe Uhr wie Player · Banner (GDI)\r\n"
		L"※ Wird per DS-Vorausschau vorgezogen, daher horbar zur angegebenen Sekunde\r\n"
		L"\r\n"
		L"[Format] @<cmd><Zeit>[-<Ende>][<Wert>[-<EndWert>]]\r\n"
		L"[Periode] %<cmd><Periode><Start[-Ende]>[Wert]  … Wiederholung (@). z.B.: %N1:00<20-40>[100-120]\r\n"
		L"[Zeit] Sek(50) oder Min:Sek(1:20). z.B.: 1:20 = 80 Sek\r\n"
		L"[Wert] normal 0-200 (100=Original). Zwei Werte in [] = lineare Rampe von Anfang bis Ende\r\n"
		L"[Basis] p=Tonhohe  t=Tempo  d=DirectSound-Lautstarke\r\n"
		L"[EQ-Bander] (Kleinbuchstaben a-o = die 15 Equalizer-Bander)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[EQ-Regler] (Grossbuchstaben M/N/K/I/S/E/F)\r\n"
		L"  M=Master N=Klarheit K=Balance(hoch/tief) I=Dichte S=Stereo\r\n"
		L"  E=Umgebungsnr.(0-100, 0=keine)  F=Umgebungsstarke(0-200, wie EQ-Schieber)\r\n"
		L"  (kompat.: Kleinbuchstabe s ist auch Stereo. sb/sl-Presets haben 2 Buchstaben. Kleinbuchstaben e/f sind Bander)\r\n"
		L"[Effekte] r=Reverb  c=Chorus  y=Delay\r\n"
		L"[Presets] sb=trube  br=hell  sl=langsam  fa=schnell (kein Wert)\r\n"
		L"[Bsp1] @p50-1:20[100-120]  … Tonhohe 100→120% von 50s bis 1:20\r\n"
		L"[Bsp2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Bsp3] @t0-30[100-80]  … Tempo 100→80% uber 30s ab Beginn\r\n"
		L"[Bsp4] @E30[12] @F30-1:00[0-160]  … Umgebung #12 bei 30s, Starke 0→160 von 30s bis 1:00",
		L"━━ Como usar ━━\r\n"
		L"1. Escreva comandos no campo acima (varias linhas possiveis, varios @ por linha)\r\n"
		L"2. [Executar] para analisar e ativar → aplicado automaticamente na hora durante a reproducao\r\n"
		L"3. [Parar]=para a aplicacao (mantem valores)  [Redefinir]=volta ao estado inicial  [Limpar]=apaga o texto\r\n"
		L"※ Os horarios usam o mesmo relogio do player · banner (GDI)\r\n"
		L"※ Aplicado via antecipacao do DS, entao a troca e ouvida no segundo indicado\r\n"
		L"\r\n"
		L"[Formato] @<cmd><tempo>[-<fim>][<val>[-<valFim>]]\r\n"
		L"[Periodo] %<cmd><periodo><inicio[-fim]>[val]  … repeticao (@). Ex: %N1:00<20-40>[100-120]\r\n"
		L"[Tempo] seg(50) ou min:seg(1:20). Ex: 1:20 = 80 seg\r\n"
		L"[Valor] normalmente 0-200 (100=original). Dois valores em [] = rampa linear do inicio ao fim\r\n"
		L"[Basico] p=tom  t=andamento  d=volume DirectSound\r\n"
		L"[Bandas EQ] (minusculas a-o = as 15 bandas do equalizador)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[Controles EQ] (maiusculas M/N/K/I/S/E/F)\r\n"
		L"  M=Master N=Nitidez K=Balanco(agudo/grave) I=Densidade S=Estereo\r\n"
		L"  E=N. de ambiente(0-100, 0=nenhum)  F=Intensidade de ambiente(0-200, mesmo slider EQ)\r\n"
		L"  (compat: a minuscula s tambem e Estereo. Os presets sb/sl tem 2 letras. As minusculas e/f sao bandas)\r\n"
		L"[Efeitos] r=Reverb  c=Chorus  y=Delay\r\n"
		L"[Presets] sb=melancolico  br=brilhante  sl=lento  fa=rapido (sem valor)\r\n"
		L"[Ex1] @p50-1:20[100-120]  … tom 100→120% de 50s a 1:20\r\n"
		L"[Ex2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Ex3] @t0-30[100-80]  … andamento 100→80% em 30s desde o inicio\r\n"
		L"[Ex4] @E30[12] @F30-1:00[0-160]  … ambiente #12 aos 30s, intensidade 0→160 de 30s a 1:00",
		L"━━ Gebruik ━━\r\n"
		L"1. Typ opdrachten in het veld hierboven (meerdere regels mogelijk, meerdere @ per regel)\r\n"
		L"2. [Uitvoeren] om te parsen en activeren → automatisch toegepast op tijd tijdens afspelen\r\n"
		L"3. [Stoppen]=toepassing stoppen (waarden behouden)  [Herstellen]=terug naar begin  [Wissen]=tekst wissen\r\n"
		L"※ Tijden gebruiken dezelfde klok als speler · banner (GDI)\r\n"
		L"※ Toegepast via DS-vooruitlezing, dus hoorbaar op de aangegeven seconde\r\n"
		L"\r\n"
		L"[Formaat] @<cmd><tijd>[-<einde>][<waarde>[-<eindWaarde>]]\r\n"
		L"[Periode] %<cmd><periode><start[-eind]>[waarde]  … herhaling (@). Bijv.: %N1:00<20-40>[100-120]\r\n"
		L"[Tijd] sec(50) of min:sec(1:20). Bijv.: 1:20 = 80 sec\r\n"
		L"[Waarde] normaal 0-200 (100=origineel). Twee waarden in [] = lineaire ramp van begin tot eind\r\n"
		L"[Basis] p=toonhoogte  t=tempo  d=DirectSound-volume\r\n"
		L"[EQ-banden] (kleine letters a-o = de 15 equalizerbanden)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[EQ-regelaars] (hoofdletters M/N/K/I/S/E/F)\r\n"
		L"  M=Master N=Helderheid K=Balans(hoog/laag) I=Dichtheid S=Stereo\r\n"
		L"  E=Omgevingsnr.(0-100, 0=geen)  F=Omgevingssterkte(0-200, zelfde EQ-schuif)\r\n"
		L"  (compat: kleine letter s is ook Stereo. sb/sl-presets zijn 2 letters. kleine letters e/f zijn banden)\r\n"
		L"[Effecten] r=Reverb  c=Chorus  y=Delay\r\n"
		L"[Presets] sb=somber  br=helder  sl=langzaam  fa=snel (geen waarde)\r\n"
		L"[Vb1] @p50-1:20[100-120]  … toonhoogte 100→120% van 50s tot 1:20\r\n"
		L"[Vb2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Vb3] @t0-30[100-80]  … tempo 100→80% over 30s vanaf begin\r\n"
		L"[Vb4] @E30[12] @F30-1:00[0-160]  … omgeving #12 op 30s, sterkte 0→160 van 30s tot 1:00",
		L"━━ Obsluga ━━\r\n"
		L"1. Wpisz polecenia w polu powyzej (mozna wiele wierszy, wiele @ w wierszu)\r\n"
		L"2. [Wykonaj] analizuje i wlacza → stosowane automatycznie o czasie podczas odtwarzania\r\n"
		L"3. [Zatrzymaj]=zatrzymuje stosowanie (zachowuje wartosci)  [Resetuj]=powrot do stanu sprzed  [Wyczysc]=usuwa tekst\r\n"
		L"※ Czasy uzywaja tego samego zegara co odtwarzacz · baner (GDI)\r\n"
		L"※ Stosowane z wyprzedzeniem bufora DS, wiec slychac zmiane w podanej sekundzie\r\n"
		L"\r\n"
		L"[Format] @<cmd><czas>[-<koniec>][<wart>[-<wartKonc>]]\r\n"
		L"[Okres] %<cmd><okres><pocz[-kon]>[wart]  … powtarzanie (@). Np.: %N1:00<20-40>[100-120]\r\n"
		L"[Czas] sek(50) lub min:sek(1:20). Np.: 1:20 = 80 sek\r\n"
		L"[Wartosc] zwykle 0-200 (100=oryginal). Dwie wartosci w [] = liniowa rampa od poczatku do konca\r\n"
		L"[Podstawy] p=wysokosc  t=tempo  d=glosnosc DirectSound\r\n"
		L"[Pasma EQ] (male litery a-o = 15 pasm korektora)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[Regulacje EQ] (wielkie litery M/N/K/I/S/E/F)\r\n"
		L"  M=Master N=Wyrazistosc K=Balans(wysokie/niskie) I=Gestosc S=Stereo\r\n"
		L"  E=Numer srodowiska(0-100, 0=brak)  F=Intensywnosc srodowiska(0-200, jak suwak EQ)\r\n"
		L"  (kompat.: mala litera s to tez Stereo. Presety sb/sl maja 2 litery. Male litery e/f to pasma)\r\n"
		L"[Efekty] r=Reverb  c=Chorus  y=Delay\r\n"
		L"[Presety] sb=smutno  br=jasno  sl=wolno  fa=szybko (bez wartosci)\r\n"
		L"[Prz1] @p50-1:20[100-120]  … wysokosc 100→120% od 50s do 1:20\r\n"
		L"[Prz2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Prz3] @t0-30[100-80]  … tempo 100→80% w 30s od poczatku\r\n"
		L"[Prz4] @E30[12] @F30-1:00[0-160]  … srodowisko #12 na 30s, intensywnosc 0→160 od 30s do 1:00",
		L"━━ Kullanim ━━\r\n"
		L"1. Yukaridaki alana komut yazin (birden fazla satir olabilir, satirda birden fazla @ olabilir)\r\n"
		L"2. [Calistir] ayristirir ve etkinlestirir → oynatma sirasinda zamani gelince otomatik uygulanir\r\n"
		L"3. [Durdur]=uygulamayi durdurur (degerleri korur)  [Sifirla]=calistirmadan onceki hale doner  [Temizle]=metni siler\r\n"
		L"※ Zamanlar oynaticiyla · afisle (GDI) ayni saati kullanir\r\n"
		L"※ DS on okuma payi kadar one alinir, boylece belirtilen saniyede duyulur\r\n"
		L"\r\n"
		L"[Bicim] @<cmd><zaman>[-<bitis>][<deger>[-<bitisDeger>]]\r\n"
		L"[Donem] %<cmd><donem><bas[-bit]>[deger]  … tekrar (@). Or: %N1:00<20-40>[100-120]\r\n"
		L"[Zaman] sn(50) veya dk:sn(1:20). Or: 1:20 = 80 sn\r\n"
		L"[Deger] genelde 0-200 (100=orijinal). [] icinde iki deger = basdan sona dogrusal gecis\r\n"
		L"[Temel] p=perde  t=tempo  d=DirectSound ses\r\n"
		L"[EQ bantlari] (kucuk harf a-o = 15 ekolayzer bandi)\r\n"
		L"  a=25 b=40 c=63 d=100 e=160 f=250 g=400 h=630 i=1k\r\n"
		L"  j=1.6k k=2.5k l=4k m=6.3k n=10k o=16k (Hz)\r\n"
		L"[EQ ogeleri] (buyuk harf M/N/K/I/S/E/F)\r\n"
		L"  M=Master N=Netlik K=Denge(tiz/bas) I=Yogunluk S=Stereo\r\n"
		L"  E=Ortam no(0-100, 0=yok)  F=Ortam miktari(0-200, EQ kaydirici ile ayni)\r\n"
		L"  (uyum: kucuk s de Stereo. sb/sl on ayarlari 2 harf. kucuk e/f bantlardir)\r\n"
		L"[Efektler] r=Reverb  c=Chorus  y=Delay\r\n"
		L"[On ayarlar] sb=huzunlu  br=parlak  sl=yavas  fa=hizli (deger gerekmez)\r\n"
		L"[Or1] @p50-1:20[100-120]  … perde 100→120% 50sn-1:20 arasi\r\n"
		L"[Or2] @p1:50[100]  @d2:00[80]  @sb1:30  @br2:00\r\n"
		L"[Or3] @t0-30[100-80]  … basindan itibaren 30sn'de tempo 100→80%\r\n"
		L"[Or4] @E30[12] @F30-1:00[0-160]  … 30sn'de ortam #12, miktar 30sn-1:00 arasi 0→160");
}

static CString MpPromptEditLabelText()
{
	return LL14(
		L"プロンプト入力欄",
		L"Prompt input",
		L"Saisie du prompt",
		L"Campo prompt",
		L"Entrada de prompt",
		L"프롬프트 입력",
		L"提示输入",
		L"إدخال الموجه",
		L"Ввод промпта",
		L"Prompt-Eingabe",
		L"Entrada de prompt",
		L"Prompt invoer",
		L"Pole promptu",
		L"Istem girisi");
}

static void MpInitPromptStatic(CCustomStatic& st, CFont* pFont, BOOL bAero)
{
	if (pFont)
		st.SetFont(pFont, FALSE);
	st.SetGradation(0, 0, 0, FALSE);
	st.SetDropShadow(0, 0, 0, 0, FALSE);
	st.SetPreferWideMode(FALSE);
	st.SetAeroMode(bAero);
}

static void MpSetPromptLabelText(CCustomStatic& st, LPCTSTR plain)
{
	if (!plain || !*plain)
		return;
	CString s;
	s.Format(_T("!@C404858%s"), plain);
	st.SetWindowText(s);
}

void CPromptDlg::StyleButtons()
{
	m_analyze.SetGradation(RGB(255, 230, 200), RGB(255, 180, 120), 0, TRUE);
	m_roll.SetGradation(RGB(220, 235, 255), RGB(160, 195, 240), 0, TRUE);
	m_run.SetGradation(RGB(200, 240, 200), RGB(130, 205, 140), 0, TRUE);
	m_stop.SetGradation(RGB(255, 215, 220), RGB(255, 165, 180), 0, TRUE);
	m_reset.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
	m_clear.SetGradation(RGB(255, 235, 205), RGB(255, 205, 150), 0, TRUE);
	m_close.SetGradation(RGB(235, 230, 240), RGB(205, 195, 215), 0, TRUE);
	m_saveHist.SetGradation(RGB(215, 235, 255), RGB(165, 205, 245), 0, TRUE);
}

void CPromptDlg::SetupTooltips()
{
	if (!CCustomControlUtility::BeginDialogToolTip(m_tooltip, this))
		return;
	auto addTip = [this](CWnd& w, LPCTSTR text) {
		if (!text || !w.GetSafeHwnd()) return;
		m_tooltip.AddTool(&w, text);
	};
	addTip(m_run, LL14(L"プロンプトを解析し、演奏中にパラメータを自動変更します。", L"Parse the prompt and apply parameter changes during playback.", L"Analyser le prompt et appliquer les changements pendant la lecture.", L"Analizza il prompt e applica le modifiche durante l'esecuzione.", L"Analizar el prompt y aplicar cambios durante la reproduccion.", L"프롬프트를 해석해 연주 중 파라미터를 자동 변경합니다.", L"解析提示并在播放中自动更改参数。", L"تحليل الموجه وتطبيق التغييرات أثناء التشغيل.", L"Разобрать промпт и применять изменения при воспроизведении.", L"Prompt parsen und waehrend der Wiedergabe anwenden.", L"Analisar o prompt e aplicar alteracoes durante a reproducao.", L"Prompt parseren en tijdens afspelen toepassen.", L"Parsuj prompt i stosuj zmiany podczas odtwarzania.", L"Istemi ayristirip calma sirasinda uygula."));
	addTip(m_analyze, LL14(L"選択曲を読込しながら解析し、@/% の時間・効果コマンドを自動生成します(再生は一時停止)。雰囲気モードで傾向が変わります。", L"Load and analyze the selected track, then auto-generate @/% timed effect commands (playback pauses). Mood mode changes the style.", L"Charger et analyser la piste, generer des commandes @/%.", L"Carica e analizza la traccia e genera comandi @/%.", L"Cargar y analizar la pista y generar comandos @/%.", L"선택 곡을 읽어 분석해 @/% 명령을 자동 생성합니다(재생 일시중단).", L"读取并分析所选曲目，自动生成 @/% 命令(播放会暂停)。", L"تحليل المقطع وإنشاء أوامر @/%.", L"Загрузить и проанализировать трек, создать команды @/%.", L"Titel laden/analysieren und @/%-Befehle erzeugen (Wiedergabe pausiert).", L"Carregar e analisar a faixa e gerar comandos @/%.", L"Track laden/analyseren en @/%-opdrachten genereren.", L"Wczytaj i przeanalizuj utwor, wygeneruj komendy @/%.", L"Secili parcayi okuyup analiz ederek @/% komut uret (calma durur)."));
	addTip(m_roll, LL14(L"コマンドロールを開き、時間軸でコマンドの変化を確認・編集します。", L"Open the command roll to view/edit timed command changes.", L"Ouvrir le rouleau de commandes.", L"Apri il command roll.", L"Abrir el command roll.", L"커맨드 롤을 열어 시간축에서 명령을 확인/편집합니다.", L"打开命令卷轴以查看/编辑时间轴命令。", L"Open command roll.", L"Открыть command roll.", L"Command-Roll oeffnen.", L"Abrir command roll.", L"Command roll openen.", L"Otworz command roll.", L"Komut rulosunu ac."));
	addTip(m_stop, LL14(L"プロンプト実行を停止します(設定値は維持)。", L"Stop prompt execution (keep current settings).", L"Arreter l'execution du prompt (conserver les reglages).", L"Ferma l'esecuzione del prompt (mantieni i valori).", L"Detener la ejecucion del prompt (mantener ajustes).", L"프롬프트 실행을 중지합니다(설정값 유지).", L"停止提示执行(保留当前设置)。", L"إيقاف تنفيذ الموجه (الإبقاء على الإعدادات).", L"Остановить промпт (настройки сохраняются).", L"Prompt-Ausfuehrung stoppen (Einstellungen behalten).", L"Parar execucao do prompt (manter configuracoes).", L"Prompt uitvoering stoppen (instellingen behouden).", L"Zatrzymaj prompt (zachowaj ustawienia).", L"Istem calistirmasini durdur (ayarlari koru)."));
	addTip(m_reset, LL14(L"実行前の設定に戻し、プロンプト実行を停止します。", L"Restore settings from before execution and stop.", L"Restaurer les reglages d'avant execution et arreter.", L"Ripristina le impostazioni precedenti e ferma.", L"Restaurar ajustes previos y detener.", L"실행 전 설정으로 되돌리고 중지합니다.", L"恢复到执行前设置并停止。", L"استعادة الإعدادات قبل التشغيل والإيقاف.", L"Восстановить настройки до запуска и остановить.", L"Einstellungen vor Ausfuehrung wiederherstellen und stoppen.", L"Restaurar configuracoes anteriores e parar.", L"Instellingen voor uitvoering herstellen en stoppen.", L"Przywroc ustawienia sprzed uruchomienia i zatrzymaj.", L"Calistirmadan onceki ayarlara don ve durdur."));
	addTip(m_clear, LL14(L"プロンプト本文を消去し、設定も初期状態に戻します。", L"Clear prompt text and restore initial settings.", L"Effacer le prompt et restaurer les reglages initiaux.", L"Cancella il prompt e ripristina le impostazioni.", L"Borrar el prompt y restaurar ajustes iniciales.", L"프롬프트 본문을 지우고 설정도 초기화합니다.", L"清除提示文本并恢复初始设置。", L"مسح نص الموجه واستعادة الإعدادات الأولية.", L"Очистить промпт и восстановить исходные настройки.", L"Prompt loeschen und Ausgangseinstellungen wiederherstellen.", L"Limpar prompt e restaurar configuracoes iniciais.", L"Prompt wissen en begininstellingen herstellen.", L"Wyczysc prompt i przywroc ustawienia poczatkowe.", L"Istem metnini temizle ve baslangic ayarlarina don."));
	addTip(m_close, LL14(L"プロンプトウィンドウを閉じます(入力内容は保存)。", L"Close the prompt window (text is saved).", L"Fermer la fenetre de prompt (texte sauvegarde).", L"Chiudi la finestra prompt (testo salvato).", L"Cerrar la ventana de prompt (se guarda el texto).", L"프롬프트 창을 닫습니다(입력 내용 저장).", L"关闭提示窗口(保存输入内容)。", L"إغلاق نافذة الموجه (يُحفظ النص).", L"Закрыть окно промпта (текст сохраняется).", L"Prompt-Fenster schliessen (Text wird gespeichert).", L"Fechar janela de prompt (texto salvo).", L"Promptvenster sluiten (tekst wordt opgeslagen).", L"Zamknij okno promptu (tekst jest zapisywany).", L"Istem penceresini kapat (metin kaydedilir)."));
	addTip(m_saveHist, LL14(L"現在のプロンプト本文を履歴に保存します。", L"Save current prompt text to history.", L"Enregistrer le prompt dans l'historique.", L"Salva il prompt nella cronologia.", L"Guardar el prompt en el historial.", L"현재 프롬프트를 기록에 저장합니다.", L"将当前提示保存到历史。", L"حفظ الموجه في السجل.", L"Сохранить промпт в историю.", L"Prompt in Verlauf speichern.", L"Salvar prompt no historico.", L"Prompt in geschiedenis opslaan.", L"Zapisz prompt w historii.", L"Promptu gecmise kaydet."));
	addTip(m_mode, LL14(L"解析の雰囲気モード(癒やし系含む)。自動生成コマンドの傾向が変わります。", L"Analyze mood mode (includes healing). Changes the style of generated commands.", L"Mode d ambiance (dont guerison).", L"Modalita atmosfera (con guarigione).", L"Modo de ambiente (incluye sanacion).", L"분석 분위기 모드(힐링 포함).", L"分析氛围模式(含疗愈)。", L"Mode (includes healing).", L"Режим (включая исцеление).", L"Stimmungsmodus (inkl. Heilung).", L"Modo (inclui cura).", L"Sfeermodus (incl. heling).", L"Tryb nastroju (z uzdrawianiem).", L"Mod (iyilestirme dahil)."));
	addTip(m_hist, LL14(L"保存したプロンプト履歴から読み込みます。", L"Load a saved prompt from history.", L"Charger un prompt depuis l'historique.", L"Carica un prompt dalla cronologia.", L"Cargar un prompt del historial.", L"저장된 프롬프트 기록에서 불러옵니다.", L"从历史记录加载提示。", L"تحميل موجه من السجل.", L"Загрузить промпт из истории.", L"Prompt aus Verlauf laden.", L"Carregar prompt do historico.", L"Prompt uit geschiedenis laden.", L"Wczytaj prompt z historii.", L"Gecmisten prompt yukle."));
	CCustomControlUtility::FinalizeDialogToolTip(m_tooltip, 360, 10000);
}

static void RaiseChildZOrder(CWnd* pWnd)
{
	if (pWnd && pWnd->GetSafeHwnd())
		pWnd->SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CPromptDlg::LayoutControls()
{
	if (!::IsWindow(GetSafeHwnd())) return;
	CRect rc;
	GetClientRect(&rc);
	const int W = rc.Width(), H = rc.Height();
	if (W < 200) return;

	const int capH = CCC_GetCustomCaptionHeight(m_hWnd);
	const int M = 8;
	const int topM = M + capH; // カスタム帯の下から本文
	const int btnH = 36;
	const int btnGap = 6;
	const int remainH = 16;
	const int modeH = 24;
	const int progH = 16;
	const int progLabelH = 14;
	const int histH = 24;
	const int histGap = 8;
	const int gapSm = 4;
	const int gapMd = 6;
	const int editMinH = 72;
	const int legendMinH = 80;
	const int editLblH = 18;

	const int iw = max(1, W - M * 2);
	const int lockGap = 6;
	const int btnW = max(48, min(68, (iw - btnGap * 6) / 7));

	// キャプション有り時は帯内配置。無し時だけ本文ヘッダ行
	if (capH > 0)
		CCC_MainLockClearHeaderRow(m_hWnd);
	else
		CCC_MainLockSetHeaderRow(m_hWnd, topM, editLblH);
	CRect lockRc;
	CCC_MainLockGetOverlayRect(m_hWnd, lockRc);
	const int lblW = lockRc.IsRectEmpty()
		? max(80, iw - CCC_MainLockGetReserveWidth(m_hWnd) - lockGap)
		: max(80, lockRc.left - M - lockGap);

	// 下から順に確保(ボタン → 履歴 → 進捗 → モード → 残り文字 → 説明 → 入力)
	const int btnY = max(topM, H - M - btnH);
	const int histY = btnY - histGap - histH;
	const int progY = histY - gapSm - progH;
	const int progLblY = progY - gapSm - progLabelH;
	const int modeY = progLblY - gapSm - modeH;
	const int remainY = modeY - gapSm - remainH;
	const int legendBottom = remainY - gapMd;

	int legendH = max(legendMinH, (H * 7) / 30);
	int legendTop = legendBottom - legendH;
	if (legendTop < topM + editLblH + gapSm + editMinH + gapMd) {
		legendTop = topM + editLblH + gapSm + editMinH + gapMd;
		legendH = legendBottom - legendTop;
	}
	if (legendH < 48)
		legendH = max(48, legendBottom - legendTop);
	if (legendTop + legendH > legendBottom)
		legendH = max(48, legendBottom - legendTop);

	const int editTop = topM + editLblH + gapSm;
	int editH = legendTop - gapMd - editTop;
	if (editH < editMinH) {
		editH = editMinH;
		legendTop = editTop + editH + gapMd;
		legendH = max(48, legendBottom - legendTop);
		if (legendTop + legendH > legendBottom)
			legendH = max(48, legendBottom - legendTop);
	}

	if (m_lblEdit.GetSafeHwnd())
		m_lblEdit.MoveWindow(M, topM, lblW, editLblH);
	if (m_edit.GetSafeHwnd())
		m_edit.MoveWindow(M, editTop, iw, editH);
	if (m_legend.GetSafeHwnd())
		m_legend.MoveWindow(M, legendTop, iw, max(48, legendH));
	if (CWnd* pRem = GetDlgItem(IDC_MPP_REMAIN))
		pRem->MoveWindow(M, remainY, iw, remainH);
	if (CWnd* pModeL = GetDlgItem(IDC_MPP_MODE_L))
		pModeL->MoveWindow(M, modeY + 4, 44, 16);
	if (m_mode.GetSafeHwnd()) {
		m_mode.MoveWindow(M + 48, modeY, max(140, min(220, iw / 2)), modeH);
		m_mode.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	if (CWnd* pProgL = GetDlgItem(IDC_MPP_PROG_L))
		pProgL->MoveWindow(M, progLblY, iw, progLabelH);
	if (m_progress.GetSafeHwnd())
		m_progress.MoveWindow(M, progY, iw, progH);
	if (CWnd* pHistL = GetDlgItem(IDC_MPP_HIST_L))
		pHistL->MoveWindow(M, histY + 3, 34, 18);
	const int saveHistW = 68;
	const int histComboW = max(120, min(220, iw - 36 - saveHistW - 8));
	if (m_hist.GetSafeHwnd()) {
		m_hist.MoveWindow(M + 36, histY, histComboW, histH);
		m_hist.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}
	if (m_saveHist.GetSafeHwnd())
		m_saveHist.MoveWindow(M + 36 + histComboW + 6, histY, saveHistW, histH);

	int bx = M;
	if (m_analyze.GetSafeHwnd()) { m_analyze.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_roll.GetSafeHwnd()) { m_roll.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_run.GetSafeHwnd()) { m_run.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_stop.GetSafeHwnd()) { m_stop.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_reset.GetSafeHwnd()) { m_reset.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_clear.GetSafeHwnd()) { m_clear.MoveWindow(bx, btnY, btnW, btnH); bx += btnW + btnGap; }
	if (m_close.GetSafeHwnd())
		m_close.MoveWindow(max(bx, W - M - btnW), btnY, btnW, btnH);

	// ボタンを最前面へ(履歴コンボと重ならないよう)
	RaiseChildZOrder(&m_lblEdit);
	RaiseChildZOrder(&m_edit);
	RaiseChildZOrder(&m_legend);
	RaiseChildZOrder(GetDlgItem(IDC_MPP_REMAIN));
	RaiseChildZOrder(GetDlgItem(IDC_MPP_MODE_L));
	RaiseChildZOrder(&m_mode);
	RaiseChildZOrder(GetDlgItem(IDC_MPP_PROG_L));
	RaiseChildZOrder(&m_progress);
	RaiseChildZOrder(GetDlgItem(IDC_MPP_HIST_L));
	RaiseChildZOrder(&m_hist);
	RaiseChildZOrder(&m_saveHist);
	RaiseChildZOrder(&m_analyze);
	RaiseChildZOrder(&m_roll);
	RaiseChildZOrder(&m_run);
	RaiseChildZOrder(&m_stop);
	RaiseChildZOrder(&m_reset);
	RaiseChildZOrder(&m_clear);
	RaiseChildZOrder(&m_close);
	// Raise 後に帯ボタンが下に沈むので載せ直す
	CCC_CaptionLayout(m_hWnd);
	CCC_MainLockBringToFront(m_hWnd);
}

void CPromptDlg::RefreshAfterLayout(BOOL bSyncRedraw)
{
	if (!::IsWindow(GetSafeHwnd())) return;

#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11())
		CCC_GroupBoxesBack(m_hWnd);
#endif

	m_run.RepaintClient();
	m_analyze.RepaintClient();
	m_roll.RepaintClient();
	m_stop.RepaintClient();
	m_reset.RepaintClient();
	m_clear.RepaintClient();
	m_close.RepaintClient();

	if (m_lblEdit.GetSafeHwnd())
		m_lblEdit.Invalidate(TRUE);
	if (m_edit.GetSafeHwnd())
		m_edit.Invalidate(TRUE);
	if (m_legend.GetSafeHwnd())
		m_legend.Invalidate(TRUE);
	if (CWnd* pRem = GetDlgItem(IDC_MPP_REMAIN))
		pRem->Invalidate(TRUE);

#if CCUSTOM_AERO_SUPPORT
	if (CCC_IsAeroEnabled() && CCC_IsWin11())
		CCC_RefreshKids(m_hWnd);
#endif

	UINT rdw = RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME;
	if (bSyncRedraw)
		rdw |= RDW_UPDATENOW;
	RedrawWindow(NULL, NULL, rdw);
}

void CPromptDlg::RefreshOpaqueFixers(BOOL bSync)
{
	if (!GetSafeHwnd()) return;
#if CCUSTOM_AERO_SUPPORT
	if (bSync)
		SendMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
	else
		PostMessage(CCC_MSG_REAPPLY_OPAQUE_FIXERS, 0, 0);
#else
	UNREFERENCED_PARAMETER(bSync);
#endif
}

void CPromptDlg::SyncLayoutAndPaint(BOOL bSyncRedraw, BOOL bReapplyOpaqueFixers)
{
	LayoutControls();
	RefreshAfterLayout(bSyncRedraw);
	if (bReapplyOpaqueFixers)
		RefreshOpaqueFixers(bSyncRedraw);
}

void CPromptDlg::SavePosToSavedata()
{
	if (!::IsWindow(GetSafeHwnd()) || IsIconic()) return;
	CRect r;
	GetWindowRect(&r);
	savedata.mpPromptX = r.left;
	savedata.mpPromptY = r.top;
	savedata.mpPromptW = r.Width();
	savedata.mpPromptH = r.Height();
	savedata.mpPromptHasPos = 1;
}

void CPromptDlg::RestorePosFromSavedata()
{
	int x = savedata.mpPromptX, y = savedata.mpPromptY;
	int w = savedata.mpPromptW, h = savedata.mpPromptH;
	if (!savedata.mpPromptHasPos || w < 280 || h < 340 || w > 10000 || h > 10000) {
		w = 375;
		h = 368;
		if (GetParent() && ::IsWindow(GetParent()->GetSafeHwnd())) {
			CRect pr;
			GetParent()->GetWindowRect(&pr);
			x = pr.left + 40;
			y = pr.top + 40;
		}
		else {
			x = 100;
			y = 100;
		}
	}
	RECT rcWork{};
	SystemParametersInfo(SPI_GETWORKAREA, 0, &rcWork, 0);
	if (x < rcWork.left - 50 || x > rcWork.right - 50) x = rcWork.left + 40;
	if (y < rcWork.top - 10 || y > rcWork.bottom - 50) y = rcWork.top + 40;
	MoveWindow(x, y, w, h);
	m_posRestored = TRUE;
}

static void ScrollMultilineEdit(CCustomEdit& edit, short zDelta)
{
	if (!edit.GetSafeHwnd()) return;
	const int lines = (zDelta > 0) ? -3 : 3;
	edit.LineScroll(lines);
	edit.RepaintClient();
}

BOOL CPromptDlg::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();
	RestorePosFromSavedata();

	SetWindowText(LL14(L"プロンプト", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"프롬프트", L"提示", L"موجه", L"Промпт", L"Prompt", L"Prompt", L"Prompt", L"Prompt", L"Istem"));
	ModifyStyle(WS_MINIMIZEBOX, 0);
	SetIcon(nullptr, TRUE);
	SetIcon(nullptr, FALSE);
	// キャプションアイコンは付けない。Aero 有効時も WS_EX_DLGMODALFRAME を
	// 立てないと既定アイコンが残る（イコライザーは rc の DS_MODALFRAME で消えている）。
	ModifyStyleEx(0, WS_EX_DLGMODALFRAME, SWP_FRAMECHANGED);
	m_legend.SetWindowText(MpPromptLegendText());
	if (m_edit.GetSafeHwnd())
		m_edit.SetLimitText((UINT)kMaxChars);
	SetDlgItemText(IDC_MPP_ANALYZE, LL14(L"解析", L"Analyze", L"Analyser", L"Analizza", L"Analizar", L"분석", L"分析", L"تحليل", L"Анализ", L"Analysieren", L"Analisar", L"Analyseren", L"Analizuj", L"Analiz"));
	SetDlgItemText(IDC_MPP_ROLL, LL14(L"ロール", L"Roll", L"Rouleau", L"Roll", L"Roll", L"롤", L"卷轴", L"Roll", L"Roll", L"Roll", L"Roll", L"Roll", L"Roll", L"Rulo"));
	SetDlgItemText(IDC_MPP_RUN, LL14(L"実行", L"Run", L"Executer", L"Esegui", L"Ejecutar", L"실행", L"执行", L"تشغيل", L"Запуск", L"Ausfuehren", L"Executar", L"Uitvoeren", L"Uruchom", L"Calistir"));
	SetDlgItemText(IDC_MPP_STOP, LL14(L"停止", L"Stop", L"Arret", L"Stop", L"Detener", L"중지", L"停止", L"إيقاف", L"Стоп", L"Stopp", L"Parar", L"Stoppen", L"Stop", L"Durdur"));
	SetDlgItemText(IDC_MPP_RESET, LL14(L"リセット", L"Reset", L"Reinit.", L"Ripristina", L"Restablecer", L"리셋", L"重置", L"إعادة ضبط", L"Сброс", L"Zuruecksetzen", L"Redefinir", L"Reset", L"Reset", L"Sifirla"));
	SetDlgItemText(IDC_MPP_CLEAR, LL14(L"クリア", L"Clear", L"Effacer", L"Cancella", L"Borrar", L"지우기", L"清除", L"مسح", L"Очистить", L"Leeren", L"Limpar", L"Wissen", L"Wyczysc", L"Temizle"));
	SetDlgItemText(IDC_MPP_CLOSE, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schliessen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	SetDlgItemText(IDC_MPP_HIST_L, LL14(L"履歴:", L"History:", L"Historique:", L"Cronologia:", L"Historial:", L"기록:", L"历史:", L"السجل:", L"История:", L"Verlauf:", L"Historico:", L"Geschiedenis:", L"Historia:", L"Gecmis:"));
	SetDlgItemText(IDC_MPP_SAVEHIST, LL14(L"履歴保存", L"Save history", L"Enregistrer", L"Salva", L"Guardar", L"기록 저장", L"保存历史", L"حفظ", L"Сохранить", L"Speichern", L"Salvar", L"Opslaan", L"Zapisz", L"Kaydet"));

	SetDlgItemText(IDC_MPP_MODE_L, LL14(L"モード:", L"Mode:", L"Mode:", L"Modalita:", L"Modo:", L"모드:", L"模式:", L"الوضع:", L"Режим:", L"Modus:", L"Modo:", L"Modus:", L"Tryb:", L"Mod:"));
	FillModeCombo();
	if (CWnd* pPh = GetDlgItem(IDC_MPP_PROGRESS)) {
		CRect rc; pPh->GetWindowRect(&rc); ScreenToClient(&rc);
		pPh->DestroyWindow();
		m_progress.Create(WS_CHILD | WS_VISIBLE, rc, this, IDC_MPP_PROGRESS);
		m_progress.SetRange(0, 100);
		m_progress.SetPos(0);
		m_progress.SetShowPercent(TRUE);
		m_progress.SetColors(RGB(255, 236, 246), RGB(255, 170, 200), RGB(200, 120, 220));
		m_progress.SetAeroMode(CCC_IsAeroEnabled());
	}
	if (CWnd* pProgL = GetDlgItem(IDC_MPP_PROG_L))
		pProgL->SetWindowText(LL14(L"解析の進捗", L"Analyze progress", L"Progression", L"Avanzamento", L"Progreso", L"분석 진행", L"分析进度", L"Progress", L"Прогресс", L"Fortschritt", L"Progresso", L"Voortgang", L"Postep", L"Ilerleme"));

	if (m_legend.GetSafeHwnd()) {
		m_legend.SetReadOnly(TRUE);
		m_legend.ModifyStyle(WS_BORDER | WS_TABSTOP, 0);
		m_legend.ModifyStyle(0, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL);
		LOGFONT lf{};
		CFont* pDef = GetFont();
		if (pDef && pDef->GetLogFont(&lf)) {
			lf.lfHeight = (lf.lfHeight < 0) ? (lf.lfHeight * 92 / 100) : -(abs(lf.lfHeight) * 92 / 100);
			if (m_fontLegend.GetSafeHandle()) m_fontLegend.DeleteObject();
			if (m_fontLegend.CreateFontIndirect(&lf))
				m_legend.SetFont(&m_fontLegend);
		}
	}
	{
		LOGFONT lf{};
		CFont* pDef = GetFont();
		if (pDef && pDef->GetLogFont(&lf)) {
			lf.lfHeight = (lf.lfHeight < 0) ? (lf.lfHeight * 11 / 10) : -(abs(lf.lfHeight) * 11 / 10);
			if (m_fontBtn.GetSafeHandle()) m_fontBtn.DeleteObject();
			if (m_fontBtn.CreateFontIndirect(&lf))
			{
				m_analyze.SetFont(&m_fontBtn);
				m_roll.SetFont(&m_fontBtn);
				m_run.SetFont(&m_fontBtn);
				m_stop.SetFont(&m_fontBtn);
				m_reset.SetFont(&m_fontBtn);
				m_clear.SetFont(&m_fontBtn);
				m_close.SetFont(&m_fontBtn);
				m_saveHist.SetFont(&m_fontBtn);
			}
		}
	}

#if CCUSTOM_AERO_SUPPORT
	const BOOL bAero = CCC_IsAeroEnabled();
#else
	const BOOL bAero = FALSE;
#endif
	if (!m_lblEdit.GetSafeHwnd()) {
		m_lblEdit.Create(_T(""), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
			CRect(0, 0, 1, 1), this, IDC_MPP_EDIT_L);
		LOGFONT lfLbl{};
		CFont* pDef = GetFont();
		if (pDef && pDef->GetLogFont(&lfLbl)) {
			lfLbl.lfWeight = FW_SEMIBOLD;
			if (m_fontEditLbl.GetSafeHandle()) m_fontEditLbl.DeleteObject();
			if (m_fontEditLbl.CreateFontIndirect(&lfLbl))
				MpInitPromptStatic(m_lblEdit, &m_fontEditLbl, bAero);
			else
				MpInitPromptStatic(m_lblEdit, pDef, bAero);
		}
		else {
			MpInitPromptStatic(m_lblEdit, pDef, bAero);
		}
		MpSetPromptLabelText(m_lblEdit, MpPromptEditLabelText());
	}

	StyleButtons();
	SetupTooltips();
	LoadTextFromSavedata();
	ReloadHistoryCombo();
	UpdateRemainLabel();
	EnableMainWindowLock(&savedata.mpPromptMainLock);
	SyncLayoutAndPaint(TRUE, TRUE);
	return TRUE;
}

void CPromptDlg::OnGetMinMaxInfo(MINMAXINFO* lpMMI)
{
	if (lpMMI) {
		lpMMI->ptMinTrackSize.x = 315;
		lpMMI->ptMinTrackSize.y = 340;
	}
	CCustomBlurDialogExBase::OnGetMinMaxInfo(lpMMI);
}

void CPromptDlg::OnEnterSizeMove()
{
	m_inSizeMove = TRUE;
	Default();
}

void CPromptDlg::OnExitSizeMove()
{
	m_inSizeMove = FALSE;
	if (::IsWindow(m_hWnd) && !IsIconic()) {
		LayoutControls();
		RedrawWindow(NULL, NULL,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		RefreshOpaqueFixers(FALSE);
		if (m_posRestored)
			SavePosToSavedata();
	}
	Default();
}

void CPromptDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
	CCustomBlurDialogExBase::OnMoving(fwSide, pRect);
	if (m_posRestored)
		SavePosToSavedata();
}

void CPromptDlg::OnSize(UINT nType, int cx, int cy)
{
	CCustomBlurDialogExBase::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED)
		return;
	LayoutControls();
	if (m_inSizeMove) {
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
	}
	else {
		RedrawWindow(NULL, NULL,
			RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
		RefreshOpaqueFixers(FALSE);
	}
	if (m_posRestored)
		SavePosToSavedata();
}

static BOOL EditHitTestWheelPoint(const CCustomEdit& edit, CPoint screenPt)
{
	if (!edit.GetSafeHwnd()) return FALSE;
	CRect rc;
	edit.GetWindowRect(&rc);
	return rc.PtInRect(screenPt);
}

BOOL CPromptDlg::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
	if (EditHitTestWheelPoint(m_edit, pt)) {
		ScrollMultilineEdit(m_edit, zDelta);
		return TRUE;
	}
	if (EditHitTestWheelPoint(m_legend, pt)) {
		ScrollMultilineEdit(m_legend, zDelta);
		return TRUE;
	}
	return CCustomBlurDialogExBase::OnMouseWheel(nFlags, zDelta, pt);
}

BOOL CPromptDlg::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message == WM_MOUSEWHEEL) {
		CPoint pt(GET_X_LPARAM(pMsg->lParam), GET_Y_LPARAM(pMsg->lParam));
		const short z = (short)HIWORD(pMsg->wParam);
		if (EditHitTestWheelPoint(m_edit, pt)) {
			ScrollMultilineEdit(m_edit, z);
			return TRUE;
		}
		if (EditHitTestWheelPoint(m_legend, pt)) {
			ScrollMultilineEdit(m_legend, z);
			return TRUE;
		}
	}
	if (m_tooltip.GetSafeHwnd())
		m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

#if CCUSTOM_AERO_SUPPORT
LRESULT CPromptDlg::OnReapplyOpaqueFixers(WPARAM wParam, LPARAM lParam)
{
	return CCustomBlurDialogExBase::OnReapplyOpaqueFixers(wParam, lParam);
}
#endif

void CPromptDlg::LoadTextFromSavedata()
{
	if (savedata.mpPromptTextLong[0])
		SetDlgItemText(IDC_MPP_TEXT, savedata.mpPromptTextLong);
	else if (savedata.mpPromptText[0])
		SetDlgItemText(IDC_MPP_TEXT, savedata.mpPromptText);
	// 起動時など、ロールが既に開いていれば本文を反映
	const CString s = GetPromptText();
	if (!s.IsEmpty())
		MpCommandRollNotifyText(s, m_syncGen);
}

void CPromptDlg::SaveTextToSavedata()
{
	CString s;
	GetDlgItemText(IDC_MPP_TEXT, s);
	if (s.GetLength() > kMaxChars)
		s = s.Left(kMaxChars);
	_tcsncpy(savedata.mpPromptTextLong, s, _countof(savedata.mpPromptTextLong) - 1);
	savedata.mpPromptTextLong[_countof(savedata.mpPromptTextLong) - 1] = 0;
	// 互換: 先頭2000文字を旧バッファにも残す
	_tcsncpy(savedata.mpPromptText, s, _countof(savedata.mpPromptText) - 1);
	savedata.mpPromptText[_countof(savedata.mpPromptText) - 1] = 0;
}

void CPromptDlg::UpdateRemainLabel()
{
	CString s;
	GetDlgItemText(IDC_MPP_TEXT, s);
	int remain = kMaxChars - s.GetLength();
	if (remain < 0) remain = 0;
	CString lbl;
	lbl.Format(LL14(L"残り %d 文字", L"%d characters left", L"%d caracteres restants", L"%d caratteri rimasti", L"%d caracteres restantes", L"%d자 남음", L"剩余 %d 字", L"%d حرف متبقٍ", L"Осталось %d симв.", L"Noch %d Zeichen", L"%d caracteres restantes", L"%d tekens over", L"Pozostalo %d znakow", L"%d karakter kaldi"), remain);
	SetDlgItemText(IDC_MPP_REMAIN, lbl);
}

void CPromptDlg::OnTextChanged()
{
	CString s;
	GetDlgItemText(IDC_MPP_TEXT, s);
	if (s.GetLength() > kMaxChars) {
		s = s.Left(kMaxChars);
		m_edit.SetWindowText(s);
		m_edit.SetSel(s.GetLength(), s.GetLength());
	}
	UpdateRemainLabel();
	SaveTextToSavedata();
	if (!m_applyingFromRoll) {
		++m_syncGen;
		MpCommandRollNotifyText(s, m_syncGen);
	}
}

CString CPromptDlg::GetPromptText() const
{
	CString s;
	if (m_edit.GetSafeHwnd())
		m_edit.GetWindowText(s);
	if (s.IsEmpty() && GetSafeHwnd())
		GetDlgItemText(IDC_MPP_TEXT, s);
	return s;
}

void CPromptDlg::ApplyTextFromRoll(const CString& text, UINT syncGen)
{
	m_applyingFromRoll = TRUE;
	m_syncGen = syncGen;
	CString t = text;
	if (t.GetLength() > kMaxChars) t = t.Left(kMaxChars);
	SetDlgItemText(IDC_MPP_TEXT, t);
	UpdateRemainLabel();
	SaveTextToSavedata();
	m_applyingFromRoll = FALSE;
}

void CPromptDlg::OnRoll()
{
	MpShowCommandRollDialog(CCC_GetActiveMainWindow());
}

void CPromptDlg::OnContextMenu(CWnd* pWnd, CPoint point)
{
	if (!m_edit.GetSafeHwnd()) return;
	CWnd* pFocus = GetFocus();
	const BOOL onEdit = (pWnd == &m_edit) || (pFocus == &m_edit);
	if (!onEdit && pWnd != this) return;

	CPoint pt = point;
	if (pt.x == -1 && pt.y == -1) {
		CRect rc;
		m_edit.GetWindowRect(&rc);
		pt = rc.CenterPoint();
	}

	enum {
		IDM_CUT = 1, IDM_COPY, IDM_PASTE, IDM_SELALL,
		IDM_INS_AT, IDM_INS_PCT, IDM_INS_SB, IDM_INS_WM, IDM_INS_PW, IDM_INS_DR,
		IDM_SAVEHIST, IDM_CLEAR
	};
	CMenu menu;
	if (!menu.CreatePopupMenu()) return;
	menu.AppendMenu(MF_STRING, IDM_CUT, LL14(L"切り取り", L"Cut", L"Couper", L"Taglia", L"Cortar", L"잘라내기", L"剪切", L"Cut", L"Вырезать", L"Ausschneiden", L"Recortar", L"Knippen", L"Wytnij", L"Kes"));
	menu.AppendMenu(MF_STRING, IDM_COPY, LL14(L"コピー", L"Copy", L"Copier", L"Copia", L"Copiar", L"복사", L"复制", L"Copy", L"Копировать", L"Kopieren", L"Copiar", L"Kopieren", L"Kopiuj", L"Kopyala"));
	menu.AppendMenu(MF_STRING, IDM_PASTE, LL14(L"貼り付け", L"Paste", L"Coller", L"Incolla", L"Pegar", L"붙여넣기", L"粘贴", L"Paste", L"Вставить", L"Einfuegen", L"Colar", L"Plakken", L"Wklej", L"Yapistir"));
	menu.AppendMenu(MF_STRING, IDM_SELALL, LL14(L"すべて選択", L"Select All", L"Tout selectionner", L"Seleziona tutto", L"Seleccionar todo", L"모두 선택", L"全选", L"Select All", L"Выделить всё", L"Alles auswaehlen", L"Selecionar tudo", L"Alles selecteren", L"Zaznacz wszystko", L"Tumunu sec"));
	menu.AppendMenu(MF_SEPARATOR);
	CMenu sub;
	sub.CreatePopupMenu();
	sub.AppendMenu(MF_STRING, IDM_INS_AT, L"@p0-30[100-110]");
	sub.AppendMenu(MF_STRING, IDM_INS_PCT, L"%N1:00<20-40>[100-120]");
	sub.AppendMenu(MF_STRING, IDM_INS_SB, L"@sb1:00");
	sub.AppendMenu(MF_STRING, IDM_INS_WM, L"@wm1:00");
	sub.AppendMenu(MF_STRING, IDM_INS_PW, L"@pw1:30");
	sub.AppendMenu(MF_STRING, IDM_INS_DR, L"@dr2:00");
	menu.AppendMenu(MF_POPUP, (UINT_PTR)sub.Detach(),
		LL14(L"サンプル挿入", L"Insert sample", L"Inserer exemple", L"Inserisci esempio", L"Insertar ejemplo", L"샘플 삽입", L"插入示例", L"Insert sample", L"Вставить пример", L"Beispiel einfuegen", L"Inserir exemplo", L"Voorbeeld invoegen", L"Wstaw przyklad", L"Ornek ekle"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, IDM_SAVEHIST, LL14(L"履歴へ保存", L"Save to history", L"Enregistrer historique", L"Salva in cronologia", L"Guardar en historial", L"기록에 저장", L"保存到历史", L"Save history", L"В историю", L"In Verlauf", L"Salvar historico", L"Naar geschiedenis", L"Do historii", L"Gecmise kaydet"));
	menu.AppendMenu(MF_STRING, IDM_CLEAR, LL14(L"クリア", L"Clear", L"Effacer", L"Cancella", L"Borrar", L"지우기", L"清除", L"Clear", L"Очистить", L"Loeschen", L"Limpar", L"Wissen", L"Wyczysc", L"Temizle"));

	const int cmd = (int)menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, this);
	if (cmd <= 0) return;
	switch (cmd) {
	case IDM_CUT: m_edit.Cut(); break;
	case IDM_COPY: m_edit.Copy(); break;
	case IDM_PASTE: m_edit.Paste(); break;
	case IDM_SELALL: m_edit.SetSel(0, -1); break;
	case IDM_INS_AT:
	case IDM_INS_PCT:
	case IDM_INS_SB:
	case IDM_INS_WM:
	case IDM_INS_PW:
	case IDM_INS_DR: {
		LPCTSTR ins = L"@sb1:00 ";
		if (cmd == IDM_INS_AT) ins = L"@p0-30[100-110] ";
		else if (cmd == IDM_INS_PCT) ins = L"%N1:00<20-40>[100-120] ";
		else if (cmd == IDM_INS_WM) ins = L"@wm1:00 ";
		else if (cmd == IDM_INS_PW) ins = L"@pw1:30 ";
		else if (cmd == IDM_INS_DR) ins = L"@dr2:00 ";
		m_edit.ReplaceSel(ins, TRUE);
		UpdateRemainLabel();
		SaveTextToSavedata();
		break;
	}
	case IDM_SAVEHIST: OnSaveHist(); break;
	case IDM_CLEAR: OnClear(); break;
	default: break;
	}
}

void CPromptDlg::OnRun()
{
	CString text, err;
	GetDlgItemText(IDC_MPP_TEXT, text);
	SaveTextToSavedata();
	if (!MpPromptExecute(text, &err)) {
		AfxMessageBox(err.IsEmpty()
			? LL14(L"プロンプトの解析に失敗しました。", L"Failed to parse prompt.", L"Echec analyse prompt.", L"Analisi prompt fallita.", L"Error al analizar prompt.", L"프롬프트 해석 실패.", L"提示解析失败。", L"فشل تحليل الموجه.", L"Ошибка разбора промпта.", L"Prompt parsen fehlgeschlagen.", L"Falha ao analisar prompt.", L"Prompt parseren mislukt.", L"Blad parsowania promptu.", L"Istem ayrıştırılamadı.")
			: err);
		return;
	}
}

void CPromptDlg::FillModeCombo()
{
	if (!m_mode.GetSafeHwnd()) return;
	m_mode.ResetContent();
	for (int i = 0; i < MP_ANA_MODE_COUNT; ++i)
		m_mode.AddString(MpPromptAnalyzeModeName(i));
	int cur = MpPromptAnalyzeModeClamp(savedata.mpPromptAnalyzeMode);
	m_mode.SetCurSel(cur);
}

int CPromptDlg::GetSelectedAnalyzeMode() const
{
	if (!m_mode.GetSafeHwnd()) return MpPromptAnalyzeModeClamp(savedata.mpPromptAnalyzeMode);
	const int sel = m_mode.GetCurSel();
	return MpPromptAnalyzeModeClamp(sel);
}

void CPromptDlg::OnModeSel()
{
	savedata.mpPromptAnalyzeMode = GetSelectedAnalyzeMode();
	MpPersistSavedataQuick();
}

void CPromptDlg::SetAnalyzeUiBusy(BOOL busy)
{
	m_analyzing = busy;
	if (m_analyze.GetSafeHwnd()) m_analyze.EnableWindow(!busy);
	if (m_roll.GetSafeHwnd()) m_roll.EnableWindow(!busy);
	if (m_mode.GetSafeHwnd()) m_mode.EnableWindow(!busy);
	if (m_run.GetSafeHwnd()) m_run.EnableWindow(!busy);
	if (m_progress.GetSafeHwnd()) {
		m_progress.ShowWindow(SW_SHOW);
		if (!busy) {
			// keep last percent briefly
		}
	}
}

void CPromptDlg::AnalyzeProgressThunk(int percent, LPCTSTR status, void* user)
{
	CPromptDlg* self = reinterpret_cast<CPromptDlg*>(user);
	if (!self || !::IsWindow(self->GetSafeHwnd())) return;
	if (self->m_progress.GetSafeHwnd())
		self->m_progress.SetPos(percent);
	if (CWnd* p = self->GetDlgItem(IDC_MPP_PROG_L)) {
		CString s;
		if (status && status[0])
			s.Format(L"%s  (%d%%)", status, percent);
		else
			s.Format(L"%d%%", percent);
		p->SetWindowText(s);
	}
	// export 経路の DoEvent と合わせて描画を進める
	MSG msg;
	while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		if (!self->IsDialogMessage(&msg)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
		}
	}
}

void CPromptDlg::OnAnalyze()
{
	if (m_analyzing) return;

	CString cur;
	GetDlgItemText(IDC_MPP_TEXT, cur);
	cur.Trim();
	const BOOL hasText = !cur.IsEmpty();
	CString ask = hasText
		? LL14(L"選択中の曲を読込しながら解析します。\r\n再生中の曲は一時停止されます。\r\n入力欄の内容は解析結果で上書きされます。よろしいですか？",
			L"Analyze the selected track while loading.\r\nCurrent playback will pause.\r\nThe input text will be replaced by the result. Continue?",
			L"Analyser la piste.\r\nLecture interrompue.\r\nLe texte sera remplace. Continuer ?",
			L"Analizzare la traccia.\r\nRiproduzione interrotta.\r\nIl testo sara sostituito. Continuare?",
			L"Analizar la pista.\r\nLa reproduccion se pausara.\r\nEl texto se reemplazara. Continuar?",
			L"선택 곡을 읽어 분석합니다.\r\n재생은 일시중단됩니다.\r\n입력란은 결과로 덮어씁니다. 계속할까요?",
			L"将读取并分析所选曲目。\r\n播放会暂停。\r\n输入内容将被结果覆盖。是否继续？",
			L"Analyze selected track. Playback pauses. Text will be replaced. Continue?",
			L"Анализ трека. Воспроизведение прервётся. Текст будет заменён. Продолжить?",
			L"Titel analysieren.\r\nWiedergabe pausiert.\r\nText wird ersetzt. Fortfahren?",
			L"Analisar a faixa.\r\nReproducao pausada.\r\nO texto sera substituido. Continuar?",
			L"Track analyseren.\r\nAfspelen pauzeert.\r\nTekst wordt vervangen. Doorgaan?",
			L"Analiza utworu.\r\nOdtwarzanie wstrzymane.\r\nTekst zostanie zastapiony. Kontynuowac?",
			L"Parcayi analiz et.\r\nCalma duraklar.\r\nMetin degistirilir. Devam?")
		: LL14(L"選択中の曲を読込しながら解析します。\r\n再生中の曲は一時停止されます。よろしいですか？",
			L"Analyze the selected track while loading.\r\nCurrent playback will pause. Continue?",
			L"Analyser la piste selectionnee.\r\nLa lecture en cours sera interrompue. Continuer ?",
			L"Analizzare la traccia selezionata.\r\nLa riproduzione verra interrotta. Continuare?",
			L"Analizar la pista seleccionada.\r\nLa reproduccion se pausara. Continuar?",
			L"선택 곡을 읽어 분석합니다.\r\n재생 중인 곡은 일시중단됩니다. 계속할까요?",
			L"将读取并分析所选曲目。\r\n当前播放会暂停。是否继续？",
			L"Analyze selected track. Playback will pause. Continue?",
			L"Анализ выбранного трека. Воспроизведение будет прервано. Продолжить?",
			L"Gewaehlten Titel analysieren.\r\nWiedergabe wird pausiert. Fortfahren?",
			L"Analisar a faixa selecionada.\r\nA reproducao sera pausada. Continuar?",
			L"Geselecteerde track analyseren.\r\nAfspelen wordt gepauzeerd. Doorgaan?",
			L"Analiza wybranego utworu.\r\nOdtwarzanie zostanie wstrzymane. Kontynuowac?",
			L"Secili parcayi analiz et.\r\nCalma duraklar. Devam?");
	if (AfxMessageBox(ask, MB_YESNO | MB_ICONQUESTION) != IDYES)
		return;

	const int mode = GetSelectedAnalyzeMode();
	savedata.mpPromptAnalyzeMode = mode;
	MpPersistSavedataQuick();

	SetAnalyzeUiBusy(TRUE);
	if (m_progress.GetSafeHwnd()) {
		m_progress.SetPos(0);
		m_progress.ShowWindow(SW_SHOW);
	}
	MpPromptAnalyzeSetProgressCb(&CPromptDlg::AnalyzeProgressThunk, this);

	CString text, err;
	const BOOL ok = MpPromptAnalyzeSelected(text, mode, &err);

	MpPromptAnalyzeSetProgressCb(nullptr, nullptr);
	SetAnalyzeUiBusy(FALSE);

	if (!ok) {
		AfxMessageBox(err.IsEmpty()
			? LL14(L"解析に失敗しました。", L"Analysis failed.", L"Echec analyse.", L"Analisi fallita.", L"Error de analisis.", L"분석 실패.", L"分析失败。", L"فشل التحليل.", L"Ошибка анализа.", L"Analyse fehlgeschlagen.", L"Falha na analise.", L"Analyse mislukt.", L"Blad analizy.", L"Analiz basarisiz.")
			: err);
		return;
	}
	if (text.GetLength() > kMaxChars)
		text = text.Left(kMaxChars);
	SetDlgItemText(IDC_MPP_TEXT, text);
	UpdateRemainLabel();
	SaveTextToSavedata();
	++m_syncGen;
	MpCommandRollNotifyText(text, m_syncGen);

	int nAt = 0, nPct = 0;
	for (int i = 0; i < text.GetLength(); ++i) {
		if (text[i] == '@') ++nAt;
		else if (text[i] == '%') ++nPct;
	}
	CString done;
	done.Format(LL14(L"解析完了: @%d / %%%d コマンド", L"Done: @%d / %%%d commands", L"Termine: @%d / %%%d", L"Fatto: @%d / %%%d", L"Hecho: @%d / %%%d", L"완료: @%d / %%%d", L"完成: @%d / %%%d", L"Done: @%d / %%%d", L"Готово: @%d / %%%d", L"Fertig: @%d / %%%d", L"Concluido: @%d / %%%d", L"Klaar: @%d / %%%d", L"Gotowe: @%d / %%%d", L"Bitti: @%d / %%%d"),
		nAt, nPct);
	if (CWnd* pProgL = GetDlgItem(IDC_MPP_PROG_L))
		pProgL->SetWindowText(done);
	if (m_progress.GetSafeHwnd())
		m_progress.SetPos(100);
}

void CPromptDlg::OnStop()
{
	MpPromptStop();
}

void CPromptDlg::OnReset()
{
	MpPromptReset();
}

void CPromptDlg::OnClear()
{
	SaveCurrentToHistory();
	SetDlgItemText(IDC_MPP_TEXT, _T(""));
	SaveTextToSavedata();
	MpPromptClearAll();
	UpdateRemainLabel();
}

void CPromptDlg::SaveCurrentToHistory()
{
	CString s;
	GetDlgItemText(IDC_MPP_TEXT, s);
	s.Trim();
	if (!s.IsEmpty())
		MpPromptPushHistory(s);
	ReloadHistoryCombo();
}

void CPromptDlg::ReloadHistoryCombo()
{
	if (!m_hist.GetSafeHwnd()) return;
	g_histSelChanging = TRUE;
	const int prev = m_hist.GetCurSel();
	m_hist.ResetContent();
	m_hist.AddString(LL14(L"(履歴なし)", L"(No history)", L"(Aucun historique)", L"(Nessuna cronologia)",
		L"(Sin historial)", L"(기록 없음)", L"(无历史)", L"(لا سجل)", L"(Нет истории)", L"(Kein Verlauf)",
		L"(Sem historico)", L"(Geen geschiedenis)", L"(Brak historii)", L"(Gecmis yok)"));
	if (savedata.mpPromptHistCnt > 0) {
		for (int i = 0; i < savedata.mpPromptHistCnt && i < 20; ++i) {
			CString line = savedata.mpPromptHistText[i];
			line.Replace(_T("\r\n"), _T(" "));
			line.Replace(_T("\n"), _T(" "));
			if (line.GetLength() > 80)
				line = line.Left(80) + _T("...");
			CString label;
			label.Format(_T("%d: %s"), i + 1, (LPCTSTR)line);
			m_hist.AddString(label);
		}
	}
	if (prev >= 0 && prev < m_hist.GetCount())
		m_hist.SetCurSel(prev);
	else
		m_hist.SetCurSel(0);
	g_histSelChanging = FALSE;
}

void CPromptDlg::OnSaveHist()
{
	SaveCurrentToHistory();
}

void CPromptDlg::OnHistSel()
{
	if (g_histSelChanging) return;
	const int sel = m_hist.GetCurSel();
	if (sel <= 0) return;
	const int idx = sel - 1;
	if (idx < 0 || idx >= savedata.mpPromptHistCnt || idx >= 20) return;
	g_histSelChanging = TRUE;
	SetDlgItemText(IDC_MPP_TEXT, savedata.mpPromptHistText[idx]);
	g_histSelChanging = FALSE;
	SaveTextToSavedata();
	UpdateRemainLabel();
}

void CPromptDlg::OnCloseBtn()
{
	SaveTextToSavedata();
	SavePosToSavedata();
	savedata.mpPromptwindow = 0;
	DestroyWindow();
}

void CPromptDlg::OnClose()
{
	SaveTextToSavedata();
	SavePosToSavedata();
	savedata.mpPromptwindow = 0;
	DestroyWindow();
}

void CPromptDlg::PostNcDestroy()
{
	CCustomBlurDialogExBase::PostNcDestroy();
	if (g_promptDlg == this)
		g_promptDlg = nullptr;
	delete this;
}

HBRUSH CPromptDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	const UINT id = pWnd ? (UINT)pWnd->GetDlgCtrlID() : 0;
	if (nCtlColor == CTLCOLOR_DLG || nCtlColor == CTLCOLOR_STATIC
		|| (nCtlColor == CTLCOLOR_EDIT && id == IDC_MPP_LEGEND)) {
		if (!m_brDlg.GetSafeHandle())
			m_brDlg.CreateSolidBrush(RGB(240, 240, 245));
		pDC->SetBkColor(RGB(240, 240, 245));
		if (nCtlColor != CTLCOLOR_EDIT || id == IDC_MPP_LEGEND)
			pDC->SetTextColor(RGB(55, 55, 70));
		return m_brDlg;
	}
	return CCustomBlurDialogExBase::OnCtlColor(pDC, pWnd, nCtlColor);
}

void MpMakeIndependentZOrder(CWnd* w)
{
	if (!w || !::IsWindow(w->GetSafeHwnd())) return;
	// オーナー付きだと常にメインの手前に張り付くので切り離す
	::SetWindowLongPtr(w->GetSafeHwnd(), GWLP_HWNDPARENT, 0);
	w->ModifyStyleEx(WS_EX_TOPMOST, WS_EX_DLGMODALFRAME);
	w->SetIcon(nullptr, TRUE);
	w->SetIcon(nullptr, FALSE);
	::SetWindowPos(w->GetSafeHwnd(), HWND_NOTOPMOST, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

CString MpPromptSourceText()
{
	if (CPromptDlg* p = MpPromptDlgInstance()) {
		CString s = p->GetPromptText();
		if (!s.IsEmpty())
			return s;
	}
	if (savedata.mpPromptTextLong[0])
		return CString(savedata.mpPromptTextLong);
	if (savedata.mpPromptText[0])
		return CString(savedata.mpPromptText);
	return CString();
}

void MpShowPromptDialog(CWnd* pParent, BOOL bActivate)
{
	UNREFERENCED_PARAMETER(pParent);
	if (g_promptDlg && ::IsWindow(g_promptDlg->GetSafeHwnd())) {
		g_promptDlg->ShowWindow(bActivate ? SW_SHOW : SW_SHOWNOACTIVATE);
		if (bActivate)
			g_promptDlg->SetForegroundWindow();
		MpMakeIndependentZOrder(g_promptDlg);
		savedata.mpPromptwindow = 1;
		return;
	}
	if (g_promptDlg && !::IsWindow(g_promptDlg->GetSafeHwnd()))
		g_promptDlg = nullptr;
	// オーナー無しで生成 → メインをクリックしたとき手前に出せる
	CPromptDlg* dlg = new CPromptDlg(nullptr);
	if (!dlg->Create(IDD_MP_PROMPT, nullptr)) {
		delete dlg;
		return;
	}
	MpMakeIndependentZOrder(dlg);
	dlg->ShowWindow(bActivate ? SW_SHOW : SW_SHOWNOACTIVATE);
	if (bActivate)
		dlg->SetForegroundWindow();
	g_promptDlg = dlg;
	savedata.mpPromptwindow = 1;
}

void MpTogglePromptDialog(CWnd* pParent)
{
	if (g_promptDlg && ::IsWindow(g_promptDlg->GetSafeHwnd())) {
		g_promptDlg->SendMessage(WM_CLOSE);
		return;
	}
	MpShowPromptDialog(pParent, TRUE);
}

BOOL MpIsPromptOpen()
{
	return (g_promptDlg && ::IsWindow(g_promptDlg->GetSafeHwnd())) ? TRUE : FALSE;
}

CPromptDlg* MpPromptDlgInstance()
{
	if (g_promptDlg && ::IsWindow(g_promptDlg->GetSafeHwnd()))
		return g_promptDlg;
	return nullptr;
}

