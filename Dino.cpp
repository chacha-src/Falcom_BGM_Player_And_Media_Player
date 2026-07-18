// Dino.cpp : 実装ファイル
//

#include "stdafx.h"
#include "ogg.h"
#include "dino.h"


// CDino ダイアログ

IMPLEMENT_DYNAMIC(CDino, CCustomBlurDialogBase)

CDino::CDino(CWnd* pParent /*=NUL*/)
	: CCustomBlurDialogBase(CDino::IDD, pParent)
{

}

CDino::~CDino()
{
}

void CDino::DoDataExchange(CDataExchange* pDX)
{
	CCustomBlurDialogBase::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST1, m_list);
	DDX_Control(pDX, IDOK, m_okdummy);
}

#include "CImageBase.h"
BEGIN_MESSAGE_MAP(CDino, CCustomBlurDialogBase)
	ON_LBN_DBLCLK(IDC_LIST1, OnDblclkList1)
	cmn(CDino);


// CDino メッセージ ハンドラ
extern CString fnn;

TCHAR tidi[][128]={
L"dinow_01 失われしものたち",
L"dinow_02 Mark My Words",
L"dinow_03 精霊の賛歌",
L"dinow_04 FRONT LINE",
L"dinow_05 突破！",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD！",
L"dinow_08 土笛",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 タルシスとの出会い",
L"dinow_12 戦いは悲しみの果てに",
L"dinow_13 邂逅の時",
L"dinow_14 BURNING!",
L"dinow_15 鎮魂",
L"dinow_16 神の啓示",
L"dinow_17 試練の塔",
L"dinow_18 龍が逝く時",
L"dinow_19 風の塔",
L"dinow_20 地下祭室",
L"dinow_21 次元の迷宮",
L"dinow_22 ダリウスの塔",
L"dinow_23 あなたを愛して",
L"dinow_24 竪琴",
L"dinow_25 汚れなき時",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 フォルナ",
L"dinow_28 精霊の塔",
L"dinow_29 軍神",
L"dinow_30 DINOSAUR",
L"dinow_31 夢つむぎ",
L"dinow_32 風の紋章",
L"dinow_33 オルゴール",
L"★オープニング"
};

TCHAR tidi_en[][128]={
L"dinow_01 Those Lost",
L"dinow_02 Mark My Words",
L"dinow_03 Hymn of the Spirits",
L"dinow_04 FRONT LINE",
L"dinow_05 Break Through!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Clay Flute",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Meeting Tarsis",
L"dinow_12 Battle at the End of Sorrow",
L"dinow_13 Moment of Encounter",
L"dinow_14 BURNING!",
L"dinow_15 Requiem",
L"dinow_16 Divine Revelation",
L"dinow_17 Tower of Trial",
L"dinow_18 When the Dragon Departs",
L"dinow_19 Tower of Wind",
L"dinow_20 Underground Shrine",
L"dinow_21 Dimensional Labyrinth",
L"dinow_22 Tower of Darius",
L"dinow_23 I Love You",
L"dinow_24 Harp",
L"dinow_25 Time of Innocence",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Forna",
L"dinow_28 Tower of Spirits",
L"dinow_29 War God",
L"dinow_30 DINOSAUR",
L"dinow_31 Weaving Dreams",
L"dinow_32 Emblem of Wind",
L"dinow_33 Music Box",
L"★Opening"
};

