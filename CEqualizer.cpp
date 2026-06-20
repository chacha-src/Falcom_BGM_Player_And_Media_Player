// CEqualizer.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "afxdialogex.h"
#include "CEqualizer.h"


// CEqualizer ダイアログ

IMPLEMENT_DYNAMIC(CEqualizer, CCustomBlurDialogExBase)

CEqualizer::CEqualizer(CWnd* pParent /*=nullptr*/)
	: CCustomBlurDialogExBase(IDD_EQUALIZER, pParent)
{

}

CEqualizer::~CEqualizer()
{
}

void CEqualizer::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogExBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SLIDER7, m_s0);
	DDX_Control(pDX, IDC_SLIDER9, m_s1);
	DDX_Control(pDX, IDC_SLIDER8, m_s2);
	DDX_Control(pDX, IDC_SLIDER10, m_s3);
	DDX_Control(pDX, IDC_SLIDER11, m_s4);
	DDX_Control(pDX, IDC_SLIDER12, m_s5);
	DDX_Control(pDX, IDC_SLIDER13, m_s6);
	DDX_Control(pDX, IDC_SLIDER14, m_s7);
	DDX_Control(pDX, IDC_SLIDER15, m_s8);
	DDX_Control(pDX, IDC_SLIDER16, m_s9);
	DDX_Control(pDX, IDC_STATIC_e0, m_v0);
	DDX_Control(pDX, IDC_STATIC_e1, m_v1);
	DDX_Control(pDX, IDC_STATIC_e2, m_v2);
	DDX_Control(pDX, IDC_STATIC_e3, m_v3);
	DDX_Control(pDX, IDC_STATIC_e4, m_v4);
	DDX_Control(pDX, IDC_STATIC_e5, m_v5);
	DDX_Control(pDX, IDC_STATIC_e6, m_v6);
	DDX_Control(pDX, IDC_STATIC_e7, m_v7);
	DDX_Control(pDX, IDC_STATIC_e8, m_v8);
	DDX_Control(pDX, IDC_STATIC_e9, m_v9);
	DDX_Control(pDX, IDC_COMBO1, m_env);
	DDX_Control(pDX, IDC_COMBO5, m_pre);
	DDX_Control(pDX, IDOK, m_ok);
	DDX_Control(pDX, IDOK3, dum);
	DDX_Control(pDX, IDC_STATIC_e10, m_v10);
	DDX_Control(pDX, IDC_STATIC_e11, m_v11);
	DDX_Control(pDX, IDC_STATIC_e12, m_v12);
	DDX_Control(pDX, IDC_STATIC_e13, m_v13);
	DDX_Control(pDX, IDC_STATIC_e14, m_v14);
	DDX_Control(pDX, IDC_SLIDER21, m_s14);
	DDX_Control(pDX, IDC_SLIDER20, m_s13);
	DDX_Control(pDX, IDC_SLIDER19, m_s12);
	DDX_Control(pDX, IDC_SLIDER18, m_s11);
	DDX_Control(pDX, IDC_SLIDER17, m_s10);
	DDX_Control(pDX, IDC_STATIC_eff, m_seff);
	DDX_Control(pDX, IDC_SLIDER22, m_eff);
	DDX_Control(pDX, IDC_SLIDER23, m_smaster);
	DDX_Control(pDX, IDC_SLIDER24, m_ssenmei);
	DDX_Control(pDX, IDC_SLIDER25, m_skoutei);
	DDX_Control(pDX, IDC_SLIDER26, m_smitsudo);
	DDX_Control(pDX, IDC_SLIDER27, m_srittai);
	DDX_Control(pDX, IDC_STATIC_e15, m_vmaster);
	DDX_Control(pDX, IDC_STATIC_e16, m_vsenmei);
	DDX_Control(pDX, IDC_STATIC_e17, m_vkoutei);
	DDX_Control(pDX, IDC_STATIC_e18, m_vmitsudo);
	DDX_Control(pDX, IDC_STATIC_e19, m_vrittai);
	DDX_Control(pDX, IDOK4, sdasdsdadsd);
	DDX_Control(pDX, IDC_STATICf, m_t);
	DDX_Control(pDX, IDC_STATIC_key, m_keyLow);
	DDX_Control(pDX, IDC_STATIC_key2, m_keyMid);
	DDX_Control(pDX, IDC_STATIC_key3, m_keyHigh);
	DDX_Control(pDX, IDC_STATIC_key4, m_keyAll);
	DDX_Control(pDX, IDC_SLIDER28, m_reverb);
	DDX_Control(pDX, IDC_SLIDER29, m_chorus);
	DDX_Control(pDX, IDC_SLIDER30, m_delay);
	DDX_Control(pDX, IDC_STATIC_e20, m_reverbi);
	DDX_Control(pDX, IDC_STATIC_e21, m_chorusi);
	DDX_Control(pDX, IDC_STATIC_e22, m_delayi);
}


BEGIN_MESSAGE_MAP(CEqualizer, CCustomBlurDialogExBase)
	ON_CBN_SELCHANGE(IDC_COMBO1, &CEqualizer::OnCbnSelchangeCombo1)
	ON_CBN_SELCHANGE(IDC_COMBO5, &CEqualizer::OnCbnSelchangeCombo5)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDOK3, &CEqualizer::OnBnClickedOk3)
	ON_BN_CLICKED(IDOK, &CEqualizer::OnBnClickedOk)
	ON_BN_CLICKED(IDOK4, &CEqualizer::OnBnClickedOk4)
END_MESSAGE_MAP()

extern save savedata;
extern int stflg;