TCHAR tidi_fr[][128]={
L"dinow_01 Ceux qui Sont Perdus",
L"dinow_02 Mark My Words",
L"dinow_03 Hymne des Esprits",
L"dinow_04 FRONT LINE",
L"dinow_05 Franchis!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Flûte de Terre",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Rencontre avec Tarsis",
L"dinow_12 BataiLe au Bout de la Tristesse",
L"dinow_13 Moment de Rencontre",
L"dinow_14 BURNING!",
L"dinow_15 Requiem",
L"dinow_16 Révélation Divine",
L"dinow_17 Tour de l'Épreuve",
L"dinow_18 Quand le Dragon S'en Va",
L"dinow_19 Tour du Vent",
L"dinow_20 Sanctuaire Souterrain",
L"dinow_21 Labyrinthe Dimensionnel",
L"dinow_22 Tour de Darius",
L"dinow_23 Je t'Aime",
L"dinow_24 Harpe",
L"dinow_25 Temps d'Innocence",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Forna",
L"dinow_28 Tour des Esprits",
L"dinow_29 Dieu de la Guerre",
L"dinow_30 DINOSAUR",
L"dinow_31 Tissage de Rêves",
L"dinow_32 Emblème du Vent",
L"dinow_33 Boîte à Musique",
L"★Ouverture"
};

TCHAR tidi_it[][128]={
L"dinow_01 Coloro che Si Sono Perduti",
L"dinow_02 Mark My Words",
L"dinow_03 Inno degli Spiriti",
L"dinow_04 FRONT LINE",
L"dinow_05 Attraversa!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Flauto di ArgiLa",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Incontro con Tarsis",
L"dinow_12 Battaglia aLa Fine del Dolore",
L"dinow_13 Momento deL'Incontro",
L"dinow_14 BURNING!",
L"dinow_15 Requiem",
L"dinow_16 Rivelazione Divina",
L"dinow_17 Torre deLa Prova",
L"dinow_18 Quando il Drago Se Ne Va",
L"dinow_19 Torre del Vento",
L"dinow_20 Santuario Sotterraneo",
L"dinow_21 Labirinto Dimensionale",
L"dinow_22 Torre di Dario",
L"dinow_23 Ti Amo",
L"dinow_24 Arpa",
L"dinow_25 Tempo di Innocenza",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Forna",
L"dinow_28 Torre degli Spiriti",
L"dinow_29 Dio deLa Guerra",
L"dinow_30 DINOSAUR",
L"dinow_31 Tessitura di Sogni",
L"dinow_32 Emblema del Vento",
L"dinow_33 CariLon",
L"★Apertura"
};

TCHAR tidi_es[][128]={
L"dinow_01 Los Perdidos",
L"dinow_02 Mark My Words",
L"dinow_03 Himno de los Espíritus",
L"dinow_04 FRONT LINE",
L"dinow_05 ¡Atraviesa!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Flauta de Barro",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Encuentro con Tarsis",
L"dinow_12 BataLa al Final del Dolor",
L"dinow_13 Momento del Encuentro",
L"dinow_14 BURNING!",
L"dinow_15 Requiem",
L"dinow_16 Revelación Divina",
L"dinow_17 Torre de la Prueba",
L"dinow_18 Cuando el Dragón Parte",
L"dinow_19 Torre del Viento",
L"dinow_20 Santuario Subterráneo",
L"dinow_21 Laberinto Dimensional",
L"dinow_22 Torre de Darío",
L"dinow_23 Te Amo",
L"dinow_24 Arpa",
L"dinow_25 Tiempo de Inocencia",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Forna",
L"dinow_28 Torre de los Espíritus",
L"dinow_29 Dios de la Guerra",
L"dinow_30 DINOSAUR",
L"dinow_31 Tejiendo Sueños",
L"dinow_32 Emblema del Viento",
L"dinow_33 Caja de Música",
L"★Apertura"
};

TCHAR tidi_ko[][128]={
L"dinow_01 잃어버린 자들",
L"dinow_02 Mark My Words",
L"dinow_03 정령의 찬가",
L"dinow_04 FRONT LINE",
L"dinow_05 돌파!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 흙피리",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 탈시스와의 만남",
L"dinow_12 전투는 슬픔의 끝에",
L"dinow_13 해후의 순간",
L"dinow_14 BURNING!",
L"dinow_15 진혼곡",
L"dinow_16 신의 계시",
L"dinow_17 시련의 탑",
L"dinow_18 용이 떠날 때",
L"dinow_19 바람의 탑",
L"dinow_20 지하 제단",
L"dinow_21 차원의 미궁",
L"dinow_22 다리우스의 탑",
L"dinow_23 당신을 사랑해",
L"dinow_24 하프",
L"dinow_25 더러움이 없는 때",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 포르나",
L"dinow_28 정령의 탑",
L"dinow_29 군신",
L"dinow_30 DINOSAUR",
L"dinow_31 꿈을 엮어",
L"dinow_32 바람의 문장",
L"dinow_33 오르골",
L"★오프닝"
};

TCHAR tidi_zh[][128]={
L"dinow_01 失落之物",
L"dinow_02 Mark My Words",
L"dinow_03 精灵赞歌",
L"dinow_04 FRONT LINE",
L"dinow_05 突破！",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 土笛",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 与塔尔西斯的相遇",
L"dinow_12 战斗在悲伤尽头",
L"dinow_13 邂逅之时",
L"dinow_14 BURNING!",
L"dinow_15 镇魂曲",
L"dinow_16 神启",
L"dinow_17 试炼之塔",
L"dinow_18 龙去之时",
L"dinow_19 风之塔",
L"dinow_20 地下祭室",
L"dinow_21 次元迷宫",
L"dinow_22 达里乌斯之塔",
L"dinow_23 我爱你",
L"dinow_24 竖琴",
L"dinow_25 无垢之时",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 福尔纳",
L"dinow_28 精灵之塔",
L"dinow_29 军神",
L"dinow_30 DINOSAUR",
L"dinow_31 纺梦",
L"dinow_32 风之纹章",
L"dinow_33 音乐盒",
L"★片头曲"
};

TCHAR tidi_ar[][128]={
L"dinow_01 الضائعون",
L"dinow_02 Mark My Words",
L"dinow_03 ترنيمة الأرواح",
L"dinow_04 FRONT LINE",
L"dinow_05 اقتحم!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 مزمار الطين",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 لقاء طرسيس",
L"dinow_12 المعركة في نهاية الحزن",
L"dinow_13 لحظة اللقاء",
L"dinow_14 BURNING!",
L"dinow_15 قداس",
L"dinow_16 الوحي الإلهي",
L"dinow_17 برج المحنة",
L"dinow_18 عندما يرحل التنين",
L"dinow_19 برج الريح",
L"dinow_20 الضريح السفلي",
L"dinow_21 متاهة الأبعاد",
L"dinow_22 برج داريوس",
L"dinow_23 أحبك",
L"dinow_24 القيثارة",
L"dinow_25 زمن البراءة",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 فورنا",
L"dinow_28 برج الأرواح",
L"dinow_29 إله الحرب",
L"dinow_30 DINOSAUR",
L"dinow_31 نسج الأحلام",
L"dinow_32 شعار الريح",
L"dinow_33 صندوق الموسيقى",
L"★المقدمة"
};

TCHAR tidi_ru[][128]={
L"dinow_01 Потерянные",
L"dinow_02 Mark My Words",
L"dinow_03 Гимн Духов",
L"dinow_04 FRONT LINE",
L"dinow_05 Прорвись!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Глиняная Флейта",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Встреча с Тарсисом",
L"dinow_12 Битва на Краю Печали",
L"dinow_13 Момент Встречи",
L"dinow_14 BURNING!",
L"dinow_15 Реквием",
L"dinow_16 Божественное Откровение",
L"dinow_17 Башня Испытаний",
L"dinow_18 Когда Дракон Уходит",
L"dinow_19 Башня Ветра",
L"dinow_20 Подземное Святилище",
L"dinow_21 Измеренческий Лабиринт",
L"dinow_22 Башня Дария",
L"dinow_23 Я Люблю Тебя",
L"dinow_24 Арфа",
L"dinow_25 Время Невинности",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Форна",
L"dinow_28 Башня Духов",
L"dinow_29 Бог Войны",
L"dinow_30 DINOSAUR",
L"dinow_31 Плетение Снов",
L"dinow_32 Эмблема Ветра",
L"dinow_33 Музыкальная Шкатулка",
L"★Заставка"
};