// CEqualizer メッセージ ハンドラー
BOOL CEqualizer::OnInitDialog()
{
	CCustomBlurDialogExBase::OnInitDialog();

	SetWindowText(LL14(L"イコライザー", L"Equalizer", L"Equaliseur", L"Equalizer", L"Ecualizador", L"이퀄라이저", L"均衡器", L"المعادل", L"Эквалайзер", L"Equalizer", L"Equalizador", L"Equalizer", L"Korektor", L"Ekolayzer"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	SetDlgItemText(IDOK3, LL14(L"イコライザーリセット", L"Equalizer reset", L"Réinitialiser égaliseur", L"Reset equalizer", L"Restablecer ecualizador", L"이퀄라이저 초기화", L"均衡器重置", L"إعادة تعيين المعادل", L"Сброс эквалайзера", L"Equalizer zurücksetzen", L"Reset equalizador", L"Equalizer resetten", L"Reset korektora", L"Ekolayzeri sıfırla"));
	SetDlgItemText(IDOK4, LL14(L"グローバルリセット", L"Global reset", L"Réinitialisation globale", L"Reset globale", L"Restablecer global", L"전역 초기화", L"全局重置", L"إعادة تعيين عامة", L"Глобальный сброс", L"Global zurücksetzen", L"Reset global", L"Globaal resetten", L"Reset globalny", L"Genel sıfırlama"));
	SetDlgItemText(IDC_STATIC_EQ_DRY, LL14(L"環境", L"Environment", L"Environnement", L"Ambiente", L"Entorno", L"환경", L"环境", L"البيئة", L"Среда", L"Umgebung", L"Ambiente", L"Omgeving", L"Środowisko", L"Ortam"));
	SetDlgItemText(IDC_STATIC_EQ_WET, LL14(L"プリセット", L"Wet", L"Mouillé", L"Bagnato", L"Húmedo", L"습함", L"湿", L"رطب", L"Мокрый", L"Wet", L"Molhado", L"Nat", L"Mokry", L"Islak"));
	SetDlgItemText(IDC_STATIC_EQ_ACOUSTIC, LL14(L"環境のかかり具合", L"Acoustic space model", L"Modèle d'espace acoustique", L"Modello spazio acustico", L"Modelo espacio acústico", L"음향 공간 모델", L"声学空间模型", L"نموذج الفضاء الصوتي", L"Модель акустического пространства", L"Akustisches Raummodell", L"Modelo espaço acústico", L"Akoestisch ruimtemodel", L"Model przestrzeni akustycznej", L"Akustik alan modeli"));
	SetDlgItemText(IDC_STATIC_EQ_SPECTRUM, LL14(L"マスター", L"Spectrum", L"Spectre", L"Spettro", L"Espectro", L"스펙트럼", L"频谱", L"الطيف", L"Спектр", L"Spektrum", L"Espectro", L"Spectrum", L"Widmo", L"Spektrum"));
	SetDlgItemText(IDC_STATIC_EQ_FREQ, LL14(L"周波数", L"Frequency", L"Fréquence", L"Frequenza", L"Frecuencia", L"주파수", L"频率", L"التردد", L"Частота", L"Frequenz", L"Frequência", L"Frequentie", L"Częstotliwość", L"Frekans"));
	SetDlgItemText(IDC_STATIC_EQ_BAND, LL14(L"バランス", L"Band", L"Bande", L"Banda", L"Banda", L"밴드", L"频段", L"النطاق", L"Полоса", L"Band", L"Banda", L"Band", L"Pasmo", L"Bant"));
	SetDlgItemText(IDC_STATIC_EQ_LOUDNESS, LL14(L"密度", L"Loudness", L"Sonorité", L"Volume", L"Sonoridad", L"음량", L"响度", L"جهارة الصوت", L"Громкость", L"Lautheit", L"Sonoridade", L"Luidheid", L"Głośność", L"Ses yüksekliği"));
	SetDlgItemText(IDC_STATIC_EQ_WARMTH, LL14(L"立体", L"Warmth", L"Chaleur", L"Calore", L"Calidez", L"따뜻함", L"温暖", L"الدفء", L"Теплота", L"Wärme", L"Calor", L"Warmte", L"Ciepło", L"Sıcaklık"));

	SetDlgItemText(IDC_STATIC_EQ_REVERB, LL14(L"リバーブ", L"Reverb", L"Réverbération", L"Riverbero", L"Reverberación", L"리버브", L"混响", L"صدى", L"Реверберация", L"Hall", L"Reverberação", L"Galm", L"Pogłos", L"Yankı"));
	SetDlgItemText(IDC_STATIC_EQ_CHORUS, LL14(L"コーラス", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"코러스", L"合唱", L"كورس", L"Хорус", L"Chorus", L"Chorus", L"Chorus", L"Chorus", L"Koro"));
	SetDlgItemText(IDC_STATIC_EQ_DELAY, LL14(L"ディレイ", L"Delay", L"Délai", L"Delay", L"Delay", L"딜레이", L"延迟", L"تأخير", L"Задержка", L"Delay", L"Delay", L"Delay", L"Delay", L"Gecikme"));


	m_tooltip.Create(this);
	m_tooltip.Activate(TRUE);
	m_tooltip.AddTool(GetDlgItem(IDOK), LL14(L"閉じます", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	m_tooltip.AddTool(GetDlgItem(IDOK3), LL14(L"イコライザーの値をリセットします", L"Reset equalizer values", L"Réinitialiser les valeurs de l'égaliseur", L"Reimposta valori equalizer", L"Restablecer valores del ecualizador", L"이퀄라이저 값 초기화", L"重置均衡器数值", L"إعادة تعيين قيم المعادل", L"Сброс значений эквалайзера", L"Equalizerwerte zurücksetzen", L"Redefinir valores do equalizador", L"Equalizatorwaarden resetten", L"Resetuj wartości korektora", L"Ekolayzer değerlerini sıfırla"));
	m_tooltip.AddTool(GetDlgItem(IDOK4), LL14(L"グローバルの値をリセットします", L"Reset global values", L"Réinitialiser les valeurs globales", L"Reimposta valori globali", L"Restablecer valores globales", L"전역 값 초기화", L"重置全局数值", L"إعادة تعيين القيم العامة", L"Сброс глобальных значений", L"Globale Werte zurücksetzen", L"Redefinir valores globais", L"Globale waarden resetten", L"Resetuj wartości globalne", L"Genel değerleri sıfırla"));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER22), LL14(L"サウンドエフェクトの強さを調整します（左の数値が現在値）", L"Adjust sound effect strength (number at left is current value)", L"Ajuster l'intensite de l'effet sonore (nombre a gauche = valeur actuelle)", L"Regola intensita effetto sonoro (numero a sinistra = valore attuale)", L"Ajustar intensidad del efecto de sonido (numero a la izquierda = valor actual)", L"사운드 이펙트 강도 조정(왼쪽 숫자가 현재값)", L"调整音效强度（左侧数字为当前值）", L"ضبط قوة المؤثر الصوتي (الرقم على اليسار = القيمة الحالية)", L"Настроить силу звукового эффекта (число слева — текущее значение)", L"Soundeffekt-Starke einstellen (Zahl links = aktueller Wert)", L"Ajustar intensidade do efeito sonoro (numero a esquerda = valor atual)", L"Sterkte geluidseffect instellen (getal links = huidige waarde)", L"Reguluj sile efektu dzwiekowego (liczba po lewej = biezaca wartosc)", L"Ses efekti gucunu ayarla (soldaki sayi guncel deger)"));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER23), LL14(L"マスター音量を調整します（左の数値が現在値）\n拡張音量・形式別倍率とは別です", L"Adjust master volume (number at left is current value)\nSeparate from extended/format volume", L"Regler le volume master (nombre a gauche = valeur actuelle)\nDistinct du volume etendu/format", L"Regola volume master (numero a sinistra = valore attuale)\nSeparato da volume esteso/formato", L"Ajustar volumen maestro (numero a la izquierda = valor actual)\nSeparado del volumen extendido/formato", L"마스터 볼륨 조정(왼쪽 숫자가 현재값)\n확장/형식별 볼륨과 별개", L"调整主音量（左侧数字为当前值）\n与扩展/格式音量分开", L"ضبط مستوى الصوت الرئيسي (الرقم على اليسار = القيمة الحالية)\nمنفصل عن الصوت الممتد/حسب التنسيق", L"Настроить общую громкость (число слева — текущее значение)\nОтдельно от доп. и форматной громкости", L"Master-Lautstarke einstellen (Zahl links = aktueller Wert)\nGetrennt von erweiterter/Format-Lautstarke", L"Ajustar volume mestre (numero a esquerda = valor atual)\nSeparado do volume estendido/formato", L"Hoofdvolume instellen (getal links = huidige waarde)\nAparte van uitgebreid/formaatvolume", L"Reguluj glosnosc glowna (liczba po lewej = biezaca wartosc)\nOsobno od rozszerzonej/formatowej", L"Ana ses seviyesini ayarla (soldaki sayi guncel deger)\nGenisletilmis/format sesinden ayri)"));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER24), LL14(L"明瞭度（クリアさ）を調整します（左の数値が現在値）", L"Adjust clarity (number at left is current value)", L"Ajuster la clarte (nombre a gauche = valeur actuelle)", L"Regola chiarezza (numero a sinistra = valore attuale)", L"Ajustar claridad (numero a la izquierda = valor actual)", L"선명도 조정(왼쪽 숫자가 현재값)", L"调整清晰度（左侧数字为当前值）", L"ضبط الوضوح (الرقم على اليسار = القيمة الحالية)", L"Настроить четкость (число слева — текущее значение)", L"Klarheit einstellen (Zahl links = aktueller Wert)", L"Ajustar clareza (numero a esquerda = valor atual)", L"Helderheid instellen (getal links = huidige waarde)", L"Reguluj klarownosc (liczba po lewej = biezaca wartosc)", L"Netligi ayarla (soldaki sayi guncel deger)"));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER25), LL14(L"バランス（左右・帯域バランス）を調整します（左の数値が現在値）", L"Adjust balance (number at left is current value)", L"Ajuster l'equilibre (nombre a gauche = valeur actuelle)", L"Regola bilanciamento (numero a sinistra = valore attuale)", L"Ajustar balance (numero a la izquierda = valor actual)", L"밸런스 조정(왼쪽 숫자가 현재값)", L"调整平衡（左侧数字为当前值）", L"ضبط التوازن (الرقم على اليسار = القيمة الحالية)", L"Настроить баланс (число слева — текущее значение)", L"Balance einstellen (Zahl links = aktueller Wert)", L"Ajustar balanco (numero a esquerda = valor atual)", L"Balans instellen (getal links = huidige waarde)", L"Reguluj balans (liczba po lewej = biezaca wartosc)", L"Dengeyi ayarla (soldaki sayi guncel deger)"));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER26), LL14(L"密度（音の厚み）を調整します（左の数値が現在値）", L"Adjust density (number at left is current value)", L"Ajuster la densite (nombre a gauche = valeur actuelle)", L"Regola densita (numero a sinistra = valore attuale)", L"Ajustar densidad (numero a la izquierda = valor actual)", L"밀도 조정(왼쪽 숫자가 현재값)", L"调整密度（左侧数字为当前值）", L"ضبط الكثافة (الرقم على اليسار = القيمة الحالية)", L"Настроить плотность (число слева — текущее значение)", L"Dichte einstellen (Zahl links = aktueller Wert)", L"Ajustar densidade (numero a esquerda = valor atual)", L"Dichtheid instellen (getal links = huidige waarde)", L"Reguluj gestosc (liczba po lewej = biezaca wartosc)", L"Yogunlugu ayarla (soldaki sayi guncel deger)"));
	m_tooltip.AddTool(GetDlgItem(IDC_SLIDER27), LL14(L"立体感（空間感）を調整します（左の数値が現在値）", L"Adjust spatial width (number at left is current value)", L"Ajuster l'espace stereo (nombre a gauche = valeur actuelle)", L"Regola spazialita (numero a sinistra = valore attuale)", L"Ajustar amplitud espacial (numero a la izquierda = valor actual)", L"입체감 조정(왼쪽 숫자가 현재값)", L"调整立体感（左侧数字为当前值）", L"ضبط العرض المكاني (الرقم على اليسار = القيمة الحالية)", L"Настроить пространственность (число слева — текущее значение)", L"Raumlichkeit einstellen (Zahl links = aktueller Wert)", L"Ajustar espacialidade (numero a esquerda = valor atual)", L"Ruimtelijkheid instellen (getal links = huidige waarde)", L"Reguluj przestrzennosc (liczba po lewej = biezaca wartosc)", L"Mekansal genisligi ayarla (soldaki sayi guncel deger)"));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO1), LL14(L"再生環境（部屋の響き）プリセットを選択します", L"Select acoustic environment preset", L"Choisir le preset d'environnement acoustique", L"Seleziona preset ambiente acustico", L"Seleccionar preset de entorno acustico", L"재생 환경(음향) 프리셋 선택", L"选择播放环境（混响）预设", L"اختر إعداد البيئة الصوتية", L"Выбрать пресет акустической среды", L"Akustische Umgebungsvoreinstellung wahlen", L"Selecionar preset de ambiente acustico", L"Akoestische omgevingspreset kiezen", L"Wybierz preset srodowiska akustycznego", L"Akustik ortam on ayarini sec"));
	m_tooltip.AddTool(GetDlgItem(IDC_COMBO5), LL14(L"イコライザープリセットを選択します", L"Select equalizer preset", L"Choisir un preset d'egaliseur", L"Seleziona preset equalizzatore", L"Seleccionar preset del ecualizador", L"이퀄라이저 프리셋 선택", L"选择均衡器预设", L"اختر إعداد المعادل", L"Выбрать пресет эквалайзера", L"Equalizer-Voreinstellung wahlen", L"Selecionar preset do equalizador", L"Equalizerpreset kiezen", L"Wybierz preset korektora", L"Ekolayzer on ayarini sec"));
	m_tooltip.SetDelayTime(TTDT_AUTOPOP, 10000);
	m_tooltip.SendMessage(TTM_SETMAXTIPWIDTH, 0, 512);


	CString s;
	s.Format(L"%d", savedata.eq[0]);
	m_v0.SetWindowText(s);
	s.Format(L"%d", savedata.eq[1]);
	m_v1.SetWindowText(s);
	s.Format(L"%d", savedata.eq[2]);
	m_v2.SetWindowText(s);
	s.Format(L"%d", savedata.eq[3]);
	m_v3.SetWindowText(s);
	s.Format(L"%d", savedata.eq[4]);
	m_v4.SetWindowText(s);
	s.Format(L"%d", savedata.eq[5]);
	m_v5.SetWindowText(s);
	s.Format(L"%d", savedata.eq[6]);
	m_v6.SetWindowText(s);
	s.Format(L"%d", savedata.eq[7]);
	m_v7.SetWindowText(s);
	s.Format(L"%d", savedata.eq[8]);
	m_v8.SetWindowText(s);
	s.Format(L"%d", savedata.eq[9]);
	m_v9.SetWindowText(s);
	s.Format(L"%d", savedata.eq[10]);
	m_v10.SetWindowText(s);
	s.Format(L"%d", savedata.eq[11]);
	m_v11.SetWindowText(s);
	s.Format(L"%d", savedata.eq[12]);
	m_v12.SetWindowText(s);
	s.Format(L"%d", savedata.eq[13]);
	m_v13.SetWindowText(s);
	s.Format(L"%d", savedata.eq[14]);
	m_v14.SetWindowText(s);

	m_s0.SetMode(1);
	m_s1.SetMode(1);
	m_s2.SetMode(1);
	m_s3.SetMode(1);
	m_s4.SetMode(1);
	m_s5.SetMode(1);
	m_s6.SetMode(1);
	m_s7.SetMode(1);
	m_s8.SetMode(1);
	m_s9.SetMode(1);
	m_s10.SetMode(1);
	m_s11.SetMode(1);
	m_s12.SetMode(1);
	m_s13.SetMode(1);
	m_s14.SetMode(1);


	m_smaster.SetMode(2);
	m_ssenmei.SetMode(2);
	m_skoutei.SetMode(2);
	m_smitsudo.SetMode(2);
	m_srittai.SetMode(2);

	m_reverb.SetMode(2);
	m_chorus.SetMode(2);
	m_delay.SetMode(2);


	ReapplyDecorativeTitleFont();

	m_t.SetPreferWideMode(TRUE);
	m_t.SetGradation(COLOR_GRAD_DARK_GREEN, COLOR_RANGE_SELECTION, 135, TRUE); // 135 左上から右下
	m_t.SetDropShadow(RGB(0, 0, 0), 45, 18, 7, TRUE);

	m_s0.SetRange(0, 200);
	m_s1.SetRange(0, 200);
	m_s2.SetRange(0, 200);
	m_s3.SetRange(0, 200);
	m_s4.SetRange(0, 200);
	m_s5.SetRange(0, 200);
	m_s6.SetRange(0, 200);
	m_s7.SetRange(0, 200);
	m_s8.SetRange(0, 200);
	m_s9.SetRange(0, 200);
	m_s10.SetRange(0, 200);
	m_s11.SetRange(0, 200);
	m_s12.SetRange(0, 200);
	m_s13.SetRange(0, 200);
	m_s14.SetRange(0, 200);

	m_eff.SetRange(0, 200);
	m_eff.SetPos(savedata.eqsoundeffect * 2);
	s.Format(L"%d", savedata.eqsoundeffect * 2);
	m_seff.SetWindowText(s);

	m_smaster.SetRange(0, 200);
	m_smaster.SetPos(200 - savedata.eq[15]);
	s.Format(L"%d", savedata.eq[15]);
	m_vmaster.SetWindowText(s);

	m_ssenmei.SetRange(0, 200);
	m_ssenmei.SetPos(200 - savedata.eq[16]);
	s.Format(L"%d", savedata.eq[16]);
	m_vsenmei.SetWindowText(s);

	m_skoutei.SetRange(0, 200);
	m_skoutei.SetPos(200 - savedata.eq[17]);
	s.Format(L"%d", savedata.eq[17]);
	m_vkoutei.SetWindowText(s);

	m_smitsudo.SetRange(0, 200);
	m_smitsudo.SetPos(200 - savedata.eq[18]);
	s.Format(L"%d", savedata.eq[18]);
	m_vmitsudo.SetWindowText(s);

	m_srittai.SetRange(0, 200);
	m_srittai.SetPos(200 - savedata.eq[19]);
	s.Format(L"%d", savedata.eq[19]);
	m_vrittai.SetWindowText(s);

	m_reverb.SetRange(0, 200);
	m_reverb.SetPos(200 - savedata.eq_reverb);
	s.Format(L"%d", savedata.eq_reverb);
	m_reverbi.SetWindowText(s);

	m_chorus.SetRange(0, 200);
	m_chorus.SetPos(200 - savedata.eq_chorus);
	s.Format(L"%d", savedata.eq_chorus);
	m_chorusi.SetWindowText(s);

	m_delay.SetRange(0, 200);
	m_delay.SetPos(200 - savedata.eq_delay);
	s.Format(L"%d", savedata.eq_delay);
	m_delayi.SetWindowText(s);


	m_s0.SetPos(200 - savedata.eq[0]);
	m_s1.SetPos(200 - savedata.eq[1]);
	m_s2.SetPos(200 - savedata.eq[2]);
	m_s3.SetPos(200 - savedata.eq[3]);
	m_s4.SetPos(200 - savedata.eq[4]);
	m_s5.SetPos(200 - savedata.eq[5]);
	m_s6.SetPos(200 - savedata.eq[6]);
	m_s7.SetPos(200 - savedata.eq[7]);
	m_s8.SetPos(200 - savedata.eq[8]);
	m_s9.SetPos(200 - savedata.eq[9]);
	m_s10.SetPos(200 - savedata.eq[10]);
	m_s11.SetPos(200 - savedata.eq[11]);
	m_s12.SetPos(200 - savedata.eq[12]);
	m_s13.SetPos(200 - savedata.eq[13]);
	m_s14.SetPos(200 - savedata.eq[14]);

	// 環境音響プリセット101種
	