TCHAR tidi_de[][128]={
L"dinow_01 Die Verlorenen",
L"dinow_02 Mark My Words",
L"dinow_03 Hymne der Geister",
L"dinow_04 FRONT LINE",
L"dinow_05 Durchbrich!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Lehmflöte",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Begegnung mit Tarsis",
L"dinow_12 Kampf am Ende der Trauer",
L"dinow_13 Moment der Begegnung",
L"dinow_14 BURNING!",
L"dinow_15 Requiem",
L"dinow_16 Göttliche Offenbarung",
L"dinow_17 Turm der Prüfung",
L"dinow_18 Als der Drache Geht",
L"dinow_19 Turm des Windes",
L"dinow_20 Unterirdisches Heiligtum",
L"dinow_21 Dimensionslabyrinth",
L"dinow_22 Turm des Darius",
L"dinow_23 Ich Liebe Dich",
L"dinow_24 Harfe",
L"dinow_25 Zeit der Unschuld",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Forna",
L"dinow_28 Turm der Geister",
L"dinow_29 Kriegsgott",
L"dinow_30 DINOSAUR",
L"dinow_31 Träume Weben",
L"dinow_32 Emblem des Windes",
L"dinow_33 Spieluhr",
L"★Vorspann"
};

TCHAR tidi_pt[][128]={
L"dinow_01 Os Perdidos",
L"dinow_02 Mark My Words",
L"dinow_03 Hino dos Espíritos",
L"dinow_04 FRONT LINE",
L"dinow_05 Atravessa!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Flauta de Barro",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Encontro com Tarsis",
L"dinow_12 Batalha no Fim da Tristeza",
L"dinow_13 Momento do Encontro",
L"dinow_14 BURNING!",
L"dinow_15 Requiem",
L"dinow_16 Revelação Divina",
L"dinow_17 Torre da Prova",
L"dinow_18 Quando o Dragão Parte",
L"dinow_19 Torre do Vento",
L"dinow_20 Santuário Subterrâneo",
L"dinow_21 Labirinto Dimensional",
L"dinow_22 Torre de Dario",
L"dinow_23 Eu Te Amo",
L"dinow_24 Harpa",
L"dinow_25 Tempo de Inocência",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Forna",
L"dinow_28 Torre dos Espíritos",
L"dinow_29 Deus da Guerra",
L"dinow_30 DINOSAUR",
L"dinow_31 Tecendo Sonhos",
L"dinow_32 Emblema do Vento",
L"dinow_33 Caixa de Música",
L"★Abertura"
};

TCHAR tidi_nl[][128]={
L"dinow_01 De Verlorenen",
L"dinow_02 Mark My Words",
L"dinow_03 Hymne van de Geesten",
L"dinow_04 FRONT LINE",
L"dinow_05 Doorbreek!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Kleifluit",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Ontmoeting met Tarsis",
L"dinow_12 Strijd aan het Einde van Verdriet",
L"dinow_13 Moment van Ontmoeting",
L"dinow_14 BURNING!",
L"dinow_15 Requiem",
L"dinow_16 Goddelijke Openbaring",
L"dinow_17 Toren van Beproeving",
L"dinow_18 Als de Draak Vertrekt",
L"dinow_19 Toren van de Wind",
L"dinow_20 Ondergronds Heiligdom",
L"dinow_21 Dimensioneel Labyrint",
L"dinow_22 Toren van Darius",
L"dinow_23 Ik Hou van Je",
L"dinow_24 Harp",
L"dinow_25 Tijd van Onschuld",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Forna",
L"dinow_28 Toren der Geesten",
L"dinow_29 Oorlogsgod",
L"dinow_30 DINOSAUR",
L"dinow_31 Dromen Weven",
L"dinow_32 Embleem van de Wind",
L"dinow_33 Muziekdoos",
L"★Opening"
};