//0
	m_env.AddString(LL14(L"なし", L"None", L"Aucun", L"Nessuno", L"Ninguno", L"없음", L"无", L"لا شيء", L"Нет", L"Keiner", L"Nenhum", L"Geen", L"Brak", L"Yok"));
	//1
	m_env.AddString(LL14(L"--[[基本空間 1-10]]--", L"--[[Basic space 1-10]]--", L"--[[Espace de base 1-10]]--", L"--[[Spazio base 1-10]]--", L"--[[Espacio básico 1-10]]--", L"--[[기본 공간 1-10]]--", L"--[[基本空间 1-10]]--", L"--[[المساحة الأساسية 1-10]]--", L"--[[Базовое пространство 1-10]]--", L"--[[Grundraum 1-10]]--", L"--[[Espaço básico 1-10]]--", L"--[[Basisruimte 1-10]]--", L"--[[Przestrzeń podstawowa 1-10]]--", L"--[[Temel alan 1-10]]--"), TRUE);
	//2
	m_env.AddString(LL14(L"風呂場 (超短く超明るい、ピーキーな金属反射)", L"Bathroom (very short, bright, peaky metal reflection)", L"Salle de bain (très court, lumineux, réflexion métallique)", L"Bagno (molto corto, brillante, riflessione metallica)", L"Baño (muy corto, brillante, reflexión metálica)", L"욕실 (매우 짧고 밝은 금속 반사)", L"浴室（极短、明亮、金属反射）", L"حمام (قصير جداً، ساطع، انعكاس معدني)", L"Ванная (очень короткий, яркий, металлический отзвук)", L"Badezimmer (sehr kurz, hell, metallische Reflexion)", L"Banheiro (muito curto, brilhante, reflexão metálica)", L"Badkamer (zeer kort, helder, metalen reflectie)", L"Łazienka (bardzo krótki, jasny, metaliczna refleksja)", L"Banyo (çok kısa, parlak, metalik yansıma)"));
	m_env.AddString(LL14(L"ホール (中程度だがはっきり響く、バランス良好)", L"Hall (moderate but clear, balanced)", L"Salle (modérée mais claire, équilibrée)", L"Sala (moderata ma chiara, equilibrata)", L"Sala (moderada pero clara, equilibrada)", L"홀 (적당하지만 선명하고 균형 잡힘)", L"大厅（适度清晰、均衡）", L"قاعة (معتدلة لكن واضحة، متوازنة)", L"Зал (умеренный, чёткий, сбалансированный)", L"Saal (mäßig aber klar, ausgewogen)", L"Sala (moderado mas claro, equilibrado)", L"Zaal (gematigd maar helder, gebalanceerd)", L"Sala (umiarkowana, czysta, zbalansowana)", L"Salon (dengeli, net)"));
	m_env.AddString(LL14(L"教会 (超長く超重厚、圧倒的な残響)", L"Church (very long, massive reverb)", L"Église (très long, réverbération massive)", L"Chiesa (molto lunga, riverbero massiccio)", L"Iglesia (muy larga, reverb masiva)", L"교회 (매우 길고 웅장한 잔향)", L"教堂（超长厚重、强烈混响）", L"كنيسة (طويلة جداً، صدى ضخم)", L"Церковь (очень длинная, массивная реверберация)", L"Kirche (sehr lang, massive Hall)", L"Igreja (muito longa, reverb massiva)", L"Kerk (zeer lang, massieve nagalm)", L"Kościół (bardzo długi, masywna pogłos)", L"Kilise (çok uzun, yoğun yankı)"));
	m_env.AddString(LL14(L"洞窟 (長く暗く湿った、こもった響き)", L"Cave (long, dark, wet, muffled)", L"Grotte (longue, sombre, humide, étouffée)", L"Grotta (lunga, scura, umida, ovattata)", L"Cueva (larga, oscura, húmeda, apagada)", L"동굴 (길고 어둡고 축축한 웅웅거림)", L"洞穴（长、暗、湿、闷响）", L"كهف (طويل، مظلم، رطب، مكتوم)", L"Пещера (длинная, тёмная, влажная, глухая)", L"Höhle (lang, dunkel, nass, gedämpft)", L"Caverna (longa, escura, úmida, abafada)", L"Grot (lang, donker, nat, gedempt)", L"Jaskinia (długa, ciemna, mokra, stłumiona)", L"Mağara (uzun, karanlık, ıslak, boğuk)"));
	m_env.AddString(LL14(L"スタジオ (極めてドライ、ほぼ無響)", L"Studio (very dry, nearly anechoic)", L"Studio (très sec, quasi anéchoïque)", L"Studio (molto secco, quasi anecoico)", L"Estudio (muy seco, casi anecoico)", L"스튜디오 (매우 드라이, 거의 무향)", L"录音室（极干、近无响）", L"استوديو (جاف جداً، شبه خالٍ من الصدى)", L"Студия (очень сухая, почти без реверберации)", L"Studio (sehr trocken, fast schalltot)", L"Estúdio (muito seco, quase anecóico)", L"Studio (zeer droog, bijna echovrij)", L"Studio (bardzo suche, prawie bezechowe)", L"Stüdyo (çok kuru, neredeyse yankısız)"));
	m_env.AddString(LL14(L"ライブハウス (パンチがあって賑やか、エネルギッシュ)", L"Live house (punchy, lively, energetic)", L"Salle de concert (dynamique, vivant, énergique)", L"Live club (punchy, vivace, energico)", L"Sala de conciertos (punchy, animada, energética)", L"라이브 하우스 (펀치감 있고 활기참)", L"现场演出厅（有力、活泼、充满能量）", L"صالة حفلات (قوية، حية، نشطة)", L"Клуб (насыщенный, живой, энергичный)", L"Live-Haus (dynamisch, lebhaft, energisch)", L"Casa de shows (impactante, animada, energética)", L"Live venue (krachtig, levendig, energiek)", L"Sala koncertowa (dynamiczna, żywa, energetyczna)", L"Canlı mekan (güçlü, canlı, enerjik)"));
	m_env.AddString(LL14(L"森 (拡散的で柔らかい、包み込む自然)", L"Forest (diffuse, soft, enveloping)", L"Forêt (diffuse, douce, enveloppante)", L"Foresta (diffusa, morbida, avvolgente)", L"Bosque (difuso, suave, envolvente)", L"숲 (확산적이고 부드러운 자연)", L"森林（扩散、柔和、包围感）", L"غابة (منتشرة، ناعمة، محيطة)", L"Лес (рассеянный, мягкий, enveloping)", L"Wald (diffus, weich, einhüllend)", L"Floresta (difusa, suave, envolvente)", L"Bos (diffuus, zacht, omhullend)", L"Las (rozproszony, miękki, otulający)", L"Orman (dağınık, yumuşak, saran)"));
	m_env.AddString(LL14(L"山 (超長いエコー、遠くまではっきり響く)", L"Mountain (long echo, clear far)", L"Montagne (long écho, clair au loin)", L"Montagna (lungo eco, chiaro in lontananza)", L"Montaña (eco largo, claro a distancia)", L"산 (긴 에코, 먼 곳까지 선명)", L"山岳（超长回声、远传清晰）", L"جبل (صدى طويل، واضح من بعيد)", L"Гора (длинное эхо, чёткое вдали)", L"Berg (langes Echo, klar in der Ferne)", L"Montanha (eco longo, claro ao longe)", L"Berg (lang nagalm, helder ver)", L"Góra (długie echo, wyraźne w oddali)", L"Dağ (uzun yankı, uzakta net)"));
	m_env.AddString(LL14(L"広場 (開放的、空気を感じる広がり)", L"Plaza (open, airy)", L"Place (ouverte, aérée)", L"Piazza (aperta, ariosa)", L"Plaza (abierta, aireada)", L"광장 (열린, 통기성 좋음)", L"广场（开放、空气流通）", L"ساحة (مفتوحة، جيدة التهوية)", L"Площадь (открытая, воздушная)", L"Platz (offen, luftig)", L"Praça (aberta, arejada)", L"Plein (open, luchtig)", L"Plac (otwarty, przewiewny)", L"Meydan (açık, havadar)"));
	m_env.AddString(LL14(L"カテドラル (超巨大空間、圧倒的な残響と重厚感)", L"Cathedral (huge space, massive reverb)", L"Cathédrale (immense espace, réverb massive)", L"Cattedrale (spazio enorme, riverbero massiccio)", L"Catedral (espacio enorme, reverb masiva)", L"대성당 (거대한 공간, 압도적 잔향)", L"大教堂（巨大空间、强烈混响）", L"كاتدرائية (مساحة ضخمة، صدى ضخم)", L"Собор (огромное пространство, массивная реверберация)", L"Kathedrale (riesiger Raum, massiver Hall)", L"Catedral (espaço enorme, reverb massiva)", L"Kathedraal (enorme ruimte, massieve nagalm)", L"Katedra (ogromna przestrzeń, masywna pogłos)", L"Katedral (devasa alan, yoğun yankı)"));
	//12
	m_env.AddString(LL14(L"--[[公共施設 11-20]]--", L"--[[Public 11-20]]--", L"--[[Public 11-20]]--", L"--[[Pubblico 11-20]]--", L"--[[Público 11-20]]--", L"--[[공공시설 11-20]]--", L"--[[公共设施 11-20]]--", L"--[[عامة 11-20]]--", L"--[[Публичное 11-20]]--", L"--[[Öffentlich 11-20]]--", L"--[[Público 11-20]]--", L"--[[Openbaar 11-20]]--", L"--[[Publiczny 11-20]]--", L"--[[Kamu 11-20]]--"), TRUE);
	//13
	m_env.AddString(LL14(L"体育館 (硬く金属的、バスケコート的な響き)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)", L"Gymnasium (hard, metallic, basketball-court style reflection)"));
	m_env.AddString(LL14(L"峡谷 (複数の明確なエコー、両側から反響)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)", L"Canyon (multiple distinct echoes, reflections from both sides)"));
	m_env.AddString(LL14(L"地下室 (狭く圧迫感、密度の高い反射)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)", L"Basement (cramped, dense reflections)"));
	m_env.AddString(LL14(L"劇場 (音響設計された空間、明瞭でバランス良好)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)", L"Theater (acoustically designed space, clear and balanced)"));
	m_env.AddString(LL14(L"水中 (特殊な密度、こもった独特の響き)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)", L"Underwater (unique density, muffled distinctive sound)"));
	m_env.AddString(LL14(L"トンネル/地下道 (フラッターエコー、平行壁面の連続反射)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)", L"Tunnel/Underground (flutter echo, parallel wall reflections)"));
	m_env.AddString(LL14(L"アリーナ/ドーム (超巨大スポーツ施設、観客席の吸音と長残響)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)", L"Arena/Dome (huge sports facility, audience absorption and long reverb)"));
	m_env.AddString(LL14(L"小部屋/クローゼット (超小空間デッド、ほぼ無反射)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)", L"Small room/Closet (very dead space, nearly no reflection)"));
	m_env.AddString(LL14(L"階段室 (縦方向の特殊反射、螺旋的な響き)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)", L"Stairwell (vertical reflections, spiral-like reverb)"));
	m_env.AddString(LL14(L"地下鉄ホーム (都市的コンクリート、硬質な反射)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)", L"Subway platform (urban concrete, hard reflections)"));

	m_env.AddString(LL14(L"--[[産業・商業 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--", L"--[[Industrial 21-30]]--"), TRUE);

	m_env.AddString(LL14(L"倉庫 (大きく空っぽ、高天井で硬い床)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)", L"Warehouse (large and empty, high ceiling, hard floor)"));
	m_env.AddString(LL14(L"廊下 (長く狭い直線的、方向性のある反響)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)", L"Corridor (long narrow linear, directional reflections)"));
	m_env.AddString(LL14(L"工場 (金属的産業的、複雑な反響)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)", L"Factory (metallic industrial, complex reflections)"));
	m_env.AddString(LL14(L"寺社仏閣 (木造の温かみ、柔らかい反射)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)", L"Temple/Shrine (wooden warmth, soft reflections)"));
	m_env.AddString(LL14(L"宇宙空間 (SF特殊空間、無重力感と極端な残響)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)", L"Outer space (SF special space, zero-g feel and extreme reverb)"));
	m_env.AddString(LL14(L"野球場/サッカー場 (屋外超大型、遠距離反射と開放感)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)", L"Baseball/Soccer field (outdoor large scale, distant reflections and openness)"));
	m_env.AddString(LL14(L"図書館 (静寂で吸音的、控えめな反射)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)", L"Library (quiet and absorbent, subtle reflections)"));
	m_env.AddString(LL14(L"プール(室内) (タイル水面反射、独特の明るい響き)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)", L"Pool (indoor) (tile and water reflections, unique bright sound)"));
	m_env.AddString(LL14(L"エレベーター (超小金属空間、密閉された短い反射)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)", L"Elevator (tiny metal space, confined short reflections)"));
	m_env.AddString(LL14(L"駐車場 (広い低天井コンクリート、硬質な反響)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)", L"Parking lot (wide low-ceiling concrete, hard reflections)"));

	m_env.AddString(LL14(L"--[[文化施設 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--", L"--[[Cultural 31-40]]--"), TRUE);

	m_env.AddString(LL14(L"コンサートホール (クラシック用最高峰、精密な音響設計)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)", L"Concert hall (classical pinnacle, precise acoustic design)"));
	m_env.AddString(LL14(L"ジャズクラブ (親密で温かい、程よい残響)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)", L"Jazz club (intimate and warm, moderate reverb)"));
	m_env.AddString(LL14(L"カラオケボックス (小密室エンタメ、明るく賑やか)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)", L"Karaoke box (small enclosed entertainment, bright and lively)"));
	m_env.AddString(LL14(L"映画館 (THX規格的、臨場感のある音響)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)", L"Movie theater (THX standard, immersive sound)"));
	m_env.AddString(LL14(L"地下鉄車内 (揺れる密室、硬質で圧迫感)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)", L"Subway car (shaking enclosed space, hard and oppressive)"));
	m_env.AddString(LL14(L"空港ターミナル (巨大公共空間、高天井と複雑な反射)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)", L"Airport terminal (vast public space, high ceiling and complex reflections)"));
	m_env.AddString(LL14(L"ショッピングモール (賑やか商業施設、適度な吸音)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)", L"Shopping mall (lively commercial facility, moderate absorption)"));
	m_env.AddString(LL14(L"病院 (静かで清潔、吸音材による落ち着いた空間)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)", L"Hospital (quiet and clean, calm space with absorption)"));
	m_env.AddString(LL14(L"レコーディングブース (プロ用極ドライ、完全無響に近い)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)", L"Recording booth (pro ultra-dry, nearly anechoic)"));
	m_env.AddString(LL14(L"オペラハウス (劇場の最高峰、豊かで美しい残響)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)", L"Opera house (theater pinnacle, rich and beautiful reverb)"));

	m_env.AddString(LL14(L"--[[生活空間 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--", L"--[[Living 41-50]]--"), TRUE);

	m_env.AddString(LL14(L"喫茶店/カフェ (適度な賑わいと吸音、リラックスした空間)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)", L"Café/Coffee shop (moderate liveliness and absorption, relaxed space)"));
	m_env.AddString(LL14(L"バー/ラウンジ (暗く落ち着いた雰囲気、中域重視)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)", L"Bar/Lounge (dark calm atmosphere, mid-focused)"));
	m_env.AddString(LL14(L"居酒屋 (賑やか木材吸音、温かみのある響き)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)", L"Izakaya (lively wood absorption, warm sound)"));
	m_env.AddString(LL14(L"美術館/博物館 (静かで広い高天井、上品な残響)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)", L"Museum/Art gallery (quiet spacious high ceiling, elegant reverb)"));
	m_env.AddString(LL14(L"講堂/大学教室 (教育施設の反射、明瞭な音響)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)", L"Auditorium/University classroom (educational facility reflections, clear sound)"));
	m_env.AddString(LL14(L"竹林 (和風自然音響、独特の拡散と風の音)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)", L"Bamboo forest (Japanese-style natural acoustics, unique diffusion and wind)"));
	m_env.AddString(LL14(L"渓谷/滝 (水の反射と濡れた岩肌、躍動感ある響き)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)", L"Gorge/Waterfall (water reflections and wet rock, dynamic sound)"));
	m_env.AddString(LL14(L"砂漠 (超開放的反射極小、乾いた空気感)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)", L"Desert (wide open, minimal reflections, dry air)"));
	m_env.AddString(LL14(L"ガレージ (車庫硬質空間、コンクリートと金属)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)", L"Garage (hard space, concrete and metal)"));
	m_env.AddString(LL14(L"展望台 (高所開放感、風と遠距離エコー)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)", L"Observation deck (elevated openness, wind and distant echo)"));

	m_env.AddString(LL14(L"--[[拡張空間 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--", L"--[[Extended 51-60]]--"), TRUE);

	m_env.AddString(LL14(L"小さな礼拝堂 (教会より親密で温かい)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)", L"Small chapel (more intimate and warm than church)"));
	m_env.AddString(LL14(L"大型ショッピングセンター (モールより巨大)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)", L"Large shopping center (bigger than mall)"));
	m_env.AddString(LL14(L"地下洞窟(深層) (より深く神秘的)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)", L"Underground cave (deep) (deeper and more mystical)"));
	m_env.AddString(LL14(L"古城の大広間 (石造り中世的)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)", L"Castle great hall (stone medieval)"));
	m_env.AddString(LL14(L"野外音楽堂 (半開放的ステージ)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)", L"Outdoor amphitheater (semi-open stage)"));
	m_env.AddString(LL14(L"鍾乳洞 (複雑な水滴反射)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)", L"Limestone cave (complex water droplet reflections)"));
	m_env.AddString(LL14(L"廃墟工場 (荒廃した金属空間)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)", L"Abandoned factory (decayed metal space)"));
	m_env.AddString(LL14(L"和室(畳) (日本的柔らかい吸音)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)", L"Japanese room (tatami) (Japanese soft absorption)"));
	m_env.AddString(LL14(L"温泉施設 (湿度高めタイル反射)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)", L"Hot spring facility (humid tile reflections)"));
	m_env.AddString(LL14(L"屋根裏部屋 (斜め天井の特殊空間)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)", L"Attic (angled ceiling special space)"));

	m_env.AddString(LL14(L"--[[特殊空間 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--", L"--[[Special 61-70]]--"), TRUE);

	m_env.AddString(LL14(L"地下駐車場(多層) (階層的複雑反射)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)", L"Underground parking (multi-level) (layered complex reflections)"));
	m_env.AddString(LL14(L"古い劇場(木造) (温かみある音響設計)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)", L"Old theater (wooden) (warm acoustic design)"));
	m_env.AddString(LL14(L"大型倉庫(空) (極端な空虚感)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)", L"Large warehouse (empty) (extreme emptiness)"));
	m_env.AddString(LL14(L"小さな教会 (カテドラルより親密)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)", L"Small church (more intimate than cathedral)"));
	m_env.AddString(LL14(L"ガラス温室 (硬質ガラス反射)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)", L"Glass greenhouse (hard glass reflections)"));
	m_env.AddString(LL14(L"石造りトンネル (硬く長い残響)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)", L"Stone tunnel (hard long reverb)"));
	m_env.AddString(LL14(L"コンクリート階段 (硬質縦方向反射)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)", L"Concrete stairs (hard vertical reflections)"));
	m_env.AddString(LL14(L"大浴場 (広いタイル反射)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)", L"Public bath (wide tile reflections)"));
	m_env.AddString(LL14(L"洗面所 (極小タイル空間)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)", L"Bathroom (tiny tile space)"));
	m_env.AddString(LL14(L"廊下(カーペット) (吸音的柔らかい)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)", L"Corridor (carpeted) (absorptive and soft)"));

	m_env.AddString(LL14(L"--[[専門空間 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--", L"--[[Professional 71-80]]--"), TRUE);

	m_env.AddString(LL14(L"会議室(大) (ビジネス空間)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)", L"Meeting room (large) (business space)"));
	m_env.AddString(LL14(L"会議室(小) (より密閉的)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)", L"Meeting room (small) (more enclosed)"));
	m_env.AddString(LL14(L"防音室 (極端なデッド空間)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)", L"Soundproof room (extreme dead space)"));
	m_env.AddString(LL14(L"エントランスホール (高天井開放的)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)", L"Entrance hall (high ceiling, open)"));
	m_env.AddString(LL14(L"書斎 (本による吸音)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)", L"Study (absorption from books)"));
	m_env.AddString(LL14(L"キッチン (硬質多反射)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)", L"Kitchen (hard multi-reflections)"));
	m_env.AddString(LL14(L"屋外駐車場 (開放的反射少)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)", L"Outdoor parking lot (open, fewer reflections)"));
	m_env.AddString(LL14(L"地下道(狭) (圧迫的狭小空間)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)", L"Underground passage (narrow) (oppressive confined space)"));
	m_env.AddString(LL14(L"展示室 (美術館より吸音的)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)", L"Exhibition room (more absorbent than gallery)"));
	m_env.AddString(LL14(L"アトリエ (創作空間の独特さ)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)", L"Atelier (unique creative space)"));

	m_env.AddString(LL14(L"--[[SFX/未来 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--", L"--[[SFX/Future 81-100]]--"), TRUE);
	m_env.AddString(LL14(L"サイバーパンク路地 (金属反射＋狭い空間、ネオン感)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)", L"Cyberpunk alley (metal reflection + narrow space, neon feel)"));
	m_env.AddString(LL14(L"宇宙船ブリッジ (クリーンで硬質、短い反射)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)", L"Spaceship bridge (clean and hard, short reflections)"));
	m_env.AddString(LL14(L"ワープトンネル (揺らぎと長い残響、引き伸ばし)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)", L"Warp tunnel (fluctuation and long reverb, stretching)"));
	m_env.AddString(LL14(L"量子ホール (不安定拡散、浮遊感)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)", L"Quantum hall (unstable diffusion, floating feel)"));
	m_env.AddString(LL14(L"無限回廊 (規則的エコー、長く続く反射)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)", L"Infinite corridor (regular echo, long-lasting reflections)"));
	m_env.AddString(LL14(L"逆再生空間 (早い反射と遅い尾、異常な広がり)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)", L"Reverse playback space (fast reflection, slow tail, abnormal spread)"));
	m_env.AddString(LL14(L"タイムストップ室 (ほぼ無響＋硬い反射)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)", L"Time-stop room (nearly anechoic + hard reflection)"));
	m_env.AddString(LL14(L"データセンター (低域振動、機械的反射)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)", L"Data center (low-frequency vibration, mechanical reflection)"));
	m_env.AddString(LL14(L"巨大機械内部 (金属共鳴、重い反射)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)", L"Inside giant machine (metal resonance, heavy reflection)"));
	m_env.AddString(LL14(L"AIホログラム室 (透明感、明るい反射)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)", L"AI hologram room (transparency, bright reflection)"));
	m_env.AddString(LL14(L"重力ゼロ船庫 (低密度で長残響)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)", L"Zero-gravity hangar (low density, long reverb)"));
	m_env.AddString(LL14(L"惑星ドーム都市 (超巨大＋ガラス反射)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)", L"Planet dome city (vast + glass reflection)"));
	m_env.AddString(LL14(L"VRシミュレーター (過剰ステレオ＋揺れ)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)", L"VR simulator (excessive stereo + sway)"));
	m_env.AddString(LL14(L"レーザー通路 (鋭いフラッター、硬質)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)", L"Laser corridor (sharp flutter, hard)"));
	m_env.AddString(LL14(L"異次元裂け目 (不規則ディレイ、崩れる残響)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)", L"Dimensional rift (irregular delay, crumbling reverb)"));
	m_env.AddString(LL14(L"夢の中 (柔らかく滲む、低コントラスト)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)", L"Dream space (soft bleeding, low contrast)"));
	m_env.AddString(LL14(L"水晶洞 (高域きらめき、長い余韻)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)", L"Crystal cave (high-frequency shimmer, long decay)"));
	m_env.AddString(LL14(L"廃宇宙ステーション (冷たく乾いた残響)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)", L"Abandoned space station (cold dry reverb)"));
	m_env.AddString(LL14(L"ブラックホール縁 (超長残響＋低域膨張)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)", L"Black hole edge (ultra-long reverb + bass expansion)"));
	m_env.AddString(LL14(L"サイバー聖堂 (金属×巨大空間、光沢残響)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)", L"Cyber cathedral (metal × vast space, glossy reverb)"));

	{
		/*const int l[81] =
		{   0,
		     2, 3, 4, 5, 6, 7, 8, 9,10,11,
		    13,14,15,16,17,18,19,20,21,22,
		    24,25,26,27,28,29,30,31,32,33,
		    35,36,37,38,39,40,41,42,43,44,
			46,47,48,49,50,51,52,53,54,55,
			57,58,59,60,61,62,63,64,65,66,
			68,69,70,71,72,73,74,75,76,77,
			79,80,81,82,83,84,85,86,87,88
		};
		const int a = l[savedata.eqsoundenv];
		*/
		m_env.SetCurSel(savedata.eqsoundenv);
	}
	m_pre.AddString(LL14(L"デフォルト", L"Default", L"Par défaut", L"Predefinito", L"Predeterminado", L"기본값", L"默认", L"افتراضي", L"По умолчанию", L"Standard", L"Padrão", L"Standaard", L"Domyślny", L"Varsayılan"));
	m_pre.AddString(LL14(L"低音ブースト", L"Bass Boost", L"Renfort basses", L"Potenziamento bassi", L"Refuerzo graves", L"베이스 부스트", L"低音增强", L"تعزيز graves", L"Усиление низких", L"Bassverstärkung", L"Reforço graves", L"Basversterking", L"Wzmocnienie basów", L"Bas güçlendirme"));
	m_pre.AddString(LL14(L"高音ブースト", L"Treble Boost", L"Renfort aigus", L"Potenziamento acuti", L"Refuerzo agudos", L"트레블 부스트", L"高音增强", L"تعزيز agudos", L"Усиление высоких", L"Höhenverstärkung", L"Reforço agudos", L"Hoge versterking", L"Wzmocnienie wysokich", L"Tiz güçlendirme"));
	m_pre.AddString(LL14(L"ボーカル強調", L"Vocal Enhance", L"Renfort vocal", L"Miglioramento vocale", L"Mejora vocal", L"보컬 강조", L"人声增强", L"تحسين الصوت", L"Улучшение вокала", L"Gesangsverbesserung", L"Melhoria vocal", L"Vocaalverbetering", L"Wzmocnienie wokalu", L"Vokal iyileştirme"));
	m_pre.AddString(LL14(L"低音カット", L"Bass Cut", L"Coupe basses", L"Taglio bassi", L"Corte graves", L"베이스 컷", L"低音衰减", L"قطع graves", L"Обрез низких", L"Bassabsenkung", L"Corte graves", L"Basreductie", L"Obniżenie basów", L"Bas kesme"));
	m_pre.AddString(LL14(L"高音カット", L"Treble Cut", L"Coupe aigus", L"Taglio acuti", L"Corte agudos", L"트레블 컷", L"高音衰减", L"قطع agudos", L"Обрез высоких", L"Höhenabsenkung", L"Corte agudos", L"Hoge reductie", L"Obniżenie wysokich", L"Tiz kesme"));
	m_pre.AddString(LL14(L"ラウドネス", L"Loudness", L"Sonorité", L"Volume", L"Sonoridad", L"음량", L"响度", L"جهارة", L"Громкость", L"Lautstärke", L"Sonoridade", L"Luidheid", L"Głośność", L"Ses yüksekliği"));
	m_pre.AddString(LL14(L"クラシック", L"Classical", L"Classique", L"Classico", L"Clásico", L"클래식", L"古典", L"كلاسيكي", L"Классика", L"Klassik", L"Clássico", L"Klassiek", L"Klasyka", L"Klasik"));
	m_pre.AddString(LL14(L"ロック", L"Rock", L"Rock", L"Rock", L"Rock", L"록", L"摇滚", L"روك", L"Рок", L"Rock", L"Rock", L"Rock", L"Rock", L"Rock"));
	m_pre.AddString(LL14(L"カスタム", L"Custom", L"Personnalisé", L"Personalizzato", L"Personalizado", L"사용자 지정", L"自定义", L"مخصص", L"Пользовательский", L"Benutzerdefiniert", L"Personalizado", L"Aangepast", L"Niestandardowy", L"Özel"));
	m_pre.AddString(LL14(L"ジャズ", L"Jazz", L"Jazz", L"Jazz", L"Jazz", L"재즈", L"爵士", L"جاز", L"Джаз", L"Jazz", L"Jazz", L"Jazz", L"Jazz", L"Caz"));
	m_pre.AddString(LL14(L"ポップ", L"Pop", L"Pop", L"Pop", L"Pop", L"팝", L"流行", L"بوب", L"Поп", L"Pop", L"Pop", L"Pop", L"Pop", L"Pop"));
	m_pre.AddString(LL14(L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM", L"EDM"));
	m_pre.AddString(LL14(L"メタル", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal", L"Metal"));
	m_pre.AddString(LL14(L"ヒップホップ", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop", L"Hip Hop"));
	m_pre.AddString(LL14(L"アコースティック", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic", L"Acoustic"));
	m_pre.AddString(LL14(L"V字型(ドンシャリ)", L"V-shape", L"V-shape", L"V-shape", L"V-shape", L"V-shape", L"V-shape", L"V-shape", L"V-shape", L"V-shape", L"V-shape", L"V-shape", L"V-shape", L"V-shape"));
	m_pre.AddString(LL14(L"逆V字型", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V", L"Inverse V"));
	m_pre.AddString(LL14(L"スマイルカーブ", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve", L"Smile curve"));
	m_pre.AddString(LL14(L"ラジオ/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast", L"Radio/Podcast"));
	m_pre.AddString(LL14(L"映画/ドラマ", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama", L"Movie/Drama"));
	m_pre.AddString(LL14(L"ゲーミング", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Gaming", L"Gaming"));
	m_pre.AddString(LL14(L"ライブ録音", L"Live recording", L"Live recording", L"Live recording", L"Live recording", L"Live recording", L"Live recording", L"Live recording", L"Live recording", L"Live recording", L"Live recording", L"Live recording", L"Live recording", L"Live recording"));
	m_pre.AddString(LL14(L"トレブルブースト", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost", L"Treble Boost"));
	m_pre.AddString(LL14(L"ベースブースト", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost", L"Bass Boost"));
	m_pre.AddString(LL14(L"小音量用", L"For low volume", L"Pour petit volume", L"Per volume basso", L"Para volumen bajo", L"저음량용", L"小音量用", L"لصوت منخفض", L"Для малой громкости", L"Für leise Lautstärke", L"Para baixo volume", L"Voor laag volume", L"Dla cichej głośności", L"Düşük ses için"));
	m_pre.AddString(LL14(L"ヘッドホン用", L"For headphones", L"Pour casque", L"Per cuffie", L"Para auriculares", L"헤드폰용", L"耳机用", L"للسماعات", L"Для наушников", L"Für Kopfhörer", L"Para fones de ouvido", L"Voor koptelefoon", L"Dla słuchawek", L"Kulaklık için"));
	m_pre.AddString(LL14(L"ボーカル除去", L"Vocal remove", L"Suppression vocal", L"Rimozione vocale", L"Eliminar voz", L"보컬 제거", L"人声消除", L"إزالة الصوت", L"Удаление вокала", L"Gesangsentfernung", L"Remover vocal", L"Vocaal verwijderen", L"Usuwanie wokalu", L"Vokal kaldırma"));
	m_pre.AddString(LL14(L"重低音強化", L"Subwoofer boost", L"Renfort subgrave", L"Potenziamento subwoofer", L"Refuerzo subgrave", L"서브우퍼 부스트", L"重低音增强", L"تعزيز subgrave", L"Усиление сабвуфера", L"Subwoofer-Verstärkung", L"Reforço subgrave", L"Subwooferversterking", L"Wzmocnienie subwoofera", L"Sublow güçlendirme"));
	m_pre.AddString(LL14(L"ラジオAM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM", L"Radio AM"));
	m_pre.AddString(LL14(L"ラジオFM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM", L"Radio FM"));
	m_pre.AddString(LL14(L"テレビ音声", L"TV audio", L"TV audio", L"TV audio", L"TV audio", L"TV audio", L"TV audio", L"TV audio", L"TV audio", L"TV audio", L"TV audio", L"TV audio", L"TV audio", L"TV audio"));
	m_pre.AddString(LL14(L"電話音声", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice", L"Phone voice"));
	m_pre.AddString(LL14(L"ビンテージ", L"Vintage", L"Vintage", L"Vintage", L"Vintage", L"빈티지", L"复古", L"كلاسيكي", L"Винтаж", L"Vintage", L"Vintage", L"Vintage", L"Retro", L"Vintage"));
	m_pre.AddString(LL14(L"モダン", L"Modern", L"Moderne", L"Moderno", L"Moderno", L"모던", L"现代", L"حديث", L"Современный", L"Modern", L"Moderno", L"Modern", L"Nowoczesny", L"Modern"));
	m_pre.AddString(LL14(L"ウォーム", L"Warm", L"Chaud", L"Caldo", L"Cálido", L"웜", L"温暖", L"دافئ", L"Тёплый", L"Warm", L"Quente", L"Warm", L"Ciepły", L"Sıcak"));
	m_pre.AddString(LL14(L"ブライト", L"Bright", L"Brillant", L"Brillante", L"Brillante", L"브라이트", L"明亮", L"ساطع", L"Яркий", L"Hell", L"Brilhante", L"Helder", L"Jasny", L"Parlak"));
	m_pre.AddString(LL14(L"フラット+", L"Flat+", L"Flat+", L"Flat+", L"Flat+", L"Flat+", L"Flat+", L"Flat+", L"Flat+", L"Flat+", L"Flat+", L"Flat+", L"Flat+", L"Flat+"));
	m_pre.AddString(LL14(L"スーパーベース", L"Super bass", L"Super bass", L"Super bass", L"Super bass", L"Super bass", L"Super bass", L"Super bass", L"Super bass", L"Super bass", L"Super bass", L"Super bass", L"Super bass", L"Super bass"));
	m_pre.AddString(LL14(L"クリスタル", L"Crystal", L"Crystal", L"Crystal", L"Crystal", L"Crystal", L"Crystal", L"Crystal", L"Crystal", L"Crystal", L"Crystal", L"Crystal", L"Crystal", L"Crystal"));
	m_pre.AddString(LL14(L"パーフェクト", L"Perfect", L"Perfect", L"Perfect", L"Perfect", L"Perfect", L"Perfect", L"Perfect", L"Perfect", L"Perfect", L"Perfect", L"Perfect", L"Perfect", L"Perfect"));
	m_pre.AddString(LL14(L"ダンス/クラブ", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club", L"Dance/Club"));
	m_pre.AddString(LL14(L"R&&B/ソウル", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul", L"R&B/Soul"));
	m_pre.AddString(LL14(L"レゲエ", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae", L"Reggae"));
	m_pre.AddString(LL14(L"ブルース", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues", L"Blues"));
	m_pre.AddString(LL14(L"カントリー", L"Country", L"Country", L"Country", L"Country", L"Country", L"Country", L"Country", L"Country", L"Country", L"Country", L"Country", L"Country", L"Country"));
	m_pre.AddString(LL14(L"ファンク", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk", L"Funk"));
	m_pre.AddString(LL14(L"エレクトロニカ", L"Electronica", L"Electronica", L"Electronica", L"Electronica", L"Electronica", L"Electronica", L"Electronica", L"Electronica", L"Electronica", L"Electronica", L"Electronica", L"Electronica", L"Electronica"));
	m_pre.AddString(LL14(L"アンビエント", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient", L"Ambient"));
	m_pre.AddString(LL14(L"インストゥルメンタル", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental", L"Instrumental"));
	m_pre.AddString(LL14(L"ナレーション/オーディオブック", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook", L"Narration/Audiobook"));
	m_pre.AddString(LL14(L"ディープベース(安全)", L"Deep Bass (Safe)", L"Basses profondes (Sûr)", L"Bassi profondi (Sicuro)", L"Bajos profundos (Seguro)", L"딥 베이스(안전)", L"深低音（安全）", L"جهير عميق (آمن)", L"Глубокий бас (безопасно)", L"Tiefer Bass (Sicher)", L"Graves profundos (Seguro)", L"Diepe bas (Veilig)", L"Głęboki bas (Bezpieczny)", L"Derin Bas (Güvenli)"));
	m_pre.AddString(LL14(L"ボーカルクリア2", L"Vocal Clear 2", L"Voix claire 2", L"Voce chiara 2", L"Voz clara 2", L"보컬 클리어 2", L"人声清晰2", L"وضوح الصوت 2", L"Чистый вокал 2", L"Klarer Gesang 2", L"Vocal claro 2", L"Heldere vocalen 2", L"Czysty wokal 2", L"Net Vokal 2"));
	m_pre.AddString(LL14(L"エアリートレブル", L"Airy Treble", L"Aigus aérés", L"Alti ariosi", L"Agudos aireados", L"에어리 트레블", L"空气感高音", L"طبقات حادة هوائية", L"Воздушные высокие", L"Luftige Höhen", L"Agudos arejados", L"Luchtige hoge tonen", L"Przestrzenne wysokie", L"Havadar Tiz"));
	m_pre.AddString(LL14(L"中域パンチ", L"Mid Punch", L"Punch médium", L"Impatto medi", L"Pegada media", L"중역 펀치", L"中频冲击", L"دفع الترددات الوسطى", L"Ударная середина", L"Mitten-Punch", L"Impacto médio", L"Mid-punch", L"Uderzenie środka", L"Orta Frekans Darbesi"));
	m_pre.AddString(LL14(L"EDM(安全)", L"EDM (Safe)", L"EDM (Sûr)", L"EDM (Sicuro)", L"EDM (Seguro)", L"EDM(안전)", L"EDM（安全）", L"EDM (آمن)", L"EDM (безопасно)", L"EDM (Sicher)", L"EDM (Seguro)", L"EDM (Veilig)", L"EDM (Bezpieczny)", L"EDM (Güvenli)"));
	m_pre.AddString(LL14(L"ロック(ワイド)", L"Rock Wide", L"Rock large", L"Rock ampio", L"Rock amplio", L"록(와이드)", L"摇滚（宽广）", L"روك (واسع)", L"Рок (широкий)", L"Rock breit", L"Rock amplo", L"Rock breed", L"Rock szeroki", L"Rock Geniş"));
	m_pre.AddString(LL14(L"メタル(タイト)", L"Metal Tight", L"Metal serré", L"Metal stretto", L"Metal ajustado", L"메탈(타이트)", L"金属（紧致）", L"ميتال (محكم)", L"Метал (плотный)", L"Metal straff", L"Metal firme", L"Metal strak", L"Metal zwarty", L"Metal Sıkı"));
	m_pre.AddString(LL14(L"ヒップホップ(クラブ)", L"Hip Hop Club", L"Hip-hop club", L"Hip hop club", L"Hip hop club", L"힙합(클럽)", L"嘻哈（俱乐部）", L"هيب هوب كلوب", L"Хип-хоп клуб", L"Hip-Hop Club", L"Hip hop club", L"Hiphop club", L"Hip-hop klub", L"Hip Hop Kulüp"));
	m_pre.AddString(LL14(L"アコースティック(ウォーム2)", L"Acoustic Warm 2", L"Acoustique chaud 2", L"Acustico caldo 2", L"Acústico cálido 2", L"어쿠스틱(웜2)", L"原声（温暖2）", L"أكوستيك دافئ 2", L"Акустика тёплая 2", L"Akustisch warm 2", L"Acústico quente 2", L"Akoestisch warm 2", L"Akustyczny ciepły 2", L"Akustik Sıcak 2"));
	m_pre.AddString(LL14(L"フラット(モニター)", L"Flat Monitor", L"Plat moniteur", L"Flat monitor", L"Plano monitor", L"플랫(모니터)", L"平直（监听）", L"مسطح (مراقبة)", L"Ровный монитор", L"Flat Monitor", L"Flat monitor", L"Vlak monitor", L"Płaski monitor", L"Düz Monitör"));
	m_pre.AddString(LL14(L"ブライトボーカル", L"Bright Vocal", L"Voix brillante", L"Voce brillante", L"Voz brillante", L"브라이트 보컬", L"明亮人声", L"صوت ساطع", L"Яркий вокал", L"Heller Gesang", L"Vocal brilhante", L"Heldere vocalen", L"Jasny wokal", L"Parlak Vokal"));
	m_pre.AddString(LL14(L"低音+空気感", L"Bass and Air", L"Basses et air", L"Bassi e aria", L"Bajos y aire", L"저음+공기감", L"低频+空气感", L"جهير وهواء", L"Бас и воздух", L"Bass und Luft", L"Graves e ar", L"Bas en lucht", L"Bas i powietrze", L"Bas ve Hava"));
	m_pre.AddString(LL14(L"ポッドキャスト(ソフト)", L"Podcast Soft", L"Podcast doux", L"Podcast morbido", L"Podcast suave", L"팟캐스트(소프트)", L"播客（柔和）", L"بودكاست ناعم", L"Подкаст мягкий", L"Podcast weich", L"Podcast suave", L"Podcast zacht", L"Podcast miękki", L"Podcast Yumuşak"));
	m_pre.AddString(LL14(L"レトロラジオ2", L"Retro Radio 2", L"Radio rétro 2", L"Radio retrò 2", L"Radio retro 2", L"레트로 라디오 2", L"复古收音机2", L"راديو كلاسيكي 2", L"Ретро-радио 2", L"Retro-Radio 2", L"Rádio retrô 2", L"Retro radio 2", L"Radio retro 2", L"Retro Radyo 2"));
	m_pre.AddString(LL14(L"TVダイアログ+", L"TV Dialog+", L"Dialogue TV+", L"Dialoghi TV+", L"Diálogo TV+", L"TV 대사+", L"电视对白+", L"حوار التلفاز+", L"ТВ-диалог+", L"TV-Dialog+", L"Diálogo TV+", L"TV-dialoog+", L"Dialog TV+", L"TV Diyalog+"));
	m_pre.AddString(LL14(L"電話(ナロー+)", L"Phone Narrow+", L"Téléphone étroit+", L"Telefono stretto+", L"Teléfono estrecho+", L"전화(협대역+)", L"电话（窄带+）", L"هاتف ضيق+", L"Телефон узкий+", L"Telefon schmal+", L"Telefone estreito+", L"Telefoon smal+", L"Telefon wąski+", L"Telefon Dar+"));
	m_pre.AddString(LL14(L"ラウドネス(安全)", L"Loudness Safe", L"Sonie sûre", L"Loudness sicuro", L"Sonoridad segura", L"라우드니스(안전)", L"响度（安全）", L"شدة الصوت آمنة", L"Громкость безопасно", L"Lautheit sicher", L"Sonoridade segura", L"Luidheid veilig", L"Głośność bezpieczna", L"Ses Yüksekliği Güvenli"));
	m_pre.AddString(LL14(L"小型スピーカー", L"Small Speaker", L"Petite enceinte", L"Piccolo altoparlante", L"Altavoz pequeño", L"소형 스피커", L"小型扬声器", L"مكبر صوت صغير", L"Малый динамик", L"Kleiner Lautsprecher", L"Alto-falante pequeno", L"Kleine luidspreker", L"Mały głośnik", L"Küçük Hoparlör"));
	m_pre.AddString(LL14(L"カーオーディオ", L"Car Audio", L"Audio voiture", L"Audio auto", L"Audio de coche", L"차량 오디오", L"车载音频", L"صوت السيارة", L"Автозвук", L"Auto-Audio", L"Áudio automotivo", L"Auto-audio", L"Car audio", L"Araç Sesi"));
	m_pre.AddString(LL14(L"ナイトリスニング", L"Night Listening", L"Écoute nocturne", L"Ascolto notturno", L"Escucha nocturna", L"나이트 리스닝", L"夜间聆听", L"استماع ليلي", L"Ночное прослушивание", L"Nachtmodus Hören", L"Audição noturna", L"Nacht luisteren", L"Słuchanie nocne", L"Gece Dinleme"));
	m_pre.AddString(LL14(L"スタジオニュートラル+", L"Studio Neutral+", L"Studio neutre+", L"Studio neutro+", L"Estudio neutro+", L"스튜디오 뉴트럴+", L"录音室中性+", L"استوديو محايد+", L"Студийный нейтральный+", L"Studio neutral+", L"Estúdio neutro+", L"Studio neutraal+", L"Studio neutralny+", L"Stüdyo Nötr+"));
	m_pre.AddString(LL14(L"シンバルスパークル", L"Cymbal Sparkle", L"Brillance cymbales", L"Brillio piatti", L"Brillo de platillos", L"심벌 스파클", L"镲片闪亮", L"بريق الصنج", L"Блеск тарелок", L"Becken-Glanz", L"Brilho de pratos", L"Cimbaalglans", L"Blask talerzy", L"Zil Parıltısı"));
	m_pre.AddString(LL14(L"ドラムアタック", L"Drum Attack", L"Attaque batterie", L"Attacco batteria", L"Ataque de batería", L"드럼 어택", L"鼓点冲击", L"هجوم الطبول", L"Атака барабанов", L"Drum-Attacke", L"Ataque de bateria", L"Drumaanval", L"Atak perkusji", L"Davul Atak"));
	m_pre.AddString(LL14(L"ピアノプレゼンス", L"Piano Presence", L"Présence piano", L"Presenza piano", L"Presencia de piano", L"피아노 프레즌스", L"钢琴存在感", L"حضور البيانو", L"Присутствие пианино", L"Piano-Präsenz", L"Presença de piano", L"Piano-aanwezigheid", L"Obecność fortepianu", L"Piyano Varlığı"));
	m_pre.AddString(LL14(L"ストリングススムース", L"Strings Smooth", L"Cordes douces", L"Archi morbidi", L"Cuerdas suaves", L"스트링 스무스", L"弦乐柔顺", L"أوتار ناعمة", L"Гладкие струны", L"Sanfte Streicher", L"Cordas suaves", L"Strijkers zacht", L"Smyczki łagodne", L"Yaylılar Yumuşak"));
	m_pre.AddString(LL14(L"ブラスフォーカス", L"Brass Focus", L"Focus cuivres", L"Focus ottoni", L"Enfoque metales", L"브라스 포커스", L"铜管聚焦", L"تركيز النحاسيات", L"Фокус на духовых", L"Blechbläser-Fokus", L"Foco em metais", L"Brassfocus", L"Skupienie blach", L"Bakır Nefes Odak"));
	m_pre.AddString(LL14(L"クワイアワイド", L"Choir Wide", L"Chœur large", L"Coro ampio", L"Coro amplio", L"합창 와이드", L"合唱宽广", L"جوقة واسعة", L"Хор широкий", L"Chor breit", L"Coro amplo", L"Koor breed", L"Chór szeroki", L"Koro Geniş"));
	m_pre.AddString(LL14(L"シネマインパクト", L"Cinema Impact", L"Impact cinéma", L"Impatto cinema", L"Impacto cine", L"시네마 임팩트", L"影院冲击", L"تأثير سينمائي", L"Кино-импакт", L"Cinema-Impact", L"Impacto cinema", L"Cinema-impact", L"Efekt kinowy", L"Sinematik Etki"));
	m_pre.AddString(LL14(L"FPS足音強調", L"FPS Footstep", L"FPS pas accentués", L"FPS passi in evidenza", L"FPS pasos resaltados", L"FPS 발소리 강조", L"FPS脚步强化", L"FPS إبراز الخطوات", L"FPS шаги акцент", L"FPS Schritte betont", L"FPS passos destacados", L"FPS voetstappen benadrukt", L"FPS kroki wzmocnione", L"FPS Ayak Sesi"));
	m_pre.AddString(LL14(L"RPG雰囲気", L"RPG Atmosphere", L"Ambiance RPG", L"Atmosfera RPG", L"Atmósfera RPG", L"RPG 분위기", L"RPG氛围", L"أجواء RPG", L"Атмосфера RPG", L"RPG-Atmosphäre", L"Atmosfera RPG", L"RPG-sfeer", L"Klimat RPG", L"RPG Atmosfer"));
	m_pre.AddString(LL14(L"オープンワールド", L"Open World", L"Monde ouvert", L"Mondo aperto", L"Mundo abierto", L"오픈 월드", L"开放世界", L"عالم مفتوح", L"Открытый мир", L"Offene Welt", L"Mundo aberto", L"Open wereld", L"Otwarty świat", L"Açık Dünya"));
	m_pre.AddString(LL14(L"レーシングV", L"Racing V", L"Course V", L"Corsa V", L"Carreras V", L"레이싱 V", L"竞速V", L"سباق V", L"Гонки V", L"Racing V", L"Corrida V", L"Racing V", L"Wyścigi V", L"Yarış V"));
	m_pre.AddString(LL14(L"ファイティングパンチ", L"Fighting Punch", L"Punch combat", L"Pugno combattimento", L"Golpe de pelea", L"파이팅 펀치", L"格斗冲击", L"لكمة قتالية", L"Боевой панч", L"Fighting-Punch", L"Soco de luta", L"Fighting punch", L"Uderzenie walki", L"Dövüş Darbesi"));
	m_pre.AddString(LL14(L"Lo-Fiマイルド", L"Lo-Fi Mild", L"Lo-Fi doux", L"Lo-Fi morbido", L"Lo-Fi suave", L"Lo-Fi 마일드", L"Lo-Fi柔和", L"لو-فاي ناعم", L"Lo-Fi мягкий", L"Lo-Fi mild", L"Lo-Fi suave", L"Lo-Fi mild", L"Lo-Fi łagodny", L"Lo-Fi Hafif"));
	m_pre.AddString(LL14(L"チルソフト", L"Chill Soft", L"Chill doux", L"Chill morbido", L"Chill suave", L"칠 소프트", L"舒缓柔和", L"تشيل ناعم", L"Chill мягкий", L"Chill weich", L"Chill suave", L"Chill zacht", L"Chill łagodny", L"Chill Yumuşak"));
	m_pre.AddString(LL14(L"K-POPシャイン", L"K-Pop Shine", L"K-Pop brillant", L"K-Pop brillante", L"K-Pop brillo", L"K-POP 샤인", L"K-POP闪耀", L"K-Pop لامع", L"K-Pop блеск", L"K-Pop Glanz", L"K-Pop brilho", L"K-Pop glans", L"K-Pop blask", L"K-Pop Parlak"));
	m_pre.AddString(LL14(L"J-POPエア", L"J-Pop Air", L"J-Pop aérien", L"J-Pop arioso", L"J-Pop aéreo", L"J-POP 에어", L"J-POP空气感", L"J-Pop هوائي", L"J-Pop воздушный", L"J-Pop luftig", L"J-Pop arejado", L"J-Pop luchtig", L"J-Pop przestrzenny", L"J-Pop Havadar"));
	m_pre.AddString(LL14(L"アニメソング", L"Anime Song", L"Chanson anime", L"Canzone anime", L"Canción anime", L"애니송", L"动漫歌曲", L"أغنية أنمي", L"Аниме-песня", L"Anime-Song", L"Música anime", L"Anime lied", L"Piosenka anime", L"Anime Şarkı"));
	m_pre.AddString(LL14(L"オーケストラホール", L"Orchestra Hall", L"Salle d'orchestre", L"Sala orchestra", L"Sala de orquesta", L"오케스트라 홀", L"管弦乐厅", L"قاعة الأوركسترا", L"Оркестровый зал", L"Orchesterhalle", L"Sala de orquestra", L"Orkestzaal", L"Sala orkiestry", L"Orkestra Salonu"));
	m_pre.AddString(LL14(L"ライブステージ2", L"Live Stage 2", L"Scène live 2", L"Palco live 2", L"Escenario en vivo 2", L"라이브 스테이지 2", L"现场舞台2", L"منصة حية 2", L"Живая сцена 2", L"Live-Bühne 2", L"Palco ao vivo 2", L"Live podium 2", L"Scena na żywo 2", L"Canlı Sahne 2"));
	m_pre.AddString(LL14(L"マスタリング(軽)", L"Mastering Light", L"Mastering léger", L"Mastering leggero", L"Masterización ligera", L"마스터링(라이트)", L"母带（轻量）", L"ماستر خفيف", L"Лёгкий мастеринг", L"Mastering leicht", L"Masterização leve", L"Mastering licht", L"Mastering lekki", L"Mastering Hafif"));
	m_pre.AddString(LL14(L"サブタイト", L"Sub Tight", L"Sub serré", L"Sub stretto", L"Sub ajustado", L"서브 타이트", L"低频紧致", L"جهير محكم", L"Саб плотный", L"Sub straff", L"Sub firme", L"Sub strak", L"Sub zwarty", L"Sub Sıkı"));
	m_pre.AddString(LL14(L"ディープハウス", L"Deep House", L"Deep House", L"Deep House", L"Deep House", L"딥하우스", L"深浩室", L"ديب هاوس", L"Дип-хаус", L"Deep House", L"Deep House", L"Deep House", L"Deep House", L"Deep House"));
	m_pre.AddString(LL14(L"トランスリフト", L"Trance Lift", L"Lift trance", L"Lift trance", L"Impulso trance", L"트랜스 리프트", L"Trance提升", L"ترانس رفع", L"Транс подъём", L"Trance Lift", L"Elevação trance", L"Trance lift", L"Trance lift", L"Trance Lift"));
	m_pre.AddString(LL14(L"テクノエッジ", L"Techno Edge", L"Techno tranchant", L"Techno incisivo", L"Techno afilado", L"테크노 엣지", L"Techno锋锐", L"تكنو حاد", L"Техно-острота", L"Techno Edge", L"Techno intenso", L"Techno edge", L"Techno krawędź", L"Techno Keskin"));
	m_pre.AddString(LL14(L"ドラムンベース", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass", L"드럼 앤 베이스", L"鼓打贝斯", L"درَم آند بيس", L"Драм-н-бейс", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass", L"Drum and Bass"));
	m_pre.AddString(LL14(L"ソフトクラシック", L"Soft Classical", L"Classique doux", L"Classica soft", L"Clásico suave", L"소프트 클래식", L"柔和古典", L"كلاسيكي ناعم", L"Мягкая классика", L"Sanfte Klassik", L"Clássico suave", L"Zachte klassiek", L"Klasyka łagodna", L"Yumuşak Klasik"));
	m_pre.AddString(LL14(L"音声明瞭", L"Speech Intelligibility", L"Intelligibilité de la parole", L"Intelligibilità vocale", L"Inteligibilidad de voz", L"음성 명료", L"语音清晰", L"وضوح الكلام", L"Разборчивость речи", L"Sprachverständlichkeit", L"Inteligibilidade da fala", L"Spraakverstaanbaarheid", L"Zrozumiałość mowy", L"Konuşma Anlaşılırlığı"));
	m_pre.AddString(LL14(L"AM(安全ナロー)", L"AM Safe Narrow", L"AM étroit sûr", L"AM stretto sicuro", L"AM estrecho seguro", L"AM(안전 협대역)", L"AM安全窄带", L"AM ضيق آمن", L"AM безопасный узкий", L"AM sicher schmal", L"AM estreito seguro", L"AM veilig smal", L"AM bezpieczny wąski", L"AM Güvenli Dar"));
	m_pre.AddString(LL14(L"FM(Hi-Fi安全)", L"FM Hi-Fi Safe", L"FM Hi-Fi sûr", L"FM Hi-Fi sicuro", L"FM Hi-Fi seguro", L"FM(Hi-Fi 안전)", L"FM Hi-Fi安全", L"FM Hi-Fi آمن", L"FM Hi-Fi безопасно", L"FM Hi-Fi sicher", L"FM Hi-Fi seguro", L"FM Hi-Fi veilig", L"FM Hi-Fi bezpieczny", L"FM Hi-Fi Güvenli"));
	m_pre.SetCurSel(savedata.eqsoundeq);

	if(savedata.eqx != -1)
		SetWindowPos(&CWnd::wndTop, savedata.eqx, savedata.eqy, 0, 0, SWP_NOSIZE| SWP_NOZORDER| SWP_NOOWNERZORDER);

	m_cachedKeyLow.Empty();
	m_cachedKeyMid.Empty();
	m_cachedKeyHigh.Empty();
	m_cachedKeyAll.Empty();

	SetTimer(1, 50, NULL);
	return TRUE;
}

void CEqualizer::ReapplyDecorativeTitleFont()
{
	if (!m_t.GetSafeHwnd())
		return;
	UINT dpi = 96;
	if (HDC hdc = ::GetDC(m_t.GetSafeHwnd())) {
		dpi = (UINT)GetDeviceCaps(hdc, LOGPIXELSX);
		::ReleaseDC(m_t.GetSafeHwnd(), hdc);
	}
	if (dpi < 96) dpi = 96;
	LOGFONT lf;
	memset(&lf, 0, sizeof(lf));
	lf.lfHeight = -MulDiv(12 * 4, (int)dpi, 96);
	const int li = (savedata.lang < 0) ? 0 : (savedata.lang > 13 ? 13 : savedata.lang);
	lf.lfItalic = TRUE;
	CFont fn;
	if (fn.CreateFontIndirect(&lf))
		m_t.SetFont(&fn);
}

extern BOOL reset;
void CEqualizer::OnCbnSelchangeCombo1()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
/*	int a = m_env.GetCurSel();
	int c = 0;
	int l[] =
	{
		0,1,
		1,2,3,4,5,6,7,8,9,10,
		11,
		11,12,13,14,15,16,17,18,19,20,
		21,
		21,22,23,24,25,26,27,28,29,30,
		31,
		31,32,33,34,35,36,37,38,39,40,
		41,
		41,42,43,44,45,46,47,48,49,50,
		51,
		51,52,53,54,55,56,57,58,59,60,
		61,
		61,62,63,64,65,66,67,68,69,70,
		71,
		71,72,73,74,75,76,77,78,79,80
	};
	*/
	savedata.eqsoundenv = m_env.GetCurSel();
	reset = TRUE;
}

void equaliser(void* data, int len, BOOL reset);

void CEqualizer::OnCbnSelchangeCombo5()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	KillTimer(1);
	savedata.eqsoundeq = m_pre.GetCurSel();
	equaliser(0, 0, 2);
	SetTimer(1, 50, NULL);
}

extern CString KeyCodeLow, KeyCodeMid, KeyCodeHigh, KeyCodeAll;
int backms = 0;
void CEqualizer::OnTimer(UINT_PTR nIDEvent)
{
	// TODO: ここにメッセージ ハンドラー コードを追加するか、既定の処理を呼び出します。
	if (mod != savedata.eqsoundeq) {
		if (savedata.eqsoundeq != 9) {
			m_s0.SetPos(200 - savedata.eq[0]);
			m_s1.SetPos(200 - savedata.eq[1]);
			m_s2.SetPos(200 - savedata.eq[2]);
			m_s3.SetPos(200 - savedata.eq[3]);
			m_s4.SetPos(200 - savedata.eq[4]);
			m_s5.SetPos(200 - savedata.eq[5]);
			m_s6.SetPos(200 - savedata.eq[6]);
			m_s7.SetPos(200 - savedata.eq[7]);
			m_s8.SetPos(200 - savedata.eq[8]);
			m_s9.SetPos(200 - savedata.eq[9]);
			m_s10.SetPos(200 - savedata.eq[10]);
			m_s11.SetPos(200 - savedata.eq[11]);
			m_s12.SetPos(200 - savedata.eq[12]);
			m_s13.SetPos(200 - savedata.eq[13]);
			m_s14.SetPos(200 - savedata.eq[14]);
			CString s;
			s.Format(L"%d", savedata.eq[0]);
			m_v0.SetWindowText(s);
			s.Format(L"%d", savedata.eq[1]);
			m_v1.SetWindowText(s);
			s.Format(L"%d", savedata.eq[2]);
			m_v2.SetWindowText(s);
			s.Format(L"%d", savedata.eq[3]);
			m_v3.SetWindowText(s);
			s.Format(L"%d", savedata.eq[4]);
			m_v4.SetWindowText(s);
			s.Format(L"%d", savedata.eq[5]);
			m_v5.SetWindowText(s);
			s.Format(L"%d", savedata.eq[6]);
			m_v6.SetWindowText(s);
			s.Format(L"%d", savedata.eq[7]);
			m_v7.SetWindowText(s);
			s.Format(L"%d", savedata.eq[8]);
			m_v8.SetWindowText(s);
			s.Format(L"%d", savedata.eq[9]);
			m_v9.SetWindowText(s);
			s.Format(L"%d", savedata.eq[10]);
			m_v10.SetWindowText(s);
			s.Format(L"%d", savedata.eq[11]);
			m_v11.SetWindowText(s);
			s.Format(L"%d", savedata.eq[12]);
			m_v12.SetWindowText(s);
			s.Format(L"%d", savedata.eq[13]);
			m_v13.SetWindowText(s);
			s.Format(L"%d", savedata.eq[14]);
			m_v14.SetWindowText(s);
		}
		mod = savedata.eqsoundeq;
	}
	CString s;
	int vol;
	int flg = 0;
	vol = 200 - m_s0.GetPos();
	if (vol != savedata.eq[0]) { s.Format(L"%d", vol); m_v0.SetWindowText(s); flg = 1; }
	savedata.eq[0] = vol;
	vol = 200 - m_s1.GetPos();
	if (vol != savedata.eq[1]) { s.Format(L"%d", vol); m_v1.SetWindowText(s); flg = 1;}
	savedata.eq[1] = vol;
	vol = 200 - m_s2.GetPos();
	if (vol != savedata.eq[2]) { s.Format(L"%d", vol); m_v2.SetWindowText(s); flg = 1;	}
	savedata.eq[2] = vol;
	vol = 200 - m_s3.GetPos();
	if (vol != savedata.eq[3]) { s.Format(L"%d", vol); m_v3.SetWindowText(s); flg = 1;	}
	savedata.eq[3] = vol;
	vol = 200 - m_s4.GetPos();
	if (vol != savedata.eq[4]) { s.Format(L"%d", vol); m_v4.SetWindowText(s);  flg = 1;	}
	savedata.eq[4] = vol;
	vol = 200 - m_s5.GetPos();
	if (vol != savedata.eq[5]) { s.Format(L"%d", vol); m_v5.SetWindowText(s); flg = 1;	}
	savedata.eq[5] = vol;
	vol = 200 - m_s6.GetPos();
	if (vol != savedata.eq[6]) { s.Format(L"%d", vol); m_v6.SetWindowText(s); flg = 1;	}
	savedata.eq[6] = vol;
	vol = 200 - m_s7.GetPos();
	if (vol != savedata.eq[7]) { s.Format(L"%d", vol); m_v7.SetWindowText(s); flg = 1;	}
	savedata.eq[7] = vol;
	vol = 200 - m_s8.GetPos();
	if (vol != savedata.eq[8]) { s.Format(L"%d", vol); m_v8.SetWindowText(s); flg = 1;	}
	savedata.eq[8] = vol;
	vol = 200 - m_s9.GetPos();
	if (vol != savedata.eq[9]) { s.Format(L"%d", vol); m_v9.SetWindowText(s); flg = 1;	}
	savedata.eq[9] = vol;

	vol = 200 - m_s10.GetPos();
	if (vol != savedata.eq[10]) { s.Format(L"%d", vol); m_v10.SetWindowText(s); flg = 1; }
	savedata.eq[10] = vol;
	vol = 200 - m_s11.GetPos();
	if (vol != savedata.eq[11]) { s.Format(L"%d", vol); m_v11.SetWindowText(s); flg = 1; }
	savedata.eq[11] = vol;
	vol = 200 - m_s12.GetPos();
	if (vol != savedata.eq[12]) { s.Format(L"%d", vol); m_v12.SetWindowText(s); flg = 1; }
	savedata.eq[12] = vol;
	vol = 200 - m_s13.GetPos();
	if (vol != savedata.eq[13]) { s.Format(L"%d", vol); m_v13.SetWindowText(s); flg = 1; }
	savedata.eq[13] = vol;
	vol = 200 - m_s14.GetPos();
	if (vol != savedata.eq[14]) { s.Format(L"%d", vol); m_v14.SetWindowText(s); flg = 1; }
	savedata.eq[14] = vol;

	if (flg == 1) { m_pre.SetCurSel(9); savedata.eqsoundeq = 9; }


	vol = 200 - m_smaster.GetPos();
	if (vol != savedata.eq[15]) { s.Format(L"%d", vol); m_vmaster.SetWindowText(s); }
	savedata.eq[15] = vol;
	vol = 200 - m_ssenmei.GetPos();
	if (vol != savedata.eq[16]) { s.Format(L"%d", vol); m_vsenmei.SetWindowText(s); }
	savedata.eq[16] = vol;
	vol = 200 - m_skoutei.GetPos();
	if (vol != savedata.eq[17]) { s.Format(L"%d", vol); m_vkoutei.SetWindowText(s); }
	savedata.eq[17] = vol;
	vol = 200 - m_smitsudo.GetPos();
	if (vol != savedata.eq[18]) { s.Format(L"%d", vol); m_vmitsudo.SetWindowText(s); }
	savedata.eq[18] = vol;
	vol = 200 - m_srittai.GetPos();
	if (vol != savedata.eq[19]) { s.Format(L"%d", vol); m_vrittai.SetWindowText(s); }
	savedata.eq[19] = vol;

	vol = 200 - m_reverb.GetPos();
	if (vol != savedata.eq_reverb) { s.Format(L"%d", vol); m_reverbi.SetWindowText(s); }
	savedata.eq_reverb = vol;
	vol = 200 - m_chorus.GetPos();
	if (vol != savedata.eq_chorus) { s.Format(L"%d", vol); m_chorusi.SetWindowText(s); }
	savedata.eq_chorus = vol;
	vol = 200 - m_delay.GetPos();
	if (vol != savedata.eq_delay) { s.Format(L"%d", vol); m_delayi.SetWindowText(s); }
	savedata.eq_delay = vol;

	vol = m_eff.GetPos();
	if(vol / 2 != savedata.eqsoundeffect) { s.Format(L"%d", vol); m_seff.SetWindowText(s); }
	savedata.eqsoundeffect = vol / 2;


	CRect rect;
	GetWindowRect(rect);
	savedata.eqx = rect.left;
	savedata.eqy = rect.top;


	// キーコード表示
	extern int playf;
	if (playf == 0) {
		KeyCodeLow  = L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B";
		KeyCodeMid  = L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B";
		KeyCodeHigh = L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B";
		KeyCodeAll  = L"!@B  , !@C002525<!@C000000!@F-01 !@F+01!@C002525>!@C000000!@B";
	}

	if (m_cachedKeyLow != KeyCodeLow) {
		m_keyLow.SetWindowText(CString(LL14(L"低音域：", L"Low: ", L"Low: ", L"Low: ", L"Low: ", L"Low: ", L"Low: ", L"Low: ", L"Low: ", L"Low: ", L"Low: ", L"Low: ", L"Low: ", L"Low: ")) + KeyCodeLow);
		m_cachedKeyLow = KeyCodeLow;
	}
	if (m_cachedKeyMid != KeyCodeMid) {
		m_keyMid.SetWindowText(CString(LL14(L"中音域：", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ", L"Mid: ")) + KeyCodeMid);
		m_cachedKeyMid = KeyCodeMid;
	}
	if (m_cachedKeyHigh != KeyCodeHigh) {
		m_keyHigh.SetWindowText(CString(LL14(L"高音域：", L"High: ", L"High: ", L"High: ", L"High: ", L"High: ", L"High: ", L"High: ", L"High: ", L"High: ", L"High: ", L"High: ", L"High: ", L"High: ")) + KeyCodeHigh);
		m_cachedKeyHigh = KeyCodeHigh;
	}
	if (m_cachedKeyAll != KeyCodeAll) {
		m_keyAll.SetWindowText(CString(LL14(L"全音域：", L"All: ", L"All: ", L"All: ", L"All: ", L"All: ", L"All: ", L"All: ", L"All: ", L"All: ", L"All: ", L"All: ", L"All: ", L"All: ")) + KeyCodeAll);
		m_cachedKeyAll = KeyCodeAll;
	}


	CCustomBlurDialogExBase::OnTimer(nIDEvent);
}

void CEqualizer::OnBnClickedOk3()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.eq[0] = 100;
	savedata.eq[1] = 100;
	savedata.eq[2] = 100;
	savedata.eq[3] = 100;
	savedata.eq[4] = 100;
	savedata.eq[5] = 100;
	savedata.eq[6] = 100;
	savedata.eq[7] = 100;
	savedata.eq[8] = 100;
	savedata.eq[9] = 100;
	savedata.eq[10] = 100;
	savedata.eq[11] = 100;
	savedata.eq[12] = 100;
	savedata.eq[13] = 100;
	savedata.eq[14] = 100;
	m_s0.SetPos(200 - savedata.eq[0]);
	m_s1.SetPos(200 - savedata.eq[1]);
	m_s2.SetPos(200 - savedata.eq[2]);
	m_s3.SetPos(200 - savedata.eq[3]);
	m_s4.SetPos(200 - savedata.eq[4]);
	m_s5.SetPos(200 - savedata.eq[5]);
	m_s6.SetPos(200 - savedata.eq[6]);
	m_s7.SetPos(200 - savedata.eq[7]);
	m_s8.SetPos(200 - savedata.eq[8]);
	m_s9.SetPos(200 - savedata.eq[9]);
	m_s10.SetPos(200 - savedata.eq[10]);
	m_s11.SetPos(200 - savedata.eq[11]);
	m_s12.SetPos(200 - savedata.eq[12]);
	m_s13.SetPos(200 - savedata.eq[13]);
	m_s14.SetPos(200 - savedata.eq[14]);
	CString s;
	s.Format(L"%d", savedata.eq[0]);
	m_v0.SetWindowText(s);
	s.Format(L"%d", savedata.eq[1]);
	m_v1.SetWindowText(s);
	s.Format(L"%d", savedata.eq[2]);
	m_v2.SetWindowText(s);
	s.Format(L"%d", savedata.eq[3]);
	m_v3.SetWindowText(s);
	s.Format(L"%d", savedata.eq[4]);
	m_v4.SetWindowText(s);
	s.Format(L"%d", savedata.eq[5]);
	m_v5.SetWindowText(s);
	s.Format(L"%d", savedata.eq[6]);
	m_v6.SetWindowText(s);
	s.Format(L"%d", savedata.eq[7]);
	m_v7.SetWindowText(s);
	s.Format(L"%d", savedata.eq[8]);
	m_v8.SetWindowText(s);
	s.Format(L"%d", savedata.eq[9]);
	m_v9.SetWindowText(s);
	s.Format(L"%d", savedata.eq[10]);
	m_v10.SetWindowText(s);
	s.Format(L"%d", savedata.eq[11]);
	m_v11.SetWindowText(s);
	s.Format(L"%d", savedata.eq[12]);
	m_v12.SetWindowText(s);
	s.Format(L"%d", savedata.eq[13]);
	m_v13.SetWindowText(s);
	s.Format(L"%d", savedata.eq[14]);
	m_v14.SetWindowText(s);
}

BOOL CEqualizer::PreTranslateMessage(MSG* pMsg)
{
	// TODO: ここに特定なコードを追加するか、もしくは基底クラスを呼び出してください。
	m_tooltip.RelayEvent(pMsg);
	return CCustomBlurDialogExBase::PreTranslateMessage(pMsg);
}

void CEqualizer::OnBnClickedOk()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.eqwindow = 0;
	DestroyWindow();

}

void CEqualizer::OnBnClickedOk4()
{
	// TODO: ここにコントロール通知ハンドラー コードを追加します。
	savedata.eq[15] = 100;
	savedata.eq[16] = 100;
	savedata.eq[17] = 100;
	savedata.eq[18] = 100;
	savedata.eq[19] = 100;
	savedata.eq_reverb = 0;   // 0 = オフ
	savedata.eq_chorus = 0;   // 0 = オフ
	savedata.eq_delay = 0;    // 0 = オフ
	m_smaster.SetPos(200 - savedata.eq[15]);
	m_ssenmei.SetPos(200 - savedata.eq[16]);
	m_skoutei.SetPos(200 - savedata.eq[17]);
	m_smitsudo.SetPos(200 - savedata.eq[18]);
	m_srittai.SetPos(200 - savedata.eq[19]);

	m_reverb.SetPos(200 - savedata.eq_reverb);
	m_chorus.SetPos(200 - savedata.eq_chorus);
	m_delay.SetPos(200 - savedata.eq_delay);
	CString s;
	s.Format(L"%d", savedata.eq[15]);
	m_vmaster.SetWindowText(s);
	s.Format(L"%d", savedata.eq[16]);
	m_vsenmei.SetWindowText(s);
	s.Format(L"%d", savedata.eq[17]);
	m_vkoutei.SetWindowText(s);
	s.Format(L"%d", savedata.eq[18]);
	m_vmitsudo.SetWindowText(s);
	s.Format(L"%d", savedata.eq[19]);
	m_vrittai.SetWindowText(s);
	s.Format(L"%d", savedata.eq_reverb);
	m_reverbi.SetWindowText(s);
	s.Format(L"%d", savedata.eq_chorus);
	m_chorusi.SetWindowText(s);
	s.Format(L"%d", savedata.eq_delay);
	m_delayi.SetWindowText(s);
}