TCHAR tidi_pl[][128]={
L"dinow_01 Zgubieni",
L"dinow_02 Mark My Words",
L"dinow_03 Hymn Duchów",
L"dinow_04 FRONT LINE",
L"dinow_05 Przejdź!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Gliniana Flet",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Spotkanie z Tarsisem",
L"dinow_12 Bitwa na Krańcu Smutku",
L"dinow_13 Chwila Spotkania",
L"dinow_14 BURNING!",
L"dinow_15 Requiem",
L"dinow_16 Boskie Objawienie",
L"dinow_17 Wieża Próby",
L"dinow_18 Gdy Smok Odszedł",
L"dinow_19 Wieża Wiatru",
L"dinow_20 Podziemne Sanktuarium",
L"dinow_21 Labirynt Wymiarowy",
L"dinow_22 Wieża Dariusza",
L"dinow_23 Kocham Cię",
L"dinow_24 Harfa",
L"dinow_25 Czas Niewinności",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Forna",
L"dinow_28 Wieża Duchów",
L"dinow_29 Bóg Wojny",
L"dinow_30 DINOSAUR",
L"dinow_31 Tkanie Snów",
L"dinow_32 Emblemat Wiatru",
L"dinow_33 Skrzynka Muzyczna",
L"★Otwarcie"
};

TCHAR tidi_tr[][128]={
L"dinow_01 Kayıplar",
L"dinow_02 Mark My Words",
L"dinow_03 Ruhların İlahisi",
L"dinow_04 FRONT LINE",
L"dinow_05 Geç!",
L"dinow_06 A TEMPLE",
L"dinow_07 GOD!",
L"dinow_08 Toprak Flüt",
L"dinow_09 EXCITING SHOP",
L"dinow_10 INTO THE CASTLE",
L"dinow_11 Tarsis ile Karşılaşma",
L"dinow_12 Kederin Sonundaki Savaş",
L"dinow_13 Karşılaşma Anı",
L"dinow_14 BURNING!",
L"dinow_15 Requiem",
L"dinow_16 İlahi Vahiy",
L"dinow_17 Deneme Kulesi",
L"dinow_18 Ejderha Gittiğinde",
L"dinow_19 Rüzgar Kulesi",
L"dinow_20 Yer Altı Tapınağı",
L"dinow_21 Boyut Labirenti",
L"dinow_22 Darius Kulesi",
L"dinow_23 Seni Seviyorum",
L"dinow_24 Arp",
L"dinow_25 Masumiyet Zamanı",
L"dinow_26 THE MASCLE MAN",
L"dinow_27 Forna",
L"dinow_28 Ruhlar Kulesi",
L"dinow_29 Savaş Tanrısı",
L"dinow_30 DINOSAUR",
L"dinow_31 HayaLeri Dokuma",
L"dinow_32 Rüzgar Amblemi",
L"dinow_33 Müzik Kutusu",
L"★Açılış"
};

double di[34][2]={
	{22.0138095238095,181.765351473923},
	{9.08820861678005,86.2321315192744},
	{10.3009070294785,168.109569160998},
	{9.56086167800454,68.3846258503401},
	{8.93421768707483,156.790839002268},
	{10.2299546485261,178.878843537415},
	{33.8431065759637,144.95537414966},
	{0.0,0.0},
	{7.16244897959183,130.49537414966},
	{9.3097052154195,113.093537414966},
	{11.7062131519274,129.33},
	{7.65356009070295,83.9453741496598},
	{8.9562358276644,97.219433106576},
	{4.27621315192744,138.798526077098},
	{0.0,0.0},
	{4.63199546485261,108.084285714286},
	{21.455283446712,168.808344671202},
	{45.0247392290249,102.797800453515},
	{3.32027210884354,242.057142857143},
	{14.3692743764172,80.6851473922902},
	{15.9714965986395,71.0846031746031},
	{16.7838548752834,87.2922902494331},
	{25.8584126984127,151.348548752834},
	{0.0,0.0},
	{22.0648526077098,88.363514739229},
	{17.430589569161,120.630589569161},
	{22.9173469387755,136.020158730159},
	{6.70092970521542,129.01514739229},
	{18.1054195011338,51.105306122449},
	{21.3657596371882,113.737709750567},
	{13.3610657596372,206.219138321996},
	{82.3797732426304,161.312947845805},
	{0.0,0.0},
	{0.0,0.0}
};

static inline CString DinoTrack(int i) {
	switch (savedata.lang) {
		case 0: return CString(tidi[i]);
		case 1: return CString(tidi_en[i]);
		case 2: return CString(tidi_fr[i]);
		case 3: return CString(tidi_it[i]);
		case 4: return CString(tidi_es[i]);
		case 5: return CString(tidi_ko[i]);
		case 6: return CString(tidi_zh[i]);
		case 7: return CString(tidi_ar[i]);
		case 8: return CString(tidi_ru[i]);
		case 9: return CString(tidi_de[i]);
		case 10: return CString(tidi_pt[i]);
		case 11: return CString(tidi_nl[i]);
		case 12: return CString(tidi_pl[i]);
		case 13: return CString(tidi_tr[i]);
		default: return CString(tidi_en[i]);
	}
}

CString CDino::Gett(int a){
	CString s,ss,sss;int aa;
	s = DinoTrack(a);
	ss=s.Left(8);ss.TrimRight();
	sss=ss.Right(2);aa=_tstoi(sss)-1;
	loop1=(int)(di[aa][0]*44100.0);
	loop2=(int)(di[aa][1]*44100.0)-loop1;
	fnn=s.Mid(9);
	return ss;
}

/////////////////////////////////////////////////////////////////////////////
// CZWEIII メッセージ ハンドラ
void CDino::OnDblclkList1() 
{
	// TODO: この位置にコントロール通知ハンドラ用のコードを追加してください
	int idx=m_list.GetItemData(m_list.GetCurSel());
	CString s,ss,sss;int aa;	s = DinoTrack(idx);
	ret=s.Left(8); ret.TrimRight();
	ret2=m_list.GetCurSel();ss=ret;
	sss=ss.Right(2);aa=_tstoi(sss)-1;
	loop1=(int)(di[aa][0]*44100.0);
	loop2=(int)(di[aa][1]*44100.0)-loop1;
	if(loop1)
	loop1 -= 44100;
	//loop2 -= 44100;
#if UNICODE
	if(s.Left(1)=="★"){
		fnn=s.Mid(1);
#else
	if(s.Left(2)=="★"){
		fnn=s.Mid(2);
#endif
	}else{
		fnn=s.Mid(9);
	}
	EndDialog(1567);
}

BOOL CDino::OnInitDialog() 
{
	CCustomBlurDialogBase::OnInitDialog();
	SetWindowText(LL14(L"ダイナソア リザレクション", L"dinosaur Resurrection", L"Résurrection Dinosaure", L"Resurrezione Dinosauro", L"Resurrección Dinosaurio", L"공룡 부활", L"恐龙复活", L"dinosaur Resurrection", L"Динозавр: Воскрешение", L"dinosaurier Auferstehung", L"Ressurreição Dinossauro", L"dinosaur Resurrection", L"dinozaur Zmartwychwstanie", L"dinozor Diriliş"));
	SetDlgItemText(IDOK, LL14(L"閉じる", L"Close", L"Fermer", L"Chiudi", L"Cerrar", L"닫기", L"关闭", L"إغلاق", L"Закрыть", L"Schließen", L"Fechar", L"Sluiten", L"Zamknij", L"Kapat"));
	
	// TODO: この位置に初期化の補足処理を追加してください
	int dx;
	for(int i=0;i<(34);i++)
	{
		CString s = DinoTrack(i);
		dx= m_list.AddString(s);
		m_list.SetItemData(dx,i);	
	}

	m_list.SetCurSel(0);
	if(ret2!=0) 
//		if(ret>65) m_list.SetCurSel(ret);
//		else m_list.SetCurSel(ret-1);
		m_list.SetCurSel(ret2);
	m_list.SetFocus();
	return FALSE;  // コントロールにフォーカスを設定しないとき、戻り値は TRUE となります
	              // 例外: OCX プロパティ ページの戻り値は FALSE となります
}
